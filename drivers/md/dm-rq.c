/*
 * Copyright (C) 2016 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm-core.h"
#include "dm-rq.h"

#include <linux/elevator.h> /* for rq_end_sector() */
#include <linux/blk-mq.h>

#define DM_MSG_PREFIX "core-rq"

#define DM_MQ_NR_HW_QUEUES 1
#define DM_MQ_QUEUE_DEPTH 2048
static unsigned dm_mq_nr_hw_queues = DM_MQ_NR_HW_QUEUES;
static unsigned dm_mq_queue_depth = DM_MQ_QUEUE_DEPTH;

/*
 * Request-based DM's mempools' reserved IOs set by the user.
 */
#define RESERVED_REQUEST_BASED_IOS	256
static unsigned reserved_rq_based_ios = RESERVED_REQUEST_BASED_IOS;

#ifdef CONFIG_DM_MQ_DEFAULT
static bool use_blk_mq = true;
#else
static bool use_blk_mq = false;
#endif

bool dm_use_blk_mq_default(void)
{
	return use_blk_mq;
}

bool dm_use_blk_mq(struct mapped_device *md)
{
	return md->use_blk_mq;
}
EXPORT_SYMBOL_GPL(dm_use_blk_mq);

unsigned dm_get_reserved_rq_based_ios(void)
{
	return __dm_get_module_param(&reserved_rq_based_ios,
				     RESERVED_REQUEST_BASED_IOS, DM_RESERVED_MAX_IOS);
}
EXPORT_SYMBOL_GPL(dm_get_reserved_rq_based_ios);

static unsigned dm_get_blk_mq_nr_hw_queues(void)
{
	return __dm_get_module_param(&dm_mq_nr_hw_queues, 1, 32);
}

static unsigned dm_get_blk_mq_queue_depth(void)
{
	return __dm_get_module_param(&dm_mq_queue_depth,
				     DM_MQ_QUEUE_DEPTH, BLK_MQ_MAX_DEPTH);
}

int dm_request_based(struct mapped_device *md)
{
	return blk_queue_stackable(md->queue);
}

static void dm_old_start_queue(struct request_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	if (blk_queue_stopped(q))
		blk_start_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static void dm_mq_start_queue(struct request_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	queue_flag_clear(QUEUE_FLAG_STOPPED, q);
	spin_unlock_irqrestore(q->queue_lock, flags);

	blk_mq_start_stopped_hw_queues(q, true);
	blk_mq_kick_requeue_list(q);
}

void dm_start_queue(struct request_queue *q)
{
	if (!q->mq_ops)
		dm_old_start_queue(q);
	else
		dm_mq_start_queue(q);
}

static void dm_old_stop_queue(struct request_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	if (!blk_queue_stopped(q))
		blk_stop_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static void dm_mq_stop_queue(struct request_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	if (blk_queue_stopped(q)) {
		spin_unlock_irqrestore(q->queue_lock, flags);
		return;
	}

	queue_flag_set(QUEUE_FLAG_STOPPED, q);
	spin_unlock_irqrestore(q->queue_lock, flags);

	/* Avoid that requeuing could restart the queue. */
	blk_mq_cancel_requeue_work(q);
	blk_mq_stop_hw_queues(q);
}

void dm_stop_queue(struct request_queue *q)
{
	if (!q->mq_ops)
		dm_old_stop_queue(q);
	else
		dm_mq_stop_queue(q);
}

static struct dm_rq_target_io *alloc_old_rq_tio(struct mapped_device *md,
						gfp_t gfp_mask)
{
	return mempool_alloc(md->io_pool, gfp_mask);
}

static void free_old_rq_tio(struct dm_rq_target_io *tio)
{
	mempool_free(tio, tio->md->io_pool);
}

static struct request *alloc_old_clone_request(struct mapped_device *md,
					       gfp_t gfp_mask)
{
	return mempool_alloc(md->rq_pool, gfp_mask);
}

static void free_old_clone_request(struct mapped_device *md, struct request *rq)
{
	mempool_free(rq, md->rq_pool);
}

/*
 * Partial completion handling for request-based dm
 */
static void end_clone_bio(struct bio *clone)
{
	struct dm_rq_clone_bio_info *info =
		container_of(clone, struct dm_rq_clone_bio_info, clone);
	struct dm_rq_target_io *tio = info->tio;
	struct bio *bio = info->orig;
	unsigned int nr_bytes = info->orig->bi_iter.bi_size;
	int error = clone->bi_error;

	bio_put(clone);

	if (tio->error)
		/*
		 * An error has already been detected on the request.
		 * Once error occurred, just let clone->end_io() handle
		 * the remainder.
		 */
		return;
	else if (error) {
		/*
		 * Don't notice the error to the upper layer yet.
		 * The error handling decision is made by the target driver,
		 * when the request is completed.
		 */
		tio->error = error;
		return;
	}

	/*
	 * I/O for the bio successfully completed.
	 * Notice the data completion to the upper layer.
	 */

	/*
	 * bios are processed from the head of the list.
	 * So the completing bio should always be rq->bio.
	 * If it's not, something wrong is happening.
	 */
	if (tio->orig->bio != bio)
		DMERR("bio completion is going in the middle of the request");

	/*
	 * Update the original request.
	 * Do not use blk_end_request() here, because it may complete
	 * the original request before the clone, and break the ordering.
	 */
	blk_update_request(tio->orig, 0, nr_bytes);
}

static struct dm_rq_target_io *tio_from_request(struct request *rq)
{
	return (rq->q->mq_ops ? blk_mq_rq_to_pdu(rq) : rq->special);
}

static void rq_end_stats(struct mapped_device *md, struct request *orig)
{
	if (unlikely(dm_stats_used(&md->stats))) {
		struct dm_rq_target_io *tio = tio_from_request(orig);
		tio->duration_jiffies = jiffies - tio->duration_jiffies;
		dm_stats_account_io(&md->stats, rq_data_dir(orig),
				    blk_rq_pos(orig), tio->n_sectors, true,
				    tio->duration_jiffies, &tio->stats_aux);
	}
}

/*
 * Don't touch any member of the md after calling this function because
 * the md may be freed in dm_put() at the end of this function.
 * Or do dm_get() before calling this function and dm_put() later.
 */
static void rq_completed(struct mapped_device *md, int rw, bool run_queue)
{
	atomic_dec(&md->pending[rw]);

	/* nudge anyone waiting on suspend queue */
	if (!md_in_flight(md))
		wake_up(&md->wait);

	/*
	 * Run this off this callpath, as drivers could invoke end_io while
	 * inside their request_fn (and holding the queue lock). Calling
	 * back into ->request_fn() could deadlock attempting to grab the
	 * queue lock again.
	 */
	if (!md->queue->mq_ops && run_queue)
		blk_run_queue_async(md->queue);

	/*
	 * dm_put() must be at the end of this function. See the comment above
	 */
	dm_put(md);
}

static void free_rq_clone(struct request *clone)
{
	struct dm_rq_target_io *tio = clone->end_io_data;
	struct mapped_device *md = tio->md;

	blk_rq_unprep_clone(clone);

	/*
	 * It is possible for a clone_old_rq() allocated clone to
	 * get passed in -- it may not yet have a request_queue.
	 * This is known to occur if the error target replaces
	 * a multipath target that has a request_fn queue stacked
	 * on blk-mq queue(s).
	 */
	if (clone->q && clone->q->mq_ops)
		/* stacked on blk-mq queue(s) */
		tio->ti->type->release_clone_rq(clone);
	else if (!md->queue->mq_ops)
		/* request_fn queue stacked on request_fn queue(s) */
		free_old_clone_request(md, clone);

	if (!md->queue->mq_ops)
		free_old_rq_tio(tio);
}

/*
 * Complete the clone and the original request.
 * Must be called without clone's queue lock held,
 * see end_clone_request() for more details.
 */
static void dm_end_request(struct request *clone, int error)
{
	int rw = rq_data_dir(clone);
	struct dm_rq_target_io *tio = clone->end_io_data;
	struct mapped_device *md = tio->md;
	struct request *rq = tio->orig;

	if (rq->cmd_type == REQ_TYPE_BLOCK_PC) {
		rq->errors = clone->errors;
		rq->resid_len = clone->resid_len;

		if (rq->sense)
			/*
			 * We are using the sense buffer of the original
			 * request.
			 * So setting the length of the sense data is enough.
			 */
			rq->sense_len = clone->sense_len;
	}

	free_rq_clone(clone);
	rq_end_stats(md, rq);
	if (!rq->q->mq_ops)
		blk_end_request_all(rq, error);
	else
		blk_mq_end_request(rq, error);
	rq_completed(md, rw, true);
}

static void dm_unprep_request(struct request *rq)
{
	struct dm_rq_target_io *tio = tio_from_request(rq);
	struct request *clone = tio->clone;

	if (!rq->q->mq_ops) {
		rq->special = NULL;
		rq->rq_flags &= ~RQF_DONTPREP;
	}

	if (clone)
		free_rq_clone(clone);
	else if (!tio->md->queue->mq_ops)
		free_old_rq_tio(tio);
}

/*
 * Requeue the original request of a clone.
 */
static void dm_old_requeue_request(struct request *rq)
{
	struct request_queue *q = rq->q;
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	blk_requeue_request(q, rq);
	blk_run_queue_async(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static void __dm_mq_kick_requeue_list(struct request_queue *q, unsigned long msecs)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	if (!blk_queue_stopped(q))
		blk_mq_delay_kick_requeue_list(q, msecs);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

void dm_mq_kick_requeue_list(struct mapped_device *md)
{
	__dm_mq_kick_requeue_list(dm_get_md_queue(md), 0);
}
EXPORT_SYMBOL(dm_mq_kick_requeue_list);

static void dm_mq_delay_requeue_request(struct request *rq, unsigned long msecs)
{
	blk_mq_requeue_request(rq);
	__dm_mq_kick_requeue_list(rq->q, msecs);
}

static void dm_requeue_original_request(struct dm_rq_target_io *tio, bool delay_requeue)
{
	struct mapped_device *md = tio->md;
	struct request *rq = tio->orig;
	int rw = rq_data_dir(rq);

	rq_end_stats(md, rq);
	dm_unprep_request(rq);

	if (!rq->q->mq_ops)
		dm_old_requeue_request(rq);
	else
		dm_mq_delay_requeue_request(rq, delay_requeue ? 5000 : 0);

	rq_completed(md, rw, false);
}

static void dm_done(struct request *clone, int error, bool mapped)
{
	int r = error;
	struct dm_rq_target_io *tio = clone->end_io_data;
	dm_request_endio_fn rq_end_io = NULL;

	if (tio->ti) {
		rq_end_io = tio->ti->type->rq_end_io;

		if (mapped && rq_end_io)
			r = rq_end_io(tio->ti, clone, error, &tio->info);
	}

	if (unlikely(r == -EREMOTEIO && (req_op(clone) == REQ_OP_WRITE_SAME) &&
		     !clone->q->limits.max_write_same_sectors))
		disable_write_same(tio->md);

	if (r <= 0)
		/* The target wants to complete the I/O */
		dm_end_request(clone, r);
	else if (r == DM_ENDIO_INCOMPLETE)
		/* The target will handle the I/O */
		return;
	else if (r == DM_ENDIO_REQUEUE)
		/* The target wants to requeue the I/O */
		dm_requeue_original_request(tio, false);
	else {
		DMWARN("unimplemented target endio return value: %d", r);
		BUG();
	}
}

/*
 * Request completion handler for request-based dm
 */
static void dm_softirq_done(struct request *rq)
{
	bool mapped = true;
	struct dm_rq_target_io *tio = tio_from_request(rq);
	struct request *clone = tio->clone;
	int rw;

	if (!clone) {
		rq_end_stats(tio->md, rq);
		rw = rq_data_dir(rq);
		if (!rq->q->mq_ops) {
			blk_end_request_all(rq, tio->error);
			rq_completed(tio->md, rw, false);
			free_old_rq_tio(tio);
		} else {
			blk_mq_end_request(rq, tio->error);
			rq_completed(tio->md, rw, false);
		}
		return;
	}

	if (rq->rq_flags & RQF_FAILED)
		mapped = false;

	dm_done(clone, tio->error, mapped);
}

/*
 * Complete the clone and the original request with the error status
 * through softirq context.
 */
static void dm_complete_request(struct request *rq, int error)
{
	struct dm_rq_target_io *tio = tio_from_request(rq);

	tio->error = error;
	if (!rq->q->mq_ops)
		blk_complete_request(rq);
	else
		blk_mq_complete_request(rq, error);
}

/*
 * Complete the not-mapped clone and the original request with the error status
 * through softirq context.
 * Target's rq_end_io() function isn't called.
 * This may be used when the target's map_rq() or clone_and_map_rq() functions fail.
 */
static void dm_kill_unmapped_request(struct request *rq, int error)
{
	rq->rq_flags |= RQF_FAILED;
	dm_complete_request(rq, error);
}

/*
 * Called with the clone's queue lock held (in the case of .request_fn)
 */
static void end_clone_request(struct request *clone, int error)
{
	struct dm_rq_target_io *tio = clone->end_io_data;

	if (!clone->q->mq_ops) {
		/*
		 * For just cleaning up the information of the queue in which
		 * the clone was dispatched.
		 * The clone is *NOT* freed actually here because it is alloced
		 * from dm own mempool (RQF_ALLOCED isn't set).
		 */
		__blk_put_request(clone->q, clone);
	}

	/*
	 * Actual request completion is done in a softirq context which doesn't
	 * hold the clone's queue lock.  Otherwise, deadlock could occur because:
	 *     - another request may be submitted by the upper level driver
	 *       of the stacking during the completion
	 *     - the submission which requires queue lock may be done
	 *       against this clone's queue
	 */
	dm_complete_request(tio->orig, error);
}

static void dm_dispatch_clone_request(struct request *clone, struct request *rq)
{
	int r;

	if (blk_queue_io_stat(clone->q))
		clone->rq_flags |= RQF_IO_STAT;

	clone->start_time = jiffies;
	r = blk_insert_cloned_request(clone->q, clone);
	if (r)
		/* must complete clone in terms of original request */
		dm_complete_request(rq, r);
}

static int dm_rq_bio_constructor(struct bio *bio, struct bio *bio_orig,
				 void *data)
{
	struct dm_rq_target_io *tio = data;
	struct dm_rq_clone_bio_info *info =
		container_of(bio, struct dm_rq_clone_bio_info, clone);

	info->orig = bio_orig;
	info->tio = tio;
	bio->bi_end_io = end_clone_bio;

	return 0;
}

static int setup_clone(struct request *clone, struct request *rq,
		       struct dm_rq_target_io *tio, gfp_t gfp_mask)
{
	int r;

	r = blk_rq_prep_clone(clone, rq, tio->md->bs, gfp_mask,
			      dm_rq_bio_constructor, tio);
	if (r)
		return r;

	clone->cmd = rq->cmd;
	clone->cmd_len = rq->cmd_len;
	clone->sense = rq->sense;
	clone->end_io = end_clone_request;
	clone->end_io_data = tio;

	tio->clone = clone;

	return 0;
}

static struct request *clone_old_rq(struct request *rq, struct mapped_device *md,
				    struct dm_rq_target_io *tio, gfp_t gfp_mask)
{
	/*
	 * Create clone for use with .request_fn request_queue
	 */
	struct request *clone;

	clone = alloc_old_clone_request(md, gfp_mask);
	if (!clone)
		return NULL;

	blk_rq_init(NULL, clone);
	if (setup_clone(clone, rq, tio, gfp_mask)) {
		/* -ENOMEM */
		free_old_clone_request(md, clone);
		return NULL;
	}

	return clone;
}

static void map_tio_request(struct kthread_work *work);

static void init_tio(struct dm_rq_target_io *tio, struct request *rq,
		     struct mapped_device *md)
{
	tio->md = md;
	tio->ti = NULL;
	tio->clone = NULL;
	tio->orig = rq;
	tio->error = 0;
	/*
	 * Avoid initializing info for blk-mq; it passes
	 * target-specific data through info.ptr
	 * (see: dm_mq_init_request)
	 */
	if (!md->init_tio_pdu)
		memset(&tio->info, 0, sizeof(tio->info));
	if (md->kworker_task)
		kthread_init_work(&tio->work, map_tio_request);
}

static struct dm_rq_target_io *dm_old_prep_tio(struct request *rq,
					       struct mapped_device *md,
					       gfp_t gfp_mask)
{
	struct dm_rq_target_io *tio;
	int srcu_idx;
	struct dm_table *table;

	tio = alloc_old_rq_tio(md, gfp_mask);
	if (!tio)
		return NULL;

	init_tio(tio, rq, md);

	table = dm_get_live_table(md, &srcu_idx);
	/*
	 * Must clone a request if this .request_fn DM device
	 * is stacked on .request_fn device(s).
	 */
	if (!dm_table_all_blk_mq_devices(table)) {
		if (!clone_old_rq(rq, md, tio, gfp_mask)) {
			dm_put_live_table(md, srcu_idx);
			free_old_rq_tio(tio);
			return NULL;
		}
	}
	dm_put_live_table(md, srcu_idx);

	return tio;
}

/*
 * Called with the queue lock held.
 */
static int dm_old_prep_fn(struct request_queue *q, struct request *rq)
{
	struct mapped_device *md = q->queuedata;
	struct dm_rq_target_io *tio;

	if (unlikely(rq->special)) {
		DMWARN("Already has something in rq->special.");
		return BLKPREP_KILL;
	}

	tio = dm_old_prep_tio(rq, md, GFP_ATOMIC);
	if (!tio)
		return BLKPREP_DEFER;

	rq->special = tio;
	rq->rq_flags |= RQF_DONTPREP;

	return BLKPREP_OK;
}

/*
 * Returns:
 * DM_MAPIO_*       : the request has been processed as indicated
 * DM_MAPIO_REQUEUE : the original request needs to be immediately requeued
 * < 0              : the request was completed due to failure
 */
static int map_request(struct dm_rq_target_io *tio)
{
	int r;
	struct dm_target *ti = tio->ti;
	struct mapped_device *md = tio->md;
	struct request *rq = tio->orig;
	struct request *clone = NULL;

	if (tio->clone) {
		clone = tio->clone;
		r = ti->type->map_rq(ti, clone, &tio->info);
		if (r == DM_MAPIO_DELAY_REQUEUE)
			return DM_MAPIO_REQUEUE; /* .request_fn requeue is always immediate */
	} else {
		r = ti->type->clone_and_map_rq(ti, rq, &tio->info, &clone);
		if (r < 0) {
			/* The target wants to complete the I/O */
			dm_kill_unmapped_request(rq, r);
			return r;
		}
		if (r == DM_MAPIO_REMAPPED &&
		    setup_clone(clone, rq, tio, GFP_ATOMIC)) {
			/* -ENOMEM */
			ti->type->release_clone_rq(clone);
			return DM_MAPIO_REQUEUE;
		}
	}

	switch (r) {
	case DM_MAPIO_SUBMITTED:
		/* The target has taken the I/O to submit by itself later */
		break;
	case DM_MAPIO_REMAPPED:
		/* The target has remapped the I/O so dispatch it */
		trace_block_rq_remap(clone->q, clone, disk_devt(dm_disk(md)),
				     blk_rq_pos(rq));
		dm_dispatch_clone_request(clone, rq);
		break;
	case DM_MAPIO_REQUEUE:
		/* The target wants to requeue the I/O */
		break;
	case DM_MAPIO_DELAY_REQUEUE:
		/* The target wants to requeue the I/O after a delay */
		dm_requeue_original_request(tio, true);
		break;
	default:
		if (r > 0) {
			DMWARN("unimplemented target map return value: %d", r);
			BUG();
		}

		/* The target wants to complete the I/O */
		dm_kill_unmapped_request(rq, r);
	}

	return r;
}

static void dm_start_request(struct mapped_device *md, struct request *orig)
{
	if (!orig->q->mq_ops)
		blk_start_request(orig);
	else
		blk_mq_start_request(orig);
	atomic_inc(&md->pending[rq_data_dir(orig)]);

	if (md->seq_rq_merge_deadline_usecs) {
		md->last_rq_pos = rq_end_sector(orig);
		md->last_rq_rw = rq_data_dir(orig);
		md->last_rq_start_time = ktime_get();
	}

	if (unlikely(dm_stats_used(&md->stats))) {
		struct dm_rq_target_io *tio = tio_from_request(orig);
		tio->duration_jiffies = jiffies;
		tio->n_sectors = blk_rq_sectors(orig);
		dm_stats_account_io(&md->stats, rq_data_dir(orig),
				    blk_rq_pos(orig), tio->n_sectors, false, 0,
				    &tio->stats_aux);
	}

	/*
	 * Hold the md reference here for the in-flight I/O.
	 * We can't rely on the reference count by device opener,
	 * because the device may be closed during the request completion
	 * when all bios are completed.
	 * See the comment in rq_completed() too.
	 */
	dm_get(md);
}

static void map_tio_request(struct kthread_work *work)
{
	struct dm_rq_target_io *tio = container_of(work, struct dm_rq_target_io, work);

	if (map_request(tio) == DM_MAPIO_REQUEUE)
		dm_requeue_original_request(tio, false);
}

ssize_t dm_attr_rq_based_seq_io_merge_deadline_show(struct mapped_device *md, char *buf)
{
	return sprintf(buf, "%u\n", md->seq_rq_merge_deadline_usecs);
}

#define MAX_SEQ_RQ_MERGE_DEADLINE_USECS 100000

ssize_t dm_attr_rq_based_seq_io_merge_deadline_store(struct mapped_device *md,
						     const char *buf, size_t count)
{
	unsigned deadline;

	if (dm_get_md_type(md) != DM_TYPE_REQUEST_BASED)
		return count;

	if (kstrtouint(buf, 10, &deadline))
		return -EINVAL;

	if (deadline > MAX_SEQ_RQ_MERGE_DEADLINE_USECS)
		deadline = MAX_SEQ_RQ_MERGE_DEADLINE_USECS;

	md->seq_rq_merge_deadline_usecs = deadline;

	return count;
}

static bool dm_old_request_peeked_before_merge_deadline(struct mapped_device *md)
{
	ktime_t kt_deadline;

	if (!md->seq_rq_merge_deadline_usecs)
		return false;

	kt_deadline = ns_to_ktime((u64)md->seq_rq_merge_deadline_usecs * NSEC_PER_USEC);
	kt_deadline = ktime_add_safe(md->last_rq_start_time, kt_deadline);

	return !ktime_after(ktime_get(), kt_deadline);
}

/*
 * q->request_fn for old request-based dm.
 * Called with the queue lock held.
 */
static void dm_old_request_fn(struct request_queue *q)
{
	struct mapped_device *md = q->queuedata;
	struct dm_target *ti = md->immutable_target;
	struct request *rq;
	struct dm_rq_target_io *tio;
	sector_t pos = 0;

	if (unlikely(!ti)) {
		int srcu_idx;
		struct dm_table *map = dm_get_live_table(md, &srcu_idx);

		ti = dm_table_find_target(map, pos);
		dm_put_live_table(md, srcu_idx);
	}

	/*
	 * For suspend, check blk_queue_stopped() and increment
	 * ->pending within a single queue_lock not to increment the
	 * number of in-flight I/Os after the queue is stopped in
	 * dm_suspend().
	 */
	while (!blk_queue_stopped(q)) {
		rq = blk_peek_request(q);
		if (!rq)
			return;

		/* always use block 0 to find the target for flushes for now */
		pos = 0;
		if (req_op(rq) != REQ_OP_FLUSH)
			pos = blk_rq_pos(rq);

		if ((dm_old_request_peeked_before_merge_deadline(md) &&
		     md_in_flight(md) && rq->bio && rq->bio->bi_vcnt == 1 &&
		     md->last_rq_pos == pos && md->last_rq_rw == rq_data_dir(rq)) ||
		    (ti->type->busy && ti->type->busy(ti))) {
			blk_delay_queue(q, 10);
			return;
		}

		dm_start_request(md, rq);

		tio = tio_from_request(rq);
		/* Establish tio->ti before queuing work (map_tio_request) */
		tio->ti = ti;
		kthread_queue_work(&md->kworker, &tio->work);
		BUG_ON(!irqs_disabled());
	}
}

/*
 * Fully initialize a .request_fn request-based queue.
 */
int dm_old_init_request_queue(struct mapped_device *md)
{
	/* Fully initialize the queue */
	if (!blk_init_allocated_queue(md->queue, dm_old_request_fn, NULL))
		return -EINVAL;

	/* disable dm_old_request_fn's merge heuristic by default */
	md->seq_rq_merge_deadline_usecs = 0;

	dm_init_normal_md_queue(md);
	blk_queue_softirq_done(md->queue, dm_softirq_done);
	blk_queue_prep_rq(md->queue, dm_old_prep_fn);

	/* Initialize the request-based DM worker thread */
	kthread_init_worker(&md->kworker);
	md->kworker_task = kthread_run(kthread_worker_fn, &md->kworker,
				       "kdmwork-%s", dm_device_name(md));
	if (IS_ERR(md->kworker_task))
		return PTR_ERR(md->kworker_task);

	elv_register_queue(md->queue);

	return 0;
}

static int dm_mq_init_request(void *data, struct request *rq,
		       unsigned int hctx_idx, unsigned int request_idx,
		       unsigned int numa_node)
{
	struct mapped_device *md = data;
	struct dm_rq_target_io *tio = blk_mq_rq_to_pdu(rq);

	/*
	 * Must initialize md member of tio, otherwise it won't
	 * be available in dm_mq_queue_rq.
	 */
	tio->md = md;

	if (md->init_tio_pdu) {
		/* target-specific per-io data is immediately after the tio */
		tio->info.ptr = tio + 1;
	}

	return 0;
}

static int dm_mq_queue_rq(struct blk_mq_hw_ctx *hctx,
			  const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct dm_rq_target_io *tio = blk_mq_rq_to_pdu(rq);
	struct mapped_device *md = tio->md;
	struct dm_target *ti = md->immutable_target;

	if (unlikely(!ti)) {
		int srcu_idx;
		struct dm_table *map = dm_get_live_table(md, &srcu_idx);

		ti = dm_table_find_target(map, 0);
		dm_put_live_table(md, srcu_idx);
	}

	/*
	 * On suspend dm_stop_queue() handles stopping the blk-mq
	 * request_queue BUT: even though the hw_queues are marked
	 * BLK_MQ_S_STOPPED at that point there is still a race that
	 * is allowing block/blk-mq.c to call ->queue_rq against a
	 * hctx that it really shouldn't.  The following check guards
	 * against this rarity (albeit _not_ race-free).
	 */
	if (unlikely(test_bit(BLK_MQ_S_STOPPED, &hctx->state)))
		return BLK_MQ_RQ_QUEUE_BUSY;

	if (ti->type->busy && ti->type->busy(ti))
		return BLK_MQ_RQ_QUEUE_BUSY;

	dm_start_request(md, rq);

	/* Init tio using md established in .init_request */
	init_tio(tio, rq, md);

	/*
	 * Establish tio->ti before calling map_request().
	 */
	tio->ti = ti;

	/* Direct call is fine since .queue_rq allows allocations */
	if (map_request(tio) == DM_MAPIO_REQUEUE) {
		/* Undo dm_start_request() before requeuing */
		rq_end_stats(md, rq);
		rq_completed(md, rq_data_dir(rq), false);
		return BLK_MQ_RQ_QUEUE_BUSY;
	}

	return BLK_MQ_RQ_QUEUE_OK;
}

static struct blk_mq_ops dm_mq_ops = {
	.queue_rq = dm_mq_queue_rq,
	.complete = dm_softirq_done,
	.init_request = dm_mq_init_request,
};

int dm_mq_init_request_queue(struct mapped_device *md, struct dm_table *t)
{
	struct request_queue *q;
	struct dm_target *immutable_tgt;
	int err;

	if (!dm_table_all_blk_mq_devices(t)) {
		DMERR("request-based dm-mq may only be stacked on blk-mq device(s)");
		return -EINVAL;
	}

	md->tag_set = kzalloc_node(sizeof(struct blk_mq_tag_set), GFP_KERNEL, md->numa_node_id);
	if (!md->tag_set)
		return -ENOMEM;

	md->tag_set->ops = &dm_mq_ops;
	md->tag_set->queue_depth = dm_get_blk_mq_queue_depth();
	md->tag_set->numa_node = md->numa_node_id;
	md->tag_set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_SG_MERGE;
	md->tag_set->nr_hw_queues = dm_get_blk_mq_nr_hw_queues();
	md->tag_set->driver_data = md;

	md->tag_set->cmd_size = sizeof(struct dm_rq_target_io);
	immutable_tgt = dm_table_get_immutable_target(t);
	if (immutable_tgt && immutable_tgt->per_io_data_size) {
		/* any target-specific per-io data is immediately after the tio */
		md->tag_set->cmd_size += immutable_tgt->per_io_data_size;
		md->init_tio_pdu = true;
	}

	err = blk_mq_alloc_tag_set(md->tag_set);
	if (err)
		goto out_kfree_tag_set;

	q = blk_mq_init_allocated_queue(md->tag_set, md->queue);
	if (IS_ERR(q)) {
		err = PTR_ERR(q);
		goto out_tag_set;
	}
	dm_init_md_queue(md);

	/* backfill 'mq' sysfs registration normally done in blk_register_queue */
	blk_mq_register_dev(disk_to_dev(md->disk), q);

	return 0;

out_tag_set:
	blk_mq_free_tag_set(md->tag_set);
out_kfree_tag_set:
	kfree(md->tag_set);

	return err;
}

void dm_mq_cleanup_mapped_device(struct mapped_device *md)
{
	if (md->tag_set) {
		blk_mq_free_tag_set(md->tag_set);
		kfree(md->tag_set);
	}
}

module_param(reserved_rq_based_ios, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(reserved_rq_based_ios, "Reserved IOs in request-based mempools");

module_param(use_blk_mq, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(use_blk_mq, "Use block multiqueue for request-based DM devices");

module_param(dm_mq_nr_hw_queues, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dm_mq_nr_hw_queues, "Number of hardware queues for request-based dm-mq devices");

module_param(dm_mq_queue_depth, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dm_mq_queue_depth, "Queue depth for request-based dm-mq devices");
