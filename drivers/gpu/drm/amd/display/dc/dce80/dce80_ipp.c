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

#include "dm_services.h"
#include "include/logger_interface.h"

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "dce80_ipp.h"

#include "dce110/dce110_ipp.h"

static const struct ipp_funcs funcs = {
		.ipp_cursor_set_attributes = dce110_ipp_cursor_set_attributes,
		.ipp_cursor_set_position = dce110_ipp_cursor_set_position,
		.ipp_program_prescale = dce110_ipp_program_prescale,
		.ipp_set_degamma = dce110_ipp_set_degamma,
};

bool dce80_ipp_construct(
	struct dce110_ipp *ipp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_ipp_reg_offsets *offset)
{
	ipp->base.ctx = ctx;

	ipp->base.inst = inst;

	ipp->offsets = *offset;

	ipp->base.funcs = &funcs;

	return true;
}

void dce80_ipp_destroy(struct input_pixel_processor **ipp)
{
	dm_free(TO_DCE80_IPP(*ipp));
	*ipp = NULL;
}
