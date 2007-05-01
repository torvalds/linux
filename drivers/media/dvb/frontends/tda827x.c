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
#include <linux/dvb/frontend.h>
#include <asm/types.h>

#include "tda827x.h"

static int debug = 0;
#define dprintk(args...) \
	do {					    \
		if (debug) printk(KERN_DEBUG "tda827x: " args); \
	} while (0)

struct tda827x_priv {
	int i2c_addr;
	struct i2c_adapter *i2c_adap;
	struct tda827x_config *cfg;
	u32 frequency;
	u32 bandwidth;
};

struct tda827x_data {
	u32 lomax;
	u8  spd;
	u8  bs;
	u8  bp;
	u8  cp;
	u8  gc3;
	u8 div1p5;
};

static const struct tda827x_data tda827x_dvbt[] = {
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

static int tda827xo_set_params(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *params)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	u8 buf[14];

	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };
	int i, tuner_freq, if_freq;
	u32 N;

	dprintk("%s:\n", __FUNCTION__);
	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		if_freq = 4000000;
		break;
	case BANDWIDTH_7_MHZ:
		if_freq = 4500000;
		break;
	default:		   /* 8 MHz or Auto */
		if_freq = 5000000;
		break;
	}
	tuner_freq = params->frequency + if_freq;

	i = 0;
	while (tda827x_dvbt[i].lomax < tuner_freq) {
		if(tda827x_dvbt[i + 1].lomax == 0)
			break;
		i++;
	}

	N = ((tuner_freq + 125000) / 250000) << (tda827x_dvbt[i].spd + 2);
	buf[0] = 0;
	buf[1] = (N>>8) | 0x40;
	buf[2] = N & 0xff;
	buf[3] = 0;
	buf[4] = 0x52;
	buf[5] = (tda827x_dvbt[i].spd << 6) + (tda827x_dvbt[i].div1p5 << 5) +
				(tda827x_dvbt[i].bs << 3) + tda827x_dvbt[i].bp;
	buf[6] = (tda827x_dvbt[i].gc3 << 4) + 0x8f;
	buf[7] = 0xbf;
	buf[8] = 0x2a;
	buf[9] = 0x05;
	buf[10] = 0xff;
	buf[11] = 0x00;
	buf[12] = 0x00;
	buf[13] = 0x40;

	msg.len = 14;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(priv->i2c_adap, &msg, 1) != 1) {
		printk("%s: could not write to tuner at addr: 0x%02x\n",
		       __FUNCTION__, priv->i2c_addr << 1);
		return -EIO;
	}
	msleep(500);
	/* correct CP value */
	buf[0] = 0x30;
	buf[1] = 0x50 + tda827x_dvbt[i].cp;
	msg.len = 2;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);

	priv->frequency = tuner_freq - if_freq; // FIXME
	priv->bandwidth = (fe->ops.info.type == FE_OFDM) ? params->u.ofdm.bandwidth : 0;

	return 0;
}

static int tda827xo_sleep(struct dvb_frontend *fe)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	static u8 buf[] = { 0x30, 0xd0 };
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };

	dprintk("%s:\n", __FUNCTION__);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);

	if (priv->cfg && priv->cfg->sleep)
		priv->cfg->sleep(fe);

	return 0;
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

static const struct tda827xa_data tda827xa_dvbt[] = {
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

static int tda827xa_set_params(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *params)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	u8 buf[11];

	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };

	int i, tuner_freq, if_freq;
	u32 N;

	dprintk("%s:\n", __FUNCTION__);
	if (priv->cfg && priv->cfg->lna_gain)
		priv->cfg->lna_gain(fe, 1);
	msleep(20);

	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		if_freq = 4000000;
		break;
	case BANDWIDTH_7_MHZ:
		if_freq = 4500000;
		break;
	default:		   /* 8 MHz or Auto */
		if_freq = 5000000;
		break;
	}
	tuner_freq = params->frequency + if_freq;

	i = 0;
	while (tda827xa_dvbt[i].lomax < tuner_freq) {
		if(tda827xa_dvbt[i + 1].lomax == 0)
			break;
		i++;
	}

	N = ((tuner_freq + 31250) / 62500) << tda827xa_dvbt[i].spd;
	buf[0] = 0;            // subaddress
	buf[1] = N >> 8;
	buf[2] = N & 0xff;
	buf[3] = 0;
	buf[4] = 0x16;
	buf[5] = (tda827xa_dvbt[i].spd << 5) + (tda827xa_dvbt[i].svco << 3) +
			tda827xa_dvbt[i].sbs;
	buf[6] = 0x4b + (tda827xa_dvbt[i].gc3 << 4);
	buf[7] = 0x1c;
	buf[8] = 0x06;
	buf[9] = 0x24;
	buf[10] = 0x00;
	msg.len = 11;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(priv->i2c_adap, &msg, 1) != 1) {
		printk("%s: could not write to tuner at addr: 0x%02x\n",
		       __FUNCTION__, priv->i2c_addr << 1);
		return -EIO;
	}
	buf[0] = 0x90;
	buf[1] = 0xff;
	buf[2] = 0x60;
	buf[3] = 0x00;
	buf[4] = 0x59;  // lpsel, for 6MHz + 2
	msg.len = 5;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);

	buf[0] = 0xa0;
	buf[1] = 0x40;
	msg.len = 2;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);

	msleep(11);
	msg.flags = I2C_M_RD;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);
	msg.flags = 0;

	buf[1] >>= 4;
	dprintk("tda8275a AGC2 gain is: %d\n", buf[1]);
	if ((buf[1]) < 2) {
		if (priv->cfg && priv->cfg->lna_gain)
			priv->cfg->lna_gain(fe, 0);
		buf[0] = 0x60;
		buf[1] = 0x0c;
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		i2c_transfer(priv->i2c_adap, &msg, 1);
	}

	buf[0] = 0xc0;
	buf[1] = 0x99;    // lpsel, for 6MHz + 2
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);

	buf[0] = 0x60;
	buf[1] = 0x3c;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);

	/* correct CP value */
	buf[0] = 0x30;
	buf[1] = 0x10 + tda827xa_dvbt[i].scr;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);

	msleep(163);
	buf[0] = 0xc0;
	buf[1] = 0x39;  // lpsel, for 6MHz + 2
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);

	msleep(3);
	/* freeze AGC1 */
	buf[0] = 0x50;
	buf[1] = 0x4f + (tda827xa_dvbt[i].gc3 << 4);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(priv->i2c_adap, &msg, 1);

	priv->frequency = tuner_freq - if_freq; // FIXME
	priv->bandwidth = (fe->ops.info.type == FE_OFDM) ? params->u.ofdm.bandwidth : 0;

	return 0;
}

static int tda827xa_sleep(struct dvb_frontend *fe)
{
	struct tda827x_priv *priv = fe->tuner_priv;
	static u8 buf[] = { 0x30, 0x90 };
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };

	dprintk("%s:\n", __FUNCTION__);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	i2c_transfer(priv->i2c_adap, &msg, 1);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (priv->cfg && priv->cfg->sleep)
		priv->cfg->sleep(fe);

	return 0;
}

static int tda827x_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
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
	dprintk("%s:\n", __FUNCTION__);
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

static struct dvb_tuner_ops tda827xo_tuner_ops = {
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
	.get_frequency = tda827x_get_frequency,
	.get_bandwidth = tda827x_get_bandwidth,
};

static struct dvb_tuner_ops tda827xa_tuner_ops = {
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
	.get_frequency = tda827x_get_frequency,
	.get_bandwidth = tda827x_get_bandwidth,
};

static int tda827x_probe_version(struct dvb_frontend *fe)
{	u8 data;
	struct tda827x_priv *priv = fe->tuner_priv;
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = I2C_M_RD,
			       .buf = &data, .len = 1 };
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(priv->i2c_adap, &msg, 1) != 1) {
		printk("%s: could not read from tuner at addr: 0x%02x\n",
		       __FUNCTION__, msg.addr << 1);
		return -EIO;
	}
	if ((data & 0x3c) == 0) {
		dprintk("tda827x tuner found\n");
		fe->ops.tuner_ops.init  = tda827x_init;
		fe->ops.tuner_ops.sleep = tda827xo_sleep;
	} else {
		dprintk("tda827xa tuner found\n");
		memcpy(&fe->ops.tuner_ops, &tda827xa_tuner_ops, sizeof(struct dvb_tuner_ops));
	}
	return 0;
}

struct dvb_frontend *tda827x_attach(struct dvb_frontend *fe, int addr,
				    struct i2c_adapter *i2c,
				    struct tda827x_config *cfg)
{
	struct tda827x_priv *priv = NULL;

	dprintk("%s:\n", __FUNCTION__);
	priv = kzalloc(sizeof(struct tda827x_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_addr = addr;
	priv->i2c_adap = i2c;
	priv->cfg = cfg;
	memcpy(&fe->ops.tuner_ops, &tda827xo_tuner_ops, sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = priv;

	return fe;
}

EXPORT_SYMBOL(tda827x_attach);

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("DVB TDA827x driver");
MODULE_AUTHOR("Hartmut Hackmann <hartmut.hackmann@t-online.de>");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_LICENSE("GPL");

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
