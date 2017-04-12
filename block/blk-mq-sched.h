#ifndef BLK_MQ_SCHED_H
#define BLK_MQ_SCHED_H

#include "blk-mq.h"
#include "blk-mq-tag.h"

int blk_mq_sched_init_hctx_data(struct request_queue *q, size_t size,
				int (*init)(struct blk_mq_hw_ctx *),
				void (*exit)(struct blk_mq_hw_ctx *));

void blk_mq_sched_free_hctx_data(struct request_queue *q,
				 void (*exit)(struct blk_mq_hw_ctx *));

struct request *blk_mq_sched_get_request(struct request_queue *q, struct bio *bio, unsigned int op, struct blk_mq_alloc_data *data);
void blk_mq_sched_put_request(struct request *rq);

void blk_mq_sched_request_inserted(struct request *rq);
bool blk_mq_sched_try_merge(struct request_queue *q, struct bio *bio,
				struct request **merged_request);
bool __blk_mq_sched_bio_merge(struct request_queue *q, struct bio *bio);
bool blk_mq_sched_try_insert_merge(struct request_queue *q, struct request *rq);
void blk_mq_sched_restart(struct blk_mq_hw_ctx *hctx);

void blk_mq_sched_insert_request(struct request *rq, bool at_head,
				 bool run_queue, bool async, bool can_block);
void blk_mq_sched_insert_requests(struct request_queue *q,
				  struct blk_mq_ctx *ctx,
				  struct list_head *list, bool run_queue_async);

void blk_mq_sched_dispatch_requests(struct blk_mq_hw_ctx *hctx);
void blk_mq_sched_move_to_dispatch(struct blk_mq_hw_ctx *hctx,
			struct list_head *rq_list,
			struct request *(*get_rq)(struct blk_mq_hw_ctx *));

int blk_mq_init_sched(struct request_queue *q, struct elevator_type *e);
void blk_mq_exit_sched(struct request_queue *q, struct elevator_queue *e);

int blk_mq_sched_init_hctx(struct request_queue *q, struct blk_mq_hw_ctx *hctx,
			   unsigned int hctx_idx);
void blk_mq_sched_exit_hctx(struct request_queue *q, struct blk_mq_hw_ctx *hctx,
			    unsigned int hctx_idx);

int blk_mq_sched_init(struct request_queue *q);

static inline bool
blk_mq_sched_bio_merge(struct request_queue *q, struct bio *bio)
{
	struct elevator_queue *e = q->elevator;

	if (!e || blk_queue_nomerges(q) || !bio_mergeable(bio))
		return false;

	return __blk_mq_sched_bio_merge(q, bio);
}

static inline int blk_mq_sched_get_rq_priv(struct request_queue *q,
					   struct request *rq,
					   struct bio *bio)
{
	struct elevator_queue *e = q->elevator;

	if (e && e->type->ops.mq.get_rq_priv)
		return e->type->ops.mq.get_rq_priv(q, rq, bio);

	return 0;
}

static inline void blk_mq_sched_put_rq_priv(struct request_queue *q,
					    struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e && e->type->ops.mq.put_rq_priv)
		e->type->ops.mq.put_rq_priv(q, rq);
}

static inline bool
blk_mq_sched_allow_merge(struct request_queue *q, struct request *rq,
			 struct bio *bio)
{
	struct elevator_queue *e = q->elevator;

	if (e && e->type->ops.mq.allow_merge)
		return e->type->ops.mq.allow_merge(q, rq, bio);

	return true;
}

static inline void
blk_mq_sched_completed_request(struct blk_mq_hw_ctx *hctx, struct request *rq)
{
	struct elevator_queue *e = hctx->queue->elevator;

	if (e && e->type->ops.mq.completed_request)
		e->type->ops.mq.completed_request(hctx, rq);

	BUG_ON(rq->internal_tag == -1);

	blk_mq_put_tag(hctx, hctx->sched_tags, rq->mq_ctx, rq->internal_tag);
}

static inline void blk_mq_sched_started_request(struct request *rq)
{
	struct request_queue *q = rq->q;
	struct elevator_queue *e = q->elevator;

	if (e && e->type->ops.mq.started_request)
		e->type->ops.mq.started_request(rq);
}

static inline void blk_mq_sched_requeue_request(struct request *rq)
{
	struct request_queue *q = rq->q;
	struct elevator_queue *e = q->elevator;

	if (e && e->type->ops.mq.requeue_request)
		e->type->ops.mq.requeue_request(rq);
}

static inline bool blk_mq_sched_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct elevator_queue *e = hctx->queue->elevator;

	if (e && e->type->ops.mq.has_work)
		return e->type->ops.mq.has_work(hctx);

	return false;
}

/*
 * Mark a hardware queue as needing a restart.
 */
static inline void blk_mq_sched_mark_restart_hctx(struct blk_mq_hw_ctx *hctx)
{
	if (!test_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state))
		set_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state);
}

static inline bool blk_mq_sched_needs_restart(struct blk_mq_hw_ctx *hctx)
{
	return test_bit(BLK_MQ_S_SCHED_RESTART, &hctx->state);
}

#endif
