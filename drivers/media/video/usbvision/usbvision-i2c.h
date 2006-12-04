/* 
 * I2C_ALGO_USB.H
 *  i2c algorithm for USB-I2C Bridges
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
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


#ifndef I2C_ALGO_USB_H
#define I2C_ALGO_USB_H 1 

#include <linux/i2c.h>

struct i2c_algo_usb_data {
	void *data;		/* private data for lowlevel routines */
	int (*inb) (void *data, unsigned char addr, char *buf, short len);
	int (*outb) (void *data, unsigned char addr, char *buf, short len);

	/* local settings */
	int udelay;
	int mdelay;
	int timeout;
};

#define I2C_USB_ADAP_MAX	16

int usbvision_i2c_usb_add_bus(struct i2c_adapter *);
int usbvision_i2c_usb_del_bus(struct i2c_adapter *);

static inline void *i2c_get_algo_usb_data (struct i2c_algo_usb_data *dev)
{
	return dev->data;
}

static inline void i2c_set_algo_usb_data (struct i2c_algo_usb_data *dev, void *data)
{
	dev->data = data;
}


#endif //I2C_ALGO_USB_H
