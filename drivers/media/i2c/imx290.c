// SPDX-License-Identifier: GPL-2.0
/*
 * Sony IMX290 CMOS Image Sensor Driver
 *
 * Copyright (C) 2019 FRAMOS GmbH.
 *
 * Copyright (C) 2019 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/unaligned.h>

#include <media/media-entity.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define IMX290_STANDBY					CCI_REG8(0x3000)
#define IMX290_REGHOLD					CCI_REG8(0x3001)
#define IMX290_XMSTA					CCI_REG8(0x3002)
#define IMX290_ADBIT					CCI_REG8(0x3005)
#define IMX290_ADBIT_10BIT				(0 << 0)
#define IMX290_ADBIT_12BIT				(1 << 0)
#define IMX290_CTRL_07					CCI_REG8(0x3007)
#define IMX290_VREVERSE					BIT(0)
#define IMX290_HREVERSE					BIT(1)
#define IMX290_WINMODE_1080P				(0 << 4)
#define IMX290_WINMODE_720P				(1 << 4)
#define IMX290_WINMODE_CROP				(4 << 4)
#define IMX290_FR_FDG_SEL				CCI_REG8(0x3009)
#define IMX290_BLKLEVEL					CCI_REG16_LE(0x300a)
#define IMX290_GAIN					CCI_REG8(0x3014)
#define IMX290_VMAX					CCI_REG24_LE(0x3018)
#define IMX290_VMAX_MAX					0x3ffff
#define IMX290_HMAX					CCI_REG16_LE(0x301c)
#define IMX290_HMAX_MAX					0xffff
#define IMX290_SHS1					CCI_REG24_LE(0x3020)
#define IMX290_WINWV_OB					CCI_REG8(0x303a)
#define IMX290_WINPV					CCI_REG16_LE(0x303c)
#define IMX290_WINWV					CCI_REG16_LE(0x303e)
#define IMX290_WINPH					CCI_REG16_LE(0x3040)
#define IMX290_WINWH					CCI_REG16_LE(0x3042)
#define IMX290_OUT_CTRL					CCI_REG8(0x3046)
#define IMX290_ODBIT_10BIT				(0 << 0)
#define IMX290_ODBIT_12BIT				(1 << 0)
#define IMX290_OPORTSEL_PARALLEL			(0x0 << 4)
#define IMX290_OPORTSEL_LVDS_2CH			(0xd << 4)
#define IMX290_OPORTSEL_LVDS_4CH			(0xe << 4)
#define IMX290_OPORTSEL_LVDS_8CH			(0xf << 4)
#define IMX290_XSOUTSEL					CCI_REG8(0x304b)
#define IMX290_XSOUTSEL_XVSOUTSEL_HIGH			(0 << 0)
#define IMX290_XSOUTSEL_XVSOUTSEL_VSYNC			(2 << 0)
#define IMX290_XSOUTSEL_XHSOUTSEL_HIGH			(0 << 2)
#define IMX290_XSOUTSEL_XHSOUTSEL_HSYNC			(2 << 2)
#define IMX290_INCKSEL1					CCI_REG8(0x305c)
#define IMX290_INCKSEL2					CCI_REG8(0x305d)
#define IMX290_INCKSEL3					CCI_REG8(0x305e)
#define IMX290_INCKSEL4					CCI_REG8(0x305f)
#define IMX290_PGCTRL					CCI_REG8(0x308c)
#define IMX290_ADBIT1					CCI_REG8(0x3129)
#define IMX290_ADBIT1_10BIT				0x1d
#define IMX290_ADBIT1_12BIT				0x00
#define IMX290_INCKSEL5					CCI_REG8(0x315e)
#define IMX290_INCKSEL6					CCI_REG8(0x3164)
#define IMX290_ADBIT2					CCI_REG8(0x317c)
#define IMX290_ADBIT2_10BIT				0x12
#define IMX290_ADBIT2_12BIT				0x00
#define IMX290_ADBIT3					CCI_REG8(0x31ec)
#define IMX290_ADBIT3_10BIT				0x37
#define IMX290_ADBIT3_12BIT				0x0e
#define IMX290_REPETITION				CCI_REG8(0x3405)
#define IMX290_PHY_LANE_NUM				CCI_REG8(0x3407)
#define IMX290_OPB_SIZE_V				CCI_REG8(0x3414)
#define IMX290_Y_OUT_SIZE				CCI_REG16_LE(0x3418)
#define IMX290_CSI_DT_FMT				CCI_REG16_LE(0x3441)
#define IMX290_CSI_DT_FMT_RAW10				0x0a0a
#define IMX290_CSI_DT_FMT_RAW12				0x0c0c
#define IMX290_CSI_LANE_MODE				CCI_REG8(0x3443)
#define IMX290_EXTCK_FREQ				CCI_REG16_LE(0x3444)
#define IMX290_TCLKPOST					CCI_REG16_LE(0x3446)
#define IMX290_THSZERO					CCI_REG16_LE(0x3448)
#define IMX290_THSPREPARE				CCI_REG16_LE(0x344a)
#define IMX290_TCLKTRAIL				CCI_REG16_LE(0x344c)
#define IMX290_THSTRAIL					CCI_REG16_LE(0x344e)
#define IMX290_TCLKZERO					CCI_REG16_LE(0x3450)
#define IMX290_TCLKPREPARE				CCI_REG16_LE(0x3452)
#define IMX290_TLPX					CCI_REG16_LE(0x3454)
#define IMX290_X_OUT_SIZE				CCI_REG16_LE(0x3472)
#define IMX290_INCKSEL7					CCI_REG8(0x3480)

#define IMX290_PGCTRL_REGEN				BIT(0)
#define IMX290_PGCTRL_THRU				BIT(1)
#define IMX290_PGCTRL_MODE(n)				((n) << 4)

/* Number of lines by which exposure must be less than VMAX */
#define IMX290_EXPOSURE_OFFSET				2

#define IMX290_PIXEL_RATE				148500000

/*
 * The IMX290 pixel array is organized as follows:
 *
 *     +------------------------------------+
 *     |           Optical Black            |     }  Vertical effective optical black (10)
 * +---+------------------------------------+---+
 * |   |                                    |   | }  Effective top margin (8)
 * |   |   +----------------------------+   |   | \
 * |   |   |                            |   |   |  |
 * |   |   |                            |   |   |  |
 * |   |   |                            |   |   |  |
 * |   |   |    Recording Pixel Area    |   |   |  | Recommended height (1080)
 * |   |   |                            |   |   |  |
 * |   |   |                            |   |   |  |
 * |   |   |                            |   |   |  |
 * |   |   +----------------------------+   |   | /
 * |   |                                    |   | }  Effective bottom margin (9)
 * +---+------------------------------------+---+
 *  <-> <-> <--------------------------> <-> <->
 *                                            \----  Ignored right margin (4)
 *                                        \--------  Effective right margin (9)
 *                       \-------------------------  Recommended width (1920)
 *       \-----------------------------------------  Effective left margin (8)
 *   \---------------------------------------------  Ignored left margin (4)
 *
 * The optical black lines are output over CSI-2 with a separate data type.
 *
 * The pixel array is meant to have 1920x1080 usable pixels after image
 * processing in an ISP. It has 8 (9) extra active pixels usable for color
 * processing in the ISP on the top and left (bottom and right) sides of the
 * image. In addition, 4 additional pixels are present on the left and right
 * sides of the image, documented as "ignored area".
 *
 * As far as is understood, all pixels of the pixel array (ignored area, color
 * processing margins and recording area) can be output by the sensor.
 */

#define IMX290_PIXEL_ARRAY_WIDTH			1945
#define IMX290_PIXEL_ARRAY_HEIGHT			1097
#define IMX290_PIXEL_ARRAY_MARGIN_LEFT			12
#define IMX290_PIXEL_ARRAY_MARGIN_RIGHT			13
#define IMX290_PIXEL_ARRAY_MARGIN_TOP			8
#define IMX290_PIXEL_ARRAY_MARGIN_BOTTOM		9
#define IMX290_PIXEL_ARRAY_RECORDING_WIDTH		1920
#define IMX290_PIXEL_ARRAY_RECORDING_HEIGHT		1080

/* Equivalent value for 16bpp */
#define IMX290_BLACK_LEVEL_DEFAULT			3840

#define IMX290_NUM_SUPPLIES				3

enum imx290_colour_variant {
	IMX290_VARIANT_COLOUR,
	IMX290_VARIANT_MONO,
	IMX290_VARIANT_MAX
};

enum imx290_model {
	IMX290_MODEL_IMX290LQR,
	IMX290_MODEL_IMX290LLR,
	IMX290_MODEL_IMX327LQR,
	IMX290_MODEL_IMX462LQR,
	IMX290_MODEL_IMX462LLR,
};

struct imx290_model_info {
	enum imx290_colour_variant colour_variant;
	const struct cci_reg_sequence *init_regs;
	size_t init_regs_num;
	unsigned int max_analog_gain;
	const char *name;
};

enum imx290_clk_freq {
	IMX290_CLK_37_125,
	IMX290_CLK_74_25,
	IMX290_NUM_CLK
};

/*
 * Clock configuration for registers INCKSEL1 to INCKSEL6.
 */
struct imx290_clk_cfg {
	u8 incksel1;
	u8 incksel2;
	u8 incksel3;
	u8 incksel4;
	u8 incksel5;
	u8 incksel6;
};

struct imx290_mode {
	u32 width;
	u32 height;
	u32 hmax_min;
	u32 vmax_min;
	u8 link_freq_index;
	u8 ctrl_07;

	const struct cci_reg_sequence *data;
	u32 data_size;

	const struct imx290_clk_cfg *clk_cfg;
};

struct imx290_csi_cfg {
	u16 repetition;
	u16 tclkpost;
	u16 thszero;
	u16 thsprepare;
	u16 tclktrail;
	u16 thstrail;
	u16 tclkzero;
	u16 tclkprepare;
	u16 tlpx;
};

struct imx290 {
	struct device *dev;
	struct clk *xclk;
	struct regmap *regmap;
	enum imx290_clk_freq xclk_idx;
	u8 nlanes;
	const struct imx290_model_info *model;

	struct v4l2_subdev sd;
	struct media_pad pad;

	const struct imx290_mode *current_mode;

	struct regulator_bulk_data supplies[IMX290_NUM_SUPPLIES];
	struct gpio_desc *rst_gpio;

	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct {
		struct v4l2_ctrl *hflip;
		struct v4l2_ctrl *vflip;
	};
};

static inline struct imx290 *to_imx290(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx290, sd);
}

/* -----------------------------------------------------------------------------
 * Modes and formats
 */

static const struct cci_reg_sequence imx290_global_init_settings[] = {
	{ IMX290_WINWV_OB, 12 },
	{ IMX290_WINPH, 0 },
	{ IMX290_WINPV, 0 },
	{ IMX290_WINWH, 1948 },
	{ IMX290_WINWV, 1097 },
	{ IMX290_XSOUTSEL, IMX290_XSOUTSEL_XVSOUTSEL_VSYNC |
			   IMX290_XSOUTSEL_XHSOUTSEL_HSYNC },
	{ CCI_REG8(0x3012), 0x64 },
	{ CCI_REG8(0x3013), 0x00 },
};

static const struct cci_reg_sequence imx290_global_init_settings_290[] = {
	{ CCI_REG8(0x300f), 0x00 },
	{ CCI_REG8(0x3010), 0x21 },
	{ CCI_REG8(0x3011), 0x00 },
	{ CCI_REG8(0x3016), 0x09 },
	{ CCI_REG8(0x3070), 0x02 },
	{ CCI_REG8(0x3071), 0x11 },
	{ CCI_REG8(0x309b), 0x10 },
	{ CCI_REG8(0x309c), 0x22 },
	{ CCI_REG8(0x30a2), 0x02 },
	{ CCI_REG8(0x30a6), 0x20 },
	{ CCI_REG8(0x30a8), 0x20 },
	{ CCI_REG8(0x30aa), 0x20 },
	{ CCI_REG8(0x30ac), 0x20 },
	{ CCI_REG8(0x30b0), 0x43 },
	{ CCI_REG8(0x3119), 0x9e },
	{ CCI_REG8(0x311c), 0x1e },
	{ CCI_REG8(0x311e), 0x08 },
	{ CCI_REG8(0x3128), 0x05 },
	{ CCI_REG8(0x313d), 0x83 },
	{ CCI_REG8(0x3150), 0x03 },
	{ CCI_REG8(0x317e), 0x00 },
	{ CCI_REG8(0x32b8), 0x50 },
	{ CCI_REG8(0x32b9), 0x10 },
	{ CCI_REG8(0x32ba), 0x00 },
	{ CCI_REG8(0x32bb), 0x04 },
	{ CCI_REG8(0x32c8), 0x50 },
	{ CCI_REG8(0x32c9), 0x10 },
	{ CCI_REG8(0x32ca), 0x00 },
	{ CCI_REG8(0x32cb), 0x04 },
	{ CCI_REG8(0x332c), 0xd3 },
	{ CCI_REG8(0x332d), 0x10 },
	{ CCI_REG8(0x332e), 0x0d },
	{ CCI_REG8(0x3358), 0x06 },
	{ CCI_REG8(0x3359), 0xe1 },
	{ CCI_REG8(0x335a), 0x11 },
	{ CCI_REG8(0x3360), 0x1e },
	{ CCI_REG8(0x3361), 0x61 },
	{ CCI_REG8(0x3362), 0x10 },
	{ CCI_REG8(0x33b0), 0x50 },
	{ CCI_REG8(0x33b2), 0x1a },
	{ CCI_REG8(0x33b3), 0x04 },
};

static const struct cci_reg_sequence imx290_global_init_settings_462[] = {
	{ CCI_REG8(0x300f), 0x00 },
	{ CCI_REG8(0x3010), 0x21 },
	{ CCI_REG8(0x3011), 0x02 },
	{ CCI_REG8(0x3016), 0x09 },
	{ CCI_REG8(0x3070), 0x02 },
	{ CCI_REG8(0x3071), 0x11 },
	{ CCI_REG8(0x309b), 0x10 },
	{ CCI_REG8(0x309c), 0x22 },
	{ CCI_REG8(0x30a2), 0x02 },
	{ CCI_REG8(0x30a6), 0x20 },
	{ CCI_REG8(0x30a8), 0x20 },
	{ CCI_REG8(0x30aa), 0x20 },
	{ CCI_REG8(0x30ac), 0x20 },
	{ CCI_REG8(0x30b0), 0x43 },
	{ CCI_REG8(0x3119), 0x9e },
	{ CCI_REG8(0x311c), 0x1e },
	{ CCI_REG8(0x311e), 0x08 },
	{ CCI_REG8(0x3128), 0x05 },
	{ CCI_REG8(0x313d), 0x83 },
	{ CCI_REG8(0x3150), 0x03 },
	{ CCI_REG8(0x317e), 0x00 },
	{ CCI_REG8(0x32b8), 0x50 },
	{ CCI_REG8(0x32b9), 0x10 },
	{ CCI_REG8(0x32ba), 0x00 },
	{ CCI_REG8(0x32bb), 0x04 },
	{ CCI_REG8(0x32c8), 0x50 },
	{ CCI_REG8(0x32c9), 0x10 },
	{ CCI_REG8(0x32ca), 0x00 },
	{ CCI_REG8(0x32cb), 0x04 },
	{ CCI_REG8(0x332c), 0xd3 },
	{ CCI_REG8(0x332d), 0x10 },
	{ CCI_REG8(0x332e), 0x0d },
	{ CCI_REG8(0x3358), 0x06 },
	{ CCI_REG8(0x3359), 0xe1 },
	{ CCI_REG8(0x335a), 0x11 },
	{ CCI_REG8(0x3360), 0x1e },
	{ CCI_REG8(0x3361), 0x61 },
	{ CCI_REG8(0x3362), 0x10 },
	{ CCI_REG8(0x33b0), 0x50 },
	{ CCI_REG8(0x33b2), 0x1a },
	{ CCI_REG8(0x33b3), 0x04 },
};

#define IMX290_NUM_CLK_REGS	2
static const struct cci_reg_sequence xclk_regs[][IMX290_NUM_CLK_REGS] = {
	[IMX290_CLK_37_125] = {
		{ IMX290_EXTCK_FREQ, (37125 * 256) / 1000 },
		{ IMX290_INCKSEL7, 0x49 },
	},
	[IMX290_CLK_74_25] = {
		{ IMX290_EXTCK_FREQ, (74250 * 256) / 1000 },
		{ IMX290_INCKSEL7, 0x92 },
	},
};

static const struct cci_reg_sequence imx290_global_init_settings_327[] = {
	{ CCI_REG8(0x3011), 0x02 },
	{ CCI_REG8(0x309e), 0x4A },
	{ CCI_REG8(0x309f), 0x4A },
	{ CCI_REG8(0x313b), 0x61 },
};

static const struct cci_reg_sequence imx290_1080p_settings[] = {
	/* mode settings */
	{ IMX290_WINWV_OB, 12 },
	{ IMX290_OPB_SIZE_V, 10 },
	{ IMX290_X_OUT_SIZE, 1920 },
	{ IMX290_Y_OUT_SIZE, 1080 },
};

static const struct cci_reg_sequence imx290_720p_settings[] = {
	/* mode settings */
	{ IMX290_WINWV_OB, 6 },
	{ IMX290_OPB_SIZE_V, 4 },
	{ IMX290_X_OUT_SIZE, 1280 },
	{ IMX290_Y_OUT_SIZE, 720 },
};

static const struct cci_reg_sequence imx290_10bit_settings[] = {
	{ IMX290_ADBIT, IMX290_ADBIT_10BIT },
	{ IMX290_OUT_CTRL, IMX290_ODBIT_10BIT },
	{ IMX290_ADBIT1, IMX290_ADBIT1_10BIT },
	{ IMX290_ADBIT2, IMX290_ADBIT2_10BIT },
	{ IMX290_ADBIT3, IMX290_ADBIT3_10BIT },
	{ IMX290_CSI_DT_FMT, IMX290_CSI_DT_FMT_RAW10 },
};

static const struct cci_reg_sequence imx290_12bit_settings[] = {
	{ IMX290_ADBIT, IMX290_ADBIT_12BIT },
	{ IMX290_OUT_CTRL, IMX290_ODBIT_12BIT },
	{ IMX290_ADBIT1, IMX290_ADBIT1_12BIT },
	{ IMX290_ADBIT2, IMX290_ADBIT2_12BIT },
	{ IMX290_ADBIT3, IMX290_ADBIT3_12BIT },
	{ IMX290_CSI_DT_FMT, IMX290_CSI_DT_FMT_RAW12 },
};

static const struct imx290_csi_cfg imx290_csi_222_75mhz = {
	/* 222.75MHz or 445.5Mbit/s per lane */
	.repetition = 0x10,
	.tclkpost = 87,
	.thszero = 55,
	.thsprepare = 31,
	.tclktrail = 31,
	.thstrail = 31,
	.tclkzero = 119,
	.tclkprepare = 31,
	.tlpx = 23,
};

static const struct imx290_csi_cfg imx290_csi_445_5mhz = {
	/* 445.5MHz or 891Mbit/s per lane */
	.repetition = 0x00,
	.tclkpost = 119,
	.thszero = 103,
	.thsprepare = 71,
	.tclktrail = 55,
	.thstrail = 63,
	.tclkzero = 255,
	.tclkprepare = 63,
	.tlpx = 55,
};

static const struct imx290_csi_cfg imx290_csi_148_5mhz = {
	/* 148.5MHz or 297Mbit/s per lane */
	.repetition = 0x10,
	.tclkpost = 79,
	.thszero = 47,
	.thsprepare = 23,
	.tclktrail = 23,
	.thstrail = 23,
	.tclkzero = 87,
	.tclkprepare = 23,
	.tlpx = 23,
};

static const struct imx290_csi_cfg imx290_csi_297mhz = {
	/* 297MHz or 594Mbit/s per lane */
	.repetition = 0x00,
	.tclkpost = 103,
	.thszero = 87,
	.thsprepare = 47,
	.tclktrail = 39,
	.thstrail = 47,
	.tclkzero = 191,
	.tclkprepare = 47,
	.tlpx = 39,
};

/* supported link frequencies */
#define FREQ_INDEX_1080P	0
#define FREQ_INDEX_720P		1
static const s64 imx290_link_freq_2lanes[] = {
	[FREQ_INDEX_1080P] = 445500000,
	[FREQ_INDEX_720P] = 297000000,
};

static const s64 imx290_link_freq_4lanes[] = {
	[FREQ_INDEX_1080P] = 222750000,
	[FREQ_INDEX_720P] = 148500000,
};

/*
 * In this function and in the similar ones below We rely on imx290_probe()
 * to ensure that nlanes is either 2 or 4.
 */
static inline const s64 *imx290_link_freqs_ptr(const struct imx290 *imx290)
{
	if (imx290->nlanes == 2)
		return imx290_link_freq_2lanes;
	else
		return imx290_link_freq_4lanes;
}

static inline int imx290_link_freqs_num(const struct imx290 *imx290)
{
	if (imx290->nlanes == 2)
		return ARRAY_SIZE(imx290_link_freq_2lanes);
	else
		return ARRAY_SIZE(imx290_link_freq_4lanes);
}

static const struct imx290_clk_cfg imx290_1080p_clock_config[] = {
	[IMX290_CLK_37_125] = {
		/* 37.125MHz clock config */
		.incksel1 = 0x18,
		.incksel2 = 0x03,
		.incksel3 = 0x20,
		.incksel4 = 0x01,
		.incksel5 = 0x1a,
		.incksel6 = 0x1a,
	},
	[IMX290_CLK_74_25] = {
		/* 74.25MHz clock config */
		.incksel1 = 0x0c,
		.incksel2 = 0x03,
		.incksel3 = 0x10,
		.incksel4 = 0x01,
		.incksel5 = 0x1b,
		.incksel6 = 0x1b,
	},
};

static const struct imx290_clk_cfg imx290_720p_clock_config[] = {
	[IMX290_CLK_37_125] = {
		/* 37.125MHz clock config */
		.incksel1 = 0x20,
		.incksel2 = 0x00,
		.incksel3 = 0x20,
		.incksel4 = 0x01,
		.incksel5 = 0x1a,
		.incksel6 = 0x1a,
	},
	[IMX290_CLK_74_25] = {
		/* 74.25MHz clock config */
		.incksel1 = 0x10,
		.incksel2 = 0x00,
		.incksel3 = 0x10,
		.incksel4 = 0x01,
		.incksel5 = 0x1b,
		.incksel6 = 0x1b,
	},
};

/* Mode configs */
static const struct imx290_mode imx290_modes_2lanes[] = {
	{
		.width = 1920,
		.height = 1080,
		.hmax_min = 2200,
		.vmax_min = 1125,
		.link_freq_index = FREQ_INDEX_1080P,
		.ctrl_07 = IMX290_WINMODE_1080P,
		.data = imx290_1080p_settings,
		.data_size = ARRAY_SIZE(imx290_1080p_settings),
		.clk_cfg = imx290_1080p_clock_config,
	},
	{
		.width = 1280,
		.height = 720,
		.hmax_min = 3300,
		.vmax_min = 750,
		.link_freq_index = FREQ_INDEX_720P,
		.ctrl_07 = IMX290_WINMODE_720P,
		.data = imx290_720p_settings,
		.data_size = ARRAY_SIZE(imx290_720p_settings),
		.clk_cfg = imx290_720p_clock_config,
	},
};

static const struct imx290_mode imx290_modes_4lanes[] = {
	{
		.width = 1920,
		.height = 1080,
		.hmax_min = 2200,
		.vmax_min = 1125,
		.link_freq_index = FREQ_INDEX_1080P,
		.ctrl_07 = IMX290_WINMODE_1080P,
		.data = imx290_1080p_settings,
		.data_size = ARRAY_SIZE(imx290_1080p_settings),
		.clk_cfg = imx290_1080p_clock_config,
	},
	{
		.width = 1280,
		.height = 720,
		.hmax_min = 3300,
		.vmax_min = 750,
		.link_freq_index = FREQ_INDEX_720P,
		.ctrl_07 = IMX290_WINMODE_720P,
		.data = imx290_720p_settings,
		.data_size = ARRAY_SIZE(imx290_720p_settings),
		.clk_cfg = imx290_720p_clock_config,
	},
};

static inline const struct imx290_mode *imx290_modes_ptr(const struct imx290 *imx290)
{
	if (imx290->nlanes == 2)
		return imx290_modes_2lanes;
	else
		return imx290_modes_4lanes;
}

static inline int imx290_modes_num(const struct imx290 *imx290)
{
	if (imx290->nlanes == 2)
		return ARRAY_SIZE(imx290_modes_2lanes);
	else
		return ARRAY_SIZE(imx290_modes_4lanes);
}

struct imx290_format_info {
	u32 code[IMX290_VARIANT_MAX];
	u8 bpp;
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

static const struct imx290_format_info imx290_formats[] = {
	{
		.code = {
			[IMX290_VARIANT_COLOUR] = MEDIA_BUS_FMT_SRGGB10_1X10,
			[IMX290_VARIANT_MONO] = MEDIA_BUS_FMT_Y10_1X10
		},
		.bpp = 10,
		.regs = imx290_10bit_settings,
		.num_regs = ARRAY_SIZE(imx290_10bit_settings),
	}, {
		.code = {
			[IMX290_VARIANT_COLOUR] = MEDIA_BUS_FMT_SRGGB12_1X12,
			[IMX290_VARIANT_MONO] = MEDIA_BUS_FMT_Y12_1X12
		},
		.bpp = 12,
		.regs = imx290_12bit_settings,
		.num_regs = ARRAY_SIZE(imx290_12bit_settings),
	}
};

static const struct imx290_format_info *
imx290_format_info(const struct imx290 *imx290, u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx290_formats); ++i) {
		const struct imx290_format_info *info = &imx290_formats[i];

		if (info->code[imx290->model->colour_variant] == code)
			return info;
	}

	return NULL;
}

static int imx290_set_register_array(struct imx290 *imx290,
				     const struct cci_reg_sequence *settings,
				     unsigned int num_settings)
{
	int ret;

	ret = cci_multi_reg_write(imx290->regmap, settings, num_settings, NULL);
	if (ret < 0)
		return ret;

	/* Provide 10ms settle time */
	usleep_range(10000, 11000);

	return 0;
}

static int imx290_set_clock(struct imx290 *imx290)
{
	const struct imx290_mode *mode = imx290->current_mode;
	enum imx290_clk_freq clk_idx = imx290->xclk_idx;
	const struct imx290_clk_cfg *clk_cfg = &mode->clk_cfg[clk_idx];
	int ret;

	ret = imx290_set_register_array(imx290, xclk_regs[clk_idx],
					IMX290_NUM_CLK_REGS);

	cci_write(imx290->regmap, IMX290_INCKSEL1, clk_cfg->incksel1, &ret);
	cci_write(imx290->regmap, IMX290_INCKSEL2, clk_cfg->incksel2, &ret);
	cci_write(imx290->regmap, IMX290_INCKSEL3, clk_cfg->incksel3, &ret);
	cci_write(imx290->regmap, IMX290_INCKSEL4, clk_cfg->incksel4, &ret);
	cci_write(imx290->regmap, IMX290_INCKSEL5, clk_cfg->incksel5, &ret);
	cci_write(imx290->regmap, IMX290_INCKSEL6, clk_cfg->incksel6, &ret);

	return ret;
}

static int imx290_set_data_lanes(struct imx290 *imx290)
{
	int ret = 0;

	cci_write(imx290->regmap, IMX290_PHY_LANE_NUM, imx290->nlanes - 1,
		  &ret);
	cci_write(imx290->regmap, IMX290_CSI_LANE_MODE, imx290->nlanes - 1,
		  &ret);
	cci_write(imx290->regmap, IMX290_FR_FDG_SEL, 0x01, &ret);

	return ret;
}

static int imx290_set_black_level(struct imx290 *imx290,
				  const struct v4l2_mbus_framefmt *format,
				  unsigned int black_level, int *err)
{
	unsigned int bpp = imx290_format_info(imx290, format->code)->bpp;

	return cci_write(imx290->regmap, IMX290_BLKLEVEL,
			 black_level >> (16 - bpp), err);
}

static int imx290_set_csi_config(struct imx290 *imx290)
{
	const s64 *link_freqs = imx290_link_freqs_ptr(imx290);
	const struct imx290_csi_cfg *csi_cfg;
	int ret = 0;

	switch (link_freqs[imx290->current_mode->link_freq_index]) {
	case 445500000:
		csi_cfg = &imx290_csi_445_5mhz;
		break;
	case 297000000:
		csi_cfg = &imx290_csi_297mhz;
		break;
	case 222750000:
		csi_cfg = &imx290_csi_222_75mhz;
		break;
	case 148500000:
		csi_cfg = &imx290_csi_148_5mhz;
		break;
	default:
		return -EINVAL;
	}

	cci_write(imx290->regmap, IMX290_REPETITION, csi_cfg->repetition, &ret);
	cci_write(imx290->regmap, IMX290_TCLKPOST, csi_cfg->tclkpost, &ret);
	cci_write(imx290->regmap, IMX290_THSZERO, csi_cfg->thszero, &ret);
	cci_write(imx290->regmap, IMX290_THSPREPARE, csi_cfg->thsprepare, &ret);
	cci_write(imx290->regmap, IMX290_TCLKTRAIL, csi_cfg->tclktrail, &ret);
	cci_write(imx290->regmap, IMX290_THSTRAIL, csi_cfg->thstrail, &ret);
	cci_write(imx290->regmap, IMX290_TCLKZERO, csi_cfg->tclkzero, &ret);
	cci_write(imx290->regmap, IMX290_TCLKPREPARE, csi_cfg->tclkprepare,
		  &ret);
	cci_write(imx290->regmap, IMX290_TLPX, csi_cfg->tlpx, &ret);

	return ret;
}

static int imx290_setup_format(struct imx290 *imx290,
			       const struct v4l2_mbus_framefmt *format)
{
	const struct imx290_format_info *info;
	int ret;

	info = imx290_format_info(imx290, format->code);

	ret = imx290_set_register_array(imx290, info->regs, info->num_regs);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set format registers\n");
		return ret;
	}

	return imx290_set_black_level(imx290, format,
				      IMX290_BLACK_LEVEL_DEFAULT, &ret);
}

/* ----------------------------------------------------------------------------
 * Controls
 */
static void imx290_exposure_update(struct imx290 *imx290,
				   const struct imx290_mode *mode)
{
	unsigned int exposure_max;

	exposure_max = imx290->vblank->val + mode->height -
		       IMX290_EXPOSURE_OFFSET;
	__v4l2_ctrl_modify_range(imx290->exposure, 1, exposure_max, 1,
				 exposure_max);
}

static int imx290_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx290 *imx290 = container_of(ctrl->handler,
					     struct imx290, ctrls);
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	int ret = 0, vmax;

	/*
	 * Return immediately for controls that don't need to be applied to the
	 * device.
	 */
	if (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY)
		return 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Changing vblank changes the allowed range for exposure. */
		imx290_exposure_update(imx290, imx290->current_mode);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(imx290->dev))
		return 0;

	state = v4l2_subdev_get_locked_active_state(&imx290->sd);
	format = v4l2_subdev_state_get_format(state, 0);

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(imx290->regmap, IMX290_GAIN, ctrl->val, NULL);
		break;

	case V4L2_CID_VBLANK:
		ret = cci_write(imx290->regmap, IMX290_VMAX,
				ctrl->val + imx290->current_mode->height, NULL);
		/*
		 * Due to the way that exposure is programmed in this sensor in
		 * relation to VMAX, we have to reprogramme it whenever VMAX is
		 * changed.
		 * Update ctrl so that the V4L2_CID_EXPOSURE case can refer to
		 * it.
		 */
		ctrl = imx290->exposure;
		fallthrough;
	case V4L2_CID_EXPOSURE:
		vmax = imx290->vblank->val + imx290->current_mode->height;
		ret = cci_write(imx290->regmap, IMX290_SHS1,
				vmax - ctrl->val - 1, NULL);
		break;

	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val) {
			imx290_set_black_level(imx290, format, 0, &ret);
			usleep_range(10000, 11000);
			cci_write(imx290->regmap, IMX290_PGCTRL,
				  (u8)(IMX290_PGCTRL_REGEN |
				       IMX290_PGCTRL_THRU |
				       IMX290_PGCTRL_MODE(ctrl->val)), &ret);
		} else {
			cci_write(imx290->regmap, IMX290_PGCTRL, 0x00, &ret);
			usleep_range(10000, 11000);
			imx290_set_black_level(imx290, format,
					       IMX290_BLACK_LEVEL_DEFAULT, &ret);
		}
		break;

	case V4L2_CID_HBLANK:
		ret = cci_write(imx290->regmap, IMX290_HMAX,
				ctrl->val + imx290->current_mode->width, NULL);
		break;

	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
	{
		u32 reg;

		reg = imx290->current_mode->ctrl_07;
		if (imx290->hflip->val)
			reg |= IMX290_HREVERSE;
		if (imx290->vflip->val)
			reg |= IMX290_VREVERSE;
		ret = cci_write(imx290->regmap, IMX290_CTRL_07, reg, NULL);
		break;
	}

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(imx290->dev);
	pm_runtime_put_autosuspend(imx290->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx290_ctrl_ops = {
	.s_ctrl = imx290_set_ctrl,
};

static const char * const imx290_test_pattern_menu[] = {
	"Disabled",
	"Sequence Pattern 1",
	"Horizontal Color-bar Chart",
	"Vertical Color-bar Chart",
	"Sequence Pattern 2",
	"Gradation Pattern 1",
	"Gradation Pattern 2",
	"000/555h Toggle Pattern",
};

static void imx290_ctrl_update(struct imx290 *imx290,
			       const struct imx290_mode *mode)
{
	unsigned int hblank_min = mode->hmax_min - mode->width;
	unsigned int hblank_max = IMX290_HMAX_MAX - mode->width;
	unsigned int vblank_min = mode->vmax_min - mode->height;
	unsigned int vblank_max = IMX290_VMAX_MAX - mode->height;

	__v4l2_ctrl_s_ctrl(imx290->link_freq, mode->link_freq_index);

	__v4l2_ctrl_modify_range(imx290->hblank, hblank_min, hblank_max, 1,
				 hblank_min);
	__v4l2_ctrl_modify_range(imx290->vblank, vblank_min, vblank_max, 1,
				 vblank_min);
}

static int imx290_ctrl_init(struct imx290 *imx290)
{
	struct v4l2_fwnode_device_properties props;
	int ret;

	ret = v4l2_fwnode_device_parse(imx290->dev, &props);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&imx290->ctrls, 11);

	/*
	 * The sensor has an analog gain and a digital gain, both controlled
	 * through a single gain value, expressed in 0.3dB increments. Values
	 * from 0.0dB (0) to 30.0dB (100) apply analog gain only, higher values
	 * up to 72.0dB (240) add further digital gain. Limit the range to
	 * analog gain only, support for digital gain can be added separately
	 * if needed.
	 */
	v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, 0,
			  imx290->model->max_analog_gain, 1, 0);

	/*
	 * Correct range will be determined through imx290_ctrl_update setting
	 * V4L2_CID_VBLANK.
	 */
	imx290->exposure = v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
					     V4L2_CID_EXPOSURE, 1, 65535, 1,
					     65535);

	/*
	 * Set the link frequency, pixel rate, horizontal blanking and vertical
	 * blanking to hardcoded values, they will be updated by
	 * imx290_ctrl_update().
	 */
	imx290->link_freq =
		v4l2_ctrl_new_int_menu(&imx290->ctrls, &imx290_ctrl_ops,
				       V4L2_CID_LINK_FREQ,
				       imx290_link_freqs_num(imx290) - 1, 0,
				       imx290_link_freqs_ptr(imx290));
	if (imx290->link_freq)
		imx290->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  IMX290_PIXEL_RATE, IMX290_PIXEL_RATE, 1,
			  IMX290_PIXEL_RATE);

	v4l2_ctrl_new_std_menu_items(&imx290->ctrls, &imx290_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx290_test_pattern_menu) - 1,
				     0, 0, imx290_test_pattern_menu);

	/*
	 * Actual range will be set from imx290_ctrl_update later in the probe.
	 */
	imx290->hblank = v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
					   V4L2_CID_HBLANK, 1, 1, 1, 1);

	imx290->vblank = v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
					   V4L2_CID_VBLANK, 1, 1, 1, 1);

	imx290->hflip = v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	imx290->vflip = v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_cluster(2, &imx290->hflip);

	v4l2_ctrl_new_fwnode_properties(&imx290->ctrls, &imx290_ctrl_ops,
					&props);

	imx290->sd.ctrl_handler = &imx290->ctrls;

	if (imx290->ctrls.error) {
		ret = imx290->ctrls.error;
		v4l2_ctrl_handler_free(&imx290->ctrls);
		return ret;
	}

	return 0;
}

/* ----------------------------------------------------------------------------
 * Subdev operations
 */

/* Start streaming */
static int imx290_start_streaming(struct imx290 *imx290,
				  struct v4l2_subdev_state *state)
{
	const struct v4l2_mbus_framefmt *format;
	int ret;

	/* Set init register settings */
	ret = imx290_set_register_array(imx290, imx290_global_init_settings,
					ARRAY_SIZE(imx290_global_init_settings));
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set init registers\n");
		return ret;
	}

	/* Set mdel specific init register settings */
	ret = imx290_set_register_array(imx290, imx290->model->init_regs,
					imx290->model->init_regs_num);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set model specific init registers\n");
		return ret;
	}

	/* Set clock parameters based on mode and xclk */
	ret = imx290_set_clock(imx290);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set clocks - %d\n", ret);
		return ret;
	}

	/* Set data lane count */
	ret = imx290_set_data_lanes(imx290);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set data lanes - %d\n", ret);
		return ret;
	}

	ret = imx290_set_csi_config(imx290);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set csi cfg - %d\n", ret);
		return ret;
	}

	/* Apply the register values related to current frame format */
	format = v4l2_subdev_state_get_format(state, 0);
	ret = imx290_setup_format(imx290, format);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set frame format - %d\n", ret);
		return ret;
	}

	/* Apply default values of current mode */
	ret = imx290_set_register_array(imx290, imx290->current_mode->data,
					imx290->current_mode->data_size);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set current mode - %d\n", ret);
		return ret;
	}

	/* Apply customized values from user */
	ret = __v4l2_ctrl_handler_setup(imx290->sd.ctrl_handler);
	if (ret) {
		dev_err(imx290->dev, "Could not sync v4l2 controls - %d\n", ret);
		return ret;
	}

	cci_write(imx290->regmap, IMX290_STANDBY, 0x00, &ret);

	msleep(30);

	/* Start streaming */
	return cci_write(imx290->regmap, IMX290_XMSTA, 0x00, &ret);
}

/* Stop streaming */
static int imx290_stop_streaming(struct imx290 *imx290)
{
	int ret = 0;

	cci_write(imx290->regmap, IMX290_STANDBY, 0x01, &ret);

	msleep(30);

	return cci_write(imx290->regmap, IMX290_XMSTA, 0x01, &ret);
}

static int imx290_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx290 *imx290 = to_imx290(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = pm_runtime_resume_and_get(imx290->dev);
		if (ret < 0)
			goto unlock;

		ret = imx290_start_streaming(imx290, state);
		if (ret) {
			dev_err(imx290->dev, "Start stream failed\n");
			pm_runtime_put_sync(imx290->dev);
			goto unlock;
		}
	} else {
		imx290_stop_streaming(imx290);
		pm_runtime_mark_last_busy(imx290->dev);
		pm_runtime_put_autosuspend(imx290->dev);
	}

	/*
	 * vflip and hflip should not be changed during streaming as the sensor
	 * will produce an invalid frame.
	 */
	__v4l2_ctrl_grab(imx290->vflip, enable);
	__v4l2_ctrl_grab(imx290->hflip, enable);

unlock:
	v4l2_subdev_unlock_state(state);
	return ret;
}

static int imx290_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	const struct imx290 *imx290 = to_imx290(sd);

	if (code->index >= ARRAY_SIZE(imx290_formats))
		return -EINVAL;

	code->code = imx290_formats[code->index].code[imx290->model->colour_variant];

	return 0;
}

static int imx290_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	const struct imx290 *imx290 = to_imx290(sd);
	const struct imx290_mode *imx290_modes = imx290_modes_ptr(imx290);

	if (!imx290_format_info(imx290, fse->code))
		return -EINVAL;

	if (fse->index >= imx290_modes_num(imx290))
		return -EINVAL;

	fse->min_width = imx290_modes[fse->index].width;
	fse->max_width = imx290_modes[fse->index].width;
	fse->min_height = imx290_modes[fse->index].height;
	fse->max_height = imx290_modes[fse->index].height;

	return 0;
}

static int imx290_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct imx290 *imx290 = to_imx290(sd);
	const struct imx290_mode *mode;
	struct v4l2_mbus_framefmt *format;

	mode = v4l2_find_nearest_size(imx290_modes_ptr(imx290),
				      imx290_modes_num(imx290), width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.width = mode->width;
	fmt->format.height = mode->height;

	if (!imx290_format_info(imx290, fmt->format.code))
		fmt->format.code = imx290_formats[0].code[imx290->model->colour_variant];

	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->format.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;

	format = v4l2_subdev_state_get_format(sd_state, 0);

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		imx290->current_mode = mode;

		imx290_ctrl_update(imx290, mode);
		imx290_exposure_update(imx290, mode);
	}

	*format = fmt->format;

	return 0;
}

static int imx290_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx290 *imx290 = to_imx290(sd);
	struct v4l2_mbus_framefmt *format;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		format = v4l2_subdev_state_get_format(sd_state, 0);

		/*
		 * The sensor moves the readout by 1 pixel based on flips to
		 * keep the Bayer order the same.
		 */
		sel->r.top = IMX290_PIXEL_ARRAY_MARGIN_TOP
			   + (IMX290_PIXEL_ARRAY_RECORDING_HEIGHT - format->height) / 2
			   + imx290->vflip->val;
		sel->r.left = IMX290_PIXEL_ARRAY_MARGIN_LEFT
			    + (IMX290_PIXEL_ARRAY_RECORDING_WIDTH - format->width) / 2
			    + imx290->hflip->val;
		sel->r.width = format->width;
		sel->r.height = format->height;

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = IMX290_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX290_PIXEL_ARRAY_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = IMX290_PIXEL_ARRAY_MARGIN_TOP;
		sel->r.left = IMX290_PIXEL_ARRAY_MARGIN_LEFT;
		sel->r.width = IMX290_PIXEL_ARRAY_RECORDING_WIDTH;
		sel->r.height = IMX290_PIXEL_ARRAY_RECORDING_HEIGHT;

		return 0;

	default:
		return -EINVAL;
	}
}

static int imx290_entity_init_state(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = 1920,
			.height = 1080,
		},
	};

	imx290_set_fmt(subdev, sd_state, &fmt);

	return 0;
}

static const struct v4l2_subdev_video_ops imx290_video_ops = {
	.s_stream = imx290_set_stream,
};

static const struct v4l2_subdev_pad_ops imx290_pad_ops = {
	.enum_mbus_code = imx290_enum_mbus_code,
	.enum_frame_size = imx290_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx290_set_fmt,
	.get_selection = imx290_get_selection,
};

static const struct v4l2_subdev_ops imx290_subdev_ops = {
	.video = &imx290_video_ops,
	.pad = &imx290_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx290_internal_ops = {
	.init_state = imx290_entity_init_state,
};

static const struct media_entity_operations imx290_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int imx290_subdev_init(struct imx290 *imx290)
{
	struct i2c_client *client = to_i2c_client(imx290->dev);
	struct v4l2_subdev_state *state;
	int ret;

	imx290->current_mode = &imx290_modes_ptr(imx290)[0];

	/*
	 * After linking the subdev with the imx290 instance, we are allowed to
	 * use the pm_runtime functions. Decrease the PM usage count. The device
	 * will get suspended after the autosuspend delay, turning the power
	 * off. However, the communication happening in imx290_ctrl_update()
	 * will already be prevented even before the delay.
	 */
	v4l2_i2c_subdev_init(&imx290->sd, client, &imx290_subdev_ops);
	imx290->sd.dev = imx290->dev;
	pm_runtime_mark_last_busy(imx290->dev);
	pm_runtime_put_autosuspend(imx290->dev);

	imx290->sd.internal_ops = &imx290_internal_ops;
	imx290->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx290->sd.entity.ops = &imx290_subdev_entity_ops;
	imx290->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx290->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx290->sd.entity, 1, &imx290->pad);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not register media entity\n");
		return ret;
	}

	ret = imx290_ctrl_init(imx290);
	if (ret < 0) {
		dev_err(imx290->dev, "Control initialization error %d\n", ret);
		goto err_media;
	}

	imx290->sd.state_lock = imx290->ctrls.lock;

	ret = v4l2_subdev_init_finalize(&imx290->sd);
	if (ret < 0) {
		dev_err(imx290->dev, "subdev initialization error %d\n", ret);
		goto err_ctrls;
	}

	state = v4l2_subdev_lock_and_get_active_state(&imx290->sd);
	imx290_ctrl_update(imx290, imx290->current_mode);
	v4l2_subdev_unlock_state(state);

	return 0;

err_ctrls:
	v4l2_ctrl_handler_free(&imx290->ctrls);
err_media:
	media_entity_cleanup(&imx290->sd.entity);
	return ret;
}

static void imx290_subdev_cleanup(struct imx290 *imx290)
{
	v4l2_subdev_cleanup(&imx290->sd);
	media_entity_cleanup(&imx290->sd.entity);
	v4l2_ctrl_handler_free(&imx290->ctrls);
}

/* ----------------------------------------------------------------------------
 * Power management
 */

static int imx290_power_on(struct imx290 *imx290)
{
	int ret;

	ret = clk_prepare_enable(imx290->xclk);
	if (ret) {
		dev_err(imx290->dev, "Failed to enable clock\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(imx290->supplies),
				    imx290->supplies);
	if (ret) {
		dev_err(imx290->dev, "Failed to enable regulators\n");
		clk_disable_unprepare(imx290->xclk);
		return ret;
	}

	usleep_range(1, 2);
	gpiod_set_value_cansleep(imx290->rst_gpio, 0);
	usleep_range(30000, 31000);

	return 0;
}

static void imx290_power_off(struct imx290 *imx290)
{
	clk_disable_unprepare(imx290->xclk);
	gpiod_set_value_cansleep(imx290->rst_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx290->supplies), imx290->supplies);
}

static int imx290_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx290 *imx290 = to_imx290(sd);

	return imx290_power_on(imx290);
}

static int imx290_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx290 *imx290 = to_imx290(sd);

	imx290_power_off(imx290);

	return 0;
}

static const struct dev_pm_ops imx290_pm_ops = {
	RUNTIME_PM_OPS(imx290_runtime_suspend, imx290_runtime_resume, NULL)
};

/* ----------------------------------------------------------------------------
 * Probe & remove
 */

static const char * const imx290_supply_name[IMX290_NUM_SUPPLIES] = {
	"vdda",
	"vddd",
	"vdddo",
};

static int imx290_get_regulators(struct device *dev, struct imx290 *imx290)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx290->supplies); i++)
		imx290->supplies[i].supply = imx290_supply_name[i];

	return devm_regulator_bulk_get(dev, ARRAY_SIZE(imx290->supplies),
				       imx290->supplies);
}

static int imx290_init_clk(struct imx290 *imx290)
{
	u32 xclk_freq;
	int ret;

	ret = device_property_read_u32(imx290->dev, "clock-frequency",
				       &xclk_freq);
	if (ret) {
		dev_err(imx290->dev, "Could not get xclk frequency\n");
		return ret;
	}

	/* external clock must be 37.125 MHz or 74.25MHz */
	switch (xclk_freq) {
	case 37125000:
		imx290->xclk_idx = IMX290_CLK_37_125;
		break;
	case 74250000:
		imx290->xclk_idx = IMX290_CLK_74_25;
		break;
	default:
		dev_err(imx290->dev, "External clock frequency %u is not supported\n",
			xclk_freq);
		return -EINVAL;
	}

	ret = clk_set_rate(imx290->xclk, xclk_freq);
	if (ret) {
		dev_err(imx290->dev, "Could not set xclk frequency\n");
		return ret;
	}

	return 0;
}

/*
 * Returns 0 if all link frequencies used by the driver for the given number
 * of MIPI data lanes are mentioned in the device tree, or the value of the
 * first missing frequency otherwise.
 */
static s64 imx290_check_link_freqs(const struct imx290 *imx290,
				   const struct v4l2_fwnode_endpoint *ep)
{
	int i, j;
	const s64 *freqs = imx290_link_freqs_ptr(imx290);
	int freqs_count = imx290_link_freqs_num(imx290);

	for (i = 0; i < freqs_count; i++) {
		for (j = 0; j < ep->nr_of_link_frequencies; j++)
			if (freqs[i] == ep->link_frequencies[j])
				break;
		if (j == ep->nr_of_link_frequencies)
			return freqs[i];
	}
	return 0;
}

static const struct imx290_model_info imx290_models[] = {
	[IMX290_MODEL_IMX290LQR] = {
		.colour_variant = IMX290_VARIANT_COLOUR,
		.init_regs = imx290_global_init_settings_290,
		.init_regs_num = ARRAY_SIZE(imx290_global_init_settings_290),
		.max_analog_gain = 100,
		.name = "imx290",
	},
	[IMX290_MODEL_IMX290LLR] = {
		.colour_variant = IMX290_VARIANT_MONO,
		.init_regs = imx290_global_init_settings_290,
		.init_regs_num = ARRAY_SIZE(imx290_global_init_settings_290),
		.max_analog_gain = 100,
		.name = "imx290",
	},
	[IMX290_MODEL_IMX327LQR] = {
		.colour_variant = IMX290_VARIANT_COLOUR,
		.init_regs = imx290_global_init_settings_327,
		.init_regs_num = ARRAY_SIZE(imx290_global_init_settings_327),
		.max_analog_gain = 98,
		.name = "imx327",
	},
	[IMX290_MODEL_IMX462LQR] = {
		.colour_variant = IMX290_VARIANT_COLOUR,
		.init_regs = imx290_global_init_settings_462,
		.init_regs_num = ARRAY_SIZE(imx290_global_init_settings_462),
		.max_analog_gain = 98,
		.name = "imx462",
	},
	[IMX290_MODEL_IMX462LLR] = {
		.colour_variant = IMX290_VARIANT_MONO,
		.init_regs = imx290_global_init_settings_462,
		.init_regs_num = ARRAY_SIZE(imx290_global_init_settings_462),
		.max_analog_gain = 98,
		.name = "imx462",
	},
};

static int imx290_parse_dt(struct imx290 *imx290)
{
	/* Only CSI2 is supported for now: */
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *endpoint;
	int ret;
	s64 fq;

	imx290->model = of_device_get_match_data(imx290->dev);

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(imx290->dev), NULL);
	if (!endpoint) {
		dev_err(imx290->dev, "Endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep);
	fwnode_handle_put(endpoint);
	if (ret == -ENXIO) {
		dev_err(imx290->dev, "Unsupported bus type, should be CSI2\n");
		goto done;
	} else if (ret) {
		dev_err(imx290->dev, "Parsing endpoint node failed\n");
		goto done;
	}

	/* Get number of data lanes */
	imx290->nlanes = ep.bus.mipi_csi2.num_data_lanes;
	if (imx290->nlanes != 2 && imx290->nlanes != 4) {
		dev_err(imx290->dev, "Invalid data lanes: %d\n", imx290->nlanes);
		ret = -EINVAL;
		goto done;
	}

	dev_dbg(imx290->dev, "Using %u data lanes\n", imx290->nlanes);

	if (!ep.nr_of_link_frequencies) {
		dev_err(imx290->dev, "link-frequency property not found in DT\n");
		ret = -EINVAL;
		goto done;
	}

	/* Check that link frequences for all the modes are in device tree */
	fq = imx290_check_link_freqs(imx290, &ep);
	if (fq) {
		dev_err(imx290->dev, "Link frequency of %lld is not supported\n",
			fq);
		ret = -EINVAL;
		goto done;
	}

	ret = 0;

done:
	v4l2_fwnode_endpoint_free(&ep);
	return ret;
}

static int imx290_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx290 *imx290;
	int ret;

	imx290 = devm_kzalloc(dev, sizeof(*imx290), GFP_KERNEL);
	if (!imx290)
		return -ENOMEM;

	imx290->dev = dev;
	imx290->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx290->regmap)) {
		dev_err(dev, "Unable to initialize I2C\n");
		return -ENODEV;
	}

	ret = imx290_parse_dt(imx290);
	if (ret)
		return ret;

	/* Acquire resources. */
	imx290->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(imx290->xclk))
		return dev_err_probe(dev, PTR_ERR(imx290->xclk),
				     "Could not get xclk\n");

	ret = imx290_get_regulators(dev, imx290);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot get regulators\n");

	imx290->rst_gpio = devm_gpiod_get_optional(dev, "reset",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(imx290->rst_gpio))
		return dev_err_probe(dev, PTR_ERR(imx290->rst_gpio),
				     "Cannot get reset gpio\n");

	/* Initialize external clock frequency. */
	ret = imx290_init_clk(imx290);
	if (ret)
		return ret;

	/*
	 * Enable power management. The driver supports runtime PM, but needs to
	 * work when runtime PM is disabled in the kernel. To that end, power
	 * the sensor on manually here.
	 */
	ret = imx290_power_on(imx290);
	if (ret < 0) {
		dev_err(dev, "Could not power on the device\n");
		return ret;
	}

	/*
	 * Enable runtime PM with autosuspend. As the device has been powered
	 * manually, mark it as active, and increase the usage count without
	 * resuming the device.
	 */
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	/*
	 * Make sure the sensor is available, in STANDBY and not streaming
	 * before the V4L2 subdev is initialized.
	 */
	ret = imx290_stop_streaming(imx290);
	if (ret) {
		ret = dev_err_probe(dev, ret, "Could not initialize device\n");
		goto err_pm;
	}

	/* Initialize the V4L2 subdev. */
	ret = imx290_subdev_init(imx290);
	if (ret)
		goto err_pm;

	v4l2_i2c_subdev_set_name(&imx290->sd, client,
				 imx290->model->name, NULL);

	/*
	 * Finally, register the V4L2 subdev. This must be done after
	 * initializing everything as the subdev can be used immediately after
	 * being registered.
	 */
	ret = v4l2_async_register_subdev(&imx290->sd);
	if (ret < 0) {
		dev_err(dev, "Could not register v4l2 device\n");
		goto err_subdev;
	}

	return 0;

err_subdev:
	imx290_subdev_cleanup(imx290);
err_pm:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	imx290_power_off(imx290);
	return ret;
}

static void imx290_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx290 *imx290 = to_imx290(sd);

	v4l2_async_unregister_subdev(sd);
	imx290_subdev_cleanup(imx290);

	/*
	 * Disable runtime PM. In case runtime PM is disabled in the kernel,
	 * make sure to turn power off manually.
	 */
	pm_runtime_disable(imx290->dev);
	if (!pm_runtime_status_suspended(imx290->dev))
		imx290_power_off(imx290);
	pm_runtime_set_suspended(imx290->dev);
}

static const struct of_device_id imx290_of_match[] = {
	{
		/* Deprecated - synonym for "sony,imx290lqr" */
		.compatible = "sony,imx290",
		.data = &imx290_models[IMX290_MODEL_IMX290LQR],
	}, {
		.compatible = "sony,imx290lqr",
		.data = &imx290_models[IMX290_MODEL_IMX290LQR],
	}, {
		.compatible = "sony,imx290llr",
		.data = &imx290_models[IMX290_MODEL_IMX290LLR],
	}, {
		.compatible = "sony,imx327lqr",
		.data = &imx290_models[IMX290_MODEL_IMX327LQR],
	}, {
		.compatible = "sony,imx462lqr",
		.data = &imx290_models[IMX290_MODEL_IMX462LQR],
	}, {
		.compatible = "sony,imx462llr",
		.data = &imx290_models[IMX290_MODEL_IMX462LLR],
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx290_of_match);

static struct i2c_driver imx290_i2c_driver = {
	.probe = imx290_probe,
	.remove = imx290_remove,
	.driver = {
		.name = "imx290",
		.pm = pm_ptr(&imx290_pm_ops),
		.of_match_table = imx290_of_match,
	},
};

module_i2c_driver(imx290_i2c_driver);

MODULE_DESCRIPTION("Sony IMX290 CMOS Image Sensor Driver");
MODULE_AUTHOR("FRAMOS GmbH");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_LICENSE("GPL v2");
