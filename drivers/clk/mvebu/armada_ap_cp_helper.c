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
	struct resource res;

	/* Do not create a name if there is no clock */
	if (!name)
		return NULL;

	of_address_to_resource(np, 0, &res);
	return devm_kasprintf(dev, GFP_KERNEL, "%llx-%s",
			      (unsigned long long)res.start, name);
}
