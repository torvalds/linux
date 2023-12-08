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

#ifndef __VCN_SW_RING_H__
#define __VCN_SW_RING_H__

#define VCN_SW_RING_EMIT_FRAME_SIZE \
		(4 + /* vcn_dec_sw_ring_emit_vm_flush */ \
		5 + 5 + /* vcn_dec_sw_ring_emit_fence x2 vm fence */ \
		1) /* vcn_dec_sw_ring_insert_end */

void vcn_dec_sw_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
	u64 seq, uint32_t flags);
void vcn_dec_sw_ring_insert_end(struct amdgpu_ring *ring);
void vcn_dec_sw_ring_emit_ib(struct amdgpu_ring *ring, struct amdgpu_job *job,
	struct amdgpu_ib *ib, uint32_t flags);
void vcn_dec_sw_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
	uint32_t val, uint32_t mask);
void vcn_dec_sw_ring_emit_vm_flush(struct amdgpu_ring *ring,
	uint32_t vmid, uint64_t pd_addr);
void vcn_dec_sw_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg,
	uint32_t val);

#endif /* __VCN_SW_RING_H__ */
