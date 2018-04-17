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

#ifndef __DC_MPCC_DCN10_H__
#define __DC_MPCC_DCN10_H__

#include "mpc.h"

#define TO_DCN10_MPC(mpc_base) \
	container_of(mpc_base, struct dcn10_mpc, base)

#define MAX_MPCC 6
#define MAX_OPP 6

#define MPC_COMMON_REG_LIST_DCN1_0(inst) \
	SRII(MPCC_TOP_SEL, MPCC, inst),\
	SRII(MPCC_BOT_SEL, MPCC, inst),\
	SRII(MPCC_CONTROL, MPCC, inst),\
	SRII(MPCC_STATUS, MPCC, inst),\
	SRII(MPCC_OPP_ID, MPCC, inst),\
	SRII(MPCC_BG_G_Y, MPCC, inst),\
	SRII(MPCC_BG_R_CR, MPCC, inst),\
	SRII(MPCC_BG_B_CB, MPCC, inst),\
	SRII(MPCC_BG_B_CB, MPCC, inst)

#define MPC_OUT_MUX_COMMON_REG_LIST_DCN1_0(inst) \
	SRII(MUX, MPC_OUT, inst)

#define MPC_COMMON_REG_VARIABLE_LIST \
	uint32_t MPCC_TOP_SEL[MAX_MPCC]; \
	uint32_t MPCC_BOT_SEL[MAX_MPCC]; \
	uint32_t MPCC_CONTROL[MAX_MPCC]; \
	uint32_t MPCC_STATUS[MAX_MPCC]; \
	uint32_t MPCC_OPP_ID[MAX_MPCC]; \
	uint32_t MPCC_BG_G_Y[MAX_MPCC]; \
	uint32_t MPCC_BG_R_CR[MAX_MPCC]; \
	uint32_t MPCC_BG_B_CB[MAX_MPCC]; \
	uint32_t MUX[MAX_OPP];

#define MPC_COMMON_MASK_SH_LIST_DCN1_0(mask_sh)\
	SF(MPCC0_MPCC_TOP_SEL, MPCC_TOP_SEL, mask_sh),\
	SF(MPCC0_MPCC_BOT_SEL, MPCC_BOT_SEL, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_MODE, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_ALPHA_BLND_MODE, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_ALPHA_MULTIPLIED_MODE, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_BLND_ACTIVE_OVERLAP_ONLY, mask_sh),\
	SF(MPCC0_MPCC_STATUS, MPCC_IDLE, mask_sh),\
	SF(MPCC0_MPCC_STATUS, MPCC_BUSY, mask_sh),\
	SF(MPCC0_MPCC_OPP_ID, MPCC_OPP_ID, mask_sh),\
	SF(MPCC0_MPCC_BG_G_Y, MPCC_BG_G_Y, mask_sh),\
	SF(MPCC0_MPCC_BG_R_CR, MPCC_BG_R_CR, mask_sh),\
	SF(MPCC0_MPCC_BG_B_CB, MPCC_BG_B_CB, mask_sh),\
	SF(MPC_OUT0_MUX, MPC_OUT_MUX, mask_sh)

#define MPC_REG_FIELD_LIST(type) \
	type MPCC_TOP_SEL;\
	type MPCC_BOT_SEL;\
	type MPCC_MODE;\
	type MPCC_ALPHA_BLND_MODE;\
	type MPCC_ALPHA_MULTIPLIED_MODE;\
	type MPCC_BLND_ACTIVE_OVERLAP_ONLY;\
	type MPCC_IDLE;\
	type MPCC_BUSY;\
	type MPCC_OPP_ID;\
	type MPCC_BG_G_Y;\
	type MPCC_BG_R_CR;\
	type MPCC_BG_B_CB;\
	type MPC_OUT_MUX;

struct dcn_mpc_registers {
	MPC_COMMON_REG_VARIABLE_LIST
};

struct dcn_mpc_shift {
	MPC_REG_FIELD_LIST(uint8_t)
};

struct dcn_mpc_mask {
	MPC_REG_FIELD_LIST(uint32_t)
};

struct dcn10_mpc {
	struct mpc base;

	int mpcc_in_use_mask;
	int num_mpcc;
	const struct dcn_mpc_registers *mpc_regs;
	const struct dcn_mpc_shift *mpc_shift;
	const struct dcn_mpc_mask *mpc_mask;
};

void dcn10_mpc_construct(struct dcn10_mpc *mpcc10,
	struct dc_context *ctx,
	const struct dcn_mpc_registers *mpc_regs,
	const struct dcn_mpc_shift *mpc_shift,
	const struct dcn_mpc_mask *mpc_mask,
	int num_mpcc);

int mpc10_mpcc_add(
		struct mpc *mpc,
		struct mpcc_cfg *cfg);

void mpc10_mpcc_remove(
		struct mpc *mpc,
		struct mpc_tree_cfg *tree_cfg,
		int opp_id,
		int dpp_id);

void mpc10_assert_idle_mpcc(
		struct mpc *mpc,
		int id);

void mpc10_update_blend_mode(
		struct mpc *mpc,
		struct mpcc_cfg *cfg);

#endif
