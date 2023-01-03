/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Clovertrail PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
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

#ifndef __ATOMISP_COMPAT_H__
#define __ATOMISP_COMPAT_H__

#include "atomisp_compat_css20.h"

#include "../../include/linux/atomisp.h"

struct atomisp_device;
struct atomisp_sub_device;
struct video_device;
enum atomisp_input_stream_id;

struct atomisp_metadata_buf {
	struct ia_css_metadata *metadata;
	void *md_vptr;
	struct list_head list;
};

void atomisp_css2_hw_store_32(hrt_address addr, uint32_t data);
void atomisp_load_uint32(hrt_address addr, uint32_t *data);

int atomisp_css_init(struct atomisp_device *isp);

void atomisp_css_uninit(struct atomisp_device *isp);

void atomisp_css_init_struct(struct atomisp_sub_device *asd);

int atomisp_css_irq_translate(struct atomisp_device *isp,
			      unsigned int *infos);

void atomisp_css_rx_get_irq_info(enum mipi_port_id port,
				 unsigned int *infos);

void atomisp_css_rx_clear_irq_info(enum mipi_port_id port,
				   unsigned int infos);

int atomisp_css_irq_enable(struct atomisp_device *isp,
			   enum ia_css_irq_info info, bool enable);

int atomisp_q_video_buffer_to_css(struct atomisp_sub_device *asd,
				  struct ia_css_frame *frame,
				  enum atomisp_input_stream_id stream_id,
				  enum ia_css_buffer_type css_buf_type,
				  enum ia_css_pipe_id css_pipe_id);

int atomisp_q_s3a_buffer_to_css(struct atomisp_sub_device *asd,
				struct atomisp_s3a_buf *s3a_buf,
				enum atomisp_input_stream_id stream_id,
				enum ia_css_pipe_id css_pipe_id);

int atomisp_q_metadata_buffer_to_css(struct atomisp_sub_device *asd,
				     struct atomisp_metadata_buf *metadata_buf,
				     enum atomisp_input_stream_id stream_id,
				     enum ia_css_pipe_id css_pipe_id);

int atomisp_q_dis_buffer_to_css(struct atomisp_sub_device *asd,
				struct atomisp_dis_buf *dis_buf,
				enum atomisp_input_stream_id stream_id,
				enum ia_css_pipe_id css_pipe_id);

void ia_css_mmu_invalidate_cache(void);

int atomisp_css_start(struct atomisp_sub_device *asd,
		      enum ia_css_pipe_id pipe_id, bool in_reset);

void atomisp_css_update_isp_params(struct atomisp_sub_device *asd);
void atomisp_css_update_isp_params_on_pipe(struct atomisp_sub_device *asd,
	struct ia_css_pipe *pipe);

int atomisp_css_queue_buffer(struct atomisp_sub_device *asd,
			     enum atomisp_input_stream_id stream_id,
			     enum ia_css_pipe_id pipe_id,
			     enum ia_css_buffer_type buf_type,
			     struct atomisp_css_buffer *isp_css_buffer);

int atomisp_css_dequeue_buffer(struct atomisp_sub_device *asd,
			       enum atomisp_input_stream_id stream_id,
			       enum ia_css_pipe_id pipe_id,
			       enum ia_css_buffer_type buf_type,
			       struct atomisp_css_buffer *isp_css_buffer);

int atomisp_css_allocate_stat_buffers(struct atomisp_sub_device *asd,
				      u16 stream_id,
				      struct atomisp_s3a_buf *s3a_buf,
				      struct atomisp_dis_buf *dis_buf,
				      struct atomisp_metadata_buf *md_buf);

void atomisp_css_free_stat_buffers(struct atomisp_sub_device *asd);

void atomisp_css_free_3a_buffer(struct atomisp_s3a_buf *s3a_buf);

void atomisp_css_free_dis_buffer(struct atomisp_dis_buf *dis_buf);

void atomisp_css_free_metadata_buffer(struct atomisp_metadata_buf
				      *metadata_buf);

int atomisp_css_get_grid_info(struct atomisp_sub_device *asd,
			      enum ia_css_pipe_id pipe_id,
			      int source_pad);

int atomisp_alloc_3a_output_buf(struct atomisp_sub_device *asd);

int atomisp_alloc_dis_coef_buf(struct atomisp_sub_device *asd);

int atomisp_alloc_metadata_output_buf(struct atomisp_sub_device *asd);

void atomisp_free_metadata_output_buf(struct atomisp_sub_device *asd);

void atomisp_css_temp_pipe_to_pipe_id(struct atomisp_sub_device *asd,
				      struct atomisp_css_event *current_event);

int atomisp_css_isys_set_resolution(struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    struct v4l2_mbus_framefmt *ffmt,
				    int isys_stream);

void atomisp_css_isys_set_link(struct atomisp_sub_device *asd,
			       enum atomisp_input_stream_id stream_id,
			       int link,
			       int isys_stream);

void atomisp_css_isys_set_valid(struct atomisp_sub_device *asd,
				enum atomisp_input_stream_id stream_id,
				bool valid,
				int isys_stream);

void atomisp_css_isys_set_format(struct atomisp_sub_device *asd,
				 enum atomisp_input_stream_id stream_id,
				 enum atomisp_input_format format,
				 int isys_stream);

int atomisp_css_set_default_isys_config(struct atomisp_sub_device *asd,
					enum atomisp_input_stream_id stream_id,
					struct v4l2_mbus_framefmt *ffmt);

int atomisp_css_isys_two_stream_cfg(struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    enum atomisp_input_format input_format);

void atomisp_css_isys_two_stream_cfg_update_stream1(
    struct atomisp_sub_device *asd,
    enum atomisp_input_stream_id stream_id,
    enum atomisp_input_format input_format,
    unsigned int width, unsigned int height);

void atomisp_css_isys_two_stream_cfg_update_stream2(
    struct atomisp_sub_device *asd,
    enum atomisp_input_stream_id stream_id,
    enum atomisp_input_format input_format,
    unsigned int width, unsigned int height);

int atomisp_css_input_set_resolution(struct atomisp_sub_device *asd,
				     enum atomisp_input_stream_id stream_id,
				     struct v4l2_mbus_framefmt *ffmt);

void atomisp_css_input_set_binning_factor(struct atomisp_sub_device *asd,
	enum atomisp_input_stream_id stream_id,
	unsigned int bin_factor);

void atomisp_css_input_set_bayer_order(struct atomisp_sub_device *asd,
				       enum atomisp_input_stream_id stream_id,
				       enum ia_css_bayer_order bayer_order);

void atomisp_css_input_set_format(struct atomisp_sub_device *asd,
				  enum atomisp_input_stream_id stream_id,
				  enum atomisp_input_format format);

int atomisp_css_input_set_effective_resolution(
    struct atomisp_sub_device *asd,
    enum atomisp_input_stream_id stream_id,
    unsigned int width,
    unsigned int height);

void atomisp_css_video_set_dis_envelope(struct atomisp_sub_device *asd,
					unsigned int dvs_w, unsigned int dvs_h);

void atomisp_css_input_set_two_pixels_per_clock(
    struct atomisp_sub_device *asd,
    bool two_ppc);

void atomisp_css_enable_raw_binning(struct atomisp_sub_device *asd,
				    bool enable);

void atomisp_css_enable_dz(struct atomisp_sub_device *asd, bool enable);

void atomisp_css_capture_set_mode(struct atomisp_sub_device *asd,
				  enum ia_css_capture_mode mode);

void atomisp_css_input_set_mode(struct atomisp_sub_device *asd,
				enum ia_css_input_mode mode);

void atomisp_css_capture_enable_online(struct atomisp_sub_device *asd,
				       unsigned short stream_index, bool enable);

void atomisp_css_preview_enable_online(struct atomisp_sub_device *asd,
				       unsigned short stream_index, bool enable);

void atomisp_css_video_enable_online(struct atomisp_sub_device *asd,
				     bool enable);

void atomisp_css_enable_continuous(struct atomisp_sub_device *asd,
				   bool enable);

void atomisp_css_enable_cvf(struct atomisp_sub_device *asd,
			    bool enable);

int atomisp_css_input_configure_port(struct atomisp_sub_device *asd,
				     enum mipi_port_id port,
				     unsigned int num_lanes,
				     unsigned int timeout,
				     unsigned int mipi_freq,
				     enum atomisp_input_format metadata_format,
				     unsigned int metadata_width,
				     unsigned int metadata_height);

int atomisp_create_pipes_stream(struct atomisp_sub_device *asd);
void atomisp_destroy_pipes_stream_force(struct atomisp_sub_device *asd);

void atomisp_css_stop(struct atomisp_sub_device *asd,
		      enum ia_css_pipe_id pipe_id, bool in_reset);

void atomisp_css_continuous_set_num_raw_frames(
     struct atomisp_sub_device *asd,
     int num_frames);

int atomisp_css_copy_configure_output(struct atomisp_sub_device *asd,
				      unsigned int stream_index,
				      unsigned int width, unsigned int height,
				      unsigned int padded_width,
				      enum ia_css_frame_format format);

int atomisp_css_yuvpp_configure_output(struct atomisp_sub_device *asd,
				       unsigned int stream_index,
				       unsigned int width, unsigned int height,
				       unsigned int padded_width,
				       enum ia_css_frame_format format);

int atomisp_css_yuvpp_get_output_frame_info(
    struct atomisp_sub_device *asd,
    unsigned int stream_index,
    struct ia_css_frame_info *info);

int atomisp_css_yuvpp_get_viewfinder_frame_info(
    struct atomisp_sub_device *asd,
    unsigned int stream_index,
    struct ia_css_frame_info *info);

int atomisp_css_preview_configure_output(struct atomisp_sub_device *asd,
	unsigned int width, unsigned int height,
	unsigned int min_width,
	enum ia_css_frame_format format);

int atomisp_css_capture_configure_output(struct atomisp_sub_device *asd,
	unsigned int width, unsigned int height,
	unsigned int min_width,
	enum ia_css_frame_format format);

int atomisp_css_video_configure_output(struct atomisp_sub_device *asd,
				       unsigned int width, unsigned int height,
				       unsigned int min_width,
				       enum ia_css_frame_format format);

int atomisp_get_css_frame_info(struct atomisp_sub_device *asd,
			       u16 source_pad,
			       struct ia_css_frame_info *frame_info);

int atomisp_css_video_configure_viewfinder(struct atomisp_sub_device *asd,
	unsigned int width, unsigned int height,
	unsigned int min_width,
	enum ia_css_frame_format format);

int atomisp_css_capture_configure_viewfinder(
    struct atomisp_sub_device *asd,
    unsigned int width, unsigned int height,
    unsigned int min_width,
    enum ia_css_frame_format format);

int atomisp_css_video_get_viewfinder_frame_info(
    struct atomisp_sub_device *asd,
    struct ia_css_frame_info *info);

int atomisp_css_capture_get_viewfinder_frame_info(
    struct atomisp_sub_device *asd,
    struct ia_css_frame_info *info);

int atomisp_css_copy_get_output_frame_info(
    struct atomisp_sub_device *asd,
    unsigned int stream_index,
    struct ia_css_frame_info *info);

int atomisp_css_capture_get_output_raw_frame_info(
    struct atomisp_sub_device *asd,
    struct ia_css_frame_info *info);

int atomisp_css_preview_get_output_frame_info(
    struct atomisp_sub_device *asd,
    struct ia_css_frame_info *info);

int atomisp_css_capture_get_output_frame_info(
    struct atomisp_sub_device *asd,
    struct ia_css_frame_info *info);

int atomisp_css_video_get_output_frame_info(
    struct atomisp_sub_device *asd,
    struct ia_css_frame_info *info);

int atomisp_css_preview_configure_pp_input(
    struct atomisp_sub_device *asd,
    unsigned int width, unsigned int height);

int atomisp_css_capture_configure_pp_input(
    struct atomisp_sub_device *asd,
    unsigned int width, unsigned int height);

int atomisp_css_video_configure_pp_input(
    struct atomisp_sub_device *asd,
    unsigned int width, unsigned int height);

int atomisp_css_offline_capture_configure(struct atomisp_sub_device *asd,
	int num_captures, unsigned int skip, int offset);
int atomisp_css_exp_id_capture(struct atomisp_sub_device *asd, int exp_id);
int atomisp_css_exp_id_unlock(struct atomisp_sub_device *asd, int exp_id);

int atomisp_css_capture_enable_xnr(struct atomisp_sub_device *asd,
				   bool enable);

void atomisp_css_set_ctc_table(struct atomisp_sub_device *asd,
			       struct ia_css_ctc_table *ctc_table);

void atomisp_css_video_set_dis_vector(struct atomisp_sub_device *asd,
				      struct atomisp_dis_vector *vector);

void atomisp_css_set_dvs2_coefs(struct atomisp_sub_device *asd,
				struct ia_css_dvs2_coefficients *coefs);

int atomisp_css_set_dis_coefs(struct atomisp_sub_device *asd,
			      struct atomisp_dis_coefficients *coefs);

void atomisp_css_set_zoom_factor(struct atomisp_sub_device *asd,
				 unsigned int zoom);

int atomisp_css_get_wb_config(struct atomisp_sub_device *asd,
			      struct atomisp_wb_config *config);

int atomisp_css_get_ob_config(struct atomisp_sub_device *asd,
			      struct atomisp_ob_config *config);

int atomisp_css_get_dp_config(struct atomisp_sub_device *asd,
			      struct atomisp_dp_config *config);

int atomisp_css_get_de_config(struct atomisp_sub_device *asd,
			      struct atomisp_de_config *config);

int atomisp_css_get_nr_config(struct atomisp_sub_device *asd,
			      struct atomisp_nr_config *config);

int atomisp_css_get_ee_config(struct atomisp_sub_device *asd,
			      struct atomisp_ee_config *config);

int atomisp_css_get_tnr_config(struct atomisp_sub_device *asd,
			       struct atomisp_tnr_config *config);

int atomisp_css_get_ctc_table(struct atomisp_sub_device *asd,
			      struct atomisp_ctc_table *config);

int atomisp_css_get_gamma_table(struct atomisp_sub_device *asd,
				struct atomisp_gamma_table *config);

int atomisp_css_get_gc_config(struct atomisp_sub_device *asd,
			      struct atomisp_gc_config *config);

int atomisp_css_get_3a_config(struct atomisp_sub_device *asd,
			      struct atomisp_3a_config *config);

int atomisp_css_get_formats_config(struct atomisp_sub_device *asd,
				   struct atomisp_formats_config *formats_config);

void atomisp_css_set_formats_config(struct atomisp_sub_device *asd,
				    struct ia_css_formats_config *formats_config);

int atomisp_css_get_zoom_factor(struct atomisp_sub_device *asd,
				unsigned int *zoom);

struct ia_css_shading_table *atomisp_css_shading_table_alloc(
    unsigned int width, unsigned int height);

void atomisp_css_set_shading_table(struct atomisp_sub_device *asd,
				   struct ia_css_shading_table *table);

void atomisp_css_shading_table_free(struct ia_css_shading_table *table);

struct ia_css_morph_table *atomisp_css_morph_table_allocate(
    unsigned int width, unsigned int height);

void atomisp_css_set_morph_table(struct atomisp_sub_device *asd,
				 struct ia_css_morph_table *table);

void atomisp_css_get_morph_table(struct atomisp_sub_device *asd,
				 struct ia_css_morph_table *table);

void atomisp_css_morph_table_free(struct ia_css_morph_table *table);

int atomisp_css_get_dis_stat(struct atomisp_sub_device *asd,
			     struct atomisp_dis_statistics *stats);

int atomisp_css_update_stream(struct atomisp_sub_device *asd);

int atomisp_css_isr_thread(struct atomisp_device *isp,
			   bool *frame_done_found,
			   bool *css_pipe_done);

bool atomisp_css_valid_sof(struct atomisp_device *isp);

void atomisp_en_dz_capt_pipe(struct atomisp_sub_device *asd, bool enable);

#endif
