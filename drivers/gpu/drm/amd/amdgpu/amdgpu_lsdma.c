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

#include "amdgpu.h"
#include "amdgpu_lsdma.h"

#define AMDGPU_LSDMA_MAX_SIZE	0x2000000ULL

int amdgpu_lsdma_wait_for(struct amdgpu_device *adev,
			  uint32_t reg_index, uint32_t reg_val,
			  uint32_t mask)
{
	uint32_t val;
	int i;

	for (i = 0; i < adev->usec_timeout; i++) {
		val = RREG32(reg_index);
		if ((val & mask) == reg_val)
			return 0;
		udelay(1);
	}

	return -ETIME;
}

int amdgpu_lsdma_copy_mem(struct amdgpu_device *adev,
			  uint64_t src_addr,
			  uint64_t dst_addr,
			  uint64_t mem_size)
{
	int ret;

	if (mem_size == 0)
		return -EINVAL;

	while (mem_size > 0) {
		uint64_t current_copy_size = min(mem_size, AMDGPU_LSDMA_MAX_SIZE);

		ret = adev->lsdma.funcs->copy_mem(adev, src_addr, dst_addr, current_copy_size);
		if (ret)
			return ret;
		src_addr += current_copy_size;
		dst_addr += current_copy_size;
		mem_size -= current_copy_size;
	}

	return 0;
}

int amdgpu_lsdma_fill_mem(struct amdgpu_device *adev,
			  uint64_t dst_addr,
			  uint32_t data,
			  uint64_t mem_size)
{
	int ret;

	if (mem_size == 0)
		return -EINVAL;

	while (mem_size > 0) {
		uint64_t current_fill_size = min(mem_size, AMDGPU_LSDMA_MAX_SIZE);

		ret = adev->lsdma.funcs->fill_mem(adev, dst_addr, data, current_fill_size);
		if (ret)
			return ret;
		dst_addr += current_fill_size;
		mem_size -= current_fill_size;
	}

	return 0;
}
