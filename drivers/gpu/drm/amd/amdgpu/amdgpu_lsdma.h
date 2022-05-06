/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_LSDMA_H__
#define __AMDGPU_LSDMA_H__

struct amdgpu_lsdma {
	const struct amdgpu_lsdma_funcs      *funcs;
};

struct amdgpu_lsdma_funcs {
	int (*copy_mem)(struct amdgpu_device *adev, uint64_t src_addr,
			uint64_t dst_addr, uint64_t size);
	int (*fill_mem)(struct amdgpu_device *adev, uint64_t dst_addr,
			uint32_t data, uint64_t size);
	void (*update_memory_power_gating)(struct amdgpu_device *adev, bool enable);
};

int amdgpu_lsdma_copy_mem(struct amdgpu_device *adev, uint64_t src_addr,
			  uint64_t dst_addr, uint64_t mem_size);
int amdgpu_lsdma_fill_mem(struct amdgpu_device *adev, uint64_t dst_addr,
			  uint32_t data, uint64_t mem_size);
int amdgpu_lsdma_wait_for(struct amdgpu_device *adev, uint32_t reg_index,
			  uint32_t reg_val, uint32_t mask);

#endif
