// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip switch driver main logic
 *
 * Copyright (C) 2017-2019 Microchip Technology Inc.
 */

#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/microchip-ksz.h>
#include <linux/phy.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/of_net.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "ksz_common.h"

void ksz_update_port_member(struct ksz_device *dev, int port)
{
	struct ksz_port *p;
	int i;

	for (i = 0; i < dev->port_cnt; i++) {
		if (i == port || i == dev->cpu_port)
			continue;
		p = &dev->ports[i];
		if (!(dev->member & (1 << i)))
			continue;

		/* Port is a member of the bridge and is forwarding. */
		if (p->stp_state == BR_STATE_FORWARDING &&
		    p->member != dev->member)
			dev->dev_ops->cfg_port_member(dev, i, dev->member);
	}
}
EXPORT_SYMBOL_GPL(ksz_update_port_member);

static void port_r_cnt(struct ksz_device *dev, int port)
{
	struct ksz_port_mib *mib = &dev->ports[port].mib;
	u64 *dropped;

	/* Some ports may not have MIB counters before SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->reg_mib_cnt) {
		dev->dev_ops->r_mib_cnt(dev, port, mib->cnt_ptr,
					&mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}

	/* last one in storage */
	dropped = &mib->counters[dev->mib_cnt];

	/* Some ports may not have MIB counters after SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->mib_cnt) {
		dev->dev_ops->r_mib_pkt(dev, port, mib->cnt_ptr,
					dropped, &mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}
	mib->cnt_ptr = 0;
}

static void ksz_mib_read_work(struct work_struct *work)
{
	struct ksz_device *dev = container_of(work, struct ksz_device,
					      mib_read.work);
	struct ksz_port_mib *mib;
	struct ksz_port *p;
	int i;

	for (i = 0; i < dev->mib_port_cnt; i++) {
		if (dsa_is_unused_port(dev->ds, i))
			continue;

		p = &dev->ports[i];
		mib = &p->mib;
		mutex_lock(&mib->cnt_mutex);

		/* Only read MIB counters when the port is told to do.
		 * If not, read only dropped counters when link is not up.
		 */
		if (!p->read) {
			const struct dsa_port *dp = dsa_to_port(dev->ds, i);

			if (!netif_carrier_ok(dp->slave))
				mib->cnt_ptr = dev->reg_mib_cnt;
		}
		port_r_cnt(dev, i);
		p->read = false;
		mutex_unlock(&mib->cnt_mutex);
	}

	schedule_delayed_work(&dev->mib_read, dev->mib_read_interval);
}

void ksz_init_mib_timer(struct ksz_device *dev)
{
	int i;

	INIT_DELAYED_WORK(&dev->mib_read, ksz_mib_read_work);

	/* Read MIB counters every 30 seconds to avoid overflow. */
	dev->mib_read_interval = msecs_to_jiffies(30000);

	for (i = 0; i < dev->mib_port_cnt; i++)
		dev->dev_ops->port_init_cnt(dev, i);

	/* Start the timer 2 seconds later. */
	schedule_delayed_work(&dev->mib_read, msecs_to_jiffies(2000));
}
EXPORT_SYMBOL_GPL(ksz_init_mib_timer);

int ksz_phy_read16(struct dsa_switch *ds, int addr, int reg)
{
	struct ksz_device *dev = ds->priv;
	u16 val = 0xffff;

	dev->dev_ops->r_phy(dev, addr, reg, &val);

	return val;
}
EXPORT_SYMBOL_GPL(ksz_phy_read16);

int ksz_phy_write16(struct dsa_switch *ds, int addr, int reg, u16 val)
{
	struct ksz_device *dev = ds->priv;

	dev->dev_ops->w_phy(dev, addr, reg, val);

	return 0;
}
EXPORT_SYMBOL_GPL(ksz_phy_write16);

void ksz_adjust_link(struct dsa_switch *ds, int port,
		     struct phy_device *phydev)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p = &dev->ports[port];

	/* Read all MIB counters when the link is going down. */
	if (!phydev->link) {
		p->read = true;
		schedule_delayed_work(&dev->mib_read, 0);
	}
	mutex_lock(&dev->dev_mutex);
	if (!phydev->link)
		dev->live_ports &= ~(1 << port);
	else
		/* Remember which port is connected and active. */
		dev->live_ports |= (1 << port) & dev->on_ports;
	mutex_unlock(&dev->dev_mutex);
}
EXPORT_SYMBOL_GPL(ksz_adjust_link);

int ksz_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct ksz_device *dev = ds->priv;

	if (sset != ETH_SS_STATS)
		return 0;

	return dev->mib_cnt;
}
EXPORT_SYMBOL_GPL(ksz_sset_count);

void ksz_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *buf)
{
	const struct dsa_port *dp = dsa_to_port(ds, port);
	struct ksz_device *dev = ds->priv;
	struct ksz_port_mib *mib;

	mib = &dev->ports[port].mib;
	mutex_lock(&mib->cnt_mutex);

	/* Only read dropped counters if no link. */
	if (!netif_carrier_ok(dp->slave))
		mib->cnt_ptr = dev->reg_mib_cnt;
	port_r_cnt(dev, port);
	memcpy(buf, mib->counters, dev->mib_cnt * sizeof(u64));
	mutex_unlock(&mib->cnt_mutex);
}
EXPORT_SYMBOL_GPL(ksz_get_ethtool_stats);

int ksz_port_bridge_join(struct dsa_switch *ds, int port,
			 struct net_device *br)
{
	struct ksz_device *dev = ds->priv;

	mutex_lock(&dev->dev_mutex);
	dev->br_member |= (1 << port);
	mutex_unlock(&dev->dev_mutex);

	/* port_stp_state_set() will be called after to put the port in
	 * appropriate state so there is no need to do anything.
	 */

	return 0;
}
EXPORT_SYMBOL_GPL(ksz_port_bridge_join);

void ksz_port_bridge_leave(struct dsa_switch *ds, int port,
			   struct net_device *br)
{
	struct ksz_device *dev = ds->priv;

	mutex_lock(&dev->dev_mutex);
	dev->br_member &= ~(1 << port);
	dev->member &= ~(1 << port);
	mutex_unlock(&dev->dev_mutex);

	/* port_stp_state_set() will be called after to put the port in
	 * forwarding state so there is no need to do anything.
	 */
}
EXPORT_SYMBOL_GPL(ksz_port_bridge_leave);

void ksz_port_fast_age(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;

	dev->dev_ops->flush_dyn_mac_table(dev, port);
}
EXPORT_SYMBOL_GPL(ksz_port_fast_age);

int ksz_port_vlan_prepare(struct dsa_switch *ds, int port,
			  const struct switchdev_obj_port_vlan *vlan)
{
	/* nothing needed */

	return 0;
}
EXPORT_SYMBOL_GPL(ksz_port_vlan_prepare);

int ksz_port_fdb_dump(struct dsa_switch *ds, int port, dsa_fdb_dump_cb_t *cb,
		      void *data)
{
	struct ksz_device *dev = ds->priv;
	int ret = 0;
	u16 i = 0;
	u16 entries = 0;
	u8 timestamp = 0;
	u8 fid;
	u8 member;
	struct alu_struct alu;

	do {
		alu.is_static = false;
		ret = dev->dev_ops->r_dyn_mac_table(dev, i, alu.mac, &fid,
						    &member, &timestamp,
						    &entries);
		if (!ret && (member & BIT(port))) {
			ret = cb(alu.mac, alu.fid, alu.is_static, data);
			if (ret)
				break;
		}
		i++;
	} while (i < entries);
	if (i >= entries)
		ret = 0;

	return ret;
}
EXPORT_SYMBOL_GPL(ksz_port_fdb_dump);

int ksz_port_mdb_prepare(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb)
{
	/* nothing to do */
	return 0;
}
EXPORT_SYMBOL_GPL(ksz_port_mdb_prepare);

void ksz_port_mdb_add(struct dsa_switch *ds, int port,
		      const struct switchdev_obj_port_mdb *mdb)
{
	struct ksz_device *dev = ds->priv;
	struct alu_struct alu;
	int index;
	int empty = 0;

	alu.port_forward = 0;
	for (index = 0; index < dev->num_statics; index++) {
		if (!dev->dev_ops->r_sta_mac_table(dev, index, &alu)) {
			/* Found one already in static MAC table. */
			if (!memcmp(alu.mac, mdb->addr, ETH_ALEN) &&
			    alu.fid == mdb->vid)
				break;
		/* Remember the first empty entry. */
		} else if (!empty) {
			empty = index + 1;
		}
	}

	/* no available entry */
	if (index == dev->num_statics && !empty)
		return;

	/* add entry */
	if (index == dev->num_statics) {
		index = empty - 1;
		memset(&alu, 0, sizeof(alu));
		memcpy(alu.mac, mdb->addr, ETH_ALEN);
		alu.is_static = true;
	}
	alu.port_forward |= BIT(port);
	if (mdb->vid) {
		alu.is_use_fid = true;

		/* Need a way to map VID to FID. */
		alu.fid = mdb->vid;
	}
	dev->dev_ops->w_sta_mac_table(dev, index, &alu);
}
EXPORT_SYMBOL_GPL(ksz_port_mdb_add);

int ksz_port_mdb_del(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_mdb *mdb)
{
	struct ksz_device *dev = ds->priv;
	struct alu_struct alu;
	int index;
	int ret = 0;

	for (index = 0; index < dev->num_statics; index++) {
		if (!dev->dev_ops->r_sta_mac_table(dev, index, &alu)) {
			/* Found one already in static MAC table. */
			if (!memcmp(alu.mac, mdb->addr, ETH_ALEN) &&
			    alu.fid == mdb->vid)
				break;
		}
	}

	/* no available entry */
	if (index == dev->num_statics)
		goto exit;

	/* clear port */
	alu.port_forward &= ~BIT(port);
	if (!alu.port_forward)
		alu.is_static = false;
	dev->dev_ops->w_sta_mac_table(dev, index, &alu);

exit:
	return ret;
}
EXPORT_SYMBOL_GPL(ksz_port_mdb_del);

int ksz_enable_port(struct dsa_switch *ds, int port, struct phy_device *phy)
{
	struct ksz_device *dev = ds->priv;

	if (!dsa_is_user_port(ds, port))
		return 0;

	/* setup slave port */
	dev->dev_ops->port_setup(dev, port, false);

	/* port_stp_state_set() will be called after to enable the port so
	 * there is no need to do anything.
	 */

	return 0;
}
EXPORT_SYMBOL_GPL(ksz_enable_port);

void ksz_disable_port(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;

	if (!dsa_is_user_port(ds, port))
		return;

	dev->on_ports &= ~(1 << port);
	dev->live_ports &= ~(1 << port);

	/* port_stp_state_set() will be called after to disable the port so
	 * there is no need to do anything.
	 */
}
EXPORT_SYMBOL_GPL(ksz_disable_port);

struct ksz_device *ksz_switch_alloc(struct device *base, void *priv)
{
	struct dsa_switch *ds;
	struct ksz_device *swdev;

	ds = devm_kzalloc(base, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return NULL;

	ds->dev = base;
	ds->num_ports = DSA_MAX_PORTS;

	swdev = devm_kzalloc(base, sizeof(*swdev), GFP_KERNEL);
	if (!swdev)
		return NULL;

	ds->priv = swdev;
	swdev->dev = base;

	swdev->ds = ds;
	swdev->priv = priv;

	return swdev;
}
EXPORT_SYMBOL(ksz_switch_alloc);

int ksz_switch_register(struct ksz_device *dev,
			const struct ksz_dev_ops *ops)
{
	phy_interface_t interface;
	int ret;

	if (dev->pdata)
		dev->chip_id = dev->pdata->chip_id;

	dev->reset_gpio = devm_gpiod_get_optional(dev->dev, "reset",
						  GPIOD_OUT_LOW);
	if (IS_ERR(dev->reset_gpio))
		return PTR_ERR(dev->reset_gpio);

	if (dev->reset_gpio) {
		gpiod_set_value_cansleep(dev->reset_gpio, 1);
		mdelay(10);
		gpiod_set_value_cansleep(dev->reset_gpio, 0);
	}

	mutex_init(&dev->dev_mutex);
	mutex_init(&dev->regmap_mutex);
	mutex_init(&dev->alu_mutex);
	mutex_init(&dev->vlan_mutex);

	dev->dev_ops = ops;

	if (dev->dev_ops->detect(dev))
		return -EINVAL;

	ret = dev->dev_ops->init(dev);
	if (ret)
		return ret;

	/* Host port interface will be self detected, or specifically set in
	 * device tree.
	 */
	if (dev->dev->of_node) {
		ret = of_get_phy_mode(dev->dev->of_node, &interface);
		if (ret == 0)
			dev->interface = interface;
		dev->synclko_125 = of_property_read_bool(dev->dev->of_node,
							 "microchip,synclko-125");
	}

	ret = dsa_register_switch(dev->ds);
	if (ret) {
		dev->dev_ops->exit(dev);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ksz_switch_register);

void ksz_switch_remove(struct ksz_device *dev)
{
	/* timer started */
	if (dev->mib_read_interval)
		cancel_delayed_work_sync(&dev->mib_read);

	dev->dev_ops->exit(dev);
	dsa_unregister_switch(dev->ds);

	if (dev->reset_gpio)
		gpiod_set_value_cansleep(dev->reset_gpio, 1);

}
EXPORT_SYMBOL(ksz_switch_remove);

MODULE_AUTHOR("Woojung Huh <Woojung.Huh@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ Series Switch DSA Driver");
MODULE_LICENSE("GPL");
