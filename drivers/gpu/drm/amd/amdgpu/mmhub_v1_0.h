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
#ifndef __MMHUB_V1_0_H__
#define __MMHUB_V1_0_H__

u64 mmhub_v1_0_get_fb_location(struct amdgpu_device *adev);
int mmhub_v1_0_gart_enable(struct amdgpu_device *adev);
void mmhub_v1_0_gart_disable(struct amdgpu_device *adev);
void mmhub_v1_0_set_fault_enable_default(struct amdgpu_device *adev,
					 bool value);
void mmhub_v1_0_init(struct amdgpu_device *adev);
int mmhub_v1_0_set_clockgating(struct amdgpu_device *adev,
			       enum amd_clockgating_state state);
void mmhub_v1_0_get_clockgating(struct amdgpu_device *adev, u32 *flags);
void mmhub_v1_0_initialize_power_gating(struct amdgpu_device *adev);
void mmhub_v1_0_update_power_gating(struct amdgpu_device *adev,
                                bool enable);

extern const struct amd_ip_funcs mmhub_v1_0_ip_funcs;
extern const struct amdgpu_ip_block_version mmhub_v1_0_ip_block;

#endif
