/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __VIA_I2C_H__
#define __VIA_I2C_H__

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

struct via_i2c_stuff {
	u16 i2c_port;			/* GPIO or I2C port */
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo;
};

#define I2CPORT           0x3c4
#define I2CPORTINDEX      0x31
#define GPIOPORT          0x3C4
#define GPIOPORTINDEX     0x2C
#define I2C_BUS             1
#define GPIO_BUS            2
#define DELAYPORT           0x3C3

int viafb_i2c_readbyte(u8 slave_addr, u8 index, u8 *pdata);
int viafb_i2c_writebyte(u8 slave_addr, u8 index, u8 data);
int viafb_i2c_readbytes(u8 slave_addr, u8 index, u8 *buff, int buff_len);
int viafb_create_i2c_bus(void *par);
void viafb_delete_i2c_buss(void *par);
#endif /* __VIA_I2C_H__ */
