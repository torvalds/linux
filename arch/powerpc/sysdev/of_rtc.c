/*
 * Instantiate mmio-mapped RTC chips based on device tree information
 *
 * Copyright 2007 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

static __initdata struct {
	const char *compatible;
	char *plat_name;
} of_rtc_table[] = {
	{ "ds1743-nvram", "rtc-ds1742" },
};

void __init of_instantiate_rtc(void)
{
	struct device_node *node;
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(of_rtc_table); i++) {
		char *plat_name = of_rtc_table[i].plat_name;

		for_each_compatible_node(node, NULL,
					 of_rtc_table[i].compatible) {
			struct resource *res;

			res = kmalloc(sizeof(*res), GFP_KERNEL);
			if (!res) {
				printk(KERN_ERR "OF RTC: Out of memory "
				       "allocating resource structure for %pOF\n",
				       node);
				continue;
			}

			err = of_address_to_resource(node, 0, res);
			if (err) {
				printk(KERN_ERR "OF RTC: Error "
				       "translating resources for %pOF\n",
				       node);
				continue;
			}

			printk(KERN_INFO "OF_RTC: %pOF is a %s @ 0x%llx-0x%llx\n",
			       node, plat_name,
			       (unsigned long long)res->start,
			       (unsigned long long)res->end);
			platform_device_register_simple(plat_name, -1, res, 1);
		}
	}
}
