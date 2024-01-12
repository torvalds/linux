// SPDX-License-Identifier: MIT
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

#include "amdgpu.h"
#include "amdgpu_seq64.h"

#include <drm/drm_exec.h>

/**
 * DOC: amdgpu_seq64
 *
 * amdgpu_seq64 allocates a 64bit memory on each request in sequence order.
 * seq64 driver is required for user queue fence memory allocation, TLB
 * counters and VM updates. It has maximum count of 32768 64 bit slots.
 */

/**
 * amdgpu_seq64_map - Map the seq64 memory to VM
 *
 * @adev: amdgpu_device pointer
 * @vm: vm pointer
 * @bo_va: bo_va pointer
 * @seq64_addr: seq64 vaddr start address
 * @size: seq64 pool size
 *
 * Map the seq64 memory to the given VM.
 *
 * Returns:
 * 0 on success or a negative error code on failure
 */
int amdgpu_seq64_map(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		     struct amdgpu_bo_va **bo_va, u64 seq64_addr,
		     uint32_t size)
{
	struct amdgpu_bo *bo;
	struct drm_exec exec;
	int r;

	bo = adev->seq64.sbo;
	if (!bo)
		return -EINVAL;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 0);
	drm_exec_until_all_locked(&exec) {
		r = amdgpu_vm_lock_pd(vm, &exec, 0);
		if (likely(!r))
			r = drm_exec_lock_obj(&exec, &bo->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto error;
	}

	*bo_va = amdgpu_vm_bo_add(adev, vm, bo);
	if (!*bo_va) {
		r = -ENOMEM;
		goto error;
	}

	r = amdgpu_vm_bo_map(adev, *bo_va, seq64_addr, 0, size,
			     AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE |
			     AMDGPU_PTE_EXECUTABLE);
	if (r) {
		DRM_ERROR("failed to do bo_map on userq sem, err=%d\n", r);
		amdgpu_vm_bo_del(adev, *bo_va);
		goto error;
	}

	r = amdgpu_vm_bo_update(adev, *bo_va, false);
	if (r) {
		DRM_ERROR("failed to do vm_bo_update on userq sem\n");
		amdgpu_vm_bo_del(adev, *bo_va);
		goto error;
	}

error:
	drm_exec_fini(&exec);
	return r;
}

/**
 * amdgpu_seq64_unmap - Unmap the seq64 memory
 *
 * @adev: amdgpu_device pointer
 * @fpriv: DRM file private
 *
 * Unmap the seq64 memory from the given VM.
 */
void amdgpu_seq64_unmap(struct amdgpu_device *adev, struct amdgpu_fpriv *fpriv)
{
	struct amdgpu_vm *vm;
	struct amdgpu_bo *bo;
	struct drm_exec exec;
	int r;

	if (!fpriv->seq64_va)
		return;

	bo = adev->seq64.sbo;
	if (!bo)
		return;

	vm = &fpriv->vm;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 0);
	drm_exec_until_all_locked(&exec) {
		r = amdgpu_vm_lock_pd(vm, &exec, 0);
		if (likely(!r))
			r = drm_exec_lock_obj(&exec, &bo->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto error;
	}

	amdgpu_vm_bo_del(adev, fpriv->seq64_va);

	fpriv->seq64_va = NULL;

error:
	drm_exec_fini(&exec);
}

/**
 * amdgpu_seq64_alloc - Allocate a 64 bit memory
 *
 * @adev: amdgpu_device pointer
 * @gpu_addr: allocated gpu VA start address
 * @cpu_addr: allocated cpu VA start address
 *
 * Alloc a 64 bit memory from seq64 pool.
 *
 * Returns:
 * 0 on success or a negative error code on failure
 */
int amdgpu_seq64_alloc(struct amdgpu_device *adev, u64 *gpu_addr,
		       u64 **cpu_addr)
{
	unsigned long bit_pos;
	u32 offset;

	bit_pos = find_first_zero_bit(adev->seq64.used, adev->seq64.num_sem);

	if (bit_pos < adev->seq64.num_sem) {
		__set_bit(bit_pos, adev->seq64.used);
		offset = bit_pos << 6; /* convert to qw offset */
	} else {
		return -EINVAL;
	}

	*gpu_addr = offset + AMDGPU_SEQ64_VADDR_START;
	*cpu_addr = offset + adev->seq64.cpu_base_addr;

	return 0;
}

/**
 * amdgpu_seq64_free - Free the given 64 bit memory
 *
 * @adev: amdgpu_device pointer
 * @gpu_addr: gpu start address to be freed
 *
 * Free the given 64 bit memory from seq64 pool.
 *
 */
void amdgpu_seq64_free(struct amdgpu_device *adev, u64 gpu_addr)
{
	u32 offset;

	offset = gpu_addr - AMDGPU_SEQ64_VADDR_START;

	offset >>= 6;
	if (offset < adev->seq64.num_sem)
		__clear_bit(offset, adev->seq64.used);
}

/**
 * amdgpu_seq64_fini - Cleanup seq64 driver
 *
 * @adev: amdgpu_device pointer
 *
 * Free the memory space allocated for seq64.
 *
 */
void amdgpu_seq64_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->seq64.sbo,
			      NULL,
			      (void **)&adev->seq64.cpu_base_addr);
}

/**
 * amdgpu_seq64_init - Initialize seq64 driver
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate the required memory space for seq64.
 *
 * Returns:
 * 0 on success or a negative error code on failure
 */
int amdgpu_seq64_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->seq64.sbo)
		return 0;

	/*
	 * AMDGPU_MAX_SEQ64_SLOTS * sizeof(u64) * 8 = AMDGPU_MAX_SEQ64_SLOTS
	 * 64bit slots
	 */
	r = amdgpu_bo_create_kernel(adev, AMDGPU_SEQ64_SIZE,
				    PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				    &adev->seq64.sbo, NULL,
				    (void **)&adev->seq64.cpu_base_addr);
	if (r) {
		dev_warn(adev->dev, "(%d) create seq64 failed\n", r);
		return r;
	}

	memset(adev->seq64.cpu_base_addr, 0, AMDGPU_SEQ64_SIZE);

	adev->seq64.num_sem = AMDGPU_MAX_SEQ64_SLOTS;
	memset(&adev->seq64.used, 0, sizeof(adev->seq64.used));

	return 0;
}
