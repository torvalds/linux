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

#define MPC_COMMON_REG_LIST_DCN1_0(inst) \
	SRII(MPCC_TOP_SEL, MPCC, inst),\
	SRII(MPCC_BOT_SEL, MPCC, inst),\
	SRII(MPCC_CONTROL, MPCC, inst),\
	SRII(MPCC_STATUS, MPCC, inst),\
	SRII(MPCC_OPP_ID, MPCC, inst),\
	SRII(MPCC_BG_G_Y, MPCC, inst),\
	SRII(MPCC_BG_R_CR, MPCC, inst),\
	SRII(MPCC_BG_B_CB, MPCC, inst),\
	SRII(MPCC_SM_CONTROL, MPCC, inst),\
	SRII(MPCC_UPDATE_LOCK_SEL, MPCC, inst)

#define MPC_OUT_MUX_COMMON_REG_LIST_DCN1_0(inst) \
	SRII(MUX, MPC_OUT, inst),\
	VUPDATE_SRII(CUR, VUPDATE_LOCK_SET, inst)

#define MPC_COMMON_REG_VARIABLE_LIST \
	uint32_t MPCC_TOP_SEL[MAX_MPCC]; \
	uint32_t MPCC_BOT_SEL[MAX_MPCC]; \
	uint32_t MPCC_CONTROL[MAX_MPCC]; \
	uint32_t MPCC_STATUS[MAX_MPCC]; \
	uint32_t MPCC_OPP_ID[MAX_MPCC]; \
	uint32_t MPCC_BG_G_Y[MAX_MPCC]; \
	uint32_t MPCC_BG_R_CR[MAX_MPCC]; \
	uint32_t MPCC_BG_B_CB[MAX_MPCC]; \
	uint32_t MPCC_SM_CONTROL[MAX_MPCC]; \
	uint32_t MUX[MAX_OPP]; \
	uint32_t MPCC_UPDATE_LOCK_SEL[MAX_MPCC]; \
	uint32_t CUR[MAX_OPP];

#define MPC_COMMON_MASK_SH_LIST_DCN1_0(mask_sh)\
	SF(MPCC0_MPCC_TOP_SEL, MPCC_TOP_SEL, mask_sh),\
	SF(MPCC0_MPCC_BOT_SEL, MPCC_BOT_SEL, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_MODE, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_ALPHA_BLND_MODE, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_ALPHA_MULTIPLIED_MODE, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_BLND_ACTIVE_OVERLAP_ONLY, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_GLOBAL_ALPHA, mask_sh),\
	SF(MPCC0_MPCC_CONTROL, MPCC_GLOBAL_GAIN, mask_sh),\
	SF(MPCC0_MPCC_STATUS, MPCC_IDLE, mask_sh),\
	SF(MPCC0_MPCC_STATUS, MPCC_BUSY, mask_sh),\
	SF(MPCC0_MPCC_OPP_ID, MPCC_OPP_ID, mask_sh),\
	SF(MPCC0_MPCC_BG_G_Y, MPCC_BG_G_Y, mask_sh),\
	SF(MPCC0_MPCC_BG_R_CR, MPCC_BG_R_CR, mask_sh),\
	SF(MPCC0_MPCC_BG_B_CB, MPCC_BG_B_CB, mask_sh),\
	SF(MPCC0_MPCC_SM_CONTROL, MPCC_SM_EN, mask_sh),\
	SF(MPCC0_MPCC_SM_CONTROL, MPCC_SM_MODE, mask_sh),\
	SF(MPCC0_MPCC_SM_CONTROL, MPCC_SM_FRAME_ALT, mask_sh),\
	SF(MPCC0_MPCC_SM_CONTROL, MPCC_SM_FIELD_ALT, mask_sh),\
	SF(MPCC0_MPCC_SM_CONTROL, MPCC_SM_FORCE_NEXT_FRAME_POL, mask_sh),\
	SF(MPCC0_MPCC_SM_CONTROL, MPCC_SM_FORCE_NEXT_TOP_POL, mask_sh),\
	SF(MPC_OUT0_MUX, MPC_OUT_MUX, mask_sh),\
	SF(MPCC0_MPCC_UPDATE_LOCK_SEL, MPCC_UPDATE_LOCK_SEL, mask_sh)

#define MPC_REG_FIELD_LIST(type) \
	type MPCC_TOP_SEL;\
	type MPCC_BOT_SEL;\
	type MPCC_MODE;\
	type MPCC_ALPHA_BLND_MODE;\
	type MPCC_ALPHA_MULTIPLIED_MODE;\
	type MPCC_BLND_ACTIVE_OVERLAP_ONLY;\
	type MPCC_GLOBAL_ALPHA;\
	type MPCC_GLOBAL_GAIN;\
	type MPCC_IDLE;\
	type MPCC_BUSY;\
	type MPCC_OPP_ID;\
	type MPCC_BG_G_Y;\
	type MPCC_BG_R_CR;\
	type MPCC_BG_B_CB;\
	type MPCC_SM_EN;\
	type MPCC_SM_MODE;\
	type MPCC_SM_FRAME_ALT;\
	type MPCC_SM_FIELD_ALT;\
	type MPCC_SM_FORCE_NEXT_FRAME_POL;\
	type MPCC_SM_FORCE_NEXT_TOP_POL;\
	type MPC_OUT_MUX;\
	type MPCC_UPDATE_LOCK_SEL;\
	type CUR_VUPDATE_LOCK_SET;

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

struct mpcc *mpc1_insert_plane(
	struct mpc *mpc,
	struct mpc_tree *tree,
	struct mpcc_blnd_cfg *blnd_cfg,
	struct mpcc_sm_cfg *sm_cfg,
	struct mpcc *insert_above_mpcc,
	int dpp_id,
	int mpcc_id);

void mpc1_remove_mpcc(
	struct mpc *mpc,
	struct mpc_tree *tree,
	struct mpcc *mpcc);

void mpc1_mpc_init(
	struct mpc *mpc);

void mpc1_mpc_init_single_inst(
	struct mpc *mpc,
	unsigned int mpcc_id);

void mpc1_assert_idle_mpcc(
	struct mpc *mpc,
	int id);

void mpc1_set_bg_color(
	struct mpc *mpc,
	struct tg_color *bg_color,
	int id);

void mpc1_update_stereo_mix(
	struct mpc *mpc,
	struct mpcc_sm_cfg *sm_cfg,
	int mpcc_id);

void mpc1_assert_mpcc_idle_before_connect(
	struct mpc *mpc,
	int mpcc_id);

void mpc1_init_mpcc_list_from_hw(
	struct mpc *mpc,
	struct mpc_tree *tree);

struct mpcc *mpc1_get_mpcc(
	struct mpc *mpc,
	int mpcc_id);

struct mpcc *mpc1_get_mpcc_for_dpp(
	struct mpc_tree *tree,
	int dpp_id);

void mpc1_read_mpcc_state(
		struct mpc *mpc,
		int mpcc_inst,
		struct mpcc_state *s);

void mpc1_cursor_lock(struct mpc *mpc, int opp_id, bool lock);

unsigned int mpc1_get_mpc_out_mux(struct mpc *mpc, int opp_id);
#endif
