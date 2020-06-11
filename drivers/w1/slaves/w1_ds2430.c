// SPDX-License-Identifier: GPL-2.0-only
/*
 * w1_ds2430.c - w1 family 14 (DS2430) driver
 **
 * Copyright (c) 2019 Angelo Dureghello <angelo.dureghello@timesys.com>
 *
 * Cloned and modified from ds2431
 * Copyright (c) 2008 Bernhard Weirich <bernhard.weirich@riedel.net>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>

#include <linux/w1.h>

#define W1_EEPROM_DS2430	0x14

#define W1_F14_EEPROM_SIZE	32
#define W1_F14_PAGE_COUNT	1
#define W1_F14_PAGE_BITS	5
#define W1_F14_PAGE_SIZE	(1 << W1_F14_PAGE_BITS)
#define W1_F14_PAGE_MASK	0x1F

#define W1_F14_SCRATCH_BITS	5
#define W1_F14_SCRATCH_SIZE	(1 << W1_F14_SCRATCH_BITS)
#define W1_F14_SCRATCH_MASK	(W1_F14_SCRATCH_SIZE-1)

#define W1_F14_READ_EEPROM	0xF0
#define W1_F14_WRITE_SCRATCH	0x0F
#define W1_F14_READ_SCRATCH	0xAA
#define W1_F14_COPY_SCRATCH	0x55
#define W1_F14_VALIDATION_KEY	0xa5

#define W1_F14_TPROG_MS		11
#define W1_F14_READ_RETRIES	10
#define W1_F14_READ_MAXLEN	W1_F14_SCRATCH_SIZE

/*
 * Check the file size bounds and adjusts count as needed.
 * This would not be needed if the file size didn't reset to 0 after a write.
 */
static inline size_t w1_f14_fix_count(loff_t off, size_t count, size_t size)
{
	if (off > size)
		return 0;

	if ((off + count) > size)
		return size - off;

	return count;
}

/*
 * Read a block from W1 ROM two times and compares the results.
 * If they are equal they are returned, otherwise the read
 * is repeated W1_F14_READ_RETRIES times.
 *
 * count must not exceed W1_F14_READ_MAXLEN.
 */
static int w1_f14_readblock(struct w1_slave *sl, int off, int count, char *buf)
{
	u8 wrbuf[2];
	u8 cmp[W1_F14_READ_MAXLEN];
	int tries = W1_F14_READ_RETRIES;

	do {
		wrbuf[0] = W1_F14_READ_EEPROM;
		wrbuf[1] = off & 0xff;

		if (w1_reset_select_slave(sl))
			return -1;

		w1_write_block(sl->master, wrbuf, 2);
		w1_read_block(sl->master, buf, count);

		if (w1_reset_select_slave(sl))
			return -1;

		w1_write_block(sl->master, wrbuf, 2);
		w1_read_block(sl->master, cmp, count);

		if (!memcmp(cmp, buf, count))
			return 0;
	} while (--tries);

	dev_err(&sl->dev, "proof reading failed %d times\n",
			W1_F14_READ_RETRIES);

	return -1;
}

static ssize_t eeprom_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr, char *buf,
			   loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int todo = count;

	count = w1_f14_fix_count(off, count, W1_F14_EEPROM_SIZE);
	if (count == 0)
		return 0;

	mutex_lock(&sl->master->bus_mutex);

	/* read directly from the EEPROM in chunks of W1_F14_READ_MAXLEN */
	while (todo > 0) {
		int block_read;

		if (todo >= W1_F14_READ_MAXLEN)
			block_read = W1_F14_READ_MAXLEN;
		else
			block_read = todo;

		if (w1_f14_readblock(sl, off, block_read, buf) < 0)
			count = -EIO;

		todo -= W1_F14_READ_MAXLEN;
		buf += W1_F14_READ_MAXLEN;
		off += W1_F14_READ_MAXLEN;
	}

	mutex_unlock(&sl->master->bus_mutex);

	return count;
}

/*
 * Writes to the scratchpad and reads it back for verification.
 * Then copies the scratchpad to EEPROM.
 * The data must be aligned at W1_F14_SCRATCH_SIZE bytes and
 * must be W1_F14_SCRATCH_SIZE bytes long.
 * The master must be locked.
 *
 * @param sl	The slave structure
 * @param addr	Address for the write
 * @param len   length must be <= (W1_F14_PAGE_SIZE - (addr & W1_F14_PAGE_MASK))
 * @param data	The data to write
 * @return	0=Success -1=failure
 */
static int w1_f14_write(struct w1_slave *sl, int addr, int len, const u8 *data)
{
	int tries = W1_F14_READ_RETRIES;
	u8 wrbuf[2];
	u8 rdbuf[W1_F14_SCRATCH_SIZE + 3];

retry:

	/* Write the data to the scratchpad */
	if (w1_reset_select_slave(sl))
		return -1;

	wrbuf[0] = W1_F14_WRITE_SCRATCH;
	wrbuf[1] = addr & 0xff;

	w1_write_block(sl->master, wrbuf, 2);
	w1_write_block(sl->master, data, len);

	/* Read the scratchpad and verify */
	if (w1_reset_select_slave(sl))
		return -1;

	w1_write_8(sl->master, W1_F14_READ_SCRATCH);
	w1_read_block(sl->master, rdbuf, len + 2);

	/*
	 * Compare what was read against the data written
	 * Note: on read scratchpad, device returns 2 bulk 0xff bytes,
	 * to be discarded.
	 */
	if ((memcmp(data, &rdbuf[2], len) != 0)) {

		if (--tries)
			goto retry;

		dev_err(&sl->dev,
			"could not write to eeprom, scratchpad compare failed %d times\n",
			W1_F14_READ_RETRIES);

		return -1;
	}

	/* Copy the scratchpad to EEPROM */
	if (w1_reset_select_slave(sl))
		return -1;

	wrbuf[0] = W1_F14_COPY_SCRATCH;
	wrbuf[1] = W1_F14_VALIDATION_KEY;
	w1_write_block(sl->master, wrbuf, 2);

	/* Sleep for tprog ms to wait for the write to complete */
	msleep(W1_F14_TPROG_MS);

	/* Reset the bus to wake up the EEPROM  */
	w1_reset_bus(sl->master);

	return 0;
}

static ssize_t eeprom_write(struct file *filp, struct kobject *kobj,
			    struct bin_attribute *bin_attr, char *buf,
			    loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int addr, len;
	int copy;

	count = w1_f14_fix_count(off, count, W1_F14_EEPROM_SIZE);
	if (count == 0)
		return 0;

	mutex_lock(&sl->master->bus_mutex);

	/* Can only write data in blocks of the size of the scratchpad */
	addr = off;
	len = count;
	while (len > 0) {

		/* if len too short or addr not aligned */
		if (len < W1_F14_SCRATCH_SIZE || addr & W1_F14_SCRATCH_MASK) {
			char tmp[W1_F14_SCRATCH_SIZE];

			/* read the block and update the parts to be written */
			if (w1_f14_readblock(sl, addr & ~W1_F14_SCRATCH_MASK,
					W1_F14_SCRATCH_SIZE, tmp)) {
				count = -EIO;
				goto out_up;
			}

			/* copy at most to the boundary of the PAGE or len */
			copy = W1_F14_SCRATCH_SIZE -
				(addr & W1_F14_SCRATCH_MASK);

			if (copy > len)
				copy = len;

			memcpy(&tmp[addr & W1_F14_SCRATCH_MASK], buf, copy);
			if (w1_f14_write(sl, addr & ~W1_F14_SCRATCH_MASK,
					W1_F14_SCRATCH_SIZE, tmp) < 0) {
				count = -EIO;
				goto out_up;
			}
		} else {

			copy = W1_F14_SCRATCH_SIZE;
			if (w1_f14_write(sl, addr, copy, buf) < 0) {
				count = -EIO;
				goto out_up;
			}
		}
		buf += copy;
		addr += copy;
		len -= copy;
	}

out_up:
	mutex_unlock(&sl->master->bus_mutex);

	return count;
}

static BIN_ATTR_RW(eeprom, W1_F14_EEPROM_SIZE);

static struct bin_attribute *w1_f14_bin_attrs[] = {
	&bin_attr_eeprom,
	NULL,
};

static const struct attribute_group w1_f14_group = {
	.bin_attrs = w1_f14_bin_attrs,
};

static const struct attribute_group *w1_f14_groups[] = {
	&w1_f14_group,
	NULL,
};

static struct w1_family_ops w1_f14_fops = {
	.groups	= w1_f14_groups,
};

static struct w1_family w1_family_14 = {
	.fid = W1_EEPROM_DS2430,
	.fops = &w1_f14_fops,
};
module_w1_family(w1_family_14);

MODULE_AUTHOR("Angelo Dureghello <angelo.dureghello@timesys.com>");
MODULE_DESCRIPTION("w1 family 14 driver for DS2430, 256b EEPROM");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_EEPROM_DS2430));
