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

#define TO_DCN10_MPCC(mpcc_base) \
	container_of(mpcc_base, struct dcn10_mpcc, base)

#define MAX_OPP 4

#define MPC_COMMON_REG_LIST_DCN1_0(inst) \
	SRII(MUX, MPC_OUT, inst),\
	SRII(OPP_PIPE_CONTROL, OPP_PIPE, inst)

#define MPCC_COMMON_REG_LIST_DCN1_0(inst) \
	SRI(MPCC_TOP_SEL, MPCC, inst),\
	SRI(MPCC_BOT_SEL, MPCC, inst),\
	SRI(MPCC_CONTROL, MPCC, inst),\
	SRI(MPCC_STATUS, MPCC, inst),\
	SRI(MPCC_OPP_ID, MPCC, inst),\
	SRI(MPCC_BG_G_Y, MPCC, inst),\
	SRI(MPCC_BG_R_CR, MPCC, inst),\
	SRI(MPCC_BG_B_CB, MPCC, inst),\
	SRI(MPCC_BG_B_CB, MPCC, inst)

struct dcn_mpcc_registers {
	uint32_t MPCC_TOP_SEL;
	uint32_t MPCC_BOT_SEL;
	uint32_t MPCC_CONTROL;
	uint32_t MPCC_STATUS;
	uint32_t MPCC_OPP_ID;
	uint32_t MPCC_BG_G_Y;
	uint32_t MPCC_BG_R_CR;
	uint32_t MPCC_BG_B_CB;
	uint32_t OPP_PIPE_CONTROL[MAX_OPP];
	uint32_t MUX[MAX_OPP];
};

#define MPCC_COMMON_MASK_SH_LIST_DCN1_0(mask_sh)\
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

#define MPCC_REG_FIELD_LIST(type) \
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

struct dcn_mpcc_shift {
	MPCC_REG_FIELD_LIST(uint8_t)
};

struct dcn_mpcc_mask {
	MPCC_REG_FIELD_LIST(uint32_t)
};

struct dcn10_mpcc {
	struct mpcc base;
	const struct dcn_mpcc_registers *mpcc_regs;
	const struct dcn_mpcc_shift *mpcc_shift;
	const struct dcn_mpcc_mask *mpcc_mask;

	int opp_id;
};

void dcn10_mpcc_construct(struct dcn10_mpcc *mpcc10,
	struct dc_context *ctx,
	const struct dcn_mpcc_registers *mpcc_regs,
	const struct dcn_mpcc_shift *mpcc_shift,
	const struct dcn_mpcc_mask *mpcc_mask,
	int inst);

#endif
