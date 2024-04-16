// SPDX-License-Identifier: GPL-2.0
//
// Driver for LNB supply and control IC STMicroelectronics LNBH29
//
// Copyright (c) 2018 Socionext Inc.

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <media/dvb_frontend.h>
#include "lnbh29.h"

/**
 * struct lnbh29_priv - LNBH29 driver private data
 * @i2c:         Pointer to the I2C adapter structure
 * @i2c_address: I2C address of LNBH29 chip
 * @config:      Registers configuration
 *               offset 0: 1st register address, always 0x01 (DATA)
 *               offset 1: DATA register value
 */
struct lnbh29_priv {
	struct i2c_adapter *i2c;
	u8 i2c_address;
	u8 config[2];
};

#define LNBH29_STATUS_OLF     BIT(0)
#define LNBH29_STATUS_OTF     BIT(1)
#define LNBH29_STATUS_VMON    BIT(2)
#define LNBH29_STATUS_PNG     BIT(3)
#define LNBH29_STATUS_PDO     BIT(4)
#define LNBH29_VSEL_MASK      GENMASK(2, 0)
#define LNBH29_VSEL_0         0x00
/* Min: 13.188V, Typ: 13.667V, Max:14V */
#define LNBH29_VSEL_13        0x03
/* Min: 18.158V, Typ: 18.817V, Max:19.475V */
#define LNBH29_VSEL_18        0x07

static int lnbh29_read_vmon(struct lnbh29_priv *priv)
{
	u8 addr = 0x00;
	u8 status[2];
	int ret;
	struct i2c_msg msg[2] = {
		{
			.addr = priv->i2c_address,
			.flags = 0,
			.len = 1,
			.buf = &addr
		}, {
			.addr = priv->i2c_address,
			.flags = I2C_M_RD,
			.len = sizeof(status),
			.buf = status
		}
	};

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret >= 0 && ret != 2)
		ret = -EIO;
	if (ret < 0) {
		dev_dbg(&priv->i2c->dev, "LNBH29 I2C transfer failed (%d)\n",
			ret);
		return ret;
	}

	if (status[0] & (LNBH29_STATUS_OLF | LNBH29_STATUS_VMON)) {
		dev_err(&priv->i2c->dev,
			"LNBH29 voltage in failure state, status reg 0x%x\n",
			status[0]);
		return -EIO;
	}

	return 0;
}

static int lnbh29_set_voltage(struct dvb_frontend *fe,
			      enum fe_sec_voltage voltage)
{
	struct lnbh29_priv *priv = fe->sec_priv;
	u8 data_reg;
	int ret;
	struct i2c_msg msg = {
		.addr = priv->i2c_address,
		.flags = 0,
		.len = sizeof(priv->config),
		.buf = priv->config
	};

	switch (voltage) {
	case SEC_VOLTAGE_OFF:
		data_reg = LNBH29_VSEL_0;
		break;
	case SEC_VOLTAGE_13:
		data_reg = LNBH29_VSEL_13;
		break;
	case SEC_VOLTAGE_18:
		data_reg = LNBH29_VSEL_18;
		break;
	default:
		return -EINVAL;
	}
	priv->config[1] &= ~LNBH29_VSEL_MASK;
	priv->config[1] |= data_reg;

	ret = i2c_transfer(priv->i2c, &msg, 1);
	if (ret >= 0 && ret != 1)
		ret = -EIO;
	if (ret < 0) {
		dev_err(&priv->i2c->dev, "LNBH29 I2C transfer error (%d)\n",
			ret);
		return ret;
	}

	/* Soft-start time (Vout 0V to 18V) is Typ. 6ms. */
	usleep_range(6000, 20000);

	if (voltage == SEC_VOLTAGE_OFF)
		return 0;

	return lnbh29_read_vmon(priv);
}

static void lnbh29_release(struct dvb_frontend *fe)
{
	lnbh29_set_voltage(fe, SEC_VOLTAGE_OFF);
	kfree(fe->sec_priv);
	fe->sec_priv = NULL;
}

struct dvb_frontend *lnbh29_attach(struct dvb_frontend *fe,
				   struct lnbh29_config *cfg,
				   struct i2c_adapter *i2c)
{
	struct lnbh29_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	priv->i2c_address = (cfg->i2c_address >> 1);
	priv->i2c = i2c;
	priv->config[0] = 0x01;
	priv->config[1] = cfg->data_config;
	fe->sec_priv = priv;

	if (lnbh29_set_voltage(fe, SEC_VOLTAGE_OFF)) {
		dev_err(&i2c->dev, "no LNBH29 found at I2C addr 0x%02x\n",
			priv->i2c_address);
		kfree(priv);
		fe->sec_priv = NULL;
		return NULL;
	}

	fe->ops.release_sec = lnbh29_release;
	fe->ops.set_voltage = lnbh29_set_voltage;

	dev_info(&i2c->dev, "LNBH29 attached at I2C addr 0x%02x\n",
		 priv->i2c_address);

	return fe;
}
EXPORT_SYMBOL(lnbh29_attach);

MODULE_AUTHOR("Katsuhiro Suzuki <suzuki.katsuhiro@socionext.com>");
MODULE_DESCRIPTION("STMicroelectronics LNBH29 driver");
MODULE_LICENSE("GPL v2");
