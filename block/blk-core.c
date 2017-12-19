/*
 * Copyright (C) 1991, 1992 Linus Torvalds
 * Copyright (C) 1994,      Karl Keyte: Added support for disk statistics
 * Elevator latency, (C) 2000  Andrea Arcangeli <andrea@suse.de> SuSE
 * Queue request tables / lock, selectable elevator, Jens Axboe <axboe@suse.de>
 * kernel-doc documentation started by NeilBrown <neilb@cse.unsw.edu.au>
 *	-  July2000
 * bio rewrite, highmem i/o, etc, Jens Axboe <axboe@suse.de> - may 2001
 */

/*
 * This handles all read/write requests to block devices
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/fault-inject.h>
#include <linux/list_sort.h>
#include <linux/delay.h>
#include <linux/ratelimit.h>
#include <linux/pm_runtime.h>
#include <linux/blk-cgroup.h>

#define CREATE_TRACE_POINTS
#include <trace/events/block.h>

#include "blk.h"
#include "blk-mq.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(block_bio_remap);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_remap);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_bio_complete);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_split);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_unplug);

DEFINE_IDA(blk_queue_ida);

/*
 * For the allocated request tables
 */
struct kmem_cache *request_cachep = NULL;

/*
 * For queue allocation
 */
struct kmem_cache *blk_requestq_cachep;

/*
 * Controlling structure to kblockd
 */
static struct workqueue_struct *kblockd_workqueue;

static void blk_clear_congested(struct request_list *rl, int sync)
{
#ifdef CONFIG_CGROUP_WRITEBACK
	clear_wb_congested(rl->blkg->wb_congested, sync);
#else
	/*
	 * If !CGROUP_WRITEBACK, all blkg's map to bdi->wb and we shouldn't
	 * flip its congestion state for events on other blkcgs.
	 */
	if (rl == &rl->q->root_rl)
		clear_wb_congested(rl->q->backing_dev_info.wb.congested, sync);
#endif
}

static void blk_set_congested(struct request_list *rl, int sync)
{
#ifdef CONFIG_CGROUP_WRITEBACK
	set_wb_congested(rl->blkg->wb_congested, sync);
#else
	/* see blk_clear_congested() */
	if (rl == &rl->q->root_rl)
		set_wb_congested(rl->q->backing_dev_info.wb.congested, sync);
#endif
}

void blk_queue_congestion_threshold(struct request_queue *q)
{
	int nr;

	nr = q->nr_requests - (q->nr_requests / 8) + 1;
	if (nr > q->nr_requests)
		nr = q->nr_requests;
	q->nr_congestion_on = nr;

	nr = q->nr_requests - (q->nr_requests / 8) - (q->nr_requests / 16) - 1;
	if (nr < 1)
		nr = 1;
	q->nr_congestion_off = nr;
}

/**
 * blk_get_backing_dev_info - get the address of a queue's backing_dev_info
 * @bdev:	device
 *
 * Locates the passed device's request queue and returns the address of its
 * backing_dev_info.  This function can only be called if @bdev is opened
 * and the return value is never NULL.
 */
struct backing_dev_info *blk_get_backing_dev_info(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	return &q->backing_dev_info;
}
EXPORT_SYMBOL(blk_get_backing_dev_info);

void blk_rq_init(struct request_queue *q, struct request *rq)
{
	memset(rq, 0, sizeof(*rq));

	INIT_LIST_HEAD(&rq->queuelist);
	INIT_LIST_HEAD(&rq->timeout_list);
	rq->cpu = -1;
	rq->q = q;
	rq->__sector = (sector_t) -1;
	INIT_HLIST_NODE(&rq->hash);
	RB_CLEAR_NODE(&rq->rb_node);
	rq->cmd = rq->__cmd;
	rq->cmd_len = BLK_MAX_CDB;
	rq->tag = -1;
	rq->start_time = jiffies;
	set_start_time_ns(rq);
	rq->part = NULL;
}
EXPORT_SYMBOL(blk_rq_init);

static void req_bio_endio(struct request *rq, struct bio *bio,
			  unsigned int nbytes, int error)
{
	if (error)
		bio->bi_error = error;

	if (unlikely(rq->cmd_flags & REQ_QUIET))
		bio_set_flag(bio, BIO_QUIET);

	bio_advance(bio, nbytes);

	/* don't actually finish bio if it's part of flush sequence */
	if (bio->bi_iter.bi_size == 0 && !(rq->cmd_flags & REQ_FLUSH_SEQ))
		bio_endio(bio);
}

void blk_dump_rq_flags(struct request *rq, char *msg)
{
	int bit;

	printk(KERN_INFO "%s: dev %s: type=%x, flags=%llx\n", msg,
		rq->rq_disk ? rq->rq_disk->disk_name : "?", rq->cmd_type,
		(unsigned long long) rq->cmd_flags);

	printk(KERN_INFO "  sector %llu, nr/cnr %u/%u\n",
	       (unsigned long long)blk_rq_pos(rq),
	       blk_rq_sectors(rq), blk_rq_cur_sectors(rq));
	printk(KERN_INFO "  bio %p, biotail %p, len %u\n",
	       rq->bio, rq->biotail, blk_rq_bytes(rq));

	if (rq->cmd_type == REQ_TYPE_BLOCK_PC) {
		printk(KERN_INFO "  cdb: ");
		for (bit = 0; bit < BLK_MAX_CDB; bit++)
			printk("%02x ", rq->cmd[bit]);
		printk("\n");
	}
}
EXPORT_SYMBOL(blk_dump_rq_flags);

static void blk_delay_work(struct work_struct *work)
{
	struct request_queue *q;

	q = container_of(work, struct request_queue, delay_work.work);
	spin_lock_irq(q->queue_lock);
	__blk_run_queue(q);
	spin_unlock_irq(q->queue_lock);
}

/**
 * blk_delay_queue - restart queueing after defined interval
 * @q:		The &struct request_queue in question
 * @msecs:	Delay in msecs
 *
 * Description:
 *   Sometimes queueing needs to be postponed for a little while, to allow
 *   resources to come back. This function will make sure that queueing is
 *   restarted around the specified time. Queue lock must be held.
 */
void blk_delay_queue(struct request_queue *q, unsigned long msecs)
{
	if (likely(!blk_queue_dead(q)))
		queue_delayed_work(kblockd_workqueue, &q->delay_work,
				   msecs_to_jiffies(msecs));
}
EXPORT_SYMBOL(blk_delay_queue);

/**
 * blk_start_queue_async - asynchronously restart a previously stopped queue
 * @q:    The &struct request_queue in question
 *
 * Description:
 *   blk_start_queue_async() will clear the stop flag on the queue, and
 *   ensure that the request_fn for the queue is run from an async
 *   context.
 **/
void blk_start_queue_async(struct request_queue *q)
{
	queue_flag_clear(QUEUE_FLAG_STOPPED, q);
	blk_run_queue_async(q);
}
EXPORT_SYMBOL(blk_start_queue_async);

/**
 * blk_start_queue - restart a previously stopped queue
 * @q:    The &struct request_queue in question
 *
 * Description:
 *   blk_start_queue() will clear the stop flag on the queue, and call
 *   the request_fn for the queue if it was in a stopped state when
 *   entered. Also see blk_stop_queue(). Queue lock must be held.
 **/
void blk_start_queue(struct request_queue *q)
{
	WARN_ON(!in_interrupt() && !irqs_disabled());

	queue_flag_clear(QUEUE_FLAG_STOPPED, q);
	__blk_run_queue(q);
}
EXPORT_SYMBOL(blk_start_queue);

/**
 * blk_stop_queue - stop a queue
 * @q:    The &struct request_queue in question
 *
 * Description:
 *   The Linux block layer assumes that a block driver will consume all
 *   entries on the request queue when the request_fn strategy is called.
 *   Often this will not happen, because of hardware limitations (queue
 *   depth settings). If a device driver gets a 'queue full' response,
 *   or if it simply chooses not to queue more I/O at one point, it can
 *   call this function to prevent the request_fn from being called until
 *   the driver has signalled it's ready to go again. This happens by calling
 *   blk_start_queue() to restart queue operations. Queue lock must be held.
 **/
void blk_stop_queue(struct request_queue *q)
{
	cancel_delayed_work(&q->delay_work);
	queue_flag_set(QUEUE_FLAG_STOPPED, q);
}
EXPORT_SYMBOL(blk_stop_queue);

/**
 * blk_sync_queue - cancel any pending callbacks on a queue
 * @q: the queue
 *
 * Description:
 *     The block layer may perform asynchronous callback activity
 *     on a queue, such as calling the unplug function after a timeout.
 *     A block device may call blk_sync_queue to ensure that any
 *     such activity is cancelled, thus allowing it to release resources
 *     that the callbacks might use. The caller must already have made sure
 *     that its ->make_request_fn will not re-add plugging prior to calling
 *     this function.
 *
 *     This function does not cancel any asynchronous activity arising
 *     out of elevator or throttling code. That would require elevator_exit()
 *     and blkcg_exit_queue() to be called with queue lock initialized.
 *
 */
void blk_sync_queue(struct request_queue *q)
{
	del_timer_sync(&q->timeout);

	if (q->mq_ops) {
		struct blk_mq_hw_ctx *hctx;
		int i;

		queue_for_each_hw_ctx(q, hctx, i) {
			cancel_delayed_work_sync(&hctx->run_work);
			cancel_delayed_work_sync(&hctx->delay_work);
		}
	} else {
		cancel_delayed_work_sync(&q->delay_work);
	}
}
EXPORT_SYMBOL(blk_sync_queue);

/**
 * __blk_run_queue_uncond - run a queue whether or not it has been stopped
 * @q:	The queue to run
 *
 * Description:
 *    Invoke request handling on a queue if there are any pending requests.
 *    May be used to restart request handling after a request has completed.
 *    This variant runs the queue whether or not the queue has been
 *    stopped. Must be called with the queue lock held and interrupts
 *    disabled. See also @blk_run_queue.
 */
inline void __blk_run_queue_uncond(struct request_queue *q)
{
	if (unlikely(blk_queue_dead(q)))
		return;

	/*
	 * Some request_fn implementations, e.g. scsi_request_fn(), unlock
	 * the queue lock internally. As a result multiple threads may be
	 * running such a request function concurrently. Keep track of the
	 * number of active request_fn invocations such that blk_drain_queue()
	 * can wait until all these request_fn calls have finished.
	 */
	q->request_fn_active++;
	q->request_fn(q);
	q->request_fn_active--;
}
EXPORT_SYMBOL_GPL(__blk_run_queue_uncond);

/**
 * __blk_run_queue - run a single device queue
 * @q:	The queue to run
 *
 * Description:
 *    See @blk_run_queue. This variant must be called with the queue lock
 *    held and interrupts disabled.
 */
void __blk_run_queue(struct request_queue *q)
{
	if (unlikely(blk_queue_stopped(q)))
		return;

	__blk_run_queue_uncond(q);
}
EXPORT_SYMBOL(__blk_run_queue);

/**
 * blk_run_queue_async - run a single device queue in workqueue context
 * @q:	The queue to run
 *
 * Description:
 *    Tells kblockd to perform the equivalent of @blk_run_queue on behalf
 *    of us. The caller must hold the queue lock.
 */
void blk_run_queue_async(struct request_queue *q)
{
	if (likely(!blk_queue_stopped(q) && !blk_queue_dead(q)))
		mod_delayed_work(kblockd_workqueue, &q->delay_work, 0);
}
EXPORT_SYMBOL(blk_run_queue_async);

/**
 * blk_run_queue - run a single device queue
 * @q: The queue to run
 *
 * Description:
 *    Invoke request handling on this queue, if it has pending work to do.
 *    May be used to restart queueing when a request has completed.
 */
void blk_run_queue(struct request_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	__blk_run_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}
EXPORT_SYMBOL(blk_run_queue);

void blk_put_queue(struct request_queue *q)
{
	kobject_put(&q->kobj);
}
EXPORT_SYMBOL(blk_put_queue);

/**
 * __blk_drain_queue - drain requests from request_queue
 * @q: queue to drain
 * @drain_all: whether to drain all requests or only the ones w/ ELVPRIV
 *
 * Drain requests from @q.  If @drain_all is set, all requests are drained.
 * If not, only ELVPRIV requests are drained.  The caller is responsible
 * for ensuring that no new requests which need to be drained are queued.
 */
static void __blk_drain_queue(struct request_queue *q, bool drain_all)
	__releases(q->queue_lock)
	__acquires(q->queue_lock)
{
	int i;

	lockdep_assert_held(q->queue_lock);

	while (true) {
		bool drain = false;

		/*
		 * The caller might be trying to drain @q before its
		 * elevator is initialized.
		 */
		if (q->elevator)
			elv_drain_elevator(q);

		blkcg_drain_queue(q);

		/*
		 * This function might be called on a queue which failed
		 * driver init after queue creation or is not yet fully
		 * active yet.  Some drivers (e.g. fd and loop) get unhappy
		 * in such cases.  Kick queue iff dispatch queue has
		 * something on it and @q has request_fn set.
		 */
		if (!list_empty(&q->queue_head) && q->request_fn)
			__blk_run_queue(q);

		drain |= q->nr_rqs_elvpriv;
		drain |= q->request_fn_active;

		/*
		 * Unfortunately, requests are queued at and tracked from
		 * multiple places and there's no single counter which can
		 * be drained.  Check all the queues and counters.
		 */
		if (drain_all) {
			struct blk_flush_queue *fq = blk_get_flush_queue(q, NULL);
			drain |= !list_empty(&q->queue_head);
			for (i = 0; i < 2; i++) {
				drain |= q->nr_rqs[i];
				drain |= q->in_flight[i];
				if (fq)
				    drain |= !list_empty(&fq->flush_queue[i]);
			}
		}

		if (!drain)
			break;

		spin_unlock_irq(q->queue_lock);

		msleep(10);

		spin_lock_irq(q->queue_lock);
	}

	/*
	 * With queue marked dead, any woken up waiter will fail the
	 * allocation path, so the wakeup chaining is lost and we're
	 * left with hung waiters. We need to wake up those waiters.
	 */
	if (q->request_fn) {
		struct request_list *rl;

		blk_queue_for_each_rl(rl, q)
			for (i = 0; i < ARRAY_SIZE(rl->wait); i++)
				wake_up_all(&rl->wait[i]);
	}
}

/**
 * blk_queue_bypass_start - enter queue bypass mode
 * @q: queue of interest
 *
 * In bypass mode, only the dispatch FIFO queue of @q is used.  This
 * function makes @q enter bypass mode and drains all requests which were
 * throttled or issued before.  On return, it's guaranteed that no request
 * is being throttled or has ELVPRIV set and blk_queue_bypass() %true
 * inside queue or RCU read lock.
 */
void blk_queue_bypass_start(struct request_queue *q)
{
	spin_lock_irq(q->queue_lock);
	q->bypass_depth++;
	queue_flag_set(QUEUE_FLAG_BYPASS, q);
	spin_unlock_irq(q->queue_lock);

	/*
	 * Queues start drained.  Skip actual draining till init is
	 * complete.  This avoids lenghty delays during queue init which
	 * can happen many times during boot.
	 */
	if (blk_queue_init_done(q)) {
		spin_lock_irq(q->queue_lock);
		__blk_drain_queue(q, false);
		spin_unlock_irq(q->queue_lock);

		/* ensure blk_queue_bypass() is %true inside RCU read lock */
		synchronize_rcu();
	}
}
EXPORT_SYMBOL_GPL(blk_queue_bypass_start);

/**
 * blk_queue_bypass_end - leave queue bypass mode
 * @q: queue of interest
 *
 * Leave bypass mode and restore the normal queueing behavior.
 */
void blk_queue_bypass_end(struct request_queue *q)
{
	spin_lock_irq(q->queue_lock);
	if (!--q->bypass_depth)
		queue_flag_clear(QUEUE_FLAG_BYPASS, q);
	WARN_ON_ONCE(q->bypass_depth < 0);
	spin_unlock_irq(q->queue_lock);
}
EXPORT_SYMBOL_GPL(blk_queue_bypass_end);

void blk_set_queue_dying(struct request_queue *q)
{
	spin_lock_irq(q->queue_lock);
	queue_flag_set(QUEUE_FLAG_DYING, q);
	spin_unlock_irq(q->queue_lock);

	if (q->mq_ops)
		blk_mq_wake_waiters(q);
	else {
		struct request_list *rl;

		blk_queue_for_each_rl(rl, q) {
			if (rl->rq_pool) {
				wake_up_all(&rl->wait[BLK_RW_SYNC]);
				wake_up_all(&rl->wait[BLK_RW_ASYNC]);
			}
		}
	}
}
EXPORT_SYMBOL_GPL(blk_set_queue_dying);

/**
 * blk_cleanup_queue - shutdown a request queue
 * @q: request queue to shutdown
 *
 * Mark @q DYING, drain all pending requests, mark @q DEAD, destroy and
 * put it.  All future requests will be failed immediately with -ENODEV.
 */
void blk_cleanup_queue(struct request_queue *q)
{
	spinlock_t *lock = q->queue_lock;

	/* mark @q DYING, no new request or merges will be allowed afterwards */
	mutex_lock(&q->sysfs_lock);
	blk_set_queue_dying(q);
	spin_lock_irq(lock);

	/*
	 * A dying queue is permanently in bypass mode till released.  Note
	 * that, unlike blk_queue_bypass_start(), we aren't performing
	 * synchronize_rcu() after entering bypass mode to avoid the delay
	 * as some drivers create and destroy a lot of queues while
	 * probing.  This is still safe because blk_release_queue() will be
	 * called only after the queue refcnt drops to zero and nothing,
	 * RCU or not, would be traversing the queue by then.
	 */
	q->bypass_depth++;
	queue_flag_set(QUEUE_FLAG_BYPASS, q);

	queue_flag_set(QUEUE_FLAG_NOMERGES, q);
	queue_flag_set(QUEUE_FLAG_NOXMERGES, q);
	queue_flag_set(QUEUE_FLAG_DYING, q);
	spin_unlock_irq(lock);
	mutex_unlock(&q->sysfs_lock);

	/*
	 * Drain all requests queued before DYING marking. Set DEAD flag to
	 * prevent that q->request_fn() gets invoked after draining finished.
	 */
	blk_freeze_queue(q);
	spin_lock_irq(lock);
	if (!q->mq_ops)
		__blk_drain_queue(q, true);
	queue_flag_set(QUEUE_FLAG_DEAD, q);
	spin_unlock_irq(lock);

	/* for synchronous bio-based driver finish in-flight integrity i/o */
	blk_flush_integrity();

	/* @q won't process any more request, flush async actions */
	del_timer_sync(&q->backing_dev_info.laptop_mode_wb_timer);
	blk_sync_queue(q);

	if (q->mq_ops)
		blk_mq_free_queue(q);
	percpu_ref_exit(&q->q_usage_counter);

	spin_lock_irq(lock);
	if (q->queue_lock != &q->__queue_lock)
		q->queue_lock = &q->__queue_lock;
	spin_unlock_irq(lock);

	bdi_unregister(&q->backing_dev_info);

	/* @q is and will stay empty, shutdown and put */
	blk_put_queue(q);
}
EXPORT_SYMBOL(blk_cleanup_queue);

/* Allocate memory local to the request queue */
static void *alloc_request_struct(gfp_t gfp_mask, void *data)
{
	int nid = (int)(long)data;
	return kmem_cache_alloc_node(request_cachep, gfp_mask, nid);
}

static void free_request_struct(void *element, void *unused)
{
	kmem_cache_free(request_cachep, element);
}

int blk_init_rl(struct request_list *rl, struct request_queue *q,
		gfp_t gfp_mask)
{
	if (unlikely(rl->rq_pool))
		return 0;

	rl->q = q;
	rl->count[BLK_RW_SYNC] = rl->count[BLK_RW_ASYNC] = 0;
	rl->starved[BLK_RW_SYNC] = rl->starved[BLK_RW_ASYNC] = 0;
	init_waitqueue_head(&rl->wait[BLK_RW_SYNC]);
	init_waitqueue_head(&rl->wait[BLK_RW_ASYNC]);

	rl->rq_pool = mempool_create_node(BLKDEV_MIN_RQ, alloc_request_struct,
					  free_request_struct,
					  (void *)(long)q->node, gfp_mask,
					  q->node);
	if (!rl->rq_pool)
		return -ENOMEM;

	return 0;
}

void blk_exit_rl(struct request_list *rl)
{
	if (rl->rq_pool)
		mempool_destroy(rl->rq_pool);
}

struct request_queue *blk_alloc_queue(gfp_t gfp_mask)
{
	return blk_alloc_queue_node(gfp_mask, NUMA_NO_NODE);
}
EXPORT_SYMBOL(blk_alloc_queue);

int blk_queue_enter(struct request_queue *q, gfp_t gfp)
{
	while (true) {
		int ret;

		if (percpu_ref_tryget_live(&q->q_usage_counter))
			return 0;

		if (!gfpflags_allow_blocking(gfp))
			return -EBUSY;

		ret = wait_event_interruptible(q->mq_freeze_wq,
				!atomic_read(&q->mq_freeze_depth) ||
				blk_queue_dying(q));
		if (blk_queue_dying(q))
			return -ENODEV;
		if (ret)
			return ret;
	}
}

void blk_queue_exit(struct request_queue *q)
{
	percpu_ref_put(&q->q_usage_counter);
}

static void blk_queue_usage_counter_release(struct percpu_ref *ref)
{
	struct request_queue *q =
		container_of(ref, struct request_queue, q_usage_counter);

	wake_up_all(&q->mq_freeze_wq);
}

struct request_queue *blk_alloc_queue_node(gfp_t gfp_mask, int node_id)
{
	struct request_queue *q;
	int err;

	q = kmem_cache_alloc_node(blk_requestq_cachep,
				gfp_mask | __GFP_ZERO, node_id);
	if (!q)
		return NULL;

	q->id = ida_simple_get(&blk_queue_ida, 0, 0, gfp_mask);
	if (q->id < 0)
		goto fail_q;

	q->bio_split = bioset_create(BIO_POOL_SIZE, 0);
	if (!q->bio_split)
		goto fail_id;

	q->backing_dev_info.ra_pages =
			(VM_MAX_READAHEAD * 1024) / PAGE_CACHE_SIZE;
	q->backing_dev_info.capabilities = BDI_CAP_CGROUP_WRITEBACK;
	q->backing_dev_info.name = "block";
	q->node = node_id;

	err = bdi_init(&q->backing_dev_info);
	if (err)
		goto fail_split;

	setup_timer(&q->backing_dev_info.laptop_mode_wb_timer,
		    laptop_mode_timer_fn, (unsigned long) q);
	setup_timer(&q->timeout, blk_rq_timed_out_timer, (unsigned long) q);
	INIT_LIST_HEAD(&q->queue_head);
	INIT_LIST_HEAD(&q->timeout_list);
	INIT_LIST_HEAD(&q->icq_list);
#ifdef CONFIG_BLK_CGROUP
	INIT_LIST_HEAD(&q->blkg_list);
#endif
	INIT_DELAYED_WORK(&q->delay_work, blk_delay_work);

	kobject_init(&q->kobj, &blk_queue_ktype);

	mutex_init(&q->sysfs_lock);
	spin_lock_init(&q->__queue_lock);

	/*
	 * By default initialize queue_lock to internal lock and driver can
	 * override it later if need be.
	 */
	q->queue_lock = &q->__queue_lock;

	/*
	 * A queue starts its life with bypass turned on to avoid
	 * unnecessary bypass on/off overhead and nasty surprises during
	 * init.  The initial bypass will be finished when the queue is
	 * registered by blk_register_queue().
	 */
	q->bypass_depth = 1;
	__set_bit(QUEUE_FLAG_BYPASS, &q->queue_flags);

	init_waitqueue_head(&q->mq_freeze_wq);

	/*
	 * Init percpu_ref in atomic mode so that it's faster to shutdown.
	 * See blk_register_queue() for details.
	 */
	if (percpu_ref_init(&q->q_usage_counter,
				blk_queue_usage_counter_release,
				PERCPU_REF_INIT_ATOMIC, GFP_KERNEL))
		goto fail_bdi;

	if (blkcg_init_queue(q))
		goto fail_ref;

	return q;

fail_ref:
	percpu_ref_exit(&q->q_usage_counter);
fail_bdi:
	bdi_destroy(&q->backing_dev_info);
fail_split:
	bioset_free(q->bio_split);
fail_id:
	ida_simple_remove(&blk_queue_ida, q->id);
fail_q:
	kmem_cache_free(blk_requestq_cachep, q);
	return NULL;
}
EXPORT_SYMBOL(blk_alloc_queue_node);

/**
 * blk_init_queue  - prepare a request queue for use with a block device
 * @rfn:  The function to be called to process requests that have been
 *        placed on the queue.
 * @lock: Request queue spin lock
 *
 * Description:
 *    If a block device wishes to use the standard request handling procedures,
 *    which sorts requests and coalesces adjacent requests, then it must
 *    call blk_init_queue().  The function @rfn will be called when there
 *    are requests on the queue that need to be processed.  If the device
 *    supports plugging, then @rfn may not be called immediately when requests
 *    are available on the queue, but may be called at some time later instead.
 *    Plugged queues are generally unplugged when a buffer belonging to one
 *    of the requests on the queue is needed, or due to memory pressure.
 *
 *    @rfn is not required, or even expected, to remove all requests off the
 *    queue, but only as many as it can handle at a time.  If it does leave
 *    requests on the queue, it is responsible for arranging that the requests
 *    get dealt with eventually.
 *
 *    The queue spin lock must be held while manipulating the requests on the
 *    request queue; this lock will be taken also from interrupt context, so irq
 *    disabling is needed for it.
 *
 *    Function returns a pointer to the initialized request queue, or %NULL if
 *    it didn't succeed.
 *
 * Note:
 *    blk_init_queue() must be paired with a blk_cleanup_queue() call
 *    when the block device is deactivated (such as at module unload).
 **/

struct request_queue *blk_init_queue(request_fn_proc *rfn, spinlock_t *lock)
{
	return blk_init_queue_node(rfn, lock, NUMA_NO_NODE);
}
EXPORT_SYMBOL(blk_init_queue);

struct request_queue *
blk_init_queue_node(request_fn_proc *rfn, spinlock_t *lock, int node_id)
{
	struct request_queue *uninit_q, *q;

	uninit_q = blk_alloc_queue_node(GFP_KERNEL, node_id);
	if (!uninit_q)
		return NULL;

	q = blk_init_allocated_queue(uninit_q, rfn, lock);
	if (!q)
		blk_cleanup_queue(uninit_q);

	return q;
}
EXPORT_SYMBOL(blk_init_queue_node);

static blk_qc_t blk_queue_bio(struct request_queue *q, struct bio *bio);

struct request_queue *
blk_init_allocated_queue(struct request_queue *q, request_fn_proc *rfn,
			 spinlock_t *lock)
{
	if (!q)
		return NULL;

	q->fq = blk_alloc_flush_queue(q, NUMA_NO_NODE, 0);
	if (!q->fq)
		return NULL;

	if (blk_init_rl(&q->root_rl, q, GFP_KERNEL))
		goto fail;

	q->request_fn		= rfn;
	q->prep_rq_fn		= NULL;
	q->unprep_rq_fn		= NULL;
	q->queue_flags		|= QUEUE_FLAG_DEFAULT;

	/* Override internal queue lock with supplied lock pointer */
	if (lock)
		q->queue_lock		= lock;

	/*
	 * This also sets hw/phys segments, boundary and size
	 */
	blk_queue_make_request(q, blk_queue_bio);

	q->sg_reserved_size = INT_MAX;

	/* Protect q->elevator from elevator_change */
	mutex_lock(&q->sysfs_lock);

	/* init elevator */
	if (elevator_init(q, NULL)) {
		mutex_unlock(&q->sysfs_lock);
		goto fail;
	}

	mutex_unlock(&q->sysfs_lock);

	return q;

fail:
	blk_free_flush_queue(q->fq);
	return NULL;
}
EXPORT_SYMBOL(blk_init_allocated_queue);

bool blk_get_queue(struct request_queue *q)
{
	if (likely(!blk_queue_dying(q))) {
		__blk_get_queue(q);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(blk_get_queue);

static inline void blk_free_request(struct request_list *rl, struct request *rq)
{
	if (rq->cmd_flags & REQ_ELVPRIV) {
		elv_put_request(rl->q, rq);
		if (rq->elv.icq)
			put_io_context(rq->elv.icq->ioc);
	}

	mempool_free(rq, rl->rq_pool);
}

/*
 * ioc_batching returns true if the ioc is a valid batching request and
 * should be given priority access to a request.
 */
static inline int ioc_batching(struct request_queue *q, struct io_context *ioc)
{
	if (!ioc)
		return 0;

	/*
	 * Make sure the process is able to allocate at least 1 request
	 * even if the batch times out, otherwise we could theoretically
	 * lose wakeups.
	 */
	return ioc->nr_batch_requests == q->nr_batching ||
		(ioc->nr_batch_requests > 0
		&& time_before(jiffies, ioc->last_waited + BLK_BATCH_TIME));
}

/*
 * ioc_set_batching sets ioc to be a new "batcher" if it is not one. This
 * will cause the process to be a "batcher" on all queues in the system. This
 * is the behaviour we want though - once it gets a wakeup it should be given
 * a nice run.
 */
static void ioc_set_batching(struct request_queue *q, struct io_context *ioc)
{
	if (!ioc || ioc_batching(q, ioc))
		return;

	ioc->nr_batch_requests = q->nr_batching;
	ioc->last_waited = jiffies;
}

static void __freed_request(struct request_list *rl, int sync)
{
	struct request_queue *q = rl->q;

	if (rl->count[sync] < queue_congestion_off_threshold(q))
		blk_clear_congested(rl, sync);

	if (rl->count[sync] + 1 <= q->nr_requests) {
		if (waitqueue_active(&rl->wait[sync]))
			wake_up(&rl->wait[sync]);

		blk_clear_rl_full(rl, sync);
	}
}

/*
 * A request has just been released.  Account for it, update the full and
 * congestion status, wake up any waiters.   Called under q->queue_lock.
 */
static void freed_request(struct request_list *rl, unsigned int flags)
{
	struct request_queue *q = rl->q;
	int sync = rw_is_sync(flags);

	q->nr_rqs[sync]--;
	rl->count[sync]--;
	if (flags & REQ_ELVPRIV)
		q->nr_rqs_elvpriv--;

	__freed_request(rl, sync);

	if (unlikely(rl->starved[sync ^ 1]))
		__freed_request(rl, sync ^ 1);
}

int blk_update_nr_requests(struct request_queue *q, unsigned int nr)
{
	struct request_list *rl;
	int on_thresh, off_thresh;

	spin_lock_irq(q->queue_lock);
	q->nr_requests = nr;
	blk_queue_congestion_threshold(q);
	on_thresh = queue_congestion_on_threshold(q);
	off_thresh = queue_congestion_off_threshold(q);

	blk_queue_for_each_rl(rl, q) {
		if (rl->count[BLK_RW_SYNC] >= on_thresh)
			blk_set_congested(rl, BLK_RW_SYNC);
		else if (rl->count[BLK_RW_SYNC] < off_thresh)
			blk_clear_congested(rl, BLK_RW_SYNC);

		if (rl->count[BLK_RW_ASYNC] >= on_thresh)
			blk_set_congested(rl, BLK_RW_ASYNC);
		else if (rl->count[BLK_RW_ASYNC] < off_thresh)
			blk_clear_congested(rl, BLK_RW_ASYNC);

		if (rl->count[BLK_RW_SYNC] >= q->nr_requests) {
			blk_set_rl_full(rl, BLK_RW_SYNC);
		} else {
			blk_clear_rl_full(rl, BLK_RW_SYNC);
			wake_up(&rl->wait[BLK_RW_SYNC]);
		}

		if (rl->count[BLK_RW_ASYNC] >= q->nr_requests) {
			blk_set_rl_full(rl, BLK_RW_ASYNC);
		} else {
			blk_clear_rl_full(rl, BLK_RW_ASYNC);
			wake_up(&rl->wait[BLK_RW_ASYNC]);
		}
	}

	spin_unlock_irq(q->queue_lock);
	return 0;
}

/*
 * Determine if elevator data should be initialized when allocating the
 * request associated with @bio.
 */
static bool blk_rq_should_init_elevator(struct bio *bio)
{
	if (!bio)
		return true;

	/*
	 * Flush requests do not use the elevator so skip initialization.
	 * This allows a request to share the flush and elevator data.
	 */
	if (bio->bi_rw & (REQ_FLUSH | REQ_FUA))
		return false;

	return true;
}

/**
 * rq_ioc - determine io_context for request allocation
 * @bio: request being allocated is for this bio (can be %NULL)
 *
 * Determine io_context to use for request allocation for @bio.  May return
 * %NULL if %current->io_context doesn't exist.
 */
static struct io_context *rq_ioc(struct bio *bio)
{
#ifdef CONFIG_BLK_CGROUP
	if (bio && bio->bi_ioc)
		return bio->bi_ioc;
#endif
	return current->io_context;
}

/**
 * __get_request - get a free request
 * @rl: request list to allocate from
 * @rw_flags: RW and SYNC flags
 * @bio: bio to allocate request for (can be %NULL)
 * @gfp_mask: allocation mask
 *
 * Get a free request from @q.  This function may fail under memory
 * pressure or if @q is dead.
 *
 * Must be called with @q->queue_lock held and,
 * Returns ERR_PTR on failure, with @q->queue_lock held.
 * Returns request pointer on success, with @q->queue_lock *not held*.
 */
static struct request *__get_request(struct request_list *rl, int rw_flags,
				     struct bio *bio, gfp_t gfp_mask)
{
	struct request_queue *q = rl->q;
	struct request *rq;
	struct elevator_type *et = q->elevator->type;
	struct io_context *ioc = rq_ioc(bio);
	struct io_cq *icq = NULL;
	const bool is_sync = rw_is_sync(rw_flags) != 0;
	int may_queue;

	if (unlikely(blk_queue_dying(q)))
		return ERR_PTR(-ENODEV);

	may_queue = elv_may_queue(q, rw_flags);
	if (may_queue == ELV_MQUEUE_NO)
		goto rq_starved;

	if (rl->count[is_sync]+1 >= queue_congestion_on_threshold(q)) {
		if (rl->count[is_sync]+1 >= q->nr_requests) {
			/*
			 * The queue will fill after this allocation, so set
			 * it as full, and mark this process as "batching".
			 * This process will be allowed to complete a batch of
			 * requests, others will be blocked.
			 */
			if (!blk_rl_full(rl, is_sync)) {
				ioc_set_batching(q, ioc);
				blk_set_rl_full(rl, is_sync);
			} else {
				if (may_queue != ELV_MQUEUE_MUST
						&& !ioc_batching(q, ioc)) {
					/*
					 * The queue is full and the allocating
					 * process is not a "batcher", and not
					 * exempted by the IO scheduler
					 */
					return ERR_PTR(-ENOMEM);
				}
			}
		}
		blk_set_congested(rl, is_sync);
	}

	/*
	 * Only allow batching queuers to allocate up to 50% over the defined
	 * limit of requests, otherwise we could have thousands of requests
	 * allocated with any setting of ->nr_requests
	 */
	if (rl->count[is_sync] >= (3 * q->nr_requests / 2))
		return ERR_PTR(-ENOMEM);

	q->nr_rqs[is_sync]++;
	rl->count[is_sync]++;
	rl->starved[is_sync] = 0;

	/*
	 * Decide whether the new request will be managed by elevator.  If
	 * so, mark @rw_flags and increment elvpriv.  Non-zero elvpriv will
	 * prevent the current elevator from being destroyed until the new
	 * request is freed.  This guarantees icq's won't be destroyed and
	 * makes creating new ones safe.
	 *
	 * Also, lookup icq while holding queue_lock.  If it doesn't exist,
	 * it will be created after releasing queue_lock.
	 */
	if (blk_rq_should_init_elevator(bio) && !blk_queue_bypass(q)) {
		rw_flags |= REQ_ELVPRIV;
		q->nr_rqs_elvpriv++;
		if (et->icq_cache && ioc)
			icq = ioc_lookup_icq(ioc, q);
	}

	if (blk_queue_io_stat(q))
		rw_flags |= REQ_IO_STAT;
	spin_unlock_irq(q->queue_lock);

	/* allocate and init request */
	rq = mempool_alloc(rl->rq_pool, gfp_mask);
	if (!rq)
		goto fail_alloc;

	blk_rq_init(q, rq);
	blk_rq_set_rl(rq, rl);
	rq->cmd_flags = rw_flags | REQ_ALLOCED;

	/* init elvpriv */
	if (rw_flags & REQ_ELVPRIV) {
		if (unlikely(et->icq_cache && !icq)) {
			if (ioc)
				icq = ioc_create_icq(ioc, q, gfp_mask);
			if (!icq)
				goto fail_elvpriv;
		}

		rq->elv.icq = icq;
		if (unlikely(elv_set_request(q, rq, bio, gfp_mask)))
			goto fail_elvpriv;

		/* @rq->elv.icq holds io_context until @rq is freed */
		if (icq)
			get_io_context(icq->ioc);
	}
out:
	/*
	 * ioc may be NULL here, and ioc_batching will be false. That's
	 * OK, if the queue is under the request limit then requests need
	 * not count toward the nr_batch_requests limit. There will always
	 * be some limit enforced by BLK_BATCH_TIME.
	 */
	if (ioc_batching(q, ioc))
		ioc->nr_batch_requests--;

	trace_block_getrq(q, bio, rw_flags & 1);
	return rq;

fail_elvpriv:
	/*
	 * elvpriv init failed.  ioc, icq and elvpriv aren't mempool backed
	 * and may fail indefinitely under memory pressure and thus
	 * shouldn't stall IO.  Treat this request as !elvpriv.  This will
	 * disturb iosched and blkcg but weird is bettern than dead.
	 */
	printk_ratelimited(KERN_WARNING "%s: dev %s: request aux data allocation failed, iosched may be disturbed\n",
			   __func__, dev_name(q->backing_dev_info.dev));

	rq->cmd_flags &= ~REQ_ELVPRIV;
	rq->elv.icq = NULL;

	spin_lock_irq(q->queue_lock);
	q->nr_rqs_elvpriv--;
	spin_unlock_irq(q->queue_lock);
	goto out;

fail_alloc:
	/*
	 * Allocation failed presumably due to memory. Undo anything we
	 * might have messed up.
	 *
	 * Allocating task should really be put onto the front of the wait
	 * queue, but this is pretty rare.
	 */
	spin_lock_irq(q->queue_lock);
	freed_request(rl, rw_flags);

	/*
	 * in the very unlikely event that allocation failed and no
	 * requests for this direction was pending, mark us starved so that
	 * freeing of a request in the other direction will notice
	 * us. another possible fix would be to split the rq mempool into
	 * READ and WRITE
	 */
rq_starved:
	if (unlikely(rl->count[is_sync] == 0))
		rl->starved[is_sync] = 1;
	return ERR_PTR(-ENOMEM);
}

/**
 * get_request - get a free request
 * @q: request_queue to allocate request from
 * @rw_flags: RW and SYNC flags
 * @bio: bio to allocate request for (can be %NULL)
 * @gfp_mask: allocation mask
 *
 * Get a free request from @q.  If %__GFP_DIRECT_RECLAIM is set in @gfp_mask,
 * this function keeps retrying under memory pressure and fails iff @q is dead.
 *
 * Must be called with @q->queue_lock held and,
 * Returns ERR_PTR on failure, with @q->queue_lock held.
 * Returns request pointer on success, with @q->queue_lock *not held*.
 */
static struct request *get_request(struct request_queue *q, int rw_flags,
				   struct bio *bio, gfp_t gfp_mask)
{
	const bool is_sync = rw_is_sync(rw_flags) != 0;
	DEFINE_WAIT(wait);
	struct request_list *rl;
	struct request *rq;

	rl = blk_get_rl(q, bio);	/* transferred to @rq on success */
retry:
	rq = __get_request(rl, rw_flags, bio, gfp_mask);
	if (!IS_ERR(rq))
		return rq;

	if (!gfpflags_allow_blocking(gfp_mask) || unlikely(blk_queue_dying(q))) {
		blk_put_rl(rl);
		return rq;
	}

	/* wait on @rl and retry */
	prepare_to_wait_exclusive(&rl->wait[is_sync], &wait,
				  TASK_UNINTERRUPTIBLE);

	trace_block_sleeprq(q, bio, rw_flags & 1);

	spin_unlock_irq(q->queue_lock);
	io_schedule();

	/*
	 * After sleeping, we become a "batching" process and will be able
	 * to allocate at least one request, and up to a big batch of them
	 * for a small period time.  See ioc_batching, ioc_set_batching
	 */
	ioc_set_batching(q, current->io_context);

	spin_lock_irq(q->queue_lock);
	finish_wait(&rl->wait[is_sync], &wait);

	goto retry;
}

static struct request *blk_old_get_request(struct request_queue *q, int rw,
		gfp_t gfp_mask)
{
	struct request *rq;

	BUG_ON(rw != READ && rw != WRITE);

	/* create ioc upfront */
	create_io_context(gfp_mask, q->node);

	spin_lock_irq(q->queue_lock);
	rq = get_request(q, rw, NULL, gfp_mask);
	if (IS_ERR(rq))
		spin_unlock_irq(q->queue_lock);
	/* q->queue_lock is unlocked at this point */

	return rq;
}

struct request *blk_get_request(struct request_queue *q, int rw, gfp_t gfp_mask)
{
	if (q->mq_ops)
		return blk_mq_alloc_request(q, rw, gfp_mask, false);
	else
		return blk_old_get_request(q, rw, gfp_mask);
}
EXPORT_SYMBOL(blk_get_request);

/**
 * blk_make_request - given a bio, allocate a corresponding struct request.
 * @q: target request queue
 * @bio:  The bio describing the memory mappings that will be submitted for IO.
 *        It may be a chained-bio properly constructed by block/bio layer.
 * @gfp_mask: gfp flags to be used for memory allocation
 *
 * blk_make_request is the parallel of generic_make_request for BLOCK_PC
 * type commands. Where the struct request needs to be farther initialized by
 * the caller. It is passed a &struct bio, which describes the memory info of
 * the I/O transfer.
 *
 * The caller of blk_make_request must make sure that bi_io_vec
 * are set to describe the memory buffers. That bio_data_dir() will return
 * the needed direction of the request. (And all bio's in the passed bio-chain
 * are properly set accordingly)
 *
 * If called under none-sleepable conditions, mapped bio buffers must not
 * need bouncing, by calling the appropriate masked or flagged allocator,
 * suitable for the target device. Otherwise the call to blk_queue_bounce will
 * BUG.
 *
 * WARNING: When allocating/cloning a bio-chain, careful consideration should be
 * given to how you allocate bios. In particular, you cannot use
 * __GFP_DIRECT_RECLAIM for anything but the first bio in the chain. Otherwise
 * you risk waiting for IO completion of a bio that hasn't been submitted yet,
 * thus resulting in a deadlock. Alternatively bios should be allocated using
 * bio_kmalloc() instead of bio_alloc(), as that avoids the mempool deadlock.
 * If possible a big IO should be split into smaller parts when allocation
 * fails. Partial allocation should not be an error, or you risk a live-lock.
 */
struct request *blk_make_request(struct request_queue *q, struct bio *bio,
				 gfp_t gfp_mask)
{
	struct request *rq = blk_get_request(q, bio_data_dir(bio), gfp_mask);

	if (IS_ERR(rq))
		return rq;

	blk_rq_set_block_pc(rq);

	for_each_bio(bio) {
		struct bio *bounce_bio = bio;
		int ret;

		blk_queue_bounce(q, &bounce_bio);
		ret = blk_rq_append_bio(q, rq, bounce_bio);
		if (unlikely(ret)) {
			blk_put_request(rq);
			return ERR_PTR(ret);
		}
	}

	return rq;
}
EXPORT_SYMBOL(blk_make_request);

/**
 * blk_rq_set_block_pc - initialize a request to type BLOCK_PC
 * @rq:		request to be initialized
 *
 */
void blk_rq_set_block_pc(struct request *rq)
{
	rq->cmd_type = REQ_TYPE_BLOCK_PC;
	rq->__data_len = 0;
	rq->__sector = (sector_t) -1;
	rq->bio = rq->biotail = NULL;
	memset(rq->__cmd, 0, sizeof(rq->__cmd));
}
EXPORT_SYMBOL(blk_rq_set_block_pc);

/**
 * blk_requeue_request - put a request back on queue
 * @q:		request queue where request should be inserted
 * @rq:		request to be inserted
 *
 * Description:
 *    Drivers often keep queueing requests until the hardware cannot accept
 *    more, when that condition happens we need to put the request back
 *    on the queue. Must be called with queue lock held.
 */
void blk_requeue_request(struct request_queue *q, struct request *rq)
{
	blk_delete_timer(rq);
	blk_clear_rq_complete(rq);
	trace_block_rq_requeue(q, rq);

	if (rq->cmd_flags & REQ_QUEUED)
		blk_queue_end_tag(q, rq);

	BUG_ON(blk_queued_rq(rq));

	elv_requeue_request(q, rq);
}
EXPORT_SYMBOL(blk_requeue_request);

static void add_acct_request(struct request_queue *q, struct request *rq,
			     int where)
{
	blk_account_io_start(rq, true);
	__elv_add_request(q, rq, where);
}

static void part_round_stats_single(int cpu, struct hd_struct *part,
				    unsigned long now)
{
	int inflight;

	if (now == part->stamp)
		return;

	inflight = part_in_flight(part);
	if (inflight) {
		__part_stat_add(cpu, part, time_in_queue,
				inflight * (now - part->stamp));
		__part_stat_add(cpu, part, io_ticks, (now - part->stamp));
	}
	part->stamp = now;
}

/**
 * part_round_stats() - Round off the performance stats on a struct disk_stats.
 * @cpu: cpu number for stats access
 * @part: target partition
 *
 * The average IO queue length and utilisation statistics are maintained
 * by observing the current state of the queue length and the amount of
 * time it has been in this state for.
 *
 * Normally, that accounting is done on IO completion, but that can result
 * in more than a second's worth of IO being accounted for within any one
 * second, leading to >100% utilisation.  To deal with that, we call this
 * function to do a round-off before returning the results when reading
 * /proc/diskstats.  This accounts immediately for all queue usage up to
 * the current jiffies and restarts the counters again.
 */
void part_round_stats(int cpu, struct hd_struct *part)
{
	unsigned long now = jiffies;

	if (part->partno)
		part_round_stats_single(cpu, &part_to_disk(part)->part0, now);
	part_round_stats_single(cpu, part, now);
}
EXPORT_SYMBOL_GPL(part_round_stats);

#ifdef CONFIG_PM
static void blk_pm_put_request(struct request *rq)
{
	if (rq->q->dev && !(rq->cmd_flags & REQ_PM) && !--rq->q->nr_pending)
		pm_runtime_mark_last_busy(rq->q->dev);
}
#else
static inline void blk_pm_put_request(struct request *rq) {}
#endif

/*
 * queue lock must be held
 */
void __blk_put_request(struct request_queue *q, struct request *req)
{
	if (unlikely(!q))
		return;

	if (q->mq_ops) {
		blk_mq_free_request(req);
		return;
	}

	blk_pm_put_request(req);

	elv_completed_request(q, req);

	/* this is a bio leak */
	WARN_ON(req->bio != NULL);

	/*
	 * Request may not have originated from ll_rw_blk. if not,
	 * it didn't come out of our reserved rq pools
	 */
	if (req->cmd_flags & REQ_ALLOCED) {
		unsigned int flags = req->cmd_flags;
		struct request_list *rl = blk_rq_rl(req);

		BUG_ON(!list_empty(&req->queuelist));
		BUG_ON(ELV_ON_HASH(req));

		blk_free_request(rl, req);
		freed_request(rl, flags);
		blk_put_rl(rl);
	}
}
EXPORT_SYMBOL_GPL(__blk_put_request);

void blk_put_request(struct request *req)
{
	struct request_queue *q = req->q;

	if (q->mq_ops)
		blk_mq_free_request(req);
	else {
		unsigned long flags;

		spin_lock_irqsave(q->queue_lock, flags);
		__blk_put_request(q, req);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}
EXPORT_SYMBOL(blk_put_request);

/**
 * blk_add_request_payload - add a payload to a request
 * @rq: request to update
 * @page: page backing the payload
 * @len: length of the payload.
 *
 * This allows to later add a payload to an already submitted request by
 * a block driver.  The driver needs to take care of freeing the payload
 * itself.
 *
 * Note that this is a quite horrible hack and nothing but handling of
 * discard requests should ever use it.
 */
void blk_add_request_payload(struct request *rq, struct page *page,
		unsigned int len)
{
	struct bio *bio = rq->bio;

	bio->bi_io_vec->bv_page = page;
	bio->bi_io_vec->bv_offset = 0;
	bio->bi_io_vec->bv_len = len;

	bio->bi_iter.bi_size = len;
	bio->bi_vcnt = 1;
	bio->bi_phys_segments = 1;

	rq->__data_len = rq->resid_len = len;
	rq->nr_phys_segments = 1;
}
EXPORT_SYMBOL_GPL(blk_add_request_payload);

bool bio_attempt_back_merge(struct request_queue *q, struct request *req,
			    struct bio *bio)
{
	const int ff = bio->bi_rw & REQ_FAILFAST_MASK;

	if (!ll_back_merge_fn(q, req, bio))
		return false;

	trace_block_bio_backmerge(q, req, bio);

	if ((req->cmd_flags & REQ_FAILFAST_MASK) != ff)
		blk_rq_set_mixed_merge(req);

	req->biotail->bi_next = bio;
	req->biotail = bio;
	req->__data_len += bio->bi_iter.bi_size;
	req->ioprio = ioprio_best(req->ioprio, bio_prio(bio));

	blk_account_io_start(req, false);
	return true;
}

bool bio_attempt_front_merge(struct request_queue *q, struct request *req,
			     struct bio *bio)
{
	const int ff = bio->bi_rw & REQ_FAILFAST_MASK;

	if (!ll_front_merge_fn(q, req, bio))
		return false;

	trace_block_bio_frontmerge(q, req, bio);

	if ((req->cmd_flags & REQ_FAILFAST_MASK) != ff)
		blk_rq_set_mixed_merge(req);

	bio->bi_next = req->bio;
	req->bio = bio;

	req->__sector = bio->bi_iter.bi_sector;
	req->__data_len += bio->bi_iter.bi_size;
	req->ioprio = ioprio_best(req->ioprio, bio_prio(bio));

	blk_account_io_start(req, false);
	return true;
}

/**
 * blk_attempt_plug_merge - try to merge with %current's plugged list
 * @q: request_queue new bio is being queued at
 * @bio: new bio being queued
 * @request_count: out parameter for number of traversed plugged requests
 * @same_queue_rq: pointer to &struct request that gets filled in when
 * another request associated with @q is found on the plug list
 * (optional, may be %NULL)
 *
 * Determine whether @bio being queued on @q can be merged with a request
 * on %current's plugged list.  Returns %true if merge was successful,
 * otherwise %false.
 *
 * Plugging coalesces IOs from the same issuer for the same purpose without
 * going through @q->queue_lock.  As such it's more of an issuing mechanism
 * than scheduling, and the request, while may have elvpriv data, is not
 * added on the elevator at this point.  In addition, we don't have
 * reliable access to the elevator outside queue lock.  Only check basic
 * merging parameters without querying the elevator.
 *
 * Caller must ensure !blk_queue_nomerges(q) beforehand.
 */
bool blk_attempt_plug_merge(struct request_queue *q, struct bio *bio,
			    unsigned int *request_count,
			    struct request **same_queue_rq)
{
	struct blk_plug *plug;
	struct request *rq;
	bool ret = false;
	struct list_head *plug_list;

	plug = current->plug;
	if (!plug)
		goto out;
	*request_count = 0;

	if (q->mq_ops)
		plug_list = &plug->mq_list;
	else
		plug_list = &plug->list;

	list_for_each_entry_reverse(rq, plug_list, queuelist) {
		int el_ret;

		if (rq->q == q) {
			(*request_count)++;
			/*
			 * Only blk-mq multiple hardware queues case checks the
			 * rq in the same queue, there should be only one such
			 * rq in a queue
			 **/
			if (same_queue_rq)
				*same_queue_rq = rq;
		}

		if (rq->q != q || !blk_rq_merge_ok(rq, bio))
			continue;

		el_ret = blk_try_merge(rq, bio);
		if (el_ret == ELEVATOR_BACK_MERGE) {
			ret = bio_attempt_back_merge(q, rq, bio);
			if (ret)
				break;
		} else if (el_ret == ELEVATOR_FRONT_MERGE) {
			ret = bio_attempt_front_merge(q, rq, bio);
			if (ret)
				break;
		}
	}
out:
	return ret;
}

unsigned int blk_plug_queued_count(struct request_queue *q)
{
	struct blk_plug *plug;
	struct request *rq;
	struct list_head *plug_list;
	unsigned int ret = 0;

	plug = current->plug;
	if (!plug)
		goto out;

	if (q->mq_ops)
		plug_list = &plug->mq_list;
	else
		plug_list = &plug->list;

	list_for_each_entry(rq, plug_list, queuelist) {
		if (rq->q == q)
			ret++;
	}
out:
	return ret;
}

void init_request_from_bio(struct request *req, struct bio *bio)
{
	req->cmd_type = REQ_TYPE_FS;

	req->cmd_flags |= bio->bi_rw & REQ_COMMON_MASK;
	if (bio->bi_rw & REQ_RAHEAD)
		req->cmd_flags |= REQ_FAILFAST_MASK;

	req->errors = 0;
	req->__sector = bio->bi_iter.bi_sector;
	req->ioprio = bio_prio(bio);
	blk_rq_bio_prep(req->q, req, bio);
}

static blk_qc_t blk_queue_bio(struct request_queue *q, struct bio *bio)
{
	const bool sync = !!(bio->bi_rw & REQ_SYNC);
	struct blk_plug *plug;
	int el_ret, rw_flags, where = ELEVATOR_INSERT_SORT;
	struct request *req;
	unsigned int request_count = 0;

	/*
	 * low level driver can indicate that it wants pages above a
	 * certain limit bounced to low memory (ie for highmem, or even
	 * ISA dma in theory)
	 */
	blk_queue_bounce(q, &bio);

	blk_queue_split(q, &bio, q->bio_split);

	if (bio_integrity_enabled(bio) && bio_integrity_prep(bio)) {
		bio->bi_error = -EIO;
		bio_endio(bio);
		return BLK_QC_T_NONE;
	}

	if (bio->bi_rw & (REQ_FLUSH | REQ_FUA)) {
		spin_lock_irq(q->queue_lock);
		where = ELEVATOR_INSERT_FLUSH;
		goto get_rq;
	}

	/*
	 * Check if we can merge with the plugged list before grabbing
	 * any locks.
	 */
	if (!blk_queue_nomerges(q)) {
		if (blk_attempt_plug_merge(q, bio, &request_count, NULL))
			return BLK_QC_T_NONE;
	} else
		request_count = blk_plug_queued_count(q);

	spin_lock_irq(q->queue_lock);

	el_ret = elv_merge(q, &req, bio);
	if (el_ret == ELEVATOR_BACK_MERGE) {
		if (bio_attempt_back_merge(q, req, bio)) {
			elv_bio_merged(q, req, bio);
			if (!attempt_back_merge(q, req))
				elv_merged_request(q, req, el_ret);
			goto out_unlock;
		}
	} else if (el_ret == ELEVATOR_FRONT_MERGE) {
		if (bio_attempt_front_merge(q, req, bio)) {
			elv_bio_merged(q, req, bio);
			if (!attempt_front_merge(q, req))
				elv_merged_request(q, req, el_ret);
			goto out_unlock;
		}
	}

get_rq:
	/*
	 * This sync check and mask will be re-done in init_request_from_bio(),
	 * but we need to set it earlier to expose the sync flag to the
	 * rq allocator and io schedulers.
	 */
	rw_flags = bio_data_dir(bio);
	if (sync)
		rw_flags |= REQ_SYNC;

	/*
	 * Grab a free request. This is might sleep but can not fail.
	 * Returns with the queue unlocked.
	 */
	req = get_request(q, rw_flags, bio, GFP_NOIO);
	if (IS_ERR(req)) {
		bio->bi_error = PTR_ERR(req);
		bio_endio(bio);
		goto out_unlock;
	}

	/*
	 * After dropping the lock and possibly sleeping here, our request
	 * may now be mergeable after it had proven unmergeable (above).
	 * We don't worry about that case for efficiency. It won't happen
	 * often, and the elevators are able to handle it.
	 */
	init_request_from_bio(req, bio);

	if (test_bit(QUEUE_FLAG_SAME_COMP, &q->queue_flags))
		req->cpu = raw_smp_processor_id();

	plug = current->plug;
	if (plug) {
		/*
		 * If this is the first request added after a plug, fire
		 * of a plug trace.
		 */
		if (!request_count)
			trace_block_plug(q);
		else {
			if (request_count >= BLK_MAX_REQUEST_COUNT) {
				blk_flush_plug_list(plug, false);
				trace_block_plug(q);
			}
		}
		list_add_tail(&req->queuelist, &plug->list);
		blk_account_io_start(req, true);
	} else {
		spin_lock_irq(q->queue_lock);
		add_acct_request(q, req, where);
		__blk_run_queue(q);
out_unlock:
		spin_unlock_irq(q->queue_lock);
	}

	return BLK_QC_T_NONE;
}

/*
 * If bio->bi_dev is a partition, remap the location
 */
static inline void blk_partition_remap(struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;

	if (bio_sectors(bio) && bdev != bdev->bd_contains) {
		struct hd_struct *p = bdev->bd_part;

		bio->bi_iter.bi_sector += p->start_sect;
		bio->bi_bdev = bdev->bd_contains;

		trace_block_bio_remap(bdev_get_queue(bio->bi_bdev), bio,
				      bdev->bd_dev,
				      bio->bi_iter.bi_sector - p->start_sect);
	}
}

static void handle_bad_sector(struct bio *bio)
{
	char b[BDEVNAME_SIZE];

	printk(KERN_INFO "attempt to access beyond end of device\n");
	printk(KERN_INFO "%s: rw=%ld, want=%Lu, limit=%Lu\n",
			bdevname(bio->bi_bdev, b),
			bio->bi_rw,
			(unsigned long long)bio_end_sector(bio),
			(long long)(i_size_read(bio->bi_bdev->bd_inode) >> 9));
}

#ifdef CONFIG_FAIL_MAKE_REQUEST

static DECLARE_FAULT_ATTR(fail_make_request);

static int __init setup_fail_make_request(char *str)
{
	return setup_fault_attr(&fail_make_request, str);
}
__setup("fail_make_request=", setup_fail_make_request);

static bool should_fail_request(struct hd_struct *part, unsigned int bytes)
{
	return part->make_it_fail && should_fail(&fail_make_request, bytes);
}

static int __init fail_make_request_debugfs(void)
{
	struct dentry *dir = fault_create_debugfs_attr("fail_make_request",
						NULL, &fail_make_request);

	return PTR_ERR_OR_ZERO(dir);
}

late_initcall(fail_make_request_debugfs);

#else /* CONFIG_FAIL_MAKE_REQUEST */

static inline bool should_fail_request(struct hd_struct *part,
					unsigned int bytes)
{
	return false;
}

#endif /* CONFIG_FAIL_MAKE_REQUEST */

/*
 * Check whether this bio extends beyond the end of the device.
 */
static inline int bio_check_eod(struct bio *bio, unsigned int nr_sectors)
{
	sector_t maxsector;

	if (!nr_sectors)
		return 0;

	/* Test device or partition size, when known. */
	maxsector = i_size_read(bio->bi_bdev->bd_inode) >> 9;
	if (maxsector) {
		sector_t sector = bio->bi_iter.bi_sector;

		if (maxsector < nr_sectors || maxsector - nr_sectors < sector) {
			/*
			 * This may well happen - the kernel calls bread()
			 * without checking the size of the device, e.g., when
			 * mounting a device.
			 */
			handle_bad_sector(bio);
			return 1;
		}
	}

	return 0;
}

static noinline_for_stack bool
generic_make_request_checks(struct bio *bio)
{
	struct request_queue *q;
	int nr_sectors = bio_sectors(bio);
	int err = -EIO;
	char b[BDEVNAME_SIZE];
	struct hd_struct *part;

	might_sleep();

	if (bio_check_eod(bio, nr_sectors))
		goto end_io;

	q = bdev_get_queue(bio->bi_bdev);
	if (unlikely(!q)) {
		printk(KERN_ERR
		       "generic_make_request: Trying to access "
			"nonexistent block-device %s (%Lu)\n",
			bdevname(bio->bi_bdev, b),
			(long long) bio->bi_iter.bi_sector);
		goto end_io;
	}

	part = bio->bi_bdev->bd_part;
	if (should_fail_request(part, bio->bi_iter.bi_size) ||
	    should_fail_request(&part_to_disk(part)->part0,
				bio->bi_iter.bi_size))
		goto end_io;

	/*
	 * If this device has partitions, remap block n
	 * of partition p to block n+start(p) of the disk.
	 */
	blk_partition_remap(bio);

	if (bio_check_eod(bio, nr_sectors))
		goto end_io;

	/*
	 * Filter flush bio's early so that make_request based
	 * drivers without flush support don't have to worry
	 * about them.
	 */
	if ((bio->bi_rw & (REQ_FLUSH | REQ_FUA)) && !q->flush_flags) {
		bio->bi_rw &= ~(REQ_FLUSH | REQ_FUA);
		if (!nr_sectors) {
			err = 0;
			goto end_io;
		}
	}

	if ((bio->bi_rw & REQ_DISCARD) &&
	    (!blk_queue_discard(q) ||
	     ((bio->bi_rw & REQ_SECURE) && !blk_queue_secdiscard(q)))) {
		err = -EOPNOTSUPP;
		goto end_io;
	}

	if (bio->bi_rw & REQ_WRITE_SAME && !bdev_write_same(bio->bi_bdev)) {
		err = -EOPNOTSUPP;
		goto end_io;
	}

	/*
	 * Various block parts want %current->io_context and lazy ioc
	 * allocation ends up trading a lot of pain for a small amount of
	 * memory.  Just allocate it upfront.  This may fail and block
	 * layer knows how to live with it.
	 */
	create_io_context(GFP_ATOMIC, q->node);

	if (!blkcg_bio_issue_check(q, bio))
		return false;

	trace_block_bio_queue(q, bio);
	return true;

end_io:
	bio->bi_error = err;
	bio_endio(bio);
	return false;
}

/**
 * generic_make_request - hand a buffer to its device driver for I/O
 * @bio:  The bio describing the location in memory and on the device.
 *
 * generic_make_request() is used to make I/O requests of block
 * devices. It is passed a &struct bio, which describes the I/O that needs
 * to be done.
 *
 * generic_make_request() does not return any status.  The
 * success/failure status of the request, along with notification of
 * completion, is delivered asynchronously through the bio->bi_end_io
 * function described (one day) else where.
 *
 * The caller of generic_make_request must make sure that bi_io_vec
 * are set to describe the memory buffer, and that bi_dev and bi_sector are
 * set to describe the device address, and the
 * bi_end_io and optionally bi_private are set to describe how
 * completion notification should be signaled.
 *
 * generic_make_request and the drivers it calls may use bi_next if this
 * bio happens to be merged with someone else, and may resubmit the bio to
 * a lower device by calling into generic_make_request recursively, which
 * means the bio should NOT be touched after the call to ->make_request_fn.
 */
blk_qc_t generic_make_request(struct bio *bio)
{
	/*
	 * bio_list_on_stack[0] contains bios submitted by the current
	 * make_request_fn.
	 * bio_list_on_stack[1] contains bios that were submitted before
	 * the current make_request_fn, but that haven't been processed
	 * yet.
	 */
	struct bio_list bio_list_on_stack[2];
	blk_qc_t ret = BLK_QC_T_NONE;

	if (!generic_make_request_checks(bio))
		goto out;

	/*
	 * We only want one ->make_request_fn to be active at a time, else
	 * stack usage with stacked devices could be a problem.  So use
	 * current->bio_list to keep a list of requests submited by a
	 * make_request_fn function.  current->bio_list is also used as a
	 * flag to say if generic_make_request is currently active in this
	 * task or not.  If it is NULL, then no make_request is active.  If
	 * it is non-NULL, then a make_request is active, and new requests
	 * should be added at the tail
	 */
	if (current->bio_list) {
		bio_list_add(&current->bio_list[0], bio);
		goto out;
	}

	/* following loop may be a bit non-obvious, and so deserves some
	 * explanation.
	 * Before entering the loop, bio->bi_next is NULL (as all callers
	 * ensure that) so we have a list with a single bio.
	 * We pretend that we have just taken it off a longer list, so
	 * we assign bio_list to a pointer to the bio_list_on_stack,
	 * thus initialising the bio_list of new bios to be
	 * added.  ->make_request() may indeed add some more bios
	 * through a recursive call to generic_make_request.  If it
	 * did, we find a non-NULL value in bio_list and re-enter the loop
	 * from the top.  In this case we really did just take the bio
	 * of the top of the list (no pretending) and so remove it from
	 * bio_list, and call into ->make_request() again.
	 */
	BUG_ON(bio->bi_next);
	bio_list_init(&bio_list_on_stack[0]);
	current->bio_list = bio_list_on_stack;
	do {
		struct request_queue *q = bdev_get_queue(bio->bi_bdev);

		if (likely(blk_queue_enter(q, __GFP_DIRECT_RECLAIM) == 0)) {
			struct bio_list lower, same;

			/* Create a fresh bio_list for all subordinate requests */
			bio_list_on_stack[1] = bio_list_on_stack[0];
			bio_list_init(&bio_list_on_stack[0]);

			ret = q->make_request_fn(q, bio);

			blk_queue_exit(q);
			/* sort new bios into those for a lower level
			 * and those for the same level
			 */
			bio_list_init(&lower);
			bio_list_init(&same);
			while ((bio = bio_list_pop(&bio_list_on_stack[0])) != NULL)
				if (q == bdev_get_queue(bio->bi_bdev))
					bio_list_add(&same, bio);
				else
					bio_list_add(&lower, bio);
			/* now assemble so we handle the lowest level first */
			bio_list_merge(&bio_list_on_stack[0], &lower);
			bio_list_merge(&bio_list_on_stack[0], &same);
			bio_list_merge(&bio_list_on_stack[0], &bio_list_on_stack[1]);
		} else {
			bio_io_error(bio);
		}
		bio = bio_list_pop(&bio_list_on_stack[0]);
	} while (bio);
	current->bio_list = NULL; /* deactivate */

out:
	return ret;
}
EXPORT_SYMBOL(generic_make_request);

/**
 * submit_bio - submit a bio to the block device layer for I/O
 * @rw: whether to %READ or %WRITE, or maybe to %READA (read ahead)
 * @bio: The &struct bio which describes the I/O
 *
 * submit_bio() is very similar in purpose to generic_make_request(), and
 * uses that function to do most of the work. Both are fairly rough
 * interfaces; @bio must be presetup and ready for I/O.
 *
 */
blk_qc_t submit_bio(int rw, struct bio *bio)
{
	bio->bi_rw |= rw;

	/*
	 * If it's a regular read/write or a barrier with data attached,
	 * go through the normal accounting stuff before submission.
	 */
	if (bio_has_data(bio)) {
		unsigned int count;

		if (unlikely(rw & REQ_WRITE_SAME))
			count = bdev_logical_block_size(bio->bi_bdev) >> 9;
		else
			count = bio_sectors(bio);

		if (rw & WRITE) {
			count_vm_events(PGPGOUT, count);
		} else {
			task_io_account_read(bio->bi_iter.bi_size);
			count_vm_events(PGPGIN, count);
		}

		if (unlikely(block_dump)) {
			char b[BDEVNAME_SIZE];
			printk(KERN_DEBUG "%s(%d): %s block %Lu on %s (%u sectors)\n",
			current->comm, task_pid_nr(current),
				(rw & WRITE) ? "WRITE" : "READ",
				(unsigned long long)bio->bi_iter.bi_sector,
				bdevname(bio->bi_bdev, b),
				count);
		}
	}

	return generic_make_request(bio);
}
EXPORT_SYMBOL(submit_bio);

/**
 * blk_cloned_rq_check_limits - Helper function to check a cloned request
 *                              for new the queue limits
 * @q:  the queue
 * @rq: the request being checked
 *
 * Description:
 *    @rq may have been made based on weaker limitations of upper-level queues
 *    in request stacking drivers, and it may violate the limitation of @q.
 *    Since the block layer and the underlying device driver trust @rq
 *    after it is inserted to @q, it should be checked against @q before
 *    the insertion using this generic function.
 *
 *    Request stacking drivers like request-based dm may change the queue
 *    limits when retrying requests on other queues. Those requests need
 *    to be checked against the new queue limits again during dispatch.
 */
static int blk_cloned_rq_check_limits(struct request_queue *q,
				      struct request *rq)
{
	if (blk_rq_sectors(rq) > blk_queue_get_max_sectors(q, rq->cmd_flags)) {
		printk(KERN_ERR "%s: over max size limit.\n", __func__);
		return -EIO;
	}

	/*
	 * queue's settings related to segment counting like q->bounce_pfn
	 * may differ from that of other stacking queues.
	 * Recalculate it to check the request correctly on this queue's
	 * limitation.
	 */
	blk_recalc_rq_segments(rq);
	if (rq->nr_phys_segments > queue_max_segments(q)) {
		printk(KERN_ERR "%s: over max segments limit.\n", __func__);
		return -EIO;
	}

	return 0;
}

/**
 * blk_insert_cloned_request - Helper for stacking drivers to submit a request
 * @q:  the queue to submit the request
 * @rq: the request being queued
 */
int blk_insert_cloned_request(struct request_queue *q, struct request *rq)
{
	unsigned long flags;
	int where = ELEVATOR_INSERT_BACK;

	if (blk_cloned_rq_check_limits(q, rq))
		return -EIO;

	if (rq->rq_disk &&
	    should_fail_request(&rq->rq_disk->part0, blk_rq_bytes(rq)))
		return -EIO;

	if (q->mq_ops) {
		if (blk_queue_io_stat(q))
			blk_account_io_start(rq, true);
		blk_mq_insert_request(rq, false, true, false);
		return 0;
	}

	spin_lock_irqsave(q->queue_lock, flags);
	if (unlikely(blk_queue_dying(q))) {
		spin_unlock_irqrestore(q->queue_lock, flags);
		return -ENODEV;
	}

	/*
	 * Submitting request must be dequeued before calling this function
	 * because it will be linked to another request_queue
	 */
	BUG_ON(blk_queued_rq(rq));

	if (rq->cmd_flags & (REQ_FLUSH|REQ_FUA))
		where = ELEVATOR_INSERT_FLUSH;

	add_acct_request(q, rq, where);
	if (where == ELEVATOR_INSERT_FLUSH)
		__blk_run_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(blk_insert_cloned_request);

/**
 * blk_rq_err_bytes - determine number of bytes till the next failure boundary
 * @rq: request to examine
 *
 * Description:
 *     A request could be merge of IOs which require different failure
 *     handling.  This function determines the number of bytes which
 *     can be failed from the beginning of the request without
 *     crossing into area which need to be retried further.
 *
 * Return:
 *     The number of bytes to fail.
 *
 * Context:
 *     queue_lock must be held.
 */
unsigned int blk_rq_err_bytes(const struct request *rq)
{
	unsigned int ff = rq->cmd_flags & REQ_FAILFAST_MASK;
	unsigned int bytes = 0;
	struct bio *bio;

	if (!(rq->cmd_flags & REQ_MIXED_MERGE))
		return blk_rq_bytes(rq);

	/*
	 * Currently the only 'mixing' which can happen is between
	 * different fastfail types.  We can safely fail portions
	 * which have all the failfast bits that the first one has -
	 * the ones which are at least as eager to fail as the first
	 * one.
	 */
	for (bio = rq->bio; bio; bio = bio->bi_next) {
		if ((bio->bi_rw & ff) != ff)
			break;
		bytes += bio->bi_iter.bi_size;
	}

	/* this could lead to infinite loop */
	BUG_ON(blk_rq_bytes(rq) && !bytes);
	return bytes;
}
EXPORT_SYMBOL_GPL(blk_rq_err_bytes);

void blk_account_io_completion(struct request *req, unsigned int bytes)
{
	if (blk_do_io_stat(req)) {
		const int rw = rq_data_dir(req);
		struct hd_struct *part;
		int cpu;

		cpu = part_stat_lock();
		part = req->part;
		part_stat_add(cpu, part, sectors[rw], bytes >> 9);
		part_stat_unlock();
	}
}

void blk_account_io_done(struct request *req)
{
	/*
	 * Account IO completion.  flush_rq isn't accounted as a
	 * normal IO on queueing nor completion.  Accounting the
	 * containing request is enough.
	 */
	if (blk_do_io_stat(req) && !(req->cmd_flags & REQ_FLUSH_SEQ)) {
		unsigned long duration = jiffies - req->start_time;
		const int rw = rq_data_dir(req);
		struct hd_struct *part;
		int cpu;

		cpu = part_stat_lock();
		part = req->part;

		part_stat_inc(cpu, part, ios[rw]);
		part_stat_add(cpu, part, ticks[rw], duration);
		part_round_stats(cpu, part);
		part_dec_in_flight(part, rw);

		hd_struct_put(part);
		part_stat_unlock();
	}
}

#ifdef CONFIG_PM
/*
 * Don't process normal requests when queue is suspended
 * or in the process of suspending/resuming
 */
static struct request *blk_pm_peek_request(struct request_queue *q,
					   struct request *rq)
{
	if (q->dev && (q->rpm_status == RPM_SUSPENDED ||
	    (q->rpm_status != RPM_ACTIVE && !(rq->cmd_flags & REQ_PM))))
		return NULL;
	else
		return rq;
}
#else
static inline struct request *blk_pm_peek_request(struct request_queue *q,
						  struct request *rq)
{
	return rq;
}
#endif

void blk_account_io_start(struct request *rq, bool new_io)
{
	struct hd_struct *part;
	int rw = rq_data_dir(rq);
	int cpu;

	if (!blk_do_io_stat(rq))
		return;

	cpu = part_stat_lock();

	if (!new_io) {
		part = rq->part;
		part_stat_inc(cpu, part, merges[rw]);
	} else {
		part = disk_map_sector_rcu(rq->rq_disk, blk_rq_pos(rq));
		if (!hd_struct_try_get(part)) {
			/*
			 * The partition is already being removed,
			 * the request will be accounted on the disk only
			 *
			 * We take a reference on disk->part0 although that
			 * partition will never be deleted, so we can treat
			 * it as any other partition.
			 */
			part = &rq->rq_disk->part0;
			hd_struct_get(part);
		}
		part_round_stats(cpu, part);
		part_inc_in_flight(part, rw);
		rq->part = part;
	}

	part_stat_unlock();
}

/**
 * blk_peek_request - peek at the top of a request queue
 * @q: request queue to peek at
 *
 * Description:
 *     Return the request at the top of @q.  The returned request
 *     should be started using blk_start_request() before LLD starts
 *     processing it.
 *
 * Return:
 *     Pointer to the request at the top of @q if available.  Null
 *     otherwise.
 *
 * Context:
 *     queue_lock must be held.
 */
struct request *blk_peek_request(struct request_queue *q)
{
	struct request *rq;
	int ret;

	while ((rq = __elv_next_request(q)) != NULL) {

		rq = blk_pm_peek_request(q, rq);
		if (!rq)
			break;

		if (!(rq->cmd_flags & REQ_STARTED)) {
			/*
			 * This is the first time the device driver
			 * sees this request (possibly after
			 * requeueing).  Notify IO scheduler.
			 */
			if (rq->cmd_flags & REQ_SORTED)
				elv_activate_rq(q, rq);

			/*
			 * just mark as started even if we don't start
			 * it, a request that has been delayed should
			 * not be passed by new incoming requests
			 */
			rq->cmd_flags |= REQ_STARTED;
			trace_block_rq_issue(q, rq);
		}

		if (!q->boundary_rq || q->boundary_rq == rq) {
			q->end_sector = rq_end_sector(rq);
			q->boundary_rq = NULL;
		}

		if (rq->cmd_flags & REQ_DONTPREP)
			break;

		if (q->dma_drain_size && blk_rq_bytes(rq)) {
			/*
			 * make sure space for the drain appears we
			 * know we can do this because max_hw_segments
			 * has been adjusted to be one fewer than the
			 * device can handle
			 */
			rq->nr_phys_segments++;
		}

		if (!q->prep_rq_fn)
			break;

		ret = q->prep_rq_fn(q, rq);
		if (ret == BLKPREP_OK) {
			break;
		} else if (ret == BLKPREP_DEFER) {
			/*
			 * the request may have been (partially) prepped.
			 * we need to keep this request in the front to
			 * avoid resource deadlock.  REQ_STARTED will
			 * prevent other fs requests from passing this one.
			 */
			if (q->dma_drain_size && blk_rq_bytes(rq) &&
			    !(rq->cmd_flags & REQ_DONTPREP)) {
				/*
				 * remove the space for the drain we added
				 * so that we don't add it again
				 */
				--rq->nr_phys_segments;
			}

			rq = NULL;
			break;
		} else if (ret == BLKPREP_KILL) {
			rq->cmd_flags |= REQ_QUIET;
			/*
			 * Mark this request as started so we don't trigger
			 * any debug logic in the end I/O path.
			 */
			blk_start_request(rq);
			__blk_end_request_all(rq, -EIO);
		} else {
			printk(KERN_ERR "%s: bad return=%d\n", __func__, ret);
			break;
		}
	}

	return rq;
}
EXPORT_SYMBOL(blk_peek_request);

void blk_dequeue_request(struct request *rq)
{
	struct request_queue *q = rq->q;

	BUG_ON(list_empty(&rq->queuelist));
	BUG_ON(ELV_ON_HASH(rq));

	list_del_init(&rq->queuelist);

	/*
	 * the time frame between a request being removed from the lists
	 * and to it is freed is accounted as io that is in progress at
	 * the driver side.
	 */
	if (blk_account_rq(rq)) {
		q->in_flight[rq_is_sync(rq)]++;
		set_io_start_time_ns(rq);
	}
}

/**
 * blk_start_request - start request processing on the driver
 * @req: request to dequeue
 *
 * Description:
 *     Dequeue @req and start timeout timer on it.  This hands off the
 *     request to the driver.
 *
 *     Block internal functions which don't want to start timer should
 *     call blk_dequeue_request().
 *
 * Context:
 *     queue_lock must be held.
 */
void blk_start_request(struct request *req)
{
	blk_dequeue_request(req);

	/*
	 * We are now handing the request to the hardware, initialize
	 * resid_len to full count and add the timeout handler.
	 */
	req->resid_len = blk_rq_bytes(req);
	if (unlikely(blk_bidi_rq(req)))
		req->next_rq->resid_len = blk_rq_bytes(req->next_rq);

	BUG_ON(test_bit(REQ_ATOM_COMPLETE, &req->atomic_flags));
	blk_add_timer(req);
}
EXPORT_SYMBOL(blk_start_request);

/**
 * blk_fetch_request - fetch a request from a request queue
 * @q: request queue to fetch a request from
 *
 * Description:
 *     Return the request at the top of @q.  The request is started on
 *     return and LLD can start processing it immediately.
 *
 * Return:
 *     Pointer to the request at the top of @q if available.  Null
 *     otherwise.
 *
 * Context:
 *     queue_lock must be held.
 */
struct request *blk_fetch_request(struct request_queue *q)
{
	struct request *rq;

	rq = blk_peek_request(q);
	if (rq)
		blk_start_request(rq);
	return rq;
}
EXPORT_SYMBOL(blk_fetch_request);

/**
 * blk_update_request - Special helper function for request stacking drivers
 * @req:      the request being processed
 * @error:    %0 for success, < %0 for error
 * @nr_bytes: number of bytes to complete @req
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @req, but doesn't complete
 *     the request structure even if @req doesn't have leftover.
 *     If @req has leftover, sets it up for the next range of segments.
 *
 *     This special helper function is only for request stacking drivers
 *     (e.g. request-based dm) so that they can handle partial completion.
 *     Actual device drivers should use blk_end_request instead.
 *
 *     Passing the result of blk_rq_bytes() as @nr_bytes guarantees
 *     %false return from this function.
 *
 * Return:
 *     %false - this request doesn't have any more data
 *     %true  - this request has more data
 **/
bool blk_update_request(struct request *req, int error, unsigned int nr_bytes)
{
	int total_bytes;

	trace_block_rq_complete(req->q, req, nr_bytes);

	if (!req->bio)
		return false;

	/*
	 * For fs requests, rq is just carrier of independent bio's
	 * and each partial completion should be handled separately.
	 * Reset per-request error on each partial completion.
	 *
	 * TODO: tj: This is too subtle.  It would be better to let
	 * low level drivers do what they see fit.
	 */
	if (req->cmd_type == REQ_TYPE_FS)
		req->errors = 0;

	if (error && req->cmd_type == REQ_TYPE_FS &&
	    !(req->cmd_flags & REQ_QUIET)) {
		char *error_type;

		switch (error) {
		case -ENOLINK:
			error_type = "recoverable transport";
			break;
		case -EREMOTEIO:
			error_type = "critical target";
			break;
		case -EBADE:
			error_type = "critical nexus";
			break;
		case -ETIMEDOUT:
			error_type = "timeout";
			break;
		case -ENOSPC:
			error_type = "critical space allocation";
			break;
		case -ENODATA:
			error_type = "critical medium";
			break;
		case -EIO:
		default:
			error_type = "I/O";
			break;
		}
		printk_ratelimited(KERN_ERR "%s: %s error, dev %s, sector %llu\n",
				   __func__, error_type, req->rq_disk ?
				   req->rq_disk->disk_name : "?",
				   (unsigned long long)blk_rq_pos(req));

	}

	blk_account_io_completion(req, nr_bytes);

	total_bytes = 0;
	while (req->bio) {
		struct bio *bio = req->bio;
		unsigned bio_bytes = min(bio->bi_iter.bi_size, nr_bytes);

		if (bio_bytes == bio->bi_iter.bi_size)
			req->bio = bio->bi_next;

		req_bio_endio(req, bio, bio_bytes, error);

		total_bytes += bio_bytes;
		nr_bytes -= bio_bytes;

		if (!nr_bytes)
			break;
	}

	/*
	 * completely done
	 */
	if (!req->bio) {
		/*
		 * Reset counters so that the request stacking driver
		 * can find how many bytes remain in the request
		 * later.
		 */
		req->__data_len = 0;
		return false;
	}

	req->__data_len -= total_bytes;

	/* update sector only for requests with clear definition of sector */
	if (req->cmd_type == REQ_TYPE_FS)
		req->__sector += total_bytes >> 9;

	/* mixed attributes always follow the first bio */
	if (req->cmd_flags & REQ_MIXED_MERGE) {
		req->cmd_flags &= ~REQ_FAILFAST_MASK;
		req->cmd_flags |= req->bio->bi_rw & REQ_FAILFAST_MASK;
	}

	/*
	 * If total number of sectors is less than the first segment
	 * size, something has gone terribly wrong.
	 */
	if (blk_rq_bytes(req) < blk_rq_cur_bytes(req)) {
		blk_dump_rq_flags(req, "request botched");
		req->__data_len = blk_rq_cur_bytes(req);
	}

	/* recalculate the number of segments */
	blk_recalc_rq_segments(req);

	return true;
}
EXPORT_SYMBOL_GPL(blk_update_request);

static bool blk_update_bidi_request(struct request *rq, int error,
				    unsigned int nr_bytes,
				    unsigned int bidi_bytes)
{
	if (blk_update_request(rq, error, nr_bytes))
		return true;

	/* Bidi request must be completed as a whole */
	if (unlikely(blk_bidi_rq(rq)) &&
	    blk_update_request(rq->next_rq, error, bidi_bytes))
		return true;

	if (blk_queue_add_random(rq->q))
		add_disk_randomness(rq->rq_disk);

	return false;
}

/**
 * blk_unprep_request - unprepare a request
 * @req:	the request
 *
 * This function makes a request ready for complete resubmission (or
 * completion).  It happens only after all error handling is complete,
 * so represents the appropriate moment to deallocate any resources
 * that were allocated to the request in the prep_rq_fn.  The queue
 * lock is held when calling this.
 */
void blk_unprep_request(struct request *req)
{
	struct request_queue *q = req->q;

	req->cmd_flags &= ~REQ_DONTPREP;
	if (q->unprep_rq_fn)
		q->unprep_rq_fn(q, req);
}
EXPORT_SYMBOL_GPL(blk_unprep_request);

/*
 * queue lock must be held
 */
void blk_finish_request(struct request *req, int error)
{
	if (req->cmd_flags & REQ_QUEUED)
		blk_queue_end_tag(req->q, req);

	BUG_ON(blk_queued_rq(req));

	if (unlikely(laptop_mode) && req->cmd_type == REQ_TYPE_FS)
		laptop_io_completion(&req->q->backing_dev_info);

	blk_delete_timer(req);

	if (req->cmd_flags & REQ_DONTPREP)
		blk_unprep_request(req);

	blk_account_io_done(req);

	if (req->end_io)
		req->end_io(req, error);
	else {
		if (blk_bidi_rq(req))
			__blk_put_request(req->next_rq->q, req->next_rq);

		__blk_put_request(req->q, req);
	}
}
EXPORT_SYMBOL(blk_finish_request);

/**
 * blk_end_bidi_request - Complete a bidi request
 * @rq:         the request to complete
 * @error:      %0 for success, < %0 for error
 * @nr_bytes:   number of bytes to complete @rq
 * @bidi_bytes: number of bytes to complete @rq->next_rq
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @rq and @rq->next_rq.
 *     Drivers that supports bidi can safely call this member for any
 *     type of request, bidi or uni.  In the later case @bidi_bytes is
 *     just ignored.
 *
 * Return:
 *     %false - we are done with this request
 *     %true  - still buffers pending for this request
 **/
static bool blk_end_bidi_request(struct request *rq, int error,
				 unsigned int nr_bytes, unsigned int bidi_bytes)
{
	struct request_queue *q = rq->q;
	unsigned long flags;

	if (blk_update_bidi_request(rq, error, nr_bytes, bidi_bytes))
		return true;

	spin_lock_irqsave(q->queue_lock, flags);
	blk_finish_request(rq, error);
	spin_unlock_irqrestore(q->queue_lock, flags);

	return false;
}

/**
 * __blk_end_bidi_request - Complete a bidi request with queue lock held
 * @rq:         the request to complete
 * @error:      %0 for success, < %0 for error
 * @nr_bytes:   number of bytes to complete @rq
 * @bidi_bytes: number of bytes to complete @rq->next_rq
 *
 * Description:
 *     Identical to blk_end_bidi_request() except that queue lock is
 *     assumed to be locked on entry and remains so on return.
 *
 * Return:
 *     %false - we are done with this request
 *     %true  - still buffers pending for this request
 **/
bool __blk_end_bidi_request(struct request *rq, int error,
				   unsigned int nr_bytes, unsigned int bidi_bytes)
{
	if (blk_update_bidi_request(rq, error, nr_bytes, bidi_bytes))
		return true;

	blk_finish_request(rq, error);

	return false;
}

/**
 * blk_end_request - Helper function for drivers to complete the request.
 * @rq:       the request being processed
 * @error:    %0 for success, < %0 for error
 * @nr_bytes: number of bytes to complete
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @rq.
 *     If @rq has leftover, sets it up for the next range of segments.
 *
 * Return:
 *     %false - we are done with this request
 *     %true  - still buffers pending for this request
 **/
bool blk_end_request(struct request *rq, int error, unsigned int nr_bytes)
{
	return blk_end_bidi_request(rq, error, nr_bytes, 0);
}
EXPORT_SYMBOL(blk_end_request);

/**
 * blk_end_request_all - Helper function for drives to finish the request.
 * @rq: the request to finish
 * @error: %0 for success, < %0 for error
 *
 * Description:
 *     Completely finish @rq.
 */
void blk_end_request_all(struct request *rq, int error)
{
	bool pending;
	unsigned int bidi_bytes = 0;

	if (unlikely(blk_bidi_rq(rq)))
		bidi_bytes = blk_rq_bytes(rq->next_rq);

	pending = blk_end_bidi_request(rq, error, blk_rq_bytes(rq), bidi_bytes);
	BUG_ON(pending);
}
EXPORT_SYMBOL(blk_end_request_all);

/**
 * blk_end_request_cur - Helper function to finish the current request chunk.
 * @rq: the request to finish the current chunk for
 * @error: %0 for success, < %0 for error
 *
 * Description:
 *     Complete the current consecutively mapped chunk from @rq.
 *
 * Return:
 *     %false - we are done with this request
 *     %true  - still buffers pending for this request
 */
bool blk_end_request_cur(struct request *rq, int error)
{
	return blk_end_request(rq, error, blk_rq_cur_bytes(rq));
}
EXPORT_SYMBOL(blk_end_request_cur);

/**
 * blk_end_request_err - Finish a request till the next failure boundary.
 * @rq: the request to finish till the next failure boundary for
 * @error: must be negative errno
 *
 * Description:
 *     Complete @rq till the next failure boundary.
 *
 * Return:
 *     %false - we are done with this request
 *     %true  - still buffers pending for this request
 */
bool blk_end_request_err(struct request *rq, int error)
{
	WARN_ON(error >= 0);
	return blk_end_request(rq, error, blk_rq_err_bytes(rq));
}
EXPORT_SYMBOL_GPL(blk_end_request_err);

/**
 * __blk_end_request - Helper function for drivers to complete the request.
 * @rq:       the request being processed
 * @error:    %0 for success, < %0 for error
 * @nr_bytes: number of bytes to complete
 *
 * Description:
 *     Must be called with queue lock held unlike blk_end_request().
 *
 * Return:
 *     %false - we are done with this request
 *     %true  - still buffers pending for this request
 **/
bool __blk_end_request(struct request *rq, int error, unsigned int nr_bytes)
{
	return __blk_end_bidi_request(rq, error, nr_bytes, 0);
}
EXPORT_SYMBOL(__blk_end_request);

/**
 * __blk_end_request_all - Helper function for drives to finish the request.
 * @rq: the request to finish
 * @error: %0 for success, < %0 for error
 *
 * Description:
 *     Completely finish @rq.  Must be called with queue lock held.
 */
void __blk_end_request_all(struct request *rq, int error)
{
	bool pending;
	unsigned int bidi_bytes = 0;

	if (unlikely(blk_bidi_rq(rq)))
		bidi_bytes = blk_rq_bytes(rq->next_rq);

	pending = __blk_end_bidi_request(rq, error, blk_rq_bytes(rq), bidi_bytes);
	BUG_ON(pending);
}
EXPORT_SYMBOL(__blk_end_request_all);

/**
 * __blk_end_request_cur - Helper function to finish the current request chunk.
 * @rq: the request to finish the current chunk for
 * @error: %0 for success, < %0 for error
 *
 * Description:
 *     Complete the current consecutively mapped chunk from @rq.  Must
 *     be called with queue lock held.
 *
 * Return:
 *     %false - we are done with this request
 *     %true  - still buffers pending for this request
 */
bool __blk_end_request_cur(struct request *rq, int error)
{
	return __blk_end_request(rq, error, blk_rq_cur_bytes(rq));
}
EXPORT_SYMBOL(__blk_end_request_cur);

/**
 * __blk_end_request_err - Finish a request till the next failure boundary.
 * @rq: the request to finish till the next failure boundary for
 * @error: must be negative errno
 *
 * Description:
 *     Complete @rq till the next failure boundary.  Must be called
 *     with queue lock held.
 *
 * Return:
 *     %false - we are done with this request
 *     %true  - still buffers pending for this request
 */
bool __blk_end_request_err(struct request *rq, int error)
{
	WARN_ON(error >= 0);
	return __blk_end_request(rq, error, blk_rq_err_bytes(rq));
}
EXPORT_SYMBOL_GPL(__blk_end_request_err);

void blk_rq_bio_prep(struct request_queue *q, struct request *rq,
		     struct bio *bio)
{
	/* Bit 0 (R/W) is identical in rq->cmd_flags and bio->bi_rw */
	rq->cmd_flags |= bio->bi_rw & REQ_WRITE;

	if (bio_has_data(bio))
		rq->nr_phys_segments = bio_phys_segments(q, bio);

	rq->__data_len = bio->bi_iter.bi_size;
	rq->bio = rq->biotail = bio;

	if (bio->bi_bdev)
		rq->rq_disk = bio->bi_bdev->bd_disk;
}

#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
/**
 * rq_flush_dcache_pages - Helper function to flush all pages in a request
 * @rq: the request to be flushed
 *
 * Description:
 *     Flush all pages in @rq.
 */
void rq_flush_dcache_pages(struct request *rq)
{
	struct req_iterator iter;
	struct bio_vec bvec;

	rq_for_each_segment(bvec, rq, iter)
		flush_dcache_page(bvec.bv_page);
}
EXPORT_SYMBOL_GPL(rq_flush_dcache_pages);
#endif

/**
 * blk_lld_busy - Check if underlying low-level drivers of a device are busy
 * @q : the queue of the device being checked
 *
 * Description:
 *    Check if underlying low-level drivers of a device are busy.
 *    If the drivers want to export their busy state, they must set own
 *    exporting function using blk_queue_lld_busy() first.
 *
 *    Basically, this function is used only by request stacking drivers
 *    to stop dispatching requests to underlying devices when underlying
 *    devices are busy.  This behavior helps more I/O merging on the queue
 *    of the request stacking driver and prevents I/O throughput regression
 *    on burst I/O load.
 *
 * Return:
 *    0 - Not busy (The request stacking driver should dispatch request)
 *    1 - Busy (The request stacking driver should stop dispatching request)
 */
int blk_lld_busy(struct request_queue *q)
{
	if (q->lld_busy_fn)
		return q->lld_busy_fn(q);

	return 0;
}
EXPORT_SYMBOL_GPL(blk_lld_busy);

/**
 * blk_rq_unprep_clone - Helper function to free all bios in a cloned request
 * @rq: the clone request to be cleaned up
 *
 * Description:
 *     Free all bios in @rq for a cloned request.
 */
void blk_rq_unprep_clone(struct request *rq)
{
	struct bio *bio;

	while ((bio = rq->bio) != NULL) {
		rq->bio = bio->bi_next;

		bio_put(bio);
	}
}
EXPORT_SYMBOL_GPL(blk_rq_unprep_clone);

/*
 * Copy attributes of the original request to the clone request.
 * The actual data parts (e.g. ->cmd, ->sense) are not copied.
 */
static void __blk_rq_prep_clone(struct request *dst, struct request *src)
{
	dst->cpu = src->cpu;
	dst->cmd_flags |= (src->cmd_flags & REQ_CLONE_MASK) | REQ_NOMERGE;
	dst->cmd_type = src->cmd_type;
	dst->__sector = blk_rq_pos(src);
	dst->__data_len = blk_rq_bytes(src);
	dst->nr_phys_segments = src->nr_phys_segments;
	dst->ioprio = src->ioprio;
	dst->extra_len = src->extra_len;
}

/**
 * blk_rq_prep_clone - Helper function to setup clone request
 * @rq: the request to be setup
 * @rq_src: original request to be cloned
 * @bs: bio_set that bios for clone are allocated from
 * @gfp_mask: memory allocation mask for bio
 * @bio_ctr: setup function to be called for each clone bio.
 *           Returns %0 for success, non %0 for failure.
 * @data: private data to be passed to @bio_ctr
 *
 * Description:
 *     Clones bios in @rq_src to @rq, and copies attributes of @rq_src to @rq.
 *     The actual data parts of @rq_src (e.g. ->cmd, ->sense)
 *     are not copied, and copying such parts is the caller's responsibility.
 *     Also, pages which the original bios are pointing to are not copied
 *     and the cloned bios just point same pages.
 *     So cloned bios must be completed before original bios, which means
 *     the caller must complete @rq before @rq_src.
 */
int blk_rq_prep_clone(struct request *rq, struct request *rq_src,
		      struct bio_set *bs, gfp_t gfp_mask,
		      int (*bio_ctr)(struct bio *, struct bio *, void *),
		      void *data)
{
	struct bio *bio, *bio_src;

	if (!bs)
		bs = fs_bio_set;

	__rq_for_each_bio(bio_src, rq_src) {
		bio = bio_clone_fast(bio_src, gfp_mask, bs);
		if (!bio)
			goto free_and_out;

		if (bio_ctr && bio_ctr(bio, bio_src, data))
			goto free_and_out;

		if (rq->bio) {
			rq->biotail->bi_next = bio;
			rq->biotail = bio;
		} else
			rq->bio = rq->biotail = bio;
	}

	__blk_rq_prep_clone(rq, rq_src);

	return 0;

free_and_out:
	if (bio)
		bio_put(bio);
	blk_rq_unprep_clone(rq);

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(blk_rq_prep_clone);

int kblockd_schedule_work(struct work_struct *work)
{
	return queue_work(kblockd_workqueue, work);
}
EXPORT_SYMBOL(kblockd_schedule_work);

int kblockd_schedule_delayed_work(struct delayed_work *dwork,
				  unsigned long delay)
{
	return queue_delayed_work(kblockd_workqueue, dwork, delay);
}
EXPORT_SYMBOL(kblockd_schedule_delayed_work);

int kblockd_schedule_delayed_work_on(int cpu, struct delayed_work *dwork,
				     unsigned long delay)
{
	return queue_delayed_work_on(cpu, kblockd_workqueue, dwork, delay);
}
EXPORT_SYMBOL(kblockd_schedule_delayed_work_on);

/**
 * blk_start_plug - initialize blk_plug and track it inside the task_struct
 * @plug:	The &struct blk_plug that needs to be initialized
 *
 * Description:
 *   Tracking blk_plug inside the task_struct will help with auto-flushing the
 *   pending I/O should the task end up blocking between blk_start_plug() and
 *   blk_finish_plug(). This is important from a performance perspective, but
 *   also ensures that we don't deadlock. For instance, if the task is blocking
 *   for a memory allocation, memory reclaim could end up wanting to free a
 *   page belonging to that request that is currently residing in our private
 *   plug. By flushing the pending I/O when the process goes to sleep, we avoid
 *   this kind of deadlock.
 */
void blk_start_plug(struct blk_plug *plug)
{
	struct task_struct *tsk = current;

	/*
	 * If this is a nested plug, don't actually assign it.
	 */
	if (tsk->plug)
		return;

	INIT_LIST_HEAD(&plug->list);
	INIT_LIST_HEAD(&plug->mq_list);
	INIT_LIST_HEAD(&plug->cb_list);
	/*
	 * Store ordering should not be needed here, since a potential
	 * preempt will imply a full memory barrier
	 */
	tsk->plug = plug;
}
EXPORT_SYMBOL(blk_start_plug);

static int plug_rq_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct request *rqa = container_of(a, struct request, queuelist);
	struct request *rqb = container_of(b, struct request, queuelist);

	return !(rqa->q < rqb->q ||
		(rqa->q == rqb->q && blk_rq_pos(rqa) < blk_rq_pos(rqb)));
}

/*
 * If 'from_schedule' is true, then postpone the dispatch of requests
 * until a safe kblockd context. We due this to avoid accidental big
 * additional stack usage in driver dispatch, in places where the originally
 * plugger did not intend it.
 */
static void queue_unplugged(struct request_queue *q, unsigned int depth,
			    bool from_schedule)
	__releases(q->queue_lock)
{
	trace_block_unplug(q, depth, !from_schedule);

	if (from_schedule)
		blk_run_queue_async(q);
	else
		__blk_run_queue(q);
	spin_unlock(q->queue_lock);
}

static void flush_plug_callbacks(struct blk_plug *plug, bool from_schedule)
{
	LIST_HEAD(callbacks);

	while (!list_empty(&plug->cb_list)) {
		list_splice_init(&plug->cb_list, &callbacks);

		while (!list_empty(&callbacks)) {
			struct blk_plug_cb *cb = list_first_entry(&callbacks,
							  struct blk_plug_cb,
							  list);
			list_del(&cb->list);
			cb->callback(cb, from_schedule);
		}
	}
}

struct blk_plug_cb *blk_check_plugged(blk_plug_cb_fn unplug, void *data,
				      int size)
{
	struct blk_plug *plug = current->plug;
	struct blk_plug_cb *cb;

	if (!plug)
		return NULL;

	list_for_each_entry(cb, &plug->cb_list, list)
		if (cb->callback == unplug && cb->data == data)
			return cb;

	/* Not currently on the callback list */
	BUG_ON(size < sizeof(*cb));
	cb = kzalloc(size, GFP_ATOMIC);
	if (cb) {
		cb->data = data;
		cb->callback = unplug;
		list_add(&cb->list, &plug->cb_list);
	}
	return cb;
}
EXPORT_SYMBOL(blk_check_plugged);

void blk_flush_plug_list(struct blk_plug *plug, bool from_schedule)
{
	struct request_queue *q;
	unsigned long flags;
	struct request *rq;
	LIST_HEAD(list);
	unsigned int depth;

	flush_plug_callbacks(plug, from_schedule);

	if (!list_empty(&plug->mq_list))
		blk_mq_flush_plug_list(plug, from_schedule);

	if (list_empty(&plug->list))
		return;

	list_splice_init(&plug->list, &list);

	list_sort(NULL, &list, plug_rq_cmp);

	q = NULL;
	depth = 0;

	/*
	 * Save and disable interrupts here, to avoid doing it for every
	 * queue lock we have to take.
	 */
	local_irq_save(flags);
	while (!list_empty(&list)) {
		rq = list_entry_rq(list.next);
		list_del_init(&rq->queuelist);
		BUG_ON(!rq->q);
		if (rq->q != q) {
			/*
			 * This drops the queue lock
			 */
			if (q)
				queue_unplugged(q, depth, from_schedule);
			q = rq->q;
			depth = 0;
			spin_lock(q->queue_lock);
		}

		/*
		 * Short-circuit if @q is dead
		 */
		if (unlikely(blk_queue_dying(q))) {
			__blk_end_request_all(rq, -ENODEV);
			continue;
		}

		/*
		 * rq is already accounted, so use raw insert
		 */
		if (rq->cmd_flags & (REQ_FLUSH | REQ_FUA))
			__elv_add_request(q, rq, ELEVATOR_INSERT_FLUSH);
		else
			__elv_add_request(q, rq, ELEVATOR_INSERT_SORT_MERGE);

		depth++;
	}

	/*
	 * This drops the queue lock
	 */
	if (q)
		queue_unplugged(q, depth, from_schedule);

	local_irq_restore(flags);
}

void blk_finish_plug(struct blk_plug *plug)
{
	if (plug != current->plug)
		return;
	blk_flush_plug_list(plug, false);

	current->plug = NULL;
}
EXPORT_SYMBOL(blk_finish_plug);

bool blk_poll(struct request_queue *q, blk_qc_t cookie)
{
	struct blk_plug *plug;
	long state;

	if (!q->mq_ops || !q->mq_ops->poll || !blk_qc_t_valid(cookie) ||
	    !test_bit(QUEUE_FLAG_POLL, &q->queue_flags))
		return false;

	plug = current->plug;
	if (plug)
		blk_flush_plug_list(plug, false);

	state = current->state;
	while (!need_resched()) {
		unsigned int queue_num = blk_qc_t_to_queue_num(cookie);
		struct blk_mq_hw_ctx *hctx = q->queue_hw_ctx[queue_num];
		int ret;

		hctx->poll_invoked++;

		ret = q->mq_ops->poll(hctx, blk_qc_t_to_tag(cookie));
		if (ret > 0) {
			hctx->poll_success++;
			set_current_state(TASK_RUNNING);
			return true;
		}

		if (signal_pending_state(state, current))
			set_current_state(TASK_RUNNING);

		if (current->state == TASK_RUNNING)
			return true;
		if (ret < 0)
			break;
		cpu_relax();
	}

	return false;
}

#ifdef CONFIG_PM
/**
 * blk_pm_runtime_init - Block layer runtime PM initialization routine
 * @q: the queue of the device
 * @dev: the device the queue belongs to
 *
 * Description:
 *    Initialize runtime-PM-related fields for @q and start auto suspend for
 *    @dev. Drivers that want to take advantage of request-based runtime PM
 *    should call this function after @dev has been initialized, and its
 *    request queue @q has been allocated, and runtime PM for it can not happen
 *    yet(either due to disabled/forbidden or its usage_count > 0). In most
 *    cases, driver should call this function before any I/O has taken place.
 *
 *    This function takes care of setting up using auto suspend for the device,
 *    the autosuspend delay is set to -1 to make runtime suspend impossible
 *    until an updated value is either set by user or by driver. Drivers do
 *    not need to touch other autosuspend settings.
 *
 *    The block layer runtime PM is request based, so only works for drivers
 *    that use request as their IO unit instead of those directly use bio's.
 */
void blk_pm_runtime_init(struct request_queue *q, struct device *dev)
{
	q->dev = dev;
	q->rpm_status = RPM_ACTIVE;
	pm_runtime_set_autosuspend_delay(q->dev, -1);
	pm_runtime_use_autosuspend(q->dev);
}
EXPORT_SYMBOL(blk_pm_runtime_init);

/**
 * blk_pre_runtime_suspend - Pre runtime suspend check
 * @q: the queue of the device
 *
 * Description:
 *    This function will check if runtime suspend is allowed for the device
 *    by examining if there are any requests pending in the queue. If there
 *    are requests pending, the device can not be runtime suspended; otherwise,
 *    the queue's status will be updated to SUSPENDING and the driver can
 *    proceed to suspend the device.
 *
 *    For the not allowed case, we mark last busy for the device so that
 *    runtime PM core will try to autosuspend it some time later.
 *
 *    This function should be called near the start of the device's
 *    runtime_suspend callback.
 *
 * Return:
 *    0		- OK to runtime suspend the device
 *    -EBUSY	- Device should not be runtime suspended
 */
int blk_pre_runtime_suspend(struct request_queue *q)
{
	int ret = 0;

	if (!q->dev)
		return ret;

	spin_lock_irq(q->queue_lock);
	if (q->nr_pending) {
		ret = -EBUSY;
		pm_runtime_mark_last_busy(q->dev);
	} else {
		q->rpm_status = RPM_SUSPENDING;
	}
	spin_unlock_irq(q->queue_lock);
	return ret;
}
EXPORT_SYMBOL(blk_pre_runtime_suspend);

/**
 * blk_post_runtime_suspend - Post runtime suspend processing
 * @q: the queue of the device
 * @err: return value of the device's runtime_suspend function
 *
 * Description:
 *    Update the queue's runtime status according to the return value of the
 *    device's runtime suspend function and mark last busy for the device so
 *    that PM core will try to auto suspend the device at a later time.
 *
 *    This function should be called near the end of the device's
 *    runtime_suspend callback.
 */
void blk_post_runtime_suspend(struct request_queue *q, int err)
{
	if (!q->dev)
		return;

	spin_lock_irq(q->queue_lock);
	if (!err) {
		q->rpm_status = RPM_SUSPENDED;
	} else {
		q->rpm_status = RPM_ACTIVE;
		pm_runtime_mark_last_busy(q->dev);
	}
	spin_unlock_irq(q->queue_lock);
}
EXPORT_SYMBOL(blk_post_runtime_suspend);

/**
 * blk_pre_runtime_resume - Pre runtime resume processing
 * @q: the queue of the device
 *
 * Description:
 *    Update the queue's runtime status to RESUMING in preparation for the
 *    runtime resume of the device.
 *
 *    This function should be called near the start of the device's
 *    runtime_resume callback.
 */
void blk_pre_runtime_resume(struct request_queue *q)
{
	if (!q->dev)
		return;

	spin_lock_irq(q->queue_lock);
	q->rpm_status = RPM_RESUMING;
	spin_unlock_irq(q->queue_lock);
}
EXPORT_SYMBOL(blk_pre_runtime_resume);

/**
 * blk_post_runtime_resume - Post runtime resume processing
 * @q: the queue of the device
 * @err: return value of the device's runtime_resume function
 *
 * Description:
 *    Update the queue's runtime status according to the return value of the
 *    device's runtime_resume function. If it is successfully resumed, process
 *    the requests that are queued into the device's queue when it is resuming
 *    and then mark last busy and initiate autosuspend for it.
 *
 *    This function should be called near the end of the device's
 *    runtime_resume callback.
 */
void blk_post_runtime_resume(struct request_queue *q, int err)
{
	if (!q->dev)
		return;

	spin_lock_irq(q->queue_lock);
	if (!err) {
		q->rpm_status = RPM_ACTIVE;
		__blk_run_queue(q);
		pm_runtime_mark_last_busy(q->dev);
		pm_request_autosuspend(q->dev);
	} else {
		q->rpm_status = RPM_SUSPENDED;
	}
	spin_unlock_irq(q->queue_lock);
}
EXPORT_SYMBOL(blk_post_runtime_resume);
#endif

int __init blk_dev_init(void)
{
	BUILD_BUG_ON(__REQ_NR_BITS > 8 *
			FIELD_SIZEOF(struct request, cmd_flags));

	/* used for unplugging and affects IO latency/throughput - HIGHPRI */
	kblockd_workqueue = alloc_workqueue("kblockd",
					    WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	if (!kblockd_workqueue)
		panic("Failed to create kblockd\n");

	request_cachep = kmem_cache_create("blkdev_requests",
			sizeof(struct request), 0, SLAB_PANIC, NULL);

	blk_requestq_cachep = kmem_cache_create("blkdev_queue",
			sizeof(struct request_queue), 0, SLAB_PANIC, NULL);

	return 0;
}
