/*
 * OmniVision OV96xx Camera Driver
 *
 * Copyright (C) 2009 Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on ov772x camera driver:
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on ov7670 and soc_camera_platform driver,
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-common.h>
#include <media/soc_camera.h>

#include "ov9640.h"

/* default register setup */
static const struct ov9640_reg ov9640_regs_dflt[] = {
	{ OV9640_COM5,	OV9640_COM5_SYSCLK | OV9640_COM5_LONGEXP },
	{ OV9640_COM6,	OV9640_COM6_OPT_BLC | OV9640_COM6_ADBLC_BIAS |
			OV9640_COM6_FMT_RST | OV9640_COM6_ADBLC_OPTEN },
	{ OV9640_PSHFT,	OV9640_PSHFT_VAL(0x01) },
	{ OV9640_ACOM,	OV9640_ACOM_2X_ANALOG | OV9640_ACOM_RSVD },
	{ OV9640_TSLB,	OV9640_TSLB_YUYV_UYVY },
	{ OV9640_COM16,	OV9640_COM16_RB_AVG },

	/* Gamma curve P */
	{ 0x6c, 0x40 },	{ 0x6d, 0x30 },	{ 0x6e, 0x4b },	{ 0x6f, 0x60 },
	{ 0x70, 0x70 },	{ 0x71, 0x70 },	{ 0x72, 0x70 },	{ 0x73, 0x70 },
	{ 0x74, 0x60 },	{ 0x75, 0x60 },	{ 0x76, 0x50 },	{ 0x77, 0x48 },
	{ 0x78, 0x3a },	{ 0x79, 0x2e },	{ 0x7a, 0x28 },	{ 0x7b, 0x22 },

	/* Gamma curve T */
	{ 0x7c, 0x04 },	{ 0x7d, 0x07 },	{ 0x7e, 0x10 },	{ 0x7f, 0x28 },
	{ 0x80, 0x36 },	{ 0x81, 0x44 },	{ 0x82, 0x52 },	{ 0x83, 0x60 },
	{ 0x84, 0x6c },	{ 0x85, 0x78 },	{ 0x86, 0x8c },	{ 0x87, 0x9e },
	{ 0x88, 0xbb },	{ 0x89, 0xd2 },	{ 0x8a, 0xe6 },
};

/* Configurations
 * NOTE: for YUV, alter the following registers:
 * 		COM12 |= OV9640_COM12_YUV_AVG
 *
 *	 for RGB, alter the following registers:
 *		COM7  |= OV9640_COM7_RGB
 *		COM13 |= OV9640_COM13_RGB_AVG
 *		COM15 |= proper RGB color encoding mode
 */
static const struct ov9640_reg ov9640_regs_qqcif[] = {
	{ OV9640_CLKRC,	OV9640_CLKRC_DPLL_EN | OV9640_CLKRC_DIV(0x0f) },
	{ OV9640_COM1,	OV9640_COM1_QQFMT | OV9640_COM1_HREF_2SKIP },
	{ OV9640_COM4,	OV9640_COM4_QQ_VP | OV9640_COM4_RSVD },
	{ OV9640_COM7,	OV9640_COM7_QCIF },
	{ OV9640_COM12,	OV9640_COM12_RSVD },
	{ OV9640_COM13,	OV9640_COM13_GAMMA_RAW | OV9640_COM13_MATRIX_EN },
	{ OV9640_COM15,	OV9640_COM15_OR_10F0 },
};

static const struct ov9640_reg ov9640_regs_qqvga[] = {
	{ OV9640_CLKRC,	OV9640_CLKRC_DPLL_EN | OV9640_CLKRC_DIV(0x07) },
	{ OV9640_COM1,	OV9640_COM1_QQFMT | OV9640_COM1_HREF_2SKIP },
	{ OV9640_COM4,	OV9640_COM4_QQ_VP | OV9640_COM4_RSVD },
	{ OV9640_COM7,	OV9640_COM7_QVGA },
	{ OV9640_COM12,	OV9640_COM12_RSVD },
	{ OV9640_COM13,	OV9640_COM13_GAMMA_RAW | OV9640_COM13_MATRIX_EN },
	{ OV9640_COM15,	OV9640_COM15_OR_10F0 },
};

static const struct ov9640_reg ov9640_regs_qcif[] = {
	{ OV9640_CLKRC,	OV9640_CLKRC_DPLL_EN | OV9640_CLKRC_DIV(0x07) },
	{ OV9640_COM4,	OV9640_COM4_QQ_VP | OV9640_COM4_RSVD },
	{ OV9640_COM7,	OV9640_COM7_QCIF },
	{ OV9640_COM12,	OV9640_COM12_RSVD },
	{ OV9640_COM13,	OV9640_COM13_GAMMA_RAW | OV9640_COM13_MATRIX_EN },
	{ OV9640_COM15,	OV9640_COM15_OR_10F0 },
};

static const struct ov9640_reg ov9640_regs_qvga[] = {
	{ OV9640_CLKRC,	OV9640_CLKRC_DPLL_EN | OV9640_CLKRC_DIV(0x03) },
	{ OV9640_COM4,	OV9640_COM4_QQ_VP | OV9640_COM4_RSVD },
	{ OV9640_COM7,	OV9640_COM7_QVGA },
	{ OV9640_COM12,	OV9640_COM12_RSVD },
	{ OV9640_COM13,	OV9640_COM13_GAMMA_RAW | OV9640_COM13_MATRIX_EN },
	{ OV9640_COM15,	OV9640_COM15_OR_10F0 },
};

static const struct ov9640_reg ov9640_regs_cif[] = {
	{ OV9640_CLKRC,	OV9640_CLKRC_DPLL_EN | OV9640_CLKRC_DIV(0x03) },
	{ OV9640_COM3,	OV9640_COM3_VP },
	{ OV9640_COM7,	OV9640_COM7_CIF },
	{ OV9640_COM12,	OV9640_COM12_RSVD },
	{ OV9640_COM13,	OV9640_COM13_GAMMA_RAW | OV9640_COM13_MATRIX_EN },
	{ OV9640_COM15,	OV9640_COM15_OR_10F0 },
};

static const struct ov9640_reg ov9640_regs_vga[] = {
	{ OV9640_CLKRC,	OV9640_CLKRC_DPLL_EN | OV9640_CLKRC_DIV(0x01) },
	{ OV9640_COM3,	OV9640_COM3_VP },
	{ OV9640_COM7,	OV9640_COM7_VGA },
	{ OV9640_COM12,	OV9640_COM12_RSVD },
	{ OV9640_COM13,	OV9640_COM13_GAMMA_RAW | OV9640_COM13_MATRIX_EN },
	{ OV9640_COM15,	OV9640_COM15_OR_10F0 },
};

static const struct ov9640_reg ov9640_regs_sxga[] = {
	{ OV9640_CLKRC,	OV9640_CLKRC_DPLL_EN | OV9640_CLKRC_DIV(0x01) },
	{ OV9640_COM3,	OV9640_COM3_VP },
	{ OV9640_COM7,	0 },
	{ OV9640_COM12,	OV9640_COM12_RSVD },
	{ OV9640_COM13,	OV9640_COM13_GAMMA_RAW | OV9640_COM13_MATRIX_EN },
	{ OV9640_COM15,	OV9640_COM15_OR_10F0 },
};

static const struct ov9640_reg ov9640_regs_yuv[] = {
	{ OV9640_MTX1,	0x58 },
	{ OV9640_MTX2,	0x48 },
	{ OV9640_MTX3,	0x10 },
	{ OV9640_MTX4,	0x28 },
	{ OV9640_MTX5,	0x48 },
	{ OV9640_MTX6,	0x70 },
	{ OV9640_MTX7,	0x40 },
	{ OV9640_MTX8,	0x40 },
	{ OV9640_MTX9,	0x40 },
	{ OV9640_MTXS,	0x0f },
};

static const struct ov9640_reg ov9640_regs_rgb[] = {
	{ OV9640_MTX1,	0x71 },
	{ OV9640_MTX2,	0x3e },
	{ OV9640_MTX3,	0x0c },
	{ OV9640_MTX4,	0x33 },
	{ OV9640_MTX5,	0x72 },
	{ OV9640_MTX6,	0x00 },
	{ OV9640_MTX7,	0x2b },
	{ OV9640_MTX8,	0x66 },
	{ OV9640_MTX9,	0xd2 },
	{ OV9640_MTXS,	0x65 },
};

static enum v4l2_mbus_pixelcode ov9640_codes[] = {
	V4L2_MBUS_FMT_UYVY8_2X8,
	V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE,
	V4L2_MBUS_FMT_RGB565_2X8_LE,
};

static const struct v4l2_queryctrl ov9640_controls[] = {
	{
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Vertically",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	},
	{
		.id		= V4L2_CID_HFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Flip Horizontally",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	},
};

/* read a register */
static int ov9640_reg_read(struct i2c_client *client, u8 reg, u8 *val)
{
	int ret;
	u8 data = reg;
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 1,
		.buf	= &data,
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		goto err;

	msg.flags = I2C_M_RD;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		goto err;

	*val = data;
	return 0;

err:
	dev_err(&client->dev, "Failed reading register 0x%02x!\n", reg);
	return ret;
}

/* write a register */
static int ov9640_reg_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;
	u8 _val;
	unsigned char data[2] = { reg, val };
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 2,
		.buf	= data,
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Failed writing register 0x%02x!\n", reg);
		return ret;
	}

	/* we have to read the register back ... no idea why, maybe HW bug */
	ret = ov9640_reg_read(client, reg, &_val);
	if (ret)
		dev_err(&client->dev,
			"Failed reading back register 0x%02x!\n", reg);

	return 0;
}


/* Read a register, alter its bits, write it back */
static int ov9640_reg_rmw(struct i2c_client *client, u8 reg, u8 set, u8 unset)
{
	u8 val;
	int ret;

	ret = ov9640_reg_read(client, reg, &val);
	if (ret) {
		dev_err(&client->dev,
			"[Read]-Modify-Write of register %02x failed!\n", reg);
		return val;
	}

	val |= set;
	val &= ~unset;

	ret = ov9640_reg_write(client, reg, val);
	if (ret)
		dev_err(&client->dev,
			"Read-Modify-[Write] of register %02x failed!\n", reg);

	return ret;
}

/* Soft reset the camera. This has nothing to do with the RESET pin! */
static int ov9640_reset(struct i2c_client *client)
{
	int ret;

	ret = ov9640_reg_write(client, OV9640_COM7, OV9640_COM7_SCCB_RESET);
	if (ret)
		dev_err(&client->dev,
			"An error occured while entering soft reset!\n");

	return ret;
}

/* Start/Stop streaming from the device */
static int ov9640_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

/* Alter bus settings on camera side */
static int ov9640_set_bus_param(struct soc_camera_device *icd,
				unsigned long flags)
{
	return 0;
}

/* Request bus settings on camera side */
static unsigned long ov9640_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);

	/*
	 * REVISIT: the camera probably can do 10 bit transfers, but I don't
	 *          have those pins connected on my hardware.
	 */
	unsigned long flags = SOCAM_PCLK_SAMPLE_RISING | SOCAM_MASTER |
		SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_HSYNC_ACTIVE_HIGH |
		SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8;

	return soc_camera_apply_sensor_flags(icl, flags);
}

/* Get status of additional camera capabilities */
static int ov9640_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov9640_priv *priv = container_of(i2c_get_clientdata(client),
					struct ov9640_priv, subdev);

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		ctrl->value = priv->flag_vflip;
		break;
	case V4L2_CID_HFLIP:
		ctrl->value = priv->flag_hflip;
		break;
	}
	return 0;
}

/* Set status of additional camera capabilities */
static int ov9640_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov9640_priv *priv = container_of(i2c_get_clientdata(client),
					struct ov9640_priv, subdev);

	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		priv->flag_vflip = ctrl->value;
		if (ctrl->value)
			ret = ov9640_reg_rmw(client, OV9640_MVFP,
							OV9640_MVFP_V, 0);
		else
			ret = ov9640_reg_rmw(client, OV9640_MVFP,
							0, OV9640_MVFP_V);
		break;
	case V4L2_CID_HFLIP:
		priv->flag_hflip = ctrl->value;
		if (ctrl->value)
			ret = ov9640_reg_rmw(client, OV9640_MVFP,
							OV9640_MVFP_H, 0);
		else
			ret = ov9640_reg_rmw(client, OV9640_MVFP,
							0, OV9640_MVFP_H);
		break;
	}

	return ret;
}

/* Get chip identification */
static int ov9640_g_chip_ident(struct v4l2_subdev *sd,
				struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov9640_priv *priv = container_of(i2c_get_clientdata(client),
					struct ov9640_priv, subdev);

	id->ident	= priv->model;
	id->revision	= priv->revision;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov9640_get_register(struct v4l2_subdev *sd,
				struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 val;

	if (reg->reg & ~0xff)
		return -EINVAL;

	reg->size = 1;

	ret = ov9640_reg_read(client, reg->reg, &val);
	if (ret)
		return ret;

	reg->val = (__u64)val;

	return 0;
}

static int ov9640_set_register(struct v4l2_subdev *sd,
				struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg & ~0xff || reg->val & ~0xff)
		return -EINVAL;

	return ov9640_reg_write(client, reg->reg, reg->val);
}
#endif

/* select nearest higher resolution for capture */
static void ov9640_res_roundup(u32 *width, u32 *height)
{
	int i;
	enum { QQCIF, QQVGA, QCIF, QVGA, CIF, VGA, SXGA };
	int res_x[] = { 88, 160, 176, 320, 352, 640, 1280 };
	int res_y[] = { 72, 120, 144, 240, 288, 480, 960 };

	for (i = 0; i < ARRAY_SIZE(res_x); i++) {
		if (res_x[i] >= *width && res_y[i] >= *height) {
			*width = res_x[i];
			*height = res_y[i];
			return;
		}
	}

	*width = res_x[SXGA];
	*height = res_y[SXGA];
}

/* Prepare necessary register changes depending on color encoding */
static void ov9640_alter_regs(enum v4l2_mbus_pixelcode code,
			      struct ov9640_reg_alt *alt)
{
	switch (code) {
	default:
	case V4L2_MBUS_FMT_UYVY8_2X8:
		alt->com12	= OV9640_COM12_YUV_AVG;
		alt->com13	= OV9640_COM13_Y_DELAY_EN |
					OV9640_COM13_YUV_DLY(0x01);
		break;
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE:
		alt->com7	= OV9640_COM7_RGB;
		alt->com13	= OV9640_COM13_RGB_AVG;
		alt->com15	= OV9640_COM15_RGB_555;
		break;
	case V4L2_MBUS_FMT_RGB565_2X8_LE:
		alt->com7	= OV9640_COM7_RGB;
		alt->com13	= OV9640_COM13_RGB_AVG;
		alt->com15	= OV9640_COM15_RGB_565;
		break;
	};
}

/* Setup registers according to resolution and color encoding */
static int ov9640_write_regs(struct i2c_client *client, u32 width,
		enum v4l2_mbus_pixelcode code, struct ov9640_reg_alt *alts)
{
	const struct ov9640_reg	*ov9640_regs, *matrix_regs;
	int			ov9640_regs_len, matrix_regs_len;
	int			i, ret;
	u8			val;

	/* select register configuration for given resolution */
	switch (width) {
	case W_QQCIF:
		ov9640_regs	= ov9640_regs_qqcif;
		ov9640_regs_len	= ARRAY_SIZE(ov9640_regs_qqcif);
		break;
	case W_QQVGA:
		ov9640_regs	= ov9640_regs_qqvga;
		ov9640_regs_len	= ARRAY_SIZE(ov9640_regs_qqvga);
		break;
	case W_QCIF:
		ov9640_regs	= ov9640_regs_qcif;
		ov9640_regs_len	= ARRAY_SIZE(ov9640_regs_qcif);
		break;
	case W_QVGA:
		ov9640_regs	= ov9640_regs_qvga;
		ov9640_regs_len	= ARRAY_SIZE(ov9640_regs_qvga);
		break;
	case W_CIF:
		ov9640_regs	= ov9640_regs_cif;
		ov9640_regs_len	= ARRAY_SIZE(ov9640_regs_cif);
		break;
	case W_VGA:
		ov9640_regs	= ov9640_regs_vga;
		ov9640_regs_len	= ARRAY_SIZE(ov9640_regs_vga);
		break;
	case W_SXGA:
		ov9640_regs	= ov9640_regs_sxga;
		ov9640_regs_len	= ARRAY_SIZE(ov9640_regs_sxga);
		break;
	default:
		dev_err(&client->dev, "Failed to select resolution!\n");
		return -EINVAL;
	}

	/* select color matrix configuration for given color encoding */
	if (code == V4L2_MBUS_FMT_UYVY8_2X8) {
		matrix_regs	= ov9640_regs_yuv;
		matrix_regs_len	= ARRAY_SIZE(ov9640_regs_yuv);
	} else {
		matrix_regs	= ov9640_regs_rgb;
		matrix_regs_len	= ARRAY_SIZE(ov9640_regs_rgb);
	}

	/* write register settings into the module */
	for (i = 0; i < ov9640_regs_len; i++) {
		val = ov9640_regs[i].val;

		switch (ov9640_regs[i].reg) {
		case OV9640_COM7:
			val |= alts->com7;
			break;
		case OV9640_COM12:
			val |= alts->com12;
			break;
		case OV9640_COM13:
			val |= alts->com13;
			break;
		case OV9640_COM15:
			val |= alts->com15;
			break;
		}

		ret = ov9640_reg_write(client, ov9640_regs[i].reg, val);
		if (ret)
			return ret;
	}

	/* write color matrix configuration into the module */
	for (i = 0; i < matrix_regs_len; i++) {
		ret = ov9640_reg_write(client, matrix_regs[i].reg,
						matrix_regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

/* program default register values */
static int ov9640_prog_dflt(struct i2c_client *client)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ov9640_regs_dflt); i++) {
		ret = ov9640_reg_write(client, ov9640_regs_dflt[i].reg,
						ov9640_regs_dflt[i].val);
		if (ret)
			return ret;
	}

	/* wait for the changes to actually happen, 140ms are not enough yet */
	mdelay(150);

	return 0;
}

/* set the format we will capture in */
static int ov9640_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov9640_reg_alt alts = {0};
	enum v4l2_colorspace cspace;
	enum v4l2_mbus_pixelcode code = mf->code;
	int ret;

	ov9640_res_roundup(&mf->width, &mf->height);
	ov9640_alter_regs(mf->code, &alts);

	ov9640_reset(client);

	ret = ov9640_prog_dflt(client);
	if (ret)
		return ret;

	switch (code) {
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE:
	case V4L2_MBUS_FMT_RGB565_2X8_LE:
		cspace = V4L2_COLORSPACE_SRGB;
		break;
	default:
		code = V4L2_MBUS_FMT_UYVY8_2X8;
	case V4L2_MBUS_FMT_UYVY8_2X8:
		cspace = V4L2_COLORSPACE_JPEG;
	}

	ret = ov9640_write_regs(client, mf->width, code, &alts);
	if (!ret) {
		mf->code	= code;
		mf->colorspace	= cspace;
	}

	return ret;
}

static int ov9640_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	ov9640_res_roundup(&mf->width, &mf->height);

	mf->field = V4L2_FIELD_NONE;

	switch (mf->code) {
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE:
	case V4L2_MBUS_FMT_RGB565_2X8_LE:
		mf->colorspace = V4L2_COLORSPACE_SRGB;
		break;
	default:
		mf->code = V4L2_MBUS_FMT_UYVY8_2X8;
	case V4L2_MBUS_FMT_UYVY8_2X8:
		mf->colorspace = V4L2_COLORSPACE_JPEG;
	}

	return 0;
}

static int ov9640_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(ov9640_codes))
		return -EINVAL;

	*code = ov9640_codes[index];
	return 0;
}

static int ov9640_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	a->c.left	= 0;
	a->c.top	= 0;
	a->c.width	= W_SXGA;
	a->c.height	= H_SXGA;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int ov9640_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= W_SXGA;
	a->bounds.height		= H_SXGA;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}



static int ov9640_video_probe(struct soc_camera_device *icd,
				struct i2c_client *client)
{
	struct ov9640_priv *priv = i2c_get_clientdata(client);
	u8		pid, ver, midh, midl;
	const char	*devname;
	int		ret = 0;

	/*
	 * We must have a parent by now. And it cannot be a wrong one.
	 * So this entire test is completely redundant.
	 */
	if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface) {
		dev_err(&client->dev, "Parent missing or invalid!\n");
		ret = -ENODEV;
		goto err;
	}

	/*
	 * check and show product ID and manufacturer ID
	 */

	ret = ov9640_reg_read(client, OV9640_PID, &pid);
	if (ret)
		goto err;

	ret = ov9640_reg_read(client, OV9640_VER, &ver);
	if (ret)
		goto err;

	ret = ov9640_reg_read(client, OV9640_MIDH, &midh);
	if (ret)
		goto err;

	ret = ov9640_reg_read(client, OV9640_MIDL, &midl);
	if (ret)
		goto err;

	switch (VERSION(pid, ver)) {
	case OV9640_V2:
		devname		= "ov9640";
		priv->model	= V4L2_IDENT_OV9640;
		priv->revision	= 2;
	case OV9640_V3:
		devname		= "ov9640";
		priv->model	= V4L2_IDENT_OV9640;
		priv->revision	= 3;
		break;
	default:
		dev_err(&client->dev, "Product ID error %x:%x\n", pid, ver);
		ret = -ENODEV;
		goto err;
	}

	dev_info(&client->dev, "%s Product ID %0x:%0x Manufacturer ID %x:%x\n",
		 devname, pid, ver, midh, midl);

err:
	return ret;
}

static struct soc_camera_ops ov9640_ops = {
	.set_bus_param		= ov9640_set_bus_param,
	.query_bus_param	= ov9640_query_bus_param,
	.controls		= ov9640_controls,
	.num_controls		= ARRAY_SIZE(ov9640_controls),
};

static struct v4l2_subdev_core_ops ov9640_core_ops = {
	.g_ctrl			= ov9640_g_ctrl,
	.s_ctrl			= ov9640_s_ctrl,
	.g_chip_ident		= ov9640_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register		= ov9640_get_register,
	.s_register		= ov9640_set_register,
#endif

};

static struct v4l2_subdev_video_ops ov9640_video_ops = {
	.s_stream	= ov9640_s_stream,
	.s_mbus_fmt	= ov9640_s_fmt,
	.try_mbus_fmt	= ov9640_try_fmt,
	.enum_mbus_fmt	= ov9640_enum_fmt,
	.cropcap	= ov9640_cropcap,
	.g_crop		= ov9640_g_crop,

};

static struct v4l2_subdev_ops ov9640_subdev_ops = {
	.core	= &ov9640_core_ops,
	.video	= &ov9640_video_ops,
};

/*
 * i2c_driver function
 */
static int ov9640_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ov9640_priv *priv;
	struct soc_camera_device *icd	= client->dev.platform_data;
	struct soc_camera_link *icl;
	int ret;

	if (!icd) {
		dev_err(&client->dev, "Missing soc-camera data!\n");
		return -EINVAL;
	}

	icl = to_soc_camera_link(icd);
	if (!icl) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(struct ov9640_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev,
			"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov9640_subdev_ops);

	icd->ops	= &ov9640_ops;

	ret = ov9640_video_probe(icd, client);

	if (ret) {
		icd->ops = NULL;
		kfree(priv);
	}

	return ret;
}

static int ov9640_remove(struct i2c_client *client)
{
	struct ov9640_priv *priv = i2c_get_clientdata(client);

	kfree(priv);
	return 0;
}

static const struct i2c_device_id ov9640_id[] = {
	{ "ov9640", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov9640_id);

static struct i2c_driver ov9640_i2c_driver = {
	.driver = {
		.name = "ov9640",
	},
	.probe    = ov9640_probe,
	.remove   = ov9640_remove,
	.id_table = ov9640_id,
};

static int __init ov9640_module_init(void)
{
	return i2c_add_driver(&ov9640_i2c_driver);
}

static void __exit ov9640_module_exit(void)
{
	i2c_del_driver(&ov9640_i2c_driver);
}

module_init(ov9640_module_init);
module_exit(ov9640_module_exit);

MODULE_DESCRIPTION("SoC Camera driver for OmniVision OV96xx");
MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_LICENSE("GPL v2");
