// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Montage Technology M88DS3103/M88RS6000 demodulator driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
 */

#include "m88ds3103_priv.h"

static const struct dvb_frontend_ops m88ds3103_ops;

/* write single register with mask */
static int m88ds3103_update_bits(struct m88ds3103_dev *dev,
				u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	/* no need for read if whole reg is written */
	if (mask != 0xff) {
		ret = regmap_bulk_read(dev->regmap, reg, &tmp, 1);
		if (ret)
			return ret;

		val &= mask;
		tmp &= ~mask;
		val |= tmp;
	}

	return regmap_bulk_write(dev->regmap, reg, &val, 1);
}

/* write reg val table using reg addr auto increment */
static int m88ds3103_wr_reg_val_tab(struct m88ds3103_dev *dev,
		const struct m88ds3103_reg_val *tab, int tab_len)
{
	struct i2c_client *client = dev->client;
	int ret, i, j;
	u8 buf[83];

	dev_dbg(&client->dev, "tab_len=%d\n", tab_len);

	if (tab_len > 86) {
		ret = -EINVAL;
		goto err;
	}

	for (i = 0, j = 0; i < tab_len; i++, j++) {
		buf[j] = tab[i].val;

		if (i == tab_len - 1 || tab[i].reg != tab[i + 1].reg - 1 ||
				!((j + 1) % (dev->cfg->i2c_wr_max - 1))) {
			ret = regmap_bulk_write(dev->regmap, tab[i].reg - j, buf, j + 1);
			if (ret)
				goto err;

			j = -1;
		}
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

/*
 * m88ds3103b demod has an internal device related to clocking. First the i2c
 * gate must be opened, for one transaction, then writes will be allowed.
 */
static int m88ds3103b_dt_write(struct m88ds3103_dev *dev, int reg, int data)
{
	struct i2c_client *client = dev->client;
	u8 buf[] = {reg, data};
	u8 val;
	int ret;
	struct i2c_msg msg = {
		.addr = dev->dt_addr, .flags = 0, .buf = buf, .len = 2
	};

	m88ds3103_update_bits(dev, 0x11, 0x01, 0x00);

	val = 0x11;
	ret = regmap_write(dev->regmap, 0x03, val);
	if (ret)
		dev_dbg(&client->dev, "fail=%d\n", ret);

	ret = i2c_transfer(dev->dt_client->adapter, &msg, 1);
	if (ret != 1) {
		dev_err(&client->dev, "0x%02x (ret=%i, reg=0x%02x, value=0x%02x)\n",
			dev->dt_addr, ret, reg, data);

		m88ds3103_update_bits(dev, 0x11, 0x01, 0x01);
		return -EREMOTEIO;
	}
	m88ds3103_update_bits(dev, 0x11, 0x01, 0x01);

	dev_dbg(&client->dev, "0x%02x reg 0x%02x, value 0x%02x\n",
		dev->dt_addr, reg, data);

	return 0;
}

/*
 * m88ds3103b demod has an internal device related to clocking. First the i2c
 * gate must be opened, for two transactions, then reads will be allowed.
 */
static int m88ds3103b_dt_read(struct m88ds3103_dev *dev, u8 reg)
{
	struct i2c_client *client = dev->client;
	int ret;
	u8 val;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{
			.addr = dev->dt_addr,
			.flags = 0,
			.buf = b0,
			.len = 1
		},
		{
			.addr = dev->dt_addr,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};

	m88ds3103_update_bits(dev, 0x11, 0x01, 0x00);

	val = 0x12;
	ret = regmap_write(dev->regmap, 0x03, val);
	if (ret)
		dev_dbg(&client->dev, "fail=%d\n", ret);

	ret = i2c_transfer(dev->dt_client->adapter, msg, 2);
	if (ret != 2) {
		dev_err(&client->dev, "0x%02x (ret=%d, reg=0x%02x)\n",
			dev->dt_addr, ret, reg);

		m88ds3103_update_bits(dev, 0x11, 0x01, 0x01);
		return -EREMOTEIO;
	}
	m88ds3103_update_bits(dev, 0x11, 0x01, 0x01);

	dev_dbg(&client->dev, "0x%02x reg 0x%02x, value 0x%02x\n",
		dev->dt_addr, reg, b1[0]);

	return b1[0];
}

/*
 * Get the demodulator AGC PWM voltage setting supplied to the tuner.
 */
int m88ds3103_get_agc_pwm(struct dvb_frontend *fe, u8 *_agc_pwm)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	unsigned tmp;
	int ret;

	ret = regmap_read(dev->regmap, 0x3f, &tmp);
	if (ret == 0)
		*_agc_pwm = tmp;
	return ret;
}
EXPORT_SYMBOL(m88ds3103_get_agc_pwm);

static int m88ds3103_read_status(struct dvb_frontend *fe,
				 enum fe_status *status)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i, itmp;
	unsigned int utmp;
	u8 buf[3];

	*status = 0;

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBS:
		ret = regmap_read(dev->regmap, 0xd1, &utmp);
		if (ret)
			goto err;

		if ((utmp & 0x07) == 0x07)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
					FE_HAS_VITERBI | FE_HAS_SYNC |
					FE_HAS_LOCK;
		break;
	case SYS_DVBS2:
		ret = regmap_read(dev->regmap, 0x0d, &utmp);
		if (ret)
			goto err;

		if ((utmp & 0x8f) == 0x8f)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
					FE_HAS_VITERBI | FE_HAS_SYNC |
					FE_HAS_LOCK;
		break;
	default:
		dev_dbg(&client->dev, "invalid delivery_system\n");
		ret = -EINVAL;
		goto err;
	}

	dev->fe_status = *status;
	dev_dbg(&client->dev, "lock=%02x status=%02x\n", utmp, *status);

	/* CNR */
	if (dev->fe_status & FE_HAS_VITERBI) {
		unsigned int cnr, noise, signal, noise_tot, signal_tot;

		cnr = 0;
		/* more iterations for more accurate estimation */
		#define M88DS3103_SNR_ITERATIONS 3

		switch (c->delivery_system) {
		case SYS_DVBS:
			itmp = 0;

			for (i = 0; i < M88DS3103_SNR_ITERATIONS; i++) {
				ret = regmap_read(dev->regmap, 0xff, &utmp);
				if (ret)
					goto err;

				itmp += utmp;
			}

			/* use of single register limits max value to 15 dB */
			/* SNR(X) dB = 10 * ln(X) / ln(10) dB */
			itmp = DIV_ROUND_CLOSEST(itmp, 8 * M88DS3103_SNR_ITERATIONS);
			if (itmp)
				cnr = div_u64((u64) 10000 * intlog2(itmp), intlog2(10));
			break;
		case SYS_DVBS2:
			noise_tot = 0;
			signal_tot = 0;

			for (i = 0; i < M88DS3103_SNR_ITERATIONS; i++) {
				ret = regmap_bulk_read(dev->regmap, 0x8c, buf, 3);
				if (ret)
					goto err;

				noise = buf[1] << 6;    /* [13:6] */
				noise |= buf[0] & 0x3f; /*  [5:0] */
				noise >>= 2;
				signal = buf[2] * buf[2];
				signal >>= 1;

				noise_tot += noise;
				signal_tot += signal;
			}

			noise = noise_tot / M88DS3103_SNR_ITERATIONS;
			signal = signal_tot / M88DS3103_SNR_ITERATIONS;

			/* SNR(X) dB = 10 * log10(X) dB */
			if (signal > noise) {
				itmp = signal / noise;
				cnr = div_u64((u64) 10000 * intlog10(itmp), (1 << 24));
			}
			break;
		default:
			dev_dbg(&client->dev, "invalid delivery_system\n");
			ret = -EINVAL;
			goto err;
		}

		if (cnr) {
			c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
			c->cnr.stat[0].svalue = cnr;
		} else {
			c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		}
	} else {
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	/* BER */
	if (dev->fe_status & FE_HAS_LOCK) {
		unsigned int utmp, post_bit_error, post_bit_count;

		switch (c->delivery_system) {
		case SYS_DVBS:
			ret = regmap_write(dev->regmap, 0xf9, 0x04);
			if (ret)
				goto err;

			ret = regmap_read(dev->regmap, 0xf8, &utmp);
			if (ret)
				goto err;

			/* measurement ready? */
			if (!(utmp & 0x10)) {
				ret = regmap_bulk_read(dev->regmap, 0xf6, buf, 2);
				if (ret)
					goto err;

				post_bit_error = buf[1] << 8 | buf[0] << 0;
				post_bit_count = 0x800000;
				dev->post_bit_error += post_bit_error;
				dev->post_bit_count += post_bit_count;
				dev->dvbv3_ber = post_bit_error;

				/* restart measurement */
				utmp |= 0x10;
				ret = regmap_write(dev->regmap, 0xf8, utmp);
				if (ret)
					goto err;
			}
			break;
		case SYS_DVBS2:
			ret = regmap_bulk_read(dev->regmap, 0xd5, buf, 3);
			if (ret)
				goto err;

			utmp = buf[2] << 16 | buf[1] << 8 | buf[0] << 0;

			/* enough data? */
			if (utmp > 4000) {
				ret = regmap_bulk_read(dev->regmap, 0xf7, buf, 2);
				if (ret)
					goto err;

				post_bit_error = buf[1] << 8 | buf[0] << 0;
				post_bit_count = 32 * utmp; /* TODO: FEC */
				dev->post_bit_error += post_bit_error;
				dev->post_bit_count += post_bit_count;
				dev->dvbv3_ber = post_bit_error;

				/* restart measurement */
				ret = regmap_write(dev->regmap, 0xd1, 0x01);
				if (ret)
					goto err;

				ret = regmap_write(dev->regmap, 0xf9, 0x01);
				if (ret)
					goto err;

				ret = regmap_write(dev->regmap, 0xf9, 0x00);
				if (ret)
					goto err;

				ret = regmap_write(dev->regmap, 0xd1, 0x00);
				if (ret)
					goto err;
			}
			break;
		default:
			dev_dbg(&client->dev, "invalid delivery_system\n");
			ret = -EINVAL;
			goto err;
		}

		c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[0].uvalue = dev->post_bit_error;
		c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[0].uvalue = dev->post_bit_count;
	} else {
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ds3103b_select_mclk(struct m88ds3103_dev *dev)
{
	struct i2c_client *client = dev->client;
	struct dtv_frontend_properties *c = &dev->fe.dtv_property_cache;
	u32 adc_Freq_MHz[3] = {96, 93, 99};
	u8  reg16_list[3] = {96, 92, 100}, reg16, reg15;
	u32 offset_MHz[3];
	u32 max_offset = 0;
	u32 old_setting = dev->mclk;
	u32 tuner_freq_MHz = c->frequency / 1000;
	u8 i;
	char big_symbol = 0;

	big_symbol = (c->symbol_rate > 45010000) ? 1 : 0;

	if (big_symbol) {
		reg16 = 115;
	} else {
		reg16 = 96;

		/* TODO: IS THIS NECESSARY ? */
		for (i = 0; i < 3; i++) {
			offset_MHz[i] = tuner_freq_MHz % adc_Freq_MHz[i];

			if (offset_MHz[i] > (adc_Freq_MHz[i] / 2))
				offset_MHz[i] = adc_Freq_MHz[i] - offset_MHz[i];

			if (offset_MHz[i] > max_offset) {
				max_offset = offset_MHz[i];
				reg16 = reg16_list[i];
				dev->mclk = adc_Freq_MHz[i] * 1000 * 1000;

				if (big_symbol)
					dev->mclk /= 2;

				dev_dbg(&client->dev, "modifying mclk %u -> %u\n",
					old_setting, dev->mclk);
			}
		}
	}

	if (dev->mclk == 93000000)
		regmap_write(dev->regmap, 0xA0, 0x42);
	else if (dev->mclk == 96000000)
		regmap_write(dev->regmap, 0xA0, 0x44);
	else if (dev->mclk == 99000000)
		regmap_write(dev->regmap, 0xA0, 0x46);
	else if (dev->mclk == 110250000)
		regmap_write(dev->regmap, 0xA0, 0x4E);
	else
		regmap_write(dev->regmap, 0xA0, 0x44);

	reg15 = m88ds3103b_dt_read(dev, 0x15);

	m88ds3103b_dt_write(dev, 0x05, 0x40);
	m88ds3103b_dt_write(dev, 0x11, 0x08);

	if (big_symbol)
		reg15 |= 0x02;
	else
		reg15 &= ~0x02;

	m88ds3103b_dt_write(dev, 0x15, reg15);
	m88ds3103b_dt_write(dev, 0x16, reg16);

	usleep_range(5000, 5500);

	m88ds3103b_dt_write(dev, 0x05, 0x00);
	m88ds3103b_dt_write(dev, 0x11, (u8)(big_symbol ? 0x0E : 0x0A));

	usleep_range(5000, 5500);

	return 0;
}

static int m88ds3103b_set_mclk(struct m88ds3103_dev *dev, u32 mclk_khz)
{
	u8 reg15, reg16, reg1D, reg1E, reg1F, tmp;
	u8 sm, f0 = 0, f1 = 0, f2 = 0, f3 = 0;
	u16 pll_div_fb, N;
	u32 div;

	reg15 = m88ds3103b_dt_read(dev, 0x15);
	reg16 = m88ds3103b_dt_read(dev, 0x16);
	reg1D = m88ds3103b_dt_read(dev, 0x1D);

	if (dev->cfg->ts_mode != M88DS3103_TS_SERIAL) {
		if (reg16 == 92)
			tmp = 93;
		else if (reg16 == 100)
			tmp = 99;
		else
			tmp = 96;

		mclk_khz *= tmp;
		mclk_khz /= 96;
	}

	pll_div_fb = (reg15 & 0x01) << 8;
	pll_div_fb += reg16;
	pll_div_fb += 32;

	div = 9000 * pll_div_fb * 4;
	div /= mclk_khz;

	if (dev->cfg->ts_mode == M88DS3103_TS_SERIAL) {
		if (div <= 32) {
			N = 2;

			f0 = 0;
			f1 = div / N;
			f2 = div - f1;
			f3 = 0;
		} else if (div <= 34) {
			N = 3;

			f0 = div / N;
			f1 = (div - f0) / (N - 1);
			f2 = div - f0 - f1;
			f3 = 0;
		} else if (div <= 64) {
			N = 4;

			f0 = div / N;
			f1 = (div - f0) / (N - 1);
			f2 = (div - f0 - f1) / (N - 2);
			f3 = div - f0 - f1 - f2;
		} else {
			N = 4;

			f0 = 16;
			f1 = 16;
			f2 = 16;
			f3 = 16;
		}

		if (f0 == 16)
			f0 = 0;
		else if ((f0 < 8) && (f0 != 0))
			f0 = 8;

		if (f1 == 16)
			f1 = 0;
		else if ((f1 < 8) && (f1 != 0))
			f1 = 8;

		if (f2 == 16)
			f2 = 0;
		else if ((f2 < 8) && (f2 != 0))
			f2 = 8;

		if (f3 == 16)
			f3 = 0;
		else if ((f3 < 8) && (f3 != 0))
			f3 = 8;
	} else {
		if (div <= 32) {
			N = 2;

			f0 = 0;
			f1 = div / N;
			f2 = div - f1;
			f3 = 0;
		} else if (div <= 48) {
			N = 3;

			f0 = div / N;
			f1 = (div - f0) / (N - 1);
			f2 = div - f0 - f1;
			f3 = 0;
		} else if (div <= 64) {
			N = 4;

			f0 = div / N;
			f1 = (div - f0) / (N - 1);
			f2 = (div - f0 - f1) / (N - 2);
			f3 = div - f0 - f1 - f2;
		} else {
			N = 4;

			f0 = 16;
			f1 = 16;
			f2 = 16;
			f3 = 16;
		}

		if (f0 == 16)
			f0 = 0;
		else if ((f0 < 9) && (f0 != 0))
			f0 = 9;

		if (f1 == 16)
			f1 = 0;
		else if ((f1 < 9) && (f1 != 0))
			f1 = 9;

		if (f2 == 16)
			f2 = 0;
		else if ((f2 < 9) && (f2 != 0))
			f2 = 9;

		if (f3 == 16)
			f3 = 0;
		else if ((f3 < 9) && (f3 != 0))
			f3 = 9;
	}

	sm = N - 1;

	/* Write to registers */
	//reg15 &= 0x01;
	//reg15 |= (pll_div_fb >> 8) & 0x01;

	//reg16 = pll_div_fb & 0xFF;

	reg1D &= ~0x03;
	reg1D |= sm;
	reg1D |= 0x80;

	reg1E = ((f3 << 4) + f2) & 0xFF;
	reg1F = ((f1 << 4) + f0) & 0xFF;

	m88ds3103b_dt_write(dev, 0x05, 0x40);
	m88ds3103b_dt_write(dev, 0x11, 0x08);
	m88ds3103b_dt_write(dev, 0x1D, reg1D);
	m88ds3103b_dt_write(dev, 0x1E, reg1E);
	m88ds3103b_dt_write(dev, 0x1F, reg1F);

	m88ds3103b_dt_write(dev, 0x17, 0xc1);
	m88ds3103b_dt_write(dev, 0x17, 0x81);

	usleep_range(5000, 5500);

	m88ds3103b_dt_write(dev, 0x05, 0x00);
	m88ds3103b_dt_write(dev, 0x11, 0x0A);

	usleep_range(5000, 5500);

	return 0;
}

static int m88ds3103_set_frontend(struct dvb_frontend *fe)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, len;
	const struct m88ds3103_reg_val *init;
	u8 u8tmp, u8tmp1 = 0, u8tmp2 = 0; /* silence compiler warning */
	u8 buf[3];
	u16 u16tmp;
	u32 tuner_frequency_khz, target_mclk, u32tmp;
	s32 s32tmp;
	static const struct reg_sequence reset_buf[] = {
		{0x07, 0x80}, {0x07, 0x00}
	};

	dev_dbg(&client->dev,
		"delivery_system=%d modulation=%d frequency=%u symbol_rate=%d inversion=%d pilot=%d rolloff=%d\n",
		c->delivery_system, c->modulation, c->frequency, c->symbol_rate,
		c->inversion, c->pilot, c->rolloff);

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	/* reset */
	ret = regmap_multi_reg_write(dev->regmap, reset_buf, 2);
	if (ret)
		goto err;

	/* Disable demod clock path */
	if (dev->chip_id == M88RS6000_CHIP_ID) {
		if (dev->chiptype == M88DS3103_CHIPTYPE_3103B) {
			ret = regmap_read(dev->regmap, 0xb2, &u32tmp);
			if (ret)
				goto err;
			if (u32tmp == 0x01) {
				ret = regmap_write(dev->regmap, 0x00, 0x00);
				if (ret)
					goto err;
				ret = regmap_write(dev->regmap, 0xb2, 0x00);
				if (ret)
					goto err;
			}
		}

		ret = regmap_write(dev->regmap, 0x06, 0xe0);
		if (ret)
			goto err;
	}

	/* program tuner */
	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (ret)
			goto err;
	}

	if (fe->ops.tuner_ops.get_frequency) {
		ret = fe->ops.tuner_ops.get_frequency(fe, &tuner_frequency_khz);
		if (ret)
			goto err;
	} else {
		/*
		 * Use nominal target frequency as tuner driver does not provide
		 * actual frequency used. Carrier offset calculation is not
		 * valid.
		 */
		tuner_frequency_khz = c->frequency;
	}

	/* set M88RS6000/DS3103B demod main mclk and ts mclk from tuner die */
	if (dev->chip_id == M88RS6000_CHIP_ID) {
		if (c->symbol_rate > 45010000)
			dev->mclk = 110250000;
		else
			dev->mclk = 96000000;

		if (c->delivery_system == SYS_DVBS)
			target_mclk = 96000000;
		else
			target_mclk = 144000000;

		if (dev->chiptype == M88DS3103_CHIPTYPE_3103B) {
			m88ds3103b_select_mclk(dev);
			m88ds3103b_set_mclk(dev, target_mclk / 1000);
		}

		/* Enable demod clock path */
		ret = regmap_write(dev->regmap, 0x06, 0x00);
		if (ret)
			goto err;
		usleep_range(10000, 20000);
	} else {
	/* set M88DS3103 mclk and ts mclk. */
		dev->mclk = 96000000;

		switch (dev->cfg->ts_mode) {
		case M88DS3103_TS_SERIAL:
		case M88DS3103_TS_SERIAL_D7:
			target_mclk = dev->cfg->ts_clk;
			break;
		case M88DS3103_TS_PARALLEL:
		case M88DS3103_TS_CI:
			if (c->delivery_system == SYS_DVBS)
				target_mclk = 96000000;
			else {
				if (c->symbol_rate < 18000000)
					target_mclk = 96000000;
				else if (c->symbol_rate < 28000000)
					target_mclk = 144000000;
				else
					target_mclk = 192000000;
			}
			break;
		default:
			dev_dbg(&client->dev, "invalid ts_mode\n");
			ret = -EINVAL;
			goto err;
		}

		switch (target_mclk) {
		case 96000000:
			u8tmp1 = 0x02; /* 0b10 */
			u8tmp2 = 0x01; /* 0b01 */
			break;
		case 144000000:
			u8tmp1 = 0x00; /* 0b00 */
			u8tmp2 = 0x01; /* 0b01 */
			break;
		case 192000000:
			u8tmp1 = 0x03; /* 0b11 */
			u8tmp2 = 0x00; /* 0b00 */
			break;
		}
		ret = m88ds3103_update_bits(dev, 0x22, 0xc0, u8tmp1 << 6);
		if (ret)
			goto err;
		ret = m88ds3103_update_bits(dev, 0x24, 0xc0, u8tmp2 << 6);
		if (ret)
			goto err;
	}

	ret = regmap_write(dev->regmap, 0xb2, 0x01);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x00, 0x01);
	if (ret)
		goto err;

	switch (c->delivery_system) {
	case SYS_DVBS:
		if (dev->chip_id == M88RS6000_CHIP_ID) {
			len = ARRAY_SIZE(m88rs6000_dvbs_init_reg_vals);
			init = m88rs6000_dvbs_init_reg_vals;
		} else {
			len = ARRAY_SIZE(m88ds3103_dvbs_init_reg_vals);
			init = m88ds3103_dvbs_init_reg_vals;
		}
		break;
	case SYS_DVBS2:
		if (dev->chip_id == M88RS6000_CHIP_ID) {
			len = ARRAY_SIZE(m88rs6000_dvbs2_init_reg_vals);
			init = m88rs6000_dvbs2_init_reg_vals;
		} else {
			len = ARRAY_SIZE(m88ds3103_dvbs2_init_reg_vals);
			init = m88ds3103_dvbs2_init_reg_vals;
		}
		break;
	default:
		dev_dbg(&client->dev, "invalid delivery_system\n");
		ret = -EINVAL;
		goto err;
	}

	/* program init table */
	if (c->delivery_system != dev->delivery_system) {
		ret = m88ds3103_wr_reg_val_tab(dev, init, len);
		if (ret)
			goto err;
	}

	if (dev->chip_id == M88RS6000_CHIP_ID) {
		if (c->delivery_system == SYS_DVBS2 &&
		    c->symbol_rate <= 5000000) {
			ret = regmap_write(dev->regmap, 0xc0, 0x04);
			if (ret)
				goto err;
			buf[0] = 0x09;
			buf[1] = 0x22;
			buf[2] = 0x88;
			ret = regmap_bulk_write(dev->regmap, 0x8a, buf, 3);
			if (ret)
				goto err;
		}
		ret = m88ds3103_update_bits(dev, 0x9d, 0x08, 0x08);
		if (ret)
			goto err;

		if (dev->chiptype == M88DS3103_CHIPTYPE_3103B) {
			buf[0] = m88ds3103b_dt_read(dev, 0x15);
			buf[1] = m88ds3103b_dt_read(dev, 0x16);

			if (c->symbol_rate > 45010000) {
				buf[0] &= ~0x03;
				buf[0] |= 0x02;
				buf[0] |= ((147 - 32) >> 8) & 0x01;
				buf[1] = (147 - 32) & 0xFF;

				dev->mclk = 110250 * 1000;
			} else {
				buf[0] &= ~0x03;
				buf[0] |= ((128 - 32) >> 8) & 0x01;
				buf[1] = (128 - 32) & 0xFF;

				dev->mclk = 96000 * 1000;
			}
			m88ds3103b_dt_write(dev, 0x15, buf[0]);
			m88ds3103b_dt_write(dev, 0x16, buf[1]);

			regmap_read(dev->regmap, 0x30, &u32tmp);
			u32tmp &= ~0x80;
			regmap_write(dev->regmap, 0x30, u32tmp & 0xff);
		}

		ret = regmap_write(dev->regmap, 0xf1, 0x01);
		if (ret)
			goto err;

		if (dev->chiptype != M88DS3103_CHIPTYPE_3103B) {
			ret = m88ds3103_update_bits(dev, 0x30, 0x80, 0x80);
			if (ret)
				goto err;
		}
	}

	switch (dev->cfg->ts_mode) {
	case M88DS3103_TS_SERIAL:
		u8tmp1 = 0x00;
		u8tmp = 0x06;
		break;
	case M88DS3103_TS_SERIAL_D7:
		u8tmp1 = 0x20;
		u8tmp = 0x06;
		break;
	case M88DS3103_TS_PARALLEL:
		u8tmp = 0x02;
		if (dev->chiptype == M88DS3103_CHIPTYPE_3103B) {
			u8tmp = 0x01;
			u8tmp1 = 0x01;
		}
		break;
	case M88DS3103_TS_CI:
		u8tmp = 0x03;
		break;
	default:
		dev_dbg(&client->dev, "invalid ts_mode\n");
		ret = -EINVAL;
		goto err;
	}

	if (dev->cfg->ts_clk_pol)
		u8tmp |= 0x40;

	/* TS mode */
	ret = regmap_write(dev->regmap, 0xfd, u8tmp);
	if (ret)
		goto err;

	switch (dev->cfg->ts_mode) {
	case M88DS3103_TS_SERIAL:
	case M88DS3103_TS_SERIAL_D7:
		ret = m88ds3103_update_bits(dev, 0x29, 0x20, u8tmp1);
		if (ret)
			goto err;
		u16tmp = 0;
		u8tmp1 = 0x3f;
		u8tmp2 = 0x3f;
		break;
	case M88DS3103_TS_PARALLEL:
		if (dev->chiptype == M88DS3103_CHIPTYPE_3103B) {
			ret = m88ds3103_update_bits(dev, 0x29, 0x01, u8tmp1);
			if (ret)
				goto err;
		}
		fallthrough;
	default:
		u16tmp = DIV_ROUND_UP(target_mclk, dev->cfg->ts_clk);
		u8tmp1 = u16tmp / 2 - 1;
		u8tmp2 = DIV_ROUND_UP(u16tmp, 2) - 1;
	}

	dev_dbg(&client->dev, "target_mclk=%u ts_clk=%u ts_clk_divide_ratio=%u\n",
		target_mclk, dev->cfg->ts_clk, u16tmp);

	/* u8tmp1[5:2] => fe[3:0], u8tmp1[1:0] => ea[7:6] */
	/* u8tmp2[5:0] => ea[5:0] */
	u8tmp = (u8tmp1 >> 2) & 0x0f;
	ret = regmap_update_bits(dev->regmap, 0xfe, 0x0f, u8tmp);
	if (ret)
		goto err;
	u8tmp = ((u8tmp1 & 0x03) << 6) | u8tmp2 >> 0;
	ret = regmap_write(dev->regmap, 0xea, u8tmp);
	if (ret)
		goto err;

	if (c->symbol_rate <= 3000000)
		u8tmp = 0x20;
	else if (c->symbol_rate <= 10000000)
		u8tmp = 0x10;
	else
		u8tmp = 0x06;

	if (dev->chiptype == M88DS3103_CHIPTYPE_3103B)
		m88ds3103b_set_mclk(dev, target_mclk / 1000);

	ret = regmap_write(dev->regmap, 0xc3, 0x08);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0xc8, u8tmp);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0xc4, 0x08);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0xc7, 0x00);
	if (ret)
		goto err;

	u16tmp = DIV_ROUND_CLOSEST_ULL((u64)c->symbol_rate * 0x10000, dev->mclk);
	buf[0] = (u16tmp >> 0) & 0xff;
	buf[1] = (u16tmp >> 8) & 0xff;
	ret = regmap_bulk_write(dev->regmap, 0x61, buf, 2);
	if (ret)
		goto err;

	ret = m88ds3103_update_bits(dev, 0x4d, 0x02, dev->cfg->spec_inv << 1);
	if (ret)
		goto err;

	ret = m88ds3103_update_bits(dev, 0x30, 0x10, dev->cfg->agc_inv << 4);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x33, dev->cfg->agc);
	if (ret)
		goto err;

	if (dev->chiptype == M88DS3103_CHIPTYPE_3103B) {
		/* enable/disable 192M LDPC clock */
		ret = m88ds3103_update_bits(dev, 0x29, 0x10,
				(c->delivery_system == SYS_DVBS) ? 0x10 : 0x0);
		if (ret)
			goto err;

		ret = m88ds3103_update_bits(dev, 0xc9, 0x08, 0x08);
		if (ret)
			goto err;
	}

	dev_dbg(&client->dev, "carrier offset=%d\n",
		(tuner_frequency_khz - c->frequency));

	/* Use 32-bit calc as there is no s64 version of DIV_ROUND_CLOSEST() */
	s32tmp = 0x10000 * (tuner_frequency_khz - c->frequency);
	s32tmp = DIV_ROUND_CLOSEST(s32tmp, dev->mclk / 1000);
	buf[0] = (s32tmp >> 0) & 0xff;
	buf[1] = (s32tmp >> 8) & 0xff;
	ret = regmap_bulk_write(dev->regmap, 0x5e, buf, 2);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x00, 0x00);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0xb2, 0x00);
	if (ret)
		goto err;

	dev->delivery_system = c->delivery_system;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ds3103_init(struct dvb_frontend *fe)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, len, rem;
	unsigned int utmp;
	const struct firmware *firmware;
	const char *name;

	dev_dbg(&client->dev, "\n");

	/* set cold state by default */
	dev->warm = false;

	/* wake up device from sleep */
	ret = m88ds3103_update_bits(dev, 0x08, 0x01, 0x01);
	if (ret)
		goto err;
	ret = m88ds3103_update_bits(dev, 0x04, 0x01, 0x00);
	if (ret)
		goto err;
	ret = m88ds3103_update_bits(dev, 0x23, 0x10, 0x00);
	if (ret)
		goto err;

	/* firmware status */
	ret = regmap_read(dev->regmap, 0xb9, &utmp);
	if (ret)
		goto err;

	dev_dbg(&client->dev, "firmware=%02x\n", utmp);

	if (utmp)
		goto warm;

	/* global reset, global diseqc reset, global fec reset */
	ret = regmap_write(dev->regmap, 0x07, 0xe0);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x07, 0x00);
	if (ret)
		goto err;

	/* cold state - try to download firmware */
	dev_info(&client->dev, "found a '%s' in cold state\n",
		 dev->fe.ops.info.name);

	if (dev->chiptype == M88DS3103_CHIPTYPE_3103B)
		name = M88DS3103B_FIRMWARE;
	else if (dev->chip_id == M88RS6000_CHIP_ID)
		name = M88RS6000_FIRMWARE;
	else
		name = M88DS3103_FIRMWARE;

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&firmware, name, &client->dev);
	if (ret) {
		dev_err(&client->dev, "firmware file '%s' not found\n", name);
		goto err;
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n", name);

	ret = regmap_write(dev->regmap, 0xb2, 0x01);
	if (ret)
		goto err_release_firmware;

	for (rem = firmware->size; rem > 0; rem -= (dev->cfg->i2c_wr_max - 1)) {
		len = min(dev->cfg->i2c_wr_max - 1, rem);
		ret = regmap_bulk_write(dev->regmap, 0xb0,
					&firmware->data[firmware->size - rem],
					len);
		if (ret) {
			dev_err(&client->dev, "firmware download failed %d\n",
				ret);
			goto err_release_firmware;
		}
	}

	ret = regmap_write(dev->regmap, 0xb2, 0x00);
	if (ret)
		goto err_release_firmware;

	release_firmware(firmware);

	ret = regmap_read(dev->regmap, 0xb9, &utmp);
	if (ret)
		goto err;

	if (!utmp) {
		ret = -EINVAL;
		dev_info(&client->dev, "firmware did not run\n");
		goto err;
	}

	dev_info(&client->dev, "found a '%s' in warm state\n",
		 dev->fe.ops.info.name);
	dev_info(&client->dev, "firmware version: %X.%X\n",
		 (utmp >> 4) & 0xf, (utmp >> 0 & 0xf));

	if (dev->chiptype == M88DS3103_CHIPTYPE_3103B) {
		m88ds3103b_dt_write(dev, 0x21, 0x92);
		m88ds3103b_dt_write(dev, 0x15, 0x6C);
		m88ds3103b_dt_write(dev, 0x17, 0xC1);
		m88ds3103b_dt_write(dev, 0x17, 0x81);
	}
warm:
	/* warm state */
	dev->warm = true;

	/* init stats here in order signal app which stats are supported */
	c->cnr.len = 1;
	c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.len = 1;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.len = 1;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	return 0;
err_release_firmware:
	release_firmware(firmware);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ds3103_sleep(struct dvb_frontend *fe)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;
	int ret;
	unsigned int utmp;

	dev_dbg(&client->dev, "\n");

	dev->fe_status = 0;
	dev->delivery_system = SYS_UNDEFINED;

	/* TS Hi-Z */
	if (dev->chip_id == M88RS6000_CHIP_ID)
		utmp = 0x29;
	else
		utmp = 0x27;
	ret = m88ds3103_update_bits(dev, utmp, 0x01, 0x00);
	if (ret)
		goto err;

	/* sleep */
	ret = m88ds3103_update_bits(dev, 0x08, 0x01, 0x00);
	if (ret)
		goto err;
	ret = m88ds3103_update_bits(dev, 0x04, 0x01, 0x01);
	if (ret)
		goto err;
	ret = m88ds3103_update_bits(dev, 0x23, 0x10, 0x10);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ds3103_get_frontend(struct dvb_frontend *fe,
				  struct dtv_frontend_properties *c)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;
	int ret;
	u8 buf[3];

	dev_dbg(&client->dev, "\n");

	if (!dev->warm || !(dev->fe_status & FE_HAS_LOCK)) {
		ret = 0;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBS:
		ret = regmap_bulk_read(dev->regmap, 0xe0, &buf[0], 1);
		if (ret)
			goto err;

		ret = regmap_bulk_read(dev->regmap, 0xe6, &buf[1], 1);
		if (ret)
			goto err;

		switch ((buf[0] >> 2) & 0x01) {
		case 0:
			c->inversion = INVERSION_OFF;
			break;
		case 1:
			c->inversion = INVERSION_ON;
			break;
		}

		switch ((buf[1] >> 5) & 0x07) {
		case 0:
			c->fec_inner = FEC_7_8;
			break;
		case 1:
			c->fec_inner = FEC_5_6;
			break;
		case 2:
			c->fec_inner = FEC_3_4;
			break;
		case 3:
			c->fec_inner = FEC_2_3;
			break;
		case 4:
			c->fec_inner = FEC_1_2;
			break;
		default:
			dev_dbg(&client->dev, "invalid fec_inner\n");
		}

		c->modulation = QPSK;

		break;
	case SYS_DVBS2:
		ret = regmap_bulk_read(dev->regmap, 0x7e, &buf[0], 1);
		if (ret)
			goto err;

		ret = regmap_bulk_read(dev->regmap, 0x89, &buf[1], 1);
		if (ret)
			goto err;

		ret = regmap_bulk_read(dev->regmap, 0xf2, &buf[2], 1);
		if (ret)
			goto err;

		switch ((buf[0] >> 0) & 0x0f) {
		case 2:
			c->fec_inner = FEC_2_5;
			break;
		case 3:
			c->fec_inner = FEC_1_2;
			break;
		case 4:
			c->fec_inner = FEC_3_5;
			break;
		case 5:
			c->fec_inner = FEC_2_3;
			break;
		case 6:
			c->fec_inner = FEC_3_4;
			break;
		case 7:
			c->fec_inner = FEC_4_5;
			break;
		case 8:
			c->fec_inner = FEC_5_6;
			break;
		case 9:
			c->fec_inner = FEC_8_9;
			break;
		case 10:
			c->fec_inner = FEC_9_10;
			break;
		default:
			dev_dbg(&client->dev, "invalid fec_inner\n");
		}

		switch ((buf[0] >> 5) & 0x01) {
		case 0:
			c->pilot = PILOT_OFF;
			break;
		case 1:
			c->pilot = PILOT_ON;
			break;
		}

		switch ((buf[0] >> 6) & 0x07) {
		case 0:
			c->modulation = QPSK;
			break;
		case 1:
			c->modulation = PSK_8;
			break;
		case 2:
			c->modulation = APSK_16;
			break;
		case 3:
			c->modulation = APSK_32;
			break;
		default:
			dev_dbg(&client->dev, "invalid modulation\n");
		}

		switch ((buf[1] >> 7) & 0x01) {
		case 0:
			c->inversion = INVERSION_OFF;
			break;
		case 1:
			c->inversion = INVERSION_ON;
			break;
		}

		switch ((buf[2] >> 0) & 0x03) {
		case 0:
			c->rolloff = ROLLOFF_35;
			break;
		case 1:
			c->rolloff = ROLLOFF_25;
			break;
		case 2:
			c->rolloff = ROLLOFF_20;
			break;
		default:
			dev_dbg(&client->dev, "invalid rolloff\n");
		}
		break;
	default:
		dev_dbg(&client->dev, "invalid delivery_system\n");
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_bulk_read(dev->regmap, 0x6d, buf, 2);
	if (ret)
		goto err;

	c->symbol_rate = DIV_ROUND_CLOSEST_ULL((u64)(buf[1] << 8 | buf[0] << 0) * dev->mclk, 0x10000);

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ds3103_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->cnr.stat[0].scale == FE_SCALE_DECIBEL)
		*snr = div_s64(c->cnr.stat[0].svalue, 100);
	else
		*snr = 0;

	return 0;
}

static int m88ds3103_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;

	*ber = dev->dvbv3_ber;

	return 0;
}

static int m88ds3103_set_tone(struct dvb_frontend *fe,
	enum fe_sec_tone_mode fe_sec_tone_mode)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;
	int ret;
	unsigned int utmp, tone, reg_a1_mask;

	dev_dbg(&client->dev, "fe_sec_tone_mode=%d\n", fe_sec_tone_mode);

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	switch (fe_sec_tone_mode) {
	case SEC_TONE_ON:
		tone = 0;
		reg_a1_mask = 0x47;
		break;
	case SEC_TONE_OFF:
		tone = 1;
		reg_a1_mask = 0x00;
		break;
	default:
		dev_dbg(&client->dev, "invalid fe_sec_tone_mode\n");
		ret = -EINVAL;
		goto err;
	}

	utmp = tone << 7 | dev->cfg->envelope_mode << 5;
	ret = m88ds3103_update_bits(dev, 0xa2, 0xe0, utmp);
	if (ret)
		goto err;

	utmp = 1 << 2;
	ret = m88ds3103_update_bits(dev, 0xa1, reg_a1_mask, utmp);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ds3103_set_voltage(struct dvb_frontend *fe,
	enum fe_sec_voltage fe_sec_voltage)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;
	int ret;
	unsigned int utmp;
	bool voltage_sel, voltage_dis;

	dev_dbg(&client->dev, "fe_sec_voltage=%d\n", fe_sec_voltage);

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	switch (fe_sec_voltage) {
	case SEC_VOLTAGE_18:
		voltage_sel = true;
		voltage_dis = false;
		break;
	case SEC_VOLTAGE_13:
		voltage_sel = false;
		voltage_dis = false;
		break;
	case SEC_VOLTAGE_OFF:
		voltage_sel = false;
		voltage_dis = true;
		break;
	default:
		dev_dbg(&client->dev, "invalid fe_sec_voltage\n");
		ret = -EINVAL;
		goto err;
	}

	/* output pin polarity */
	voltage_sel ^= dev->cfg->lnb_hv_pol;
	voltage_dis ^= dev->cfg->lnb_en_pol;

	utmp = voltage_dis << 1 | voltage_sel << 0;
	ret = m88ds3103_update_bits(dev, 0xa2, 0x03, utmp);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ds3103_diseqc_send_master_cmd(struct dvb_frontend *fe,
		struct dvb_diseqc_master_cmd *diseqc_cmd)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;
	int ret;
	unsigned int utmp;
	unsigned long timeout;

	dev_dbg(&client->dev, "msg=%*ph\n",
		diseqc_cmd->msg_len, diseqc_cmd->msg);

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	if (diseqc_cmd->msg_len < 3 || diseqc_cmd->msg_len > 6) {
		ret = -EINVAL;
		goto err;
	}

	utmp = dev->cfg->envelope_mode << 5;
	ret = m88ds3103_update_bits(dev, 0xa2, 0xe0, utmp);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0xa3, diseqc_cmd->msg,
			diseqc_cmd->msg_len);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0xa1,
			(diseqc_cmd->msg_len - 1) << 3 | 0x07);
	if (ret)
		goto err;

	/* wait DiSEqC TX ready */
	#define SEND_MASTER_CMD_TIMEOUT 120
	timeout = jiffies + msecs_to_jiffies(SEND_MASTER_CMD_TIMEOUT);

	/* DiSEqC message period is 13.5 ms per byte */
	utmp = diseqc_cmd->msg_len * 13500;
	usleep_range(utmp - 4000, utmp);

	for (utmp = 1; !time_after(jiffies, timeout) && utmp;) {
		ret = regmap_read(dev->regmap, 0xa1, &utmp);
		if (ret)
			goto err;
		utmp = (utmp >> 6) & 0x1;
	}

	if (utmp == 0) {
		dev_dbg(&client->dev, "diseqc tx took %u ms\n",
			jiffies_to_msecs(jiffies) -
			(jiffies_to_msecs(timeout) - SEND_MASTER_CMD_TIMEOUT));
	} else {
		dev_dbg(&client->dev, "diseqc tx timeout\n");

		ret = m88ds3103_update_bits(dev, 0xa1, 0xc0, 0x40);
		if (ret)
			goto err;
	}

	ret = m88ds3103_update_bits(dev, 0xa2, 0xc0, 0x80);
	if (ret)
		goto err;

	if (utmp == 1) {
		ret = -ETIMEDOUT;
		goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ds3103_diseqc_send_burst(struct dvb_frontend *fe,
	enum fe_sec_mini_cmd fe_sec_mini_cmd)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;
	int ret;
	unsigned int utmp, burst;
	unsigned long timeout;

	dev_dbg(&client->dev, "fe_sec_mini_cmd=%d\n", fe_sec_mini_cmd);

	if (!dev->warm) {
		ret = -EAGAIN;
		goto err;
	}

	utmp = dev->cfg->envelope_mode << 5;
	ret = m88ds3103_update_bits(dev, 0xa2, 0xe0, utmp);
	if (ret)
		goto err;

	switch (fe_sec_mini_cmd) {
	case SEC_MINI_A:
		burst = 0x02;
		break;
	case SEC_MINI_B:
		burst = 0x01;
		break;
	default:
		dev_dbg(&client->dev, "invalid fe_sec_mini_cmd\n");
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_write(dev->regmap, 0xa1, burst);
	if (ret)
		goto err;

	/* wait DiSEqC TX ready */
	#define SEND_BURST_TIMEOUT 40
	timeout = jiffies + msecs_to_jiffies(SEND_BURST_TIMEOUT);

	/* DiSEqC ToneBurst period is 12.5 ms */
	usleep_range(8500, 12500);

	for (utmp = 1; !time_after(jiffies, timeout) && utmp;) {
		ret = regmap_read(dev->regmap, 0xa1, &utmp);
		if (ret)
			goto err;
		utmp = (utmp >> 6) & 0x1;
	}

	if (utmp == 0) {
		dev_dbg(&client->dev, "diseqc tx took %u ms\n",
			jiffies_to_msecs(jiffies) -
			(jiffies_to_msecs(timeout) - SEND_BURST_TIMEOUT));
	} else {
		dev_dbg(&client->dev, "diseqc tx timeout\n");

		ret = m88ds3103_update_bits(dev, 0xa1, 0xc0, 0x40);
		if (ret)
			goto err;
	}

	ret = m88ds3103_update_bits(dev, 0xa2, 0xc0, 0x80);
	if (ret)
		goto err;

	if (utmp == 1) {
		ret = -ETIMEDOUT;
		goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88ds3103_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 3000;

	return 0;
}

static void m88ds3103_release(struct dvb_frontend *fe)
{
	struct m88ds3103_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->client;

	i2c_unregister_device(client);
}

static int m88ds3103_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct m88ds3103_dev *dev = i2c_mux_priv(muxc);
	struct i2c_client *client = dev->client;
	int ret;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = "\x03\x11",
	};

	/* Open tuner I2C repeater for 1 xfer, closes automatically */
	ret = __i2c_transfer(client->adapter, &msg, 1);
	if (ret != 1) {
		dev_warn(&client->dev, "i2c wr failed=%d\n", ret);
		if (ret >= 0)
			ret = -EREMOTEIO;
		return ret;
	}

	return 0;
}

/*
 * XXX: That is wrapper to m88ds3103_probe() via driver core in order to provide
 * proper I2C client for legacy media attach binding.
 * New users must use I2C client binding directly!
 */
struct dvb_frontend *m88ds3103_attach(const struct m88ds3103_config *cfg,
				      struct i2c_adapter *i2c,
				      struct i2c_adapter **tuner_i2c_adapter)
{
	struct i2c_client *client;
	struct i2c_board_info board_info;
	struct m88ds3103_platform_data pdata = {};

	pdata.clk = cfg->clock;
	pdata.i2c_wr_max = cfg->i2c_wr_max;
	pdata.ts_mode = cfg->ts_mode;
	pdata.ts_clk = cfg->ts_clk;
	pdata.ts_clk_pol = cfg->ts_clk_pol;
	pdata.spec_inv = cfg->spec_inv;
	pdata.agc = cfg->agc;
	pdata.agc_inv = cfg->agc_inv;
	pdata.clk_out = cfg->clock_out;
	pdata.envelope_mode = cfg->envelope_mode;
	pdata.lnb_hv_pol = cfg->lnb_hv_pol;
	pdata.lnb_en_pol = cfg->lnb_en_pol;
	pdata.attach_in_use = true;

	memset(&board_info, 0, sizeof(board_info));
	strscpy(board_info.type, "m88ds3103", I2C_NAME_SIZE);
	board_info.addr = cfg->i2c_addr;
	board_info.platform_data = &pdata;
	client = i2c_new_client_device(i2c, &board_info);
	if (!i2c_client_has_driver(client))
		return NULL;

	*tuner_i2c_adapter = pdata.get_i2c_adapter(client);
	return pdata.get_dvb_frontend(client);
}
EXPORT_SYMBOL_GPL(m88ds3103_attach);

static const struct dvb_frontend_ops m88ds3103_ops = {
	.delsys = {SYS_DVBS, SYS_DVBS2},
	.info = {
		.name = "Montage Technology M88DS3103",
		.frequency_min_hz =  950 * MHz,
		.frequency_max_hz = 2150 * MHz,
		.frequency_tolerance_hz = 5 * MHz,
		.symbol_rate_min =  1000000,
		.symbol_rate_max = 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_8_9 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_RECOVER |
			FE_CAN_2G_MODULATION
	},

	.release = m88ds3103_release,

	.get_tune_settings = m88ds3103_get_tune_settings,

	.init = m88ds3103_init,
	.sleep = m88ds3103_sleep,

	.set_frontend = m88ds3103_set_frontend,
	.get_frontend = m88ds3103_get_frontend,

	.read_status = m88ds3103_read_status,
	.read_snr = m88ds3103_read_snr,
	.read_ber = m88ds3103_read_ber,

	.diseqc_send_master_cmd = m88ds3103_diseqc_send_master_cmd,
	.diseqc_send_burst = m88ds3103_diseqc_send_burst,

	.set_tone = m88ds3103_set_tone,
	.set_voltage = m88ds3103_set_voltage,
};

static struct dvb_frontend *m88ds3103_get_dvb_frontend(struct i2c_client *client)
{
	struct m88ds3103_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	return &dev->fe;
}

static struct i2c_adapter *m88ds3103_get_i2c_adapter(struct i2c_client *client)
{
	struct m88ds3103_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	return dev->muxc->adapter[0];
}

static int m88ds3103_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct m88ds3103_dev *dev;
	struct m88ds3103_platform_data *pdata = client->dev.platform_data;
	int ret;
	unsigned int utmp;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	dev->client = client;
	dev->config.clock = pdata->clk;
	dev->config.i2c_wr_max = pdata->i2c_wr_max;
	dev->config.ts_mode = pdata->ts_mode;
	dev->config.ts_clk = pdata->ts_clk * 1000;
	dev->config.ts_clk_pol = pdata->ts_clk_pol;
	dev->config.spec_inv = pdata->spec_inv;
	dev->config.agc_inv = pdata->agc_inv;
	dev->config.clock_out = pdata->clk_out;
	dev->config.envelope_mode = pdata->envelope_mode;
	dev->config.agc = pdata->agc;
	dev->config.lnb_hv_pol = pdata->lnb_hv_pol;
	dev->config.lnb_en_pol = pdata->lnb_en_pol;
	dev->cfg = &dev->config;
	/* create regmap */
	dev->regmap_config.reg_bits = 8;
	dev->regmap_config.val_bits = 8;
	dev->regmap_config.lock_arg = dev;
	dev->regmap = devm_regmap_init_i2c(client, &dev->regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

	/* 0x00: chip id[6:0], 0x01: chip ver[7:0], 0x02: chip ver[15:8] */
	ret = regmap_read(dev->regmap, 0x00, &utmp);
	if (ret)
		goto err_kfree;

	dev->chip_id = utmp >> 1;
	dev->chiptype = (u8)id->driver_data;

	dev_dbg(&client->dev, "chip_id=%02x\n", dev->chip_id);

	switch (dev->chip_id) {
	case M88RS6000_CHIP_ID:
	case M88DS3103_CHIP_ID:
		break;
	default:
		ret = -ENODEV;
		dev_err(&client->dev, "Unknown device. Chip_id=%02x\n", dev->chip_id);
		goto err_kfree;
	}

	switch (dev->cfg->clock_out) {
	case M88DS3103_CLOCK_OUT_DISABLED:
		utmp = 0x80;
		break;
	case M88DS3103_CLOCK_OUT_ENABLED:
		utmp = 0x00;
		break;
	case M88DS3103_CLOCK_OUT_ENABLED_DIV2:
		utmp = 0x10;
		break;
	default:
		ret = -EINVAL;
		goto err_kfree;
	}

	if (!pdata->ts_clk) {
		ret = -EINVAL;
		goto err_kfree;
	}

	/* 0x29 register is defined differently for m88rs6000. */
	/* set internal tuner address to 0x21 */
	if (dev->chip_id == M88RS6000_CHIP_ID)
		utmp = 0x00;

	ret = regmap_write(dev->regmap, 0x29, utmp);
	if (ret)
		goto err_kfree;

	/* sleep */
	ret = m88ds3103_update_bits(dev, 0x08, 0x01, 0x00);
	if (ret)
		goto err_kfree;
	ret = m88ds3103_update_bits(dev, 0x04, 0x01, 0x01);
	if (ret)
		goto err_kfree;
	ret = m88ds3103_update_bits(dev, 0x23, 0x10, 0x10);
	if (ret)
		goto err_kfree;

	/* create mux i2c adapter for tuner */
	dev->muxc = i2c_mux_alloc(client->adapter, &client->dev, 1, 0, 0,
				  m88ds3103_select, NULL);
	if (!dev->muxc) {
		ret = -ENOMEM;
		goto err_kfree;
	}
	dev->muxc->priv = dev;
	ret = i2c_mux_add_adapter(dev->muxc, 0, 0, 0);
	if (ret)
		goto err_kfree;

	/* create dvb_frontend */
	memcpy(&dev->fe.ops, &m88ds3103_ops, sizeof(struct dvb_frontend_ops));
	if (dev->chiptype == M88DS3103_CHIPTYPE_3103B)
		strscpy(dev->fe.ops.info.name, "Montage Technology M88DS3103B",
			sizeof(dev->fe.ops.info.name));
	else if (dev->chip_id == M88RS6000_CHIP_ID)
		strscpy(dev->fe.ops.info.name, "Montage Technology M88RS6000",
			sizeof(dev->fe.ops.info.name));
	if (!pdata->attach_in_use)
		dev->fe.ops.release = NULL;
	dev->fe.demodulator_priv = dev;
	i2c_set_clientdata(client, dev);

	/* setup callbacks */
	pdata->get_dvb_frontend = m88ds3103_get_dvb_frontend;
	pdata->get_i2c_adapter = m88ds3103_get_i2c_adapter;

	if (dev->chiptype == M88DS3103_CHIPTYPE_3103B) {
		/* enable i2c repeater for tuner */
		m88ds3103_update_bits(dev, 0x11, 0x01, 0x01);

		/* get frontend address */
		ret = regmap_read(dev->regmap, 0x29, &utmp);
		if (ret)
			goto err_kfree;
		dev->dt_addr = ((utmp & 0x80) == 0) ? 0x42 >> 1 : 0x40 >> 1;
		dev_dbg(&client->dev, "dt addr is 0x%02x\n", dev->dt_addr);

		dev->dt_client = i2c_new_dummy_device(client->adapter,
						      dev->dt_addr);
		if (IS_ERR(dev->dt_client)) {
			ret = PTR_ERR(dev->dt_client);
			goto err_kfree;
		}
	}

	return 0;
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static void m88ds3103_remove(struct i2c_client *client)
{
	struct m88ds3103_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	if (dev->dt_client)
		i2c_unregister_device(dev->dt_client);

	i2c_mux_del_adapters(dev->muxc);

	kfree(dev);
}

static const struct i2c_device_id m88ds3103_id_table[] = {
	{"m88ds3103",  M88DS3103_CHIPTYPE_3103},
	{"m88rs6000",  M88DS3103_CHIPTYPE_RS6000},
	{"m88ds3103b", M88DS3103_CHIPTYPE_3103B},
	{}
};
MODULE_DEVICE_TABLE(i2c, m88ds3103_id_table);

static struct i2c_driver m88ds3103_driver = {
	.driver = {
		.name	= "m88ds3103",
		.suppress_bind_attrs = true,
	},
	.probe		= m88ds3103_probe,
	.remove		= m88ds3103_remove,
	.id_table	= m88ds3103_id_table,
};

module_i2c_driver(m88ds3103_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Montage Technology M88DS3103 DVB-S/S2 demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(M88DS3103_FIRMWARE);
MODULE_FIRMWARE(M88RS6000_FIRMWARE);
MODULE_FIRMWARE(M88DS3103B_FIRMWARE);
