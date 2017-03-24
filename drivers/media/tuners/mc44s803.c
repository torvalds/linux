/*
 *  Driver for Freescale MC44S803 Low Power CMOS Broadband Tuner
 *
 *  Copyright (c) 2009 Jochen Friedrich <jochen@scram.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>
#include <linux/slab.h>

#include "dvb_frontend.h"

#include "mc44s803.h"
#include "mc44s803_priv.h"

#define mc_printk(level, format, arg...)	\
	printk(level "mc44s803: " format , ## arg)

/* Writes a single register */
static int mc44s803_writereg(struct mc44s803_priv *priv, u32 val)
{
	u8 buf[3];
	struct i2c_msg msg = {
		.addr = priv->cfg->i2c_address, .flags = 0, .buf = buf, .len = 3
	};

	buf[0] = (val & 0xff0000) >> 16;
	buf[1] = (val & 0xff00) >> 8;
	buf[2] = (val & 0xff);

	if (i2c_transfer(priv->i2c, &msg, 1) != 1) {
		mc_printk(KERN_WARNING, "I2C write failed\n");
		return -EREMOTEIO;
	}
	return 0;
}

/* Reads a single register */
static int mc44s803_readreg(struct mc44s803_priv *priv, u8 reg, u32 *val)
{
	u32 wval;
	u8 buf[3];
	int ret;
	struct i2c_msg msg[] = {
		{ .addr = priv->cfg->i2c_address, .flags = I2C_M_RD,
		  .buf = buf, .len = 3 },
	};

	wval = MC44S803_REG_SM(MC44S803_REG_DATAREG, MC44S803_ADDR) |
	       MC44S803_REG_SM(reg, MC44S803_D);

	ret = mc44s803_writereg(priv, wval);
	if (ret)
		return ret;

	if (i2c_transfer(priv->i2c, msg, 1) != 1) {
		mc_printk(KERN_WARNING, "I2C read failed\n");
		return -EREMOTEIO;
	}

	*val = (buf[0] << 16) | (buf[1] << 8) | buf[2];

	return 0;
}

static void mc44s803_release(struct dvb_frontend *fe)
{
	struct mc44s803_priv *priv = fe->tuner_priv;

	fe->tuner_priv = NULL;
	kfree(priv);
}

static int mc44s803_init(struct dvb_frontend *fe)
{
	struct mc44s803_priv *priv = fe->tuner_priv;
	u32 val;
	int err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

/* Reset chip */
	val = MC44S803_REG_SM(MC44S803_REG_RESET, MC44S803_ADDR) |
	      MC44S803_REG_SM(1, MC44S803_RS);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	val = MC44S803_REG_SM(MC44S803_REG_RESET, MC44S803_ADDR);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

/* Power Up and Start Osc */

	val = MC44S803_REG_SM(MC44S803_REG_REFOSC, MC44S803_ADDR) |
	      MC44S803_REG_SM(0xC0, MC44S803_REFOSC) |
	      MC44S803_REG_SM(1, MC44S803_OSCSEL);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	val = MC44S803_REG_SM(MC44S803_REG_POWER, MC44S803_ADDR) |
	      MC44S803_REG_SM(0x200, MC44S803_POWER);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	msleep(10);

	val = MC44S803_REG_SM(MC44S803_REG_REFOSC, MC44S803_ADDR) |
	      MC44S803_REG_SM(0x40, MC44S803_REFOSC) |
	      MC44S803_REG_SM(1, MC44S803_OSCSEL);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	msleep(20);

/* Setup Mixer */

	val = MC44S803_REG_SM(MC44S803_REG_MIXER, MC44S803_ADDR) |
	      MC44S803_REG_SM(1, MC44S803_TRI_STATE) |
	      MC44S803_REG_SM(0x7F, MC44S803_MIXER_RES);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

/* Setup Cirquit Adjust */

	val = MC44S803_REG_SM(MC44S803_REG_CIRCADJ, MC44S803_ADDR) |
	      MC44S803_REG_SM(1, MC44S803_G1) |
	      MC44S803_REG_SM(1, MC44S803_G3) |
	      MC44S803_REG_SM(0x3, MC44S803_CIRCADJ_RES) |
	      MC44S803_REG_SM(1, MC44S803_G6) |
	      MC44S803_REG_SM(priv->cfg->dig_out, MC44S803_S1) |
	      MC44S803_REG_SM(0x3, MC44S803_LP) |
	      MC44S803_REG_SM(1, MC44S803_CLRF) |
	      MC44S803_REG_SM(1, MC44S803_CLIF);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	val = MC44S803_REG_SM(MC44S803_REG_CIRCADJ, MC44S803_ADDR) |
	      MC44S803_REG_SM(1, MC44S803_G1) |
	      MC44S803_REG_SM(1, MC44S803_G3) |
	      MC44S803_REG_SM(0x3, MC44S803_CIRCADJ_RES) |
	      MC44S803_REG_SM(1, MC44S803_G6) |
	      MC44S803_REG_SM(priv->cfg->dig_out, MC44S803_S1) |
	      MC44S803_REG_SM(0x3, MC44S803_LP);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

/* Setup Digtune */

	val = MC44S803_REG_SM(MC44S803_REG_DIGTUNE, MC44S803_ADDR) |
	      MC44S803_REG_SM(3, MC44S803_XOD);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

/* Setup AGC */

	val = MC44S803_REG_SM(MC44S803_REG_LNAAGC, MC44S803_ADDR) |
	      MC44S803_REG_SM(1, MC44S803_AT1) |
	      MC44S803_REG_SM(1, MC44S803_AT2) |
	      MC44S803_REG_SM(1, MC44S803_AGC_AN_DIG) |
	      MC44S803_REG_SM(1, MC44S803_AGC_READ_EN) |
	      MC44S803_REG_SM(1, MC44S803_LNA0);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);
	return 0;

exit:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	mc_printk(KERN_WARNING, "I/O Error\n");
	return err;
}

static int mc44s803_set_params(struct dvb_frontend *fe)
{
	struct mc44s803_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 r1, r2, n1, n2, lo1, lo2, freq, val;
	int err;

	priv->frequency = c->frequency;

	r1 = MC44S803_OSC / 1000000;
	r2 = MC44S803_OSC /  100000;

	n1 = (c->frequency + MC44S803_IF1 + 500000) / 1000000;
	freq = MC44S803_OSC / r1 * n1;
	lo1 = ((60 * n1) + (r1 / 2)) / r1;
	freq = freq - c->frequency;

	n2 = (freq - MC44S803_IF2 + 50000) / 100000;
	lo2 = ((60 * n2) + (r2 / 2)) / r2;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	val = MC44S803_REG_SM(MC44S803_REG_REFDIV, MC44S803_ADDR) |
	      MC44S803_REG_SM(r1-1, MC44S803_R1) |
	      MC44S803_REG_SM(r2-1, MC44S803_R2) |
	      MC44S803_REG_SM(1, MC44S803_REFBUF_EN);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	val = MC44S803_REG_SM(MC44S803_REG_LO1, MC44S803_ADDR) |
	      MC44S803_REG_SM(n1-2, MC44S803_LO1);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	val = MC44S803_REG_SM(MC44S803_REG_LO2, MC44S803_ADDR) |
	      MC44S803_REG_SM(n2-2, MC44S803_LO2);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	val = MC44S803_REG_SM(MC44S803_REG_DIGTUNE, MC44S803_ADDR) |
	      MC44S803_REG_SM(1, MC44S803_DA) |
	      MC44S803_REG_SM(lo1, MC44S803_LO_REF) |
	      MC44S803_REG_SM(1, MC44S803_AT);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	val = MC44S803_REG_SM(MC44S803_REG_DIGTUNE, MC44S803_ADDR) |
	      MC44S803_REG_SM(2, MC44S803_DA) |
	      MC44S803_REG_SM(lo2, MC44S803_LO_REF) |
	      MC44S803_REG_SM(1, MC44S803_AT);

	err = mc44s803_writereg(priv, val);
	if (err)
		goto exit;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;

exit:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	mc_printk(KERN_WARNING, "I/O Error\n");
	return err;
}

static int mc44s803_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct mc44s803_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static int mc44s803_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	*frequency = MC44S803_IF2; /* 36.125 MHz */
	return 0;
}

static const struct dvb_tuner_ops mc44s803_tuner_ops = {
	.info = {
		.name           = "Freescale MC44S803",
		.frequency_min  =   48000000,
		.frequency_max  = 1000000000,
		.frequency_step =     100000,
	},

	.release       = mc44s803_release,
	.init          = mc44s803_init,
	.set_params    = mc44s803_set_params,
	.get_frequency = mc44s803_get_frequency,
	.get_if_frequency = mc44s803_get_if_frequency,
};

/* This functions tries to identify a MC44S803 tuner by reading the ID
   register. This is hasty. */
struct dvb_frontend *mc44s803_attach(struct dvb_frontend *fe,
	 struct i2c_adapter *i2c, struct mc44s803_config *cfg)
{
	struct mc44s803_priv *priv;
	u32 reg;
	u8 id;
	int ret;

	reg = 0;

	priv = kzalloc(sizeof(struct mc44s803_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->cfg = cfg;
	priv->i2c = i2c;
	priv->fe  = fe;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open i2c_gate */

	ret = mc44s803_readreg(priv, MC44S803_REG_ID, &reg);
	if (ret)
		goto error;

	id = MC44S803_REG_MS(reg, MC44S803_ID);

	if (id != 0x14) {
		mc_printk(KERN_ERR, "unsupported ID (%x should be 0x14)\n",
			  id);
		goto error;
	}

	mc_printk(KERN_INFO, "successfully identified (ID = %x)\n", id);
	memcpy(&fe->ops.tuner_ops, &mc44s803_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = priv;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close i2c_gate */

	return fe;

error:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close i2c_gate */

	kfree(priv);
	return NULL;
}
EXPORT_SYMBOL(mc44s803_attach);

MODULE_AUTHOR("Jochen Friedrich");
MODULE_DESCRIPTION("Freescale MC44S803 silicon tuner driver");
MODULE_LICENSE("GPL");
