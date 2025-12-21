/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLK_MQ_SCHED_H
#define BLK_MQ_SCHED_H

#include "elevator.h"
#include "blk-mq.h"

#define MAX_SCHED_RQ (16 * BLKDEV_DEFAULT_RQ)

bool blk_mq_sched_try_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs, struct request **merged_request);
bool blk_mq_sched_bio_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs);
bool blk_mq_sched_try_insert_merge(struct request_queue *q, struct request *rq,
				   struct list_head *free);
void blk_mq_sched_mark_restart_hctx(struct blk_mq_hw_ctx *hctx);
void __blk_mq_sched_restart(struct blk_mq_hw_ctx *hctx);

void blk_mq_sched_dispatch_requests(struct blk_mq_hw_ctx *hctx);

int blk_mq_init_sched(struct request_queue *q, struct elevator_type *e,
		struct elevator_resources *res);
void blk_mq_exit_sched(struct request_queue *q, struct elevator_queue *e);
void blk_mq_sched_free_rqs(struct request_queue *q);

struct elevator_tags *blk_mq_alloc_sched_tags(struct blk_mq_tag_set *set,
		unsigned int nr_hw_queues, unsigned int nr_requests);
int blk_mq_alloc_sched_res(struct request_queue *q,
		struct elevator_type *type,
		struct elevator_resources *res,
		unsigned int nr_hw_queues);
int blk_mq_alloc_sched_res_batch(struct xarray *elv_tbl,
		struct blk_mq_tag_set *set, unsigned int nr_hw_queues);
int blk_mq_alloc_sched_ctx_batch(struct xarray *elv_tbl,
		struct blk_mq_tag_set *set);
void blk_mq_free_sched_ctx_batch(struct xarray *elv_tbl);
void blk_mq_free_sched_tags(struct elevator_tags *et,
		struct blk_mq_tag_set *set);
void blk_mq_free_sched_res(struct elevator_resources *res,
		struct elevator_type *type,
		struct blk_mq_tag_set *set);
void blk_mq_free_sched_res_batch(struct xarray *et_table,
		struct blk_mq_tag_set *set);
/*
 * blk_mq_alloc_sched_data() - Allocates scheduler specific data
 * Returns:
 *         - Pointer to allocated data on success
 *         - NULL if no allocation needed
 *         - ERR_PTR(-ENOMEM) in case of failure
 */
static inline void *blk_mq_alloc_sched_data(struct request_queue *q,
		struct elevator_type *e)
{
	void *sched_data;

	if (!e || !e->ops.alloc_sched_data)
		return NULL;

	sched_data = e->ops.alloc_sched_data(q);
	return (sched_data) ?: ERR_PTR(-ENOMEM);
}

static inline void blk_mq_free_sched_data(struct elevator_type *e, void *data)
{
	if (e && e->ops.free_sched_data)
		e->ops.free_sched_data(data);
}

static inline void blk_mq_sched_restart(struct blk_mq_hw_ctx *hctx)
{
	if (test_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state))
		__blk_mq_sched_restart(hctx);
}

static inline bool bio_mergeable(struct bio *bio)
{
	return !(bio->bi_opf & REQ_NOMERGE_FLAGS);
}

static inline bool
blk_mq_sched_allow_merge(struct request_queue *q, struct request *rq,
			 struct bio *bio)
{
	if (rq->rq_flags & RQF_USE_SCHED) {
		struct elevator_queue *e = q->elevator;

		if (e->type->ops.allow_merge)
			return e->type->ops.allow_merge(q, rq, bio);
	}
	return true;
}

static inline void blk_mq_sched_completed_request(struct request *rq, u64 now)
{
	if (rq->rq_flags & RQF_USE_SCHED) {
		struct elevator_queue *e = rq->q->elevator;

		if (e->type->ops.completed_request)
			e->type->ops.completed_request(rq, now);
	}
}

static inline void blk_mq_sched_requeue_request(struct request *rq)
{
	if (rq->rq_flags & RQF_USE_SCHED) {
		struct request_queue *q = rq->q;
		struct elevator_queue *e = q->elevator;

		if (e->type->ops.requeue_request)
			e->type->ops.requeue_request(rq);
	}
}

static inline bool blk_mq_sched_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct elevator_queue *e = hctx->queue->elevator;

	if (e && e->type->ops.has_work)
		return e->type->ops.has_work(hctx);

	return false;
}

static inline bool blk_mq_sched_needs_restart(struct blk_mq_hw_ctx *hctx)
{
	return test_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state);
}

static inline void blk_mq_set_min_shallow_depth(struct request_queue *q,
						unsigned int depth)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned long i;

	queue_for_each_hw_ctx(q, hctx, i)
		sbitmap_queue_min_shallow_depth(&hctx->sched_tags->bitmap_tags,
						depth);
}

#endif
