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


void mpc42_init_mpcc(struct mpcc *mpcc, int mpcc_inst)
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



void mpc42_read_mpcc_state(
		struct mpc *mpc,
		int mpcc_inst,
		struct mpcc_state *s)
{
	mpc1_read_mpcc_state(mpc, mpcc_inst, s);
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
	.get_lut_mode = mpc401_get_lut_mode,
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
	mpc42->base.inst = 0;

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
