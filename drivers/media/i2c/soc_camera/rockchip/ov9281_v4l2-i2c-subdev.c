// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/media/i2c/soc_camera/xgold/ov9281.c
 *
 * ov9281 sensor driver
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
 * Note:
 *    07/01/2014: new implementation using v4l2-subdev
 *                        instead of v4l2-int-device.
 */

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf-core.h>
#include <linux/slab.h>
#include "ov_camera_module.h"
#define DEBUG
#define ov9281_DRIVER_NAME "ov9281"

#define ov9281_FETCH_LSB_GAIN(VAL) ((VAL) & 0x00ff)
#define ov9281_FETCH_MSB_GAIN(VAL) (((VAL) >> 8) & 0xff)
#define ov9281_AEC_PK_LONG_GAIN_REG	 0x3509	/* Bits 0 -5 */

#define ov9281_AEC_PK_LONG_EXPO_3RD_REG 0x3500	/* Exposure Bits 16-19 */
#define ov9281_AEC_PK_LONG_EXPO_2ND_REG 0x3501	/* Exposure Bits 8-15 */
#define ov9281_AEC_PK_LONG_EXPO_1ST_REG 0x3502	/* Exposure Bits 0-7 */

#define ov9281_AEC_GROUP_UPDATE_ADDRESS 0x3208
#define ov9281_AEC_GROUP_UPDATE_START_DATA 0x00
#define ov9281_AEC_GROUP_UPDATE_END_DATA 0x10
#define ov9281_AEC_GROUP_UPDATE_END_LAUNCH 0xA0

#define ov9281_FETCH_3RD_BYTE_EXP(VAL) (((VAL) >> 16) & 0xF)	/* 4 Bits */
#define ov9281_FETCH_2ND_BYTE_EXP(VAL) (((VAL) >> 8) & 0xFF)	/* 8 Bits */
#define ov9281_FETCH_1ST_BYTE_EXP(VAL) ((VAL) & 0xFF)	/* 8 Bits */

#define ov9281_PIDH_ADDR     0x300A
#define ov9281_PIDL_ADDR     0x300B

#define ov9281_TIMING_VTS_HIGH_REG 0x380e
#define ov9281_TIMING_VTS_LOW_REG 0x380f
#define ov9281_TIMING_HTS_HIGH_REG 0x380c
#define ov9281_TIMING_HTS_LOW_REG 0x380d
#define ov9281_INTEGRATION_TIME_MARGIN 8
#define ov9281_FINE_INTG_TIME_MIN 0
#define ov9281_FINE_INTG_TIME_MAX_MARGIN 0
#define ov9281_COARSE_INTG_TIME_MIN 1
#define ov9281_COARSE_INTG_TIME_MAX_MARGIN 25
#define ov9281_TIMING_X_INC		0x3814
#define ov9281_TIMING_Y_INC		0x3815
#define ov9281_HORIZONTAL_START_HIGH_REG 0x3800
#define ov9281_HORIZONTAL_START_LOW_REG 0x3801
#define ov9281_VERTICAL_START_HIGH_REG 0x3802
#define ov9281_VERTICAL_START_LOW_REG 0x3803
#define ov9281_HORIZONTAL_END_HIGH_REG 0x3804
#define ov9281_HORIZONTAL_END_LOW_REG 0x3805
#define ov9281_VERTICAL_END_HIGH_REG 0x3806
#define ov9281_VERTICAL_END_LOW_REG 0x3807
#define ov9281_HORIZONTAL_OUTPUT_SIZE_HIGH_REG 0x3808
#define ov9281_HORIZONTAL_OUTPUT_SIZE_LOW_REG 0x3809
#define ov9281_VERTICAL_OUTPUT_SIZE_HIGH_REG 0x380a
#define ov9281_VERTICAL_OUTPUT_SIZE_LOW_REG 0x380b
#define ov9281_FLIP_REG                      0x3820
#define ov9281_MIRROR_REG                      0x3821

#define ov9281_EXT_CLK 24000000

#define ov9281_FULL_SIZE_RESOLUTION_WIDTH 1280
#define ov9281_BINING_SIZE_RESOLUTION_WIDTH 1280
#define ov9281_VIDEO_SIZE_RESOLUTION_WIDTH 1200

#define ov9281_EXP_VALID_FRAMES		4
/* High byte of product ID */
#define ov9281_PIDH_MAGIC 0x92
/* Low byte of product ID  */
#define ov9281_PIDL_MAGIC 0x81

#define ov9281_SNAPSHOT_MODE 1

static struct ov_camera_module ov9281;
static struct ov_camera_module_custom_config ov9281_custom_config;

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */

/* MCLK:26MHz  1280x800  60fps   mipi 2lane   481Mbps/lane */
static struct ov_camera_module_reg
	ov9281_init_tab_1280_800_60fps[] = {
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0103, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x0302, 0x32},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030d, 0x50},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x030e, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3001, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3004, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3005, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3006, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3011, 0x0a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3013, 0x18},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3022, 0x01},
	/* normal */
	//	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4f00, 0x01};
	#ifdef ov9281_SNAPSHOT_MODE
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3023, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302c, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302f, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3030, 0x04},
	#else
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3030, 0x10},
	#endif
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3039, 0x32},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x303a, 0x00},

	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x303f, 0x01},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3500, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3501, 0x2a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3502, 0x90},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3503, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3505, 0x8c},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3507, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3508, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3509, 0x10},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3610, 0x80},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3611, 0xa0},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3620, 0x6f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3632, 0x56},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3633, 0x78},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3662, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3666, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x366f, 0x5a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3680, 0x84},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3712, 0x80},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x372d, 0x22},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3731, 0x80},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3732, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3778, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x377d, 0x22},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3788, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3789, 0xa4},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x378a, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x378b, 0x4a},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3799, 0x20},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3800, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3801, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3802, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3803, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3804, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3805, 0x0f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3806, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3807, 0x2f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3808, 0x05},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3809, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380a, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380b, 0x20},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380c, 0x02},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0xd8},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380e, 0x03},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380f, 0x8e},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3810, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3811, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3812, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3813, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3814, 0x11},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3815, 0x11},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3820, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3821, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3881, 0x42},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x38b1, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3920, 0xff},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4003, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4008, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4009, 0x0b},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x400c, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x400d, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4010, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4043, 0x40},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4307, 0x30},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4317, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4501, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4507, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4509, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x450a, 0x08},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4601, 0x04},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x470f, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4f07, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4800, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5000, 0x9f},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5001, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5e00, 0x00},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5d00, 0x07},
	{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5d01, 0x00}
};

/* ======================================================================== */
static struct ov_camera_module_config ov9281_configs[] = {
{
		.name = "1280x800_60fps",
		.frm_fmt = {
			.width = 1280,
			.height = 800,
			.code = MEDIA_BUS_FMT_Y10_1X10
		},
		.frm_intrvl = {
			.interval = {
				.numerator = 1,
				.denominator = 120
			}
		},
		.auto_exp_enabled = false,
		.auto_gain_enabled = false,
		.auto_wb_enabled = false,
		.reg_table = (void *)ov9281_init_tab_1280_800_60fps,
		.reg_table_num_entries =
			sizeof(ov9281_init_tab_1280_800_60fps) /
			sizeof(ov9281_init_tab_1280_800_60fps[0]),
		.v_blanking_time_us = 7251,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 2, 450, 24000000)
	}
};

/*--------------------------------------------------------------------------*/
static int ov9281_g_VTS(struct ov_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ret = ov_camera_module_read_reg_table(cam_mod,
		ov9281_TIMING_VTS_HIGH_REG,
		&msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = ov_camera_module_read_reg_table(cam_mod,
		ov9281_TIMING_VTS_LOW_REG,
		&lsb);
	if (IS_ERR_VALUE(ret))
		goto err;

	*vts = (msb << 8) | lsb;
	cam_mod->vts_cur = *vts;

	return 0;
err:
	ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/
static int ov9281_auto_adjust_fps(struct ov_camera_module *cam_mod,
	u32 exp_time)
{
	int ret;
	u32 vts;

	if ((cam_mod->exp_config.exp_time + ov9281_COARSE_INTG_TIME_MAX_MARGIN)
		> cam_mod->vts_min)
		vts = cam_mod->exp_config.exp_time +
			ov9281_COARSE_INTG_TIME_MAX_MARGIN;
	else
		vts = cam_mod->vts_min;
	ret = ov_camera_module_write_reg(cam_mod,
		ov9281_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= ov_camera_module_write_reg(cam_mod,
		ov9281_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		ov_camera_module_pr_info(cam_mod,
			"updated vts = %d,vts_min=%d\n", vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}

	return ret;
}

static int ov9281_set_vts(struct ov_camera_module *cam_mod,
	u32 vts)
{
	int ret = 0;

	if (vts <= cam_mod->vts_min)
		return ret;

	ret = ov_camera_module_write_reg(cam_mod,
		ov9281_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= ov_camera_module_write_reg(cam_mod,
		ov9281_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0x0F);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		ov_camera_module_pr_info(cam_mod,
			"updated vts = 0x%x,vts_min=0x%x\n",
			vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}
	return ret;
}

static int ov9281_write_aec(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_err(cam_mod,
				  "exp_time = %d, gain = %d, flash_mode = %d\n",
				  cam_mod->exp_config.exp_time,
				  cam_mod->exp_config.gain,
				  cam_mod->exp_config.flash_mode);

	/*
	 * if the sensor is already streaming, write to shadow registers,
	 * if the sensor is in SW standby, write to active registers,
	 * if the sensor is off/registers are not writeable, do nothing
	 */
	if ((cam_mod->state == OV_CAMERA_MODULE_SW_STANDBY) ||
		(cam_mod->state == OV_CAMERA_MODULE_STREAMING)) {
		u32 a_gain = cam_mod->exp_config.gain;
		u32 exp_time;

		a_gain = a_gain > 0x7ff ? 0x7ff : a_gain;
		a_gain = a_gain * cam_mod->exp_config.gain_percent / 100;

		exp_time = cam_mod->exp_config.exp_time << 4;
		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING)
			ret = ov_camera_module_write_reg(cam_mod,
			ov9281_AEC_GROUP_UPDATE_ADDRESS,
			ov9281_AEC_GROUP_UPDATE_START_DATA);
		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret = ov9281_auto_adjust_fps(cam_mod,
				cam_mod->exp_config.exp_time);

		ret |= ov_camera_module_write_reg(cam_mod,
			ov9281_AEC_PK_LONG_GAIN_REG,
			ov9281_FETCH_LSB_GAIN(a_gain));
		ret = ov_camera_module_write_reg(cam_mod,
			ov9281_AEC_PK_LONG_EXPO_3RD_REG,
			ov9281_FETCH_3RD_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov9281_AEC_PK_LONG_EXPO_2ND_REG,
			ov9281_FETCH_2ND_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			ov9281_AEC_PK_LONG_EXPO_1ST_REG,
			ov9281_FETCH_1ST_BYTE_EXP(exp_time));
		if (!cam_mod->auto_adjust_fps)
			ret |= ov9281_set_vts(cam_mod,
					      cam_mod->exp_config.vts_value);
		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING) {
			ret = ov_camera_module_write_reg(cam_mod,
				ov9281_AEC_GROUP_UPDATE_ADDRESS,
				ov9281_AEC_GROUP_UPDATE_END_DATA);
			ret = ov_camera_module_write_reg(cam_mod,
				ov9281_AEC_GROUP_UPDATE_ADDRESS,
				ov9281_AEC_GROUP_UPDATE_END_LAUNCH);
		}
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/
static int ov9281_g_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

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
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/
static int ov9281_filltimings(struct ov_camera_module_custom_config *custom)
{
	size_t i;
	int j;
	struct ov_camera_module_config *config;
	struct ov_camera_module_timings *timings;
	struct ov_camera_module_reg *reg_table;
	int reg_table_num_entries;

	for (i = 0; i < custom->num_configs; i++) {
		config = &custom->configs[i];
		reg_table = config->reg_table;
		reg_table_num_entries = config->reg_table_num_entries;
		timings = &config->timings;

		memset(timings, 0x00, sizeof(*timings));
		for (j = 0; j < reg_table_num_entries; j++) {
			switch (reg_table[j].reg) {
			case ov9281_TIMING_VTS_HIGH_REG:
				timings->frame_length_lines =
					((reg_table[j].val << 8) |
					(timings->frame_length_lines & 0xff));
				break;
			case ov9281_TIMING_VTS_LOW_REG:
				timings->frame_length_lines =
					(reg_table[j].val |
					(timings->frame_length_lines & 0xff00));
				break;
			case ov9281_TIMING_HTS_HIGH_REG:
				timings->line_length_pck =
					((reg_table[j].val << 8) |
					timings->line_length_pck);
				break;
			case ov9281_TIMING_HTS_LOW_REG:
				timings->line_length_pck =
					(reg_table[j].val |
					(timings->line_length_pck & 0xff00));
				break;
			case ov9281_TIMING_X_INC:
				timings->binning_factor_x =
				((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_x == 0)
					timings->binning_factor_x = 1;
				break;
			case ov9281_TIMING_Y_INC:
				timings->binning_factor_y =
				((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_y == 0)
					timings->binning_factor_y = 1;
				break;
			case ov9281_HORIZONTAL_START_HIGH_REG:
				timings->crop_horizontal_start =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_start &
					0xff));
				break;
			case ov9281_HORIZONTAL_START_LOW_REG:
				timings->crop_horizontal_start =
					(reg_table[j].val |
					(timings->crop_horizontal_start &
					0xff00));
				break;
			case ov9281_VERTICAL_START_HIGH_REG:
				timings->crop_vertical_start =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_start & 0xff));
				break;
			case ov9281_VERTICAL_START_LOW_REG:
				timings->crop_vertical_start =
					((reg_table[j].val) |
					(timings->crop_vertical_start &
					0xff00));
				break;
			case ov9281_HORIZONTAL_END_HIGH_REG:
				timings->crop_horizontal_end =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_end & 0xff));
				break;
			case ov9281_HORIZONTAL_END_LOW_REG:
				timings->crop_horizontal_end =
					(reg_table[j].val |
					(timings->crop_horizontal_end &
					0xff00));
				break;
			case ov9281_VERTICAL_END_HIGH_REG:
				timings->crop_vertical_end =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_end & 0xff));
				break;
			case ov9281_VERTICAL_END_LOW_REG:
				timings->crop_vertical_end =
					(reg_table[j].val |
					(timings->crop_vertical_end & 0xff00));
				break;
			case ov9281_HORIZONTAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_width =
					((reg_table[j].val << 8) |
					(timings->sensor_output_width & 0xff));
				break;
			case ov9281_HORIZONTAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_width =
					(reg_table[j].val |
					(timings->sensor_output_width &
					0xff00));
				break;
			case ov9281_VERTICAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_height =
					((reg_table[j].val << 8) |
					(timings->sensor_output_height & 0xff));
				break;
			case ov9281_VERTICAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_height =
					(reg_table[j].val |
					(timings->sensor_output_height &
					0xff00));
				break;
			case ov9281_AEC_PK_LONG_EXPO_1ST_REG:
				timings->exp_time =
					((reg_table[j].val) |
					(timings->exp_time & 0xffff00));
				break;
			case ov9281_AEC_PK_LONG_EXPO_2ND_REG:
				timings->exp_time =
					((reg_table[j].val << 8) |
					(timings->exp_time & 0x00ff00));
				break;
			case ov9281_AEC_PK_LONG_EXPO_3RD_REG:
				timings->exp_time =
					(((reg_table[j].val & 0x0f) << 16) |
					(timings->exp_time & 0xff0000));
				break;
			case ov9281_AEC_PK_LONG_GAIN_REG:
				timings->gain =
					((reg_table[j].val) |
					(timings->gain & 0x00ff));
				break;
			}
		}

		timings->exp_time >>= 4;
		timings->vt_pix_clk_freq_hz =
			config->frm_intrvl.interval.denominator
			* timings->frame_length_lines
			* timings->line_length_pck;

		timings->coarse_integration_time_min =
			ov9281_COARSE_INTG_TIME_MIN;
		timings->coarse_integration_time_max_margin =
			ov9281_COARSE_INTG_TIME_MAX_MARGIN;

		/* OV Sensor do not use fine integration time. */
		timings->fine_integration_time_min =
			ov9281_FINE_INTG_TIME_MIN;
		timings->fine_integration_time_max_margin =
			ov9281_FINE_INTG_TIME_MAX_MARGIN;
	}

	return 0;
}

static int ov9281_g_timings(struct ov_camera_module *cam_mod,
	struct ov_camera_module_timings *timings)
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
			cam_mod->frm_intrvl.interval.denominator *
			vts * timings->line_length_pck;
	else
		timings->vt_pix_clk_freq_hz =
		cam_mod->active_config->frm_intrvl.interval.denominator *
		vts * timings->line_length_pck;

	return ret;
err:
	ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov9281_s_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = ov9281_write_aec(cam_mod);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d) 0x%x\n", ret, ctrl_id);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov9281_s_ext_ctrls(struct ov_camera_module *cam_mod,
				 struct ov_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	/* Handles only exposure and gain together special case. */
	if ((ctrls->ctrls[0].id == V4L2_CID_GAIN ||
		ctrls->ctrls[0].id == V4L2_CID_EXPOSURE))
		ret = ov9281_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/
static int ov9281_start_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod,
		"active config=%s\n", cam_mod->active_config->name);

	ret = ov9281_g_VTS(cam_mod, &cam_mod->vts_min);
	if (IS_ERR_VALUE(ret))
		goto err;

#ifdef ov9281_SNAPSHOT_MODE
	if (IS_ERR_VALUE(ov_camera_module_write_reg(cam_mod, 0x0100, 0)))
		goto err;
#else
	if (IS_ERR_VALUE(ov_camera_module_write_reg(cam_mod, 0x0100, 1)))
		goto err;
#endif

	msleep(25);

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n",
		ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov9281_stop_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret = ov_camera_module_write_reg(cam_mod, 0x0100, 0);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/
static int ov9281_check_camera_id(struct ov_camera_module *cam_mod)
{
	u32 pidh, pidl = 0;
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret |= ov_camera_module_read_reg(cam_mod, 1, ov9281_PIDH_ADDR, &pidh);
	ret |= ov_camera_module_read_reg(cam_mod, 1, ov9281_PIDL_ADDR, &pidl);
	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if ((pidh == ov9281_PIDH_MAGIC) && (pidl == ov9281_PIDL_MAGIC)) {
		ov_camera_module_pr_debug(cam_mod,
			"successfully detected camera ID 0x%02x%02x\n",
			pidh, pidl);
	} else {
		ov_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			ov9281_PIDH_MAGIC, ov9281_PIDL_MAGIC, pidh, pidl);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */

static struct v4l2_subdev_core_ops ov9281_camera_module_core_ops = {
	.g_ctrl = ov_camera_module_g_ctrl,
	.s_ctrl = ov_camera_module_s_ctrl,
	.s_ext_ctrls = ov_camera_module_s_ext_ctrls,
	.s_power = ov_camera_module_s_power,
	.ioctl = ov_camera_module_ioctl
};

static struct v4l2_subdev_video_ops ov9281_camera_module_video_ops = {
	.s_frame_interval = ov_camera_module_s_frame_interval,
	.s_stream = ov_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops ov9281_camera_module_pad_ops = {
	.enum_frame_interval = ov_camera_module_enum_frameintervals,
	.get_fmt = ov_camera_module_g_fmt,
	.set_fmt = ov_camera_module_s_fmt,
};

static struct v4l2_subdev_ops ov9281_camera_module_ops = {
	.core = &ov9281_camera_module_core_ops,
	.video = &ov9281_camera_module_video_ops,
	.pad = &ov9281_camera_module_pad_ops
};

static struct ov_camera_module_custom_config ov9281_custom_config = {
	.start_streaming = ov9281_start_streaming,
	.stop_streaming = ov9281_stop_streaming,
	.s_ctrl = ov9281_s_ctrl,
	.s_vts = ov9281_auto_adjust_fps,
	.s_ext_ctrls = ov9281_s_ext_ctrls,
	.g_ctrl = ov9281_g_ctrl,
	.g_timings = ov9281_g_timings,
	.check_camera_id = ov9281_check_camera_id,
	.configs = ov9281_configs,
	.num_configs = ARRAY_SIZE(ov9281_configs),
	.power_up_delays_ms = {5, 20, 0}
};

static int ov9281_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "tlc cam probing...\n");
	dev_dbg(&client->dev, "tlc cam debug...\n");
	ov9281_filltimings(&ov9281_custom_config);
	client->flags |= I2C_CLIENT_SCCB;
	v4l2_i2c_subdev_init(&ov9281.sd, client, &ov9281_camera_module_ops);
	ov9281.custom = ov9281_custom_config;

	dev_info(&client->dev, "tlc cam probing successful\n");

	return 0;
}

static int ov9281_remove(struct i2c_client *client)
{
	struct ov_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	ov_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id ov9281_id[] = {
	{ ov9281_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id ov9281_of_match[] = {
	{.compatible = "omnivision,ov9281-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov9281_id);

static struct i2c_driver ov9281_i2c_driver = {
	.driver = {
		.name = ov9281_DRIVER_NAME,
		.of_match_table = ov9281_of_match
	},
	.probe = ov9281_probe,
	.remove = ov9281_remove,
	.id_table = ov9281_id,
};

module_i2c_driver(ov9281_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ov9281");
MODULE_AUTHOR("George");
MODULE_LICENSE("GPL");
