// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * isl6405.c - driver for dual lnb supply and control ic ISL6405
 *
 * Copyright (C) 2008 Hartmut Hackmann
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
#include "isl6405.h"

struct isl6405 {
	u8			config;
	u8			override_or;
	u8			override_and;
	struct i2c_adapter	*i2c;
	u8			i2c_addr;
};

static int isl6405_set_voltage(struct dvb_frontend *fe,
			       enum fe_sec_voltage voltage)
{
	struct isl6405 *isl6405 = (struct isl6405 *) fe->sec_priv;
	struct i2c_msg msg = {	.addr = isl6405->i2c_addr, .flags = 0,
				.buf = &isl6405->config,
				.len = sizeof(isl6405->config) };

	if (isl6405->override_or & 0x80) {
		isl6405->config &= ~(ISL6405_VSEL2 | ISL6405_EN2);
		switch (voltage) {
		case SEC_VOLTAGE_OFF:
			break;
		case SEC_VOLTAGE_13:
			isl6405->config |= ISL6405_EN2;
			break;
		case SEC_VOLTAGE_18:
			isl6405->config |= (ISL6405_EN2 | ISL6405_VSEL2);
			break;
		default:
			return -EINVAL;
		}
	} else {
		isl6405->config &= ~(ISL6405_VSEL1 | ISL6405_EN1);
		switch (voltage) {
		case SEC_VOLTAGE_OFF:
			break;
		case SEC_VOLTAGE_13:
			isl6405->config |= ISL6405_EN1;
			break;
		case SEC_VOLTAGE_18:
			isl6405->config |= (ISL6405_EN1 | ISL6405_VSEL1);
			break;
		default:
			return -EINVAL;
		}
	}
	isl6405->config |= isl6405->override_or;
	isl6405->config &= isl6405->override_and;

	return (i2c_transfer(isl6405->i2c, &msg, 1) == 1) ? 0 : -EIO;
}

static int isl6405_enable_high_lnb_voltage(struct dvb_frontend *fe, long arg)
{
	struct isl6405 *isl6405 = (struct isl6405 *) fe->sec_priv;
	struct i2c_msg msg = {	.addr = isl6405->i2c_addr, .flags = 0,
				.buf = &isl6405->config,
				.len = sizeof(isl6405->config) };

	if (isl6405->override_or & 0x80) {
		if (arg)
			isl6405->config |= ISL6405_LLC2;
		else
			isl6405->config &= ~ISL6405_LLC2;
	} else {
		if (arg)
			isl6405->config |= ISL6405_LLC1;
		else
			isl6405->config &= ~ISL6405_LLC1;
	}
	isl6405->config |= isl6405->override_or;
	isl6405->config &= isl6405->override_and;

	return (i2c_transfer(isl6405->i2c, &msg, 1) == 1) ? 0 : -EIO;
}

static void isl6405_release(struct dvb_frontend *fe)
{
	/* power off */
	isl6405_set_voltage(fe, SEC_VOLTAGE_OFF);

	/* free */
	kfree(fe->sec_priv);
	fe->sec_priv = NULL;
}

struct dvb_frontend *isl6405_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c,
				    u8 i2c_addr, u8 override_set, u8 override_clear)
{
	struct isl6405 *isl6405 = kmalloc(sizeof(struct isl6405), GFP_KERNEL);
	if (!isl6405)
		return NULL;

	/* default configuration */
	if (override_set & 0x80)
		isl6405->config = ISL6405_ISEL2;
	else
		isl6405->config = ISL6405_ISEL1;
	isl6405->i2c = i2c;
	isl6405->i2c_addr = i2c_addr;
	fe->sec_priv = isl6405;

	/* bits which should be forced to '1' */
	isl6405->override_or = override_set;

	/* bits which should be forced to '0' */
	isl6405->override_and = ~override_clear;

	/* detect if it is present or not */
	if (isl6405_set_voltage(fe, SEC_VOLTAGE_OFF)) {
		kfree(isl6405);
		fe->sec_priv = NULL;
		return NULL;
	}

	/* install release callback */
	fe->ops.release_sec = isl6405_release;

	/* override frontend ops */
	fe->ops.set_voltage = isl6405_set_voltage;
	fe->ops.enable_high_lnb_voltage = isl6405_enable_high_lnb_voltage;

	return fe;
}
EXPORT_SYMBOL_GPL(isl6405_attach);

MODULE_DESCRIPTION("Driver for lnb supply and control ic isl6405");
MODULE_AUTHOR("Hartmut Hackmann & Oliver Endriss");
MODULE_LICENSE("GPL");
