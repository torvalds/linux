/*
 * lnbp22.h - driver for lnb supply and control ic lnbp22
 *
 * Copyright (C) 2006 Dominik Kuhlen
 * Based on lnbp21 driver
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
#include "lnbp22.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");


#define dprintk(lvl, arg...) if (debug >= (lvl)) printk(arg)

struct lnbp22 {
	u8		    config[4];
	struct i2c_adapter *i2c;
};

static int lnbp22_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct lnbp22 *lnbp22 = (struct lnbp22 *)fe->sec_priv;
	struct i2c_msg msg = {
		.addr = 0x08,
		.flags = 0,
		.buf = (char *)&lnbp22->config,
		.len = sizeof(lnbp22->config),
	};

	dprintk(1, "%s: %d (18V=%d 13V=%d)\n", __func__, voltage,
	       SEC_VOLTAGE_18, SEC_VOLTAGE_13);

	lnbp22->config[3] = 0x60; /* Power down */
	switch (voltage) {
	case SEC_VOLTAGE_OFF:
		break;
	case SEC_VOLTAGE_13:
		lnbp22->config[3] |= LNBP22_EN;
		break;
	case SEC_VOLTAGE_18:
		lnbp22->config[3] |= (LNBP22_EN | LNBP22_VSEL);
		break;
	default:
		return -EINVAL;
	}

	dprintk(1, "%s: 0x%02x)\n", __func__, lnbp22->config[3]);
	return (i2c_transfer(lnbp22->i2c, &msg, 1) == 1) ? 0 : -EIO;
}

static int lnbp22_enable_high_lnb_voltage(struct dvb_frontend *fe, long arg)
{
	struct lnbp22 *lnbp22 = (struct lnbp22 *) fe->sec_priv;
	struct i2c_msg msg = {
		.addr = 0x08,
		.flags = 0,
		.buf = (char *)&lnbp22->config,
		.len = sizeof(lnbp22->config),
	};

	dprintk(1, "%s: %d\n", __func__, (int)arg);
	if (arg)
		lnbp22->config[3] |= LNBP22_LLC;
	else
		lnbp22->config[3] &= ~LNBP22_LLC;

	return (i2c_transfer(lnbp22->i2c, &msg, 1) == 1) ? 0 : -EIO;
}

static void lnbp22_release(struct dvb_frontend *fe)
{
	dprintk(1, "%s\n", __func__);
	/* LNBP power off */
	lnbp22_set_voltage(fe, SEC_VOLTAGE_OFF);

	/* free data */
	kfree(fe->sec_priv);
	fe->sec_priv = NULL;
}

struct dvb_frontend *lnbp22_attach(struct dvb_frontend *fe,
					struct i2c_adapter *i2c)
{
	struct lnbp22 *lnbp22 = kmalloc(sizeof(struct lnbp22), GFP_KERNEL);
	if (!lnbp22)
		return NULL;

	/* default configuration */
	lnbp22->config[0] = 0x00; /* ? */
	lnbp22->config[1] = 0x28; /* ? */
	lnbp22->config[2] = 0x48; /* ? */
	lnbp22->config[3] = 0x60; /* Power down */
	lnbp22->i2c = i2c;
	fe->sec_priv = lnbp22;

	/* detect if it is present or not */
	if (lnbp22_set_voltage(fe, SEC_VOLTAGE_OFF)) {
		dprintk(0, "%s LNBP22 not found\n", __func__);
		kfree(lnbp22);
		fe->sec_priv = NULL;
		return NULL;
	}

	/* install release callback */
	fe->ops.release_sec = lnbp22_release;

	/* override frontend ops */
	fe->ops.set_voltage = lnbp22_set_voltage;
	fe->ops.enable_high_lnb_voltage = lnbp22_enable_high_lnb_voltage;

	return fe;
}
EXPORT_SYMBOL(lnbp22_attach);

MODULE_DESCRIPTION("Driver for lnb supply and control ic lnbp22");
MODULE_AUTHOR("Dominik Kuhlen");
MODULE_LICENSE("GPL");
