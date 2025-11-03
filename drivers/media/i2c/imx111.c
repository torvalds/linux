// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/units.h>

#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>

/* product information registers */
#define IMX111_PRODUCT_ID			CCI_REG16(0x0000)
#define   IMX111_CHIP_ID			0x111
#define IMX111_REVISION				CCI_REG8(0x0002)
#define IMX111_MANUFACTURER_ID			CCI_REG8(0x0003)
#define IMX111_FRAME_COUNTER			CCI_REG8(0x0005)
#define IMX111_PIXEL_ORDER			CCI_REG8(0x0006)

/* general configuration registers */
#define IMX111_STREAMING_MODE			CCI_REG8(0x0100)
#define   IMX111_MODE_STANDBY			0
#define   IMX111_MODE_STREAMING			1
#define IMX111_IMAGE_ORIENTATION		CCI_REG8(0x0101)
#define   IMX111_IMAGE_HFLIP			BIT(0)
#define   IMX111_IMAGE_VFLIP			BIT(1)
#define IMX111_SOFTWARE_RESET			CCI_REG8(0x0103)
#define   IMX111_RESET_ON			1
#define IMX111_GROUP_WRITE			CCI_REG8(0x0104)
#define   IMX111_GROUP_WRITE_ON			1
#define IMX111_FRAME_DROP			CCI_REG8(0x0105)
#define   IMX111_FRAME_DROP_ON			1
#define IMX111_CHANNEL_ID			CCI_REG8(0x0110)
#define IMX111_SIGNALLING_MODE			CCI_REG8(0x0111)
#define IMX111_DATA_DEPTH			CCI_REG16(0x0112)
#define   IMX111_DATA_DEPTH_RAW8		0x08
#define   IMX111_DATA_DEPTH_RAW10		0x0a

/* integration time registers */
#define IMX111_INTEGRATION_TIME			CCI_REG16(0x0202)
#define IMX111_INTEGRATION_TIME_MIN		0x1
#define IMX111_INTEGRATION_TIME_MAX		0xffff
#define IMX111_INTEGRATION_TIME_STEP		1
#define IMX111_INTEGRATION_TIME_OFFSET		5

/* analog gain control */
#define IMX111_REG_ANALOG_GAIN			CCI_REG8(0x0205)
#define IMX111_ANA_GAIN_MIN			0
#define IMX111_ANA_GAIN_MAX			240
#define IMX111_ANA_GAIN_STEP			1
#define IMX111_ANA_GAIN_DEFAULT			0

/* digital gain control */
#define IMX111_REG_DIG_GAIN_GREENR		CCI_REG16(0x020e)
#define IMX111_REG_DIG_GAIN_RED			CCI_REG16(0x0210)
#define IMX111_REG_DIG_GAIN_BLUE		CCI_REG16(0x0212)
#define IMX111_REG_DIG_GAIN_GREENB		CCI_REG16(0x0214)
#define IMX111_DGTL_GAIN_MIN			0x0100
#define IMX111_DGTL_GAIN_MAX			0x0fff
#define IMX111_DGTL_GAIN_DEFAULT		0x0100
#define IMX111_DGTL_GAIN_STEP			1

/* clock configuration registers */
#define IMX111_PIXEL_CLK_DIVIDER_PLL1		CCI_REG8(0x0301)
#define IMX111_SYSTEM_CLK_DIVIDER_PLL1		CCI_REG8(0x0303)
#define IMX111_PRE_PLL_CLK_DIVIDER_PLL1		CCI_REG8(0x0305)
#define IMX111_PLL_MULTIPLIER_PLL1		CCI_REG8(0x0307)
#define IMX111_PLL_SETTLING_TIME		CCI_REG8(0x303c)
#define   IMX111_PLL_SETTLING_TIME_DEFAULT	200
#define IMX111_POST_DIVIDER			CCI_REG8(0x30a4)
#define   IMX111_POST_DIVIDER_DIV1		2
#define   IMX111_POST_DIVIDER_DIV2		0
#define   IMX111_POST_DIVIDER_DIV4		1

/* frame timing registers */
#define IMX111_VERTICAL_TOTAL_LENGTH		CCI_REG16(0x0340)
#define IMX111_VTL_MAX				0x09d8
#define IMX111_VBLANK_MIN			16
#define IMX111_HORIZONTAL_TOTAL_LENGTH		CCI_REG16(0x0342)
#define IMX111_HTL_MAX				0x0dd0
#define IMX111_HBLANK_MIN			16

/* image size registers */
#define IMX111_HORIZONTAL_START			CCI_REG16(0x0344)
#define IMX111_VERTICAL_START			CCI_REG16(0x0346)
#define IMX111_HORIZONTAL_END			CCI_REG16(0x0348)
#define IMX111_VERTICAL_END			CCI_REG16(0x034a)
#define IMX111_IMAGE_WIDTH			CCI_REG16(0x034c)
#define IMX111_IMAGE_HEIGHT			CCI_REG16(0x034e)
#define IMX111_H_EVEN_INC			CCI_REG8(0x0381)
#define IMX111_H_ODD_INC			CCI_REG8(0x0383)
#define IMX111_W_EVEN_INC			CCI_REG8(0x0385)
#define IMX111_W_ODD_INC			CCI_REG8(0x0387)

/* test pattern registers */
#define IMX111_TEST_PATTERN			CCI_REG8(0x0601)
#define   IMX111_TEST_PATTERN_NONE		0
#define   IMX111_TEST_PATTERN_SOLID		1
#define   IMX111_TEST_PATTERN_BARS		2
#define   IMX111_TEST_PATTERN_FADE		3
#define   IMX111_TEST_PATTERN_PN9		4
#define IMX111_SOLID_COLOR_RED			CCI_REG16(0x0602)
#define IMX111_SOLID_COLOR_GR			CCI_REG16(0x0604)
#define IMX111_SOLID_COLOR_BLUE			CCI_REG16(0x0606)
#define IMX111_SOLID_COLOR_GB			CCI_REG16(0x0608)
#define IMX111_TESTP_COLOUR_MIN			0
#define IMX111_TESTP_COLOUR_MAX			0x03ff
#define IMX111_TESTP_COLOUR_STEP		1

#define IMX111_FRAME_RATE_STEP			5

#define IMX111_PIXEL_ARRAY_WIDTH		3280U
#define IMX111_PIXEL_ARRAY_HEIGHT		2464U

enum {
	IMX111_MODE_3280x2464,
	IMX111_MODE_3280x1848,
	IMX111_MODE_3280x1098,
	IMX111_MODE_2100x1200,
	IMX111_MODE_1952x1098,
	IMX111_MODE_1920x1080,
	IMX111_MODE_1640x1232,
	IMX111_MODE_1440x1080,
	IMX111_MODE_1640x924,
	IMX111_MODE_1308x736,
	IMX111_MODE_1280x720,
	IMX111_MODE_820x614,
	IMX111_MODE_640x480,
};

static const struct regulator_bulk_data imx111_supplies[] = {
	{ .supply = "iovdd" },
	{ .supply = "dvdd" },
	{ .supply = "avdd" },
};

struct imx111_mode {
	u32 width;
	u32 height;

	/* Default vertical and horizontal total length */
	u32 vtl_def;
	u32 htl_def;

	struct {
		const struct cci_reg_sequence *regs;
		u32 num_of_regs;
	} reg_list;
};

struct imx111_pll {
	u64 extclk_rate;
	u8 pre_div;
	u8 mult;
};

struct imx111 {
	struct regmap *regmap;

	struct clk *extclk;
	struct gpio_desc *reset;
	struct regulator_bulk_data *supplies;

	struct v4l2_fwnode_endpoint bus_cfg;
	struct v4l2_subdev sd;
	struct media_pad pad;

	/* V4L2 Controls */
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;

	/* Current mode */
	const struct imx111_mode *cur_mode;
	const struct imx111_pll *pll;
	u32 data_depth;

	u64 pixel_clk_raw;
	s64 default_link_freq;
};

static const struct imx111_pll imx111_pll[] = {
	{ .extclk_rate =  6000000, .pre_div = 1, .mult = 113, },
	{ .extclk_rate = 12000000, .pre_div = 2, .mult = 113, },
	{ .extclk_rate = 13500000, .pre_div = 1, .mult = 50, },
	{ .extclk_rate = 18000000, .pre_div = 2, .mult = 75, },
	{ .extclk_rate = 24000000, .pre_div = 4, .mult = 113, },
	{ .extclk_rate = 27000000, .pre_div = 2, .mult = 50, },
	{ .extclk_rate = 36000000, .pre_div = 4, .mult = 75, },
	{ .extclk_rate = 54000000, .pre_div = 4, .mult = 50, },
};

/*
 * This table MUST contain 4 entries per format, to cover the various flip
 * combinations in the order
 * - no flip
 * - h flip
 * - v flip
 * - h&v flips
 */
static const u32 imx111_mbus_formats[] = {
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,

	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
};

static const struct cci_reg_sequence imx111_global_init[] = {
	{ CCI_REG8(0x3080), 0x50 },
	{ CCI_REG8(0x3087), 0x53 },
	{ CCI_REG8(0x309d), 0x94 },
	{ CCI_REG8(0x30b1), 0x03 },
	{ CCI_REG8(0x30c6), 0x00 },
	{ CCI_REG8(0x30c7), 0x00 },
	{ CCI_REG8(0x3115), 0x0b },
	{ CCI_REG8(0x3118), 0x30 },
	{ CCI_REG8(0x311d), 0x25 },
	{ CCI_REG8(0x3121), 0x0a },
	{ CCI_REG8(0x3212), 0xf2 },
	{ CCI_REG8(0x3213), 0x0f },
	{ CCI_REG8(0x3215), 0x0f },
	{ CCI_REG8(0x3217), 0x0b },
	{ CCI_REG8(0x3219), 0x0b },
	{ CCI_REG8(0x321b), 0x0d },
	{ CCI_REG8(0x321d), 0x0d },
	{ CCI_REG8(0x32aa), 0x11 },
	{ CCI_REG8(0x3032), 0x40 },
};

static const struct cci_reg_sequence mode_820x614[] = {
	{ IMX111_GROUP_WRITE, 1 },
	{ IMX111_HORIZONTAL_START, 0x0008 }, { IMX111_VERTICAL_START, 0x0034 },
	{ IMX111_HORIZONTAL_END,   0x0cd7 }, { IMX111_VERTICAL_END,   0x09cb },
	{ IMX111_IMAGE_WIDTH,      0x0334 }, { IMX111_IMAGE_HEIGHT,   0x0266 },
	{ IMX111_GROUP_WRITE, 0 },
	{ IMX111_H_EVEN_INC, 0x05 },	{ IMX111_H_ODD_INC, 0x03 },
	{ IMX111_W_EVEN_INC, 0x05 },	{ IMX111_W_ODD_INC, 0x03 },
	{ CCI_REG8(0x3033), 0x00 },	{ CCI_REG8(0x303d), 0x10 },
	{ CCI_REG8(0x303e), 0x40 },	{ CCI_REG8(0x3040), 0x08 },
	{ CCI_REG8(0x3041), 0x97 },	{ CCI_REG8(0x3048), 0x01 },
	{ CCI_REG8(0x304c), 0x6f },	{ CCI_REG8(0x304d), 0x03 },
	{ CCI_REG8(0x3064), 0x12 },	{ CCI_REG8(0x3073), 0x00 },
	{ CCI_REG8(0x3074), 0x11 },	{ CCI_REG8(0x3075), 0x11 },
	{ CCI_REG8(0x3076), 0x11 },	{ CCI_REG8(0x3077), 0x11 },
	{ CCI_REG8(0x3079), 0x00 },	{ CCI_REG8(0x307a), 0x00 },
	{ CCI_REG8(0x309b), 0x28 },	{ CCI_REG8(0x309c), 0x13 },
	{ CCI_REG8(0x309e), 0x00 },	{ CCI_REG8(0x30a0), 0x14 },
	{ CCI_REG8(0x30a1), 0x09 },	{ CCI_REG8(0x30aa), 0x03 },
	{ CCI_REG8(0x30b2), 0x03 },	{ CCI_REG8(0x30d5), 0x09 },
	{ CCI_REG8(0x30d6), 0x00 },	{ CCI_REG8(0x30d7), 0x00 },
	{ CCI_REG8(0x30d8), 0x00 },	{ CCI_REG8(0x30d9), 0x00 },
	{ CCI_REG8(0x30de), 0x04 },	{ CCI_REG8(0x30df), 0x20 },
	{ CCI_REG8(0x3102), 0x08 },	{ CCI_REG8(0x3103), 0x22 },
	{ CCI_REG8(0x3104), 0x20 },	{ CCI_REG8(0x3105), 0x00 },
	{ CCI_REG8(0x3106), 0x87 },	{ CCI_REG8(0x3107), 0x00 },
	{ CCI_REG8(0x3108), 0x03 },	{ CCI_REG8(0x3109), 0x02 },
	{ CCI_REG8(0x310a), 0x03 },	{ CCI_REG8(0x315c), 0x9c },
	{ CCI_REG8(0x315d), 0x9b },	{ CCI_REG8(0x316e), 0x9d },
	{ CCI_REG8(0x316f), 0x9c },	{ CCI_REG8(0x3318), 0x7a },
	{ CCI_REG8(0x3348), 0xe0 },
};

static const struct cci_reg_sequence mode_1308x736[] = {
	{ IMX111_GROUP_WRITE, 1 },
	{ IMX111_HORIZONTAL_START, 0x0154 }, { IMX111_VERTICAL_START, 0x0220 },
	{ IMX111_HORIZONTAL_END,   0x0b8b }, { IMX111_VERTICAL_END,   0x07df },
	{ IMX111_IMAGE_WIDTH,      0x051c }, { IMX111_IMAGE_HEIGHT,   0x02e0 },
	{ IMX111_GROUP_WRITE, 0 },
	{ IMX111_H_EVEN_INC, 0x01 },	{ IMX111_H_ODD_INC, 0x01 },
	{ IMX111_W_EVEN_INC, 0x01 },	{ IMX111_W_ODD_INC, 0x03 },
	{ CCI_REG8(0x3033), 0x84 },	{ CCI_REG8(0x303d), 0x10 },
	{ CCI_REG8(0x303e), 0x40 },	{ CCI_REG8(0x3040), 0x08 },
	{ CCI_REG8(0x3041), 0x97 },	{ CCI_REG8(0x3048), 0x01 },
	{ CCI_REG8(0x304c), 0xd7 },	{ CCI_REG8(0x304d), 0x01 },
	{ CCI_REG8(0x3064), 0x12 },	{ CCI_REG8(0x3073), 0x00 },
	{ CCI_REG8(0x3074), 0x11 },	{ CCI_REG8(0x3075), 0x11 },
	{ CCI_REG8(0x3076), 0x11 },	{ CCI_REG8(0x3077), 0x11 },
	{ CCI_REG8(0x3079), 0x00 },	{ CCI_REG8(0x307a), 0x00 },
	{ CCI_REG8(0x309b), 0x48 },	{ CCI_REG8(0x309c), 0x12 },
	{ CCI_REG8(0x309e), 0x04 },	{ CCI_REG8(0x30a0), 0x14 },
	{ CCI_REG8(0x30a1), 0x0a },	{ CCI_REG8(0x30aa), 0x01 },
	{ CCI_REG8(0x30b2), 0x05 },	{ CCI_REG8(0x30d5), 0x04 },
	{ CCI_REG8(0x30d6), 0x85 },	{ CCI_REG8(0x30d7), 0x2a },
	{ CCI_REG8(0x30d8), 0x64 },	{ CCI_REG8(0x30d9), 0x89 },
	{ CCI_REG8(0x30de), 0x00 },	{ CCI_REG8(0x30df), 0x20 },
	{ CCI_REG8(0x3102), 0x08 },	{ CCI_REG8(0x3103), 0x22 },
	{ CCI_REG8(0x3104), 0x20 },	{ CCI_REG8(0x3105), 0x00 },
	{ CCI_REG8(0x3106), 0x87 },	{ CCI_REG8(0x3107), 0x00 },
	{ CCI_REG8(0x3108), 0x03 },	{ CCI_REG8(0x3109), 0x02 },
	{ CCI_REG8(0x310a), 0x03 },	{ CCI_REG8(0x315c), 0x42 },
	{ CCI_REG8(0x315d), 0x41 },	{ CCI_REG8(0x316e), 0x43 },
	{ CCI_REG8(0x316f), 0x42 },	{ CCI_REG8(0x3318), 0x62 },
	{ CCI_REG8(0x3348), 0xe0 },
};

static const struct cci_reg_sequence mode_1640x924[] = {
	{ IMX111_GROUP_WRITE, 1 },
	{ IMX111_HORIZONTAL_START, 0x0008 }, { IMX111_VERTICAL_START, 0x0164 },
	{ IMX111_HORIZONTAL_END,   0x0cd7 }, { IMX111_VERTICAL_END,   0x089b },
	{ IMX111_IMAGE_WIDTH,      0x0668 }, { IMX111_IMAGE_HEIGHT,   0x039c },
	{ IMX111_GROUP_WRITE, 0 },
	{ IMX111_H_EVEN_INC, 0x01 },	{ IMX111_H_ODD_INC, 0x03 },
	{ IMX111_W_EVEN_INC, 0x01 },	{ IMX111_W_ODD_INC, 0x03 },
	{ CCI_REG8(0x3033), 0x00 },	{ CCI_REG8(0x303d), 0x10 },
	{ CCI_REG8(0x303e), 0x40 },	{ CCI_REG8(0x3040), 0x08 },
	{ CCI_REG8(0x3041), 0x97 },	{ CCI_REG8(0x3048), 0x01 },
	{ CCI_REG8(0x304c), 0x6f },	{ CCI_REG8(0x304d), 0x03 },
	{ CCI_REG8(0x3064), 0x12 },	{ CCI_REG8(0x3073), 0x00 },
	{ CCI_REG8(0x3074), 0x11 },	{ CCI_REG8(0x3075), 0x11 },
	{ CCI_REG8(0x3076), 0x11 },	{ CCI_REG8(0x3077), 0x11 },
	{ CCI_REG8(0x3079), 0x00 },	{ CCI_REG8(0x307a), 0x00 },
	{ CCI_REG8(0x309b), 0x28 },	{ CCI_REG8(0x309c), 0x13 },
	{ CCI_REG8(0x309e), 0x00 },	{ CCI_REG8(0x30a0), 0x14 },
	{ CCI_REG8(0x30a1), 0x09 },	{ CCI_REG8(0x30aa), 0x03 },
	{ CCI_REG8(0x30b2), 0x05 },	{ CCI_REG8(0x30d5), 0x09 },
	{ CCI_REG8(0x30d6), 0x01 },	{ CCI_REG8(0x30d7), 0x01 },
	{ CCI_REG8(0x30d8), 0x64 },	{ CCI_REG8(0x30d9), 0x89 },
	{ CCI_REG8(0x30de), 0x02 },	{ CCI_REG8(0x30df), 0x20 },
	{ CCI_REG8(0x3102), 0x08 },	{ CCI_REG8(0x3103), 0x22 },
	{ CCI_REG8(0x3104), 0x20 },	{ CCI_REG8(0x3105), 0x00 },
	{ CCI_REG8(0x3106), 0x87 },	{ CCI_REG8(0x3107), 0x00 },
	{ CCI_REG8(0x3108), 0x03 },	{ CCI_REG8(0x3109), 0x02 },
	{ CCI_REG8(0x310a), 0x03 },	{ CCI_REG8(0x315c), 0x9c },
	{ CCI_REG8(0x315d), 0x9b },	{ CCI_REG8(0x316e), 0x9d },
	{ CCI_REG8(0x316f), 0x9c },	{ CCI_REG8(0x3318), 0x72 },
	{ CCI_REG8(0x3348), 0xe0 },
};

static const struct cci_reg_sequence mode_1640x1232[] = {
	{ IMX111_GROUP_WRITE, 1 },
	{ IMX111_HORIZONTAL_START, 0x0008 }, { IMX111_VERTICAL_START, 0x0030 },
	{ IMX111_HORIZONTAL_END,   0x0cd7 }, { IMX111_VERTICAL_END,   0x09cf },
	{ IMX111_IMAGE_WIDTH,      0x0668 }, { IMX111_IMAGE_HEIGHT,   0x04d0 },
	{ IMX111_GROUP_WRITE, 0 },
	{ IMX111_H_EVEN_INC, 0x01 },	{ IMX111_H_ODD_INC, 0x03 },
	{ IMX111_W_EVEN_INC, 0x01 },	{ IMX111_W_ODD_INC, 0x03 },
	{ CCI_REG8(0x3033), 0x00 },	{ CCI_REG8(0x303d), 0x10 },
	{ CCI_REG8(0x303e), 0x40 },	{ CCI_REG8(0x3040), 0x08 },
	{ CCI_REG8(0x3041), 0x97 },	{ CCI_REG8(0x3048), 0x01 },
	{ CCI_REG8(0x304c), 0x6f },	{ CCI_REG8(0x304d), 0x03 },
	{ CCI_REG8(0x3064), 0x12 },	{ CCI_REG8(0x3073), 0x00 },
	{ CCI_REG8(0x3074), 0x11 },	{ CCI_REG8(0x3075), 0x11 },
	{ CCI_REG8(0x3076), 0x11 },	{ CCI_REG8(0x3077), 0x11 },
	{ CCI_REG8(0x3079), 0x00 },	{ CCI_REG8(0x307a), 0x00 },
	{ CCI_REG8(0x309b), 0x28 },	{ CCI_REG8(0x309c), 0x13 },
	{ CCI_REG8(0x309e), 0x00 },	{ CCI_REG8(0x30a0), 0x14 },
	{ CCI_REG8(0x30a1), 0x09 },	{ CCI_REG8(0x30aa), 0x03 },
	{ CCI_REG8(0x30b2), 0x05 },	{ CCI_REG8(0x30d5), 0x09 },
	{ CCI_REG8(0x30d6), 0x01 },	{ CCI_REG8(0x30d7), 0x01 },
	{ CCI_REG8(0x30d8), 0x64 },	{ CCI_REG8(0x30d9), 0x89 },
	{ CCI_REG8(0x30de), 0x02 },	{ CCI_REG8(0x30df), 0x20 },
	{ CCI_REG8(0x3102), 0x08 },	{ CCI_REG8(0x3103), 0x22 },
	{ CCI_REG8(0x3104), 0x20 },	{ CCI_REG8(0x3105), 0x00 },
	{ CCI_REG8(0x3106), 0x87 },	{ CCI_REG8(0x3107), 0x00 },
	{ CCI_REG8(0x3108), 0x03 },	{ CCI_REG8(0x3109), 0x02 },
	{ CCI_REG8(0x310a), 0x03 },	{ CCI_REG8(0x315c), 0x9c },
	{ CCI_REG8(0x315d), 0x9b },	{ CCI_REG8(0x316e), 0x9d },
	{ CCI_REG8(0x316f), 0x9c },	{ CCI_REG8(0x3318), 0x72 },
	{ CCI_REG8(0x3348), 0xe0 },
};

static const struct cci_reg_sequence mode_1952x1098[] = {
	{ IMX111_GROUP_WRITE, 1 },
	{ IMX111_HORIZONTAL_START, 0x0016 }, { IMX111_VERTICAL_START, 0x016e },
	{ IMX111_HORIZONTAL_END,   0x0ccb }, { IMX111_VERTICAL_END,   0x0893 },
	{ IMX111_IMAGE_WIDTH,      0x07a0 }, { IMX111_IMAGE_HEIGHT,   0x044a },
	{ IMX111_GROUP_WRITE, 0 },
	{ IMX111_H_EVEN_INC, 0x01 },	{ IMX111_H_ODD_INC, 0x01 },
	{ IMX111_W_EVEN_INC, 0x01 },	{ IMX111_W_ODD_INC, 0x01 },
	{ CCI_REG8(0x3033), 0x00 },	{ CCI_REG8(0x303d), 0x10 },
	{ CCI_REG8(0x303e), 0x00 },	{ CCI_REG8(0x3040), 0x08 },
	{ CCI_REG8(0x3041), 0x91 },	{ CCI_REG8(0x3048), 0x00 },
	{ CCI_REG8(0x304c), 0x67 },	{ CCI_REG8(0x304d), 0x03 },
	{ CCI_REG8(0x3064), 0x10 },	{ CCI_REG8(0x3073), 0xa0 },
	{ CCI_REG8(0x3074), 0x12 },	{ CCI_REG8(0x3075), 0x12 },
	{ CCI_REG8(0x3076), 0x12 },	{ CCI_REG8(0x3077), 0x11 },
	{ CCI_REG8(0x3079), 0x0a },	{ CCI_REG8(0x307a), 0x0a },
	{ CCI_REG8(0x309b), 0x60 },	{ CCI_REG8(0x309e), 0x04 },
	{ CCI_REG8(0x30a0), 0x15 },	{ CCI_REG8(0x30a1), 0x08 },
	{ CCI_REG8(0x30aa), 0x03 },	{ CCI_REG8(0x30b2), 0x05 },
	{ CCI_REG8(0x30d5), 0x20 },	{ CCI_REG8(0x30d6), 0x85 },
	{ CCI_REG8(0x30d7), 0x2a },	{ CCI_REG8(0x30d8), 0x64 },
	{ CCI_REG8(0x30d9), 0x89 },	{ CCI_REG8(0x30de), 0x00 },
	{ CCI_REG8(0x30df), 0x21 },	{ CCI_REG8(0x3102), 0x08 },
	{ CCI_REG8(0x3103), 0x1d },	{ CCI_REG8(0x3104), 0x1e },
	{ CCI_REG8(0x3105), 0x00 },	{ CCI_REG8(0x3106), 0x74 },
	{ CCI_REG8(0x3107), 0x00 },	{ CCI_REG8(0x3108), 0x03 },
	{ CCI_REG8(0x3109), 0x02 },	{ CCI_REG8(0x310a), 0x03 },
	{ CCI_REG8(0x315c), 0x37 },	{ CCI_REG8(0x315d), 0x36 },
	{ CCI_REG8(0x316e), 0x38 },	{ CCI_REG8(0x316f), 0x37 },
	{ CCI_REG8(0x3318), 0x63 },	{ CCI_REG8(0x3348), 0xA0 },
};

static const struct cci_reg_sequence mode_2100x1200[] = {
	{ IMX111_GROUP_WRITE, 1 },
	{ IMX111_HORIZONTAL_START, 0x0256 }, { IMX111_VERTICAL_START, 0x02a8 },
	{ IMX111_HORIZONTAL_END,   0x0a89 }, { IMX111_VERTICAL_END,   0x0757 },
	{ IMX111_IMAGE_WIDTH,      0x0834 }, { IMX111_IMAGE_HEIGHT,   0x04b0 },
	{ IMX111_GROUP_WRITE, 0 },
	{ IMX111_H_EVEN_INC, 0x01 },	{ IMX111_H_ODD_INC, 0x01 },
	{ IMX111_W_EVEN_INC, 0x01 },	{ IMX111_W_ODD_INC, 0x01 },
	{ CCI_REG8(0x3033), 0x00 },	{ CCI_REG8(0x303d), 0x10 },
	{ CCI_REG8(0x303e), 0x40 },	{ CCI_REG8(0x3040), 0x08 },
	{ CCI_REG8(0x3041), 0x97 },	{ CCI_REG8(0x3048), 0x00 },
	{ CCI_REG8(0x304c), 0x6f },	{ CCI_REG8(0x304d), 0x03 },
	{ CCI_REG8(0x3064), 0x12 },	{ CCI_REG8(0x3073), 0x00 },
	{ CCI_REG8(0x3074), 0x11 },	{ CCI_REG8(0x3075), 0x11 },
	{ CCI_REG8(0x3076), 0x11 },	{ CCI_REG8(0x3077), 0x11 },
	{ CCI_REG8(0x3079), 0x00 },	{ CCI_REG8(0x307a), 0x00 },
	{ CCI_REG8(0x309b), 0x20 },	{ CCI_REG8(0x309c), 0x13 },
	{ CCI_REG8(0x309e), 0x00 },	{ CCI_REG8(0x30a0), 0x14 },
	{ CCI_REG8(0x30a1), 0x08 },	{ CCI_REG8(0x30aa), 0x03 },
	{ CCI_REG8(0x30b2), 0x07 },	{ CCI_REG8(0x30d5), 0x00 },
	{ CCI_REG8(0x30d6), 0x85 },	{ CCI_REG8(0x30d7), 0x2a },
	{ CCI_REG8(0x30d8), 0x64 },	{ CCI_REG8(0x30d9), 0x89 },
	{ CCI_REG8(0x30de), 0x00 },	{ CCI_REG8(0x30df), 0x20 },
	{ CCI_REG8(0x3102), 0x08 },	{ CCI_REG8(0x3103), 0x22 },
	{ CCI_REG8(0x3104), 0x20 },	{ CCI_REG8(0x3105), 0x00 },
	{ CCI_REG8(0x3106), 0x87 },	{ CCI_REG8(0x3107), 0x00 },
	{ CCI_REG8(0x3108), 0x03 },	{ CCI_REG8(0x3109), 0x02 },
	{ CCI_REG8(0x310a), 0x03 },	{ CCI_REG8(0x315c), 0x9c },
	{ CCI_REG8(0x315d), 0x9b },	{ CCI_REG8(0x316e), 0x9d },
	{ CCI_REG8(0x316f), 0x9c },	{ CCI_REG8(0x3318), 0x62 },
	{ CCI_REG8(0x3348), 0xe0 },
};

static const struct cci_reg_sequence mode_3280x1098[] = {
	{ IMX111_GROUP_WRITE, 1 },
	{ IMX111_HORIZONTAL_START, 0x0008 }, { IMX111_VERTICAL_START, 0x01f6 },
	{ IMX111_HORIZONTAL_END,   0x0cd7 }, { IMX111_VERTICAL_END,   0x080b },
	{ IMX111_IMAGE_WIDTH,      0x0cd0 }, { IMX111_IMAGE_HEIGHT,   0x044a },
	{ IMX111_GROUP_WRITE, 0 },
	{ IMX111_H_EVEN_INC, 0x01 },	{ IMX111_H_ODD_INC, 0x01 },
	{ IMX111_W_EVEN_INC, 0x01 },	{ IMX111_W_ODD_INC, 0x01 },
	{ CCI_REG8(0x3033), 0x00 },	{ CCI_REG8(0x303d), 0x10 },
	{ CCI_REG8(0x303e), 0x40 },	{ CCI_REG8(0x3040), 0x08 },
	{ CCI_REG8(0x3041), 0x93 },	{ CCI_REG8(0x3048), 0x00 },
	{ CCI_REG8(0x304c), 0x67 },	{ CCI_REG8(0x304d), 0x03 },
	{ CCI_REG8(0x3064), 0x12 },	{ CCI_REG8(0x3073), 0xe0 },
	{ CCI_REG8(0x3074), 0x12 },	{ CCI_REG8(0x3075), 0x12 },
	{ CCI_REG8(0x3076), 0x12 },	{ CCI_REG8(0x3077), 0x12 },
	{ CCI_REG8(0x3079), 0x2a },	{ CCI_REG8(0x307a), 0x0a },
	{ CCI_REG8(0x309b), 0x60 },	{ CCI_REG8(0x309e), 0x04 },
	{ CCI_REG8(0x30a0), 0x15 },	{ CCI_REG8(0x30a1), 0x08 },
	{ CCI_REG8(0x30aa), 0x03 },	{ CCI_REG8(0x30b2), 0x05 },
	{ CCI_REG8(0x30d5), 0x00 },	{ CCI_REG8(0x30d6), 0x85 },
	{ CCI_REG8(0x30d7), 0x2a },	{ CCI_REG8(0x30d8), 0x64 },
	{ CCI_REG8(0x30d9), 0x89 },	{ CCI_REG8(0x30de), 0x00 },
	{ CCI_REG8(0x30df), 0x20 },	{ CCI_REG8(0x3102), 0x08 },
	{ CCI_REG8(0x3103), 0x1d },	{ CCI_REG8(0x3104), 0x1e },
	{ CCI_REG8(0x3105), 0x00 },	{ CCI_REG8(0x3106), 0x74 },
	{ CCI_REG8(0x3107), 0x00 },	{ CCI_REG8(0x3108), 0x03 },
	{ CCI_REG8(0x3109), 0x02 },	{ CCI_REG8(0x310a), 0x03 },
	{ CCI_REG8(0x315c), 0x37 },	{ CCI_REG8(0x315d), 0x36 },
	{ CCI_REG8(0x316e), 0x38 },	{ CCI_REG8(0x316f), 0x37 },
	{ CCI_REG8(0x3318), 0x63 },	{ CCI_REG8(0x3348), 0xe0 },
};

static const struct cci_reg_sequence mode_3280x1848[] = {
	{ IMX111_GROUP_WRITE, 1 },
	{ IMX111_HORIZONTAL_START, 0x0008 }, { IMX111_VERTICAL_START, 0x0164 },
	{ IMX111_HORIZONTAL_END,   0x0cd7 }, { IMX111_VERTICAL_END,   0x089b },
	{ IMX111_IMAGE_WIDTH,      0x0cd0 }, { IMX111_IMAGE_HEIGHT,   0x0738 },
	{ IMX111_GROUP_WRITE, 0 },
	{ IMX111_H_EVEN_INC, 0x01 },	{ IMX111_H_ODD_INC, 0x01 },
	{ IMX111_W_EVEN_INC, 0x01 },	{ IMX111_W_ODD_INC, 0x01 },
	{ CCI_REG8(0x3033), 0x00 },	{ CCI_REG8(0x303d), 0x00 },
	{ CCI_REG8(0x303e), 0x41 },	{ CCI_REG8(0x3040), 0x08 },
	{ CCI_REG8(0x3041), 0x97 },	{ CCI_REG8(0x3048), 0x00 },
	{ CCI_REG8(0x304c), 0x6f },	{ CCI_REG8(0x304d), 0x03 },
	{ CCI_REG8(0x3064), 0x12 },	{ CCI_REG8(0x3073), 0x00 },
	{ CCI_REG8(0x3074), 0x11 },	{ CCI_REG8(0x3075), 0x11 },
	{ CCI_REG8(0x3076), 0x11 },	{ CCI_REG8(0x3077), 0x11 },
	{ CCI_REG8(0x3079), 0x00 },	{ CCI_REG8(0x307a), 0x00 },
	{ CCI_REG8(0x309b), 0x20 },	{ CCI_REG8(0x309c), 0x13 },
	{ CCI_REG8(0x309e), 0x00 },	{ CCI_REG8(0x30a0), 0x14 },
	{ CCI_REG8(0x30a1), 0x08 },	{ CCI_REG8(0x30aa), 0x03 },
	{ CCI_REG8(0x30b2), 0x07 },	{ CCI_REG8(0x30d5), 0x00 },
	{ CCI_REG8(0x30d6), 0x85 },	{ CCI_REG8(0x30d7), 0x2a },
	{ CCI_REG8(0x30d8), 0x64 },	{ CCI_REG8(0x30d9), 0x89 },
	{ CCI_REG8(0x30de), 0x00 },	{ CCI_REG8(0x30df), 0x20 },
	{ CCI_REG8(0x3102), 0x10 },	{ CCI_REG8(0x3103), 0x44 },
	{ CCI_REG8(0x3104), 0x40 },	{ CCI_REG8(0x3105), 0x00 },
	{ CCI_REG8(0x3106), 0x0d },	{ CCI_REG8(0x3107), 0x01 },
	{ CCI_REG8(0x3108), 0x09 },	{ CCI_REG8(0x3109), 0x08 },
	{ CCI_REG8(0x310a), 0x0f },	{ CCI_REG8(0x315c), 0x5d },
	{ CCI_REG8(0x315d), 0x5c },	{ CCI_REG8(0x316e), 0x5e },
	{ CCI_REG8(0x316f), 0x5d },	{ CCI_REG8(0x3318), 0x60 },
	{ CCI_REG8(0x3348), 0xe0 },
};

static const struct cci_reg_sequence mode_3280x2464[] = {
	{ IMX111_GROUP_WRITE, 1 },
	{ IMX111_HORIZONTAL_START, 0x0008 }, { IMX111_VERTICAL_START, 0x0030 },
	{ IMX111_HORIZONTAL_END,   0x0cd7 }, { IMX111_VERTICAL_END,   0x09cf },
	{ IMX111_IMAGE_WIDTH,      0x0cd0 }, { IMX111_IMAGE_HEIGHT,   0x09a0 },
	{ IMX111_GROUP_WRITE, 0 },
	{ IMX111_H_EVEN_INC, 0x01 },	{ IMX111_H_ODD_INC, 0x01 },
	{ IMX111_W_EVEN_INC, 0x01 },	{ IMX111_W_ODD_INC, 0x01 },
	{ CCI_REG8(0x3033), 0x00 },	{ CCI_REG8(0x303d), 0x00 },
	{ CCI_REG8(0x303e), 0x41 },	{ CCI_REG8(0x3040), 0x08 },
	{ CCI_REG8(0x3041), 0x97 },	{ CCI_REG8(0x3048), 0x00 },
	{ CCI_REG8(0x304c), 0x6f },	{ CCI_REG8(0x304d), 0x03 },
	{ CCI_REG8(0x3064), 0x12 },	{ CCI_REG8(0x3073), 0x00 },
	{ CCI_REG8(0x3074), 0x11 },	{ CCI_REG8(0x3075), 0x11 },
	{ CCI_REG8(0x3076), 0x11 },	{ CCI_REG8(0x3077), 0x11 },
	{ CCI_REG8(0x3079), 0x00 },	{ CCI_REG8(0x307a), 0x00 },
	{ CCI_REG8(0x309b), 0x20 },	{ CCI_REG8(0x309c), 0x13 },
	{ CCI_REG8(0x309e), 0x00 },	{ CCI_REG8(0x30a0), 0x14 },
	{ CCI_REG8(0x30a1), 0x08 },	{ CCI_REG8(0x30aa), 0x03 },
	{ CCI_REG8(0x30b2), 0x07 },	{ CCI_REG8(0x30d5), 0x00 },
	{ CCI_REG8(0x30d6), 0x85 },	{ CCI_REG8(0x30d7), 0x2a },
	{ CCI_REG8(0x30d8), 0x64 },	{ CCI_REG8(0x30d9), 0x89 },
	{ CCI_REG8(0x30de), 0x00 },	{ CCI_REG8(0x30df), 0x20 },
	{ CCI_REG8(0x3102), 0x10 },	{ CCI_REG8(0x3103), 0x44 },
	{ CCI_REG8(0x3104), 0x40 },	{ CCI_REG8(0x3105), 0x00 },
	{ CCI_REG8(0x3106), 0x0d },	{ CCI_REG8(0x3107), 0x01 },
	{ CCI_REG8(0x3108), 0x09 },	{ CCI_REG8(0x3109), 0x08 },
	{ CCI_REG8(0x310a), 0x0f },	{ CCI_REG8(0x315c), 0x5d },
	{ CCI_REG8(0x315d), 0x5c },	{ CCI_REG8(0x316e), 0x5e },
	{ CCI_REG8(0x316f), 0x5d },	{ CCI_REG8(0x3318), 0x60 },
	{ CCI_REG8(0x3348), 0xe0 },
};

static const struct imx111_mode imx111_modes[] = {
	[IMX111_MODE_3280x2464] = {
		.width = 3280,
		.height = 2464,
		.vtl_def = 2490,
		.htl_def = 3536,
		.reg_list = {
			.regs = mode_3280x2464,
			.num_of_regs = ARRAY_SIZE(mode_3280x2464),
		},
	},
	[IMX111_MODE_3280x1848] = {
		.width = 3280,
		.height = 1848,
		.vtl_def = 1874,
		.htl_def = 3536,
		.reg_list = {
			.regs = mode_3280x1848,
			.num_of_regs = ARRAY_SIZE(mode_3280x1848),
		},
	},
	[IMX111_MODE_3280x1098] = {
		.width = 3280,
		.height = 1098,
		.vtl_def = 1130,
		.htl_def = 3500,
		.reg_list = {
			.regs = mode_3280x1098,
			.num_of_regs = ARRAY_SIZE(mode_3280x1098),
		},
	},
	[IMX111_MODE_2100x1200] = {
		.width = 2100,
		.height = 1200,
		.vtl_def = 1260,
		.htl_def = 3536,
		.reg_list = {
			.regs = mode_2100x1200,
			.num_of_regs = ARRAY_SIZE(mode_2100x1200),
		},
	},
	[IMX111_MODE_1952x1098] = {
		.width = 1952,
		.height = 1098,
		.vtl_def = 1884,
		.htl_def = 3500,
		.reg_list = {
			.regs = mode_1952x1098,
			.num_of_regs = ARRAY_SIZE(mode_1952x1098),
		},
	},
	[IMX111_MODE_1920x1080] = {
		.width = 1920,
		.height = 1080,
		.vtl_def = 1884,
		.htl_def = 3500,
		.reg_list = {
			.regs = mode_1952x1098,
			.num_of_regs = ARRAY_SIZE(mode_1952x1098),
		},
	},
	[IMX111_MODE_1640x1232] = {
		.width = 1640,
		.height = 1232,
		.vtl_def = 1254,
		.htl_def = 3536,
		.reg_list = {
			.regs = mode_1640x1232,
			.num_of_regs = ARRAY_SIZE(mode_1640x1232),
		},
	},
	[IMX111_MODE_1440x1080] = {
		.width = 1440,
		.height = 1080,
		.vtl_def = 1254,
		.htl_def = 3536,
		.reg_list = {
			.regs = mode_1640x1232,
			.num_of_regs = ARRAY_SIZE(mode_1640x1232),
		},
	},
	[IMX111_MODE_1640x924] = {
		.width = 1640,
		.height = 924,
		.vtl_def = 946,
		.htl_def = 3536,
		.reg_list = {
			.regs = mode_1640x924,
			.num_of_regs = ARRAY_SIZE(mode_1640x924),
		},
	},
	[IMX111_MODE_1308x736] = {
		.width = 1308,
		.height = 736,
		.vtl_def = 2369,
		.htl_def = 1896,
		.reg_list = {
			.regs = mode_1308x736,
			.num_of_regs = ARRAY_SIZE(mode_1308x736),
		},
	},
	[IMX111_MODE_1280x720] = {
		.width = 1280,
		.height = 720,
		.vtl_def = 2369,
		.htl_def = 1896,
		.reg_list = {
			.regs = mode_1308x736,
			.num_of_regs = ARRAY_SIZE(mode_1308x736),
		},
	},
	[IMX111_MODE_820x614] = {
		.width = 820,
		.height = 614,
		.vtl_def = 1260,
		.htl_def = 3536,
		.reg_list = {
			.regs = mode_820x614,
			.num_of_regs = ARRAY_SIZE(mode_820x614),
		},
	},
	[IMX111_MODE_640x480] = {
		.width = 640,
		.height = 480,
		.vtl_def = 1260,
		.htl_def = 3536,
		.reg_list = {
			.regs = mode_820x614,
			.num_of_regs = ARRAY_SIZE(mode_820x614),
		},
	},
};

static inline struct imx111 *sd_to_imx111(struct v4l2_subdev *sd)
{
	return container_of_const(sd, struct imx111, sd);
}

static inline struct imx111 *ctrl_to_imx111(struct v4l2_ctrl *ctrl)
{
	return container_of_const(ctrl->handler, struct imx111, hdl);
}

static u8 to_settle_delay(u64 extclk_rate)
{
	u64 extclk_mhz = div_u64(extclk_rate, MEGA);

	return DIV_ROUND_UP(IMX111_PLL_SETTLING_TIME_DEFAULT * extclk_mhz - 63,
			    64);
}

static u32 imx111_get_format_code(struct imx111 *sensor, u32 code, bool test)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(imx111_mbus_formats); i++)
		if (imx111_mbus_formats[i] == code)
			break;

	if (i >= ARRAY_SIZE(imx111_mbus_formats))
		i = 0;

	if (test)
		return imx111_mbus_formats[i];

	i = (i & ~3) | (sensor->vflip->val ? 2 : 0) |
	    (sensor->hflip->val ? 1 : 0);

	return imx111_mbus_formats[i];
}

static u32 imx111_get_format_bpp(const struct v4l2_mbus_framefmt *format)
{
	switch (format->code) {
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		return 8;

	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	default:
		return 10;
	}
}

static int imx111_update_digital_gain(struct imx111 *sensor, u32 val)
{
	int ret = 0;

	cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
			IMX111_GROUP_WRITE_ON,
			IMX111_GROUP_WRITE_ON, &ret);

	cci_write(sensor->regmap, IMX111_REG_DIG_GAIN_GREENR, val, &ret);
	cci_write(sensor->regmap, IMX111_REG_DIG_GAIN_RED, val, &ret);
	cci_write(sensor->regmap, IMX111_REG_DIG_GAIN_BLUE, val, &ret);
	cci_write(sensor->regmap, IMX111_REG_DIG_GAIN_GREENB, val, &ret);

	cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
			IMX111_GROUP_WRITE_ON, 0, &ret);

	return ret;
}

static int imx111_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx111 *sensor = ctrl_to_imx111(ctrl);
	struct device *dev = regmap_get_device(sensor->regmap);
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		s64 max = sensor->cur_mode->height + ctrl->val -
			  IMX111_INTEGRATION_TIME_OFFSET;

		ret = __v4l2_ctrl_modify_range(sensor->exposure,
					       sensor->exposure->minimum,
					       max, sensor->exposure->step,
					       max);
		if (ret)
			return ret;
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_in_use(dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(sensor->regmap, IMX111_REG_ANALOG_GAIN, ctrl->val,
			  &ret);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = imx111_update_digital_gain(sensor, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON,
				IMX111_GROUP_WRITE_ON, &ret);
		cci_write(sensor->regmap, IMX111_INTEGRATION_TIME, ctrl->val,
			  &ret);
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON, 0, &ret);
		break;
	case V4L2_CID_HBLANK:
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON,
				IMX111_GROUP_WRITE_ON, &ret);
		cci_write(sensor->regmap, IMX111_HORIZONTAL_TOTAL_LENGTH,
			  sensor->cur_mode->width + ctrl->val, &ret);
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON, 0, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON,
				IMX111_GROUP_WRITE_ON, &ret);
		cci_write(sensor->regmap, IMX111_VERTICAL_TOTAL_LENGTH,
			  sensor->cur_mode->height + ctrl->val, &ret);
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON, 0, &ret);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		cci_write(sensor->regmap, IMX111_IMAGE_ORIENTATION,
			  sensor->hflip->val | sensor->vflip->val << 1, &ret);
		break;
	case V4L2_CID_TEST_PATTERN:
		cci_write(sensor->regmap, IMX111_TEST_PATTERN, ctrl->val,
			  &ret);
		break;
	case V4L2_CID_TEST_PATTERN_RED:
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON,
				IMX111_GROUP_WRITE_ON, &ret);
		cci_write(sensor->regmap, IMX111_SOLID_COLOR_RED, ctrl->val,
			  &ret);
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON, 0, &ret);
		break;
	case V4L2_CID_TEST_PATTERN_GREENR:
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON,
				IMX111_GROUP_WRITE_ON, &ret);
		cci_write(sensor->regmap, IMX111_SOLID_COLOR_GR, ctrl->val,
			  &ret);
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON, 0, &ret);
		break;
	case V4L2_CID_TEST_PATTERN_BLUE:
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON,
				IMX111_GROUP_WRITE_ON, &ret);
		cci_write(sensor->regmap, IMX111_SOLID_COLOR_BLUE, ctrl->val,
			  &ret);
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON, 0, &ret);
		break;
	case V4L2_CID_TEST_PATTERN_GREENB:
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON,
				IMX111_GROUP_WRITE_ON, &ret);
		cci_write(sensor->regmap, IMX111_SOLID_COLOR_GB, ctrl->val,
			  &ret);
		cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
				IMX111_GROUP_WRITE_ON, 0, &ret);
		break;
	default:
		ret = -EINVAL;
	}

	pm_runtime_put(dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx111_ctrl_ops = {
	.s_ctrl = imx111_set_ctrl,
};

static const char * const test_pattern_menu[] = {
	"Disabled",
	"Solid Color Fill",
	"Standard Color Bars",
	"Fade To Grey Color Bars",
	"Pseudorandom data",
};

static int imx111_init_controls(struct imx111 *sensor)
{
	const struct v4l2_ctrl_ops *ops = &imx111_ctrl_ops;
	struct device *dev = regmap_get_device(sensor->regmap);
	const struct imx111_mode *mode = sensor->cur_mode;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *hdl = &sensor->hdl;
	s64 pixel_rate_min, pixel_rate_max;
	int i, ret;

	ret = v4l2_fwnode_device_parse(dev, &props);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(hdl, 15);

	pixel_rate_min = div_u64(sensor->pixel_clk_raw,
				 2 * IMX111_DATA_DEPTH_RAW10);
	pixel_rate_max = div_u64(sensor->pixel_clk_raw,
				 2 * IMX111_DATA_DEPTH_RAW8);
	sensor->pixel_rate = v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE,
					       pixel_rate_min, pixel_rate_max,
					       1,
					       div_u64(sensor->pixel_clk_raw,
						       2 *
						       sensor->data_depth));

	sensor->link_freq = v4l2_ctrl_new_int_menu(hdl, NULL,
						   V4L2_CID_LINK_FREQ, 0, 0,
						   &sensor->default_link_freq);
	if (sensor->link_freq)
		sensor->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX111_ANA_GAIN_MIN, IMX111_ANA_GAIN_MAX,
			  IMX111_ANA_GAIN_STEP, IMX111_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_DIGITAL_GAIN,
			  IMX111_DGTL_GAIN_MIN, IMX111_DGTL_GAIN_MAX,
			  IMX111_DGTL_GAIN_STEP, IMX111_DGTL_GAIN_DEFAULT);

	sensor->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP, 0, 1, 1,
					  0);
	if (sensor->hflip)
		sensor->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	sensor->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP, 0, 1, 1,
					  0);
	if (sensor->vflip)
		sensor->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	sensor->vblank = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VBLANK,
					   IMX111_VBLANK_MIN,
					   IMX111_VTL_MAX - mode->height, 1,
					   mode->vtl_def - mode->height);
	sensor->hblank = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HBLANK,
					   IMX111_HBLANK_MIN,
					   IMX111_HTL_MAX - mode->width, 1,
					   mode->htl_def - mode->width);

	/*
	 * The maximum coarse integration time is the frame length in lines
	 * minus five.
	 */
	sensor->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					     IMX111_INTEGRATION_TIME_MIN,
					     IMX111_PIXEL_ARRAY_HEIGHT -
					     IMX111_INTEGRATION_TIME_OFFSET,
					     IMX111_INTEGRATION_TIME_STEP,
					     IMX111_PIXEL_ARRAY_HEIGHT -
					     IMX111_INTEGRATION_TIME_OFFSET);

	v4l2_ctrl_new_fwnode_properties(hdl, ops, &props);

	v4l2_ctrl_new_std_menu_items(hdl, ops, V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(test_pattern_menu) - 1, 0, 0,
				     test_pattern_menu);
	for (i = 0; i < 4; i++) {
		/*
		 * The assumption is that
		 * TEST_PATTERN_GREENR == TEST_PATTERN_RED + 1
		 * TEST_PATTERN_BLUE   == TEST_PATTERN_RED + 2
		 * TEST_PATTERN_GREENB == TEST_PATTERN_RED + 3
		 */
		v4l2_ctrl_new_std(hdl, ops, V4L2_CID_TEST_PATTERN_RED + i,
				  IMX111_TESTP_COLOUR_MIN,
				  IMX111_TESTP_COLOUR_MAX,
				  IMX111_TESTP_COLOUR_STEP,
				  IMX111_TESTP_COLOUR_MAX);
		/* The "Solid color" pattern is white by default */
	}

	if (hdl->error)
		return hdl->error;

	sensor->sd.ctrl_handler = hdl;

	return 0;
};

static int imx111_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct imx111 *sensor = sd_to_imx111(sd);
	struct device *dev = regmap_get_device(sensor->regmap);
	const struct imx111_mode *mode = sensor->cur_mode;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	/* Apply default values of current mode */
	ret = cci_multi_reg_write(sensor->regmap, mode->reg_list.regs,
				  mode->reg_list.num_of_regs, NULL);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize the sensor\n");
		goto err_rpm_put;
	}

	cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
			IMX111_GROUP_WRITE_ON,
			IMX111_GROUP_WRITE_ON, &ret);
	cci_write(sensor->regmap, IMX111_DATA_DEPTH,
		  sensor->data_depth | sensor->data_depth << 8, &ret);
	cci_update_bits(sensor->regmap, IMX111_GROUP_WRITE,
			IMX111_GROUP_WRITE_ON, 0, &ret);

	if (ret)
		goto err_rpm_put;

	ret = __v4l2_ctrl_handler_setup(&sensor->hdl);
	if (ret)
		goto err_rpm_put;

	ret = cci_write(sensor->regmap, IMX111_STREAMING_MODE,
			IMX111_MODE_STREAMING, NULL);
	if (ret)
		dev_err(dev, "failed to start stream");

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(sensor->vflip, true);
	__v4l2_ctrl_grab(sensor->hflip, true);

	msleep(30);

	return 0;

err_rpm_put:
	pm_runtime_put_autosuspend(dev);
	return ret;
}

static int imx111_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct imx111 *sensor = sd_to_imx111(sd);
	struct device *dev = regmap_get_device(sensor->regmap);
	int ret;

	ret = cci_write(sensor->regmap, IMX111_STREAMING_MODE,
			IMX111_MODE_STANDBY, NULL);
	if (ret)
		dev_err(dev, "failed to stop stream\n");

	__v4l2_ctrl_grab(sensor->vflip, false);
	__v4l2_ctrl_grab(sensor->hflip, false);

	pm_runtime_put_autosuspend(dev);

	return ret;
}

static int imx111_initialize(struct imx111 *sensor)
{
	struct device *dev = regmap_get_device(sensor->regmap);
	int ret = 0;

	/* Configure the PLL. */
	cci_write(sensor->regmap, IMX111_PRE_PLL_CLK_DIVIDER_PLL1,
		  sensor->pll->pre_div, &ret);
	cci_write(sensor->regmap, IMX111_PLL_MULTIPLIER_PLL1,
		  sensor->pll->mult, &ret);
	cci_write(sensor->regmap, IMX111_POST_DIVIDER,
		  IMX111_POST_DIVIDER_DIV1, &ret);
	cci_write(sensor->regmap, IMX111_PLL_SETTLING_TIME,
		  to_settle_delay(sensor->pll->extclk_rate), &ret);

	cci_multi_reg_write(sensor->regmap, imx111_global_init,
			    ARRAY_SIZE(imx111_global_init), &ret);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize the sensor\n");
		return ret;
	}

	return 0;
}

/* ----------------------------------------------------------------------------
 * IMX111 Pad Subdev Init and Operations
 */
static int imx111_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx111 *sensor = sd_to_imx111(sd);

	if (code->index >= ARRAY_SIZE(imx111_mbus_formats) / 4)
		return -EINVAL;

	code->code = imx111_get_format_code(sensor,
					    imx111_mbus_formats[code->index *
					    4], false);

	return 0;
}

static int imx111_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx111 *sensor = sd_to_imx111(sd);
	u32 code;

	if (fse->index >= ARRAY_SIZE(imx111_modes))
		return -EINVAL;

	code = imx111_get_format_code(sensor, fse->code, true);
	if (fse->code != code)
		return -EINVAL;

	fse->min_width = imx111_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = imx111_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int imx111_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_format *format)
{
	struct imx111 *sensor = sd_to_imx111(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &format->format;
	struct v4l2_mbus_framefmt *fmt;
	const struct imx111_mode *mode;

	mode = v4l2_find_nearest_size(imx111_modes, ARRAY_SIZE(imx111_modes),
				      width, height,
				      mbus_fmt->width, mbus_fmt->height);

	fmt = v4l2_subdev_state_get_format(state, format->pad);

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)	{
		int ret;

		sensor->cur_mode = mode;
		sensor->data_depth = imx111_get_format_bpp(fmt);

		ret = __v4l2_ctrl_s_ctrl_int64(sensor->pixel_rate,
					       div_u64(sensor->pixel_clk_raw,
						       2 *
						       sensor->data_depth));
		if (ret)
			return ret;

		ret = __v4l2_ctrl_modify_range(sensor->vblank,
					       IMX111_VBLANK_MIN,
					       IMX111_VTL_MAX - mode->height,
					       1,
					       mode->vtl_def - mode->height);
		if (ret)
			return ret;

		ret = __v4l2_ctrl_s_ctrl(sensor->vblank, mode->vtl_def -
					 mode->height);
		if (ret)
			return ret;

		ret = __v4l2_ctrl_modify_range(sensor->hblank,
					       IMX111_HBLANK_MIN,
					       IMX111_HTL_MAX - mode->width,
					       1,
					       mode->htl_def - mode->width);
		if (ret)
			return ret;

		ret = __v4l2_ctrl_s_ctrl(sensor->hblank, mode->htl_def -
					 mode->width);
		if (ret)
			return ret;
	}

	fmt->code = imx111_get_format_code(sensor, mbus_fmt->code, false);
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->colorspace = V4L2_COLORSPACE_RAW;

	*mbus_fmt = *fmt;

	return 0;
}

static int imx111_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct imx111 *sensor = sd_to_imx111(sd);
	const struct imx111_mode *mode = sensor->cur_mode;
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_state_get_format(sd_state, 0);

	fmt->code = MEDIA_BUS_FMT_SGBRG10_1X10;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;

	return 0;
}

static const struct v4l2_subdev_video_ops imx111_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops imx111_pad_ops = {
	.enum_mbus_code = imx111_enum_mbus_code,
	.enum_frame_size = imx111_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx111_set_format,
	.enable_streams = imx111_enable_streams,
	.disable_streams = imx111_disable_streams,
};

static const struct v4l2_subdev_ops imx111_subdev_ops = {
	.video = &imx111_video_ops,
	.pad = &imx111_pad_ops,
};

static const struct media_entity_operations imx111_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops imx111_internal_ops = {
	.init_state = imx111_init_state,
};

static int imx111_init_subdev(struct imx111 *sensor, struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct v4l2_subdev *sd = &sensor->sd;
	struct media_pad *pad = &sensor->pad;
	struct v4l2_ctrl_handler *hdl = &sensor->hdl;
	int ret;

	/* Initialize the subdev. */
	v4l2_i2c_subdev_init(sd, client, &imx111_subdev_ops);

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &imx111_internal_ops;

	/* Initialize the media entity. */
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sd->entity.ops = &imx111_subdev_entity_ops;
	pad->flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity, 1, pad);
	if (ret < 0) {
		dev_err(dev, "failed to init entity pads: %d", ret);
		return ret;
	}

	/* Initialize the control handler. */
	ret = imx111_init_controls(sensor);
	if (ret)
		goto error;

	return 0;
error:
	v4l2_ctrl_handler_free(hdl);
	media_entity_cleanup(&sd->entity);
	return ret;
};

/* ----------------------------------------------------------------------------
 * Power Management
 */

static int imx111_power_on(struct imx111 *sensor)
{
	int ret;

	if (sensor->reset)
		gpiod_set_value(sensor->reset, 1);

	ret = regulator_bulk_enable(ARRAY_SIZE(imx111_supplies),
				    sensor->supplies);
	if (ret < 0)
		return ret;

	usleep_range(500, 600);

	if (sensor->reset)
		gpiod_set_value(sensor->reset, 0);

	usleep_range(200, 250);

	ret = clk_prepare_enable(sensor->extclk);
	if (ret < 0)
		goto error_regulator;

	usleep_range(200, 250);

	return 0;

error_regulator:
	regulator_bulk_disable(ARRAY_SIZE(imx111_supplies), sensor->supplies);
	return ret;
}

static void imx111_power_off(struct imx111 *sensor)
{
	if (sensor->reset)
		gpiod_set_value(sensor->reset, 1);
	usleep_range(1000, 2000);

	clk_disable_unprepare(sensor->extclk);
	regulator_bulk_disable(ARRAY_SIZE(imx111_supplies), sensor->supplies);
}

static int __maybe_unused imx111_pm_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx111 *sensor = sd_to_imx111(sd);
	int ret;

	ret = imx111_power_on(sensor);
	if (ret)
		return ret;

	ret = imx111_initialize(sensor);
	if (ret) {
		imx111_power_off(sensor);
		return ret;
	}

	return 0;
}

static int __maybe_unused imx111_pm_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx111 *sensor = sd_to_imx111(sd);

	imx111_power_off(sensor);

	return 0;
}

static const struct dev_pm_ops imx111_pm_ops = {
	SET_RUNTIME_PM_OPS(imx111_pm_runtime_suspend,
			   imx111_pm_runtime_resume, NULL)
};

/* ----------------------------------------------------------------------------
 * Probe & Remove
 */

static int imx111_identify_module(struct imx111 *sensor)
{
	struct device *dev = regmap_get_device(sensor->regmap);
	u64 value, revision, manufacturer;
	int ret = 0;

	ret = cci_read(sensor->regmap, IMX111_PRODUCT_ID, &value, NULL);
	if (ret)
		return ret;

	if (value != IMX111_CHIP_ID) {
		dev_err(dev, "chip id mismatch: %x!=%04llx", IMX111_CHIP_ID,
			value);
		return -ENXIO;
	}

	cci_read(sensor->regmap, IMX111_REVISION, &revision, &ret);
	cci_read(sensor->regmap, IMX111_MANUFACTURER_ID, &manufacturer, &ret);

	dev_dbg(dev, "module IMX%03llx rev. %llu manufacturer %llu\n",
		value, revision, manufacturer);

	return ret;
}

static int imx111_clk_init(struct imx111 *sensor)
{
	struct device *dev = regmap_get_device(sensor->regmap);
	u32 ndata_lanes = sensor->bus_cfg.bus.mipi_csi2.num_data_lanes;
	u64 extclk_rate, system_clk;
	unsigned int i;

	extclk_rate = clk_get_rate(sensor->extclk);
	if (!extclk_rate)
		return dev_err_probe(dev, -EINVAL, "EXTCLK rate unknown\n");

	for (i = 0; i < ARRAY_SIZE(imx111_pll); i++) {
		if (clk_get_rate(sensor->extclk) ==
		    imx111_pll[i].extclk_rate) {
			sensor->pll = &imx111_pll[i];
			break;
		}
	}
	if (!sensor->pll)
		return dev_err_probe(dev, -EINVAL,
				     "Unsupported EXTCLK rate %llu\n",
				     extclk_rate);

	system_clk = div_u64(extclk_rate, sensor->pll->pre_div) *
		     sensor->pll->mult;

	/*
	 * Pixel clock or Logic clock is used for internal image processing is
	 * generated by dividing into 1/10 or 1/8 frequency according to the
	 * word length of the CSI2 interface. This clock is designating the
	 * pixel rate and used as the base of integration time, frame rate etc.
	 */
	sensor->pixel_clk_raw = system_clk * ndata_lanes;

	/*
	 * The CSI-2 bus is clocked for 16-bit per pixel, transmitted in DDR
	 * over n lanes for RAW10 default format.
	 */
	sensor->default_link_freq = div_u64(sensor->pixel_clk_raw * 8,
					    2 * IMX111_DATA_DEPTH_RAW10);

	if (sensor->bus_cfg.nr_of_link_frequencies != 1 ||
	    sensor->bus_cfg.link_frequencies[0] != sensor->default_link_freq)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid link-frequency, expected %llu\n",
				     sensor->default_link_freq);

	return 0;
}

static int imx111_parse_dt(struct imx111 *sensor)
{
	struct device *dev = regmap_get_device(sensor->regmap);
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct fwnode_handle *ep;
	int ret;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep) {
		dev_err(dev, "No endpoint found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &sensor->bus_cfg);
	fwnode_handle_put(ep);
	if (ret < 0) {
		dev_err(dev, "Failed to parse endpoint\n");
		goto error;
	}

	sensor->bus_cfg.bus_type = V4L2_MBUS_CSI2_DPHY;

	/* Check the number of MIPI CSI2 data lanes */
	if (sensor->bus_cfg.bus.mipi_csi2.num_data_lanes > 2) {
		dev_err(dev, "number of lanes is more than 2\n");
		ret = -EINVAL;
		goto error;
	}

	return 0;

error:
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);
	return ret;
}

static int imx111_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx111 *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(sensor->regmap))
		return dev_err_probe(dev, PTR_ERR(sensor->regmap),
				     "Failed to allocate register map\n");

	sensor->extclk = devm_v4l2_sensor_clk_get(dev, NULL);
	if (IS_ERR(sensor->extclk))
		return dev_err_probe(dev, PTR_ERR(sensor->extclk),
				     "Failed to get clock\n");

	sensor->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset))
		return dev_err_probe(dev, PTR_ERR(sensor->reset),
				     "Failed to get reset GPIO\n");

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(imx111_supplies),
					    imx111_supplies,
					    &sensor->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ret = imx111_parse_dt(sensor);
	if (ret < 0)
		return ret;

	ret = imx111_clk_init(sensor);
	if (ret < 0)
		goto error_ep_free;

	ret = imx111_power_on(sensor);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Could not power on the device\n");
		goto error_ep_free;
	}

	ret = imx111_identify_module(sensor);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Could not identify module\n");
		goto error_power_off;
	}

	sensor->cur_mode = &imx111_modes[IMX111_MODE_3280x2464];
	sensor->data_depth = IMX111_DATA_DEPTH_RAW10;

	ret = imx111_initialize(sensor);
	if (ret < 0)
		goto error_power_off;

	ret = imx111_init_subdev(sensor, client);
	if (ret < 0) {
		dev_err(dev, "failed to init controls: %d", ret);
		goto error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto error_v4l2_ctrl_handler_free;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register V4L2 subdev: %d", ret);
		goto error_pm;
	}

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_idle(dev);

	return 0;

error_pm:
	v4l2_subdev_cleanup(&sensor->sd);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);

error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(&sensor->hdl);
	media_entity_cleanup(&sensor->sd.entity);

error_power_off:
	imx111_power_off(sensor);

error_ep_free:
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);

	return ret;
}

static void imx111_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx111 *sensor = sd_to_imx111(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->hdl);
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);

	/*
	 * Disable runtime PM. In case runtime PM is disabled in the kernel,
	 * make sure to turn power off manually.
	 */
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		imx111_power_off(sensor);
		pm_runtime_set_suspended(&client->dev);
	}
}

static const struct of_device_id imx111_of_match[] = {
	{ .compatible = "sony,imx111" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx111_of_match);

static struct i2c_driver imx111_i2c_driver = {
	.driver = {
		.name = "imx111",
		.of_match_table = imx111_of_match,
		.pm = &imx111_pm_ops,
	},
	.probe = imx111_probe,
	.remove = imx111_remove,
};
module_i2c_driver(imx111_i2c_driver);

MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("Sony IMX111 CMOS Image Sensor driver");
MODULE_LICENSE("GPL");
