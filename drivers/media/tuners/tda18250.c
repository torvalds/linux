/*
 * NXP TDA18250 silicon tuner driver
 *
 * Copyright (C) 2017 Olli Salonen <olli.salonen@iki.fi>
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
 */

#include "tda18250_priv.h"
#include <linux/regmap.h>

static const struct dvb_tuner_ops tda18250_ops;

static int tda18250_power_control(struct dvb_frontend *fe,
		unsigned int power_state)
{
	struct i2c_client *client = fe->tuner_priv;
	struct tda18250_dev *dev = i2c_get_clientdata(client);
	int ret;
	unsigned int utmp;

	dev_dbg(&client->dev, "power state: %d", power_state);

	switch (power_state) {
	case TDA18250_POWER_NORMAL:
		ret = regmap_write_bits(dev->regmap, R06_POWER2, 0x07, 0x00);
		if (ret)
			goto err;
		ret = regmap_write_bits(dev->regmap, R25_REF, 0xc0, 0xc0);
		if (ret)
			goto err;
		break;
	case TDA18250_POWER_STANDBY:
		if (dev->loopthrough) {
			ret = regmap_write_bits(dev->regmap,
					R25_REF, 0xc0, 0x80);
			if (ret)
				goto err;
			ret = regmap_write_bits(dev->regmap,
					R06_POWER2, 0x07, 0x02);
			if (ret)
				goto err;
			ret = regmap_write_bits(dev->regmap,
					R10_LT1, 0x80, 0x00);
			if (ret)
				goto err;
		} else {
			ret = regmap_write_bits(dev->regmap,
					R25_REF, 0xc0, 0x80);
			if (ret)
				goto err;
			ret = regmap_write_bits(dev->regmap,
					R06_POWER2, 0x07, 0x01);
			if (ret)
				goto err;
			ret = regmap_read(dev->regmap,
					R0D_AGC12, &utmp);
			if (ret)
				goto err;
			ret = regmap_write_bits(dev->regmap,
					R0D_AGC12, 0x03, 0x03);
			if (ret)
				goto err;
			ret = regmap_write_bits(dev->regmap,
					R10_LT1, 0x80, 0x80);
			if (ret)
				goto err;
			ret = regmap_write_bits(dev->regmap,
					R0D_AGC12, 0x03, utmp & 0x03);
			if (ret)
				goto err;
		}
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	return ret;
}

static int tda18250_wait_for_irq(struct dvb_frontend *fe,
		int maxwait, int step, u8 irq)
{
	struct i2c_client *client = fe->tuner_priv;
	struct tda18250_dev *dev = i2c_get_clientdata(client);
	int ret;
	unsigned long timeout;
	bool triggered;
	unsigned int utmp;

	triggered = false;
	timeout = jiffies + msecs_to_jiffies(maxwait);
	while (!time_after(jiffies, timeout)) {
		// check for the IRQ
		ret = regmap_read(dev->regmap, R08_IRQ1, &utmp);
		if (ret)
			goto err;
		if ((utmp & irq) == irq) {
			triggered = true;
			break;
		}
		msleep(step);
	}

	dev_dbg(&client->dev, "waited IRQ (0x%02x) %d ms, triggered: %s", irq,
			jiffies_to_msecs(jiffies) -
			(jiffies_to_msecs(timeout) - maxwait),
			triggered ? "true" : "false");

	if (!triggered)
		return -ETIMEDOUT;

	return 0;
err:
	return ret;
}

static int tda18250_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->tuner_priv;
	struct tda18250_dev *dev = i2c_get_clientdata(client);
	int ret, i;

	/* default values for various regs */
	static const u8 init_regs[][2] = {
		{ R0C_AGC11, 0xc7 },
		{ R0D_AGC12, 0x5d },
		{ R0E_AGC13, 0x40 },
		{ R0F_AGC14, 0x0e },
		{ R10_LT1, 0x47 },
		{ R11_LT2, 0x4e },
		{ R12_AGC21, 0x26 },
		{ R13_AGC22, 0x60 },
		{ R18_AGC32, 0x37 },
		{ R19_AGC33, 0x09 },
		{ R1A_AGCK, 0x00 },
		{ R1E_WI_FI, 0x29 },
		{ R1F_RF_BPF, 0x06 },
		{ R20_IR_MIX, 0xc6 },
		{ R21_IF_AGC, 0x00 },
		{ R2C_PS1, 0x75 },
		{ R2D_PS2, 0x06 },
		{ R2E_PS3, 0x07 },
		{ R30_RSSI2, 0x0e },
		{ R31_IRQ_CTRL, 0x00 },
		{ R39_SD5, 0x00 },
		{ R3B_REGU, 0x55 },
		{ R3C_RCCAL1, 0xa7 },
		{ R3F_IRCAL2, 0x85 },
		{ R40_IRCAL3, 0x87 },
		{ R41_IRCAL4, 0xc0 },
		{ R43_PD1, 0x40 },
		{ R44_PD2, 0xc0 },
		{ R46_CPUMP, 0x0c },
		{ R47_LNAPOL, 0x64 },
		{ R4B_XTALOSC1, 0x30 },
		{ R59_AGC2_UP2, 0x05 },
		{ R5B_AGC_AUTO, 0x07 },
		{ R5C_AGC_DEBUG, 0x00 },
	};

	/* crystal related regs depend on frequency */
	static const u8 xtal_regs[][5] = {
					/* reg:   4d    4e    4f    50    51 */
		[TDA18250_XTAL_FREQ_16MHZ]  = { 0x3e, 0x80, 0x50, 0x00, 0x20 },
		[TDA18250_XTAL_FREQ_24MHZ]  = { 0x5d, 0xc0, 0xec, 0x00, 0x18 },
		[TDA18250_XTAL_FREQ_25MHZ]  = { 0x61, 0xa8, 0xec, 0x80, 0x19 },
		[TDA18250_XTAL_FREQ_27MHZ]  = { 0x69, 0x78, 0x8d, 0x80, 0x1b },
		[TDA18250_XTAL_FREQ_30MHZ]  = { 0x75, 0x30, 0x8f, 0x00, 0x1e },
	};

	dev_dbg(&client->dev, "\n");

	ret = tda18250_power_control(fe, TDA18250_POWER_NORMAL);
	if (ret)
		goto err;

	msleep(20);

	if (dev->warm)
		goto warm;

	/* set initial register values */
	for (i = 0; i < ARRAY_SIZE(init_regs); i++) {
		ret = regmap_write(dev->regmap, init_regs[i][0],
				init_regs[i][1]);
		if (ret)
			goto err;
	}

	/* set xtal related regs */
	ret = regmap_bulk_write(dev->regmap, R4D_XTALFLX1,
			xtal_regs[dev->xtal_freq], 5);
	if (ret)
		goto err;

	ret = regmap_write_bits(dev->regmap, R10_LT1, 0x80,
			dev->loopthrough ? 0x00 : 0x80);
	if (ret)
		goto err;

	/* clear IRQ */
	ret = regmap_write(dev->regmap, R0A_IRQ3, TDA18250_IRQ_HW_INIT);
	if (ret)
		goto err;

	/* start HW init */
	ret = regmap_write(dev->regmap, R2A_MSM1, 0x70);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, R2B_MSM2, 0x01);
	if (ret)
		goto err;

	ret = tda18250_wait_for_irq(fe, 500, 10, TDA18250_IRQ_HW_INIT);
	if (ret)
		goto err;

	/* tuner calibration */
	ret = regmap_write(dev->regmap, R2A_MSM1, 0x02);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, R2B_MSM2, 0x01);
	if (ret)
		goto err;

	ret = tda18250_wait_for_irq(fe, 500, 10, TDA18250_IRQ_CAL);
	if (ret)
		goto err;

	dev->warm = true;

warm:
	/* power up LNA */
	ret = regmap_write_bits(dev->regmap, R0C_AGC11, 0x80, 0x00);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d", ret);
	return ret;
}

static int tda18250_set_agc(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->tuner_priv;
	struct tda18250_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	u8 utmp, utmp2;

	dev_dbg(&client->dev, "\n");

	ret = regmap_write_bits(dev->regmap, R1F_RF_BPF, 0x87, 0x06);
	if (ret)
		goto err;

	utmp = ((c->frequency < 100000000) &&
			((c->delivery_system == SYS_DVBC_ANNEX_A) ||
			(c->delivery_system == SYS_DVBC_ANNEX_C)) &&
			(c->bandwidth_hz == 6000000)) ? 0x80 : 0x00;
	ret = regmap_write(dev->regmap, R5A_H3H5, utmp);
	if (ret)
		goto err;

	/* AGC1 */
	switch (c->delivery_system) {
	case SYS_ATSC:
	case SYS_DVBT:
	case SYS_DVBT2:
		utmp = 4;
		break;
	default: /* DVB-C/QAM */
		switch (c->bandwidth_hz) {
		case 6000000:
			utmp = (c->frequency < 800000000) ? 6 : 4;
			break;
		default: /* 7.935 and 8 MHz */
			utmp = (c->frequency < 100000000) ? 2 : 3;
			break;
		}
		break;
	}

	ret = regmap_write_bits(dev->regmap, R0C_AGC11, 0x07, utmp);
	if (ret)
		goto err;

	/* AGC2 */
	switch (c->delivery_system) {
	case SYS_ATSC:
	case SYS_DVBT:
	case SYS_DVBT2:
		utmp = (c->frequency < 320000000) ? 20 : 16;
		utmp2 = (c->frequency < 320000000) ? 22 : 18;
		break;
	default: /* DVB-C/QAM */
		switch (c->bandwidth_hz) {
		case 6000000:
			if (c->frequency < 600000000) {
				utmp = 18;
				utmp2 = 22;
			} else if (c->frequency < 800000000) {
				utmp = 16;
				utmp2 = 20;
			} else {
				utmp = 14;
				utmp2 = 16;
			}
			break;
		default: /* 7.935 and 8 MHz */
			utmp = (c->frequency < 320000000) ? 16 : 18;
			utmp2 = (c->frequency < 320000000) ? 18 : 20;
			break;
		}
		break;
	}
	ret = regmap_write_bits(dev->regmap, R58_AGC2_UP1, 0x1f, utmp2+8);
	if (ret)
		goto err;
	ret = regmap_write_bits(dev->regmap, R13_AGC22, 0x1f, utmp);
	if (ret)
		goto err;
	ret = regmap_write_bits(dev->regmap, R14_AGC23, 0x1f, utmp2);
	if (ret)
		goto err;

	switch (c->delivery_system) {
	case SYS_ATSC:
	case SYS_DVBT:
	case SYS_DVBT2:
		utmp = 98;
		break;
	default: /* DVB-C/QAM */
		utmp = 90;
		break;
	}
	ret = regmap_write_bits(dev->regmap, R16_AGC25, 0xf8, utmp);
	if (ret)
		goto err;

	ret = regmap_write_bits(dev->regmap, R12_AGC21, 0x60,
			(c->frequency > 800000000) ? 0x40 : 0x20);
	if (ret)
		goto err;

	/* AGC3 */
	switch (c->delivery_system) {
	case SYS_ATSC:
	case SYS_DVBT:
	case SYS_DVBT2:
		utmp = (c->frequency < 320000000) ? 5 : 7;
		utmp2 = (c->frequency < 320000000) ? 10 : 12;
		break;
	default: /* DVB-C/QAM */
		utmp = 7;
		utmp2 = 12;
		break;
	}
	ret = regmap_write(dev->regmap, R17_AGC31, (utmp << 4) | utmp2);
	if (ret)
		goto err;

	/* S2D */
	switch (c->delivery_system) {
	case SYS_ATSC:
	case SYS_DVBT:
	case SYS_DVBT2:
		if (c->bandwidth_hz == 8000000)
			utmp = 0x04;
		else
			utmp = (c->frequency < 320000000) ? 0x04 : 0x02;
		break;
	default: /* DVB-C/QAM */
		if (c->bandwidth_hz == 6000000)
			utmp = ((c->frequency > 172544000) &&
				(c->frequency < 320000000)) ? 0x04 : 0x02;
		else /* 7.935 and 8 MHz */
			utmp = ((c->frequency > 320000000) &&
				(c->frequency < 600000000)) ? 0x02 : 0x04;
		break;
	}
	ret = regmap_write_bits(dev->regmap, R20_IR_MIX, 0x06, utmp);
	if (ret)
		goto err;

	switch (c->delivery_system) {
	case SYS_ATSC:
	case SYS_DVBT:
	case SYS_DVBT2:
		utmp = 0;
		break;
	default: /* DVB-C/QAM */
		utmp = (c->frequency < 600000000) ? 0 : 3;
		break;
	}
	ret = regmap_write_bits(dev->regmap, R16_AGC25, 0x03, utmp);
	if (ret)
		goto err;

	utmp = 0x09;
	switch (c->delivery_system) {
	case SYS_ATSC:
	case SYS_DVBT:
	case SYS_DVBT2:
		if (c->bandwidth_hz == 8000000)
			utmp = 0x0c;
		break;
	default: /* DVB-C/QAM */
		utmp = 0x0c;
		break;
	}
	ret = regmap_write_bits(dev->regmap, R0F_AGC14, 0x3f, utmp);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d", ret);
	return ret;
}

static int tda18250_pll_calc(struct dvb_frontend *fe, u8 *rdiv,
		u8 *ndiv, u8 *icp)
{
	struct i2c_client *client = fe->tuner_priv;
	struct tda18250_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	unsigned int uval, exp, lopd, scale;
	unsigned long fvco;

	ret = regmap_read(dev->regmap, R34_MD1, &uval);
	if (ret)
		goto err;

	exp = (uval & 0x70) >> 4;
	if (exp > 5)
		exp = 0;
	lopd = 1 << (exp - 1);
	scale = uval & 0x0f;
	fvco = lopd * scale * ((c->frequency / 1000) + dev->if_frequency);

	switch (dev->xtal_freq) {
	case TDA18250_XTAL_FREQ_16MHZ:
		*rdiv = 1;
		*ndiv = 0;
		*icp = (fvco < 6622000) ? 0x05 : 0x02;
	break;
	case TDA18250_XTAL_FREQ_24MHZ:
	case TDA18250_XTAL_FREQ_25MHZ:
		*rdiv = 3;
		*ndiv = 1;
		*icp = (fvco < 6622000) ? 0x05 : 0x02;
	break;
	case TDA18250_XTAL_FREQ_27MHZ:
		if (fvco < 6643000) {
			*rdiv = 2;
			*ndiv = 0;
			*icp = 0x05;
		} else if (fvco < 6811000) {
			*rdiv = 2;
			*ndiv = 0;
			*icp = 0x06;
		} else {
			*rdiv = 3;
			*ndiv = 1;
			*icp = 0x02;
		}
	break;
	case TDA18250_XTAL_FREQ_30MHZ:
		*rdiv = 2;
		*ndiv = 0;
		*icp = (fvco < 6811000) ? 0x05 : 0x02;
	break;
	default:
		return -EINVAL;
	}

	dev_dbg(&client->dev,
			"lopd=%d scale=%u fvco=%lu, rdiv=%d ndiv=%d icp=%d",
			lopd, scale, fvco, *rdiv, *ndiv, *icp);
	return 0;
err:
	return ret;
}

static int tda18250_set_params(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->tuner_priv;
	struct tda18250_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 if_khz;
	int ret;
	unsigned int i, j;
	u8 utmp;
	u8 buf[3];

	#define REG      0
	#define MASK     1
	#define DVBT_6   2
	#define DVBT_7   3
	#define DVBT_8   4
	#define DVBC_6   5
	#define DVBC_8   6
	#define ATSC     7

	static const u8 delsys_params[][16] = {
		[REG]    = { 0x22, 0x23, 0x24, 0x21, 0x0d, 0x0c, 0x0f, 0x14,
			     0x0e, 0x12, 0x58, 0x59, 0x1a, 0x19, 0x1e, 0x30 },
		[MASK]   = { 0x77, 0xff, 0xff, 0x87, 0xf0, 0x78, 0x07, 0xe0,
			     0x60, 0x0f, 0x60, 0x0f, 0x33, 0x30, 0x80, 0x06 },
		[DVBT_6] = { 0x51, 0x03, 0x83, 0x82, 0x40, 0x48, 0x01, 0xe0,
			     0x60, 0x0f, 0x60, 0x05, 0x03, 0x10, 0x00, 0x04 },
		[DVBT_7] = { 0x52, 0x03, 0x85, 0x82, 0x40, 0x48, 0x01, 0xe0,
			     0x60, 0x0f, 0x60, 0x05, 0x03, 0x10, 0x00, 0x04 },
		[DVBT_8] = { 0x53, 0x03, 0x87, 0x82, 0x40, 0x48, 0x06, 0xe0,
			     0x60, 0x07, 0x60, 0x05, 0x03, 0x10, 0x00, 0x04 },
		[DVBC_6] = { 0x32, 0x05, 0x86, 0x82, 0x50, 0x00, 0x06, 0x60,
			     0x40, 0x0e, 0x60, 0x05, 0x33, 0x10, 0x00, 0x04 },
		[DVBC_8] = { 0x53, 0x03, 0x88, 0x82, 0x50, 0x00, 0x06, 0x60,
			     0x40, 0x0e, 0x60, 0x05, 0x33, 0x10, 0x00, 0x04 },
		[ATSC]   = { 0x51, 0x03, 0x83, 0x82, 0x40, 0x48, 0x01, 0xe0,
			     0x40, 0x0e, 0x60, 0x05, 0x03, 0x00, 0x80, 0x04 },
	};

	dev_dbg(&client->dev,
			"delivery_system=%d frequency=%u bandwidth_hz=%u",
			c->delivery_system, c->frequency, c->bandwidth_hz);


	switch (c->delivery_system) {
	case SYS_ATSC:
		j = ATSC;
		if_khz = dev->if_atsc;
		break;
	case SYS_DVBT:
	case SYS_DVBT2:
		if (c->bandwidth_hz == 0) {
			ret = -EINVAL;
			goto err;
		} else if (c->bandwidth_hz <= 6000000) {
			j = DVBT_6;
			if_khz = dev->if_dvbt_6;
		} else if (c->bandwidth_hz <= 7000000) {
			j = DVBT_7;
			if_khz = dev->if_dvbt_7;
		} else if (c->bandwidth_hz <= 8000000) {
			j = DVBT_8;
			if_khz = dev->if_dvbt_8;
		} else {
			ret = -EINVAL;
			goto err;
		}
		break;
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
		if (c->bandwidth_hz == 0) {
			ret = -EINVAL;
			goto err;
		} else if (c->bandwidth_hz <= 6000000) {
			j = DVBC_6;
			if_khz = dev->if_dvbc_6;
		} else if (c->bandwidth_hz <= 8000000) {
			j = DVBC_8;
			if_khz = dev->if_dvbc_8;
		} else {
			ret = -EINVAL;
			goto err;
		}
		break;
	default:
		ret = -EINVAL;
		dev_err(&client->dev, "unsupported delivery system=%d",
				c->delivery_system);
		goto err;
	}

	/* set delivery system dependent registers */
	for (i = 0; i < 16; i++) {
		ret = regmap_write_bits(dev->regmap, delsys_params[REG][i],
			 delsys_params[MASK][i],  delsys_params[j][i]);
		if (ret)
			goto err;
	}

	/* set IF if needed */
	if (dev->if_frequency != if_khz) {
		utmp = DIV_ROUND_CLOSEST(if_khz, 50);
		ret = regmap_write(dev->regmap, R26_IF, utmp);
		if (ret)
			goto err;
		dev->if_frequency = if_khz;
		dev_dbg(&client->dev, "set IF=%u kHz", if_khz);

	}

	ret = tda18250_set_agc(fe);
	if (ret)
		goto err;

	ret = regmap_write_bits(dev->regmap, R1A_AGCK, 0x03, 0x01);
	if (ret)
		goto err;

	ret = regmap_write_bits(dev->regmap, R14_AGC23, 0x40, 0x00);
	if (ret)
		goto err;

	/* set frequency */
	buf[0] = ((c->frequency / 1000) >> 16) & 0xff;
	buf[1] = ((c->frequency / 1000) >>  8) & 0xff;
	buf[2] = ((c->frequency / 1000) >>  0) & 0xff;
	ret = regmap_bulk_write(dev->regmap, R27_RF1, buf, 3);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, R0A_IRQ3, TDA18250_IRQ_TUNE);
	if (ret)
		goto err;

	/* initial tune */
	ret = regmap_write(dev->regmap, R2A_MSM1, 0x01);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, R2B_MSM2, 0x01);
	if (ret)
		goto err;

	ret = tda18250_wait_for_irq(fe, 500, 10, TDA18250_IRQ_TUNE);
	if (ret)
		goto err;

	/* calc ndiv and rdiv */
	ret = tda18250_pll_calc(fe, &buf[0], &buf[1], &buf[2]);
	if (ret)
		goto err;

	ret = regmap_write_bits(dev->regmap, R4F_XTALFLX3, 0xe0,
			(buf[0] << 6) | (buf[1] << 5));
	if (ret)
		goto err;

	/* clear IRQ */
	ret = regmap_write(dev->regmap, R0A_IRQ3, TDA18250_IRQ_TUNE);
	if (ret)
		goto err;

	ret = regmap_write_bits(dev->regmap, R46_CPUMP, 0x07, 0x00);
	if (ret)
		goto err;

	ret = regmap_write_bits(dev->regmap, R39_SD5, 0x03, 0x00);
	if (ret)
		goto err;

	/* tune again */
	ret = regmap_write(dev->regmap, R2A_MSM1, 0x01); /* tune */
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, R2B_MSM2, 0x01); /* go */
	if (ret)
		goto err;

	ret = tda18250_wait_for_irq(fe, 500, 10, TDA18250_IRQ_TUNE);
	if (ret)
		goto err;

	/* pll locking */
	msleep(20);

	ret = regmap_write_bits(dev->regmap, R2B_MSM2, 0x04, 0x04);
	if (ret)
		goto err;

	msleep(20);

	/* restore AGCK */
	ret = regmap_write_bits(dev->regmap, R1A_AGCK, 0x03, 0x03);
	if (ret)
		goto err;

	ret = regmap_write_bits(dev->regmap, R14_AGC23, 0x40, 0x40);
	if (ret)
		goto err;

	/* charge pump */
	ret = regmap_write_bits(dev->regmap, R46_CPUMP, 0x07, buf[2]);

	return 0;
err:
	return ret;
}

static int tda18250_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct i2c_client *client = fe->tuner_priv;
	struct tda18250_dev *dev = i2c_get_clientdata(client);

	*frequency = dev->if_frequency * 1000;
	return 0;
}

static int tda18250_sleep(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->tuner_priv;
	struct tda18250_dev *dev = i2c_get_clientdata(client);
	int ret;

	dev_dbg(&client->dev, "\n");

	/* power down LNA */
	ret = regmap_write_bits(dev->regmap, R0C_AGC11, 0x80, 0x00);
	if (ret)
		return ret;

	/* set if freq to 0 in order to make sure it's set after wake up */
	dev->if_frequency = 0;

	ret = tda18250_power_control(fe, TDA18250_POWER_STANDBY);
	return ret;
}

static const struct dvb_tuner_ops tda18250_ops = {
	.info = {
		.name           = "NXP TDA18250",
		.frequency_min  = 42000000,
		.frequency_max  = 870000000,
	},

	.init = tda18250_init,
	.set_params = tda18250_set_params,
	.get_if_frequency = tda18250_get_if_frequency,
	.sleep = tda18250_sleep,
};

static int tda18250_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct tda18250_config *cfg = client->dev.platform_data;
	struct dvb_frontend *fe = cfg->fe;
	struct tda18250_dev *dev;
	int ret;
	unsigned char chip_id[3];

	/* some registers are always read from HW */
	static const struct regmap_range tda18250_yes_ranges[] = {
		regmap_reg_range(R05_POWER1, R0B_IRQ4),
		regmap_reg_range(R21_IF_AGC, R21_IF_AGC),
		regmap_reg_range(R2A_MSM1, R2B_MSM2),
		regmap_reg_range(R2F_RSSI1, R31_IRQ_CTRL),
	};

	static const struct regmap_access_table tda18250_volatile_table = {
		.yes_ranges = tda18250_yes_ranges,
		.n_yes_ranges = ARRAY_SIZE(tda18250_yes_ranges),
	};

	static const struct regmap_config tda18250_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = TDA18250_NUM_REGS - 1,
		.volatile_table = &tda18250_volatile_table,
	};

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	i2c_set_clientdata(client, dev);

	dev->fe = cfg->fe;
	dev->loopthrough = cfg->loopthrough;
	if (cfg->xtal_freq < TDA18250_XTAL_FREQ_MAX) {
		dev->xtal_freq = cfg->xtal_freq;
	} else {
		ret = -EINVAL;
		dev_err(&client->dev, "xtal_freq invalid=%d", cfg->xtal_freq);
		goto err_kfree;
	}
	dev->if_dvbt_6 = cfg->if_dvbt_6;
	dev->if_dvbt_7 = cfg->if_dvbt_7;
	dev->if_dvbt_8 = cfg->if_dvbt_8;
	dev->if_dvbc_6 = cfg->if_dvbc_6;
	dev->if_dvbc_8 = cfg->if_dvbc_8;
	dev->if_atsc = cfg->if_atsc;

	dev->if_frequency = 0;
	dev->warm = false;

	dev->regmap = devm_regmap_init_i2c(client, &tda18250_regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

	/* read the three chip ID registers */
	regmap_bulk_read(dev->regmap, R00_ID1, &chip_id, 3);
	dev_dbg(&client->dev, "chip_id=%02x:%02x:%02x",
			chip_id[0], chip_id[1], chip_id[2]);

	switch (chip_id[0]) {
	case 0xc7:
		dev->slave = false;
		break;
	case 0x47:
		dev->slave = true;
		break;
	default:
		ret = -ENODEV;
		goto err_kfree;
	}

	if (chip_id[1] != 0x4a) {
		ret = -ENODEV;
		goto err_kfree;
	}

	switch (chip_id[2]) {
	case 0x20:
		dev_info(&client->dev,
				"NXP TDA18250AHN/%s successfully identified",
				dev->slave ? "S" : "M");
		break;
	case 0x21:
		dev_info(&client->dev,
				"NXP TDA18250BHN/%s successfully identified",
				dev->slave ? "S" : "M");
		break;
	default:
		ret = -ENODEV;
		goto err_kfree;
	}

	fe->tuner_priv = client;
	memcpy(&fe->ops.tuner_ops, &tda18250_ops,
			sizeof(struct dvb_tuner_ops));

	/* put the tuner in standby */
	tda18250_power_control(fe, TDA18250_POWER_STANDBY);

	return 0;
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d", ret);
	return ret;
}

static int tda18250_remove(struct i2c_client *client)
{
	struct tda18250_dev *dev = i2c_get_clientdata(client);
	struct dvb_frontend *fe = dev->fe;

	dev_dbg(&client->dev, "\n");

	memset(&fe->ops.tuner_ops, 0, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = NULL;
	kfree(dev);

	return 0;
}

static const struct i2c_device_id tda18250_id_table[] = {
	{"tda18250", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, tda18250_id_table);

static struct i2c_driver tda18250_driver = {
	.driver = {
		.name	= "tda18250",
	},
	.probe		= tda18250_probe,
	.remove		= tda18250_remove,
	.id_table	= tda18250_id_table,
};

module_i2c_driver(tda18250_driver);

MODULE_DESCRIPTION("NXP TDA18250 silicon tuner driver");
MODULE_AUTHOR("Olli Salonen <olli.salonen@iki.fi>");
MODULE_LICENSE("GPL");
