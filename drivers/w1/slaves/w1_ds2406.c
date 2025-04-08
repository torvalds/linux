// SPDX-License-Identifier: GPL-2.0-only
/*
 * w1_ds2406.c - w1 family 12 (DS2406) driver
 * based on w1_ds2413.c by Mariusz Bialonczyk <manio@skyboo.net>
 *
 * Copyright (c) 2014 Scott Alfter <scott@alfter.us>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/crc16.h>

#include <linux/w1.h>

#define W1_FAMILY_DS2406	0x12

#define W1_F12_FUNC_READ_STATUS		   0xAA
#define W1_F12_FUNC_WRITE_STATUS	   0x55

static ssize_t w1_f12_read_state(
	struct file *filp, struct kobject *kobj,
	const struct bin_attribute *bin_attr,
	char *buf, loff_t off, size_t count)
{
	u8 w1_buf[6] = {W1_F12_FUNC_READ_STATUS, 7, 0, 0, 0, 0};
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u16 crc = 0;
	int i;
	ssize_t rtnval = 1;

	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_reset_select_slave(sl)) {
		mutex_unlock(&sl->master->bus_mutex);
		return -EIO;
	}

	w1_write_block(sl->master, w1_buf, 3);
	w1_read_block(sl->master, w1_buf+3, 3);
	for (i = 0; i < 6; i++)
		crc = crc16_byte(crc, w1_buf[i]);
	if (crc == 0xb001) /* good read? */
		*buf = ((w1_buf[3]>>5)&3)|0x30;
	else
		rtnval = -EIO;

	mutex_unlock(&sl->master->bus_mutex);

	return rtnval;
}

static ssize_t w1_f12_write_output(
	struct file *filp, struct kobject *kobj,
	const struct bin_attribute *bin_attr,
	char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u8 w1_buf[6] = {W1_F12_FUNC_WRITE_STATUS, 7, 0, 0, 0, 0};
	u16 crc = 0;
	int i;
	ssize_t rtnval = 1;

	if (count != 1 || off != 0)
		return -EFAULT;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_reset_select_slave(sl)) {
		mutex_unlock(&sl->master->bus_mutex);
		return -EIO;
	}

	w1_buf[3] = (((*buf)&3)<<5)|0x1F;
	w1_write_block(sl->master, w1_buf, 4);
	w1_read_block(sl->master, w1_buf+4, 2);
	for (i = 0; i < 6; i++)
		crc = crc16_byte(crc, w1_buf[i]);
	if (crc == 0xb001) /* good read? */
		w1_write_8(sl->master, 0xFF);
	else
		rtnval = -EIO;

	mutex_unlock(&sl->master->bus_mutex);
	return rtnval;
}

#define NB_SYSFS_BIN_FILES 2
static const struct bin_attribute w1_f12_sysfs_bin_files[NB_SYSFS_BIN_FILES] = {
	{
		.attr = {
			.name = "state",
			.mode = 0444,
		},
		.size = 1,
		.read_new = w1_f12_read_state,
	},
	{
		.attr = {
			.name = "output",
			.mode = 0664,
		},
		.size = 1,
		.write_new = w1_f12_write_output,
	}
};

static int w1_f12_add_slave(struct w1_slave *sl)
{
	int err = 0;
	int i;

	for (i = 0; i < NB_SYSFS_BIN_FILES && !err; ++i)
		err = sysfs_create_bin_file(
			&sl->dev.kobj,
			&(w1_f12_sysfs_bin_files[i]));
	if (err)
		while (--i >= 0)
			sysfs_remove_bin_file(&sl->dev.kobj,
				&(w1_f12_sysfs_bin_files[i]));
	return err;
}

static void w1_f12_remove_slave(struct w1_slave *sl)
{
	int i;

	for (i = NB_SYSFS_BIN_FILES - 1; i >= 0; --i)
		sysfs_remove_bin_file(&sl->dev.kobj,
			&(w1_f12_sysfs_bin_files[i]));
}

static const struct w1_family_ops w1_f12_fops = {
	.add_slave      = w1_f12_add_slave,
	.remove_slave   = w1_f12_remove_slave,
};

static struct w1_family w1_family_12 = {
	.fid = W1_FAMILY_DS2406,
	.fops = &w1_f12_fops,
};
module_w1_family(w1_family_12);

MODULE_AUTHOR("Scott Alfter <scott@alfter.us>");
MODULE_DESCRIPTION("w1 family 12 driver for DS2406 2 Pin IO");
MODULE_LICENSE("GPL");
