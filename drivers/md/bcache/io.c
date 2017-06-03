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

	bio_init(bio, bio->bi_inline_vecs, bucket_pages(c));

	return bio;
}

void __bch_submit_bbio(struct bio *bio, struct cache_set *c)
{
	struct bbio *b = container_of(bio, struct bbio, bio);

	bio->bi_iter.bi_sector	= PTR_OFFSET(&b->key, 0);
	bio->bi_bdev		= PTR_CACHE(c, &b->key, 0)->bdev;

	b->submit_time_us = local_clock_us();
	closure_bio_submit(bio, bio->bi_private);
}

void bch_submit_bbio(struct bio *bio, struct cache_set *c,
		     struct bkey *k, unsigned ptr)
{
	struct bbio *b = container_of(bio, struct bbio, bio);
	bch_bkey_copy_single_ptr(&b->key, k, ptr);
	__bch_submit_bbio(bio, c);
}

/* IO errors */

void bch_count_io_errors(struct cache *ca, blk_status_t error, const char *m)
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
			      blk_status_t error, const char *m)
{
	struct bbio *b = container_of(bio, struct bbio, bio);
	struct cache *ca = PTR_CACHE(c, &b->key, 0);

	unsigned threshold = op_is_write(bio_op(bio))
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
		    blk_status_t error, const char *m)
{
	struct closure *cl = bio->bi_private;

	bch_bbio_count_io_errors(c, bio, error, m);
	bio_put(bio);
	closure_put(cl);
}
