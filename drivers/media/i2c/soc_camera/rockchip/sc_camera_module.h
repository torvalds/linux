/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sc_camera_module.h
 *
 * Generic omnivision sensor driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * Copyright (C) 2012-2014 Intel Mobile Communications GmbH
 *
 * Copyright (C) 2008 Texas Instruments.
 *
 */

#ifndef SC_CAMERA_MODULE_H
#define SC_CAMERA_MODULE_H
#include <linux/workqueue.h>
#include <linux/platform_data/rk_isp10_platform_camera_module.h>
#include <linux/platform_data/rk_isp10_platform.h>

/*
 * TODO: references to v4l2 should be reomved from here and go into a
 * platform dependent wrapper
 */

#define SC_CAMERA_MODULE_REG_TYPE_DATA PLTFRM_CAMERA_MODULE_REG_TYPE_DATA
#define SC_CAMERA_MODULE_REG_TYPE_TIMEOUT PLTFRM_CAMERA_MODULE_REG_TYPE_TIMEOUT
#define sc_camera_module_csi_config
#define sc_camera_module_reg pltfrm_camera_module_reg
#define SC_FLIP_BIT_MASK				(1 << PLTFRM_CAMERA_MODULE_FLIP_BIT)
#define SC_MIRROR_BIT_MASK				(1 << PLTFRM_CAMERA_MODULE_MIRROR_BIT)

#define SC_CAMERA_MODULE_CTRL_UPDT_GAIN				0x01
#define SC_CAMERA_MODULE_CTRL_UPDT_EXP_TIME			0x02
#define SC_CAMERA_MODULE_CTRL_UPDT_WB_TEMPERATURE	0x04
#define SC_CAMERA_MODULE_CTRL_UPDT_AUTO_WB			0x08
#define SC_CAMERA_MODULE_CTRL_UPDT_AUTO_GAIN		0x10
#define SC_CAMERA_MODULE_CTRL_UPDT_AUTO_EXP			0x20
#define SC_CAMERA_MODULE_CTRL_UPDT_FOCUS_ABSOLUTE	0x40
#define SC_CAMERA_MODULE_CTRL_UPDT_PRESET_WB		0x80
#define SC_CAMERA_MODULE_CTRL_UPDT_VTS_VALUE	0x100

enum sc_camera_module_state {
	SC_CAMERA_MODULE_POWER_OFF = 0,
	SC_CAMERA_MODULE_HW_STANDBY = 1,
	SC_CAMERA_MODULE_SW_STANDBY = 2,
	SC_CAMERA_MODULE_STREAMING = 3
};

struct sc_camera_module;

struct sc_camera_module_timings {
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

struct sc_camera_module_config {
	const char *name;
	struct v4l2_mbus_framefmt frm_fmt;
	struct v4l2_subdev_frame_interval frm_intrvl;
	u8 auto_exp_enabled;
	u8 auto_gain_enabled;
	u8 auto_wb_enabled;
	struct sc_camera_module_reg *reg_table;
	u32 reg_table_num_entries;
	struct sc_camera_module_reg *reg_diff_table;
	u32 reg_diff_table_num_entries;
	u32 v_blanking_time_us;
	u32 line_length_pck;
	u32 frame_length_lines;
	struct sc_camera_module_timings timings;
	u8 soft_reset;
	u8 ignore_measurement_check;
	u8 max_exp_gain_h;
	u8 max_exp_gain_l;
	struct pltfrm_cam_itf itf_cfg;
};

struct sc_camera_module_exp_config {
	s32 exp_time;
	u8 auto_exp;
	u16 gain;
	u16 gain_percent;
	u8 auto_gain;
	enum v4l2_flash_led_mode flash_mode;
	u32 vts_value;
};

struct sc_camera_module_wb_config {
	u32 temperature;
	u32 preset_id;
	u8 auto_wb;
};

struct sc_camera_module_af_config {
	u32 abs_pos;
	u32 rel_pos;
};

struct sc_camera_module_ext_ctrl {
	/* public */
	u32 id;
	u32 value;
	__u32 reserved2[1];
};

struct sc_camera_module_ext_ctrls {
	/* public */
	u32 count;
	struct sc_camera_module_ext_ctrl *ctrls;
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
 *		powered on. If provided should check the sensor ID/version
 *		required by the custom driver. Register access should be
 *		possible when this function is invoked.
 *
 *	s_ctrl: (mandatory) will be called at the successful end of
 *		sc_camera_module_s_ctrl with the ctrl_id as argument.
 *
 *	priv: (optional) for private data used by the custom driver.
 */
struct sc_camera_module_custom_config {
	int (*start_streaming)(struct sc_camera_module *cam_mod);
	int (*stop_streaming)(struct sc_camera_module *cam_mod);
	int (*check_camera_id)(struct sc_camera_module *cam_mod);
	int (*s_ctrl)(struct sc_camera_module *cam_mod, u32 ctrl_id);
	int (*g_ctrl)(struct sc_camera_module *cam_mod, u32 ctrl_id);
	int (*g_timings)(struct sc_camera_module *cam_mod,
		struct sc_camera_module_timings *timings);
	int (*s_vts)(struct sc_camera_module *cam_mod,
		u32 vts);
	int (*s_ext_ctrls)(struct sc_camera_module *cam_mod,
		struct sc_camera_module_ext_ctrls *ctrls);
	int (*set_flip)(struct sc_camera_module *cam_mod,
		struct pltfrm_camera_module_reg reglist[],
		int len);
	int (*init_common)(struct sc_camera_module *cam_mod);
	int (*read_otp)(struct sc_camera_module *cam_mod);
	struct sc_camera_module_config *configs;
	u32 num_configs;
	u32 power_up_delays_ms[3];
	unsigned short exposure_valid_frame[2];
	void *priv;
};

struct sc_camera_module_otp_work {
	struct work_struct work;
	struct workqueue_struct *wq;
	void *cam_mod;
};

struct sc_camera_module {
	/* public */
	struct v4l2_subdev sd;
	struct v4l2_mbus_framefmt frm_fmt;
	struct v4l2_subdev_frame_interval frm_intrvl;
	struct sc_camera_module_exp_config exp_config;
	struct sc_camera_module_wb_config wb_config;
	struct sc_camera_module_af_config af_config;
	struct sc_camera_module_custom_config custom;
	enum sc_camera_module_state state;
	enum sc_camera_module_state state_before_suspend;
	struct sc_camera_module_config *active_config;
	struct sc_camera_module_otp_work otp_work;
	u32 ctrl_updt;
	u32 vts_cur;
	u32 vts_min;
	u8 auto_adjust_fps;
	u8 update_config;
	u8 frm_fmt_valid;
	u8 frm_intrvl_valid;
	u8 hflip;
	u8 vflip;
	u8 flip_flg;
	u32 rotation;
	void *pltfm_data;
	u8 inited;
	int as_master;
	struct mutex lock;
};

#define sc_camera_module_pr_info(cam_mod, fmt, arg...) \
	pltfrm_camera_module_pr_info(&(cam_mod)->sd, fmt, ## arg)
#define sc_camera_module_pr_debug(cam_mod, fmt, arg...) \
		pltfrm_camera_module_pr_debug(&(cam_mod)->sd, fmt, ## arg)
#define sc_camera_module_pr_warn(cam_mod, fmt, arg...) \
		pltfrm_camera_module_pr_warn(&(cam_mod)->sd, fmt, ## arg)
#define sc_camera_module_pr_err(cam_mod, fmt, arg...) \
		pltfrm_camera_module_pr_err(&(cam_mod)->sd, fmt, ## arg)

int sc_camera_module_write_reglist(struct sc_camera_module *cam_mod,
	const struct sc_camera_module_reg reglist[],
	int len);

int sc_camera_module_write_reg(struct sc_camera_module *cam_mod,
	u16 reg,
	u8 val);

int sc_camera_module_read_reg(struct sc_camera_module *cam_mod,
	u16 data_length,
	u16 reg,
	u32 *val);

int sc_camera_module_read_reg_table(struct sc_camera_module *cam_mod,
	u16 reg,
	u32 *val);

int sc_camera_module_try_fmt(struct v4l2_subdev *sd,
	struct v4l2_mbus_framefmt *fmt);

int sc_camera_module_s_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *format);

int sc_camera_module_g_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *format);

int sc_camera_module_s_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_frame_interval *interval);

int sc_camera_module_g_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_frame_interval *interval);

int sc_camera_module_s_stream(struct v4l2_subdev *sd,
	int enable);

int sc_camera_module_s_power(struct v4l2_subdev *sd,
	int on);

int sc_camera_module_g_ctrl(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl);

int sc_camera_module_s_ctrl(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl);

int sc_camera_module_s_ext_ctrls(struct v4l2_subdev *sd,
	struct v4l2_ext_controls *ctrls);

int sc_camera_module_enum_frameintervals(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie);

int sc_camera_module_init(struct sc_camera_module *cam_mod,
	struct sc_camera_module_custom_config *custom);

void sc_camera_module_release(struct sc_camera_module *cam_mod);

long sc_camera_module_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd,
	void *arg);

int sc_camera_module_get_flip_mirror(struct sc_camera_module *cam_mod);
#endif
