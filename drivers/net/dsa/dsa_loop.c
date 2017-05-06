/*
 * Distributed Switch Architecture loopback driver
 *
 * Copyright (C) 2016, Florian Fainelli <f.fainelli@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/export.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/if_bridge.h>
#include <net/switchdev.h>
#include <net/dsa.h>

#include "dsa_loop.h"

struct dsa_loop_vlan {
	u16 members;
	u16 untagged;
};

#define DSA_LOOP_VLANS	5

struct dsa_loop_priv {
	struct mii_bus	*bus;
	unsigned int	port_base;
	struct dsa_loop_vlan vlans[DSA_LOOP_VLANS];
	struct net_device *netdev;
	u16 pvid;
};

static struct phy_device *phydevs[PHY_MAX_ADDR];

static enum dsa_tag_protocol dsa_loop_get_protocol(struct dsa_switch *ds)
{
	dev_dbg(ds->dev, "%s\n", __func__);

	return DSA_TAG_PROTO_NONE;
}

static int dsa_loop_setup(struct dsa_switch *ds)
{
	dev_dbg(ds->dev, "%s\n", __func__);

	return 0;
}

static int dsa_loop_set_addr(struct dsa_switch *ds, u8 *addr)
{
	dev_dbg(ds->dev, "%s\n", __func__);

	return 0;
}

static int dsa_loop_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;

	dev_dbg(ds->dev, "%s\n", __func__);

	return mdiobus_read_nested(bus, ps->port_base + port, regnum);
}

static int dsa_loop_phy_write(struct dsa_switch *ds, int port,
			      int regnum, u16 value)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;

	dev_dbg(ds->dev, "%s\n", __func__);

	return mdiobus_write_nested(bus, ps->port_base + port, regnum, value);
}

static int dsa_loop_port_bridge_join(struct dsa_switch *ds, int port,
				     struct net_device *bridge)
{
	dev_dbg(ds->dev, "%s\n", __func__);

	return 0;
}

static void dsa_loop_port_bridge_leave(struct dsa_switch *ds, int port,
				       struct net_device *bridge)
{
	dev_dbg(ds->dev, "%s\n", __func__);
}

static void dsa_loop_port_stp_state_set(struct dsa_switch *ds, int port,
					u8 state)
{
	dev_dbg(ds->dev, "%s\n", __func__);
}

static int dsa_loop_port_vlan_filtering(struct dsa_switch *ds, int port,
					bool vlan_filtering)
{
	dev_dbg(ds->dev, "%s\n", __func__);

	return 0;
}

static int dsa_loop_port_vlan_prepare(struct dsa_switch *ds, int port,
				      const struct switchdev_obj_port_vlan *vlan,
				      struct switchdev_trans *trans)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;

	dev_dbg(ds->dev, "%s\n", __func__);

	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	if (vlan->vid_end > DSA_LOOP_VLANS)
		return -ERANGE;

	return 0;
}

static void dsa_loop_port_vlan_add(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan,
				   struct switchdev_trans *trans)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	struct dsa_loop_vlan *vl;
	u16 vid;

	dev_dbg(ds->dev, "%s\n", __func__);

	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		vl = &ps->vlans[vid];

		vl->members |= BIT(port);
		if (untagged)
			vl->untagged |= BIT(port);
		else
			vl->untagged &= ~BIT(port);
	}

	if (pvid)
		ps->pvid = vid;
}

static int dsa_loop_port_vlan_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	struct dsa_loop_vlan *vl;
	u16 vid, pvid = ps->pvid;

	dev_dbg(ds->dev, "%s\n", __func__);

	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		vl = &ps->vlans[vid];

		vl->members &= ~BIT(port);
		if (untagged)
			vl->untagged &= ~BIT(port);

		if (pvid == vid)
			pvid = 1;
	}
	ps->pvid = pvid;

	return 0;
}

static int dsa_loop_port_vlan_dump(struct dsa_switch *ds, int port,
				   struct switchdev_obj_port_vlan *vlan,
				   int (*cb)(struct switchdev_obj *obj))
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	struct dsa_loop_vlan *vl;
	u16 vid, vid_start = 0;
	int err = 0;

	dev_dbg(ds->dev, "%s\n", __func__);

	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	for (vid = vid_start; vid < DSA_LOOP_VLANS; vid++) {
		vl = &ps->vlans[vid];

		if (!(vl->members & BIT(port)))
			continue;

		vlan->vid_begin = vlan->vid_end = vid;
		vlan->flags = 0;

		if (vl->untagged & BIT(port))
			vlan->flags |= BRIDGE_VLAN_INFO_UNTAGGED;
		if (ps->pvid == vid)
			vlan->flags |= BRIDGE_VLAN_INFO_PVID;

		err = cb(&vlan->obj);
		if (err)
			break;
	}

	return err;
}

static struct dsa_switch_ops dsa_loop_driver = {
	.get_tag_protocol	= dsa_loop_get_protocol,
	.setup			= dsa_loop_setup,
	.set_addr		= dsa_loop_set_addr,
	.phy_read		= dsa_loop_phy_read,
	.phy_write		= dsa_loop_phy_write,
	.port_bridge_join	= dsa_loop_port_bridge_join,
	.port_bridge_leave	= dsa_loop_port_bridge_leave,
	.port_stp_state_set	= dsa_loop_port_stp_state_set,
	.port_vlan_filtering	= dsa_loop_port_vlan_filtering,
	.port_vlan_prepare	= dsa_loop_port_vlan_prepare,
	.port_vlan_add		= dsa_loop_port_vlan_add,
	.port_vlan_del		= dsa_loop_port_vlan_del,
	.port_vlan_dump		= dsa_loop_port_vlan_dump,
};

static int dsa_loop_drv_probe(struct mdio_device *mdiodev)
{
	struct dsa_loop_pdata *pdata = mdiodev->dev.platform_data;
	struct dsa_loop_priv *ps;
	struct dsa_switch *ds;

	if (!pdata)
		return -ENODEV;

	dev_info(&mdiodev->dev, "%s: 0x%0x\n",
		 pdata->name, pdata->enabled_ports);

	ds = dsa_switch_alloc(&mdiodev->dev, DSA_MAX_PORTS);
	if (!ds)
		return -ENOMEM;

	ps = devm_kzalloc(&mdiodev->dev, sizeof(*ps), GFP_KERNEL);
	if (!ps)
		return -ENOMEM;

	ps->netdev = dev_get_by_name(&init_net, pdata->netdev);
	if (!ps->netdev)
		return -EPROBE_DEFER;

	pdata->cd.netdev[DSA_LOOP_CPU_PORT] = &ps->netdev->dev;

	ds->dev = &mdiodev->dev;
	ds->ops = &dsa_loop_driver;
	ds->priv = ps;
	ps->bus = mdiodev->bus;

	dev_set_drvdata(&mdiodev->dev, ds);

	return dsa_register_switch(ds, ds->dev);
}

static void dsa_loop_drv_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct dsa_loop_priv *ps = ds->priv;

	dsa_unregister_switch(ds);
	dev_put(ps->netdev);
}

static struct mdio_driver dsa_loop_drv = {
	.mdiodrv.driver	= {
		.name	= "dsa-loop",
	},
	.probe	= dsa_loop_drv_probe,
	.remove	= dsa_loop_drv_remove,
};

#define NUM_FIXED_PHYS	(DSA_LOOP_NUM_PORTS - 2)

static void unregister_fixed_phys(void)
{
	unsigned int i;

	for (i = 0; i < NUM_FIXED_PHYS; i++)
		if (phydevs[i])
			fixed_phy_unregister(phydevs[i]);
}

static int __init dsa_loop_init(void)
{
	struct fixed_phy_status status = {
		.link = 1,
		.speed = SPEED_100,
		.duplex = DUPLEX_FULL,
	};
	unsigned int i;

	for (i = 0; i < NUM_FIXED_PHYS; i++)
		phydevs[i] = fixed_phy_register(PHY_POLL, &status, -1, NULL);

	return mdio_driver_register(&dsa_loop_drv);
}
module_init(dsa_loop_init);

static void __exit dsa_loop_exit(void)
{
	mdio_driver_unregister(&dsa_loop_drv);
	unregister_fixed_phys();
}
module_exit(dsa_loop_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Fainelli");
MODULE_DESCRIPTION("DSA loopback driver");
