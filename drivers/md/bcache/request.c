// SPDX-License-Identifier: GPL-2.0
/*
 * Main bcache entry point - handle a read or a write request and decide what to
 * do with it; the make_request functions are called by the block layer.
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "btree.h"
#include "debug.h"
#include "request.h"
#include "writeback.h"

#include <linux/module.h>
#include <linux/hash.h>
#include <linux/random.h>
#include <linux/backing-dev.h>

#include <trace/events/bcache.h>

#define CUTOFF_CACHE_ADD	95
#define CUTOFF_CACHE_READA	90

struct kmem_cache *bch_search_cache;

static void bch_data_insert_start(struct closure *cl);

static unsigned int cache_mode(struct cached_dev *dc)
{
	return BDEV_CACHE_MODE(&dc->sb);
}

static bool verify(struct cached_dev *dc)
{
	return dc->verify;
}

static void bio_csum(struct bio *bio, struct bkey *k)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	uint64_t csum = 0;

	bio_for_each_segment(bv, bio, iter) {
		void *d = kmap(bv.bv_page) + bv.bv_offset;

		csum = bch_crc64_update(csum, d, bv.bv_len);
		kunmap(bv.bv_page);
	}

	k->ptr[KEY_PTRS(k)] = csum & (~0ULL >> 1);
}

/* Insert data into cache */

static void bch_data_insert_keys(struct closure *cl)
{
	struct data_insert_op *op = container_of(cl, struct data_insert_op, cl);
	atomic_t *journal_ref = NULL;
	struct bkey *replace_key = op->replace ? &op->replace_key : NULL;
	int ret;

	if (!op->replace)
		journal_ref = bch_journal(op->c, &op->insert_keys,
					  op->flush_journal ? cl : NULL);

	ret = bch_btree_insert(op->c, &op->insert_keys,
			       journal_ref, replace_key);
	if (ret == -ESRCH) {
		op->replace_collision = true;
	} else if (ret) {
		op->status		= BLK_STS_RESOURCE;
		op->insert_data_done	= true;
	}

	if (journal_ref)
		atomic_dec_bug(journal_ref);

	if (!op->insert_data_done) {
		continue_at(cl, bch_data_insert_start, op->wq);
		return;
	}

	bch_keylist_free(&op->insert_keys);
	closure_return(cl);
}

static int bch_keylist_realloc(struct keylist *l, unsigned int u64s,
			       struct cache_set *c)
{
	size_t oldsize = bch_keylist_nkeys(l);
	size_t newsize = oldsize + u64s;

	/*
	 * The journalling code doesn't handle the case where the keys to insert
	 * is bigger than an empty write: If we just return -ENOMEM here,
	 * bch_data_insert_keys() will insert the keys created so far
	 * and finish the rest when the keylist is empty.
	 */
	if (newsize * sizeof(uint64_t) > block_bytes(c->cache) - sizeof(struct jset))
		return -ENOMEM;

	return __bch_keylist_realloc(l, u64s);
}

static void bch_data_invalidate(struct closure *cl)
{
	struct data_insert_op *op = container_of(cl, struct data_insert_op, cl);
	struct bio *bio = op->bio;

	pr_debug("invalidating %i sectors from %llu\n",
		 bio_sectors(bio), (uint64_t) bio->bi_iter.bi_sector);

	while (bio_sectors(bio)) {
		unsigned int sectors = min(bio_sectors(bio),
				       1U << (KEY_SIZE_BITS - 1));

		if (bch_keylist_realloc(&op->insert_keys, 2, op->c))
			goto out;

		bio->bi_iter.bi_sector	+= sectors;
		bio->bi_iter.bi_size	-= sectors << 9;

		bch_keylist_add(&op->insert_keys,
				&KEY(op->inode,
				     bio->bi_iter.bi_sector,
				     sectors));
	}

	op->insert_data_done = true;
	/* get in bch_data_insert() */
	bio_put(bio);
out:
	continue_at(cl, bch_data_insert_keys, op->wq);
}

static void bch_data_insert_error(struct closure *cl)
{
	struct data_insert_op *op = container_of(cl, struct data_insert_op, cl);

	/*
	 * Our data write just errored, which means we've got a bunch of keys to
	 * insert that point to data that wasn't successfully written.
	 *
	 * We don't have to insert those keys but we still have to invalidate
	 * that region of the cache - so, if we just strip off all the pointers
	 * from the keys we'll accomplish just that.
	 */

	struct bkey *src = op->insert_keys.keys, *dst = op->insert_keys.keys;

	while (src != op->insert_keys.top) {
		struct bkey *n = bkey_next(src);

		SET_KEY_PTRS(src, 0);
		memmove(dst, src, bkey_bytes(src));

		dst = bkey_next(dst);
		src = n;
	}

	op->insert_keys.top = dst;

	bch_data_insert_keys(cl);
}

static void bch_data_insert_endio(struct bio *bio)
{
	struct closure *cl = bio->bi_private;
	struct data_insert_op *op = container_of(cl, struct data_insert_op, cl);

	if (bio->bi_status) {
		/* TODO: We could try to recover from this. */
		if (op->writeback)
			op->status = bio->bi_status;
		else if (!op->replace)
			set_closure_fn(cl, bch_data_insert_error, op->wq);
		else
			set_closure_fn(cl, NULL, NULL);
	}

	bch_bbio_endio(op->c, bio, bio->bi_status, "writing data to cache");
}

static void bch_data_insert_start(struct closure *cl)
{
	struct data_insert_op *op = container_of(cl, struct data_insert_op, cl);
	struct bio *bio = op->bio, *n;

	if (op->bypass)
		return bch_data_invalidate(cl);

	if (atomic_sub_return(bio_sectors(bio), &op->c->sectors_to_gc) < 0)
		wake_up_gc(op->c);

	/*
	 * Journal writes are marked REQ_PREFLUSH; if the original write was a
	 * flush, it'll wait on the journal write.
	 */
	bio->bi_opf &= ~(REQ_PREFLUSH|REQ_FUA);

	do {
		unsigned int i;
		struct bkey *k;
		struct bio_set *split = &op->c->bio_split;

		/* 1 for the device pointer and 1 for the chksum */
		if (bch_keylist_realloc(&op->insert_keys,
					3 + (op->csum ? 1 : 0),
					op->c)) {
			continue_at(cl, bch_data_insert_keys, op->wq);
			return;
		}

		k = op->insert_keys.top;
		bkey_init(k);
		SET_KEY_INODE(k, op->inode);
		SET_KEY_OFFSET(k, bio->bi_iter.bi_sector);

		if (!bch_alloc_sectors(op->c, k, bio_sectors(bio),
				       op->write_point, op->write_prio,
				       op->writeback))
			goto err;

		n = bio_next_split(bio, KEY_SIZE(k), GFP_NOIO, split);

		n->bi_end_io	= bch_data_insert_endio;
		n->bi_private	= cl;

		if (op->writeback) {
			SET_KEY_DIRTY(k, true);

			for (i = 0; i < KEY_PTRS(k); i++)
				SET_GC_MARK(PTR_BUCKET(op->c, k, i),
					    GC_MARK_DIRTY);
		}

		SET_KEY_CSUM(k, op->csum);
		if (KEY_CSUM(k))
			bio_csum(n, k);

		trace_bcache_cache_insert(k);
		bch_keylist_push(&op->insert_keys);

		bio_set_op_attrs(n, REQ_OP_WRITE, 0);
		bch_submit_bbio(n, op->c, k, 0);
	} while (n != bio);

	op->insert_data_done = true;
	continue_at(cl, bch_data_insert_keys, op->wq);
	return;
err:
	/* bch_alloc_sectors() blocks if s->writeback = true */
	BUG_ON(op->writeback);

	/*
	 * But if it's not a writeback write we'd rather just bail out if
	 * there aren't any buckets ready to write to - it might take awhile and
	 * we might be starving btree writes for gc or something.
	 */

	if (!op->replace) {
		/*
		 * Writethrough write: We can't complete the write until we've
		 * updated the index. But we don't want to delay the write while
		 * we wait for buckets to be freed up, so just invalidate the
		 * rest of the write.
		 */
		op->bypass = true;
		return bch_data_invalidate(cl);
	} else {
		/*
		 * From a cache miss, we can just insert the keys for the data
		 * we have written or bail out if we didn't do anything.
		 */
		op->insert_data_done = true;
		bio_put(bio);

		if (!bch_keylist_empty(&op->insert_keys))
			continue_at(cl, bch_data_insert_keys, op->wq);
		else
			closure_return(cl);
	}
}

/**
 * bch_data_insert - stick some data in the cache
 * @cl: closure pointer.
 *
 * This is the starting point for any data to end up in a cache device; it could
 * be from a normal write, or a writeback write, or a write to a flash only
 * volume - it's also used by the moving garbage collector to compact data in
 * mostly empty buckets.
 *
 * It first writes the data to the cache, creating a list of keys to be inserted
 * (if the data had to be fragmented there will be multiple keys); after the
 * data is written it calls bch_journal, and after the keys have been added to
 * the next journal write they're inserted into the btree.
 *
 * It inserts the data in op->bio; bi_sector is used for the key offset,
 * and op->inode is used for the key inode.
 *
 * If op->bypass is true, instead of inserting the data it invalidates the
 * region of the cache represented by op->bio and op->inode.
 */
void bch_data_insert(struct closure *cl)
{
	struct data_insert_op *op = container_of(cl, struct data_insert_op, cl);

	trace_bcache_write(op->c, op->inode, op->bio,
			   op->writeback, op->bypass);

	bch_keylist_init(&op->insert_keys);
	bio_get(op->bio);
	bch_data_insert_start(cl);
}

/*
 * Congested?  Return 0 (not congested) or the limit (in sectors)
 * beyond which we should bypass the cache due to congestion.
 */
unsigned int bch_get_congested(const struct cache_set *c)
{
	int i;

	if (!c->congested_read_threshold_us &&
	    !c->congested_write_threshold_us)
		return 0;

	i = (local_clock_us() - c->congested_last_us) / 1024;
	if (i < 0)
		return 0;

	i += atomic_read(&c->congested);
	if (i >= 0)
		return 0;

	i += CONGESTED_MAX;

	if (i > 0)
		i = fract_exp_two(i, 6);

	i -= hweight32(get_random_u32());

	return i > 0 ? i : 1;
}

static void add_sequential(struct task_struct *t)
{
	ewma_add(t->sequential_io_avg,
		 t->sequential_io, 8, 0);

	t->sequential_io = 0;
}

static struct hlist_head *iohash(struct cached_dev *dc, uint64_t k)
{
	return &dc->io_hash[hash_64(k, RECENT_IO_BITS)];
}

static bool check_should_bypass(struct cached_dev *dc, struct bio *bio)
{
	struct cache_set *c = dc->disk.c;
	unsigned int mode = cache_mode(dc);
	unsigned int sectors, congested;
	struct task_struct *task = current;
	struct io *i;

	if (test_bit(BCACHE_DEV_DETACHING, &dc->disk.flags) ||
	    c->gc_stats.in_use > CUTOFF_CACHE_ADD ||
	    (bio_op(bio) == REQ_OP_DISCARD))
		goto skip;

	if (mode == CACHE_MODE_NONE ||
	    (mode == CACHE_MODE_WRITEAROUND &&
	     op_is_write(bio_op(bio))))
		goto skip;

	/*
	 * If the bio is for read-ahead or background IO, bypass it or
	 * not depends on the following situations,
	 * - If the IO is for meta data, always cache it and no bypass
	 * - If the IO is not meta data, check dc->cache_reada_policy,
	 *      BCH_CACHE_READA_ALL: cache it and not bypass
	 *      BCH_CACHE_READA_META_ONLY: not cache it and bypass
	 * That is, read-ahead request for metadata always get cached
	 * (eg, for gfs2 or xfs).
	 */
	if ((bio->bi_opf & (REQ_RAHEAD|REQ_BACKGROUND))) {
		if (!(bio->bi_opf & (REQ_META|REQ_PRIO)) &&
		    (dc->cache_readahead_policy != BCH_CACHE_READA_ALL))
			goto skip;
	}

	if (bio->bi_iter.bi_sector & (c->sb.block_size - 1) ||
	    bio_sectors(bio) & (c->sb.block_size - 1)) {
		pr_debug("skipping unaligned io\n");
		goto skip;
	}

	if (bypass_torture_test(dc)) {
		if ((get_random_int() & 3) == 3)
			goto skip;
		else
			goto rescale;
	}

	congested = bch_get_congested(c);
	if (!congested && !dc->sequential_cutoff)
		goto rescale;

	spin_lock(&dc->io_lock);

	hlist_for_each_entry(i, iohash(dc, bio->bi_iter.bi_sector), hash)
		if (i->last == bio->bi_iter.bi_sector &&
		    time_before(jiffies, i->jiffies))
			goto found;

	i = list_first_entry(&dc->io_lru, struct io, lru);

	add_sequential(task);
	i->sequential = 0;
found:
	if (i->sequential + bio->bi_iter.bi_size > i->sequential)
		i->sequential	+= bio->bi_iter.bi_size;

	i->last			 = bio_end_sector(bio);
	i->jiffies		 = jiffies + msecs_to_jiffies(5000);
	task->sequential_io	 = i->sequential;

	hlist_del(&i->hash);
	hlist_add_head(&i->hash, iohash(dc, i->last));
	list_move_tail(&i->lru, &dc->io_lru);

	spin_unlock(&dc->io_lock);

	sectors = max(task->sequential_io,
		      task->sequential_io_avg) >> 9;

	if (dc->sequential_cutoff &&
	    sectors >= dc->sequential_cutoff >> 9) {
		trace_bcache_bypass_sequential(bio);
		goto skip;
	}

	if (congested && sectors >= congested) {
		trace_bcache_bypass_congested(bio);
		goto skip;
	}

rescale:
	bch_rescale_priorities(c, bio_sectors(bio));
	return false;
skip:
	bch_mark_sectors_bypassed(c, dc, bio_sectors(bio));
	return true;
}

/* Cache lookup */

struct search {
	/* Stack frame for bio_complete */
	struct closure		cl;

	struct bbio		bio;
	struct bio		*orig_bio;
	struct bio		*cache_miss;
	struct bcache_device	*d;

	unsigned int		insert_bio_sectors;
	unsigned int		recoverable:1;
	unsigned int		write:1;
	unsigned int		read_dirty_data:1;
	unsigned int		cache_missed:1;

	struct hd_struct	*part;
	unsigned long		start_time;

	struct btree_op		op;
	struct data_insert_op	iop;
};

static void bch_cache_read_endio(struct bio *bio)
{
	struct bbio *b = container_of(bio, struct bbio, bio);
	struct closure *cl = bio->bi_private;
	struct search *s = container_of(cl, struct search, cl);

	/*
	 * If the bucket was reused while our bio was in flight, we might have
	 * read the wrong data. Set s->error but not error so it doesn't get
	 * counted against the cache device, but we'll still reread the data
	 * from the backing device.
	 */

	if (bio->bi_status)
		s->iop.status = bio->bi_status;
	else if (!KEY_DIRTY(&b->key) &&
		 ptr_stale(s->iop.c, &b->key, 0)) {
		atomic_long_inc(&s->iop.c->cache_read_races);
		s->iop.status = BLK_STS_IOERR;
	}

	bch_bbio_endio(s->iop.c, bio, bio->bi_status, "reading from cache");
}

/*
 * Read from a single key, handling the initial cache miss if the key starts in
 * the middle of the bio
 */
static int cache_lookup_fn(struct btree_op *op, struct btree *b, struct bkey *k)
{
	struct search *s = container_of(op, struct search, op);
	struct bio *n, *bio = &s->bio.bio;
	struct bkey *bio_key;
	unsigned int ptr;

	if (bkey_cmp(k, &KEY(s->iop.inode, bio->bi_iter.bi_sector, 0)) <= 0)
		return MAP_CONTINUE;

	if (KEY_INODE(k) != s->iop.inode ||
	    KEY_START(k) > bio->bi_iter.bi_sector) {
		unsigned int bio_sectors = bio_sectors(bio);
		unsigned int sectors = KEY_INODE(k) == s->iop.inode
			? min_t(uint64_t, INT_MAX,
				KEY_START(k) - bio->bi_iter.bi_sector)
			: INT_MAX;
		int ret = s->d->cache_miss(b, s, bio, sectors);

		if (ret != MAP_CONTINUE)
			return ret;

		/* if this was a complete miss we shouldn't get here */
		BUG_ON(bio_sectors <= sectors);
	}

	if (!KEY_SIZE(k))
		return MAP_CONTINUE;

	/* XXX: figure out best pointer - for multiple cache devices */
	ptr = 0;

	PTR_BUCKET(b->c, k, ptr)->prio = INITIAL_PRIO;

	if (KEY_DIRTY(k))
		s->read_dirty_data = true;

	n = bio_next_split(bio, min_t(uint64_t, INT_MAX,
				      KEY_OFFSET(k) - bio->bi_iter.bi_sector),
			   GFP_NOIO, &s->d->bio_split);

	bio_key = &container_of(n, struct bbio, bio)->key;
	bch_bkey_copy_single_ptr(bio_key, k, ptr);

	bch_cut_front(&KEY(s->iop.inode, n->bi_iter.bi_sector, 0), bio_key);
	bch_cut_back(&KEY(s->iop.inode, bio_end_sector(n), 0), bio_key);

	n->bi_end_io	= bch_cache_read_endio;
	n->bi_private	= &s->cl;

	/*
	 * The bucket we're reading from might be reused while our bio
	 * is in flight, and we could then end up reading the wrong
	 * data.
	 *
	 * We guard against this by checking (in cache_read_endio()) if
	 * the pointer is stale again; if so, we treat it as an error
	 * and reread from the backing device (but we don't pass that
	 * error up anywhere).
	 */

	__bch_submit_bbio(n, b->c);
	return n == bio ? MAP_DONE : MAP_CONTINUE;
}

static void cache_lookup(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, iop.cl);
	struct bio *bio = &s->bio.bio;
	struct cached_dev *dc;
	int ret;

	bch_btree_op_init(&s->op, -1);

	ret = bch_btree_map_keys(&s->op, s->iop.c,
				 &KEY(s->iop.inode, bio->bi_iter.bi_sector, 0),
				 cache_lookup_fn, MAP_END_KEY);
	if (ret == -EAGAIN) {
		continue_at(cl, cache_lookup, bcache_wq);
		return;
	}

	/*
	 * We might meet err when searching the btree, If that happens, we will
	 * get negative ret, in this scenario we should not recover data from
	 * backing device (when cache device is dirty) because we don't know
	 * whether bkeys the read request covered are all clean.
	 *
	 * And after that happened, s->iop.status is still its initial value
	 * before we submit s->bio.bio
	 */
	if (ret < 0) {
		BUG_ON(ret == -EINTR);
		if (s->d && s->d->c &&
				!UUID_FLASH_ONLY(&s->d->c->uuids[s->d->id])) {
			dc = container_of(s->d, struct cached_dev, disk);
			if (dc && atomic_read(&dc->has_dirty))
				s->recoverable = false;
		}
		if (!s->iop.status)
			s->iop.status = BLK_STS_IOERR;
	}

	closure_return(cl);
}

/* Common code for the make_request functions */

static void request_endio(struct bio *bio)
{
	struct closure *cl = bio->bi_private;

	if (bio->bi_status) {
		struct search *s = container_of(cl, struct search, cl);

		s->iop.status = bio->bi_status;
		/* Only cache read errors are recoverable */
		s->recoverable = false;
	}

	bio_put(bio);
	closure_put(cl);
}

static void backing_request_endio(struct bio *bio)
{
	struct closure *cl = bio->bi_private;

	if (bio->bi_status) {
		struct search *s = container_of(cl, struct search, cl);
		struct cached_dev *dc = container_of(s->d,
						     struct cached_dev, disk);
		/*
		 * If a bio has REQ_PREFLUSH for writeback mode, it is
		 * speically assembled in cached_dev_write() for a non-zero
		 * write request which has REQ_PREFLUSH. we don't set
		 * s->iop.status by this failure, the status will be decided
		 * by result of bch_data_insert() operation.
		 */
		if (unlikely(s->iop.writeback &&
			     bio->bi_opf & REQ_PREFLUSH)) {
			pr_err("Can't flush %s: returned bi_status %i\n",
				dc->backing_dev_name, bio->bi_status);
		} else {
			/* set to orig_bio->bi_status in bio_complete() */
			s->iop.status = bio->bi_status;
		}
		s->recoverable = false;
		/* should count I/O error for backing device here */
		bch_count_backing_io_errors(dc, bio);
	}

	bio_put(bio);
	closure_put(cl);
}

static void bio_complete(struct search *s)
{
	if (s->orig_bio) {
		/* Count on bcache device */
		part_end_io_acct(s->part, s->orig_bio, s->start_time);

		trace_bcache_request_end(s->d, s->orig_bio);
		s->orig_bio->bi_status = s->iop.status;
		bio_endio(s->orig_bio);
		s->orig_bio = NULL;
	}
}

static void do_bio_hook(struct search *s,
			struct bio *orig_bio,
			bio_end_io_t *end_io_fn)
{
	struct bio *bio = &s->bio.bio;

	bio_init(bio, NULL, 0);
	__bio_clone_fast(bio, orig_bio);
	/*
	 * bi_end_io can be set separately somewhere else, e.g. the
	 * variants in,
	 * - cache_bio->bi_end_io from cached_dev_cache_miss()
	 * - n->bi_end_io from cache_lookup_fn()
	 */
	bio->bi_end_io		= end_io_fn;
	bio->bi_private		= &s->cl;

	bio_cnt_set(bio, 3);
}

static void search_free(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);

	atomic_dec(&s->iop.c->search_inflight);

	if (s->iop.bio)
		bio_put(s->iop.bio);

	bio_complete(s);
	closure_debug_destroy(cl);
	mempool_free(s, &s->iop.c->search);
}

static inline struct search *search_alloc(struct bio *bio,
					  struct bcache_device *d)
{
	struct search *s;

	s = mempool_alloc(&d->c->search, GFP_NOIO);

	closure_init(&s->cl, NULL);
	do_bio_hook(s, bio, request_endio);
	atomic_inc(&d->c->search_inflight);

	s->orig_bio		= bio;
	s->cache_miss		= NULL;
	s->cache_missed		= 0;
	s->d			= d;
	s->recoverable		= 1;
	s->write		= op_is_write(bio_op(bio));
	s->read_dirty_data	= 0;
	/* Count on the bcache device */
	s->start_time		= part_start_io_acct(d->disk, &s->part, bio);
	s->iop.c		= d->c;
	s->iop.bio		= NULL;
	s->iop.inode		= d->id;
	s->iop.write_point	= hash_long((unsigned long) current, 16);
	s->iop.write_prio	= 0;
	s->iop.status		= 0;
	s->iop.flags		= 0;
	s->iop.flush_journal	= op_is_flush(bio->bi_opf);
	s->iop.wq		= bcache_wq;

	return s;
}

/* Cached devices */

static void cached_dev_bio_complete(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);
	struct cached_dev *dc = container_of(s->d, struct cached_dev, disk);

	cached_dev_put(dc);
	search_free(cl);
}

/* Process reads */

static void cached_dev_read_error_done(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);

	if (s->iop.replace_collision)
		bch_mark_cache_miss_collision(s->iop.c, s->d);

	if (s->iop.bio)
		bio_free_pages(s->iop.bio);

	cached_dev_bio_complete(cl);
}

static void cached_dev_read_error(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);
	struct bio *bio = &s->bio.bio;

	/*
	 * If read request hit dirty data (s->read_dirty_data is true),
	 * then recovery a failed read request from cached device may
	 * get a stale data back. So read failure recovery is only
	 * permitted when read request hit clean data in cache device,
	 * or when cache read race happened.
	 */
	if (s->recoverable && !s->read_dirty_data) {
		/* Retry from the backing device: */
		trace_bcache_read_retry(s->orig_bio);

		s->iop.status = 0;
		do_bio_hook(s, s->orig_bio, backing_request_endio);

		/* XXX: invalidate cache */

		/* I/O request sent to backing device */
		closure_bio_submit(s->iop.c, bio, cl);
	}

	continue_at(cl, cached_dev_read_error_done, NULL);
}

static void cached_dev_cache_miss_done(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);
	struct bcache_device *d = s->d;

	if (s->iop.replace_collision)
		bch_mark_cache_miss_collision(s->iop.c, s->d);

	if (s->iop.bio)
		bio_free_pages(s->iop.bio);

	cached_dev_bio_complete(cl);
	closure_put(&d->cl);
}

static void cached_dev_read_done(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);
	struct cached_dev *dc = container_of(s->d, struct cached_dev, disk);

	/*
	 * We had a cache miss; cache_bio now contains data ready to be inserted
	 * into the cache.
	 *
	 * First, we copy the data we just read from cache_bio's bounce buffers
	 * to the buffers the original bio pointed to:
	 */

	if (s->iop.bio) {
		bio_reset(s->iop.bio);
		s->iop.bio->bi_iter.bi_sector =
			s->cache_miss->bi_iter.bi_sector;
		bio_copy_dev(s->iop.bio, s->cache_miss);
		s->iop.bio->bi_iter.bi_size = s->insert_bio_sectors << 9;
		bch_bio_map(s->iop.bio, NULL);

		bio_copy_data(s->cache_miss, s->iop.bio);

		bio_put(s->cache_miss);
		s->cache_miss = NULL;
	}

	if (verify(dc) && s->recoverable && !s->read_dirty_data)
		bch_data_verify(dc, s->orig_bio);

	closure_get(&dc->disk.cl);
	bio_complete(s);

	if (s->iop.bio &&
	    !test_bit(CACHE_SET_STOPPING, &s->iop.c->flags)) {
		BUG_ON(!s->iop.replace);
		closure_call(&s->iop.cl, bch_data_insert, NULL, cl);
	}

	continue_at(cl, cached_dev_cache_miss_done, NULL);
}

static void cached_dev_read_done_bh(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);
	struct cached_dev *dc = container_of(s->d, struct cached_dev, disk);

	bch_mark_cache_accounting(s->iop.c, s->d,
				  !s->cache_missed, s->iop.bypass);
	trace_bcache_read(s->orig_bio, !s->cache_missed, s->iop.bypass);

	if (s->iop.status)
		continue_at_nobarrier(cl, cached_dev_read_error, bcache_wq);
	else if (s->iop.bio || verify(dc))
		continue_at_nobarrier(cl, cached_dev_read_done, bcache_wq);
	else
		continue_at_nobarrier(cl, cached_dev_bio_complete, NULL);
}

static int cached_dev_cache_miss(struct btree *b, struct search *s,
				 struct bio *bio, unsigned int sectors)
{
	int ret = MAP_CONTINUE;
	unsigned int reada = 0;
	struct cached_dev *dc = container_of(s->d, struct cached_dev, disk);
	struct bio *miss, *cache_bio;

	s->cache_missed = 1;

	if (s->cache_miss || s->iop.bypass) {
		miss = bio_next_split(bio, sectors, GFP_NOIO, &s->d->bio_split);
		ret = miss == bio ? MAP_DONE : MAP_CONTINUE;
		goto out_submit;
	}

	if (!(bio->bi_opf & REQ_RAHEAD) &&
	    !(bio->bi_opf & (REQ_META|REQ_PRIO)) &&
	    s->iop.c->gc_stats.in_use < CUTOFF_CACHE_READA)
		reada = min_t(sector_t, dc->readahead >> 9,
			      get_capacity(bio->bi_disk) - bio_end_sector(bio));

	s->insert_bio_sectors = min(sectors, bio_sectors(bio) + reada);

	s->iop.replace_key = KEY(s->iop.inode,
				 bio->bi_iter.bi_sector + s->insert_bio_sectors,
				 s->insert_bio_sectors);

	ret = bch_btree_insert_check_key(b, &s->op, &s->iop.replace_key);
	if (ret)
		return ret;

	s->iop.replace = true;

	miss = bio_next_split(bio, sectors, GFP_NOIO, &s->d->bio_split);

	/* btree_search_recurse()'s btree iterator is no good anymore */
	ret = miss == bio ? MAP_DONE : -EINTR;

	cache_bio = bio_alloc_bioset(GFP_NOWAIT,
			DIV_ROUND_UP(s->insert_bio_sectors, PAGE_SECTORS),
			&dc->disk.bio_split);
	if (!cache_bio)
		goto out_submit;

	cache_bio->bi_iter.bi_sector	= miss->bi_iter.bi_sector;
	bio_copy_dev(cache_bio, miss);
	cache_bio->bi_iter.bi_size	= s->insert_bio_sectors << 9;

	cache_bio->bi_end_io	= backing_request_endio;
	cache_bio->bi_private	= &s->cl;

	bch_bio_map(cache_bio, NULL);
	if (bch_bio_alloc_pages(cache_bio, __GFP_NOWARN|GFP_NOIO))
		goto out_put;

	if (reada)
		bch_mark_cache_readahead(s->iop.c, s->d);

	s->cache_miss	= miss;
	s->iop.bio	= cache_bio;
	bio_get(cache_bio);
	/* I/O request sent to backing device */
	closure_bio_submit(s->iop.c, cache_bio, &s->cl);

	return ret;
out_put:
	bio_put(cache_bio);
out_submit:
	miss->bi_end_io		= backing_request_endio;
	miss->bi_private	= &s->cl;
	/* I/O request sent to backing device */
	closure_bio_submit(s->iop.c, miss, &s->cl);
	return ret;
}

static void cached_dev_read(struct cached_dev *dc, struct search *s)
{
	struct closure *cl = &s->cl;

	closure_call(&s->iop.cl, cache_lookup, NULL, cl);
	continue_at(cl, cached_dev_read_done_bh, NULL);
}

/* Process writes */

static void cached_dev_write_complete(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);
	struct cached_dev *dc = container_of(s->d, struct cached_dev, disk);

	up_read_non_owner(&dc->writeback_lock);
	cached_dev_bio_complete(cl);
}

static void cached_dev_write(struct cached_dev *dc, struct search *s)
{
	struct closure *cl = &s->cl;
	struct bio *bio = &s->bio.bio;
	struct bkey start = KEY(dc->disk.id, bio->bi_iter.bi_sector, 0);
	struct bkey end = KEY(dc->disk.id, bio_end_sector(bio), 0);

	bch_keybuf_check_overlapping(&s->iop.c->moving_gc_keys, &start, &end);

	down_read_non_owner(&dc->writeback_lock);
	if (bch_keybuf_check_overlapping(&dc->writeback_keys, &start, &end)) {
		/*
		 * We overlap with some dirty data undergoing background
		 * writeback, force this write to writeback
		 */
		s->iop.bypass = false;
		s->iop.writeback = true;
	}

	/*
	 * Discards aren't _required_ to do anything, so skipping if
	 * check_overlapping returned true is ok
	 *
	 * But check_overlapping drops dirty keys for which io hasn't started,
	 * so we still want to call it.
	 */
	if (bio_op(bio) == REQ_OP_DISCARD)
		s->iop.bypass = true;

	if (should_writeback(dc, s->orig_bio,
			     cache_mode(dc),
			     s->iop.bypass)) {
		s->iop.bypass = false;
		s->iop.writeback = true;
	}

	if (s->iop.bypass) {
		s->iop.bio = s->orig_bio;
		bio_get(s->iop.bio);

		if (bio_op(bio) == REQ_OP_DISCARD &&
		    !blk_queue_discard(bdev_get_queue(dc->bdev)))
			goto insert_data;

		/* I/O request sent to backing device */
		bio->bi_end_io = backing_request_endio;
		closure_bio_submit(s->iop.c, bio, cl);

	} else if (s->iop.writeback) {
		bch_writeback_add(dc);
		s->iop.bio = bio;

		if (bio->bi_opf & REQ_PREFLUSH) {
			/*
			 * Also need to send a flush to the backing
			 * device.
			 */
			struct bio *flush;

			flush = bio_alloc_bioset(GFP_NOIO, 0,
						 &dc->disk.bio_split);
			if (!flush) {
				s->iop.status = BLK_STS_RESOURCE;
				goto insert_data;
			}
			bio_copy_dev(flush, bio);
			flush->bi_end_io = backing_request_endio;
			flush->bi_private = cl;
			flush->bi_opf = REQ_OP_WRITE | REQ_PREFLUSH;
			/* I/O request sent to backing device */
			closure_bio_submit(s->iop.c, flush, cl);
		}
	} else {
		s->iop.bio = bio_clone_fast(bio, GFP_NOIO, &dc->disk.bio_split);
		/* I/O request sent to backing device */
		bio->bi_end_io = backing_request_endio;
		closure_bio_submit(s->iop.c, bio, cl);
	}

insert_data:
	closure_call(&s->iop.cl, bch_data_insert, NULL, cl);
	continue_at(cl, cached_dev_write_complete, NULL);
}

static void cached_dev_nodata(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);
	struct bio *bio = &s->bio.bio;

	if (s->iop.flush_journal)
		bch_journal_meta(s->iop.c, cl);

	/* If it's a flush, we send the flush to the backing device too */
	bio->bi_end_io = backing_request_endio;
	closure_bio_submit(s->iop.c, bio, cl);

	continue_at(cl, cached_dev_bio_complete, NULL);
}

struct detached_dev_io_private {
	struct bcache_device	*d;
	unsigned long		start_time;
	bio_end_io_t		*bi_end_io;
	void			*bi_private;
	struct hd_struct	*part;
};

static void detached_dev_end_io(struct bio *bio)
{
	struct detached_dev_io_private *ddip;

	ddip = bio->bi_private;
	bio->bi_end_io = ddip->bi_end_io;
	bio->bi_private = ddip->bi_private;

	/* Count on the bcache device */
	part_end_io_acct(ddip->part, bio, ddip->start_time);

	if (bio->bi_status) {
		struct cached_dev *dc = container_of(ddip->d,
						     struct cached_dev, disk);
		/* should count I/O error for backing device here */
		bch_count_backing_io_errors(dc, bio);
	}

	kfree(ddip);
	bio->bi_end_io(bio);
}

static void detached_dev_do_request(struct bcache_device *d, struct bio *bio)
{
	struct detached_dev_io_private *ddip;
	struct cached_dev *dc = container_of(d, struct cached_dev, disk);

	/*
	 * no need to call closure_get(&dc->disk.cl),
	 * because upper layer had already opened bcache device,
	 * which would call closure_get(&dc->disk.cl)
	 */
	ddip = kzalloc(sizeof(struct detached_dev_io_private), GFP_NOIO);
	ddip->d = d;
	/* Count on the bcache device */
	ddip->start_time = part_start_io_acct(d->disk, &ddip->part, bio);
	ddip->bi_end_io = bio->bi_end_io;
	ddip->bi_private = bio->bi_private;
	bio->bi_end_io = detached_dev_end_io;
	bio->bi_private = ddip;

	if ((bio_op(bio) == REQ_OP_DISCARD) &&
	    !blk_queue_discard(bdev_get_queue(dc->bdev)))
		bio->bi_end_io(bio);
	else
		submit_bio_noacct(bio);
}

static void quit_max_writeback_rate(struct cache_set *c,
				    struct cached_dev *this_dc)
{
	int i;
	struct bcache_device *d;
	struct cached_dev *dc;

	/*
	 * mutex bch_register_lock may compete with other parallel requesters,
	 * or attach/detach operations on other backing device. Waiting to
	 * the mutex lock may increase I/O request latency for seconds or more.
	 * To avoid such situation, if mutext_trylock() failed, only writeback
	 * rate of current cached device is set to 1, and __update_write_back()
	 * will decide writeback rate of other cached devices (remember now
	 * c->idle_counter is 0 already).
	 */
	if (mutex_trylock(&bch_register_lock)) {
		for (i = 0; i < c->devices_max_used; i++) {
			if (!c->devices[i])
				continue;

			if (UUID_FLASH_ONLY(&c->uuids[i]))
				continue;

			d = c->devices[i];
			dc = container_of(d, struct cached_dev, disk);
			/*
			 * set writeback rate to default minimum value,
			 * then let update_writeback_rate() to decide the
			 * upcoming rate.
			 */
			atomic_long_set(&dc->writeback_rate.rate, 1);
		}
		mutex_unlock(&bch_register_lock);
	} else
		atomic_long_set(&this_dc->writeback_rate.rate, 1);
}

/* Cached devices - read & write stuff */

blk_qc_t cached_dev_submit_bio(struct bio *bio)
{
	struct search *s;
	struct bcache_device *d = bio->bi_disk->private_data;
	struct cached_dev *dc = container_of(d, struct cached_dev, disk);
	int rw = bio_data_dir(bio);

	if (unlikely((d->c && test_bit(CACHE_SET_IO_DISABLE, &d->c->flags)) ||
		     dc->io_disable)) {
		bio->bi_status = BLK_STS_IOERR;
		bio_endio(bio);
		return BLK_QC_T_NONE;
	}

	if (likely(d->c)) {
		if (atomic_read(&d->c->idle_counter))
			atomic_set(&d->c->idle_counter, 0);
		/*
		 * If at_max_writeback_rate of cache set is true and new I/O
		 * comes, quit max writeback rate of all cached devices
		 * attached to this cache set, and set at_max_writeback_rate
		 * to false.
		 */
		if (unlikely(atomic_read(&d->c->at_max_writeback_rate) == 1)) {
			atomic_set(&d->c->at_max_writeback_rate, 0);
			quit_max_writeback_rate(d->c, dc);
		}
	}

	bio_set_dev(bio, dc->bdev);
	bio->bi_iter.bi_sector += dc->sb.data_offset;

	if (cached_dev_get(dc)) {
		s = search_alloc(bio, d);
		trace_bcache_request_start(s->d, bio);

		if (!bio->bi_iter.bi_size) {
			/*
			 * can't call bch_journal_meta from under
			 * submit_bio_noacct
			 */
			continue_at_nobarrier(&s->cl,
					      cached_dev_nodata,
					      bcache_wq);
		} else {
			s->iop.bypass = check_should_bypass(dc, bio);

			if (rw)
				cached_dev_write(dc, s);
			else
				cached_dev_read(dc, s);
		}
	} else
		/* I/O request sent to backing device */
		detached_dev_do_request(d, bio);

	return BLK_QC_T_NONE;
}

static int cached_dev_ioctl(struct bcache_device *d, fmode_t mode,
			    unsigned int cmd, unsigned long arg)
{
	struct cached_dev *dc = container_of(d, struct cached_dev, disk);

	if (dc->io_disable)
		return -EIO;

	return __blkdev_driver_ioctl(dc->bdev, mode, cmd, arg);
}

void bch_cached_dev_request_init(struct cached_dev *dc)
{
	dc->disk.cache_miss			= cached_dev_cache_miss;
	dc->disk.ioctl				= cached_dev_ioctl;
}

/* Flash backed devices */

static int flash_dev_cache_miss(struct btree *b, struct search *s,
				struct bio *bio, unsigned int sectors)
{
	unsigned int bytes = min(sectors, bio_sectors(bio)) << 9;

	swap(bio->bi_iter.bi_size, bytes);
	zero_fill_bio(bio);
	swap(bio->bi_iter.bi_size, bytes);

	bio_advance(bio, bytes);

	if (!bio->bi_iter.bi_size)
		return MAP_DONE;

	return MAP_CONTINUE;
}

static void flash_dev_nodata(struct closure *cl)
{
	struct search *s = container_of(cl, struct search, cl);

	if (s->iop.flush_journal)
		bch_journal_meta(s->iop.c, cl);

	continue_at(cl, search_free, NULL);
}

blk_qc_t flash_dev_submit_bio(struct bio *bio)
{
	struct search *s;
	struct closure *cl;
	struct bcache_device *d = bio->bi_disk->private_data;

	if (unlikely(d->c && test_bit(CACHE_SET_IO_DISABLE, &d->c->flags))) {
		bio->bi_status = BLK_STS_IOERR;
		bio_endio(bio);
		return BLK_QC_T_NONE;
	}

	s = search_alloc(bio, d);
	cl = &s->cl;
	bio = &s->bio.bio;

	trace_bcache_request_start(s->d, bio);

	if (!bio->bi_iter.bi_size) {
		/*
		 * can't call bch_journal_meta from under submit_bio_noacct
		 */
		continue_at_nobarrier(&s->cl,
				      flash_dev_nodata,
				      bcache_wq);
		return BLK_QC_T_NONE;
	} else if (bio_data_dir(bio)) {
		bch_keybuf_check_overlapping(&s->iop.c->moving_gc_keys,
					&KEY(d->id, bio->bi_iter.bi_sector, 0),
					&KEY(d->id, bio_end_sector(bio), 0));

		s->iop.bypass		= (bio_op(bio) == REQ_OP_DISCARD) != 0;
		s->iop.writeback	= true;
		s->iop.bio		= bio;

		closure_call(&s->iop.cl, bch_data_insert, NULL, cl);
	} else {
		closure_call(&s->iop.cl, cache_lookup, NULL, cl);
	}

	continue_at(cl, search_free, NULL);
	return BLK_QC_T_NONE;
}

static int flash_dev_ioctl(struct bcache_device *d, fmode_t mode,
			   unsigned int cmd, unsigned long arg)
{
	return -ENOTTY;
}

void bch_flash_dev_request_init(struct bcache_device *d)
{
	d->cache_miss				= flash_dev_cache_miss;
	d->ioctl				= flash_dev_ioctl;
}

void bch_request_exit(void)
{
	kmem_cache_destroy(bch_search_cache);
}

int __init bch_request_init(void)
{
	bch_search_cache = KMEM_CACHE(search, 0);
	if (!bch_search_cache)
		return -ENOMEM;

	return 0;
}
