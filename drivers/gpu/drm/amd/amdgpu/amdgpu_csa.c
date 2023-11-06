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

#include "amdgpu.h"

uint64_t amdgpu_csa_vaddr(struct amdgpu_device *adev)
{
	uint64_t addr = adev->vm_manager.max_pfn << AMDGPU_GPU_PAGE_SHIFT;

	addr -= AMDGPU_VA_RESERVED_SIZE;
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
	struct ww_acquire_ctx ticket;
	struct list_head list;
	struct amdgpu_bo_list_entry pd;
	struct ttm_validate_buffer csa_tv;
	int r;

	INIT_LIST_HEAD(&list);
	INIT_LIST_HEAD(&csa_tv.head);
	csa_tv.bo = &bo->tbo;
	csa_tv.num_shared = 1;

	list_add(&csa_tv.head, &list);
	amdgpu_vm_get_pd_bo(vm, &list, &pd);

	r = ttm_eu_reserve_buffers(&ticket, &list, true, NULL);
	if (r) {
		DRM_ERROR("failed to reserve CSA,PD BOs: err=%d\n", r);
		return r;
	}

	*bo_va = amdgpu_vm_bo_add(adev, vm, bo);
	if (!*bo_va) {
		ttm_eu_backoff_reservation(&ticket, &list);
		DRM_ERROR("failed to create bo_va for static CSA\n");
		return -ENOMEM;
	}

	r = amdgpu_vm_bo_map(adev, *bo_va, csa_addr, 0, size,
			     AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE |
			     AMDGPU_PTE_EXECUTABLE);

	if (r) {
		DRM_ERROR("failed to do bo_map on static CSA, err=%d\n", r);
		amdgpu_vm_bo_del(adev, *bo_va);
		ttm_eu_backoff_reservation(&ticket, &list);
		return r;
	}

	ttm_eu_backoff_reservation(&ticket, &list);
	return 0;
}

int amdgpu_unmap_static_csa(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			    struct amdgpu_bo *bo, struct amdgpu_bo_va *bo_va,
			    uint64_t csa_addr)
{
	struct ww_acquire_ctx ticket;
	struct list_head list;
	struct amdgpu_bo_list_entry pd;
	struct ttm_validate_buffer csa_tv;
	int r;

	INIT_LIST_HEAD(&list);
	INIT_LIST_HEAD(&csa_tv.head);
	csa_tv.bo = &bo->tbo;
	csa_tv.num_shared = 1;

	list_add(&csa_tv.head, &list);
	amdgpu_vm_get_pd_bo(vm, &list, &pd);

	r = ttm_eu_reserve_buffers(&ticket, &list, true, NULL);
	if (r) {
		DRM_ERROR("failed to reserve CSA,PD BOs: err=%d\n", r);
		return r;
	}

	r = amdgpu_vm_bo_unmap(adev, bo_va, csa_addr);
	if (r) {
		DRM_ERROR("failed to do bo_unmap on static CSA, err=%d\n", r);
		ttm_eu_backoff_reservation(&ticket, &list);
		return r;
	}

	amdgpu_vm_bo_del(adev, bo_va);

	ttm_eu_backoff_reservation(&ticket, &list);

	return 0;
}
