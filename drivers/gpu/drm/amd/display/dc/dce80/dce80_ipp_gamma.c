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
#include "include/fixed31_32.h"
#include "basics/conversion.h"

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "dce80_ipp.h"
#include "dce110/dce110_ipp.h"
#include "gamma_types.h"

#define DCP_REG(reg)\
	(reg + ipp80->offsets.dcp_offset)

enum {
	MAX_INPUT_LUT_ENTRY = 256
};

/*PROTOTYPE DECLARATIONS*/

static void set_legacy_input_gamma_mode(
	struct dce110_ipp *ipp80,
	bool is_legacy);

void dce80_ipp_set_legacy_input_gamma_mode(
		struct input_pixel_processor *ipp,
		bool is_legacy)
{
	struct dce110_ipp *ipp80 = TO_DCE80_IPP(ipp);

	set_legacy_input_gamma_mode(ipp80, is_legacy);
}

static void set_legacy_input_gamma_mode(
	struct dce110_ipp *ipp80,
	bool is_legacy)
{
	const uint32_t addr = DCP_REG(mmINPUT_GAMMA_CONTROL);
	uint32_t value = dm_read_reg(ipp80->base.ctx, addr);

	set_reg_field_value(
		value,
		!is_legacy,
		INPUT_GAMMA_CONTROL,
		GRPH_INPUT_GAMMA_MODE);

	dm_write_reg(ipp80->base.ctx, addr, value);
}

