/*
 * NXP TDA18212HN silicon tuner driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "tda18212.h"

struct tda18212_priv {
	struct tda18212_config *cfg;
	struct i2c_adapter *i2c;
};

#define dbg(fmt, arg...)					\
do {								\
	if (debug)						\
		pr_info("%s: " fmt, __func__, ##arg);		\
} while (0)

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

/* write multiple registers */
static int tda18212_wr_regs(struct tda18212_priv *priv, u8 reg, u8 *val,
	int len)
{
	int ret;
	u8 buf[len+1];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->cfg->i2c_address,
			.flags = 0,
			.len = sizeof(buf),
			.buf = buf,
		}
	};

	buf[0] = reg;
	memcpy(&buf[1], val, len);

	ret = i2c_transfer(priv->i2c, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		pr_warn("i2c wr failed ret:%d reg:%02x len:%d\n",
			ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* read multiple registers */
static int tda18212_rd_regs(struct tda18212_priv *priv, u8 reg, u8 *val,
	int len)
{
	int ret;
	u8 buf[len];
	struct i2c_msg msg[2] = {
		{
			.addr = priv->cfg->i2c_address,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = priv->cfg->i2c_address,
			.flags = I2C_M_RD,
			.len = sizeof(buf),
			.buf = buf,
		}
	};

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret == 2) {
		memcpy(val, buf, len);
		ret = 0;
	} else {
		pr_warn("i2c rd failed ret:%d reg:%02x len:%d\n",
			ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* write single register */
static int tda18212_wr_reg(struct tda18212_priv *priv, u8 reg, u8 val)
{
	return tda18212_wr_regs(priv, reg, &val, 1);
}

/* read single register */
static int tda18212_rd_reg(struct tda18212_priv *priv, u8 reg, u8 *val)
{
	return tda18212_rd_regs(priv, reg, val, 1);
}

#if 0 /* keep, useful when developing driver */
static void tda18212_dump_regs(struct tda18212_priv *priv)
{
	int i;
	u8 buf[256];

	#define TDA18212_RD_LEN 32
	for (i = 0; i < sizeof(buf); i += TDA18212_RD_LEN)
		tda18212_rd_regs(priv, i, &buf[i], TDA18212_RD_LEN);

	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 32, 1, buf,
		sizeof(buf), true);

	return;
}
#endif

static int tda18212_set_params(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *p)
{
	struct tda18212_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u32 if_khz;
	u8 buf[9];
	static const u8 bw_params[][3] = {
		/*  0f    13    23 */
		{ 0xb3, 0x20, 0x03 }, /* DVB-T 6 MHz */
		{ 0xb3, 0x31, 0x01 }, /* DVB-T 7 MHz */
		{ 0xb3, 0x22, 0x01 }, /* DVB-T 8 MHz */
		{ 0x92, 0x53, 0x03 }, /* DVB-C */
	};

	dbg("delsys=%d RF=%d BW=%d\n",
	    c->delivery_system, c->frequency, c->bandwidth_hz);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	switch (c->delivery_system) {
	case SYS_DVBT:
		switch (c->bandwidth_hz) {
		case 6000000:
			if_khz = priv->cfg->if_dvbt_6;
			i = 0;
			break;
		case 7000000:
			if_khz = priv->cfg->if_dvbt_7;
			i = 1;
			break;
		case 8000000:
			if_khz = priv->cfg->if_dvbt_8;
			i = 2;
			break;
		default:
			ret = -EINVAL;
			goto error;
		}
		break;
	case SYS_DVBC_ANNEX_AC:
		if_khz = priv->cfg->if_dvbc;
		i = 3;
		break;
	default:
		ret = -EINVAL;
		goto error;
	}

	ret = tda18212_wr_reg(priv, 0x23, bw_params[i][2]);
	if (ret)
		goto error;

	ret = tda18212_wr_reg(priv, 0x06, 0x00);
	if (ret)
		goto error;

	ret = tda18212_wr_reg(priv, 0x0f, bw_params[i][0]);
	if (ret)
		goto error;

	buf[0] = 0x02;
	buf[1] = bw_params[i][1];
	buf[2] = 0x03; /* default value */
	buf[3] = if_khz / 50;
	buf[4] = ((c->frequency / 1000) >> 16) & 0xff;
	buf[5] = ((c->frequency / 1000) >>  8) & 0xff;
	buf[6] = ((c->frequency / 1000) >>  0) & 0xff;
	buf[7] = 0xc1;
	buf[8] = 0x01;
	ret = tda18212_wr_regs(priv, 0x12, buf, sizeof(buf));
	if (ret)
		goto error;

exit:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	return ret;

error:
	dbg("failed:%d\n", ret);
	goto exit;
}

static int tda18212_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static const struct dvb_tuner_ops tda18212_tuner_ops = {
	.info = {
		.name           = "NXP TDA18212",

		.frequency_min  =  48000000,
		.frequency_max  = 864000000,
		.frequency_step =      1000,
	},

	.release       = tda18212_release,

	.set_params    = tda18212_set_params,
};

struct dvb_frontend *tda18212_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, struct tda18212_config *cfg)
{
	struct tda18212_priv *priv = NULL;
	int ret;
	u8 val;

	priv = kzalloc(sizeof(struct tda18212_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->cfg = cfg;
	priv->i2c = i2c;
	fe->tuner_priv = priv;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	/* check if the tuner is there */
	ret = tda18212_rd_reg(priv, 0x00, &val);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	dbg("ret:%d chip ID:%02x\n", ret, val);
	if (ret || val != 0xc7) {
		kfree(priv);
		return NULL;
	}

	pr_info("NXP TDA18212HN successfully identified\n");

	memcpy(&fe->ops.tuner_ops, &tda18212_tuner_ops,
		sizeof(struct dvb_tuner_ops));

	return fe;
}
EXPORT_SYMBOL(tda18212_attach);

MODULE_DESCRIPTION("NXP TDA18212HN silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
