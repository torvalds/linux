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

#ifndef __AMDGPU_SEQ64_H__
#define __AMDGPU_SEQ64_H__

#include "amdgpu_vm.h"

#define AMDGPU_MAX_SEQ64_SLOTS         (AMDGPU_VA_RESERVED_SEQ64_SIZE / sizeof(u64))

struct amdgpu_seq64 {
	struct amdgpu_bo *sbo;
	u32 num_sem;
	u64 *cpu_base_addr;
	DECLARE_BITMAP(used, AMDGPU_MAX_SEQ64_SLOTS);
};

void amdgpu_seq64_fini(struct amdgpu_device *adev);
int amdgpu_seq64_init(struct amdgpu_device *adev);
int amdgpu_seq64_alloc(struct amdgpu_device *adev, u64 *gpu_addr, u64 **cpu_addr);
void amdgpu_seq64_free(struct amdgpu_device *adev, u64 gpu_addr);
int amdgpu_seq64_map(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		     struct amdgpu_bo_va **bo_va);
void amdgpu_seq64_unmap(struct amdgpu_device *adev, struct amdgpu_fpriv *fpriv);

#endif

