// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "flush.h"

#include <linux/mempool.h>
#include <linux/spinlock.h>

#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

#include "admin-state.h"
#include "completion.h"
#include "io-submitter.h"
#include "logical-zone.h"
#include "slab-depot.h"
#include "types.h"
#include "vdo.h"

struct flusher {
	struct vdo_completion completion;
	/* The vdo to which this flusher belongs */
	struct vdo *vdo;
	/* The administrative state of the flusher */
	struct admin_state state;
	/* The current flush generation of the vdo */
	sequence_number_t flush_generation;
	/* The first unacknowledged flush generation */
	sequence_number_t first_unacknowledged_generation;
	/* The queue of flush requests waiting to notify other threads */
	struct vdo_wait_queue notifiers;
	/* The queue of flush requests waiting for VIOs to complete */
	struct vdo_wait_queue pending_flushes;
	/* The flush generation for which notifications are being sent */
	sequence_number_t notify_generation;
	/* The logical zone to notify next */
	struct logical_zone *logical_zone_to_notify;
	/* The ID of the thread on which flush requests should be made */
	thread_id_t thread_id;
	/* The pool of flush requests */
	mempool_t *flush_pool;
	/* Bios waiting for a flush request to become available */
	struct bio_list waiting_flush_bios;
	/* The lock to protect the previous fields */
	spinlock_t lock;
	/* The rotor for selecting the bio queue for submitting flush bios */
	zone_count_t bio_queue_rotor;
	/* The number of flushes submitted to the current bio queue */
	int flush_count;
};

/**
 * assert_on_flusher_thread() - Check that we are on the flusher thread.
 * @flusher: The flusher.
 * @caller: The function which is asserting.
 */
static inline void assert_on_flusher_thread(struct flusher *flusher, const char *caller)
{
	VDO_ASSERT_LOG_ONLY((vdo_get_callback_thread_id() == flusher->thread_id),
			    "%s() called from flusher thread", caller);
}

/**
 * as_flusher() - Convert a generic vdo_completion to a flusher.
 * @completion: The completion to convert.
 *
 * Return: The completion as a flusher.
 */
static struct flusher *as_flusher(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VDO_FLUSH_NOTIFICATION_COMPLETION);
	return container_of(completion, struct flusher, completion);
}

/**
 * completion_as_vdo_flush() - Convert a generic vdo_completion to a vdo_flush.
 * @completion: The completion to convert.
 *
 * Return: The completion as a vdo_flush.
 */
static inline struct vdo_flush *completion_as_vdo_flush(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VDO_FLUSH_COMPLETION);
	return container_of(completion, struct vdo_flush, completion);
}

/**
 * vdo_waiter_as_flush() - Convert a vdo_flush's generic wait queue entry back to the vdo_flush.
 * @waiter: The wait queue entry to convert.
 *
 * Return: The wait queue entry as a vdo_flush.
 */
static struct vdo_flush *vdo_waiter_as_flush(struct vdo_waiter *waiter)
{
	return container_of(waiter, struct vdo_flush, waiter);
}

static void *allocate_flush(gfp_t gfp_mask, void *pool_data)
{
	struct vdo_flush *flush = NULL;

	if ((gfp_mask & GFP_NOWAIT) == GFP_NOWAIT) {
		flush = vdo_allocate_memory_nowait(sizeof(struct vdo_flush), __func__);
	} else {
		int result = vdo_allocate(1, struct vdo_flush, __func__, &flush);

		if (result != VDO_SUCCESS)
			vdo_log_error_strerror(result, "failed to allocate spare flush");
	}

	if (flush != NULL) {
		struct flusher *flusher = pool_data;

		vdo_initialize_completion(&flush->completion, flusher->vdo,
					  VDO_FLUSH_COMPLETION);
	}

	return flush;
}

static void free_flush(void *element, void *pool_data __always_unused)
{
	vdo_free(element);
}

/**
 * vdo_make_flusher() - Make a flusher for a vdo.
 * @vdo: The vdo which owns the flusher.
 *
 * Return: VDO_SUCCESS or an error.
 */
int vdo_make_flusher(struct vdo *vdo)
{
	int result = vdo_allocate(1, struct flusher, __func__, &vdo->flusher);

	if (result != VDO_SUCCESS)
		return result;

	vdo->flusher->vdo = vdo;
	vdo->flusher->thread_id = vdo->thread_config.packer_thread;
	vdo_set_admin_state_code(&vdo->flusher->state, VDO_ADMIN_STATE_NORMAL_OPERATION);
	vdo_initialize_completion(&vdo->flusher->completion, vdo,
				  VDO_FLUSH_NOTIFICATION_COMPLETION);

	spin_lock_init(&vdo->flusher->lock);
	bio_list_init(&vdo->flusher->waiting_flush_bios);
	vdo->flusher->flush_pool = mempool_create(1, allocate_flush, free_flush,
						  vdo->flusher);
	return ((vdo->flusher->flush_pool == NULL) ? -ENOMEM : VDO_SUCCESS);
}

/**
 * vdo_free_flusher() - Free a flusher.
 * @flusher: The flusher to free.
 */
void vdo_free_flusher(struct flusher *flusher)
{
	if (flusher == NULL)
		return;

	if (flusher->flush_pool != NULL)
		mempool_destroy(vdo_forget(flusher->flush_pool));
	vdo_free(flusher);
}

/**
 * vdo_get_flusher_thread_id() - Get the ID of the thread on which flusher functions should be
 *                               called.
 * @flusher: The flusher to query.
 *
 * Return: The ID of the thread which handles the flusher.
 */
thread_id_t vdo_get_flusher_thread_id(struct flusher *flusher)
{
	return flusher->thread_id;
}

static void notify_flush(struct flusher *flusher);
static void vdo_complete_flush(struct vdo_flush *flush);

/**
 * finish_notification() - Finish the notification process.
 * @completion: The flusher completion.
 *
 * Finishes the notification process by checking if any flushes have completed and then starting
 * the notification of the next flush request if one came in while the current notification was in
 * progress. This callback is registered in flush_packer_callback().
 */
static void finish_notification(struct vdo_completion *completion)
{
	struct flusher *flusher = as_flusher(completion);

	assert_on_flusher_thread(flusher, __func__);

	vdo_waitq_enqueue_waiter(&flusher->pending_flushes,
				 vdo_waitq_dequeue_waiter(&flusher->notifiers));
	vdo_complete_flushes(flusher);
	if (vdo_waitq_has_waiters(&flusher->notifiers))
		notify_flush(flusher);
}

/**
 * flush_packer_callback() - Flush the packer.
 * @completion: The flusher completion.
 *
 * Flushes the packer now that all of the logical and physical zones have been notified of the new
 * flush request. This callback is registered in increment_generation().
 */
static void flush_packer_callback(struct vdo_completion *completion)
{
	struct flusher *flusher = as_flusher(completion);

	vdo_increment_packer_flush_generation(flusher->vdo->packer);
	vdo_launch_completion_callback(completion, finish_notification,
				       flusher->thread_id);
}

/**
 * increment_generation() - Increment the flush generation in a logical zone.
 * @completion: The flusher as a completion.
 *
 * If there are more logical zones, go on to the next one, otherwise, prepare the physical zones.
 * This callback is registered both in notify_flush() and in itself.
 */
static void increment_generation(struct vdo_completion *completion)
{
	struct flusher *flusher = as_flusher(completion);
	struct logical_zone *zone = flusher->logical_zone_to_notify;

	vdo_increment_logical_zone_flush_generation(zone, flusher->notify_generation);
	if (zone->next == NULL) {
		vdo_launch_completion_callback(completion, flush_packer_callback,
					       flusher->thread_id);
		return;
	}

	flusher->logical_zone_to_notify = zone->next;
	vdo_launch_completion_callback(completion, increment_generation,
				       flusher->logical_zone_to_notify->thread_id);
}

/**
 * notify_flush() - Launch a flush notification.
 * @flusher: The flusher doing the notification.
 */
static void notify_flush(struct flusher *flusher)
{
	struct vdo_flush *flush =
		vdo_waiter_as_flush(vdo_waitq_get_first_waiter(&flusher->notifiers));

	flusher->notify_generation = flush->flush_generation;
	flusher->logical_zone_to_notify = &flusher->vdo->logical_zones->zones[0];
	flusher->completion.requeue = true;
	vdo_launch_completion_callback(&flusher->completion, increment_generation,
				       flusher->logical_zone_to_notify->thread_id);
}

/**
 * flush_vdo() - Start processing a flush request.
 * @completion: A flush request (as a vdo_completion)
 *
 * This callback is registered in launch_flush().
 */
static void flush_vdo(struct vdo_completion *completion)
{
	struct vdo_flush *flush = completion_as_vdo_flush(completion);
	struct flusher *flusher = completion->vdo->flusher;
	bool may_notify;
	int result;

	assert_on_flusher_thread(flusher, __func__);
	result = VDO_ASSERT(vdo_is_state_normal(&flusher->state),
			    "flusher is in normal operation");
	if (result != VDO_SUCCESS) {
		vdo_enter_read_only_mode(flusher->vdo, result);
		vdo_complete_flush(flush);
		return;
	}

	flush->flush_generation = flusher->flush_generation++;
	may_notify = !vdo_waitq_has_waiters(&flusher->notifiers);
	vdo_waitq_enqueue_waiter(&flusher->notifiers, &flush->waiter);
	if (may_notify)
		notify_flush(flusher);
}

/**
 * check_for_drain_complete() - Check whether the flusher has drained.
 * @flusher: The flusher.
 */
static void check_for_drain_complete(struct flusher *flusher)
{
	bool drained;

	if (!vdo_is_state_draining(&flusher->state) ||
	    vdo_waitq_has_waiters(&flusher->pending_flushes))
		return;

	spin_lock(&flusher->lock);
	drained = bio_list_empty(&flusher->waiting_flush_bios);
	spin_unlock(&flusher->lock);

	if (drained)
		vdo_finish_draining(&flusher->state);
}

/**
 * vdo_complete_flushes() - Attempt to complete any flushes which might have finished.
 * @flusher: The flusher.
 */
void vdo_complete_flushes(struct flusher *flusher)
{
	sequence_number_t oldest_active_generation = U64_MAX;
	struct logical_zone *zone;

	assert_on_flusher_thread(flusher, __func__);

	for (zone = &flusher->vdo->logical_zones->zones[0]; zone != NULL; zone = zone->next)
		oldest_active_generation =
			min(oldest_active_generation,
			    READ_ONCE(zone->oldest_active_generation));

	while (vdo_waitq_has_waiters(&flusher->pending_flushes)) {
		struct vdo_flush *flush =
			vdo_waiter_as_flush(vdo_waitq_get_first_waiter(&flusher->pending_flushes));

		if (flush->flush_generation >= oldest_active_generation)
			return;

		VDO_ASSERT_LOG_ONLY((flush->flush_generation ==
				     flusher->first_unacknowledged_generation),
				    "acknowledged next expected flush, %llu, was: %llu",
				    (unsigned long long) flusher->first_unacknowledged_generation,
				    (unsigned long long) flush->flush_generation);
		vdo_waitq_dequeue_waiter(&flusher->pending_flushes);
		vdo_complete_flush(flush);
		flusher->first_unacknowledged_generation++;
	}

	check_for_drain_complete(flusher);
}

/**
 * vdo_dump_flusher() - Dump the flusher, in a thread-unsafe fashion.
 * @flusher: The flusher.
 */
void vdo_dump_flusher(const struct flusher *flusher)
{
	vdo_log_info("struct flusher");
	vdo_log_info("  flush_generation=%llu first_unacknowledged_generation=%llu",
		     (unsigned long long) flusher->flush_generation,
		     (unsigned long long) flusher->first_unacknowledged_generation);
	vdo_log_info("  notifiers queue is %s; pending_flushes queue is %s",
		     (vdo_waitq_has_waiters(&flusher->notifiers) ? "not empty" : "empty"),
		     (vdo_waitq_has_waiters(&flusher->pending_flushes) ? "not empty" : "empty"));
}

/**
 * initialize_flush() - Initialize a vdo_flush structure.
 * @flush: The flush to initialize.
 * @vdo: The vdo being flushed.
 *
 * Initializes a vdo_flush structure, transferring all the bios in the flusher's waiting_flush_bios
 * list to it. The caller MUST already hold the lock.
 */
static void initialize_flush(struct vdo_flush *flush, struct vdo *vdo)
{
	bio_list_init(&flush->bios);
	bio_list_merge(&flush->bios, &vdo->flusher->waiting_flush_bios);
	bio_list_init(&vdo->flusher->waiting_flush_bios);
}

static void launch_flush(struct vdo_flush *flush)
{
	struct vdo_completion *completion = &flush->completion;

	vdo_prepare_completion(completion, flush_vdo, flush_vdo,
			       completion->vdo->thread_config.packer_thread, NULL);
	vdo_enqueue_completion(completion, VDO_DEFAULT_Q_FLUSH_PRIORITY);
}

/**
 * vdo_launch_flush() - Function called to start processing a flush request.
 * @vdo: The vdo.
 * @bio: The bio containing an empty flush request.
 *
 * This is called when we receive an empty flush bio from the block layer, and before acknowledging
 * a non-empty bio with the FUA flag set.
 */
void vdo_launch_flush(struct vdo *vdo, struct bio *bio)
{
	/*
	 * Try to allocate a vdo_flush to represent the flush request. If the allocation fails,
	 * we'll deal with it later.
	 */
	struct vdo_flush *flush = mempool_alloc(vdo->flusher->flush_pool, GFP_NOWAIT);
	struct flusher *flusher = vdo->flusher;
	const struct admin_state_code *code = vdo_get_admin_state_code(&flusher->state);

	VDO_ASSERT_LOG_ONLY(!code->quiescent, "Flushing not allowed in state %s",
			    code->name);

	spin_lock(&flusher->lock);

	/* We have a new bio to start. Add it to the list. */
	bio_list_add(&flusher->waiting_flush_bios, bio);

	if (flush == NULL) {
		spin_unlock(&flusher->lock);
		return;
	}

	/* We have flushes to start. Capture them in the vdo_flush structure. */
	initialize_flush(flush, vdo);
	spin_unlock(&flusher->lock);

	/* Finish launching the flushes. */
	launch_flush(flush);
}

/**
 * release_flush() - Release a vdo_flush structure that has completed its work.
 * @flush: The completed flush structure to re-use or free.
 *
 * If there are any pending flush requests whose vdo_flush allocation failed, they will be launched
 * by immediately re-using the released vdo_flush. If there is no spare vdo_flush, the released
 * structure will become the spare. Otherwise, the vdo_flush will be freed.
 */
static void release_flush(struct vdo_flush *flush)
{
	bool relaunch_flush;
	struct flusher *flusher = flush->completion.vdo->flusher;

	spin_lock(&flusher->lock);
	if (bio_list_empty(&flusher->waiting_flush_bios)) {
		relaunch_flush = false;
	} else {
		/* We have flushes to start. Capture them in a flush request. */
		initialize_flush(flush, flusher->vdo);
		relaunch_flush = true;
	}
	spin_unlock(&flusher->lock);

	if (relaunch_flush) {
		/* Finish launching the flushes. */
		launch_flush(flush);
		return;
	}

	mempool_free(flush, flusher->flush_pool);
}

/**
 * vdo_complete_flush_callback() - Function called to complete and free a flush request, registered
 *                                 in vdo_complete_flush().
 * @completion: The flush request.
 */
static void vdo_complete_flush_callback(struct vdo_completion *completion)
{
	struct vdo_flush *flush = completion_as_vdo_flush(completion);
	struct vdo *vdo = completion->vdo;
	struct bio *bio;

	while ((bio = bio_list_pop(&flush->bios)) != NULL) {
		/*
		 * We're not acknowledging this bio now, but we'll never touch it again, so this is
		 * the last chance to account for it.
		 */
		vdo_count_bios(&vdo->stats.bios_acknowledged, bio);

		/* Update the device, and send it on down... */
		bio_set_dev(bio, vdo_get_backing_device(vdo));
		atomic64_inc(&vdo->stats.flush_out);
		submit_bio_noacct(bio);
	}


	/*
	 * Release the flush structure, freeing it, re-using it as the spare, or using it to launch
	 * any flushes that had to wait when allocations failed.
	 */
	release_flush(flush);
}

/**
 * select_bio_queue() - Select the bio queue on which to finish a flush request.
 * @flusher: The flusher finishing the request.
 */
static thread_id_t select_bio_queue(struct flusher *flusher)
{
	struct vdo *vdo = flusher->vdo;
	zone_count_t bio_threads = flusher->vdo->thread_config.bio_thread_count;
	int interval;

	if (bio_threads == 1)
		return vdo->thread_config.bio_threads[0];

	interval = vdo->device_config->thread_counts.bio_rotation_interval;
	if (flusher->flush_count == interval) {
		flusher->flush_count = 1;
		flusher->bio_queue_rotor = ((flusher->bio_queue_rotor + 1) % bio_threads);
	} else {
		flusher->flush_count++;
	}

	return vdo->thread_config.bio_threads[flusher->bio_queue_rotor];
}

/**
 * vdo_complete_flush() - Complete and free a vdo flush request.
 * @flush: The flush request.
 */
static void vdo_complete_flush(struct vdo_flush *flush)
{
	struct vdo_completion *completion = &flush->completion;

	vdo_prepare_completion(completion, vdo_complete_flush_callback,
			       vdo_complete_flush_callback,
			       select_bio_queue(completion->vdo->flusher), NULL);
	vdo_enqueue_completion(completion, BIO_Q_FLUSH_PRIORITY);
}

/**
 * initiate_drain() - Initiate a drain.
 *
 * Implements vdo_admin_initiator_fn.
 */
static void initiate_drain(struct admin_state *state)
{
	check_for_drain_complete(container_of(state, struct flusher, state));
}

/**
 * vdo_drain_flusher() - Drain the flusher.
 * @flusher: The flusher to drain.
 * @completion: The completion to finish when the flusher has drained.
 *
 * Drains the flusher by preventing any more VIOs from entering the flusher and then flushing. The
 * flusher will be left in the suspended state.
 */
void vdo_drain_flusher(struct flusher *flusher, struct vdo_completion *completion)
{
	assert_on_flusher_thread(flusher, __func__);
	vdo_start_draining(&flusher->state, VDO_ADMIN_STATE_SUSPENDING, completion,
			   initiate_drain);
}

/**
 * vdo_resume_flusher() - Resume a flusher which has been suspended.
 * @flusher: The flusher to resume.
 * @parent: The completion to finish when the flusher has resumed.
 */
void vdo_resume_flusher(struct flusher *flusher, struct vdo_completion *parent)
{
	assert_on_flusher_thread(flusher, __func__);
	vdo_continue_completion(parent, vdo_resume_if_quiescent(&flusher->state));
}
