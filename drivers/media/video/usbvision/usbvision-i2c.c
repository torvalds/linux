/* 
 * I2C_ALGO_USB.C
 *  i2c algorithm for USB-I2C Bridges
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
 *                         Dwaine Garden <dwainegarden@rogers.com>
 *
 * This module is part of usbvision driver project.
 * Updates to driver completed by Dwaine P. Garden
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/version.h>
	#include <linux/utsname.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include "usbvision-i2c.h"

static int debug_i2c_usb = 0;	

#if defined(module_param)                               // Showing parameters under SYSFS
module_param (debug_i2c_usb, int, 0444);			// debug_i2c_usb mode of the device driver
#else
MODULE_PARM(debug_i2c_usb, "i");				// debug_i2c_usb mode of the device driver
#endif


static inline int try_write_address(struct i2c_adapter *i2c_adap,
				    unsigned char addr, int retries)
{
	struct i2c_algo_usb_data *adap = i2c_adap->algo_data;
	void *data;
	int i, ret = -1;
	char buf[4];

	data = i2c_get_adapdata(i2c_adap);
	buf[0] = 0x00;
	for (i = 0; i <= retries; i++) {
		ret = (adap->outb(data, addr, buf, 1));
		if (ret == 1)
			break;	/* success! */
		udelay(5 /*adap->udelay */ );
		if (i == retries)	/* no success */
			break;
		udelay(adap->udelay);
	}
	if (debug_i2c_usb) {
		if (i) {
			info("%s: Needed %d retries for address %#2x", __FUNCTION__, i, addr);
			info("%s: Maybe there's no device at this address", __FUNCTION__);
		}
	}
	return ret;
}

static inline int try_read_address(struct i2c_adapter *i2c_adap,
				   unsigned char addr, int retries)
{
	struct i2c_algo_usb_data *adap = i2c_adap->algo_data;
	void *data;
	int i, ret = -1;
	char buf[4];

	data = i2c_get_adapdata(i2c_adap);
	for (i = 0; i <= retries; i++) {
		ret = (adap->inb(data, addr, buf, 1));
		if (ret == 1)
			break;	/* success! */
		udelay(5 /*adap->udelay */ );
		if (i == retries)	/* no success */
			break;
		udelay(adap->udelay);
	}
	if (debug_i2c_usb) {
		if (i) {
			info("%s: Needed %d retries for address %#2x", __FUNCTION__, i, addr);
			info("%s: Maybe there's no device at this address", __FUNCTION__);
		}
	}
	return ret;
}

static inline int usb_find_address(struct i2c_adapter *i2c_adap,
				   struct i2c_msg *msg, int retries,
				   unsigned char *add)
{
	unsigned short flags = msg->flags;
	
	unsigned char addr;
	int ret;
	if ((flags & I2C_M_TEN)) {
		/* a ten bit address */
		addr = 0xf0 | ((msg->addr >> 7) & 0x03);
		/* try extended address code... */
		ret = try_write_address(i2c_adap, addr, retries);
		if (ret != 1) {
			err("died at extended address code, while writing");
			return -EREMOTEIO;
		}
		add[0] = addr;
		if (flags & I2C_M_RD) {
			/* okay, now switch into reading mode */
			addr |= 0x01;
			ret = try_read_address(i2c_adap, addr, retries);
			if (ret != 1) {
				err("died at extended address code, while reading");
				return -EREMOTEIO;
			}
		}

	} else {		/* normal 7bit address  */
		addr = (msg->addr << 1);
		if (flags & I2C_M_RD)
			addr |= 1;
		if (flags & I2C_M_REV_DIR_ADDR)
			addr ^= 1;

		add[0] = addr;
		if (flags & I2C_M_RD)
			ret = try_read_address(i2c_adap, addr, retries);
		else
			ret = try_write_address(i2c_adap, addr, retries);

		if (ret != 1) {
			return -EREMOTEIO;
		}
	}
	return 0;
}

static int
usb_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msgs[], int num)
{
	struct i2c_msg *pmsg;
	struct i2c_algo_usb_data *adap = i2c_adap->algo_data;
	void *data;
	int i, ret;
	unsigned char addr;

	data = i2c_get_adapdata(i2c_adap);

	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];
		ret = usb_find_address(i2c_adap, pmsg, i2c_adap->retries, &addr);
		if (ret != 0) {
			if (debug_i2c_usb) {
				info("%s: got NAK from device, message #%d\n", __FUNCTION__, i);
			}
			return (ret < 0) ? ret : -EREMOTEIO;
		}

		if (pmsg->flags & I2C_M_RD) {
			/* read bytes into buffer */
			ret = (adap->inb(data, addr, pmsg->buf, pmsg->len));
			if (ret < pmsg->len) {
				return (ret < 0) ? ret : -EREMOTEIO;
			}
		} else {
			/* write bytes from buffer */
			ret = (adap->outb(data, addr, pmsg->buf, pmsg->len));
			if (ret < pmsg->len) {
				return (ret < 0) ? ret : -EREMOTEIO;
			}
		}
	}
	return num;
}

static int algo_control(struct i2c_adapter *adapter, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static u32 usb_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR | I2C_FUNC_PROTOCOL_MANGLING;
}


/* -----exported algorithm data: -------------------------------------	*/

static struct i2c_algorithm i2c_usb_algo = {
	.master_xfer   = usb_xfer,
	.smbus_xfer    = NULL,
	.slave_send    = NULL,
	.slave_recv    = NULL,
	.algo_control  = algo_control,
	.functionality = usb_func,
};


/*
 * registering functions to load algorithms at runtime
 */
int usbvision_i2c_usb_add_bus(struct i2c_adapter *adap)
{
	/* register new adapter to i2c module... */

	adap->algo = &i2c_usb_algo;

	adap->timeout = 100;	/* default values, should       */
	adap->retries = 3;	/* be replaced by defines       */

#ifdef MODULE
	#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 21)
		MOD_INC_USE_COUNT;
	#endif
#endif

	i2c_add_adapter(adap);

	if (debug_i2c_usb) {
		info("i2c bus for %s registered", adap->name);
	}

	return 0;
}


int usbvision_i2c_usb_del_bus(struct i2c_adapter *adap)
{

	i2c_del_adapter(adap);

	if (debug_i2c_usb) {
		info("i2c bus for %s unregistered", adap->name);
	}
#ifdef MODULE
	#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 21)
		MOD_DEC_USE_COUNT;
	#endif
#endif

	return 0;
}

EXPORT_SYMBOL(usbvision_i2c_usb_add_bus);
EXPORT_SYMBOL(usbvision_i2c_usb_del_bus);
