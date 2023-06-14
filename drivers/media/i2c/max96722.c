// SPDX-License-Identifier: GPL-2.0
/*
 * max96722 GMSL2/GMSL1 to CSI-2 Deserializer driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V1.0X00.0X00 Support New Driver Framework.
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <linux/of_graph.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION			KERNEL_VERSION(1, 0x00, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MAX96722_LINK_FREQ_MHZ(x)	((x) * 1000000UL)
#define MAX96722_XVCLK_FREQ		25000000

#define MAX96722_CHIP_ID		0xA1
#define MAX96722_REG_CHIP_ID		0x0D

#define MAX96715_CHIP_ID		0x45
#define MAX96715_REG_CHIP_ID		0x1E

#define MAX9295_CHIP_ID			0x91
#define MAX9295_REG_CHIP_ID		0x0D

#define MAX96717_CHIP_ID		0xBF
#define MAX96717_REG_CHIP_ID		0x0D

#define MAX96722_REMOTE_CTRL		0x0003
#define MAX96722_REMOTE_DISABLE		0xFF

/* max96722->link mask: link type = bit[7:4], link mask = bit[3:0] */
#define MAX96722_GMSL_TYPE_LINK_A	BIT(4)
#define MAX96722_GMSL_TYPE_LINK_B	BIT(5)
#define MAX96722_GMSL_TYPE_LINK_C	BIT(6)
#define MAX96722_GMSL_TYPE_LINK_D	BIT(7)
#define MAX96722_GMSL_TYPE_MASK		0xF0 /* bit[7:4], GMSL link type: 0 = GMSL1, 1 = GMSL2 */

#define MAX96722_LOCK_STATE_LINK_A	BIT(0)
#define MAX96722_LOCK_STATE_LINK_B	BIT(1)
#define MAX96722_LOCK_STATE_LINK_C	BIT(2)
#define MAX96722_LOCK_STATE_LINK_D	BIT(3)
#define MAX96722_LOCK_STATE_MASK	0x0F /* bit[3:0], GMSL link mask: 1 = disable, 1 = enable */

#define MAX96722_FORCE_ALL_CLOCK_EN	1 /* 1: enable, 0: disable */

#define REG_NULL			0xFFFF

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define MAX96722_NAME			"max96722"

/* register length: 8bit or 16bit */
#define MAX96722_REG_LENGTH_08BIT	1
#define MAX96722_REG_LENGTH_16BIT	2

/* register value: 8bit or 16bit or 24bit */
#define MAX96722_REG_VALUE_08BIT	1
#define MAX96722_REG_VALUE_16BIT	2
#define MAX96722_REG_VALUE_24BIT	3

#define MAX96722_I2C_ADDR		(0x29)
#define MAX9295_I2C_ADDR		(0x40)
#define MAX96715_I2C_ADDR		(0x40)
#define MAX96717_I2C_ADDR		(0x40)
#define CAMERA_I2C_ADDR			(0x36)

#define MAX96722_GET_BIT(x, bit)	((x & (1 << bit)) >> bit)
#define MAX96722_GET_BIT_M_TO_N(x, m, n)	\
		((unsigned int)(x << (31 - (n))) >> ((31 - (n)) + (m)))

static const char *const max96722_supply_names[] = {
	"avdd", /* Analog power */
	"dovdd", /* Digital I/O power */
	"dvdd", /* Digital core power */
};

#define MAX96722_NUM_SUPPLIES		ARRAY_SIZE(max96722_supply_names)

struct regval {
	u16 i2c_addr;
	u16 reg_len;
	u16 reg;
	u8 val;
	u8 mask;
	u16 delay;
};

struct max96722_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u32 bus_fmt;
	u32 bpp;
	const struct regval *reg_list;
	u32 vc[PAD_MAX];
};

struct max96722 {
	struct i2c_client *client;
	struct clk *xvclk;
	struct gpio_desc *power_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct gpio_desc *pocen_gpio;
	struct gpio_desc *lock_gpio;
	struct regulator_bulk_data supplies[MAX96722_NUM_SUPPLIES];

	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_sleep;

	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *anal_gain;
	struct v4l2_ctrl *digi_gain;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *test_pattern;
	struct v4l2_fwnode_endpoint bus_cfg;

	struct mutex mutex;
	bool streaming;
	bool power_on;
	bool hot_plug;
	u8 is_reset;
	int hot_plug_irq;
	u32 link_mask;
	const struct max96722_mode *supported_modes;
	const struct max96722_mode *cur_mode;
	u32 cfg_modes_num;
	u32 module_index;
	u32 auto_init_deskew_mask;
	u32 frame_sync_period;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
	struct regmap *regmap;
};

static const struct regmap_config max96722_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x1F17,
};

static struct rkmodule_csi_dphy_param rk3588_dcphy_param = {
	.vendor = PHY_VENDOR_SAMSUNG,
	.lp_vol_ref = 3,
	.lp_hys_sw = {3, 0, 0, 0},
	.lp_escclk_pol_sel = {1, 0, 0, 0},
	.skew_data_cal_clk = {0, 0, 0, 0},
	.clk_hs_term_sel = 2,
	.data_hs_term_sel = {2, 2, 2, 2},
	.reserved = {0},
};

/* Max96715 */
static const struct regval max96722_mipi_4lane_1280x800_30fps[] = {
	// Link A/B/C/D all use GMSL1, and disabled
	{ 0x29, 2, 0x0006, 0x00, 0x00, 0x00 }, // Link A/B/C/D: select GMSL1, Disabled
	// Disable MIPI CSI output
	{ 0x29, 2, 0x040B, 0x00, 0x00, 0x00 }, // CSI_OUT_EN=0, CSI output disabled
	// Increase CMU voltage
	{ 0x29, 2, 0x06C2, 0x10, 0x00, 0x0a }, // Increase CMU voltage to for wide temperature range
	// VGAHiGain
	{ 0x29, 2, 0x14D1, 0x03, 0x00, 0x00 }, // VGAHiGain
	{ 0x29, 2, 0x15D1, 0x03, 0x00, 0x00 }, // VGAHiGain
	{ 0x29, 2, 0x16D1, 0x03, 0x00, 0x00 }, // VGAHiGain
	{ 0x29, 2, 0x17D1, 0x03, 0x00, 0x0a }, // VGAHiGain
	// SSC Configuration
	{ 0x29, 2, 0x1445, 0x00, 0x00, 0x00 }, // Disable SSC
	{ 0x29, 2, 0x1545, 0x00, 0x00, 0x00 }, // Disable SSC
	{ 0x29, 2, 0x1645, 0x00, 0x00, 0x00 }, // Disable SSC
	{ 0x29, 2, 0x1745, 0x00, 0x00, 0x0a }, // Disable SSC
	// GMSL1 configuration to match serializer
	{ 0x29, 2, 0x0B07, 0x84, 0x00, 0x00 }, // Enable HVEN and DBL (application specific)
	{ 0x29, 2, 0x0C07, 0x84, 0x00, 0x00 }, // Enable HVEN and DBL (application specific)
	{ 0x29, 2, 0x0D07, 0x84, 0x00, 0x00 }, // Enable HVEN and DBL (application specific)
	{ 0x29, 2, 0x0E07, 0x84, 0x00, 0x00 }, // Enable HVEN and DBL (application specific)
	{ 0x29, 2, 0x0B0F, 0x01, 0x00, 0x00 }, // Disable processing HS and DE signals(required when paring with GMSL1 parallel serializers)
	{ 0x29, 2, 0x0C0F, 0x01, 0x00, 0x00 }, // Disable processing HS and DE signals(required when paring with GMSL1 parallel serializers)
	{ 0x29, 2, 0x0D0F, 0x01, 0x00, 0x00 }, // Disable processing HS and DE signals(required when paring with GMSL1 parallel serializers)
	{ 0x29, 2, 0x0E0F, 0x01, 0x00, 0x00 }, // Disable processing HS and DE signals(required when paring with GMSL1 parallel serializers)
	// Send YUV422, FS, and FE from Video Pipe 0 to Controller 1
	{ 0x29, 2, 0x090B, 0x07, 0x00, 0x00 }, // Enable 0/1/2 SRC/DST Mappings
	{ 0x29, 2, 0x092D, 0x15, 0x00, 0x00 }, // SRC/DST 0/1/2 -> CSI2 Controller 1;
	// For the following MSB 2 bits = VC, LSB 6 bits = DT
	{ 0x29, 2, 0x090D, 0x1e, 0x00, 0x00 }, // SRC0 VC = 0, DT = YUV422 8bit
	{ 0x29, 2, 0x090E, 0x1e, 0x00, 0x00 }, // DST0 VC = 0, DT = YUV422 8bit
	{ 0x29, 2, 0x090F, 0x00, 0x00, 0x00 }, // SRC1 VC = 0, DT = Frame Start
	{ 0x29, 2, 0x0910, 0x00, 0x00, 0x00 }, // DST1 VC = 0, DT = Frame Start
	{ 0x29, 2, 0x0911, 0x01, 0x00, 0x00 }, // SRC2 VC = 0, DT = Frame End
	{ 0x29, 2, 0x0912, 0x01, 0x00, 0x00 }, // DST2 VC = 0, DT = Frame End
	// Send YUV422, FS, and FE from Video Pipe 1 to Controller 1
	{ 0x29, 2, 0x094B, 0x07, 0x00, 0x00 }, // Enable 0/1/2 SRC/DST Mappings
	{ 0x29, 2, 0x096D, 0x15, 0x00, 0x00 }, // SRC/DST 0/1/2 -> CSI2 Controller 1;
	// For the following MSB 2 bits = VC, LSB 6 bits = DT
	{ 0x29, 2, 0x094D, 0x1e, 0x00, 0x00 }, // SRC0 VC = 0, DT = YUV422 8bit
	{ 0x29, 2, 0x094E, 0x5e, 0x00, 0x00 }, // DST0 VC = 1, DT = YUV422 8bit
	{ 0x29, 2, 0x094F, 0x00, 0x00, 0x00 }, // SRC1 VC = 0, DT = Frame Start
	{ 0x29, 2, 0x0950, 0x40, 0x00, 0x00 }, // DST1 VC = 1, DT = Frame Start
	{ 0x29, 2, 0x0951, 0x01, 0x00, 0x00 }, // SRC2 VC = 0, DT = Frame End
	{ 0x29, 2, 0x0952, 0x41, 0x00, 0x00 }, // DST2 VC = 1, DT = Frame End
	// Send YUV422, FS, and FE from Video Pipe 2 to Controller 1
	{ 0x29, 2, 0x098B, 0x07, 0x00, 0x00 }, // Enable 0/1/2 SRC/DST Mappings
	{ 0x29, 2, 0x09AD, 0x15, 0x00, 0x00 }, // SRC/DST 0/1/2 -> CSI2 Controller 1;
	// For the following MSB 2 bits = VC, LSB 6 bits = DT
	{ 0x29, 2, 0x098D, 0x1e, 0x00, 0x00 }, // SRC0 VC = 0, DT = YUV422 8bit
	{ 0x29, 2, 0x098E, 0x9e, 0x00, 0x00 }, // DST0 VC = 2, DT = YUV422 8bit
	{ 0x29, 2, 0x098F, 0x00, 0x00, 0x00 }, // SRC1 VC = 0, DT = Frame Start
	{ 0x29, 2, 0x0990, 0x80, 0x00, 0x00 }, // DST1 VC = 2, DT = Frame Start
	{ 0x29, 2, 0x0991, 0x01, 0x00, 0x00 }, // SRC2 VC = 0, DT = Frame End
	{ 0x29, 2, 0x0992, 0x81, 0x00, 0x00 }, // DST2 VC = 2, DT = Frame End
	// Send YUV422, FS, and FE from Video Pipe 3 to Controller 1
	{ 0x29, 2, 0x09CB, 0x07, 0x00, 0x00 }, // Enable 0/1/2 SRC/DST Mappings
	{ 0x29, 2, 0x09ED, 0x15, 0x00, 0x00 }, // SRC/DST 0/1/2 -> CSI2 Controller 1;
	// For the following MSB 2 bits = VC, LSB 6 bits = DT
	{ 0x29, 2, 0x09CD, 0x1e, 0x00, 0x00 }, // SRC0 VC = 0, DT = YUV422 8bit
	{ 0x29, 2, 0x09CE, 0xde, 0x00, 0x00 }, // DST0 VC = 3, DT = YUV422 8bit
	{ 0x29, 2, 0x09CF, 0x00, 0x00, 0x00 }, // SRC1 VC = 0, DT = Frame Start
	{ 0x29, 2, 0x09D0, 0xc0, 0x00, 0x00 }, // DST1 VC = 3, DT = Frame Start
	{ 0x29, 2, 0x09D1, 0x01, 0x00, 0x00 }, // SRC2 VC = 0, DT = Frame End
	{ 0x29, 2, 0x09D2, 0xc1, 0x00, 0x00 }, // DST2 VC = 3, DT = Frame End
	// MIPI PHY Setting
	{ 0x29, 2, 0x08A0, 0x24, 0x00, 0x00 }, // DPHY0 enabled as clock, MIPI PHY Mode: 2x4 mode
	// Set Lane Mapping for 4-lane port A
	{ 0x29, 2, 0x08A3, 0xe4, 0x00, 0x00 }, // PHY1 D1->D3, D0->D2; PHY0 D1->D1, D0->D0
	// Set 4 lane D-PHY, 2bit VC
	{ 0x29, 2, 0x090A, 0xc0, 0x00, 0x00 }, // MIPI PHY 0: 4 lanes, DPHY, 2bit VC
	{ 0x29, 2, 0x094A, 0xc0, 0x00, 0x00 }, // MIPI PHY 1: 4 lanes, DPHY, 2bit VC
	// Turn on MIPI PHYs
	{ 0x29, 2, 0x08A2, 0x34, 0x00, 0x00 }, // Enable MIPI PHY 0/1, t_lpx = 106.7ns
	// Enable software override for all pipes since GMSL1 data is parallel mode, bpp=8, dt=0x1e(yuv-8)
	{ 0x29, 2, 0x040B, 0x40, 0x00, 0x00 }, // pipe 0 bpp=0x08: Datatypes = 0x2A, 0x10-12, 0x31-37
	{ 0x29, 2, 0x040C, 0x00, 0x00, 0x00 }, // pipe 0 and 1 VC software override: 0x00
	{ 0x29, 2, 0x040D, 0x00, 0x00, 0x00 }, // pipe 2 and 3 VC software override: 0x00
	{ 0x29, 2, 0x040E, 0x5e, 0x00, 0x00 }, // pipe 0 DT=0x1E: YUV422 8-bit
	{ 0x29, 2, 0x040F, 0x7e, 0x00, 0x00 }, // pipe 1 DT=0x1E: YUV422 8-bit
	{ 0x29, 2, 0x0410, 0x7a, 0x00, 0x00 }, // pipe 2 DT=0x1E, pipe 3 DT=0x1E: YUV422 8-bit
	{ 0x29, 2, 0x0411, 0x48, 0x00, 0x00 }, // pipe 1 bpp=0x08: Datatypes = 0x2A, 0x10-12, 0x31-37
	{ 0x29, 2, 0x0412, 0x20, 0x00, 0x00 }, // pipe 2 bpp=0x08, pipe 3 bpp=0x08: Datatypes = 0x2A, 0x10-12, 0x31-37
	{ 0x29, 2, 0x0415, 0xc0, 0xc0, 0x00 }, // pipe 0/1 enable software overide
	{ 0x29, 2, 0x0418, 0xc0, 0xc0, 0x00 }, // pipe 2/3 enable software overide
	{ 0x29, 2, 0x041A, 0xf0, 0x00, 0x00 }, // pipe 0/1/2/3: Enable YUV8-/10-bit mux mode
	// Enable all links and pipes
	{ 0x29, 2, 0x0003, 0xaa, 0x00, 0x00 }, // Enable Remote Control Channel Link A/B/C/D for Port 0
	{ 0x29, 2, 0x0006, 0x0f, 0x00, 0x64 }, // Enable all links and pipes
	// Serializer Setting
	{ 0x40, 1, 0x04, 0x47, 0x00, 0x05 }, // main_control: Enable CLINK
	{ 0x40, 1, 0x07, 0x84, 0x00, 0x00 }, // Config SerDes: DBL=1, BWS=0, HIBW=0, PXL_CRC=0, HVEN=1
	{ 0x40, 1, 0x67, 0xc4, 0x00, 0x00 }, // Double Alignment Mode: Align at each rising edge of HS
	{ 0x40, 1, 0x0F, 0xbf, 0x00, 0x00 }, // Enable Set GPO, GPO Output High
	{ 0x40, 1, 0x3F, 0x08, 0x00, 0x00 }, // Crossbar HS: DIN8
	{ 0x40, 1, 0x40, 0x2d, 0x00, 0x00 }, // Crossbar VS: DIN13, INVERT_MUX_VS
	{ 0x40, 1, 0x20, 0x10, 0x00, 0x00 },
	{ 0x40, 1, 0x21, 0x11, 0x00, 0x00 },
	{ 0x40, 1, 0x22, 0x12, 0x00, 0x00 },
	{ 0x40, 1, 0x23, 0x13, 0x00, 0x00 },
	{ 0x40, 1, 0x24, 0x14, 0x00, 0x00 },
	{ 0x40, 1, 0x25, 0x15, 0x00, 0x00 },
	{ 0x40, 1, 0x26, 0x16, 0x00, 0x00 },
	{ 0x40, 1, 0x27, 0x17, 0x00, 0x00 },
	{ 0x40, 1, 0x30, 0x00, 0x00, 0x00 },
	{ 0x40, 1, 0x31, 0x01, 0x00, 0x00 },
	{ 0x40, 1, 0x32, 0x02, 0x00, 0x00 },
	{ 0x40, 1, 0x33, 0x03, 0x00, 0x00 },
	{ 0x40, 1, 0x34, 0x04, 0x00, 0x00 },
	{ 0x40, 1, 0x35, 0x05, 0x00, 0x00 },
	{ 0x40, 1, 0x36, 0x06, 0x00, 0x00 },
	{ 0x40, 1, 0x37, 0x07, 0x00, 0x00 },
	{ 0x40, 1, 0x04, 0x87, 0x00, 0x05 }, // main_control: Enable Serialization
	{ 0x29, 2, REG_NULL, 0x00, 0x00, 0x00 },
};

static const struct max96722_mode supported_modes_4lane[] = {
	{
		.width = 1280,
		.height = 800,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.reg_list = max96722_mipi_4lane_1280x800_30fps,
		.link_freq_idx = 20,
		.bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
		.bpp = 16,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	},
};

/* link freq = index * MAX96722_LINK_FREQ_MHZ(50) */
static const s64 link_freq_items[] = {
	MAX96722_LINK_FREQ_MHZ(0),
	MAX96722_LINK_FREQ_MHZ(50),
	MAX96722_LINK_FREQ_MHZ(100),
	MAX96722_LINK_FREQ_MHZ(150),
	MAX96722_LINK_FREQ_MHZ(200),
	MAX96722_LINK_FREQ_MHZ(250),
	MAX96722_LINK_FREQ_MHZ(300),
	MAX96722_LINK_FREQ_MHZ(350),
	MAX96722_LINK_FREQ_MHZ(400),
	MAX96722_LINK_FREQ_MHZ(450),
	MAX96722_LINK_FREQ_MHZ(500),
	MAX96722_LINK_FREQ_MHZ(550),
	MAX96722_LINK_FREQ_MHZ(600),
	MAX96722_LINK_FREQ_MHZ(650),
	MAX96722_LINK_FREQ_MHZ(700),
	MAX96722_LINK_FREQ_MHZ(750),
	MAX96722_LINK_FREQ_MHZ(800),
	MAX96722_LINK_FREQ_MHZ(850),
	MAX96722_LINK_FREQ_MHZ(900),
	MAX96722_LINK_FREQ_MHZ(950),
	MAX96722_LINK_FREQ_MHZ(1000),
	MAX96722_LINK_FREQ_MHZ(1050),
	MAX96722_LINK_FREQ_MHZ(1100),
	MAX96722_LINK_FREQ_MHZ(1150),
	MAX96722_LINK_FREQ_MHZ(1200),
	MAX96722_LINK_FREQ_MHZ(1250),
};

static int max96722_write_reg(struct i2c_client *client,
			u16 client_addr, u16 reg, u16 reg_len, u16 val_len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_info(&client->dev, "addr(0x%02x) write reg(0x%04x, %d, 0x%02x)\n",
		client_addr, reg, reg_len, val);

	if (val_len > 4)
		return -EINVAL;

	if (reg_len == 2) {
		buf[0] = reg >> 8;
		buf[1] = reg & 0xff;

		buf_i = 2;
	} else {
		buf[0] = reg & 0xff;

		buf_i = 1;
	}

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	val_i = 4 - val_len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	client->addr = client_addr;

	if (i2c_master_send(client, buf, (val_len + reg_len)) != (val_len + reg_len)) {
		dev_err(&client->dev,
			"%s: writing register 0x%04x from 0x%02x failed\n",
			__func__, reg, client->addr);
		return -EIO;
	}

	return 0;
}

static int max96722_read_reg(struct i2c_client *client,
			u16 client_addr, u16 reg, u16 reg_len, u16 val_len, u8 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	u8 *reg_be_p;
	int ret;

	if (val_len > 4 || !val_len)
		return -EINVAL;

	client->addr = client_addr;
	data_be_p = (u8 *)&data_be;
	reg_be_p = (u8 *)&reg_addr_be;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = reg_len;
	msgs[0].buf = &reg_be_p[2 - reg_len];

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = val_len;
	msgs[1].buf = &data_be_p[4 - val_len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev,
			"%s: reading register 0x%x from 0x%x failed\n",
			__func__, reg, client->addr);
		return -EIO;
	}

	*val = be32_to_cpu(data_be);

#if 0
	dev_info(&client->dev, "addr(0x%02x) read reg(0x%04x, %d, 0x%02x)\n",
		client_addr, reg, reg_len, *val);
#endif

	return 0;
}

static int max96722_update_reg_bits(struct i2c_client *client,
			u16 client_addr, u16 reg, u16 reg_len, u8 mask, u8 val)
{
	u8 value;
	u32 val_len = MAX96722_REG_VALUE_08BIT;
	int ret;

	ret = max96722_read_reg(client, client_addr, reg, reg_len, val_len, &value);
	if (ret)
		return ret;

	value &= ~mask;
	value |= (val & mask);
	ret = max96722_write_reg(client, client_addr, reg, reg_len, val_len, value);
	if (ret)
		return ret;

	return 0;
}

static int max96722_write_array(struct i2c_client *client,
				const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].reg != REG_NULL; i++) {
		if (regs[i].mask != 0)
			ret = max96722_update_reg_bits(client, regs[i].i2c_addr,
					regs[i].reg, regs[i].reg_len,
					regs[i].mask, regs[i].val);
		else
			ret = max96722_write_reg(client, regs[i].i2c_addr,
						regs[i].reg, regs[i].reg_len,
						MAX96722_REG_VALUE_08BIT, regs[i].val);

		if (regs[i].delay != 0)
			msleep(regs[i].delay);
	}

	return ret;
}

static int max96722_check_local_chipid(struct max96722 *max96722)
{
	struct i2c_client *client = max96722->client;
	struct device *dev = &max96722->client->dev;
	int ret;
	u8 id = 0;

	ret = max96722_read_reg(client, MAX96722_I2C_ADDR,
				MAX96722_REG_CHIP_ID, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, &id);
	if ((ret != 0) || (id != MAX96722_CHIP_ID)) {
		dev_err(dev, "Unexpected MAX96722 chip id(%02x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected MAX96722 chipid: %02x\n", id);

	return 0;
}

static int __maybe_unused max96722_check_remote_chipid(struct max96722 *max96722)
{
	struct device *dev = &max96722->client->dev;
	int ret = 0;
	u8 id;

	dev_info(dev, "Check remote chipid\n");

	id = 0;
#if 0
	// max96717
	ret = max96722_read_reg(max96722->client, MAX96717_I2C_ADDR,
				MAX96717_REG_CHIP_ID, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, &id);
	if ((ret != 0) || (id != MAX96717_CHIP_ID)) {
		dev_err(dev, "Unexpected MAX96717 chip id(%02x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "Detected MAX96717 chipid: 0x%02x\n", id);
#endif

#if 0
	// max9295
	ret = max96722_read_reg(max96722->client, MAX9295_I2C_ADDR,
				MAX9295_REG_CHIP_ID, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, &id);
	if ((ret != 0) || (id != MAX9295_CHIP_ID)) {
		dev_err(dev, "Unexpected MAX9295 chip id(%02x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "Detected MAX9295 chipid: 0x%02x\n", id);
#endif

#if 0
	// max96715
	ret = max96722_read_reg(max96722->client, MAX96715_I2C_ADDR,
				MAX96715_REG_CHIP_ID, MAX96722_REG_LENGTH_08BIT,
				MAX96722_REG_VALUE_08BIT, &id);
	if ((ret != 0) || (id != MAX96715_CHIP_ID)) {
		dev_err(dev, "Unexpected MAX96715 chip id(%02x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "Detected MAX96715 chipid: 0x%02x\n", id);
#endif

	return ret;
}

static u8 max96722_get_link_lock_state(struct max96722 *max96722, u8 link_mask)
{
	struct i2c_client *client = max96722->client;
	struct device *dev = &max96722->client->dev;
	u8 lock = 0, lock_state = 0;
	u8 link_type = 0;

	link_type = max96722->link_mask & MAX96722_GMSL_TYPE_MASK;

	if (link_mask & MAX96722_LOCK_STATE_LINK_A) {
		if (link_type & MAX96722_GMSL_TYPE_LINK_A) {
			// GMSL2 LinkA
			max96722_read_reg(client, MAX96722_I2C_ADDR,
					0x001a, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, &lock);
			if (lock & BIT(3)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_A;
				dev_info(dev, "GMSL2 LinkA locked\n");
			}
		} else {
			// GMSL1 LinkA
			max96722_read_reg(client, MAX96722_I2C_ADDR,
					0x0bcb, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, &lock);
			if (lock & BIT(0)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_A;
				dev_info(dev, "GMSL1 LinkA locked\n");
			}
		}
	}

	if (link_mask & MAX96722_LOCK_STATE_LINK_B) {
		if (link_type & MAX96722_GMSL_TYPE_LINK_B) {
			// GMSL2 LinkB
			max96722_read_reg(client, MAX96722_I2C_ADDR,
					0x000a, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, &lock);
			if (lock & BIT(3)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_B;
				dev_info(dev, "GMSL2 LinkB locked\n");
			}
		} else {
			// GMSL1 LinkB
			max96722_read_reg(client, MAX96722_I2C_ADDR,
					0x0ccb, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, &lock);
			if (lock & BIT(0)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_B;
				dev_info(dev, "GMSL1 LinkB locked\n");
			}
		}
	}

	if (link_mask & MAX96722_LOCK_STATE_LINK_C) {
		if (link_type & MAX96722_GMSL_TYPE_LINK_C) {
			// GMSL2 LinkC
			max96722_read_reg(client, MAX96722_I2C_ADDR,
					0x000b, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, &lock);
			if (lock & BIT(3)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_C;
				dev_info(dev, "GMSL2 LinkC locked\n");
			}
		} else {
			// GMSL1 LinkC
			max96722_read_reg(client, MAX96722_I2C_ADDR,
					0x0dcb, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, &lock);
			if (lock & BIT(0)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_C;
				dev_info(dev, "GMSL1 LinkC locked\n");
			}
		}
	}

	if (link_mask & MAX96722_LOCK_STATE_LINK_D) {
		if (link_type & MAX96722_GMSL_TYPE_LINK_D) {
			// GMSL2 LinkD
			max96722_read_reg(client, MAX96722_I2C_ADDR,
					0x000c, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, &lock);
			if (lock & BIT(3)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_D;
				dev_info(dev, "GMSL2 LinkD locked\n");
			}
		} else {
			// GMSL1 LinkD
			max96722_read_reg(client, MAX96722_I2C_ADDR,
					0x0ecb, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, &lock);
			if (lock & BIT(0)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_D;
				dev_info(dev, "GMSL1 LinkD locked\n");
			}
		}
	}

	return lock_state;
}

static int max96722_check_link_lock_state(struct max96722 *max96722)
{
	struct i2c_client *client = max96722->client;
	struct device *dev = &max96722->client->dev;
	u8 lock_state = 0, link_mask = 0, link_type = 0;
	int ret, i, time_ms;

	ret = max96722_check_local_chipid(max96722);
	if (ret)
		return ret;

	/* IF VDD = 1.2V: Enable REG_ENABLE and REG_MNL
	 *	CTRL0: Enable REG_ENABLE
	 *	CTRL2: Enable REG_MNL
	 */
	max96722_update_reg_bits(client, MAX96722_I2C_ADDR,
				0x0017, MAX96722_REG_LENGTH_16BIT, BIT(2), BIT(2));
	max96722_update_reg_bits(client, MAX96722_I2C_ADDR,
				0x0019, MAX96722_REG_LENGTH_16BIT, BIT(4), BIT(4));

	// CSI output disabled
	max96722_write_reg(client, MAX96722_I2C_ADDR,
				0x040B, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, 0x00);

	// All links select mode by link_type and disable at beginning.
	link_type = max96722->link_mask & MAX96722_GMSL_TYPE_MASK;
	max96722_write_reg(client, MAX96722_I2C_ADDR,
				0x0006, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, link_type);

	// Link Rate
	// Link A ~ Link D Transmitter Rate: 187.5Mbps, Receiver Rate: 3Gbps
	max96722_write_reg(client, MAX96722_I2C_ADDR,
				0x0010, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, 0x11);
	max96722_write_reg(client, MAX96722_I2C_ADDR,
				0x0011, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, 0x11);

	// GMSL1: Enable HIM on deserializer on Link A/B/C/D
	if ((link_type & MAX96722_GMSL_TYPE_LINK_A) == 0) {
		max96722_write_reg(client, MAX96722_I2C_ADDR,
					0x0B06, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, 0xEF);
	}
	if ((link_type & MAX96722_GMSL_TYPE_LINK_B) == 0) {
		max96722_write_reg(client, MAX96722_I2C_ADDR,
					0x0C06, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, 0xEF);
	}
	if ((link_type & MAX96722_GMSL_TYPE_LINK_C) == 0) {
		max96722_write_reg(client, MAX96722_I2C_ADDR,
					0x0D06, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, 0xEF);
	}
	if ((link_type & MAX96722_GMSL_TYPE_LINK_D) == 0) {
		max96722_write_reg(client, MAX96722_I2C_ADDR,
					0x0E06, MAX96722_REG_LENGTH_16BIT,
					MAX96722_REG_VALUE_08BIT, 0xEF);
	}

	// Link A ~ Link D One-Shot Reset depend on link_mask
	link_mask = max96722->link_mask & MAX96722_LOCK_STATE_MASK;
	max96722_write_reg(client, MAX96722_I2C_ADDR,
				0x0018, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, link_mask);

	// Link A ~ Link D enable depend on link_type and link_mask
	max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x0006, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, link_type | link_mask);

	time_ms = 50;
	msleep(time_ms);

	for (i = 0; i < 20; i++) {
		if ((lock_state & MAX96722_LOCK_STATE_LINK_A) == 0)
			if (max96722_get_link_lock_state(max96722, MAX96722_LOCK_STATE_LINK_A)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_A;
				dev_info(dev, "LinkA locked time: %d ms\n", time_ms);
			}

		if ((lock_state & MAX96722_LOCK_STATE_LINK_B) == 0)
			if (max96722_get_link_lock_state(max96722, MAX96722_LOCK_STATE_LINK_B)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_B;
				dev_info(dev, "LinkB locked time: %d ms\n", time_ms);
			}

		if ((lock_state & MAX96722_LOCK_STATE_LINK_C) == 0)
			if (max96722_get_link_lock_state(max96722, MAX96722_LOCK_STATE_LINK_C)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_C;
				dev_info(dev, "LinkC locked time: %d ms\n", time_ms);
			}

		if ((lock_state & MAX96722_LOCK_STATE_LINK_D) == 0)
			if (max96722_get_link_lock_state(max96722, MAX96722_LOCK_STATE_LINK_D)) {
				lock_state |= MAX96722_LOCK_STATE_LINK_D;
				dev_info(dev, "LinkD locked time: %d ms\n", time_ms);
			}

		if ((lock_state & link_mask) == link_mask) {
			dev_info(dev, "All Links are locked: 0x%x, time_ms = %d\n", lock_state, time_ms);
#if 0
			max96722_check_remote_chipid(max96722);
#endif
			return 0;
		}

		msleep(10);
		time_ms += 10;
	}

	if ((lock_state & link_mask) != 0) {
		dev_info(dev, "Partial links are locked: 0x%x, time_ms = %d\n", lock_state, time_ms);
		return 0;
	} else {
		dev_err(dev, "Failed to detect camera link, time_ms = %d!\n", time_ms);
		return -ENODEV;
	}
}

static irqreturn_t max96722_hot_plug_detect_irq_handler(int irq, void *dev_id)
{
	struct max96722 *max96722 = dev_id;
	struct device *dev = &max96722->client->dev;
	u8 lock_state = 0, link_mask = 0;

	link_mask = max96722->link_mask & MAX96722_LOCK_STATE_MASK;
	if (max96722->streaming) {
		lock_state = max96722_get_link_lock_state(max96722, link_mask);
		if (lock_state == link_mask) {
			dev_info(dev, "serializer plug in, lock_state = 0x%02x\n", lock_state);
		} else {
			dev_info(dev, "serializer plug out, lock_state = 0x%02x\n", lock_state);
		}
	}

	return IRQ_HANDLED;
}

static int __maybe_unused max96722_dphy_dpll_predef_set(struct i2c_client *client,
				u32 link_freq_mhz)
{
	int ret = 0;
	u8 dpll_val = 0, dpll_lock = 0;
	u8 mipi_tx_phy_enable = 0;

	ret = max96722_read_reg(client, MAX96722_I2C_ADDR,
			0x08A2, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, &mipi_tx_phy_enable);
	if (ret)
		return ret;
	mipi_tx_phy_enable = (mipi_tx_phy_enable & 0xF0) >> 4;

	dev_info(&client->dev, "DPLL predef set: mipi_tx_phy_enable = 0x%02x, link_freq_mhz = %d\n",
			mipi_tx_phy_enable, link_freq_mhz);

	// dphy max data rate is 2500MHz
	if (link_freq_mhz > (2500 >> 1))
		link_freq_mhz = (2500 >> 1);

	dpll_val = DIV_ROUND_UP(link_freq_mhz * 2, 100) & 0x1F;
	// Disable software override for frequency fine tuning
	dpll_val |= BIT(5);

	// MIPI PHY0
	if (mipi_tx_phy_enable & BIT(0)) {
		// Hold DPLL in reset (config_soft_rst_n = 0) before changing the rate
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x1C00, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT,
			0xf4);
		// Set data rate and enable software override
		ret |= max96722_update_reg_bits(client, MAX96722_I2C_ADDR,
			0x0415, MAX96722_REG_LENGTH_16BIT, 0x3F, dpll_val);
		// Release reset to DPLL (config_soft_rst_n = 1)
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x1C00, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0xf5);
	}

	// MIPI PHY1
	if (mipi_tx_phy_enable & BIT(1)) {
		// Hold DPLL in reset (config_soft_rst_n = 0) before changing the rate
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x1D00, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0xf4);
		// Set data rate and enable software override
		ret |= max96722_update_reg_bits(client, MAX96722_I2C_ADDR,
			0x0418, MAX96722_REG_LENGTH_16BIT, 0x3F, dpll_val);
		// Release reset to DPLL (config_soft_rst_n = 1)
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x1D00, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0xf5);
	}

	// MIPI PHY2
	if (mipi_tx_phy_enable & BIT(2)) {
		// Hold DPLL in reset (config_soft_rst_n = 0) before changing the rate
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x1E00, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0xf4);
		// Set data rate and enable software override
		ret |= max96722_update_reg_bits(client, MAX96722_I2C_ADDR,
			0x041B, MAX96722_REG_LENGTH_16BIT, 0x3F, dpll_val);
		// Release reset to DPLL (config_soft_rst_n = 1)
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x1E00, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0xf5);
	}

	// MIPI PHY3
	if (mipi_tx_phy_enable & BIT(3)) {
		// Hold DPLL in reset (config_soft_rst_n = 0) before changing the rate
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x1F00, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0xf4);
		// Set data rate and enable software override
		ret |= max96722_update_reg_bits(client, MAX96722_I2C_ADDR,
			0x041E, MAX96722_REG_LENGTH_16BIT, 0x3F, dpll_val);
		// Release reset to DPLL (config_soft_rst_n = 1)
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x1F00, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0xf5);
	}

	if (ret) {
		dev_err(&client->dev, "DPLL predef set error!\n");
		return ret;
	}

	ret = read_poll_timeout(max96722_read_reg, ret,
				!(ret < 0) && (dpll_lock & 0xF0),
				1000, 10000, false,
				client, MAX96722_I2C_ADDR,
				0x0400, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, &dpll_lock);
	if (ret < 0) {
		dev_err(&client->dev, "DPLL is not locked, dpll_lock = 0x%02x\n", dpll_lock);
		return ret;
	} else {
		dev_err(&client->dev, "DPLL is locked, dpll_lock = 0x%02x\n", dpll_lock);
		return 0;
	}
}

static int max96722_auto_init_deskew(struct i2c_client *client, u32 deskew_mask)
{
	int ret = 0;

	dev_info(&client->dev, "Auto initial deskew: deskew_mask = 0x%02x\n", deskew_mask);

	// D-PHY Deskew Initial Calibration Control
	if (deskew_mask & BIT(0)) // MIPI PHY0
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
				0x0903, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, 0x80);

	if (deskew_mask & BIT(1)) // MIPI PHY1
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
				0x0943, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, 0x80);

	if (deskew_mask & BIT(2)) // MIPI PHY2
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
				0x0983, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, 0x80);

	if (deskew_mask & BIT(3)) // MIPI PHY3
		ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
				0x09C3, MAX96722_REG_LENGTH_16BIT,
				MAX96722_REG_VALUE_08BIT, 0x80);

	return ret;
}

static int max96722_frame_sync_period(struct i2c_client *client, u32 period)
{
	u32 pclk, fsync_peroid;
	u8 fsync_peroid_h, fsync_peroid_m, fsync_peroid_l;
	int ret = 0;

	if (period == 0)
		return 0;

	dev_info(&client->dev, "Frame sync period = %d\n", period);

#if 1 // TODO: Sensor
	// sendor slave mode enable
#endif

	// Master link Video 0 for frame sync generation
	ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x04A2, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0x00);
	// Disable Vsync-Fsync overlap window
	ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x04AA, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0x00);
	ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x04AB, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0x00);

	// Set FSYNC period to 25M/30 clock cycles. PCLK = 25MHz. Sync freq = 30Hz
	pclk = 25 * 1000 * 1000;
	fsync_peroid = DIV_ROUND_UP(pclk, period) - 1;
	fsync_peroid_l = (fsync_peroid >> 0) & 0xFF;
	fsync_peroid_m = (fsync_peroid >> 8) & 0xFF;
	fsync_peroid_h = (fsync_peroid >> 16) & 0xFF;
	dev_info(&client->dev, "Frame sync period: H = 0x%02x, M = 0x%02x, L = 0x%02x\n",
			fsync_peroid_h, fsync_peroid_m, fsync_peroid_l);
	// FSYNC_PERIOD_H
	ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x04A7, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, fsync_peroid_h);
	// FSYNC_PERIOD_M
	ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x04A6, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, fsync_peroid_m);
	// FSYNC_PERIOD_L
	ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x04A5, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, fsync_peroid_l);

	// FSYNC is GMSL2 type, use osc for fsync, include all links/pipes in fsync gen
	ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x04AF, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0xcf);

#if 1 // TODO: FSYNC GPIO
	// FSYNC_TX_ID: set 4 to match MFP4 on serializer side
	ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x04B1, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0x20);
#endif

#if 1 // TODO: Serializer
	// Enable GPIO_RX_EN on serializer MFP4
	ret |= max96722_write_reg(client, 0x40,
			0x02CA, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0x84);
#endif

	// MFP2, VS not gen internally, GPIO not used to gen fsync, manual mode
	ret |= max96722_write_reg(client, MAX96722_I2C_ADDR,
			0x04A0, MAX96722_REG_LENGTH_16BIT,
			MAX96722_REG_VALUE_08BIT, 0x04);

	return ret;
}

static int max96722_mipi_enable(struct i2c_client *client, bool enable)
{
	int ret = 0;

	if (enable) {
#if MAX96722_FORCE_ALL_CLOCK_EN
		// Force all MIPI clocks running
		ret |= max96722_update_reg_bits(client,
				MAX96722_I2C_ADDR,
				0x08A0, MAX96722_REG_LENGTH_16BIT, BIT(7), BIT(7));
#endif
		// CSI output enabled
		ret |= max96722_update_reg_bits(client,
				MAX96722_I2C_ADDR,
				0x040B, MAX96722_REG_LENGTH_16BIT, BIT(1), BIT(1));
	} else {
#if MAX96722_FORCE_ALL_CLOCK_EN
		// Normal mode
		ret |= max96722_update_reg_bits(client,
				MAX96722_I2C_ADDR,
				0x08A0, MAX96722_REG_LENGTH_16BIT, BIT(7), 0x00);
#endif
		// CSI output disabled
		ret |= max96722_update_reg_bits(client,
				MAX96722_I2C_ADDR,
				0x040B, MAX96722_REG_LENGTH_16BIT, BIT(1), 0x00);
	}

	return ret;
}

static int max96722_get_reso_dist(const struct max96722_mode *mode,
				  struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct max96722_mode *
max96722_find_best_fit(struct max96722 *max96722, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < max96722->cfg_modes_num; i++) {
		dist = max96722_get_reso_dist(&max96722->supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist < cur_best_fit_dist)
				&& (max96722->supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &max96722->supported_modes[cur_best_fit];
}

static int max96722_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);
	const struct max96722_mode *mode;
	u64 pixel_rate = 0;
	u8 data_lanes;

	mutex_lock(&max96722->mutex);

	mode = max96722_find_best_fit(max96722, fmt);

	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&max96722->mutex);
		return -ENOTTY;
#endif
	} else {
		if (max96722->streaming) {
			mutex_unlock(&max96722->mutex);
			return -EBUSY;
		}

		max96722->cur_mode = mode;

		__v4l2_ctrl_s_ctrl(max96722->link_freq, mode->link_freq_idx);
		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		data_lanes = max96722->bus_cfg.bus.mipi_csi2.num_data_lanes;
		pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * data_lanes;
		__v4l2_ctrl_s_ctrl_int64(max96722->pixel_rate, pixel_rate);

		dev_info(&max96722->client->dev, "mipi_freq_idx = %d, mipi_link_freq = %lld\n",
				mode->link_freq_idx, link_freq_items[mode->link_freq_idx]);
		dev_info(&max96722->client->dev, "pixel_rate = %lld, bpp = %d\n",
				pixel_rate, mode->bpp);
	}

	mutex_unlock(&max96722->mutex);

	return 0;
}

static int max96722_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);
	const struct max96722_mode *mode = max96722->cur_mode;

	mutex_lock(&max96722->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&max96722->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && fmt->pad >= PAD0)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&max96722->mutex);

	return 0;
}

static int max96722_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);
	const struct max96722_mode *mode = max96722->cur_mode;

	if (code->index != 0)
		return -EINVAL;
	code->code = mode->bus_fmt;

	return 0;
}

static int max96722_enum_frame_sizes(struct v4l2_subdev *sd,
				     struct v4l2_subdev_pad_config *cfg,
				     struct v4l2_subdev_frame_size_enum *fse)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);

	if (fse->index >= max96722->cfg_modes_num)
		return -EINVAL;

	if (fse->code != max96722->supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = max96722->supported_modes[fse->index].width;
	fse->max_width  = max96722->supported_modes[fse->index].width;
	fse->max_height = max96722->supported_modes[fse->index].height;
	fse->min_height = max96722->supported_modes[fse->index].height;

	return 0;
}

static int max96722_g_frame_interval(struct v4l2_subdev *sd,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);
	const struct max96722_mode *mode = max96722->cur_mode;

	mutex_lock(&max96722->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&max96722->mutex);

	return 0;
}

static void max96722_get_module_inf(struct max96722 *max96722,
				    struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, MAX96722_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, max96722->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, max96722->len_name, sizeof(inf->base.lens));
}

static void
max96722_get_vicap_rst_inf(struct max96722 *max96722,
			   struct rkmodule_vicap_reset_info *rst_info)
{
	struct i2c_client *client = max96722->client;

	rst_info->is_reset = max96722->hot_plug;
	max96722->hot_plug = false;
	rst_info->src = RKCIF_RESET_SRC_ERR_HOTPLUG;
	dev_info(&client->dev, "%s: rst_info->is_reset:%d.\n", __func__,
		 rst_info->is_reset);
}

static void
max96722_set_vicap_rst_inf(struct max96722 *max96722,
			   struct rkmodule_vicap_reset_info rst_info)
{
	max96722->is_reset = rst_info.is_reset;
}

static long max96722_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);
	struct rkmodule_csi_dphy_param *dphy_param;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		max96722_get_module_inf(max96722, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);

		if (stream)
			ret = max96722_mipi_enable(max96722->client, true);
		else
			ret = max96722_mipi_enable(max96722->client, false);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		max96722_get_vicap_rst_inf(
			max96722, (struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		max96722_set_vicap_rst_inf(
			max96722, *(struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		break;
	case RKMODULE_SET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		if (dphy_param->vendor == rk3588_dcphy_param.vendor)
			rk3588_dcphy_param = *dphy_param;
		dev_dbg(&max96722->client->dev, "sensor set dphy param\n");
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		if (dphy_param->vendor == rk3588_dcphy_param.vendor)
			*dphy_param = rk3588_dcphy_param;
		dev_dbg(&max96722->client->dev, "sensor get dphy param\n");
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long max96722_compat_ioctl32(struct v4l2_subdev *sd, unsigned int cmd,
				    unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_vicap_reset_info *vicap_rst_inf;
	struct rkmodule_csi_dphy_param *dphy_param;
	long ret = 0;
	int *seq;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = max96722_ioctl(sd, cmd, inf);
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
			ret = max96722_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = max96722_ioctl(sd, cmd, vicap_rst_inf);
		if (!ret) {
			ret = copy_to_user(up, vicap_rst_inf,
					   sizeof(*vicap_rst_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(vicap_rst_inf, up, sizeof(*vicap_rst_inf));
		if (!ret)
			ret = max96722_ioctl(sd, cmd, vicap_rst_inf);
		else
			ret = -EFAULT;
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		seq = kzalloc(sizeof(*seq), GFP_KERNEL);
		if (!seq) {
			ret = -ENOMEM;
			return ret;
		}

		ret = max96722_ioctl(sd, cmd, seq);
		if (!ret) {
			ret = copy_to_user(up, seq, sizeof(*seq));
			if (ret)
				ret = -EFAULT;
		}
		kfree(seq);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = max96722_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_SET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(dphy_param, up, sizeof(*dphy_param));
		if (!ret)
			ret = max96722_ioctl(sd, cmd, dphy_param);
		else
			ret = -EFAULT;
		kfree(dphy_param);
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}

		ret = max96722_ioctl(sd, cmd, dphy_param);
		if (!ret) {
			ret = copy_to_user(up, dphy_param, sizeof(*dphy_param));
			if (ret)
				ret = -EFAULT;
		}
		kfree(dphy_param);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __max96722_start_stream(struct max96722 *max96722)
{
	int ret;
	u32 link_freq_mhz, link_freq_idx;

	ret = max96722_check_link_lock_state(max96722);
	if (ret)
		return ret;

	if (max96722->hot_plug_irq > 0)
		enable_irq(max96722->hot_plug_irq);

	ret = max96722_write_array(max96722->client,
				   max96722->cur_mode->reg_list);
	if (ret)
		return ret;

	link_freq_idx = max96722->cur_mode->link_freq_idx;
	link_freq_mhz = (u32)div_s64(link_freq_items[link_freq_idx], 1000000L);
	ret = max96722_dphy_dpll_predef_set(max96722->client, link_freq_mhz);
	if (ret)
		return ret;

	if (max96722->auto_init_deskew_mask != 0) {
		ret = max96722_auto_init_deskew(max96722->client,
					max96722->auto_init_deskew_mask);
		if (ret)
			return ret;
	}

	if (max96722->frame_sync_period != 0) {
		ret = max96722_frame_sync_period(max96722->client,
					max96722->frame_sync_period);
		if (ret)
			return ret;
	}

	/* In case these controls are set before streaming */
	mutex_unlock(&max96722->mutex);
	ret = v4l2_ctrl_handler_setup(&max96722->ctrl_handler);
	mutex_lock(&max96722->mutex);
	if (ret)
		return ret;

	return max96722_mipi_enable(max96722->client, true);

}

static int __max96722_stop_stream(struct max96722 *max96722)
{
	if (max96722->hot_plug_irq > 0)
		disable_irq(max96722->hot_plug_irq);

	return max96722_mipi_enable(max96722->client, false);
}

static int max96722_s_stream(struct v4l2_subdev *sd, int on)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);
	struct i2c_client *client = max96722->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
		max96722->cur_mode->width, max96722->cur_mode->height,
		DIV_ROUND_CLOSEST(max96722->cur_mode->max_fps.denominator,
				max96722->cur_mode->max_fps.numerator));

	mutex_lock(&max96722->mutex);
	on = !!on;
	if (on == max96722->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __max96722_start_stream(max96722);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__max96722_stop_stream(max96722);
		pm_runtime_put(&client->dev);
	}

	max96722->streaming = on;

unlock_and_return:
	mutex_unlock(&max96722->mutex);

	return ret;
}

static int max96722_s_power(struct v4l2_subdev *sd, int on)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);
	struct i2c_client *client = max96722->client;
	int ret = 0;

	mutex_lock(&max96722->mutex);

	/* If the power state is not modified - no work to do. */
	if (max96722->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		max96722->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		max96722->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&max96722->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 max96722_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, MAX96722_XVCLK_FREQ / 1000 / 1000);
}

static int __max96722_power_on(struct max96722 *max96722)
{
	int ret;
	u32 delay_us;
	struct device *dev = &max96722->client->dev;

	if (!IS_ERR(max96722->power_gpio)) {
		gpiod_set_value_cansleep(max96722->power_gpio, 1);
		usleep_range(5000, 10000);
	}

	if (!IS_ERR(max96722->pocen_gpio)) {
		gpiod_set_value_cansleep(max96722->pocen_gpio, 1);
		usleep_range(5000, 10000);
	}

	if (!IS_ERR_OR_NULL(max96722->pins_default)) {
		ret = pinctrl_select_state(max96722->pinctrl,
					max96722->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (!IS_ERR(max96722->reset_gpio))
		gpiod_set_value_cansleep(max96722->reset_gpio, 0);

	ret = regulator_bulk_enable(MAX96722_NUM_SUPPLIES, max96722->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}
	if (!IS_ERR(max96722->reset_gpio)) {
		gpiod_set_value_cansleep(max96722->reset_gpio, 1);
		usleep_range(500, 1000);
	}

	if (!IS_ERR(max96722->pwdn_gpio))
		gpiod_set_value_cansleep(max96722->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = max96722_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(max96722->xvclk);

	return ret;
}

static void __max96722_power_off(struct max96722 *max96722)
{
	int ret;
	struct device *dev = &max96722->client->dev;

	if (!IS_ERR(max96722->pwdn_gpio))
		gpiod_set_value_cansleep(max96722->pwdn_gpio, 0);
	clk_disable_unprepare(max96722->xvclk);

	if (!IS_ERR(max96722->reset_gpio))
		gpiod_set_value_cansleep(max96722->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(max96722->pins_sleep)) {
		ret = pinctrl_select_state(max96722->pinctrl,
					   max96722->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}

	regulator_bulk_disable(MAX96722_NUM_SUPPLIES, max96722->supplies);

	if (!IS_ERR(max96722->pocen_gpio))
		gpiod_set_value_cansleep(max96722->pocen_gpio, 0);

	if (!IS_ERR(max96722->power_gpio))
		gpiod_set_value_cansleep(max96722->power_gpio, 0);
}

static int max96722_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);

	return __max96722_power_on(max96722);
}

static int max96722_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);

	__max96722_power_off(max96722);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int max96722_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct max96722_mode *def_mode = &max96722->supported_modes[0];

	mutex_lock(&max96722->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&max96722->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int
max96722_enum_frame_interval(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_frame_interval_enum *fie)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);

	if (fie->index >= max96722->cfg_modes_num)
		return -EINVAL;

	fie->code = max96722->supported_modes[fie->index].bus_fmt;
	fie->width = max96722->supported_modes[fie->index].width;
	fie->height = max96722->supported_modes[fie->index].height;
	fie->interval = max96722->supported_modes[fie->index].max_fps;

	return 0;
}

static int max96722_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				  struct v4l2_mbus_config *config)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);
	u32 val = 0;
	u8 data_lanes = max96722->bus_cfg.bus.mipi_csi2.num_data_lanes;

	val |= V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	val |= (1 << (data_lanes - 1));
	switch (data_lanes) {
	case 4:
		val |= V4L2_MBUS_CSI2_CHANNEL_3;
		fallthrough;
	case 3:
		val |= V4L2_MBUS_CSI2_CHANNEL_2;
		fallthrough;
	case 2:
		val |= V4L2_MBUS_CSI2_CHANNEL_1;
		fallthrough;
	case 1:
	default:
		val |= V4L2_MBUS_CSI2_CHANNEL_0;
		break;
	}

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static int max96722_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_selection *sel)
{
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = max96722->cur_mode->width;
		sel->r.top = 0;
		sel->r.height = max96722->cur_mode->height;
		return 0;
	}

	return -EINVAL;
}

static const struct dev_pm_ops max96722_pm_ops = { SET_RUNTIME_PM_OPS(
	max96722_runtime_suspend, max96722_runtime_resume, NULL) };

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops max96722_internal_ops = {
	.open = max96722_open,
};
#endif

static const struct v4l2_subdev_core_ops max96722_core_ops = {
	.s_power = max96722_s_power,
	.ioctl = max96722_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = max96722_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops max96722_video_ops = {
	.s_stream = max96722_s_stream,
	.g_frame_interval = max96722_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops max96722_pad_ops = {
	.enum_mbus_code = max96722_enum_mbus_code,
	.enum_frame_size = max96722_enum_frame_sizes,
	.enum_frame_interval = max96722_enum_frame_interval,
	.get_fmt = max96722_get_fmt,
	.set_fmt = max96722_set_fmt,
	.get_selection = max96722_get_selection,
	.get_mbus_config = max96722_g_mbus_config,
};

static const struct v4l2_subdev_ops max96722_subdev_ops = {
	.core = &max96722_core_ops,
	.video = &max96722_video_ops,
	.pad = &max96722_pad_ops,
};

static int max96722_initialize_controls(struct max96722 *max96722)
{
	const struct max96722_mode *mode;
	struct v4l2_ctrl_handler *handler;
	u64 pixel_rate;
	u8 data_lanes;
	int ret;

	handler = &max96722->ctrl_handler;

	mode = max96722->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 2);
	if (ret)
		return ret;
	handler->lock = &max96722->mutex;

	max96722->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_items) - 1, 0,
				link_freq_items);
	__v4l2_ctrl_s_ctrl(max96722->link_freq, mode->link_freq_idx);
	dev_info(&max96722->client->dev, "mipi_freq_idx = %d, mipi_link_freq = %lld\n",
			mode->link_freq_idx, link_freq_items[mode->link_freq_idx]);

	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	data_lanes = max96722->bus_cfg.bus.mipi_csi2.num_data_lanes;
	pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * data_lanes;
	max96722->pixel_rate =
		v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE, 0,
				  pixel_rate, 1, pixel_rate);
	dev_info(&max96722->client->dev, "pixel_rate = %lld, bpp = %d\n",
			pixel_rate, mode->bpp);

	if (handler->error) {
		ret = handler->error;
		dev_err(&max96722->client->dev, "Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	max96722->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int max96722_configure_regulators(struct max96722 *max96722)
{
	unsigned int i;

	for (i = 0; i < MAX96722_NUM_SUPPLIES; i++)
		max96722->supplies[i].supply = max96722_supply_names[i];

	return devm_regulator_bulk_get(&max96722->client->dev,
				       MAX96722_NUM_SUPPLIES,
				       max96722->supplies);
}

static int max96722_parse_dt(struct max96722 *max96722)
{
	struct device *dev = &max96722->client->dev;
	struct device_node *node = dev->of_node;
	u8 mipi_data_lanes = max96722->bus_cfg.bus.mipi_csi2.num_data_lanes;
	int ret = 0;

	/* max96722 link mask:
	 *     bit[3:0] = link enable mask: 0 = disable, 1 = enable:
	 *         bit0 - LinkA, bit1 - LinkB, bit2 - LinkC, bit3 - LinkD
	 *     bit[7:4] = link type, 0 = GMSL1, 1 = GMSL2:
	 *         bit4 - LinkA, bit5 - LinkB, bit6 - LinkC, bit7 = LinkD
	 */
	ret = of_property_read_u32(node, "link-mask", &max96722->link_mask);
	if (ret) {
		/* default link mask */
		if (mipi_data_lanes == 4)
			max96722->link_mask = 0xFF; /* Link A/B/C/D: GMSL2 and enable */
		else
			max96722->link_mask = 0x33; /* Link A/B: GMSL2 and enable */
	} else {
		dev_info(dev, "link-mask property: 0x%x\n", max96722->link_mask);
	}
	dev_info(dev, "serdes link mask: 0x%02x\n", max96722->link_mask);

	/* auto initial deskew mask */
	ret = of_property_read_u32(node, "auto-init-deskew-mask",
				&max96722->auto_init_deskew_mask);
	if (ret)
		max96722->auto_init_deskew_mask = 0x0F; // 0x0F: default enable all
	dev_info(dev, "auto init deskew mask: 0x%02x\n", max96722->auto_init_deskew_mask);

	/* FSYNC period config */
	ret = of_property_read_u32(node, "frame-sync-period",
				&max96722->frame_sync_period);
	if (ret)
		max96722->frame_sync_period = 0; // 0: disable (default)
	dev_info(dev, "frame sync period: %d\n", max96722->frame_sync_period);

	return 0;
}

static int max96722_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct max96722 *max96722;
	struct v4l2_subdev *sd;
	struct device_node *endpoint;
	char facing[2];
	u8 mipi_data_lanes;
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x", DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8, DRIVER_VERSION & 0x00ff);

	max96722 = devm_kzalloc(dev, sizeof(*max96722), GFP_KERNEL);
	if (!max96722)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
					&max96722->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
					&max96722->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
					&max96722->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
					&max96722->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	max96722->regmap = devm_regmap_init_i2c(client, &max96722_regmap_config);
	if (IS_ERR(max96722->regmap)) {
		dev_err(dev, "Failed to regmap initialize I2C\n");
		return PTR_ERR(max96722->regmap);
	}

	max96722->client = client;
	i2c_set_clientdata(client, max96722);

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint),
				&max96722->bus_cfg);
	if (ret) {
		dev_err(dev, "Failed to get bus config\n");
		return -EINVAL;
	}
	mipi_data_lanes = max96722->bus_cfg.bus.mipi_csi2.num_data_lanes;
	dev_info(dev, "mipi csi2 phy data lanes %d\n", mipi_data_lanes);

	if (mipi_data_lanes == 4) {
		max96722->supported_modes = supported_modes_4lane;
		max96722->cfg_modes_num = ARRAY_SIZE(supported_modes_4lane);
	} else {
		dev_err(dev, "Not support mipi data lane: %d\n", mipi_data_lanes);
		return -EINVAL;
	}
	max96722->cur_mode = &max96722->supported_modes[0];

	max96722->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(max96722->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	max96722->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(max96722->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	max96722->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(max96722->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	max96722->pocen_gpio = devm_gpiod_get(dev, "pocen", GPIOD_OUT_LOW);
	if (IS_ERR(max96722->pocen_gpio))
		dev_warn(dev, "Failed to get pocen-gpios\n");

	max96722->lock_gpio = devm_gpiod_get(dev, "lock", GPIOD_IN);
	if (IS_ERR(max96722->lock_gpio))
		dev_warn(dev, "Failed to get lock-gpios\n");

	ret = max96722_configure_regulators(max96722);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	max96722->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(max96722->pinctrl)) {
		max96722->pins_default = pinctrl_lookup_state(
			max96722->pinctrl, OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(max96722->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		max96722->pins_sleep = pinctrl_lookup_state(
			max96722->pinctrl, OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(max96722->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	max96722_parse_dt(max96722);

	mutex_init(&max96722->mutex);

	sd = &max96722->subdev;
	v4l2_i2c_subdev_init(sd, client, &max96722_subdev_ops);
	ret = max96722_initialize_controls(max96722);
	if (ret)
		goto err_destroy_mutex;

	ret = __max96722_power_on(max96722);
	if (ret)
		goto err_free_handler;

	ret = max96722_check_link_lock_state(max96722);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &max96722_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	max96722->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &max96722->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(max96722->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	v4l2_set_subdevdata(sd, max96722);

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		max96722->module_index, facing, MAX96722_NAME,
		dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	if (!IS_ERR(max96722->lock_gpio)) {
		max96722->hot_plug_irq = gpiod_to_irq(max96722->lock_gpio);
		if (max96722->hot_plug_irq < 0) {
			dev_err(dev, "failed to get hot plug irq\n");
		} else {
			ret = devm_request_threaded_irq(dev,
					max96722->hot_plug_irq,
					NULL,
					max96722_hot_plug_detect_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"max96722_hot_plug",
					max96722);
			if (ret) {
				dev_err(dev, "failed to request hot plug irq (%d)\n", ret);
				max96722->hot_plug_irq = -1;
			} else {
				disable_irq(max96722->hot_plug_irq);
			}
		}
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
	__max96722_power_off(max96722);
err_free_handler:
	v4l2_ctrl_handler_free(&max96722->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&max96722->mutex);

	return ret;
}

static int max96722_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct max96722 *max96722 = v4l2_get_subdevdata(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&max96722->ctrl_handler);
	mutex_destroy(&max96722->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__max96722_power_off(max96722);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id max96722_of_match[] = {
	{ .compatible = "maxim,max96722" },
	{},
};
MODULE_DEVICE_TABLE(of, max96722_of_match);
#endif

static const struct i2c_device_id max96722_match_id[] = {
	{ "maxim,max96722", 0 },
	{},
};

static struct i2c_driver max96722_i2c_driver = {
	.driver = {
		.name = MAX96722_NAME,
		.pm = &max96722_pm_ops,
		.of_match_table = of_match_ptr(max96722_of_match),
	},
	.probe		= &max96722_probe,
	.remove		= &max96722_remove,
	.id_table	= max96722_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&max96722_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&max96722_i2c_driver);
}

module_init(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Maxim max96722 deserializer driver");
MODULE_LICENSE("GPL");
