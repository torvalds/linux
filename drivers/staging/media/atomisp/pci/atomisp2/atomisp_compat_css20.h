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

#define atomisp_css_pipe_id ia_css_pipe_id
#define atomisp_css_pipeline	ia_css_pipe
#define atomisp_css_buffer_type ia_css_buffer_type
#define atomisp_css_dis_data ia_css_isp_dvs_statistics
#define atomisp_css_irq_info  ia_css_irq_info
#define atomisp_css_isp_config ia_css_isp_config
#define atomisp_css_bayer_order ia_css_bayer_order
#define atomisp_css_capture_mode ia_css_capture_mode
#define atomisp_css_input_mode ia_css_input_mode
#define atomisp_css_frame ia_css_frame
#define atomisp_css_frame_format ia_css_frame_format
#define atomisp_css_frame_info ia_css_frame_info
#define atomisp_css_dp_config	ia_css_dp_config
#define atomisp_css_wb_config	ia_css_wb_config
#define atomisp_css_cc_config	ia_css_cc_config
#define atomisp_css_nr_config	ia_css_nr_config
#define atomisp_css_ee_config	ia_css_ee_config
#define atomisp_css_ob_config	ia_css_ob_config
#define atomisp_css_de_config	ia_css_de_config
#define atomisp_css_dz_config	ia_css_dz_config
#define atomisp_css_ce_config	ia_css_ce_config
#define atomisp_css_gc_config	ia_css_gc_config
#define atomisp_css_tnr_config	ia_css_tnr_config
#define atomisp_css_cnr_config	ia_css_cnr_config
#define atomisp_css_ctc_config	ia_css_ctc_config
#define atomisp_css_3a_config	ia_css_3a_config
#define atomisp_css_ecd_config	ia_css_ecd_config
#define atomisp_css_ynr_config	ia_css_ynr_config
#define atomisp_css_fc_config	ia_css_fc_config
#define atomisp_css_aa_config	ia_css_aa_config
#define atomisp_css_baa_config	ia_css_aa_config
#define atomisp_css_anr_config	ia_css_anr_config
#define atomisp_css_xnr_config	ia_css_xnr_config
#define atomisp_css_macc_config	ia_css_macc_config
#define atomisp_css_gamma_table	ia_css_gamma_table
#define atomisp_css_ctc_table	ia_css_ctc_table
#define atomisp_css_macc_table	ia_css_macc_table
#define atomisp_css_xnr_table	ia_css_xnr_table
#define atomisp_css_rgb_gamma_table	ia_css_rgb_gamma_table
#define atomisp_css_anr_thres	ia_css_anr_thres
#define atomisp_css_dvs_6axis	ia_css_dvs_6axis_config
#define atomisp_css_grid_info	ia_css_grid_info
#define atomisp_css_3a_grid_info	ia_css_3a_grid_info
#define atomisp_css_dvs_grid_info	ia_css_dvs_grid_info
#define atomisp_css_shading_table	ia_css_shading_table
#define atomisp_css_morph_table	ia_css_morph_table
#define atomisp_css_dvs_6axis_config	ia_css_dvs_6axis_config
#define atomisp_css_fw_info	ia_css_fw_info
#define atomisp_css_formats_config	ia_css_formats_config

#define CSS_PIPE_ID_PREVIEW	IA_CSS_PIPE_ID_PREVIEW
#define CSS_PIPE_ID_COPY	IA_CSS_PIPE_ID_COPY
#define CSS_PIPE_ID_VIDEO	IA_CSS_PIPE_ID_VIDEO
#define CSS_PIPE_ID_CAPTURE	IA_CSS_PIPE_ID_CAPTURE
#define CSS_PIPE_ID_ACC		IA_CSS_PIPE_ID_ACC
#define CSS_PIPE_ID_YUVPP	IA_CSS_PIPE_ID_YUVPP
#define CSS_PIPE_ID_NUM		IA_CSS_PIPE_ID_NUM

#define CSS_INPUT_MODE_SENSOR	IA_CSS_INPUT_MODE_BUFFERED_SENSOR
#define CSS_INPUT_MODE_FIFO	IA_CSS_INPUT_MODE_FIFO
#define CSS_INPUT_MODE_TPG	IA_CSS_INPUT_MODE_TPG
#define CSS_INPUT_MODE_PRBS	IA_CSS_INPUT_MODE_PRBS
#define CSS_INPUT_MODE_MEMORY	IA_CSS_INPUT_MODE_MEMORY

#define CSS_IRQ_INFO_CSS_RECEIVER_ERROR	IA_CSS_IRQ_INFO_CSS_RECEIVER_ERROR
#define CSS_IRQ_INFO_EVENTS_READY	IA_CSS_IRQ_INFO_EVENTS_READY
#define CSS_IRQ_INFO_INPUT_SYSTEM_ERROR \
	IA_CSS_IRQ_INFO_INPUT_SYSTEM_ERROR
#define CSS_IRQ_INFO_IF_ERROR	IA_CSS_IRQ_INFO_IF_ERROR

#define CSS_BUFFER_TYPE_NUM	IA_CSS_BUFFER_TYPE_NUM

#define CSS_FRAME_FLASH_STATE_NONE	IA_CSS_FRAME_FLASH_STATE_NONE
#define CSS_FRAME_FLASH_STATE_PARTIAL	IA_CSS_FRAME_FLASH_STATE_PARTIAL
#define CSS_FRAME_FLASH_STATE_FULL	IA_CSS_FRAME_FLASH_STATE_FULL

#define CSS_BAYER_ORDER_GRBG	IA_CSS_BAYER_ORDER_GRBG
#define CSS_BAYER_ORDER_RGGB	IA_CSS_BAYER_ORDER_RGGB
#define CSS_BAYER_ORDER_BGGR	IA_CSS_BAYER_ORDER_BGGR
#define CSS_BAYER_ORDER_GBRG	IA_CSS_BAYER_ORDER_GBRG

/*
 * Hide IA_ naming difference in otherwise common CSS macros.
 */
#define CSS_ID(val)	(IA_ ## val)
#define CSS_EVENT(val)	(IA_CSS_EVENT_TYPE_ ## val)
#define CSS_FORMAT(val)	(ATOMISP_INPUT_FORMAT_ ## val)

#define CSS_EVENT_PORT_EOF	CSS_EVENT(PORT_EOF)
#define CSS_EVENT_FRAME_TAGGED	CSS_EVENT(FRAME_TAGGED)

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
	struct atomisp_css_dis_data *dis_data;
	struct ia_css_isp_dvs_statistics_map *dvs_map;
	struct list_head list;
};

struct atomisp_css_buffer {
	struct ia_css_buffer css_buffer;
};

struct atomisp_css_event {
	enum atomisp_css_pipe_id pipe;
	struct ia_css_event event;
};

void atomisp_css_set_macc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_macc_config *macc_config);

void atomisp_css_set_ecd_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ecd_config *ecd_config);

void atomisp_css_set_ynr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ynr_config *ynr_config);

void atomisp_css_set_fc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_fc_config *fc_config);

void atomisp_css_set_aa_config(struct atomisp_sub_device *asd,
			struct atomisp_css_aa_config *aa_config);

void atomisp_css_set_baa_config(struct atomisp_sub_device *asd,
			struct atomisp_css_baa_config *baa_config);

void atomisp_css_set_anr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_anr_config *anr_config);

void atomisp_css_set_xnr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_xnr_config *xnr_config);

void atomisp_css_set_cnr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_cnr_config *cnr_config);

void atomisp_css_set_ctc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ctc_config *ctc_config);

void atomisp_css_set_yuv2rgb_cc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_cc_config *yuv2rgb_cc_config);

void atomisp_css_set_rgb2yuv_cc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_cc_config *rgb2yuv_cc_config);

void atomisp_css_set_xnr_table(struct atomisp_sub_device *asd,
			struct atomisp_css_xnr_table *xnr_table);

void atomisp_css_set_r_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_css_rgb_gamma_table *r_gamma_table);

void atomisp_css_set_g_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_css_rgb_gamma_table *g_gamma_table);

void atomisp_css_set_b_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_css_rgb_gamma_table *b_gamma_table);

void atomisp_css_set_anr_thres(struct atomisp_sub_device *asd,
			struct atomisp_css_anr_thres *anr_thres);

int atomisp_css_check_firmware_version(struct atomisp_device *isp);

int atomisp_css_load_firmware(struct atomisp_device *isp);

void atomisp_css_unload_firmware(struct atomisp_device *isp);

void atomisp_css_set_dvs_6axis(struct atomisp_sub_device *asd,
			struct atomisp_css_dvs_6axis *dvs_6axis);

unsigned int atomisp_css_debug_get_dtrace_level(void);

int atomisp_css_debug_dump_isp_binary(void);

int atomisp_css_dump_sp_raw_copy_linecount(bool reduced);

int atomisp_css_dump_blob_infor(void);

void atomisp_css_set_isp_config_id(struct atomisp_sub_device *asd,
			uint32_t isp_config_id);

void atomisp_css_set_isp_config_applied_frame(struct atomisp_sub_device *asd,
			struct atomisp_css_frame *output_frame);

int atomisp_get_css_dbgfunc(void);

int atomisp_set_css_dbgfunc(struct atomisp_device *isp, int opt);
struct atomisp_css_dvs_grid_info *atomisp_css_get_dvs_grid_info(
	struct atomisp_css_grid_info *grid_info);
#endif
