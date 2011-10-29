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

/* lock FE */
static int cxd2820r_lock(struct cxd2820r_priv *priv, int active_fe)
{
	int ret = 0;
	dbg("%s: active_fe=%d", __func__, active_fe);

	mutex_lock(&priv->fe_lock);

	/* -1=NONE, 0=DVB-T/T2, 1=DVB-C */
	if (priv->active_fe == active_fe)
		;
	else if (priv->active_fe == -1)
		priv->active_fe = active_fe;
	else
		ret = -EBUSY;

	mutex_unlock(&priv->fe_lock);

	return ret;
}

/* unlock FE */
static void cxd2820r_unlock(struct cxd2820r_priv *priv, int active_fe)
{
	dbg("%s: active_fe=%d", __func__, active_fe);

	mutex_lock(&priv->fe_lock);

	/* -1=NONE, 0=DVB-T/T2, 1=DVB-C */
	if (priv->active_fe == active_fe)
		priv->active_fe = -1;

	mutex_unlock(&priv->fe_lock);

	return;
}

/* 64 bit div with round closest, like DIV_ROUND_CLOSEST but 64 bit */
u32 cxd2820r_div_u64_round_closest(u64 dividend, u32 divisor)
{
	return div_u64(dividend + (divisor / 2), divisor);
}

static int cxd2820r_set_frontend(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *p)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		switch (priv->delivery_system) {
		case SYS_UNDEFINED:
			if (c->delivery_system == SYS_DVBT) {
				/* SLEEP => DVB-T */
				ret = cxd2820r_set_frontend_t(fe, p);
			} else {
				/* SLEEP => DVB-T2 */
				ret = cxd2820r_set_frontend_t2(fe, p);
			}
			break;
		case SYS_DVBT:
			if (c->delivery_system == SYS_DVBT) {
				/* DVB-T => DVB-T */
				ret = cxd2820r_set_frontend_t(fe, p);
			} else if (c->delivery_system == SYS_DVBT2) {
				/* DVB-T => DVB-T2 */
				ret = cxd2820r_sleep_t(fe);
				if (ret)
					break;
				ret = cxd2820r_set_frontend_t2(fe, p);
			}
			break;
		case SYS_DVBT2:
			if (c->delivery_system == SYS_DVBT2) {
				/* DVB-T2 => DVB-T2 */
				ret = cxd2820r_set_frontend_t2(fe, p);
			} else if (c->delivery_system == SYS_DVBT) {
				/* DVB-T2 => DVB-T */
				ret = cxd2820r_sleep_t2(fe);
				if (ret)
					break;
				ret = cxd2820r_set_frontend_t(fe, p);
			}
			break;
		default:
			dbg("%s: error state=%d", __func__,
				priv->delivery_system);
			ret = -EINVAL;
		}
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_set_frontend_c(fe, p);
	}

	return ret;
}

static int cxd2820r_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		switch (fe->dtv_property_cache.delivery_system) {
		case SYS_DVBT:
			ret = cxd2820r_read_status_t(fe, status);
			break;
		case SYS_DVBT2:
			ret = cxd2820r_read_status_t2(fe, status);
			break;
		default:
			ret = -EINVAL;
		}
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_read_status_c(fe, status);
	}

	return ret;
}

static int cxd2820r_get_frontend(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *p)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		switch (fe->dtv_property_cache.delivery_system) {
		case SYS_DVBT:
			ret = cxd2820r_get_frontend_t(fe, p);
			break;
		case SYS_DVBT2:
			ret = cxd2820r_get_frontend_t2(fe, p);
			break;
		default:
			ret = -EINVAL;
		}
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_get_frontend_c(fe, p);
	}

	return ret;
}

static int cxd2820r_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		switch (fe->dtv_property_cache.delivery_system) {
		case SYS_DVBT:
			ret = cxd2820r_read_ber_t(fe, ber);
			break;
		case SYS_DVBT2:
			ret = cxd2820r_read_ber_t2(fe, ber);
			break;
		default:
			ret = -EINVAL;
		}
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_read_ber_c(fe, ber);
	}

	return ret;
}

static int cxd2820r_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		switch (fe->dtv_property_cache.delivery_system) {
		case SYS_DVBT:
			ret = cxd2820r_read_signal_strength_t(fe, strength);
			break;
		case SYS_DVBT2:
			ret = cxd2820r_read_signal_strength_t2(fe, strength);
			break;
		default:
			ret = -EINVAL;
		}
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_read_signal_strength_c(fe, strength);
	}

	return ret;
}

static int cxd2820r_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		switch (fe->dtv_property_cache.delivery_system) {
		case SYS_DVBT:
			ret = cxd2820r_read_snr_t(fe, snr);
			break;
		case SYS_DVBT2:
			ret = cxd2820r_read_snr_t2(fe, snr);
			break;
		default:
			ret = -EINVAL;
		}
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_read_snr_c(fe, snr);
	}

	return ret;
}

static int cxd2820r_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		switch (fe->dtv_property_cache.delivery_system) {
		case SYS_DVBT:
			ret = cxd2820r_read_ucblocks_t(fe, ucblocks);
			break;
		case SYS_DVBT2:
			ret = cxd2820r_read_ucblocks_t2(fe, ucblocks);
			break;
		default:
			ret = -EINVAL;
		}
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_read_ucblocks_c(fe, ucblocks);
	}

	return ret;
}

static int cxd2820r_init(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	priv->delivery_system = SYS_UNDEFINED;
	/* delivery system is unknown at that (init) phase */

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		ret = cxd2820r_init_t(fe);
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_init_c(fe);
	}

	return ret;
}

static int cxd2820r_sleep(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		switch (fe->dtv_property_cache.delivery_system) {
		case SYS_DVBT:
			ret = cxd2820r_sleep_t(fe);
			break;
		case SYS_DVBT2:
			ret = cxd2820r_sleep_t2(fe);
			break;
		default:
			ret = -EINVAL;
		}

		cxd2820r_unlock(priv, 0);
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_sleep_c(fe);

		cxd2820r_unlock(priv, 1);
	}

	return ret;
}

static int cxd2820r_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	int ret;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	if (fe->ops.info.type == FE_OFDM) {
		/* DVB-T/T2 */
		ret = cxd2820r_lock(priv, 0);
		if (ret)
			return ret;

		switch (fe->dtv_property_cache.delivery_system) {
		case SYS_DVBT:
			ret = cxd2820r_get_tune_settings_t(fe, s);
			break;
		case SYS_DVBT2:
			ret = cxd2820r_get_tune_settings_t2(fe, s);
			break;
		default:
			ret = -EINVAL;
		}
	} else {
		/* DVB-C */
		ret = cxd2820r_lock(priv, 1);
		if (ret)
			return ret;

		ret = cxd2820r_get_tune_settings_c(fe, s);
	}

	return ret;
}

static enum dvbfe_search cxd2820r_search(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *p)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	fe_status_t status = 0;
	dbg("%s: delsys=%d", __func__, fe->dtv_property_cache.delivery_system);

	/* switch between DVB-T and DVB-T2 when tune fails */
	if (priv->last_tune_failed) {
		if (priv->delivery_system == SYS_DVBT)
			c->delivery_system = SYS_DVBT2;
		else
			c->delivery_system = SYS_DVBT;
	}

	/* set frontend */
	ret = cxd2820r_set_frontend(fe, p);
	if (ret)
		goto error;


	/* frontend lock wait loop count */
	switch (priv->delivery_system) {
	case SYS_DVBT:
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

		if (status & FE_HAS_SIGNAL)
			break;
	}

	/* check if we have a valid signal */
	if (status) {
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

	if (fe->ops.info.type == FE_OFDM) {
		i2c_del_adapter(&priv->tuner_i2c_adapter);
		kfree(priv);
	}

	return;
}

static u32 cxd2820r_tuner_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static int cxd2820r_tuner_i2c_xfer(struct i2c_adapter *i2c_adap,
	struct i2c_msg msg[], int num)
{
	struct cxd2820r_priv *priv = i2c_get_adapdata(i2c_adap);
	int ret;
	u8 *obuf = kmalloc(msg[0].len + 2, GFP_KERNEL);
	struct i2c_msg msg2[2] = {
		{
			.addr = priv->cfg.i2c_address,
			.flags = 0,
			.len = msg[0].len + 2,
			.buf = obuf,
		}, {
			.addr = priv->cfg.i2c_address,
			.flags = I2C_M_RD,
			.len = msg[1].len,
			.buf = msg[1].buf,
		}
	};

	if (!obuf)
		return -ENOMEM;

	obuf[0] = 0x09;
	obuf[1] = (msg[0].addr << 1);
	if (num == 2) { /* I2C read */
		obuf[1] = (msg[0].addr << 1) | I2C_M_RD; /* I2C RD flag */
		msg2[0].len = msg[0].len + 2 - 1; /* '-1' maybe HW bug ? */
	}
	memcpy(&obuf[2], msg[0].buf, msg[0].len);

	ret = i2c_transfer(priv->i2c, msg2, num);
	if (ret < 0)
		warn("tuner i2c failed ret:%d", ret);

	kfree(obuf);

	return ret;
}

static struct i2c_algorithm cxd2820r_tuner_i2c_algo = {
	.master_xfer   = cxd2820r_tuner_i2c_xfer,
	.functionality = cxd2820r_tuner_i2c_func,
};

struct i2c_adapter *cxd2820r_get_tuner_i2c_adapter(struct dvb_frontend *fe)
{
	struct cxd2820r_priv *priv = fe->demodulator_priv;
	return &priv->tuner_i2c_adapter;
}
EXPORT_SYMBOL(cxd2820r_get_tuner_i2c_adapter);

static struct dvb_frontend_ops cxd2820r_ops[2];

struct dvb_frontend *cxd2820r_attach(const struct cxd2820r_config *cfg,
	struct i2c_adapter *i2c, struct dvb_frontend *fe)
{
	int ret;
	struct cxd2820r_priv *priv = NULL;
	u8 tmp;

	if (fe == NULL) {
		/* FE0 */
		/* allocate memory for the internal priv */
		priv = kzalloc(sizeof(struct cxd2820r_priv), GFP_KERNEL);
		if (priv == NULL)
			goto error;

		/* setup the priv */
		priv->i2c = i2c;
		memcpy(&priv->cfg, cfg, sizeof(struct cxd2820r_config));
		mutex_init(&priv->fe_lock);

		priv->active_fe = -1; /* NONE */

		/* check if the demod is there */
		priv->bank[0] = priv->bank[1] = 0xff;
		ret = cxd2820r_rd_reg(priv, 0x000fd, &tmp);
		dbg("%s: chip id=%02x", __func__, tmp);
		if (ret || tmp != 0xe1)
			goto error;

		/* create frontends */
		memcpy(&priv->fe[0].ops, &cxd2820r_ops[0],
			sizeof(struct dvb_frontend_ops));
		memcpy(&priv->fe[1].ops, &cxd2820r_ops[1],
			sizeof(struct dvb_frontend_ops));

		priv->fe[0].demodulator_priv = priv;
		priv->fe[1].demodulator_priv = priv;

		/* create tuner i2c adapter */
		strlcpy(priv->tuner_i2c_adapter.name,
			"CXD2820R tuner I2C adapter",
			sizeof(priv->tuner_i2c_adapter.name));
		priv->tuner_i2c_adapter.algo = &cxd2820r_tuner_i2c_algo;
		priv->tuner_i2c_adapter.algo_data = NULL;
		i2c_set_adapdata(&priv->tuner_i2c_adapter, priv);
		if (i2c_add_adapter(&priv->tuner_i2c_adapter) < 0) {
			err("tuner I2C bus could not be initialized");
			goto error;
		}

		return &priv->fe[0];

	} else {
		/* FE1: FE0 given as pointer, just return FE1 we have
		 * already created */
		priv = fe->demodulator_priv;
		return &priv->fe[1];
	}

error:
	kfree(priv);
	return NULL;
}
EXPORT_SYMBOL(cxd2820r_attach);

static struct dvb_frontend_ops cxd2820r_ops[2] = {
	{
		/* DVB-T/T2 */
		.info = {
			.name = "Sony CXD2820R (DVB-T/T2)",
			.type = FE_OFDM,
			.caps =
				FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
				FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 |
				FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
				FE_CAN_QPSK | FE_CAN_QAM_16 |
				FE_CAN_QAM_64 | FE_CAN_QAM_256 |
				FE_CAN_QAM_AUTO |
				FE_CAN_TRANSMISSION_MODE_AUTO |
				FE_CAN_GUARD_INTERVAL_AUTO |
				FE_CAN_HIERARCHY_AUTO |
				FE_CAN_MUTE_TS |
				FE_CAN_2G_MODULATION
		},

		.release = cxd2820r_release,
		.init = cxd2820r_init,
		.sleep = cxd2820r_sleep,

		.get_tune_settings = cxd2820r_get_tune_settings,

		.get_frontend = cxd2820r_get_frontend,

		.get_frontend_algo = cxd2820r_get_frontend_algo,
		.search = cxd2820r_search,

		.read_status = cxd2820r_read_status,
		.read_snr = cxd2820r_read_snr,
		.read_ber = cxd2820r_read_ber,
		.read_ucblocks = cxd2820r_read_ucblocks,
		.read_signal_strength = cxd2820r_read_signal_strength,
	},
	{
		/* DVB-C */
		.info = {
			.name = "Sony CXD2820R (DVB-C)",
			.type = FE_QAM,
			.caps =
				FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 |
				FE_CAN_QAM_128 | FE_CAN_QAM_256 |
				FE_CAN_FEC_AUTO
		},

		.release = cxd2820r_release,
		.init = cxd2820r_init,
		.sleep = cxd2820r_sleep,

		.get_tune_settings = cxd2820r_get_tune_settings,

		.set_frontend = cxd2820r_set_frontend,
		.get_frontend = cxd2820r_get_frontend,

		.read_status = cxd2820r_read_status,
		.read_snr = cxd2820r_read_snr,
		.read_ber = cxd2820r_read_ber,
		.read_ucblocks = cxd2820r_read_ucblocks,
		.read_signal_strength = cxd2820r_read_signal_strength,
	},
};


MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Sony CXD2820R demodulator driver");
MODULE_LICENSE("GPL");
