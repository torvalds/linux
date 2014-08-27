/*
 * ITE Tech IT9137 silicon tuner driver
 *
 *  Copyright (C) 2011 Malcolm Priestley (tvboxspy@gmail.com)
 *  IT9137 Copyright (C) ITE Tech Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#include "it913x_priv.h"

struct it913x_dev {
	struct i2c_client *client;
	struct regmap *regmap;
	struct dvb_frontend *fe;
	u8 chip_ver:2;
	u8 role:2;
	u8 firmware_ver;
	u16 tun_xtal;
	u8 tun_fdiv;
	u8 tun_clk_mode;
	u32 tun_fn_min;
};

static int it913x_script_loader(struct it913x_dev *dev,
		struct it913xset *loadscript)
{
	int ret, i;

	if (loadscript == NULL)
		return -EINVAL;

	for (i = 0; i < 1000; ++i) {
		if (loadscript[i].address == 0x000000)
			break;
		ret = regmap_bulk_write(dev->regmap, loadscript[i].address,
			loadscript[i].reg, loadscript[i].count);
		if (ret < 0)
			return -ENODEV;
	}
	return 0;
}

static int it913x_init(struct dvb_frontend *fe)
{
	struct it913x_dev *dev = fe->tuner_priv;
	int ret, i;
	unsigned int reg;
	u8 val, nv_val;
	u8 nv[] = {48, 32, 24, 16, 12, 8, 6, 4, 2};
	u8 b[2];

	ret = regmap_read(dev->regmap, 0x80ec86, &reg);
	switch (reg) {
	case 0:
		dev->tun_clk_mode = reg;
		dev->tun_xtal = 2000;
		dev->tun_fdiv = 3;
		val = 16;
		break;
	case 1:
	default: /* I/O error too */
		dev->tun_clk_mode = reg;
		dev->tun_xtal = 640;
		dev->tun_fdiv = 1;
		val = 6;
		break;
	}

	ret = regmap_read(dev->regmap, 0x80ed03,  &reg);

	if (reg < 0)
		return -ENODEV;
	else if (reg < ARRAY_SIZE(nv))
		nv_val = nv[reg];
	else
		nv_val = 2;

	for (i = 0; i < 50; i++) {
		ret = regmap_bulk_read(dev->regmap, 0x80ed23, &b[0], sizeof(b));
		reg = (b[1] << 8) + b[0];
		if (reg > 0)
			break;
		if (ret)
			return -ENODEV;
		udelay(2000);
	}
	dev->tun_fn_min = dev->tun_xtal * reg;
	dev->tun_fn_min /= (dev->tun_fdiv * nv_val);
	dev_dbg(&dev->client->dev, "Tuner fn_min %d\n", dev->tun_fn_min);

	if (dev->chip_ver > 1)
		msleep(50);
	else {
		for (i = 0; i < 50; i++) {
			ret = regmap_read(dev->regmap, 0x80ec82, &reg);
			if (ret < 0)
				return -ENODEV;
			if (reg > 0)
				break;
			udelay(2000);
		}
	}

	/* Power Up Tuner - common all versions */
	ret = regmap_write(dev->regmap, 0x80ec40, 0x1);
	ret |= regmap_write(dev->regmap, 0x80ec57, 0x0);
	ret |= regmap_write(dev->regmap, 0x80ec58, 0x0);

	return regmap_write(dev->regmap, 0x80ed81, val);
}

static int it913x_sleep(struct dvb_frontend *fe)
{
	struct it913x_dev *dev = fe->tuner_priv;
	int ret, len;

	dev_dbg(&dev->client->dev, "role=%u\n", dev->role);

	ret  = regmap_bulk_write(dev->regmap, 0x80ec40, "\x00", 1);
	if (ret)
		goto err;

	/*
	 * Writing '0x00' to master tuner register '0x80ec08' causes slave tuner
	 * communication lost. Due to that, we cannot put master full sleep.
	 */
	if (dev->role == IT913X_ROLE_DUAL_MASTER)
		len = 4;
	else
		len = 15;

	dev_dbg(&dev->client->dev, "role=%u len=%d\n", dev->role, len);

	ret = regmap_bulk_write(dev->regmap, 0x80ec02,
			"\x3f\x1f\x3f\x3e\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
			len);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x80ec12, "\x00\x00\x00\x00", 4);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x80ec17,
			"\x00\x00\x00\x00\x00\x00\x00\x00\x00", 9);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x80ec22,
			"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 10);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x80ec20, "\x00", 1);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x80ec3f, "\x01", 1);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&dev->client->dev, "failed %d\n", ret);
	return ret;
}

static int it9137_set_params(struct dvb_frontend *fe)
{
	struct it913x_dev *dev = fe->tuner_priv;
	struct it913xset *set_tuner = set_it9137_template;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 bandwidth = p->bandwidth_hz;
	u32 frequency_m = p->frequency;
	int ret;
	unsigned int reg;
	u32 frequency = frequency_m / 1000;
	u32 freq, temp_f, tmp;
	u16 iqik_m_cal;
	u16 n_div;
	u8 n;
	u8 l_band;
	u8 lna_band;
	u8 bw;

	if (dev->firmware_ver == 1)
		set_tuner = set_it9135_template;
	else
		set_tuner = set_it9137_template;

	dev_dbg(&dev->client->dev, "Tuner Frequency %d Bandwidth %d\n",
			frequency, bandwidth);

	if (frequency >= 51000 && frequency <= 440000) {
		l_band = 0;
		lna_band = 0;
	} else if (frequency > 440000 && frequency <= 484000) {
		l_band = 1;
		lna_band = 1;
	} else if (frequency > 484000 && frequency <= 533000) {
		l_band = 1;
		lna_band = 2;
	} else if (frequency > 533000 && frequency <= 587000) {
		l_band = 1;
		lna_band = 3;
	} else if (frequency > 587000 && frequency <= 645000) {
		l_band = 1;
		lna_band = 4;
	} else if (frequency > 645000 && frequency <= 710000) {
		l_band = 1;
		lna_band = 5;
	} else if (frequency > 710000 && frequency <= 782000) {
		l_band = 1;
		lna_band = 6;
	} else if (frequency > 782000 && frequency <= 860000) {
		l_band = 1;
		lna_band = 7;
	} else if (frequency > 1450000 && frequency <= 1492000) {
		l_band = 1;
		lna_band = 0;
	} else if (frequency > 1660000 && frequency <= 1685000) {
		l_band = 1;
		lna_band = 1;
	} else
		return -EINVAL;
	set_tuner[0].reg[0] = lna_band;

	switch (bandwidth) {
	case 5000000:
		bw = 0;
		break;
	case 6000000:
		bw = 2;
		break;
	case 7000000:
		bw = 4;
		break;
	default:
	case 8000000:
		bw = 6;
		break;
	}

	set_tuner[1].reg[0] = bw;
	set_tuner[2].reg[0] = 0xa0 | (l_band << 3);

	if (frequency > 53000 && frequency <= 74000) {
		n_div = 48;
		n = 0;
	} else if (frequency > 74000 && frequency <= 111000) {
		n_div = 32;
		n = 1;
	} else if (frequency > 111000 && frequency <= 148000) {
		n_div = 24;
		n = 2;
	} else if (frequency > 148000 && frequency <= 222000) {
		n_div = 16;
		n = 3;
	} else if (frequency > 222000 && frequency <= 296000) {
		n_div = 12;
		n = 4;
	} else if (frequency > 296000 && frequency <= 445000) {
		n_div = 8;
		n = 5;
	} else if (frequency > 445000 && frequency <= dev->tun_fn_min) {
		n_div = 6;
		n = 6;
	} else if (frequency > dev->tun_fn_min && frequency <= 950000) {
		n_div = 4;
		n = 7;
	} else if (frequency > 1450000 && frequency <= 1680000) {
		n_div = 2;
		n = 0;
	} else
		return -EINVAL;

	ret = regmap_read(dev->regmap, 0x80ed81, &reg);
	iqik_m_cal = (u16)reg * n_div;

	if (reg < 0x20) {
		if (dev->tun_clk_mode == 0)
			iqik_m_cal = (iqik_m_cal * 9) >> 5;
		else
			iqik_m_cal >>= 1;
	} else {
		iqik_m_cal = 0x40 - iqik_m_cal;
		if (dev->tun_clk_mode == 0)
			iqik_m_cal = ~((iqik_m_cal * 9) >> 5);
		else
			iqik_m_cal = ~(iqik_m_cal >> 1);
	}

	temp_f = frequency * (u32)n_div * (u32)dev->tun_fdiv;
	freq = temp_f / dev->tun_xtal;
	tmp = freq * dev->tun_xtal;

	if ((temp_f - tmp) >= (dev->tun_xtal >> 1))
		freq++;

	freq += (u32) n << 13;
	/* Frequency OMEGA_IQIK_M_CAL_MID*/
	temp_f = freq + (u32)iqik_m_cal;

	set_tuner[3].reg[0] =  temp_f & 0xff;
	set_tuner[4].reg[0] =  (temp_f >> 8) & 0xff;

	dev_dbg(&dev->client->dev, "High Frequency = %04x\n", temp_f);

	/* Lower frequency */
	set_tuner[5].reg[0] =  freq & 0xff;
	set_tuner[6].reg[0] =  (freq >> 8) & 0xff;

	dev_dbg(&dev->client->dev, "low Frequency = %04x\n", freq);

	ret = it913x_script_loader(dev, set_tuner);

	return (ret < 0) ? -ENODEV : 0;
}

static const struct dvb_tuner_ops it913x_tuner_ops = {
	.info = {
		.name           = "ITE Tech IT913X",
		.frequency_min  = 174000000,
		.frequency_max  = 862000000,
	},

	.init = it913x_init,
	.sleep = it913x_sleep,
	.set_params = it9137_set_params,
};

static int it913x_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct it913x_config *cfg = client->dev.platform_data;
	struct dvb_frontend *fe = cfg->fe;
	struct it913x_dev *dev;
	int ret;
	char *chip_ver_str;
	static const struct regmap_config regmap_config = {
		.reg_bits = 24,
		.val_bits = 8,
	};

	dev = kzalloc(sizeof(struct it913x_dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		dev_err(&client->dev, "kzalloc() failed\n");
		goto err;
	}

	dev->client = client;
	dev->fe = cfg->fe;
	dev->chip_ver = cfg->chip_ver;
	dev->role = cfg->role;
	dev->firmware_ver = 1;
	dev->regmap = regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

	/* tuner RF initial */
	ret = regmap_write(dev->regmap, 0x80ec4c, 0x68);
	if (ret)
		goto err_regmap_exit;

	fe->tuner_priv = dev;
	memcpy(&fe->ops.tuner_ops, &it913x_tuner_ops,
			sizeof(struct dvb_tuner_ops));
	i2c_set_clientdata(client, dev);

	if (dev->chip_ver == 1)
		chip_ver_str = "AX";
	else if (dev->chip_ver == 2)
		chip_ver_str = "BX";
	else
		chip_ver_str = "??";

	dev_info(&dev->client->dev, "ITE IT913X %s successfully attached\n",
			chip_ver_str);
	dev_dbg(&dev->client->dev, "chip_ver=%u role=%u\n", dev->chip_ver, dev->role);
	return 0;

err_regmap_exit:
	regmap_exit(dev->regmap);
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return ret;
}

static int it913x_remove(struct i2c_client *client)
{
	struct it913x_dev *dev = i2c_get_clientdata(client);
	struct dvb_frontend *fe = dev->fe;

	dev_dbg(&client->dev, "\n");

	memset(&fe->ops.tuner_ops, 0, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = NULL;
	regmap_exit(dev->regmap);
	kfree(dev);

	return 0;
}

static const struct i2c_device_id it913x_id_table[] = {
	{"it913x", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, it913x_id_table);

static struct i2c_driver it913x_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "it913x",
	},
	.probe		= it913x_probe,
	.remove		= it913x_remove,
	.id_table	= it913x_id_table,
};

module_i2c_driver(it913x_driver);

MODULE_DESCRIPTION("ITE Tech IT913X silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
