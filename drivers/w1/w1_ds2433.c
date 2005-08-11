/*
 *	w1_ds2433.c - w1 family 23 (DS2433) driver
 *
 * Copyright (c) 2005 Ben Gardner <bgardner@wabtec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>

#include "w1.h"
#include "w1_io.h"
#include "w1_int.h"
#include "w1_family.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Gardner <bgardner@wabtec.com>");
MODULE_DESCRIPTION("w1 family 23 driver for DS2433, 4kb EEPROM");

#define W1_EEPROM_SIZE		512
#define W1_PAGE_SIZE		32
#define W1_PAGE_BITS		5
#define W1_PAGE_MASK		0x1F

#define W1_F23_READ_EEPROM	0xF0
#define W1_F23_WRITE_SCRATCH	0x0F
#define W1_F23_READ_SCRATCH	0xAA
#define W1_F23_COPY_SCRATCH	0x55

/**
 * Check the file size bounds and adjusts count as needed.
 * This may not be needed if the sysfs layer checks bounds.
 */
static inline size_t w1_f23_fix_count(loff_t off, size_t count, size_t size)
{
	if (off > size)
		return 0;

	if ((off + count) > size)
		return (size - off);

	return count;
}

static ssize_t w1_f23_read_bin(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u8 wrbuf[3];

	if ((count = w1_f23_fix_count(off, count, W1_EEPROM_SIZE)) == 0)
		return 0;

	atomic_inc(&sl->refcnt);
	if (down_interruptible(&sl->master->mutex)) {
		count = 0;
		goto out_dec;
	}

	/* read directly from the EEPROM */
	if (w1_reset_select_slave(sl)) {
		count = -EIO;
		goto out_up;
	}

	wrbuf[0] = W1_F23_READ_EEPROM;
	wrbuf[1] = off & 0xff;
	wrbuf[2] = off >> 8;
	w1_write_block(sl->master, wrbuf, 3);
	w1_read_block(sl->master, buf, count);

out_up:
	up(&sl->master->mutex);
out_dec:
	atomic_dec(&sl->refcnt);

	return count;
}

/**
 * Writes to the scratchpad and reads it back for verification.
 * The master must be locked.
 *
 * @param sl	The slave structure
 * @param addr	Address for the write
 * @param len   length must be <= (W1_PAGE_SIZE - (addr & W1_PAGE_MASK))
 * @param data	The data to write
 * @return	0=Success -1=failure
 */
static int w1_f23_write(struct w1_slave *sl, int addr, int len, const u8 *data)
{
	u8 wrbuf[4];
	u8 rdbuf[W1_PAGE_SIZE + 3];
	u8 es = (addr + len - 1) & 0x1f;

	/* Write the data to the scratchpad */
	if (w1_reset_select_slave(sl))
		return -1;

	wrbuf[0] = W1_F23_WRITE_SCRATCH;
	wrbuf[1] = addr & 0xff;
	wrbuf[2] = addr >> 8;

	w1_write_block(sl->master, wrbuf, 3);
	w1_write_block(sl->master, data, len);

	/* Read the scratchpad and verify */
	if (w1_reset_select_slave(sl))
		return -1;

	w1_write_8(sl->master, W1_F23_READ_SCRATCH);
	w1_read_block(sl->master, rdbuf, len + 3);

	/* Compare what was read against the data written */
	if ((rdbuf[0] != wrbuf[1]) || (rdbuf[1] != wrbuf[2]) ||
	    (rdbuf[2] != es) || (memcmp(data, &rdbuf[3], len) != 0))
		return -1;

	/* Copy the scratchpad to EEPROM */
	if (w1_reset_select_slave(sl))
		return -1;

	wrbuf[0] = W1_F23_COPY_SCRATCH;
	wrbuf[3] = es;
	w1_write_block(sl->master, wrbuf, 4);

	/* Sleep for 5 ms to wait for the write to complete */
	msleep(5);

	/* Reset the bus to wake up the EEPROM (this may not be needed) */
	w1_reset_bus(sl->master);

	return 0;
}

static ssize_t w1_f23_write_bin(struct kobject *kobj, char *buf, loff_t off,
				size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int addr, len, idx;

	if ((count = w1_f23_fix_count(off, count, W1_EEPROM_SIZE)) == 0)
		return 0;

	atomic_inc(&sl->refcnt);
	if (down_interruptible(&sl->master->mutex)) {
		count = 0;
		goto out_dec;
	}

	/* Can only write data to one page at a time */
	idx = 0;
	while (idx < count) {
		addr = off + idx;
		len = W1_PAGE_SIZE - (addr & W1_PAGE_MASK);
		if (len > (count - idx))
			len = count - idx;

		if (w1_f23_write(sl, addr, len, &buf[idx]) < 0) {
			count = -EIO;
			goto out_up;
		}
		idx += len;
	}

out_up:
	up(&sl->master->mutex);
out_dec:
	atomic_dec(&sl->refcnt);

	return count;
}

static struct bin_attribute w1_f23_bin_attr = {
	.attr = {
		.name = "eeprom",
		.mode = S_IRUGO | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = W1_EEPROM_SIZE,
	.read = w1_f23_read_bin,
	.write = w1_f23_write_bin,
};

static int w1_f23_add_slave(struct w1_slave *sl)
{
	return sysfs_create_bin_file(&sl->dev.kobj, &w1_f23_bin_attr);
}

static void w1_f23_remove_slave(struct w1_slave *sl)
{
	sysfs_remove_bin_file(&sl->dev.kobj, &w1_f23_bin_attr);
}

static struct w1_family_ops w1_f23_fops = {
	.add_slave      = w1_f23_add_slave,
	.remove_slave   = w1_f23_remove_slave,
};

static struct w1_family w1_family_23 = {
	.fid = W1_EEPROM_DS2433,
	.fops = &w1_f23_fops,
};

static int __init w1_f23_init(void)
{
	return w1_register_family(&w1_family_23);
}

static void __exit w1_f23_fini(void)
{
	w1_unregister_family(&w1_family_23);
}

module_init(w1_f23_init);
module_exit(w1_f23_fini);
