// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Distributed Switch Architecture loopback driver
 *
 * Copyright (C) 2016, Florian Fainelli <f.fainelli@gmail.com>
 */

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/export.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/if_bridge.h>
#include <linux/dsa/loop.h>
#include <net/dsa.h>

#include "dsa_loop.h"

static struct dsa_loop_mib_entry dsa_loop_mibs[] = {
	[DSA_LOOP_PHY_READ_OK]	= { "phy_read_ok", },
	[DSA_LOOP_PHY_READ_ERR]	= { "phy_read_err", },
	[DSA_LOOP_PHY_WRITE_OK] = { "phy_write_ok", },
	[DSA_LOOP_PHY_WRITE_ERR] = { "phy_write_err", },
};

static struct phy_device *phydevs[PHY_MAX_ADDR];

enum dsa_loop_devlink_resource_id {
	DSA_LOOP_DEVLINK_PARAM_ID_VTU,
};

static u64 dsa_loop_devlink_vtu_get(void *priv)
{
	struct dsa_loop_priv *ps = priv;
	unsigned int i, count = 0;
	struct dsa_loop_vlan *vl;

	for (i = 0; i < ARRAY_SIZE(ps->vlans); i++) {
		vl = &ps->vlans[i];
		if (vl->members)
			count++;
	}

	return count;
}

static int dsa_loop_setup_devlink_resources(struct dsa_switch *ds)
{
	struct devlink_resource_size_params size_params;
	struct dsa_loop_priv *ps = ds->priv;
	int err;

	devlink_resource_size_params_init(&size_params, ARRAY_SIZE(ps->vlans),
					  ARRAY_SIZE(ps->vlans),
					  1, DEVLINK_RESOURCE_UNIT_ENTRY);

	err = dsa_devlink_resource_register(ds, "VTU", ARRAY_SIZE(ps->vlans),
					    DSA_LOOP_DEVLINK_PARAM_ID_VTU,
					    DEVLINK_RESOURCE_ID_PARENT_TOP,
					    &size_params);
	if (err)
		goto out;

	dsa_devlink_resource_occ_get_register(ds,
					      DSA_LOOP_DEVLINK_PARAM_ID_VTU,
					      dsa_loop_devlink_vtu_get, ps);

	return 0;

out:
	dsa_devlink_resources_unregister(ds);
	return err;
}

static enum dsa_tag_protocol dsa_loop_get_protocol(struct dsa_switch *ds,
						   int port,
						   enum dsa_tag_protocol mp)
{
	dev_dbg(ds->dev, "%s: port: %d\n", __func__, port);

	return DSA_TAG_PROTO_NONE;
}

static int dsa_loop_setup(struct dsa_switch *ds)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	for (i = 0; i < ds->num_ports; i++)
		memcpy(ps->ports[i].mib, dsa_loop_mibs,
		       sizeof(dsa_loop_mibs));

	dev_dbg(ds->dev, "%s\n", __func__);

	return dsa_loop_setup_devlink_resources(ds);
}

static void dsa_loop_teardown(struct dsa_switch *ds)
{
	dsa_devlink_resources_unregister(ds);
}

static int dsa_loop_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS && sset != ETH_SS_PHY_STATS)
		return 0;

	return __DSA_LOOP_CNT_MAX;
}

static void dsa_loop_get_strings(struct dsa_switch *ds, int port,
				 u32 stringset, uint8_t *data)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	if (stringset != ETH_SS_STATS && stringset != ETH_SS_PHY_STATS)
		return;

	for (i = 0; i < __DSA_LOOP_CNT_MAX; i++)
		memcpy(data + i * ETH_GSTRING_LEN,
		       ps->ports[port].mib[i].name, ETH_GSTRING_LEN);
}

static void dsa_loop_get_ethtool_stats(struct dsa_switch *ds, int port,
				       uint64_t *data)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	for (i = 0; i < __DSA_LOOP_CNT_MAX; i++)
		data[i] = ps->ports[port].mib[i].val;
}

static int dsa_loop_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	int ret;

	ret = mdiobus_read_nested(bus, ps->port_base + port, regnum);
	if (ret < 0)
		ps->ports[port].mib[DSA_LOOP_PHY_READ_ERR].val++;
	else
		ps->ports[port].mib[DSA_LOOP_PHY_READ_OK].val++;

	return ret;
}

static int dsa_loop_phy_write(struct dsa_switch *ds, int port,
			      int regnum, u16 value)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	int ret;

	ret = mdiobus_write_nested(bus, ps->port_base + port, regnum, value);
	if (ret < 0)
		ps->ports[port].mib[DSA_LOOP_PHY_WRITE_ERR].val++;
	else
		ps->ports[port].mib[DSA_LOOP_PHY_WRITE_OK].val++;

	return ret;
}

static int dsa_loop_port_bridge_join(struct dsa_switch *ds, int port,
				     struct dsa_bridge bridge,
				     bool *tx_fwd_offload,
				     struct netlink_ext_ack *extack)
{
	dev_dbg(ds->dev, "%s: port: %d, bridge: %s\n",
		__func__, port, bridge.dev->name);

	return 0;
}

static void dsa_loop_port_bridge_leave(struct dsa_switch *ds, int port,
				       struct dsa_bridge bridge)
{
	dev_dbg(ds->dev, "%s: port: %d, bridge: %s\n",
		__func__, port, bridge.dev->name);
}

static void dsa_loop_port_stp_state_set(struct dsa_switch *ds, int port,
					u8 state)
{
	dev_dbg(ds->dev, "%s: port: %d, state: %d\n",
		__func__, port, state);
}

static int dsa_loop_port_vlan_filtering(struct dsa_switch *ds, int port,
					bool vlan_filtering,
					struct netlink_ext_ack *extack)
{
	dev_dbg(ds->dev, "%s: port: %d, vlan_filtering: %d\n",
		__func__, port, vlan_filtering);

	return 0;
}

static int dsa_loop_port_vlan_add(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan,
				  struct netlink_ext_ack *extack)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	struct dsa_loop_vlan *vl;

	if (vlan->vid >= ARRAY_SIZE(ps->vlans))
		return -ERANGE;

	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	vl = &ps->vlans[vlan->vid];

	vl->members |= BIT(port);
	if (untagged)
		vl->untagged |= BIT(port);
	else
		vl->untagged &= ~BIT(port);

	dev_dbg(ds->dev, "%s: port: %d vlan: %d, %stagged, pvid: %d\n",
		__func__, port, vlan->vid, untagged ? "un" : "", pvid);

	if (pvid)
		ps->ports[port].pvid = vlan->vid;

	return 0;
}

static int dsa_loop_port_vlan_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct dsa_loop_priv *ps = ds->priv;
	u16 pvid = ps->ports[port].pvid;
	struct mii_bus *bus = ps->bus;
	struct dsa_loop_vlan *vl;

	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	vl = &ps->vlans[vlan->vid];

	vl->members &= ~BIT(port);
	if (untagged)
		vl->untagged &= ~BIT(port);

	if (pvid == vlan->vid)
		pvid = 1;

	dev_dbg(ds->dev, "%s: port: %d vlan: %d, %stagged, pvid: %d\n",
		__func__, port, vlan->vid, untagged ? "un" : "", pvid);
	ps->ports[port].pvid = pvid;

	return 0;
}

static int dsa_loop_port_change_mtu(struct dsa_switch *ds, int port,
				    int new_mtu)
{
	struct dsa_loop_priv *priv = ds->priv;

	priv->ports[port].mtu = new_mtu;

	return 0;
}

static int dsa_loop_port_max_mtu(struct dsa_switch *ds, int port)
{
	return ETH_MAX_MTU;
}

static const struct dsa_switch_ops dsa_loop_driver = {
	.get_tag_protocol	= dsa_loop_get_protocol,
	.setup			= dsa_loop_setup,
	.teardown		= dsa_loop_teardown,
	.get_strings		= dsa_loop_get_strings,
	.get_ethtool_stats	= dsa_loop_get_ethtool_stats,
	.get_sset_count		= dsa_loop_get_sset_count,
	.get_ethtool_phy_stats	= dsa_loop_get_ethtool_stats,
	.phy_read		= dsa_loop_phy_read,
	.phy_write		= dsa_loop_phy_write,
	.port_bridge_join	= dsa_loop_port_bridge_join,
	.port_bridge_leave	= dsa_loop_port_bridge_leave,
	.port_stp_state_set	= dsa_loop_port_stp_state_set,
	.port_vlan_filtering	= dsa_loop_port_vlan_filtering,
	.port_vlan_add		= dsa_loop_port_vlan_add,
	.port_vlan_del		= dsa_loop_port_vlan_del,
	.port_change_mtu	= dsa_loop_port_change_mtu,
	.port_max_mtu		= dsa_loop_port_max_mtu,
};

static int dsa_loop_drv_probe(struct mdio_device *mdiodev)
{
	struct dsa_loop_pdata *pdata = mdiodev->dev.platform_data;
	struct dsa_loop_priv *ps;
	struct dsa_switch *ds;
	int ret;

	if (!pdata)
		return -ENODEV;

	ds = devm_kzalloc(&mdiodev->dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	ds->dev = &mdiodev->dev;
	ds->num_ports = DSA_LOOP_NUM_PORTS;

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

	ret = dsa_register_switch(ds);
	if (!ret)
		dev_info(&mdiodev->dev, "%s: 0x%0x\n",
			 pdata->name, pdata->enabled_ports);

	return ret;
}

static void dsa_loop_drv_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct dsa_loop_priv *ps;

	if (!ds)
		return;

	ps = ds->priv;

	dsa_unregister_switch(ds);
	dev_put(ps->netdev);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static void dsa_loop_drv_shutdown(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);

	if (!ds)
		return;

	dsa_switch_shutdown(ds);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static struct mdio_driver dsa_loop_drv = {
	.mdiodrv.driver	= {
		.name	= "dsa-loop",
	},
	.probe	= dsa_loop_drv_probe,
	.remove	= dsa_loop_drv_remove,
	.shutdown = dsa_loop_drv_shutdown,
};

#define NUM_FIXED_PHYS	(DSA_LOOP_NUM_PORTS - 2)

static int __init dsa_loop_init(void)
{
	struct fixed_phy_status status = {
		.link = 1,
		.speed = SPEED_100,
		.duplex = DUPLEX_FULL,
	};
	unsigned int i;

	for (i = 0; i < NUM_FIXED_PHYS; i++)
		phydevs[i] = fixed_phy_register(PHY_POLL, &status, NULL);

	return mdio_driver_register(&dsa_loop_drv);
}
module_init(dsa_loop_init);

static void __exit dsa_loop_exit(void)
{
	unsigned int i;

	mdio_driver_unregister(&dsa_loop_drv);
	for (i = 0; i < NUM_FIXED_PHYS; i++)
		if (!IS_ERR(phydevs[i]))
			fixed_phy_unregister(phydevs[i]);
}
module_exit(dsa_loop_exit);

MODULE_SOFTDEP("pre: dsa_loop_bdinfo");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Fainelli");
MODULE_DESCRIPTION("DSA loopback driver");
