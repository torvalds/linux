// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/property.h>

#include <asm/mpc5xxx.h>

/**
 * mpc5xxx_fwnode_get_bus_frequency - Find the bus frequency for a firmware node
 * @fwnode:	firmware node
 *
 * Returns bus frequency (IPS on MPC512x, IPB on MPC52xx),
 * or 0 if the bus frequency cannot be found.
 */
unsigned long mpc5xxx_fwnode_get_bus_frequency(struct fwnode_handle *fwnode)
{
	struct fwnode_handle *parent;
	u32 bus_freq;
	int ret;

	ret = fwnode_property_read_u32(fwnode, "bus-frequency", &bus_freq);
	if (!ret)
		return bus_freq;

	fwnode_for_each_parent_node(fwnode, parent) {
		ret = fwnode_property_read_u32(parent, "bus-frequency", &bus_freq);
		if (!ret) {
			fwnode_handle_put(parent);
			return bus_freq;
		}
	}

	return 0;
}
EXPORT_SYMBOL(mpc5xxx_fwnode_get_bus_frequency);
