/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#ifndef __AMDGPU_ISP_H__
#define __AMDGPU_ISP_H__

#define ISP_REGS_OFFSET_END 0x629A4

struct amdgpu_isp;

struct isp_platform_data {
	void *adev;
	u32 asic_type;
	resource_size_t base_rmmio_size;
};

struct isp_funcs {
	int (*hw_init)(struct amdgpu_isp *isp);
	int (*hw_fini)(struct amdgpu_isp *isp);
};

struct amdgpu_isp {
	struct device *parent;
	struct amdgpu_device	*adev;
	const struct isp_funcs	*funcs;
	struct mfd_cell *isp_cell;
	struct resource *isp_res;
	struct resource *isp_i2c_res;
	struct isp_platform_data *isp_pdata;
	unsigned int harvest_config;
	const struct firmware	*fw;
};

extern const struct amdgpu_ip_block_version isp_v4_1_0_ip_block;
extern const struct amdgpu_ip_block_version isp_v4_1_1_ip_block;

#endif /* __AMDGPU_ISP_H__ */
