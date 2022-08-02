// SPDX-License-Identifier: GPL-2.0
/*
 * vehicle sensor max96714
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 * Authors:
 *	Jianwei Fan <jianwei.fan@rock-chips.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/sysctl.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include "vehicle_cfg.h"
#include "vehicle_main.h"
#include "vehicle_ad.h"
#include "vehicle_ad_max96714.h"

enum {
	CVSTD_720P60 = 0,
	CVSTD_720P50,
	CVSTD_1080P30,
	CVSTD_1080P25,
	CVSTD_720P30,
	CVSTD_720P25,
	CVSTD_SVGAP30,
	CVSTD_SD,
	CVSTD_NTSC,
	CVSTD_PAL
};

enum {
	FORCE_PAL_WIDTH = 960,
	FORCE_PAL_HEIGHT = 576,
	FORCE_NTSC_WIDTH = 960,
	FORCE_NTSC_HEIGHT = 480,
	FORCE_SVGA_WIDTH = 800,
	FORCE_SVGA_HEIGHT = 600,
	FORCE_720P_WIDTH = 1280,
	FORCE_720P_HEIGHT = 720,
	FORCE_1080P_WIDTH = 1920,
	FORCE_1080P_HEIGHT = 1080,
	FORCE_CIF_OUTPUT_FORMAT = CIF_OUTPUT_FORMAT_420,
};

enum {
	VIDEO_UNPLUG,
	VIDEO_IN,
	VIDEO_LOCKED,
	VIDEO_UNLOCK
};

#define FLAG_LOCKED			(0x1 << 3)
#define MAX96714_LINK_FREQ_150M		150000000UL

static struct vehicle_ad_dev *max96714_g_addev;
static int cvstd_mode = CVSTD_1080P30;
//static int cvstd_old = CVSTD_720P25;
static int cvstd_old = CVSTD_NTSC;

//static int cvstd_sd = CVSTD_NTSC;
static int cvstd_state = VIDEO_UNPLUG;
static int cvstd_old_state = VIDEO_UNLOCK;

static bool g_max96714_streaming;

#define SENSOR_VALUE_LEN	1	/* sensor register value bytes*/
#define MAX96714_CHIP_ID	0xC9
#define MAX96714_CHIP_ID_REG	0x0D
#define MAX96714_GMSL_STATE	0x0013
#define MAX96714_STREAM_CTL	0x0313
#define MAX96714_MODE_SW_STANDBY	0x0
#define MAX96714_MODE_STREAMING		BIT(1)

struct regval {
	u16 reg;
	u8 val;
};
#define REG_NULL  0xFFFF

/* 1080p Preview resolution setting*/
static struct regval sensor_preview_data_1080p_30hz[] = {
	{0x0313, 0x00},
	{0x0001, 0x01},
	{0x0010, 0x21},
	{0x0320, 0x23},
	{0x0325, 0x80},
	{0x0313, 0x00},
	{REG_NULL, 0x00},
};

static struct rkmodule_csi_dphy_param max96714_dcphy_param = {
	.vendor = PHY_VENDOR_SAMSUNG,
	.lp_vol_ref = 3,
	.lp_hys_sw = {3, 0, 0, 0},
	.lp_escclk_pol_sel = {1, 0, 0, 0},
	.skew_data_cal_clk = {0, 3, 3, 3},
	.clk_hs_term_sel = 2,
	.data_hs_term_sel = {2, 2, 2, 2},
	.reserved = {0},
};

static int max96714_read_reg(struct vehicle_ad_dev *ad, u16 reg,
			    unsigned int len, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = ad->i2c_add;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = ad->i2c_add;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(ad->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int max96714_write_reg(struct vehicle_ad_dev *ad, u16 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[3];
	int ret;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	buf[2] = val;

	msg.addr = ad->i2c_add;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(ad->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	VEHICLE_DGERR(
		"max96714 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int max96714_write_array(struct vehicle_ad_dev *ad,
				const struct regval *regs)
{
	u32 i = 0;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].reg != REG_NULL; i++)
		ret = max96714_write_reg(ad, regs[i].reg, regs[i].val);

	return ret;
}

static void max96714_reinit_parameter(struct vehicle_ad_dev *ad, unsigned char cvstd)
{
	int i = 0;

	switch (cvstd) {
	case CVSTD_1080P30:
		ad->cfg.width = 1920;
		ad->cfg.height = 1080;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_YUV;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;/*00 - UYVY*/
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 30;
		ad->cfg.mipi_freq = MAX96714_LINK_FREQ_150M;
		break;

	default:
		ad->cfg.width = 1920;
		ad->cfg.height = 1080;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_YUV;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;/*00 - UYVY*/
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 30;
		ad->cfg.mipi_freq = MAX96714_LINK_FREQ_150M;
		break;
	}
	ad->cfg.type = V4L2_MBUS_CSI2_DPHY;
	ad->cfg.mbus_flags = V4L2_MBUS_CSI2_4_LANE | V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
			 V4L2_MBUS_CSI2_CHANNEL_0;
	ad->cfg.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8;
	ad->cfg.dphy_param = &max96714_dcphy_param;

	switch (ad->cfg.mbus_flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_1_LANE:
		ad->cfg.lanes = 1;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		ad->cfg.lanes = 2;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		ad->cfg.lanes = 3;
		break;
	case V4L2_MBUS_CSI2_4_LANE:
		ad->cfg.lanes = 4;
		break;
	default:
		ad->cfg.lanes = 1;
		break;
	}

	/* fix crop info from dts config */
	for (i = 0; i < 4; i++) {
		if ((ad->defrects[i].width == ad->cfg.width) &&
		    (ad->defrects[i].height == ad->cfg.height)) {
			ad->cfg.start_x = ad->defrects[i].crop_x;
			ad->cfg.start_y = ad->defrects[i].crop_y;
			ad->cfg.width = ad->defrects[i].crop_width;
			ad->cfg.height = ad->defrects[i].crop_height;
		}
	}

	VEHICLE_DG("crop(%d,%d)", ad->cfg.start_x, ad->cfg.start_y);
}

static void max96714_reg_init(struct vehicle_ad_dev *ad, unsigned char cvstd)
{
	struct regval *sensor;
	int ret = 0;

	switch (cvstd) {
	case CVSTD_1080P30:
		VEHICLE_INFO("%s, init CVSTD_1080P30 mode", __func__);
		sensor = sensor_preview_data_1080p_30hz;
		break;
	default:
		VEHICLE_INFO("%s, init CVSTD_1080P30 mode", __func__);
		sensor = sensor_preview_data_1080p_30hz;
		break;
	}

	ret = max96714_write_array(ad, sensor);
	if (ret)
		VEHICLE_DGERR("%s, init sensor fail", __func__);
}

void max96714_channel_set(struct vehicle_ad_dev *ad, int channel)
{
}

int max96714_ad_get_cfg(struct vehicle_cfg **cfg)
{
	if (!max96714_g_addev)
		return -1;

	switch (cvstd_state) {
	case VIDEO_UNPLUG:
		max96714_g_addev->cfg.ad_ready = false;
		break;
	case VIDEO_LOCKED:
		max96714_g_addev->cfg.ad_ready = true;
		break;
	case VIDEO_IN:
		max96714_g_addev->cfg.ad_ready = false;
		break;
	}

	max96714_g_addev->cfg.ad_ready = true;

	*cfg = &max96714_g_addev->cfg;

	return 0;
}

void max96714_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line)
{
	VEHICLE_DG("last_line %d\n", last_line);

	if (last_line < 1)
		return;

	ad->cif_error_last_line = last_line;
	if (cvstd_mode == CVSTD_PAL) {
		if (last_line == FORCE_NTSC_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	} else if (cvstd_mode == CVSTD_NTSC) {
		if (last_line == FORCE_PAL_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	} else if (cvstd_mode == CVSTD_1080P30) {
		if (last_line == FORCE_1080P_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	}
}

int max96714_check_id(struct vehicle_ad_dev *ad)
{
	int ret = 0;
	u32 pid = 0;

	ret = max96714_read_reg(ad, MAX96714_CHIP_ID_REG, SENSOR_VALUE_LEN, &pid);
	if (pid != MAX96714_CHIP_ID) {
		VEHICLE_DGERR("%s: expected 0xC9, detected: 0x%02x !",
		    ad->ad_name, pid);
		ret = -EINVAL;
	} else {
		VEHICLE_INFO("Found MAX96714 sensor: id(0x%2x) !\n", pid);
	}

	return ret;
}

static int max96714_check_cvstd(struct vehicle_ad_dev *ad, bool activate_check)
{
	static int state = VIDEO_UNPLUG;
	int ret = 0;

	ret = max96714_read_reg(ad, MAX96714_GMSL_STATE, SENSOR_VALUE_LEN, &state);
	if (ret)
		VEHICLE_DGERR("read GMSL2 link lock failed!\n");

	if (state & FLAG_LOCKED) {
		state = VIDEO_LOCKED;
		VEHICLE_DG("GMSL2 link locked!\n");
		cvstd_mode = CVSTD_1080P30;
	} else {
		state = VIDEO_UNPLUG;
		VEHICLE_DG("GMSL2 link not locked!\n");
		cvstd_mode = cvstd_old;
	}

	return 0;
}

int max96714_stream(struct vehicle_ad_dev *ad, int enable)
{
	VEHICLE_INFO("%s on(%d)\n", __func__, enable);

	g_max96714_streaming = (enable != 0);
	if (g_max96714_streaming) {
		max96714_write_reg(ad, MAX96714_STREAM_CTL, MAX96714_MODE_STREAMING);
		if (ad->state_check_work.state_check_wq)
			queue_delayed_work(ad->state_check_work.state_check_wq,
				&ad->state_check_work.work, msecs_to_jiffies(200));
	} else {
		max96714_write_reg(ad, MAX96714_STREAM_CTL, MAX96714_MODE_SW_STANDBY);
		if (ad->state_check_work.state_check_wq)
			cancel_delayed_work_sync(&ad->state_check_work.work);
	}

	return 0;
}

static void max96714_power_on(struct vehicle_ad_dev *ad)
{
	/* gpio_direction_output(ad->power, ad->pwr_active); */
	if (gpio_is_valid(ad->power)) {
		gpio_request(ad->power, "max96714_power");
		gpio_direction_output(ad->power, ad->pwr_active);
		/* gpio_set_value(ad->power, ad->pwr_active); */
	}

	if (gpio_is_valid(ad->powerdown)) {
		gpio_request(ad->powerdown, "max96714_pwd");
		gpio_direction_output(ad->powerdown, 1);
		/* gpio_set_value(ad->powerdown, !ad->pwdn_active); */
	}

	if (gpio_is_valid(ad->reset)) {
		gpio_request(ad->reset, "max96714_rst");
		gpio_direction_output(ad->reset, 0);
		usleep_range(1500, 2000);
		gpio_direction_output(ad->reset, 1);
	}
}

static void max96714_power_deinit(struct vehicle_ad_dev *ad)
{
	if (gpio_is_valid(ad->reset))
		gpio_free(ad->reset);
	if (gpio_is_valid(ad->power))
		gpio_free(ad->power);
	if (gpio_is_valid(ad->powerdown))
		gpio_free(ad->powerdown);
}

static void max96714_check_state_work(struct work_struct *work)
{
	struct vehicle_ad_dev *ad;

	ad = max96714_g_addev;

	if (ad->cif_error_last_line > 0) {
		max96714_check_cvstd(ad, true);
		ad->cif_error_last_line = 0;
	} else {
		max96714_check_cvstd(ad, false);
	}

	VEHICLE_DG("%s:cvstd_old(%d), cvstd_mode(%d)\n", __func__, cvstd_old, cvstd_mode);
	if (cvstd_old != cvstd_mode || cvstd_old_state != cvstd_state) {
		VEHICLE_INFO("%s:ad sensor std mode change, cvstd_old(%d), cvstd_mode(%d)\n",
				 __func__, cvstd_old, cvstd_mode);
		cvstd_old = cvstd_mode;
		cvstd_old_state = cvstd_state;
		max96714_reinit_parameter(ad, cvstd_mode);
		max96714_reg_init(ad, cvstd_mode);
		vehicle_ad_stat_change_notify();
	}
	if (g_max96714_streaming) {
		queue_delayed_work(ad->state_check_work.state_check_wq,
			&ad->state_check_work.work, msecs_to_jiffies(100));
	}
}

int max96714_ad_deinit(void)
{
	struct vehicle_ad_dev *ad;

	ad = max96714_g_addev;

	if (!ad)
		return -ENODEV;

	if (ad->state_check_work.state_check_wq) {
		cancel_delayed_work_sync(&ad->state_check_work.work);
		flush_delayed_work(&ad->state_check_work.work);
		flush_workqueue(ad->state_check_work.state_check_wq);
		destroy_workqueue(ad->state_check_work.state_check_wq);
	}
	if (ad->irq)
		free_irq(ad->irq, ad);
	max96714_power_deinit(ad);

	return 0;
}

static __maybe_unused int get_ad_mode_from_fix_format(int fix_format)
{
	int mode = -1;

	switch (fix_format) {
	case AD_FIX_FORMAT_PAL:
	case AD_FIX_FORMAT_NTSC:
	case AD_FIX_FORMAT_720P_50FPS:
	case AD_FIX_FORMAT_720P_30FPS:
	case AD_FIX_FORMAT_720P_25FPS:
		mode = CVSTD_720P25;
		break;
	case AD_FIX_FORMAT_1080P_30FPS:
	case AD_FIX_FORMAT_1080P_25FPS:

	default:
		mode = CVSTD_1080P30;
		break;
	}

	return mode;
}

int max96714_ad_init(struct vehicle_ad_dev *ad)
{
	max96714_g_addev = ad;

	/*  1. i2c init */
	while (ad->adapter == NULL) {
		ad->adapter = i2c_get_adapter(ad->i2c_chl);
		usleep_range(10000, 12000);
	}
	if (ad->adapter == NULL)
		return -ENODEV;

	if (!i2c_check_functionality(ad->adapter, I2C_FUNC_I2C))
		return -EIO;

	max96714_power_on(ad);

	max96714_reg_init(ad, cvstd_mode);

	max96714_reinit_parameter(ad, cvstd_mode);

	INIT_DELAYED_WORK(&ad->state_check_work.work, max96714_check_state_work);
	ad->state_check_work.state_check_wq =
		create_singlethread_workqueue("vehicle-ad-max96714");

	queue_delayed_work(ad->state_check_work.state_check_wq,
			   &ad->state_check_work.work, msecs_to_jiffies(100));

	return 0;
}
