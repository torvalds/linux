/*
 * OF helpers for the I2C API
 *
 * Copyright (c) 2008 Jochen Friedrich <jochen@scram.de>
 *
 * Based on a previous patch from Jon Smirl <jonsmirl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/module.h>

struct i2c_driver_device {
	char    *of_device;
	char    *i2c_type;
};

static struct i2c_driver_device i2c_devices[] = {
	{ "dallas,ds1374", "rtc-ds1374" },
};

static int of_find_i2c_driver(struct device_node *node,
			      struct i2c_board_info *info)
{
	int i, cplen;
	const char *compatible;
	const char *p;

	/* 1. search for exception list entry */
	for (i = 0; i < ARRAY_SIZE(i2c_devices); i++) {
		if (!of_device_is_compatible(node, i2c_devices[i].of_device))
			continue;
		if (strlcpy(info->type, i2c_devices[i].i2c_type,
			    I2C_NAME_SIZE) >= I2C_NAME_SIZE)
			return -ENOMEM;

		return 0;
	}

	compatible = of_get_property(node, "compatible", &cplen);
	if (!compatible)
		return -ENODEV;

	/* 2. search for linux,<i2c-type> entry */
	p = compatible;
	while (cplen > 0) {
		if (!strncmp(p, "linux,", 6)) {
			p += 6;
			if (strlcpy(info->type, p,
				    I2C_NAME_SIZE) >= I2C_NAME_SIZE)
				return -ENOMEM;
			return 0;
		}

		i = strlen(p) + 1;
		p += i;
		cplen -= i;
	}

	/* 3. take fist compatible entry and strip manufacturer */
	p = strchr(compatible, ',');
	if (!p)
		return -ENODEV;
	p++;
	if (strlcpy(info->type, p, I2C_NAME_SIZE) >= I2C_NAME_SIZE)
		return -ENOMEM;
	return 0;
}

void of_register_i2c_devices(struct i2c_adapter *adap,
			     struct device_node *adap_node)
{
	void *result;
	struct device_node *node;

	for_each_child_of_node(adap_node, node) {
		struct i2c_board_info info = {};
		const u32 *addr;
		int len;

		addr = of_get_property(node, "reg", &len);
		if (!addr || len < sizeof(int) || *addr > (1 << 10) - 1) {
			printk(KERN_ERR
			       "of-i2c: invalid i2c device entry\n");
			continue;
		}

		info.irq = irq_of_parse_and_map(node, 0);
		if (info.irq == NO_IRQ)
			info.irq = -1;

		if (of_find_i2c_driver(node, &info) < 0) {
			irq_dispose_mapping(info.irq);
			continue;
		}

		info.addr = *addr;

		request_module(info.type);

		result = i2c_new_device(adap, &info);
		if (result == NULL) {
			printk(KERN_ERR
			       "of-i2c: Failed to load driver for %s\n",
			       info.type);
			irq_dispose_mapping(info.irq);
			continue;
		}
	}
}
EXPORT_SYMBOL(of_register_i2c_devices);

MODULE_LICENSE("GPL");
