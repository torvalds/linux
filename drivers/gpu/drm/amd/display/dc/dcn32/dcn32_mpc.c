/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "reg_helper.h"
#include "dcn30/dcn30_mpc.h"
#include "dcn30/dcn30_cm_common.h"
#include "dcn32_mpc.h"
#include "basics/conversion.h"
#include "dcn10/dcn10_cm_common.h"
#include "dc.h"

#define REG(reg)\
	mpc30->mpc_regs->reg

#define CTX \
	mpc30->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mpc30->mpc_shift->field_name, mpc30->mpc_mask->field_name


void mpc32_mpc_init(struct mpc *mpc)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	int mpcc_id;

	mpc1_mpc_init(mpc);

	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc) {
		if (mpc30->mpc_mask->MPCC_MCM_SHAPER_MEM_LOW_PWR_MODE && mpc30->mpc_mask->MPCC_MCM_3DLUT_MEM_LOW_PWR_MODE) {
			for (mpcc_id = 0; mpcc_id < mpc30->num_mpcc; mpcc_id++) {
				REG_UPDATE(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], MPCC_MCM_SHAPER_MEM_LOW_PWR_MODE, 3);
				REG_UPDATE(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], MPCC_MCM_3DLUT_MEM_LOW_PWR_MODE, 3);
				REG_UPDATE(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], MPCC_MCM_1DLUT_MEM_LOW_PWR_MODE, 3);
			}
		}
		if (mpc30->mpc_mask->MPCC_OGAM_MEM_LOW_PWR_MODE) {
			for (mpcc_id = 0; mpcc_id < mpc30->num_mpcc; mpcc_id++)
				REG_UPDATE(MPCC_MEM_PWR_CTRL[mpcc_id], MPCC_OGAM_MEM_LOW_PWR_MODE, 3);
		}
	}
}

static void mpc32_power_on_blnd_lut(
	struct mpc *mpc,
	uint32_t mpcc_id,
	bool power_on)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.cm) {
		if (power_on) {
			REG_UPDATE(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], MPCC_MCM_1DLUT_MEM_PWR_FORCE, 0);
			REG_WAIT(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], MPCC_MCM_1DLUT_MEM_PWR_STATE, 0, 1, 5);
		} else {
			ASSERT(false);
			/* TODO: change to mpc
			 *  dpp_base->ctx->dc->optimized_required = true;
			 *  dpp_base->deferred_reg_writes.bits.disable_blnd_lut = true;
			 */
		}
	} else {
		REG_SET(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], 0,
				MPCC_MCM_1DLUT_MEM_PWR_FORCE, power_on == true ? 0 : 1);
	}
}

static enum dc_lut_mode mpc32_get_post1dlut_current(struct mpc *mpc, uint32_t mpcc_id)
{
	enum dc_lut_mode mode;
	uint32_t mode_current = 0;
	uint32_t in_use = 0;

	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_GET(MPCC_MCM_1DLUT_CONTROL[mpcc_id],
			MPCC_MCM_1DLUT_MODE_CURRENT, &mode_current);
	REG_GET(MPCC_MCM_1DLUT_CONTROL[mpcc_id],
			MPCC_MCM_1DLUT_SELECT_CURRENT, &in_use);

	switch (mode_current) {
	case 0:
	case 1:
		mode = LUT_BYPASS;
		break;

	case 2:
		if (in_use == 0)
			mode = LUT_RAM_A;
		else
			mode = LUT_RAM_B;
		break;
	default:
		mode = LUT_BYPASS;
		break;
	}
	return mode;
}

static void mpc32_configure_post1dlut(
		struct mpc *mpc,
		uint32_t mpcc_id,
		bool is_ram_a)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	//TODO: this
	REG_UPDATE_2(MPCC_MCM_1DLUT_LUT_CONTROL[mpcc_id],
			MPCC_MCM_1DLUT_LUT_WRITE_COLOR_MASK, 7,
			MPCC_MCM_1DLUT_LUT_HOST_SEL, is_ram_a == true ? 0 : 1);

	REG_SET(MPCC_MCM_1DLUT_LUT_INDEX[mpcc_id], 0, MPCC_MCM_1DLUT_LUT_INDEX, 0);
}

static void mpc32_post1dlut_get_reg_field(
		struct dcn30_mpc *mpc,
		struct dcn3_xfer_func_reg *reg)
{
	reg->shifts.exp_region0_lut_offset = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->masks.exp_region0_lut_offset = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->shifts.exp_region0_num_segments = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->masks.exp_region0_num_segments = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->shifts.exp_region1_lut_offset = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->masks.exp_region1_lut_offset = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->shifts.exp_region1_num_segments = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION1_NUM_SEGMENTS;
	reg->masks.exp_region1_num_segments = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION1_NUM_SEGMENTS;

	reg->shifts.field_region_end = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION_END_B;
	reg->masks.field_region_end = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION_END_B;
	reg->shifts.field_region_end_slope = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION_END_SLOPE_B;
	reg->masks.field_region_end_slope = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION_END_SLOPE_B;
	reg->shifts.field_region_end_base = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION_END_BASE_B;
	reg->masks.field_region_end_base = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION_END_BASE_B;
	reg->shifts.field_region_linear_slope = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SLOPE_B;
	reg->masks.field_region_linear_slope = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SLOPE_B;
	reg->shifts.exp_region_start = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION_START_B;
	reg->masks.exp_region_start = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION_START_B;
	reg->shifts.exp_resion_start_segment = mpc->mpc_shift->MPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SEGMENT_B;
	reg->masks.exp_resion_start_segment = mpc->mpc_mask->MPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SEGMENT_B;
}

/*program blnd lut RAM A*/
static void mpc32_program_post1dluta_settings(
		struct mpc *mpc,
		uint32_t mpcc_id,
		const struct pwl_params *params)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	struct dcn3_xfer_func_reg gam_regs;

	mpc32_post1dlut_get_reg_field(mpc30, &gam_regs);

	gam_regs.start_cntl_b = REG(MPCC_MCM_1DLUT_RAMA_START_CNTL_B[mpcc_id]);
	gam_regs.start_cntl_g = REG(MPCC_MCM_1DLUT_RAMA_START_CNTL_G[mpcc_id]);
	gam_regs.start_cntl_r = REG(MPCC_MCM_1DLUT_RAMA_START_CNTL_R[mpcc_id]);
	gam_regs.start_slope_cntl_b = REG(MPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_B[mpcc_id]);
	gam_regs.start_slope_cntl_g = REG(MPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_G[mpcc_id]);
	gam_regs.start_slope_cntl_r = REG(MPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_R[mpcc_id]);
	gam_regs.start_end_cntl1_b = REG(MPCC_MCM_1DLUT_RAMA_END_CNTL1_B[mpcc_id]);
	gam_regs.start_end_cntl2_b = REG(MPCC_MCM_1DLUT_RAMA_END_CNTL2_B[mpcc_id]);
	gam_regs.start_end_cntl1_g = REG(MPCC_MCM_1DLUT_RAMA_END_CNTL1_G[mpcc_id]);
	gam_regs.start_end_cntl2_g = REG(MPCC_MCM_1DLUT_RAMA_END_CNTL2_G[mpcc_id]);
	gam_regs.start_end_cntl1_r = REG(MPCC_MCM_1DLUT_RAMA_END_CNTL1_R[mpcc_id]);
	gam_regs.start_end_cntl2_r = REG(MPCC_MCM_1DLUT_RAMA_END_CNTL2_R[mpcc_id]);
	gam_regs.region_start = REG(MPCC_MCM_1DLUT_RAMA_REGION_0_1[mpcc_id]);
	gam_regs.region_end = REG(MPCC_MCM_1DLUT_RAMA_REGION_32_33[mpcc_id]);

	cm_helper_program_gamcor_xfer_func(mpc->ctx, params, &gam_regs);
}

/*program blnd lut RAM B*/
static void mpc32_program_post1dlutb_settings(
		struct mpc *mpc,
		uint32_t mpcc_id,
		const struct pwl_params *params)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	struct dcn3_xfer_func_reg gam_regs;

	mpc32_post1dlut_get_reg_field(mpc30, &gam_regs);

	gam_regs.start_cntl_b = REG(MPCC_MCM_1DLUT_RAMB_START_CNTL_B[mpcc_id]);
	gam_regs.start_cntl_g = REG(MPCC_MCM_1DLUT_RAMB_START_CNTL_G[mpcc_id]);
	gam_regs.start_cntl_r = REG(MPCC_MCM_1DLUT_RAMB_START_CNTL_R[mpcc_id]);
	gam_regs.start_slope_cntl_b = REG(MPCC_MCM_1DLUT_RAMB_START_SLOPE_CNTL_B[mpcc_id]);
	gam_regs.start_slope_cntl_g = REG(MPCC_MCM_1DLUT_RAMB_START_SLOPE_CNTL_G[mpcc_id]);
	gam_regs.start_slope_cntl_r = REG(MPCC_MCM_1DLUT_RAMB_START_SLOPE_CNTL_R[mpcc_id]);
	gam_regs.start_end_cntl1_b = REG(MPCC_MCM_1DLUT_RAMB_END_CNTL1_B[mpcc_id]);
	gam_regs.start_end_cntl2_b = REG(MPCC_MCM_1DLUT_RAMB_END_CNTL2_B[mpcc_id]);
	gam_regs.start_end_cntl1_g = REG(MPCC_MCM_1DLUT_RAMB_END_CNTL1_G[mpcc_id]);
	gam_regs.start_end_cntl2_g = REG(MPCC_MCM_1DLUT_RAMB_END_CNTL2_G[mpcc_id]);
	gam_regs.start_end_cntl1_r = REG(MPCC_MCM_1DLUT_RAMB_END_CNTL1_R[mpcc_id]);
	gam_regs.start_end_cntl2_r = REG(MPCC_MCM_1DLUT_RAMB_END_CNTL2_R[mpcc_id]);
	gam_regs.region_start = REG(MPCC_MCM_1DLUT_RAMB_REGION_0_1[mpcc_id]);
	gam_regs.region_end = REG(MPCC_MCM_1DLUT_RAMB_REGION_32_33[mpcc_id]);

	cm_helper_program_gamcor_xfer_func(mpc->ctx, params, &gam_regs);
}

static void mpc32_program_post1dlut_pwl(
		struct mpc *mpc,
		uint32_t mpcc_id,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	uint32_t last_base_value_red = rgb[num-1].red_reg + rgb[num-1].delta_red_reg;
	uint32_t last_base_value_green = rgb[num-1].green_reg + rgb[num-1].delta_green_reg;
	uint32_t last_base_value_blue = rgb[num-1].blue_reg + rgb[num-1].delta_blue_reg;

	if (is_rgb_equal(rgb, num)) {
		for (i = 0 ; i < num; i++)
			REG_SET(MPCC_MCM_1DLUT_LUT_DATA[mpcc_id], 0, MPCC_MCM_1DLUT_LUT_DATA, rgb[i].red_reg);
		REG_SET(MPCC_MCM_1DLUT_LUT_DATA[mpcc_id], 0, MPCC_MCM_1DLUT_LUT_DATA, last_base_value_red);
	} else {
		REG_UPDATE(MPCC_MCM_1DLUT_LUT_CONTROL[mpcc_id], MPCC_MCM_1DLUT_LUT_WRITE_COLOR_MASK, 4);
		for (i = 0 ; i < num; i++)
			REG_SET(MPCC_MCM_1DLUT_LUT_DATA[mpcc_id], 0, MPCC_MCM_1DLUT_LUT_DATA, rgb[i].red_reg);
		REG_SET(MPCC_MCM_1DLUT_LUT_DATA[mpcc_id], 0, MPCC_MCM_1DLUT_LUT_DATA, last_base_value_red);

		REG_UPDATE(MPCC_MCM_1DLUT_LUT_CONTROL[mpcc_id], MPCC_MCM_1DLUT_LUT_WRITE_COLOR_MASK, 2);
		for (i = 0 ; i < num; i++)
			REG_SET(MPCC_MCM_1DLUT_LUT_DATA[mpcc_id], 0, MPCC_MCM_1DLUT_LUT_DATA, rgb[i].green_reg);
		REG_SET(MPCC_MCM_1DLUT_LUT_DATA[mpcc_id], 0, MPCC_MCM_1DLUT_LUT_DATA, last_base_value_green);

		REG_UPDATE(MPCC_MCM_1DLUT_LUT_CONTROL[mpcc_id], MPCC_MCM_1DLUT_LUT_WRITE_COLOR_MASK, 1);
		for (i = 0 ; i < num; i++)
			REG_SET(MPCC_MCM_1DLUT_LUT_DATA[mpcc_id], 0, MPCC_MCM_1DLUT_LUT_DATA, rgb[i].blue_reg);
		REG_SET(MPCC_MCM_1DLUT_LUT_DATA[mpcc_id], 0, MPCC_MCM_1DLUT_LUT_DATA, last_base_value_blue);
	}
}

bool mpc32_program_post1dlut(
		struct mpc *mpc,
		const struct pwl_params *params,
		uint32_t mpcc_id)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	if (params == NULL) {
		REG_SET(MPCC_MCM_1DLUT_CONTROL[mpcc_id], 0, MPCC_MCM_1DLUT_MODE, 0);
		if (mpc->ctx->dc->debug.enable_mem_low_power.bits.cm)
			mpc32_power_on_blnd_lut(mpc, mpcc_id, false);
		return false;
	}

	current_mode = mpc32_get_post1dlut_current(mpc, mpcc_id);
	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_B)
		next_mode = LUT_RAM_A;
	else
		next_mode = LUT_RAM_B;

	mpc32_power_on_blnd_lut(mpc, mpcc_id, true);
	mpc32_configure_post1dlut(mpc, mpcc_id, next_mode == LUT_RAM_A);

	if (next_mode == LUT_RAM_A)
		mpc32_program_post1dluta_settings(mpc, mpcc_id, params);
	else
		mpc32_program_post1dlutb_settings(mpc, mpcc_id, params);

	mpc32_program_post1dlut_pwl(
			mpc, mpcc_id, params->rgb_resulted, params->hw_points_num);

	REG_UPDATE_2(MPCC_MCM_1DLUT_CONTROL[mpcc_id],
			MPCC_MCM_1DLUT_MODE, 2,
			MPCC_MCM_1DLUT_SELECT, next_mode == LUT_RAM_A ? 0 : 1);

	return true;
}

static enum dc_lut_mode mpc32_get_shaper_current(struct mpc *mpc, uint32_t mpcc_id)
{
	enum dc_lut_mode mode;
	uint32_t state_mode;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_GET(MPCC_MCM_SHAPER_CONTROL[mpcc_id], MPCC_MCM_SHAPER_MODE_CURRENT, &state_mode);

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


static void mpc32_configure_shaper_lut(
		struct mpc *mpc,
		bool is_ram_a,
		uint32_t mpcc_id)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_UPDATE(MPCC_MCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPCC_MCM_SHAPER_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(MPCC_MCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPCC_MCM_SHAPER_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(MPCC_MCM_SHAPER_LUT_INDEX[mpcc_id], 0, MPCC_MCM_SHAPER_LUT_INDEX, 0);
}


static void mpc32_program_shaper_luta_settings(
		struct mpc *mpc,
		const struct pwl_params *params,
		uint32_t mpcc_id)
{
	const struct gamma_curve *curve;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_SET_2(MPCC_MCM_SHAPER_RAMA_START_CNTL_B[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPCC_MCM_SHAPER_RAMA_START_CNTL_G[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].green.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPCC_MCM_SHAPER_RAMA_START_CNTL_R[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].red.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(MPCC_MCM_SHAPER_RAMA_END_CNTL_B[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);
	REG_SET_2(MPCC_MCM_SHAPER_RAMA_END_CNTL_G[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].green.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].green.custom_float_y);
	REG_SET_2(MPCC_MCM_SHAPER_RAMA_END_CNTL_R[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].red.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_0_1[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_2_3[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_4_5[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_6_7[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_8_9[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_10_11[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_12_13[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_14_15[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_16_17[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_18_19[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_20_21[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_22_23[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_24_25[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_26_27[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_28_29[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_30_31[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMA_REGION_32_33[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
}


static void mpc32_program_shaper_lutb_settings(
		struct mpc *mpc,
		const struct pwl_params *params,
		uint32_t mpcc_id)
{
	const struct gamma_curve *curve;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_SET_2(MPCC_MCM_SHAPER_RAMB_START_CNTL_B[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPCC_MCM_SHAPER_RAMB_START_CNTL_G[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].green.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPCC_MCM_SHAPER_RAMB_START_CNTL_R[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].red.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(MPCC_MCM_SHAPER_RAMB_END_CNTL_B[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);
	REG_SET_2(MPCC_MCM_SHAPER_RAMB_END_CNTL_G[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].green.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].green.custom_float_y);
	REG_SET_2(MPCC_MCM_SHAPER_RAMB_END_CNTL_R[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].red.custom_float_x,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_0_1[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_2_3[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_4_5[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_6_7[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_8_9[mpcc_id], 0,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_10_11[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_12_13[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_14_15[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_16_17[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_18_19[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_20_21[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_22_23[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_24_25[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_26_27[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_28_29[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_30_31[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(MPCC_MCM_SHAPER_RAMB_REGION_32_33[mpcc_id], 0,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
}


static void mpc32_program_shaper_lut(
		struct mpc *mpc,
		const struct pwl_result_data *rgb,
		uint32_t num,
		uint32_t mpcc_id)
{
	uint32_t i, red, green, blue;
	uint32_t  red_delta, green_delta, blue_delta;
	uint32_t  red_value, green_value, blue_value;

	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	for (i = 0 ; i < num; i++) {

		red   = rgb[i].red_reg;
		green = rgb[i].green_reg;
		blue  = rgb[i].blue_reg;

		red_delta   = rgb[i].delta_red_reg;
		green_delta = rgb[i].delta_green_reg;
		blue_delta  = rgb[i].delta_blue_reg;

		red_value   = ((red_delta   & 0x3ff) << 14) | (red   & 0x3fff);
		green_value = ((green_delta & 0x3ff) << 14) | (green & 0x3fff);
		blue_value  = ((blue_delta  & 0x3ff) << 14) | (blue  & 0x3fff);

		REG_SET(MPCC_MCM_SHAPER_LUT_DATA[mpcc_id], 0, MPCC_MCM_SHAPER_LUT_DATA, red_value);
		REG_SET(MPCC_MCM_SHAPER_LUT_DATA[mpcc_id], 0, MPCC_MCM_SHAPER_LUT_DATA, green_value);
		REG_SET(MPCC_MCM_SHAPER_LUT_DATA[mpcc_id], 0, MPCC_MCM_SHAPER_LUT_DATA, blue_value);
	}

}


static void mpc32_power_on_shaper_3dlut(
		struct mpc *mpc,
		uint32_t mpcc_id,
		bool power_on)
{
	uint32_t power_status_shaper = 2;
	uint32_t power_status_3dlut  = 2;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	int max_retries = 10;

	REG_SET(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], 0,
	MPCC_MCM_3DLUT_MEM_PWR_DIS, power_on == true ? 1:0);
	/* wait for memory to fully power up */
	if (power_on && mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc) {
		REG_WAIT(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], MPCC_MCM_SHAPER_MEM_PWR_STATE, 0, 1, max_retries);
		REG_WAIT(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], MPCC_MCM_3DLUT_MEM_PWR_STATE, 0, 1, max_retries);
	}

	/*read status is not mandatory, it is just for debugging*/
	REG_GET(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], MPCC_MCM_SHAPER_MEM_PWR_STATE, &power_status_shaper);
	REG_GET(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], MPCC_MCM_3DLUT_MEM_PWR_STATE, &power_status_3dlut);

	if (power_status_shaper != 0 && power_on == true)
		BREAK_TO_DEBUGGER();

	if (power_status_3dlut != 0 && power_on == true)
		BREAK_TO_DEBUGGER();
}


bool mpc32_program_shaper(
		struct mpc *mpc,
		const struct pwl_params *params,
		uint32_t mpcc_id)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;

	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	if (params == NULL) {
		REG_SET(MPCC_MCM_SHAPER_CONTROL[mpcc_id], 0, MPCC_MCM_SHAPER_LUT_MODE, 0);
		return false;
	}

	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
		mpc32_power_on_shaper_3dlut(mpc, mpcc_id, true);

	current_mode = mpc32_get_shaper_current(mpc, mpcc_id);

	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	mpc32_configure_shaper_lut(mpc, next_mode == LUT_RAM_A, mpcc_id);

	if (next_mode == LUT_RAM_A)
		mpc32_program_shaper_luta_settings(mpc, params, mpcc_id);
	else
		mpc32_program_shaper_lutb_settings(mpc, params, mpcc_id);

	mpc32_program_shaper_lut(
			mpc, params->rgb_resulted, params->hw_points_num, mpcc_id);

	REG_SET(MPCC_MCM_SHAPER_CONTROL[mpcc_id], 0, MPCC_MCM_SHAPER_LUT_MODE, next_mode == LUT_RAM_A ? 1:2);
	mpc32_power_on_shaper_3dlut(mpc, mpcc_id, false);

	return true;
}


static enum dc_lut_mode get3dlut_config(
			struct mpc *mpc,
			bool *is_17x17x17,
			bool *is_12bits_color_channel,
			int mpcc_id)
{
	uint32_t i_mode, i_enable_10bits, lut_size;
	enum dc_lut_mode mode;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_GET(MPCC_MCM_3DLUT_MODE[mpcc_id],
			MPCC_MCM_3DLUT_MODE_CURRENT,  &i_mode);

	REG_GET(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
			MPCC_MCM_3DLUT_30BIT_EN, &i_enable_10bits);

	switch (i_mode) {
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
	if (i_enable_10bits > 0)
		*is_12bits_color_channel = false;
	else
		*is_12bits_color_channel = true;

	REG_GET(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_SIZE, &lut_size);

	if (lut_size == 0)
		*is_17x17x17 = true;
	else
		*is_17x17x17 = false;

	return mode;
}


static void mpc32_select_3dlut_ram(
		struct mpc *mpc,
		enum dc_lut_mode mode,
		bool is_color_channel_12bits,
		uint32_t mpcc_id)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_UPDATE_2(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
		MPCC_MCM_3DLUT_RAM_SEL, mode == LUT_RAM_A ? 0 : 1,
		MPCC_MCM_3DLUT_30BIT_EN, is_color_channel_12bits == true ? 0:1);
}


static void mpc32_select_3dlut_ram_mask(
		struct mpc *mpc,
		uint32_t ram_selection_mask,
		uint32_t mpcc_id)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_UPDATE(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id], MPCC_MCM_3DLUT_WRITE_EN_MASK,
			ram_selection_mask);
	REG_SET(MPCC_MCM_3DLUT_INDEX[mpcc_id], 0, MPCC_MCM_3DLUT_INDEX, 0);
}


static void mpc32_set3dlut_ram12(
		struct mpc *mpc,
		const struct dc_rgb *lut,
		uint32_t entries,
		uint32_t mpcc_id)
{
	uint32_t i, red, green, blue, red1, green1, blue1;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	for (i = 0 ; i < entries; i += 2) {
		red   = lut[i].red<<4;
		green = lut[i].green<<4;
		blue  = lut[i].blue<<4;
		red1   = lut[i+1].red<<4;
		green1 = lut[i+1].green<<4;
		blue1  = lut[i+1].blue<<4;

		REG_SET_2(MPCC_MCM_3DLUT_DATA[mpcc_id], 0,
				MPCC_MCM_3DLUT_DATA0, red,
				MPCC_MCM_3DLUT_DATA1, red1);

		REG_SET_2(MPCC_MCM_3DLUT_DATA[mpcc_id], 0,
				MPCC_MCM_3DLUT_DATA0, green,
				MPCC_MCM_3DLUT_DATA1, green1);

		REG_SET_2(MPCC_MCM_3DLUT_DATA[mpcc_id], 0,
				MPCC_MCM_3DLUT_DATA0, blue,
				MPCC_MCM_3DLUT_DATA1, blue1);
	}
}


static void mpc32_set3dlut_ram10(
		struct mpc *mpc,
		const struct dc_rgb *lut,
		uint32_t entries,
		uint32_t mpcc_id)
{
	uint32_t i, red, green, blue, value;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	for (i = 0; i < entries; i++) {
		red   = lut[i].red;
		green = lut[i].green;
		blue  = lut[i].blue;
		//should we shift red 22bit and green 12?
		value = (red<<20) | (green<<10) | blue;

		REG_SET(MPCC_MCM_3DLUT_DATA_30BIT[mpcc_id], 0, MPCC_MCM_3DLUT_DATA_30BIT, value);
	}

}


static void mpc32_set_3dlut_mode(
		struct mpc *mpc,
		enum dc_lut_mode mode,
		bool is_color_channel_12bits,
		bool is_lut_size17x17x17,
		uint32_t mpcc_id)
{
	uint32_t lut_mode;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	// set default 3DLUT to pre-blend
	// TODO: implement movable CM location
	REG_UPDATE(MPCC_MOVABLE_CM_LOCATION_CONTROL[mpcc_id], MPCC_MOVABLE_CM_LOCATION_CNTL, 0);

	if (mode == LUT_BYPASS)
		lut_mode = 0;
	else if (mode == LUT_RAM_A)
		lut_mode = 1;
	else
		lut_mode = 2;

	REG_UPDATE_2(MPCC_MCM_3DLUT_MODE[mpcc_id],
			MPCC_MCM_3DLUT_MODE, lut_mode,
			MPCC_MCM_3DLUT_SIZE, is_lut_size17x17x17 == true ? 0 : 1);
}


bool mpc32_program_3dlut(
		struct mpc *mpc,
		const struct tetrahedral_params *params,
		int mpcc_id)
{
	enum dc_lut_mode mode;
	bool is_17x17x17;
	bool is_12bits_color_channel;
	const struct dc_rgb *lut0;
	const struct dc_rgb *lut1;
	const struct dc_rgb *lut2;
	const struct dc_rgb *lut3;
	int lut_size0;
	int lut_size;

	if (params == NULL) {
		mpc32_set_3dlut_mode(mpc, LUT_BYPASS, false, false, mpcc_id);
		return false;
	}
	mpc32_power_on_shaper_3dlut(mpc, mpcc_id, true);

	mode = get3dlut_config(mpc, &is_17x17x17, &is_12bits_color_channel, mpcc_id);

	if (mode == LUT_BYPASS || mode == LUT_RAM_B)
		mode = LUT_RAM_A;
	else
		mode = LUT_RAM_B;

	is_17x17x17 = !params->use_tetrahedral_9;
	is_12bits_color_channel = params->use_12bits;
	if (is_17x17x17) {
		lut0 = params->tetrahedral_17.lut0;
		lut1 = params->tetrahedral_17.lut1;
		lut2 = params->tetrahedral_17.lut2;
		lut3 = params->tetrahedral_17.lut3;
		lut_size0 = sizeof(params->tetrahedral_17.lut0)/
					sizeof(params->tetrahedral_17.lut0[0]);
		lut_size  = sizeof(params->tetrahedral_17.lut1)/
					sizeof(params->tetrahedral_17.lut1[0]);
	} else {
		lut0 = params->tetrahedral_9.lut0;
		lut1 = params->tetrahedral_9.lut1;
		lut2 = params->tetrahedral_9.lut2;
		lut3 = params->tetrahedral_9.lut3;
		lut_size0 = sizeof(params->tetrahedral_9.lut0)/
				sizeof(params->tetrahedral_9.lut0[0]);
		lut_size  = sizeof(params->tetrahedral_9.lut1)/
				sizeof(params->tetrahedral_9.lut1[0]);
		}

	mpc32_select_3dlut_ram(mpc, mode,
				is_12bits_color_channel, mpcc_id);
	mpc32_select_3dlut_ram_mask(mpc, 0x1, mpcc_id);
	if (is_12bits_color_channel)
		mpc32_set3dlut_ram12(mpc, lut0, lut_size0, mpcc_id);
	else
		mpc32_set3dlut_ram10(mpc, lut0, lut_size0, mpcc_id);

	mpc32_select_3dlut_ram_mask(mpc, 0x2, mpcc_id);
	if (is_12bits_color_channel)
		mpc32_set3dlut_ram12(mpc, lut1, lut_size, mpcc_id);
	else
		mpc32_set3dlut_ram10(mpc, lut1, lut_size, mpcc_id);

	mpc32_select_3dlut_ram_mask(mpc, 0x4, mpcc_id);
	if (is_12bits_color_channel)
		mpc32_set3dlut_ram12(mpc, lut2, lut_size, mpcc_id);
	else
		mpc32_set3dlut_ram10(mpc, lut2, lut_size, mpcc_id);

	mpc32_select_3dlut_ram_mask(mpc, 0x8, mpcc_id);
	if (is_12bits_color_channel)
		mpc32_set3dlut_ram12(mpc, lut3, lut_size, mpcc_id);
	else
		mpc32_set3dlut_ram10(mpc, lut3, lut_size, mpcc_id);

	mpc32_set_3dlut_mode(mpc, mode, is_12bits_color_channel,
					is_17x17x17, mpcc_id);

	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
		mpc32_power_on_shaper_3dlut(mpc, mpcc_id, false);

	return true;
}

static const struct mpc_funcs dcn32_mpc_funcs = {
	.read_mpcc_state = mpc1_read_mpcc_state,
	.insert_plane = mpc1_insert_plane,
	.remove_mpcc = mpc1_remove_mpcc,
	.mpc_init = mpc32_mpc_init,
	.mpc_init_single_inst = mpc1_mpc_init_single_inst,
	.update_blending = mpc2_update_blending,
	.cursor_lock = mpc1_cursor_lock,
	.get_mpcc_for_dpp = mpc1_get_mpcc_for_dpp,
	.wait_for_idle = mpc2_assert_idle_mpcc,
	.assert_mpcc_idle_before_connect = mpc2_assert_mpcc_idle_before_connect,
	.init_mpcc_list_from_hw = mpc1_init_mpcc_list_from_hw,
	.set_denorm =  mpc3_set_denorm,
	.set_denorm_clamp = mpc3_set_denorm_clamp,
	.set_output_csc = mpc3_set_output_csc,
	.set_ocsc_default = mpc3_set_ocsc_default,
	.set_output_gamma = mpc3_set_output_gamma,
	.insert_plane_to_secondary = NULL,
	.remove_mpcc_from_secondary =  NULL,
	.set_dwb_mux = mpc3_set_dwb_mux,
	.disable_dwb_mux = mpc3_disable_dwb_mux,
	.is_dwb_idle = mpc3_is_dwb_idle,
	.set_out_rate_control = mpc3_set_out_rate_control,
	.set_gamut_remap = mpc3_set_gamut_remap,
	.program_shaper = mpc32_program_shaper,
	.program_3dlut = mpc32_program_3dlut,
	.program_1dlut = mpc32_program_post1dlut,
	.acquire_rmu = NULL,
	.release_rmu = NULL,
	.power_on_mpc_mem_pwr = mpc3_power_on_ogam_lut,
	.get_mpc_out_mux = mpc1_get_mpc_out_mux,
	.set_bg_color = mpc1_set_bg_color,
};


void dcn32_mpc_construct(struct dcn30_mpc *mpc30,
	struct dc_context *ctx,
	const struct dcn30_mpc_registers *mpc_regs,
	const struct dcn30_mpc_shift *mpc_shift,
	const struct dcn30_mpc_mask *mpc_mask,
	int num_mpcc,
	int num_rmu)
{
	int i;

	mpc30->base.ctx = ctx;

	mpc30->base.funcs = &dcn32_mpc_funcs;

	mpc30->mpc_regs = mpc_regs;
	mpc30->mpc_shift = mpc_shift;
	mpc30->mpc_mask = mpc_mask;

	mpc30->mpcc_in_use_mask = 0;
	mpc30->num_mpcc = num_mpcc;
	mpc30->num_rmu = num_rmu;

	for (i = 0; i < MAX_MPCC; i++)
		mpc3_init_mpcc(&mpc30->base.mpcc_array[i], i);
}
