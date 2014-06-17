/*
 * Some low level IO code, and hacks for various block layer limitations
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "bset.h"
#include "debug.h"

#include <linux/blkdev.h>

static unsigned bch_bio_max_sectors(struct bio *bio)
{
	struct request_queue *q = bdev_get_queue(bio->bi_bdev);
	struct bio_vec bv;
	struct bvec_iter iter;
	unsigned ret = 0, seg = 0;

	if (bio->bi_rw & REQ_DISCARD)
		return min(bio_sectors(bio), q->limits.max_discard_sectors);

	bio_for_each_segment(bv, bio, iter) {
		struct bvec_merge_data bvm = {
			.bi_bdev	= bio->bi_bdev,
			.bi_sector	= bio->bi_iter.bi_sector,
			.bi_size	= ret << 9,
			.bi_rw		= bio->bi_rw,
		};

		if (seg == min_t(unsigned, BIO_MAX_PAGES,
				 queue_max_segments(q)))
			break;

		if (q->merge_bvec_fn &&
		    q->merge_bvec_fn(q, &bvm, &bv) < (int) bv.bv_len)
			break;

		seg++;
		ret += bv.bv_len >> 9;
	}

	ret = min(ret, queue_max_sectors(q));

	WARN_ON(!ret);
	ret = max_t(int, ret, bio_iovec(bio).bv_len >> 9);

	return ret;
}

static void bch_bio_submit_split_done(struct closure *cl)
{
	struct bio_split_hook *s = container_of(cl, struct bio_split_hook, cl);

	s->bio->bi_end_io = s->bi_end_io;
	s->bio->bi_private = s->bi_private;
	bio_endio_nodec(s->bio, 0);

	closure_debug_destroy(&s->cl);
	mempool_free(s, s->p->bio_split_hook);
}

static void bch_bio_submit_split_endio(struct bio *bio, int error)
{
	struct closure *cl = bio->bi_private;
	struct bio_split_hook *s = container_of(cl, struct bio_split_hook, cl);

	if (error)
		clear_bit(BIO_UPTODATE, &s->bio->bi_flags);

	bio_put(bio);
	closure_put(cl);
}

void bch_generic_make_request(struct bio *bio, struct bio_split_pool *p)
{
	struct bio_split_hook *s;
	struct bio *n;

	if (!bio_has_data(bio) && !(bio->bi_rw & REQ_DISCARD))
		goto submit;

	if (bio_sectors(bio) <= bch_bio_max_sectors(bio))
		goto submit;

	s = mempool_alloc(p->bio_split_hook, GFP_NOIO);
	closure_init(&s->cl, NULL);

	s->bio		= bio;
	s->p		= p;
	s->bi_end_io	= bio->bi_end_io;
	s->bi_private	= bio->bi_private;
	bio_get(bio);

	do {
		n = bio_next_split(bio, bch_bio_max_sectors(bio),
				   GFP_NOIO, s->p->bio_split);

		n->bi_end_io	= bch_bio_submit_split_endio;
		n->bi_private	= &s->cl;

		closure_get(&s->cl);
		generic_make_request(n);
	} while (n != bio);

	continue_at(&s->cl, bch_bio_submit_split_done, NULL);
submit:
	generic_make_request(bio);
}

/* Bios with headers */

void bch_bbio_free(struct bio *bio, struct cache_set *c)
{
	struct bbio *b = container_of(bio, struct bbio, bio);
	mempool_free(b, c->bio_meta);
}

struct bio *bch_bbio_alloc(struct cache_set *c)
{
	struct bbio *b = mempool_alloc(c->bio_meta, GFP_NOIO);
	struct bio *bio = &b->bio;

	bio_init(bio);
	bio->bi_flags		|= BIO_POOL_NONE << BIO_POOL_OFFSET;
	bio->bi_max_vecs	 = bucket_pages(c);
	bio->bi_io_vec		 = bio->bi_inline_vecs;

	return bio;
}

void __bch_submit_bbio(struct bio *bio, struct cache_set *c)
{
	struct bbio *b = container_of(bio, struct bbio, bio);

	bio->bi_iter.bi_sector	= PTR_OFFSET(&b->key, 0);
	bio->bi_bdev		= PTR_CACHE(c, &b->key, 0)->bdev;

	b->submit_time_us = local_clock_us();
	closure_bio_submit(bio, bio->bi_private, PTR_CACHE(c, &b->key, 0));
}

void bch_submit_bbio(struct bio *bio, struct cache_set *c,
		     struct bkey *k, unsigned ptr)
{
	struct bbio *b = container_of(bio, struct bbio, bio);
	bch_bkey_copy_single_ptr(&b->key, k, ptr);
	__bch_submit_bbio(bio, c);
}

/* IO errors */

void bch_count_io_errors(struct cache *ca, int error, const char *m)
{
	/*
	 * The halflife of an error is:
	 * log2(1/2)/log2(127/128) * refresh ~= 88 * refresh
	 */

	if (ca->set->error_decay) {
		unsigned count = atomic_inc_return(&ca->io_count);

		while (count > ca->set->error_decay) {
			unsigned errors;
			unsigned old = count;
			unsigned new = count - ca->set->error_decay;

			/*
			 * First we subtract refresh from count; each time we
			 * succesfully do so, we rescale the errors once:
			 */

			count = atomic_cmpxchg(&ca->io_count, old, new);

			if (count == old) {
				count = new;

				errors = atomic_read(&ca->io_errors);
				do {
					old = errors;
					new = ((uint64_t) errors * 127) / 128;
					errors = atomic_cmpxchg(&ca->io_errors,
								old, new);
				} while (old != errors);
			}
		}
	}

	if (error) {
		char buf[BDEVNAME_SIZE];
		unsigned errors = atomic_add_return(1 << IO_ERROR_SHIFT,
						    &ca->io_errors);
		errors >>= IO_ERROR_SHIFT;

		if (errors < ca->set->error_limit)
			pr_err("%s: IO error on %s, recovering",
			       bdevname(ca->bdev, buf), m);
		else
			bch_cache_set_error(ca->set,
					    "%s: too many IO errors %s",
					    bdevname(ca->bdev, buf), m);
	}
}

void bch_bbio_count_io_errors(struct cache_set *c, struct bio *bio,
			      int error, const char *m)
{
	struct bbio *b = container_of(bio, struct bbio, bio);
	struct cache *ca = PTR_CACHE(c, &b->key, 0);

	unsigned threshold = bio->bi_rw & REQ_WRITE
		? c->congested_write_threshold_us
		: c->congested_read_threshold_us;

	if (threshold) {
		unsigned t = local_clock_us();

		int us = t - b->submit_time_us;
		int congested = atomic_read(&c->congested);

		if (us > (int) threshold) {
			int ms = us / 1024;
			c->congested_last_us = t;

			ms = min(ms, CONGESTED_MAX + congested);
			atomic_sub(ms, &c->congested);
		} else if (congested < 0)
			atomic_inc(&c->congested);
	}

	bch_count_io_errors(ca, error, m);
}

void bch_bbio_endio(struct cache_set *c, struct bio *bio,
		    int error, const char *m)
{
	struct closure *cl = bio->bi_private;

	bch_bbio_count_io_errors(c, bio, error, m);
	bio_put(bio);
	closure_put(cl);
}
