/* net/dsa/mv88e6171.c - Marvell 88e6171/8826172 switch chip support
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

static char *mv88e6171_probe(struct device *host_dev, int sw_addr)
{
	struct mii_bus *bus = dsa_host_dev_to_mii_bus(host_dev);
	int ret;

	if (bus == NULL)
		return NULL;

	ret = __mv88e6xxx_reg_read(bus, sw_addr, REG_PORT(0), PORT_SWITCH_ID);
	if (ret >= 0) {
		if ((ret & 0xfff0) == PORT_SWITCH_ID_6171)
			return "Marvell 88E6171";
		if ((ret & 0xfff0) == PORT_SWITCH_ID_6172)
			return "Marvell 88E6172";
	}

	return NULL;
}

static int mv88e6171_setup_global(struct dsa_switch *ds)
{
	int ret;

	ret = mv88e6xxx_setup_global(ds);
	if (ret)
		return ret;

	/* Discard packets with excessive collisions, mask all
	 * interrupt sources, enable PPU.
	 */
	REG_WRITE(REG_GLOBAL, 0x04, 0x6000);

	/* Configure the upstream port, and configure the upstream
	 * port as the port to which ingress and egress monitor frames
	 * are to be sent.
	 */
	if (REG_READ(REG_PORT(0), 0x03) == 0x1710)
		REG_WRITE(REG_GLOBAL, 0x1a, (dsa_upstream_port(ds) * 0x1111));
	else
		REG_WRITE(REG_GLOBAL, 0x1a, (dsa_upstream_port(ds) * 0x1110));

	/* Disable remote management for now, and set the switch's
	 * DSA device number.
	 */
	REG_WRITE(REG_GLOBAL, 0x1c, ds->index & 0x1f);

	return 0;
}

static int mv88e6171_setup(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int i;
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

	for (i = 0; i < ps->num_ports; i++) {
		if (!(dsa_is_cpu_port(ds, i) || ds->phys_port_mask & (1 << i)))
			continue;

		ret = mv88e6xxx_setup_port(ds, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int mv88e6171_get_eee(struct dsa_switch *ds, int port,
			     struct ethtool_eee *e)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);

	if (ps->id == PORT_SWITCH_ID_6172)
		return mv88e6xxx_get_eee(ds, port, e);

	return -EOPNOTSUPP;
}

static int mv88e6171_set_eee(struct dsa_switch *ds, int port,
			     struct phy_device *phydev, struct ethtool_eee *e)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);

	if (ps->id == PORT_SWITCH_ID_6172)
		return mv88e6xxx_set_eee(ds, port, phydev, e);

	return -EOPNOTSUPP;
}

struct dsa_switch_driver mv88e6171_switch_driver = {
	.tag_protocol		= DSA_TAG_PROTO_EDSA,
	.priv_size		= sizeof(struct mv88e6xxx_priv_state),
	.probe			= mv88e6171_probe,
	.setup			= mv88e6171_setup,
	.set_addr		= mv88e6xxx_set_addr_indirect,
	.phy_read		= mv88e6xxx_phy_read_indirect,
	.phy_write		= mv88e6xxx_phy_write_indirect,
	.poll_link		= mv88e6xxx_poll_link,
	.get_strings		= mv88e6xxx_get_strings,
	.get_ethtool_stats	= mv88e6xxx_get_ethtool_stats,
	.get_sset_count		= mv88e6xxx_get_sset_count,
	.set_eee		= mv88e6171_set_eee,
	.get_eee		= mv88e6171_get_eee,
#ifdef CONFIG_NET_DSA_HWMON
	.get_temp               = mv88e6xxx_get_temp,
#endif
	.get_regs_len		= mv88e6xxx_get_regs_len,
	.get_regs		= mv88e6xxx_get_regs,
	.port_join_bridge       = mv88e6xxx_join_bridge,
	.port_leave_bridge      = mv88e6xxx_leave_bridge,
	.port_stp_update        = mv88e6xxx_port_stp_update,
	.fdb_add		= mv88e6xxx_port_fdb_add,
	.fdb_del		= mv88e6xxx_port_fdb_del,
	.fdb_getnext		= mv88e6xxx_port_fdb_getnext,
};

MODULE_ALIAS("platform:mv88e6171");
MODULE_ALIAS("platform:mv88e6172");
