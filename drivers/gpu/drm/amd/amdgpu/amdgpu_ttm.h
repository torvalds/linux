/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 *
 */

#ifndef __AMDGPU_TTM_H__
#define __AMDGPU_TTM_H__

#include "gpu_scheduler.h"

#define AMDGPU_PL_GDS		TTM_PL_PRIV0
#define AMDGPU_PL_GWS		TTM_PL_PRIV1
#define AMDGPU_PL_OA		TTM_PL_PRIV2

#define AMDGPU_PL_FLAG_GDS		TTM_PL_FLAG_PRIV0
#define AMDGPU_PL_FLAG_GWS		TTM_PL_FLAG_PRIV1
#define AMDGPU_PL_FLAG_OA		TTM_PL_FLAG_PRIV2

#define AMDGPU_TTM_LRU_SIZE	20

struct amdgpu_mman_lru {
	struct list_head		*lru[TTM_NUM_MEM_TYPES];
	struct list_head		*swap_lru;
};

struct amdgpu_mman {
	struct ttm_bo_global_ref        bo_global_ref;
	struct drm_global_reference	mem_global_ref;
	struct ttm_bo_device		bdev;
	bool				mem_global_referenced;
	bool				initialized;

#if defined(CONFIG_DEBUG_FS)
	struct dentry			*vram;
	struct dentry			*gtt;
#endif

	/* buffer handling */
	const struct amdgpu_buffer_funcs	*buffer_funcs;
	struct amdgpu_ring			*buffer_funcs_ring;
	/* Scheduler entity for buffer moves */
	struct amd_sched_entity			entity;

	/* custom LRU management */
	struct amdgpu_mman_lru			log2_size[AMDGPU_TTM_LRU_SIZE];
};

int amdgpu_copy_buffer(struct amdgpu_ring *ring,
		       uint64_t src_offset,
		       uint64_t dst_offset,
		       uint32_t byte_count,
		       struct reservation_object *resv,
		       struct fence **fence, bool direct_submit);
int amdgpu_fill_buffer(struct amdgpu_bo *bo,
			uint32_t src_data,
			struct reservation_object *resv,
			struct fence **fence);

int amdgpu_mmap(struct file *filp, struct vm_area_struct *vma);
#endif
