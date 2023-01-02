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
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define IMX290_REG_SIZE_SHIFT				16
#define IMX290_REG_ADDR_MASK				0xffff
#define IMX290_REG_8BIT(n)				((1U << IMX290_REG_SIZE_SHIFT) | (n))
#define IMX290_REG_16BIT(n)				((2U << IMX290_REG_SIZE_SHIFT) | (n))
#define IMX290_REG_24BIT(n)				((3U << IMX290_REG_SIZE_SHIFT) | (n))

#define IMX290_STANDBY					IMX290_REG_8BIT(0x3000)
#define IMX290_REGHOLD					IMX290_REG_8BIT(0x3001)
#define IMX290_XMSTA					IMX290_REG_8BIT(0x3002)
#define IMX290_ADBIT					IMX290_REG_8BIT(0x3005)
#define IMX290_ADBIT_10BIT				(0 << 0)
#define IMX290_ADBIT_12BIT				(1 << 0)
#define IMX290_CTRL_07					IMX290_REG_8BIT(0x3007)
#define IMX290_VREVERSE					BIT(0)
#define IMX290_HREVERSE					BIT(1)
#define IMX290_WINMODE_1080P				(0 << 4)
#define IMX290_WINMODE_720P				(1 << 4)
#define IMX290_WINMODE_CROP				(4 << 4)
#define IMX290_FR_FDG_SEL				IMX290_REG_8BIT(0x3009)
#define IMX290_BLKLEVEL					IMX290_REG_16BIT(0x300a)
#define IMX290_GAIN					IMX290_REG_8BIT(0x3014)
#define IMX290_VMAX					IMX290_REG_24BIT(0x3018)
#define IMX290_HMAX					IMX290_REG_16BIT(0x301c)
#define IMX290_SHS1					IMX290_REG_24BIT(0x3020)
#define IMX290_WINWV_OB					IMX290_REG_8BIT(0x303a)
#define IMX290_WINPV					IMX290_REG_16BIT(0x303c)
#define IMX290_WINWV					IMX290_REG_16BIT(0x303e)
#define IMX290_WINPH					IMX290_REG_16BIT(0x3040)
#define IMX290_WINWH					IMX290_REG_16BIT(0x3042)
#define IMX290_OUT_CTRL					IMX290_REG_8BIT(0x3046)
#define IMX290_ODBIT_10BIT				(0 << 0)
#define IMX290_ODBIT_12BIT				(1 << 0)
#define IMX290_OPORTSEL_PARALLEL			(0x0 << 4)
#define IMX290_OPORTSEL_LVDS_2CH			(0xd << 4)
#define IMX290_OPORTSEL_LVDS_4CH			(0xe << 4)
#define IMX290_OPORTSEL_LVDS_8CH			(0xf << 4)
#define IMX290_XSOUTSEL					IMX290_REG_8BIT(0x304b)
#define IMX290_XSOUTSEL_XVSOUTSEL_HIGH			(0 << 0)
#define IMX290_XSOUTSEL_XVSOUTSEL_VSYNC			(2 << 0)
#define IMX290_XSOUTSEL_XHSOUTSEL_HIGH			(0 << 2)
#define IMX290_XSOUTSEL_XHSOUTSEL_HSYNC			(2 << 2)
#define IMX290_INCKSEL1					IMX290_REG_8BIT(0x305c)
#define IMX290_INCKSEL2					IMX290_REG_8BIT(0x305d)
#define IMX290_INCKSEL3					IMX290_REG_8BIT(0x305e)
#define IMX290_INCKSEL4					IMX290_REG_8BIT(0x305f)
#define IMX290_PGCTRL					IMX290_REG_8BIT(0x308c)
#define IMX290_ADBIT1					IMX290_REG_8BIT(0x3129)
#define IMX290_ADBIT1_10BIT				0x1d
#define IMX290_ADBIT1_12BIT				0x00
#define IMX290_INCKSEL5					IMX290_REG_8BIT(0x315e)
#define IMX290_INCKSEL6					IMX290_REG_8BIT(0x3164)
#define IMX290_ADBIT2					IMX290_REG_8BIT(0x317c)
#define IMX290_ADBIT2_10BIT				0x12
#define IMX290_ADBIT2_12BIT				0x00
#define IMX290_CHIP_ID					IMX290_REG_16BIT(0x319a)
#define IMX290_ADBIT3					IMX290_REG_8BIT(0x31ec)
#define IMX290_ADBIT3_10BIT				0x37
#define IMX290_ADBIT3_12BIT				0x0e
#define IMX290_REPETITION				IMX290_REG_8BIT(0x3405)
#define IMX290_PHY_LANE_NUM				IMX290_REG_8BIT(0x3407)
#define IMX290_OPB_SIZE_V				IMX290_REG_8BIT(0x3414)
#define IMX290_Y_OUT_SIZE				IMX290_REG_16BIT(0x3418)
#define IMX290_CSI_DT_FMT				IMX290_REG_16BIT(0x3441)
#define IMX290_CSI_DT_FMT_RAW10				0x0a0a
#define IMX290_CSI_DT_FMT_RAW12				0x0c0c
#define IMX290_CSI_LANE_MODE				IMX290_REG_8BIT(0x3443)
#define IMX290_EXTCK_FREQ				IMX290_REG_16BIT(0x3444)
#define IMX290_TCLKPOST					IMX290_REG_16BIT(0x3446)
#define IMX290_THSZERO					IMX290_REG_16BIT(0x3448)
#define IMX290_THSPREPARE				IMX290_REG_16BIT(0x344a)
#define IMX290_TCLKTRAIL				IMX290_REG_16BIT(0x344c)
#define IMX290_THSTRAIL					IMX290_REG_16BIT(0x344e)
#define IMX290_TCLKZERO					IMX290_REG_16BIT(0x3450)
#define IMX290_TCLKPREPARE				IMX290_REG_16BIT(0x3452)
#define IMX290_TLPX					IMX290_REG_16BIT(0x3454)
#define IMX290_X_OUT_SIZE				IMX290_REG_16BIT(0x3472)

#define IMX290_PGCTRL_REGEN				BIT(0)
#define IMX290_PGCTRL_THRU				BIT(1)
#define IMX290_PGCTRL_MODE(n)				((n) << 4)

#define IMX290_VMAX_DEFAULT				1125


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
#define IMX920_PIXEL_ARRAY_MARGIN_LEFT			12
#define IMX920_PIXEL_ARRAY_MARGIN_RIGHT			13
#define IMX920_PIXEL_ARRAY_MARGIN_TOP			8
#define IMX920_PIXEL_ARRAY_MARGIN_BOTTOM		9
#define IMX290_PIXEL_ARRAY_RECORDING_WIDTH		1920
#define IMX290_PIXEL_ARRAY_RECORDING_HEIGHT		1080

static const char * const imx290_supply_name[] = {
	"vdda",
	"vddd",
	"vdddo",
};

#define IMX290_NUM_SUPPLIES ARRAY_SIZE(imx290_supply_name)

struct imx290_regval {
	u32 reg;
	u32 val;
};

struct imx290_mode {
	u32 width;
	u32 height;
	u32 hmax;
	u8 link_freq_index;

	const struct imx290_regval *data;
	u32 data_size;
};

struct imx290 {
	struct device *dev;
	struct clk *xclk;
	struct regmap *regmap;
	u8 nlanes;
	u8 bpp;

	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt current_format;
	const struct imx290_mode *current_mode;

	struct regulator_bulk_data supplies[IMX290_NUM_SUPPLIES];
	struct gpio_desc *rst_gpio;

	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;

	struct mutex lock;
};

struct imx290_pixfmt {
	u32 code;
	u8 bpp;
};

static const struct imx290_pixfmt imx290_formats[] = {
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10 },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12 },
};

static const struct regmap_config imx290_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
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

static const struct imx290_regval imx290_global_init_settings[] = {
	{ IMX290_CTRL_07, IMX290_WINMODE_1080P },
	{ IMX290_VMAX, IMX290_VMAX_DEFAULT },
	{ IMX290_EXTCK_FREQ, 0x2520 },
	{ IMX290_WINWV_OB, 12 },
	{ IMX290_WINPH, 0 },
	{ IMX290_WINPV, 0 },
	{ IMX290_WINWH, 1948 },
	{ IMX290_WINWV, 1097 },
	{ IMX290_XSOUTSEL, IMX290_XSOUTSEL_XVSOUTSEL_VSYNC |
			   IMX290_XSOUTSEL_XHSOUTSEL_HSYNC },
	{ IMX290_REG_8BIT(0x300f), 0x00 },
	{ IMX290_REG_8BIT(0x3010), 0x21 },
	{ IMX290_REG_8BIT(0x3012), 0x64 },
	{ IMX290_REG_8BIT(0x3013), 0x00 },
	{ IMX290_REG_8BIT(0x3016), 0x09 },
	{ IMX290_REG_8BIT(0x3070), 0x02 },
	{ IMX290_REG_8BIT(0x3071), 0x11 },
	{ IMX290_REG_8BIT(0x309b), 0x10 },
	{ IMX290_REG_8BIT(0x309c), 0x22 },
	{ IMX290_REG_8BIT(0x30a2), 0x02 },
	{ IMX290_REG_8BIT(0x30a6), 0x20 },
	{ IMX290_REG_8BIT(0x30a8), 0x20 },
	{ IMX290_REG_8BIT(0x30aa), 0x20 },
	{ IMX290_REG_8BIT(0x30ac), 0x20 },
	{ IMX290_REG_8BIT(0x30b0), 0x43 },
	{ IMX290_REG_8BIT(0x3119), 0x9e },
	{ IMX290_REG_8BIT(0x311c), 0x1e },
	{ IMX290_REG_8BIT(0x311e), 0x08 },
	{ IMX290_REG_8BIT(0x3128), 0x05 },
	{ IMX290_REG_8BIT(0x313d), 0x83 },
	{ IMX290_REG_8BIT(0x3150), 0x03 },
	{ IMX290_REG_8BIT(0x317e), 0x00 },
	{ IMX290_REG_8BIT(0x32b8), 0x50 },
	{ IMX290_REG_8BIT(0x32b9), 0x10 },
	{ IMX290_REG_8BIT(0x32ba), 0x00 },
	{ IMX290_REG_8BIT(0x32bb), 0x04 },
	{ IMX290_REG_8BIT(0x32c8), 0x50 },
	{ IMX290_REG_8BIT(0x32c9), 0x10 },
	{ IMX290_REG_8BIT(0x32ca), 0x00 },
	{ IMX290_REG_8BIT(0x32cb), 0x04 },
	{ IMX290_REG_8BIT(0x332c), 0xd3 },
	{ IMX290_REG_8BIT(0x332d), 0x10 },
	{ IMX290_REG_8BIT(0x332e), 0x0d },
	{ IMX290_REG_8BIT(0x3358), 0x06 },
	{ IMX290_REG_8BIT(0x3359), 0xe1 },
	{ IMX290_REG_8BIT(0x335a), 0x11 },
	{ IMX290_REG_8BIT(0x3360), 0x1e },
	{ IMX290_REG_8BIT(0x3361), 0x61 },
	{ IMX290_REG_8BIT(0x3362), 0x10 },
	{ IMX290_REG_8BIT(0x33b0), 0x50 },
	{ IMX290_REG_8BIT(0x33b2), 0x1a },
	{ IMX290_REG_8BIT(0x33b3), 0x04 },
	{ IMX290_REG_8BIT(0x3480), 0x49 },
};

static const struct imx290_regval imx290_1080p_settings[] = {
	/* mode settings */
	{ IMX290_CTRL_07, IMX290_WINMODE_1080P },
	{ IMX290_WINWV_OB, 12 },
	{ IMX290_OPB_SIZE_V, 10 },
	{ IMX290_X_OUT_SIZE, 1920 },
	{ IMX290_Y_OUT_SIZE, 1080 },
	{ IMX290_INCKSEL1, 0x18 },
	{ IMX290_INCKSEL2, 0x03 },
	{ IMX290_INCKSEL3, 0x20 },
	{ IMX290_INCKSEL4, 0x01 },
	{ IMX290_INCKSEL5, 0x1a },
	{ IMX290_INCKSEL6, 0x1a },
	/* data rate settings */
	{ IMX290_REPETITION, 0x10 },
	{ IMX290_TCLKPOST, 87 },
	{ IMX290_THSZERO, 55 },
	{ IMX290_THSPREPARE, 31 },
	{ IMX290_TCLKTRAIL, 31 },
	{ IMX290_THSTRAIL, 31 },
	{ IMX290_TCLKZERO, 119 },
	{ IMX290_TCLKPREPARE, 31 },
	{ IMX290_TLPX, 23 },
};

static const struct imx290_regval imx290_720p_settings[] = {
	/* mode settings */
	{ IMX290_CTRL_07, IMX290_WINMODE_720P },
	{ IMX290_WINWV_OB, 6 },
	{ IMX290_OPB_SIZE_V, 4 },
	{ IMX290_X_OUT_SIZE, 1280 },
	{ IMX290_Y_OUT_SIZE, 720 },
	{ IMX290_INCKSEL1, 0x20 },
	{ IMX290_INCKSEL2, 0x00 },
	{ IMX290_INCKSEL3, 0x20 },
	{ IMX290_INCKSEL4, 0x01 },
	{ IMX290_INCKSEL5, 0x1a },
	{ IMX290_INCKSEL6, 0x1a },
	/* data rate settings */
	{ IMX290_REPETITION, 0x10 },
	{ IMX290_TCLKPOST, 79 },
	{ IMX290_THSZERO, 47 },
	{ IMX290_THSPREPARE, 23 },
	{ IMX290_TCLKTRAIL, 23 },
	{ IMX290_THSTRAIL, 23 },
	{ IMX290_TCLKZERO, 87 },
	{ IMX290_TCLKPREPARE, 23 },
	{ IMX290_TLPX, 23 },
};

static const struct imx290_regval imx290_10bit_settings[] = {
	{ IMX290_ADBIT, IMX290_ADBIT_10BIT },
	{ IMX290_OUT_CTRL, IMX290_ODBIT_10BIT },
	{ IMX290_ADBIT1, IMX290_ADBIT1_10BIT },
	{ IMX290_ADBIT2, IMX290_ADBIT2_10BIT },
	{ IMX290_ADBIT3, IMX290_ADBIT3_10BIT },
	{ IMX290_CSI_DT_FMT, IMX290_CSI_DT_FMT_RAW10 },
	{ IMX290_BLKLEVEL, 60 },
};

static const struct imx290_regval imx290_12bit_settings[] = {
	{ IMX290_ADBIT, IMX290_ADBIT_12BIT },
	{ IMX290_OUT_CTRL, IMX290_ODBIT_12BIT },
	{ IMX290_ADBIT1, IMX290_ADBIT1_12BIT },
	{ IMX290_ADBIT2, IMX290_ADBIT2_12BIT },
	{ IMX290_ADBIT3, IMX290_ADBIT3_12BIT },
	{ IMX290_CSI_DT_FMT, IMX290_CSI_DT_FMT_RAW12 },
	{ IMX290_BLKLEVEL, 240 },
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

/* Mode configs */
static const struct imx290_mode imx290_modes_2lanes[] = {
	{
		.width = 1920,
		.height = 1080,
		.hmax = 4400,
		.link_freq_index = FREQ_INDEX_1080P,
		.data = imx290_1080p_settings,
		.data_size = ARRAY_SIZE(imx290_1080p_settings),
	},
	{
		.width = 1280,
		.height = 720,
		.hmax = 6600,
		.link_freq_index = FREQ_INDEX_720P,
		.data = imx290_720p_settings,
		.data_size = ARRAY_SIZE(imx290_720p_settings),
	},
};

static const struct imx290_mode imx290_modes_4lanes[] = {
	{
		.width = 1920,
		.height = 1080,
		.hmax = 2200,
		.link_freq_index = FREQ_INDEX_1080P,
		.data = imx290_1080p_settings,
		.data_size = ARRAY_SIZE(imx290_1080p_settings),
	},
	{
		.width = 1280,
		.height = 720,
		.hmax = 3300,
		.link_freq_index = FREQ_INDEX_720P,
		.data = imx290_720p_settings,
		.data_size = ARRAY_SIZE(imx290_720p_settings),
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

static inline struct imx290 *to_imx290(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx290, sd);
}

static int __always_unused imx290_read(struct imx290 *imx290, u32 addr, u32 *value)
{
	u8 data[3] = { 0, 0, 0 };
	int ret;

	ret = regmap_raw_read(imx290->regmap, addr & IMX290_REG_ADDR_MASK,
			      data, (addr >> IMX290_REG_SIZE_SHIFT) & 3);
	if (ret < 0) {
		dev_err(imx290->dev, "%u-bit read from 0x%04x failed: %d\n",
			 ((addr >> IMX290_REG_SIZE_SHIFT) & 3) * 8,
			 addr & IMX290_REG_ADDR_MASK, ret);
		return ret;
	}

	*value = (data[2] << 16) | (data[1] << 8) | data[0];
	return 0;
}

static int imx290_write(struct imx290 *imx290, u32 addr, u32 value, int *err)
{
	u8 data[3] = { value & 0xff, (value >> 8) & 0xff, value >> 16 };
	int ret;

	if (err && *err)
		return *err;

	ret = regmap_raw_write(imx290->regmap, addr & IMX290_REG_ADDR_MASK,
			       data, (addr >> IMX290_REG_SIZE_SHIFT) & 3);
	if (ret < 0) {
		dev_err(imx290->dev, "%u-bit write to 0x%04x failed: %d\n",
			 ((addr >> IMX290_REG_SIZE_SHIFT) & 3) * 8,
			 addr & IMX290_REG_ADDR_MASK, ret);
		if (err)
			*err = ret;
	}

	return ret;
}

static int imx290_set_register_array(struct imx290 *imx290,
				     const struct imx290_regval *settings,
				     unsigned int num_settings)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num_settings; ++i, ++settings) {
		ret = imx290_write(imx290, settings->reg, settings->val, NULL);
		if (ret < 0)
			return ret;
	}

	/* Provide 10ms settle time */
	usleep_range(10000, 11000);

	return 0;
}

/* Stop streaming */
static int imx290_stop_streaming(struct imx290 *imx290)
{
	int ret = 0;

	imx290_write(imx290, IMX290_STANDBY, 0x01, &ret);

	msleep(30);

	return imx290_write(imx290, IMX290_XMSTA, 0x01, &ret);
}

static int imx290_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx290 *imx290 = container_of(ctrl->handler,
					     struct imx290, ctrls);
	int ret = 0;

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(imx290->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = imx290_write(imx290, IMX290_GAIN, ctrl->val, NULL);
		break;

	case V4L2_CID_EXPOSURE:
		ret = imx290_write(imx290, IMX290_SHS1,
				   IMX290_VMAX_DEFAULT - ctrl->val - 1, NULL);
		break;

	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val) {
			imx290_write(imx290, IMX290_BLKLEVEL, 0, &ret);
			usleep_range(10000, 11000);
			imx290_write(imx290, IMX290_PGCTRL,
				     (u8)(IMX290_PGCTRL_REGEN |
				     IMX290_PGCTRL_THRU |
				     IMX290_PGCTRL_MODE(ctrl->val)), &ret);
		} else {
			imx290_write(imx290, IMX290_PGCTRL, 0x00, &ret);
			usleep_range(10000, 11000);
			if (imx290->bpp == 10)
				imx290_write(imx290, IMX290_BLKLEVEL, 0x3c,
					     &ret);
			else /* 12 bits per pixel */
				imx290_write(imx290, IMX290_BLKLEVEL, 0xf0,
					     &ret);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(imx290->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx290_ctrl_ops = {
	.s_ctrl = imx290_set_ctrl,
};

static struct v4l2_mbus_framefmt *
imx290_get_pad_format(struct imx290 *imx290, struct v4l2_subdev_state *state,
		      u32 which)
{
	if (which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return &imx290->current_format;
	else
		return v4l2_subdev_get_try_format(&imx290->sd, state, 0);
}

static int imx290_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(imx290_formats))
		return -EINVAL;

	code->code = imx290_formats[code->index].code;

	return 0;
}

static int imx290_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	const struct imx290 *imx290 = to_imx290(sd);
	const struct imx290_mode *imx290_modes = imx290_modes_ptr(imx290);

	if ((fse->code != imx290_formats[0].code) &&
	    (fse->code != imx290_formats[1].code))
		return -EINVAL;

	if (fse->index >= imx290_modes_num(imx290))
		return -EINVAL;

	fse->min_width = imx290_modes[fse->index].width;
	fse->max_width = imx290_modes[fse->index].width;
	fse->min_height = imx290_modes[fse->index].height;
	fse->max_height = imx290_modes[fse->index].height;

	return 0;
}

static int imx290_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct imx290 *imx290 = to_imx290(sd);
	struct v4l2_mbus_framefmt *framefmt;

	mutex_lock(&imx290->lock);

	framefmt = imx290_get_pad_format(imx290, sd_state, fmt->which);
	fmt->format = *framefmt;

	mutex_unlock(&imx290->lock);

	return 0;
}

static inline u8 imx290_get_link_freq_index(struct imx290 *imx290)
{
	return imx290->current_mode->link_freq_index;
}

static s64 imx290_get_link_freq(struct imx290 *imx290)
{
	u8 index = imx290_get_link_freq_index(imx290);

	return *(imx290_link_freqs_ptr(imx290) + index);
}

static u64 imx290_calc_pixel_rate(struct imx290 *imx290)
{
	s64 link_freq = imx290_get_link_freq(imx290);
	u8 nlanes = imx290->nlanes;
	u64 pixel_rate;

	/* pixel rate = link_freq * 2 * nr_of_lanes / bits_per_sample */
	pixel_rate = link_freq * 2 * nlanes;
	do_div(pixel_rate, imx290->bpp);
	return pixel_rate;
}

static int imx290_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct imx290 *imx290 = to_imx290(sd);
	const struct imx290_mode *mode;
	struct v4l2_mbus_framefmt *format;
	unsigned int i;

	mutex_lock(&imx290->lock);

	mode = v4l2_find_nearest_size(imx290_modes_ptr(imx290),
				      imx290_modes_num(imx290), width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.width = mode->width;
	fmt->format.height = mode->height;

	for (i = 0; i < ARRAY_SIZE(imx290_formats); i++)
		if (imx290_formats[i].code == fmt->format.code)
			break;

	if (i >= ARRAY_SIZE(imx290_formats))
		i = 0;

	fmt->format.code = imx290_formats[i].code;
	fmt->format.field = V4L2_FIELD_NONE;

	format = imx290_get_pad_format(imx290, sd_state, fmt->which);

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		imx290->current_mode = mode;
		imx290->bpp = imx290_formats[i].bpp;

		if (imx290->link_freq)
			__v4l2_ctrl_s_ctrl(imx290->link_freq,
					   imx290_get_link_freq_index(imx290));
		if (imx290->pixel_rate)
			__v4l2_ctrl_s_ctrl_int64(imx290->pixel_rate,
						 imx290_calc_pixel_rate(imx290));

		if (imx290->hblank) {
			unsigned int hblank = mode->hmax - mode->width;

			__v4l2_ctrl_modify_range(imx290->hblank, hblank, hblank,
						 1, hblank);
		}

		if (imx290->vblank) {
			unsigned int vblank = IMX290_VMAX_DEFAULT - mode->height;

			__v4l2_ctrl_modify_range(imx290->vblank, vblank, vblank,
						 1, vblank);
		}
	}

	*format = fmt->format;

	mutex_unlock(&imx290->lock);

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
		format = imx290_get_pad_format(imx290, sd_state, sel->which);

		mutex_lock(&imx290->lock);

		sel->r.top = IMX920_PIXEL_ARRAY_MARGIN_TOP
			   + (IMX290_PIXEL_ARRAY_RECORDING_HEIGHT - format->height) / 2;
		sel->r.left = IMX920_PIXEL_ARRAY_MARGIN_LEFT
			    + (IMX290_PIXEL_ARRAY_RECORDING_WIDTH - format->width) / 2;
		sel->r.width = format->width;
		sel->r.height = format->height;

		mutex_unlock(&imx290->lock);
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
		sel->r.top = IMX920_PIXEL_ARRAY_MARGIN_TOP;
		sel->r.left = IMX920_PIXEL_ARRAY_MARGIN_LEFT;
		sel->r.width = IMX290_PIXEL_ARRAY_RECORDING_WIDTH;
		sel->r.height = IMX290_PIXEL_ARRAY_RECORDING_HEIGHT;

		return 0;

	default:
		return -EINVAL;
	}
}

static int imx290_entity_init_cfg(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.format.width = 1920;
	fmt.format.height = 1080;

	imx290_set_fmt(subdev, sd_state, &fmt);

	return 0;
}

static int imx290_write_current_format(struct imx290 *imx290)
{
	int ret;

	switch (imx290->current_format.code) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		ret = imx290_set_register_array(imx290, imx290_10bit_settings,
						ARRAY_SIZE(
							imx290_10bit_settings));
		if (ret < 0) {
			dev_err(imx290->dev, "Could not set format registers\n");
			return ret;
		}
		break;
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		ret = imx290_set_register_array(imx290, imx290_12bit_settings,
						ARRAY_SIZE(
							imx290_12bit_settings));
		if (ret < 0) {
			dev_err(imx290->dev, "Could not set format registers\n");
			return ret;
		}
		break;
	default:
		dev_err(imx290->dev, "Unknown pixel format\n");
		return -EINVAL;
	}

	return 0;
}

/* Start streaming */
static int imx290_start_streaming(struct imx290 *imx290)
{
	int ret;

	/* Set init register settings */
	ret = imx290_set_register_array(imx290, imx290_global_init_settings,
					ARRAY_SIZE(
						imx290_global_init_settings));
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set init registers\n");
		return ret;
	}

	/* Apply the register values related to current frame format */
	ret = imx290_write_current_format(imx290);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set frame format\n");
		return ret;
	}

	/* Apply default values of current mode */
	ret = imx290_set_register_array(imx290, imx290->current_mode->data,
					imx290->current_mode->data_size);
	if (ret < 0) {
		dev_err(imx290->dev, "Could not set current mode\n");
		return ret;
	}

	ret = imx290_write(imx290, IMX290_HMAX, imx290->current_mode->hmax,
			   NULL);
	if (ret)
		return ret;

	/* Apply customized values from user */
	ret = v4l2_ctrl_handler_setup(imx290->sd.ctrl_handler);
	if (ret) {
		dev_err(imx290->dev, "Could not sync v4l2 controls\n");
		return ret;
	}

	imx290_write(imx290, IMX290_STANDBY, 0x00, &ret);

	msleep(30);

	/* Start streaming */
	return imx290_write(imx290, IMX290_XMSTA, 0x00, &ret);
}

static int imx290_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx290 *imx290 = to_imx290(sd);
	int ret = 0;

	if (enable) {
		ret = pm_runtime_resume_and_get(imx290->dev);
		if (ret < 0)
			goto unlock_and_return;

		ret = imx290_start_streaming(imx290);
		if (ret) {
			dev_err(imx290->dev, "Start stream failed\n");
			pm_runtime_put(imx290->dev);
			goto unlock_and_return;
		}
	} else {
		imx290_stop_streaming(imx290);
		pm_runtime_put(imx290->dev);
	}

unlock_and_return:

	return ret;
}

static int imx290_get_regulators(struct device *dev, struct imx290 *imx290)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx290->supplies); i++)
		imx290->supplies[i].supply = imx290_supply_name[i];

	return devm_regulator_bulk_get(dev, ARRAY_SIZE(imx290->supplies),
				       imx290->supplies);
}

static int imx290_set_data_lanes(struct imx290 *imx290)
{
	int ret = 0, laneval, frsel;

	switch (imx290->nlanes) {
	case 2:
		laneval = 0x01;
		frsel = 0x02;
		break;
	case 4:
		laneval = 0x03;
		frsel = 0x01;
		break;
	default:
		/*
		 * We should never hit this since the data lane count is
		 * validated in probe itself
		 */
		dev_err(imx290->dev, "Lane configuration not supported\n");
		return -EINVAL;
	}

	imx290_write(imx290, IMX290_PHY_LANE_NUM, laneval, &ret);
	imx290_write(imx290, IMX290_CSI_LANE_MODE, laneval, &ret);
	imx290_write(imx290, IMX290_FR_FDG_SEL, frsel, &ret);

	return ret;
}

static int imx290_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx290 *imx290 = to_imx290(sd);
	int ret;

	ret = clk_prepare_enable(imx290->xclk);
	if (ret) {
		dev_err(dev, "Failed to enable clock\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(imx290->supplies),
				    imx290->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable regulators\n");
		clk_disable_unprepare(imx290->xclk);
		return ret;
	}

	usleep_range(1, 2);
	gpiod_set_value_cansleep(imx290->rst_gpio, 0);
	usleep_range(30000, 31000);

	/* Set data lane count */
	imx290_set_data_lanes(imx290);

	return 0;
}

static int imx290_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx290 *imx290 = to_imx290(sd);

	clk_disable_unprepare(imx290->xclk);
	gpiod_set_value_cansleep(imx290->rst_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx290->supplies), imx290->supplies);

	return 0;
}

static const struct dev_pm_ops imx290_pm_ops = {
	SET_RUNTIME_PM_OPS(imx290_power_off, imx290_power_on, NULL)
};

static const struct v4l2_subdev_video_ops imx290_video_ops = {
	.s_stream = imx290_set_stream,
};

static const struct v4l2_subdev_pad_ops imx290_pad_ops = {
	.init_cfg = imx290_entity_init_cfg,
	.enum_mbus_code = imx290_enum_mbus_code,
	.enum_frame_size = imx290_enum_frame_size,
	.get_fmt = imx290_get_fmt,
	.set_fmt = imx290_set_fmt,
	.get_selection = imx290_get_selection,
};

static const struct v4l2_subdev_ops imx290_subdev_ops = {
	.video = &imx290_video_ops,
	.pad = &imx290_pad_ops,
};

static const struct media_entity_operations imx290_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int imx290_ctrl_init(struct imx290 *imx290)
{
	struct v4l2_fwnode_device_properties props;
	unsigned int blank;
	int ret;

	ret = v4l2_fwnode_device_parse(imx290->dev, &props);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&imx290->ctrls, 9);
	imx290->ctrls.lock = &imx290->lock;

	/*
	 * The sensor has an analog gain and a digital gain, both controlled
	 * through a single gain value, expressed in 0.3dB increments. Values
	 * from 0.0dB (0) to 30.0dB (100) apply analog gain only, higher values
	 * up to 72.0dB (240) add further digital gain. Limit the range to
	 * analog gain only, support for digital gain can be added separately
	 * if needed.
	 *
	 * The IMX327 and IMX462 are largely compatible with the IMX290, but
	 * have an analog gain range of 0.0dB to 29.4dB and 42dB of digital
	 * gain. When support for those sensors gets added to the driver, the
	 * gain control should be adjusted accordingly.
	 */
	v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, 0, 100, 1, 0);

	v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
			  V4L2_CID_EXPOSURE, 1, IMX290_VMAX_DEFAULT - 2, 1,
			  IMX290_VMAX_DEFAULT - 2);

	imx290->link_freq =
		v4l2_ctrl_new_int_menu(&imx290->ctrls, &imx290_ctrl_ops,
				       V4L2_CID_LINK_FREQ,
				       imx290_link_freqs_num(imx290) - 1, 0,
				       imx290_link_freqs_ptr(imx290));
	if (imx290->link_freq)
		imx290->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx290->pixel_rate = v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       1, INT_MAX, 1,
					       imx290_calc_pixel_rate(imx290));

	v4l2_ctrl_new_std_menu_items(&imx290->ctrls, &imx290_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx290_test_pattern_menu) - 1,
				     0, 0, imx290_test_pattern_menu);

	blank = imx290->current_mode->hmax - imx290->current_mode->width;
	imx290->hblank = v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
					   V4L2_CID_HBLANK, blank, blank, 1,
					   blank);
	if (imx290->hblank)
		imx290->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	blank = IMX290_VMAX_DEFAULT - imx290->current_mode->height;
	imx290->vblank = v4l2_ctrl_new_std(&imx290->ctrls, &imx290_ctrl_ops,
					   V4L2_CID_VBLANK, blank, blank, 1,
					   blank);
	if (imx290->vblank)
		imx290->vblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

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

static int imx290_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	/* Only CSI2 is supported for now: */
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct imx290 *imx290;
	u32 xclk_freq;
	s64 fq;
	int ret;

	imx290 = devm_kzalloc(dev, sizeof(*imx290), GFP_KERNEL);
	if (!imx290)
		return -ENOMEM;

	imx290->dev = dev;
	imx290->regmap = devm_regmap_init_i2c(client, &imx290_regmap_config);
	if (IS_ERR(imx290->regmap)) {
		dev_err(dev, "Unable to initialize I2C\n");
		return -ENODEV;
	}

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "Endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep);
	fwnode_handle_put(endpoint);
	if (ret == -ENXIO) {
		dev_err(dev, "Unsupported bus type, should be CSI2\n");
		goto free_err;
	} else if (ret) {
		dev_err(dev, "Parsing endpoint node failed\n");
		goto free_err;
	}

	/* Get number of data lanes */
	imx290->nlanes = ep.bus.mipi_csi2.num_data_lanes;
	if (imx290->nlanes != 2 && imx290->nlanes != 4) {
		dev_err(dev, "Invalid data lanes: %d\n", imx290->nlanes);
		ret = -EINVAL;
		goto free_err;
	}

	dev_dbg(dev, "Using %u data lanes\n", imx290->nlanes);

	if (!ep.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		ret = -EINVAL;
		goto free_err;
	}

	/* Check that link frequences for all the modes are in device tree */
	fq = imx290_check_link_freqs(imx290, &ep);
	if (fq) {
		dev_err(dev, "Link frequency of %lld is not supported\n", fq);
		ret = -EINVAL;
		goto free_err;
	}

	/* get system clock (xclk) */
	imx290->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(imx290->xclk)) {
		dev_err(dev, "Could not get xclk");
		ret = PTR_ERR(imx290->xclk);
		goto free_err;
	}

	ret = fwnode_property_read_u32(dev_fwnode(dev), "clock-frequency",
				       &xclk_freq);
	if (ret) {
		dev_err(dev, "Could not get xclk frequency\n");
		goto free_err;
	}

	/* external clock must be 37.125 MHz */
	if (xclk_freq != 37125000) {
		dev_err(dev, "External clock frequency %u is not supported\n",
			xclk_freq);
		ret = -EINVAL;
		goto free_err;
	}

	ret = clk_set_rate(imx290->xclk, xclk_freq);
	if (ret) {
		dev_err(dev, "Could not set xclk frequency\n");
		goto free_err;
	}

	ret = imx290_get_regulators(dev, imx290);
	if (ret < 0) {
		dev_err(dev, "Cannot get regulators\n");
		goto free_err;
	}

	imx290->rst_gpio = devm_gpiod_get_optional(dev, "reset",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(imx290->rst_gpio)) {
		dev_err(dev, "Cannot get reset gpio\n");
		ret = PTR_ERR(imx290->rst_gpio);
		goto free_err;
	}

	mutex_init(&imx290->lock);

	/*
	 * Initialize the frame format. In particular, imx290->current_mode
	 * and imx290->bpp are set to defaults: imx290_calc_pixel_rate() call
	 * below relies on these fields.
	 */
	imx290_entity_init_cfg(&imx290->sd, NULL);

	ret = imx290_ctrl_init(imx290);
	if (ret < 0) {
		dev_err(dev, "Control initialization error %d\n", ret);
		goto free_mutex;
	}

	v4l2_i2c_subdev_init(&imx290->sd, client, &imx290_subdev_ops);
	imx290->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx290->sd.dev = &client->dev;
	imx290->sd.entity.ops = &imx290_subdev_entity_ops;
	imx290->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx290->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx290->sd.entity, 1, &imx290->pad);
	if (ret < 0) {
		dev_err(dev, "Could not register media entity\n");
		goto free_ctrl;
	}

	ret = v4l2_async_register_subdev(&imx290->sd);
	if (ret < 0) {
		dev_err(dev, "Could not register v4l2 device\n");
		goto free_entity;
	}

	/* Power on the device to match runtime PM state below */
	ret = imx290_power_on(dev);
	if (ret < 0) {
		dev_err(dev, "Could not power on the device\n");
		goto free_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	v4l2_fwnode_endpoint_free(&ep);

	return 0;

free_entity:
	media_entity_cleanup(&imx290->sd.entity);
free_ctrl:
	v4l2_ctrl_handler_free(&imx290->ctrls);
free_mutex:
	mutex_destroy(&imx290->lock);
free_err:
	v4l2_fwnode_endpoint_free(&ep);

	return ret;
}

static void imx290_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx290 *imx290 = to_imx290(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	mutex_destroy(&imx290->lock);

	pm_runtime_disable(imx290->dev);
	if (!pm_runtime_status_suspended(imx290->dev))
		imx290_power_off(imx290->dev);
	pm_runtime_set_suspended(imx290->dev);
}

static const struct of_device_id imx290_of_match[] = {
	{ .compatible = "sony,imx290" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx290_of_match);

static struct i2c_driver imx290_i2c_driver = {
	.probe_new  = imx290_probe,
	.remove = imx290_remove,
	.driver = {
		.name  = "imx290",
		.pm = &imx290_pm_ops,
		.of_match_table = of_match_ptr(imx290_of_match),
	},
};

module_i2c_driver(imx290_i2c_driver);

MODULE_DESCRIPTION("Sony IMX290 CMOS Image Sensor Driver");
MODULE_AUTHOR("FRAMOS GmbH");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_LICENSE("GPL v2");
