/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DCN35_OPP_H
#define __DCN35_OPP_H

#include "dcn20/dcn20_opp.h"

#define OPP_REG_VARIABLE_LIST_DCN3_5  \
	OPP_REG_VARIABLE_LIST_DCN2_0; \
	uint32_t OPP_TOP_CLK_CONTROL

#define OPP_MASK_SH_LIST_DCN35(mask_sh)  \
	OPP_MASK_SH_LIST_DCN20(mask_sh), \
		OPP_SF(OPP_TOP_CLK_CONTROL, OPP_FGCG_REP_DIS, mask_sh)

#define OPP_DCN35_REG_FIELD_LIST(type)          \
	struct {                                \
		OPP_DCN20_REG_FIELD_LIST(type); \
		type OPP_FGCG_REP_DIS;          \
	}

struct dcn35_opp_registers {
	OPP_REG_VARIABLE_LIST_DCN3_5;
};

struct dcn35_opp_shift {
	OPP_DCN35_REG_FIELD_LIST(uint8_t);
};

struct dcn35_opp_mask {
	OPP_DCN35_REG_FIELD_LIST(uint32_t);
};

void dcn35_opp_construct(struct dcn20_opp *oppn20,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn35_opp_registers *regs,
	const struct dcn35_opp_shift *opp_shift,
	const struct dcn35_opp_mask *opp_mask);

void dcn35_opp_set_fgcg(struct dcn20_opp *oppn20, bool enable);

#endif
