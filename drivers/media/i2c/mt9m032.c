/*
 * Driver for MT9M032 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2010-2011 Lund Engineering
 * Contact: Gil Lund <gwlund@lundeng.com>
 * Author: Martin Hostettler <martin@neutronstar.dyndns.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>

#include <media/media-entity.h>
#include <media/mt9m032.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "aptina-pll.h"

/*
 * width and height include active boundary and black parts
 *
 * column    0-  15 active boundary
 * column   16-1455 image
 * column 1456-1471 active boundary
 * column 1472-1599 black
 *
 * row       0-  51 black
 * row      53-  59 active boundary
 * row      60-1139 image
 * row    1140-1147 active boundary
 * row    1148-1151 black
 */

#define MT9M032_PIXEL_ARRAY_WIDTH			1600
#define MT9M032_PIXEL_ARRAY_HEIGHT			1152

#define MT9M032_CHIP_VERSION				0x00
#define		MT9M032_CHIP_VERSION_VALUE		0x1402
#define MT9M032_ROW_START				0x01
#define		MT9M032_ROW_START_MIN			0
#define		MT9M032_ROW_START_MAX			1152
#define		MT9M032_ROW_START_DEF			60
#define MT9M032_COLUMN_START				0x02
#define		MT9M032_COLUMN_START_MIN		0
#define		MT9M032_COLUMN_START_MAX		1600
#define		MT9M032_COLUMN_START_DEF		16
#define MT9M032_ROW_SIZE				0x03
#define		MT9M032_ROW_SIZE_MIN			32
#define		MT9M032_ROW_SIZE_MAX			1152
#define		MT9M032_ROW_SIZE_DEF			1080
#define MT9M032_COLUMN_SIZE				0x04
#define		MT9M032_COLUMN_SIZE_MIN			32
#define		MT9M032_COLUMN_SIZE_MAX			1600
#define		MT9M032_COLUMN_SIZE_DEF			1440
#define MT9M032_HBLANK					0x05
#define MT9M032_VBLANK					0x06
#define		MT9M032_VBLANK_MAX			0x7ff
#define MT9M032_SHUTTER_WIDTH_HIGH			0x08
#define MT9M032_SHUTTER_WIDTH_LOW			0x09
#define		MT9M032_SHUTTER_WIDTH_MIN		1
#define		MT9M032_SHUTTER_WIDTH_MAX		1048575
#define		MT9M032_SHUTTER_WIDTH_DEF		1943
#define MT9M032_PIX_CLK_CTRL				0x0a
#define		MT9M032_PIX_CLK_CTRL_INV_PIXCLK		0x8000
#define MT9M032_RESTART					0x0b
#define MT9M032_RESET					0x0d
#define MT9M032_PLL_CONFIG1				0x11
#define		MT9M032_PLL_CONFIG1_PREDIV_MASK		0x3f
#define		MT9M032_PLL_CONFIG1_MUL_SHIFT		8
#define MT9M032_READ_MODE1				0x1e
#define		MT9M032_READ_MODE1_OUTPUT_BAD_FRAMES	(1 << 13)
#define		MT9M032_READ_MODE1_MAINTAIN_FRAME_RATE	(1 << 12)
#define		MT9M032_READ_MODE1_XOR_LINE_VALID	(1 << 11)
#define		MT9M032_READ_MODE1_CONT_LINE_VALID	(1 << 10)
#define		MT9M032_READ_MODE1_INVERT_TRIGGER	(1 << 9)
#define		MT9M032_READ_MODE1_SNAPSHOT		(1 << 8)
#define		MT9M032_READ_MODE1_GLOBAL_RESET		(1 << 7)
#define		MT9M032_READ_MODE1_BULB_EXPOSURE	(1 << 6)
#define		MT9M032_READ_MODE1_INVERT_STROBE	(1 << 5)
#define		MT9M032_READ_MODE1_STROBE_ENABLE	(1 << 4)
#define		MT9M032_READ_MODE1_STROBE_START_TRIG1	(0 << 2)
#define		MT9M032_READ_MODE1_STROBE_START_EXP	(1 << 2)
#define		MT9M032_READ_MODE1_STROBE_START_SHUTTER	(2 << 2)
#define		MT9M032_READ_MODE1_STROBE_START_TRIG2	(3 << 2)
#define		MT9M032_READ_MODE1_STROBE_END_TRIG1	(0 << 0)
#define		MT9M032_READ_MODE1_STROBE_END_EXP	(1 << 0)
#define		MT9M032_READ_MODE1_STROBE_END_SHUTTER	(2 << 0)
#define		MT9M032_READ_MODE1_STROBE_END_TRIG2	(3 << 0)
#define MT9M032_READ_MODE2				0x20
#define		MT9M032_READ_MODE2_VFLIP_SHIFT		15
#define		MT9M032_READ_MODE2_HFLIP_SHIFT		14
#define		MT9M032_READ_MODE2_ROW_BLC		0x40
#define MT9M032_GAIN_GREEN1				0x2b
#define MT9M032_GAIN_BLUE				0x2c
#define MT9M032_GAIN_RED				0x2d
#define MT9M032_GAIN_GREEN2				0x2e

/* write only */
#define MT9M032_GAIN_ALL				0x35
#define		MT9M032_GAIN_DIGITAL_MASK		0x7f
#define		MT9M032_GAIN_DIGITAL_SHIFT		8
#define		MT9M032_GAIN_AMUL_SHIFT			6
#define		MT9M032_GAIN_ANALOG_MASK		0x3f
#define MT9M032_FORMATTER1				0x9e
#define		MT9M032_FORMATTER1_PLL_P1_6		(1 << 8)
#define		MT9M032_FORMATTER1_PARALLEL		(1 << 12)
#define MT9M032_FORMATTER2				0x9f
#define		MT9M032_FORMATTER2_DOUT_EN		0x1000
#define		MT9M032_FORMATTER2_PIXCLK_EN		0x2000

/*
 * The available MT9M032 datasheet is missing documentation for register 0x10
 * MT9P031 seems to be close enough, so use constants from that datasheet for
 * now.
 * But keep the name MT9P031 to remind us, that this isn't really confirmed
 * for this sensor.
 */
#define MT9P031_PLL_CONTROL				0x10
#define		MT9P031_PLL_CONTROL_PWROFF		0x0050
#define		MT9P031_PLL_CONTROL_PWRON		0x0051
#define		MT9P031_PLL_CONTROL_USEPLL		0x0052

struct mt9m032 {
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct mt9m032_platform_data *pdata;

	unsigned int pix_clock;

	struct v4l2_ctrl_handler ctrls;
	struct {
		struct v4l2_ctrl *hflip;
		struct v4l2_ctrl *vflip;
	};

	struct mutex lock; /* Protects streaming, format, interval and crop */

	bool streaming;

	struct v4l2_mbus_framefmt format;
	struct v4l2_rect crop;
	struct v4l2_fract frame_interval;
};

#define to_mt9m032(sd)	container_of(sd, struct mt9m032, subdev)
#define to_dev(sensor) \
	(&((struct i2c_client *)v4l2_get_subdevdata(&(sensor)->subdev))->dev)

static int mt9m032_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_word_swapped(client, reg);
}

static int mt9m032_write(struct i2c_client *client, u8 reg, const u16 data)
{
	return i2c_smbus_write_word_swapped(client, reg, data);
}

static u32 mt9m032_row_time(struct mt9m032 *sensor, unsigned int width)
{
	unsigned int effective_width;
	u32 ns;

	effective_width = width + 716; /* empirical value */
	ns = div_u64(1000000000ULL * effective_width, sensor->pix_clock);
	dev_dbg(to_dev(sensor),	"MT9M032 line time: %u ns\n", ns);
	return ns;
}

static int mt9m032_update_timing(struct mt9m032 *sensor,
				 struct v4l2_fract *interval)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	struct v4l2_rect *crop = &sensor->crop;
	unsigned int min_vblank;
	unsigned int vblank;
	u32 row_time;

	if (!interval)
		interval = &sensor->frame_interval;

	row_time = mt9m032_row_time(sensor, crop->width);

	vblank = div_u64(1000000000ULL * interval->numerator,
			 (u64)row_time * interval->denominator)
	       - crop->height;

	if (vblank > MT9M032_VBLANK_MAX) {
		/* hardware limits to 11 bit values */
		interval->denominator = 1000;
		interval->numerator =
			div_u64((crop->height + MT9M032_VBLANK_MAX) *
				(u64)row_time * interval->denominator,
				1000000000ULL);
		vblank = div_u64(1000000000ULL * interval->numerator,
				 (u64)row_time * interval->denominator)
		       - crop->height;
	}
	/* enforce minimal 1.6ms blanking time. */
	min_vblank = 1600000 / row_time;
	vblank = clamp_t(unsigned int, vblank, min_vblank, MT9M032_VBLANK_MAX);

	return mt9m032_write(client, MT9M032_VBLANK, vblank);
}

static int mt9m032_update_geom_timing(struct mt9m032 *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	int ret;

	ret = mt9m032_write(client, MT9M032_COLUMN_SIZE,
			    sensor->crop.width - 1);
	if (!ret)
		ret = mt9m032_write(client, MT9M032_ROW_SIZE,
				    sensor->crop.height - 1);
	if (!ret)
		ret = mt9m032_write(client, MT9M032_COLUMN_START,
				    sensor->crop.left);
	if (!ret)
		ret = mt9m032_write(client, MT9M032_ROW_START,
				    sensor->crop.top);
	if (!ret)
		ret = mt9m032_update_timing(sensor, NULL);
	return ret;
}

static int update_formatter2(struct mt9m032 *sensor, bool streaming)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	u16 reg_val =   MT9M032_FORMATTER2_DOUT_EN
		      | 0x0070;  /* parts reserved! */
				 /* possibly for changing to 14-bit mode */

	if (streaming)
		reg_val |= MT9M032_FORMATTER2_PIXCLK_EN;   /* pixclock enable */

	return mt9m032_write(client, MT9M032_FORMATTER2, reg_val);
}

static int mt9m032_setup_pll(struct mt9m032 *sensor)
{
	static const struct aptina_pll_limits limits = {
		.ext_clock_min = 8000000,
		.ext_clock_max = 16500000,
		.int_clock_min = 2000000,
		.int_clock_max = 24000000,
		.out_clock_min = 322000000,
		.out_clock_max = 693000000,
		.pix_clock_max = 99000000,
		.n_min = 1,
		.n_max = 64,
		.m_min = 16,
		.m_max = 255,
		.p1_min = 6,
		.p1_max = 7,
	};

	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	struct mt9m032_platform_data *pdata = sensor->pdata;
	struct aptina_pll pll;
	u16 reg_val;
	int ret;

	pll.ext_clock = pdata->ext_clock;
	pll.pix_clock = pdata->pix_clock;

	ret = aptina_pll_calculate(&client->dev, &limits, &pll);
	if (ret < 0)
		return ret;

	sensor->pix_clock = pdata->pix_clock;

	ret = mt9m032_write(client, MT9M032_PLL_CONFIG1,
			    (pll.m << MT9M032_PLL_CONFIG1_MUL_SHIFT) |
			    ((pll.n - 1) & MT9M032_PLL_CONFIG1_PREDIV_MASK));
	if (!ret)
		ret = mt9m032_write(client, MT9P031_PLL_CONTROL,
				    MT9P031_PLL_CONTROL_PWRON |
				    MT9P031_PLL_CONTROL_USEPLL);
	if (!ret)		/* more reserved, Continuous, Master Mode */
		ret = mt9m032_write(client, MT9M032_READ_MODE1, 0x8000 |
				    MT9M032_READ_MODE1_STROBE_START_EXP |
				    MT9M032_READ_MODE1_STROBE_END_SHUTTER);
	if (!ret) {
		reg_val = (pll.p1 == 6 ? MT9M032_FORMATTER1_PLL_P1_6 : 0)
			| MT9M032_FORMATTER1_PARALLEL | 0x001e; /* 14-bit */
		ret = mt9m032_write(client, MT9M032_FORMATTER1, reg_val);
	}

	return ret;
}

/* -----------------------------------------------------------------------------
 * Subdev pad operations
 */

static int mt9m032_enum_mbus_code(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_Y8_1X8;
	return 0;
}

static int mt9m032_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index != 0 || fse->code != MEDIA_BUS_FMT_Y8_1X8)
		return -EINVAL;

	fse->min_width = MT9M032_COLUMN_SIZE_DEF;
	fse->max_width = MT9M032_COLUMN_SIZE_DEF;
	fse->min_height = MT9M032_ROW_SIZE_DEF;
	fse->max_height = MT9M032_ROW_SIZE_DEF;

	return 0;
}

/**
 * __mt9m032_get_pad_crop() - get crop rect
 * @sensor: pointer to the sensor struct
 * @fh: file handle for getting the try crop rect from
 * @which: select try or active crop rect
 *
 * Returns a pointer the current active or fh relative try crop rect
 */
static struct v4l2_rect *
__mt9m032_get_pad_crop(struct mt9m032 *sensor, struct v4l2_subdev_fh *fh,
		       enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(fh, 0);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->crop;
	default:
		return NULL;
	}
}

/**
 * __mt9m032_get_pad_format() - get format
 * @sensor: pointer to the sensor struct
 * @fh: file handle for getting the try format from
 * @which: select try or active format
 *
 * Returns a pointer the current active or fh relative try format
 */
static struct v4l2_mbus_framefmt *
__mt9m032_get_pad_format(struct mt9m032 *sensor, struct v4l2_subdev_fh *fh,
			 enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, 0);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->format;
	default:
		return NULL;
	}
}

static int mt9m032_get_pad_format(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_format *fmt)
{
	struct mt9m032 *sensor = to_mt9m032(subdev);

	mutex_lock(&sensor->lock);
	fmt->format = *__mt9m032_get_pad_format(sensor, fh, fmt->which);
	mutex_unlock(&sensor->lock);

	return 0;
}

static int mt9m032_set_pad_format(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_format *fmt)
{
	struct mt9m032 *sensor = to_mt9m032(subdev);
	int ret;

	mutex_lock(&sensor->lock);

	if (sensor->streaming && fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ret = -EBUSY;
		goto done;
	}

	/* Scaling is not supported, the format is thus fixed. */
	fmt->format = *__mt9m032_get_pad_format(sensor, fh, fmt->which);
	ret = 0;

done:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int mt9m032_get_pad_crop(struct v4l2_subdev *subdev,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_crop *crop)
{
	struct mt9m032 *sensor = to_mt9m032(subdev);

	mutex_lock(&sensor->lock);
	crop->rect = *__mt9m032_get_pad_crop(sensor, fh, crop->which);
	mutex_unlock(&sensor->lock);

	return 0;
}

static int mt9m032_set_pad_crop(struct v4l2_subdev *subdev,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_crop *crop)
{
	struct mt9m032 *sensor = to_mt9m032(subdev);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *__crop;
	struct v4l2_rect rect;
	int ret = 0;

	mutex_lock(&sensor->lock);

	if (sensor->streaming && crop->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ret = -EBUSY;
		goto done;
	}

	/* Clamp the crop rectangle boundaries and align them to a multiple of 2
	 * pixels to ensure a GRBG Bayer pattern.
	 */
	rect.left = clamp(ALIGN(crop->rect.left, 2), MT9M032_COLUMN_START_MIN,
			  MT9M032_COLUMN_START_MAX);
	rect.top = clamp(ALIGN(crop->rect.top, 2), MT9M032_ROW_START_MIN,
			 MT9M032_ROW_START_MAX);
	rect.width = clamp_t(unsigned int, ALIGN(crop->rect.width, 2),
			     MT9M032_COLUMN_SIZE_MIN, MT9M032_COLUMN_SIZE_MAX);
	rect.height = clamp_t(unsigned int, ALIGN(crop->rect.height, 2),
			      MT9M032_ROW_SIZE_MIN, MT9M032_ROW_SIZE_MAX);

	rect.width = min_t(unsigned int, rect.width,
			   MT9M032_PIXEL_ARRAY_WIDTH - rect.left);
	rect.height = min_t(unsigned int, rect.height,
			    MT9M032_PIXEL_ARRAY_HEIGHT - rect.top);

	__crop = __mt9m032_get_pad_crop(sensor, fh, crop->which);

	if (rect.width != __crop->width || rect.height != __crop->height) {
		/* Reset the output image size if the crop rectangle size has
		 * been modified.
		 */
		format = __mt9m032_get_pad_format(sensor, fh, crop->which);
		format->width = rect.width;
		format->height = rect.height;
	}

	*__crop = rect;
	crop->rect = rect;

	if (crop->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		ret = mt9m032_update_geom_timing(sensor);

done:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int mt9m032_get_frame_interval(struct v4l2_subdev *subdev,
				      struct v4l2_subdev_frame_interval *fi)
{
	struct mt9m032 *sensor = to_mt9m032(subdev);

	mutex_lock(&sensor->lock);
	memset(fi, 0, sizeof(*fi));
	fi->interval = sensor->frame_interval;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int mt9m032_set_frame_interval(struct v4l2_subdev *subdev,
				      struct v4l2_subdev_frame_interval *fi)
{
	struct mt9m032 *sensor = to_mt9m032(subdev);
	int ret;

	mutex_lock(&sensor->lock);

	if (sensor->streaming) {
		ret = -EBUSY;
		goto done;
	}

	/* Avoid divisions by 0. */
	if (fi->interval.denominator == 0)
		fi->interval.denominator = 1;

	ret = mt9m032_update_timing(sensor, &fi->interval);
	if (!ret)
		sensor->frame_interval = fi->interval;

done:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int mt9m032_s_stream(struct v4l2_subdev *subdev, int streaming)
{
	struct mt9m032 *sensor = to_mt9m032(subdev);
	int ret;

	mutex_lock(&sensor->lock);
	ret = update_formatter2(sensor, streaming);
	if (!ret)
		sensor->streaming = streaming;
	mutex_unlock(&sensor->lock);

	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9m032_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct mt9m032 *sensor = to_mt9m032(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	int val;

	if (reg->reg > 0xff)
		return -EINVAL;

	val = mt9m032_read(client, reg->reg);
	if (val < 0)
		return -EIO;

	reg->size = 2;
	reg->val = val;

	return 0;
}

static int mt9m032_s_register(struct v4l2_subdev *sd,
			      const struct v4l2_dbg_register *reg)
{
	struct mt9m032 *sensor = to_mt9m032(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);

	if (reg->reg > 0xff)
		return -EINVAL;

	return mt9m032_write(client, reg->reg, reg->val);
}
#endif

/* -----------------------------------------------------------------------------
 * V4L2 subdev control operations
 */

static int update_read_mode2(struct mt9m032 *sensor, bool vflip, bool hflip)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	int reg_val = (vflip << MT9M032_READ_MODE2_VFLIP_SHIFT)
		    | (hflip << MT9M032_READ_MODE2_HFLIP_SHIFT)
		    | MT9M032_READ_MODE2_ROW_BLC
		    | 0x0007;

	return mt9m032_write(client, MT9M032_READ_MODE2, reg_val);
}

static int mt9m032_set_gain(struct mt9m032 *sensor, s32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	int digital_gain_val;	/* in 1/8th (0..127) */
	int analog_mul;		/* 0 or 1 */
	int analog_gain_val;	/* in 1/16th. (0..63) */
	u16 reg_val;

	digital_gain_val = 51; /* from setup example */

	if (val < 63) {
		analog_mul = 0;
		analog_gain_val = val;
	} else {
		analog_mul = 1;
		analog_gain_val = val / 2;
	}

	/* a_gain = (1 + analog_mul) + (analog_gain_val + 1) / 16 */
	/* overall_gain = a_gain * (1 + digital_gain_val / 8) */

	reg_val = ((digital_gain_val & MT9M032_GAIN_DIGITAL_MASK)
		   << MT9M032_GAIN_DIGITAL_SHIFT)
		| ((analog_mul & 1) << MT9M032_GAIN_AMUL_SHIFT)
		| (analog_gain_val & MT9M032_GAIN_ANALOG_MASK);

	return mt9m032_write(client, MT9M032_GAIN_ALL, reg_val);
}

static int mt9m032_try_ctrl(struct v4l2_ctrl *ctrl)
{
	if (ctrl->id == V4L2_CID_GAIN && ctrl->val >= 63) {
		/* round because of multiplier used for values >= 63 */
		ctrl->val &= ~1;
	}

	return 0;
}

static int mt9m032_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m032 *sensor =
		container_of(ctrl->handler, struct mt9m032, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		return mt9m032_set_gain(sensor, ctrl->val);

	case V4L2_CID_HFLIP:
	/* case V4L2_CID_VFLIP: -- In the same cluster */
		return update_read_mode2(sensor, sensor->vflip->val,
					 sensor->hflip->val);

	case V4L2_CID_EXPOSURE:
		ret = mt9m032_write(client, MT9M032_SHUTTER_WIDTH_HIGH,
				    (ctrl->val >> 16) & 0xffff);
		if (ret < 0)
			return ret;

		return mt9m032_write(client, MT9M032_SHUTTER_WIDTH_LOW,
				     ctrl->val & 0xffff);
	}

	return 0;
}

static struct v4l2_ctrl_ops mt9m032_ctrl_ops = {
	.s_ctrl = mt9m032_set_ctrl,
	.try_ctrl = mt9m032_try_ctrl,
};

/* -------------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops mt9m032_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = mt9m032_g_register,
	.s_register = mt9m032_s_register,
#endif
};

static const struct v4l2_subdev_video_ops mt9m032_video_ops = {
	.s_stream = mt9m032_s_stream,
	.g_frame_interval = mt9m032_get_frame_interval,
	.s_frame_interval = mt9m032_set_frame_interval,
};

static const struct v4l2_subdev_pad_ops mt9m032_pad_ops = {
	.enum_mbus_code = mt9m032_enum_mbus_code,
	.enum_frame_size = mt9m032_enum_frame_size,
	.get_fmt = mt9m032_get_pad_format,
	.set_fmt = mt9m032_set_pad_format,
	.set_crop = mt9m032_set_pad_crop,
	.get_crop = mt9m032_get_pad_crop,
};

static const struct v4l2_subdev_ops mt9m032_ops = {
	.core = &mt9m032_core_ops,
	.video = &mt9m032_video_ops,
	.pad = &mt9m032_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Driver initialization and probing
 */

static int mt9m032_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct mt9m032_platform_data *pdata = client->dev.platform_data;
	struct i2c_adapter *adapter = client->adapter;
	struct mt9m032 *sensor;
	int chip_version;
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

	if (!client->dev.platform_data)
		return -ENODEV;

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (sensor == NULL)
		return -ENOMEM;

	mutex_init(&sensor->lock);

	sensor->pdata = pdata;

	v4l2_i2c_subdev_init(&sensor->subdev, client, &mt9m032_ops);
	sensor->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	chip_version = mt9m032_read(client, MT9M032_CHIP_VERSION);
	if (chip_version != MT9M032_CHIP_VERSION_VALUE) {
		dev_err(&client->dev, "MT9M032 not detected, wrong version "
			"0x%04x\n", chip_version);
		ret = -ENODEV;
		goto error_sensor;
	}

	dev_info(&client->dev, "MT9M032 detected at address 0x%02x\n",
		 client->addr);

	sensor->frame_interval.numerator = 1;
	sensor->frame_interval.denominator = 30;

	sensor->crop.left = MT9M032_COLUMN_START_DEF;
	sensor->crop.top = MT9M032_ROW_START_DEF;
	sensor->crop.width = MT9M032_COLUMN_SIZE_DEF;
	sensor->crop.height = MT9M032_ROW_SIZE_DEF;

	sensor->format.width = sensor->crop.width;
	sensor->format.height = sensor->crop.height;
	sensor->format.code = MEDIA_BUS_FMT_Y8_1X8;
	sensor->format.field = V4L2_FIELD_NONE;
	sensor->format.colorspace = V4L2_COLORSPACE_SRGB;

	v4l2_ctrl_handler_init(&sensor->ctrls, 5);

	v4l2_ctrl_new_std(&sensor->ctrls, &mt9m032_ctrl_ops,
			  V4L2_CID_GAIN, 0, 127, 1, 64);

	sensor->hflip = v4l2_ctrl_new_std(&sensor->ctrls,
					  &mt9m032_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	sensor->vflip = v4l2_ctrl_new_std(&sensor->ctrls,
					  &mt9m032_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&sensor->ctrls, &mt9m032_ctrl_ops,
			  V4L2_CID_EXPOSURE, MT9M032_SHUTTER_WIDTH_MIN,
			  MT9M032_SHUTTER_WIDTH_MAX, 1,
			  MT9M032_SHUTTER_WIDTH_DEF);
	v4l2_ctrl_new_std(&sensor->ctrls, &mt9m032_ctrl_ops,
			  V4L2_CID_PIXEL_RATE, pdata->pix_clock,
			  pdata->pix_clock, 1, pdata->pix_clock);

	if (sensor->ctrls.error) {
		ret = sensor->ctrls.error;
		dev_err(&client->dev, "control initialization error %d\n", ret);
		goto error_ctrl;
	}

	v4l2_ctrl_cluster(2, &sensor->hflip);

	sensor->subdev.ctrl_handler = &sensor->ctrls;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sensor->subdev.entity, 1, &sensor->pad, 0);
	if (ret < 0)
		goto error_ctrl;

	ret = mt9m032_write(client, MT9M032_RESET, 1);	/* reset on */
	if (ret < 0)
		goto error_entity;
	ret = mt9m032_write(client, MT9M032_RESET, 0);	/* reset off */
	if (ret < 0)
		goto error_entity;

	ret = mt9m032_setup_pll(sensor);
	if (ret < 0)
		goto error_entity;
	usleep_range(10000, 11000);

	ret = v4l2_ctrl_handler_setup(&sensor->ctrls);
	if (ret < 0)
		goto error_entity;

	/* SIZE */
	ret = mt9m032_update_geom_timing(sensor);
	if (ret < 0)
		goto error_entity;

	ret = mt9m032_write(client, 0x41, 0x0000);	/* reserved !!! */
	if (ret < 0)
		goto error_entity;
	ret = mt9m032_write(client, 0x42, 0x0003);	/* reserved !!! */
	if (ret < 0)
		goto error_entity;
	ret = mt9m032_write(client, 0x43, 0x0003);	/* reserved !!! */
	if (ret < 0)
		goto error_entity;
	ret = mt9m032_write(client, 0x7f, 0x0000);	/* reserved !!! */
	if (ret < 0)
		goto error_entity;
	if (sensor->pdata->invert_pixclock) {
		ret = mt9m032_write(client, MT9M032_PIX_CLK_CTRL,
				    MT9M032_PIX_CLK_CTRL_INV_PIXCLK);
		if (ret < 0)
			goto error_entity;
	}

	ret = mt9m032_write(client, MT9M032_RESTART, 1); /* Restart on */
	if (ret < 0)
		goto error_entity;
	msleep(100);
	ret = mt9m032_write(client, MT9M032_RESTART, 0); /* Restart off */
	if (ret < 0)
		goto error_entity;
	msleep(100);
	ret = update_formatter2(sensor, false);
	if (ret < 0)
		goto error_entity;

	return ret;

error_entity:
	media_entity_cleanup(&sensor->subdev.entity);
error_ctrl:
	v4l2_ctrl_handler_free(&sensor->ctrls);
error_sensor:
	mutex_destroy(&sensor->lock);
	return ret;
}

static int mt9m032_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct mt9m032 *sensor = to_mt9m032(subdev);

	v4l2_device_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&sensor->ctrls);
	media_entity_cleanup(&subdev->entity);
	mutex_destroy(&sensor->lock);
	return 0;
}

static const struct i2c_device_id mt9m032_id_table[] = {
	{ MT9M032_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, mt9m032_id_table);

static struct i2c_driver mt9m032_i2c_driver = {
	.driver = {
		.name = MT9M032_NAME,
	},
	.probe = mt9m032_probe,
	.remove = mt9m032_remove,
	.id_table = mt9m032_id_table,
};

module_i2c_driver(mt9m032_i2c_driver);

MODULE_AUTHOR("Martin Hostettler <martin@neutronstar.dyndns.org>");
MODULE_DESCRIPTION("MT9M032 camera sensor driver");
MODULE_LICENSE("GPL v2");
