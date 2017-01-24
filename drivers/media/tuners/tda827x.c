/*
 *
 * (c) 2005 Hartmut Hackmann
 * (c) 2007 Michael Krufky
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <asm/types.h>
#include <linux/dvb/frontend.h>
#include <linux/videodev2.h>

#include "tda827x.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

#define dprintk(args...) \
	do {					    \
		if (debug) printk(KERN_DEBUG "tda827x: " args); \
	} while (0)

struct tda827x_priv {
	int i2c_addr;
	struct i2c_adapter *i2c_adap;
	struct tda827x_config *cfg;

	unsigned int sgIF;
	unsigned char lpsel;

	u32 frequency;
	u32 bandwidth;
};

static void tda827x_set_std(struct dvb_frontend *fe,
			    struct analog_parameters *params)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	char *mode;

	priv->lpsel = 0;
	if (params->std & V4L2_STD_MN) {
		priv->sgIF = 92;
		priv->lpsel = 1;
		mode = "MN";
	} else if (params->std & V4L2_STD_B) {
		priv->sgIF = 108;
		mode = "B";
	} else if (params->std & V4L2_STD_GH) {
		priv->sgIF = 124;
		mode = "GH";
	} else if (params->std & V4L2_STD_PAL_I) {
		priv->sgIF = 124;
		mode = "I";
	} else if (params->std & V4L2_STD_DK) {
		priv->sgIF = 124;
		mode = "DK";
	} else if (params->std & V4L2_STD_SECAM_L) {
		priv->sgIF = 124;
		mode = "L";
	} else if (params->std & V4L2_STD_SECAM_LC) {
		priv->sgIF = 20;
		mode = "LC";
	} else {
		priv->sgIF = 124;
		mode = "xx";
	}

	if (params->mode == V4L2_TUNER_RADIO) {
		priv->sgIF = 88; /* if frequency is 5.5 MHz */
		dprintk("setting tda827x to radio FM\n");
	} else
		dprintk("setting tda827x to system %s\n", mode);
}


/* ------------------------------------------------------------------ */

struct tda827x_data {
	u32 lomax;
	u8  spd;
	u8  bs;
	u8  bp;
	u8  cp;
	u8  gc3;
	u8 div1p5;
};

static const struct tda827x_data tda827x_table[] = {
	{ .lomax =  62000000, .spd = 3, .bs = 2, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 1},
	{ .lomax =  66000000, .spd = 3, .bs = 3, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 1},
	{ .lomax =  76000000, .spd = 3, .bs = 1, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 0},
	{ .lomax =  84000000, .spd = 3, .bs = 2, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 0},
	{ .lomax =  93000000, .spd = 3, .bs = 2, .bp = 0, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax =  98000000, .spd = 3, .bs = 3, .bp = 0, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 109000000, .spd = 3, .bs = 3, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 123000000, .spd = 2, .bs = 2, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 133000000, .spd = 2, .bs = 3, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 151000000, .spd = 2, .bs = 1, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 154000000, .spd = 2, .bs = 2, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 181000000, .spd = 2, .bs = 2, .bp = 1, .cp = 0, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 185000000, .spd = 2, .bs = 2, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 217000000, .spd = 2, .bs = 3, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 244000000, .spd = 1, .bs = 2, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 265000000, .spd = 1, .bs = 3, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 302000000, .spd = 1, .bs = 1, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 324000000, .spd = 1, .bs = 2, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 370000000, .spd = 1, .bs = 2, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 454000000, .spd = 1, .bs = 3, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 493000000, .spd = 0, .bs = 2, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 530000000, .spd = 0, .bs = 3, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 554000000, .spd = 0, .bs = 1, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 604000000, .spd = 0, .bs = 1, .bp = 4, .cp = 0, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 696000000, .spd = 0, .bs = 2, .bp = 4, .cp = 0, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 740000000, .spd = 0, .bs = 2, .bp = 4, .cp = 1, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 820000000, .spd = 0, .bs = 3, .bp = 4, .cp = 0, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 865000000, .spd = 0, .bs = 3, .bp = 4, .cp = 1, .gc3 = 0, .div1p5 = 0},
	{ .lomax =         0, .spd = 0, .bs = 0, .bp = 0, .cp = 0, .gc3 = 0, .div1p5 = 0}
};

static int tuner_transfer(struct dvb_frontend *fe,
			  struct i2c_msg *msg,
			  const int size)
{
	int rc;
	struct tda827x_priv *priv = fe->tuner_priv;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	rc = i2c_transfer(priv->i2c_adap, msg, size);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (rc >= 0 && rc != size)
		return -EIO;

	return rc;
}

static int tda827xo_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct tda827x_priv *priv = fe->tuner_priv;
	u8 buf[14];
	int rc;

	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };
	int i, tuner_freq, if_freq;
	u32 N;

	dprintk("%s:\n", __func__);
	if (c->bandwidth_hz == 0) {
		if_freq = 5000000;
	} else if (c->bandwidth_hz <= 6000000) {
		if_freq = 4000000;
	} else if (c->bandwidth_hz <= 7000000) {
		if_freq = 4500000;
	} else {	/* 8 MHz */
		if_freq = 5000000;
	}
	tuner_freq = c->frequency;

	i = 0;
	while (tda827x_table[i].lomax < tuner_freq) {
		if (tda827x_table[i + 1].lomax == 0)
			break;
		i++;
	}

	tuner_freq += if_freq;

	N = ((tuner_freq + 125000) / 250000) << (tda827x_table[i].spd + 2);
	buf[0] = 0;
	buf[1] = (N>>8) | 0x40;
	buf[2] = N & 0xff;
	buf[3] = 0;
	buf[4] = 0x52;
	buf[5] = (tda827x_table[i].spd << 6) + (tda827x_table[i].div1p5 << 5) +
				(tda827x_table[i].bs << 3) +
				tda827x_table[i].bp;
	buf[6] = (tda827x_table[i].gc3 << 4) + 0x8f;
	buf[7] = 0xbf;
	buf[8] = 0x2a;
	buf[9] = 0x05;
	buf[10] = 0xff;
	buf[11] = 0x00;
	buf[12] = 0x00;
	buf[13] = 0x40;

	msg.len = 14;
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	msleep(500);
	/* correct CP value */
	buf[0] = 0x30;
	buf[1] = 0x50 + tda827x_table[i].cp;
	msg.len = 2;

	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	priv->frequency = c->frequency;
	priv->bandwidth = c->bandwidth_hz;

	return 0;

err:
	printk(KERN_ERR "%s: could not write to tuner at addr: 0x%02x\n",
	       __func__, priv->i2c_addr << 1);
	return rc;
}

static int tda827xo_sleep(struct dvb_frontend *fe)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	static u8 buf[] = { 0x30, 0xd0 };
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };

	dprintk("%s:\n", __func__);
	tuner_transfer(fe, &msg, 1);

	if (priv->cfg && priv->cfg->sleep)
		priv->cfg->sleep(fe);

	return 0;
}

/* ------------------------------------------------------------------ */

static int tda827xo_set_analog_params(struct dvb_frontend *fe,
				      struct analog_parameters *params)
{
	unsigned char tuner_reg[8];
	unsigned char reg2[2];
	u32 N;
	int i;
	struct tda827x_priv *priv = fe->tuner_priv;
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0 };
	unsigned int freq = params->frequency;

	tda827x_set_std(fe, params);

	if (params->mode == V4L2_TUNER_RADIO)
		freq = freq / 1000;

	N = freq + priv->sgIF;

	i = 0;
	while (tda827x_table[i].lomax < N * 62500) {
		if (tda827x_table[i + 1].lomax == 0)
			break;
		i++;
	}

	N = N << tda827x_table[i].spd;

	tuner_reg[0] = 0;
	tuner_reg[1] = (unsigned char)(N>>8);
	tuner_reg[2] = (unsigned char) N;
	tuner_reg[3] = 0x40;
	tuner_reg[4] = 0x52 + (priv->lpsel << 5);
	tuner_reg[5] = (tda827x_table[i].spd    << 6) +
		       (tda827x_table[i].div1p5 << 5) +
		       (tda827x_table[i].bs     << 3) + tda827x_table[i].bp;
	tuner_reg[6] = 0x8f + (tda827x_table[i].gc3 << 4);
	tuner_reg[7] = 0x8f;

	msg.buf = tuner_reg;
	msg.len = 8;
	tuner_transfer(fe, &msg, 1);

	msg.buf = reg2;
	msg.len = 2;
	reg2[0] = 0x80;
	reg2[1] = 0;
	tuner_transfer(fe, &msg, 1);

	reg2[0] = 0x60;
	reg2[1] = 0xbf;
	tuner_transfer(fe, &msg, 1);

	reg2[0] = 0x30;
	reg2[1] = tuner_reg[4] + 0x80;
	tuner_transfer(fe, &msg, 1);

	msleep(1);
	reg2[0] = 0x30;
	reg2[1] = tuner_reg[4] + 4;
	tuner_transfer(fe, &msg, 1);

	msleep(1);
	reg2[0] = 0x30;
	reg2[1] = tuner_reg[4];
	tuner_transfer(fe, &msg, 1);

	msleep(550);
	reg2[0] = 0x30;
	reg2[1] = (tuner_reg[4] & 0xfc) + tda827x_table[i].cp;
	tuner_transfer(fe, &msg, 1);

	reg2[0] = 0x60;
	reg2[1] = 0x3f;
	tuner_transfer(fe, &msg, 1);

	reg2[0] = 0x80;
	reg2[1] = 0x08;   /* Vsync en */
	tuner_transfer(fe, &msg, 1);

	priv->frequency = params->frequency;

	return 0;
}

static void tda827xo_agcf(struct dvb_frontend *fe)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	unsigned char data[] = { 0x80, 0x0c };
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = data, .len = 2};

	tuner_transfer(fe, &msg, 1);
}

/* ------------------------------------------------------------------ */

struct tda827xa_data {
	u32 lomax;
	u8  svco;
	u8  spd;
	u8  scr;
	u8  sbs;
	u8  gc3;
};

static struct tda827xa_data tda827xa_dvbt[] = {
	{ .lomax =  56875000, .svco = 3, .spd = 4, .scr = 0, .sbs = 0, .gc3 = 1},
	{ .lomax =  67250000, .svco = 0, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 1},
	{ .lomax =  81250000, .svco = 1, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 1},
	{ .lomax =  97500000, .svco = 2, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 1},
	{ .lomax = 113750000, .svco = 3, .spd = 3, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 134500000, .svco = 0, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 154000000, .svco = 1, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 162500000, .svco = 1, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 183000000, .svco = 2, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 195000000, .svco = 2, .spd = 2, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 227500000, .svco = 3, .spd = 2, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 269000000, .svco = 0, .spd = 1, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 290000000, .svco = 1, .spd = 1, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 325000000, .svco = 1, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 390000000, .svco = 2, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 455000000, .svco = 3, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 520000000, .svco = 0, .spd = 0, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 538000000, .svco = 0, .spd = 0, .scr = 1, .sbs = 3, .gc3 = 1},
	{ .lomax = 550000000, .svco = 1, .spd = 0, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 620000000, .svco = 1, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},
	{ .lomax = 650000000, .svco = 1, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},
	{ .lomax = 700000000, .svco = 2, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},
	{ .lomax = 780000000, .svco = 2, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},
	{ .lomax = 820000000, .svco = 3, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},
	{ .lomax = 870000000, .svco = 3, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},
	{ .lomax = 911000000, .svco = 3, .spd = 0, .scr = 2, .sbs = 4, .gc3 = 0},
	{ .lomax =         0, .svco = 0, .spd = 0, .scr = 0, .sbs = 0, .gc3 = 0}
};

static struct tda827xa_data tda827xa_dvbc[] = {
	{ .lomax =  50125000, .svco = 2, .spd = 4, .scr = 2, .sbs = 0, .gc3 = 3},
	{ .lomax =  58500000, .svco = 3, .spd = 4, .scr = 2, .sbs = 0, .gc3 = 3},
	{ .lomax =  69250000, .svco = 0, .spd = 3, .scr = 2, .sbs = 0, .gc3 = 3},
	{ .lomax =  83625000, .svco = 1, .spd = 3, .scr = 2, .sbs = 0, .gc3 = 3},
	{ .lomax =  97500000, .svco = 2, .spd = 3, .scr = 2, .sbs = 0, .gc3 = 3},
	{ .lomax = 100250000, .svco = 2, .spd = 3, .scr = 2, .sbs = 1, .gc3 = 1},
	{ .lomax = 117000000, .svco = 3, .spd = 3, .scr = 2, .sbs = 1, .gc3 = 1},
	{ .lomax = 138500000, .svco = 0, .spd = 2, .scr = 2, .sbs = 1, .gc3 = 1},
	{ .lomax = 167250000, .svco = 1, .spd = 2, .scr = 2, .sbs = 1, .gc3 = 1},
	{ .lomax = 187000000, .svco = 2, .spd = 2, .scr = 2, .sbs = 1, .gc3 = 1},
	{ .lomax = 200500000, .svco = 2, .spd = 2, .scr = 2, .sbs = 2, .gc3 = 1},
	{ .lomax = 234000000, .svco = 3, .spd = 2, .scr = 2, .sbs = 2, .gc3 = 3},
	{ .lomax = 277000000, .svco = 0, .spd = 1, .scr = 2, .sbs = 2, .gc3 = 3},
	{ .lomax = 325000000, .svco = 1, .spd = 1, .scr = 2, .sbs = 2, .gc3 = 1},
	{ .lomax = 334500000, .svco = 1, .spd = 1, .scr = 2, .sbs = 3, .gc3 = 3},
	{ .lomax = 401000000, .svco = 2, .spd = 1, .scr = 2, .sbs = 3, .gc3 = 3},
	{ .lomax = 468000000, .svco = 3, .spd = 1, .scr = 2, .sbs = 3, .gc3 = 1},
	{ .lomax = 535000000, .svco = 0, .spd = 0, .scr = 1, .sbs = 3, .gc3 = 1},
	{ .lomax = 554000000, .svco = 0, .spd = 0, .scr = 2, .sbs = 3, .gc3 = 1},
	{ .lomax = 638000000, .svco = 1, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 1},
	{ .lomax = 669000000, .svco = 1, .spd = 0, .scr = 2, .sbs = 4, .gc3 = 1},
	{ .lomax = 720000000, .svco = 2, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 1},
	{ .lomax = 802000000, .svco = 2, .spd = 0, .scr = 2, .sbs = 4, .gc3 = 1},
	{ .lomax = 835000000, .svco = 3, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 1},
	{ .lomax = 885000000, .svco = 3, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 1},
	{ .lomax = 911000000, .svco = 3, .spd = 0, .scr = 2, .sbs = 4, .gc3 = 1},
	{ .lomax =         0, .svco = 0, .spd = 0, .scr = 0, .sbs = 0, .gc3 = 0}
};

static struct tda827xa_data tda827xa_analog[] = {
	{ .lomax =  56875000, .svco = 3, .spd = 4, .scr = 0, .sbs = 0, .gc3 = 3},
	{ .lomax =  67250000, .svco = 0, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 3},
	{ .lomax =  81250000, .svco = 1, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 3},
	{ .lomax =  97500000, .svco = 2, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 3},
	{ .lomax = 113750000, .svco = 3, .spd = 3, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 134500000, .svco = 0, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 154000000, .svco = 1, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 162500000, .svco = 1, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 183000000, .svco = 2, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 195000000, .svco = 2, .spd = 2, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 227500000, .svco = 3, .spd = 2, .scr = 0, .sbs = 2, .gc3 = 3},
	{ .lomax = 269000000, .svco = 0, .spd = 1, .scr = 0, .sbs = 2, .gc3 = 3},
	{ .lomax = 325000000, .svco = 1, .spd = 1, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 390000000, .svco = 2, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 3},
	{ .lomax = 455000000, .svco = 3, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 3},
	{ .lomax = 520000000, .svco = 0, .spd = 0, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 538000000, .svco = 0, .spd = 0, .scr = 1, .sbs = 3, .gc3 = 1},
	{ .lomax = 554000000, .svco = 1, .spd = 0, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 620000000, .svco = 1, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},
	{ .lomax = 650000000, .svco = 1, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},
	{ .lomax = 700000000, .svco = 2, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},
	{ .lomax = 780000000, .svco = 2, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},
	{ .lomax = 820000000, .svco = 3, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},
	{ .lomax = 870000000, .svco = 3, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},
	{ .lomax = 911000000, .svco = 3, .spd = 0, .scr = 2, .sbs = 4, .gc3 = 0},
	{ .lomax =         0, .svco = 0, .spd = 0, .scr = 0, .sbs = 0, .gc3 = 0}
};

static int tda827xa_sleep(struct dvb_frontend *fe)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	static u8 buf[] = { 0x30, 0x90 };
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };

	dprintk("%s:\n", __func__);

	tuner_transfer(fe, &msg, 1);

	if (priv->cfg && priv->cfg->sleep)
		priv->cfg->sleep(fe);

	return 0;
}

static void tda827xa_lna_gain(struct dvb_frontend *fe, int high,
			      struct analog_parameters *params)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	unsigned char buf[] = {0x22, 0x01};
	int arg;
	int gp_func;
	struct i2c_msg msg = { .flags = 0, .buf = buf, .len = sizeof(buf) };

	if (NULL == priv->cfg) {
		dprintk("tda827x_config not defined, cannot set LNA gain!\n");
		return;
	}
	msg.addr = priv->cfg->switch_addr;
	if (priv->cfg->config) {
		if (high)
			dprintk("setting LNA to high gain\n");
		else
			dprintk("setting LNA to low gain\n");
	}
	switch (priv->cfg->config) {
	case TDA8290_LNA_OFF: /* no LNA */
		break;
	case TDA8290_LNA_GP0_HIGH_ON: /* switch is GPIO 0 of tda8290 */
	case TDA8290_LNA_GP0_HIGH_OFF:
		if (params == NULL) {
			gp_func = 0;
			arg  = 0;
		} else {
			/* turn Vsync on */
			gp_func = 1;
			if (params->std & V4L2_STD_MN)
				arg = 1;
			else
				arg = 0;
		}
		if (fe->callback)
			fe->callback(priv->i2c_adap->algo_data,
				     DVB_FRONTEND_COMPONENT_TUNER,
				     gp_func, arg);
		buf[1] = high ? 0 : 1;
		if (priv->cfg->config == TDA8290_LNA_GP0_HIGH_OFF)
			buf[1] = high ? 1 : 0;
		tuner_transfer(fe, &msg, 1);
		break;
	case TDA8290_LNA_ON_BRIDGE: /* switch with GPIO of saa713x */
		if (fe->callback)
			fe->callback(priv->i2c_adap->algo_data,
				     DVB_FRONTEND_COMPONENT_TUNER, 0, high);
		break;
	}
}

static int tda827xa_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct tda827x_priv *priv = fe->tuner_priv;
	struct tda827xa_data *frequency_map = tda827xa_dvbt;
	u8 buf[11];

	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };

	int i, tuner_freq, if_freq, rc;
	u32 N;

	dprintk("%s:\n", __func__);

	tda827xa_lna_gain(fe, 1, NULL);
	msleep(20);

	if (c->bandwidth_hz == 0) {
		if_freq = 5000000;
	} else if (c->bandwidth_hz <= 6000000) {
		if_freq = 4000000;
	} else if (c->bandwidth_hz <= 7000000) {
		if_freq = 4500000;
	} else {	/* 8 MHz */
		if_freq = 5000000;
	}
	tuner_freq = c->frequency;

	switch (c->delivery_system) {
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
		dprintk("%s select tda827xa_dvbc\n", __func__);
		frequency_map = tda827xa_dvbc;
		break;
	default:
		break;
	}

	i = 0;
	while (frequency_map[i].lomax < tuner_freq) {
		if (frequency_map[i + 1].lomax == 0)
			break;
		i++;
	}

	tuner_freq += if_freq;

	N = ((tuner_freq + 31250) / 62500) << frequency_map[i].spd;
	buf[0] = 0;            // subaddress
	buf[1] = N >> 8;
	buf[2] = N & 0xff;
	buf[3] = 0;
	buf[4] = 0x16;
	buf[5] = (frequency_map[i].spd << 5) + (frequency_map[i].svco << 3) +
			frequency_map[i].sbs;
	buf[6] = 0x4b + (frequency_map[i].gc3 << 4);
	buf[7] = 0x1c;
	buf[8] = 0x06;
	buf[9] = 0x24;
	buf[10] = 0x00;
	msg.len = 11;
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	buf[0] = 0x90;
	buf[1] = 0xff;
	buf[2] = 0x60;
	buf[3] = 0x00;
	buf[4] = 0x59;  // lpsel, for 6MHz + 2
	msg.len = 5;
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	buf[0] = 0xa0;
	buf[1] = 0x40;
	msg.len = 2;
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	msleep(11);
	msg.flags = I2C_M_RD;
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;
	msg.flags = 0;

	buf[1] >>= 4;
	dprintk("tda8275a AGC2 gain is: %d\n", buf[1]);
	if ((buf[1]) < 2) {
		tda827xa_lna_gain(fe, 0, NULL);
		buf[0] = 0x60;
		buf[1] = 0x0c;
		rc = tuner_transfer(fe, &msg, 1);
		if (rc < 0)
			goto err;
	}

	buf[0] = 0xc0;
	buf[1] = 0x99;    // lpsel, for 6MHz + 2
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	buf[0] = 0x60;
	buf[1] = 0x3c;
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	/* correct CP value */
	buf[0] = 0x30;
	buf[1] = 0x10 + frequency_map[i].scr;
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	msleep(163);
	buf[0] = 0xc0;
	buf[1] = 0x39;  // lpsel, for 6MHz + 2
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	msleep(3);
	/* freeze AGC1 */
	buf[0] = 0x50;
	buf[1] = 0x4f + (frequency_map[i].gc3 << 4);
	rc = tuner_transfer(fe, &msg, 1);
	if (rc < 0)
		goto err;

	priv->frequency = c->frequency;
	priv->bandwidth = c->bandwidth_hz;

	return 0;

err:
	printk(KERN_ERR "%s: could not write to tuner at addr: 0x%02x\n",
	       __func__, priv->i2c_addr << 1);
	return rc;
}


static int tda827xa_set_analog_params(struct dvb_frontend *fe,
				      struct analog_parameters *params)
{
	unsigned char tuner_reg[11];
	u32 N;
	int i;
	struct tda827x_priv *priv = fe->tuner_priv;
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = tuner_reg, .len = sizeof(tuner_reg) };
	unsigned int freq = params->frequency;

	tda827x_set_std(fe, params);

	tda827xa_lna_gain(fe, 1, params);
	msleep(10);

	if (params->mode == V4L2_TUNER_RADIO)
		freq = freq / 1000;

	N = freq + priv->sgIF;

	i = 0;
	while (tda827xa_analog[i].lomax < N * 62500) {
		if (tda827xa_analog[i + 1].lomax == 0)
			break;
		i++;
	}

	N = N << tda827xa_analog[i].spd;

	tuner_reg[0] = 0;
	tuner_reg[1] = (unsigned char)(N>>8);
	tuner_reg[2] = (unsigned char) N;
	tuner_reg[3] = 0;
	tuner_reg[4] = 0x16;
	tuner_reg[5] = (tda827xa_analog[i].spd << 5) +
		       (tda827xa_analog[i].svco << 3) +
			tda827xa_analog[i].sbs;
	tuner_reg[6] = 0x8b + (tda827xa_analog[i].gc3 << 4);
	tuner_reg[7] = 0x1c;
	tuner_reg[8] = 4;
	tuner_reg[9] = 0x20;
	tuner_reg[10] = 0x00;
	msg.len = 11;
	tuner_transfer(fe, &msg, 1);

	tuner_reg[0] = 0x90;
	tuner_reg[1] = 0xff;
	tuner_reg[2] = 0xe0;
	tuner_reg[3] = 0;
	tuner_reg[4] = 0x99 + (priv->lpsel << 1);
	msg.len = 5;
	tuner_transfer(fe, &msg, 1);

	tuner_reg[0] = 0xa0;
	tuner_reg[1] = 0xc0;
	msg.len = 2;
	tuner_transfer(fe, &msg, 1);

	tuner_reg[0] = 0x30;
	tuner_reg[1] = 0x10 + tda827xa_analog[i].scr;
	tuner_transfer(fe, &msg, 1);

	msg.flags = I2C_M_RD;
	tuner_transfer(fe, &msg, 1);
	msg.flags = 0;
	tuner_reg[1] >>= 4;
	dprintk("AGC2 gain is: %d\n", tuner_reg[1]);
	if (tuner_reg[1] < 1)
		tda827xa_lna_gain(fe, 0, params);

	msleep(100);
	tuner_reg[0] = 0x60;
	tuner_reg[1] = 0x3c;
	tuner_transfer(fe, &msg, 1);

	msleep(163);
	tuner_reg[0] = 0x50;
	tuner_reg[1] = 0x8f + (tda827xa_analog[i].gc3 << 4);
	tuner_transfer(fe, &msg, 1);

	tuner_reg[0] = 0x80;
	tuner_reg[1] = 0x28;
	tuner_transfer(fe, &msg, 1);

	tuner_reg[0] = 0xb0;
	tuner_reg[1] = 0x01;
	tuner_transfer(fe, &msg, 1);

	tuner_reg[0] = 0xc0;
	tuner_reg[1] = 0x19 + (priv->lpsel << 1);
	tuner_transfer(fe, &msg, 1);

	priv->frequency = params->frequency;

	return 0;
}

static void tda827xa_agcf(struct dvb_frontend *fe)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	unsigned char data[] = {0x80, 0x2c};
	struct i2c_msg msg = {.addr = priv->i2c_addr, .flags = 0,
			      .buf = data, .len = 2};
	tuner_transfer(fe, &msg, 1);
}

/* ------------------------------------------------------------------ */

static void tda827x_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static int tda827x_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static int tda827x_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	*bandwidth = priv->bandwidth;
	return 0;
}

static int tda827x_init(struct dvb_frontend *fe)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	dprintk("%s:\n", __func__);
	if (priv->cfg && priv->cfg->init)
		priv->cfg->init(fe);

	return 0;
}

static int tda827x_probe_version(struct dvb_frontend *fe);

static int tda827x_initial_init(struct dvb_frontend *fe)
{
	int ret;
	ret = tda827x_probe_version(fe);
	if (ret)
		return ret;
	return fe->ops.tuner_ops.init(fe);
}

static int tda827x_initial_sleep(struct dvb_frontend *fe)
{
	int ret;
	ret = tda827x_probe_version(fe);
	if (ret)
		return ret;
	return fe->ops.tuner_ops.sleep(fe);
}

static const struct dvb_tuner_ops tda827xo_tuner_ops = {
	.info = {
		.name = "Philips TDA827X",
		.frequency_min  =  55000000,
		.frequency_max  = 860000000,
		.frequency_step =    250000
	},
	.release = tda827x_release,
	.init = tda827x_initial_init,
	.sleep = tda827x_initial_sleep,
	.set_params = tda827xo_set_params,
	.set_analog_params = tda827xo_set_analog_params,
	.get_frequency = tda827x_get_frequency,
	.get_bandwidth = tda827x_get_bandwidth,
};

static const struct dvb_tuner_ops tda827xa_tuner_ops = {
	.info = {
		.name = "Philips TDA827XA",
		.frequency_min  =  44000000,
		.frequency_max  = 906000000,
		.frequency_step =     62500
	},
	.release = tda827x_release,
	.init = tda827x_init,
	.sleep = tda827xa_sleep,
	.set_params = tda827xa_set_params,
	.set_analog_params = tda827xa_set_analog_params,
	.get_frequency = tda827x_get_frequency,
	.get_bandwidth = tda827x_get_bandwidth,
};

static int tda827x_probe_version(struct dvb_frontend *fe)
{
	u8 data;
	int rc;
	struct tda827x_priv *priv = fe->tuner_priv;
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = I2C_M_RD,
			       .buf = &data, .len = 1 };

	rc = tuner_transfer(fe, &msg, 1);

	if (rc < 0) {
		printk("%s: could not read from tuner at addr: 0x%02x\n",
		       __func__, msg.addr << 1);
		return rc;
	}
	if ((data & 0x3c) == 0) {
		dprintk("tda827x tuner found\n");
		fe->ops.tuner_ops.init  = tda827x_init;
		fe->ops.tuner_ops.sleep = tda827xo_sleep;
		if (priv->cfg)
			priv->cfg->agcf = tda827xo_agcf;
	} else {
		dprintk("tda827xa tuner found\n");
		memcpy(&fe->ops.tuner_ops, &tda827xa_tuner_ops, sizeof(struct dvb_tuner_ops));
		if (priv->cfg)
			priv->cfg->agcf = tda827xa_agcf;
	}
	return 0;
}

struct dvb_frontend *tda827x_attach(struct dvb_frontend *fe, int addr,
				    struct i2c_adapter *i2c,
				    struct tda827x_config *cfg)
{
	struct tda827x_priv *priv = NULL;

	dprintk("%s:\n", __func__);
	priv = kzalloc(sizeof(struct tda827x_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_addr = addr;
	priv->i2c_adap = i2c;
	priv->cfg = cfg;
	memcpy(&fe->ops.tuner_ops, &tda827xo_tuner_ops, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = priv;

	dprintk("type set to %s\n", fe->ops.tuner_ops.info.name);

	return fe;
}
EXPORT_SYMBOL_GPL(tda827x_attach);

MODULE_DESCRIPTION("DVB TDA827x driver");
MODULE_AUTHOR("Hartmut Hackmann <hartmut.hackmann@t-online.de>");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_LICENSE("GPL");
