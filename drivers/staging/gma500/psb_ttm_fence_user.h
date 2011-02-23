/**************************************************************************
 *
 * Copyright 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/
/*
 * Authors
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#ifndef TTM_FENCE_USER_H
#define TTM_FENCE_USER_H

#if !defined(__KERNEL__) && !defined(_KERNEL)
#include <stdint.h>
#endif

#define TTM_FENCE_MAJOR 0
#define TTM_FENCE_MINOR 1
#define TTM_FENCE_PL    0
#define TTM_FENCE_DATE  "080819"

/**
 * struct ttm_fence_signaled_req
 *
 * @handle: Handle to the fence object. Input.
 *
 * @fence_type: Fence types we want to flush. Input.
 *
 * @flush: Boolean. Flush the indicated fence_types. Input.
 *
 * Argument to the TTM_FENCE_SIGNALED ioctl.
 */

struct ttm_fence_signaled_req {
	uint32_t handle;
	uint32_t fence_type;
	int32_t flush;
	uint32_t pad64;
};

/**
 * struct ttm_fence_rep
 *
 * @signaled_types: Fence type that has signaled.
 *
 * @fence_error: Command execution error.
 * Hardware errors that are consequences of the execution
 * of the command stream preceding the fence are reported
 * here.
 *
 * Output argument to the TTM_FENCE_SIGNALED and
 * TTM_FENCE_FINISH ioctls.
 */

struct ttm_fence_rep {
	uint32_t signaled_types;
	uint32_t fence_error;
};

union ttm_fence_signaled_arg {
	struct ttm_fence_signaled_req req;
	struct ttm_fence_rep rep;
};

/*
 * Waiting mode flags for the TTM_FENCE_FINISH ioctl.
 *
 * TTM_FENCE_FINISH_MODE_LAZY: Allow for sleeps during polling
 * wait.
 *
 * TTM_FENCE_FINISH_MODE_NO_BLOCK: Don't block waiting for GPU,
 * but return -EBUSY if the buffer is busy.
 */

#define TTM_FENCE_FINISH_MODE_LAZY     (1 << 0)
#define TTM_FENCE_FINISH_MODE_NO_BLOCK (1 << 1)

/**
 * struct ttm_fence_finish_req
 *
 * @handle: Handle to the fence object. Input.
 *
 * @fence_type: Fence types we want to finish.
 *
 * @mode: Wait mode.
 *
 * Input to the TTM_FENCE_FINISH ioctl.
 */

struct ttm_fence_finish_req {
	uint32_t handle;
	uint32_t fence_type;
	uint32_t mode;
	uint32_t pad64;
};

union ttm_fence_finish_arg {
	struct ttm_fence_finish_req req;
	struct ttm_fence_rep rep;
};

/**
 * struct ttm_fence_unref_arg
 *
 * @handle: Handle to the fence object.
 *
 * Argument to the TTM_FENCE_UNREF ioctl.
 */

struct ttm_fence_unref_arg {
	uint32_t handle;
	uint32_t pad64;
};

/*
 * Ioctl offsets frome extenstion start.
 */

#define TTM_FENCE_SIGNALED 0x01
#define TTM_FENCE_FINISH   0x02
#define TTM_FENCE_UNREF    0x03

#endif
