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

#ifndef __JPEG_V4_0_3_H__
#define __JPEG_V4_0_3_H__

#define regUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET			0x1bfff
#define regUVD_JPEG_GPCOM_CMD_INTERNAL_OFFSET				0x404d
#define regUVD_JPEG_GPCOM_DATA0_INTERNAL_OFFSET				0x404e
#define regUVD_JPEG_GPCOM_DATA1_INTERNAL_OFFSET				0x404f
#define regUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_LOW_INTERNAL_OFFSET		0x40ab
#define regUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_HIGH_INTERNAL_OFFSET	0x40ac
#define regUVD_LMI_JRBC_IB_VMID_INTERNAL_OFFSET				0x40a4
#define regUVD_LMI_JPEG_VMID_INTERNAL_OFFSET				0x40a6
#define regUVD_LMI_JRBC_IB_64BIT_BAR_LOW_INTERNAL_OFFSET		0x40b6
#define regUVD_LMI_JRBC_IB_64BIT_BAR_HIGH_INTERNAL_OFFSET		0x40b7
#define regUVD_JRBC_IB_SIZE_INTERNAL_OFFSET				0x4082
#define regUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW_INTERNAL_OFFSET		0x42d4
#define regUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH_INTERNAL_OFFSET	0x42d5
#define regUVD_JRBC_RB_COND_RD_TIMER_INTERNAL_OFFSET			0x4085
#define regUVD_JRBC_RB_REF_DATA_INTERNAL_OFFSET				0x4084
#define regUVD_JRBC_STATUS_INTERNAL_OFFSET				0x4089
#define regUVD_JPEG_PITCH_INTERNAL_OFFSET				0x4043
#define regUVD_JRBC0_UVD_JRBC_SCRATCH0_INTERNAL_OFFSET			0x4094
#define regUVD_JRBC_EXTERNAL_MCM_ADDR_INTERNAL_OFFSET			0x1bffe

#define JRBC_DEC_EXTERNAL_REG_WRITE_ADDR				0x18000

#define JPEG_REG_RANGE_START						0x4000
#define JPEG_REG_RANGE_END						0x41c2

extern const struct amdgpu_ip_block_version jpeg_v4_0_3_ip_block;

void jpeg_v4_0_3_dec_ring_emit_ib(struct amdgpu_ring *ring,
				struct amdgpu_job *job,
				struct amdgpu_ib *ib,
				uint32_t flags);
void jpeg_v4_0_3_dec_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				unsigned int flags);
void jpeg_v4_0_3_dec_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned int vmid, uint64_t pd_addr);
void jpeg_v4_0_3_dec_ring_nop(struct amdgpu_ring *ring, uint32_t count);
void jpeg_v4_0_3_dec_ring_insert_start(struct amdgpu_ring *ring);
void jpeg_v4_0_3_dec_ring_insert_end(struct amdgpu_ring *ring);
void jpeg_v4_0_3_dec_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg, uint32_t val);
void jpeg_v4_0_3_dec_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					uint32_t val, uint32_t mask);
int jpeg_v4_0_3_dec_ring_parse_cs(struct amdgpu_cs_parser *parser,
				  struct amdgpu_job *job,
				  struct amdgpu_ib *ib);
#endif /* __JPEG_V4_0_3_H__ */
