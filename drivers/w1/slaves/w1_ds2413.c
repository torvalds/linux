// SPDX-License-Identifier: GPL-2.0-only
/*
 * w1_ds2413.c - w1 family 3a (DS2413) driver
 * based on w1_ds2408.c by Jean-Francois Dagenais <dagenaisj@sonatest.com>
 *
 * Copyright (c) 2013 Mariusz Bialonczyk <manio@skyboo.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <linux/w1.h>

#define W1_FAMILY_DS2413	0x3A

#define W1_F3A_RETRIES                     3
#define W1_F3A_FUNC_PIO_ACCESS_READ        0xF5
#define W1_F3A_FUNC_PIO_ACCESS_WRITE       0x5A
#define W1_F3A_SUCCESS_CONFIRM_BYTE        0xAA
#define W1_F3A_INVALID_PIO_STATE           0xFF

static ssize_t state_read(struct file *filp, struct kobject *kobj,
			  const struct bin_attribute *bin_attr, char *buf,
			  loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	unsigned int retries = W1_F3A_RETRIES;
	ssize_t bytes_read = -EIO;
	u8 state;

	dev_dbg(&sl->dev,
		"Reading %s kobj: %p, off: %0#10x, count: %zu, buff addr: %p",
		bin_attr->attr.name, kobj, (unsigned int)off, count, buf);

	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "mutex locked");

next:
	if (w1_reset_select_slave(sl))
		goto out;

	while (retries--) {
		w1_write_8(sl->master, W1_F3A_FUNC_PIO_ACCESS_READ);

		state = w1_read_8(sl->master);
		if ((state & 0x0F) == ((~state >> 4) & 0x0F)) {
			/* complement is correct */
			*buf = state;
			bytes_read = 1;
			goto out;
		} else if (state == W1_F3A_INVALID_PIO_STATE) {
			/* slave didn't respond, try to select it again */
			dev_warn(&sl->dev, "slave device did not respond to PIO_ACCESS_READ, " \
					    "reselecting, retries left: %d\n", retries);
			goto next;
		}

		if (w1_reset_resume_command(sl->master))
			goto out; /* unrecoverable error */

		dev_warn(&sl->dev, "PIO_ACCESS_READ error, retries left: %d\n", retries);
	}

out:
	mutex_unlock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "%s, mutex unlocked, retries: %d\n",
		(bytes_read > 0) ? "succeeded" : "error", retries);
	return bytes_read;
}

static const BIN_ATTR_RO(state, 1);

static ssize_t output_write(struct file *filp, struct kobject *kobj,
			    const struct bin_attribute *bin_attr, char *buf,
			    loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	u8 w1_buf[3];
	unsigned int retries = W1_F3A_RETRIES;
	ssize_t bytes_written = -EIO;

	if (count != 1 || off != 0)
		return -EFAULT;

	dev_dbg(&sl->dev, "locking mutex for write_output");
	mutex_lock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "mutex locked");

	if (w1_reset_select_slave(sl))
		goto out;

	/*
	 * according to the DS2413 datasheet the most significant 6 bits
	 * should be set to "1"s, so do it now
	 */
	*buf = *buf | 0xFC;

	while (retries--) {
		w1_buf[0] = W1_F3A_FUNC_PIO_ACCESS_WRITE;
		w1_buf[1] = *buf;
		w1_buf[2] = ~(*buf);
		w1_write_block(sl->master, w1_buf, 3);

		if (w1_read_8(sl->master) == W1_F3A_SUCCESS_CONFIRM_BYTE) {
			bytes_written = 1;
			goto out;
		}
		if (w1_reset_resume_command(sl->master))
			goto out; /* unrecoverable error */

		dev_warn(&sl->dev, "PIO_ACCESS_WRITE error, retries left: %d\n", retries);
	}

out:
	mutex_unlock(&sl->master->bus_mutex);
	dev_dbg(&sl->dev, "%s, mutex unlocked, retries: %d\n",
		(bytes_written > 0) ? "succeeded" : "error", retries);
	return bytes_written;
}

static const BIN_ATTR(output, 0664, NULL, output_write, 1);

static const struct bin_attribute *const w1_f3a_bin_attrs[] = {
	&bin_attr_state,
	&bin_attr_output,
	NULL,
};

static const struct attribute_group w1_f3a_group = {
	.bin_attrs = w1_f3a_bin_attrs,
};

static const struct attribute_group *w1_f3a_groups[] = {
	&w1_f3a_group,
	NULL,
};

static const struct w1_family_ops w1_f3a_fops = {
	.groups		= w1_f3a_groups,
};

static struct w1_family w1_family_3a = {
	.fid = W1_FAMILY_DS2413,
	.fops = &w1_f3a_fops,
};
module_w1_family(w1_family_3a);

MODULE_AUTHOR("Mariusz Bialonczyk <manio@skyboo.net>");
MODULE_DESCRIPTION("w1 family 3a driver for DS2413 2 Pin IO");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS2413));
