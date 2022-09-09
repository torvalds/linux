// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI helpers for the MDIO (Ethernet PHY) API
 *
 * This file provides helper functions for extracting PHY device information
 * out of the ACPI ASL and using it to populate an mii_bus.
 */

#include <linux/acpi.h>
#include <linux/acpi_mdio.h>
#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/fwnode_mdio.h>
#include <linux/module.h>
#include <linux/types.h>

MODULE_AUTHOR("Calvin Johnson <calvin.johnson@oss.nxp.com>");
MODULE_LICENSE("GPL");

/**
 * acpi_mdiobus_register - Register mii_bus and create PHYs from the ACPI ASL.
 * @mdio: pointer to mii_bus structure
 * @fwnode: pointer to fwnode of MDIO bus. This fwnode is expected to represent
 * an ACPI device object corresponding to the MDIO bus and its children are
 * expected to correspond to the PHY devices on that bus.
 *
 * This function registers the mii_bus structure and registers a phy_device
 * for each child node of @fwnode.
 */
int acpi_mdiobus_register(struct mii_bus *mdio, struct fwnode_handle *fwnode)
{
	struct fwnode_handle *child;
	u32 addr;
	int ret;

	/* Mask out all PHYs from auto probing. */
	mdio->phy_mask = GENMASK(31, 0);
	ret = mdiobus_register(mdio);
	if (ret)
		return ret;

	ACPI_COMPANION_SET(&mdio->dev, to_acpi_device_node(fwnode));

	/* Loop over the child nodes and register a phy_device for each PHY */
	fwnode_for_each_child_node(fwnode, child) {
		ret = acpi_get_local_address(ACPI_HANDLE_FWNODE(child), &addr);
		if (ret || addr >= PHY_MAX_ADDR)
			continue;

		ret = fwnode_mdiobus_register_phy(mdio, child, addr);
		if (ret == -ENODEV)
			dev_err(&mdio->dev,
				"MDIO device at address %d is missing.\n",
				addr);
	}
	return 0;
}
EXPORT_SYMBOL(acpi_mdiobus_register);
