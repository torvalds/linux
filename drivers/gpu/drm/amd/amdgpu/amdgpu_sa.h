/* SPDX-License-Identifier: MIT */
/*
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
 *
 */

#ifndef AMDGPU_SA_H_
#define AMDGPU_SA_H_

#include <drm/drm_suballoc.h>

struct amdgpu_device;
struct amdgpu_bo;

struct amdgpu_sa_manager {
	struct drm_suballoc_manager	base;
	struct amdgpu_bo		*bo;
	uint64_t			gpu_addr;
	void				*cpu_ptr;
};

static inline struct amdgpu_sa_manager *
to_amdgpu_sa_manager(struct drm_suballoc_manager *manager)
{
	return container_of(manager, struct amdgpu_sa_manager, base);
}

static inline uint64_t amdgpu_sa_bo_gpu_addr(struct drm_suballoc *sa_bo)
{
	return to_amdgpu_sa_manager(sa_bo->manager)->gpu_addr +
		drm_suballoc_soffset(sa_bo);
}

static inline void *amdgpu_sa_bo_cpu_addr(struct drm_suballoc *sa_bo)
{
	return to_amdgpu_sa_manager(sa_bo->manager)->cpu_ptr +
		drm_suballoc_soffset(sa_bo);
}

int amdgpu_sa_bo_manager_init(struct amdgpu_device *adev,
			      struct amdgpu_sa_manager *sa_manager,
			      unsigned size, u32 align, u32 domain);
void amdgpu_sa_bo_manager_fini(struct amdgpu_device *adev,
			       struct amdgpu_sa_manager *sa_manager);
int amdgpu_sa_bo_manager_start(struct amdgpu_device *adev,
			       struct amdgpu_sa_manager *sa_manager);
int amdgpu_sa_bo_new(struct amdgpu_sa_manager *sa_manager,
		     struct drm_suballoc **sa_bo,
		     unsigned int size);
void amdgpu_sa_bo_free(struct drm_suballoc **sa_bo,
		       struct dma_fence *fence);
#if defined(CONFIG_DEBUG_FS)
void amdgpu_sa_bo_dump_debug_info(struct amdgpu_sa_manager *sa_manager,
				  struct seq_file *m);
u64 amdgpu_bo_print_info(int id, struct amdgpu_bo *bo, struct seq_file *m);
#endif
void amdgpu_debugfs_sa_init(struct amdgpu_device *adev);

#endif
