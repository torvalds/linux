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

#include "reg_helper.h"
#include "dcn30_mpc.h"
#include "dcn30_cm_common.h"
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


#define NUM_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))


bool mpc3_is_dwb_idle(
	struct mpc *mpc,
	int dwb_id)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	unsigned int status;

	REG_GET(DWB_MUX[dwb_id], MPC_DWB0_MUX_STATUS, &status);

	if (status == 0xf)
		return true;
	else
		return false;
}

void mpc3_set_dwb_mux(
	struct mpc *mpc,
	int dwb_id,
	int mpcc_id)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_SET(DWB_MUX[dwb_id], 0,
		MPC_DWB0_MUX, mpcc_id);
}

void mpc3_disable_dwb_mux(
	struct mpc *mpc,
	int dwb_id)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_SET(DWB_MUX[dwb_id], 0,
		MPC_DWB0_MUX, 0xf);
}

void mpc3_set_out_rate_control(
	struct mpc *mpc,
	int opp_id,
	bool enable,
	bool rate_2x_mode,
	struct mpc_dwb_flow_control *flow_control)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_UPDATE_2(MUX[opp_id],
			MPC_OUT_RATE_CONTROL_DISABLE, !enable,
			MPC_OUT_RATE_CONTROL, rate_2x_mode);

	if (flow_control)
		REG_UPDATE_2(MUX[opp_id],
			MPC_OUT_FLOW_CONTROL_MODE, flow_control->flow_ctrl_mode,
			MPC_OUT_FLOW_CONTROL_COUNT, flow_control->flow_ctrl_cnt1);
}

enum dc_lut_mode mpc3_get_ogam_current(struct mpc *mpc, int mpcc_id)
{
	/*Contrary to DCN2 and DCN1 wherein a single status register field holds this info;
	 *in DCN3/3AG, we need to read two separate fields to retrieve the same info
	 */
	enum dc_lut_mode mode;
	uint32_t state_mode;
	uint32_t state_ram_lut_in_use;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_GET_2(MPCC_OGAM_CONTROL[mpcc_id], MPCC_OGAM_MODE_CURRENT, &state_mode,
		  MPCC_OGAM_SELECT_CURRENT, &state_ram_lut_in_use);

	switch (state_mode) {
	case 0:
		mode = LUT_BYPASS;
		break;
	case 2:
		switch (state_ram_lut_in_use) {
		case 0:
			mode = LUT_RAM_A;
			break;
		case 1:
			mode = LUT_RAM_B;
			break;
		default:
			mode = LUT_BYPASS;
			break;
		}
		break;
	default:
		mode = LUT_BYPASS;
		break;
	}

	return mode;
}

void mpc3_power_on_ogam_lut(
		struct mpc *mpc, int mpcc_id,
		bool power_on)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	/*
	 * Powering on: force memory active so the LUT can be updated.
	 * Powering off: allow entering memory low power mode
	 *
	 * Memory low power mode is controlled during MPC OGAM LUT init.
	 */
	REG_UPDATE(MPCC_MEM_PWR_CTRL[mpcc_id],
		   MPCC_OGAM_MEM_PWR_DIS, power_on != 0);

	/* Wait for memory to be powered on - we won't be able to write to it otherwise. */
	if (power_on)
		REG_WAIT(MPCC_MEM_PWR_CTRL[mpcc_id], MPCC_OGAM_MEM_PWR_STATE, 0, 10, 10);
}

static void mpc3_configure_ogam_lut(
		struct mpc *mpc, int mpcc_id,
		bool is_ram_a)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_UPDATE_2(MPCC_OGAM_LUT_CONTROL[mpcc_id],
			MPCC_OGAM_LUT_WRITE_COLOR_MASK, 7,
			MPCC_OGAM_LUT_HOST_SEL, is_ram_a == true ? 0:1);

	REG_SET(MPCC_OGAM_LUT_INDEX[mpcc_id], 0, MPCC_OGAM_LUT_INDEX, 0);
}

static void mpc3_ogam_get_reg_field(
		struct mpc *mpc,
		struct dcn3_xfer_func_reg *reg)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	reg->shifts.field_region_start_base = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_START_BASE_B;
	reg->masks.field_region_start_base = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_START_BASE_B;
	reg->shifts.field_offset = mpc30->mpc_shift->MPCC_OGAM_RAMA_OFFSET_B;
	reg->masks.field_offset = mpc30->mpc_mask->MPCC_OGAM_RAMA_OFFSET_B;

	reg->shifts.exp_region0_lut_offset = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->masks.exp_region0_lut_offset = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION0_LUT_OFFSET;
	reg->shifts.exp_region0_num_segments = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->masks.exp_region0_num_segments = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION0_NUM_SEGMENTS;
	reg->shifts.exp_region1_lut_offset = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->masks.exp_region1_lut_offset = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION1_LUT_OFFSET;
	reg->shifts.exp_region1_num_segments = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;
	reg->masks.exp_region1_num_segments = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION1_NUM_SEGMENTS;

	reg->shifts.field_region_end = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_END_B;
	reg->masks.field_region_end = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_END_B;
	reg->shifts.field_region_end_slope = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->masks.field_region_end_slope = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_B;
	reg->shifts.field_region_end_base = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->masks.field_region_end_base = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_END_BASE_B;
	reg->shifts.field_region_linear_slope = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_START_SLOPE_B;
	reg->masks.field_region_linear_slope = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_START_SLOPE_B;
	reg->shifts.exp_region_start = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_START_B;
	reg->masks.exp_region_start = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_START_B;
	reg->shifts.exp_resion_start_segment = mpc30->mpc_shift->MPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_B;
	reg->masks.exp_resion_start_segment = mpc30->mpc_mask->MPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_B;
}

static void mpc3_program_luta(struct mpc *mpc, int mpcc_id,
		const struct pwl_params *params)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	struct dcn3_xfer_func_reg gam_regs;

	mpc3_ogam_get_reg_field(mpc, &gam_regs);

	gam_regs.start_cntl_b = REG(MPCC_OGAM_RAMA_START_CNTL_B[mpcc_id]);
	gam_regs.start_cntl_g = REG(MPCC_OGAM_RAMA_START_CNTL_G[mpcc_id]);
	gam_regs.start_cntl_r = REG(MPCC_OGAM_RAMA_START_CNTL_R[mpcc_id]);
	gam_regs.start_slope_cntl_b = REG(MPCC_OGAM_RAMA_START_SLOPE_CNTL_B[mpcc_id]);
	gam_regs.start_slope_cntl_g = REG(MPCC_OGAM_RAMA_START_SLOPE_CNTL_G[mpcc_id]);
	gam_regs.start_slope_cntl_r = REG(MPCC_OGAM_RAMA_START_SLOPE_CNTL_R[mpcc_id]);
	gam_regs.start_end_cntl1_b = REG(MPCC_OGAM_RAMA_END_CNTL1_B[mpcc_id]);
	gam_regs.start_end_cntl2_b = REG(MPCC_OGAM_RAMA_END_CNTL2_B[mpcc_id]);
	gam_regs.start_end_cntl1_g = REG(MPCC_OGAM_RAMA_END_CNTL1_G[mpcc_id]);
	gam_regs.start_end_cntl2_g = REG(MPCC_OGAM_RAMA_END_CNTL2_G[mpcc_id]);
	gam_regs.start_end_cntl1_r = REG(MPCC_OGAM_RAMA_END_CNTL1_R[mpcc_id]);
	gam_regs.start_end_cntl2_r = REG(MPCC_OGAM_RAMA_END_CNTL2_R[mpcc_id]);
	gam_regs.region_start = REG(MPCC_OGAM_RAMA_REGION_0_1[mpcc_id]);
	gam_regs.region_end = REG(MPCC_OGAM_RAMA_REGION_32_33[mpcc_id]);
	//New registers in DCN3AG/DCN OGAM block
	gam_regs.offset_b =  REG(MPCC_OGAM_RAMA_OFFSET_B[mpcc_id]);
	gam_regs.offset_g =  REG(MPCC_OGAM_RAMA_OFFSET_G[mpcc_id]);
	gam_regs.offset_r =  REG(MPCC_OGAM_RAMA_OFFSET_R[mpcc_id]);
	gam_regs.start_base_cntl_b = REG(MPCC_OGAM_RAMA_START_BASE_CNTL_B[mpcc_id]);
	gam_regs.start_base_cntl_g = REG(MPCC_OGAM_RAMA_START_BASE_CNTL_G[mpcc_id]);
	gam_regs.start_base_cntl_r = REG(MPCC_OGAM_RAMA_START_BASE_CNTL_R[mpcc_id]);

	cm_helper_program_gamcor_xfer_func(mpc30->base.ctx, params, &gam_regs);
}

static void mpc3_program_lutb(struct mpc *mpc, int mpcc_id,
		const struct pwl_params *params)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	struct dcn3_xfer_func_reg gam_regs;

	mpc3_ogam_get_reg_field(mpc, &gam_regs);

	gam_regs.start_cntl_b = REG(MPCC_OGAM_RAMB_START_CNTL_B[mpcc_id]);
	gam_regs.start_cntl_g = REG(MPCC_OGAM_RAMB_START_CNTL_G[mpcc_id]);
	gam_regs.start_cntl_r = REG(MPCC_OGAM_RAMB_START_CNTL_R[mpcc_id]);
	gam_regs.start_slope_cntl_b = REG(MPCC_OGAM_RAMB_START_SLOPE_CNTL_B[mpcc_id]);
	gam_regs.start_slope_cntl_g = REG(MPCC_OGAM_RAMB_START_SLOPE_CNTL_G[mpcc_id]);
	gam_regs.start_slope_cntl_r = REG(MPCC_OGAM_RAMB_START_SLOPE_CNTL_R[mpcc_id]);
	gam_regs.start_end_cntl1_b = REG(MPCC_OGAM_RAMB_END_CNTL1_B[mpcc_id]);
	gam_regs.start_end_cntl2_b = REG(MPCC_OGAM_RAMB_END_CNTL2_B[mpcc_id]);
	gam_regs.start_end_cntl1_g = REG(MPCC_OGAM_RAMB_END_CNTL1_G[mpcc_id]);
	gam_regs.start_end_cntl2_g = REG(MPCC_OGAM_RAMB_END_CNTL2_G[mpcc_id]);
	gam_regs.start_end_cntl1_r = REG(MPCC_OGAM_RAMB_END_CNTL1_R[mpcc_id]);
	gam_regs.start_end_cntl2_r = REG(MPCC_OGAM_RAMB_END_CNTL2_R[mpcc_id]);
	gam_regs.region_start = REG(MPCC_OGAM_RAMB_REGION_0_1[mpcc_id]);
	gam_regs.region_end = REG(MPCC_OGAM_RAMB_REGION_32_33[mpcc_id]);
	//New registers in DCN3AG/DCN OGAM block
	gam_regs.offset_b =  REG(MPCC_OGAM_RAMB_OFFSET_B[mpcc_id]);
	gam_regs.offset_g =  REG(MPCC_OGAM_RAMB_OFFSET_G[mpcc_id]);
	gam_regs.offset_r =  REG(MPCC_OGAM_RAMB_OFFSET_R[mpcc_id]);
	gam_regs.start_base_cntl_b = REG(MPCC_OGAM_RAMB_START_BASE_CNTL_B[mpcc_id]);
	gam_regs.start_base_cntl_g = REG(MPCC_OGAM_RAMB_START_BASE_CNTL_G[mpcc_id]);
	gam_regs.start_base_cntl_r = REG(MPCC_OGAM_RAMB_START_BASE_CNTL_R[mpcc_id]);

	cm_helper_program_gamcor_xfer_func(mpc30->base.ctx, params, &gam_regs);
}


static void mpc3_program_ogam_pwl(
		struct mpc *mpc, int mpcc_id,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	uint32_t last_base_value_red = rgb[num-1].red_reg + rgb[num-1].delta_red_reg;
	uint32_t last_base_value_green = rgb[num-1].green_reg + rgb[num-1].delta_green_reg;
	uint32_t last_base_value_blue = rgb[num-1].blue_reg + rgb[num-1].delta_blue_reg;

	/*the entries of DCN3AG gamma LUTs take 18bit base values as opposed to
	 *38 base+delta values per entry in earlier DCN architectures
	 *last base value for our lut is compute by adding the last base value
	 *in our data + last delta
	 */

	if (is_rgb_equal(rgb,  num)) {
		for (i = 0 ; i < num; i++)
			REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, rgb[i].red_reg);

		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, last_base_value_red);

	} else {

		REG_UPDATE(MPCC_OGAM_LUT_CONTROL[mpcc_id],
				MPCC_OGAM_LUT_WRITE_COLOR_MASK, 4);

		for (i = 0 ; i < num; i++)
			REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, rgb[i].red_reg);

		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, last_base_value_red);

		REG_SET(MPCC_OGAM_LUT_INDEX[mpcc_id], 0, MPCC_OGAM_LUT_INDEX, 0);

		REG_UPDATE(MPCC_OGAM_LUT_CONTROL[mpcc_id],
				MPCC_OGAM_LUT_WRITE_COLOR_MASK, 2);

		for (i = 0 ; i < num; i++)
			REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, rgb[i].green_reg);

		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, last_base_value_green);

		REG_SET(MPCC_OGAM_LUT_INDEX[mpcc_id], 0, MPCC_OGAM_LUT_INDEX, 0);

		REG_UPDATE(MPCC_OGAM_LUT_CONTROL[mpcc_id],
				MPCC_OGAM_LUT_WRITE_COLOR_MASK, 1);

		for (i = 0 ; i < num; i++)
			REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, rgb[i].blue_reg);

		REG_SET(MPCC_OGAM_LUT_DATA[mpcc_id], 0, MPCC_OGAM_LUT_DATA, last_base_value_blue);
	}

}

void mpc3_set_output_gamma(
		struct mpc *mpc,
		int mpcc_id,
		const struct pwl_params *params)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	if (mpc->ctx->dc->debug.cm_in_bypass) {
		REG_SET(MPCC_OGAM_MODE[mpcc_id], 0, MPCC_OGAM_MODE, 0);
		return;
	}

	if (params == NULL) { //disable OGAM
		REG_SET(MPCC_OGAM_CONTROL[mpcc_id], 0, MPCC_OGAM_MODE, 0);
		return;
	}
	//enable OGAM
	REG_SET(MPCC_OGAM_CONTROL[mpcc_id], 0, MPCC_OGAM_MODE, 2);

	current_mode = mpc3_get_ogam_current(mpc, mpcc_id);
	if (current_mode == LUT_BYPASS)
		next_mode = LUT_RAM_A;
	else if (current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	mpc3_power_on_ogam_lut(mpc, mpcc_id, true);
	mpc3_configure_ogam_lut(mpc, mpcc_id, next_mode == LUT_RAM_A);

	if (next_mode == LUT_RAM_A)
		mpc3_program_luta(mpc, mpcc_id, params);
	else
		mpc3_program_lutb(mpc, mpcc_id, params);

	mpc3_program_ogam_pwl(
			mpc, mpcc_id, params->rgb_resulted, params->hw_points_num);

	/*we need to program 2 fields here as apposed to 1*/
	REG_UPDATE(MPCC_OGAM_CONTROL[mpcc_id],
			MPCC_OGAM_SELECT, next_mode == LUT_RAM_A ? 0:1);

	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
		mpc3_power_on_ogam_lut(mpc, mpcc_id, false);
}

void mpc3_set_denorm(
		struct mpc *mpc,
		int opp_id,
		enum dc_color_depth output_depth)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	/* De-normalize Fixed U1.13 color data to different target bit depths. 0 is bypass*/
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

void mpc3_set_denorm_clamp(
		struct mpc *mpc,
		int opp_id,
		struct mpc_denorm_clamp denorm_clamp)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	/*program min and max clamp values for the pixel components*/
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

static enum dc_lut_mode mpc3_get_shaper_current(struct mpc *mpc, uint32_t rmu_idx)
{
	enum dc_lut_mode mode;
	uint32_t state_mode;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_GET(SHAPER_CONTROL[rmu_idx], MPC_RMU_SHAPER_LUT_MODE_CURRENT, &state_mode);

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

static void mpc3_configure_shaper_lut(
		struct mpc *mpc,
		bool is_ram_a,
		uint32_t rmu_idx)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_UPDATE(SHAPER_LUT_WRITE_EN_MASK[rmu_idx],
			MPC_RMU_SHAPER_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(SHAPER_LUT_WRITE_EN_MASK[rmu_idx],
			MPC_RMU_SHAPER_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(SHAPER_LUT_INDEX[rmu_idx], 0, MPC_RMU_SHAPER_LUT_INDEX, 0);
}

static void mpc3_program_shaper_luta_settings(
		struct mpc *mpc,
		const struct pwl_params *params,
		uint32_t rmu_idx)
{
	const struct gamma_curve *curve;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_SET_2(SHAPER_RAMA_START_CNTL_B[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		MPC_RMU_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(SHAPER_RAMA_START_CNTL_G[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].green.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(SHAPER_RAMA_START_CNTL_R[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].red.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(SHAPER_RAMA_END_CNTL_B[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);
	REG_SET_2(SHAPER_RAMA_END_CNTL_G[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].green.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].green.custom_float_y);
	REG_SET_2(SHAPER_RAMA_END_CNTL_R[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].red.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(SHAPER_RAMA_REGION_0_1[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_2_3[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_4_5[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_6_7[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_8_9[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_10_11[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_12_13[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_14_15[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_16_17[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_18_19[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_20_21[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_22_23[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_24_25[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_26_27[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_28_29[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_30_31[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMA_REGION_32_33[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
}

static void mpc3_program_shaper_lutb_settings(
		struct mpc *mpc,
		const struct pwl_params *params,
		uint32_t rmu_idx)
{
	const struct gamma_curve *curve;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_SET_2(SHAPER_RAMB_START_CNTL_B[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		MPC_RMU_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(SHAPER_RAMB_START_CNTL_G[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].green.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(SHAPER_RAMB_START_CNTL_R[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].red.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(SHAPER_RAMB_END_CNTL_B[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);
	REG_SET_2(SHAPER_RAMB_END_CNTL_G[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].green.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].green.custom_float_y);
	REG_SET_2(SHAPER_RAMB_END_CNTL_R[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].red.custom_float_x,
			MPC_RMU_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(SHAPER_RAMB_REGION_0_1[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_2_3[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_4_5[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_6_7[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_8_9[rmu_idx], 0,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_10_11[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_12_13[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_14_15[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_16_17[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_18_19[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_20_21[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_22_23[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_24_25[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_26_27[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_28_29[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_30_31[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(SHAPER_RAMB_REGION_32_33[rmu_idx], 0,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMU_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
}


static void mpc3_program_shaper_lut(
		struct mpc *mpc,
		const struct pwl_result_data *rgb,
		uint32_t num,
		uint32_t rmu_idx)
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

		REG_SET(SHAPER_LUT_DATA[rmu_idx], 0, MPC_RMU_SHAPER_LUT_DATA, red_value);
		REG_SET(SHAPER_LUT_DATA[rmu_idx], 0, MPC_RMU_SHAPER_LUT_DATA, green_value);
		REG_SET(SHAPER_LUT_DATA[rmu_idx], 0, MPC_RMU_SHAPER_LUT_DATA, blue_value);
	}

}

static void mpc3_power_on_shaper_3dlut(
		struct mpc *mpc,
		uint32_t rmu_idx,
	bool power_on)
{
	uint32_t power_status_shaper = 2;
	uint32_t power_status_3dlut  = 2;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	int max_retries = 10;

	if (rmu_idx == 0) {
		REG_SET(MPC_RMU_MEM_PWR_CTRL, 0,
			MPC_RMU0_MEM_PWR_DIS, power_on == true ? 1:0);
		/* wait for memory to fully power up */
		if (power_on && mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc) {
			REG_WAIT(MPC_RMU_MEM_PWR_CTRL, MPC_RMU0_SHAPER_MEM_PWR_STATE, 0, 1, max_retries);
			REG_WAIT(MPC_RMU_MEM_PWR_CTRL, MPC_RMU0_3DLUT_MEM_PWR_STATE, 0, 1, max_retries);
		}

		/*read status is not mandatory, it is just for debugging*/
		REG_GET(MPC_RMU_MEM_PWR_CTRL, MPC_RMU0_SHAPER_MEM_PWR_STATE, &power_status_shaper);
		REG_GET(MPC_RMU_MEM_PWR_CTRL, MPC_RMU0_3DLUT_MEM_PWR_STATE, &power_status_3dlut);
	} else if (rmu_idx == 1) {
		REG_SET(MPC_RMU_MEM_PWR_CTRL, 0,
			MPC_RMU1_MEM_PWR_DIS, power_on == true ? 1:0);
		if (power_on && mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc) {
			REG_WAIT(MPC_RMU_MEM_PWR_CTRL, MPC_RMU1_SHAPER_MEM_PWR_STATE, 0, 1, max_retries);
			REG_WAIT(MPC_RMU_MEM_PWR_CTRL, MPC_RMU1_3DLUT_MEM_PWR_STATE, 0, 1, max_retries);
		}

		REG_GET(MPC_RMU_MEM_PWR_CTRL, MPC_RMU1_SHAPER_MEM_PWR_STATE, &power_status_shaper);
		REG_GET(MPC_RMU_MEM_PWR_CTRL, MPC_RMU1_3DLUT_MEM_PWR_STATE, &power_status_3dlut);
	}
	/*TODO Add rmu_idx == 2 for SIENNA_CICHLID */
	if (power_status_shaper != 0 && power_on == true)
		BREAK_TO_DEBUGGER();

	if (power_status_3dlut != 0 && power_on == true)
		BREAK_TO_DEBUGGER();
}



bool mpc3_program_shaper(
		struct mpc *mpc,
		const struct pwl_params *params,
		uint32_t rmu_idx)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;

	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	if (params == NULL) {
		REG_SET(SHAPER_CONTROL[rmu_idx], 0, MPC_RMU_SHAPER_LUT_MODE, 0);
		return false;
	}

	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
		mpc3_power_on_shaper_3dlut(mpc, rmu_idx, true);

	current_mode = mpc3_get_shaper_current(mpc, rmu_idx);

	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	mpc3_configure_shaper_lut(mpc, next_mode == LUT_RAM_A, rmu_idx);

	if (next_mode == LUT_RAM_A)
		mpc3_program_shaper_luta_settings(mpc, params, rmu_idx);
	else
		mpc3_program_shaper_lutb_settings(mpc, params, rmu_idx);

	mpc3_program_shaper_lut(
			mpc, params->rgb_resulted, params->hw_points_num, rmu_idx);

	REG_SET(SHAPER_CONTROL[rmu_idx], 0, MPC_RMU_SHAPER_LUT_MODE, next_mode == LUT_RAM_A ? 1:2);
	mpc3_power_on_shaper_3dlut(mpc, rmu_idx, false);

	return true;
}

static void mpc3_set_3dlut_mode(
		struct mpc *mpc,
		enum dc_lut_mode mode,
		bool is_color_channel_12bits,
		bool is_lut_size17x17x17,
		uint32_t rmu_idx)
{
	uint32_t lut_mode;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	if (mode == LUT_BYPASS)
		lut_mode = 0;
	else if (mode == LUT_RAM_A)
		lut_mode = 1;
	else
		lut_mode = 2;

	REG_UPDATE_2(RMU_3DLUT_MODE[rmu_idx],
			MPC_RMU_3DLUT_MODE, lut_mode,
			MPC_RMU_3DLUT_SIZE, is_lut_size17x17x17 == true ? 0 : 1);
}

static enum dc_lut_mode get3dlut_config(
			struct mpc *mpc,
			bool *is_17x17x17,
			bool *is_12bits_color_channel,
			int rmu_idx)
{
	uint32_t i_mode, i_enable_10bits, lut_size;
	enum dc_lut_mode mode;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_GET(RMU_3DLUT_MODE[rmu_idx],
			MPC_RMU_3DLUT_MODE_CURRENT,  &i_mode);

	REG_GET(RMU_3DLUT_READ_WRITE_CONTROL[rmu_idx],
			MPC_RMU_3DLUT_30BIT_EN, &i_enable_10bits);

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

	REG_GET(RMU_3DLUT_MODE[rmu_idx], MPC_RMU_3DLUT_SIZE, &lut_size);

	if (lut_size == 0)
		*is_17x17x17 = true;
	else
		*is_17x17x17 = false;

	return mode;
}

static void mpc3_select_3dlut_ram(
		struct mpc *mpc,
		enum dc_lut_mode mode,
		bool is_color_channel_12bits,
		uint32_t rmu_idx)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_UPDATE_2(RMU_3DLUT_READ_WRITE_CONTROL[rmu_idx],
		MPC_RMU_3DLUT_RAM_SEL, mode == LUT_RAM_A ? 0 : 1,
		MPC_RMU_3DLUT_30BIT_EN, is_color_channel_12bits == true ? 0:1);
}

static void mpc3_select_3dlut_ram_mask(
		struct mpc *mpc,
		uint32_t ram_selection_mask,
		uint32_t rmu_idx)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	REG_UPDATE(RMU_3DLUT_READ_WRITE_CONTROL[rmu_idx], MPC_RMU_3DLUT_WRITE_EN_MASK,
			ram_selection_mask);
	REG_SET(RMU_3DLUT_INDEX[rmu_idx], 0, MPC_RMU_3DLUT_INDEX, 0);
}

static void mpc3_set3dlut_ram12(
		struct mpc *mpc,
		const struct dc_rgb *lut,
		uint32_t entries,
		uint32_t rmu_idx)
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

		REG_SET_2(RMU_3DLUT_DATA[rmu_idx], 0,
				MPC_RMU_3DLUT_DATA0, red,
				MPC_RMU_3DLUT_DATA1, red1);

		REG_SET_2(RMU_3DLUT_DATA[rmu_idx], 0,
				MPC_RMU_3DLUT_DATA0, green,
				MPC_RMU_3DLUT_DATA1, green1);

		REG_SET_2(RMU_3DLUT_DATA[rmu_idx], 0,
				MPC_RMU_3DLUT_DATA0, blue,
				MPC_RMU_3DLUT_DATA1, blue1);
	}
}

static void mpc3_set3dlut_ram10(
		struct mpc *mpc,
		const struct dc_rgb *lut,
		uint32_t entries,
		uint32_t rmu_idx)
{
	uint32_t i, red, green, blue, value;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	for (i = 0; i < entries; i++) {
		red   = lut[i].red;
		green = lut[i].green;
		blue  = lut[i].blue;
		//should we shift red 22bit and green 12? ask Nvenko
		value = (red<<20) | (green<<10) | blue;

		REG_SET(RMU_3DLUT_DATA_30BIT[rmu_idx], 0, MPC_RMU_3DLUT_DATA_30BIT, value);
	}

}


void mpc3_init_mpcc(struct mpcc *mpcc, int mpcc_inst)
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
	mpcc->shared_bottom = false;
}

static void program_gamut_remap(
		struct dcn30_mpc *mpc30,
		int mpcc_id,
		const uint16_t *regval,
		int select)
{
	uint16_t selection = 0;
	struct color_matrices_reg gam_regs;

	if (regval == NULL || select == GAMUT_REMAP_BYPASS) {
		REG_SET(MPCC_GAMUT_REMAP_MODE[mpcc_id], 0,
				MPCC_GAMUT_REMAP_MODE, GAMUT_REMAP_BYPASS);
		return;
	}
	switch (select) {
	case GAMUT_REMAP_COEFF:
		selection = 1;
		break;
		/*this corresponds to GAMUT_REMAP coefficients set B
		 * we don't have common coefficient sets in dcn3ag/dcn3
		 */
	case GAMUT_REMAP_COMA_COEFF:
		selection = 2;
		break;
	default:
		break;
	}

	gam_regs.shifts.csc_c11 = mpc30->mpc_shift->MPCC_GAMUT_REMAP_C11_A;
	gam_regs.masks.csc_c11  = mpc30->mpc_mask->MPCC_GAMUT_REMAP_C11_A;
	gam_regs.shifts.csc_c12 = mpc30->mpc_shift->MPCC_GAMUT_REMAP_C12_A;
	gam_regs.masks.csc_c12 = mpc30->mpc_mask->MPCC_GAMUT_REMAP_C12_A;


	if (select == GAMUT_REMAP_COEFF) {
		gam_regs.csc_c11_c12 = REG(MPC_GAMUT_REMAP_C11_C12_A[mpcc_id]);
		gam_regs.csc_c33_c34 = REG(MPC_GAMUT_REMAP_C33_C34_A[mpcc_id]);

		cm_helper_program_color_matrices(
				mpc30->base.ctx,
				regval,
				&gam_regs);

	} else  if (select == GAMUT_REMAP_COMA_COEFF) {

		gam_regs.csc_c11_c12 = REG(MPC_GAMUT_REMAP_C11_C12_B[mpcc_id]);
		gam_regs.csc_c33_c34 = REG(MPC_GAMUT_REMAP_C33_C34_B[mpcc_id]);

		cm_helper_program_color_matrices(
				mpc30->base.ctx,
				regval,
				&gam_regs);

	}
	//select coefficient set to use
	REG_SET(MPCC_GAMUT_REMAP_MODE[mpcc_id], 0,
					MPCC_GAMUT_REMAP_MODE, selection);
}

void mpc3_set_gamut_remap(
		struct mpc *mpc,
		int mpcc_id,
		const struct mpc_grph_gamut_adjustment *adjust)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	int i = 0;
	int gamut_mode;

	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW)
		program_gamut_remap(mpc30, mpcc_id, NULL, GAMUT_REMAP_BYPASS);
	else {
		struct fixed31_32 arr_matrix[12];
		uint16_t arr_reg_val[12];

		for (i = 0; i < 12; i++)
			arr_matrix[i] = adjust->temperature_matrix[i];

		convert_float_matrix(
			arr_reg_val, arr_matrix, 12);

		//current coefficient set in use
		REG_GET(MPCC_GAMUT_REMAP_MODE[mpcc_id], MPCC_GAMUT_REMAP_MODE_CURRENT, &gamut_mode);

		if (gamut_mode == 0)
			gamut_mode = 1; //use coefficient set A
		else if (gamut_mode == 1)
			gamut_mode = 2;
		else
			gamut_mode = 1;

		program_gamut_remap(mpc30, mpcc_id, arr_reg_val, gamut_mode);
	}
}

bool mpc3_program_3dlut(
		struct mpc *mpc,
		const struct tetrahedral_params *params,
		int rmu_idx)
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
		mpc3_set_3dlut_mode(mpc, LUT_BYPASS, false, false, rmu_idx);
		return false;
	}
	mpc3_power_on_shaper_3dlut(mpc, rmu_idx, true);

	mode = get3dlut_config(mpc, &is_17x17x17, &is_12bits_color_channel, rmu_idx);

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

	mpc3_select_3dlut_ram(mpc, mode,
				is_12bits_color_channel, rmu_idx);
	mpc3_select_3dlut_ram_mask(mpc, 0x1, rmu_idx);
	if (is_12bits_color_channel)
		mpc3_set3dlut_ram12(mpc, lut0, lut_size0, rmu_idx);
	else
		mpc3_set3dlut_ram10(mpc, lut0, lut_size0, rmu_idx);

	mpc3_select_3dlut_ram_mask(mpc, 0x2, rmu_idx);
	if (is_12bits_color_channel)
		mpc3_set3dlut_ram12(mpc, lut1, lut_size, rmu_idx);
	else
		mpc3_set3dlut_ram10(mpc, lut1, lut_size, rmu_idx);

	mpc3_select_3dlut_ram_mask(mpc, 0x4, rmu_idx);
	if (is_12bits_color_channel)
		mpc3_set3dlut_ram12(mpc, lut2, lut_size, rmu_idx);
	else
		mpc3_set3dlut_ram10(mpc, lut2, lut_size, rmu_idx);

	mpc3_select_3dlut_ram_mask(mpc, 0x8, rmu_idx);
	if (is_12bits_color_channel)
		mpc3_set3dlut_ram12(mpc, lut3, lut_size, rmu_idx);
	else
		mpc3_set3dlut_ram10(mpc, lut3, lut_size, rmu_idx);

	mpc3_set_3dlut_mode(mpc, mode, is_12bits_color_channel,
					is_17x17x17, rmu_idx);

	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
		mpc3_power_on_shaper_3dlut(mpc, rmu_idx, false);

	return true;
}

void mpc3_set_output_csc(
		struct mpc *mpc,
		int opp_id,
		const uint16_t *regval,
		enum mpc_output_csc_mode ocsc_mode)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	struct color_matrices_reg ocsc_regs;

	REG_WRITE(MPC_OUT_CSC_COEF_FORMAT, 0);

	REG_SET(CSC_MODE[opp_id], 0, MPC_OCSC_MODE, ocsc_mode);

	if (ocsc_mode == MPC_OUTPUT_CSC_DISABLE)
		return;

	if (regval == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	ocsc_regs.shifts.csc_c11 = mpc30->mpc_shift->MPC_OCSC_C11_A;
	ocsc_regs.masks.csc_c11  = mpc30->mpc_mask->MPC_OCSC_C11_A;
	ocsc_regs.shifts.csc_c12 = mpc30->mpc_shift->MPC_OCSC_C12_A;
	ocsc_regs.masks.csc_c12 = mpc30->mpc_mask->MPC_OCSC_C12_A;

	if (ocsc_mode == MPC_OUTPUT_CSC_COEF_A) {
		ocsc_regs.csc_c11_c12 = REG(CSC_C11_C12_A[opp_id]);
		ocsc_regs.csc_c33_c34 = REG(CSC_C33_C34_A[opp_id]);
	} else {
		ocsc_regs.csc_c11_c12 = REG(CSC_C11_C12_B[opp_id]);
		ocsc_regs.csc_c33_c34 = REG(CSC_C33_C34_B[opp_id]);
	}
	cm_helper_program_color_matrices(
			mpc30->base.ctx,
			regval,
			&ocsc_regs);
}

void mpc3_set_ocsc_default(
		struct mpc *mpc,
		int opp_id,
		enum dc_color_space color_space,
		enum mpc_output_csc_mode ocsc_mode)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	uint32_t arr_size;
	struct color_matrices_reg ocsc_regs;
	const uint16_t *regval = NULL;

	REG_WRITE(MPC_OUT_CSC_COEF_FORMAT, 0);

	REG_SET(CSC_MODE[opp_id], 0, MPC_OCSC_MODE, ocsc_mode);
	if (ocsc_mode == MPC_OUTPUT_CSC_DISABLE)
		return;

	regval = find_color_matrix(color_space, &arr_size);

	if (regval == NULL) {
		BREAK_TO_DEBUGGER();
		return;
	}

	ocsc_regs.shifts.csc_c11 = mpc30->mpc_shift->MPC_OCSC_C11_A;
	ocsc_regs.masks.csc_c11  = mpc30->mpc_mask->MPC_OCSC_C11_A;
	ocsc_regs.shifts.csc_c12 = mpc30->mpc_shift->MPC_OCSC_C12_A;
	ocsc_regs.masks.csc_c12 = mpc30->mpc_mask->MPC_OCSC_C12_A;


	if (ocsc_mode == MPC_OUTPUT_CSC_COEF_A) {
		ocsc_regs.csc_c11_c12 = REG(CSC_C11_C12_A[opp_id]);
		ocsc_regs.csc_c33_c34 = REG(CSC_C33_C34_A[opp_id]);
	} else {
		ocsc_regs.csc_c11_c12 = REG(CSC_C11_C12_B[opp_id]);
		ocsc_regs.csc_c33_c34 = REG(CSC_C33_C34_B[opp_id]);
	}

	cm_helper_program_color_matrices(
			mpc30->base.ctx,
			regval,
			&ocsc_regs);
}

void mpc3_set_rmu_mux(
	struct mpc *mpc,
	int rmu_idx,
	int value)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	if (rmu_idx == 0)
		REG_UPDATE(MPC_RMU_CONTROL, MPC_RMU0_MUX, value);
	else if (rmu_idx == 1)
		REG_UPDATE(MPC_RMU_CONTROL, MPC_RMU1_MUX, value);

}

uint32_t mpc3_get_rmu_mux_status(
	struct mpc *mpc,
	int rmu_idx)
{
	uint32_t status = 0xf;
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);

	if (rmu_idx == 0)
		REG_GET(MPC_RMU_CONTROL, MPC_RMU0_MUX_STATUS, &status);
	else if (rmu_idx == 1)
		REG_GET(MPC_RMU_CONTROL, MPC_RMU1_MUX_STATUS, &status);

	return status;
}

uint32_t mpcc3_acquire_rmu(struct mpc *mpc, int mpcc_id, int rmu_idx)
{
	uint32_t rmu_status;

	//determine if this mpcc is already multiplexed to an RMU unit
	rmu_status = mpc3_get_rmu_mux_status(mpc, rmu_idx);
	if (rmu_status == mpcc_id)
		//return rmu_idx of pre_acquired rmu unit
		return rmu_idx;

	if (rmu_status == 0xf) {//rmu unit is disabled
		mpc3_set_rmu_mux(mpc, rmu_idx, mpcc_id);
		return rmu_idx;
	}

	//no vacant RMU units or invalid parameters acquire_post_bldn_3dlut
	return -1;
}

static int mpcc3_release_rmu(struct mpc *mpc, int mpcc_id)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	int rmu_idx;
	uint32_t rmu_status;
	int released_rmu = -1;

	for (rmu_idx = 0; rmu_idx < mpc30->num_rmu; rmu_idx++) {
		rmu_status = mpc3_get_rmu_mux_status(mpc, rmu_idx);
		if (rmu_status == mpcc_id) {
			mpc3_set_rmu_mux(mpc, rmu_idx, 0xf);
			released_rmu = rmu_idx;
			break;
		}
	}
	return released_rmu;

}

static void mpc3_set_mpc_mem_lp_mode(struct mpc *mpc)
{
	struct dcn30_mpc *mpc30 = TO_DCN30_MPC(mpc);
	int mpcc_id;

	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc) {
		if (mpc30->mpc_mask->MPC_RMU0_MEM_LOW_PWR_MODE && mpc30->mpc_mask->MPC_RMU1_MEM_LOW_PWR_MODE) {
			REG_UPDATE(MPC_RMU_MEM_PWR_CTRL, MPC_RMU0_MEM_LOW_PWR_MODE, 3);
			REG_UPDATE(MPC_RMU_MEM_PWR_CTRL, MPC_RMU1_MEM_LOW_PWR_MODE, 3);
		}

		if (mpc30->mpc_mask->MPCC_OGAM_MEM_LOW_PWR_MODE) {
			for (mpcc_id = 0; mpcc_id < mpc30->num_mpcc; mpcc_id++)
				REG_UPDATE(MPCC_MEM_PWR_CTRL[mpcc_id], MPCC_OGAM_MEM_LOW_PWR_MODE, 3);
		}
	}
}

static const struct mpc_funcs dcn30_mpc_funcs = {
	.read_mpcc_state = mpc1_read_mpcc_state,
	.insert_plane = mpc1_insert_plane,
	.remove_mpcc = mpc1_remove_mpcc,
	.mpc_init = mpc1_mpc_init,
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
	.program_shaper = mpc3_program_shaper,
	.acquire_rmu = mpcc3_acquire_rmu,
	.program_3dlut = mpc3_program_3dlut,
	.release_rmu = mpcc3_release_rmu,
	.power_on_mpc_mem_pwr = mpc3_power_on_ogam_lut,
	.get_mpc_out_mux = mpc1_get_mpc_out_mux,
	.set_bg_color = mpc1_set_bg_color,
	.set_mpc_mem_lp_mode = mpc3_set_mpc_mem_lp_mode,
};

void dcn30_mpc_construct(struct dcn30_mpc *mpc30,
	struct dc_context *ctx,
	const struct dcn30_mpc_registers *mpc_regs,
	const struct dcn30_mpc_shift *mpc_shift,
	const struct dcn30_mpc_mask *mpc_mask,
	int num_mpcc,
	int num_rmu)
{
	int i;

	mpc30->base.ctx = ctx;

	mpc30->base.funcs = &dcn30_mpc_funcs;

	mpc30->mpc_regs = mpc_regs;
	mpc30->mpc_shift = mpc_shift;
	mpc30->mpc_mask = mpc_mask;

	mpc30->mpcc_in_use_mask = 0;
	mpc30->num_mpcc = num_mpcc;
	mpc30->num_rmu = num_rmu;

	for (i = 0; i < MAX_MPCC; i++)
		mpc3_init_mpcc(&mpc30->base.mpcc_array[i], i);
}

