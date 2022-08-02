// SPDX-License-Identifier: GPL-2.0
/*
 * vehicle sensor adv7181
 *
 * Copyright (C) 2022 Rockchip Electronics Co.Ltd
 * Authors:
 *      Zhiqin Wei <wzq@rock-chips.com>
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
#include <linux/videodev2.h>
#include "vehicle_cfg.h"
#include "vehicle_main.h"
#include "vehicle_ad.h"
#include "vehicle_ad_7181.h"

enum {
	FORCE_PAL_WIDTH = 720,
	FORCE_PAL_HEIGHT = 576,
	FORCE_NTSC_WIDTH = 720,
	FORCE_NTSC_HEIGHT = 480,
	FORCE_CIF_OUTPUT_FORMAT = CIF_OUTPUT_FORMAT_420,
};

static struct vehicle_ad_dev *ad7181_g_addev;
static v4l2_std_id std_old = V4L2_STD_NTSC;

#define SENSOR_REGISTER_LEN	1	/* sensor register address bytes*/
#define SENSOR_VALUE_LEN	1	/* sensor register value bytes*/

struct rk_sensor_reg {
	unsigned int reg;
	unsigned int val;
};

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
#define ADV7181_INPUT_YPRPB_AIN6_8_10	0x00

#define SEQCMD_END  0xFF000000
#define SensorEnd   {SEQCMD_END, 0x00}

#define SENSOR_DG VEHICLE_DG

/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] = {
	/* autodetect cvbs in ntsc/pal/secam 8-bit 422 encode */
	{0x00, 0x0B}, /*cvbs in AIN1*/
	{0x04, 0x77},
	{0x17, 0x41},
	{0x1D, 0x47},
	{0x31, 0x02},
	{0x3A, 0x17},
	{0x3B, 0x81},
	{0x3D, 0xA2},
	{0x3E, 0x6A},
	{0x3F, 0xA0},
	{0x86, 0x0B},
	{0xF3, 0x01},
	{0xF9, 0x03},
	{0x0E, 0x80},
	{0x52, 0x46},
	{0x54, 0x80},
	{0x7F, 0xFF},
	{0x81, 0x30},
	{0x90, 0xC9},
	{0x91, 0x40},
	{0x92, 0x3C},
	{0x93, 0xCA},
	{0x94, 0xD5},
	{0xB1, 0xFF},
	{0xB6, 0x08},
	{0xC0, 0x9A},
	{0xCF, 0x50},
	{0xD0, 0x4E},
	{0xD1, 0xB9},
	{0xD6, 0xDD},
	{0xD7, 0xE2},
	{0xE5, 0x51},
	{0xF6, 0x3B},
	{0x0E, 0x00},
	{0x03, 0x4C}, //stream off
	{0xDF, 0X46},
	{0xC9, 0x04},
	{0xC5, 0x81},
	{0xC4, 0x34},
	{0xBf, 0x02},
	{0xB5, 0x83},
	{0xB6, 0x00},
	{0xaf, 0x03},
	{0xae, 0x00},
	{0xac, 0x00},
	{0xAB, 0x00},
	{0xa1, 0xFF},
	{0xA2, 0x00},
	{0xA3, 0x00},
	{0xA4, 0x00},
	{0xa5, 0x01},
	{0xA6, 0x00},
	{0xA6, 0x00},
	{0xA7, 0x00},
	{0xA8, 0x00},
	{0xa0, 0x03},
	{0x98, 0X00},
	{0x97, 0X00},
	{0X90, 0X00},
	{0X85, 0X02},
	{0x7B, 0x1E},
	{0x74, 0x04},
	{0x75, 0x01},
	{0x76, 0x00},
	{0x6B, 0xC0},
	{0x67, 0x03},
	{0x3C, 0x58},
	{0x30, 0x4C},
	{0x2E, 0X9F},
	{0x12, 0XC0},
	{0x10, 0X0D},
	{0x05, 0X00},
	{0x06, 0X02},
	{0x60, 0x01},
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_yprpb_p[] = {
	{0x05, 0x01},
	{0x06, 0x06},
	{0xc3, 0x56},
	{0xc4, 0xb4},
	{0x1d, 0x47},
	{0x3a, 0x11},
	{0x3b, 0x81},
	{0x3c, 0x3b},
	{0x6b, 0x83},
	{0xc9, 0x00},
	{0x73, 0x10},
	{0x74, 0xa3},
	{0x75, 0xe8},
	{0x76, 0xfa},
	{0x7b, 0x1c},
	{0x85, 0x19},
	{0x86, 0x0b},
	{0xbf, 0x06},
	{0xc0, 0x40},
	{0xc1, 0xf0},
	{0xc2, 0x80},
	{0xc5, 0x01},
	{0xc9, 0x08},
	{0x0e, 0x80},
	{0x52, 0x46},
	{0x54, 0x80},
	{0x57, 0x01},
	{0xf6, 0x3b},
	{0x0e, 0x00},
	{0x67, 0x2f},
	{0x03, 0x4C}, //disable out put
	SensorEnd
};

static v4l2_std_id adv7181_std_to_v4l2(u8 status1)
{
	/* in case V4L2_IN_ST_NO_SIGNAL */
	if (!(status1 & ADV7181_STATUS1_IN_LOCK))
		return V4L2_STD_UNKNOWN;

	switch (status1 & ADV7181_STATUS1_AUTOD_MASK) {
	case ADV7181_STATUS1_AUTOD_PAL_M:
	case ADV7181_STATUS1_AUTOD_NTSM_M_J:
		return V4L2_STD_NTSC;
	case ADV7181_STATUS1_AUTOD_NTSC_4_43:
		return V4L2_STD_NTSC_443;
	case ADV7181_STATUS1_AUTOD_PAL_60:
		return V4L2_STD_PAL_60;
	case ADV7181_STATUS1_AUTOD_PAL_B_G:
		return V4L2_STD_PAL;
	case ADV7181_STATUS1_AUTOD_SECAM:
		return V4L2_STD_SECAM;
	case ADV7181_STATUS1_AUTOD_PAL_COMB:
		return V4L2_STD_PAL_Nc | V4L2_STD_PAL_N;
	case ADV7181_STATUS1_AUTOD_SECAM_525:
		return V4L2_STD_SECAM;
	default:
		return V4L2_STD_UNKNOWN;
	}
}

static u32 adv7181_status_to_v4l2(u8 status1)
{
	if (!(status1 & ADV7181_STATUS1_IN_LOCK))
		return V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int adv7181_vehicle_status(struct vehicle_ad_dev *ad,
				  u32 *status,
				  v4l2_std_id *std)
{
	unsigned char status1 = 0;

	status1 = vehicle_generic_sensor_read(ad, ADV7181_STATUS1_REG);
	if (status1)
		return status1;

	if (status)
		*status = adv7181_status_to_v4l2(status1);

	if (std)
		*std = adv7181_std_to_v4l2(status1);

	return 0;
}

static void adv7181_reinit_parameter(struct vehicle_ad_dev *ad, v4l2_std_id std)
{
	int i;

	if (ad7181_g_addev->ad_chl == 0) {
		ad->cfg.width = 1024;
		ad->cfg.height = 500;
		ad->cfg.start_x = 56;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_YUV;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 1;
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 60;
		ad->cfg.type = V4L2_MBUS_PARALLEL;
		ad->cfg.mbus_flags = V4L2_MBUS_HSYNC_ACTIVE_LOW |
					V4L2_MBUS_VSYNC_ACTIVE_LOW |
					V4L2_MBUS_PCLK_SAMPLE_RISING;
	} else if (std == V4L2_STD_PAL) {
		ad->cfg.width = FORCE_PAL_WIDTH;
		ad->cfg.height = FORCE_PAL_HEIGHT;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_PAL;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 0;
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 25;
		ad->cfg.type = V4L2_MBUS_PARALLEL;
		ad->cfg.mbus_flags = V4L2_MBUS_HSYNC_ACTIVE_LOW |
					V4L2_MBUS_VSYNC_ACTIVE_LOW |
					V4L2_MBUS_PCLK_SAMPLE_RISING;
	} else {
		ad->cfg.width = FORCE_NTSC_WIDTH;
		ad->cfg.height = FORCE_NTSC_HEIGHT;
		ad->cfg.start_x = 0;
		ad->cfg.start_y = 0;
		ad->cfg.input_format = CIF_INPUT_FORMAT_NTSC;
		ad->cfg.output_format = FORCE_CIF_OUTPUT_FORMAT;
		ad->cfg.field_order = 0;
		ad->cfg.yuv_order = 2;
		ad->cfg.href = 0;
		ad->cfg.vsync = 0;
		ad->cfg.frame_rate = 30;
		ad->cfg.type = V4L2_MBUS_PARALLEL;
		ad->cfg.mbus_flags = V4L2_MBUS_HSYNC_ACTIVE_LOW |
					V4L2_MBUS_VSYNC_ACTIVE_LOW |
					V4L2_MBUS_PCLK_SAMPLE_RISING;
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

	SENSOR_DG("size %dx%d, crop(%d,%d)\n",
	    ad->cfg.width, ad->cfg.height,
	    ad->cfg.start_x, ad->cfg.start_y);
}

static void adv7181_reg_init(struct vehicle_ad_dev *ad, unsigned char cvstd)
{
	struct rk_sensor_reg *sensor;
	int i = 0;
	unsigned char val[2];

	switch (ad->ad_chl) {
	case 0:
		ad->ad_chl = ADV7181_INPUT_CVBS_AIN1;
		break;
	case 1:
		ad->ad_chl = ADV7181_INPUT_CVBS_AIN6;
		break;
	case 2:
		ad->ad_chl = ADV7181_INPUT_CVBS_AIN8;
		break;
	case 3:
		ad->ad_chl = ADV7181_INPUT_CVBS_AIN10;
		break;
	case 4:
		ad->ad_chl = ADV7181_INPUT_YPRPB_AIN6_8_10;
		break;
	default:
		ad->ad_chl = ADV7181_INPUT_CVBS_AIN1;
	}
	val[0] = ad->ad_chl;
	vehicle_generic_sensor_write(ad, ADV7181_INPUT_CONTROL, val);

	if (ad->ad_chl == ADV7181_INPUT_YPRPB_AIN6_8_10) {
		SENSOR_DG("%s %d set sensor_preview_data_yprpb_p/p", __func__, __LINE__);
		sensor = sensor_preview_data_yprpb_p;
	} else {
		SENSOR_DG("%s %d set n/p", __func__, __LINE__);
		sensor = sensor_preview_data;
	}
	while ((sensor[i].reg != SEQCMD_END) && (sensor[i].reg != 0xFC000000)) {
		if (sensor[i].reg == ADV7181_INPUT_CONTROL) {
			SENSOR_DG("%s %d lkg test ad channel = %d\n",
					__func__, __LINE__, ad->ad_chl);
		} else {
			val[0] = sensor[i].val;
			vehicle_generic_sensor_write(ad, sensor[i].reg, val);
		}
		i++;
	}

	val[0] = ad->ad_chl;
	vehicle_generic_sensor_write(ad, ADV7181_INPUT_CONTROL, val);
}

int adv7181_ad_get_cfg(struct vehicle_cfg **cfg)
{
	u32 status;

	if (!ad7181_g_addev)
		return -1;

	adv7181_vehicle_status(ad7181_g_addev, &status, NULL);

	ad7181_g_addev->cfg.ad_ready = true;

	*cfg = &ad7181_g_addev->cfg;

	return 0;
}

void adv7181_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line)
{
	SENSOR_DG("%s, last_line %d\n", __func__, last_line);
	if (last_line < 1)
		return;

	ad->cif_error_last_line = last_line;
	if (std_old == V4L2_STD_PAL) {
		if (last_line == FORCE_NTSC_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	} else if (std_old == V4L2_STD_NTSC) {
		if (last_line == FORCE_PAL_HEIGHT) {
			if (ad->state_check_work.state_check_wq)
				queue_delayed_work(
					ad->state_check_work.state_check_wq,
					&ad->state_check_work.work,
					msecs_to_jiffies(0));
		}
	}
}

int adv7181_check_id(struct vehicle_ad_dev *ad)
{
	int ret = 0;
	int val;

	val = vehicle_generic_sensor_read(ad, 0x11);
	SENSOR_DG("%s vehicle read 0x11 --> 0x%02x\n", ad->ad_name, val);
	if (val != 0x20) {
		SENSOR_DG("%s vehicle wrong camera ID, expected 0x20, detected 0x%02x\n",
		    ad->ad_name, val);
		ret = -EINVAL;
	}

	return ret;
}

static int adv7181_check_std(struct vehicle_ad_dev *ad, v4l2_std_id *std)
{
	u32 status = 0;

	adv7181_vehicle_status(ad, &status, std);

	if (status != 0) { /* No signal */
		mdelay(30);
		adv7181_vehicle_status(ad, &status, std);
		SENSOR_DG("status 0x%x\n", status);
	}

	return 0;
}
void adv7181_channel_set(struct vehicle_ad_dev *ad, int channel)
{
	static int channel_change = 11;
	v4l2_std_id std = 0;

	ad->ad_chl = channel;
	adv7181_reg_init(ad, std);
	adv7181_check_std(ad, &std);
	adv7181_reinit_parameter(ad, std);
	if (channel_change != ad->ad_chl) {
		SENSOR_DG("%s %d channel changed now channel = %d old_channel = %d\n",
						__func__, __LINE__, ad->ad_chl, channel);
		channel_change = ad->ad_chl;
		vehicle_ad_stat_change_notify();
	}
}

int adv7181_stream(struct vehicle_ad_dev *ad, int value)
{
	char val;

	if (value)
		val = 0x0c;	//on
	else
		val = 0x4c;

	SENSOR_DG("stream write 0x%x to reg 0x03\n", val);
	vehicle_generic_sensor_write(ad, 0x03, &val);
	if (value)
		val = 0x47;	//on
	else
		val = 0x87;

	SENSOR_DG("stream write 0x%x to reg 0x01d\n", val);
	vehicle_generic_sensor_write(ad, 0x1d, &val);

	return 0;
}

static void power_on(struct vehicle_ad_dev *ad)
{
	/* gpio_direction_output(ad->power, ad->pwr_active); */

	if (gpio_is_valid(ad->powerdown)) {
		gpio_request(ad->powerdown, "ad_powerdown");
		gpio_direction_output(ad->powerdown, !ad->pwdn_active);
		/* gpio_set_value(ad->powerdown, !ad->pwdn_active); */
	}

	if (gpio_is_valid(ad->power)) {
		gpio_request(ad->power, "ad_power");
		gpio_direction_output(ad->power, ad->pwr_active);
		/* gpio_set_value(ad->power, ad->pwr_active); */
	}

	if (gpio_is_valid(ad->reset)) {
		gpio_request(ad->reset, "ad_reset");
		gpio_direction_output(ad->reset, 0);
		usleep_range(10000, 12000);
		gpio_set_value(ad->reset, 1);
		usleep_range(10000, 12000);
	}
}

static void power_off(struct vehicle_ad_dev *ad)
{
	if (gpio_is_valid(ad->powerdown))
		gpio_free(ad->powerdown);

	if (gpio_is_valid(ad->power))
		gpio_free(ad->power);

	if (gpio_is_valid(ad->reset))
		gpio_free(ad->reset);
}

static void adv7181_check_state_work(struct work_struct *work)
{
	struct vehicle_ad_dev *ad;
	v4l2_std_id std;

	ad = ad7181_g_addev;

	if (ad->cif_error_last_line > 0)
		ad->cif_error_last_line = 0;

	adv7181_check_std(ad, &std);
	SENSOR_DG("%s:new std(%llx), std_old(%llx)\n", __func__, std, std_old);
	if (std != std_old) {
		std_old = std;
		adv7181_reinit_parameter(ad, std);
		SENSOR_DG("%s:ad signal change notify\n", __func__);
		vehicle_ad_stat_change_notify();
	}

	queue_delayed_work(ad->state_check_work.state_check_wq,
			   &ad->state_check_work.work, msecs_to_jiffies(3000));
}

int adv7181_ad_deinit(void)
{
	struct vehicle_ad_dev *ad;

	ad = ad7181_g_addev;

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
	power_off(ad);

	return 0;
}

int adv7181_ad_init(struct vehicle_ad_dev *ad)
{
	v4l2_std_id std = V4L2_STD_NTSC;

	if (!ad)
		return -1;

	ad7181_g_addev = ad;

	/*  1. i2c init */
	while (ad->adapter == NULL) {
		ad->adapter = i2c_get_adapter(ad->i2c_chl);
		usleep_range(10000, 12000);
	}

	if (!i2c_check_functionality(ad->adapter, I2C_FUNC_I2C))
		return -EIO;

	/*  2. ad power on sequence */
	power_on(ad);

	/* fix mode */
	adv7181_check_std(ad, &std);
	std_old = std;
	SENSOR_DG("std: %s\n", (std == V4L2_STD_NTSC) ? "ntsc" : "pal");
	SENSOR_DG("std_old: %s\n", (std_old == V4L2_STD_NTSC) ? "ntsc" : "pal");

	/*  3 .init default format params */
	adv7181_reg_init(ad, std);
	adv7181_reinit_parameter(ad, std);
	vehicle_ad_stat_change_notify();

	/*  5. create workqueue to detect signal change */
	INIT_DELAYED_WORK(&ad->state_check_work.work, adv7181_check_state_work);
	ad->state_check_work.state_check_wq =
		create_singlethread_workqueue("vehicle-ad-adv7181");

	queue_delayed_work(ad->state_check_work.state_check_wq,
			   &ad->state_check_work.work, msecs_to_jiffies(100));

	return 0;
}


