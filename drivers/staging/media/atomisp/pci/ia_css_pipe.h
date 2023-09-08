/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_PIPE_H__
#define __IA_CSS_PIPE_H__

#include <type_support.h>
#include "ia_css_stream.h"
#include "ia_css_frame.h"
#include "ia_css_pipeline.h"
#include "ia_css_binary.h"
#include "sh_css_legacy.h"

#define PIPE_ENTRY_EMPTY_TOKEN                (~0U)
#define PIPE_ENTRY_RESERVED_TOKEN             (0x1)

struct ia_css_preview_settings {
	struct ia_css_binary copy_binary;
	struct ia_css_binary preview_binary;
	struct ia_css_binary vf_pp_binary;

	/* 2401 only for these two - do we in fact use them for anything real */
	struct ia_css_frame *delay_frames[MAX_NUM_VIDEO_DELAY_FRAMES];
	struct ia_css_frame *tnr_frames[NUM_VIDEO_TNR_FRAMES];

	struct ia_css_pipe *copy_pipe;
	struct ia_css_pipe *capture_pipe;
};

#define IA_CSS_DEFAULT_PREVIEW_SETTINGS { \
	.copy_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.preview_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.vf_pp_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
}

struct ia_css_capture_settings {
	struct ia_css_binary copy_binary;
	/* we extend primary binary to multiple stages because in ISP2.6.1
	 * the computation load is too high to fit in one single binary. */
	struct ia_css_binary primary_binary[MAX_NUM_PRIMARY_STAGES];
	unsigned int num_primary_stage;
	struct ia_css_binary pre_isp_binary;
	struct ia_css_binary anr_gdc_binary;
	struct ia_css_binary post_isp_binary;
	struct ia_css_binary capture_pp_binary;
	struct ia_css_binary vf_pp_binary;
	struct ia_css_binary capture_ldc_binary;
	struct ia_css_binary *yuv_scaler_binary;
	struct ia_css_frame *delay_frames[MAX_NUM_VIDEO_DELAY_FRAMES];
	bool *is_output_stage;
	unsigned int num_yuv_scaler;
};

#define IA_CSS_DEFAULT_CAPTURE_SETTINGS { \
	.copy_binary		= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.primary_binary		= {IA_CSS_BINARY_DEFAULT_SETTINGS}, \
	.pre_isp_binary		= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.anr_gdc_binary		= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.post_isp_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.capture_pp_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.vf_pp_binary		= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.capture_ldc_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
}

struct ia_css_video_settings {
	struct ia_css_binary copy_binary;
	struct ia_css_binary video_binary;
	struct ia_css_binary vf_pp_binary;
	struct ia_css_binary *yuv_scaler_binary;
	struct ia_css_frame *delay_frames[MAX_NUM_VIDEO_DELAY_FRAMES];
	struct ia_css_frame *tnr_frames[NUM_VIDEO_TNR_FRAMES];
	struct ia_css_frame *vf_pp_in_frame;
	struct ia_css_pipe *copy_pipe;
	struct ia_css_pipe *capture_pipe;
	bool *is_output_stage;
	unsigned int num_yuv_scaler;
};

#define IA_CSS_DEFAULT_VIDEO_SETTINGS { \
	.copy_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.video_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
	.vf_pp_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
}

struct ia_css_yuvpp_settings {
	struct ia_css_binary copy_binary;
	struct ia_css_binary *yuv_scaler_binary;
	struct ia_css_binary *vf_pp_binary;
	bool *is_output_stage;
	unsigned int num_yuv_scaler;
	unsigned int num_vf_pp;
	unsigned int num_output;
};

#define IA_CSS_DEFAULT_YUVPP_SETTINGS { \
	.copy_binary	= IA_CSS_BINARY_DEFAULT_SETTINGS, \
}

struct osys_object;

struct ia_css_pipe {
	/* TODO: Remove stop_requested and use stop_requested in the pipeline */
	bool                            stop_requested;
	struct ia_css_pipe_config       config;
	struct ia_css_pipe_extra_config extra_config;
	struct ia_css_pipe_info         info;
	enum ia_css_pipe_id		mode;
	struct ia_css_shading_table	*shading_table;
	struct ia_css_pipeline		pipeline;
	struct ia_css_frame_info	output_info[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	struct ia_css_frame_info	bds_output_info;
	struct ia_css_frame_info	vf_output_info[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	struct ia_css_frame_info	out_yuv_ds_input_info;
	struct ia_css_frame_info	vf_yuv_ds_input_info;
	struct ia_css_fw_info		*output_stage;	/* extra output stage */
	struct ia_css_fw_info		*vf_stage;	/* extra vf_stage */
	unsigned int			required_bds_factor;
	unsigned int			dvs_frame_delay;
	int				num_invalid_frames;
	bool				enable_viewfinder[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	struct ia_css_stream		*stream;
	struct ia_css_frame		in_frame_struct;
	struct ia_css_frame		out_frame_struct;
	struct ia_css_frame		vf_frame_struct;
	struct ia_css_frame		*continuous_frames[NUM_CONTINUOUS_FRAMES];
	struct ia_css_metadata	*cont_md_buffers[NUM_CONTINUOUS_FRAMES];
	union {
		struct ia_css_preview_settings preview;
		struct ia_css_video_settings   video;
		struct ia_css_capture_settings capture;
		struct ia_css_yuvpp_settings yuvpp;
	} pipe_settings;
	ia_css_ptr scaler_pp_lut;
	struct osys_object *osys_obj;

	/* This number is unique per pipe each instance of css. This number is
	 * reused as pipeline number also. There is a 1-1 mapping between pipe_num
	 * and sp thread id. Current logic limits pipe_num to
	 * SH_CSS_MAX_SP_THREADS */
	unsigned int pipe_num;
};

#define IA_CSS_DEFAULT_PIPE { \
	.config			= DEFAULT_PIPE_CONFIG, \
	.info			= DEFAULT_PIPE_INFO, \
	.mode			= IA_CSS_PIPE_ID_VIDEO, /* (pipe_id) */ \
	.pipeline		= DEFAULT_PIPELINE, \
	.output_info		= {IA_CSS_BINARY_DEFAULT_FRAME_INFO}, \
	.bds_output_info	= IA_CSS_BINARY_DEFAULT_FRAME_INFO, \
	.vf_output_info		= {IA_CSS_BINARY_DEFAULT_FRAME_INFO}, \
	.out_yuv_ds_input_info	= IA_CSS_BINARY_DEFAULT_FRAME_INFO, \
	.vf_yuv_ds_input_info	= IA_CSS_BINARY_DEFAULT_FRAME_INFO, \
	.required_bds_factor	= SH_CSS_BDS_FACTOR_1_00, \
	.dvs_frame_delay	= 1, \
	.enable_viewfinder	= {true}, \
	.in_frame_struct	= DEFAULT_FRAME, \
	.out_frame_struct	= DEFAULT_FRAME, \
	.vf_frame_struct	= DEFAULT_FRAME, \
	.pipe_settings		= { \
		.preview = IA_CSS_DEFAULT_PREVIEW_SETTINGS \
	}, \
	.pipe_num		= PIPE_ENTRY_EMPTY_TOKEN, \
}

void ia_css_pipe_map_queue(struct ia_css_pipe *pipe, bool map);

int
sh_css_param_update_isp_params(struct ia_css_pipe *curr_pipe,
			       struct ia_css_isp_parameters *params,
			       bool commit, struct ia_css_pipe *pipe);

#endif /* __IA_CSS_PIPE_H__ */
