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

#ifndef _SH_CSS_LEGACY_H_
#define _SH_CSS_LEGACY_H_

#include <type_support.h>
#include <ia_css_err.h>
#include <ia_css_types.h>
#include <ia_css_frame_public.h>
#include <ia_css_pipe_public.h>
#include <ia_css_stream_public.h>

/* The pipe id type, distinguishes the kind of pipes that
 *  can be run in parallel.
 */
enum ia_css_pipe_id {
	IA_CSS_PIPE_ID_PREVIEW,
	IA_CSS_PIPE_ID_COPY,
	IA_CSS_PIPE_ID_VIDEO,
	IA_CSS_PIPE_ID_CAPTURE,
	IA_CSS_PIPE_ID_YUVPP,
#ifndef ISP2401
	IA_CSS_PIPE_ID_ACC,
	IA_CSS_PIPE_ID_NUM
#else
	IA_CSS_PIPE_ID_ACC
#endif
};
#ifdef ISP2401
#define IA_CSS_PIPE_ID_NUM (IA_CSS_PIPE_ID_ACC+1)
#endif

struct ia_css_pipe_extra_config {
	bool enable_raw_binning;
	bool enable_yuv_ds;
	bool enable_high_speed;
	bool enable_dvs_6axis;
	bool enable_reduced_pipe;
	bool enable_fractional_ds;
	bool disable_vf_pp;
};

enum ia_css_err
ia_css_pipe_create_extra(const struct ia_css_pipe_config *config,
			 const struct ia_css_pipe_extra_config *extra_config,
			 struct ia_css_pipe **pipe);

void
ia_css_pipe_extra_config_defaults(struct ia_css_pipe_extra_config *extra_config);

enum ia_css_err
ia_css_temp_pipe_to_pipe_id(const struct ia_css_pipe *pipe,
			    enum ia_css_pipe_id *pipe_id);

/* DEPRECATED. FPN is not supported. */
enum ia_css_err
sh_css_set_black_frame(struct ia_css_stream *stream,
			const struct ia_css_frame *raw_black_frame);

#ifndef ISP2401
void
sh_css_enable_cont_capt(bool enable, bool stop_copy_preview);

#endif
#endif /* _SH_CSS_LEGACY_H_ */
