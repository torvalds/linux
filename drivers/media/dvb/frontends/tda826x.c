  /*
     Driver for Philips tda8262/tda8263 DVBS Silicon tuners

     (c) 2006 Andrew de Quincey

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 2 of the License, or
     (at your option) any later version.

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the

     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

  */

#include <linux/module.h>
#include <linux/dvb/frontend.h>
#include <asm/types.h>

#include "tda826x.h"

static int debug = 0;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "tda826x: " args); \
	} while (0)

struct tda826x_priv {
	/* i2c details */
	int i2c_address;
	struct i2c_adapter *i2c;
	u8 has_loopthrough:1;
	u32 frequency;
};

static int tda826x_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int tda826x_sleep(struct dvb_frontend *fe)
{
	struct tda826x_priv *priv = fe->tuner_priv;
	int ret;
	u8 buf [] = { 0x00, 0x8d };
	struct i2c_msg msg = { .addr = priv->i2c_address, .flags = 0, .buf = buf, .len = 2 };

	dprintk("%s:\n", __FUNCTION__);

	if (!priv->has_loopthrough)
		buf[1] = 0xad;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if ((ret = i2c_transfer (priv->i2c, &msg, 1)) != 1) {
		dprintk("%s: i2c error\n", __FUNCTION__);
	}
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return (ret == 1) ? 0 : ret;
}

static int tda826x_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct tda826x_priv *priv = fe->tuner_priv;
	int ret;
	u32 div;
	u8 buf [11];
	struct i2c_msg msg = { .addr = priv->i2c_address, .flags = 0, .buf = buf, .len = 11 };

	dprintk("%s:\n", __FUNCTION__);

	div = (params->frequency + (1000-1)) / 1000;

	buf[0] = 0x00; // subaddress
	buf[1] = 0x09; // powerdown RSSI + the magic value 1
	if (!priv->has_loopthrough)
		buf[1] |= 0x20; // power down loopthrough if not needed
	buf[2] = (1<<5) | 0x0b; // 1Mhz + 0.45 VCO
	buf[3] = div >> 7;
	buf[4] = div << 1;
	buf[5] = 0xff; // basedband filter to max
	buf[6] = 0xfe; // gains at max + no RF attenuation
	buf[7] = 0x83; // charge pumps at high, tests off
	buf[8] = 0x80; // recommended value 4 for AMPVCO + disable ports.
	buf[9] = 0x1a; // normal caltime + recommended values for SELTH + SELVTL
	buf[10] = 0xd4; // recommended value 13 for BBIAS + unknown bit set on

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if ((ret = i2c_transfer (priv->i2c, &msg, 1)) != 1) {
		dprintk("%s: i2c error\n", __FUNCTION__);
	}
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	priv->frequency = div * 1000;

	return (ret == 1) ? 0 : ret;
}

static int tda826x_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tda826x_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static struct dvb_tuner_ops tda826x_tuner_ops = {
	.info = {
		.name = "Philips TDA826X",
		.frequency_min = 950000,
		.frequency_max = 2175000
	},
	.release = tda826x_release,
	.sleep = tda826x_sleep,
	.set_params = tda826x_set_params,
	.get_frequency = tda826x_get_frequency,
};

struct dvb_frontend *tda826x_attach(struct dvb_frontend *fe, int addr, struct i2c_adapter *i2c, int has_loopthrough)
{
	struct tda826x_priv *priv = NULL;
	u8 b1 [] = { 0, 0 };
	struct i2c_msg msg[2] = {
		{ .addr = addr, .flags = 0,        .buf = NULL, .len = 0 },
		{ .addr = addr, .flags = I2C_M_RD, .buf = b1, .len = 2 }
	};
	int ret;

	dprintk("%s:\n", __FUNCTION__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	ret = i2c_transfer (i2c, msg, 2);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (ret != 2)
		return NULL;
	if (!(b1[1] & 0x80))
		return NULL;

	priv = kzalloc(sizeof(struct tda826x_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_address = addr;
	priv->i2c = i2c;
	priv->has_loopthrough = has_loopthrough;

	memcpy(&fe->ops.tuner_ops, &tda826x_tuner_ops, sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = priv;

	return fe;
}
EXPORT_SYMBOL(tda826x_attach);

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("DVB TDA826x driver");
MODULE_AUTHOR("Andrew de Quincey");
MODULE_LICENSE("GPL");
