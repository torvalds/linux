/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#ifndef __SOC_V1_0_H__
#define __SOC_V1_0_H__

extern const struct amdgpu_ip_block_version soc_v1_0_common_ip_block;

void soc_v1_0_grbm_select(struct amdgpu_device *adev,
			  u32 me, u32 pipe,
			  u32 queue, u32 vmid,
			  int xcc_id);
int soc_v1_0_init_soc_config(struct amdgpu_device *adev);
bool soc_v1_0_normalize_xcc_reg_range(uint32_t reg);
uint32_t soc_v1_0_normalize_xcc_reg_offset(uint32_t reg);
u64 soc_v1_0_encode_ext_smn_addressing(int ext_id);

#endif
