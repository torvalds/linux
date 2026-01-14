// SPDX-License-Identifier: GPL-2.0+
/* Framework to drive Ethernet ports
 *
 * Copyright (c) 2024 Maxime Chevallier <maxime.chevallier@bootlin.com>
 */

#include <linux/linkmode.h>
#include <linux/of.h>
#include <linux/phy_port.h>

#include "phy-caps.h"

/**
 * phy_port_alloc() - Allocate a new phy_port
 *
 * Returns: a newly allocated struct phy_port, or NULL.
 */
struct phy_port *phy_port_alloc(void)
{
	struct phy_port *port;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return NULL;

	linkmode_zero(port->supported);
	INIT_LIST_HEAD(&port->head);

	return port;
}
EXPORT_SYMBOL_GPL(phy_port_alloc);

/**
 * phy_port_destroy() - Free a struct phy_port
 * @port: The port to destroy
 */
void phy_port_destroy(struct phy_port *port)
{
	kfree(port);
}
EXPORT_SYMBOL_GPL(phy_port_destroy);

/**
 * phy_of_parse_port() - Create a phy_port from a firmware representation
 * @dn: device_node representation of the port, following the
 *	ethernet-connector.yaml binding
 *
 * Returns: a newly allocated and initialized phy_port pointer, or an ERR_PTR.
 */
struct phy_port *phy_of_parse_port(struct device_node *dn)
{
	struct fwnode_handle *fwnode = of_fwnode_handle(dn);
	enum ethtool_link_medium medium;
	struct phy_port *port;
	const char *med_str;
	u32 pairs = 0, mediums = 0;
	int ret;

	ret = fwnode_property_read_string(fwnode, "media", &med_str);
	if (ret)
		return ERR_PTR(ret);

	medium = ethtool_str_to_medium(med_str);
	if (medium == ETHTOOL_LINK_MEDIUM_NONE)
		return ERR_PTR(-EINVAL);

	if (medium == ETHTOOL_LINK_MEDIUM_BASET) {
		ret = fwnode_property_read_u32(fwnode, "pairs", &pairs);
		if (ret)
			return ERR_PTR(ret);

		switch (pairs) {
		case 1: /* BaseT1 */
		case 2: /* 100BaseTX */
		case 4:
			break;
		default:
			pr_err("%u is not a valid number of pairs\n", pairs);
			return ERR_PTR(-EINVAL);
		}
	}

	if (pairs && medium != ETHTOOL_LINK_MEDIUM_BASET) {
		pr_err("pairs property is only compatible with BaseT medium\n");
		return ERR_PTR(-EINVAL);
	}

	mediums |= BIT(medium);

	if (!mediums)
		return ERR_PTR(-EINVAL);

	port = phy_port_alloc();
	if (!port)
		return ERR_PTR(-ENOMEM);

	port->pairs = pairs;
	port->mediums = mediums;

	return port;
}
EXPORT_SYMBOL_GPL(phy_of_parse_port);

/**
 * phy_port_update_supported() - Setup the port->supported field
 * @port: the port to update
 *
 * Once the port's medium list and number of pairs has been configured based
 * on firmware, straps and vendor-specific properties, this function may be
 * called to update the port's supported linkmodes list.
 *
 * Any mode that was manually set in the port's supported list remains set.
 */
void phy_port_update_supported(struct phy_port *port)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported) = { 0 };
	unsigned long mode;
	int i;

	for_each_set_bit(i, &port->mediums, __ETHTOOL_LINK_MEDIUM_LAST) {
		linkmode_zero(supported);
		phy_caps_medium_get_supported(supported, i, port->pairs);
		linkmode_or(port->supported, port->supported, supported);
	}

	/* If there's no pairs specified, we grab the default number of
	 * pairs as the max of the default pairs for each linkmode
	 */
	if (!port->pairs)
		for_each_set_bit(mode, port->supported,
				 __ETHTOOL_LINK_MODE_MASK_NBITS)
			port->pairs = max_t(int, port->pairs,
					    ethtool_linkmode_n_pairs(mode));

	/* Serdes ports supported through SFP may not have any medium set,
	 * as they will output PHY_INTERFACE_MODE_XXX modes. In that case, derive
	 * the supported list based on these interfaces
	 */
	if (port->is_mii && !port->mediums) {
		unsigned long interface, link_caps = 0;

		/* Get each interface's caps */
		for_each_set_bit(interface, port->interfaces,
				 PHY_INTERFACE_MODE_MAX)
			link_caps |= phy_caps_from_interface(interface);

		phy_caps_linkmodes(link_caps, port->supported);
	}
}
EXPORT_SYMBOL_GPL(phy_port_update_supported);

/**
 * phy_port_filter_supported() - Make sure that port->supported match port->mediums
 * @port: The port to filter
 *
 * After updating a port's mediums to a more restricted subset, this helper will
 * make sure that port->supported only contains linkmodes that are compatible
 * with port->mediums.
 */
static void phy_port_filter_supported(struct phy_port *port)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported) = { 0 };
	int i;

	for_each_set_bit(i, &port->mediums, __ETHTOOL_LINK_MEDIUM_LAST)
		phy_caps_medium_get_supported(supported, i, port->pairs);

	linkmode_and(port->supported, port->supported, supported);
}

/**
 * phy_port_restrict_mediums - Mask away some of the port's supported mediums
 * @port: The port to act upon
 * @mediums: A mask of mediums to support on the port
 *
 * This helper allows removing some mediums from a port's list of supported
 * mediums, which occurs once we have enough information about the port to
 * know its nature.
 *
 * Returns: 0 if the change was donne correctly, a negative value otherwise.
 */
int phy_port_restrict_mediums(struct phy_port *port, unsigned long mediums)
{
	/* We forbid ending-up with a port with empty mediums */
	if (!(port->mediums & mediums))
		return -EINVAL;

	port->mediums &= mediums;

	phy_port_filter_supported(port);

	return 0;
}
EXPORT_SYMBOL_GPL(phy_port_restrict_mediums);

/**
 * phy_port_get_type() - get the PORT_* attribute for that port.
 * @port: The port we want the information from
 *
 * Returns: A PORT_XXX value.
 */
int phy_port_get_type(struct phy_port *port)
{
	if (port->mediums & BIT(ETHTOOL_LINK_MEDIUM_BASET))
		return PORT_TP;

	if (phy_port_is_fiber(port))
		return PORT_FIBRE;

	return PORT_OTHER;
}
EXPORT_SYMBOL_GPL(phy_port_get_type);
