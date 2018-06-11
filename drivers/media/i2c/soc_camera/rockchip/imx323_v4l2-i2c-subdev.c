/*
 * IMX323 sensor driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
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

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf-core.h>
#include <linux/slab.h>
#include "imx_camera_module.h"

#define IMX323_DRIVER_NAME "imx323"

#define IMX323_AEC_PK_GAIN_REG 0x301e

#define IMX323_AEC_PK_EXPO_HIGH_REG 0x0202
#define IMX323_AEC_PK_EXPO_LOW_REG 0x0203

#define IMX323_FETCH_HIGH_BYTE_EXP(VAL) ((VAL >> 8) & 0xFF)
#define IMX323_FETCH_LOW_BYTE_EXP(VAL) (VAL & 0xFF)

#define IMX323_PID_ADDR 0x0112
#define IMX323_PID_MAGIC 0xa

#define IMX323_TIMING_VTS_HIGH_REG 0x0340
#define IMX323_TIMING_VTS_LOW_REG 0x0341
#define IMX323_TIMING_HTS_HIGH_REG 0x0342
#define IMX323_TIMING_HTS_LOW_REG 0x0343

#define IMX323_INTEGRATION_TIME_MARGIN 8
#define IMX323_FINE_INTG_TIME_MIN 0
#define IMX323_FINE_INTG_TIME_MAX_MARGIN 0
#define IMX323_COARSE_INTG_TIME_MIN 16
#define IMX323_COARSE_INTG_TIME_MAX_MARGIN 4

#define IMX323_ORIENTATION_REG 0x0101
#define IMX323_ORIENTATION_H 0x1
#define IMX323_ORIENTATION_V 0x2

#define IMX323_EXT_CLK 37125000

static struct imx_camera_module imx323;
static struct imx_camera_module_custom_config imx323_custom_config;

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */

/* MCLK:37.125MHz  1920x1080  30fps   DVP_12_DATA   PCLK:74.25MHz */
static struct imx_camera_module_reg imx323_init_tab_1920_1080_30fps[] = {
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x0100, 0x00},
	{IMX_CAMERA_MODULE_REG_TYPE_TIMEOUT, 0x0000, 0x01},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x0009, 0xf0},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x0112, 0x0c},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x0113, 0x0c},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x0340, 0x04},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x0341, 0x65},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x0342, 0x04},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x0343, 0x4c},
	{IMX_CAMERA_MODULE_REG_TYPE_TIMEOUT, 0x0000, 0x01},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3000, 0x31},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3002, 0x0f},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3011, 0x00},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3012, 0x82},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3013, 0x40},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3016, 0x3c},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x301a, 0xc9},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x301c, 0x50},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x301f, 0x73},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3021, 0x00},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3022, 0x40},
	{IMX_CAMERA_MODULE_REG_TYPE_TIMEOUT, 0x0000, 0x01},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3027, 0x20},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x302c, 0x00},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x303f, 0x0a},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x304f, 0x47},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3054, 0x11},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x307a, 0x00},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x307b, 0x00},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3098, 0x26},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3099, 0x02},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x309a, 0x26},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x309b, 0x02},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x30ce, 0x16},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x30cf, 0x82},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x30d0, 0x00},
	{IMX_CAMERA_MODULE_REG_TYPE_DATA, 0x3117, 0x0d}
};

/* ======================================================================== */

static struct imx_camera_module_config imx323_configs[] = {
	{
		.name = "1920x1080_30fps",
		.frm_fmt = {
			.width = 2200,
			.height = 1125,
			.code = MEDIA_BUS_FMT_SGBRG12_1X12
		},
		.frm_intrvl = {
			.interval = {
				.numerator = 1,
				.denominator = 30
			}
		},
		.auto_exp_enabled = false,
		.auto_gain_enabled = false,
		.auto_wb_enabled = false,
		.reg_table = (void *)imx323_init_tab_1920_1080_30fps,
		.reg_table_num_entries =
			sizeof(imx323_init_tab_1920_1080_30fps) /
			sizeof(imx323_init_tab_1920_1080_30fps[0]),
		.v_blanking_time_us = 5000,
		.max_exp_gain_h = 16,
		.max_exp_gain_l = 0,
		PLTFRM_CAM_ITF_DVP_CFG(
			PLTFRM_CAM_ITF_BT601_12,
			PLTFRM_CAM_SIGNAL_HIGH_LEVEL,
			PLTFRM_CAM_SIGNAL_HIGH_LEVEL,
			PLTFRM_CAM_SDR_NEG_EDG,
			IMX323_EXT_CLK)
	}
};

/*--------------------------------------------------------------------------*/

static int imx323_g_VTS(struct imx_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ret = imx_camera_module_read_reg_table(
		cam_mod,
		IMX323_TIMING_VTS_HIGH_REG,
		&msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = imx_camera_module_read_reg_table(
		cam_mod,
		IMX323_TIMING_VTS_LOW_REG,
		&lsb);
	if (IS_ERR_VALUE(ret))
		goto err;

	*vts = (msb << 8) | lsb;

	return 0;
err:
	imx_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int imx323_auto_adjust_fps(struct imx_camera_module *cam_mod,
	u32 exp_time)
{
	int ret;
	u32 vts;

	if ((exp_time + IMX323_COARSE_INTG_TIME_MAX_MARGIN)
		> cam_mod->vts_min)
		vts = exp_time + IMX323_COARSE_INTG_TIME_MAX_MARGIN;
	else
		vts = cam_mod->vts_min;
	ret = imx_camera_module_write_reg(cam_mod,
				IMX323_TIMING_VTS_LOW_REG, vts & 0xFF);
	ret |= imx_camera_module_write_reg(cam_mod,
				IMX323_TIMING_VTS_HIGH_REG, (vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		imx_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		imx_camera_module_pr_info(cam_mod,
			"updated vts = %d,vts_min=%d\n", vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}

	return ret;
}

static int imx323_set_vts(struct imx_camera_module *cam_mod,
	u32 vts)
{
	int ret = 0;

	if (vts < cam_mod->vts_min)
		return ret;

	if (vts > 0xfff)
		vts = 0xfff;

	ret = imx_camera_module_write_reg(cam_mod,
				IMX323_TIMING_VTS_LOW_REG, vts & 0xFF);
	ret |= imx_camera_module_write_reg(cam_mod,
				IMX323_TIMING_VTS_HIGH_REG, (vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		imx_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	} else {
		imx_camera_module_pr_info(cam_mod, "updated vts=%d,vts_min=%d\n", vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}
	return ret;
}

/*--------------------------------------------------------------------------*/

static int imx323_write_aec(struct imx_camera_module *cam_mod)
{
	int ret = 0;

	imx_camera_module_pr_debug(cam_mod,
				  "exp_time = %d, gain = %d, flash_mode = %d\n",
				  cam_mod->exp_config.exp_time,
				  cam_mod->exp_config.gain,
				  cam_mod->exp_config.flash_mode);

	/*
	 * if the sensor is already streaming, write to shadow registers,
	 * if the sensor is in SW standby, write to active registers,
	 * if the sensor is off/registers are not writeable, do nothing
	 */
	if ((cam_mod->state == IMX_CAMERA_MODULE_SW_STANDBY) ||
	(cam_mod->state == IMX_CAMERA_MODULE_STREAMING)) {
		u32 a_gain = cam_mod->exp_config.gain;
		u32 exp_time = cam_mod->exp_config.exp_time;

		a_gain = a_gain * cam_mod->exp_config.gain_percent / 100;

		mutex_lock(&cam_mod->lock);
		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret = imx323_auto_adjust_fps(cam_mod,
					cam_mod->exp_config.exp_time);

		/* Gain */
		ret = imx_camera_module_write_reg(cam_mod,
				IMX323_AEC_PK_GAIN_REG, a_gain);

		/* Integration Time */
		ret = imx_camera_module_write_reg(cam_mod,
				IMX323_AEC_PK_EXPO_HIGH_REG,
				IMX323_FETCH_HIGH_BYTE_EXP(exp_time));
		ret |= imx_camera_module_write_reg(cam_mod,
				IMX323_AEC_PK_EXPO_LOW_REG,
				IMX323_FETCH_LOW_BYTE_EXP(exp_time));

		if (!cam_mod->auto_adjust_fps)
			ret |= imx323_set_vts(cam_mod, cam_mod->exp_config.vts_value);
		mutex_unlock(&cam_mod->lock);
	}

	if (IS_ERR_VALUE(ret))
		imx_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int imx323_g_ctrl(struct imx_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	imx_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		imx_camera_module_pr_debug(cam_mod,
				"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int imx323_filltimings(struct imx_camera_module_custom_config *custom)
{
	int i, j;
	struct imx_camera_module_config *config;
	struct imx_camera_module_timings *timings;
	struct imx_camera_module_reg *reg_table;
	int reg_table_num_entries;

	for (i = 0; i < custom->num_configs; i++) {
		config = &custom->configs[i];
		reg_table = config->reg_table;
		reg_table_num_entries = config->reg_table_num_entries;
		timings = &config->timings;

		for (j = 0; j < reg_table_num_entries; j++) {
			switch (reg_table[j].reg) {
			case IMX323_TIMING_VTS_HIGH_REG:
				timings->frame_length_lines =
					reg_table[j].val << 8;
				break;
			case IMX323_TIMING_VTS_LOW_REG:
				timings->frame_length_lines |= reg_table[j].val;
				break;
			case IMX323_TIMING_HTS_HIGH_REG:
				timings->line_length_pck =
					(reg_table[j].val << 8);
				break;
			case IMX323_TIMING_HTS_LOW_REG:
				timings->line_length_pck |= reg_table[j].val;
				break;
			}
		}

		timings->exp_time >>= 4;
		timings->line_length_pck = timings->line_length_pck * 2;
		timings->vt_pix_clk_freq_hz =
					config->frm_intrvl.interval.denominator
					* timings->frame_length_lines
					* timings->line_length_pck;

		timings->coarse_integration_time_min =
			IMX323_COARSE_INTG_TIME_MIN;
		timings->coarse_integration_time_max_margin =
			IMX323_COARSE_INTG_TIME_MAX_MARGIN;

		/* IMX Sensor do not use fine integration time. */
		timings->fine_integration_time_min = IMX323_FINE_INTG_TIME_MIN;
		timings->fine_integration_time_max_margin =
			IMX323_FINE_INTG_TIME_MAX_MARGIN;
	}

	return 0;
}

static int imx323_g_timings(struct imx_camera_module *cam_mod,
	struct imx_camera_module_timings *timings)
{
	int ret = 0;
	unsigned int vts;

	if (IS_ERR_OR_NULL(cam_mod->active_config))
		goto err;

	*timings = cam_mod->active_config->timings;

	vts = (!cam_mod->vts_cur) ?
		timings->frame_length_lines :
		cam_mod->vts_cur;

	if (cam_mod->frm_intrvl_valid)
		timings->vt_pix_clk_freq_hz =
			cam_mod->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;
	else
		timings->vt_pix_clk_freq_hz =
			cam_mod->active_config->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;

	timings->frame_length_lines = vts;

	return ret;
err:
	imx_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int imx323_set_flip(
	struct imx_camera_module *cam_mod,
	struct pltfrm_camera_module_reg reglist[],
	int len)
{
	int i, mode = 0;
	u16 orientation = 0;

	mode = imx_camera_module_get_flip_mirror(cam_mod);
	if (mode == -1) {
		imx_camera_module_pr_info(cam_mod,
			"dts don't set flip, return!\n");
		return 0;
	}

	if (!IS_ERR_OR_NULL(cam_mod->active_config)) {
		if (PLTFRM_CAMERA_MODULE_IS_MIRROR(mode))
			orientation |= 0x01;
		if (PLTFRM_CAMERA_MODULE_IS_FLIP(mode))
			orientation |= 0x02;
		for (i = 0; i < len; i++) {
			if (reglist[i].reg == IMX323_ORIENTATION_REG)
				reglist[i].val = orientation;
		}
	}

	return 0;
}

/*--------------------------------------------------------------------------*/

static int imx323_s_ctrl(struct imx_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	imx_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = imx323_write_aec(cam_mod);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		imx_camera_module_pr_debug(cam_mod,
				"failed with error (%d) 0x%x\n",
				ret, ctrl_id);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int imx323_s_ext_ctrls(struct imx_camera_module *cam_mod,
	struct imx_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	if ((ctrls->ctrls[0].id == V4L2_CID_GAIN ||
		ctrls->ctrls[0].id == V4L2_CID_EXPOSURE))
		ret = imx323_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		imx_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int imx323_start_streaming(struct imx_camera_module *cam_mod)
{
	int ret = 0;

	imx_camera_module_pr_debug(cam_mod,
		"active config=%s\n", cam_mod->active_config->name);

	ret = imx323_g_VTS(cam_mod, &cam_mod->vts_min);
	if (IS_ERR_VALUE(ret))
		goto err;

	mutex_lock(&cam_mod->lock);
	ret = imx_camera_module_write_reg(cam_mod, 0x0100, 1);
	mutex_unlock(&cam_mod->lock);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);

	return 0;
err:
	imx_camera_module_pr_err(cam_mod, "failed with error (%d)\n",
		ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int imx323_stop_streaming(struct imx_camera_module *cam_mod)
{
	int ret = 0;

	imx_camera_module_pr_debug(cam_mod, "\n");

	mutex_lock(&cam_mod->lock);
	ret = imx_camera_module_write_reg(cam_mod, 0x0100, 0);
	mutex_unlock(&cam_mod->lock);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);

	return 0;
err:
	imx_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/
static int imx323_check_camera_id(struct imx_camera_module *cam_mod)
{
	u32 pid;
	int ret = 0;

	imx_camera_module_pr_debug(cam_mod, "\n");

	ret |= imx_camera_module_read_reg(cam_mod, 1, IMX323_PID_ADDR, &pid);
	if (IS_ERR_VALUE(ret)) {
		imx_camera_module_pr_err(cam_mod,
		"register read failed, camera module powered off?\n");
		goto err;
	}

	if (pid == IMX323_PID_MAGIC) {
		imx_camera_module_pr_debug(cam_mod,
		"successfully detected camera ID 0x%02x\n",
		pid);
	} else {
		imx_camera_module_pr_err(cam_mod,
		"wrong camera ID, expected 0x%02x, detected 0x%02x\n",
		IMX323_PID_MAGIC, pid);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	imx_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */

static struct v4l2_subdev_core_ops imx323_camera_module_core_ops = {
	.g_ctrl = imx_camera_module_g_ctrl,
	.s_ctrl = imx_camera_module_s_ctrl,
	.s_ext_ctrls = imx_camera_module_s_ext_ctrls,
	.s_power = imx_camera_module_s_power,
	.ioctl = imx_camera_module_ioctl
};

static struct v4l2_subdev_video_ops imx323_camera_module_video_ops = {
	.s_frame_interval = imx_camera_module_s_frame_interval,
	.g_frame_interval = imx_camera_module_g_frame_interval,
	.s_stream = imx_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops imx323_camera_module_pad_ops = {
	.enum_frame_interval = imx_camera_module_enum_frameintervals,
	.get_fmt = imx_camera_module_g_fmt,
	.set_fmt = imx_camera_module_s_fmt,
};

static struct v4l2_subdev_ops imx323_camera_module_ops = {
	.core = &imx323_camera_module_core_ops,
	.video = &imx323_camera_module_video_ops,
	.pad = &imx323_camera_module_pad_ops
};

static struct imx_camera_module_custom_config imx323_custom_config = {
	.start_streaming = imx323_start_streaming,
	.stop_streaming = imx323_stop_streaming,
	.s_ctrl = imx323_s_ctrl,
	.s_ext_ctrls = imx323_s_ext_ctrls,
	.g_ctrl = imx323_g_ctrl,
	.g_timings = imx323_g_timings,
	.check_camera_id = imx323_check_camera_id,
	.set_flip = imx323_set_flip,
	.s_vts = imx323_auto_adjust_fps,
	.configs = imx323_configs,
	.num_configs =  ARRAY_SIZE(imx323_configs),
	.power_up_delays_ms = {5, 20, 0},
	/*
	*0: Exposure time valid fileds;
	*1: Exposure gain valid fileds;
	*(2 fileds == 1 frames)
	*/
	.exposure_valid_frame = {4, 4}
};

static int imx323_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");

	imx323_filltimings(&imx323_custom_config);
	v4l2_i2c_subdev_init(&imx323.sd, client, &imx323_camera_module_ops);
	imx323.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx323.custom = imx323_custom_config;

	mutex_init(&imx323.lock);
	dev_info(&client->dev, "probing successful\n");
	return 0;
}

static int imx323_remove(struct i2c_client *client)
{
	struct imx_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	mutex_destroy(&cam_mod->lock);
	imx_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id imx323_id[] = {
	{ IMX323_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id imx323_of_match[] = {
	{.compatible = "sony,imx323-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, imx323_id);

static struct i2c_driver imx323_i2c_driver = {
	.driver = {
		.name = IMX323_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = imx323_of_match
	},
	.probe = imx323_probe,
	.remove = imx323_remove,
	.id_table = imx323_id,
};

module_i2c_driver(imx323_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for IMX323");
MODULE_AUTHOR("George");
MODULE_LICENSE("GPL");
