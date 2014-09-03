/*
 *  tw68 code to handle the i2c interface.
 *
 *  Much of this code is derived from the bt87x driver.  The original
 *  work was by Gerd Knorr; more recently the code was enhanced by Mauro
 *  Carvalho Chehab.  Their work is gratefully acknowledged.  Full credit
 *  goes to them - any problems within this code are mine.
 *
 *  Copyright (C) 2009  William M. Brack <wbrack@mmm.com.hk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "tw68.h"
#include <media/v4l2-common.h>
#include <linux/i2c-algo-bit.h>

/*----------------------------------------------------------------*/

static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

#if 0
static unsigned int i2c_scan;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");
#endif

#define d1printk if (1 == i2c_debug) printk

#define	I2C_CLOCK	0xa6	/* 99.4 kHz */

/*----------------------------------------------------------------------*/
/* Although the TW68xx i2c controller has a "hardware" mode, where all of
 * the low-level i2c/smbbus is handled by the chip, it appears that mode
 * is not suitable for linux i2c handling routines because extended "bursts"
 * of data (sequences of bytes without intervening START/STOP bits) are
 * not possible.  Instead, we put the chip into "software" mode, and handle
 * the i2c bus at a low level.  To accomplish this, we use the routines
 * from the i2c modules.
 *
 * Because the particular boards which I had for testing did not have any
 * devices attached to the i2c bus, I have been unable to test these
 * routines.
 */

/*----------------------------------------------------------------------*/
/* I2C functions - "bit-banging" adapter (software i2c) 		*/

/* tw68_bit_setcl
 * Handles "toggling" the i2c clock bit
 */
static void tw68_bit_setscl(void *data, int state)
{
	struct tw68_dev *dev = data;

	tw_andorb(TW68_SBUSC, (state ? 1 : 0) << TW68_SSCLK, TW68_SSCLK_B);
}

/* tw68_bit_setsda
 * Handles "toggling" the i2c data bit
 */
static void tw68_bit_setsda(void *data, int state)
{
	struct tw68_dev *dev = data;

	tw_andorb(TW68_SBUSC, (state ? 1 : 0) << TW68_SSDAT, TW68_SSDAT_B);
}

/* tw68_bit_getscl
 *
 * Returns the current state of the clock bit
 */
static int tw68_bit_getscl(void *data)
{
	struct tw68_dev *dev = data;

	return (tw_readb(TW68_SBUSC) & TW68_SSCLK_B) ? 1 : 0;
}

/* tw68_bit_getsda
 *
 * Returns the current state of the data bit
 */
static int tw68_bit_getsda(void *data)
{
	struct tw68_dev *dev = data;

	return (tw_readb(TW68_SBUSC) & TW68_SSDAT_B) ? 1 : 0;
}

static struct i2c_algo_bit_data __devinitdata tw68_i2c_algo_bit_template = {
	.setsda	 = tw68_bit_setsda,
	.setscl	 = tw68_bit_setscl,
	.getsda	 = tw68_bit_getsda,
	.getscl	 = tw68_bit_getscl,
	.udelay	 = 16,
	.timeout = 200,
};

static struct i2c_client tw68_client_template = {
	.name		= "tw68 internal",
};

/*----------------------------------------------------------------*/

static int attach_inform(struct i2c_client *client)
{
/*	struct tw68_dev *dev = client->adapter->algo_data; */

	d1printk("%s i2c attach [addr=0x%x,client=%s]\n",
		client->driver->driver.name, client->addr, client->name);

	switch (client->addr) {
		/* No info yet on what addresses to expect */
	}

	return 0;
}

static struct i2c_adapter tw68_adap_sw_template = {
	.owner		= THIS_MODULE,
	.name		= "tw68_sw",
	.client_register = attach_inform,
};

static int tw68_i2c_eeprom(struct tw68_dev *dev, unsigned char *eedata,
			   int len)
{
	unsigned char buf;
	int i, err;

	dev->i2c_client.addr = 0xa0 >> 1;
	buf = 256 - len;

	err = i2c_master_send(&dev->i2c_client, &buf, 1);
	if (1 != err) {
		printk(KERN_INFO "%s: Huh, no eeprom present (err = %d)?\n",
			dev->name, err);
		return -1;
	}
	err = i2c_master_recv(&dev->i2c_client, eedata, len);
	if (len != err) {
		printk(KERN_WARNING "%s: i2c eeprom read error (err=%d)\n",
			dev->name, err);
		return -1;
	}

	for (i = 0; i < len; i++) {
		if (0 == (i % 16))
			printk(KERN_INFO "%s: i2c eeprom %02x:",
				dev->name, i);
		printk(KERN_INFO " %02x", eedata[i]);
		if (15 == (i % 16))
			printk("\n");
	}
	return 0;
}

#if 0
static char *i2c_devs[128] = {
	[0xa0 >> 1] = "eeprom",
};

static void do_i2c_scan(char *name, struct i2c_client *c)
{
	unsigned char buf;
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(i2c_devs); i++) {
		c->addr = i;
		rc = i2c_master_recv(c, &buf, 1);
		if (rc < 0)
			continue;
		printk(KERN_INFO "%s: i2c scan: found device "
				 "@ 0x%x [%s]\n", name, i << 1,
				 i2c_devs[i] ? i2c_devs[i] : "???");
	}
}
#endif

int __devinit tw68_i2c_register(struct tw68_dev *dev)
{
	int rc;

printk(KERN_DEBUG "%s: Registering i2c module\n", __func__);
	tw_writeb(TW68_I2C_RST, 1);	/* reset the i2c module */

	memcpy(&dev->i2c_client, &tw68_client_template,
		sizeof(tw68_client_template));

	memcpy(&dev->i2c_adap, &tw68_adap_sw_template,
		sizeof(tw68_adap_sw_template));
	dev->i2c_adap.algo_data = &dev->i2c_algo;
	dev->i2c_adap.dev.parent = &dev->pci->dev;

	memcpy(&dev->i2c_algo, &tw68_i2c_algo_bit_template,
		sizeof(tw68_i2c_algo_bit_template));
	dev->i2c_algo.data = dev;
	/* TODO - may want to set better name (see bttv code) */

	i2c_set_adapdata(&dev->i2c_adap, &dev->v4l2_dev);
	dev->i2c_client.adapter = &dev->i2c_adap;

	/* Assure chip is in "software" mode */
	tw_writel(TW68_SBUSC, TW68_SSDAT | TW68_SSCLK);
	tw68_bit_setscl(dev, 1);
	tw68_bit_setsda(dev, 1);

	rc = i2c_bit_add_bus(&dev->i2c_adap);

	tw68_i2c_eeprom(dev, dev->eedata, sizeof(dev->eedata));
#if 0
	if (i2c_scan)
		do_i2c_scan(dev->name, &dev->i2c_client);
#endif

	return rc;
}

int tw68_i2c_unregister(struct tw68_dev *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
}
