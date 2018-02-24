/*
 * tc_camera_module.c
 *
 * Generic toshiba sensor driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * Copyright (C) 2012-2014 Intel Mobile Communications GmbH
 *
 * Copyright (C) 2008 Texas Instruments.
 *
 * Author:zhoupeng<benjo.zhou@rock-chips.com>
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#include <linux/delay.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/videobuf-core.h>
#include <linux/slab.h>
#include <linux/gcd.h>
#include <linux/i2c.h>
#include <media/v4l2-controls_rockchip.h>
#include <linux/platform_data/rk_isp10_platform.h>

#include "tc_camera_module.h"

#define I2C_M_WR 0
#define I2C_MSG_MAX 300

static struct tc_camera_module *to_tc_camera_module(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tc_camera_module, sd);
}

/* ======================================================================== */

static void tc_camera_module_reset(
	struct tc_camera_module *cam_mod)
{
	pltfrm_camera_module_pr_debug(&cam_mod->sd, "\n");

	cam_mod->inited = false;
	cam_mod->active_config = NULL;
	cam_mod->update_config = true;
	cam_mod->frm_fmt_valid = false;
	cam_mod->frm_intrvl_valid = false;
	cam_mod->exp_config.auto_exp = false;
	cam_mod->exp_config.auto_gain = false;
	cam_mod->wb_config.auto_wb = false;
	cam_mod->hflip = false;
	cam_mod->vflip = false;
	cam_mod->auto_adjust_fps = true;
	cam_mod->rotation = 0;
	cam_mod->ctrl_updt = 0;
	cam_mod->state = TC_CAMERA_MODULE_POWER_OFF;
	cam_mod->state_before_suspend = TC_CAMERA_MODULE_POWER_OFF;

	cam_mod->exp_config.exp_time = 0;
	cam_mod->exp_config.gain = 0;
	cam_mod->vts_cur = 0;
}

/* ======================================================================== */

static void tc_camera_module_set_active_config(
	struct tc_camera_module *cam_mod,
	struct tc_camera_module_config *new_config)
{
	pltfrm_camera_module_pr_debug(&cam_mod->sd, "\n");

	if (IS_ERR_OR_NULL(new_config)) {
		cam_mod->active_config = new_config;
		pltfrm_camera_module_pr_err(&cam_mod->sd,
			"no active config\n");
	} else {
		cam_mod->ctrl_updt &= TC_CAMERA_MODULE_CTRL_UPDT_AUTO_EXP |
			TC_CAMERA_MODULE_CTRL_UPDT_AUTO_GAIN |
			TC_CAMERA_MODULE_CTRL_UPDT_AUTO_WB;
		if (new_config->auto_exp_enabled !=
			cam_mod->exp_config.auto_exp) {
			cam_mod->ctrl_updt |=
				TC_CAMERA_MODULE_CTRL_UPDT_AUTO_EXP;
			cam_mod->exp_config.auto_exp =
				new_config->auto_exp_enabled;
		}
		if (new_config->auto_gain_enabled !=
			cam_mod->exp_config.auto_gain) {
			cam_mod->ctrl_updt |=
				TC_CAMERA_MODULE_CTRL_UPDT_AUTO_GAIN;
			cam_mod->exp_config.auto_gain =
				new_config->auto_gain_enabled;
		}
		if (new_config->auto_wb_enabled !=
			cam_mod->wb_config.auto_wb) {
			cam_mod->ctrl_updt |=
				TC_CAMERA_MODULE_CTRL_UPDT_AUTO_WB;
			cam_mod->wb_config.auto_wb =
				new_config->auto_wb_enabled;
		}
		if (new_config != cam_mod->active_config) {
			cam_mod->update_config = true;
			cam_mod->active_config = new_config;
			pltfrm_camera_module_pr_err(&cam_mod->sd,
				"activating config '%s'\n",
				cam_mod->active_config->name);
		}
	}
}

/* ======================================================================== */

static struct tc_camera_module_config *tc_camera_module_find_config(
	struct tc_camera_module *cam_mod,
	struct v4l2_mbus_framefmt *fmt,
	struct v4l2_subdev_frame_interval *frm_intrvl)
{
	u32 i;
	unsigned long gcdiv;
	struct v4l2_subdev_frame_interval norm_interval;

	if (!IS_ERR_OR_NULL(fmt))
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"%dx%d, fmt code 0x%04x\n",
			fmt->width, fmt->height, fmt->code);

	if (!IS_ERR_OR_NULL(frm_intrvl))
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"frame interval %d/%d\n",
			frm_intrvl->interval.numerator,
			frm_intrvl->interval.denominator);

	for (i = 0; i < cam_mod->custom.num_configs; i++) {
		if (!IS_ERR_OR_NULL(frm_intrvl)) {
			gcdiv = gcd(cam_mod->custom.configs[i].frm_intrvl.interval.numerator,
				    cam_mod->custom.configs[i].frm_intrvl.interval.denominator);
			norm_interval.interval.numerator =
				cam_mod->custom.configs[i].frm_intrvl.interval.numerator / gcdiv;
			norm_interval.interval.denominator =
				cam_mod->custom.configs[i].frm_intrvl.interval.denominator / gcdiv;
			if ((frm_intrvl->interval.numerator !=
				norm_interval.interval.numerator) ||
				(frm_intrvl->interval.denominator !=
				norm_interval.interval.denominator))
				continue;
		}
		if (!IS_ERR_OR_NULL(fmt)) {
			if ((cam_mod->custom.configs[i].frm_fmt.width !=
				fmt->width) ||
				(cam_mod->custom.configs[i].frm_fmt.height !=
				fmt->height) ||
				(cam_mod->custom.configs[i].frm_fmt.code !=
				fmt->code)) {
				continue;
			}
		}
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"found matching config %s\n",
			cam_mod->custom.configs[i].name);
		return &cam_mod->custom.configs[i];
	}
	pltfrm_camera_module_pr_debug(&cam_mod->sd,
		"no matching config found\n");

	return ERR_PTR(-EINVAL);
}

/* ======================================================================== */

static int tc_camera_module_write_config(
	struct tc_camera_module *cam_mod)
{
	int ret = 0;
	struct tc_camera_module_reg *reg_table;
	u32 reg_table_num_entries;

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "\n");

	if (IS_ERR_OR_NULL(cam_mod->active_config)) {
		pltfrm_camera_module_pr_err(&cam_mod->sd,
			"no active sensor configuration");
		ret = -EFAULT;
		goto err;
	}

	if (!cam_mod->inited) {
		cam_mod->active_config->soft_reset = true;
		reg_table = cam_mod->active_config->reg_table;
		reg_table_num_entries =
			cam_mod->active_config->reg_table_num_entries;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
				"write config %s\n",
				cam_mod->active_config->name);
	} else {
		if (cam_mod->active_config->reg_diff_table &&
		    cam_mod->active_config->reg_diff_table_num_entries) {
			cam_mod->active_config->soft_reset = false;
			reg_table = cam_mod->active_config->reg_diff_table;
			reg_table_num_entries =
				cam_mod->active_config->reg_diff_table_num_entries;
			pltfrm_camera_module_pr_debug(&cam_mod->sd,
				"write config %s%s\n",
				cam_mod->active_config->name, "_diff");
		} else {
			cam_mod->active_config->soft_reset = true;
			reg_table = cam_mod->active_config->reg_table;
			reg_table_num_entries =
				cam_mod->active_config->reg_table_num_entries;
			pltfrm_camera_module_pr_debug(&cam_mod->sd,
				"write config %s\n",
				cam_mod->active_config->name);
		}
	}

	if (!IS_ERR_OR_NULL(cam_mod->custom.set_flip))
		cam_mod->custom.set_flip(cam_mod,
			reg_table, reg_table_num_entries);

	ret = tc_camera_module_write_reglist(cam_mod,
		reg_table, reg_table_num_entries);
	if (IS_ERR_VALUE(ret))
		goto err;
	ret = pltfrm_camera_module_patch_config(&cam_mod->sd,
		&cam_mod->frm_fmt,
		&cam_mod->frm_intrvl);
	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	pltfrm_camera_module_pr_err(&cam_mod->sd,
		"failed with error %d\n", ret);
	return ret;
}

static int tc_camera_module_attach(
	struct tc_camera_module *cam_mod)
{
	int ret = 0;
	struct tc_camera_module_custom_config *custom;

	pltfrm_camera_module_pr_err(&cam_mod->sd, "enter\n");

	custom = &cam_mod->custom;

	if (custom->check_camera_id) {
		tc_camera_module_s_power(&cam_mod->sd, 1);
		ret = custom->check_camera_id(cam_mod);
		tc_camera_module_s_power(&cam_mod->sd, 0);
		if (ret != 0)
			goto err;
	}

	return 0;
err:
	pltfrm_camera_module_pr_err(&cam_mod->sd,
		"failed with error %d\n", ret);
	tc_camera_module_release(cam_mod);
	return ret;
}

/* ======================================================================== */

int tc_camera_module_try_fmt(struct v4l2_subdev *sd,
	struct v4l2_mbus_framefmt *fmt)
{
	struct tc_camera_module *cam_mod = to_tc_camera_module(sd);

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "%dx%d, fmt code 0x%04x\n",
		fmt->width, fmt->height, fmt->code);

	if (IS_ERR_OR_NULL(tc_camera_module_find_config(cam_mod, fmt, NULL))) {
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"format not supported\n");
		return -EINVAL;
	}
	pltfrm_camera_module_pr_debug(&cam_mod->sd, "format supported\n");

	return 0;
}

/* ======================================================================== */

int tc_camera_module_s_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *format)
{
	struct tc_camera_module *cam_mod =  to_tc_camera_module(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;
	int ret = 0;

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "%dx%d, fmt code 0x%04x\n",
		fmt->width, fmt->height, fmt->code);

	if (IS_ERR_OR_NULL(tc_camera_module_find_config(cam_mod, fmt, NULL))) {
		pltfrm_camera_module_pr_err(&cam_mod->sd,
			"format %dx%d, code 0x%04x, not supported\n",
			fmt->width, fmt->height, fmt->code);
		ret = -EINVAL;
		goto err;
	}
	cam_mod->frm_fmt_valid = true;
	cam_mod->frm_fmt = *fmt;
	if (cam_mod->frm_intrvl_valid) {
		tc_camera_module_set_active_config(cam_mod,
			tc_camera_module_find_config(cam_mod,
				fmt, &cam_mod->frm_intrvl));
	}
	return 0;
err:
	pltfrm_camera_module_pr_err(&cam_mod->sd,
		"failed with error %d\n", ret);
	return ret;
}

/* ======================================================================== */

int tc_camera_module_g_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *format)
{
	struct tc_camera_module *cam_mod =  to_tc_camera_module(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "\n");

	if (cam_mod->active_config) {
		fmt->code = cam_mod->active_config->frm_fmt.code;
		fmt->width = cam_mod->active_config->frm_fmt.width;
		fmt->height = cam_mod->active_config->frm_fmt.height;
		return 0;
	}

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "no active config\n");

	return -1;
}

/* ======================================================================== */

int tc_camera_module_s_frame_interval(
	struct v4l2_subdev *sd,
	struct v4l2_subdev_frame_interval *interval)
{
	struct tc_camera_module *cam_mod = to_tc_camera_module(sd);
	unsigned long gcdiv;
	struct v4l2_subdev_frame_interval norm_interval;
	int ret = 0;

	if ((interval->interval.denominator == 0) ||
		(interval->interval.numerator == 0)) {
		pltfrm_camera_module_pr_err(&cam_mod->sd,
			"invalid frame interval %d/%d\n",
			interval->interval.numerator,
			interval->interval.denominator);
		ret = -EINVAL;
		goto err;
	}

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "%d/%d (%dfps)\n",
		interval->interval.numerator, interval->interval.denominator,
		(interval->interval.denominator +
		(interval->interval.numerator >> 1)) /
		interval->interval.numerator);

	/* normalize interval */
	gcdiv = gcd(interval->interval.numerator,
		interval->interval.denominator);
	norm_interval.interval.numerator =
		interval->interval.numerator / gcdiv;
	norm_interval.interval.denominator =
		interval->interval.denominator / gcdiv;

	if (IS_ERR_OR_NULL(tc_camera_module_find_config(cam_mod,
			NULL, &norm_interval))) {
		pltfrm_camera_module_pr_err(&cam_mod->sd,
			"frame interval %d/%d not supported\n",
			interval->interval.numerator,
			interval->interval.denominator);
		ret = -EINVAL;
		goto err;
	}
	cam_mod->frm_intrvl_valid = true;
	cam_mod->frm_intrvl = norm_interval;
	if (cam_mod->frm_fmt_valid) {
		tc_camera_module_set_active_config(cam_mod,
			tc_camera_module_find_config(cam_mod,
				&cam_mod->frm_fmt, interval));
	}
	return 0;
err:
	pltfrm_camera_module_pr_err(&cam_mod->sd,
		"failed with error %d\n", ret);
	return ret;
}

/* ======================================================================== */

int tc_camera_module_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	struct tc_camera_module *cam_mod =  to_tc_camera_module(sd);

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "%d\n", enable);

	if (enable) {
		if (cam_mod->state == TC_CAMERA_MODULE_STREAMING)
			return 0;
		if (IS_ERR_OR_NULL(cam_mod->active_config)) {
			pltfrm_camera_module_pr_err(&cam_mod->sd,
				"no active sensor configuration, cannot start streaming\n");
			ret = -EFAULT;
			goto err;
		}
		if (cam_mod->state != TC_CAMERA_MODULE_SW_STANDBY) {
			pltfrm_camera_module_pr_err(&cam_mod->sd,
				"sensor is not powered on (in state %d), cannot start streaming\n",
				cam_mod->state);
			ret = -EINVAL;
			goto err;
		}
		if (cam_mod->update_config) {
			ret = tc_camera_module_write_config(cam_mod);
			if (IS_ERR_VALUE(ret))
				goto err;
		}

		ret = cam_mod->custom.enable_stream(cam_mod, true);
		if (IS_ERR_VALUE(ret))
			goto err;
		if (!cam_mod->inited && cam_mod->update_config)
			cam_mod->inited = true;
		cam_mod->update_config = false;
		cam_mod->ctrl_updt = 0;
		mdelay(cam_mod->custom.power_up_delays_ms[2]);
		cam_mod->state = TC_CAMERA_MODULE_STREAMING;
	} else {
		int pclk;
		int wait_ms;
		struct isp_supplemental_sensor_mode_data timings;

		if (cam_mod->state != TC_CAMERA_MODULE_STREAMING)
			return 0;
		ret = cam_mod->custom.enable_stream(cam_mod, false);
		if (IS_ERR_VALUE(ret))
			goto err;

		ret = tc_camera_module_ioctl(sd,
					RK_VIDIOC_SENSOR_MODE_DATA,
					&timings);

		cam_mod->state = TC_CAMERA_MODULE_SW_STANDBY;

		if (IS_ERR_VALUE(ret))
			goto err;

		pclk = timings.vt_pix_clk_freq_hz / 1000;

		if (!pclk)
			goto err;

		wait_ms =
			(timings.line_length_pck *
			timings.frame_length_lines) /
			pclk;

		/*
		 * wait for a frame period to make sure that there is
		 * no pending frame left.
		 */

		msleep(wait_ms + 1);
	}

	cam_mod->state_before_suspend = cam_mod->state;

	return 0;
err:
	pltfrm_camera_module_pr_err(&cam_mod->sd,
		"failed with error %d\n", ret);
	return ret;
}

/* ======================================================================== */

int tc_camera_module_s_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;
	struct tc_camera_module *cam_mod =  to_tc_camera_module(sd);
	struct v4l2_subdev *af_ctrl;

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "%d\n", on);

	if (on) {
		if (cam_mod->state == TC_CAMERA_MODULE_POWER_OFF) {
			cam_mod->custom.s_power(cam_mod, 1);
			ret = pltfrm_camera_module_s_power(&cam_mod->sd, 1);
			if (!IS_ERR_VALUE(ret)) {
				mdelay(cam_mod->custom.power_up_delays_ms[0]);
				cam_mod->state = TC_CAMERA_MODULE_HW_STANDBY;
			}
		}
		if (cam_mod->state == TC_CAMERA_MODULE_HW_STANDBY) {
			ret = pltfrm_camera_module_set_pin_state(&cam_mod->sd,
				PLTFRM_CAMERA_MODULE_PIN_PD,
				PLTFRM_CAMERA_MODULE_PIN_STATE_INACTIVE);
			if (!IS_ERR_VALUE(ret)) {
				mdelay(cam_mod->custom.power_up_delays_ms[1]);
				cam_mod->state = TC_CAMERA_MODULE_SW_STANDBY;
				if (!IS_ERR_OR_NULL(
				    cam_mod->custom.init_common) &&
				    cam_mod->custom.init_common(
				    cam_mod))
					usleep_range(1000, 1500);

				af_ctrl = pltfrm_camera_module_get_af_ctrl(sd);
				if (!IS_ERR_OR_NULL(af_ctrl)) {
					v4l2_subdev_call(af_ctrl,
							 core, init, 0);
				}
			}
		}
		if (cam_mod->update_config) {
			tc_camera_module_write_config(cam_mod);
			cam_mod->update_config = false;
		}
	} else {
		if (cam_mod->state == TC_CAMERA_MODULE_STREAMING) {
			ret = tc_camera_module_s_stream(sd, 0);
			if (!IS_ERR_VALUE(ret))
				cam_mod->state = TC_CAMERA_MODULE_SW_STANDBY;
		}
		if (cam_mod->state == TC_CAMERA_MODULE_SW_STANDBY) {
			ret = pltfrm_camera_module_set_pin_state(
				&cam_mod->sd,
				PLTFRM_CAMERA_MODULE_PIN_PD,
				PLTFRM_CAMERA_MODULE_PIN_STATE_ACTIVE);

			if (!IS_ERR_VALUE(ret))
				cam_mod->state = TC_CAMERA_MODULE_HW_STANDBY;
		}
		if (cam_mod->state == TC_CAMERA_MODULE_HW_STANDBY) {
			cam_mod->custom.s_power(cam_mod, 0);
			ret = pltfrm_camera_module_s_power(&cam_mod->sd, 0);
			if (!IS_ERR_VALUE(ret)) {
				cam_mod->state = TC_CAMERA_MODULE_POWER_OFF;
				tc_camera_module_reset(cam_mod);
			}
		}
	}

	cam_mod->state_before_suspend = cam_mod->state;

	if (IS_ERR_VALUE(ret)) {
		pltfrm_camera_module_pr_err(&cam_mod->sd,
			"%s failed, camera left in state %d\n",
			on ? "on" : "off", cam_mod->state);
		goto err;
	} else {
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"camera powered %s\n", on ? "on" : "off");
	}

	return 0;
err:
	pltfrm_camera_module_pr_err(&cam_mod->sd,
		"failed with error %d\n", ret);
	return ret;
}

/* ======================================================================== */

int tc_camera_module_g_ctrl(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct tc_camera_module *cam_mod = to_tc_camera_module(sd);
	int ret;

	pltfrm_camera_module_pr_debug(&cam_mod->sd, " id 0x%x\n", ctrl->id);

	if (ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_CAPTURE) {
		ctrl->value = 2;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"V4L2_CID_MIN_BUFFERS_FOR_CAPTURE %d\n",
			ctrl->value);
		return 0;
	}

	if (ctrl->id == V4L2_CID_FLASH_LED_MODE) {
		ctrl->value = cam_mod->exp_config.flash_mode;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"V4L2_CID_FLASH_LED_MODE %d\n",
			ctrl->value);
		return 0;
	}

	if (IS_ERR_OR_NULL(cam_mod->active_config)) {
		pltfrm_camera_module_pr_err(&cam_mod->sd,
			"no active configuration\n");
		return -EFAULT;
	}

	if (ctrl->id == RK_V4L2_CID_VBLANKING) {
		ctrl->value = cam_mod->active_config->v_blanking_time_us;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"RK_V4L2_CID_VBLANKING %d\n",
			ctrl->value);
		return 0;
	}

	if ((cam_mod->state != TC_CAMERA_MODULE_SW_STANDBY) &&
		(cam_mod->state != TC_CAMERA_MODULE_STREAMING)) {
		pltfrm_camera_module_pr_err(&cam_mod->sd,
			"cannot get controls when camera is off\n");
		return -EFAULT;
	}

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		struct v4l2_subdev *af_ctrl;

		af_ctrl = pltfrm_camera_module_get_af_ctrl(sd);
		if (!IS_ERR_OR_NULL(af_ctrl)) {
			ret = v4l2_subdev_call(af_ctrl, core, g_ctrl, ctrl);
			return ret;
		}
	}

	if (!IS_ERR_OR_NULL(cam_mod->custom.g_ctrl)) {
		ret = cam_mod->custom.g_ctrl(cam_mod, ctrl->id);
		if (IS_ERR_VALUE(ret))
			return ret;
	}

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		ctrl->value = cam_mod->exp_config.gain;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			     "V4L2_CID_GAIN %d\n",
			     ctrl->value);
		break;
	case V4L2_CID_EXPOSURE:
		ctrl->value = cam_mod->exp_config.exp_time;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			     "V4L2_CID_EXPOSURE %d\n",
			     ctrl->value);
		break;
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		ctrl->value = cam_mod->wb_config.temperature;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"V4L2_CID_WHITE_BALANCE_TEMPERATURE %d\n",
			ctrl->value);
		break;
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		ctrl->value = cam_mod->wb_config.preset_id;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE %d\n",
			ctrl->value);
		break;
	case V4L2_CID_AUTOGAIN:
		ctrl->value = cam_mod->exp_config.auto_gain;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"V4L2_CID_AUTOGAIN %d\n",
			ctrl->value);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ctrl->value = cam_mod->exp_config.auto_exp;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"V4L2_CID_EXPOSURE_AUTO %d\n",
			ctrl->value);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ctrl->value = cam_mod->wb_config.auto_wb;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"V4L2_CID_AUTO_WHITE_BALANCE %d\n",
			ctrl->value);
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		ctrl->value = cam_mod->af_config.abs_pos;
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"V4L2_CID_FOCUS_ABSOLUTE %d\n",
			ctrl->value);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		/* TBD */
		/* fallthrough */
	default:
		pltfrm_camera_module_pr_debug(&cam_mod->sd,
			"failed, unknown ctrl %d\n", ctrl->id);
		return -EINVAL;
	}

	return 0;
}

static int flash_light_ctrl(
		struct v4l2_subdev *sd,
		struct tc_camera_module *cam_mod,
		int value)
{
	return 0;
}

/* ======================================================================== */

int tc_camera_module_s_ext_ctrls(
	struct v4l2_subdev *sd,
	struct v4l2_ext_controls *ctrls)
{
	int i;
	int ctrl_cnt = 0;
	struct tc_camera_module *cam_mod =  to_tc_camera_module(sd);
	int ret = 0;

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "\n");
	if (ctrls->count == 0)
		return -EINVAL;

	for (i = 0; i < ctrls->count; i++) {
		struct v4l2_ext_control *ctrl;
		u32 ctrl_updt = 0;

		ctrl = &ctrls->controls[i];

		switch (ctrl->id) {
		case V4L2_CID_GAIN:
			ctrl_updt = TC_CAMERA_MODULE_CTRL_UPDT_GAIN;
			cam_mod->exp_config.gain = ctrl->value;
			pltfrm_camera_module_pr_debug(
				&cam_mod->sd,
				"V4L2_CID_GAIN %d\n",
				ctrl->value);
			break;
		case RK_V4L2_CID_GAIN_PERCENT:
			ctrl_updt = TC_CAMERA_MODULE_CTRL_UPDT_GAIN;
			cam_mod->exp_config.gain_percent = ctrl->value;
			break;
		case V4L2_CID_FLASH_LED_MODE:
			ret = flash_light_ctrl(sd, cam_mod, ctrl->value);
			if (ret == 0) {
				cam_mod->exp_config.flash_mode = ctrl->value;
				pltfrm_camera_module_pr_debug(&cam_mod->sd,
					"V4L2_CID_FLASH_LED_MODE %d\n",
					ctrl->value);
			}
			break;
		case V4L2_CID_EXPOSURE:
			ctrl_updt = TC_CAMERA_MODULE_CTRL_UPDT_EXP_TIME;
			cam_mod->exp_config.exp_time = ctrl->value;
			pltfrm_camera_module_pr_debug
				(&cam_mod->sd,
				"V4L2_CID_EXPOSURE %d\n",
				ctrl->value);
			break;
		case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
			ctrl_updt = TC_CAMERA_MODULE_CTRL_UPDT_WB_TEMPERATURE;
			cam_mod->wb_config.temperature = ctrl->value;
			pltfrm_camera_module_pr_debug(
				&cam_mod->sd,
				"V4L2_CID_WHITE_BALANCE_TEMPERATURE %d\n",
				ctrl->value);
			break;
		case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
			ctrl_updt = TC_CAMERA_MODULE_CTRL_UPDT_PRESET_WB;
			cam_mod->wb_config.preset_id = ctrl->value;
			pltfrm_camera_module_pr_debug(
				&cam_mod->sd,
				"V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE %d\n",
				ctrl->value);
			break;
		case V4L2_CID_AUTOGAIN:
			ctrl_updt = TC_CAMERA_MODULE_CTRL_UPDT_AUTO_GAIN;
			cam_mod->exp_config.auto_gain = ctrl->value;
			pltfrm_camera_module_pr_debug(
				&cam_mod->sd,
				"V4L2_CID_AUTOGAIN %d\n",
				ctrl->value);
			break;
		case V4L2_CID_EXPOSURE_AUTO:
			ctrl_updt = TC_CAMERA_MODULE_CTRL_UPDT_AUTO_EXP;
			cam_mod->exp_config.auto_exp = ctrl->value;
			pltfrm_camera_module_pr_debug(
				&cam_mod->sd,
				"V4L2_CID_EXPOSURE_AUTO %d\n",
				ctrl->value);
			break;
		case V4L2_CID_AUTO_WHITE_BALANCE:
			ctrl_updt = TC_CAMERA_MODULE_CTRL_UPDT_AUTO_WB;
			cam_mod->wb_config.auto_wb = ctrl->value;
			pltfrm_camera_module_pr_debug(
				&cam_mod->sd,
				"V4L2_CID_AUTO_WHITE_BALANCE %d\n",
				ctrl->value);
			break;
		case RK_V4L2_CID_AUTO_FPS:
			cam_mod->auto_adjust_fps = ctrl->value;
			pltfrm_camera_module_pr_debug(
				&cam_mod->sd,
				"RK_V4L2_CID_AUTO_FPS %d\n",
				ctrl->value);
			break;
		case V4L2_CID_FOCUS_ABSOLUTE:
			{
				struct v4l2_subdev *af_ctrl;

				af_ctrl = pltfrm_camera_module_get_af_ctrl(sd);
				if (!IS_ERR_OR_NULL(af_ctrl)) {
					struct v4l2_control single_ctrl;

					single_ctrl.id =
						V4L2_CID_FOCUS_ABSOLUTE;
					single_ctrl.value = ctrl->value;
					ret = v4l2_subdev_call(af_ctrl,
						core, s_ctrl, &single_ctrl);
					return ret;
				}
			}
			ctrl_updt =
				TC_CAMERA_MODULE_CTRL_UPDT_FOCUS_ABSOLUTE;
			cam_mod->af_config.abs_pos = ctrl->value;
			pltfrm_camera_module_pr_debug(
				&cam_mod->sd,
				"V4L2_CID_FOCUS_ABSOLUTE %d\n",
				ctrl->value);
			break;
		case V4L2_CID_HFLIP:
			if (ctrl->value)
				cam_mod->hflip = true;
			else
				cam_mod->hflip = false;
			break;
		case V4L2_CID_VFLIP:
			if (ctrl->value)
				cam_mod->vflip = true;
			else
				cam_mod->vflip = false;
			break;
		default:
			pltfrm_camera_module_pr_warn(
				&cam_mod->sd,
				"ignoring unknown ctrl 0x%x\n",
				ctrl->id);
			break;
		}

		if (cam_mod->state != TC_CAMERA_MODULE_SW_STANDBY &&
		    cam_mod->state != TC_CAMERA_MODULE_STREAMING)
			cam_mod->ctrl_updt |= ctrl_updt;
		else if (ctrl_updt)
			ctrl_cnt++;
	}

	/* if camera module is already streaming, write through */
	if (ctrl_cnt &&
		(cam_mod->state == TC_CAMERA_MODULE_STREAMING ||
		cam_mod->state == TC_CAMERA_MODULE_SW_STANDBY)) {
		struct tc_camera_module_ext_ctrls tc_ctrls;

		tc_ctrls.ctrls =
		(struct tc_camera_module_ext_ctrl *)
		kmalloc(ctrl_cnt * sizeof(struct tc_camera_module_ext_ctrl),
			GFP_KERNEL);

		if (tc_ctrls.ctrls) {
			for (i = 0; i < ctrl_cnt; i++) {
				tc_ctrls.ctrls[i].id = ctrls->controls[i].id;
				tc_ctrls.ctrls[i].value =
					ctrls->controls[i].value;
			}

			tc_ctrls.count = ctrl_cnt;

			ret = cam_mod->custom.s_ext_ctrls(cam_mod, &tc_ctrls);

			kfree(tc_ctrls.ctrls);
		} else {
			ret = -ENOMEM;
		}
		if (IS_ERR_VALUE(ret))
			pltfrm_camera_module_pr_debug(&cam_mod->sd,
				"failed with error %d\n", ret);
	}

	return ret;
}

/* ======================================================================== */

int tc_camera_module_s_ctrl(
	struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct tc_camera_module *cam_mod =  to_tc_camera_module(sd);
	struct v4l2_ext_control ext_ctrl[1];
	struct v4l2_ext_controls ext_ctrls;

	pltfrm_camera_module_pr_debug(&cam_mod->sd,
		"0x%x 0x%x\n", ctrl->id, ctrl->value);

	ext_ctrl[0].id = ctrl->id;
	ext_ctrl[0].value = ctrl->value;

	ext_ctrls.count = 1;
	ext_ctrls.controls = ext_ctrl;

	return tc_camera_module_s_ext_ctrls(sd, &ext_ctrls);
}

/* ======================================================================== */

long tc_camera_module_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd,
	void *arg)
{
	struct tc_camera_module *cam_mod =  to_tc_camera_module(sd);
	int ret;

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "cmd: 0x%x\n", cmd);

	if (cmd == RK_VIDIOC_SENSOR_MODE_DATA) {
		struct tc_camera_module_timings tc_timings;
		struct isp_supplemental_sensor_mode_data *timings =
		(struct isp_supplemental_sensor_mode_data *)arg;

		if (cam_mod->custom.g_timings)
			ret = cam_mod->custom.g_timings(cam_mod, &tc_timings);
		else
			ret = -EPERM;

		if (IS_ERR_VALUE(ret)) {
			pltfrm_camera_module_pr_err(
				&cam_mod->sd,
				"failed with error %d\n",
				ret);
			return ret;
		}

		timings->sensor_output_width = tc_timings.sensor_output_width;
		timings->sensor_output_height = tc_timings.sensor_output_height;
		timings->crop_horizontal_start =
			tc_timings.crop_horizontal_start;
		timings->crop_vertical_start = tc_timings.crop_vertical_start;
		timings->crop_horizontal_end = tc_timings.crop_horizontal_end;
		timings->crop_vertical_end = tc_timings.crop_vertical_end;
		timings->line_length_pck = tc_timings.line_length_pck;
		timings->frame_length_lines = tc_timings.frame_length_lines;
		timings->vt_pix_clk_freq_hz = tc_timings.vt_pix_clk_freq_hz;
		timings->binning_factor_x = tc_timings.binning_factor_x;
		timings->binning_factor_y = tc_timings.binning_factor_y;
		timings->coarse_integration_time_max_margin =
			tc_timings.coarse_integration_time_max_margin;
		timings->coarse_integration_time_min =
			tc_timings.coarse_integration_time_min;
		timings->fine_integration_time_max_margin =
			tc_timings.fine_integration_time_max_margin;
		timings->fine_integration_time_min =
			tc_timings.fine_integration_time_min;

		if (cam_mod->custom.g_exposure_valid_frame)
			timings->exposure_valid_frame[0] =
				cam_mod->custom.g_exposure_valid_frame(cam_mod);
		if (cam_mod->exp_config.exp_time)
			timings->exp_time = cam_mod->exp_config.exp_time;
		else
			timings->exp_time = tc_timings.exp_time;
		if (cam_mod->exp_config.gain)
			timings->gain = cam_mod->exp_config.gain;
		else
			timings->gain = tc_timings.gain;
		return ret;
	} else if (cmd == PLTFRM_CIFCAM_G_ITF_CFG) {
		struct pltfrm_cam_itf *itf_cfg = (struct pltfrm_cam_itf *)arg;
		struct tc_camera_module_config *config;

		if (cam_mod->custom.num_configs <= 0) {
			pltfrm_camera_module_pr_err(&cam_mod->sd,
				"cam_mod->custom.num_configs is NULL, Get interface config failed!\n");
			return -EINVAL;
		}

		if (IS_ERR_OR_NULL(cam_mod->active_config))
			config = &cam_mod->custom.configs[0];
		else
			config = cam_mod->active_config;

		*itf_cfg = config->itf_cfg;

		pltfrm_camera_module_ioctl(sd, PLTFRM_CIFCAM_G_ITF_CFG, arg);
		return 0;
	} else if (cmd == PLTFRM_CIFCAM_ATTACH) {
		tc_camera_module_init(cam_mod, &cam_mod->custom);
		pltfrm_camera_module_ioctl(sd, cmd, arg);
		return tc_camera_module_attach(cam_mod);
	}

	ret = pltfrm_camera_module_ioctl(sd, cmd, arg);
	return ret;
}

/* ======================================================================== */

int tc_camera_module_get_flip_mirror(
	struct tc_camera_module *cam_mod)
{
	return pltfrm_camera_module_get_flip_mirror(&cam_mod->sd);
}

/* ======================================================================== */

int tc_camera_module_enum_frameintervals(
	struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	struct tc_camera_module *cam_mod =  to_tc_camera_module(sd);

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "%d\n", fie->index);

	if (fie->index >= cam_mod->custom.num_configs)
		return -EINVAL;
	fie->code =
		cam_mod->custom.configs[fie->index].frm_fmt.code;
	fie->width =
		cam_mod->custom.configs[fie->index].frm_fmt.width;
	fie->height =
		cam_mod->custom.configs[fie->index].frm_fmt.height;
	fie->interval.numerator = cam_mod->custom.configs[fie->index].frm_intrvl.interval.numerator;
	fie->interval.denominator = cam_mod->custom.configs[fie->index].frm_intrvl.interval.denominator;
	return 0;
}

/* ======================================================================== */
static int tc_camera_module_i2c_write(struct v4l2_subdev *sd, u16 reg,
				       u8 *values, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret, i;
	struct i2c_msg msg[1];
	unsigned char data[32];
	int retries;

	if (!client->adapter) {
		pltfrm_camera_module_pr_err(sd, "client->adapter NULL\n");
		return -ENODEV;
	}

	for (retries = 0; retries < 5; retries++) {
		msg->addr = client->addr;
		msg->flags = I2C_M_WR;
		msg->len = 2 + len;
		msg->buf = data;

		/* high byte goes out first */
		data[0] = (u8)(reg >> 8);
		data[1] = (u8)(reg & 0xff);
		for (i = 0; i < len; i++)
			data[2 + i] = values[i];

		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret == 1) {
			pltfrm_camera_module_pr_debug(sd,
				"i2c write to offset 0x%08x success\n", reg);
			return 0;
		}

		pltfrm_camera_module_pr_err(sd,
			"retrying I2C... %d\n", retries);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(20));
	}
	pltfrm_camera_module_pr_err(sd,
		"i2c write to offset 0x%08x failed with error %d\n", reg, ret);
	return ret;
}

/* ======================================================================== */
static int tc_camera_module_i2c_read(struct v4l2_subdev *sd, u16 reg,
				       u32 *value, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	unsigned char data[4] = {0, 0, 0, 0};
	struct i2c_msg msg[1];

	if (!client->adapter) {
		pltfrm_camera_module_pr_err(sd, "client->adapter NULL\n");
		return -ENODEV;
	}

	msg->addr = client->addr;
	msg->flags = I2C_M_WR;
	msg->len = 2;
	msg->buf = data;

	/* High byte goes out first */
	data[0] = (u8)(reg >> 8);
	data[1] = (u8)(reg & 0xff);

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret >= 0) {
		mdelay(3);
		msg->flags = I2C_M_RD;
		msg->len = len;
		msg->buf = data;
		i2c_transfer(client->adapter, msg, 1);
	}
	if (ret >= 0) {
		if (len == 1)
			*value = data[0];
		else if (len == 2)
			*value = data[0] + (data[1] << 8);
		else
			*value = data[0] + (data[1] << 8) +
			    (data[2] << 16) + (data[3] << 24);
		return 0;
	}

	return ret;
}

/* ===============================I2c==================================== */

int tc_camera_module_write8_reg(
	struct tc_camera_module *cam_mod,
	u16 reg,
	u8 val)
{
	return tc_camera_module_i2c_write(&cam_mod->sd, reg, &val, 1);
}

int tc_camera_module_write16_reg(
	struct tc_camera_module *cam_mod,
	u16 reg,
	u16 val)
{
	return tc_camera_module_i2c_write(&cam_mod->sd, reg, (u8 *)&val, 2);
}

int tc_camera_module_write32_reg(
	struct tc_camera_module *cam_mod,
	u16 reg,
	u32 val)
{
	return tc_camera_module_i2c_write(&cam_mod->sd, reg, (u8 *)&val, 4);
}

u8 tc_camera_module_read8_reg(
	struct tc_camera_module *cam_mod,
	u16 reg)
{
	u32 val;
	u8 ret;

	tc_camera_module_i2c_read(&cam_mod->sd, reg, &val, 1);
	ret = val & 0xff;

	return ret;
}

u16 tc_camera_module_read16_reg(
	struct tc_camera_module *cam_mod,
	u16 reg)
{
	u32 val;
	u16 ret;

	tc_camera_module_i2c_read(&cam_mod->sd, reg, &val, 2);
	ret = val & 0xffff;

	return ret;
}

u32 tc_camera_module_read32_reg(
	struct tc_camera_module *cam_mod,
	u16 reg)
{
	u32 val;

	tc_camera_module_i2c_read(&cam_mod->sd, reg, &val, 4);

	return val;
}

int tc_camera_module_read_reg_table(
	struct tc_camera_module *cam_mod,
	u16 reg,
	u32 *val)
{
	int i;

	if (cam_mod->state == TC_CAMERA_MODULE_STREAMING)
		return tc_camera_module_i2c_read(&cam_mod->sd,
			reg, val, 1);

	if (!IS_ERR_OR_NULL(cam_mod->active_config)) {
		for (
			i = cam_mod->active_config->reg_table_num_entries - 1;
			i > 0;
			i--) {
			if (cam_mod->active_config->reg_table[i].reg == reg) {
				*val = cam_mod->active_config->reg_table[i].val;
				return 0;
			}
		}
	}

	if (cam_mod->state == TC_CAMERA_MODULE_SW_STANDBY)
		return tc_camera_module_i2c_read(&cam_mod->sd,
			reg, val, 1);

	return -EFAULT;
}

int tc_camera_module_write_reglist(
	struct tc_camera_module *cam_mod,
	const struct tc_camera_module_reg reglist[],
	int len)
{
	int i = 0;

	for (i = 0; i < len; i++) {
		switch (reglist[i].flag) {
		case TC_CAMERA_MODULE_REG_TYPE_DATA:
			switch (reglist[i].len) {
			case TC_CAMERA_MOUDLE_REG_VALUE_LEN_8BIT:
				tc_camera_module_write8_reg(cam_mod, reglist[i].reg, (u8)reglist[i].val);
				break;
			case TC_CAMERA_MOUDLE_REG_VALUE_LEN_16BIT:
				tc_camera_module_write16_reg(cam_mod, reglist[i].reg, (u16)reglist[i].val);
				break;
			case TC_CAMERA_MOUDLE_REG_VALUE_LEN_32BIT:
				tc_camera_module_write32_reg(cam_mod, reglist[i].reg, (u32)reglist[i].val);
				break;
			default:
				pltfrm_camera_module_pr_err(&cam_mod->sd, "unknown value len, default 8bit\n");
				tc_camera_module_write8_reg(cam_mod, reglist[i].reg, (u8)reglist[i].val);
				break;
			}
			break;
		case PLTFRM_CAMERA_MODULE_REG_TYPE_TIMEOUT:
			mdelay(reglist[i].val);
			break;
		default:
			pltfrm_camera_module_pr_err(&cam_mod->sd, "unknown command\n");
			return -1;
		}
	}

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "write reg list len %d\n", len);
	return 0;
}

int tc_camera_module_init(struct tc_camera_module *cam_mod,
	struct tc_camera_module_custom_config *custom)
{
	int ret = 0;

	pltfrm_camera_module_pr_debug(&cam_mod->sd, "\n");

	tc_camera_module_reset(cam_mod);

	if (IS_ERR_OR_NULL(custom->s_ctrl) ||
		IS_ERR_OR_NULL(custom->g_ctrl)) {
		pltfrm_camera_module_pr_err(&cam_mod->sd,
			"mandatory callback function is missing\n");
		ret = -EINVAL;
		goto err;
	}

	ret = pltfrm_camera_module_init(&cam_mod->sd, &cam_mod->pltfm_data);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = pltfrm_camera_module_set_pin_state(&cam_mod->sd,
				PLTFRM_CAMERA_MODULE_PIN_PD,
				PLTFRM_CAMERA_MODULE_PIN_STATE_INACTIVE);
	ret = pltfrm_camera_module_set_pin_state(&cam_mod->sd,
				PLTFRM_CAMERA_MODULE_PIN_RESET,
				PLTFRM_CAMERA_MODULE_PIN_STATE_ACTIVE);
	if (IS_ERR_VALUE(ret)) {
		tc_camera_module_release(cam_mod);
		goto err;
	}

	return 0;
err:
	pltfrm_camera_module_pr_err(&cam_mod->sd,
		"failed with error %d\n", ret);
	return ret;
}

void tc_camera_module_release(struct tc_camera_module *cam_mod)
{
	pltfrm_camera_module_pr_debug(&cam_mod->sd, "\n");

	if (cam_mod->otp_work.wq) {
		flush_workqueue(cam_mod->otp_work.wq);
		destroy_workqueue(cam_mod->otp_work.wq);
		cam_mod->otp_work.wq = NULL;
	}

	cam_mod->custom.configs = NULL;

	pltfrm_camera_module_release(&cam_mod->sd);
	v4l2_device_unregister_subdev(&cam_mod->sd);
}
