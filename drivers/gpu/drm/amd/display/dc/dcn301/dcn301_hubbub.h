/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#ifndef DAL_DC_DCN301_DCN301_HUBBUB_H_
#define DAL_DC_DCN301_DCN301_HUBBUB_H_

#include "dcn30/dcn30_hubbub.h"


#define HUBBUB_REG_LIST_DCN301(id)\
	HUBBUB_REG_LIST_DCN30(id), \
	HUBBUB_HVM_REG_LIST()


#define HUBBUB_MASK_SH_LIST_DCN301(mask_sh)\
	HUBBUB_MASK_SH_LIST_DCN30(mask_sh), \
	HUBBUB_SF(DCHVM_CTRL0, HOSTVM_INIT_REQ, mask_sh), \
	HUBBUB_SF(DCHVM_MEM_CTRL, HVM_GPUVMRET_PWR_REQ_DIS, mask_sh), \
	HUBBUB_SF(DCHVM_MEM_CTRL, HVM_GPUVMRET_FORCE_REQ, mask_sh), \
	HUBBUB_SF(DCHVM_MEM_CTRL, HVM_GPUVMRET_POWER_STATUS, mask_sh), \
	HUBBUB_SF(DCHVM_CLK_CTRL, HVM_DISPCLK_R_GATE_DIS, mask_sh), \
	HUBBUB_SF(DCHVM_CLK_CTRL, HVM_DISPCLK_G_GATE_DIS, mask_sh), \
	HUBBUB_SF(DCHVM_CLK_CTRL, HVM_DCFCLK_R_GATE_DIS, mask_sh), \
	HUBBUB_SF(DCHVM_CLK_CTRL, HVM_DCFCLK_G_GATE_DIS, mask_sh), \
	HUBBUB_SF(DCHVM_CLK_CTRL, TR_REQ_REQCLKREQ_MODE, mask_sh), \
	HUBBUB_SF(DCHVM_CLK_CTRL, TW_RSP_COMPCLKREQ_MODE, mask_sh), \
	HUBBUB_SF(DCHVM_RIOMMU_CTRL0, HOSTVM_PREFETCH_REQ, mask_sh), \
	HUBBUB_SF(DCHVM_RIOMMU_CTRL0, HOSTVM_POWERSTATUS, mask_sh), \
	HUBBUB_SF(DCHVM_RIOMMU_STAT0, RIOMMU_ACTIVE, mask_sh), \
	HUBBUB_SF(DCHVM_RIOMMU_STAT0, HOSTVM_PREFETCH_DONE, mask_sh)

void hubbub301_construct(struct dcn20_hubbub *hubbub3,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask);


#endif /* DAL_DC_DCN301_DCN301_HUBBUB_H_ */
