// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	w1_ds2423.c
 *
 * Copyright (c) 2010 Mika Laitio <lamikr@pilppa.org>
 *
 * This driver will read and write the value of 4 counters to w1_slave file in
 * sys filesystem.
 * Inspired by the w1_therm and w1_ds2431 drivers.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/crc16.h>

#include <linux/w1.h>

#define W1_COUNTER_DS2423	0x1D

#define CRC16_VALID	0xb001
#define CRC16_INIT	0

#define COUNTER_COUNT 4
#define READ_BYTE_COUNT 42

static ssize_t w1_slave_show(struct device *device,
			     struct device_attribute *attr, char *out_buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct w1_master *dev = sl->master;
	u8 rbuf[COUNTER_COUNT * READ_BYTE_COUNT];
	u8 wrbuf[3];
	int rom_addr;
	int read_byte_count;
	int result;
	ssize_t c;
	int ii;
	int p;
	int crc;

	c		= PAGE_SIZE;
	rom_addr	= (12 << 5) + 31;
	wrbuf[0]	= 0xA5;
	wrbuf[1]	= rom_addr & 0xFF;
	wrbuf[2]	= rom_addr >> 8;
	mutex_lock(&dev->bus_mutex);
	if (!w1_reset_select_slave(sl)) {
		w1_write_block(dev, wrbuf, 3);
		read_byte_count = 0;
		for (p = 0; p < 4; p++) {
			/*
			 * 1 byte for first bytes in ram page read
			 * 4 bytes for counter
			 * 4 bytes for zero bits
			 * 2 bytes for crc
			 * 31 remaining bytes from the ram page
			 */
			read_byte_count += w1_read_block(dev,
				rbuf + (p * READ_BYTE_COUNT), READ_BYTE_COUNT);
			for (ii = 0; ii < READ_BYTE_COUNT; ++ii)
				c -= snprintf(out_buf + PAGE_SIZE - c,
					c, "%02x ",
					rbuf[(p * READ_BYTE_COUNT) + ii]);
			if (read_byte_count != (p + 1) * READ_BYTE_COUNT) {
				dev_warn(device,
					"w1_counter_read() returned %u bytes "
					"instead of %d bytes wanted.\n",
					read_byte_count,
					READ_BYTE_COUNT);
				c -= snprintf(out_buf + PAGE_SIZE - c,
					c, "crc=NO\n");
			} else {
				if (p == 0) {
					crc = crc16(CRC16_INIT, wrbuf, 3);
					crc = crc16(crc, rbuf, 11);
				} else {
					/*
					 * DS2423 calculates crc from all bytes
					 * read after the previous crc bytes.
					 */
					crc = crc16(CRC16_INIT,
						(rbuf + 11) +
						((p - 1) * READ_BYTE_COUNT),
						READ_BYTE_COUNT);
				}
				if (crc == CRC16_VALID) {
					result = 0;
					for (ii = 4; ii > 0; ii--) {
						result <<= 8;
						result |= rbuf[(p *
							READ_BYTE_COUNT) + ii];
					}
					c -= snprintf(out_buf + PAGE_SIZE - c,
						c, "crc=YES c=%d\n", result);
				} else {
					c -= snprintf(out_buf + PAGE_SIZE - c,
						c, "crc=NO\n");
				}
			}
		}
	} else {
		c -= snprintf(out_buf + PAGE_SIZE - c, c, "Connection error");
	}
	mutex_unlock(&dev->bus_mutex);
	return PAGE_SIZE - c;
}

static DEVICE_ATTR_RO(w1_slave);

static struct attribute *w1_f1d_attrs[] = {
	&dev_attr_w1_slave.attr,
	NULL,
};
ATTRIBUTE_GROUPS(w1_f1d);

static struct w1_family_ops w1_f1d_fops = {
	.groups		= w1_f1d_groups,
};

static struct w1_family w1_family_1d = {
	.fid = W1_COUNTER_DS2423,
	.fops = &w1_f1d_fops,
};
module_w1_family(w1_family_1d);

MODULE_AUTHOR("Mika Laitio <lamikr@pilppa.org>");
MODULE_DESCRIPTION("w1 family 1d driver for DS2423, 4 counters and 4kb ram");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_COUNTER_DS2423));
