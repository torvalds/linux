/*
 * adv_camera_module.h
 *
 * Generic adi sensor driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * Copyright (C) 2012-2014 Intel Mobile Communications GmbH
 *
 * Copyright (C) 2008 Texas Instruments.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#ifndef ADV_CAMERA_MODULE_H
#define ADV_CAMERA_MODULE_H
#include <linux/workqueue.h>
#include <linux/platform_data/rk_isp10_platform_camera_module.h>
#include <linux/platform_data/rk_isp10_platform.h>

/*
 * TODO: references to v4l2 should be reomved from here and go into a
 * platform dependent wrapper
 */

#define ADV_CAMERA_MODULE_REG_TYPE_DATA PLTFRM_CAMERA_MODULE_REG_TYPE_DATA
#define ADV_CAMERA_MODULE_REG_TYPE_TIMEOUT PLTFRM_CAMERA_MODULE_REG_TYPE_TIMEOUT
#define adv_camera_module_csi_config
#define adv_camera_module_reg pltfrm_camera_module_reg
#define ADV_FLIP_BIT_MASK					0x2
#define ADV_MIRROR_BIT_MASK					0x1

#define ADV_CAMERA_MODULE_CTRL_UPDT_GAIN			0x01
#define ADV_CAMERA_MODULE_CTRL_UPDT_EXP_TIME			0x02
#define ADV_CAMERA_MODULE_CTRL_UPDT_WB_TEMPERATURE		0x04
#define ADV_CAMERA_MODULE_CTRL_UPDT_AUTO_WB			0x08
#define ADV_CAMERA_MODULE_CTRL_UPDT_AUTO_GAIN			0x10
#define ADV_CAMERA_MODULE_CTRL_UPDT_AUTO_EXP			0x20
#define ADV_CAMERA_MODULE_CTRL_UPDT_FOCUS_ABSOLUTE		0x40
#define ADV_CAMERA_MODULE_CTRL_UPDT_PRESET_WB			0x80

enum adv_camera_module_state {
	ADV_CAMERA_MODULE_POWER_OFF = 0,
	ADV_CAMERA_MODULE_HW_STANDBY = 1,
	ADV_CAMERA_MODULE_SW_STANDBY = 2,
	ADV_CAMERA_MODULE_STREAMING = 3
};

struct adv_camera_module;

struct adv_camera_module_timings {
	/* public */
	u32 coarse_integration_time_min;
	u32 coarse_integration_time_max_margin;
	u32 fine_integration_time_min;
	u32 fine_integration_time_max_margin;
	u32 frame_length_lines;
	u32 line_length_pck;
	u32 vt_pix_clk_freq_hz;
	u32 sensor_output_width;
	u32 sensor_output_height;
	u32 crop_horizontal_start; /* Sensor crop start cord. (x0,y0) */
	u32 crop_vertical_start;
	u32 crop_horizontal_end; /* Sensor crop end cord. (x1,y1) */
	u32 crop_vertical_end;
	u8 binning_factor_x;
	u8 binning_factor_y;
	u32 exp_time;
	u32 gain;
};

struct adv_camera_module_config {
	const char *name;
	struct v4l2_mbus_framefmt frm_fmt;
	struct v4l2_subdev_frame_interval frm_intrvl;
	bool auto_exp_enabled;
	bool auto_gain_enabled;
	bool auto_wb_enabled;
	struct adv_camera_module_reg *reg_table;
	u32 reg_table_num_entries;
	struct adv_camera_module_reg *reg_diff_table;
	u32 reg_diff_table_num_entries;
	u32 v_blanking_time_us;
	u32 line_length_pck;
	u32 frame_length_lines;
	struct adv_camera_module_timings timings;
	bool soft_reset;
	bool ignore_measurement_check;

	struct pltfrm_cam_itf itf_cfg;
};

struct adv_camera_module_exp_config {
	s32 exp_time;
	bool auto_exp;
	u16 gain;
	u16 gain_percent;
	bool auto_gain;
	enum v4l2_flash_led_mode flash_mode;
};

struct adv_camera_module_wb_config {
	u32 temperature;
	u32 preset_id;
	bool auto_wb;
};

struct adv_camera_module_af_config {
	u32 abs_pos;
	u32 rel_pos;
};

struct adv_camera_module_ext_ctrl {
	/* public */
	u32 id;
	u32 value;
	__u32 reserved2[1];
};

struct adv_camera_module_ext_ctrls {
	/* public */
	u32 count;
	struct adv_camera_module_ext_ctrl *ctrls;
};

/*
 *	start_streaming: (mandatory) will be called when sensor should be
 *		put into streaming mode right after the base config has been
 *		written to the sensor. After a successful call of this function
 *		the sensor should start delivering frame data.
 *
 *	stop_streaming: (mandatory) will be called when sensor should stop
 *		delivering data. After a successful call of this function the
 *		sensor should not deliver any more frame data.
 *
 *	check_camera_id: (optional) will be called when the sensor is
 *		powered on. If pradvided should check the sensor ID/version
 *		required by the custom driver. Register access should be
 *		possible when this function is invoked.
 *
 *	s_ctrl: (mandatory) will be called at the successful end of
 *		adv_camera_module_s_ctrl with the ctrl_id as argument.
 *
 *	priv: (optional) for private data used by the custom driver.
 */
struct adv_camera_module_custom_config {
	int (*start_streaming)(struct adv_camera_module *cam_mod);
	int (*stop_streaming)(struct adv_camera_module *cam_mod);
	int (*check_camera_id)(struct adv_camera_module *cam_mod);
	int (*s_ctrl)(struct adv_camera_module *cam_mod, u32 ctrl_id);
	int (*g_ctrl)(struct adv_camera_module *cam_mod, u32 ctrl_id);
	int (*g_timings)(struct adv_camera_module *cam_mod,
		struct adv_camera_module_timings *timings);
	int (*g_exposure_valid_frame)(struct adv_camera_module *cam_mod);
	int (*s_ext_ctrls)(struct adv_camera_module *cam_mod,
		struct adv_camera_module_ext_ctrls *ctrls);
	int (*set_flip)(
		struct adv_camera_module *cam_mod,
		struct pltfrm_camera_module_reg reglist[],
		int len);
	int (*init_common)(struct adv_camera_module *cam_mod);
	int (*read_otp)(struct adv_camera_module *cam_mod);
	struct adv_camera_module_config *configs;
	u32 num_configs;
	u32 power_up_delays_ms[3];
	void *priv;
};

struct adv_camera_module_otp_work {
	struct work_struct work;
	struct workqueue_struct *wq;
	void *cam_mod;
};

struct adv_camera_module {
	/* public */
	struct v4l2_subdev sd;
	struct v4l2_mbus_framefmt frm_fmt;
	struct v4l2_subdev_frame_interval frm_intrvl;
	struct adv_camera_module_exp_config exp_config;
	struct adv_camera_module_wb_config wb_config;
	struct adv_camera_module_af_config af_config;
	struct adv_camera_module_custom_config custom;
	enum adv_camera_module_state state;
	enum adv_camera_module_state state_before_suspend;
	struct adv_camera_module_config *active_config;
	struct adv_camera_module_otp_work otp_work;
	u32 ctrl_updt;
	u32 vts_cur;
	u32 vts_min;
	bool auto_adjust_fps;
	bool update_config;
	bool frm_fmt_valid;
	bool frm_intrvl_valid;
	bool hflip;
	bool vflip;
	u32 rotation;
	void *pltfm_data;
	bool inited;
};

#define adv_camera_module_pr_info(cam_mod, fmt, arg...) \
	pltfrm_camera_module_pr_info(&(cam_mod)->sd, fmt, ## arg)
#define adv_camera_module_pr_debug(cam_mod, fmt, arg...) \
		pltfrm_camera_module_pr_debug(&(cam_mod)->sd, fmt, ## arg)
#define adv_camera_module_pr_warn(cam_mod, fmt, arg...) \
		pltfrm_camera_module_pr_warn(&(cam_mod)->sd, fmt, ## arg)
#define adv_camera_module_pr_err(cam_mod, fmt, arg...) \
		pltfrm_camera_module_pr_err(&(cam_mod)->sd, fmt, ## arg)

int adv_camera_module_write_reglist(
	struct adv_camera_module *cam_mod,
	const struct adv_camera_module_reg reglist[],
	int len);

int adv_camera_module_write_reg(
	struct adv_camera_module *cam_mod,
	u8 reg,
	u8 val);

int adv_camera_module_read_reg(
	struct adv_camera_module *cam_mod,
	u8 reg,
	u8 *val);

int adv_camera_module_read_reg_table(
	struct adv_camera_module *cam_mod,
	u16 reg,
	u32 *val);

int adv_camera_module_s_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *format);

int adv_camera_module_g_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *format);

int adv_camera_module_s_frame_interval(
	struct v4l2_subdev *sd,
	struct v4l2_subdev_frame_interval *interval);

int adv_camera_module_s_stream(
	struct v4l2_subdev *sd,
	int enable);

int adv_camera_module_s_power(
	struct v4l2_subdev *sd,
	int on);

int adv_camera_module_g_ctrl(
	struct v4l2_subdev *sd,
	struct v4l2_control *ctrl);

int adv_camera_module_s_ctrl(
	struct v4l2_subdev *sd,
	struct v4l2_control *ctrl);

int adv_camera_module_s_ext_ctrls(
	struct v4l2_subdev *sd,
	struct v4l2_ext_controls *ctrls);

int adv_camera_module_enum_frameintervals(
	struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie);

int adv_camera_module_init(
	struct adv_camera_module *cam_mod,
	struct adv_camera_module_custom_config *custom);

void adv_camera_module_release(
	struct adv_camera_module *cam_mod);

long adv_camera_module_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd,
	void *arg);

int adv_camera_module_get_flip_mirror(
	struct adv_camera_module *cam_mod);
#endif
