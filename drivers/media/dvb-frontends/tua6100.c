/*
 * Driver for Infineon tua6100 pll.
 *
 * (c) 2006 Andrew de Quincey
 *
 * Based on code found in budget-av.c, which has the following:
 * Compiled from various sources by Michael Hunold <michael@mihu.de>
 *
 * CI interface support (c) 2004 Olivier Gournet <ogournet@anevia.com> &
 *                               Andrew de Quincey <adq_dvb@lidskialf.net>
 *
 * Copyright (C) 2002 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/dvb/frontend.h>
#include <asm/types.h>

#include "tua6100.h"

struct tua6100_priv {
	/* i2c details */
	int i2c_address;
	struct i2c_adapter *i2c;
	u32 frequency;
};

static void tua6100_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static int tua6100_sleep(struct dvb_frontend *fe)
{
	struct tua6100_priv *priv = fe->tuner_priv;
	int ret;
	u8 reg0[] = { 0x00, 0x00 };
	struct i2c_msg msg = { .addr = priv->i2c_address, .flags = 0, .buf = reg0, .len = 2 };

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if ((ret = i2c_transfer (priv->i2c, &msg, 1)) != 1) {
		printk("%s: i2c error\n", __func__);
	}
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return (ret == 1) ? 0 : ret;
}

static int tua6100_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct tua6100_priv *priv = fe->tuner_priv;
	u32 div;
	u32 prediv;
	u8 reg0[] = { 0x00, 0x00 };
	u8 reg1[] = { 0x01, 0x00, 0x00, 0x00 };
	u8 reg2[] = { 0x02, 0x00, 0x00 };
	struct i2c_msg msg0 = { .addr = priv->i2c_address, .flags = 0, .buf = reg0, .len = 2 };
	struct i2c_msg msg1 = { .addr = priv->i2c_address, .flags = 0, .buf = reg1, .len = 4 };
	struct i2c_msg msg2 = { .addr = priv->i2c_address, .flags = 0, .buf = reg2, .len = 3 };

#define _R 4
#define _P 32
#define _ri 4000000

	// setup register 0
	if (c->frequency < 2000000)
		reg0[1] = 0x03;
	else
		reg0[1] = 0x07;

	// setup register 1
	if (c->frequency < 1630000)
		reg1[1] = 0x2c;
	else
		reg1[1] = 0x0c;

	if (_P == 64)
		reg1[1] |= 0x40;
	if (c->frequency >= 1525000)
		reg1[1] |= 0x80;

	// register 2
	reg2[1] = (_R >> 8) & 0x03;
	reg2[2] = _R;
	if (c->frequency < 1455000)
		reg2[1] |= 0x1c;
	else if (c->frequency < 1630000)
		reg2[1] |= 0x0c;
	else
		reg2[1] |= 0x1c;

	/*
	 * The N divisor ratio (note: c->frequency is in kHz, but we
	 * need it in Hz)
	 */
	prediv = (c->frequency * _R) / (_ri / 1000);
	div = prediv / _P;
	reg1[1] |= (div >> 9) & 0x03;
	reg1[2] = div >> 1;
	reg1[3] = (div << 7);
	priv->frequency = ((div * _P) * (_ri / 1000)) / _R;

	// Finally, calculate and store the value for A
	reg1[3] |= (prediv - (div*_P)) & 0x7f;

#undef _R
#undef _P
#undef _ri

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(priv->i2c, &msg0, 1) != 1)
		return -EIO;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(priv->i2c, &msg2, 1) != 1)
		return -EIO;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(priv->i2c, &msg1, 1) != 1)
		return -EIO;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
}

static int tua6100_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tua6100_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static const struct dvb_tuner_ops tua6100_tuner_ops = {
	.info = {
		.name = "Infineon TUA6100",
		.frequency_min_hz  =  950 * MHz,
		.frequency_max_hz  = 2150 * MHz,
		.frequency_step_hz =    1 * MHz,
	},
	.release = tua6100_release,
	.sleep = tua6100_sleep,
	.set_params = tua6100_set_params,
	.get_frequency = tua6100_get_frequency,
};

struct dvb_frontend *tua6100_attach(struct dvb_frontend *fe, int addr, struct i2c_adapter *i2c)
{
	struct tua6100_priv *priv = NULL;
	u8 b1 [] = { 0x80 };
	u8 b2 [] = { 0x00 };
	struct i2c_msg msg [] = { { .addr = addr, .flags = 0, .buf = b1, .len = 1 },
				  { .addr = addr, .flags = I2C_M_RD, .buf = b2, .len = 1 } };
	int ret;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	ret = i2c_transfer (i2c, msg, 2);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (ret != 2)
		return NULL;

	priv = kzalloc(sizeof(struct tua6100_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_address = addr;
	priv->i2c = i2c;

	memcpy(&fe->ops.tuner_ops, &tua6100_tuner_ops, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = priv;
	return fe;
}
EXPORT_SYMBOL(tua6100_attach);

MODULE_DESCRIPTION("DVB tua6100 driver");
MODULE_AUTHOR("Andrew de Quincey");
MODULE_LICENSE("GPL");
