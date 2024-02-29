// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Sony IMX219 cameras.
 * Copyright (C) 2019, Raspberry Pi (Trading) Ltd
 *
 * Based on Sony imx258 camera driver
 * Copyright (C) 2018 Intel Corporation
 *
 * DT / fwnode changes, and regulator / GPIO control taken from imx214 driver
 * Copyright 2018 Qtechnology A/S
 *
 * Flip handling taken from the Sony IMX319 driver.
 * Copyright (C) 2018 Intel Corporation
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

/* Chip ID */
#define IMX219_REG_CHIP_ID		CCI_REG16(0x0000)
#define IMX219_CHIP_ID			0x0219

#define IMX219_REG_MODE_SELECT		CCI_REG8(0x0100)
#define IMX219_MODE_STANDBY		0x00
#define IMX219_MODE_STREAMING		0x01

#define IMX219_REG_CSI_LANE_MODE	CCI_REG8(0x0114)
#define IMX219_CSI_2_LANE_MODE		0x01
#define IMX219_CSI_4_LANE_MODE		0x03

#define IMX219_REG_DPHY_CTRL		CCI_REG8(0x0128)
#define IMX219_DPHY_CTRL_TIMING_AUTO	0
#define IMX219_DPHY_CTRL_TIMING_MANUAL	1

#define IMX219_REG_EXCK_FREQ		CCI_REG16(0x012a)
#define IMX219_EXCK_FREQ(n)		((n) * 256)		/* n expressed in MHz */

/* Analog gain control */
#define IMX219_REG_ANALOG_GAIN		CCI_REG8(0x0157)
#define IMX219_ANA_GAIN_MIN		0
#define IMX219_ANA_GAIN_MAX		232
#define IMX219_ANA_GAIN_STEP		1
#define IMX219_ANA_GAIN_DEFAULT		0x0

/* Digital gain control */
#define IMX219_REG_DIGITAL_GAIN		CCI_REG16(0x0158)
#define IMX219_DGTL_GAIN_MIN		0x0100
#define IMX219_DGTL_GAIN_MAX		0x0fff
#define IMX219_DGTL_GAIN_DEFAULT	0x0100
#define IMX219_DGTL_GAIN_STEP		1

/* Exposure control */
#define IMX219_REG_EXPOSURE		CCI_REG16(0x015a)
#define IMX219_EXPOSURE_MIN		4
#define IMX219_EXPOSURE_STEP		1
#define IMX219_EXPOSURE_DEFAULT		0x640
#define IMX219_EXPOSURE_MAX		65535

/* V_TIMING internal */
#define IMX219_REG_VTS			CCI_REG16(0x0160)
#define IMX219_VTS_MAX			0xffff

#define IMX219_VBLANK_MIN		4

/* HBLANK control - read only */
#define IMX219_PPL_DEFAULT		3448

#define IMX219_REG_LINE_LENGTH_A	CCI_REG16(0x0162)
#define IMX219_REG_X_ADD_STA_A		CCI_REG16(0x0164)
#define IMX219_REG_X_ADD_END_A		CCI_REG16(0x0166)
#define IMX219_REG_Y_ADD_STA_A		CCI_REG16(0x0168)
#define IMX219_REG_Y_ADD_END_A		CCI_REG16(0x016a)
#define IMX219_REG_X_OUTPUT_SIZE	CCI_REG16(0x016c)
#define IMX219_REG_Y_OUTPUT_SIZE	CCI_REG16(0x016e)
#define IMX219_REG_X_ODD_INC_A		CCI_REG8(0x0170)
#define IMX219_REG_Y_ODD_INC_A		CCI_REG8(0x0171)
#define IMX219_REG_ORIENTATION		CCI_REG8(0x0172)

/* Binning  Mode */
#define IMX219_REG_BINNING_MODE_H	CCI_REG8(0x0174)
#define IMX219_REG_BINNING_MODE_V	CCI_REG8(0x0175)
#define IMX219_BINNING_NONE		0x00
#define IMX219_BINNING_X2		0x01
#define IMX219_BINNING_X2_ANALOG	0x03

#define IMX219_REG_CSI_DATA_FORMAT_A	CCI_REG16(0x018c)

/* PLL Settings */
#define IMX219_REG_VTPXCK_DIV		CCI_REG8(0x0301)
#define IMX219_REG_VTSYCK_DIV		CCI_REG8(0x0303)
#define IMX219_REG_PREPLLCK_VT_DIV	CCI_REG8(0x0304)
#define IMX219_REG_PREPLLCK_OP_DIV	CCI_REG8(0x0305)
#define IMX219_REG_PLL_VT_MPY		CCI_REG16(0x0306)
#define IMX219_REG_OPPXCK_DIV		CCI_REG8(0x0309)
#define IMX219_REG_OPSYCK_DIV		CCI_REG8(0x030b)
#define IMX219_REG_PLL_OP_MPY		CCI_REG16(0x030c)

/* Test Pattern Control */
#define IMX219_REG_TEST_PATTERN		CCI_REG16(0x0600)
#define IMX219_TEST_PATTERN_DISABLE	0
#define IMX219_TEST_PATTERN_SOLID_COLOR	1
#define IMX219_TEST_PATTERN_COLOR_BARS	2
#define IMX219_TEST_PATTERN_GREY_COLOR	3
#define IMX219_TEST_PATTERN_PN9		4

/* Test pattern colour components */
#define IMX219_REG_TESTP_RED		CCI_REG16(0x0602)
#define IMX219_REG_TESTP_GREENR		CCI_REG16(0x0604)
#define IMX219_REG_TESTP_BLUE		CCI_REG16(0x0606)
#define IMX219_REG_TESTP_GREENB		CCI_REG16(0x0608)
#define IMX219_TESTP_COLOUR_MIN		0
#define IMX219_TESTP_COLOUR_MAX		0x03ff
#define IMX219_TESTP_COLOUR_STEP	1

#define IMX219_REG_TP_WINDOW_WIDTH	CCI_REG16(0x0624)
#define IMX219_REG_TP_WINDOW_HEIGHT	CCI_REG16(0x0626)

/* External clock frequency is 24.0M */
#define IMX219_XCLK_FREQ		24000000

/* Pixel rate is fixed for all the modes */
#define IMX219_PIXEL_RATE		182400000
#define IMX219_PIXEL_RATE_4LANE		280800000

#define IMX219_DEFAULT_LINK_FREQ	456000000
#define IMX219_DEFAULT_LINK_FREQ_4LANE	363000000

/* IMX219 native and active pixel array size. */
#define IMX219_NATIVE_WIDTH		3296U
#define IMX219_NATIVE_HEIGHT		2480U
#define IMX219_PIXEL_ARRAY_LEFT		8U
#define IMX219_PIXEL_ARRAY_TOP		8U
#define IMX219_PIXEL_ARRAY_WIDTH	3280U
#define IMX219_PIXEL_ARRAY_HEIGHT	2464U

/* Mode : resolution and related config&values */
struct imx219_mode {
	/* Frame width */
	unsigned int width;
	/* Frame height */
	unsigned int height;

	/* V-timing */
	unsigned int vts_def;
};

static const struct cci_reg_sequence imx219_common_regs[] = {
	{ IMX219_REG_MODE_SELECT, 0x00 },	/* Mode Select */

	/* To Access Addresses 3000-5fff, send the following commands */
	{ CCI_REG8(0x30eb), 0x0c },
	{ CCI_REG8(0x30eb), 0x05 },
	{ CCI_REG8(0x300a), 0xff },
	{ CCI_REG8(0x300b), 0xff },
	{ CCI_REG8(0x30eb), 0x05 },
	{ CCI_REG8(0x30eb), 0x09 },

	/* PLL Clock Table */
	{ IMX219_REG_VTPXCK_DIV, 5 },
	{ IMX219_REG_VTSYCK_DIV, 1 },
	{ IMX219_REG_PREPLLCK_VT_DIV, 3 },	/* 0x03 = AUTO set */
	{ IMX219_REG_PREPLLCK_OP_DIV, 3 },	/* 0x03 = AUTO set */
	{ IMX219_REG_PLL_VT_MPY, 57 },
	{ IMX219_REG_OPSYCK_DIV, 1 },
	{ IMX219_REG_PLL_OP_MPY, 114 },

	/* Undocumented registers */
	{ CCI_REG8(0x455e), 0x00 },
	{ CCI_REG8(0x471e), 0x4b },
	{ CCI_REG8(0x4767), 0x0f },
	{ CCI_REG8(0x4750), 0x14 },
	{ CCI_REG8(0x4540), 0x00 },
	{ CCI_REG8(0x47b4), 0x14 },
	{ CCI_REG8(0x4713), 0x30 },
	{ CCI_REG8(0x478b), 0x10 },
	{ CCI_REG8(0x478f), 0x10 },
	{ CCI_REG8(0x4793), 0x10 },
	{ CCI_REG8(0x4797), 0x0e },
	{ CCI_REG8(0x479b), 0x0e },

	/* Frame Bank Register Group "A" */
	{ IMX219_REG_LINE_LENGTH_A, 3448 },
	{ IMX219_REG_X_ODD_INC_A, 1 },
	{ IMX219_REG_Y_ODD_INC_A, 1 },

	/* Output setup registers */
	{ IMX219_REG_DPHY_CTRL, IMX219_DPHY_CTRL_TIMING_AUTO },
	{ IMX219_REG_EXCK_FREQ, IMX219_EXCK_FREQ(IMX219_XCLK_FREQ / 1000000) },
};

static const s64 imx219_link_freq_menu[] = {
	IMX219_DEFAULT_LINK_FREQ,
};

static const s64 imx219_link_freq_4lane_menu[] = {
	IMX219_DEFAULT_LINK_FREQ_4LANE,
};

static const char * const imx219_test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Solid Color",
	"Grey Color Bars",
	"PN9"
};

static const int imx219_test_pattern_val[] = {
	IMX219_TEST_PATTERN_DISABLE,
	IMX219_TEST_PATTERN_COLOR_BARS,
	IMX219_TEST_PATTERN_SOLID_COLOR,
	IMX219_TEST_PATTERN_GREY_COLOR,
	IMX219_TEST_PATTERN_PN9,
};

/* regulator supplies */
static const char * const imx219_supply_name[] = {
	/* Supplies can be enabled in any order */
	"VANA",  /* Analog (2.8V) supply */
	"VDIG",  /* Digital Core (1.8V) supply */
	"VDDL",  /* IF (1.2V) supply */
};

#define IMX219_NUM_SUPPLIES ARRAY_SIZE(imx219_supply_name)

/*
 * The supported formats.
 * This table MUST contain 4 entries per format, to cover the various flip
 * combinations in the order
 * - no flip
 * - h flip
 * - v flip
 * - h&v flips
 */
static const u32 imx219_mbus_formats[] = {
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,

	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SBGGR8_1X8,
};

/*
 * Initialisation delay between XCLR low->high and the moment when the sensor
 * can start capture (i.e. can leave software stanby) must be not less than:
 *   t4 + max(t5, t6 + <time to initialize the sensor register over I2C>)
 * where
 *   t4 is fixed, and is max 200uS,
 *   t5 is fixed, and is 6000uS,
 *   t6 depends on the sensor external clock, and is max 32000 clock periods.
 * As per sensor datasheet, the external clock must be from 6MHz to 27MHz.
 * So for any acceptable external clock t6 is always within the range of
 * 1185 to 5333 uS, and is always less than t5.
 * For this reason this is always safe to wait (t4 + t5) = 6200 uS, then
 * initialize the sensor over I2C, and then exit the software standby.
 *
 * This start-up time can be optimized a bit more, if we start the writes
 * over I2C after (t4+t6), but before (t4+t5) expires. But then sensor
 * initialization over I2C may complete before (t4+t5) expires, and we must
 * ensure that capture is not started before (t4+t5).
 *
 * This delay doesn't account for the power supply startup time. If needed,
 * this should be taken care of via the regulator framework. E.g. in the
 * case of DT for regulator-fixed one should define the startup-delay-us
 * property.
 */
#define IMX219_XCLR_MIN_DELAY_US	6200
#define IMX219_XCLR_DELAY_RANGE_US	1000

/* Mode configs */
static const struct imx219_mode supported_modes[] = {
	{
		/* 8MPix 15fps mode */
		.width = 3280,
		.height = 2464,
		.vts_def = 3526,
	},
	{
		/* 1080P 30fps cropped */
		.width = 1920,
		.height = 1080,
		.vts_def = 1763,
	},
	{
		/* 2x2 binned 30fps mode */
		.width = 1640,
		.height = 1232,
		.vts_def = 1763,
	},
	{
		/* 640x480 30fps mode */
		.width = 640,
		.height = 480,
		.vts_def = 1763,
	},
};

struct imx219 {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct regmap *regmap;
	struct clk *xclk; /* system clock to IMX219 */
	u32 xclk_freq;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX219_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	/* Two or Four lanes */
	u8 lanes;
};

static inline struct imx219 *to_imx219(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx219, sd);
}

/* Get bayer order based on flip setting. */
static u32 imx219_get_format_code(struct imx219 *imx219, u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx219_mbus_formats); i++)
		if (imx219_mbus_formats[i] == code)
			break;

	if (i >= ARRAY_SIZE(imx219_mbus_formats))
		i = 0;

	i = (i & ~3) | (imx219->vflip->val ? 2 : 0) |
	    (imx219->hflip->val ? 1 : 0);

	return imx219_mbus_formats[i];
}

/* -----------------------------------------------------------------------------
 * Controls
 */

static int imx219_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx219 *imx219 =
		container_of(ctrl->handler, struct imx219, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx219->sd);
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_get_locked_active_state(&imx219->sd);
	format = v4l2_subdev_state_get_format(state, 0);

	if (ctrl->id == V4L2_CID_VBLANK) {
		int exposure_max, exposure_def;

		/* Update max exposure while meeting expected vblanking */
		exposure_max = format->height + ctrl->val - 4;
		exposure_def = (exposure_max < IMX219_EXPOSURE_DEFAULT) ?
			exposure_max : IMX219_EXPOSURE_DEFAULT;
		__v4l2_ctrl_modify_range(imx219->exposure,
					 imx219->exposure->minimum,
					 exposure_max, imx219->exposure->step,
					 exposure_def);
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (pm_runtime_get_if_in_use(&client->dev) == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(imx219->regmap, IMX219_REG_ANALOG_GAIN,
			  ctrl->val, &ret);
		break;
	case V4L2_CID_EXPOSURE:
		cci_write(imx219->regmap, IMX219_REG_EXPOSURE,
			  ctrl->val, &ret);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		cci_write(imx219->regmap, IMX219_REG_DIGITAL_GAIN,
			  ctrl->val, &ret);
		break;
	case V4L2_CID_TEST_PATTERN:
		cci_write(imx219->regmap, IMX219_REG_TEST_PATTERN,
			  imx219_test_pattern_val[ctrl->val], &ret);
		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		cci_write(imx219->regmap, IMX219_REG_ORIENTATION,
			  imx219->hflip->val | imx219->vflip->val << 1, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_write(imx219->regmap, IMX219_REG_VTS,
			  format->height + ctrl->val, &ret);
		break;
	case V4L2_CID_TEST_PATTERN_RED:
		cci_write(imx219->regmap, IMX219_REG_TESTP_RED,
			  ctrl->val, &ret);
		break;
	case V4L2_CID_TEST_PATTERN_GREENR:
		cci_write(imx219->regmap, IMX219_REG_TESTP_GREENR,
			  ctrl->val, &ret);
		break;
	case V4L2_CID_TEST_PATTERN_BLUE:
		cci_write(imx219->regmap, IMX219_REG_TESTP_BLUE,
			  ctrl->val, &ret);
		break;
	case V4L2_CID_TEST_PATTERN_GREENB:
		cci_write(imx219->regmap, IMX219_REG_TESTP_GREENB,
			  ctrl->val, &ret);
		break;
	default:
		dev_info(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx219_ctrl_ops = {
	.s_ctrl = imx219_set_ctrl,
};

static unsigned long imx219_get_pixel_rate(struct imx219 *imx219)
{
	return (imx219->lanes == 2) ? IMX219_PIXEL_RATE : IMX219_PIXEL_RATE_4LANE;
}

/* Initialize control handlers */
static int imx219_init_controls(struct imx219 *imx219)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx219->sd);
	const struct imx219_mode *mode = &supported_modes[0];
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_fwnode_device_properties props;
	int exposure_max, exposure_def, hblank;
	int i, ret;

	ctrl_hdlr = &imx219->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 12);
	if (ret)
		return ret;

	/* By default, PIXEL_RATE is read only */
	imx219->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx219_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       imx219_get_pixel_rate(imx219),
					       imx219_get_pixel_rate(imx219), 1,
					       imx219_get_pixel_rate(imx219));

	imx219->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx219_ctrl_ops,
				       V4L2_CID_LINK_FREQ,
				       ARRAY_SIZE(imx219_link_freq_menu) - 1, 0,
				       (imx219->lanes == 2) ? imx219_link_freq_menu :
				       imx219_link_freq_4lane_menu);
	if (imx219->link_freq)
		imx219->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* Initial vblank/hblank/exposure parameters based on current mode */
	imx219->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx219_ctrl_ops,
					   V4L2_CID_VBLANK, IMX219_VBLANK_MIN,
					   IMX219_VTS_MAX - mode->height, 1,
					   mode->vts_def - mode->height);
	hblank = IMX219_PPL_DEFAULT - mode->width;
	imx219->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx219_ctrl_ops,
					   V4L2_CID_HBLANK, hblank, hblank,
					   1, hblank);
	if (imx219->hblank)
		imx219->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	exposure_max = mode->vts_def - 4;
	exposure_def = (exposure_max < IMX219_EXPOSURE_DEFAULT) ?
		exposure_max : IMX219_EXPOSURE_DEFAULT;
	imx219->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx219_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX219_EXPOSURE_MIN, exposure_max,
					     IMX219_EXPOSURE_STEP,
					     exposure_def);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx219_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX219_ANA_GAIN_MIN, IMX219_ANA_GAIN_MAX,
			  IMX219_ANA_GAIN_STEP, IMX219_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx219_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  IMX219_DGTL_GAIN_MIN, IMX219_DGTL_GAIN_MAX,
			  IMX219_DGTL_GAIN_STEP, IMX219_DGTL_GAIN_DEFAULT);

	imx219->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx219_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (imx219->hflip)
		imx219->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	imx219->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx219_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (imx219->vflip)
		imx219->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx219_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx219_test_pattern_menu) - 1,
				     0, 0, imx219_test_pattern_menu);
	for (i = 0; i < 4; i++) {
		/*
		 * The assumption is that
		 * V4L2_CID_TEST_PATTERN_GREENR == V4L2_CID_TEST_PATTERN_RED + 1
		 * V4L2_CID_TEST_PATTERN_BLUE   == V4L2_CID_TEST_PATTERN_RED + 2
		 * V4L2_CID_TEST_PATTERN_GREENB == V4L2_CID_TEST_PATTERN_RED + 3
		 */
		v4l2_ctrl_new_std(ctrl_hdlr, &imx219_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_RED + i,
				  IMX219_TESTP_COLOUR_MIN,
				  IMX219_TESTP_COLOUR_MAX,
				  IMX219_TESTP_COLOUR_STEP,
				  IMX219_TESTP_COLOUR_MAX);
		/* The "Solid color" pattern is white by default */
	}

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx219_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx219->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static void imx219_free_controls(struct imx219 *imx219)
{
	v4l2_ctrl_handler_free(imx219->sd.ctrl_handler);
}

/* -----------------------------------------------------------------------------
 * Subdev operations
 */

static int imx219_set_framefmt(struct imx219 *imx219,
			       struct v4l2_subdev_state *state)
{
	const struct v4l2_mbus_framefmt *format;
	const struct v4l2_rect *crop;
	unsigned int bpp;
	u64 bin_h, bin_v;
	int ret = 0;

	format = v4l2_subdev_state_get_format(state, 0);
	crop = v4l2_subdev_state_get_crop(state, 0);

	switch (format->code) {
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		bpp = 8;
		break;

	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	default:
		bpp = 10;
		break;
	}

	cci_write(imx219->regmap, IMX219_REG_X_ADD_STA_A,
		  crop->left - IMX219_PIXEL_ARRAY_LEFT, &ret);
	cci_write(imx219->regmap, IMX219_REG_X_ADD_END_A,
		  crop->left - IMX219_PIXEL_ARRAY_LEFT + crop->width - 1, &ret);
	cci_write(imx219->regmap, IMX219_REG_Y_ADD_STA_A,
		  crop->top - IMX219_PIXEL_ARRAY_TOP, &ret);
	cci_write(imx219->regmap, IMX219_REG_Y_ADD_END_A,
		  crop->top - IMX219_PIXEL_ARRAY_TOP + crop->height - 1, &ret);

	switch (crop->width / format->width) {
	case 1:
	default:
		bin_h = IMX219_BINNING_NONE;
		break;
	case 2:
		bin_h = bpp == 8 ? IMX219_BINNING_X2_ANALOG : IMX219_BINNING_X2;
		break;
	}

	switch (crop->height / format->height) {
	case 1:
	default:
		bin_v = IMX219_BINNING_NONE;
		break;
	case 2:
		bin_v = bpp == 8 ? IMX219_BINNING_X2_ANALOG : IMX219_BINNING_X2;
		break;
	}

	cci_write(imx219->regmap, IMX219_REG_BINNING_MODE_H, bin_h, &ret);
	cci_write(imx219->regmap, IMX219_REG_BINNING_MODE_V, bin_v, &ret);

	cci_write(imx219->regmap, IMX219_REG_X_OUTPUT_SIZE,
		  format->width, &ret);
	cci_write(imx219->regmap, IMX219_REG_Y_OUTPUT_SIZE,
		  format->height, &ret);

	cci_write(imx219->regmap, IMX219_REG_TP_WINDOW_WIDTH,
		  format->width, &ret);
	cci_write(imx219->regmap, IMX219_REG_TP_WINDOW_HEIGHT,
		  format->height, &ret);

	cci_write(imx219->regmap, IMX219_REG_CSI_DATA_FORMAT_A,
		  (bpp << 8) | bpp, &ret);
	cci_write(imx219->regmap, IMX219_REG_OPPXCK_DIV, bpp, &ret);

	return ret;
}

static int imx219_configure_lanes(struct imx219 *imx219)
{
	return cci_write(imx219->regmap, IMX219_REG_CSI_LANE_MODE,
			 imx219->lanes == 2 ? IMX219_CSI_2_LANE_MODE :
			 IMX219_CSI_4_LANE_MODE, NULL);
};

static int imx219_start_streaming(struct imx219 *imx219,
				  struct v4l2_subdev_state *state)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx219->sd);
	int ret;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret < 0)
		return ret;

	/* Send all registers that are common to all modes */
	ret = cci_multi_reg_write(imx219->regmap, imx219_common_regs,
				  ARRAY_SIZE(imx219_common_regs), NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to send mfg header\n", __func__);
		goto err_rpm_put;
	}

	/* Configure two or four Lane mode */
	ret = imx219_configure_lanes(imx219);
	if (ret) {
		dev_err(&client->dev, "%s failed to configure lanes\n", __func__);
		goto err_rpm_put;
	}

	/* Apply format and crop settings. */
	ret = imx219_set_framefmt(imx219, state);
	if (ret) {
		dev_err(&client->dev, "%s failed to set frame format: %d\n",
			__func__, ret);
		goto err_rpm_put;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imx219->sd.ctrl_handler);
	if (ret)
		goto err_rpm_put;

	/* set stream on register */
	ret = cci_write(imx219->regmap, IMX219_REG_MODE_SELECT,
			IMX219_MODE_STREAMING, NULL);
	if (ret)
		goto err_rpm_put;

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(imx219->vflip, true);
	__v4l2_ctrl_grab(imx219->hflip, true);

	return 0;

err_rpm_put:
	pm_runtime_put(&client->dev);
	return ret;
}

static void imx219_stop_streaming(struct imx219 *imx219)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx219->sd);
	int ret;

	/* set stream off register */
	ret = cci_write(imx219->regmap, IMX219_REG_MODE_SELECT,
			IMX219_MODE_STANDBY, NULL);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);

	__v4l2_ctrl_grab(imx219->vflip, false);
	__v4l2_ctrl_grab(imx219->hflip, false);

	pm_runtime_put(&client->dev);
}

static int imx219_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx219 *imx219 = to_imx219(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable)
		ret = imx219_start_streaming(imx219, state);
	else
		imx219_stop_streaming(imx219);

	v4l2_subdev_unlock_state(state);
	return ret;
}

static void imx219_update_pad_format(struct imx219 *imx219,
				     const struct imx219_mode *mode,
				     struct v4l2_mbus_framefmt *fmt, u32 code)
{
	/* Bayer order varies with flips */
	fmt->code = imx219_get_format_code(imx219, code);
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int imx219_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx219 *imx219 = to_imx219(sd);

	if (code->index >= (ARRAY_SIZE(imx219_mbus_formats) / 4))
		return -EINVAL;

	code->code = imx219_get_format_code(imx219, imx219_mbus_formats[code->index * 4]);

	return 0;
}

static int imx219_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx219 *imx219 = to_imx219(sd);
	u32 code;

	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	code = imx219_get_format_code(imx219, fse->code);
	if (fse->code != code)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int imx219_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx219 *imx219 = to_imx219(sd);
	const struct imx219_mode *mode;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	unsigned int bin_h, bin_v;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	imx219_update_pad_format(imx219, mode, &fmt->format, fmt->format.code);

	format = v4l2_subdev_state_get_format(state, 0);
	*format = fmt->format;

	/*
	 * Use binning to maximize the crop rectangle size, and centre it in the
	 * sensor.
	 */
	bin_h = min(IMX219_PIXEL_ARRAY_WIDTH / format->width, 2U);
	bin_v = min(IMX219_PIXEL_ARRAY_HEIGHT / format->height, 2U);

	crop = v4l2_subdev_state_get_crop(state, 0);
	crop->width = format->width * bin_h;
	crop->height = format->height * bin_v;
	crop->left = (IMX219_NATIVE_WIDTH - crop->width) / 2;
	crop->top = (IMX219_NATIVE_HEIGHT - crop->height) / 2;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		int exposure_max;
		int exposure_def;
		int hblank;

		/* Update limits and set FPS to default */
		__v4l2_ctrl_modify_range(imx219->vblank, IMX219_VBLANK_MIN,
					 IMX219_VTS_MAX - mode->height, 1,
					 mode->vts_def - mode->height);
		__v4l2_ctrl_s_ctrl(imx219->vblank,
				   mode->vts_def - mode->height);
		/* Update max exposure while meeting expected vblanking */
		exposure_max = mode->vts_def - 4;
		exposure_def = (exposure_max < IMX219_EXPOSURE_DEFAULT) ?
			exposure_max : IMX219_EXPOSURE_DEFAULT;
		__v4l2_ctrl_modify_range(imx219->exposure,
					 imx219->exposure->minimum,
					 exposure_max, imx219->exposure->step,
					 exposure_def);
		/*
		 * Currently PPL is fixed to IMX219_PPL_DEFAULT, so hblank
		 * depends on mode->width only, and is not changeble in any
		 * way other than changing the mode.
		 */
		hblank = IMX219_PPL_DEFAULT - mode->width;
		__v4l2_ctrl_modify_range(imx219->hblank, hblank, hblank, 1,
					 hblank);
	}

	return 0;
}

static int imx219_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		sel->r = *v4l2_subdev_state_get_crop(state, 0);
		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = IMX219_NATIVE_WIDTH;
		sel->r.height = IMX219_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = IMX219_PIXEL_ARRAY_TOP;
		sel->r.left = IMX219_PIXEL_ARRAY_LEFT;
		sel->r.width = IMX219_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX219_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

static int imx219_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			.code = MEDIA_BUS_FMT_SRGGB10_1X10,
			.width = supported_modes[0].width,
			.height = supported_modes[0].height,
		},
	};

	imx219_set_pad_format(sd, state, &fmt);

	return 0;
}

static const struct v4l2_subdev_core_ops imx219_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx219_video_ops = {
	.s_stream = imx219_set_stream,
};

static const struct v4l2_subdev_pad_ops imx219_pad_ops = {
	.enum_mbus_code = imx219_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx219_set_pad_format,
	.get_selection = imx219_get_selection,
	.enum_frame_size = imx219_enum_frame_size,
};

static const struct v4l2_subdev_ops imx219_subdev_ops = {
	.core = &imx219_core_ops,
	.video = &imx219_video_ops,
	.pad = &imx219_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx219_internal_ops = {
	.init_state = imx219_init_state,
};

/* -----------------------------------------------------------------------------
 * Power management
 */

static int imx219_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx219 *imx219 = to_imx219(sd);
	int ret;

	ret = regulator_bulk_enable(IMX219_NUM_SUPPLIES,
				    imx219->supplies);
	if (ret) {
		dev_err(dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imx219->xclk);
	if (ret) {
		dev_err(dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(imx219->reset_gpio, 1);
	usleep_range(IMX219_XCLR_MIN_DELAY_US,
		     IMX219_XCLR_MIN_DELAY_US + IMX219_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(IMX219_NUM_SUPPLIES, imx219->supplies);

	return ret;
}

static int imx219_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx219 *imx219 = to_imx219(sd);

	gpiod_set_value_cansleep(imx219->reset_gpio, 0);
	regulator_bulk_disable(IMX219_NUM_SUPPLIES, imx219->supplies);
	clk_disable_unprepare(imx219->xclk);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Probe & remove
 */

static int imx219_get_regulators(struct imx219 *imx219)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx219->sd);
	unsigned int i;

	for (i = 0; i < IMX219_NUM_SUPPLIES; i++)
		imx219->supplies[i].supply = imx219_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       IMX219_NUM_SUPPLIES,
				       imx219->supplies);
}

/* Verify chip ID */
static int imx219_identify_module(struct imx219 *imx219)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx219->sd);
	int ret;
	u64 val;

	ret = cci_read(imx219->regmap, IMX219_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x\n",
			IMX219_CHIP_ID);
		return ret;
	}

	if (val != IMX219_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%llx\n",
			IMX219_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static int imx219_check_hwcfg(struct device *dev, struct imx219 *imx219)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = -EINVAL;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
		dev_err(dev, "could not parse endpoint\n");
		goto error_out;
	}

	/* Check the number of MIPI CSI2 data lanes */
	if (ep_cfg.bus.mipi_csi2.num_data_lanes != 2 &&
	    ep_cfg.bus.mipi_csi2.num_data_lanes != 4) {
		dev_err(dev, "only 2 or 4 data lanes are currently supported\n");
		goto error_out;
	}
	imx219->lanes = ep_cfg.bus.mipi_csi2.num_data_lanes;

	/* Check the link frequency set in device tree */
	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		goto error_out;
	}

	if (ep_cfg.nr_of_link_frequencies != 1 ||
	   (ep_cfg.link_frequencies[0] != ((imx219->lanes == 2) ?
	    IMX219_DEFAULT_LINK_FREQ : IMX219_DEFAULT_LINK_FREQ_4LANE))) {
		dev_err(dev, "Link frequency not supported: %lld\n",
			ep_cfg.link_frequencies[0]);
		goto error_out;
	}

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int imx219_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx219 *imx219;
	int ret;

	imx219 = devm_kzalloc(&client->dev, sizeof(*imx219), GFP_KERNEL);
	if (!imx219)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imx219->sd, client, &imx219_subdev_ops);
	imx219->sd.internal_ops = &imx219_internal_ops;

	/* Check the hardware configuration in device tree */
	if (imx219_check_hwcfg(dev, imx219))
		return -EINVAL;

	imx219->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx219->regmap)) {
		ret = PTR_ERR(imx219->regmap);
		dev_err(dev, "failed to initialize CCI: %d\n", ret);
		return ret;
	}

	/* Get system clock (xclk) */
	imx219->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(imx219->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(imx219->xclk);
	}

	imx219->xclk_freq = clk_get_rate(imx219->xclk);
	if (imx219->xclk_freq != IMX219_XCLK_FREQ) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			imx219->xclk_freq);
		return -EINVAL;
	}

	ret = imx219_get_regulators(imx219);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	/* Request optional enable pin */
	imx219->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);

	/*
	 * The sensor must be powered for imx219_identify_module()
	 * to be able to read the CHIP_ID register
	 */
	ret = imx219_power_on(dev);
	if (ret)
		return ret;

	ret = imx219_identify_module(imx219);
	if (ret)
		goto error_power_off;

	/*
	 * Sensor doesn't enter LP-11 state upon power up until and unless
	 * streaming is started, so upon power up switch the modes to:
	 * streaming -> standby
	 */
	ret = cci_write(imx219->regmap, IMX219_REG_MODE_SELECT,
			IMX219_MODE_STREAMING, NULL);
	if (ret < 0)
		goto error_power_off;

	usleep_range(100, 110);

	/* put sensor back to standby mode */
	ret = cci_write(imx219->regmap, IMX219_REG_MODE_SELECT,
			IMX219_MODE_STANDBY, NULL);
	if (ret < 0)
		goto error_power_off;

	usleep_range(100, 110);

	ret = imx219_init_controls(imx219);
	if (ret)
		goto error_power_off;

	/* Initialize subdev */
	imx219->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx219->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx219->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx219->sd.entity, 1, &imx219->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	imx219->sd.state_lock = imx219->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&imx219->sd);
	if (ret < 0) {
		dev_err(dev, "subdev init error: %d\n", ret);
		goto error_media_entity;
	}

	ret = v4l2_async_register_subdev_sensor(&imx219->sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_subdev_cleanup;
	}

	/* Enable runtime PM and turn off the device */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&imx219->sd);

error_media_entity:
	media_entity_cleanup(&imx219->sd.entity);

error_handler_free:
	imx219_free_controls(imx219);

error_power_off:
	imx219_power_off(dev);

	return ret;
}

static void imx219_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx219 *imx219 = to_imx219(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	imx219_free_controls(imx219);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx219_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct of_device_id imx219_dt_ids[] = {
	{ .compatible = "sony,imx219" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx219_dt_ids);

static const struct dev_pm_ops imx219_pm_ops = {
	SET_RUNTIME_PM_OPS(imx219_power_off, imx219_power_on, NULL)
};

static struct i2c_driver imx219_i2c_driver = {
	.driver = {
		.name = "imx219",
		.of_match_table	= imx219_dt_ids,
		.pm = &imx219_pm_ops,
	},
	.probe = imx219_probe,
	.remove = imx219_remove,
};

module_i2c_driver(imx219_i2c_driver);

MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.com");
MODULE_DESCRIPTION("Sony IMX219 sensor driver");
MODULE_LICENSE("GPL v2");
