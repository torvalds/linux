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
#include "dc.h"
#include "mem_input.h"

#define REG(reg)\
	mpc10->mpc_regs->reg

#define CTX \
	mpc10->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mpc10->mpc_shift->field_name, mpc10->mpc_mask->field_name

#define MODE_TOP_ONLY 1
#define MODE_BLEND 3
#define BLND_PP_ALPHA 0
#define BLND_GLOBAL_ALPHA 2


static void mpc10_set_bg_color(
		struct dcn10_mpc *mpc10,
		struct tg_color *bg_color,
		int id)
{
	/* mpc color is 12 bit.  tg_color is 10 bit */
	/* todo: might want to use 16 bit to represent color and have each
	 * hw block translate to correct color depth.
	 */
	uint32_t bg_r_cr = bg_color->color_r_cr << 2;
	uint32_t bg_g_y = bg_color->color_g_y << 2;
	uint32_t bg_b_cb = bg_color->color_b_cb << 2;

	REG_SET(MPCC_BG_R_CR[id], 0,
			MPCC_BG_R_CR, bg_r_cr);
	REG_SET(MPCC_BG_G_Y[id], 0,
			MPCC_BG_G_Y, bg_g_y);
	REG_SET(MPCC_BG_B_CB[id], 0,
			MPCC_BG_B_CB, bg_b_cb);
}

static void mpc10_assert_idle_mpcc(struct mpc *mpc, int id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);

	ASSERT(!(mpc10->mpcc_in_use_mask & 1 << id));
	REG_WAIT(MPCC_STATUS[id],
			MPCC_IDLE, 1,
			1, 100000);
}

static int mpc10_get_idle_mpcc_id(struct dcn10_mpc *mpc10)
{
	int i;
	int last_free_mpcc_id = -1;

	for (i = 0; i < mpc10->num_mpcc; i++) {
		uint32_t is_idle = 0;

		if (mpc10->mpcc_in_use_mask & 1 << i)
			continue;

		last_free_mpcc_id = i;
		REG_GET(MPCC_STATUS[i], MPCC_IDLE, &is_idle);
		if (is_idle)
			return i;
	}

	/* This assert should never trigger, we have mpcc leak if it does */
	ASSERT(last_free_mpcc_id != -1);

	mpc10_assert_idle_mpcc(&mpc10->base, last_free_mpcc_id);
	return last_free_mpcc_id;
}

static void mpc10_assert_mpcc_idle_before_connect(struct dcn10_mpc *mpc10, int id)
{
	unsigned int top_sel, mpc_busy, mpc_idle;

	REG_GET(MPCC_TOP_SEL[id],
			MPCC_TOP_SEL, &top_sel);

	if (top_sel == 0xf) {
		REG_GET_2(MPCC_STATUS[id],
				MPCC_BUSY, &mpc_busy,
				MPCC_IDLE, &mpc_idle);

		ASSERT(mpc_busy == 0);
		ASSERT(mpc_idle == 1);
	}
}

static void mpc10_mpcc_remove(
		struct mpc *mpc,
		struct output_pixel_processor *opp,
		int dpp_id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);
	int mpcc_id, z_idx;

	for (z_idx = 0; z_idx < opp->mpc_tree.num_pipes; z_idx++)
		if (opp->mpc_tree.dpp[z_idx] == dpp_id)
			break;
	if (z_idx == opp->mpc_tree.num_pipes) {
		ASSERT(0);
		return;
	}
	mpcc_id = opp->mpc_tree.mpcc[z_idx];

	REG_SET(MPCC_OPP_ID[mpcc_id], 0,
			MPCC_OPP_ID, 0xf);
	REG_SET(MPCC_TOP_SEL[mpcc_id], 0,
			MPCC_TOP_SEL, 0xf);
	REG_SET(MPCC_BOT_SEL[mpcc_id], 0,
			MPCC_BOT_SEL, 0xf);

	if (z_idx > 0) {
		int top_mpcc_id = opp->mpc_tree.mpcc[z_idx - 1];

		if (z_idx + 1 < opp->mpc_tree.num_pipes)
			REG_SET(MPCC_BOT_SEL[top_mpcc_id], 0,
					MPCC_BOT_SEL, opp->mpc_tree.mpcc[z_idx + 1]);
		else {
			REG_SET(MPCC_BOT_SEL[top_mpcc_id], 0,
					MPCC_BOT_SEL, 0xf);
			REG_UPDATE(MPCC_CONTROL[top_mpcc_id],
					MPCC_MODE, MODE_TOP_ONLY);
		}
	} else if (opp->mpc_tree.num_pipes > 1)
		REG_SET(MUX[opp->inst], 0,
				MPC_OUT_MUX, opp->mpc_tree.mpcc[z_idx + 1]);
	else
		REG_SET(MUX[opp->inst], 0, MPC_OUT_MUX, 0xf);

	mpc10->mpcc_in_use_mask &= ~(1 << mpcc_id);
	opp->mpc_tree.num_pipes--;
	for (; z_idx < opp->mpc_tree.num_pipes; z_idx++) {
		opp->mpc_tree.dpp[z_idx] = opp->mpc_tree.dpp[z_idx + 1];
		opp->mpc_tree.mpcc[z_idx] = opp->mpc_tree.mpcc[z_idx + 1];
	}
	opp->mpc_tree.dpp[opp->mpc_tree.num_pipes] = 0xdeadbeef;
	opp->mpc_tree.mpcc[opp->mpc_tree.num_pipes] = 0xdeadbeef;
}

static void mpc10_mpcc_add(struct mpc *mpc, struct mpcc_cfg *cfg)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);
	int alpha_blnd_mode = cfg->per_pixel_alpha ?
			BLND_PP_ALPHA : BLND_GLOBAL_ALPHA;
	int mpcc_mode = MODE_TOP_ONLY;
	int mpcc_id, z_idx;

	ASSERT(cfg->z_index < mpc10->num_mpcc);

	for (z_idx = 0; z_idx < cfg->opp->mpc_tree.num_pipes; z_idx++)
		if (cfg->opp->mpc_tree.dpp[z_idx] == cfg->mi->inst)
			break;
	if (z_idx == cfg->opp->mpc_tree.num_pipes) {
		ASSERT(cfg->z_index <= cfg->opp->mpc_tree.num_pipes);
		mpcc_id = mpc10_get_idle_mpcc_id(mpc10);
		/*todo: remove hack*/
		mpcc_id = cfg->mi->inst;
		ASSERT(!(mpc10->mpcc_in_use_mask & 1 << mpcc_id));

		if (mpc->ctx->dc->debug.sanity_checks)
			mpc10_assert_mpcc_idle_before_connect(mpc10, mpcc_id);
	} else {
		ASSERT(cfg->z_index < cfg->opp->mpc_tree.num_pipes);
		mpcc_id = cfg->opp->mpc_tree.mpcc[z_idx];
		mpc10_mpcc_remove(mpc, cfg->opp, cfg->mi->inst);
	}

	REG_SET(MPCC_OPP_ID[mpcc_id], 0,
			MPCC_OPP_ID, cfg->opp->inst);

	REG_SET(MPCC_TOP_SEL[mpcc_id], 0,
			MPCC_TOP_SEL, cfg->mi->inst);

	if (cfg->z_index > 0) {
		int top_mpcc_id = cfg->opp->mpc_tree.mpcc[cfg->z_index - 1];

		REG_SET(MPCC_BOT_SEL[top_mpcc_id], 0,
				MPCC_BOT_SEL, mpcc_id);
		REG_UPDATE(MPCC_CONTROL[top_mpcc_id],
				MPCC_MODE, MODE_BLEND);
	} else
		REG_SET(MUX[cfg->opp->inst], 0, MPC_OUT_MUX, mpcc_id);

	if (cfg->z_index < cfg->opp->mpc_tree.num_pipes) {
		int bot_mpcc_id = cfg->opp->mpc_tree.mpcc[cfg->z_index];

		REG_SET(MPCC_BOT_SEL[mpcc_id], 0,
				MPCC_BOT_SEL, bot_mpcc_id);
		mpcc_mode = MODE_BLEND;
	}

	REG_SET_4(MPCC_CONTROL[mpcc_id], 0xffffffff,
		MPCC_MODE, mpcc_mode,
		MPCC_ALPHA_BLND_MODE, alpha_blnd_mode,
		MPCC_ALPHA_MULTIPLIED_MODE, cfg->pre_multiplied_alpha,
		MPCC_BLND_ACTIVE_OVERLAP_ONLY, false);

	mpc10_set_bg_color(mpc10, &cfg->black_color, mpcc_id);

	mpc10->mpcc_in_use_mask |= 1 << mpcc_id;
	for (z_idx = cfg->opp->mpc_tree.num_pipes; z_idx > cfg->z_index; z_idx--) {
		cfg->opp->mpc_tree.dpp[z_idx] = cfg->opp->mpc_tree.dpp[z_idx - 1];
		cfg->opp->mpc_tree.mpcc[z_idx] = cfg->opp->mpc_tree.mpcc[z_idx - 1];
	}
	cfg->opp->mpc_tree.dpp[cfg->z_index] = cfg->mi->inst;
	cfg->opp->mpc_tree.mpcc[cfg->z_index] = mpcc_id;
	cfg->opp->mpc_tree.num_pipes++;
	cfg->mi->opp_id = cfg->opp->inst;
	cfg->mi->mpcc_id = mpcc_id;
}

const struct mpc_funcs dcn10_mpc_funcs = {
		.add = mpc10_mpcc_add,
		.remove = mpc10_mpcc_remove,
		.wait_for_idle = mpc10_assert_idle_mpcc
};

void dcn10_mpc_construct(struct dcn10_mpc *mpc10,
	struct dc_context *ctx,
	const struct dcn_mpc_registers *mpc_regs,
	const struct dcn_mpc_shift *mpc_shift,
	const struct dcn_mpc_mask *mpc_mask,
	int num_mpcc)
{
	mpc10->base.ctx = ctx;

	mpc10->base.funcs = &dcn10_mpc_funcs;

	mpc10->mpc_regs = mpc_regs;
	mpc10->mpc_shift = mpc_shift;
	mpc10->mpc_mask = mpc_mask;

	mpc10->mpcc_in_use_mask = 0;
	mpc10->num_mpcc = num_mpcc;
}
