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

/* include DCE8 register header files */
#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "dce80_opp.h"

#define FROM_OPP(opp)\
	container_of(opp, struct dce80_opp, base)

enum {
	MAX_LUT_ENTRY = 256,
	MAX_NUMBER_OF_ENTRIES = 256
};

static const struct dce80_opp_reg_offsets reg_offsets[] = {
{
	.fmt_offset = (mmFMT0_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.crtc_offset = (mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP0_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{	.fmt_offset = (mmFMT1_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.crtc_offset = (mmCRTC1_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP1_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{	.fmt_offset = (mmFMT2_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.crtc_offset = (mmCRTC2_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP2_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.fmt_offset = (mmFMT3_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.crtc_offset = (mmCRTC3_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP3_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.fmt_offset = (mmFMT4_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.crtc_offset = (mmCRTC4_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP4_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
},
{
	.fmt_offset = (mmFMT5_FMT_CONTROL - mmFMT0_FMT_CONTROL),
	.crtc_offset = (mmCRTC5_DCFE_MEM_LIGHT_SLEEP_CNTL -
					mmCRTC0_DCFE_MEM_LIGHT_SLEEP_CNTL),
	.dcp_offset = (mmDCP5_GRPH_CONTROL - mmDCP0_GRPH_CONTROL),
}
};

static const struct opp_funcs funcs = {
		.opp_power_on_regamma_lut = dce80_opp_power_on_regamma_lut,
		.opp_set_csc_adjustment = dce80_opp_set_csc_adjustment,
		.opp_set_csc_default = dce80_opp_set_csc_default,
		.opp_set_dyn_expansion = dce80_opp_set_dyn_expansion,
		.opp_program_regamma_pwl = dce80_opp_program_regamma_pwl,
		.opp_set_regamma_mode = dce80_opp_set_regamma_mode,
		.opp_destroy = dce80_opp_destroy,
		.opp_program_fmt = dce110_opp_program_fmt,
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce80_opp_construct(struct dce80_opp *opp80,
	struct dc_context *ctx,
	uint32_t inst)
{
	if (inst >= ARRAY_SIZE(reg_offsets))
		return false;

	opp80->base.funcs = &funcs;

	opp80->base.ctx = ctx;

	opp80->base.inst = inst;

	opp80->offsets = reg_offsets[inst];

	return true;
}

void dce80_opp_destroy(struct output_pixel_processor **opp)
{
	dm_free(FROM_OPP(*opp));
	*opp = NULL;
}

struct output_pixel_processor *dce80_opp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce80_opp *opp =
		dm_alloc(sizeof(struct dce80_opp));

	if (!opp)
		return NULL;

	if (dce80_opp_construct(opp,
			ctx, inst))
		return &opp->base;

	BREAK_TO_DEBUGGER();
	dm_free(opp);
	return NULL;
}

