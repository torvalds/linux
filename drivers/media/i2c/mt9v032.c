/*
 * Driver for MT9V032 CMOS Image Sensor from Micron
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

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>
#include <linux/module.h>

#include <media/mt9v032.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#define MT9V032_PIXEL_ARRAY_HEIGHT			492
#define MT9V032_PIXEL_ARRAY_WIDTH			782

#define MT9V032_SYSCLK_FREQ_DEF				26600000

#define MT9V032_CHIP_VERSION				0x00
#define		MT9V032_CHIP_ID_REV1			0x1311
#define		MT9V032_CHIP_ID_REV3			0x1313
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
#define		MT9V032_HORIZONTAL_BLANKING_DEF		94
#define		MT9V032_HORIZONTAL_BLANKING_MAX		1023
#define MT9V032_VERTICAL_BLANKING			0x06
#define		MT9V032_VERTICAL_BLANKING_MIN		4
#define		MT9V032_VERTICAL_BLANKING_DEF		45
#define		MT9V032_VERTICAL_BLANKING_MAX		3000
#define MT9V032_CHIP_CONTROL				0x07
#define		MT9V032_CHIP_CONTROL_MASTER_MODE	(1 << 3)
#define		MT9V032_CHIP_CONTROL_DOUT_ENABLE	(1 << 7)
#define		MT9V032_CHIP_CONTROL_SEQUENTIAL		(1 << 8)
#define MT9V032_SHUTTER_WIDTH1				0x08
#define MT9V032_SHUTTER_WIDTH2				0x09
#define MT9V032_SHUTTER_WIDTH_CONTROL			0x0a
#define MT9V032_TOTAL_SHUTTER_WIDTH			0x0b
#define		MT9V032_TOTAL_SHUTTER_WIDTH_MIN		1
#define		MT9V032_TOTAL_SHUTTER_WIDTH_DEF		480
#define		MT9V032_TOTAL_SHUTTER_WIDTH_MAX		32767
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
#define MT9V032_PIXEL_OPERATION_MODE			0x0f
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
#define		MT9V032_ROW_NOISE_CORR_ENABLE		(1 << 5)
#define		MT9V032_ROW_NOISE_CORR_USE_BLK_AVG	(1 << 7)
#define MT9V032_PIXEL_CLOCK				0x74
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
#define MT9V032_AEC_AGC_ENABLE				0xaf
#define		MT9V032_AEC_ENABLE			(1 << 0)
#define		MT9V032_AGC_ENABLE			(1 << 1)
#define MT9V032_THERMAL_INFO				0xc1

struct mt9v032 {
	struct v4l2_subdev subdev;
	struct media_pad pad;

	struct v4l2_mbus_framefmt format;
	struct v4l2_rect crop;

	struct v4l2_ctrl_handler ctrls;
	struct {
		struct v4l2_ctrl *link_freq;
		struct v4l2_ctrl *pixel_rate;
	};

	struct mutex power_lock;
	int power_count;

	struct mt9v032_platform_data *pdata;

	u32 sysclk;
	u16 chip_control;
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

static int mt9v032_read(struct i2c_client *client, const u8 reg)
{
	s32 data = i2c_smbus_read_word_swapped(client, reg);
	dev_dbg(&client->dev, "%s: read 0x%04x from 0x%02x\n", __func__,
		data, reg);
	return data;
}

static int mt9v032_write(struct i2c_client *client, const u8 reg,
			 const u16 data)
{
	dev_dbg(&client->dev, "%s: writing 0x%04x to 0x%02x\n", __func__,
		data, reg);
	return i2c_smbus_write_word_swapped(client, reg, data);
}

static int mt9v032_set_chip_control(struct mt9v032 *mt9v032, u16 clear, u16 set)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9v032->subdev);
	u16 value = (mt9v032->chip_control & ~clear) | set;
	int ret;

	ret = mt9v032_write(client, MT9V032_CHIP_CONTROL, value);
	if (ret < 0)
		return ret;

	mt9v032->chip_control = value;
	return 0;
}

static int
mt9v032_update_aec_agc(struct mt9v032 *mt9v032, u16 which, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9v032->subdev);
	u16 value = mt9v032->aec_agc;
	int ret;

	if (enable)
		value |= which;
	else
		value &= ~which;

	ret = mt9v032_write(client, MT9V032_AEC_AGC_ENABLE, value);
	if (ret < 0)
		return ret;

	mt9v032->aec_agc = value;
	return 0;
}

static int
mt9v032_update_hblank(struct mt9v032 *mt9v032)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9v032->subdev);
	struct v4l2_rect *crop = &mt9v032->crop;

	return mt9v032_write(client, MT9V032_HORIZONTAL_BLANKING,
			     max_t(s32, mt9v032->hblank, 660 - crop->width));
}

#define EXT_CLK		25000000

static int mt9v032_power_on(struct mt9v032 *mt9v032)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9v032->subdev);
	int ret;

	if (mt9v032->pdata->set_clock) {
		mt9v032->pdata->set_clock(&mt9v032->subdev, mt9v032->sysclk);
		udelay(1);
	}

	/* Reset the chip and stop data read out */
	ret = mt9v032_write(client, MT9V032_RESET, 1);
	if (ret < 0)
		return ret;

	ret = mt9v032_write(client, MT9V032_RESET, 0);
	if (ret < 0)
		return ret;

	return mt9v032_write(client, MT9V032_CHIP_CONTROL, 0);
}

static void mt9v032_power_off(struct mt9v032 *mt9v032)
{
	if (mt9v032->pdata->set_clock)
		mt9v032->pdata->set_clock(&mt9v032->subdev, 0);
}

static int __mt9v032_set_power(struct mt9v032 *mt9v032, bool on)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9v032->subdev);
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
		ret = mt9v032_write(client, MT9V032_PIXEL_CLOCK,
				MT9V032_PIXEL_CLOCK_INV_PXL_CLK);
		if (ret < 0)
			return ret;
	}

	/* Disable the noise correction algorithm and restore the controls. */
	ret = mt9v032_write(client, MT9V032_ROW_NOISE_CORR_CONTROL, 0);
	if (ret < 0)
		return ret;

	return v4l2_ctrl_handler_setup(&mt9v032->ctrls);
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev video operations
 */

static struct v4l2_mbus_framefmt *
__mt9v032_get_pad_format(struct mt9v032 *mt9v032, struct v4l2_subdev_fh *fh,
			 unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &mt9v032->format;
	default:
		return NULL;
	}
}

static struct v4l2_rect *
__mt9v032_get_pad_crop(struct mt9v032 *mt9v032, struct v4l2_subdev_fh *fh,
		       unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &mt9v032->crop;
	default:
		return NULL;
	}
}

static int mt9v032_s_stream(struct v4l2_subdev *subdev, int enable)
{
	const u16 mode = MT9V032_CHIP_CONTROL_MASTER_MODE
		       | MT9V032_CHIP_CONTROL_DOUT_ENABLE
		       | MT9V032_CHIP_CONTROL_SEQUENTIAL;
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);
	struct v4l2_mbus_framefmt *format = &mt9v032->format;
	struct v4l2_rect *crop = &mt9v032->crop;
	unsigned int hratio;
	unsigned int vratio;
	int ret;

	if (!enable)
		return mt9v032_set_chip_control(mt9v032, mode, 0);

	/* Configure the window size and row/column bin */
	hratio = DIV_ROUND_CLOSEST(crop->width, format->width);
	vratio = DIV_ROUND_CLOSEST(crop->height, format->height);

	ret = mt9v032_write(client, MT9V032_READ_MODE,
		    (hratio - 1) << MT9V032_READ_MODE_ROW_BIN_SHIFT |
		    (vratio - 1) << MT9V032_READ_MODE_COLUMN_BIN_SHIFT);
	if (ret < 0)
		return ret;

	ret = mt9v032_write(client, MT9V032_COLUMN_START, crop->left);
	if (ret < 0)
		return ret;

	ret = mt9v032_write(client, MT9V032_ROW_START, crop->top);
	if (ret < 0)
		return ret;

	ret = mt9v032_write(client, MT9V032_WINDOW_WIDTH, crop->width);
	if (ret < 0)
		return ret;

	ret = mt9v032_write(client, MT9V032_WINDOW_HEIGHT, crop->height);
	if (ret < 0)
		return ret;

	ret = mt9v032_update_hblank(mt9v032);
	if (ret < 0)
		return ret;

	/* Switch to master "normal" mode */
	return mt9v032_set_chip_control(mt9v032, 0, mode);
}

static int mt9v032_enum_mbus_code(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = V4L2_MBUS_FMT_SGRBG10_1X10;
	return 0;
}

static int mt9v032_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= 8 || fse->code != V4L2_MBUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	fse->min_width = MT9V032_WINDOW_WIDTH_DEF / fse->index;
	fse->max_width = fse->min_width;
	fse->min_height = MT9V032_WINDOW_HEIGHT_DEF / fse->index;
	fse->max_height = fse->min_height;

	return 0;
}

static int mt9v032_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *format)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);

	format->format = *__mt9v032_get_pad_format(mt9v032, fh, format->pad,
						   format->which);
	return 0;
}

static void mt9v032_configure_pixel_rate(struct mt9v032 *mt9v032,
					 unsigned int hratio)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9v032->subdev);
	int ret;

	ret = v4l2_ctrl_s_ctrl_int64(mt9v032->pixel_rate,
				     mt9v032->sysclk / hratio);
	if (ret < 0)
		dev_warn(&client->dev, "failed to set pixel rate (%d)\n", ret);
}

static int mt9v032_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *format)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_rect *__crop;
	unsigned int width;
	unsigned int height;
	unsigned int hratio;
	unsigned int vratio;

	__crop = __mt9v032_get_pad_crop(mt9v032, fh, format->pad,
					format->which);

	/* Clamp the width and height to avoid dividing by zero. */
	width = clamp_t(unsigned int, ALIGN(format->format.width, 2),
			max(__crop->width / 8, MT9V032_WINDOW_WIDTH_MIN),
			__crop->width);
	height = clamp_t(unsigned int, ALIGN(format->format.height, 2),
			 max(__crop->height / 8, MT9V032_WINDOW_HEIGHT_MIN),
			 __crop->height);

	hratio = DIV_ROUND_CLOSEST(__crop->width, width);
	vratio = DIV_ROUND_CLOSEST(__crop->height, height);

	__format = __mt9v032_get_pad_format(mt9v032, fh, format->pad,
					    format->which);
	__format->width = __crop->width / hratio;
	__format->height = __crop->height / vratio;
	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		mt9v032_configure_pixel_rate(mt9v032, hratio);

	format->format = *__format;

	return 0;
}

static int mt9v032_get_crop(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_fh *fh,
			    struct v4l2_subdev_crop *crop)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);

	crop->rect = *__mt9v032_get_pad_crop(mt9v032, fh, crop->pad,
					     crop->which);
	return 0;
}

static int mt9v032_set_crop(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_fh *fh,
			    struct v4l2_subdev_crop *crop)
{
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_rect *__crop;
	struct v4l2_rect rect;

	/* Clamp the crop rectangle boundaries and align them to a non multiple
	 * of 2 pixels to ensure a GRBG Bayer pattern.
	 */
	rect.left = clamp(ALIGN(crop->rect.left + 1, 2) - 1,
			  MT9V032_COLUMN_START_MIN,
			  MT9V032_COLUMN_START_MAX);
	rect.top = clamp(ALIGN(crop->rect.top + 1, 2) - 1,
			 MT9V032_ROW_START_MIN,
			 MT9V032_ROW_START_MAX);
	rect.width = clamp(ALIGN(crop->rect.width, 2),
			   MT9V032_WINDOW_WIDTH_MIN,
			   MT9V032_WINDOW_WIDTH_MAX);
	rect.height = clamp(ALIGN(crop->rect.height, 2),
			    MT9V032_WINDOW_HEIGHT_MIN,
			    MT9V032_WINDOW_HEIGHT_MAX);

	rect.width = min(rect.width, MT9V032_PIXEL_ARRAY_WIDTH - rect.left);
	rect.height = min(rect.height, MT9V032_PIXEL_ARRAY_HEIGHT - rect.top);

	__crop = __mt9v032_get_pad_crop(mt9v032, fh, crop->pad, crop->which);

	if (rect.width != __crop->width || rect.height != __crop->height) {
		/* Reset the output image size if the crop rectangle size has
		 * been modified.
		 */
		__format = __mt9v032_get_pad_format(mt9v032, fh, crop->pad,
						    crop->which);
		__format->width = rect.width;
		__format->height = rect.height;
		if (crop->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			mt9v032_configure_pixel_rate(mt9v032, 1);
	}

	*__crop = rect;
	crop->rect = rect;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev control operations
 */

#define V4L2_CID_TEST_PATTERN_COLOR	(V4L2_CID_USER_BASE | 0x1001)

static int mt9v032_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9v032 *mt9v032 =
			container_of(ctrl->handler, struct mt9v032, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&mt9v032->subdev);
	u32 freq;
	u16 data;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		return mt9v032_update_aec_agc(mt9v032, MT9V032_AGC_ENABLE,
					      ctrl->val);

	case V4L2_CID_GAIN:
		return mt9v032_write(client, MT9V032_ANALOG_GAIN, ctrl->val);

	case V4L2_CID_EXPOSURE_AUTO:
		return mt9v032_update_aec_agc(mt9v032, MT9V032_AEC_ENABLE,
					      !ctrl->val);

	case V4L2_CID_EXPOSURE:
		return mt9v032_write(client, MT9V032_TOTAL_SHUTTER_WIDTH,
				     ctrl->val);

	case V4L2_CID_HBLANK:
		mt9v032->hblank = ctrl->val;
		return mt9v032_update_hblank(mt9v032);

	case V4L2_CID_VBLANK:
		return mt9v032_write(client, MT9V032_VERTICAL_BLANKING,
				     ctrl->val);

	case V4L2_CID_PIXEL_RATE:
	case V4L2_CID_LINK_FREQ:
		if (mt9v032->link_freq == NULL)
			break;

		freq = mt9v032->pdata->link_freqs[mt9v032->link_freq->val];
		mt9v032->pixel_rate->val64 = freq;
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
		return mt9v032_write(client, MT9V032_TEST_PATTERN, data);
	}

	return 0;
}

static struct v4l2_ctrl_ops mt9v032_ctrl_ops = {
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
	s32 data;
	int ret;

	dev_info(&client->dev, "Probing MT9V032 at address 0x%02x\n",
			client->addr);

	ret = mt9v032_power_on(mt9v032);
	if (ret < 0) {
		dev_err(&client->dev, "MT9V032 power up failed\n");
		return ret;
	}

	/* Read and check the sensor version */
	data = mt9v032_read(client, MT9V032_CHIP_VERSION);
	if (data != MT9V032_CHIP_ID_REV1 && data != MT9V032_CHIP_ID_REV3) {
		dev_err(&client->dev, "MT9V032 not detected, wrong version "
				"0x%04x\n", data);
		return -ENODEV;
	}

	mt9v032_power_off(mt9v032);

	dev_info(&client->dev, "MT9V032 detected at address 0x%02x\n",
			client->addr);

	mt9v032_configure_pixel_rate(mt9v032, 1);

	return ret;
}

static int mt9v032_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	crop = v4l2_subdev_get_try_crop(fh, 0);
	crop->left = MT9V032_COLUMN_START_DEF;
	crop->top = MT9V032_ROW_START_DEF;
	crop->width = MT9V032_WINDOW_WIDTH_DEF;
	crop->height = MT9V032_WINDOW_HEIGHT_DEF;

	format = v4l2_subdev_get_try_format(fh, 0);
	format->code = V4L2_MBUS_FMT_SGRBG10_1X10;
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

static struct v4l2_subdev_core_ops mt9v032_subdev_core_ops = {
	.s_power	= mt9v032_set_power,
};

static struct v4l2_subdev_video_ops mt9v032_subdev_video_ops = {
	.s_stream	= mt9v032_s_stream,
};

static struct v4l2_subdev_pad_ops mt9v032_subdev_pad_ops = {
	.enum_mbus_code = mt9v032_enum_mbus_code,
	.enum_frame_size = mt9v032_enum_frame_size,
	.get_fmt = mt9v032_get_format,
	.set_fmt = mt9v032_set_format,
	.get_crop = mt9v032_get_crop,
	.set_crop = mt9v032_set_crop,
};

static struct v4l2_subdev_ops mt9v032_subdev_ops = {
	.core	= &mt9v032_subdev_core_ops,
	.video	= &mt9v032_subdev_video_ops,
	.pad	= &mt9v032_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops mt9v032_subdev_internal_ops = {
	.registered = mt9v032_registered,
	.open = mt9v032_open,
	.close = mt9v032_close,
};

/* -----------------------------------------------------------------------------
 * Driver initialization and probing
 */

static int mt9v032_probe(struct i2c_client *client,
		const struct i2c_device_id *did)
{
	struct mt9v032_platform_data *pdata = client->dev.platform_data;
	struct mt9v032 *mt9v032;
	unsigned int i;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&client->adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9v032 = devm_kzalloc(&client->dev, sizeof(*mt9v032), GFP_KERNEL);
	if (!mt9v032)
		return -ENOMEM;

	mutex_init(&mt9v032->power_lock);
	mt9v032->pdata = pdata;

	v4l2_ctrl_handler_init(&mt9v032->ctrls, 10);

	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_GAIN, MT9V032_ANALOG_GAIN_MIN,
			  MT9V032_ANALOG_GAIN_MAX, 1, MT9V032_ANALOG_GAIN_DEF);
	v4l2_ctrl_new_std_menu(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			       V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL, 0,
			       V4L2_EXPOSURE_AUTO);
	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_EXPOSURE, MT9V032_TOTAL_SHUTTER_WIDTH_MIN,
			  MT9V032_TOTAL_SHUTTER_WIDTH_MAX, 1,
			  MT9V032_TOTAL_SHUTTER_WIDTH_DEF);
	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_HBLANK, MT9V032_HORIZONTAL_BLANKING_MIN,
			  MT9V032_HORIZONTAL_BLANKING_MAX, 1,
			  MT9V032_HORIZONTAL_BLANKING_DEF);
	v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
			  V4L2_CID_VBLANK, MT9V032_VERTICAL_BLANKING_MIN,
			  MT9V032_VERTICAL_BLANKING_MAX, 1,
			  MT9V032_VERTICAL_BLANKING_DEF);
	mt9v032->test_pattern = v4l2_ctrl_new_std_menu_items(&mt9v032->ctrls,
				&mt9v032_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(mt9v032_test_pattern_menu) - 1, 0, 0,
				mt9v032_test_pattern_menu);
	mt9v032->test_pattern_color = v4l2_ctrl_new_custom(&mt9v032->ctrls,
				      &mt9v032_test_pattern_color, NULL);

	v4l2_ctrl_cluster(2, &mt9v032->test_pattern);

	mt9v032->pixel_rate =
		v4l2_ctrl_new_std(&mt9v032->ctrls, &mt9v032_ctrl_ops,
				  V4L2_CID_PIXEL_RATE, 0, 0, 1, 0);

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

	if (mt9v032->ctrls.error)
		printk(KERN_INFO "%s: control initialization error %d\n",
		       __func__, mt9v032->ctrls.error);

	mt9v032->crop.left = MT9V032_COLUMN_START_DEF;
	mt9v032->crop.top = MT9V032_ROW_START_DEF;
	mt9v032->crop.width = MT9V032_WINDOW_WIDTH_DEF;
	mt9v032->crop.height = MT9V032_WINDOW_HEIGHT_DEF;

	mt9v032->format.code = V4L2_MBUS_FMT_SGRBG10_1X10;
	mt9v032->format.width = MT9V032_WINDOW_WIDTH_DEF;
	mt9v032->format.height = MT9V032_WINDOW_HEIGHT_DEF;
	mt9v032->format.field = V4L2_FIELD_NONE;
	mt9v032->format.colorspace = V4L2_COLORSPACE_SRGB;

	mt9v032->aec_agc = MT9V032_AEC_ENABLE | MT9V032_AGC_ENABLE;
	mt9v032->hblank = MT9V032_HORIZONTAL_BLANKING_DEF;
	mt9v032->sysclk = MT9V032_SYSCLK_FREQ_DEF;

	v4l2_i2c_subdev_init(&mt9v032->subdev, client, &mt9v032_subdev_ops);
	mt9v032->subdev.internal_ops = &mt9v032_subdev_internal_ops;
	mt9v032->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	mt9v032->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&mt9v032->subdev.entity, 1, &mt9v032->pad, 0);

	if (ret < 0)
		v4l2_ctrl_handler_free(&mt9v032->ctrls);

	return ret;
}

static int mt9v032_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct mt9v032 *mt9v032 = to_mt9v032(subdev);

	v4l2_ctrl_handler_free(&mt9v032->ctrls);
	v4l2_device_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct i2c_device_id mt9v032_id[] = {
	{ "mt9v032", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9v032_id);

static struct i2c_driver mt9v032_driver = {
	.driver = {
		.name = "mt9v032",
	},
	.probe		= mt9v032_probe,
	.remove		= mt9v032_remove,
	.id_table	= mt9v032_id,
};

module_i2c_driver(mt9v032_driver);

MODULE_DESCRIPTION("Aptina MT9V032 Camera driver");
MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_LICENSE("GPL");
