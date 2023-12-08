// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved */

#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <net/netevent.h>
#include <net/switchdev.h>

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_switchdev.h"

#define PRESTERA_VID_ALL (0xffff)

#define PRESTERA_DEFAULT_AGEING_TIME_MS 300000
#define PRESTERA_MAX_AGEING_TIME_MS 1000000000
#define PRESTERA_MIN_AGEING_TIME_MS 32000

struct prestera_fdb_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *dev;
	unsigned long event;
};

struct prestera_switchdev {
	struct prestera_switch *sw;
	struct list_head bridge_list;
	bool bridge_8021q_exists;
	struct notifier_block swdev_nb_blk;
	struct notifier_block swdev_nb;
};

struct prestera_bridge {
	struct list_head head;
	struct net_device *dev;
	struct prestera_switchdev *swdev;
	struct list_head port_list;
	struct list_head br_mdb_entry_list;
	bool mrouter_exist;
	bool vlan_enabled;
	bool multicast_enabled;
	u16 bridge_id;
};

struct prestera_bridge_port {
	struct list_head head;
	struct net_device *dev;
	struct prestera_bridge *bridge;
	struct list_head vlan_list;
	struct list_head br_mdb_port_list;
	refcount_t ref_count;
	unsigned long flags;
	bool mrouter;
	u8 stp_state;
};

struct prestera_bridge_vlan {
	struct list_head head;
	struct list_head port_vlan_list;
	u16 vid;
};

struct prestera_port_vlan {
	struct list_head br_vlan_head;
	struct list_head port_head;
	struct prestera_port *port;
	struct prestera_bridge_port *br_port;
	u16 vid;
};

struct prestera_br_mdb_port {
	struct prestera_bridge_port *br_port;
	struct list_head br_mdb_port_node;
};

/* Software representation of MDB table. */
struct prestera_br_mdb_entry {
	struct prestera_bridge *bridge;
	struct prestera_mdb_entry *mdb;
	struct list_head br_mdb_port_list;
	struct list_head br_mdb_entry_node;
	bool enabled;
};

static struct workqueue_struct *swdev_wq;

static void prestera_bridge_port_put(struct prestera_bridge_port *br_port);

static int prestera_port_vid_stp_set(struct prestera_port *port, u16 vid,
				     u8 state);

static struct prestera_bridge *
prestera_bridge_find(const struct prestera_switch *sw,
		     const struct net_device *br_dev)
{
	struct prestera_bridge *bridge;

	list_for_each_entry(bridge, &sw->swdev->bridge_list, head)
		if (bridge->dev == br_dev)
			return bridge;

	return NULL;
}

static struct prestera_bridge_port *
__prestera_bridge_port_find(const struct prestera_bridge *bridge,
			    const struct net_device *brport_dev)
{
	struct prestera_bridge_port *br_port;

	list_for_each_entry(br_port, &bridge->port_list, head)
		if (br_port->dev == brport_dev)
			return br_port;

	return NULL;
}

static struct prestera_bridge_port *
prestera_bridge_port_find(struct prestera_switch *sw,
			  struct net_device *brport_dev)
{
	struct net_device *br_dev = netdev_master_upper_dev_get(brport_dev);
	struct prestera_bridge *bridge;

	if (!br_dev)
		return NULL;

	bridge = prestera_bridge_find(sw, br_dev);
	if (!bridge)
		return NULL;

	return __prestera_bridge_port_find(bridge, brport_dev);
}

static void
prestera_br_port_flags_reset(struct prestera_bridge_port *br_port,
			     struct prestera_port *port)
{
	prestera_port_uc_flood_set(port, false);
	prestera_port_mc_flood_set(port, false);
	prestera_port_learning_set(port, false);
	prestera_port_br_locked_set(port, false);
}

static int prestera_br_port_flags_set(struct prestera_bridge_port *br_port,
				      struct prestera_port *port)
{
	int err;

	err = prestera_port_uc_flood_set(port, br_port->flags & BR_FLOOD);
	if (err)
		goto err_out;

	err = prestera_port_mc_flood_set(port, br_port->flags & BR_MCAST_FLOOD);
	if (err)
		goto err_out;

	err = prestera_port_learning_set(port, br_port->flags & BR_LEARNING);
	if (err)
		goto err_out;

	err = prestera_port_br_locked_set(port,
					  br_port->flags & BR_PORT_LOCKED);
	if (err)
		goto err_out;

	return 0;

err_out:
	prestera_br_port_flags_reset(br_port, port);
	return err;
}

static struct prestera_bridge_vlan *
prestera_bridge_vlan_create(struct prestera_bridge_port *br_port, u16 vid)
{
	struct prestera_bridge_vlan *br_vlan;

	br_vlan = kzalloc(sizeof(*br_vlan), GFP_KERNEL);
	if (!br_vlan)
		return NULL;

	INIT_LIST_HEAD(&br_vlan->port_vlan_list);
	br_vlan->vid = vid;
	list_add(&br_vlan->head, &br_port->vlan_list);

	return br_vlan;
}

static void prestera_bridge_vlan_destroy(struct prestera_bridge_vlan *br_vlan)
{
	list_del(&br_vlan->head);
	WARN_ON(!list_empty(&br_vlan->port_vlan_list));
	kfree(br_vlan);
}

static struct prestera_bridge_vlan *
prestera_bridge_vlan_by_vid(struct prestera_bridge_port *br_port, u16 vid)
{
	struct prestera_bridge_vlan *br_vlan;

	list_for_each_entry(br_vlan, &br_port->vlan_list, head) {
		if (br_vlan->vid == vid)
			return br_vlan;
	}

	return NULL;
}

static int prestera_bridge_vlan_port_count(struct prestera_bridge *bridge,
					   u16 vid)
{
	struct prestera_bridge_port *br_port;
	struct prestera_bridge_vlan *br_vlan;
	int count = 0;

	list_for_each_entry(br_port, &bridge->port_list, head) {
		list_for_each_entry(br_vlan, &br_port->vlan_list, head) {
			if (br_vlan->vid == vid) {
				count += 1;
				break;
			}
		}
	}

	return count;
}

static void prestera_bridge_vlan_put(struct prestera_bridge_vlan *br_vlan)
{
	if (list_empty(&br_vlan->port_vlan_list))
		prestera_bridge_vlan_destroy(br_vlan);
}

static struct prestera_port_vlan *
prestera_port_vlan_by_vid(struct prestera_port *port, u16 vid)
{
	struct prestera_port_vlan *port_vlan;

	list_for_each_entry(port_vlan, &port->vlans_list, port_head) {
		if (port_vlan->vid == vid)
			return port_vlan;
	}

	return NULL;
}

static struct prestera_port_vlan *
prestera_port_vlan_create(struct prestera_port *port, u16 vid, bool untagged)
{
	struct prestera_port_vlan *port_vlan;
	int err;

	port_vlan = prestera_port_vlan_by_vid(port, vid);
	if (port_vlan)
		return ERR_PTR(-EEXIST);

	err = prestera_hw_vlan_port_set(port, vid, true, untagged);
	if (err)
		return ERR_PTR(err);

	port_vlan = kzalloc(sizeof(*port_vlan), GFP_KERNEL);
	if (!port_vlan) {
		err = -ENOMEM;
		goto err_port_vlan_alloc;
	}

	port_vlan->port = port;
	port_vlan->vid = vid;

	list_add(&port_vlan->port_head, &port->vlans_list);

	return port_vlan;

err_port_vlan_alloc:
	prestera_hw_vlan_port_set(port, vid, false, false);
	return ERR_PTR(err);
}

static int prestera_fdb_add(struct prestera_port *port,
			    const unsigned char *mac, u16 vid, bool dynamic)
{
	if (prestera_port_is_lag_member(port))
		return prestera_hw_lag_fdb_add(port->sw, prestera_port_lag_id(port),
					      mac, vid, dynamic);

	return prestera_hw_fdb_add(port, mac, vid, dynamic);
}

static int prestera_fdb_del(struct prestera_port *port,
			    const unsigned char *mac, u16 vid)
{
	if (prestera_port_is_lag_member(port))
		return prestera_hw_lag_fdb_del(port->sw, prestera_port_lag_id(port),
					      mac, vid);
	else
		return prestera_hw_fdb_del(port, mac, vid);
}

static int prestera_fdb_flush_port_vlan(struct prestera_port *port, u16 vid,
					u32 mode)
{
	if (prestera_port_is_lag_member(port))
		return prestera_hw_fdb_flush_lag_vlan(port->sw, prestera_port_lag_id(port),
						      vid, mode);
	else
		return prestera_hw_fdb_flush_port_vlan(port, vid, mode);
}

static int prestera_fdb_flush_port(struct prestera_port *port, u32 mode)
{
	if (prestera_port_is_lag_member(port))
		return prestera_hw_fdb_flush_lag(port->sw, prestera_port_lag_id(port),
						 mode);
	else
		return prestera_hw_fdb_flush_port(port, mode);
}

static void
prestera_mdb_port_del(struct prestera_mdb_entry *mdb,
		      struct net_device *orig_dev)
{
	struct prestera_flood_domain *fl_domain = mdb->flood_domain;
	struct prestera_flood_domain_port *flood_domain_port;

	flood_domain_port = prestera_flood_domain_port_find(fl_domain,
							    orig_dev,
							    mdb->vid);
	if (flood_domain_port)
		prestera_flood_domain_port_destroy(flood_domain_port);
}

static void
prestera_br_mdb_entry_put(struct prestera_br_mdb_entry *br_mdb)
{
	struct prestera_bridge_port *br_port;

	if (list_empty(&br_mdb->br_mdb_port_list)) {
		list_for_each_entry(br_port, &br_mdb->bridge->port_list, head)
			prestera_mdb_port_del(br_mdb->mdb, br_port->dev);

		prestera_mdb_entry_destroy(br_mdb->mdb);
		list_del(&br_mdb->br_mdb_entry_node);
		kfree(br_mdb);
	}
}

static void
prestera_br_mdb_port_del(struct prestera_br_mdb_entry *br_mdb,
			 struct prestera_bridge_port *br_port)
{
	struct prestera_br_mdb_port *br_mdb_port, *tmp;

	list_for_each_entry_safe(br_mdb_port, tmp, &br_mdb->br_mdb_port_list,
				 br_mdb_port_node) {
		if (br_mdb_port->br_port == br_port) {
			list_del(&br_mdb_port->br_mdb_port_node);
			kfree(br_mdb_port);
		}
	}
}

static void
prestera_mdb_flush_bridge_port(struct prestera_bridge_port *br_port)
{
	struct prestera_br_mdb_port *br_mdb_port, *tmp_port;
	struct prestera_br_mdb_entry *br_mdb, *br_mdb_tmp;
	struct prestera_bridge *br_dev = br_port->bridge;

	list_for_each_entry_safe(br_mdb, br_mdb_tmp, &br_dev->br_mdb_entry_list,
				 br_mdb_entry_node) {
		list_for_each_entry_safe(br_mdb_port, tmp_port,
					 &br_mdb->br_mdb_port_list,
					 br_mdb_port_node) {
			prestera_mdb_port_del(br_mdb->mdb,
					      br_mdb_port->br_port->dev);
			prestera_br_mdb_port_del(br_mdb,  br_mdb_port->br_port);
		}
		prestera_br_mdb_entry_put(br_mdb);
	}
}

static void
prestera_port_vlan_bridge_leave(struct prestera_port_vlan *port_vlan)
{
	u32 fdb_flush_mode = PRESTERA_FDB_FLUSH_MODE_DYNAMIC;
	struct prestera_port *port = port_vlan->port;
	struct prestera_bridge_vlan *br_vlan;
	struct prestera_bridge_port *br_port;
	bool last_port, last_vlan;
	u16 vid = port_vlan->vid;
	int port_count;

	br_port = port_vlan->br_port;
	port_count = prestera_bridge_vlan_port_count(br_port->bridge, vid);
	br_vlan = prestera_bridge_vlan_by_vid(br_port, vid);

	last_vlan = list_is_singular(&br_port->vlan_list);
	last_port = port_count == 1;

	if (last_vlan)
		prestera_fdb_flush_port(port, fdb_flush_mode);
	else if (last_port)
		prestera_hw_fdb_flush_vlan(port->sw, vid, fdb_flush_mode);
	else
		prestera_fdb_flush_port_vlan(port, vid, fdb_flush_mode);

	prestera_mdb_flush_bridge_port(br_port);

	list_del(&port_vlan->br_vlan_head);
	prestera_bridge_vlan_put(br_vlan);
	prestera_bridge_port_put(br_port);
	port_vlan->br_port = NULL;
}

static void prestera_port_vlan_destroy(struct prestera_port_vlan *port_vlan)
{
	struct prestera_port *port = port_vlan->port;
	u16 vid = port_vlan->vid;

	if (port_vlan->br_port)
		prestera_port_vlan_bridge_leave(port_vlan);

	prestera_hw_vlan_port_set(port, vid, false, false);
	list_del(&port_vlan->port_head);
	kfree(port_vlan);
}

static struct prestera_bridge *
prestera_bridge_create(struct prestera_switchdev *swdev, struct net_device *dev)
{
	bool vlan_enabled = br_vlan_enabled(dev);
	struct prestera_bridge *bridge;
	u16 bridge_id;
	int err;

	if (vlan_enabled && swdev->bridge_8021q_exists) {
		netdev_err(dev, "Only one VLAN-aware bridge is supported\n");
		return ERR_PTR(-EINVAL);
	}

	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return ERR_PTR(-ENOMEM);

	if (vlan_enabled) {
		swdev->bridge_8021q_exists = true;
	} else {
		err = prestera_hw_bridge_create(swdev->sw, &bridge_id);
		if (err) {
			kfree(bridge);
			return ERR_PTR(err);
		}

		bridge->bridge_id = bridge_id;
	}

	bridge->vlan_enabled = vlan_enabled;
	bridge->swdev = swdev;
	bridge->dev = dev;
	bridge->multicast_enabled = br_multicast_enabled(dev);

	INIT_LIST_HEAD(&bridge->port_list);
	INIT_LIST_HEAD(&bridge->br_mdb_entry_list);

	list_add(&bridge->head, &swdev->bridge_list);

	return bridge;
}

static void prestera_bridge_destroy(struct prestera_bridge *bridge)
{
	struct prestera_switchdev *swdev = bridge->swdev;

	list_del(&bridge->head);

	if (bridge->vlan_enabled)
		swdev->bridge_8021q_exists = false;
	else
		prestera_hw_bridge_delete(swdev->sw, bridge->bridge_id);

	WARN_ON(!list_empty(&bridge->br_mdb_entry_list));
	WARN_ON(!list_empty(&bridge->port_list));
	kfree(bridge);
}

static void prestera_bridge_put(struct prestera_bridge *bridge)
{
	if (list_empty(&bridge->port_list))
		prestera_bridge_destroy(bridge);
}

static
struct prestera_bridge *prestera_bridge_by_dev(struct prestera_switchdev *swdev,
					       const struct net_device *dev)
{
	struct prestera_bridge *bridge;

	list_for_each_entry(bridge, &swdev->bridge_list, head)
		if (bridge->dev == dev)
			return bridge;

	return NULL;
}

static struct prestera_bridge_port *
__prestera_bridge_port_by_dev(struct prestera_bridge *bridge,
			      struct net_device *dev)
{
	struct prestera_bridge_port *br_port;

	list_for_each_entry(br_port, &bridge->port_list, head) {
		if (br_port->dev == dev)
			return br_port;
	}

	return NULL;
}

static int prestera_match_upper_bridge_dev(struct net_device *dev,
					   struct netdev_nested_priv *priv)
{
	if (netif_is_bridge_master(dev))
		priv->data = dev;

	return 0;
}

static struct net_device *prestera_get_upper_bridge_dev(struct net_device *dev)
{
	struct netdev_nested_priv priv = { };

	netdev_walk_all_upper_dev_rcu(dev, prestera_match_upper_bridge_dev,
				      &priv);
	return priv.data;
}

static struct prestera_bridge_port *
prestera_bridge_port_by_dev(struct prestera_switchdev *swdev,
			    struct net_device *dev)
{
	struct net_device *br_dev = prestera_get_upper_bridge_dev(dev);
	struct prestera_bridge *bridge;

	if (!br_dev)
		return NULL;

	bridge = prestera_bridge_by_dev(swdev, br_dev);
	if (!bridge)
		return NULL;

	return __prestera_bridge_port_by_dev(bridge, dev);
}

static struct prestera_bridge_port *
prestera_bridge_port_create(struct prestera_bridge *bridge,
			    struct net_device *dev)
{
	struct prestera_bridge_port *br_port;

	br_port = kzalloc(sizeof(*br_port), GFP_KERNEL);
	if (!br_port)
		return NULL;

	br_port->flags = BR_LEARNING | BR_FLOOD | BR_LEARNING_SYNC |
				BR_MCAST_FLOOD;
	br_port->stp_state = BR_STATE_DISABLED;
	refcount_set(&br_port->ref_count, 1);
	br_port->bridge = bridge;
	br_port->dev = dev;

	INIT_LIST_HEAD(&br_port->vlan_list);
	list_add(&br_port->head, &bridge->port_list);
	INIT_LIST_HEAD(&br_port->br_mdb_port_list);

	return br_port;
}

static void
prestera_bridge_port_destroy(struct prestera_bridge_port *br_port)
{
	list_del(&br_port->head);
	WARN_ON(!list_empty(&br_port->vlan_list));
	WARN_ON(!list_empty(&br_port->br_mdb_port_list));
	kfree(br_port);
}

static void prestera_bridge_port_get(struct prestera_bridge_port *br_port)
{
	refcount_inc(&br_port->ref_count);
}

static void prestera_bridge_port_put(struct prestera_bridge_port *br_port)
{
	struct prestera_bridge *bridge = br_port->bridge;

	if (refcount_dec_and_test(&br_port->ref_count)) {
		prestera_bridge_port_destroy(br_port);
		prestera_bridge_put(bridge);
	}
}

static struct prestera_bridge_port *
prestera_bridge_port_add(struct prestera_bridge *bridge, struct net_device *dev)
{
	struct prestera_bridge_port *br_port;

	br_port = __prestera_bridge_port_by_dev(bridge, dev);
	if (br_port) {
		prestera_bridge_port_get(br_port);
		return br_port;
	}

	br_port = prestera_bridge_port_create(bridge, dev);
	if (!br_port)
		return ERR_PTR(-ENOMEM);

	return br_port;
}

static int
prestera_bridge_1d_port_join(struct prestera_bridge_port *br_port)
{
	struct prestera_port *port = netdev_priv(br_port->dev);
	struct prestera_bridge *bridge = br_port->bridge;
	int err;

	err = prestera_hw_bridge_port_add(port, bridge->bridge_id);
	if (err)
		return err;

	err = prestera_br_port_flags_set(br_port, port);
	if (err)
		goto err_flags2port_set;

	return 0;

err_flags2port_set:
	prestera_hw_bridge_port_delete(port, bridge->bridge_id);

	return err;
}

int prestera_bridge_port_join(struct net_device *br_dev,
			      struct prestera_port *port,
			      struct netlink_ext_ack *extack)
{
	struct prestera_switchdev *swdev = port->sw->swdev;
	struct prestera_bridge_port *br_port;
	struct prestera_bridge *bridge;
	int err;

	bridge = prestera_bridge_by_dev(swdev, br_dev);
	if (!bridge) {
		bridge = prestera_bridge_create(swdev, br_dev);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
	}

	br_port = prestera_bridge_port_add(bridge, port->dev);
	if (IS_ERR(br_port)) {
		prestera_bridge_put(bridge);
		return PTR_ERR(br_port);
	}

	err = switchdev_bridge_port_offload(br_port->dev, port->dev, NULL,
					    NULL, NULL, false, extack);
	if (err)
		goto err_switchdev_offload;

	if (bridge->vlan_enabled)
		return 0;

	err = prestera_bridge_1d_port_join(br_port);
	if (err)
		goto err_port_join;

	return 0;

err_port_join:
	switchdev_bridge_port_unoffload(br_port->dev, NULL, NULL, NULL);
err_switchdev_offload:
	prestera_bridge_port_put(br_port);
	return err;
}

static void prestera_bridge_1q_port_leave(struct prestera_bridge_port *br_port)
{
	struct prestera_port *port = netdev_priv(br_port->dev);

	prestera_hw_fdb_flush_port(port, PRESTERA_FDB_FLUSH_MODE_ALL);
	prestera_port_pvid_set(port, PRESTERA_DEFAULT_VID);
}

static void prestera_bridge_1d_port_leave(struct prestera_bridge_port *br_port)
{
	struct prestera_port *port = netdev_priv(br_port->dev);

	prestera_hw_fdb_flush_port(port, PRESTERA_FDB_FLUSH_MODE_ALL);
	prestera_hw_bridge_port_delete(port, br_port->bridge->bridge_id);
}

static int prestera_port_vid_stp_set(struct prestera_port *port, u16 vid,
				     u8 state)
{
	u8 hw_state = state;

	switch (state) {
	case BR_STATE_DISABLED:
		hw_state = PRESTERA_STP_DISABLED;
		break;

	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		hw_state = PRESTERA_STP_BLOCK_LISTEN;
		break;

	case BR_STATE_LEARNING:
		hw_state = PRESTERA_STP_LEARN;
		break;

	case BR_STATE_FORWARDING:
		hw_state = PRESTERA_STP_FORWARD;
		break;

	default:
		return -EINVAL;
	}

	return prestera_hw_vlan_port_stp_set(port, vid, hw_state);
}

void prestera_bridge_port_leave(struct net_device *br_dev,
				struct prestera_port *port)
{
	struct prestera_switchdev *swdev = port->sw->swdev;
	struct prestera_bridge_port *br_port;
	struct prestera_bridge *bridge;

	bridge = prestera_bridge_by_dev(swdev, br_dev);
	if (!bridge)
		return;

	br_port = __prestera_bridge_port_by_dev(bridge, port->dev);
	if (!br_port)
		return;

	bridge = br_port->bridge;

	if (bridge->vlan_enabled)
		prestera_bridge_1q_port_leave(br_port);
	else
		prestera_bridge_1d_port_leave(br_port);

	switchdev_bridge_port_unoffload(br_port->dev, NULL, NULL, NULL);

	prestera_mdb_flush_bridge_port(br_port);

	prestera_br_port_flags_reset(br_port, port);
	prestera_port_vid_stp_set(port, PRESTERA_VID_ALL, BR_STATE_FORWARDING);
	prestera_bridge_port_put(br_port);
}

static int prestera_port_attr_br_flags_set(struct prestera_port *port,
					   struct net_device *dev,
					   struct switchdev_brport_flags flags)
{
	struct prestera_bridge_port *br_port;

	br_port = prestera_bridge_port_by_dev(port->sw->swdev, dev);
	if (!br_port)
		return 0;

	br_port->flags &= ~flags.mask;
	br_port->flags |= flags.val & flags.mask;
	return prestera_br_port_flags_set(br_port, port);
}

static int prestera_port_attr_br_ageing_set(struct prestera_port *port,
					    unsigned long ageing_clock_t)
{
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock_t);
	u32 ageing_time_ms = jiffies_to_msecs(ageing_jiffies);
	struct prestera_switch *sw = port->sw;

	if (ageing_time_ms < PRESTERA_MIN_AGEING_TIME_MS ||
	    ageing_time_ms > PRESTERA_MAX_AGEING_TIME_MS)
		return -ERANGE;

	return prestera_hw_switch_ageing_set(sw, ageing_time_ms);
}

static int prestera_port_attr_br_vlan_set(struct prestera_port *port,
					  struct net_device *dev,
					  bool vlan_enabled)
{
	struct prestera_switch *sw = port->sw;
	struct prestera_bridge *bridge;

	bridge = prestera_bridge_by_dev(sw->swdev, dev);
	if (WARN_ON(!bridge))
		return -EINVAL;

	if (bridge->vlan_enabled == vlan_enabled)
		return 0;

	netdev_err(bridge->dev, "VLAN filtering can't be changed for existing bridge\n");

	return -EINVAL;
}

static int prestera_port_bridge_vlan_stp_set(struct prestera_port *port,
					     struct prestera_bridge_vlan *br_vlan,
					     u8 state)
{
	struct prestera_port_vlan *port_vlan;

	list_for_each_entry(port_vlan, &br_vlan->port_vlan_list, br_vlan_head) {
		if (port_vlan->port != port)
			continue;

		return prestera_port_vid_stp_set(port, br_vlan->vid, state);
	}

	return 0;
}

static int prestera_port_attr_stp_state_set(struct prestera_port *port,
					    struct net_device *dev,
					    u8 state)
{
	struct prestera_bridge_port *br_port;
	struct prestera_bridge_vlan *br_vlan;
	int err;
	u16 vid;

	br_port = prestera_bridge_port_by_dev(port->sw->swdev, dev);
	if (!br_port)
		return 0;

	if (!br_port->bridge->vlan_enabled) {
		vid = br_port->bridge->bridge_id;
		err = prestera_port_vid_stp_set(port, vid, state);
		if (err)
			goto err_port_stp_set;
	} else {
		list_for_each_entry(br_vlan, &br_port->vlan_list, head) {
			err = prestera_port_bridge_vlan_stp_set(port, br_vlan,
								state);
			if (err)
				goto err_port_vlan_stp_set;
		}
	}

	br_port->stp_state = state;

	return 0;

err_port_vlan_stp_set:
	list_for_each_entry_continue_reverse(br_vlan, &br_port->vlan_list, head)
		prestera_port_bridge_vlan_stp_set(port, br_vlan, br_port->stp_state);
	return err;

err_port_stp_set:
	prestera_port_vid_stp_set(port, vid, br_port->stp_state);

	return err;
}

static int
prestera_br_port_lag_mdb_mc_enable_sync(struct prestera_bridge_port *br_port,
					bool enabled)
{
	struct prestera_port *pr_port;
	struct prestera_switch *sw;
	u16 lag_id;
	int err;

	pr_port = prestera_port_dev_lower_find(br_port->dev);
	if (!pr_port)
		return 0;

	sw = pr_port->sw;
	err = prestera_lag_id(sw, br_port->dev, &lag_id);
	if (err)
		return err;

	list_for_each_entry(pr_port, &sw->port_list, list) {
		if (pr_port->lag->lag_id == lag_id) {
			err = prestera_port_mc_flood_set(pr_port, enabled);
			if (err)
				return err;
		}
	}

	return 0;
}

static int prestera_br_mdb_mc_enable_sync(struct prestera_bridge *br_dev)
{
	struct prestera_bridge_port *br_port;
	struct prestera_port *port;
	bool enabled;
	int err;

	/* if mrouter exists:
	 *  - make sure every mrouter receives unreg mcast traffic;
	 * if mrouter doesn't exists:
	 *  - make sure every port receives unreg mcast traffic;
	 */
	list_for_each_entry(br_port, &br_dev->port_list, head) {
		if (br_dev->multicast_enabled && br_dev->mrouter_exist)
			enabled = br_port->mrouter;
		else
			enabled = br_port->flags & BR_MCAST_FLOOD;

		if (netif_is_lag_master(br_port->dev)) {
			err = prestera_br_port_lag_mdb_mc_enable_sync(br_port,
								      enabled);
			if (err)
				return err;
			continue;
		}

		port = prestera_port_dev_lower_find(br_port->dev);
		if (!port)
			continue;

		err = prestera_port_mc_flood_set(port, enabled);
		if (err)
			return err;
	}

	return 0;
}

static bool
prestera_br_mdb_port_is_member(struct prestera_br_mdb_entry *br_mdb,
			       struct net_device *orig_dev)
{
	struct prestera_br_mdb_port *tmp_port;

	list_for_each_entry(tmp_port, &br_mdb->br_mdb_port_list,
			    br_mdb_port_node)
		if (tmp_port->br_port->dev == orig_dev)
			return true;

	return false;
}

static int
prestera_mdb_port_add(struct prestera_mdb_entry *mdb,
		      struct net_device *orig_dev,
		      const unsigned char addr[ETH_ALEN], u16 vid)
{
	struct prestera_flood_domain *flood_domain = mdb->flood_domain;
	int err;

	if (!prestera_flood_domain_port_find(flood_domain,
					     orig_dev, vid)) {
		err = prestera_flood_domain_port_create(flood_domain, orig_dev,
							vid);
		if (err)
			return err;
	}

	return 0;
}

/* Sync bridge mdb (software table) with HW table (if MC is enabled). */
static int prestera_br_mdb_sync(struct prestera_bridge *br_dev)
{
	struct prestera_br_mdb_port *br_mdb_port;
	struct prestera_bridge_port *br_port;
	struct prestera_br_mdb_entry *br_mdb;
	struct prestera_mdb_entry *mdb;
	struct prestera_port *pr_port;
	int err = 0;

	if (!br_dev->multicast_enabled)
		return 0;

	list_for_each_entry(br_mdb, &br_dev->br_mdb_entry_list,
			    br_mdb_entry_node) {
		mdb = br_mdb->mdb;
		/* Make sure every port that explicitly been added to the mdb
		 * joins the specified group.
		 */
		list_for_each_entry(br_mdb_port, &br_mdb->br_mdb_port_list,
				    br_mdb_port_node) {
			br_port = br_mdb_port->br_port;
			pr_port = prestera_port_dev_lower_find(br_port->dev);

			/* Match only mdb and br_mdb ports that belong to the
			 * same broadcast domain.
			 */
			if (br_dev->vlan_enabled &&
			    !prestera_port_vlan_by_vid(pr_port,
						       mdb->vid))
				continue;

			/* If port is not in MDB or there's no Mrouter
			 * clear HW mdb.
			 */
			if (prestera_br_mdb_port_is_member(br_mdb,
							   br_mdb_port->br_port->dev) &&
							   br_dev->mrouter_exist)
				err = prestera_mdb_port_add(mdb, br_port->dev,
							    mdb->addr,
							    mdb->vid);
			else
				prestera_mdb_port_del(mdb, br_port->dev);

			if (err)
				return err;
		}

		/* Make sure that every mrouter port joins every MC group int
		 * broadcast domain. If it's not an mrouter - it should leave
		 */
		list_for_each_entry(br_port, &br_dev->port_list, head) {
			pr_port = prestera_port_dev_lower_find(br_port->dev);

			/* Make sure mrouter woudln't receive traffci from
			 * another broadcast domain (e.g. from a vlan, which
			 * mrouter port is not a member of).
			 */
			if (br_dev->vlan_enabled &&
			    !prestera_port_vlan_by_vid(pr_port,
						       mdb->vid))
				continue;

			if (br_port->mrouter) {
				err = prestera_mdb_port_add(mdb, br_port->dev,
							    mdb->addr,
							    mdb->vid);
				if (err)
					return err;
			} else if (!br_port->mrouter &&
				   !prestera_br_mdb_port_is_member
				   (br_mdb, br_port->dev)) {
				prestera_mdb_port_del(mdb, br_port->dev);
			}
		}
	}

	return 0;
}

static int
prestera_mdb_enable_set(struct prestera_br_mdb_entry *br_mdb, bool enable)
{
	int err;

	if (enable != br_mdb->enabled) {
		if (enable)
			err = prestera_hw_mdb_create(br_mdb->mdb);
		else
			err = prestera_hw_mdb_destroy(br_mdb->mdb);

		if (err)
			return err;

		br_mdb->enabled = enable;
	}

	return 0;
}

static int
prestera_br_mdb_enable_set(struct prestera_bridge *br_dev, bool enable)
{
	struct prestera_br_mdb_entry *br_mdb;
	int err;

	list_for_each_entry(br_mdb, &br_dev->br_mdb_entry_list,
			    br_mdb_entry_node) {
		err = prestera_mdb_enable_set(br_mdb, enable);
		if (err)
			return err;
	}

	return 0;
}

static int prestera_port_attr_br_mc_disabled_set(struct prestera_port *port,
						 struct net_device *orig_dev,
						 bool mc_disabled)
{
	struct prestera_switch *sw = port->sw;
	struct prestera_bridge *br_dev;

	br_dev = prestera_bridge_find(sw, orig_dev);
	if (!br_dev)
		return 0;

	br_dev->multicast_enabled = !mc_disabled;

	/* There's no point in enabling mdb back if router is missing. */
	WARN_ON(prestera_br_mdb_enable_set(br_dev, br_dev->multicast_enabled &&
					   br_dev->mrouter_exist));

	WARN_ON(prestera_br_mdb_sync(br_dev));

	WARN_ON(prestera_br_mdb_mc_enable_sync(br_dev));

	return 0;
}

static bool
prestera_bridge_mdb_mc_mrouter_exists(struct prestera_bridge *br_dev)
{
	struct prestera_bridge_port *br_port;

	list_for_each_entry(br_port, &br_dev->port_list, head)
		if (br_port->mrouter)
			return true;

	return false;
}

static int
prestera_port_attr_mrouter_set(struct prestera_port *port,
			       struct net_device *orig_dev,
			       bool is_port_mrouter)
{
	struct prestera_bridge_port *br_port;
	struct prestera_bridge *br_dev;

	br_port = prestera_bridge_port_find(port->sw, orig_dev);
	if (!br_port)
		return 0;

	br_dev = br_port->bridge;
	br_port->mrouter = is_port_mrouter;

	br_dev->mrouter_exist = prestera_bridge_mdb_mc_mrouter_exists(br_dev);

	/* Enable MDB processing if both mrouter exists and mc is enabled.
	 * In case if MC enabled, but there is no mrouter, device would flood
	 * all multicast traffic (even if MDB table is not empty) with the use
	 * of bridge's flood capabilities (without the use of flood_domain).
	 */
	WARN_ON(prestera_br_mdb_enable_set(br_dev, br_dev->multicast_enabled &&
					   br_dev->mrouter_exist));

	WARN_ON(prestera_br_mdb_sync(br_dev));

	WARN_ON(prestera_br_mdb_mc_enable_sync(br_dev));

	return 0;
}

static int prestera_port_obj_attr_set(struct net_device *dev, const void *ctx,
				      const struct switchdev_attr *attr,
				      struct netlink_ext_ack *extack)
{
	struct prestera_port *port = netdev_priv(dev);
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		err = prestera_port_attr_stp_state_set(port, attr->orig_dev,
						       attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		if (attr->u.brport_flags.mask &
		    ~(BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD | BR_PORT_LOCKED))
			err = -EINVAL;
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		err = prestera_port_attr_br_flags_set(port, attr->orig_dev,
						      attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		err = prestera_port_attr_br_ageing_set(port,
						       attr->u.ageing_time);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		err = prestera_port_attr_br_vlan_set(port, attr->orig_dev,
						     attr->u.vlan_filtering);
		break;
	case SWITCHDEV_ATTR_ID_PORT_MROUTER:
		err = prestera_port_attr_mrouter_set(port, attr->orig_dev,
						     attr->u.mrouter);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED:
		err = prestera_port_attr_br_mc_disabled_set(port, attr->orig_dev,
							    attr->u.mc_disabled);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static void
prestera_fdb_offload_notify(struct prestera_port *port,
			    struct switchdev_notifier_fdb_info *info)
{
	struct switchdev_notifier_fdb_info send_info = {};

	send_info.addr = info->addr;
	send_info.vid = info->vid;
	send_info.offloaded = true;

	call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED, port->dev,
				 &send_info.info, NULL);
}

static int prestera_port_fdb_set(struct prestera_port *port,
				 struct switchdev_notifier_fdb_info *fdb_info,
				 bool adding)
{
	struct prestera_switch *sw = port->sw;
	struct prestera_bridge_port *br_port;
	struct prestera_bridge *bridge;
	int err;
	u16 vid;

	br_port = prestera_bridge_port_by_dev(sw->swdev, port->dev);
	if (!br_port)
		return -EINVAL;

	bridge = br_port->bridge;

	if (bridge->vlan_enabled)
		vid = fdb_info->vid;
	else
		vid = bridge->bridge_id;

	if (adding)
		err = prestera_fdb_add(port, fdb_info->addr, vid, false);
	else
		err = prestera_fdb_del(port, fdb_info->addr, vid);

	return err;
}

static void prestera_fdb_event_work(struct work_struct *work)
{
	struct switchdev_notifier_fdb_info *fdb_info;
	struct prestera_fdb_event_work *swdev_work;
	struct prestera_port *port;
	struct net_device *dev;
	int err;

	swdev_work = container_of(work, struct prestera_fdb_event_work, work);
	dev = swdev_work->dev;

	rtnl_lock();

	port = prestera_port_dev_lower_find(dev);
	if (!port)
		goto out_unlock;

	switch (swdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fdb_info = &swdev_work->fdb_info;
		if (!fdb_info->added_by_user || fdb_info->is_local)
			break;

		err = prestera_port_fdb_set(port, fdb_info, true);
		if (err)
			break;

		prestera_fdb_offload_notify(port, fdb_info);
		break;

	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb_info = &swdev_work->fdb_info;
		prestera_port_fdb_set(port, fdb_info, false);
		break;
	}

out_unlock:
	rtnl_unlock();

	kfree(swdev_work->fdb_info.addr);
	kfree(swdev_work);
	dev_put(dev);
}

static int prestera_switchdev_event(struct notifier_block *unused,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct switchdev_notifier_fdb_info *fdb_info;
	struct switchdev_notifier_info *info = ptr;
	struct prestera_fdb_event_work *swdev_work;
	struct net_device *upper;
	int err;

	if (event == SWITCHDEV_PORT_ATTR_SET) {
		err = switchdev_handle_port_attr_set(dev, ptr,
						     prestera_netdev_check,
						     prestera_port_obj_attr_set);
		return notifier_from_errno(err);
	}

	if (!prestera_netdev_check(dev))
		return NOTIFY_DONE;

	upper = netdev_master_upper_dev_get_rcu(dev);
	if (!upper)
		return NOTIFY_DONE;

	if (!netif_is_bridge_master(upper))
		return NOTIFY_DONE;

	swdev_work = kzalloc(sizeof(*swdev_work), GFP_ATOMIC);
	if (!swdev_work)
		return NOTIFY_BAD;

	swdev_work->event = event;
	swdev_work->dev = dev;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb_info = container_of(info,
					struct switchdev_notifier_fdb_info,
					info);

		INIT_WORK(&swdev_work->work, prestera_fdb_event_work);
		memcpy(&swdev_work->fdb_info, ptr,
		       sizeof(swdev_work->fdb_info));

		swdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
		if (!swdev_work->fdb_info.addr)
			goto out_bad;

		ether_addr_copy((u8 *)swdev_work->fdb_info.addr,
				fdb_info->addr);
		dev_hold(dev);
		break;

	default:
		kfree(swdev_work);
		return NOTIFY_DONE;
	}

	queue_work(swdev_wq, &swdev_work->work);
	return NOTIFY_DONE;

out_bad:
	kfree(swdev_work);
	return NOTIFY_BAD;
}

static int
prestera_port_vlan_bridge_join(struct prestera_port_vlan *port_vlan,
			       struct prestera_bridge_port *br_port)
{
	struct prestera_port *port = port_vlan->port;
	struct prestera_bridge_vlan *br_vlan;
	u16 vid = port_vlan->vid;
	int err;

	if (port_vlan->br_port)
		return 0;

	err = prestera_br_port_flags_set(br_port, port);
	if (err)
		goto err_flags2port_set;

	err = prestera_port_vid_stp_set(port, vid, br_port->stp_state);
	if (err)
		goto err_port_vid_stp_set;

	br_vlan = prestera_bridge_vlan_by_vid(br_port, vid);
	if (!br_vlan) {
		br_vlan = prestera_bridge_vlan_create(br_port, vid);
		if (!br_vlan) {
			err = -ENOMEM;
			goto err_bridge_vlan_get;
		}
	}

	list_add(&port_vlan->br_vlan_head, &br_vlan->port_vlan_list);

	prestera_bridge_port_get(br_port);
	port_vlan->br_port = br_port;

	return 0;

err_bridge_vlan_get:
	prestera_port_vid_stp_set(port, vid, BR_STATE_FORWARDING);
err_port_vid_stp_set:
	prestera_br_port_flags_reset(br_port, port);
err_flags2port_set:
	return err;
}

static int
prestera_bridge_port_vlan_add(struct prestera_port *port,
			      struct prestera_bridge_port *br_port,
			      u16 vid, bool is_untagged, bool is_pvid,
			      struct netlink_ext_ack *extack)
{
	struct prestera_port_vlan *port_vlan;
	u16 old_pvid = port->pvid;
	u16 pvid;
	int err;

	if (is_pvid)
		pvid = vid;
	else
		pvid = port->pvid == vid ? 0 : port->pvid;

	port_vlan = prestera_port_vlan_by_vid(port, vid);
	if (port_vlan && port_vlan->br_port != br_port)
		return -EEXIST;

	if (!port_vlan) {
		port_vlan = prestera_port_vlan_create(port, vid, is_untagged);
		if (IS_ERR(port_vlan))
			return PTR_ERR(port_vlan);
	} else {
		err = prestera_hw_vlan_port_set(port, vid, true, is_untagged);
		if (err)
			goto err_port_vlan_set;
	}

	err = prestera_port_pvid_set(port, pvid);
	if (err)
		goto err_port_pvid_set;

	err = prestera_port_vlan_bridge_join(port_vlan, br_port);
	if (err)
		goto err_port_vlan_bridge_join;

	return 0;

err_port_vlan_bridge_join:
	prestera_port_pvid_set(port, old_pvid);
err_port_pvid_set:
	prestera_hw_vlan_port_set(port, vid, false, false);
err_port_vlan_set:
	prestera_port_vlan_destroy(port_vlan);

	return err;
}

static void
prestera_bridge_port_vlan_del(struct prestera_port *port,
			      struct prestera_bridge_port *br_port, u16 vid)
{
	u16 pvid = port->pvid == vid ? 0 : port->pvid;
	struct prestera_port_vlan *port_vlan;

	port_vlan = prestera_port_vlan_by_vid(port, vid);
	if (WARN_ON(!port_vlan))
		return;

	prestera_port_vlan_bridge_leave(port_vlan);
	prestera_port_pvid_set(port, pvid);
	prestera_port_vlan_destroy(port_vlan);
}

static int prestera_port_vlans_add(struct prestera_port *port,
				   const struct switchdev_obj_port_vlan *vlan,
				   struct netlink_ext_ack *extack)
{
	bool flag_untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool flag_pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct net_device *orig_dev = vlan->obj.orig_dev;
	struct prestera_bridge_port *br_port;
	struct prestera_switch *sw = port->sw;
	struct prestera_bridge *bridge;

	if (netif_is_bridge_master(orig_dev))
		return 0;

	br_port = prestera_bridge_port_by_dev(sw->swdev, port->dev);
	if (WARN_ON(!br_port))
		return -EINVAL;

	bridge = br_port->bridge;
	if (!bridge->vlan_enabled)
		return 0;

	return prestera_bridge_port_vlan_add(port, br_port,
					     vlan->vid, flag_untagged,
					     flag_pvid, extack);
}

static struct prestera_br_mdb_entry *
prestera_br_mdb_entry_create(struct prestera_switch *sw,
			     struct prestera_bridge *br_dev,
			     const unsigned char *addr, u16 vid)
{
	struct prestera_br_mdb_entry *br_mdb_entry;
	struct prestera_mdb_entry *mdb_entry;

	br_mdb_entry = kzalloc(sizeof(*br_mdb_entry), GFP_KERNEL);
	if (!br_mdb_entry)
		return NULL;

	mdb_entry = prestera_mdb_entry_create(sw, addr, vid);
	if (!mdb_entry)
		goto err_mdb_alloc;

	br_mdb_entry->mdb = mdb_entry;
	br_mdb_entry->bridge = br_dev;
	br_mdb_entry->enabled = true;
	INIT_LIST_HEAD(&br_mdb_entry->br_mdb_port_list);

	list_add(&br_mdb_entry->br_mdb_entry_node, &br_dev->br_mdb_entry_list);

	return br_mdb_entry;

err_mdb_alloc:
	kfree(br_mdb_entry);
	return NULL;
}

static int prestera_br_mdb_port_add(struct prestera_br_mdb_entry *br_mdb,
				    struct prestera_bridge_port *br_port)
{
	struct prestera_br_mdb_port *br_mdb_port;

	list_for_each_entry(br_mdb_port, &br_mdb->br_mdb_port_list,
			    br_mdb_port_node)
		if (br_mdb_port->br_port == br_port)
			return 0;

	br_mdb_port = kzalloc(sizeof(*br_mdb_port), GFP_KERNEL);
	if (!br_mdb_port)
		return -ENOMEM;

	br_mdb_port->br_port = br_port;
	list_add(&br_mdb_port->br_mdb_port_node,
		 &br_mdb->br_mdb_port_list);

	return 0;
}

static struct prestera_br_mdb_entry *
prestera_br_mdb_entry_find(struct prestera_bridge *br_dev,
			   const unsigned char *addr, u16 vid)
{
	struct prestera_br_mdb_entry *br_mdb;

	list_for_each_entry(br_mdb, &br_dev->br_mdb_entry_list,
			    br_mdb_entry_node)
		if (ether_addr_equal(&br_mdb->mdb->addr[0], addr) &&
		    vid == br_mdb->mdb->vid)
			return br_mdb;

	return NULL;
}

static struct prestera_br_mdb_entry *
prestera_br_mdb_entry_get(struct prestera_switch *sw,
			  struct prestera_bridge *br_dev,
			  const unsigned char *addr, u16 vid)
{
	struct prestera_br_mdb_entry *br_mdb;

	br_mdb = prestera_br_mdb_entry_find(br_dev, addr, vid);
	if (br_mdb)
		return br_mdb;

	return prestera_br_mdb_entry_create(sw, br_dev, addr, vid);
}

static int
prestera_mdb_port_addr_obj_add(const struct switchdev_obj_port_mdb *mdb)
{
	struct prestera_br_mdb_entry *br_mdb;
	struct prestera_bridge_port *br_port;
	struct prestera_bridge *br_dev;
	struct prestera_switch *sw;
	struct prestera_port *port;
	int err;

	sw = prestera_switch_get(mdb->obj.orig_dev);
	port = prestera_port_dev_lower_find(mdb->obj.orig_dev);

	br_port = prestera_bridge_port_find(sw, mdb->obj.orig_dev);
	if (!br_port)
		return 0;

	br_dev = br_port->bridge;

	if (mdb->vid && !prestera_port_vlan_by_vid(port, mdb->vid))
		return 0;

	if (mdb->vid)
		br_mdb = prestera_br_mdb_entry_get(sw, br_dev, &mdb->addr[0],
						   mdb->vid);
	else
		br_mdb = prestera_br_mdb_entry_get(sw, br_dev, &mdb->addr[0],
						   br_dev->bridge_id);

	if (!br_mdb)
		return -ENOMEM;

	/* Make sure newly allocated MDB entry gets disabled if either MC is
	 * disabled, or the mrouter does not exist.
	 */
	WARN_ON(prestera_mdb_enable_set(br_mdb, br_dev->multicast_enabled &&
					br_dev->mrouter_exist));

	err = prestera_br_mdb_port_add(br_mdb, br_port);
	if (err) {
		prestera_br_mdb_entry_put(br_mdb);
		return err;
	}

	err = prestera_br_mdb_sync(br_dev);
	if (err)
		return err;

	return 0;
}

static int prestera_port_obj_add(struct net_device *dev, const void *ctx,
				 const struct switchdev_obj *obj,
				 struct netlink_ext_ack *extack)
{
	struct prestera_port *port = netdev_priv(dev);
	const struct switchdev_obj_port_vlan *vlan;
	const struct switchdev_obj_port_mdb *mdb;
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
		return prestera_port_vlans_add(port, vlan, extack);
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
		err = prestera_mdb_port_addr_obj_add(mdb);
		break;
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		fallthrough;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int prestera_port_vlans_del(struct prestera_port *port,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct net_device *orig_dev = vlan->obj.orig_dev;
	struct prestera_bridge_port *br_port;
	struct prestera_switch *sw = port->sw;

	if (netif_is_bridge_master(orig_dev))
		return -EOPNOTSUPP;

	br_port = prestera_bridge_port_by_dev(sw->swdev, port->dev);
	if (WARN_ON(!br_port))
		return -EINVAL;

	if (!br_port->bridge->vlan_enabled)
		return 0;

	prestera_bridge_port_vlan_del(port, br_port, vlan->vid);

	return 0;
}

static int
prestera_mdb_port_addr_obj_del(struct prestera_port *port,
			       const struct switchdev_obj_port_mdb *mdb)
{
	struct prestera_br_mdb_entry *br_mdb;
	struct prestera_bridge_port *br_port;
	struct prestera_bridge *br_dev;
	int err;

	/* Bridge port no longer exists - and so does this MDB entry */
	br_port = prestera_bridge_port_find(port->sw, mdb->obj.orig_dev);
	if (!br_port)
		return 0;

	/* Removing MDB with non-existing VLAN - not supported; */
	if (mdb->vid && !prestera_port_vlan_by_vid(port, mdb->vid))
		return 0;

	br_dev = br_port->bridge;

	if (br_port->bridge->vlan_enabled)
		br_mdb = prestera_br_mdb_entry_find(br_dev, &mdb->addr[0],
						    mdb->vid);
	else
		br_mdb = prestera_br_mdb_entry_find(br_dev, &mdb->addr[0],
						    br_port->bridge->bridge_id);

	if (!br_mdb)
		return 0;

	/* Since there might be a situation that this port was the last in the
	 * MDB group, we have to both remove this port from software and HW MDB,
	 * sync MDB table, and then destroy software MDB (if needed).
	 */
	prestera_br_mdb_port_del(br_mdb, br_port);

	prestera_br_mdb_entry_put(br_mdb);

	err = prestera_br_mdb_sync(br_dev);
	if (err)
		return err;

	return 0;
}

static int prestera_port_obj_del(struct net_device *dev, const void *ctx,
				 const struct switchdev_obj *obj)
{
	struct prestera_port *port = netdev_priv(dev);
	const struct switchdev_obj_port_mdb *mdb;
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		return prestera_port_vlans_del(port, SWITCHDEV_OBJ_PORT_VLAN(obj));
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
		err = prestera_mdb_port_addr_obj_del(port, mdb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int prestera_switchdev_blk_event(struct notifier_block *unused,
					unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = switchdev_handle_port_obj_add(dev, ptr,
						    prestera_netdev_check,
						    prestera_port_obj_add);
		break;
	case SWITCHDEV_PORT_OBJ_DEL:
		err = switchdev_handle_port_obj_del(dev, ptr,
						    prestera_netdev_check,
						    prestera_port_obj_del);
		break;
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     prestera_netdev_check,
						     prestera_port_obj_attr_set);
		break;
	default:
		return NOTIFY_DONE;
	}

	return notifier_from_errno(err);
}

static void prestera_fdb_event(struct prestera_switch *sw,
			       struct prestera_event *evt, void *arg)
{
	struct switchdev_notifier_fdb_info info = {};
	struct net_device *dev = NULL;
	struct prestera_port *port;
	struct prestera_lag *lag;

	switch (evt->fdb_evt.type) {
	case PRESTERA_FDB_ENTRY_TYPE_REG_PORT:
		port = prestera_find_port(sw, evt->fdb_evt.dest.port_id);
		if (port)
			dev = port->dev;
		break;
	case PRESTERA_FDB_ENTRY_TYPE_LAG:
		lag = prestera_lag_by_id(sw, evt->fdb_evt.dest.lag_id);
		if (lag)
			dev = lag->dev;
		break;
	default:
		return;
	}

	if (!dev)
		return;

	info.addr = evt->fdb_evt.data.mac;
	info.vid = evt->fdb_evt.vid;
	info.offloaded = true;

	rtnl_lock();

	switch (evt->id) {
	case PRESTERA_FDB_EVENT_LEARNED:
		call_switchdev_notifiers(SWITCHDEV_FDB_ADD_TO_BRIDGE,
					 dev, &info.info, NULL);
		break;
	case PRESTERA_FDB_EVENT_AGED:
		call_switchdev_notifiers(SWITCHDEV_FDB_DEL_TO_BRIDGE,
					 dev, &info.info, NULL);
		break;
	}

	rtnl_unlock();
}

static int prestera_fdb_init(struct prestera_switch *sw)
{
	int err;

	err = prestera_hw_event_handler_register(sw, PRESTERA_EVENT_TYPE_FDB,
						 prestera_fdb_event, NULL);
	if (err)
		return err;

	err = prestera_hw_switch_ageing_set(sw, PRESTERA_DEFAULT_AGEING_TIME_MS);
	if (err)
		goto err_ageing_set;

	return 0;

err_ageing_set:
	prestera_hw_event_handler_unregister(sw, PRESTERA_EVENT_TYPE_FDB,
					     prestera_fdb_event);
	return err;
}

static void prestera_fdb_fini(struct prestera_switch *sw)
{
	prestera_hw_event_handler_unregister(sw, PRESTERA_EVENT_TYPE_FDB,
					     prestera_fdb_event);
}

static int prestera_switchdev_handler_init(struct prestera_switchdev *swdev)
{
	int err;

	swdev->swdev_nb.notifier_call = prestera_switchdev_event;
	err = register_switchdev_notifier(&swdev->swdev_nb);
	if (err)
		goto err_register_swdev_notifier;

	swdev->swdev_nb_blk.notifier_call = prestera_switchdev_blk_event;
	err = register_switchdev_blocking_notifier(&swdev->swdev_nb_blk);
	if (err)
		goto err_register_blk_swdev_notifier;

	return 0;

err_register_blk_swdev_notifier:
	unregister_switchdev_notifier(&swdev->swdev_nb);
err_register_swdev_notifier:
	destroy_workqueue(swdev_wq);
	return err;
}

static void prestera_switchdev_handler_fini(struct prestera_switchdev *swdev)
{
	unregister_switchdev_blocking_notifier(&swdev->swdev_nb_blk);
	unregister_switchdev_notifier(&swdev->swdev_nb);
}

int prestera_switchdev_init(struct prestera_switch *sw)
{
	struct prestera_switchdev *swdev;
	int err;

	swdev = kzalloc(sizeof(*swdev), GFP_KERNEL);
	if (!swdev)
		return -ENOMEM;

	sw->swdev = swdev;
	swdev->sw = sw;

	INIT_LIST_HEAD(&swdev->bridge_list);

	swdev_wq = alloc_ordered_workqueue("%s_ordered", 0, "prestera_br");
	if (!swdev_wq) {
		err = -ENOMEM;
		goto err_alloc_wq;
	}

	err = prestera_switchdev_handler_init(swdev);
	if (err)
		goto err_swdev_init;

	err = prestera_fdb_init(sw);
	if (err)
		goto err_fdb_init;

	return 0;

err_fdb_init:
err_swdev_init:
	destroy_workqueue(swdev_wq);
err_alloc_wq:
	kfree(swdev);

	return err;
}

void prestera_switchdev_fini(struct prestera_switch *sw)
{
	struct prestera_switchdev *swdev = sw->swdev;

	prestera_fdb_fini(sw);
	prestera_switchdev_handler_fini(swdev);
	destroy_workqueue(swdev_wq);
	kfree(swdev);
}
