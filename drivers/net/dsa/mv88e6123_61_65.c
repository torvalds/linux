/*
 * net/dsa/mv88e6123_61_65.c - Marvell 88e6123/6161/6165 switch chip support
 * Copyright (c) 2008-2009 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <net/dsa.h>
#include "mv88e6xxx.h"

static char *mv88e6123_61_65_probe(struct device *host_dev, int sw_addr)
{
	struct mii_bus *bus = dsa_host_dev_to_mii_bus(host_dev);
	int ret;

	if (bus == NULL)
		return NULL;

	ret = __mv88e6xxx_reg_read(bus, sw_addr, REG_PORT(0), PORT_SWITCH_ID);
	if (ret >= 0) {
		if (ret == PORT_SWITCH_ID_6123_A1)
			return "Marvell 88E6123 (A1)";
		if (ret == PORT_SWITCH_ID_6123_A2)
			return "Marvell 88E6123 (A2)";
		if ((ret & 0xfff0) == PORT_SWITCH_ID_6123)
			return "Marvell 88E6123";

		if (ret == PORT_SWITCH_ID_6161_A1)
			return "Marvell 88E6161 (A1)";
		if (ret == PORT_SWITCH_ID_6161_A2)
			return "Marvell 88E6161 (A2)";
		if ((ret & 0xfff0) == PORT_SWITCH_ID_6161)
			return "Marvell 88E6161";

		if (ret == PORT_SWITCH_ID_6165_A1)
			return "Marvell 88E6165 (A1)";
		if (ret == PORT_SWITCH_ID_6165_A2)
			return "Marvell 88e6165 (A2)";
		if ((ret & 0xfff0) == PORT_SWITCH_ID_6165)
			return "Marvell 88E6165";
	}

	return NULL;
}

static int mv88e6123_61_65_setup_global(struct dsa_switch *ds)
{
	u32 upstream_port = dsa_upstream_port(ds);
	int ret;
	u32 reg;

	ret = mv88e6xxx_setup_global(ds);
	if (ret)
		return ret;

	/* Disable the PHY polling unit (since there won't be any
	 * external PHYs to poll), don't discard packets with
	 * excessive collisions, and mask all interrupt sources.
	 */
	REG_WRITE(REG_GLOBAL, GLOBAL_CONTROL, 0x0000);

	/* Configure the upstream port, and configure the upstream
	 * port as the port to which ingress and egress monitor frames
	 * are to be sent.
	 */
	reg = upstream_port << GLOBAL_MONITOR_CONTROL_INGRESS_SHIFT |
		upstream_port << GLOBAL_MONITOR_CONTROL_EGRESS_SHIFT |
		upstream_port << GLOBAL_MONITOR_CONTROL_ARP_SHIFT;
	REG_WRITE(REG_GLOBAL, GLOBAL_MONITOR_CONTROL, reg);

	/* Disable remote management for now, and set the switch's
	 * DSA device number.
	 */
	REG_WRITE(REG_GLOBAL, GLOBAL_CONTROL_2, ds->index & 0x1f);

	return 0;
}

static int mv88e6123_61_65_setup(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	ret = mv88e6xxx_setup_common(ds);
	if (ret < 0)
		return ret;

	switch (ps->id) {
	case PORT_SWITCH_ID_6123:
		ps->num_ports = 3;
		break;
	case PORT_SWITCH_ID_6161:
	case PORT_SWITCH_ID_6165:
		ps->num_ports = 6;
		break;
	default:
		return -ENODEV;
	}

	ret = mv88e6xxx_switch_reset(ds, false);
	if (ret < 0)
		return ret;

	ret = mv88e6123_61_65_setup_global(ds);
	if (ret < 0)
		return ret;

	return mv88e6xxx_setup_ports(ds);
}

struct dsa_switch_driver mv88e6123_61_65_switch_driver = {
	.tag_protocol		= DSA_TAG_PROTO_EDSA,
	.priv_size		= sizeof(struct mv88e6xxx_priv_state),
	.probe			= mv88e6123_61_65_probe,
	.setup			= mv88e6123_61_65_setup,
	.set_addr		= mv88e6xxx_set_addr_indirect,
	.phy_read		= mv88e6xxx_phy_read,
	.phy_write		= mv88e6xxx_phy_write,
	.poll_link		= mv88e6xxx_poll_link,
	.get_strings		= mv88e6xxx_get_strings,
	.get_ethtool_stats	= mv88e6xxx_get_ethtool_stats,
	.get_sset_count		= mv88e6xxx_get_sset_count,
	.adjust_link		= mv88e6xxx_adjust_link,
#ifdef CONFIG_NET_DSA_HWMON
	.get_temp		= mv88e6xxx_get_temp,
#endif
	.get_regs_len		= mv88e6xxx_get_regs_len,
	.get_regs		= mv88e6xxx_get_regs,
};

MODULE_ALIAS("platform:mv88e6123");
MODULE_ALIAS("platform:mv88e6161");
MODULE_ALIAS("platform:mv88e6165");
