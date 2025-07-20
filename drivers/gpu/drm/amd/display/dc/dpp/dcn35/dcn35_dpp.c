/* SPDX-License-Identifier: MIT */
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
 * Authors: AMD
 *
 */

#include "core_types.h"
#include "dcn35/dcn35_dpp.h"
#include "reg_helper.h"

#define REG(reg) dpp->tf_regs->reg

#define CTX dpp->base.ctx

#undef FN
#define FN(reg_name, field_name)                                       \
	((const struct dcn35_dpp_shift *)(dpp->tf_shift))->field_name, \
	((const struct dcn35_dpp_mask *)(dpp->tf_mask))->field_name

void dpp35_dppclk_control(
		struct dpp *dpp_base,
		bool dppclk_div,
		bool enable)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	if (enable) {
		if (dpp->tf_mask->DPPCLK_RATE_CONTROL)
			REG_UPDATE_2(DPP_CONTROL,
				DPPCLK_RATE_CONTROL, dppclk_div,
				DPP_CLOCK_ENABLE, 1);
		else
			if (dpp->dispclk_r_gate_disable)
				REG_UPDATE_2(DPP_CONTROL,
					DPP_CLOCK_ENABLE, 1,
					DISPCLK_R_GATE_DISABLE, 1);
			else
				REG_UPDATE(DPP_CONTROL,
						DPP_CLOCK_ENABLE, 1);
	} else
		if (dpp->dispclk_r_gate_disable)
			REG_UPDATE_2(DPP_CONTROL,
				DPP_CLOCK_ENABLE, 0,
				DISPCLK_R_GATE_DISABLE, 0);
		else
			REG_UPDATE(DPP_CONTROL,
					DPP_CLOCK_ENABLE, 0);
}

void dpp35_program_bias_and_scale_fcnv(
	struct dpp *dpp_base,
	struct dc_bias_and_scale *params)
{
	struct dcn20_dpp *dpp = TO_DCN20_DPP(dpp_base);

	if (!params->bias_and_scale_valid) {
		REG_SET(FCNV_FP_BIAS_R, 0, FCNV_FP_BIAS_R, 0);
		REG_SET(FCNV_FP_BIAS_G, 0, FCNV_FP_BIAS_G, 0);
		REG_SET(FCNV_FP_BIAS_B, 0, FCNV_FP_BIAS_B, 0);

		REG_SET(FCNV_FP_SCALE_R, 0, FCNV_FP_SCALE_R, 0x1F000);
		REG_SET(FCNV_FP_SCALE_G, 0, FCNV_FP_SCALE_G, 0x1F000);
		REG_SET(FCNV_FP_SCALE_B, 0, FCNV_FP_SCALE_B, 0x1F000);
	} else {
		REG_SET(FCNV_FP_BIAS_R, 0, FCNV_FP_BIAS_R, params->bias_red);
		REG_SET(FCNV_FP_BIAS_G, 0, FCNV_FP_BIAS_G, params->bias_green);
		REG_SET(FCNV_FP_BIAS_B, 0, FCNV_FP_BIAS_B, params->bias_blue);

		REG_SET(FCNV_FP_SCALE_R, 0, FCNV_FP_SCALE_R, params->scale_red);
		REG_SET(FCNV_FP_SCALE_G, 0, FCNV_FP_SCALE_G, params->scale_green);
		REG_SET(FCNV_FP_SCALE_B, 0, FCNV_FP_SCALE_B, params->scale_blue);
	}
}

static struct dpp_funcs dcn35_dpp_funcs = {
	.dpp_program_gamcor_lut		= dpp3_program_gamcor_lut,
	.dpp_read_state				= dpp30_read_state,
	.dpp_reset					= dpp_reset,
	.dpp_set_scaler				= dpp1_dscl_set_scaler_manual_scale,
	.dpp_get_optimal_number_of_taps	= dpp3_get_optimal_number_of_taps,
	.dpp_set_gamut_remap		= dpp3_cm_set_gamut_remap,
	.dpp_set_csc_adjustment		= NULL,
	.dpp_set_csc_default		= NULL,
	.dpp_program_regamma_pwl	= NULL,
	.dpp_set_pre_degam			= dpp3_set_pre_degam,
	.dpp_program_input_lut		= NULL,
	.dpp_full_bypass			= dpp1_full_bypass,
	.dpp_setup					= dpp3_cnv_setup,
	.dpp_program_degamma_pwl	= NULL,
	.dpp_program_cm_dealpha		= dpp3_program_cm_dealpha,
	.dpp_program_cm_bias		= dpp3_program_cm_bias,

	.dpp_program_blnd_lut		= NULL, // BLNDGAM is removed completely in DCN3.2 DPP
	.dpp_program_shaper_lut		= NULL, // CM SHAPER block is removed in DCN3.2 DPP, (it is in MPCC, programmable before or after BLND)
	.dpp_program_3dlut			= NULL, // CM 3DLUT block is removed in DCN3.2 DPP, (it is in MPCC, programmable before or after BLND)

	.dpp_program_bias_and_scale	= dpp35_program_bias_and_scale_fcnv,
	.dpp_cnv_set_alpha_keyer	= dpp2_cnv_set_alpha_keyer,
	.set_cursor_attributes		= dpp3_set_cursor_attributes,
	.set_cursor_position		= dpp1_set_cursor_position,
	.set_optional_cursor_attributes	= dpp1_cnv_set_optional_cursor_attributes,
	.dpp_dppclk_control			= dpp35_dppclk_control,
	.dpp_set_hdr_multiplier		= dpp3_set_hdr_multiplier,
	.dpp_get_gamut_remap		= dpp3_cm_get_gamut_remap,
};


bool dpp35_construct(
	struct dcn3_dpp *dpp, struct dc_context *ctx,
	uint32_t inst, const struct dcn3_dpp_registers *tf_regs,
	const struct dcn35_dpp_shift *tf_shift,
	const struct dcn35_dpp_mask *tf_mask)
{
	bool ret = dpp32_construct(dpp, ctx, inst, tf_regs,
			      (const struct dcn3_dpp_shift *)(tf_shift),
			      (const struct dcn3_dpp_mask *)(tf_mask));

	dpp->base.funcs = &dcn35_dpp_funcs;

	// w/a for cursor memory stuck in LS by programming DISPCLK_R_GATE_DISABLE, limit w/a to some ASIC revs
	if (dpp->base.ctx->asic_id.hw_internal_rev < 0x40)
		dpp->dispclk_r_gate_disable = true;
	return ret;
}

void dpp35_set_fgcg(struct dcn3_dpp *dpp, bool enable)
{
	REG_UPDATE(DPP_CONTROL, DPP_FGCG_REP_DIS, !enable);
}
