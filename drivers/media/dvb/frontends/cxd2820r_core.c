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

int cxd2820r_debug;
module_param_named(debug, cxd2820r_debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

/* write multiple registers */
static int cxd2820r_wr_regs_i2c(struct cxd2820r_priv *priv, u8 i2c, u8 reg,
	u8 *val, int len)
{
	int ret;
	u8 buf[len+1];
	struct i2c_msg msg[1] = {
		{
			.addr = i2c,
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
		warn("i2c wr failed ret:%d reg:%02x len:%d", ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* read multiple registers */
static int cxd2820r_rd_regs_i2c(struct cxd2820r_priv *priv, u8 i2c, u8 reg,
	u8 *val, int len)
{
	int ret;
	u8 buf[len];
	struct i2c_msg msg[2] = {
		{
			.addr = i2c,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = i2c,
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
		warn("i2c rd failed ret:%d reg:%02x len:%d", ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* write multiple registers */
int cxd2820r_wr_regs(struct cxd2820r_priv *priv, u32 reginfo, u8 *val,
	int len)
{
	int ret;
	u8 i2c_addr;
	u8 reg = (reginfo >> 0) & 0xff;
	u8 bank = (reginfo >> 8) & 0xff;
	u8 i2c = (reginfo >> 16) & 0x01;

	/* select I2C */
	if (i2c)
		i2c_addr = priv->cfg.i2c_address | (1 << 1); /* DVB-C */
	else
		i2c_addr = priv->cfg.i2c_address; /* DVB-T/T2 */

	/* switch bank if needed */
	if (bank != priv->bank[i2c]) {
		ret = cxd2820r_wr_regs_i2c(priv, i2c_addr, 0x00, &bank, 1);
		if (ret)
			return ret;
		priv->bank[i2c] = bank;
	}
	return cxd2820r_wr_regs_i2c(priv, i2c_addr, reg, val, len);
}

/* read multiple registers */
int cxd2820r_rd_regs(struct cxd2820r_priv *priv, u32 reginfo, u8 *val,
	int len)
{
	int ret;
	u8 i2c_addr;
	u8 reg = (reginfo >> 0) & 0xff;
	u8 bank = (reginfo >> 8) & 0xff;
	u8 i2c = (reginfo >> 16) & 0x01;

	/* select I2C */
	if (i2c)
		i2c_addr = priv->cfg.i2c_address | (1 << 1); /* DVB-C */
	else
		i2c_addr = priv->cfg.i2c_address; /* DVB-T/T2 */

	/* switch bank if needed */
	if (bank != priv->bank[i2c]) {
		ret = cxd2820r_wr_regs_i2c(priv, i2c_addr, 0x00, &bank, 1);
		if (ret)
			return ret;
		priv->bank[i2c] = bank;
	}
	return cxd2820r_rd_regs_i2c(priv, i2c_addr, reg, val, len);
}

/* write single register */
int cxd2820r_wr_reg(struct cxd2820r_priv *priv, u32 reg, u8 val)
{
	return cxd2820r_wr_regs(priv, reg, &val, 1);
}

/* read single register */
int cxd2820r_rd_reg(struct cxd2820r_priv *priv, u32 reg, u8 *val)
{
	return cxd2820r_rd_regs(priv, reg, val, 1);
}

/* write single register with mask */
int cxd2820r_wr_reg_mask(struct cxd2820r_priv *priv, u32 reg, u8 val,
	u8 mask)
{
	int ret;
	u8 tmp;

	/* no need for read if whole reg is written */
	if (mask != 0xff) {
		ret = cxd2820r_rd_reg(priv, reg, &tmp);
		if (ret)
			return ret;

		val &= mask;
		tmp &= ~mask;
		val |= tmp;
	}

	return cxd2820r_wr_reg(priv, reg, val);
}

int cxd2820r_gpio(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret, i;
	u8 *gpio, tmp0, tmp1;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	switch (fe->dtv_property_cache.delivery_system) {
	case SYS_DVBT:
		gpio = priv->cfg.gpio_dvbt;
		break;
	case SYS_DVBT2:
		gpio = priv->cfg.gpio_dvbt2;
		break;
	case SYS_DVBC_ANNEX_AC:
		gpio = priv->cfg.gpio_dvbc;
		break;
	default:
		ret = -EINVAL;
		goto error;
	}

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

		dbg("%s: GPIO i=%d %02x %02x", __func__, i, tmp0, tmp1);
	}

	dbg("%s: wr gpio=%02x %02x", __func__, tmp0, tmp1);

	/* write bits [7:2] */
	ret = cxd2820r_wr_reg_mask(priv, 0x00089, tmp0, 0xfc);
	if (ret)
		goto error;

	/* write bits [5:0] */
	ret = cxd2820r_wr_reg_mask(priv, 0x0008e, tmp1, 0x3f);
	if (ret)
		goto error;

	memcpy(priv->gpio, gpio, sizeof(priv->gpio));

	return ret;
error:
	dbg("%s: failed:%d", __func__, ret);
	return ret;
}

/* 64 bit div with round closest, like DIV_ROUND_CLOSEST but 64 bit */
u32 cxd2820r_div_u64_round_closest(u64 dividend, u32 divisor)
{
	return div_u64(dividend + (divisor / 2), divisor);
}

static int cxd2820r_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;

	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);
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
		dbg("%s: error state=%d", __func__, fe->dtv_property_cache.delivery_system);
		ret = -EINVAL;
		break;
	}
err:
	return ret;
}
static int cxd2820r_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	int ret;

	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);
	switch (fe->dtv_property_cache.delivery_system) {
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

static int cxd2820r_get_frontend(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;

	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (priv->delivery_system == SYS_UNDEFINED)
		return 0;

	switch (fe->dtv_property_cache.delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_get_frontend_t(fe);
		break;
	case SYS_DVBT2:
		ret = cxd2820r_get_frontend_t2(fe);
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_get_frontend_c(fe);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cxd2820r_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	int ret;

	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);
	switch (fe->dtv_property_cache.delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_read_ber_t(fe, ber);
		break;
	case SYS_DVBT2:
		ret = cxd2820r_read_ber_t2(fe, ber);
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_read_ber_c(fe, ber);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cxd2820r_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	int ret;

	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);
	switch (fe->dtv_property_cache.delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_read_signal_strength_t(fe, strength);
		break;
	case SYS_DVBT2:
		ret = cxd2820r_read_signal_strength_t2(fe, strength);
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_read_signal_strength_c(fe, strength);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cxd2820r_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	int ret;

	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);
	switch (fe->dtv_property_cache.delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_read_snr_t(fe, snr);
		break;
	case SYS_DVBT2:
		ret = cxd2820r_read_snr_t2(fe, snr);
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_read_snr_c(fe, snr);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cxd2820r_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	int ret;

	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);
	switch (fe->dtv_property_cache.delivery_system) {
	case SYS_DVBT:
		ret = cxd2820r_read_ucblocks_t(fe, ucblocks);
		break;
	case SYS_DVBT2:
		ret = cxd2820r_read_ucblocks_t2(fe, ucblocks);
		break;
	case SYS_DVBC_ANNEX_A:
		ret = cxd2820r_read_ucblocks_c(fe, ucblocks);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cxd2820r_init(struct dvb_frontend *fe)
{
	return 0;
}

static int cxd2820r_sleep(struct dvb_frontend *fe)
{
	int ret;

	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);
	switch (fe->dtv_property_cache.delivery_system) {
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
	int ret;

	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);
	switch (fe->dtv_property_cache.delivery_system) {
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
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	fe_status_t status = 0;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

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
		dbg("%s: LOOP=%d", __func__, i);
		msleep(50);
		ret = cxd2820r_read_status(fe, &status);
		if (ret)
			goto error;

		if (status & FE_HAS_LOCK)
			break;
	}

	/* check if we have a valid signal */
	if (status & FE_HAS_LOCK) {
		priv->last_tune_failed = 0;
		return DVBFE_ALGO_SEARCH_SUCCESS;
	} else {
		priv->last_tune_failed = 1;
		return DVBFE_ALGO_SEARCH_AGAIN;
	}

error:
	dbg("%s: failed:%d", __func__, ret);
	return DVBFE_ALGO_SEARCH_ERROR;
}

static int cxd2820r_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_CUSTOM;
}

static void cxd2820r_release(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	dbg("%s", __func__);

	kfree(priv);
	return;
}

static int cxd2820r_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	dbg("%s: %d", __func__, enable);

	/* Bit 0 of reg 0xdb in bank 0x00 controls I2C repeater */
	return cxd2820r_wr_reg_mask(priv, 0xdb, enable ? 1 : 0, 0x1);
}

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
			FE_CAN_2G_MODULATION
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

struct dvb_frontend *cxd2820r_attach(const struct cxd2820r_config *cfg,
		struct i2c_adapter *i2c)
{
	struct cxd2820r_priv *priv = NULL;
	int ret;
	u8 tmp;

	priv = kzalloc(sizeof (struct cxd2820r_priv), GFP_KERNEL);
	if (!priv)
		goto error;

	priv->i2c = i2c;
	memcpy(&priv->cfg, cfg, sizeof (struct cxd2820r_config));

	priv->bank[0] = priv->bank[1] = 0xff;
	ret = cxd2820r_rd_reg(priv, 0x000fd, &tmp);
	dbg("%s: chip id=%02x", __func__, tmp);
	if (ret || tmp != 0xe1)
		goto error;

	memcpy(&priv->fe.ops, &cxd2820r_ops, sizeof (struct dvb_frontend_ops));
	priv->fe.demodulator_priv = priv;
	return &priv->fe;
error:
	kfree(priv);
	return NULL;
}
EXPORT_SYMBOL(cxd2820r_attach);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Sony CXD2820R demodulator driver");
MODULE_LICENSE("GPL");
