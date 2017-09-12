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

#ifndef __DC_IPP_DCE80_H__
#define __DC_IPP_DCE80_H__

#include "ipp.h"

#define TO_DCE80_IPP(input_pixel_processor)\
		container_of(input_pixel_processor, struct dce110_ipp, base)

struct dce110_ipp;
struct dce110_ipp_reg_offsets;
struct gamma_parameters;
struct dev_c_lut;

bool dce80_ipp_construct(
	struct dce110_ipp *ipp,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_ipp_reg_offsets *offset);

void dce80_ipp_destroy(struct input_pixel_processor **ipp);

#endif /*__DC_IPP_DCE80_H__*/
