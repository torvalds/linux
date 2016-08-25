/*
 * Copyright (C) 2013-2014, 2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_timeline_fence_wait.h"

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_spinlock_reentrant.h"

/**
 * Allocate a fence waiter tracker.
 *
 * @return New fence waiter if successful, NULL if not.
 */
static struct mali_timeline_fence_wait_tracker *mali_timeline_fence_wait_tracker_alloc(void)
{
	return (struct mali_timeline_fence_wait_tracker *) _mali_osk_calloc(1, sizeof(struct mali_timeline_fence_wait_tracker));
}

/**
 * Free fence waiter tracker.
 *
 * @param wait Fence wait tracker to free.
 */
static void mali_timeline_fence_wait_tracker_free(struct mali_timeline_fence_wait_tracker *wait)
{
	MALI_DEBUG_ASSERT_POINTER(wait);
	_mali_osk_atomic_term(&wait->refcount);
	_mali_osk_free(wait);
}

/**
 * Check if fence wait tracker has been activated.  Used as a wait queue condition.
 *
 * @param data Fence waiter.
 * @return MALI_TRUE if tracker has been activated, MALI_FALSE if not.
 */
static mali_bool mali_timeline_fence_wait_tracker_is_activated(void *data)
{
	struct mali_timeline_fence_wait_tracker *wait;

	wait = (struct mali_timeline_fence_wait_tracker *) data;
	MALI_DEBUG_ASSERT_POINTER(wait);

	return wait->activated;
}

/**
 * Check if fence has been signaled.
 *
 * @param system Timeline system.
 * @param fence Timeline fence.
 * @return MALI_TRUE if fence is signaled, MALI_FALSE if not.
 */
static mali_bool mali_timeline_fence_wait_check_status(struct mali_timeline_system *system, struct mali_timeline_fence *fence)
{
	int i;
	u32 tid = _mali_osk_get_tid();
	mali_bool ret = MALI_TRUE;
#if defined(CONFIG_SYNC)
	struct sync_fence *sync_fence = NULL;
#endif

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(fence);

	mali_spinlock_reentrant_wait(system->spinlock, tid);

	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		struct mali_timeline *timeline;
		mali_timeline_point   point;

		point = fence->points[i];

		if (likely(MALI_TIMELINE_NO_POINT == point)) {
			/* Fence contains no point on this timeline. */
			continue;
		}

		timeline = system->timelines[i];
		MALI_DEBUG_ASSERT_POINTER(timeline);

		if (unlikely(!mali_timeline_is_point_valid(timeline, point))) {
			MALI_PRINT_ERROR(("Mali Timeline: point %d is not valid (oldest=%d, next=%d)\n", point, timeline->point_oldest, timeline->point_next));
		}

		if (!mali_timeline_is_point_released(timeline, point)) {
			ret = MALI_FALSE;
			goto exit;
		}
	}

#if defined(CONFIG_SYNC)
	if (-1 != fence->sync_fd) {
		sync_fence = sync_fence_fdget(fence->sync_fd);
		if (likely(NULL != sync_fence)) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
			if (0 == sync_fence->status) {
#else
			if (0 == atomic_read(&sync_fence->status)) {
#endif
				ret = MALI_FALSE;
			}
		} else {
			MALI_PRINT_ERROR(("Mali Timeline: failed to get sync fence from fd %d\n", fence->sync_fd));
		}
	}
#endif /* defined(CONFIG_SYNC) */

exit:
	mali_spinlock_reentrant_signal(system->spinlock, tid);

#if defined(CONFIG_SYNC)
	if (NULL != sync_fence) {
		sync_fence_put(sync_fence);
	}
#endif /* defined(CONFIG_SYNC) */

	return ret;
}

mali_bool mali_timeline_fence_wait(struct mali_timeline_system *system, struct mali_timeline_fence *fence, u32 timeout)
{
	struct mali_timeline_fence_wait_tracker *wait;
	mali_timeline_point point;
	mali_bool ret;

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(fence);

	MALI_DEBUG_PRINT(4, ("Mali Timeline: wait on fence\n"));

	if (MALI_TIMELINE_FENCE_WAIT_TIMEOUT_IMMEDIATELY == timeout) {
		return mali_timeline_fence_wait_check_status(system, fence);
	}

	wait = mali_timeline_fence_wait_tracker_alloc();
	if (unlikely(NULL == wait)) {
		MALI_PRINT_ERROR(("Mali Timeline: failed to allocate data for fence wait\n"));
		return MALI_FALSE;
	}

	wait->activated = MALI_FALSE;
	wait->system = system;

	/* Initialize refcount to two references.  The reference first will be released by this
	 * function after the wait is over.  The second reference will be released when the tracker
	 * is activated. */
	_mali_osk_atomic_init(&wait->refcount, 2);

	/* Add tracker to timeline system, but not to a timeline. */
	mali_timeline_tracker_init(&wait->tracker, MALI_TIMELINE_TRACKER_WAIT, fence, wait);
	point = mali_timeline_system_add_tracker(system, &wait->tracker, MALI_TIMELINE_NONE);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_NO_POINT == point);
	MALI_IGNORE(point);

	/* Wait for the tracker to be activated or time out. */
	if (MALI_TIMELINE_FENCE_WAIT_TIMEOUT_NEVER == timeout) {
		_mali_osk_wait_queue_wait_event(system->wait_queue, mali_timeline_fence_wait_tracker_is_activated, (void *) wait);
	} else {
		_mali_osk_wait_queue_wait_event_timeout(system->wait_queue, mali_timeline_fence_wait_tracker_is_activated, (void *) wait, timeout);
	}

	ret = wait->activated;

	if (0 == _mali_osk_atomic_dec_return(&wait->refcount)) {
		mali_timeline_fence_wait_tracker_free(wait);
	}

	return ret;
}

void mali_timeline_fence_wait_activate(struct mali_timeline_fence_wait_tracker *wait)
{
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;

	MALI_DEBUG_ASSERT_POINTER(wait);
	MALI_DEBUG_ASSERT_POINTER(wait->system);

	MALI_DEBUG_PRINT(4, ("Mali Timeline: activation for fence wait tracker\n"));

	MALI_DEBUG_ASSERT(MALI_FALSE == wait->activated);
	wait->activated = MALI_TRUE;

	_mali_osk_wait_queue_wake_up(wait->system->wait_queue);

	/* Nothing can wait on this tracker, so nothing to schedule after release. */
	schedule_mask = mali_timeline_tracker_release(&wait->tracker);
	MALI_DEBUG_ASSERT(MALI_SCHEDULER_MASK_EMPTY == schedule_mask);
	MALI_IGNORE(schedule_mask);

	if (0 == _mali_osk_atomic_dec_return(&wait->refcount)) {
		mali_timeline_fence_wait_tracker_free(wait);
	}
}
