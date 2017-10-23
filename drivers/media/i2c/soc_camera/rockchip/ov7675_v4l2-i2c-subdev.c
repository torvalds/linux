/*
 * ov7675 sensor driver
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
#include "ov_camera_module.h"

#define OV7675_CAMERA_MODULE_DATA (PLTFRM_CAMERA_MODULE_WR_SINGLE | \
PLTFRM_CAMERA_MODULE_RD_CONTINUE | PLTFRM_CAMERA_MODULE_REG1_TYPE_DATA1)

#define ov7675_DRIVER_NAME "ov7675"

#define ov7675_PIDH_ADDR                       0x0a
#define ov7675_PIDL_ADDR                       0x0b

#define ov7675_EXT_CLK                         24000000
/* High byte of product ID */
#define ov7675_PIDH_MAGIC                      0x76
/* Low byte of product ID  */
#define ov7675_PIDL_MAGIC                      0x73

static struct ov_camera_module ov7675;
static struct ov_camera_module_custom_config ov7675_custom_config;

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */
static struct ov_camera_module_reg ov7675_init_tab_640_480_30fps[] = {
	{OV7675_CAMERA_MODULE_DATA, 0x12, 0x80},
	{OV7675_CAMERA_MODULE_DATA, 0x09, 0x10},
	{OV7675_CAMERA_MODULE_DATA, 0xc1, 0x7f},
	{OV7675_CAMERA_MODULE_DATA, 0x11, 0x80},
	{OV7675_CAMERA_MODULE_DATA, 0x3a, 0x0c},
	{OV7675_CAMERA_MODULE_DATA, 0x3d, 0xc0},
	{OV7675_CAMERA_MODULE_DATA, 0x12, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x15, 0x40},
	{OV7675_CAMERA_MODULE_DATA, 0x17, 0x13},
	{OV7675_CAMERA_MODULE_DATA, 0x18, 0x01},
	{OV7675_CAMERA_MODULE_DATA, 0x32, 0xbf},
	{OV7675_CAMERA_MODULE_DATA, 0x19, 0x02},
	{OV7675_CAMERA_MODULE_DATA, 0x1a, 0x7a},
	{OV7675_CAMERA_MODULE_DATA, 0x03, 0x0a},
	{OV7675_CAMERA_MODULE_DATA, 0x0c, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x3e, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x70, 0x3a},
	{OV7675_CAMERA_MODULE_DATA, 0x71, 0x35},
	{OV7675_CAMERA_MODULE_DATA, 0x72, 0x11},
	{OV7675_CAMERA_MODULE_DATA, 0x73, 0xf0},
	{OV7675_CAMERA_MODULE_DATA, 0xa2, 0x02},
	{OV7675_CAMERA_MODULE_DATA, 0x7a, 0x20},
	{OV7675_CAMERA_MODULE_DATA, 0x7b, 0x03},
	{OV7675_CAMERA_MODULE_DATA, 0x7c, 0x0a},
	{OV7675_CAMERA_MODULE_DATA, 0x7d, 0x1a},
	{OV7675_CAMERA_MODULE_DATA, 0x7e, 0x3f},
	{OV7675_CAMERA_MODULE_DATA, 0x7f, 0x4e},
	{OV7675_CAMERA_MODULE_DATA, 0x80, 0x5b},
	{OV7675_CAMERA_MODULE_DATA, 0x81, 0x68},
	{OV7675_CAMERA_MODULE_DATA, 0x82, 0x75},
	{OV7675_CAMERA_MODULE_DATA, 0x83, 0x7f},
	{OV7675_CAMERA_MODULE_DATA, 0x84, 0x89},
	{OV7675_CAMERA_MODULE_DATA, 0x85, 0x9a},
	{OV7675_CAMERA_MODULE_DATA, 0x86, 0xa6},
	{OV7675_CAMERA_MODULE_DATA, 0x87, 0xbd},
	{OV7675_CAMERA_MODULE_DATA, 0x88, 0xd3},
	{OV7675_CAMERA_MODULE_DATA, 0x89, 0xe8},
	{OV7675_CAMERA_MODULE_DATA, 0x13, 0xe0},
	{OV7675_CAMERA_MODULE_DATA, 0x00, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x10, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x0d, 0x40},
	{OV7675_CAMERA_MODULE_DATA, 0x14, 0x28},
	{OV7675_CAMERA_MODULE_DATA, 0xa5, 0x02},
	{OV7675_CAMERA_MODULE_DATA, 0xab, 0x02},
	{OV7675_CAMERA_MODULE_DATA, 0x24, 0x68},
	{OV7675_CAMERA_MODULE_DATA, 0x25, 0x58},
	{OV7675_CAMERA_MODULE_DATA, 0x26, 0xc2},
	{OV7675_CAMERA_MODULE_DATA, 0x9f, 0x78},
	{OV7675_CAMERA_MODULE_DATA, 0xa0, 0x68},
	{OV7675_CAMERA_MODULE_DATA, 0xa1, 0x03},
	{OV7675_CAMERA_MODULE_DATA, 0xa6, 0xd8},
	{OV7675_CAMERA_MODULE_DATA, 0xa7, 0xd8},
	{OV7675_CAMERA_MODULE_DATA, 0xa8, 0xf0},
	{OV7675_CAMERA_MODULE_DATA, 0xa9, 0x90},
	{OV7675_CAMERA_MODULE_DATA, 0xaa, 0x14},
	{OV7675_CAMERA_MODULE_DATA, 0x13, 0xe5},
	{OV7675_CAMERA_MODULE_DATA, 0x0e, 0x61},
	{OV7675_CAMERA_MODULE_DATA, 0x0f, 0x4b},
	{OV7675_CAMERA_MODULE_DATA, 0x16, 0x02},
	{OV7675_CAMERA_MODULE_DATA, 0x1e, 0x07},
	{OV7675_CAMERA_MODULE_DATA, 0x21, 0x02},
	{OV7675_CAMERA_MODULE_DATA, 0x22, 0x91},
	{OV7675_CAMERA_MODULE_DATA, 0x29, 0x07},
	{OV7675_CAMERA_MODULE_DATA, 0x33, 0x0b},
	{OV7675_CAMERA_MODULE_DATA, 0x35, 0x0b},
	{OV7675_CAMERA_MODULE_DATA, 0x37, 0x1d},
	{OV7675_CAMERA_MODULE_DATA, 0x38, 0x71},
	{OV7675_CAMERA_MODULE_DATA, 0x39, 0x2a},
	{OV7675_CAMERA_MODULE_DATA, 0x3c, 0x78},
	{OV7675_CAMERA_MODULE_DATA, 0x4d, 0x40},
	{OV7675_CAMERA_MODULE_DATA, 0x4e, 0x20},
	{OV7675_CAMERA_MODULE_DATA, 0x69, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x6b, 0x0a},
	{OV7675_CAMERA_MODULE_DATA, 0x74, 0x10},
	{OV7675_CAMERA_MODULE_DATA, 0x8d, 0x4f},
	{OV7675_CAMERA_MODULE_DATA, 0x8e, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x8f, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x90, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x91, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x96, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x9a, 0x80},
	{OV7675_CAMERA_MODULE_DATA, 0xb0, 0x84},
	{OV7675_CAMERA_MODULE_DATA, 0xb1, 0x0c},
	{OV7675_CAMERA_MODULE_DATA, 0xb2, 0x0e},
	{OV7675_CAMERA_MODULE_DATA, 0xb3, 0x82},
	{OV7675_CAMERA_MODULE_DATA, 0xb8, 0x0a},
	{OV7675_CAMERA_MODULE_DATA, 0x43, 0x0a},
	{OV7675_CAMERA_MODULE_DATA, 0x44, 0xf2},
	{OV7675_CAMERA_MODULE_DATA, 0x45, 0x39},
	{OV7675_CAMERA_MODULE_DATA, 0x46, 0x62},
	{OV7675_CAMERA_MODULE_DATA, 0x47, 0x3d},
	{OV7675_CAMERA_MODULE_DATA, 0x48, 0x55},
	{OV7675_CAMERA_MODULE_DATA, 0x59, 0x83},
	{OV7675_CAMERA_MODULE_DATA, 0x5a, 0x0d},
	{OV7675_CAMERA_MODULE_DATA, 0x5b, 0xcd},
	{OV7675_CAMERA_MODULE_DATA, 0x5c, 0x8c},
	{OV7675_CAMERA_MODULE_DATA, 0x5d, 0x77},
	{OV7675_CAMERA_MODULE_DATA, 0x5e, 0x16},
	{OV7675_CAMERA_MODULE_DATA, 0x6c, 0x0a},
	{OV7675_CAMERA_MODULE_DATA, 0x6d, 0x65},
	{OV7675_CAMERA_MODULE_DATA, 0x6e, 0x11},
	{OV7675_CAMERA_MODULE_DATA, 0x6f, 0x9e},
	{OV7675_CAMERA_MODULE_DATA, 0x6a, 0x40},
	{OV7675_CAMERA_MODULE_DATA, 0x01, 0x56},
	{OV7675_CAMERA_MODULE_DATA, 0x02, 0x44},
	{OV7675_CAMERA_MODULE_DATA, 0x13, 0xe7},
	{OV7675_CAMERA_MODULE_DATA, 0x4f, 0x88},
	{OV7675_CAMERA_MODULE_DATA, 0x50, 0x8b},
	{OV7675_CAMERA_MODULE_DATA, 0x51, 0x04},
	{OV7675_CAMERA_MODULE_DATA, 0x52, 0x11},
	{OV7675_CAMERA_MODULE_DATA, 0x53, 0x8c},
	{OV7675_CAMERA_MODULE_DATA, 0x54, 0x9d},
	{OV7675_CAMERA_MODULE_DATA, 0x55, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x56, 0x40},
	{OV7675_CAMERA_MODULE_DATA, 0x57, 0x80},
	{OV7675_CAMERA_MODULE_DATA, 0x58, 0x9a},
	{OV7675_CAMERA_MODULE_DATA, 0x41, 0x08},
	{OV7675_CAMERA_MODULE_DATA, 0x3f, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x75, 0x04},
	{OV7675_CAMERA_MODULE_DATA, 0x76, 0x60},
	{OV7675_CAMERA_MODULE_DATA, 0x4c, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x77, 0x01},
	{OV7675_CAMERA_MODULE_DATA, 0x3d, 0xc2},
	{OV7675_CAMERA_MODULE_DATA, 0x4b, 0x09},
	{OV7675_CAMERA_MODULE_DATA, 0xc9, 0x30},
	{OV7675_CAMERA_MODULE_DATA, 0x41, 0x38},
	{OV7675_CAMERA_MODULE_DATA, 0x56, 0x40},
	{OV7675_CAMERA_MODULE_DATA, 0x34, 0x11},
	{OV7675_CAMERA_MODULE_DATA, 0x3b, 0x12},
	{OV7675_CAMERA_MODULE_DATA, 0xa4, 0x88},
	{OV7675_CAMERA_MODULE_DATA, 0x96, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x97, 0x30},
	{OV7675_CAMERA_MODULE_DATA, 0x98, 0x20},
	{OV7675_CAMERA_MODULE_DATA, 0x99, 0x30},
	{OV7675_CAMERA_MODULE_DATA, 0x9a, 0x84},
	{OV7675_CAMERA_MODULE_DATA, 0x9b, 0x29},
	{OV7675_CAMERA_MODULE_DATA, 0x9c, 0x03},
	{OV7675_CAMERA_MODULE_DATA, 0x9d, 0x99},
	{OV7675_CAMERA_MODULE_DATA, 0x9e, 0x7f},
	{OV7675_CAMERA_MODULE_DATA, 0x78, 0x04},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x01},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0xf0},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x0f},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x10},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0x7e},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x0a},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0x80},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x0b},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0x01},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x0c},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0x0f},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x0d},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0x20},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x09},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0x80},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x02},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0xc0},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x03},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0x40},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x05},
	{OV7675_CAMERA_MODULE_DATA, 0xc8, 0x30},
	{OV7675_CAMERA_MODULE_DATA, 0x79, 0x26},
	{OV7675_CAMERA_MODULE_DATA, 0x62, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x63, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x64, 0x06},
	{OV7675_CAMERA_MODULE_DATA, 0x65, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x66, 0x05},
	{OV7675_CAMERA_MODULE_DATA, 0x94, 0x05},
	{OV7675_CAMERA_MODULE_DATA, 0x95, 0x09},
	{OV7675_CAMERA_MODULE_DATA, 0x2a, 0x10},
	{OV7675_CAMERA_MODULE_DATA, 0x2b, 0xc2},
	{OV7675_CAMERA_MODULE_DATA, 0x15, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x3a, 0x04},
	{OV7675_CAMERA_MODULE_DATA, 0x3d, 0xc3},
	{OV7675_CAMERA_MODULE_DATA, 0x19, 0x03},
	{OV7675_CAMERA_MODULE_DATA, 0x1a, 0x7b},
	{OV7675_CAMERA_MODULE_DATA, 0x2a, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x2b, 0x00},
	{OV7675_CAMERA_MODULE_DATA, 0x18, 0x01},
	{OV7675_CAMERA_MODULE_DATA, 0x66, 0x05},
	{OV7675_CAMERA_MODULE_DATA, 0x62, 0x10},
	{OV7675_CAMERA_MODULE_DATA, 0x63, 0x0b},
	{OV7675_CAMERA_MODULE_DATA, 0x65, 0x07},
	{OV7675_CAMERA_MODULE_DATA, 0x64, 0x0f},
	{OV7675_CAMERA_MODULE_DATA, 0x94, 0x0e},
	{OV7675_CAMERA_MODULE_DATA, 0x95, 0x10},
	{OV7675_CAMERA_MODULE_DATA, 0x4f, 0x87},
	{OV7675_CAMERA_MODULE_DATA, 0x50, 0x68},
	{OV7675_CAMERA_MODULE_DATA, 0x51, 0x1e},
	{OV7675_CAMERA_MODULE_DATA, 0x52, 0x15},
	{OV7675_CAMERA_MODULE_DATA, 0x53, 0x7c},
	{OV7675_CAMERA_MODULE_DATA, 0x54, 0x91},
	{OV7675_CAMERA_MODULE_DATA, 0x58, 0x1e},
	{OV7675_CAMERA_MODULE_DATA, 0x41, 0x38},
	{OV7675_CAMERA_MODULE_DATA, 0x76, 0xe0},
	{OV7675_CAMERA_MODULE_DATA, 0x24, 0x40},
	{OV7675_CAMERA_MODULE_DATA, 0x25, 0x38},
	{OV7675_CAMERA_MODULE_DATA, 0x26, 0x91},
	{OV7675_CAMERA_MODULE_DATA, 0x7a, 0x09},
	{OV7675_CAMERA_MODULE_DATA, 0x7b, 0x0c},
	{OV7675_CAMERA_MODULE_DATA, 0x7c, 0x16},
	{OV7675_CAMERA_MODULE_DATA, 0x7d, 0x28},
	{OV7675_CAMERA_MODULE_DATA, 0x7e, 0x48},
	{OV7675_CAMERA_MODULE_DATA, 0x7f, 0x57},
	{OV7675_CAMERA_MODULE_DATA, 0x80, 0x64},
	{OV7675_CAMERA_MODULE_DATA, 0x81, 0x71},
	{OV7675_CAMERA_MODULE_DATA, 0x82, 0x7e},
	{OV7675_CAMERA_MODULE_DATA, 0x83, 0x89},
	{OV7675_CAMERA_MODULE_DATA, 0x84, 0x94},
	{OV7675_CAMERA_MODULE_DATA, 0x85, 0xa8},
	{OV7675_CAMERA_MODULE_DATA, 0x86, 0xba},
	{OV7675_CAMERA_MODULE_DATA, 0x87, 0xd7},
	{OV7675_CAMERA_MODULE_DATA, 0x88, 0xec},
	{OV7675_CAMERA_MODULE_DATA, 0x89, 0xf9},
	{OV7675_CAMERA_MODULE_DATA, 0x09, 0x00},
};

/* ======================================================================== */

static struct ov_camera_module_config ov7675_configs[] = {
	{
		.name = "640x480_30fps",
		.frm_fmt = {
			.width = 640,
			.height = 480,
			.code = MEDIA_BUS_FMT_YVYU8_2X8
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
		.reg_table = (void *)ov7675_init_tab_640_480_30fps,
		.reg_table_num_entries =
			sizeof(ov7675_init_tab_640_480_30fps) /
			sizeof(ov7675_init_tab_640_480_30fps[0]),
		.v_blanking_time_us = 1000,
		PLTFRM_CAM_ITF_DVP_CFG(
			PLTFRM_CAM_ITF_BT601_8,
			PLTFRM_CAM_SIGNAL_HIGH_LEVEL,
			PLTFRM_CAM_SIGNAL_HIGH_LEVEL,
			PLTFRM_CAM_SDR_POS_EDG,
			ov7675_EXT_CLK)
	}
};

/*--------------------------------------------------------------------------*/

static int ov7675_write_aec(struct ov_camera_module *cam_mod)
{
	return 0;
}

/*--------------------------------------------------------------------------*/

static int ov7675_g_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_info(cam_mod, "\n");

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

static int ov7675_filltimings(struct ov_camera_module_custom_config *custom)
{
	return 0;
}

static int ov7675_g_timings(struct ov_camera_module *cam_mod,
	struct ov_camera_module_timings *timings)
{
	return 0;
}

static int ov7675_s_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_info(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = ov7675_write_aec(cam_mod);
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

static int ov7675_s_ext_ctrls(struct ov_camera_module *cam_mod,
				 struct ov_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	ov_camera_module_pr_info(cam_mod, "\n");

	/* Handles only exposure and gain together special case. */
	if (ctrls->count == 1)
		ret = ov7675_s_ctrl(cam_mod, ctrls->ctrls[0].id);
	else if ((ctrls->count == 3) &&
		 ((ctrls->ctrls[0].id == V4L2_CID_GAIN &&
		   ctrls->ctrls[1].id == V4L2_CID_EXPOSURE) ||
		  (ctrls->ctrls[1].id == V4L2_CID_GAIN &&
		   ctrls->ctrls[0].id == V4L2_CID_EXPOSURE)))
		ret = ov7675_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

static int ov7675_set_flip(
	struct ov_camera_module *cam_mod,
	struct pltfrm_camera_module_reg reglist[],
	int len)
{
	return 0;
}

static int ov7675_check_camera_id(struct ov_camera_module *cam_mod)
{
	u32 pidh, pidl;
	int ret = 0;

	ov_camera_module_pr_info(cam_mod, "\n");
	ret |= pltfrm_camera_module_read_reg_ex(&cam_mod->sd, 1,
		OV7675_CAMERA_MODULE_DATA, ov7675_PIDH_ADDR, &pidh);
	ret |= pltfrm_camera_module_read_reg_ex(&cam_mod->sd, 1,
		OV7675_CAMERA_MODULE_DATA, ov7675_PIDL_ADDR, &pidl);

	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if ((pidh == ov7675_PIDH_MAGIC) && (pidl == ov7675_PIDL_MAGIC)) {
		ov_camera_module_pr_info(cam_mod,
			"successfully detected camera ID 0x%02x%02x\n",
			pidh, pidl);
	} else {
		ov_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
			ov7675_PIDH_MAGIC, ov7675_PIDL_MAGIC, pidh, pidl);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

static int ov7675_start_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret = pltfrm_camera_module_write_reg_ex(&cam_mod->sd,
		OV7675_CAMERA_MODULE_DATA, 0x09, 0);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

static int ov7675_stop_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret = pltfrm_camera_module_write_reg_ex(&cam_mod->sd,
		OV7675_CAMERA_MODULE_DATA, 0x09, 10);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */
static struct v4l2_subdev_core_ops ov7675_camera_module_core_ops = {
	.g_ctrl = ov_camera_module_g_ctrl,
	.s_ctrl = ov_camera_module_s_ctrl,
	.s_ext_ctrls = ov_camera_module_s_ext_ctrls,
	.s_power = ov_camera_module_s_power,
	.ioctl = ov_camera_module_ioctl
};

static struct v4l2_subdev_video_ops ov7675_camera_module_video_ops = {
	.s_frame_interval = ov_camera_module_s_frame_interval,
	.s_stream = ov_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops ov7675_camera_module_pad_ops = {
	.enum_frame_interval = ov_camera_module_enum_frameintervals,
	.get_fmt = ov_camera_module_g_fmt,
	.set_fmt = ov_camera_module_s_fmt,
};

static struct v4l2_subdev_ops ov7675_camera_module_ops = {
	.core = &ov7675_camera_module_core_ops,
	.video = &ov7675_camera_module_video_ops,
	.pad = &ov7675_camera_module_pad_ops
};

static struct ov_camera_module_custom_config ov7675_custom_config = {
	.start_streaming = ov7675_start_streaming,
	.stop_streaming = ov7675_stop_streaming,
	.s_ctrl = ov7675_s_ctrl,
	.s_ext_ctrls = ov7675_s_ext_ctrls,
	.g_ctrl = ov7675_g_ctrl,
	.g_timings = ov7675_g_timings,
	.check_camera_id = ov7675_check_camera_id,
	.set_flip = ov7675_set_flip,
	.configs = ov7675_configs,
	.num_configs = ARRAY_SIZE(ov7675_configs),
	.power_up_delays_ms = {20, 20, 0}
};

static int ov7675_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");

	ov7675_filltimings(&ov7675_custom_config);
	v4l2_i2c_subdev_init(&ov7675.sd, client, &ov7675_camera_module_ops);

	ov7675.custom = ov7675_custom_config;

	dev_info(&client->dev, "probing successful\n");
	return 0;
}

static int ov7675_remove(struct i2c_client *client)
{
	struct ov_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	ov_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id ov7675_id[] = {
	{ ov7675_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id ov7675_of_match[] = {
	{.compatible = "omnivision,ov7675-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov7675_id);

static struct i2c_driver ov7675_i2c_driver = {
	.driver = {
		.name = ov7675_DRIVER_NAME,
		.of_match_table = ov7675_of_match
	},
	.probe = ov7675_probe,
	.remove = ov7675_remove,
	.id_table = ov7675_id,
};

module_i2c_driver(ov7675_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ov7675");
MODULE_AUTHOR("George");
MODULE_LICENSE("GPL");
