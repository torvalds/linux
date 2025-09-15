/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_USERQ_FENCE_H__
#define __AMDGPU_USERQ_FENCE_H__

#include <linux/types.h>

#include "amdgpu_userq.h"

struct amdgpu_userq_fence {
	struct dma_fence base;
	/*
	 * This lock is necessary to synchronize the
	 * userqueue dma fence operations.
	 */
	spinlock_t lock;
	struct list_head link;
	unsigned long fence_drv_array_count;
	struct amdgpu_userq_fence_driver *fence_drv;
	struct amdgpu_userq_fence_driver **fence_drv_array;
};

struct amdgpu_userq_fence_driver {
	struct kref refcount;
	u64 va;
	u64 gpu_addr;
	u64 *cpu_addr;
	u64 context;
	/*
	 * This lock is necesaary to synchronize the access
	 * to the fences list by the fence driver.
	 */
	spinlock_t fence_list_lock;
	struct list_head fences;
	struct amdgpu_device *adev;
	char timeline_name[TASK_COMM_LEN];
};

int amdgpu_userq_fence_slab_init(void);
void amdgpu_userq_fence_slab_fini(void);

void amdgpu_userq_fence_driver_get(struct amdgpu_userq_fence_driver *fence_drv);
void amdgpu_userq_fence_driver_put(struct amdgpu_userq_fence_driver *fence_drv);
int amdgpu_userq_fence_driver_alloc(struct amdgpu_device *adev,
				    struct amdgpu_usermode_queue *userq);
void amdgpu_userq_fence_driver_free(struct amdgpu_usermode_queue *userq);
void amdgpu_userq_fence_driver_process(struct amdgpu_userq_fence_driver *fence_drv);
void amdgpu_userq_fence_driver_force_completion(struct amdgpu_usermode_queue *userq);
void amdgpu_userq_fence_driver_destroy(struct kref *ref);
int amdgpu_userq_signal_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *filp);
int amdgpu_userq_wait_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *filp);

#endif
