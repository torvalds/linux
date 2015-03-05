/*
 * Copyright (C) 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_timeline_sync_fence.h
 *
 * This file contains code related to creating sync fences from timeline fences.
 */

#ifndef __MALI_TIMELINE_SYNC_FENCE_H__
#define __MALI_TIMELINE_SYNC_FENCE_H__

#include "mali_timeline.h"

#if defined(CONFIG_SYNC)

/**
 * Sync fence tracker.
 */
struct mali_timeline_sync_fence_tracker {
	struct mali_sync_flag        *flag;    /**< Sync flag used to connect tracker and sync fence. */
	struct mali_timeline_tracker  tracker; /**< Timeline tracker. */
};

/**
 * Create a sync fence that will be signaled when @ref fence is signaled.
 *
 * @param system Timeline system.
 * @param fence Fence to create sync fence from.
 * @return File descriptor for new sync fence, or -1 on error.
 */
s32 mali_timeline_sync_fence_create(struct mali_timeline_system *system, struct mali_timeline_fence *fence);

/**
 * Used by the Timeline system to activate a sync fence tracker.
 *
 * @param sync_fence_tracker Sync fence tracker.
 *
 */
void mali_timeline_sync_fence_activate(struct mali_timeline_sync_fence_tracker *sync_fence_tracker);

#endif /* defined(CONFIG_SYNC) */

#endif /* __MALI_TIMELINE_SYNC_FENCE_H__ */
