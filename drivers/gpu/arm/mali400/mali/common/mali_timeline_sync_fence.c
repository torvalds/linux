/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_timeline_sync_fence.h"

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_sync.h"

#if defined(CONFIG_SYNC)

/**
 * Creates a sync fence tracker and a sync fence.  Adds sync fence tracker to Timeline system and
 * returns sync fence.  The sync fence will be signaled when the sync fence tracker is activated.
 *
 * @param timeline Timeline.
 * @param point Point on timeline.
 * @return Sync fence that will be signaled when tracker is activated.
 */
static struct sync_fence *mali_timeline_sync_fence_create_and_add_tracker(struct mali_timeline *timeline, mali_timeline_point point)
{
	struct mali_timeline_sync_fence_tracker *sync_fence_tracker;
	struct sync_fence                       *sync_fence;
	struct mali_timeline_fence               fence;

	MALI_DEBUG_ASSERT_POINTER(timeline);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_NO_POINT != point);

	/* Allocate sync fence tracker. */
	sync_fence_tracker = _mali_osk_calloc(1, sizeof(struct mali_timeline_sync_fence_tracker));
	if (NULL == sync_fence_tracker) {
		MALI_PRINT_ERROR(("Mali Timeline: sync_fence_tracker allocation failed\n"));
		return NULL;
	}

	/* Create sync flag. */
	MALI_DEBUG_ASSERT_POINTER(timeline->sync_tl);
	sync_fence_tracker->flag = mali_sync_flag_create(timeline->sync_tl, point);
	if (NULL == sync_fence_tracker->flag) {
		MALI_PRINT_ERROR(("Mali Timeline: sync_flag creation failed\n"));
		_mali_osk_free(sync_fence_tracker);
		return NULL;
	}

	/* Create sync fence from sync flag. */
	sync_fence = mali_sync_flag_create_fence(sync_fence_tracker->flag);
	if (NULL == sync_fence) {
		MALI_PRINT_ERROR(("Mali Timeline: sync_fence creation failed\n"));
		mali_sync_flag_put(sync_fence_tracker->flag);
		_mali_osk_free(sync_fence_tracker);
		return NULL;
	}

	/* Setup fence for tracker. */
	_mali_osk_memset(&fence, 0, sizeof(struct mali_timeline_fence));
	fence.sync_fd = -1;
	fence.points[timeline->id] = point;

	/* Finally, add the tracker to Timeline system. */
	mali_timeline_tracker_init(&sync_fence_tracker->tracker, MALI_TIMELINE_TRACKER_SYNC, &fence, sync_fence_tracker);
	point = mali_timeline_system_add_tracker(timeline->system, &sync_fence_tracker->tracker, MALI_TIMELINE_NONE);
	MALI_DEBUG_ASSERT(MALI_TIMELINE_NO_POINT == point);

	return sync_fence;
}

s32 mali_timeline_sync_fence_create(struct mali_timeline_system *system, struct mali_timeline_fence *fence)
{
	u32 i;
	struct sync_fence *sync_fence_acc = NULL;

	MALI_DEBUG_ASSERT_POINTER(system);
	MALI_DEBUG_ASSERT_POINTER(fence);

	for (i = 0; i < MALI_TIMELINE_MAX; ++i) {
		struct mali_timeline *timeline;
		struct sync_fence *sync_fence;

		if (MALI_TIMELINE_NO_POINT == fence->points[i]) continue;

		timeline = system->timelines[i];
		MALI_DEBUG_ASSERT_POINTER(timeline);

		sync_fence = mali_timeline_sync_fence_create_and_add_tracker(timeline, fence->points[i]);
		if (NULL == sync_fence) goto error;

		if (NULL != sync_fence_acc) {
			/* Merge sync fences. */
			sync_fence_acc = mali_sync_fence_merge(sync_fence_acc, sync_fence);
			if (NULL == sync_fence_acc) goto error;
		} else {
			/* This was the first sync fence created. */
			sync_fence_acc = sync_fence;
		}
	}

	if (-1 != fence->sync_fd) {
		struct sync_fence *sync_fence;

		sync_fence = sync_fence_fdget(fence->sync_fd);
		if (NULL == sync_fence) goto error;

		if (NULL != sync_fence_acc) {
			sync_fence_acc = mali_sync_fence_merge(sync_fence_acc, sync_fence);
			if (NULL == sync_fence_acc) goto error;
		} else {
			sync_fence_acc = sync_fence;
		}
	}

	if (NULL == sync_fence_acc) {
		MALI_DEBUG_ASSERT_POINTER(system->signaled_sync_tl);

		/* There was nothing to wait on, so return an already signaled fence. */

		sync_fence_acc = mali_sync_timeline_create_signaled_fence(system->signaled_sync_tl);
		if (NULL == sync_fence_acc) goto error;
	}

	/* Return file descriptor for the accumulated sync fence. */
	return mali_sync_fence_fd_alloc(sync_fence_acc);

error:
	if (NULL != sync_fence_acc) {
		sync_fence_put(sync_fence_acc);
	}

	return -1;
}

void mali_timeline_sync_fence_activate(struct mali_timeline_sync_fence_tracker *sync_fence_tracker)
{
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;

	MALI_DEBUG_ASSERT_POINTER(sync_fence_tracker);
	MALI_DEBUG_ASSERT_POINTER(sync_fence_tracker->flag);

	MALI_DEBUG_PRINT(4, ("Mali Timeline: activation for sync fence tracker\n"));

	/* Signal flag and release reference. */
	mali_sync_flag_signal(sync_fence_tracker->flag, 0);
	mali_sync_flag_put(sync_fence_tracker->flag);

	/* Nothing can wait on this tracker, so nothing to schedule after release. */
	schedule_mask = mali_timeline_tracker_release(&sync_fence_tracker->tracker);
	MALI_DEBUG_ASSERT(MALI_SCHEDULER_MASK_EMPTY == schedule_mask);

	_mali_osk_free(sync_fence_tracker);
}

#endif /* defined(CONFIG_SYNC) */
