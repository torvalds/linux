// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <net/switchdev.h>
#include <net/vxlan.h>

#include "spectrum_span.h"
#include "spectrum_switchdev.h"
#include "spectrum.h"
#include "core.h"
#include "reg.h"

struct mlxsw_sp_bridge_ops;

struct mlxsw_sp_bridge {
	struct mlxsw_sp *mlxsw_sp;
	struct {
		struct delayed_work dw;
#define MLXSW_SP_DEFAULT_LEARNING_INTERVAL 100
		unsigned int interval; /* ms */
	} fdb_notify;
#define MLXSW_SP_MIN_AGEING_TIME 10
#define MLXSW_SP_MAX_AGEING_TIME 1000000
#define MLXSW_SP_DEFAULT_AGEING_TIME 300
	u32 ageing_time;
	bool vlan_enabled_exists;
	struct list_head bridges_list;
	DECLARE_BITMAP(mids_bitmap, MLXSW_SP_MID_MAX);
	const struct mlxsw_sp_bridge_ops *bridge_8021q_ops;
	const struct mlxsw_sp_bridge_ops *bridge_8021d_ops;
	const struct mlxsw_sp_bridge_ops *bridge_8021ad_ops;
};

struct mlxsw_sp_bridge_device {
	struct net_device *dev;
	struct list_head list;
	struct list_head ports_list;
	struct list_head mdb_list;
	struct rhashtable mdb_ht;
	u8 vlan_enabled:1,
	   multicast_enabled:1,
	   mrouter:1;
	const struct mlxsw_sp_bridge_ops *ops;
};

struct mlxsw_sp_bridge_port {
	struct net_device *dev;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct list_head list;
	struct list_head vlans_list;
	unsigned int ref_count;
	u8 stp_state;
	unsigned long flags;
	bool mrouter;
	bool lagged;
	union {
		u16 lag_id;
		u16 system_port;
	};
};

struct mlxsw_sp_bridge_vlan {
	struct list_head list;
	struct list_head port_vlan_list;
	u16 vid;
};

struct mlxsw_sp_bridge_ops {
	int (*port_join)(struct mlxsw_sp_bridge_device *bridge_device,
			 struct mlxsw_sp_bridge_port *bridge_port,
			 struct mlxsw_sp_port *mlxsw_sp_port,
			 struct netlink_ext_ack *extack);
	void (*port_leave)(struct mlxsw_sp_bridge_device *bridge_device,
			   struct mlxsw_sp_bridge_port *bridge_port,
			   struct mlxsw_sp_port *mlxsw_sp_port);
	int (*vxlan_join)(struct mlxsw_sp_bridge_device *bridge_device,
			  const struct net_device *vxlan_dev, u16 vid,
			  struct netlink_ext_ack *extack);
	struct mlxsw_sp_fid *
		(*fid_get)(struct mlxsw_sp_bridge_device *bridge_device,
			   u16 vid, struct netlink_ext_ack *extack);
	struct mlxsw_sp_fid *
		(*fid_lookup)(struct mlxsw_sp_bridge_device *bridge_device,
			      u16 vid);
	u16 (*fid_vid)(struct mlxsw_sp_bridge_device *bridge_device,
		       const struct mlxsw_sp_fid *fid);
};

struct mlxsw_sp_switchdev_ops {
	void (*init)(struct mlxsw_sp *mlxsw_sp);
};

struct mlxsw_sp_mdb_entry_key {
	unsigned char addr[ETH_ALEN];
	u16 fid;
};

struct mlxsw_sp_mdb_entry {
	struct list_head list;
	struct rhash_head ht_node;
	struct mlxsw_sp_mdb_entry_key key;
	u16 mid;
	struct list_head ports_list;
	u16 ports_count;
};

struct mlxsw_sp_mdb_entry_port {
	struct list_head list; /* Member of 'ports_list'. */
	u16 local_port;
	refcount_t refcount;
	bool mrouter;
};

static const struct rhashtable_params mlxsw_sp_mdb_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_mdb_entry, key),
	.head_offset = offsetof(struct mlxsw_sp_mdb_entry, ht_node),
	.key_len = sizeof(struct mlxsw_sp_mdb_entry_key),
};

static int
mlxsw_sp_bridge_port_fdb_flush(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_bridge_port *bridge_port,
			       u16 fid_index);

static void
mlxsw_sp_bridge_port_mdb_flush(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct mlxsw_sp_bridge_port *bridge_port,
			       u16 fid_index);

static int
mlxsw_sp_bridge_mdb_mc_enable_sync(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_bridge_device
				   *bridge_device, bool mc_enabled);

static void
mlxsw_sp_port_mrouter_update_mdb(struct mlxsw_sp_port *mlxsw_sp_port,
				 struct mlxsw_sp_bridge_port *bridge_port,
				 bool add);

static struct mlxsw_sp_bridge_device *
mlxsw_sp_bridge_device_find(const struct mlxsw_sp_bridge *bridge,
			    const struct net_device *br_dev)
{
	struct mlxsw_sp_bridge_device *bridge_device;

	list_for_each_entry(bridge_device, &bridge->bridges_list, list)
		if (bridge_device->dev == br_dev)
			return bridge_device;

	return NULL;
}

bool mlxsw_sp_bridge_device_is_offloaded(const struct mlxsw_sp *mlxsw_sp,
					 const struct net_device *br_dev)
{
	return !!mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, br_dev);
}

static int mlxsw_sp_bridge_device_upper_rif_destroy(struct net_device *dev,
						    struct netdev_nested_priv *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv->data;

	mlxsw_sp_rif_destroy_by_dev(mlxsw_sp, dev);
	return 0;
}

static void mlxsw_sp_bridge_device_rifs_destroy(struct mlxsw_sp *mlxsw_sp,
						struct net_device *dev)
{
	struct netdev_nested_priv priv = {
		.data = (void *)mlxsw_sp,
	};

	mlxsw_sp_rif_destroy_by_dev(mlxsw_sp, dev);
	netdev_walk_all_upper_dev_rcu(dev,
				      mlxsw_sp_bridge_device_upper_rif_destroy,
				      &priv);
}

static int mlxsw_sp_bridge_device_vxlan_init(struct mlxsw_sp_bridge *bridge,
					     struct net_device *br_dev,
					     struct netlink_ext_ack *extack)
{
	struct net_device *dev, *stop_dev;
	struct list_head *iter;
	int err;

	netdev_for_each_lower_dev(br_dev, dev, iter) {
		if (netif_is_vxlan(dev) && netif_running(dev)) {
			err = mlxsw_sp_bridge_vxlan_join(bridge->mlxsw_sp,
							 br_dev, dev, 0,
							 extack);
			if (err) {
				stop_dev = dev;
				goto err_vxlan_join;
			}
		}
	}

	return 0;

err_vxlan_join:
	netdev_for_each_lower_dev(br_dev, dev, iter) {
		if (netif_is_vxlan(dev) && netif_running(dev)) {
			if (stop_dev == dev)
				break;
			mlxsw_sp_bridge_vxlan_leave(bridge->mlxsw_sp, dev);
		}
	}
	return err;
}

static void mlxsw_sp_bridge_device_vxlan_fini(struct mlxsw_sp_bridge *bridge,
					      struct net_device *br_dev)
{
	struct net_device *dev;
	struct list_head *iter;

	netdev_for_each_lower_dev(br_dev, dev, iter) {
		if (netif_is_vxlan(dev) && netif_running(dev))
			mlxsw_sp_bridge_vxlan_leave(bridge->mlxsw_sp, dev);
	}
}

static void mlxsw_sp_fdb_notify_work_schedule(struct mlxsw_sp *mlxsw_sp,
					      bool no_delay)
{
	struct mlxsw_sp_bridge *bridge = mlxsw_sp->bridge;
	unsigned int interval = no_delay ? 0 : bridge->fdb_notify.interval;

	mlxsw_core_schedule_dw(&bridge->fdb_notify.dw,
			       msecs_to_jiffies(interval));
}

static struct mlxsw_sp_bridge_device *
mlxsw_sp_bridge_device_create(struct mlxsw_sp_bridge *bridge,
			      struct net_device *br_dev,
			      struct netlink_ext_ack *extack)
{
	struct device *dev = bridge->mlxsw_sp->bus_info->dev;
	struct mlxsw_sp_bridge_device *bridge_device;
	bool vlan_enabled = br_vlan_enabled(br_dev);
	int err;

	if (vlan_enabled && bridge->vlan_enabled_exists) {
		dev_err(dev, "Only one VLAN-aware bridge is supported\n");
		NL_SET_ERR_MSG_MOD(extack, "Only one VLAN-aware bridge is supported");
		return ERR_PTR(-EINVAL);
	}

	bridge_device = kzalloc(sizeof(*bridge_device), GFP_KERNEL);
	if (!bridge_device)
		return ERR_PTR(-ENOMEM);

	err = rhashtable_init(&bridge_device->mdb_ht, &mlxsw_sp_mdb_ht_params);
	if (err)
		goto err_mdb_rhashtable_init;

	bridge_device->dev = br_dev;
	bridge_device->vlan_enabled = vlan_enabled;
	bridge_device->multicast_enabled = br_multicast_enabled(br_dev);
	bridge_device->mrouter = br_multicast_router(br_dev);
	INIT_LIST_HEAD(&bridge_device->ports_list);
	if (vlan_enabled) {
		u16 proto;

		bridge->vlan_enabled_exists = true;
		br_vlan_get_proto(br_dev, &proto);
		if (proto == ETH_P_8021AD)
			bridge_device->ops = bridge->bridge_8021ad_ops;
		else
			bridge_device->ops = bridge->bridge_8021q_ops;
	} else {
		bridge_device->ops = bridge->bridge_8021d_ops;
	}
	INIT_LIST_HEAD(&bridge_device->mdb_list);

	if (list_empty(&bridge->bridges_list))
		mlxsw_sp_fdb_notify_work_schedule(bridge->mlxsw_sp, false);
	list_add(&bridge_device->list, &bridge->bridges_list);

	/* It is possible we already have VXLAN devices enslaved to the bridge.
	 * In which case, we need to replay their configuration as if they were
	 * just now enslaved to the bridge.
	 */
	err = mlxsw_sp_bridge_device_vxlan_init(bridge, br_dev, extack);
	if (err)
		goto err_vxlan_init;

	return bridge_device;

err_vxlan_init:
	list_del(&bridge_device->list);
	if (bridge_device->vlan_enabled)
		bridge->vlan_enabled_exists = false;
	rhashtable_destroy(&bridge_device->mdb_ht);
err_mdb_rhashtable_init:
	kfree(bridge_device);
	return ERR_PTR(err);
}

static void
mlxsw_sp_bridge_device_destroy(struct mlxsw_sp_bridge *bridge,
			       struct mlxsw_sp_bridge_device *bridge_device)
{
	mlxsw_sp_bridge_device_vxlan_fini(bridge, bridge_device->dev);
	mlxsw_sp_bridge_device_rifs_destroy(bridge->mlxsw_sp,
					    bridge_device->dev);
	list_del(&bridge_device->list);
	if (list_empty(&bridge->bridges_list))
		cancel_delayed_work(&bridge->fdb_notify.dw);
	if (bridge_device->vlan_enabled)
		bridge->vlan_enabled_exists = false;
	WARN_ON(!list_empty(&bridge_device->ports_list));
	WARN_ON(!list_empty(&bridge_device->mdb_list));
	rhashtable_destroy(&bridge_device->mdb_ht);
	kfree(bridge_device);
}

static struct mlxsw_sp_bridge_device *
mlxsw_sp_bridge_device_get(struct mlxsw_sp_bridge *bridge,
			   struct net_device *br_dev,
			   struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_bridge_device *bridge_device;

	bridge_device = mlxsw_sp_bridge_device_find(bridge, br_dev);
	if (bridge_device)
		return bridge_device;

	return mlxsw_sp_bridge_device_create(bridge, br_dev, extack);
}

static void
mlxsw_sp_bridge_device_put(struct mlxsw_sp_bridge *bridge,
			   struct mlxsw_sp_bridge_device *bridge_device)
{
	if (list_empty(&bridge_device->ports_list))
		mlxsw_sp_bridge_device_destroy(bridge, bridge_device);
}

static struct mlxsw_sp_bridge_port *
__mlxsw_sp_bridge_port_find(const struct mlxsw_sp_bridge_device *bridge_device,
			    const struct net_device *brport_dev)
{
	struct mlxsw_sp_bridge_port *bridge_port;

	list_for_each_entry(bridge_port, &bridge_device->ports_list, list) {
		if (bridge_port->dev == brport_dev)
			return bridge_port;
	}

	return NULL;
}

struct mlxsw_sp_bridge_port *
mlxsw_sp_bridge_port_find(struct mlxsw_sp_bridge *bridge,
			  struct net_device *brport_dev)
{
	struct net_device *br_dev = netdev_master_upper_dev_get(brport_dev);
	struct mlxsw_sp_bridge_device *bridge_device;

	if (!br_dev)
		return NULL;

	bridge_device = mlxsw_sp_bridge_device_find(bridge, br_dev);
	if (!bridge_device)
		return NULL;

	return __mlxsw_sp_bridge_port_find(bridge_device, brport_dev);
}

static struct mlxsw_sp_bridge_port *
mlxsw_sp_bridge_port_create(struct mlxsw_sp_bridge_device *bridge_device,
			    struct net_device *brport_dev,
			    struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_port *mlxsw_sp_port;
	int err;

	bridge_port = kzalloc(sizeof(*bridge_port), GFP_KERNEL);
	if (!bridge_port)
		return ERR_PTR(-ENOMEM);

	mlxsw_sp_port = mlxsw_sp_port_dev_lower_find(brport_dev);
	bridge_port->lagged = mlxsw_sp_port->lagged;
	if (bridge_port->lagged)
		bridge_port->lag_id = mlxsw_sp_port->lag_id;
	else
		bridge_port->system_port = mlxsw_sp_port->local_port;
	bridge_port->dev = brport_dev;
	bridge_port->bridge_device = bridge_device;
	bridge_port->stp_state = BR_STATE_DISABLED;
	bridge_port->flags = BR_LEARNING | BR_FLOOD | BR_LEARNING_SYNC |
			     BR_MCAST_FLOOD;
	INIT_LIST_HEAD(&bridge_port->vlans_list);
	list_add(&bridge_port->list, &bridge_device->ports_list);
	bridge_port->ref_count = 1;

	err = switchdev_bridge_port_offload(brport_dev, mlxsw_sp_port->dev,
					    NULL, NULL, NULL, false, extack);
	if (err)
		goto err_switchdev_offload;

	return bridge_port;

err_switchdev_offload:
	list_del(&bridge_port->list);
	kfree(bridge_port);
	return ERR_PTR(err);
}

static void
mlxsw_sp_bridge_port_destroy(struct mlxsw_sp_bridge_port *bridge_port)
{
	switchdev_bridge_port_unoffload(bridge_port->dev, NULL, NULL, NULL);
	list_del(&bridge_port->list);
	WARN_ON(!list_empty(&bridge_port->vlans_list));
	kfree(bridge_port);
}

static struct mlxsw_sp_bridge_port *
mlxsw_sp_bridge_port_get(struct mlxsw_sp_bridge *bridge,
			 struct net_device *brport_dev,
			 struct netlink_ext_ack *extack)
{
	struct net_device *br_dev = netdev_master_upper_dev_get(brport_dev);
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	int err;

	bridge_port = mlxsw_sp_bridge_port_find(bridge, brport_dev);
	if (bridge_port) {
		bridge_port->ref_count++;
		return bridge_port;
	}

	bridge_device = mlxsw_sp_bridge_device_get(bridge, br_dev, extack);
	if (IS_ERR(bridge_device))
		return ERR_CAST(bridge_device);

	bridge_port = mlxsw_sp_bridge_port_create(bridge_device, brport_dev,
						  extack);
	if (IS_ERR(bridge_port)) {
		err = PTR_ERR(bridge_port);
		goto err_bridge_port_create;
	}

	return bridge_port;

err_bridge_port_create:
	mlxsw_sp_bridge_device_put(bridge, bridge_device);
	return ERR_PTR(err);
}

static void mlxsw_sp_bridge_port_put(struct mlxsw_sp_bridge *bridge,
				     struct mlxsw_sp_bridge_port *bridge_port)
{
	struct mlxsw_sp_bridge_device *bridge_device;

	if (--bridge_port->ref_count != 0)
		return;
	bridge_device = bridge_port->bridge_device;
	mlxsw_sp_bridge_port_destroy(bridge_port);
	mlxsw_sp_bridge_device_put(bridge, bridge_device);
}

static struct mlxsw_sp_port_vlan *
mlxsw_sp_port_vlan_find_by_bridge(struct mlxsw_sp_port *mlxsw_sp_port,
				  const struct mlxsw_sp_bridge_device *
				  bridge_device,
				  u16 vid)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	list_for_each_entry(mlxsw_sp_port_vlan, &mlxsw_sp_port->vlans_list,
			    list) {
		if (!mlxsw_sp_port_vlan->bridge_port)
			continue;
		if (mlxsw_sp_port_vlan->bridge_port->bridge_device !=
		    bridge_device)
			continue;
		if (bridge_device->vlan_enabled &&
		    mlxsw_sp_port_vlan->vid != vid)
			continue;
		return mlxsw_sp_port_vlan;
	}

	return NULL;
}

static struct mlxsw_sp_port_vlan*
mlxsw_sp_port_vlan_find_by_fid(struct mlxsw_sp_port *mlxsw_sp_port,
			       u16 fid_index)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	list_for_each_entry(mlxsw_sp_port_vlan, &mlxsw_sp_port->vlans_list,
			    list) {
		struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;

		if (fid && mlxsw_sp_fid_index(fid) == fid_index)
			return mlxsw_sp_port_vlan;
	}

	return NULL;
}

static struct mlxsw_sp_bridge_vlan *
mlxsw_sp_bridge_vlan_find(const struct mlxsw_sp_bridge_port *bridge_port,
			  u16 vid)
{
	struct mlxsw_sp_bridge_vlan *bridge_vlan;

	list_for_each_entry(bridge_vlan, &bridge_port->vlans_list, list) {
		if (bridge_vlan->vid == vid)
			return bridge_vlan;
	}

	return NULL;
}

static struct mlxsw_sp_bridge_vlan *
mlxsw_sp_bridge_vlan_create(struct mlxsw_sp_bridge_port *bridge_port, u16 vid)
{
	struct mlxsw_sp_bridge_vlan *bridge_vlan;

	bridge_vlan = kzalloc(sizeof(*bridge_vlan), GFP_KERNEL);
	if (!bridge_vlan)
		return NULL;

	INIT_LIST_HEAD(&bridge_vlan->port_vlan_list);
	bridge_vlan->vid = vid;
	list_add(&bridge_vlan->list, &bridge_port->vlans_list);

	return bridge_vlan;
}

static void
mlxsw_sp_bridge_vlan_destroy(struct mlxsw_sp_bridge_vlan *bridge_vlan)
{
	list_del(&bridge_vlan->list);
	WARN_ON(!list_empty(&bridge_vlan->port_vlan_list));
	kfree(bridge_vlan);
}

static struct mlxsw_sp_bridge_vlan *
mlxsw_sp_bridge_vlan_get(struct mlxsw_sp_bridge_port *bridge_port, u16 vid)
{
	struct mlxsw_sp_bridge_vlan *bridge_vlan;

	bridge_vlan = mlxsw_sp_bridge_vlan_find(bridge_port, vid);
	if (bridge_vlan)
		return bridge_vlan;

	return mlxsw_sp_bridge_vlan_create(bridge_port, vid);
}

static void mlxsw_sp_bridge_vlan_put(struct mlxsw_sp_bridge_vlan *bridge_vlan)
{
	if (list_empty(&bridge_vlan->port_vlan_list))
		mlxsw_sp_bridge_vlan_destroy(bridge_vlan);
}

static int
mlxsw_sp_port_bridge_vlan_stp_set(struct mlxsw_sp_port *mlxsw_sp_port,
				  struct mlxsw_sp_bridge_vlan *bridge_vlan,
				  u8 state)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	list_for_each_entry(mlxsw_sp_port_vlan, &bridge_vlan->port_vlan_list,
			    bridge_vlan_node) {
		if (mlxsw_sp_port_vlan->mlxsw_sp_port != mlxsw_sp_port)
			continue;
		return mlxsw_sp_port_vid_stp_set(mlxsw_sp_port,
						 bridge_vlan->vid, state);
	}

	return 0;
}

static int mlxsw_sp_port_attr_stp_state_set(struct mlxsw_sp_port *mlxsw_sp_port,
					    struct net_device *orig_dev,
					    u8 state)
{
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_bridge_vlan *bridge_vlan;
	int err;

	/* It's possible we failed to enslave the port, yet this
	 * operation is executed due to it being deferred.
	 */
	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp_port->mlxsw_sp->bridge,
						orig_dev);
	if (!bridge_port)
		return 0;

	list_for_each_entry(bridge_vlan, &bridge_port->vlans_list, list) {
		err = mlxsw_sp_port_bridge_vlan_stp_set(mlxsw_sp_port,
							bridge_vlan, state);
		if (err)
			goto err_port_bridge_vlan_stp_set;
	}

	bridge_port->stp_state = state;

	return 0;

err_port_bridge_vlan_stp_set:
	list_for_each_entry_continue_reverse(bridge_vlan,
					     &bridge_port->vlans_list, list)
		mlxsw_sp_port_bridge_vlan_stp_set(mlxsw_sp_port, bridge_vlan,
						  bridge_port->stp_state);
	return err;
}

static int
mlxsw_sp_port_bridge_vlan_flood_set(struct mlxsw_sp_port *mlxsw_sp_port,
				    struct mlxsw_sp_bridge_vlan *bridge_vlan,
				    enum mlxsw_sp_flood_type packet_type,
				    bool member)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	list_for_each_entry(mlxsw_sp_port_vlan, &bridge_vlan->port_vlan_list,
			    bridge_vlan_node) {
		if (mlxsw_sp_port_vlan->mlxsw_sp_port != mlxsw_sp_port)
			continue;
		return mlxsw_sp_fid_flood_set(mlxsw_sp_port_vlan->fid,
					      packet_type,
					      mlxsw_sp_port->local_port,
					      member);
	}

	return 0;
}

static int
mlxsw_sp_bridge_port_flood_table_set(struct mlxsw_sp_port *mlxsw_sp_port,
				     struct mlxsw_sp_bridge_port *bridge_port,
				     enum mlxsw_sp_flood_type packet_type,
				     bool member)
{
	struct mlxsw_sp_bridge_vlan *bridge_vlan;
	int err;

	list_for_each_entry(bridge_vlan, &bridge_port->vlans_list, list) {
		err = mlxsw_sp_port_bridge_vlan_flood_set(mlxsw_sp_port,
							  bridge_vlan,
							  packet_type,
							  member);
		if (err)
			goto err_port_bridge_vlan_flood_set;
	}

	return 0;

err_port_bridge_vlan_flood_set:
	list_for_each_entry_continue_reverse(bridge_vlan,
					     &bridge_port->vlans_list, list)
		mlxsw_sp_port_bridge_vlan_flood_set(mlxsw_sp_port, bridge_vlan,
						    packet_type, !member);
	return err;
}

static int
mlxsw_sp_bridge_vlans_flood_set(struct mlxsw_sp_bridge_vlan *bridge_vlan,
				enum mlxsw_sp_flood_type packet_type,
				bool member)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	int err;

	list_for_each_entry(mlxsw_sp_port_vlan, &bridge_vlan->port_vlan_list,
			    bridge_vlan_node) {
		u16 local_port = mlxsw_sp_port_vlan->mlxsw_sp_port->local_port;

		err = mlxsw_sp_fid_flood_set(mlxsw_sp_port_vlan->fid,
					     packet_type, local_port, member);
		if (err)
			goto err_fid_flood_set;
	}

	return 0;

err_fid_flood_set:
	list_for_each_entry_continue_reverse(mlxsw_sp_port_vlan,
					     &bridge_vlan->port_vlan_list,
					     list) {
		u16 local_port = mlxsw_sp_port_vlan->mlxsw_sp_port->local_port;

		mlxsw_sp_fid_flood_set(mlxsw_sp_port_vlan->fid, packet_type,
				       local_port, !member);
	}

	return err;
}

static int
mlxsw_sp_bridge_ports_flood_table_set(struct mlxsw_sp_bridge_port *bridge_port,
				      enum mlxsw_sp_flood_type packet_type,
				      bool member)
{
	struct mlxsw_sp_bridge_vlan *bridge_vlan;
	int err;

	list_for_each_entry(bridge_vlan, &bridge_port->vlans_list, list) {
		err = mlxsw_sp_bridge_vlans_flood_set(bridge_vlan, packet_type,
						      member);
		if (err)
			goto err_bridge_vlans_flood_set;
	}

	return 0;

err_bridge_vlans_flood_set:
	list_for_each_entry_continue_reverse(bridge_vlan,
					     &bridge_port->vlans_list, list)
		mlxsw_sp_bridge_vlans_flood_set(bridge_vlan, packet_type,
						!member);
	return err;
}

static int
mlxsw_sp_port_bridge_vlan_learning_set(struct mlxsw_sp_port *mlxsw_sp_port,
				       struct mlxsw_sp_bridge_vlan *bridge_vlan,
				       bool set)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	u16 vid = bridge_vlan->vid;

	list_for_each_entry(mlxsw_sp_port_vlan, &bridge_vlan->port_vlan_list,
			    bridge_vlan_node) {
		if (mlxsw_sp_port_vlan->mlxsw_sp_port != mlxsw_sp_port)
			continue;
		return mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid, set);
	}

	return 0;
}

static int
mlxsw_sp_bridge_port_learning_set(struct mlxsw_sp_port *mlxsw_sp_port,
				  struct mlxsw_sp_bridge_port *bridge_port,
				  bool set)
{
	struct mlxsw_sp_bridge_vlan *bridge_vlan;
	int err;

	list_for_each_entry(bridge_vlan, &bridge_port->vlans_list, list) {
		err = mlxsw_sp_port_bridge_vlan_learning_set(mlxsw_sp_port,
							     bridge_vlan, set);
		if (err)
			goto err_port_bridge_vlan_learning_set;
	}

	return 0;

err_port_bridge_vlan_learning_set:
	list_for_each_entry_continue_reverse(bridge_vlan,
					     &bridge_port->vlans_list, list)
		mlxsw_sp_port_bridge_vlan_learning_set(mlxsw_sp_port,
						       bridge_vlan, !set);
	return err;
}

static int
mlxsw_sp_port_attr_br_pre_flags_set(struct mlxsw_sp_port *mlxsw_sp_port,
				    const struct net_device *orig_dev,
				    struct switchdev_brport_flags flags,
				    struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD |
			   BR_PORT_LOCKED | BR_PORT_MAB)) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported bridge port flag");
		return -EINVAL;
	}

	if ((flags.mask & BR_PORT_LOCKED) && is_vlan_dev(orig_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Locked flag cannot be set on a VLAN upper");
		return -EINVAL;
	}

	if ((flags.mask & BR_PORT_LOCKED) && vlan_uses_dev(orig_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Locked flag cannot be set on a bridge port that has VLAN uppers");
		return -EINVAL;
	}

	return 0;
}

static int mlxsw_sp_port_attr_br_flags_set(struct mlxsw_sp_port *mlxsw_sp_port,
					   struct net_device *orig_dev,
					   struct switchdev_brport_flags flags)
{
	struct mlxsw_sp_bridge_port *bridge_port;
	int err;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp_port->mlxsw_sp->bridge,
						orig_dev);
	if (!bridge_port)
		return 0;

	if (flags.mask & BR_FLOOD) {
		err = mlxsw_sp_bridge_port_flood_table_set(mlxsw_sp_port,
							   bridge_port,
							   MLXSW_SP_FLOOD_TYPE_UC,
							   flags.val & BR_FLOOD);
		if (err)
			return err;
	}

	if (flags.mask & BR_LEARNING) {
		err = mlxsw_sp_bridge_port_learning_set(mlxsw_sp_port,
							bridge_port,
							flags.val & BR_LEARNING);
		if (err)
			return err;
	}

	if (flags.mask & BR_PORT_LOCKED) {
		err = mlxsw_sp_port_security_set(mlxsw_sp_port,
						 flags.val & BR_PORT_LOCKED);
		if (err)
			return err;
	}

	if (bridge_port->bridge_device->multicast_enabled)
		goto out;

	if (flags.mask & BR_MCAST_FLOOD) {
		err = mlxsw_sp_bridge_port_flood_table_set(mlxsw_sp_port,
							   bridge_port,
							   MLXSW_SP_FLOOD_TYPE_MC,
							   flags.val & BR_MCAST_FLOOD);
		if (err)
			return err;
	}

out:
	memcpy(&bridge_port->flags, &flags.val, sizeof(flags.val));
	return 0;
}

static int mlxsw_sp_ageing_set(struct mlxsw_sp *mlxsw_sp, u32 ageing_time)
{
	char sfdat_pl[MLXSW_REG_SFDAT_LEN];
	int err;

	mlxsw_reg_sfdat_pack(sfdat_pl, ageing_time);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfdat), sfdat_pl);
	if (err)
		return err;
	mlxsw_sp->bridge->ageing_time = ageing_time;
	return 0;
}

static int mlxsw_sp_port_attr_br_ageing_set(struct mlxsw_sp_port *mlxsw_sp_port,
					    unsigned long ageing_clock_t)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock_t);
	u32 ageing_time = jiffies_to_msecs(ageing_jiffies) / 1000;

	if (ageing_time < MLXSW_SP_MIN_AGEING_TIME ||
	    ageing_time > MLXSW_SP_MAX_AGEING_TIME)
		return -ERANGE;

	return mlxsw_sp_ageing_set(mlxsw_sp, ageing_time);
}

static int mlxsw_sp_port_attr_br_vlan_set(struct mlxsw_sp_port *mlxsw_sp_port,
					  struct net_device *orig_dev,
					  bool vlan_enabled)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, orig_dev);
	if (WARN_ON(!bridge_device))
		return -EINVAL;

	if (bridge_device->vlan_enabled == vlan_enabled)
		return 0;

	netdev_err(bridge_device->dev, "VLAN filtering can't be changed for existing bridge\n");
	return -EINVAL;
}

static int mlxsw_sp_port_attr_br_vlan_proto_set(struct mlxsw_sp_port *mlxsw_sp_port,
						struct net_device *orig_dev,
						u16 vlan_proto)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, orig_dev);
	if (WARN_ON(!bridge_device))
		return -EINVAL;

	netdev_err(bridge_device->dev, "VLAN protocol can't be changed on existing bridge\n");
	return -EINVAL;
}

static int mlxsw_sp_port_attr_mrouter_set(struct mlxsw_sp_port *mlxsw_sp_port,
					  struct net_device *orig_dev,
					  bool is_port_mrouter)
{
	struct mlxsw_sp_bridge_port *bridge_port;
	int err;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp_port->mlxsw_sp->bridge,
						orig_dev);
	if (!bridge_port)
		return 0;

	mlxsw_sp_port_mrouter_update_mdb(mlxsw_sp_port, bridge_port,
					 is_port_mrouter);

	if (!bridge_port->bridge_device->multicast_enabled)
		goto out;

	err = mlxsw_sp_bridge_port_flood_table_set(mlxsw_sp_port, bridge_port,
						   MLXSW_SP_FLOOD_TYPE_MC,
						   is_port_mrouter);
	if (err)
		return err;

out:
	bridge_port->mrouter = is_port_mrouter;
	return 0;
}

static bool mlxsw_sp_mc_flood(const struct mlxsw_sp_bridge_port *bridge_port)
{
	const struct mlxsw_sp_bridge_device *bridge_device;

	bridge_device = bridge_port->bridge_device;
	return bridge_device->multicast_enabled ? bridge_port->mrouter :
					bridge_port->flags & BR_MCAST_FLOOD;
}

static int mlxsw_sp_port_mc_disabled_set(struct mlxsw_sp_port *mlxsw_sp_port,
					 struct net_device *orig_dev,
					 bool mc_disabled)
{
	enum mlxsw_sp_flood_type packet_type = MLXSW_SP_FLOOD_TYPE_MC;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	int err;

	/* It's possible we failed to enslave the port, yet this
	 * operation is executed due to it being deferred.
	 */
	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, orig_dev);
	if (!bridge_device)
		return 0;

	if (bridge_device->multicast_enabled == !mc_disabled)
		return 0;

	bridge_device->multicast_enabled = !mc_disabled;
	err = mlxsw_sp_bridge_mdb_mc_enable_sync(mlxsw_sp, bridge_device,
						 !mc_disabled);
	if (err)
		goto err_mc_enable_sync;

	list_for_each_entry(bridge_port, &bridge_device->ports_list, list) {
		bool member = mlxsw_sp_mc_flood(bridge_port);

		err = mlxsw_sp_bridge_ports_flood_table_set(bridge_port,
							    packet_type,
							    member);
		if (err)
			goto err_flood_table_set;
	}

	return 0;

err_flood_table_set:
	list_for_each_entry_continue_reverse(bridge_port,
					     &bridge_device->ports_list, list) {
		bool member = mlxsw_sp_mc_flood(bridge_port);

		mlxsw_sp_bridge_ports_flood_table_set(bridge_port, packet_type,
						      !member);
	}
	mlxsw_sp_bridge_mdb_mc_enable_sync(mlxsw_sp, bridge_device,
					   mc_disabled);
err_mc_enable_sync:
	bridge_device->multicast_enabled = mc_disabled;
	return err;
}

static struct mlxsw_sp_mdb_entry_port *
mlxsw_sp_mdb_entry_port_lookup(struct mlxsw_sp_mdb_entry *mdb_entry,
			       u16 local_port)
{
	struct mlxsw_sp_mdb_entry_port *mdb_entry_port;

	list_for_each_entry(mdb_entry_port, &mdb_entry->ports_list, list) {
		if (mdb_entry_port->local_port == local_port)
			return mdb_entry_port;
	}

	return NULL;
}

static struct mlxsw_sp_mdb_entry_port *
mlxsw_sp_mdb_entry_port_get(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_mdb_entry *mdb_entry,
			    u16 local_port)
{
	struct mlxsw_sp_mdb_entry_port *mdb_entry_port;
	int err;

	mdb_entry_port = mlxsw_sp_mdb_entry_port_lookup(mdb_entry, local_port);
	if (mdb_entry_port) {
		if (mdb_entry_port->mrouter &&
		    refcount_read(&mdb_entry_port->refcount) == 1)
			mdb_entry->ports_count++;

		refcount_inc(&mdb_entry_port->refcount);
		return mdb_entry_port;
	}

	err = mlxsw_sp_pgt_entry_port_set(mlxsw_sp, mdb_entry->mid,
					  mdb_entry->key.fid, local_port, true);
	if (err)
		return ERR_PTR(err);

	mdb_entry_port = kzalloc(sizeof(*mdb_entry_port), GFP_KERNEL);
	if (!mdb_entry_port) {
		err = -ENOMEM;
		goto err_mdb_entry_port_alloc;
	}

	mdb_entry_port->local_port = local_port;
	refcount_set(&mdb_entry_port->refcount, 1);
	list_add(&mdb_entry_port->list, &mdb_entry->ports_list);
	mdb_entry->ports_count++;

	return mdb_entry_port;

err_mdb_entry_port_alloc:
	mlxsw_sp_pgt_entry_port_set(mlxsw_sp, mdb_entry->mid,
				    mdb_entry->key.fid, local_port, false);
	return ERR_PTR(err);
}

static void
mlxsw_sp_mdb_entry_port_put(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_mdb_entry *mdb_entry,
			    u16 local_port, bool force)
{
	struct mlxsw_sp_mdb_entry_port *mdb_entry_port;

	mdb_entry_port = mlxsw_sp_mdb_entry_port_lookup(mdb_entry, local_port);
	if (!mdb_entry_port)
		return;

	if (!force && !refcount_dec_and_test(&mdb_entry_port->refcount)) {
		if (mdb_entry_port->mrouter &&
		    refcount_read(&mdb_entry_port->refcount) == 1)
			mdb_entry->ports_count--;
		return;
	}

	mdb_entry->ports_count--;
	list_del(&mdb_entry_port->list);
	kfree(mdb_entry_port);
	mlxsw_sp_pgt_entry_port_set(mlxsw_sp, mdb_entry->mid,
				    mdb_entry->key.fid, local_port, false);
}

static __always_unused struct mlxsw_sp_mdb_entry_port *
mlxsw_sp_mdb_entry_mrouter_port_get(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_mdb_entry *mdb_entry,
				    u16 local_port)
{
	struct mlxsw_sp_mdb_entry_port *mdb_entry_port;
	int err;

	mdb_entry_port = mlxsw_sp_mdb_entry_port_lookup(mdb_entry, local_port);
	if (mdb_entry_port) {
		if (!mdb_entry_port->mrouter)
			refcount_inc(&mdb_entry_port->refcount);
		return mdb_entry_port;
	}

	err = mlxsw_sp_pgt_entry_port_set(mlxsw_sp, mdb_entry->mid,
					  mdb_entry->key.fid, local_port, true);
	if (err)
		return ERR_PTR(err);

	mdb_entry_port = kzalloc(sizeof(*mdb_entry_port), GFP_KERNEL);
	if (!mdb_entry_port) {
		err = -ENOMEM;
		goto err_mdb_entry_port_alloc;
	}

	mdb_entry_port->local_port = local_port;
	refcount_set(&mdb_entry_port->refcount, 1);
	mdb_entry_port->mrouter = true;
	list_add(&mdb_entry_port->list, &mdb_entry->ports_list);

	return mdb_entry_port;

err_mdb_entry_port_alloc:
	mlxsw_sp_pgt_entry_port_set(mlxsw_sp, mdb_entry->mid,
				    mdb_entry->key.fid, local_port, false);
	return ERR_PTR(err);
}

static __always_unused void
mlxsw_sp_mdb_entry_mrouter_port_put(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_mdb_entry *mdb_entry,
				    u16 local_port)
{
	struct mlxsw_sp_mdb_entry_port *mdb_entry_port;

	mdb_entry_port = mlxsw_sp_mdb_entry_port_lookup(mdb_entry, local_port);
	if (!mdb_entry_port)
		return;

	if (!mdb_entry_port->mrouter)
		return;

	mdb_entry_port->mrouter = false;
	if (!refcount_dec_and_test(&mdb_entry_port->refcount))
		return;

	list_del(&mdb_entry_port->list);
	kfree(mdb_entry_port);
	mlxsw_sp_pgt_entry_port_set(mlxsw_sp, mdb_entry->mid,
				    mdb_entry->key.fid, local_port, false);
}

static void
mlxsw_sp_bridge_mrouter_update_mdb(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_bridge_device *bridge_device,
				   bool add)
{
	u16 local_port = mlxsw_sp_router_port(mlxsw_sp);
	struct mlxsw_sp_mdb_entry *mdb_entry;

	list_for_each_entry(mdb_entry, &bridge_device->mdb_list, list) {
		if (add)
			mlxsw_sp_mdb_entry_mrouter_port_get(mlxsw_sp, mdb_entry,
							    local_port);
		else
			mlxsw_sp_mdb_entry_mrouter_port_put(mlxsw_sp, mdb_entry,
							    local_port);
	}
}

static int
mlxsw_sp_port_attr_br_mrouter_set(struct mlxsw_sp_port *mlxsw_sp_port,
				  struct net_device *orig_dev,
				  bool is_mrouter)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;

	/* It's possible we failed to enslave the port, yet this
	 * operation is executed due to it being deferred.
	 */
	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, orig_dev);
	if (!bridge_device)
		return 0;

	if (bridge_device->mrouter != is_mrouter)
		mlxsw_sp_bridge_mrouter_update_mdb(mlxsw_sp, bridge_device,
						   is_mrouter);
	bridge_device->mrouter = is_mrouter;
	return 0;
}

static int mlxsw_sp_port_attr_set(struct net_device *dev, const void *ctx,
				  const struct switchdev_attr *attr,
				  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		err = mlxsw_sp_port_attr_stp_state_set(mlxsw_sp_port,
						       attr->orig_dev,
						       attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		err = mlxsw_sp_port_attr_br_pre_flags_set(mlxsw_sp_port,
							  attr->orig_dev,
							  attr->u.brport_flags,
							  extack);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		err = mlxsw_sp_port_attr_br_flags_set(mlxsw_sp_port,
						      attr->orig_dev,
						      attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		err = mlxsw_sp_port_attr_br_ageing_set(mlxsw_sp_port,
						       attr->u.ageing_time);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		err = mlxsw_sp_port_attr_br_vlan_set(mlxsw_sp_port,
						     attr->orig_dev,
						     attr->u.vlan_filtering);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_PROTOCOL:
		err = mlxsw_sp_port_attr_br_vlan_proto_set(mlxsw_sp_port,
							   attr->orig_dev,
							   attr->u.vlan_protocol);
		break;
	case SWITCHDEV_ATTR_ID_PORT_MROUTER:
		err = mlxsw_sp_port_attr_mrouter_set(mlxsw_sp_port,
						     attr->orig_dev,
						     attr->u.mrouter);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED:
		err = mlxsw_sp_port_mc_disabled_set(mlxsw_sp_port,
						    attr->orig_dev,
						    attr->u.mc_disabled);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MROUTER:
		err = mlxsw_sp_port_attr_br_mrouter_set(mlxsw_sp_port,
							attr->orig_dev,
							attr->u.mrouter);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mlxsw_sp_span_respin(mlxsw_sp_port->mlxsw_sp);

	return err;
}

static int
mlxsw_sp_port_vlan_fid_join(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan,
			    struct mlxsw_sp_bridge_port *bridge_port,
			    struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp_bridge_device *bridge_device;
	u16 local_port = mlxsw_sp_port->local_port;
	u16 vid = mlxsw_sp_port_vlan->vid;
	struct mlxsw_sp_fid *fid;
	int err;

	bridge_device = bridge_port->bridge_device;
	fid = bridge_device->ops->fid_get(bridge_device, vid, extack);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	err = mlxsw_sp_fid_flood_set(fid, MLXSW_SP_FLOOD_TYPE_UC, local_port,
				     bridge_port->flags & BR_FLOOD);
	if (err)
		goto err_fid_uc_flood_set;

	err = mlxsw_sp_fid_flood_set(fid, MLXSW_SP_FLOOD_TYPE_MC, local_port,
				     mlxsw_sp_mc_flood(bridge_port));
	if (err)
		goto err_fid_mc_flood_set;

	err = mlxsw_sp_fid_flood_set(fid, MLXSW_SP_FLOOD_TYPE_BC, local_port,
				     true);
	if (err)
		goto err_fid_bc_flood_set;

	err = mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port, vid);
	if (err)
		goto err_fid_port_vid_map;

	mlxsw_sp_port_vlan->fid = fid;

	return 0;

err_fid_port_vid_map:
	mlxsw_sp_fid_flood_set(fid, MLXSW_SP_FLOOD_TYPE_BC, local_port, false);
err_fid_bc_flood_set:
	mlxsw_sp_fid_flood_set(fid, MLXSW_SP_FLOOD_TYPE_MC, local_port, false);
err_fid_mc_flood_set:
	mlxsw_sp_fid_flood_set(fid, MLXSW_SP_FLOOD_TYPE_UC, local_port, false);
err_fid_uc_flood_set:
	mlxsw_sp_fid_put(fid);
	return err;
}

static void
mlxsw_sp_port_vlan_fid_leave(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;
	u16 local_port = mlxsw_sp_port->local_port;
	u16 vid = mlxsw_sp_port_vlan->vid;

	mlxsw_sp_port_vlan->fid = NULL;
	mlxsw_sp_fid_port_vid_unmap(fid, mlxsw_sp_port, vid);
	mlxsw_sp_fid_flood_set(fid, MLXSW_SP_FLOOD_TYPE_BC, local_port, false);
	mlxsw_sp_fid_flood_set(fid, MLXSW_SP_FLOOD_TYPE_MC, local_port, false);
	mlxsw_sp_fid_flood_set(fid, MLXSW_SP_FLOOD_TYPE_UC, local_port, false);
	mlxsw_sp_fid_put(fid);
}

static u16
mlxsw_sp_port_pvid_determine(const struct mlxsw_sp_port *mlxsw_sp_port,
			     u16 vid, bool is_pvid)
{
	if (is_pvid)
		return vid;
	else if (mlxsw_sp_port->pvid == vid)
		return 0;	/* Dis-allow untagged packets */
	else
		return mlxsw_sp_port->pvid;
}

static int
mlxsw_sp_port_vlan_bridge_join(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan,
			       struct mlxsw_sp_bridge_port *bridge_port,
			       struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp_bridge_vlan *bridge_vlan;
	u16 vid = mlxsw_sp_port_vlan->vid;
	int err;

	/* No need to continue if only VLAN flags were changed */
	if (mlxsw_sp_port_vlan->bridge_port)
		return 0;

	err = mlxsw_sp_port_vlan_fid_join(mlxsw_sp_port_vlan, bridge_port,
					  extack);
	if (err)
		return err;

	err = mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid,
					     bridge_port->flags & BR_LEARNING);
	if (err)
		goto err_port_vid_learning_set;

	err = mlxsw_sp_port_vid_stp_set(mlxsw_sp_port, vid,
					bridge_port->stp_state);
	if (err)
		goto err_port_vid_stp_set;

	bridge_vlan = mlxsw_sp_bridge_vlan_get(bridge_port, vid);
	if (!bridge_vlan) {
		err = -ENOMEM;
		goto err_bridge_vlan_get;
	}

	list_add(&mlxsw_sp_port_vlan->bridge_vlan_node,
		 &bridge_vlan->port_vlan_list);

	mlxsw_sp_bridge_port_get(mlxsw_sp_port->mlxsw_sp->bridge,
				 bridge_port->dev, extack);
	mlxsw_sp_port_vlan->bridge_port = bridge_port;

	return 0;

err_bridge_vlan_get:
	mlxsw_sp_port_vid_stp_set(mlxsw_sp_port, vid, BR_STATE_DISABLED);
err_port_vid_stp_set:
	mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid, false);
err_port_vid_learning_set:
	mlxsw_sp_port_vlan_fid_leave(mlxsw_sp_port_vlan);
	return err;
}

void
mlxsw_sp_port_vlan_bridge_leave(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;
	struct mlxsw_sp_bridge_vlan *bridge_vlan;
	struct mlxsw_sp_bridge_port *bridge_port;
	u16 vid = mlxsw_sp_port_vlan->vid;
	bool last_port;

	if (WARN_ON(mlxsw_sp_fid_type(fid) != MLXSW_SP_FID_TYPE_8021Q &&
		    mlxsw_sp_fid_type(fid) != MLXSW_SP_FID_TYPE_8021D))
		return;

	bridge_port = mlxsw_sp_port_vlan->bridge_port;
	bridge_vlan = mlxsw_sp_bridge_vlan_find(bridge_port, vid);
	last_port = list_is_singular(&bridge_vlan->port_vlan_list);

	list_del(&mlxsw_sp_port_vlan->bridge_vlan_node);
	mlxsw_sp_bridge_vlan_put(bridge_vlan);
	mlxsw_sp_port_vid_stp_set(mlxsw_sp_port, vid, BR_STATE_DISABLED);
	mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid, false);
	if (last_port)
		mlxsw_sp_bridge_port_fdb_flush(mlxsw_sp_port->mlxsw_sp,
					       bridge_port,
					       mlxsw_sp_fid_index(fid));

	mlxsw_sp_bridge_port_mdb_flush(mlxsw_sp_port, bridge_port,
				       mlxsw_sp_fid_index(fid));

	mlxsw_sp_port_vlan_fid_leave(mlxsw_sp_port_vlan);

	mlxsw_sp_bridge_port_put(mlxsw_sp_port->mlxsw_sp->bridge, bridge_port);
	mlxsw_sp_port_vlan->bridge_port = NULL;
}

static int
mlxsw_sp_bridge_port_vlan_add(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_bridge_port *bridge_port,
			      u16 vid, bool is_untagged, bool is_pvid,
			      struct netlink_ext_ack *extack)
{
	u16 pvid = mlxsw_sp_port_pvid_determine(mlxsw_sp_port, vid, is_pvid);
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	u16 old_pvid = mlxsw_sp_port->pvid;
	u16 proto;
	int err;

	/* The only valid scenario in which a port-vlan already exists, is if
	 * the VLAN flags were changed and the port-vlan is associated with the
	 * correct bridge port
	 */
	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (mlxsw_sp_port_vlan &&
	    mlxsw_sp_port_vlan->bridge_port != bridge_port)
		return -EEXIST;

	if (!mlxsw_sp_port_vlan) {
		mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_create(mlxsw_sp_port,
							       vid);
		if (IS_ERR(mlxsw_sp_port_vlan))
			return PTR_ERR(mlxsw_sp_port_vlan);
	}

	err = mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid, true,
				     is_untagged);
	if (err)
		goto err_port_vlan_set;

	br_vlan_get_proto(bridge_port->bridge_device->dev, &proto);
	err = mlxsw_sp_port_pvid_set(mlxsw_sp_port, pvid, proto);
	if (err)
		goto err_port_pvid_set;

	err = mlxsw_sp_port_vlan_bridge_join(mlxsw_sp_port_vlan, bridge_port,
					     extack);
	if (err)
		goto err_port_vlan_bridge_join;

	return 0;

err_port_vlan_bridge_join:
	mlxsw_sp_port_pvid_set(mlxsw_sp_port, old_pvid, proto);
err_port_pvid_set:
	mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid, false, false);
err_port_vlan_set:
	mlxsw_sp_port_vlan_destroy(mlxsw_sp_port_vlan);
	return err;
}

static int
mlxsw_sp_br_rif_pvid_change(struct mlxsw_sp *mlxsw_sp,
			    struct net_device *br_dev,
			    const struct switchdev_obj_port_vlan *vlan,
			    struct netlink_ext_ack *extack)
{
	bool flag_pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;

	return mlxsw_sp_router_bridge_vlan_add(mlxsw_sp, br_dev, vlan->vid,
					       flag_pvid, extack);
}

static int mlxsw_sp_port_vlans_add(struct mlxsw_sp_port *mlxsw_sp_port,
				   const struct switchdev_obj_port_vlan *vlan,
				   struct netlink_ext_ack *extack)
{
	bool flag_untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool flag_pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct net_device *orig_dev = vlan->obj.orig_dev;
	struct mlxsw_sp_bridge_port *bridge_port;

	if (netif_is_bridge_master(orig_dev)) {
		int err = 0;

		if (br_vlan_enabled(orig_dev))
			err = mlxsw_sp_br_rif_pvid_change(mlxsw_sp, orig_dev,
							  vlan, extack);
		if (!err)
			err = -EOPNOTSUPP;
		return err;
	}

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp->bridge, orig_dev);
	if (WARN_ON(!bridge_port))
		return -EINVAL;

	if (!bridge_port->bridge_device->vlan_enabled)
		return 0;

	return mlxsw_sp_bridge_port_vlan_add(mlxsw_sp_port, bridge_port,
					     vlan->vid, flag_untagged,
					     flag_pvid, extack);
}

static enum mlxsw_reg_sfdf_flush_type mlxsw_sp_fdb_flush_type(bool lagged)
{
	return lagged ? MLXSW_REG_SFDF_FLUSH_PER_LAG_AND_FID :
			MLXSW_REG_SFDF_FLUSH_PER_PORT_AND_FID;
}

static int
mlxsw_sp_bridge_port_fdb_flush(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_bridge_port *bridge_port,
			       u16 fid_index)
{
	bool lagged = bridge_port->lagged;
	char sfdf_pl[MLXSW_REG_SFDF_LEN];
	u16 system_port;

	system_port = lagged ? bridge_port->lag_id : bridge_port->system_port;
	mlxsw_reg_sfdf_pack(sfdf_pl, mlxsw_sp_fdb_flush_type(lagged));
	mlxsw_reg_sfdf_fid_set(sfdf_pl, fid_index);
	mlxsw_reg_sfdf_port_fid_system_port_set(sfdf_pl, system_port);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfdf), sfdf_pl);
}

static enum mlxsw_reg_sfd_rec_policy mlxsw_sp_sfd_rec_policy(bool dynamic)
{
	return dynamic ? MLXSW_REG_SFD_REC_POLICY_DYNAMIC_ENTRY_INGRESS :
			 MLXSW_REG_SFD_REC_POLICY_DYNAMIC_ENTRY_MLAG;
}

static enum mlxsw_reg_sfd_op mlxsw_sp_sfd_op(bool adding)
{
	return adding ? MLXSW_REG_SFD_OP_WRITE_EDIT :
			MLXSW_REG_SFD_OP_WRITE_REMOVE;
}

static int
mlxsw_sp_port_fdb_tun_uc_op4(struct mlxsw_sp *mlxsw_sp, bool dynamic,
			     const char *mac, u16 fid, __be32 addr, bool adding)
{
	char *sfd_pl;
	u8 num_rec;
	u32 uip;
	int err;

	sfd_pl = kmalloc(MLXSW_REG_SFD_LEN, GFP_KERNEL);
	if (!sfd_pl)
		return -ENOMEM;

	uip = be32_to_cpu(addr);
	mlxsw_reg_sfd_pack(sfd_pl, mlxsw_sp_sfd_op(adding), 0);
	mlxsw_reg_sfd_uc_tunnel_pack4(sfd_pl, 0,
				      mlxsw_sp_sfd_rec_policy(dynamic), mac,
				      fid, MLXSW_REG_SFD_REC_ACTION_NOP, uip);
	num_rec = mlxsw_reg_sfd_num_rec_get(sfd_pl);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfd), sfd_pl);
	if (err)
		goto out;

	if (num_rec != mlxsw_reg_sfd_num_rec_get(sfd_pl))
		err = -EBUSY;

out:
	kfree(sfd_pl);
	return err;
}

static int mlxsw_sp_port_fdb_tun_uc_op6_sfd_write(struct mlxsw_sp *mlxsw_sp,
						  const char *mac, u16 fid,
						  u32 kvdl_index, bool adding)
{
	char *sfd_pl;
	u8 num_rec;
	int err;

	sfd_pl = kmalloc(MLXSW_REG_SFD_LEN, GFP_KERNEL);
	if (!sfd_pl)
		return -ENOMEM;

	mlxsw_reg_sfd_pack(sfd_pl, mlxsw_sp_sfd_op(adding), 0);
	mlxsw_reg_sfd_uc_tunnel_pack6(sfd_pl, 0, mac, fid,
				      MLXSW_REG_SFD_REC_ACTION_NOP, kvdl_index);
	num_rec = mlxsw_reg_sfd_num_rec_get(sfd_pl);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfd), sfd_pl);
	if (err)
		goto out;

	if (num_rec != mlxsw_reg_sfd_num_rec_get(sfd_pl))
		err = -EBUSY;

out:
	kfree(sfd_pl);
	return err;
}

static int mlxsw_sp_port_fdb_tun_uc_op6_add(struct mlxsw_sp *mlxsw_sp,
					    const char *mac, u16 fid,
					    const struct in6_addr *addr)
{
	u32 kvdl_index;
	int err;

	err = mlxsw_sp_nve_ipv6_addr_kvdl_set(mlxsw_sp, addr, &kvdl_index);
	if (err)
		return err;

	err = mlxsw_sp_port_fdb_tun_uc_op6_sfd_write(mlxsw_sp, mac, fid,
						     kvdl_index, true);
	if (err)
		goto err_sfd_write;

	err = mlxsw_sp_nve_ipv6_addr_map_replace(mlxsw_sp, mac, fid, addr);
	if (err)
		/* Replace can fail only for creating new mapping, so removing
		 * the FDB entry in the error path is OK.
		 */
		goto err_addr_replace;

	return 0;

err_addr_replace:
	mlxsw_sp_port_fdb_tun_uc_op6_sfd_write(mlxsw_sp, mac, fid, kvdl_index,
					       false);
err_sfd_write:
	mlxsw_sp_nve_ipv6_addr_kvdl_unset(mlxsw_sp, addr);
	return err;
}

static void mlxsw_sp_port_fdb_tun_uc_op6_del(struct mlxsw_sp *mlxsw_sp,
					     const char *mac, u16 fid,
					     const struct in6_addr *addr)
{
	mlxsw_sp_nve_ipv6_addr_map_del(mlxsw_sp, mac, fid);
	mlxsw_sp_port_fdb_tun_uc_op6_sfd_write(mlxsw_sp, mac, fid, 0, false);
	mlxsw_sp_nve_ipv6_addr_kvdl_unset(mlxsw_sp, addr);
}

static int
mlxsw_sp_port_fdb_tun_uc_op6(struct mlxsw_sp *mlxsw_sp, const char *mac,
			     u16 fid, const struct in6_addr *addr, bool adding)
{
	if (adding)
		return mlxsw_sp_port_fdb_tun_uc_op6_add(mlxsw_sp, mac, fid,
							addr);

	mlxsw_sp_port_fdb_tun_uc_op6_del(mlxsw_sp, mac, fid, addr);
	return 0;
}

static int mlxsw_sp_port_fdb_tunnel_uc_op(struct mlxsw_sp *mlxsw_sp,
					  const char *mac, u16 fid,
					  enum mlxsw_sp_l3proto proto,
					  const union mlxsw_sp_l3addr *addr,
					  bool adding, bool dynamic)
{
	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		return mlxsw_sp_port_fdb_tun_uc_op4(mlxsw_sp, dynamic, mac, fid,
						    addr->addr4, adding);
	case MLXSW_SP_L3_PROTO_IPV6:
		return mlxsw_sp_port_fdb_tun_uc_op6(mlxsw_sp, mac, fid,
						    &addr->addr6, adding);
	default:
		WARN_ON(1);
		return -EOPNOTSUPP;
	}
}

static int __mlxsw_sp_port_fdb_uc_op(struct mlxsw_sp *mlxsw_sp, u16 local_port,
				     const char *mac, u16 fid, u16 vid,
				     bool adding,
				     enum mlxsw_reg_sfd_rec_action action,
				     enum mlxsw_reg_sfd_rec_policy policy)
{
	char *sfd_pl;
	u8 num_rec;
	int err;

	sfd_pl = kmalloc(MLXSW_REG_SFD_LEN, GFP_KERNEL);
	if (!sfd_pl)
		return -ENOMEM;

	mlxsw_reg_sfd_pack(sfd_pl, mlxsw_sp_sfd_op(adding), 0);
	mlxsw_reg_sfd_uc_pack(sfd_pl, 0, policy, mac, fid, vid, action,
			      local_port);
	num_rec = mlxsw_reg_sfd_num_rec_get(sfd_pl);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfd), sfd_pl);
	if (err)
		goto out;

	if (num_rec != mlxsw_reg_sfd_num_rec_get(sfd_pl))
		err = -EBUSY;

out:
	kfree(sfd_pl);
	return err;
}

static int mlxsw_sp_port_fdb_uc_op(struct mlxsw_sp *mlxsw_sp, u16 local_port,
				   const char *mac, u16 fid, u16 vid,
				   bool adding, bool dynamic)
{
	return __mlxsw_sp_port_fdb_uc_op(mlxsw_sp, local_port, mac, fid, vid,
					 adding, MLXSW_REG_SFD_REC_ACTION_NOP,
					 mlxsw_sp_sfd_rec_policy(dynamic));
}

int mlxsw_sp_rif_fdb_op(struct mlxsw_sp *mlxsw_sp, const char *mac, u16 fid,
			bool adding)
{
	return __mlxsw_sp_port_fdb_uc_op(mlxsw_sp, 0, mac, fid, 0, adding,
					 MLXSW_REG_SFD_REC_ACTION_FORWARD_IP_ROUTER,
					 MLXSW_REG_SFD_REC_POLICY_STATIC_ENTRY);
}

static int mlxsw_sp_port_fdb_uc_lag_op(struct mlxsw_sp *mlxsw_sp, u16 lag_id,
				       const char *mac, u16 fid, u16 lag_vid,
				       bool adding, bool dynamic)
{
	char *sfd_pl;
	u8 num_rec;
	int err;

	sfd_pl = kmalloc(MLXSW_REG_SFD_LEN, GFP_KERNEL);
	if (!sfd_pl)
		return -ENOMEM;

	mlxsw_reg_sfd_pack(sfd_pl, mlxsw_sp_sfd_op(adding), 0);
	mlxsw_reg_sfd_uc_lag_pack(sfd_pl, 0, mlxsw_sp_sfd_rec_policy(dynamic),
				  mac, fid, MLXSW_REG_SFD_REC_ACTION_NOP,
				  lag_vid, lag_id);
	num_rec = mlxsw_reg_sfd_num_rec_get(sfd_pl);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfd), sfd_pl);
	if (err)
		goto out;

	if (num_rec != mlxsw_reg_sfd_num_rec_get(sfd_pl))
		err = -EBUSY;

out:
	kfree(sfd_pl);
	return err;
}

static int
mlxsw_sp_port_fdb_set(struct mlxsw_sp_port *mlxsw_sp_port,
		      struct switchdev_notifier_fdb_info *fdb_info, bool adding)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct net_device *orig_dev = fdb_info->info.dev;
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	u16 fid_index, vid;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp->bridge, orig_dev);
	if (!bridge_port)
		return -EINVAL;

	bridge_device = bridge_port->bridge_device;
	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_bridge(mlxsw_sp_port,
							       bridge_device,
							       fdb_info->vid);
	if (!mlxsw_sp_port_vlan)
		return 0;

	fid_index = mlxsw_sp_fid_index(mlxsw_sp_port_vlan->fid);
	vid = mlxsw_sp_port_vlan->vid;

	if (!bridge_port->lagged)
		return mlxsw_sp_port_fdb_uc_op(mlxsw_sp,
					       bridge_port->system_port,
					       fdb_info->addr, fid_index, vid,
					       adding, false);
	else
		return mlxsw_sp_port_fdb_uc_lag_op(mlxsw_sp,
						   bridge_port->lag_id,
						   fdb_info->addr, fid_index,
						   vid, adding, false);
}

static int mlxsw_sp_mdb_entry_write(struct mlxsw_sp *mlxsw_sp,
				    const struct mlxsw_sp_mdb_entry *mdb_entry,
				    bool adding)
{
	char *sfd_pl;
	u8 num_rec;
	int err;

	sfd_pl = kmalloc(MLXSW_REG_SFD_LEN, GFP_KERNEL);
	if (!sfd_pl)
		return -ENOMEM;

	mlxsw_reg_sfd_pack(sfd_pl, mlxsw_sp_sfd_op(adding), 0);
	mlxsw_reg_sfd_mc_pack(sfd_pl, 0, mdb_entry->key.addr,
			      mdb_entry->key.fid, MLXSW_REG_SFD_REC_ACTION_NOP,
			      mdb_entry->mid);
	num_rec = mlxsw_reg_sfd_num_rec_get(sfd_pl);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfd), sfd_pl);
	if (err)
		goto out;

	if (num_rec != mlxsw_reg_sfd_num_rec_get(sfd_pl))
		err = -EBUSY;

out:
	kfree(sfd_pl);
	return err;
}

static void
mlxsw_sp_bridge_port_get_ports_bitmap(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_bridge_port *bridge_port,
				      struct mlxsw_sp_ports_bitmap *ports_bm)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	u64 max_lag_members, i;
	int lag_id;

	if (!bridge_port->lagged) {
		set_bit(bridge_port->system_port, ports_bm->bitmap);
	} else {
		max_lag_members = MLXSW_CORE_RES_GET(mlxsw_sp->core,
						     MAX_LAG_MEMBERS);
		lag_id = bridge_port->lag_id;
		for (i = 0; i < max_lag_members; i++) {
			mlxsw_sp_port = mlxsw_sp_port_lagged_get(mlxsw_sp,
								 lag_id, i);
			if (mlxsw_sp_port)
				set_bit(mlxsw_sp_port->local_port,
					ports_bm->bitmap);
		}
	}
}

static void
mlxsw_sp_mc_get_mrouters_bitmap(struct mlxsw_sp_ports_bitmap *flood_bm,
				struct mlxsw_sp_bridge_device *bridge_device,
				struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_bridge_port *bridge_port;

	list_for_each_entry(bridge_port, &bridge_device->ports_list, list) {
		if (bridge_port->mrouter) {
			mlxsw_sp_bridge_port_get_ports_bitmap(mlxsw_sp,
							      bridge_port,
							      flood_bm);
		}
	}
}

static int mlxsw_sp_mc_mdb_mrouters_add(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_ports_bitmap *ports_bm,
					struct mlxsw_sp_mdb_entry *mdb_entry)
{
	struct mlxsw_sp_mdb_entry_port *mdb_entry_port;
	unsigned int nbits = ports_bm->nbits;
	int i;

	for_each_set_bit(i, ports_bm->bitmap, nbits) {
		mdb_entry_port = mlxsw_sp_mdb_entry_mrouter_port_get(mlxsw_sp,
								     mdb_entry,
								     i);
		if (IS_ERR(mdb_entry_port)) {
			nbits = i;
			goto err_mrouter_port_get;
		}
	}

	return 0;

err_mrouter_port_get:
	for_each_set_bit(i, ports_bm->bitmap, nbits)
		mlxsw_sp_mdb_entry_mrouter_port_put(mlxsw_sp, mdb_entry, i);
	return PTR_ERR(mdb_entry_port);
}

static void mlxsw_sp_mc_mdb_mrouters_del(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_ports_bitmap *ports_bm,
					 struct mlxsw_sp_mdb_entry *mdb_entry)
{
	int i;

	for_each_set_bit(i, ports_bm->bitmap, ports_bm->nbits)
		mlxsw_sp_mdb_entry_mrouter_port_put(mlxsw_sp, mdb_entry, i);
}

static int
mlxsw_sp_mc_mdb_mrouters_set(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_bridge_device *bridge_device,
			     struct mlxsw_sp_mdb_entry *mdb_entry, bool add)
{
	struct mlxsw_sp_ports_bitmap ports_bm;
	int err;

	err = mlxsw_sp_port_bitmap_init(mlxsw_sp, &ports_bm);
	if (err)
		return err;

	mlxsw_sp_mc_get_mrouters_bitmap(&ports_bm, bridge_device, mlxsw_sp);

	if (add)
		err = mlxsw_sp_mc_mdb_mrouters_add(mlxsw_sp, &ports_bm,
						   mdb_entry);
	else
		mlxsw_sp_mc_mdb_mrouters_del(mlxsw_sp, &ports_bm, mdb_entry);

	mlxsw_sp_port_bitmap_fini(&ports_bm);
	return err;
}

static struct mlxsw_sp_mdb_entry *
mlxsw_sp_mc_mdb_entry_init(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_bridge_device *bridge_device,
			   const unsigned char *addr, u16 fid, u16 local_port)
{
	struct mlxsw_sp_mdb_entry_port *mdb_entry_port;
	struct mlxsw_sp_mdb_entry *mdb_entry;
	int err;

	mdb_entry = kzalloc(sizeof(*mdb_entry), GFP_KERNEL);
	if (!mdb_entry)
		return ERR_PTR(-ENOMEM);

	ether_addr_copy(mdb_entry->key.addr, addr);
	mdb_entry->key.fid = fid;
	err = mlxsw_sp_pgt_mid_alloc(mlxsw_sp, &mdb_entry->mid);
	if (err)
		goto err_pgt_mid_alloc;

	INIT_LIST_HEAD(&mdb_entry->ports_list);

	err = mlxsw_sp_mc_mdb_mrouters_set(mlxsw_sp, bridge_device, mdb_entry,
					   true);
	if (err)
		goto err_mdb_mrouters_set;

	mdb_entry_port = mlxsw_sp_mdb_entry_port_get(mlxsw_sp, mdb_entry,
						     local_port);
	if (IS_ERR(mdb_entry_port)) {
		err = PTR_ERR(mdb_entry_port);
		goto err_mdb_entry_port_get;
	}

	if (bridge_device->multicast_enabled) {
		err = mlxsw_sp_mdb_entry_write(mlxsw_sp, mdb_entry, true);
		if (err)
			goto err_mdb_entry_write;
	}

	err = rhashtable_insert_fast(&bridge_device->mdb_ht,
				     &mdb_entry->ht_node,
				     mlxsw_sp_mdb_ht_params);
	if (err)
		goto err_rhashtable_insert;

	list_add_tail(&mdb_entry->list, &bridge_device->mdb_list);

	return mdb_entry;

err_rhashtable_insert:
	if (bridge_device->multicast_enabled)
		mlxsw_sp_mdb_entry_write(mlxsw_sp, mdb_entry, false);
err_mdb_entry_write:
	mlxsw_sp_mdb_entry_port_put(mlxsw_sp, mdb_entry, local_port, false);
err_mdb_entry_port_get:
	mlxsw_sp_mc_mdb_mrouters_set(mlxsw_sp, bridge_device, mdb_entry, false);
err_mdb_mrouters_set:
	mlxsw_sp_pgt_mid_free(mlxsw_sp, mdb_entry->mid);
err_pgt_mid_alloc:
	kfree(mdb_entry);
	return ERR_PTR(err);
}

static void
mlxsw_sp_mc_mdb_entry_fini(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_mdb_entry *mdb_entry,
			   struct mlxsw_sp_bridge_device *bridge_device,
			   u16 local_port, bool force)
{
	list_del(&mdb_entry->list);
	rhashtable_remove_fast(&bridge_device->mdb_ht, &mdb_entry->ht_node,
			       mlxsw_sp_mdb_ht_params);
	if (bridge_device->multicast_enabled)
		mlxsw_sp_mdb_entry_write(mlxsw_sp, mdb_entry, false);
	mlxsw_sp_mdb_entry_port_put(mlxsw_sp, mdb_entry, local_port, force);
	mlxsw_sp_mc_mdb_mrouters_set(mlxsw_sp, bridge_device, mdb_entry, false);
	WARN_ON(!list_empty(&mdb_entry->ports_list));
	mlxsw_sp_pgt_mid_free(mlxsw_sp, mdb_entry->mid);
	kfree(mdb_entry);
}

static struct mlxsw_sp_mdb_entry *
mlxsw_sp_mc_mdb_entry_get(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_bridge_device *bridge_device,
			  const unsigned char *addr, u16 fid, u16 local_port)
{
	struct mlxsw_sp_mdb_entry_key key = {};
	struct mlxsw_sp_mdb_entry *mdb_entry;

	ether_addr_copy(key.addr, addr);
	key.fid = fid;
	mdb_entry = rhashtable_lookup_fast(&bridge_device->mdb_ht, &key,
					   mlxsw_sp_mdb_ht_params);
	if (mdb_entry) {
		struct mlxsw_sp_mdb_entry_port *mdb_entry_port;

		mdb_entry_port = mlxsw_sp_mdb_entry_port_get(mlxsw_sp,
							     mdb_entry,
							     local_port);
		if (IS_ERR(mdb_entry_port))
			return ERR_CAST(mdb_entry_port);

		return mdb_entry;
	}

	return mlxsw_sp_mc_mdb_entry_init(mlxsw_sp, bridge_device, addr, fid,
					  local_port);
}

static bool
mlxsw_sp_mc_mdb_entry_remove(struct mlxsw_sp_mdb_entry *mdb_entry,
			     struct mlxsw_sp_mdb_entry_port *removed_entry_port,
			     bool force)
{
	if (mdb_entry->ports_count > 1)
		return false;

	if (force)
		return true;

	if (!removed_entry_port->mrouter &&
	    refcount_read(&removed_entry_port->refcount) > 1)
		return false;

	if (removed_entry_port->mrouter &&
	    refcount_read(&removed_entry_port->refcount) > 2)
		return false;

	return true;
}

static void
mlxsw_sp_mc_mdb_entry_put(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_bridge_device *bridge_device,
			  struct mlxsw_sp_mdb_entry *mdb_entry, u16 local_port,
			  bool force)
{
	struct mlxsw_sp_mdb_entry_port *mdb_entry_port;

	mdb_entry_port = mlxsw_sp_mdb_entry_port_lookup(mdb_entry, local_port);
	if (!mdb_entry_port)
		return;

	/* Avoid a temporary situation in which the MDB entry points to an empty
	 * PGT entry, as otherwise packets will be temporarily dropped instead
	 * of being flooded. Instead, in this situation, call
	 * mlxsw_sp_mc_mdb_entry_fini(), which first deletes the MDB entry and
	 * then releases the PGT entry.
	 */
	if (mlxsw_sp_mc_mdb_entry_remove(mdb_entry, mdb_entry_port, force))
		mlxsw_sp_mc_mdb_entry_fini(mlxsw_sp, mdb_entry, bridge_device,
					   local_port, force);
	else
		mlxsw_sp_mdb_entry_port_put(mlxsw_sp, mdb_entry, local_port,
					    force);
}

static int mlxsw_sp_port_mdb_add(struct mlxsw_sp_port *mlxsw_sp_port,
				 const struct switchdev_obj_port_mdb *mdb)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct net_device *orig_dev = mdb->obj.orig_dev;
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_mdb_entry *mdb_entry;
	u16 fid_index;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp->bridge, orig_dev);
	if (!bridge_port)
		return 0;

	bridge_device = bridge_port->bridge_device;
	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_bridge(mlxsw_sp_port,
							       bridge_device,
							       mdb->vid);
	if (!mlxsw_sp_port_vlan)
		return 0;

	fid_index = mlxsw_sp_fid_index(mlxsw_sp_port_vlan->fid);

	mdb_entry = mlxsw_sp_mc_mdb_entry_get(mlxsw_sp, bridge_device,
					      mdb->addr, fid_index,
					      mlxsw_sp_port->local_port);
	if (IS_ERR(mdb_entry))
		return PTR_ERR(mdb_entry);

	return 0;
}

static int
mlxsw_sp_bridge_mdb_mc_enable_sync(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_bridge_device *bridge_device,
				   bool mc_enabled)
{
	struct mlxsw_sp_mdb_entry *mdb_entry;
	int err;

	list_for_each_entry(mdb_entry, &bridge_device->mdb_list, list) {
		err = mlxsw_sp_mdb_entry_write(mlxsw_sp, mdb_entry, mc_enabled);
		if (err)
			goto err_mdb_entry_write;
	}
	return 0;

err_mdb_entry_write:
	list_for_each_entry_continue_reverse(mdb_entry,
					     &bridge_device->mdb_list, list)
		mlxsw_sp_mdb_entry_write(mlxsw_sp, mdb_entry, !mc_enabled);
	return err;
}

static void
mlxsw_sp_port_mrouter_update_mdb(struct mlxsw_sp_port *mlxsw_sp_port,
				 struct mlxsw_sp_bridge_port *bridge_port,
				 bool add)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;
	u16 local_port = mlxsw_sp_port->local_port;
	struct mlxsw_sp_mdb_entry *mdb_entry;

	bridge_device = bridge_port->bridge_device;

	list_for_each_entry(mdb_entry, &bridge_device->mdb_list, list) {
		if (add)
			mlxsw_sp_mdb_entry_mrouter_port_get(mlxsw_sp, mdb_entry,
							    local_port);
		else
			mlxsw_sp_mdb_entry_mrouter_port_put(mlxsw_sp, mdb_entry,
							    local_port);
	}
}

static int mlxsw_sp_port_obj_add(struct net_device *dev, const void *ctx,
				 const struct switchdev_obj *obj,
				 struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	const struct switchdev_obj_port_vlan *vlan;
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);

		err = mlxsw_sp_port_vlans_add(mlxsw_sp_port, vlan, extack);

		/* The event is emitted before the changes are actually
		 * applied to the bridge. Therefore schedule the respin
		 * call for later, so that the respin logic sees the
		 * updated bridge state.
		 */
		mlxsw_sp_span_respin(mlxsw_sp_port->mlxsw_sp);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = mlxsw_sp_port_mdb_add(mlxsw_sp_port,
					    SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static void
mlxsw_sp_bridge_port_vlan_del(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_bridge_port *bridge_port, u16 vid)
{
	u16 pvid = mlxsw_sp_port->pvid == vid ? 0 : mlxsw_sp_port->pvid;
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	u16 proto;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_port_vlan))
		return;

	mlxsw_sp_port_vlan_bridge_leave(mlxsw_sp_port_vlan);
	br_vlan_get_proto(bridge_port->bridge_device->dev, &proto);
	mlxsw_sp_port_pvid_set(mlxsw_sp_port, pvid, proto);
	mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid, false, false);
	mlxsw_sp_port_vlan_destroy(mlxsw_sp_port_vlan);
}

static int mlxsw_sp_port_vlans_del(struct mlxsw_sp_port *mlxsw_sp_port,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct net_device *orig_dev = vlan->obj.orig_dev;
	struct mlxsw_sp_bridge_port *bridge_port;

	if (netif_is_bridge_master(orig_dev))
		return -EOPNOTSUPP;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp->bridge, orig_dev);
	if (WARN_ON(!bridge_port))
		return -EINVAL;

	if (!bridge_port->bridge_device->vlan_enabled)
		return 0;

	mlxsw_sp_bridge_port_vlan_del(mlxsw_sp_port, bridge_port, vlan->vid);

	return 0;
}

static int mlxsw_sp_port_mdb_del(struct mlxsw_sp_port *mlxsw_sp_port,
				 const struct switchdev_obj_port_mdb *mdb)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct net_device *orig_dev = mdb->obj.orig_dev;
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct net_device *dev = mlxsw_sp_port->dev;
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_mdb_entry_key key = {};
	struct mlxsw_sp_mdb_entry *mdb_entry;
	u16 fid_index;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp->bridge, orig_dev);
	if (!bridge_port)
		return 0;

	bridge_device = bridge_port->bridge_device;
	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_bridge(mlxsw_sp_port,
							       bridge_device,
							       mdb->vid);
	if (!mlxsw_sp_port_vlan)
		return 0;

	fid_index = mlxsw_sp_fid_index(mlxsw_sp_port_vlan->fid);

	ether_addr_copy(key.addr, mdb->addr);
	key.fid = fid_index;
	mdb_entry = rhashtable_lookup_fast(&bridge_device->mdb_ht, &key,
					   mlxsw_sp_mdb_ht_params);
	if (!mdb_entry) {
		netdev_err(dev, "Unable to remove port from MC DB\n");
		return -EINVAL;
	}

	mlxsw_sp_mc_mdb_entry_put(mlxsw_sp, bridge_device, mdb_entry,
				  mlxsw_sp_port->local_port, false);
	return 0;
}

static void
mlxsw_sp_bridge_port_mdb_flush(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct mlxsw_sp_bridge_port *bridge_port,
			       u16 fid_index)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_mdb_entry *mdb_entry, *tmp;
	u16 local_port = mlxsw_sp_port->local_port;

	bridge_device = bridge_port->bridge_device;

	list_for_each_entry_safe(mdb_entry, tmp, &bridge_device->mdb_list,
				 list) {
		if (mdb_entry->key.fid != fid_index)
			continue;

		if (bridge_port->mrouter)
			mlxsw_sp_mdb_entry_mrouter_port_put(mlxsw_sp,
							    mdb_entry,
							    local_port);

		mlxsw_sp_mc_mdb_entry_put(mlxsw_sp, bridge_device, mdb_entry,
					  local_port, true);
	}
}

static int mlxsw_sp_port_obj_del(struct net_device *dev, const void *ctx,
				 const struct switchdev_obj *obj)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = mlxsw_sp_port_vlans_del(mlxsw_sp_port,
					      SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = mlxsw_sp_port_mdb_del(mlxsw_sp_port,
					    SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mlxsw_sp_span_respin(mlxsw_sp_port->mlxsw_sp);

	return err;
}

static struct mlxsw_sp_port *mlxsw_sp_lag_rep_port(struct mlxsw_sp *mlxsw_sp,
						   u16 lag_id)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	u64 max_lag_members;
	int i;

	max_lag_members = MLXSW_CORE_RES_GET(mlxsw_sp->core,
					     MAX_LAG_MEMBERS);
	for (i = 0; i < max_lag_members; i++) {
		mlxsw_sp_port = mlxsw_sp_port_lagged_get(mlxsw_sp, lag_id, i);
		if (mlxsw_sp_port)
			return mlxsw_sp_port;
	}
	return NULL;
}

static int
mlxsw_sp_bridge_vlan_aware_port_join(struct mlxsw_sp_bridge_port *bridge_port,
				     struct mlxsw_sp_port *mlxsw_sp_port,
				     struct netlink_ext_ack *extack)
{
	if (is_vlan_dev(bridge_port->dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Can not enslave a VLAN device to a VLAN-aware bridge");
		return -EINVAL;
	}

	/* Port is no longer usable as a router interface */
	if (mlxsw_sp_port->default_vlan->fid)
		mlxsw_sp_port_vlan_router_leave(mlxsw_sp_port->default_vlan);

	return 0;
}

static int
mlxsw_sp_bridge_8021q_port_join(struct mlxsw_sp_bridge_device *bridge_device,
				struct mlxsw_sp_bridge_port *bridge_port,
				struct mlxsw_sp_port *mlxsw_sp_port,
				struct netlink_ext_ack *extack)
{
	return mlxsw_sp_bridge_vlan_aware_port_join(bridge_port, mlxsw_sp_port,
						    extack);
}

static void
mlxsw_sp_bridge_vlan_aware_port_leave(struct mlxsw_sp_port *mlxsw_sp_port)
{
	/* Make sure untagged frames are allowed to ingress */
	mlxsw_sp_port_pvid_set(mlxsw_sp_port, MLXSW_SP_DEFAULT_VID,
			       ETH_P_8021Q);
}

static void
mlxsw_sp_bridge_8021q_port_leave(struct mlxsw_sp_bridge_device *bridge_device,
				 struct mlxsw_sp_bridge_port *bridge_port,
				 struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_bridge_vlan_aware_port_leave(mlxsw_sp_port);
}

static int
mlxsw_sp_bridge_vlan_aware_vxlan_join(struct mlxsw_sp_bridge_device *bridge_device,
				      const struct net_device *vxlan_dev,
				      u16 vid, u16 ethertype,
				      struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(bridge_device->dev);
	struct vxlan_dev *vxlan = netdev_priv(vxlan_dev);
	struct mlxsw_sp_nve_params params = {
		.type = MLXSW_SP_NVE_TYPE_VXLAN,
		.vni = vxlan->cfg.vni,
		.dev = vxlan_dev,
		.ethertype = ethertype,
	};
	struct mlxsw_sp_fid *fid;
	int err;

	/* If the VLAN is 0, we need to find the VLAN that is configured as
	 * PVID and egress untagged on the bridge port of the VxLAN device.
	 * It is possible no such VLAN exists
	 */
	if (!vid) {
		err = mlxsw_sp_vxlan_mapped_vid(vxlan_dev, &vid);
		if (err || !vid)
			return err;
	}

	fid = mlxsw_sp_fid_8021q_get(mlxsw_sp, vid);
	if (IS_ERR(fid)) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to create 802.1Q FID");
		return PTR_ERR(fid);
	}

	if (mlxsw_sp_fid_vni_is_set(fid)) {
		NL_SET_ERR_MSG_MOD(extack, "VNI is already set on FID");
		err = -EINVAL;
		goto err_vni_exists;
	}

	err = mlxsw_sp_nve_fid_enable(mlxsw_sp, fid, &params, extack);
	if (err)
		goto err_nve_fid_enable;

	return 0;

err_nve_fid_enable:
err_vni_exists:
	mlxsw_sp_fid_put(fid);
	return err;
}

static int
mlxsw_sp_bridge_8021q_vxlan_join(struct mlxsw_sp_bridge_device *bridge_device,
				 const struct net_device *vxlan_dev, u16 vid,
				 struct netlink_ext_ack *extack)
{
	return mlxsw_sp_bridge_vlan_aware_vxlan_join(bridge_device, vxlan_dev,
						     vid, ETH_P_8021Q, extack);
}

static struct net_device *
mlxsw_sp_bridge_8021q_vxlan_dev_find(struct net_device *br_dev, u16 vid)
{
	struct net_device *dev;
	struct list_head *iter;

	netdev_for_each_lower_dev(br_dev, dev, iter) {
		u16 pvid;
		int err;

		if (!netif_is_vxlan(dev))
			continue;

		err = mlxsw_sp_vxlan_mapped_vid(dev, &pvid);
		if (err || pvid != vid)
			continue;

		return dev;
	}

	return NULL;
}

static struct mlxsw_sp_fid *
mlxsw_sp_bridge_8021q_fid_get(struct mlxsw_sp_bridge_device *bridge_device,
			      u16 vid, struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(bridge_device->dev);

	return mlxsw_sp_fid_8021q_get(mlxsw_sp, vid);
}

static struct mlxsw_sp_fid *
mlxsw_sp_bridge_8021q_fid_lookup(struct mlxsw_sp_bridge_device *bridge_device,
				 u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(bridge_device->dev);

	return mlxsw_sp_fid_8021q_lookup(mlxsw_sp, vid);
}

static u16
mlxsw_sp_bridge_8021q_fid_vid(struct mlxsw_sp_bridge_device *bridge_device,
			      const struct mlxsw_sp_fid *fid)
{
	return mlxsw_sp_fid_8021q_vid(fid);
}

static const struct mlxsw_sp_bridge_ops mlxsw_sp_bridge_8021q_ops = {
	.port_join	= mlxsw_sp_bridge_8021q_port_join,
	.port_leave	= mlxsw_sp_bridge_8021q_port_leave,
	.vxlan_join	= mlxsw_sp_bridge_8021q_vxlan_join,
	.fid_get	= mlxsw_sp_bridge_8021q_fid_get,
	.fid_lookup	= mlxsw_sp_bridge_8021q_fid_lookup,
	.fid_vid	= mlxsw_sp_bridge_8021q_fid_vid,
};

static bool
mlxsw_sp_port_is_br_member(const struct mlxsw_sp_port *mlxsw_sp_port,
			   const struct net_device *br_dev)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	list_for_each_entry(mlxsw_sp_port_vlan, &mlxsw_sp_port->vlans_list,
			    list) {
		if (mlxsw_sp_port_vlan->bridge_port &&
		    mlxsw_sp_port_vlan->bridge_port->bridge_device->dev ==
		    br_dev)
			return true;
	}

	return false;
}

static int
mlxsw_sp_bridge_8021d_port_join(struct mlxsw_sp_bridge_device *bridge_device,
				struct mlxsw_sp_bridge_port *bridge_port,
				struct mlxsw_sp_port *mlxsw_sp_port,
				struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct net_device *dev = bridge_port->dev;
	u16 vid;

	vid = is_vlan_dev(dev) ? vlan_dev_vlan_id(dev) : MLXSW_SP_DEFAULT_VID;
	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_port_vlan))
		return -EINVAL;

	if (mlxsw_sp_port_is_br_member(mlxsw_sp_port, bridge_device->dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Can not bridge VLAN uppers of the same port");
		return -EINVAL;
	}

	/* Port is no longer usable as a router interface */
	if (mlxsw_sp_port_vlan->fid)
		mlxsw_sp_port_vlan_router_leave(mlxsw_sp_port_vlan);

	return mlxsw_sp_port_vlan_bridge_join(mlxsw_sp_port_vlan, bridge_port,
					      extack);
}

static void
mlxsw_sp_bridge_8021d_port_leave(struct mlxsw_sp_bridge_device *bridge_device,
				 struct mlxsw_sp_bridge_port *bridge_port,
				 struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct net_device *dev = bridge_port->dev;
	u16 vid;

	vid = is_vlan_dev(dev) ? vlan_dev_vlan_id(dev) : MLXSW_SP_DEFAULT_VID;
	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (!mlxsw_sp_port_vlan || !mlxsw_sp_port_vlan->bridge_port)
		return;

	mlxsw_sp_port_vlan_bridge_leave(mlxsw_sp_port_vlan);
}

static int
mlxsw_sp_bridge_8021d_vxlan_join(struct mlxsw_sp_bridge_device *bridge_device,
				 const struct net_device *vxlan_dev, u16 vid,
				 struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(bridge_device->dev);
	struct vxlan_dev *vxlan = netdev_priv(vxlan_dev);
	struct mlxsw_sp_nve_params params = {
		.type = MLXSW_SP_NVE_TYPE_VXLAN,
		.vni = vxlan->cfg.vni,
		.dev = vxlan_dev,
		.ethertype = ETH_P_8021Q,
	};
	struct mlxsw_sp_fid *fid;
	int err;

	fid = mlxsw_sp_fid_8021d_get(mlxsw_sp, bridge_device->dev->ifindex);
	if (IS_ERR(fid)) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to create 802.1D FID");
		return -EINVAL;
	}

	if (mlxsw_sp_fid_vni_is_set(fid)) {
		NL_SET_ERR_MSG_MOD(extack, "VNI is already set on FID");
		err = -EINVAL;
		goto err_vni_exists;
	}

	err = mlxsw_sp_nve_fid_enable(mlxsw_sp, fid, &params, extack);
	if (err)
		goto err_nve_fid_enable;

	return 0;

err_nve_fid_enable:
err_vni_exists:
	mlxsw_sp_fid_put(fid);
	return err;
}

static struct mlxsw_sp_fid *
mlxsw_sp_bridge_8021d_fid_get(struct mlxsw_sp_bridge_device *bridge_device,
			      u16 vid, struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(bridge_device->dev);

	return mlxsw_sp_fid_8021d_get(mlxsw_sp, bridge_device->dev->ifindex);
}

static struct mlxsw_sp_fid *
mlxsw_sp_bridge_8021d_fid_lookup(struct mlxsw_sp_bridge_device *bridge_device,
				 u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(bridge_device->dev);

	/* The only valid VLAN for a VLAN-unaware bridge is 0 */
	if (vid)
		return NULL;

	return mlxsw_sp_fid_8021d_lookup(mlxsw_sp, bridge_device->dev->ifindex);
}

static u16
mlxsw_sp_bridge_8021d_fid_vid(struct mlxsw_sp_bridge_device *bridge_device,
			      const struct mlxsw_sp_fid *fid)
{
	return 0;
}

static const struct mlxsw_sp_bridge_ops mlxsw_sp_bridge_8021d_ops = {
	.port_join	= mlxsw_sp_bridge_8021d_port_join,
	.port_leave	= mlxsw_sp_bridge_8021d_port_leave,
	.vxlan_join	= mlxsw_sp_bridge_8021d_vxlan_join,
	.fid_get	= mlxsw_sp_bridge_8021d_fid_get,
	.fid_lookup	= mlxsw_sp_bridge_8021d_fid_lookup,
	.fid_vid	= mlxsw_sp_bridge_8021d_fid_vid,
};

static int
mlxsw_sp_bridge_8021ad_port_join(struct mlxsw_sp_bridge_device *bridge_device,
				 struct mlxsw_sp_bridge_port *bridge_port,
				 struct mlxsw_sp_port *mlxsw_sp_port,
				 struct netlink_ext_ack *extack)
{
	int err;

	err = mlxsw_sp_port_vlan_classification_set(mlxsw_sp_port, true, false);
	if (err)
		return err;

	err = mlxsw_sp_bridge_vlan_aware_port_join(bridge_port, mlxsw_sp_port,
						   extack);
	if (err)
		goto err_bridge_vlan_aware_port_join;

	return 0;

err_bridge_vlan_aware_port_join:
	mlxsw_sp_port_vlan_classification_set(mlxsw_sp_port, false, true);
	return err;
}

static void
mlxsw_sp_bridge_8021ad_port_leave(struct mlxsw_sp_bridge_device *bridge_device,
				  struct mlxsw_sp_bridge_port *bridge_port,
				  struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_bridge_vlan_aware_port_leave(mlxsw_sp_port);
	mlxsw_sp_port_vlan_classification_set(mlxsw_sp_port, false, true);
}

static int
mlxsw_sp_bridge_8021ad_vxlan_join(struct mlxsw_sp_bridge_device *bridge_device,
				  const struct net_device *vxlan_dev, u16 vid,
				  struct netlink_ext_ack *extack)
{
	return mlxsw_sp_bridge_vlan_aware_vxlan_join(bridge_device, vxlan_dev,
						     vid, ETH_P_8021AD, extack);
}

static const struct mlxsw_sp_bridge_ops mlxsw_sp1_bridge_8021ad_ops = {
	.port_join	= mlxsw_sp_bridge_8021ad_port_join,
	.port_leave	= mlxsw_sp_bridge_8021ad_port_leave,
	.vxlan_join	= mlxsw_sp_bridge_8021ad_vxlan_join,
	.fid_get	= mlxsw_sp_bridge_8021q_fid_get,
	.fid_lookup	= mlxsw_sp_bridge_8021q_fid_lookup,
	.fid_vid	= mlxsw_sp_bridge_8021q_fid_vid,
};

static int
mlxsw_sp2_bridge_8021ad_port_join(struct mlxsw_sp_bridge_device *bridge_device,
				  struct mlxsw_sp_bridge_port *bridge_port,
				  struct mlxsw_sp_port *mlxsw_sp_port,
				  struct netlink_ext_ack *extack)
{
	int err;

	/* The EtherType of decapsulated packets is determined at the egress
	 * port to allow 802.1d and 802.1ad bridges with VXLAN devices to
	 * co-exist.
	 */
	err = mlxsw_sp_port_egress_ethtype_set(mlxsw_sp_port, ETH_P_8021AD);
	if (err)
		return err;

	err = mlxsw_sp_bridge_8021ad_port_join(bridge_device, bridge_port,
					       mlxsw_sp_port, extack);
	if (err)
		goto err_bridge_8021ad_port_join;

	return 0;

err_bridge_8021ad_port_join:
	mlxsw_sp_port_egress_ethtype_set(mlxsw_sp_port, ETH_P_8021Q);
	return err;
}

static void
mlxsw_sp2_bridge_8021ad_port_leave(struct mlxsw_sp_bridge_device *bridge_device,
				   struct mlxsw_sp_bridge_port *bridge_port,
				   struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_bridge_8021ad_port_leave(bridge_device, bridge_port,
					  mlxsw_sp_port);
	mlxsw_sp_port_egress_ethtype_set(mlxsw_sp_port, ETH_P_8021Q);
}

static const struct mlxsw_sp_bridge_ops mlxsw_sp2_bridge_8021ad_ops = {
	.port_join	= mlxsw_sp2_bridge_8021ad_port_join,
	.port_leave	= mlxsw_sp2_bridge_8021ad_port_leave,
	.vxlan_join	= mlxsw_sp_bridge_8021ad_vxlan_join,
	.fid_get	= mlxsw_sp_bridge_8021q_fid_get,
	.fid_lookup	= mlxsw_sp_bridge_8021q_fid_lookup,
	.fid_vid	= mlxsw_sp_bridge_8021q_fid_vid,
};

int mlxsw_sp_port_bridge_join(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct net_device *brport_dev,
			      struct net_device *br_dev,
			      struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	int err;

	bridge_port = mlxsw_sp_bridge_port_get(mlxsw_sp->bridge, brport_dev,
					       extack);
	if (IS_ERR(bridge_port))
		return PTR_ERR(bridge_port);
	bridge_device = bridge_port->bridge_device;

	err = bridge_device->ops->port_join(bridge_device, bridge_port,
					    mlxsw_sp_port, extack);
	if (err)
		goto err_port_join;

	return 0;

err_port_join:
	mlxsw_sp_bridge_port_put(mlxsw_sp->bridge, bridge_port);
	return err;
}

void mlxsw_sp_port_bridge_leave(struct mlxsw_sp_port *mlxsw_sp_port,
				struct net_device *brport_dev,
				struct net_device *br_dev)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, br_dev);
	if (!bridge_device)
		return;
	bridge_port = __mlxsw_sp_bridge_port_find(bridge_device, brport_dev);
	if (!bridge_port)
		return;

	bridge_device->ops->port_leave(bridge_device, bridge_port,
				       mlxsw_sp_port);
	mlxsw_sp_port_security_set(mlxsw_sp_port, false);
	mlxsw_sp_bridge_port_put(mlxsw_sp->bridge, bridge_port);
}

int mlxsw_sp_bridge_vxlan_join(struct mlxsw_sp *mlxsw_sp,
			       const struct net_device *br_dev,
			       const struct net_device *vxlan_dev, u16 vid,
			       struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_bridge_device *bridge_device;

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, br_dev);
	if (WARN_ON(!bridge_device))
		return -EINVAL;

	return bridge_device->ops->vxlan_join(bridge_device, vxlan_dev, vid,
					      extack);
}

void mlxsw_sp_bridge_vxlan_leave(struct mlxsw_sp *mlxsw_sp,
				 const struct net_device *vxlan_dev)
{
	struct vxlan_dev *vxlan = netdev_priv(vxlan_dev);
	struct mlxsw_sp_fid *fid;

	/* If the VxLAN device is down, then the FID does not have a VNI */
	fid = mlxsw_sp_fid_lookup_by_vni(mlxsw_sp, vxlan->cfg.vni);
	if (!fid)
		return;

	mlxsw_sp_nve_fid_disable(mlxsw_sp, fid);
	/* Drop both the reference we just took during lookup and the reference
	 * the VXLAN device took.
	 */
	mlxsw_sp_fid_put(fid);
	mlxsw_sp_fid_put(fid);
}

static void
mlxsw_sp_switchdev_vxlan_addr_convert(const union vxlan_addr *vxlan_addr,
				      enum mlxsw_sp_l3proto *proto,
				      union mlxsw_sp_l3addr *addr)
{
	if (vxlan_addr->sa.sa_family == AF_INET) {
		addr->addr4 = vxlan_addr->sin.sin_addr.s_addr;
		*proto = MLXSW_SP_L3_PROTO_IPV4;
	} else {
		addr->addr6 = vxlan_addr->sin6.sin6_addr;
		*proto = MLXSW_SP_L3_PROTO_IPV6;
	}
}

static void
mlxsw_sp_switchdev_addr_vxlan_convert(enum mlxsw_sp_l3proto proto,
				      const union mlxsw_sp_l3addr *addr,
				      union vxlan_addr *vxlan_addr)
{
	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		vxlan_addr->sa.sa_family = AF_INET;
		vxlan_addr->sin.sin_addr.s_addr = addr->addr4;
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		vxlan_addr->sa.sa_family = AF_INET6;
		vxlan_addr->sin6.sin6_addr = addr->addr6;
		break;
	}
}

static void mlxsw_sp_fdb_vxlan_call_notifiers(struct net_device *dev,
					      const char *mac,
					      enum mlxsw_sp_l3proto proto,
					      union mlxsw_sp_l3addr *addr,
					      __be32 vni, bool adding)
{
	struct switchdev_notifier_vxlan_fdb_info info;
	struct vxlan_dev *vxlan = netdev_priv(dev);
	enum switchdev_notifier_type type;

	type = adding ? SWITCHDEV_VXLAN_FDB_ADD_TO_BRIDGE :
			SWITCHDEV_VXLAN_FDB_DEL_TO_BRIDGE;
	mlxsw_sp_switchdev_addr_vxlan_convert(proto, addr, &info.remote_ip);
	info.remote_port = vxlan->cfg.dst_port;
	info.remote_vni = vni;
	info.remote_ifindex = 0;
	ether_addr_copy(info.eth_addr, mac);
	info.vni = vni;
	info.offloaded = adding;
	call_switchdev_notifiers(type, dev, &info.info, NULL);
}

static void mlxsw_sp_fdb_nve_call_notifiers(struct net_device *dev,
					    const char *mac,
					    enum mlxsw_sp_l3proto proto,
					    union mlxsw_sp_l3addr *addr,
					    __be32 vni,
					    bool adding)
{
	if (netif_is_vxlan(dev))
		mlxsw_sp_fdb_vxlan_call_notifiers(dev, mac, proto, addr, vni,
						  adding);
}

static void
mlxsw_sp_fdb_call_notifiers(enum switchdev_notifier_type type,
			    const char *mac, u16 vid,
			    struct net_device *dev, bool offloaded, bool locked)
{
	struct switchdev_notifier_fdb_info info = {};

	info.addr = mac;
	info.vid = vid;
	info.offloaded = offloaded;
	info.locked = locked;
	call_switchdev_notifiers(type, dev, &info.info, NULL);
}

static void mlxsw_sp_fdb_notify_mac_process(struct mlxsw_sp *mlxsw_sp,
					    char *sfn_pl, int rec_index,
					    bool adding)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_port *mlxsw_sp_port;
	u16 local_port, vid, fid, evid = 0;
	enum switchdev_notifier_type type;
	char mac[ETH_ALEN];
	bool do_notification = true;
	int err;

	mlxsw_reg_sfn_mac_unpack(sfn_pl, rec_index, mac, &fid, &local_port);

	if (WARN_ON_ONCE(!mlxsw_sp_local_port_is_valid(mlxsw_sp, local_port)))
		return;
	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Incorrect local port in FDB notification\n");
		goto just_remove;
	}

	if (mlxsw_sp_fid_is_dummy(mlxsw_sp, fid))
		goto just_remove;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_fid(mlxsw_sp_port, fid);
	if (!mlxsw_sp_port_vlan) {
		netdev_err(mlxsw_sp_port->dev, "Failed to find a matching {Port, VID} following FDB notification\n");
		goto just_remove;
	}

	bridge_port = mlxsw_sp_port_vlan->bridge_port;
	if (!bridge_port) {
		netdev_err(mlxsw_sp_port->dev, "{Port, VID} not associated with a bridge\n");
		goto just_remove;
	}

	bridge_device = bridge_port->bridge_device;
	vid = bridge_device->vlan_enabled ? mlxsw_sp_port_vlan->vid : 0;
	evid = mlxsw_sp_port_vlan->vid;

	if (adding && mlxsw_sp_port->security) {
		mlxsw_sp_fdb_call_notifiers(SWITCHDEV_FDB_ADD_TO_BRIDGE, mac,
					    vid, bridge_port->dev, false, true);
		return;
	}

do_fdb_op:
	err = mlxsw_sp_port_fdb_uc_op(mlxsw_sp, local_port, mac, fid, evid,
				      adding, true);
	if (err) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to set FDB entry\n");
		return;
	}

	if (!do_notification)
		return;
	type = adding ? SWITCHDEV_FDB_ADD_TO_BRIDGE : SWITCHDEV_FDB_DEL_TO_BRIDGE;
	mlxsw_sp_fdb_call_notifiers(type, mac, vid, bridge_port->dev, adding,
				    false);

	return;

just_remove:
	adding = false;
	do_notification = false;
	goto do_fdb_op;
}

static void mlxsw_sp_fdb_notify_mac_lag_process(struct mlxsw_sp *mlxsw_sp,
						char *sfn_pl, int rec_index,
						bool adding)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_port *mlxsw_sp_port;
	enum switchdev_notifier_type type;
	char mac[ETH_ALEN];
	u16 lag_vid = 0;
	u16 lag_id;
	u16 vid, fid;
	bool do_notification = true;
	int err;

	mlxsw_reg_sfn_mac_lag_unpack(sfn_pl, rec_index, mac, &fid, &lag_id);
	mlxsw_sp_port = mlxsw_sp_lag_rep_port(mlxsw_sp, lag_id);
	if (!mlxsw_sp_port) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Cannot find port representor for LAG\n");
		goto just_remove;
	}

	if (mlxsw_sp_fid_is_dummy(mlxsw_sp, fid))
		goto just_remove;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_fid(mlxsw_sp_port, fid);
	if (!mlxsw_sp_port_vlan) {
		netdev_err(mlxsw_sp_port->dev, "Failed to find a matching {Port, VID} following FDB notification\n");
		goto just_remove;
	}

	bridge_port = mlxsw_sp_port_vlan->bridge_port;
	if (!bridge_port) {
		netdev_err(mlxsw_sp_port->dev, "{Port, VID} not associated with a bridge\n");
		goto just_remove;
	}

	bridge_device = bridge_port->bridge_device;
	vid = bridge_device->vlan_enabled ? mlxsw_sp_port_vlan->vid : 0;
	lag_vid = mlxsw_sp_port_vlan->vid;

	if (adding && mlxsw_sp_port->security) {
		mlxsw_sp_fdb_call_notifiers(SWITCHDEV_FDB_ADD_TO_BRIDGE, mac,
					    vid, bridge_port->dev, false, true);
		return;
	}

do_fdb_op:
	err = mlxsw_sp_port_fdb_uc_lag_op(mlxsw_sp, lag_id, mac, fid, lag_vid,
					  adding, true);
	if (err) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to set FDB entry\n");
		return;
	}

	if (!do_notification)
		return;
	type = adding ? SWITCHDEV_FDB_ADD_TO_BRIDGE : SWITCHDEV_FDB_DEL_TO_BRIDGE;
	mlxsw_sp_fdb_call_notifiers(type, mac, vid, bridge_port->dev, adding,
				    false);

	return;

just_remove:
	adding = false;
	do_notification = false;
	goto do_fdb_op;
}

static int
__mlxsw_sp_fdb_notify_mac_uc_tunnel_process(struct mlxsw_sp *mlxsw_sp,
					    const struct mlxsw_sp_fid *fid,
					    bool adding,
					    struct net_device **nve_dev,
					    u16 *p_vid, __be32 *p_vni)
{
	struct mlxsw_sp_bridge_device *bridge_device;
	struct net_device *br_dev, *dev;
	int nve_ifindex;
	int err;

	err = mlxsw_sp_fid_nve_ifindex(fid, &nve_ifindex);
	if (err)
		return err;

	err = mlxsw_sp_fid_vni(fid, p_vni);
	if (err)
		return err;

	dev = __dev_get_by_index(mlxsw_sp_net(mlxsw_sp), nve_ifindex);
	if (!dev)
		return -EINVAL;
	*nve_dev = dev;

	if (!netif_running(dev))
		return -EINVAL;

	if (adding && !br_port_flag_is_set(dev, BR_LEARNING))
		return -EINVAL;

	if (adding && netif_is_vxlan(dev)) {
		struct vxlan_dev *vxlan = netdev_priv(dev);

		if (!(vxlan->cfg.flags & VXLAN_F_LEARN))
			return -EINVAL;
	}

	br_dev = netdev_master_upper_dev_get(dev);
	if (!br_dev)
		return -EINVAL;

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, br_dev);
	if (!bridge_device)
		return -EINVAL;

	*p_vid = bridge_device->ops->fid_vid(bridge_device, fid);

	return 0;
}

static void mlxsw_sp_fdb_notify_mac_uc_tunnel_process(struct mlxsw_sp *mlxsw_sp,
						      char *sfn_pl,
						      int rec_index,
						      bool adding)
{
	enum mlxsw_reg_sfn_uc_tunnel_protocol sfn_proto;
	enum switchdev_notifier_type type;
	struct net_device *nve_dev;
	union mlxsw_sp_l3addr addr;
	struct mlxsw_sp_fid *fid;
	char mac[ETH_ALEN];
	u16 fid_index, vid;
	__be32 vni;
	u32 uip;
	int err;

	mlxsw_reg_sfn_uc_tunnel_unpack(sfn_pl, rec_index, mac, &fid_index,
				       &uip, &sfn_proto);

	fid = mlxsw_sp_fid_lookup_by_index(mlxsw_sp, fid_index);
	if (!fid)
		goto err_fid_lookup;

	err = mlxsw_sp_nve_learned_ip_resolve(mlxsw_sp, uip,
					      (enum mlxsw_sp_l3proto) sfn_proto,
					      &addr);
	if (err)
		goto err_ip_resolve;

	err = __mlxsw_sp_fdb_notify_mac_uc_tunnel_process(mlxsw_sp, fid, adding,
							  &nve_dev, &vid, &vni);
	if (err)
		goto err_fdb_process;

	err = mlxsw_sp_port_fdb_tunnel_uc_op(mlxsw_sp, mac, fid_index,
					     (enum mlxsw_sp_l3proto) sfn_proto,
					     &addr, adding, true);
	if (err)
		goto err_fdb_op;

	mlxsw_sp_fdb_nve_call_notifiers(nve_dev, mac,
					(enum mlxsw_sp_l3proto) sfn_proto,
					&addr, vni, adding);

	type = adding ? SWITCHDEV_FDB_ADD_TO_BRIDGE :
			SWITCHDEV_FDB_DEL_TO_BRIDGE;
	mlxsw_sp_fdb_call_notifiers(type, mac, vid, nve_dev, adding, false);

	mlxsw_sp_fid_put(fid);

	return;

err_fdb_op:
err_fdb_process:
err_ip_resolve:
	mlxsw_sp_fid_put(fid);
err_fid_lookup:
	/* Remove an FDB entry in case we cannot process it. Otherwise the
	 * device will keep sending the same notification over and over again.
	 */
	mlxsw_sp_port_fdb_tunnel_uc_op(mlxsw_sp, mac, fid_index,
				       (enum mlxsw_sp_l3proto) sfn_proto, &addr,
				       false, true);
}

static void mlxsw_sp_fdb_notify_rec_process(struct mlxsw_sp *mlxsw_sp,
					    char *sfn_pl, int rec_index)
{
	switch (mlxsw_reg_sfn_rec_type_get(sfn_pl, rec_index)) {
	case MLXSW_REG_SFN_REC_TYPE_LEARNED_MAC:
		mlxsw_sp_fdb_notify_mac_process(mlxsw_sp, sfn_pl,
						rec_index, true);
		break;
	case MLXSW_REG_SFN_REC_TYPE_AGED_OUT_MAC:
		mlxsw_sp_fdb_notify_mac_process(mlxsw_sp, sfn_pl,
						rec_index, false);
		break;
	case MLXSW_REG_SFN_REC_TYPE_LEARNED_MAC_LAG:
		mlxsw_sp_fdb_notify_mac_lag_process(mlxsw_sp, sfn_pl,
						    rec_index, true);
		break;
	case MLXSW_REG_SFN_REC_TYPE_AGED_OUT_MAC_LAG:
		mlxsw_sp_fdb_notify_mac_lag_process(mlxsw_sp, sfn_pl,
						    rec_index, false);
		break;
	case MLXSW_REG_SFN_REC_TYPE_LEARNED_UNICAST_TUNNEL:
		mlxsw_sp_fdb_notify_mac_uc_tunnel_process(mlxsw_sp, sfn_pl,
							  rec_index, true);
		break;
	case MLXSW_REG_SFN_REC_TYPE_AGED_OUT_UNICAST_TUNNEL:
		mlxsw_sp_fdb_notify_mac_uc_tunnel_process(mlxsw_sp, sfn_pl,
							  rec_index, false);
		break;
	}
}

#define MLXSW_SP_FDB_SFN_QUERIES_PER_SESSION 10

static void mlxsw_sp_fdb_notify_work(struct work_struct *work)
{
	struct mlxsw_sp_bridge *bridge;
	struct mlxsw_sp *mlxsw_sp;
	bool reschedule = false;
	char *sfn_pl;
	int queries;
	u8 num_rec;
	int i;
	int err;

	sfn_pl = kmalloc(MLXSW_REG_SFN_LEN, GFP_KERNEL);
	if (!sfn_pl)
		return;

	bridge = container_of(work, struct mlxsw_sp_bridge, fdb_notify.dw.work);
	mlxsw_sp = bridge->mlxsw_sp;

	rtnl_lock();
	if (list_empty(&bridge->bridges_list))
		goto out;
	reschedule = true;
	queries = MLXSW_SP_FDB_SFN_QUERIES_PER_SESSION;
	while (queries > 0) {
		mlxsw_reg_sfn_pack(sfn_pl);
		err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(sfn), sfn_pl);
		if (err) {
			dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to get FDB notifications\n");
			goto out;
		}
		num_rec = mlxsw_reg_sfn_num_rec_get(sfn_pl);
		for (i = 0; i < num_rec; i++)
			mlxsw_sp_fdb_notify_rec_process(mlxsw_sp, sfn_pl, i);
		if (num_rec != MLXSW_REG_SFN_REC_MAX_COUNT)
			goto out;
		queries--;
	}

out:
	rtnl_unlock();
	kfree(sfn_pl);
	if (!reschedule)
		return;
	mlxsw_sp_fdb_notify_work_schedule(mlxsw_sp, !queries);
}

struct mlxsw_sp_switchdev_event_work {
	struct work_struct work;
	union {
		struct switchdev_notifier_fdb_info fdb_info;
		struct switchdev_notifier_vxlan_fdb_info vxlan_fdb_info;
	};
	struct net_device *dev;
	unsigned long event;
};

static void
mlxsw_sp_switchdev_bridge_vxlan_fdb_event(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_switchdev_event_work *
					  switchdev_work,
					  struct mlxsw_sp_fid *fid, __be32 vni)
{
	struct switchdev_notifier_vxlan_fdb_info vxlan_fdb_info;
	struct switchdev_notifier_fdb_info *fdb_info;
	struct net_device *dev = switchdev_work->dev;
	enum mlxsw_sp_l3proto proto;
	union mlxsw_sp_l3addr addr;
	int err;

	fdb_info = &switchdev_work->fdb_info;
	err = vxlan_fdb_find_uc(dev, fdb_info->addr, vni, &vxlan_fdb_info);
	if (err)
		return;

	mlxsw_sp_switchdev_vxlan_addr_convert(&vxlan_fdb_info.remote_ip,
					      &proto, &addr);

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		err = mlxsw_sp_port_fdb_tunnel_uc_op(mlxsw_sp,
						     vxlan_fdb_info.eth_addr,
						     mlxsw_sp_fid_index(fid),
						     proto, &addr, true, false);
		if (err)
			return;
		vxlan_fdb_info.offloaded = true;
		call_switchdev_notifiers(SWITCHDEV_VXLAN_FDB_OFFLOADED, dev,
					 &vxlan_fdb_info.info, NULL);
		mlxsw_sp_fdb_call_notifiers(SWITCHDEV_FDB_OFFLOADED,
					    vxlan_fdb_info.eth_addr,
					    fdb_info->vid, dev, true, false);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		err = mlxsw_sp_port_fdb_tunnel_uc_op(mlxsw_sp,
						     vxlan_fdb_info.eth_addr,
						     mlxsw_sp_fid_index(fid),
						     proto, &addr, false,
						     false);
		vxlan_fdb_info.offloaded = false;
		call_switchdev_notifiers(SWITCHDEV_VXLAN_FDB_OFFLOADED, dev,
					 &vxlan_fdb_info.info, NULL);
		break;
	}
}

static void
mlxsw_sp_switchdev_bridge_nve_fdb_event(struct mlxsw_sp_switchdev_event_work *
					switchdev_work)
{
	struct mlxsw_sp_bridge_device *bridge_device;
	struct net_device *dev = switchdev_work->dev;
	struct net_device *br_dev;
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_fid *fid;
	__be32 vni;
	int err;

	if (switchdev_work->event != SWITCHDEV_FDB_ADD_TO_DEVICE &&
	    switchdev_work->event != SWITCHDEV_FDB_DEL_TO_DEVICE)
		return;

	if (switchdev_work->event == SWITCHDEV_FDB_ADD_TO_DEVICE &&
	    (!switchdev_work->fdb_info.added_by_user ||
	     switchdev_work->fdb_info.is_local))
		return;

	if (!netif_running(dev))
		return;
	br_dev = netdev_master_upper_dev_get(dev);
	if (!br_dev)
		return;
	if (!netif_is_bridge_master(br_dev))
		return;
	mlxsw_sp = mlxsw_sp_lower_get(br_dev);
	if (!mlxsw_sp)
		return;
	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, br_dev);
	if (!bridge_device)
		return;

	fid = bridge_device->ops->fid_lookup(bridge_device,
					     switchdev_work->fdb_info.vid);
	if (!fid)
		return;

	err = mlxsw_sp_fid_vni(fid, &vni);
	if (err)
		goto out;

	mlxsw_sp_switchdev_bridge_vxlan_fdb_event(mlxsw_sp, switchdev_work, fid,
						  vni);

out:
	mlxsw_sp_fid_put(fid);
}

static void mlxsw_sp_switchdev_bridge_fdb_event_work(struct work_struct *work)
{
	struct mlxsw_sp_switchdev_event_work *switchdev_work =
		container_of(work, struct mlxsw_sp_switchdev_event_work, work);
	struct net_device *dev = switchdev_work->dev;
	struct switchdev_notifier_fdb_info *fdb_info;
	struct mlxsw_sp_port *mlxsw_sp_port;
	int err;

	rtnl_lock();
	if (netif_is_vxlan(dev)) {
		mlxsw_sp_switchdev_bridge_nve_fdb_event(switchdev_work);
		goto out;
	}

	mlxsw_sp_port = mlxsw_sp_port_dev_lower_find(dev);
	if (!mlxsw_sp_port)
		goto out;

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fdb_info = &switchdev_work->fdb_info;
		if (!fdb_info->added_by_user || fdb_info->is_local)
			break;
		err = mlxsw_sp_port_fdb_set(mlxsw_sp_port, fdb_info, true);
		if (err)
			break;
		mlxsw_sp_fdb_call_notifiers(SWITCHDEV_FDB_OFFLOADED,
					    fdb_info->addr,
					    fdb_info->vid, dev, true, false);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb_info = &switchdev_work->fdb_info;
		mlxsw_sp_port_fdb_set(mlxsw_sp_port, fdb_info, false);
		break;
	case SWITCHDEV_FDB_ADD_TO_BRIDGE:
	case SWITCHDEV_FDB_DEL_TO_BRIDGE:
		/* These events are only used to potentially update an existing
		 * SPAN mirror.
		 */
		break;
	}

	mlxsw_sp_span_respin(mlxsw_sp_port->mlxsw_sp);

out:
	rtnl_unlock();
	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(dev);
}

static void
mlxsw_sp_switchdev_vxlan_fdb_add(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_switchdev_event_work *
				 switchdev_work)
{
	struct switchdev_notifier_vxlan_fdb_info *vxlan_fdb_info;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct net_device *dev = switchdev_work->dev;
	u8 all_zeros_mac[ETH_ALEN] = { 0 };
	enum mlxsw_sp_l3proto proto;
	union mlxsw_sp_l3addr addr;
	struct net_device *br_dev;
	struct mlxsw_sp_fid *fid;
	u16 vid;
	int err;

	vxlan_fdb_info = &switchdev_work->vxlan_fdb_info;
	br_dev = netdev_master_upper_dev_get(dev);

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, br_dev);
	if (!bridge_device)
		return;

	fid = mlxsw_sp_fid_lookup_by_vni(mlxsw_sp, vxlan_fdb_info->vni);
	if (!fid)
		return;

	mlxsw_sp_switchdev_vxlan_addr_convert(&vxlan_fdb_info->remote_ip,
					      &proto, &addr);

	if (ether_addr_equal(vxlan_fdb_info->eth_addr, all_zeros_mac)) {
		err = mlxsw_sp_nve_flood_ip_add(mlxsw_sp, fid, proto, &addr);
		if (err) {
			mlxsw_sp_fid_put(fid);
			return;
		}
		vxlan_fdb_info->offloaded = true;
		call_switchdev_notifiers(SWITCHDEV_VXLAN_FDB_OFFLOADED, dev,
					 &vxlan_fdb_info->info, NULL);
		mlxsw_sp_fid_put(fid);
		return;
	}

	/* The device has a single FDB table, whereas Linux has two - one
	 * in the bridge driver and another in the VxLAN driver. We only
	 * program an entry to the device if the MAC points to the VxLAN
	 * device in the bridge's FDB table
	 */
	vid = bridge_device->ops->fid_vid(bridge_device, fid);
	if (br_fdb_find_port(br_dev, vxlan_fdb_info->eth_addr, vid) != dev)
		goto err_br_fdb_find;

	err = mlxsw_sp_port_fdb_tunnel_uc_op(mlxsw_sp, vxlan_fdb_info->eth_addr,
					     mlxsw_sp_fid_index(fid), proto,
					     &addr, true, false);
	if (err)
		goto err_fdb_tunnel_uc_op;
	vxlan_fdb_info->offloaded = true;
	call_switchdev_notifiers(SWITCHDEV_VXLAN_FDB_OFFLOADED, dev,
				 &vxlan_fdb_info->info, NULL);
	mlxsw_sp_fdb_call_notifiers(SWITCHDEV_FDB_OFFLOADED,
				    vxlan_fdb_info->eth_addr, vid, dev, true,
				    false);

	mlxsw_sp_fid_put(fid);

	return;

err_fdb_tunnel_uc_op:
err_br_fdb_find:
	mlxsw_sp_fid_put(fid);
}

static void
mlxsw_sp_switchdev_vxlan_fdb_del(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_switchdev_event_work *
				 switchdev_work)
{
	struct switchdev_notifier_vxlan_fdb_info *vxlan_fdb_info;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct net_device *dev = switchdev_work->dev;
	struct net_device *br_dev = netdev_master_upper_dev_get(dev);
	u8 all_zeros_mac[ETH_ALEN] = { 0 };
	enum mlxsw_sp_l3proto proto;
	union mlxsw_sp_l3addr addr;
	struct mlxsw_sp_fid *fid;
	u16 vid;

	vxlan_fdb_info = &switchdev_work->vxlan_fdb_info;
	if (!vxlan_fdb_info->offloaded)
		return;

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, br_dev);
	if (!bridge_device)
		return;

	fid = mlxsw_sp_fid_lookup_by_vni(mlxsw_sp, vxlan_fdb_info->vni);
	if (!fid)
		return;

	mlxsw_sp_switchdev_vxlan_addr_convert(&vxlan_fdb_info->remote_ip,
					      &proto, &addr);

	if (ether_addr_equal(vxlan_fdb_info->eth_addr, all_zeros_mac)) {
		mlxsw_sp_nve_flood_ip_del(mlxsw_sp, fid, proto, &addr);
		mlxsw_sp_fid_put(fid);
		return;
	}

	mlxsw_sp_port_fdb_tunnel_uc_op(mlxsw_sp, vxlan_fdb_info->eth_addr,
				       mlxsw_sp_fid_index(fid), proto, &addr,
				       false, false);
	vid = bridge_device->ops->fid_vid(bridge_device, fid);
	mlxsw_sp_fdb_call_notifiers(SWITCHDEV_FDB_OFFLOADED,
				    vxlan_fdb_info->eth_addr, vid, dev, false,
				    false);

	mlxsw_sp_fid_put(fid);
}

static void mlxsw_sp_switchdev_vxlan_fdb_event_work(struct work_struct *work)
{
	struct mlxsw_sp_switchdev_event_work *switchdev_work =
		container_of(work, struct mlxsw_sp_switchdev_event_work, work);
	struct net_device *dev = switchdev_work->dev;
	struct mlxsw_sp *mlxsw_sp;
	struct net_device *br_dev;

	rtnl_lock();

	if (!netif_running(dev))
		goto out;
	br_dev = netdev_master_upper_dev_get(dev);
	if (!br_dev)
		goto out;
	if (!netif_is_bridge_master(br_dev))
		goto out;
	mlxsw_sp = mlxsw_sp_lower_get(br_dev);
	if (!mlxsw_sp)
		goto out;

	switch (switchdev_work->event) {
	case SWITCHDEV_VXLAN_FDB_ADD_TO_DEVICE:
		mlxsw_sp_switchdev_vxlan_fdb_add(mlxsw_sp, switchdev_work);
		break;
	case SWITCHDEV_VXLAN_FDB_DEL_TO_DEVICE:
		mlxsw_sp_switchdev_vxlan_fdb_del(mlxsw_sp, switchdev_work);
		break;
	}

out:
	rtnl_unlock();
	kfree(switchdev_work);
	dev_put(dev);
}

static int
mlxsw_sp_switchdev_vxlan_work_prepare(struct mlxsw_sp_switchdev_event_work *
				      switchdev_work,
				      struct switchdev_notifier_info *info)
{
	struct vxlan_dev *vxlan = netdev_priv(switchdev_work->dev);
	struct switchdev_notifier_vxlan_fdb_info *vxlan_fdb_info;
	struct vxlan_config *cfg = &vxlan->cfg;
	struct netlink_ext_ack *extack;

	extack = switchdev_notifier_info_to_extack(info);
	vxlan_fdb_info = container_of(info,
				      struct switchdev_notifier_vxlan_fdb_info,
				      info);

	if (vxlan_fdb_info->remote_port != cfg->dst_port) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: FDB: Non-default remote port is not supported");
		return -EOPNOTSUPP;
	}
	if (vxlan_fdb_info->remote_vni != cfg->vni ||
	    vxlan_fdb_info->vni != cfg->vni) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: FDB: Non-default VNI is not supported");
		return -EOPNOTSUPP;
	}
	if (vxlan_fdb_info->remote_ifindex) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: FDB: Local interface is not supported");
		return -EOPNOTSUPP;
	}
	if (is_multicast_ether_addr(vxlan_fdb_info->eth_addr)) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: FDB: Multicast MAC addresses not supported");
		return -EOPNOTSUPP;
	}
	if (vxlan_addr_multicast(&vxlan_fdb_info->remote_ip)) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN: FDB: Multicast destination IP is not supported");
		return -EOPNOTSUPP;
	}

	switchdev_work->vxlan_fdb_info = *vxlan_fdb_info;

	return 0;
}

/* Called under rcu_read_lock() */
static int mlxsw_sp_switchdev_event(struct notifier_block *unused,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct mlxsw_sp_switchdev_event_work *switchdev_work;
	struct switchdev_notifier_fdb_info *fdb_info;
	struct switchdev_notifier_info *info = ptr;
	struct net_device *br_dev;
	int err;

	if (event == SWITCHDEV_PORT_ATTR_SET) {
		err = switchdev_handle_port_attr_set(dev, ptr,
						     mlxsw_sp_port_dev_check,
						     mlxsw_sp_port_attr_set);
		return notifier_from_errno(err);
	}

	/* Tunnel devices are not our uppers, so check their master instead */
	br_dev = netdev_master_upper_dev_get_rcu(dev);
	if (!br_dev)
		return NOTIFY_DONE;
	if (!netif_is_bridge_master(br_dev))
		return NOTIFY_DONE;
	if (!mlxsw_sp_port_dev_lower_find_rcu(br_dev))
		return NOTIFY_DONE;

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (!switchdev_work)
		return NOTIFY_BAD;

	switchdev_work->dev = dev;
	switchdev_work->event = event;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
	case SWITCHDEV_FDB_ADD_TO_BRIDGE:
	case SWITCHDEV_FDB_DEL_TO_BRIDGE:
		fdb_info = container_of(info,
					struct switchdev_notifier_fdb_info,
					info);
		INIT_WORK(&switchdev_work->work,
			  mlxsw_sp_switchdev_bridge_fdb_event_work);
		memcpy(&switchdev_work->fdb_info, ptr,
		       sizeof(switchdev_work->fdb_info));
		switchdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
		if (!switchdev_work->fdb_info.addr)
			goto err_addr_alloc;
		ether_addr_copy((u8 *)switchdev_work->fdb_info.addr,
				fdb_info->addr);
		/* Take a reference on the device. This can be either
		 * upper device containig mlxsw_sp_port or just a
		 * mlxsw_sp_port
		 */
		dev_hold(dev);
		break;
	case SWITCHDEV_VXLAN_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_VXLAN_FDB_DEL_TO_DEVICE:
		INIT_WORK(&switchdev_work->work,
			  mlxsw_sp_switchdev_vxlan_fdb_event_work);
		err = mlxsw_sp_switchdev_vxlan_work_prepare(switchdev_work,
							    info);
		if (err)
			goto err_vxlan_work_prepare;
		dev_hold(dev);
		break;
	default:
		kfree(switchdev_work);
		return NOTIFY_DONE;
	}

	mlxsw_core_schedule_work(&switchdev_work->work);

	return NOTIFY_DONE;

err_vxlan_work_prepare:
err_addr_alloc:
	kfree(switchdev_work);
	return NOTIFY_BAD;
}

struct notifier_block mlxsw_sp_switchdev_notifier = {
	.notifier_call = mlxsw_sp_switchdev_event,
};

static int
mlxsw_sp_switchdev_vxlan_vlan_add(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_bridge_device *bridge_device,
				  const struct net_device *vxlan_dev, u16 vid,
				  bool flag_untagged, bool flag_pvid,
				  struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan = netdev_priv(vxlan_dev);
	__be32 vni = vxlan->cfg.vni;
	struct mlxsw_sp_fid *fid;
	u16 old_vid;
	int err;

	/* We cannot have the same VLAN as PVID and egress untagged on multiple
	 * VxLAN devices. Note that we get this notification before the VLAN is
	 * actually added to the bridge's database, so it is not possible for
	 * the lookup function to return 'vxlan_dev'
	 */
	if (flag_untagged && flag_pvid &&
	    mlxsw_sp_bridge_8021q_vxlan_dev_find(bridge_device->dev, vid)) {
		NL_SET_ERR_MSG_MOD(extack, "VLAN already mapped to a different VNI");
		return -EINVAL;
	}

	if (!netif_running(vxlan_dev))
		return 0;

	/* First case: FID is not associated with this VNI, but the new VLAN
	 * is both PVID and egress untagged. Need to enable NVE on the FID, if
	 * it exists
	 */
	fid = mlxsw_sp_fid_lookup_by_vni(mlxsw_sp, vni);
	if (!fid) {
		if (!flag_untagged || !flag_pvid)
			return 0;
		return bridge_device->ops->vxlan_join(bridge_device, vxlan_dev,
						      vid, extack);
	}

	/* Second case: FID is associated with the VNI and the VLAN associated
	 * with the FID is the same as the notified VLAN. This means the flags
	 * (PVID / egress untagged) were toggled and that NVE should be
	 * disabled on the FID
	 */
	old_vid = mlxsw_sp_fid_8021q_vid(fid);
	if (vid == old_vid) {
		if (WARN_ON(flag_untagged && flag_pvid)) {
			mlxsw_sp_fid_put(fid);
			return -EINVAL;
		}
		mlxsw_sp_bridge_vxlan_leave(mlxsw_sp, vxlan_dev);
		mlxsw_sp_fid_put(fid);
		return 0;
	}

	/* Third case: A new VLAN was configured on the VxLAN device, but this
	 * VLAN is not PVID, so there is nothing to do.
	 */
	if (!flag_pvid) {
		mlxsw_sp_fid_put(fid);
		return 0;
	}

	/* Fourth case: Thew new VLAN is PVID, which means the VLAN currently
	 * mapped to the VNI should be unmapped
	 */
	mlxsw_sp_bridge_vxlan_leave(mlxsw_sp, vxlan_dev);
	mlxsw_sp_fid_put(fid);

	/* Fifth case: The new VLAN is also egress untagged, which means the
	 * VLAN needs to be mapped to the VNI
	 */
	if (!flag_untagged)
		return 0;

	err = bridge_device->ops->vxlan_join(bridge_device, vxlan_dev, vid, extack);
	if (err)
		goto err_vxlan_join;

	return 0;

err_vxlan_join:
	bridge_device->ops->vxlan_join(bridge_device, vxlan_dev, old_vid, NULL);
	return err;
}

static void
mlxsw_sp_switchdev_vxlan_vlan_del(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_bridge_device *bridge_device,
				  const struct net_device *vxlan_dev, u16 vid)
{
	struct vxlan_dev *vxlan = netdev_priv(vxlan_dev);
	__be32 vni = vxlan->cfg.vni;
	struct mlxsw_sp_fid *fid;

	if (!netif_running(vxlan_dev))
		return;

	fid = mlxsw_sp_fid_lookup_by_vni(mlxsw_sp, vni);
	if (!fid)
		return;

	/* A different VLAN than the one mapped to the VNI is deleted */
	if (mlxsw_sp_fid_8021q_vid(fid) != vid)
		goto out;

	mlxsw_sp_bridge_vxlan_leave(mlxsw_sp, vxlan_dev);

out:
	mlxsw_sp_fid_put(fid);
}

static int
mlxsw_sp_switchdev_vxlan_vlans_add(struct net_device *vxlan_dev,
				   struct switchdev_notifier_port_obj_info *
				   port_obj_info)
{
	struct switchdev_obj_port_vlan *vlan =
		SWITCHDEV_OBJ_PORT_VLAN(port_obj_info->obj);
	bool flag_untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool flag_pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct netlink_ext_ack *extack;
	struct mlxsw_sp *mlxsw_sp;
	struct net_device *br_dev;

	extack = switchdev_notifier_info_to_extack(&port_obj_info->info);
	br_dev = netdev_master_upper_dev_get(vxlan_dev);
	if (!br_dev)
		return 0;

	mlxsw_sp = mlxsw_sp_lower_get(br_dev);
	if (!mlxsw_sp)
		return 0;

	port_obj_info->handled = true;

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, br_dev);
	if (!bridge_device)
		return -EINVAL;

	if (!bridge_device->vlan_enabled)
		return 0;

	return mlxsw_sp_switchdev_vxlan_vlan_add(mlxsw_sp, bridge_device,
						 vxlan_dev, vlan->vid,
						 flag_untagged,
						 flag_pvid, extack);
}

static void
mlxsw_sp_switchdev_vxlan_vlans_del(struct net_device *vxlan_dev,
				   struct switchdev_notifier_port_obj_info *
				   port_obj_info)
{
	struct switchdev_obj_port_vlan *vlan =
		SWITCHDEV_OBJ_PORT_VLAN(port_obj_info->obj);
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp *mlxsw_sp;
	struct net_device *br_dev;

	br_dev = netdev_master_upper_dev_get(vxlan_dev);
	if (!br_dev)
		return;

	mlxsw_sp = mlxsw_sp_lower_get(br_dev);
	if (!mlxsw_sp)
		return;

	port_obj_info->handled = true;

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, br_dev);
	if (!bridge_device)
		return;

	if (!bridge_device->vlan_enabled)
		return;

	mlxsw_sp_switchdev_vxlan_vlan_del(mlxsw_sp, bridge_device, vxlan_dev,
					  vlan->vid);
}

static int
mlxsw_sp_switchdev_handle_vxlan_obj_add(struct net_device *vxlan_dev,
					struct switchdev_notifier_port_obj_info *
					port_obj_info)
{
	int err = 0;

	switch (port_obj_info->obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = mlxsw_sp_switchdev_vxlan_vlans_add(vxlan_dev,
							 port_obj_info);
		break;
	default:
		break;
	}

	return err;
}

static void
mlxsw_sp_switchdev_handle_vxlan_obj_del(struct net_device *vxlan_dev,
					struct switchdev_notifier_port_obj_info *
					port_obj_info)
{
	switch (port_obj_info->obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		mlxsw_sp_switchdev_vxlan_vlans_del(vxlan_dev, port_obj_info);
		break;
	default:
		break;
	}
}

static int mlxsw_sp_switchdev_blocking_event(struct notifier_block *unused,
					     unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err = 0;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		if (netif_is_vxlan(dev))
			err = mlxsw_sp_switchdev_handle_vxlan_obj_add(dev, ptr);
		else
			err = switchdev_handle_port_obj_add(dev, ptr,
							mlxsw_sp_port_dev_check,
							mlxsw_sp_port_obj_add);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_OBJ_DEL:
		if (netif_is_vxlan(dev))
			mlxsw_sp_switchdev_handle_vxlan_obj_del(dev, ptr);
		else
			err = switchdev_handle_port_obj_del(dev, ptr,
							mlxsw_sp_port_dev_check,
							mlxsw_sp_port_obj_del);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     mlxsw_sp_port_dev_check,
						     mlxsw_sp_port_attr_set);
		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

static struct notifier_block mlxsw_sp_switchdev_blocking_notifier = {
	.notifier_call = mlxsw_sp_switchdev_blocking_event,
};

u8
mlxsw_sp_bridge_port_stp_state(struct mlxsw_sp_bridge_port *bridge_port)
{
	return bridge_port->stp_state;
}

static int mlxsw_sp_fdb_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_bridge *bridge = mlxsw_sp->bridge;
	struct notifier_block *nb;
	int err;

	err = mlxsw_sp_ageing_set(mlxsw_sp, MLXSW_SP_DEFAULT_AGEING_TIME);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to set default ageing time\n");
		return err;
	}

	err = register_switchdev_notifier(&mlxsw_sp_switchdev_notifier);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to register switchdev notifier\n");
		return err;
	}

	nb = &mlxsw_sp_switchdev_blocking_notifier;
	err = register_switchdev_blocking_notifier(nb);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to register switchdev blocking notifier\n");
		goto err_register_switchdev_blocking_notifier;
	}

	INIT_DELAYED_WORK(&bridge->fdb_notify.dw, mlxsw_sp_fdb_notify_work);
	bridge->fdb_notify.interval = MLXSW_SP_DEFAULT_LEARNING_INTERVAL;
	return 0;

err_register_switchdev_blocking_notifier:
	unregister_switchdev_notifier(&mlxsw_sp_switchdev_notifier);
	return err;
}

static void mlxsw_sp_fdb_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct notifier_block *nb;

	cancel_delayed_work_sync(&mlxsw_sp->bridge->fdb_notify.dw);

	nb = &mlxsw_sp_switchdev_blocking_notifier;
	unregister_switchdev_blocking_notifier(nb);

	unregister_switchdev_notifier(&mlxsw_sp_switchdev_notifier);
}

static void mlxsw_sp1_switchdev_init(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp->bridge->bridge_8021ad_ops = &mlxsw_sp1_bridge_8021ad_ops;
}

const struct mlxsw_sp_switchdev_ops mlxsw_sp1_switchdev_ops = {
	.init	= mlxsw_sp1_switchdev_init,
};

static void mlxsw_sp2_switchdev_init(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp->bridge->bridge_8021ad_ops = &mlxsw_sp2_bridge_8021ad_ops;
}

const struct mlxsw_sp_switchdev_ops mlxsw_sp2_switchdev_ops = {
	.init	= mlxsw_sp2_switchdev_init,
};

int mlxsw_sp_switchdev_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_bridge *bridge;

	bridge = kzalloc(sizeof(*mlxsw_sp->bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;
	mlxsw_sp->bridge = bridge;
	bridge->mlxsw_sp = mlxsw_sp;

	INIT_LIST_HEAD(&mlxsw_sp->bridge->bridges_list);

	bridge->bridge_8021q_ops = &mlxsw_sp_bridge_8021q_ops;
	bridge->bridge_8021d_ops = &mlxsw_sp_bridge_8021d_ops;

	mlxsw_sp->switchdev_ops->init(mlxsw_sp);

	return mlxsw_sp_fdb_init(mlxsw_sp);
}

void mlxsw_sp_switchdev_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_fdb_fini(mlxsw_sp);
	WARN_ON(!list_empty(&mlxsw_sp->bridge->bridges_list));
	kfree(mlxsw_sp->bridge);
}

