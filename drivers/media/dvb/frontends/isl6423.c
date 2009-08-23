/*
	Intersil ISL6423 SEC and LNB Power supply controller

	Copyright (C) Manu Abraham <abraham.manu@gmail.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "isl6423.h"

static unsigned int verbose;
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "Set Verbosity level");

#define FE_ERROR				0
#define FE_NOTICE				1
#define FE_INFO					2
#define FE_DEBUG				3
#define FE_DEBUGREG				4

#define dprintk(__y, __z, format, arg...) do {						\
	if (__z) {									\
		if	((verbose > FE_ERROR) && (verbose > __y))			\
			printk(KERN_ERR "%s: " format "\n", __func__ , ##arg);		\
		else if	((verbose > FE_NOTICE) && (verbose > __y))			\
			printk(KERN_NOTICE "%s: " format "\n", __func__ , ##arg);	\
		else if ((verbose > FE_INFO) && (verbose > __y))			\
			printk(KERN_INFO "%s: " format "\n", __func__ , ##arg);		\
		else if ((verbose > FE_DEBUG) && (verbose > __y))			\
			printk(KERN_DEBUG "%s: " format "\n", __func__ , ##arg);	\
	} else {									\
		if (verbose > __y)							\
			printk(format, ##arg);						\
	}										\
} while (0)

struct isl6423_dev {
	const struct isl6423_config	*config;
	struct i2c_adapter		*i2c;

	u8 reg_3;
	u8 reg_4;

	unsigned int verbose;
};

static int isl6423_write(struct isl6423_dev *isl6423, u8 reg)
{
	struct i2c_adapter *i2c = isl6423->i2c;
	u8 addr			= isl6423->config->addr;
	int err = 0;

	struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = &reg, .len = 1 };

	dprintk(FE_DEBUG, 1, "write reg %02X", reg);
	err = i2c_transfer(i2c, &msg, 1);
	if (err < 0)
		goto exit;
	return 0;

exit:
	dprintk(FE_ERROR, 1, "I/O error <%d>", err);
	return err;
}

static int isl6423_set_modulation(struct dvb_frontend *fe)
{
	struct isl6423_dev *isl6423		= (struct isl6423_dev *) fe->sec_priv;
	const struct isl6423_config *config	= isl6423->config;
	int err = 0;
	u8 reg_2 = 0;

	reg_2 = 0x01 << 5;

	if (config->mod_extern)
		reg_2 |= (1 << 3);
	else
		reg_2 |= (1 << 4);

	err = isl6423_write(isl6423, reg_2);
	if (err < 0)
		goto exit;
	return 0;

exit:
	dprintk(FE_ERROR, 1, "I/O error <%d>", err);
	return err;
}

static int isl6423_voltage_boost(struct dvb_frontend *fe, long arg)
{
	struct isl6423_dev *isl6423 = (struct isl6423_dev *) fe->sec_priv;
	u8 reg_3 = isl6423->reg_3;
	u8 reg_4 = isl6423->reg_4;
	int err = 0;

	if (arg) {
		/* EN = 1, VSPEN = 1, VBOT = 1 */
		reg_4 |= (1 << 4);
		reg_4 |= 0x1;
		reg_3 |= (1 << 3);
	} else {
		/* EN = 1, VSPEN = 1, VBOT = 0 */
		reg_4 |= (1 << 4);
		reg_4 &= ~0x1;
		reg_3 |= (1 << 3);
	}
	err = isl6423_write(isl6423, reg_3);
	if (err < 0)
		goto exit;

	err = isl6423_write(isl6423, reg_4);
	if (err < 0)
		goto exit;

	isl6423->reg_3 = reg_3;
	isl6423->reg_4 = reg_4;

	return 0;
exit:
	dprintk(FE_ERROR, 1, "I/O error <%d>", err);
	return err;
}


static int isl6423_set_voltage(struct dvb_frontend *fe,
			       enum fe_sec_voltage voltage)
{
	struct isl6423_dev *isl6423 = (struct isl6423_dev *) fe->sec_priv;
	u8 reg_3 = isl6423->reg_3;
	u8 reg_4 = isl6423->reg_4;
	int err = 0;

	switch (voltage) {
	case SEC_VOLTAGE_OFF:
		/* EN = 0 */
		reg_4 &= ~(1 << 4);
		break;

	case SEC_VOLTAGE_13:
		/* EN = 1, VSPEN = 1, VTOP = 0, VBOT = 0 */
		reg_4 |= (1 << 4);
		reg_4 &= ~0x3;
		reg_3 |= (1 << 3);
		break;

	case SEC_VOLTAGE_18:
		/* EN = 1, VSPEN = 1, VTOP = 1, VBOT = 0 */
		reg_4 |= (1 << 4);
		reg_4 |=  0x2;
		reg_4 &= ~0x1;
		reg_3 |= (1 << 3);
		break;

	default:
		break;
	}
	err = isl6423_write(isl6423, reg_3);
	if (err < 0)
		goto exit;

	err = isl6423_write(isl6423, reg_4);
	if (err < 0)
		goto exit;

	isl6423->reg_3 = reg_3;
	isl6423->reg_4 = reg_4;

	return 0;
exit:
	dprintk(FE_ERROR, 1, "I/O error <%d>", err);
	return err;
}

static int isl6423_set_current(struct dvb_frontend *fe)
{
	struct isl6423_dev *isl6423		= (struct isl6423_dev *) fe->sec_priv;
	u8 reg_3 = isl6423->reg_3;
	const struct isl6423_config *config	= isl6423->config;
	int err = 0;

	switch (config->current_max) {
	case SEC_CURRENT_275m:
		/* 275mA */
		/* ISELH = 0, ISELL = 0 */
		reg_3 &= ~0x3;
		break;

	case SEC_CURRENT_515m:
		/* 515mA */
		/* ISELH = 0, ISELL = 1 */
		reg_3 &= ~0x2;
		reg_3 |=  0x1;
		break;

	case SEC_CURRENT_635m:
		/* 635mA */
		/* ISELH = 1, ISELL = 0 */
		reg_3 &= ~0x1;
		reg_3 |=  0x2;
		break;

	case SEC_CURRENT_800m:
		/* 800mA */
		/* ISELH = 1, ISELL = 1 */
		reg_3 |= 0x3;
		break;
	}

	err = isl6423_write(isl6423, reg_3);
	if (err < 0)
		goto exit;

	switch (config->curlim) {
	case SEC_CURRENT_LIM_ON:
		/* DCL = 0 */
		reg_3 &= ~0x10;
		break;

	case SEC_CURRENT_LIM_OFF:
		/* DCL = 1 */
		reg_3 |= 0x10;
		break;
	}

	err = isl6423_write(isl6423, reg_3);
	if (err < 0)
		goto exit;

	isl6423->reg_3 = reg_3;

	return 0;
exit:
	dprintk(FE_ERROR, 1, "I/O error <%d>", err);
	return err;
}

static void isl6423_release(struct dvb_frontend *fe)
{
	isl6423_set_voltage(fe, SEC_VOLTAGE_OFF);

	kfree(fe->sec_priv);
	fe->sec_priv = NULL;
}

struct dvb_frontend *isl6423_attach(struct dvb_frontend *fe,
				    struct i2c_adapter *i2c,
				    const struct isl6423_config *config)
{
	struct isl6423_dev *isl6423;

	isl6423 = kzalloc(sizeof(struct isl6423_dev), GFP_KERNEL);
	if (!isl6423)
		return NULL;

	isl6423->config	= config;
	isl6423->i2c	= i2c;
	fe->sec_priv	= isl6423;

	/* SR3H = 0, SR3M = 1, SR3L = 0 */
	isl6423->reg_3 = 0x02 << 5;
	/* SR4H = 0, SR4M = 1, SR4L = 1 */
	isl6423->reg_4 = 0x03 << 5;

	if (isl6423_set_current(fe))
		goto exit;

	if (isl6423_set_modulation(fe))
		goto exit;

	fe->ops.release_sec		= isl6423_release;
	fe->ops.set_voltage		= isl6423_set_voltage;
	fe->ops.enable_high_lnb_voltage = isl6423_voltage_boost;
	isl6423->verbose		= verbose;

	return fe;

exit:
	kfree(isl6423);
	fe->sec_priv = NULL;
	return NULL;
}
EXPORT_SYMBOL(isl6423_attach);

MODULE_DESCRIPTION("ISL6423 SEC");
MODULE_AUTHOR("Manu Abraham");
MODULE_LICENSE("GPL");
