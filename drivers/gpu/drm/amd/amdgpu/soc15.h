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

#ifndef __SOC15_H__
#define __SOC15_H__

#include "nbio_v6_1.h"
#include "nbio_v7_0.h"

#define SOC15_FLUSH_GPU_TLB_NUM_WREG		4
#define SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT	1

extern const struct amd_ip_funcs soc15_common_ip_funcs;

struct soc15_reg_golden {
	u32	hwip;
	u32	instance;
	u32	segment;
	u32	reg;
	u32	and_mask;
	u32	or_mask;
};

#define SOC15_REG_ENTRY(ip, inst, reg)	ip##_HWIP, inst, reg##_BASE_IDX, reg

#define SOC15_REG_GOLDEN_VALUE(ip, inst, reg, and_mask, or_mask) \
	{ ip##_HWIP, inst, reg##_BASE_IDX, reg, and_mask, or_mask }

void soc15_grbm_select(struct amdgpu_device *adev,
		    u32 me, u32 pipe, u32 queue, u32 vmid);
int soc15_set_ip_blocks(struct amdgpu_device *adev);

void soc15_program_register_sequence(struct amdgpu_device *adev,
					     const struct soc15_reg_golden *registers,
					     const u32 array_size);

int vega10_reg_base_init(struct amdgpu_device *adev);
int vega20_reg_base_init(struct amdgpu_device *adev);

#endif
