/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "dm_services.h"
#include "dcn10_ipp.h"
#include "reg_helper.h"

#define REG(reg) \
	(ippn10->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	ippn10->ipp_shift->field_name, ippn10->ipp_mask->field_name

#define CTX \
	ippn10->base.ctx

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

static void dcn10_ipp_destroy(struct input_pixel_processor **ipp)
{
	kfree(TO_DCN10_IPP(*ipp));
	*ipp = NULL;
}

static const struct ipp_funcs dcn10_ipp_funcs = {
	.ipp_destroy			= dcn10_ipp_destroy
};

static const struct ipp_funcs dcn20_ipp_funcs = {
	.ipp_destroy			= dcn10_ipp_destroy
};

void dcn10_ipp_construct(
	struct dcn10_ipp *ippn10,
	struct dc_context *ctx,
	int inst,
	const struct dcn10_ipp_registers *regs,
	const struct dcn10_ipp_shift *ipp_shift,
	const struct dcn10_ipp_mask *ipp_mask)
{
	ippn10->base.ctx = ctx;
	ippn10->base.inst = inst;
	ippn10->base.funcs = &dcn10_ipp_funcs;

	ippn10->regs = regs;
	ippn10->ipp_shift = ipp_shift;
	ippn10->ipp_mask = ipp_mask;
}

void dcn20_ipp_construct(
	struct dcn10_ipp *ippn10,
	struct dc_context *ctx,
	int inst,
	const struct dcn10_ipp_registers *regs,
	const struct dcn10_ipp_shift *ipp_shift,
	const struct dcn10_ipp_mask *ipp_mask)
{
	ippn10->base.ctx = ctx;
	ippn10->base.inst = inst;
	ippn10->base.funcs = &dcn20_ipp_funcs;

	ippn10->regs = regs;
	ippn10->ipp_shift = ipp_shift;
	ippn10->ipp_mask = ipp_mask;
}
