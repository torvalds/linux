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

#ifndef __DC_MPCC_DCN201_H__
#define __DC_MPCC_DCN201_H__

#include "dcn20/dcn20_mpc.h"

#define TO_DCN201_MPC(mpc_base) \
	container_of(mpc_base, struct dcn201_mpc, base)

#define MPC_REG_LIST_DCN201(inst) \
	MPC_REG_LIST_DCN2_0(inst)

#define MPC_OUT_MUX_REG_LIST_DCN201(inst) \
	MPC_OUT_MUX_REG_LIST_DCN2_0(inst)

#define MPC_REG_VARIABLE_LIST_DCN201 \
	MPC_REG_VARIABLE_LIST_DCN2_0

#define MPC_COMMON_MASK_SH_LIST_DCN201(mask_sh) \
	MPC_COMMON_MASK_SH_LIST_DCN2_0(mask_sh),\
	SF(MPC_OUT0_MUX, MPC_OUT_RATE_CONTROL, mask_sh),\
	SF(MPC_OUT0_MUX, MPC_OUT_RATE_CONTROL_DISABLE, mask_sh),\
	SF(MPC_OUT0_MUX, MPC_OUT_FLOW_CONTROL_MODE, mask_sh),\
	SF(MPC_OUT0_MUX, MPC_OUT_FLOW_CONTROL_COUNT0, mask_sh),\
	SF(MPC_OUT0_MUX, MPC_OUT_FLOW_CONTROL_COUNT1, mask_sh)

#define MPC_REG_FIELD_LIST_DCN201(type) \
	MPC_REG_FIELD_LIST_DCN2_0(type) \
	type MPC_OUT_RATE_CONTROL;\
	type MPC_OUT_RATE_CONTROL_DISABLE;\
	type MPC_OUT_FLOW_CONTROL_MODE;\
	type MPC_OUT_FLOW_CONTROL_COUNT0;\
	type MPC_OUT_FLOW_CONTROL_COUNT1;

struct dcn201_mpc_registers {
	MPC_REG_VARIABLE_LIST_DCN201
};

struct dcn201_mpc_shift {
	MPC_REG_FIELD_LIST_DCN201(uint8_t)
};

struct dcn201_mpc_mask {
	MPC_REG_FIELD_LIST_DCN201(uint32_t)
};

struct dcn201_mpc {
	struct mpc base;
	int mpcc_in_use_mask;
	int num_mpcc;
	const struct dcn201_mpc_registers *mpc_regs;
	const struct dcn201_mpc_shift *mpc_shift;
	const struct dcn201_mpc_mask *mpc_mask;
};

void dcn201_mpc_construct(struct dcn201_mpc *mpc201,
	struct dc_context *ctx,
	const struct dcn201_mpc_registers *mpc_regs,
	const struct dcn201_mpc_shift *mpc_shift,
	const struct dcn201_mpc_mask *mpc_mask,
	int num_mpcc);

#endif
