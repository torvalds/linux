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
#include <linux/regmap.h>
#include <linux/math64.h>

#define TS2020_XTAL_FREQ   27000 /* in kHz */
#define FREQ_OFFSET_LOW_SYM_RATE 3000

struct ts2020_priv {
	struct i2c_client *client;
	struct mutex regmap_mutex;
	struct regmap_config regmap_config;
	struct regmap *regmap;
	struct dvb_frontend *fe;
	struct delayed_work stat_work;
	int (*get_agc_pwm)(struct dvb_frontend *fe, u8 *_agc_pwm);
	/* i2c details */
	struct i2c_adapter *i2c;
	int i2c_address;
	bool loop_through:1;
	u8 clk_out:2;
	u8 clk_out_div:5;
	bool dont_poll:1;
	u32 frequency_div; /* LO output divider switch frequency */
	u32 frequency_khz; /* actual used LO frequency */
#define TS2020_M88TS2020 0
#define TS2020_M88TS2022 1
	u8 tuner;
};

struct ts2020_reg_val {
	u8 reg;
	u8 val;
};

static void ts2020_stat_work(struct work_struct *work);

static int ts2020_release(struct dvb_frontend *fe)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	struct i2c_client *client = priv->client;

	dev_dbg(&client->dev, "\n");

	i2c_unregister_device(client);
	return 0;
}

static int ts2020_sleep(struct dvb_frontend *fe)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	int ret;
	u8 u8tmp;

	if (priv->tuner == TS2020_M88TS2020)
		u8tmp = 0x0a; /* XXX: probably wrong */
	else
		u8tmp = 0x00;

	ret = regmap_write(priv->regmap, u8tmp, 0x00);
	if (ret < 0)
		return ret;

	/* stop statistics polling */
	if (!priv->dont_poll)
		cancel_delayed_work_sync(&priv->stat_work);
	return 0;
}

static int ts2020_init(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct ts2020_priv *priv = fe->tuner_priv;
	int i;
	u8 u8tmp;

	if (priv->tuner == TS2020_M88TS2020) {
		regmap_write(priv->regmap, 0x42, 0x73);
		regmap_write(priv->regmap, 0x05, priv->clk_out_div);
		regmap_write(priv->regmap, 0x20, 0x27);
		regmap_write(priv->regmap, 0x07, 0x02);
		regmap_write(priv->regmap, 0x11, 0xff);
		regmap_write(priv->regmap, 0x60, 0xf9);
		regmap_write(priv->regmap, 0x08, 0x01);
		regmap_write(priv->regmap, 0x00, 0x41);
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

		regmap_write(priv->regmap, 0x00, 0x01);
		regmap_write(priv->regmap, 0x00, 0x03);

		switch (priv->clk_out) {
		case TS2020_CLK_OUT_DISABLED:
			u8tmp = 0x60;
			break;
		case TS2020_CLK_OUT_ENABLED:
			u8tmp = 0x70;
			regmap_write(priv->regmap, 0x05, priv->clk_out_div);
			break;
		case TS2020_CLK_OUT_ENABLED_XTALOUT:
			u8tmp = 0x6c;
			break;
		default:
			u8tmp = 0x60;
			break;
		}

		regmap_write(priv->regmap, 0x42, u8tmp);

		if (priv->loop_through)
			u8tmp = 0xec;
		else
			u8tmp = 0x6c;

		regmap_write(priv->regmap, 0x62, u8tmp);

		for (i = 0; i < ARRAY_SIZE(reg_vals); i++)
			regmap_write(priv->regmap, reg_vals[i].reg,
				     reg_vals[i].val);
	}

	/* Initialise v5 stats here */
	c->strength.len = 1;
	c->strength.stat[0].scale = FE_SCALE_DECIBEL;
	c->strength.stat[0].uvalue = 0;

	/* Start statistics polling by invoking the work function */
	ts2020_stat_work(&priv->stat_work.work);
	return 0;
}

static int ts2020_tuner_gate_ctrl(struct dvb_frontend *fe, u8 offset)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	int ret;
	ret = regmap_write(priv->regmap, 0x51, 0x1f - offset);
	ret |= regmap_write(priv->regmap, 0x51, 0x1f);
	ret |= regmap_write(priv->regmap, 0x50, offset);
	ret |= regmap_write(priv->regmap, 0x50, 0x00);
	msleep(20);
	return ret;
}

static int ts2020_set_tuner_rf(struct dvb_frontend *fe)
{
	struct ts2020_priv *dev = fe->tuner_priv;
	int ret;
	unsigned int utmp;

	ret = regmap_read(dev->regmap, 0x3d, &utmp);
	utmp &= 0x7f;
	if (utmp < 0x16)
		utmp = 0xa1;
	else if (utmp == 0x16)
		utmp = 0x99;
	else
		utmp = 0xf9;

	regmap_write(dev->regmap, 0x60, utmp);
	ret = ts2020_tuner_gate_ctrl(fe, 0x08);

	return ret;
}

static int ts2020_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct ts2020_priv *priv = fe->tuner_priv;
	int ret;
	unsigned int utmp;
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
		ret = regmap_write(priv->regmap, 0x10, reg10);
	} else {
		lpf_coeff = 3200;
		reg10 |= 0x0b;
		ret = regmap_write(priv->regmap, 0x10, reg10);
		ret |= regmap_write(priv->regmap, 0x11, 0x40);
	}

	u16tmp = pll_n - 1024;
	buf[0] = (u16tmp >> 8) & 0xff;
	buf[1] = (u16tmp >> 0) & 0xff;
	buf[2] = div_ref - 8;

	ret |= regmap_write(priv->regmap, 0x01, buf[0]);
	ret |= regmap_write(priv->regmap, 0x02, buf[1]);
	ret |= regmap_write(priv->regmap, 0x03, buf[2]);

	ret |= ts2020_tuner_gate_ctrl(fe, 0x10);
	if (ret < 0)
		return -ENODEV;

	ret |= ts2020_tuner_gate_ctrl(fe, 0x08);

	/* Tuner RF */
	if (priv->tuner == TS2020_M88TS2020)
		ret |= ts2020_set_tuner_rf(fe);

	gdiv28 = (TS2020_XTAL_FREQ / 1000 * 1694 + 500) / 1000;
	ret |= regmap_write(priv->regmap, 0x04, gdiv28 & 0xff);
	ret |= ts2020_tuner_gate_ctrl(fe, 0x04);
	if (ret < 0)
		return -ENODEV;

	if (priv->tuner == TS2020_M88TS2022) {
		ret = regmap_write(priv->regmap, 0x25, 0x00);
		ret |= regmap_write(priv->regmap, 0x27, 0x70);
		ret |= regmap_write(priv->regmap, 0x41, 0x09);
		ret |= regmap_write(priv->regmap, 0x08, 0x0b);
		if (ret < 0)
			return -ENODEV;
	}

	regmap_read(priv->regmap, 0x26, &utmp);
	value = utmp;

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

	ret = regmap_write(priv->regmap, 0x04, lpf_mxdiv);
	ret |= regmap_write(priv->regmap, 0x06, nlpf);

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

/*
 * Get the tuner gain.
 * @fe: The front end for which we're determining the gain
 * @v_agc: The voltage of the AGC from the demodulator (0-2600mV)
 * @_gain: Where to store the gain (in 0.001dB units)
 *
 * Returns 0 or a negative error code.
 */
static int ts2020_read_tuner_gain(struct dvb_frontend *fe, unsigned v_agc,
				  __s64 *_gain)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	unsigned long gain1, gain2, gain3;
	unsigned utmp;
	int ret;

	/* Read the RF gain */
	ret = regmap_read(priv->regmap, 0x3d, &utmp);
	if (ret < 0)
		return ret;
	gain1 = utmp & 0x1f;

	/* Read the baseband gain */
	ret = regmap_read(priv->regmap, 0x21, &utmp);
	if (ret < 0)
		return ret;
	gain2 = utmp & 0x1f;

	switch (priv->tuner) {
	case TS2020_M88TS2020:
		gain1 = clamp_t(long, gain1, 0, 15);
		gain2 = clamp_t(long, gain2, 0, 13);
		v_agc = clamp_t(long, v_agc, 400, 1100);

		*_gain = -(gain1 * 2330 +
			   gain2 * 3500 +
			   v_agc * 24 / 10 * 10 +
			   10000);
		/* gain in range -19600 to -116850 in units of 0.001dB */
		break;

	case TS2020_M88TS2022:
		ret = regmap_read(priv->regmap, 0x66, &utmp);
		if (ret < 0)
			return ret;
		gain3 = (utmp >> 3) & 0x07;

		gain1 = clamp_t(long, gain1, 0, 15);
		gain2 = clamp_t(long, gain2, 2, 16);
		gain3 = clamp_t(long, gain3, 0, 6);
		v_agc = clamp_t(long, v_agc, 600, 1600);

		*_gain = -(gain1 * 2650 +
			   gain2 * 3380 +
			   gain3 * 2850 +
			   v_agc * 176 / 100 * 10 -
			   30000);
		/* gain in range -47320 to -158950 in units of 0.001dB */
		break;
	}

	return 0;
}

/*
 * Get the AGC information from the demodulator and use that to calculate the
 * tuner gain.
 */
static int ts2020_get_tuner_gain(struct dvb_frontend *fe, __s64 *_gain)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	int v_agc = 0, ret;
	u8 agc_pwm;

	/* Read the AGC PWM rate from the demodulator */
	if (priv->get_agc_pwm) {
		ret = priv->get_agc_pwm(fe, &agc_pwm);
		if (ret < 0)
			return ret;

		switch (priv->tuner) {
		case TS2020_M88TS2020:
			v_agc = (int)agc_pwm * 20 - 1166;
			break;
		case TS2020_M88TS2022:
			v_agc = (int)agc_pwm * 16 - 670;
			break;
		}

		if (v_agc < 0)
			v_agc = 0;
	}

	return ts2020_read_tuner_gain(fe, v_agc, _gain);
}

/*
 * Gather statistics on a regular basis
 */
static void ts2020_stat_work(struct work_struct *work)
{
	struct ts2020_priv *priv = container_of(work, struct ts2020_priv,
					       stat_work.work);
	struct i2c_client *client = priv->client;
	struct dtv_frontend_properties *c = &priv->fe->dtv_property_cache;
	int ret;

	dev_dbg(&client->dev, "\n");

	ret = ts2020_get_tuner_gain(priv->fe, &c->strength.stat[0].svalue);
	if (ret < 0)
		goto err;

	c->strength.stat[0].scale = FE_SCALE_DECIBEL;

	if (!priv->dont_poll)
		schedule_delayed_work(&priv->stat_work, msecs_to_jiffies(2000));
	return;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
}

/*
 * Read TS2020 signal strength in v3 format.
 */
static int ts2020_read_signal_strength(struct dvb_frontend *fe,
				       u16 *_signal_strength)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct ts2020_priv *priv = fe->tuner_priv;
	unsigned strength;
	__s64 gain;

	if (priv->dont_poll)
		ts2020_stat_work(&priv->stat_work.work);

	if (c->strength.stat[0].scale == FE_SCALE_NOT_AVAILABLE) {
		*_signal_strength = 0;
		return 0;
	}

	gain = c->strength.stat[0].svalue;

	/* Calculate the signal strength based on the total gain of the tuner */
	if (gain < -85000)
		/* 0%: no signal or weak signal */
		strength = 0;
	else if (gain < -65000)
		/* 0% - 60%: weak signal */
		strength = 0 + div64_s64((85000 + gain) * 3, 1000);
	else if (gain < -45000)
		/* 60% - 90%: normal signal */
		strength = 60 + div64_s64((65000 + gain) * 3, 2000);
	else
		/* 90% - 99%: strong signal */
		strength = 90 + div64_s64((45000 + gain), 5000);

	*_signal_strength = strength * 65535 / 100;
	return 0;
}

static const struct dvb_tuner_ops ts2020_tuner_ops = {
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
	struct i2c_client *client;
	struct i2c_board_info board_info;

	/* This is only used by ts2020_probe() so can be on the stack */
	struct ts2020_config pdata;

	memcpy(&pdata, config, sizeof(pdata));
	pdata.fe = fe;
	pdata.attach_in_use = true;

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "ts2020", I2C_NAME_SIZE);
	board_info.addr = config->tuner_address;
	board_info.platform_data = &pdata;
	client = i2c_new_device(i2c, &board_info);
	if (!client || !client->dev.driver)
		return NULL;

	return fe;
}
EXPORT_SYMBOL(ts2020_attach);

/*
 * We implement own regmap locking due to legacy DVB attach which uses frontend
 * gate control callback to control I2C bus access. We can open / close gate and
 * serialize whole open / I2C-operation / close sequence at the same.
 */
static void ts2020_regmap_lock(void *__dev)
{
	struct ts2020_priv *dev = __dev;

	mutex_lock(&dev->regmap_mutex);
	if (dev->fe->ops.i2c_gate_ctrl)
		dev->fe->ops.i2c_gate_ctrl(dev->fe, 1);
}

static void ts2020_regmap_unlock(void *__dev)
{
	struct ts2020_priv *dev = __dev;

	if (dev->fe->ops.i2c_gate_ctrl)
		dev->fe->ops.i2c_gate_ctrl(dev->fe, 0);
	mutex_unlock(&dev->regmap_mutex);
}

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

	/* create regmap */
	mutex_init(&dev->regmap_mutex);
	dev->regmap_config.reg_bits = 8,
	dev->regmap_config.val_bits = 8,
	dev->regmap_config.lock = ts2020_regmap_lock,
	dev->regmap_config.unlock = ts2020_regmap_unlock,
	dev->regmap_config.lock_arg = dev,
	dev->regmap = regmap_init_i2c(client, &dev->regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

	dev->i2c = client->adapter;
	dev->i2c_address = client->addr;
	dev->loop_through = pdata->loop_through;
	dev->clk_out = pdata->clk_out;
	dev->clk_out_div = pdata->clk_out_div;
	dev->dont_poll = pdata->dont_poll;
	dev->frequency_div = pdata->frequency_div;
	dev->fe = fe;
	dev->get_agc_pwm = pdata->get_agc_pwm;
	fe->tuner_priv = dev;
	dev->client = client;
	INIT_DELAYED_WORK(&dev->stat_work, ts2020_stat_work);

	/* check if the tuner is there */
	ret = regmap_read(dev->regmap, 0x00, &utmp);
	if (ret)
		goto err_regmap_exit;

	if ((utmp & 0x03) == 0x00) {
		ret = regmap_write(dev->regmap, 0x00, 0x01);
		if (ret)
			goto err_regmap_exit;

		usleep_range(2000, 50000);
	}

	ret = regmap_write(dev->regmap, 0x00, 0x03);
	if (ret)
		goto err_regmap_exit;

	usleep_range(2000, 50000);

	ret = regmap_read(dev->regmap, 0x00, &utmp);
	if (ret)
		goto err_regmap_exit;

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
		goto err_regmap_exit;
	}

	if (dev->tuner == TS2020_M88TS2022) {
		switch (dev->clk_out) {
		case TS2020_CLK_OUT_DISABLED:
			u8tmp = 0x60;
			break;
		case TS2020_CLK_OUT_ENABLED:
			u8tmp = 0x70;
			ret = regmap_write(dev->regmap, 0x05, dev->clk_out_div);
			if (ret)
				goto err_regmap_exit;
			break;
		case TS2020_CLK_OUT_ENABLED_XTALOUT:
			u8tmp = 0x6c;
			break;
		default:
			ret = -EINVAL;
			goto err_regmap_exit;
		}

		ret = regmap_write(dev->regmap, 0x42, u8tmp);
		if (ret)
			goto err_regmap_exit;

		if (dev->loop_through)
			u8tmp = 0xec;
		else
			u8tmp = 0x6c;

		ret = regmap_write(dev->regmap, 0x62, u8tmp);
		if (ret)
			goto err_regmap_exit;
	}

	/* sleep */
	ret = regmap_write(dev->regmap, 0x00, 0x00);
	if (ret)
		goto err_regmap_exit;

	dev_info(&client->dev,
		 "Montage Technology %s successfully identified\n", chip_str);

	memcpy(&fe->ops.tuner_ops, &ts2020_tuner_ops,
			sizeof(struct dvb_tuner_ops));
	if (!pdata->attach_in_use)
		fe->ops.tuner_ops.release = NULL;

	i2c_set_clientdata(client, dev);
	return 0;
err_regmap_exit:
	regmap_exit(dev->regmap);
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int ts2020_remove(struct i2c_client *client)
{
	struct ts2020_priv *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	/* stop statistics polling */
	if (!dev->dont_poll)
		cancel_delayed_work_sync(&dev->stat_work);

	regmap_exit(dev->regmap);
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
