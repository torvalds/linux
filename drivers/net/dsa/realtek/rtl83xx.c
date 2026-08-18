// SPDX-License-Identifier: GPL-2.0+

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of_mdio.h>
#include <linux/if_bridge.h>
#include <linux/etherdevice.h>

#include "realtek.h"
#include "rtl83xx.h"

/**
 * rtl83xx_lock() - Locks the mutex used by regmaps
 * @ctx: realtek_priv pointer
 *
 * This function is passed to regmap to be used as the lock function.
 * It is also used externally to block regmap before executing multiple
 * operations that must happen in sequence (which will use
 * realtek_priv.map_nolock instead).
 *
 * Context: Can sleep. Holds priv->map_lock lock.
 * Return: nothing
 */
void rtl83xx_lock(void *ctx)
{
	struct realtek_priv *priv = ctx;

	mutex_lock(&priv->map_lock);
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_lock, "REALTEK_DSA");

/**
 * rtl83xx_unlock() - Unlocks the mutex used by regmaps
 * @ctx: realtek_priv pointer
 *
 * This function unlocks the lock acquired by rtl83xx_lock.
 *
 * Context: Releases priv->map_lock lock.
 * Return: nothing
 */
void rtl83xx_unlock(void *ctx)
{
	struct realtek_priv *priv = ctx;

	mutex_unlock(&priv->map_lock);
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_unlock, "REALTEK_DSA");

static int rtl83xx_user_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct realtek_priv *priv = bus->priv;

	return priv->ops->phy_read(priv, addr, regnum);
}

static int rtl83xx_user_mdio_write(struct mii_bus *bus, int addr, int regnum,
				   u16 val)
{
	struct realtek_priv *priv = bus->priv;

	return priv->ops->phy_write(priv, addr, regnum, val);
}

/**
 * rtl83xx_setup_user_mdio() - register the user mii bus driver
 * @ds: DSA switch associated with this user_mii_bus
 *
 * Registers the MDIO bus for built-in Ethernet PHYs, and associates it with
 * the mandatory 'mdio' child OF node of the switch.
 *
 * Context: Can sleep.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_setup_user_mdio(struct dsa_switch *ds)
{
	struct realtek_priv *priv = ds->priv;
	struct device_node *mdio_np;
	struct mii_bus *bus;
	int ret = 0;

	mdio_np = of_get_child_by_name(priv->dev->of_node, "mdio");
	if (!mdio_np) {
		dev_err(priv->dev, "no MDIO bus node\n");
		return -ENODEV;
	}

	bus = devm_mdiobus_alloc(priv->dev);
	if (!bus) {
		ret = -ENOMEM;
		goto err_put_node;
	}

	bus->priv = priv;
	bus->name = "Realtek user MII";
	bus->read = rtl83xx_user_mdio_read;
	bus->write = rtl83xx_user_mdio_write;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s:user_mii", dev_name(priv->dev));
	bus->parent = priv->dev;

	ret = devm_of_mdiobus_register(priv->dev, bus, mdio_np);
	if (ret) {
		dev_err(priv->dev, "unable to register MDIO bus %s\n",
			bus->id);
		goto err_put_node;
	}

	priv->user_mii_bus = bus;

err_put_node:
	of_node_put(mdio_np);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_setup_user_mdio, "REALTEK_DSA");

/**
 * rtl83xx_probe() - probe a Realtek switch
 * @dev: the device being probed
 * @interface_info: specific management interface info.
 *
 * This function initializes realtek_priv and reads data from the device tree
 * node. The switch is hard resetted if a method is provided.
 *
 * Context: Can sleep.
 * Return: Pointer to the realtek_priv or ERR_PTR() in case of failure.
 *
 * The realtek_priv pointer does not need to be freed as it is controlled by
 * devres.
 */
struct realtek_priv *
rtl83xx_probe(struct device *dev,
	      const struct realtek_interface_info *interface_info)
{
	const struct realtek_variant *var;
	struct realtek_priv *priv;
	struct regmap_config rc = {
		.reg_bits = 10, /* A4..A0 R4..R0 */
		.val_bits = 16,
		.reg_stride = 1,
		.max_register = 0xffff,
		.reg_format_endian = REGMAP_ENDIAN_BIG,
		.reg_read = interface_info->reg_read,
		.reg_write = interface_info->reg_write,
		.cache_type = REGCACHE_NONE,
		.lock = rtl83xx_lock,
		.unlock = rtl83xx_unlock,
	};
	int ret;

	var = of_device_get_match_data(dev);
	if (!var)
		return ERR_PTR(-EINVAL);

	priv = devm_kzalloc(dev, size_add(sizeof(*priv), var->chip_data_sz),
			    GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	mutex_init(&priv->map_lock);
	mutex_init(&priv->vlan_lock);
	mutex_init(&priv->l2_lock);

	rc.lock_arg = priv;
	priv->map = devm_regmap_init(dev, NULL, priv, &rc);
	if (IS_ERR(priv->map)) {
		ret = PTR_ERR(priv->map);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ERR_PTR(ret);
	}

	rc.disable_locking = true;
	priv->map_nolock = devm_regmap_init(dev, NULL, priv, &rc);
	if (IS_ERR(priv->map_nolock)) {
		ret = PTR_ERR(priv->map_nolock);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ERR_PTR(ret);
	}

	/* Link forward and backward */
	priv->dev = dev;
	priv->variant = var;
	priv->ops = var->ops;
	priv->chip_data = (void *)priv + sizeof(*priv);

	spin_lock_init(&priv->lock);

	priv->leds_disabled = of_property_read_bool(dev->of_node,
						    "realtek,disable-leds");

	/* TODO: if power is software controlled, set up any regulators here */
	priv->reset_ctl = devm_reset_control_get_optional(dev, NULL);
	if (IS_ERR(priv->reset_ctl))
		return dev_err_cast_probe(dev, priv->reset_ctl,
					  "failed to get reset control\n");

	priv->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset)) {
		dev_err(dev, "failed to get RESET GPIO\n");
		return ERR_CAST(priv->reset);
	}

	dev_set_drvdata(dev, priv);

	if (priv->reset_ctl || priv->reset) {
		rtl83xx_reset_assert(priv);
		dev_dbg(dev, "asserted RESET\n");
		msleep(REALTEK_HW_STOP_DELAY);
		rtl83xx_reset_deassert(priv);
		msleep(REALTEK_HW_START_DELAY);
		dev_dbg(dev, "deasserted RESET\n");
	}

	return priv;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_probe, "REALTEK_DSA");

/**
 * rtl83xx_register_switch() - detects and register a switch
 * @priv: realtek_priv pointer
 *
 * This function first checks the switch chip ID and register a DSA
 * switch.
 *
 * Context: Can sleep. Takes and releases priv->map_lock.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_register_switch(struct realtek_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	int ret;

	ret = priv->ops->detect(priv);
	if (ret) {
		dev_err_probe(priv->dev, ret, "unable to detect switch\n");
		return ret;
	}

	ds->priv = priv;
	ds->dev = priv->dev;
	ds->ops = priv->variant->ds_ops;
	ds->phylink_mac_ops = priv->variant->phylink_mac_ops;
	ds->num_ports = priv->num_ports;

	ret = dsa_register_switch(ds);
	if (ret) {
		dev_err_probe(priv->dev, ret, "unable to register switch\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_register_switch, "REALTEK_DSA");

/**
 * rtl83xx_unregister_switch() - unregister a switch
 * @priv: realtek_priv pointer
 *
 * This function unregister a DSA switch.
 *
 * Context: Can sleep.
 * Return: Nothing.
 */
void rtl83xx_unregister_switch(struct realtek_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;

	dsa_unregister_switch(ds);
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_unregister_switch, "REALTEK_DSA");

/**
 * rtl83xx_shutdown() - shutdown a switch
 * @priv: realtek_priv pointer
 *
 * This function shuts down the DSA switch and cleans the platform driver data,
 * to prevent realtek_{smi,mdio}_remove() from running afterwards, which is
 * possible if the parent bus implements its own .shutdown() as .remove().
 *
 * Context: Can sleep.
 * Return: Nothing.
 */
void rtl83xx_shutdown(struct realtek_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;

	dsa_switch_shutdown(ds);

	dev_set_drvdata(priv->dev, NULL);
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_shutdown, "REALTEK_DSA");

/**
 * rtl83xx_remove() - Cleanup a realtek switch driver
 * @priv: realtek_priv pointer
 *
 * Placehold for common cleanup procedures.
 *
 * Context: Any
 * Return: nothing
 */
void rtl83xx_remove(struct realtek_priv *priv)
{
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_remove, "REALTEK_DSA");

void rtl83xx_reset_assert(struct realtek_priv *priv)
{
	int ret;

	ret = reset_control_assert(priv->reset_ctl);
	if (ret)
		dev_warn(priv->dev,
			 "Failed to assert the switch reset control: %pe\n",
			 ERR_PTR(ret));

	gpiod_set_value(priv->reset, true);
}

void rtl83xx_reset_deassert(struct realtek_priv *priv)
{
	int ret;

	ret = reset_control_deassert(priv->reset_ctl);
	if (ret)
		dev_warn(priv->dev,
			 "Failed to deassert the switch reset control: %pe\n",
			 ERR_PTR(ret));

	gpiod_set_value(priv->reset, false);
}

/**
 * rtl83xx_port_bridge_join() - join a port to a bridge
 * @ds: DSA switch instance
 * @port: port index
 * @bridge: bridge being joined
 * @tx_forward_offload: if the switch can offload TX forwarding
 * @extack: netlink extended ack for reporting errors
 *
 * This function handles joining a port to a bridge. It updates the port
 * isolation masks and EFID.
 *
 * Context: Can sleep.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_port_bridge_join(struct dsa_switch *ds, int port,
			     struct dsa_bridge bridge,
			     bool *tx_forward_offload,
			     struct netlink_ext_ack *extack)
{
	struct realtek_priv *priv = ds->priv;
	struct dsa_port *dp;
	u32 mask = 0;
	int ret;

	if (!priv->ops->port_add_isolation)
		return -EOPNOTSUPP;

	if (!priv->ops->port_set_learning)
		return -EOPNOTSUPP;

	dev_dbg(priv->dev, "bridge %d join port %d\n", bridge.num, port);

	/* Add this port to the isolation group of every other port
	 * offloading this bridge.
	 */
	dsa_switch_for_each_user_port(dp, ds) {
		/* Handle this port after */
		if (dp->index == port)
			continue;

		/* Skip ports that are not in this bridge */
		if (!dsa_port_offloads_bridge(dp, &bridge))
			continue;

		ret = priv->ops->port_add_isolation(priv, dp->index, BIT(port));
		if (ret)
			goto undo_isolation;

		mask |= BIT(dp->index);
	}

	/* If we support cascade switches, it should also include the
	 * downstream DSA ports to the isolation group.
	 */

	/* Add those ports to the isolation group of this port */
	ret = priv->ops->port_add_isolation(priv, port, mask);
	if (ret)
		goto undo_isolation;

	/* Use the bridge number as the EFID for this port */
	if (priv->ops->port_set_efid) {
		ret = priv->ops->port_set_efid(priv, port, bridge.num);
		if (ret)
			goto undo_self_isolation;
	}

	ret = priv->ops->port_set_learning(priv, port, true);
	if (ret)
		goto undo_efid;

	return 0;

undo_efid:
	if (priv->ops->port_set_efid)
		priv->ops->port_set_efid(priv, port, 0);

undo_self_isolation:
	priv->ops->port_remove_isolation(priv, port, mask);

undo_isolation:
	dsa_switch_for_each_port(dp, ds) {
		if (mask & BIT(dp->index))
			priv->ops->port_remove_isolation(priv, dp->index,
							 BIT(port));
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_port_bridge_join, "REALTEK_DSA");

/**
 * rtl83xx_port_bridge_leave() - leave a bridge
 * @ds: DSA switch instance
 * @port: port index
 * @bridge: bridge being left
 *
 * This function handles removing a port from a bridge. It updates the port
 * isolation masks and EFID.
 *
 * Context: Can sleep.
 * Return: nothing
 */
void rtl83xx_port_bridge_leave(struct dsa_switch *ds, int port,
			       struct dsa_bridge bridge)
{
	struct realtek_priv *priv = ds->priv;
	struct dsa_port *dp;
	u32 mask = 0;
	int ret;

	if (!priv->ops->port_remove_isolation)
		return;

	if (!priv->ops->port_set_learning)
		return;

	dev_dbg(priv->dev, "bridge %d leave port %d\n", bridge.num, port);

	/* Remove this port from the isolation group of every other
	 * port offloading this bridge.
	 */
	dsa_switch_for_each_user_port(dp, ds) {
		/* Handle this port after */
		if (dp->index == port)
			continue;

		/* Skip ports that are not in this bridge */
		if (!dsa_port_offloads_bridge(dp, &bridge))
			continue;

		ret = priv->ops->port_remove_isolation(priv, dp->index,
						       BIT(port));
		if (ret)
			dev_err(priv->dev,
				"failed to isolate port %d from port %d: %pe\n",
				port, dp->index, ERR_PTR(ret));

		mask |= BIT(dp->index);
	}

	/* If we support cascade switches, it should also exclude the
	 * downstream DSA ports from the isolation group.
	 */

	ret = priv->ops->port_set_learning(priv, port, false);
	if (ret)
		dev_err(priv->dev,
			"failed to disable learning on port %d: %pe\n",
			port, ERR_PTR(ret));

	/* Remove those ports from the isolation group of this port */
	ret = priv->ops->port_remove_isolation(priv, port, mask);
	if (ret)
		dev_err(priv->dev,
			"failed to remove isolation mask from port %d: %pe\n",
			port, ERR_PTR(ret));

	/* Revert to the default EFID 0 for standalone mode */
	if (priv->ops->port_set_efid) {
		ret = priv->ops->port_set_efid(priv, port, 0);
		if (ret)
			dev_err(priv->dev,
				"failed to clear EFID on port %d: %pe\n",
				port, ERR_PTR(ret));
	}
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_port_bridge_leave, "REALTEK_DSA");

/**
 * rtl83xx_port_fast_age() - flush dynamic FDB entries learned on a port
 * @ds: DSA switch instance
 * @port: port index
 *
 * This function requests the switch to age out dynamic FDB entries learned on
 * @port.
 *
 * Context: Can sleep.
 * Return: Nothing.
 */
void rtl83xx_port_fast_age(struct dsa_switch *ds, int port)
{
	struct realtek_priv *priv = ds->priv;
	int ret;

	if (!priv->ops->l2_flush) {
		dev_warn_once(priv->dev, "l2_flush op not defined\n");
		return;
	}

	dev_dbg(priv->dev, "fast_age port %d\n", port);

	mutex_lock(&priv->l2_lock);
	ret = priv->ops->l2_flush(priv, port, 0);
	mutex_unlock(&priv->l2_lock);
	if (ret)
		dev_err(priv->dev, "failed to fast age on port %d: %d\n", port,
			ret);
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_port_fast_age, "REALTEK_DSA");

/**
 * rtl83xx_port_fdb_add() - add a static FDB entry to a port database
 * @ds: DSA switch instance
 * @port: port index
 * @addr: MAC address to add
 * @vid: VLAN ID associated with @addr
 * @db: database where the entry should be added
 *
 * This function adds a static unicast FDB entry to the standalone port
 * database or to a bridge database.
 *
 * Context: Can sleep.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_port_fdb_add(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid,
			 struct dsa_db db)
{
	struct realtek_priv *priv = ds->priv;
	int efid;
	int ret;

	if (is_multicast_ether_addr(addr))
		return -EOPNOTSUPP;

	if (!priv->ops->l2_add_uc)
		return -EOPNOTSUPP;

	if (db.type != DSA_DB_PORT && db.type != DSA_DB_BRIDGE)
		return -EOPNOTSUPP;

	/* Bridge ports use bridge.num as EFID, while standalone ports use
	 * EFID 0. FDB entries for the CPU port follow the bridge EFID due
	 * to assisted learning.
	 */
	efid = db.type == DSA_DB_BRIDGE ? db.bridge.num : 0;

	dev_dbg(priv->dev, "%s: port:%d addr:%pM efid:%d vid:%d dbtype:%d\n",
		__func__, port, addr, efid, vid, db.type);

	mutex_lock(&priv->l2_lock);
	ret = priv->ops->l2_add_uc(priv, port, addr, efid, vid);

	mutex_unlock(&priv->l2_lock);

	if (ret)
		dev_err(priv->dev, "fdb_add ERROR %pe\n", ERR_PTR(ret));
	return ret;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_port_fdb_add, "REALTEK_DSA");

/**
 * rtl83xx_port_fdb_del() - delete a static FDB entry from a port database
 * @ds: DSA switch instance
 * @port: port index
 * @addr: MAC address to delete
 * @vid: VLAN ID associated with @addr
 * @db: database where the entry should be removed
 *
 * This function deletes a static unicast FDB entry from the standalone port
 * database or from a bridge database.
 *
 * Context: Can sleep.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_port_fdb_del(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid,
			 struct dsa_db db)
{
	struct realtek_priv *priv = ds->priv;
	int efid;
	int ret;

	if (is_multicast_ether_addr(addr))
		return -EOPNOTSUPP;

	if (!priv->ops->l2_del_uc)
		return -EOPNOTSUPP;

	if (db.type != DSA_DB_PORT && db.type != DSA_DB_BRIDGE)
		return -EOPNOTSUPP;

	/*
	 * DSA_DB_BRIDGE ports use bridge number [1..N] as EFID, while
	 * DSA_DB_PORT use the default EFID (0), not used by any bridge.
	 */
	efid = db.type == DSA_DB_BRIDGE ? db.bridge.num : 0;

	dev_dbg(priv->dev, "%s: port:%d addr:%pM efid:%d vid:%d dbtype:%d\n",
		__func__, port, addr, efid, vid, db.type);

	mutex_lock(&priv->l2_lock);
	ret = priv->ops->l2_del_uc(priv, port, addr, efid, vid);
	mutex_unlock(&priv->l2_lock);

	if (ret)
		dev_err(priv->dev, "fdb_del ERROR %pe\n", ERR_PTR(ret));
	return ret;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_port_fdb_del, "REALTEK_DSA");

/**
 * rtl83xx_port_fdb_dump() - iterate over FDB entries associated with a port
 * @ds: DSA switch instance
 * @port: port index
 * @cb: callback invoked for each entry
 * @data: opaque pointer passed to @cb
 *
 * This function walks the unicast FDB entries associated with @port and calls
 * @cb for each matching entry.
 *
 * Context: Can sleep.
 * Return: 0 on success, or negative value for failure.
 */
int rtl83xx_port_fdb_dump(struct dsa_switch *ds, int port,
			  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct realtek_fdb_entry entry = { 0 };
	struct realtek_priv *priv = ds->priv;
	u16 start_addr, addr = 0;
	int ret = 0;

	if (!priv->ops->l2_get_next_uc)
		return -EOPNOTSUPP;

	mutex_lock(&priv->l2_lock);
	while (true) {
		start_addr = addr;

		dev_dbg(priv->dev, "l2_get_next_uc, addr:%d, port:%d\n",
			addr, port);
		ret = priv->ops->l2_get_next_uc(priv, &addr, port, &entry);
		dev_dbg(priv->dev,
			"%s addr:%d mac:%pM vid:%d static:%d ret:%pe\n",
			__func__, addr, entry.mac_addr, entry.vid,
			entry.is_static, ERR_PTR(ret));

		if (ret == -ENOENT) {
			/* If the table is empty, returns without errors. Note
			 * that the l2_get_next_uc overflow to the first match
			 * when it reaches the end of the table.
			 */
			ret = 0;
			break;
		}

		if (ret)
			break;

		/* When the addr returned is before the requested one, it
		 * indicates that we reached the end.
		 */
		if (addr < start_addr)
			break;

		ret = cb(entry.mac_addr, entry.vid, entry.is_static, data);
		if (ret)
			break;

		addr++;
	}
	mutex_unlock(&priv->l2_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_port_fdb_dump, "REALTEK_DSA");

/**
 * rtl83xx_port_mdb_add() - add a multicast database entry to a port database
 * @ds: DSA switch instance
 * @port: port index
 * @mdb: multicast database entry to add
 * @db: database where the entry should be added
 *
 * This function adds a multicast database entry to the standalone port
 * database or to a bridge database.
 *
 * Context: Can sleep.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_port_mdb_add(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb,
			 struct dsa_db db)
{
	struct realtek_priv *priv = ds->priv;
	const unsigned char *addr = mdb->addr;
	u16 vid = mdb->vid;
	int efid;
	int ret;

	if (!priv->ops->l2_add_mc)
		return -EOPNOTSUPP;

	if (db.type != DSA_DB_PORT && db.type != DSA_DB_BRIDGE)
		return -EOPNOTSUPP;

	/* EFID is not used by hardware MDB entries; debugging only */
	efid = db.type == DSA_DB_BRIDGE ? db.bridge.num : 0;

	dev_dbg(priv->dev, "%s: port:%d addr:%pM efid:%d vid:%d dbtype:%d\n",
		__func__, port, addr, efid, vid, db.type);

	mutex_lock(&priv->l2_lock);
	ret = priv->ops->l2_add_mc(priv, port, addr, vid);
	mutex_unlock(&priv->l2_lock);

	if (ret)
		dev_err(priv->dev, "mdb_add ERROR %pe\n", ERR_PTR(ret));
	return ret;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_port_mdb_add, "REALTEK_DSA");

/**
 * rtl83xx_port_mdb_del() - delete a multicast database entry from a port
 * database
 * @ds: DSA switch instance
 * @port: port index
 * @mdb: multicast database entry to delete
 * @db: database where the entry should be removed
 *
 * This function deletes a multicast database entry from the standalone port
 * database or from a bridge database.
 *
 * Context: Can sleep.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_port_mdb_del(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb,
			 struct dsa_db db)
{
	struct realtek_priv *priv = ds->priv;
	const unsigned char *addr = mdb->addr;
	u16 vid = mdb->vid;
	int efid;
	int ret;

	if (!priv->ops->l2_del_mc)
		return -EOPNOTSUPP;

	if (db.type != DSA_DB_PORT && db.type != DSA_DB_BRIDGE)
		return -EOPNOTSUPP;

	/* EFID is not used by hardware MDB entries; debugging only */
	efid = db.type == DSA_DB_BRIDGE ? db.bridge.num : 0;

	dev_dbg(priv->dev, "%s: port:%d addr:%pM efid:%d vid:%d dbtype:%d\n",
		__func__, port, addr, efid, vid, db.type);

	mutex_lock(&priv->l2_lock);
	ret = priv->ops->l2_del_mc(priv, port, addr, vid);
	mutex_unlock(&priv->l2_lock);

	if (ret)
		dev_err(priv->dev, "mdb_del ERROR %pe\n", ERR_PTR(ret));
	return ret;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_port_mdb_del, "REALTEK_DSA");

/**
 * rtl83xx_port_bridge_flags() - set port bridge flags
 * @ds: DSA switch instance
 * @port: port index
 * @flags: bridge port flags
 * @extack: netlink extended ack for reporting errors
 *
 * This function handles setting bridge port flags like learning and flooding.
 *
 * Context: Can sleep.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_port_bridge_flags(struct dsa_switch *ds, int port,
			      struct switchdev_brport_flags flags,
			      struct netlink_ext_ack *extack)
{
	struct realtek_priv *priv = ds->priv;
	bool enable;
	int ret;

	if (flags.mask & BR_LEARNING) {
		if (!priv->ops->port_set_learning)
			return -EOPNOTSUPP;

		enable = !!(flags.val & BR_LEARNING);
		ret = priv->ops->port_set_learning(priv, port, enable);
		if (ret)
			return ret;
	}

	if (flags.mask & BR_FLOOD) {
		if (!priv->ops->port_set_ucast_flood)
			return -EOPNOTSUPP;

		enable = !!(flags.val & BR_FLOOD);
		ret = priv->ops->port_set_ucast_flood(priv, port, enable);
		if (ret)
			return ret;
	}

	if (flags.mask & BR_MCAST_FLOOD) {
		if (!priv->ops->port_set_mcast_flood)
			return -EOPNOTSUPP;

		enable = !!(flags.val & BR_MCAST_FLOOD);
		ret = priv->ops->port_set_mcast_flood(priv, port, enable);
		if (ret)
			return ret;
	}

	if (flags.mask & BR_BCAST_FLOOD) {
		if (!priv->ops->port_set_bcast_flood)
			return -EOPNOTSUPP;

		enable = !!(flags.val & BR_BCAST_FLOOD);
		ret = priv->ops->port_set_bcast_flood(priv, port, enable);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_port_bridge_flags, "REALTEK_DSA");

/**
 * rtl83xx_setup_port_flood_control() - setup default flood control for a port
 * @priv: realtek_priv pointer
 * @port: port index
 *
 * This function enables flooding for a given port.
 *
 * Context: Can sleep.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_setup_port_flood_control(struct realtek_priv *priv, int port)
{
	int ret;

	if (priv->ops->port_set_ucast_flood) {
		ret = priv->ops->port_set_ucast_flood(priv, port, true);
		if (ret)
			return ret;
	}

	if (priv->ops->port_set_mcast_flood) {
		ret = priv->ops->port_set_mcast_flood(priv, port, true);
		if (ret)
			return ret;
	}

	if (priv->ops->port_set_bcast_flood) {
		ret = priv->ops->port_set_bcast_flood(priv, port, true);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_setup_port_flood_control, "REALTEK_DSA");

MODULE_AUTHOR("Luiz Angelo Daros de Luca <luizluca@gmail.com>");
MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Realtek DSA switches common module");
MODULE_LICENSE("GPL");
