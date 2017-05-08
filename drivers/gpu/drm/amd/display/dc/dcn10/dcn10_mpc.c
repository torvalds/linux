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
	mpc->mpc_regs->reg

#define CTX \
	mpc->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mpc->mpc_shift->field_name, mpc->mpc_mask->field_name

/* Internal function to set mpc output mux */
static void set_output_mux(struct dcn10_mpc *mpc,
	uint8_t opp_id,
	uint8_t mpcc_id)
{
	if (mpcc_id != 0xf)
		REG_UPDATE(OPP_PIPE_CONTROL[opp_id],
				OPP_PIPE_CLOCK_EN, 1);

	REG_SET(MUX[opp_id], 0,
			MPC_OUT_MUX, mpcc_id);

/*	TODO: Move to post when ready.
   if (mpcc_id == 0xf) {
		MPCC_REG_UPDATE(OPP_PIPE0_OPP_PIPE_CONTROL,
				OPP_PIPE_CLOCK_EN, 0);
	}
*/
}

static void set_blend_mode(struct dcn10_mpc *mpc,
	enum blend_mode mode,
	uint8_t mpcc_id)
{
	/* Enable per-pixel alpha on this pipe */
	if (mode == TOP_BLND)
		REG_UPDATE_3(MPCC_CONTROL[mpcc_id],
				MPCC_ALPHA_BLND_MODE, 0,
				MPCC_ALPHA_MULTIPLIED_MODE, 0,
				MPCC_BLND_ACTIVE_OVERLAP_ONLY, 0);
	else
		REG_UPDATE_3(MPCC_CONTROL[mpcc_id],
				MPCC_ALPHA_BLND_MODE, 0,
				MPCC_ALPHA_MULTIPLIED_MODE, 1,
				MPCC_BLND_ACTIVE_OVERLAP_ONLY, 1);
}

void dcn10_set_mpc_background_color(struct dcn10_mpc *mpc,
	unsigned int mpcc_inst,
	struct tg_color *bg_color)
{
	/* mpc color is 12 bit.  tg_color is 10 bit */
	/* todo: might want to use 16 bit to represent color and have each
	 * hw block translate to correct color depth.
	 */
	uint32_t bg_r_cr = bg_color->color_r_cr << 2;
	uint32_t bg_g_y = bg_color->color_g_y << 2;
	uint32_t bg_b_cb = bg_color->color_b_cb << 2;

	REG_SET(MPCC_BG_R_CR[mpcc_inst], 0,
			MPCC_BG_R_CR, bg_r_cr);
	REG_SET(MPCC_BG_G_Y[mpcc_inst], 0,
			MPCC_BG_G_Y, bg_g_y);
	REG_SET(MPCC_BG_B_CB[mpcc_inst], 0,
			MPCC_BG_B_CB, bg_b_cb);
}

/* This function programs MPC tree configuration
 * Assume it is the initial time to setup MPC tree_configure, means
 * the instance of dpp/mpcc/opp specified in structure tree_cfg are
 * in idle status.
 * Before invoke this function, ensure that master lock of OPTC specified
 * by opp_id is set.
 *
 * tree_cfg[in] - new MPC_TREE_CFG
 */

void dcn10_set_mpc_tree(struct dcn10_mpc *mpc,
	struct mpc_tree_cfg *tree_cfg)
{
	int i;

	for (i = 0; i < tree_cfg->num_pipes; i++) {
		uint8_t mpcc_inst = tree_cfg->mpcc[i];

		REG_SET(MPCC_OPP_ID[mpcc_inst], 0,
			MPCC_OPP_ID, tree_cfg->opp_id);

		REG_SET(MPCC_TOP_SEL[mpcc_inst], 0,
			MPCC_TOP_SEL, tree_cfg->dpp[i]);

		if (i == tree_cfg->num_pipes-1) {
			REG_SET(MPCC_BOT_SEL[mpcc_inst], 0,
				MPCC_BOT_SEL, 0xF);

			/* MPCC_CONTROL->MPCC_MODE */
			REG_UPDATE(MPCC_CONTROL[mpcc_inst],
					MPCC_MODE, tree_cfg->mode);
		} else {
			REG_SET(MPCC_BOT_SEL[mpcc_inst], 0,
				MPCC_BOT_SEL, tree_cfg->dpp[i+1]);

			/* MPCC_CONTROL->MPCC_MODE */
			REG_UPDATE(MPCC_CONTROL[mpcc_inst],
					MPCC_MODE, 3);
		}

		if (i == 0)
			set_output_mux(
				mpc, tree_cfg->opp_id, mpcc_inst);

		set_blend_mode(mpc, tree_cfg->mode, mpcc_inst);
	}
}

void dcn10_set_mpc_passthrough(struct dcn10_mpc *mpc,
	uint8_t dpp_idx,
	uint8_t mpcc_idx,
	uint8_t opp_idx)
{
	struct mpc_tree_cfg tree_cfg = { 0 };

	tree_cfg.num_pipes = 1;
	tree_cfg.opp_id = opp_idx;
	tree_cfg.mode = TOP_PASSTHRU;
	/* TODO: FPGA bring up one MPC has only 1 DPP and 1 MPCC
	 * For blend case, need fill mode DPP and cascade MPCC
	 */
	tree_cfg.dpp[0] = dpp_idx;
	tree_cfg.mpcc[0] = mpcc_idx;
	dcn10_set_mpc_tree(mpc, &tree_cfg);
}

/*
 * This is the function to remove current MPC tree specified by tree_cfg
 * Before invoke this function, ensure that master lock of OPTC specified
 * by opp_id is set.
 *
 *tree_cfg[in/out] - current MPC_TREE_CFG
 */
void dcn10_delete_mpc_tree(struct dcn10_mpc *mpc,
	struct mpc_tree_cfg *tree_cfg)
{
	int i;

	for (i = 0; i < tree_cfg->num_pipes; i++) {
		uint8_t mpcc_inst = tree_cfg->mpcc[i];

		REG_SET(MPCC_OPP_ID[mpcc_inst], 0,
			MPCC_OPP_ID, 0xf);

		REG_SET(MPCC_TOP_SEL[mpcc_inst], 0,
			MPCC_TOP_SEL, 0xf);

		REG_SET(MPCC_BOT_SEL[mpcc_inst], 0,
			MPCC_BOT_SEL, 0xF);

		/* add remove dpp/mpcc pair into pending list
		 * TODO FPGA AddToPendingList if empty from pseudo code
		 */
		tree_cfg->dpp[i] = 0xf;
		tree_cfg->mpcc[i] = 0xf;
	}
	set_output_mux(mpc, tree_cfg->opp_id, 0xf);
	tree_cfg->opp_id = 0xf;
	tree_cfg->num_pipes = 0;
}

/* TODO FPGA: how to handle DPP?
 * Function to remove one of pipe from MPC configure tree by dpp idx
 * Before invoke this function, ensure that master lock of OPTC specified
 * by opp_id is set
 * This function can be invoke multiple times to remove more than 1 dpps.
 *
 * tree_cfg[in/out] - current MPC_TREE_CFG
 * idx[in] - index of dpp from tree_cfg to be removed.
 */
bool dcn10_remove_dpp(struct dcn10_mpc *mpc,
	struct mpc_tree_cfg *tree_cfg,
	uint8_t idx)
{
	int i;
	bool found = false;

	/* find dpp_idx from dpp array of tree_cfg */
	for (i = 0; i < tree_cfg->num_pipes; i++) {
		if (tree_cfg->dpp[i] == idx) {
			found = true;
			break;
		}
	}

	if (found) {
		/* add remove dpp/mpcc pair into pending list */

		/* TODO FPGA AddToPendingList if empty from pseudo code
		 * AddToPendingList(tree_cfg->dpp[i],tree_cfg->mpcc[i]);
		 */
		uint8_t mpcc_inst = tree_cfg->mpcc[i];

		REG_SET(MPCC_OPP_ID[mpcc_inst], 0,
				MPCC_OPP_ID, 0xf);

		REG_SET(MPCC_TOP_SEL[mpcc_inst], 0,
				MPCC_TOP_SEL, 0xf);

		REG_SET(MPCC_BOT_SEL[mpcc_inst], 0,
				MPCC_BOT_SEL, 0xF);

		if (i == 0) {
			if (tree_cfg->num_pipes > 1)
				set_output_mux(mpc,
					tree_cfg->opp_id, tree_cfg->mpcc[i+1]);
			else
				set_output_mux(mpc, tree_cfg->opp_id, 0xf);
		} else if (i == tree_cfg->num_pipes-1) {
			mpcc_inst = tree_cfg->mpcc[i - 1];

			REG_SET(MPCC_BOT_SEL[mpcc_inst], 0,
					MPCC_BOT_SEL, 0xF);

			REG_UPDATE(MPCC_CONTROL[mpcc_inst],
					MPCC_MODE, tree_cfg->mode);
		} else {
			mpcc_inst = tree_cfg->mpcc[i - 1];

			REG_SET(MPCC_BOT_SEL[mpcc_inst], 0,
				MPCC_BOT_SEL, tree_cfg->mpcc[i+1]);
		}
		set_blend_mode(mpc, tree_cfg->mode, mpcc_inst);

		/* update tree_cfg structure */
		while (i < tree_cfg->num_pipes - 1) {
			tree_cfg->dpp[i] = tree_cfg->dpp[i+1];
			tree_cfg->mpcc[i] = tree_cfg->mpcc[i+1];
			i++;
		}
		tree_cfg->num_pipes--;
	}
	return found;
}

/* TODO FPGA: how to handle DPP?
 * Function to add DPP/MPCC pair into MPC configure tree by position.
 * Before invoke this function, ensure that master lock of OPTC specified
 * by opp_id is set
 * This function can be invoke multiple times to add more than 1 pipes.
 *
 * tree_cfg[in/out] - current MPC_TREE_CFG
 * dpp_idx[in]	 - index of an idle dpp insatnce to be added.
 * mpcc_idx[in]	 - index of an idle mpcc instance to be added.
 * poistion[in]	 - position of dpp/mpcc pair to be added into current tree_cfg
 *                 0 means insert to the most top layer of MPC tree
 */
void dcn10_add_dpp(struct dcn10_mpc *mpc,
	struct mpc_tree_cfg *tree_cfg,
	uint8_t dpp_idx,
	uint8_t mpcc_idx,
	uint8_t position)
{
	uint8_t temp;
	uint8_t temp1;

	REG_SET(MPCC_OPP_ID[mpcc_idx], 0,
			MPCC_OPP_ID, tree_cfg->opp_id);

	REG_SET(MPCC_TOP_SEL[mpcc_idx], 0,
			MPCC_TOP_SEL, dpp_idx);

	if (position == 0) {
		/* idle dpp/mpcc is added to the top layer of tree */
		REG_SET(MPCC_BOT_SEL[mpcc_idx], 0,
				MPCC_BOT_SEL, tree_cfg->mpcc[0]);
		REG_UPDATE(MPCC_CONTROL[mpcc_idx],
				MPCC_MODE, 3);

		/* opp will get new output. from new added mpcc */
		set_output_mux(mpc, tree_cfg->opp_id, mpcc_idx);

		set_blend_mode(mpc, tree_cfg->mode, mpcc_idx);

	} else if (position == tree_cfg->num_pipes) {
		/* idle dpp/mpcc is added to the bottom layer of tree */

		/* get instance of previous bottom mpcc, set to middle layer */
		temp = tree_cfg->mpcc[tree_cfg->num_pipes - 1];

		REG_SET(MPCC_BOT_SEL[temp], 0,
				MPCC_BOT_SEL, mpcc_idx);

		REG_UPDATE(MPCC_CONTROL[temp],
				MPCC_MODE, 3);

		/* mpcc_idx become new bottom mpcc*/
		REG_SET(MPCC_BOT_SEL[mpcc_idx], 0,
				MPCC_BOT_SEL, 0xf);

		REG_UPDATE(MPCC_CONTROL[mpcc_idx],
				MPCC_MODE, tree_cfg->mode);

		set_blend_mode(mpc, tree_cfg->mode, mpcc_idx);
	} else {
		/* idle dpp/mpcc is added to middle of tree */
		temp = tree_cfg->mpcc[position - 1];
		temp1 = tree_cfg->mpcc[position];

		/* new mpcc instance temp1 is added right after temp*/
		REG_SET(MPCC_BOT_SEL[temp], 0,
				MPCC_BOT_SEL, mpcc_idx);

		/* mpcc_idx connect previous temp+1 to new mpcc */
		REG_SET(MPCC_BOT_SEL[mpcc_idx], 0,
				MPCC_BOT_SEL, temp1);

		/* temp TODO: may not need*/
		REG_UPDATE(MPCC_CONTROL[temp],
				MPCC_MODE, 3);

		set_blend_mode(mpc, tree_cfg->mode, temp);
	}

	/* update tree_cfg structure */
	temp = tree_cfg->num_pipes - 1;

	/*
	 * iterating from the last mpc/dpp pair to the one being added, shift
	 * them down one position
	 */
	while (temp > position) {
		tree_cfg->dpp[temp + 1] = tree_cfg->dpp[temp];
		tree_cfg->mpcc[temp + 1] = tree_cfg->mpcc[temp];
		temp--;
	}

	/* insert the new mpc/dpp pair into the tree_cfg*/
	tree_cfg->dpp[position] = dpp_idx;
	tree_cfg->mpcc[position] = mpcc_idx;
	tree_cfg->num_pipes++;
}

void wait_mpcc_idle(struct dcn10_mpc *mpc,
	uint8_t mpcc_id)
{
	REG_WAIT(MPCC_STATUS[mpcc_id],
			MPCC_IDLE, 1,
			1000, 1000);
}

