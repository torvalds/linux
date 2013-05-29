/*
 * Driver for IMX074 CMOS Image Sensor from Sony
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * Partially inspired by the IMX074 driver from the Android / MSM tree
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/v4l2-mediabus.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/module.h>

#include <media/soc_camera.h>
#include <media/v4l2-subdev.h>

/* IMX074 registers */

#define MODE_SELECT			0x0100
#define IMAGE_ORIENTATION		0x0101
#define GROUPED_PARAMETER_HOLD		0x0104

/* Integration Time */
#define COARSE_INTEGRATION_TIME_HI	0x0202
#define COARSE_INTEGRATION_TIME_LO	0x0203
/* Gain */
#define ANALOGUE_GAIN_CODE_GLOBAL_HI	0x0204
#define ANALOGUE_GAIN_CODE_GLOBAL_LO	0x0205

/* PLL registers */
#define PRE_PLL_CLK_DIV			0x0305
#define PLL_MULTIPLIER			0x0307
#define PLSTATIM			0x302b
#define VNDMY_ABLMGSHLMT		0x300a
#define Y_OPBADDR_START_DI		0x3014
/* mode setting */
#define FRAME_LENGTH_LINES_HI		0x0340
#define FRAME_LENGTH_LINES_LO		0x0341
#define LINE_LENGTH_PCK_HI		0x0342
#define LINE_LENGTH_PCK_LO		0x0343
#define YADDR_START			0x0347
#define YADDR_END			0x034b
#define X_OUTPUT_SIZE_MSB		0x034c
#define X_OUTPUT_SIZE_LSB		0x034d
#define Y_OUTPUT_SIZE_MSB		0x034e
#define Y_OUTPUT_SIZE_LSB		0x034f
#define X_EVEN_INC			0x0381
#define X_ODD_INC			0x0383
#define Y_EVEN_INC			0x0385
#define Y_ODD_INC			0x0387

#define HMODEADD			0x3001
#define VMODEADD			0x3016
#define VAPPLINE_START			0x3069
#define VAPPLINE_END			0x306b
#define SHUTTER				0x3086
#define HADDAVE				0x30e8
#define LANESEL				0x3301

/* IMX074 supported geometry */
#define IMX074_WIDTH			1052
#define IMX074_HEIGHT			780

/* IMX074 has only one fixed colorspace per pixelcode */
struct imx074_datafmt {
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_colorspace		colorspace;
};

struct imx074 {
	struct v4l2_subdev		subdev;
	const struct imx074_datafmt	*fmt;
};

static const struct imx074_datafmt imx074_colour_fmts[] = {
	{V4L2_MBUS_FMT_SBGGR8_1X8, V4L2_COLORSPACE_SRGB},
};

static struct imx074 *to_imx074(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct imx074, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct imx074_datafmt *imx074_find_datafmt(enum v4l2_mbus_pixelcode code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(imx074_colour_fmts); i++)
		if (imx074_colour_fmts[i].code == code)
			return imx074_colour_fmts + i;

	return NULL;
}

static int reg_write(struct i2c_client *client, const u16 addr, const u8 data)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	unsigned char tx[3];
	int ret;

	msg.addr = client->addr;
	msg.buf = tx;
	msg.len = 3;
	msg.flags = 0;

	tx[0] = addr >> 8;
	tx[1] = addr & 0xff;
	tx[2] = data;

	ret = i2c_transfer(adap, &msg, 1);

	mdelay(2);

	return ret == 1 ? 0 : -EIO;
}

static int reg_read(struct i2c_client *client, const u16 addr)
{
	u8 buf[2] = {addr >> 8, addr & 0xff};
	int ret;
	struct i2c_msg msgs[] = {
		{
			.addr  = client->addr,
			.flags = 0,
			.len   = 2,
			.buf   = buf,
		}, {
			.addr  = client->addr,
			.flags = I2C_M_RD,
			.len   = 2,
			.buf   = buf,
		},
	};

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0) {
		dev_warn(&client->dev, "Reading register %x from %x failed\n",
			 addr, client->addr);
		return ret;
	}

	return buf[0] & 0xff; /* no sign-extension */
}

static int imx074_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	const struct imx074_datafmt *fmt = imx074_find_datafmt(mf->code);

	dev_dbg(sd->v4l2_dev->dev, "%s(%u)\n", __func__, mf->code);

	if (!fmt) {
		mf->code	= imx074_colour_fmts[0].code;
		mf->colorspace	= imx074_colour_fmts[0].colorspace;
	}

	mf->width	= IMX074_WIDTH;
	mf->height	= IMX074_HEIGHT;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int imx074_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx074 *priv = to_imx074(client);

	dev_dbg(sd->v4l2_dev->dev, "%s(%u)\n", __func__, mf->code);

	/* MIPI CSI could have changed the format, double-check */
	if (!imx074_find_datafmt(mf->code))
		return -EINVAL;

	imx074_try_fmt(sd, mf);

	priv->fmt = imx074_find_datafmt(mf->code);

	return 0;
}

static int imx074_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx074 *priv = to_imx074(client);

	const struct imx074_datafmt *fmt = priv->fmt;

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->width	= IMX074_WIDTH;
	mf->height	= IMX074_HEIGHT;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int imx074_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct v4l2_rect *rect = &a->c;

	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rect->top	= 0;
	rect->left	= 0;
	rect->width	= IMX074_WIDTH;
	rect->height	= IMX074_HEIGHT;

	return 0;
}

static int imx074_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= IMX074_WIDTH;
	a->bounds.height		= IMX074_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int imx074_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if ((unsigned int)index >= ARRAY_SIZE(imx074_colour_fmts))
		return -EINVAL;

	*code = imx074_colour_fmts[index].code;
	return 0;
}

static int imx074_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/* MODE_SELECT: stream or standby */
	return reg_write(client, MODE_SELECT, !!enable);
}

static int imx074_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	return soc_camera_set_power(&client->dev, ssdd, on);
}

static int imx074_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
	cfg->flags = V4L2_MBUS_CSI2_2_LANE |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	return 0;
}

static struct v4l2_subdev_video_ops imx074_subdev_video_ops = {
	.s_stream	= imx074_s_stream,
	.s_mbus_fmt	= imx074_s_fmt,
	.g_mbus_fmt	= imx074_g_fmt,
	.try_mbus_fmt	= imx074_try_fmt,
	.enum_mbus_fmt	= imx074_enum_fmt,
	.g_crop		= imx074_g_crop,
	.cropcap	= imx074_cropcap,
	.g_mbus_config	= imx074_g_mbus_config,
};

static struct v4l2_subdev_core_ops imx074_subdev_core_ops = {
	.s_power	= imx074_s_power,
};

static struct v4l2_subdev_ops imx074_subdev_ops = {
	.core	= &imx074_subdev_core_ops,
	.video	= &imx074_subdev_video_ops,
};

static int imx074_video_probe(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	int ret;
	u16 id;

	ret = imx074_s_power(subdev, 1);
	if (ret < 0)
		return ret;

	/* Read sensor Model ID */
	ret = reg_read(client, 0);
	if (ret < 0)
		goto done;

	id = ret << 8;

	ret = reg_read(client, 1);
	if (ret < 0)
		goto done;

	id |= ret;

	dev_info(&client->dev, "Chip ID 0x%04x detected\n", id);

	if (id != 0x74) {
		ret = -ENODEV;
		goto done;
	}

	/* PLL Setting EXTCLK=24MHz, 22.5times */
	reg_write(client, PLL_MULTIPLIER, 0x2D);
	reg_write(client, PRE_PLL_CLK_DIV, 0x02);
	reg_write(client, PLSTATIM, 0x4B);

	/* 2-lane mode */
	reg_write(client, 0x3024, 0x00);

	reg_write(client, IMAGE_ORIENTATION, 0x00);

	/* select RAW mode:
	 * 0x08+0x08 = top 8 bits
	 * 0x0a+0x08 = compressed 8-bits
	 * 0x0a+0x0a = 10 bits
	 */
	reg_write(client, 0x0112, 0x08);
	reg_write(client, 0x0113, 0x08);

	/* Base setting for High frame mode */
	reg_write(client, VNDMY_ABLMGSHLMT, 0x80);
	reg_write(client, Y_OPBADDR_START_DI, 0x08);
	reg_write(client, 0x3015, 0x37);
	reg_write(client, 0x301C, 0x01);
	reg_write(client, 0x302C, 0x05);
	reg_write(client, 0x3031, 0x26);
	reg_write(client, 0x3041, 0x60);
	reg_write(client, 0x3051, 0x24);
	reg_write(client, 0x3053, 0x34);
	reg_write(client, 0x3057, 0xC0);
	reg_write(client, 0x305C, 0x09);
	reg_write(client, 0x305D, 0x07);
	reg_write(client, 0x3060, 0x30);
	reg_write(client, 0x3065, 0x00);
	reg_write(client, 0x30AA, 0x08);
	reg_write(client, 0x30AB, 0x1C);
	reg_write(client, 0x30B0, 0x32);
	reg_write(client, 0x30B2, 0x83);
	reg_write(client, 0x30D3, 0x04);
	reg_write(client, 0x3106, 0x78);
	reg_write(client, 0x310C, 0x82);
	reg_write(client, 0x3304, 0x05);
	reg_write(client, 0x3305, 0x04);
	reg_write(client, 0x3306, 0x11);
	reg_write(client, 0x3307, 0x02);
	reg_write(client, 0x3308, 0x0C);
	reg_write(client, 0x3309, 0x06);
	reg_write(client, 0x330A, 0x08);
	reg_write(client, 0x330B, 0x04);
	reg_write(client, 0x330C, 0x08);
	reg_write(client, 0x330D, 0x06);
	reg_write(client, 0x330E, 0x01);
	reg_write(client, 0x3381, 0x00);

	/* V : 1/2V-addition (1,3), H : 1/2H-averaging (1,3) -> Full HD */
	/* 1608 = 1560 + 48 (black lines) */
	reg_write(client, FRAME_LENGTH_LINES_HI, 0x06);
	reg_write(client, FRAME_LENGTH_LINES_LO, 0x48);
	reg_write(client, YADDR_START, 0x00);
	reg_write(client, YADDR_END, 0x2F);
	/* 0x838 == 2104 */
	reg_write(client, X_OUTPUT_SIZE_MSB, 0x08);
	reg_write(client, X_OUTPUT_SIZE_LSB, 0x38);
	/* 0x618 == 1560 */
	reg_write(client, Y_OUTPUT_SIZE_MSB, 0x06);
	reg_write(client, Y_OUTPUT_SIZE_LSB, 0x18);
	reg_write(client, X_EVEN_INC, 0x01);
	reg_write(client, X_ODD_INC, 0x03);
	reg_write(client, Y_EVEN_INC, 0x01);
	reg_write(client, Y_ODD_INC, 0x03);
	reg_write(client, HMODEADD, 0x00);
	reg_write(client, VMODEADD, 0x16);
	reg_write(client, VAPPLINE_START, 0x24);
	reg_write(client, VAPPLINE_END, 0x53);
	reg_write(client, SHUTTER, 0x00);
	reg_write(client, HADDAVE, 0x80);

	reg_write(client, LANESEL, 0x00);

	reg_write(client, GROUPED_PARAMETER_HOLD, 0x00);	/* off */

	ret = 0;

done:
	imx074_s_power(subdev, 0);
	return ret;
}

static int imx074_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct imx074 *priv;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	if (!ssdd) {
		dev_err(&client->dev, "IMX074: missing platform data!\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct imx074), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->subdev, client, &imx074_subdev_ops);

	priv->fmt	= &imx074_colour_fmts[0];

	return imx074_video_probe(client);
}

static int imx074_remove(struct i2c_client *client)
{
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	if (ssdd->free_bus)
		ssdd->free_bus(ssdd);

	return 0;
}

static const struct i2c_device_id imx074_id[] = {
	{ "imx074", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, imx074_id);

static struct i2c_driver imx074_i2c_driver = {
	.driver = {
		.name = "imx074",
	},
	.probe		= imx074_probe,
	.remove		= imx074_remove,
	.id_table	= imx074_id,
};

module_i2c_driver(imx074_i2c_driver);

MODULE_DESCRIPTION("Sony IMX074 Camera driver");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");
