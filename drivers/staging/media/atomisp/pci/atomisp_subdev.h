/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
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
#ifndef __ATOMISP_SUBDEV_H__
#define __ATOMISP_SUBDEV_H__

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>
#include "atomisp_common.h"
#include "atomisp_compat.h"
#include "atomisp_v4l2.h"

#include "ia_css.h"

/* EXP_ID's ranger is 1 ~ 250 */
#define ATOMISP_MAX_EXP_ID     (250)

#define ATOMISP_SUBDEV_PAD_SINK			0
#define ATOMISP_SUBDEV_PAD_SOURCE		1
#define ATOMISP_SUBDEV_PADS_NUM			2

struct atomisp_in_fmt_conv {
	u32     code;
	u8 bpp; /* bits per pixel */
	u8 depth; /* uncompressed */
	enum atomisp_input_format atomisp_in_fmt;
	enum ia_css_bayer_order bayer_order;
};

struct atomisp_sub_device;

struct atomisp_video_pipe {
	struct video_device vdev;
	enum v4l2_buf_type type;
	struct media_pad pad;
	struct vb2_queue vb_queue;
	/* Lock for vb_queue, when also taking isp->mutex this must be taken first! */
	struct mutex vb_queue_mutex;
	/* List of video-buffers handed over to the CSS  */
	struct list_head buffers_in_css;
	/* List of video-buffers handed over to the driver, but not yet to the CSS */
	struct list_head activeq;
	/*
	 * the buffers waiting for per-frame parameters, this is only valid
	 * in per-frame setting mode.
	 */
	struct list_head buffers_waiting_for_param;
	/* the link list to store per_frame parameters */
	struct list_head per_frame_params;

	/* Filled through atomisp_get_css_frame_info() on queue setup */
	struct ia_css_frame_info frame_info;

	/* Set from streamoff to disallow queuing further buffers in CSS */
	bool stopping;

	/*
	 * irq_lock is used to protect video buffer state change operations and
	 * also to make activeq and capq operations atomic.
	 */
	spinlock_t irq_lock;
	unsigned int users;

	struct atomisp_device *isp;
	struct v4l2_pix_format pix;
	u32 sh_fmt;

	struct atomisp_sub_device *asd;

	/*
	 * This frame_config_id is got from CSS when dequueues buffers from CSS,
	 * it is used to indicate which parameter it has applied.
	 */
	unsigned int frame_config_id[VIDEO_MAX_FRAME];
	/*
	 * This config id is set when camera HAL enqueues buffer, it has a
	 * non-zero value to indicate which parameter it needs to applu
	 */
	unsigned int frame_request_config_id[VIDEO_MAX_FRAME];
	struct atomisp_css_params_with_list *frame_params[VIDEO_MAX_FRAME];
};

#define vq_to_pipe(queue) \
	container_of(queue, struct atomisp_video_pipe, vb_queue)

#define vb_to_pipe(vb) vq_to_pipe((vb)->vb2_queue)

struct atomisp_pad_format {
	struct v4l2_mbus_framefmt fmt;
	struct v4l2_rect crop;
	struct v4l2_rect compose;
};

/* Internal states for flash process */
enum atomisp_flash_state {
	ATOMISP_FLASH_IDLE,
	ATOMISP_FLASH_REQUESTED,
	ATOMISP_FLASH_ONGOING,
	ATOMISP_FLASH_DONE
};

/*
 * This structure is used to cache the CSS parameters, it aligns to
 * struct ia_css_isp_config but without un-supported and deprecated parts.
 */
struct atomisp_css_params {
	struct ia_css_wb_config   wb_config;
	struct ia_css_cc_config   cc_config;
	struct ia_css_tnr_config  tnr_config;
	struct ia_css_ecd_config  ecd_config;
	struct ia_css_ynr_config  ynr_config;
	struct ia_css_fc_config   fc_config;
	struct ia_css_formats_config formats_config;
	struct ia_css_cnr_config  cnr_config;
	struct ia_css_macc_config macc_config;
	struct ia_css_ctc_config  ctc_config;
	struct ia_css_aa_config   aa_config;
	struct ia_css_aa_config   baa_config;
	struct ia_css_ce_config   ce_config;
	struct ia_css_ob_config   ob_config;
	struct ia_css_dp_config   dp_config;
	struct ia_css_de_config   de_config;
	struct ia_css_gc_config   gc_config;
	struct ia_css_nr_config   nr_config;
	struct ia_css_ee_config   ee_config;
	struct ia_css_anr_config  anr_config;
	struct ia_css_3a_config   s3a_config;
	struct ia_css_xnr_config  xnr_config;
	struct ia_css_dz_config   dz_config;
	struct ia_css_cc_config yuv2rgb_cc_config;
	struct ia_css_cc_config rgb2yuv_cc_config;
	struct ia_css_macc_table  macc_table;
	struct ia_css_gamma_table gamma_table;
	struct ia_css_ctc_table   ctc_table;

	struct ia_css_xnr_table   xnr_table;
	struct ia_css_rgb_gamma_table r_gamma_table;
	struct ia_css_rgb_gamma_table g_gamma_table;
	struct ia_css_rgb_gamma_table b_gamma_table;

	struct ia_css_vector      motion_vector;
	struct ia_css_anr_thres   anr_thres;

	struct ia_css_dvs_6axis_config *dvs_6axis;
	struct ia_css_dvs2_coefficients *dvs2_coeff;
	struct ia_css_shading_table *shading_table;
	struct ia_css_morph_table   *morph_table;

	/*
	 * Used to store the user pointer address of the frame. driver needs to
	 * translate to ia_css_frame * and then set to CSS.
	 */
	void		*output_frame;
	u32	isp_config_id;

	/* Indicates which parameters need to be updated. */
	struct atomisp_parameters update_flag;
};

struct atomisp_subdev_params {
	int yuv_ds_en;
	unsigned int color_effect;
	bool gdc_cac_en;
	bool macc_en;
	bool bad_pixel_en;
	bool video_dis_en;
	bool sc_en;
	bool fpn_en;
	bool xnr_en;
	bool low_light;
	int false_color;
	unsigned int histogram_elenum;

	/* Current grid info */
	struct ia_css_grid_info curr_grid_info;
	enum ia_css_pipe_id s3a_enabled_pipe;

	int s3a_output_bytes;

	bool dis_proj_data_valid;

	struct ia_css_dz_config   dz_config;  /** Digital Zoom */
	struct ia_css_capture_config   capture_config;

	struct ia_css_isp_config config;

	/* current configurations */
	struct atomisp_css_params css_param;

	/*
	 * Intermediate buffers used to communicate data between
	 * CSS and user space.
	 */
	struct ia_css_3a_statistics *s3a_user_stat;

	void *metadata_user[ATOMISP_METADATA_TYPE_NUM];
	u32 metadata_width_size;

	struct ia_css_dvs2_statistics *dvs_stat;
	struct ia_css_dvs_6axis_config *dvs_6axis;
	u32 exp_id;
	int  dvs_hor_coef_bytes;
	int  dvs_ver_coef_bytes;
	int  dvs_ver_proj_bytes;
	int  dvs_hor_proj_bytes;

	/* Flash */
	int num_flash_frames;
	enum atomisp_flash_state flash_state;
	enum atomisp_frame_status last_frame_status;

	/* Flag to check if driver needs to update params to css */
	bool css_update_params_needed;
};

struct atomisp_css_params_with_list {
	/* parameters for CSS */
	struct atomisp_css_params params;
	struct list_head list;
};

struct atomisp_sub_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[ATOMISP_SUBDEV_PADS_NUM];
	struct atomisp_pad_format fmt[ATOMISP_SUBDEV_PADS_NUM];
	/* Padding for currently set sink-pad fmt */
	u32 sink_pad_padding_w;
	u32 sink_pad_padding_h;

	unsigned int output;
	struct atomisp_video_pipe video_out;
	struct atomisp_device *isp;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *run_mode;
	struct v4l2_ctrl *vfpp;
	struct v4l2_ctrl *continuous_raw_buffer_size;
	struct v4l2_ctrl *continuous_viewfinder;
	struct v4l2_ctrl *enable_raw_buffer_lock;

	/* ISP2401 */
	struct v4l2_ctrl *ion_dev_fd;

	struct v4l2_ctrl *disable_dz;

	struct atomisp_subdev_params params;

	struct atomisp_stream_env stream_env[ATOMISP_INPUT_STREAM_NUM];

	struct v4l2_pix_format dvs_envelop;
	unsigned int s3a_bufs_in_css[IA_CSS_PIPE_ID_NUM];
	unsigned int dis_bufs_in_css;

	unsigned int metadata_bufs_in_css
	[ATOMISP_INPUT_STREAM_NUM][IA_CSS_PIPE_ID_NUM];
	/* The list of free and available metadata buffers for CSS */
	struct list_head metadata[ATOMISP_METADATA_TYPE_NUM];
	/* The list of metadata buffers which have been en-queued to CSS */
	struct list_head metadata_in_css[ATOMISP_METADATA_TYPE_NUM];
	/* The list of metadata buffers which are ready for userspace to get */
	struct list_head metadata_ready[ATOMISP_METADATA_TYPE_NUM];

	/* The list of free and available s3a stat buffers for CSS */
	struct list_head s3a_stats;
	/* The list of s3a stat buffers which have been en-queued to CSS */
	struct list_head s3a_stats_in_css;
	/* The list of s3a stat buffers which are ready for userspace to get */
	struct list_head s3a_stats_ready;

	struct list_head dis_stats;
	struct list_head dis_stats_in_css;
	spinlock_t dis_stats_lock;

	struct ia_css_frame *vf_frame; /* TODO: needed? */
	enum atomisp_frame_status frame_status[VIDEO_MAX_FRAME];

	/* This field specifies which camera (v4l2 input) is selected. */
	int input_curr;

	atomic_t sof_count;
	atomic_t sequence;      /* Sequence value that is assigned to buffer. */
	atomic_t sequence_temp;

	/*
	 * Writers of streaming must hold both isp->mutex and isp->lock.
	 * Readers of streaming need to hold only one of the two locks.
	 */
	bool streaming;
	bool stream_prepared; /* whether css stream is created */
	bool recreate_streams_on_resume;

	unsigned int latest_preview_exp_id; /* CSS ZSL/SDV raw buffer id */

	bool copy_mode; /* CSI2+ use copy mode */

	int raw_buffer_bitmap[ATOMISP_MAX_EXP_ID / 32 +
						 1]; /* Record each Raw Buffer lock status */
	int raw_buffer_locked_count;
	spinlock_t raw_buffer_bitmap_lock;

	/* ISP2401 */
	bool re_trigger_capture;

	struct atomisp_resolution sensor_array_res;
	bool high_speed_mode; /* Indicate whether now is a high speed mode */

	unsigned int preview_exp_id;
	unsigned int postview_exp_id;
};

extern const struct atomisp_in_fmt_conv atomisp_in_fmt_conv[];

u32 atomisp_subdev_uncompressed_code(u32 code);
bool atomisp_subdev_is_compressed(u32 code);
const struct atomisp_in_fmt_conv *atomisp_find_in_fmt_conv(u32 code);

/* ISP2400 */
const struct atomisp_in_fmt_conv *atomisp_find_in_fmt_conv_by_atomisp_in_fmt(
    enum atomisp_input_format atomisp_in_fmt);

/* ISP2401 */
const struct atomisp_in_fmt_conv
*atomisp_find_in_fmt_conv_by_atomisp_in_fmt(enum atomisp_input_format
	atomisp_in_fmt);

const struct atomisp_in_fmt_conv *atomisp_find_in_fmt_conv_compressed(u32 code);
bool atomisp_subdev_format_conversion(struct atomisp_sub_device *asd);

/* Get pointer to appropriate format */
struct v4l2_mbus_framefmt
*atomisp_subdev_get_ffmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *sd_state, uint32_t which,
			 uint32_t pad);
struct v4l2_rect *atomisp_subdev_get_rect(struct v4l2_subdev *sd,
	struct v4l2_subdev_state *sd_state,
	u32 which, uint32_t pad,
	uint32_t target);
int atomisp_subdev_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 u32 which, uint32_t pad, uint32_t target,
				 u32 flags, struct v4l2_rect *r);
/* Actually set the format */
void atomisp_subdev_set_ffmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     uint32_t which,
			     u32 pad, struct v4l2_mbus_framefmt *ffmt);

void atomisp_subdev_cleanup_pending_events(struct atomisp_sub_device *asd);

void atomisp_subdev_unregister_entities(struct atomisp_sub_device *asd);
int atomisp_subdev_register_subdev(struct atomisp_sub_device *asd,
				   struct v4l2_device *vdev);
int atomisp_subdev_init(struct atomisp_device *isp);
void atomisp_subdev_cleanup(struct atomisp_device *isp);

#endif /* __ATOMISP_SUBDEV_H__ */
