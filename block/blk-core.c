// SPDX-License-Identifier: GPL-2.0
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
#include <linux/pagemap.h>
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
#include <linux/t10-pi.h>
#include <linux/debugfs.h>
#include <linux/bpf.h>
#include <linux/psi.h>
#include <linux/sched/sysctl.h>
#include <linux/blk-crypto.h>

#define CREATE_TRACE_POINTS
#include <trace/events/block.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-sched.h"
#include "blk-pm.h"
#include "blk-rq-qos.h"

struct dentry *blk_debugfs_root;

EXPORT_TRACEPOINT_SYMBOL_GPL(block_bio_remap);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_remap);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_bio_complete);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_split);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_unplug);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_bio_queue);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_getrq);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_insert);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_issue);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_merge);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_requeue);
EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_complete);

DEFINE_IDA(blk_queue_ida);

/*
 * For queue allocation
 */
struct kmem_cache *blk_requestq_cachep;

/*
 * Controlling structure to kblockd
 */
static struct workqueue_struct *kblockd_workqueue;

/**
 * blk_queue_flag_set - atomically set a queue flag
 * @flag: flag to be set
 * @q: request queue
 */
void blk_queue_flag_set(unsigned int flag, struct request_queue *q)
{
	set_bit(flag, &q->queue_flags);
}
EXPORT_SYMBOL(blk_queue_flag_set);

/**
 * blk_queue_flag_clear - atomically clear a queue flag
 * @flag: flag to be cleared
 * @q: request queue
 */
void blk_queue_flag_clear(unsigned int flag, struct request_queue *q)
{
	clear_bit(flag, &q->queue_flags);
}
EXPORT_SYMBOL(blk_queue_flag_clear);

/**
 * blk_queue_flag_test_and_set - atomically test and set a queue flag
 * @flag: flag to be set
 * @q: request queue
 *
 * Returns the previous value of @flag - 0 if the flag was not set and 1 if
 * the flag was already set.
 */
bool blk_queue_flag_test_and_set(unsigned int flag, struct request_queue *q)
{
	return test_and_set_bit(flag, &q->queue_flags);
}
EXPORT_SYMBOL_GPL(blk_queue_flag_test_and_set);

void blk_rq_init(struct request_queue *q, struct request *rq)
{
	memset(rq, 0, sizeof(*rq));

	INIT_LIST_HEAD(&rq->queuelist);
	rq->q = q;
	rq->__sector = (sector_t) -1;
	INIT_HLIST_NODE(&rq->hash);
	RB_CLEAR_NODE(&rq->rb_node);
	rq->tag = BLK_MQ_NO_TAG;
	rq->internal_tag = BLK_MQ_NO_TAG;
	rq->start_time_ns = ktime_get_ns();
	rq->part = NULL;
	blk_crypto_rq_set_defaults(rq);
}
EXPORT_SYMBOL(blk_rq_init);

#define REQ_OP_NAME(name) [REQ_OP_##name] = #name
static const char *const blk_op_name[] = {
	REQ_OP_NAME(READ),
	REQ_OP_NAME(WRITE),
	REQ_OP_NAME(FLUSH),
	REQ_OP_NAME(DISCARD),
	REQ_OP_NAME(SECURE_ERASE),
	REQ_OP_NAME(ZONE_RESET),
	REQ_OP_NAME(ZONE_RESET_ALL),
	REQ_OP_NAME(ZONE_OPEN),
	REQ_OP_NAME(ZONE_CLOSE),
	REQ_OP_NAME(ZONE_FINISH),
	REQ_OP_NAME(ZONE_APPEND),
	REQ_OP_NAME(WRITE_SAME),
	REQ_OP_NAME(WRITE_ZEROES),
	REQ_OP_NAME(SCSI_IN),
	REQ_OP_NAME(SCSI_OUT),
	REQ_OP_NAME(DRV_IN),
	REQ_OP_NAME(DRV_OUT),
};
#undef REQ_OP_NAME

/**
 * blk_op_str - Return string XXX in the REQ_OP_XXX.
 * @op: REQ_OP_XXX.
 *
 * Description: Centralize block layer function to convert REQ_OP_XXX into
 * string format. Useful in the debugging and tracing bio or request. For
 * invalid REQ_OP_XXX it returns string "UNKNOWN".
 */
inline const char *blk_op_str(unsigned int op)
{
	const char *op_str = "UNKNOWN";

	if (op < ARRAY_SIZE(blk_op_name) && blk_op_name[op])
		op_str = blk_op_name[op];

	return op_str;
}
EXPORT_SYMBOL_GPL(blk_op_str);

static const struct {
	int		errno;
	const char	*name;
} blk_errors[] = {
	[BLK_STS_OK]		= { 0,		"" },
	[BLK_STS_NOTSUPP]	= { -EOPNOTSUPP, "operation not supported" },
	[BLK_STS_TIMEOUT]	= { -ETIMEDOUT,	"timeout" },
	[BLK_STS_NOSPC]		= { -ENOSPC,	"critical space allocation" },
	[BLK_STS_TRANSPORT]	= { -ENOLINK,	"recoverable transport" },
	[BLK_STS_TARGET]	= { -EREMOTEIO,	"critical target" },
	[BLK_STS_NEXUS]		= { -EBADE,	"critical nexus" },
	[BLK_STS_MEDIUM]	= { -ENODATA,	"critical medium" },
	[BLK_STS_PROTECTION]	= { -EILSEQ,	"protection" },
	[BLK_STS_RESOURCE]	= { -ENOMEM,	"kernel resource" },
	[BLK_STS_DEV_RESOURCE]	= { -EBUSY,	"device resource" },
	[BLK_STS_AGAIN]		= { -EAGAIN,	"nonblocking retry" },

	/* device mapper special case, should not leak out: */
	[BLK_STS_DM_REQUEUE]	= { -EREMCHG, "dm internal retry" },

	/* zone device specific errors */
	[BLK_STS_ZONE_OPEN_RESOURCE]	= { -ETOOMANYREFS, "open zones exceeded" },
	[BLK_STS_ZONE_ACTIVE_RESOURCE]	= { -EOVERFLOW, "active zones exceeded" },

	/* everything else not covered above: */
	[BLK_STS_IOERR]		= { -EIO,	"I/O" },
};

blk_status_t errno_to_blk_status(int errno)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(blk_errors); i++) {
		if (blk_errors[i].errno == errno)
			return (__force blk_status_t)i;
	}

	return BLK_STS_IOERR;
}
EXPORT_SYMBOL_GPL(errno_to_blk_status);

int blk_status_to_errno(blk_status_t status)
{
	int idx = (__force int)status;

	if (WARN_ON_ONCE(idx >= ARRAY_SIZE(blk_errors)))
		return -EIO;
	return blk_errors[idx].errno;
}
EXPORT_SYMBOL_GPL(blk_status_to_errno);

static void print_req_error(struct request *req, blk_status_t status,
		const char *caller)
{
	int idx = (__force int)status;

	if (WARN_ON_ONCE(idx >= ARRAY_SIZE(blk_errors)))
		return;

	printk_ratelimited(KERN_ERR
		"%s: %s error, dev %s, sector %llu op 0x%x:(%s) flags 0x%x "
		"phys_seg %u prio class %u\n",
		caller, blk_errors[idx].name,
		req->rq_disk ? req->rq_disk->disk_name : "?",
		blk_rq_pos(req), req_op(req), blk_op_str(req_op(req)),
		req->cmd_flags & ~REQ_OP_MASK,
		req->nr_phys_segments,
		IOPRIO_PRIO_CLASS(req->ioprio));
}

static void req_bio_endio(struct request *rq, struct bio *bio,
			  unsigned int nbytes, blk_status_t error)
{
	if (error)
		bio->bi_status = error;

	if (unlikely(rq->rq_flags & RQF_QUIET))
		bio_set_flag(bio, BIO_QUIET);

	bio_advance(bio, nbytes);

	if (req_op(rq) == REQ_OP_ZONE_APPEND && error == BLK_STS_OK) {
		/*
		 * Partial zone append completions cannot be supported as the
		 * BIO fragments may end up not being written sequentially.
		 */
		if (bio->bi_iter.bi_size)
			bio->bi_status = BLK_STS_IOERR;
		else
			bio->bi_iter.bi_sector = rq->__sector;
	}

	/* don't actually finish bio if it's part of flush sequence */
	if (bio->bi_iter.bi_size == 0 && !(rq->rq_flags & RQF_FLUSH_SEQ))
		bio_endio(bio);
}

void blk_dump_rq_flags(struct request *rq, char *msg)
{
	printk(KERN_INFO "%s: dev %s: flags=%llx\n", msg,
		rq->rq_disk ? rq->rq_disk->disk_name : "?",
		(unsigned long long) rq->cmd_flags);

	printk(KERN_INFO "  sector %llu, nr/cnr %u/%u\n",
	       (unsigned long long)blk_rq_pos(rq),
	       blk_rq_sectors(rq), blk_rq_cur_sectors(rq));
	printk(KERN_INFO "  bio %p, biotail %p, len %u\n",
	       rq->bio, rq->biotail, blk_rq_bytes(rq));
}
EXPORT_SYMBOL(blk_dump_rq_flags);

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
 *     that its ->submit_bio will not re-add plugging prior to calling
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
	cancel_work_sync(&q->timeout_work);
}
EXPORT_SYMBOL(blk_sync_queue);

/**
 * blk_set_pm_only - increment pm_only counter
 * @q: request queue pointer
 */
void blk_set_pm_only(struct request_queue *q)
{
	atomic_inc(&q->pm_only);
}
EXPORT_SYMBOL_GPL(blk_set_pm_only);

void blk_clear_pm_only(struct request_queue *q)
{
	int pm_only;

	pm_only = atomic_dec_return(&q->pm_only);
	WARN_ON_ONCE(pm_only < 0);
	if (pm_only == 0)
		wake_up_all(&q->mq_freeze_wq);
}
EXPORT_SYMBOL_GPL(blk_clear_pm_only);

/**
 * blk_put_queue - decrement the request_queue refcount
 * @q: the request_queue structure to decrement the refcount for
 *
 * Decrements the refcount of the request_queue kobject. When this reaches 0
 * we'll have blk_release_queue() called.
 *
 * Context: Any context, but the last reference must not be dropped from
 *          atomic context.
 */
void blk_put_queue(struct request_queue *q)
{
	kobject_put(&q->kobj);
}
EXPORT_SYMBOL(blk_put_queue);

void blk_set_queue_dying(struct request_queue *q)
{
	blk_queue_flag_set(QUEUE_FLAG_DYING, q);

	/*
	 * When queue DYING flag is set, we need to block new req
	 * entering queue, so we call blk_freeze_queue_start() to
	 * prevent I/O from crossing blk_queue_enter().
	 */
	blk_freeze_queue_start(q);

	if (queue_is_mq(q))
		blk_mq_wake_waiters(q);

	/* Make blk_queue_enter() reexamine the DYING flag. */
	wake_up_all(&q->mq_freeze_wq);
}
EXPORT_SYMBOL_GPL(blk_set_queue_dying);

/**
 * blk_cleanup_queue - shutdown a request queue
 * @q: request queue to shutdown
 *
 * Mark @q DYING, drain all pending requests, mark @q DEAD, destroy and
 * put it.  All future requests will be failed immediately with -ENODEV.
 *
 * Context: can sleep
 */
void blk_cleanup_queue(struct request_queue *q)
{
	/* cannot be called from atomic context */
	might_sleep();

	WARN_ON_ONCE(blk_queue_registered(q));

	/* mark @q DYING, no new request or merges will be allowed afterwards */
	blk_set_queue_dying(q);

	blk_queue_flag_set(QUEUE_FLAG_NOMERGES, q);
	blk_queue_flag_set(QUEUE_FLAG_NOXMERGES, q);

	/*
	 * Drain all requests queued before DYING marking. Set DEAD flag to
	 * prevent that blk_mq_run_hw_queues() accesses the hardware queues
	 * after draining finished.
	 */
	blk_freeze_queue(q);

	rq_qos_exit(q);

	blk_queue_flag_set(QUEUE_FLAG_DEAD, q);

	/* for synchronous bio-based driver finish in-flight integrity i/o */
	blk_flush_integrity();

	/* @q won't process any more request, flush async actions */
	del_timer_sync(&q->backing_dev_info->laptop_mode_wb_timer);
	blk_sync_queue(q);

	if (queue_is_mq(q))
		blk_mq_exit_queue(q);

	/*
	 * In theory, request pool of sched_tags belongs to request queue.
	 * However, the current implementation requires tag_set for freeing
	 * requests, so free the pool now.
	 *
	 * Queue has become frozen, there can't be any in-queue requests, so
	 * it is safe to free requests now.
	 */
	mutex_lock(&q->sysfs_lock);
	if (q->elevator)
		blk_mq_sched_free_requests(q);
	mutex_unlock(&q->sysfs_lock);

	percpu_ref_exit(&q->q_usage_counter);

	/* @q is and will stay empty, shutdown and put */
	blk_put_queue(q);
}
EXPORT_SYMBOL(blk_cleanup_queue);

/**
 * blk_queue_enter() - try to increase q->q_usage_counter
 * @q: request queue pointer
 * @flags: BLK_MQ_REQ_NOWAIT and/or BLK_MQ_REQ_PM
 */
int blk_queue_enter(struct request_queue *q, blk_mq_req_flags_t flags)
{
	const bool pm = flags & BLK_MQ_REQ_PM;

	while (true) {
		bool success = false;

		rcu_read_lock();
		if (percpu_ref_tryget_live(&q->q_usage_counter)) {
			/*
			 * The code that increments the pm_only counter is
			 * responsible for ensuring that that counter is
			 * globally visible before the queue is unfrozen.
			 */
			if (pm || !blk_queue_pm_only(q)) {
				success = true;
			} else {
				percpu_ref_put(&q->q_usage_counter);
			}
		}
		rcu_read_unlock();

		if (success)
			return 0;

		if (flags & BLK_MQ_REQ_NOWAIT)
			return -EBUSY;

		/*
		 * read pair of barrier in blk_freeze_queue_start(),
		 * we need to order reading __PERCPU_REF_DEAD flag of
		 * .q_usage_counter and reading .mq_freeze_depth or
		 * queue dying flag, otherwise the following wait may
		 * never return if the two reads are reordered.
		 */
		smp_rmb();

		wait_event(q->mq_freeze_wq,
			   (!q->mq_freeze_depth &&
			    (pm || (blk_pm_request_resume(q),
				    !blk_queue_pm_only(q)))) ||
			   blk_queue_dying(q));
		if (blk_queue_dying(q))
			return -ENODEV;
	}
}

static inline int bio_queue_enter(struct bio *bio)
{
	struct request_queue *q = bio->bi_disk->queue;
	bool nowait = bio->bi_opf & REQ_NOWAIT;
	int ret;

	ret = blk_queue_enter(q, nowait ? BLK_MQ_REQ_NOWAIT : 0);
	if (unlikely(ret)) {
		if (nowait && !blk_queue_dying(q))
			bio_wouldblock_error(bio);
		else
			bio_io_error(bio);
	}

	return ret;
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

static void blk_rq_timed_out_timer(struct timer_list *t)
{
	struct request_queue *q = from_timer(q, t, timeout);

	kblockd_schedule_work(&q->timeout_work);
}

static void blk_timeout_work(struct work_struct *work)
{
}

struct request_queue *blk_alloc_queue(int node_id)
{
	struct request_queue *q;
	int ret;

	q = kmem_cache_alloc_node(blk_requestq_cachep,
				GFP_KERNEL | __GFP_ZERO, node_id);
	if (!q)
		return NULL;

	q->last_merge = NULL;

	q->id = ida_simple_get(&blk_queue_ida, 0, 0, GFP_KERNEL);
	if (q->id < 0)
		goto fail_q;

	ret = bioset_init(&q->bio_split, BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (ret)
		goto fail_id;

	q->backing_dev_info = bdi_alloc(node_id);
	if (!q->backing_dev_info)
		goto fail_split;

	q->stats = blk_alloc_queue_stats();
	if (!q->stats)
		goto fail_stats;

	q->node = node_id;

	atomic_set(&q->nr_active_requests_shared_sbitmap, 0);

	timer_setup(&q->backing_dev_info->laptop_mode_wb_timer,
		    laptop_mode_timer_fn, 0);
	timer_setup(&q->timeout, blk_rq_timed_out_timer, 0);
	INIT_WORK(&q->timeout_work, blk_timeout_work);
	INIT_LIST_HEAD(&q->icq_list);
#ifdef CONFIG_BLK_CGROUP
	INIT_LIST_HEAD(&q->blkg_list);
#endif

	kobject_init(&q->kobj, &blk_queue_ktype);

	mutex_init(&q->debugfs_mutex);
	mutex_init(&q->sysfs_lock);
	mutex_init(&q->sysfs_dir_lock);
	spin_lock_init(&q->queue_lock);

	init_waitqueue_head(&q->mq_freeze_wq);
	mutex_init(&q->mq_freeze_lock);

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

	blk_queue_dma_alignment(q, 511);
	blk_set_default_limits(&q->limits);
	q->nr_requests = BLKDEV_MAX_RQ;

	return q;

fail_ref:
	percpu_ref_exit(&q->q_usage_counter);
fail_bdi:
	blk_free_queue_stats(q->stats);
fail_stats:
	bdi_put(q->backing_dev_info);
fail_split:
	bioset_exit(&q->bio_split);
fail_id:
	ida_simple_remove(&blk_queue_ida, q->id);
fail_q:
	kmem_cache_free(blk_requestq_cachep, q);
	return NULL;
}
EXPORT_SYMBOL(blk_alloc_queue);

/**
 * blk_get_queue - increment the request_queue refcount
 * @q: the request_queue structure to increment the refcount for
 *
 * Increment the refcount of the request_queue kobject.
 *
 * Context: Any context.
 */
bool blk_get_queue(struct request_queue *q)
{
	if (likely(!blk_queue_dying(q))) {
		__blk_get_queue(q);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(blk_get_queue);

/**
 * blk_get_request - allocate a request
 * @q: request queue to allocate a request for
 * @op: operation (REQ_OP_*) and REQ_* flags, e.g. REQ_SYNC.
 * @flags: BLK_MQ_REQ_* flags, e.g. BLK_MQ_REQ_NOWAIT.
 */
struct request *blk_get_request(struct request_queue *q, unsigned int op,
				blk_mq_req_flags_t flags)
{
	struct request *req;

	WARN_ON_ONCE(op & REQ_NOWAIT);
	WARN_ON_ONCE(flags & ~(BLK_MQ_REQ_NOWAIT | BLK_MQ_REQ_PM));

	req = blk_mq_alloc_request(q, op, flags);
	if (!IS_ERR(req) && q->mq_ops->initialize_rq_fn)
		q->mq_ops->initialize_rq_fn(req);

	return req;
}
EXPORT_SYMBOL(blk_get_request);

void blk_put_request(struct request *req)
{
	blk_mq_free_request(req);
}
EXPORT_SYMBOL(blk_put_request);

static void handle_bad_sector(struct bio *bio, sector_t maxsector)
{
	char b[BDEVNAME_SIZE];

	pr_info_ratelimited("attempt to access beyond end of device\n"
			    "%s: rw=%d, want=%llu, limit=%llu\n",
			    bio_devname(bio, b), bio->bi_opf,
			    bio_end_sector(bio), maxsector);
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

static inline bool bio_check_ro(struct bio *bio, struct hd_struct *part)
{
	const int op = bio_op(bio);

	if (part->policy && op_is_write(op)) {
		char b[BDEVNAME_SIZE];

		if (op_is_flush(bio->bi_opf) && !bio_sectors(bio))
			return false;

		WARN_ONCE(1,
		       "Trying to write to read-only block-device %s (partno %d)\n",
			bio_devname(bio, b), part->partno);
		/* Older lvm-tools actually trigger this */
		return false;
	}

	return false;
}

static noinline int should_fail_bio(struct bio *bio)
{
	if (should_fail_request(&bio->bi_disk->part0, bio->bi_iter.bi_size))
		return -EIO;
	return 0;
}
ALLOW_ERROR_INJECTION(should_fail_bio, ERRNO);

/*
 * Check whether this bio extends beyond the end of the device or partition.
 * This may well happen - the kernel calls bread() without checking the size of
 * the device, e.g., when mounting a file system.
 */
static inline int bio_check_eod(struct bio *bio, sector_t maxsector)
{
	unsigned int nr_sectors = bio_sectors(bio);

	if (nr_sectors && maxsector &&
	    (nr_sectors > maxsector ||
	     bio->bi_iter.bi_sector > maxsector - nr_sectors)) {
		handle_bad_sector(bio, maxsector);
		return -EIO;
	}
	return 0;
}

/*
 * Remap block n of partition p to block n+start(p) of the disk.
 */
static inline int blk_partition_remap(struct bio *bio)
{
	struct hd_struct *p;
	int ret = -EIO;

	rcu_read_lock();
	p = __disk_get_part(bio->bi_disk, bio->bi_partno);
	if (unlikely(!p))
		goto out;
	if (unlikely(should_fail_request(p, bio->bi_iter.bi_size)))
		goto out;
	if (unlikely(bio_check_ro(bio, p)))
		goto out;

	if (bio_sectors(bio)) {
		if (bio_check_eod(bio, part_nr_sects_read(p)))
			goto out;
		bio->bi_iter.bi_sector += p->start_sect;
		trace_block_bio_remap(bio->bi_disk->queue, bio, part_devt(p),
				      bio->bi_iter.bi_sector - p->start_sect);
	}
	bio->bi_partno = 0;
	ret = 0;
out:
	rcu_read_unlock();
	return ret;
}

/*
 * Check write append to a zoned block device.
 */
static inline blk_status_t blk_check_zone_append(struct request_queue *q,
						 struct bio *bio)
{
	sector_t pos = bio->bi_iter.bi_sector;
	int nr_sectors = bio_sectors(bio);

	/* Only applicable to zoned block devices */
	if (!blk_queue_is_zoned(q))
		return BLK_STS_NOTSUPP;

	/* The bio sector must point to the start of a sequential zone */
	if (pos & (blk_queue_zone_sectors(q) - 1) ||
	    !blk_queue_zone_is_seq(q, pos))
		return BLK_STS_IOERR;

	/*
	 * Not allowed to cross zone boundaries. Otherwise, the BIO will be
	 * split and could result in non-contiguous sectors being written in
	 * different zones.
	 */
	if (nr_sectors > q->limits.chunk_sectors)
		return BLK_STS_IOERR;

	/* Make sure the BIO is small enough and will not get split */
	if (nr_sectors > q->limits.max_zone_append_sectors)
		return BLK_STS_IOERR;

	bio->bi_opf |= REQ_NOMERGE;

	return BLK_STS_OK;
}

static noinline_for_stack bool submit_bio_checks(struct bio *bio)
{
	struct request_queue *q = bio->bi_disk->queue;
	blk_status_t status = BLK_STS_IOERR;
	struct blk_plug *plug;

	might_sleep();

	plug = blk_mq_plug(q, bio);
	if (plug && plug->nowait)
		bio->bi_opf |= REQ_NOWAIT;

	/*
	 * For a REQ_NOWAIT based request, return -EOPNOTSUPP
	 * if queue does not support NOWAIT.
	 */
	if ((bio->bi_opf & REQ_NOWAIT) && !blk_queue_nowait(q))
		goto not_supported;

	if (should_fail_bio(bio))
		goto end_io;

	if (bio->bi_partno) {
		if (unlikely(blk_partition_remap(bio)))
			goto end_io;
	} else {
		if (unlikely(bio_check_ro(bio, &bio->bi_disk->part0)))
			goto end_io;
		if (unlikely(bio_check_eod(bio, get_capacity(bio->bi_disk))))
			goto end_io;
	}

	/*
	 * Filter flush bio's early so that bio based drivers without flush
	 * support don't have to worry about them.
	 */
	if (op_is_flush(bio->bi_opf) &&
	    !test_bit(QUEUE_FLAG_WC, &q->queue_flags)) {
		bio->bi_opf &= ~(REQ_PREFLUSH | REQ_FUA);
		if (!bio_sectors(bio)) {
			status = BLK_STS_OK;
			goto end_io;
		}
	}

	if (!test_bit(QUEUE_FLAG_POLL, &q->queue_flags))
		bio->bi_opf &= ~REQ_HIPRI;

	switch (bio_op(bio)) {
	case REQ_OP_DISCARD:
		if (!blk_queue_discard(q))
			goto not_supported;
		break;
	case REQ_OP_SECURE_ERASE:
		if (!blk_queue_secure_erase(q))
			goto not_supported;
		break;
	case REQ_OP_WRITE_SAME:
		if (!q->limits.max_write_same_sectors)
			goto not_supported;
		break;
	case REQ_OP_ZONE_APPEND:
		status = blk_check_zone_append(q, bio);
		if (status != BLK_STS_OK)
			goto end_io;
		break;
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_OPEN:
	case REQ_OP_ZONE_CLOSE:
	case REQ_OP_ZONE_FINISH:
		if (!blk_queue_is_zoned(q))
			goto not_supported;
		break;
	case REQ_OP_ZONE_RESET_ALL:
		if (!blk_queue_is_zoned(q) || !blk_queue_zone_resetall(q))
			goto not_supported;
		break;
	case REQ_OP_WRITE_ZEROES:
		if (!q->limits.max_write_zeroes_sectors)
			goto not_supported;
		break;
	default:
		break;
	}

	/*
	 * Various block parts want %current->io_context, so allocate it up
	 * front rather than dealing with lots of pain to allocate it only
	 * where needed. This may fail and the block layer knows how to live
	 * with it.
	 */
	if (unlikely(!current->io_context))
		create_task_io_context(current, GFP_ATOMIC, q->node);

	if (blk_throtl_bio(bio))
		return false;

	blk_cgroup_bio_start(bio);
	blkcg_bio_issue_init(bio);

	if (!bio_flagged(bio, BIO_TRACE_COMPLETION)) {
		trace_block_bio_queue(q, bio);
		/* Now that enqueuing has been traced, we need to trace
		 * completion as well.
		 */
		bio_set_flag(bio, BIO_TRACE_COMPLETION);
	}
	return true;

not_supported:
	status = BLK_STS_NOTSUPP;
end_io:
	bio->bi_status = status;
	bio_endio(bio);
	return false;
}

static blk_qc_t __submit_bio(struct bio *bio)
{
	struct gendisk *disk = bio->bi_disk;
	blk_qc_t ret = BLK_QC_T_NONE;

	if (blk_crypto_bio_prep(&bio)) {
		if (!disk->fops->submit_bio)
			return blk_mq_submit_bio(bio);
		ret = disk->fops->submit_bio(bio);
	}
	blk_queue_exit(disk->queue);
	return ret;
}

/*
 * The loop in this function may be a bit non-obvious, and so deserves some
 * explanation:
 *
 *  - Before entering the loop, bio->bi_next is NULL (as all callers ensure
 *    that), so we have a list with a single bio.
 *  - We pretend that we have just taken it off a longer list, so we assign
 *    bio_list to a pointer to the bio_list_on_stack, thus initialising the
 *    bio_list of new bios to be added.  ->submit_bio() may indeed add some more
 *    bios through a recursive call to submit_bio_noacct.  If it did, we find a
 *    non-NULL value in bio_list and re-enter the loop from the top.
 *  - In this case we really did just take the bio of the top of the list (no
 *    pretending) and so remove it from bio_list, and call into ->submit_bio()
 *    again.
 *
 * bio_list_on_stack[0] contains bios submitted by the current ->submit_bio.
 * bio_list_on_stack[1] contains bios that were submitted before the current
 *	->submit_bio_bio, but that haven't been processed yet.
 */
static blk_qc_t __submit_bio_noacct(struct bio *bio)
{
	struct bio_list bio_list_on_stack[2];
	blk_qc_t ret = BLK_QC_T_NONE;

	BUG_ON(bio->bi_next);

	bio_list_init(&bio_list_on_stack[0]);
	current->bio_list = bio_list_on_stack;

	do {
		struct request_queue *q = bio->bi_disk->queue;
		struct bio_list lower, same;

		if (unlikely(bio_queue_enter(bio) != 0))
			continue;

		/*
		 * Create a fresh bio_list for all subordinate requests.
		 */
		bio_list_on_stack[1] = bio_list_on_stack[0];
		bio_list_init(&bio_list_on_stack[0]);

		ret = __submit_bio(bio);

		/*
		 * Sort new bios into those for a lower level and those for the
		 * same level.
		 */
		bio_list_init(&lower);
		bio_list_init(&same);
		while ((bio = bio_list_pop(&bio_list_on_stack[0])) != NULL)
			if (q == bio->bi_disk->queue)
				bio_list_add(&same, bio);
			else
				bio_list_add(&lower, bio);

		/*
		 * Now assemble so we handle the lowest level first.
		 */
		bio_list_merge(&bio_list_on_stack[0], &lower);
		bio_list_merge(&bio_list_on_stack[0], &same);
		bio_list_merge(&bio_list_on_stack[0], &bio_list_on_stack[1]);
	} while ((bio = bio_list_pop(&bio_list_on_stack[0])));

	current->bio_list = NULL;
	return ret;
}

static blk_qc_t __submit_bio_noacct_mq(struct bio *bio)
{
	struct bio_list bio_list[2] = { };
	blk_qc_t ret = BLK_QC_T_NONE;

	current->bio_list = bio_list;

	do {
		struct gendisk *disk = bio->bi_disk;

		if (unlikely(bio_queue_enter(bio) != 0))
			continue;

		if (!blk_crypto_bio_prep(&bio)) {
			blk_queue_exit(disk->queue);
			ret = BLK_QC_T_NONE;
			continue;
		}

		ret = blk_mq_submit_bio(bio);
	} while ((bio = bio_list_pop(&bio_list[0])));

	current->bio_list = NULL;
	return ret;
}

/**
 * submit_bio_noacct - re-submit a bio to the block device layer for I/O
 * @bio:  The bio describing the location in memory and on the device.
 *
 * This is a version of submit_bio() that shall only be used for I/O that is
 * resubmitted to lower level drivers by stacking block drivers.  All file
 * systems and other upper level users of the block layer should use
 * submit_bio() instead.
 */
blk_qc_t submit_bio_noacct(struct bio *bio)
{
	if (!submit_bio_checks(bio))
		return BLK_QC_T_NONE;

	/*
	 * We only want one ->submit_bio to be active at a time, else stack
	 * usage with stacked devices could be a problem.  Use current->bio_list
	 * to collect a list of requests submited by a ->submit_bio method while
	 * it is active, and then process them after it returned.
	 */
	if (current->bio_list) {
		bio_list_add(&current->bio_list[0], bio);
		return BLK_QC_T_NONE;
	}

	if (!bio->bi_disk->fops->submit_bio)
		return __submit_bio_noacct_mq(bio);
	return __submit_bio_noacct(bio);
}
EXPORT_SYMBOL(submit_bio_noacct);

/**
 * submit_bio - submit a bio to the block device layer for I/O
 * @bio: The &struct bio which describes the I/O
 *
 * submit_bio() is used to submit I/O requests to block devices.  It is passed a
 * fully set up &struct bio that describes the I/O that needs to be done.  The
 * bio will be send to the device described by the bi_disk and bi_partno fields.
 *
 * The success/failure status of the request, along with notification of
 * completion, is delivered asynchronously through the ->bi_end_io() callback
 * in @bio.  The bio must NOT be touched by thecaller until ->bi_end_io() has
 * been called.
 */
blk_qc_t submit_bio(struct bio *bio)
{
	if (blkcg_punt_bio_submit(bio))
		return BLK_QC_T_NONE;

	/*
	 * If it's a regular read/write or a barrier with data attached,
	 * go through the normal accounting stuff before submission.
	 */
	if (bio_has_data(bio)) {
		unsigned int count;

		if (unlikely(bio_op(bio) == REQ_OP_WRITE_SAME))
			count = queue_logical_block_size(bio->bi_disk->queue) >> 9;
		else
			count = bio_sectors(bio);

		if (op_is_write(bio_op(bio))) {
			count_vm_events(PGPGOUT, count);
		} else {
			task_io_account_read(bio->bi_iter.bi_size);
			count_vm_events(PGPGIN, count);
		}

		if (unlikely(block_dump)) {
			char b[BDEVNAME_SIZE];
			printk(KERN_DEBUG "%s(%d): %s block %Lu on %s (%u sectors)\n",
			current->comm, task_pid_nr(current),
				op_is_write(bio_op(bio)) ? "WRITE" : "READ",
				(unsigned long long)bio->bi_iter.bi_sector,
				bio_devname(bio, b), count);
		}
	}

	/*
	 * If we're reading data that is part of the userspace workingset, count
	 * submission time as memory stall.  When the device is congested, or
	 * the submitting cgroup IO-throttled, submission can be a significant
	 * part of overall IO time.
	 */
	if (unlikely(bio_op(bio) == REQ_OP_READ &&
	    bio_flagged(bio, BIO_WORKINGSET))) {
		unsigned long pflags;
		blk_qc_t ret;

		psi_memstall_enter(&pflags);
		ret = submit_bio_noacct(bio);
		psi_memstall_leave(&pflags);

		return ret;
	}

	return submit_bio_noacct(bio);
}
EXPORT_SYMBOL(submit_bio);

/**
 * blk_cloned_rq_check_limits - Helper function to check a cloned request
 *                              for the new queue limits
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
static blk_status_t blk_cloned_rq_check_limits(struct request_queue *q,
				      struct request *rq)
{
	unsigned int max_sectors = blk_queue_get_max_sectors(q, req_op(rq));

	if (blk_rq_sectors(rq) > max_sectors) {
		/*
		 * SCSI device does not have a good way to return if
		 * Write Same/Zero is actually supported. If a device rejects
		 * a non-read/write command (discard, write same,etc.) the
		 * low-level device driver will set the relevant queue limit to
		 * 0 to prevent blk-lib from issuing more of the offending
		 * operations. Commands queued prior to the queue limit being
		 * reset need to be completed with BLK_STS_NOTSUPP to avoid I/O
		 * errors being propagated to upper layers.
		 */
		if (max_sectors == 0)
			return BLK_STS_NOTSUPP;

		printk(KERN_ERR "%s: over max size limit. (%u > %u)\n",
			__func__, blk_rq_sectors(rq), max_sectors);
		return BLK_STS_IOERR;
	}

	/*
	 * queue's settings related to segment counting like q->bounce_pfn
	 * may differ from that of other stacking queues.
	 * Recalculate it to check the request correctly on this queue's
	 * limitation.
	 */
	rq->nr_phys_segments = blk_recalc_rq_segments(rq);
	if (rq->nr_phys_segments > queue_max_segments(q)) {
		printk(KERN_ERR "%s: over max segments limit. (%hu > %hu)\n",
			__func__, rq->nr_phys_segments, queue_max_segments(q));
		return BLK_STS_IOERR;
	}

	return BLK_STS_OK;
}

/**
 * blk_insert_cloned_request - Helper for stacking drivers to submit a request
 * @q:  the queue to submit the request
 * @rq: the request being queued
 */
blk_status_t blk_insert_cloned_request(struct request_queue *q, struct request *rq)
{
	blk_status_t ret;

	ret = blk_cloned_rq_check_limits(q, rq);
	if (ret != BLK_STS_OK)
		return ret;

	if (rq->rq_disk &&
	    should_fail_request(&rq->rq_disk->part0, blk_rq_bytes(rq)))
		return BLK_STS_IOERR;

	if (blk_crypto_insert_cloned_request(rq))
		return BLK_STS_IOERR;

	if (blk_queue_io_stat(q))
		blk_account_io_start(rq);

	/*
	 * Since we have a scheduler attached on the top device,
	 * bypass a potential scheduler on the bottom device for
	 * insert.
	 */
	return blk_mq_request_issue_directly(rq, true);
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
 */
unsigned int blk_rq_err_bytes(const struct request *rq)
{
	unsigned int ff = rq->cmd_flags & REQ_FAILFAST_MASK;
	unsigned int bytes = 0;
	struct bio *bio;

	if (!(rq->rq_flags & RQF_MIXED_MERGE))
		return blk_rq_bytes(rq);

	/*
	 * Currently the only 'mixing' which can happen is between
	 * different fastfail types.  We can safely fail portions
	 * which have all the failfast bits that the first one has -
	 * the ones which are at least as eager to fail as the first
	 * one.
	 */
	for (bio = rq->bio; bio; bio = bio->bi_next) {
		if ((bio->bi_opf & ff) != ff)
			break;
		bytes += bio->bi_iter.bi_size;
	}

	/* this could lead to infinite loop */
	BUG_ON(blk_rq_bytes(rq) && !bytes);
	return bytes;
}
EXPORT_SYMBOL_GPL(blk_rq_err_bytes);

static void update_io_ticks(struct hd_struct *part, unsigned long now, bool end)
{
	unsigned long stamp;
again:
	stamp = READ_ONCE(part->stamp);
	if (unlikely(stamp != now)) {
		if (likely(cmpxchg(&part->stamp, stamp, now) == stamp))
			__part_stat_add(part, io_ticks, end ? now - stamp : 1);
	}
	if (part->partno) {
		part = &part_to_disk(part)->part0;
		goto again;
	}
}

static void blk_account_io_completion(struct request *req, unsigned int bytes)
{
	if (req->part && blk_do_io_stat(req)) {
		const int sgrp = op_stat_group(req_op(req));
		struct hd_struct *part;

		part_stat_lock();
		part = req->part;
		part_stat_add(part, sectors[sgrp], bytes >> 9);
		part_stat_unlock();
	}
}

void blk_account_io_done(struct request *req, u64 now)
{
	/*
	 * Account IO completion.  flush_rq isn't accounted as a
	 * normal IO on queueing nor completion.  Accounting the
	 * containing request is enough.
	 */
	if (req->part && blk_do_io_stat(req) &&
	    !(req->rq_flags & RQF_FLUSH_SEQ)) {
		const int sgrp = op_stat_group(req_op(req));
		struct hd_struct *part;

		part_stat_lock();
		part = req->part;

		update_io_ticks(part, jiffies, true);
		part_stat_inc(part, ios[sgrp]);
		part_stat_add(part, nsecs[sgrp], now - req->start_time_ns);
		part_stat_unlock();

		hd_struct_put(part);
	}
}

void blk_account_io_start(struct request *rq)
{
	if (!blk_do_io_stat(rq))
		return;

	rq->part = disk_map_sector_rcu(rq->rq_disk, blk_rq_pos(rq));

	part_stat_lock();
	update_io_ticks(rq->part, jiffies, false);
	part_stat_unlock();
}

static unsigned long __part_start_io_acct(struct hd_struct *part,
					  unsigned int sectors, unsigned int op)
{
	const int sgrp = op_stat_group(op);
	unsigned long now = READ_ONCE(jiffies);

	part_stat_lock();
	update_io_ticks(part, now, false);
	part_stat_inc(part, ios[sgrp]);
	part_stat_add(part, sectors[sgrp], sectors);
	part_stat_local_inc(part, in_flight[op_is_write(op)]);
	part_stat_unlock();

	return now;
}

unsigned long part_start_io_acct(struct gendisk *disk, struct hd_struct **part,
				 struct bio *bio)
{
	*part = disk_map_sector_rcu(disk, bio->bi_iter.bi_sector);

	return __part_start_io_acct(*part, bio_sectors(bio), bio_op(bio));
}
EXPORT_SYMBOL_GPL(part_start_io_acct);

unsigned long disk_start_io_acct(struct gendisk *disk, unsigned int sectors,
				 unsigned int op)
{
	return __part_start_io_acct(&disk->part0, sectors, op);
}
EXPORT_SYMBOL(disk_start_io_acct);

static void __part_end_io_acct(struct hd_struct *part, unsigned int op,
			       unsigned long start_time)
{
	const int sgrp = op_stat_group(op);
	unsigned long now = READ_ONCE(jiffies);
	unsigned long duration = now - start_time;

	part_stat_lock();
	update_io_ticks(part, now, true);
	part_stat_add(part, nsecs[sgrp], jiffies_to_nsecs(duration));
	part_stat_local_dec(part, in_flight[op_is_write(op)]);
	part_stat_unlock();
}

void part_end_io_acct(struct hd_struct *part, struct bio *bio,
		      unsigned long start_time)
{
	__part_end_io_acct(part, bio_op(bio), start_time);
	hd_struct_put(part);
}
EXPORT_SYMBOL_GPL(part_end_io_acct);

void disk_end_io_acct(struct gendisk *disk, unsigned int op,
		      unsigned long start_time)
{
	__part_end_io_acct(&disk->part0, op, start_time);
}
EXPORT_SYMBOL(disk_end_io_acct);

/*
 * Steal bios from a request and add them to a bio list.
 * The request must not have been partially completed before.
 */
void blk_steal_bios(struct bio_list *list, struct request *rq)
{
	if (rq->bio) {
		if (list->tail)
			list->tail->bi_next = rq->bio;
		else
			list->head = rq->bio;
		list->tail = rq->biotail;

		rq->bio = NULL;
		rq->biotail = NULL;
	}

	rq->__data_len = 0;
}
EXPORT_SYMBOL_GPL(blk_steal_bios);

/**
 * blk_update_request - Special helper function for request stacking drivers
 * @req:      the request being processed
 * @error:    block status code
 * @nr_bytes: number of bytes to complete @req
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @req, but doesn't complete
 *     the request structure even if @req doesn't have leftover.
 *     If @req has leftover, sets it up for the next range of segments.
 *
 *     This special helper function is only for request stacking drivers
 *     (e.g. request-based dm) so that they can handle partial completion.
 *     Actual device drivers should use blk_mq_end_request instead.
 *
 *     Passing the result of blk_rq_bytes() as @nr_bytes guarantees
 *     %false return from this function.
 *
 * Note:
 *	The RQF_SPECIAL_PAYLOAD flag is ignored on purpose in both
 *	blk_rq_bytes() and in blk_update_request().
 *
 * Return:
 *     %false - this request doesn't have any more data
 *     %true  - this request has more data
 **/
bool blk_update_request(struct request *req, blk_status_t error,
		unsigned int nr_bytes)
{
	int total_bytes;

	trace_block_rq_complete(req, blk_status_to_errno(error), nr_bytes);

	if (!req->bio)
		return false;

#ifdef CONFIG_BLK_DEV_INTEGRITY
	if (blk_integrity_rq(req) && req_op(req) == REQ_OP_READ &&
	    error == BLK_STS_OK)
		req->q->integrity.profile->complete_fn(req, nr_bytes);
#endif

	if (unlikely(error && !blk_rq_is_passthrough(req) &&
		     !(req->rq_flags & RQF_QUIET)))
		print_req_error(req, error, __func__);

	blk_account_io_completion(req, nr_bytes);

	total_bytes = 0;
	while (req->bio) {
		struct bio *bio = req->bio;
		unsigned bio_bytes = min(bio->bi_iter.bi_size, nr_bytes);

		if (bio_bytes == bio->bi_iter.bi_size)
			req->bio = bio->bi_next;

		/* Completion has already been traced */
		bio_clear_flag(bio, BIO_TRACE_COMPLETION);
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
	if (!blk_rq_is_passthrough(req))
		req->__sector += total_bytes >> 9;

	/* mixed attributes always follow the first bio */
	if (req->rq_flags & RQF_MIXED_MERGE) {
		req->cmd_flags &= ~REQ_FAILFAST_MASK;
		req->cmd_flags |= req->bio->bi_opf & REQ_FAILFAST_MASK;
	}

	if (!(req->rq_flags & RQF_SPECIAL_PAYLOAD)) {
		/*
		 * If total number of sectors is less than the first segment
		 * size, something has gone terribly wrong.
		 */
		if (blk_rq_bytes(req) < blk_rq_cur_bytes(req)) {
			blk_dump_rq_flags(req, "request botched");
			req->__data_len = blk_rq_cur_bytes(req);
		}

		/* recalculate the number of segments */
		req->nr_phys_segments = blk_recalc_rq_segments(req);
	}

	return true;
}
EXPORT_SYMBOL_GPL(blk_update_request);

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
	if (queue_is_mq(q) && q->mq_ops->busy)
		return q->mq_ops->busy(q);

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
		bs = &fs_bio_set;

	__rq_for_each_bio(bio_src, rq_src) {
		bio = bio_clone_fast(bio_src, gfp_mask, bs);
		if (!bio)
			goto free_and_out;

		if (bio_ctr && bio_ctr(bio, bio_src, data))
			goto free_and_out;

		if (rq->bio) {
			rq->biotail->bi_next = bio;
			rq->biotail = bio;
		} else {
			rq->bio = rq->biotail = bio;
		}
		bio = NULL;
	}

	/* Copy attributes of the original request to the clone request. */
	rq->__sector = blk_rq_pos(rq_src);
	rq->__data_len = blk_rq_bytes(rq_src);
	if (rq_src->rq_flags & RQF_SPECIAL_PAYLOAD) {
		rq->rq_flags |= RQF_SPECIAL_PAYLOAD;
		rq->special_vec = rq_src->special_vec;
	}
	rq->nr_phys_segments = rq_src->nr_phys_segments;
	rq->ioprio = rq_src->ioprio;

	if (rq->bio && blk_crypto_rq_bio_prep(rq, rq->bio, gfp_mask) < 0)
		goto free_and_out;

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

int kblockd_mod_delayed_work_on(int cpu, struct delayed_work *dwork,
				unsigned long delay)
{
	return mod_delayed_work_on(cpu, kblockd_workqueue, dwork, delay);
}
EXPORT_SYMBOL(kblockd_mod_delayed_work_on);

/**
 * blk_start_plug - initialize blk_plug and track it inside the task_struct
 * @plug:	The &struct blk_plug that needs to be initialized
 *
 * Description:
 *   blk_start_plug() indicates to the block layer an intent by the caller
 *   to submit multiple I/O requests in a batch.  The block layer may use
 *   this hint to defer submitting I/Os from the caller until blk_finish_plug()
 *   is called.  However, the block layer may choose to submit requests
 *   before a call to blk_finish_plug() if the number of queued I/Os
 *   exceeds %BLK_MAX_REQUEST_COUNT, or if the size of the I/O is larger than
 *   %BLK_PLUG_FLUSH_SIZE.  The queued I/Os may also be submitted early if
 *   the task schedules (see below).
 *
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

	INIT_LIST_HEAD(&plug->mq_list);
	INIT_LIST_HEAD(&plug->cb_list);
	plug->rq_count = 0;
	plug->multiple_queues = false;
	plug->nowait = false;

	/*
	 * Store ordering should not be needed here, since a potential
	 * preempt will imply a full memory barrier
	 */
	tsk->plug = plug;
}
EXPORT_SYMBOL(blk_start_plug);

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
	flush_plug_callbacks(plug, from_schedule);

	if (!list_empty(&plug->mq_list))
		blk_mq_flush_plug_list(plug, from_schedule);
}

/**
 * blk_finish_plug - mark the end of a batch of submitted I/O
 * @plug:	The &struct blk_plug passed to blk_start_plug()
 *
 * Description:
 * Indicate that a batch of I/O submissions is complete.  This function
 * must be paired with an initial call to blk_start_plug().  The intent
 * is to allow the block layer to optimize I/O submission.  See the
 * documentation for blk_start_plug() for more information.
 */
void blk_finish_plug(struct blk_plug *plug)
{
	if (plug != current->plug)
		return;
	blk_flush_plug_list(plug, false);

	current->plug = NULL;
}
EXPORT_SYMBOL(blk_finish_plug);

void blk_io_schedule(void)
{
	/* Prevent hang_check timer from firing at us during very long I/O */
	unsigned long timeout = sysctl_hung_task_timeout_secs * HZ / 2;

	if (timeout)
		io_schedule_timeout(timeout);
	else
		io_schedule();
}
EXPORT_SYMBOL_GPL(blk_io_schedule);

int __init blk_dev_init(void)
{
	BUILD_BUG_ON(REQ_OP_LAST >= (1 << REQ_OP_BITS));
	BUILD_BUG_ON(REQ_OP_BITS + REQ_FLAG_BITS > 8 *
			sizeof_field(struct request, cmd_flags));
	BUILD_BUG_ON(REQ_OP_BITS + REQ_FLAG_BITS > 8 *
			sizeof_field(struct bio, bi_opf));

	/* used for unplugging and affects IO latency/throughput - HIGHPRI */
	kblockd_workqueue = alloc_workqueue("kblockd",
					    WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	if (!kblockd_workqueue)
		panic("Failed to create kblockd\n");

	blk_requestq_cachep = kmem_cache_create("request_queue",
			sizeof(struct request_queue), 0, SLAB_PANIC, NULL);

	blk_debugfs_root = debugfs_create_dir("block", NULL);

	return 0;
}
