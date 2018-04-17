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

#ifndef __NBIO_V7_0_H__
#define __NBIO_V7_0_H__

#include "soc15_common.h"

extern const struct nbio_hdp_flush_reg nbio_v7_0_hdp_flush_reg;
extern const struct nbio_pcie_index_data nbio_v7_0_pcie_index_data;
int nbio_v7_0_init(struct amdgpu_device *adev);
u32 nbio_v7_0_get_atombios_scratch_regs(struct amdgpu_device *adev,
                                        uint32_t idx);
void nbio_v7_0_set_atombios_scratch_regs(struct amdgpu_device *adev,
                                         uint32_t idx, uint32_t val);
void nbio_v7_0_mc_access_enable(struct amdgpu_device *adev, bool enable);
void nbio_v7_0_hdp_flush(struct amdgpu_device *adev);
u32 nbio_v7_0_get_memsize(struct amdgpu_device *adev);
void nbio_v7_0_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
				  bool use_doorbell, int doorbell_index);
void nbio_v7_0_enable_doorbell_aperture(struct amdgpu_device *adev,
					bool enable);
void nbio_v7_0_ih_doorbell_range(struct amdgpu_device *adev,
				bool use_doorbell, int doorbell_index);
void nbio_v7_0_ih_control(struct amdgpu_device *adev);
u32 nbio_v7_0_get_rev_id(struct amdgpu_device *adev);
void nbio_v7_0_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						bool enable);
#endif
