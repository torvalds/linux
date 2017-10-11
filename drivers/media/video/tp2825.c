/*
 * drivers/media/video/tp2825.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 * Author:zhoupeng<benjo.zhou@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "generic_sensor.h"
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/debugfs.h>

/*
*      Driver Version Note
*v0.0.1: this driver is compatible with generic_sensor
*v0.0.2: support debug_fs for debug
*	 support irq interrupt for switch input source
*	 support support PAL mode
*/
static int version = KERNEL_VERSION(0, 0, 2);
module_param(version, int, S_IRUGO);

static int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);

#define dprintk(level, fmt, arg...) do {	\
	if (debug >= level)					\
	printk(KERN_WARNING fmt, ## arg);	\
} while (0)
#define debug_printk(format, ...) dprintk(1, format, ## __VA_ARGS__)
/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_SENSOR_TP2825
#define SENSOR_V4L2_IDENT V4L2_IDENT_TP2825
#define SENSOR_ID 0x2825
#define SENSOR_BUS_PARAM		(V4L2_MBUS_MASTER | \
					V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_HSYNC_ACTIVE_HIGH | V4L2_MBUS_VSYNC_ACTIVE_HIGH | \
					V4L2_MBUS_DATA_ACTIVE_HIGH | SOCAM_MCLK_24MHZ)

static int SENSOR_PREVIEW_W = 1280;
static int SENSOR_PREVIEW_H = 720;

static struct rk_camera_device_signal_config dev_info[] = {
	{
		.type = RK_CAMERA_DEVICE_BT601_PIONGPONG,
		.dvp = {
			.vsync = RK_CAMERA_DEVICE_SIGNAL_HIGH_LEVEL,
			.hsync = RK_CAMERA_DEVICE_SIGNAL_HIGH_LEVEL
		},
		.crop = {
			.top = 20,
			.left = 8,
			.width = 1280,
			.height = 720
		}
	}
};

static struct rk_camera_device_defrect defrects[4];

#define SENSOR_PREVIEW_FPS		30000		/* 30fps	*/
#define SENSOR_FULLRES_L_FPS		15000		/* 15fps	*/
#define SENSOR_FULLRES_H_FPS		15000		/* 15fps	*/
#define SENSOR_720P_FPS				0
#define SENSOR_1080P_FPS			0

#define SENSOR_REGISTER_LEN			1	/* sensor register address bytes */
#define SENSOR_VALUE_LEN			1	/* sensor register value bytes */
static char input_mode[10] = "720P";
#define SENSOR_CHANNEL_REG		0x41
#define SENSOR_CLAMPING_CONTROL		0x26

static unsigned int SensorConfiguration = (CFG_Effect | CFG_Scene);
static unsigned int SensorChipID[] = {SENSOR_ID};
/* Sensor Driver Configuration End */

#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

#define SensorRegVal(a, b) CONS4(SensorReg, SENSOR_REGISTER_LEN, Val, SENSOR_VALUE_LEN)(a, b)
#define sensor_write(client, reg, v) CONS4(sensor_write_reg, SENSOR_REGISTER_LEN, val, SENSOR_VALUE_LEN)(client, (reg), (v))
#define sensor_read(client, reg, v) CONS4(sensor_read_reg, SENSOR_REGISTER_LEN, val, SENSOR_VALUE_LEN)(client, (reg), (v))
#define sensor_write_array generic_sensor_write_array

struct sensor_parameter {
	unsigned int PreviewDummyPixels;
	unsigned int CaptureDummyPixels;
	unsigned int preview_exposure;

	unsigned short int preview_line_width;
	unsigned short int preview_gain;
	unsigned short int PreviewPclk;
	unsigned short int CapturePclk;
	char awb[6];
};

struct specific_sensor {
	struct generic_sensor common_sensor;
	struct sensor_parameter parameter;
};

/*
*  The follow setting need been filled.
*
*  Must Filled:
*  sensor_init_data :               Sensor initial setting;
*  sensor_fullres_lowfps_data :     Sensor full resolution setting with best auality, recommand for video;
*  sensor_preview_data :            Sensor preview resolution setting, recommand it is vga or svga;
*  sensor_softreset_data :          Sensor software reset register;
*  sensor_check_id_data :           Sensir chip id register;
*
*  Optional filled:
*  sensor_fullres_highfps_data:     Sensor full resolution setting with high framerate, recommand for video;
*  sensor_720p:                     Sensor 720p setting, it is for video;
*  sensor_1080p:                    Sensor 1080p setting, it is for video;
*
*  :::::WARNING:::::
*  The SensorEnd which is the setting end flag must be filled int the last of each setting;
*/

/* Sensor initial setting */
static struct rk_sensor_reg sensor_preview_data[] = {
	SensorEnd
};

/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] = {
	SensorEnd
};

/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] = {
	SensorEnd
};

/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data_ntsc[] = {
	{0x02, 0xCF},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x53},
	{0x0D, 0x10},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0x5e},
	{0x11, 0x40},
	{0x12, 0x44},
	{0x13, 0x00},
	{0x14, 0x00},
	{0x15, 0x13},
	{0x16, 0x4E},
	{0x17, 0xBC},
	{0x18, 0x15},
	{0x19, 0xF0},
	{0x1A, 0x07},
	{0x1B, 0x00},
	{0x1C, 0x09},
	{0x1D, 0x38},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0xA0},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x70},
	{0x2C, 0x0A},/*1a*/
	{0x2D, 0x68},
	{0x2E, 0x5E},
	{0x2F, 0x00},
	{0x30, 0x62},
	{0x31, 0xBB},
	{0x32, 0x96},
	{0x33, 0xC0},
	{0x34, 0x00},
	{0x35, 0x65},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x84},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	//{0x4D, 0x03},
	{0x4E, 0x37},
	{0x4F, 0x01},
	{0xB5, 0x01},
	{0xB8, 0x02},
	{0xBA, 0x10},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_pal[] = {
	{0x02, 0xCE},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x53},
	{0x0D, 0x11},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0x70},
	{0x11, 0x4D},
	{0x12, 0x40},
	{0x13, 0x00},
	{0x14, 0x00},
	{0x15, 0x13},
	{0x16, 0x67},
	{0x17, 0xBC},
	{0x18, 0x16},
	{0x19, 0x20},
	{0x1A, 0x17},
	{0x1B, 0x00},
	{0x1C, 0x09},
	{0x1D, 0x48},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0xB0},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x70},
	{0x2C, 0x1A},
	{0x2D, 0x60},
	{0x2E, 0x5E},
	{0x2F, 0x00},
	{0x30, 0x7A},
	{0x31, 0x4A},
	{0x32, 0x4D},
	{0x33, 0xF0},
	{0x34, 0x00},
	{0x35, 0x65},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x84},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	//{0x4D, 0x03},
	{0x4E, 0x37},
	{0x4F, 0x01},
	{0xB5, 0x01},
	{0xB8, 0x02},
	{0xBA, 0x10},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_720p_50hz[] = {
	{0x02, 0xCA},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x43},
	{0x0D, 0x10},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0xf0},
	{0x11, 0x50},
	{0x12, 0x60},
	{0x13, 0x00},
	{0x14, 0x08},
	{0x15, 0x13},
	{0x16, 0x16},
	{0x17, 0x00},
	{0x18, 0x18},
	{0x19, 0xD0},
	{0x1A, 0x25},
	{0x1B, 0x00},
	{0x1C, 0x07},
	{0x1D, 0xBC},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0x60},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x4A},
	{0x2C, 0x1A},
	{0x2D, 0x30},
	{0x2E, 0x70},
	{0x2F, 0x00},
	{0x30, 0x48},
	{0x31, 0xBB},
	{0x32, 0x2E},
	{0x33, 0x90},
	{0x34, 0x00},
	{0x35, 0x05},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x8C},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	//{0x4D, 0x03},
	{0x4E, 0x03},
	{0x4F, 0x01},
	{0xB5, 0x01},
	{0xB8, 0x02},
	{0xBA, 0x10},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_720p_30hz[] = {
	{0x02, 0xDA},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x53},
	{0x0D, 0x10},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0xf0},
	{0x11, 0x50},
	{0x12, 0x60},
	{0x13, 0x00},
	{0x14, 0x08},
	{0x15, 0x13},
	{0x16, 0x16},
	{0x17, 0x00},
	{0x18, 0x19},
	{0x19, 0xD0},
	{0x1A, 0x25},
	{0x1B, 0x00},
	{0x1C, 0x06},
	{0x1D, 0x72},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0x60},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x4A},
	{0x2C, 0x1A},
	{0x2D, 0x30},
	{0x2E, 0x70},
	{0x2F, 0x00},
	{0x30, 0x48},
	{0x31, 0xBB},
	{0x32, 0x2E},
	{0x33, 0x90},
	{0x34, 0x00},
	{0x35, 0x25},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x88},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x03},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	//{0x4D, 0x03},
	{0x4E, 0x17},
	{0x4F, 0x01},
	{0x85, 0x00},
	{0x88, 0x00},
	{0x8A, 0x00},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static struct rk_sensor_reg sensor_preview_data_720p_25hz[] = {
	{0x02, 0xCA},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x53},
	{0x0D, 0x10},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0xf0},
	{0x11, 0x50},
	{0x12, 0x60},
	{0x13, 0x00},
	{0x14, 0x08},
	{0x15, 0x13},
	{0x16, 0x16},
	{0x17, 0x00},
	{0x18, 0x19},
	{0x19, 0xD0},
	{0x1A, 0x25},
	{0x1B, 0x00},
	{0x1C, 0x07},
	{0x1D, 0xBC},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0x60},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x70},
	{0x2C, 0x1A},
	{0x2D, 0x30},
	{0x2E, 0x70},
	{0x2F, 0x00},
	{0x30, 0x48},
	{0x31, 0xBB},
	{0x32, 0x2E},
	{0x33, 0x90},
	{0x34, 0x00},
	{0x35, 0x25},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x88},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	//{0x4D, 0x03},
	{0x4E, 0x17},
	{0x4F, 0x01},
	{0xB5, 0x01},
	{0xB8, 0x02},
	{0xBA, 0x10},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

static struct rk_sensor_reg sensor_init_data[] = {
/*default 720p 50hz*/
	{0x02, 0xCA},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x43},
	{0x0D, 0x10},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0xf0},
	{0x11, 0x50},
	{0x12, 0x60},
	{0x13, 0x00},
	{0x14, 0x08},
	{0x15, 0x13},
	{0x16, 0x16},
	{0x17, 0x00},
	{0x18, 0x18},
	{0x19, 0xD0},
	{0x1A, 0x25},
	{0x1B, 0x00},
	{0x1C, 0x07},
	{0x1D, 0xBC},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0x60},
	{0x21, 0x86},
	{0x22, 0x38},
	{0x23, 0x3C},
	{0x24, 0x56},
	{0x25, 0xFF},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x4A},
	{0x2C, 0x0A},
	{0x2D, 0x30},
	{0x2E, 0x70},
	{0x2F, 0x00},
	{0x30, 0x48},
	{0x31, 0xBB},
	{0x32, 0x2E},
	{0x33, 0x90},
	{0x34, 0x00},
	{0x35, 0x05},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x40},
	{0x39, 0x8C},
	{0x3A, 0x00},
	{0x3B, 0x03},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x42, 0x00},
	{0x43, 0x12},
	{0x44, 0x07},
	{0x45, 0x49},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x03},
	//{0x4D, 0x03},
	{0x4E, 0x03},
	{0x4F, 0x01},
	{0xB5, 0x01},
	{0xB8, 0x02},
	{0xBA, 0x10},
	{0xF3, 0x00},
	{0xF4, 0x00},
	{0xF5, 0x00},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x00},
	{0xFB, 0x00},
	{0xFC, 0xC0},
	{0xFD, 0x00},
	SensorEnd
};

/* 1280x720 */
static struct rk_sensor_reg sensor_720p[] = {
	SensorEnd
};

/* 1920x1080 */
static struct rk_sensor_reg sensor_1080p[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_softreset_data[] = {
	SensorRegVal(0x06, 0x32 | 0x80),
	SensorEnd
};

static struct rk_sensor_reg sensor_check_id_data[] = {
	SensorRegVal(0xfe, 0x0),
	SensorRegVal(0xff, 0x0),
	SensorEnd
};

/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[] = {
	SensorEnd
};

/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[] = {
	SensorEnd
};

/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[] = {
	SensorEnd
};

/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[] = {
	SensorEnd
};

/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {
	sensor_WhiteB_Auto,
	sensor_WhiteB_TungstenLamp1,
	sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay,
	sensor_WhiteB_Cloudy,
	NULL,
};

static struct rk_sensor_reg sensor_Brightness0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness2[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness3[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness4[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Brightness5[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {
	sensor_Brightness0,
	sensor_Brightness1,
	sensor_Brightness2,
	sensor_Brightness3,
	sensor_Brightness4,
	sensor_Brightness5,
	NULL,
};

static struct rk_sensor_reg sensor_Effect_Normal[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_WandB[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_Sepia[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_Negative[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_Bluish[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Effect_Green[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_EffectSeqe[] = {
	sensor_Effect_Normal,
	sensor_Effect_WandB,
	sensor_Effect_Negative,
	sensor_Effect_Sepia,
	sensor_Effect_Bluish,
	sensor_Effect_Green,
	NULL,
};

static struct rk_sensor_reg sensor_Exposure0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure2[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure3[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure4[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure5[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Exposure6[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_ExposureSeqe[] = {
	sensor_Exposure0,
	sensor_Exposure1,
	sensor_Exposure2,
	sensor_Exposure3,
	sensor_Exposure4,
	sensor_Exposure5,
	sensor_Exposure6,
	NULL,
};

static struct rk_sensor_reg sensor_Saturation0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Saturation1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Saturation2[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_SaturationSeqe[] = {
	sensor_Saturation0,
	sensor_Saturation1,
	sensor_Saturation2,
	NULL,
};

static struct rk_sensor_reg sensor_Contrast0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast2[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast3[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast4[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast5[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Contrast6[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_ContrastSeqe[] = {
	sensor_Contrast0,
	sensor_Contrast1,
	sensor_Contrast2,
	sensor_Contrast3,
	sensor_Contrast4,
	sensor_Contrast5,
	sensor_Contrast6,
	NULL,
};

static struct rk_sensor_reg sensor_SceneAuto[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_SceneNight[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_SceneSeqe[] = {
	sensor_SceneAuto,
	sensor_SceneNight,
	NULL,
};

static struct rk_sensor_reg sensor_Zoom0[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom1[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom2[] = {
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom3[] = {
	SensorEnd
};

static struct rk_sensor_reg *sensor_ZoomSeqe[] = {
	sensor_Zoom0,
	sensor_Zoom1,
	sensor_Zoom2,
	sensor_Zoom3,
	NULL,
};

/*
* User could be add v4l2_querymenu in sensor_controls by new_usr_v4l2menu
*/
static struct v4l2_querymenu sensor_menus[] = {
};

/*
* User could be add v4l2_queryctrl in sensor_controls by new_user_v4l2ctrl
*/

static inline int sensor_v4l2ctrl_inside_cb(struct soc_camera_device *icd,
					    struct sensor_v4l2ctrl_info_s *ctrl_info,
					    struct v4l2_ext_control *ext_ctrl,
					    bool is_set)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct generic_sensor *sensor = to_generic_sensor(client);
	int ret = 0;

	switch (ctrl_info->qctrl->id) {
	case V4L2_CID_DEINTERLACE:
		{
			if (is_set) {
				SENSOR_TR("%s(%d): deinterlace is not support set!", __func__, __LINE__);
				ret = -EINVAL;
			} else {
				if ((RK_CAMERA_DEVICE_BT601_8 ==
				     sensor->info_priv.dev_sig_cnf.type) ||
				    (RK_CAMERA_DEVICE_BT601_PIONGPONG ==
				     sensor->info_priv.dev_sig_cnf.type)) {
					/* don't need deinterlace process */
					ext_ctrl->value = 0;
					ctrl_info->cur_value = 0;
				} else {
					ext_ctrl->value = 1;
					ctrl_info->cur_value = 1;
				}
			}
			break;
		}
	case V4L2_CID_CHANNEL:
	{
		if (is_set) {
			if ((ext_ctrl->value < ctrl_info->qctrl->minimum) ||
			    (ext_ctrl->value > ctrl_info->qctrl->maximum)) {
				SENSOR_TR("%s(%d):channel(%d) is not support\n",
					  __func__, __LINE__, ext_ctrl->value);
				ret = -EINVAL;
				goto cb_end;
			}
			if (sensor->channel_id != ext_ctrl->value) {
				SENSOR_TR("%s(%d):set channel(%d)!\n",
					  __func__, __LINE__, ext_ctrl->value);
				sensor->channel_id = ext_ctrl->value;
				sensor_write(client, SENSOR_CHANNEL_REG,
					     sensor->channel_id);
				sensor_write(client, SENSOR_CLAMPING_CONTROL,
					     0x01);
			}
		} else {
			ext_ctrl->value = sensor->channel_id;
			ctrl_info->cur_value = sensor->channel_id;
		}
		break;
	}
	case V4L2_CID_VIDEO_STATE:
	{
		if (is_set) {
			SENSOR_TR("%s(%d): set isn't support!\n",
				  __func__, __LINE__);
		} else {
			ext_ctrl->value = sensor->info_priv.video_state;
			ctrl_info->cur_value = sensor->info_priv.video_state;
		}
		break;
	}
	default:
		{
			SENSOR_TR("%s(%d): cmd(0x%x) is unknown !",
				  __func__, __LINE__, ctrl_info->qctrl->id);
			ret = -EINVAL;
		}
	}

cb_end:
	return ret;
}

static struct sensor_v4l2ctrl_usr_s sensor_controls[] = {
	{
		{
			V4L2_CID_DEINTERLACE,
			V4L2_CTRL_TYPE_BOOLEAN,
			"deinterlace",
			0,
			1,
			1,
			0
		},
		sensor_v4l2ctrl_inside_cb,
		NULL
	},
	{
		{
			V4L2_CID_CHANNEL,
			V4L2_CTRL_TYPE_INTEGER,
			"channel",
			0,
			4,
			1,
			0
		},
		sensor_v4l2ctrl_inside_cb,
		NULL
	},
	{
		{
			V4L2_CID_VIDEO_STATE,
			V4L2_CTRL_TYPE_BOOLEAN,
			"video_state",
			0,
			1,
			1,
			0
		},
		sensor_v4l2ctrl_inside_cb,
		NULL
	}
};

/*
* MUST define the current used format as the first item
*/
static struct rk_sensor_datafmt sensor_colour_fmts[] = {
	{MEDIA_BUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
};

/*
**********************************************************
* Following is local code:
*
* Please codeing your program here
**********************************************************
*/
static int sensor_parameter_record(struct i2c_client *client)
{
	return 0;
}

static int sensor_ae_transfer(struct i2c_client *client)
{
	return 0;
}

/*
**********************************************************
* Following is callback
* If necessary, you could coding these callback
**********************************************************
*/
enum {
	CVSTD_720P60 = 0,
	CVSTD_720P50,
	CVSTD_1080P30,
	CVSTD_1080P25,
	CVSTD_720P30,
	CVSTD_720P25,
	CVSTD_SD,
	CVSTD_NO_DET,
	CVSTD_720P30V2,
	CVSTD_720P25V2,
	CVSTD_NTSC,
	CVSTD_PAL
};

enum {
	VIDEO_UNPLUG,
	VIDEO_IN,
	VIDEO_LOCKED,
	VIDEO_UNLOCK
};

#define FLAG_LOSS	0x80
#define FLAG_LOCKED	0x60
static int cvstd_mode = CVSTD_720P50;
static int cvstd_old = CVSTD_720P50;
static int cvstd_sd = CVSTD_PAL;

static void tp2825_reinit_parameter(unsigned char cvstd, struct generic_sensor *sensor)
{
	struct rk_sensor_sequence *series = sensor->info_priv.sensor_series;
	int num_series = sensor->info_priv.num_series;
	int i;

	if (CVSTD_PAL == cvstd) {
		SENSOR_PREVIEW_W = 960;
		SENSOR_PREVIEW_H = 576;
		sensor->info_priv.dev_sig_cnf.type = RK_CAMERA_DEVICE_CVBS_PAL;
		strcpy(input_mode, "PAL");
	} else if (CVSTD_NTSC == cvstd) {
		SENSOR_PREVIEW_W = 960;
		SENSOR_PREVIEW_H = 480;
		sensor->info_priv.dev_sig_cnf.type = RK_CAMERA_DEVICE_CVBS_NTSC;
		strcpy(input_mode, "NTSC");
	} else {
		SENSOR_PREVIEW_W = 1280;
		SENSOR_PREVIEW_H = 720;
		sensor->info_priv.dev_sig_cnf.type = RK_CAMERA_DEVICE_BT601_8;
		strcpy(input_mode, "720P");
	}
	for (i = 0; i < 4; i++) {
		if ((defrects[i].width == SENSOR_PREVIEW_W) &&
		    (defrects[i].height == SENSOR_PREVIEW_H)) {
			SENSOR_PREVIEW_W = defrects[i].defrect.width;
			SENSOR_PREVIEW_H = defrects[i].defrect.height;
			memcpy(&sensor->info_priv.dev_sig_cnf.crop,
			       &defrects[i].defrect,
			       sizeof(defrects[i].defrect));
			if (!defrects[i].interface) {
				SENSOR_TR("%s(%d): interface is NULL\n",
					  __func__, __LINE__);
				continue;
			}
			if (!strcmp(defrects[i].interface, "bt601_8"))
				sensor->info_priv.dev_sig_cnf.type =
					RK_CAMERA_DEVICE_BT601_8;
			if (!strcmp(defrects[i].interface, "cvbs_ntsc"))
				sensor->info_priv.dev_sig_cnf.type =
					RK_CAMERA_DEVICE_CVBS_NTSC;
			if (!strcmp(defrects[i].interface, "cvbs_pal"))
				sensor->info_priv.dev_sig_cnf.type =
					RK_CAMERA_DEVICE_CVBS_PAL;
			if (!strcmp(defrects[i].interface, "bt601_8_pp"))
				sensor->info_priv.dev_sig_cnf.type =
					RK_CAMERA_DEVICE_BT601_PIONGPONG;
			if (!strcmp(defrects[i].interface, "cvbs_deinterlace"))
				sensor->info_priv.dev_sig_cnf.type =
					RK_CAMERA_DEVICE_CVBS_DEINTERLACE;
			SENSOR_TR("%s(%d): type 0x%x\n", __func__, __LINE__,
				  sensor->info_priv.dev_sig_cnf.type);
		}
	}

	/*update sensor info_priv*/
	for (i = 0; i < num_series; i++) {
		series[i].gSeq_info.w = SENSOR_PREVIEW_W;
		series[i].gSeq_info.h = SENSOR_PREVIEW_H;
	}
	generic_sensor_get_max_min_res(sensor->info_priv.sensor_series,
				       sensor->info_priv.num_series,
				       &(sensor->info_priv.max_real_res),
				       &(sensor->info_priv.max_res),
				       &(sensor->info_priv.min_res));
}

static int tp2825_uevent_video_state(struct generic_sensor *sensor, int state)
{
	char *event_msg = NULL;
	char *envp[2];

	return 0;
	event_msg = kasprintf(GFP_KERNEL, "CVBS_NAME=TP2825, VIDEO_STATUS=%d",
			      state);
	SENSOR_TR("%s(%d): event_msg: %s\n", __func__, __LINE__, event_msg);
	envp[0] = event_msg;
	envp[1] = NULL;
	kobject_uevent_env(&(sensor->subdev.v4l2_dev->dev->kobj), KOBJ_CHANGE,
			   envp);

	return 0;
}

static int tp2825_check_cvstd(struct i2c_client *client, bool activate_check)
{
	unsigned char cvstd;
	int i;
	int ret = -EINVAL;
	unsigned char status;
	static int state = VIDEO_UNPLUG;
	static bool first_reinit = 1;
	struct generic_sensor *sensor = to_generic_sensor(client);
	struct rk_sensor_sequence *sensor_series =
		sensor->info_priv.sensor_series;
	int series_num = sensor->info_priv.num_series;

	for (i = 0; i < series_num; i++)
		if ((sensor_series[i].property == SEQUENCE_INIT) &&
		    (sensor_series[i].data[0].reg != SEQCMD_END))
			break;

	ret = sensor_read(client, 0x01, &status);
	if (IS_ERR_VALUE(ret)) {
		SENSOR_TR("sensor read failed\n");
		return -EBUSY;
	}
	SENSOR_DG("%s(%d): state %d, read 0x01:0x%x\n", __func__, __LINE__, state, status);

	if (status & FLAG_LOSS) {
		state = VIDEO_UNPLUG;
		tp2825_uevent_video_state(sensor, 0);
		sensor_write(client, SENSOR_CLAMPING_CONTROL, 0x01);
		sensor->info_priv.video_state = RK_CAM_INPUT_VIDEO_STATE_LOSS;
	} else if (FLAG_LOCKED == (status & FLAG_LOCKED)) {
		/* video locked */
		if ((state != VIDEO_LOCKED) && !activate_check) {
			state = VIDEO_LOCKED;
			tp2825_uevent_video_state(sensor, 1);

			sensor_write(client, SENSOR_CLAMPING_CONTROL, 0x02);
		}
		sensor->info_priv.video_state = RK_CAM_INPUT_VIDEO_STATE_LOCKED;
	} else {
		/* video in but unlocked */
		state = VIDEO_IN;

		sensor_write(client, SENSOR_CLAMPING_CONTROL, 0x02);
		//sensor->info_priv.video_state = RK_CAM_INPUT_VIDEO_STATE_LOSS;
	}
	SENSOR_DG("%s(%d): state %s\n", __func__, __LINE__,
		  (VIDEO_UNPLUG == state) ? "UNPLUG" : (VIDEO_LOCKED == state) ? "LOCKED" : "VIDEO_IN");

	if (state == VIDEO_IN) {
		sensor_read(client, 0x03, &cvstd);
		SENSOR_TR("%s(%d): cvstd_old %d, read 0x03 return 0x%x",
			  __func__, __LINE__, cvstd_old, cvstd);

		cvstd &= 0x07;
		if (cvstd == cvstd_old)
			goto check_end;

		if (cvstd == CVSTD_720P30) {
			cvstd_mode = CVSTD_720P30;
			SENSOR_TR("%s(%d): 720P30\n", __func__, __LINE__);
			sensor_series[i].data = sensor_preview_data_720p_30hz;
		} else if (cvstd == CVSTD_720P25) {
			cvstd_mode = CVSTD_720P25;
			SENSOR_TR("%s(%d): 720P25\n", __func__, __LINE__);
			sensor_series[i].data = sensor_preview_data_720p_25hz;
		} else if (cvstd == CVSTD_720P60) {
			SENSOR_TR("%s(%d): 720P60", __func__, __LINE__);
		} else if (cvstd == CVSTD_720P50) {
			cvstd_mode = CVSTD_720P50;
			SENSOR_TR("%s(%d): 720P50\n", __func__, __LINE__);
			sensor_series[i].data = sensor_preview_data_720p_50hz;
		} else if (cvstd == CVSTD_1080P30) {
			SENSOR_TR("%s(%d): 1080P30", __func__, __LINE__);
		} else if (cvstd == CVSTD_1080P25) {
			SENSOR_TR("%s(%d): 1080P25", __func__, __LINE__);
		} else if (cvstd == CVSTD_SD) {
			msleep(80);
			ret = sensor_read(client, 0x01, &status);
			SENSOR_DG("%s(%d): read 0x01 return 0x%x\n",
				  __func__, __LINE__, status);

			/*
			 * 1: pal  0: ntsc
			 */
			if ((status >> 2) & 0x01)
				cvstd_sd = CVSTD_PAL;
			else
				cvstd_sd = CVSTD_NTSC;

			SENSOR_TR("%s(%d): cvstd_sd is %s\n",
				  __func__, __LINE__,
				  (cvstd_sd == CVSTD_PAL) ? "PAL" : "NTSC");
			cvstd_mode = cvstd_sd;

			if (cvstd_mode == CVSTD_PAL) {
				sensor_series[i].data = sensor_preview_data_pal;
			} else {
				sensor_series[i].data = sensor_preview_data_ntsc;
			}
		}
	} else if (state == VIDEO_LOCKED) {
		goto check_end;
	} else {
		SENSOR_TR("tp2825_check_cvstd: check sensor statue failed!\n");

		goto check_failed;
	}

	/* config irq interrupt */
	/*for (i = 0; i < ARRAY_SIZE(sensor_preview_data); i++) {
		if (sensor_preview_data[i].reg == 0x4F)
			sensor_preview_data[i].val = 0x01;
		if (sensor_preview_data[i].reg == 0xB8)
			sensor_preview_data[i].val = 0x03;
		if (sensor_preview_data[i].reg == 0xBA)
			sensor_preview_data[i].val = 0x10;
	}*/

	if (cvstd_mode != cvstd_old) {
		tp2825_reinit_parameter(cvstd_mode, sensor);
		generic_sensor_write_array(client, sensor_series[i].data);
		sensor_write(client, SENSOR_CHANNEL_REG, sensor->channel_id);
	}
check_end:

	if (first_reinit) {
		first_reinit = false;
		tp2825_reinit_parameter(cvstd_mode, sensor);
	}
	return 0;

check_failed:
	return -1;
}

/*
* the function is called in open sensor
*/
static int sensor_activate_cb(struct i2c_client *client)
{
	struct generic_sensor *sensor = to_generic_sensor(client);

	SENSOR_DG("Here I am: %s %d/n", __func__, __LINE__);
	sensor_write(client, SENSOR_CHANNEL_REG, sensor->channel_id);
	//msleep(200);
	tp2825_check_cvstd(client, true);
	cvstd_old = cvstd_mode;
	sensor->info_priv.video_state = RK_CAM_INPUT_VIDEO_STATE_LOCKED;

	if (sensor->state_check_work.state_check_wq) {
		SENSOR_DG("sensor_activate_cb: queue_delayed_work 1000ms");
		queue_delayed_work(sensor->state_check_work.state_check_wq,
			&sensor->state_check_work.work, 100);
	}

	return 0;
}

/*
* the function is called in close sensor
*/
static int sensor_deactivate_cb(struct i2c_client *client)
{
	int ret = 0;
	struct generic_sensor *sensor = to_generic_sensor(client);

	ret = cancel_delayed_work_sync(&sensor->state_check_work.work);
	return ret;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	SENSOR_DG("stream %d\n", enable);
	if (enable)
		sensor_write(client, 0x4D, 0x03);
	else
		sensor_write(client, 0x4D, 0x00);
	return 0;
}

/*
* the function is called before sensor register setting in VIDIOC_S_FMT
*/
static int sensor_s_fmt_cb_th(struct i2c_client *client, struct v4l2_mbus_framefmt *mf, bool capture)
{
	if (capture)
		sensor_parameter_record(client);

	return 0;
}

/*
* the function is called after sensor register setting finished in VIDIOC_S_FMT
*/
static int sensor_s_fmt_cb_bh(struct i2c_client *client, struct v4l2_mbus_framefmt *mf, bool capture)
{
	if (capture)
		sensor_ae_transfer(client);

	return 0;
}

static int sensor_try_fmt_cb_th(struct i2c_client *client, struct v4l2_mbus_framefmt *mf)
{
	return 0;
}

static int sensor_softrest_usr_cb(struct i2c_client *client, struct rk_sensor_reg *series)
{
	return 0;
}
static int sensor_check_id_usr_cb(struct i2c_client *client, struct rk_sensor_reg *series)
{
	return 0;
}

static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
	return 0;
}

static int sensor_resume(struct soc_camera_device *icd)
{
	SENSOR_DG("Resume");

	return 0;
}

static int sensor_mirror_cb(struct i2c_client *client, int mirror)
{
	return 0;
}

/*
* the function is v4l2 control V4L2_CID_HFLIP callback
*/
static int sensor_v4l2ctrl_mirror_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info,
				     struct v4l2_ext_control *ext_ctrl)
{
	SENSOR_DG("sensor_mirror success, value:0x%x", ext_ctrl->value);
	return 0;
}

static int sensor_flip_cb(struct i2c_client *client, int flip)
{
	return 0;
}

/*
* the function is v4l2 control V4L2_CID_VFLIP callback
*/
static int sensor_v4l2ctrl_flip_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info,
				   struct v4l2_ext_control *ext_ctrl)
{
	SENSOR_DG("sensor_flip success, value:0x%x", ext_ctrl->value);
	return 0;
}

/*
* the functions are focus callbacks
*/
static int sensor_focus_init_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_single_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_near_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_far_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_specialpos_usr_cb(struct i2c_client *client, int pos)
{
	return 0;
}

static int sensor_focus_af_const_usr_cb(struct i2c_client *client)
{
	return 0;
}
static int sensor_focus_af_const_pause_usr_cb(struct i2c_client *client)
{
	return 0;
}
static int sensor_focus_af_close_usr_cb(struct i2c_client *client)
{
	return 0;
}

static int sensor_focus_af_zoneupdate_usr_cb(struct i2c_client *client, int *zone_tm_pos)
{
	return 0;
}

/*
face defect call back
*/
static int sensor_face_detect_usr_cb(struct i2c_client *client, int on)
{
	return 0;
}

static void tp2825_send_uevent(struct generic_sensor *sensor)
{
	char *event_msg = NULL;
	char *envp[2];

	event_msg = kasprintf(GFP_KERNEL, "CVBS_NAME=TP2825, NOW_INPUT_MODE=%s, RESOLUTION=%dx%d",
			      input_mode, SENSOR_PREVIEW_W, SENSOR_PREVIEW_H);
	SENSOR_TR("%s(%d): event_msg: %s\n", __func__, __LINE__, event_msg);
	envp[0] = event_msg;
	envp[1] = NULL;
	kobject_uevent_env(&(sensor->subdev.v4l2_dev->dev->kobj), KOBJ_CHANGE, envp);
}

/* tp2825 irq interrupt process */
static irqreturn_t tp2825_irq(int irq, void *dev_id)
{
	struct specific_sensor *spsensor = (struct specific_sensor *)dev_id;
	struct generic_sensor *sensor = &spsensor->common_sensor;

/*	struct i2c_client *client = spsensor->common_sensor.client;
	int ret;
	unsigned char val;

	ret = sensor_read(client, 0xB5, &val);
	if (IS_ERR_VALUE(ret)) {
		SENSOR_DG("%s(%d): sensor_read failed", __func__, __LINE__);
		return IRQ_HANDLED;
	}
	SENSOR_DG("%s(%d): read 0xB5 val 0x%x\n", __func__, __LINE__, val);

	ret = sensor_read(client, 0x01, &val);
	spsensor->common_sensor.info_priv.video_state =
		((val & 0x80) >> 7) ?
		RK_CAM_INPUT_VIDEO_STATE_LOSS :
		RK_CAM_INPUT_VIDEO_STATE_LOCKED;

	SENSOR_TR("%s(%d): video status is %s\n", __func__, __LINE__,
		  spsensor->common_sensor.info_priv.video_state ?
		  "video present" :
		  "Video loss");
*/
	/*
	 * irq interrupt active while video lossed.
	 */
	SENSOR_TR("%s(%d): video status is video loss\n",
		  __func__, __LINE__);
	sensor->info_priv.video_state = RK_CAM_INPUT_VIDEO_STATE_LOSS;
	tp2825_uevent_video_state(sensor, 0);
/*
	ret = sensor_write(client, 0xB5, 0x01);
	if (IS_ERR_VALUE(ret)) {
		SENSOR_TR("%s(%d): sensor_write failed\n", __func__, __LINE__);
		return IRQ_HANDLED;
	}
	SENSOR_DG("%s(%d): write 0xB5 val 0x01\n", __func__, __LINE__);
*/
	return IRQ_HANDLED;
}

/* config debug fs ops */
#define DEBUG_FS_NTSC_WIDTH 0x8000
#define DEBUG_FS_NTSC_HEIGHT 0x8001
#define DEBUG_FS_NTSC_LEFT 0x8002
#define DEBUG_FS_NTSC_TOP 0x8003

#define DEBUG_FS_PAL_WIDTH 0x8004
#define DEBUG_FS_PAL_HEIGHT 0x8005
#define DEBUG_FS_PAL_LEFT 0x8006
#define DEBUG_FS_PAL_TOP 0x8007

static ssize_t tp2825_debugfs_reg_write(struct file *file,
					const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct specific_sensor *spsensor =
		((struct seq_file *)file->private_data)->private;
	struct i2c_client *client = spsensor->common_sensor.client;
	int reg, val, ret;
	unsigned char read;
	char kbuf[30];
	int nbytes = min(count, sizeof(kbuf) - 1);
	int i = 0;

	if (copy_from_user(kbuf, buf, nbytes))
		return -EFAULT;

	kbuf[nbytes] = '\0';
	if (sscanf(kbuf, " %x %x", &reg, &val) != 2)
		return -EINVAL;

	SENSOR_TR("%s(%d): register write reg: 0x%x, val 0x%x\n",
		  __func__, __LINE__, reg, val);

	switch (reg) {
	case DEBUG_FS_NTSC_WIDTH:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 480)
					defrects[i].defrect.width = val;
			break;
		}
	case DEBUG_FS_NTSC_HEIGHT:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 480)
					defrects[i].defrect.height = val;
			break;
		}
	case DEBUG_FS_NTSC_TOP:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 480)
					defrects[i].defrect.top = val;
			break;
		}
	case DEBUG_FS_NTSC_LEFT:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 480)
					defrects[i].defrect.left = val;
			break;
		}
	case DEBUG_FS_PAL_WIDTH:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 576)
					defrects[i].defrect.width = val;
			break;
		}
	case DEBUG_FS_PAL_HEIGHT:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 576)
					defrects[i].defrect.height = val;
			break;
		}
	case DEBUG_FS_PAL_LEFT:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 576)
					defrects[i].defrect.left = val;
			break;
		}
	case DEBUG_FS_PAL_TOP:
		{
			for (i = 0; i < 4; i++)
				if (defrects[i].height == 576)
					defrects[i].defrect.top = val;
			break;
		}
	default:
		{
			ret = sensor_write(client, reg, val);
			if (IS_ERR_VALUE(ret)) {
				SENSOR_TR("d_fs: write fail: 0x%x, val 0x%x\n",
					  reg, val);
			}

			ret = sensor_read(client, reg, &read);
			if (IS_ERR_VALUE(ret)) {
				SENSOR_TR("d_fs: write fail: 0x%x, val 0x%x\n",
					  reg, read);
			} else
				SENSOR_TR("d_fs: read 0x%x return 0x%x\n",
					  reg, val);
			break;
		}
	}

	return count;
}

static int tp2825_debugfs_reg_show(struct seq_file *s, void *v)
{
	int i, ret;
	unsigned char val;
	struct specific_sensor *spsensor = s->private;
	struct i2c_client *client = spsensor->common_sensor.client;

	SENSOR_TR("%s(%d): test\n", __func__, __LINE__);

	for (i = 0; i < 0xff; i++) {
		ret = sensor_read(client, i + 1, &val);
		if (IS_ERR_VALUE(ret))
			SENSOR_TR("%s(%d): register read failed: 0x%x\n",
				  __func__, __LINE__, i + 1);

		seq_printf(s, "0x%02x : 0x%02x\n", i + 1, (u8)val);
	}

	return 0;
}

static int tp2825_debugfs_open(struct inode *inode, struct file *file)
{
	struct specific_sensor *spsensor = inode->i_private;

	return single_open(file, tp2825_debugfs_reg_show, spsensor);
}

static const struct file_operations tp2825_debugfs_fops = {
	.owner			= THIS_MODULE,
	.open			= tp2825_debugfs_open,
	.read			= seq_read,
	.write			= tp2825_debugfs_reg_write,
	.llseek			= seq_lseek,
	.release		= single_release
};

static void tp2825_check_state_work(struct work_struct *work)
{
	struct rk_state_check_work *state_check_work =
		container_of(work, struct rk_state_check_work, work.work);
	struct generic_sensor *sensor =
		container_of(state_check_work, struct generic_sensor, state_check_work);
	struct i2c_client *client = sensor->client;

	tp2825_check_cvstd(client, false);

	if (cvstd_old != cvstd_mode) {
		cvstd_old = cvstd_mode;
		tp2825_send_uevent(sensor);
	}

	queue_delayed_work(sensor->state_check_work.state_check_wq,
			   &sensor->state_check_work.work, 100);
}

/*
*   The function can been run in sensor_init_parametres which run in sensor_probe, so user can do some
* initialization in the function.
*/
static void sensor_init_parameters_user(struct specific_sensor *spsensor, struct soc_camera_device *icd)
{
	struct soc_camera_desc *desc = to_soc_camera_desc(icd);
	struct rk29camera_platform_data *pdata = desc->subdev_desc.drv_priv;
	struct rkcamera_platform_data *sensor_device = NULL, *new_camera;
	struct dentry *debugfs_dir = spsensor->common_sensor.info_priv.debugfs_dir;
	int ret;

	new_camera = pdata->register_dev_new;
	while (new_camera != NULL) {
		SENSOR_TR("%s(%d): icd_name %s, new_camera_name %s.\n",
			  __func__, __LINE__, dev_name(icd->pdev), new_camera->dev_name);
		if (strcmp(dev_name(icd->pdev), new_camera->dev_name) == 0) {
			sensor_device = new_camera;
			break;
		}
		new_camera = new_camera->next_camera;
	}
	if (!sensor_device) {
		SENSOR_TR("%s(%d): Could not find %s\n", __func__, __LINE__,
			  dev_name(icd->pdev));
		return;
	}
	memcpy(&defrects, &sensor_device->defrects,
	       sizeof(sensor_device->defrects));
	SENSOR_TR("%s(%d): channel %d, default %d\n", __func__, __LINE__,
		  sensor_device->channel_info.channel_total,
		  sensor_device->channel_info.default_id);
	spsensor->common_sensor.channel_id =
		sensor_device->channel_info.default_id;

	if (new_camera->io.gpio_irq) {
		spsensor->common_sensor.irq = gpiod_to_irq(new_camera->io.gpio_irq);
		ret = request_irq(spsensor->common_sensor.irq, tp2825_irq, IRQF_TRIGGER_FALLING,
				  dev_name(icd->pdev), spsensor);
		if (ret < 0)
			SENSOR_TR("%s(%d): request irq failed\n", __func__, __LINE__);
	}

	/* init debugfs */
	debugfs_dir = debugfs_create_dir("tp2825", NULL);
	if (IS_ERR(debugfs_dir))
		SENSOR_TR("%s(%d): create debugfs dir failed\n", __func__, __LINE__);
	else
		debugfs_create_file("register", S_IRUSR, debugfs_dir, spsensor, &tp2825_debugfs_fops);

	/* init work_queue for state_check */
	INIT_DELAYED_WORK(&spsensor->common_sensor.state_check_work.work, tp2825_check_state_work);
	spsensor->common_sensor.state_check_work.state_check_wq =
		create_singlethread_workqueue(SENSOR_NAME_STRING(_state_check_workqueue));
	if (spsensor->common_sensor.state_check_work.state_check_wq == NULL) {
		SENSOR_TR("%s(%d): %s create failed.\n", __func__, __LINE__,
			  SENSOR_NAME_STRING(_state_check_workqueue));
		BUG();
	}

	memcpy(&spsensor->common_sensor.info_priv.dev_sig_cnf, &dev_info[0], sizeof(dev_info));
	spsensor->common_sensor.crop_percent = 0;
	spsensor->common_sensor.sensor_cb.sensor_s_stream_cb = sensor_s_stream;
}

/*
* :::::WARNING:::::
* It is not allowed to modify the following code
*/

sensor_init_parameters_default_code();

sensor_v4l2_struct_initialization();

sensor_probe_default_code();

sensor_remove_default_code();

sensor_driver_default_module_code();

