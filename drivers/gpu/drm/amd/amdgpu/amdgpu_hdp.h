/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#ifndef __AMDGPU_HDP_H__
#define __AMDGPU_HDP_H__
#include "amdgpu_ras.h"

struct amdgpu_hdp_ras {
	struct amdgpu_ras_block_object ras_block;
};

struct amdgpu_hdp_funcs {
	void (*flush_hdp)(struct amdgpu_device *adev, struct amdgpu_ring *ring);
	void (*invalidate_hdp)(struct amdgpu_device *adev,
			       struct amdgpu_ring *ring);
	void (*update_clock_gating)(struct amdgpu_device *adev, bool enable);
	void (*get_clock_gating_state)(struct amdgpu_device *adev, u32 *flags);
	void (*init_registers)(struct amdgpu_device *adev);
};

struct amdgpu_hdp {
	struct ras_common_if			*ras_if;
	const struct amdgpu_hdp_funcs		*funcs;
	struct amdgpu_hdp_ras	*ras;
};

int amdgpu_hdp_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block);
void amdgpu_hdp_ras_fini(struct amdgpu_device *adev);
#endif /* __AMDGPU_HDP_H__ */
