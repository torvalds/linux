#ifndef _BCACHE_REQUEST_H_
#define _BCACHE_REQUEST_H_

#include <linux/cgroup.h>

struct data_insert_op {
	struct closure		cl;
	struct cache_set	*c;
	struct bio		*bio;

	unsigned		inode;
	uint16_t		write_point;
	uint16_t		write_prio;
	short			error;

	union {
		uint16_t	flags;

	struct {
		unsigned	bypass:1;
		unsigned	writeback:1;
		unsigned	flush_journal:1;
		unsigned	csum:1;

		unsigned	replace:1;
		unsigned	replace_collision:1;

		unsigned	insert_data_done:1;
	};
	};

	struct keylist		insert_keys;
	BKEY_PADDED(replace_key);
};

unsigned bch_get_congested(struct cache_set *);
void bch_data_insert(struct closure *cl);

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
