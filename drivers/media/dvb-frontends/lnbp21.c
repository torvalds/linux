// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lnbp21.c - driver for lnb supply and control ic lnbp21
 *
 * Copyright (C) 2006, 2009 Oliver Endriss <o.endriss@gmx.de>
 * Copyright (C) 2009 Igor M. Liplianin <liplianin@netup.ru>
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
#include "lnbp21.h"
#include "lnbh24.h"

struct lnbp21 {
	u8			config;
	u8			override_or;
	u8			override_and;
	struct i2c_adapter	*i2c;
	u8			i2c_addr;
};

static int lnbp21_set_voltage(struct dvb_frontend *fe,
			      enum fe_sec_voltage voltage)
{
	struct lnbp21 *lnbp21 = (struct lnbp21 *) fe->sec_priv;
	struct i2c_msg msg = {	.addr = lnbp21->i2c_addr, .flags = 0,
				.buf = &lnbp21->config,
				.len = sizeof(lnbp21->config) };

	lnbp21->config &= ~(LNBP21_VSEL | LNBP21_EN);

	switch(voltage) {
	case SEC_VOLTAGE_OFF:
		break;
	case SEC_VOLTAGE_13:
		lnbp21->config |= LNBP21_EN;
		break;
	case SEC_VOLTAGE_18:
		lnbp21->config |= (LNBP21_EN | LNBP21_VSEL);
		break;
	default:
		return -EINVAL;
	}

	lnbp21->config |= lnbp21->override_or;
	lnbp21->config &= lnbp21->override_and;

	return (i2c_transfer(lnbp21->i2c, &msg, 1) == 1) ? 0 : -EIO;
}

static int lnbp21_enable_high_lnb_voltage(struct dvb_frontend *fe, long arg)
{
	struct lnbp21 *lnbp21 = (struct lnbp21 *) fe->sec_priv;
	struct i2c_msg msg = {	.addr = lnbp21->i2c_addr, .flags = 0,
				.buf = &lnbp21->config,
				.len = sizeof(lnbp21->config) };

	if (arg)
		lnbp21->config |= LNBP21_LLC;
	else
		lnbp21->config &= ~LNBP21_LLC;

	lnbp21->config |= lnbp21->override_or;
	lnbp21->config &= lnbp21->override_and;

	return (i2c_transfer(lnbp21->i2c, &msg, 1) == 1) ? 0 : -EIO;
}

static int lnbp21_set_tone(struct dvb_frontend *fe,
			   enum fe_sec_tone_mode tone)
{
	struct lnbp21 *lnbp21 = (struct lnbp21 *) fe->sec_priv;
	struct i2c_msg msg = {	.addr = lnbp21->i2c_addr, .flags = 0,
				.buf = &lnbp21->config,
				.len = sizeof(lnbp21->config) };

	switch (tone) {
	case SEC_TONE_OFF:
		lnbp21->config &= ~LNBP21_TEN;
		break;
	case SEC_TONE_ON:
		lnbp21->config |= LNBP21_TEN;
		break;
	default:
		return -EINVAL;
	}

	lnbp21->config |= lnbp21->override_or;
	lnbp21->config &= lnbp21->override_and;

	return (i2c_transfer(lnbp21->i2c, &msg, 1) == 1) ? 0 : -EIO;
}

static void lnbp21_release(struct dvb_frontend *fe)
{
	/* LNBP power off */
	lnbp21_set_voltage(fe, SEC_VOLTAGE_OFF);

	/* free data */
	kfree(fe->sec_priv);
	fe->sec_priv = NULL;
}

static struct dvb_frontend *lnbx2x_attach(struct dvb_frontend *fe,
				struct i2c_adapter *i2c, u8 override_set,
				u8 override_clear, u8 i2c_addr, u8 config)
{
	struct lnbp21 *lnbp21 = kmalloc(sizeof(struct lnbp21), GFP_KERNEL);
	if (!lnbp21)
		return NULL;

	/* default configuration */
	lnbp21->config = config;
	lnbp21->i2c = i2c;
	lnbp21->i2c_addr = i2c_addr;
	fe->sec_priv = lnbp21;

	/* bits which should be forced to '1' */
	lnbp21->override_or = override_set;

	/* bits which should be forced to '0' */
	lnbp21->override_and = ~override_clear;

	/* detect if it is present or not */
	if (lnbp21_set_voltage(fe, SEC_VOLTAGE_OFF)) {
		kfree(lnbp21);
		return NULL;
	}

	/* install release callback */
	fe->ops.release_sec = lnbp21_release;

	/* override frontend ops */
	fe->ops.set_voltage = lnbp21_set_voltage;
	fe->ops.enable_high_lnb_voltage = lnbp21_enable_high_lnb_voltage;
	if (!(override_clear & LNBH24_TEN)) /*22kHz logic controlled by demod*/
		fe->ops.set_tone = lnbp21_set_tone;
	printk(KERN_INFO "LNBx2x attached on addr=%x\n", lnbp21->i2c_addr);

	return fe;
}

struct dvb_frontend *lnbh24_attach(struct dvb_frontend *fe,
				struct i2c_adapter *i2c, u8 override_set,
				u8 override_clear, u8 i2c_addr)
{
	return lnbx2x_attach(fe, i2c, override_set, override_clear,
							i2c_addr, LNBH24_TTX);
}
EXPORT_SYMBOL_GPL(lnbh24_attach);

struct dvb_frontend *lnbp21_attach(struct dvb_frontend *fe,
				struct i2c_adapter *i2c, u8 override_set,
				u8 override_clear)
{
	return lnbx2x_attach(fe, i2c, override_set, override_clear,
							0x08, LNBP21_ISEL);
}
EXPORT_SYMBOL_GPL(lnbp21_attach);

MODULE_DESCRIPTION("Driver for lnb supply and control ic lnbp21, lnbh24");
MODULE_AUTHOR("Oliver Endriss, Igor M. Liplianin");
MODULE_LICENSE("GPL");
