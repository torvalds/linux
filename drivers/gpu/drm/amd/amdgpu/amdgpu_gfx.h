/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_GFX_H__
#define __AMDGPU_GFX_H__

int amdgpu_gfx_scratch_get(struct amdgpu_device *adev, uint32_t *reg);
void amdgpu_gfx_scratch_free(struct amdgpu_device *adev, uint32_t reg);

void amdgpu_gfx_parse_disable_cu(unsigned *mask, unsigned max_se,
		unsigned max_sh);

void amdgpu_gfx_compute_queue_acquire(struct amdgpu_device *adev);

/**
 * amdgpu_gfx_create_bitmask - create a bitmask
 *
 * @bit_width: length of the mask
 *
 * create a variable length bit mask.
 * Returns the bitmask.
 */
static inline u32 amdgpu_gfx_create_bitmask(u32 bit_width)
{
	return (u32)((1ULL << bit_width) - 1);
}

#endif
