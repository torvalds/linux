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
	mpc10->mpc_regs->reg

#define CTX \
	mpc10->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mpc10->mpc_shift->field_name, mpc10->mpc_mask->field_name


void mpc1_set_bg_color(struct mpc *mpc,
		struct tg_color *bg_color,
		int mpcc_id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);

	/* mpc color is 12 bit.  tg_color is 10 bit */
	/* todo: might want to use 16 bit to represent color and have each
	 * hw block translate to correct color depth.
	 */
	uint32_t bg_r_cr = bg_color->color_r_cr << 2;
	uint32_t bg_g_y = bg_color->color_g_y << 2;
	uint32_t bg_b_cb = bg_color->color_b_cb << 2;

	REG_SET(MPCC_BG_R_CR[mpcc_id], 0,
			MPCC_BG_R_CR, bg_r_cr);
	REG_SET(MPCC_BG_G_Y[mpcc_id], 0,
			MPCC_BG_G_Y, bg_g_y);
	REG_SET(MPCC_BG_B_CB[mpcc_id], 0,
			MPCC_BG_B_CB, bg_b_cb);
}

static void mpc1_update_blending(
	struct mpc *mpc,
	struct mpcc_blnd_cfg *blnd_cfg,
	int mpcc_id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);

	REG_UPDATE_5(MPCC_CONTROL[mpcc_id],
			MPCC_ALPHA_BLND_MODE,		blnd_cfg->alpha_mode,
			MPCC_ALPHA_MULTIPLIED_MODE,	blnd_cfg->pre_multiplied_alpha,
			MPCC_BLND_ACTIVE_OVERLAP_ONLY,	blnd_cfg->overlap_only,
			MPCC_GLOBAL_ALPHA,		blnd_cfg->global_alpha,
			MPCC_GLOBAL_GAIN,		blnd_cfg->global_gain);

	mpc1_set_bg_color(mpc, &blnd_cfg->black_color, mpcc_id);
}

void mpc1_update_stereo_mix(
	struct mpc *mpc,
	struct mpcc_sm_cfg *sm_cfg,
	int mpcc_id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);

	REG_UPDATE_6(MPCC_SM_CONTROL[mpcc_id],
			MPCC_SM_EN,			sm_cfg->enable,
			MPCC_SM_MODE,			sm_cfg->sm_mode,
			MPCC_SM_FRAME_ALT,		sm_cfg->frame_alt,
			MPCC_SM_FIELD_ALT,		sm_cfg->field_alt,
			MPCC_SM_FORCE_NEXT_FRAME_POL,	sm_cfg->force_next_frame_porlarity,
			MPCC_SM_FORCE_NEXT_TOP_POL,	sm_cfg->force_next_field_polarity);
}
void mpc1_assert_idle_mpcc(struct mpc *mpc, int id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);

	ASSERT(!(mpc10->mpcc_in_use_mask & 1 << id));
	REG_WAIT(MPCC_STATUS[id],
			MPCC_IDLE, 1,
			1, 100000);
}

struct mpcc *mpc1_get_mpcc(struct mpc *mpc, int mpcc_id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);

	ASSERT(mpcc_id < mpc10->num_mpcc);
	return &(mpc->mpcc_array[mpcc_id]);
}

struct mpcc *mpc1_get_mpcc_for_dpp(struct mpc_tree *tree, int dpp_id)
{
	struct mpcc *tmp_mpcc = tree->opp_list;

	while (tmp_mpcc != NULL) {
		if (tmp_mpcc->dpp_id == dpp_id)
			return tmp_mpcc;
		tmp_mpcc = tmp_mpcc->mpcc_bot;
	}
	return NULL;
}

bool mpc1_is_mpcc_idle(struct mpc *mpc, int mpcc_id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);
	unsigned int top_sel;
	unsigned int opp_id;
	unsigned int idle;

	REG_GET(MPCC_TOP_SEL[mpcc_id], MPCC_TOP_SEL, &top_sel);
	REG_GET(MPCC_OPP_ID[mpcc_id],  MPCC_OPP_ID, &opp_id);
	REG_GET(MPCC_STATUS[mpcc_id],  MPCC_IDLE,   &idle);
	if (top_sel == 0xf && opp_id == 0xf && idle)
		return true;
	else
		return false;
}

void mpc1_assert_mpcc_idle_before_connect(struct mpc *mpc, int mpcc_id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);
	unsigned int top_sel, mpc_busy, mpc_idle;

	REG_GET(MPCC_TOP_SEL[mpcc_id],
			MPCC_TOP_SEL, &top_sel);

	if (top_sel == 0xf) {
		REG_GET_2(MPCC_STATUS[mpcc_id],
				MPCC_BUSY, &mpc_busy,
				MPCC_IDLE, &mpc_idle);

		ASSERT(mpc_busy == 0);
		ASSERT(mpc_idle == 1);
	}
}

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
struct mpcc *mpc1_insert_plane(
	struct mpc *mpc,
	struct mpc_tree *tree,
	struct mpcc_blnd_cfg *blnd_cfg,
	struct mpcc_sm_cfg *sm_cfg,
	struct mpcc *insert_above_mpcc,
	int dpp_id,
	int mpcc_id)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);
	struct mpcc *new_mpcc = NULL;

	/* sanity check parameters */
	ASSERT(mpcc_id < mpc10->num_mpcc);
	ASSERT(!(mpc10->mpcc_in_use_mask & 1 << mpcc_id));

	if (insert_above_mpcc) {
		/* check insert_above_mpcc exist in tree->opp_list */
		struct mpcc *temp_mpcc = tree->opp_list;

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
		REG_UPDATE(MPCC_CONTROL[mpcc_id], MPCC_MODE, MPCC_BLEND_MODE_TOP_LAYER_PASSTHROUGH);
	}
	REG_SET(MPCC_TOP_SEL[mpcc_id], 0, MPCC_TOP_SEL, dpp_id);
	REG_SET(MPCC_OPP_ID[mpcc_id], 0, MPCC_OPP_ID, tree->opp_id);

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
	new_mpcc->blnd_cfg = *blnd_cfg;
	mpc->funcs->update_blending(mpc, &new_mpcc->blnd_cfg, mpcc_id);

	/* update the stereo mix settings, if provided */
	if (sm_cfg != NULL) {
		new_mpcc->sm_cfg = *sm_cfg;
		mpc1_update_stereo_mix(mpc, sm_cfg, mpcc_id);
	}

	/* mark this mpcc as in use */
	mpc10->mpcc_in_use_mask |= 1 << mpcc_id;

	return new_mpcc;
}

/*
 * Remove a specified MPCC from the MPC tree.
 *
 * Parameters:
 * [in/out] mpc		- MPC context.
 * [in/out] tree	- MPC tree structure that plane will be removed from.
 * [in/out] mpcc	- MPCC to be removed from tree.
 *
 * Return:  void
 */
void mpc1_remove_mpcc(
	struct mpc *mpc,
	struct mpc_tree *tree,
	struct mpcc *mpcc_to_remove)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);
	bool found = false;
	int mpcc_id = mpcc_to_remove->mpcc_id;

	if (tree->opp_list == mpcc_to_remove) {
		found = true;
		/* remove MPCC from top of tree */
		if (mpcc_to_remove->mpcc_bot) {
			/* set the next MPCC in list to be the top MPCC */
			tree->opp_list = mpcc_to_remove->mpcc_bot;
			REG_UPDATE(MUX[tree->opp_id], MPC_OUT_MUX, tree->opp_list->mpcc_id);
		} else {
			/* there are no other MPCC is list */
			tree->opp_list = NULL;
			REG_UPDATE(MUX[tree->opp_id], MPC_OUT_MUX, 0xf);
		}
	} else {
		/* find mpcc to remove MPCC list */
		struct mpcc *temp_mpcc = tree->opp_list;

		while (temp_mpcc && temp_mpcc->mpcc_bot != mpcc_to_remove)
			temp_mpcc = temp_mpcc->mpcc_bot;

		if (temp_mpcc && temp_mpcc->mpcc_bot == mpcc_to_remove) {
			found = true;
			temp_mpcc->mpcc_bot = mpcc_to_remove->mpcc_bot;
			if (mpcc_to_remove->mpcc_bot) {
				/* remove MPCC in middle of list */
				REG_SET(MPCC_BOT_SEL[temp_mpcc->mpcc_id], 0,
						MPCC_BOT_SEL, mpcc_to_remove->mpcc_bot->mpcc_id);
			} else {
				/* remove MPCC from bottom of list */
				REG_SET(MPCC_BOT_SEL[temp_mpcc->mpcc_id], 0,
						MPCC_BOT_SEL, 0xf);
				REG_UPDATE(MPCC_CONTROL[temp_mpcc->mpcc_id],
						MPCC_MODE, MPCC_BLEND_MODE_TOP_LAYER_PASSTHROUGH);
			}
		}
	}

	if (found) {
		/* turn off MPCC mux registers */
		REG_SET(MPCC_TOP_SEL[mpcc_id], 0, MPCC_TOP_SEL, 0xf);
		REG_SET(MPCC_BOT_SEL[mpcc_id], 0, MPCC_BOT_SEL, 0xf);
		REG_SET(MPCC_OPP_ID[mpcc_id],  0, MPCC_OPP_ID,  0xf);

		/* mark this mpcc as not in use */
		mpc10->mpcc_in_use_mask &= ~(1 << mpcc_id);
		mpcc_to_remove->dpp_id = 0xf;
		mpcc_to_remove->mpcc_bot = NULL;
	} else {
		/* In case of resume from S3/S4, remove mpcc from bios left over */
		REG_SET(MPCC_TOP_SEL[mpcc_id], 0, MPCC_TOP_SEL, 0xf);
		REG_SET(MPCC_BOT_SEL[mpcc_id], 0, MPCC_BOT_SEL, 0xf);
		REG_SET(MPCC_OPP_ID[mpcc_id],  0, MPCC_OPP_ID,  0xf);
	}
}

static void mpc1_init_mpcc(struct mpcc *mpcc, int mpcc_inst)
{
	mpcc->mpcc_id = mpcc_inst;
	mpcc->dpp_id = 0xf;
	mpcc->mpcc_bot = NULL;
	mpcc->blnd_cfg.overlap_only = false;
	mpcc->blnd_cfg.global_alpha = 0xff;
	mpcc->blnd_cfg.global_gain = 0xff;
	mpcc->sm_cfg.enable = false;
}

/*
 * Reset the MPCC HW status by disconnecting all muxes.
 *
 * Parameters:
 * [in/out] mpc		- MPC context.
 *
 * Return:  void
 */
void mpc1_mpc_init(struct mpc *mpc)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);
	int mpcc_id;
	int opp_id;

	mpc10->mpcc_in_use_mask = 0;
	for (mpcc_id = 0; mpcc_id < mpc10->num_mpcc; mpcc_id++) {
		REG_SET(MPCC_TOP_SEL[mpcc_id], 0, MPCC_TOP_SEL, 0xf);
		REG_SET(MPCC_BOT_SEL[mpcc_id], 0, MPCC_BOT_SEL, 0xf);
		REG_SET(MPCC_OPP_ID[mpcc_id],  0, MPCC_OPP_ID,  0xf);

		mpc1_init_mpcc(&(mpc->mpcc_array[mpcc_id]), mpcc_id);
	}

	for (opp_id = 0; opp_id < MAX_OPP; opp_id++) {
		if (REG(MUX[opp_id]))
			REG_UPDATE(MUX[opp_id], MPC_OUT_MUX, 0xf);
	}
}

void mpc1_init_mpcc_list_from_hw(
	struct mpc *mpc,
	struct mpc_tree *tree)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);
	unsigned int opp_id;
	unsigned int top_sel;
	unsigned int bot_sel;
	unsigned int out_mux;
	struct mpcc *mpcc;
	int mpcc_id;
	int bot_mpcc_id;

	REG_GET(MUX[tree->opp_id], MPC_OUT_MUX, &out_mux);

	if (out_mux != 0xf) {
		for (mpcc_id = 0; mpcc_id < mpc10->num_mpcc; mpcc_id++) {
			REG_GET(MPCC_OPP_ID[mpcc_id],  MPCC_OPP_ID,  &opp_id);
			REG_GET(MPCC_TOP_SEL[mpcc_id], MPCC_TOP_SEL, &top_sel);
			REG_GET(MPCC_BOT_SEL[mpcc_id],  MPCC_BOT_SEL, &bot_sel);

			if (bot_sel == mpcc_id)
				bot_sel = 0xf;

			if ((opp_id == tree->opp_id) && (top_sel != 0xf)) {
				mpcc = mpc1_get_mpcc(mpc, mpcc_id);
				mpcc->dpp_id = top_sel;
				mpc10->mpcc_in_use_mask |= 1 << mpcc_id;

				if (out_mux == mpcc_id)
					tree->opp_list = mpcc;
				if (bot_sel != 0xf && bot_sel < mpc10->num_mpcc) {
					bot_mpcc_id = bot_sel;
					REG_GET(MPCC_OPP_ID[bot_mpcc_id],  MPCC_OPP_ID,  &opp_id);
					REG_GET(MPCC_TOP_SEL[bot_mpcc_id], MPCC_TOP_SEL, &top_sel);
					if ((opp_id == tree->opp_id) && (top_sel != 0xf)) {
						struct mpcc *mpcc_bottom = mpc1_get_mpcc(mpc, bot_mpcc_id);

						mpcc->mpcc_bot = mpcc_bottom;
					}
				}
			}
		}
	}
}

void mpc1_read_mpcc_state(
		struct mpc *mpc,
		int mpcc_inst,
		struct mpcc_state *s)
{
	struct dcn10_mpc *mpc10 = TO_DCN10_MPC(mpc);

	REG_GET(MPCC_OPP_ID[mpcc_inst], MPCC_OPP_ID, &s->opp_id);
	REG_GET(MPCC_TOP_SEL[mpcc_inst], MPCC_TOP_SEL, &s->dpp_id);
	REG_GET(MPCC_BOT_SEL[mpcc_inst], MPCC_BOT_SEL, &s->bot_mpcc_id);
	REG_GET_4(MPCC_CONTROL[mpcc_inst], MPCC_MODE, &s->mode,
			MPCC_ALPHA_BLND_MODE, &s->alpha_mode,
			MPCC_ALPHA_MULTIPLIED_MODE, &s->pre_multiplied_alpha,
			MPCC_BLND_ACTIVE_OVERLAP_ONLY, &s->pre_multiplied_alpha);
}

const struct mpc_funcs dcn10_mpc_funcs = {
	.read_mpcc_state = mpc1_read_mpcc_state,
	.insert_plane = mpc1_insert_plane,
	.remove_mpcc = mpc1_remove_mpcc,
	.mpc_init = mpc1_mpc_init,
	.get_mpcc_for_dpp = mpc1_get_mpcc_for_dpp,
	.wait_for_idle = mpc1_assert_idle_mpcc,
	.assert_mpcc_idle_before_connect = mpc1_assert_mpcc_idle_before_connect,
	.init_mpcc_list_from_hw = mpc1_init_mpcc_list_from_hw,
	.update_blending = mpc1_update_blending,
};

void dcn10_mpc_construct(struct dcn10_mpc *mpc10,
	struct dc_context *ctx,
	const struct dcn_mpc_registers *mpc_regs,
	const struct dcn_mpc_shift *mpc_shift,
	const struct dcn_mpc_mask *mpc_mask,
	int num_mpcc)
{
	int i;

	mpc10->base.ctx = ctx;

	mpc10->base.funcs = &dcn10_mpc_funcs;

	mpc10->mpc_regs = mpc_regs;
	mpc10->mpc_shift = mpc_shift;
	mpc10->mpc_mask = mpc_mask;

	mpc10->mpcc_in_use_mask = 0;
	mpc10->num_mpcc = num_mpcc;

	for (i = 0; i < MAX_MPCC; i++)
		mpc1_init_mpcc(&mpc10->base.mpcc_array[i], i);
}

