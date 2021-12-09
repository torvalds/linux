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

/*
 * One of these is allocated per request.
 */
struct dm_rq_target_io {
	struct mapped_device *md;
	struct dm_target *ti;
	struct request *orig, *clone;
	struct kthread_work work;
	blk_status_t error;
	union map_info info;
	struct dm_stats_aux stats_aux;
	unsigned long duration_jiffies;
	unsigned n_sectors;
	unsigned completed;
};

#define DM_MQ_NR_HW_QUEUES 1
#define DM_MQ_QUEUE_DEPTH 2048
static unsigned dm_mq_nr_hw_queues = DM_MQ_NR_HW_QUEUES;
static unsigned dm_mq_queue_depth = DM_MQ_QUEUE_DEPTH;

/*
 * Request-based DM's mempools' reserved IOs set by the user.
 */
#define RESERVED_REQUEST_BASED_IOS	256
static unsigned reserved_rq_based_ios = RESERVED_REQUEST_BASED_IOS;

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
	return queue_is_mq(md->queue);
}

void dm_start_queue(struct request_queue *q)
{
	blk_mq_unquiesce_queue(q);
	blk_mq_kick_requeue_list(q);
}

void dm_stop_queue(struct request_queue *q)
{
	blk_mq_quiesce_queue(q);
}

/*
 * Partial completion handling for request-based dm
 */
static void end_clone_bio(struct bio *clone)
{
	struct dm_rq_clone_bio_info *info =
		container_of(clone, struct dm_rq_clone_bio_info, clone);
	struct dm_rq_target_io *tio = info->tio;
	unsigned int nr_bytes = info->orig->bi_iter.bi_size;
	blk_status_t error = clone->bi_status;
	bool is_last = !clone->bi_next;

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
		goto exit;
	}

	/*
	 * I/O for the bio successfully completed.
	 * Notice the data completion to the upper layer.
	 */
	tio->completed += nr_bytes;

	/*
	 * Update the original request.
	 * Do not use blk_mq_end_request() here, because it may complete
	 * the original request before the clone, and break the ordering.
	 */
	if (is_last)
 exit:
		blk_update_request(tio->orig, BLK_STS_OK, tio->completed);
}

static struct dm_rq_target_io *tio_from_request(struct request *rq)
{
	return blk_mq_rq_to_pdu(rq);
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
static void rq_completed(struct mapped_device *md)
{
	/*
	 * dm_put() must be at the end of this function. See the comment above
	 */
	dm_put(md);
}

/*
 * Complete the clone and the original request.
 * Must be called without clone's queue lock held,
 * see end_clone_request() for more details.
 */
static void dm_end_request(struct request *clone, blk_status_t error)
{
	struct dm_rq_target_io *tio = clone->end_io_data;
	struct mapped_device *md = tio->md;
	struct request *rq = tio->orig;

	blk_rq_unprep_clone(clone);
	tio->ti->type->release_clone_rq(clone, NULL);

	rq_end_stats(md, rq);
	blk_mq_end_request(rq, error);
	rq_completed(md);
}

static void __dm_mq_kick_requeue_list(struct request_queue *q, unsigned long msecs)
{
	blk_mq_delay_kick_requeue_list(q, msecs);
}

void dm_mq_kick_requeue_list(struct mapped_device *md)
{
	__dm_mq_kick_requeue_list(md->queue, 0);
}
EXPORT_SYMBOL(dm_mq_kick_requeue_list);

static void dm_mq_delay_requeue_request(struct request *rq, unsigned long msecs)
{
	blk_mq_requeue_request(rq, false);
	__dm_mq_kick_requeue_list(rq->q, msecs);
}

static void dm_requeue_original_request(struct dm_rq_target_io *tio, bool delay_requeue)
{
	struct mapped_device *md = tio->md;
	struct request *rq = tio->orig;
	unsigned long delay_ms = delay_requeue ? 100 : 0;

	rq_end_stats(md, rq);
	if (tio->clone) {
		blk_rq_unprep_clone(tio->clone);
		tio->ti->type->release_clone_rq(tio->clone, NULL);
	}

	dm_mq_delay_requeue_request(rq, delay_ms);
	rq_completed(md);
}

static void dm_done(struct request *clone, blk_status_t error, bool mapped)
{
	int r = DM_ENDIO_DONE;
	struct dm_rq_target_io *tio = clone->end_io_data;
	dm_request_endio_fn rq_end_io = NULL;

	if (tio->ti) {
		rq_end_io = tio->ti->type->rq_end_io;

		if (mapped && rq_end_io)
			r = rq_end_io(tio->ti, clone, error, &tio->info);
	}

	if (unlikely(error == BLK_STS_TARGET)) {
		if (req_op(clone) == REQ_OP_DISCARD &&
		    !clone->q->limits.max_discard_sectors)
			disable_discard(tio->md);
		else if (req_op(clone) == REQ_OP_WRITE_SAME &&
			 !clone->q->limits.max_write_same_sectors)
			disable_write_same(tio->md);
		else if (req_op(clone) == REQ_OP_WRITE_ZEROES &&
			 !clone->q->limits.max_write_zeroes_sectors)
			disable_write_zeroes(tio->md);
	}

	switch (r) {
	case DM_ENDIO_DONE:
		/* The target wants to complete the I/O */
		dm_end_request(clone, error);
		break;
	case DM_ENDIO_INCOMPLETE:
		/* The target will handle the I/O */
		return;
	case DM_ENDIO_REQUEUE:
		/* The target wants to requeue the I/O */
		dm_requeue_original_request(tio, false);
		break;
	case DM_ENDIO_DELAY_REQUEUE:
		/* The target wants to requeue the I/O after a delay */
		dm_requeue_original_request(tio, true);
		break;
	default:
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

	if (!clone) {
		struct mapped_device *md = tio->md;

		rq_end_stats(md, rq);
		blk_mq_end_request(rq, tio->error);
		rq_completed(md);
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
static void dm_complete_request(struct request *rq, blk_status_t error)
{
	struct dm_rq_target_io *tio = tio_from_request(rq);

	tio->error = error;
	if (likely(!blk_should_fake_timeout(rq->q)))
		blk_mq_complete_request(rq);
}

/*
 * Complete the not-mapped clone and the original request with the error status
 * through softirq context.
 * Target's rq_end_io() function isn't called.
 * This may be used when the target's clone_and_map_rq() function fails.
 */
static void dm_kill_unmapped_request(struct request *rq, blk_status_t error)
{
	rq->rq_flags |= RQF_FAILED;
	dm_complete_request(rq, error);
}

static void end_clone_request(struct request *clone, blk_status_t error)
{
	struct dm_rq_target_io *tio = clone->end_io_data;

	dm_complete_request(tio->orig, error);
}

static blk_status_t dm_dispatch_clone_request(struct request *clone, struct request *rq)
{
	blk_status_t r;

	if (blk_queue_io_stat(clone->q))
		clone->rq_flags |= RQF_IO_STAT;

	clone->start_time_ns = ktime_get_ns();
	r = blk_insert_cloned_request(clone->q, clone);
	if (r != BLK_STS_OK && r != BLK_STS_RESOURCE && r != BLK_STS_DEV_RESOURCE)
		/* must complete clone in terms of original request */
		dm_complete_request(rq, r);
	return r;
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

	r = blk_rq_prep_clone(clone, rq, &tio->md->bs, gfp_mask,
			      dm_rq_bio_constructor, tio);
	if (r)
		return r;

	clone->end_io = end_clone_request;
	clone->end_io_data = tio;

	tio->clone = clone;

	return 0;
}

static void init_tio(struct dm_rq_target_io *tio, struct request *rq,
		     struct mapped_device *md)
{
	tio->md = md;
	tio->ti = NULL;
	tio->clone = NULL;
	tio->orig = rq;
	tio->error = 0;
	tio->completed = 0;
	/*
	 * Avoid initializing info for blk-mq; it passes
	 * target-specific data through info.ptr
	 * (see: dm_mq_init_request)
	 */
	if (!md->init_tio_pdu)
		memset(&tio->info, 0, sizeof(tio->info));
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
	blk_status_t ret;

	r = ti->type->clone_and_map_rq(ti, rq, &tio->info, &clone);
	switch (r) {
	case DM_MAPIO_SUBMITTED:
		/* The target has taken the I/O to submit by itself later */
		break;
	case DM_MAPIO_REMAPPED:
		if (setup_clone(clone, rq, tio, GFP_ATOMIC)) {
			/* -ENOMEM */
			ti->type->release_clone_rq(clone, &tio->info);
			return DM_MAPIO_REQUEUE;
		}

		/* The target has remapped the I/O so dispatch it */
		trace_block_rq_remap(clone, disk_devt(dm_disk(md)),
				     blk_rq_pos(rq));
		ret = dm_dispatch_clone_request(clone, rq);
		if (ret == BLK_STS_RESOURCE || ret == BLK_STS_DEV_RESOURCE) {
			blk_rq_unprep_clone(clone);
			blk_mq_cleanup_rq(clone);
			tio->ti->type->release_clone_rq(clone, &tio->info);
			tio->clone = NULL;
			return DM_MAPIO_REQUEUE;
		}
		break;
	case DM_MAPIO_REQUEUE:
		/* The target wants to requeue the I/O */
		break;
	case DM_MAPIO_DELAY_REQUEUE:
		/* The target wants to requeue the I/O after a delay */
		dm_requeue_original_request(tio, true);
		break;
	case DM_MAPIO_KILL:
		/* The target wants to complete the I/O */
		dm_kill_unmapped_request(rq, BLK_STS_IOERR);
		break;
	default:
		DMWARN("unimplemented target map return value: %d", r);
		BUG();
	}

	return r;
}

/* DEPRECATED: previously used for request-based merge heuristic in dm_request_fn() */
ssize_t dm_attr_rq_based_seq_io_merge_deadline_show(struct mapped_device *md, char *buf)
{
	return sprintf(buf, "%u\n", 0);
}

ssize_t dm_attr_rq_based_seq_io_merge_deadline_store(struct mapped_device *md,
						     const char *buf, size_t count)
{
	return count;
}

static void dm_start_request(struct mapped_device *md, struct request *orig)
{
	blk_mq_start_request(orig);

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

static int dm_mq_init_request(struct blk_mq_tag_set *set, struct request *rq,
			      unsigned int hctx_idx, unsigned int numa_node)
{
	struct mapped_device *md = set->driver_data;
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

static blk_status_t dm_mq_queue_rq(struct blk_mq_hw_ctx *hctx,
			  const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct dm_rq_target_io *tio = blk_mq_rq_to_pdu(rq);
	struct mapped_device *md = tio->md;
	struct dm_target *ti = md->immutable_target;

	/*
	 * blk-mq's unquiesce may come from outside events, such as
	 * elevator switch, updating nr_requests or others, and request may
	 * come during suspend, so simply ask for blk-mq to requeue it.
	 */
	if (unlikely(test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)))
		return BLK_STS_RESOURCE;

	if (unlikely(!ti)) {
		int srcu_idx;
		struct dm_table *map = dm_get_live_table(md, &srcu_idx);

		ti = dm_table_find_target(map, 0);
		dm_put_live_table(md, srcu_idx);
	}

	if (ti->type->busy && ti->type->busy(ti))
		return BLK_STS_RESOURCE;

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
		rq_completed(md);
		return BLK_STS_RESOURCE;
	}

	return BLK_STS_OK;
}

static const struct blk_mq_ops dm_mq_ops = {
	.queue_rq = dm_mq_queue_rq,
	.complete = dm_softirq_done,
	.init_request = dm_mq_init_request,
};

int dm_mq_init_request_queue(struct mapped_device *md, struct dm_table *t)
{
	struct dm_target *immutable_tgt;
	int err;

	md->tag_set = kzalloc_node(sizeof(struct blk_mq_tag_set), GFP_KERNEL, md->numa_node_id);
	if (!md->tag_set)
		return -ENOMEM;

	md->tag_set->ops = &dm_mq_ops;
	md->tag_set->queue_depth = dm_get_blk_mq_queue_depth();
	md->tag_set->numa_node = md->numa_node_id;
	md->tag_set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_STACKING;
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

	err = blk_mq_init_allocated_queue(md->tag_set, md->queue);
	if (err)
		goto out_tag_set;
	return 0;

out_tag_set:
	blk_mq_free_tag_set(md->tag_set);
out_kfree_tag_set:
	kfree(md->tag_set);
	md->tag_set = NULL;

	return err;
}

void dm_mq_cleanup_mapped_device(struct mapped_device *md)
{
	if (md->tag_set) {
		blk_mq_free_tag_set(md->tag_set);
		kfree(md->tag_set);
		md->tag_set = NULL;
	}
}

module_param(reserved_rq_based_ios, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(reserved_rq_based_ios, "Reserved IOs in request-based mempools");

/* Unused, but preserved for userspace compatibility */
static bool use_blk_mq = true;
module_param(use_blk_mq, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(use_blk_mq, "Use block multiqueue for request-based DM devices");

module_param(dm_mq_nr_hw_queues, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dm_mq_nr_hw_queues, "Number of hardware queues for request-based dm-mq devices");

module_param(dm_mq_queue_depth, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dm_mq_queue_depth, "Queue depth for request-based dm-mq devices");
