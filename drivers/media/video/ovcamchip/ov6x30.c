/* OmniVision OV6630/OV6130 Camera Chip Support Code
 *
 * Copyright (c) 1999-2004 Mark McClelland <mark@alpha.dyndns.org>
 * http://alpha.dyndns.org/ov511/
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. NO WARRANTY OF ANY KIND is expressed or implied.
 */

#define DEBUG

#include <linux/slab.h>
#include "ovcamchip_priv.h"

/* Registers */
#define REG_GAIN		0x00	/* gain [5:0] */
#define REG_BLUE		0x01	/* blue gain */
#define REG_RED			0x02	/* red gain */
#define REG_SAT			0x03	/* saturation [7:3] */
#define REG_CNT			0x05	/* Y contrast [3:0] */
#define REG_BRT			0x06	/* Y brightness */
#define REG_SHARP		0x07	/* sharpness */
#define REG_WB_BLUE		0x0C	/* WB blue ratio [5:0] */
#define REG_WB_RED		0x0D	/* WB red ratio [5:0] */
#define REG_EXP			0x10	/* exposure */

/* Window parameters */
#define HWSBASE 0x38
#define HWEBASE 0x3A
#define VWSBASE 0x05
#define VWEBASE 0x06

struct ov6x30 {
	int auto_brt;
	int auto_exp;
	int backlight;
	int bandfilt;
	int mirror;
};

static struct ovcamchip_regvals regvals_init_6x30[] = {
	{ 0x12, 0x80 }, /* reset */
	{ 0x00, 0x1f }, /* Gain */
	{ 0x01, 0x99 }, /* Blue gain */
	{ 0x02, 0x7c }, /* Red gain */
	{ 0x03, 0xc0 }, /* Saturation */
	{ 0x05, 0x0a }, /* Contrast */
	{ 0x06, 0x95 }, /* Brightness */
	{ 0x07, 0x2d }, /* Sharpness */
	{ 0x0c, 0x20 },
	{ 0x0d, 0x20 },
	{ 0x0e, 0x20 },
	{ 0x0f, 0x05 },
	{ 0x10, 0x9a }, /* "exposure check" */
	{ 0x11, 0x00 }, /* Pixel clock = fastest */
	{ 0x12, 0x24 }, /* Enable AGC and AWB */
	{ 0x13, 0x21 },
	{ 0x14, 0x80 },
	{ 0x15, 0x01 },
	{ 0x16, 0x03 },
	{ 0x17, 0x38 },
	{ 0x18, 0xea },
	{ 0x19, 0x04 },
	{ 0x1a, 0x93 },
	{ 0x1b, 0x00 },
	{ 0x1e, 0xc4 },
	{ 0x1f, 0x04 },
	{ 0x20, 0x20 },
	{ 0x21, 0x10 },
	{ 0x22, 0x88 },
	{ 0x23, 0xc0 }, /* Crystal circuit power level */
	{ 0x25, 0x9a }, /* Increase AEC black pixel ratio */
	{ 0x26, 0xb2 }, /* BLC enable */
	{ 0x27, 0xa2 },
	{ 0x28, 0x00 },
	{ 0x29, 0x00 },
	{ 0x2a, 0x84 }, /* (keep) */
	{ 0x2b, 0xa8 }, /* (keep) */
	{ 0x2c, 0xa0 },
	{ 0x2d, 0x95 },	/* Enable auto-brightness */
	{ 0x2e, 0x88 },
	{ 0x33, 0x26 },
	{ 0x34, 0x03 },
	{ 0x36, 0x8f },
	{ 0x37, 0x80 },
	{ 0x38, 0x83 },
	{ 0x39, 0x80 },
	{ 0x3a, 0x0f },
	{ 0x3b, 0x3c },
	{ 0x3c, 0x1a },
	{ 0x3d, 0x80 },
	{ 0x3e, 0x80 },
	{ 0x3f, 0x0e },
	{ 0x40, 0x00 }, /* White bal */
	{ 0x41, 0x00 }, /* White bal */
	{ 0x42, 0x80 },
	{ 0x43, 0x3f }, /* White bal */
	{ 0x44, 0x80 },
	{ 0x45, 0x20 },
	{ 0x46, 0x20 },
	{ 0x47, 0x80 },
	{ 0x48, 0x7f },
	{ 0x49, 0x00 },
	{ 0x4a, 0x00 },
	{ 0x4b, 0x80 },
	{ 0x4c, 0xd0 },
	{ 0x4d, 0x10 }, /* U = 0.563u, V = 0.714v */
	{ 0x4e, 0x40 },
	{ 0x4f, 0x07 }, /* UV average mode, color killer: strongest */
	{ 0x50, 0xff },
	{ 0x54, 0x23 }, /* Max AGC gain: 18dB */
	{ 0x55, 0xff },
	{ 0x56, 0x12 },
	{ 0x57, 0x81 }, /* (default) */
	{ 0x58, 0x75 },
	{ 0x59, 0x01 }, /* AGC dark current compensation: +1 */
	{ 0x5a, 0x2c },
	{ 0x5b, 0x0f }, /* AWB chrominance levels */
	{ 0x5c, 0x10 },
	{ 0x3d, 0x80 },
	{ 0x27, 0xa6 },
	/* Toggle AWB off and on */
	{ 0x12, 0x20 },
	{ 0x12, 0x24 },

	{ 0xff, 0xff },	/* END MARKER */
};

/* This initializes the OV6x30 camera chip and relevant variables. */
static int ov6x30_init(struct i2c_client *c)
{
	struct ovcamchip *ov = i2c_get_clientdata(c);
	struct ov6x30 *s;
	int rc;

	DDEBUG(4, &c->dev, "entered");

	rc = ov_write_regvals(c, regvals_init_6x30);
	if (rc < 0)
		return rc;

	ov->spriv = s = kmalloc(sizeof *s, GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	memset(s, 0, sizeof *s);

	s->auto_brt = 1;
	s->auto_exp = 1;

	return rc;
}

static int ov6x30_free(struct i2c_client *c)
{
	struct ovcamchip *ov = i2c_get_clientdata(c);

	kfree(ov->spriv);
	return 0;
}

static int ov6x30_set_control(struct i2c_client *c,
			      struct ovcamchip_control *ctl)
{
	struct ovcamchip *ov = i2c_get_clientdata(c);
	struct ov6x30 *s = ov->spriv;
	int rc;
	int v = ctl->value;

	switch (ctl->id) {
	case OVCAMCHIP_CID_CONT:
		rc = ov_write_mask(c, REG_CNT, v >> 12, 0x0f);
		break;
	case OVCAMCHIP_CID_BRIGHT:
		rc = ov_write(c, REG_BRT, v >> 8);
		break;
	case OVCAMCHIP_CID_SAT:
		rc = ov_write(c, REG_SAT, v >> 8);
		break;
	case OVCAMCHIP_CID_HUE:
		rc = ov_write(c, REG_RED, 0xFF - (v >> 8));
		if (rc < 0)
			goto out;

		rc = ov_write(c, REG_BLUE, v >> 8);
		break;
	case OVCAMCHIP_CID_EXP:
		rc = ov_write(c, REG_EXP, v);
		break;
	case OVCAMCHIP_CID_FREQ:
	{
		int sixty = (v == 60);

		rc = ov_write(c, 0x2b, sixty?0xa8:0x28);
		if (rc < 0)
			goto out;

		rc = ov_write(c, 0x2a, sixty?0x84:0xa4);
		break;
	}
	case OVCAMCHIP_CID_BANDFILT:
		rc = ov_write_mask(c, 0x2d, v?0x04:0x00, 0x04);
		s->bandfilt = v;
		break;
	case OVCAMCHIP_CID_AUTOBRIGHT:
		rc = ov_write_mask(c, 0x2d, v?0x10:0x00, 0x10);
		s->auto_brt = v;
		break;
	case OVCAMCHIP_CID_AUTOEXP:
		rc = ov_write_mask(c, 0x28, v?0x00:0x10, 0x10);
		s->auto_exp = v;
		break;
	case OVCAMCHIP_CID_BACKLIGHT:
	{
		rc = ov_write_mask(c, 0x4e, v?0x80:0x60, 0xe0);
		if (rc < 0)
			goto out;

		rc = ov_write_mask(c, 0x29, v?0x08:0x00, 0x08);
		if (rc < 0)
			goto out;

		rc = ov_write_mask(c, 0x28, v?0x02:0x00, 0x02);
		s->backlight = v;
		break;
	}
	case OVCAMCHIP_CID_MIRROR:
		rc = ov_write_mask(c, 0x12, v?0x40:0x00, 0x40);
		s->mirror = v;
		break;
	default:
		DDEBUG(2, &c->dev, "control not supported: %d", ctl->id);
		return -EPERM;
	}

out:
	DDEBUG(3, &c->dev, "id=%d, arg=%d, rc=%d", ctl->id, v, rc);
	return rc;
}

static int ov6x30_get_control(struct i2c_client *c,
			      struct ovcamchip_control *ctl)
{
	struct ovcamchip *ov = i2c_get_clientdata(c);
	struct ov6x30 *s = ov->spriv;
	int rc = 0;
	unsigned char val = 0;

	switch (ctl->id) {
	case OVCAMCHIP_CID_CONT:
		rc = ov_read(c, REG_CNT, &val);
		ctl->value = (val & 0x0f) << 12;
		break;
	case OVCAMCHIP_CID_BRIGHT:
		rc = ov_read(c, REG_BRT, &val);
		ctl->value = val << 8;
		break;
	case OVCAMCHIP_CID_SAT:
		rc = ov_read(c, REG_SAT, &val);
		ctl->value = val << 8;
		break;
	case OVCAMCHIP_CID_HUE:
		rc = ov_read(c, REG_BLUE, &val);
		ctl->value = val << 8;
		break;
	case OVCAMCHIP_CID_EXP:
		rc = ov_read(c, REG_EXP, &val);
		ctl->value = val;
		break;
	case OVCAMCHIP_CID_BANDFILT:
		ctl->value = s->bandfilt;
		break;
	case OVCAMCHIP_CID_AUTOBRIGHT:
		ctl->value = s->auto_brt;
		break;
	case OVCAMCHIP_CID_AUTOEXP:
		ctl->value = s->auto_exp;
		break;
	case OVCAMCHIP_CID_BACKLIGHT:
		ctl->value = s->backlight;
		break;
	case OVCAMCHIP_CID_MIRROR:
		ctl->value = s->mirror;
		break;
	default:
		DDEBUG(2, &c->dev, "control not supported: %d", ctl->id);
		return -EPERM;
	}

	DDEBUG(3, &c->dev, "id=%d, arg=%d, rc=%d", ctl->id, ctl->value, rc);
	return rc;
}

static int ov6x30_mode_init(struct i2c_client *c, struct ovcamchip_window *win)
{
	/******** QCIF-specific regs ********/

	ov_write_mask(c, 0x14, win->quarter?0x20:0x00, 0x20);

	/******** Palette-specific regs ********/

	if (win->format == VIDEO_PALETTE_GREY) {
		if (c->adapter->id == I2C_HW_SMBUS_OV518) {
			/* Do nothing - we're already in 8-bit mode */
		} else {
			ov_write_mask(c, 0x13, 0x20, 0x20);
		}
	} else {
		/* The OV518 needs special treatment. Although both the OV518
		 * and the OV6630 support a 16-bit video bus, only the 8 bit Y
		 * bus is actually used. The UV bus is tied to ground.
		 * Therefore, the OV6630 needs to be in 8-bit multiplexed
		 * output mode */

		if (c->adapter->id == I2C_HW_SMBUS_OV518) {
			/* Do nothing - we want to stay in 8-bit mode */
			/* Warning: Messing with reg 0x13 breaks OV518 color */
		} else {
			ov_write_mask(c, 0x13, 0x00, 0x20);
		}
	}

	/******** Clock programming ********/

	ov_write(c, 0x11, win->clockdiv);

	return 0;
}

static int ov6x30_set_window(struct i2c_client *c, struct ovcamchip_window *win)
{
	int ret, hwscale, vwscale;

	ret = ov6x30_mode_init(c, win);
	if (ret < 0)
		return ret;

	if (win->quarter) {
		hwscale = 0;
		vwscale = 0;
	} else {
		hwscale = 1;
		vwscale = 1;	/* The datasheet says 0; it's wrong */
	}

	ov_write(c, 0x17, HWSBASE + (win->x >> hwscale));
	ov_write(c, 0x18, HWEBASE + ((win->x + win->width) >> hwscale));
	ov_write(c, 0x19, VWSBASE + (win->y >> vwscale));
	ov_write(c, 0x1a, VWEBASE + ((win->y + win->height) >> vwscale));

	return 0;
}

static int ov6x30_command(struct i2c_client *c, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case OVCAMCHIP_CMD_S_CTRL:
		return ov6x30_set_control(c, arg);
	case OVCAMCHIP_CMD_G_CTRL:
		return ov6x30_get_control(c, arg);
	case OVCAMCHIP_CMD_S_MODE:
		return ov6x30_set_window(c, arg);
	default:
		DDEBUG(2, &c->dev, "command not supported: %d", cmd);
		return -ENOIOCTLCMD;
	}
}

struct ovcamchip_ops ov6x30_ops = {
	.init    =	ov6x30_init,
	.free    =	ov6x30_free,
	.command =	ov6x30_command,
};
