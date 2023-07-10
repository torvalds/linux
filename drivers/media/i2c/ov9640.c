// SPDX-License-Identifier: GPL-2.0
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
 * transition from soc_camera to pxa_camera based on mt9m111
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>

#include <linux/gpio/consumer.h>

#include "ov9640.h"

#define to_ov9640_sensor(sd)	container_of(sd, struct ov9640_priv, subdev)

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
 *		COM12 |= OV9640_COM12_YUV_AVG
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

static const u32 ov9640_codes[] = {
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
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
		return ret;
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
			"An error occurred while entering soft reset!\n");

	return ret;
}

/* Start/Stop streaming from the device */
static int ov9640_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

/* Set status of additional camera capabilities */
static int ov9640_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov9640_priv *priv = container_of(ctrl->handler,
						struct ov9640_priv, hdl);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			return ov9640_reg_rmw(client, OV9640_MVFP,
					      OV9640_MVFP_V, 0);
		return ov9640_reg_rmw(client, OV9640_MVFP, 0, OV9640_MVFP_V);
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			return ov9640_reg_rmw(client, OV9640_MVFP,
					      OV9640_MVFP_H, 0);
		return ov9640_reg_rmw(client, OV9640_MVFP, 0, OV9640_MVFP_H);
	}

	return -EINVAL;
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
				const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg & ~0xff || reg->val & ~0xff)
		return -EINVAL;

	return ov9640_reg_write(client, reg->reg, reg->val);
}
#endif

static int ov9640_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov9640_priv *priv = to_ov9640_sensor(sd);
	int ret = 0;

	if (on) {
		gpiod_set_value(priv->gpio_power, 1);
		usleep_range(1000, 2000);
		ret = clk_prepare_enable(priv->clk);
		usleep_range(1000, 2000);
		gpiod_set_value(priv->gpio_reset, 0);
	} else {
		gpiod_set_value(priv->gpio_reset, 1);
		usleep_range(1000, 2000);
		clk_disable_unprepare(priv->clk);
		usleep_range(1000, 2000);
		gpiod_set_value(priv->gpio_power, 0);
	}

	return ret;
}

/* select nearest higher resolution for capture */
static void ov9640_res_roundup(u32 *width, u32 *height)
{
	unsigned int i;
	enum { QQCIF, QQVGA, QCIF, QVGA, CIF, VGA, SXGA };
	static const u32 res_x[] = { 88, 160, 176, 320, 352, 640, 1280 };
	static const u32 res_y[] = { 72, 120, 144, 240, 288, 480, 960 };

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
static void ov9640_alter_regs(u32 code,
			      struct ov9640_reg_alt *alt)
{
	switch (code) {
	default:
	case MEDIA_BUS_FMT_UYVY8_2X8:
		alt->com12	= OV9640_COM12_YUV_AVG;
		alt->com13	= OV9640_COM13_Y_DELAY_EN |
					OV9640_COM13_YUV_DLY(0x01);
		break;
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE:
		alt->com7	= OV9640_COM7_RGB;
		alt->com13	= OV9640_COM13_RGB_AVG;
		alt->com15	= OV9640_COM15_RGB_555;
		break;
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		alt->com7	= OV9640_COM7_RGB;
		alt->com13	= OV9640_COM13_RGB_AVG;
		alt->com15	= OV9640_COM15_RGB_565;
		break;
	}
}

/* Setup registers according to resolution and color encoding */
static int ov9640_write_regs(struct i2c_client *client, u32 width,
		u32 code, struct ov9640_reg_alt *alts)
{
	const struct ov9640_reg	*ov9640_regs, *matrix_regs;
	unsigned int		ov9640_regs_len, matrix_regs_len;
	unsigned int		i;
	int			ret;
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
	if (code == MEDIA_BUS_FMT_UYVY8_2X8) {
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
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ov9640_regs_dflt); i++) {
		ret = ov9640_reg_write(client, ov9640_regs_dflt[i].reg,
				       ov9640_regs_dflt[i].val);
		if (ret)
			return ret;
	}

	/* wait for the changes to actually happen, 140ms are not enough yet */
	msleep(150);

	return 0;
}

/* set the format we will capture in */
static int ov9640_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov9640_reg_alt alts = {0};
	int ret;

	ov9640_alter_regs(mf->code, &alts);

	ov9640_reset(client);

	ret = ov9640_prog_dflt(client);
	if (ret)
		return ret;

	return ov9640_write_regs(client, mf->width, mf->code, &alts);
}

static int ov9640_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	if (format->pad)
		return -EINVAL;

	ov9640_res_roundup(&mf->width, &mf->height);

	mf->field = V4L2_FIELD_NONE;

	switch (mf->code) {
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE:
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		mf->colorspace = V4L2_COLORSPACE_SRGB;
		break;
	default:
		mf->code = MEDIA_BUS_FMT_UYVY8_2X8;
		fallthrough;
	case MEDIA_BUS_FMT_UYVY8_2X8:
		mf->colorspace = V4L2_COLORSPACE_JPEG;
		break;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return ov9640_s_fmt(sd, mf);

	sd_state->pads->try_fmt = *mf;

	return 0;
}

static int ov9640_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ov9640_codes))
		return -EINVAL;

	code->code = ov9640_codes[code->index];

	return 0;
}

static int ov9640_get_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_selection *sel)
{
	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	sel->r.left = 0;
	sel->r.top = 0;
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP:
		sel->r.width = W_SXGA;
		sel->r.height = H_SXGA;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ov9640_video_probe(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9640_priv *priv = to_ov9640_sensor(sd);
	u8		pid, ver, midh, midl;
	const char	*devname;
	int		ret;

	ret = ov9640_s_power(&priv->subdev, 1);
	if (ret < 0)
		return ret;

	/*
	 * check and show product ID and manufacturer ID
	 */

	ret = ov9640_reg_read(client, OV9640_PID, &pid);
	if (!ret)
		ret = ov9640_reg_read(client, OV9640_VER, &ver);
	if (!ret)
		ret = ov9640_reg_read(client, OV9640_MIDH, &midh);
	if (!ret)
		ret = ov9640_reg_read(client, OV9640_MIDL, &midl);
	if (ret)
		goto done;

	switch (VERSION(pid, ver)) {
	case OV9640_V2:
		devname		= "ov9640";
		priv->revision	= 2;
		break;
	case OV9640_V3:
		devname		= "ov9640";
		priv->revision	= 3;
		break;
	default:
		dev_err(&client->dev, "Product ID error %x:%x\n", pid, ver);
		ret = -ENODEV;
		goto done;
	}

	dev_info(&client->dev, "%s Product ID %0x:%0x Manufacturer ID %x:%x\n",
		 devname, pid, ver, midh, midl);

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

done:
	ov9640_s_power(&priv->subdev, 0);
	return ret;
}

static const struct v4l2_ctrl_ops ov9640_ctrl_ops = {
	.s_ctrl = ov9640_s_ctrl,
};

static const struct v4l2_subdev_core_ops ov9640_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register		= ov9640_get_register,
	.s_register		= ov9640_set_register,
#endif
	.s_power		= ov9640_s_power,
};

/* Request bus settings on camera side */
static int ov9640_get_mbus_config(struct v4l2_subdev *sd,
				  unsigned int pad,
				  struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_PARALLEL;
	cfg->bus.parallel.flags = V4L2_MBUS_PCLK_SAMPLE_RISING |
				  V4L2_MBUS_MASTER |
				  V4L2_MBUS_VSYNC_ACTIVE_HIGH |
				  V4L2_MBUS_HSYNC_ACTIVE_HIGH |
				  V4L2_MBUS_DATA_ACTIVE_HIGH;

	return 0;
}

static const struct v4l2_subdev_video_ops ov9640_video_ops = {
	.s_stream	= ov9640_s_stream,
};

static const struct v4l2_subdev_pad_ops ov9640_pad_ops = {
	.enum_mbus_code = ov9640_enum_mbus_code,
	.get_selection	= ov9640_get_selection,
	.set_fmt	= ov9640_set_fmt,
	.get_mbus_config = ov9640_get_mbus_config,
};

static const struct v4l2_subdev_ops ov9640_subdev_ops = {
	.core	= &ov9640_core_ops,
	.video	= &ov9640_video_ops,
	.pad	= &ov9640_pad_ops,
};

/*
 * i2c_driver function
 */
static int ov9640_probe(struct i2c_client *client)
{
	struct ov9640_priv *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->gpio_power = devm_gpiod_get(&client->dev, "Camera power",
					  GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpio_power)) {
		ret = PTR_ERR(priv->gpio_power);
		return ret;
	}

	priv->gpio_reset = devm_gpiod_get(&client->dev, "Camera reset",
					  GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpio_reset)) {
		ret = PTR_ERR(priv->gpio_reset);
		return ret;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov9640_subdev_ops);

	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &ov9640_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ov9640_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);

	if (priv->hdl.error) {
		ret = priv->hdl.error;
		goto ectrlinit;
	}

	priv->subdev.ctrl_handler = &priv->hdl;

	priv->clk = devm_clk_get(&client->dev, "mclk");
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		goto ectrlinit;
	}

	ret = ov9640_video_probe(client);
	if (ret)
		goto ectrlinit;

	priv->subdev.dev = &client->dev;
	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret)
		goto ectrlinit;

	return 0;

ectrlinit:
	v4l2_ctrl_handler_free(&priv->hdl);

	return ret;
}

static void ov9640_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9640_priv *priv = to_ov9640_sensor(sd);

	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->hdl);
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

module_i2c_driver(ov9640_i2c_driver);

MODULE_DESCRIPTION("OmniVision OV96xx CMOS Image Sensor driver");
MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_LICENSE("GPL v2");
