/*
 * Driver for MT9M111/MT9M112/MT9M131 CMOS Image Sensor from Micron/Aptina
 *
 * Copyright (C) 2008, Robert Jarzmik <robert.jarzmik@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>

/*
 * MT9M111, MT9M112 and MT9M131:
 * i2c address is 0x48 or 0x5d (depending on SADDR pin)
 * The platform has to define i2c_board_info and call i2c_register_board_info()
 */

/*
 * Sensor core register addresses (0x000..0x0ff)
 */
#define MT9M111_CHIP_VERSION		0x000
#define MT9M111_ROW_START		0x001
#define MT9M111_COLUMN_START		0x002
#define MT9M111_WINDOW_HEIGHT		0x003
#define MT9M111_WINDOW_WIDTH		0x004
#define MT9M111_HORIZONTAL_BLANKING_B	0x005
#define MT9M111_VERTICAL_BLANKING_B	0x006
#define MT9M111_HORIZONTAL_BLANKING_A	0x007
#define MT9M111_VERTICAL_BLANKING_A	0x008
#define MT9M111_SHUTTER_WIDTH		0x009
#define MT9M111_ROW_SPEED		0x00a
#define MT9M111_EXTRA_DELAY		0x00b
#define MT9M111_SHUTTER_DELAY		0x00c
#define MT9M111_RESET			0x00d
#define MT9M111_READ_MODE_B		0x020
#define MT9M111_READ_MODE_A		0x021
#define MT9M111_FLASH_CONTROL		0x023
#define MT9M111_GREEN1_GAIN		0x02b
#define MT9M111_BLUE_GAIN		0x02c
#define MT9M111_RED_GAIN		0x02d
#define MT9M111_GREEN2_GAIN		0x02e
#define MT9M111_GLOBAL_GAIN		0x02f
#define MT9M111_CONTEXT_CONTROL		0x0c8
#define MT9M111_PAGE_MAP		0x0f0
#define MT9M111_BYTE_WISE_ADDR		0x0f1

#define MT9M111_RESET_SYNC_CHANGES	(1 << 15)
#define MT9M111_RESET_RESTART_BAD_FRAME	(1 << 9)
#define MT9M111_RESET_SHOW_BAD_FRAMES	(1 << 8)
#define MT9M111_RESET_RESET_SOC		(1 << 5)
#define MT9M111_RESET_OUTPUT_DISABLE	(1 << 4)
#define MT9M111_RESET_CHIP_ENABLE	(1 << 3)
#define MT9M111_RESET_ANALOG_STANDBY	(1 << 2)
#define MT9M111_RESET_RESTART_FRAME	(1 << 1)
#define MT9M111_RESET_RESET_MODE	(1 << 0)

#define MT9M111_RMB_MIRROR_COLS		(1 << 1)
#define MT9M111_RMB_MIRROR_ROWS		(1 << 0)
#define MT9M111_CTXT_CTRL_RESTART	(1 << 15)
#define MT9M111_CTXT_CTRL_DEFECTCOR_B	(1 << 12)
#define MT9M111_CTXT_CTRL_RESIZE_B	(1 << 10)
#define MT9M111_CTXT_CTRL_CTRL2_B	(1 << 9)
#define MT9M111_CTXT_CTRL_GAMMA_B	(1 << 8)
#define MT9M111_CTXT_CTRL_XENON_EN	(1 << 7)
#define MT9M111_CTXT_CTRL_READ_MODE_B	(1 << 3)
#define MT9M111_CTXT_CTRL_LED_FLASH_EN	(1 << 2)
#define MT9M111_CTXT_CTRL_VBLANK_SEL_B	(1 << 1)
#define MT9M111_CTXT_CTRL_HBLANK_SEL_B	(1 << 0)

/*
 * Colorpipe register addresses (0x100..0x1ff)
 */
#define MT9M111_OPER_MODE_CTRL		0x106
#define MT9M111_OUTPUT_FORMAT_CTRL	0x108
#define MT9M111_REDUCER_XZOOM_B		0x1a0
#define MT9M111_REDUCER_XSIZE_B		0x1a1
#define MT9M111_REDUCER_YZOOM_B		0x1a3
#define MT9M111_REDUCER_YSIZE_B		0x1a4
#define MT9M111_REDUCER_XZOOM_A		0x1a6
#define MT9M111_REDUCER_XSIZE_A		0x1a7
#define MT9M111_REDUCER_YZOOM_A		0x1a9
#define MT9M111_REDUCER_YSIZE_A		0x1aa

#define MT9M111_OUTPUT_FORMAT_CTRL2_A	0x13a
#define MT9M111_OUTPUT_FORMAT_CTRL2_B	0x19b

#define MT9M111_OPMODE_AUTOEXPO_EN	(1 << 14)
#define MT9M111_OPMODE_AUTOWHITEBAL_EN	(1 << 1)

#define MT9M111_OUTFMT_PROCESSED_BAYER	(1 << 14)
#define MT9M111_OUTFMT_BYPASS_IFP	(1 << 10)
#define MT9M111_OUTFMT_INV_PIX_CLOCK	(1 << 9)
#define MT9M111_OUTFMT_RGB		(1 << 8)
#define MT9M111_OUTFMT_RGB565		(0x0 << 6)
#define MT9M111_OUTFMT_RGB555		(0x1 << 6)
#define MT9M111_OUTFMT_RGB444x		(0x2 << 6)
#define MT9M111_OUTFMT_RGBx444		(0x3 << 6)
#define MT9M111_OUTFMT_TST_RAMP_OFF	(0x0 << 4)
#define MT9M111_OUTFMT_TST_RAMP_COL	(0x1 << 4)
#define MT9M111_OUTFMT_TST_RAMP_ROW	(0x2 << 4)
#define MT9M111_OUTFMT_TST_RAMP_FRAME	(0x3 << 4)
#define MT9M111_OUTFMT_SHIFT_3_UP	(1 << 3)
#define MT9M111_OUTFMT_AVG_CHROMA	(1 << 2)
#define MT9M111_OUTFMT_SWAP_YCbCr_C_Y	(1 << 1)
#define MT9M111_OUTFMT_SWAP_RGB_EVEN	(1 << 1)
#define MT9M111_OUTFMT_SWAP_YCbCr_Cb_Cr	(1 << 0)

/*
 * Camera control register addresses (0x200..0x2ff not implemented)
 */

#define reg_read(reg) mt9m111_reg_read(client, MT9M111_##reg)
#define reg_write(reg, val) mt9m111_reg_write(client, MT9M111_##reg, (val))
#define reg_set(reg, val) mt9m111_reg_set(client, MT9M111_##reg, (val))
#define reg_clear(reg, val) mt9m111_reg_clear(client, MT9M111_##reg, (val))

#define MT9M111_MIN_DARK_ROWS	8
#define MT9M111_MIN_DARK_COLS	24
#define MT9M111_MAX_HEIGHT	1024
#define MT9M111_MAX_WIDTH	1280

/* MT9M111 has only one fixed colorspace per pixelcode */
struct mt9m111_datafmt {
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_colorspace		colorspace;
};

/* Find a data format by a pixel code in an array */
static const struct mt9m111_datafmt *mt9m111_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct mt9m111_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}

static const struct mt9m111_datafmt mt9m111_colour_fmts[] = {
	{V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_YVYU8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_VYUY8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_RGB565_2X8_LE, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SBGGR8_1X8, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE, V4L2_COLORSPACE_SRGB},
};

enum mt9m111_context {
	HIGHPOWER = 0,
	LOWPOWER,
};

struct mt9m111 {
	struct v4l2_subdev subdev;
	int model;	/* V4L2_IDENT_MT9M111 or V4L2_IDENT_MT9M112 code
			 * from v4l2-chip-ident.h */
	enum mt9m111_context context;
	struct v4l2_rect rect;
	const struct mt9m111_datafmt *fmt;
	unsigned int gain;
	unsigned char autoexposure;
	unsigned char datawidth;
	unsigned int powered:1;
	unsigned int hflip:1;
	unsigned int vflip:1;
	unsigned int swap_rgb_even_odd:1;
	unsigned int swap_rgb_red_blue:1;
	unsigned int swap_yuv_y_chromas:1;
	unsigned int swap_yuv_cb_cr:1;
	unsigned int autowhitebalance:1;
};

static struct mt9m111 *to_mt9m111(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct mt9m111, subdev);
}

static int reg_page_map_set(struct i2c_client *client, const u16 reg)
{
	int ret;
	u16 page;
	static int lastpage = -1;	/* PageMap cache value */

	page = (reg >> 8);
	if (page == lastpage)
		return 0;
	if (page > 2)
		return -EINVAL;

	ret = i2c_smbus_write_word_data(client, MT9M111_PAGE_MAP, swab16(page));
	if (!ret)
		lastpage = page;
	return ret;
}

static int mt9m111_reg_read(struct i2c_client *client, const u16 reg)
{
	int ret;

	ret = reg_page_map_set(client, reg);
	if (!ret)
		ret = swab16(i2c_smbus_read_word_data(client, reg & 0xff));

	dev_dbg(&client->dev, "read  reg.%03x -> %04x\n", reg, ret);
	return ret;
}

static int mt9m111_reg_write(struct i2c_client *client, const u16 reg,
			     const u16 data)
{
	int ret;

	ret = reg_page_map_set(client, reg);
	if (!ret)
		ret = i2c_smbus_write_word_data(client, reg & 0xff,
						swab16(data));
	dev_dbg(&client->dev, "write reg.%03x = %04x -> %d\n", reg, data, ret);
	return ret;
}

static int mt9m111_reg_set(struct i2c_client *client, const u16 reg,
			   const u16 data)
{
	int ret;

	ret = mt9m111_reg_read(client, reg);
	if (ret >= 0)
		ret = mt9m111_reg_write(client, reg, ret | data);
	return ret;
}

static int mt9m111_reg_clear(struct i2c_client *client, const u16 reg,
			     const u16 data)
{
	int ret;

	ret = mt9m111_reg_read(client, reg);
	return mt9m111_reg_write(client, reg, ret & ~data);
}

static int mt9m111_set_context(struct i2c_client *client,
			       enum mt9m111_context ctxt)
{
	int valB = MT9M111_CTXT_CTRL_RESTART | MT9M111_CTXT_CTRL_DEFECTCOR_B
		| MT9M111_CTXT_CTRL_RESIZE_B | MT9M111_CTXT_CTRL_CTRL2_B
		| MT9M111_CTXT_CTRL_GAMMA_B | MT9M111_CTXT_CTRL_READ_MODE_B
		| MT9M111_CTXT_CTRL_VBLANK_SEL_B
		| MT9M111_CTXT_CTRL_HBLANK_SEL_B;
	int valA = MT9M111_CTXT_CTRL_RESTART;

	if (ctxt == HIGHPOWER)
		return reg_write(CONTEXT_CONTROL, valB);
	else
		return reg_write(CONTEXT_CONTROL, valA);
}

static int mt9m111_setup_rect(struct i2c_client *client,
			      struct v4l2_rect *rect)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int ret, is_raw_format;
	int width = rect->width;
	int height = rect->height;

	if (mt9m111->fmt->code == V4L2_MBUS_FMT_SBGGR8_1X8 ||
	    mt9m111->fmt->code == V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE)
		is_raw_format = 1;
	else
		is_raw_format = 0;

	ret = reg_write(COLUMN_START, rect->left);
	if (!ret)
		ret = reg_write(ROW_START, rect->top);

	if (is_raw_format) {
		if (!ret)
			ret = reg_write(WINDOW_WIDTH, width);
		if (!ret)
			ret = reg_write(WINDOW_HEIGHT, height);
	} else {
		if (!ret)
			ret = reg_write(REDUCER_XZOOM_B, MT9M111_MAX_WIDTH);
		if (!ret)
			ret = reg_write(REDUCER_YZOOM_B, MT9M111_MAX_HEIGHT);
		if (!ret)
			ret = reg_write(REDUCER_XSIZE_B, width);
		if (!ret)
			ret = reg_write(REDUCER_YSIZE_B, height);
		if (!ret)
			ret = reg_write(REDUCER_XZOOM_A, MT9M111_MAX_WIDTH);
		if (!ret)
			ret = reg_write(REDUCER_YZOOM_A, MT9M111_MAX_HEIGHT);
		if (!ret)
			ret = reg_write(REDUCER_XSIZE_A, width);
		if (!ret)
			ret = reg_write(REDUCER_YSIZE_A, height);
	}

	return ret;
}

static int mt9m111_setup_pixfmt(struct i2c_client *client, u16 outfmt)
{
	int ret;

	ret = reg_write(OUTPUT_FORMAT_CTRL2_A, outfmt);
	if (!ret)
		ret = reg_write(OUTPUT_FORMAT_CTRL2_B, outfmt);
	return ret;
}

static int mt9m111_setfmt_bayer8(struct i2c_client *client)
{
	return mt9m111_setup_pixfmt(client, MT9M111_OUTFMT_PROCESSED_BAYER |
				    MT9M111_OUTFMT_RGB);
}

static int mt9m111_setfmt_bayer10(struct i2c_client *client)
{
	return mt9m111_setup_pixfmt(client, MT9M111_OUTFMT_BYPASS_IFP);
}

static int mt9m111_setfmt_rgb565(struct i2c_client *client)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int val = 0;

	if (mt9m111->swap_rgb_red_blue)
		val |= MT9M111_OUTFMT_SWAP_YCbCr_Cb_Cr;
	if (mt9m111->swap_rgb_even_odd)
		val |= MT9M111_OUTFMT_SWAP_RGB_EVEN;
	val |= MT9M111_OUTFMT_RGB | MT9M111_OUTFMT_RGB565;

	return mt9m111_setup_pixfmt(client, val);
}

static int mt9m111_setfmt_rgb555(struct i2c_client *client)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int val = 0;

	if (mt9m111->swap_rgb_red_blue)
		val |= MT9M111_OUTFMT_SWAP_YCbCr_Cb_Cr;
	if (mt9m111->swap_rgb_even_odd)
		val |= MT9M111_OUTFMT_SWAP_RGB_EVEN;
	val |= MT9M111_OUTFMT_RGB | MT9M111_OUTFMT_RGB555;

	return mt9m111_setup_pixfmt(client, val);
}

static int mt9m111_setfmt_yuv(struct i2c_client *client)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int val = 0;

	if (mt9m111->swap_yuv_cb_cr)
		val |= MT9M111_OUTFMT_SWAP_YCbCr_Cb_Cr;
	if (mt9m111->swap_yuv_y_chromas)
		val |= MT9M111_OUTFMT_SWAP_YCbCr_C_Y;

	return mt9m111_setup_pixfmt(client, val);
}

static int mt9m111_enable(struct i2c_client *client)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int ret;

	ret = reg_set(RESET, MT9M111_RESET_CHIP_ENABLE);
	if (!ret)
		mt9m111->powered = 1;
	return ret;
}

static int mt9m111_reset(struct i2c_client *client)
{
	int ret;

	ret = reg_set(RESET, MT9M111_RESET_RESET_MODE);
	if (!ret)
		ret = reg_set(RESET, MT9M111_RESET_RESET_SOC);
	if (!ret)
		ret = reg_clear(RESET, MT9M111_RESET_RESET_MODE
				| MT9M111_RESET_RESET_SOC);

	return ret;
}

static unsigned long mt9m111_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	unsigned long flags = SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING |
		SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_HIGH |
		SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8;

	return soc_camera_apply_sensor_flags(icl, flags);
}

static int mt9m111_set_bus_param(struct soc_camera_device *icd, unsigned long f)
{
	return 0;
}

static int mt9m111_make_rect(struct i2c_client *client,
			     struct v4l2_rect *rect)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);

	if (mt9m111->fmt->code == V4L2_MBUS_FMT_SBGGR8_1X8 ||
	    mt9m111->fmt->code == V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE) {
		/* Bayer format - even size lengths */
		rect->width	= ALIGN(rect->width, 2);
		rect->height	= ALIGN(rect->height, 2);
		/* Let the user play with the starting pixel */
	}

	/* FIXME: the datasheet doesn't specify minimum sizes */
	soc_camera_limit_side(&rect->left, &rect->width,
		     MT9M111_MIN_DARK_COLS, 2, MT9M111_MAX_WIDTH);

	soc_camera_limit_side(&rect->top, &rect->height,
		     MT9M111_MIN_DARK_ROWS, 2, MT9M111_MAX_HEIGHT);

	return mt9m111_setup_rect(client, rect);
}

static int mt9m111_s_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct v4l2_rect rect = a->c;
	struct i2c_client *client = sd->priv;
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int ret;

	dev_dbg(&client->dev, "%s left=%d, top=%d, width=%d, height=%d\n",
		__func__, rect.left, rect.top, rect.width, rect.height);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	ret = mt9m111_make_rect(client, &rect);
	if (!ret)
		mt9m111->rect = rect;
	return ret;
}

static int mt9m111_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = sd->priv;
	struct mt9m111 *mt9m111 = to_mt9m111(client);

	a->c	= mt9m111->rect;
	a->type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int mt9m111_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	a->bounds.left			= MT9M111_MIN_DARK_COLS;
	a->bounds.top			= MT9M111_MIN_DARK_ROWS;
	a->bounds.width			= MT9M111_MAX_WIDTH;
	a->bounds.height		= MT9M111_MAX_HEIGHT;
	a->defrect			= a->bounds;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int mt9m111_g_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = sd->priv;
	struct mt9m111 *mt9m111 = to_mt9m111(client);

	mf->width	= mt9m111->rect.width;
	mf->height	= mt9m111->rect.height;
	mf->code	= mt9m111->fmt->code;
	mf->colorspace	= mt9m111->fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int mt9m111_set_pixfmt(struct i2c_client *client,
			      enum v4l2_mbus_pixelcode code)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int ret;

	switch (code) {
	case V4L2_MBUS_FMT_SBGGR8_1X8:
		ret = mt9m111_setfmt_bayer8(client);
		break;
	case V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE:
		ret = mt9m111_setfmt_bayer10(client);
		break;
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE:
		ret = mt9m111_setfmt_rgb555(client);
		break;
	case V4L2_MBUS_FMT_RGB565_2X8_LE:
		ret = mt9m111_setfmt_rgb565(client);
		break;
	case V4L2_MBUS_FMT_UYVY8_2X8:
		mt9m111->swap_yuv_y_chromas = 0;
		mt9m111->swap_yuv_cb_cr = 0;
		ret = mt9m111_setfmt_yuv(client);
		break;
	case V4L2_MBUS_FMT_VYUY8_2X8:
		mt9m111->swap_yuv_y_chromas = 0;
		mt9m111->swap_yuv_cb_cr = 1;
		ret = mt9m111_setfmt_yuv(client);
		break;
	case V4L2_MBUS_FMT_YUYV8_2X8:
		mt9m111->swap_yuv_y_chromas = 1;
		mt9m111->swap_yuv_cb_cr = 0;
		ret = mt9m111_setfmt_yuv(client);
		break;
	case V4L2_MBUS_FMT_YVYU8_2X8:
		mt9m111->swap_yuv_y_chromas = 1;
		mt9m111->swap_yuv_cb_cr = 1;
		ret = mt9m111_setfmt_yuv(client);
		break;
	default:
		dev_err(&client->dev, "Pixel format not handled : %x\n",
			code);
		ret = -EINVAL;
	}

	return ret;
}

static int mt9m111_s_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = sd->priv;
	const struct mt9m111_datafmt *fmt;
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	struct v4l2_rect rect = {
		.left	= mt9m111->rect.left,
		.top	= mt9m111->rect.top,
		.width	= mf->width,
		.height	= mf->height,
	};
	int ret;

	fmt = mt9m111_find_datafmt(mf->code, mt9m111_colour_fmts,
				   ARRAY_SIZE(mt9m111_colour_fmts));
	if (!fmt)
		return -EINVAL;

	dev_dbg(&client->dev,
		"%s code=%x left=%d, top=%d, width=%d, height=%d\n", __func__,
		mf->code, rect.left, rect.top, rect.width, rect.height);

	ret = mt9m111_make_rect(client, &rect);
	if (!ret)
		ret = mt9m111_set_pixfmt(client, mf->code);
	if (!ret) {
		mt9m111->rect	= rect;
		mt9m111->fmt	= fmt;
		mf->colorspace	= fmt->colorspace;
	}

	return ret;
}

static int mt9m111_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = sd->priv;
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	const struct mt9m111_datafmt *fmt;
	bool bayer = mf->code == V4L2_MBUS_FMT_SBGGR8_1X8 ||
		mf->code == V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE;

	fmt = mt9m111_find_datafmt(mf->code, mt9m111_colour_fmts,
				   ARRAY_SIZE(mt9m111_colour_fmts));
	if (!fmt) {
		fmt = mt9m111->fmt;
		mf->code = fmt->code;
	}

	/*
	 * With Bayer format enforce even side lengths, but let the user play
	 * with the starting pixel
	 */

	if (mf->height > MT9M111_MAX_HEIGHT)
		mf->height = MT9M111_MAX_HEIGHT;
	else if (mf->height < 2)
		mf->height = 2;
	else if (bayer)
		mf->height = ALIGN(mf->height, 2);

	if (mf->width > MT9M111_MAX_WIDTH)
		mf->width = MT9M111_MAX_WIDTH;
	else if (mf->width < 2)
		mf->width = 2;
	else if (bayer)
		mf->width = ALIGN(mf->width, 2);

	mf->colorspace = fmt->colorspace;

	return 0;
}

static int mt9m111_g_chip_ident(struct v4l2_subdev *sd,
				struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = sd->priv;
	struct mt9m111 *mt9m111 = to_mt9m111(client);

	if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match.addr != client->addr)
		return -ENODEV;

	id->ident	= mt9m111->model;
	id->revision	= 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9m111_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = sd->priv;
	int val;

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0x2ff)
		return -EINVAL;
	if (reg->match.addr != client->addr)
		return -ENODEV;

	val = mt9m111_reg_read(client, reg->reg);
	reg->size = 2;
	reg->val = (u64)val;

	if (reg->val > 0xffff)
		return -EIO;

	return 0;
}

static int mt9m111_s_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = sd->priv;

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0x2ff)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	if (mt9m111_reg_write(client, reg->reg, reg->val) < 0)
		return -EIO;

	return 0;
}
#endif

static const struct v4l2_queryctrl mt9m111_controls[] = {
	{
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Verticaly",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	}, {
		.id		= V4L2_CID_HFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Horizontaly",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	}, {	/* gain = 1/32*val (=>gain=1 if val==32) */
		.id		= V4L2_CID_GAIN,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Gain",
		.minimum	= 0,
		.maximum	= 63 * 2 * 2,
		.step		= 1,
		.default_value	= 32,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.id		= V4L2_CID_EXPOSURE_AUTO,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Auto Exposure",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 1,
	}
};

static int mt9m111_resume(struct soc_camera_device *icd);
static int mt9m111_suspend(struct soc_camera_device *icd, pm_message_t state);

static struct soc_camera_ops mt9m111_ops = {
	.suspend		= mt9m111_suspend,
	.resume			= mt9m111_resume,
	.query_bus_param	= mt9m111_query_bus_param,
	.set_bus_param		= mt9m111_set_bus_param,
	.controls		= mt9m111_controls,
	.num_controls		= ARRAY_SIZE(mt9m111_controls),
};

static int mt9m111_set_flip(struct i2c_client *client, int flip, int mask)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int ret;

	if (mt9m111->context == HIGHPOWER) {
		if (flip)
			ret = reg_set(READ_MODE_B, mask);
		else
			ret = reg_clear(READ_MODE_B, mask);
	} else {
		if (flip)
			ret = reg_set(READ_MODE_A, mask);
		else
			ret = reg_clear(READ_MODE_A, mask);
	}

	return ret;
}

static int mt9m111_get_global_gain(struct i2c_client *client)
{
	int data;

	data = reg_read(GLOBAL_GAIN);
	if (data >= 0)
		return (data & 0x2f) * (1 << ((data >> 10) & 1)) *
			(1 << ((data >> 9) & 1));
	return data;
}

static int mt9m111_set_global_gain(struct i2c_client *client, int gain)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	u16 val;

	if (gain > 63 * 2 * 2)
		return -EINVAL;

	mt9m111->gain = gain;
	if ((gain >= 64 * 2) && (gain < 63 * 2 * 2))
		val = (1 << 10) | (1 << 9) | (gain / 4);
	else if ((gain >= 64) && (gain < 64 * 2))
		val = (1 << 9) | (gain / 2);
	else
		val = gain;

	return reg_write(GLOBAL_GAIN, val);
}

static int mt9m111_set_autoexposure(struct i2c_client *client, int on)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int ret;

	if (on)
		ret = reg_set(OPER_MODE_CTRL, MT9M111_OPMODE_AUTOEXPO_EN);
	else
		ret = reg_clear(OPER_MODE_CTRL, MT9M111_OPMODE_AUTOEXPO_EN);

	if (!ret)
		mt9m111->autoexposure = on;

	return ret;
}

static int mt9m111_set_autowhitebalance(struct i2c_client *client, int on)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int ret;

	if (on)
		ret = reg_set(OPER_MODE_CTRL, MT9M111_OPMODE_AUTOWHITEBAL_EN);
	else
		ret = reg_clear(OPER_MODE_CTRL, MT9M111_OPMODE_AUTOWHITEBAL_EN);

	if (!ret)
		mt9m111->autowhitebalance = on;

	return ret;
}

static int mt9m111_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = sd->priv;
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int data;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (mt9m111->context == HIGHPOWER)
			data = reg_read(READ_MODE_B);
		else
			data = reg_read(READ_MODE_A);

		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & MT9M111_RMB_MIRROR_ROWS);
		break;
	case V4L2_CID_HFLIP:
		if (mt9m111->context == HIGHPOWER)
			data = reg_read(READ_MODE_B);
		else
			data = reg_read(READ_MODE_A);

		if (data < 0)
			return -EIO;
		ctrl->value = !!(data & MT9M111_RMB_MIRROR_COLS);
		break;
	case V4L2_CID_GAIN:
		data = mt9m111_get_global_gain(client);
		if (data < 0)
			return data;
		ctrl->value = data;
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ctrl->value = mt9m111->autoexposure;
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ctrl->value = mt9m111->autowhitebalance;
		break;
	}
	return 0;
}

static int mt9m111_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = sd->priv;
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	const struct v4l2_queryctrl *qctrl;
	int ret;

	qctrl = soc_camera_find_qctrl(&mt9m111_ops, ctrl->id);
	if (!qctrl)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		mt9m111->vflip = ctrl->value;
		ret = mt9m111_set_flip(client, ctrl->value,
					MT9M111_RMB_MIRROR_ROWS);
		break;
	case V4L2_CID_HFLIP:
		mt9m111->hflip = ctrl->value;
		ret = mt9m111_set_flip(client, ctrl->value,
					MT9M111_RMB_MIRROR_COLS);
		break;
	case V4L2_CID_GAIN:
		ret = mt9m111_set_global_gain(client, ctrl->value);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ret =  mt9m111_set_autoexposure(client, ctrl->value);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret =  mt9m111_set_autowhitebalance(client, ctrl->value);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int mt9m111_suspend(struct soc_camera_device *icd, pm_message_t state)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct mt9m111 *mt9m111 = to_mt9m111(client);

	mt9m111->gain = mt9m111_get_global_gain(client);

	return 0;
}

static int mt9m111_restore_state(struct i2c_client *client)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);

	mt9m111_set_context(client, mt9m111->context);
	mt9m111_set_pixfmt(client, mt9m111->fmt->code);
	mt9m111_setup_rect(client, &mt9m111->rect);
	mt9m111_set_flip(client, mt9m111->hflip, MT9M111_RMB_MIRROR_COLS);
	mt9m111_set_flip(client, mt9m111->vflip, MT9M111_RMB_MIRROR_ROWS);
	mt9m111_set_global_gain(client, mt9m111->gain);
	mt9m111_set_autoexposure(client, mt9m111->autoexposure);
	mt9m111_set_autowhitebalance(client, mt9m111->autowhitebalance);
	return 0;
}

static int mt9m111_resume(struct soc_camera_device *icd)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int ret = 0;

	if (mt9m111->powered) {
		ret = mt9m111_enable(client);
		if (!ret)
			ret = mt9m111_reset(client);
		if (!ret)
			ret = mt9m111_restore_state(client);
	}
	return ret;
}

static int mt9m111_init(struct i2c_client *client)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	int ret;

	mt9m111->context = HIGHPOWER;
	ret = mt9m111_enable(client);
	if (!ret)
		ret = mt9m111_reset(client);
	if (!ret)
		ret = mt9m111_set_context(client, mt9m111->context);
	if (!ret)
		ret = mt9m111_set_autoexposure(client, mt9m111->autoexposure);
	if (ret)
		dev_err(&client->dev, "mt9m111 init failed: %d\n", ret);
	return ret;
}

/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int mt9m111_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	s32 data;
	int ret;

	/*
	 * We must have a parent by now. And it cannot be a wrong one.
	 * So this entire test is completely redundant.
	 */
	if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	mt9m111->autoexposure = 1;
	mt9m111->autowhitebalance = 1;

	mt9m111->swap_rgb_even_odd = 1;
	mt9m111->swap_rgb_red_blue = 1;

	data = reg_read(CHIP_VERSION);

	switch (data) {
	case 0x143a: /* MT9M111 or MT9M131 */
		mt9m111->model = V4L2_IDENT_MT9M111;
		dev_info(&client->dev,
			"Detected a MT9M111/MT9M131 chip ID %x\n", data);
		break;
	case 0x148c: /* MT9M112 */
		mt9m111->model = V4L2_IDENT_MT9M112;
		dev_info(&client->dev, "Detected a MT9M112 chip ID %x\n", data);
		break;
	default:
		ret = -ENODEV;
		dev_err(&client->dev,
			"No MT9M111/MT9M112/MT9M131 chip detected register read %x\n",
			data);
		goto ei2c;
	}

	ret = mt9m111_init(client);

ei2c:
	return ret;
}

static struct v4l2_subdev_core_ops mt9m111_subdev_core_ops = {
	.g_ctrl		= mt9m111_g_ctrl,
	.s_ctrl		= mt9m111_s_ctrl,
	.g_chip_ident	= mt9m111_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= mt9m111_g_register,
	.s_register	= mt9m111_s_register,
#endif
};

static int mt9m111_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(mt9m111_colour_fmts))
		return -EINVAL;

	*code = mt9m111_colour_fmts[index].code;
	return 0;
}

static struct v4l2_subdev_video_ops mt9m111_subdev_video_ops = {
	.s_mbus_fmt	= mt9m111_s_fmt,
	.g_mbus_fmt	= mt9m111_g_fmt,
	.try_mbus_fmt	= mt9m111_try_fmt,
	.s_crop		= mt9m111_s_crop,
	.g_crop		= mt9m111_g_crop,
	.cropcap	= mt9m111_cropcap,
	.enum_mbus_fmt	= mt9m111_enum_fmt,
};

static struct v4l2_subdev_ops mt9m111_subdev_ops = {
	.core	= &mt9m111_subdev_core_ops,
	.video	= &mt9m111_subdev_video_ops,
};

static int mt9m111_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9m111 *mt9m111;
	struct soc_camera_device *icd = client->dev.platform_data;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct soc_camera_link *icl;
	int ret;

	if (!icd) {
		dev_err(&client->dev, "mt9m111: soc-camera data missing!\n");
		return -EINVAL;
	}

	icl = to_soc_camera_link(icd);
	if (!icl) {
		dev_err(&client->dev, "mt9m111: driver needs platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	mt9m111 = kzalloc(sizeof(struct mt9m111), GFP_KERNEL);
	if (!mt9m111)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&mt9m111->subdev, client, &mt9m111_subdev_ops);

	/* Second stage probe - when a capture adapter is there */
	icd->ops		= &mt9m111_ops;

	mt9m111->rect.left	= MT9M111_MIN_DARK_COLS;
	mt9m111->rect.top	= MT9M111_MIN_DARK_ROWS;
	mt9m111->rect.width	= MT9M111_MAX_WIDTH;
	mt9m111->rect.height	= MT9M111_MAX_HEIGHT;
	mt9m111->fmt		= &mt9m111_colour_fmts[0];

	ret = mt9m111_video_probe(icd, client);
	if (ret) {
		icd->ops = NULL;
		kfree(mt9m111);
	}

	return ret;
}

static int mt9m111_remove(struct i2c_client *client)
{
	struct mt9m111 *mt9m111 = to_mt9m111(client);
	struct soc_camera_device *icd = client->dev.platform_data;

	icd->ops = NULL;
	client->driver = NULL;
	kfree(mt9m111);

	return 0;
}

static const struct i2c_device_id mt9m111_id[] = {
	{ "mt9m111", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9m111_id);

static struct i2c_driver mt9m111_i2c_driver = {
	.driver = {
		.name = "mt9m111",
	},
	.probe		= mt9m111_probe,
	.remove		= mt9m111_remove,
	.id_table	= mt9m111_id,
};

static int __init mt9m111_mod_init(void)
{
	return i2c_add_driver(&mt9m111_i2c_driver);
}

static void __exit mt9m111_mod_exit(void)
{
	i2c_del_driver(&mt9m111_i2c_driver);
}

module_init(mt9m111_mod_init);
module_exit(mt9m111_mod_exit);

MODULE_DESCRIPTION("Micron/Aptina MT9M111/MT9M112/MT9M131 Camera driver");
MODULE_AUTHOR("Robert Jarzmik");
MODULE_LICENSE("GPL");
