/*
 * OF helpers for the MDIO (Ethernet PHY) API
 *
 * Copyright (c) 2009 Secret Lab Technologies, Ltd.
 *
 * This file is released under the GPLv2
 *
 * This file provides helper functions for extracting PHY device information
 * out of the OpenFirmware device tree and using it to populate an mii_bus.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/err.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/module.h>

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_LICENSE("GPL");

/**
 * of_mdiobus_register - Register mii_bus and create PHYs from the device tree
 * @mdio: pointer to mii_bus structure
 * @np: pointer to device_node of MDIO bus.
 *
 * This function registers the mii_bus structure and registers a phy_device
 * for each child node of @np.
 */
int of_mdiobus_register(struct mii_bus *mdio, struct device_node *np)
{
	struct phy_device *phy;
	struct device_node *child;
	int rc, i;

	/* Mask out all PHYs from auto probing.  Instead the PHYs listed in
	 * the device tree are populated after the bus has been registered */
	mdio->phy_mask = ~0;

	/* Clear all the IRQ properties */
	if (mdio->irq)
		for (i=0; i<PHY_MAX_ADDR; i++)
			mdio->irq[i] = PHY_POLL;

	/* Register the MDIO bus */
	rc = mdiobus_register(mdio);
	if (rc)
		return rc;

	/* Loop over the child nodes and register a phy_device for each one */
	for_each_child_of_node(np, child) {
		const u32 *addr;
		int len;

		/* A PHY must have a reg property in the range [0-31] */
		addr = of_get_property(child, "reg", &len);
		if (!addr || len < sizeof(*addr) || *addr >= 32 || *addr < 0) {
			dev_err(&mdio->dev, "%s has invalid PHY address\n",
				child->full_name);
			continue;
		}

		if (mdio->irq) {
			mdio->irq[*addr] = irq_of_parse_and_map(child, 0);
			if (!mdio->irq[*addr])
				mdio->irq[*addr] = PHY_POLL;
		}

		phy = get_phy_device(mdio, *addr);
		if (!phy) {
			dev_err(&mdio->dev, "error probing PHY at address %i\n",
				*addr);
			continue;
		}
		phy_scan_fixups(phy);

		/* Associate the OF node with the device structure so it
		 * can be looked up later */
		of_node_get(child);
		dev_archdata_set_node(&phy->dev.archdata, child);

		/* All data is now stored in the phy struct; register it */
		rc = phy_device_register(phy);
		if (rc) {
			phy_device_free(phy);
			of_node_put(child);
			continue;
		}

		dev_dbg(&mdio->dev, "registered phy %s at address %i\n",
			child->name, *addr);
	}

	return 0;
}
EXPORT_SYMBOL(of_mdiobus_register);

/* Helper function for of_phy_find_device */
static int of_phy_match(struct device *dev, void *phy_np)
{
	return dev_archdata_get_node(&dev->archdata) == phy_np;
}

/**
 * of_phy_find_device - Give a PHY node, find the phy_device
 * @phy_np: Pointer to the phy's device tree node
 *
 * Returns a pointer to the phy_device.
 */
struct phy_device *of_phy_find_device(struct device_node *phy_np)
{
	struct device *d;
	if (!phy_np)
		return NULL;

	d = bus_find_device(&mdio_bus_type, NULL, phy_np, of_phy_match);
	return d ? to_phy_device(d) : NULL;
}
EXPORT_SYMBOL(of_phy_find_device);

/**
 * of_phy_connect - Connect to the phy described in the device tree
 * @dev: pointer to net_device claiming the phy
 * @phy_np: Pointer to device tree node for the PHY
 * @hndlr: Link state callback for the network device
 * @iface: PHY data interface type
 *
 * Returns a pointer to the phy_device if successfull.  NULL otherwise
 */
struct phy_device *of_phy_connect(struct net_device *dev,
				  struct device_node *phy_np,
				  void (*hndlr)(struct net_device *), u32 flags,
				  phy_interface_t iface)
{
	struct phy_device *phy = of_phy_find_device(phy_np);

	if (!phy)
		return NULL;

	return phy_connect_direct(dev, phy, hndlr, flags, iface) ? NULL : phy;
}
EXPORT_SYMBOL(of_phy_connect);

/**
 * of_phy_connect_fixed_link - Parse fixed-link property and return a dummy phy
 * @dev: pointer to net_device claiming the phy
 * @hndlr: Link state callback for the network device
 * @iface: PHY data interface type
 *
 * This function is a temporary stop-gap and will be removed soon.  It is
 * only to support the fs_enet, ucc_geth and gianfar Ethernet drivers.  Do
 * not call this function from new drivers.
 */
struct phy_device *of_phy_connect_fixed_link(struct net_device *dev,
					     void (*hndlr)(struct net_device *),
					     phy_interface_t iface)
{
	struct device_node *net_np;
	char bus_id[MII_BUS_ID_SIZE + 3];
	struct phy_device *phy;
	const u32 *phy_id;
	int sz;

	if (!dev->dev.parent)
		return NULL;

	net_np = dev_archdata_get_node(&dev->dev.parent->archdata);
	if (!net_np)
		return NULL;

	phy_id = of_get_property(net_np, "fixed-link", &sz);
	if (!phy_id || sz < sizeof(*phy_id))
		return NULL;

	sprintf(bus_id, PHY_ID_FMT, "0", phy_id[0]);

	phy = phy_connect(dev, bus_id, hndlr, 0, iface);
	return IS_ERR(phy) ? NULL : phy;
}
EXPORT_SYMBOL(of_phy_connect_fixed_link);
