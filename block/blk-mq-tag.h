/* SPDX-License-Identifier: GPL-2.0 */
#ifndef INT_BLK_MQ_TAG_H
#define INT_BLK_MQ_TAG_H

struct blk_mq_alloc_data;

extern struct blk_mq_tags *blk_mq_init_tags(unsigned int nr_tags,
					unsigned int reserved_tags,
					int node, int alloc_policy);
extern void blk_mq_free_tags(struct blk_mq_tags *tags);
extern int blk_mq_init_bitmaps(struct sbitmap_queue *bitmap_tags,
			       struct sbitmap_queue *breserved_tags,
			       unsigned int queue_depth,
			       unsigned int reserved,
			       int node, int alloc_policy);

extern unsigned int blk_mq_get_tag(struct blk_mq_alloc_data *data);
unsigned long blk_mq_get_tags(struct blk_mq_alloc_data *data, int nr_tags,
			      unsigned int *offset);
extern void blk_mq_put_tag(struct blk_mq_tags *tags, struct blk_mq_ctx *ctx,
			   unsigned int tag);
void blk_mq_put_tags(struct blk_mq_tags *tags, int *tag_array, int nr_tags);
extern int blk_mq_tag_update_depth(struct blk_mq_hw_ctx *hctx,
					struct blk_mq_tags **tags,
					unsigned int depth, bool can_grow);
extern void blk_mq_tag_resize_shared_tags(struct blk_mq_tag_set *set,
					     unsigned int size);
extern void blk_mq_tag_update_sched_shared_tags(struct request_queue *q);

extern void blk_mq_tag_wakeup_all(struct blk_mq_tags *tags, bool);
void blk_mq_queue_tag_busy_iter(struct request_queue *q, busy_iter_fn *fn,
		void *priv);
void blk_mq_all_tag_iter(struct blk_mq_tags *tags, busy_tag_iter_fn *fn,
		void *priv);

static inline struct sbq_wait_state *bt_wait_ptr(struct sbitmap_queue *bt,
						 struct blk_mq_hw_ctx *hctx)
{
	if (!hctx)
		return &bt->ws[0];
	return sbq_wait_ptr(bt, &hctx->wait_index);
}

enum {
	BLK_MQ_NO_TAG		= -1U,
	BLK_MQ_TAG_MIN		= 1,
	BLK_MQ_TAG_MAX		= BLK_MQ_NO_TAG - 1,
};

extern bool __blk_mq_tag_busy(struct blk_mq_hw_ctx *);
extern void __blk_mq_tag_idle(struct blk_mq_hw_ctx *);

static inline bool blk_mq_tag_busy(struct blk_mq_hw_ctx *hctx)
{
	if (!(hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED))
		return false;

	return __blk_mq_tag_busy(hctx);
}

static inline void blk_mq_tag_idle(struct blk_mq_hw_ctx *hctx)
{
	if (!(hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED))
		return;

	__blk_mq_tag_idle(hctx);
}

static inline bool blk_mq_tag_is_reserved(struct blk_mq_tags *tags,
					  unsigned int tag)
{
	return tag < tags->nr_reserved_tags;
}

#endif
