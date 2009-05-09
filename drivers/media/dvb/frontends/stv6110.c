/*
 * stv6110.c
 *
 * Driver for ST STV6110 satellite tuner IC.
 *
 * Copyright (C) 2009 NetUP Inc.
 * Copyright (C) 2009 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/dvb/frontend.h>

#include <linux/types.h>

#include "stv6110.h"

static int debug;

struct stv6110_priv {
	int i2c_address;
	struct i2c_adapter *i2c;

	u32 mclk;
	u8 regs[8];
};

#define dprintk(args...) \
	do { \
		if (debug) \
			printk(KERN_DEBUG args); \
	} while (0)

static s32 abssub(s32 a, s32 b)
{
	if (a > b)
		return a - b;
	else
		return b - a;
};

static int stv6110_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int stv6110_write_regs(struct dvb_frontend *fe, u8 buf[],
							int start, int len)
{
	struct stv6110_priv *priv = fe->tuner_priv;
	int rc;
	u8 cmdbuf[len + 1];
	struct i2c_msg msg = {
		.addr	= priv->i2c_address,
		.flags	= 0,
		.buf	= cmdbuf,
		.len	= len + 1
	};

	dprintk("%s\n", __func__);

	if (start + len > 8)
		return -EINVAL;

	memcpy(&cmdbuf[1], buf, len);
	cmdbuf[0] = start;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	rc = i2c_transfer(priv->i2c, &msg, 1);
	if (rc != 1)
		dprintk("%s: i2c error\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
}

static int stv6110_read_regs(struct dvb_frontend *fe, u8 regs[],
							int start, int len)
{
	struct stv6110_priv *priv = fe->tuner_priv;
	int rc;
	u8 reg[] = { start };
	struct i2c_msg msg_wr = {
		.addr	= priv->i2c_address,
		.flags	= 0,
		.buf	= reg,
		.len	= 1,
	};

	struct i2c_msg msg_rd = {
		.addr	= priv->i2c_address,
		.flags	= I2C_M_RD,
		.buf	= regs,
		.len	= len,
	};
	/* write subaddr */
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	rc = i2c_transfer(priv->i2c, &msg_wr, 1);
	if (rc != 1)
		dprintk("%s: i2c error\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);
	/* read registers */
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	rc = i2c_transfer(priv->i2c, &msg_rd, 1);
	if (rc != 1)
		dprintk("%s: i2c error\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	memcpy(&priv->regs[start], regs, len);

	return 0;
}

static int stv6110_read_reg(struct dvb_frontend *fe, int start)
{
	u8 buf[] = { 0 };
	stv6110_read_regs(fe, buf, start, 1);

	return buf[0];
}

static int stv6110_sleep(struct dvb_frontend *fe)
{
	u8 reg[] = { 0 };
	stv6110_write_regs(fe, reg, 0, 1);

	return 0;
}

static u32 carrier_width(u32 symbol_rate, fe_rolloff_t rolloff)
{
	u32 rlf;

	switch (rolloff) {
	case ROLLOFF_20:
		rlf = 20;
		break;
	case ROLLOFF_25:
		rlf = 25;
		break;
	default:
		rlf = 35;
		break;
	}

	return symbol_rate  + ((symbol_rate * rlf) / 100);
}

static int stv6110_set_bandwidth(struct dvb_frontend *fe, u32 bandwidth)
{
	struct stv6110_priv *priv = fe->tuner_priv;
	u8 r8, ret = 0x04;
	int i;

	if ((bandwidth / 2) > 36000000) /*BW/2 max=31+5=36 mhz for r8=31*/
		r8 = 31;
	else if ((bandwidth / 2) < 5000000) /* BW/2 min=5Mhz for F=0 */
		r8 = 0;
	else /*if 5 < BW/2 < 36*/
		r8 = (bandwidth / 2) / 1000000 - 5;

	/* ctrl3, RCCLKOFF = 0 Activate the calibration Clock */
	/* ctrl3, CF = r8 Set the LPF value */
	priv->regs[RSTV6110_CTRL3] &= ~((1 << 6) | 0x1f);
	priv->regs[RSTV6110_CTRL3] |= (r8 & 0x1f);
	stv6110_write_regs(fe, &priv->regs[RSTV6110_CTRL3], RSTV6110_CTRL3, 1);
	/* stat1, CALRCSTRT = 1 Start LPF auto calibration*/
	priv->regs[RSTV6110_STAT1] |= 0x02;
	stv6110_write_regs(fe, &priv->regs[RSTV6110_STAT1], RSTV6110_STAT1, 1);

	i = 0;
	/* Wait for CALRCSTRT == 0 */
	while ((i < 10) && (ret != 0)) {
		ret = ((stv6110_read_reg(fe, RSTV6110_STAT1)) & 0x02);
		mdelay(1);	/* wait for LPF auto calibration */
		i++;
	}

	/* RCCLKOFF = 1 calibration done, desactivate the calibration Clock */
	priv->regs[RSTV6110_CTRL3] |= (1 << 6);
	stv6110_write_regs(fe, &priv->regs[RSTV6110_CTRL3], RSTV6110_CTRL3, 1);
	return 0;
}

static int stv6110_init(struct dvb_frontend *fe)
{
	struct stv6110_priv *priv = fe->tuner_priv;
	u8 buf0[] = { 0x07, 0x11, 0xdc, 0x85, 0x17, 0x01, 0xe6, 0x1e };

	memcpy(priv->regs, buf0, 8);
	/* K = (Reference / 1000000) - 16 */
	priv->regs[RSTV6110_CTRL1] &= ~(0x1f << 3);
	priv->regs[RSTV6110_CTRL1] |=
				((((priv->mclk / 1000000) - 16) & 0x1f) << 3);

	stv6110_write_regs(fe, &priv->regs[RSTV6110_CTRL1], RSTV6110_CTRL1, 8);
	msleep(1);
	stv6110_set_bandwidth(fe, 72000000);

	return 0;
}

static int stv6110_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct stv6110_priv *priv = fe->tuner_priv;
	u32 nbsteps, divider, psd2, freq;
	u8 regs[] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	stv6110_read_regs(fe, regs, 0, 8);
	/*N*/
	divider = (priv->regs[RSTV6110_TUNING2] & 0x0f) << 8;
	divider += priv->regs[RSTV6110_TUNING1];

	/*R*/
	nbsteps  = (priv->regs[RSTV6110_TUNING2] >> 6) & 3;
	/*p*/
	psd2  = (priv->regs[RSTV6110_TUNING2] >> 4) & 1;

	freq = divider * (priv->mclk / 1000);
	freq /= (1 << (nbsteps + psd2));
	freq /= 4;

	*frequency = freq;

	return 0;
}

static int stv6110_set_frequency(struct dvb_frontend *fe, u32 frequency)
{
	struct stv6110_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 ret = 0x04;
	u32 divider, ref, p, presc, i, result_freq, vco_freq;
	s32 p_calc, p_calc_opt = 1000, r_div, r_div_opt = 0, p_val;
	s32 srate; u8 gain;

	dprintk("%s, freq=%d kHz, mclk=%d Hz\n", __func__,
						frequency, priv->mclk);

	/* K = (Reference / 1000000) - 16 */
	priv->regs[RSTV6110_CTRL1] &= ~(0x1f << 3);
	priv->regs[RSTV6110_CTRL1] |=
				((((priv->mclk / 1000000) - 16) & 0x1f) << 3);

	/* BB_GAIN = db/2 */
	if (fe->ops.set_property && fe->ops.get_property) {
		srate = c->symbol_rate;
		dprintk("%s: Get Frontend parameters: srate=%d\n",
							__func__, srate);
	} else
		srate = 15000000;

	if (srate >= 15000000)
		gain = 3; /* +6 dB */
	else if (srate >= 5000000)
		gain = 3; /* +6 dB */
	else
		gain = 3; /* +6 dB */

	priv->regs[RSTV6110_CTRL2] &= ~0x0f;
	priv->regs[RSTV6110_CTRL2] |= (gain & 0x0f);

	if (frequency <= 1023000) {
		p = 1;
		presc = 0;
	} else if (frequency <= 1300000) {
		p = 1;
		presc = 1;
	} else if (frequency <= 2046000) {
		p = 0;
		presc = 0;
	} else {
		p = 0;
		presc = 1;
	}
	/* DIV4SEL = p*/
	priv->regs[RSTV6110_TUNING2] &= ~(1 << 4);
	priv->regs[RSTV6110_TUNING2] |= (p << 4);

	/* PRESC32ON = presc */
	priv->regs[RSTV6110_TUNING2] &= ~(1 << 5);
	priv->regs[RSTV6110_TUNING2] |= (presc << 5);

	p_val = (int)(1 << (p + 1)) * 10;/* P = 2 or P = 4 */
	for (r_div = 0; r_div <= 3; r_div++) {
		p_calc = (priv->mclk / 100000);
		p_calc /= (1 << (r_div + 1));
		if ((abssub(p_calc, p_val)) < (abssub(p_calc_opt, p_val)))
			r_div_opt = r_div;

		p_calc_opt = (priv->mclk / 100000);
		p_calc_opt /= (1 << (r_div_opt + 1));
	}

	ref = priv->mclk / ((1 << (r_div_opt + 1))  * (1 << (p + 1)));
	divider = (((frequency * 1000) + (ref >> 1)) / ref);

	/* RDIV = r_div_opt */
	priv->regs[RSTV6110_TUNING2] &= ~(3 << 6);
	priv->regs[RSTV6110_TUNING2] |= (((r_div_opt) & 3) << 6);

	/* NDIV_MSB = MSB(divider) */
	priv->regs[RSTV6110_TUNING2] &= ~0x0f;
	priv->regs[RSTV6110_TUNING2] |= (((divider) >> 8) & 0x0f);

	/* NDIV_LSB, LSB(divider) */
	priv->regs[RSTV6110_TUNING1] = (divider & 0xff);

	/* CALVCOSTRT = 1 VCO Auto Calibration */
	priv->regs[RSTV6110_STAT1] |= 0x04;
	stv6110_write_regs(fe, &priv->regs[RSTV6110_CTRL1],
						RSTV6110_CTRL1, 8);

	i = 0;
	/* Wait for CALVCOSTRT == 0 */
	while ((i < 10) && (ret != 0)) {
		ret = ((stv6110_read_reg(fe, RSTV6110_STAT1)) & 0x04);
		msleep(1); /* wait for VCO auto calibration */
		i++;
	}

	ret = stv6110_read_reg(fe, RSTV6110_STAT1);
	stv6110_get_frequency(fe, &result_freq);

	vco_freq = divider * ((priv->mclk / 1000) / ((1 << (r_div_opt + 1))));
	dprintk("%s, stat1=%x, lo_freq=%d kHz, vco_frec=%d kHz\n", __func__,
						ret, result_freq, vco_freq);

	return 0;
}

static int stv6110_set_params(struct dvb_frontend *fe,
			      struct dvb_frontend_parameters *params)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 bandwidth = carrier_width(c->symbol_rate, c->rolloff);

	stv6110_set_frequency(fe, c->frequency);
	stv6110_set_bandwidth(fe, bandwidth);

	return 0;
}

static int stv6110_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct stv6110_priv *priv = fe->tuner_priv;
	u8 r8 = 0;
	u8 regs[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	stv6110_read_regs(fe, regs, 0, 8);

	/* CF */
	r8 = priv->regs[RSTV6110_CTRL3] & 0x1f;
	*bandwidth = (r8 + 5) * 2000000;/* x2 for ZIF tuner BW/2 = F+5 Mhz */

	return 0;
}

static struct dvb_tuner_ops stv6110_tuner_ops = {
	.info = {
		.name = "ST STV6110",
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_step = 1000,
	},
	.init = stv6110_init,
	.release = stv6110_release,
	.sleep = stv6110_sleep,
	.set_params = stv6110_set_params,
	.get_frequency = stv6110_get_frequency,
	.set_frequency = stv6110_set_frequency,
	.get_bandwidth = stv6110_get_bandwidth,
	.set_bandwidth = stv6110_set_bandwidth,

};

struct dvb_frontend *stv6110_attach(struct dvb_frontend *fe,
					const struct stv6110_config *config,
					struct i2c_adapter *i2c)
{
	struct stv6110_priv *priv = NULL;
	u8 reg0[] = { 0x00, 0x07, 0x11, 0xdc, 0x85, 0x17, 0x01, 0xe6, 0x1e };

	struct i2c_msg msg[] = {
		{
			.addr = config->i2c_address,
			.flags = 0,
			.buf = reg0,
			.len = 9
		}
	};
	int ret;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = i2c_transfer(i2c, msg, 1);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (ret != 1)
		return NULL;

	priv = kzalloc(sizeof(struct stv6110_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_address = config->i2c_address;
	priv->i2c = i2c;
	priv->mclk = config->mclk;

	memcpy(&priv->regs, &reg0[1], 8);

	memcpy(&fe->ops.tuner_ops, &stv6110_tuner_ops,
				sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = priv;
	printk(KERN_INFO "STV6110 attached on addr=%x!\n", priv->i2c_address);

	return fe;
}
EXPORT_SYMBOL(stv6110_attach);

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("ST STV6110 driver");
MODULE_AUTHOR("Igor M. Liplianin");
MODULE_LICENSE("GPL");
