// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * isl6421.h - driver for lnb supply and control ic ISL6421
 *
 * Copyright (C) 2006 Andrew de Quincey
 * Copyright (C) 2006 Oliver Endriss
 *
 * the project's page is at https://linuxtv.org
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <media/dvb_frontend.h>
#include "isl6421.h"

struct isl6421 {
	u8			config;
	u8			override_or;
	u8			override_and;
	struct i2c_adapter	*i2c;
	u8			i2c_addr;
	bool			is_off;
};

static int isl6421_set_voltage(struct dvb_frontend *fe,
			       enum fe_sec_voltage voltage)
{
	int ret;
	u8 buf;
	bool is_off;
	struct isl6421 *isl6421 = (struct isl6421 *) fe->sec_priv;
	struct i2c_msg msg[2] = {
		{
		  .addr = isl6421->i2c_addr,
		  .flags = 0,
		  .buf = &isl6421->config,
		  .len = 1,
		}, {
		  .addr = isl6421->i2c_addr,
		  .flags = I2C_M_RD,
		  .buf = &buf,
		  .len = 1,
		}

	};

	isl6421->config &= ~(ISL6421_VSEL1 | ISL6421_EN1);

	switch(voltage) {
	case SEC_VOLTAGE_OFF:
		is_off = true;
		break;
	case SEC_VOLTAGE_13:
		is_off = false;
		isl6421->config |= ISL6421_EN1;
		break;
	case SEC_VOLTAGE_18:
		is_off = false;
		isl6421->config |= (ISL6421_EN1 | ISL6421_VSEL1);
		break;
	default:
		return -EINVAL;
	}

	/*
	 * If LNBf were not powered on, disable dynamic current limit, as,
	 * according with datasheet, highly capacitive load on the output may
	 * cause a difficult start-up.
	 */
	if (isl6421->is_off && !is_off)
		isl6421->config |= ISL6421_DCL;

	isl6421->config |= isl6421->override_or;
	isl6421->config &= isl6421->override_and;

	ret = i2c_transfer(isl6421->i2c, msg, 2);
	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	/* Store off status now in case future commands fail */
	isl6421->is_off = is_off;

	/* On overflow, the device will try again after 900 ms (typically) */
	if (!is_off && (buf & ISL6421_OLF1))
		msleep(1000);

	/* Re-enable dynamic current limit */
	if ((isl6421->config & ISL6421_DCL) &&
	    !(isl6421->override_or & ISL6421_DCL)) {
		isl6421->config &= ~ISL6421_DCL;

		ret = i2c_transfer(isl6421->i2c, msg, 2);
		if (ret < 0)
			return ret;
		if (ret != 2)
			return -EIO;
	}

	/* Check if overload flag is active. If so, disable power */
	if (!is_off && (buf & ISL6421_OLF1)) {
		isl6421->config &= ~(ISL6421_VSEL1 | ISL6421_EN1);
		ret = i2c_transfer(isl6421->i2c, msg, 1);
		if (ret < 0)
			return ret;
		if (ret != 1)
			return -EIO;
		isl6421->is_off = true;

		dev_warn(&isl6421->i2c->dev,
			 "Overload current detected. disabling LNBf power\n");
		return -EINVAL;
	}

	return 0;
}

static int isl6421_enable_high_lnb_voltage(struct dvb_frontend *fe, long arg)
{
	struct isl6421 *isl6421 = (struct isl6421 *) fe->sec_priv;
	struct i2c_msg msg = {	.addr = isl6421->i2c_addr, .flags = 0,
				.buf = &isl6421->config,
				.len = sizeof(isl6421->config) };

	if (arg)
		isl6421->config |= ISL6421_LLC1;
	else
		isl6421->config &= ~ISL6421_LLC1;

	isl6421->config |= isl6421->override_or;
	isl6421->config &= isl6421->override_and;

	return (i2c_transfer(isl6421->i2c, &msg, 1) == 1) ? 0 : -EIO;
}

static int isl6421_set_tone(struct dvb_frontend *fe,
			    enum fe_sec_tone_mode tone)
{
	struct isl6421 *isl6421 = (struct isl6421 *) fe->sec_priv;
	struct i2c_msg msg = { .addr = isl6421->i2c_addr, .flags = 0,
			       .buf = &isl6421->config,
			       .len = sizeof(isl6421->config) };

	switch (tone) {
	case SEC_TONE_ON:
		isl6421->config |= ISL6421_ENT1;
		break;
	case SEC_TONE_OFF:
		isl6421->config &= ~ISL6421_ENT1;
		break;
	default:
		return -EINVAL;
	}

	isl6421->config |= isl6421->override_or;
	isl6421->config &= isl6421->override_and;

	return (i2c_transfer(isl6421->i2c, &msg, 1) == 1) ? 0 : -EIO;
}

static void isl6421_release(struct dvb_frontend *fe)
{
	/* power off */
	isl6421_set_voltage(fe, SEC_VOLTAGE_OFF);

	/* free */
	kfree(fe->sec_priv);
	fe->sec_priv = NULL;
}

struct dvb_frontend *isl6421_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, u8 i2c_addr,
		   u8 override_set, u8 override_clear, bool override_tone)
{
	struct isl6421 *isl6421 = kmalloc(sizeof(struct isl6421), GFP_KERNEL);
	if (!isl6421)
		return NULL;

	/* default configuration */
	isl6421->config = ISL6421_ISEL1;
	isl6421->i2c = i2c;
	isl6421->i2c_addr = i2c_addr;
	fe->sec_priv = isl6421;

	/* bits which should be forced to '1' */
	isl6421->override_or = override_set;

	/* bits which should be forced to '0' */
	isl6421->override_and = ~override_clear;

	/* detect if it is present or not */
	if (isl6421_set_voltage(fe, SEC_VOLTAGE_OFF)) {
		kfree(isl6421);
		fe->sec_priv = NULL;
		return NULL;
	}

	isl6421->is_off = true;

	/* install release callback */
	fe->ops.release_sec = isl6421_release;

	/* override frontend ops */
	fe->ops.set_voltage = isl6421_set_voltage;
	fe->ops.enable_high_lnb_voltage = isl6421_enable_high_lnb_voltage;
	if (override_tone)
		fe->ops.set_tone = isl6421_set_tone;

	return fe;
}
EXPORT_SYMBOL(isl6421_attach);

MODULE_DESCRIPTION("Driver for lnb supply and control ic isl6421");
MODULE_AUTHOR("Andrew de Quincey & Oliver Endriss");
MODULE_LICENSE("GPL");
