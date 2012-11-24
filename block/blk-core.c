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

#define CREATE_TRACE_POINTS
#include <trace/events/block.h>

#include "blk.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(block_bio_remap);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_remap);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_bio_complete);

DEFINE_IDA(blk_queue_ida);

/*
 * For the allocated request tables
 */
static struct kmem_cache *request_cachep;

/*
 * For queue allocation
 */
struct kmem_cache *blk_requestq_cachep;

/*
 * Controlling structure to kblockd
 */
static struct workqueue_struct *kblockd_workqueue;

static void drive_stat_acct(struct request *rq, int new_io)
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
 * backing_dev_info
 *
 * Will return NULL if the request queue cannot be located.
 */
struct backing_dev_info *blk_get_backing_dev_info(struct block_device *bdev)
{
	struct backing_dev_info *ret = NULL;
	struct request_queue *q = bdev_get_queue(bdev);

	if (q)
		ret = &q->backing_dev_info;
	return ret;
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
	rq->ref_count = 1;
	rq->start_time = jiffies;
	set_start_time_ns(rq);
	rq->part = NULL;
}
EXPORT_SYMBOL(blk_rq_init);

static void req_bio_endio(struct request *rq, struct bio *bio,
			  unsigned int nbytes, int error)
{
	if (error)
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
	else if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		error = -EIO;

	if (unlikely(nbytes > bio->bi_size)) {
		printk(KERN_ERR "%s: want %u bytes done, %u left\n",
		       __func__, nbytes, bio->bi_size);
		nbytes = bio->bi_size;
	}

	if (unlikely(rq->cmd_flags & REQ_QUIET))
		set_bit(BIO_QUIET, &bio->bi_flags);

	bio->bi_size -= nbytes;
	bio->bi_sector += (nbytes >> 9);

	if (bio_integrity(bio))
		bio_integrity_advance(bio, nbytes);

	/* don't actually finish bio if it's part of flush sequence */
	if (bio->bi_size == 0 && !(rq->cmd_flags & REQ_FLUSH_SEQ))
		bio_endio(bio, error);
}

void blk_dump_rq_flags(struct request *rq, char *msg)
{
	int bit;

	printk(KERN_INFO "%s: dev %s: type=%x, flags=%x\n", msg,
		rq->rq_disk ? rq->rq_disk->disk_name : "?", rq->cmd_type,
		rq->cmd_flags);

	printk(KERN_INFO "  sector %llu, nr/cnr %u/%u\n",
	       (unsigned long long)blk_rq_pos(rq),
	       blk_rq_sectors(rq), blk_rq_cur_sectors(rq));
	printk(KERN_INFO "  bio %p, biotail %p, buffer %p, len %u\n",
	       rq->bio, rq->biotail, rq->buffer, blk_rq_bytes(rq));

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
 *   restarted around the specified time.
 */
void blk_delay_queue(struct request_queue *q, unsigned long msecs)
{
	queue_delayed_work(kblockd_workqueue, &q->delay_work,
				msecs_to_jiffies(msecs));
}
EXPORT_SYMBOL(blk_delay_queue);

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
	WARN_ON(!irqs_disabled());

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
	__cancel_delayed_work(&q->delay_work);
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
 *     out of elevator or throttling code. That would require elevaotor_exit()
 *     and blk_throtl_exit() to be called with queue lock initialized.
 *
 */
void blk_sync_queue(struct request_queue *q)
{
	del_timer_sync(&q->timeout);
	cancel_delayed_work_sync(&q->delay_work);
}
EXPORT_SYMBOL(blk_sync_queue);

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

	q->request_fn(q);
}
EXPORT_SYMBOL(__blk_run_queue);

/**
 * blk_run_queue_async - run a single device queue in workqueue context
 * @q:	The queue to run
 *
 * Description:
 *    Tells kblockd to perform the equivalent of @blk_run_queue on behalf
 *    of us.
 */
void blk_run_queue_async(struct request_queue *q)
{
	if (likely(!blk_queue_stopped(q))) {
		__cancel_delayed_work(&q->delay_work);
		queue_delayed_work(kblockd_workqueue, &q->delay_work, 0);
	}
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
 * blk_drain_queue - drain requests from request_queue
 * @q: queue to drain
 * @drain_all: whether to drain all requests or only the ones w/ ELVPRIV
 *
 * Drain requests from @q.  If @drain_all is set, all requests are drained.
 * If not, only ELVPRIV requests are drained.  The caller is responsible
 * for ensuring that no new requests which need to be drained are queued.
 */
void blk_drain_queue(struct request_queue *q, bool drain_all)
{
	while (true) {
		bool drain = false;
		int i;

		spin_lock_irq(q->queue_lock);

		elv_drain_elevator(q);
		if (drain_all)
			blk_throtl_drain(q);

		/*
		 * This function might be called on a queue which failed
		 * driver init after queue creation.  Some drivers
		 * (e.g. fd) get unhappy in such cases.  Kick queue iff
		 * dispatch queue has something on it.
		 */
		if (!list_empty(&q->queue_head))
			__blk_run_queue(q);

		drain |= q->rq.elvpriv;

		/*
		 * Unfortunately, requests are queued at and tracked from
		 * multiple places and there's no single counter which can
		 * be drained.  Check all the queues and counters.
		 */
		if (drain_all) {
			drain |= !list_empty(&q->queue_head);
			for (i = 0; i < 2; i++) {
				drain |= q->rq.count[i];
				drain |= q->in_flight[i];
				drain |= !list_empty(&q->flush_queue[i]);
			}
		}

		spin_unlock_irq(q->queue_lock);

		if (!drain)
			break;
		msleep(10);
	}
}

/**
 * blk_cleanup_queue - shutdown a request queue
 * @q: request queue to shutdown
 *
 * Mark @q DEAD, drain all pending requests, destroy and put it.  All
 * future requests will be failed immediately with -ENODEV.
 */
void blk_cleanup_queue(struct request_queue *q)
{
	spinlock_t *lock = q->queue_lock;

	/* mark @q DEAD, no new request or merges will be allowed afterwards */
	mutex_lock(&q->sysfs_lock);
	queue_flag_set_unlocked(QUEUE_FLAG_DEAD, q);

	spin_lock_irq(lock);
	queue_flag_set(QUEUE_FLAG_NOMERGES, q);
	queue_flag_set(QUEUE_FLAG_NOXMERGES, q);
	queue_flag_set(QUEUE_FLAG_DEAD, q);

	if (q->queue_lock != &q->__queue_lock)
		q->queue_lock = &q->__queue_lock;

	spin_unlock_irq(lock);
	mutex_unlock(&q->sysfs_lock);

	/*
	 * Drain all requests queued before DEAD marking.  The caller might
	 * be trying to tear down @q before its elevator is initialized, in
	 * which case we don't want to call into draining.
	 */
	if (q->elevator)
		blk_drain_queue(q, true);

	/* @q won't process any more request, flush async actions */
	del_timer_sync(&q->backing_dev_info.laptop_mode_wb_timer);
	blk_sync_queue(q);

	/* @q is and will stay empty, shutdown and put */
	blk_put_queue(q);
}
EXPORT_SYMBOL(blk_cleanup_queue);

static int blk_init_free_list(struct request_queue *q)
{
	struct request_list *rl = &q->rq;

	if (unlikely(rl->rq_pool))
		return 0;

	rl->count[BLK_RW_SYNC] = rl->count[BLK_RW_ASYNC] = 0;
	rl->starved[BLK_RW_SYNC] = rl->starved[BLK_RW_ASYNC] = 0;
	rl->elvpriv = 0;
	init_waitqueue_head(&rl->wait[BLK_RW_SYNC]);
	init_waitqueue_head(&rl->wait[BLK_RW_ASYNC]);

	rl->rq_pool = mempool_create_node(BLKDEV_MIN_RQ, mempool_alloc_slab,
				mempool_free_slab, request_cachep, q->node);

	if (!rl->rq_pool)
		return -ENOMEM;

	return 0;
}

struct request_queue *blk_alloc_queue(gfp_t gfp_mask)
{
	return blk_alloc_queue_node(gfp_mask, -1);
}
EXPORT_SYMBOL(blk_alloc_queue);

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

	q->backing_dev_info.ra_pages =
			(VM_MAX_READAHEAD * 1024) / PAGE_CACHE_SIZE;
	q->backing_dev_info.state = 0;
	q->backing_dev_info.capabilities = BDI_CAP_MAP_COPY;
	q->backing_dev_info.name = "block";
	q->node = node_id;

	err = bdi_init(&q->backing_dev_info);
	if (err)
		goto fail_id;

	if (blk_throtl_init(q))
		goto fail_id;

	setup_timer(&q->backing_dev_info.laptop_mode_wb_timer,
		    laptop_mode_timer_fn, (unsigned long) q);
	setup_timer(&q->timeout, blk_rq_timed_out_timer, (unsigned long) q);
	INIT_LIST_HEAD(&q->timeout_list);
	INIT_LIST_HEAD(&q->icq_list);
	INIT_LIST_HEAD(&q->flush_queue[0]);
	INIT_LIST_HEAD(&q->flush_queue[1]);
	INIT_LIST_HEAD(&q->flush_data_in_flight);
	INIT_DELAYED_WORK(&q->delay_work, blk_delay_work);

	kobject_init(&q->kobj, &blk_queue_ktype);

	mutex_init(&q->sysfs_lock);
	spin_lock_init(&q->__queue_lock);

	/*
	 * By default initialize queue_lock to internal lock and driver can
	 * override it later if need be.
	 */
	q->queue_lock = &q->__queue_lock;

	return q;

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
	return blk_init_queue_node(rfn, lock, -1);
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

struct request_queue *
blk_init_allocated_queue(struct request_queue *q, request_fn_proc *rfn,
			 spinlock_t *lock)
{
	if (!q)
		return NULL;

	if (blk_init_free_list(q))
		return NULL;

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

	/*
	 * all done
	 */
	if (!elevator_init(q, NULL)) {
		blk_queue_congestion_threshold(q);
		return q;
	}

	return NULL;
}
EXPORT_SYMBOL(blk_init_allocated_queue);

bool blk_get_queue(struct request_queue *q)
{
	if (likely(!blk_queue_dead(q))) {
		__blk_get_queue(q);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(blk_get_queue);

static inline void blk_free_request(struct request_queue *q, struct request *rq)
{
	if (rq->cmd_flags & REQ_ELVPRIV) {
		elv_put_request(q, rq);
		if (rq->elv.icq)
			put_io_context(rq->elv.icq->ioc);
	}

	mempool_free(rq, q->rq.rq_pool);
}

static struct request *
blk_alloc_request(struct request_queue *q, struct io_cq *icq,
		  unsigned int flags, gfp_t gfp_mask)
{
	struct request *rq = mempool_alloc(q->rq.rq_pool, gfp_mask);

	if (!rq)
		return NULL;

	blk_rq_init(q, rq);

	rq->cmd_flags = flags | REQ_ALLOCED;

	if (flags & REQ_ELVPRIV) {
		rq->elv.icq = icq;
		if (unlikely(elv_set_request(q, rq, gfp_mask))) {
			mempool_free(rq, q->rq.rq_pool);
			return NULL;
		}
		/* @rq->elv.icq holds on to io_context until @rq is freed */
		if (icq)
			get_io_context(icq->ioc);
	}

	return rq;
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

static void __freed_request(struct request_queue *q, int sync)
{
	struct request_list *rl = &q->rq;

	if (rl->count[sync] < queue_congestion_off_threshold(q))
		blk_clear_queue_congested(q, sync);

	if (rl->count[sync] + 1 <= q->nr_requests) {
		if (waitqueue_active(&rl->wait[sync]))
			wake_up(&rl->wait[sync]);

		blk_clear_queue_full(q, sync);
	}
}

/*
 * A request has just been released.  Account for it, update the full and
 * congestion status, wake up any waiters.   Called under q->queue_lock.
 */
static void freed_request(struct request_queue *q, unsigned int flags)
{
	struct request_list *rl = &q->rq;
	int sync = rw_is_sync(flags);

	rl->count[sync]--;
	if (flags & REQ_ELVPRIV)
		rl->elvpriv--;

	__freed_request(q, sync);

	if (unlikely(rl->starved[sync ^ 1]))
		__freed_request(q, sync ^ 1);
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
 * get_request - get a free request
 * @q: request_queue to allocate request from
 * @rw_flags: RW and SYNC flags
 * @bio: bio to allocate request for (can be %NULL)
 * @gfp_mask: allocation mask
 *
 * Get a free request from @q.  This function may fail under memory
 * pressure or if @q is dead.
 *
 * Must be callled with @q->queue_lock held and,
 * Returns %NULL on failure, with @q->queue_lock held.
 * Returns !%NULL on success, with @q->queue_lock *not held*.
 */
static struct request *get_request(struct request_queue *q, int rw_flags,
				   struct bio *bio, gfp_t gfp_mask)
{
	struct request *rq = NULL;
	struct request_list *rl = &q->rq;
	struct elevator_type *et;
	struct io_context *ioc;
	struct io_cq *icq = NULL;
	const bool is_sync = rw_is_sync(rw_flags) != 0;
	bool retried = false;
	int may_queue;
retry:
	et = q->elevator->type;
	ioc = current->io_context;

	if (unlikely(blk_queue_dead(q)))
		return NULL;

	may_queue = elv_may_queue(q, rw_flags);
	if (may_queue == ELV_MQUEUE_NO)
		goto rq_starved;

	if (rl->count[is_sync]+1 >= queue_congestion_on_threshold(q)) {
		if (rl->count[is_sync]+1 >= q->nr_requests) {
			/*
			 * We want ioc to record batching state.  If it's
			 * not already there, creating a new one requires
			 * dropping queue_lock, which in turn requires
			 * retesting conditions to avoid queue hang.
			 */
			if (!ioc && !retried) {
				spin_unlock_irq(q->queue_lock);
				create_io_context(current, gfp_mask, q->node);
				spin_lock_irq(q->queue_lock);
				retried = true;
				goto retry;
			}

			/*
			 * The queue will fill after this allocation, so set
			 * it as full, and mark this process as "batching".
			 * This process will be allowed to complete a batch of
			 * requests, others will be blocked.
			 */
			if (!blk_queue_full(q, is_sync)) {
				ioc_set_batching(q, ioc);
				blk_set_queue_full(q, is_sync);
			} else {
				if (may_queue != ELV_MQUEUE_MUST
						&& !ioc_batching(q, ioc)) {
					/*
					 * The queue is full and the allocating
					 * process is not a "batcher", and not
					 * exempted by the IO scheduler
					 */
					goto out;
				}
			}
		}
		blk_set_queue_congested(q, is_sync);
	}

	/*
	 * Only allow batching queuers to allocate up to 50% over the defined
	 * limit of requests, otherwise we could have thousands of requests
	 * allocated with any setting of ->nr_requests
	 */
	if (rl->count[is_sync] >= (3 * q->nr_requests / 2))
		goto out;

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
	if (blk_rq_should_init_elevator(bio) &&
	    !test_bit(QUEUE_FLAG_ELVSWITCH, &q->queue_flags)) {
		rw_flags |= REQ_ELVPRIV;
		rl->elvpriv++;
		if (et->icq_cache && ioc)
			icq = ioc_lookup_icq(ioc, q);
	}

	if (blk_queue_io_stat(q))
		rw_flags |= REQ_IO_STAT;
	spin_unlock_irq(q->queue_lock);

	/* create icq if missing */
	if ((rw_flags & REQ_ELVPRIV) && unlikely(et->icq_cache && !icq)) {
		icq = ioc_create_icq(q, gfp_mask);
		if (!icq)
			goto fail_icq;
	}

	rq = blk_alloc_request(q, icq, rw_flags, gfp_mask);

fail_icq:
	if (unlikely(!rq)) {
		/*
		 * Allocation failed presumably due to memory. Undo anything
		 * we might have messed up.
		 *
		 * Allocating task should really be put onto the front of the
		 * wait queue, but this is pretty rare.
		 */
		spin_lock_irq(q->queue_lock);
		freed_request(q, rw_flags);

		/*
		 * in the very unlikely event that allocation failed and no
		 * requests for this direction was pending, mark us starved
		 * so that freeing of a request in the other direction will
		 * notice us. another possible fix would be to split the
		 * rq mempool into READ and WRITE
		 */
rq_starved:
		if (unlikely(rl->count[is_sync] == 0))
			rl->starved[is_sync] = 1;

		goto out;
	}

	/*
	 * ioc may be NULL here, and ioc_batching will be false. That's
	 * OK, if the queue is under the request limit then requests need
	 * not count toward the nr_batch_requests limit. There will always
	 * be some limit enforced by BLK_BATCH_TIME.
	 */
	if (ioc_batching(q, ioc))
		ioc->nr_batch_requests--;

	trace_block_getrq(q, bio, rw_flags & 1);
out:
	return rq;
}

/**
 * get_request_wait - get a free request with retry
 * @q: request_queue to allocate request from
 * @rw_flags: RW and SYNC flags
 * @bio: bio to allocate request for (can be %NULL)
 *
 * Get a free request from @q.  This function keeps retrying under memory
 * pressure and fails iff @q is dead.
 *
 * Must be callled with @q->queue_lock held and,
 * Returns %NULL on failure, with @q->queue_lock held.
 * Returns !%NULL on success, with @q->queue_lock *not held*.
 */
static struct request *get_request_wait(struct request_queue *q, int rw_flags,
					struct bio *bio)
{
	const bool is_sync = rw_is_sync(rw_flags) != 0;
	struct request *rq;

	rq = get_request(q, rw_flags, bio, GFP_NOIO);
	while (!rq) {
		DEFINE_WAIT(wait);
		struct request_list *rl = &q->rq;

		if (unlikely(blk_queue_dead(q)))
			return NULL;

		prepare_to_wait_exclusive(&rl->wait[is_sync], &wait,
				TASK_UNINTERRUPTIBLE);

		trace_block_sleeprq(q, bio, rw_flags & 1);

		spin_unlock_irq(q->queue_lock);
		io_schedule();

		/*
		 * After sleeping, we become a "batching" process and
		 * will be able to allocate at least one request, and
		 * up to a big batch of them for a small period time.
		 * See ioc_batching, ioc_set_batching
		 */
		create_io_context(current, GFP_NOIO, q->node);
		ioc_set_batching(q, current->io_context);

		spin_lock_irq(q->queue_lock);
		finish_wait(&rl->wait[is_sync], &wait);

		rq = get_request(q, rw_flags, bio, GFP_NOIO);
	};

	return rq;
}

struct request *blk_get_request(struct request_queue *q, int rw, gfp_t gfp_mask)
{
	struct request *rq;

	BUG_ON(rw != READ && rw != WRITE);

	spin_lock_irq(q->queue_lock);
	if (gfp_mask & __GFP_WAIT)
		rq = get_request_wait(q, rw, NULL);
	else
		rq = get_request(q, rw, NULL, gfp_mask);
	if (!rq)
		spin_unlock_irq(q->queue_lock);
	/* q->queue_lock is unlocked at this point */

	return rq;
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
 * given to how you allocate bios. In particular, you cannot use __GFP_WAIT for
 * anything but the first bio in the chain. Otherwise you risk waiting for IO
 * completion of a bio that hasn't been submitted yet, thus resulting in a
 * deadlock. Alternatively bios should be allocated using bio_kmalloc() instead
 * of bio_alloc(), as that avoids the mempool deadlock.
 * If possible a big IO should be split into smaller parts when allocation
 * fails. Partial allocation should not be an error, or you risk a live-lock.
 */
struct request *blk_make_request(struct request_queue *q, struct bio *bio,
				 gfp_t gfp_mask)
{
	struct request *rq = blk_get_request(q, bio_data_dir(bio), gfp_mask);

	if (unlikely(!rq))
		return ERR_PTR(-ENOMEM);

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

	if (blk_rq_tagged(rq))
		blk_queue_end_tag(q, rq);

	BUG_ON(blk_queued_rq(rq));

	elv_requeue_request(q, rq);
}
EXPORT_SYMBOL(blk_requeue_request);

static void add_acct_request(struct request_queue *q, struct request *rq,
			     int where)
{
	drive_stat_acct(rq, 1);
	__elv_add_request(q, rq, where);
}

static void part_round_stats_single(int cpu, struct hd_struct *part,
				    unsigned long now)
{
	if (now == part->stamp)
		return;

	if (part_in_flight(part)) {
		__part_stat_add(cpu, part, time_in_queue,
				part_in_flight(part) * (now - part->stamp));
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

/*
 * queue lock must be held
 */
void __blk_put_request(struct request_queue *q, struct request *req)
{
	if (unlikely(!q))
		return;
	if (unlikely(--req->ref_count))
		return;

	elv_completed_request(q, req);

	/* this is a bio leak */
	WARN_ON(req->bio != NULL);

	/*
	 * Request may not have originated from ll_rw_blk. if not,
	 * it didn't come out of our reserved rq pools
	 */
	if (req->cmd_flags & REQ_ALLOCED) {
		unsigned int flags = req->cmd_flags;

		BUG_ON(!list_empty(&req->queuelist));
		BUG_ON(!hlist_unhashed(&req->hash));

		blk_free_request(q, req);
		freed_request(q, flags);
	}
}
EXPORT_SYMBOL_GPL(__blk_put_request);

void blk_put_request(struct request *req)
{
	unsigned long flags;
	struct request_queue *q = req->q;

	spin_lock_irqsave(q->queue_lock, flags);
	__blk_put_request(q, req);
	spin_unlock_irqrestore(q->queue_lock, flags);
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

	bio->bi_size = len;
	bio->bi_vcnt = 1;
	bio->bi_phys_segments = 1;

	rq->__data_len = rq->resid_len = len;
	rq->nr_phys_segments = 1;
	rq->buffer = bio_data(bio);
}
EXPORT_SYMBOL_GPL(blk_add_request_payload);

static bool bio_attempt_back_merge(struct request_queue *q, struct request *req,
				   struct bio *bio)
{
	const int ff = bio->bi_rw & REQ_FAILFAST_MASK;

	if (!ll_back_merge_fn(q, req, bio))
		return false;

	trace_block_bio_backmerge(q, bio);

	if ((req->cmd_flags & REQ_FAILFAST_MASK) != ff)
		blk_rq_set_mixed_merge(req);

	req->biotail->bi_next = bio;
	req->biotail = bio;
	req->__data_len += bio->bi_size;
	req->ioprio = ioprio_best(req->ioprio, bio_prio(bio));

	drive_stat_acct(req, 0);
	return true;
}

static bool bio_attempt_front_merge(struct request_queue *q,
				    struct request *req, struct bio *bio)
{
	const int ff = bio->bi_rw & REQ_FAILFAST_MASK;

	if (!ll_front_merge_fn(q, req, bio))
		return false;

	trace_block_bio_frontmerge(q, bio);

	if ((req->cmd_flags & REQ_FAILFAST_MASK) != ff)
		blk_rq_set_mixed_merge(req);

	bio->bi_next = req->bio;
	req->bio = bio;

	/*
	 * may not be valid. if the low level driver said
	 * it didn't need a bounce buffer then it better
	 * not touch req->buffer either...
	 */
	req->buffer = bio_data(bio);
	req->__sector = bio->bi_sector;
	req->__data_len += bio->bi_size;
	req->ioprio = ioprio_best(req->ioprio, bio_prio(bio));

	drive_stat_acct(req, 0);
	return true;
}

/**
 * attempt_plug_merge - try to merge with %current's plugged list
 * @q: request_queue new bio is being queued at
 * @bio: new bio being queued
 * @request_count: out parameter for number of traversed plugged requests
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
 */
static bool attempt_plug_merge(struct request_queue *q, struct bio *bio,
			       unsigned int *request_count)
{
	struct blk_plug *plug;
	struct request *rq;
	bool ret = false;

	plug = current->plug;
	if (!plug)
		goto out;
	*request_count = 0;

	list_for_each_entry_reverse(rq, &plug->list, queuelist) {
		int el_ret;

		if (rq->q == q)
			(*request_count)++;

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

void init_request_from_bio(struct request *req, struct bio *bio)
{
	req->cmd_type = REQ_TYPE_FS;

	req->cmd_flags |= bio->bi_rw & REQ_COMMON_MASK;
	if (bio->bi_rw & REQ_RAHEAD)
		req->cmd_flags |= REQ_FAILFAST_MASK;

	req->errors = 0;
	req->__sector = bio->bi_sector;
	req->ioprio = bio_prio(bio);
	blk_rq_bio_prep(req->q, req, bio);
}

void blk_queue_bio(struct request_queue *q, struct bio *bio)
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

	if (bio->bi_rw & (REQ_FLUSH | REQ_FUA)) {
		spin_lock_irq(q->queue_lock);
		where = ELEVATOR_INSERT_FLUSH;
		goto get_rq;
	}

	/*
	 * Check if we can merge with the plugged list before grabbing
	 * any locks.
	 */
	if (attempt_plug_merge(q, bio, &request_count))
		return;

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
	req = get_request_wait(q, rw_flags, bio);
	if (unlikely(!req)) {
		bio_endio(bio, -ENODEV);	/* @q is dead */
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
		 * of a plug trace. If others have been added before, check
		 * if we have multiple devices in this plug. If so, make a
		 * note to sort the list before dispatch.
		 */
		if (list_empty(&plug->list))
			trace_block_plug(q);
		else {
			if (!plug->should_sort) {
				struct request *__rq;

				__rq = list_entry_rq(plug->list.prev);
				if (__rq->q != q)
					plug->should_sort = 1;
			}
			if (request_count >= BLK_MAX_REQUEST_COUNT) {
				blk_flush_plug_list(plug, false);
				trace_block_plug(q);
			}
		}
		list_add_tail(&req->queuelist, &plug->list);
		drive_stat_acct(req, 1);
	} else {
		spin_lock_irq(q->queue_lock);
		add_acct_request(q, req, where);
		__blk_run_queue(q);
out_unlock:
		spin_unlock_irq(q->queue_lock);
	}
}
EXPORT_SYMBOL_GPL(blk_queue_bio);	/* for device mapper only */

/*
 * If bio->bi_dev is a partition, remap the location
 */
static inline void blk_partition_remap(struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;

	if (bio_sectors(bio) && bdev != bdev->bd_contains) {
		struct hd_struct *p = bdev->bd_part;

		bio->bi_sector += p->start_sect;
		bio->bi_bdev = bdev->bd_contains;

		trace_block_bio_remap(bdev_get_queue(bio->bi_bdev), bio,
				      bdev->bd_dev,
				      bio->bi_sector - p->start_sect);
	}
}

static void handle_bad_sector(struct bio *bio)
{
	char b[BDEVNAME_SIZE];

	printk(KERN_INFO "attempt to access beyond end of device\n");
	printk(KERN_INFO "%s: rw=%ld, want=%Lu, limit=%Lu\n",
			bdevname(bio->bi_bdev, b),
			bio->bi_rw,
			(unsigned long long)bio->bi_sector + bio_sectors(bio),
			(long long)(i_size_read(bio->bi_bdev->bd_inode) >> 9));

	set_bit(BIO_EOF, &bio->bi_flags);
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

	return IS_ERR(dir) ? PTR_ERR(dir) : 0;
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
		sector_t sector = bio->bi_sector;

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
			(long long) bio->bi_sector);
		goto end_io;
	}

	if (unlikely(!(bio->bi_rw & REQ_DISCARD) &&
		     nr_sectors > queue_max_hw_sectors(q))) {
		printk(KERN_ERR "bio too big device %s (%u > %u)\n",
		       bdevname(bio->bi_bdev, b),
		       bio_sectors(bio),
		       queue_max_hw_sectors(q));
		goto end_io;
	}

	part = bio->bi_bdev->bd_part;
	if (should_fail_request(part, bio->bi_size) ||
	    should_fail_request(&part_to_disk(part)->part0,
				bio->bi_size))
		goto end_io;

	/*
	 * If this device has partitions, remap block n
	 * of partition p to block n+start(p) of the disk.
	 */
	blk_partition_remap(bio);

	if (bio_integrity_enabled(bio) && bio_integrity_prep(bio))
		goto end_io;

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
	     ((bio->bi_rw & REQ_SECURE) &&
	      !blk_queue_secdiscard(q)))) {
		err = -EOPNOTSUPP;
		goto end_io;
	}

	if (blk_throtl_bio(q, bio))
		return false;	/* throttled, will be resubmitted later */

	trace_block_bio_queue(q, bio);
	return true;

end_io:
	bio_endio(bio, err);
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
void generic_make_request(struct bio *bio)
{
	struct bio_list bio_list_on_stack;

	if (!generic_make_request_checks(bio))
		return;

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
		bio_list_add(current->bio_list, bio);
		return;
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
	bio_list_init(&bio_list_on_stack);
	current->bio_list = &bio_list_on_stack;
	do {
		struct request_queue *q = bdev_get_queue(bio->bi_bdev);

		q->make_request_fn(q, bio);

		bio = bio_list_pop(current->bio_list);
	} while (bio);
	current->bio_list = NULL; /* deactivate */
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
void submit_bio(int rw, struct bio *bio)
{
	int count = bio_sectors(bio);

	bio->bi_rw |= rw;

	/*
	 * If it's a regular read/write or a barrier with data attached,
	 * go through the normal accounting stuff before submission.
	 */
	if (bio_has_data(bio) && !(rw & REQ_DISCARD)) {
		if (rw & WRITE) {
			count_vm_events(PGPGOUT, count);
		} else {
			task_io_account_read(bio->bi_size);
			count_vm_events(PGPGIN, count);
		}

		if (unlikely(block_dump)) {
			char b[BDEVNAME_SIZE];
			printk(KERN_DEBUG "%s(%d): %s block %Lu on %s (%u sectors)\n",
			current->comm, task_pid_nr(current),
				(rw & WRITE) ? "WRITE" : "READ",
				(unsigned long long)bio->bi_sector,
				bdevname(bio->bi_bdev, b),
				count);
		}
	}

	generic_make_request(bio);
}
EXPORT_SYMBOL(submit_bio);

/**
 * blk_rq_check_limits - Helper function to check a request for the queue limit
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
 *    This function should also be useful for request stacking drivers
 *    in some cases below, so export this function.
 *    Request stacking drivers like request-based dm may change the queue
 *    limits while requests are in the queue (e.g. dm's table swapping).
 *    Such request stacking drivers should check those requests agaist
 *    the new queue limits again when they dispatch those requests,
 *    although such checkings are also done against the old queue limits
 *    when submitting requests.
 */
int blk_rq_check_limits(struct request_queue *q, struct request *rq)
{
	if (rq->cmd_flags & REQ_DISCARD)
		return 0;

	if (blk_rq_sectors(rq) > queue_max_sectors(q) ||
	    blk_rq_bytes(rq) > queue_max_hw_sectors(q) << 9) {
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
EXPORT_SYMBOL_GPL(blk_rq_check_limits);

/**
 * blk_insert_cloned_request - Helper for stacking drivers to submit a request
 * @q:  the queue to submit the request
 * @rq: the request being queued
 */
int blk_insert_cloned_request(struct request_queue *q, struct request *rq)
{
	unsigned long flags;
	int where = ELEVATOR_INSERT_BACK;

	if (blk_rq_check_limits(q, rq))
		return -EIO;

	if (rq->rq_disk &&
	    should_fail_request(&rq->rq_disk->part0, blk_rq_bytes(rq)))
		return -EIO;

	spin_lock_irqsave(q->queue_lock, flags);
	if (unlikely(blk_queue_dead(q))) {
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
		bytes += bio->bi_size;
	}

	/* this could lead to infinite loop */
	BUG_ON(blk_rq_bytes(rq) && !bytes);
	return bytes;
}
EXPORT_SYMBOL_GPL(blk_rq_err_bytes);

static void blk_account_io_completion(struct request *req, unsigned int bytes)
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

static void blk_account_io_done(struct request *req)
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
	int total_bytes, bio_nbytes, next_idx = 0;
	struct bio *bio;

	if (!req->bio)
		return false;

	trace_block_rq_complete(req->q, req);

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
		case -EIO:
		default:
			error_type = "I/O";
			break;
		}
		printk(KERN_ERR "end_request: %s error, dev %s, sector %llu\n",
		       error_type, req->rq_disk ? req->rq_disk->disk_name : "?",
		       (unsigned long long)blk_rq_pos(req));
	}

	blk_account_io_completion(req, nr_bytes);

	total_bytes = bio_nbytes = 0;
	while ((bio = req->bio) != NULL) {
		int nbytes;

		if (nr_bytes >= bio->bi_size) {
			req->bio = bio->bi_next;
			nbytes = bio->bi_size;
			req_bio_endio(req, bio, nbytes, error);
			next_idx = 0;
			bio_nbytes = 0;
		} else {
			int idx = bio->bi_idx + next_idx;

			if (unlikely(idx >= bio->bi_vcnt)) {
				blk_dump_rq_flags(req, "__end_that");
				printk(KERN_ERR "%s: bio idx %d >= vcnt %d\n",
				       __func__, idx, bio->bi_vcnt);
				break;
			}

			nbytes = bio_iovec_idx(bio, idx)->bv_len;
			BIO_BUG_ON(nbytes > bio->bi_size);

			/*
			 * not a complete bvec done
			 */
			if (unlikely(nbytes > nr_bytes)) {
				bio_nbytes += nr_bytes;
				total_bytes += nr_bytes;
				break;
			}

			/*
			 * advance to the next vector
			 */
			next_idx++;
			bio_nbytes += nbytes;
		}

		total_bytes += nbytes;
		nr_bytes -= nbytes;

		bio = req->bio;
		if (bio) {
			/*
			 * end more in this run, or just return 'not-done'
			 */
			if (unlikely(nr_bytes <= 0))
				break;
		}
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

	/*
	 * if the request wasn't completed, update state
	 */
	if (bio_nbytes) {
		req_bio_endio(req, bio, bio_nbytes, error);
		bio->bi_idx += next_idx;
		bio_iovec(bio)->bv_offset += nr_bytes;
		bio_iovec(bio)->bv_len -= nr_bytes;
	}

	req->__data_len -= total_bytes;
	req->buffer = bio_data(req->bio);

	/* update sector only for requests with clear definition of sector */
	if (req->cmd_type == REQ_TYPE_FS || (req->cmd_flags & REQ_DISCARD))
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
static void blk_finish_request(struct request *req, int error)
{
	if (blk_rq_tagged(req))
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

	if (bio_has_data(bio)) {
		rq->nr_phys_segments = bio_phys_segments(q, bio);
		rq->buffer = bio_data(bio);
	}
	rq->__data_len = bio->bi_size;
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
	struct bio_vec *bvec;

	rq_for_each_segment(bvec, rq, iter)
		flush_dcache_page(bvec->bv_page);
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
 * The actual data parts (e.g. ->cmd, ->buffer, ->sense) are not copied.
 */
static void __blk_rq_prep_clone(struct request *dst, struct request *src)
{
	dst->cpu = src->cpu;
	dst->cmd_flags = (src->cmd_flags & REQ_CLONE_MASK) | REQ_NOMERGE;
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
 *     The actual data parts of @rq_src (e.g. ->cmd, ->buffer, ->sense)
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

	blk_rq_init(NULL, rq);

	__rq_for_each_bio(bio_src, rq_src) {
		bio = bio_alloc_bioset(gfp_mask, bio_src->bi_max_vecs, bs);
		if (!bio)
			goto free_and_out;

		__bio_clone(bio, bio_src);

		if (bio_integrity(bio_src) &&
		    bio_integrity_clone(bio, bio_src, gfp_mask, bs))
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
		bio_free(bio, bs);
	blk_rq_unprep_clone(rq);

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(blk_rq_prep_clone);

int kblockd_schedule_work(struct request_queue *q, struct work_struct *work)
{
	return queue_work(kblockd_workqueue, work);
}
EXPORT_SYMBOL(kblockd_schedule_work);

int kblockd_schedule_delayed_work(struct request_queue *q,
			struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work(kblockd_workqueue, dwork, delay);
}
EXPORT_SYMBOL(kblockd_schedule_delayed_work);

#define PLUG_MAGIC	0x91827364

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

	plug->magic = PLUG_MAGIC;
	INIT_LIST_HEAD(&plug->list);
	INIT_LIST_HEAD(&plug->cb_list);
	plug->should_sort = 0;

	/*
	 * If this is a nested plug, don't actually assign it. It will be
	 * flushed on its own.
	 */
	if (!tsk->plug) {
		/*
		 * Store ordering should not be needed here, since a potential
		 * preempt will imply a full memory barrier
		 */
		tsk->plug = plug;
	}
}
EXPORT_SYMBOL(blk_start_plug);

static int plug_rq_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct request *rqa = container_of(a, struct request, queuelist);
	struct request *rqb = container_of(b, struct request, queuelist);

	return !(rqa->q <= rqb->q);
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

	/*
	 * Don't mess with dead queue.
	 */
	if (unlikely(blk_queue_dead(q))) {
		spin_unlock(q->queue_lock);
		return;
	}

	/*
	 * If we are punting this to kblockd, then we can safely drop
	 * the queue_lock before waking kblockd (which needs to take
	 * this lock).
	 */
	if (from_schedule) {
		spin_unlock(q->queue_lock);
		blk_run_queue_async(q);
	} else {
		__blk_run_queue(q);
		spin_unlock(q->queue_lock);
	}

}

static void flush_plug_callbacks(struct blk_plug *plug)
{
	LIST_HEAD(callbacks);

	if (list_empty(&plug->cb_list))
		return;

	list_splice_init(&plug->cb_list, &callbacks);

	while (!list_empty(&callbacks)) {
		struct blk_plug_cb *cb = list_first_entry(&callbacks,
							  struct blk_plug_cb,
							  list);
		list_del(&cb->list);
		cb->callback(cb);
	}
}

void blk_flush_plug_list(struct blk_plug *plug, bool from_schedule)
{
	struct request_queue *q;
	unsigned long flags;
	struct request *rq;
	LIST_HEAD(list);
	unsigned int depth;

	BUG_ON(plug->magic != PLUG_MAGIC);

	flush_plug_callbacks(plug);
	if (list_empty(&plug->list))
		return;

	list_splice_init(&plug->list, &list);

	if (plug->should_sort) {
		list_sort(NULL, &list, plug_rq_cmp);
		plug->should_sort = 0;
	}

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
		if (unlikely(blk_queue_dead(q))) {
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
	blk_flush_plug_list(plug, false);

	if (plug == current->plug)
		current->plug = NULL;
}
EXPORT_SYMBOL(blk_finish_plug);

int __init blk_dev_init(void)
{
	BUILD_BUG_ON(__REQ_NR_BITS > 8 *
			sizeof(((struct request *)0)->cmd_flags));

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
