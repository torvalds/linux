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

#ifndef __DC_OPP_DCE110_V_H__
#define __DC_OPP_DCE110_V_H__

#include "dc_types.h"
#include "opp.h"
#include "core_types.h"

bool dce110_opp_v_construct(struct dce110_opp *opp110,
	struct dc_context *ctx);

/* underlay callbacks */
void dce110_opp_v_set_csc_default(
	struct output_pixel_processor *opp,
	const struct default_adjustment *default_adjust);

void dce110_opp_v_set_csc_adjustment(
	struct output_pixel_processor *opp,
	const struct out_csc_color_matrix *tbl_entry);

bool dce110_opp_program_regamma_pwl_v(
	struct output_pixel_processor *opp,
	const struct pwl_params *params);

void dce110_opp_power_on_regamma_lut_v(
	struct output_pixel_processor *opp,
	bool power_on);

void dce110_opp_set_regamma_mode_v(
	struct output_pixel_processor *opp,
	enum opp_regamma mode);

#endif /* __DC_OPP_DCE110_V_H__ */
