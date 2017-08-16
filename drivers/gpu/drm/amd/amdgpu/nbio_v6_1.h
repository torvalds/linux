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

#ifndef __NBIO_V6_1_H__
#define __NBIO_V6_1_H__

#include "soc15_common.h"

extern struct nbio_hdp_flush_reg nbio_v6_1_hdp_flush_reg;
extern struct nbio_pcie_index_data nbio_v6_1_pcie_index_data;
int nbio_v6_1_init(struct amdgpu_device *adev);
u32 nbio_v6_1_get_atombios_scratch_regs(struct amdgpu_device *adev,
                                        uint32_t idx);
void nbio_v6_1_set_atombios_scratch_regs(struct amdgpu_device *adev,
                                         uint32_t idx, uint32_t val);
void nbio_v6_1_mc_access_enable(struct amdgpu_device *adev, bool enable);
void nbio_v6_1_hdp_flush(struct amdgpu_device *adev);
u32 nbio_v6_1_get_memsize(struct amdgpu_device *adev);
void nbio_v6_1_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
				  bool use_doorbell, int doorbell_index);
void nbio_v6_1_enable_doorbell_aperture(struct amdgpu_device *adev,
					bool enable);
void nbio_v6_1_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
					bool enable);
void nbio_v6_1_ih_doorbell_range(struct amdgpu_device *adev,
				bool use_doorbell, int doorbell_index);
void nbio_v6_1_ih_control(struct amdgpu_device *adev);
u32 nbio_v6_1_get_rev_id(struct amdgpu_device *adev);
void nbio_v6_1_update_medium_grain_clock_gating(struct amdgpu_device *adev, bool enable);
void nbio_v6_1_update_medium_grain_light_sleep(struct amdgpu_device *adev, bool enable);
void nbio_v6_1_get_clockgating_state(struct amdgpu_device *adev, u32 *flags);
void nbio_v6_1_detect_hw_virt(struct amdgpu_device *adev);

#endif
