/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Clovertrail PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */

#ifndef __ATOMISP_COMPAT_CSS20_H__
#define __ATOMISP_COMPAT_CSS20_H__

#include <media/v4l2-mediabus.h>

#include "ia_css.h"
#include "ia_css_types.h"
#include "ia_css_acc_types.h"
#include "sh_css_legacy.h"

#define ATOMISP_CSS2_PIPE_MAX	2
#define ATOMISP_CSS2_NUM_OFFLINE_INIT_CONTINUOUS_FRAMES     3
#define ATOMISP_CSS2_NUM_OFFLINE_INIT_CONTINUOUS_FRAMES_LOCK_EN     4
#define ATOMISP_CSS2_NUM_DVS_FRAME_DELAY     2

#define CSS_MIPI_FRAME_BUFFER_SIZE_1	0x60000
#define CSS_MIPI_FRAME_BUFFER_SIZE_2	0x80000

struct atomisp_device;
struct atomisp_sub_device;

#define MAX_STREAMS_PER_CHANNEL	2

/*
 * These are used to indicate the css stream state, corresponding
 * stream handling can be done via judging the different state.
 */
enum atomisp_css_stream_state {
	CSS_STREAM_UNINIT,
	CSS_STREAM_CREATED,
	CSS_STREAM_STARTED,
	CSS_STREAM_STOPPED,
};

/*
 *  Sensor of external ISP can send multiple steams with different mipi data
 * type in the same virtual channel. This information needs to come from the
 * sensor or external ISP
 */
struct atomisp_css_isys_config_info {
	unsigned int input_format;
	unsigned int width;
	unsigned int height;
};

struct atomisp_stream_env {
	struct ia_css_stream *stream;
	struct ia_css_stream_config stream_config;
	struct ia_css_stream_info stream_info;
	struct ia_css_pipe *pipes[IA_CSS_PIPE_ID_NUM];
	struct ia_css_pipe *multi_pipes[IA_CSS_PIPE_ID_NUM];
	struct ia_css_pipe_config pipe_configs[IA_CSS_PIPE_ID_NUM];
	struct ia_css_pipe_extra_config pipe_extra_configs[IA_CSS_PIPE_ID_NUM];
	bool update_pipe[IA_CSS_PIPE_ID_NUM];
	enum atomisp_css_stream_state stream_state;
	struct ia_css_stream *acc_stream;
	enum atomisp_css_stream_state acc_stream_state;
	struct ia_css_stream_config acc_stream_config;
	unsigned int ch_id; /* virtual channel ID */
	unsigned int isys_configs;
	struct atomisp_css_isys_config_info isys_info[MAX_STREAMS_PER_CHANNEL];
};

struct atomisp_css_env {
	struct ia_css_env isp_css_env;
	struct ia_css_fw isp_css_fw;
};

struct atomisp_s3a_buf {
	struct ia_css_isp_3a_statistics *s3a_data;
	struct ia_css_isp_3a_statistics_map *s3a_map;
	struct list_head list;
};

struct atomisp_dis_buf {
	struct ia_css_isp_dvs_statistics *dis_data;
	struct ia_css_isp_dvs_statistics_map *dvs_map;
	struct list_head list;
};

struct atomisp_css_buffer {
	struct ia_css_buffer css_buffer;
};

struct atomisp_css_event {
	enum ia_css_pipe_id pipe;
	struct ia_css_event event;
};

void atomisp_css_set_macc_config(struct atomisp_sub_device *asd,
				 struct ia_css_macc_config *macc_config);

void atomisp_css_set_ecd_config(struct atomisp_sub_device *asd,
				struct ia_css_ecd_config *ecd_config);

void atomisp_css_set_ynr_config(struct atomisp_sub_device *asd,
				struct ia_css_ynr_config *ynr_config);

void atomisp_css_set_fc_config(struct atomisp_sub_device *asd,
			       struct ia_css_fc_config *fc_config);

void atomisp_css_set_aa_config(struct atomisp_sub_device *asd,
			       struct ia_css_aa_config *aa_config);

void atomisp_css_set_baa_config(struct atomisp_sub_device *asd,
				struct ia_css_aa_config *baa_config);

void atomisp_css_set_anr_config(struct atomisp_sub_device *asd,
				struct ia_css_anr_config *anr_config);

void atomisp_css_set_xnr_config(struct atomisp_sub_device *asd,
				struct ia_css_xnr_config *xnr_config);

void atomisp_css_set_cnr_config(struct atomisp_sub_device *asd,
				struct ia_css_cnr_config *cnr_config);

void atomisp_css_set_ctc_config(struct atomisp_sub_device *asd,
				struct ia_css_ctc_config *ctc_config);

void atomisp_css_set_yuv2rgb_cc_config(struct atomisp_sub_device *asd,
				       struct ia_css_cc_config *yuv2rgb_cc_config);

void atomisp_css_set_rgb2yuv_cc_config(struct atomisp_sub_device *asd,
				       struct ia_css_cc_config *rgb2yuv_cc_config);

void atomisp_css_set_anr_thres(struct atomisp_sub_device *asd,
			       struct ia_css_anr_thres *anr_thres);

int atomisp_css_load_firmware(struct atomisp_device *isp);

void atomisp_css_set_dvs_6axis(struct atomisp_sub_device *asd,
			       struct ia_css_dvs_6axis_config *dvs_6axis);

int atomisp_css_debug_dump_isp_binary(void);

int atomisp_css_dump_sp_raw_copy_linecount(bool reduced);

int atomisp_css_dump_blob_infor(void);

void atomisp_css_set_isp_config_id(struct atomisp_sub_device *asd,
				   uint32_t isp_config_id);

void atomisp_css_set_isp_config_applied_frame(struct atomisp_sub_device *asd,
	struct ia_css_frame *output_frame);

int atomisp_get_css_dbgfunc(void);

int atomisp_set_css_dbgfunc(struct atomisp_device *isp, int opt);
struct ia_css_dvs_grid_info *atomisp_css_get_dvs_grid_info(
    struct ia_css_grid_info *grid_info);
#endif
