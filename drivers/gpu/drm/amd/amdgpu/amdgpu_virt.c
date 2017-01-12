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

#include "amdgpu.h"

int amdgpu_allocate_static_csa(struct amdgpu_device *adev)
{
	int r;
	void *ptr;

	r = amdgpu_bo_create_kernel(adev, AMDGPU_CSA_SIZE, PAGE_SIZE,
				AMDGPU_GEM_DOMAIN_VRAM, &adev->virt.csa_obj,
				&adev->virt.csa_vmid0_addr, &ptr);
	if (r)
		return r;

	memset(ptr, 0, AMDGPU_CSA_SIZE);
	return 0;
}

/*
 * amdgpu_map_static_csa should be called during amdgpu_vm_init
 * it maps virtual address "AMDGPU_VA_RESERVED_SIZE - AMDGPU_CSA_SIZE"
 * to this VM, and each command submission of GFX should use this virtual
 * address within META_DATA init package to support SRIOV gfx preemption.
 */

int amdgpu_map_static_csa(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	int r;
	struct amdgpu_bo_va *bo_va;
	struct ww_acquire_ctx ticket;
	struct list_head list;
	struct amdgpu_bo_list_entry pd;
	struct ttm_validate_buffer csa_tv;

	INIT_LIST_HEAD(&list);
	INIT_LIST_HEAD(&csa_tv.head);
	csa_tv.bo = &adev->virt.csa_obj->tbo;
	csa_tv.shared = true;

	list_add(&csa_tv.head, &list);
	amdgpu_vm_get_pd_bo(vm, &list, &pd);

	r = ttm_eu_reserve_buffers(&ticket, &list, true, NULL);
	if (r) {
		DRM_ERROR("failed to reserve CSA,PD BOs: err=%d\n", r);
		return r;
	}

	bo_va = amdgpu_vm_bo_add(adev, vm, adev->virt.csa_obj);
	if (!bo_va) {
		ttm_eu_backoff_reservation(&ticket, &list);
		DRM_ERROR("failed to create bo_va for static CSA\n");
		return -ENOMEM;
	}

	r = amdgpu_vm_bo_map(adev, bo_va, AMDGPU_CSA_VADDR, 0,AMDGPU_CSA_SIZE,
						AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE |
						AMDGPU_PTE_EXECUTABLE);

	if (r) {
		DRM_ERROR("failed to do bo_map on static CSA, err=%d\n", r);
		amdgpu_vm_bo_rmv(adev, bo_va);
		ttm_eu_backoff_reservation(&ticket, &list);
		kfree(bo_va);
		return r;
	}

	vm->csa_bo_va = bo_va;
	ttm_eu_backoff_reservation(&ticket, &list);
	return 0;
}

void amdgpu_virt_init_setting(struct amdgpu_device *adev)
{
	mutex_init(&adev->virt.lock);
}

uint32_t amdgpu_virt_kiq_rreg(struct amdgpu_device *adev, uint32_t reg)
{
	signed long r;
	uint32_t val;
	struct dma_fence *f;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *ring = &kiq->ring;

	BUG_ON(!ring->funcs->emit_rreg);

	mutex_lock(&adev->virt.lock);
	amdgpu_ring_alloc(ring, 32);
	amdgpu_ring_emit_hdp_flush(ring);
	amdgpu_ring_emit_rreg(ring, reg);
	amdgpu_ring_emit_hdp_invalidate(ring);
	amdgpu_fence_emit(ring, &f);
	amdgpu_ring_commit(ring);
	mutex_unlock(&adev->virt.lock);

	r = dma_fence_wait(f, false);
	if (r)
		DRM_ERROR("wait for kiq fence error: %ld.\n", r);
	dma_fence_put(f);

	val = adev->wb.wb[adev->virt.reg_val_offs];

	return val;
}

void amdgpu_virt_kiq_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v)
{
	signed long r;
	struct dma_fence *f;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *ring = &kiq->ring;

	BUG_ON(!ring->funcs->emit_wreg);

	mutex_lock(&adev->virt.lock);
	amdgpu_ring_alloc(ring, 32);
	amdgpu_ring_emit_hdp_flush(ring);
	amdgpu_ring_emit_wreg(ring, reg, v);
	amdgpu_ring_emit_hdp_invalidate(ring);
	amdgpu_fence_emit(ring, &f);
	amdgpu_ring_commit(ring);
	mutex_unlock(&adev->virt.lock);

	r = dma_fence_wait(f, false);
	if (r)
		DRM_ERROR("wait for kiq fence error: %ld.\n", r);
	dma_fence_put(f);
}

/**
 * amdgpu_virt_request_full_gpu() - request full gpu access
 * @amdgpu:	amdgpu device.
 * @init:	is driver init time.
 * When start to init/fini driver, first need to request full gpu access.
 * Return: Zero if request success, otherwise will return error.
 */
int amdgpu_virt_request_full_gpu(struct amdgpu_device *adev, bool init)
{
	struct amdgpu_virt *virt = &adev->virt;
	int r;

	if (virt->ops && virt->ops->req_full_gpu) {
		r = virt->ops->req_full_gpu(adev, init);
		if (r)
			return r;

		adev->virt.caps &= ~AMDGPU_SRIOV_CAPS_RUNTIME;
	}

	return 0;
}

/**
 * amdgpu_virt_release_full_gpu() - release full gpu access
 * @amdgpu:	amdgpu device.
 * @init:	is driver init time.
 * When finishing driver init/fini, need to release full gpu access.
 * Return: Zero if release success, otherwise will returen error.
 */
int amdgpu_virt_release_full_gpu(struct amdgpu_device *adev, bool init)
{
	struct amdgpu_virt *virt = &adev->virt;
	int r;

	if (virt->ops && virt->ops->rel_full_gpu) {
		r = virt->ops->rel_full_gpu(adev, init);
		if (r)
			return r;

		adev->virt.caps |= AMDGPU_SRIOV_CAPS_RUNTIME;
	}
	return 0;
}

/**
 * amdgpu_virt_reset_gpu() - reset gpu
 * @amdgpu:	amdgpu device.
 * Send reset command to GPU hypervisor to reset GPU that VM is using
 * Return: Zero if reset success, otherwise will return error.
 */
int amdgpu_virt_reset_gpu(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;
	int r;

	if (virt->ops && virt->ops->reset_gpu) {
		r = virt->ops->reset_gpu(adev);
		if (r)
			return r;

		adev->virt.caps &= ~AMDGPU_SRIOV_CAPS_RUNTIME;
	}

	return 0;
}
