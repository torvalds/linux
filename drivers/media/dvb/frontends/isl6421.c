/*
 * isl6421.h - driver for lnb supply and control ic ISL6421
 *
 * Copyright (C) 2006 Andrew de Quincey
 * Copyright (C) 2006 Oliver Endriss
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at http://www.linuxtv.org
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "isl6421.h"

struct isl6421 {
	u8			config;
	u8			override_or;
	u8			override_and;
	struct i2c_adapter	*i2c;
	u8			i2c_addr;
};

static int isl6421_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct isl6421 *isl6421 = (struct isl6421 *) fe->sec_priv;
	struct i2c_msg msg = {	.addr = isl6421->i2c_addr, .flags = 0,
				.buf = &isl6421->config,
				.len = sizeof(isl6421->config) };

	isl6421->config &= ~(ISL6421_VSEL1 | ISL6421_EN1);

	switch(voltage) {
	case SEC_VOLTAGE_OFF:
		break;
	case SEC_VOLTAGE_13:
		isl6421->config |= ISL6421_EN1;
		break;
	case SEC_VOLTAGE_18:
		isl6421->config |= (ISL6421_EN1 | ISL6421_VSEL1);
		break;
	default:
		return -EINVAL;
	};

	isl6421->config |= isl6421->override_or;
	isl6421->config &= isl6421->override_and;

	return (i2c_transfer(isl6421->i2c, &msg, 1) == 1) ? 0 : -EIO;
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

static void isl6421_release(struct dvb_frontend *fe)
{
	/* power off */
	isl6421_set_voltage(fe, SEC_VOLTAGE_OFF);

	/* free */
	kfree(fe->sec_priv);
	fe->sec_priv = NULL;
}

struct dvb_frontend *isl6421_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, u8 i2c_addr,
		   u8 override_set, u8 override_clear)
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
		return NULL;
	}

	/* install release callback */
	fe->ops.release_sec = isl6421_release;

	/* override frontend ops */
	fe->ops.set_voltage = isl6421_set_voltage;
	fe->ops.enable_high_lnb_voltage = isl6421_enable_high_lnb_voltage;

	return fe;
}
EXPORT_SYMBOL(isl6421_attach);

MODULE_DESCRIPTION("Driver for lnb supply and control ic isl6421");
MODULE_AUTHOR("Andrew de Quincey & Oliver Endriss");
MODULE_LICENSE("GPL");
