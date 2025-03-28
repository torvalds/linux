// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (c) Copyright 2006 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/of_address.h>
#include <asm/dcr.h>

unsigned int dcr_resource_start(const struct device_node *np,
				unsigned int index)
{
	unsigned int ds;
	const u32 *dr = of_get_property(np, "dcr-reg", &ds);

	if (dr == NULL || ds & 1 || index >= (ds / 8))
		return 0;

	return dr[index * 2];
}
EXPORT_SYMBOL_GPL(dcr_resource_start);

unsigned int dcr_resource_len(const struct device_node *np, unsigned int index)
{
	unsigned int ds;
	const u32 *dr = of_get_property(np, "dcr-reg", &ds);

	if (dr == NULL || ds & 1 || index >= (ds / 8))
		return 0;

	return dr[index * 2 + 1];
}
EXPORT_SYMBOL_GPL(dcr_resource_len);

DEFINE_SPINLOCK(dcr_ind_lock);
EXPORT_SYMBOL_GPL(dcr_ind_lock);
