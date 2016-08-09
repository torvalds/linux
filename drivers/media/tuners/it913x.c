/*
 * ITE IT913X silicon tuner driver
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

#include "it913x.h"
#include <linux/regmap.h>

struct it913x_dev {
	struct i2c_client *client;
	struct regmap *regmap;
	struct dvb_frontend *fe;
	u8 chip_ver:2;
	u8 role:2;
	u16 xtal;
	u8 fdiv;
	u8 clk_mode;
	u32 fn_min;
	bool active;
};

static int it913x_init(struct dvb_frontend *fe)
{
	struct it913x_dev *dev = fe->tuner_priv;
	int ret;
	unsigned int utmp;
	u8 iqik_m_cal, nv_val, buf[2];
	static const u8 nv[] = {48, 32, 24, 16, 12, 8, 6, 4, 2};
	unsigned long timeout;

	dev_dbg(&dev->client->dev, "role %u\n", dev->role);

	ret = regmap_write(dev->regmap, 0x80ec4c, 0x68);
	if (ret)
		goto err;

	usleep_range(10000, 100000);

	ret = regmap_read(dev->regmap, 0x80ec86, &utmp);
	if (ret)
		goto err;

	switch (utmp) {
	case 0:
		/* 12.000 MHz */
		dev->clk_mode = utmp;
		dev->xtal = 2000;
		dev->fdiv = 3;
		iqik_m_cal = 16;
		break;
	case 1:
		/* 20.480 MHz */
		dev->clk_mode = utmp;
		dev->xtal = 640;
		dev->fdiv = 1;
		iqik_m_cal = 6;
		break;
	default:
		dev_err(&dev->client->dev, "unknown clock identifier %d\n", utmp);
		goto err;
	}

	ret = regmap_read(dev->regmap, 0x80ed03,  &utmp);
	if (ret)
		goto err;

	else if (utmp < ARRAY_SIZE(nv))
		nv_val = nv[utmp];
	else
		nv_val = 2;

	#define TIMEOUT 50
	timeout = jiffies + msecs_to_jiffies(TIMEOUT);
	while (!time_after(jiffies, timeout)) {
		ret = regmap_bulk_read(dev->regmap, 0x80ed23, buf, 2);
		if (ret)
			goto err;

		utmp = (buf[1] << 8) | (buf[0] << 0);
		if (utmp)
			break;
	}

	dev_dbg(&dev->client->dev, "r_fbc_m_bdry took %u ms, val %u\n",
			jiffies_to_msecs(jiffies) -
			(jiffies_to_msecs(timeout) - TIMEOUT), utmp);

	dev->fn_min = dev->xtal * utmp;
	dev->fn_min /= (dev->fdiv * nv_val);
	dev->fn_min *= 1000;
	dev_dbg(&dev->client->dev, "fn_min %u\n", dev->fn_min);

	/*
	 * Chip version BX never sets that flag so we just wait 50ms in that
	 * case. It is possible poll BX similarly than AX and then timeout in
	 * order to get 50ms delay, but that causes about 120 extra I2C
	 * messages. As for now, we just wait and reduce IO.
	 */
	if (dev->chip_ver == 1) {
		#define TIMEOUT 50
		timeout = jiffies + msecs_to_jiffies(TIMEOUT);
		while (!time_after(jiffies, timeout)) {
			ret = regmap_read(dev->regmap, 0x80ec82, &utmp);
			if (ret)
				goto err;

			if (utmp)
				break;
		}

		dev_dbg(&dev->client->dev, "p_tsm_init_mode took %u ms, val %u\n",
				jiffies_to_msecs(jiffies) -
				(jiffies_to_msecs(timeout) - TIMEOUT), utmp);
	} else {
		msleep(50);
	}

	ret = regmap_write(dev->regmap, 0x80ed81, iqik_m_cal);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x80ec57, 0x00);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x80ec58, 0x00);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x80ec40, 0x01);
	if (ret)
		goto err;

	dev->active = true;

	return 0;
err:
	dev_dbg(&dev->client->dev, "failed %d\n", ret);
	return ret;
}

static int it913x_sleep(struct dvb_frontend *fe)
{
	struct it913x_dev *dev = fe->tuner_priv;
	int ret, len;

	dev_dbg(&dev->client->dev, "role %u\n", dev->role);

	dev->active = false;

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

	dev_dbg(&dev->client->dev, "role %u, len %d\n", dev->role, len);

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

static int it913x_set_params(struct dvb_frontend *fe)
{
	struct it913x_dev *dev = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	unsigned int utmp;
	u32 pre_lo_freq, t_cal_freq;
	u16 iqik_m_cal, n_div;
	u8 u8tmp, n, l_band, lna_band;

	dev_dbg(&dev->client->dev, "role=%u, frequency %u, bandwidth_hz %u\n",
			dev->role, c->frequency, c->bandwidth_hz);

	if (!dev->active) {
		ret = -EINVAL;
		goto err;
	}

	if (c->frequency <=         74000000) {
		n_div = 48;
		n = 0;
	} else if (c->frequency <= 111000000) {
		n_div = 32;
		n = 1;
	} else if (c->frequency <= 148000000) {
		n_div = 24;
		n = 2;
	} else if (c->frequency <= 222000000) {
		n_div = 16;
		n = 3;
	} else if (c->frequency <= 296000000) {
		n_div = 12;
		n = 4;
	} else if (c->frequency <= 445000000) {
		n_div = 8;
		n = 5;
	} else if (c->frequency <= dev->fn_min) {
		n_div = 6;
		n = 6;
	} else if (c->frequency <= 950000000) {
		n_div = 4;
		n = 7;
	} else {
		n_div = 2;
		n = 0;
	}

	ret = regmap_read(dev->regmap, 0x80ed81, &utmp);
	if (ret)
		goto err;

	iqik_m_cal = utmp * n_div;

	if (utmp < 0x20) {
		if (dev->clk_mode == 0)
			iqik_m_cal = (iqik_m_cal * 9) >> 5;
		else
			iqik_m_cal >>= 1;
	} else {
		iqik_m_cal = 0x40 - iqik_m_cal;
		if (dev->clk_mode == 0)
			iqik_m_cal = ~((iqik_m_cal * 9) >> 5);
		else
			iqik_m_cal = ~(iqik_m_cal >> 1);
	}

	t_cal_freq = (c->frequency / 1000) * n_div * dev->fdiv;
	pre_lo_freq = t_cal_freq / dev->xtal;
	utmp = pre_lo_freq * dev->xtal;

	if ((t_cal_freq - utmp) >= (dev->xtal >> 1))
		pre_lo_freq++;

	pre_lo_freq += (u32) n << 13;
	/* Frequency OMEGA_IQIK_M_CAL_MID*/
	t_cal_freq = pre_lo_freq + (u32)iqik_m_cal;
	dev_dbg(&dev->client->dev, "t_cal_freq %u, pre_lo_freq %u\n",
			t_cal_freq, pre_lo_freq);

	if (c->frequency <=         440000000) {
		l_band = 0;
		lna_band = 0;
	} else if (c->frequency <=  484000000) {
		l_band = 1;
		lna_band = 1;
	} else if (c->frequency <=  533000000) {
		l_band = 1;
		lna_band = 2;
	} else if (c->frequency <=  587000000) {
		l_band = 1;
		lna_band = 3;
	} else if (c->frequency <=  645000000) {
		l_band = 1;
		lna_band = 4;
	} else if (c->frequency <=  710000000) {
		l_band = 1;
		lna_band = 5;
	} else if (c->frequency <=  782000000) {
		l_band = 1;
		lna_band = 6;
	} else if (c->frequency <=  860000000) {
		l_band = 1;
		lna_band = 7;
	} else if (c->frequency <= 1492000000) {
		l_band = 1;
		lna_band = 0;
	} else if (c->frequency <= 1685000000) {
		l_band = 1;
		lna_band = 1;
	} else {
		ret = -EINVAL;
		goto err;
	}

	/* XXX: latest windows driver does not set that at all */
	ret = regmap_write(dev->regmap, 0x80ee06, lna_band);
	if (ret)
		goto err;

	if (c->bandwidth_hz <=      5000000)
		u8tmp = 0;
	else if (c->bandwidth_hz <= 6000000)
		u8tmp = 2;
	else if (c->bandwidth_hz <= 7000000)
		u8tmp = 4;
	else
		u8tmp = 6;       /* 8000000 */

	ret = regmap_write(dev->regmap, 0x80ec56, u8tmp);
	if (ret)
		goto err;

	/* XXX: latest windows driver sets different value (a8 != 68) */
	ret = regmap_write(dev->regmap, 0x80ec4c, 0xa0 | (l_band << 3));
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x80ec4d, (t_cal_freq >> 0) & 0xff);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x80ec4e, (t_cal_freq >> 8) & 0xff);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x80011e, (pre_lo_freq >> 0) & 0xff);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x80011f, (pre_lo_freq >> 8) & 0xff);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&dev->client->dev, "failed %d\n", ret);
	return ret;
}

static const struct dvb_tuner_ops it913x_tuner_ops = {
	.info = {
		.name           = "ITE IT913X",
		.frequency_min  = 174000000,
		.frequency_max  = 862000000,
	},

	.init = it913x_init,
	.sleep = it913x_sleep,
	.set_params = it913x_set_params,
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
	dev->regmap = regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

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
	dev_dbg(&dev->client->dev, "chip_ver %u, role %u\n",
			dev->chip_ver, dev->role);
	return 0;

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
		.name	= "it913x",
		.suppress_bind_attrs	= true,
	},
	.probe		= it913x_probe,
	.remove		= it913x_remove,
	.id_table	= it913x_id_table,
};

module_i2c_driver(it913x_driver);

MODULE_DESCRIPTION("ITE IT913X silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
