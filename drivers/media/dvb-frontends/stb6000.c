// SPDX-License-Identifier: GPL-2.0-or-later
  /*
     Driver for ST STB6000 DVBS Silicon tuner

     Copyright (C) 2008 Igor M. Liplianin (liplianin@me.by)


  */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/dvb/frontend.h>
#include <asm/types.h>

#include "stb6000.h"

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) \
			printk(KERN_DEBUG "stb6000: " args); \
	} while (0)

struct stb6000_priv {
	/* i2c details */
	int i2c_address;
	struct i2c_adapter *i2c;
	u32 frequency;
};

static void stb6000_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static int stb6000_sleep(struct dvb_frontend *fe)
{
	struct stb6000_priv *priv = fe->tuner_priv;
	int ret;
	u8 buf[] = { 10, 0 };
	struct i2c_msg msg = {
		.addr = priv->i2c_address,
		.flags = 0,
		.buf = buf,
		.len = 2
	};

	dprintk("%s:\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = i2c_transfer(priv->i2c, &msg, 1);
	if (ret != 1)
		dprintk("%s: i2c error\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return (ret == 1) ? 0 : ret;
}

static int stb6000_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stb6000_priv *priv = fe->tuner_priv;
	unsigned int n, m;
	int ret;
	u32 freq_mhz;
	int bandwidth;
	u8 buf[12];
	struct i2c_msg msg = {
		.addr = priv->i2c_address,
		.flags = 0,
		.buf = buf,
		.len = 12
	};

	dprintk("%s:\n", __func__);

	freq_mhz = p->frequency / 1000;
	bandwidth = p->symbol_rate / 1000000;

	if (bandwidth > 31)
		bandwidth = 31;

	if ((freq_mhz > 949) && (freq_mhz < 2151)) {
		buf[0] = 0x01;
		buf[1] = 0xac;
		if (freq_mhz < 1950)
			buf[1] = 0xaa;
		if (freq_mhz < 1800)
			buf[1] = 0xa8;
		if (freq_mhz < 1650)
			buf[1] = 0xa6;
		if (freq_mhz < 1530)
			buf[1] = 0xa5;
		if (freq_mhz < 1470)
			buf[1] = 0xa4;
		if (freq_mhz < 1370)
			buf[1] = 0xa2;
		if (freq_mhz < 1300)
			buf[1] = 0xa1;
		if (freq_mhz < 1200)
			buf[1] = 0xa0;
		if (freq_mhz < 1075)
			buf[1] = 0xbc;
		if (freq_mhz < 1000)
			buf[1] = 0xba;
		if (freq_mhz < 1075) {
			n = freq_mhz / 8; /* vco=lo*4 */
			m = 2;
		} else {
			n = freq_mhz / 16; /* vco=lo*2 */
			m = 1;
		}
		buf[2] = n >> 1;
		buf[3] = (unsigned char)(((n & 1) << 7) |
					(m * freq_mhz - n * 16) | 0x60);
		buf[4] = 0x04;
		buf[5] = 0x0e;

		buf[6] = (unsigned char)(bandwidth);

		buf[7] = 0xd8;
		buf[8] = 0xd0;
		buf[9] = 0x50;
		buf[10] = 0xeb;
		buf[11] = 0x4f;

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		ret = i2c_transfer(priv->i2c, &msg, 1);
		if (ret != 1)
			dprintk("%s: i2c error\n", __func__);

		udelay(10);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);

		buf[0] = 0x07;
		buf[1] = 0xdf;
		buf[2] = 0xd0;
		buf[3] = 0x50;
		buf[4] = 0xfb;
		msg.len = 5;

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		ret = i2c_transfer(priv->i2c, &msg, 1);
		if (ret != 1)
			dprintk("%s: i2c error\n", __func__);

		udelay(10);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);

		priv->frequency = freq_mhz * 1000;

		return (ret == 1) ? 0 : ret;
	}
	return -1;
}

static int stb6000_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct stb6000_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static const struct dvb_tuner_ops stb6000_tuner_ops = {
	.info = {
		.name = "ST STB6000",
		.frequency_min_hz =  950 * MHz,
		.frequency_max_hz = 2150 * MHz
	},
	.release = stb6000_release,
	.sleep = stb6000_sleep,
	.set_params = stb6000_set_params,
	.get_frequency = stb6000_get_frequency,
};

struct dvb_frontend *stb6000_attach(struct dvb_frontend *fe, int addr,
						struct i2c_adapter *i2c)
{
	struct stb6000_priv *priv = NULL;
	u8 b0[] = { 0 };
	u8 b1[] = { 0, 0 };
	struct i2c_msg msg[2] = {
		{
			.addr = addr,
			.flags = 0,
			.buf = b0,
			.len = 0
		}, {
			.addr = addr,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 2
		}
	};
	int ret;

	dprintk("%s:\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* is some i2c device here ? */
	ret = i2c_transfer(i2c, msg, 2);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (ret != 2)
		return NULL;

	priv = kzalloc(sizeof(struct stb6000_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_address = addr;
	priv->i2c = i2c;

	memcpy(&fe->ops.tuner_ops, &stb6000_tuner_ops,
				sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = priv;

	return fe;
}
EXPORT_SYMBOL(stb6000_attach);

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("DVB STB6000 driver");
MODULE_AUTHOR("Igor M. Liplianin <liplianin@me.by>");
MODULE_LICENSE("GPL");
