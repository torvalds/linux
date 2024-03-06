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
 * Author: Monk.liu@amd.com
 */

#ifndef AMDGPU_CSA_MANAGER_H
#define AMDGPU_CSA_MANAGER_H

#define AMDGPU_CSA_SIZE		(128 * 1024)

uint32_t amdgpu_get_total_csa_size(struct amdgpu_device *adev);
uint64_t amdgpu_csa_vaddr(struct amdgpu_device *adev);
int amdgpu_allocate_static_csa(struct amdgpu_device *adev, struct amdgpu_bo **bo,
				u32 domain, uint32_t size);
int amdgpu_map_static_csa(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			  struct amdgpu_bo *bo, struct amdgpu_bo_va **bo_va,
			  uint64_t csa_addr, uint32_t size);
int amdgpu_unmap_static_csa(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			    struct amdgpu_bo *bo, struct amdgpu_bo_va *bo_va,
			    uint64_t csa_addr);
void amdgpu_free_static_csa(struct amdgpu_bo **bo);

#endif
