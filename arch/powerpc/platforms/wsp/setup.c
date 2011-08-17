/*
 * Copyright 2010 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/of_platform.h>

#include "wsp.h"

/*
 * Find chip-id by walking up device tree looking for ibm,wsp-chip-id property.
 * Won't work for nodes that are not a descendant of a wsp node.
 */
int wsp_get_chip_id(struct device_node *dn)
{
	const u32 *p;
	int rc;

	/* Start looking at the specified node, not its parent */
	dn = of_node_get(dn);
	while (dn && !(p = of_get_property(dn, "ibm,wsp-chip-id", NULL)))
		dn = of_get_next_parent(dn);

	if (!dn)
		return -1;

	rc = *p;
	of_node_put(dn);

	return rc;
}
