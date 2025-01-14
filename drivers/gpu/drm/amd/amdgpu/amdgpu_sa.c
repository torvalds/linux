/*
 * Copyright 2011 Red Hat Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Jerome Glisse <glisse@freedesktop.org>
 */
/* Algorithm:
 *
 * We store the last allocated bo in "hole", we always try to allocate
 * after the last allocated bo. Principle is that in a linear GPU ring
 * progression was is after last is the oldest bo we allocated and thus
 * the first one that should no longer be in use by the GPU.
 *
 * If it's not the case we skip over the bo after last to the closest
 * done bo if such one exist. If none exist and we are not asked to
 * block we report failure to allocate.
 *
 * If we are asked to block we wait on all the oldest fence of all
 * rings. We just wait for any of those fence to complete.
 */

#include "amdgpu.h"

int amdgpu_sa_bo_manager_init(struct amdgpu_device *adev,
			      struct amdgpu_sa_manager *sa_manager,
			      unsigned int size, u32 suballoc_align, u32 domain)
{
	int r;

	r = amdgpu_bo_create_kernel(adev, size, AMDGPU_GPU_PAGE_SIZE, domain,
				    &sa_manager->bo, &sa_manager->gpu_addr,
				    &sa_manager->cpu_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to allocate bo for manager\n", r);
		return r;
	}

	memset(sa_manager->cpu_ptr, 0, size);
	drm_suballoc_manager_init(&sa_manager->base, size, suballoc_align);
	return r;
}

void amdgpu_sa_bo_manager_fini(struct amdgpu_device *adev,
			       struct amdgpu_sa_manager *sa_manager)
{
	if (sa_manager->bo == NULL) {
		dev_err(adev->dev, "no bo for sa manager\n");
		return;
	}

	drm_suballoc_manager_fini(&sa_manager->base);

	amdgpu_bo_free_kernel(&sa_manager->bo, &sa_manager->gpu_addr, &sa_manager->cpu_ptr);
}

int amdgpu_sa_bo_new(struct amdgpu_sa_manager *sa_manager,
		     struct drm_suballoc **sa_bo,
		     unsigned int size)
{
	struct drm_suballoc *sa = drm_suballoc_new(&sa_manager->base, size,
						   GFP_KERNEL, false, 0);

	if (IS_ERR(sa)) {
		*sa_bo = NULL;

		return PTR_ERR(sa);
	}

	*sa_bo = sa;
	return 0;
}

void amdgpu_sa_bo_free(struct drm_suballoc **sa_bo, struct dma_fence *fence)
{
	if (sa_bo == NULL || *sa_bo == NULL) {
		return;
	}

	drm_suballoc_free(*sa_bo, fence);
	*sa_bo = NULL;
}

#if defined(CONFIG_DEBUG_FS)

void amdgpu_sa_bo_dump_debug_info(struct amdgpu_sa_manager *sa_manager,
				  struct seq_file *m)
{
	struct drm_printer p = drm_seq_file_printer(m);

	drm_suballoc_dump_debug_info(&sa_manager->base, &p, sa_manager->gpu_addr);
}
#endif
