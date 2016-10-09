/*
 * drivers/media/i2c/soc_camera/xgold/ov2710.c
 *
 * ov2710 sensor driver
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
 *
 * v0.1.0:
 *	1. Initialize version;
 *	2. Stream on sensor in configuration,
 *     and stream off sensor after 1frame;
 *	3. Stream delay time is define in power_up_delays_ms[2];
 */

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf-core.h>
#include <linux/slab.h>
#include <media/v4l2-controls_rockchip.h>
#include "ov_camera_module.h"

#define OV2710_DRIVER_NAME "ov2710"

#define OV2710_FETCH_LSB_GAIN(VAL) (VAL & 0x00FF)       /* gain[7:0] */
#define OV2710_FETCH_MSB_GAIN(VAL) ((VAL >> 8) & 0x1)	/* gain[10:8] */
#define OV2710_AEC_PK_LONG_GAIN_HIGH_REG 0x350a	/* Bit 8 */
#define OV2710_AEC_PK_LONG_GAIN_LOW_REG	 0x350b	/* Bits 0 -7 */

#define OV2710_AEC_PK_LONG_EXPO_3RD_REG 0x3500	/* Exposure Bits 16-19 */
#define OV2710_AEC_PK_LONG_EXPO_2ND_REG 0x3501	/* Exposure Bits 8-15 */
#define OV2710_AEC_PK_LONG_EXPO_1ST_REG 0x3502	/* Exposure Bits 0-7 */

#define OV2710_AEC_GROUP_UPDATE_ADDRESS 0x3212
#define OV2710_AEC_GROUP_UPDATE_START_DATA 0x00
#define OV2710_AEC_GROUP_UPDATE_END_DATA 0x10
#define OV2710_AEC_GROUP_UPDATE_END_LAUNCH 0xA0

#define OV2710_FETCH_3RD_BYTE_EXP(VAL) (((VAL) >> 12) & 0xF)	/* 4 Bits */
#define OV2710_FETCH_2ND_BYTE_EXP(VAL) (((VAL) >> 4) & 0xFF)	/* 8 Bits */
#define OV2710_FETCH_1ST_BYTE_EXP(VAL) (((VAL) & 0x0F) << 4)	/* 4 Bits */

#define OV2710_PIDH_ADDR     0x300A
#define OV2710_PIDL_ADDR     0x300B

/* High byte of product ID */
#define OV2710_PIDH_MAGIC 0x27
/* Low byte of product ID  */
#define OV2710_PIDL_MAGIC 0x10

#define OV2710_EXT_CLK 24000000
#define OV2710_PLL_PREDIV0_REG 0x3088
#define OV2710_PLL_PREDIV_REG  0x3080
#define OV2710_PLL_MUL_HIGH_REG 0x3081
#define OV2710_PLL_MUL_LOW_REG 0x3082
#define OV2710_PLL_SPDIV_REG 0x3086
#define OV2710_PLL_DIVSYS_REG 0x3084
#define OV2710_TIMING_VTS_HIGH_REG 0x380e
#define OV2710_TIMING_VTS_LOW_REG 0x380f
#define OV2710_TIMING_HTS_HIGH_REG 0x380c
#define OV2710_TIMING_HTS_LOW_REG 0x380d
#define OV2710_FINE_INTG_TIME_MIN 0
#define OV2710_FINE_INTG_TIME_MAX_MARGIN 0
#define OV2710_COARSE_INTG_TIME_MIN 1
#define OV2710_COARSE_INTG_TIME_MAX_MARGIN 4
#define OV2710_TIMING_X_INC		0x3814
#define OV2710_TIMING_Y_INC		0x3815
#define OV2710_HORIZONTAL_START_HIGH_REG 0x3800
#define OV2710_HORIZONTAL_START_LOW_REG 0x3801
#define OV2710_VERTICAL_START_HIGH_REG 0x3802
#define OV2710_VERTICAL_START_LOW_REG 0x3803
#define OV2710_HORIZONTAL_END_HIGH_REG 0x3804
#define OV2710_HORIZONTAL_END_LOW_REG 0x3805
#define OV2710_VERTICAL_END_HIGH_REG 0x3806
#define OV2710_VERTICAL_END_LOW_REG 0x3807
#define OV2710_HORIZONTAL_OUTPUT_SIZE_HIGH_REG 0x3808
#define OV2710_HORIZONTAL_OUTPUT_SIZE_LOW_REG 0x3809
#define OV2710_VERTICAL_OUTPUT_SIZE_HIGH_REG 0x380a
#define OV2710_VERTICAL_OUTPUT_SIZE_LOW_REG 0x380b
#define OV2710_H_WIN_OFF_HIGH_REG 0x3810
#define OV2710_H_WIN_OFF_LOW_REG 0x3811
#define OV2710_V_WIN_OFF_HIGH_REG 0x3812
#define OV2710_V_WIN_OFF_LOW_REG 0x3813

#define OV2710_ANA_ARRAR01 0x3621
#define OV2710_TIMING_CONCTROL_VH 0x3803
#define OV2710_TIMING_CONCTROL18 0x3818

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */
/* MCLK:24MHz  1920x1080  30fps   mipi 1lane   800Mbps/lane */
static struct ov_camera_module_reg ov2710_init_tab_1920_1080_30fps[] = {
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3008, 0x82},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3008, 0x42},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4201, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4202, 0x0f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3103, 0x93},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3017, 0x7f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3018, 0xfc},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3706, 0x61},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3712, 0x0c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3630, 0x6d},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3800, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3801, 0xb4},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3802, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3803, 0x0a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3818, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3804, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3805, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3806, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3807, 0x38},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3808, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3809, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380a, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380b, 0x38},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3810, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3811, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3812, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3813, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3621, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3604, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3603, 0xa7},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3631, 0x26},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3600, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3620, 0x37},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3623, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3702, 0x9e},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3703, 0x5c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3704, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370d, 0x0f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3713, 0x9f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3714, 0x4c},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3710, 0x9e},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3801, 0xc4},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3605, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3606, 0x3f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302d, 0x90},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370b, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3716, 0x31},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3707, 0x52},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0x74},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5181, 0x20},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x518f, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4301, 0xff},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4303, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a00, 0x78},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300f, 0x88},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3011, 0x28},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a1a, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a18, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a19, 0x7a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a13, 0x54},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x382e, 0x0f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x381a, 0x1a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x401d, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5688, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5684, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5685, 0xa0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5686, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5687, 0x43},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3011, 0x0a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300f, 0x8a},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3017, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3018, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300e, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4801, 0x0f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300f, 0xc3},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a0f, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a10, 0x38},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a1b, 0x48},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a1e, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a11, 0x90},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3a1f, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380c, 0x09},/* HTS H */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380d, 0x74},/* HTS L */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380e, 0x04},/* VTS H */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x380f, 0x50},/* VTS L */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3500, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3501, 0x28},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3502, 0x90},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3503, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350a, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350b, 0x1f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5000, 0x5f},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x5001, 0x4e},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3406, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3400, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3401, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3402, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3403, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3404, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3405, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4800, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4201, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4202, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3008, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_TIMEOUT, 0x0000, 40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3008, 0x42},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4201, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x4202, 0x0f}
};

/* ======================================================================== */
static struct ov_camera_module_config ov2710_configs[] = {
	{
		.name = "1920x1080_30fps",
		.frm_fmt = {
			.width = 1920,
			.height = 1080,
			.code = MEDIA_BUS_FMT_SBGGR10_1X10
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
		.reg_table = (void *)ov2710_init_tab_1920_1080_30fps,
		.reg_table_num_entries =
			sizeof(ov2710_init_tab_1920_1080_30fps) /
			sizeof(ov2710_init_tab_1920_1080_30fps[0]),
		.v_blanking_time_us = 3078,
		.ignore_measurement_check = 1,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 1, 800, 24000000)
	}
};

/*--------------------------------------------------------------------------*/
static int ov2710_set_flip(
	struct ov_camera_module *cam_mod,
	struct pltfrm_camera_module_reg reglist[],
	int len)
{
	int i, mode = 0;
	u16 match_reg[3];

	mode = ov_camera_module_get_flip_mirror(cam_mod);

	if (mode == -1) {
		ov_camera_module_pr_debug(
			cam_mod,
			"dts don't set flip, return!\n");
		return 0;
	}

	if (mode == OV_FLIP_BIT_MASK) {
		match_reg[0] = 0x04;
		match_reg[1] = 0x09;
		match_reg[2] = 0xa0;
	} else if (mode == OV_MIRROR_BIT_MASK) {
		match_reg[0] = 0x14;
		match_reg[1] = 0x0a;
		match_reg[2] = 0xc0;
	} else if (mode == (OV_MIRROR_BIT_MASK |
		OV_FLIP_BIT_MASK)) {
		match_reg[0] = 0x14;
		match_reg[1] = 0x09;
		match_reg[2] = 0xe0;
	} else {
		match_reg[0] = 0x04;
		match_reg[1] = 0x0a;
		match_reg[2] = 0x80;
	}

	for (i = len; i > 0; i--) {
		if (reglist[i].reg == OV2710_ANA_ARRAR01)
			reglist[i].val = match_reg[0];
		else if (reglist[i].reg == OV2710_TIMING_CONCTROL_VH)
			reglist[i].val = match_reg[1];
		else if (reglist[i].reg == OV2710_TIMING_CONCTROL18)
			reglist[i].val = match_reg[2];
	}

	return 0;
}

/*--------------------------------------------------------------------------*/
static int OV2710_g_VTS(struct ov_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ret = ov_camera_module_read_reg_table(
		cam_mod,
		OV2710_TIMING_VTS_HIGH_REG,
		&msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = ov_camera_module_read_reg_table(
		cam_mod,
		OV2710_TIMING_VTS_LOW_REG,
		&lsb);
	if (IS_ERR_VALUE(ret))
		goto err;

	*vts = (msb << 8) | lsb;

	return 0;
err:
	ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int OV2710_auto_adjust_fps(struct ov_camera_module *cam_mod,
	u32 exp_time)
{
	int ret;
	u32 vts;

	if ((cam_mod->exp_config.exp_time + OV2710_COARSE_INTG_TIME_MAX_MARGIN)
		> cam_mod->vts_min)
		vts = cam_mod->exp_config.exp_time +
			OV2710_COARSE_INTG_TIME_MAX_MARGIN;
	else
		vts = cam_mod->vts_min;

	/*
	 * if (cam_mod->fps_ctrl > 0 && cam_mod->fps_ctrl < 100)
	 * vts = vts * 100 / cam_mod->fps_ctrl;
	 */

	if (vts > 0xfff)
		vts = 0xfff;
	else
		vts = vts;/* VTS value is 0x380e[3:0]/380f[7:0] */

	ret = ov_camera_module_write_reg(cam_mod,
		OV2710_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= ov_camera_module_write_reg(cam_mod,
		OV2710_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0x0F);

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod,
				"failed with error (%d)\n", ret);
	else
		ov_camera_module_pr_debug(cam_mod,
			"updated vts = 0x%x,vts_min=0x%x\n",
			vts, cam_mod->vts_min);

	return ret;
}

static int ov2710_write_aec(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod,
		"exp_time = %d lines, gain = %d, flash_mode = %d\n",
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
		u32 exp_time = cam_mod->exp_config.exp_time;

		a_gain = a_gain * cam_mod->exp_config.gain_percent / 100;

		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING)
			ret = ov_camera_module_write_reg(cam_mod,
				OV2710_AEC_GROUP_UPDATE_ADDRESS,
				OV2710_AEC_GROUP_UPDATE_START_DATA);
		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret = OV2710_auto_adjust_fps(cam_mod,
				cam_mod->exp_config.exp_time);
		ret |= ov_camera_module_write_reg(cam_mod,
			OV2710_AEC_PK_LONG_GAIN_HIGH_REG,
			OV2710_FETCH_MSB_GAIN(a_gain));
		ret |= ov_camera_module_write_reg(cam_mod,
			OV2710_AEC_PK_LONG_GAIN_LOW_REG,
			OV2710_FETCH_LSB_GAIN(a_gain));
		ret = ov_camera_module_write_reg(cam_mod,
			OV2710_AEC_PK_LONG_EXPO_3RD_REG,
			OV2710_FETCH_3RD_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			OV2710_AEC_PK_LONG_EXPO_2ND_REG,
			OV2710_FETCH_2ND_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod,
			OV2710_AEC_PK_LONG_EXPO_1ST_REG,
			OV2710_FETCH_1ST_BYTE_EXP(exp_time));
		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING) {
			ret = ov_camera_module_write_reg(cam_mod,
				OV2710_AEC_GROUP_UPDATE_ADDRESS,
				OV2710_AEC_GROUP_UPDATE_END_DATA);
			ret = ov_camera_module_write_reg(cam_mod,
				OV2710_AEC_GROUP_UPDATE_ADDRESS,
				OV2710_AEC_GROUP_UPDATE_END_LAUNCH);
		}
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int ov2710_g_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
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

static int ov2710_filltimings(struct ov_camera_module_custom_config *custom)
{
	int i, j;
	u32 win_h_off = 0, win_v_off = 0;
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
			case OV2710_TIMING_VTS_HIGH_REG:
				timings->frame_length_lines =
					((reg_table[j].val << 8) |
					(timings->frame_length_lines & 0xff));
				break;
			case OV2710_TIMING_VTS_LOW_REG:
				timings->frame_length_lines =
					(reg_table[j].val |
					(timings->frame_length_lines & 0xff00));
				break;
			case OV2710_TIMING_HTS_HIGH_REG:
				timings->line_length_pck =
					((reg_table[j].val << 8) |
					timings->line_length_pck);
				break;
			case OV2710_TIMING_HTS_LOW_REG:
				timings->line_length_pck =
					(reg_table[j].val |
					(timings->line_length_pck & 0xff00));
				break;
			case OV2710_TIMING_X_INC:
				timings->binning_factor_x =
					((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_x == 0)
					timings->binning_factor_x = 1;
				break;
			case OV2710_TIMING_Y_INC:
				timings->binning_factor_y =
					((reg_table[j].val >> 4) + 1) / 2;
				if (timings->binning_factor_y == 0)
					timings->binning_factor_y = 1;
				break;
			case OV2710_HORIZONTAL_START_HIGH_REG:
				timings->crop_horizontal_start =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_start &
					0xff));
				break;
			case OV2710_HORIZONTAL_START_LOW_REG:
				timings->crop_horizontal_start =
					(reg_table[j].val |
					(timings->crop_horizontal_start &
					0xff00));
				break;
			case OV2710_VERTICAL_START_HIGH_REG:
				timings->crop_vertical_start =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_start & 0xff));
				break;
			case OV2710_VERTICAL_START_LOW_REG:
				timings->crop_vertical_start =
					((reg_table[j].val) |
					(timings->crop_vertical_start &
					0xff00));
				break;
			case OV2710_HORIZONTAL_END_HIGH_REG:
				timings->crop_horizontal_end =
					((reg_table[j].val << 8) |
					(timings->crop_horizontal_end & 0xff));
				break;
			case OV2710_HORIZONTAL_END_LOW_REG:
				timings->crop_horizontal_end =
					(reg_table[j].val |
					(timings->crop_horizontal_end &
					0xff00));
				break;
			case OV2710_VERTICAL_END_HIGH_REG:
				timings->crop_vertical_end =
					((reg_table[j].val << 8) |
					(timings->crop_vertical_end & 0xff));
				break;
			case OV2710_VERTICAL_END_LOW_REG:
				timings->crop_vertical_end =
					(reg_table[j].val |
					(timings->crop_vertical_end & 0xff00));
				break;
			case OV2710_H_WIN_OFF_HIGH_REG:
				win_h_off = (reg_table[j].val & 0xf) << 8;
				break;
			case OV2710_H_WIN_OFF_LOW_REG:
				win_h_off |= (reg_table[j].val & 0xff);
				break;
			case OV2710_V_WIN_OFF_HIGH_REG:
				win_v_off = (reg_table[j].val & 0xf) << 8;
				break;
			case OV2710_V_WIN_OFF_LOW_REG:
				win_v_off |= (reg_table[j].val & 0xff);
				break;
			case OV2710_HORIZONTAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_width =
					((reg_table[j].val << 8) |
					(timings->sensor_output_width & 0xff));
				break;
			case OV2710_HORIZONTAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_width =
					(reg_table[j].val |
					(timings->sensor_output_width &
					0xff00));
				break;
			case OV2710_VERTICAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_height =
					((reg_table[j].val << 8) |
					(timings->sensor_output_height & 0xff));
				break;
			case OV2710_VERTICAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_height =
					(reg_table[j].val |
					(timings->sensor_output_height &
					0xff00));
				break;
			}
		}

		timings->crop_horizontal_start += win_h_off;
		timings->crop_horizontal_end -= win_h_off;
		timings->crop_vertical_start += win_v_off;
		timings->crop_vertical_end -= win_v_off;

		timings->exp_time >>= 4;
		timings->vt_pix_clk_freq_hz =
			config->frm_intrvl.interval.denominator *
			timings->frame_length_lines *
			timings->line_length_pck;

		timings->coarse_integration_time_min =
			OV2710_COARSE_INTG_TIME_MIN;
		timings->coarse_integration_time_max_margin =
			OV2710_COARSE_INTG_TIME_MAX_MARGIN;

		/* OV Sensor do not use fine integration time. */
		timings->fine_integration_time_min =
			OV2710_FINE_INTG_TIME_MIN;
		timings->fine_integration_time_max_margin =
			OV2710_FINE_INTG_TIME_MAX_MARGIN;
	}

	return 0;
}

/*--------------------------------------------------------------------------*/

static int ov2710_g_timings(struct ov_camera_module *cam_mod,
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
			cam_mod->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;
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

static int ov2710_s_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = ov2710_write_aec(cam_mod);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		/* todo*/
		break;
	/*
	 * case RK_V4L2_CID_FPS_CTRL:
	 * if (cam_mod->auto_adjust_fps)
	 * ret = OV2710_auto_adjust_fps(
	 * cam_mod,
	 * cam_mod->exp_config.exp_time);
	 * break;
	 */
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov2710_s_ext_ctrls(struct ov_camera_module *cam_mod,
				 struct ov_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	/* Handles only exposure and gain together special case. */
	if (ctrls->count == 1)
		ret = ov2710_s_ctrl(cam_mod, ctrls->ctrls[0].id);
	else if ((ctrls->count == 3) &&
		(ctrls->ctrls[0].id == V4L2_CID_GAIN ||
		ctrls->ctrls[0].id == V4L2_CID_EXPOSURE ||
		ctrls->ctrls[1].id == V4L2_CID_GAIN ||
		ctrls->ctrls[1].id == V4L2_CID_EXPOSURE))
		ret = ov2710_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);

	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov2710_start_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod,
		"active config=%s\n", cam_mod->active_config->name);

	ret = OV2710_g_VTS(cam_mod, &cam_mod->vts_min);
	if (IS_ERR_VALUE(ret))
		goto err;
	ov_camera_module_pr_debug(cam_mod, "=====streaming on ===\n");
	ret = ov_camera_module_write_reg(cam_mod, 0x3008, 0x02);
	ret |= ov_camera_module_write_reg(cam_mod, 0x4201, 0x00);
	ret |= ov_camera_module_write_reg(cam_mod, 0x4202, 0x00);

	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n",
		ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov2710_stop_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");
	ret = ov_camera_module_write_reg(cam_mod, 0x3008, 0x42);
	ret |= ov_camera_module_write_reg(cam_mod, 0x4201, 0x00);
	ret |= ov_camera_module_write_reg(cam_mod, 0x4202, 0x0f);

	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int ov2710_check_camera_id(struct ov_camera_module *cam_mod)
{
	u32 pidh, pidl;
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret |= ov_camera_module_read_reg(cam_mod, 1, OV2710_PIDH_ADDR, &pidh);
	ret |= ov_camera_module_read_reg(cam_mod, 1, OV2710_PIDL_ADDR, &pidl);
	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if ((pidh == OV2710_PIDH_MAGIC) && (pidl == OV2710_PIDL_MAGIC))
		ov_camera_module_pr_debug(cam_mod,
			"successfully detected camera ID 0x%02x%02x\n",
			pidh, pidl);
	else {
		ov_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			OV2710_PIDH_MAGIC, OV2710_PIDL_MAGIC, pidh, pidl);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
int ov_camera_2710_module_s_ctrl(
	struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	return 0;
}

/* ======================================================================== */

int ov_camera_2710_module_s_ext_ctrls(
	struct v4l2_subdev *sd,
	struct v4l2_ext_controls *ctrls)
{
	return 0;
}

long ov_camera_2710_module_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd,
	void *arg)
{
	return 0;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */

static struct v4l2_subdev_core_ops ov2710_camera_module_core_ops = {
	.g_ctrl = ov_camera_module_g_ctrl,
	.s_ctrl = ov_camera_module_s_ctrl,
	.s_ext_ctrls = ov_camera_module_s_ext_ctrls,
	.s_power = ov_camera_module_s_power,
	.ioctl = ov_camera_module_ioctl
};

static struct v4l2_subdev_video_ops ov2710_camera_module_video_ops = {
	.s_frame_interval = ov_camera_module_s_frame_interval,
	.s_stream = ov_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops ov2710_camera_module_pad_ops = {
	.enum_frame_interval = ov_camera_module_enum_frameintervals,
	.get_fmt = ov_camera_module_g_fmt,
	.set_fmt = ov_camera_module_s_fmt,
};

static struct v4l2_subdev_ops ov2710_camera_module_ops = {
	.core = &ov2710_camera_module_core_ops,
	.video = &ov2710_camera_module_video_ops,
	.pad = &ov2710_camera_module_pad_ops
};

static struct ov_camera_module ov2710;

static struct ov_camera_module_custom_config ov2710_custom_config = {
	.start_streaming = ov2710_start_streaming,
	.stop_streaming = ov2710_stop_streaming,
	.s_ctrl = ov2710_s_ctrl,
	.g_ctrl = ov2710_g_ctrl,
	.s_ext_ctrls = ov2710_s_ext_ctrls,
	.g_timings = ov2710_g_timings,
	.set_flip = ov2710_set_flip,
	.check_camera_id = ov2710_check_camera_id,
	.configs = ov2710_configs,
	.num_configs = ARRAY_SIZE(ov2710_configs),
	.power_up_delays_ms = {5, 30, 30}
};

static int ov2710_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");

	ov2710_filltimings(&ov2710_custom_config);
	v4l2_i2c_subdev_init(&ov2710.sd, client,
				&ov2710_camera_module_ops);

	ov2710.custom = ov2710_custom_config;

	dev_info(&client->dev, "probing successful\n");
	return 0;
}

/* ======================================================================== */

static int ov2710_remove(
	struct i2c_client *client)
{
	struct ov_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	ov_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id ov2710_id[] = {
	{ OV2710_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id ov2710_of_match[] = {
	{.compatible = "omnivision,ov2710-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov2710_id);

static struct i2c_driver ov2710_i2c_driver = {
	.driver = {
		.name = OV2710_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = ov2710_of_match
	},
	.probe = ov2710_probe,
	.remove = ov2710_remove,
	.id_table = ov2710_id,
};

module_i2c_driver(ov2710_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ov2710");
MODULE_AUTHOR("Eike Grimpe");
MODULE_LICENSE("GPL");

