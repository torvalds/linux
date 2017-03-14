/*
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <media/v4l2-device.h>
#include "common.h"

/*
 * Read EEPROM data from the gerneral e2prom chip(eg.
 * CAT24C08, CAT24C128, le24l042cs, and store
 * it into a kmalloced buffer. On error return NULL.
 * @size: set to the size of the returned EEPROM data.
 */
void *e2prom_otp_read(struct v4l2_subdev *sd, u8 dev_addr,
	u32 start_addr, u32 size)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned int e2prom_i2c_addr = dev_addr >> 1;
	static const unsigned int max_read_size = 30;
	int addr;
	u32 s_addr = start_addr & E2PROM_ADDR_MASK;
	bool two_addr = (start_addr & E2PROM_2ADDR) >> 31;
	char *buffer;

	buffer = devm_kzalloc(&client->dev, size, GFP_KERNEL);
	if (!buffer)
		return NULL;

	for (addr = s_addr; addr < size; addr += max_read_size) {
		struct i2c_msg msg[2];
		unsigned int i2c_addr = e2prom_i2c_addr;
		u16 addr_buf;
		int r;

		msg[0].flags = 0;
		if (two_addr) {
			msg[0].addr = i2c_addr;
			addr_buf = cpu_to_be16(addr & 0xFFFF);
			msg[0].len = 2;
			msg[0].buf = (u8 *)&addr_buf;
		} else {
			i2c_addr |= (addr >> 8) & 0x7;
			msg[0].addr = i2c_addr;
			addr_buf = addr & 0xFF;
			msg[0].len = 1;
			msg[0].buf = (u8 *)&addr_buf;
		}

		msg[1].addr = i2c_addr;
		msg[1].flags = I2C_M_RD;
		msg[1].len = min(max_read_size, size - addr);
		msg[1].buf = &buffer[addr];

		r = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
		if (r != ARRAY_SIZE(msg)) {
			dev_err(&client->dev, "read failed at 0x%03x\n", addr);
			return NULL;
		}
	}
	return buffer;
}


