// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#include "reg_helper.h"
#include "dc.h"
#include "dcn60_mpc.h"
#include "dcn10/dcn10_cm_common.h"
#include "basics/conversion.h"
#include "mpc.h"

#define REG(reg)\
	mpc60->mpc_regs->reg

#define CTX \
	mpc60->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mpc60->mpc_shift->field_name, mpc60->mpc_mask->field_name

/*
 * Insert DPP into MPC tree based on specified blending position.
 * Only used for planes that are part of blending chain for OPP output
 *
 * Parameters:
 * [in/out] mpc		- MPC context.
 * [in/out] tree	- MPC tree structure that plane will be added to.
 * [in]	blnd_cfg	- MPCC blending configuration for the new blending layer.
 * [in]	sm_cfg		- MPCC stereo mix configuration for the new blending layer.
 *			  stereo mix must disable for the very bottom layer of the tree config.
 * [in]	insert_above_mpcc - Insert new plane above this MPCC.  If NULL, insert as bottom plane.
 * [in]	dpp_id		- DPP instance for the plane to be added.
 * [in]	mpcc_id		- The MPCC physical instance to use for blending.
 *
 * Return:  struct mpcc* - MPCC that was added.
 */
static struct mpcc *mpc60_insert_plane(
	struct mpc *mpc,
	struct mpc_tree *tree,
	struct mpcc_blnd_cfg *blnd_cfg,
	struct mpcc_sm_cfg *sm_cfg,
	struct mpcc *insert_above_mpcc,
	int dpp_id,
	int mpcc_id)
{
	(void)sm_cfg;
	struct dcn60_mpc *mpc60 = TO_DCN60_MPC(mpc);
	struct mpcc *new_mpcc = NULL;

	/* sanity check parameters */
	ASSERT(mpcc_id < mpc60->num_mpcc);
	ASSERT(!(mpc60->mpcc_in_use_mask & 1 << mpcc_id));

	if (insert_above_mpcc) {
		/* check insert_above_mpcc exist in tree->opp_list */
		struct mpcc *temp_mpcc = tree->opp_list;

		if (temp_mpcc != insert_above_mpcc)
			while (temp_mpcc && temp_mpcc->mpcc_bot != insert_above_mpcc)
				temp_mpcc = temp_mpcc->mpcc_bot;
		if (temp_mpcc == NULL)
			return NULL;
	}

	/* Get and update MPCC struct parameters */
	new_mpcc = mpc1_get_mpcc(mpc, mpcc_id);
	new_mpcc->dpp_id = dpp_id;

	/* program mux and MPCC_MODE */
	if (insert_above_mpcc) {
		new_mpcc->mpcc_bot = insert_above_mpcc;
		REG_SET(MPCC_BOT_SEL[mpcc_id], 0, MPCC_BOT_SEL, insert_above_mpcc->mpcc_id);
		REG_UPDATE(MPCC_CONTROL[mpcc_id], MPCC_MODE, MPCC_BLEND_MODE_TOP_BOT_BLENDING);
	} else {
		new_mpcc->mpcc_bot = NULL;
		REG_SET(MPCC_BOT_SEL[mpcc_id], 0, MPCC_BOT_SEL, 0xf);
		REG_UPDATE(MPCC_CONTROL[mpcc_id], MPCC_MODE, MPCC_BLEND_MODE_TOP_LAYER_ONLY);
	}
	REG_SET(MPCC_TOP_SEL[mpcc_id], 0, MPCC_TOP_SEL, dpp_id);
	REG_SET(MPCC_OPP_ID[mpcc_id], 0, MPCC_OPP_ID, tree->opp_id);

	/* Configure VUPDATE lock set for this MPCC to map to the OPP */
	REG_SET(MPCC_UPDATE_LOCK_SEL[mpcc_id], 0, MPCC_UPDATE_LOCK_SEL, tree->opp_id);

	/* update mpc tree mux setting */
	if (tree->opp_list == insert_above_mpcc) {
		/* insert the toppest mpcc */
		tree->opp_list = new_mpcc;
		REG_UPDATE(MUX[tree->opp_id], MPC_OUT_MUX, mpcc_id);
	} else {
		/* find insert position */
		struct mpcc *temp_mpcc = tree->opp_list;

		while (temp_mpcc && temp_mpcc->mpcc_bot != insert_above_mpcc)
			temp_mpcc = temp_mpcc->mpcc_bot;
		if (temp_mpcc && temp_mpcc->mpcc_bot == insert_above_mpcc) {
			REG_SET(MPCC_BOT_SEL[temp_mpcc->mpcc_id], 0, MPCC_BOT_SEL, mpcc_id);
			temp_mpcc->mpcc_bot = new_mpcc;
			if (!insert_above_mpcc)
				REG_UPDATE(MPCC_CONTROL[temp_mpcc->mpcc_id],
					MPCC_MODE, MPCC_BLEND_MODE_TOP_BOT_BLENDING);
		}
	}

	/* update the blending configuration */
	mpc->funcs->update_blending(mpc, blnd_cfg, mpcc_id);

	/* mark this mpcc as in use */
	mpc60->mpcc_in_use_mask |= 1 << mpcc_id;

	return new_mpcc;
}

void mpc60_program_rmcm_lut_read_write_control(struct mpc *mpc, const enum MCM_LUT_ID id,
	bool lut_bank_a, bool enabled, int mpcc_id)
{
	struct dcn60_mpc *mpc60 = TO_DCN60_MPC(mpc);

	switch (id) {
	case MCM_LUT_3DLUT:
		REG_UPDATE(MPC_RMCM_3DLUT_MODE[mpcc_id], MPC_RMCM_3DLUT_MODE,
			(!enabled) ? 0 :
			(lut_bank_a) ? 1 : 2);
		break;

	case MCM_LUT_SHAPER:
		REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, 7);

		REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[mpcc_id],
			MPC_RMCM_SHAPER_LUT_WRITE_SEL,
			lut_bank_a == true ? 0 : 1);

		REG_SET(MPC_RMCM_SHAPER_LUT_INDEX[mpcc_id], 0,
			MPC_RMCM_SHAPER_LUT_INDEX, 0);
		break;
	default:
		break;
	}
}

void mpc60_select_3dlut_ram(
	struct mpc *mpc,
	enum dc_lut_mode mode,
	bool is_color_channel_12bits,
	uint32_t mpcc_id)
{
	(void)mode;
	struct dcn60_mpc *mpc60 = TO_DCN60_MPC(mpc);

	REG_UPDATE(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
		MPCC_MCM_3DLUT_30BIT_EN, is_color_channel_12bits == true ? 0 : 1);
}

static enum dc_lut_mode get3dlut_config(
	struct mpc *mpc,
	bool *is_17x17x17,
	bool *is_12bits_color_channel,
	int mpcc_id)
{
	uint32_t i_mode, i_enable_10bits, lut_size;
	enum dc_lut_mode mode;
	struct dcn60_mpc *mpc60 = TO_DCN60_MPC(mpc);

	REG_GET(MPCC_MCM_3DLUT_MODE[mpcc_id],
		MPCC_MCM_3DLUT_MODE_CURRENT, &i_mode);

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

bool mpc60_program_3dlut(
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
		lut_size0 = sizeof(params->tetrahedral_17.lut0) /
			sizeof(params->tetrahedral_17.lut0[0]);
		lut_size = sizeof(params->tetrahedral_17.lut1) /
			sizeof(params->tetrahedral_17.lut1[0]);
	} else {
		lut0 = params->tetrahedral_9.lut0;
		lut1 = params->tetrahedral_9.lut1;
		lut2 = params->tetrahedral_9.lut2;
		lut3 = params->tetrahedral_9.lut3;
		lut_size0 = sizeof(params->tetrahedral_9.lut0) /
			sizeof(params->tetrahedral_9.lut0[0]);
		lut_size = sizeof(params->tetrahedral_9.lut1) /
			sizeof(params->tetrahedral_9.lut1[0]);
	}

	mpc60_select_3dlut_ram(mpc, mode,
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

void mpc60_program_lut_read_write_control(struct mpc *mpc, const enum MCM_LUT_ID id, bool lut_bank_a, int mpcc_id)
{

	switch (id) {
	case MCM_LUT_3DLUT:
		mpc32_select_3dlut_ram_mask(mpc, 0xf, mpcc_id);
		break;
	case MCM_LUT_SHAPER:
		mpc32_configure_shaper_lut(mpc, lut_bank_a, mpcc_id);
		break;
	case MCM_LUT_1DLUT:
		mpc32_configure_post1dlut(mpc, lut_bank_a, mpcc_id);
		break;
	}
}

static const struct mpc_funcs dcn60_mpc_funcs = {
	.read_mpcc_state = mpc1_read_mpcc_state,
	.insert_plane = mpc60_insert_plane,
	.remove_mpcc = mpc1_remove_mpcc,
	.mpc_init = mpc32_mpc_init,
	.mpc_init_single_inst = mpc3_mpc_init_single_inst,
	.update_blending = mpc42_update_blending,
	.cursor_lock = mpc1_cursor_lock,
	.get_mpcc_for_dpp = mpc1_get_mpcc_for_dpp,
	.wait_for_idle = mpc2_assert_idle_mpcc,
	.assert_mpcc_idle_before_connect = mpc2_assert_mpcc_idle_before_connect,
	.init_mpcc_list_from_hw = mpc1_init_mpcc_list_from_hw,
	.set_denorm = mpc3_set_denorm,
	.set_denorm_clamp = mpc3_set_denorm_clamp,
	.set_output_csc = mpc3_set_output_csc,
	.set_ocsc_default = mpc3_set_ocsc_default,
	.set_output_gamma = mpc3_set_output_gamma,
	.set_gamut_remap = mpc401_set_gamut_remap,
	.program_shaper = mpc32_program_shaper,
	.program_3dlut = mpc60_program_3dlut,
	.program_1dlut = mpc32_program_post1dlut,
	.power_on_mpc_mem_pwr = mpc3_power_on_ogam_lut,
	.get_mpc_out_mux = mpc1_get_mpc_out_mux,
	.mpc_read_reg_state = mpc3_read_reg_state,
	.set_bg_color = mpc1_set_bg_color,
	.set_movable_cm_location = mpc401_set_movable_cm_location,
	.update_3dlut_fast_load_select = mpc401_update_3dlut_fast_load_select,
	.get_3dlut_fast_load_status = mpc401_get_3dlut_fast_load_status,
	.populate_lut = mpc401_populate_lut,
	.program_lut_read_write_control = mpc60_program_lut_read_write_control,
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
		.program_lut_read_write_control = mpc60_program_rmcm_lut_read_write_control,
		.program_lut_mode = mpc42_program_lut_mode,
		.program_3dlut_size = mpc42_program_rmcm_3dlut_size,
		.program_bias_scale = mpc42_program_rmcm_3dlut_fast_load_bias_scale,
		.program_bit_depth = mpc42_program_rmcm_bit_depth,
		.is_config_supported = mpc42_is_rmcm_config_supported,
		.power_on_shaper_3dlut = mpc42_power_on_rmcm_shaper_3dlut,
		.populate_lut = mpc42_populate_rmcm_lut,
	},
};

void dcn60_mpc_construct(struct dcn60_mpc *mpc60,
	struct dc_context *ctx,
	const struct dcn60_mpc_registers *mpc_regs,
	const struct dcn60_mpc_shift *mpc_shift,
	const struct dcn60_mpc_mask *mpc_mask,
	int num_mpcc,
	int num_rmu)
{
	int i;

	mpc60->base.ctx = ctx;

	mpc60->base.funcs = &dcn60_mpc_funcs;

	mpc60->mpc_regs = mpc_regs;
	mpc60->mpc_shift = mpc_shift;
	mpc60->mpc_mask = mpc_mask;

	mpc60->mpcc_in_use_mask = 0;
	mpc60->num_mpcc = num_mpcc;
	mpc60->num_rmu = num_rmu;

	for (i = 0; i < MAX_MPCC; i++)
		mpc42_init_mpcc(&mpc60->base.mpcc_array[i], i);
}

