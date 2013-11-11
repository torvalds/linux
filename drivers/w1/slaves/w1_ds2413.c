/*
 * w1_ds2413.c - w1 family 3a (DS2413) driver
 * based on w1_ds2408.c by Jean-Francois Dagenais <dagenaisj@sonatest.com>
 *
 * Copyright (c) 2013 Mariusz Bialonczyk <manio@skyboo.net>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "../w1.h"
#include "../w1_int.h"
#include "../w1_family.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mariusz Bialonczyk <manio@skyboo.net>");
MODULE_DESCRIPTION("w1 family 3a driver for DS2413 2 Pin IO");

#define W1_F3A_RETRIES                     3
#define W1_F3A_FUNC_PIO_ACCESS_READ        0xF5
#define W1_F3A_FUNC_PIO_ACCESS_WRITE       0x5A
#define W1_F3A_SUCCESS_CONFIRM_BYTE        0xAA

static ssize_t w1_f3a_read_state(
	struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr,
	char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	dev_dbg(&sl->dev,
		"Reading %s kobj: %p, off: %0#10x, count: %zu, buff addr: %p",
		bin_attr->attr.name, kobj, (unsigned int)off, count, buf);

	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "mutex locked");

	if (w1_reset_select_slave(sl)) {
		mutex_unlock(&sl->master->bus_mutex);
		return -EIO;
	}

	w1_write_8(sl->master, W1_F3A_FUNC_PIO_ACCESS_READ);
	*buf = w1_read_8(sl->master);

	mutex_unlock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "mutex unlocked");

	/* check for correct complement */
	if ((*buf & 0x0F) != ((~*buf >> 4) & 0x0F))
		return -EIO;
	else
		return 1;
}

static ssize_t w1_f3a_write_output(
	struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr,
	char *buf, loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u8 w1_buf[3];
	unsigned int retries = W1_F3A_RETRIES;

	if (count != 1 || off != 0)
		return -EFAULT;

	dev_dbg(&sl->dev, "locking mutex for write_output");
	mutex_lock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "mutex locked");

	if (w1_reset_select_slave(sl))
		goto error;

	/* according to the DS2413 datasheet the most significant 6 bits
	   should be set to "1"s, so do it now */
	*buf = *buf | 0xFC;

	while (retries--) {
		w1_buf[0] = W1_F3A_FUNC_PIO_ACCESS_WRITE;
		w1_buf[1] = *buf;
		w1_buf[2] = ~(*buf);
		w1_write_block(sl->master, w1_buf, 3);

		if (w1_read_8(sl->master) == W1_F3A_SUCCESS_CONFIRM_BYTE) {
			mutex_unlock(&sl->master->bus_mutex);
			dev_dbg(&sl->dev, "mutex unlocked, retries:%d", retries);
			return 1;
		}
		if (w1_reset_resume_command(sl->master))
			goto error;
	}

error:
	mutex_unlock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "mutex unlocked in error, retries:%d", retries);
	return -EIO;
}

#define NB_SYSFS_BIN_FILES 2
static struct bin_attribute w1_f3a_sysfs_bin_files[NB_SYSFS_BIN_FILES] = {
	{
		.attr = {
			.name = "state",
			.mode = S_IRUGO,
		},
		.size = 1,
		.read = w1_f3a_read_state,
	},
	{
		.attr = {
			.name = "output",
			.mode = S_IRUGO | S_IWUSR | S_IWGRP,
		},
		.size = 1,
		.write = w1_f3a_write_output,
	}
};

static int w1_f3a_add_slave(struct w1_slave *sl)
{
	int err = 0;
	int i;

	for (i = 0; i < NB_SYSFS_BIN_FILES && !err; ++i)
		err = sysfs_create_bin_file(
			&sl->dev.kobj,
			&(w1_f3a_sysfs_bin_files[i]));
	if (err)
		while (--i >= 0)
			sysfs_remove_bin_file(&sl->dev.kobj,
				&(w1_f3a_sysfs_bin_files[i]));
	return err;
}

static void w1_f3a_remove_slave(struct w1_slave *sl)
{
	int i;
	for (i = NB_SYSFS_BIN_FILES - 1; i >= 0; --i)
		sysfs_remove_bin_file(&sl->dev.kobj,
			&(w1_f3a_sysfs_bin_files[i]));
}

static struct w1_family_ops w1_f3a_fops = {
	.add_slave      = w1_f3a_add_slave,
	.remove_slave   = w1_f3a_remove_slave,
};

static struct w1_family w1_family_3a = {
	.fid = W1_FAMILY_DS2413,
	.fops = &w1_f3a_fops,
};

static int __init w1_f3a_init(void)
{
	return w1_register_family(&w1_family_3a);
}

static void __exit w1_f3a_exit(void)
{
	w1_unregister_family(&w1_family_3a);
}

module_init(w1_f3a_init);
module_exit(w1_f3a_exit);
