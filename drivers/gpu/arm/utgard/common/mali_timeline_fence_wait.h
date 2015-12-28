/*
 * Copyright (C) 2013, 2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_timeline_fence_wait.h
 *
 * This file contains functions used to wait until a Timeline fence is signaled.
 */

#ifndef __MALI_TIMELINE_FENCE_WAIT_H__
#define __MALI_TIMELINE_FENCE_WAIT_H__

#include "mali_osk.h"
#include "mali_timeline.h"

/**
 * If used as the timeout argument in @ref mali_timeline_fence_wait, a timer is not used and the
 * function only returns when the fence is signaled.
 */
#define MALI_TIMELINE_FENCE_WAIT_TIMEOUT_NEVER ((u32) -1)

/**
 * If used as the timeout argument in @ref mali_timeline_fence_wait, the function will return
 * immediately with the current state of the fence.
 */
#define MALI_TIMELINE_FENCE_WAIT_TIMEOUT_IMMEDIATELY 0

/**
 * Fence wait tracker.
 *
 * The fence wait tracker is added to the Timeline system with the fence we are waiting on as a
 * dependency.  We will then perform a blocking wait, possibly with a timeout, until the tracker is
 * activated, which happens when the fence is signaled.
 */
struct mali_timeline_fence_wait_tracker {
	mali_bool activated;                  /**< MALI_TRUE if the tracker has been activated, MALI_FALSE if not. */
	_mali_osk_atomic_t refcount;          /**< Reference count. */
	struct mali_timeline_system *system;  /**< Timeline system. */
	struct mali_timeline_tracker tracker; /**< Timeline tracker. */
};

/**
 * Wait for a fence to be signaled, or timeout is reached.
 *
 * @param system Timeline system.
 * @param fence Fence to wait on.
 * @param timeout Timeout in ms, or MALI_TIMELINE_FENCE_WAIT_TIMEOUT_NEVER or
 * MALI_TIMELINE_FENCE_WAIT_TIMEOUT_IMMEDIATELY.
 * @return MALI_TRUE if signaled, MALI_FALSE if timed out.
 */
mali_bool mali_timeline_fence_wait(struct mali_timeline_system *system, struct mali_timeline_fence *fence, u32 timeout);

/**
 * Used by the Timeline system to activate a fence wait tracker.
 *
 * @param fence_wait_tracker Fence waiter tracker.
 */
void mali_timeline_fence_wait_activate(struct mali_timeline_fence_wait_tracker *fence_wait_tracker);

#endif /* __MALI_TIMELINE_FENCE_WAIT_H__ */
