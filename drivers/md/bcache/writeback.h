#ifndef _BCACHE_WRITEBACK_H
#define _BCACHE_WRITEBACK_H

static inline uint64_t bcache_dev_sectors_dirty(struct bcache_device *d)
{
	uint64_t i, ret = 0;

	for (i = 0; i < d->nr_stripes; i++)
		ret += atomic_read(d->stripe_sectors_dirty + i);

	return ret;
}

void bcache_dev_sectors_dirty_add(struct cache_set *, unsigned, uint64_t, int);
void bch_writeback_queue(struct cached_dev *);
void bch_writeback_add(struct cached_dev *);

void bch_sectors_dirty_init(struct cached_dev *dc);
void bch_cached_dev_writeback_init(struct cached_dev *);

#endif
