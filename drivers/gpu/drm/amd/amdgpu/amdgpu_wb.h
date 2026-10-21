/* SPDX-License-Identifier: GPL-2.0 OR MIT
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __AMDGPU_WB_H__
#define __AMDGPU_WB_H__

#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <linux/math.h>

/*
 * Writeback
 */
#define AMDGPU_MAX_WB 1024	/* Reserve at most 1024 WB slots for amdgpu-owned rings. */

/**
 * struct amdgpu_wb - This struct is used for small GPU memory allocation.
 *
 * This struct is used to allocate a small amount of GPU memory that can be
 * used to shadow certain states into the memory. This is especially useful for
 * providing easy CPU access to some states without requiring register access
 * (e.g., if some block is power gated, reading register may be problematic).
 *
 * Note: the term writeback was initially used because many of the amdgpu
 * components had some level of writeback memory, and this struct initially
 * described those components.
 */

struct amdgpu_bo;
struct amdgpu_device;

struct amdgpu_wb {

	/**
	 * @wb_obj:
	 *
	 * Buffer Object used for the writeback memory.
	 */
	struct amdgpu_bo	*wb_obj;

	/**
	 * @wb:
	 *
	 * Pointer to the first writeback slot. In terms of CPU address
	 * this value can be accessed directly by using the offset as an index.
	 * For the GPU address, it is necessary to use gpu_addr and the offset.
	 */
	uint32_t		*wb;

	/**
	 * @gpu_addr:
	 *
	 * Writeback base address in the GPU.
	 */
	uint64_t		gpu_addr;

	/**
	 * @num_wb:
	 *
	 * Number of writeback slots reserved for amdgpu.
	 */
	u32			num_wb;

	/**
	 * @used:
	 *
	 * Track the writeback slot already used.
	 */
	unsigned long		used[DIV_ROUND_UP(AMDGPU_MAX_WB, BITS_PER_LONG)];

	/**
	 * @lock:
	 *
	 * Protects read and write of the used field array.
	 */
	spinlock_t		lock;
};

void amdgpu_wb_fini(struct amdgpu_device *adev);
int amdgpu_wb_init(struct amdgpu_device *adev);
int amdgpu_wb_get(struct amdgpu_device *adev, u32 *wb);
void amdgpu_wb_free(struct amdgpu_device *adev, u32 wb);
#endif
