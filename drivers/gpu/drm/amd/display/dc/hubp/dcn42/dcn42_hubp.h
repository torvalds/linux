/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#ifndef __DC_HUBP_DCN42_H__
#define __DC_HUBP_DCN42_H__

#include "dcn35/dcn35_hubp.h"

#define HUBP_MASK_SH_LIST_DCN42(mask_sh)\
	HUBP_MASK_SH_LIST_DCN35(mask_sh),\
	HUBP_SF(HUBP0_3DLUT_FL_CONFIG, HUBP0_3DLUT_FL_MODE, mask_sh),\
	HUBP_SF(HUBP0_3DLUT_FL_CONFIG, HUBP0_3DLUT_FL_FORMAT, mask_sh),\
	HUBP_SF(HUBP0_3DLUT_FL_BIAS_SCALE, HUBP0_3DLUT_FL_BIAS, mask_sh),\
	HUBP_SF(HUBP0_3DLUT_FL_BIAS_SCALE, HUBP0_3DLUT_FL_SCALE, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_CONTROL, HUBP_3DLUT_ENABLE, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_CONTROL, HUBP_3DLUT_DONE, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_CONTROL, HUBP_3DLUT_ADDRESSING_MODE, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_CONTROL, HUBP_3DLUT_WIDTH, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_CONTROL, HUBP_3DLUT_MPC_WIDTH, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_CONTROL, HUBP_3DLUT_TMZ, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_CONTROL, HUBP_3DLUT_CROSSBAR_SEL_B, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_CONTROL, HUBP_3DLUT_CROSSBAR_SEL_G, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_CONTROL, HUBP_3DLUT_CROSSBAR_SEL_R, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_ADDRESS_HIGH, HUBP_3DLUT_ADDRESS_HIGH, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_ADDRESS_LOW, HUBP_3DLUT_ADDRESS_LOW, mask_sh),\
	HUBP_SF(CURSOR0_0_HUBP_3DLUT_DLG_PARAM, REFCYC_PER_3DLUT_GROUP, mask_sh)

bool hubp42_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask);

void hubp42_program_3dlut_fl_crossbar(
	struct hubp *hubp,
	enum hubp_3dlut_fl_crossbar_bit_slice bit_slice_r,
	enum hubp_3dlut_fl_crossbar_bit_slice bit_slice_g,
	enum hubp_3dlut_fl_crossbar_bit_slice bit_slice_b);

void hubp42_read_state(struct hubp *hubp);

void hubp42_setup(
		struct hubp *hubp,
	    struct dml2_dchub_per_pipe_register_set *pipe_regs,
		union dml2_global_sync_programming *pipe_global_sync,
		struct dc_crtc_timing *timing);

void hubp42_program_deadline(
		struct hubp *hubp,
		struct dml2_display_dlg_regs *dlg_attr,
		struct dml2_display_ttu_regs *ttu_attr);


#endif /* __DC_HUBP_DCN42_H__ */
