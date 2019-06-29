/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dcn20_mpc.h"

#include "reg_helper.h"
#include "dc.h"
#include "mem_input.h"
#include "dcn10/dcn10_cm_common.h"

#define REG(reg)\
	mpc20->mpc_regs->reg

#define CTX \
	mpc20->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mpc20->mpc_shift->field_name, mpc20->mpc_mask->field_name

#define NUM_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

void mpc2_update_blending(
	struct mpc *mpc,
	struct mpcc_blnd_cfg *blnd_cfg,
	int mpcc_id)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);

	struct mpcc *mpcc = mpc1_get_mpcc(mpc, mpcc_id);

	REG_UPDATE_7(MPCC_CONTROL[mpcc_id],
			MPCC_ALPHA_BLND_MODE,		blnd_cfg->alpha_mode,
			MPCC_ALPHA_MULTIPLIED_MODE,	blnd_cfg->pre_multiplied_alpha,
			MPCC_BLND_ACTIVE_OVERLAP_ONLY,	blnd_cfg->overlap_only,
			MPCC_GLOBAL_ALPHA,		blnd_cfg->global_alpha,
			MPCC_GLOBAL_GAIN,		blnd_cfg->global_gain,
			MPCC_BG_BPC,			blnd_cfg->background_color_bpc,
			MPCC_BOT_GAIN_MODE,		blnd_cfg->bottom_gain_mode);

	REG_SET(MPCC_TOP_GAIN[mpcc_id], 0, MPCC_TOP_GAIN, blnd_cfg->top_gain);
	REG_SET(MPCC_BOT_GAIN_INSIDE[mpcc_id], 0, MPCC_BOT_GAIN_INSIDE, blnd_cfg->bottom_inside_gain);
	REG_SET(MPCC_BOT_GAIN_OUTSIDE[mpcc_id], 0, MPCC_BOT_GAIN_OUTSIDE, blnd_cfg->bottom_outside_gain);

	mpc1_set_bg_color(mpc, &blnd_cfg->black_color, mpcc_id);
	mpcc->blnd_cfg = *blnd_cfg;
}

void mpc2_set_denorm(
		struct mpc *mpc,
		int opp_id,
		enum dc_color_depth output_depth)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);
	int denorm_mode = 0;

	switch (output_depth) {
	case COLOR_DEPTH_666:
		denorm_mode = 1;
		break;
	case COLOR_DEPTH_888:
		denorm_mode = 2;
		break;
	case COLOR_DEPTH_999:
		denorm_mode = 3;
		break;
	case COLOR_DEPTH_101010:
		denorm_mode = 4;
		break;
	case COLOR_DEPTH_111111:
		denorm_mode = 5;
		break;
	case COLOR_DEPTH_121212:
		denorm_mode = 6;
		break;
	case COLOR_DEPTH_141414:
	case COLOR_DEPTH_161616:
	default:
		/* not valid used case! */
		break;
	}

	REG_UPDATE(DENORM_CONTROL[opp_id],
			MPC_OUT_DENORM_MODE, denorm_mode);
}

void mpc2_set_denorm_clamp(
		struct mpc *mpc,
		int opp_id,
		struct mpc_denorm_clamp denorm_clamp)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);

	REG_UPDATE_2(DENORM_CONTROL[opp_id],
			MPC_OUT_DENORM_CLAMP_MAX_R_CR, denorm_clamp.clamp_max_r_cr,
			MPC_OUT_DENORM_CLAMP_MIN_R_CR, denorm_clamp.clamp_min_r_cr);
	REG_UPDATE_2(DENORM_CLAMP_G_Y[opp_id],
			MPC_OUT_DENORM_CLAMP_MAX_G_Y, denorm_clamp.clamp_max_g_y,
			MPC_OUT_DENORM_CLAMP_MIN_G_Y, denorm_clamp.clamp_min_g_y);
	REG_UPDATE_2(DENORM_CLAMP_B_CB[opp_id],
			MPC_OUT_DENORM_CLAMP_MAX_B_CB, denorm_clamp.clamp_max_b_cb,
			MPC_OUT_DENORM_CLAMP_MIN_B_CB, denorm_clamp.clamp_min_b_cb);
}



void mpc2_set_output_csc(
		struct mpc *mpc,
		int opp_id,
		const uint16_t *regval,
		enum mpc_output_csc_mode ocsc_mode)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);
	struct color_matrices_reg ocsc_regs;

	REG_SET(CSC_MODE[opp_id], 0, MPC_OCSC_MODE, ocsc_mode);

	if (ocsc_mode == MPC_OUTPUT_CSC_DISABLE)
		return;

	if (regval == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	ocsc_regs.shifts.csc_c11 = mpc20->mpc_shift->MPC_OCSC_C11_A;
	ocsc_regs.masks.csc_c11  = mpc20->mpc_mask->MPC_OCSC_C11_A;
	ocsc_regs.shifts.csc_c12 = mpc20->mpc_shift->MPC_OCSC_C12_A;
	ocsc_regs.masks.csc_c12 = mpc20->mpc_mask->MPC_OCSC_C12_A;

	if (ocsc_mode == MPC_OUTPUT_CSC_COEF_A) {
		ocsc_regs.csc_c11_c12 = REG(CSC_C11_C12_A[opp_id]);
		ocsc_regs.csc_c33_c34 = REG(CSC_C33_C34_A[opp_id]);
	} else {
		ocsc_regs.csc_c11_c12 = REG(CSC_C11_C12_B[opp_id]);
		ocsc_regs.csc_c33_c34 = REG(CSC_C33_C34_B[opp_id]);
	}
	cm_helper_program_color_matrices(
			mpc20->base.ctx,
			regval,
			&ocsc_regs);
}

void mpc2_set_ocsc_default(
		struct mpc *mpc,
		int opp_id,
		enum dc_color_space color_space,
		enum mpc_output_csc_mode ocsc_mode)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);
	uint32_t arr_size;
	struct color_matrices_reg ocsc_regs;
	const uint16_t *regval = NULL;

	REG_SET(CSC_MODE[opp_id], 0, MPC_OCSC_MODE, ocsc_mode);
	if (ocsc_mode == MPC_OUTPUT_CSC_DISABLE)
		return;

	regval = find_color_matrix(color_space, &arr_size);

	if (regval == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	ocsc_regs.shifts.csc_c11 = mpc20->mpc_shift->MPC_OCSC_C11_A;
	ocsc_regs.masks.csc_c11  = mpc20->mpc_mask->MPC_OCSC_C11_A;
	ocsc_regs.shifts.csc_c12 = mpc20->mpc_shift->MPC_OCSC_C12_A;
	ocsc_regs.masks.csc_c12 = mpc20->mpc_mask->MPC_OCSC_C12_A;


	if (ocsc_mode == MPC_OUTPUT_CSC_COEF_A) {
		ocsc_regs.csc_c11_c12 = REG(CSC_C11_C12_A[opp_id]);
		ocsc_regs.csc_c33_c34 = REG(CSC_C33_C34_A[opp_id]);
	} else {
		ocsc_regs.csc_c11_c12 = REG(CSC_C11_C12_B[opp_id]);
		ocsc_regs.csc_c33_c34 = REG(CSC_C33_C34_B[opp_id]);
	}

	cm_helper_program_color_matrices(
			mpc20->base.ctx,
			regval,
			&ocsc_regs);
}

static void mpc2_ogam_get_reg_field(
		struct mpc *mpc,
		struct xfer_func_reg *reg)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);

	reg->shifts.exp_region0_lut_offset = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->masks.exp_region0_lut_offset = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->shifts.exp_region0_num_segments = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->masks.exp_region0_num_segments = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->shifts.exp_region1_lut_offset = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->masks.exp_region1_lut_offset = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->shifts.exp_region1_num_segments = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;
	reg->masks.exp_region1_num_segments = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;
	reg->shifts.field_region_end = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_END_B;
	reg->masks.field_region_end = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_END_B;
	reg->shifts.field_region_end_slope = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->masks.field_region_end_slope = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->shifts.field_region_end_base = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->masks.field_region_end_base = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->shifts.field_region_linear_slope = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_LINEAR_SLOPE_B;
	reg->masks.field_region_linear_slope = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_LINEAR_SLOPE_B;
	reg->shifts.exp_region_start = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_START_B;
	reg->masks.exp_region_start = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_START_B;
	reg->shifts.exp_resion_start_segment = mpc20->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_B;
	reg->masks.exp_resion_start_segment = mpc20->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_B;
}

static void mpc20_power_on_ogam_lut(
		struct mpc *mpc, int mpcc_id,
		bool power_on)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);

	REG_SET(MPCC_MEM_PWR_CTRL[mpcc_id], 0,
			MPCC_OGAM_MEM_PWR_FORCE, power_on == true ? 0:1);

}

static void mpc20_configure_ogam_lut(
		struct mpc *mpc, int mpcc_id,
		bool is_ram_a)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);

	REG_UPDATE_2(MPCC_OGAM_LUT_RAM_CONTROL[mpcc_id],
			MPCC_OGAM_LUT_WRITE_EN_MASK, 7,
			MPCC_OGAM_LUT_RAM_SEL, is_ram_a == true ? 0:1);

	REG_SET(MPCC_OGAM_LUT_INDEX[mpcc_id], 0, MPCC_OGAM_LUT_INDEX, 0);
}

static enum dc_lut_mode mpc20_get_ogam_current(struct mpc *mpc, int mpcc_id)
{
	enum dc_lut_mode mode;
	uint32_t state_mode;
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);

	REG_GET(MPCC_OGAM_LUT_RAM_CONTROL[mpcc_id],
			MPCC_OGAM_CONFIG_STATUS, &state_mode);

		switch (state_mode) {
		case 0:
			mode = LUT_BYPASS;
			break;
		case 1:
			mode = LUT_RAM_A;
			break;
		case 2:
			mode = LUT_RAM_B;
			break;
		default:
			mode = LUT_BYPASS;
			break;
		}
		return mode;
}

static void mpc2_program_lutb(struct mpc *mpc, int mpcc_id,
			const struct pwl_params *params)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);
	struct xfer_func_reg gam_regs;

	mpc2_ogam_get_reg_field(mpc, &gam_regs);

	gam_regs.start_cntl_b = REG(MPCC_OGAM_RAMB_START_CNTL_B[mpcc_id]);
	gam_regs.start_cntl_g = REG(MPCC_OGAM_RAMB_START_CNTL_G[mpcc_id]);
	gam_regs.start_cntl_r = REG(MPCC_OGAM_RAMB_START_CNTL_R[mpcc_id]);
	gam_regs.start_slope_cntl_b = REG(MPCC_OGAM_RAMB_SLOPE_CNTL_B[mpcc_id]);
	gam_regs.start_slope_cntl_g = REG(MPCC_OGAM_RAMB_SLOPE_CNTL_G[mpcc_id]);
	gam_regs.start_slope_cntl_r = REG(MPCC_OGAM_RAMB_SLOPE_CNTL_R[mpcc_id]);
	gam_regs.start_end_cntl1_b = REG(MPCC_OGAM_RAMB_END_CNTL1_B[mpcc_id]);
	gam_regs.start_end_cntl2_b = REG(MPCC_OGAM_RAMB_END_CNTL2_B[mpcc_id]);
	gam_regs.start_end_cntl1_g = REG(MPCC_OGAM_RAMB_END_CNTL1_G[mpcc_id]);
	gam_regs.start_end_cntl2_g = REG(MPCC_OGAM_RAMB_END_CNTL2_G[mpcc_id]);
	gam_regs.start_end_cntl1_r = REG(MPCC_OGAM_RAMB_END_CNTL1_R[mpcc_id]);
	gam_regs.start_end_cntl2_r = REG(MPCC_OGAM_RAMB_END_CNTL2_R[mpcc_id]);
	gam_regs.region_start = REG(MPCC_OGAM_RAMB_REGION_0_1[mpcc_id]);
	gam_regs.region_end = REG(MPCC_OGAM_RAMB_REGION_32_33[mpcc_id]);

	cm_helper_program_xfer_func(mpc20->base.ctx, params, &gam_regs);

}

static void mpc2_program_luta(struct mpc *mpc, int mpcc_id,
		const struct pwl_params *params)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);
	struct xfer_func_reg gam_regs;

	mpc2_ogam_get_reg_field(mpc, &gam_regs);

	gam_regs.start_cntl_b = REG(MPCC_OGAM_RAMA_START_CNTL_B[mpcc_id]);
	gam_regs.start_cntl_g = REG(MPCC_OGAM_RAMA_START_CNTL_G[mpcc_id]);
	gam_regs.start_cntl_r = REG(MPCC_OGAM_RAMA_START_CNTL_R[mpcc_id]);
	gam_regs.start_slope_cntl_b = REG(MPCC_OGAM_RAMA_SLOPE_CNTL_B[mpcc_id]);
	gam_regs.start_slope_cntl_g = REG(MPCC_OGAM_RAMA_SLOPE_CNTL_G[mpcc_id]);
	gam_regs.start_slope_cntl_r = REG(MPCC_OGAM_RAMA_SLOPE_CNTL_R[mpcc_id]);
	gam_regs.start_end_cntl1_b = REG(MPCC_OGAM_RAMA_END_CNTL1_B[mpcc_id]);
	gam_regs.start_end_cntl2_b = REG(MPCC_OGAM_RAMA_END_CNTL2_B[mpcc_id]);
	gam_regs.start_end_cntl1_g = REG(MPCC_OGAM_RAMA_END_CNTL1_G[mpcc_id]);
	gam_regs.start_end_cntl2_g = REG(MPCC_OGAM_RAMA_END_CNTL2_G[mpcc_id]);
	gam_regs.start_end_cntl1_r = REG(MPCC_OGAM_RAMA_END_CNTL1_R[mpcc_id]);
	gam_regs.start_end_cntl2_r = REG(MPCC_OGAM_RAMA_END_CNTL2_R[mpcc_id]);
	gam_regs.region_start = REG(MPCC_OGAM_RAMA_REGION_0_1[mpcc_id]);
	gam_regs.region_end = REG(MPCC_OGAM_RAMA_REGION_32_33[mpcc_id]);

	cm_helper_program_xfer_func(mpc20->base.ctx, params, &gam_regs);

}

static void mpc20_program_ogam_pwl(
		struct mpc *mpc, int mpcc_id,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i;
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);

	for (i = 0 ; i < num; i++) {
		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, rgb[i].red_reg);
		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, rgb[i].green_reg);
		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, rgb[i].blue_reg);

		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0,
				MPCC_OGAM_LUT_DATA, rgb[i].delta_red_reg);
		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0,
				MPCC_OGAM_LUT_DATA, rgb[i].delta_green_reg);
		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0,
				MPCC_OGAM_LUT_DATA, rgb[i].delta_blue_reg);

	}

}

void apply_DEDCN20_305_wa(
		struct mpc *mpc,
		int mpcc_id, enum dc_lut_mode current_mode,
		enum dc_lut_mode next_mode)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);

	if (mpc->ctx->dc->debug.cm_in_bypass) {
		REG_SET(MPCC_OGAM_MODE[mpcc_id], 0, MPCC_OGAM_MODE, 0);
		return;
	}

	if (mpc->ctx->dc->work_arounds.dedcn20_305_wa == false) {
		/*hw fixed in new review*/
		return;
	}
	if (current_mode == LUT_BYPASS)
		/*this will only work if OTG is locked.
		 *if we were to support OTG unlock case,
		 *the workaround will be more complex
		 */
		REG_SET(MPCC_OGAM_MODE[mpcc_id], 0, MPCC_OGAM_MODE,
			next_mode == LUT_RAM_A ? 1:2);
}

void mpc2_set_output_gamma(
		struct mpc *mpc,
		int mpcc_id,
		const struct pwl_params *params)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);

	if (mpc->ctx->dc->debug.cm_in_bypass) {
		REG_SET(MPCC_OGAM_MODE[mpcc_id], 0, MPCC_OGAM_MODE, 0);
		return;
	}

	if (params == NULL) {
		REG_SET(MPCC_OGAM_MODE[mpcc_id], 0, MPCC_OGAM_MODE, 0);
		return;
	}

	current_mode = mpc20_get_ogam_current(mpc, mpcc_id);
	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	mpc20_power_on_ogam_lut(mpc, mpcc_id, true);
	mpc20_configure_ogam_lut(mpc, mpcc_id, next_mode == LUT_RAM_A ? true:false);

	if (next_mode == LUT_RAM_A)
		mpc2_program_luta(mpc, mpcc_id, params);
	else
		mpc2_program_lutb(mpc, mpcc_id, params);

	apply_DEDCN20_305_wa(mpc, mpcc_id, current_mode, next_mode);

	mpc20_program_ogam_pwl(
			mpc, mpcc_id, params->rgb_resulted, params->hw_points_num);

	REG_SET(MPCC_OGAM_MODE[mpcc_id], 0, MPCC_OGAM_MODE,
		next_mode == LUT_RAM_A ? 1:2);
}
void mpc2_assert_idle_mpcc(struct mpc *mpc, int id)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);
	unsigned int mpc_disabled;

	ASSERT(!(mpc20->mpcc_in_use_mask & 1 << id));
	REG_GET(MPCC_STATUS[id], MPCC_DISABLED, &mpc_disabled);
	if (mpc_disabled)
		return;

	REG_WAIT(MPCC_STATUS[id],
			MPCC_IDLE, 1,
			1, 100000);
}

void mpc2_assert_mpcc_idle_before_connect(struct mpc *mpc, int mpcc_id)
{
	struct dcn20_mpc *mpc20 = TO_DCN20_MPC(mpc);
	unsigned int top_sel, mpc_busy, mpc_idle, mpc_disabled;

	REG_GET(MPCC_TOP_SEL[mpcc_id],
			MPCC_TOP_SEL, &top_sel);

	REG_GET_3(MPCC_STATUS[mpcc_id],
			MPCC_BUSY, &mpc_busy,
			MPCC_IDLE, &mpc_idle,
			MPCC_DISABLED, &mpc_disabled);

	if (top_sel == 0xf) {
		ASSERT(!mpc_busy);
		ASSERT(mpc_idle);
		ASSERT(mpc_disabled);
	} else {
		ASSERT(!mpc_disabled);
		ASSERT(!mpc_idle);
	}
}

static void mpc2_init_mpcc(struct mpcc *mpcc, int mpcc_inst)
{
	mpcc->mpcc_id = mpcc_inst;
	mpcc->dpp_id = 0xf;
	mpcc->mpcc_bot = NULL;
	mpcc->blnd_cfg.overlap_only = false;
	mpcc->blnd_cfg.global_alpha = 0xff;
	mpcc->blnd_cfg.global_gain = 0xff;
	mpcc->blnd_cfg.background_color_bpc = 4;
	mpcc->blnd_cfg.bottom_gain_mode = 0;
	mpcc->blnd_cfg.top_gain = 0x1f000;
	mpcc->blnd_cfg.bottom_inside_gain = 0x1f000;
	mpcc->blnd_cfg.bottom_outside_gain = 0x1f000;
	mpcc->sm_cfg.enable = false;
}

struct mpcc *mpc2_get_mpcc_for_dpp(struct mpc_tree *tree, int dpp_id)
{
	struct mpcc *tmp_mpcc = tree->opp_list;

	while (tmp_mpcc != NULL) {
		if (tmp_mpcc->dpp_id == 0xf || tmp_mpcc->dpp_id == dpp_id)
			return tmp_mpcc;
		tmp_mpcc = tmp_mpcc->mpcc_bot;
	}
	return NULL;
}

const struct mpc_funcs dcn20_mpc_funcs = {
	.read_mpcc_state = mpc1_read_mpcc_state,
	.insert_plane = mpc1_insert_plane,
	.remove_mpcc = mpc1_remove_mpcc,
	.mpc_init = mpc1_mpc_init,
	.update_blending = mpc2_update_blending,
	.get_mpcc_for_dpp = mpc2_get_mpcc_for_dpp,
	.wait_for_idle = mpc2_assert_idle_mpcc,
	.assert_mpcc_idle_before_connect = mpc2_assert_mpcc_idle_before_connect,
	.init_mpcc_list_from_hw = mpc1_init_mpcc_list_from_hw,
	.set_denorm = mpc2_set_denorm,
	.set_denorm_clamp = mpc2_set_denorm_clamp,
	.set_output_csc = mpc2_set_output_csc,
	.set_ocsc_default = mpc2_set_ocsc_default,
	.set_output_gamma = mpc2_set_output_gamma,
};

void dcn20_mpc_construct(struct dcn20_mpc *mpc20,
	struct dc_context *ctx,
	const struct dcn20_mpc_registers *mpc_regs,
	const struct dcn20_mpc_shift *mpc_shift,
	const struct dcn20_mpc_mask *mpc_mask,
	int num_mpcc)
{
	int i;

	mpc20->base.ctx = ctx;

	mpc20->base.funcs = &dcn20_mpc_funcs;

	mpc20->mpc_regs = mpc_regs;
	mpc20->mpc_shift = mpc_shift;
	mpc20->mpc_mask = mpc_mask;

	mpc20->mpcc_in_use_mask = 0;
	mpc20->num_mpcc = num_mpcc;

	for (i = 0; i < MAX_MPCC; i++)
		mpc2_init_mpcc(&mpc20->base.mpcc_array[i], i);
}

