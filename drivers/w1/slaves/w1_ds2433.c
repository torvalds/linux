// SPDX-License-Identifier: GPL-2.0-only
/*
 *	w1_ds2433.c - w1 family 23 (DS2433) driver
 *
 * Copyright (c) 2005 Ben Gardner <bgardner@wabtec.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#ifdef CONFIG_W1_SLAVE_DS2433_CRC
#include <linux/crc16.h>

#define CRC16_INIT		0
#define CRC16_VALID		0xb001

#endif

#include <linux/w1.h>

#define W1_EEPROM_DS2433	0x23

#define W1_EEPROM_SIZE		512
#define W1_PAGE_COUNT		16
#define W1_PAGE_SIZE		32
#define W1_PAGE_BITS		5
#define W1_PAGE_MASK		0x1F

#define W1_F23_TIME		300

#define W1_F23_READ_EEPROM	0xF0
#define W1_F23_WRITE_SCRATCH	0x0F
#define W1_F23_READ_SCRATCH	0xAA
#define W1_F23_COPY_SCRATCH	0x55

struct w1_f23_data {
	u8	memory[W1_EEPROM_SIZE];
	u32	validcrc;
};

/**
 * Check the file size bounds and adjusts count as needed.
 * This would not be needed if the file size didn't reset to 0 after a write.
 */
static inline size_t w1_f23_fix_count(loff_t off, size_t count, size_t size)
{
	if (off > size)
		return 0;

	if ((off + count) > size)
		return (size - off);

	return count;
}

#ifdef CONFIG_W1_SLAVE_DS2433_CRC
static int w1_f23_refresh_block(struct w1_slave *sl, struct w1_f23_data *data,
				int block)
{
	u8	wrbuf[3];
	int	off = block * W1_PAGE_SIZE;

	if (data->validcrc & (1 << block))
		return 0;

	if (w1_reset_select_slave(sl)) {
		data->validcrc = 0;
		return -EIO;
	}

	wrbuf[0] = W1_F23_READ_EEPROM;
	wrbuf[1] = off & 0xff;
	wrbuf[2] = off >> 8;
	w1_write_block(sl->master, wrbuf, 3);
	w1_read_block(sl->master, &data->memory[off], W1_PAGE_SIZE);

	/* cache the block if the CRC is valid */
	if (crc16(CRC16_INIT, &data->memory[off], W1_PAGE_SIZE) == CRC16_VALID)
		data->validcrc |= (1 << block);

	return 0;
}
#endif	/* CONFIG_W1_SLAVE_DS2433_CRC */

static ssize_t eeprom_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr, char *buf,
			   loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
#ifdef CONFIG_W1_SLAVE_DS2433_CRC
	struct w1_f23_data *data = sl->family_data;
	int i, min_page, max_page;
#else
	u8 wrbuf[3];
#endif

	if ((count = w1_f23_fix_count(off, count, W1_EEPROM_SIZE)) == 0)
		return 0;

	mutex_lock(&sl->master->bus_mutex);

#ifdef CONFIG_W1_SLAVE_DS2433_CRC

	min_page = (off >> W1_PAGE_BITS);
	max_page = (off + count - 1) >> W1_PAGE_BITS;
	for (i = min_page; i <= max_page; i++) {
		if (w1_f23_refresh_block(sl, data, i)) {
			count = -EIO;
			goto out_up;
		}
	}
	memcpy(buf, &data->memory[off], count);

#else 	/* CONFIG_W1_SLAVE_DS2433_CRC */

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

#endif	/* CONFIG_W1_SLAVE_DS2433_CRC */

out_up:
	mutex_unlock(&sl->master->bus_mutex);

	return count;
}

/**
 * Writes to the scratchpad and reads it back for verification.
 * Then copies the scratchpad to EEPROM.
 * The data must be on one page.
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
#ifdef CONFIG_W1_SLAVE_DS2433_CRC
	struct w1_f23_data *f23 = sl->family_data;
#endif
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
#ifdef CONFIG_W1_SLAVE_DS2433_CRC
	f23->validcrc &= ~(1 << (addr >> W1_PAGE_BITS));
#endif
	return 0;
}

static ssize_t eeprom_write(struct file *filp, struct kobject *kobj,
			    struct bin_attribute *bin_attr, char *buf,
			    loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int addr, len, idx;

	if ((count = w1_f23_fix_count(off, count, W1_EEPROM_SIZE)) == 0)
		return 0;

#ifdef CONFIG_W1_SLAVE_DS2433_CRC
	/* can only write full blocks in cached mode */
	if ((off & W1_PAGE_MASK) || (count & W1_PAGE_MASK)) {
		dev_err(&sl->dev, "invalid offset/count off=%d cnt=%zd\n",
			(int)off, count);
		return -EINVAL;
	}

	/* make sure the block CRCs are valid */
	for (idx = 0; idx < count; idx += W1_PAGE_SIZE) {
		if (crc16(CRC16_INIT, &buf[idx], W1_PAGE_SIZE) != CRC16_VALID) {
			dev_err(&sl->dev, "bad CRC at offset %d\n", (int)off);
			return -EINVAL;
		}
	}
#endif	/* CONFIG_W1_SLAVE_DS2433_CRC */

	mutex_lock(&sl->master->bus_mutex);

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
	mutex_unlock(&sl->master->bus_mutex);

	return count;
}

static BIN_ATTR_RW(eeprom, W1_EEPROM_SIZE);

static struct bin_attribute *w1_f23_bin_attributes[] = {
	&bin_attr_eeprom,
	NULL,
};

static const struct attribute_group w1_f23_group = {
	.bin_attrs = w1_f23_bin_attributes,
};

static const struct attribute_group *w1_f23_groups[] = {
	&w1_f23_group,
	NULL,
};

static int w1_f23_add_slave(struct w1_slave *sl)
{
#ifdef CONFIG_W1_SLAVE_DS2433_CRC
	struct w1_f23_data *data;

	data = kzalloc(sizeof(struct w1_f23_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	sl->family_data = data;

#endif	/* CONFIG_W1_SLAVE_DS2433_CRC */
	return 0;
}

static void w1_f23_remove_slave(struct w1_slave *sl)
{
#ifdef CONFIG_W1_SLAVE_DS2433_CRC
	kfree(sl->family_data);
	sl->family_data = NULL;
#endif	/* CONFIG_W1_SLAVE_DS2433_CRC */
}

static const struct w1_family_ops w1_f23_fops = {
	.add_slave      = w1_f23_add_slave,
	.remove_slave   = w1_f23_remove_slave,
	.groups		= w1_f23_groups,
};

static struct w1_family w1_family_23 = {
	.fid = W1_EEPROM_DS2433,
	.fops = &w1_f23_fops,
};
module_w1_family(w1_family_23);

MODULE_AUTHOR("Ben Gardner <bgardner@wabtec.com>");
MODULE_DESCRIPTION("w1 family 23 driver for DS2433, 4kb EEPROM");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_EEPROM_DS2433));
