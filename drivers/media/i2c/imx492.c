// SPDX-License-Identifier: GPL-2.0
/*
 * IMX492 driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version
 * V0.0X01.0X01 add conversion gain control
 * V0.0X01.0X02 add debug interface for conversion gain control
 * V0.0X01.0X03 support enum sensor fmt
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>
#include <linux/of_graph.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x03)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_864M			864000000


#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define IMX492_10BIT_PIXEL_RATE	(MIPI_FREQ_864M * 2 / 10 * 4)


#define CHIP_ID				0xE6
#define IMX492_REG_CHIP_ID		0x3084

#define IMX492_REG_CTRL_STANDBY		0x3000
#define IMX492_REG_CTRL_CLKEN		0x35e5
#define IMX492_REG_CTRL_XMSTA		0x3033
#define IMX492_REG_CTRL_SYNCDRV		0x3017

#define IMX492_AGAIN_REG_L			0x300a
#define IMX492_AGAIN_REG_H			0x300b
#define IMX492_DGAIN_REG			0x3012
#define IMX492_GAIN_MIN				0x600
#define IMX492_GAIN_MAX				0x1e94
#define IMX492_GAIN_STEP			1
#define IMX492_GAIN_DEFAULT			0x600
#define IMX492_FETCH_GAIN_H(VAL)	(((VAL) >> 8) & 0x07)
#define IMX492_FETCH_GAIN_L(VAL)	((VAL) & 0xFF)

#define IMX492_VTS_REG_L			0x30a9
#define IMX492_VTS_REG_M			0x30aA
#define IMX492_VTS_REG_H			0x30aB
#define IMX492_VTS_MAX				0x7fff
#define IMX492_FETCH_VTS_H(VAL)		(((VAL) >> 16) & 0x07)
#define IMX492_FETCH_VTS_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX492_FETCH_VTS_L(VAL)		((VAL) & 0xFF)


#define IMX492_EXPO_REG_L			0x302c
#define IMX492_EXPO_REG_H			0x302d
#define IMX492_EXPO_SVR_L			0X300e
#define IMX492_EXPO_SVR_H			0X300f
#define	IMX492_EXPOSURE_MIN			12
#define	IMX492_EXPOSURE_STEP		1
#define IMX492_FETCH_EXP_L(VAL)		((VAL) & 0xFF)
#define IMX492_FETCH_EXP_H(VAL)		(((VAL) >> 8) & 0xFF)


#define IMX492_GROUP_HOLD_REG		0x3001
#define IMX492_GROUP_HOLD_START		0x01
#define IMX492_GROUP_HOLD_END		0x00

#define REG_NULL			0xFFFF
#define DELAY_MS			0xEEEE

#define IMX492_REG_VALUE_08BIT		1
#define IMX492_REG_VALUE_16BIT		2
#define IMX492_REG_VALUE_24BIT		3

#define IMX492_VREVERSE_REG	0x304f
#define IMX492_HREVERSE_REG	0x304e

#define USED_SYS_DEBUG

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define IMX492_NAME			"imx492"

static const char * const imx492_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define IMX492_NUM_SUPPLIES ARRAY_SIZE(imx492_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct imx492_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 mipi_freq_idx;
	u32 mclk;
	u32 bpp;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct imx492 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX492_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_a_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	struct v4l2_fwnode_endpoint bus_cfg;
	bool			streaming;
	bool			power_on;
	bool			has_init_exp;
	const struct imx492_mode *support_modes;
	const struct imx492_mode *cur_mode;
	u32			module_index;
	u32			cfg_num;
	u32			cur_vts;
	u32			cur_mclk;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	enum rkmodule_sync_mode	sync_mode;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	bool			isHCG;
};

#define to_IMX492(sd) container_of(sd, struct imx492, subdev)

static const struct regval imx492_linear_12bit_8192x4320_4lane_mode1_regs[] = {
	{0x3033, 0x30},
	{0x303C, 0x01},	//SYS_MODE[1:0]
	{0x31E8, 0x20},	//PLRD1
	{0x31E9, 0x01},
	{0x3122, 0x02},	//PLRD2
	{0x3129, 0x90},	//PLRD3
	{0x312A, 0x02},	//PLRD4
	{0x311F, 0x00},	//PLRD10
	{0x3123, 0x00},	//PLRD11
	{0x3124, 0x00},	//PLRD12
	{0x3125, 0x01},	//PLRD13
	{0x3127, 0x02},	//PLRD14
	{0x312D, 0x02},	//PLRD15
	{0x3000, 0x12},
	{0x310b, 0x00},
	{0x3004, 0x1C},
	{0x3005, 0x06},
	{0x3006, 0x00},
	{0x3007, 0xA7},
	{0x300A, 0xff},	//Again
	{0x300B, 0x00},
	{0x300E, 0x00},	//SVR
	{0x300F, 0x00},
	{0x3012, 0x03},	//Dgain
	{0x3017, 0xab},
	{0x302C, 0x0F},	//SHR
	{0x302D, 0x00},	//SHR
	{0x3042, 0x32},
	{0x3043, 0x00},
	{0x3047, 0x02},
	{0x304E, 0x0B},
	{0x304F, 0x2A},
	{0x3052, 0xEE},
	{0x3062, 0x25},
	{0x3064, 0x78},
	{0x3065, 0x33},
	{0x3066, 0x64},
	{0x3067, 0x71},
	{0x3081, 0x00},
	{0x3084, 0x00},
	{0x3085, 0x00},
	{0x3086, 0x00},
	{0x3087, 0x00},
	{0x3088, 0x75},
	{0x308A, 0x09},
	{0x308C, 0x61},
	{0x30A9, 0x4c},	//VMA
	{0x30AA, 0x11},
	{0x30AB, 0x00},
	{0x30AC, 0xb2},	//HMA
	{0x30AD, 0x04},
	{0x30E5, 0x00},
	{0x30EF, 0x01},
	{0x312F, 0x20},
	{0x3130, 0x1C},
	{0x3131, 0x11},
	{0x3132, 0xFC},
	{0x3133, 0x10},
	{0x3134, 0xAF},
	{0x3136, 0xC7},
	{0x3138, 0x7F},
	{0x313A, 0x6F},
	{0x313C, 0x6F},
	{0x313E, 0xCF},
	{0x3140, 0x77},
	{0x3142, 0x5F},
	{0x3146, 0x00},
	{0x31F5, 0x01},
	{0x3234, 0x32},
	{0x3248, 0xBC},
	{0x3250, 0xBC},
	{0x3258, 0xBC},
	{0x3260, 0xBC},
	{0x3274, 0x13},
	{0x3276, 0x00},
	{0x3277, 0x00},
	{0x327C, 0x13},
	{0x327E, 0x00},
	{0x327F, 0x00},
	{0x3284, 0x13},
	{0x3286, 0x00},
	{0x3287, 0x00},
	{0x328C, 0x13},
	{0x328E, 0x00},
	{0x328F, 0x00},
	{0x32AE, 0x00},
	{0x32AF, 0x00},
	{0x32CA, 0x5A},
	{0x332C, 0x00},
	{0x332D, 0x00},
	{0x332F, 0x00},
	{0x334A, 0x00},
	{0x334B, 0x00},
	{0x334C, 0x01},
	{0x3352, 0x50},
	{0x3356, 0x4F},
	{0x335A, 0x79},
	{0x335E, 0x56},
	{0x3360, 0x6A},
	{0x336A, 0x56},
	{0x33D6, 0x79},
	{0x340C, 0x6E},
	{0x3448, 0x7E},
	{0x348E, 0x6F},
	{0x3492, 0x11},
	{0x34C4, 0x5A},
	{0x3506, 0x56},
	{0x350C, 0x56},
	{0x350E, 0x58},
	{0x353D, 0x10},
	{0x3549, 0x04},
	{0x355D, 0x03},
	{0x355E, 0x03},
	{0x3574, 0x56},
	{0x357F, 0x0C},
	{0x3580, 0x0A},
	{0x3581, 0x08},
	{0x3583, 0x72},
	{0x3587, 0x01},
	{0x35D0, 0x5E},
	{0x35D4, 0x63},
	{0x35E5, 0x9A},
	{0x366A, 0x04},
	{0x366B, 0x04},
	{0x366C, 0x00},
	{0x366D, 0x00},
	{0x366E, 0x00},
	{0x366F, 0x00},
	{0x3670, 0x00},
	{0x3671, 0x05},
	{0x3676, 0x83},
	{0x3677, 0x03},
	{0x3678, 0x00},
	{0x3679, 0x04},
	{0x367A, 0x2C},
	{0x367B, 0x05},
	{0x367D, 0x06},
	{0x367E, 0xFF},
	{0x367F, 0x06},
	{0x3680, 0x4B},
	{0x3688, 0x05},
	{0x3690, 0x27},
	{0x3692, 0x65},
	{0x3694, 0x4F},
	{0x3696, 0xA1},
	{0x371C, 0x02},
	{0x372F, 0x3C},
	{0x3730, 0x01},
	{0x3732, 0xB8},
	{0x3734, 0x4A},
	{0x3736, 0x57},
	{0x3738, 0x4D},
	{0x3744, 0x0F},
	{0x375B, 0x01},
	{0x382B, 0x68},
	{0x38B3, 0x00},
	{0x3A43, 0x00},
	{0x3A54, 0xF0},
	{0x3A55, 0x20},
	{0x3AC4, 0x00},
	{0x3C08, 0x3F},
	{0x3C0C, 0x1B},
	{0x3E80, 0x14},
	{0x3E82, 0x30},
	{0x3E84, 0x0C},
	{0x3E85, 0x06},
	{0x3E86, 0xFC},
	{0x3E87, 0x10},
	{0x3E88, 0x03},
	{0x3E89, 0xFE},
	{0x3E8A, 0x01},
	{0x3E8B, 0x06},
	{0x3E8E, 0x03},
	{0x3E8F, 0xFE},
	{0x3E90, 0x01},
	{0x3E91, 0x06},
	{0x3E94, 0x33},
	{0x3E95, 0x01},
	{0x3E96, 0x19},
	{0x3E98, 0x30},
	{0x3E9A, 0x11},
	{0x3E9B, 0x06},
	{0x3E9C, 0xFC},
	{0x3E9D, 0x10},
	{0x3E9E, 0xFE},
	{0x3E9F, 0x03},
	{0x3EA0, 0x06},
	{0x3EA3, 0x01},
	{0x3EA4, 0xFE},
	{0x3EA5, 0x03},
	{0x3EA6, 0x06},
	{0x3EA9, 0x33},
	{0x3EAA, 0x00},
	{0x3EAB, 0x08},
	{0x3EAC, 0x08},
	{0x3EAD, 0x01},
	{0x3EAE, 0x08},
	{0x3EAF, 0x08},
	{0x3EB0, 0x00},
	{0x3EB1, 0x10},
	{0x3EB2, 0x10},
	{0x3EB3, 0x01},
	{0x3EB4, 0x10},
	{0x3EB5, 0x10},
	{0x3EB6, 0x00},
	{0x3EB7, 0x00},
	{0x3EB8, 0x00},
	{0x3EB9, 0x00},
	{0x3EBA, 0x00},
	{0x3EBB, 0x00},
	{0x3EC0, 0x54},
	{0x3ECC, 0x04},
	{0x3ECD, 0x04},
	{0x3ED0, 0xF0},
	{0x3ED1, 0x20},
	{0x3ED2, 0x0B},
	{0x3ED3, 0x04},
	{0x3ED5, 0x13},
	{0x3ED6, 0x00},
	{0x3ED9, 0x0F},
	{0x3EE4, 0x02},
	{0x3EE5, 0x02},
	{0x3EE7, 0x00},
	{0x3EF6, 0x00},
	{0x3EF8, 0x10},
	{0x3EFA, 0x00},
	{0x3EFC, 0x10},
	{DELAY_MS, 20},
	{0x3000, 0x02},
	{0x35E5, 0x92},
	{0x35E5, 0x9a},
	{REG_NULL, 0x00},
};

static const struct regval imx492_linear_10bit_8192x4320_4lane_mode2_regs[] = {
	{0x3033, 0x30},
	{0x303C, 0x01},		//SYS_MODE[1:0]
	{0x31E8, 0x20},	//PLRD1
	{0x31E9, 0x01},
	{0x3122, 0x02},	//PLRD2
	{0x3129, 0x90},	//PLRD3
	{0x312A, 0x02},	//PLRD4
	{0x311F, 0x00},	//PLRD10
	{0x3123, 0x00},	//PLRD11
	{0x3124, 0x00},	//PLRD12
	{0x3125, 0x01},	//PLRD13
	{0x3127, 0x02},	//PLRD14
	{0x312D, 0x02},	//PLRD15
	{0x3000, 0x12},
	{0x310b, 0x00},
	{0x3004, 0x1C},
	{0x3005, 0x01},
	{0x3006, 0x00},
	{0x3007, 0xA7},
	{0x300A, 0xfa},	//Again
	{0x300B, 0x00},
	{0x300E, 0x00},	//SVR
	{0x300F, 0x00},
	{0x3012, 0x03},	//Dgain
	{0x3017, 0xab},
	{0x302C, 0x0F},	//SHR
	{0x302D, 0x00},	//SHR
	{0x3042, 0x32},
	{0x3043, 0x00},
	{0x3047, 0x01},
	{0x304E, 0x0B},
	{0x304F, 0x24},
	{0x3062, 0x25},
	{0x3064, 0x78},
	{0x3065, 0x33},
	{0x3067, 0x71},
	{0x3068, 0x44},
	{0x3081, 0x00},
	{0x3084, 0x00},
	{0x3085, 0x00},
	{0x3086, 0x00},
	{0x3087, 0x00},
	{0x3088, 0x75},
	{0x308A, 0x09},
	{0x308C, 0x61},
	{0x30A9, 0x4c},	//VMAX
	{0x30AA, 0x11},
	{0x30AB, 0x00},
	{0x30AC, 0x98},	//HMAX
	{0x30AD, 0x03},
	{0x30E5, 0x00},
	{0x30EF, 0x01},
	{0x312F, 0x20},
	{0x3130, 0x1C},
	{0x3131, 0x11},
	{0x3132, 0xFC},
	{0x3133, 0x10},
	{0x3134, 0xAF},
	{0x3136, 0xC7},
	{0x3138, 0x7F},
	{0x313A, 0x6F},
	{0x313C, 0x6F},
	{0x313E, 0xCF},
	{0x3140, 0x77},
	{0x3142, 0x5F},
	{0x3146, 0x00},
	{0x31F5, 0x01},
	{0x3234, 0x32},
	{0x3248, 0xBC},
	{0x3250, 0xBC},
	{0x3258, 0xBC},
	{0x3260, 0xBC},
	{0x3274, 0x13},
	{0x3276, 0x00},
	{0x3277, 0x00},
	{0x327C, 0x13},
	{0x327E, 0x00},
	{0x327F, 0x00},
	{0x3284, 0x13},
	{0x3286, 0x00},
	{0x3287, 0x00},
	{0x328C, 0x13},
	{0x328E, 0x00},
	{0x328F, 0x00},
	{0x32AE, 0x00},
	{0x32AF, 0x00},
	{0x32CA, 0x5A},
	{0x332C, 0x00},	//PSSLVS1
	{0x332D, 0x00},
	{0x332F, 0x00},
	{0x334A, 0x00},	//PSSLVS2
	{0x334B, 0x00},
	{0x334C, 0x01},
	{0x335A, 0x79},
	{0x335E, 0x56},
	{0x3360, 0x6A},
	{0x336A, 0x56},
	{0x33D6, 0x79},
	{0x340C, 0x6E},
	{0x3448, 0x7E},
	{0x348E, 0x6F},
	{0x3492, 0x11},
	{0x34C4, 0x5A},
	{0x3506, 0x56},
	{0x350C, 0x56},
	{0x350E, 0x58},
	{0x353D, 0x10},
	{0x3549, 0x04},
	{0x355D, 0x03},
	{0x355E, 0x03},
	{0x3574, 0x56},
	{0x357F, 0x0C},
	{0x3580, 0x0A},
	{0x3581, 0x0A},
	{0x3583, 0x75},
	{0x3587, 0x01},
	{0x35D0, 0x5E},
	{0x35D4, 0x63},
	{0x35E5, 0x9A},
	{0x366A, 0x1A},
	{0x366B, 0x16},
	{0x366C, 0x10},
	{0x366D, 0x09},
	{0x366E, 0x00},
	{0x366F, 0x00},
	{0x3670, 0x00},
	{0x3671, 0x00},
	{0x3676, 0x83},
	{0x3677, 0x03},
	{0x3678, 0x00},
	{0x3679, 0x04},
	{0x367A, 0x2C},
	{0x367B, 0x05},
	{0x367D, 0x06},
	{0x367E, 0x00},
	{0x3680, 0x4B},
	{0x3690, 0x27},
	{0x3692, 0x65},
	{0x3694, 0x4F},
	{0x3696, 0xA1},
	{0x36BC, 0x00},	//PSSLVS0
	{0x36BD, 0x00},
	{0x371C, 0x02},
	{0x372F, 0x3C},
	{0x3730, 0x01},
	{0x3732, 0xB8},
	{0x3744, 0x0F},
	{0x375B, 0x01},
	{0x382B, 0x68},
	{0x38B3, 0x00},
	{0x3A43, 0x00},
	{0x3A54, 0xF0},
	{0x3A55, 0x20},
	{0x3AC4, 0x00},
	{0x3C00, 0x01},
	{0x3C01, 0x01},
	{0x3E80, 0x14},
	{0x3E82, 0x30},
	{0x3E84, 0x0C},
	{0x3E85, 0x06},
	{0x3E86, 0xFC},
	{0x3E87, 0x10},
	{0x3E88, 0x03},
	{0x3E89, 0xFE},
	{0x3E8A, 0x01},
	{0x3E8B, 0x06},
	{0x3E8E, 0x03},
	{0x3E8F, 0xFE},
	{0x3E90, 0x01},
	{0x3E91, 0x06},
	{0x3E94, 0x33},
	{0x3E95, 0x01},
	{0x3E96, 0x19},
	{0x3E98, 0x30},
	{0x3E9A, 0x11},
	{0x3E9B, 0x06},
	{0x3E9C, 0xFC},
	{0x3E9D, 0x10},
	{0x3E9E, 0xFE},
	{0x3E9F, 0x03},
	{0x3EA0, 0x06},
	{0x3EA3, 0x01},
	{0x3EA4, 0xFE},
	{0x3EA5, 0x03},
	{0x3EA6, 0x06},
	{0x3EA9, 0x33},
	{0x3EAA, 0x00},
	{0x3EAB, 0x08},
	{0x3EAC, 0x08},
	{0x3EAD, 0x01},
	{0x3EAE, 0x08},
	{0x3EAF, 0x08},
	{0x3EB0, 0x00},
	{0x3EB1, 0x10},
	{0x3EB2, 0x10},
	{0x3EB3, 0x01},
	{0x3EB4, 0x10},
	{0x3EB5, 0x10},
	{0x3EB6, 0x00},
	{0x3EB7, 0x00},
	{0x3EB8, 0x00},
	{0x3EB9, 0x00},
	{0x3EBA, 0x00},
	{0x3EBB, 0x00},
	{0x3EC0, 0x54},
	{0x3ECC, 0x04},
	{0x3ECD, 0x04},
	{0x3ED0, 0xF0},
	{0x3ED1, 0x20},
	{0x3ED2, 0x0B},
	{0x3ED3, 0x04},
	{0x3ED5, 0x13},
	{0x3ED6, 0x00},
	{0x3ED9, 0x0F},
	{0x3EE4, 0x02},
	{0x3EE5, 0x02},
	{0x3EE7, 0x00},
	{0x3EF6, 0x00},
	{0x3EF8, 0x10},
	{0x3EFA, 0x00},
	{0x3EFC, 0x10},
	{DELAY_MS, 20},
	{0x3000, 0x02},
	{0x35E5, 0x92},
	{0x35E5, 0x9a},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval imx492_pllsetting_regs[] = {
	{0x31E8, 0x20},	//PLRD1
	{0x31E9, 0x01},
	{0x3122, 0x02},	//PLRD2
	{0x3129, 0x90},	//PLRD3
	{0x312A, 0x02},	//PLRD4
	{0x311F, 0x00},	//PLRD10
	{0x3123, 0x00},	//PLRD11
	{0x3124, 0x00},	//PLRD12
	{0x3125, 0x01},	//PLRD13
	{0x3127, 0x02},	//PLRD14
	{0x312D, 0x02},	//PLRD15
	{REG_NULL, 0x00},
};
/*
 * The width and height must be configured to be
 * the same as the current output resolution of the sensor.
 * The input width of the isp needs to be 16 aligned.
 * The input height of the isp needs to be 8 aligned.
 * If the width or height does not meet the alignment rules,
 * you can configure the cropping parameters with the following function to
 * crop out the appropriate resolution.
 * struct v4l2_subdev_pad_ops {
 *	.get_selection
 * }
 */
static const struct imx492_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB12_1X12,
		.width = 8360,
		.height = 4320,
		.max_fps = {
			.numerator = 10000,
			.denominator = 135200,
		},
		.exp_def = 0x0906,
		.hts_def = 0x04b2 * 7,
		.vts_def = 0x114c,
		.mipi_freq_idx = 0,
		.bpp = 12,
		.mclk = 24000000,
		.reg_list = imx492_linear_12bit_8192x4320_4lane_mode1_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 8360,
		.height = 4320,
		.max_fps = {
			.numerator = 10000,
			.denominator = 176200,
		},
		.exp_def = 0x0906,
		.hts_def = 0x0398 * 9,
		.vts_def = 0x114c,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.mclk = 24000000,
		.reg_list = imx492_linear_10bit_8192x4320_4lane_mode2_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_864M
};

/* Write registers up to 4 at a time */
static int imx492_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int imx492_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i, delay_ms;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (regs[i].addr == DELAY_MS) {
			delay_ms = regs[i].val;
			dev_info(&client->dev, "delay(%d) ms !\n", delay_ms);
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
			continue;
		}
		ret = imx492_write_reg(client, regs[i].addr,
				       IMX492_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int imx492_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
			   u32 *val)
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
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int imx492_get_reso_dist(const struct imx492_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct imx492_mode *
imx492_find_best_fit(struct imx492 *imx492, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < imx492->cfg_num; i++) {
		dist = imx492_get_reso_dist(&imx492->support_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			imx492->support_modes[i].bus_fmt == framefmt->code) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &imx492->support_modes[cur_best_fit];
}

static int imx492_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx492 *imx492 = to_IMX492(sd);
	const struct imx492_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&imx492->mutex);

	mode = imx492_find_best_fit(imx492, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&imx492->mutex);
		return -ENOTTY;
#endif
	} else {
		imx492->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx492->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx492->vblank, vblank_def,
					 IMX492_VTS_MAX - mode->height, 1,
					 vblank_def);
		imx492->cur_vts = imx492->cur_mode->vts_def;
		pixel_rate = (u32)link_freq_menu_items[mode->mipi_freq_idx] /
				mode->bpp * 2 * 4;
		__v4l2_ctrl_s_ctrl_int64(imx492->pixel_rate, pixel_rate);
		__v4l2_ctrl_s_ctrl(imx492->link_freq, mode->mipi_freq_idx);
	}

	mutex_unlock(&imx492->mutex);

	return 0;
}

static int imx492_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx492 *imx492 = to_IMX492(sd);
	const struct imx492_mode *mode = imx492->cur_mode;

	mutex_lock(&imx492->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&imx492->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&imx492->mutex);

	return 0;
}

static int imx492_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx492 *imx492 = to_IMX492(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = imx492->cur_mode->bus_fmt;

	return 0;
}

static int imx492_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx492 *imx492 = to_IMX492(sd);

	if (fse->index >= imx492->cfg_num)
		return -EINVAL;

	if (fse->code != imx492->support_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = imx492->support_modes[fse->index].width;
	fse->max_width  = imx492->support_modes[fse->index].width;
	fse->max_height = imx492->support_modes[fse->index].height;
	fse->min_height = imx492->support_modes[fse->index].height;

	return 0;
}

static int imx492_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx492 *imx492 = to_IMX492(sd);
	const struct imx492_mode *mode = imx492->cur_mode;

	fi->interval = mode->max_fps;

	return 0;
}

static int imx492_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct imx492 *imx492 = to_IMX492(sd);
	const struct imx492_mode *mode = imx492->cur_mode;
	u32 val = 0;
	u32 lane_num = imx492->bus_cfg.bus.mipi_csi2.num_data_lanes;

	if (mode->hdr_mode == NO_HDR) {
		val = 1 << (lane_num - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	}
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (lane_num - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;
	if (mode->hdr_mode == HDR_X3)
		val = 1 << (lane_num - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1 |
		V4L2_MBUS_CSI2_CHANNEL_2;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void imx492_get_module_inf(struct imx492 *imx492,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, IMX492_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, imx492->module_name, sizeof(inf->base.module));
	strscpy(inf->base.lens, imx492->len_name, sizeof(inf->base.lens));
}


static int imx492_get_channel_info(struct imx492 *imx492,
				   struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = imx492->cur_mode->vc[ch_info->index];
	ch_info->width = imx492->cur_mode->width;
	ch_info->height = imx492->cur_mode->height;
	ch_info->bus_fmt = imx492->cur_mode->bus_fmt;
	return 0;
}

static long imx492_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx492 *imx492 = to_IMX492(sd);
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_channel_info *ch_info;
	u32 i, h, w, stream;
	long ret = 0;
	u64 pixel_rate = 0;
	u32 *sync_mode = NULL;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_GET_MODULE_INFO:
		imx492_get_module_inf(imx492, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = imx492->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = imx492->cur_mode->width;
		h = imx492->cur_mode->height;
		for (i = 0; i < imx492->cfg_num; i++) {
			if (w == imx492->support_modes[i].width &&
			    h == imx492->support_modes[i].height &&
			    imx492->support_modes[i].hdr_mode == hdr->hdr_mode) {
				imx492->cur_mode = &imx492->support_modes[i];
				break;
			}
		}
		if (i == imx492->cfg_num) {
			dev_err(&imx492->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = imx492->cur_mode->hts_def - imx492->cur_mode->width;
			h = imx492->cur_mode->vts_def - imx492->cur_mode->height;
			__v4l2_ctrl_modify_range(imx492->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(imx492->vblank, h,
				IMX492_VTS_MAX - imx492->cur_mode->height,
				1, h);
			imx492->cur_vts = imx492->cur_mode->vts_def;
			pixel_rate = (u32)link_freq_menu_items[imx492->cur_mode->mipi_freq_idx]
					/ imx492->cur_mode->bpp * 2 *
					imx492->bus_cfg.bus.mipi_csi2.num_data_lanes;
			__v4l2_ctrl_s_ctrl_int64(imx492->pixel_rate,
						 pixel_rate);
			__v4l2_ctrl_s_ctrl(imx492->link_freq,
					   imx492->cur_mode->mipi_freq_idx);
		}
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);

		if (stream) {
			ret |= imx492_write_reg(imx492->client, IMX492_REG_CTRL_STANDBY,
						IMX492_REG_VALUE_08BIT, 0x00);
		} else {
			ret |= imx492_write_reg(imx492->client, IMX492_REG_CTRL_STANDBY,
						IMX492_REG_VALUE_08BIT, 0x11);
		}
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = imx492_get_channel_info(imx492, ch_info);
		break;
	case RKMODULE_GET_SYNC_MODE:
		sync_mode = (u32 *)arg;
		*sync_mode = imx492->sync_mode;
		break;
	case RKMODULE_SET_SYNC_MODE:
		sync_mode = (u32 *)arg;
		imx492->sync_mode = *sync_mode;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx492_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	struct rkmodule_channel_info *ch_info;
	long ret;
	u32 cg = 0;
	u32  stream;
	u32 sync_mode;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx492_ioctl(sd, cmd, inf);
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
			ret = imx492_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx492_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				ret = -EFAULT;
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (!ret)
			ret = imx492_ioctl(sd, cmd, hdr);
		else
			ret = -EFAULT;
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdrae, up, sizeof(*hdrae));
		if (!ret)
			ret = imx492_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = copy_from_user(&cg, up, sizeof(cg));
		if (!ret)
			ret = imx492_ioctl(sd, cmd, &cg);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = imx492_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;

		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx492_ioctl(sd, cmd, ch_info);
		if (!ret) {
			ret = copy_to_user(up, ch_info, sizeof(*ch_info));
			if (ret)
				ret = -EFAULT;
		}
		kfree(ch_info);
		break;
	case RKMODULE_GET_SYNC_MODE:
		ret = imx492_ioctl(sd, cmd, &sync_mode);
		if (!ret) {
			ret = copy_to_user(up, &sync_mode, sizeof(u32));
			if (ret)
				ret = -EFAULT;
		}
		break;
	case RKMODULE_SET_SYNC_MODE:
		ret = copy_from_user(&sync_mode, up, sizeof(u32));
		if (!ret)
			ret = imx492_ioctl(sd, cmd, &sync_mode);
		else
			ret = -EFAULT;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif


static int __imx492_start_stream(struct imx492 *imx492)
{
	int ret;

	ret = imx492_write_array(imx492->client, imx492->cur_mode->reg_list);
	if (ret)
		return ret;
	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&imx492->ctrl_handler);
	if (ret)
		return ret;
	if (imx492->has_init_exp && imx492->cur_mode->hdr_mode != NO_HDR) {
		ret = imx492_ioctl(&imx492->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&imx492->init_hdrae_exp);
		if (ret) {
			dev_err(&imx492->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	ret |= imx492_write_reg(imx492->client, IMX492_REG_CTRL_STANDBY,
				IMX492_REG_VALUE_08BIT, 0x00);
	usleep_range(1000, 1100);
	ret |= imx492_write_reg(imx492->client, IMX492_REG_CTRL_XMSTA,
				IMX492_REG_VALUE_08BIT, 0x20);
	ret |= imx492_write_reg(imx492->client, IMX492_REG_CTRL_SYNCDRV,
				IMX492_REG_VALUE_08BIT, 0xa8);

	return ret;
}

static int __imx492_stop_stream(struct imx492 *imx492)
{
	int ret = 0;

	imx492->has_init_exp = false;
	ret = imx492_write_reg(imx492->client, IMX492_REG_CTRL_STANDBY,
				IMX492_REG_VALUE_08BIT, 0x11);
	return ret;
}

static int imx492_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx492 *imx492 = to_IMX492(sd);
	struct i2c_client *client = imx492->client;
	int ret = 0;

	mutex_lock(&imx492->mutex);
	on = !!on;
	if (on == imx492->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx492_start_stream(imx492);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx492_stop_stream(imx492);
		pm_runtime_put(&client->dev);
	}

	imx492->streaming = on;

unlock_and_return:
	mutex_unlock(&imx492->mutex);

	return ret;
}

static int imx492_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx492 *imx492 = to_IMX492(sd);
	struct i2c_client *client = imx492->client;
	int ret = 0;

	mutex_lock(&imx492->mutex);

	/* If the power state is not modified - no work to do. */
	if (imx492->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		imx492->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx492->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx492->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */

static int __imx492_power_on(struct imx492 *imx492)
{
	int ret;
	struct device *dev = &imx492->client->dev;

	if (!IS_ERR_OR_NULL(imx492->pins_default)) {
		ret = pinctrl_select_state(imx492->pinctrl,
					   imx492->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_set_rate(imx492->xvclk, imx492->cur_mode->mclk);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");
	if (clk_get_rate(imx492->xvclk) != imx492->cur_mode->mclk)
		dev_warn(dev, "xvclk mismatched, %lu\n", clk_get_rate(imx492->xvclk));
	else
		imx492->cur_mclk = imx492->cur_mode->mclk;
	ret = clk_prepare_enable(imx492->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(IMX492_NUM_SUPPLIES, imx492->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}
	if (!IS_ERR(imx492->pwdn_gpio))
		gpiod_set_value_cansleep(imx492->pwdn_gpio, 0);
	if (!IS_ERR(imx492->reset_gpio))
		gpiod_set_value_cansleep(imx492->reset_gpio, 0);

	usleep_range(3000, 3100);
	return 0;

disable_clk:
	clk_disable_unprepare(imx492->xvclk);

	return ret;
}

static void __imx492_power_off(struct imx492 *imx492)
{
	int ret;
	struct device *dev = &imx492->client->dev;

	if (!IS_ERR(imx492->pwdn_gpio))
		gpiod_set_value_cansleep(imx492->pwdn_gpio, 1);
	clk_disable_unprepare(imx492->xvclk);
	if (!IS_ERR(imx492->reset_gpio))
		gpiod_set_value_cansleep(imx492->reset_gpio, 1);
	if (!IS_ERR_OR_NULL(imx492->pins_sleep)) {
		ret = pinctrl_select_state(imx492->pinctrl, imx492->pins_sleep);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	regulator_bulk_disable(IMX492_NUM_SUPPLIES, imx492->supplies);
	usleep_range(15000, 16000);
}

static int imx492_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx492 *imx492 = to_IMX492(sd);

	return __imx492_power_on(imx492);
}

static int imx492_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx492 *imx492 = to_IMX492(sd);

	__imx492_power_off(imx492);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int imx492_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx492 *imx492 = to_IMX492(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct imx492_mode *def_mode = &imx492->support_modes[0];

	mutex_lock(&imx492->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx492->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int imx492_enum_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	struct imx492 *imx492 = to_IMX492(sd);

	if (fie->index >= imx492->cfg_num)
		return -EINVAL;

	fie->code = imx492->support_modes[fie->index].bus_fmt;
	fie->width = imx492->support_modes[fie->index].width;
	fie->height = imx492->support_modes[fie->index].height;
	fie->interval = imx492->support_modes[fie->index].max_fps;
	fie->reserved[0] = imx492->support_modes[fie->index].hdr_mode;
	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH_8192 8192
#define DST_HEIGHT_4320 4320
#define DST_WIDTH_3840 3840
#define DST_HEIGHT_2160 2160
#define DST_WIDTH_1920 1920
#define DST_HEIGHT_1080 1080
/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int imx492_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct imx492 *imx492 = to_IMX492(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 168;// CROP_START(imx492->cur_mode->width, DST_WIDTH_8192);
		sel->r.width = DST_WIDTH_8192;
		sel->r.top = CROP_START(imx492->cur_mode->height, DST_HEIGHT_4320);
		sel->r.height = DST_HEIGHT_4320;
		dev_info(&imx492->client->dev, "sel->r.left %d sel->r.top %d\n",
			 sel->r.left, sel->r.top);
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops imx492_pm_ops = {
	SET_RUNTIME_PM_OPS(imx492_runtime_suspend, imx492_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops imx492_internal_ops = {
	.open = imx492_open,
};
#endif

static const struct v4l2_subdev_core_ops imx492_core_ops = {
	.s_power = imx492_s_power,
	.ioctl = imx492_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx492_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx492_video_ops = {
	.s_stream = imx492_s_stream,
	.g_frame_interval = imx492_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx492_pad_ops = {
	.enum_mbus_code = imx492_enum_mbus_code,
	.enum_frame_size = imx492_enum_frame_sizes,
	.enum_frame_interval = imx492_enum_frame_interval,
	.get_fmt = imx492_get_fmt,
	.set_fmt = imx492_set_fmt,
	.get_selection = imx492_get_selection,
	.get_mbus_config = imx492_g_mbus_config,
};

static const struct v4l2_subdev_ops imx492_subdev_ops = {
	.core	= &imx492_core_ops,
	.video	= &imx492_video_ops,
	.pad	= &imx492_pad_ops,
};


static int imx492_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx492 *imx492 = container_of(ctrl->handler,
					     struct imx492, ctrl_handler);
	struct i2c_client *client = imx492->client;
	const struct imx492_mode *mode = imx492->cur_mode;
	s64 max;
	u32 vts = 0;
	int ret = 0;
	u16 reg_val = 0;
	u16 SHR = 12;


	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		if (mode->hdr_mode == NO_HDR) {
			max = imx492->cur_mode->height + ctrl->val - 3;
			__v4l2_ctrl_modify_range(imx492->exposure,
					 imx492->exposure->minimum, max,
					 imx492->exposure->step,
					 imx492->exposure->default_value);
		}
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (mode->hdr_mode == NO_HDR) {
			SHR = mode->vts_def - ctrl->val;
			ret |= imx492_write_reg(imx492->client, IMX492_EXPO_REG_L,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_EXP_L(SHR));
			ret |= imx492_write_reg(imx492->client, IMX492_EXPO_REG_H,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_EXP_H(SHR));
			dev_info(&client->dev, "ctr_val 0x%x set exposure 0x%x\n",
				 ctrl->val, SHR);
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (mode->hdr_mode == NO_HDR) {
			if (ctrl->val <= 1957) {
				reg_val = ctrl->val;
				ret |= imx492_write_reg(imx492->client, IMX492_AGAIN_REG_H,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_GAIN_H(reg_val));
				ret |= imx492_write_reg(imx492->client, IMX492_AGAIN_REG_L,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_GAIN_L(reg_val));
				ret |= imx492_write_reg(imx492->client, IMX492_DGAIN_REG,
						IMX492_REG_VALUE_08BIT, 0x0);
			} else if (ctrl->val <= 3914) {
				reg_val = ctrl->val - 1957;
				ret |= imx492_write_reg(imx492->client, IMX492_AGAIN_REG_H,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_GAIN_H(reg_val));
				ret |= imx492_write_reg(imx492->client, IMX492_AGAIN_REG_L,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_GAIN_L(reg_val));
				ret |= imx492_write_reg(imx492->client, IMX492_DGAIN_REG,
						IMX492_REG_VALUE_08BIT, 0x1);
			} else if (ctrl->val <= 5871) {
				reg_val = ctrl->val - 3914;
				ret |= imx492_write_reg(imx492->client, IMX492_AGAIN_REG_H,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_GAIN_H(reg_val));
				ret |= imx492_write_reg(imx492->client, IMX492_AGAIN_REG_L,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_GAIN_L(reg_val));
				ret |= imx492_write_reg(imx492->client, IMX492_DGAIN_REG,
						IMX492_REG_VALUE_08BIT, 0x2);
			} else if (ctrl->val <= 7828) {
				reg_val = ctrl->val - 5871;
				ret |= imx492_write_reg(imx492->client, IMX492_AGAIN_REG_H,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_GAIN_H(reg_val));
				ret |= imx492_write_reg(imx492->client, IMX492_AGAIN_REG_L,
						IMX492_REG_VALUE_08BIT,
						IMX492_FETCH_GAIN_L(reg_val));
				ret |= imx492_write_reg(imx492->client, IMX492_DGAIN_REG,
						IMX492_REG_VALUE_08BIT, 0x3);
			}
			dev_err(&client->dev, "set analog gain 0x%x\n",
				ctrl->val);
		}
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + imx492->cur_mode->height;

		imx492->cur_vts = vts;
		ret |= imx492_write_reg(imx492->client, IMX492_VTS_REG_L,
					IMX492_REG_VALUE_08BIT,
					IMX492_FETCH_VTS_L(vts));
		ret |= imx492_write_reg(imx492->client, IMX492_VTS_REG_M,
					IMX492_REG_VALUE_08BIT,
					IMX492_FETCH_VTS_M(vts));
		ret |= imx492_write_reg(imx492->client, IMX492_VTS_REG_H,
					IMX492_REG_VALUE_08BIT,
					IMX492_FETCH_VTS_H(vts));

		dev_info(&client->dev, "set vts 0x%x\n",
			vts);
		break;
	case V4L2_CID_HFLIP:
		break;
	case V4L2_CID_VFLIP:
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx492_ctrl_ops = {
	.s_ctrl = imx492_set_ctrl,
};

static int imx492_initialize_controls(struct imx492 *imx492)
{
	const struct imx492_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	u64 pixel_rate = 0;
	int ret;

	handler = &imx492->ctrl_handler;
	mode = imx492->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &imx492->mutex;

	imx492->link_freq = v4l2_ctrl_new_int_menu(handler,
						NULL, V4L2_CID_LINK_FREQ,
						1, 0, link_freq_menu_items);
	__v4l2_ctrl_s_ctrl(imx492->link_freq, imx492->cur_mode->mipi_freq_idx);
	pixel_rate = (u32)link_freq_menu_items[mode->mipi_freq_idx] / mode->bpp
			* 2 * imx492->bus_cfg.bus.mipi_csi2.num_data_lanes;
	imx492->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
					0, IMX492_10BIT_PIXEL_RATE,
					1, pixel_rate);

	h_blank = mode->hts_def - mode->width;
	imx492->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (imx492->hblank)
		imx492->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx492->vblank = v4l2_ctrl_new_std(handler, &imx492_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   IMX492_VTS_MAX - mode->height,
					   1, vblank_def);
	imx492->cur_vts = mode->vts_def;

	exposure_max = mode->vts_def - 4;
	imx492->exposure = v4l2_ctrl_new_std(handler, &imx492_ctrl_ops,
					V4L2_CID_EXPOSURE, IMX492_EXPOSURE_MIN,
					exposure_max, IMX492_EXPOSURE_STEP,
					mode->exp_def);

	imx492->anal_a_gain = v4l2_ctrl_new_std(handler, &imx492_ctrl_ops,
					V4L2_CID_ANALOGUE_GAIN, IMX492_GAIN_MIN,
					IMX492_GAIN_MAX, IMX492_GAIN_STEP,
					IMX492_GAIN_DEFAULT);
	v4l2_ctrl_new_std(handler, &imx492_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &imx492_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx492->client->dev, "Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	imx492->subdev.ctrl_handler = handler;
	imx492->has_init_exp = false;
	imx492->isHCG = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx492_check_sensor_id(struct imx492 *imx492,
				  struct i2c_client *client)
{
	struct device *dev = &imx492->client->dev;
	u32 id = 0;
	int ret;

	ret = imx492_read_reg(client, IMX492_REG_CHIP_ID,
			      IMX492_REG_VALUE_08BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected imx492 id %06x\n", CHIP_ID);

	return 0;
}

static int imx492_configure_regulators(struct imx492 *imx492)
{
	unsigned int i;

	for (i = 0; i < IMX492_NUM_SUPPLIES; i++)
		imx492->supplies[i].supply = imx492_supply_names[i];

	return devm_regulator_bulk_get(&imx492->client->dev,
					IMX492_NUM_SUPPLIES,
					imx492->supplies);
}

static int imx492_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx492 *imx492;
	struct v4l2_subdev *sd;
	struct device_node *endpoint;
	char facing[2];
	int ret;
	int i;
	u32 hdr_mode = 0;
	const char *sync_mode_name = NULL;


	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	imx492 = devm_kzalloc(dev, sizeof(*imx492), GFP_KERNEL);
	if (!imx492)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &imx492->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &imx492->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &imx492->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &imx492->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_string(node, RKMODULE_CAMERA_SYNC_MODE,
				      &sync_mode_name);
	if (ret) {
		imx492->sync_mode = NO_SYNC_MODE;
		dev_err(dev, "could not get sync mode!\n");
	} else {
		if (strcmp(sync_mode_name, RKMODULE_EXTERNAL_MASTER_MODE) == 0)
			imx492->sync_mode = EXTERNAL_MASTER_MODE;
		else if (strcmp(sync_mode_name, RKMODULE_INTERNAL_MASTER_MODE) == 0)
			imx492->sync_mode = INTERNAL_MASTER_MODE;
		else if (strcmp(sync_mode_name, RKMODULE_SLAVE_MODE) == 0)
			imx492->sync_mode = SLAVE_MODE;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	imx492->client = client;
	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint),
					 &imx492->bus_cfg);
	if (ret) {
		dev_err(dev, "Failed to get bus cfg\n");
		return ret;
	}
	imx492->support_modes = supported_modes;
	imx492->cfg_num = ARRAY_SIZE(supported_modes);

	for (i = 0; i < imx492->cfg_num; i++) {
		if (hdr_mode == imx492->support_modes[i].hdr_mode) {
			imx492->cur_mode = &imx492->support_modes[i];
			break;
		}
	}
	imx492->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx492->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx492->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx492->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx492->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx492->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	imx492->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(imx492->pinctrl)) {
		imx492->pins_default =
			pinctrl_lookup_state(imx492->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(imx492->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		imx492->pins_sleep =
			pinctrl_lookup_state(imx492->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(imx492->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = imx492_configure_regulators(imx492);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&imx492->mutex);

	sd = &imx492->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx492_subdev_ops);
	ret = imx492_initialize_controls(imx492);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx492_power_on(imx492);
	if (ret)
		goto err_free_handler;

	ret = imx492_check_sensor_id(imx492, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &imx492_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	imx492->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx492->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx492->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
			 imx492->module_index, facing,
			 IMX492_NAME, dev_name(sd->dev));
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
	__imx492_power_off(imx492);
err_free_handler:
	v4l2_ctrl_handler_free(&imx492->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx492->mutex);

	return ret;
}

static int imx492_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx492 *imx492 = to_IMX492(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx492->ctrl_handler);
	mutex_destroy(&imx492->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx492_power_off(imx492);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx492_of_match[] = {
	{ .compatible = "sony,imx492" },
	{},
};
MODULE_DEVICE_TABLE(of, imx492_of_match);
#endif

static const struct i2c_device_id imx492_match_id[] = {
	{ "sony,imx492", 0 },
	{ },
};

static struct i2c_driver imx492_i2c_driver = {
	.driver = {
		.name = IMX492_NAME,
		.pm = &imx492_pm_ops,
		.of_match_table = of_match_ptr(imx492_of_match),
	},
	.probe		= &imx492_probe,
	.remove		= &imx492_remove,
	.id_table	= imx492_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx492_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx492_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony IMX492 sensor driver");
MODULE_LICENSE("GPL");
