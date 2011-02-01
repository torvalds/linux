/*
 * Driver for RJ54N1CB0C CMOS Image Sensor from Sharp
 *
 * Copyright (C) 2009, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/rj54n1cb0c.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-chip-ident.h>

#define RJ54N1_DEV_CODE			0x0400
#define RJ54N1_DEV_CODE2		0x0401
#define RJ54N1_OUT_SEL			0x0403
#define RJ54N1_XY_OUTPUT_SIZE_S_H	0x0404
#define RJ54N1_X_OUTPUT_SIZE_S_L	0x0405
#define RJ54N1_Y_OUTPUT_SIZE_S_L	0x0406
#define RJ54N1_XY_OUTPUT_SIZE_P_H	0x0407
#define RJ54N1_X_OUTPUT_SIZE_P_L	0x0408
#define RJ54N1_Y_OUTPUT_SIZE_P_L	0x0409
#define RJ54N1_LINE_LENGTH_PCK_S_H	0x040a
#define RJ54N1_LINE_LENGTH_PCK_S_L	0x040b
#define RJ54N1_LINE_LENGTH_PCK_P_H	0x040c
#define RJ54N1_LINE_LENGTH_PCK_P_L	0x040d
#define RJ54N1_RESIZE_N			0x040e
#define RJ54N1_RESIZE_N_STEP		0x040f
#define RJ54N1_RESIZE_STEP		0x0410
#define RJ54N1_RESIZE_HOLD_H		0x0411
#define RJ54N1_RESIZE_HOLD_L		0x0412
#define RJ54N1_H_OBEN_OFS		0x0413
#define RJ54N1_V_OBEN_OFS		0x0414
#define RJ54N1_RESIZE_CONTROL		0x0415
#define RJ54N1_STILL_CONTROL		0x0417
#define RJ54N1_INC_USE_SEL_H		0x0425
#define RJ54N1_INC_USE_SEL_L		0x0426
#define RJ54N1_MIRROR_STILL_MODE	0x0427
#define RJ54N1_INIT_START		0x0428
#define RJ54N1_SCALE_1_2_LEV		0x0429
#define RJ54N1_SCALE_4_LEV		0x042a
#define RJ54N1_Y_GAIN			0x04d8
#define RJ54N1_APT_GAIN_UP		0x04fa
#define RJ54N1_RA_SEL_UL		0x0530
#define RJ54N1_BYTE_SWAP		0x0531
#define RJ54N1_OUT_SIGPO		0x053b
#define RJ54N1_WB_SEL_WEIGHT_I		0x054e
#define RJ54N1_BIT8_WB			0x0569
#define RJ54N1_HCAPS_WB			0x056a
#define RJ54N1_VCAPS_WB			0x056b
#define RJ54N1_HCAPE_WB			0x056c
#define RJ54N1_VCAPE_WB			0x056d
#define RJ54N1_EXPOSURE_CONTROL		0x058c
#define RJ54N1_FRAME_LENGTH_S_H		0x0595
#define RJ54N1_FRAME_LENGTH_S_L		0x0596
#define RJ54N1_FRAME_LENGTH_P_H		0x0597
#define RJ54N1_FRAME_LENGTH_P_L		0x0598
#define RJ54N1_PEAK_H			0x05b7
#define RJ54N1_PEAK_50			0x05b8
#define RJ54N1_PEAK_60			0x05b9
#define RJ54N1_PEAK_DIFF		0x05ba
#define RJ54N1_IOC			0x05ef
#define RJ54N1_TG_BYPASS		0x0700
#define RJ54N1_PLL_L			0x0701
#define RJ54N1_PLL_N			0x0702
#define RJ54N1_PLL_EN			0x0704
#define RJ54N1_RATIO_TG			0x0706
#define RJ54N1_RATIO_T			0x0707
#define RJ54N1_RATIO_R			0x0708
#define RJ54N1_RAMP_TGCLK_EN		0x0709
#define RJ54N1_OCLK_DSP			0x0710
#define RJ54N1_RATIO_OP			0x0711
#define RJ54N1_RATIO_O			0x0712
#define RJ54N1_OCLK_SEL_EN		0x0713
#define RJ54N1_CLK_RST			0x0717
#define RJ54N1_RESET_STANDBY		0x0718
#define RJ54N1_FWFLG			0x07fe

#define E_EXCLK				(1 << 7)
#define SOFT_STDBY			(1 << 4)
#define SEN_RSTX			(1 << 2)
#define TG_RSTX				(1 << 1)
#define DSP_RSTX			(1 << 0)

#define RESIZE_HOLD_SEL			(1 << 2)
#define RESIZE_GO			(1 << 1)

/*
 * When cropping, the camera automatically centers the cropped region, there
 * doesn't seem to be a way to specify an explicit location of the rectangle.
 */
#define RJ54N1_COLUMN_SKIP		0
#define RJ54N1_ROW_SKIP			0
#define RJ54N1_MAX_WIDTH		1600
#define RJ54N1_MAX_HEIGHT		1200

#define PLL_L				2
#define PLL_N				0x31

/* I2C addresses: 0x50, 0x51, 0x60, 0x61 */

/* RJ54N1CB0C has only one fixed colorspace per pixelcode */
struct rj54n1_datafmt {
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_colorspace		colorspace;
};

/* Find a data format by a pixel code in an array */
static const struct rj54n1_datafmt *rj54n1_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct rj54n1_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}

static const struct rj54n1_datafmt rj54n1_colour_fmts[] = {
	{V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_YVYU8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_RGB565_2X8_LE, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_RGB565_2X8_BE, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_LE, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_BE, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_BE, V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SBGGR10_1X10, V4L2_COLORSPACE_SRGB},
};

struct rj54n1_clock_div {
	u8 ratio_tg;	/* can be 0 or an odd number */
	u8 ratio_t;
	u8 ratio_r;
	u8 ratio_op;
	u8 ratio_o;
};

struct rj54n1 {
	struct v4l2_subdev subdev;
	struct rj54n1_clock_div clk_div;
	const struct rj54n1_datafmt *fmt;
	struct v4l2_rect rect;	/* Sensor window */
	unsigned int tgclk_mhz;
	bool auto_wb;
	unsigned short width;	/* Output window */
	unsigned short height;
	unsigned short resize;	/* Sensor * 1024 / resize = Output */
	unsigned short scale;
	u8 bank;
};

struct rj54n1_reg_val {
	u16 reg;
	u8 val;
};

static const struct rj54n1_reg_val bank_4[] = {
	{0x417, 0},
	{0x42c, 0},
	{0x42d, 0xf0},
	{0x42e, 0},
	{0x42f, 0x50},
	{0x430, 0xf5},
	{0x431, 0x16},
	{0x432, 0x20},
	{0x433, 0},
	{0x434, 0xc8},
	{0x43c, 8},
	{0x43e, 0x90},
	{0x445, 0x83},
	{0x4ba, 0x58},
	{0x4bb, 4},
	{0x4bc, 0x20},
	{0x4db, 4},
	{0x4fe, 2},
};

static const struct rj54n1_reg_val bank_5[] = {
	{0x514, 0},
	{0x516, 0},
	{0x518, 0},
	{0x51a, 0},
	{0x51d, 0xff},
	{0x56f, 0x28},
	{0x575, 0x40},
	{0x5bc, 0x48},
	{0x5c1, 6},
	{0x5e5, 0x11},
	{0x5e6, 0x43},
	{0x5e7, 0x33},
	{0x5e8, 0x21},
	{0x5e9, 0x30},
	{0x5ea, 0x0},
	{0x5eb, 0xa5},
	{0x5ec, 0xff},
	{0x5fe, 2},
};

static const struct rj54n1_reg_val bank_7[] = {
	{0x70a, 0},
	{0x714, 0xff},
	{0x715, 0xff},
	{0x716, 0x1f},
	{0x7FE, 2},
};

static const struct rj54n1_reg_val bank_8[] = {
	{0x800, 0x00},
	{0x801, 0x01},
	{0x802, 0x61},
	{0x805, 0x00},
	{0x806, 0x00},
	{0x807, 0x00},
	{0x808, 0x00},
	{0x809, 0x01},
	{0x80A, 0x61},
	{0x80B, 0x00},
	{0x80C, 0x01},
	{0x80D, 0x00},
	{0x80E, 0x00},
	{0x80F, 0x00},
	{0x810, 0x00},
	{0x811, 0x01},
	{0x812, 0x61},
	{0x813, 0x00},
	{0x814, 0x11},
	{0x815, 0x00},
	{0x816, 0x41},
	{0x817, 0x00},
	{0x818, 0x51},
	{0x819, 0x01},
	{0x81A, 0x1F},
	{0x81B, 0x00},
	{0x81C, 0x01},
	{0x81D, 0x00},
	{0x81E, 0x11},
	{0x81F, 0x00},
	{0x820, 0x41},
	{0x821, 0x00},
	{0x822, 0x51},
	{0x823, 0x00},
	{0x824, 0x00},
	{0x825, 0x00},
	{0x826, 0x47},
	{0x827, 0x01},
	{0x828, 0x4F},
	{0x829, 0x00},
	{0x82A, 0x00},
	{0x82B, 0x00},
	{0x82C, 0x30},
	{0x82D, 0x00},
	{0x82E, 0x40},
	{0x82F, 0x00},
	{0x830, 0xB3},
	{0x831, 0x00},
	{0x832, 0xE3},
	{0x833, 0x00},
	{0x834, 0x00},
	{0x835, 0x00},
	{0x836, 0x00},
	{0x837, 0x00},
	{0x838, 0x00},
	{0x839, 0x01},
	{0x83A, 0x61},
	{0x83B, 0x00},
	{0x83C, 0x01},
	{0x83D, 0x00},
	{0x83E, 0x00},
	{0x83F, 0x00},
	{0x840, 0x00},
	{0x841, 0x01},
	{0x842, 0x61},
	{0x843, 0x00},
	{0x844, 0x1D},
	{0x845, 0x00},
	{0x846, 0x00},
	{0x847, 0x00},
	{0x848, 0x00},
	{0x849, 0x01},
	{0x84A, 0x1F},
	{0x84B, 0x00},
	{0x84C, 0x05},
	{0x84D, 0x00},
	{0x84E, 0x19},
	{0x84F, 0x01},
	{0x850, 0x21},
	{0x851, 0x01},
	{0x852, 0x5D},
	{0x853, 0x00},
	{0x854, 0x00},
	{0x855, 0x00},
	{0x856, 0x19},
	{0x857, 0x01},
	{0x858, 0x21},
	{0x859, 0x00},
	{0x85A, 0x00},
	{0x85B, 0x00},
	{0x85C, 0x00},
	{0x85D, 0x00},
	{0x85E, 0x00},
	{0x85F, 0x00},
	{0x860, 0xB3},
	{0x861, 0x00},
	{0x862, 0xE3},
	{0x863, 0x00},
	{0x864, 0x00},
	{0x865, 0x00},
	{0x866, 0x00},
	{0x867, 0x00},
	{0x868, 0x00},
	{0x869, 0xE2},
	{0x86A, 0x00},
	{0x86B, 0x01},
	{0x86C, 0x06},
	{0x86D, 0x00},
	{0x86E, 0x00},
	{0x86F, 0x00},
	{0x870, 0x60},
	{0x871, 0x8C},
	{0x872, 0x10},
	{0x873, 0x00},
	{0x874, 0xE0},
	{0x875, 0x00},
	{0x876, 0x27},
	{0x877, 0x01},
	{0x878, 0x00},
	{0x879, 0x00},
	{0x87A, 0x00},
	{0x87B, 0x03},
	{0x87C, 0x00},
	{0x87D, 0x00},
	{0x87E, 0x00},
	{0x87F, 0x00},
	{0x880, 0x00},
	{0x881, 0x00},
	{0x882, 0x00},
	{0x883, 0x00},
	{0x884, 0x00},
	{0x885, 0x00},
	{0x886, 0xF8},
	{0x887, 0x00},
	{0x888, 0x03},
	{0x889, 0x00},
	{0x88A, 0x64},
	{0x88B, 0x00},
	{0x88C, 0x03},
	{0x88D, 0x00},
	{0x88E, 0xB1},
	{0x88F, 0x00},
	{0x890, 0x03},
	{0x891, 0x01},
	{0x892, 0x1D},
	{0x893, 0x00},
	{0x894, 0x03},
	{0x895, 0x01},
	{0x896, 0x4B},
	{0x897, 0x00},
	{0x898, 0xE5},
	{0x899, 0x00},
	{0x89A, 0x01},
	{0x89B, 0x00},
	{0x89C, 0x01},
	{0x89D, 0x04},
	{0x89E, 0xC8},
	{0x89F, 0x00},
	{0x8A0, 0x01},
	{0x8A1, 0x01},
	{0x8A2, 0x61},
	{0x8A3, 0x00},
	{0x8A4, 0x01},
	{0x8A5, 0x00},
	{0x8A6, 0x00},
	{0x8A7, 0x00},
	{0x8A8, 0x00},
	{0x8A9, 0x00},
	{0x8AA, 0x7F},
	{0x8AB, 0x03},
	{0x8AC, 0x00},
	{0x8AD, 0x00},
	{0x8AE, 0x00},
	{0x8AF, 0x00},
	{0x8B0, 0x00},
	{0x8B1, 0x00},
	{0x8B6, 0x00},
	{0x8B7, 0x01},
	{0x8B8, 0x00},
	{0x8B9, 0x00},
	{0x8BA, 0x02},
	{0x8BB, 0x00},
	{0x8BC, 0xFF},
	{0x8BD, 0x00},
	{0x8FE, 2},
};

static const struct rj54n1_reg_val bank_10[] = {
	{0x10bf, 0x69}
};

/* Clock dividers - these are default register values, divider = register + 1 */
static const struct rj54n1_clock_div clk_div = {
	.ratio_tg	= 3 /* default: 5 */,
	.ratio_t	= 4 /* default: 1 */,
	.ratio_r	= 4 /* default: 0 */,
	.ratio_op	= 1 /* default: 5 */,
	.ratio_o	= 9 /* default: 0 */,
};

static struct rj54n1 *to_rj54n1(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct rj54n1, subdev);
}

static int reg_read(struct i2c_client *client, const u16 reg)
{
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	int ret;

	/* set bank */
	if (rj54n1->bank != reg >> 8) {
		dev_dbg(&client->dev, "[0x%x] = 0x%x\n", 0xff, reg >> 8);
		ret = i2c_smbus_write_byte_data(client, 0xff, reg >> 8);
		if (ret < 0)
			return ret;
		rj54n1->bank = reg >> 8;
	}
	return i2c_smbus_read_byte_data(client, reg & 0xff);
}

static int reg_write(struct i2c_client *client, const u16 reg,
		     const u8 data)
{
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	int ret;

	/* set bank */
	if (rj54n1->bank != reg >> 8) {
		dev_dbg(&client->dev, "[0x%x] = 0x%x\n", 0xff, reg >> 8);
		ret = i2c_smbus_write_byte_data(client, 0xff, reg >> 8);
		if (ret < 0)
			return ret;
		rj54n1->bank = reg >> 8;
	}
	dev_dbg(&client->dev, "[0x%x] = 0x%x\n", reg & 0xff, data);
	return i2c_smbus_write_byte_data(client, reg & 0xff, data);
}

static int reg_set(struct i2c_client *client, const u16 reg,
		   const u8 data, const u8 mask)
{
	int ret;

	ret = reg_read(client, reg);
	if (ret < 0)
		return ret;
	return reg_write(client, reg, (ret & ~mask) | (data & mask));
}

static int reg_write_multiple(struct i2c_client *client,
			      const struct rj54n1_reg_val *rv, const int n)
{
	int i, ret;

	for (i = 0; i < n; i++) {
		ret = reg_write(client, rv->reg, rv->val);
		if (ret < 0)
			return ret;
		rv++;
	}

	return 0;
}

static int rj54n1_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(rj54n1_colour_fmts))
		return -EINVAL;

	*code = rj54n1_colour_fmts[index].code;
	return 0;
}

static int rj54n1_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/* Switch between preview and still shot modes */
	return reg_set(client, RJ54N1_STILL_CONTROL, (!enable) << 7, 0x80);
}

static int rj54n1_set_bus_param(struct soc_camera_device *icd,
				unsigned long flags)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	/* Figures 2.5-1 to 2.5-3 - default falling pixclk edge */

	if (flags & SOCAM_PCLK_SAMPLE_RISING)
		return reg_write(client, RJ54N1_OUT_SIGPO, 1 << 4);
	else
		return reg_write(client, RJ54N1_OUT_SIGPO, 0);
}

static unsigned long rj54n1_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	const unsigned long flags =
		SOCAM_PCLK_SAMPLE_RISING | SOCAM_PCLK_SAMPLE_FALLING |
		SOCAM_MASTER | SOCAM_DATAWIDTH_8 |
		SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_HIGH |
		SOCAM_DATA_ACTIVE_HIGH;

	return soc_camera_apply_sensor_flags(icl, flags);
}

static int rj54n1_set_rect(struct i2c_client *client,
			   u16 reg_x, u16 reg_y, u16 reg_xy,
			   u32 width, u32 height)
{
	int ret;

	ret = reg_write(client, reg_xy,
			((width >> 4) & 0x70) |
			((height >> 8) & 7));

	if (!ret)
		ret = reg_write(client, reg_x, width & 0xff);
	if (!ret)
		ret = reg_write(client, reg_y, height & 0xff);

	return ret;
}

/*
 * Some commands, specifically certain initialisation sequences, require
 * a commit operation.
 */
static int rj54n1_commit(struct i2c_client *client)
{
	int ret = reg_write(client, RJ54N1_INIT_START, 1);
	msleep(10);
	if (!ret)
		ret = reg_write(client, RJ54N1_INIT_START, 0);
	return ret;
}

static int rj54n1_sensor_scale(struct v4l2_subdev *sd, s32 *in_w, s32 *in_h,
			       s32 *out_w, s32 *out_h);

static int rj54n1_s_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	struct v4l2_rect *rect = &a->c;
	int dummy = 0, output_w, output_h,
		input_w = rect->width, input_h = rect->height;
	int ret;

	/* arbitrary minimum width and height, edges unimportant */
	soc_camera_limit_side(&dummy, &input_w,
		     RJ54N1_COLUMN_SKIP, 8, RJ54N1_MAX_WIDTH);

	soc_camera_limit_side(&dummy, &input_h,
		     RJ54N1_ROW_SKIP, 8, RJ54N1_MAX_HEIGHT);

	output_w = (input_w * 1024 + rj54n1->resize / 2) / rj54n1->resize;
	output_h = (input_h * 1024 + rj54n1->resize / 2) / rj54n1->resize;

	dev_dbg(&client->dev, "Scaling for %dx%d : %u = %dx%d\n",
		input_w, input_h, rj54n1->resize, output_w, output_h);

	ret = rj54n1_sensor_scale(sd, &input_w, &input_h, &output_w, &output_h);
	if (ret < 0)
		return ret;

	rj54n1->width		= output_w;
	rj54n1->height		= output_h;
	rj54n1->resize		= ret;
	rj54n1->rect.width	= input_w;
	rj54n1->rect.height	= input_h;

	return 0;
}

static int rj54n1_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rj54n1 *rj54n1 = to_rj54n1(client);

	a->c	= rj54n1->rect;
	a->type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int rj54n1_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= RJ54N1_COLUMN_SKIP;
	a->bounds.top			= RJ54N1_ROW_SKIP;
	a->bounds.width			= RJ54N1_MAX_WIDTH;
	a->bounds.height		= RJ54N1_MAX_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int rj54n1_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rj54n1 *rj54n1 = to_rj54n1(client);

	mf->code	= rj54n1->fmt->code;
	mf->colorspace	= rj54n1->fmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;
	mf->width	= rj54n1->width;
	mf->height	= rj54n1->height;

	return 0;
}

/*
 * The actual geometry configuration routine. It scales the input window into
 * the output one, updates the window sizes and returns an error or the resize
 * coefficient on success. Note: we only use the "Fixed Scaling" on this camera.
 */
static int rj54n1_sensor_scale(struct v4l2_subdev *sd, s32 *in_w, s32 *in_h,
			       s32 *out_w, s32 *out_h)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	unsigned int skip, resize, input_w = *in_w, input_h = *in_h,
		output_w = *out_w, output_h = *out_h;
	u16 inc_sel, wb_bit8, wb_left, wb_right, wb_top, wb_bottom;
	unsigned int peak, peak_50, peak_60;
	int ret;

	/*
	 * We have a problem with crops, where the window is larger than 512x384
	 * and output window is larger than a half of the input one. In this
	 * case we have to either reduce the input window to equal or below
	 * 512x384 or the output window to equal or below 1/2 of the input.
	 */
	if (output_w > max(512U, input_w / 2)) {
		if (2 * output_w > RJ54N1_MAX_WIDTH) {
			input_w = RJ54N1_MAX_WIDTH;
			output_w = RJ54N1_MAX_WIDTH / 2;
		} else {
			input_w = output_w * 2;
		}

		dev_dbg(&client->dev, "Adjusted output width: in %u, out %u\n",
			input_w, output_w);
	}

	if (output_h > max(384U, input_h / 2)) {
		if (2 * output_h > RJ54N1_MAX_HEIGHT) {
			input_h = RJ54N1_MAX_HEIGHT;
			output_h = RJ54N1_MAX_HEIGHT / 2;
		} else {
			input_h = output_h * 2;
		}

		dev_dbg(&client->dev, "Adjusted output height: in %u, out %u\n",
			input_h, output_h);
	}

	/* Idea: use the read mode for snapshots, handle separate geometries */
	ret = rj54n1_set_rect(client, RJ54N1_X_OUTPUT_SIZE_S_L,
			      RJ54N1_Y_OUTPUT_SIZE_S_L,
			      RJ54N1_XY_OUTPUT_SIZE_S_H, output_w, output_h);
	if (!ret)
		ret = rj54n1_set_rect(client, RJ54N1_X_OUTPUT_SIZE_P_L,
			      RJ54N1_Y_OUTPUT_SIZE_P_L,
			      RJ54N1_XY_OUTPUT_SIZE_P_H, output_w, output_h);

	if (ret < 0)
		return ret;

	if (output_w > input_w && output_h > input_h) {
		input_w = output_w;
		input_h = output_h;

		resize = 1024;
	} else {
		unsigned int resize_x, resize_y;
		resize_x = (input_w * 1024 + output_w / 2) / output_w;
		resize_y = (input_h * 1024 + output_h / 2) / output_h;

		/* We want max(resize_x, resize_y), check if it still fits */
		if (resize_x > resize_y &&
		    (output_h * resize_x + 512) / 1024 > RJ54N1_MAX_HEIGHT)
			resize = (RJ54N1_MAX_HEIGHT * 1024 + output_h / 2) /
				output_h;
		else if (resize_y > resize_x &&
			 (output_w * resize_y + 512) / 1024 > RJ54N1_MAX_WIDTH)
			resize = (RJ54N1_MAX_WIDTH * 1024 + output_w / 2) /
				output_w;
		else
			resize = max(resize_x, resize_y);

		/* Prohibited value ranges */
		switch (resize) {
		case 2040 ... 2047:
			resize = 2039;
			break;
		case 4080 ... 4095:
			resize = 4079;
			break;
		case 8160 ... 8191:
			resize = 8159;
			break;
		case 16320 ... 16384:
			resize = 16319;
		}
	}

	/* Set scaling */
	ret = reg_write(client, RJ54N1_RESIZE_HOLD_L, resize & 0xff);
	if (!ret)
		ret = reg_write(client, RJ54N1_RESIZE_HOLD_H, resize >> 8);

	if (ret < 0)
		return ret;

	/*
	 * Configure a skipping bitmask. The sensor will select a skipping value
	 * among set bits automatically. This is very unclear in the datasheet
	 * too. I was told, in this register one enables all skipping values,
	 * that are required for a specific resize, and the camera selects
	 * automatically, which ones to use. But it is unclear how to identify,
	 * which cropping values are needed. Secondly, why don't we just set all
	 * bits and let the camera choose? Would it increase processing time and
	 * reduce the framerate? Using 0xfffc for INC_USE_SEL doesn't seem to
	 * improve the image quality or stability for larger frames (see comment
	 * above), but I didn't check the framerate.
	 */
	skip = min(resize / 1024, 15U);

	inc_sel = 1 << skip;

	if (inc_sel <= 2)
		inc_sel = 0xc;
	else if (resize & 1023 && skip < 15)
		inc_sel |= 1 << (skip + 1);

	ret = reg_write(client, RJ54N1_INC_USE_SEL_L, inc_sel & 0xfc);
	if (!ret)
		ret = reg_write(client, RJ54N1_INC_USE_SEL_H, inc_sel >> 8);

	if (!rj54n1->auto_wb) {
		/* Auto white balance window */
		wb_left	  = output_w / 16;
		wb_right  = (3 * output_w / 4 - 3) / 4;
		wb_top	  = output_h / 16;
		wb_bottom = (3 * output_h / 4 - 3) / 4;
		wb_bit8	  = ((wb_left >> 2) & 0x40) | ((wb_top >> 4) & 0x10) |
			((wb_right >> 6) & 4) | ((wb_bottom >> 8) & 1);

		if (!ret)
			ret = reg_write(client, RJ54N1_BIT8_WB, wb_bit8);
		if (!ret)
			ret = reg_write(client, RJ54N1_HCAPS_WB, wb_left);
		if (!ret)
			ret = reg_write(client, RJ54N1_VCAPS_WB, wb_top);
		if (!ret)
			ret = reg_write(client, RJ54N1_HCAPE_WB, wb_right);
		if (!ret)
			ret = reg_write(client, RJ54N1_VCAPE_WB, wb_bottom);
	}

	/* Antiflicker */
	peak = 12 * RJ54N1_MAX_WIDTH * (1 << 14) * resize / rj54n1->tgclk_mhz /
		10000;
	peak_50 = peak / 6;
	peak_60 = peak / 5;

	if (!ret)
		ret = reg_write(client, RJ54N1_PEAK_H,
				((peak_50 >> 4) & 0xf0) | (peak_60 >> 8));
	if (!ret)
		ret = reg_write(client, RJ54N1_PEAK_50, peak_50);
	if (!ret)
		ret = reg_write(client, RJ54N1_PEAK_60, peak_60);
	if (!ret)
		ret = reg_write(client, RJ54N1_PEAK_DIFF, peak / 150);

	/* Start resizing */
	if (!ret)
		ret = reg_write(client, RJ54N1_RESIZE_CONTROL,
				RESIZE_HOLD_SEL | RESIZE_GO | 1);

	if (ret < 0)
		return ret;

	/* Constant taken from manufacturer's example */
	msleep(230);

	ret = reg_write(client, RJ54N1_RESIZE_CONTROL, RESIZE_HOLD_SEL | 1);
	if (ret < 0)
		return ret;

	*in_w = (output_w * resize + 512) / 1024;
	*in_h = (output_h * resize + 512) / 1024;
	*out_w = output_w;
	*out_h = output_h;

	dev_dbg(&client->dev, "Scaled for %dx%d : %u = %ux%u, skip %u\n",
		*in_w, *in_h, resize, output_w, output_h, skip);

	return resize;
}

static int rj54n1_set_clock(struct i2c_client *client)
{
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	int ret;

	/* Enable external clock */
	ret = reg_write(client, RJ54N1_RESET_STANDBY, E_EXCLK | SOFT_STDBY);
	/* Leave stand-by. Note: use this when implementing suspend / resume */
	if (!ret)
		ret = reg_write(client, RJ54N1_RESET_STANDBY, E_EXCLK);

	if (!ret)
		ret = reg_write(client, RJ54N1_PLL_L, PLL_L);
	if (!ret)
		ret = reg_write(client, RJ54N1_PLL_N, PLL_N);

	/* TGCLK dividers */
	if (!ret)
		ret = reg_write(client, RJ54N1_RATIO_TG,
				rj54n1->clk_div.ratio_tg);
	if (!ret)
		ret = reg_write(client, RJ54N1_RATIO_T,
				rj54n1->clk_div.ratio_t);
	if (!ret)
		ret = reg_write(client, RJ54N1_RATIO_R,
				rj54n1->clk_div.ratio_r);

	/* Enable TGCLK & RAMP */
	if (!ret)
		ret = reg_write(client, RJ54N1_RAMP_TGCLK_EN, 3);

	/* Disable clock output */
	if (!ret)
		ret = reg_write(client, RJ54N1_OCLK_DSP, 0);

	/* Set divisors */
	if (!ret)
		ret = reg_write(client, RJ54N1_RATIO_OP,
				rj54n1->clk_div.ratio_op);
	if (!ret)
		ret = reg_write(client, RJ54N1_RATIO_O,
				rj54n1->clk_div.ratio_o);

	/* Enable OCLK */
	if (!ret)
		ret = reg_write(client, RJ54N1_OCLK_SEL_EN, 1);

	/* Use PLL for Timing Generator, write 2 to reserved bits */
	if (!ret)
		ret = reg_write(client, RJ54N1_TG_BYPASS, 2);

	/* Take sensor out of reset */
	if (!ret)
		ret = reg_write(client, RJ54N1_RESET_STANDBY,
				E_EXCLK | SEN_RSTX);
	/* Enable PLL */
	if (!ret)
		ret = reg_write(client, RJ54N1_PLL_EN, 1);

	/* Wait for PLL to stabilise */
	msleep(10);

	/* Enable clock to frequency divider */
	if (!ret)
		ret = reg_write(client, RJ54N1_CLK_RST, 1);

	if (!ret)
		ret = reg_read(client, RJ54N1_CLK_RST);
	if (ret != 1) {
		dev_err(&client->dev,
			"Resetting RJ54N1CB0C clock failed: %d!\n", ret);
		return -EIO;
	}

	/* Start the PLL */
	ret = reg_set(client, RJ54N1_OCLK_DSP, 1, 1);

	/* Enable OCLK */
	if (!ret)
		ret = reg_write(client, RJ54N1_OCLK_SEL_EN, 1);

	return ret;
}

static int rj54n1_reg_init(struct i2c_client *client)
{
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	int ret = rj54n1_set_clock(client);

	if (!ret)
		ret = reg_write_multiple(client, bank_7, ARRAY_SIZE(bank_7));
	if (!ret)
		ret = reg_write_multiple(client, bank_10, ARRAY_SIZE(bank_10));

	/* Set binning divisors */
	if (!ret)
		ret = reg_write(client, RJ54N1_SCALE_1_2_LEV, 3 | (7 << 4));
	if (!ret)
		ret = reg_write(client, RJ54N1_SCALE_4_LEV, 0xf);

	/* Switch to fixed resize mode */
	if (!ret)
		ret = reg_write(client, RJ54N1_RESIZE_CONTROL,
				RESIZE_HOLD_SEL | 1);

	/* Set gain */
	if (!ret)
		ret = reg_write(client, RJ54N1_Y_GAIN, 0x84);

	/*
	 * Mirror the image back: default is upside down and left-to-right...
	 * Set manual preview / still shot switching
	 */
	if (!ret)
		ret = reg_write(client, RJ54N1_MIRROR_STILL_MODE, 0x27);

	if (!ret)
		ret = reg_write_multiple(client, bank_4, ARRAY_SIZE(bank_4));

	/* Auto exposure area */
	if (!ret)
		ret = reg_write(client, RJ54N1_EXPOSURE_CONTROL, 0x80);
	/* Check current auto WB config */
	if (!ret)
		ret = reg_read(client, RJ54N1_WB_SEL_WEIGHT_I);
	if (ret >= 0) {
		rj54n1->auto_wb = ret & 0x80;
		ret = reg_write_multiple(client, bank_5, ARRAY_SIZE(bank_5));
	}
	if (!ret)
		ret = reg_write_multiple(client, bank_8, ARRAY_SIZE(bank_8));

	if (!ret)
		ret = reg_write(client, RJ54N1_RESET_STANDBY,
				E_EXCLK | DSP_RSTX | SEN_RSTX);

	/* Commit init */
	if (!ret)
		ret = rj54n1_commit(client);

	/* Take DSP, TG, sensor out of reset */
	if (!ret)
		ret = reg_write(client, RJ54N1_RESET_STANDBY,
				E_EXCLK | DSP_RSTX | TG_RSTX | SEN_RSTX);

	/* Start register update? Same register as 0x?FE in many bank_* sets */
	if (!ret)
		ret = reg_write(client, RJ54N1_FWFLG, 2);

	/* Constant taken from manufacturer's example */
	msleep(700);

	return ret;
}

static int rj54n1_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	const struct rj54n1_datafmt *fmt;
	int align = mf->code == V4L2_MBUS_FMT_SBGGR10_1X10 ||
		mf->code == V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_BE ||
		mf->code == V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_BE ||
		mf->code == V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE ||
		mf->code == V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_LE;

	dev_dbg(&client->dev, "%s: code = %d, width = %u, height = %u\n",
		__func__, mf->code, mf->width, mf->height);

	fmt = rj54n1_find_datafmt(mf->code, rj54n1_colour_fmts,
				  ARRAY_SIZE(rj54n1_colour_fmts));
	if (!fmt) {
		fmt = rj54n1->fmt;
		mf->code = fmt->code;
	}

	mf->field	= V4L2_FIELD_NONE;
	mf->colorspace	= fmt->colorspace;

	v4l_bound_align_image(&mf->width, 112, RJ54N1_MAX_WIDTH, align,
			      &mf->height, 84, RJ54N1_MAX_HEIGHT, align, 0);

	return 0;
}

static int rj54n1_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	const struct rj54n1_datafmt *fmt;
	int output_w, output_h, max_w, max_h,
		input_w = rj54n1->rect.width, input_h = rj54n1->rect.height;
	int ret;

	/*
	 * The host driver can call us without .try_fmt(), so, we have to take
	 * care ourseleves
	 */
	rj54n1_try_fmt(sd, mf);

	/*
	 * Verify if the sensor has just been powered on. TODO: replace this
	 * with proper PM, when a suitable API is available.
	 */
	ret = reg_read(client, RJ54N1_RESET_STANDBY);
	if (ret < 0)
		return ret;

	if (!(ret & E_EXCLK)) {
		ret = rj54n1_reg_init(client);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&client->dev, "%s: code = %d, width = %u, height = %u\n",
		__func__, mf->code, mf->width, mf->height);

	/* RA_SEL_UL is only relevant for raw modes, ignored otherwise. */
	switch (mf->code) {
	case V4L2_MBUS_FMT_YUYV8_2X8:
		ret = reg_write(client, RJ54N1_OUT_SEL, 0);
		if (!ret)
			ret = reg_set(client, RJ54N1_BYTE_SWAP, 8, 8);
		break;
	case V4L2_MBUS_FMT_YVYU8_2X8:
		ret = reg_write(client, RJ54N1_OUT_SEL, 0);
		if (!ret)
			ret = reg_set(client, RJ54N1_BYTE_SWAP, 0, 8);
		break;
	case V4L2_MBUS_FMT_RGB565_2X8_LE:
		ret = reg_write(client, RJ54N1_OUT_SEL, 0x11);
		if (!ret)
			ret = reg_set(client, RJ54N1_BYTE_SWAP, 8, 8);
		break;
	case V4L2_MBUS_FMT_RGB565_2X8_BE:
		ret = reg_write(client, RJ54N1_OUT_SEL, 0x11);
		if (!ret)
			ret = reg_set(client, RJ54N1_BYTE_SWAP, 0, 8);
		break;
	case V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_LE:
		ret = reg_write(client, RJ54N1_OUT_SEL, 4);
		if (!ret)
			ret = reg_set(client, RJ54N1_BYTE_SWAP, 8, 8);
		if (!ret)
			ret = reg_write(client, RJ54N1_RA_SEL_UL, 0);
		break;
	case V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE:
		ret = reg_write(client, RJ54N1_OUT_SEL, 4);
		if (!ret)
			ret = reg_set(client, RJ54N1_BYTE_SWAP, 8, 8);
		if (!ret)
			ret = reg_write(client, RJ54N1_RA_SEL_UL, 8);
		break;
	case V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_BE:
		ret = reg_write(client, RJ54N1_OUT_SEL, 4);
		if (!ret)
			ret = reg_set(client, RJ54N1_BYTE_SWAP, 0, 8);
		if (!ret)
			ret = reg_write(client, RJ54N1_RA_SEL_UL, 0);
		break;
	case V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_BE:
		ret = reg_write(client, RJ54N1_OUT_SEL, 4);
		if (!ret)
			ret = reg_set(client, RJ54N1_BYTE_SWAP, 0, 8);
		if (!ret)
			ret = reg_write(client, RJ54N1_RA_SEL_UL, 8);
		break;
	case V4L2_MBUS_FMT_SBGGR10_1X10:
		ret = reg_write(client, RJ54N1_OUT_SEL, 5);
		break;
	default:
		ret = -EINVAL;
	}

	/* Special case: a raw mode with 10 bits of data per clock tick */
	if (!ret)
		ret = reg_set(client, RJ54N1_OCLK_SEL_EN,
			      (mf->code == V4L2_MBUS_FMT_SBGGR10_1X10) << 1, 2);

	if (ret < 0)
		return ret;

	/* Supported scales 1:1 >= scale > 1:16 */
	max_w = mf->width * (16 * 1024 - 1) / 1024;
	if (input_w > max_w)
		input_w = max_w;
	max_h = mf->height * (16 * 1024 - 1) / 1024;
	if (input_h > max_h)
		input_h = max_h;

	output_w = mf->width;
	output_h = mf->height;

	ret = rj54n1_sensor_scale(sd, &input_w, &input_h, &output_w, &output_h);
	if (ret < 0)
		return ret;

	fmt = rj54n1_find_datafmt(mf->code, rj54n1_colour_fmts,
				  ARRAY_SIZE(rj54n1_colour_fmts));

	rj54n1->fmt		= fmt;
	rj54n1->resize		= ret;
	rj54n1->rect.width	= input_w;
	rj54n1->rect.height	= input_h;
	rj54n1->width		= output_w;
	rj54n1->height		= output_h;

	mf->width		= output_w;
	mf->height		= output_h;
	mf->field		= V4L2_FIELD_NONE;
	mf->colorspace		= fmt->colorspace;

	return 0;
}

static int rj54n1_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match.addr != client->addr)
		return -ENODEV;

	id->ident	= V4L2_IDENT_RJ54N1CB0C;
	id->revision	= 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int rj54n1_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR ||
	    reg->reg < 0x400 || reg->reg > 0x1fff)
		/* Registers > 0x0800 are only available from Sharp support */
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	reg->size = 1;
	reg->val = reg_read(client, reg->reg);

	if (reg->val > 0xff)
		return -EIO;

	return 0;
}

static int rj54n1_s_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR ||
	    reg->reg < 0x400 || reg->reg > 0x1fff)
		/* Registers >= 0x0800 are only available from Sharp support */
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	if (reg_write(client, reg->reg, reg->val) < 0)
		return -EIO;

	return 0;
}
#endif

static const struct v4l2_queryctrl rj54n1_controls[] = {
	{
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Vertically",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	}, {
		.id		= V4L2_CID_HFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Horizontally",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	}, {
		.id		= V4L2_CID_GAIN,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Gain",
		.minimum	= 0,
		.maximum	= 127,
		.step		= 1,
		.default_value	= 66,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	}, {
		.id		= V4L2_CID_AUTO_WHITE_BALANCE,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Auto white balance",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 1,
	},
};

static struct soc_camera_ops rj54n1_ops = {
	.set_bus_param		= rj54n1_set_bus_param,
	.query_bus_param	= rj54n1_query_bus_param,
	.controls		= rj54n1_controls,
	.num_controls		= ARRAY_SIZE(rj54n1_controls),
};

static int rj54n1_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	int data;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		data = reg_read(client, RJ54N1_MIRROR_STILL_MODE);
		if (data < 0)
			return -EIO;
		ctrl->value = !(data & 1);
		break;
	case V4L2_CID_HFLIP:
		data = reg_read(client, RJ54N1_MIRROR_STILL_MODE);
		if (data < 0)
			return -EIO;
		ctrl->value = !(data & 2);
		break;
	case V4L2_CID_GAIN:
		data = reg_read(client, RJ54N1_Y_GAIN);
		if (data < 0)
			return -EIO;

		ctrl->value = data / 2;
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ctrl->value = rj54n1->auto_wb;
		break;
	}

	return 0;
}

static int rj54n1_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int data;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	const struct v4l2_queryctrl *qctrl;

	qctrl = soc_camera_find_qctrl(&rj54n1_ops, ctrl->id);
	if (!qctrl)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->value)
			data = reg_set(client, RJ54N1_MIRROR_STILL_MODE, 0, 1);
		else
			data = reg_set(client, RJ54N1_MIRROR_STILL_MODE, 1, 1);
		if (data < 0)
			return -EIO;
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->value)
			data = reg_set(client, RJ54N1_MIRROR_STILL_MODE, 0, 2);
		else
			data = reg_set(client, RJ54N1_MIRROR_STILL_MODE, 2, 2);
		if (data < 0)
			return -EIO;
		break;
	case V4L2_CID_GAIN:
		if (ctrl->value > qctrl->maximum ||
		    ctrl->value < qctrl->minimum)
			return -EINVAL;
		else if (reg_write(client, RJ54N1_Y_GAIN, ctrl->value * 2) < 0)
			return -EIO;
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		/* Auto WB area - whole image */
		if (reg_set(client, RJ54N1_WB_SEL_WEIGHT_I, ctrl->value << 7,
			    0x80) < 0)
			return -EIO;
		rj54n1->auto_wb = ctrl->value;
		break;
	}

	return 0;
}

static struct v4l2_subdev_core_ops rj54n1_subdev_core_ops = {
	.g_ctrl		= rj54n1_g_ctrl,
	.s_ctrl		= rj54n1_s_ctrl,
	.g_chip_ident	= rj54n1_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= rj54n1_g_register,
	.s_register	= rj54n1_s_register,
#endif
};

static struct v4l2_subdev_video_ops rj54n1_subdev_video_ops = {
	.s_stream	= rj54n1_s_stream,
	.s_mbus_fmt	= rj54n1_s_fmt,
	.g_mbus_fmt	= rj54n1_g_fmt,
	.try_mbus_fmt	= rj54n1_try_fmt,
	.enum_mbus_fmt	= rj54n1_enum_fmt,
	.g_crop		= rj54n1_g_crop,
	.s_crop		= rj54n1_s_crop,
	.cropcap	= rj54n1_cropcap,
};

static struct v4l2_subdev_ops rj54n1_subdev_ops = {
	.core	= &rj54n1_subdev_core_ops,
	.video	= &rj54n1_subdev_video_ops,
};

/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int rj54n1_video_probe(struct soc_camera_device *icd,
			      struct i2c_client *client,
			      struct rj54n1_pdata *priv)
{
	int data1, data2;
	int ret;

	/* This could be a BUG_ON() or a WARN_ON(), or remove it completely */
	if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	/* Read out the chip version register */
	data1 = reg_read(client, RJ54N1_DEV_CODE);
	data2 = reg_read(client, RJ54N1_DEV_CODE2);

	if (data1 != 0x51 || data2 != 0x10) {
		ret = -ENODEV;
		dev_info(&client->dev, "No RJ54N1CB0C found, read 0x%x:0x%x\n",
			 data1, data2);
		goto ei2c;
	}

	/* Configure IOCTL polarity from the platform data: 0 or 1 << 7. */
	ret = reg_write(client, RJ54N1_IOC, priv->ioctl_high << 7);
	if (ret < 0)
		goto ei2c;

	dev_info(&client->dev, "Detected a RJ54N1CB0C chip ID 0x%x:0x%x\n",
		 data1, data2);

ei2c:
	return ret;
}

static int rj54n1_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct rj54n1 *rj54n1;
	struct soc_camera_device *icd = client->dev.platform_data;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct soc_camera_link *icl;
	struct rj54n1_pdata *rj54n1_priv;
	int ret;

	if (!icd) {
		dev_err(&client->dev, "RJ54N1CB0C: missing soc-camera data!\n");
		return -EINVAL;
	}

	icl = to_soc_camera_link(icd);
	if (!icl || !icl->priv) {
		dev_err(&client->dev, "RJ54N1CB0C: missing platform data!\n");
		return -EINVAL;
	}

	rj54n1_priv = icl->priv;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}

	rj54n1 = kzalloc(sizeof(struct rj54n1), GFP_KERNEL);
	if (!rj54n1)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&rj54n1->subdev, client, &rj54n1_subdev_ops);

	icd->ops		= &rj54n1_ops;

	rj54n1->clk_div		= clk_div;
	rj54n1->rect.left	= RJ54N1_COLUMN_SKIP;
	rj54n1->rect.top	= RJ54N1_ROW_SKIP;
	rj54n1->rect.width	= RJ54N1_MAX_WIDTH;
	rj54n1->rect.height	= RJ54N1_MAX_HEIGHT;
	rj54n1->width		= RJ54N1_MAX_WIDTH;
	rj54n1->height		= RJ54N1_MAX_HEIGHT;
	rj54n1->fmt		= &rj54n1_colour_fmts[0];
	rj54n1->resize		= 1024;
	rj54n1->tgclk_mhz	= (rj54n1_priv->mclk_freq / PLL_L * PLL_N) /
		(clk_div.ratio_tg + 1) / (clk_div.ratio_t + 1);

	ret = rj54n1_video_probe(icd, client, rj54n1_priv);
	if (ret < 0) {
		icd->ops = NULL;
		kfree(rj54n1);
		return ret;
	}

	return ret;
}

static int rj54n1_remove(struct i2c_client *client)
{
	struct rj54n1 *rj54n1 = to_rj54n1(client);
	struct soc_camera_device *icd = client->dev.platform_data;
	struct soc_camera_link *icl = to_soc_camera_link(icd);

	icd->ops = NULL;
	if (icl->free_bus)
		icl->free_bus(icl);
	kfree(rj54n1);

	return 0;
}

static const struct i2c_device_id rj54n1_id[] = {
	{ "rj54n1cb0c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rj54n1_id);

static struct i2c_driver rj54n1_i2c_driver = {
	.driver = {
		.name = "rj54n1cb0c",
	},
	.probe		= rj54n1_probe,
	.remove		= rj54n1_remove,
	.id_table	= rj54n1_id,
};

static int __init rj54n1_mod_init(void)
{
	return i2c_add_driver(&rj54n1_i2c_driver);
}

static void __exit rj54n1_mod_exit(void)
{
	i2c_del_driver(&rj54n1_i2c_driver);
}

module_init(rj54n1_mod_init);
module_exit(rj54n1_mod_exit);

MODULE_DESCRIPTION("Sharp RJ54N1CB0C Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");
