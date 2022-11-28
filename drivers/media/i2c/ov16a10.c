// SPDX-License-Identifier: GPL-2.0
/*
 * ov16a10 camera driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 *
 */
//#define DEBUG
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
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OV16A10_LINK_FREQ_726MHZ	726000000U

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define OV16A10_PIXEL_RATE		(OV16A10_LINK_FREQ_726MHZ * 2LL * 4LL / 10LL)
#define OV16A10_XVCLK_FREQ		24000000

#define CHIP_ID				0x561641
#define OV16A10_REG_CHIP_ID		0x300a

#define OV16A10_REG_CTRL_MODE		0x0100
#define OV16A10_MODE_SW_STANDBY		0x0
#define OV16A10_MODE_STREAMING		BIT(0)

#define OV16A10_REG_EXPOSURE_H		0x3500
#define OV16A10_REG_EXPOSURE_M		0x3501
#define OV16A10_REG_EXPOSURE_L		0x3502
#define	OV16A10_EXPOSURE_MIN		4
#define	OV16A10_EXPOSURE_STEP		1
#define OV16A10_VTS_MAX			0x7fff

#define OV16A10_REG_AGAIN_H		0x3508
#define OV16A10_REG_AGAIN_L		0x3509
#define OV16A10_REG_DAGAIN_H_B		0x350A
#define OV16A10_REG_DAGAIN_M_B		0x350B
#define OV16A10_REG_DAGAIN_L_B		0x350C
#define OV16A10_GAIN_MIN		0x80
#define OV16A10_GAIN_MAX		0x3df61
#define OV16A10_GAIN_STEP		1
#define OV16A10_GAIN_DEFAULT		0x80

#define OV16A10_SOFTWARE_RESET_REG	0x0103
#define OV16A10_REG_ISP_X_WIN		0x3810
#define OV16A10_REG_ISP_Y_WIN		0x3812

#define OV16A10_GROUP_UPDATE_ADDRESS	0x3208
#define OV16A10_GROUP_UPDATE_START_DATA	0x00
#define OV16A10_GROUP_UPDATE_END_DATA	0x10
#define OV16A10_GROUP_UPDATE_LAUNCH	0xA0

#define OV16A10_REG_TEST_PATTERN	0x5081
#define	OV16A10_TEST_PATTERN_ENABLE	0x01
#define	OV16A10_TEST_PATTERN_DISABLE	0x0

#define OV16A10_REG_VTS_H		0x380e
#define OV16A10_REG_VTS_L		0x380f

#define OV16A10_FLIP_REG		0x3820
#define OV16A10_MIRROR_REG		0x3821
#define MIRROR_BIT_MASK			BIT(2)
#define FLIP_BIT_MASK			BIT(2)

#define OV16A10_FETCH_EXP_H(VAL)	(((VAL) >> 16) & 0x7F)
#define OV16A10_FETCH_EXP_M(VAL)	(((VAL) >> 8) & 0xFF)
#define OV16A10_FETCH_EXP_L(VAL)	((VAL) & 0xFF)

#define OV16A10_FETCH_AGAIN_H(VAL)	(((VAL) >> 8) & 0x7F)
#define OV16A10_FETCH_AGAIN_L(VAL)	((VAL) & 0xFE)

#define OV16A10_FETCH_DGAIN_H(VAL)	(((VAL) >> 16) & 0x0F)
#define OV16A10_FETCH_DGAIN_M(VAL)	(((VAL) >> 8) & 0xFF)
#define OV16A10_FETCH_DGAIN_L(VAL)	((VAL) & 0xC0)

#define OV16A10_FETCH_VTS_H(VAL)	(((VAL) >> 8) & 0x7F)
#define OV16A10_FETCH_VTS_L(VAL)	((VAL) & 0xFF)

#define REG_NULL			0xFFFF

#define OV16A10_REG_VALUE_08BIT		1
#define OV16A10_REG_VALUE_16BIT		2
#define OV16A10_REG_VALUE_24BIT		3

#define OV16A10_LANES			4
#define OV16A10_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define OV16A10_NAME			"ov16a10"
#define OV16A10_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SBGGR10_1X10

static const char * const ov16a10_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV16A10_NUM_SUPPLIES ARRAY_SIZE(ov16a10_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov16a10_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct ov16a10 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV16A10_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct v4l2_ctrl	*test_pattern;
	struct v4l2_ctrl	*h_flip;
	struct v4l2_ctrl	*v_flip;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct ov16a10_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_ov16a10(sd) container_of(sd, struct ov16a10, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval ov16a10_global_regs[] = {
	{0x0103, 0x01},
	{0x0102, 0x00},
	{0x0301, 0x48},
	{0x0302, 0x31},
	{0x0303, 0x04},
	{0x0305, 0x6b},
	{0x0306, 0x00},
	{0x0320, 0x02},
	{0x0323, 0x04},
	{0x0326, 0xd8},
	{0x0327, 0x0b},
	{0x0329, 0x01},
	{0x0343, 0x04},
	{0x0344, 0x01},
	{0x0345, 0x2c},
	{0x0346, 0xc0},
	{0x034a, 0x07},
	{0x300e, 0x22},
	{0x3012, 0x41},
	{0x3016, 0xd2},
	{0x3018, 0x70},
	{0x301e, 0x98},
	{0x3025, 0x03},
	{0x3026, 0x10},
	{0x3027, 0x08},
	{0x3102, 0x00},
	{0x3400, 0x04},
	{0x3406, 0x04},
	{0x3408, 0x04},
	{0x3421, 0x09},
	{0x3422, 0x20},
	{0x3423, 0x15},
	{0x3424, 0x40},
	{0x3425, 0x14},
	{0x3426, 0x04},
	{0x3504, 0x08},
	{0x3508, 0x01},
	{0x3509, 0x00},
	{0x350a, 0x01},
	{0x350b, 0x00},
	{0x350c, 0x00},
	{0x3548, 0x01},
	{0x3549, 0x00},
	{0x354a, 0x01},
	{0x354b, 0x00},
	{0x354c, 0x00},
	{0x3600, 0xff},
	{0x3602, 0x42},
	{0x3603, 0x7b},
	{0x3608, 0x9b},
	{0x360a, 0x69},
	{0x360b, 0x53},
	{0x3618, 0xc0},
	{0x361a, 0x8b},
	{0x361d, 0x20},
	{0x361e, 0x10},
	{0x361f, 0x01},
	{0x3620, 0x89},
	{0x3624, 0x8f},
	{0x3629, 0x09},
	{0x362e, 0x50},
	{0x3631, 0xe2},
	{0x3632, 0xe2},
	{0x3634, 0x10},
	{0x3635, 0x10},
	{0x3636, 0x10},
	{0x3639, 0xa6},
	{0x363a, 0xaa},
	{0x363b, 0x0c},
	{0x363c, 0x16},
	{0x363d, 0x29},
	{0x363e, 0x4f},
	{0x3642, 0xa8},
	{0x3652, 0x00},
	{0x3653, 0x00},
	{0x3654, 0x8a},
	{0x3656, 0x0c},
	{0x3657, 0x8e},
	{0x3660, 0x80},
	{0x3663, 0x00},
	{0x3664, 0x00},
	{0x3668, 0x05},
	{0x3669, 0x05},
	{0x370d, 0x10},
	{0x370e, 0x05},
	{0x370f, 0x10},
	{0x3711, 0x01},
	{0x3712, 0x09},
	{0x3713, 0x40},
	{0x3714, 0xe4},
	{0x3716, 0x04},
	{0x3717, 0x01},
	{0x3718, 0x02},
	{0x3719, 0x01},
	{0x371a, 0x02},
	{0x371b, 0x02},
	{0x371c, 0x01},
	{0x371d, 0x02},
	{0x371e, 0x12},
	{0x371f, 0x02},
	{0x3720, 0x14},
	{0x3721, 0x12},
	{0x3722, 0x44},
	{0x3723, 0x60},
	{0x372f, 0x34},
	{0x3726, 0x21},
	{0x37d0, 0x02},
	{0x37d1, 0x10},
	{0x37db, 0x08},
	{0x3808, 0x12},
	{0x3809, 0x30},
	{0x380a, 0x0d},
	{0x380b, 0xa8},
	{0x380c, 0x03},
	{0x380d, 0x52},
	{0x380e, 0x0f},
	{0x380f, 0x50},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x00},
	{0x3821, 0x06},
	{0x3822, 0x00},
	{0x3823, 0x00},
	{0x3837, 0x10},
	{0x383c, 0x22},
	{0x383d, 0xff},
	{0x383e, 0x0d},
	{0x383f, 0x33},
	{0x3857, 0x00},
	{0x388f, 0x00},
	{0x3890, 0x00},
	{0x3891, 0x00},
	{0x3d81, 0x10},
	{0x3d83, 0x0c},
	{0x3d84, 0x00},
	{0x3d85, 0x1b},
	{0x3d88, 0x00},
	{0x3d89, 0x00},
	{0x3d8a, 0x00},
	{0x3d8b, 0x01},
	{0x3d8c, 0x77},
	{0x3d8d, 0xa0},
	{0x3f00, 0x02},
	{0x3f0c, 0x07},
	{0x3f0d, 0x2f},
	{0x4012, 0x0d},
	{0x4015, 0x04},
	{0x4016, 0x1b},
	{0x4017, 0x04},
	{0x4018, 0x0b},
	{0x401b, 0x10},
	{0x401e, 0x01},
	{0x401f, 0x38},
	{0x4500, 0x20},
	{0x4501, 0x6a},
	{0x4502, 0xb4},
	{0x4586, 0x00},
	{0x4588, 0x02},
	{0x4640, 0x00},
	{0x4641, 0x28},
	{0x4643, 0x08},
	{0x4645, 0x04},
	{0x4806, 0x40},
	{0x480e, 0x00},
	{0x4815, 0x2b},
	{0x481b, 0x3c},
	{0x4833, 0x18},
	{0x4837, 0x08},
	{0x484b, 0x07},
	{0x4850, 0x41},
	{0x4860, 0x00},
	{0x4861, 0xec},
	{0x4864, 0x00},
	{0x4883, 0x00},
	{0x4888, 0x10},
	{0x4a00, 0x10},
	{0x4e00, 0x00},
	{0x4e01, 0x04},
	{0x4e02, 0x01},
	{0x4e03, 0x00},
	{0x4e04, 0x08},
	{0x4e05, 0x04},
	{0x4e06, 0x00},
	{0x4e07, 0x13},
	{0x4e08, 0x01},
	{0x4e09, 0x00},
	{0x4e0a, 0x15},
	{0x4e0b, 0x0e},
	{0x4e0c, 0x00},
	{0x4e0d, 0x17},
	{0x4e0e, 0x07},
	{0x4e0f, 0x00},
	{0x4e10, 0x19},
	{0x4e11, 0x06},
	{0x4e12, 0x00},
	{0x4e13, 0x1b},
	{0x4e14, 0x08},
	{0x4e15, 0x00},
	{0x4e16, 0x1f},
	{0x4e17, 0x08},
	{0x4e18, 0x00},
	{0x4e19, 0x21},
	{0x4e1a, 0x0e},
	{0x4e1b, 0x00},
	{0x4e1c, 0x2d},
	{0x4e1d, 0x30},
	{0x4e1e, 0x00},
	{0x4e1f, 0x6a},
	{0x4e20, 0x05},
	{0x4e21, 0x00},
	{0x4e22, 0x6c},
	{0x4e23, 0x05},
	{0x4e24, 0x00},
	{0x4e25, 0x6e},
	{0x4e26, 0x39},
	{0x4e27, 0x00},
	{0x4e28, 0x7a},
	{0x4e29, 0x6d},
	{0x4e2a, 0x00},
	{0x4e2b, 0x00},
	{0x4e2c, 0x00},
	{0x4e2d, 0x00},
	{0x4e2e, 0x00},
	{0x4e2f, 0x00},
	{0x4e30, 0x00},
	{0x4e31, 0x00},
	{0x4e32, 0x00},
	{0x4e33, 0x00},
	{0x4e34, 0x00},
	{0x4e35, 0x00},
	{0x4e36, 0x00},
	{0x4e37, 0x00},
	{0x4e38, 0x00},
	{0x4e39, 0x00},
	{0x4e3a, 0x00},
	{0x4e3b, 0x00},
	{0x4e3c, 0x00},
	{0x4e3d, 0x00},
	{0x4e3e, 0x00},
	{0x4e3f, 0x00},
	{0x4e40, 0x00},
	{0x4e41, 0x00},
	{0x4e42, 0x00},
	{0x4e43, 0x00},
	{0x4e44, 0x00},
	{0x4e45, 0x00},
	{0x4e46, 0x00},
	{0x4e47, 0x00},
	{0x4e48, 0x00},
	{0x4e49, 0x00},
	{0x4e4a, 0x00},
	{0x4e4b, 0x00},
	{0x4e4c, 0x00},
	{0x4e4d, 0x00},
	{0x4e4e, 0x00},
	{0x4e4f, 0x00},
	{0x4e50, 0x00},
	{0x4e51, 0x00},
	{0x4e52, 0x00},
	{0x4e53, 0x00},
	{0x4e54, 0x00},
	{0x4e55, 0x00},
	{0x4e56, 0x00},
	{0x4e57, 0x00},
	{0x4e58, 0x00},
	{0x4e59, 0x00},
	{0x4e5a, 0x00},
	{0x4e5b, 0x00},
	{0x4e5c, 0x00},
	{0x4e5d, 0x00},
	{0x4e5e, 0x00},
	{0x4e5f, 0x00},
	{0x4e60, 0x00},
	{0x4e61, 0x00},
	{0x4e62, 0x00},
	{0x4e63, 0x00},
	{0x4e64, 0x00},
	{0x4e65, 0x00},
	{0x4e66, 0x00},
	{0x4e67, 0x00},
	{0x4e68, 0x00},
	{0x4e69, 0x00},
	{0x4e6a, 0x00},
	{0x4e6b, 0x00},
	{0x4e6c, 0x00},
	{0x4e6d, 0x00},
	{0x4e6e, 0x00},
	{0x4e6f, 0x00},
	{0x4e70, 0x00},
	{0x4e71, 0x00},
	{0x4e72, 0x00},
	{0x4e73, 0x00},
	{0x4e74, 0x00},
	{0x4e75, 0x00},
	{0x4e76, 0x00},
	{0x4e77, 0x00},
	{0x4e78, 0x1c},
	{0x4e79, 0x1e},
	{0x4e7a, 0x00},
	{0x4e7b, 0x00},
	{0x4e7c, 0x2c},
	{0x4e7d, 0x2f},
	{0x4e7e, 0x79},
	{0x4e7f, 0x7b},
	{0x4e80, 0x0a},
	{0x4e81, 0x31},
	{0x4e82, 0x66},
	{0x4e83, 0x81},
	{0x4e84, 0x03},
	{0x4e85, 0x40},
	{0x4e86, 0x02},
	{0x4e87, 0x09},
	{0x4e88, 0x43},
	{0x4e89, 0x53},
	{0x4e8a, 0x32},
	{0x4e8b, 0x67},
	{0x4e8c, 0x05},
	{0x4e8d, 0x83},
	{0x4e8e, 0x00},
	{0x4e8f, 0x00},
	{0x4e90, 0x00},
	{0x4e91, 0x00},
	{0x4e92, 0x00},
	{0x4e93, 0x00},
	{0x4e94, 0x00},
	{0x4e95, 0x00},
	{0x4e96, 0x00},
	{0x4e97, 0x00},
	{0x4e98, 0x00},
	{0x4e99, 0x00},
	{0x4e9a, 0x00},
	{0x4e9b, 0x00},
	{0x4e9c, 0x00},
	{0x4e9d, 0x00},
	{0x4e9e, 0x00},
	{0x4e9f, 0x00},
	{0x4ea0, 0x00},
	{0x4ea1, 0x00},
	{0x4ea2, 0x00},
	{0x4ea3, 0x00},
	{0x4ea4, 0x00},
	{0x4ea5, 0x00},
	{0x4ea6, 0x1e},
	{0x4ea7, 0x20},
	{0x4ea8, 0x32},
	{0x4ea9, 0x6d},
	{0x4eaa, 0x18},
	{0x4eab, 0x7f},
	{0x4eac, 0x00},
	{0x4ead, 0x00},
	{0x4eae, 0x7c},
	{0x4eaf, 0x07},
	{0x4eb0, 0x7c},
	{0x4eb1, 0x07},
	{0x4eb2, 0x07},
	{0x4eb3, 0x1c},
	{0x4eb4, 0x07},
	{0x4eb5, 0x1c},
	{0x4eb6, 0x07},
	{0x4eb7, 0x1c},
	{0x4eb8, 0x07},
	{0x4eb9, 0x1c},
	{0x4eba, 0x07},
	{0x4ebb, 0x14},
	{0x4ebc, 0x07},
	{0x4ebd, 0x1c},
	{0x4ebe, 0x07},
	{0x4ebf, 0x1c},
	{0x4ec0, 0x07},
	{0x4ec1, 0x1c},
	{0x4ec2, 0x07},
	{0x4ec3, 0x1c},
	{0x4ec4, 0x2c},
	{0x4ec5, 0x2f},
	{0x4ec6, 0x79},
	{0x4ec7, 0x7b},
	{0x4ec8, 0x7c},
	{0x4ec9, 0x07},
	{0x4eca, 0x7c},
	{0x4ecb, 0x07},
	{0x4ecc, 0x00},
	{0x4ecd, 0x00},
	{0x4ece, 0x07},
	{0x4ecf, 0x31},
	{0x4ed0, 0x69},
	{0x4ed1, 0x7f},
	{0x4ed2, 0x67},
	{0x4ed3, 0x00},
	{0x4ed4, 0x00},
	{0x4ed5, 0x00},
	{0x4ed6, 0x7c},
	{0x4ed7, 0x07},
	{0x4ed8, 0x7c},
	{0x4ed9, 0x07},
	{0x4eda, 0x33},
	{0x4edb, 0x7f},
	{0x4edc, 0x00},
	{0x4edd, 0x16},
	{0x4ede, 0x00},
	{0x4edf, 0x00},
	{0x4ee0, 0x32},
	{0x4ee1, 0x70},
	{0x4ee2, 0x01},
	{0x4ee3, 0x30},
	{0x4ee4, 0x22},
	{0x4ee5, 0x28},
	{0x4ee6, 0x6f},
	{0x4ee7, 0x75},
	{0x4ee8, 0x00},
	{0x4ee9, 0x00},
	{0x4eea, 0x30},
	{0x4eeb, 0x7f},
	{0x4eec, 0x00},
	{0x4eed, 0x00},
	{0x4eee, 0x00},
	{0x4eef, 0x00},
	{0x4ef0, 0x69},
	{0x4ef1, 0x7f},
	{0x4ef2, 0x07},
	{0x4ef3, 0x30},
	{0x4ef4, 0x32},
	{0x4ef5, 0x09},
	{0x4ef6, 0x7d},
	{0x4ef7, 0x65},
	{0x4ef8, 0x00},
	{0x4ef9, 0x00},
	{0x4efa, 0x00},
	{0x4efb, 0x00},
	{0x4efc, 0x7f},
	{0x4efd, 0x09},
	{0x4efe, 0x7f},
	{0x4eff, 0x09},
	{0x4f00, 0x1e},
	{0x4f01, 0x7c},
	{0x4f02, 0x7f},
	{0x4f03, 0x09},
	{0x4f04, 0x7f},
	{0x4f05, 0x0b},
	{0x4f06, 0x7c},
	{0x4f07, 0x02},
	{0x4f08, 0x7c},
	{0x4f09, 0x02},
	{0x4f0a, 0x32},
	{0x4f0b, 0x64},
	{0x4f0c, 0x32},
	{0x4f0d, 0x64},
	{0x4f0e, 0x32},
	{0x4f0f, 0x64},
	{0x4f10, 0x32},
	{0x4f11, 0x64},
	{0x4f12, 0x31},
	{0x4f13, 0x4f},
	{0x4f14, 0x83},
	{0x4f15, 0x84},
	{0x4f16, 0x63},
	{0x4f17, 0x64},
	{0x4f18, 0x83},
	{0x4f19, 0x84},
	{0x4f1a, 0x31},
	{0x4f1b, 0x32},
	{0x4f1c, 0x7b},
	{0x4f1d, 0x7c},
	{0x4f1e, 0x2f},
	{0x4f1f, 0x30},
	{0x4f20, 0x30},
	{0x4f21, 0x69},
	{0x4d06, 0x08},
	{0x5000, 0x0b},
	{0x5001, 0x4b},
	{0x5002, 0x57},
	{0x5003, 0x42},
	{0x5005, 0x00},
	{0x5038, 0x00},
	{0x5081, 0x00},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x07},
	{0x5183, 0x8f},
	{0x5184, 0x03},
	{0x5820, 0xc5},
	{0x5854, 0x00},
	{0x58cb, 0x03},
	{0x5bd0, 0x01},
	{0x5bd1, 0x02},
	{0x5c0e, 0x11},
	{0x5c11, 0x01},
	{0x5c16, 0x02},
	{0x5c17, 0x00},
	{0x5c1a, 0x00},
	{0x5c1b, 0x00},
	{0x5c21, 0x10},
	{0x5c22, 0x10},
	{0x5c23, 0x02},
	{0x5c24, 0x0a},
	{0x5c25, 0x06},
	{0x5c26, 0x0e},
	{0x5c27, 0x02},
	{0x5c28, 0x02},
	{0x5c29, 0x0a},
	{0x5c2a, 0x0a},
	{0x5c2b, 0x01},
	{0x5c2c, 0x00},
	{0x5c2e, 0x08},
	{0x5c30, 0x04},
	{0x5c35, 0x03},
	{0x5c36, 0x03},
	{0x5c37, 0x03},
	{0x5c38, 0x03},
	{0x5d00, 0xff},
	{0x5d01, 0x07},
	{0x5d02, 0x80},
	{0x5d03, 0x44},
	{0x5d05, 0xfc},
	{0x5d06, 0x0b},
	{0x5d08, 0x10},
	{0x5d09, 0x10},
	{0x5d0a, 0x02},
	{0x5d0b, 0x0a},
	{0x5d0c, 0x06},
	{0x5d0d, 0x0e},
	{0x5d0e, 0x02},
	{0x5d0f, 0x02},
	{0x5d10, 0x0a},
	{0x5d11, 0x0a},
	{0x5d12, 0x01},
	{0x5d13, 0x00},
	{0x5d15, 0x10},
	{0x5d16, 0x10},
	{0x5d17, 0x10},
	{0x5d18, 0x10},
	{0x5d1a, 0x10},
	{0x5d1b, 0x10},
	{0x5d1c, 0x10},
	{0x5d1d, 0x10},
	{0x5d1e, 0x04},
	{0x5d1f, 0x04},
	{0x5d20, 0x04},
	{0x5d27, 0x64},
	{0x5d28, 0xc8},
	{0x5d29, 0x96},
	{0x5d2a, 0xff},
	{0x5d2b, 0xc8},
	{0x5d2c, 0xff},
	{0x5d2d, 0x04},
	{0x5d34, 0x00},
	{0x5d35, 0x08},
	{0x5d36, 0x00},
	{0x5d37, 0x04},
	{0x5d4a, 0x00},
	{0x5d4c, 0x00},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 */
static const struct regval ov16a10_4656x3496_30fps_regs[] = {
	{0x0305, 0x6b},
	{0x0307, 0x00},
	{0x4837, 0x0b},
	{0x0100, 0x00},
	{0x0329, 0x01},
	{0x0344, 0x01},
	{0x0345, 0x2c},
	{0x034a, 0x07},
	{0x360a, 0x69},
	{0x361a, 0x8b},
	{0x3639, 0xa6},
	{0x3654, 0x8a},
	{0x3656, 0x0c},
	{0x37d0, 0x02},
	{0x37d1, 0x10},
	{0x37db, 0x08},
	{0x3808, 0x12},
	{0x3809, 0x30},
	{0x380a, 0x0d},
	{0x380b, 0xa8},
	{0x380c, 0x03},
	{0x380d, 0x52},
	{0x380e, 0x0f},
	{0x380f, 0x50},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x00},
	{0x3821, 0x06},
	{0x3822, 0x00},
	{0x383c, 0x22},
	{0x4015, 0x04},
	{0x4016, 0x1b},
	{0x4017, 0x04},
	{0x4018, 0x0b},
	{0x401e, 0x01},
	{0x401f, 0x38},
	{0x4500, 0x20},
	{0x4501, 0x6a},
	{0x4586, 0x00},
	{0x4588, 0x02},
	{0x4e05, 0x04},
	{0x4e11, 0x06},
	{0x4e1d, 0x30},
	{0x4e26, 0x39},
	{0x4e29, 0x6d},
	{0x5000, 0x0b},
	{0x5001, 0x4b},
	{0x5002, 0x57},
	{0x5820, 0xc5},
	{0x5bd0, 0x01},
	{0x5c0e, 0x11},
	{0x5c21, 0x10},
	{0x5c22, 0x10},
	{0x5c23, 0x02},
	{0x5c24, 0x0a},
	{0x5c25, 0x06},
	{0x5c26, 0x0e},
	{0x5c27, 0x02},
	{0x5c28, 0x02},
	{0x5c29, 0x0a},
	{0x5c2a, 0x0a},
	{0x5d08, 0x10},
	{0x5d09, 0x10},
	{0x5d0a, 0x02},
	{0x5d0b, 0x0a},
	{0x5d0c, 0x06},
	{0x5d0d, 0x0e},
	{0x5d0e, 0x02},
	{0x5d0f, 0x02},
	{0x5d10, 0x0a},
	{0x5d11, 0x0a},
	{0x3501, 0x0f},
	{0x3502, 0x48},
	{0x3508, 0x01},
	{0x3509, 0x00},
	//{0x0100, 0x01},
	{REG_NULL, 0x00},
};

static const struct regval ov16a10_2328x1748_30fps_regs[] = {
	{0x0305, 0x6b},
	{0x0307, 0x00},
	{0x4837, 0x0b},
	{0x0100, 0x00},
	{0x0329, 0x01},
	{0x0344, 0x01},
	{0x0345, 0x2c},
	{0x034a, 0x07},
	{0x360a, 0x69},
	{0x361a, 0x8b},
	{0x3639, 0xa6},
	{0x3654, 0x8a},
	{0x3656, 0x0c},
	{0x37d0, 0x01},
	{0x37d1, 0x10},
	{0x37db, 0x08},
	{0x3808, 0x09},
	{0x3809, 0x18},
	{0x380a, 0x06},
	{0x380b, 0xd4},
	{0x380c, 0x03},
	{0x380d, 0x52},
	{0x380e, 0x0f},
	{0x380f, 0x50},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x00},
	{0x3821, 0x06},
	{0x3822, 0x11},
	{0x383c, 0x22},
	{0x4015, 0x04},
	{0x4016, 0x1b},
	{0x4017, 0x00},
	{0x4018, 0x07},
	{0x401e, 0x01},
	{0x401f, 0x38},
	{0x4500, 0x20},
	{0x4501, 0x6a},
	{0x4586, 0x00},
	{0x4588, 0x02},
	{0x4e05, 0x04},
	{0x4e11, 0x06},
	{0x4e1d, 0x30},
	{0x4e26, 0x39},
	{0x4e29, 0x6d},
	{0x5000, 0x2b},
	{0x5001, 0x4b},
	{0x5002, 0x17},
	{0x5820, 0xc3},
	{0x5bd0, 0x01},
	{0x5c0e, 0x11},
	{0x5c21, 0x10},
	{0x5c22, 0x10},
	{0x5c23, 0x02},
	{0x5c24, 0x0a},
	{0x5c25, 0x06},
	{0x5c26, 0x0e},
	{0x5c27, 0x02},
	{0x5c28, 0x02},
	{0x5c29, 0x0a},
	{0x5c2a, 0x0a},
	{0x5d08, 0x10},
	{0x5d09, 0x10},
	{0x5d0a, 0x02},
	{0x5d0b, 0x0a},
	{0x5d0c, 0x06},
	{0x5d0d, 0x0e},
	{0x5d0e, 0x02},
	{0x5d0f, 0x02},
	{0x5d10, 0x0a},
	{0x5d11, 0x0a},
	{0x3501, 0x07},
	{0x3502, 0xa0},
	{0x3508, 0x01},
	{0x3509, 0x00},
	//{0x0100, 0x01},
	{REG_NULL, 0x00},
};

static const struct ov16a10_mode supported_modes[] = {
	{
		.width = 4656,
		.height = 3496,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0f4a,
		.hts_def = 0x0352 * 6,
		.vts_def = 0x0f50,
		.bpp = 10,
		.reg_list = ov16a10_4656x3496_30fps_regs,
		.link_freq_idx = 0,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 2328,
		.height = 1748,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0f4a,
		.hts_def = 0x0352 * 3,
		.vts_def = 0x0f50,
		.bpp = 10,
		.reg_list = ov16a10_2328x1748_30fps_regs,
		.link_freq_idx = 0,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_items[] = {
	OV16A10_LINK_FREQ_726MHZ,
};

static const char * const ov16a10_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ov16a10_write_reg(struct i2c_client *client, u16 reg,
			     u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_dbg(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);

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

static int ov16a10_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov16a10_write_reg(client, regs[i].addr,
					OV16A10_REG_VALUE_08BIT,
					regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int ov16a10_read_reg(struct i2c_client *client, u16 reg,
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

static int ov16a10_get_reso_dist(const struct ov16a10_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov16a10_mode *
ov16a10_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov16a10_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov16a10_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov16a10 *ov16a10 = to_ov16a10(sd);
	const struct ov16a10_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;
	u32 lane_num = OV16A10_LANES;

	mutex_lock(&ov16a10->mutex);

	mode = ov16a10_find_best_fit(fmt);
	fmt->format.code = OV16A10_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov16a10->mutex);
		return -ENOTTY;
#endif
	} else {
		ov16a10->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov16a10->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov16a10->vblank, vblank_def,
					 OV16A10_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl(ov16a10->vblank, vblank_def);
		pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * lane_num;

		__v4l2_ctrl_s_ctrl_int64(ov16a10->pixel_rate,
					 pixel_rate);
		__v4l2_ctrl_s_ctrl(ov16a10->link_freq,
				   mode->link_freq_idx);
	}
	dev_info(&ov16a10->client->dev, "%s: mode->link_freq_idx(%d)",
		 __func__, mode->link_freq_idx);

	mutex_unlock(&ov16a10->mutex);

	return 0;
}

static int ov16a10_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct ov16a10 *ov16a10 = to_ov16a10(sd);
	const struct ov16a10_mode *mode = ov16a10->cur_mode;

	mutex_lock(&ov16a10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov16a10->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = OV16A10_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&ov16a10->mutex);

	return 0;
}

static int ov16a10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = OV16A10_MEDIA_BUS_FMT;

	return 0;
}

static int ov16a10_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov16a10 *ov16a10 = to_ov16a10(sd);

	if (fse->index >= ov16a10->cfg_num)
		return -EINVAL;

	if (fse->code != OV16A10_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov16a10_enable_test_pattern(struct ov16a10 *ov16a10, u32 pattern)
{
	u32 val;

	if (pattern)
		val = ((pattern - 1) << 4) | OV16A10_TEST_PATTERN_ENABLE;
	else
		val = OV16A10_TEST_PATTERN_DISABLE;

	return ov16a10_write_reg(ov16a10->client,
				 OV16A10_REG_TEST_PATTERN,
				 OV16A10_REG_VALUE_08BIT,
				 val);
}

static int ov16a10_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct ov16a10 *ov16a10 = to_ov16a10(sd);
	const struct ov16a10_mode *mode = ov16a10->cur_mode;

	mutex_lock(&ov16a10->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov16a10->mutex);

	return 0;
}

static void ov16a10_get_module_inf(struct ov16a10 *ov16a10,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, OV16A10_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, ov16a10->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, ov16a10->len_name, sizeof(inf->base.lens));
}

static long ov16a10_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov16a10 *ov16a10 = to_ov16a10(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 i, h, w;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		w = ov16a10->cur_mode->width;
		h = ov16a10->cur_mode->height;
		for (i = 0; i < ov16a10->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			h == supported_modes[i].height &&
			supported_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				ov16a10->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ov16a10->cfg_num) {
			dev_err(&ov16a10->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = ov16a10->cur_mode->hts_def - ov16a10->cur_mode->width;
			h = ov16a10->cur_mode->vts_def - ov16a10->cur_mode->height;
			__v4l2_ctrl_modify_range(ov16a10->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(ov16a10->vblank, h,
						 OV16A10_VTS_MAX - ov16a10->cur_mode->height,
						 1, h);
			dev_info(&ov16a10->client->dev,
				"sensor mode: %d\n",
				ov16a10->cur_mode->hdr_mode);
		}
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = ov16a10->cur_mode->hdr_mode;
		break;
	case RKMODULE_GET_MODULE_INFO:
		ov16a10_get_module_inf(ov16a10, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov16a10_write_reg(ov16a10->client,
				 OV16A10_REG_CTRL_MODE,
				 OV16A10_REG_VALUE_08BIT,
				 OV16A10_MODE_STREAMING);
		else
			ret = ov16a10_write_reg(ov16a10->client,
				 OV16A10_REG_CTRL_MODE,
				 OV16A10_REG_VALUE_08BIT,
				 OV16A10_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov16a10_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov16a10_ioctl(sd, cmd, inf);
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
			ret = ov16a10_ioctl(sd, cmd, cfg);
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

		ret = ov16a10_ioctl(sd, cmd, hdr);
		if (!ret) {
			if (copy_to_user(up, hdr, sizeof(*hdr))) {
				kfree(hdr);
				return -EFAULT;
			}
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(hdr, up, sizeof(*hdr))) {
			kfree(hdr);
			return -EFAULT;
		}
		ret = ov16a10_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov16a10_ioctl(sd, cmd, &stream);
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

static int __ov16a10_start_stream(struct ov16a10 *ov16a10)
{
	int ret;

	ret = ov16a10_write_array(ov16a10->client, ov16a10->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&ov16a10->mutex);
	ret = v4l2_ctrl_handler_setup(&ov16a10->ctrl_handler);
	mutex_lock(&ov16a10->mutex);
	if (ret)
		return ret;

	return ov16a10_write_reg(ov16a10->client,
				 OV16A10_REG_CTRL_MODE,
				 OV16A10_REG_VALUE_08BIT,
				 OV16A10_MODE_STREAMING);
}

static int __ov16a10_stop_stream(struct ov16a10 *ov16a10)
{
	return ov16a10_write_reg(ov16a10->client,
				 OV16A10_REG_CTRL_MODE,
				 OV16A10_REG_VALUE_08BIT,
				 OV16A10_MODE_SW_STANDBY);
}

static int ov16a10_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov16a10 *ov16a10 = to_ov16a10(sd);
	struct i2c_client *client = ov16a10->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				ov16a10->cur_mode->width,
				ov16a10->cur_mode->height,
		DIV_ROUND_CLOSEST(ov16a10->cur_mode->max_fps.denominator,
				  ov16a10->cur_mode->max_fps.numerator));

	mutex_lock(&ov16a10->mutex);
	on = !!on;
	if (on == ov16a10->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov16a10_start_stream(ov16a10);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov16a10_stop_stream(ov16a10);
		pm_runtime_put(&client->dev);
	}

	ov16a10->streaming = on;

unlock_and_return:
	mutex_unlock(&ov16a10->mutex);

	return ret;
}

static int ov16a10_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov16a10 *ov16a10 = to_ov16a10(sd);
	struct i2c_client *client = ov16a10->client;
	int ret = 0;

	mutex_lock(&ov16a10->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov16a10->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = ov16a10_write_array(ov16a10->client, ov16a10_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ov16a10->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ov16a10->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov16a10->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov16a10_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV16A10_XVCLK_FREQ / 1000 / 1000);
}

static int __ov16a10_power_on(struct ov16a10 *ov16a10)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov16a10->client->dev;

	if (!IS_ERR(ov16a10->power_gpio))
		gpiod_set_value_cansleep(ov16a10->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(ov16a10->pins_default)) {
		ret = pinctrl_select_state(ov16a10->pinctrl,
					   ov16a10->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(ov16a10->xvclk, OV16A10_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov16a10->xvclk) != OV16A10_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov16a10->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(ov16a10->reset_gpio))
		gpiod_set_value_cansleep(ov16a10->reset_gpio, 0);

	ret = regulator_bulk_enable(OV16A10_NUM_SUPPLIES, ov16a10->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov16a10->reset_gpio))
		gpiod_set_value_cansleep(ov16a10->reset_gpio, 1);

	usleep_range(5000, 6000);
	if (!IS_ERR(ov16a10->pwdn_gpio))
		gpiod_set_value_cansleep(ov16a10->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov16a10_cal_delay(8192);
	usleep_range(delay_us * 2, delay_us * 3);

	return 0;

disable_clk:
	clk_disable_unprepare(ov16a10->xvclk);

	return ret;
}

static void __ov16a10_power_off(struct ov16a10 *ov16a10)
{
	int ret;
	struct device *dev = &ov16a10->client->dev;

	if (!IS_ERR(ov16a10->pwdn_gpio))
		gpiod_set_value_cansleep(ov16a10->pwdn_gpio, 0);
	clk_disable_unprepare(ov16a10->xvclk);
	if (!IS_ERR(ov16a10->reset_gpio))
		gpiod_set_value_cansleep(ov16a10->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(ov16a10->pins_sleep)) {
		ret = pinctrl_select_state(ov16a10->pinctrl,
					   ov16a10->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(ov16a10->power_gpio))
		gpiod_set_value_cansleep(ov16a10->power_gpio, 0);

	regulator_bulk_disable(OV16A10_NUM_SUPPLIES, ov16a10->supplies);
}

static int ov16a10_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov16a10 *ov16a10 = to_ov16a10(sd);

	return __ov16a10_power_on(ov16a10);
}

static int ov16a10_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov16a10 *ov16a10 = to_ov16a10(sd);

	__ov16a10_power_off(ov16a10);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov16a10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov16a10 *ov16a10 = to_ov16a10(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov16a10_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov16a10->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = OV16A10_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov16a10->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov16a10_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != OV16A10_MEDIA_BUS_FMT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;

	return 0;
}

static int ov16a10_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	if (2 == OV16A10_LANES) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_2_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else if (4 == OV16A10_LANES) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_4_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	}

	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH_2320 2320
#define DST_HEIGHT_1744 1744
/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int ov16a10_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct ov16a10 *ov16a10 = to_ov16a10(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		if (ov16a10->cur_mode->width == 2328) {
			sel->r.left = CROP_START(ov16a10->cur_mode->width, DST_WIDTH_2320);
			sel->r.width = DST_WIDTH_2320;
			sel->r.top = CROP_START(ov16a10->cur_mode->height, DST_HEIGHT_1744);
			sel->r.height = DST_HEIGHT_1744;
		} else {
			sel->r.left = 0;
			sel->r.width = ov16a10->cur_mode->width;
			sel->r.top = 0;
			sel->r.height = ov16a10->cur_mode->height;
		}
		return 0;
	}

	return -EINVAL;
}

static const struct dev_pm_ops ov16a10_pm_ops = {
	SET_RUNTIME_PM_OPS(ov16a10_runtime_suspend,
			   ov16a10_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov16a10_internal_ops = {
	.open = ov16a10_open,
};
#endif

static const struct v4l2_subdev_core_ops ov16a10_core_ops = {
	.s_power = ov16a10_s_power,
	.ioctl = ov16a10_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov16a10_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov16a10_video_ops = {
	.s_stream = ov16a10_s_stream,
	.g_frame_interval = ov16a10_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov16a10_pad_ops = {
	.enum_mbus_code = ov16a10_enum_mbus_code,
	.enum_frame_size = ov16a10_enum_frame_sizes,
	.enum_frame_interval = ov16a10_enum_frame_interval,
	.get_fmt = ov16a10_get_fmt,
	.set_fmt = ov16a10_set_fmt,
	.get_selection = ov16a10_get_selection,
	.get_mbus_config = ov16a10_g_mbus_config,
};

static const struct v4l2_subdev_ops ov16a10_subdev_ops = {
	.core	= &ov16a10_core_ops,
	.video	= &ov16a10_video_ops,
	.pad	= &ov16a10_pad_ops,
};

static int ov16a10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov16a10 *ov16a10 = container_of(ctrl->handler,
					     struct ov16a10, ctrl_handler);
	struct i2c_client *client = ov16a10->client;
	s64 max;
	int ret = 0;
	u32 again, dgain;
	u32 val = 0, x_win = 0, y_win = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov16a10->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov16a10->exposure,
					 ov16a10->exposure->minimum, max,
					 ov16a10->exposure->step,
					 ov16a10->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret |= ov16a10_write_reg(ov16a10->client,
					OV16A10_REG_EXPOSURE_H,
					OV16A10_REG_VALUE_24BIT,
					ctrl->val & 0x7fffff);
		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (ctrl->val > 1984) {// >15.5x
			dgain = ctrl->val * 10 / 155;
			again = 1984;
		} else {
			dgain = 1024;
			again = ctrl->val;
		}
		ret |= ov16a10_write_reg(ov16a10->client,
					 OV16A10_REG_AGAIN_H,
					 OV16A10_REG_VALUE_16BIT,
					 (again << 1) & 0x7ffe);
		ret |= ov16a10_write_reg(ov16a10->client,
					 OV16A10_REG_DAGAIN_H_B,
					 OV16A10_REG_VALUE_24BIT,
					 (dgain << 6) & 0xfffc0);

		dev_dbg(&client->dev, "set gain 0x%x set analog gain 0x%x digital gain 0x%x\n",
			ctrl->val, again, dgain);
		break;
	case V4L2_CID_VBLANK:
		ret = ov16a10_write_reg(ov16a10->client,
					OV16A10_REG_VTS_H,
					OV16A10_REG_VALUE_16BIT,
					ctrl->val + ov16a10->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov16a10_enable_test_pattern(ov16a10, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = ov16a10_read_reg(ov16a10->client, OV16A10_MIRROR_REG,
				       OV16A10_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= MIRROR_BIT_MASK;
		else
			val &= ~MIRROR_BIT_MASK;

		ret |= ov16a10_read_reg(ov16a10->client, OV16A10_REG_ISP_X_WIN,
					OV16A10_REG_VALUE_16BIT,
					&x_win);

		if ((x_win == 0x0010) && (val & 0x04))
			x_win = 0x0011;
		else if ((x_win == 0x0011) && (!(val & 0x04)))
			x_win = 0x0010;

		ret |= ov16a10_write_reg(ov16a10->client,
					 OV16A10_GROUP_UPDATE_ADDRESS,
					 OV16A10_REG_VALUE_08BIT,
					 OV16A10_GROUP_UPDATE_START_DATA);

		ret |= ov16a10_write_reg(ov16a10->client, OV16A10_MIRROR_REG,
					 OV16A10_REG_VALUE_08BIT,
					 val);
		ret |= ov16a10_write_reg(ov16a10->client, OV16A10_REG_ISP_X_WIN,
					 OV16A10_REG_VALUE_16BIT,
					 x_win);

		ret |= ov16a10_write_reg(ov16a10->client,
					 OV16A10_GROUP_UPDATE_ADDRESS,
					 OV16A10_REG_VALUE_08BIT,
					 OV16A10_GROUP_UPDATE_END_DATA);
		ret |= ov16a10_write_reg(ov16a10->client,
					 OV16A10_GROUP_UPDATE_ADDRESS,
					 OV16A10_REG_VALUE_08BIT,
					 OV16A10_GROUP_UPDATE_LAUNCH);
		break;
	case V4L2_CID_VFLIP:
		ret = ov16a10_read_reg(ov16a10->client, OV16A10_FLIP_REG,
				       OV16A10_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= FLIP_BIT_MASK;
		else
			val &= ~FLIP_BIT_MASK;

		ret |= ov16a10_read_reg(ov16a10->client, OV16A10_REG_ISP_Y_WIN,
					OV16A10_REG_VALUE_16BIT,
					&y_win);

		if ((y_win == 0x0004) && (val & 0x04))
			y_win = 0x0005;
		else if ((y_win == 0x0005) && (!(val & 0x04)))
			y_win = 0x0004;

		ret |= ov16a10_write_reg(ov16a10->client,
					 OV16A10_GROUP_UPDATE_ADDRESS,
					 OV16A10_REG_VALUE_08BIT,
					 OV16A10_GROUP_UPDATE_START_DATA);

		ret |= ov16a10_write_reg(ov16a10->client, OV16A10_FLIP_REG,
					 OV16A10_REG_VALUE_08BIT,
					 val);
		ret |= ov16a10_write_reg(ov16a10->client, OV16A10_REG_ISP_Y_WIN,
					 OV16A10_REG_VALUE_16BIT,
					 y_win);

		ret |= ov16a10_write_reg(ov16a10->client,
					 OV16A10_GROUP_UPDATE_ADDRESS,
					 OV16A10_REG_VALUE_08BIT,
					 OV16A10_GROUP_UPDATE_END_DATA);
		ret |= ov16a10_write_reg(ov16a10->client,
					 OV16A10_GROUP_UPDATE_ADDRESS,
					 OV16A10_REG_VALUE_08BIT,
					 OV16A10_GROUP_UPDATE_LAUNCH);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov16a10_ctrl_ops = {
	.s_ctrl = ov16a10_set_ctrl,
};

static int ov16a10_initialize_controls(struct ov16a10 *ov16a10)
{
	const struct ov16a10_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_pixel_rate = 0;
	u32 lane_num = OV16A10_LANES;

	handler = &ov16a10->ctrl_handler;
	mode = ov16a10->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &ov16a10->mutex;

	ov16a10->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			0, 0, link_freq_items);

	dst_pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * lane_num;

	ov16a10->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, OV16A10_PIXEL_RATE,
			1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(ov16a10->link_freq,
			   mode->link_freq_idx);

	h_blank = mode->hts_def - mode->width;
	ov16a10->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov16a10->hblank)
		ov16a10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov16a10->vblank = v4l2_ctrl_new_std(handler, &ov16a10_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV16A10_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov16a10->exposure = v4l2_ctrl_new_std(handler, &ov16a10_ctrl_ops,
				V4L2_CID_EXPOSURE, OV16A10_EXPOSURE_MIN,
				exposure_max, OV16A10_EXPOSURE_STEP,
				mode->exp_def);

	ov16a10->anal_gain = v4l2_ctrl_new_std(handler, &ov16a10_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV16A10_GAIN_MIN,
				OV16A10_GAIN_MAX, OV16A10_GAIN_STEP,
				OV16A10_GAIN_DEFAULT);

	ov16a10->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov16a10_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov16a10_test_pattern_menu) - 1,
				0, 0, ov16a10_test_pattern_menu);

	ov16a10->h_flip = v4l2_ctrl_new_std(handler, &ov16a10_ctrl_ops,
					    V4L2_CID_HFLIP, 0, 1, 1, 0);

	ov16a10->v_flip = v4l2_ctrl_new_std(handler, &ov16a10_ctrl_ops,
					    V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov16a10->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov16a10->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov16a10_check_sensor_id(struct ov16a10 *ov16a10,
				   struct i2c_client *client)
{
	struct device *dev = &ov16a10->client->dev;
	u32 id = 0;
	int ret;

	ret = ov16a10_read_reg(client, OV16A10_REG_CHIP_ID,
			       OV16A10_REG_VALUE_24BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov16a10_configure_regulators(struct ov16a10 *ov16a10)
{
	unsigned int i;

	for (i = 0; i < OV16A10_NUM_SUPPLIES; i++)
		ov16a10->supplies[i].supply = ov16a10_supply_names[i];

	return devm_regulator_bulk_get(&ov16a10->client->dev,
				       OV16A10_NUM_SUPPLIES,
				       ov16a10->supplies);
}

static int ov16a10_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov16a10 *ov16a10;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov16a10 = devm_kzalloc(dev, sizeof(*ov16a10), GFP_KERNEL);
	if (!ov16a10)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov16a10->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov16a10->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov16a10->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov16a10->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE,
			&hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	ov16a10->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < ov16a10->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			ov16a10->cur_mode = &supported_modes[i];
			break;
		}
	}

	ov16a10->client = client;

	ov16a10->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov16a10->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov16a10->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(ov16a10->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	ov16a10->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov16a10->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov16a10->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov16a10->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov16a10_configure_regulators(ov16a10);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	ov16a10->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov16a10->pinctrl)) {
		ov16a10->pins_default =
			pinctrl_lookup_state(ov16a10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov16a10->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov16a10->pins_sleep =
			pinctrl_lookup_state(ov16a10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov16a10->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&ov16a10->mutex);

	sd = &ov16a10->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov16a10_subdev_ops);
	ret = ov16a10_initialize_controls(ov16a10);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov16a10_power_on(ov16a10);
	if (ret)
		goto err_free_handler;

	ret = ov16a10_check_sensor_id(ov16a10, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov16a10_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov16a10->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov16a10->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov16a10->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov16a10->module_index, facing,
		 OV16A10_NAME, dev_name(sd->dev));
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
	__ov16a10_power_off(ov16a10);
err_free_handler:
	v4l2_ctrl_handler_free(&ov16a10->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov16a10->mutex);

	return ret;
}

static int ov16a10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov16a10 *ov16a10 = to_ov16a10(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov16a10->ctrl_handler);
	mutex_destroy(&ov16a10->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov16a10_power_off(ov16a10);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov16a10_of_match[] = {
	{ .compatible = "ovti,ov16a10" },
	{},
};
MODULE_DEVICE_TABLE(of, ov16a10_of_match);
#endif

static const struct i2c_device_id ov16a10_match_id[] = {
	{ "ovti,ov16a10", 0 },
	{},
};

static struct i2c_driver ov16a10_i2c_driver = {
	.driver = {
		.name = OV16A10_NAME,
		.pm = &ov16a10_pm_ops,
		.of_match_table = of_match_ptr(ov16a10_of_match),
	},
	.probe		= &ov16a10_probe,
	.remove		= &ov16a10_remove,
	.id_table	= ov16a10_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov16a10_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov16a10_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov16a10 sensor driver");
MODULE_LICENSE("GPL");
