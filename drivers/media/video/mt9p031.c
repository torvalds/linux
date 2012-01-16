/*
 * Driver for MT9P031 CMOS Image Sensor from Aptina
 *
 * Copyright (C) 2011, Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 * Copyright (C) 2011, Javier Martin <javier.martin@vista-silicon.com>
 * Copyright (C) 2011, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * Based on the MT9V032 driver and Bastian Hecht's code.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <media/v4l2-subdev.h>
#include <linux/videodev2.h>

#include <media/mt9p031.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#define MT9P031_PIXEL_ARRAY_WIDTH			2752
#define MT9P031_PIXEL_ARRAY_HEIGHT			2004

#define MT9P031_CHIP_VERSION				0x00
#define		MT9P031_CHIP_VERSION_VALUE		0x1801
#define MT9P031_ROW_START				0x01
#define		MT9P031_ROW_START_MIN			0
#define		MT9P031_ROW_START_MAX			2004
#define		MT9P031_ROW_START_DEF			54
#define MT9P031_COLUMN_START				0x02
#define		MT9P031_COLUMN_START_MIN		0
#define		MT9P031_COLUMN_START_MAX		2750
#define		MT9P031_COLUMN_START_DEF		16
#define MT9P031_WINDOW_HEIGHT				0x03
#define		MT9P031_WINDOW_HEIGHT_MIN		2
#define		MT9P031_WINDOW_HEIGHT_MAX		2006
#define		MT9P031_WINDOW_HEIGHT_DEF		1944
#define MT9P031_WINDOW_WIDTH				0x04
#define		MT9P031_WINDOW_WIDTH_MIN		2
#define		MT9P031_WINDOW_WIDTH_MAX		2752
#define		MT9P031_WINDOW_WIDTH_DEF		2592
#define MT9P031_HORIZONTAL_BLANK			0x05
#define		MT9P031_HORIZONTAL_BLANK_MIN		0
#define		MT9P031_HORIZONTAL_BLANK_MAX		4095
#define MT9P031_VERTICAL_BLANK				0x06
#define		MT9P031_VERTICAL_BLANK_MIN		0
#define		MT9P031_VERTICAL_BLANK_MAX		4095
#define		MT9P031_VERTICAL_BLANK_DEF		25
#define MT9P031_OUTPUT_CONTROL				0x07
#define		MT9P031_OUTPUT_CONTROL_CEN		2
#define		MT9P031_OUTPUT_CONTROL_SYN		1
#define		MT9P031_OUTPUT_CONTROL_DEF		0x1f82
#define MT9P031_SHUTTER_WIDTH_UPPER			0x08
#define MT9P031_SHUTTER_WIDTH_LOWER			0x09
#define		MT9P031_SHUTTER_WIDTH_MIN		1
#define		MT9P031_SHUTTER_WIDTH_MAX		1048575
#define		MT9P031_SHUTTER_WIDTH_DEF		1943
#define	MT9P031_PLL_CONTROL				0x10
#define		MT9P031_PLL_CONTROL_PWROFF		0x0050
#define		MT9P031_PLL_CONTROL_PWRON		0x0051
#define		MT9P031_PLL_CONTROL_USEPLL		0x0052
#define	MT9P031_PLL_CONFIG_1				0x11
#define	MT9P031_PLL_CONFIG_2				0x12
#define MT9P031_PIXEL_CLOCK_CONTROL			0x0a
#define MT9P031_FRAME_RESTART				0x0b
#define MT9P031_SHUTTER_DELAY				0x0c
#define MT9P031_RST					0x0d
#define		MT9P031_RST_ENABLE			1
#define		MT9P031_RST_DISABLE			0
#define MT9P031_READ_MODE_1				0x1e
#define MT9P031_READ_MODE_2				0x20
#define		MT9P031_READ_MODE_2_ROW_MIR		(1 << 15)
#define		MT9P031_READ_MODE_2_COL_MIR		(1 << 14)
#define		MT9P031_READ_MODE_2_ROW_BLC		(1 << 6)
#define MT9P031_ROW_ADDRESS_MODE			0x22
#define MT9P031_COLUMN_ADDRESS_MODE			0x23
#define MT9P031_GLOBAL_GAIN				0x35
#define		MT9P031_GLOBAL_GAIN_MIN			8
#define		MT9P031_GLOBAL_GAIN_MAX			1024
#define		MT9P031_GLOBAL_GAIN_DEF			8
#define		MT9P031_GLOBAL_GAIN_MULT		(1 << 6)
#define MT9P031_ROW_BLACK_DEF_OFFSET			0x4b
#define MT9P031_TEST_PATTERN				0xa0
#define		MT9P031_TEST_PATTERN_SHIFT		3
#define		MT9P031_TEST_PATTERN_ENABLE		(1 << 0)
#define		MT9P031_TEST_PATTERN_DISABLE		(0 << 0)
#define MT9P031_TEST_PATTERN_GREEN			0xa1
#define MT9P031_TEST_PATTERN_RED			0xa2
#define MT9P031_TEST_PATTERN_BLUE			0xa3

struct mt9p031_pll_divs {
	u32 ext_freq;
	u32 target_freq;
	u8 m;
	u8 n;
	u8 p1;
};

struct mt9p031 {
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_rect crop;  /* Sensor window */
	struct v4l2_mbus_framefmt format;
	struct v4l2_ctrl_handler ctrls;
	struct mt9p031_platform_data *pdata;
	struct mutex power_lock; /* lock to protect power_count */
	int power_count;
	u16 xskip;
	u16 yskip;

	const struct mt9p031_pll_divs *pll;

	/* Registers cache */
	u16 output_control;
	u16 mode2;
};

static struct mt9p031 *to_mt9p031(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9p031, subdev);
}

static int mt9p031_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_word_swapped(client, reg);
}

static int mt9p031_write(struct i2c_client *client, u8 reg, u16 data)
{
	return i2c_smbus_write_word_swapped(client, reg, data);
}

static int mt9p031_set_output_control(struct mt9p031 *mt9p031, u16 clear,
				      u16 set)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9p031->subdev);
	u16 value = (mt9p031->output_control & ~clear) | set;
	int ret;

	ret = mt9p031_write(client, MT9P031_OUTPUT_CONTROL, value);
	if (ret < 0)
		return ret;

	mt9p031->output_control = value;
	return 0;
}

static int mt9p031_set_mode2(struct mt9p031 *mt9p031, u16 clear, u16 set)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9p031->subdev);
	u16 value = (mt9p031->mode2 & ~clear) | set;
	int ret;

	ret = mt9p031_write(client, MT9P031_READ_MODE_2, value);
	if (ret < 0)
		return ret;

	mt9p031->mode2 = value;
	return 0;
}

static int mt9p031_reset(struct mt9p031 *mt9p031)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9p031->subdev);
	int ret;

	/* Disable chip output, synchronous option update */
	ret = mt9p031_write(client, MT9P031_RST, MT9P031_RST_ENABLE);
	if (ret < 0)
		return ret;
	ret = mt9p031_write(client, MT9P031_RST, MT9P031_RST_DISABLE);
	if (ret < 0)
		return ret;

	return mt9p031_set_output_control(mt9p031, MT9P031_OUTPUT_CONTROL_CEN,
					  0);
}

/*
 * This static table uses ext_freq and vdd_io values to select suitable
 * PLL dividers m, n and p1 which have been calculated as specifiec in p36
 * of Aptina's mt9p031 datasheet. New values should be added here.
 */
static const struct mt9p031_pll_divs mt9p031_divs[] = {
	/* ext_freq	target_freq	m	n	p1 */
	{21000000,	48000000,	26,	2,	6}
};

static int mt9p031_pll_get_divs(struct mt9p031 *mt9p031)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9p031->subdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(mt9p031_divs); i++) {
		if (mt9p031_divs[i].ext_freq == mt9p031->pdata->ext_freq &&
		  mt9p031_divs[i].target_freq == mt9p031->pdata->target_freq) {
			mt9p031->pll = &mt9p031_divs[i];
			return 0;
		}
	}

	dev_err(&client->dev, "Couldn't find PLL dividers for ext_freq = %d, "
		"target_freq = %d\n", mt9p031->pdata->ext_freq,
		mt9p031->pdata->target_freq);
	return -EINVAL;
}

static int mt9p031_pll_enable(struct mt9p031 *mt9p031)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9p031->subdev);
	int ret;

	ret = mt9p031_write(client, MT9P031_PLL_CONTROL,
			    MT9P031_PLL_CONTROL_PWRON);
	if (ret < 0)
		return ret;

	ret = mt9p031_write(client, MT9P031_PLL_CONFIG_1,
			    (mt9p031->pll->m << 8) | (mt9p031->pll->n - 1));
	if (ret < 0)
		return ret;

	ret = mt9p031_write(client, MT9P031_PLL_CONFIG_2, mt9p031->pll->p1 - 1);
	if (ret < 0)
		return ret;

	usleep_range(1000, 2000);
	ret = mt9p031_write(client, MT9P031_PLL_CONTROL,
			    MT9P031_PLL_CONTROL_PWRON |
			    MT9P031_PLL_CONTROL_USEPLL);
	return ret;
}

static inline int mt9p031_pll_disable(struct mt9p031 *mt9p031)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9p031->subdev);

	return mt9p031_write(client, MT9P031_PLL_CONTROL,
			     MT9P031_PLL_CONTROL_PWROFF);
}

static int mt9p031_power_on(struct mt9p031 *mt9p031)
{
	/* Ensure RESET_BAR is low */
	if (mt9p031->pdata->reset) {
		mt9p031->pdata->reset(&mt9p031->subdev, 1);
		usleep_range(1000, 2000);
	}

	/* Emable clock */
	if (mt9p031->pdata->set_xclk)
		mt9p031->pdata->set_xclk(&mt9p031->subdev,
					 mt9p031->pdata->ext_freq);

	/* Now RESET_BAR must be high */
	if (mt9p031->pdata->reset) {
		mt9p031->pdata->reset(&mt9p031->subdev, 0);
		usleep_range(1000, 2000);
	}

	return 0;
}

static void mt9p031_power_off(struct mt9p031 *mt9p031)
{
	if (mt9p031->pdata->reset) {
		mt9p031->pdata->reset(&mt9p031->subdev, 1);
		usleep_range(1000, 2000);
	}

	if (mt9p031->pdata->set_xclk)
		mt9p031->pdata->set_xclk(&mt9p031->subdev, 0);
}

static int __mt9p031_set_power(struct mt9p031 *mt9p031, bool on)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9p031->subdev);
	int ret;

	if (!on) {
		mt9p031_power_off(mt9p031);
		return 0;
	}

	ret = mt9p031_power_on(mt9p031);
	if (ret < 0)
		return ret;

	ret = mt9p031_reset(mt9p031);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to reset the camera\n");
		return ret;
	}

	return v4l2_ctrl_handler_setup(&mt9p031->ctrls);
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev video operations
 */

static int mt9p031_set_params(struct mt9p031 *mt9p031)
{
	struct i2c_client *client = v4l2_get_subdevdata(&mt9p031->subdev);
	struct v4l2_mbus_framefmt *format = &mt9p031->format;
	const struct v4l2_rect *crop = &mt9p031->crop;
	unsigned int hblank;
	unsigned int vblank;
	unsigned int xskip;
	unsigned int yskip;
	unsigned int xbin;
	unsigned int ybin;
	int ret;

	/* Windows position and size.
	 *
	 * TODO: Make sure the start coordinates and window size match the
	 * skipping, binning and mirroring (see description of registers 2 and 4
	 * in table 13, and Binning section on page 41).
	 */
	ret = mt9p031_write(client, MT9P031_COLUMN_START, crop->left);
	if (ret < 0)
		return ret;
	ret = mt9p031_write(client, MT9P031_ROW_START, crop->top);
	if (ret < 0)
		return ret;
	ret = mt9p031_write(client, MT9P031_WINDOW_WIDTH, crop->width - 1);
	if (ret < 0)
		return ret;
	ret = mt9p031_write(client, MT9P031_WINDOW_HEIGHT, crop->height - 1);
	if (ret < 0)
		return ret;

	/* Row and column binning and skipping. Use the maximum binning value
	 * compatible with the skipping settings.
	 */
	xskip = DIV_ROUND_CLOSEST(crop->width, format->width);
	yskip = DIV_ROUND_CLOSEST(crop->height, format->height);
	xbin = 1 << (ffs(xskip) - 1);
	ybin = 1 << (ffs(yskip) - 1);

	ret = mt9p031_write(client, MT9P031_COLUMN_ADDRESS_MODE,
			    ((xbin - 1) << 4) | (xskip - 1));
	if (ret < 0)
		return ret;
	ret = mt9p031_write(client, MT9P031_ROW_ADDRESS_MODE,
			    ((ybin - 1) << 4) | (yskip - 1));
	if (ret < 0)
		return ret;

	/* Blanking - use minimum value for horizontal blanking and default
	 * value for vertical blanking.
	 */
	hblank = 346 * ybin + 64 + (80 >> max_t(unsigned int, xbin, 3));
	vblank = MT9P031_VERTICAL_BLANK_DEF;

	ret = mt9p031_write(client, MT9P031_HORIZONTAL_BLANK, hblank);
	if (ret < 0)
		return ret;
	ret = mt9p031_write(client, MT9P031_VERTICAL_BLANK, vblank);
	if (ret < 0)
		return ret;

	return ret;
}

static int mt9p031_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);
	int ret;

	if (!enable) {
		/* Stop sensor readout */
		ret = mt9p031_set_output_control(mt9p031,
						 MT9P031_OUTPUT_CONTROL_CEN, 0);
		if (ret < 0)
			return ret;

		return mt9p031_pll_disable(mt9p031);
	}

	ret = mt9p031_set_params(mt9p031);
	if (ret < 0)
		return ret;

	/* Switch to master "normal" mode */
	ret = mt9p031_set_output_control(mt9p031, 0,
					 MT9P031_OUTPUT_CONTROL_CEN);
	if (ret < 0)
		return ret;

	return mt9p031_pll_enable(mt9p031);
}

static int mt9p031_enum_mbus_code(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);

	if (code->pad || code->index)
		return -EINVAL;

	code->code = mt9p031->format.code;
	return 0;
}

static int mt9p031_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);

	if (fse->index >= 8 || fse->code != mt9p031->format.code)
		return -EINVAL;

	fse->min_width = MT9P031_WINDOW_WIDTH_DEF
		       / min_t(unsigned int, 7, fse->index + 1);
	fse->max_width = fse->min_width;
	fse->min_height = MT9P031_WINDOW_HEIGHT_DEF / (fse->index + 1);
	fse->max_height = fse->min_height;

	return 0;
}

static struct v4l2_mbus_framefmt *
__mt9p031_get_pad_format(struct mt9p031 *mt9p031, struct v4l2_subdev_fh *fh,
			 unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &mt9p031->format;
	default:
		return NULL;
	}
}

static struct v4l2_rect *
__mt9p031_get_pad_crop(struct mt9p031 *mt9p031, struct v4l2_subdev_fh *fh,
		     unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &mt9p031->crop;
	default:
		return NULL;
	}
}

static int mt9p031_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);

	fmt->format = *__mt9p031_get_pad_format(mt9p031, fh, fmt->pad,
						fmt->which);
	return 0;
}

static int mt9p031_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *format)
{
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_rect *__crop;
	unsigned int width;
	unsigned int height;
	unsigned int hratio;
	unsigned int vratio;

	__crop = __mt9p031_get_pad_crop(mt9p031, fh, format->pad,
					format->which);

	/* Clamp the width and height to avoid dividing by zero. */
	width = clamp_t(unsigned int, ALIGN(format->format.width, 2),
			max(__crop->width / 7, MT9P031_WINDOW_WIDTH_MIN),
			__crop->width);
	height = clamp_t(unsigned int, ALIGN(format->format.height, 2),
			max(__crop->height / 8, MT9P031_WINDOW_HEIGHT_MIN),
			__crop->height);

	hratio = DIV_ROUND_CLOSEST(__crop->width, width);
	vratio = DIV_ROUND_CLOSEST(__crop->height, height);

	__format = __mt9p031_get_pad_format(mt9p031, fh, format->pad,
					    format->which);
	__format->width = __crop->width / hratio;
	__format->height = __crop->height / vratio;

	format->format = *__format;

	return 0;
}

static int mt9p031_get_crop(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_fh *fh,
			    struct v4l2_subdev_crop *crop)
{
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);

	crop->rect = *__mt9p031_get_pad_crop(mt9p031, fh, crop->pad,
					     crop->which);
	return 0;
}

static int mt9p031_set_crop(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_fh *fh,
			    struct v4l2_subdev_crop *crop)
{
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);
	struct v4l2_mbus_framefmt *__format;
	struct v4l2_rect *__crop;
	struct v4l2_rect rect;

	/* Clamp the crop rectangle boundaries and align them to a multiple of 2
	 * pixels to ensure a GRBG Bayer pattern.
	 */
	rect.left = clamp(ALIGN(crop->rect.left, 2), MT9P031_COLUMN_START_MIN,
			  MT9P031_COLUMN_START_MAX);
	rect.top = clamp(ALIGN(crop->rect.top, 2), MT9P031_ROW_START_MIN,
			 MT9P031_ROW_START_MAX);
	rect.width = clamp(ALIGN(crop->rect.width, 2),
			   MT9P031_WINDOW_WIDTH_MIN,
			   MT9P031_WINDOW_WIDTH_MAX);
	rect.height = clamp(ALIGN(crop->rect.height, 2),
			    MT9P031_WINDOW_HEIGHT_MIN,
			    MT9P031_WINDOW_HEIGHT_MAX);

	rect.width = min(rect.width, MT9P031_PIXEL_ARRAY_WIDTH - rect.left);
	rect.height = min(rect.height, MT9P031_PIXEL_ARRAY_HEIGHT - rect.top);

	__crop = __mt9p031_get_pad_crop(mt9p031, fh, crop->pad, crop->which);

	if (rect.width != __crop->width || rect.height != __crop->height) {
		/* Reset the output image size if the crop rectangle size has
		 * been modified.
		 */
		__format = __mt9p031_get_pad_format(mt9p031, fh, crop->pad,
						    crop->which);
		__format->width = rect.width;
		__format->height = rect.height;
	}

	*__crop = rect;
	crop->rect = rect;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev control operations
 */

#define V4L2_CID_TEST_PATTERN		(V4L2_CID_USER_BASE | 0x1001)

static int mt9p031_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9p031 *mt9p031 =
			container_of(ctrl->handler, struct mt9p031, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&mt9p031->subdev);
	u16 data;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = mt9p031_write(client, MT9P031_SHUTTER_WIDTH_UPPER,
				    (ctrl->val >> 16) & 0xffff);
		if (ret < 0)
			return ret;

		return mt9p031_write(client, MT9P031_SHUTTER_WIDTH_LOWER,
				     ctrl->val & 0xffff);

	case V4L2_CID_GAIN:
		/* Gain is controlled by 2 analog stages and a digital stage.
		 * Valid values for the 3 stages are
		 *
		 * Stage                Min     Max     Step
		 * ------------------------------------------
		 * First analog stage   x1      x2      1
		 * Second analog stage  x1      x4      0.125
		 * Digital stage        x1      x16     0.125
		 *
		 * To minimize noise, the gain stages should be used in the
		 * second analog stage, first analog stage, digital stage order.
		 * Gain from a previous stage should be pushed to its maximum
		 * value before the next stage is used.
		 */
		if (ctrl->val <= 32) {
			data = ctrl->val;
		} else if (ctrl->val <= 64) {
			ctrl->val &= ~1;
			data = (1 << 6) | (ctrl->val >> 1);
		} else {
			ctrl->val &= ~7;
			data = ((ctrl->val - 64) << 5) | (1 << 6) | 32;
		}

		return mt9p031_write(client, MT9P031_GLOBAL_GAIN, data);

	case V4L2_CID_HFLIP:
		if (ctrl->val)
			return mt9p031_set_mode2(mt9p031,
					0, MT9P031_READ_MODE_2_COL_MIR);
		else
			return mt9p031_set_mode2(mt9p031,
					MT9P031_READ_MODE_2_COL_MIR, 0);

	case V4L2_CID_VFLIP:
		if (ctrl->val)
			return mt9p031_set_mode2(mt9p031,
					0, MT9P031_READ_MODE_2_ROW_MIR);
		else
			return mt9p031_set_mode2(mt9p031,
					MT9P031_READ_MODE_2_ROW_MIR, 0);

	case V4L2_CID_TEST_PATTERN:
		if (!ctrl->val) {
			ret = mt9p031_set_mode2(mt9p031,
					0, MT9P031_READ_MODE_2_ROW_BLC);
			if (ret < 0)
				return ret;

			return mt9p031_write(client, MT9P031_TEST_PATTERN,
					     MT9P031_TEST_PATTERN_DISABLE);
		}

		ret = mt9p031_write(client, MT9P031_TEST_PATTERN_GREEN, 0x05a0);
		if (ret < 0)
			return ret;
		ret = mt9p031_write(client, MT9P031_TEST_PATTERN_RED, 0x0a50);
		if (ret < 0)
			return ret;
		ret = mt9p031_write(client, MT9P031_TEST_PATTERN_BLUE, 0x0aa0);
		if (ret < 0)
			return ret;

		ret = mt9p031_set_mode2(mt9p031, MT9P031_READ_MODE_2_ROW_BLC,
					0);
		if (ret < 0)
			return ret;
		ret = mt9p031_write(client, MT9P031_ROW_BLACK_DEF_OFFSET, 0);
		if (ret < 0)
			return ret;

		return mt9p031_write(client, MT9P031_TEST_PATTERN,
				((ctrl->val - 1) << MT9P031_TEST_PATTERN_SHIFT)
				| MT9P031_TEST_PATTERN_ENABLE);
	}
	return 0;
}

static struct v4l2_ctrl_ops mt9p031_ctrl_ops = {
	.s_ctrl = mt9p031_s_ctrl,
};

static const char * const mt9p031_test_pattern_menu[] = {
	"Disabled",
	"Color Field",
	"Horizontal Gradient",
	"Vertical Gradient",
	"Diagonal Gradient",
	"Classic Test Pattern",
	"Walking 1s",
	"Monochrome Horizontal Bars",
	"Monochrome Vertical Bars",
	"Vertical Color Bars",
};

static const struct v4l2_ctrl_config mt9p031_ctrls[] = {
	{
		.ops		= &mt9p031_ctrl_ops,
		.id		= V4L2_CID_TEST_PATTERN,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Test Pattern",
		.min		= 0,
		.max		= ARRAY_SIZE(mt9p031_test_pattern_menu) - 1,
		.step		= 0,
		.def		= 0,
		.flags		= 0,
		.menu_skip_mask	= 0,
		.qmenu		= mt9p031_test_pattern_menu,
	}
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

static int mt9p031_set_power(struct v4l2_subdev *subdev, int on)
{
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);
	int ret = 0;

	mutex_lock(&mt9p031->power_lock);

	/* If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (mt9p031->power_count == !on) {
		ret = __mt9p031_set_power(mt9p031, !!on);
		if (ret < 0)
			goto out;
	}

	/* Update the power count. */
	mt9p031->power_count += on ? 1 : -1;
	WARN_ON(mt9p031->power_count < 0);

out:
	mutex_unlock(&mt9p031->power_lock);
	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

static int mt9p031_registered(struct v4l2_subdev *subdev)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);
	s32 data;
	int ret;

	ret = mt9p031_power_on(mt9p031);
	if (ret < 0) {
		dev_err(&client->dev, "MT9P031 power up failed\n");
		return ret;
	}

	/* Read out the chip version register */
	data = mt9p031_read(client, MT9P031_CHIP_VERSION);
	if (data != MT9P031_CHIP_VERSION_VALUE) {
		dev_err(&client->dev, "MT9P031 not detected, wrong version "
			"0x%04x\n", data);
		return -ENODEV;
	}

	mt9p031_power_off(mt9p031);

	dev_info(&client->dev, "MT9P031 detected at address 0x%02x\n",
		 client->addr);

	return ret;
}

static int mt9p031_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	crop = v4l2_subdev_get_try_crop(fh, 0);
	crop->left = MT9P031_COLUMN_START_DEF;
	crop->top = MT9P031_ROW_START_DEF;
	crop->width = MT9P031_WINDOW_WIDTH_DEF;
	crop->height = MT9P031_WINDOW_HEIGHT_DEF;

	format = v4l2_subdev_get_try_format(fh, 0);

	if (mt9p031->pdata->version == MT9P031_MONOCHROME_VERSION)
		format->code = V4L2_MBUS_FMT_Y12_1X12;
	else
		format->code = V4L2_MBUS_FMT_SGRBG12_1X12;

	format->width = MT9P031_WINDOW_WIDTH_DEF;
	format->height = MT9P031_WINDOW_HEIGHT_DEF;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	mt9p031->xskip = 1;
	mt9p031->yskip = 1;
	return mt9p031_set_power(subdev, 1);
}

static int mt9p031_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return mt9p031_set_power(subdev, 0);
}

static struct v4l2_subdev_core_ops mt9p031_subdev_core_ops = {
	.s_power        = mt9p031_set_power,
};

static struct v4l2_subdev_video_ops mt9p031_subdev_video_ops = {
	.s_stream       = mt9p031_s_stream,
};

static struct v4l2_subdev_pad_ops mt9p031_subdev_pad_ops = {
	.enum_mbus_code = mt9p031_enum_mbus_code,
	.enum_frame_size = mt9p031_enum_frame_size,
	.get_fmt = mt9p031_get_format,
	.set_fmt = mt9p031_set_format,
	.get_crop = mt9p031_get_crop,
	.set_crop = mt9p031_set_crop,
};

static struct v4l2_subdev_ops mt9p031_subdev_ops = {
	.core   = &mt9p031_subdev_core_ops,
	.video  = &mt9p031_subdev_video_ops,
	.pad    = &mt9p031_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops mt9p031_subdev_internal_ops = {
	.registered = mt9p031_registered,
	.open = mt9p031_open,
	.close = mt9p031_close,
};

/* -----------------------------------------------------------------------------
 * Driver initialization and probing
 */

static int mt9p031_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9p031_platform_data *pdata = client->dev.platform_data;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct mt9p031 *mt9p031;
	unsigned int i;
	int ret;

	if (pdata == NULL) {
		dev_err(&client->dev, "No platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&client->dev,
			"I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9p031 = kzalloc(sizeof(*mt9p031), GFP_KERNEL);
	if (mt9p031 == NULL)
		return -ENOMEM;

	mt9p031->pdata = pdata;
	mt9p031->output_control	= MT9P031_OUTPUT_CONTROL_DEF;
	mt9p031->mode2 = MT9P031_READ_MODE_2_ROW_BLC;

	v4l2_ctrl_handler_init(&mt9p031->ctrls, ARRAY_SIZE(mt9p031_ctrls) + 4);

	v4l2_ctrl_new_std(&mt9p031->ctrls, &mt9p031_ctrl_ops,
			  V4L2_CID_EXPOSURE, MT9P031_SHUTTER_WIDTH_MIN,
			  MT9P031_SHUTTER_WIDTH_MAX, 1,
			  MT9P031_SHUTTER_WIDTH_DEF);
	v4l2_ctrl_new_std(&mt9p031->ctrls, &mt9p031_ctrl_ops,
			  V4L2_CID_GAIN, MT9P031_GLOBAL_GAIN_MIN,
			  MT9P031_GLOBAL_GAIN_MAX, 1, MT9P031_GLOBAL_GAIN_DEF);
	v4l2_ctrl_new_std(&mt9p031->ctrls, &mt9p031_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&mt9p031->ctrls, &mt9p031_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	for (i = 0; i < ARRAY_SIZE(mt9p031_ctrls); ++i)
		v4l2_ctrl_new_custom(&mt9p031->ctrls, &mt9p031_ctrls[i], NULL);

	mt9p031->subdev.ctrl_handler = &mt9p031->ctrls;

	if (mt9p031->ctrls.error)
		printk(KERN_INFO "%s: control initialization error %d\n",
		       __func__, mt9p031->ctrls.error);

	mutex_init(&mt9p031->power_lock);
	v4l2_i2c_subdev_init(&mt9p031->subdev, client, &mt9p031_subdev_ops);
	mt9p031->subdev.internal_ops = &mt9p031_subdev_internal_ops;

	mt9p031->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&mt9p031->subdev.entity, 1, &mt9p031->pad, 0);
	if (ret < 0)
		goto done;

	mt9p031->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	mt9p031->crop.width = MT9P031_WINDOW_WIDTH_DEF;
	mt9p031->crop.height = MT9P031_WINDOW_HEIGHT_DEF;
	mt9p031->crop.left = MT9P031_COLUMN_START_DEF;
	mt9p031->crop.top = MT9P031_ROW_START_DEF;

	if (mt9p031->pdata->version == MT9P031_MONOCHROME_VERSION)
		mt9p031->format.code = V4L2_MBUS_FMT_Y12_1X12;
	else
		mt9p031->format.code = V4L2_MBUS_FMT_SGRBG12_1X12;

	mt9p031->format.width = MT9P031_WINDOW_WIDTH_DEF;
	mt9p031->format.height = MT9P031_WINDOW_HEIGHT_DEF;
	mt9p031->format.field = V4L2_FIELD_NONE;
	mt9p031->format.colorspace = V4L2_COLORSPACE_SRGB;

	ret = mt9p031_pll_get_divs(mt9p031);

done:
	if (ret < 0) {
		v4l2_ctrl_handler_free(&mt9p031->ctrls);
		media_entity_cleanup(&mt9p031->subdev.entity);
		kfree(mt9p031);
	}

	return ret;
}

static int mt9p031_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct mt9p031 *mt9p031 = to_mt9p031(subdev);

	v4l2_ctrl_handler_free(&mt9p031->ctrls);
	v4l2_device_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	kfree(mt9p031);

	return 0;
}

static const struct i2c_device_id mt9p031_id[] = {
	{ "mt9p031", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9p031_id);

static struct i2c_driver mt9p031_i2c_driver = {
	.driver = {
		.name = "mt9p031",
	},
	.probe          = mt9p031_probe,
	.remove         = mt9p031_remove,
	.id_table       = mt9p031_id,
};

static int __init mt9p031_mod_init(void)
{
	return i2c_add_driver(&mt9p031_i2c_driver);
}

static void __exit mt9p031_mod_exit(void)
{
	i2c_del_driver(&mt9p031_i2c_driver);
}

module_init(mt9p031_mod_init);
module_exit(mt9p031_mod_exit);

MODULE_DESCRIPTION("Aptina MT9P031 Camera driver");
MODULE_AUTHOR("Bastian Hecht <hechtb@gmail.com>");
MODULE_LICENSE("GPL v2");
