// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "reg_helper.h"
#include "dc.h"
#include "dcn42_mpc.h"
#include "dcn10/dcn10_cm_common.h"
#include "basics/conversion.h"
#include "mpc.h"

#define REG(reg)\
	mpc42->mpc_regs->reg

#define CTX \
	mpc42->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mpc42->mpc_shift->field_name, mpc42->mpc_mask->field_name


static void mpc42_init_mpcc(struct mpcc *mpcc, int mpcc_inst)
{
	mpcc->mpcc_id = mpcc_inst;
	mpcc->dpp_id = 0xf;
	mpcc->mpcc_bot = NULL;
	mpcc->blnd_cfg.overlap_only = false;
	mpcc->blnd_cfg.global_alpha = 0xfff;
	mpcc->blnd_cfg.global_gain = 0xfff;
	mpcc->blnd_cfg.background_color_bpc = 4;
	mpcc->blnd_cfg.bottom_gain_mode = 0;
	mpcc->blnd_cfg.top_gain = 0x1f000;
	mpcc->blnd_cfg.bottom_inside_gain = 0x1f000;
	mpcc->blnd_cfg.bottom_outside_gain = 0x1f000;
	mpcc->sm_cfg.enable = false;
	mpcc->shared_bottom = false;
}

void mpc42_update_blending(
	struct mpc *mpc,
	struct mpcc_blnd_cfg *blnd_cfg,
	int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	struct mpcc *mpcc = mpc1_get_mpcc(mpc, mpcc_id);

	REG_UPDATE_5(MPCC_CONTROL[mpcc_id],
			MPCC_ALPHA_BLND_MODE,		blnd_cfg->alpha_mode,
			MPCC_ALPHA_MULTIPLIED_MODE,	blnd_cfg->pre_multiplied_alpha,
			MPCC_BLND_ACTIVE_OVERLAP_ONLY,	blnd_cfg->overlap_only,
			MPCC_BG_BPC,			blnd_cfg->background_color_bpc,
			MPCC_BOT_GAIN_MODE,		blnd_cfg->bottom_gain_mode);
	REG_UPDATE_2(MPCC_CONTROL2[mpcc_id],
			MPCC_GLOBAL_ALPHA,		blnd_cfg->global_alpha,
			MPCC_GLOBAL_GAIN,		blnd_cfg->global_gain);

	REG_SET(MPCC_TOP_GAIN[mpcc_id], 0, MPCC_TOP_GAIN, blnd_cfg->top_gain);
	REG_SET(MPCC_BOT_GAIN_INSIDE[mpcc_id], 0, MPCC_BOT_GAIN_INSIDE, blnd_cfg->bottom_inside_gain);
	REG_SET(MPCC_BOT_GAIN_OUTSIDE[mpcc_id], 0, MPCC_BOT_GAIN_OUTSIDE, blnd_cfg->bottom_outside_gain);

	mpcc->blnd_cfg = *blnd_cfg;
}

/* Shaper functions */
void mpc42_power_on_shaper_3dlut(
	struct mpc *mpc,
	uint32_t mpcc_id,
	bool power_on)
{
	uint32_t power_status_shaper = 2;
	uint32_t power_status_3dlut  = 2;
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);
	int max_retries = 10;

	REG_SET(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], 0,
		MPCC_MCM_3DLUT_MEM_PWR_DIS, power_on == true ? 1:0);
	REG_SET(MPCC_MCM_MEM_PWR_CTRL[mpcc_id], 0,
		MPCC_MCM_SHAPER_MEM_PWR_DIS, power_on == true ? 1:0);
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

void mpc42_configure_shaper_lut(
	struct mpc *mpc,
	bool is_ram_a,
	uint32_t mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	REG_UPDATE(MPCC_MCM_SHAPER_SCALE_G_B[mpcc_id],
		MPCC_MCM_SHAPER_SCALE_B, 0x7000);
	REG_UPDATE(MPCC_MCM_SHAPER_SCALE_G_B[mpcc_id],
		MPCC_MCM_SHAPER_SCALE_G, 0x7000);
	REG_UPDATE(MPCC_MCM_SHAPER_SCALE_R[mpcc_id],
		MPCC_MCM_SHAPER_SCALE_R, 0x7000);
	REG_UPDATE(MPCC_MCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPCC_MCM_SHAPER_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(MPCC_MCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPCC_MCM_SHAPER_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(MPCC_MCM_SHAPER_LUT_INDEX[mpcc_id], 0, MPCC_MCM_SHAPER_LUT_INDEX, 0);
}


void mpc42_program_3dlut_size(struct mpc *mpc, uint32_t width, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);
	uint32_t size = 0xff;

	REG_GET(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_SIZE, &size);

	REG_UPDATE(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_SIZE,
		(width == 33) ? 2 :
		(width == 17) ? 0 : 2);

	REG_GET(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_SIZE, &size);
}

void mpc42_program_3dlut_fl_bias_scale(struct mpc *mpc, uint16_t bias, uint16_t scale, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	REG_UPDATE_2(MPCC_MCM_3DLUT_OUT_OFFSET_R[mpcc_id],
		MPCC_MCM_3DLUT_OUT_OFFSET_R, bias,
		MPCC_MCM_3DLUT_OUT_SCALE_R, scale);

	REG_UPDATE_2(MPCC_MCM_3DLUT_OUT_OFFSET_G[mpcc_id],
		MPCC_MCM_3DLUT_OUT_OFFSET_G, bias,
		MPCC_MCM_3DLUT_OUT_SCALE_G, scale);

	REG_UPDATE_2(MPCC_MCM_3DLUT_OUT_OFFSET_B[mpcc_id],
		MPCC_MCM_3DLUT_OUT_OFFSET_B, bias,
		MPCC_MCM_3DLUT_OUT_SCALE_B, scale);
}

void mpc42_program_bit_depth(struct mpc *mpc, uint16_t bit_depth, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	REG_UPDATE(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id], MPCC_MCM_3DLUT_WRITE_EN_MASK, 0xF);

	//program bit_depth
	REG_UPDATE(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
		MPCC_MCM_3DLUT_30BIT_EN,
		(bit_depth == 10) ? 1 : 0);
}

bool mpc42_is_config_supported(uint32_t width)
{
	if (width == 17)
		return true;

	return false;
}

void mpc42_populate_lut(struct mpc *mpc, const union mcm_lut_params params,
	bool lut_bank_a, int mpcc_id)
{
	const enum dc_lut_mode next_mode = lut_bank_a ? LUT_RAM_A : LUT_RAM_B;
	const struct pwl_params *lut_shaper = params.pwl;

	if (lut_shaper == NULL)
		return;
	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
		mpc42_power_on_shaper_3dlut(mpc, mpcc_id, true);

	mpc42_configure_shaper_lut(mpc, next_mode == LUT_RAM_A, mpcc_id);

	if (next_mode == LUT_RAM_A)
		mpc32_program_shaper_luta_settings(mpc, lut_shaper, mpcc_id);
	else
		mpc32_program_shaper_lutb_settings(mpc, lut_shaper, mpcc_id);

	mpc32_program_shaper_lut(
			mpc, lut_shaper->rgb_resulted, lut_shaper->hw_points_num, mpcc_id);

	mpc42_power_on_shaper_3dlut(mpc, mpcc_id, false);
}

void mpc42_program_lut_read_write_control(struct mpc *mpc, const enum MCM_LUT_ID id,
	bool lut_bank_a, bool enabled, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	switch (id) {
	case MCM_LUT_3DLUT:
		REG_UPDATE(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_MODE,
			(!enabled) ? 0 :
			(lut_bank_a) ? 1 : 2);
		REG_UPDATE(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id], MPCC_MCM_3DLUT_RAM_SEL, lut_bank_a ? 0 : 1);
		break;
	case MCM_LUT_SHAPER:
		mpc32_configure_shaper_lut(mpc, lut_bank_a, mpcc_id);
		break;
	default:
		break;
	}
}

/* RMCM Shaper functions */
void mpc42_power_on_rmcm_shaper_3dlut(
	struct mpc *mpc,
	uint32_t mpcc_id,
	bool power_on)
{
	uint32_t power_status_shaper = 2;
	uint32_t power_status_3dlut  = 2;
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);
	int max_retries = 10;

	REG_SET(MPC_RMCM_MEM_PWR_CTRL[mpcc_id], 0,
		MPC_RMCM_3DLUT_MEM_PWR_DIS, power_on == true ? 0 : 1);
	REG_SET(MPC_RMCM_MEM_PWR_CTRL[mpcc_id], 0,
		MPC_RMCM_SHAPER_MEM_PWR_DIS, power_on == true ? 0 : 1);
	/* wait for memory to fully power up */
	if (power_on && mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc) {
		REG_WAIT(MPC_RMCM_MEM_PWR_CTRL[mpcc_id], MPC_RMCM_SHAPER_MEM_PWR_STATE, 0, 1, max_retries);
		REG_WAIT(MPC_RMCM_MEM_PWR_CTRL[mpcc_id], MPC_RMCM_3DLUT_MEM_PWR_STATE, 0, 1, max_retries);
	}

	/*read status is not mandatory, it is just for debugging*/
	REG_GET(MPC_RMCM_MEM_PWR_CTRL[mpcc_id], MPC_RMCM_SHAPER_MEM_PWR_STATE, &power_status_shaper);
	REG_GET(MPC_RMCM_MEM_PWR_CTRL[mpcc_id], MPC_RMCM_3DLUT_MEM_PWR_STATE, &power_status_3dlut);

	if (power_status_shaper != 0 && power_on == true)
		BREAK_TO_DEBUGGER();

	if (power_status_3dlut != 0 && power_on == true)
		BREAK_TO_DEBUGGER();
}

void mpc42_configure_rmcm_shaper_lut(
	struct mpc *mpc,
	bool is_ram_a,
	uint32_t mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	REG_UPDATE(MPC_RMCM_SHAPER_SCALE_G_B[mpcc_id],
		MPC_RMCM_SHAPER_SCALE_B, 0x7000);
	REG_UPDATE(MPC_RMCM_SHAPER_SCALE_G_B[mpcc_id],
		MPC_RMCM_SHAPER_SCALE_G, 0x7000);
	REG_UPDATE(MPC_RMCM_SHAPER_SCALE_R[mpcc_id],
		MPC_RMCM_SHAPER_SCALE_R, 0x7000);
	REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPC_RMCM_SHAPER_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(MPC_RMCM_SHAPER_LUT_INDEX[mpcc_id], 0, MPC_RMCM_SHAPER_LUT_INDEX, 0);
}

void mpc42_program_rmcm_shaper_luta_settings(
	struct mpc *mpc,
	const struct pwl_params *params,
	uint32_t mpcc_id)
{
	const struct gamma_curve *curve;
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	REG_SET_2(MPC_RMCM_SHAPER_RAMA_START_CNTL_B[mpcc_id], 0,
		MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPC_RMCM_SHAPER_RAMA_START_CNTL_G[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].green.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPC_RMCM_SHAPER_RAMA_START_CNTL_R[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B, params->corner_points[0].red.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(MPC_RMCM_SHAPER_RAMA_END_CNTL_B[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);
	REG_SET_2(MPC_RMCM_SHAPER_RAMA_END_CNTL_G[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].green.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].green.custom_float_y);
	REG_SET_2(MPC_RMCM_SHAPER_RAMA_END_CNTL_R[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B, params->corner_points[1].red.custom_float_x,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	if (curve) {
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_0_1[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_2_3[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_4_5[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_6_7[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_8_9[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_10_11[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_12_13[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_14_15[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_16_17[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_18_19[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_20_21[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_22_23[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_24_25[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_26_27[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_28_29[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_30_31[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMA_REGION_32_33[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	}
}


void mpc42_program_rmcm_shaper_lutb_settings(
	struct mpc *mpc,
	const struct pwl_params *params,
	uint32_t mpcc_id)
{
	const struct gamma_curve *curve;
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	REG_SET_2(MPC_RMCM_SHAPER_RAMB_START_CNTL_B[mpcc_id], 0,
		MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_B, params->corner_points[0].blue.custom_float_x,
		MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPC_RMCM_SHAPER_RAMB_START_CNTL_G[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_B, params->corner_points[0].green.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(MPC_RMCM_SHAPER_RAMB_START_CNTL_R[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_B, params->corner_points[0].red.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_START_SEGMENT_B, 0);

	REG_SET_2(MPC_RMCM_SHAPER_RAMB_END_CNTL_B[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_B, params->corner_points[1].blue.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_BASE_B, params->corner_points[1].blue.custom_float_y);
	REG_SET_2(MPC_RMCM_SHAPER_RAMB_END_CNTL_G[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_B, params->corner_points[1].green.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_BASE_B, params->corner_points[1].green.custom_float_y);
	REG_SET_2(MPC_RMCM_SHAPER_RAMB_END_CNTL_R[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_B, params->corner_points[1].red.custom_float_x,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION_END_BASE_B, params->corner_points[1].red.custom_float_y);

	curve = params->arr_curve_points;
	if (curve) {
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_0_1[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_2_3[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);


		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_4_5[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_6_7[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_8_9[mpcc_id], 0,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
			MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_10_11[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_12_13[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_14_15[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_16_17[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_18_19[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_20_21[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_22_23[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_24_25[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_26_27[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_28_29[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_30_31[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

		curve += 2;
		REG_SET_4(MPC_RMCM_SHAPER_RAMB_REGION_32_33[mpcc_id], 0,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
				MPC_RMCM_SHAPER_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);
	}
}

void mpc42_program_rmcm_shaper_lut(
	struct mpc *mpc,
	const struct pwl_result_data *rgb,
	uint32_t num,
	uint32_t mpcc_id)
{
	uint32_t i, red, green, blue;
	uint32_t  red_delta, green_delta, blue_delta;
	uint32_t  red_value, green_value, blue_value;

	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

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

		REG_SET(MPC_RMCM_SHAPER_LUT_DATA[mpcc_id], 0, MPC_RMCM_SHAPER_LUT_DATA, red_value);
		REG_SET(MPC_RMCM_SHAPER_LUT_DATA[mpcc_id], 0, MPC_RMCM_SHAPER_LUT_DATA, green_value);
		REG_SET(MPC_RMCM_SHAPER_LUT_DATA[mpcc_id], 0, MPC_RMCM_SHAPER_LUT_DATA, blue_value);
	}
}

void mpc42_enable_3dlut_fl(struct mpc *mpc, bool enable, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	//if enabled cho0se mpc 0, else: off (default value)
	REG_UPDATE(MPC_RMCM_CNTL[mpcc_id], MPC_RMCM_CNTL, enable ? 0 : 0xF); //0xF is not connected

	REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[mpcc_id], MPC_RMCM_3DLUT_WRITE_EN_MASK, 0);

	REG_UPDATE(MPC_RMCM_MEM_PWR_CTRL[mpcc_id], MPC_RMCM_3DLUT_MEM_PWR_DIS, enable ? 0 : 3);
}

void mpc42_update_3dlut_fast_load_select(struct mpc *mpc, int mpcc_id, int hubp_idx)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	REG_SET(MPC_RMCM_3DLUT_FAST_LOAD_SELECT[mpcc_id], 0,
		MPC_RMCM_3DLUT_FL_SEL,
		hubp_idx);
}

void mpc42_populate_rmcm_lut(struct mpc *mpc, const union mcm_lut_params params,
	bool lut_bank_a, int mpcc_id)
{
	const enum dc_lut_mode next_mode = lut_bank_a ? LUT_RAM_A : LUT_RAM_B;
	const struct pwl_params *lut_shaper = params.pwl;

	if (lut_shaper == NULL)
		return;
	if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
		mpc42_power_on_rmcm_shaper_3dlut(mpc, mpcc_id, true);

	mpc42_configure_rmcm_shaper_lut(mpc, next_mode == LUT_RAM_A, mpcc_id);

	if (next_mode == LUT_RAM_A)
		mpc42_program_rmcm_shaper_luta_settings(mpc, lut_shaper, mpcc_id);
	else
		mpc42_program_rmcm_shaper_lutb_settings(mpc, lut_shaper, mpcc_id);

	mpc42_program_rmcm_shaper_lut(
			mpc, lut_shaper->rgb_resulted, lut_shaper->hw_points_num, mpcc_id);

	mpc42_power_on_rmcm_shaper_3dlut(mpc, mpcc_id, false);
}

void mpc42_program_rmcm_lut_read_write_control(struct mpc *mpc, const enum MCM_LUT_ID id,
	bool lut_bank_a, bool enabled, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	switch (id) {
	case MCM_LUT_3DLUT:
		REG_UPDATE(MPC_RMCM_3DLUT_MODE[mpcc_id], MPC_RMCM_3DLUT_MODE,
			(!enabled) ? 0 :
			(lut_bank_a) ? 1 : 2);

		REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
			MPC_RMCM_3DLUT_RAM_SEL,
			(lut_bank_a) ? 0 : 1);
		break;
	case MCM_LUT_SHAPER:
		REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, 7);

		REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPC_RMCM_SHAPER_LUT_WRITE_SEL,
			lut_bank_a == true ? 0:1);

		REG_SET(MPC_RMCM_SHAPER_LUT_INDEX[mpcc_id], 0,
			MPC_RMCM_SHAPER_LUT_INDEX, 0);
		break;
	default:
		break;
	}
}

void mpc42_program_lut_mode(struct mpc *mpc, const enum MCM_LUT_XABLE xable,
	bool lut_bank_a, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	switch (xable) {
	case MCM_LUT_DISABLE:
		REG_UPDATE(MPC_RMCM_SHAPER_CONTROL[mpcc_id], MPC_RMCM_SHAPER_LUT_MODE, 0);
		break;
	case MCM_LUT_ENABLE:
		REG_UPDATE(MPC_RMCM_SHAPER_CONTROL[mpcc_id], MPC_RMCM_SHAPER_LUT_MODE, lut_bank_a ? 1 : 2);
		break;
	}
}

void mpc42_program_rmcm_3dlut_size(struct mpc *mpc, uint32_t width, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);
	uint32_t size = 0xff;

	REG_GET(MPC_RMCM_3DLUT_MODE[mpcc_id], MPC_RMCM_3DLUT_SIZE, &size);

	REG_UPDATE(MPC_RMCM_3DLUT_MODE[mpcc_id], MPC_RMCM_3DLUT_SIZE,
		(width == 33) ? 2 : 0);

	REG_GET(MPC_RMCM_3DLUT_MODE[mpcc_id], MPC_RMCM_3DLUT_SIZE, &size);
}

void mpc42_program_rmcm_3dlut_fast_load_bias_scale(struct mpc *mpc, uint16_t bias, uint16_t scale, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	REG_UPDATE_2(MPC_RMCM_3DLUT_OUT_OFFSET_R[mpcc_id],
		MPC_RMCM_3DLUT_OUT_OFFSET_R, bias,
		MPC_RMCM_3DLUT_OUT_SCALE_R, scale);

	REG_UPDATE_2(MPC_RMCM_3DLUT_OUT_OFFSET_G[mpcc_id],
		MPC_RMCM_3DLUT_OUT_OFFSET_G, bias,
		MPC_RMCM_3DLUT_OUT_SCALE_G, scale);

	REG_UPDATE_2(MPC_RMCM_3DLUT_OUT_OFFSET_B[mpcc_id],
		MPC_RMCM_3DLUT_OUT_OFFSET_B, bias,
		MPC_RMCM_3DLUT_OUT_SCALE_B, scale);
}

void mpc42_program_rmcm_bit_depth(struct mpc *mpc, uint16_t bit_depth, int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[mpcc_id], MPC_RMCM_3DLUT_WRITE_EN_MASK, 0xF);

	//program bit_depth
	REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
		MPC_RMCM_3DLUT_30BIT_EN,
		(bit_depth == 10) ? 1 : 0);
}

bool mpc42_is_rmcm_config_supported(uint32_t width)
{
	if (width == 17 || width == 33)
		return true;

	return false;
}

void mpc42_set_fl_config(
	struct mpc *mpc,
	struct mpc_fl_3dlut_config *cfg,
	int mpcc_id)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	/*
	From: Jie Zhou

		To program any of the memories content.  The following sequence is used.
	Set the MPCC_OGAM/SHAPER/3DLUT/1DLUT_PWR_DIS to 1 (Only need to set the one
	that is being programmed) Set DISPCLK_G_PIPE<i>_GATE_DISABLE to 1 for the
	MPCC pipe that’s being used, so the memory’s clock is ungated. Program the
	target memory. Set the MPCC_OGAM/SHAPER/3DLUT/1DLUT_PWR_DIS back to 0.
	Set DISPCLK_G_PIPE<i>_GATE_DISABLE back to 0
	*/

	//disconnect fl from mpc
	REG_SET(MPCC_MCM_3DLUT_FAST_LOAD_SELECT[mpcc_id], 0,
		MPCC_MCM_3DLUT_FL_SEL, 0xF);

	REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
		MPC_RMCM_3DLUT_WRITE_EN_MASK, 0xF);

	//program bit_depth
	REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
		MPC_RMCM_3DLUT_30BIT_EN, (cfg->bit_depth == 10) ? 1 : 0);

	REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
		MPC_RMCM_3DLUT_RAM_SEL, (cfg->select_lut_bank_a) ? 0 : 1);

	//bias and scale
	REG_UPDATE_2(MPC_RMCM_3DLUT_OUT_OFFSET_R[mpcc_id],
		MPC_RMCM_3DLUT_OUT_OFFSET_R, cfg->bias,
		MPC_RMCM_3DLUT_OUT_SCALE_R, cfg->scale);

	REG_UPDATE_2(MPC_RMCM_3DLUT_OUT_OFFSET_G[mpcc_id],
		MPC_RMCM_3DLUT_OUT_OFFSET_G, cfg->bias,
		MPC_RMCM_3DLUT_OUT_SCALE_G, cfg->scale);

	REG_UPDATE_2(MPC_RMCM_3DLUT_OUT_OFFSET_B[mpcc_id],
		MPC_RMCM_3DLUT_OUT_OFFSET_B, cfg->bias,
		MPC_RMCM_3DLUT_OUT_SCALE_B, cfg->scale);

	//width
	REG_UPDATE_2(MPC_RMCM_3DLUT_MODE[mpcc_id],
		MPC_RMCM_3DLUT_SIZE, (cfg->width == 33) ? 2 : 0,
		MPC_RMCM_3DLUT_MODE, (!cfg->enabled) ? 0 : (cfg->select_lut_bank_a) ? 1 : 2);

	//connect to hubp
	REG_SET(MPC_RMCM_3DLUT_FAST_LOAD_SELECT[mpcc_id], 0,
		MPC_RMCM_3DLUT_FL_SEL, cfg->hubp_index);

	//ENABLE
	//if enabled pick mpc 0, else: off (0xF)
	//in future we'll select specific MPC
	REG_UPDATE(MPC_RMCM_CNTL[mpcc_id], MPC_RMCM_CNTL, cfg->enabled ? 0 : 0xF);
}

//static void rmcm_program_gamut_remap(
//	struct mpc *mpc,
//	unsigned int mpcc_id,
//	const uint16_t *regval,
//	enum mpcc_gamut_remap_id gamut_remap_block_id,
//	enum mpcc_gamut_remap_mode_select mode_select)
//{
//	struct color_matrices_reg gamut_regs;
//	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);
//
//	if (gamut_remap_block_id == MPCC_OGAM_GAMUT_REMAP ||
//		gamut_remap_block_id == MPCC_MCM_FIRST_GAMUT_REMAP ||
//		gamut_remap_block_id == MPCC_MCM_SECOND_GAMUT_REMAP) {
//		mpc_program_gamut_remap(mpc, mpcc_id, regval, gamut_remap_block_id, mode_select);
//		return;
//	}
//	if (gamut_remap_block_id == MPCC_OGAM_GAMUT_REMAP) {
//
//		if (regval == NULL || mode_select == MPCC_GAMUT_REMAP_MODE_SELECT_0) {
//			REG_SET(MPC_RMCM_GAMUT_REMAP_MODE[mpcc_id], 0,
//				MPC_RMCM_GAMUT_REMAP_MODE, mode_select);
//			return;
//		}
//
//		gamut_regs.shifts.csc_c11 = mpc42->mpc_shift->MPCC_GAMUT_REMAP_C11_A;
//		gamut_regs.masks.csc_c11 = mpc42->mpc_mask->MPCC_GAMUT_REMAP_C11_A;
//		gamut_regs.shifts.csc_c12 = mpc42->mpc_shift->MPCC_GAMUT_REMAP_C12_A;
//		gamut_regs.masks.csc_c12 = mpc42->mpc_mask->MPCC_GAMUT_REMAP_C12_A;
//
//		switch (mode_select) {
//		case MPCC_GAMUT_REMAP_MODE_SELECT_1:
//			gamut_regs.csc_c11_c12 = REG(MPC_RMCM_GAMUT_REMAP_C11_C12_A[mpcc_id]);
//			gamut_regs.csc_c33_c34 = REG(MPC_RMCM_GAMUT_REMAP_C33_C34_A[mpcc_id]);
//			break;
//		case MPCC_GAMUT_REMAP_MODE_SELECT_2:
//			gamut_regs.csc_c11_c12 = REG(MPC_RMCM_GAMUT_REMAP_C11_C12_B[mpcc_id]);
//			gamut_regs.csc_c33_c34 = REG(MPC_RMCM_GAMUT_REMAP_C33_C34_B[mpcc_id]);
//			break;
//		default:
//			break;
//		}
//
//		cm_helper_program_color_matrices(
//			mpc->ctx,
//			regval,
//			&gamut_regs);
//
//		//select coefficient set to use, set A (MODE_1) or set B (MODE_2)
//		REG_SET(MPC_RMCM_GAMUT_REMAP_MODE[mpcc_id], 0, MPC_RMCM_GAMUT_REMAP_MODE, mode_select);
//	}
//}

//static bool is_mpc_legacy_gamut_id(enum mpcc_gamut_remap_id gamut_remap_block_id)
//{
//	if (gamut_remap_block_id == MPCC_OGAM_GAMUT_REMAP ||
//		gamut_remap_block_id == MPCC_MCM_FIRST_GAMUT_REMAP ||
//		gamut_remap_block_id == MPCC_MCM_SECOND_GAMUT_REMAP) {
//		return true;
//	}
//	return false;
//}
//static void program_gamut_remap(
//	struct mpc *mpc,
//	unsigned int mpcc_id,
//	const uint16_t *regval,
//	enum mpcc_gamut_remap_id gamut_remap_block_id,
//	enum mpcc_gamut_remap_mode_select mode_select)
//{
//	if (is_mpc_legacy_gamut_id(gamut_remap_block_id))
//		mpc_program_gamut_remap(mpc, mpcc_id, regval, gamut_remap_block_id, mode_select);
//	else
//		rmcm_program_gamut_remap(mpc, mpcc_id, regval, gamut_remap_block_id, mode_select);
//}

//void mpc42_set_gamut_remap(
//	struct mpc *mpc,
//	int mpcc_id,
//	const struct mpc_grph_gamut_adjustment *adjust)
//{
//	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);
//	unsigned int i = 0;
//	uint32_t mode_select = 0;
//
//	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW) {
//		/* Bypass / Disable if type is bypass or hw */
//		program_gamut_remap(mpc, mpcc_id, NULL,
//			adjust->mpcc_gamut_remap_block_id, MPCC_GAMUT_REMAP_MODE_SELECT_0);
//	} else {
//		struct fixed31_32 arr_matrix[12];
//		uint16_t arr_reg_val[12];
//
//		for (i = 0; i < 12; i++)
//			arr_matrix[i] = adjust->temperature_matrix[i];
//
//		convert_float_matrix(arr_reg_val, arr_matrix, 12);
//
//		if (is_mpc_legacy_gamut_id(adjust->mpcc_gamut_remap_block_id))
//			REG_GET(MPCC_GAMUT_REMAP_MODE[mpcc_id],
//				MPCC_GAMUT_REMAP_MODE_CURRENT, &mode_select);
//		else
//			REG_GET(MPC_RMCM_GAMUT_REMAP_MODE[mpcc_id],
//				MPC_RMCM_GAMUT_REMAP_MODE_CURRENT, &mode_select);
//
//		//If current set in use not set A (MODE_1), then use set A, otherwise use set B
//		if (mode_select != MPCC_GAMUT_REMAP_MODE_SELECT_1)
//			mode_select = MPCC_GAMUT_REMAP_MODE_SELECT_1;
//		else
//			mode_select = MPCC_GAMUT_REMAP_MODE_SELECT_2;
//
//		program_gamut_remap(mpc, mpcc_id, arr_reg_val,
//			adjust->mpcc_gamut_remap_block_id, mode_select);
//	}
//}

//static void read_gamut_remap(struct mpc *mpc,
//	int mpcc_id,
//	uint16_t *regval,
//	enum mpcc_gamut_remap_id gamut_remap_block_id,
//	uint32_t *mode_select)
//{
//	struct color_matrices_reg gamut_regs = {0};
//	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);
//
//	if (is_mpc_legacy_gamut_id(gamut_remap_block_id)) {
//		mpc_read_gamut_remap(mpc, mpcc_id, regval, gamut_remap_block_id, mode_select);
//	}
//	if (gamut_remap_block_id == MPCC_RMCM_GAMUT_REMAP) {
//		//current coefficient set in use
//		REG_GET(MPC_RMCM_GAMUT_REMAP_MODE[mpcc_id], MPC_RMCM_GAMUT_REMAP_MODE, mode_select);
//
//		gamut_regs.shifts.csc_c11 = mpc42->mpc_shift->MPCC_GAMUT_REMAP_C11_A;
//		gamut_regs.masks.csc_c11 = mpc42->mpc_mask->MPCC_GAMUT_REMAP_C11_A;
//		gamut_regs.shifts.csc_c12 = mpc42->mpc_shift->MPCC_GAMUT_REMAP_C12_A;
//		gamut_regs.masks.csc_c12 = mpc42->mpc_mask->MPCC_GAMUT_REMAP_C12_A;
//
//		switch (*mode_select) {
//		case MPCC_GAMUT_REMAP_MODE_SELECT_1:
//			gamut_regs.csc_c11_c12 = REG(MPC_RMCM_GAMUT_REMAP_C11_C12_A[mpcc_id]);
//			gamut_regs.csc_c33_c34 = REG(MPC_RMCM_GAMUT_REMAP_C33_C34_A[mpcc_id]);
//			break;
//		case MPCC_GAMUT_REMAP_MODE_SELECT_2:
//			gamut_regs.csc_c11_c12 = REG(MPC_RMCM_GAMUT_REMAP_C11_C12_B[mpcc_id]);
//			gamut_regs.csc_c33_c34 = REG(MPC_RMCM_GAMUT_REMAP_C33_C34_B[mpcc_id]);
//			break;
//		default:
//			break;
//		}
//	}
//
//	if (*mode_select != MPCC_GAMUT_REMAP_MODE_SELECT_0) {
//		cm_helper_read_color_matrices(
//			mpc42->base.ctx,
//			regval,
//			&gamut_regs);
//	}
//}

//void mpc42_get_gamut_remap(struct mpc *mpc,
//	int mpcc_id,
//	struct mpc_grph_gamut_adjustment *adjust)
//{
//	uint16_t arr_reg_val[12] = {0};
//	uint32_t mode_select;
//
//	read_gamut_remap(mpc, mpcc_id, arr_reg_val, adjust->mpcc_gamut_remap_block_id, &mode_select);
//
//	if (mode_select == MPCC_GAMUT_REMAP_MODE_SELECT_0) {
//		adjust->gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;
//		return;
//	}
//
//	adjust->gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
//	convert_hw_matrix(adjust->temperature_matrix,
//		arr_reg_val, ARRAY_SIZE(arr_reg_val));
//}

void mpc42_read_mpcc_state(
		struct mpc *mpc,
		int mpcc_inst,
		struct mpcc_state *s)
{
	struct dcn42_mpc *mpc42 = TO_DCN42_MPC(mpc);

	mpc1_read_mpcc_state(mpc, mpcc_inst, s);

	if (mpcc_inst < 2) {
		/* RMCM 3DLUT Status */
		REG_GET_4(MPC_RMCM_MEM_PWR_CTRL[mpcc_inst], MPC_RMCM_3DLUT_MEM_PWR_FORCE, &s->rmcm_regs.rmcm_3dlut_mem_pwr_force,
				MPC_RMCM_3DLUT_MEM_PWR_DIS, &s->rmcm_regs.rmcm_3dlut_mem_pwr_dis,
				MPC_RMCM_3DLUT_MEM_LOW_PWR_MODE, &s->rmcm_regs.rmcm_3dlut_mem_pwr_mode,
				MPC_RMCM_3DLUT_MEM_PWR_STATE, &s->rmcm_regs.rmcm_3dlut_mem_pwr_state);

		REG_GET_3(MPC_RMCM_3DLUT_MODE[mpcc_inst], MPC_RMCM_3DLUT_SIZE, &s->rmcm_regs.rmcm_3dlut_size,
				MPC_RMCM_3DLUT_MODE, &s->rmcm_regs.rmcm_3dlut_mode,
				MPC_RMCM_3DLUT_MODE_CURRENT, &s->rmcm_regs.rmcm_3dlut_mode_cur);

		REG_GET_4(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[mpcc_inst], MPC_RMCM_3DLUT_READ_SEL, &s->rmcm_regs.rmcm_3dlut_read_sel,
				MPC_RMCM_3DLUT_30BIT_EN, &s->rmcm_regs.rmcm_3dlut_30bit_en,
				MPC_RMCM_3DLUT_WRITE_EN_MASK, &s->rmcm_regs.rmcm_3dlut_wr_en_mask,
				MPC_RMCM_3DLUT_RAM_SEL, &s->rmcm_regs.rmcm_3dlut_ram_sel);

		REG_GET(MPC_RMCM_3DLUT_OUT_NORM_FACTOR[mpcc_inst], MPC_RMCM_3DLUT_OUT_NORM_FACTOR, &s->rmcm_regs.rmcm_3dlut_out_norm_factor);

		REG_GET(MPC_RMCM_3DLUT_FAST_LOAD_SELECT[mpcc_inst], MPC_RMCM_3DLUT_FL_SEL, &s->rmcm_regs.rmcm_3dlut_fl_sel);

		REG_GET_2(MPC_RMCM_3DLUT_OUT_OFFSET_R[mpcc_inst], MPC_RMCM_3DLUT_OUT_OFFSET_R, &s->rmcm_regs.rmcm_3dlut_out_offset_r,
				MPC_RMCM_3DLUT_OUT_SCALE_R, &s->rmcm_regs.rmcm_3dlut_out_scale_r);

		REG_GET_3(MPC_RMCM_3DLUT_FAST_LOAD_STATUS[mpcc_inst], MPC_RMCM_3DLUT_FL_DONE, &s->rmcm_regs.rmcm_3dlut_fl_done,
				MPC_RMCM_3DLUT_FL_SOFT_UNDERFLOW, &s->rmcm_regs.rmcm_3dlut_fl_soft_underflow,
				MPC_RMCM_3DLUT_FL_HARD_UNDERFLOW, &s->rmcm_regs.rmcm_3dlut_fl_hard_underflow);

		/* RMCM Shaper Status */
		REG_GET_4(MPC_RMCM_MEM_PWR_CTRL[mpcc_inst], MPC_RMCM_SHAPER_MEM_PWR_FORCE, &s->rmcm_regs.rmcm_shaper_mem_pwr_force,
				MPC_RMCM_SHAPER_MEM_PWR_DIS, &s->rmcm_regs.rmcm_shaper_mem_pwr_dis,
				MPC_RMCM_SHAPER_MEM_LOW_PWR_MODE, &s->rmcm_regs.rmcm_shaper_mem_pwr_mode,
				MPC_RMCM_SHAPER_MEM_PWR_STATE, &s->rmcm_regs.rmcm_shaper_mem_pwr_state);

		REG_GET_2(MPC_RMCM_SHAPER_CONTROL[mpcc_inst], MPC_RMCM_SHAPER_LUT_MODE, &s->rmcm_regs.rmcm_shaper_lut_mode,
				MPC_RMCM_SHAPER_MODE_CURRENT, &s->rmcm_regs.rmcm_shaper_mode_cur);

		REG_GET_2(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_inst], MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, &s->rmcm_regs.rmcm_shaper_lut_write_en_mask,
				MPC_RMCM_SHAPER_LUT_WRITE_SEL, &s->rmcm_regs.rmcm_shaper_lut_write_sel);

		REG_GET(MPC_RMCM_SHAPER_OFFSET_B[mpcc_inst], MPC_RMCM_SHAPER_OFFSET_B, &s->rmcm_regs.rmcm_shaper_offset_b);

		REG_GET(MPC_RMCM_SHAPER_SCALE_G_B[mpcc_inst], MPC_RMCM_SHAPER_SCALE_B, &s->rmcm_regs.rmcm_shaper_scale_b);

		REG_GET_2(MPC_RMCM_SHAPER_RAMA_START_CNTL_B[mpcc_inst], MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B, &s->rmcm_regs.rmcm_shaper_rama_exp_region_start_b,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, &s->rmcm_regs.rmcm_shaper_rama_exp_region_start_seg_b);

		REG_GET_2(MPC_RMCM_SHAPER_RAMA_END_CNTL_B[mpcc_inst], MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B, &s->rmcm_regs.rmcm_shaper_rama_exp_region_end_b,
				MPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, &s->rmcm_regs.rmcm_shaper_rama_exp_region_end_base_b);

		REG_GET(MPC_RMCM_CNTL[mpcc_inst], MPC_RMCM_CNTL, &s->rmcm_regs.rmcm_cntl);
	}
}

static const struct mpc_funcs dcn42_mpc_funcs = {
	.read_mpcc_state = mpc42_read_mpcc_state,
	.insert_plane = mpc1_insert_plane,
	.remove_mpcc = mpc1_remove_mpcc,
	.mpc_init = mpc32_mpc_init,
	.mpc_init_single_inst = mpc3_mpc_init_single_inst,
	.update_blending = mpc42_update_blending,
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
	.set_dwb_mux = mpc3_set_dwb_mux,
	.disable_dwb_mux = mpc3_disable_dwb_mux,
	.is_dwb_idle = mpc3_is_dwb_idle,
	.set_gamut_remap = mpc401_set_gamut_remap,
	.program_shaper = mpc32_program_shaper,
	.program_3dlut = mpc32_program_3dlut,
	.program_1dlut = mpc32_program_post1dlut,
	.power_on_mpc_mem_pwr = mpc3_power_on_ogam_lut,
	.get_mpc_out_mux = mpc1_get_mpc_out_mux,
	.mpc_read_reg_state = mpc3_read_reg_state,
	.set_bg_color = mpc1_set_bg_color,
	.set_movable_cm_location = mpc401_set_movable_cm_location,
	.update_3dlut_fast_load_select = mpc401_update_3dlut_fast_load_select,
	.get_3dlut_fast_load_status = mpc401_get_3dlut_fast_load_status,
	.populate_lut = mpc401_populate_lut,
	.program_lut_read_write_control = mpc401_program_lut_read_write_control,
	.program_lut_mode = mpc401_program_lut_mode,
	.mcm = {
		.program_lut_read_write_control = mpc42_program_lut_read_write_control,
		.program_3dlut_size = mpc42_program_3dlut_size,
		.program_bias_scale = mpc42_program_3dlut_fl_bias_scale,
		.program_bit_depth = mpc42_program_bit_depth,
		.is_config_supported = mpc42_is_config_supported,
		.populate_lut = mpc42_populate_lut,
	},
	.rmcm = {
		.enable_3dlut_fl = mpc42_enable_3dlut_fl,
		.update_3dlut_fast_load_select = mpc42_update_3dlut_fast_load_select,
		.program_lut_read_write_control = mpc42_program_rmcm_lut_read_write_control,
		.program_lut_mode = mpc42_program_lut_mode,
		.program_3dlut_size = mpc42_program_rmcm_3dlut_size,
		.program_bias_scale = mpc42_program_rmcm_3dlut_fast_load_bias_scale,
		.program_bit_depth = mpc42_program_rmcm_bit_depth,
		.is_config_supported = mpc42_is_rmcm_config_supported,
		.power_on_shaper_3dlut = mpc42_power_on_rmcm_shaper_3dlut,
		.populate_lut = mpc42_populate_rmcm_lut,
		.fl_3dlut_configure = mpc42_set_fl_config,
	},
};

void dcn42_mpc_construct(struct dcn42_mpc *mpc42,
	struct dc_context *ctx,
	const struct dcn42_mpc_registers *mpc_regs,
	const struct dcn42_mpc_shift *mpc_shift,
	const struct dcn42_mpc_mask *mpc_mask,
	int num_mpcc,
	int num_rmu)
{
	int i;

	mpc42->base.ctx = ctx;

	mpc42->base.funcs = &dcn42_mpc_funcs;

	mpc42->mpc_regs = mpc_regs;
	mpc42->mpc_shift = mpc_shift;
	mpc42->mpc_mask = mpc_mask;

	mpc42->mpcc_in_use_mask = 0;
	mpc42->num_mpcc = num_mpcc;
	mpc42->num_rmu = num_rmu;

	for (i = 0; i < MAX_MPCC; i++)
		mpc42_init_mpcc(&mpc42->base.mpcc_array[i], i);
}
