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

#define MODE_TOP_ONLY 1
#define MODE_BLEND 3

/* Internal function to set mpc output mux */
static void set_output_mux(struct dcn10_mpc *mpc,
	uint8_t opp_id,
	uint8_t mpcc_id)
{
	if (mpcc_id != 0xf)
		REG_UPDATE(OPP_PIPE_CONTROL[opp_id],
				OPP_PIPE_CLOCK_EN, 1);

	REG_SET(MUX[opp_id], 0, MPC_OUT_MUX, mpcc_id);
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

			REG_UPDATE(MPCC_CONTROL[mpcc_inst],
					MPCC_MODE, MODE_TOP_ONLY);
		} else {
			REG_SET(MPCC_BOT_SEL[mpcc_inst], 0,
				MPCC_BOT_SEL, tree_cfg->dpp[i+1]);

			REG_UPDATE(MPCC_CONTROL[mpcc_inst],
					MPCC_MODE, MODE_BLEND);
		}

		if (i == 0)
			set_output_mux(
				mpc, tree_cfg->opp_id, mpcc_inst);

		REG_UPDATE_2(MPCC_CONTROL[mpcc_inst],
				MPCC_ALPHA_BLND_MODE,
				tree_cfg->per_pixel_alpha[i] ? 0 : 2,
				MPCC_ALPHA_MULTIPLIED_MODE, 0);
	}
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
		tree_cfg->per_pixel_alpha[i] = false;
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
	uint8_t mpcc_inst;
	bool found = false;

	/* find dpp_idx from dpp array of tree_cfg */
	for (i = 0; i < tree_cfg->num_pipes; i++) {
		if (tree_cfg->dpp[i] == idx) {
			found = true;
			break;
		}
	}

	if (!found) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	mpcc_inst = tree_cfg->mpcc[i];

	REG_SET(MPCC_OPP_ID[mpcc_inst], 0,
			MPCC_OPP_ID, 0xf);

	REG_SET(MPCC_TOP_SEL[mpcc_inst], 0,
			MPCC_TOP_SEL, 0xf);

	REG_SET(MPCC_BOT_SEL[mpcc_inst], 0,
			MPCC_BOT_SEL, 0xf);

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

		/* prev mpc is now last, set to top only*/
		REG_UPDATE(MPCC_CONTROL[mpcc_inst],
				MPCC_MODE, MODE_TOP_ONLY);
	} else {
		mpcc_inst = tree_cfg->mpcc[i - 1];

		REG_SET(MPCC_BOT_SEL[mpcc_inst], 0,
			MPCC_BOT_SEL, tree_cfg->mpcc[i+1]);
	}

	/* update tree_cfg structure */
	while (i < tree_cfg->num_pipes - 1) {
		tree_cfg->dpp[i] = tree_cfg->dpp[i+1];
		tree_cfg->mpcc[i] = tree_cfg->mpcc[i+1];
		tree_cfg->per_pixel_alpha[i] = tree_cfg->per_pixel_alpha[i+1];
		i++;
	}
	tree_cfg->num_pipes--;

	return true;
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
	uint8_t per_pixel_alpha,
	uint8_t position)
{
	uint8_t prev;
	uint8_t next;

	REG_SET(MPCC_OPP_ID[mpcc_idx], 0,
			MPCC_OPP_ID, tree_cfg->opp_id);
	REG_SET(MPCC_TOP_SEL[mpcc_idx], 0,
			MPCC_TOP_SEL, dpp_idx);

	if (position == 0) {
		/* idle dpp/mpcc is added to the top layer of tree */
		REG_SET(MPCC_BOT_SEL[mpcc_idx], 0,
				MPCC_BOT_SEL, tree_cfg->mpcc[0]);

		/* bottom mpc is always top only */
		REG_UPDATE(MPCC_CONTROL[mpcc_idx],
				MPCC_MODE, MODE_TOP_ONLY);
		/* opp will get new output. from new added mpcc */
		set_output_mux(mpc, tree_cfg->opp_id, mpcc_idx);

	} else if (position == tree_cfg->num_pipes) {
		/* idle dpp/mpcc is added to the bottom layer of tree */

		/* get instance of previous bottom mpcc, set to middle layer */
		prev = tree_cfg->mpcc[position - 1];

		REG_SET(MPCC_BOT_SEL[prev], 0,
				MPCC_BOT_SEL, mpcc_idx);

		/* all mpcs other than bottom need to blend */
		REG_UPDATE(MPCC_CONTROL[prev],
				MPCC_MODE, MODE_BLEND);

		/* mpcc_idx become new bottom mpcc*/
		REG_SET(MPCC_BOT_SEL[mpcc_idx], 0,
				MPCC_BOT_SEL, 0xf);

		/* bottom mpc is always top only */
		REG_UPDATE(MPCC_CONTROL[mpcc_idx],
				MPCC_MODE, MODE_TOP_ONLY);
	} else {
		/* idle dpp/mpcc is added to middle of tree */
		prev = tree_cfg->mpcc[position - 1]; /* mpc a */
		next = tree_cfg->mpcc[position]; /* mpc b */

		/* connect mpc inserted below mpc a*/
		REG_SET(MPCC_BOT_SEL[prev], 0,
				MPCC_BOT_SEL, mpcc_idx);

		/* blend on mpc being inserted */
		REG_UPDATE(MPCC_CONTROL[mpcc_idx],
				MPCC_MODE, MODE_BLEND);

		/* Connect mpc b below one inserted */
		REG_SET(MPCC_BOT_SEL[mpcc_idx], 0,
				MPCC_BOT_SEL, next);

	}
	/* premultiplied mode only if alpha is on for the layer*/
	REG_UPDATE_2(MPCC_CONTROL[mpcc_idx],
			MPCC_ALPHA_BLND_MODE,
			tree_cfg->per_pixel_alpha[position] ? 0 : 2,
			MPCC_ALPHA_MULTIPLIED_MODE, 0);

	/*
	 * iterating from the last mpc/dpp pair to the one being added, shift
	 * them down one position
	 */
	for (next = tree_cfg->num_pipes; next > position; next--) {
		tree_cfg->dpp[next] = tree_cfg->dpp[next - 1];
		tree_cfg->mpcc[next] = tree_cfg->mpcc[next - 1];
		tree_cfg->per_pixel_alpha[next] = tree_cfg->per_pixel_alpha[next - 1];
	}

	/* insert the new mpc/dpp pair into the tree_cfg*/
	tree_cfg->dpp[position] = dpp_idx;
	tree_cfg->mpcc[position] = mpcc_idx;
	tree_cfg->per_pixel_alpha[position] = per_pixel_alpha;
	tree_cfg->num_pipes++;
}

void wait_mpcc_idle(struct dcn10_mpc *mpc,
	uint8_t mpcc_id)
{
	REG_WAIT(MPCC_STATUS[mpcc_id],
			MPCC_IDLE, 1,
			1000, 1000);
}

