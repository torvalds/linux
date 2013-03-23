#ifndef _BCACHE_REQUEST_H_
#define _BCACHE_REQUEST_H_

#include <linux/cgroup.h>

struct search {
	/* Stack frame for bio_complete */
	struct closure		cl;

	struct bcache_device	*d;
	struct task_struct	*task;

	struct bbio		bio;
	struct bio		*orig_bio;
	struct bio		*cache_miss;
	unsigned		cache_bio_sectors;

	unsigned		recoverable:1;
	unsigned		unaligned_bvec:1;

	unsigned		write:1;
	unsigned		writeback:1;

	/* IO error returned to s->bio */
	short			error;
	unsigned long		start_time;

	/* Anything past op->keys won't get zeroed in do_bio_hook */
	struct btree_op		op;
};

void bch_cache_read_endio(struct bio *, int);
int bch_get_congested(struct cache_set *);
void bch_insert_data(struct closure *cl);
void bch_btree_insert_async(struct closure *);
void bch_cache_read_endio(struct bio *, int);

void bch_open_buckets_free(struct cache_set *);
int bch_open_buckets_alloc(struct cache_set *);

void bch_cached_dev_request_init(struct cached_dev *dc);
void bch_flash_dev_request_init(struct bcache_device *d);

extern struct kmem_cache *bch_search_cache, *bch_passthrough_cache;

struct bch_cgroup {
#ifdef CONFIG_CGROUP_BCACHE
	struct cgroup_subsys_state	css;
#endif
	/*
	 * We subtract one from the index into bch_cache_modes[], so that
	 * default == -1; this makes it so the rest match up with d->cache_mode,
	 * and we use d->cache_mode if cgrp->cache_mode < 0
	 */
	short				cache_mode;
	bool				verify;
	struct cache_stat_collector	stats;
};

struct bch_cgroup *bch_bio_to_cgroup(struct bio *bio);

#endif /* _BCACHE_REQUEST_H_ */
