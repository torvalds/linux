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

#include "reg_helper.h"
#include "dcn201_mpc.h"

#define REG(reg)\
	mpc201->mpc_regs->reg

#define CTX \
	mpc201->base.ctx

#define DC_LOGGER \
	mpc201->base.ctx->logger

#undef FN
#define FN(reg_name, field_name) \
	mpc201->mpc_shift->field_name, mpc201->mpc_mask->field_name

static void mpc201_set_out_rate_control(
	struct mpc *mpc,
	int opp_id,
	bool enable,
	bool rate_2x_mode,
	struct mpc_dwb_flow_control *flow_control)
{
	struct dcn201_mpc *mpc201 = TO_DCN201_MPC(mpc);

	REG_UPDATE_2(MUX[opp_id],
			MPC_OUT_RATE_CONTROL_DISABLE, !enable,
			MPC_OUT_RATE_CONTROL, rate_2x_mode);

	if (flow_control)
		REG_UPDATE_3(MUX[opp_id],
			MPC_OUT_FLOW_CONTROL_MODE, flow_control->flow_ctrl_mode,
			MPC_OUT_FLOW_CONTROL_COUNT0, flow_control->flow_ctrl_cnt0,
			MPC_OUT_FLOW_CONTROL_COUNT1, flow_control->flow_ctrl_cnt1);
}

static void mpc201_init_mpcc(struct mpcc *mpcc, int mpcc_inst)
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

const struct mpc_funcs dcn201_mpc_funcs = {
	.read_mpcc_state = mpc1_read_mpcc_state,
	.insert_plane = mpc1_insert_plane,
	.remove_mpcc = mpc1_remove_mpcc,
	.mpc_init = mpc1_mpc_init,
	.mpc_init_single_inst = mpc1_mpc_init_single_inst,
	.update_blending = mpc2_update_blending,
	.cursor_lock = mpc1_cursor_lock,
	.get_mpcc_for_dpp = mpc1_get_mpcc_for_dpp,
	.get_mpcc_for_dpp_from_secondary = NULL,
	.wait_for_idle = mpc2_assert_idle_mpcc,
	.assert_mpcc_idle_before_connect = mpc2_assert_mpcc_idle_before_connect,
	.init_mpcc_list_from_hw = mpc1_init_mpcc_list_from_hw,
	.set_denorm = mpc2_set_denorm,
	.set_denorm_clamp = mpc2_set_denorm_clamp,
	.set_output_csc = mpc2_set_output_csc,
	.set_ocsc_default = mpc2_set_ocsc_default,
	.set_output_gamma = mpc2_set_output_gamma,
	.set_out_rate_control = mpc201_set_out_rate_control,
	.power_on_mpc_mem_pwr = mpc20_power_on_ogam_lut,
	.get_mpc_out_mux = mpc1_get_mpc_out_mux,
	.set_bg_color = mpc1_set_bg_color,
};

void dcn201_mpc_construct(struct dcn201_mpc *mpc201,
	struct dc_context *ctx,
	const struct dcn201_mpc_registers *mpc_regs,
	const struct dcn201_mpc_shift *mpc_shift,
	const struct dcn201_mpc_mask *mpc_mask,
	int num_mpcc)
{
	int i;

	mpc201->base.ctx = ctx;

	mpc201->base.funcs = &dcn201_mpc_funcs;

	mpc201->mpc_regs = mpc_regs;
	mpc201->mpc_shift = mpc_shift;
	mpc201->mpc_mask = mpc_mask;

	mpc201->mpcc_in_use_mask = 0;
	mpc201->num_mpcc = num_mpcc;

	for (i = 0; i < MAX_MPCC; i++)
		mpc201_init_mpcc(&mpc201->base.mpcc_array[i], i);
}
