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

#ifndef __DC_IPP_DCE110_H__
#define __DC_IPP_DCE110_H__

#include "ipp.h"

struct gamma_parameters;
struct dev_c_lut;

#define TO_DCE110_IPP(input_pixel_processor)\
	container_of(input_pixel_processor, struct dce110_ipp, base)

struct dce110_ipp_reg_offsets {
	uint32_t dcp_offset;
};

struct dce110_ipp {
	struct input_pixel_processor base;
	struct dce110_ipp_reg_offsets offsets;
};

bool dce110_ipp_construct(
	struct dce110_ipp* ipp,
	struct dc_context *ctx,
	enum controller_id id,
	const struct dce110_ipp_reg_offsets *offset);

void dce110_ipp_destroy(struct input_pixel_processor **ipp);

/* CURSOR RELATED */
void dce110_ipp_cursor_set_position(
	struct input_pixel_processor *ipp,
	const struct dc_cursor_position *position);

bool dce110_ipp_cursor_set_attributes(
	struct input_pixel_processor *ipp,
	const struct dc_cursor_attributes *attributes);

/* DEGAMMA RELATED */
bool dce110_ipp_set_degamma(
	struct input_pixel_processor *ipp,
	enum ipp_degamma_mode mode);

void dce110_ipp_program_prescale(
	struct input_pixel_processor *ipp,
	struct ipp_prescale_params *params);
/*
 * Helper functions to be resused in other ASICs
 */
void dce110_helper_select_lut(struct dce110_ipp *ipp110);

#endif /*__DC_IPP_DCE110_H__*/
