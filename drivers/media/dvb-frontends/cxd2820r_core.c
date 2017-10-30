/*
 * Sony CXD2820R demodulator driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
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


#include "cxd2820r_priv.h"

/* Write register table */
int cxd2820r_wr_reg_val_mask_tab(struct cxd2820r_priv *priv,
				 const struct reg_val_mask *tab, int tab_len)
{
	struct i2c_client *client = priv->client[0];
	int ret;
	unsigned int i, reg, mask, val;
	struct regmap *regmap;

	dev_dbg(&client->dev, "tab_len=%d\n", tab_len);

	for (i = 0; i < tab_len; i++) {
		if ((tab[i].reg >> 16) & 0x1)
			regmap = priv->regmap[1];
		else
			regmap = priv->regmap[0];

		reg = (tab[i].reg >> 0) & 0xffff;
		val = tab[i].val;
		mask = tab[i].mask;

		if (mask == 0xff)
			ret = regmap_write(regmap, reg, val);
		else
			ret = regmap_write_bits(regmap, reg, mask, val);
		if (ret)
			goto error;
	}

	return 0;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

int cxd2820r_gpio(struct dvb_frontend *fe, u8 *gpio)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u8 tmp0, tmp1;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	/* update GPIOs only when needed */
	if (!memcmp(gpio, priv->gpio, sizeof(priv->gpio)))
		return 0;

	tmp0 = 0x00;
	tmp1 = 0x00;
	for (i = 0; i < sizeof(priv->gpio); i++) {
		/* enable / disable */
		if (gpio[i] & CXD2820R_GPIO_E)
			tmp0 |= (2 << 6) >> (2 * i);
		else
			tmp0 |= (1 << 6) >> (2 * i);

		/* input / output */
		if (gpio[i] & CXD2820R_GPIO_I)
			tmp1 |= (1 << (3 + i));
		else
			tmp1 |= (0 << (3 + i));

		/* high / low */
		if (gpio[i] & CXD2820R_GPIO_H)
			tmp1 |= (1 << (0 + i));
		else
			tmp1 |= (0 << (0 + i));

		dev_dbg(&client->dev, "gpio i=%d %02x %02x\n", i, tmp0, tmp1);
	}

	dev_dbg(&client->dev, "wr gpio=%02x %02x\n", tmp0, tmp1);

	/* write bits [7:2] */
	ret = regmap_update_bits(priv->regmap[0], 0x0089, 0xfc, tmp0);
	if (ret)
		goto error;

	/* write bits [5:0] */
	ret = regmap_update_bits(priv->regmap[0], 0x008e, 0x3f, tmp1);
	if (ret)
		goto error;

	memcpy(priv->gpio, gpio, sizeof(priv->gpio));

	return ret;
error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int cxd2820r_set_frontend(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_init_t(fe);
		if (ret < 0)
			goto err;
		ret = cxd2820r_set_frontend_t(fe);
		if (ret < 0)
			goto err;
		break;
	case SYS_DVBT2:
		ret = cxd2820r_init_t(fe);
		if (ret < 0)
			goto err;
		ret = cxd2820r_set_frontend_t2(fe);
		if (ret < 0)
			goto err;
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_init_c(fe);
		if (ret < 0)
			goto err;
		ret = cxd2820r_set_frontend_c(fe);
		if (ret < 0)
			goto err;
		break;
	default:
		dev_dbg(&client->dev, "invalid delivery_system\n");
		ret = -EINVAL;
		break;
	}
err:
	return ret;
}

static int cxd2820r_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_read_status_t(fe, status);
		break;
	case SYS_DVBT2:
		ret = cxd2820r_read_status_t2(fe, status);
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_read_status_c(fe, status);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cxd2820r_get_frontend(struct dvb_frontend *fe,
				 struct dtv_frontend_properties *p)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	if (priv->delivery_system == SYS_UNDEFINED)
		return 0;

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_get_frontend_t(fe, p);
		break;
	case SYS_DVBT2:
		ret = cxd2820r_get_frontend_t2(fe, p);
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_get_frontend_c(fe, p);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cxd2820r_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	*ber = (priv->post_bit_error - priv->post_bit_error_prev_dvbv3);
	priv->post_bit_error_prev_dvbv3 = priv->post_bit_error;

	return 0;
}

static int cxd2820r_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	if (c->strength.stat[0].scale == FE_SCALE_RELATIVE)
		*strength = c->strength.stat[0].uvalue;
	else
		*strength = 0;

	return 0;
}

static int cxd2820r_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	if (c->cnr.stat[0].scale == FE_SCALE_DECIBEL)
		*snr = div_s64(c->cnr.stat[0].svalue, 100);
	else
		*snr = 0;

	return 0;
}

static int cxd2820r_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	*ucblocks = 0;

	return 0;
}

static int cxd2820r_init(struct dvb_frontend *fe)
{
	return 0;
}

static int cxd2820r_sleep(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_sleep_t(fe);
		break;
	case SYS_DVBT2:
		ret = cxd2820r_sleep_t2(fe);
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_sleep_c(fe);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cxd2820r_get_tune_settings(struct dvb_frontend *fe,
				      struct dvb_frontend_tune_settings *s)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_get_tune_settings_t(fe, s);
		break;
	case SYS_DVBT2:
		ret = cxd2820r_get_tune_settings_t2(fe, s);
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_get_tune_settings_c(fe, s);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static enum dvbfe_search cxd2820r_search(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	enum fe_status status = 0;

	dev_dbg(&client->dev, "delivery_system=%d\n", c->delivery_system);

	/* switch between DVB-T and DVB-T2 when tune fails */
	if (priv->last_tune_failed) {
		if (priv->delivery_system == SYS_DVBT) {
			ret = cxd2820r_sleep_t(fe);
			if (ret)
				goto error;

			c->delivery_system = SYS_DVBT2;
		} else if (priv->delivery_system == SYS_DVBT2) {
			ret = cxd2820r_sleep_t2(fe);
			if (ret)
				goto error;

			c->delivery_system = SYS_DVBT;
		}
	}

	/* set frontend */
	ret = cxd2820r_set_frontend(fe);
	if (ret)
		goto error;

	/* frontend lock wait loop count */
	switch (priv->delivery_system) {
	case SYS_DVBT:
	case SYS_DVBC_ANNEX_A:
		i = 20;
		break;
	case SYS_DVBT2:
		i = 40;
		break;
	case SYS_UNDEFINED:
	default:
		i = 0;
		break;
	}

	/* wait frontend lock */
	for (; i > 0; i--) {
		dev_dbg(&client->dev, "loop=%d\n", i);
		msleep(50);
		ret = cxd2820r_read_status(fe, &status);
		if (ret)
			goto error;

		if (status & FE_HAS_LOCK)
			break;
	}

	/* check if we have a valid signal */
	if (status & FE_HAS_LOCK) {
		priv->last_tune_failed = false;
		return DVBFE_ALGO_SEARCH_SUCCESS;
	} else {
		priv->last_tune_failed = true;
		return DVBFE_ALGO_SEARCH_AGAIN;
	}

error:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return DVBFE_ALGO_SEARCH_ERROR;
}

static int cxd2820r_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_CUSTOM;
}

static void cxd2820r_release(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];

	dev_dbg(&client->dev, "\n");

	i2c_unregister_device(client);

	return;
}

static int cxd2820r_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct i2c_client *client = priv->client[0];

	dev_dbg_ratelimited(&client->dev, "enable=%d\n", enable);

	return regmap_update_bits(priv->regmap[0], 0x00db, 0x01, enable ? 1 : 0);
}

#ifdef CONFIG_GPIOLIB
static int cxd2820r_gpio_direction_output(struct gpio_chip *chip, unsigned nr,
		int val)
{
	struct cxd2820r_priv *priv = gpiochip_get_data(chip);
	struct i2c_client *client = priv->client[0];
	u8 gpio[GPIO_COUNT];

	dev_dbg(&client->dev, "nr=%u val=%d\n", nr, val);

	memcpy(gpio, priv->gpio, sizeof(gpio));
	gpio[nr] = CXD2820R_GPIO_E | CXD2820R_GPIO_O | (val << 2);

	return cxd2820r_gpio(&priv->fe, gpio);
}

static void cxd2820r_gpio_set(struct gpio_chip *chip, unsigned nr, int val)
{
	struct cxd2820r_priv *priv = gpiochip_get_data(chip);
	struct i2c_client *client = priv->client[0];
	u8 gpio[GPIO_COUNT];

	dev_dbg(&client->dev, "nr=%u val=%d\n", nr, val);

	memcpy(gpio, priv->gpio, sizeof(gpio));
	gpio[nr] = CXD2820R_GPIO_E | CXD2820R_GPIO_O | (val << 2);

	(void) cxd2820r_gpio(&priv->fe, gpio);

	return;
}

static int cxd2820r_gpio_get(struct gpio_chip *chip, unsigned nr)
{
	struct cxd2820r_priv *priv = gpiochip_get_data(chip);
	struct i2c_client *client = priv->client[0];

	dev_dbg(&client->dev, "nr=%u\n", nr);

	return (priv->gpio[nr] >> 2) & 0x01;
}
#endif

static const struct dvb_frontend_ops cxd2820r_ops = {
	.delsys = { SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A },
	/* default: DVB-T/T2 */
	.info = {
		.name = "Sony CXD2820R",

		.caps =	FE_CAN_FEC_1_2			|
			FE_CAN_FEC_2_3			|
			FE_CAN_FEC_3_4			|
			FE_CAN_FEC_5_6			|
			FE_CAN_FEC_7_8			|
			FE_CAN_FEC_AUTO			|
			FE_CAN_QPSK			|
			FE_CAN_QAM_16			|
			FE_CAN_QAM_32			|
			FE_CAN_QAM_64			|
			FE_CAN_QAM_128			|
			FE_CAN_QAM_256			|
			FE_CAN_QAM_AUTO			|
			FE_CAN_TRANSMISSION_MODE_AUTO	|
			FE_CAN_GUARD_INTERVAL_AUTO	|
			FE_CAN_HIERARCHY_AUTO		|
			FE_CAN_MUTE_TS			|
			FE_CAN_2G_MODULATION		|
			FE_CAN_MULTISTREAM
		},

	.release		= cxd2820r_release,
	.init			= cxd2820r_init,
	.sleep			= cxd2820r_sleep,

	.get_tune_settings	= cxd2820r_get_tune_settings,
	.i2c_gate_ctrl		= cxd2820r_i2c_gate_ctrl,

	.get_frontend		= cxd2820r_get_frontend,

	.get_frontend_algo	= cxd2820r_get_frontend_algo,
	.search			= cxd2820r_search,

	.read_status		= cxd2820r_read_status,
	.read_snr		= cxd2820r_read_snr,
	.read_ber		= cxd2820r_read_ber,
	.read_ucblocks		= cxd2820r_read_ucblocks,
	.read_signal_strength	= cxd2820r_read_signal_strength,
};

/*
 * XXX: That is wrapper to cxd2820r_probe() via driver core in order to provide
 * proper I2C client for legacy media attach binding.
 * New users must use I2C client binding directly!
 */
struct dvb_frontend *cxd2820r_attach(const struct cxd2820r_config *config,
				     struct i2c_adapter *adapter,
				     int *gpio_chip_base)
{
	struct i2c_client *client;
	struct i2c_board_info board_info;
	struct cxd2820r_platform_data pdata;

	pdata.ts_mode = config->ts_mode;
	pdata.ts_clk_inv = config->ts_clock_inv;
	pdata.if_agc_polarity = config->if_agc_polarity;
	pdata.spec_inv = config->spec_inv;
	pdata.gpio_chip_base = &gpio_chip_base;
	pdata.attach_in_use = true;

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "cxd2820r", I2C_NAME_SIZE);
	board_info.addr = config->i2c_address;
	board_info.platform_data = &pdata;
	client = i2c_new_device(adapter, &board_info);
	if (!client || !client->dev.driver)
		return NULL;

	return pdata.get_dvb_frontend(client);
}
EXPORT_SYMBOL(cxd2820r_attach);

static struct dvb_frontend *cxd2820r_get_dvb_frontend(struct i2c_client *client)
{
	struct cxd2820r_priv *priv = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	return &priv->fe;
}

static int cxd2820r_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct cxd2820r_platform_data *pdata = client->dev.platform_data;
	struct cxd2820r_priv *priv;
	int ret, *gpio_chip_base;
	unsigned int utmp;
	static const struct regmap_range_cfg regmap_range_cfg0[] = {
		{
			.range_min        = 0x0000,
			.range_max        = 0x3fff,
			.selector_reg     = 0x00,
			.selector_mask    = 0xff,
			.selector_shift   = 0,
			.window_start     = 0x00,
			.window_len       = 0x100,
		},
	};
	static const struct regmap_range_cfg regmap_range_cfg1[] = {
		{
			.range_min        = 0x0000,
			.range_max        = 0x01ff,
			.selector_reg     = 0x00,
			.selector_mask    = 0xff,
			.selector_shift   = 0,
			.window_start     = 0x00,
			.window_len       = 0x100,
		},
	};
	static const struct regmap_config regmap_config0 = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0x3fff,
		.ranges = regmap_range_cfg0,
		.num_ranges = ARRAY_SIZE(regmap_range_cfg0),
		.cache_type = REGCACHE_NONE,
	};
	static const struct regmap_config regmap_config1 = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0x01ff,
		.ranges = regmap_range_cfg1,
		.num_ranges = ARRAY_SIZE(regmap_range_cfg1),
		.cache_type = REGCACHE_NONE,
	};

	dev_dbg(&client->dev, "\n");

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err;
	}

	priv->client[0] = client;
	priv->fe.demodulator_priv = priv;
	priv->i2c = client->adapter;
	priv->ts_mode = pdata->ts_mode;
	priv->ts_clk_inv = pdata->ts_clk_inv;
	priv->if_agc_polarity = pdata->if_agc_polarity;
	priv->spec_inv = pdata->spec_inv;
	gpio_chip_base = *pdata->gpio_chip_base;
	priv->regmap[0] = regmap_init_i2c(priv->client[0], &regmap_config0);
	if (IS_ERR(priv->regmap[0])) {
		ret = PTR_ERR(priv->regmap[0]);
		goto err_kfree;
	}

	/* Check demod answers with correct chip id */
	ret = regmap_read(priv->regmap[0], 0x00fd, &utmp);
	if (ret)
		goto err_regmap_0_regmap_exit;

	dev_dbg(&client->dev, "chip_id=%02x\n", utmp);

	if (utmp != 0xe1) {
		ret = -ENODEV;
		goto err_regmap_0_regmap_exit;
	}

	/*
	 * Chip has two I2C addresses for different register banks. We register
	 * one dummy I2C client in in order to get own I2C client for each
	 * register bank.
	 */
	priv->client[1] = i2c_new_dummy(client->adapter, client->addr | (1 << 1));
	if (!priv->client[1]) {
		ret = -ENODEV;
		dev_err(&client->dev, "I2C registration failed\n");
		if (ret)
			goto err_regmap_0_regmap_exit;
	}

	priv->regmap[1] = regmap_init_i2c(priv->client[1], &regmap_config1);
	if (IS_ERR(priv->regmap[1])) {
		ret = PTR_ERR(priv->regmap[1]);
		goto err_client_1_i2c_unregister_device;
	}

	if (gpio_chip_base) {
#ifdef CONFIG_GPIOLIB
		/* Add GPIOs */
		priv->gpio_chip.label = KBUILD_MODNAME;
		priv->gpio_chip.parent = &client->dev;
		priv->gpio_chip.owner = THIS_MODULE;
		priv->gpio_chip.direction_output = cxd2820r_gpio_direction_output;
		priv->gpio_chip.set = cxd2820r_gpio_set;
		priv->gpio_chip.get = cxd2820r_gpio_get;
		priv->gpio_chip.base = -1; /* Dynamic allocation */
		priv->gpio_chip.ngpio = GPIO_COUNT;
		priv->gpio_chip.can_sleep = 1;
		ret = gpiochip_add_data(&priv->gpio_chip, priv);
		if (ret)
			goto err_regmap_1_regmap_exit;

		dev_dbg(&client->dev, "gpio_chip.base=%d\n",
			priv->gpio_chip.base);

		*gpio_chip_base = priv->gpio_chip.base;
#else
		/*
		 * Use static GPIO configuration if GPIOLIB is undefined.
		 * This is fallback condition.
		 */
		u8 gpio[GPIO_COUNT];
		gpio[0] = (*gpio_chip_base >> 0) & 0x07;
		gpio[1] = (*gpio_chip_base >> 3) & 0x07;
		gpio[2] = 0;
		ret = cxd2820r_gpio(&priv->fe, gpio);
		if (ret)
			goto err_regmap_1_regmap_exit;
#endif
	}

	/* Create dvb frontend */
	memcpy(&priv->fe.ops, &cxd2820r_ops, sizeof(priv->fe.ops));
	if (!pdata->attach_in_use)
		priv->fe.ops.release = NULL;
	i2c_set_clientdata(client, priv);

	/* Setup callbacks */
	pdata->get_dvb_frontend = cxd2820r_get_dvb_frontend;

	dev_info(&client->dev, "Sony CXD2820R successfully identified\n");

	return 0;
err_regmap_1_regmap_exit:
	regmap_exit(priv->regmap[1]);
err_client_1_i2c_unregister_device:
	i2c_unregister_device(priv->client[1]);
err_regmap_0_regmap_exit:
	regmap_exit(priv->regmap[0]);
err_kfree:
	kfree(priv);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int cxd2820r_remove(struct i2c_client *client)
{
	struct cxd2820r_priv *priv = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

#ifdef CONFIG_GPIOLIB
	if (priv->gpio_chip.label)
		gpiochip_remove(&priv->gpio_chip);
#endif
	regmap_exit(priv->regmap[1]);
	i2c_unregister_device(priv->client[1]);

	regmap_exit(priv->regmap[0]);

	kfree(priv);

	return 0;
}

static const struct i2c_device_id cxd2820r_id_table[] = {
	{"cxd2820r", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cxd2820r_id_table);

static struct i2c_driver cxd2820r_driver = {
	.driver = {
		.name                = "cxd2820r",
		.suppress_bind_attrs = true,
	},
	.probe    = cxd2820r_probe,
	.remove   = cxd2820r_remove,
	.id_table = cxd2820r_id_table,
};

module_i2c_driver(cxd2820r_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Sony CXD2820R demodulator driver");
MODULE_LICENSE("GPL");
