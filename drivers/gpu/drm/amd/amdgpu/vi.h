/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __VI_H__
#define __VI_H__

void vi_srbm_select(struct amdgpu_device *adev,
		    u32 me, u32 pipe, u32 queue, u32 vmid);
int vi_set_ip_blocks(struct amdgpu_device *adev);

struct amdgpu_ce_ib_state
{
	uint32_t    ce_ib_completion_status;
	uint32_t    ce_constegnine_count;
	uint32_t    ce_ibOffset_ib1;
	uint32_t    ce_ibOffset_ib2;
}; /* Total of 4 DWORD */

struct amdgpu_de_ib_state
{
	uint32_t    ib_completion_status;
	uint32_t    de_constEngine_count;
	uint32_t    ib_offset_ib1;
	uint32_t    ib_offset_ib2;
	uint32_t    preamble_begin_ib1;
	uint32_t    preamble_begin_ib2;
	uint32_t    preamble_end_ib1;
	uint32_t    preamble_end_ib2;
	uint32_t    draw_indirect_baseLo;
	uint32_t    draw_indirect_baseHi;
	uint32_t    disp_indirect_baseLo;
	uint32_t    disp_indirect_baseHi;
	uint32_t    gds_backup_addrlo;
	uint32_t    gds_backup_addrhi;
	uint32_t    index_base_addrlo;
	uint32_t    index_base_addrhi;
	uint32_t    sample_cntl;
}; /* Total of 17 DWORD */

struct amdgpu_ce_ib_state_chained_ib
{
	/* section of non chained ib part */
	uint32_t    ce_ib_completion_status;
	uint32_t    ce_constegnine_count;
	uint32_t    ce_ibOffset_ib1;
	uint32_t    ce_ibOffset_ib2;

	/* section of chained ib */
	uint32_t    ce_chainib_addrlo_ib1;
	uint32_t    ce_chainib_addrlo_ib2;
	uint32_t    ce_chainib_addrhi_ib1;
	uint32_t    ce_chainib_addrhi_ib2;
	uint32_t    ce_chainib_size_ib1;
	uint32_t    ce_chainib_size_ib2;
}; /* total 10 DWORD */

struct amdgpu_de_ib_state_chained_ib
{
	/* section of non chained ib part */
	uint32_t    ib_completion_status;
	uint32_t    de_constEngine_count;
	uint32_t    ib_offset_ib1;
	uint32_t    ib_offset_ib2;

	/* section of chained ib */
	uint32_t    chain_ib_addrlo_ib1;
	uint32_t    chain_ib_addrlo_ib2;
	uint32_t    chain_ib_addrhi_ib1;
	uint32_t    chain_ib_addrhi_ib2;
	uint32_t    chain_ib_size_ib1;
	uint32_t    chain_ib_size_ib2;

	/* section of non chained ib part */
	uint32_t    preamble_begin_ib1;
	uint32_t    preamble_begin_ib2;
	uint32_t    preamble_end_ib1;
	uint32_t    preamble_end_ib2;

	/* section of chained ib */
	uint32_t    chain_ib_pream_addrlo_ib1;
	uint32_t    chain_ib_pream_addrlo_ib2;
	uint32_t    chain_ib_pream_addrhi_ib1;
	uint32_t    chain_ib_pream_addrhi_ib2;

	/* section of non chained ib part */
	uint32_t    draw_indirect_baseLo;
	uint32_t    draw_indirect_baseHi;
	uint32_t    disp_indirect_baseLo;
	uint32_t    disp_indirect_baseHi;
	uint32_t    gds_backup_addrlo;
	uint32_t    gds_backup_addrhi;
	uint32_t    index_base_addrlo;
	uint32_t    index_base_addrhi;
	uint32_t    sample_cntl;
}; /* Total of 27 DWORD */

struct amdgpu_gfx_meta_data
{
	/* 4 DWORD, address must be 4KB aligned */
	struct amdgpu_ce_ib_state    ce_payload;
	uint32_t                     reserved1[60];
	/* 17 DWORD, address must be 64B aligned */
	struct amdgpu_de_ib_state    de_payload;
	/* PFP IB base address which get pre-empted */
	uint32_t                     DeIbBaseAddrLo;
	uint32_t                     DeIbBaseAddrHi;
	uint32_t                     reserved2[941];
}; /* Total of 4K Bytes */

struct amdgpu_gfx_meta_data_chained_ib
{
	/* 10 DWORD, address must be 4KB aligned */
	struct amdgpu_ce_ib_state_chained_ib   ce_payload;
	uint32_t                               reserved1[54];
	/* 27 DWORD, address must be 64B aligned */
	struct amdgpu_de_ib_state_chained_ib   de_payload;
	/* PFP IB base address which get pre-empted */
	uint32_t                               DeIbBaseAddrLo;
	uint32_t                               DeIbBaseAddrHi;
	uint32_t                               reserved2[931];
}; /* Total of 4K Bytes */

#endif
