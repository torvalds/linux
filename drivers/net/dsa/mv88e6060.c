/*
 * net/dsa/mv88e6060.c - Driver for Marvell 88e6060 switch chips
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
#include "mv88e6060.h"

static int reg_read(struct dsa_switch *ds, int addr, int reg)
{
	struct mv88e6060_priv *priv = ds->priv;

	return mdiobus_read_nested(priv->bus, priv->sw_addr + addr, reg);
}

#define REG_READ(addr, reg)					\
	({							\
		int __ret;					\
								\
		__ret = reg_read(ds, addr, reg);		\
		if (__ret < 0)					\
			return __ret;				\
		__ret;						\
	})


static int reg_write(struct dsa_switch *ds, int addr, int reg, u16 val)
{
	struct mv88e6060_priv *priv = ds->priv;

	return mdiobus_write_nested(priv->bus, priv->sw_addr + addr, reg, val);
}

#define REG_WRITE(addr, reg, val)				\
	({							\
		int __ret;					\
								\
		__ret = reg_write(ds, addr, reg, val);		\
		if (__ret < 0)					\
			return __ret;				\
	})

static const char *mv88e6060_get_name(struct mii_bus *bus, int sw_addr)
{
	int ret;

	ret = mdiobus_read(bus, sw_addr + REG_PORT(0), PORT_SWITCH_ID);
	if (ret >= 0) {
		if (ret == PORT_SWITCH_ID_6060)
			return "Marvell 88E6060 (A0)";
		if (ret == PORT_SWITCH_ID_6060_R1 ||
		    ret == PORT_SWITCH_ID_6060_R2)
			return "Marvell 88E6060 (B0)";
		if ((ret & PORT_SWITCH_ID_6060_MASK) == PORT_SWITCH_ID_6060)
			return "Marvell 88E6060";
	}

	return NULL;
}

static enum dsa_tag_protocol mv88e6060_get_tag_protocol(struct dsa_switch *ds)
{
	return DSA_TAG_PROTO_TRAILER;
}

static const char *mv88e6060_drv_probe(struct device *dsa_dev,
				       struct device *host_dev, int sw_addr,
				       void **_priv)
{
	struct mii_bus *bus = dsa_host_dev_to_mii_bus(host_dev);
	struct mv88e6060_priv *priv;
	const char *name;

	name = mv88e6060_get_name(bus, sw_addr);
	if (name) {
		priv = devm_kzalloc(dsa_dev, sizeof(*priv), GFP_KERNEL);
		if (!priv)
			return NULL;
		*_priv = priv;
		priv->bus = bus;
		priv->sw_addr = sw_addr;
	}

	return name;
}

static int mv88e6060_switch_reset(struct dsa_switch *ds)
{
	int i;
	int ret;
	unsigned long timeout;

	/* Set all ports to the disabled state. */
	for (i = 0; i < MV88E6060_PORTS; i++) {
		ret = REG_READ(REG_PORT(i), PORT_CONTROL);
		REG_WRITE(REG_PORT(i), PORT_CONTROL,
			  ret & ~PORT_CONTROL_STATE_MASK);
	}

	/* Wait for transmit queues to drain. */
	usleep_range(2000, 4000);

	/* Reset the switch. */
	REG_WRITE(REG_GLOBAL, GLOBAL_ATU_CONTROL,
		  GLOBAL_ATU_CONTROL_SWRESET |
		  GLOBAL_ATU_CONTROL_ATUSIZE_1024 |
		  GLOBAL_ATU_CONTROL_ATE_AGE_5MIN);

	/* Wait up to one second for reset to complete. */
	timeout = jiffies + 1 * HZ;
	while (time_before(jiffies, timeout)) {
		ret = REG_READ(REG_GLOBAL, GLOBAL_STATUS);
		if (ret & GLOBAL_STATUS_INIT_READY)
			break;

		usleep_range(1000, 2000);
	}
	if (time_after(jiffies, timeout))
		return -ETIMEDOUT;

	return 0;
}

static int mv88e6060_setup_global(struct dsa_switch *ds)
{
	/* Disable discarding of frames with excessive collisions,
	 * set the maximum frame size to 1536 bytes, and mask all
	 * interrupt sources.
	 */
	REG_WRITE(REG_GLOBAL, GLOBAL_CONTROL, GLOBAL_CONTROL_MAX_FRAME_1536);

	/* Enable automatic address learning, set the address
	 * database size to 1024 entries, and set the default aging
	 * time to 5 minutes.
	 */
	REG_WRITE(REG_GLOBAL, GLOBAL_ATU_CONTROL,
		  GLOBAL_ATU_CONTROL_ATUSIZE_1024 |
		  GLOBAL_ATU_CONTROL_ATE_AGE_5MIN);

	return 0;
}

static int mv88e6060_setup_port(struct dsa_switch *ds, int p)
{
	int addr = REG_PORT(p);

	/* Do not force flow control, disable Ingress and Egress
	 * Header tagging, disable VLAN tunneling, and set the port
	 * state to Forwarding.  Additionally, if this is the CPU
	 * port, enable Ingress and Egress Trailer tagging mode.
	 */
	REG_WRITE(addr, PORT_CONTROL,
		  dsa_is_cpu_port(ds, p) ?
			PORT_CONTROL_TRAILER |
			PORT_CONTROL_INGRESS_MODE |
			PORT_CONTROL_STATE_FORWARDING :
			PORT_CONTROL_STATE_FORWARDING);

	/* Port based VLAN map: give each port its own address
	 * database, allow the CPU port to talk to each of the 'real'
	 * ports, and allow each of the 'real' ports to only talk to
	 * the CPU port.
	 */
	REG_WRITE(addr, PORT_VLAN_MAP,
		  ((p & 0xf) << PORT_VLAN_MAP_DBNUM_SHIFT) |
		   (dsa_is_cpu_port(ds, p) ?
			ds->enabled_port_mask :
			BIT(ds->dst->cpu_port)));

	/* Port Association Vector: when learning source addresses
	 * of packets, add the address to the address database using
	 * a port bitmap that has only the bit for this port set and
	 * the other bits clear.
	 */
	REG_WRITE(addr, PORT_ASSOC_VECTOR, BIT(p));

	return 0;
}

static int mv88e6060_setup(struct dsa_switch *ds)
{
	int ret;
	int i;

	ret = mv88e6060_switch_reset(ds);
	if (ret < 0)
		return ret;

	/* @@@ initialise atu */

	ret = mv88e6060_setup_global(ds);
	if (ret < 0)
		return ret;

	for (i = 0; i < MV88E6060_PORTS; i++) {
		ret = mv88e6060_setup_port(ds, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int mv88e6060_set_addr(struct dsa_switch *ds, u8 *addr)
{
	/* Use the same MAC Address as FD Pause frames for all ports */
	REG_WRITE(REG_GLOBAL, GLOBAL_MAC_01, (addr[0] << 9) | addr[1]);
	REG_WRITE(REG_GLOBAL, GLOBAL_MAC_23, (addr[2] << 8) | addr[3]);
	REG_WRITE(REG_GLOBAL, GLOBAL_MAC_45, (addr[4] << 8) | addr[5]);

	return 0;
}

static int mv88e6060_port_to_phy_addr(int port)
{
	if (port >= 0 && port < MV88E6060_PORTS)
		return port;
	return -1;
}

static int mv88e6060_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	int addr;

	addr = mv88e6060_port_to_phy_addr(port);
	if (addr == -1)
		return 0xffff;

	return reg_read(ds, addr, regnum);
}

static int
mv88e6060_phy_write(struct dsa_switch *ds, int port, int regnum, u16 val)
{
	int addr;

	addr = mv88e6060_port_to_phy_addr(port);
	if (addr == -1)
		return 0xffff;

	return reg_write(ds, addr, regnum, val);
}

static const struct dsa_switch_ops mv88e6060_switch_ops = {
	.get_tag_protocol = mv88e6060_get_tag_protocol,
	.probe		= mv88e6060_drv_probe,
	.setup		= mv88e6060_setup,
	.set_addr	= mv88e6060_set_addr,
	.phy_read	= mv88e6060_phy_read,
	.phy_write	= mv88e6060_phy_write,
};

static struct dsa_switch_driver mv88e6060_switch_drv = {
	.ops		= &mv88e6060_switch_ops,
};

static int __init mv88e6060_init(void)
{
	register_switch_driver(&mv88e6060_switch_drv);
	return 0;
}
module_init(mv88e6060_init);

static void __exit mv88e6060_cleanup(void)
{
	unregister_switch_driver(&mv88e6060_switch_drv);
}
module_exit(mv88e6060_cleanup);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Marvell 88E6060 ethernet switch chip");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mv88e6060");
