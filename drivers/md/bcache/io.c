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

static void bch_bi_idx_hack_endio(struct bio *bio, int error)
{
	struct bio *p = bio->bi_private;

	bio_endio(p, error);
	bio_put(bio);
}

static void bch_generic_make_request_hack(struct bio *bio)
{
	if (bio->bi_idx) {
		struct bio *clone = bio_alloc(GFP_NOIO, bio_segments(bio));

		memcpy(clone->bi_io_vec,
		       bio_iovec(bio),
		       bio_segments(bio) * sizeof(struct bio_vec));

		clone->bi_sector	= bio->bi_sector;
		clone->bi_bdev		= bio->bi_bdev;
		clone->bi_rw		= bio->bi_rw;
		clone->bi_vcnt		= bio_segments(bio);
		clone->bi_size		= bio->bi_size;

		clone->bi_private	= bio;
		clone->bi_end_io	= bch_bi_idx_hack_endio;

		bio = clone;
	}

	/*
	 * Hack, since drivers that clone bios clone up to bi_max_vecs, but our
	 * bios might have had more than that (before we split them per device
	 * limitations).
	 *
	 * To be taken out once immutable bvec stuff is in.
	 */
	bio->bi_max_vecs = bio->bi_vcnt;

	generic_make_request(bio);
}

/**
 * bch_bio_split - split a bio
 * @bio:	bio to split
 * @sectors:	number of sectors to split from the front of @bio
 * @gfp:	gfp mask
 * @bs:		bio set to allocate from
 *
 * Allocates and returns a new bio which represents @sectors from the start of
 * @bio, and updates @bio to represent the remaining sectors.
 *
 * If bio_sectors(@bio) was less than or equal to @sectors, returns @bio
 * unchanged.
 *
 * The newly allocated bio will point to @bio's bi_io_vec, if the split was on a
 * bvec boundry; it is the caller's responsibility to ensure that @bio is not
 * freed before the split.
 *
 * If bch_bio_split() is running under generic_make_request(), it's not safe to
 * allocate more than one bio from the same bio set. Therefore, if it is running
 * under generic_make_request() it masks out __GFP_WAIT when doing the
 * allocation. The caller must check for failure if there's any possibility of
 * it being called from under generic_make_request(); it is then the caller's
 * responsibility to retry from a safe context (by e.g. punting to workqueue).
 */
struct bio *bch_bio_split(struct bio *bio, int sectors,
			  gfp_t gfp, struct bio_set *bs)
{
	unsigned idx = bio->bi_idx, vcnt = 0, nbytes = sectors << 9;
	struct bio_vec *bv;
	struct bio *ret = NULL;

	BUG_ON(sectors <= 0);

	/*
	 * If we're being called from underneath generic_make_request() and we
	 * already allocated any bios from this bio set, we risk deadlock if we
	 * use the mempool. So instead, we possibly fail and let the caller punt
	 * to workqueue or somesuch and retry in a safe context.
	 */
	if (current->bio_list)
		gfp &= ~__GFP_WAIT;

	if (sectors >= bio_sectors(bio))
		return bio;

	if (bio->bi_rw & REQ_DISCARD) {
		ret = bio_alloc_bioset(gfp, 1, bs);
		if (!ret)
			return NULL;
		idx = 0;
		goto out;
	}

	bio_for_each_segment(bv, bio, idx) {
		vcnt = idx - bio->bi_idx;

		if (!nbytes) {
			ret = bio_alloc_bioset(gfp, vcnt, bs);
			if (!ret)
				return NULL;

			memcpy(ret->bi_io_vec, bio_iovec(bio),
			       sizeof(struct bio_vec) * vcnt);

			break;
		} else if (nbytes < bv->bv_len) {
			ret = bio_alloc_bioset(gfp, ++vcnt, bs);
			if (!ret)
				return NULL;

			memcpy(ret->bi_io_vec, bio_iovec(bio),
			       sizeof(struct bio_vec) * vcnt);

			ret->bi_io_vec[vcnt - 1].bv_len = nbytes;
			bv->bv_offset	+= nbytes;
			bv->bv_len	-= nbytes;
			break;
		}

		nbytes -= bv->bv_len;
	}
out:
	ret->bi_bdev	= bio->bi_bdev;
	ret->bi_sector	= bio->bi_sector;
	ret->bi_size	= sectors << 9;
	ret->bi_rw	= bio->bi_rw;
	ret->bi_vcnt	= vcnt;
	ret->bi_max_vecs = vcnt;

	bio->bi_sector	+= sectors;
	bio->bi_size	-= sectors << 9;
	bio->bi_idx	 = idx;

	if (bio_integrity(bio)) {
		if (bio_integrity_clone(ret, bio, gfp)) {
			bio_put(ret);
			return NULL;
		}

		bio_integrity_trim(ret, 0, bio_sectors(ret));
		bio_integrity_trim(bio, bio_sectors(ret), bio_sectors(bio));
	}

	return ret;
}

static unsigned bch_bio_max_sectors(struct bio *bio)
{
	unsigned ret = bio_sectors(bio);
	struct request_queue *q = bdev_get_queue(bio->bi_bdev);
	unsigned max_segments = min_t(unsigned, BIO_MAX_PAGES,
				      queue_max_segments(q));
	struct bio_vec *bv, *end = bio_iovec(bio) +
		min_t(int, bio_segments(bio), max_segments);

	if (bio->bi_rw & REQ_DISCARD)
		return min(ret, q->limits.max_discard_sectors);

	if (bio_segments(bio) > max_segments ||
	    q->merge_bvec_fn) {
		ret = 0;

		for (bv = bio_iovec(bio); bv < end; bv++) {
			struct bvec_merge_data bvm = {
				.bi_bdev	= bio->bi_bdev,
				.bi_sector	= bio->bi_sector,
				.bi_size	= ret << 9,
				.bi_rw		= bio->bi_rw,
			};

			if (q->merge_bvec_fn &&
			    q->merge_bvec_fn(q, &bvm, bv) < (int) bv->bv_len)
				break;

			ret += bv->bv_len >> 9;
		}
	}

	ret = min(ret, queue_max_sectors(q));

	WARN_ON(!ret);
	ret = max_t(int, ret, bio_iovec(bio)->bv_len >> 9);

	return ret;
}

static void bch_bio_submit_split_done(struct closure *cl)
{
	struct bio_split_hook *s = container_of(cl, struct bio_split_hook, cl);

	s->bio->bi_end_io = s->bi_end_io;
	s->bio->bi_private = s->bi_private;
	bio_endio(s->bio, 0);

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

static void __bch_bio_submit_split(struct closure *cl)
{
	struct bio_split_hook *s = container_of(cl, struct bio_split_hook, cl);
	struct bio *bio = s->bio, *n;

	do {
		n = bch_bio_split(bio, bch_bio_max_sectors(bio),
				  GFP_NOIO, s->p->bio_split);
		if (!n)
			continue_at(cl, __bch_bio_submit_split, system_wq);

		n->bi_end_io	= bch_bio_submit_split_endio;
		n->bi_private	= cl;

		closure_get(cl);
		bch_generic_make_request_hack(n);
	} while (n != bio);

	continue_at(cl, bch_bio_submit_split_done, NULL);
}

void bch_generic_make_request(struct bio *bio, struct bio_split_pool *p)
{
	struct bio_split_hook *s;

	if (!bio_has_data(bio) && !(bio->bi_rw & REQ_DISCARD))
		goto submit;

	if (bio_sectors(bio) <= bch_bio_max_sectors(bio))
		goto submit;

	s = mempool_alloc(p->bio_split_hook, GFP_NOIO);

	s->bio		= bio;
	s->p		= p;
	s->bi_end_io	= bio->bi_end_io;
	s->bi_private	= bio->bi_private;
	bio_get(bio);

	closure_call(&s->cl, __bch_bio_submit_split, NULL, NULL);
	return;
submit:
	bch_generic_make_request_hack(bio);
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

	bio->bi_sector	= PTR_OFFSET(&b->key, 0);
	bio->bi_bdev	= PTR_CACHE(c, &b->key, 0)->bdev;

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
