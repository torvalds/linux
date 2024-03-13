// SPDX-License-Identifier: GPL-2.0+
/*
 * Marvell Armada AP and CP110 helper
 *
 * Copyright (C) 2018 Marvell
 *
 * Gregory Clement <gregory.clement@bootlin.com>
 *
 */

#include "armada_ap_cp_helper.h"
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>

char *ap_cp_unique_name(struct device *dev, struct device_node *np,
			const char *name)
{
	const __be32 *reg;
	u64 addr;

	/* Do not create a name if there is no clock */
	if (!name)
		return NULL;

	reg = of_get_property(np, "reg", NULL);
	addr = of_translate_address(np, reg);
	return devm_kasprintf(dev, GFP_KERNEL, "%llx-%s",
			      (unsigned long long)addr, name);
}
