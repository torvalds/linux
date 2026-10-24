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

#include "reg_helper.h"
#include "dc.h"
#include "dcn42_rmcm.h"
#include "dcn10/dcn10_cm_common.h"
#include "basics/conversion.h"
#include "hw_shared.h"
#include "mpc.h"

#define REG(reg)\
	rmcm42->rmcm_regs->reg

#define CTX \
	rmcm42->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	rmcm42->rmcm_shift->field_name, rmcm42->rmcm_mask->field_name

/* RMCM Shaper functions */

void rmcm42_power_on_shaper_3dlut(
	struct rmcm *rmcm,
	uint32_t rmcm_id,
	bool power_on)
{
	uint32_t power_status_shaper = 2;
	uint32_t power_status_3dlut  = 2;
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);
	int max_retries = 10;

	REG_SET(MPC_RMCM_MEM_PWR_CTRL[rmcm_id], 0,
		MPC_RMCM_3DLUT_MEM_PWR_DIS, power_on == true ? 0 : 1);
	REG_SET(MPC_RMCM_MEM_PWR_CTRL[rmcm_id], 0,
		MPC_RMCM_SHAPER_MEM_PWR_DIS, power_on == true ? 0 : 1);
	/* wait for memory to fully power up */
	if (power_on && rmcm->ctx->dc->debug.enable_mem_low_power.bits.mpc) {
		REG_WAIT(MPC_RMCM_MEM_PWR_CTRL[rmcm_id], MPC_RMCM_SHAPER_MEM_PWR_STATE, 0, 1, max_retries);
		REG_WAIT(MPC_RMCM_MEM_PWR_CTRL[rmcm_id], MPC_RMCM_3DLUT_MEM_PWR_STATE, 0, 1, max_retries);
	}

	/*read status is not mandatory, it is just for debugging*/
	REG_GET(MPC_RMCM_MEM_PWR_CTRL[rmcm_id], MPC_RMCM_SHAPER_MEM_PWR_STATE, &power_status_shaper);
	REG_GET(MPC_RMCM_MEM_PWR_CTRL[rmcm_id], MPC_RMCM_3DLUT_MEM_PWR_STATE, &power_status_3dlut);

	if (power_status_shaper != 0 && power_on == true)
		BREAK_TO_DEBUGGER();

	if (power_status_3dlut != 0 && power_on == true)
		BREAK_TO_DEBUGGER();
}

static void rmcm42_configure_shaper_lut(
	struct rmcm *rmcm,
	bool is_ram_a,
	uint32_t rmcm_id)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	REG_UPDATE(MPC_RMCM_SHAPER_SCALE_G_B[rmcm_id],
		MPC_RMCM_SHAPER_SCALE_B, 0x7000);
	REG_UPDATE(MPC_RMCM_SHAPER_SCALE_G_B[rmcm_id],
		MPC_RMCM_SHAPER_SCALE_G, 0x7000);
	REG_UPDATE(MPC_RMCM_SHAPER_SCALE_R[rmcm_id],
		MPC_RMCM_SHAPER_SCALE_R, 0x7000);
	REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[rmcm_id],
			MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[rmcm_id],
			MPC_RMCM_SHAPER_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(MPC_RMCM_SHAPER_LUT_INDEX[rmcm_id], 0, MPC_RMCM_SHAPER_LUT_INDEX, 0);
}

static void rmcm42_program_shaper_luta_settings(
	struct rmcm *rmcm,
	const struct pwl_params *params,
	uint32_t rmcm_id)
{
	const struct gamma_curve *curve;
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	REG_SET_2(MPC_RMCM_SHAPER_RAMA_START_CNTL_B[rmcm_id], 0,
		MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPC_RMCM_SHAPER_RAMA_START_CNTL_G[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].green.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPC_RMCM_SHAPER_RAMA_START_CNTL_R[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].red.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(MPC_RMCM_SHAPER_RAMA_END_CNTL_B[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);
	REG_SET_2(MPC_RMCM_SHAPER_RAMA_END_CNTL_G[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].green.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].green.custom_float_y);
	REG_SET_2(MPC_RMCM_SHAPER_RAMA_END_CNTL_R[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].red.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	if (curve) {
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_0_1[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_2_3[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_4_5[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_6_7[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_8_9[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_10_11[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_12_13[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_14_15[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_16_17[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_18_19[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_20_21[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_22_23[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_24_25[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_26_27[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_28_29[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_30_31[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_32_33[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	}
}


static void rmcm42_program_shaper_lutb_settings(
	struct rmcm *rmcm,
	const struct pwl_params *params,
	uint32_t rmcm_id)
{
	const struct gamma_curve *curve;
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	REG_SET_2(MPC_RMCM_SHAPER_RAMB_START_CNTL_B[rmcm_id], 0,
		MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPC_RMCM_SHAPER_RAMB_START_CNTL_G[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_B, params->corner_points[0].green.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPC_RMCM_SHAPER_RAMB_START_CNTL_R[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_B, params->corner_points[0].red.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(MPC_RMCM_SHAPER_RAMB_END_CNTL_B[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);
	REG_SET_2(MPC_RMCM_SHAPER_RAMB_END_CNTL_G[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_B, params->corner_points[1].green.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_BASE_B, params->corner_points[1].green.custom_float_y);
	REG_SET_2(MPC_RMCM_SHAPER_RAMB_END_CNTL_R[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_B, params->corner_points[1].red.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_BASE_B, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	if (curve) {
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_0_1[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_2_3[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_4_5[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_6_7[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_8_9[rmcm_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_10_11[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_12_13[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_14_15[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_16_17[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_18_19[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_20_21[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_22_23[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_24_25[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_26_27[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_28_29[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_30_31[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_32_33[rmcm_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	}
}

static void rmcm42_program_shaper_lut(
	struct rmcm *rmcm,
	const struct pwl_result_data *rgb,
	uint32_t num,
	uint32_t rmcm_id)
{
	uint32_t i, red, green, blue;
	uint32_t  red_delta, green_delta, blue_delta;
	uint32_t  red_value, green_value, blue_value;

	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	for (i = 0; i < num; i++) {

		red   = rgb[i].red_reg;
		green = rgb[i].green_reg;
		blue  = rgb[i].blue_reg;

		red_delta   = rgb[i].delta_red_reg;
		green_delta = rgb[i].delta_green_reg;
		blue_delta  = rgb[i].delta_blue_reg;

		red_value   = ((red_delta   & 0x3ff) << 14) | (red   & 0x3fff);
		green_value = ((green_delta & 0x3ff) << 14) | (green & 0x3fff);
		blue_value  = ((blue_delta  & 0x3ff) << 14) | (blue  & 0x3fff);

		REG_SET(MPC_RMCM_SHAPER_LUT_DATA[rmcm_id], 0, MPC_RMCM_SHAPER_LUT_DATA, red_value);
		REG_SET(MPC_RMCM_SHAPER_LUT_DATA[rmcm_id], 0, MPC_RMCM_SHAPER_LUT_DATA, green_value);
		REG_SET(MPC_RMCM_SHAPER_LUT_DATA[rmcm_id], 0, MPC_RMCM_SHAPER_LUT_DATA, blue_value);
	}
}

void rmcm42_update_3dlut_fast_load_select(struct rmcm *rmcm, int rmcm_id, int hubp_idx)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	REG_SET(MPC_RMCM_3DLUT_FAST_LOAD_SELECT[rmcm_id], 0,
		MPC_RMCM_3DLUT_FL_SEL,
		hubp_idx);
}

static void rmcm42_select_3dlut_ram_mask(
		struct rmcm *rmcm,
		uint32_t ram_selection_mask,
		int rmcm_id)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_id],
			MPC_RMCM_3DLUT_WRITE_EN_MASK, ram_selection_mask);
	REG_SET(MPC_RMCM_3DLUT_INDEX[rmcm_id], 0, MPC_RMCM_3DLUT_INDEX, 0);
}

static void rmcm42_set3dlut_ram12(
		struct rmcm *rmcm,
		const struct dc_rgb *lut,
		uint32_t entries,
		int rmcm_id)
{
	uint32_t i, red, green, blue, red1, green1, blue1;
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	for (i = 0; i < entries; i += 2) {
		red   = lut[i].red << 4;
		green = lut[i].green << 4;
		blue  = lut[i].blue << 4;
		red1   = lut[i + 1].red << 4;
		green1 = lut[i + 1].green << 4;
		blue1  = lut[i + 1].blue << 4;

		REG_SET_2(MPC_RMCM_3DLUT_DATA[rmcm_id], 0,
				MPC_RMCM_3DLUT_DATA0, red,
				MPC_RMCM_3DLUT_DATA1, red1);

		REG_SET_2(MPC_RMCM_3DLUT_DATA[rmcm_id], 0,
				MPC_RMCM_3DLUT_DATA0, green,
				MPC_RMCM_3DLUT_DATA1, green1);

		REG_SET_2(MPC_RMCM_3DLUT_DATA[rmcm_id], 0,
				MPC_RMCM_3DLUT_DATA0, blue,
				MPC_RMCM_3DLUT_DATA1, blue1);
	}
}

static void rmcm42_set3dlut_ram10(
		struct rmcm *rmcm,
		const struct dc_rgb *lut,
		uint32_t entries,
		int rmcm_id)
{
	uint32_t i, red, green, blue, value;
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	for (i = 0; i < entries; i++) {
		red   = lut[i].red;
		green = lut[i].green;
		blue  = lut[i].blue;
		value = (red << 20) | (green << 10) | blue;

		REG_SET(MPC_RMCM_3DLUT_DATA_30BIT[rmcm_id], 0,
				MPC_RMCM_3DLUT_DATA_30BIT, value);
	}
}

void rmcm42_populate_lut(struct rmcm *rmcm, const enum MCM_LUT_ID id,
	const union rmcm_lut_params params, bool lut_bank_a, int rmcm_id)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);
	const enum dc_lut_mode next_mode = lut_bank_a ? LUT_RAM_A : LUT_RAM_B;

	switch (id) {
	case MCM_LUT_SHAPER: {
		const struct pwl_params *lut_shaper = params.pwl;

		if (lut_shaper == NULL)
			return;
		if (rmcm->ctx->dc->debug.enable_mem_low_power.bits.mpc)
			rmcm42_power_on_shaper_3dlut(rmcm, rmcm_id, true);

		rmcm42_configure_shaper_lut(rmcm, next_mode == LUT_RAM_A, rmcm_id);

		if (next_mode == LUT_RAM_A)
			rmcm42_program_shaper_luta_settings(rmcm, lut_shaper, rmcm_id);
		else
			rmcm42_program_shaper_lutb_settings(rmcm, lut_shaper, rmcm_id);

		rmcm42_program_shaper_lut(
				rmcm, lut_shaper->rgb_resulted, lut_shaper->hw_points_num, rmcm_id);

		rmcm42_power_on_shaper_3dlut(rmcm, rmcm_id, false);
		break;
	}
	case MCM_LUT_3DLUT: {
		const struct tetrahedral_params *lut3d = params.lut3d;
		bool is_17x17x17;
		bool is_12bits_color_channel;
		const struct dc_rgb *lut0;
		const struct dc_rgb *lut1;
		const struct dc_rgb *lut2;
		const struct dc_rgb *lut3;
		int lut_size0;
		int lut_size;

		if (lut3d == NULL)
			return;

		rmcm42_power_on_shaper_3dlut(rmcm, rmcm_id, true);

		/* Program bit depth before loading data */
		is_12bits_color_channel = lut3d->use_12bits;
		REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_id],
			MPC_RMCM_3DLUT_WRITE_EN_MASK, 0xF);
		REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_id],
			MPC_RMCM_3DLUT_30BIT_EN,
			is_12bits_color_channel ? 0 : 1);

		is_17x17x17 = !lut3d->use_tetrahedral_9;
		is_12bits_color_channel = lut3d->use_12bits;
		if (is_17x17x17) {
			lut0 = lut3d->tetrahedral_17.lut0;
			lut1 = lut3d->tetrahedral_17.lut1;
			lut2 = lut3d->tetrahedral_17.lut2;
			lut3 = lut3d->tetrahedral_17.lut3;
			lut_size0 = sizeof(lut3d->tetrahedral_17.lut0) /
						sizeof(lut3d->tetrahedral_17.lut0[0]);
			lut_size  = sizeof(lut3d->tetrahedral_17.lut1) /
						sizeof(lut3d->tetrahedral_17.lut1[0]);
		} else {
			lut0 = lut3d->tetrahedral_9.lut0;
			lut1 = lut3d->tetrahedral_9.lut1;
			lut2 = lut3d->tetrahedral_9.lut2;
			lut3 = lut3d->tetrahedral_9.lut3;
			lut_size0 = sizeof(lut3d->tetrahedral_9.lut0) /
					sizeof(lut3d->tetrahedral_9.lut0[0]);
			lut_size  = sizeof(lut3d->tetrahedral_9.lut1) /
					sizeof(lut3d->tetrahedral_9.lut1[0]);
		}

		rmcm42_select_3dlut_ram_mask(rmcm, 0x1, rmcm_id);
		if (is_12bits_color_channel)
			rmcm42_set3dlut_ram12(rmcm, lut0, lut_size0, rmcm_id);
		else
			rmcm42_set3dlut_ram10(rmcm, lut0, lut_size0, rmcm_id);

		rmcm42_select_3dlut_ram_mask(rmcm, 0x2, rmcm_id);
		if (is_12bits_color_channel)
			rmcm42_set3dlut_ram12(rmcm, lut1, lut_size, rmcm_id);
		else
			rmcm42_set3dlut_ram10(rmcm, lut1, lut_size, rmcm_id);

		rmcm42_select_3dlut_ram_mask(rmcm, 0x4, rmcm_id);
		if (is_12bits_color_channel)
			rmcm42_set3dlut_ram12(rmcm, lut2, lut_size, rmcm_id);
		else
			rmcm42_set3dlut_ram10(rmcm, lut2, lut_size, rmcm_id);

		rmcm42_select_3dlut_ram_mask(rmcm, 0x8, rmcm_id);
		if (is_12bits_color_channel)
			rmcm42_set3dlut_ram12(rmcm, lut3, lut_size, rmcm_id);
		else
			rmcm42_set3dlut_ram10(rmcm, lut3, lut_size, rmcm_id);

		if (rmcm->ctx->dc->debug.enable_mem_low_power.bits.mpc)
			rmcm42_power_on_shaper_3dlut(rmcm, rmcm_id, false);

		break;
	}
	case MCM_LUT_1DLUT:
		/* RMCM doesn't support 1DLUT */
		break;
	}
}

static void rmcm42_program_lut_read_write_control(struct rmcm *rmcm, const enum MCM_LUT_ID id,
	bool lut_bank_a, bool enabled, int rmcm_id)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	switch (id) {
	case MCM_LUT_3DLUT:
		REG_UPDATE(MPC_RMCM_3DLUT_MODE[rmcm_id], MPC_RMCM_3DLUT_MODE,
			(!enabled) ? 0 :
			(lut_bank_a) ? 1 : 2);

		REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_id],
			MPC_RMCM_3DLUT_RAM_SEL,
			(lut_bank_a) ? 0 : 1);
		break;
	case MCM_LUT_SHAPER:
		REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[rmcm_id],
			MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, 7);

		REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[rmcm_id],
			MPC_RMCM_SHAPER_LUT_WRITE_SEL,
			lut_bank_a == true ? 0:1);

		REG_SET(MPC_RMCM_SHAPER_LUT_INDEX[rmcm_id], 0,
			MPC_RMCM_SHAPER_LUT_INDEX, 0);
		break;
	default:
		break;
	}
}

uint32_t rmcm42_get_3dlut_width(
		const enum dc_cm_lut_size size)
{
	uint32_t width = 0;

	switch (size) {
	case CM_LUT_SIZE_999:
		width = 1;
		break;
	case CM_LUT_SIZE_333333:
		width = 2;
		break;
	case CM_LUT_SIZE_171717:
	default:
		width = 0;
		break;
	}

	return width;
}

static void rmcm42_program_lut_mode(struct rmcm *rmcm,
	const enum MCM_LUT_ID id,
	bool enable,
	bool lut_bank_a,
	const enum dc_cm_lut_size size,
	uint16_t bias,
	uint16_t scale,
	int rmcm_id)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	(void)bias;
	(void)scale;

	switch (id) {
	case MCM_LUT_3DLUT:
		if (enable) {
			uint32_t lut_size = rmcm42_get_3dlut_width(size);

			REG_UPDATE_2(MPC_RMCM_3DLUT_MODE[rmcm_id],
					MPC_RMCM_3DLUT_MODE, lut_bank_a ? 1 : 2,
					MPC_RMCM_3DLUT_SIZE, lut_size);

			/* Default to 12-bit (30BIT_EN=0) for DMA path;
			 * populate_lut overrides for host-load path.
			 */
			REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_id],
				MPC_RMCM_3DLUT_WRITE_EN_MASK, 0xF);
			REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_id],
				MPC_RMCM_3DLUT_30BIT_EN, 0);
		} else {
			if (rmcm->ctx->dc->debug.enable_mem_low_power.bits.mpc)
				rmcm42_power_on_shaper_3dlut(rmcm, rmcm_id, false);
			REG_UPDATE(MPC_RMCM_3DLUT_MODE[rmcm_id], MPC_RMCM_3DLUT_MODE, 0);
		}
		break;
	case MCM_LUT_SHAPER:
		if (enable) {
			REG_UPDATE(MPC_RMCM_SHAPER_CONTROL[rmcm_id],
					MPC_RMCM_SHAPER_LUT_MODE, lut_bank_a ? 1 : 2);
		} else {
			if (rmcm->ctx->dc->debug.enable_mem_low_power.bits.mpc)
				rmcm42_power_on_shaper_3dlut(rmcm, rmcm_id, false);
			REG_UPDATE(MPC_RMCM_SHAPER_CONTROL[rmcm_id],
					MPC_RMCM_SHAPER_LUT_MODE, 0);
		}
		break;
	case MCM_LUT_1DLUT:
		/* RMCM doesn't support 1DLUT */
		break;
	}
}

static void rmcm42_get_lut_mode(struct rmcm *rmcm,
		const enum MCM_LUT_ID id,
		int rmcm_id,
		bool *enable,
		bool *lut_bank_a)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);
	uint32_t lut_mode = 0;

	*enable = false;
	*lut_bank_a = true;

	switch (id) {
	case MCM_LUT_SHAPER:
		REG_GET(MPC_RMCM_SHAPER_CONTROL[rmcm_id],
				MPC_RMCM_SHAPER_MODE_CURRENT, &lut_mode);
		*enable = lut_mode != 0;
		*lut_bank_a = lut_mode != 2;
		break;
	case MCM_LUT_1DLUT:
		/* RMCM doesn't support 1DLUT, return disabled */
		*enable = false;
		*lut_bank_a = true;
		break;
	case MCM_LUT_3DLUT:
	default:
		REG_GET(MPC_RMCM_3DLUT_MODE[rmcm_id],
				MPC_RMCM_3DLUT_MODE_CURRENT, &lut_mode);
		*enable = lut_mode != 0;
		*lut_bank_a = lut_mode != 2;
		break;
	}
}

void rmcm42_get_3dlut_fast_load_status(struct rmcm *rmcm, int rmcm_id,
	bool *done, bool *soft_underflow, bool *hard_underflow)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);
	uint32_t fl_done = 0, fl_soft = 0, fl_hard = 0;

	REG_GET_3(MPC_RMCM_3DLUT_FAST_LOAD_STATUS[rmcm_id],
		MPC_RMCM_3DLUT_FL_DONE, &fl_done,
		MPC_RMCM_3DLUT_FL_SOFT_UNDERFLOW, &fl_soft,
		MPC_RMCM_3DLUT_FL_HARD_UNDERFLOW, &fl_hard);

	*done = (fl_done != 0);
	*soft_underflow = (fl_soft != 0);
	*hard_underflow = (fl_hard != 0);
}

void rmcm42_read_state(struct rmcm *rmcm, int rmcm_inst, struct rmcm_state *s)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	/* RMCM 3DLUT Status */
	REG_GET_4(MPC_RMCM_MEM_PWR_CTRL[rmcm_inst], MPC_RMCM_3DLUT_MEM_PWR_FORCE, &s->regs.rmcm_3dlut_mem_pwr_force,
			MPC_RMCM_3DLUT_MEM_PWR_DIS, &s->regs.rmcm_3dlut_mem_pwr_dis,
			MPC_RMCM_3DLUT_MEM_LOW_PWR_MODE, &s->regs.rmcm_3dlut_mem_pwr_mode,
			MPC_RMCM_3DLUT_MEM_PWR_STATE, &s->regs.rmcm_3dlut_mem_pwr_state);

	REG_GET_3(MPC_RMCM_3DLUT_MODE[rmcm_inst], MPC_RMCM_3DLUT_SIZE, &s->regs.rmcm_3dlut_size,
			MPC_RMCM_3DLUT_MODE, &s->regs.rmcm_3dlut_mode,
			MPC_RMCM_3DLUT_MODE_CURRENT, &s->regs.rmcm_3dlut_mode_cur);

	REG_GET_4(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_inst], MPC_RMCM_3DLUT_READ_SEL, &s->regs.rmcm_3dlut_read_sel,
			MPC_RMCM_3DLUT_30BIT_EN, &s->regs.rmcm_3dlut_30bit_en,
			MPC_RMCM_3DLUT_WRITE_EN_MASK, &s->regs.rmcm_3dlut_wr_en_mask,
			MPC_RMCM_3DLUT_RAM_SEL, &s->regs.rmcm_3dlut_ram_sel);

	REG_GET(MPC_RMCM_3DLUT_OUT_NORM_FACTOR[rmcm_inst], MPC_RMCM_3DLUT_OUT_NORM_FACTOR, &s->regs.rmcm_3dlut_out_norm_factor);

	REG_GET(MPC_RMCM_3DLUT_FAST_LOAD_SELECT[rmcm_inst], MPC_RMCM_3DLUT_FL_SEL, &s->regs.rmcm_3dlut_fl_sel);

	REG_GET_2(MPC_RMCM_3DLUT_OUT_OFFSET_R[rmcm_inst], MPC_RMCM_3DLUT_OUT_OFFSET_R, &s->regs.rmcm_3dlut_out_offset_r,
			MPC_RMCM_3DLUT_OUT_SCALE_R, &s->regs.rmcm_3dlut_out_scale_r);

	REG_GET_3(MPC_RMCM_3DLUT_FAST_LOAD_STATUS[rmcm_inst], MPC_RMCM_3DLUT_FL_DONE, &s->regs.rmcm_3dlut_fl_done,
			MPC_RMCM_3DLUT_FL_SOFT_UNDERFLOW, &s->regs.rmcm_3dlut_fl_soft_underflow,
			MPC_RMCM_3DLUT_FL_HARD_UNDERFLOW, &s->regs.rmcm_3dlut_fl_hard_underflow);

	/* RMCM Shaper Status */
	REG_GET_4(MPC_RMCM_MEM_PWR_CTRL[rmcm_inst], MPC_RMCM_SHAPER_MEM_PWR_FORCE, &s->regs.rmcm_shaper_mem_pwr_force,
			MPC_RMCM_SHAPER_MEM_PWR_DIS, &s->regs.rmcm_shaper_mem_pwr_dis,
			MPC_RMCM_SHAPER_MEM_LOW_PWR_MODE, &s->regs.rmcm_shaper_mem_pwr_mode,
			MPC_RMCM_SHAPER_MEM_PWR_STATE, &s->regs.rmcm_shaper_mem_pwr_state);

	REG_GET_2(MPC_RMCM_SHAPER_CONTROL[rmcm_inst], MPC_RMCM_SHAPER_LUT_MODE, &s->regs.rmcm_shaper_lut_mode,
			MPC_RMCM_SHAPER_MODE_CURRENT, &s->regs.rmcm_shaper_mode_cur);

	REG_GET_2(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[rmcm_inst], MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, &s->regs.rmcm_shaper_lut_write_en_mask,
			MPC_RMCM_SHAPER_LUT_WRITE_SEL, &s->regs.rmcm_shaper_lut_write_sel);

	REG_GET(MPC_RMCM_SHAPER_OFFSET_B[rmcm_inst], MPC_RMCM_SHAPER_OFFSET_B, &s->regs.rmcm_shaper_offset_b);

	REG_GET(MPC_RMCM_SHAPER_SCALE_G_B[rmcm_inst], MPC_RMCM_SHAPER_SCALE_B, &s->regs.rmcm_shaper_scale_b);

	REG_GET_2(MPC_RMCM_SHAPER_RAMA_START_CNTL_B[rmcm_inst], MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B, &s->regs.rmcm_shaper_rama_exp_region_start_b,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, &s->regs.rmcm_shaper_rama_exp_region_start_seg_b);

	REG_GET_2(MPC_RMCM_SHAPER_RAMA_END_CNTL_B[rmcm_inst], MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B, &s->regs.rmcm_shaper_rama_exp_region_end_b,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, &s->regs.rmcm_shaper_rama_exp_region_end_base_b);

	REG_GET(MPC_RMCM_CNTL[rmcm_inst], MPC_RMCM_CNTL, &s->regs.rmcm_cntl);
}

/*
 * rmcm42_connect_mpcc() - Program RMCM to MPCC routing mux
 * @rmcm: RMCM instance
 * @rmcm_id: RMCM hardware index
 * @mpcc_id: MPCC to connect (0-3), or RMCM_MPCC_ID_NONE to disconnect
 *
 * Programs MPC_RMCM_CNTL to route the specified MPCC to this RMCM instance,
 * allowing any MPCC[x] to RMCM[y] mapping.
 */
void rmcm42_connect_mpcc(struct rmcm *rmcm, int rmcm_id, int mpcc_id)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	REG_UPDATE(MPC_RMCM_CNTL[rmcm_id], MPC_RMCM_CNTL, mpcc_id);

	rmcm->mpcc_id = mpcc_id;
}

const struct rmcm_funcs dcn42_rmcm_funcs = {
	.get_lut_mode = rmcm42_get_lut_mode,
	.program_lut_mode = rmcm42_program_lut_mode,
	.populate_lut = rmcm42_populate_lut,
	.program_lut_read_write_control = rmcm42_program_lut_read_write_control,
	.update_3dlut_fast_load_select = rmcm42_update_3dlut_fast_load_select,
	.get_3dlut_fast_load_status = rmcm42_get_3dlut_fast_load_status,
	.read_rmcm_state = rmcm42_read_state,
	.connect_mpcc = rmcm42_connect_mpcc,
};

void dcn42_rmcm_construct(
	struct dcn42_rmcm *rmcm42,
	struct dc_context *ctx,
	const struct dcn42_rmcm_registers *regs,
	const struct dcn42_rmcm_shift *shift,
	const struct dcn42_rmcm_mask *mask,
	int inst)
{
	rmcm42->base.ctx = ctx;
	rmcm42->base.inst = inst;
	rmcm42->base.mpcc_id = RMCM_MPCC_ID_UNKNOWN;
	rmcm42->base.funcs = &dcn42_rmcm_funcs;
	rmcm42->rmcm_regs = regs;
	rmcm42->rmcm_shift = shift;
	rmcm42->rmcm_mask = mask;
}
