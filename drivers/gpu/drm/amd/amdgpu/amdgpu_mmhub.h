/*
 * Copyright (C) 2019  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __AMDGPU_MMHUB_H__
#define __AMDGPU_MMHUB_H__

struct amdgpu_mmhub_funcs {
	void (*ras_init)(struct amdgpu_device *adev);
	int (*ras_late_init)(struct amdgpu_device *adev);
	void (*query_ras_error_count)(struct amdgpu_device *adev,
					void *ras_error_status);
	void (*reset_ras_error_count)(struct amdgpu_device *adev);
};

struct amdgpu_mmhub {
	struct ras_common_if *ras_if;
	const struct amdgpu_mmhub_funcs *funcs;
};

int amdgpu_mmhub_ras_late_init(struct amdgpu_device *adev);
void amdgpu_mmhub_ras_fini(struct amdgpu_device *adev);
#endif

