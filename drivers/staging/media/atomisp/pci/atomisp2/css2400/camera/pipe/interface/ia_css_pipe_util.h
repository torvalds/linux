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

#ifndef __IA_CSS_PIPE_UTIL_H__
#define __IA_CSS_PIPE_UTIL_H__

#include <ia_css_types.h>
#include <ia_css_frame_public.h>

/* @brief Get Input format bits per pixel based on stream configuration of this
 * pipe.
 *
 * @param[in] pipe
 * @return   bits per pixel for the underlying stream
 *
 */
extern unsigned int ia_css_pipe_util_pipe_input_format_bpp(
	const struct ia_css_pipe * const pipe);

extern void ia_css_pipe_util_create_output_frames(
	struct ia_css_frame *frames[]);

extern void ia_css_pipe_util_set_output_frames(
	struct ia_css_frame *frames[],
	unsigned int idx,
	struct ia_css_frame *frame);

#endif /* __IA_CSS_PIPE_UTIL_H__ */
