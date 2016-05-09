/*
 * net/dsa/mv88e6131.c - Marvell 88e6095/6095f/6131 switch chip support
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

static const struct mv88e6xxx_info mv88e6131_table[] = {
	{
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6095,
		.family = MV88E6XXX_FAMILY_6095,
		.name = "Marvell 88E6095/88E6095F",
		.num_databases = 256,
		.num_ports = 11,
		.flags = MV88E6XXX_FLAGS_FAMILY_6095,
	}, {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6085,
		.family = MV88E6XXX_FAMILY_6097,
		.name = "Marvell 88E6085",
		.num_databases = 4096,
		.num_ports = 10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6097,
	}, {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6131,
		.family = MV88E6XXX_FAMILY_6185,
		.name = "Marvell 88E6131",
		.num_databases = 256,
		.num_ports = 8,
		.flags = MV88E6XXX_FLAGS_FAMILY_6185,
	}, {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6185,
		.family = MV88E6XXX_FAMILY_6185,
		.name = "Marvell 88E6185",
		.num_databases = 256,
		.num_ports = 10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6185,
	}
};

static const char *mv88e6131_drv_probe(struct device *dsa_dev,
				       struct device *host_dev, int sw_addr,
				       void **priv)
{
	return mv88e6xxx_drv_probe(dsa_dev, host_dev, sw_addr, priv,
				   mv88e6131_table,
				   ARRAY_SIZE(mv88e6131_table));
}

struct dsa_switch_driver mv88e6131_switch_driver = {
	.tag_protocol		= DSA_TAG_PROTO_EDSA,
	.probe			= mv88e6131_drv_probe,
	.setup			= mv88e6xxx_setup,
	.set_addr		= mv88e6xxx_set_addr,
	.phy_read		= mv88e6xxx_phy_read,
	.phy_write		= mv88e6xxx_phy_write,
	.set_eee		= mv88e6xxx_set_eee,
	.get_eee		= mv88e6xxx_get_eee,
	.get_strings		= mv88e6xxx_get_strings,
	.get_ethtool_stats	= mv88e6xxx_get_ethtool_stats,
	.get_sset_count		= mv88e6xxx_get_sset_count,
	.get_eeprom		= mv88e6xxx_get_eeprom,
	.set_eeprom		= mv88e6xxx_set_eeprom,
	.get_regs_len		= mv88e6xxx_get_regs_len,
	.get_regs		= mv88e6xxx_get_regs,
#ifdef CONFIG_NET_DSA_HWMON
	.get_temp		= mv88e6xxx_get_temp,
	.get_temp_limit		= mv88e6xxx_get_temp_limit,
	.set_temp_limit		= mv88e6xxx_set_temp_limit,
	.get_temp_alarm		= mv88e6xxx_get_temp_alarm,
#endif
	.adjust_link		= mv88e6xxx_adjust_link,
	.port_bridge_join	= mv88e6xxx_port_bridge_join,
	.port_bridge_leave	= mv88e6xxx_port_bridge_leave,
	.port_stp_state_set	= mv88e6xxx_port_stp_state_set,
	.port_vlan_filtering	= mv88e6xxx_port_vlan_filtering,
	.port_vlan_prepare	= mv88e6xxx_port_vlan_prepare,
	.port_vlan_add		= mv88e6xxx_port_vlan_add,
	.port_vlan_del		= mv88e6xxx_port_vlan_del,
	.port_vlan_dump		= mv88e6xxx_port_vlan_dump,
	.port_fdb_prepare       = mv88e6xxx_port_fdb_prepare,
	.port_fdb_add           = mv88e6xxx_port_fdb_add,
	.port_fdb_del           = mv88e6xxx_port_fdb_del,
	.port_fdb_dump          = mv88e6xxx_port_fdb_dump,
};

MODULE_ALIAS("platform:mv88e6085");
MODULE_ALIAS("platform:mv88e6095");
MODULE_ALIAS("platform:mv88e6095f");
MODULE_ALIAS("platform:mv88e6131");
