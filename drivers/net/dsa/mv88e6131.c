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

static char *mv88e6131_probe(struct device *host_dev, int sw_addr)
{
	struct mii_bus *bus = dsa_host_dev_to_mii_bus(host_dev);
	int ret;

	if (bus == NULL)
		return NULL;

	ret = __mv88e6xxx_reg_read(bus, sw_addr, REG_PORT(0), PORT_SWITCH_ID);
	if (ret >= 0) {
		int ret_masked = ret & 0xfff0;

		if (ret_masked == PORT_SWITCH_ID_6085)
			return "Marvell 88E6085";
		if (ret_masked == PORT_SWITCH_ID_6095)
			return "Marvell 88E6095/88E6095F";
		if (ret == PORT_SWITCH_ID_6131_B2)
			return "Marvell 88E6131 (B2)";
		if (ret_masked == PORT_SWITCH_ID_6131)
			return "Marvell 88E6131";
	}

	return NULL;
}

static int mv88e6131_setup_global(struct dsa_switch *ds)
{
	int ret;
	int i;

	/* Enable the PHY polling unit, don't discard packets with
	 * excessive collisions, use a weighted fair queueing scheme
	 * to arbitrate between packet queues, set the maximum frame
	 * size to 1632, and mask all interrupt sources.
	 */
	REG_WRITE(REG_GLOBAL, 0x04, 0x4400);

	/* Set the default address aging time to 5 minutes, and
	 * enable address learn messages to be sent to all message
	 * ports.
	 */
	REG_WRITE(REG_GLOBAL, 0x0a, 0x0148);

	/* Configure the priority mapping registers. */
	ret = mv88e6xxx_config_prio(ds);
	if (ret < 0)
		return ret;

	/* Set the VLAN ethertype to 0x8100. */
	REG_WRITE(REG_GLOBAL, 0x19, 0x8100);

	/* Disable ARP mirroring, and configure the upstream port as
	 * the port to which ingress and egress monitor frames are to
	 * be sent.
	 */
	REG_WRITE(REG_GLOBAL, 0x1a, (dsa_upstream_port(ds) * 0x1100) | 0x00f0);

	/* Disable cascade port functionality unless this device
	 * is used in a cascade configuration, and set the switch's
	 * DSA device number.
	 */
	if (ds->dst->pd->nr_chips > 1)
		REG_WRITE(REG_GLOBAL, 0x1c, 0xf000 | (ds->index & 0x1f));
	else
		REG_WRITE(REG_GLOBAL, 0x1c, 0xe000 | (ds->index & 0x1f));

	/* Send all frames with destination addresses matching
	 * 01:80:c2:00:00:0x to the CPU port.
	 */
	REG_WRITE(REG_GLOBAL2, 0x03, 0xffff);

	/* Ignore removed tag data on doubly tagged packets, disable
	 * flow control messages, force flow control priority to the
	 * highest, and send all special multicast frames to the CPU
	 * port at the highest priority.
	 */
	REG_WRITE(REG_GLOBAL2, 0x05, 0x00ff);

	/* Program the DSA routing table. */
	for (i = 0; i < 32; i++) {
		int nexthop;

		nexthop = 0x1f;
		if (ds->pd->rtable &&
		    i != ds->index && i < ds->dst->pd->nr_chips)
			nexthop = ds->pd->rtable[i] & 0x1f;

		REG_WRITE(REG_GLOBAL2, 0x06, 0x8000 | (i << 8) | nexthop);
	}

	/* Clear all trunk masks. */
	for (i = 0; i < 8; i++)
		REG_WRITE(REG_GLOBAL2, 0x07, 0x8000 | (i << 12) | 0x7ff);

	/* Clear all trunk mappings. */
	for (i = 0; i < 16; i++)
		REG_WRITE(REG_GLOBAL2, 0x08, 0x8000 | (i << 11));

	/* Force the priority of IGMP/MLD snoop frames and ARP frames
	 * to the highest setting.
	 */
	REG_WRITE(REG_GLOBAL2, 0x0f, 0x00ff);

	return 0;
}

static int mv88e6131_setup_port(struct dsa_switch *ds, int p)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int addr = REG_PORT(p);
	u16 val;

	/* MAC Forcing register: don't force link, speed, duplex
	 * or flow control state to any particular values on physical
	 * ports, but force the CPU port and all DSA ports to 1000 Mb/s
	 * (100 Mb/s on 6085) full duplex.
	 */
	if (dsa_is_cpu_port(ds, p) || ds->dsa_port_mask & (1 << p))
		if (ps->id == PORT_SWITCH_ID_6085)
			REG_WRITE(addr, 0x01, 0x003d); /* 100 Mb/s */
		else
			REG_WRITE(addr, 0x01, 0x003e); /* 1000 Mb/s */
	else
		REG_WRITE(addr, 0x01, 0x0003);

	/* Port Control: disable Core Tag, disable Drop-on-Lock,
	 * transmit frames unmodified, disable Header mode,
	 * enable IGMP/MLD snoop, disable DoubleTag, disable VLAN
	 * tunneling, determine priority by looking at 802.1p and
	 * IP priority fields (IP prio has precedence), and set STP
	 * state to Forwarding.
	 *
	 * If this is the upstream port for this switch, enable
	 * forwarding of unknown unicasts, and enable DSA tagging
	 * mode.
	 *
	 * If this is the link to another switch, use DSA tagging
	 * mode, but do not enable forwarding of unknown unicasts.
	 */
	val = 0x0433;
	if (p == dsa_upstream_port(ds)) {
		val |= 0x0104;
		/* On 6085, unknown multicast forward is controlled
		 * here rather than in Port Control 2 register.
		 */
		if (ps->id == PORT_SWITCH_ID_6085)
			val |= 0x0008;
	}
	if (ds->dsa_port_mask & (1 << p))
		val |= 0x0100;
	REG_WRITE(addr, 0x04, val);

	/* Port Control 2: don't force a good FCS, don't use
	 * VLAN-based, source address-based or destination
	 * address-based priority overrides, don't let the switch
	 * add or strip 802.1q tags, don't discard tagged or
	 * untagged frames on this port, do a destination address
	 * lookup on received packets as usual, don't send a copy
	 * of all transmitted/received frames on this port to the
	 * CPU, and configure the upstream port number.
	 *
	 * If this is the upstream port for this switch, enable
	 * forwarding of unknown multicast addresses.
	 */
	if (ps->id == PORT_SWITCH_ID_6085)
		/* on 6085, bits 3:0 are reserved, bit 6 control ARP
		 * mirroring, and multicast forward is handled in
		 * Port Control register.
		 */
		REG_WRITE(addr, 0x08, 0x0080);
	else {
		val = 0x0080 | dsa_upstream_port(ds);
		if (p == dsa_upstream_port(ds))
			val |= 0x0040;
		REG_WRITE(addr, 0x08, val);
	}

	/* Rate Control: disable ingress rate limiting. */
	REG_WRITE(addr, 0x09, 0x0000);

	/* Rate Control 2: disable egress rate limiting. */
	REG_WRITE(addr, 0x0a, 0x0000);

	/* Port Association Vector: when learning source addresses
	 * of packets, add the address to the address database using
	 * a port bitmap that has only the bit for this port set and
	 * the other bits clear.
	 */
	REG_WRITE(addr, 0x0b, 1 << p);

	/* Tag Remap: use an identity 802.1p prio -> switch prio
	 * mapping.
	 */
	REG_WRITE(addr, 0x18, 0x3210);

	/* Tag Remap 2: use an identity 802.1p prio -> switch prio
	 * mapping.
	 */
	REG_WRITE(addr, 0x19, 0x7654);

	return mv88e6xxx_setup_port_common(ds, p);
}

static int mv88e6131_setup(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int i;
	int ret;

	ret = mv88e6xxx_setup_common(ds);
	if (ret < 0)
		return ret;

	mv88e6xxx_ppu_state_init(ds);

	switch (ps->id) {
	case PORT_SWITCH_ID_6085:
		ps->num_ports = 10;
		break;
	case PORT_SWITCH_ID_6095:
		ps->num_ports = 11;
		break;
	case PORT_SWITCH_ID_6131:
	case PORT_SWITCH_ID_6131_B2:
		ps->num_ports = 8;
		break;
	default:
		return -ENODEV;
	}

	ret = mv88e6xxx_switch_reset(ds, false);
	if (ret < 0)
		return ret;

	/* @@@ initialise vtu and atu */

	ret = mv88e6131_setup_global(ds);
	if (ret < 0)
		return ret;

	for (i = 0; i < ps->num_ports; i++) {
		ret = mv88e6131_setup_port(ds, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int mv88e6131_port_to_phy_addr(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);

	if (port >= 0 && port < ps->num_ports)
		return port;

	return -EINVAL;
}

static int
mv88e6131_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	int addr = mv88e6131_port_to_phy_addr(ds, port);

	if (addr < 0)
		return addr;

	return mv88e6xxx_phy_read_ppu(ds, addr, regnum);
}

static int
mv88e6131_phy_write(struct dsa_switch *ds,
			      int port, int regnum, u16 val)
{
	int addr = mv88e6131_port_to_phy_addr(ds, port);

	if (addr < 0)
		return addr;

	return mv88e6xxx_phy_write_ppu(ds, addr, regnum, val);
}

struct dsa_switch_driver mv88e6131_switch_driver = {
	.tag_protocol		= DSA_TAG_PROTO_DSA,
	.priv_size		= sizeof(struct mv88e6xxx_priv_state),
	.probe			= mv88e6131_probe,
	.setup			= mv88e6131_setup,
	.set_addr		= mv88e6xxx_set_addr_direct,
	.phy_read		= mv88e6131_phy_read,
	.phy_write		= mv88e6131_phy_write,
	.poll_link		= mv88e6xxx_poll_link,
	.get_strings		= mv88e6xxx_get_strings,
	.get_ethtool_stats	= mv88e6xxx_get_ethtool_stats,
	.get_sset_count		= mv88e6xxx_get_sset_count,
};

MODULE_ALIAS("platform:mv88e6085");
MODULE_ALIAS("platform:mv88e6095");
MODULE_ALIAS("platform:mv88e6095f");
MODULE_ALIAS("platform:mv88e6131");
