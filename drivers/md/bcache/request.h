#ifndef _BCACHE_REQUEST_H_
#define _BCACHE_REQUEST_H_

struct data_insert_op {
	struct closure		cl;
	struct cache_set	*c;
	struct bio		*bio;
	struct workqueue_struct *wq;

	unsigned		inode;
	uint16_t		write_point;
	uint16_t		write_prio;
	blk_status_t		status;

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

#endif /* _BCACHE_REQUEST_H_ */
