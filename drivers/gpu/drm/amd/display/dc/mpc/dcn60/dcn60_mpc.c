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

static void mpc60_program_lut_read_write_control(struct mpc *mpc,
		const enum MCM_LUT_ID id,
		const bool lut_bank_a,
		const unsigned int bit_depth,
		const int mpcc_id)
{
	struct dcn60_mpc *mpc60 = TO_DCN60_MPC(mpc);

	switch (id) {
	case MCM_LUT_3DLUT:
		mpc32_select_3dlut_ram_mask(mpc, 0xf, mpcc_id);
		REG_UPDATE(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
				MPCC_MCM_3DLUT_30BIT_EN, (bit_depth == 10) ? 1 : 0);
		break;
	case MCM_LUT_SHAPER:
		mpc32_configure_shaper_lut(mpc, lut_bank_a, mpcc_id);
		break;
	case MCM_LUT_1DLUT:
		mpc32_configure_post1dlut(mpc, lut_bank_a, mpcc_id);
		break;
	}
}

static uint32_t mpc60_cm_lut_size_to_3dlut_size(const enum dc_cm_lut_size cm_size)
{
	uint32_t size = 0;

	switch (cm_size) {
	case CM_LUT_SIZE_999:
		size = 1;
		break;
	case CM_LUT_SIZE_171717:
		size = 0;
		break;
	default:
		/* invalid LUT size for MCM */
		ASSERT(false);
		size = 0;
		break;
	}

	return size;
}

static void mpc60_program_lut_mode(
		struct mpc *mpc,
		const enum MCM_LUT_ID id,
		const bool enable,
		const bool lut_bank_a,
		const enum dc_cm_lut_size size,
		const int mpcc_id)
{
	uint32_t lut_size;
	struct dcn60_mpc *mpc60 = TO_DCN60_MPC(mpc);

	switch (id) {
	case MCM_LUT_3DLUT:
		if (enable) {
			lut_size = mpc60_cm_lut_size_to_3dlut_size(size);
			REG_UPDATE_2(MPCC_MCM_3DLUT_MODE[mpcc_id],
					MPCC_MCM_3DLUT_MODE, 1,
					MPCC_MCM_3DLUT_SIZE, lut_size);
		} else {
			if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
				mpc32_power_on_shaper_3dlut(mpc, mpcc_id, false);
			REG_UPDATE(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_MODE, 0);
		}
		break;
	case MCM_LUT_SHAPER:
		if (enable) {
			REG_UPDATE(MPCC_MCM_SHAPER_CONTROL[mpcc_id], MPCC_MCM_SHAPER_LUT_MODE, lut_bank_a ? 1 : 2);
		} else {
			if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
				mpc32_power_on_shaper_3dlut(mpc, mpcc_id, false);
			REG_UPDATE(MPCC_MCM_SHAPER_CONTROL[mpcc_id], MPCC_MCM_SHAPER_LUT_MODE, 0);
		}
		break;
	case MCM_LUT_1DLUT:
		if (enable) {
			REG_UPDATE(MPCC_MCM_1DLUT_CONTROL[mpcc_id],
					MPCC_MCM_1DLUT_MODE, 2);
		} else {
			if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
				mpc32_power_on_blnd_lut(mpc, mpcc_id, false);
			REG_UPDATE(MPCC_MCM_1DLUT_CONTROL[mpcc_id],
					MPCC_MCM_1DLUT_MODE, 0);
		}
		REG_UPDATE(MPCC_MCM_1DLUT_CONTROL[mpcc_id],
				MPCC_MCM_1DLUT_SELECT, lut_bank_a ? 0 : 1);
		break;
	}
}

static bool mpc60_program_3dlut(
	struct mpc *mpc,
	const struct tetrahedral_params *params,
	int mpcc_id)
{
	union mcm_lut_params lut_params = { 0 };
	lut_params.lut3d = params;

	mpc60_program_lut_read_write_control(mpc,
			MCM_LUT_3DLUT,
			true,
			params->use_12bits ? 12 : 10,
			mpcc_id);
	mpc401_populate_lut(mpc, MCM_LUT_3DLUT, &lut_params, true, mpcc_id);
	mpc60_program_lut_mode(mpc,
		MCM_LUT_3DLUT,
		true,
		true,
		params->use_tetrahedral_9 ? CM_LUT_SIZE_999 : CM_LUT_SIZE_171717,
		mpcc_id);

	return true;
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
	.program_lut_mode = mpc60_program_lut_mode,
	.get_lut_mode = mpc401_get_lut_mode,
	.rmcm = {
		.enable_3dlut_fl = mpc42_enable_3dlut_fl,
		.update_3dlut_fast_load_select = mpc42_update_3dlut_fast_load_select,
		.program_lut_read_write_control = mpc60_program_rmcm_lut_read_write_control,
		.program_lut_mode = mpc42_program_lut_mode,
		.program_3dlut_size = mpc42_program_rmcm_3dlut_size,
		.program_bias_scale = mpc42_program_rmcm_3dlut_fast_load_bias_scale,
		.program_bit_depth = mpc42_program_rmcm_bit_depth,
		.power_on_shaper_3dlut = mpc42_power_on_rmcm_shaper_3dlut,
		.populate_lut = mpc42_populate_rmcm_lut,
		.get_3dlut_mode = mpc42_get_rmcm_3dlut_mode,
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

