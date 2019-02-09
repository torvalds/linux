/*
 *	w1_ds2405.c
 *
 * Copyright (c) 2017 Maciej S. Szmigiero <mail@maciej.szmigiero.name>
 * Based on w1_therm.c copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the therms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/w1.h>

#define W1_FAMILY_DS2405	0x05

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maciej S. Szmigiero <mail@maciej.szmigiero.name>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas DS2405 PIO.");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS2405));

static int w1_ds2405_select(struct w1_slave *sl, bool only_active)
{
	struct w1_master *dev = sl->master;

	u64 dev_addr = le64_to_cpu(*(u64 *)&sl->reg_num);
	unsigned int bit_ctr;

	if (w1_reset_bus(dev) != 0)
		return 0;

	/*
	 * We cannot use a normal Match ROM command
	 * since doing so would toggle PIO state
	 */
	w1_write_8(dev, only_active ? W1_ALARM_SEARCH : W1_SEARCH);

	for (bit_ctr = 0; bit_ctr < 64; bit_ctr++) {
		int bit2send = !!(dev_addr & BIT(bit_ctr));
		u8 ret;

		ret = w1_triplet(dev, bit2send);

		if ((ret & (BIT(0) | BIT(1))) ==
		    (BIT(0) | BIT(1))) /* no devices found */
			return 0;

		if (!!(ret & BIT(2)) != bit2send)
			/* wrong direction taken - no such device */
			return 0;
	}

	return 1;
}

static int w1_ds2405_read_pio(struct w1_slave *sl)
{
	if (w1_ds2405_select(sl, true))
		return 0; /* "active" means PIO is low */

	if (w1_ds2405_select(sl, false))
		return 1;

	return -ENODEV;
}

static ssize_t state_show(struct device *device,
			  struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct w1_master *dev = sl->master;

	int ret;
	ssize_t f_retval;
	u8 state;

	ret = mutex_lock_interruptible(&dev->bus_mutex);
	if (ret)
		return ret;

	if (!w1_ds2405_select(sl, false)) {
		f_retval = -ENODEV;
		goto out_unlock;
	}

	state = w1_read_8(dev);
	if (state != 0 &&
	    state != 0xff) {
		dev_err(device, "non-consistent state %x\n", state);
		f_retval = -EIO;
		goto out_unlock;
	}

	*buf = state ? '1' : '0';
	f_retval = 1;

out_unlock:
	w1_reset_bus(dev);
	mutex_unlock(&dev->bus_mutex);

	return f_retval;
}

static ssize_t output_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct w1_master *dev = sl->master;

	int ret;
	ssize_t f_retval;

	ret = mutex_lock_interruptible(&dev->bus_mutex);
	if (ret)
		return ret;

	ret = w1_ds2405_read_pio(sl);
	if (ret < 0) {
		f_retval = ret;
		goto out_unlock;
	}

	*buf = ret ? '1' : '0';
	f_retval = 1;

out_unlock:
	w1_reset_bus(dev);
	mutex_unlock(&dev->bus_mutex);

	return f_retval;
}

static ssize_t output_store(struct device *device,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct w1_master *dev = sl->master;

	int ret, current_pio;
	unsigned int val;
	ssize_t f_retval;

	if (count < 1)
		return -EINVAL;

	if (sscanf(buf, " %u%n", &val, &ret) < 1)
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	f_retval = ret;

	ret = mutex_lock_interruptible(&dev->bus_mutex);
	if (ret)
		return ret;

	current_pio = w1_ds2405_read_pio(sl);
	if (current_pio < 0) {
		f_retval = current_pio;
		goto out_unlock;
	}

	if (current_pio == val)
		goto out_unlock;

	if (w1_reset_bus(dev) != 0) {
		f_retval = -ENODEV;
		goto out_unlock;
	}

	/*
	 * can't use w1_reset_select_slave() here since it uses Skip ROM if
	 * there is only one device on bus
	 */
	do {
		u64 dev_addr = le64_to_cpu(*(u64 *)&sl->reg_num);
		u8 cmd[9];

		cmd[0] = W1_MATCH_ROM;
		memcpy(&cmd[1], &dev_addr, sizeof(dev_addr));

		w1_write_block(dev, cmd, sizeof(cmd));
	} while (0);

out_unlock:
	w1_reset_bus(dev);
	mutex_unlock(&dev->bus_mutex);

	return f_retval;
}

static DEVICE_ATTR_RO(state);
static DEVICE_ATTR_RW(output);

static struct attribute *w1_ds2405_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_output.attr,
	NULL
};

ATTRIBUTE_GROUPS(w1_ds2405);

static struct w1_family_ops w1_ds2405_fops = {
	.groups = w1_ds2405_groups
};

static struct w1_family w1_family_ds2405 = {
	.fid = W1_FAMILY_DS2405,
	.fops = &w1_ds2405_fops
};

module_w1_family(w1_family_ds2405);
