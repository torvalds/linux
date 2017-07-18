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
 * Author: Huang Rui
 *
 */
#ifndef __PSP_V10_0_H__
#define __PSP_V10_0_H__

#include "amdgpu_psp.h"

extern int psp_v10_0_prep_cmd_buf(struct amdgpu_firmware_info *ucode,
				 struct psp_gfx_cmd_resp *cmd);
extern int psp_v10_0_ring_init(struct psp_context *psp,
			      enum psp_ring_type ring_type);
extern int psp_v10_0_cmd_submit(struct psp_context *psp,
			       struct amdgpu_firmware_info *ucode,
			       uint64_t cmd_buf_mc_addr, uint64_t fence_mc_addr,
			       int index);
extern bool psp_v10_0_compare_sram_data(struct psp_context *psp,
				       struct amdgpu_firmware_info *ucode,
				       enum AMDGPU_UCODE_ID ucode_type);
#endif
