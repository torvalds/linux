/*
 * 	w1_io.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * 
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/io.h>

#include <linux/delay.h>
#include <linux/moduleparam.h>

#include "w1.h"
#include "w1_log.h"
#include "w1_io.h"

int w1_delay_parm = 1;
module_param_named(delay_coef, w1_delay_parm, int, 0);

static u8 w1_crc8_table[] = {
	0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
	157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
	35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
	190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
	70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
	219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
	101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
	248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
	140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
	17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
	175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
	50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
	202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
	87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
	233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
	116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53
};

void w1_delay(unsigned long tm)
{
	udelay(tm * w1_delay_parm);
}

u8 w1_touch_bit(struct w1_master *dev, int bit)
{
	if (dev->bus_master->touch_bit)
		return dev->bus_master->touch_bit(dev->bus_master->data, bit);
	else
		return w1_read_bit(dev);
}

void w1_write_bit(struct w1_master *dev, int bit)
{
	if (bit) {
		dev->bus_master->write_bit(dev->bus_master->data, 0);
		w1_delay(6);
		dev->bus_master->write_bit(dev->bus_master->data, 1);
		w1_delay(64);
	} else {
		dev->bus_master->write_bit(dev->bus_master->data, 0);
		w1_delay(60);
		dev->bus_master->write_bit(dev->bus_master->data, 1);
		w1_delay(10);
	}
}

void w1_write_8(struct w1_master *dev, u8 byte)
{
	int i;

	if (dev->bus_master->write_byte)
		dev->bus_master->write_byte(dev->bus_master->data, byte);
	else
		for (i = 0; i < 8; ++i)
			w1_write_bit(dev, (byte >> i) & 0x1);
}

u8 w1_read_bit(struct w1_master *dev)
{
	int result;

	dev->bus_master->write_bit(dev->bus_master->data, 0);
	w1_delay(6);
	dev->bus_master->write_bit(dev->bus_master->data, 1);
	w1_delay(9);

	result = dev->bus_master->read_bit(dev->bus_master->data);
	w1_delay(55);

	return result & 0x1;
}

u8 w1_read_8(struct w1_master * dev)
{
	int i;
	u8 res = 0;

	if (dev->bus_master->read_byte)
		res = dev->bus_master->read_byte(dev->bus_master->data);
	else
		for (i = 0; i < 8; ++i)
			res |= (w1_read_bit(dev) << i);

	return res;
}

void w1_write_block(struct w1_master *dev, u8 *buf, int len)
{
	int i;

	if (dev->bus_master->write_block)
		dev->bus_master->write_block(dev->bus_master->data, buf, len);
	else
		for (i = 0; i < len; ++i)
			w1_write_8(dev, buf[i]);
}

u8 w1_read_block(struct w1_master *dev, u8 *buf, int len)
{
	int i;
	u8 ret;

	if (dev->bus_master->read_block)
		ret = dev->bus_master->read_block(dev->bus_master->data, buf, len);
	else {
		for (i = 0; i < len; ++i)
			buf[i] = w1_read_8(dev);
		ret = len;
	}

	return ret;
}

int w1_reset_bus(struct w1_master *dev)
{
	int result = 0;

	if (dev->bus_master->reset_bus)
		result = dev->bus_master->reset_bus(dev->bus_master->data) & 0x1;
	else {
		dev->bus_master->write_bit(dev->bus_master->data, 0);
		w1_delay(480);
		dev->bus_master->write_bit(dev->bus_master->data, 1);
		w1_delay(70);

		result = dev->bus_master->read_bit(dev->bus_master->data) & 0x1;
		w1_delay(410);
	}

	return result;
}

u8 w1_calc_crc8(u8 * data, int len)
{
	u8 crc = 0;

	while (len--)
		crc = w1_crc8_table[crc ^ *data++];

	return crc;
}

void w1_search_devices(struct w1_master *dev, w1_slave_found_callback cb)
{
	dev->attempts++;
	if (dev->bus_master->search)
		dev->bus_master->search(dev->bus_master->data, cb);
	else
		w1_search(dev);
}

EXPORT_SYMBOL(w1_write_bit);
EXPORT_SYMBOL(w1_write_8);
EXPORT_SYMBOL(w1_read_bit);
EXPORT_SYMBOL(w1_read_8);
EXPORT_SYMBOL(w1_reset_bus);
EXPORT_SYMBOL(w1_calc_crc8);
EXPORT_SYMBOL(w1_delay);
EXPORT_SYMBOL(w1_read_block);
EXPORT_SYMBOL(w1_write_block);
EXPORT_SYMBOL(w1_search_devices);
