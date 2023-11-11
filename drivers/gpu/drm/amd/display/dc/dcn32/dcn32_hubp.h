/*
 * Copyright 2012-20 Advanced Micro Devices, Inc.
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

#ifndef __DC_HUBP_DCN32_H__
#define __DC_HUBP_DCN32_H__

#include "dcn20/dcn20_hubp.h"
#include "dcn21/dcn21_hubp.h"
#include "dcn30/dcn30_hubp.h"
#include "dcn31/dcn31_hubp.h"

#define HUBP_REG_LIST_DCN32(id)\
	HUBP_REG_LIST_DCN30(id),\
	SRI(DCHUBP_MALL_CONFIG, HUBP, id),\
	SRI(DCHUBP_VMPG_CONFIG, HUBP, id),\
	SRI(UCLK_PSTATE_FORCE, HUBPREQ, id)

#define HUBP_MASK_SH_LIST_DCN32(mask_sh)\
	HUBP_MASK_SH_LIST_DCN31(mask_sh),\
	HUBP_SF(HUBP0_DCHUBP_MALL_CONFIG, USE_MALL_SEL, mask_sh),\
	HUBP_SF(HUBP0_DCHUBP_MALL_CONFIG, USE_MALL_FOR_CURSOR, mask_sh),\
	HUBP_SF(HUBP0_DCHUBP_VMPG_CONFIG, VMPG_SIZE, mask_sh),\
	HUBP_SF(HUBP0_DCHUBP_VMPG_CONFIG, PTE_BUFFER_MODE, mask_sh),\
	HUBP_SF(HUBP0_DCHUBP_VMPG_CONFIG, BIGK_FRAGMENT_SIZE, mask_sh),\
	HUBP_SF(HUBP0_DCHUBP_VMPG_CONFIG, FORCE_ONE_ROW_FOR_FRAME, mask_sh),\
	HUBP_SF(HUBPREQ0_UCLK_PSTATE_FORCE, DATA_UCLK_PSTATE_FORCE_EN, mask_sh),\
	HUBP_SF(HUBPREQ0_UCLK_PSTATE_FORCE, DATA_UCLK_PSTATE_FORCE_VALUE, mask_sh),\
	HUBP_SF(HUBPREQ0_UCLK_PSTATE_FORCE, CURSOR_UCLK_PSTATE_FORCE_EN, mask_sh),\
	HUBP_SF(HUBPREQ0_UCLK_PSTATE_FORCE, CURSOR_UCLK_PSTATE_FORCE_VALUE, mask_sh)

void hubp32_update_force_pstate_disallow(struct hubp *hubp, bool pstate_disallow);

void hubp32_update_mall_sel(struct hubp *hubp, uint32_t mall_sel, bool c_cursor);

void hubp32_prepare_subvp_buffering(struct hubp *hubp, bool enable);

void hubp32_phantom_hubp_post_enable(struct hubp *hubp);

void hubp32_cursor_set_attributes(struct hubp *hubp,
		const struct dc_cursor_attributes *attr);

bool hubp32_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask);

#endif /* __DC_HUBP_DCN32_H__ */
