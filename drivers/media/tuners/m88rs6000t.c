/*
 * Driver for the internal tuner of Montage M88RS6000
 *
 * Copyright (C) 2014 Max nibble <nibble.max@gmail.com>
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
 */

#include "m88rs6000t.h"
#include <linux/regmap.h>

struct m88rs6000t_dev {
	struct m88rs6000t_config cfg;
	struct i2c_client *client;
	struct regmap *regmap;
	u32 frequency_khz;
};

struct m88rs6000t_reg_val {
	u8 reg;
	u8 val;
};

/* set demod main mclk and ts mclk */
static int m88rs6000t_set_demod_mclk(struct dvb_frontend *fe)
{
	struct m88rs6000t_dev *dev = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 reg11, reg15, reg16, reg1D, reg1E, reg1F;
	u8 N, f0 = 0, f1 = 0, f2 = 0, f3 = 0;
	u16 pll_div_fb;
	u32 div, ts_mclk;
	unsigned int utmp;
	int ret;

	/* select demod main mclk */
	ret = regmap_read(dev->regmap, 0x15, &utmp);
	if (ret)
		goto err;
	reg15 = utmp;
	if (c->symbol_rate > 45010000) {
		reg11 = 0x0E;
		reg15 |= 0x02;
		reg16 = 115; /* mclk = 110.25MHz */
	} else {
		reg11 = 0x0A;
		reg15 &= ~0x02;
		reg16 = 96; /* mclk = 96MHz */
	}

	/* set ts mclk */
	if (c->delivery_system == SYS_DVBS)
		ts_mclk = 96000;
	else
		ts_mclk = 144000;

	pll_div_fb = (reg15 & 0x01) << 8;
	pll_div_fb += reg16;
	pll_div_fb += 32;

	div = 36000 * pll_div_fb;
	div /= ts_mclk;

	if (div <= 32) {
		N = 2;
		f0 = 0;
		f1 = div / 2;
		f2 = div - f1;
		f3 = 0;
	} else if (div <= 48) {
		N = 3;
		f0 = div / 3;
		f1 = (div - f0) / 2;
		f2 = div - f0 - f1;
		f3 = 0;
	} else if (div <= 64) {
		N = 4;
		f0 = div / 4;
		f1 = (div - f0) / 3;
		f2 = (div - f0 - f1) / 2;
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
	if (f1 == 16)
		f1 = 0;
	if (f2 == 16)
		f2 = 0;
	if (f3 == 16)
		f3 = 0;

	ret = regmap_read(dev->regmap, 0x1D, &utmp);
	if (ret)
		goto err;
	reg1D = utmp;
	reg1D &= ~0x03;
	reg1D |= N - 1;
	reg1E = ((f3 << 4) + f2) & 0xFF;
	reg1F = ((f1 << 4) + f0) & 0xFF;

	/* program and recalibrate demod PLL */
	ret = regmap_write(dev->regmap, 0x05, 0x40);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x11, 0x08);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x15, reg15);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x16, reg16);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x1D, reg1D);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x1E, reg1E);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x1F, reg1F);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x17, 0xc1);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x17, 0x81);
	if (ret)
		goto err;
	usleep_range(5000, 50000);
	ret = regmap_write(dev->regmap, 0x05, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x11, reg11);
	if (ret)
		goto err;
	usleep_range(5000, 50000);
err:
	if (ret)
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88rs6000t_set_pll_freq(struct m88rs6000t_dev *dev,
			u32 tuner_freq_MHz)
{
	u32 fcry_KHz, ulNDiv1, ulNDiv2, ulNDiv;
	u8 refDiv, ucLoDiv1, ucLomod1, ucLoDiv2, ucLomod2, ucLoDiv, ucLomod;
	u8 reg27, reg29, reg42, reg42buf;
	unsigned int utmp;
	int ret;

	fcry_KHz = 27000; /* in kHz */
	refDiv = 27;

	ret = regmap_write(dev->regmap, 0x36, (refDiv - 8));
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x31, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x2c, 0x02);
	if (ret)
		goto err;

	if (tuner_freq_MHz >= 1550) {
		ucLoDiv1 = 2;
		ucLomod1 = 0;
		ucLoDiv2 = 2;
		ucLomod2 = 0;
	} else if (tuner_freq_MHz >= 1380) {
		ucLoDiv1 = 3;
		ucLomod1 = 16;
		ucLoDiv2 = 2;
		ucLomod2 = 0;
	} else if (tuner_freq_MHz >= 1070) {
		ucLoDiv1 = 3;
		ucLomod1 = 16;
		ucLoDiv2 = 3;
		ucLomod2 = 16;
	} else if (tuner_freq_MHz >= 1000) {
		ucLoDiv1 = 3;
		ucLomod1 = 16;
		ucLoDiv2 = 4;
		ucLomod2 = 64;
	} else if (tuner_freq_MHz >= 775) {
		ucLoDiv1 = 4;
		ucLomod1 = 64;
		ucLoDiv2 = 4;
		ucLomod2 = 64;
	} else if (tuner_freq_MHz >= 700) {
		ucLoDiv1 = 6;
		ucLomod1 = 48;
		ucLoDiv2 = 4;
		ucLomod2 = 64;
	} else if (tuner_freq_MHz >= 520) {
		ucLoDiv1 = 6;
		ucLomod1 = 48;
		ucLoDiv2 = 6;
		ucLomod2 = 48;
	} else {
		ucLoDiv1 = 8;
		ucLomod1 = 96;
		ucLoDiv2 = 8;
		ucLomod2 = 96;
	}

	ulNDiv1 = ((tuner_freq_MHz * ucLoDiv1 * 1000) * refDiv
			/ fcry_KHz - 1024) / 2;
	ulNDiv2 = ((tuner_freq_MHz * ucLoDiv2 * 1000) * refDiv
			/ fcry_KHz - 1024) / 2;

	reg27 = (((ulNDiv1 >> 8) & 0x0F) + ucLomod1) & 0x7F;
	ret = regmap_write(dev->regmap, 0x27, reg27);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x28, (u8)(ulNDiv1 & 0xFF));
	if (ret)
		goto err;
	reg29 = (((ulNDiv2 >> 8) & 0x0F) + ucLomod2) & 0x7f;
	ret = regmap_write(dev->regmap, 0x29, reg29);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x2a, (u8)(ulNDiv2 & 0xFF));
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x2F, 0xf5);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x30, 0x05);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x08, 0x1f);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x08, 0x3f);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x09, 0x20);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x09, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x3e, 0x11);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x08, 0x2f);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x08, 0x3f);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x09, 0x10);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x09, 0x00);
	if (ret)
		goto err;
	usleep_range(2000, 50000);

	ret = regmap_read(dev->regmap, 0x42, &utmp);
	if (ret)
		goto err;
	reg42 = utmp;

	ret = regmap_write(dev->regmap, 0x3e, 0x10);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x08, 0x2f);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x08, 0x3f);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x09, 0x10);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x09, 0x00);
	if (ret)
		goto err;
	usleep_range(2000, 50000);

	ret = regmap_read(dev->regmap, 0x42, &utmp);
	if (ret)
		goto err;
	reg42buf = utmp;
	if (reg42buf < reg42) {
		ret = regmap_write(dev->regmap, 0x3e, 0x11);
		if (ret)
			goto err;
	}
	usleep_range(5000, 50000);

	ret = regmap_read(dev->regmap, 0x2d, &utmp);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x2d, utmp);
	if (ret)
		goto err;
	ret = regmap_read(dev->regmap, 0x2e, &utmp);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x2e, utmp);
	if (ret)
		goto err;

	ret = regmap_read(dev->regmap, 0x27, &utmp);
	if (ret)
		goto err;
	reg27 = utmp & 0x70;
	ret = regmap_read(dev->regmap, 0x83, &utmp);
	if (ret)
		goto err;
	if (reg27 == (utmp & 0x70)) {
		ucLoDiv	= ucLoDiv1;
		ulNDiv = ulNDiv1;
		ucLomod = ucLomod1 / 16;
	} else {
		ucLoDiv	= ucLoDiv2;
		ulNDiv = ulNDiv2;
		ucLomod = ucLomod2 / 16;
	}

	if ((ucLoDiv == 3) || (ucLoDiv == 6)) {
		refDiv = 18;
		ret = regmap_write(dev->regmap, 0x36, (refDiv - 8));
		if (ret)
			goto err;
		ulNDiv = ((tuner_freq_MHz * ucLoDiv * 1000) * refDiv
				/ fcry_KHz - 1024) / 2;
	}

	reg27 = (0x80 + ((ucLomod << 4) & 0x70)
			+ ((ulNDiv >> 8) & 0x0F)) & 0xFF;
	ret = regmap_write(dev->regmap, 0x27, reg27);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x28, (u8)(ulNDiv & 0xFF));
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x29, 0x80);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x31, 0x03);
	if (ret)
		goto err;

	if (ucLoDiv == 3)
		utmp = 0xCE;
	else
		utmp = 0x8A;
	ret = regmap_write(dev->regmap, 0x3b, utmp);
	if (ret)
		goto err;

	dev->frequency_khz = fcry_KHz * (ulNDiv * 2 + 1024) / refDiv / ucLoDiv;

	dev_dbg(&dev->client->dev,
		"actual tune frequency=%d\n", dev->frequency_khz);
err:
	if (ret)
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88rs6000t_set_bb(struct m88rs6000t_dev *dev,
		u32 symbol_rate_KSs, s32 lpf_offset_KHz)
{
	u32 f3dB;
	u8  reg40;

	f3dB = symbol_rate_KSs * 9 / 14 + 2000;
	f3dB += lpf_offset_KHz;
	f3dB = clamp_val(f3dB, 6000U, 43000U);
	reg40 = f3dB / 1000;
	return regmap_write(dev->regmap, 0x40, reg40);
}

static int m88rs6000t_set_params(struct dvb_frontend *fe)
{
	struct m88rs6000t_dev *dev = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	s32 lpf_offset_KHz;
	u32 realFreq, freq_MHz;

	dev_dbg(&dev->client->dev,
			"frequency=%d symbol_rate=%d\n",
			c->frequency, c->symbol_rate);

	if (c->symbol_rate < 5000000)
		lpf_offset_KHz = 3000;
	else
		lpf_offset_KHz = 0;

	realFreq = c->frequency + lpf_offset_KHz;
	/* set tuner pll.*/
	freq_MHz = (realFreq + 500) / 1000;
	ret = m88rs6000t_set_pll_freq(dev, freq_MHz);
	if (ret)
		goto err;
	ret = m88rs6000t_set_bb(dev, c->symbol_rate / 1000, lpf_offset_KHz);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x00, 0x01);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x00, 0x00);
	if (ret)
		goto err;
	/* set demod mlck */
	ret = m88rs6000t_set_demod_mclk(fe);
err:
	if (ret)
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88rs6000t_init(struct dvb_frontend *fe)
{
	struct m88rs6000t_dev *dev = fe->tuner_priv;
	int ret;

	dev_dbg(&dev->client->dev, "%s:\n", __func__);

	ret = regmap_update_bits(dev->regmap, 0x11, 0x08, 0x08);
	if (ret)
		goto err;
	usleep_range(5000, 50000);
	ret = regmap_update_bits(dev->regmap, 0x10, 0x01, 0x01);
	if (ret)
		goto err;
	usleep_range(10000, 50000);
	ret = regmap_write(dev->regmap, 0x07, 0x7d);
err:
	if (ret)
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);
	return ret;
}

static int m88rs6000t_sleep(struct dvb_frontend *fe)
{
	struct m88rs6000t_dev *dev = fe->tuner_priv;
	int ret;

	dev_dbg(&dev->client->dev, "%s:\n", __func__);

	ret = regmap_write(dev->regmap, 0x07, 0x6d);
	if (ret) {
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);
		return ret;
	}
	usleep_range(5000, 10000);
	return 0;
}

static int m88rs6000t_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct m88rs6000t_dev *dev = fe->tuner_priv;

	dev_dbg(&dev->client->dev, "\n");

	*frequency = dev->frequency_khz;
	return 0;
}

static int m88rs6000t_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct m88rs6000t_dev *dev = fe->tuner_priv;

	dev_dbg(&dev->client->dev, "\n");

	*frequency = 0; /* Zero-IF */
	return 0;
}


static int m88rs6000t_get_rf_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct m88rs6000t_dev *dev = fe->tuner_priv;
	unsigned int val, i;
	int ret;
	u16 gain;
	u32 PGA2_cri_GS = 46, PGA2_crf_GS = 290, TIA_GS = 290;
	u32 RF_GC = 1200, IF_GC = 1100, BB_GC = 300;
	u32 PGA2_GC = 300, TIA_GC = 300, PGA2_cri = 0, PGA2_crf = 0;
	u32 RFG = 0, IFG = 0, BBG = 0, PGA2G = 0, TIAG = 0;
	u32 RFGS[13] = {0, 245, 266, 268, 270, 285,
			298, 295, 283, 285, 285, 300, 300};
	u32 IFGS[12] = {0, 300, 230, 270, 270, 285,
			295, 285, 290, 295, 295, 310};
	u32 BBGS[14] = {0, 286, 275, 290, 294, 300, 290,
			290, 285, 283, 260, 295, 290, 260};

	ret = regmap_read(dev->regmap, 0x5A, &val);
	if (ret)
		goto err;
	RF_GC = val & 0x0f;

	ret = regmap_read(dev->regmap, 0x5F, &val);
	if (ret)
		goto err;
	IF_GC = val & 0x0f;

	ret = regmap_read(dev->regmap, 0x3F, &val);
	if (ret)
		goto err;
	TIA_GC = (val >> 4) & 0x07;

	ret = regmap_read(dev->regmap, 0x77, &val);
	if (ret)
		goto err;
	BB_GC = (val >> 4) & 0x0f;

	ret = regmap_read(dev->regmap, 0x76, &val);
	if (ret)
		goto err;
	PGA2_GC = val & 0x3f;
	PGA2_cri = PGA2_GC >> 2;
	PGA2_crf = PGA2_GC & 0x03;

	for (i = 0; i <= RF_GC; i++)
		RFG += RFGS[i];

	if (RF_GC == 0)
		RFG += 400;
	if (RF_GC == 1)
		RFG += 300;
	if (RF_GC == 2)
		RFG += 200;
	if (RF_GC == 3)
		RFG += 100;

	for (i = 0; i <= IF_GC; i++)
		IFG += IFGS[i];

	TIAG = TIA_GC * TIA_GS;

	for (i = 0; i <= BB_GC; i++)
		BBG += BBGS[i];

	PGA2G = PGA2_cri * PGA2_cri_GS + PGA2_crf * PGA2_crf_GS;

	gain = RFG + IFG - TIAG + BBG + PGA2G;

	/* scale value to 0x0000-0xffff */
	gain = clamp_val(gain, 1000U, 10500U);
	*strength = (10500 - gain) * 0xffff / (10500 - 1000);
err:
	if (ret)
		dev_dbg(&dev->client->dev, "failed=%d\n", ret);
	return ret;
}

static const struct dvb_tuner_ops m88rs6000t_tuner_ops = {
	.info = {
		.name          = "Montage M88RS6000 Internal Tuner",
		.frequency_min = 950000,
		.frequency_max = 2150000,
	},

	.init = m88rs6000t_init,
	.sleep = m88rs6000t_sleep,
	.set_params = m88rs6000t_set_params,
	.get_frequency = m88rs6000t_get_frequency,
	.get_if_frequency = m88rs6000t_get_if_frequency,
	.get_rf_strength = m88rs6000t_get_rf_strength,
};

static int m88rs6000t_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct m88rs6000t_config *cfg = client->dev.platform_data;
	struct dvb_frontend *fe = cfg->fe;
	struct m88rs6000t_dev *dev;
	int ret, i;
	unsigned int utmp;
	static const struct regmap_config regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};
	static const struct m88rs6000t_reg_val reg_vals[] = {
		{0x10, 0xfb},
		{0x24, 0x38},
		{0x11, 0x0a},
		{0x12, 0x00},
		{0x2b, 0x1c},
		{0x44, 0x48},
		{0x54, 0x24},
		{0x55, 0x06},
		{0x59, 0x00},
		{0x5b, 0x4c},
		{0x60, 0x8b},
		{0x61, 0xf4},
		{0x65, 0x07},
		{0x6d, 0x6f},
		{0x6e, 0x31},
		{0x3c, 0xf3},
		{0x37, 0x0f},
		{0x48, 0x28},
		{0x49, 0xd8},
		{0x70, 0x66},
		{0x71, 0xCF},
		{0x72, 0x81},
		{0x73, 0xA7},
		{0x74, 0x4F},
		{0x75, 0xFC},
	};

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		dev_err(&client->dev, "kzalloc() failed\n");
		goto err;
	}

	memcpy(&dev->cfg, cfg, sizeof(struct m88rs6000t_config));
	dev->client = client;
	dev->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err;
	}

	ret = regmap_update_bits(dev->regmap, 0x11, 0x08, 0x08);
	if (ret)
		goto err;
	usleep_range(5000, 50000);
	ret = regmap_update_bits(dev->regmap, 0x10, 0x01, 0x01);
	if (ret)
		goto err;
	usleep_range(10000, 50000);
	ret = regmap_write(dev->regmap, 0x07, 0x7d);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x04, 0x01);
	if (ret)
		goto err;

	/* check tuner chip id */
	ret = regmap_read(dev->regmap, 0x01, &utmp);
	if (ret)
		goto err;
	dev_info(&dev->client->dev, "chip_id=%02x\n", utmp);
	if (utmp != 0x64) {
		ret = -ENODEV;
		goto err;
	}

	/* tuner init. */
	ret = regmap_write(dev->regmap, 0x05, 0x40);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x11, 0x08);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x15, 0x6c);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x17, 0xc1);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x17, 0x81);
	if (ret)
		goto err;
	usleep_range(10000, 50000);
	ret = regmap_write(dev->regmap, 0x05, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap, 0x11, 0x0a);
	if (ret)
		goto err;

	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		ret = regmap_write(dev->regmap,
				reg_vals[i].reg, reg_vals[i].val);
		if (ret)
			goto err;
	}

	dev_info(&dev->client->dev, "Montage M88RS6000 internal tuner successfully identified\n");

	fe->tuner_priv = dev;
	memcpy(&fe->ops.tuner_ops, &m88rs6000t_tuner_ops,
			sizeof(struct dvb_tuner_ops));
	i2c_set_clientdata(client, dev);
	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	kfree(dev);
	return ret;
}

static int m88rs6000t_remove(struct i2c_client *client)
{
	struct m88rs6000t_dev *dev = i2c_get_clientdata(client);
	struct dvb_frontend *fe = dev->cfg.fe;

	dev_dbg(&client->dev, "\n");

	memset(&fe->ops.tuner_ops, 0, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = NULL;
	kfree(dev);

	return 0;
}

static const struct i2c_device_id m88rs6000t_id[] = {
	{"m88rs6000t", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, m88rs6000t_id);

static struct i2c_driver m88rs6000t_driver = {
	.driver = {
		.name	= "m88rs6000t",
	},
	.probe		= m88rs6000t_probe,
	.remove		= m88rs6000t_remove,
	.id_table	= m88rs6000t_id,
};

module_i2c_driver(m88rs6000t_driver);

MODULE_AUTHOR("Max nibble <nibble.max@gmail.com>");
MODULE_DESCRIPTION("Montage M88RS6000 internal tuner driver");
MODULE_LICENSE("GPL");
