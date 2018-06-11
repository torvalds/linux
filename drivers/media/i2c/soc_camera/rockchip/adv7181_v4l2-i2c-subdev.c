/*
 * adv7181 sensor driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * Copyright (C) 2012-2014 Intel Mobile Communications GmbH
 *
 * Copyright (C) 2008 Texas Instruments.
 *
 * Author: zhoupeng <benjo.zhou@rock-chips.com>
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
#include <linux/debugfs.h>
#include <media/v4l2-controls_rockchip.h>
#include "adv_camera_module.h"

#define ADV7181_DRIVER_NAME "adv7181"

/* product ID */
#define ADV7181_PID_MAGIC	0x20
#define ADV7181_PID_ADDR	0x11

#define ADV7181_STATUS1_REG		0x10
#define ADV7181_STATUS1_IN_LOCK		0x01
#define ADV7181_STATUS1_AUTOD_MASK	0x70
#define ADV7181_STATUS1_AUTOD_NTSM_M_J	0x00
#define ADV7181_STATUS1_AUTOD_NTSC_4_43 0x10
#define ADV7181_STATUS1_AUTOD_PAL_M	0x20
#define ADV7181_STATUS1_AUTOD_PAL_60	0x30
#define ADV7181_STATUS1_AUTOD_PAL_B_G	0x40
#define ADV7181_STATUS1_AUTOD_SECAM	0x50
#define ADV7181_STATUS1_AUTOD_PAL_COMB	0x60
#define ADV7181_STATUS1_AUTOD_SECAM_525	0x70

#define ADV7181_INPUT_CONTROL		0x00
#define ADV7181_INPUT_DEFAULT		0x00
#define ADV7181_INPUT_CVBS_AIN2		0x00
#define ADV7181_INPUT_CVBS_AIN3		0x01
#define ADV7181_INPUT_CVBS_AIN5		0x02
#define ADV7181_INPUT_CVBS_AIN6		0x03
#define ADV7181_INPUT_CVBS_AIN8		0x04
#define ADV7181_INPUT_CVBS_AIN10	0x05
#define ADV7181_INPUT_CVBS_AIN1		0x0B
#define ADV7181_INPUT_CVBS_AIN4		0x0D
#define ADV7181_INPUT_CVBS_AIN7		0x0F
#define ADV7181_INPUT_YPRPB_AIN6_8_10	0x09

#define ADV7181_EXT_CLK 24000000

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */
/* resolution 720x480  30fps */
static struct adv_camera_module_reg adv7180_cvbs_30fps[] = {
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x04, 0x77},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x17, 0x41},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x1D, 0x47},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x31, 0x02},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x3A, 0x17},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x3B, 0x81},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x3D, 0xA2},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x3E, 0x6A},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F, 0xA0},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x86, 0x0B},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xF3, 0x01},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xF9, 0x03},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x0E, 0x80},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x52, 0x46},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x54, 0x80},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x7F, 0xFF},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x81, 0x30},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x90, 0xC9},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x91, 0x40},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x92, 0x3C},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x93, 0xCA},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x94, 0xD5},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xB1, 0xFF},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xB6, 0x08},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xC0, 0x9A},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xCF, 0x50},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xD0, 0x4E},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xD1, 0xB9},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xD6, 0xDD},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xD7, 0xE2},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xE5, 0x51},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0xF6, 0x3B},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x0E, 0x00},
	/* disable out put data */
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0x03, 0x4C},
	{ADV_CAMERA_MODULE_REG_TYPE_DATA, 0X00, 0X0B},

};

/* ======================================================================== */
static struct adv_camera_module_config adv7181_configs[] = {
	/* For normal preview NTSC 480i */
	{
		.name = "adv7180_cvbs_ntsc_30fps",
		.frm_fmt = {
			.width			= 720,
			.height			= 480,
			.code			= MEDIA_BUS_FMT_UYVY8_2X8
		},
		.frm_intrvl = {
			.interval = {
				.numerator	= 1,
				.denominator	= 30
			}
		},
		.reg_table			= (void *)adv7180_cvbs_30fps,
		.reg_table_num_entries		=
			sizeof(adv7180_cvbs_30fps)
			/
			sizeof(adv7180_cvbs_30fps[0]),
		.v_blanking_time_us		= 0,
		.max_exp_gain_h = 16,
		.max_exp_gain_l = 0,
		.ignore_measurement_check = 1,
		PLTFRM_CAM_ITF_DVP_CFG(
			PLTFRM_CAM_ITF_BT656_8I,
			PLTFRM_CAM_SIGNAL_HIGH_LEVEL,
			PLTFRM_CAM_SIGNAL_HIGH_LEVEL,
			PLTFRM_CAM_SDR_NEG_EDG,
			ADV7181_EXT_CLK)
	},
	/* For normal preview PAL 576i */
	{
		.name = "adv7180_cvbs_pal_25fps",
		.frm_fmt = {
			.width			= 720,
			.height			= 576,
			.code			= MEDIA_BUS_FMT_UYVY8_2X8
		},
		.frm_intrvl = {
			.interval = {
				.numerator	= 1,
				.denominator	= 25
			}
		},
		.reg_table			= (void *)adv7180_cvbs_30fps,
		.reg_table_num_entries		=
			sizeof(adv7180_cvbs_30fps)
			/
			sizeof(adv7180_cvbs_30fps[0]),
		.v_blanking_time_us		= 0,
		.max_exp_gain_h = 16,
		.max_exp_gain_l = 0,
		.ignore_measurement_check = 1,
		PLTFRM_CAM_ITF_DVP_CFG(
			PLTFRM_CAM_ITF_BT656_8I,
			PLTFRM_CAM_SIGNAL_HIGH_LEVEL,
			PLTFRM_CAM_SIGNAL_HIGH_LEVEL,
			PLTFRM_CAM_SDR_NEG_EDG,
			ADV7181_EXT_CLK)
	},
};

/*--------------------------------------------------------------------------*/
static int adv7181_set_flip(
	struct adv_camera_module *cam_mod,
	struct pltfrm_camera_module_reg reglist[],
	int len)
{
	return 0;
}

static int adv7181_g_ctrl(struct adv_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	adv_camera_module_pr_debug(cam_mod, "\n");

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
		adv_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int adv7181_g_timings(struct adv_camera_module *cam_mod,
			     struct adv_camera_module_timings *timings)
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

	timings->frame_length_lines = vts;

	return ret;
err:
	adv_camera_module_pr_err(cam_mod,
				 "failed with error (%d)\n",
				 ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int adv7181_s_ctrl(struct adv_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	adv_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		break;
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		/* todo*/
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		adv_camera_module_pr_err(cam_mod,
					 "failed with error (%d)\n",
					 ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int adv7181_s_ext_ctrls(struct adv_camera_module *cam_mod,
			       struct adv_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	/* Handles only exposure and gain together special case. */
	if (ctrls->count == 1)
		ret = adv7181_s_ctrl(cam_mod, ctrls->ctrls[0].id);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		adv_camera_module_pr_debug(cam_mod,
			"failed with error (%d)\n", ret);

	return ret;
}

/*--------------------------------------------------------------------------*/

static int adv7181_start_streaming(struct adv_camera_module *cam_mod)
{
	int ret = 0;

	adv_camera_module_pr_debug(cam_mod,
		"active config=%s\n", cam_mod->active_config->name);

	adv_camera_module_pr_debug(cam_mod, "=====streaming on ===\n");
	ret = adv_camera_module_write_reg(cam_mod, 0x03, 0x0c);

	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	adv_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int adv7181_stop_streaming(struct adv_camera_module *cam_mod)
{
	int ret = 0;

	adv_camera_module_pr_debug(cam_mod, "\n");
	ret = adv_camera_module_write_reg(cam_mod, 0x03, 0x4c);

	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	adv_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int adv7181_check_camera_id(struct adv_camera_module *cam_mod)
{
	u8 pid;
	int ret = 0;

	adv_camera_module_pr_err(cam_mod, "\n");

	ret |= adv_camera_module_read_reg(cam_mod, ADV7181_PID_ADDR, &pid);
	if (IS_ERR_VALUE(ret)) {
		adv_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off?\n");
		goto err;
	}

	if (pid == ADV7181_PID_MAGIC)
		adv_camera_module_pr_err(cam_mod,
			"successfully detected camera ID 0x%02x\n",
			pid);
	else {
		adv_camera_module_pr_err(cam_mod,
			"wrong camera ID, expected 0x%02x, detected 0x%02x\n",
			ADV7181_PID_MAGIC, pid);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	adv_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
int adv_camera_7181_module_s_ctrl(
	struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	return 0;
}

/* ======================================================================== */

int adv_camera_7181_module_s_ext_ctrls(
	struct v4l2_subdev *sd,
	struct v4l2_ext_controls *ctrls)
{
	return 0;
}

long adv_camera_7181_module_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd,
	void *arg)
{
	return 0;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */

static struct v4l2_subdev_core_ops adv7181_camera_module_core_ops = {
	.g_ctrl = adv_camera_module_g_ctrl,
	.s_ctrl = adv_camera_module_s_ctrl,
	.s_ext_ctrls = adv_camera_module_s_ext_ctrls,
	.s_power = adv_camera_module_s_power,
	.ioctl = adv_camera_module_ioctl
};

static struct v4l2_subdev_video_ops adv7181_camera_module_video_ops = {
	.s_frame_interval = adv_camera_module_s_frame_interval,
	.g_frame_interval = adv_camera_module_g_frame_interval,
	.s_stream = adv_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops adv7181_camera_module_pad_ops = {
	.enum_frame_interval = adv_camera_module_enum_frameintervals,
	.get_fmt = adv_camera_module_g_fmt,
	.set_fmt = adv_camera_module_s_fmt,
};

static struct v4l2_subdev_ops adv7181_camera_module_ops = {
	.core = &adv7181_camera_module_core_ops,
	.video = &adv7181_camera_module_video_ops,
	.pad = &adv7181_camera_module_pad_ops
};

static struct adv_camera_module adv7181;

static struct adv_camera_module_custom_config adv7181_custom_config = {
	.start_streaming = adv7181_start_streaming,
	.stop_streaming = adv7181_stop_streaming,
	.s_ctrl = adv7181_s_ctrl,
	.g_ctrl = adv7181_g_ctrl,
	.s_ext_ctrls = adv7181_s_ext_ctrls,
	.g_timings = adv7181_g_timings,
	.set_flip = adv7181_set_flip,
	.check_camera_id = adv7181_check_camera_id,
	.configs = adv7181_configs,
	.num_configs = ARRAY_SIZE(adv7181_configs),
	.power_up_delays_ms = {5, 30, 30},
	/*
	*0: Exposure time valid fileds;
	*1: Exposure gain valid fileds;
	*(2 fileds == 1 frames)
	*/
	.exposure_valid_frame = {4, 4}
};

static ssize_t adv7181_debugfs_reg_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *ppos)
{
	struct adv_camera_module *cam_mod =
		((struct seq_file *)file->private_data)->private;

	char kbuf[30];
	int reg;
	int reg_value;
	int ret;
	int nbytes = min(count, sizeof(kbuf) - 1);

	if (copy_from_user(kbuf, buf, nbytes))
		return -EFAULT;

	kbuf[nbytes] = '\0';
	adv_camera_module_pr_err(cam_mod, "kbuf is %s\n", kbuf);
	ret = sscanf(kbuf, " %x %x", &reg, &reg_value);
	adv_camera_module_pr_err(cam_mod, "ret = %d!\n", ret);
	if (ret != 2) {
		adv_camera_module_pr_err(cam_mod, "sscanf failed!\n");
		return 0;
	}

	adv_camera_module_write_reg(cam_mod, (u8)reg, (u8)reg_value);
	adv_camera_module_pr_err(cam_mod,
				"%s(%d): read reg 0x%02x ---> 0x%x!\n",
				__func__, __LINE__,
				reg, reg_value);

	return count;
}

static int adv7181_debugfs_reg_show(struct seq_file *s, void *v)
{
	int i;
	u8 val;
	struct adv_camera_module *cam_mod = s->private;

	adv_camera_module_pr_err(cam_mod, "test\n");

	for (i = 0; i <= 0xff; i++) {
		adv_camera_module_read_reg(cam_mod, (u8)i, &val);
		seq_printf(s, "0x%02x : 0x%02x\n", i, val);
	}
	return 0;
}

static int adv7181_debugfs_open(struct inode *inode, struct file *file)
{
	struct specific_sensor *spsensor = inode->i_private;

	return single_open(file, adv7181_debugfs_reg_show, spsensor);
}

static const struct file_operations adv7181_debugfs_fops = {
	.owner			= THIS_MODULE,
	.open			= adv7181_debugfs_open,
	.read			= seq_read,
	.write			= adv7181_debugfs_reg_write,
	.llseek			= seq_lseek,
	.release		= single_release
};

static struct dentry *debugfs_dir;

static int adv7181_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	dev_info(&client->dev, "probing...\n");

	v4l2_i2c_subdev_init(&adv7181.sd, client,
				&adv7181_camera_module_ops);

	adv7181.custom = adv7181_custom_config;

	debugfs_dir = debugfs_create_dir("adv7181", NULL);
	if (IS_ERR(debugfs_dir))
		printk(KERN_ERR "%s(%d): create debugfs dir failed!\n",
		       __func__, __LINE__);
	else
		debugfs_create_file("register", S_IRUSR,
				    debugfs_dir, &adv7181,
				    &adv7181_debugfs_fops);

	dev_info(&client->dev, "probing successful\n");
	return 0;
}

/* ======================================================================== */

static int adv7181_remove(
	struct i2c_client *client)
{
	struct adv_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "remadving device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	adv_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id adv7181_id[] = {
	{ ADV7181_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id adv7181_of_match[] = {
	{.compatible = "adi,adv7181-v4l2-i2c-subdev"},
	{},
};

MODULE_DEVICE_TABLE(i2c, adv7181_id);

static struct i2c_driver adv7181_i2c_driver = {
	.driver = {
		.name = ADV7181_DRIVER_NAME,
		.of_match_table = adv7181_of_match
	},
	.probe = adv7181_probe,
	.remove = adv7181_remove,
	.id_table = adv7181_id,
};

module_i2c_driver(adv7181_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for adv7181");
MODULE_AUTHOR("Benjo");
MODULE_LICENSE("GPL");

