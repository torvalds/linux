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

#ifndef _TTM_PLACEMENT_USER_H_
#define _TTM_PLACEMENT_USER_H_

#if !defined(__KERNEL__) && !defined(_KERNEL)
#include <stdint.h>
#else
#include <linux/kernel.h>
#endif

#include "ttm/ttm_placement.h"

#define TTM_PLACEMENT_MAJOR 0
#define TTM_PLACEMENT_MINOR 1
#define TTM_PLACEMENT_PL    0
#define TTM_PLACEMENT_DATE  "080819"

/**
 * struct ttm_pl_create_req
 *
 * @size: The buffer object size.
 * @placement: Flags that indicate initial acceptable
 *  placement.
 * @page_alignment: Required alignment in pages.
 *
 * Input to the TTM_BO_CREATE ioctl.
 */

struct ttm_pl_create_req {
	uint64_t size;
	uint32_t placement;
	uint32_t page_alignment;
};

/**
 * struct ttm_pl_create_ub_req
 *
 * @size: The buffer object size.
 * @user_address: User-space address of the memory area that
 * should be used to back the buffer object cast to 64-bit.
 * @placement: Flags that indicate initial acceptable
 *  placement.
 * @page_alignment: Required alignment in pages.
 *
 * Input to the TTM_BO_CREATE_UB ioctl.
 */

struct ttm_pl_create_ub_req {
	uint64_t size;
	uint64_t user_address;
	uint32_t placement;
	uint32_t page_alignment;
};

/**
 * struct ttm_pl_rep
 *
 * @gpu_offset: The current offset into the memory region used.
 * This can be used directly by the GPU if there are no
 * additional GPU mapping procedures used by the driver.
 *
 * @bo_size: Actual buffer object size.
 *
 * @map_handle: Offset into the device address space.
 * Used for map, seek, read, write. This will never change
 * during the lifetime of an object.
 *
 * @placement: Flag indicating the placement status of
 * the buffer object using the TTM_PL flags above.
 *
 * @sync_object_arg: Used for user-space synchronization and
 * depends on the synchronization model used. If fences are
 * used, this is the buffer_object::fence_type_mask
 *
 * Output from the TTM_PL_CREATE and TTM_PL_REFERENCE, and
 * TTM_PL_SETSTATUS ioctls.
 */

struct ttm_pl_rep {
	uint64_t gpu_offset;
	uint64_t bo_size;
	uint64_t map_handle;
	uint32_t placement;
	uint32_t handle;
	uint32_t sync_object_arg;
	uint32_t pad64;
};

/**
 * struct ttm_pl_setstatus_req
 *
 * @set_placement: Placement flags to set.
 *
 * @clr_placement: Placement flags to clear.
 *
 * @handle: The object handle
 *
 * Input to the TTM_PL_SETSTATUS ioctl.
 */

struct ttm_pl_setstatus_req {
	uint32_t set_placement;
	uint32_t clr_placement;
	uint32_t handle;
	uint32_t pad64;
};

/**
 * struct ttm_pl_reference_req
 *
 * @handle: The object to put a reference on.
 *
 * Input to the TTM_PL_REFERENCE and the TTM_PL_UNREFERENCE ioctls.
 */

struct ttm_pl_reference_req {
	uint32_t handle;
	uint32_t pad64;
};

/*
 * ACCESS mode flags for SYNCCPU.
 *
 * TTM_SYNCCPU_MODE_READ will guarantee that the GPU is not
 * writing to the buffer.
 *
 * TTM_SYNCCPU_MODE_WRITE will guarantee that the GPU is not
 * accessing the buffer.
 *
 * TTM_SYNCCPU_MODE_NO_BLOCK makes sure the call does not wait
 * for GPU accesses to finish but return -EBUSY.
 *
 * TTM_SYNCCPU_MODE_TRYCACHED Try to place the buffer in cacheable
 * memory while synchronized for CPU.
 */

#define TTM_PL_SYNCCPU_MODE_READ      TTM_ACCESS_READ
#define TTM_PL_SYNCCPU_MODE_WRITE     TTM_ACCESS_WRITE
#define TTM_PL_SYNCCPU_MODE_NO_BLOCK  (1 << 2)
#define TTM_PL_SYNCCPU_MODE_TRYCACHED (1 << 3)

/**
 * struct ttm_pl_synccpu_arg
 *
 * @handle: The object to synchronize.
 *
 * @access_mode: access mode indicated by the
 * TTM_SYNCCPU_MODE flags.
 *
 * @op: indicates whether to grab or release the
 * buffer for cpu usage.
 *
 * Input to the TTM_PL_SYNCCPU ioctl.
 */

struct ttm_pl_synccpu_arg {
	uint32_t handle;
	uint32_t access_mode;
	enum {
		TTM_PL_SYNCCPU_OP_GRAB,
		TTM_PL_SYNCCPU_OP_RELEASE
	} op;
	uint32_t pad64;
};

/*
 * Waiting mode flags for the TTM_BO_WAITIDLE ioctl.
 *
 * TTM_WAITIDLE_MODE_LAZY: Allow for sleeps during polling
 * wait.
 *
 * TTM_WAITIDLE_MODE_NO_BLOCK: Don't block waiting for GPU,
 * but return -EBUSY if the buffer is busy.
 */

#define TTM_PL_WAITIDLE_MODE_LAZY     (1 << 0)
#define TTM_PL_WAITIDLE_MODE_NO_BLOCK (1 << 1)

/**
 * struct ttm_waitidle_arg
 *
 * @handle: The object to synchronize.
 *
 * @mode: wait mode indicated by the
 * TTM_SYNCCPU_MODE flags.
 *
 * Argument to the TTM_BO_WAITIDLE ioctl.
 */

struct ttm_pl_waitidle_arg {
	uint32_t handle;
	uint32_t mode;
};

union ttm_pl_create_arg {
	struct ttm_pl_create_req req;
	struct ttm_pl_rep rep;
};

union ttm_pl_reference_arg {
	struct ttm_pl_reference_req req;
	struct ttm_pl_rep rep;
};

union ttm_pl_setstatus_arg {
	struct ttm_pl_setstatus_req req;
	struct ttm_pl_rep rep;
};

union ttm_pl_create_ub_arg {
	struct ttm_pl_create_ub_req req;
	struct ttm_pl_rep rep;
};

/*
 * Ioctl offsets.
 */

#define TTM_PL_CREATE      0x00
#define TTM_PL_REFERENCE   0x01
#define TTM_PL_UNREF       0x02
#define TTM_PL_SYNCCPU     0x03
#define TTM_PL_WAITIDLE    0x04
#define TTM_PL_SETSTATUS   0x05
#define TTM_PL_CREATE_UB   0x06

#endif
