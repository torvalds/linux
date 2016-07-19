/* net/dsa/mv88e6171.c - Marvell 88e6171 switch chip support
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2014 Claudio Leite <leitec@staticky.com>
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

static const struct mv88e6xxx_switch_id mv88e6171_table[] = {
	{ PORT_SWITCH_ID_6171, "Marvell 88E6171" },
	{ PORT_SWITCH_ID_6175, "Marvell 88E6175" },
	{ PORT_SWITCH_ID_6350, "Marvell 88E6350" },
	{ PORT_SWITCH_ID_6351, "Marvell 88E6351" },
};

static char *mv88e6171_probe(struct device *host_dev, int sw_addr)
{
	return mv88e6xxx_lookup_name(host_dev, sw_addr, mv88e6171_table,
				     ARRAY_SIZE(mv88e6171_table));
}

static int mv88e6171_setup_global(struct dsa_switch *ds)
{
	u32 upstream_port = dsa_upstream_port(ds);
	int ret;
	u32 reg;

	ret = mv88e6xxx_setup_global(ds);
	if (ret)
		return ret;

	/* Discard packets with excessive collisions, mask all
	 * interrupt sources, enable PPU.
	 */
	REG_WRITE(REG_GLOBAL, GLOBAL_CONTROL,
		  GLOBAL_CONTROL_PPU_ENABLE | GLOBAL_CONTROL_DISCARD_EXCESS);

	/* Configure the upstream port, and configure the upstream
	 * port as the port to which ingress and egress monitor frames
	 * are to be sent.
	 */
	reg = upstream_port << GLOBAL_MONITOR_CONTROL_INGRESS_SHIFT |
		upstream_port << GLOBAL_MONITOR_CONTROL_EGRESS_SHIFT |
		upstream_port << GLOBAL_MONITOR_CONTROL_ARP_SHIFT |
		upstream_port << GLOBAL_MONITOR_CONTROL_MIRROR_SHIFT;
	REG_WRITE(REG_GLOBAL, GLOBAL_MONITOR_CONTROL, reg);

	/* Disable remote management for now, and set the switch's
	 * DSA device number.
	 */
	REG_WRITE(REG_GLOBAL, GLOBAL_CONTROL_2, ds->index & 0x1f);

	return 0;
}

static int mv88e6171_setup(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	ret = mv88e6xxx_setup_common(ds);
	if (ret < 0)
		return ret;

	ps->num_ports = 7;

	ret = mv88e6xxx_switch_reset(ds, true);
	if (ret < 0)
		return ret;

	ret = mv88e6171_setup_global(ds);
	if (ret < 0)
		return ret;

	return mv88e6xxx_setup_ports(ds);
}

struct dsa_switch_driver mv88e6171_switch_driver = {
	.tag_protocol		= DSA_TAG_PROTO_EDSA,
	.priv_size		= sizeof(struct mv88e6xxx_priv_state),
	.probe			= mv88e6171_probe,
	.setup			= mv88e6171_setup,
	.set_addr		= mv88e6xxx_set_addr_indirect,
	.phy_read		= mv88e6xxx_phy_read_indirect,
	.phy_write		= mv88e6xxx_phy_write_indirect,
	.get_strings		= mv88e6xxx_get_strings,
	.get_ethtool_stats	= mv88e6xxx_get_ethtool_stats,
	.get_sset_count		= mv88e6xxx_get_sset_count,
	.adjust_link		= mv88e6xxx_adjust_link,
#ifdef CONFIG_NET_DSA_HWMON
	.get_temp               = mv88e6xxx_get_temp,
#endif
	.get_regs_len		= mv88e6xxx_get_regs_len,
	.get_regs		= mv88e6xxx_get_regs,
	.port_bridge_join	= mv88e6xxx_port_bridge_join,
	.port_bridge_leave	= mv88e6xxx_port_bridge_leave,
	.port_stp_update        = mv88e6xxx_port_stp_update,
	.port_vlan_filtering	= mv88e6xxx_port_vlan_filtering,
	.port_vlan_prepare	= mv88e6xxx_port_vlan_prepare,
	.port_vlan_add		= mv88e6xxx_port_vlan_add,
	.port_vlan_del		= mv88e6xxx_port_vlan_del,
	.port_vlan_dump		= mv88e6xxx_port_vlan_dump,
	.port_fdb_prepare	= mv88e6xxx_port_fdb_prepare,
	.port_fdb_add		= mv88e6xxx_port_fdb_add,
	.port_fdb_del		= mv88e6xxx_port_fdb_del,
	.port_fdb_dump		= mv88e6xxx_port_fdb_dump,
};

MODULE_ALIAS("platform:mv88e6171");
MODULE_ALIAS("platform:mv88e6175");
MODULE_ALIAS("platform:mv88e6350");
MODULE_ALIAS("platform:mv88e6351");
