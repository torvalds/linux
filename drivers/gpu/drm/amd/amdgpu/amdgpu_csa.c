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

 * * Author: Monk.liu@amd.com
 */

#include <drm/drm_exec.h>

#include "amdgpu.h"

uint64_t amdgpu_csa_vaddr(struct amdgpu_device *adev)
{
	uint64_t addr = AMDGPU_VA_RESERVED_CSA_START(adev);

	addr = amdgpu_gmc_sign_extend(addr);

	return addr;
}

int amdgpu_allocate_static_csa(struct amdgpu_device *adev, struct amdgpu_bo **bo,
				u32 domain, uint32_t size)
{
	void *ptr;

	amdgpu_bo_create_kernel(adev, size, PAGE_SIZE,
				domain, bo,
				NULL, &ptr);
	if (!*bo)
		return -ENOMEM;

	memset(ptr, 0, size);
	adev->virt.csa_cpu_addr = ptr;
	return 0;
}

void amdgpu_free_static_csa(struct amdgpu_bo **bo)
{
	amdgpu_bo_free_kernel(bo, NULL, NULL);
}

/*
 * amdgpu_map_static_csa should be called during amdgpu_vm_init
 * it maps virtual address amdgpu_csa_vaddr() to this VM, and each command
 * submission of GFX should use this virtual address within META_DATA init
 * package to support SRIOV gfx preemption.
 */
int amdgpu_map_static_csa(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			  struct amdgpu_bo *bo, struct amdgpu_bo_va **bo_va,
			  uint64_t csa_addr, uint32_t size)
{
	struct drm_exec exec;
	int r;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 0);
	drm_exec_until_all_locked(&exec) {
		r = amdgpu_vm_lock_pd(vm, &exec, 0);
		if (likely(!r))
			r = drm_exec_lock_obj(&exec, &bo->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r)) {
			DRM_ERROR("failed to reserve CSA,PD BOs: err=%d\n", r);
			goto error;
		}
	}

	*bo_va = amdgpu_vm_bo_add(adev, vm, bo);
	if (!*bo_va) {
		r = -ENOMEM;
		goto error;
	}

	r = amdgpu_vm_bo_map(adev, *bo_va, csa_addr, 0, size,
			     AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE |
			     AMDGPU_PTE_EXECUTABLE);

	if (r) {
		DRM_ERROR("failed to do bo_map on static CSA, err=%d\n", r);
		amdgpu_vm_bo_del(adev, *bo_va);
		goto error;
	}

error:
	drm_exec_fini(&exec);
	return r;
}

int amdgpu_unmap_static_csa(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			    struct amdgpu_bo *bo, struct amdgpu_bo_va *bo_va,
			    uint64_t csa_addr)
{
	struct drm_exec exec;
	int r;

	drm_exec_init(&exec, 0, 0);
	drm_exec_until_all_locked(&exec) {
		r = amdgpu_vm_lock_pd(vm, &exec, 0);
		if (likely(!r))
			r = drm_exec_lock_obj(&exec, &bo->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r)) {
			DRM_ERROR("failed to reserve CSA,PD BOs: err=%d\n", r);
			goto error;
		}
	}

	r = amdgpu_vm_bo_unmap(adev, bo_va, csa_addr);
	if (r) {
		DRM_ERROR("failed to do bo_unmap on static CSA, err=%d\n", r);
		goto error;
	}

	amdgpu_vm_bo_del(adev, bo_va);

error:
	drm_exec_fini(&exec);
	return r;
}
