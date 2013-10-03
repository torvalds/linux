#ifndef _BCACHE_WRITEBACK_H
#define _BCACHE_WRITEBACK_H

#define CUTOFF_WRITEBACK	40
#define CUTOFF_WRITEBACK_SYNC	70

static inline uint64_t bcache_dev_sectors_dirty(struct bcache_device *d)
{
	uint64_t i, ret = 0;

	for (i = 0; i < d->nr_stripes; i++)
		ret += atomic_read(d->stripe_sectors_dirty + i);

	return ret;
}

static inline bool bcache_dev_stripe_dirty(struct bcache_device *d,
					   uint64_t offset,
					   unsigned nr_sectors)
{
	uint64_t stripe = offset >> d->stripe_size_bits;

	while (1) {
		if (atomic_read(d->stripe_sectors_dirty + stripe))
			return true;

		if (nr_sectors <= 1 << d->stripe_size_bits)
			return false;

		nr_sectors -= 1 << d->stripe_size_bits;
		stripe++;
	}
}

static inline bool should_writeback(struct cached_dev *dc, struct bio *bio,
				    unsigned cache_mode, bool would_skip)
{
	unsigned in_use = dc->disk.c->gc_stats.in_use;

	if (cache_mode != CACHE_MODE_WRITEBACK ||
	    atomic_read(&dc->disk.detaching) ||
	    in_use > CUTOFF_WRITEBACK_SYNC)
		return false;

	if (dc->partial_stripes_expensive &&
	    bcache_dev_stripe_dirty(&dc->disk, bio->bi_sector,
				    bio_sectors(bio)))
		return true;

	if (would_skip)
		return false;

	return bio->bi_rw & REQ_SYNC ||
		in_use <= CUTOFF_WRITEBACK;
}

void bcache_dev_sectors_dirty_add(struct cache_set *, unsigned, uint64_t, int);
void bch_writeback_queue(struct cached_dev *);
void bch_writeback_add(struct cached_dev *);

void bch_sectors_dirty_init(struct cached_dev *dc);
void bch_cached_dev_writeback_init(struct cached_dev *);

#endif
