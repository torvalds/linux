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
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/blktrace_api.h>
#include <linux/fault-inject.h>

#include "blk.h"

static int __make_request(struct request_queue *q, struct bio *bio);

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

static DEFINE_PER_CPU(struct list_head, blk_cpu_done);

static void drive_stat_acct(struct request *rq, int new_io)
{
	int rw = rq_data_dir(rq);

	if (!blk_fs_request(rq) || !rq->rq_disk)
		return;

	if (!new_io) {
		__all_stat_inc(rq->rq_disk, merges[rw], rq->sector);
	} else {
		struct hd_struct *part = get_part(rq->rq_disk, rq->sector);
		disk_round_stats(rq->rq_disk);
		rq->rq_disk->in_flight++;
		if (part) {
			part_round_stats(part);
			part->in_flight++;
		}
	}
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

/*
 * We can't just memset() the structure, since the allocation path
 * already stored some information in the request.
 */
void rq_init(struct request_queue *q, struct request *rq)
{
	INIT_LIST_HEAD(&rq->queuelist);
	INIT_LIST_HEAD(&rq->donelist);
	rq->q = q;
	rq->sector = rq->hard_sector = (sector_t) -1;
	rq->nr_sectors = rq->hard_nr_sectors = 0;
	rq->current_nr_sectors = rq->hard_cur_sectors = 0;
	rq->bio = rq->biotail = NULL;
	INIT_HLIST_NODE(&rq->hash);
	RB_CLEAR_NODE(&rq->rb_node);
	rq->rq_disk = NULL;
	rq->nr_phys_segments = 0;
	rq->nr_hw_segments = 0;
	rq->ioprio = 0;
	rq->special = NULL;
	rq->raw_data_len = 0;
	rq->buffer = NULL;
	rq->tag = -1;
	rq->errors = 0;
	rq->ref_count = 1;
	rq->cmd_len = 0;
	memset(rq->cmd, 0, sizeof(rq->cmd));
	rq->data_len = 0;
	rq->sense_len = 0;
	rq->data = NULL;
	rq->sense = NULL;
	rq->end_io = NULL;
	rq->end_io_data = NULL;
	rq->next_rq = NULL;
}

static void req_bio_endio(struct request *rq, struct bio *bio,
			  unsigned int nbytes, int error)
{
	struct request_queue *q = rq->q;

	if (&q->bar_rq != rq) {
		if (error)
			clear_bit(BIO_UPTODATE, &bio->bi_flags);
		else if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
			error = -EIO;

		if (unlikely(nbytes > bio->bi_size)) {
			printk(KERN_ERR "%s: want %u bytes done, %u left\n",
			       __FUNCTION__, nbytes, bio->bi_size);
			nbytes = bio->bi_size;
		}

		bio->bi_size -= nbytes;
		bio->bi_sector += (nbytes >> 9);
		if (bio->bi_size == 0)
			bio_endio(bio, error);
	} else {

		/*
		 * Okay, this is the barrier request in progress, just
		 * record the error;
		 */
		if (error && !q->orderr)
			q->orderr = error;
	}
}

void blk_dump_rq_flags(struct request *rq, char *msg)
{
	int bit;

	printk(KERN_INFO "%s: dev %s: type=%x, flags=%x\n", msg,
		rq->rq_disk ? rq->rq_disk->disk_name : "?", rq->cmd_type,
		rq->cmd_flags);

	printk(KERN_INFO "  sector %llu, nr/cnr %lu/%u\n",
						(unsigned long long)rq->sector,
						rq->nr_sectors,
						rq->current_nr_sectors);
	printk(KERN_INFO "  bio %p, biotail %p, buffer %p, data %p, len %u\n",
						rq->bio, rq->biotail,
						rq->buffer, rq->data,
						rq->data_len);

	if (blk_pc_request(rq)) {
		printk(KERN_INFO "  cdb: ");
		for (bit = 0; bit < sizeof(rq->cmd); bit++)
			printk("%02x ", rq->cmd[bit]);
		printk("\n");
	}
}
EXPORT_SYMBOL(blk_dump_rq_flags);

/*
 * "plug" the device if there are no outstanding requests: this will
 * force the transfer to start only after we have put all the requests
 * on the list.
 *
 * This is called with interrupts off and no requests on the queue and
 * with the queue lock held.
 */
void blk_plug_device(struct request_queue *q)
{
	WARN_ON(!irqs_disabled());

	/*
	 * don't plug a stopped queue, it must be paired with blk_start_queue()
	 * which will restart the queueing
	 */
	if (blk_queue_stopped(q))
		return;

	if (!test_and_set_bit(QUEUE_FLAG_PLUGGED, &q->queue_flags)) {
		mod_timer(&q->unplug_timer, jiffies + q->unplug_delay);
		blk_add_trace_generic(q, NULL, 0, BLK_TA_PLUG);
	}
}
EXPORT_SYMBOL(blk_plug_device);

/*
 * remove the queue from the plugged list, if present. called with
 * queue lock held and interrupts disabled.
 */
int blk_remove_plug(struct request_queue *q)
{
	WARN_ON(!irqs_disabled());

	if (!test_and_clear_bit(QUEUE_FLAG_PLUGGED, &q->queue_flags))
		return 0;

	del_timer(&q->unplug_timer);
	return 1;
}
EXPORT_SYMBOL(blk_remove_plug);

/*
 * remove the plug and let it rip..
 */
void __generic_unplug_device(struct request_queue *q)
{
	if (unlikely(blk_queue_stopped(q)))
		return;

	if (!blk_remove_plug(q))
		return;

	q->request_fn(q);
}
EXPORT_SYMBOL(__generic_unplug_device);

/**
 * generic_unplug_device - fire a request queue
 * @q:    The &struct request_queue in question
 *
 * Description:
 *   Linux uses plugging to build bigger requests queues before letting
 *   the device have at them. If a queue is plugged, the I/O scheduler
 *   is still adding and merging requests on the queue. Once the queue
 *   gets unplugged, the request_fn defined for the queue is invoked and
 *   transfers started.
 **/
void generic_unplug_device(struct request_queue *q)
{
	spin_lock_irq(q->queue_lock);
	__generic_unplug_device(q);
	spin_unlock_irq(q->queue_lock);
}
EXPORT_SYMBOL(generic_unplug_device);

static void blk_backing_dev_unplug(struct backing_dev_info *bdi,
				   struct page *page)
{
	struct request_queue *q = bdi->unplug_io_data;

	blk_unplug(q);
}

void blk_unplug_work(struct work_struct *work)
{
	struct request_queue *q =
		container_of(work, struct request_queue, unplug_work);

	blk_add_trace_pdu_int(q, BLK_TA_UNPLUG_IO, NULL,
				q->rq.count[READ] + q->rq.count[WRITE]);

	q->unplug_fn(q);
}

void blk_unplug_timeout(unsigned long data)
{
	struct request_queue *q = (struct request_queue *)data;

	blk_add_trace_pdu_int(q, BLK_TA_UNPLUG_TIMER, NULL,
				q->rq.count[READ] + q->rq.count[WRITE]);

	kblockd_schedule_work(&q->unplug_work);
}

void blk_unplug(struct request_queue *q)
{
	/*
	 * devices don't necessarily have an ->unplug_fn defined
	 */
	if (q->unplug_fn) {
		blk_add_trace_pdu_int(q, BLK_TA_UNPLUG_IO, NULL,
					q->rq.count[READ] + q->rq.count[WRITE]);

		q->unplug_fn(q);
	}
}
EXPORT_SYMBOL(blk_unplug);

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

	clear_bit(QUEUE_FLAG_STOPPED, &q->queue_flags);

	/*
	 * one level of recursion is ok and is much faster than kicking
	 * the unplug handling
	 */
	if (!test_and_set_bit(QUEUE_FLAG_REENTER, &q->queue_flags)) {
		q->request_fn(q);
		clear_bit(QUEUE_FLAG_REENTER, &q->queue_flags);
	} else {
		blk_plug_device(q);
		kblockd_schedule_work(&q->unplug_work);
	}
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
	blk_remove_plug(q);
	set_bit(QUEUE_FLAG_STOPPED, &q->queue_flags);
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
 */
void blk_sync_queue(struct request_queue *q)
{
	del_timer_sync(&q->unplug_timer);
	kblockd_flush_work(&q->unplug_work);
}
EXPORT_SYMBOL(blk_sync_queue);

/**
 * blk_run_queue - run a single device queue
 * @q:	The queue to run
 */
void blk_run_queue(struct request_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	blk_remove_plug(q);

	/*
	 * Only recurse once to avoid overrunning the stack, let the unplug
	 * handling reinvoke the handler shortly if we already got there.
	 */
	if (!elv_queue_empty(q)) {
		if (!test_and_set_bit(QUEUE_FLAG_REENTER, &q->queue_flags)) {
			q->request_fn(q);
			clear_bit(QUEUE_FLAG_REENTER, &q->queue_flags);
		} else {
			blk_plug_device(q);
			kblockd_schedule_work(&q->unplug_work);
		}
	}

	spin_unlock_irqrestore(q->queue_lock, flags);
}
EXPORT_SYMBOL(blk_run_queue);

void blk_put_queue(struct request_queue *q)
{
	kobject_put(&q->kobj);
}
EXPORT_SYMBOL(blk_put_queue);

void blk_cleanup_queue(struct request_queue *q)
{
	mutex_lock(&q->sysfs_lock);
	set_bit(QUEUE_FLAG_DEAD, &q->queue_flags);
	mutex_unlock(&q->sysfs_lock);

	if (q->elevator)
		elevator_exit(q->elevator);

	blk_put_queue(q);
}
EXPORT_SYMBOL(blk_cleanup_queue);

static int blk_init_free_list(struct request_queue *q)
{
	struct request_list *rl = &q->rq;

	rl->count[READ] = rl->count[WRITE] = 0;
	rl->starved[READ] = rl->starved[WRITE] = 0;
	rl->elvpriv = 0;
	init_waitqueue_head(&rl->wait[READ]);
	init_waitqueue_head(&rl->wait[WRITE]);

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

	q->backing_dev_info.unplug_io_fn = blk_backing_dev_unplug;
	q->backing_dev_info.unplug_io_data = q;
	err = bdi_init(&q->backing_dev_info);
	if (err) {
		kmem_cache_free(blk_requestq_cachep, q);
		return NULL;
	}

	init_timer(&q->unplug_timer);

	kobject_init(&q->kobj, &blk_queue_ktype);

	mutex_init(&q->sysfs_lock);

	return q;
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
 *    Function returns a pointer to the initialized request queue, or NULL if
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
	struct request_queue *q = blk_alloc_queue_node(GFP_KERNEL, node_id);

	if (!q)
		return NULL;

	q->node = node_id;
	if (blk_init_free_list(q)) {
		kmem_cache_free(blk_requestq_cachep, q);
		return NULL;
	}

	/*
	 * if caller didn't supply a lock, they get per-queue locking with
	 * our embedded lock
	 */
	if (!lock) {
		spin_lock_init(&q->__queue_lock);
		lock = &q->__queue_lock;
	}

	q->request_fn		= rfn;
	q->prep_rq_fn		= NULL;
	q->unplug_fn		= generic_unplug_device;
	q->queue_flags		= (1 << QUEUE_FLAG_CLUSTER);
	q->queue_lock		= lock;

	blk_queue_segment_boundary(q, 0xffffffff);

	blk_queue_make_request(q, __make_request);
	blk_queue_max_segment_size(q, MAX_SEGMENT_SIZE);

	blk_queue_max_hw_segments(q, MAX_HW_SEGMENTS);
	blk_queue_max_phys_segments(q, MAX_PHYS_SEGMENTS);

	q->sg_reserved_size = INT_MAX;

	/*
	 * all done
	 */
	if (!elevator_init(q, NULL)) {
		blk_queue_congestion_threshold(q);
		return q;
	}

	blk_put_queue(q);
	return NULL;
}
EXPORT_SYMBOL(blk_init_queue_node);

int blk_get_queue(struct request_queue *q)
{
	if (likely(!test_bit(QUEUE_FLAG_DEAD, &q->queue_flags))) {
		kobject_get(&q->kobj);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL(blk_get_queue);

static inline void blk_free_request(struct request_queue *q, struct request *rq)
{
	if (rq->cmd_flags & REQ_ELVPRIV)
		elv_put_request(q, rq);
	mempool_free(rq, q->rq.rq_pool);
}

static struct request *
blk_alloc_request(struct request_queue *q, int rw, int priv, gfp_t gfp_mask)
{
	struct request *rq = mempool_alloc(q->rq.rq_pool, gfp_mask);

	if (!rq)
		return NULL;

	/*
	 * first three bits are identical in rq->cmd_flags and bio->bi_rw,
	 * see bio.h and blkdev.h
	 */
	rq->cmd_flags = rw | REQ_ALLOCED;

	if (priv) {
		if (unlikely(elv_set_request(q, rq, gfp_mask))) {
			mempool_free(rq, q->rq.rq_pool);
			return NULL;
		}
		rq->cmd_flags |= REQ_ELVPRIV;
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

static void __freed_request(struct request_queue *q, int rw)
{
	struct request_list *rl = &q->rq;

	if (rl->count[rw] < queue_congestion_off_threshold(q))
		blk_clear_queue_congested(q, rw);

	if (rl->count[rw] + 1 <= q->nr_requests) {
		if (waitqueue_active(&rl->wait[rw]))
			wake_up(&rl->wait[rw]);

		blk_clear_queue_full(q, rw);
	}
}

/*
 * A request has just been released.  Account for it, update the full and
 * congestion status, wake up any waiters.   Called under q->queue_lock.
 */
static void freed_request(struct request_queue *q, int rw, int priv)
{
	struct request_list *rl = &q->rq;

	rl->count[rw]--;
	if (priv)
		rl->elvpriv--;

	__freed_request(q, rw);

	if (unlikely(rl->starved[rw ^ 1]))
		__freed_request(q, rw ^ 1);
}

#define blkdev_free_rq(list) list_entry((list)->next, struct request, queuelist)
/*
 * Get a free request, queue_lock must be held.
 * Returns NULL on failure, with queue_lock held.
 * Returns !NULL on success, with queue_lock *not held*.
 */
static struct request *get_request(struct request_queue *q, int rw_flags,
				   struct bio *bio, gfp_t gfp_mask)
{
	struct request *rq = NULL;
	struct request_list *rl = &q->rq;
	struct io_context *ioc = NULL;
	const int rw = rw_flags & 0x01;
	int may_queue, priv;

	may_queue = elv_may_queue(q, rw_flags);
	if (may_queue == ELV_MQUEUE_NO)
		goto rq_starved;

	if (rl->count[rw]+1 >= queue_congestion_on_threshold(q)) {
		if (rl->count[rw]+1 >= q->nr_requests) {
			ioc = current_io_context(GFP_ATOMIC, q->node);
			/*
			 * The queue will fill after this allocation, so set
			 * it as full, and mark this process as "batching".
			 * This process will be allowed to complete a batch of
			 * requests, others will be blocked.
			 */
			if (!blk_queue_full(q, rw)) {
				ioc_set_batching(q, ioc);
				blk_set_queue_full(q, rw);
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
		blk_set_queue_congested(q, rw);
	}

	/*
	 * Only allow batching queuers to allocate up to 50% over the defined
	 * limit of requests, otherwise we could have thousands of requests
	 * allocated with any setting of ->nr_requests
	 */
	if (rl->count[rw] >= (3 * q->nr_requests / 2))
		goto out;

	rl->count[rw]++;
	rl->starved[rw] = 0;

	priv = !test_bit(QUEUE_FLAG_ELVSWITCH, &q->queue_flags);
	if (priv)
		rl->elvpriv++;

	spin_unlock_irq(q->queue_lock);

	rq = blk_alloc_request(q, rw_flags, priv, gfp_mask);
	if (unlikely(!rq)) {
		/*
		 * Allocation failed presumably due to memory. Undo anything
		 * we might have messed up.
		 *
		 * Allocating task should really be put onto the front of the
		 * wait queue, but this is pretty rare.
		 */
		spin_lock_irq(q->queue_lock);
		freed_request(q, rw, priv);

		/*
		 * in the very unlikely event that allocation failed and no
		 * requests for this direction was pending, mark us starved
		 * so that freeing of a request in the other direction will
		 * notice us. another possible fix would be to split the
		 * rq mempool into READ and WRITE
		 */
rq_starved:
		if (unlikely(rl->count[rw] == 0))
			rl->starved[rw] = 1;

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

	rq_init(q, rq);

	blk_add_trace_generic(q, bio, rw, BLK_TA_GETRQ);
out:
	return rq;
}

/*
 * No available requests for this queue, unplug the device and wait for some
 * requests to become available.
 *
 * Called with q->queue_lock held, and returns with it unlocked.
 */
static struct request *get_request_wait(struct request_queue *q, int rw_flags,
					struct bio *bio)
{
	const int rw = rw_flags & 0x01;
	struct request *rq;

	rq = get_request(q, rw_flags, bio, GFP_NOIO);
	while (!rq) {
		DEFINE_WAIT(wait);
		struct request_list *rl = &q->rq;

		prepare_to_wait_exclusive(&rl->wait[rw], &wait,
				TASK_UNINTERRUPTIBLE);

		rq = get_request(q, rw_flags, bio, GFP_NOIO);

		if (!rq) {
			struct io_context *ioc;

			blk_add_trace_generic(q, bio, rw, BLK_TA_SLEEPRQ);

			__generic_unplug_device(q);
			spin_unlock_irq(q->queue_lock);
			io_schedule();

			/*
			 * After sleeping, we become a "batching" process and
			 * will be able to allocate at least one request, and
			 * up to a big batch of them for a small period time.
			 * See ioc_batching, ioc_set_batching
			 */
			ioc = current_io_context(GFP_NOIO, q->node);
			ioc_set_batching(q, ioc);

			spin_lock_irq(q->queue_lock);
		}
		finish_wait(&rl->wait[rw], &wait);
	}

	return rq;
}

struct request *blk_get_request(struct request_queue *q, int rw, gfp_t gfp_mask)
{
	struct request *rq;

	BUG_ON(rw != READ && rw != WRITE);

	spin_lock_irq(q->queue_lock);
	if (gfp_mask & __GFP_WAIT) {
		rq = get_request_wait(q, rw, NULL);
	} else {
		rq = get_request(q, rw, NULL, gfp_mask);
		if (!rq)
			spin_unlock_irq(q->queue_lock);
	}
	/* q->queue_lock is unlocked at this point */

	return rq;
}
EXPORT_SYMBOL(blk_get_request);

/**
 * blk_start_queueing - initiate dispatch of requests to device
 * @q:		request queue to kick into gear
 *
 * This is basically a helper to remove the need to know whether a queue
 * is plugged or not if someone just wants to initiate dispatch of requests
 * for this queue.
 *
 * The queue lock must be held with interrupts disabled.
 */
void blk_start_queueing(struct request_queue *q)
{
	if (!blk_queue_plugged(q))
		q->request_fn(q);
	else
		__generic_unplug_device(q);
}
EXPORT_SYMBOL(blk_start_queueing);

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
	blk_add_trace_rq(q, rq, BLK_TA_REQUEUE);

	if (blk_rq_tagged(rq))
		blk_queue_end_tag(q, rq);

	elv_requeue_request(q, rq);
}
EXPORT_SYMBOL(blk_requeue_request);

/**
 * blk_insert_request - insert a special request in to a request queue
 * @q:		request queue where request should be inserted
 * @rq:		request to be inserted
 * @at_head:	insert request at head or tail of queue
 * @data:	private data
 *
 * Description:
 *    Many block devices need to execute commands asynchronously, so they don't
 *    block the whole kernel from preemption during request execution.  This is
 *    accomplished normally by inserting aritficial requests tagged as
 *    REQ_SPECIAL in to the corresponding request queue, and letting them be
 *    scheduled for actual execution by the request queue.
 *
 *    We have the option of inserting the head or the tail of the queue.
 *    Typically we use the tail for new ioctls and so forth.  We use the head
 *    of the queue for things like a QUEUE_FULL message from a device, or a
 *    host that is unable to accept a particular command.
 */
void blk_insert_request(struct request_queue *q, struct request *rq,
			int at_head, void *data)
{
	int where = at_head ? ELEVATOR_INSERT_FRONT : ELEVATOR_INSERT_BACK;
	unsigned long flags;

	/*
	 * tell I/O scheduler that this isn't a regular read/write (ie it
	 * must not attempt merges on this) and that it acts as a soft
	 * barrier
	 */
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd_flags |= REQ_SOFTBARRIER;

	rq->special = data;

	spin_lock_irqsave(q->queue_lock, flags);

	/*
	 * If command is tagged, release the tag
	 */
	if (blk_rq_tagged(rq))
		blk_queue_end_tag(q, rq);

	drive_stat_acct(rq, 1);
	__elv_add_request(q, rq, where, 0);
	blk_start_queueing(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}
EXPORT_SYMBOL(blk_insert_request);

/*
 * add-request adds a request to the linked list.
 * queue lock is held and interrupts disabled, as we muck with the
 * request queue list.
 */
static inline void add_request(struct request_queue *q, struct request *req)
{
	drive_stat_acct(req, 1);

	/*
	 * elevator indicated where it wants this request to be
	 * inserted at elevator_merge time
	 */
	__elv_add_request(q, req, ELEVATOR_INSERT_SORT, 0);
}

/*
 * disk_round_stats()	- Round off the performance stats on a struct
 * disk_stats.
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
void disk_round_stats(struct gendisk *disk)
{
	unsigned long now = jiffies;

	if (now == disk->stamp)
		return;

	if (disk->in_flight) {
		__disk_stat_add(disk, time_in_queue,
				disk->in_flight * (now - disk->stamp));
		__disk_stat_add(disk, io_ticks, (now - disk->stamp));
	}
	disk->stamp = now;
}
EXPORT_SYMBOL_GPL(disk_round_stats);

void part_round_stats(struct hd_struct *part)
{
	unsigned long now = jiffies;

	if (now == part->stamp)
		return;

	if (part->in_flight) {
		__part_stat_add(part, time_in_queue,
				part->in_flight * (now - part->stamp));
		__part_stat_add(part, io_ticks, (now - part->stamp));
	}
	part->stamp = now;
}

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

	/*
	 * Request may not have originated from ll_rw_blk. if not,
	 * it didn't come out of our reserved rq pools
	 */
	if (req->cmd_flags & REQ_ALLOCED) {
		int rw = rq_data_dir(req);
		int priv = req->cmd_flags & REQ_ELVPRIV;

		BUG_ON(!list_empty(&req->queuelist));
		BUG_ON(!hlist_unhashed(&req->hash));

		blk_free_request(q, req);
		freed_request(q, rw, priv);
	}
}
EXPORT_SYMBOL_GPL(__blk_put_request);

void blk_put_request(struct request *req)
{
	unsigned long flags;
	struct request_queue *q = req->q;

	/*
	 * Gee, IDE calls in w/ NULL q.  Fix IDE and remove the
	 * following if (q) test.
	 */
	if (q) {
		spin_lock_irqsave(q->queue_lock, flags);
		__blk_put_request(q, req);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}
EXPORT_SYMBOL(blk_put_request);

void init_request_from_bio(struct request *req, struct bio *bio)
{
	req->cmd_type = REQ_TYPE_FS;

	/*
	 * inherit FAILFAST from bio (for read-ahead, and explicit FAILFAST)
	 */
	if (bio_rw_ahead(bio) || bio_failfast(bio))
		req->cmd_flags |= REQ_FAILFAST;

	/*
	 * REQ_BARRIER implies no merging, but lets make it explicit
	 */
	if (unlikely(bio_barrier(bio)))
		req->cmd_flags |= (REQ_HARDBARRIER | REQ_NOMERGE);

	if (bio_sync(bio))
		req->cmd_flags |= REQ_RW_SYNC;
	if (bio_rw_meta(bio))
		req->cmd_flags |= REQ_RW_META;

	req->errors = 0;
	req->hard_sector = req->sector = bio->bi_sector;
	req->ioprio = bio_prio(bio);
	req->start_time = jiffies;
	blk_rq_bio_prep(req->q, req, bio);
}

static int __make_request(struct request_queue *q, struct bio *bio)
{
	struct request *req;
	int el_ret, nr_sectors, barrier, err;
	const unsigned short prio = bio_prio(bio);
	const int sync = bio_sync(bio);
	int rw_flags;

	nr_sectors = bio_sectors(bio);

	/*
	 * low level driver can indicate that it wants pages above a
	 * certain limit bounced to low memory (ie for highmem, or even
	 * ISA dma in theory)
	 */
	blk_queue_bounce(q, &bio);

	barrier = bio_barrier(bio);
	if (unlikely(barrier) && (q->next_ordered == QUEUE_ORDERED_NONE)) {
		err = -EOPNOTSUPP;
		goto end_io;
	}

	spin_lock_irq(q->queue_lock);

	if (unlikely(barrier) || elv_queue_empty(q))
		goto get_rq;

	el_ret = elv_merge(q, &req, bio);
	switch (el_ret) {
	case ELEVATOR_BACK_MERGE:
		BUG_ON(!rq_mergeable(req));

		if (!ll_back_merge_fn(q, req, bio))
			break;

		blk_add_trace_bio(q, bio, BLK_TA_BACKMERGE);

		req->biotail->bi_next = bio;
		req->biotail = bio;
		req->nr_sectors = req->hard_nr_sectors += nr_sectors;
		req->ioprio = ioprio_best(req->ioprio, prio);
		drive_stat_acct(req, 0);
		if (!attempt_back_merge(q, req))
			elv_merged_request(q, req, el_ret);
		goto out;

	case ELEVATOR_FRONT_MERGE:
		BUG_ON(!rq_mergeable(req));

		if (!ll_front_merge_fn(q, req, bio))
			break;

		blk_add_trace_bio(q, bio, BLK_TA_FRONTMERGE);

		bio->bi_next = req->bio;
		req->bio = bio;

		/*
		 * may not be valid. if the low level driver said
		 * it didn't need a bounce buffer then it better
		 * not touch req->buffer either...
		 */
		req->buffer = bio_data(bio);
		req->current_nr_sectors = bio_cur_sectors(bio);
		req->hard_cur_sectors = req->current_nr_sectors;
		req->sector = req->hard_sector = bio->bi_sector;
		req->nr_sectors = req->hard_nr_sectors += nr_sectors;
		req->ioprio = ioprio_best(req->ioprio, prio);
		drive_stat_acct(req, 0);
		if (!attempt_front_merge(q, req))
			elv_merged_request(q, req, el_ret);
		goto out;

	/* ELV_NO_MERGE: elevator says don't/can't merge. */
	default:
		;
	}

get_rq:
	/*
	 * This sync check and mask will be re-done in init_request_from_bio(),
	 * but we need to set it earlier to expose the sync flag to the
	 * rq allocator and io schedulers.
	 */
	rw_flags = bio_data_dir(bio);
	if (sync)
		rw_flags |= REQ_RW_SYNC;

	/*
	 * Grab a free request. This is might sleep but can not fail.
	 * Returns with the queue unlocked.
	 */
	req = get_request_wait(q, rw_flags, bio);

	/*
	 * After dropping the lock and possibly sleeping here, our request
	 * may now be mergeable after it had proven unmergeable (above).
	 * We don't worry about that case for efficiency. It won't happen
	 * often, and the elevators are able to handle it.
	 */
	init_request_from_bio(req, bio);

	spin_lock_irq(q->queue_lock);
	if (elv_queue_empty(q))
		blk_plug_device(q);
	add_request(q, req);
out:
	if (sync)
		__generic_unplug_device(q);

	spin_unlock_irq(q->queue_lock);
	return 0;

end_io:
	bio_endio(bio, err);
	return 0;
}

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

		blk_add_trace_remap(bdev_get_queue(bio->bi_bdev), bio,
				    bdev->bd_dev, bio->bi_sector,
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
			(long long)(bio->bi_bdev->bd_inode->i_size >> 9));

	set_bit(BIO_EOF, &bio->bi_flags);
}

#ifdef CONFIG_FAIL_MAKE_REQUEST

static DECLARE_FAULT_ATTR(fail_make_request);

static int __init setup_fail_make_request(char *str)
{
	return setup_fault_attr(&fail_make_request, str);
}
__setup("fail_make_request=", setup_fail_make_request);

static int should_fail_request(struct bio *bio)
{
	if ((bio->bi_bdev->bd_disk->flags & GENHD_FL_FAIL) ||
	    (bio->bi_bdev->bd_part && bio->bi_bdev->bd_part->make_it_fail))
		return should_fail(&fail_make_request, bio->bi_size);

	return 0;
}

static int __init fail_make_request_debugfs(void)
{
	return init_fault_attr_dentries(&fail_make_request,
					"fail_make_request");
}

late_initcall(fail_make_request_debugfs);

#else /* CONFIG_FAIL_MAKE_REQUEST */

static inline int should_fail_request(struct bio *bio)
{
	return 0;
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
	maxsector = bio->bi_bdev->bd_inode->i_size >> 9;
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

/**
 * generic_make_request: hand a buffer to its device driver for I/O
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
 * bio happens to be merged with someone else, and may change bi_dev and
 * bi_sector for remaps as it sees fit.  So the values of these fields
 * should NOT be depended on after the call to generic_make_request.
 */
static inline void __generic_make_request(struct bio *bio)
{
	struct request_queue *q;
	sector_t old_sector;
	int ret, nr_sectors = bio_sectors(bio);
	dev_t old_dev;
	int err = -EIO;

	might_sleep();

	if (bio_check_eod(bio, nr_sectors))
		goto end_io;

	/*
	 * Resolve the mapping until finished. (drivers are
	 * still free to implement/resolve their own stacking
	 * by explicitly returning 0)
	 *
	 * NOTE: we don't repeat the blk_size check for each new device.
	 * Stacking drivers are expected to know what they are doing.
	 */
	old_sector = -1;
	old_dev = 0;
	do {
		char b[BDEVNAME_SIZE];

		q = bdev_get_queue(bio->bi_bdev);
		if (!q) {
			printk(KERN_ERR
			       "generic_make_request: Trying to access "
				"nonexistent block-device %s (%Lu)\n",
				bdevname(bio->bi_bdev, b),
				(long long) bio->bi_sector);
end_io:
			bio_endio(bio, err);
			break;
		}

		if (unlikely(nr_sectors > q->max_hw_sectors)) {
			printk(KERN_ERR "bio too big device %s (%u > %u)\n",
				bdevname(bio->bi_bdev, b),
				bio_sectors(bio),
				q->max_hw_sectors);
			goto end_io;
		}

		if (unlikely(test_bit(QUEUE_FLAG_DEAD, &q->queue_flags)))
			goto end_io;

		if (should_fail_request(bio))
			goto end_io;

		/*
		 * If this device has partitions, remap block n
		 * of partition p to block n+start(p) of the disk.
		 */
		blk_partition_remap(bio);

		if (old_sector != -1)
			blk_add_trace_remap(q, bio, old_dev, bio->bi_sector,
					    old_sector);

		blk_add_trace_bio(q, bio, BLK_TA_QUEUE);

		old_sector = bio->bi_sector;
		old_dev = bio->bi_bdev->bd_dev;

		if (bio_check_eod(bio, nr_sectors))
			goto end_io;
		if (bio_empty_barrier(bio) && !q->prepare_flush_fn) {
			err = -EOPNOTSUPP;
			goto end_io;
		}

		ret = q->make_request_fn(q, bio);
	} while (ret);
}

/*
 * We only want one ->make_request_fn to be active at a time,
 * else stack usage with stacked devices could be a problem.
 * So use current->bio_{list,tail} to keep a list of requests
 * submited by a make_request_fn function.
 * current->bio_tail is also used as a flag to say if
 * generic_make_request is currently active in this task or not.
 * If it is NULL, then no make_request is active.  If it is non-NULL,
 * then a make_request is active, and new requests should be added
 * at the tail
 */
void generic_make_request(struct bio *bio)
{
	if (current->bio_tail) {
		/* make_request is active */
		*(current->bio_tail) = bio;
		bio->bi_next = NULL;
		current->bio_tail = &bio->bi_next;
		return;
	}
	/* following loop may be a bit non-obvious, and so deserves some
	 * explanation.
	 * Before entering the loop, bio->bi_next is NULL (as all callers
	 * ensure that) so we have a list with a single bio.
	 * We pretend that we have just taken it off a longer list, so
	 * we assign bio_list to the next (which is NULL) and bio_tail
	 * to &bio_list, thus initialising the bio_list of new bios to be
	 * added.  __generic_make_request may indeed add some more bios
	 * through a recursive call to generic_make_request.  If it
	 * did, we find a non-NULL value in bio_list and re-enter the loop
	 * from the top.  In this case we really did just take the bio
	 * of the top of the list (no pretending) and so fixup bio_list and
	 * bio_tail or bi_next, and call into __generic_make_request again.
	 *
	 * The loop was structured like this to make only one call to
	 * __generic_make_request (which is important as it is large and
	 * inlined) and to keep the structure simple.
	 */
	BUG_ON(bio->bi_next);
	do {
		current->bio_list = bio->bi_next;
		if (bio->bi_next == NULL)
			current->bio_tail = &current->bio_list;
		else
			bio->bi_next = NULL;
		__generic_make_request(bio);
		bio = current->bio_list;
	} while (bio);
	current->bio_tail = NULL; /* deactivate */
}
EXPORT_SYMBOL(generic_make_request);

/**
 * submit_bio: submit a bio to the block device layer for I/O
 * @rw: whether to %READ or %WRITE, or maybe to %READA (read ahead)
 * @bio: The &struct bio which describes the I/O
 *
 * submit_bio() is very similar in purpose to generic_make_request(), and
 * uses that function to do most of the work. Both are fairly rough
 * interfaces, @bio must be presetup and ready for I/O.
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
	if (!bio_empty_barrier(bio)) {

		BIO_BUG_ON(!bio->bi_size);
		BIO_BUG_ON(!bio->bi_io_vec);

		if (rw & WRITE) {
			count_vm_events(PGPGOUT, count);
		} else {
			task_io_account_read(bio->bi_size);
			count_vm_events(PGPGIN, count);
		}

		if (unlikely(block_dump)) {
			char b[BDEVNAME_SIZE];
			printk(KERN_DEBUG "%s(%d): %s block %Lu on %s\n",
			current->comm, task_pid_nr(current),
				(rw & WRITE) ? "WRITE" : "READ",
				(unsigned long long)bio->bi_sector,
				bdevname(bio->bi_bdev, b));
		}
	}

	generic_make_request(bio);
}
EXPORT_SYMBOL(submit_bio);

/**
 * __end_that_request_first - end I/O on a request
 * @req:      the request being processed
 * @error:    0 for success, < 0 for error
 * @nr_bytes: number of bytes to complete
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @req, and sets it up
 *     for the next range of segments (if any) in the cluster.
 *
 * Return:
 *     0 - we are done with this request, call end_that_request_last()
 *     1 - still buffers pending for this request
 **/
static int __end_that_request_first(struct request *req, int error,
				    int nr_bytes)
{
	int total_bytes, bio_nbytes, next_idx = 0;
	struct bio *bio;

	blk_add_trace_rq(req->q, req, BLK_TA_COMPLETE);

	/*
	 * for a REQ_BLOCK_PC request, we want to carry any eventual
	 * sense key with us all the way through
	 */
	if (!blk_pc_request(req))
		req->errors = 0;

	if (error && (blk_fs_request(req) && !(req->cmd_flags & REQ_QUIET))) {
		printk(KERN_ERR "end_request: I/O error, dev %s, sector %llu\n",
				req->rq_disk ? req->rq_disk->disk_name : "?",
				(unsigned long long)req->sector);
	}

	if (blk_fs_request(req) && req->rq_disk) {
		const int rw = rq_data_dir(req);

		all_stat_add(req->rq_disk, sectors[rw],
			     nr_bytes >> 9, req->sector);
	}

	total_bytes = bio_nbytes = 0;
	while ((bio = req->bio) != NULL) {
		int nbytes;

		/*
		 * For an empty barrier request, the low level driver must
		 * store a potential error location in ->sector. We pass
		 * that back up in ->bi_sector.
		 */
		if (blk_empty_barrier(req))
			bio->bi_sector = req->sector;

		if (nr_bytes >= bio->bi_size) {
			req->bio = bio->bi_next;
			nbytes = bio->bi_size;
			req_bio_endio(req, bio, nbytes, error);
			next_idx = 0;
			bio_nbytes = 0;
		} else {
			int idx = bio->bi_idx + next_idx;

			if (unlikely(bio->bi_idx >= bio->bi_vcnt)) {
				blk_dump_rq_flags(req, "__end_that");
				printk(KERN_ERR "%s: bio idx %d >= vcnt %d\n",
						__FUNCTION__, bio->bi_idx,
						bio->bi_vcnt);
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
	if (!req->bio)
		return 0;

	/*
	 * if the request wasn't completed, update state
	 */
	if (bio_nbytes) {
		req_bio_endio(req, bio, bio_nbytes, error);
		bio->bi_idx += next_idx;
		bio_iovec(bio)->bv_offset += nr_bytes;
		bio_iovec(bio)->bv_len -= nr_bytes;
	}

	blk_recalc_rq_sectors(req, total_bytes >> 9);
	blk_recalc_rq_segments(req);
	return 1;
}

/*
 * splice the completion data to a local structure and hand off to
 * process_completion_queue() to complete the requests
 */
static void blk_done_softirq(struct softirq_action *h)
{
	struct list_head *cpu_list, local_list;

	local_irq_disable();
	cpu_list = &__get_cpu_var(blk_cpu_done);
	list_replace_init(cpu_list, &local_list);
	local_irq_enable();

	while (!list_empty(&local_list)) {
		struct request *rq;

		rq = list_entry(local_list.next, struct request, donelist);
		list_del_init(&rq->donelist);
		rq->q->softirq_done_fn(rq);
	}
}

static int __cpuinit blk_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	/*
	 * If a CPU goes away, splice its entries to the current CPU
	 * and trigger a run of the softirq
	 */
	if (action == CPU_DEAD || action == CPU_DEAD_FROZEN) {
		int cpu = (unsigned long) hcpu;

		local_irq_disable();
		list_splice_init(&per_cpu(blk_cpu_done, cpu),
				 &__get_cpu_var(blk_cpu_done));
		raise_softirq_irqoff(BLOCK_SOFTIRQ);
		local_irq_enable();
	}

	return NOTIFY_OK;
}


static struct notifier_block blk_cpu_notifier __cpuinitdata = {
	.notifier_call	= blk_cpu_notify,
};

/**
 * blk_complete_request - end I/O on a request
 * @req:      the request being processed
 *
 * Description:
 *     Ends all I/O on a request. It does not handle partial completions,
 *     unless the driver actually implements this in its completion callback
 *     through requeueing. The actual completion happens out-of-order,
 *     through a softirq handler. The user must have registered a completion
 *     callback through blk_queue_softirq_done().
 **/

void blk_complete_request(struct request *req)
{
	struct list_head *cpu_list;
	unsigned long flags;

	BUG_ON(!req->q->softirq_done_fn);

	local_irq_save(flags);

	cpu_list = &__get_cpu_var(blk_cpu_done);
	list_add_tail(&req->donelist, cpu_list);
	raise_softirq_irqoff(BLOCK_SOFTIRQ);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(blk_complete_request);

/*
 * queue lock must be held
 */
static void end_that_request_last(struct request *req, int error)
{
	struct gendisk *disk = req->rq_disk;

	if (blk_rq_tagged(req))
		blk_queue_end_tag(req->q, req);

	if (blk_queued_rq(req))
		blkdev_dequeue_request(req);

	if (unlikely(laptop_mode) && blk_fs_request(req))
		laptop_io_completion();

	/*
	 * Account IO completion.  bar_rq isn't accounted as a normal
	 * IO on queueing nor completion.  Accounting the containing
	 * request is enough.
	 */
	if (disk && blk_fs_request(req) && req != &req->q->bar_rq) {
		unsigned long duration = jiffies - req->start_time;
		const int rw = rq_data_dir(req);
		struct hd_struct *part = get_part(disk, req->sector);

		__all_stat_inc(disk, ios[rw], req->sector);
		__all_stat_add(disk, ticks[rw], duration, req->sector);
		disk_round_stats(disk);
		disk->in_flight--;
		if (part) {
			part_round_stats(part);
			part->in_flight--;
		}
	}

	if (req->end_io)
		req->end_io(req, error);
	else {
		if (blk_bidi_rq(req))
			__blk_put_request(req->next_rq->q, req->next_rq);

		__blk_put_request(req->q, req);
	}
}

static inline void __end_request(struct request *rq, int uptodate,
				 unsigned int nr_bytes)
{
	int error = 0;

	if (uptodate <= 0)
		error = uptodate ? uptodate : -EIO;

	__blk_end_request(rq, error, nr_bytes);
}

/**
 * blk_rq_bytes - Returns bytes left to complete in the entire request
 **/
unsigned int blk_rq_bytes(struct request *rq)
{
	if (blk_fs_request(rq))
		return rq->hard_nr_sectors << 9;

	return rq->data_len;
}
EXPORT_SYMBOL_GPL(blk_rq_bytes);

/**
 * blk_rq_cur_bytes - Returns bytes left to complete in the current segment
 **/
unsigned int blk_rq_cur_bytes(struct request *rq)
{
	if (blk_fs_request(rq))
		return rq->current_nr_sectors << 9;

	if (rq->bio)
		return rq->bio->bi_size;

	return rq->data_len;
}
EXPORT_SYMBOL_GPL(blk_rq_cur_bytes);

/**
 * end_queued_request - end all I/O on a queued request
 * @rq:		the request being processed
 * @uptodate:	error value or 0/1 uptodate flag
 *
 * Description:
 *     Ends all I/O on a request, and removes it from the block layer queues.
 *     Not suitable for normal IO completion, unless the driver still has
 *     the request attached to the block layer.
 *
 **/
void end_queued_request(struct request *rq, int uptodate)
{
	__end_request(rq, uptodate, blk_rq_bytes(rq));
}
EXPORT_SYMBOL(end_queued_request);

/**
 * end_dequeued_request - end all I/O on a dequeued request
 * @rq:		the request being processed
 * @uptodate:	error value or 0/1 uptodate flag
 *
 * Description:
 *     Ends all I/O on a request. The request must already have been
 *     dequeued using blkdev_dequeue_request(), as is normally the case
 *     for most drivers.
 *
 **/
void end_dequeued_request(struct request *rq, int uptodate)
{
	__end_request(rq, uptodate, blk_rq_bytes(rq));
}
EXPORT_SYMBOL(end_dequeued_request);


/**
 * end_request - end I/O on the current segment of the request
 * @req:	the request being processed
 * @uptodate:	error value or 0/1 uptodate flag
 *
 * Description:
 *     Ends I/O on the current segment of a request. If that is the only
 *     remaining segment, the request is also completed and freed.
 *
 *     This is a remnant of how older block drivers handled IO completions.
 *     Modern drivers typically end IO on the full request in one go, unless
 *     they have a residual value to account for. For that case this function
 *     isn't really useful, unless the residual just happens to be the
 *     full current segment. In other words, don't use this function in new
 *     code. Either use end_request_completely(), or the
 *     end_that_request_chunk() (along with end_that_request_last()) for
 *     partial completions.
 *
 **/
void end_request(struct request *req, int uptodate)
{
	__end_request(req, uptodate, req->hard_cur_sectors << 9);
}
EXPORT_SYMBOL(end_request);

/**
 * blk_end_io - Generic end_io function to complete a request.
 * @rq:           the request being processed
 * @error:        0 for success, < 0 for error
 * @nr_bytes:     number of bytes to complete @rq
 * @bidi_bytes:   number of bytes to complete @rq->next_rq
 * @drv_callback: function called between completion of bios in the request
 *                and completion of the request.
 *                If the callback returns non 0, this helper returns without
 *                completion of the request.
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @rq and @rq->next_rq.
 *     If @rq has leftover, sets it up for the next range of segments.
 *
 * Return:
 *     0 - we are done with this request
 *     1 - this request is not freed yet, it still has pending buffers.
 **/
static int blk_end_io(struct request *rq, int error, unsigned int nr_bytes,
		      unsigned int bidi_bytes,
		      int (drv_callback)(struct request *))
{
	struct request_queue *q = rq->q;
	unsigned long flags = 0UL;

	if (blk_fs_request(rq) || blk_pc_request(rq)) {
		if (__end_that_request_first(rq, error, nr_bytes))
			return 1;

		/* Bidi request must be completed as a whole */
		if (blk_bidi_rq(rq) &&
		    __end_that_request_first(rq->next_rq, error, bidi_bytes))
			return 1;
	}

	/* Special feature for tricky drivers */
	if (drv_callback && drv_callback(rq))
		return 1;

	add_disk_randomness(rq->rq_disk);

	spin_lock_irqsave(q->queue_lock, flags);
	end_that_request_last(rq, error);
	spin_unlock_irqrestore(q->queue_lock, flags);

	return 0;
}

/**
 * blk_end_request - Helper function for drivers to complete the request.
 * @rq:       the request being processed
 * @error:    0 for success, < 0 for error
 * @nr_bytes: number of bytes to complete
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @rq.
 *     If @rq has leftover, sets it up for the next range of segments.
 *
 * Return:
 *     0 - we are done with this request
 *     1 - still buffers pending for this request
 **/
int blk_end_request(struct request *rq, int error, unsigned int nr_bytes)
{
	return blk_end_io(rq, error, nr_bytes, 0, NULL);
}
EXPORT_SYMBOL_GPL(blk_end_request);

/**
 * __blk_end_request - Helper function for drivers to complete the request.
 * @rq:       the request being processed
 * @error:    0 for success, < 0 for error
 * @nr_bytes: number of bytes to complete
 *
 * Description:
 *     Must be called with queue lock held unlike blk_end_request().
 *
 * Return:
 *     0 - we are done with this request
 *     1 - still buffers pending for this request
 **/
int __blk_end_request(struct request *rq, int error, unsigned int nr_bytes)
{
	if (blk_fs_request(rq) || blk_pc_request(rq)) {
		if (__end_that_request_first(rq, error, nr_bytes))
			return 1;
	}

	add_disk_randomness(rq->rq_disk);

	end_that_request_last(rq, error);

	return 0;
}
EXPORT_SYMBOL_GPL(__blk_end_request);

/**
 * blk_end_bidi_request - Helper function for drivers to complete bidi request.
 * @rq:         the bidi request being processed
 * @error:      0 for success, < 0 for error
 * @nr_bytes:   number of bytes to complete @rq
 * @bidi_bytes: number of bytes to complete @rq->next_rq
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @rq and @rq->next_rq.
 *
 * Return:
 *     0 - we are done with this request
 *     1 - still buffers pending for this request
 **/
int blk_end_bidi_request(struct request *rq, int error, unsigned int nr_bytes,
			 unsigned int bidi_bytes)
{
	return blk_end_io(rq, error, nr_bytes, bidi_bytes, NULL);
}
EXPORT_SYMBOL_GPL(blk_end_bidi_request);

/**
 * blk_end_request_callback - Special helper function for tricky drivers
 * @rq:           the request being processed
 * @error:        0 for success, < 0 for error
 * @nr_bytes:     number of bytes to complete
 * @drv_callback: function called between completion of bios in the request
 *                and completion of the request.
 *                If the callback returns non 0, this helper returns without
 *                completion of the request.
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @rq.
 *     If @rq has leftover, sets it up for the next range of segments.
 *
 *     This special helper function is used only for existing tricky drivers.
 *     (e.g. cdrom_newpc_intr() of ide-cd)
 *     This interface will be removed when such drivers are rewritten.
 *     Don't use this interface in other places anymore.
 *
 * Return:
 *     0 - we are done with this request
 *     1 - this request is not freed yet.
 *         this request still has pending buffers or
 *         the driver doesn't want to finish this request yet.
 **/
int blk_end_request_callback(struct request *rq, int error,
			     unsigned int nr_bytes,
			     int (drv_callback)(struct request *))
{
	return blk_end_io(rq, error, nr_bytes, 0, drv_callback);
}
EXPORT_SYMBOL_GPL(blk_end_request_callback);

void blk_rq_bio_prep(struct request_queue *q, struct request *rq,
		     struct bio *bio)
{
	/* first two bits are identical in rq->cmd_flags and bio->bi_rw */
	rq->cmd_flags |= (bio->bi_rw & 3);

	rq->nr_phys_segments = bio_phys_segments(q, bio);
	rq->nr_hw_segments = bio_hw_segments(q, bio);
	rq->current_nr_sectors = bio_cur_sectors(bio);
	rq->hard_cur_sectors = rq->current_nr_sectors;
	rq->hard_nr_sectors = rq->nr_sectors = bio_sectors(bio);
	rq->buffer = bio_data(bio);
	rq->raw_data_len = bio->bi_size;
	rq->data_len = bio->bi_size;

	rq->bio = rq->biotail = bio;

	if (bio->bi_bdev)
		rq->rq_disk = bio->bi_bdev->bd_disk;
}

int kblockd_schedule_work(struct work_struct *work)
{
	return queue_work(kblockd_workqueue, work);
}
EXPORT_SYMBOL(kblockd_schedule_work);

void kblockd_flush_work(struct work_struct *work)
{
	cancel_work_sync(work);
}
EXPORT_SYMBOL(kblockd_flush_work);

int __init blk_dev_init(void)
{
	int i;

	kblockd_workqueue = create_workqueue("kblockd");
	if (!kblockd_workqueue)
		panic("Failed to create kblockd\n");

	request_cachep = kmem_cache_create("blkdev_requests",
			sizeof(struct request), 0, SLAB_PANIC, NULL);

	blk_requestq_cachep = kmem_cache_create("blkdev_queue",
			sizeof(struct request_queue), 0, SLAB_PANIC, NULL);

	for_each_possible_cpu(i)
		INIT_LIST_HEAD(&per_cpu(blk_cpu_done, i));

	open_softirq(BLOCK_SOFTIRQ, blk_done_softirq, NULL);
	register_hotcpu_notifier(&blk_cpu_notifier);

	return 0;
}

