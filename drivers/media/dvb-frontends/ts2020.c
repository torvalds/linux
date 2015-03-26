/*
    Montage Technology TS2020 - Silicon Tuner driver
    Copyright (C) 2009-2012 Konstantin Dimitrov <kosio.dimitrov@gmail.com>

    Copyright (C) 2009-2012 TurboSight.com

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

#include "dvb_frontend.h"
#include "ts2020.h"

#define TS2020_XTAL_FREQ   27000 /* in kHz */
#define FREQ_OFFSET_LOW_SYM_RATE 3000

struct ts2020_priv {
	struct dvb_frontend *fe;
	/* i2c details */
	int i2c_address;
	struct i2c_adapter *i2c;
	u8 clk_out:2;
	u8 clk_out_div:5;
	u32 frequency_div; /* LO output divider switch frequency */
	u32 frequency_khz; /* actual used LO frequency */
#define TS2020_M88TS2020 0
#define TS2020_M88TS2022 1
	u8 tuner;
	u8 loop_through:1;
};

struct ts2020_reg_val {
	u8 reg;
	u8 val;
};

static int ts2020_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int ts2020_writereg(struct dvb_frontend *fe, int reg, int data)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	u8 buf[] = { reg, data };
	struct i2c_msg msg[] = {
		{
			.addr = priv->i2c_address,
			.flags = 0,
			.buf = buf,
			.len = 2
		}
	};
	int err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	err = i2c_transfer(priv->i2c, msg, 1);
	if (err != 1) {
		printk(KERN_ERR
		       "%s: writereg error(err == %i, reg == 0x%02x, value == 0x%02x)\n",
		       __func__, err, reg, data);
		return -EREMOTEIO;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
}

static int ts2020_readreg(struct dvb_frontend *fe, u8 reg)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{
			.addr = priv->i2c_address,
			.flags = 0,
			.buf = b0,
			.len = 1
		}, {
			.addr = priv->i2c_address,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = i2c_transfer(priv->i2c, msg, 2);

	if (ret != 2) {
		printk(KERN_ERR "%s: reg=0x%x(error=%d)\n",
		       __func__, reg, ret);
		return ret;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return b1[0];
}

static int ts2020_sleep(struct dvb_frontend *fe)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	u8 u8tmp;

	if (priv->tuner == TS2020_M88TS2020)
		u8tmp = 0x0a; /* XXX: probably wrong */
	else
		u8tmp = 0x00;

	return ts2020_writereg(fe, u8tmp, 0x00);
}

static int ts2020_init(struct dvb_frontend *fe)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	int i;
	u8 u8tmp;

	if (priv->tuner == TS2020_M88TS2020) {
		ts2020_writereg(fe, 0x42, 0x73);
		ts2020_writereg(fe, 0x05, priv->clk_out_div);
		ts2020_writereg(fe, 0x20, 0x27);
		ts2020_writereg(fe, 0x07, 0x02);
		ts2020_writereg(fe, 0x11, 0xff);
		ts2020_writereg(fe, 0x60, 0xf9);
		ts2020_writereg(fe, 0x08, 0x01);
		ts2020_writereg(fe, 0x00, 0x41);
	} else {
		static const struct ts2020_reg_val reg_vals[] = {
			{0x7d, 0x9d},
			{0x7c, 0x9a},
			{0x7a, 0x76},
			{0x3b, 0x01},
			{0x63, 0x88},
			{0x61, 0x85},
			{0x22, 0x30},
			{0x30, 0x40},
			{0x20, 0x23},
			{0x24, 0x02},
			{0x12, 0xa0},
		};

		ts2020_writereg(fe, 0x00, 0x01);
		ts2020_writereg(fe, 0x00, 0x03);

		switch (priv->clk_out) {
		case TS2020_CLK_OUT_DISABLED:
			u8tmp = 0x60;
			break;
		case TS2020_CLK_OUT_ENABLED:
			u8tmp = 0x70;
			ts2020_writereg(fe, 0x05, priv->clk_out_div);
			break;
		case TS2020_CLK_OUT_ENABLED_XTALOUT:
			u8tmp = 0x6c;
			break;
		default:
			u8tmp = 0x60;
			break;
		}

		ts2020_writereg(fe, 0x42, u8tmp);

		if (priv->loop_through)
			u8tmp = 0xec;
		else
			u8tmp = 0x6c;

		ts2020_writereg(fe, 0x62, u8tmp);

		for (i = 0; i < ARRAY_SIZE(reg_vals); i++)
			ts2020_writereg(fe, reg_vals[i].reg, reg_vals[i].val);
	}

	return 0;
}

static int ts2020_tuner_gate_ctrl(struct dvb_frontend *fe, u8 offset)
{
	int ret;
	ret = ts2020_writereg(fe, 0x51, 0x1f - offset);
	ret |= ts2020_writereg(fe, 0x51, 0x1f);
	ret |= ts2020_writereg(fe, 0x50, offset);
	ret |= ts2020_writereg(fe, 0x50, 0x00);
	msleep(20);
	return ret;
}

static int ts2020_set_tuner_rf(struct dvb_frontend *fe)
{
	int reg;

	reg = ts2020_readreg(fe, 0x3d);
	reg &= 0x7f;
	if (reg < 0x16)
		reg = 0xa1;
	else if (reg == 0x16)
		reg = 0x99;
	else
		reg = 0xf9;

	ts2020_writereg(fe, 0x60, reg);
	reg = ts2020_tuner_gate_ctrl(fe, 0x08);

	return reg;
}

static int ts2020_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct ts2020_priv *priv = fe->tuner_priv;
	int ret;
	u32 f3db, gdiv28;
	u16 u16tmp, value, lpf_coeff;
	u8 buf[3], reg10, lpf_mxdiv, mlpf_max, mlpf_min, nlpf;
	unsigned int f_ref_khz, f_vco_khz, div_ref, div_out, pll_n;
	unsigned int frequency_khz = c->frequency;

	/*
	 * Integer-N PLL synthesizer
	 * kHz is used for all calculations to keep calculations within 32-bit
	 */
	f_ref_khz = TS2020_XTAL_FREQ;
	div_ref = DIV_ROUND_CLOSEST(f_ref_khz, 2000);

	/* select LO output divider */
	if (frequency_khz < priv->frequency_div) {
		div_out = 4;
		reg10 = 0x10;
	} else {
		div_out = 2;
		reg10 = 0x00;
	}

	f_vco_khz = frequency_khz * div_out;
	pll_n = f_vco_khz * div_ref / f_ref_khz;
	pll_n += pll_n % 2;
	priv->frequency_khz = pll_n * f_ref_khz / div_ref / div_out;

	pr_debug("frequency=%u offset=%d f_vco_khz=%u pll_n=%u div_ref=%u div_out=%u\n",
		 priv->frequency_khz, priv->frequency_khz - c->frequency,
		 f_vco_khz, pll_n, div_ref, div_out);

	if (priv->tuner == TS2020_M88TS2020) {
		lpf_coeff = 2766;
		reg10 |= 0x01;
		ret = ts2020_writereg(fe, 0x10, reg10);
	} else {
		lpf_coeff = 3200;
		reg10 |= 0x0b;
		ret = ts2020_writereg(fe, 0x10, reg10);
		ret |= ts2020_writereg(fe, 0x11, 0x40);
	}

	u16tmp = pll_n - 1024;
	buf[0] = (u16tmp >> 8) & 0xff;
	buf[1] = (u16tmp >> 0) & 0xff;
	buf[2] = div_ref - 8;

	ret |= ts2020_writereg(fe, 0x01, buf[0]);
	ret |= ts2020_writereg(fe, 0x02, buf[1]);
	ret |= ts2020_writereg(fe, 0x03, buf[2]);

	ret |= ts2020_tuner_gate_ctrl(fe, 0x10);
	if (ret < 0)
		return -ENODEV;

	ret |= ts2020_tuner_gate_ctrl(fe, 0x08);

	/* Tuner RF */
	if (priv->tuner == TS2020_M88TS2020)
		ret |= ts2020_set_tuner_rf(fe);

	gdiv28 = (TS2020_XTAL_FREQ / 1000 * 1694 + 500) / 1000;
	ret |= ts2020_writereg(fe, 0x04, gdiv28 & 0xff);
	ret |= ts2020_tuner_gate_ctrl(fe, 0x04);
	if (ret < 0)
		return -ENODEV;

	if (priv->tuner == TS2020_M88TS2022) {
		ret = ts2020_writereg(fe, 0x25, 0x00);
		ret |= ts2020_writereg(fe, 0x27, 0x70);
		ret |= ts2020_writereg(fe, 0x41, 0x09);
		ret |= ts2020_writereg(fe, 0x08, 0x0b);
		if (ret < 0)
			return -ENODEV;
	}

	value = ts2020_readreg(fe, 0x26);

	f3db = (c->bandwidth_hz / 1000 / 2) + 2000;
	f3db += FREQ_OFFSET_LOW_SYM_RATE; /* FIXME: ~always too wide filter */
	f3db = clamp(f3db, 7000U, 40000U);

	gdiv28 = gdiv28 * 207 / (value * 2 + 151);
	mlpf_max = gdiv28 * 135 / 100;
	mlpf_min = gdiv28 * 78 / 100;
	if (mlpf_max > 63)
		mlpf_max = 63;

	nlpf = (f3db * gdiv28 * 2 / lpf_coeff /
		(TS2020_XTAL_FREQ / 1000)  + 1) / 2;
	if (nlpf > 23)
		nlpf = 23;
	if (nlpf < 1)
		nlpf = 1;

	lpf_mxdiv = (nlpf * (TS2020_XTAL_FREQ / 1000)
		* lpf_coeff * 2  / f3db + 1) / 2;

	if (lpf_mxdiv < mlpf_min) {
		nlpf++;
		lpf_mxdiv = (nlpf * (TS2020_XTAL_FREQ / 1000)
			* lpf_coeff * 2  / f3db + 1) / 2;
	}

	if (lpf_mxdiv > mlpf_max)
		lpf_mxdiv = mlpf_max;

	ret = ts2020_writereg(fe, 0x04, lpf_mxdiv);
	ret |= ts2020_writereg(fe, 0x06, nlpf);

	ret |= ts2020_tuner_gate_ctrl(fe, 0x04);

	ret |= ts2020_tuner_gate_ctrl(fe, 0x01);

	msleep(80);

	return (ret < 0) ? -EINVAL : 0;
}

static int ts2020_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct ts2020_priv *priv = fe->tuner_priv;

	*frequency = priv->frequency_khz;
	return 0;
}

static int ts2020_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	*frequency = 0; /* Zero-IF */
	return 0;
}

/* read TS2020 signal strength */
static int ts2020_read_signal_strength(struct dvb_frontend *fe,
						u16 *signal_strength)
{
	u16 sig_reading, sig_strength;
	u8 rfgain, bbgain;

	rfgain = ts2020_readreg(fe, 0x3d) & 0x1f;
	bbgain = ts2020_readreg(fe, 0x21) & 0x1f;

	if (rfgain > 15)
		rfgain = 15;
	if (bbgain > 13)
		bbgain = 13;

	sig_reading = rfgain * 2 + bbgain * 3;

	sig_strength = 40 + (64 - sig_reading) * 50 / 64 ;

	/* cook the value to be suitable for szap-s2 human readable output */
	*signal_strength = sig_strength * 1000;

	return 0;
}

static struct dvb_tuner_ops ts2020_tuner_ops = {
	.info = {
		.name = "TS2020",
		.frequency_min = 950000,
		.frequency_max = 2150000
	},
	.init = ts2020_init,
	.release = ts2020_release,
	.sleep = ts2020_sleep,
	.set_params = ts2020_set_params,
	.get_frequency = ts2020_get_frequency,
	.get_if_frequency = ts2020_get_if_frequency,
	.get_rf_strength = ts2020_read_signal_strength,
};

struct dvb_frontend *ts2020_attach(struct dvb_frontend *fe,
					const struct ts2020_config *config,
					struct i2c_adapter *i2c)
{
	struct ts2020_priv *priv = NULL;
	u8 buf;

	priv = kzalloc(sizeof(struct ts2020_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_address = config->tuner_address;
	priv->i2c = i2c;
	priv->clk_out = config->clk_out;
	priv->clk_out_div = config->clk_out_div;
	priv->frequency_div = config->frequency_div;
	priv->fe = fe;
	fe->tuner_priv = priv;

	if (!priv->frequency_div)
		priv->frequency_div = 1060000;

	/* Wake Up the tuner */
	if ((0x03 & ts2020_readreg(fe, 0x00)) == 0x00) {
		ts2020_writereg(fe, 0x00, 0x01);
		msleep(2);
	}

	ts2020_writereg(fe, 0x00, 0x03);
	msleep(2);

	/* Check the tuner version */
	buf = ts2020_readreg(fe, 0x00);
	if ((buf == 0x01) || (buf == 0x41) || (buf == 0x81)) {
		printk(KERN_INFO "%s: Find tuner TS2020!\n", __func__);
		priv->tuner = TS2020_M88TS2020;
	} else if ((buf == 0x83) || (buf == 0xc3)) {
		printk(KERN_INFO "%s: Find tuner TS2022!\n", __func__);
		priv->tuner = TS2020_M88TS2022;
	} else {
		printk(KERN_ERR "%s: Read tuner reg[0] = %d\n", __func__, buf);
		kfree(priv);
		return NULL;
	}

	memcpy(&fe->ops.tuner_ops, &ts2020_tuner_ops,
				sizeof(struct dvb_tuner_ops));

	return fe;
}
EXPORT_SYMBOL(ts2020_attach);

static int ts2020_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct ts2020_config *pdata = client->dev.platform_data;
	struct dvb_frontend *fe = pdata->fe;
	struct ts2020_priv *dev;
	int ret;
	u8 u8tmp;
	unsigned int utmp;
	char *chip_str;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	dev->i2c = client->adapter;
	dev->i2c_address = client->addr;
	dev->clk_out = pdata->clk_out;
	dev->clk_out_div = pdata->clk_out_div;
	dev->frequency_div = pdata->frequency_div;
	dev->fe = fe;
	fe->tuner_priv = dev;

	/* check if the tuner is there */
	ret = ts2020_readreg(fe, 0x00);
	if (ret < 0)
		goto err;
	utmp = ret;

	if ((utmp & 0x03) == 0x00) {
		ret = ts2020_writereg(fe, 0x00, 0x01);
		if (ret)
			goto err;

		usleep_range(2000, 50000);
	}

	ret = ts2020_writereg(fe, 0x00, 0x03);
	if (ret)
		goto err;

	usleep_range(2000, 50000);

	ret = ts2020_readreg(fe, 0x00);
	if (ret < 0)
		goto err;
	utmp = ret;

	dev_dbg(&client->dev, "chip_id=%02x\n", utmp);

	switch (utmp) {
	case 0x01:
	case 0x41:
	case 0x81:
		dev->tuner = TS2020_M88TS2020;
		chip_str = "TS2020";
		if (!dev->frequency_div)
			dev->frequency_div = 1060000;
		break;
	case 0xc3:
	case 0x83:
		dev->tuner = TS2020_M88TS2022;
		chip_str = "TS2022";
		if (!dev->frequency_div)
			dev->frequency_div = 1103000;
		break;
	default:
		ret = -ENODEV;
		goto err;
	}

	if (dev->tuner == TS2020_M88TS2022) {
		switch (dev->clk_out) {
		case TS2020_CLK_OUT_DISABLED:
			u8tmp = 0x60;
			break;
		case TS2020_CLK_OUT_ENABLED:
			u8tmp = 0x70;
			ret = ts2020_writereg(fe, 0x05, dev->clk_out_div);
			if (ret)
				goto err;
			break;
		case TS2020_CLK_OUT_ENABLED_XTALOUT:
			u8tmp = 0x6c;
			break;
		default:
			ret = -EINVAL;
			goto err;
		}

		ret = ts2020_writereg(fe, 0x42, u8tmp);
		if (ret)
			goto err;

		if (dev->loop_through)
			u8tmp = 0xec;
		else
			u8tmp = 0x6c;

		ret = ts2020_writereg(fe, 0x62, u8tmp);
		if (ret)
			goto err;
	}

	/* sleep */
	ret = ts2020_writereg(fe, 0x00, 0x00);
	if (ret)
		goto err;

	dev_info(&client->dev,
		 "Montage Technology %s successfully identified\n", chip_str);

	memcpy(&fe->ops.tuner_ops, &ts2020_tuner_ops,
			sizeof(struct dvb_tuner_ops));
	fe->ops.tuner_ops.release = NULL;

	i2c_set_clientdata(client, dev);
	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	kfree(dev);
	return ret;
}

static int ts2020_remove(struct i2c_client *client)
{
	struct ts2020_priv *dev = i2c_get_clientdata(client);
	struct dvb_frontend *fe = dev->fe;

	dev_dbg(&client->dev, "\n");

	memset(&fe->ops.tuner_ops, 0, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = NULL;
	kfree(dev);

	return 0;
}

static const struct i2c_device_id ts2020_id_table[] = {
	{"ts2020", 0},
	{"ts2022", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ts2020_id_table);

static struct i2c_driver ts2020_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "ts2020",
	},
	.probe		= ts2020_probe,
	.remove		= ts2020_remove,
	.id_table	= ts2020_id_table,
};

module_i2c_driver(ts2020_driver);

MODULE_AUTHOR("Konstantin Dimitrov <kosio.dimitrov@gmail.com>");
MODULE_DESCRIPTION("Montage Technology TS2020 - Silicon tuner driver module");
MODULE_LICENSE("GPL");
