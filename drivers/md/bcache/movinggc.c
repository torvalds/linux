/*
 * Moving/copying garbage collector
 *
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "btree.h"
#include "debug.h"
#include "request.h"

#include <trace/events/bcache.h>

struct moving_io {
	struct keybuf_key	*w;
	struct search		s;
	struct bbio		bio;
};

static bool moving_pred(struct keybuf *buf, struct bkey *k)
{
	struct cache_set *c = container_of(buf, struct cache_set,
					   moving_gc_keys);
	unsigned i;

	for (i = 0; i < KEY_PTRS(k); i++) {
		struct cache *ca = PTR_CACHE(c, k, i);
		struct bucket *g = PTR_BUCKET(c, k, i);

		if (GC_SECTORS_USED(g) < ca->gc_move_threshold)
			return true;
	}

	return false;
}

/* Moving GC - IO loop */

static void moving_io_destructor(struct closure *cl)
{
	struct moving_io *io = container_of(cl, struct moving_io, s.cl);
	kfree(io);
}

static void write_moving_finish(struct closure *cl)
{
	struct moving_io *io = container_of(cl, struct moving_io, s.cl);
	struct bio *bio = &io->bio.bio;
	struct bio_vec *bv = bio_iovec_idx(bio, bio->bi_vcnt);

	while (bv-- != bio->bi_io_vec)
		__free_page(bv->bv_page);

	if (io->s.op.insert_collision)
		trace_bcache_gc_copy_collision(&io->w->key);

	bch_keybuf_del(&io->s.op.c->moving_gc_keys, io->w);

	atomic_dec_bug(&io->s.op.c->in_flight);
	closure_wake_up(&io->s.op.c->moving_gc_wait);

	closure_return_with_destructor(cl, moving_io_destructor);
}

static void read_moving_endio(struct bio *bio, int error)
{
	struct moving_io *io = container_of(bio->bi_private,
					    struct moving_io, s.cl);

	if (error)
		io->s.error = error;

	bch_bbio_endio(io->s.op.c, bio, error, "reading data to move");
}

static void moving_init(struct moving_io *io)
{
	struct bio *bio = &io->bio.bio;

	bio_init(bio);
	bio_get(bio);
	bio_set_prio(bio, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0));

	bio->bi_size		= KEY_SIZE(&io->w->key) << 9;
	bio->bi_max_vecs	= DIV_ROUND_UP(KEY_SIZE(&io->w->key),
					       PAGE_SECTORS);
	bio->bi_private		= &io->s.cl;
	bio->bi_io_vec		= bio->bi_inline_vecs;
	bch_bio_map(bio, NULL);
}

static void write_moving(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);
	struct moving_io *io = container_of(s, struct moving_io, s);

	if (!s->error) {
		moving_init(io);

		io->bio.bio.bi_sector	= KEY_START(&io->w->key);
		s->op.lock		= -1;
		s->op.write_prio	= 1;
		s->op.cache_bio		= &io->bio.bio;

		s->writeback		= KEY_DIRTY(&io->w->key);
		s->op.csum		= KEY_CSUM(&io->w->key);

		s->op.type = BTREE_REPLACE;
		bkey_copy(&s->op.replace, &io->w->key);

		closure_init(&s->op.cl, cl);
		bch_insert_data(&s->op.cl);
	}

	continue_at(cl, write_moving_finish, NULL);
}

static void read_moving_submit(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);
	struct moving_io *io = container_of(s, struct moving_io, s);
	struct bio *bio = &io->bio.bio;

	bch_submit_bbio(bio, s->op.c, &io->w->key, 0);

	continue_at(cl, write_moving, bch_gc_wq);
}

static void read_moving(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, moving_gc);
	struct keybuf_key *w;
	struct moving_io *io;
	struct bio *bio;

	/* XXX: if we error, background writeback could stall indefinitely */

	while (!test_bit(CACHE_SET_STOPPING, &c->flags)) {
		w = bch_keybuf_next_rescan(c, &c->moving_gc_keys, &MAX_KEY);
		if (!w)
			break;

		io = kzalloc(sizeof(struct moving_io) + sizeof(struct bio_vec)
			     * DIV_ROUND_UP(KEY_SIZE(&w->key), PAGE_SECTORS),
			     GFP_KERNEL);
		if (!io)
			goto err;

		w->private	= io;
		io->w		= w;
		io->s.op.inode	= KEY_INODE(&w->key);
		io->s.op.c	= c;

		moving_init(io);
		bio = &io->bio.bio;

		bio->bi_rw	= READ;
		bio->bi_end_io	= read_moving_endio;

		if (bch_bio_alloc_pages(bio, GFP_KERNEL))
			goto err;

		trace_bcache_gc_copy(&w->key);

		closure_call(&io->s.cl, read_moving_submit, NULL, &c->gc.cl);

		if (atomic_inc_return(&c->in_flight) >= 64) {
			closure_wait_event(&c->moving_gc_wait, cl,
					   atomic_read(&c->in_flight) < 64);
			continue_at(cl, read_moving, bch_gc_wq);
		}
	}

	if (0) {
err:		if (!IS_ERR_OR_NULL(w->private))
			kfree(w->private);

		bch_keybuf_del(&c->moving_gc_keys, w);
	}

	closure_return(cl);
}

static bool bucket_cmp(struct bucket *l, struct bucket *r)
{
	return GC_SECTORS_USED(l) < GC_SECTORS_USED(r);
}

static unsigned bucket_heap_top(struct cache *ca)
{
	return GC_SECTORS_USED(heap_peek(&ca->heap));
}

void bch_moving_gc(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, gc.cl);
	struct cache *ca;
	struct bucket *b;
	unsigned i;

	if (!c->copy_gc_enabled)
		closure_return(cl);

	mutex_lock(&c->bucket_lock);

	for_each_cache(ca, c, i) {
		unsigned sectors_to_move = 0;
		unsigned reserve_sectors = ca->sb.bucket_size *
			min(fifo_used(&ca->free), ca->free.size / 2);

		ca->heap.used = 0;

		for_each_bucket(b, ca) {
			if (!GC_SECTORS_USED(b))
				continue;

			if (!heap_full(&ca->heap)) {
				sectors_to_move += GC_SECTORS_USED(b);
				heap_add(&ca->heap, b, bucket_cmp);
			} else if (bucket_cmp(b, heap_peek(&ca->heap))) {
				sectors_to_move -= bucket_heap_top(ca);
				sectors_to_move += GC_SECTORS_USED(b);

				ca->heap.data[0] = b;
				heap_sift(&ca->heap, 0, bucket_cmp);
			}
		}

		while (sectors_to_move > reserve_sectors) {
			heap_pop(&ca->heap, b, bucket_cmp);
			sectors_to_move -= GC_SECTORS_USED(b);
		}

		ca->gc_move_threshold = bucket_heap_top(ca);

		pr_debug("threshold %u", ca->gc_move_threshold);
	}

	mutex_unlock(&c->bucket_lock);

	c->moving_gc_keys.last_scanned = ZERO_KEY;

	closure_init(&c->moving_gc, cl);
	read_moving(&c->moving_gc);

	closure_return(cl);
}

void bch_moving_init_cache_set(struct cache_set *c)
{
	bch_keybuf_init(&c->moving_gc_keys, moving_pred);
}
