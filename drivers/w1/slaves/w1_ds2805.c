// SPDX-License-Identifier: GPL-2.0-only
/*
 * w1_ds2805 - w1 family 0d (DS28E05) driver
 *
 * Copyright (c) 2016 Andrew Worsley amworsley@gmail.com
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>

#include <linux/w1.h>

#define W1_EEPROM_DS2805       0x0D

#define W1_F0D_EEPROM_SIZE		128
#define W1_F0D_PAGE_BITS		3
#define W1_F0D_PAGE_SIZE		(1<<W1_F0D_PAGE_BITS)
#define W1_F0D_PAGE_MASK		0x0F

#define W1_F0D_SCRATCH_BITS  1
#define W1_F0D_SCRATCH_SIZE  (1<<W1_F0D_SCRATCH_BITS)
#define W1_F0D_SCRATCH_MASK  (W1_F0D_SCRATCH_SIZE-1)

#define W1_F0D_READ_EEPROM	0xF0
#define W1_F0D_WRITE_EEPROM	0x55
#define W1_F0D_RELEASE		0xFF

#define W1_F0D_CS_OK		0xAA /* Chip Status Ok */

#define W1_F0D_TPROG_MS		16

#define W1_F0D_READ_RETRIES		10
#define W1_F0D_READ_MAXLEN		W1_F0D_EEPROM_SIZE

/*
 * Check the file size bounds and adjusts count as needed.
 * This would not be needed if the file size didn't reset to 0 after a write.
 */
static inline size_t w1_f0d_fix_count(loff_t off, size_t count, size_t size)
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
 * is repeated W1_F0D_READ_RETRIES times.
 *
 * count must not exceed W1_F0D_READ_MAXLEN.
 */
static int w1_f0d_readblock(struct w1_slave *sl, int off, int count, char *buf)
{
	u8 wrbuf[3];
	u8 cmp[W1_F0D_READ_MAXLEN];
	int tries = W1_F0D_READ_RETRIES;

	do {
		wrbuf[0] = W1_F0D_READ_EEPROM;
		wrbuf[1] = off & 0x7f;
		wrbuf[2] = 0;

		if (w1_reset_select_slave(sl))
			return -1;

		w1_write_block(sl->master, wrbuf, sizeof(wrbuf));
		w1_read_block(sl->master, buf, count);

		if (w1_reset_select_slave(sl))
			return -1;

		w1_write_block(sl->master, wrbuf, sizeof(wrbuf));
		w1_read_block(sl->master, cmp, count);

		if (!memcmp(cmp, buf, count))
			return 0;
	} while (--tries);

	dev_err(&sl->dev, "proof reading failed %d times\n",
			W1_F0D_READ_RETRIES);

	return -1;
}

static ssize_t w1_f0d_read_bin(struct file *filp, struct kobject *kobj,
			       const struct bin_attribute *bin_attr,
			       char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int todo = count;

	count = w1_f0d_fix_count(off, count, W1_F0D_EEPROM_SIZE);
	if (count == 0)
		return 0;

	mutex_lock(&sl->master->mutex);

	/* read directly from the EEPROM in chunks of W1_F0D_READ_MAXLEN */
	while (todo > 0) {
		int block_read;

		if (todo >= W1_F0D_READ_MAXLEN)
			block_read = W1_F0D_READ_MAXLEN;
		else
			block_read = todo;

		if (w1_f0d_readblock(sl, off, block_read, buf) < 0) {
			count = -EIO;
			break;
		}

		todo -= W1_F0D_READ_MAXLEN;
		buf += W1_F0D_READ_MAXLEN;
		off += W1_F0D_READ_MAXLEN;
	}

	mutex_unlock(&sl->master->mutex);

	return count;
}

/*
 * Writes to the scratchpad and reads it back for verification.
 * Then copies the scratchpad to EEPROM.
 * The data must be aligned at W1_F0D_SCRATCH_SIZE bytes and
 * must be W1_F0D_SCRATCH_SIZE bytes long.
 * The master must be locked.
 *
 * @param sl	The slave structure
 * @param addr	Address for the write
 * @param len   length must be <= (W1_F0D_PAGE_SIZE - (addr & W1_F0D_PAGE_MASK))
 * @param data	The data to write
 * @return	0=Success -1=failure
 */
static int w1_f0d_write(struct w1_slave *sl, int addr, int len, const u8 *data)
{
	int tries = W1_F0D_READ_RETRIES;
	u8 wrbuf[3];
	u8 rdbuf[W1_F0D_SCRATCH_SIZE];
	u8 cs;

	if ((addr & 1) || (len != 2)) {
		dev_err(&sl->dev, "%s: bad addr/len -  addr=%#x len=%d\n",
		    __func__, addr, len);
		return -1;
	}

retry:

	/* Write the data to the scratchpad */
	if (w1_reset_select_slave(sl))
		return -1;

	wrbuf[0] = W1_F0D_WRITE_EEPROM;
	wrbuf[1] = addr & 0xff;
	wrbuf[2] = 0xff; /* ?? from Example */

	w1_write_block(sl->master, wrbuf, sizeof(wrbuf));
	w1_write_block(sl->master, data, len);

	w1_read_block(sl->master, rdbuf, sizeof(rdbuf));
	/* Compare what was read against the data written */
	if ((rdbuf[0] != data[0]) || (rdbuf[1] != data[1])) {

		if (--tries)
			goto retry;

		dev_err(&sl->dev,
			"could not write to eeprom, scratchpad compare failed %d times\n",
			W1_F0D_READ_RETRIES);
		pr_info("%s: rdbuf = %#x %#x data = %#x %#x\n",
		    __func__, rdbuf[0], rdbuf[1], data[0], data[1]);

		return -1;
	}

	/* Trigger write out to EEPROM */
	w1_write_8(sl->master, W1_F0D_RELEASE);

	/* Sleep for tprog ms to wait for the write to complete */
	msleep(W1_F0D_TPROG_MS);

	/* Check CS (Command Status) == 0xAA ? */
	cs = w1_read_8(sl->master);
	if (cs != W1_F0D_CS_OK) {
		dev_err(&sl->dev, "save to eeprom failed = CS=%#x\n", cs);
		return -1;
	}

	return 0;
}

static ssize_t w1_f0d_write_bin(struct file *filp, struct kobject *kobj,
				const struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int addr, len;
	int copy;

	count = w1_f0d_fix_count(off, count, W1_F0D_EEPROM_SIZE);
	if (count == 0)
		return 0;

	mutex_lock(&sl->master->mutex);

	/* Can only write data in blocks of the size of the scratchpad */
	addr = off;
	len = count;
	while (len > 0) {

		/* if len too short or addr not aligned */
		if (len < W1_F0D_SCRATCH_SIZE || addr & W1_F0D_SCRATCH_MASK) {
			char tmp[W1_F0D_SCRATCH_SIZE];

			/* read the block and update the parts to be written */
			if (w1_f0d_readblock(sl, addr & ~W1_F0D_SCRATCH_MASK,
					W1_F0D_SCRATCH_SIZE, tmp)) {
				count = -EIO;
				goto out_up;
			}

			/* copy at most to the boundary of the PAGE or len */
			copy = W1_F0D_SCRATCH_SIZE -
				(addr & W1_F0D_SCRATCH_MASK);

			if (copy > len)
				copy = len;

			memcpy(&tmp[addr & W1_F0D_SCRATCH_MASK], buf, copy);
			if (w1_f0d_write(sl, addr & ~W1_F0D_SCRATCH_MASK,
					W1_F0D_SCRATCH_SIZE, tmp) < 0) {
				count = -EIO;
				goto out_up;
			}
		} else {

			copy = W1_F0D_SCRATCH_SIZE;
			if (w1_f0d_write(sl, addr, copy, buf) < 0) {
				count = -EIO;
				goto out_up;
			}
		}
		buf += copy;
		addr += copy;
		len -= copy;
	}

out_up:
	mutex_unlock(&sl->master->mutex);

	return count;
}

static const struct bin_attribute w1_f0d_bin_attr = {
	.attr = {
		.name = "eeprom",
		.mode = 0644,
	},
	.size = W1_F0D_EEPROM_SIZE,
	.read_new = w1_f0d_read_bin,
	.write_new = w1_f0d_write_bin,
};

static int w1_f0d_add_slave(struct w1_slave *sl)
{
	return sysfs_create_bin_file(&sl->dev.kobj, &w1_f0d_bin_attr);
}

static void w1_f0d_remove_slave(struct w1_slave *sl)
{
	sysfs_remove_bin_file(&sl->dev.kobj, &w1_f0d_bin_attr);
}

static const struct w1_family_ops w1_f0d_fops = {
	.add_slave      = w1_f0d_add_slave,
	.remove_slave   = w1_f0d_remove_slave,
};

static struct w1_family w1_family_0d = {
	.fid = W1_EEPROM_DS2805,
	.fops = &w1_f0d_fops,
};

module_w1_family(w1_family_0d);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrew Worsley amworsley@gmail.com");
MODULE_DESCRIPTION("w1 family 0d driver for DS2805, 1kb EEPROM");
