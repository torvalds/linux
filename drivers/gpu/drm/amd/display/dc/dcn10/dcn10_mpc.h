/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_MPC_DCN10_H__
#define __DC_MPC_DCN10_H__

#include "mpc.h"

#define TO_DCN10_MPC(mpc_base)\
	container_of(mpc_base, struct dcn10_mpc, base)

#define MAX_MPCC 4
#define MAX_MPC_OUT 4
#define MAX_OPP 4

#define MPC_COMMON_REG_LIST_DCN1_0(inst) \
	SRII(MPCC_TOP_SEL, MPCC, inst),\
	SRII(MPCC_BOT_SEL, MPCC, inst),\
	SRII(MPCC_CONTROL, MPCC, inst),\
	SRII(MPCC_STATUS, MPCC, inst),\
	SRII(MPCC_OPP_ID, MPCC, inst),\
	SRII(MPCC_BG_G_Y, MPCC, inst),\
	SRII(MPCC_BG_R_CR, MPCC, inst),\
	SRII(MPCC_BG_B_CB, MPCC, inst),\
	SRII(MPCC_BG_B_CB, MPCC, inst),\
	SRII(MUX, MPC_OUT, inst),\
	SRII(OPP_PIPE_CONTROL, OPP_PIPE, inst)

struct dcn_mpc_registers {
	uint32_t MPCC_TOP_SEL[MAX_MPCC];
	uint32_t MPCC_BOT_SEL[MAX_MPCC];
	uint32_t MPCC_CONTROL[MAX_MPCC];
	uint32_t MPCC_STATUS[MAX_MPCC];
	uint32_t MPCC_OPP_ID[MAX_MPCC];
	uint32_t MPCC_BG_G_Y[MAX_MPCC];
	uint32_t MPCC_BG_R_CR[MAX_MPCC];
	uint32_t MPCC_BG_B_CB[MAX_MPCC];
	uint32_t MUX[MAX_MPC_OUT];
	uint32_t OPP_PIPE_CONTROL[MAX_OPP];
};

#define MPC_COMMON_MASK_SH_LIST_DCN1_0(mask_sh)\
	SF(MPCC0_MPCC_TOP_SEL, MPCC_TOP_SEL, mask_sh),\
	SF(MPCC0_MPCC_BOT_SEL, MPCC_BOT_SEL, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_MODE, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_ALPHA_BLND_MODE, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_ALPHA_MULTIPLIED_MODE, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_BLND_ACTIVE_OVERLAP_ONLY, mask_sh),\
	SF(MPCC0_MPCC_STATUS, MPCC_IDLE, mask_sh),\
	SF(MPCC0_MPCC_OPP_ID, MPCC_OPP_ID, mask_sh),\
	SF(MPCC0_MPCC_BG_G_Y, MPCC_BG_G_Y, mask_sh),\
	SF(MPCC0_MPCC_BG_R_CR, MPCC_BG_R_CR, mask_sh),\
	SF(MPCC0_MPCC_BG_B_CB, MPCC_BG_B_CB, mask_sh),\
	SF(MPC_OUT0_MUX, MPC_OUT_MUX, mask_sh),\
	SF(OPP_PIPE0_OPP_PIPE_CONTROL, OPP_PIPE_CLOCK_EN, mask_sh)

#define MPC_REG_FIELD_LIST(type) \
	type MPCC_TOP_SEL;\
	type MPCC_BOT_SEL;\
	type MPCC_MODE;\
	type MPCC_ALPHA_BLND_MODE;\
	type MPCC_ALPHA_MULTIPLIED_MODE;\
	type MPCC_BLND_ACTIVE_OVERLAP_ONLY;\
	type MPCC_IDLE;\
	type MPCC_OPP_ID;\
	type MPCC_BG_G_Y;\
	type MPCC_BG_R_CR;\
	type MPCC_BG_B_CB;\
	type MPC_OUT_MUX;\
	type OPP_PIPE_CLOCK_EN;\

struct dcn_mpc_shift {
	MPC_REG_FIELD_LIST(uint8_t)
};

struct dcn_mpc_mask {
	MPC_REG_FIELD_LIST(uint32_t)
};

struct dcn10_mpc {
	struct mpc base;
	const struct dcn_mpc_registers *mpc_regs;
	const struct dcn_mpc_shift *mpc_shift;
	const struct dcn_mpc_mask *mpc_mask;
};

void dcn10_delete_mpc_tree(struct dcn10_mpc *mpc,
	struct mpc_tree_cfg *tree_cfg);

bool dcn10_remove_dpp(struct dcn10_mpc *mpc,
	struct mpc_tree_cfg *tree_cfg,
	uint8_t idx);

void dcn10_add_dpp(struct dcn10_mpc *mpc,
	struct mpc_tree_cfg *tree_cfg,
	uint8_t dpp_idx,
	uint8_t mpcc_idx,
	uint8_t per_pixel_alpha,
	uint8_t position);

void wait_mpcc_idle(struct dcn10_mpc *mpc,
	uint8_t mpcc_id);

void dcn10_set_mpc_tree(struct dcn10_mpc *mpc,
	struct mpc_tree_cfg *tree_cfg);

void dcn10_set_mpc_background_color(struct dcn10_mpc *mpc,
	unsigned int mpcc_inst,
	struct tg_color *bg_color);
#endif
