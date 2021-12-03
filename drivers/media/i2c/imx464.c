// SPDX-License-Identifier: GPL-2.0
/*
 * IMX464 driver
 *
 * Copyright (C) 2020 Fuzhou Rockchip Electronics Co., Ltd.
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

#define MIPI_FREQ_360M			360000000
#define MIPI_FREQ_445M			445600000
#define MIPI_FREQ_594M			594000000

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define IMX464_10BIT_HDR2_PIXEL_RATE	(MIPI_FREQ_594M * 2 / 10 * 4)
#define IMX464_XVCLK_FREQ_37M		37125000
#define IMX464_XVCLK_FREQ_24M		24000000

#define CHIP_ID				0x06
#define IMX464_REG_CHIP_ID		0x3057

#define IMX464_REG_CTRL_MODE		0x3000
#define IMX464_MODE_SW_STANDBY		BIT(0)
#define IMX464_MODE_STREAMING		0x0

#define IMX464_REG_MARSTER_MODE		0x3002
#define IMX464_MODE_STOP		BIT(0)
#define IMX464_MODE_START		0x0

#define IMX464_GAIN_SWITCH_REG		0x3019

#define IMX464_LF_GAIN_REG_H		0x30E9
#define IMX464_LF_GAIN_REG_L		0x30E8

#define IMX464_SF1_GAIN_REG_H		0x30EB
#define IMX464_SF1_GAIN_REG_L		0x30EA

#define IMX464_SF2_GAIN_REG_H		0x30ED
#define IMX464_SF2_GAIN_REG_L		0x30EC

#define IMX464_LF_EXPO_REG_H		0x305A
#define IMX464_LF_EXPO_REG_M		0x3059
#define IMX464_LF_EXPO_REG_L		0x3058

#define IMX464_SF1_EXPO_REG_H		0x305E
#define IMX464_SF1_EXPO_REG_M		0x305D
#define IMX464_SF1_EXPO_REG_L		0x305C

#define IMX464_SF2_EXPO_REG_H		0x3062
#define IMX464_SF2_EXPO_REG_M		0x3061
#define IMX464_SF2_EXPO_REG_L		0x3060
#define IMX464_RHS1_DEFAULT		0x06d
#define IMX464_RHS1_X3_DEFAULT		0x0a3

#define IMX464_RHS1_REG_H		0x306a
#define IMX464_RHS1_REG_M		0x3069
#define IMX464_RHS1_REG_L		0x3068

#define IMX464_RHS2_REG_H		0x306E
#define IMX464_RHS2_REG_M		0x306D
#define IMX464_RHS2_REG_L		0x306C
#define IMX464_RHS2_X3_DEFAULT		0x0ce


#define	IMX464_EXPOSURE_MIN		2
#define	IMX464_EXPOSURE_STEP		1
#define IMX464_VTS_MAX			0x7fff

#define IMX464_GAIN_MIN			0x00
#define IMX464_GAIN_MAX			0xee
#define IMX464_GAIN_STEP		1
#define IMX464_GAIN_DEFAULT		0x00

#define IMX464_FETCH_GAIN_H(VAL)	(((VAL) >> 8) & 0x07)
#define IMX464_FETCH_GAIN_L(VAL)	((VAL) & 0xFF)

#define IMX464_FETCH_EXP_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX464_FETCH_EXP_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX464_FETCH_EXP_L(VAL)		((VAL) & 0xFF)

#define IMX464_FETCH_RHS1_H(VAL)	(((VAL) >> 16) & 0x0F)
#define IMX464_FETCH_RHS1_M(VAL)	(((VAL) >> 8) & 0xFF)
#define IMX464_FETCH_RHS1_L(VAL)	((VAL) & 0xFF)

#define IMX464_FETCH_VTS_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX464_FETCH_VTS_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX464_FETCH_VTS_L(VAL)		((VAL) & 0xFF)

#define IMX464_GROUP_HOLD_REG		0x3001
#define IMX464_GROUP_HOLD_START		0x01
#define IMX464_GROUP_HOLD_END		0x00

#define IMX464_VTS_REG_L		0x3030
#define IMX464_VTS_REG_M		0x3031
#define IMX464_VTS_REG_H		0x3032

#define REG_NULL			0xFFFF

#define IMX464_REG_VALUE_08BIT		1
#define IMX464_REG_VALUE_16BIT		2
#define IMX464_REG_VALUE_24BIT		3

#define IMX464_BITS_PER_SAMPLE		10

#define IMX464_VREVERSE_REG	0x304f
#define IMX464_HREVERSE_REG	0x304e

#define BRL				1558
#define RHS1_MAX			((BRL * 2 - 1) / 4 * 4 + 1) // <3*BRL=2*1558 && 6n+1
#define SHR1_MIN			9u

/* Readout timing setting of SEF1(DOL3): RHS1 < 3 * BRL and should be 6n + 1 */
#define RHS1_MAX_X3			((BRL * 3 - 1) / 6 * 6 + 1)
#define SHR1_MIN_X3			13u

#define USED_SYS_DEBUG

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define IMX464_NAME			"imx464"

static const char * const IMX464_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define IMX464_NUM_SUPPLIES ARRAY_SIZE(IMX464_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct IMX464_mode {
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

struct IMX464 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX464_NUM_SUPPLIES];

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
	const struct IMX464_mode *support_modes;
	const struct IMX464_mode *cur_mode;
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

#define to_IMX464(sd) container_of(sd, struct IMX464, subdev)

/*
 * Xclk 37.125Mhz
 */
static const struct regval IMX464_global_regs[] = {
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval IMX464_linear_10bit_2688x1520_2lane_37m_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x01},
	{0x300C, 0x5B},
	{0x300D, 0x40},
	{0x3034, 0xDC},
	{0x3035, 0x05},
	{0x3050, 0x00},
	{0x3058, 0x83},
	{0x3059, 0x04},
	{0x30BE, 0x5E},
	{0x30E8, 0x14},
	{0x3110, 0x02},
	{0x314C, 0xC0},
	{0x315A, 0x06},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x319E, 0x02},
	{0x31A1, 0x00},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x35BC, 0x00},
	{0x35BE, 0xFF},
	{0x35CC, 0x1B},
	{0x35CD, 0x00},
	{0x35CE, 0x2A},
	{0x35CF, 0x00},
	{0x35DC, 0x07},
	{0x35DE, 0x1A},
	{0x35DF, 0x00},
	{0x35E4, 0x2B},
	{0x35E5, 0x00},
	{0x35E6, 0x07},
	{0x35E7, 0x01},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x3718, 0x1C},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A01, 0x01},
	{0x3A18, 0x7F},
	{0x3A1A, 0x37},
	{0x3A1C, 0x37},
	{0x3A1E, 0xF7},
	{0x3A1F, 0x00},
	{0x3A20, 0x3F},
	{0x3A22, 0x6F},
	{0x3A24, 0x3F},
	{0x3A26, 0x5F},
	{0x3A28, 0x2F},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval IMX464_hdr_2x_10bit_2688x1520_2lane_37m_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x01},
	{0x300C, 0x5B},
	{0x300D, 0x40},
	{0x3034, 0xDC},
	{0x3035, 0x05},
	{0x3048, 0x01},
	{0x3049, 0x01},
	{0x304A, 0x01},
	{0x304B, 0x01},
	{0x304C, 0x13},
	{0x304D, 0x00},
	{0x3050, 0x00},
	{0x3058, 0xF4},
	{0x3059, 0x0A},
	{0x3068, 0x3D},
	{0x30BE, 0x5E},
	{0x30E8, 0x0A},
	{0x3110, 0x02},
	{0x314C, 0x80},//
	{0x315A, 0x02},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x319E, 0x01},//1188M
	{0x31A1, 0x00},
	{0x31D7, 0x01},
	{0x3200, 0x10},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x35BC, 0x00},
	{0x35BE, 0xFF},
	{0x35CC, 0x1B},
	{0x35CD, 0x00},
	{0x35CE, 0x2A},
	{0x35CF, 0x00},
	{0x35DC, 0x07},
	{0x35DE, 0x1A},
	{0x35DF, 0x00},
	{0x35E4, 0x2B},
	{0x35E5, 0x00},
	{0x35E6, 0x07},
	{0x35E7, 0x01},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x3718, 0x1C},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A01, 0x01},
	{0x3A18, 0x8F},
	{0x3A1A, 0x4F},
	{0x3A1C, 0x47},
	{0x3A1E, 0x37},
	{0x3A1F, 0x01},
	{0x3A20, 0x4F},
	{0x3A22, 0x87},
	{0x3A24, 0x4F},
	{0x3A26, 0x7F},
	{0x3A28, 0x3F},
	{REG_NULL, 0x00},
};

static const struct regval IMX464_linear_10bit_2688x1520_2lane_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x01},
	{0x300C, 0x3b},
	{0x300D, 0x2a},
	{0x3034, 0xDC},
	{0x3035, 0x05},
	{0x3048, 0x00},
	{0x3049, 0x00},
	{0x304A, 0x03},
	{0x304B, 0x02},
	{0x304C, 0x14},
	{0x304D, 0x03},
	{0x3050, 0x00},
	{0x3058, 0x83},
	{0x3059, 0x04},
	{0x3068, 0xc9},
	{0x30BE, 0x5E},
	{0x30E8, 0x14},
	{0x3110, 0x02},
	{0x314C, 0x29},
	{0x314D, 0x01},
	{0x315A, 0x06},
	{0x3168, 0xA0},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x319E, 0x02},
	{0x31A1, 0x00},
	{0x31D7, 0x00},
	{0x3200, 0x11},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x35BC, 0x00},
	{0x35BE, 0xFF},
	{0x35CC, 0x1B},
	{0x35CD, 0x00},
	{0x35CE, 0x2A},
	{0x35CF, 0x00},
	{0x35DC, 0x07},
	{0x35DE, 0x1A},
	{0x35DF, 0x00},
	{0x35E4, 0x2B},
	{0x35E5, 0x00},
	{0x35E6, 0x07},
	{0x35E7, 0x01},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x3718, 0x1C},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A01, 0x01},
	{0x3A18, 0x7F},
	{0x3A1A, 0x37},
	{0x3A1C, 0x37},
	{0x3A1E, 0xF7},
	{0x3A1F, 0x00},
	{0x3A20, 0x3F},
	{0x3A22, 0x6F},
	{0x3A24, 0x3F},
	{0x3A26, 0x5F},
	{0x3A28, 0x2F},
	{REG_NULL, 0x00},
};

static const struct regval IMX464_hdr_2x_10bit_2688x1520_2lane_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x01},
	{0x300C, 0x3B},
	{0x300D, 0x2A},
	{0x3034, 0xDC},
	{0x3035, 0x05},
	{0x3048, 0x01},
	{0x3049, 0x01},
	{0x304A, 0x04},
	{0x304B, 0x04},
	{0x304C, 0x13},
	{0x304D, 0x00},
	{0x3050, 0x00},
	{0x3058, 0xF4},
	{0x3059, 0x0A},
	{0x3068, 0x3D},
	{0x30BE, 0x5E},
	{0x30E8, 0x14},
	{0x3110, 0x02},
	{0x314C, 0x29},//
	{0x314D, 0x01},//
	{0x315A, 0x06},
	{0x3168, 0xA0},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x319E, 0x02},//1188M
	{0x31A1, 0x00},
	{0x31D7, 0x01},
	{0x3200, 0x10},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x35BC, 0x00},
	{0x35BE, 0xFF},
	{0x35CC, 0x1B},
	{0x35CD, 0x00},
	{0x35CE, 0x2A},
	{0x35CF, 0x00},
	{0x35DC, 0x07},
	{0x35DE, 0x1A},
	{0x35DF, 0x00},
	{0x35E4, 0x2B},
	{0x35E5, 0x00},
	{0x35E6, 0x07},
	{0x35E7, 0x01},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x3718, 0x1C},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A01, 0x01},
	{0x3A18, 0x7F},
	{0x3A1A, 0x37},
	{0x3A1C, 0x37},
	{0x3A1E, 0xF7},
	{0x3A1F, 0x00},
	{0x3A20, 0x3F},
	{0x3A22, 0x6F},
	{0x3A24, 0x3F},
	{0x3A26, 0x5F},
	{0x3A28, 0x2F},
	{REG_NULL, 0x00},
};

static const struct regval IMX464_linear_10bit_2688x1520_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x01},
	{0x300C, 0x5B},
	{0x300D, 0x40},
	{0x3030, 0xE4},
	{0x3031, 0x0C},
	{0x3034, 0xee},
	{0x3035, 0x02},
	{0x3048, 0x00},
	{0x3049, 0x00},
	{0x304A, 0x03},
	{0x304B, 0x02},
	{0x304C, 0x14},
	{0x3050, 0x00},
	{0x3058, 0x06},
	{0x3059, 0x09},
	{0x305C, 0x09},
	{0x3060, 0x21},
	{0x3061, 0x01},
	{0x3068, 0xc9},
	{0x306C, 0x56},
	{0x306D, 0x09},
	{0x30BE, 0x5E},
	{0x30E8, 0x14},
	{0x3110, 0x02},
	{0x314C, 0xC0},
	{0x315A, 0x06},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x319E, 0x02},
	{0x31A1, 0x00},
	{0x31D7, 0x00},
	{0x3200, 0x11},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x35BC, 0x00},
	{0x35BE, 0xFF},
	{0x35CC, 0x1B},
	{0x35CD, 0x00},
	{0x35CE, 0x2A},
	{0x35CF, 0x00},
	{0x35DC, 0x07},
	{0x35DE, 0x1A},
	{0x35DF, 0x00},
	{0x35E4, 0x2B},
	{0x35E5, 0x00},
	{0x35E6, 0x07},
	{0x35E7, 0x01},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x3718, 0x1C},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A18, 0x7F},
	{0x3A1A, 0x37},
	{0x3A1C, 0x37},
	{0x3A1E, 0xF7},
	{0x3A1F, 0x00},
	{0x3A20, 0x3F},
	{0x3A22, 0x6F},
	{0x3A24, 0x3F},
	{0x3A26, 0x5F},
	{0x3A28, 0x2F},
	{REG_NULL, 0x00},
};

static const struct regval IMX464_hdr_2x_10bit_2688x1520_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x01},
	{0x300C, 0x5B},
	{0x300D, 0x40},
	{0x3030, 0x72},
	{0x3031, 0x06},
	{0x3034, 0xee},
	{0x3035, 0x02},
	{0x3048, 0x01},
	{0x3049, 0x01},
	{0x304A, 0x04},
	{0x304B, 0x04},
	{0x304C, 0x13},
	{0x3050, 0x00},
	{0x3058, 0x06},
	{0x3059, 0x09},
	{0x305C, 0x09},
	{0x3060, 0x21},
	{0x3061, 0x01},
	{0x3068, 0x6D},
	{0x306C, 0x56},
	{0x306D, 0x09},
	{0x30BE, 0x5E},
	{0x30E8, 0x14},
	{0x3110, 0x02},
	{0x314C, 0xC0},
	{0x315A, 0x06},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x319E, 0x02},
	{0x31A1, 0x00},
	{0x31D7, 0x01},
	{0x3200, 0x10},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x35BC, 0x00},
	{0x35BE, 0xFF},
	{0x35CC, 0x1B},
	{0x35CD, 0x00},
	{0x35CE, 0x2A},
	{0x35CF, 0x00},
	{0x35DC, 0x07},
	{0x35DE, 0x1A},
	{0x35DF, 0x00},
	{0x35E4, 0x2B},
	{0x35E5, 0x00},
	{0x35E6, 0x07},
	{0x35E7, 0x01},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x3718, 0x1C},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A18, 0x7F},
	{0x3A1A, 0x37},
	{0x3A1C, 0x37},
	{0x3A1E, 0xF7},
	{0x3A1F, 0x00},
	{0x3A20, 0x3F},
	{0x3A22, 0x6F},
	{0x3A24, 0x3F},
	{0x3A26, 0x5F},
	{0x3A28, 0x2F},
	{REG_NULL, 0x00},
};

static const struct regval IMX464_hdr_3x_10bit_2688x1520_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x01},
	{0x300C, 0x5B},
	{0x300D, 0x40},
#ifdef FRAME_15_FPS
	{0x3030, 0xA2},
	{0x3031, 0x09},
#else
	{0x3030, 0xD1},
	{0x3031, 0x04},
#endif
//add for default
	{0x3034, 0xF4},
	{0x3035, 0x01},
	{0x3048, 0x01},
	{0x3049, 0x02},
	{0x304A, 0x05},
	{0x304B, 0x04},
	{0x304C, 0x13},
	{0x3050, 0x00},
	{0x3058, 0x77},
	{0x3059, 0x0D},
	{0x305C, 0x0D},
	{0x3060, 0xB0},
	{0x3061, 0x00},
	{0x3068, 0xA3},
	{0x306C, 0xCE},
	{0x306D, 0x00},
	{0x30BE, 0x5E},
	{0x30E8, 0x14},
	{0x3110, 0x02},
	{0x314C, 0x80},
	{0x315A, 0x02},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x319E, 0x01},
	{0x31A1, 0x00},
	{0x31D7, 0x03},
	{0x3200, 0x10},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x35BC, 0x00},
	{0x35BE, 0xFF},
	{0x35CC, 0x1B},
	{0x35CD, 0x00},
	{0x35CE, 0x2A},
	{0x35CF, 0x00},
	{0x35DC, 0x07},
	{0x35DE, 0x1A},
	{0x35DF, 0x00},
	{0x35E4, 0x2B},
	{0x35E5, 0x00},
	{0x35E6, 0x07},
	{0x35E7, 0x01},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x3718, 0x1C},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A18, 0x8F},
	{0x3A1A, 0x4F},
	{0x3A1C, 0x47},
	{0x3A1E, 0xF7},
	{0x3A1F, 0x01},
	{0x3A20, 0x4F},
	{0x3A22, 0x87},
	{0x3A24, 0x4F},
	{0x3A26, 0x5F},
	{0x3A28, 0x3F},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval IMX464_linear_12bit_2688x1520_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x00},
	{0x300C, 0x3B},
	{0x300D, 0x2A},
	{0x3018, 0x04},
	{0x302C, 0x30},
	{0x302E, 0x80},
	{0x302F, 0x0A},
	{0x3030, 0x6B},
	{0x3031, 0x0A},
	{0x3032, 0x00},
	{0x3034, 0xee},
	{0x3035, 0x02},
	{0x3048, 0x00},
	{0x3049, 0x00},
	{0x304A, 0x03},
	{0x304B, 0x02},
	{0x304C, 0x14},
	{0x3050, 0x01},
	{0x3056, 0x02},
	{0x3057, 0x06},
	{0x3058, 0x03},
	{0x3059, 0x00},
	{0x3068, 0xc9},
	{0x3069, 0x00},
	{0x30BE, 0x5E},
	{0x30C6, 0x00},
	{0x30CE, 0x00},
	{0x30D8, 0x4F},
	{0x30D9, 0x64},
	{0x3110, 0x02},
	{0x314C, 0xF0},
	{0x315A, 0x06},
	{0x3168, 0x82},
	{0x316A, 0x7E},
	{0x319D, 0x01},
	{0x319E, 0x02},
	{0x31A1, 0x00},
	{0x31D7, 0x00},
	{0x3202, 0x02},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3200, 0x11},
	{0x3798, 0xBF},
	{0x3A01, 0x03},
	{0x3A18, 0x6F},
	{0x3A1A, 0x2F},
	{0x3A1C, 0x2F},
	{0x3A1E, 0xBF},
	{0x3A1F, 0x00},
	{0x3A20, 0x2F},
	{0x3A22, 0x57},
	{0x3A24, 0x2F},
	{0x3A26, 0x4F},
	{0x3A28, 0x27},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval IMX464_hdr_2x_12bit_2688x1520_regs[] = {
	{0x3000, 0x01},
	{0x3002, 0x00},
	{0x300C, 0x3B},
	{0x300D, 0x2A},
	{0x3018, 0x04},
	{0x302C, 0x30},
	{0x302E, 0x80},
	{0x302F, 0x0A},
	{0x3030, 0x40},
	{0x3031, 0x06},
	{0x3032, 0x00},
	{0x3034, 0xee},
	{0x3035, 0x02},
	{0x3048, 0x01},
	{0x3049, 0x01},
	{0x304A, 0x04},
	{0x304B, 0x04},
	{0x304C, 0x13},
	{0x3050, 0x01},
	{0x3056, 0x02},
	{0x3057, 0x06},
	{0x3058, 0x20},
	{0x3059, 0x03},
	{0x3068, 0xD9},
	{0x3069, 0x02},
	{0x30BE, 0x5E},
	{0x30C6, 0x00},
	{0x30CE, 0x00},
	{0x30D8, 0x4F},
	{0x30D9, 0x64},
	{0x3110, 0x02},
	{0x314C, 0xF0},
	{0x315A, 0x06},
	{0x3168, 0x82},
	{0x316A, 0x7E},
	{0x319D, 0x01},
	{0x319E, 0x02},
	{0x31A1, 0x00},
	{0x31D7, 0x01},
	{0x3202, 0x02},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3200, 0x10},
	{0x3798, 0xBF},
	{0x3A01, 0x03},
	{0x3A18, 0x6F},
	{0x3A1A, 0x2F},
	{0x3A1C, 0x2F},
	{0x3A1E, 0xBF},
	{0x3A1F, 0x00},
	{0x3A20, 0x2F},
	{0x3A22, 0x57},
	{0x3A24, 0x2F},
	{0x3A26, 0x4F},
	{0x3A28, 0x27},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval IMX464_interal_sync_master_start_regs[] = {
	{0x3010, 0x07},
	{0x31a1, 0x00},
	{REG_NULL, 0x00},
};
static __maybe_unused const struct regval IMX464_interal_sync_master_stop_regs[] = {
	{0x31a1, 0x0f},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval IMX464_external_sync_master_start_regs[] = {
	{0x3010, 0x05},
	{0x31a1, 0x03},
	{0x31d9, 0x01},
	{REG_NULL, 0x00},
};
static __maybe_unused const struct regval IMX464_external_sync_master_stop_regs[] = {
	{0x31a1, 0x0f},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval IMX464_slave_start_regs[] = {
	{0x3010, 0x05},
	{0x31a1, 0x0f},
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
static const struct IMX464_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2712,
		.height = 1536,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0906,
		.hts_def = 0x05dc * 2,
		.vts_def = 0x0ce4,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.mclk = 37125000,
		.reg_list = IMX464_linear_10bit_2688x1520_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2712,
		.height = 1536,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x03de,
		.hts_def = 0x02ee * 4,
		.vts_def = 0x0672 * 2,
		.mipi_freq_idx = 1,
		.bpp = 10,
		.mclk = 37125000,
		.reg_list = IMX464_hdr_2x_10bit_2688x1520_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2712,
		.height = 1536,
		.max_fps = {
			.numerator = 10000,
			#ifdef FRAME_15_FPS
			.denominator = 150000,
			#else
			.denominator = 300000,
			#endif
		},
		.exp_def = 0x05cd,
		.hts_def = 0x01F4 * 8,
		#ifdef FRAME_15_FPS
		.vts_def = 0x09A2 * 4,
		#else
		.vts_def = 0x04D1 * 4,
		#endif
		.mipi_freq_idx = 1,
		.bpp = 10,
		.mclk = 37125000,
		.reg_list = IMX464_hdr_3x_10bit_2688x1520_regs,
		.hdr_mode = HDR_X3,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_2,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr1
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_2,//S->csi wr2
	},
};

static const struct IMX464_mode supported_modes_2lane[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2712,
		.height = 1538,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x05dc * 2,
		.vts_def = 0x672,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.mclk = 24000000,
		.reg_list = IMX464_linear_10bit_2688x1520_2lane_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2712,
		.height = 1538,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x05dc * 4,
		.vts_def = 0x0672 * 2,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.mclk = 24000000,
		.reg_list = IMX464_hdr_2x_10bit_2688x1520_2lane_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_445M,
	MIPI_FREQ_594M,
};

/* Write registers up to 4 at a time */
static int imx464_write_reg(struct i2c_client *client, u16 reg,
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

static int IMX464_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = imx464_write_reg(client, regs[i].addr,
				       IMX464_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int IMX464_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int IMX464_get_reso_dist(const struct IMX464_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct IMX464_mode *
IMX464_find_best_fit(struct IMX464 *IMX464, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < IMX464->cfg_num; i++) {
		dist = IMX464_get_reso_dist(&IMX464->support_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			IMX464->support_modes[i].bus_fmt == framefmt->code) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &IMX464->support_modes[cur_best_fit];
}

static int IMX464_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct IMX464 *IMX464 = to_IMX464(sd);
	const struct IMX464_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&IMX464->mutex);

	mode = IMX464_find_best_fit(IMX464, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&IMX464->mutex);
		return -ENOTTY;
#endif
	} else {
		IMX464->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(IMX464->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(IMX464->vblank, vblank_def,
					 IMX464_VTS_MAX - mode->height,
					 1, vblank_def);
		IMX464->cur_vts = IMX464->cur_mode->vts_def;
		pixel_rate = (u32)link_freq_menu_items[mode->mipi_freq_idx] / mode->bpp * 2 *
			     IMX464->bus_cfg.bus.mipi_csi2.num_data_lanes;
		__v4l2_ctrl_s_ctrl_int64(IMX464->pixel_rate,
					 pixel_rate);
		__v4l2_ctrl_s_ctrl(IMX464->link_freq,
				   mode->mipi_freq_idx);
	}

	mutex_unlock(&IMX464->mutex);

	return 0;
}

static int IMX464_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct IMX464 *IMX464 = to_IMX464(sd);
	const struct IMX464_mode *mode = IMX464->cur_mode;

	mutex_lock(&IMX464->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&IMX464->mutex);
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
	mutex_unlock(&IMX464->mutex);

	return 0;
}

static int IMX464_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct IMX464 *IMX464 = to_IMX464(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = IMX464->cur_mode->bus_fmt;

	return 0;
}

static int IMX464_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct IMX464 *IMX464 = to_IMX464(sd);

	if (fse->index >= IMX464->cfg_num)
		return -EINVAL;

	if (fse->code != IMX464->support_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = IMX464->support_modes[fse->index].width;
	fse->max_width  = IMX464->support_modes[fse->index].width;
	fse->max_height = IMX464->support_modes[fse->index].height;
	fse->min_height = IMX464->support_modes[fse->index].height;

	return 0;
}

static int IMX464_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct IMX464 *IMX464 = to_IMX464(sd);
	const struct IMX464_mode *mode = IMX464->cur_mode;

	mutex_lock(&IMX464->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&IMX464->mutex);

	return 0;
}

static int IMX464_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct IMX464 *IMX464 = to_IMX464(sd);
	const struct IMX464_mode *mode = IMX464->cur_mode;
	u32 val = 0;
	u32 lane_num = IMX464->bus_cfg.bus.mipi_csi2.num_data_lanes;

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

static void IMX464_get_module_inf(struct IMX464 *IMX464,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, IMX464_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, IMX464->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, IMX464->len_name, sizeof(inf->base.lens));
}

static int IMX464_set_hdrae(struct IMX464 *IMX464,
			    struct preisp_hdrae_exp_s *ae)
{
	struct i2c_client *client = IMX464->client;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	u32 gain_switch = 0;
	u32 shr1 = 0;
	u32 shr0 = 0;
	u32 rhs1 = 0;
	u32 rhs1_max = 0;
	static int rhs1_old = IMX464_RHS1_DEFAULT;
	int rhs1_change_limit;
	int ret = 0;
	u32 fsc = IMX464->cur_vts;
	u8 cg_mode = 0;

	if (!IMX464->has_init_exp && !IMX464->streaming) {
		IMX464->init_hdrae_exp = *ae;
		IMX464->has_init_exp = true;
		dev_dbg(&IMX464->client->dev, "IMX464 don't stream, record exp for hdr!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;
	dev_dbg(&client->dev,
		"rev exp req: L_exp: 0x%x, 0x%x, M_exp: 0x%x, 0x%x S_exp: 0x%x, 0x%x\n",
		l_exp_time, m_exp_time, s_exp_time,
		l_a_gain, m_a_gain, s_a_gain);

	if (IMX464->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
		cg_mode = ae->middle_cg_mode;
	}
	if (!IMX464->isHCG && cg_mode == GAIN_MODE_HCG) {
		gain_switch = 0x01 | 0x100;
		IMX464->isHCG = true;
	} else if (IMX464->isHCG && cg_mode == GAIN_MODE_LCG) {
		gain_switch = 0x00 | 0x100;
		IMX464->isHCG = false;
	}
	ret = imx464_write_reg(client,
		IMX464_GROUP_HOLD_REG,
		IMX464_REG_VALUE_08BIT,
		IMX464_GROUP_HOLD_START);
	//gain effect n+1
	ret |= imx464_write_reg(client,
		IMX464_LF_GAIN_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_GAIN_H(l_a_gain));
	ret |= imx464_write_reg(client,
		IMX464_LF_GAIN_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_GAIN_L(l_a_gain));
	ret |= imx464_write_reg(client,
		IMX464_SF1_GAIN_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_GAIN_H(s_a_gain));
	ret |= imx464_write_reg(client,
		IMX464_SF1_GAIN_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_GAIN_L(s_a_gain));
	if (gain_switch & 0x100)
		ret |= imx464_write_reg(client,
			IMX464_GAIN_SWITCH_REG,
			IMX464_REG_VALUE_08BIT,
			gain_switch & 0xff);
	/* Restrictions
	 *     FSC = 2 * VMAX = 4n                   (4n, align with 4)
	 *   SHR1 + 9 <= SHR0 <= (FSC - 2)
	 *
	 *   exp_l = FSC - SHR0
	 *    SHR0 = FSC - exp_l                     (2n, align with 2)
	 *
	 *   exp_s = RHS1 - SHR1
	 *    SHR1 + 2 <= RHS1 < BRL * 2             (4n + 1)
	 *    SHR1 + 2 <= RHS1 <= SHR0 - 9
	 *          9 <= SHR1 <= RHS1 - 2           (2n + 1)
	 *
	 *    RHS1(n+1) >= (RHS1(n) + BRL * 2) - FSC + 2
	 *
	 *    RHS1 and SHR1 shall be even value.
	 *
	 *    T(l_exp) = FSC - SHR0,  unit: H
	 *    T(s_exp) = RHS1 - SHR1, unit: H
	 *    Exposure ratio: T(l_exp) / T(s_exp) >= 1
	 */

	/* The HDR mode vts is already double by default to workaround T-line */

	//long exposure and short exposure
	shr0 = fsc - l_exp_time;
	rhs1_max = (RHS1_MAX > (shr0 - 9)) ? (shr0 - 9) : RHS1_MAX;
	rhs1 = SHR1_MIN + s_exp_time;
	dev_err(&client->dev, "line(%d) rhs1 %d\n", __LINE__, rhs1);
	if (rhs1 < 11)
		rhs1 = 11;
	else if (rhs1 > rhs1_max)
		rhs1 = rhs1_max;
	dev_dbg(&client->dev, "line(%d) rhs1 %d\n", __LINE__, rhs1);

	//Dynamic adjustment rhs1 must meet the following conditions
	rhs1_change_limit = rhs1_old + 2 * BRL - fsc + 2;
	rhs1_change_limit = (rhs1_change_limit < 11) ?  11 : rhs1_change_limit;
	if (rhs1_max < rhs1_change_limit)
		dev_err(&client->dev,
			"The total exposure limit makes rhs1 max is %d,but old rhs1 limit makes rhs1 min is %d\n",
			rhs1_max, rhs1_change_limit);
	if (rhs1 < rhs1_change_limit)
		rhs1 = rhs1_change_limit;

	dev_dbg(&client->dev,
		"line(%d) rhs1 %d,short time %d rhs1_old %d test %d\n",
		__LINE__, rhs1, s_exp_time, rhs1_old,
		(rhs1_old + 2 * BRL - fsc + 2));

	rhs1 = (rhs1 >> 2) * 4 + 1;
	rhs1_old = rhs1;

	if (rhs1 - s_exp_time <= SHR1_MIN) {
		shr1 = SHR1_MIN;
		s_exp_time = rhs1 - shr1;
	} else {
		shr1 = rhs1 - s_exp_time;
	}

	if (shr1 < 9)
		shr1 = 9;
	else if (shr1 > (rhs1 - 2))
		shr1 = rhs1 - 2;

	if (shr0 < (rhs1 + 9))
		shr0 = rhs1 + 9;
	else if (shr0 > (fsc - 2))
		shr0 = fsc - 2;

	dev_dbg(&client->dev,
		"fsc=%d,RHS1_MAX=%d,SHR1_MIN=%d,rhs1_max=%d\n",
		fsc, RHS1_MAX, SHR1_MIN, rhs1_max);
	dev_dbg(&client->dev,
		"l_exp_time=%d,s_exp_time=%d,shr0=%d,shr1=%d,rhs1=%d,l_a_gain=%d,s_a_gain=%d\n",
		l_exp_time, s_exp_time, shr0, shr1, rhs1, l_a_gain, s_a_gain);
	//time effect n+2
	ret |= imx464_write_reg(client,
		IMX464_RHS1_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_RHS1_L(rhs1));
	ret |= imx464_write_reg(client,
		IMX464_RHS1_REG_M,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_RHS1_M(rhs1));
	ret |= imx464_write_reg(client,
		IMX464_RHS1_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_RHS1_H(rhs1));

	ret |= imx464_write_reg(client,
		IMX464_SF1_EXPO_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_L(shr1));
	ret |= imx464_write_reg(client,
		IMX464_SF1_EXPO_REG_M,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_M(shr1));
	ret |= imx464_write_reg(client,
		IMX464_SF1_EXPO_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_H(shr1));
	ret |= imx464_write_reg(client,
		IMX464_LF_EXPO_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_L(shr0));
	ret |= imx464_write_reg(client,
		IMX464_LF_EXPO_REG_M,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_M(shr0));
	ret |= imx464_write_reg(client,
		IMX464_LF_EXPO_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_H(shr0));
	ret |= imx464_write_reg(client,
		IMX464_GROUP_HOLD_REG,
		IMX464_REG_VALUE_08BIT,
		IMX464_GROUP_HOLD_END);
	return ret;
}

static int IMX464_set_hdrae_3frame(struct IMX464 *IMX464,
				   struct preisp_hdrae_exp_s *ae)
{
	struct i2c_client *client = IMX464->client;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	int shr2, shr1, shr0, rhs2, rhs1 = 0;
	int rhs1_change_limit, rhs2_change_limit = 0;
	static int rhs1_old = IMX464_RHS1_X3_DEFAULT;
	static int rhs2_old = IMX464_RHS2_X3_DEFAULT;
	int ret = 0;
	u32 gain_switch = 0;
	u8 cg_mode = 0;
	u32 fsc;
	int rhs1_max = 0;
	int shr2_min = 0;

	if (!IMX464->has_init_exp && !IMX464->streaming) {
		IMX464->init_hdrae_exp = *ae;
		IMX464->has_init_exp = true;
		dev_dbg(&IMX464->client->dev, "IMX464 is not streaming, save hdr ae!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;

	if (IMX464->cur_mode->hdr_mode == HDR_X3) {
		//3 stagger
		cg_mode = ae->long_cg_mode;
	}
	if (!IMX464->isHCG && cg_mode == GAIN_MODE_HCG) {
		gain_switch = 0x01 | 0x100;
		IMX464->isHCG = true;
	} else if (IMX464->isHCG && cg_mode == GAIN_MODE_LCG) {
		gain_switch = 0x00 | 0x100;
		IMX464->isHCG = false;
	}

	dev_dbg(&client->dev,
		"rev exp req: L_exp: 0x%x, 0x%x, M_exp: 0x%x, 0x%x S_exp: 0x%x, 0x%x\n",
		l_exp_time, l_a_gain, m_exp_time, m_a_gain, s_exp_time, s_a_gain);

	ret = imx464_write_reg(client, IMX464_GROUP_HOLD_REG,
		IMX464_REG_VALUE_08BIT, IMX464_GROUP_HOLD_START);
	/* gain effect n+1 */
	ret |= imx464_write_reg(client, IMX464_LF_GAIN_REG_H,
		IMX464_REG_VALUE_08BIT, IMX464_FETCH_GAIN_H(l_a_gain));
	ret |= imx464_write_reg(client, IMX464_LF_GAIN_REG_L,
		IMX464_REG_VALUE_08BIT, IMX464_FETCH_GAIN_L(l_a_gain));
	ret |= imx464_write_reg(client, IMX464_SF1_GAIN_REG_H,
		IMX464_REG_VALUE_08BIT, IMX464_FETCH_GAIN_H(m_a_gain));
	ret |= imx464_write_reg(client, IMX464_SF1_GAIN_REG_L,
		IMX464_REG_VALUE_08BIT, IMX464_FETCH_GAIN_L(m_a_gain));
	ret |= imx464_write_reg(client, IMX464_SF2_GAIN_REG_H,
		IMX464_REG_VALUE_08BIT, IMX464_FETCH_GAIN_H(s_a_gain));
	ret |= imx464_write_reg(client, IMX464_SF2_GAIN_REG_L,
		IMX464_REG_VALUE_08BIT, IMX464_FETCH_GAIN_L(s_a_gain));
	if (gain_switch & 0x100)
		ret |= imx464_write_reg(client,
			IMX464_GAIN_SWITCH_REG,
			IMX464_REG_VALUE_08BIT,
			gain_switch & 0xff);

	/* Restrictions
	 *   FSC = 4 * VMAX and FSC should be 6n;
	 *   exp_l = FSC - SHR0 + Toffset;
	 *
	 *   SHR0 = FSC - exp_l + Toffset;
	 *   SHR0 <= (FSC -3);
	 *   SHR0 >= RHS2 + 13;
	 *   SHR0 should be 3n;
	 *
	 *   exp_m = RHS1 - SHR1 + Toffset;
	 *
	 *   RHS1 < BRL * 3;
	 *   RHS1 <= SHR2 - 13;
	 *   RHS1 >= SHR1 + 3;
	 *   SHR1 >= 13;
	 *   SHR1 <= RHS1 - 3;
	 *   RHS1(n+1) >= RHS1(n) + BRL * 3 -FSC + 3;
	 *
	 *   SHR1 should be 3n+1 and RHS1 should be 6n+1;
	 *
	 *   exp_s = RHS2 - SHR2 + Toffset;
	 *
	 *   RHS2 < BRL * 3 + RHS1;
	 *   RHS2 <= SHR0 - 13;
	 *   RHS2 >= SHR2 + 3;
	 *   SHR2 >= RHS1 + 13;
	 *   SHR2 <= RHS2 - 3;
	 *   RHS1(n+1) >= RHS1(n) + BRL * 3 -FSC + 3;
	 *
	 *   SHR2 should be 3n+2 and RHS2 should be 6n+2;
	 */

	/* The HDR mode vts is double by default to workaround T-line */
	fsc = IMX464->cur_vts;
	shr0 = fsc - l_exp_time;
	dev_dbg(&client->dev,
		"line(%d) shr0 %d, l_exp_time %d, fsc %d\n",
		__LINE__, shr0, l_exp_time, fsc);

	rhs1 = (SHR1_MIN_X3 + m_exp_time + 5) / 6 * 6 + 1;
	rhs1_max = RHS1_MAX_X3;
	if (rhs1 < SHR1_MIN_X3 + 3)
		rhs1 = SHR1_MIN_X3 + 3;
	else if (rhs1 > rhs1_max)
		rhs1 = rhs1_max;

	dev_dbg(&client->dev,
		"line(%d) rhs1 %d, m_exp_time %d rhs1_old %d\n",
		__LINE__, rhs1, m_exp_time, rhs1_old);

	//Dynamic adjustment rhs2 must meet the following conditions

	rhs1_change_limit = rhs1_old + 3 * BRL - fsc + 3;
	rhs1_change_limit = (rhs1_change_limit < 16) ? 16 : rhs1_change_limit;
	rhs1_change_limit = (rhs1_change_limit + 5) / 6 * 6 + 1;
	if (rhs1_max < rhs1_change_limit) {
		dev_err(&client->dev,
			"The total exposure limit makes rhs1 max is %d,but old rhs1 limit makes rhs1 min is %d\n",
			rhs1_max, rhs1_change_limit);
		return -EINVAL;
	}
	if (rhs1 < rhs1_change_limit)
		rhs1 = rhs1_change_limit;

	dev_dbg(&client->dev,
		"line(%d) m_exp_time %d rhs1_old %d, rhs1_new %d\n",
		__LINE__, m_exp_time, rhs1_old, rhs1);

	rhs1_old = rhs1;

	/* shr1 = rhs1 - s_exp_time */
	if (rhs1 - m_exp_time <= SHR1_MIN_X3) {
		shr1 = SHR1_MIN_X3;
		m_exp_time = rhs1 - shr1;
	} else {
		shr1 = rhs1 - m_exp_time;
	}

	shr2_min = rhs1 + 13;
	rhs2 =  (shr2_min + s_exp_time + 5) / 6 * 6 + 2;
	if (rhs2 > (shr0 - 13))
		rhs2 = shr0 - 13;
	else if (rhs2 < 32)//16+13 +3
		rhs2 = 32;

	dev_err(&client->dev,
		"line(%d) rhs2 %d, s_exp_time %d, rhs2_old %d\n",
		__LINE__, rhs2, s_exp_time, rhs2_old);

	//Dynamic adjustment rhs2 must meet the following conditions
	//RHS2(N+1) > (RHS2(N) + BRL ¡Á 3) ¨C VMAX ¡Á 4) + 3
	rhs2_change_limit = rhs2_old + 3 * BRL - fsc + 3;
	rhs2_change_limit = (rhs2_change_limit < 32) ?  32 : rhs2_change_limit;
	rhs2_change_limit = (rhs2_change_limit + 5) / 6 * 6 + 2;
	if ((shr0 - 13) < rhs2_change_limit) {
		dev_err(&client->dev,
			"The total exposure limit makes rhs2 max is %d,but old rhs1 limit makes rhs2 min is %d\n",
			shr0 - 13, rhs2_change_limit);
		return -EINVAL;
	}
	if (rhs2 < rhs2_change_limit)
		rhs2 = rhs2_change_limit;

	rhs2_old = rhs2;

	/* shr2 = rhs2 - s_exp_time */
	if (rhs2 - s_exp_time <= shr2_min) {
		shr2 = shr2_min;
		s_exp_time = rhs2 - shr2;
	} else {
		shr2 = rhs2 - s_exp_time;
	}
	dev_dbg(&client->dev,
		"line(%d) rhs2_new %d, s_exp_time %d shr2 %d, rhs2_change_limit %d\n",
		__LINE__, rhs2, s_exp_time, shr2, rhs2_change_limit);

	if (shr0 < rhs2 + 13)
		shr0 = rhs2 + 13;
	else if (shr0 > fsc - 3)
		shr0 = fsc - 3;

	dev_dbg(&client->dev,
		"long exposure: l_exp_time=%d, fsc=%d, shr0=%d, l_a_gain=%d\n",
		l_exp_time, fsc, shr0, l_a_gain);
	dev_dbg(&client->dev,
		"middle exposure(SEF1): m_exp_time=%d, rhs1=%d, shr1=%d, m_a_gain=%d\n",
		m_exp_time, rhs1, shr1, m_a_gain);
	dev_dbg(&client->dev,
		"short exposure(SEF2): s_exp_time=%d, rhs2=%d, shr2=%d, s_a_gain=%d\n",
		s_exp_time, rhs2, shr2, s_a_gain);
	/* time effect n+1 */
	/* write SEF2 exposure RHS2 regs*/
	ret |= imx464_write_reg(client,
		IMX464_RHS2_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_RHS1_L(rhs2));
	ret |= imx464_write_reg(client,
		IMX464_RHS2_REG_M,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_RHS1_M(rhs2));
	ret |= imx464_write_reg(client,
		IMX464_RHS2_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_RHS1_H(rhs2));
	/* write SEF2 exposure SHR2 regs*/
	ret |= imx464_write_reg(client,
		IMX464_SF2_EXPO_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_L(shr2));
	ret |= imx464_write_reg(client,
		IMX464_SF2_EXPO_REG_M,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_M(shr2));
	ret |= imx464_write_reg(client,
		IMX464_SF2_EXPO_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_H(shr2));
	/* write SEF1 exposure RHS1 regs*/
	ret |= imx464_write_reg(client,
		IMX464_RHS1_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_RHS1_L(rhs1));
	ret |= imx464_write_reg(client,
		IMX464_RHS1_REG_M,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_RHS1_M(rhs1));
	ret |= imx464_write_reg(client,
		IMX464_RHS1_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_RHS1_H(rhs1));
	/* write SEF1 exposure SHR1 regs*/
	ret |= imx464_write_reg(client,
		IMX464_SF1_EXPO_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_L(shr1));
	ret |= imx464_write_reg(client,
		IMX464_SF1_EXPO_REG_M,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_M(shr1));
	ret |= imx464_write_reg(client,
		IMX464_SF1_EXPO_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_H(shr1));
	/* write LF exposure SHR0 regs*/
	ret |= imx464_write_reg(client,
		IMX464_LF_EXPO_REG_L,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_L(shr0));
	ret |= imx464_write_reg(client,
		IMX464_LF_EXPO_REG_M,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_M(shr0));
	ret |= imx464_write_reg(client,
		IMX464_LF_EXPO_REG_H,
		IMX464_REG_VALUE_08BIT,
		IMX464_FETCH_EXP_H(shr0));

	ret |= imx464_write_reg(client, IMX464_GROUP_HOLD_REG,
		IMX464_REG_VALUE_08BIT, IMX464_GROUP_HOLD_END);
	return ret;
}

static int IMX464_set_conversion_gain(struct IMX464 *IMX464, u32 *cg)
{
	int ret = 0;
	struct i2c_client *client = IMX464->client;
	int cur_cg = *cg;
	u32 gain_switch = 0;

	if (IMX464->isHCG && cur_cg == GAIN_MODE_LCG) {
		gain_switch = 0x00 | 0x100;
		IMX464->isHCG = false;
	} else if (!IMX464->isHCG && cur_cg == GAIN_MODE_HCG) {
		gain_switch = 0x01 | 0x100;
		IMX464->isHCG = true;
	}
	ret = imx464_write_reg(client,
			IMX464_GROUP_HOLD_REG,
			IMX464_REG_VALUE_08BIT,
			IMX464_GROUP_HOLD_START);
	if (gain_switch & 0x100)
		ret |= imx464_write_reg(client,
			IMX464_GAIN_SWITCH_REG,
			IMX464_REG_VALUE_08BIT,
			gain_switch & 0xff);
	ret |= imx464_write_reg(client,
			IMX464_GROUP_HOLD_REG,
			IMX464_REG_VALUE_08BIT,
			IMX464_GROUP_HOLD_END);
	return ret;
}

#ifdef USED_SYS_DEBUG
//ag: echo 0 >  /sys/devices/platform/ff510000.i2c/i2c-1/1-0037/cam_s_cg
static ssize_t set_conversion_gain_status(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct IMX464 *IMX464 = to_IMX464(sd);
	int status = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &status);
	if (!ret && status >= 0 && status < 2)
		IMX464_set_conversion_gain(IMX464, &status);
	else
		dev_err(dev, "input 0 for LCG, 1 for HCG, cur %d\n", status);
	return count;
}

static struct device_attribute attributes[] = {
	__ATTR(cam_s_cg, S_IWUSR, NULL, set_conversion_gain_status),
};

static int add_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		if (device_create_file(dev, attributes + i))
			goto undo;
	return 0;
undo:
	for (i--; i >= 0 ; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}

static int remove_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(dev, attributes + i);
	return 0;
}
#endif

static int IMX464_get_channel_info(struct IMX464 *IMX464, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = IMX464->cur_mode->vc[ch_info->index];
	ch_info->width = IMX464->cur_mode->width;
	ch_info->height = IMX464->cur_mode->height;
	ch_info->bus_fmt = IMX464->cur_mode->bus_fmt;
	return 0;
}

static long IMX464_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct IMX464 *IMX464 = to_IMX464(sd);
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_channel_info *ch_info;
	u32 i, h, w, stream;
	long ret = 0;
	u64 pixel_rate = 0;
	u32 *sync_mode = NULL;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		if (IMX464->cur_mode->hdr_mode == HDR_X2)
			ret = IMX464_set_hdrae(IMX464, arg);
		else if (IMX464->cur_mode->hdr_mode == HDR_X3)
			ret = IMX464_set_hdrae_3frame(IMX464, arg);
		break;
	case RKMODULE_GET_MODULE_INFO:
		IMX464_get_module_inf(IMX464, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = IMX464->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = IMX464->cur_mode->width;
		h = IMX464->cur_mode->height;
		for (i = 0; i < IMX464->cfg_num; i++) {
			if (w == IMX464->support_modes[i].width &&
			    h == IMX464->support_modes[i].height &&
			    IMX464->support_modes[i].hdr_mode == hdr->hdr_mode) {
				IMX464->cur_mode = &IMX464->support_modes[i];
				break;
			}
		}
		if (i == IMX464->cfg_num) {
			dev_err(&IMX464->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = IMX464->cur_mode->hts_def - IMX464->cur_mode->width;
			h = IMX464->cur_mode->vts_def - IMX464->cur_mode->height;
			__v4l2_ctrl_modify_range(IMX464->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(IMX464->vblank, h,
				IMX464_VTS_MAX - IMX464->cur_mode->height,
				1, h);
			IMX464->cur_vts = IMX464->cur_mode->vts_def;
			pixel_rate = (u32)link_freq_menu_items[IMX464->cur_mode->mipi_freq_idx] / IMX464->cur_mode->bpp * 2 *
				     IMX464->bus_cfg.bus.mipi_csi2.num_data_lanes;
			__v4l2_ctrl_s_ctrl_int64(IMX464->pixel_rate,
						 pixel_rate);
			__v4l2_ctrl_s_ctrl(IMX464->link_freq,
					   IMX464->cur_mode->mipi_freq_idx);
		}
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = IMX464_set_conversion_gain(IMX464, (u32 *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = imx464_write_reg(IMX464->client, IMX464_REG_CTRL_MODE,
				IMX464_REG_VALUE_08BIT, IMX464_MODE_STREAMING);
		else
			ret = imx464_write_reg(IMX464->client, IMX464_REG_CTRL_MODE,
				IMX464_REG_VALUE_08BIT, IMX464_MODE_SW_STANDBY);

		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = IMX464_get_channel_info(IMX464, ch_info);
		break;
	case RKMODULE_GET_SYNC_MODE:
		sync_mode = (u32 *)arg;
		*sync_mode = IMX464->sync_mode;
		break;
	case RKMODULE_SET_SYNC_MODE:
		sync_mode = (u32 *)arg;
		IMX464->sync_mode = *sync_mode;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long IMX464_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = IMX464_ioctl(sd, cmd, inf);
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
			ret = IMX464_ioctl(sd, cmd, cfg);
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

		ret = IMX464_ioctl(sd, cmd, hdr);
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
			ret = IMX464_ioctl(sd, cmd, hdr);
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
			ret = IMX464_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = copy_from_user(&cg, up, sizeof(cg));
		if (!ret)
			ret = IMX464_ioctl(sd, cmd, &cg);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = IMX464_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;

		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = IMX464_ioctl(sd, cmd, ch_info);
		if (!ret) {
			ret = copy_to_user(up, ch_info, sizeof(*ch_info));
			if (ret)
				ret = -EFAULT;
		}
		kfree(ch_info);
		break;
	case RKMODULE_GET_SYNC_MODE:
		ret = IMX464_ioctl(sd, cmd, &sync_mode);
		if (!ret) {
			ret = copy_to_user(up, &sync_mode, sizeof(u32));
			if (ret)
				ret = -EFAULT;
		}
		break;
	case RKMODULE_SET_SYNC_MODE:
		ret = copy_from_user(&sync_mode, up, sizeof(u32));
		if (!ret)
			ret = IMX464_ioctl(sd, cmd, &sync_mode);
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

static int IMX464_init_conversion_gain(struct IMX464 *IMX464, bool isHCG)
{
	struct i2c_client *client = IMX464->client;
	int ret = 0;
	u32 val = 0;

	if (isHCG)
		val = 0x01;
	else
		val = 0;
	ret = imx464_write_reg(client,
		IMX464_GAIN_SWITCH_REG,
		IMX464_REG_VALUE_08BIT,
		val);
	return ret;
}

static int __IMX464_start_stream(struct IMX464 *IMX464)
{
	int ret;

	ret = IMX464_write_array(IMX464->client, IMX464->cur_mode->reg_list);
	if (ret)
		return ret;
	ret = IMX464_init_conversion_gain(IMX464, IMX464->isHCG);
	if (ret)
		return ret;
	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&IMX464->ctrl_handler);
	if (ret)
		return ret;
	if (IMX464->has_init_exp && IMX464->cur_mode->hdr_mode != NO_HDR) {
		ret = IMX464_ioctl(&IMX464->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&IMX464->init_hdrae_exp);
		if (ret) {
			dev_err(&IMX464->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}

	if (IMX464->sync_mode == EXTERNAL_MASTER_MODE) {
		ret |= IMX464_write_array(IMX464->client, IMX464_external_sync_master_start_regs);
		v4l2_err(&IMX464->subdev, "cur externam master mode\n");
	} else if (IMX464->sync_mode == INTERNAL_MASTER_MODE) {
		ret |= IMX464_write_array(IMX464->client, IMX464_interal_sync_master_start_regs);
		v4l2_err(&IMX464->subdev, "cur intertal master\n");
	} else if (IMX464->sync_mode == SLAVE_MODE) {
		ret |= IMX464_write_array(IMX464->client, IMX464_slave_start_regs);
		v4l2_err(&IMX464->subdev, "cur slave mode\n");
	}
	if (IMX464->sync_mode == NO_SYNC_MODE) {
		ret = imx464_write_reg(IMX464->client, IMX464_REG_CTRL_MODE,
					IMX464_REG_VALUE_08BIT, IMX464_MODE_STREAMING);
		usleep_range(30000, 40000);
		ret |= imx464_write_reg(IMX464->client, IMX464_REG_MARSTER_MODE,
					IMX464_REG_VALUE_08BIT, 0);
	} else {
		ret |= imx464_write_reg(IMX464->client, IMX464_REG_MARSTER_MODE,
					IMX464_REG_VALUE_08BIT, 0);
	}
	return ret;
}

static int __IMX464_stop_stream(struct IMX464 *IMX464)
{
	int ret = 0;

	IMX464->has_init_exp = false;
	ret = imx464_write_reg(IMX464->client, IMX464_REG_CTRL_MODE,
			IMX464_REG_VALUE_08BIT, IMX464_MODE_SW_STANDBY);

	if (IMX464->sync_mode == EXTERNAL_MASTER_MODE)
		ret |= IMX464_write_array(IMX464->client, IMX464_external_sync_master_stop_regs);
	else if (IMX464->sync_mode == INTERNAL_MASTER_MODE)
		ret |= IMX464_write_array(IMX464->client, IMX464_interal_sync_master_stop_regs);
	return ret;
}

static int IMX464_s_stream(struct v4l2_subdev *sd, int on)
{
	struct IMX464 *IMX464 = to_IMX464(sd);
	struct i2c_client *client = IMX464->client;
	int ret = 0;

	mutex_lock(&IMX464->mutex);
	on = !!on;
	if (on == IMX464->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __IMX464_start_stream(IMX464);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__IMX464_stop_stream(IMX464);
		pm_runtime_put(&client->dev);
	}

	IMX464->streaming = on;

unlock_and_return:
	mutex_unlock(&IMX464->mutex);

	return ret;
}

static int IMX464_s_power(struct v4l2_subdev *sd, int on)
{
	struct IMX464 *IMX464 = to_IMX464(sd);
	struct i2c_client *client = IMX464->client;
	int ret = 0;

	mutex_lock(&IMX464->mutex);

	/* If the power state is not modified - no work to do. */
	if (IMX464->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = IMX464_write_array(IMX464->client, IMX464_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		IMX464->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		IMX464->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&IMX464->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 IMX464_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, IMX464_XVCLK_FREQ_37M / 1000 / 1000);
}

static int __IMX464_power_on(struct IMX464 *IMX464)
{
	int ret;
	u32 delay_us;
	struct device *dev = &IMX464->client->dev;

	if (!IS_ERR_OR_NULL(IMX464->pins_default)) {
		ret = pinctrl_select_state(IMX464->pinctrl,
					   IMX464->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_set_rate(IMX464->xvclk, IMX464->cur_mode->mclk);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");
	if (clk_get_rate(IMX464->xvclk) != IMX464->cur_mode->mclk)
		dev_warn(dev, "xvclk mismatched, %lu\n", clk_get_rate(IMX464->xvclk));
	else
		IMX464->cur_mclk = IMX464->cur_mode->mclk;
	ret = clk_prepare_enable(IMX464->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(IMX464->reset_gpio))
		gpiod_set_value_cansleep(IMX464->reset_gpio, 0);

	ret = regulator_bulk_enable(IMX464_NUM_SUPPLIES, IMX464->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}
	usleep_range(15000, 16000);
	if (!IS_ERR(IMX464->reset_gpio))
		gpiod_set_value_cansleep(IMX464->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(IMX464->pwdn_gpio))
		gpiod_set_value_cansleep(IMX464->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = IMX464_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(IMX464->xvclk);

	return ret;
}

static void __IMX464_power_off(struct IMX464 *IMX464)
{
	int ret;
	struct device *dev = &IMX464->client->dev;

	if (!IS_ERR(IMX464->pwdn_gpio))
		gpiod_set_value_cansleep(IMX464->pwdn_gpio, 0);
	clk_disable_unprepare(IMX464->xvclk);
	if (!IS_ERR(IMX464->reset_gpio))
		gpiod_set_value_cansleep(IMX464->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(IMX464->pins_sleep)) {
		ret = pinctrl_select_state(IMX464->pinctrl,
					   IMX464->pins_sleep);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	regulator_bulk_disable(IMX464_NUM_SUPPLIES, IMX464->supplies);
	usleep_range(15000, 16000);
}

static int IMX464_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct IMX464 *IMX464 = to_IMX464(sd);

	return __IMX464_power_on(IMX464);
}

static int IMX464_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct IMX464 *IMX464 = to_IMX464(sd);

	__IMX464_power_off(IMX464);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int IMX464_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct IMX464 *IMX464 = to_IMX464(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct IMX464_mode *def_mode = &IMX464->support_modes[0];

	mutex_lock(&IMX464->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&IMX464->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int IMX464_enum_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	struct IMX464 *IMX464 = to_IMX464(sd);

	if (fie->index >= IMX464->cfg_num)
		return -EINVAL;

	fie->code = IMX464->support_modes[fie->index].bus_fmt;
	fie->width = IMX464->support_modes[fie->index].width;
	fie->height = IMX464->support_modes[fie->index].height;
	fie->interval = IMX464->support_modes[fie->index].max_fps;
	fie->reserved[0] = IMX464->support_modes[fie->index].hdr_mode;
	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH 2560
#define DST_HEIGHT 1520

/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int IMX464_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct IMX464 *IMX464 = to_IMX464(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = CROP_START(IMX464->cur_mode->width, DST_WIDTH);
		sel->r.width = DST_WIDTH;
		sel->r.top = CROP_START(IMX464->cur_mode->height, DST_HEIGHT);
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops IMX464_pm_ops = {
	SET_RUNTIME_PM_OPS(IMX464_runtime_suspend,
			   IMX464_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops IMX464_internal_ops = {
	.open = IMX464_open,
};
#endif

static const struct v4l2_subdev_core_ops IMX464_core_ops = {
	.s_power = IMX464_s_power,
	.ioctl = IMX464_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = IMX464_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops IMX464_video_ops = {
	.s_stream = IMX464_s_stream,
	.g_frame_interval = IMX464_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops IMX464_pad_ops = {
	.enum_mbus_code = IMX464_enum_mbus_code,
	.enum_frame_size = IMX464_enum_frame_sizes,
	.enum_frame_interval = IMX464_enum_frame_interval,
	.get_fmt = IMX464_get_fmt,
	.set_fmt = IMX464_set_fmt,
	.get_selection = IMX464_get_selection,
	.get_mbus_config = IMX464_g_mbus_config,
};

static const struct v4l2_subdev_ops IMX464_subdev_ops = {
	.core	= &IMX464_core_ops,
	.video	= &IMX464_video_ops,
	.pad	= &IMX464_pad_ops,
};

static int IMX464_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct IMX464 *IMX464 = container_of(ctrl->handler,
					     struct IMX464, ctrl_handler);
	struct i2c_client *client = IMX464->client;
	const struct IMX464_mode *mode = IMX464->cur_mode;
	s64 max;
	u32 vts = 0;
	int ret = 0;
	u32 shr0 = 0;
	u32 flip = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		if (mode->hdr_mode == NO_HDR) {
			max = IMX464->cur_mode->height + ctrl->val - 3;
			__v4l2_ctrl_modify_range(IMX464->exposure,
						 IMX464->exposure->minimum, max,
						 IMX464->exposure->step,
						 IMX464->exposure->default_value);
		}
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (mode->hdr_mode == NO_HDR) {
			shr0 = IMX464->cur_vts - ctrl->val;
			ret = imx464_write_reg(IMX464->client, IMX464_LF_EXPO_REG_L,
					IMX464_REG_VALUE_08BIT,
					IMX464_FETCH_EXP_L(shr0));
			ret |= imx464_write_reg(IMX464->client, IMX464_LF_EXPO_REG_M,
					IMX464_REG_VALUE_08BIT,
					IMX464_FETCH_EXP_M(shr0));
			ret |= imx464_write_reg(IMX464->client, IMX464_LF_EXPO_REG_H,
					IMX464_REG_VALUE_08BIT,
					IMX464_FETCH_EXP_H(shr0));
			dev_err(&client->dev, "set exposure 0x%x\n",
				ctrl->val);
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (mode->hdr_mode == NO_HDR) {
			ret = imx464_write_reg(IMX464->client, IMX464_LF_GAIN_REG_H,
					IMX464_REG_VALUE_08BIT,
					IMX464_FETCH_GAIN_H(ctrl->val));
			ret |= imx464_write_reg(IMX464->client, IMX464_LF_GAIN_REG_L,
					IMX464_REG_VALUE_08BIT,
					IMX464_FETCH_GAIN_L(ctrl->val));
			dev_err(&client->dev, "set analog gain 0x%x\n",
				ctrl->val);
		}
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + IMX464->cur_mode->height;

		if (mode->hdr_mode == HDR_X2) {
			vts = (vts + 3) / 4 * 4;
			IMX464->cur_vts = vts;
			vts /= 2;
		} else if (mode->hdr_mode == HDR_X3) {
			vts = (vts + 5) / 6 * 6;
			IMX464->cur_vts = vts;
			vts /= 4;
		} else {
			IMX464->cur_vts = vts;
		}
		ret = imx464_write_reg(IMX464->client, IMX464_VTS_REG_L,
				       IMX464_REG_VALUE_08BIT,
				       IMX464_FETCH_VTS_L(vts));
		ret |= imx464_write_reg(IMX464->client, IMX464_VTS_REG_M,
				       IMX464_REG_VALUE_08BIT,
				       IMX464_FETCH_VTS_M(vts));
		ret |= imx464_write_reg(IMX464->client, IMX464_VTS_REG_H,
				       IMX464_REG_VALUE_08BIT,
				       IMX464_FETCH_VTS_H(vts));

		dev_err(&client->dev, "set vts 0x%x\n",
			vts);
		break;
	case V4L2_CID_HFLIP:
		ret = imx464_write_reg(client,
				       IMX464_GROUP_HOLD_REG,
				       IMX464_REG_VALUE_08BIT,
				       IMX464_GROUP_HOLD_START);
		ret |= imx464_write_reg(IMX464->client, IMX464_HREVERSE_REG,
				       IMX464_REG_VALUE_08BIT, !!ctrl->val);
		ret |= imx464_write_reg(client,
					IMX464_GROUP_HOLD_REG,
					IMX464_REG_VALUE_08BIT,
					IMX464_GROUP_HOLD_END);
		break;
	case V4L2_CID_VFLIP:
		flip = ctrl->val;
		ret = imx464_write_reg(client,
				       IMX464_GROUP_HOLD_REG,
				       IMX464_REG_VALUE_08BIT,
				       IMX464_GROUP_HOLD_START);
		ret |= imx464_write_reg(IMX464->client, IMX464_VREVERSE_REG,
				IMX464_REG_VALUE_08BIT, !!flip);
		if (flip) {
			ret |= imx464_write_reg(IMX464->client, 0x3074,
				IMX464_REG_VALUE_08BIT, 0x40);
			ret |= imx464_write_reg(IMX464->client, 0x3075,
				IMX464_REG_VALUE_08BIT, 0x06);
			ret |= imx464_write_reg(IMX464->client, 0x3080,
				IMX464_REG_VALUE_08BIT, 0xff);
			ret |= imx464_write_reg(IMX464->client, 0x30ad,
				IMX464_REG_VALUE_08BIT, 0x7e);
			ret |= imx464_write_reg(IMX464->client, 0x30b6,
				IMX464_REG_VALUE_08BIT, 0xff);
			ret |= imx464_write_reg(IMX464->client, 0x30b7,
				IMX464_REG_VALUE_08BIT, 0x01);
			ret |= imx464_write_reg(IMX464->client, 0x30d8,
				IMX464_REG_VALUE_08BIT, 0x45);
			ret |= imx464_write_reg(IMX464->client, 0x3114,
				IMX464_REG_VALUE_08BIT, 0x01);
		} else {
			ret |= imx464_write_reg(IMX464->client, 0x3074,
				IMX464_REG_VALUE_08BIT, 0x3c);
			ret |= imx464_write_reg(IMX464->client, 0x3075,
				IMX464_REG_VALUE_08BIT, 0x00);
			ret |= imx464_write_reg(IMX464->client, 0x3080,
				IMX464_REG_VALUE_08BIT, 0x01);
			ret |= imx464_write_reg(IMX464->client, 0x30ad,
				IMX464_REG_VALUE_08BIT, 0x02);
			ret |= imx464_write_reg(IMX464->client, 0x30b6,
				IMX464_REG_VALUE_08BIT, 0x00);
			ret |= imx464_write_reg(IMX464->client, 0x30b7,
				IMX464_REG_VALUE_08BIT, 0x00);
			ret |= imx464_write_reg(IMX464->client, 0x30d8,
				IMX464_REG_VALUE_08BIT, 0x44);
			ret |= imx464_write_reg(IMX464->client, 0x3114,
				IMX464_REG_VALUE_08BIT, 0x02);
		}
		ret |= imx464_write_reg(client,
					IMX464_GROUP_HOLD_REG,
					IMX464_REG_VALUE_08BIT,
					IMX464_GROUP_HOLD_END);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops IMX464_ctrl_ops = {
	.s_ctrl = IMX464_set_ctrl,
};

static int IMX464_initialize_controls(struct IMX464 *IMX464)
{
	const struct IMX464_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	u64 pixel_rate = 0;
	int ret;

	handler = &IMX464->ctrl_handler;
	mode = IMX464->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &IMX464->mutex;

	IMX464->link_freq = v4l2_ctrl_new_int_menu(handler,
				NULL, V4L2_CID_LINK_FREQ,
				1, 0, link_freq_menu_items);
	__v4l2_ctrl_s_ctrl(IMX464->link_freq,
			 IMX464->cur_mode->mipi_freq_idx);
	pixel_rate = (u32)link_freq_menu_items[mode->mipi_freq_idx] / mode->bpp * 2 *
		     IMX464->bus_cfg.bus.mipi_csi2.num_data_lanes;
	IMX464->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, IMX464_10BIT_HDR2_PIXEL_RATE,
		1, pixel_rate);

	h_blank = mode->hts_def - mode->width;
	IMX464->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (IMX464->hblank)
		IMX464->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	IMX464->vblank = v4l2_ctrl_new_std(handler, &IMX464_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				IMX464_VTS_MAX - mode->height,
				1, vblank_def);
	IMX464->cur_vts = mode->vts_def;

	exposure_max = mode->vts_def - 3;
	IMX464->exposure = v4l2_ctrl_new_std(handler, &IMX464_ctrl_ops,
				V4L2_CID_EXPOSURE, IMX464_EXPOSURE_MIN,
				exposure_max, IMX464_EXPOSURE_STEP,
				mode->exp_def);

	IMX464->anal_a_gain = v4l2_ctrl_new_std(handler, &IMX464_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, IMX464_GAIN_MIN,
				IMX464_GAIN_MAX, IMX464_GAIN_STEP,
				IMX464_GAIN_DEFAULT);
	v4l2_ctrl_new_std(handler, &IMX464_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &IMX464_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&IMX464->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	IMX464->subdev.ctrl_handler = handler;
	IMX464->has_init_exp = false;
	IMX464->isHCG = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int IMX464_check_sensor_id(struct IMX464 *IMX464,
				  struct i2c_client *client)
{
	struct device *dev = &IMX464->client->dev;
	u32 id = 0;
	int ret;

	ret = IMX464_read_reg(client, IMX464_REG_CHIP_ID,
			      IMX464_REG_VALUE_08BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected IMX464 id %06x\n", CHIP_ID);

	return 0;
}

static int IMX464_configure_regulators(struct IMX464 *IMX464)
{
	unsigned int i;

	for (i = 0; i < IMX464_NUM_SUPPLIES; i++)
		IMX464->supplies[i].supply = IMX464_supply_names[i];

	return devm_regulator_bulk_get(&IMX464->client->dev,
				       IMX464_NUM_SUPPLIES,
				       IMX464->supplies);
}

static int IMX464_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct IMX464 *IMX464;
	struct v4l2_subdev *sd;
	struct device_node *endpoint;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;
	const char *sync_mode_name = NULL;


	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	IMX464 = devm_kzalloc(dev, sizeof(*IMX464), GFP_KERNEL);
	if (!IMX464)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &IMX464->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &IMX464->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &IMX464->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &IMX464->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_string(node, RKMODULE_CAMERA_SYNC_MODE,
				      &sync_mode_name);
	if (ret) {
		IMX464->sync_mode = NO_SYNC_MODE;
		dev_err(dev, "could not get sync mode!\n");
	} else {
		if (strcmp(sync_mode_name, RKMODULE_EXTERNAL_MASTER_MODE) == 0)
			IMX464->sync_mode = EXTERNAL_MASTER_MODE;
		else if (strcmp(sync_mode_name, RKMODULE_INTERNAL_MASTER_MODE) == 0)
			IMX464->sync_mode = INTERNAL_MASTER_MODE;
		else if (strcmp(sync_mode_name, RKMODULE_SLAVE_MODE) == 0)
			IMX464->sync_mode = SLAVE_MODE;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE,
			&hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	IMX464->client = client;
	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint),
		&IMX464->bus_cfg);
	if (ret) {
		dev_err(dev, "Failed to get bus cfg\n");
		return ret;
	}
	if (IMX464->bus_cfg.bus.mipi_csi2.num_data_lanes == 4) {
		IMX464->support_modes = supported_modes;
		IMX464->cfg_num = ARRAY_SIZE(supported_modes);
	} else {
		IMX464->support_modes = supported_modes_2lane;
		IMX464->cfg_num = ARRAY_SIZE(supported_modes_2lane);
	}

	for (i = 0; i < IMX464->cfg_num; i++) {
		if (hdr_mode == IMX464->support_modes[i].hdr_mode) {
			IMX464->cur_mode = &IMX464->support_modes[i];
			break;
		}
	}
	IMX464->cur_mode = &IMX464->support_modes[0];
	IMX464->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(IMX464->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	IMX464->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(IMX464->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	IMX464->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(IMX464->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	IMX464->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(IMX464->pinctrl)) {
		IMX464->pins_default =
			pinctrl_lookup_state(IMX464->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(IMX464->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		IMX464->pins_sleep =
			pinctrl_lookup_state(IMX464->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(IMX464->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = IMX464_configure_regulators(IMX464);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&IMX464->mutex);

	sd = &IMX464->subdev;
	v4l2_i2c_subdev_init(sd, client, &IMX464_subdev_ops);
	ret = IMX464_initialize_controls(IMX464);
	if (ret)
		goto err_destroy_mutex;

	ret = __IMX464_power_on(IMX464);
	if (ret)
		goto err_free_handler;

	ret = IMX464_check_sensor_id(IMX464, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &IMX464_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	IMX464->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &IMX464->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(IMX464->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 IMX464->module_index, facing,
		 IMX464_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

#ifdef USED_SYS_DEBUG
	add_sysfs_interfaces(dev);
#endif
	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__IMX464_power_off(IMX464);
err_free_handler:
	v4l2_ctrl_handler_free(&IMX464->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&IMX464->mutex);

	return ret;
}

static int IMX464_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct IMX464 *IMX464 = to_IMX464(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&IMX464->ctrl_handler);
	mutex_destroy(&IMX464->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__IMX464_power_off(IMX464);
	pm_runtime_set_suspended(&client->dev);
#ifdef USED_SYS_DEBUG
	remove_sysfs_interfaces(&client->dev);
#endif
	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id IMX464_of_match[] = {
	{ .compatible = "sony,imx464" },
	{},
};
MODULE_DEVICE_TABLE(of, IMX464_of_match);
#endif

static const struct i2c_device_id IMX464_match_id[] = {
	{ "sony,imx464", 0 },
	{ },
};

static struct i2c_driver IMX464_i2c_driver = {
	.driver = {
		.name = IMX464_NAME,
		.pm = &IMX464_pm_ops,
		.of_match_table = of_match_ptr(IMX464_of_match),
	},
	.probe		= &IMX464_probe,
	.remove		= &IMX464_remove,
	.id_table	= IMX464_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&IMX464_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&IMX464_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony IMX464 sensor driver");
MODULE_LICENSE("GPL v2");
