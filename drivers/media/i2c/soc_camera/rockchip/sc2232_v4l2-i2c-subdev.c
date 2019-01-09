/*
 * sc2232 sensor driver
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
 *v0.1.0:
 *1. Initialize version;
 *
 */

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf-core.h>
#include <linux/slab.h>
#include <media/v4l2-controls_rockchip.h>
#include "sc_camera_module.h"

#define SC2232_DRIVER_NAME "sc2232"

#define SC2232_AEC_PK_LONG_GAIN_HIGH_REG 0x3e08
#define SC2232_AEC_PK_LONG_GAIN_LOW_REG 0x3e09

#define SC2232_AEC_PK_LONG_EXPO_HIGH_REG 0x3e01
#define SC2232_AEC_PK_LONG_EXPO_LOW_REG 0x3e02

#define SC2232_AEC_GROUP_UPDATE_ADDRESS 0x3812
#define SC2232_AEC_GROUP_UPDATE_START_DATA 0x00
#define SC2232_AEC_GROUP_UPDATE_END_DATA 0x30

#define SC2232_PIDH_ADDR 0x3107
#define SC2232_PIDL_ADDR 0x3108

/* High byte of product ID */
#define SC2232_PIDH_MAGIC 0x22
/* Low byte of product ID  */
#define SC2232_PIDL_MAGIC 0x32

#define SC2232_EXT_CLK 24000000
#define SC2232_TIMING_VTS_HIGH_REG 0x320e
#define SC2232_TIMING_VTS_LOW_REG 0x320f
#define SC2232_TIMING_HTS_HIGH_REG 0x320c
#define SC2232_TIMING_HTS_LOW_REG 0x320d
#define SC2232_FINE_INTG_TIME_MIN 0
#define SC2232_FINE_INTG_TIME_MAX_MARGIN 0
#define SC2232_COARSE_INTG_TIME_MIN 1
#define SC2232_COARSE_INTG_TIME_MAX_MARGIN 4
#define SC2232_HORIZONTAL_OUTPUT_SIZE_HIGH_REG 0x3208
#define SC2232_HORIZONTAL_OUTPUT_SIZE_LOW_REG 0x3209
#define SC2232_VERTICAL_OUTPUT_SIZE_HIGH_REG 0x320a
#define SC2232_VERTICAL_OUTPUT_SIZE_LOW_REG 0x320b

#define SC2232_MIRROR_FILP 0x3221

static struct sc_camera_module sc2232;

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */
/* MCLK:24MHz  1920x1080  30fps   mipi 2lane  10bit  390Mbps/lane */
static struct sc_camera_module_reg sc2232_init_tab_1920_1080_30fps[] = {
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x0100, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3001, 0xfe},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3018, 0x33},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3031, 0x0a},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3034, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3035, 0x9b},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3037, 0x20},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3038, 0xff},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3039, 0x54},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x303a, 0xb3},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x303b, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x303c, 0x0e},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320c, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320d, 0x20},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320e, 0x04},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x320f, 0xe2},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3211, 0x0c},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3213, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3222, 0x29},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3235, 0x09},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3236, 0xc2},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3301, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3302, 0x1f},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3303, 0x20},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3306, 0x48},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3308, 0x10},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3309, 0x60},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x330b, 0xd3},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x330e, 0x30},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3314, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x331b, 0x83},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x331e, 0x19},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x331f, 0x59},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3320, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3326, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3333, 0x30},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x335e, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x335f, 0x03},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3366, 0x7c},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3367, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3368, 0x04},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3369, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x336a, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x336b, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x337c, 0x04},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x337d, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x337f, 0x03},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x33a0, 0x05},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x33aa, 0x10},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x360f, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3620, 0x28},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3621, 0x28},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3622, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3624, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3625, 0x02},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3630, 0x1c},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3631, 0x84},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3632, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3633, 0x4f},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3635, 0xa0},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3636, 0x25},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3637, 0x59},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3638, 0x1f},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3639, 0x09},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x363b, 0x0b},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x363c, 0x05},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x366e, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x366f, 0x2f},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3670, 0x0c},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3671, 0xc6},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3672, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3673, 0x16},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3677, 0x84},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3678, 0x88},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3679, 0x88},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x367a, 0x28},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x367b, 0x3f},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x367e, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x367f, 0x28},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3690, 0x34},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3691, 0x11},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3692, 0x42},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x369c, 0x08},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x369d, 0x28},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3802, 0x01},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3901, 0x02},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3902, 0x45},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3905, 0x98},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3907, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3908, 0x11},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x391b, 0x80},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e00, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e01, 0x8c},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e02, 0xa0},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e03, 0x03},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e06, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e07, 0x80},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e08, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e09, 0x10},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3e1e, 0x34},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3f00, 0x07},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3f04, 0x03},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x3f05, 0xec},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x4603, 0x00},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x4827, 0x48},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x4837, 0x34},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x5000, 0x06},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x5780, 0xff},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x5781, 0x04},
{SC_CAMERA_MODULE_REG_TYPE_DATA, 0x5785, 0x18},
};

/* ======================================================================== */

static struct sc_camera_module_config sc2232_configs[] = {
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
		.reg_table = (void *)sc2232_init_tab_1920_1080_30fps,
		.reg_table_num_entries =
			ARRAY_SIZE(sc2232_init_tab_1920_1080_30fps),
		.v_blanking_time_us = 3078,
		.max_exp_gain_h = 16,
		.max_exp_gain_l = 0,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 2, 390, SC2232_EXT_CLK)
	}
};

static int sc2232_set_flip(struct sc_camera_module *cam_mod,
	struct pltfrm_camera_module_reg reglist[],
	int len)
{
	int i, mode = 0;
	u16 orientation = 0;

	mode = sc_camera_module_get_flip_mirror(cam_mod);

	if (mode == -1) {
		sc_camera_module_pr_debug(cam_mod,
			"dts don't set flip, return!\n");
		return 0;
	}

	if (!IS_ERR_OR_NULL(cam_mod->active_config)) {
		if (mode == SC_MIRROR_BIT_MASK)
			orientation |= 0x06;
		if (mode == SC_FLIP_BIT_MASK)
			orientation |= 0x60;
		for (i = 0; i < len; i++) {
			if (reglist[i].reg == SC2232_MIRROR_FILP)
				reglist[i].val = orientation;
		}
	}

	return 0;
}

static int SC2232_g_VTS(struct sc_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ret = sc_camera_module_read_reg_table(cam_mod,
		SC2232_TIMING_VTS_HIGH_REG,
		&msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = sc_camera_module_read_reg_table(cam_mod,
		SC2232_TIMING_VTS_LOW_REG,
		&lsb);
	if (IS_ERR_VALUE(ret))
		goto err;

	*vts = (msb << 8) | lsb;

	return 0;
err:
	sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int sc2232_auto_adjust_fps(struct sc_camera_module *cam_mod,
	u32 exp_time)
{
	int ret;
	u32 vts;

	exp_time = exp_time << 1;

	if ((exp_time + SC2232_COARSE_INTG_TIME_MAX_MARGIN) >
			2 * cam_mod->vts_min)
		vts = (exp_time + SC2232_COARSE_INTG_TIME_MAX_MARGIN) >> 1;
	else
		vts = cam_mod->vts_min;

	ret = sc_camera_module_write_reg(cam_mod,
		SC2232_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= sc_camera_module_write_reg(cam_mod,
		SC2232_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		sc_camera_module_pr_info(cam_mod,
			"updated vts = 0x%x,vts_min=0x%x\n",
				vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}

	return ret;
}

static int sc2232_set_vts(struct sc_camera_module *cam_mod,
	u32 vts)
{
	int ret = 0;

	if (vts <= cam_mod->vts_min)
		return ret;

	ret = sc_camera_module_write_reg(cam_mod,
		SC2232_TIMING_VTS_LOW_REG,
		vts & 0xFF);
	ret |= sc_camera_module_write_reg(cam_mod,
		SC2232_TIMING_VTS_HIGH_REG,
		(vts >> 8) & 0xFF);

	if (IS_ERR_VALUE(ret)) {
		sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	} else {
		sc_camera_module_pr_info(cam_mod,
			"updated vts = 0x%x,vts_min=0x%x\n",
				vts, cam_mod->vts_min);
		cam_mod->vts_cur = vts;
	}

	return ret;
}

static int sc2232_write_aec(struct sc_camera_module *cam_mod)
{
	int ret = 0;

	sc_camera_module_pr_debug(cam_mod,
		"exp_time = %d lines, gain = %d, flash_mode = %d\n",
		cam_mod->exp_config.exp_time,
		cam_mod->exp_config.gain,
		cam_mod->exp_config.flash_mode);

	/*
	 * if the sensor is already streaming, write to shadow registers,
	 * if the sensor is in SW standby, write to active registers,
	 * if the sensor is off/registers are not writeable, do nothing
	 */

	if (cam_mod->state == SC_CAMERA_MODULE_SW_STANDBY ||
		cam_mod->state == SC_CAMERA_MODULE_STREAMING) {
		u32 a_gain = cam_mod->exp_config.gain;
		u32 exp_time = cam_mod->exp_config.exp_time;

		a_gain = a_gain * cam_mod->exp_config.gain_percent / 100;

		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret |= sc2232_auto_adjust_fps(cam_mod,
				cam_mod->exp_config.exp_time);

		if (a_gain > 0xf04)
			a_gain = 0xf04;
		exp_time = exp_time << 1;
		if (exp_time > 0xfff)
			exp_time = 0xfff;

		/* hold reg start */
		mutex_lock(&cam_mod->lock);
		ret = sc_camera_module_write_reg(cam_mod,
			SC2232_AEC_GROUP_UPDATE_ADDRESS,
			SC2232_AEC_GROUP_UPDATE_START_DATA);

		ret |= sc_camera_module_write_reg(cam_mod,
			SC2232_AEC_PK_LONG_GAIN_HIGH_REG,
			(a_gain >> 8) & 0xff);
		ret |= sc_camera_module_write_reg(cam_mod,
			SC2232_AEC_PK_LONG_GAIN_LOW_REG,
			a_gain & 0xff);

		if (a_gain < 0x20) {//1x~2x
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3301, 0x06);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3306, 0x48);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3632, 0x08);
		} else if (a_gain < 0x40) {//2x~4x
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3301, 0x14);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3306, 0x48);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3632, 0x08);
		} else if (a_gain < 0x80) {//4x~8x
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3301, 0x18);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3306, 0x48);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3632, 0x08);
		} else if (a_gain < 0xf8) {//8x~15.5x
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3301, 0x13);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3306, 0x48);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3632, 0x08);
		} else if (a_gain < 0x1f0) {//15.5x~31x
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3301, 0xc5);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3306, 0x78);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3632, 0x48);
		} else {//31x~
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3301, 0xc5);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3306, 0x78);
			ret |= sc_camera_module_write_reg(cam_mod,
				0x3632, 0x78);
		}

		ret |= sc_camera_module_write_reg(cam_mod,
			SC2232_AEC_PK_LONG_EXPO_HIGH_REG,
			(exp_time >> 4) & 0xff);
		ret |= sc_camera_module_write_reg(cam_mod,
			SC2232_AEC_PK_LONG_EXPO_LOW_REG,
			(exp_time & 0xff) << 4);

		if (!cam_mod->auto_adjust_fps)
			ret |= sc2232_set_vts(cam_mod,
				cam_mod->exp_config.vts_value);

		/* hold reg end */
		ret |= sc_camera_module_write_reg(cam_mod,
			SC2232_AEC_GROUP_UPDATE_ADDRESS,
			SC2232_AEC_GROUP_UPDATE_END_DATA);
	mutex_unlock(&cam_mod->lock);
	}

	if (IS_ERR_VALUE(ret))
		sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int sc2232_g_ctrl(struct sc_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	sc_camera_module_pr_debug(cam_mod, "\n");

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
		sc_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int sc2232_filltimings(struct sc_camera_module_custom_config *custom)
{
	int i, j;
	u32 win_h_off = 0, win_v_off = 0;
	struct sc_camera_module_config *config;
	struct sc_camera_module_timings *timings;
	struct sc_camera_module_reg *reg_table;
	int reg_table_num_entries;

	for (i = 0; i < custom->num_configs; i++) {
		config = &custom->configs[i];
		reg_table = config->reg_table;
		reg_table_num_entries = config->reg_table_num_entries;
		timings = &config->timings;

		memset(timings, 0x00, sizeof(*timings));
		for (j = 0; j < reg_table_num_entries; j++) {
			switch (reg_table[j].reg) {
			case SC2232_TIMING_VTS_HIGH_REG:
				if (timings->frame_length_lines & 0xff00)
					timings->frame_length_lines = 0;
				timings->frame_length_lines =
					((reg_table[j].val << 8) |
					(timings->frame_length_lines & 0xff));
				break;
			case SC2232_TIMING_VTS_LOW_REG:
				timings->frame_length_lines =
					(reg_table[j].val |
					(timings->frame_length_lines & 0xff00));
				break;
			case SC2232_TIMING_HTS_HIGH_REG:
				if (timings->line_length_pck & 0xff00)
					timings->line_length_pck = 0;
				timings->line_length_pck =
					((reg_table[j].val << 8) |
					timings->line_length_pck);
				break;
			case SC2232_TIMING_HTS_LOW_REG:
				timings->line_length_pck =
					(reg_table[j].val |
					(timings->line_length_pck & 0xff00));
				break;
			case SC2232_HORIZONTAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_width =
					((reg_table[j].val << 8) |
					(timings->sensor_output_width & 0xff));
				break;
			case SC2232_HORIZONTAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_width =
					(reg_table[j].val |
					(timings->sensor_output_width & 0xff00));
				break;
			case SC2232_VERTICAL_OUTPUT_SIZE_HIGH_REG:
				timings->sensor_output_height =
					((reg_table[j].val << 8) |
					(timings->sensor_output_height & 0xff));
				break;
			case SC2232_VERTICAL_OUTPUT_SIZE_LOW_REG:
				timings->sensor_output_height =
					(reg_table[j].val |
					(timings->sensor_output_height & 0xff00));
				break;
			}
		}

		timings->crop_horizontal_start += win_h_off;
		timings->crop_horizontal_end -= win_h_off;
		timings->crop_vertical_start += win_v_off;
		timings->crop_vertical_end -= win_v_off;

		timings->vt_pix_clk_freq_hz =
			config->frm_intrvl.interval.denominator
			* timings->frame_length_lines
			* timings->line_length_pck;

		timings->coarse_integration_time_min =
			SC2232_COARSE_INTG_TIME_MIN;
		timings->coarse_integration_time_max_margin =
			SC2232_COARSE_INTG_TIME_MAX_MARGIN;

		/* OV Sensor do not use fine integration time. */
		timings->fine_integration_time_min =
			SC2232_FINE_INTG_TIME_MIN;
		timings->fine_integration_time_max_margin =
			SC2232_FINE_INTG_TIME_MAX_MARGIN;
	}

	return 0;
}

/*--------------------------------------------------------------------------*/

static int sc2232_g_timings(struct sc_camera_module *cam_mod,
	struct sc_camera_module_timings *timings)
{
	int ret = 0;
	unsigned int vts;

	if (IS_ERR_OR_NULL(cam_mod->active_config))
		goto err;

	*timings = cam_mod->active_config->timings;

	vts = (!cam_mod->vts_cur) ?
		timings->frame_length_lines :
		cam_mod->vts_cur;

	if (cam_mod->frm_intrvl_valid) {
		timings->vt_pix_clk_freq_hz =
			cam_mod->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;
	} else {
		timings->vt_pix_clk_freq_hz =
			cam_mod->active_config->frm_intrvl.interval.denominator
			* vts
			* timings->line_length_pck;
	}

	timings->frame_length_lines = vts;

	return ret;
err:
	sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int sc2232_s_ctrl(struct sc_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	sc_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = sc2232_write_aec(cam_mod);
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
	 * ret = OV2740_auto_adjust_fps(
	 * cam_mod,
	 * cam_mod->exp_config.exp_time);
	 * break;
	 */
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		sc_camera_module_pr_err(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int sc2232_s_ext_ctrls(struct sc_camera_module *cam_mod,
	struct sc_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	if ((ctrls->ctrls[0].id == V4L2_CID_GAIN ||
		ctrls->ctrls[0].id == V4L2_CID_EXPOSURE))
		ret = sc2232_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		sc_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);

	return ret;
}

/*--------------------------------------------------------------------------*/

static int sc2232_start_streaming(struct sc_camera_module *cam_mod)
{
	int ret = 0;

	sc_camera_module_pr_info(cam_mod, "active config=%s\n",
		cam_mod->active_config->name);

	ret = SC2232_g_VTS(cam_mod, &cam_mod->vts_min);
	if (IS_ERR_VALUE(ret))
		goto err;

	mutex_lock(&cam_mod->lock);
	ret = sc_camera_module_write_reg(cam_mod, 0x0100, 0x01);
	mutex_unlock(&cam_mod->lock);
	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	sc_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int sc2232_stop_streaming(struct sc_camera_module *cam_mod)
{
	int ret = 0;

	sc_camera_module_pr_info(cam_mod, "\n");
	mutex_lock(&cam_mod->lock);
	ret = sc_camera_module_write_reg(cam_mod, 0x0100, 0x00);
	mutex_unlock(&cam_mod->lock);
	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	sc_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int sc2232_check_camera_id(struct sc_camera_module *cam_mod)
{
	u32 pidh, pidl;
	int ret = 0;

	sc_camera_module_pr_debug(cam_mod, "\n");

	ret |= sc_camera_module_read_reg(cam_mod, 1,
		SC2232_PIDH_ADDR, &pidh);
	ret |= sc_camera_module_read_reg(cam_mod, 1,
		SC2232_PIDL_ADDR, &pidl);

	if (IS_ERR_VALUE(ret)) {
		sc_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if (pidh == SC2232_PIDH_MAGIC && pidl == SC2232_PIDL_MAGIC) {
		sc_camera_module_pr_info(cam_mod,
			"successfully detected camera ID 0x%02x%02x\n",
			pidh, pidl);
	} else {
		sc_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			SC2232_PIDH_MAGIC, SC2232_PIDL_MAGIC, pidh, pidl);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	sc_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */

static struct v4l2_subdev_core_ops sc2232_camera_module_core_ops = {
	.g_ctrl = sc_camera_module_g_ctrl,
	.s_ctrl = sc_camera_module_s_ctrl,
	.s_ext_ctrls = sc_camera_module_s_ext_ctrls,
	.s_power = sc_camera_module_s_power,
	.ioctl = sc_camera_module_ioctl
};

static struct v4l2_subdev_video_ops sc2232_camera_module_video_ops = {
	.s_frame_interval = sc_camera_module_s_frame_interval,
	.g_frame_interval = sc_camera_module_g_frame_interval,
	.s_stream = sc_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops sc2232_camera_module_pad_ops = {
	.enum_frame_interval = sc_camera_module_enum_frameintervals,
	.get_fmt = sc_camera_module_g_fmt,
	.set_fmt = sc_camera_module_s_fmt,
};

static struct v4l2_subdev_ops sc2232_camera_module_ops = {
	.core = &sc2232_camera_module_core_ops,
	.video = &sc2232_camera_module_video_ops,
	.pad = &sc2232_camera_module_pad_ops
};

static struct sc_camera_module_custom_config sc2232_custom_config = {
	.start_streaming = sc2232_start_streaming,
	.stop_streaming = sc2232_stop_streaming,
	.s_ctrl = sc2232_s_ctrl,
	.g_ctrl = sc2232_g_ctrl,
	.s_ext_ctrls = sc2232_s_ext_ctrls,
	.g_timings = sc2232_g_timings,
	.s_vts = sc2232_auto_adjust_fps,
	.set_flip = sc2232_set_flip,
	.check_camera_id = sc2232_check_camera_id,
	.configs = sc2232_configs,
	.num_configs = ARRAY_SIZE(sc2232_configs),
	.power_up_delays_ms = {5, 30, 30},
	.exposure_valid_frame = {4, 4}
};

static int sc2232_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");

	sc2232_filltimings(&sc2232_custom_config);

	v4l2_i2c_subdev_init(&sc2232.sd, client,
		&sc2232_camera_module_ops);
	sc2232.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sc2232.custom = sc2232_custom_config;
	mutex_init(&sc2232.lock);
	dev_info(&client->dev, "probing successful\n");

	return 0;
}

/* ======================================================================== */

static int sc2232_remove(struct i2c_client *client)
{
	struct sc_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */
	mutex_destroy(&cam_mod->lock);
	sc_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id sc2232_id[] = {
	{ SC2232_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id sc2232_of_match[] = {
	{.compatible = "smartsens,sc2232-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, sc2232_id);

static struct i2c_driver sc2232_i2c_driver = {
	.driver = {
		.name = SC2232_DRIVER_NAME,
		.of_match_table = sc2232_of_match
	},
	.probe = sc2232_probe,
	.remove = sc2232_remove,
	.id_table = sc2232_id,
};

module_i2c_driver(sc2232_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for sc2232");
MODULE_AUTHOR("zack.zeng");
MODULE_LICENSE("GPL");

