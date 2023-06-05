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

int blk_mq_init_sched(struct request_queue *q, struct elevator_type *e);
void blk_mq_exit_sched(struct request_queue *q, struct elevator_queue *e);
void blk_mq_sched_free_rqs(struct request_queue *q);

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
	if (rq->rq_flags & RQF_ELV) {
		struct elevator_queue *e = q->elevator;

		if (e->type->ops.allow_merge)
			return e->type->ops.allow_merge(q, rq, bio);
	}
	return true;
}

static inline void blk_mq_sched_completed_request(struct request *rq, u64 now)
{
	if (rq->rq_flags & RQF_ELV) {
		struct elevator_queue *e = rq->q->elevator;

		if (e->type->ops.completed_request)
			e->type->ops.completed_request(rq, now);
	}
}

static inline void blk_mq_sched_requeue_request(struct request *rq)
{
	if (rq->rq_flags & RQF_ELV) {
		struct request_queue *q = rq->q;
		struct elevator_queue *e = q->elevator;

		if ((rq->rq_flags & RQF_ELVPRIV) && e->type->ops.requeue_request)
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

#endif
