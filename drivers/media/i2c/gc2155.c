// SPDX-License-Identifier: GPL-2.0
/*
 * gc2155 driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add exposure control and fix v4l2_ctrl init issues.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/pinctrl/consumer.h>
#include <linux/version.h>
#include <media/v4l2-async.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <linux/rk-camera-module.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)
#define GC2155_PIXEL_RATE		(120 * 1000 * 1000)
#define GC2155_EXPOSURE_CONTROL

#define REG_CHIP_ID_H			0xf0
#define REG_CHIP_ID_L			0xf1
#define CHIP_ID_H			0x21
#define CHIP_ID_L			0x55

#define REG_NULL			0xFF

#define GC2155_XVCLK_FREQ		24000000

#define GC2155_NAME			"gc2155"

static const char * const gc2155_supply_names[] = {
	"avdd",
	"dovdd",
	"dvdd",
};

#define GC2155_NUM_SUPPLIES ARRAY_SIZE(gc2155_supply_names)

struct regval {
	u8 addr;
	u8 val;
};

struct gc2155_mode {
	u32 width;
	u32 height;
	unsigned int fps;
	struct v4l2_fract max_fps;
	const struct regval *reg_list;
};

struct gc2155 {
	struct i2c_client *client;
	struct clk *xvclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct gpio_desc *power_gpio;
	struct regulator_bulk_data supplies[GC2155_NUM_SUPPLIES];

	bool streaming;
	bool power_on;
	struct mutex mutex; /* lock to serialize v4l2 callback */
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	unsigned int fps;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *pixel_rate;

	const struct gc2155_mode *cur_mode;
	const struct gc2155_mode *framesize_cfg;
	unsigned int cfg_num;
	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
};

#define to_gc2155(sd) container_of(sd, struct gc2155, subdev)

static struct regval gc2155_global_regs[] = {
	{0xfe, 0xf0},
	{0xfe, 0xf0},
	{0xfe, 0xf0},
	{0xfc, 0x06},
	{0xf6, 0x00},
	{0xf7, 0x1d},
	{0xf8, 0x84},
	{0xfa, 0x00},
	{0xf9, 0xfe},
	{0xf2, 0x00},
	/* ISP reg */
	{0xfe, 0x00},
	{0x03, 0x04},
	{0x04, 0xe2},
	{0x09, 0x00},
	{0x0a, 0x00},
	{0x0b, 0x00},
	{0x0c, 0x00},
	{0x0d, 0x04},
	{0x0e, 0xc0},
	{0x0f, 0x06},
	{0x10, 0x50},
	{0x12, 0x2e},
	{0x17, 0x14}, // mirror
	{0x18, 0x02},
	{0x19, 0x0e},
	{0x1a, 0x01},
	{0x1b, 0x4b},
	{0x1c, 0x07},
	{0x1d, 0x10},
	{0x1e, 0x98},
	{0x1f, 0x78},
	{0x20, 0x05},
	{0x21, 0x40},
	{0x22, 0xf0},
	{0x24, 0x16},
	{0x25, 0x01},
	{0x26, 0x10},
	{0x2d, 0x40},
	{0x30, 0x01},
	{0x31, 0x90},
	{0x33, 0x04},
	{0x34, 0x01},
	/* ISP reg */
	{0xfe, 0x00},
	{0x80, 0xff},
	{0x81, 0x2c},
	{0x82, 0xfa},
	{0x83, 0x00},
	{0x84, 0x00}, //yuv 01
	{0x85, 0x08},
	{0x86, 0x02},
	{0x89, 0x03},
	{0x8a, 0x00},
	{0x8b, 0x00},
	{0xb0, 0x55},
	{0xc3, 0x11}, //00
	{0xc4, 0x20},
	{0xc5, 0x30},
	{0xc6, 0x38},
	{0xc7, 0x40},
	{0xec, 0x02},
	{0xed, 0x04},
	{0xee, 0x60},
	{0xef, 0x90},
	{0xb6, 0x01},
	{0x90, 0x01},
	{0x91, 0x00},
	{0x92, 0x00},
	{0x93, 0x00},
	{0x94, 0x00},
	{0x95, 0x04},
	{0x96, 0xb0},
	{0x97, 0x06},
	{0x98, 0x40},
	/*   BLK   */
	{0xfe, 0x00},
	{0x18, 0x02},
	{0x40, 0x42},
	{0x41, 0x00},
	{0x43, 0x5b}, //0x54
	{0x5e, 0x00},
	{0x5f, 0x00},
	{0x60, 0x00},
	{0x61, 0x00},
	{0x62, 0x00},
	{0x63, 0x00},
	{0x64, 0x00},
	{0x65, 0x00},
	{0x66, 0x20},
	{0x67, 0x20},
	{0x68, 0x20},
	{0x69, 0x20},
	{0x6a, 0x08},
	{0x6b, 0x08},
	{0x6c, 0x08},
	{0x6d, 0x08},
	{0x6e, 0x08},
	{0x6f, 0x08},
	{0x70, 0x08},
	{0x71, 0x08},
	{0x72, 0xf0},
	{0x7e, 0x3c},
	{0x7f, 0x00},
	{0xfe, 0x00},
	/*   AEC   */
	{0xfe, 0x01},
	{0x01, 0x08},
	{0x02, 0xc0},
	{0x03, 0x04},
	{0x04, 0x90},
	{0x05, 0x30},
	{0x06, 0x98},
	{0x07, 0x28},
	{0x08, 0x6c},
	{0x09, 0x00},
	{0x0a, 0xc2},
	{0x0b, 0x11},
	{0x0c, 0x10},
	{0x13, 0x2d},
	{0x17, 0x00},
	{0x1c, 0x11},
	{0x1e, 0x61},
	{0x1f, 0x30},
	{0x20, 0x40},
	{0x22, 0x80},
	{0x23, 0x20},

	{0x12, 0x35},
	{0x15, 0x50},
	{0x10, 0x31},
	{0x3e, 0x28},
	{0x3f, 0xe0},
	{0x40, 0xe0},
	{0x41, 0x08},

	{0xfe, 0x02},
	{0x0f, 0x05},
	/*  INTPEE */
	{0xfe, 0x02},
	{0x90, 0x6c},
	{0x91, 0x03},
	{0x92, 0xc4},
	{0x97, 0x64},
	{0x98, 0x88},
	{0x9d, 0x08},
	{0xa2, 0x11},
	{0xfe, 0x00},
	/*   DNDD  */
	{0xfe, 0x02},
	{0x80, 0xc1},
	{0x81, 0x08},
	{0x82, 0x05},
	{0x83, 0x04},
	{0x84, 0x0a},
	{0x86, 0x80},
	{0x87, 0x30},
	{0x88, 0x15},
	{0x89, 0x80},
	{0x8a, 0x60},
	{0x8b, 0x30},
	/*   ASDE  */
	{0xfe, 0x01},
	{0x21, 0x14},
	{0xfe, 0x02},
	{0x3c, 0x06},
	{0x3d, 0x40},
	{0x48, 0x30},
	{0x49, 0x06},
	{0x4b, 0x08},
	{0x4c, 0x20},
	{0xa3, 0x50},
	{0xa4, 0x30},
	{0xa5, 0x40},
	{0xa6, 0x80},
	{0xab, 0x40},
	{0xae, 0x0c},
	{0xb3, 0x42},
	{0xb4, 0x24},
	{0xb6, 0x50},
	{0xb7, 0x01},
	{0xb9, 0x28},
	{0xfe, 0x00},
	/*  gamma1 */
	{0xfe, 0x02},
	{0x10, 0x0d},
	{0x11, 0x12},
	{0x12, 0x17},
	{0x13, 0x1c},
	{0x14, 0x27},
	{0x15, 0x34},
	{0x16, 0x44},
	{0x17, 0x55},
	{0x18, 0x6e},
	{0x19, 0x81},
	{0x1a, 0x91},
	{0x1b, 0x9c},
	{0x1c, 0xaa},
	{0x1d, 0xbb},
	{0x1e, 0xca},
	{0x1f, 0xd5},
	{0x20, 0xe0},
	{0x21, 0xe7},
	{0x22, 0xed},
	{0x23, 0xf6},
	{0x24, 0xfb},
	{0x25, 0xff},
	/*  gamma2 */
	{0xfe, 0x02},
	{0x26, 0x0d},
	{0x27, 0x12},
	{0x28, 0x17},
	{0x29, 0x1c},
	{0x2a, 0x27},
	{0x2b, 0x34},
	{0x2c, 0x44},
	{0x2d, 0x55},
	{0x2e, 0x6e},
	{0x2f, 0x81},
	{0x30, 0x91},
	{0x31, 0x9c},
	{0x32, 0xaa},
	{0x33, 0xbb},
	{0x34, 0xca},
	{0x35, 0xd5},
	{0x36, 0xe0},
	{0x37, 0xe7},
	{0x38, 0xed},
	{0x39, 0xf6},
	{0x3a, 0xfb},
	{0x3b, 0xff},
	/*   YCP   */
	{0xfe, 0x02},
	{0xd1, 0x28},
	{0xd2, 0x28},
	{0xdd, 0x14},
	{0xde, 0x88},
	{0xed, 0x80},
	/*   LSC   */
	{0xfe, 0x01},
	{0xc2, 0x1f},
	{0xc3, 0x13},
	{0xc4, 0x0e},
	{0xc8, 0x16},
	{0xc9, 0x0f},
	{0xca, 0x0c},
	{0xbc, 0x52},
	{0xbd, 0x2c},
	{0xbe, 0x27},
	{0xb6, 0x47},
	{0xb7, 0x32},
	{0xb8, 0x30},
	{0xc5, 0x00},
	{0xc6, 0x00},
	{0xc7, 0x00},
	{0xcb, 0x00},
	{0xcc, 0x00},
	{0xcd, 0x00},
	{0xbf, 0x0e},
	{0xc0, 0x00},
	{0xc1, 0x00},
	{0xb9, 0x08},
	{0xba, 0x00},
	{0xbb, 0x00},
	{0xaa, 0x0a},
	{0xab, 0x0c},
	{0xac, 0x0d},
	{0xad, 0x02},
	{0xae, 0x06},
	{0xaf, 0x05},
	{0xb0, 0x00},
	{0xb1, 0x05},
	{0xb2, 0x02},
	{0xb3, 0x04},
	{0xb4, 0x04},
	{0xb5, 0x05},
	{0xd0, 0x00},
	{0xd1, 0x00},
	{0xd2, 0x00},
	{0xd6, 0x02},
	{0xd7, 0x00},
	{0xd8, 0x00},
	{0xd9, 0x00},
	{0xda, 0x00},
	{0xdb, 0x00},
	{0xd3, 0x00},
	{0xd4, 0x00},
	{0xd5, 0x00},
	{0xa4, 0x04},
	{0xa5, 0x00},
	{0xa6, 0x77},
	{0xa7, 0x77},
	{0xa8, 0x77},
	{0xa9, 0x77},
	{0xa1, 0x80},
	{0xa2, 0x80},

	{0xfe, 0x01},
	{0xdc, 0x35},
	{0xdd, 0x28},
	{0xdf, 0x0d},
	{0xe0, 0x70},
	{0xe1, 0x78},
	{0xe2, 0x70},
	{0xe3, 0x78},
	{0xe6, 0x90},
	{0xe7, 0x70},
	{0xe8, 0x90},
	{0xe9, 0x70},
	{0xfe, 0x00},
	/*   AWB   */
	{0xfe, 0x01},
	{0x4f, 0x00},
	{0x4f, 0x00},
	{0x4b, 0x01},
	{0x4f, 0x00},
	{0x4c, 0x01},
	{0x4d, 0x71},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x91},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x50},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x70},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x90},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xb0},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xd0},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x4f},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x6f},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x8f},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xaf},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xcf},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x6e},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8e},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xae},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xce},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x4d},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x6d},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8d},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xad},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xcd},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x4c},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x6c},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8c},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xac},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xcc},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xec},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x4b},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x6b},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8b},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xab},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8a},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xaa},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xca},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xa9},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xc9},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xcb},
	{0x4e, 0x05},
	{0x4c, 0x01},
	{0x4d, 0xeb},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x0b},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x2b},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x4b},
	{0x4e, 0x05},
	{0x4c, 0x01},
	{0x4d, 0xea},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x0a},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x2a},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x6a},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x29},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x49},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x69},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x89},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0xa9},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0xc9},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x48},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x68},
	{0x4e, 0x06},
	{0x4c, 0x03},
	{0x4d, 0x09},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xa8},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xc8},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xe8},
	{0x4e, 0x07},
	{0x4c, 0x03},
	{0x4d, 0x08},
	{0x4e, 0x07},
	{0x4c, 0x03},
	{0x4d, 0x28},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0x87},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xa7},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xc7},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xe7},
	{0x4e, 0x07},
	{0x4c, 0x03},
	{0x4d, 0x07},
	{0x4e, 0x07},
	{0x4f, 0x01},
	{0xfe, 0x01},

	{0x50, 0x80},
	{0x51, 0xa8},
	{0x52, 0x57},
	{0x53, 0x38},
	{0x54, 0xc7},
	{0x56, 0x0e},
	{0x58, 0x08},
	{0x5b, 0x00},
	{0x5c, 0x74},
	{0x5d, 0x8b},
	{0x61, 0xd3},
	{0x62, 0x90},
	{0x63, 0xaa},
	{0x65, 0x04},
	{0x67, 0xb2},
	{0x68, 0xac},
	{0x69, 0x00},
	{0x6a, 0xb2},
	{0x6b, 0xac},
	{0x6c, 0xdc},
	{0x6d, 0xb0},
	{0x6e, 0x30},
	{0x6f, 0x40},
	{0x70, 0x05},
	{0x71, 0x80},
	{0x72, 0x80},
	{0x73, 0x30},
	{0x74, 0x01},
	{0x75, 0x01},
	{0x7f, 0x08},
	{0x76, 0x70},
	{0x77, 0x48},
	{0x78, 0xa0},
	{0xfe, 0x00},
	/*   CC    */
	{0xfe, 0x02},
	{0xc0, 0x01},
	{0xc1, 0x4a},
	{0xc2, 0xf3},
	{0xc3, 0xfc},
	{0xc4, 0xe4},
	{0xc5, 0x48},
	{0xc6, 0xec},
	{0xc7, 0x45},
	{0xc8, 0xf8},
	{0xc9, 0x02},
	{0xca, 0xfe},
	{0xcb, 0x42},
	{0xcc, 0x00},
	{0xcd, 0x45},
	{0xce, 0xf0},
	{0xcf, 0x00},
	{0xe3, 0xf0},
	{0xe4, 0x45},
	{0xe5, 0xe8},
	/*   ABS   */
	{0xfe, 0x01},
	{0x9f, 0x42},
	{0xfe, 0x00},
	/* frame rate 50Hz */
	{0xfe, 0x00},
	{0x05, 0x02},
	{0x06, 0x20},
	{0x07, 0x00},
	{0x08, 0x50},
	{0xfe, 0x01},
	{0x25, 0x00},
	{0x26, 0xfa},

	{0x27, 0x04},
	{0x28, 0xe2},
	{0x29, 0x04},
	{0x2a, 0xe2},
	{0x2b, 0x04},
	{0x2c, 0xe2},
	{0x2d, 0x04},
	{0x2e, 0xe2},

	/*   SVGA  */
	{0xfe, 0x00},
	{0xfa, 0x00},
	{0xfd, 0x01},
	/* crop window */
	{0xfe, 0x00},
	{0x90, 0x01},
	{0x91, 0x00},
	{0x92, 0x00},
	{0x93, 0x00},
	{0x94, 0x00},
	{0x95, 0x02},
	{0x96, 0x58},
	{0x97, 0x03},
	{0x98, 0x20},
	{0x99, 0x11},
	{0x9a, 0x06},
	/*   AWB   */
	{0xfe, 0x00},
	{0xec, 0x01},
	{0xed, 0x02},
	{0xee, 0x30},
	{0xef, 0x48},
	{0xfe, 0x01},
	{0x74, 0x00},
	/*   AEC   */
	{0xfe, 0x01},
	{0x01, 0x04},
	{0x02, 0x60},
	{0x03, 0x02},
	{0x04, 0x48},
	{0x05, 0x18},
	{0x06, 0x4c},
	{0x07, 0x14},
	{0x08, 0x36},
	{0x0a, 0xc0},
	{0x21, 0x14},
	{0xfe, 0x00},
	/*  gamma  */
	{0xfe, 0x00},
	{0xc3, 0x11},
	{0xc4, 0x20},
	{0xc5, 0x30},
	{0xfe, 0x00},
	/*  OUTPUT */
	{0xfe, 0x00},
	{0xf2, 0x0f},
	{REG_NULL, 0x0},
};

static struct regval gc2155_800x600_15fps[] = {
	//SENSORDB("GC2155_Sensor_SVGA"},
	{0xfe, 0x00},
	{0xb6, 0x01},
	{0xfd, 0x01},
	{0xfa, 0x00},

	/*crop window*/
	{0xfe, 0x00},
	{0x90, 0x01},
	{0x91, 0x00},
	{0x92, 0x00},
	{0x93, 0x00},
	{0x94, 0x00},
	{0x95, 0x02},
	{0x96, 0x58},
	{0x97, 0x03},
	{0x98, 0x20},
	{0x99, 0x11},
	{0x9a, 0x06},
	/*AWB*/
	{0xfe, 0x00},
	{0xec, 0x01},
	{0xed, 0x02},
	{0xee, 0x30},
	{0xef, 0x48},
	{0xfe, 0x01},
	{0x74, 0x00},
	/*   AEC   */
	{0xfe, 0x01},
	{0x01, 0x04},
	{0x02, 0x60},
	{0x03, 0x02},
	{0x04, 0x48},
	{0x05, 0x18},
	{0x06, 0x4c},
	{0x07, 0x14},
	{0x08, 0x36},
	{0x0a, 0xc0},
	{0x21, 0x14},
	{0xfe, 0x00},
	/*  gamma  */
	{0xfe, 0x00},
	{0xc3, 0x11},
	{0xc4, 0x20},
	{0xc5, 0x30},
	{0xfe, 0x00},

	{REG_NULL, 0x0},
};

static struct regval gc2155_1600x1200_7fps[] = {
	//SENSORDB("GC2155_Sensor_2M"},
	{0xfe, 0x00},
	{0xb6, 0x00},
	{0xfa, 0x11},
	{0xfd, 0x00},
	/* crop window */
	{0xfe, 0x00},
	{0x90, 0x01},
	{0x91, 0x00},
	{0x92, 0x00},
	{0x93, 0x00},
	{0x94, 0x00},
	{0x95, 0x04},
	{0x96, 0xb0},
	{0x97, 0x06},
	{0x98, 0x40},
	{0x99, 0x11},
	{0x9a, 0x06},
	/*AWB*/
	{0xfe, 0x00},
	{0xec, 0x02},
	{0xed, 0x04},
	{0xee, 0x60},
	{0xef, 0x90},
	{0xfe, 0x01},
	{0x74, 0x01},
	/*   AEC   */
	{0xfe, 0x01},
	{0x01, 0x08},
	{0x02, 0xc0},
	{0x03, 0x04},
	{0x04, 0x90},
	{0x05, 0x30},
	{0x06, 0x98},
	{0x07, 0x28},
	{0x08, 0x6c},
	{0x0a, 0xc2},
	{0x21, 0x15}, //if 0xfa=11,then 0x21=15;else if 0xfa=00,then 0x21=14
	{0xfe, 0x00},
	/*  gamma  */
	{0xfe, 0x00},
	{0xc3, 0x00}, //if shutter/2 when capture,then exp_gamma_th/2
	{0xc4, 0x90},
	{0xc5, 0x98},
	{0xfe, 0x00},

	{REG_NULL, 0x0},
};

static const struct gc2155_mode supported_modes[] = {
	{
		.width		= 800,
		.height		= 600,
		.fps		= 15,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.reg_list	= gc2155_800x600_15fps,
	},
	{
		.width		= 1600,
		.height		= 1200,
		.fps		= 7,
		.max_fps = {
			.numerator = 10000,
			.denominator = 70000,
		},
		.reg_list	= gc2155_1600x1200_7fps,
	},
};

static int gc2155_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0)
		dev_err(&client->dev, "write reg error: %d\n", ret);

	return ret;
}

static int gc2155_write_array(struct i2c_client *client,
	const struct regval *regs)
{
	int i, ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc2155_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

static inline u8 gc2155_read_reg(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int gc2155_get_reso_dist(const struct gc2155_mode *mode,
	struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct gc2155_mode *
gc2155_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = gc2155_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

#ifdef GC2155_EXPOSURE_CONTROL
/*
 * the function is called before sensor register setting in VIDIOC_S_FMT
 */
/* Row times = Hb + Sh_delay + win_width + 4*/

static int gc2155_aec_ctrl(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	u8 value;
	static unsigned int capture_fps = 75, capture_lines = 1266;
	static unsigned int preview_fps = 150, preview_lines = 1266;
	static unsigned int lines_10ms = 1;
	static unsigned int shutter_h = 0x04, shutter_l = 0xe2;
	static unsigned int cap = 0, shutter = 0x04e2;

	dev_info(&client->dev, "%s enter\n", __func__);
	if ((mf->width == 800 && mf->height == 600) && cap == 1) {
		cap = 0;
		ret = gc2155_write_reg(client, 0xfe, 0x00);
		ret |= gc2155_write_reg(client, 0xb6, 0x00);
		ret |= gc2155_write_reg(client, 0x03, shutter_h);
		ret |= gc2155_write_reg(client, 0x04, shutter_l);
		ret |= gc2155_write_reg(client, 0x82, 0xfa);
		ret |= gc2155_write_reg(client, 0xb6, 0x01);
		if (ret)
			dev_err(&client->dev, "gc2155 reconfig failed!\n");
	}
	if (mf->width == 1600 && mf->height == 1200) {
		cap = 1;
		ret = gc2155_write_reg(client, 0xfe, 0x00);
		ret |= gc2155_write_reg(client, 0xb6, 0x00);
		ret |= gc2155_write_reg(client, 0x82, 0xf8);

		/*shutter calculate*/
		value = gc2155_read_reg(client, 0x03);
		shutter_h = value;
		shutter = (value << 8);
		value = gc2155_read_reg(client, 0x04);
		shutter_l = value;
		shutter |= (value & 0xff);
		dev_info(&client->dev, "%s(%d) 800x600 shutter read(0x%04x)!\n",
					__func__, __LINE__, shutter);
		shutter = shutter * capture_lines / preview_lines;
		shutter = shutter * capture_fps / preview_fps;
		lines_10ms = capture_fps * capture_lines / 100 / 10;
		if (shutter > lines_10ms) {
			shutter = shutter + lines_10ms / 2;
			shutter /= lines_10ms;
			shutter *= lines_10ms;
		}
		if (shutter < 1)
			shutter = 0x276;
		dev_info(&client->dev, "%s(%d)lines_10ms(%d),cal(0x%08x)!\n",
			  __func__, __LINE__, lines_10ms, shutter);

		ret |= gc2155_write_reg(client, 0x03, ((shutter >> 8) & 0x1f));
		ret |= gc2155_write_reg(client, 0x04, (shutter & 0xff));
		if (ret)
			dev_err(&client->dev, "full exp reconfig failed!\n");
	}
	return ret;
}
#endif

static int gc2155_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct gc2155 *gc2155 = to_gc2155(sd);
	const struct gc2155_mode *mode;

	mutex_lock(&gc2155->mutex);

	mode = gc2155_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_UYVY8_2X8;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_SRGB;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc2155->mutex);
		return -ENOTTY;
#endif
	} else {
		gc2155->cur_mode = mode;
		gc2155->format = fmt->format;
	}

#ifdef GC2155_EXPOSURE_CONTROL
		if (gc2155->power_on)
			gc2155_aec_ctrl(sd, &fmt->format);
#endif
	mutex_unlock(&gc2155->mutex);

	return 0;
}

static int gc2155_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct gc2155 *gc2155 = to_gc2155(sd);
	const struct gc2155_mode *mode = gc2155->cur_mode;

	mutex_lock(&gc2155->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc2155->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_UYVY8_2X8;
		fmt->format.field = V4L2_FIELD_NONE;
		fmt->format.colorspace = V4L2_COLORSPACE_SRGB;
	}
	mutex_unlock(&gc2155->mutex);

	return 0;
}

static void gc2155_get_default_format(struct gc2155 *gc2155,
				      struct v4l2_mbus_framefmt *format)
{
	format->width = gc2155->cur_mode->width;
	format->height = gc2155->cur_mode->height;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = MEDIA_BUS_FMT_UYVY8_2X8;
	format->field = V4L2_FIELD_NONE;
}

static int gc2155_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_2X8;

	return 0;
}

static int gc2155_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	u32 index = fse->index;

	if (index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fse->code = MEDIA_BUS_FMT_UYVY8_2X8;

	fse->min_width  = supported_modes[index].width;
	fse->max_width  = supported_modes[index].width;
	fse->max_height = supported_modes[index].height;
	fse->min_height = supported_modes[index].height;

	return 0;
}

static int __gc2155_power_on(struct gc2155 *gc2155)
{
	int ret;
	struct device *dev = &gc2155->client->dev;

	dev_info(dev, "%s(%d)\n", __func__, __LINE__);
	if (!IS_ERR(gc2155->power_gpio)) {
		gpiod_set_value_cansleep(gc2155->power_gpio, 1);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc2155->reset_gpio)) {
		gpiod_set_value_cansleep(gc2155->reset_gpio, 0);
		usleep_range(2000, 5000);
	}
	ret = regulator_bulk_enable(GC2155_NUM_SUPPLIES, gc2155->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		return ret;
	}

	ret = clk_set_rate(gc2155->xvclk, GC2155_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc2155->xvclk) != GC2155_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc2155->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(gc2155->pwdn_gpio)) {
		gpiod_set_value_cansleep(gc2155->pwdn_gpio, 0);
		usleep_range(2000, 5000);
	}

	if (!IS_ERR(gc2155->reset_gpio))
		gpiod_set_value_cansleep(gc2155->reset_gpio, 1);
	usleep_range(7000, 10000);
	gc2155->power_on = true;
	return 0;
}

static void __gc2155_power_off(struct gc2155 *gc2155)
{
	if (!IS_ERR(gc2155->reset_gpio))
		gpiod_set_value_cansleep(gc2155->reset_gpio, 0);
	if (!IS_ERR(gc2155->pwdn_gpio))
		gpiod_set_value_cansleep(gc2155->pwdn_gpio, 1);

	if (!IS_ERR(gc2155->xvclk))
		clk_disable_unprepare(gc2155->xvclk);
	if (!IS_ERR(gc2155->power_gpio))
		gpiod_set_value_cansleep(gc2155->power_gpio, 0);
	regulator_bulk_disable(GC2155_NUM_SUPPLIES, gc2155->supplies);
	gc2155->power_on = false;
}

static void gc2155_get_module_inf(struct gc2155 *gc2155,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, GC2155_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc2155->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, gc2155->len_name, sizeof(inf->base.lens));
}

static long gc2155_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc2155 *gc2155 = to_gc2155(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc2155_get_module_inf(gc2155, (struct rkmodule_inf *)arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc2155_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc2155_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = gc2155_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int gc2155_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc2155 *gc2155 = to_gc2155(sd);
	struct i2c_client *client = gc2155->client;
	int ret = 0;
	u8 val;
	unsigned int fps;
	int delay_us;

	fps = DIV_ROUND_CLOSEST(gc2155->cur_mode->max_fps.denominator,
			  gc2155->cur_mode->max_fps.numerator);

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				gc2155->cur_mode->width,
				gc2155->cur_mode->height,
				fps);

	mutex_lock(&gc2155->mutex);

	on = !!on;
	if (on == gc2155->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&gc2155->client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc2155_write_array(gc2155->client,
					  gc2155->cur_mode->reg_list);
		if (ret) {
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}

	} else {
		pm_runtime_put(&client->dev);
	}
	val = on ? 0x0f : 0;
	ret = gc2155_write_reg(client, 0xf2, val);
	gc2155->streaming = on;

	/* delay to enable oneframe complete */
	if (!on) {
		delay_us = 1000 * 1000 / fps;
		usleep_range(delay_us, delay_us + 10);
		dev_info(&client->dev, "%s: on: %d, sleep(%dus)\n",
				__func__, on, delay_us);
	}

unlock_and_return:
	mutex_unlock(&gc2155->mutex);

	return ret;
}

static int gc2155_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc2155 *gc2155 = to_gc2155(sd);
	struct i2c_client *client = gc2155->client;
	int ret = 0;

	mutex_lock(&gc2155->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc2155->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc2155_write_array(gc2155->client, gc2155_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc2155->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc2155->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc2155->mutex);

	return ret;
}

static int gc2155_set_test_pattern(struct gc2155 *gc2155, int value)
{
	return 0;
}

static int gc2155_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc2155 *gc2155 =
			container_of(ctrl->handler, struct gc2155, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		return gc2155_set_test_pattern(gc2155, ctrl->val);
	}

	return 0;
}

static const struct v4l2_ctrl_ops gc2155_ctrl_ops = {
	.s_ctrl = gc2155_s_ctrl,
};

static const char * const gc2155_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bars",
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc2155_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc2155 *gc2155 = to_gc2155(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc2155_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc2155->mutex);

	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	try_fmt->field = V4L2_FIELD_NONE;
	try_fmt->colorspace = V4L2_COLORSPACE_SRGB;

	mutex_unlock(&gc2155->mutex);

	return 0;
}
#endif

static int gc2155_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2155 *gc2155 = to_gc2155(sd);

	return __gc2155_power_on(gc2155);
}

static int gc2155_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2155 *gc2155 = to_gc2155(sd);

	__gc2155_power_off(gc2155);

	return 0;
}

static int gc2155_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_PARALLEL;
	config->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_LOW |
			V4L2_MBUS_PCLK_SAMPLE_RISING;

	return 0;
}

static int gc2155_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc2155 *gc2155 = to_gc2155(sd);

	mutex_lock(&gc2155->mutex);
	fi->interval = gc2155->cur_mode->max_fps;
	mutex_unlock(&gc2155->mutex);

	return 0;
}

static void __gc2155_try_frame_size_fps(struct gc2155 *gc2155,
					struct v4l2_mbus_framefmt *mf,
					const struct gc2155_mode **size,
					unsigned int fps)
{
	const struct gc2155_mode *fsize = &gc2155->framesize_cfg[0];
	const struct gc2155_mode *match = NULL;
	unsigned int i = gc2155->cfg_num;
	unsigned int min_err = UINT_MAX;

	while (i--) {
		unsigned int err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);
		if (err < min_err && fsize->reg_list[0].addr) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}

	if (!match) {
		match = &gc2155->framesize_cfg[0];
	} else {
		fsize = &gc2155->framesize_cfg[0];
		for (i = 0; i < gc2155->cfg_num; i++) {
			if (fsize->width == match->width &&
			    fsize->height == match->height &&
			    fps >= fsize->fps)
				match = fsize;

			fsize++;
		}
	}

	mf->width  = match->width;
	mf->height = match->height;

	if (size)
		*size = match;
}

static int gc2155_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc2155 *gc2155 = to_gc2155(sd);
	const struct gc2155_mode *mode = NULL;
	struct v4l2_mbus_framefmt mf;
	unsigned int fps;
	int ret = 0;

	dev_dbg(&client->dev, "Setting %d/%d frame interval\n",
		 fi->interval.numerator, fi->interval.denominator);

	mutex_lock(&gc2155->mutex);
	if (gc2155->cur_mode->width == 1600)
		goto unlock;
	fps = DIV_ROUND_CLOSEST(fi->interval.denominator,
				fi->interval.numerator);
	mf = gc2155->format;
	__gc2155_try_frame_size_fps(gc2155, &mf, &mode, fps);
	if (gc2155->cur_mode != mode) {
		ret = gc2155_write_array(client, mode->reg_list);
		if (ret)
			goto unlock;
		gc2155->cur_mode = mode;
		gc2155->fps = fps;
	}
unlock:
	mutex_unlock(&gc2155->mutex);

	return ret;
}

static int gc2155_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_UYVY8_2X8)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static const struct dev_pm_ops gc2155_pm_ops = {
	SET_RUNTIME_PM_OPS(gc2155_runtime_suspend,
			   gc2155_runtime_resume, NULL)
};

static const struct v4l2_subdev_core_ops gc2155_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = gc2155_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc2155_compat_ioctl32,
#endif
	.s_power = gc2155_s_power,
};

static const struct v4l2_subdev_video_ops gc2155_video_ops = {
	.s_stream = gc2155_s_stream,
	.g_mbus_config = gc2155_g_mbus_config,
	.g_frame_interval = gc2155_g_frame_interval,
	.s_frame_interval = gc2155_s_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc2155_pad_ops = {
	.enum_mbus_code = gc2155_enum_mbus_code,
	.enum_frame_size = gc2155_enum_frame_sizes,
	.enum_frame_interval = gc2155_enum_frame_interval,
	.get_fmt = gc2155_get_fmt,
	.set_fmt = gc2155_set_fmt,
};

static const struct v4l2_subdev_ops gc2155_subdev_ops = {
	.core	= &gc2155_core_ops,
	.video	= &gc2155_video_ops,
	.pad	= &gc2155_pad_ops,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc2155_internal_ops = {
	.open = gc2155_open,
};
#endif

static int gc2155_check_sensor_id(struct gc2155 *gc2155,
				  struct i2c_client *client)
{
	struct device *dev = &gc2155->client->dev;
	u8 id_h, id_l;

	id_h = gc2155_read_reg(client, REG_CHIP_ID_H);
	id_l = gc2155_read_reg(client, REG_CHIP_ID_L);
	if (id_h != CHIP_ID_H && id_l != CHIP_ID_L) {
		dev_err(dev, "Wrong camera sensor id(0x%02x%02x)\n",
			id_h, id_l);
		return -EINVAL;
	}

	dev_info(dev, "Detected GC2155 (0x%02x%02x) sensor\n",
		CHIP_ID_H, CHIP_ID_L);

	return 0;
}

static int gc2155_configure_regulators(struct gc2155 *gc2155)
{
	u32 i;

	for (i = 0; i < GC2155_NUM_SUPPLIES; i++)
		gc2155->supplies[i].supply = gc2155_supply_names[i];

	return devm_regulator_bulk_get(&gc2155->client->dev,
				       GC2155_NUM_SUPPLIES,
				       gc2155->supplies);
}

static int gc2155_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc2155 *gc2155;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc2155 = devm_kzalloc(dev, sizeof(*gc2155), GFP_KERNEL);
	if (!gc2155)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc2155->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc2155->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc2155->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc2155->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc2155->client = client;
	gc2155->cur_mode = &supported_modes[0];
	gc2155_get_default_format(gc2155, &gc2155->format);
	gc2155->format.width = gc2155->cur_mode->width;
	gc2155->format.height = gc2155->cur_mode->height;
	gc2155->fps =  DIV_ROUND_CLOSEST(gc2155->cur_mode->max_fps.denominator,
			gc2155->cur_mode->max_fps.numerator);
	gc2155->framesize_cfg = supported_modes;
	gc2155->cfg_num = ARRAY_SIZE(supported_modes);

	gc2155->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc2155->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	gc2155->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(gc2155->power_gpio))
		dev_info(dev, "Failed to get power-gpios, maybe no use\n");

	gc2155->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc2155->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc2155->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc2155->pwdn_gpio))
		dev_warn(dev, "Failed to get gc2155-gpios\n");

	ret = gc2155_configure_regulators(gc2155);
	if (ret) {
		dev_warn(dev, "Failed to get power regulators\n");
		return ret;
	}
	v4l2_ctrl_handler_init(&gc2155->ctrls, 2);
	gc2155->pixel_rate =
			v4l2_ctrl_new_std(&gc2155->ctrls, &gc2155_ctrl_ops,
					  V4L2_CID_PIXEL_RATE, 0,
					  GC2155_PIXEL_RATE, 1,
					  GC2155_PIXEL_RATE);

	v4l2_ctrl_new_std_menu_items(&gc2155->ctrls, &gc2155_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(gc2155_test_pattern_menu) - 1,
				     0, 0, gc2155_test_pattern_menu);
	gc2155->subdev.ctrl_handler = &gc2155->ctrls;

	if (gc2155->ctrls.error) {
		dev_err(&client->dev, "%s: control initialization error %d\n",
			__func__, gc2155->ctrls.error);
		return  gc2155->ctrls.error;
	}

	mutex_init(&gc2155->mutex);
	v4l2_i2c_subdev_init(&gc2155->subdev, client, &gc2155_subdev_ops);

	ret = __gc2155_power_on(gc2155);
	if (ret)
		goto err_destroy_mutex;

	ret = gc2155_check_sensor_id(gc2155, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	gc2155->subdev.internal_ops = &gc2155_internal_ops;
	gc2155->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc2155->pad.flags = MEDIA_PAD_FL_SOURCE;
	gc2155->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&gc2155->subdev.entity, 1, &gc2155->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	sd = &gc2155->subdev;
	memset(facing, 0, sizeof(facing));
	if (strcmp(gc2155->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc2155->module_index, facing,
		 GC2155_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&gc2155->subdev.entity);
#endif
err_power_off:
	__gc2155_power_off(gc2155);
err_destroy_mutex:
	mutex_destroy(&gc2155->mutex);
	v4l2_ctrl_handler_free(&gc2155->ctrls);
	return ret;
}

static int gc2155_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2155 *gc2155 = to_gc2155(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&gc2155->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc2155_power_off(gc2155);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc2155_of_match[] = {
	{ .compatible = "galaxycore,gc2155" },
	{},
};
MODULE_DEVICE_TABLE(of, gc2155_of_match);
#endif

static const struct i2c_device_id gc2155_match_id[] = {
	{"gc2155", 0},
	{},
};

static struct i2c_driver gc2155_i2c_driver = {
	.driver = {
		.name = GC2155_NAME,
		.pm = &gc2155_pm_ops,
		.of_match_table = of_match_ptr(gc2155_of_match),
	},
	.probe		= gc2155_probe,
	.remove		= gc2155_remove,
	.id_table	= gc2155_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc2155_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc2155_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GalaxyCore gc2155 sensor driver");
MODULE_LICENSE("GPL v2");
