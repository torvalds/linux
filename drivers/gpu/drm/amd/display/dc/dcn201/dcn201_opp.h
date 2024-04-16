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

#ifndef __DC_OPP_DCN201_H__
#define __DC_OPP_DCN201_H__

#include "dcn20/dcn20_opp.h"

#define TO_DCN201_OPP(opp)\
	container_of(opp, struct dcn201_opp, base)

#define OPP_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define OPP_REG_LIST_DCN201(id) \
	OPP_REG_LIST_DCN10(id), \
	OPP_DPG_REG_LIST(id), \
	SRI(FMT_422_CONTROL, FMT, id)

#define OPP_MASK_SH_LIST_DCN201(mask_sh) \
	OPP_MASK_SH_LIST_DCN20(mask_sh)

#define OPP_DCN201_REG_FIELD_LIST(type) \
	OPP_DCN20_REG_FIELD_LIST(type);

struct dcn201_opp_shift {
	OPP_DCN201_REG_FIELD_LIST(uint8_t);
};

struct dcn201_opp_mask {
	OPP_DCN201_REG_FIELD_LIST(uint32_t);
};

struct dcn201_opp_registers {
	OPP_REG_VARIABLE_LIST_DCN2_0;
};

struct dcn201_opp {
	struct output_pixel_processor base;
	const struct  dcn201_opp_registers *regs;
	const struct  dcn201_opp_shift *opp_shift;
	const struct  dcn201_opp_mask *opp_mask;
	bool is_write_to_ram_a_safe;
};

void dcn201_opp_construct(struct dcn201_opp *oppn201,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn201_opp_registers *regs,
	const struct dcn201_opp_shift *opp_shift,
	const struct dcn201_opp_mask *opp_mask);

#endif
