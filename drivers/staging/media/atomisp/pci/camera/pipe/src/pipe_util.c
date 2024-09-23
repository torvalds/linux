// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "ia_css_pipe_util.h"
#include "ia_css_frame_public.h"
#include "ia_css_pipe.h"
#include "ia_css_util.h"
#include "assert_support.h"

unsigned int ia_css_pipe_util_pipe_input_format_bpp(
    const struct ia_css_pipe *const pipe)
{
	assert(pipe);
	assert(pipe->stream);

	return ia_css_util_input_format_bpp(pipe->stream->config.input_config.format,
					    pipe->stream->config.pixels_per_clock == 2);
}

void ia_css_pipe_util_create_output_frames(
    struct ia_css_frame *frames[])
{
	unsigned int i;

	assert(frames);
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
