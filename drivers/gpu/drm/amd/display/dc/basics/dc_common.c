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

#include "core_types.h"
#include "dc_common.h"
#include "basics/conversion.h"

bool is_rgb_cspace(enum dc_color_space output_color_space)
{
	switch (output_color_space) {
	case COLOR_SPACE_SRGB:
	case COLOR_SPACE_SRGB_LIMITED:
	case COLOR_SPACE_2020_RGB_FULLRANGE:
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
	case COLOR_SPACE_ADOBERGB:
		return true;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_LIMITED:
	case COLOR_SPACE_YCBCR709_LIMITED:
	case COLOR_SPACE_2020_YCBCR_LIMITED:
	case COLOR_SPACE_2020_YCBCR_FULL:
		return false;
	default:
		/* Add a case to switch */
		BREAK_TO_DEBUGGER();
		return false;
	}
}

bool is_lower_pipe_tree_visible(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->plane_state && pipe_ctx->plane_state->visible)
		return true;
	if (pipe_ctx->bottom_pipe && is_lower_pipe_tree_visible(pipe_ctx->bottom_pipe))
		return true;
	return false;
}

bool is_upper_pipe_tree_visible(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->plane_state && pipe_ctx->plane_state->visible)
		return true;
	if (pipe_ctx->top_pipe && is_upper_pipe_tree_visible(pipe_ctx->top_pipe))
		return true;
	return false;
}

bool is_pipe_tree_visible(struct pipe_ctx *pipe_ctx)
{
	if (pipe_ctx->plane_state && pipe_ctx->plane_state->visible)
		return true;
	if (pipe_ctx->top_pipe && is_upper_pipe_tree_visible(pipe_ctx->top_pipe))
		return true;
	if (pipe_ctx->bottom_pipe && is_lower_pipe_tree_visible(pipe_ctx->bottom_pipe))
		return true;
	return false;
}

void build_prescale_params(struct  dc_bias_and_scale *bias_and_scale,
		const struct dc_plane_state *plane_state)
{
	if (plane_state->format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN
			&& plane_state->format != SURFACE_PIXEL_FORMAT_INVALID
			&& plane_state->input_csc_color_matrix.enable_adjustment
			&& plane_state->coeff_reduction_factor.value != 0) {
		bias_and_scale->scale_blue = fixed_point_to_int_frac(
			dc_fixpt_mul(plane_state->coeff_reduction_factor,
					dc_fixpt_from_fraction(256, 255)),
				2,
				13);
		bias_and_scale->scale_red = bias_and_scale->scale_blue;
		bias_and_scale->scale_green = bias_and_scale->scale_blue;
	} else {
		bias_and_scale->scale_blue = 0x2000;
		bias_and_scale->scale_red = 0x2000;
		bias_and_scale->scale_green = 0x2000;
	}
}

