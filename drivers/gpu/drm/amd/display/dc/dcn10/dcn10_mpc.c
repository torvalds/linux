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
#include "dcn10_mpc.h"

#define REG(reg)\
	mpcc10->mpcc_regs->reg

#define CTX \
	mpcc10->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mpcc10->mpcc_shift->field_name, mpcc10->mpcc_mask->field_name

#define MODE_TOP_ONLY 1
#define MODE_BLEND 3
#define BLND_PP_ALPHA 0
#define BLND_GLOBAL_ALPHA 2


void dcn10_mpcc_set_bg_color(
		struct mpcc *mpcc,
		struct tg_color *bg_color)
{
	struct dcn10_mpcc *mpcc10 = TO_DCN10_MPCC(mpcc);
	/* mpc color is 12 bit.  tg_color is 10 bit */
	/* todo: might want to use 16 bit to represent color and have each
	 * hw block translate to correct color depth.
	 */
	uint32_t bg_r_cr = bg_color->color_r_cr << 2;
	uint32_t bg_g_y = bg_color->color_g_y << 2;
	uint32_t bg_b_cb = bg_color->color_b_cb << 2;

	REG_SET(MPCC_BG_R_CR, 0,
			MPCC_BG_R_CR, bg_r_cr);
	REG_SET(MPCC_BG_G_Y, 0,
			MPCC_BG_G_Y, bg_g_y);
	REG_SET(MPCC_BG_B_CB, 0,
			MPCC_BG_B_CB, bg_b_cb);
}

static void set_output_mux(struct dcn10_mpcc *mpcc10, int opp_id, int mpcc_id)
{
	ASSERT(mpcc10->opp_id == 0xf || opp_id == mpcc10->opp_id);
	mpcc10->opp_id = opp_id;
	REG_UPDATE(OPP_PIPE_CONTROL[opp_id], OPP_PIPE_CLOCK_EN, 1);
	REG_SET(MUX[opp_id], 0, MPC_OUT_MUX, mpcc_id);
}

static void reset_output_mux(struct dcn10_mpcc *mpcc10)
{
	REG_SET(MUX[mpcc10->opp_id], 0, MPC_OUT_MUX, 0xf);
	REG_UPDATE(OPP_PIPE_CONTROL[mpcc10->opp_id], OPP_PIPE_CLOCK_EN, 0);
	mpcc10->opp_id = 0xf;
}

static void dcn10_mpcc_set(struct mpcc *mpcc, struct mpcc_cfg *cfg)
{
	struct dcn10_mpcc *mpcc10 = TO_DCN10_MPCC(mpcc);
	int alpha_blnd_mode = cfg->per_pixel_alpha ?
			BLND_PP_ALPHA : BLND_GLOBAL_ALPHA;
	int mpcc_mode = cfg->bot_mpcc_id != 0xf ?
				MODE_BLEND : MODE_TOP_ONLY;

	REG_SET(MPCC_OPP_ID, 0,
		MPCC_OPP_ID, cfg->opp_id);

	REG_SET(MPCC_TOP_SEL, 0,
		MPCC_TOP_SEL, cfg->top_dpp_id);

	REG_SET(MPCC_BOT_SEL, 0,
		MPCC_BOT_SEL, cfg->bot_mpcc_id);

	REG_SET_4(MPCC_CONTROL, 0xffffffff,
		MPCC_MODE, mpcc_mode,
		MPCC_ALPHA_BLND_MODE, alpha_blnd_mode,
		MPCC_ALPHA_MULTIPLIED_MODE, cfg->pre_multiplied_alpha,
		MPCC_BLND_ACTIVE_OVERLAP_ONLY, cfg->top_of_tree);

	if (cfg->top_of_tree) {
		if (cfg->opp_id != 0xf)
			set_output_mux(mpcc10, cfg->opp_id, mpcc->inst);
		else
			reset_output_mux(mpcc10);
	}
}

static void dcn10_mpcc_wait_idle(struct mpcc *mpcc)
{
	struct dcn10_mpcc *mpcc10 = TO_DCN10_MPCC(mpcc);

	REG_WAIT(MPCC_STATUS, MPCC_IDLE, 1, 1000, 1000);
}


const struct mpcc_funcs dcn10_mpcc_funcs = {
		.set = dcn10_mpcc_set,
		.wait_for_idle = dcn10_mpcc_wait_idle,
		.set_bg_color = dcn10_mpcc_set_bg_color,
};

void dcn10_mpcc_construct(struct dcn10_mpcc *mpcc10,
	struct dc_context *ctx,
	const struct dcn_mpcc_registers *mpcc_regs,
	const struct dcn_mpcc_shift *mpcc_shift,
	const struct dcn_mpcc_mask *mpcc_mask,
	int inst)
{
	mpcc10->base.ctx = ctx;

	mpcc10->base.inst = inst;
	mpcc10->base.funcs = &dcn10_mpcc_funcs;

	mpcc10->mpcc_regs = mpcc_regs;
	mpcc10->mpcc_shift = mpcc_shift;
	mpcc10->mpcc_mask = mpcc_mask;

	mpcc10->opp_id = inst;
}
