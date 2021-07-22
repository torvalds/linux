/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef DAL_DC_DCN20_DCN20_VMID_H_
#define DAL_DC_DCN20_DCN20_VMID_H_

#include "vmid.h"

#define BASE_INNER(seg) \
	DCE_BASE__INST0_SEG ## seg

#define BASE(seg) \
	BASE_INNER(seg)

#define DCN20_VMID_REG_LIST(id)\
	SRI(CNTL, DCN_VM_CONTEXT, id),\
	SRI(PAGE_TABLE_BASE_ADDR_HI32, DCN_VM_CONTEXT, id),\
	SRI(PAGE_TABLE_BASE_ADDR_LO32, DCN_VM_CONTEXT, id),\
	SRI(PAGE_TABLE_START_ADDR_HI32, DCN_VM_CONTEXT, id),\
	SRI(PAGE_TABLE_START_ADDR_LO32, DCN_VM_CONTEXT, id),\
	SRI(PAGE_TABLE_END_ADDR_HI32, DCN_VM_CONTEXT, id),\
	SRI(PAGE_TABLE_END_ADDR_LO32, DCN_VM_CONTEXT, id)

#define DCN20_VMID_MASK_SH_LIST(mask_sh)\
	SF(DCN_VM_CONTEXT0_CNTL, VM_CONTEXT0_PAGE_TABLE_DEPTH, mask_sh),\
	SF(DCN_VM_CONTEXT0_CNTL, VM_CONTEXT0_PAGE_TABLE_BLOCK_SIZE, mask_sh),\
	SF(DCN_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32, VM_CONTEXT0_PAGE_DIRECTORY_ENTRY_HI32, mask_sh),\
	SF(DCN_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32, VM_CONTEXT0_PAGE_DIRECTORY_ENTRY_LO32, mask_sh),\
	SF(DCN_VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32, VM_CONTEXT0_START_LOGICAL_PAGE_NUMBER_HI4, mask_sh),\
	SF(DCN_VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32, VM_CONTEXT0_START_LOGICAL_PAGE_NUMBER_LO32, mask_sh),\
	SF(DCN_VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32, VM_CONTEXT0_END_LOGICAL_PAGE_NUMBER_HI4, mask_sh),\
	SF(DCN_VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32, VM_CONTEXT0_END_LOGICAL_PAGE_NUMBER_LO32, mask_sh)

#define DCN20_VMID_REG_FIELD_LIST(type)\
	type VM_CONTEXT0_PAGE_TABLE_DEPTH;\
	type VM_CONTEXT0_PAGE_TABLE_BLOCK_SIZE;\
	type VM_CONTEXT0_PAGE_DIRECTORY_ENTRY_HI32;\
	type VM_CONTEXT0_PAGE_DIRECTORY_ENTRY_LO32;\
	type VM_CONTEXT0_START_LOGICAL_PAGE_NUMBER_HI4;\
	type VM_CONTEXT0_START_LOGICAL_PAGE_NUMBER_LO32;\
	type VM_CONTEXT0_END_LOGICAL_PAGE_NUMBER_HI4;\
	type VM_CONTEXT0_END_LOGICAL_PAGE_NUMBER_LO32

struct dcn20_vmid_shift {
	DCN20_VMID_REG_FIELD_LIST(uint8_t);
};

struct dcn20_vmid_mask {
	DCN20_VMID_REG_FIELD_LIST(uint32_t);
};

struct dcn20_vmid {
	struct dc_context *ctx;
	const struct dcn_vmid_registers *regs;
	const struct dcn20_vmid_shift *shifts;
	const struct dcn20_vmid_mask *masks;
};

void dcn20_vmid_setup(struct dcn20_vmid *vmid, const struct dcn_vmid_page_table_config *config);

#endif /* DAL_DC_DCN20_DCN20_VMID_H_ */
