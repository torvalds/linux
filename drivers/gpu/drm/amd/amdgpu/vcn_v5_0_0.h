/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __VCN_V5_0_0_H__
#define __VCN_V5_0_0_H__

#define VCN_VID_SOC_ADDRESS                        0x1FC00
#define VCN_AON_SOC_ADDRESS                        0x1F800
#define VCN1_VID_SOC_ADDRESS                       0x48300
#define VCN1_AON_SOC_ADDRESS                       0x48000

#define VCN_VID_IP_ADDRESS                         0x0
#define VCN_AON_IP_ADDRESS                         0x30000

void vcn_v5_0_0_alloc_ip_dump(struct amdgpu_device *adev);
void vcn_v5_0_0_print_ip_state(struct amdgpu_ip_block *ip_block,
			       struct drm_printer *p);
void vcn_v5_0_0_dump_ip_state(struct amdgpu_ip_block *ip_block);

extern const struct amdgpu_ip_block_version vcn_v5_0_0_ip_block;

#endif /* __VCN_V5_0_0_H__ */
