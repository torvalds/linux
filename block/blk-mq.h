/* SPDX-License-Identifier: GPL-2.0 */
#ifndef INT_BLK_MQ_H
#define INT_BLK_MQ_H

#include <linux/blk-mq.h>
#include "blk-stat.h"

struct blk_mq_tag_set;

struct blk_mq_ctxs {
	struct kobject kobj;
	struct blk_mq_ctx __percpu	*queue_ctx;
};

/**
 * struct blk_mq_ctx - State for a software queue facing the submitting CPUs
 */
struct blk_mq_ctx {
	struct {
		spinlock_t		lock;
		struct list_head	rq_lists[HCTX_MAX_TYPES];
	} ____cacheline_aligned_in_smp;

	unsigned int		cpu;
	unsigned short		index_hw[HCTX_MAX_TYPES];
	struct blk_mq_hw_ctx 	*hctxs[HCTX_MAX_TYPES];

	struct request_queue	*queue;
	struct blk_mq_ctxs      *ctxs;
	struct kobject		kobj;
} ____cacheline_aligned_in_smp;

enum {
	BLK_MQ_NO_TAG		= -1U,
	BLK_MQ_TAG_MIN		= 1,
	BLK_MQ_TAG_MAX		= BLK_MQ_NO_TAG - 1,
};

#define BLK_MQ_CPU_WORK_BATCH	(8)

typedef unsigned int __bitwise blk_insert_t;
#define BLK_MQ_INSERT_AT_HEAD		((__force blk_insert_t)0x01)

void blk_mq_submit_bio(struct bio *bio);
int blk_mq_poll(struct request_queue *q, blk_qc_t cookie, struct io_comp_batch *iob,
		unsigned int flags);
void blk_mq_exit_queue(struct request_queue *q);
int blk_mq_update_nr_requests(struct request_queue *q, unsigned int nr);
void blk_mq_wake_waiters(struct request_queue *q);
bool blk_mq_dispatch_rq_list(struct blk_mq_hw_ctx *hctx, struct list_head *,
			     unsigned int);
void blk_mq_flush_busy_ctxs(struct blk_mq_hw_ctx *hctx, struct list_head *list);
struct request *blk_mq_dequeue_from_ctx(struct blk_mq_hw_ctx *hctx,
					struct blk_mq_ctx *start);
void blk_mq_put_rq_ref(struct request *rq);

/*
 * Internal helpers for allocating/freeing the request map
 */
void blk_mq_free_rqs(struct blk_mq_tag_set *set, struct blk_mq_tags *tags,
		     unsigned int hctx_idx);
void blk_mq_free_rq_map(struct blk_mq_tags *tags);
struct blk_mq_tags *blk_mq_alloc_map_and_rqs(struct blk_mq_tag_set *set,
				unsigned int hctx_idx, unsigned int depth);
void blk_mq_free_map_and_rqs(struct blk_mq_tag_set *set,
			     struct blk_mq_tags *tags,
			     unsigned int hctx_idx);

/*
 * CPU -> queue mappings
 */
extern int blk_mq_hw_queue_to_node(struct blk_mq_queue_map *qmap, unsigned int);

/*
 * blk_mq_map_queue_type() - map (hctx_type,cpu) to hardware queue
 * @q: request queue
 * @type: the hctx type index
 * @cpu: CPU
 */
static inline struct blk_mq_hw_ctx *blk_mq_map_queue_type(struct request_queue *q,
							  enum hctx_type type,
							  unsigned int cpu)
{
	return xa_load(&q->hctx_table, q->tag_set->map[type].mq_map[cpu]);
}

static inline enum hctx_type blk_mq_get_hctx_type(blk_opf_t opf)
{
	enum hctx_type type = HCTX_TYPE_DEFAULT;

	/*
	 * The caller ensure that if REQ_POLLED, poll must be enabled.
	 */
	if (opf & REQ_POLLED)
		type = HCTX_TYPE_POLL;
	else if ((opf & REQ_OP_MASK) == REQ_OP_READ)
		type = HCTX_TYPE_READ;
	return type;
}

/*
 * blk_mq_map_queue() - map (cmd_flags,type) to hardware queue
 * @q: request queue
 * @opf: operation type (REQ_OP_*) and flags (e.g. REQ_POLLED).
 * @ctx: software queue cpu ctx
 */
static inline struct blk_mq_hw_ctx *blk_mq_map_queue(struct request_queue *q,
						     blk_opf_t opf,
						     struct blk_mq_ctx *ctx)
{
	return ctx->hctxs[blk_mq_get_hctx_type(opf)];
}

/*
 * sysfs helpers
 */
extern void blk_mq_sysfs_init(struct request_queue *q);
extern void blk_mq_sysfs_deinit(struct request_queue *q);
int blk_mq_sysfs_register(struct gendisk *disk);
void blk_mq_sysfs_unregister(struct gendisk *disk);
int blk_mq_sysfs_register_hctxs(struct request_queue *q);
void blk_mq_sysfs_unregister_hctxs(struct request_queue *q);
extern void blk_mq_hctx_kobj_init(struct blk_mq_hw_ctx *hctx);
void blk_mq_free_plug_rqs(struct blk_plug *plug);
void blk_mq_flush_plug_list(struct blk_plug *plug, bool from_schedule);

void blk_mq_cancel_work_sync(struct request_queue *q);

void blk_mq_release(struct request_queue *q);

static inline struct blk_mq_ctx *__blk_mq_get_ctx(struct request_queue *q,
					   unsigned int cpu)
{
	return per_cpu_ptr(q->queue_ctx, cpu);
}

/*
 * This assumes per-cpu software queueing queues. They could be per-node
 * as well, for instance. For now this is hardcoded as-is. Note that we don't
 * care about preemption, since we know the ctx's are persistent. This does
 * mean that we can't rely on ctx always matching the currently running CPU.
 */
static inline struct blk_mq_ctx *blk_mq_get_ctx(struct request_queue *q)
{
	return __blk_mq_get_ctx(q, raw_smp_processor_id());
}

struct blk_mq_alloc_data {
	/* input parameter */
	struct request_queue *q;
	blk_mq_req_flags_t flags;
	unsigned int shallow_depth;
	blk_opf_t cmd_flags;
	req_flags_t rq_flags;

	/* allocate multiple requests/tags in one go */
	unsigned int nr_tags;
	struct rq_list *cached_rqs;

	/* input & output parameter */
	struct blk_mq_ctx *ctx;
	struct blk_mq_hw_ctx *hctx;
};

struct blk_mq_tags *blk_mq_init_tags(unsigned int nr_tags,
		unsigned int reserved_tags, int node, int alloc_policy);
void blk_mq_free_tags(struct blk_mq_tags *tags);
int blk_mq_init_bitmaps(struct sbitmap_queue *bitmap_tags,
		struct sbitmap_queue *breserved_tags, unsigned int queue_depth,
		unsigned int reserved, int node, int alloc_policy);

unsigned int blk_mq_get_tag(struct blk_mq_alloc_data *data);
unsigned long blk_mq_get_tags(struct blk_mq_alloc_data *data, int nr_tags,
		unsigned int *offset);
void blk_mq_put_tag(struct blk_mq_tags *tags, struct blk_mq_ctx *ctx,
		unsigned int tag);
void blk_mq_put_tags(struct blk_mq_tags *tags, int *tag_array, int nr_tags);
int blk_mq_tag_update_depth(struct blk_mq_hw_ctx *hctx,
		struct blk_mq_tags **tags, unsigned int depth, bool can_grow);
void blk_mq_tag_resize_shared_tags(struct blk_mq_tag_set *set,
		unsigned int size);
void blk_mq_tag_update_sched_shared_tags(struct request_queue *q);

void blk_mq_tag_wakeup_all(struct blk_mq_tags *tags, bool);
void blk_mq_queue_tag_busy_iter(struct request_queue *q, busy_tag_iter_fn *fn,
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

void __blk_mq_tag_busy(struct blk_mq_hw_ctx *);
void __blk_mq_tag_idle(struct blk_mq_hw_ctx *);

static inline void blk_mq_tag_busy(struct blk_mq_hw_ctx *hctx)
{
	if (hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED)
		__blk_mq_tag_busy(hctx);
}

static inline void blk_mq_tag_idle(struct blk_mq_hw_ctx *hctx)
{
	if (hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED)
		__blk_mq_tag_idle(hctx);
}

static inline bool blk_mq_tag_is_reserved(struct blk_mq_tags *tags,
					  unsigned int tag)
{
	return tag < tags->nr_reserved_tags;
}

static inline bool blk_mq_is_shared_tags(unsigned int flags)
{
	return flags & BLK_MQ_F_TAG_HCTX_SHARED;
}

static inline struct blk_mq_tags *blk_mq_tags_from_data(struct blk_mq_alloc_data *data)
{
	if (data->rq_flags & RQF_SCHED_TAGS)
		return data->hctx->sched_tags;
	return data->hctx->tags;
}

static inline bool blk_mq_hctx_stopped(struct blk_mq_hw_ctx *hctx)
{
	/* Fast path: hardware queue is not stopped most of the time. */
	if (likely(!test_bit(BLK_MQ_S_STOPPED, &hctx->state)))
		return false;

	/*
	 * This barrier is used to order adding of dispatch list before and
	 * the test of BLK_MQ_S_STOPPED below. Pairs with the memory barrier
	 * in blk_mq_start_stopped_hw_queue() so that dispatch code could
	 * either see BLK_MQ_S_STOPPED is cleared or dispatch list is not
	 * empty to avoid missing dispatching requests.
	 */
	smp_mb();

	return test_bit(BLK_MQ_S_STOPPED, &hctx->state);
}

static inline bool blk_mq_hw_queue_mapped(struct blk_mq_hw_ctx *hctx)
{
	return hctx->nr_ctx && hctx->tags;
}

unsigned int blk_mq_in_flight(struct request_queue *q,
		struct block_device *part);
void blk_mq_in_flight_rw(struct request_queue *q, struct block_device *part,
		unsigned int inflight[2]);

static inline void blk_mq_put_dispatch_budget(struct request_queue *q,
					      int budget_token)
{
	if (q->mq_ops->put_budget)
		q->mq_ops->put_budget(q, budget_token);
}

static inline int blk_mq_get_dispatch_budget(struct request_queue *q)
{
	if (q->mq_ops->get_budget)
		return q->mq_ops->get_budget(q);
	return 0;
}

static inline void blk_mq_set_rq_budget_token(struct request *rq, int token)
{
	if (token < 0)
		return;

	if (rq->q->mq_ops->set_rq_budget_token)
		rq->q->mq_ops->set_rq_budget_token(rq, token);
}

static inline int blk_mq_get_rq_budget_token(struct request *rq)
{
	if (rq->q->mq_ops->get_rq_budget_token)
		return rq->q->mq_ops->get_rq_budget_token(rq);
	return -1;
}

static inline void __blk_mq_add_active_requests(struct blk_mq_hw_ctx *hctx,
						int val)
{
	if (blk_mq_is_shared_tags(hctx->flags))
		atomic_add(val, &hctx->queue->nr_active_requests_shared_tags);
	else
		atomic_add(val, &hctx->nr_active);
}

static inline void __blk_mq_inc_active_requests(struct blk_mq_hw_ctx *hctx)
{
	__blk_mq_add_active_requests(hctx, 1);
}

static inline void __blk_mq_sub_active_requests(struct blk_mq_hw_ctx *hctx,
		int val)
{
	if (blk_mq_is_shared_tags(hctx->flags))
		atomic_sub(val, &hctx->queue->nr_active_requests_shared_tags);
	else
		atomic_sub(val, &hctx->nr_active);
}

static inline void __blk_mq_dec_active_requests(struct blk_mq_hw_ctx *hctx)
{
	__blk_mq_sub_active_requests(hctx, 1);
}

static inline void blk_mq_add_active_requests(struct blk_mq_hw_ctx *hctx,
					      int val)
{
	if (hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED)
		__blk_mq_add_active_requests(hctx, val);
}

static inline void blk_mq_inc_active_requests(struct blk_mq_hw_ctx *hctx)
{
	if (hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED)
		__blk_mq_inc_active_requests(hctx);
}

static inline void blk_mq_sub_active_requests(struct blk_mq_hw_ctx *hctx,
					      int val)
{
	if (hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED)
		__blk_mq_sub_active_requests(hctx, val);
}

static inline void blk_mq_dec_active_requests(struct blk_mq_hw_ctx *hctx)
{
	if (hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED)
		__blk_mq_dec_active_requests(hctx);
}

static inline int __blk_mq_active_requests(struct blk_mq_hw_ctx *hctx)
{
	if (blk_mq_is_shared_tags(hctx->flags))
		return atomic_read(&hctx->queue->nr_active_requests_shared_tags);
	return atomic_read(&hctx->nr_active);
}
static inline void __blk_mq_put_driver_tag(struct blk_mq_hw_ctx *hctx,
					   struct request *rq)
{
	blk_mq_dec_active_requests(hctx);
	blk_mq_put_tag(hctx->tags, rq->mq_ctx, rq->tag);
	rq->tag = BLK_MQ_NO_TAG;
}

static inline void blk_mq_put_driver_tag(struct request *rq)
{
	if (rq->tag == BLK_MQ_NO_TAG || rq->internal_tag == BLK_MQ_NO_TAG)
		return;

	__blk_mq_put_driver_tag(rq->mq_hctx, rq);
}

bool __blk_mq_alloc_driver_tag(struct request *rq);

static inline bool blk_mq_get_driver_tag(struct request *rq)
{
	if (rq->tag == BLK_MQ_NO_TAG && !__blk_mq_alloc_driver_tag(rq))
		return false;

	return true;
}

static inline void blk_mq_clear_mq_map(struct blk_mq_queue_map *qmap)
{
	int cpu;

	for_each_possible_cpu(cpu)
		qmap->mq_map[cpu] = 0;
}

/* Free all requests on the list */
static inline void blk_mq_free_requests(struct list_head *list)
{
	while (!list_empty(list)) {
		struct request *rq = list_entry_rq(list->next);

		list_del_init(&rq->queuelist);
		blk_mq_free_request(rq);
	}
}

/*
 * For shared tag users, we track the number of currently active users
 * and attempt to provide a fair share of the tag depth for each of them.
 */
static inline bool hctx_may_queue(struct blk_mq_hw_ctx *hctx,
				  struct sbitmap_queue *bt)
{
	unsigned int depth, users;

	if (!hctx || !(hctx->flags & BLK_MQ_F_TAG_QUEUE_SHARED))
		return true;

	/*
	 * Don't try dividing an ant
	 */
	if (bt->sb.depth == 1)
		return true;

	if (blk_mq_is_shared_tags(hctx->flags)) {
		struct request_queue *q = hctx->queue;

		if (!test_bit(QUEUE_FLAG_HCTX_ACTIVE, &q->queue_flags))
			return true;
	} else {
		if (!test_bit(BLK_MQ_S_TAG_ACTIVE, &hctx->state))
			return true;
	}

	users = READ_ONCE(hctx->tags->active_queues);
	if (!users)
		return true;

	/*
	 * Allow at least some tags
	 */
	depth = max((bt->sb.depth + users - 1) / users, 4U);
	return __blk_mq_active_requests(hctx) < depth;
}

/* run the code block in @dispatch_ops with rcu/srcu read lock held */
#define __blk_mq_run_dispatch_ops(q, check_sleep, dispatch_ops)	\
do {								\
	if ((q)->tag_set->flags & BLK_MQ_F_BLOCKING) {		\
		struct blk_mq_tag_set *__tag_set = (q)->tag_set; \
		int srcu_idx;					\
								\
		might_sleep_if(check_sleep);			\
		srcu_idx = srcu_read_lock(__tag_set->srcu);	\
		(dispatch_ops);					\
		srcu_read_unlock(__tag_set->srcu, srcu_idx);	\
	} else {						\
		rcu_read_lock();				\
		(dispatch_ops);					\
		rcu_read_unlock();				\
	}							\
} while (0)

#define blk_mq_run_dispatch_ops(q, dispatch_ops)		\
	__blk_mq_run_dispatch_ops(q, true, dispatch_ops)	\

#endif
