/*
 * Driver for MT9M111/MT9M112 CMOS Image Sensor from Micron
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
 * mt9m111 and mt9m112 i2c address is 0x5d or 0x48 (depending on SAddr pin)
 * The platform has to define i2c_board_info and call i2c_register_board_info()
 */

/* mt9m111: Sensor register addresses */
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
 * mt9m111: Colorpipe register addresses (0x100..0x1ff)
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
 * mt9m111: Camera control register addresses (0x200..0x2ff not implemented)
 */

#define reg_read(reg) mt9m111_reg_read(icd, MT9M111_##reg)
#define reg_write(reg, val) mt9m111_reg_write(icd, MT9M111_##reg, (val))
#define reg_set(reg, val) mt9m111_reg_set(icd, MT9M111_##reg, (val))
#define reg_clear(reg, val) mt9m111_reg_clear(icd, MT9M111_##reg, (val))

#define MT9M111_MIN_DARK_ROWS	8
#define MT9M111_MIN_DARK_COLS	24
#define MT9M111_MAX_HEIGHT	1024
#define MT9M111_MAX_WIDTH	1280

#define COL_FMT(_name, _depth, _fourcc, _colorspace) \
	{ .name = _name, .depth = _depth, .fourcc = _fourcc, \
	.colorspace = _colorspace }
#define RGB_FMT(_name, _depth, _fourcc) \
	COL_FMT(_name, _depth, _fourcc, V4L2_COLORSPACE_SRGB)
#define JPG_FMT(_name, _depth, _fourcc) \
	COL_FMT(_name, _depth, _fourcc, V4L2_COLORSPACE_JPEG)

static const struct soc_camera_data_format mt9m111_colour_formats[] = {
	JPG_FMT("CbYCrY 16 bit", 16, V4L2_PIX_FMT_UYVY),
	JPG_FMT("CrYCbY 16 bit", 16, V4L2_PIX_FMT_VYUY),
	JPG_FMT("YCbYCr 16 bit", 16, V4L2_PIX_FMT_YUYV),
	JPG_FMT("YCrYCb 16 bit", 16, V4L2_PIX_FMT_YVYU),
	RGB_FMT("RGB 565", 16, V4L2_PIX_FMT_RGB565),
	RGB_FMT("RGB 555", 16, V4L2_PIX_FMT_RGB555),
	RGB_FMT("Bayer (sRGB) 10 bit", 10, V4L2_PIX_FMT_SBGGR16),
	RGB_FMT("Bayer (sRGB) 8 bit", 8, V4L2_PIX_FMT_SBGGR8),
};

enum mt9m111_context {
	HIGHPOWER = 0,
	LOWPOWER,
};

struct mt9m111 {
	struct i2c_client *client;
	struct soc_camera_device icd;
	int model;	/* V4L2_IDENT_MT9M11x* codes from v4l2-chip-ident.h */
	enum mt9m111_context context;
	unsigned int left, top, width, height;
	u32 pixfmt;
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

static int mt9m111_reg_read(struct soc_camera_device *icd, const u16 reg)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	struct i2c_client *client = mt9m111->client;
	int ret;

	ret = reg_page_map_set(client, reg);
	if (!ret)
		ret = swab16(i2c_smbus_read_word_data(client, (reg & 0xff)));

	dev_dbg(&icd->dev, "read  reg.%03x -> %04x\n", reg, ret);
	return ret;
}

static int mt9m111_reg_write(struct soc_camera_device *icd, const u16 reg,
			     const u16 data)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	struct i2c_client *client = mt9m111->client;
	int ret;

	ret = reg_page_map_set(client, reg);
	if (!ret)
		ret = i2c_smbus_write_word_data(mt9m111->client, (reg & 0xff),
						swab16(data));
	dev_dbg(&icd->dev, "write reg.%03x = %04x -> %d\n", reg, data, ret);
	return ret;
}

static int mt9m111_reg_set(struct soc_camera_device *icd, const u16 reg,
			   const u16 data)
{
	int ret;

	ret = mt9m111_reg_read(icd, reg);
	if (ret >= 0)
		ret = mt9m111_reg_write(icd, reg, ret | data);
	return ret;
}

static int mt9m111_reg_clear(struct soc_camera_device *icd, const u16 reg,
			     const u16 data)
{
	int ret;

	ret = mt9m111_reg_read(icd, reg);
	return mt9m111_reg_write(icd, reg, ret & ~data);
}

static int mt9m111_set_context(struct soc_camera_device *icd,
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

static int mt9m111_setup_rect(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int ret, is_raw_format;
	int width = mt9m111->width;
	int height = mt9m111->height;

	if ((mt9m111->pixfmt == V4L2_PIX_FMT_SBGGR8)
	    || (mt9m111->pixfmt == V4L2_PIX_FMT_SBGGR16))
		is_raw_format = 1;
	else
		is_raw_format = 0;

	ret = reg_write(COLUMN_START, mt9m111->left);
	if (!ret)
		ret = reg_write(ROW_START, mt9m111->top);

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

static int mt9m111_setup_pixfmt(struct soc_camera_device *icd, u16 outfmt)
{
	int ret;

	ret = reg_write(OUTPUT_FORMAT_CTRL2_A, outfmt);
	if (!ret)
		ret = reg_write(OUTPUT_FORMAT_CTRL2_B, outfmt);
	return ret;
}

static int mt9m111_setfmt_bayer8(struct soc_camera_device *icd)
{
	return mt9m111_setup_pixfmt(icd, MT9M111_OUTFMT_PROCESSED_BAYER);
}

static int mt9m111_setfmt_bayer10(struct soc_camera_device *icd)
{
	return mt9m111_setup_pixfmt(icd, MT9M111_OUTFMT_BYPASS_IFP);
}

static int mt9m111_setfmt_rgb565(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int val = 0;

	if (mt9m111->swap_rgb_red_blue)
		val |= MT9M111_OUTFMT_SWAP_YCbCr_Cb_Cr;
	if (mt9m111->swap_rgb_even_odd)
		val |= MT9M111_OUTFMT_SWAP_RGB_EVEN;
	val |= MT9M111_OUTFMT_RGB | MT9M111_OUTFMT_RGB565;

	return mt9m111_setup_pixfmt(icd, val);
}

static int mt9m111_setfmt_rgb555(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int val = 0;

	if (mt9m111->swap_rgb_red_blue)
		val |= MT9M111_OUTFMT_SWAP_YCbCr_Cb_Cr;
	if (mt9m111->swap_rgb_even_odd)
		val |= MT9M111_OUTFMT_SWAP_RGB_EVEN;
	val |= MT9M111_OUTFMT_RGB | MT9M111_OUTFMT_RGB555;

	return mt9m111_setup_pixfmt(icd, val);
}

static int mt9m111_setfmt_yuv(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int val = 0;

	if (mt9m111->swap_yuv_cb_cr)
		val |= MT9M111_OUTFMT_SWAP_YCbCr_Cb_Cr;
	if (mt9m111->swap_yuv_y_chromas)
		val |= MT9M111_OUTFMT_SWAP_YCbCr_C_Y;

	return mt9m111_setup_pixfmt(icd, val);
}

static int mt9m111_enable(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	struct soc_camera_link *icl = mt9m111->client->dev.platform_data;
	int ret;

	if (icl->power) {
		ret = icl->power(&mt9m111->client->dev, 1);
		if (ret < 0) {
			dev_err(icd->vdev->parent,
				"Platform failed to power-on the camera.\n");
			return ret;
		}
	}

	ret = reg_set(RESET, MT9M111_RESET_CHIP_ENABLE);
	if (!ret)
		mt9m111->powered = 1;
	return ret;
}

static int mt9m111_disable(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	struct soc_camera_link *icl = mt9m111->client->dev.platform_data;
	int ret;

	ret = reg_clear(RESET, MT9M111_RESET_CHIP_ENABLE);
	if (!ret)
		mt9m111->powered = 0;

	if (icl->power)
		icl->power(&mt9m111->client->dev, 0);

	return ret;
}

static int mt9m111_reset(struct soc_camera_device *icd)
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

static int mt9m111_start_capture(struct soc_camera_device *icd)
{
	return 0;
}

static int mt9m111_stop_capture(struct soc_camera_device *icd)
{
	return 0;
}

static unsigned long mt9m111_query_bus_param(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	struct soc_camera_link *icl = mt9m111->client->dev.platform_data;
	unsigned long flags = SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING |
		SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_HIGH |
		SOCAM_DATAWIDTH_8;

	return soc_camera_apply_sensor_flags(icl, flags);
}

static int mt9m111_set_bus_param(struct soc_camera_device *icd, unsigned long f)
{
	return 0;
}

static int mt9m111_set_pixfmt(struct soc_camera_device *icd, u32 pixfmt)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int ret;

	switch (pixfmt) {
	case V4L2_PIX_FMT_SBGGR8:
		ret = mt9m111_setfmt_bayer8(icd);
		break;
	case V4L2_PIX_FMT_SBGGR16:
		ret = mt9m111_setfmt_bayer10(icd);
		break;
	case V4L2_PIX_FMT_RGB555:
		ret = mt9m111_setfmt_rgb555(icd);
		break;
	case V4L2_PIX_FMT_RGB565:
		ret = mt9m111_setfmt_rgb565(icd);
		break;
	case V4L2_PIX_FMT_UYVY:
		mt9m111->swap_yuv_y_chromas = 0;
		mt9m111->swap_yuv_cb_cr = 0;
		ret = mt9m111_setfmt_yuv(icd);
		break;
	case V4L2_PIX_FMT_VYUY:
		mt9m111->swap_yuv_y_chromas = 0;
		mt9m111->swap_yuv_cb_cr = 1;
		ret = mt9m111_setfmt_yuv(icd);
		break;
	case V4L2_PIX_FMT_YUYV:
		mt9m111->swap_yuv_y_chromas = 1;
		mt9m111->swap_yuv_cb_cr = 0;
		ret = mt9m111_setfmt_yuv(icd);
		break;
	case V4L2_PIX_FMT_YVYU:
		mt9m111->swap_yuv_y_chromas = 1;
		mt9m111->swap_yuv_cb_cr = 1;
		ret = mt9m111_setfmt_yuv(icd);
		break;
	default:
		dev_err(&icd->dev, "Pixel format not handled : %x\n", pixfmt);
		ret = -EINVAL;
	}

	if (!ret)
		mt9m111->pixfmt = pixfmt;

	return ret;
}

static int mt9m111_set_fmt(struct soc_camera_device *icd,
			   __u32 pixfmt, struct v4l2_rect *rect)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int ret;

	mt9m111->left = rect->left;
	mt9m111->top = rect->top;
	mt9m111->width = rect->width;
	mt9m111->height = rect->height;

	dev_dbg(&icd->dev, "%s fmt=%x left=%d, top=%d, width=%d, height=%d\n",
		__func__, pixfmt, mt9m111->left, mt9m111->top, mt9m111->width,
		mt9m111->height);

	ret = mt9m111_setup_rect(icd);
	if (!ret)
		ret = mt9m111_set_pixfmt(icd, pixfmt);
	return ret;
}

static int mt9m111_try_fmt(struct soc_camera_device *icd,
			   struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (pix->height > MT9M111_MAX_HEIGHT)
		pix->height = MT9M111_MAX_HEIGHT;
	if (pix->width > MT9M111_MAX_WIDTH)
		pix->width = MT9M111_MAX_WIDTH;

	return 0;
}

static int mt9m111_get_chip_id(struct soc_camera_device *icd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);

	if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match.addr != mt9m111->client->addr)
		return -ENODEV;

	id->ident	= mt9m111->model;
	id->revision	= 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9m111_get_register(struct soc_camera_device *icd,
				struct v4l2_dbg_register *reg)
{
	int val;

	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0x2ff)
		return -EINVAL;
	if (reg->match.addr != mt9m111->client->addr)
		return -ENODEV;

	val = mt9m111_reg_read(icd, reg->reg);
	reg->size = 2;
	reg->val = (u64)val;

	if (reg->val > 0xffff)
		return -EIO;

	return 0;
}

static int mt9m111_set_register(struct soc_camera_device *icd,
				struct v4l2_dbg_register *reg)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->reg > 0x2ff)
		return -EINVAL;

	if (reg->match.addr != mt9m111->client->addr)
		return -ENODEV;

	if (mt9m111_reg_write(icd, reg->reg, reg->val) < 0)
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

static int mt9m111_video_probe(struct soc_camera_device *);
static void mt9m111_video_remove(struct soc_camera_device *);
static int mt9m111_get_control(struct soc_camera_device *,
			       struct v4l2_control *);
static int mt9m111_set_control(struct soc_camera_device *,
			       struct v4l2_control *);
static int mt9m111_resume(struct soc_camera_device *icd);
static int mt9m111_init(struct soc_camera_device *icd);
static int mt9m111_release(struct soc_camera_device *icd);

static struct soc_camera_ops mt9m111_ops = {
	.owner			= THIS_MODULE,
	.probe			= mt9m111_video_probe,
	.remove			= mt9m111_video_remove,
	.init			= mt9m111_init,
	.resume			= mt9m111_resume,
	.release		= mt9m111_release,
	.start_capture		= mt9m111_start_capture,
	.stop_capture		= mt9m111_stop_capture,
	.set_fmt		= mt9m111_set_fmt,
	.try_fmt		= mt9m111_try_fmt,
	.query_bus_param	= mt9m111_query_bus_param,
	.set_bus_param		= mt9m111_set_bus_param,
	.controls		= mt9m111_controls,
	.num_controls		= ARRAY_SIZE(mt9m111_controls),
	.get_control		= mt9m111_get_control,
	.set_control		= mt9m111_set_control,
	.get_chip_id		= mt9m111_get_chip_id,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.get_register		= mt9m111_get_register,
	.set_register		= mt9m111_set_register,
#endif
};

static int mt9m111_set_flip(struct soc_camera_device *icd, int flip, int mask)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
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

static int mt9m111_get_global_gain(struct soc_camera_device *icd)
{
	int data;

	data = reg_read(GLOBAL_GAIN);
	if (data >= 0)
		return (data & 0x2f) * (1 << ((data >> 10) & 1)) *
			(1 << ((data >> 9) & 1));
	return data;
}

static int mt9m111_set_global_gain(struct soc_camera_device *icd, int gain)
{
	u16 val;

	if (gain > 63 * 2 * 2)
		return -EINVAL;

	icd->gain = gain;
	if ((gain >= 64 * 2) && (gain < 63 * 2 * 2))
		val = (1 << 10) | (1 << 9) | (gain / 4);
	else if ((gain >= 64) && (gain < 64 * 2))
		val = (1 << 9) | (gain / 2);
	else
		val = gain;

	return reg_write(GLOBAL_GAIN, val);
}

static int mt9m111_set_autoexposure(struct soc_camera_device *icd, int on)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int ret;

	if (on)
		ret = reg_set(OPER_MODE_CTRL, MT9M111_OPMODE_AUTOEXPO_EN);
	else
		ret = reg_clear(OPER_MODE_CTRL, MT9M111_OPMODE_AUTOEXPO_EN);

	if (!ret)
		mt9m111->autoexposure = on;

	return ret;
}

static int mt9m111_set_autowhitebalance(struct soc_camera_device *icd, int on)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int ret;

	if (on)
		ret = reg_set(OPER_MODE_CTRL, MT9M111_OPMODE_AUTOWHITEBAL_EN);
	else
		ret = reg_clear(OPER_MODE_CTRL, MT9M111_OPMODE_AUTOWHITEBAL_EN);

	if (!ret)
		mt9m111->autowhitebalance = on;

	return ret;
}

static int mt9m111_get_control(struct soc_camera_device *icd,
			       struct v4l2_control *ctrl)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
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
		data = mt9m111_get_global_gain(icd);
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

static int mt9m111_set_control(struct soc_camera_device *icd,
			       struct v4l2_control *ctrl)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	const struct v4l2_queryctrl *qctrl;
	int ret;

	qctrl = soc_camera_find_qctrl(&mt9m111_ops, ctrl->id);

	if (!qctrl)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		mt9m111->vflip = ctrl->value;
		ret = mt9m111_set_flip(icd, ctrl->value,
					MT9M111_RMB_MIRROR_ROWS);
		break;
	case V4L2_CID_HFLIP:
		mt9m111->hflip = ctrl->value;
		ret = mt9m111_set_flip(icd, ctrl->value,
					MT9M111_RMB_MIRROR_COLS);
		break;
	case V4L2_CID_GAIN:
		ret = mt9m111_set_global_gain(icd, ctrl->value);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ret =  mt9m111_set_autoexposure(icd, ctrl->value);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret =  mt9m111_set_autowhitebalance(icd, ctrl->value);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int mt9m111_restore_state(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);

	mt9m111_set_context(icd, mt9m111->context);
	mt9m111_set_pixfmt(icd, mt9m111->pixfmt);
	mt9m111_setup_rect(icd);
	mt9m111_set_flip(icd, mt9m111->hflip, MT9M111_RMB_MIRROR_COLS);
	mt9m111_set_flip(icd, mt9m111->vflip, MT9M111_RMB_MIRROR_ROWS);
	mt9m111_set_global_gain(icd, icd->gain);
	mt9m111_set_autoexposure(icd, mt9m111->autoexposure);
	mt9m111_set_autowhitebalance(icd, mt9m111->autowhitebalance);
	return 0;
}

static int mt9m111_resume(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int ret = 0;

	if (mt9m111->powered) {
		ret = mt9m111_enable(icd);
		if (!ret)
			ret = mt9m111_reset(icd);
		if (!ret)
			ret = mt9m111_restore_state(icd);
	}
	return ret;
}

static int mt9m111_init(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	int ret;

	mt9m111->context = HIGHPOWER;
	ret = mt9m111_enable(icd);
	if (!ret)
		ret = mt9m111_reset(icd);
	if (!ret)
		ret = mt9m111_set_context(icd, mt9m111->context);
	if (!ret)
		ret = mt9m111_set_autoexposure(icd, mt9m111->autoexposure);
	if (ret)
		dev_err(&icd->dev, "mt9m11x init failed: %d\n", ret);
	return ret;
}

static int mt9m111_release(struct soc_camera_device *icd)
{
	int ret;

	ret = mt9m111_disable(icd);
	if (ret < 0)
		dev_err(&icd->dev, "mt9m11x release failed: %d\n", ret);

	return ret;
}

/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int mt9m111_video_probe(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);
	s32 data;
	int ret;

	/*
	 * We must have a parent by now. And it cannot be a wrong one.
	 * So this entire test is completely redundant.
	 */
	if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	ret = mt9m111_enable(icd);
	if (ret)
		goto ei2c;
	ret = mt9m111_reset(icd);
	if (ret)
		goto ei2c;

	data = reg_read(CHIP_VERSION);

	switch (data) {
	case 0x143a: /* MT9M111 */
		mt9m111->model = V4L2_IDENT_MT9M111;
		break;
	case 0x148c: /* MT9M112 */
		mt9m111->model = V4L2_IDENT_MT9M112;
		break;
	default:
		ret = -ENODEV;
		dev_err(&icd->dev,
			"No MT9M11x chip detected, register read %x\n", data);
		goto ei2c;
	}

	icd->formats = mt9m111_colour_formats;
	icd->num_formats = ARRAY_SIZE(mt9m111_colour_formats);

	dev_info(&icd->dev, "Detected a MT9M11x chip ID %x\n", data);

	ret = soc_camera_video_start(icd);
	if (ret)
		goto eisis;

	mt9m111->autoexposure = 1;
	mt9m111->autowhitebalance = 1;

	mt9m111->swap_rgb_even_odd = 1;
	mt9m111->swap_rgb_red_blue = 1;

	return 0;
eisis:
ei2c:
	return ret;
}

static void mt9m111_video_remove(struct soc_camera_device *icd)
{
	struct mt9m111 *mt9m111 = container_of(icd, struct mt9m111, icd);

	dev_dbg(&icd->dev, "Video %x removed: %p, %p\n", mt9m111->client->addr,
		mt9m111->icd.dev.parent, mt9m111->icd.vdev);
	soc_camera_video_stop(&mt9m111->icd);
}

static int mt9m111_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9m111 *mt9m111;
	struct soc_camera_device *icd;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct soc_camera_link *icl = client->dev.platform_data;
	int ret;

	if (!icl) {
		dev_err(&client->dev, "MT9M11x driver needs platform data\n");
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

	mt9m111->client = client;
	i2c_set_clientdata(client, mt9m111);

	/* Second stage probe - when a capture adapter is there */
	icd 		= &mt9m111->icd;
	icd->ops	= &mt9m111_ops;
	icd->control	= &client->dev;
	icd->x_min	= MT9M111_MIN_DARK_COLS;
	icd->y_min	= MT9M111_MIN_DARK_ROWS;
	icd->x_current	= icd->x_min;
	icd->y_current	= icd->y_min;
	icd->width_min	= MT9M111_MIN_DARK_ROWS;
	icd->width_max	= MT9M111_MAX_WIDTH;
	icd->height_min	= MT9M111_MIN_DARK_COLS;
	icd->height_max	= MT9M111_MAX_HEIGHT;
	icd->y_skip_top	= 0;
	icd->iface	= icl->bus_id;

	ret = soc_camera_device_register(icd);
	if (ret)
		goto eisdr;
	return 0;

eisdr:
	kfree(mt9m111);
	return ret;
}

static int mt9m111_remove(struct i2c_client *client)
{
	struct mt9m111 *mt9m111 = i2c_get_clientdata(client);
	soc_camera_device_unregister(&mt9m111->icd);
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

MODULE_DESCRIPTION("Micron MT9M111/MT9M112 Camera driver");
MODULE_AUTHOR("Robert Jarzmik");
MODULE_LICENSE("GPL");
