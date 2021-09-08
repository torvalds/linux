// SPDX-License-Identifier: GPL-2.0
/*
 * imx378 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 * V0.0X01.0X01 add imx378 driver.
 * V0.0X01.0X02 add imx378 support mirror and flip.
 * V0.0X01.0X03 add quick stream on/off
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
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/rk-preisp.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x03)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define IMX378_LINK_FREQ_848		848000000// 1696Mbps

#define IMX378_LANES			4

#define PIXEL_RATE_WITH_848M_10BIT	(IMX378_LINK_FREQ_848 * 2 / 10 * 4)
#define PIXEL_RATE_WITH_848M_12BIT	(IMX378_LINK_FREQ_848 * 2 / 12 * 4)

#define IMX378_XVCLK_FREQ		24000000

#define CHIP_ID				0x0378
#define IMX378_REG_CHIP_ID_H		0x0016
#define IMX378_REG_CHIP_ID_L		0x0017

#define IMX378_REG_CTRL_MODE		0x0100
#define IMX378_MODE_SW_STANDBY		0x0
#define IMX378_MODE_STREAMING		0x1

#define IMX378_REG_EXPOSURE_H		0x0202
#define IMX378_REG_EXPOSURE_L		0x0203
#define IMX378_EXPOSURE_MIN		2
#define IMX378_EXPOSURE_STEP		1
#define IMX378_VTS_MAX			0x7fff

#define IMX378_REG_GAIN_H		0x0204
#define IMX378_REG_GAIN_L		0x0205
#define IMX378_GAIN_MIN			0x00
#define IMX378_GAIN_MAX			0x13AB
#define IMX378_GAIN_STEP		1
#define IMX378_GAIN_DEFAULT		0x0080

#define IMX378_REG_DGAIN		0x3ff9
#define IMX378_DGAIN_MODE		1
#define IMX378_REG_DGAINGR_H		0x020e
#define IMX378_REG_DGAINGR_L		0x020f
#define IMX378_REG_DGAINR_H		0x0210
#define IMX378_REG_DGAINR_L		0x0211
#define IMX378_REG_DGAINB_H		0x0212
#define IMX378_REG_DGAINB_L		0x0213
#define IMX378_REG_DGAINGB_H		0x0214
#define IMX378_REG_DGAINGB_L		0x0215
#define IMX378_REG_GAIN_GLOBAL_H	0x3ffc
#define IMX378_REG_GAIN_GLOBAL_L	0x3ffd

//#define IMX378_REG_TEST_PATTERN_H	0x0600
#define IMX378_REG_TEST_PATTERN	0x0601
#define IMX378_TEST_PATTERN_ENABLE	0x1
#define IMX378_TEST_PATTERN_DISABLE	0x0

#define IMX378_REG_VTS_H		0x0340
#define IMX378_REG_VTS_L		0x0341

#define IMX378_FLIP_MIRROR_REG		0x0101
#define IMX378_MIRROR_BIT_MASK		BIT(0)
#define IMX378_FLIP_BIT_MASK		BIT(1)

#define IMX378_FETCH_EXP_H(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX378_FETCH_EXP_L(VAL)		((VAL) & 0xFF)

#define IMX378_FETCH_AGAIN_H(VAL)		(((VAL) >> 8) & 0x03)
#define IMX378_FETCH_AGAIN_L(VAL)		((VAL) & 0xFF)

#define IMX378_FETCH_DGAIN_H(VAL)		(((VAL) >> 8) & 0x0F)
#define IMX378_FETCH_DGAIN_L(VAL)		((VAL) & 0xFF)

#define IMX378_FETCH_RHS1_H(VAL)	(((VAL) >> 16) & 0x0F)
#define IMX378_FETCH_RHS1_M(VAL)	(((VAL) >> 8) & 0xFF)
#define IMX378_FETCH_RHS1_L(VAL)	((VAL) & 0xFF)

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define IMX378_REG_VALUE_08BIT		1
#define IMX378_REG_VALUE_16BIT		2
#define IMX378_REG_VALUE_24BIT		3

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define IMX378_NAME			"imx378"

static const char * const imx378_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define IMX378_NUM_SUPPLIES ARRAY_SIZE(imx378_supply_names)

enum imx378_max_pad {
	PAD0, /* link to isp */
	PAD1, /* link to csi wr0 | hdr x2:L x3:M */
	PAD2, /* link to csi wr1 | hdr      x3:L */
	PAD3, /* link to csi wr2 | hdr x2:M x3:S */
	PAD_MAX,
};

struct regval {
	u16 addr;
	u8 val;
};

struct imx378_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct imx378 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX378_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*h_flip;
	struct v4l2_ctrl	*v_flip;
	struct v4l2_ctrl	*test_pattern;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct imx378_mode *cur_mode;
	u32			cfg_num;
	u32			cur_pixel_rate;
	u32			cur_link_freq;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	u8			flip;
};

#define to_imx378(sd) container_of(sd, struct imx378, subdev)

/*
 *IMX378LQR All-pixel scan CSI-2_4lane 24Mhz
 *AD:10bit Output:10bit 1696Mbps Master Mode 30fps
 *Tool ver : Ver4.0
 */
static const struct regval imx378_linear_10_4056x3040_regs[] = {
	{0x0101, 0x00},
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0xE000, 0x00},
	{0x4AE9, 0x18},
	{0x4AEA, 0x08},
	{0xF61C, 0x04},
	{0xF61E, 0x04},
	{0x4AE9, 0x21},
	{0x4AEA, 0x80},
	{0x38A8, 0x1F},
	{0x38A9, 0xFF},
	{0x38AA, 0x1F},
	{0x38AB, 0xFF},
	{0x55D4, 0x00},
	{0x55D5, 0x00},
	{0x55D6, 0x07},
	{0x55D7, 0xFF},
	{0x55E8, 0x07},
	{0x55E9, 0xFF},
	{0x55EA, 0x00},
	{0x55EB, 0x00},
	{0x574C, 0x07},
	{0x574D, 0xFF},
	{0x574E, 0x00},
	{0x574F, 0x00},
	{0x5754, 0x00},
	{0x5755, 0x00},
	{0x5756, 0x07},
	{0x5757, 0xFF},
	{0x5973, 0x04},
	{0x5974, 0x01},
	{0x5D13, 0xC3},
	{0x5D14, 0x58},
	{0x5D15, 0xA3},
	{0x5D16, 0x1D},
	{0x5D17, 0x65},
	{0x5D18, 0x8C},
	{0x5D1A, 0x06},
	{0x5D1B, 0xA9},
	{0x5D1C, 0x45},
	{0x5D1D, 0x3A},
	{0x5D1E, 0xAB},
	{0x5D1F, 0x15},
	{0x5D21, 0x0E},
	{0x5D22, 0x52},
	{0x5D23, 0xAA},
	{0x5D24, 0x7D},
	{0x5D25, 0x57},
	{0x5D26, 0xA8},
	{0x5D37, 0x5A},
	{0x5D38, 0x5A},
	{0x5D77, 0x7F},
	{0x7B75, 0x0E},
	{0x7B76, 0x0B},
	{0x7B77, 0x08},
	{0x7B78, 0x0A},
	{0x7B79, 0x47},
	{0x7B7C, 0x00},
	{0x7B7D, 0x00},
	{0x8D1F, 0x00},
	{0x8D27, 0x00},
	{0x9004, 0x03},
	{0x9200, 0x50},
	{0x9201, 0x6C},
	{0x9202, 0x71},
	{0x9203, 0x00},
	{0x9204, 0x71},
	{0x9205, 0x01},
	{0x9371, 0x6A},
	{0x9373, 0x6A},
	{0x9375, 0x64},
	{0x991A, 0x00},
	{0x996B, 0x8C},
	{0x996C, 0x64},
	{0x996D, 0x50},
	{0x9A4C, 0x0D},
	{0x9A4D, 0x0D},
	{0xA001, 0x0A},
	{0xA003, 0x0A},
	{0xA005, 0x0A},
	{0xA006, 0x01},
	{0xA007, 0xC0},
	{0xA009, 0xC0},

	{0x3D8A, 0x01},
	{0x4421, 0x08},
	{0x7B3B, 0x01},
	{0x7B4C, 0x00},
	{0x9905, 0x00},
	{0x9907, 0x00},
	{0x9909, 0x00},
	{0x990B, 0x00},
	{0x9944, 0x3C},
	{0x9947, 0x3C},
	{0x994A, 0x8C},
	{0x994B, 0x50},
	{0x994C, 0x1B},
	{0x994D, 0x8C},
	{0x994E, 0x50},
	{0x994F, 0x1B},
	{0x9950, 0x8C},
	{0x9951, 0x1B},
	{0x9952, 0x0A},
	{0x9953, 0x8C},
	{0x9954, 0x1B},
	{0x9955, 0x0A},
	{0x9A13, 0x04},
	{0x9A14, 0x04},
	{0x9A19, 0x00},
	{0x9A1C, 0x04},
	{0x9A1D, 0x04},
	{0x9A26, 0x05},
	{0x9A27, 0x05},
	{0x9A2C, 0x01},
	{0x9A2D, 0x03},
	{0x9A2F, 0x05},
	{0x9A30, 0x05},
	{0x9A41, 0x00},
	{0x9A46, 0x00},
	{0x9A47, 0x00},
	{0x9C17, 0x35},
	{0x9C1D, 0x31},
	{0x9C29, 0x50},
	{0x9C3B, 0x2F},
	{0x9C41, 0x6B},
	{0x9C47, 0x2D},
	{0x9C4D, 0x40},
	{0x9C6B, 0x00},
	{0x9C71, 0xC8},
	{0x9C73, 0x32},
	{0x9C75, 0x04},
	{0x9C7D, 0x2D},
	{0x9C83, 0x40},
	{0x9C94, 0x3F},
	{0x9C95, 0x3F},
	{0x9C96, 0x3F},
	{0x9C97, 0x00},
	{0x9C98, 0x00},
	{0x9C99, 0x00},
	{0x9C9A, 0x3F},
	{0x9C9B, 0x3F},
	{0x9C9C, 0x3F},
	{0x9CA0, 0x0F},
	{0x9CA1, 0x0F},
	{0x9CA2, 0x0F},
	{0x9CA3, 0x00},
	{0x9CA4, 0x00},
	{0x9CA5, 0x00},
	{0x9CA6, 0x1E},
	{0x9CA7, 0x1E},
	{0x9CA8, 0x1E},
	{0x9CA9, 0x00},
	{0x9CAA, 0x00},
	{0x9CAB, 0x00},
	{0x9CAC, 0x09},
	{0x9CAD, 0x09},
	{0x9CAE, 0x09},
	{0x9CBD, 0x50},
	{0x9CBF, 0x50},
	{0x9CC1, 0x50},
	{0x9CC3, 0x40},
	{0x9CC5, 0x40},
	{0x9CC7, 0x40},
	{0x9CC9, 0x0A},
	{0x9CCB, 0x0A},
	{0x9CCD, 0x0A},
	{0x9D17, 0x35},
	{0x9D1D, 0x31},
	{0x9D29, 0x50},
	{0x9D3B, 0x2F},
	{0x9D41, 0x6B},
	{0x9D47, 0x42},
	{0x9D4D, 0x5A},
	{0x9D6B, 0x00},
	{0x9D71, 0xC8},
	{0x9D73, 0x32},
	{0x9D75, 0x04},
	{0x9D7D, 0x42},
	{0x9D83, 0x5A},
	{0x9D94, 0x3F},
	{0x9D95, 0x3F},
	{0x9D96, 0x3F},
	{0x9D97, 0x00},
	{0x9D98, 0x00},
	{0x9D99, 0x00},
	{0x9D9A, 0x3F},
	{0x9D9B, 0x3F},
	{0x9D9C, 0x3F},
	{0x9D9D, 0x1F},
	{0x9D9E, 0x1F},
	{0x9D9F, 0x1F},
	{0x9DA0, 0x0F},
	{0x9DA1, 0x0F},
	{0x9DA2, 0x0F},
	{0x9DA3, 0x00},
	{0x9DA4, 0x00},
	{0x9DA5, 0x00},
	{0x9DA6, 0x1E},
	{0x9DA7, 0x1E},
	{0x9DA8, 0x1E},
	{0x9DA9, 0x00},
	{0x9DAA, 0x00},
	{0x9DAB, 0x00},
	{0x9DAC, 0x09},
	{0x9DAD, 0x09},
	{0x9DAE, 0x09},
	{0x9DC9, 0x0A},
	{0x9DCB, 0x0A},
	{0x9DCD, 0x0A},
	{0x9E17, 0x35},
	{0x9E1D, 0x31},
	{0x9E29, 0x50},
	{0x9E3B, 0x2F},
	{0x9E41, 0x6B},
	{0x9E47, 0x2D},
	{0x9E4D, 0x40},
	{0x9E6B, 0x00},
	{0x9E71, 0xC8},
	{0x9E73, 0x32},
	{0x9E75, 0x04},
	{0x9E94, 0x0F},
	{0x9E95, 0x0F},
	{0x9E96, 0x0F},
	{0x9E97, 0x00},
	{0x9E98, 0x00},
	{0x9E99, 0x00},
	{0x9EA0, 0x0F},
	{0x9EA1, 0x0F},
	{0x9EA2, 0x0F},
	{0x9EA3, 0x00},
	{0x9EA4, 0x00},
	{0x9EA5, 0x00},
	{0x9EA6, 0x3F},
	{0x9EA7, 0x3F},
	{0x9EA8, 0x3F},
	{0x9EA9, 0x00},
	{0x9EAA, 0x00},
	{0x9EAB, 0x00},
	{0x9EAC, 0x09},
	{0x9EAD, 0x09},
	{0x9EAE, 0x09},
	{0x9EC9, 0x0A},
	{0x9ECB, 0x0A},
	{0x9ECD, 0x0A},
	{0x9F17, 0x35},
	{0x9F1D, 0x31},
	{0x9F29, 0x50},
	{0x9F3B, 0x2F},
	{0x9F41, 0x6B},
	{0x9F47, 0x42},
	{0x9F4D, 0x5A},
	{0x9F6B, 0x00},
	{0x9F71, 0xC8},
	{0x9F73, 0x32},
	{0x9F75, 0x04},
	{0x9F94, 0x0F},
	{0x9F95, 0x0F},
	{0x9F96, 0x0F},
	{0x9F97, 0x00},
	{0x9F98, 0x00},
	{0x9F99, 0x00},
	{0x9F9A, 0x2F},
	{0x9F9B, 0x2F},
	{0x9F9C, 0x2F},
	{0x9F9D, 0x00},
	{0x9F9E, 0x00},
	{0x9F9F, 0x00},
	{0x9FA0, 0x0F},
	{0x9FA1, 0x0F},
	{0x9FA2, 0x0F},
	{0x9FA3, 0x00},
	{0x9FA4, 0x00},
	{0x9FA5, 0x00},
	{0x9FA6, 0x1E},
	{0x9FA7, 0x1E},
	{0x9FA8, 0x1E},
	{0x9FA9, 0x00},
	{0x9FAA, 0x00},
	{0x9FAB, 0x00},
	{0x9FAC, 0x09},
	{0x9FAD, 0x09},
	{0x9FAE, 0x09},
	{0x9FC9, 0x0A},
	{0x9FCB, 0x0A},
	{0x9FCD, 0x0A},
	{0xA14B, 0xFF},
	{0xA151, 0x0C},
	{0xA153, 0x50},
	{0xA155, 0x02},
	{0xA157, 0x00},
	{0xA1AD, 0xFF},
	{0xA1B3, 0x0C},
	{0xA1B5, 0x50},
	{0xA1B9, 0x00},
	{0xA24B, 0xFF},
	{0xA257, 0x00},
	{0xA2AD, 0xFF},
	{0xA2B9, 0x00},
	{0xB21F, 0x04},
	{0xB35C, 0x00},
	{0xB35E, 0x08},

	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},
	{0x0342, 0x16},
	{0x0343, 0xA8},
	{0x0340, 0x0F},
	{0x0341, 0x3C},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0F},
	{0x0349, 0xD7},
	{0x034A, 0x0B},
	{0x034B, 0xDF},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x02},
	{0x3140, 0x02},
	{0x3C00, 0x00},
	{0x3C01, 0x03},
	{0x3C02, 0xDC},
	{0x3F0D, 0x00},
	{0x5748, 0x07},
	{0x5749, 0xFF},
	{0x574A, 0x00},
	{0x574B, 0x00},
	{0x7B53, 0x01},
	{0x9369, 0x5A},
	{0x936B, 0x55},
	{0x936D, 0x28},
	{0x9304, 0x03},
	{0x9305, 0x00},
	{0x9E9A, 0x2F},
	{0x9E9B, 0x2F},
	{0x9E9C, 0x2F},
	{0x9E9D, 0x00},
	{0x9E9E, 0x00},
	{0x9E9F, 0x00},
	{0xA2A9, 0x60},
	{0xA2B7, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x0F},
	{0x040D, 0xD8},
	{0x040E, 0x0B},
	{0x040F, 0xE0},
	{0x034C, 0x0F},
	{0x034D, 0xD8},
	{0x034E, 0x0B},
	{0x034F, 0xE0},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x03},
	{0x0306, 0x00},
	{0x0307, 0xD4},
	{0x0309, 0x0A},
	{0x030B, 0x01},
	{0x030D, 0x02},
	{0x030E, 0x01},
	{0x030F, 0x5E},
	{0x0310, 0x00},
	{0x0820, 0x1A},
	{0x0821, 0x80},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3E20, 0x01},
	{0x3E37, 0x01},
	{0x3F50, 0x00},
	{0x3F56, 0x00},
	{0x3F57, 0xA0},
	{REG_NULL, 0x00},
};

static const struct regval imx378_linear_10_3840x2160_regs[] = {
	{0x0101, 0x00},
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0xE000, 0x00},
	{0x4AE9, 0x18},
	{0x4AEA, 0x08},
	{0xF61C, 0x04},
	{0xF61E, 0x04},
	{0x4AE9, 0x21},
	{0x4AEA, 0x80},
	{0x38A8, 0x1F},
	{0x38A9, 0xFF},
	{0x38AA, 0x1F},
	{0x38AB, 0xFF},
	{0x55D4, 0x00},
	{0x55D5, 0x00},
	{0x55D6, 0x07},
	{0x55D7, 0xFF},
	{0x55E8, 0x07},
	{0x55E9, 0xFF},
	{0x55EA, 0x00},
	{0x55EB, 0x00},
	{0x574C, 0x07},
	{0x574D, 0xFF},
	{0x574E, 0x00},
	{0x574F, 0x00},
	{0x5754, 0x00},
	{0x5755, 0x00},
	{0x5756, 0x07},
	{0x5757, 0xFF},
	{0x5973, 0x04},
	{0x5974, 0x01},
	{0x5D13, 0xC3},
	{0x5D14, 0x58},
	{0x5D15, 0xA3},
	{0x5D16, 0x1D},
	{0x5D17, 0x65},
	{0x5D18, 0x8C},
	{0x5D1A, 0x06},
	{0x5D1B, 0xA9},
	{0x5D1C, 0x45},
	{0x5D1D, 0x3A},
	{0x5D1E, 0xAB},
	{0x5D1F, 0x15},
	{0x5D21, 0x0E},
	{0x5D22, 0x52},
	{0x5D23, 0xAA},
	{0x5D24, 0x7D},
	{0x5D25, 0x57},
	{0x5D26, 0xA8},
	{0x5D37, 0x5A},
	{0x5D38, 0x5A},
	{0x5D77, 0x7F},
	{0x7B75, 0x0E},
	{0x7B76, 0x0B},
	{0x7B77, 0x08},
	{0x7B78, 0x0A},
	{0x7B79, 0x47},
	{0x7B7C, 0x00},
	{0x7B7D, 0x00},
	{0x8D1F, 0x00},
	{0x8D27, 0x00},
	{0x9004, 0x03},
	{0x9200, 0x50},
	{0x9201, 0x6C},
	{0x9202, 0x71},
	{0x9203, 0x00},
	{0x9204, 0x71},
	{0x9205, 0x01},
	{0x9371, 0x6A},
	{0x9373, 0x6A},
	{0x9375, 0x64},
	{0x991A, 0x00},
	{0x996B, 0x8C},
	{0x996C, 0x64},
	{0x996D, 0x50},
	{0x9A4C, 0x0D},
	{0x9A4D, 0x0D},
	{0xA001, 0x0A},
	{0xA003, 0x0A},
	{0xA005, 0x0A},
	{0xA006, 0x01},
	{0xA007, 0xC0},
	{0xA009, 0xC0},

	{0x3D8A, 0x01},
	{0x4421, 0x08},
	{0x7B3B, 0x01},
	{0x7B4C, 0x00},
	{0x9905, 0x00},
	{0x9907, 0x00},
	{0x9909, 0x00},
	{0x990B, 0x00},
	{0x9944, 0x3C},
	{0x9947, 0x3C},
	{0x994A, 0x8C},
	{0x994B, 0x50},
	{0x994C, 0x1B},
	{0x994D, 0x8C},
	{0x994E, 0x50},
	{0x994F, 0x1B},
	{0x9950, 0x8C},
	{0x9951, 0x1B},
	{0x9952, 0x0A},
	{0x9953, 0x8C},
	{0x9954, 0x1B},
	{0x9955, 0x0A},
	{0x9A13, 0x04},
	{0x9A14, 0x04},
	{0x9A19, 0x00},
	{0x9A1C, 0x04},
	{0x9A1D, 0x04},
	{0x9A26, 0x05},
	{0x9A27, 0x05},
	{0x9A2C, 0x01},
	{0x9A2D, 0x03},
	{0x9A2F, 0x05},
	{0x9A30, 0x05},
	{0x9A41, 0x00},
	{0x9A46, 0x00},
	{0x9A47, 0x00},
	{0x9C17, 0x35},
	{0x9C1D, 0x31},
	{0x9C29, 0x50},
	{0x9C3B, 0x2F},
	{0x9C41, 0x6B},
	{0x9C47, 0x2D},
	{0x9C4D, 0x40},
	{0x9C6B, 0x00},
	{0x9C71, 0xC8},
	{0x9C73, 0x32},
	{0x9C75, 0x04},
	{0x9C7D, 0x2D},
	{0x9C83, 0x40},
	{0x9C94, 0x3F},
	{0x9C95, 0x3F},
	{0x9C96, 0x3F},
	{0x9C97, 0x00},
	{0x9C98, 0x00},
	{0x9C99, 0x00},
	{0x9C9A, 0x3F},
	{0x9C9B, 0x3F},
	{0x9C9C, 0x3F},
	{0x9CA0, 0x0F},
	{0x9CA1, 0x0F},
	{0x9CA2, 0x0F},
	{0x9CA3, 0x00},
	{0x9CA4, 0x00},
	{0x9CA5, 0x00},
	{0x9CA6, 0x1E},
	{0x9CA7, 0x1E},
	{0x9CA8, 0x1E},
	{0x9CA9, 0x00},
	{0x9CAA, 0x00},
	{0x9CAB, 0x00},
	{0x9CAC, 0x09},
	{0x9CAD, 0x09},
	{0x9CAE, 0x09},
	{0x9CBD, 0x50},
	{0x9CBF, 0x50},
	{0x9CC1, 0x50},
	{0x9CC3, 0x40},
	{0x9CC5, 0x40},
	{0x9CC7, 0x40},
	{0x9CC9, 0x0A},
	{0x9CCB, 0x0A},
	{0x9CCD, 0x0A},
	{0x9D17, 0x35},
	{0x9D1D, 0x31},
	{0x9D29, 0x50},
	{0x9D3B, 0x2F},
	{0x9D41, 0x6B},
	{0x9D47, 0x42},
	{0x9D4D, 0x5A},
	{0x9D6B, 0x00},
	{0x9D71, 0xC8},
	{0x9D73, 0x32},
	{0x9D75, 0x04},
	{0x9D7D, 0x42},
	{0x9D83, 0x5A},
	{0x9D94, 0x3F},
	{0x9D95, 0x3F},
	{0x9D96, 0x3F},
	{0x9D97, 0x00},
	{0x9D98, 0x00},
	{0x9D99, 0x00},
	{0x9D9A, 0x3F},
	{0x9D9B, 0x3F},
	{0x9D9C, 0x3F},
	{0x9D9D, 0x1F},
	{0x9D9E, 0x1F},
	{0x9D9F, 0x1F},
	{0x9DA0, 0x0F},
	{0x9DA1, 0x0F},
	{0x9DA2, 0x0F},
	{0x9DA3, 0x00},
	{0x9DA4, 0x00},
	{0x9DA5, 0x00},
	{0x9DA6, 0x1E},
	{0x9DA7, 0x1E},
	{0x9DA8, 0x1E},
	{0x9DA9, 0x00},
	{0x9DAA, 0x00},
	{0x9DAB, 0x00},
	{0x9DAC, 0x09},
	{0x9DAD, 0x09},
	{0x9DAE, 0x09},
	{0x9DC9, 0x0A},
	{0x9DCB, 0x0A},
	{0x9DCD, 0x0A},
	{0x9E17, 0x35},
	{0x9E1D, 0x31},
	{0x9E29, 0x50},
	{0x9E3B, 0x2F},
	{0x9E41, 0x6B},
	{0x9E47, 0x2D},
	{0x9E4D, 0x40},
	{0x9E6B, 0x00},
	{0x9E71, 0xC8},
	{0x9E73, 0x32},
	{0x9E75, 0x04},
	{0x9E94, 0x0F},
	{0x9E95, 0x0F},
	{0x9E96, 0x0F},
	{0x9E97, 0x00},
	{0x9E98, 0x00},
	{0x9E99, 0x00},
	{0x9EA0, 0x0F},
	{0x9EA1, 0x0F},
	{0x9EA2, 0x0F},
	{0x9EA3, 0x00},
	{0x9EA4, 0x00},
	{0x9EA5, 0x00},
	{0x9EA6, 0x3F},
	{0x9EA7, 0x3F},
	{0x9EA8, 0x3F},
	{0x9EA9, 0x00},
	{0x9EAA, 0x00},
	{0x9EAB, 0x00},
	{0x9EAC, 0x09},
	{0x9EAD, 0x09},
	{0x9EAE, 0x09},
	{0x9EC9, 0x0A},
	{0x9ECB, 0x0A},
	{0x9ECD, 0x0A},
	{0x9F17, 0x35},
	{0x9F1D, 0x31},
	{0x9F29, 0x50},
	{0x9F3B, 0x2F},
	{0x9F41, 0x6B},
	{0x9F47, 0x42},
	{0x9F4D, 0x5A},
	{0x9F6B, 0x00},
	{0x9F71, 0xC8},
	{0x9F73, 0x32},
	{0x9F75, 0x04},
	{0x9F94, 0x0F},
	{0x9F95, 0x0F},
	{0x9F96, 0x0F},
	{0x9F97, 0x00},
	{0x9F98, 0x00},
	{0x9F99, 0x00},
	{0x9F9A, 0x2F},
	{0x9F9B, 0x2F},
	{0x9F9C, 0x2F},
	{0x9F9D, 0x00},
	{0x9F9E, 0x00},
	{0x9F9F, 0x00},
	{0x9FA0, 0x0F},
	{0x9FA1, 0x0F},
	{0x9FA2, 0x0F},
	{0x9FA3, 0x00},
	{0x9FA4, 0x00},
	{0x9FA5, 0x00},
	{0x9FA6, 0x1E},
	{0x9FA7, 0x1E},
	{0x9FA8, 0x1E},
	{0x9FA9, 0x00},
	{0x9FAA, 0x00},
	{0x9FAB, 0x00},
	{0x9FAC, 0x09},
	{0x9FAD, 0x09},
	{0x9FAE, 0x09},
	{0x9FC9, 0x0A},
	{0x9FCB, 0x0A},
	{0x9FCD, 0x0A},
	{0xA14B, 0xFF},
	{0xA151, 0x0C},
	{0xA153, 0x50},
	{0xA155, 0x02},
	{0xA157, 0x00},
	{0xA1AD, 0xFF},
	{0xA1B3, 0x0C},
	{0xA1B5, 0x50},
	{0xA1B9, 0x00},
	{0xA24B, 0xFF},
	{0xA257, 0x00},
	{0xA2AD, 0xFF},
	{0xA2B9, 0x00},
	{0xB21F, 0x04},
	{0xB35C, 0x00},
	{0xB35E, 0x08},

	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},
	{0x0342, 0x16},
	{0x0343, 0xA8},
	{0x0340, 0x0F},
	{0x0341, 0x3C},
	{0x0344, 0x00},
	{0x0345, 0x6C},
	{0x0346, 0x01},
	{0x0347, 0xB8},
	{0x0348, 0x0F},
	{0x0349, 0x6B},
	{0x034A, 0x0A},
	{0x034B, 0x27},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x02},
	{0x3140, 0x02},
	{0x3C00, 0x00},
	{0x3C01, 0x03},
	{0x3C02, 0xDC},
	{0x3F0D, 0x00},
	{0x5748, 0x07},
	{0x5749, 0xFF},
	{0x574A, 0x00},
	{0x574B, 0x00},
	{0x7B53, 0x01},
	{0x9369, 0x5A},
	{0x936B, 0x55},
	{0x936D, 0x28},
	{0x9304, 0x03},
	{0x9305, 0x00},
	{0x9E9A, 0x2F},
	{0x9E9B, 0x2F},
	{0x9E9C, 0x2F},
	{0x9E9D, 0x00},
	{0x9E9E, 0x00},
	{0x9E9F, 0x00},
	{0xA2A9, 0x60},
	{0xA2B7, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x0F},
	{0x040D, 0x00},
	{0x040E, 0x08},
	{0x040F, 0x70},
	{0x034C, 0x0F},
	{0x034D, 0x00},
	{0x034E, 0x08},
	{0x034F, 0x70},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x03},
	{0x0306, 0x00},
	{0x0307, 0xD4},
	{0x0309, 0x0A},
	{0x030B, 0x01},
	{0x030D, 0x02},
	{0x030E, 0x01},
	{0x030F, 0x5E},
	{0x0310, 0x00},
	{0x0820, 0x1A},
	{0x0821, 0x80},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3E20, 0x01},
	{0x3E37, 0x01},
	{0x3F50, 0x00},
	{0x3F56, 0x00},
	{0x3F57, 0xA0},
	{REG_NULL, 0x00},
};

/*
 *IMX378LQR All-pixel scan CSI-2_4lane 24Mhz
 *AD:12bit Output:12bit 1696Mbps Master Mode 30fps
 *Tool ver : Ver4.0
 */
static const struct regval imx378_linear_12_4056x3040_regs[] = {
	{0x0101, 0x00},
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0xE000, 0x00},
	{0x4AE9, 0x18},
	{0x4AEA, 0x08},
	{0xF61C, 0x04},
	{0xF61E, 0x04},
	{0x4AE9, 0x21},
	{0x4AEA, 0x80},
	{0x38A8, 0x1F},
	{0x38A9, 0xFF},
	{0x38AA, 0x1F},
	{0x38AB, 0xFF},
	{0x55D4, 0x00},
	{0x55D5, 0x00},
	{0x55D6, 0x07},
	{0x55D7, 0xFF},
	{0x55E8, 0x07},
	{0x55E9, 0xFF},
	{0x55EA, 0x00},
	{0x55EB, 0x00},
	{0x574C, 0x07},
	{0x574D, 0xFF},
	{0x574E, 0x00},
	{0x574F, 0x00},
	{0x5754, 0x00},
	{0x5755, 0x00},
	{0x5756, 0x07},
	{0x5757, 0xFF},
	{0x5973, 0x04},
	{0x5974, 0x01},
	{0x5D13, 0xC3},
	{0x5D14, 0x58},
	{0x5D15, 0xA3},
	{0x5D16, 0x1D},
	{0x5D17, 0x65},
	{0x5D18, 0x8C},
	{0x5D1A, 0x06},
	{0x5D1B, 0xA9},
	{0x5D1C, 0x45},
	{0x5D1D, 0x3A},
	{0x5D1E, 0xAB},
	{0x5D1F, 0x15},
	{0x5D21, 0x0E},
	{0x5D22, 0x52},
	{0x5D23, 0xAA},
	{0x5D24, 0x7D},
	{0x5D25, 0x57},
	{0x5D26, 0xA8},
	{0x5D37, 0x5A},
	{0x5D38, 0x5A},
	{0x5D77, 0x7F},
	{0x7B75, 0x0E},
	{0x7B76, 0x0B},
	{0x7B77, 0x08},
	{0x7B78, 0x0A},
	{0x7B79, 0x47},
	{0x7B7C, 0x00},
	{0x7B7D, 0x00},
	{0x8D1F, 0x00},
	{0x8D27, 0x00},
	{0x9004, 0x03},
	{0x9200, 0x50},
	{0x9201, 0x6C},
	{0x9202, 0x71},
	{0x9203, 0x00},
	{0x9204, 0x71},
	{0x9205, 0x01},
	{0x9371, 0x6A},
	{0x9373, 0x6A},
	{0x9375, 0x64},
	{0x991A, 0x00},
	{0x996B, 0x8C},
	{0x996C, 0x64},
	{0x996D, 0x50},
	{0x9A4C, 0x0D},
	{0x9A4D, 0x0D},
	{0xA001, 0x0A},
	{0xA003, 0x0A},
	{0xA005, 0x0A},
	{0xA006, 0x01},
	{0xA007, 0xC0},
	{0xA009, 0xC0},

	{0x3D8A, 0x01},
	{0x4421, 0x08},
	{0x7B3B, 0x01},
	{0x7B4C, 0x00},
	{0x9905, 0x00},
	{0x9907, 0x00},
	{0x9909, 0x00},
	{0x990B, 0x00},
	{0x9944, 0x3C},
	{0x9947, 0x3C},
	{0x994A, 0x8C},
	{0x994B, 0x50},
	{0x994C, 0x1B},
	{0x994D, 0x8C},
	{0x994E, 0x50},
	{0x994F, 0x1B},
	{0x9950, 0x8C},
	{0x9951, 0x1B},
	{0x9952, 0x0A},
	{0x9953, 0x8C},
	{0x9954, 0x1B},
	{0x9955, 0x0A},
	{0x9A13, 0x04},
	{0x9A14, 0x04},
	{0x9A19, 0x00},
	{0x9A1C, 0x04},
	{0x9A1D, 0x04},
	{0x9A26, 0x05},
	{0x9A27, 0x05},
	{0x9A2C, 0x01},
	{0x9A2D, 0x03},
	{0x9A2F, 0x05},
	{0x9A30, 0x05},
	{0x9A41, 0x00},
	{0x9A46, 0x00},
	{0x9A47, 0x00},
	{0x9C17, 0x35},
	{0x9C1D, 0x31},
	{0x9C29, 0x50},
	{0x9C3B, 0x2F},
	{0x9C41, 0x6B},
	{0x9C47, 0x2D},
	{0x9C4D, 0x40},
	{0x9C6B, 0x00},
	{0x9C71, 0xC8},
	{0x9C73, 0x32},
	{0x9C75, 0x04},
	{0x9C7D, 0x2D},
	{0x9C83, 0x40},
	{0x9C94, 0x3F},
	{0x9C95, 0x3F},
	{0x9C96, 0x3F},
	{0x9C97, 0x00},
	{0x9C98, 0x00},
	{0x9C99, 0x00},
	{0x9C9A, 0x3F},
	{0x9C9B, 0x3F},
	{0x9C9C, 0x3F},
	{0x9CA0, 0x0F},
	{0x9CA1, 0x0F},
	{0x9CA2, 0x0F},
	{0x9CA3, 0x00},
	{0x9CA4, 0x00},
	{0x9CA5, 0x00},
	{0x9CA6, 0x1E},
	{0x9CA7, 0x1E},
	{0x9CA8, 0x1E},
	{0x9CA9, 0x00},
	{0x9CAA, 0x00},
	{0x9CAB, 0x00},
	{0x9CAC, 0x09},
	{0x9CAD, 0x09},
	{0x9CAE, 0x09},
	{0x9CBD, 0x50},
	{0x9CBF, 0x50},
	{0x9CC1, 0x50},
	{0x9CC3, 0x40},
	{0x9CC5, 0x40},
	{0x9CC7, 0x40},
	{0x9CC9, 0x0A},
	{0x9CCB, 0x0A},
	{0x9CCD, 0x0A},
	{0x9D17, 0x35},
	{0x9D1D, 0x31},
	{0x9D29, 0x50},
	{0x9D3B, 0x2F},
	{0x9D41, 0x6B},
	{0x9D47, 0x42},
	{0x9D4D, 0x5A},
	{0x9D6B, 0x00},
	{0x9D71, 0xC8},
	{0x9D73, 0x32},
	{0x9D75, 0x04},
	{0x9D7D, 0x42},
	{0x9D83, 0x5A},
	{0x9D94, 0x3F},
	{0x9D95, 0x3F},
	{0x9D96, 0x3F},
	{0x9D97, 0x00},
	{0x9D98, 0x00},
	{0x9D99, 0x00},
	{0x9D9A, 0x3F},
	{0x9D9B, 0x3F},
	{0x9D9C, 0x3F},
	{0x9D9D, 0x1F},
	{0x9D9E, 0x1F},
	{0x9D9F, 0x1F},
	{0x9DA0, 0x0F},
	{0x9DA1, 0x0F},
	{0x9DA2, 0x0F},
	{0x9DA3, 0x00},
	{0x9DA4, 0x00},
	{0x9DA5, 0x00},
	{0x9DA6, 0x1E},
	{0x9DA7, 0x1E},
	{0x9DA8, 0x1E},
	{0x9DA9, 0x00},
	{0x9DAA, 0x00},
	{0x9DAB, 0x00},
	{0x9DAC, 0x09},
	{0x9DAD, 0x09},
	{0x9DAE, 0x09},
	{0x9DC9, 0x0A},
	{0x9DCB, 0x0A},
	{0x9DCD, 0x0A},
	{0x9E17, 0x35},
	{0x9E1D, 0x31},
	{0x9E29, 0x50},
	{0x9E3B, 0x2F},
	{0x9E41, 0x6B},
	{0x9E47, 0x2D},
	{0x9E4D, 0x40},
	{0x9E6B, 0x00},
	{0x9E71, 0xC8},
	{0x9E73, 0x32},
	{0x9E75, 0x04},
	{0x9E94, 0x0F},
	{0x9E95, 0x0F},
	{0x9E96, 0x0F},
	{0x9E97, 0x00},
	{0x9E98, 0x00},
	{0x9E99, 0x00},
	{0x9EA0, 0x0F},
	{0x9EA1, 0x0F},
	{0x9EA2, 0x0F},
	{0x9EA3, 0x00},
	{0x9EA4, 0x00},
	{0x9EA5, 0x00},
	{0x9EA6, 0x3F},
	{0x9EA7, 0x3F},
	{0x9EA8, 0x3F},
	{0x9EA9, 0x00},
	{0x9EAA, 0x00},
	{0x9EAB, 0x00},
	{0x9EAC, 0x09},
	{0x9EAD, 0x09},
	{0x9EAE, 0x09},
	{0x9EC9, 0x0A},
	{0x9ECB, 0x0A},
	{0x9ECD, 0x0A},
	{0x9F17, 0x35},
	{0x9F1D, 0x31},
	{0x9F29, 0x50},
	{0x9F3B, 0x2F},
	{0x9F41, 0x6B},
	{0x9F47, 0x42},
	{0x9F4D, 0x5A},
	{0x9F6B, 0x00},
	{0x9F71, 0xC8},
	{0x9F73, 0x32},
	{0x9F75, 0x04},
	{0x9F94, 0x0F},
	{0x9F95, 0x0F},
	{0x9F96, 0x0F},
	{0x9F97, 0x00},
	{0x9F98, 0x00},
	{0x9F99, 0x00},
	{0x9F9A, 0x2F},
	{0x9F9B, 0x2F},
	{0x9F9C, 0x2F},
	{0x9F9D, 0x00},
	{0x9F9E, 0x00},
	{0x9F9F, 0x00},
	{0x9FA0, 0x0F},
	{0x9FA1, 0x0F},
	{0x9FA2, 0x0F},
	{0x9FA3, 0x00},
	{0x9FA4, 0x00},
	{0x9FA5, 0x00},
	{0x9FA6, 0x1E},
	{0x9FA7, 0x1E},
	{0x9FA8, 0x1E},
	{0x9FA9, 0x00},
	{0x9FAA, 0x00},
	{0x9FAB, 0x00},
	{0x9FAC, 0x09},
	{0x9FAD, 0x09},
	{0x9FAE, 0x09},
	{0x9FC9, 0x0A},
	{0x9FCB, 0x0A},
	{0x9FCD, 0x0A},
	{0xA14B, 0xFF},
	{0xA151, 0x0C},
	{0xA153, 0x50},
	{0xA155, 0x02},
	{0xA157, 0x00},
	{0xA1AD, 0xFF},
	{0xA1B3, 0x0C},
	{0xA1B5, 0x50},
	{0xA1B9, 0x00},
	{0xA24B, 0xFF},
	{0xA257, 0x00},
	{0xA2AD, 0xFF},
	{0xA2B9, 0x00},
	{0xB21F, 0x04},
	{0xB35C, 0x00},
	{0xB35E, 0x08},

	{0x0112, 0x0C},
	{0x0113, 0x0C},
	{0x0114, 0x03},
	{0x0342, 0x1B},
	{0x0343, 0xD8},
	{0x0340, 0x0F},
	{0x0341, 0x57},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0F},
	{0x0349, 0xD7},
	{0x034A, 0x0B},
	{0x034B, 0xDF},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x02},
	{0x3140, 0x02},
	{0x3C00, 0x00},
	{0x3C01, 0x03},
	{0x3C02, 0xA2},
	{0x3F0D, 0x01},
	{0x5748, 0x07},
	{0x5749, 0xFF},
	{0x574A, 0x00},
	{0x574B, 0x00},
	{0x7B53, 0x01},
	{0x9369, 0x5A},
	{0x936B, 0x55},
	{0x936D, 0x28},
	{0x9304, 0x03},
	{0x9305, 0x00},
	{0x9E9A, 0x2F},
	{0x9E9B, 0x2F},
	{0x9E9C, 0x2F},
	{0x9E9D, 0x00},
	{0x9E9E, 0x00},
	{0x9E9F, 0x00},
	{0xA2A9, 0x60},
	{0xA2B7, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x0F},
	{0x040D, 0xD8},
	{0x040E, 0x0B},
	{0x040F, 0xE0},
	{0x034C, 0x0F},
	{0x034D, 0xD8},
	{0x034E, 0x0B},
	{0x034F, 0xE0},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x02},
	{0x0306, 0x00},
	{0x0307, 0xAF},
	{0x0309, 0x0C},
	{0x030B, 0x01},
	{0x030D, 0x03},
	{0x030E, 0x00},
	{0x030F, 0xD4},
	{0x0310, 0x01},
	{0x0820, 0x1A},
	{0x0821, 0x80},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3E20, 0x01},
	{0x3E37, 0x01},
	{0x3F50, 0x00},
	{0x3F56, 0x00},
	{0x3F57, 0xB8},
	{REG_NULL, 0x00},
};

static const struct regval imx378_linear_12_2028x1520_regs[] = {
	{0x0101, 0x00},
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0xE000, 0x00},
	{0x4AE9, 0x18},
	{0x4AEA, 0x08},
	{0xF61C, 0x04},
	{0xF61E, 0x04},
	{0x4AE9, 0x21},
	{0x4AEA, 0x80},
	{0x38A8, 0x1F},
	{0x38A9, 0xFF},
	{0x38AA, 0x1F},
	{0x38AB, 0xFF},
	{0x55D4, 0x00},
	{0x55D5, 0x00},
	{0x55D6, 0x07},
	{0x55D7, 0xFF},
	{0x55E8, 0x07},
	{0x55E9, 0xFF},
	{0x55EA, 0x00},
	{0x55EB, 0x00},
	{0x574C, 0x07},
	{0x574D, 0xFF},
	{0x574E, 0x00},
	{0x574F, 0x00},
	{0x5754, 0x00},
	{0x5755, 0x00},
	{0x5756, 0x07},
	{0x5757, 0xFF},
	{0x5973, 0x04},
	{0x5974, 0x01},
	{0x5D13, 0xC3},
	{0x5D14, 0x58},
	{0x5D15, 0xA3},
	{0x5D16, 0x1D},
	{0x5D17, 0x65},
	{0x5D18, 0x8C},
	{0x5D1A, 0x06},
	{0x5D1B, 0xA9},
	{0x5D1C, 0x45},
	{0x5D1D, 0x3A},
	{0x5D1E, 0xAB},
	{0x5D1F, 0x15},
	{0x5D21, 0x0E},
	{0x5D22, 0x52},
	{0x5D23, 0xAA},
	{0x5D24, 0x7D},
	{0x5D25, 0x57},
	{0x5D26, 0xA8},
	{0x5D37, 0x5A},
	{0x5D38, 0x5A},
	{0x5D77, 0x7F},
	{0x7B75, 0x0E},
	{0x7B76, 0x0B},
	{0x7B77, 0x08},
	{0x7B78, 0x0A},
	{0x7B79, 0x47},
	{0x7B7C, 0x00},
	{0x7B7D, 0x00},
	{0x8D1F, 0x00},
	{0x8D27, 0x00},
	{0x9004, 0x03},
	{0x9200, 0x50},
	{0x9201, 0x6C},
	{0x9202, 0x71},
	{0x9203, 0x00},
	{0x9204, 0x71},
	{0x9205, 0x01},
	{0x9371, 0x6A},
	{0x9373, 0x6A},
	{0x9375, 0x64},
	{0x991A, 0x00},
	{0x996B, 0x8C},
	{0x996C, 0x64},
	{0x996D, 0x50},
	{0x9A4C, 0x0D},
	{0x9A4D, 0x0D},
	{0xA001, 0x0A},
	{0xA003, 0x0A},
	{0xA005, 0x0A},
	{0xA006, 0x01},
	{0xA007, 0xC0},
	{0xA009, 0xC0},

	{0x3D8A, 0x01},
	{0x4421, 0x08},
	{0x7B3B, 0x01},
	{0x7B4C, 0x00},
	{0x9905, 0x00},
	{0x9907, 0x00},
	{0x9909, 0x00},
	{0x990B, 0x00},
	{0x9944, 0x3C},
	{0x9947, 0x3C},
	{0x994A, 0x8C},
	{0x994B, 0x50},
	{0x994C, 0x1B},
	{0x994D, 0x8C},
	{0x994E, 0x50},
	{0x994F, 0x1B},
	{0x9950, 0x8C},
	{0x9951, 0x1B},
	{0x9952, 0x0A},
	{0x9953, 0x8C},
	{0x9954, 0x1B},
	{0x9955, 0x0A},
	{0x9A13, 0x04},
	{0x9A14, 0x04},
	{0x9A19, 0x00},
	{0x9A1C, 0x04},
	{0x9A1D, 0x04},
	{0x9A26, 0x05},
	{0x9A27, 0x05},
	{0x9A2C, 0x01},
	{0x9A2D, 0x03},
	{0x9A2F, 0x05},
	{0x9A30, 0x05},
	{0x9A41, 0x00},
	{0x9A46, 0x00},
	{0x9A47, 0x00},
	{0x9C17, 0x35},
	{0x9C1D, 0x31},
	{0x9C29, 0x50},
	{0x9C3B, 0x2F},
	{0x9C41, 0x6B},
	{0x9C47, 0x2D},
	{0x9C4D, 0x40},
	{0x9C6B, 0x00},
	{0x9C71, 0xC8},
	{0x9C73, 0x32},
	{0x9C75, 0x04},
	{0x9C7D, 0x2D},
	{0x9C83, 0x40},
	{0x9C94, 0x3F},
	{0x9C95, 0x3F},
	{0x9C96, 0x3F},
	{0x9C97, 0x00},
	{0x9C98, 0x00},
	{0x9C99, 0x00},
	{0x9C9A, 0x3F},
	{0x9C9B, 0x3F},
	{0x9C9C, 0x3F},
	{0x9CA0, 0x0F},
	{0x9CA1, 0x0F},
	{0x9CA2, 0x0F},
	{0x9CA3, 0x00},
	{0x9CA4, 0x00},
	{0x9CA5, 0x00},
	{0x9CA6, 0x1E},
	{0x9CA7, 0x1E},
	{0x9CA8, 0x1E},
	{0x9CA9, 0x00},
	{0x9CAA, 0x00},
	{0x9CAB, 0x00},
	{0x9CAC, 0x09},
	{0x9CAD, 0x09},
	{0x9CAE, 0x09},
	{0x9CBD, 0x50},
	{0x9CBF, 0x50},
	{0x9CC1, 0x50},
	{0x9CC3, 0x40},
	{0x9CC5, 0x40},
	{0x9CC7, 0x40},
	{0x9CC9, 0x0A},
	{0x9CCB, 0x0A},
	{0x9CCD, 0x0A},
	{0x9D17, 0x35},
	{0x9D1D, 0x31},
	{0x9D29, 0x50},
	{0x9D3B, 0x2F},
	{0x9D41, 0x6B},
	{0x9D47, 0x42},
	{0x9D4D, 0x5A},
	{0x9D6B, 0x00},
	{0x9D71, 0xC8},
	{0x9D73, 0x32},
	{0x9D75, 0x04},
	{0x9D7D, 0x42},
	{0x9D83, 0x5A},
	{0x9D94, 0x3F},
	{0x9D95, 0x3F},
	{0x9D96, 0x3F},
	{0x9D97, 0x00},
	{0x9D98, 0x00},
	{0x9D99, 0x00},
	{0x9D9A, 0x3F},
	{0x9D9B, 0x3F},
	{0x9D9C, 0x3F},
	{0x9D9D, 0x1F},
	{0x9D9E, 0x1F},
	{0x9D9F, 0x1F},
	{0x9DA0, 0x0F},
	{0x9DA1, 0x0F},
	{0x9DA2, 0x0F},
	{0x9DA3, 0x00},
	{0x9DA4, 0x00},
	{0x9DA5, 0x00},
	{0x9DA6, 0x1E},
	{0x9DA7, 0x1E},
	{0x9DA8, 0x1E},
	{0x9DA9, 0x00},
	{0x9DAA, 0x00},
	{0x9DAB, 0x00},
	{0x9DAC, 0x09},
	{0x9DAD, 0x09},
	{0x9DAE, 0x09},
	{0x9DC9, 0x0A},
	{0x9DCB, 0x0A},
	{0x9DCD, 0x0A},
	{0x9E17, 0x35},
	{0x9E1D, 0x31},
	{0x9E29, 0x50},
	{0x9E3B, 0x2F},
	{0x9E41, 0x6B},
	{0x9E47, 0x2D},
	{0x9E4D, 0x40},
	{0x9E6B, 0x00},
	{0x9E71, 0xC8},
	{0x9E73, 0x32},
	{0x9E75, 0x04},
	{0x9E94, 0x0F},
	{0x9E95, 0x0F},
	{0x9E96, 0x0F},
	{0x9E97, 0x00},
	{0x9E98, 0x00},
	{0x9E99, 0x00},
	{0x9EA0, 0x0F},
	{0x9EA1, 0x0F},
	{0x9EA2, 0x0F},
	{0x9EA3, 0x00},
	{0x9EA4, 0x00},
	{0x9EA5, 0x00},
	{0x9EA6, 0x3F},
	{0x9EA7, 0x3F},
	{0x9EA8, 0x3F},
	{0x9EA9, 0x00},
	{0x9EAA, 0x00},
	{0x9EAB, 0x00},
	{0x9EAC, 0x09},
	{0x9EAD, 0x09},
	{0x9EAE, 0x09},
	{0x9EC9, 0x0A},
	{0x9ECB, 0x0A},
	{0x9ECD, 0x0A},
	{0x9F17, 0x35},
	{0x9F1D, 0x31},
	{0x9F29, 0x50},
	{0x9F3B, 0x2F},
	{0x9F41, 0x6B},
	{0x9F47, 0x42},
	{0x9F4D, 0x5A},
	{0x9F6B, 0x00},
	{0x9F71, 0xC8},
	{0x9F73, 0x32},
	{0x9F75, 0x04},
	{0x9F94, 0x0F},
	{0x9F95, 0x0F},
	{0x9F96, 0x0F},
	{0x9F97, 0x00},
	{0x9F98, 0x00},
	{0x9F99, 0x00},
	{0x9F9A, 0x2F},
	{0x9F9B, 0x2F},
	{0x9F9C, 0x2F},
	{0x9F9D, 0x00},
	{0x9F9E, 0x00},
	{0x9F9F, 0x00},
	{0x9FA0, 0x0F},
	{0x9FA1, 0x0F},
	{0x9FA2, 0x0F},
	{0x9FA3, 0x00},
	{0x9FA4, 0x00},
	{0x9FA5, 0x00},
	{0x9FA6, 0x1E},
	{0x9FA7, 0x1E},
	{0x9FA8, 0x1E},
	{0x9FA9, 0x00},
	{0x9FAA, 0x00},
	{0x9FAB, 0x00},
	{0x9FAC, 0x09},
	{0x9FAD, 0x09},
	{0x9FAE, 0x09},
	{0x9FC9, 0x0A},
	{0x9FCB, 0x0A},
	{0x9FCD, 0x0A},
	{0xA14B, 0xFF},
	{0xA151, 0x0C},
	{0xA153, 0x50},
	{0xA155, 0x02},
	{0xA157, 0x00},
	{0xA1AD, 0xFF},
	{0xA1B3, 0x0C},
	{0xA1B5, 0x50},
	{0xA1B9, 0x00},
	{0xA24B, 0xFF},
	{0xA257, 0x00},
	{0xA2AD, 0xFF},
	{0xA2B9, 0x00},
	{0xB21F, 0x04},
	{0xB35C, 0x00},
	{0xB35E, 0x08},

	{0x0112, 0x0C},
	{0x0113, 0x0C},
	{0x0114, 0x03},
	{0x0342, 0x1B},
	{0x0343, 0xD8},
	{0x0340, 0x0F},
	{0x0341, 0x57},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0F},
	{0x0349, 0xD7},
	{0x034A, 0x0B},
	{0x034B, 0xDF},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x01},
	{0x0901, 0x22},
	{0x0902, 0x02},
	{0x3140, 0x02},
	{0x3C00, 0x00},
	{0x3C01, 0x01},
	{0x3C02, 0x9C},
	{0x3F0D, 0x00},
	{0x5748, 0x00},
	{0x5749, 0x00},
	{0x574A, 0x00},
	{0x574B, 0xA4},
	{0x7B53, 0x00},
	{0x9369, 0x73},
	{0x936B, 0x64},
	{0x936D, 0x5F},
	{0x9304, 0x03},
	{0x9305, 0x80},
	{0x9E9A, 0x2F},
	{0x9E9B, 0x2F},
	{0x9E9C, 0x2F},
	{0x9E9D, 0x00},
	{0x9E9E, 0x00},
	{0x9E9F, 0x00},
	{0xA2A9, 0x27},
	{0xA2B7, 0x03},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x07},
	{0x040D, 0xEC},
	{0x040E, 0x05},
	{0x040F, 0xF0},
	{0x034C, 0x07},
	{0x034D, 0xEC},
	{0x034E, 0x05},
	{0x034F, 0xF0},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x02},
	{0x0306, 0x00},
	{0x0307, 0xAF},
	{0x0309, 0x0C},
	{0x030B, 0x01},
	{0x030D, 0x03},
	{0x030E, 0x00},
	{0x030F, 0xD4},
	{0x0310, 0x01},
	{0x0820, 0x1A},
	{0x0821, 0x80},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3E20, 0x01},
	{0x3E37, 0x01},
	{0x3F50, 0x00},
	{0x3F56, 0x00},
	{0x3F57, 0xCC},
	{REG_NULL, 0x00},
};

static const struct imx378_mode supported_modes[] = {
	{
		.width = 3840,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x16A8,
		.vts_def = 0x0F3C,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.reg_list = imx378_linear_10_3840x2160_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 4056,
		.height = 3040,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x16A8,
		.vts_def = 0x0F3C,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.reg_list = imx378_linear_10_4056x3040_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 2028,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0C80,
		.hts_def = 0x1BD8,
		.vts_def = 0x0F57,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB12_1X12,
		.reg_list = imx378_linear_12_2028x1520_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 4056,
		.height = 3040,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x1BD8,
		.vts_def = 0x0F57,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB12_1X12,
		.reg_list = imx378_linear_12_4056x3040_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	IMX378_LINK_FREQ_848,
};

static const char * const imx378_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int imx378_write_reg(struct i2c_client *client, u16 reg,
			    int len, u32 val)
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

static int imx378_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		if (unlikely(regs[i].addr == REG_DELAY))
			usleep_range(regs[i].val, regs[i].val * 2);
		else
			ret = imx378_write_reg(client, regs[i].addr,
					       IMX378_REG_VALUE_08BIT,
					       regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int imx378_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
			   u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret, i;

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

	for (i = 0; i < 3; i++) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret == ARRAY_SIZE(msgs))
			break;
	}
	if (ret != ARRAY_SIZE(msgs) && i == 3)
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int imx378_get_reso_dist(const struct imx378_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		   abs(mode->height - framefmt->height);
}

static const struct imx378_mode *
imx378_find_best_fit(struct imx378 *imx378, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < imx378->cfg_num; i++) {
		dist = imx378_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int imx378_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx378 *imx378 = to_imx378(sd);
	const struct imx378_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&imx378->mutex);

	mode = imx378_find_best_fit(imx378, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&imx378->mutex);
		return -ENOTTY;
#endif
	} else {
		imx378->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx378->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx378->vblank, vblank_def,
					 IMX378_VTS_MAX - mode->height,
					 1, vblank_def);

		if (imx378->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB10_1X10) {
			imx378->cur_link_freq = 0;
			imx378->cur_pixel_rate = PIXEL_RATE_WITH_848M_10BIT;
		} else if (imx378->cur_mode->bus_fmt ==
			   MEDIA_BUS_FMT_SRGGB12_1X12) {
			imx378->cur_link_freq = 0;
			imx378->cur_pixel_rate = PIXEL_RATE_WITH_848M_12BIT;
		}

		__v4l2_ctrl_s_ctrl_int64(imx378->pixel_rate,
					 imx378->cur_pixel_rate);
		__v4l2_ctrl_s_ctrl(imx378->link_freq,
				   imx378->cur_link_freq);
	}

	mutex_unlock(&imx378->mutex);

	return 0;
}

static int imx378_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx378 *imx378 = to_imx378(sd);
	const struct imx378_mode *mode = imx378->cur_mode;

	mutex_lock(&imx378->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&imx378->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		if (imx378->flip & IMX378_MIRROR_BIT_MASK) {
			fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;
			if (imx378->flip & IMX378_FLIP_BIT_MASK)
				fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		} else if (imx378->flip & IMX378_FLIP_BIT_MASK) {
			fmt->format.code = MEDIA_BUS_FMT_SGBRG10_1X10;
		} else {
			fmt->format.code = mode->bus_fmt;
		}
		fmt->format.field = V4L2_FIELD_NONE;
		/* format info: width/height/data type/virctual channel */
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&imx378->mutex);

	return 0;
}

static int imx378_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx378 *imx378 = to_imx378(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = imx378->cur_mode->bus_fmt;

	return 0;
}

static int imx378_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx378 *imx378 = to_imx378(sd);

	if (fse->index >= imx378->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int imx378_enable_test_pattern(struct imx378 *imx378, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | IMX378_TEST_PATTERN_ENABLE;
	else
		val = IMX378_TEST_PATTERN_DISABLE;

	return imx378_write_reg(imx378->client,
				IMX378_REG_TEST_PATTERN,
				IMX378_REG_VALUE_08BIT,
				val);
}

static int imx378_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx378 *imx378 = to_imx378(sd);
	const struct imx378_mode *mode = imx378->cur_mode;

	mutex_lock(&imx378->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&imx378->mutex);

	return 0;
}

static int imx378_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct imx378 *imx378 = to_imx378(sd);
	const struct imx378_mode *mode = imx378->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (IMX378_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	if (mode->hdr_mode == HDR_X2)
		val = 1 << (IMX378_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void imx378_get_module_inf(struct imx378 *imx378,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, IMX378_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, imx378->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, imx378->len_name, sizeof(inf->base.lens));
}

static long imx378_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx378 *imx378 = to_imx378(sd);
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 i, h, w;
	u32 stream = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_GET_MODULE_INFO:
		imx378_get_module_inf(imx378, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = imx378->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = imx378->cur_mode->width;
		h = imx378->cur_mode->height;
		for (i = 0; i < imx378->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				imx378->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == imx378->cfg_num) {
			dev_err(&imx378->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = imx378->cur_mode->hts_def -
			    imx378->cur_mode->width;
			h = imx378->cur_mode->vts_def -
			    imx378->cur_mode->height;
			__v4l2_ctrl_modify_range(imx378->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(imx378->vblank, h,
						 IMX378_VTS_MAX -
						 imx378->cur_mode->height,
						 1, h);

			if (imx378->cur_mode->bus_fmt ==
			    MEDIA_BUS_FMT_SRGGB10_1X10) {
				imx378->cur_link_freq = 0;
				imx378->cur_pixel_rate =
				PIXEL_RATE_WITH_848M_10BIT;
			} else if (imx378->cur_mode->bus_fmt ==
				   MEDIA_BUS_FMT_SRGGB12_1X12) {
				imx378->cur_link_freq = 0;
				imx378->cur_pixel_rate =
				PIXEL_RATE_WITH_848M_12BIT;
			}

			__v4l2_ctrl_s_ctrl_int64(imx378->pixel_rate,
						 imx378->cur_pixel_rate);
			__v4l2_ctrl_s_ctrl(imx378->link_freq,
					   imx378->cur_link_freq);
		}
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = imx378_write_reg(imx378->client, IMX378_REG_CTRL_MODE,
				IMX378_REG_VALUE_08BIT, IMX378_MODE_STREAMING);
		else
			ret = imx378_write_reg(imx378->client, IMX378_REG_CTRL_MODE,
				IMX378_REG_VALUE_08BIT, IMX378_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx378_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx378_ioctl(sd, cmd, inf);
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
			ret = imx378_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx378_ioctl(sd, cmd, hdr);
		if (!ret)
			ret = copy_to_user(up, hdr, sizeof(*hdr));
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
			ret = imx378_ioctl(sd, cmd, hdr);
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
			ret = imx378_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = imx378_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int imx378_set_flip(struct imx378 *imx378)
{
	int ret = 0;
	u32 val = 0;

	ret = imx378_read_reg(imx378->client, IMX378_FLIP_MIRROR_REG,
			      IMX378_REG_VALUE_08BIT, &val);
	if (imx378->flip & IMX378_MIRROR_BIT_MASK)
		val |= IMX378_MIRROR_BIT_MASK;
	else
		val &= ~IMX378_MIRROR_BIT_MASK;
	if (imx378->flip & IMX378_FLIP_BIT_MASK)
		val |= IMX378_FLIP_BIT_MASK;
	else
		val &= ~IMX378_FLIP_BIT_MASK;
	ret |= imx378_write_reg(imx378->client, IMX378_FLIP_MIRROR_REG,
				IMX378_REG_VALUE_08BIT, val);

	return ret;
}

static int __imx378_start_stream(struct imx378 *imx378)
{
	int ret;

	ret = imx378_write_array(imx378->client, imx378->cur_mode->reg_list);
	if (ret)
		return ret;
	imx378->cur_vts = imx378->cur_mode->vts_def;
	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&imx378->ctrl_handler);
	if (ret)
		return ret;
	if (imx378->has_init_exp && imx378->cur_mode->hdr_mode != NO_HDR) {
		ret = imx378_ioctl(&imx378->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&imx378->init_hdrae_exp);
		if (ret) {
			dev_err(&imx378->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}

	imx378_set_flip(imx378);

	return imx378_write_reg(imx378->client, IMX378_REG_CTRL_MODE,
				IMX378_REG_VALUE_08BIT, IMX378_MODE_STREAMING);
}

static int __imx378_stop_stream(struct imx378 *imx378)
{
	return imx378_write_reg(imx378->client, IMX378_REG_CTRL_MODE,
				IMX378_REG_VALUE_08BIT, IMX378_MODE_SW_STANDBY);
}

static int imx378_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx378 *imx378 = to_imx378(sd);
	struct i2c_client *client = imx378->client;
	int ret = 0;

	mutex_lock(&imx378->mutex);
	on = !!on;
	if (on == imx378->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx378_start_stream(imx378);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx378_stop_stream(imx378);
		pm_runtime_put(&client->dev);
	}

	imx378->streaming = on;

unlock_and_return:
	mutex_unlock(&imx378->mutex);

	return ret;
}

static int imx378_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx378 *imx378 = to_imx378(sd);
	struct i2c_client *client = imx378->client;
	int ret = 0;

	mutex_lock(&imx378->mutex);

	/* If the power state is not modified - no work to do. */
	if (imx378->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		imx378->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx378->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx378->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 imx378_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, IMX378_XVCLK_FREQ / 1000 / 1000);
}

static int __imx378_power_on(struct imx378 *imx378)
{
	int ret;
	u32 delay_us;
	struct device *dev = &imx378->client->dev;

	ret = clk_set_rate(imx378->xvclk, IMX378_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(imx378->xvclk) != IMX378_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 37.125MHz\n");
	ret = clk_prepare_enable(imx378->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(imx378->reset_gpio))
		gpiod_set_value_cansleep(imx378->reset_gpio, 0);

	ret = regulator_bulk_enable(IMX378_NUM_SUPPLIES, imx378->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(imx378->reset_gpio))
		gpiod_set_value_cansleep(imx378->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(imx378->pwdn_gpio))
		gpiod_set_value_cansleep(imx378->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = imx378_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(imx378->xvclk);

	return ret;
}

static void __imx378_power_off(struct imx378 *imx378)
{
	if (!IS_ERR(imx378->pwdn_gpio))
		gpiod_set_value_cansleep(imx378->pwdn_gpio, 0);
	clk_disable_unprepare(imx378->xvclk);
	if (!IS_ERR(imx378->reset_gpio))
		gpiod_set_value_cansleep(imx378->reset_gpio, 0);
	regulator_bulk_disable(IMX378_NUM_SUPPLIES, imx378->supplies);
}

static int imx378_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx378 *imx378 = to_imx378(sd);

	return __imx378_power_on(imx378);
}

static int imx378_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx378 *imx378 = to_imx378(sd);

	__imx378_power_off(imx378);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int imx378_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx378 *imx378 = to_imx378(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct imx378_mode *def_mode = &supported_modes[0];

	mutex_lock(&imx378->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx378->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int imx378_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	struct imx378 *imx378 = to_imx378(sd);

	if (fie->index >= imx378->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops imx378_pm_ops = {
	SET_RUNTIME_PM_OPS(imx378_runtime_suspend,
			   imx378_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops imx378_internal_ops = {
	.open = imx378_open,
};
#endif

static const struct v4l2_subdev_core_ops imx378_core_ops = {
	.s_power = imx378_s_power,
	.ioctl = imx378_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx378_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx378_video_ops = {
	.s_stream = imx378_s_stream,
	.g_frame_interval = imx378_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx378_pad_ops = {
	.enum_mbus_code = imx378_enum_mbus_code,
	.enum_frame_size = imx378_enum_frame_sizes,
	.enum_frame_interval = imx378_enum_frame_interval,
	.get_fmt = imx378_get_fmt,
	.set_fmt = imx378_set_fmt,
	.get_mbus_config = imx378_g_mbus_config,
};

static const struct v4l2_subdev_ops imx378_subdev_ops = {
	.core	= &imx378_core_ops,
	.video	= &imx378_video_ops,
	.pad	= &imx378_pad_ops,
};

static int imx378_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx378 *imx378 = container_of(ctrl->handler,
					     struct imx378, ctrl_handler);
	struct i2c_client *client = imx378->client;
	s64 max;
	int ret = 0;
	u32 again = 0;
	u32 dgain = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = imx378->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(imx378->exposure,
					 imx378->exposure->minimum, max,
					 imx378->exposure->step,
					 imx378->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = imx378_write_reg(imx378->client,
				       IMX378_REG_EXPOSURE_H,
				       IMX378_REG_VALUE_08BIT,
				       IMX378_FETCH_EXP_H(ctrl->val));
		ret |= imx378_write_reg(imx378->client,
					IMX378_REG_EXPOSURE_L,
					IMX378_REG_VALUE_08BIT,
					IMX378_FETCH_EXP_L(ctrl->val));
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		again = ctrl->val > 978 ? 978 : ctrl->val;
		dgain = ctrl->val > 978 ? ctrl->val - 978 : 256;
		ret = imx378_write_reg(imx378->client, IMX378_REG_GAIN_H,
				       IMX378_REG_VALUE_08BIT,
				       IMX378_FETCH_AGAIN_H(again));
		ret |= imx378_write_reg(imx378->client, IMX378_REG_GAIN_L,
					IMX378_REG_VALUE_08BIT,
					IMX378_FETCH_AGAIN_L(again));
		ret |= imx378_write_reg(imx378->client, IMX378_REG_DGAIN,
					IMX378_REG_VALUE_08BIT,
					IMX378_DGAIN_MODE);
		if (IMX378_DGAIN_MODE && dgain > 0) {
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_DGAINGR_H,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_H(dgain));
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_DGAINGR_L,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_L(dgain));
		} else if (dgain > 0) {
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_DGAINR_H,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_H(dgain));
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_DGAINR_L,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_L(dgain));
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_DGAINB_H,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_H(dgain));
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_DGAINB_L,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_L(dgain));
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_DGAINGB_H,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_H(dgain));
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_DGAINGB_L,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_L(dgain));
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_GAIN_GLOBAL_H,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_H(dgain));
			ret |= imx378_write_reg(imx378->client,
						IMX378_REG_GAIN_GLOBAL_L,
						IMX378_REG_VALUE_08BIT,
						IMX378_FETCH_DGAIN_L(dgain));
		}
		break;
	case V4L2_CID_VBLANK:
		ret = imx378_write_reg(imx378->client,
				       IMX378_REG_VTS_H,
				       IMX378_REG_VALUE_08BIT,
				       (ctrl->val + imx378->cur_mode->height)
				       >> 8);
		ret |= imx378_write_reg(imx378->client,
					IMX378_REG_VTS_L,
					IMX378_REG_VALUE_08BIT,
					(ctrl->val + imx378->cur_mode->height)
					& 0xff);
		imx378->cur_vts = ctrl->val + imx378->cur_mode->height;
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			imx378->flip |= IMX378_MIRROR_BIT_MASK;
		else
			imx378->flip &= ~IMX378_MIRROR_BIT_MASK;
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			imx378->flip |= IMX378_FLIP_BIT_MASK;
		else
			imx378->flip &= ~IMX378_FLIP_BIT_MASK;
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx378_enable_test_pattern(imx378, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx378_ctrl_ops = {
	.s_ctrl = imx378_set_ctrl,
};

static int imx378_initialize_controls(struct imx378 *imx378)
{
	const struct imx378_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &imx378->ctrl_handler;
	mode = imx378->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &imx378->mutex;

	imx378->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
						   V4L2_CID_LINK_FREQ,
						   0, 0, link_freq_menu_items);

	if (imx378->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB10_1X10) {
		imx378->cur_link_freq = 0;
		imx378->cur_pixel_rate = PIXEL_RATE_WITH_848M_10BIT;
	} else if (imx378->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB12_1X12) {
		imx378->cur_link_freq = 0;
		imx378->cur_pixel_rate = PIXEL_RATE_WITH_848M_12BIT;
	}

	imx378->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
					       V4L2_CID_PIXEL_RATE,
					       0, PIXEL_RATE_WITH_848M_10BIT,
					       1, imx378->cur_pixel_rate);
	v4l2_ctrl_s_ctrl(imx378->link_freq,
			   imx378->cur_link_freq);

	h_blank = mode->hts_def - mode->width;
	imx378->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (imx378->hblank)
		imx378->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx378->vblank = v4l2_ctrl_new_std(handler, &imx378_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   IMX378_VTS_MAX - mode->height,
					   1, vblank_def);
	imx378->cur_vts = mode->vts_def;
	exposure_max = mode->vts_def - 4;
	imx378->exposure = v4l2_ctrl_new_std(handler, &imx378_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX378_EXPOSURE_MIN,
					     exposure_max,
					     IMX378_EXPOSURE_STEP,
					     mode->exp_def);
	imx378->anal_gain = v4l2_ctrl_new_std(handler, &imx378_ctrl_ops,
					      V4L2_CID_ANALOGUE_GAIN,
					      IMX378_GAIN_MIN,
					      IMX378_GAIN_MAX,
					      IMX378_GAIN_STEP,
					      IMX378_GAIN_DEFAULT);
	imx378->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &imx378_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(imx378_test_pattern_menu) - 1,
				0, 0, imx378_test_pattern_menu);

	imx378->h_flip = v4l2_ctrl_new_std(handler, &imx378_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	imx378->v_flip = v4l2_ctrl_new_std(handler, &imx378_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	imx378->flip = 0;

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx378->client->dev,
			"Failed to init controls(  %d  )\n", ret);
		goto err_free_handler;
	}

	imx378->subdev.ctrl_handler = handler;
	imx378->has_init_exp = false;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx378_check_sensor_id(struct imx378 *imx378,
				  struct i2c_client *client)
{
	struct device *dev = &imx378->client->dev;
	u16 id = 0;
	u32 reg_H = 0;
	u32 reg_L = 0;
	int ret;

	ret = imx378_read_reg(client, IMX378_REG_CHIP_ID_H,
			      IMX378_REG_VALUE_08BIT, &reg_H);
	ret |= imx378_read_reg(client, IMX378_REG_CHIP_ID_L,
			       IMX378_REG_VALUE_08BIT, &reg_L);
	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (!(reg_H == (CHIP_ID >> 8) || reg_L == (CHIP_ID & 0xff))) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "detected imx378 %04x sensor\n", id);
	return 0;
}

static int imx378_configure_regulators(struct imx378 *imx378)
{
	unsigned int i;

	for (i = 0; i < IMX378_NUM_SUPPLIES; i++)
		imx378->supplies[i].supply = imx378_supply_names[i];

	return devm_regulator_bulk_get(&imx378->client->dev,
				       IMX378_NUM_SUPPLIES,
				       imx378->supplies);
}

static int imx378_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx378 *imx378;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	imx378 = devm_kzalloc(dev, sizeof(*imx378), GFP_KERNEL);
	if (!imx378)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &imx378->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &imx378->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &imx378->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &imx378->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}

	imx378->client = client;
	imx378->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < imx378->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			imx378->cur_mode = &supported_modes[i];
			break;
		}
	}

	if (i == imx378->cfg_num)
		imx378->cur_mode = &supported_modes[0];

	imx378->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx378->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx378->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx378->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx378->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx378->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = imx378_configure_regulators(imx378);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&imx378->mutex);

	sd = &imx378->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx378_subdev_ops);

	ret = imx378_initialize_controls(imx378);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx378_power_on(imx378);
	if (ret)
		goto err_free_handler;

	ret = imx378_check_sensor_id(imx378, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &imx378_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	imx378->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx378->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx378->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 imx378->module_index, facing,
		 IMX378_NAME, dev_name(sd->dev));
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
	__imx378_power_off(imx378);
err_free_handler:
	v4l2_ctrl_handler_free(&imx378->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx378->mutex);

	return ret;
}

static int imx378_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx378 *imx378 = to_imx378(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx378->ctrl_handler);
	mutex_destroy(&imx378->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx378_power_off(imx378);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx378_of_match[] = {
	{ .compatible = "sony,imx378" },
	{},
};
MODULE_DEVICE_TABLE(of, imx378_of_match);
#endif

static const struct i2c_device_id imx378_match_id[] = {
	{ "sony,imx378", 0 },
	{ },
};

static struct i2c_driver imx378_i2c_driver = {
	.driver = {
		.name = IMX378_NAME,
		.pm = &imx378_pm_ops,
		.of_match_table = of_match_ptr(imx378_of_match),
	},
	.probe		= &imx378_probe,
	.remove		= &imx378_remove,
	.id_table	= imx378_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx378_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx378_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony imx378 sensor driver");
MODULE_LICENSE("GPL v2");
