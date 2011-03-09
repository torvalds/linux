/*
 * Flash partitions described by the OF (or flattened) device tree
 *
 * Copyright © 2006 MontaVista Software Inc.
 * Author: Vitaly Wool <vwool@ru.mvista.com>
 *
 * Revised to handle newer style flash binding by:
 *   Copyright © 2007 David Gibson, IBM Corporation.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/mtd/partitions.h>

int __devinit of_mtd_parse_partitions(struct device *dev,
                                      struct device_node *node,
                                      struct mtd_partition **pparts)
{
	const char *partname;
	struct device_node *pp;
	int nr_parts, i;

	/* First count the subnodes */
	pp = NULL;
	nr_parts = 0;
	while ((pp = of_get_next_child(node, pp)))
		nr_parts++;

	if (nr_parts == 0)
		return 0;

	*pparts = kzalloc(nr_parts * sizeof(**pparts), GFP_KERNEL);
	if (!*pparts)
		return -ENOMEM;

	pp = NULL;
	i = 0;
	while ((pp = of_get_next_child(node, pp))) {
		const __be32 *reg;
		int len;

		reg = of_get_property(pp, "reg", &len);
		if (!reg) {
			nr_parts--;
			continue;
		}

		(*pparts)[i].offset = be32_to_cpu(reg[0]);
		(*pparts)[i].size = be32_to_cpu(reg[1]);

		partname = of_get_property(pp, "label", &len);
		if (!partname)
			partname = of_get_property(pp, "name", &len);
		(*pparts)[i].name = (char *)partname;

		if (of_get_property(pp, "read-only", &len))
			(*pparts)[i].mask_flags = MTD_WRITEABLE;

		i++;
	}

	if (!i) {
		of_node_put(pp);
		dev_err(dev, "No valid partition found on %s\n", node->full_name);
		kfree(*pparts);
		*pparts = NULL;
		return -EINVAL;
	}

	return nr_parts;
}
EXPORT_SYMBOL(of_mtd_parse_partitions);

MODULE_LICENSE("GPL");
