/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
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

#ifndef	__ATOMISP_CMD_H__
#define	__ATOMISP_CMD_H__

#include "../../include/linux/atomisp.h"
#include <linux/interrupt.h>
#include <linux/videodev2.h>

#include <media/v4l2-subdev.h>

#include "atomisp_internal.h"

#include "ia_css_types.h"
#include "ia_css.h"

struct atomisp_device;
struct ia_css_frame;

#define MSI_ENABLE_BIT		16
#define INTR_DISABLE_BIT	10
#define BUS_MASTER_ENABLE	2
#define MEMORY_SPACE_ENABLE	1
#define INTR_IER		24
#define INTR_IIR		16

/* ISP2401 */
#define RUNMODE_MASK (ATOMISP_RUN_MODE_VIDEO | ATOMISP_RUN_MODE_STILL_CAPTURE \
			| ATOMISP_RUN_MODE_PREVIEW)

/* FIXME: check if can go */
extern int atomisp_punit_hpll_freq;

/* Helper function */
void dump_sp_dmem(struct atomisp_device *isp, unsigned int addr,
		  unsigned int size);
struct camera_mipi_info *atomisp_to_sensor_mipi_info(struct v4l2_subdev *sd);
struct atomisp_video_pipe *atomisp_to_video_pipe(struct video_device *dev);
int atomisp_reset(struct atomisp_device *isp);
int atomisp_buffers_in_css(struct atomisp_video_pipe *pipe);
void atomisp_buffer_done(struct ia_css_frame *frame, enum vb2_buffer_state state);
void atomisp_flush_video_pipe(struct atomisp_video_pipe *pipe, enum vb2_buffer_state state,
			      bool warn_on_css_frames);
void atomisp_flush_bufs_and_wakeup(struct atomisp_sub_device *asd);
void atomisp_clear_css_buffer_counters(struct atomisp_sub_device *asd);

/* Interrupt functions */
void atomisp_msi_irq_init(struct atomisp_device *isp);
void atomisp_msi_irq_uninit(struct atomisp_device *isp);
void atomisp_assert_recovery_work(struct work_struct *work);
void atomisp_setup_flash(struct atomisp_sub_device *asd);
irqreturn_t atomisp_isr(int irq, void *dev);
irqreturn_t atomisp_isr_thread(int irq, void *isp_ptr);
const struct atomisp_format_bridge *get_atomisp_format_bridge_from_mbus(
    u32 mbus_code);
bool atomisp_is_mbuscode_raw(uint32_t code);
void atomisp_delayed_init_work(struct work_struct *work);

/* Get internal fmt according to V4L2 fmt */
bool atomisp_is_viewfinder_support(struct atomisp_device *isp);

/* ISP features control function */

/*
 * Function to set sensor runmode by user when
 * ATOMISP_IOC_S_SENSOR_RUNMODE ioctl was called
 */
int atomisp_set_sensor_runmode(struct atomisp_sub_device *asd,
			       struct atomisp_s_runmode *runmode);
/*
 * Function to enable/disable lens geometry distortion correction (GDC) and
 * chromatic aberration correction (CAC)
 */
int atomisp_gdc_cac(struct atomisp_sub_device *asd, int flag,
		    __s32 *value);

/* Function to enable/disable low light mode (including ANR) */
int atomisp_low_light(struct atomisp_sub_device *asd, int flag,
		      __s32 *value);

/*
 * Function to enable/disable extra noise reduction (XNR) in low light
 * condition
 */
int atomisp_xnr(struct atomisp_sub_device *asd, int flag, int *arg);

int atomisp_formats(struct atomisp_sub_device *asd, int flag,
		    struct atomisp_formats_config *config);

/* Function to configure noise reduction */
int atomisp_nr(struct atomisp_sub_device *asd, int flag,
	       struct atomisp_nr_config *config);

/* Function to configure temporal noise reduction (TNR) */
int atomisp_tnr(struct atomisp_sub_device *asd, int flag,
		struct atomisp_tnr_config *config);

/* Function to configure black level compensation */
int atomisp_black_level(struct atomisp_sub_device *asd, int flag,
			struct atomisp_ob_config *config);

/* Function to configure edge enhancement */
int atomisp_ee(struct atomisp_sub_device *asd, int flag,
	       struct atomisp_ee_config *config);

/* Function to update Gamma table for gamma, brightness and contrast config */
int atomisp_gamma(struct atomisp_sub_device *asd, int flag,
		  struct atomisp_gamma_table *config);

/* Function to update Ctc table for Chroma Enhancement */
int atomisp_ctc(struct atomisp_sub_device *asd, int flag,
		struct atomisp_ctc_table *config);

/* Function to update gamma correction parameters */
int atomisp_gamma_correction(struct atomisp_sub_device *asd, int flag,
			     struct atomisp_gc_config *config);

/* Function to update Gdc table for gdc */
int atomisp_gdc_cac_table(struct atomisp_sub_device *asd, int flag,
			  struct atomisp_morph_table *config);

/* Function to update table for macc */
int atomisp_macc_table(struct atomisp_sub_device *asd, int flag,
		       struct atomisp_macc_config *config);

/* Function to get DIS statistics. */
int atomisp_get_dis_stat(struct atomisp_sub_device *asd,
			 struct atomisp_dis_statistics *stats);

/* Function to get DVS2 BQ resolution settings */
int atomisp_get_dvs2_bq_resolutions(struct atomisp_sub_device *asd,
				    struct atomisp_dvs2_bq_resolutions *bq_res);

/* Function to set the DIS coefficients. */
int atomisp_set_dis_coefs(struct atomisp_sub_device *asd,
			  struct atomisp_dis_coefficients *coefs);

/* Function to set the DIS motion vector. */
int atomisp_set_dis_vector(struct atomisp_sub_device *asd,
			   struct atomisp_dis_vector *vector);

/* Function to set/get 3A stat from isp */
int atomisp_3a_stat(struct atomisp_sub_device *asd, int flag,
		    struct atomisp_3a_statistics *config);

/* Function to get metadata from isp */
int atomisp_get_metadata(struct atomisp_sub_device *asd, int flag,
			 struct atomisp_metadata *config);

int atomisp_get_metadata_by_type(struct atomisp_sub_device *asd, int flag,
				 struct atomisp_metadata_with_type *config);

int atomisp_set_parameters(struct video_device *vdev,
			   struct atomisp_parameters *arg);

/* Function to set/get isp parameters to isp */
int atomisp_param(struct atomisp_sub_device *asd, int flag,
		  struct atomisp_parm *config);

/* Function to configure color effect of the image */
int atomisp_color_effect(struct atomisp_sub_device *asd, int flag,
			 __s32 *effect);

/* Function to configure bad pixel correction */
int atomisp_bad_pixel(struct atomisp_sub_device *asd, int flag,
		      __s32 *value);

/* Function to configure bad pixel correction params */
int atomisp_bad_pixel_param(struct atomisp_sub_device *asd, int flag,
			    struct atomisp_dp_config *config);

/* Function to enable/disable video image stablization */
int atomisp_video_stable(struct atomisp_sub_device *asd, int flag,
			 __s32 *value);

/* Function to configure fixed pattern noise */
int atomisp_fixed_pattern(struct atomisp_sub_device *asd, int flag,
			  __s32 *value);

/* Function to configure fixed pattern noise table */
int atomisp_fixed_pattern_table(struct atomisp_sub_device *asd,
				struct v4l2_framebuffer *config);

/* Function to configure false color correction */
int atomisp_false_color(struct atomisp_sub_device *asd, int flag,
			__s32 *value);

/* Function to configure false color correction params */
int atomisp_false_color_param(struct atomisp_sub_device *asd, int flag,
			      struct atomisp_de_config *config);

/* Function to configure white balance params */
int atomisp_white_balance_param(struct atomisp_sub_device *asd, int flag,
				struct atomisp_wb_config *config);

int atomisp_3a_config_param(struct atomisp_sub_device *asd, int flag,
			    struct atomisp_3a_config *config);

/* Function to setup digital zoom */
int atomisp_digital_zoom(struct atomisp_sub_device *asd, int flag,
			 __s32 *value);

/* Function  set camera_prefiles.xml current sensor pixel array size */
int atomisp_set_array_res(struct atomisp_sub_device *asd,
			  struct atomisp_resolution  *config);

/* Function to calculate real zoom region for every pipe */
int atomisp_calculate_real_zoom_region(struct atomisp_sub_device *asd,
				       struct ia_css_dz_config   *dz_config,
				       enum ia_css_pipe_id css_pipe_id);

int atomisp_cp_general_isp_parameters(struct atomisp_sub_device *asd,
				      struct atomisp_parameters *arg,
				      struct atomisp_css_params *css_param,
				      bool from_user);

int atomisp_cp_lsc_table(struct atomisp_sub_device *asd,
			 struct atomisp_shading_table *source_st,
			 struct atomisp_css_params *css_param,
			 bool from_user);

int atomisp_css_cp_dvs2_coefs(struct atomisp_sub_device *asd,
			      struct ia_css_dvs2_coefficients *coefs,
			      struct atomisp_css_params *css_param,
			      bool from_user);

int atomisp_cp_morph_table(struct atomisp_sub_device *asd,
			   struct atomisp_morph_table *source_morph_table,
			   struct atomisp_css_params *css_param,
			   bool from_user);

int atomisp_cp_dvs_6axis_config(struct atomisp_sub_device *asd,
				struct atomisp_dvs_6axis_config *user_6axis_config,
				struct atomisp_css_params *css_param,
				bool from_user);

int atomisp_makeup_css_parameters(struct atomisp_sub_device *asd,
				  struct atomisp_parameters *arg,
				  struct atomisp_css_params *css_param);

int atomisp_compare_grid(struct atomisp_sub_device *asd,
			 struct atomisp_grid_info *atomgrid);

/* This function looks up the closest available resolution. */
int atomisp_try_fmt(struct video_device *vdev, struct v4l2_pix_format *f,
		    bool *res_overflow);

int atomisp_set_fmt(struct video_device *vdev, struct v4l2_format *f);

int atomisp_set_shading_table(struct atomisp_sub_device *asd,
			      struct atomisp_shading_table *shading_table);

int atomisp_offline_capture_configure(struct atomisp_sub_device *asd,
				      struct atomisp_cont_capture_conf *cvf_config);

void atomisp_free_internal_buffers(struct atomisp_sub_device *asd);

int atomisp_s_ae_window(struct atomisp_sub_device *asd,
			struct atomisp_ae_window *arg);

int  atomisp_flash_enable(struct atomisp_sub_device *asd,
			  int num_frames);

int atomisp_freq_scaling(struct atomisp_device *vdev,
			 enum atomisp_dfs_mode mode,
			 bool force);

void atomisp_buf_done(struct atomisp_sub_device *asd, int error,
		      enum ia_css_buffer_type buf_type,
		      enum ia_css_pipe_id css_pipe_id,
		      bool q_buffers, enum atomisp_input_stream_id stream_id);

void atomisp_css_flush(struct atomisp_device *isp);

/* Events. Only one event has to be exported for now. */
void atomisp_eof_event(struct atomisp_sub_device *asd, uint8_t exp_id);

enum mipi_port_id __get_mipi_port(struct atomisp_device *isp,
				  enum atomisp_camera_port port);

bool atomisp_is_vf_pipe(struct atomisp_video_pipe *pipe);

void atomisp_apply_css_parameters(
    struct atomisp_sub_device *asd,
    struct atomisp_css_params *css_param);
void atomisp_free_css_parameters(struct atomisp_css_params *css_param);

void atomisp_handle_parameter_and_buffer(struct atomisp_video_pipe *pipe);

void atomisp_flush_params_queue(struct atomisp_video_pipe *asd);

/* Function to do Raw Buffer related operation, after enable Lock Unlock Raw Buffer */
int atomisp_exp_id_unlock(struct atomisp_sub_device *asd, int *exp_id);
int atomisp_exp_id_capture(struct atomisp_sub_device *asd, int *exp_id);

void atomisp_init_raw_buffer_bitmap(struct atomisp_sub_device *asd);

/* Function to enable/disable zoom for capture pipe */
int atomisp_enable_dz_capt_pipe(struct atomisp_sub_device *asd,
				unsigned int *enable);

/* Function to get metadata type bu pipe id */
enum atomisp_metadata_type
atomisp_get_metadata_type(struct atomisp_sub_device *asd,
			  enum ia_css_pipe_id pipe_id);

u32 atomisp_get_pixel_depth(u32 pixelformat);

/* Function for HAL to inject a fake event to wake up poll thread */
int atomisp_inject_a_fake_event(struct atomisp_sub_device *asd, int *event);

/*
 * Function for HAL to query how many invalid frames at the beginning of ISP
 * pipeline output
 */
int atomisp_get_invalid_frame_num(struct video_device *vdev,
				  int *invalid_frame_num);

int atomisp_power_off(struct device *dev);
int atomisp_power_on(struct device *dev);
#endif /* __ATOMISP_CMD_H__ */
