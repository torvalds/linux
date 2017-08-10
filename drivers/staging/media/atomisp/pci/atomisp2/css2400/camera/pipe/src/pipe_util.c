/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "ia_css_pipe_util.h"
#include "ia_css_frame_public.h"
#include "ia_css_pipe.h"
#include "ia_css_util.h"
#include "assert_support.h"

unsigned int ia_css_pipe_util_pipe_input_format_bpp(
	const struct ia_css_pipe * const pipe)
{
	assert(pipe != NULL);
	assert(pipe->stream != NULL);

	return ia_css_util_input_format_bpp(pipe->stream->config.input_config.format,
			  pipe->stream->config.pixels_per_clock == 2);
}

void ia_css_pipe_util_create_output_frames(
	struct ia_css_frame *frames[])
{
	unsigned int i;

	assert(frames != NULL);
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++) {
		frames[i] = NULL;
	}
}

void ia_css_pipe_util_set_output_frames(
	struct ia_css_frame *frames[],
	unsigned int idx,
	struct ia_css_frame *frame)
{
	assert(idx < IA_CSS_BINARY_MAX_OUTPUT_PORTS);

	frames[idx] = frame;
}

