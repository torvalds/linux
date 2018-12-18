/*
 * Driver for MT9V022, MT9V024, MT9V032, and MT9V034 CMOS Image Sensors
 *
 * Copyright (C) 2010, Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * Based on the MT9M001 driver,
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>
#include <linux/module.h>

#include <media/i2c/mt9v032.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* The first four rows are black rows. The active area spans 753x481 pixels. */
#define MT9V032_PIXEL_ARRAY_HEIGHT			485
#define MT9V032_PIXEL_ARRAY_WIDTH			753

#define MT9V032_SYSCLK_FREQ_DEF				26600000

#define MT9V032_CHIP_VERSION				0x00
#define		MT9V032_CHIP_ID_REV1			0x1311
#define		MT9V032_CHIP_ID_REV3			0x1313
#define		MT9V034_CHIP_ID_REV1			0X1324
#define MT9V032_COLUMN_START				0x01
#define		MT9V032_COLUMN_START_MIN		1
#define		MT9V032_COLUMN_START_DEF		1
#define		MT9V032_COLUMN_START_MAX		752
#define MT9V032_ROW_START				0x02
#define		MT9V032_ROW_START_MIN			4
#define		MT9V032_ROW_START_DEF			5
#define		MT9V032_ROW_START_MAX			482
#define MT9V032_WINDOW_HEIGHT				0x03
#define		MT9V032_WINDOW_HEIGHT_MIN		1
#define		MT9V032_WINDOW_HEIGHT_DEF		480
#define		MT9V032_WINDOW_HEIGHT_MAX		480
#define MT9V032_WINDOW_WIDTH				0x04
#define		MT9V032_WINDOW_WIDTH_MIN		1
#define		MT9V032_WINDOW_WIDTH_DEF		752
#define		MT9V032_WINDOW_WIDTH_MAX		752
#define MT9V032_HORIZONTAL_BLANKING			0x05
#define		MT9V032_HORIZONTAL_BLANKING_MIN		43
#define		MT9V034_HORIZONTAL_BLANKING_MIN		61
#define		MT9V032_HORIZONTAL_BLANKING_DEF		94
#define		MT9V032_HORIZONTAL_BLANKING_MAX		1023
#define MT9V032_VERTICAL_BLANKING			0x06
#define		MT9V032_VERTICAL_BLANKING_MIN		4
#define		MT9V034_VERTICAL_BLANKING_MIN		2
#define		MT9V032_VERTICAL_BLANKING_DEF		45
#define		MT9V032_VERTICAL_BLANKING_MAX		3000
#define		MT9V034_VERTICAL_BLANKING_MAX		32288
#define MT9V032_CHIP_CONTROL				0x07
#define		MT9V032_CHIP_CONTROL_MASTER_MODE	(1 << 3)
#define		MT9V032_CHIP_CONTROL_DOUT_ENABLE	(1 << 7)
#define		MT9V032_CHIP_CONTROL_SEQUENTIAL		(1 << 8)
#define MT9V032_SHUTTER_WIDTH1				0x08
#define MT9V032_SHUTTER_WIDTH2				0x09
#define MT9V032_SHUTTER_WIDTH_CONTROL			0x0a
#define MT9V032_TOTAL_SHUTTER_WIDTH			0x0b
#define		MT9V032_TOTAL_SHUTTER_WIDTH_MIN		1
#define		MT9V034_TOTAL_SHUTTER_WIDTH_MIN		0
#define		MT9V032_TOTAL_SHUTTER_WIDTH_DEF		480
#define		MT9V032_TOTAL_SHUTTER_WIDTH_MAX		32767
#define		MT9V034_TOTAL_SHUTTER_WIDTH_MAX		32765
#define MT9V032_RESET					0x0c
#define MT9V032_READ_MODE				0x0d
#define		MT9V032_READ_MODE_ROW_BIN_MASK		(3 << 0)
#define		MT9V032_READ_MODE_ROW_BIN_SHIFT		0
#define		MT9V032_READ_MODE_COLUMN_BIN_MASK	(3 << 2)
#define		MT9V032_READ_MODE_COLUMN_BIN_SHIFT	2
#define		MT9V032_READ_MODE_ROW_FLIP		(1 << 4)
#define		MT9V032_READ_MODE_COLUMN_FLIP		(1 << 5)
#define		MT9V032_READ_MODE_DARK_COLUMNS		(1 << 6)
#define		MT9V032_READ_MODE_DARK_ROWS		(1 << 7)
#define		MT9V032_READ_MODE_RESERVED		0x0300
#define MT9V032_PIXEL_OPERATION_MODE			0x0f
#define		MT9V034_PIXEL_OPERATION_MODE_HDR	(1 << 0)
#define		MT9V034_PIXEL_OPERATION_MODE_COLOR	(1 << 1)
#define		MT9V032_PIXEL_OPERATION_MODE_COLOR	(1 << 2)
#define		MT9V032_PIXEL_OPERATION_MODE_HDR	(1 << 6)
#define MT9V032_ANALOG_GAIN				0x35
#define		MT9V032_ANALOG_GAIN_MIN			16
#define		MT9V032_ANALOG_GAIN_DEF			16
#define		MT9V032_ANALOG_GAIN_MAX			64
#define MT9V032_MAX_ANALOG_GAIN				0x36
#define		MT9V032_MAX_ANALOG_GAIN_MAX		127
#define MT9V032_FRAME_DARK_AVERAGE			0x42
#define MT9V032_DARK_AVG_THRESH				0x46
#define		MT9V032_DARK_AVG_LOW_THRESH_MASK	(255 << 0)
#define		MT9V032_DARK_AVG_LOW_THRESH_SHIFT	0
#define		MT9V032_DARK_AVG_HIGH_THRESH_MASK	(255 << 8)
#define		MT9V032_DARK_AVG_HIGH_THRESH_SHIFT	8
#define MT9V032_ROW_NOISE_CORR_CONTROL			0x70
#define		MT9V034_ROW_NOISE_CORR_ENABLE		(1 << 0)
#define		MT9V034_ROW_NOISE_CORR_USE_BLK_AVG	(1 << 1)
#define		MT9V032_ROW_NOISE_CORR_ENABLE		(1 << 5)
#define		MT9V032_ROW_NOISE_CORR_USE_BLK_AVG	(1 << 7)
#define MT9V032_PIXEL_CLOCK				0x74
#define MT9V034_PIXEL_CLOCK				0x72
#define		MT9V032_PIXEL_CLOCK_INV_LINE		(1 << 0)
#define		MT9V032_PIXEL_CLOCK_INV_FRAME		(1 << 1)
#define		MT9V032_PIXEL_CLOCK_XOR_LINE		(1 << 2)
#define		MT9V032_PIXEL_CLOCK_CONT_LINE		(1 << 3)
#define		MT9V032_PIXEL_CLOCK_INV_PXL_CLK		(1 << 4)
#define MT9V032_TEST_PATTERN				0x7f
#define		MT9V032_TEST_PATTERN_DATA_MASK		(1023 << 0)
#define		MT9V032_TEST_PATTERN_DATA_SHIFT		0
#define		MT9V032_TEST_PATTERN_USE_DATA		(1 << 10)
#define		MT9V032_TEST_PATTERN_GRAY_MASK		(3 << 11)
#define		MT9V032_TEST_PATTERN_GRAY_NONE		(0 << 11)
#define		MT9V032_TEST_PATTERN_GRAY_VERTICAL	(1 << 11)
#define		MT9V032_TEST_PATTERN_GRAY_HORIZONTAL	(2 << 11)
#define		MT9V032_TEST_PATTERN_GRAY_DIAGONAL	(3 << 11)
#define		MT9V032_TEST_PATTERN_ENABLE		(1 << 13)
#define		MT9V032_TEST_PATTERN_FLIP		(1 << 14)
#define MT9V032_AEGC_DESIRED_BIN			0xa5
#define MT9V032_AEC_UPDATE_FREQUENCY			0xa6
#define MT9V032_AEC_LPF					0xa8
#define MT9V032_AGC_UPDATE_FREQUENCY			0xa9
#define MT9V032_AGC_LPF					0xaa
#define MT9V032_AEC_AGC_ENABLE				0xaf
#define		MT9V032_AEC_ENABLE			(1 << 0)
#define		MT9V032_AGC_ENABLE			(1 << 1)
#define MT9V034_AEC_MAX_SHUTTER_WIDTH			0xad
#define MT9V032_AEC_MAX_SHUTTER_WIDTH			0xbd
#define MT9V032_THERMAL_INFO				0xc1

enum mt9v032_model {
	MT9V032_MODEL_V022_COLOR,	/* MT9V022IX7ATC */
	MT9V032_MODEL_V022_MONO,	/* MT9V022IX7ATM */
	MT9V032_MODEL_V024_COLOR,	/* MT9V024IA7XTC */
	MT9V032_MODEL_V024_MONO,	/* MT9V024IA7XTM */
	MT9V032_MODEL_V032_COLOR,	/* MT9V032C12STM */
	MT9V032_MODEL_V032_MONO,	/* MT9V032C12STC */
	MT9V032_MODEL_V034_COLOR,
	MT9V032_MODEL_V034_MONO,
};

struct mt9v032_model_version {
	unsigned int version;
	const char *name;
};

struct mt9v032_model_data {
	unsigned int min_row_time;
	unsigned int min_hblank;
	unsigned int min_vblank;
	unsigned int max_vblank;
	unsigned int min_shutter;
	unsigned int max_shutter;
	unsigned int pclk_reg;
	unsigned int aec_max_shutter_reg;
	const struct v4l2_ctrl_config * const aec_max_shutter_v4l2_ctrl;
};

struct mt9v032_model_info {
	const struct mt9v032_model_data *data;
	bool color;
};

static const struct mt9v032_model_version mt9v032_versions[] = {
	{ MT9V032_CHIP_ID_REV1, "MT9V022/MT9V032 rev1/2" },
	{ MT9V032_CHIP_ID_REV3, "MT9V022/MT9V032 rev3" },
	{ MT9V034_CHIP_ID_REV1, "MT9V024/MT9V034 rev1" },
};

struct mt9v032 {
	struct v4l2_subdev subdev;
	struct media_pad pad;

	struct v4l2_mbus_framefmt format;
	struct v4l2_rect crop;
	unsigned int hratio;
	unsigned int vratio;

	struct v4l2_ctrl_handler ctrls;
	struct {
		struct v4l2_ctrl *link_freq;
		struct v4l2_ctrl *pixel_rate;
	};

	struct mutex power_lock;
	int power_count;

	struct regmap *regmap;
	struct clk *clk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *standby_gpio;

	struct mt9v032_platform_data *pdata;
	const struct mt9v032_model_info *model;
	const struct mt9v032_model_version *version;

	u32 sysclk;
	u16 aec_agc;
	u16 hblank;
	struct {
		struct v4l2_ctrl *test_pattern;
		struct v4l2_ctrl *test_pattern_color;
	};
};

static struct mt9v032 *to_mt9v032(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9v032, subdev);
}

static int
mt9v032_update_aec_agc(struct mt9v032 *mt9v032, u16 which, int enable)
{
	struct regmap *map = mt9v032->regmap;
	u16 value = mt9v032->aec_agc;
	int ret;

	if (enable)
		value |= which;
	else
		value &= ~which;

	ret = regmap_write(map, MT9V032_AEC_AGC_ENABLE, value);
	if (ret < 0)
		return ret;

	mt9v032->aec_agc = value;
	return 0;
}

static int
mt9v032_update_hblank(struct mt9v032 *mt9v032)
{
	struct v4l2_rect *crop = &mt9v032->crop;
	unsigned int min_hblank = mt9v032->model->data->min_hblank;
	unsigned int hblank;

	if (mt9v032->version->version == MT9V034_CHIP_ID_REV1)
		min_hblank += (mt9v032->hratio - 1) * 10;
	min_hblank = max_t(int, mt9v032->model->data->min_row_time - crop->width,
			   min_hblank);
	hblank = max_t(unsigned int, mt9v032->hblank, min_hblank);

	return regmap_write(mt9v032->regmap, MT9V032_HORIZONTAL_BLANKING,
			    hblank);
}

static int mt9v032_power_on(struct mt9v032 *mt9v032)
{
	struct regmap *map = mt9v032->regmap;
	int ret;

	gpiod_set_value_cansleep(mt9v032->reset_gpio, 1);

	ret = clk_set_rate(mt9v032->clk, mt9v032->sysclk);
	if (ret < 0)
		return ret;

	/* System clock has to be enabled before releasing the reset */
	ret = clk_prepare_enable(mt9v032->clk);
	if (ret)
		return ret;

	udelay(1);

	if (mt9v032->reset_gpio) {
		gpiod_set_value_cansleep(mt9v032->reset_gpio, 0);

		/* After releasing reset we need to wait 10 clock cycles
		 * before accessing the sensor over I2C. As the minimum SYSCLK
		 * frequency is 13MHz, waiting 1µs will be enough in the worst
		 * case.
		 */
		udelay(1);
	}

	/* Reset the chip and stop data read out */
	ret = regmap_write(map, MT9V032_RESET, 1);
	if (ret < 0)
		goto err;

	ret = regmap_write(map, MT9V032_RESET, 0);
	if (ret < 0)
		goto err;

	ret = regmap_write(map, MT9V032_CHIP_CONTROL,
			   MT9V032_CHIP_CONTROL_MASTER_MODE);
	if (ret < 0)
		goto err;

	return 0;

err:
	clk_disable_unprepare(mt9v032->clk);
	return ret;
}

static void mt9v032_power_off(struct mt9v032 *mt9v032)
{
	clk_disable_unprepare(mt9v032->clk);
}

static int __mt9v032_set_power(struct mt9v032 *mt9v032, bool on)
{
	struct regmap *map = mt9v032->regmap;
	int ret;

	if (!on) {
		mt9v032_power_off(mt9v032);
		return 0;
	}

	ret = mt9v032_power_on(mt9v032);
	if (ret < 0)
		return ret;

	/* Configure the pixel clock polarity */
	if (mt9v032->pdata && mt9v032->pdata->clk_pol) {
		ret = regmap_write(map, mt9v032->model->data->pclk_reg,
				MT9V032_PIXEL_CLOCK_INV_PXL_CLK);
		if (ret < 0)
			return ret;
	}

	/* Disable the noise correction algorithm and restore the controls. */
	ret = regmap_write(map, MT9V032_ROW_NOISE_CORR_CONTROL, 0);
	if (ret < 0)
		return ret;

	return v4l2_ctrl_handler_setup(&mt9v032->ctrls);
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev video operations
 */

static struct v4l2_mbus_framefmt *
__mt9v032_get_pad_format(struct mt9v032 *mt9v032, struct v4l2_subdev_pad_config *cfg,
			 unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&mt9v032->subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &mt9v032->format;
	default:
		return NULL;
	}
}

static struct v4l2_rect *
__mt9v032_get_pad_crop(struct mt9v032 *mt9v032, struct v4l2_subdev_pad_config *cfg,
		       unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&mt9v032->subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &mt9v032->crop;
	default:
		return NULL;
	}
}

static int mt9v032_s_stream(struct v4l2_subdev *subdev, int enable)
{
	const u16 mode = MT9V032_CHIP_CONTROL_DOUT_ENABLE
		       | MT9V032_CHIP_CONTROL_SEQUENTIAL;
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);
	struct v4l2_rect *crop = &mt9v032->crop;
	struct regmap *map = mt9v032->regmap;
	unsigned int hbin;
	unsigned int vbin;
	int ret;

	if (!enable)
		return regmap_update_bits(map, MT9V032_CHIP_CONTROL, mode, 0);

	/* Configure the window size and row/column bin */
	hbin = fls(mt9v032->hratio) - 1;
	vbin = fls(mt9v032->vratio) - 1;
	ret = regmap_update_bits(map, MT9V032_READ_MODE,
				 ~MT9V032_READ_MODE_RESERVED,
				 hbin << MT9V032_READ_MODE_COLUMN_BIN_SHIFT |
				 vbin << MT9V032_READ_MODE_ROW_BIN_SHIFT);
	if (ret < 0)
		return ret;

	ret = regmap_write(map, MT9V032_COLUMN_START, crop->left);
	if (ret < 0)
		return ret;

	ret = regmap_write(map, MT9V032_ROW_START, crop->top);
	if (ret < 0)
		return ret;

	ret = regmap_write(map, MT9V032_WINDOW_WIDTH, crop->width);
	if (ret < 0)
		return ret;

	ret = regmap_write(map, MT9V032_WINDOW_HEIGHT, crop->height);
	if (ret < 0)
		return ret;

	ret = mt9v032_update_hblank(mt9v032);
	if (ret < 0)
		return ret;

	/* Switch to master "normal" mode */
	return regmap_update_bits(map, MT9V032_CHIP_CONTROL, mode, mode);
}

static int mt9v032_enum_mbus_code(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	return 0;
}

static int mt9v032_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= 3 || fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	fse->min_width = MT9V032_WINDOW_WIDTH_DEF / (1 << fse->index);
	fse->max_width = fse->min_width;
	fse->min_height = MT9V032_WINDOW_HEIGHT_DEF / (1 << fse->index);
	fse->max_height = fse->min_height;

	return 0;
}

static int mt9v032_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *format)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);

	format->format = *__mt9v032_get_pad_format(mt9v032, cfg, format->pad,
						   format->which);
	return 0;
}

static void mt9v032_configure_pixel_rate(struct mt9v032 *mt9v032)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9v032->subdev);
	int ret;

	ret = v4l2_ctrl_s_ctrl_int64(mt9v032->pixel_rate,
				     mt9v032->sysclk / mt9v032->hratio);
	if (ret < 0)
		dev_warn(&client->dev, "failed to set pixel rate (%d)\n", ret);
}

static unsigned int mt9v032_calc_ratio(unsigned int input, unsigned int output)
{
	/* Compute the power-of-two binning factor closest to the input size to
	 * output size ratio. Given that the output size is bounded by input/4
	 * and input, a generic implementation would be an ineffective luxury.
	 */
	if (output * 3 > input * 2)
		return 1;
	if (output * 3 > input)
		return 2;
	return 4;
}

static int mt9v032_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *format)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_rect *__crop;
	unsigned int width;
	unsigned int height;
	unsigned int hratio;
	unsigned int vratio;

	__crop = __mt9v032_get_pad_crop(mt9v032, cfg, format->pad,
					format->which);

	/* Clamp the width and height to avoid dividing by zero. */
	width = clamp(ALIGN(format->format.width, 2),
		      max_t(unsigned int, __crop->width / 4,
			    MT9V032_WINDOW_WIDTH_MIN),
		      __crop->width);
	height = clamp(ALIGN(format->format.height, 2),
		       max_t(unsigned int, __crop->height / 4,
			     MT9V032_WINDOW_HEIGHT_MIN),
		       __crop->height);

	hratio = mt9v032_calc_ratio(__crop->width, width);
	vratio = mt9v032_calc_ratio(__crop->height, height);

	__format = __mt9v032_get_pad_format(mt9v032, cfg, format->pad,
					    format->which);
	__format->width = __crop->width / hratio;
	__format->height = __crop->height / vratio;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		mt9v032->hratio = hratio;
		mt9v032->vratio = vratio;
		mt9v032_configure_pixel_rate(mt9v032);
	}

	format->format = *__format;

	return 0;
}

static int mt9v032_get_selection(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sel->r = *__mt9v032_get_pad_crop(mt9v032, cfg, sel->pad, sel->which);
	return 0;
}

static int mt9v032_set_selection(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_rect *__crop;
	struct v4l2_rect rect;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	/* Clamp the crop rectangle boundaries and align them to a non multiple
	 * of 2 pixels to ensure a GRBG Bayer pattern.
	 */
	rect.left = clamp(ALIGN(sel->r.left + 1, 2) - 1,
			  MT9V032_COLUMN_START_MIN,
			  MT9V032_COLUMN_START_MAX);
	rect.top = clamp(ALIGN(sel->r.top + 1, 2) - 1,
			 MT9V032_ROW_START_MIN,
			 MT9V032_ROW_START_MAX);
	rect.width = clamp_t(unsigned int, ALIGN(sel->r.width, 2),
			     MT9V032_WINDOW_WIDTH_MIN,
			     MT9V032_WINDOW_WIDTH_MAX);
	rect.height = clamp_t(unsigned int, ALIGN(sel->r.height, 2),
			      MT9V032_WINDOW_HEIGHT_MIN,
			      MT9V032_WINDOW_HEIGHT_MAX);

	rect.width = min_t(unsigned int,
			   rect.width, MT9V032_PIXEL_ARRAY_WIDTH - rect.left);
	rect.height = min_t(unsigned int,
			    rect.height, MT9V032_PIXEL_ARRAY_HEIGHT - rect.top);

	__crop = __mt9v032_get_pad_crop(mt9v032, cfg, sel->pad, sel->which);

	if (rect.width != __crop->width || rect.height != __crop->height) {
		/* Reset the output image size if the crop rectangle size has
		 * been modified.
		 */
		__format = __mt9v032_get_pad_format(mt9v032, cfg, sel->pad,
						    sel->which);
		__format->width = rect.width;
		__format->height = rect.height;
		if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			mt9v032->hratio = 1;
			mt9v032->vratio = 1;
			mt9v032_configure_pixel_rate(mt9v032);
		}
	}

	*__crop = rect;
	sel->r = rect;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev control operations
 */

#define V4L2_CID_TEST_PATTERN_COLOR	(V4L2_CID_USER_BASE | 0x1001)
/*
 * Value between 1 and 64 to set the desired bin. This is effectively a measure
 * of how bright the image is supposed to be. Both AGC and AEC try to reach
 * this.
 */
#define V4L2_CID_AEGC_DESIRED_BIN	(V4L2_CID_USER_BASE | 0x1002)
/*
 * LPF is the low pass filter capability of the chip. Both AEC and AGC have
 * this setting. This limits the speed in which AGC/AEC adjust their settings.
 * Possible values are 0-2. 0 means no LPF. For 1 and 2 this equation is used:
 *
 * if |(calculated new exp - current exp)| > (current exp / 4)
 *	next exp = calculated new exp
 * else
 *	next exp = current exp + ((calculated new exp - current exp) / 2^LPF)
 */
#define V4L2_CID_AEC_LPF		(V4L2_CID_USER_BASE | 0x1003)
#define V4L2_CID_AGC_LPF		(V4L2_CID_USER_BASE | 0x1004)
/*
 * Value between 0 and 15. This is the number of frames being skipped before
 * updating the auto exposure/gain.
 */
#define V4L2_CID_AEC_UPDATE_INTERVAL	(V4L2_CID_USER_BASE | 0x1005)
#define V4L2_CID_AGC_UPDATE_INTERVAL	(V4L2_CID_USER_BASE | 0x1006)
/*
 * Maximum shutter width used for AEC.
 */
#define V4L2_CID_AEC_MAX_SHUTTER_WIDTH	(V4L2_CID_USER_BASE | 0x1007)

static int mt9v032_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9v032 *mt9v032 =
			container_of(ctrl->handler, struct mt9v032, ctrls);
	struct regmap *map = mt9v032->regmap;
	u32 freq;
	u16 data;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		return mt9v032_update_aec_agc(mt9v032, MT9V032_AGC_ENABLE,
					      ctrl->val);

	case V4L2_CID_GAIN:
		return regmap_write(map, MT9V032_ANALOG_GAIN, ctrl->val);

	case V4L2_CID_EXPOSURE_AUTO:
		return mt9v032_update_aec_agc(mt9v032, MT9V032_AEC_ENABLE,
					      !ctrl->val);

	case V4L2_CID_EXPOSURE:
		return regmap_write(map, MT9V032_TOTAL_SHUTTER_WIDTH,
				    ctrl->val);

	case V4L2_CID_HBLANK:
		mt9v032->hblank = ctrl->val;
		return mt9v032_update_hblank(mt9v032);

	case V4L2_CID_VBLANK:
		return regmap_write(map, MT9V032_VERTICAL_BLANKING,
				    ctrl->val);

	case V4L2_CID_PIXEL_RATE:
	case V4L2_CID_LINK_FREQ:
		if (mt9v032->link_freq == NULL)
			break;

		freq = mt9v032->pdata->link_freqs[mt9v032->link_freq->val];
		*mt9v032->pixel_rate->p_new.p_s64 = freq;
		mt9v032->sysclk = freq;
		break;

	case V4L2_CID_TEST_PATTERN:
		switch (mt9v032->test_pattern->val) {
		case 0:
			data = 0;
			break;
		case 1:
			data = MT9V032_TEST_PATTERN_GRAY_VERTICAL
			     | MT9V032_TEST_PATTERN_ENABLE;
			break;
		case 2:
			data = MT9V032_TEST_PATTERN_GRAY_HORIZONTAL
			     | MT9V032_TEST_PATTERN_ENABLE;
			break;
		case 3:
			data = MT9V032_TEST_PATTERN_GRAY_DIAGONAL
			     | MT9V032_TEST_PATTERN_ENABLE;
			break;
		default:
			data = (mt9v032->test_pattern_color->val <<
				MT9V032_TEST_PATTERN_DATA_SHIFT)
			     | MT9V032_TEST_PATTERN_USE_DATA
			     | MT9V032_TEST_PATTERN_ENABLE
			     | MT9V032_TEST_PATTERN_FLIP;
			break;
		}
		return regmap_write(map, MT9V032_TEST_PATTERN, data);

	case V4L2_CID_AEGC_DESIRED_BIN:
		return regmap_write(map, MT9V032_AEGC_DESIRED_BIN, ctrl->val);

	case V4L2_CID_AEC_LPF:
		return regmap_write(map, MT9V032_AEC_LPF, ctrl->val);

	case V4L2_CID_AGC_LPF:
		return regmap_write(map, MT9V032_AGC_LPF, ctrl->val);

	case V4L2_CID_AEC_UPDATE_INTERVAL:
		return regmap_write(map, MT9V032_AEC_UPDATE_FREQUENCY,
				    ctrl->val);

	case V4L2_CID_AGC_UPDATE_INTERVAL:
		return regmap_write(map, MT9V032_AGC_UPDATE_FREQUENCY,
				    ctrl->val);

	case V4L2_CID_AEC_MAX_SHUTTER_WIDTH:
		return regmap_write(map,
				    mt9v032->model->data->aec_max_shutter_reg,
				    ctrl->val);
	}

	return 0;
}

static const struct v4l2_ctrl_ops mt9v032_ctrl_ops = {
	.s_ctrl = mt9v032_s_ctrl,
};

static const char * const mt9v032_test_pattern_menu[] = {
	"Disabled",
	"Gray Vertical Shade",
	"Gray Horizontal Shade",
	"Gray Diagonal Shade",
	"Plain",
};

static const struct v4l2_ctrl_config mt9v032_test_pattern_color = {
	.ops		= &mt9v032_ctrl_ops,
	.id		= V4L2_CID_TEST_PATTERN_COLOR,
	.type		= V4L2_CTRL_TYPE_INTEGER,
	.name		= "Test Pattern Color",
	.min		= 0,
	.max		= 1023,
	.step		= 1,
	.def		= 0,
	.flags		= 0,
};

static const struct v4l2_ctrl_config mt9v032_aegc_controls[] = {
	{
		.ops		= &mt9v032_ctrl_ops,
		.id		= V4L2_CID_AEGC_DESIRED_BIN,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "AEC/AGC Desired Bin",
		.min		= 1,
		.max		= 64,
		.step		= 1,
		.def		= 58,
		.flags		= 0,
	}, {
		.ops		= &mt9v032_ctrl_ops,
		.id		= V4L2_CID_AEC_LPF,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "AEC Low Pass Filter",
		.min		= 0,
		.max		= 2,
		.step		= 1,
		.def		= 0,
		.flags		= 0,
	}, {
		.ops		= &mt9v032_ctrl_ops,
		.id		= V4L2_CID_AGC_LPF,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "AGC Low Pass Filter",
		.min		= 0,
		.max		= 2,
		.step		= 1,
		.def		= 2,
		.flags		= 0,
	}, {
		.ops		= &mt9v032_ctrl_ops,
		.id		= V4L2_CID_AEC_UPDATE_INTERVAL,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "AEC Update Interval",
		.min		= 0,
		.max		= 16,
		.step		= 1,
		.def		= 2,
		.flags		= 0,
	}, {
		.ops		= &mt9v032_ctrl_ops,
		.id		= V4L2_CID_AGC_UPDATE_INTERVAL,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "AGC Update Interval",
		.min		= 0,
		.max		= 16,
		.step		= 1,
		.def		= 2,
		.flags		= 0,
	}
};

static const struct v4l2_ctrl_config mt9v032_aec_max_shutter_width = {
	.ops		= &mt9v032_ctrl_ops,
	.id		= V4L2_CID_AEC_MAX_SHUTTER_WIDTH,
	.type		= V4L2_CTRL_TYPE_INTEGER,
	.name		= "AEC Max Shutter Width",
	.min		= 1,
	.max		= 2047,
	.step		= 1,
	.def		= 480,
	.flags		= 0,
};

static const struct v4l2_ctrl_config mt9v034_aec_max_shutter_width = {
	.ops		= &mt9v032_ctrl_ops,
	.id		= V4L2_CID_AEC_MAX_SHUTTER_WIDTH,
	.type		= V4L2_CTRL_TYPE_INTEGER,
	.name		= "AEC Max Shutter Width",
	.min		= 1,
	.max		= 32765,
	.step		= 1,
	.def		= 480,
	.flags		= 0,
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

static int mt9v032_set_power(struct v4l2_subdev *subdev, int on)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);
	int ret = 0;

	mutex_lock(&mt9v032->power_lock);

	/* If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (mt9v032->power_count == !on) {
		ret = __mt9v032_set_power(mt9v032, !!on);
		if (ret < 0)
			goto done;
	}

	/* Update the power count. */
	mt9v032->power_count += on ? 1 : -1;
	WARN_ON(mt9v032->power_count < 0);

done:
	mutex_unlock(&mt9v032->power_lock);
	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

static int mt9v032_registered(struct v4l2_subdev *subdev)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);
	unsigned int i;
	u32 version;
	int ret;

	dev_info(&client->dev, "Probing MT9V032 at address 0x%02x\n",
			client->addr);

	ret = mt9v032_power_on(mt9v032);
	if (ret < 0) {
		dev_err(&client->dev, "MT9V032 power up failed\n");
		return ret;
	}

	/* Read and check the sensor version */
	ret = regmap_read(mt9v032->regmap, MT9V032_CHIP_VERSION, &version);

	mt9v032_power_off(mt9v032);

	if (ret < 0) {
		dev_err(&client->dev, "Failed reading chip version\n");
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(mt9v032_versions); ++i) {
		if (mt9v032_versions[i].version == version) {
			mt9v032->version = &mt9v032_versions[i];
			break;
		}
	}

	if (mt9v032->version == NULL) {
		dev_err(&client->dev, "Unsupported chip version 0x%04x\n",
			version);
		return -ENODEV;
	}

	dev_info(&client->dev, "%s detected at address 0x%02x\n",
		 mt9v032->version->name, client->addr);

	mt9v032_configure_pixel_rate(mt9v032);

	return ret;
}

static int mt9v032_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	crop = v4l2_subdev_get_try_crop(subdev, fh->pad, 0);
	crop->left = MT9V032_COLUMN_START_DEF;
	crop->top = MT9V032_ROW_START_DEF;
	crop->width = MT9V032_WINDOW_WIDTH_DEF;
	crop->height = MT9V032_WINDOW_HEIGHT_DEF;

	format = v4l2_subdev_get_try_format(subdev, fh->pad, 0);

	if (mt9v032->model->color)
		format->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	else
		format->code = MEDIA_BUS_FMT_Y10_1X10;

	format->width = MT9V032_WINDOW_WIDTH_DEF;
	format->height = MT9V032_WINDOW_HEIGHT_DEF;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	return mt9v032_set_power(subdev, 1);
}

static int mt9v032_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return mt9v032_set_power(subdev, 0);
}

static const struct v4l2_subdev_core_ops mt9v032_subdev_core_ops = {
	.s_power	= mt9v032_set_power,
};

static const struct v4l2_subdev_video_ops mt9v032_subdev_video_ops = {
	.s_stream	= mt9v032_s_stream,
};

static const struct v4l2_subdev_pad_ops mt9v032_subdev_pad_ops = {
	.enum_mbus_code = mt9v032_enum_mbus_code,
	.enum_frame_size = mt9v032_enum_frame_size,
	.get_fmt = mt9v032_get_format,
	.set_fmt = mt9v032_set_format,
	.get_selection = mt9v032_get_selection,
	.set_selection = mt9v032_set_selection,
};

static const struct v4l2_subdev_ops mt9v032_subdev_ops = {
	.core	= &mt9v032_subdev_core_ops,
	.video	= &mt9v032_subdev_video_ops,
	.pad	= &mt9v032_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops mt9v032_subdev_internal_ops = {
	.registered = mt9v032_registered,
	.open = mt9v032_open,
	.close = mt9v032_close,
};

static const struct regmap_config mt9v032_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0xff,
	.cache_type = REGCACHE_RBTREE,
};

/* -----------------------------------------------------------------------------
 * Driver initialization and probing
 */

static struct mt9v032_platform_data *
mt9v032_get_pdata(struct i2c_client *client)
{
	struct mt9v032_platform_data *pdata = NULL;
	struct v4l2_fwnode_endpoint endpoint;
	struct device_node *np;
	struct property *prop;

	if (!IS_ENABLED(CONFIG_OF) || !client->dev.of_node)
		return client->dev.platform_data;

	np = of_graph_get_next_endpoint(client->dev.of_node, NULL);
	if (!np)
		return NULL;

	if (v4l2_fwnode_endpoint_parse(of_fwnode_handle(np), &endpoint) < 0)
		goto done;

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		goto done;

	prop = of_find_property(np, "link-frequencies", NULL);
	if (prop) {
		u64 *link_freqs;
		size_t size = prop->length / sizeof(*link_freqs);

		link_freqs = devm_kcalloc(&client->dev, size,
					  sizeof(*link_freqs), GFP_KERNEL);
		if (!link_freqs)
			goto done;

		if (of_property_read_u64_array(np, "link-frequencies",
					       link_freqs, size) < 0)
			goto done;

		pdata->link_freqs = link_freqs;
		pdata->link_def_freq = link_freqs[0];
	}

	pdata->clk_pol = !!(endpoint.bus.parallel.flags &
			    V4L2_MBUS_PCLK_SAMPLE_RISING);

done:
	of_node_put(np);
	return pdata;
}

static int mt9v032_probe(struct i2c_client *client,
		const struct i2c_device_id *did)
{
	struct mt9v032_platform_data *pdata = mt9v032_get_pdata(client);
	struct mt9v032 *mt9v032;
	unsigned int i;
	int ret;

	mt9v032 = devm_kzalloc(&client->dev, sizeof(*mt9v032), GFP_KERNEL);
	if (!mt9v032)
		return -ENOMEM;

	mt9v032->regmap = devm_regmap_init_i2c(client, &mt9v032_regmap_config);
	if (IS_ERR(mt9v032->regmap))
		return PTR_ERR(mt9v032->regmap);

	mt9v032->clk = devm_clk_get(&client->dev, NULL);
	if (IS_ERR(mt9v032->clk))
		return PTR_ERR(mt9v032->clk);

	mt9v032->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(mt9v032->reset_gpio))
		return PTR_ERR(mt9v032->reset_gpio);

	mt9v032->standby_gpio = devm_gpiod_get_optional(&client->dev, "standby",
							GPIOD_OUT_LOW);
	if (IS_ERR(mt9v032->standby_gpio))
		return PTR_ERR(mt9v032->standby_gpio);

	mutex_init(&mt9v032->power_lock);
	mt9v032->pdata = pdata;
	mt9v032->model = (const void *)did->driver_data;

	v4l2_ctrl_handler_init(&mt9v032->ctrls, 11 +
			       ARRAY_SIZE(mt9v032_aegc_controls));

	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_GAIN, MT9V032_ANALOG_GAIN_MIN,
			  MT9V032_ANALOG_GAIN_MAX, 1, MT9V032_ANALOG_GAIN_DEF);
	v4l2_ctrl_new_std_menu(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			       V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL, 0,
			       V4L2_EXPOSURE_AUTO);
	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_EXPOSURE, mt9v032->model->data->min_shutter,
			  mt9v032->model->data->max_shutter, 1,
			  MT9V032_TOTAL_SHUTTER_WIDTH_DEF);
	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_HBLANK, mt9v032->model->data->min_hblank,
			  MT9V032_HORIZONTAL_BLANKING_MAX, 1,
			  MT9V032_HORIZONTAL_BLANKING_DEF);
	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_VBLANK, mt9v032->model->data->min_vblank,
			  mt9v032->model->data->max_vblank, 1,
			  MT9V032_VERTICAL_BLANKING_DEF);
	mt9v032->test_pattern = v4l2_ctrl_new_std_menu_items(&mt9v032->ctrls,
				&mt9v032_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(mt9v032_test_pattern_menu) - 1, 0, 0,
				mt9v032_test_pattern_menu);
	mt9v032->test_pattern_color = v4l2_ctrl_new_custom(&mt9v032->ctrls,
				      &mt9v032_test_pattern_color, NULL);

	v4l2_ctrl_new_custom(&mt9v032->ctrls,
			     mt9v032->model->data->aec_max_shutter_v4l2_ctrl,
			     NULL);
	for (i = 0; i < ARRAY_SIZE(mt9v032_aegc_controls); ++i)
		v4l2_ctrl_new_custom(&mt9v032->ctrls, &mt9v032_aegc_controls[i],
				     NULL);

	v4l2_ctrl_cluster(2, &mt9v032->test_pattern);

	mt9v032->pixel_rate =
		v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
				  V4L2_CID_PIXEL_RATE, 1, INT_MAX, 1, 1);

	if (pdata && pdata->link_freqs) {
		unsigned int def = 0;

		for (i = 0; pdata->link_freqs[i]; ++i) {
			if (pdata->link_freqs[i] == pdata->link_def_freq)
				def = i;
		}

		mt9v032->link_freq =
			v4l2_ctrl_new_int_menu(&mt9v032->ctrls,
					       &mt9v032_ctrl_ops,
					       V4L2_CID_LINK_FREQ, i - 1, def,
					       pdata->link_freqs);
		v4l2_ctrl_cluster(2, &mt9v032->link_freq);
	}


	mt9v032->subdev.ctrl_handler = &mt9v032->ctrls;

	if (mt9v032->ctrls.error) {
		dev_err(&client->dev, "control initialization error %d\n",
			mt9v032->ctrls.error);
		ret = mt9v032->ctrls.error;
		goto err;
	}

	mt9v032->crop.left = MT9V032_COLUMN_START_DEF;
	mt9v032->crop.top = MT9V032_ROW_START_DEF;
	mt9v032->crop.width = MT9V032_WINDOW_WIDTH_DEF;
	mt9v032->crop.height = MT9V032_WINDOW_HEIGHT_DEF;

	if (mt9v032->model->color)
		mt9v032->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;
	else
		mt9v032->format.code = MEDIA_BUS_FMT_Y10_1X10;

	mt9v032->format.width = MT9V032_WINDOW_WIDTH_DEF;
	mt9v032->format.height = MT9V032_WINDOW_HEIGHT_DEF;
	mt9v032->format.field = V4L2_FIELD_NONE;
	mt9v032->format.colorspace = V4L2_COLORSPACE_SRGB;

	mt9v032->hratio = 1;
	mt9v032->vratio = 1;

	mt9v032->aec_agc = MT9V032_AEC_ENABLE | MT9V032_AGC_ENABLE;
	mt9v032->hblank = MT9V032_HORIZONTAL_BLANKING_DEF;
	mt9v032->sysclk = MT9V032_SYSCLK_FREQ_DEF;

	v4l2_i2c_subdev_init(&mt9v032->subdev, client, &mt9v032_subdev_ops);
	mt9v032->subdev.internal_ops = &mt9v032_subdev_internal_ops;
	mt9v032->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	mt9v032->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	mt9v032->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&mt9v032->subdev.entity, 1, &mt9v032->pad);
	if (ret < 0)
		goto err;

	mt9v032->subdev.dev = &client->dev;
	ret = v4l2_async_register_subdev(&mt9v032->subdev);
	if (ret < 0)
		goto err;

	return 0;

err:
	media_entity_cleanup(&mt9v032->subdev.entity);
	v4l2_ctrl_handler_free(&mt9v032->ctrls);
	return ret;
}

static int mt9v032_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&mt9v032->ctrls);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct mt9v032_model_data mt9v032_model_data[] = {
	{
		/* MT9V022, MT9V032 revisions 1/2/3 */
		.min_row_time = 660,
		.min_hblank = MT9V032_HORIZONTAL_BLANKING_MIN,
		.min_vblank = MT9V032_VERTICAL_BLANKING_MIN,
		.max_vblank = MT9V032_VERTICAL_BLANKING_MAX,
		.min_shutter = MT9V032_TOTAL_SHUTTER_WIDTH_MIN,
		.max_shutter = MT9V032_TOTAL_SHUTTER_WIDTH_MAX,
		.pclk_reg = MT9V032_PIXEL_CLOCK,
		.aec_max_shutter_reg = MT9V032_AEC_MAX_SHUTTER_WIDTH,
		.aec_max_shutter_v4l2_ctrl = &mt9v032_aec_max_shutter_width,
	}, {
		/* MT9V024, MT9V034 */
		.min_row_time = 690,
		.min_hblank = MT9V034_HORIZONTAL_BLANKING_MIN,
		.min_vblank = MT9V034_VERTICAL_BLANKING_MIN,
		.max_vblank = MT9V034_VERTICAL_BLANKING_MAX,
		.min_shutter = MT9V034_TOTAL_SHUTTER_WIDTH_MIN,
		.max_shutter = MT9V034_TOTAL_SHUTTER_WIDTH_MAX,
		.pclk_reg = MT9V034_PIXEL_CLOCK,
		.aec_max_shutter_reg = MT9V034_AEC_MAX_SHUTTER_WIDTH,
		.aec_max_shutter_v4l2_ctrl = &mt9v034_aec_max_shutter_width,
	},
};

static const struct mt9v032_model_info mt9v032_models[] = {
	[MT9V032_MODEL_V022_COLOR] = {
		.data = &mt9v032_model_data[0],
		.color = true,
	},
	[MT9V032_MODEL_V022_MONO] = {
		.data = &mt9v032_model_data[0],
		.color = false,
	},
	[MT9V032_MODEL_V024_COLOR] = {
		.data = &mt9v032_model_data[1],
		.color = true,
	},
	[MT9V032_MODEL_V024_MONO] = {
		.data = &mt9v032_model_data[1],
		.color = false,
	},
	[MT9V032_MODEL_V032_COLOR] = {
		.data = &mt9v032_model_data[0],
		.color = true,
	},
	[MT9V032_MODEL_V032_MONO] = {
		.data = &mt9v032_model_data[0],
		.color = false,
	},
	[MT9V032_MODEL_V034_COLOR] = {
		.data = &mt9v032_model_data[1],
		.color = true,
	},
	[MT9V032_MODEL_V034_MONO] = {
		.data = &mt9v032_model_data[1],
		.color = false,
	},
};

static const struct i2c_device_id mt9v032_id[] = {
	{ "mt9v022", (kernel_ulong_t)&mt9v032_models[MT9V032_MODEL_V022_COLOR] },
	{ "mt9v022m", (kernel_ulong_t)&mt9v032_models[MT9V032_MODEL_V022_MONO] },
	{ "mt9v024", (kernel_ulong_t)&mt9v032_models[MT9V032_MODEL_V024_COLOR] },
	{ "mt9v024m", (kernel_ulong_t)&mt9v032_models[MT9V032_MODEL_V024_MONO] },
	{ "mt9v032", (kernel_ulong_t)&mt9v032_models[MT9V032_MODEL_V032_COLOR] },
	{ "mt9v032m", (kernel_ulong_t)&mt9v032_models[MT9V032_MODEL_V032_MONO] },
	{ "mt9v034", (kernel_ulong_t)&mt9v032_models[MT9V032_MODEL_V034_COLOR] },
	{ "mt9v034m", (kernel_ulong_t)&mt9v032_models[MT9V032_MODEL_V034_MONO] },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9v032_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id mt9v032_of_match[] = {
	{ .compatible = "aptina,mt9v022" },
	{ .compatible = "aptina,mt9v022m" },
	{ .compatible = "aptina,mt9v024" },
	{ .compatible = "aptina,mt9v024m" },
	{ .compatible = "aptina,mt9v032" },
	{ .compatible = "aptina,mt9v032m" },
	{ .compatible = "aptina,mt9v034" },
	{ .compatible = "aptina,mt9v034m" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt9v032_of_match);
#endif

static struct i2c_driver mt9v032_driver = {
	.driver = {
		.name = "mt9v032",
		.of_match_table = of_match_ptr(mt9v032_of_match),
	},
	.probe		= mt9v032_probe,
	.remove		= mt9v032_remove,
	.id_table	= mt9v032_id,
};

module_i2c_driver(mt9v032_driver);

MODULE_DESCRIPTION("Aptina MT9V032 Camera driver");
MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_LICENSE("GPL");
