/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLK_INTERNAL_H
#define BLK_INTERNAL_H

#include <linux/idr.h>
#include <linux/blk-mq.h>
#include "blk-mq.h"

/* Amount of time in which a process may batch requests */
#define BLK_BATCH_TIME	(HZ/50UL)

/* Number of requests a "batching" process may submit */
#define BLK_BATCH_REQ	32

/* Max future timer expiry for timeouts */
#define BLK_MAX_TIMEOUT		(5 * HZ)

#ifdef CONFIG_DEBUG_FS
extern struct dentry *blk_debugfs_root;
#endif

struct blk_flush_queue {
	unsigned int		flush_queue_delayed:1;
	unsigned int		flush_pending_idx:1;
	unsigned int		flush_running_idx:1;
	unsigned long		flush_pending_since;
	struct list_head	flush_queue[2];
	struct list_head	flush_data_in_flight;
	struct request		*flush_rq;

	/*
	 * flush_rq shares tag with this rq, both can't be active
	 * at the same time
	 */
	struct request		*orig_rq;
	spinlock_t		mq_flush_lock;
};

extern struct kmem_cache *blk_requestq_cachep;
extern struct kmem_cache *request_cachep;
extern struct kobj_type blk_queue_ktype;
extern struct ida blk_queue_ida;

/*
 * @q->queue_lock is set while a queue is being initialized. Since we know
 * that no other threads access the queue object before @q->queue_lock has
 * been set, it is safe to manipulate queue flags without holding the
 * queue_lock if @q->queue_lock == NULL. See also blk_alloc_queue_node() and
 * blk_init_allocated_queue().
 */
static inline void queue_lockdep_assert_held(struct request_queue *q)
{
	if (q->queue_lock)
		lockdep_assert_held(q->queue_lock);
}

static inline void queue_flag_set_unlocked(unsigned int flag,
					   struct request_queue *q)
{
	if (test_bit(QUEUE_FLAG_INIT_DONE, &q->queue_flags) &&
	    kref_read(&q->kobj.kref))
		lockdep_assert_held(q->queue_lock);
	__set_bit(flag, &q->queue_flags);
}

static inline void queue_flag_clear_unlocked(unsigned int flag,
					     struct request_queue *q)
{
	if (test_bit(QUEUE_FLAG_INIT_DONE, &q->queue_flags) &&
	    kref_read(&q->kobj.kref))
		lockdep_assert_held(q->queue_lock);
	__clear_bit(flag, &q->queue_flags);
}

static inline int queue_flag_test_and_clear(unsigned int flag,
					    struct request_queue *q)
{
	queue_lockdep_assert_held(q);

	if (test_bit(flag, &q->queue_flags)) {
		__clear_bit(flag, &q->queue_flags);
		return 1;
	}

	return 0;
}

static inline int queue_flag_test_and_set(unsigned int flag,
					  struct request_queue *q)
{
	queue_lockdep_assert_held(q);

	if (!test_bit(flag, &q->queue_flags)) {
		__set_bit(flag, &q->queue_flags);
		return 0;
	}

	return 1;
}

static inline void queue_flag_set(unsigned int flag, struct request_queue *q)
{
	queue_lockdep_assert_held(q);
	__set_bit(flag, &q->queue_flags);
}

static inline void queue_flag_clear(unsigned int flag, struct request_queue *q)
{
	queue_lockdep_assert_held(q);
	__clear_bit(flag, &q->queue_flags);
}

static inline struct blk_flush_queue *blk_get_flush_queue(
		struct request_queue *q, struct blk_mq_ctx *ctx)
{
	if (q->mq_ops)
		return blk_mq_map_queue(q, ctx->cpu)->fq;
	return q->fq;
}

static inline void __blk_get_queue(struct request_queue *q)
{
	kobject_get(&q->kobj);
}

struct blk_flush_queue *blk_alloc_flush_queue(struct request_queue *q,
		int node, int cmd_size);
void blk_free_flush_queue(struct blk_flush_queue *q);

int blk_init_rl(struct request_list *rl, struct request_queue *q,
		gfp_t gfp_mask);
void blk_exit_rl(struct request_queue *q, struct request_list *rl);
void blk_exit_queue(struct request_queue *q);
void blk_rq_bio_prep(struct request_queue *q, struct request *rq,
			struct bio *bio);
void blk_queue_bypass_start(struct request_queue *q);
void blk_queue_bypass_end(struct request_queue *q);
void __blk_queue_free_tags(struct request_queue *q);
void blk_freeze_queue(struct request_queue *q);

static inline void blk_queue_enter_live(struct request_queue *q)
{
	/*
	 * Given that running in generic_make_request() context
	 * guarantees that a live reference against q_usage_counter has
	 * been established, further references under that same context
	 * need not check that the queue has been frozen (marked dead).
	 */
	percpu_ref_get(&q->q_usage_counter);
}

#ifdef CONFIG_BLK_DEV_INTEGRITY
void blk_flush_integrity(void);
bool __bio_integrity_endio(struct bio *);
static inline bool bio_integrity_endio(struct bio *bio)
{
	if (bio_integrity(bio))
		return __bio_integrity_endio(bio);
	return true;
}
#else
static inline void blk_flush_integrity(void)
{
}
static inline bool bio_integrity_endio(struct bio *bio)
{
	return true;
}
#endif

void blk_timeout_work(struct work_struct *work);
unsigned long blk_rq_timeout(unsigned long timeout);
void blk_add_timer(struct request *req);
void blk_delete_timer(struct request *);


bool bio_attempt_front_merge(struct request_queue *q, struct request *req,
			     struct bio *bio);
bool bio_attempt_back_merge(struct request_queue *q, struct request *req,
			    struct bio *bio);
bool bio_attempt_discard_merge(struct request_queue *q, struct request *req,
		struct bio *bio);
bool blk_attempt_plug_merge(struct request_queue *q, struct bio *bio,
			    unsigned int *request_count,
			    struct request **same_queue_rq);
unsigned int blk_plug_queued_count(struct request_queue *q);

void blk_account_io_start(struct request *req, bool new_io);
void blk_account_io_completion(struct request *req, unsigned int bytes);
void blk_account_io_done(struct request *req, u64 now);

/*
 * EH timer and IO completion will both attempt to 'grab' the request, make
 * sure that only one of them succeeds. Steal the bottom bit of the
 * __deadline field for this.
 */
static inline int blk_mark_rq_complete(struct request *rq)
{
	return test_and_set_bit(0, &rq->__deadline);
}

static inline void blk_clear_rq_complete(struct request *rq)
{
	clear_bit(0, &rq->__deadline);
}

static inline bool blk_rq_is_complete(struct request *rq)
{
	return test_bit(0, &rq->__deadline);
}

/*
 * Internal elevator interface
 */
#define ELV_ON_HASH(rq) ((rq)->rq_flags & RQF_HASHED)

void blk_insert_flush(struct request *rq);

static inline void elv_activate_rq(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.sq.elevator_activate_req_fn)
		e->type->ops.sq.elevator_activate_req_fn(q, rq);
}

static inline void elv_deactivate_rq(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.sq.elevator_deactivate_req_fn)
		e->type->ops.sq.elevator_deactivate_req_fn(q, rq);
}

int elevator_init(struct request_queue *);
int elevator_init_mq(struct request_queue *q);
int elevator_switch_mq(struct request_queue *q,
			      struct elevator_type *new_e);
void elevator_exit(struct request_queue *, struct elevator_queue *);
int elv_register_queue(struct request_queue *q);
void elv_unregister_queue(struct request_queue *q);

struct hd_struct *__disk_get_part(struct gendisk *disk, int partno);

#ifdef CONFIG_FAIL_IO_TIMEOUT
int blk_should_fake_timeout(struct request_queue *);
ssize_t part_timeout_show(struct device *, struct device_attribute *, char *);
ssize_t part_timeout_store(struct device *, struct device_attribute *,
				const char *, size_t);
#else
static inline int blk_should_fake_timeout(struct request_queue *q)
{
	return 0;
}
#endif

int ll_back_merge_fn(struct request_queue *q, struct request *req,
		     struct bio *bio);
int ll_front_merge_fn(struct request_queue *q, struct request *req, 
		      struct bio *bio);
struct request *attempt_back_merge(struct request_queue *q, struct request *rq);
struct request *attempt_front_merge(struct request_queue *q, struct request *rq);
int blk_attempt_req_merge(struct request_queue *q, struct request *rq,
				struct request *next);
void blk_recalc_rq_segments(struct request *rq);
void blk_rq_set_mixed_merge(struct request *rq);
bool blk_rq_merge_ok(struct request *rq, struct bio *bio);
enum elv_merge blk_try_merge(struct request *rq, struct bio *bio);

void blk_queue_congestion_threshold(struct request_queue *q);

int blk_dev_init(void);


/*
 * Return the threshold (number of used requests) at which the queue is
 * considered to be congested.  It include a little hysteresis to keep the
 * context switch rate down.
 */
static inline int queue_congestion_on_threshold(struct request_queue *q)
{
	return q->nr_congestion_on;
}

/*
 * The threshold at which a queue is considered to be uncongested
 */
static inline int queue_congestion_off_threshold(struct request_queue *q)
{
	return q->nr_congestion_off;
}

extern int blk_update_nr_requests(struct request_queue *, unsigned int);

/*
 * Contribute to IO statistics IFF:
 *
 *	a) it's attached to a gendisk, and
 *	b) the queue had IO stats enabled when this request was started, and
 *	c) it's a file system request
 */
static inline bool blk_do_io_stat(struct request *rq)
{
	return rq->rq_disk &&
	       (rq->rq_flags & RQF_IO_STAT) &&
		!blk_rq_is_passthrough(rq);
}

static inline void req_set_nomerge(struct request_queue *q, struct request *req)
{
	req->cmd_flags |= REQ_NOMERGE;
	if (req == q->last_merge)
		q->last_merge = NULL;
}

/*
 * Steal a bit from this field for legacy IO path atomic IO marking. Note that
 * setting the deadline clears the bottom bit, potentially clearing the
 * completed bit. The user has to be OK with this (current ones are fine).
 */
static inline void blk_rq_set_deadline(struct request *rq, unsigned long time)
{
	rq->__deadline = time & ~0x1UL;
}

static inline unsigned long blk_rq_deadline(struct request *rq)
{
	return rq->__deadline & ~0x1UL;
}

/*
 * Internal io_context interface
 */
void get_io_context(struct io_context *ioc);
struct io_cq *ioc_lookup_icq(struct io_context *ioc, struct request_queue *q);
struct io_cq *ioc_create_icq(struct io_context *ioc, struct request_queue *q,
			     gfp_t gfp_mask);
void ioc_clear_queue(struct request_queue *q);

int create_task_io_context(struct task_struct *task, gfp_t gfp_mask, int node);

/**
 * rq_ioc - determine io_context for request allocation
 * @bio: request being allocated is for this bio (can be %NULL)
 *
 * Determine io_context to use for request allocation for @bio.  May return
 * %NULL if %current->io_context doesn't exist.
 */
static inline struct io_context *rq_ioc(struct bio *bio)
{
#ifdef CONFIG_BLK_CGROUP
	if (bio && bio->bi_ioc)
		return bio->bi_ioc;
#endif
	return current->io_context;
}

/**
 * create_io_context - try to create task->io_context
 * @gfp_mask: allocation mask
 * @node: allocation node
 *
 * If %current->io_context is %NULL, allocate a new io_context and install
 * it.  Returns the current %current->io_context which may be %NULL if
 * allocation failed.
 *
 * Note that this function can't be called with IRQ disabled because
 * task_lock which protects %current->io_context is IRQ-unsafe.
 */
static inline struct io_context *create_io_context(gfp_t gfp_mask, int node)
{
	WARN_ON_ONCE(irqs_disabled());
	if (unlikely(!current->io_context))
		create_task_io_context(current, gfp_mask, node);
	return current->io_context;
}

/*
 * Internal throttling interface
 */
#ifdef CONFIG_BLK_DEV_THROTTLING
extern void blk_throtl_drain(struct request_queue *q);
extern int blk_throtl_init(struct request_queue *q);
extern void blk_throtl_exit(struct request_queue *q);
extern void blk_throtl_register_queue(struct request_queue *q);
#else /* CONFIG_BLK_DEV_THROTTLING */
static inline void blk_throtl_drain(struct request_queue *q) { }
static inline int blk_throtl_init(struct request_queue *q) { return 0; }
static inline void blk_throtl_exit(struct request_queue *q) { }
static inline void blk_throtl_register_queue(struct request_queue *q) { }
#endif /* CONFIG_BLK_DEV_THROTTLING */
#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
extern ssize_t blk_throtl_sample_time_show(struct request_queue *q, char *page);
extern ssize_t blk_throtl_sample_time_store(struct request_queue *q,
	const char *page, size_t count);
extern void blk_throtl_bio_endio(struct bio *bio);
extern void blk_throtl_stat_add(struct request *rq, u64 time);
#else
static inline void blk_throtl_bio_endio(struct bio *bio) { }
static inline void blk_throtl_stat_add(struct request *rq, u64 time) { }
#endif

#ifdef CONFIG_BOUNCE
extern int init_emergency_isa_pool(void);
extern void blk_queue_bounce(struct request_queue *q, struct bio **bio);
#else
static inline int init_emergency_isa_pool(void)
{
	return 0;
}
static inline void blk_queue_bounce(struct request_queue *q, struct bio **bio)
{
}
#endif /* CONFIG_BOUNCE */

extern void blk_drain_queue(struct request_queue *q);

#ifdef CONFIG_BLK_CGROUP_IOLATENCY
extern int blk_iolatency_init(struct request_queue *q);
#else
static inline int blk_iolatency_init(struct request_queue *q) { return 0; }
#endif

#endif /* BLK_INTERNAL_H */
