
/*
 *
 * cx88-i2c.c  --  all the i2c code is here
 *
 * Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
 *			   & Marcus Metzler (mocm@thp.uni-koeln.de)
 * (c) 2002 Yurij Sysoev <yurij@naturesoft.net>
 * (c) 1999-2003 Gerd Knorr <kraxel@bytesex.org>
 *
 * (c) 2005 Mauro Carvalho Chehab <mchehab@infradead.org>
 *	- Multituner support and i2c address binding
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "cx88.h"

#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>

#include <media/v4l2-common.h>

static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

static unsigned int i2c_scan;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");

static unsigned int i2c_udelay = 5;
module_param(i2c_udelay, int, 0644);
MODULE_PARM_DESC(i2c_udelay,
		 "i2c delay at insmod time, in usecs (should be 5 or higher). Lower value means higher bus speed.");

#define dprintk(level, fmt, arg...) do {				\
	if (i2c_debug >= level)						\
		printk(KERN_DEBUG pr_fmt("%s: i2c:" fmt),		\
			__func__, ##arg);				\
} while (0)

/* ----------------------------------------------------------------------- */

static void cx8800_bit_setscl(void *data, int state)
{
	struct cx88_core *core = data;

	if (state)
		core->i2c_state |= 0x02;
	else
		core->i2c_state &= ~0x02;
	cx_write(MO_I2C, core->i2c_state);
	cx_read(MO_I2C);
}

static void cx8800_bit_setsda(void *data, int state)
{
	struct cx88_core *core = data;

	if (state)
		core->i2c_state |= 0x01;
	else
		core->i2c_state &= ~0x01;
	cx_write(MO_I2C, core->i2c_state);
	cx_read(MO_I2C);
}

static int cx8800_bit_getscl(void *data)
{
	struct cx88_core *core = data;
	u32 state;

	state = cx_read(MO_I2C);
	return state & 0x02 ? 1 : 0;
}

static int cx8800_bit_getsda(void *data)
{
	struct cx88_core *core = data;
	u32 state;

	state = cx_read(MO_I2C);
	return state & 0x01;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_algo_bit_data cx8800_i2c_algo_template = {
	.setsda  = cx8800_bit_setsda,
	.setscl  = cx8800_bit_setscl,
	.getsda  = cx8800_bit_getsda,
	.getscl  = cx8800_bit_getscl,
	.udelay  = 16,
	.timeout = 200,
};

/* ----------------------------------------------------------------------- */

static const char * const i2c_devs[128] = {
	[0x1c >> 1] = "lgdt330x",
	[0x86 >> 1] = "tda9887/cx22702",
	[0xa0 >> 1] = "eeprom",
	[0xc0 >> 1] = "tuner (analog)",
	[0xc2 >> 1] = "tuner (analog/dvb)",
	[0xc8 >> 1] = "xc5000",
};

static void do_i2c_scan(const char *name, struct i2c_client *c)
{
	unsigned char buf;
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(i2c_devs); i++) {
		c->addr = i;
		rc = i2c_master_recv(c, &buf, 0);
		if (rc < 0)
			continue;
		pr_info("i2c scan: found device @ 0x%x  [%s]\n",
			i << 1, i2c_devs[i] ? i2c_devs[i] : "???");
	}
}

/* init + register i2c adapter */
int cx88_i2c_init(struct cx88_core *core, struct pci_dev *pci)
{
	/* Prevents usage of invalid delay values */
	if (i2c_udelay < 5)
		i2c_udelay = 5;

	core->i2c_algo = cx8800_i2c_algo_template;

	core->i2c_adap.dev.parent = &pci->dev;
	strlcpy(core->i2c_adap.name, core->name, sizeof(core->i2c_adap.name));
	core->i2c_adap.owner = THIS_MODULE;
	core->i2c_algo.udelay = i2c_udelay;
	core->i2c_algo.data = core;
	i2c_set_adapdata(&core->i2c_adap, &core->v4l2_dev);
	core->i2c_adap.algo_data = &core->i2c_algo;
	core->i2c_client.adapter = &core->i2c_adap;
	strlcpy(core->i2c_client.name, "cx88xx internal", I2C_NAME_SIZE);

	cx8800_bit_setscl(core, 1);
	cx8800_bit_setsda(core, 1);

	core->i2c_rc = i2c_bit_add_bus(&core->i2c_adap);
	if (core->i2c_rc == 0) {
		static u8 tuner_data[] = {
			0x0b, 0xdc, 0x86, 0x52 };
		static struct i2c_msg tuner_msg = {
			.flags = 0,
			.addr = 0xc2 >> 1,
			.buf = tuner_data,
			.len = 4
		};

		dprintk(1, "i2c register ok\n");
		switch (core->boardnr) {
		case CX88_BOARD_HAUPPAUGE_HVR1300:
		case CX88_BOARD_HAUPPAUGE_HVR3000:
		case CX88_BOARD_HAUPPAUGE_HVR4000:
			pr_info("i2c init: enabling analog demod on HVR1300/3000/4000 tuner\n");
			i2c_transfer(core->i2c_client.adapter, &tuner_msg, 1);
			break;
		default:
			break;
		}
		if (i2c_scan)
			do_i2c_scan(core->name, &core->i2c_client);
	} else
		pr_err("i2c register FAILED\n");

	return core->i2c_rc;
}
