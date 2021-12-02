// SPDX-License-Identifier: GPL-2.0
/*
 * gc08a3 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 init first version.
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
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC08A3_REG_VALUE_08BIT		1
#define GC08A3_REG_VALUE_16BIT		2
#define GC08A3_REG_VALUE_24BIT		3

#define GC08A3_LANES			4
#define GC08A3_BITS_PER_SAMPLE		10
#define GC08A3_MIPI_FREQ_150MHZ		150000000U
#define GC08A3_MIPI_FREQ_350MHZ		350000000U
#define GC08A3_MIPI_FREQ_700MHZ		700000000U

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define GC08A3_PIXEL_RATE		288000000
#define GC08A3_XVCLK_FREQ		24000000

#define CHIP_ID				0x08a3
#define GC08A3_REG_CHIP_ID_H		0x03f0
#define GC08A3_REG_CHIP_ID_L		0x03f1

#define GC08A3_REG_CTRL_MODE		0x0100
#define GC08A3_MODE_SW_STANDBY		0x00
#define GC08A3_MODE_STREAMING		0x01

#define GC08A3_REG_EXPOSURE_H		0x0202
#define GC08A3_REG_EXPOSURE_L		0x0203
#define GC08A3_FETCH_HIGH_BYTE(VAL) (((VAL) >> 8) & 0xFF)	/* 4 Bits */
#define GC08A3_FETCH_LOW_BYTE(VAL)	((VAL) & 0xFF)	/* 8 Bits */
#define	GC08A3_EXPOSURE_MIN		4
#define	GC08A3_EXPOSURE_STEP		1
#define GC08A3_VTS_MAX			0xfffe

#define GC08A3_REG_GAIN_H		0x0204
#define GC08A3_REG_GAIN_L		0x0205

#define GC08A3_AGAIN_MIN			0x400
#define GC08A3_AGAIN_MAX			0x4000
#define GC08A3_AGAIN_STEP		1
#define GC08A3_AGAIN_DEFAULT		0x800

#define GC08A3_REG_VTS_H		0x0340
#define GC08A3_REG_VTS_L		0x0341

#define REG_NULL			0xFFFF

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define GC08A3_NAME			"gc08a3"
#define GC08A3_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SRGGB10_1X10

static const char * const gc08a3_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define GC08A3_NUM_SUPPLIES ARRAY_SIZE(gc08a3_supply_names)

struct gc08a3_id_name {
	u32 id;
	char name[RKMODULE_NAME_LEN];
};

struct regval {
	u16 addr;
	u8 val;
};

struct gc08a3_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	const struct regval *global_reg_list;
	u32 mipi_freq_idx;
	u32 vc[PAD_MAX];
};

struct gc08a3 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[GC08A3_NUM_SUPPLIES];
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;
	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	bool			streaming;
	unsigned int		lane_num;
	unsigned int		cfg_num;
	unsigned int		pixel_rate;
	bool			power_on;
	const struct gc08a3_mode *cur_mode;
	const struct gc08a3_mode *support_modes;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	struct rkmodule_inf	module_inf;
	struct rkmodule_awb_cfg	awb_cfg;
};

#define to_gc08a3(sd) container_of(sd, struct gc08a3, subdev)

#undef GC08A3_MIRROR_NORMAL
#undef GC08A3_MIRROR_H
#undef GC08A3_MIRROR_V
#undef GC08A3_MIRROR_HV

/* SENSOR MIRROR FLIP INFO */
#define GC08A3_MIRROR_NORMAL	0
#define GC08A3_MIRROR_H		1
#define GC08A3_MIRROR_V		0
#define GC08A3_MIRROR_HV	0

#if GC08A3_MIRROR_NORMAL
	#define GC08A3_MIRROR	0x00
	#define FULL_STARTY	0x06
	#define FULL_STARTX	0x08
	#define BINNING_STARTY	0x03
	#define BINNING_STARTX	0x03
#elif GC08A3_MIRROR_H
	#define GC08A3_MIRROR	0x01
	#define FULL_STARTY	0x06
	#define FULL_STARTX	0x09
	#define BINNING_STARTY	0x03
	#define BINNING_STARTX	0x04
#elif GC08A3_MIRROR_V
	#define GC08A3_MIRROR	0x02
	#define FULL_STARTY	0x07
	#define FULL_STARTX	0x08
	#define BINNING_STARTY	0x04
	#define BINNING_STARTX	0x03
#elif GC08A3_MIRROR_HV
	#define GC08A3_MIRROR	0x03
	#define FULL_STARTY	0x07
	#define FULL_STARTX	0x09
	#define BINNING_STARTY	0x04
	#define BINNING_STARTX	0x04
#else
	#define GC08A3_MIRROR	0x00
	#define FULL_STARTY	0x06
	#define FULL_STARTX	0x08
	#define BINNING_STARTY	0x03
	#define BINNING_STARTX	0x03
#endif

/*
 * Xclk 24Mhz
 */
static const struct regval gc08a3_global_regs_4lane[] = {
	/*system*/
	{0x031c, 0x60},
	{0x0337, 0x04},
	{0x0335, 0x51},
	{0x0336, 0x70},
	{0x0383, 0xbb},
	{0x031a, 0x00},
	{0x0321, 0x10},
	{0x0327, 0x03},
	{0x0325, 0x40},
	{0x0326, 0x23},
	{0x0314, 0x11},
	{0x0315, 0xd6},
	{0x0316, 0x01},
	{0x0334, 0x40},
	{0x0324, 0x42},
	{0x031c, 0x00},
	{0x031c, 0x9f},
	{0x039a, 0x13},
	{0x0084, 0x30},
	{0x02b3, 0x08},
	{0x0057, 0x0c},
	{0x05c3, 0x50},
	{0x0311, 0x90},
	{0x05a0, 0x02},
	{0x0074, 0x0a},
	{0x0059, 0x11},
	{0x0070, 0x05},
	{0x0101, GC08A3_MIRROR},

	/*analog*/
	{0x0344, 0x00},
	{0x0345, 0x06},
	{0x0346, 0x00},
	{0x0347, 0x04},
	{0x0348, 0x0c},
	{0x0349, 0xd0},
	{0x034a, 0x09},
	{0x034b, 0x9c},
	{0x0202, 0x09},
	{0x0203, 0x04},
	{0x0340, 0x09},
	{0x0341, 0xf4},
	{0x0342, 0x07},
	{0x0343, 0x1c},
	{0x0219, 0x05},
	{0x0226, 0x00},
	{0x0227, 0x28},
	{0x0e0a, 0x00},
	{0x0e0b, 0x00},
	{0x0e24, 0x04},
	{0x0e25, 0x04},
	{0x0e26, 0x00},
	{0x0e27, 0x10},
	{0x0e01, 0x74},
	{0x0e03, 0x47},
	{0x0e04, 0x33},
	{0x0e05, 0x44},
	{0x0e06, 0x44},
	{0x0e0c, 0x1e},
	{0x0e17, 0x3a},
	{0x0e18, 0x3c},
	{0x0e19, 0x40},
	{0x0e1a, 0x42},
	{0x0e28, 0x21},
	{0x0e2b, 0x68},
	{0x0e2c, 0x0d},
	{0x0e2d, 0x08},
	{0x0e34, 0xf4},
	{0x0e35, 0x44},
	{0x0e36, 0x07},
	{0x0e38, 0x49},
	{0x0210, 0x13},
	{0x0218, 0x00},
	{0x0241, 0x88},
	{0x0e32, 0x00},
	{0x0e33, 0x18},
	{0x0e42, 0x03},
	{0x0e43, 0x80},
	{0x0e44, 0x04},
	{0x0e45, 0x00},
	{0x0e4f, 0x04},
	{0x057a, 0x20},
	{0x0381, 0x7c},
	{0x0382, 0x9b},
	{0x0384, 0xfb},
	{0x0389, 0x38},
	{0x038a, 0x03},
	{0x0390, 0x6a},
	{0x0391, 0x0b},
	{0x0392, 0x60},
	{0x0393, 0xc1},
	{0x0396, 0xff},
	{0x0398, 0x62},

	/*cisctl reset*/
	{0x031c, 0x80},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x031c, 0x9f},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x031c, 0x80},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x031c, 0x9f},
	{0x0360, 0x01},
	{0x0360, 0x00},
	{0x0316, 0x09},
	{0x0a67, 0x80},
	{0x0313, 0x00},
	{0x0a53, 0x0e},
	{0x0a65, 0x17},
	{0x0a68, 0xa1},
	{0x0a58, 0x00},
	{0x0ace, 0x0c},
	{0x00a4, 0x00},
	{0x00a5, 0x01},
	{0x00a7, 0x09},
	{0x00a8, 0x9c},
	{0x00a9, 0x0c},
	{0x00aa, 0xd0},
	{0x0a8a, 0x00},
	{0x0a8b, 0xe0},
	{0x0a8c, 0x13},
	{0x0a8d, 0xe8},
	{0x0a90, 0x0a},
	{0x0a91, 0x10},
	{0x0a92, 0xf8},
	{0x0a71, 0xf2},
	{0x0a72, 0x12},
	{0x0a73, 0x64},
	{0x0a75, 0x41},
	{0x0a70, 0x07},
	{0x0313, 0x80},

	/*ISP*/
	{0x00a0, 0x01},
	{0x0080, 0xd2},
	{0x0081, 0x3f},
	{0x0087, 0x51},
	{0x0089, 0x03},
	{0x009b, 0x40},
	{0x05a0, 0x82},
	{0x05ac, 0x00},
	{0x05ad, 0x01},
	{0x05ae, 0x00},
	{0x0800, 0x0a},
	{0x0801, 0x14},
	{0x0802, 0x28},
	{0x0803, 0x34},
	{0x0804, 0x0e},
	{0x0805, 0x33},
	{0x0806, 0x03},
	{0x0807, 0x8a},
	{0x0808, 0x50},
	{0x0809, 0x00},
	{0x080a, 0x34},
	{0x080b, 0x03},
	{0x080c, 0x26},
	{0x080d, 0x03},
	{0x080e, 0x18},
	{0x080f, 0x03},
	{0x0810, 0x10},
	{0x0811, 0x03},
	{0x0812, 0x00},
	{0x0813, 0x00},
	{0x0814, 0x01},
	{0x0815, 0x00},
	{0x0816, 0x01},
	{0x0817, 0x00},
	{0x0818, 0x00},
	{0x0819, 0x0a},
	{0x081a, 0x01},
	{0x081b, 0x6c},
	{0x081c, 0x00},
	{0x081d, 0x0b},
	{0x081e, 0x02},
	{0x081f, 0x00},
	{0x0820, 0x00},
	{0x0821, 0x0c},
	{0x0822, 0x02},
	{0x0823, 0xd9},
	{0x0824, 0x00},
	{0x0825, 0x0d},
	{0x0826, 0x03},
	{0x0827, 0xf0},
	{0x0828, 0x00},
	{0x0829, 0x0e},
	{0x082a, 0x05},
	{0x082b, 0x94},
	{0x082c, 0x09},
	{0x082d, 0x6e},
	{0x082e, 0x07},
	{0x082f, 0xe6},
	{0x0830, 0x10},
	{0x0831, 0x0e},
	{0x0832, 0x0b},
	{0x0833, 0x2c},
	{0x0834, 0x14},
	{0x0835, 0xae},
	{0x0836, 0x0f},
	{0x0837, 0xc4},
	{0x0838, 0x18},
	{0x0839, 0x0e},
	{0x05ac, 0x01},
	{0x059a, 0x00},
	{0x059b, 0x00},
	{0x059c, 0x01},
	{0x0598, 0x00},
	{0x0597, 0x14},
	{0x05ab, 0x09},
	{0x05a4, 0x02},
	{0x05a3, 0x05},
	{0x05a0, 0xc2},
	{0x0207, 0xc4},

	/*GAIN*/
	{0x0208, 0x01},
	{0x0209, 0x72},
	{0x0204, 0x04},
	{0x0205, 0x00},

	{0x0040, 0x22},
	{0x0041, 0x20},
	{0x0043, 0x10},
	{0x0044, 0x00},
	{0x0046, 0x08},
	{0x0047, 0xf0},
	{0x0048, 0x0f},
	{0x004b, 0x0f},
	{0x004c, 0x00},
	{0x0050, 0x5c},
	{0x0051, 0x44},
	{0x005b, 0x03},
	{0x00c0, 0x00},
	{0x00c1, 0x80},
	{0x00c2, 0x31},
	{0x00c3, 0x00},
	{0x0460, 0x04},
	{0x0462, 0x08},
	{0x0464, 0x0e},
	{0x0466, 0x0a},
	{0x0468, 0x12},
	{0x046a, 0x12},
	{0x046c, 0x10},
	{0x046e, 0x0c},
	{0x0461, 0x03},
	{0x0463, 0x03},
	{0x0465, 0x03},
	{0x0467, 0x03},
	{0x0469, 0x04},
	{0x046b, 0x04},
	{0x046d, 0x04},
	{0x046f, 0x04},
	{0x0470, 0x04},
	{0x0472, 0x10},
	{0x0474, 0x26},
	{0x0476, 0x38},
	{0x0478, 0x20},
	{0x047a, 0x30},
	{0x047c, 0x38},
	{0x047e, 0x60},
	{0x0471, 0x05},
	{0x0473, 0x05},
	{0x0475, 0x05},
	{0x0477, 0x05},
	{0x0479, 0x04},
	{0x047b, 0x04},
	{0x047d, 0x04},
	{0x047f, 0x04},

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 700Mbps
 */
static const struct regval gc08a3_3264x2448_regs_4lane[] = {
	   /*system*/
	{0x031c, 0x60},
	{0x0337, 0x04},
	{0x0335, 0x51},
	{0x0336, 0x70},
	{0x0383, 0xbb},
	{0x031a, 0x00},
	{0x0321, 0x10},
	{0x0327, 0x03},
	{0x0325, 0x40},
	{0x0326, 0x23},
	{0x0314, 0x11},
	{0x0315, 0xd6},
	{0x0316, 0x01},
	{0x0334, 0x40},
	{0x0324, 0x42},
	{0x031c, 0x00},
	{0x031c, 0x9f},
	{0x0344, 0x00},
	{0x0345, 0x06},
	{0x0346, 0x00},
	{0x0347, 0x04},
	{0x0348, 0x0c},
	{0x0349, 0xd0},
	{0x034a, 0x09},
	{0x034b, 0x9c},
	{0x0202, 0x09},
	{0x0203, 0x04},
	{0x0340, 0x09},
	{0x0341, 0xf4},
	{0x0342, 0x07},
	{0x0343, 0x1c},
	{0x0226, 0x00},
	{0x0227, 0x28},
	{0x0e38, 0x49},
	{0x0210, 0x13},
	{0x0218, 0x00},
	{0x0241, 0x88},
	{0x0392, 0x60},

	/*ISP*/
	{0x031c, 0x80},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x031c, 0x9f},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x031c, 0x80},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x031c, 0x9f},
	{0x00a2, 0x00},
	{0x00a3, 0x00},
	{0x00ab, 0x00},
	{0x00ac, 0x00},
	{0x05a0, 0x82},
	{0x05ac, 0x00},
	{0x05ad, 0x01},
	{0x05ae, 0x00},
	{0x0800, 0x0a},
	{0x0801, 0x14},
	{0x0802, 0x28},
	{0x0803, 0x34},
	{0x0804, 0x0e},
	{0x0805, 0x33},
	{0x0806, 0x03},
	{0x0807, 0x8a},
	{0x0808, 0x50},
	{0x0809, 0x00},
	{0x080a, 0x34},
	{0x080b, 0x03},
	{0x080c, 0x26},
	{0x080d, 0x03},
	{0x080e, 0x18},
	{0x080f, 0x03},
	{0x0810, 0x10},
	{0x0811, 0x03},
	{0x0812, 0x00},
	{0x0813, 0x00},
	{0x0814, 0x01},
	{0x0815, 0x00},
	{0x0816, 0x01},
	{0x0817, 0x00},
	{0x0818, 0x00},
	{0x0819, 0x0a},
	{0x081a, 0x01},
	{0x081b, 0x6c},
	{0x081c, 0x00},
	{0x081d, 0x0b},
	{0x081e, 0x02},
	{0x081f, 0x00},
	{0x0820, 0x00},
	{0x0821, 0x0c},
	{0x0822, 0x02},
	{0x0823, 0xd9},
	{0x0824, 0x00},
	{0x0825, 0x0d},
	{0x0826, 0x03},
	{0x0827, 0xf0},
	{0x0828, 0x00},
	{0x0829, 0x0e},
	{0x082a, 0x05},
	{0x082b, 0x94},
	{0x082c, 0x09},
	{0x082d, 0x6e},
	{0x082e, 0x07},
	{0x082f, 0xe6},
	{0x0830, 0x10},
	{0x0831, 0x0e},
	{0x0832, 0x0b},
	{0x0833, 0x2c},
	{0x0834, 0x14},
	{0x0835, 0xae},
	{0x0836, 0x0f},
	{0x0837, 0xc4},
	{0x0838, 0x18},
	{0x0839, 0x0e},
	{0x05ac, 0x01},
	{0x059a, 0x00},
	{0x059b, 0x00},
	{0x059c, 0x01},
	{0x0598, 0x00},
	{0x0597, 0x14},
	{0x05ab, 0x09},
	{0x05a4, 0x02},
	{0x05a3, 0x05},
	{0x05a0, 0xc2},
	{0x0207, 0xc4},

	/*GAIN*/
	{0x0204, 0x04},
	{0x0205, 0x00},
	{0x0050, 0x5c},
	{0x0051, 0x44},

	/*out window*/
	{0x009a, 0x00},
	{0x0351, 0x00},
	{0x0352, FULL_STARTY},
	{0x0353, 0x00},
	{0x0354, FULL_STARTX},
	{0x034c, 0x0c},
	{0x034d, 0xc0},
	{0x034e, 0x09},
	{0x034f, 0x90},

	/*MIPI*/
	{0x0114, 0x03},
	{0x0180, 0x67},
	{0x0181, 0xf0},
	{0x0185, 0x01},
	{0x0115, 0x30},
	{0x011b, 0x12},
	{0x011c, 0x12},
	{0x0121, 0x06},
	{0x0122, 0x06},
	{0x0123, 0x15},
	{0x0124, 0x01},
	{0x0125, 0x0b},
	{0x0126, 0x08},
	{0x0129, 0x06},
	{0x012a, 0x08},
	{0x012b, 0x08},

	{0x0a73, 0x60},
	{0x0a70, 0x11},
	{0x0313, 0x80},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0a70, 0x00},
	{0x00a4, 0x80},
	{0x0316, 0x01},
	{0x0a67, 0x00},
	{0x0084, 0x10},
	{0x0102, 0x09},

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 350Mbps
 */
static const struct regval gc08a3_1280x720_regs_4lane[] = {
	/*system*/
	{0x031c, 0x60},
	{0x0337, 0x04},
	{0x0335, 0x55},
	{0x0336, 0x5d},
	{0x0383, 0x9b},
	{0x031a, 0x00},
	{0x0321, 0x10},
	{0x0327, 0x03},
	{0x0325, 0x40},
	{0x0326, 0x23},
	{0x0314, 0x11},
	{0x0315, 0xd6},
	{0x0316, 0x01},
	{0x0334, 0x40},
	{0x0324, 0x42},
	{0x031c, 0x00},
	{0x031c, 0x9f},
	{0x0344, 0x01},
	{0x0345, 0x66},
	{0x0346, 0x01},
	{0x0347, 0xfc},
	{0x0348, 0x0a},
	{0x0349, 0x10},
	{0x034a, 0x05},
	{0x034b, 0xac},
	{0x0202, 0x03},
	{0x0203, 0x00},
	{0x0340, 0x09},
	{0x0341, 0xf4},
	{0x0342, 0x07},
	{0x0343, 0x1c},
	{0x0226, 0x00},
	{0x0227, 0x56},
	{0x0e38, 0x49},
	{0x0210, 0x53},
	{0x0218, 0x80},
	{0x0241, 0x8c},
	{0x0392, 0x3b},
	/*ISP*/
	{0x031c, 0x80},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x031c, 0x9f},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x031c, 0x80},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x031c, 0x9f},
	{0x00a2, 0xf8},
	{0x00a3, 0x01},
	{0x00ab, 0x60},
	{0x00ac, 0x01},
	{0x05a0, 0x82},
	{0x05ac, 0x00},
	{0x05ad, 0x01},
	{0x05ae, 0x00},
	{0x0800, 0x0a},
	{0x0801, 0x14},
	{0x0802, 0x28},
	{0x0803, 0x34},
	{0x0804, 0x0e},
	{0x0805, 0x33},
	{0x0806, 0x03},
	{0x0807, 0x8a},
	{0x0808, 0x50},
	{0x0809, 0x00},
	{0x080a, 0x34},
	{0x080b, 0x03},
	{0x080c, 0x26},
	{0x080d, 0x03},
	{0x080e, 0x18},
	{0x080f, 0x03},
	{0x0810, 0x10},
	{0x0811, 0x03},
	{0x0812, 0x00},
	{0x0813, 0x00},
	{0x0814, 0x01},
	{0x0815, 0x00},
	{0x0816, 0x01},
	{0x0817, 0x00},
	{0x0818, 0x00},
	{0x0819, 0x0a},
	{0x081a, 0x01},
	{0x081b, 0x6c},
	{0x081c, 0x00},
	{0x081d, 0x0b},
	{0x081e, 0x02},
	{0x081f, 0x00},
	{0x0820, 0x00},
	{0x0821, 0x0c},
	{0x0822, 0x02},
	{0x0823, 0xd9},
	{0x0824, 0x00},
	{0x0825, 0x0d},
	{0x0826, 0x03},
	{0x0827, 0xf0},
	{0x0828, 0x00},
	{0x0829, 0x0e},
	{0x082a, 0x05},
	{0x082b, 0x94},
	{0x082c, 0x09},
	{0x082d, 0x6e},
	{0x082e, 0x07},
	{0x082f, 0xe6},
	{0x0830, 0x10},
	{0x0831, 0x0e},
	{0x0832, 0x0b},
	{0x0833, 0x2c},
	{0x0834, 0x14},
	{0x0835, 0xae},
	{0x0836, 0x0f},
	{0x0837, 0xc4},
	{0x0838, 0x18},
	{0x0839, 0x0e},
	{0x05ac, 0x01},
	{0x059a, 0x00},
	{0x059b, 0x00},
	{0x059c, 0x01},
	{0x0598, 0x00},
	{0x0597, 0x14},
	{0x05ab, 0x09},
	{0x05a4, 0x02},
	{0x05a3, 0x05},
	{0x05a0, 0xc2},
	{0x0207, 0xc4},
	/*GAIN*/
	{0x0204, 0x04},
	{0x0205, 0x00},
	{0x0050, 0x48},
	{0x0051, 0x30},
	/*out window*/
	{0x009a, 0x00},
	{0x0351, 0x00},
	{0x0352, BINNING_STARTY},
	{0x0353, 0x00},
	{0x0354, BINNING_STARTX},
	{0x034c, 0x05},
	{0x034d, 0x00},
	{0x034e, 0x02},
	{0x034f, 0xd0},
	/*MIPI*/
	{0x0114, 0x03},
	{0x0180, 0x67},
	{0x0181, 0xf0},
	{0x0185, 0x01},
	{0x0115, 0x30},
	{0x011b, 0x12},
	{0x011c, 0x12},
	{0x0121, 0x01},
	{0x0122, 0x02},
	{0x0123, 0x07},
	{0x0124, 0x00},
	{0x0125, 0x07},
	{0x0126, 0x04},
	{0x0129, 0x02},
	{0x012a, 0x01},
	{0x012b, 0x04},
	{0x0a73, 0x60},
	{0x0a70, 0x11},
	{0x0313, 0x80},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0a70, 0x00},
	{0x00a4, 0x80},
	{0x0316, 0x01},
	{0x0a67, 0x00},
	{0x0084, 0x10},
	{0x0102, 0x09},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 350Mbps
 */
static const struct regval gc08a3_1280x800_regs_4lane[] = {
	/*system*/
	{0x031c, 0x60},
	{0x0337, 0x04},
	{0x0335, 0x55},
	{0x0336, 0x5d},
	{0x0383, 0x9b},
	{0x031a, 0x00},
	{0x0321, 0x10},
	{0x0327, 0x03},
	{0x0325, 0x40},
	{0x0326, 0x23},
	{0x0314, 0x11},
	{0x0315, 0xd6},
	{0x0316, 0x01},
	{0x0334, 0x40},
	{0x0324, 0x42},
	{0x031c, 0x00},
	{0x031c, 0x9f},
	{0x0344, 0x01},
	{0x0345, 0x66},
	{0x0346, 0x01},
	{0x0347, 0xaa},
	{0x0348, 0x0a},
	{0x0349, 0x10},
	{0x034a, 0x06},
	{0x034b, 0x50},

	{0x0202, 0x03},
	{0x0203, 0x00},
	{0x0340, 0x09},
	{0x0341, 0xf4},
	{0x0342, 0x07},
	{0x0343, 0x1c},
	{0x0226, 0x00},
	{0x0227, 0x56},
	{0x0e38, 0x49},
	{0x0210, 0x53},
	{0x0218, 0x80},
	{0x0241, 0x8c},
	{0x0392, 0x3b},
	/*ISP*/
	{0x031c, 0x80},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x031c, 0x9f},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x031c, 0x80},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x031c, 0x9f},
	{0x00a2, 0xf8},
	{0x00a3, 0x01},
	{0x00ab, 0x60},
	{0x00ac, 0x01},
	{0x05a0, 0x82},
	{0x05ac, 0x00},
	{0x05ad, 0x01},
	{0x05ae, 0x00},
	{0x0800, 0x0a},
	{0x0801, 0x14},
	{0x0802, 0x28},
	{0x0803, 0x34},
	{0x0804, 0x0e},
	{0x0805, 0x33},
	{0x0806, 0x03},
	{0x0807, 0x8a},
	{0x0808, 0x50},
	{0x0809, 0x00},
	{0x080a, 0x34},
	{0x080b, 0x03},
	{0x080c, 0x26},
	{0x080d, 0x03},
	{0x080e, 0x18},
	{0x080f, 0x03},
	{0x0810, 0x10},
	{0x0811, 0x03},
	{0x0812, 0x00},
	{0x0813, 0x00},
	{0x0814, 0x01},
	{0x0815, 0x00},
	{0x0816, 0x01},
	{0x0817, 0x00},
	{0x0818, 0x00},
	{0x0819, 0x0a},
	{0x081a, 0x01},
	{0x081b, 0x6c},
	{0x081c, 0x00},
	{0x081d, 0x0b},
	{0x081e, 0x02},
	{0x081f, 0x00},
	{0x0820, 0x00},
	{0x0821, 0x0c},
	{0x0822, 0x02},
	{0x0823, 0xd9},
	{0x0824, 0x00},
	{0x0825, 0x0d},
	{0x0826, 0x03},
	{0x0827, 0xf0},
	{0x0828, 0x00},
	{0x0829, 0x0e},
	{0x082a, 0x05},
	{0x082b, 0x94},
	{0x082c, 0x09},
	{0x082d, 0x6e},
	{0x082e, 0x07},
	{0x082f, 0xe6},
	{0x0830, 0x10},
	{0x0831, 0x0e},
	{0x0832, 0x0b},
	{0x0833, 0x2c},
	{0x0834, 0x14},
	{0x0835, 0xae},
	{0x0836, 0x0f},
	{0x0837, 0xc4},
	{0x0838, 0x18},
	{0x0839, 0x0e},
	{0x05ac, 0x01},
	{0x059a, 0x00},
	{0x059b, 0x00},
	{0x059c, 0x01},
	{0x0598, 0x00},
	{0x0597, 0x14},
	{0x05ab, 0x09},
	{0x05a4, 0x02},
	{0x05a3, 0x05},
	{0x05a0, 0xc2},
	{0x0207, 0xc4},
	/*GAIN*/
	{0x0204, 0x04},
	{0x0205, 0x00},
	{0x0050, 0x48},
	{0x0051, 0x30},
	/*out window*/
	{0x009a, 0x00},
	{0x0351, 0x00},
	{0x0352, BINNING_STARTY},
	{0x0353, 0x00},
	{0x0354, BINNING_STARTX},
	{0x034c, 0x05},
	{0x034d, 0x00},
	{0x034e, 0x03},
	{0x034f, 0x20},
	/*MIPI*/
	{0x0114, 0x03},
	{0x0180, 0x67},
	{0x0181, 0xf0},
	{0x0185, 0x01},
	{0x0115, 0x30},
	{0x011b, 0x12},
	{0x011c, 0x12},
	{0x0121, 0x01},
	{0x0122, 0x02},
	{0x0123, 0x07},
	{0x0124, 0x00},
	{0x0125, 0x07},
	{0x0126, 0x04},
	{0x0129, 0x02},
	{0x012a, 0x01},
	{0x012b, 0x04},
	{0x0a73, 0x60},
	{0x0a70, 0x11},
	{0x0313, 0x80},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0aff, 0x00},
	{0x0a70, 0x00},
	{0x00a4, 0x80},
	{0x0316, 0x01},
	{0x0a67, 0x00},
	{0x0084, 0x10},
	{0x0102, 0x09},
	{REG_NULL, 0x00},
};

static const struct gc08a3_mode supported_modes_4lane[] = {
	{
		.width = 3264,
		.height = 2448,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0900,
		.hts_def = 0x0568 * 4,
		.vts_def = 0x0a04,
		.reg_list = gc08a3_3264x2448_regs_4lane,
		.global_reg_list = gc08a3_global_regs_4lane,
		.mipi_freq_idx = 1,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 1280,
		.height = 800,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0900,
		.hts_def = 0x0568 * 4,
		.vts_def = 0x0a04,
		.reg_list = gc08a3_1280x800_regs_4lane,
		.global_reg_list = gc08a3_global_regs_4lane,
		.mipi_freq_idx = 0,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0900,
		.hts_def = 0x0568 * 4,
		.vts_def = 0x0a04,
		.reg_list = gc08a3_1280x720_regs_4lane,
		.global_reg_list = gc08a3_global_regs_4lane,
		.mipi_freq_idx = 0,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	GC08A3_MIPI_FREQ_150MHZ,
	GC08A3_MIPI_FREQ_350MHZ,
	GC08A3_MIPI_FREQ_700MHZ
};

static int gc08a3_write_reg(struct i2c_client *client, u16 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[3];
	int ret;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	buf[2] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"gc08a3 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int gc08a3_write_array(struct i2c_client *client,
	const struct regval *regs)
{
	u32 i = 0;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc08a3_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int gc08a3_read_reg(struct i2c_client *client, u16 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[2];
	int ret;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev,
		"gc08a3 read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc08a3_get_reso_dist(const struct gc08a3_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct gc08a3_mode *
gc08a3_find_best_fit(struct gc08a3 *gc08a3,
		     struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < gc08a3->cfg_num; i++) {
		dist = gc08a3_get_reso_dist(&gc08a3->support_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &gc08a3->support_modes[cur_best_fit];
}

static int gc08a3_set_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);
	const struct gc08a3_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc08a3->mutex);

	mode = gc08a3_find_best_fit(gc08a3, fmt);
	fmt->format.code = GC08A3_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc08a3->mutex);
		return -ENOTTY;
#endif
	} else {
		gc08a3->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc08a3->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc08a3->vblank, vblank_def,
					 GC08A3_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl(gc08a3->link_freq,
				   mode->mipi_freq_idx);
	}
	dev_info(&gc08a3->client->dev, "%s: mode->mipi_freq_idx(%d)",
		 __func__, mode->mipi_freq_idx);

	mutex_unlock(&gc08a3->mutex);

	return 0;
}

static int gc08a3_get_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);
	const struct gc08a3_mode *mode = gc08a3->cur_mode;

	mutex_lock(&gc08a3->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc08a3->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = GC08A3_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc08a3->mutex);

	return 0;
}

static int gc08a3_enum_mbus_code(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = GC08A3_MEDIA_BUS_FMT;

	return 0;
}

static int gc08a3_enum_frame_sizes(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);

	if (fse->index >= gc08a3->cfg_num)
		return -EINVAL;

	if (fse->code != GC08A3_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = gc08a3->support_modes[fse->index].width;
	fse->max_width  = gc08a3->support_modes[fse->index].width;
	fse->max_height = gc08a3->support_modes[fse->index].height;
	fse->min_height = gc08a3->support_modes[fse->index].height;

	return 0;
}

static int gc08a3_g_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_frame_interval *fi)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);
	const struct gc08a3_mode *mode = gc08a3->cur_mode;

	mutex_lock(&gc08a3->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc08a3->mutex);

	return 0;
}

static void gc08a3_get_module_inf(struct gc08a3 *gc08a3,
				struct rkmodule_inf *inf)
{
	strscpy(inf->base.sensor,
		GC08A3_NAME,
		sizeof(inf->base.sensor));
	strscpy(inf->base.module,
		gc08a3->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens,
		gc08a3->len_name,
		sizeof(inf->base.lens));
}

static void gc08a3_set_module_inf(struct gc08a3 *gc08a3,
				struct rkmodule_awb_cfg *cfg)
{
	mutex_lock(&gc08a3->mutex);
	memcpy(&gc08a3->awb_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&gc08a3->mutex);
}

static int gc08a3_get_channel_info(struct gc08a3 *gc08a3, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = gc08a3->cur_mode->vc[ch_info->index];
	ch_info->width = gc08a3->cur_mode->width;
	ch_info->height = gc08a3->cur_mode->height;
	ch_info->bus_fmt = GC08A3_MEDIA_BUS_FMT;
	return 0;
}

static long gc08a3_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);
	long ret = 0;
	u32 stream = 0;
	struct rkmodule_channel_info *ch_info;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc08a3_get_module_inf(gc08a3, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_AWB_CFG:
		gc08a3_set_module_inf(gc08a3, (struct rkmodule_awb_cfg *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream) {
			ret |= gc08a3_write_reg(gc08a3->client,
						GC08A3_REG_CTRL_MODE,
						GC08A3_MODE_STREAMING);
		} else {
			ret |= gc08a3_write_reg(gc08a3->client,
						GC08A3_REG_CTRL_MODE,
						GC08A3_MODE_SW_STANDBY);
		}
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = gc08a3_get_channel_info(gc08a3, ch_info);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc08a3_compat_ioctl32(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret = 0;
	u32 stream = 0;
	struct rkmodule_channel_info *ch_info;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc08a3_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
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
			ret = gc08a3_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc08a3_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc08a3_ioctl(sd, cmd, ch_info);
		if (!ret) {
			ret = copy_to_user(up, ch_info, sizeof(*ch_info));
			if (ret)
				ret = -EFAULT;
		}
		kfree(ch_info);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}
#endif

static int __gc08a3_start_stream(struct gc08a3 *gc08a3)
{
	int ret;

	ret = gc08a3_write_array(gc08a3->client, gc08a3->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&gc08a3->mutex);
	ret = v4l2_ctrl_handler_setup(&gc08a3->ctrl_handler);
	mutex_lock(&gc08a3->mutex);
	ret |= gc08a3_write_reg(gc08a3->client,
		GC08A3_REG_CTRL_MODE,
		GC08A3_MODE_STREAMING);
	return ret;
}

static int __gc08a3_stop_stream(struct gc08a3 *gc08a3)
{
	int ret;

	ret = gc08a3_write_reg(gc08a3->client,
		GC08A3_REG_CTRL_MODE,
		GC08A3_MODE_SW_STANDBY);

	return ret;
}

static int gc08a3_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);
	struct i2c_client *client = gc08a3->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				gc08a3->cur_mode->width,
				gc08a3->cur_mode->height,
		DIV_ROUND_CLOSEST(gc08a3->cur_mode->max_fps.denominator,
		gc08a3->cur_mode->max_fps.numerator));

	mutex_lock(&gc08a3->mutex);
	on = !!on;
	if (on == gc08a3->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc08a3_start_stream(gc08a3);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc08a3_stop_stream(gc08a3);
		pm_runtime_put(&client->dev);
	}

	gc08a3->streaming = on;

unlock_and_return:
	mutex_unlock(&gc08a3->mutex);

	return ret;
}

static int gc08a3_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);
	struct i2c_client *client = gc08a3->client;
	int ret = 0;

	dev_info(&client->dev, "%s(%d) on(%d)\n", __func__, __LINE__, on);
	mutex_lock(&gc08a3->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc08a3->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc08a3_write_array(gc08a3->client, gc08a3->cur_mode->global_reg_list);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc08a3->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc08a3->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc08a3->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc08a3_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC08A3_XVCLK_FREQ / 1000 / 1000);
}

static int __gc08a3_power_on(struct gc08a3 *gc08a3)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc08a3->client->dev;

	if (!IS_ERR(gc08a3->power_gpio))
		gpiod_set_value_cansleep(gc08a3->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(gc08a3->pins_default)) {
		ret = pinctrl_select_state(gc08a3->pinctrl,
					   gc08a3->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc08a3->xvclk, GC08A3_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc08a3->xvclk) != GC08A3_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc08a3->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(gc08a3->reset_gpio))
		gpiod_set_value_cansleep(gc08a3->reset_gpio, 0);

	ret = regulator_bulk_enable(GC08A3_NUM_SUPPLIES, gc08a3->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	usleep_range(1000, 1100);
	if (!IS_ERR(gc08a3->reset_gpio))
		gpiod_set_value_cansleep(gc08a3->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(gc08a3->pwdn_gpio))
		gpiod_set_value_cansleep(gc08a3->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc08a3_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc08a3->xvclk);

	return ret;
}

static void __gc08a3_power_off(struct gc08a3 *gc08a3)
{
	int ret;

	if (!IS_ERR(gc08a3->pwdn_gpio))
		gpiod_set_value_cansleep(gc08a3->pwdn_gpio, 0);
	clk_disable_unprepare(gc08a3->xvclk);
	if (!IS_ERR(gc08a3->reset_gpio))
		gpiod_set_value_cansleep(gc08a3->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(gc08a3->pins_sleep)) {
		ret = pinctrl_select_state(gc08a3->pinctrl,
					   gc08a3->pins_sleep);
		if (ret < 0)
			dev_dbg(&gc08a3->client->dev, "could not set pins\n");
	}
	if (!IS_ERR(gc08a3->power_gpio))
		gpiod_set_value_cansleep(gc08a3->power_gpio, 0);

	regulator_bulk_disable(GC08A3_NUM_SUPPLIES, gc08a3->supplies);
}

static int gc08a3_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc08a3 *gc08a3 = to_gc08a3(sd);

	return __gc08a3_power_on(gc08a3);
}

static int gc08a3_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc08a3 *gc08a3 = to_gc08a3(sd);

	__gc08a3_power_off(gc08a3);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc08a3_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);
	struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc08a3_mode *def_mode = &gc08a3->support_modes[0];

	mutex_lock(&gc08a3->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = GC08A3_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc08a3->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc08a3_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc08a3 *gc08a3 = to_gc08a3(sd);

	if (fie->index >= gc08a3->cfg_num)
		return -EINVAL;

	if (fie->code != GC08A3_MEDIA_BUS_FMT)
		return -EINVAL;

	fie->width = gc08a3->support_modes[fie->index].width;
	fie->height = gc08a3->support_modes[fie->index].height;
	fie->interval = gc08a3->support_modes[fie->index].max_fps;
	return 0;
}

static int gc08a3_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct gc08a3 *sensor = to_gc08a3(sd);
	struct device *dev = &sensor->client->dev;

	dev_info(dev, "%s(%d) enter!\n", __func__, __LINE__);

	if (2 == sensor->lane_num) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_2_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else if (4 == sensor->lane_num) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_4_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else {
		dev_err(&sensor->client->dev,
			"unsupported lane_num(%d)\n", sensor->lane_num);
	}
	return 0;
}

static const struct dev_pm_ops gc08a3_pm_ops = {
	SET_RUNTIME_PM_OPS(gc08a3_runtime_suspend,
			gc08a3_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc08a3_internal_ops = {
	.open = gc08a3_open,
};
#endif

static const struct v4l2_subdev_core_ops gc08a3_core_ops = {
	.s_power = gc08a3_s_power,
	.ioctl = gc08a3_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc08a3_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc08a3_video_ops = {
	.s_stream = gc08a3_s_stream,
	.g_frame_interval = gc08a3_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc08a3_pad_ops = {
	.enum_mbus_code = gc08a3_enum_mbus_code,
	.enum_frame_size = gc08a3_enum_frame_sizes,
	.enum_frame_interval = gc08a3_enum_frame_interval,
	.get_fmt = gc08a3_get_fmt,
	.set_fmt = gc08a3_set_fmt,
	.get_mbus_config = gc08a3_g_mbus_config,
};

static const struct v4l2_subdev_ops gc08a3_subdev_ops = {
	.core	= &gc08a3_core_ops,
	.video	= &gc08a3_video_ops,
	.pad	= &gc08a3_pad_ops,
};

static int gc08a3_set_exposure_reg(struct gc08a3 *gc08a3, u32 exposure)
{
	int ret = 0;
	u32 cal_shutter = 0;

	cal_shutter = exposure >> 1;
	cal_shutter = cal_shutter << 1;

	ret |= gc08a3_write_reg(gc08a3->client,
		GC08A3_REG_EXPOSURE_H,
		GC08A3_FETCH_HIGH_BYTE(cal_shutter));
	ret |= gc08a3_write_reg(gc08a3->client,
		GC08A3_REG_EXPOSURE_L,
		GC08A3_FETCH_LOW_BYTE(cal_shutter));
	return ret;
}

static int gc08a3_set_gain_reg(struct gc08a3 *gc08a3, u32 a_gain)
{
	int ret = 0;
	u32 temp_gain;

	if (a_gain < GC08A3_AGAIN_MIN)
		temp_gain = GC08A3_AGAIN_MIN;
	else if (a_gain > GC08A3_AGAIN_MAX)
		temp_gain = GC08A3_AGAIN_MAX;
	else
		temp_gain = a_gain;

	ret |= gc08a3_write_reg(gc08a3->client,
		GC08A3_REG_GAIN_H,
		GC08A3_FETCH_HIGH_BYTE(temp_gain));
	/* gain effect when 0x0205 is written */
	ret |= gc08a3_write_reg(gc08a3->client,
		GC08A3_REG_GAIN_L,
		GC08A3_FETCH_LOW_BYTE(temp_gain));

	return ret;
}

static int gc08a3_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc08a3 *gc08a3 = container_of(ctrl->handler,
					struct gc08a3, ctrl_handler);
	struct i2c_client *client = gc08a3->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc08a3->cur_mode->height + ctrl->val - 16;
		__v4l2_ctrl_modify_range(gc08a3->exposure,
					 gc08a3->exposure->minimum, max,
					 gc08a3->exposure->step,
					 gc08a3->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		dev_info(&client->dev, "set exposure value 0x%x\n", ctrl->val);
		ret = gc08a3_set_exposure_reg(gc08a3, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_info(&client->dev, "set analog gain value 0x%x\n", ctrl->val);
		ret = gc08a3_set_gain_reg(gc08a3, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_info(&client->dev, "set vb value 0x%x\n", ctrl->val);
		ret = gc08a3_write_reg(gc08a3->client,
					GC08A3_REG_VTS_H,
					(ctrl->val + gc08a3->cur_mode->height)
					>> 8);
		ret |= gc08a3_write_reg(gc08a3->client,
					GC08A3_REG_VTS_L,
					(ctrl->val + gc08a3->cur_mode->height)
					& 0xff);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc08a3_ctrl_ops = {
	.s_ctrl = gc08a3_set_ctrl,
};

static int gc08a3_initialize_controls(struct gc08a3 *gc08a3)
{
	const struct gc08a3_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc08a3->ctrl_handler;
	mode = gc08a3->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc08a3->mutex;

	gc08a3->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ, 2, 0,
				link_freq_menu_items);

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			0, gc08a3->pixel_rate, 1, gc08a3->pixel_rate);

	__v4l2_ctrl_s_ctrl(gc08a3->link_freq,
			   mode->mipi_freq_idx);

	h_blank = mode->hts_def - mode->width;
	gc08a3->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (gc08a3->hblank)
		gc08a3->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc08a3->vblank = v4l2_ctrl_new_std(handler, &gc08a3_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				GC08A3_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc08a3->exposure = v4l2_ctrl_new_std(handler, &gc08a3_ctrl_ops,
				V4L2_CID_EXPOSURE, GC08A3_EXPOSURE_MIN,
				exposure_max, GC08A3_EXPOSURE_STEP,
				mode->exp_def);

	gc08a3->anal_gain = v4l2_ctrl_new_std(handler, &gc08a3_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, GC08A3_AGAIN_MIN,
				GC08A3_AGAIN_MAX, GC08A3_AGAIN_STEP,
				GC08A3_AGAIN_DEFAULT);

	if (handler->error) {
		ret = handler->error;
		dev_err(&gc08a3->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc08a3->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc08a3_check_sensor_id(struct gc08a3 *gc08a3,
				struct i2c_client *client)
{
	struct device *dev = &gc08a3->client->dev;
	u32 id = 0;
	u8 reg_H = 0;
	u8 reg_L = 0;
	int ret;

	ret = gc08a3_read_reg(client, GC08A3_REG_CHIP_ID_H, &reg_H);
	ret |= gc08a3_read_reg(client, GC08A3_REG_CHIP_ID_L, &reg_L);
	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "detected gc%04x sensor\n", id);
	return ret;
}

static int gc08a3_configure_regulators(struct gc08a3 *gc08a3)
{
	unsigned int i;

	for (i = 0; i < GC08A3_NUM_SUPPLIES; i++)
		gc08a3->supplies[i].supply = gc08a3_supply_names[i];

	return devm_regulator_bulk_get(&gc08a3->client->dev,
		GC08A3_NUM_SUPPLIES,
		gc08a3->supplies);
}

static int gc08a3_parse_of(struct gc08a3 *gc08a3)
{
	struct device *dev = &gc08a3->client->dev;
	struct device_node *endpoint;
	struct fwnode_handle *fwnode;
	int rval;
	unsigned int fps;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	fwnode = of_fwnode_handle(endpoint);
	rval = fwnode_property_read_u32_array(fwnode, "data-lanes", NULL, 0);
	if (rval <= 0) {
		dev_warn(dev, " Get mipi lane num failed!\n");
		return -1;
	}

	gc08a3->lane_num = rval;
	if (4 == gc08a3->lane_num) {
		gc08a3->cur_mode = &supported_modes_4lane[0];
		gc08a3->support_modes = supported_modes_4lane;
		gc08a3->cfg_num = ARRAY_SIZE(supported_modes_4lane);
		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		fps = DIV_ROUND_CLOSEST(gc08a3->cur_mode->max_fps.denominator,
					gc08a3->cur_mode->max_fps.numerator);
		gc08a3->pixel_rate = gc08a3->cur_mode->vts_def *
				     gc08a3->cur_mode->hts_def * fps;

		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
			 gc08a3->lane_num, gc08a3->pixel_rate);
	} else if (2 == gc08a3->lane_num) {
		/* TODO*/
		dev_err(dev, "unsupported lane_num(%d)\n", gc08a3->lane_num);
		return -1;
	}

	return 0;
}

static int gc08a3_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc08a3 *gc08a3;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc08a3 = devm_kzalloc(dev, sizeof(*gc08a3), GFP_KERNEL);
	if (!gc08a3)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
		&gc08a3->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
		&gc08a3->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
		&gc08a3->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
		&gc08a3->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	gc08a3->client = client;

	gc08a3->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc08a3->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc08a3->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(gc08a3->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");
	gc08a3->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc08a3->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc08a3->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc08a3->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc08a3_configure_regulators(gc08a3);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	ret = gc08a3_parse_of(gc08a3);
	if (ret != 0)
		return -EINVAL;

	gc08a3->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc08a3->pinctrl)) {
		gc08a3->pins_default =
			pinctrl_lookup_state(gc08a3->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc08a3->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc08a3->pins_sleep =
			pinctrl_lookup_state(gc08a3->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc08a3->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&gc08a3->mutex);

	sd = &gc08a3->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc08a3_subdev_ops);
	ret = gc08a3_initialize_controls(gc08a3);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc08a3_power_on(gc08a3);
	if (ret)
		goto err_free_handler;

	ret = gc08a3_check_sensor_id(gc08a3, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc08a3_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc08a3->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc08a3->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc08a3->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc08a3->module_index, facing,
		 GC08A3_NAME, dev_name(sd->dev));
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
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__gc08a3_power_off(gc08a3);
err_free_handler:
	v4l2_ctrl_handler_free(&gc08a3->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc08a3->mutex);

	return ret;
}

static int gc08a3_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc08a3 *gc08a3 = to_gc08a3(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc08a3->ctrl_handler);
	mutex_destroy(&gc08a3->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc08a3_power_off(gc08a3);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc08a3_of_match[] = {
	{ .compatible = "galaxycore,gc08a3" },
	{},
};
MODULE_DEVICE_TABLE(of, gc08a3_of_match);
#endif

static const struct i2c_device_id gc08a3_match_id[] = {
	{ "galaxycore,gc08a3", 0},
	{ },
};

static struct i2c_driver gc08a3_i2c_driver = {
	.driver = {
		.name = GC08A3_NAME,
		.pm = &gc08a3_pm_ops,
		.of_match_table = of_match_ptr(gc08a3_of_match),
	},
	.probe		= &gc08a3_probe,
	.remove		= &gc08a3_remove,
	.id_table	= gc08a3_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc08a3_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc08a3_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GalaxyCore gc08a3 sensor driver");
MODULE_LICENSE("GPL v2");
