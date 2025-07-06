/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __VCN_V4_0_3_H__
#define __VCN_V4_0_3_H__

enum amdgpu_vcn_v4_0_3_sub_block {
	AMDGPU_VCN_V4_0_3_VCPU_VCODEC = 0,

	AMDGPU_VCN_V4_0_3_MAX_SUB_BLOCK,
};

extern const struct amdgpu_ip_block_version vcn_v4_0_3_ip_block;

void vcn_v4_0_3_enc_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
				       uint32_t val, uint32_t mask);

void vcn_v4_0_3_enc_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg,
				   uint32_t val);
void vcn_v4_0_3_enc_ring_emit_vm_flush(struct amdgpu_ring *ring,
				       unsigned int vmid, uint64_t pd_addr);
void vcn_v4_0_3_ring_emit_hdp_flush(struct amdgpu_ring *ring);

#endif /* __VCN_V4_0_3_H__ */
