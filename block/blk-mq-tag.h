#ifndef INT_BLK_MQ_TAG_H
#define INT_BLK_MQ_TAG_H

#include <linux/percpu_ida.h>

/*
 * Tag address space map.
 */
struct blk_mq_tags {
	unsigned int nr_tags;
	unsigned int nr_reserved_tags;
	unsigned int nr_batch_move;
	unsigned int nr_max_cache;

	struct percpu_ida free_tags;
	struct percpu_ida reserved_tags;

	struct request **rqs;
	struct list_head page_list;
};


extern struct blk_mq_tags *blk_mq_init_tags(unsigned int nr_tags, unsigned int reserved_tags, int node);
extern void blk_mq_free_tags(struct blk_mq_tags *tags);

extern unsigned int blk_mq_get_tag(struct blk_mq_tags *tags, gfp_t gfp, bool reserved);
extern void blk_mq_wait_for_tags(struct blk_mq_tags *tags, bool reserved);
extern void blk_mq_put_tag(struct blk_mq_tags *tags, unsigned int tag);
extern void blk_mq_tag_busy_iter(struct blk_mq_tags *tags, void (*fn)(void *data, unsigned long *), void *data);
extern bool blk_mq_has_free_tags(struct blk_mq_tags *tags);
extern ssize_t blk_mq_tag_sysfs_show(struct blk_mq_tags *tags, char *page);

enum {
	BLK_MQ_TAG_CACHE_MIN	= 1,
	BLK_MQ_TAG_CACHE_MAX	= 64,
};

enum {
	BLK_MQ_TAG_FAIL		= -1U,
	BLK_MQ_TAG_MIN		= BLK_MQ_TAG_CACHE_MIN,
	BLK_MQ_TAG_MAX		= BLK_MQ_TAG_FAIL - 1,
};

#endif
