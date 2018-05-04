/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_switchdev.c
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2015 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2015 Elad Raz <eladr@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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

#include "spectrum_span.h"
#include "spectrum_router.h"
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
};

struct mlxsw_sp_bridge_device {
	struct net_device *dev;
	struct list_head list;
	struct list_head ports_list;
	struct list_head mids_list;
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
	struct mlxsw_sp_fid *
		(*fid_get)(struct mlxsw_sp_bridge_device *bridge_device,
			   u16 vid);
};

static int
mlxsw_sp_bridge_port_fdb_flush(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_bridge_port *bridge_port,
			       u16 fid_index);

static void
mlxsw_sp_bridge_port_mdb_flush(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct mlxsw_sp_bridge_port *bridge_port);

static void
mlxsw_sp_bridge_mdb_mc_enable_sync(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct mlxsw_sp_bridge_device
				   *bridge_device);

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

static struct mlxsw_sp_bridge_device *
mlxsw_sp_bridge_device_create(struct mlxsw_sp_bridge *bridge,
			      struct net_device *br_dev)
{
	struct device *dev = bridge->mlxsw_sp->bus_info->dev;
	struct mlxsw_sp_bridge_device *bridge_device;
	bool vlan_enabled = br_vlan_enabled(br_dev);

	if (vlan_enabled && bridge->vlan_enabled_exists) {
		dev_err(dev, "Only one VLAN-aware bridge is supported\n");
		return ERR_PTR(-EINVAL);
	}

	bridge_device = kzalloc(sizeof(*bridge_device), GFP_KERNEL);
	if (!bridge_device)
		return ERR_PTR(-ENOMEM);

	bridge_device->dev = br_dev;
	bridge_device->vlan_enabled = vlan_enabled;
	bridge_device->multicast_enabled = br_multicast_enabled(br_dev);
	bridge_device->mrouter = br_multicast_router(br_dev);
	INIT_LIST_HEAD(&bridge_device->ports_list);
	if (vlan_enabled) {
		bridge->vlan_enabled_exists = true;
		bridge_device->ops = bridge->bridge_8021q_ops;
	} else {
		bridge_device->ops = bridge->bridge_8021d_ops;
	}
	INIT_LIST_HEAD(&bridge_device->mids_list);
	list_add(&bridge_device->list, &bridge->bridges_list);

	return bridge_device;
}

static void
mlxsw_sp_bridge_device_destroy(struct mlxsw_sp_bridge *bridge,
			       struct mlxsw_sp_bridge_device *bridge_device)
{
	list_del(&bridge_device->list);
	if (bridge_device->vlan_enabled)
		bridge->vlan_enabled_exists = false;
	WARN_ON(!list_empty(&bridge_device->ports_list));
	WARN_ON(!list_empty(&bridge_device->mids_list));
	kfree(bridge_device);
}

static struct mlxsw_sp_bridge_device *
mlxsw_sp_bridge_device_get(struct mlxsw_sp_bridge *bridge,
			   struct net_device *br_dev)
{
	struct mlxsw_sp_bridge_device *bridge_device;

	bridge_device = mlxsw_sp_bridge_device_find(bridge, br_dev);
	if (bridge_device)
		return bridge_device;

	return mlxsw_sp_bridge_device_create(bridge, br_dev);
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
			    struct net_device *brport_dev)
{
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_port *mlxsw_sp_port;

	bridge_port = kzalloc(sizeof(*bridge_port), GFP_KERNEL);
	if (!bridge_port)
		return NULL;

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

	return bridge_port;
}

static void
mlxsw_sp_bridge_port_destroy(struct mlxsw_sp_bridge_port *bridge_port)
{
	list_del(&bridge_port->list);
	WARN_ON(!list_empty(&bridge_port->vlans_list));
	kfree(bridge_port);
}

static bool
mlxsw_sp_bridge_port_should_destroy(const struct mlxsw_sp_bridge_port *
				    bridge_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(bridge_port->dev);

	/* In case ports were pulled from out of a bridged LAG, then
	 * it's possible the reference count isn't zero, yet the bridge
	 * port should be destroyed, as it's no longer an upper of ours.
	 */
	if (!mlxsw_sp && list_empty(&bridge_port->vlans_list))
		return true;
	else if (bridge_port->ref_count == 0)
		return true;
	else
		return false;
}

static struct mlxsw_sp_bridge_port *
mlxsw_sp_bridge_port_get(struct mlxsw_sp_bridge *bridge,
			 struct net_device *brport_dev)
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

	bridge_device = mlxsw_sp_bridge_device_get(bridge, br_dev);
	if (IS_ERR(bridge_device))
		return ERR_CAST(bridge_device);

	bridge_port = mlxsw_sp_bridge_port_create(bridge_device, brport_dev);
	if (!bridge_port) {
		err = -ENOMEM;
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

	bridge_port->ref_count--;
	if (!mlxsw_sp_bridge_port_should_destroy(bridge_port))
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

static void mlxsw_sp_port_bridge_flags_get(struct mlxsw_sp_bridge *bridge,
					   struct net_device *dev,
					   unsigned long *brport_flags)
{
	struct mlxsw_sp_bridge_port *bridge_port;

	bridge_port = mlxsw_sp_bridge_port_find(bridge, dev);
	if (WARN_ON(!bridge_port))
		return;

	memcpy(brport_flags, &bridge_port->flags, sizeof(*brport_flags));
}

static int mlxsw_sp_port_attr_get(struct net_device *dev,
				  struct switchdev_attr *attr)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID:
		attr->u.ppid.id_len = sizeof(mlxsw_sp->base_mac);
		memcpy(&attr->u.ppid.id, &mlxsw_sp->base_mac,
		       attr->u.ppid.id_len);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		mlxsw_sp_port_bridge_flags_get(mlxsw_sp->bridge, attr->orig_dev,
					       &attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS_SUPPORT:
		attr->u.brport_flags_support = BR_LEARNING | BR_FLOOD |
					       BR_MCAST_FLOOD;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
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
					    struct switchdev_trans *trans,
					    struct net_device *orig_dev,
					    u8 state)
{
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_bridge_vlan *bridge_vlan;
	int err;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

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

static int mlxsw_sp_port_attr_br_flags_set(struct mlxsw_sp_port *mlxsw_sp_port,
					   struct switchdev_trans *trans,
					   struct net_device *orig_dev,
					   unsigned long brport_flags)
{
	struct mlxsw_sp_bridge_port *bridge_port;
	int err;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp_port->mlxsw_sp->bridge,
						orig_dev);
	if (!bridge_port)
		return 0;

	err = mlxsw_sp_bridge_port_flood_table_set(mlxsw_sp_port, bridge_port,
						   MLXSW_SP_FLOOD_TYPE_UC,
						   brport_flags & BR_FLOOD);
	if (err)
		return err;

	err = mlxsw_sp_bridge_port_learning_set(mlxsw_sp_port, bridge_port,
						brport_flags & BR_LEARNING);
	if (err)
		return err;

	if (bridge_port->bridge_device->multicast_enabled)
		goto out;

	err = mlxsw_sp_bridge_port_flood_table_set(mlxsw_sp_port, bridge_port,
						   MLXSW_SP_FLOOD_TYPE_MC,
						   brport_flags &
						   BR_MCAST_FLOOD);
	if (err)
		return err;

out:
	memcpy(&bridge_port->flags, &brport_flags, sizeof(brport_flags));
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
					    struct switchdev_trans *trans,
					    unsigned long ageing_clock_t)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock_t);
	u32 ageing_time = jiffies_to_msecs(ageing_jiffies) / 1000;

	if (switchdev_trans_ph_prepare(trans)) {
		if (ageing_time < MLXSW_SP_MIN_AGEING_TIME ||
		    ageing_time > MLXSW_SP_MAX_AGEING_TIME)
			return -ERANGE;
		else
			return 0;
	}

	return mlxsw_sp_ageing_set(mlxsw_sp, ageing_time);
}

static int mlxsw_sp_port_attr_br_vlan_set(struct mlxsw_sp_port *mlxsw_sp_port,
					  struct switchdev_trans *trans,
					  struct net_device *orig_dev,
					  bool vlan_enabled)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;

	if (!switchdev_trans_ph_prepare(trans))
		return 0;

	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, orig_dev);
	if (WARN_ON(!bridge_device))
		return -EINVAL;

	if (bridge_device->vlan_enabled == vlan_enabled)
		return 0;

	netdev_err(bridge_device->dev, "VLAN filtering can't be changed for existing bridge\n");
	return -EINVAL;
}

static int mlxsw_sp_port_attr_mrouter_set(struct mlxsw_sp_port *mlxsw_sp_port,
					  struct switchdev_trans *trans,
					  struct net_device *orig_dev,
					  bool is_port_mrouter)
{
	struct mlxsw_sp_bridge_port *bridge_port;
	int err;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp_port->mlxsw_sp->bridge,
						orig_dev);
	if (!bridge_port)
		return 0;

	if (!bridge_port->bridge_device->multicast_enabled)
		goto out;

	err = mlxsw_sp_bridge_port_flood_table_set(mlxsw_sp_port, bridge_port,
						   MLXSW_SP_FLOOD_TYPE_MC,
						   is_port_mrouter);
	if (err)
		return err;

	mlxsw_sp_port_mrouter_update_mdb(mlxsw_sp_port, bridge_port,
					 is_port_mrouter);
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
					 struct switchdev_trans *trans,
					 struct net_device *orig_dev,
					 bool mc_disabled)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	int err;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	/* It's possible we failed to enslave the port, yet this
	 * operation is executed due to it being deferred.
	 */
	bridge_device = mlxsw_sp_bridge_device_find(mlxsw_sp->bridge, orig_dev);
	if (!bridge_device)
		return 0;

	if (bridge_device->multicast_enabled != !mc_disabled) {
		bridge_device->multicast_enabled = !mc_disabled;
		mlxsw_sp_bridge_mdb_mc_enable_sync(mlxsw_sp_port,
						   bridge_device);
	}

	list_for_each_entry(bridge_port, &bridge_device->ports_list, list) {
		enum mlxsw_sp_flood_type packet_type = MLXSW_SP_FLOOD_TYPE_MC;
		bool member = mlxsw_sp_mc_flood(bridge_port);

		err = mlxsw_sp_bridge_port_flood_table_set(mlxsw_sp_port,
							   bridge_port,
							   packet_type, member);
		if (err)
			return err;
	}

	bridge_device->multicast_enabled = !mc_disabled;

	return 0;
}

static int mlxsw_sp_smid_router_port_set(struct mlxsw_sp *mlxsw_sp,
					 u16 mid_idx, bool add)
{
	char *smid_pl;
	int err;

	smid_pl = kmalloc(MLXSW_REG_SMID_LEN, GFP_KERNEL);
	if (!smid_pl)
		return -ENOMEM;

	mlxsw_reg_smid_pack(smid_pl, mid_idx,
			    mlxsw_sp_router_port(mlxsw_sp), add);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(smid), smid_pl);
	kfree(smid_pl);
	return err;
}

static void
mlxsw_sp_bridge_mrouter_update_mdb(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_bridge_device *bridge_device,
				   bool add)
{
	struct mlxsw_sp_mid *mid;

	list_for_each_entry(mid, &bridge_device->mids_list, list)
		mlxsw_sp_smid_router_port_set(mlxsw_sp, mid->mid, add);
}

static int
mlxsw_sp_port_attr_br_mrouter_set(struct mlxsw_sp_port *mlxsw_sp_port,
				  struct switchdev_trans *trans,
				  struct net_device *orig_dev,
				  bool is_mrouter)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_bridge_device *bridge_device;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

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

static int mlxsw_sp_port_attr_set(struct net_device *dev,
				  const struct switchdev_attr *attr,
				  struct switchdev_trans *trans)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		err = mlxsw_sp_port_attr_stp_state_set(mlxsw_sp_port, trans,
						       attr->orig_dev,
						       attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		err = mlxsw_sp_port_attr_br_flags_set(mlxsw_sp_port, trans,
						      attr->orig_dev,
						      attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		err = mlxsw_sp_port_attr_br_ageing_set(mlxsw_sp_port, trans,
						       attr->u.ageing_time);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		err = mlxsw_sp_port_attr_br_vlan_set(mlxsw_sp_port, trans,
						     attr->orig_dev,
						     attr->u.vlan_filtering);
		break;
	case SWITCHDEV_ATTR_ID_PORT_MROUTER:
		err = mlxsw_sp_port_attr_mrouter_set(mlxsw_sp_port, trans,
						     attr->orig_dev,
						     attr->u.mrouter);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED:
		err = mlxsw_sp_port_mc_disabled_set(mlxsw_sp_port, trans,
						    attr->orig_dev,
						    attr->u.mc_disabled);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MROUTER:
		err = mlxsw_sp_port_attr_br_mrouter_set(mlxsw_sp_port, trans,
							attr->orig_dev,
							attr->u.mrouter);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	if (switchdev_trans_ph_commit(trans))
		mlxsw_sp_span_respin(mlxsw_sp_port->mlxsw_sp);

	return err;
}

static int
mlxsw_sp_port_vlan_fid_join(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan,
			    struct mlxsw_sp_bridge_port *bridge_port)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp_bridge_device *bridge_device;
	u8 local_port = mlxsw_sp_port->local_port;
	u16 vid = mlxsw_sp_port_vlan->vid;
	struct mlxsw_sp_fid *fid;
	int err;

	bridge_device = bridge_port->bridge_device;
	fid = bridge_device->ops->fid_get(bridge_device, vid);
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
	u8 local_port = mlxsw_sp_port->local_port;
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
			       struct mlxsw_sp_bridge_port *bridge_port)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp_bridge_vlan *bridge_vlan;
	u16 vid = mlxsw_sp_port_vlan->vid;
	int err;

	/* No need to continue if only VLAN flags were changed */
	if (mlxsw_sp_port_vlan->bridge_port)
		return 0;

	err = mlxsw_sp_port_vlan_fid_join(mlxsw_sp_port_vlan, bridge_port);
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
				 bridge_port->dev);
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
	bool last_port, last_vlan;

	if (WARN_ON(mlxsw_sp_fid_type(fid) != MLXSW_SP_FID_TYPE_8021Q &&
		    mlxsw_sp_fid_type(fid) != MLXSW_SP_FID_TYPE_8021D))
		return;

	bridge_port = mlxsw_sp_port_vlan->bridge_port;
	last_vlan = list_is_singular(&bridge_port->vlans_list);
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
	if (last_vlan)
		mlxsw_sp_bridge_port_mdb_flush(mlxsw_sp_port, bridge_port);

	mlxsw_sp_port_vlan_fid_leave(mlxsw_sp_port_vlan);

	mlxsw_sp_bridge_port_put(mlxsw_sp_port->mlxsw_sp->bridge, bridge_port);
	mlxsw_sp_port_vlan->bridge_port = NULL;
}

static int
mlxsw_sp_bridge_port_vlan_add(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_bridge_port *bridge_port,
			      u16 vid, bool is_untagged, bool is_pvid)
{
	u16 pvid = mlxsw_sp_port_pvid_determine(mlxsw_sp_port, vid, is_pvid);
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	u16 old_pvid = mlxsw_sp_port->pvid;
	int err;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_get(mlxsw_sp_port, vid);
	if (IS_ERR(mlxsw_sp_port_vlan))
		return PTR_ERR(mlxsw_sp_port_vlan);

	err = mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid, true,
				     is_untagged);
	if (err)
		goto err_port_vlan_set;

	err = mlxsw_sp_port_pvid_set(mlxsw_sp_port, pvid);
	if (err)
		goto err_port_pvid_set;

	err = mlxsw_sp_port_vlan_bridge_join(mlxsw_sp_port_vlan, bridge_port);
	if (err)
		goto err_port_vlan_bridge_join;

	return 0;

err_port_vlan_bridge_join:
	mlxsw_sp_port_pvid_set(mlxsw_sp_port, old_pvid);
err_port_pvid_set:
	mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid, false, false);
err_port_vlan_set:
	mlxsw_sp_port_vlan_put(mlxsw_sp_port_vlan);
	return err;
}

static int mlxsw_sp_port_vlans_add(struct mlxsw_sp_port *mlxsw_sp_port,
				   const struct switchdev_obj_port_vlan *vlan,
				   struct switchdev_trans *trans)
{
	bool flag_untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool flag_pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct net_device *orig_dev = vlan->obj.orig_dev;
	struct mlxsw_sp_bridge_port *bridge_port;
	u16 vid;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp->bridge, orig_dev);
	if (WARN_ON(!bridge_port))
		return -EINVAL;

	if (!bridge_port->bridge_device->vlan_enabled)
		return 0;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		int err;

		err = mlxsw_sp_bridge_port_vlan_add(mlxsw_sp_port, bridge_port,
						    vid, flag_untagged,
						    flag_pvid);
		if (err)
			return err;
	}

	return 0;
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
			 MLXSW_REG_SFD_REC_POLICY_STATIC_ENTRY;
}

static enum mlxsw_reg_sfd_op mlxsw_sp_sfd_op(bool adding)
{
	return adding ? MLXSW_REG_SFD_OP_WRITE_EDIT :
			MLXSW_REG_SFD_OP_WRITE_REMOVE;
}

static int __mlxsw_sp_port_fdb_uc_op(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				     const char *mac, u16 fid, bool adding,
				     enum mlxsw_reg_sfd_rec_action action,
				     bool dynamic)
{
	char *sfd_pl;
	u8 num_rec;
	int err;

	sfd_pl = kmalloc(MLXSW_REG_SFD_LEN, GFP_KERNEL);
	if (!sfd_pl)
		return -ENOMEM;

	mlxsw_reg_sfd_pack(sfd_pl, mlxsw_sp_sfd_op(adding), 0);
	mlxsw_reg_sfd_uc_pack(sfd_pl, 0, mlxsw_sp_sfd_rec_policy(dynamic),
			      mac, fid, action, local_port);
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

static int mlxsw_sp_port_fdb_uc_op(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				   const char *mac, u16 fid, bool adding,
				   bool dynamic)
{
	return __mlxsw_sp_port_fdb_uc_op(mlxsw_sp, local_port, mac, fid, adding,
					 MLXSW_REG_SFD_REC_ACTION_NOP, dynamic);
}

int mlxsw_sp_rif_fdb_op(struct mlxsw_sp *mlxsw_sp, const char *mac, u16 fid,
			bool adding)
{
	return __mlxsw_sp_port_fdb_uc_op(mlxsw_sp, 0, mac, fid, adding,
					 MLXSW_REG_SFD_REC_ACTION_FORWARD_IP_ROUTER,
					 false);
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
					       fdb_info->addr, fid_index,
					       adding, false);
	else
		return mlxsw_sp_port_fdb_uc_lag_op(mlxsw_sp,
						   bridge_port->lag_id,
						   fdb_info->addr, fid_index,
						   vid, adding, false);
}

static int mlxsw_sp_port_mdb_op(struct mlxsw_sp *mlxsw_sp, const char *addr,
				u16 fid, u16 mid_idx, bool adding)
{
	char *sfd_pl;
	u8 num_rec;
	int err;

	sfd_pl = kmalloc(MLXSW_REG_SFD_LEN, GFP_KERNEL);
	if (!sfd_pl)
		return -ENOMEM;

	mlxsw_reg_sfd_pack(sfd_pl, mlxsw_sp_sfd_op(adding), 0);
	mlxsw_reg_sfd_mc_pack(sfd_pl, 0, addr, fid,
			      MLXSW_REG_SFD_REC_ACTION_NOP, mid_idx);
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

static int mlxsw_sp_port_smid_full_entry(struct mlxsw_sp *mlxsw_sp, u16 mid_idx,
					 long *ports_bitmap,
					 bool set_router_port)
{
	char *smid_pl;
	int err, i;

	smid_pl = kmalloc(MLXSW_REG_SMID_LEN, GFP_KERNEL);
	if (!smid_pl)
		return -ENOMEM;

	mlxsw_reg_smid_pack(smid_pl, mid_idx, 0, false);
	for (i = 1; i < mlxsw_core_max_ports(mlxsw_sp->core); i++) {
		if (mlxsw_sp->ports[i])
			mlxsw_reg_smid_port_mask_set(smid_pl, i, 1);
	}

	mlxsw_reg_smid_port_mask_set(smid_pl,
				     mlxsw_sp_router_port(mlxsw_sp), 1);

	for_each_set_bit(i, ports_bitmap, mlxsw_core_max_ports(mlxsw_sp->core))
		mlxsw_reg_smid_port_set(smid_pl, i, 1);

	mlxsw_reg_smid_port_set(smid_pl, mlxsw_sp_router_port(mlxsw_sp),
				set_router_port);

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(smid), smid_pl);
	kfree(smid_pl);
	return err;
}

static int mlxsw_sp_port_smid_set(struct mlxsw_sp_port *mlxsw_sp_port,
				  u16 mid_idx, bool add)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char *smid_pl;
	int err;

	smid_pl = kmalloc(MLXSW_REG_SMID_LEN, GFP_KERNEL);
	if (!smid_pl)
		return -ENOMEM;

	mlxsw_reg_smid_pack(smid_pl, mid_idx, mlxsw_sp_port->local_port, add);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(smid), smid_pl);
	kfree(smid_pl);
	return err;
}

static struct
mlxsw_sp_mid *__mlxsw_sp_mc_get(struct mlxsw_sp_bridge_device *bridge_device,
				const unsigned char *addr,
				u16 fid)
{
	struct mlxsw_sp_mid *mid;

	list_for_each_entry(mid, &bridge_device->mids_list, list) {
		if (ether_addr_equal(mid->addr, addr) && mid->fid == fid)
			return mid;
	}
	return NULL;
}

static void
mlxsw_sp_bridge_port_get_ports_bitmap(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_bridge_port *bridge_port,
				      unsigned long *ports_bitmap)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	u64 max_lag_members, i;
	int lag_id;

	if (!bridge_port->lagged) {
		set_bit(bridge_port->system_port, ports_bitmap);
	} else {
		max_lag_members = MLXSW_CORE_RES_GET(mlxsw_sp->core,
						     MAX_LAG_MEMBERS);
		lag_id = bridge_port->lag_id;
		for (i = 0; i < max_lag_members; i++) {
			mlxsw_sp_port = mlxsw_sp_port_lagged_get(mlxsw_sp,
								 lag_id, i);
			if (mlxsw_sp_port)
				set_bit(mlxsw_sp_port->local_port,
					ports_bitmap);
		}
	}
}

static void
mlxsw_sp_mc_get_mrouters_bitmap(unsigned long *flood_bitmap,
				struct mlxsw_sp_bridge_device *bridge_device,
				struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_bridge_port *bridge_port;

	list_for_each_entry(bridge_port, &bridge_device->ports_list, list) {
		if (bridge_port->mrouter) {
			mlxsw_sp_bridge_port_get_ports_bitmap(mlxsw_sp,
							      bridge_port,
							      flood_bitmap);
		}
	}
}

static bool
mlxsw_sp_mc_write_mdb_entry(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_mid *mid,
			    struct mlxsw_sp_bridge_device *bridge_device)
{
	long *flood_bitmap;
	int num_of_ports;
	int alloc_size;
	u16 mid_idx;
	int err;

	mid_idx = find_first_zero_bit(mlxsw_sp->bridge->mids_bitmap,
				      MLXSW_SP_MID_MAX);
	if (mid_idx == MLXSW_SP_MID_MAX)
		return false;

	num_of_ports = mlxsw_core_max_ports(mlxsw_sp->core);
	alloc_size = sizeof(long) * BITS_TO_LONGS(num_of_ports);
	flood_bitmap = kzalloc(alloc_size, GFP_KERNEL);
	if (!flood_bitmap)
		return false;

	bitmap_copy(flood_bitmap,  mid->ports_in_mid, num_of_ports);
	mlxsw_sp_mc_get_mrouters_bitmap(flood_bitmap, bridge_device, mlxsw_sp);

	mid->mid = mid_idx;
	err = mlxsw_sp_port_smid_full_entry(mlxsw_sp, mid_idx, flood_bitmap,
					    bridge_device->mrouter);
	kfree(flood_bitmap);
	if (err)
		return false;

	err = mlxsw_sp_port_mdb_op(mlxsw_sp, mid->addr, mid->fid, mid_idx,
				   true);
	if (err)
		return false;

	set_bit(mid_idx, mlxsw_sp->bridge->mids_bitmap);
	mid->in_hw = true;
	return true;
}

static int mlxsw_sp_mc_remove_mdb_entry(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_mid *mid)
{
	if (!mid->in_hw)
		return 0;

	clear_bit(mid->mid, mlxsw_sp->bridge->mids_bitmap);
	mid->in_hw = false;
	return mlxsw_sp_port_mdb_op(mlxsw_sp, mid->addr, mid->fid, mid->mid,
				    false);
}

static struct
mlxsw_sp_mid *__mlxsw_sp_mc_alloc(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_bridge_device *bridge_device,
				  const unsigned char *addr,
				  u16 fid)
{
	struct mlxsw_sp_mid *mid;
	size_t alloc_size;

	mid = kzalloc(sizeof(*mid), GFP_KERNEL);
	if (!mid)
		return NULL;

	alloc_size = sizeof(unsigned long) *
		     BITS_TO_LONGS(mlxsw_core_max_ports(mlxsw_sp->core));

	mid->ports_in_mid = kzalloc(alloc_size, GFP_KERNEL);
	if (!mid->ports_in_mid)
		goto err_ports_in_mid_alloc;

	ether_addr_copy(mid->addr, addr);
	mid->fid = fid;
	mid->in_hw = false;

	if (!bridge_device->multicast_enabled)
		goto out;

	if (!mlxsw_sp_mc_write_mdb_entry(mlxsw_sp, mid, bridge_device))
		goto err_write_mdb_entry;

out:
	list_add_tail(&mid->list, &bridge_device->mids_list);
	return mid;

err_write_mdb_entry:
	kfree(mid->ports_in_mid);
err_ports_in_mid_alloc:
	kfree(mid);
	return NULL;
}

static int mlxsw_sp_port_remove_from_mid(struct mlxsw_sp_port *mlxsw_sp_port,
					 struct mlxsw_sp_mid *mid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	int err = 0;

	clear_bit(mlxsw_sp_port->local_port, mid->ports_in_mid);
	if (bitmap_empty(mid->ports_in_mid,
			 mlxsw_core_max_ports(mlxsw_sp->core))) {
		err = mlxsw_sp_mc_remove_mdb_entry(mlxsw_sp, mid);
		list_del(&mid->list);
		kfree(mid->ports_in_mid);
		kfree(mid);
	}
	return err;
}

static int mlxsw_sp_port_mdb_add(struct mlxsw_sp_port *mlxsw_sp_port,
				 const struct switchdev_obj_port_mdb *mdb,
				 struct switchdev_trans *trans)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct net_device *orig_dev = mdb->obj.orig_dev;
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct net_device *dev = mlxsw_sp_port->dev;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_mid *mid;
	u16 fid_index;
	int err = 0;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

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

	mid = __mlxsw_sp_mc_get(bridge_device, mdb->addr, fid_index);
	if (!mid) {
		mid = __mlxsw_sp_mc_alloc(mlxsw_sp, bridge_device, mdb->addr,
					  fid_index);
		if (!mid) {
			netdev_err(dev, "Unable to allocate MC group\n");
			return -ENOMEM;
		}
	}
	set_bit(mlxsw_sp_port->local_port, mid->ports_in_mid);

	if (!bridge_device->multicast_enabled)
		return 0;

	if (bridge_port->mrouter)
		return 0;

	err = mlxsw_sp_port_smid_set(mlxsw_sp_port, mid->mid, true);
	if (err) {
		netdev_err(dev, "Unable to set SMID\n");
		goto err_out;
	}

	return 0;

err_out:
	mlxsw_sp_port_remove_from_mid(mlxsw_sp_port, mid);
	return err;
}

static void
mlxsw_sp_bridge_mdb_mc_enable_sync(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct mlxsw_sp_bridge_device
				   *bridge_device)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_mid *mid;
	bool mc_enabled;

	mc_enabled = bridge_device->multicast_enabled;

	list_for_each_entry(mid, &bridge_device->mids_list, list) {
		if (mc_enabled)
			mlxsw_sp_mc_write_mdb_entry(mlxsw_sp, mid,
						    bridge_device);
		else
			mlxsw_sp_mc_remove_mdb_entry(mlxsw_sp, mid);
	}
}

static void
mlxsw_sp_port_mrouter_update_mdb(struct mlxsw_sp_port *mlxsw_sp_port,
				 struct mlxsw_sp_bridge_port *bridge_port,
				 bool add)
{
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_mid *mid;

	bridge_device = bridge_port->bridge_device;

	list_for_each_entry(mid, &bridge_device->mids_list, list) {
		if (!test_bit(mlxsw_sp_port->local_port, mid->ports_in_mid))
			mlxsw_sp_port_smid_set(mlxsw_sp_port, mid->mid, add);
	}
}

struct mlxsw_sp_span_respin_work {
	struct work_struct work;
	struct mlxsw_sp *mlxsw_sp;
};

static void mlxsw_sp_span_respin_work(struct work_struct *work)
{
	struct mlxsw_sp_span_respin_work *respin_work =
		container_of(work, struct mlxsw_sp_span_respin_work, work);

	rtnl_lock();
	mlxsw_sp_span_respin(respin_work->mlxsw_sp);
	rtnl_unlock();
	kfree(respin_work);
}

static void mlxsw_sp_span_respin_schedule(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_span_respin_work *respin_work;

	respin_work = kzalloc(sizeof(*respin_work), GFP_ATOMIC);
	if (!respin_work)
		return;

	INIT_WORK(&respin_work->work, mlxsw_sp_span_respin_work);
	respin_work->mlxsw_sp = mlxsw_sp;

	mlxsw_core_schedule_work(&respin_work->work);
}

static int mlxsw_sp_port_obj_add(struct net_device *dev,
				 const struct switchdev_obj *obj,
				 struct switchdev_trans *trans)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	const struct switchdev_obj_port_vlan *vlan;
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
		err = mlxsw_sp_port_vlans_add(mlxsw_sp_port, vlan, trans);

		if (switchdev_trans_ph_commit(trans)) {
			/* The event is emitted before the changes are actually
			 * applied to the bridge. Therefore schedule the respin
			 * call for later, so that the respin logic sees the
			 * updated bridge state.
			 */
			mlxsw_sp_span_respin_schedule(mlxsw_sp_port->mlxsw_sp);
		}
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = mlxsw_sp_port_mdb_add(mlxsw_sp_port,
					    SWITCHDEV_OBJ_PORT_MDB(obj),
					    trans);
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
	u16 pvid = mlxsw_sp_port->pvid == vid ? 0 : vid;
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_port_vlan))
		return;

	mlxsw_sp_port_vlan_bridge_leave(mlxsw_sp_port_vlan);
	mlxsw_sp_port_pvid_set(mlxsw_sp_port, pvid);
	mlxsw_sp_port_vlan_set(mlxsw_sp_port, vid, vid, false, false);
	mlxsw_sp_port_vlan_put(mlxsw_sp_port_vlan);
}

static int mlxsw_sp_port_vlans_del(struct mlxsw_sp_port *mlxsw_sp_port,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct net_device *orig_dev = vlan->obj.orig_dev;
	struct mlxsw_sp_bridge_port *bridge_port;
	u16 vid;

	bridge_port = mlxsw_sp_bridge_port_find(mlxsw_sp->bridge, orig_dev);
	if (WARN_ON(!bridge_port))
		return -EINVAL;

	if (!bridge_port->bridge_device->vlan_enabled)
		return 0;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++)
		mlxsw_sp_bridge_port_vlan_del(mlxsw_sp_port, bridge_port, vid);

	return 0;
}

static int
__mlxsw_sp_port_mdb_del(struct mlxsw_sp_port *mlxsw_sp_port,
			struct mlxsw_sp_bridge_port *bridge_port,
			struct mlxsw_sp_mid *mid)
{
	struct net_device *dev = mlxsw_sp_port->dev;
	int err;

	if (bridge_port->bridge_device->multicast_enabled &&
	    !bridge_port->mrouter) {
		err = mlxsw_sp_port_smid_set(mlxsw_sp_port, mid->mid, false);
		if (err)
			netdev_err(dev, "Unable to remove port from SMID\n");
	}

	err = mlxsw_sp_port_remove_from_mid(mlxsw_sp_port, mid);
	if (err)
		netdev_err(dev, "Unable to remove MC SFD\n");

	return err;
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
	struct mlxsw_sp_mid *mid;
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

	mid = __mlxsw_sp_mc_get(bridge_device, mdb->addr, fid_index);
	if (!mid) {
		netdev_err(dev, "Unable to remove port from MC DB\n");
		return -EINVAL;
	}

	return __mlxsw_sp_port_mdb_del(mlxsw_sp_port, bridge_port, mid);
}

static void
mlxsw_sp_bridge_port_mdb_flush(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct mlxsw_sp_bridge_port *bridge_port)
{
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_mid *mid, *tmp;

	bridge_device = bridge_port->bridge_device;

	list_for_each_entry_safe(mid, tmp, &bridge_device->mids_list, list) {
		if (test_bit(mlxsw_sp_port->local_port, mid->ports_in_mid)) {
			__mlxsw_sp_port_mdb_del(mlxsw_sp_port, bridge_port,
						mid);
		} else if (bridge_device->multicast_enabled &&
			   bridge_port->mrouter) {
			mlxsw_sp_port_smid_set(mlxsw_sp_port, mid->mid, false);
		}
	}
}

static int mlxsw_sp_port_obj_del(struct net_device *dev,
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

static const struct switchdev_ops mlxsw_sp_port_switchdev_ops = {
	.switchdev_port_attr_get	= mlxsw_sp_port_attr_get,
	.switchdev_port_attr_set	= mlxsw_sp_port_attr_set,
	.switchdev_port_obj_add		= mlxsw_sp_port_obj_add,
	.switchdev_port_obj_del		= mlxsw_sp_port_obj_del,
};

static int
mlxsw_sp_bridge_8021q_port_join(struct mlxsw_sp_bridge_device *bridge_device,
				struct mlxsw_sp_bridge_port *bridge_port,
				struct mlxsw_sp_port *mlxsw_sp_port,
				struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	if (is_vlan_dev(bridge_port->dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Can not enslave a VLAN device to a VLAN-aware bridge");
		return -EINVAL;
	}

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, 1);
	if (WARN_ON(!mlxsw_sp_port_vlan))
		return -EINVAL;

	/* Let VLAN-aware bridge take care of its own VLANs */
	mlxsw_sp_port_vlan_put(mlxsw_sp_port_vlan);

	return 0;
}

static void
mlxsw_sp_bridge_8021q_port_leave(struct mlxsw_sp_bridge_device *bridge_device,
				 struct mlxsw_sp_bridge_port *bridge_port,
				 struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_port_vlan_get(mlxsw_sp_port, 1);
	/* Make sure untagged frames are allowed to ingress */
	mlxsw_sp_port_pvid_set(mlxsw_sp_port, 1);
}

static struct mlxsw_sp_fid *
mlxsw_sp_bridge_8021q_fid_get(struct mlxsw_sp_bridge_device *bridge_device,
			      u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(bridge_device->dev);

	return mlxsw_sp_fid_8021q_get(mlxsw_sp, vid);
}

static const struct mlxsw_sp_bridge_ops mlxsw_sp_bridge_8021q_ops = {
	.port_join	= mlxsw_sp_bridge_8021q_port_join,
	.port_leave	= mlxsw_sp_bridge_8021q_port_leave,
	.fid_get	= mlxsw_sp_bridge_8021q_fid_get,
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

	vid = is_vlan_dev(dev) ? vlan_dev_vlan_id(dev) : 1;
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

	return mlxsw_sp_port_vlan_bridge_join(mlxsw_sp_port_vlan, bridge_port);
}

static void
mlxsw_sp_bridge_8021d_port_leave(struct mlxsw_sp_bridge_device *bridge_device,
				 struct mlxsw_sp_bridge_port *bridge_port,
				 struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct net_device *dev = bridge_port->dev;
	u16 vid;

	vid = is_vlan_dev(dev) ? vlan_dev_vlan_id(dev) : 1;
	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_port_vlan))
		return;

	mlxsw_sp_port_vlan_bridge_leave(mlxsw_sp_port_vlan);
}

static struct mlxsw_sp_fid *
mlxsw_sp_bridge_8021d_fid_get(struct mlxsw_sp_bridge_device *bridge_device,
			      u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(bridge_device->dev);

	return mlxsw_sp_fid_8021d_get(mlxsw_sp, bridge_device->dev->ifindex);
}

static const struct mlxsw_sp_bridge_ops mlxsw_sp_bridge_8021d_ops = {
	.port_join	= mlxsw_sp_bridge_8021d_port_join,
	.port_leave	= mlxsw_sp_bridge_8021d_port_leave,
	.fid_get	= mlxsw_sp_bridge_8021d_fid_get,
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

	bridge_port = mlxsw_sp_bridge_port_get(mlxsw_sp->bridge, brport_dev);
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
	mlxsw_sp_bridge_port_put(mlxsw_sp->bridge, bridge_port);
}

static void
mlxsw_sp_fdb_call_notifiers(enum switchdev_notifier_type type,
			    const char *mac, u16 vid,
			    struct net_device *dev)
{
	struct switchdev_notifier_fdb_info info;

	info.addr = mac;
	info.vid = vid;
	call_switchdev_notifiers(type, dev, &info.info);
}

static void mlxsw_sp_fdb_notify_mac_process(struct mlxsw_sp *mlxsw_sp,
					    char *sfn_pl, int rec_index,
					    bool adding)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;
	struct mlxsw_sp_bridge_device *bridge_device;
	struct mlxsw_sp_bridge_port *bridge_port;
	struct mlxsw_sp_port *mlxsw_sp_port;
	enum switchdev_notifier_type type;
	char mac[ETH_ALEN];
	u8 local_port;
	u16 vid, fid;
	bool do_notification = true;
	int err;

	mlxsw_reg_sfn_mac_unpack(sfn_pl, rec_index, mac, &fid, &local_port);
	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Incorrect local port in FDB notification\n");
		goto just_remove;
	}

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

do_fdb_op:
	err = mlxsw_sp_port_fdb_uc_op(mlxsw_sp, local_port, mac, fid,
				      adding, true);
	if (err) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to set FDB entry\n");
		return;
	}

	if (!do_notification)
		return;
	type = adding ? SWITCHDEV_FDB_ADD_TO_BRIDGE : SWITCHDEV_FDB_DEL_TO_BRIDGE;
	mlxsw_sp_fdb_call_notifiers(type, mac, vid, bridge_port->dev);

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
	mlxsw_sp_fdb_call_notifiers(type, mac, vid, bridge_port->dev);

	return;

just_remove:
	adding = false;
	do_notification = false;
	goto do_fdb_op;
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
	}
}

static void mlxsw_sp_fdb_notify_work_schedule(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_bridge *bridge = mlxsw_sp->bridge;

	mlxsw_core_schedule_dw(&bridge->fdb_notify.dw,
			       msecs_to_jiffies(bridge->fdb_notify.interval));
}

static void mlxsw_sp_fdb_notify_work(struct work_struct *work)
{
	struct mlxsw_sp_bridge *bridge;
	struct mlxsw_sp *mlxsw_sp;
	char *sfn_pl;
	u8 num_rec;
	int i;
	int err;

	sfn_pl = kmalloc(MLXSW_REG_SFN_LEN, GFP_KERNEL);
	if (!sfn_pl)
		return;

	bridge = container_of(work, struct mlxsw_sp_bridge, fdb_notify.dw.work);
	mlxsw_sp = bridge->mlxsw_sp;

	rtnl_lock();
	mlxsw_reg_sfn_pack(sfn_pl);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(sfn), sfn_pl);
	if (err) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to get FDB notifications\n");
		goto out;
	}
	num_rec = mlxsw_reg_sfn_num_rec_get(sfn_pl);
	for (i = 0; i < num_rec; i++)
		mlxsw_sp_fdb_notify_rec_process(mlxsw_sp, sfn_pl, i);

out:
	rtnl_unlock();
	kfree(sfn_pl);
	mlxsw_sp_fdb_notify_work_schedule(mlxsw_sp);
}

struct mlxsw_sp_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *dev;
	unsigned long event;
};

static void mlxsw_sp_switchdev_event_work(struct work_struct *work)
{
	struct mlxsw_sp_switchdev_event_work *switchdev_work =
		container_of(work, struct mlxsw_sp_switchdev_event_work, work);
	struct net_device *dev = switchdev_work->dev;
	struct switchdev_notifier_fdb_info *fdb_info;
	struct mlxsw_sp_port *mlxsw_sp_port;
	int err;

	rtnl_lock();
	mlxsw_sp_port = mlxsw_sp_port_dev_lower_find(dev);
	if (!mlxsw_sp_port)
		goto out;

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fdb_info = &switchdev_work->fdb_info;
		if (!fdb_info->added_by_user)
			break;
		err = mlxsw_sp_port_fdb_set(mlxsw_sp_port, fdb_info, true);
		if (err)
			break;
		mlxsw_sp_fdb_call_notifiers(SWITCHDEV_FDB_OFFLOADED,
					    fdb_info->addr,
					    fdb_info->vid, dev);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb_info = &switchdev_work->fdb_info;
		if (!fdb_info->added_by_user)
			break;
		mlxsw_sp_port_fdb_set(mlxsw_sp_port, fdb_info, false);
		break;
	case SWITCHDEV_FDB_ADD_TO_BRIDGE: /* fall through */
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

/* Called under rcu_read_lock() */
static int mlxsw_sp_switchdev_event(struct notifier_block *unused,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct mlxsw_sp_switchdev_event_work *switchdev_work;
	struct switchdev_notifier_fdb_info *fdb_info = ptr;

	if (!mlxsw_sp_port_dev_lower_find_rcu(dev))
		return NOTIFY_DONE;

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (!switchdev_work)
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work, mlxsw_sp_switchdev_event_work);
	switchdev_work->dev = dev;
	switchdev_work->event = event;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE: /* fall through */
	case SWITCHDEV_FDB_DEL_TO_DEVICE: /* fall through */
	case SWITCHDEV_FDB_ADD_TO_BRIDGE: /* fall through */
	case SWITCHDEV_FDB_DEL_TO_BRIDGE:
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
	default:
		kfree(switchdev_work);
		return NOTIFY_DONE;
	}

	mlxsw_core_schedule_work(&switchdev_work->work);

	return NOTIFY_DONE;

err_addr_alloc:
	kfree(switchdev_work);
	return NOTIFY_BAD;
}

static struct notifier_block mlxsw_sp_switchdev_notifier = {
	.notifier_call = mlxsw_sp_switchdev_event,
};

u8
mlxsw_sp_bridge_port_stp_state(struct mlxsw_sp_bridge_port *bridge_port)
{
	return bridge_port->stp_state;
}

static int mlxsw_sp_fdb_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_bridge *bridge = mlxsw_sp->bridge;
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

	INIT_DELAYED_WORK(&bridge->fdb_notify.dw, mlxsw_sp_fdb_notify_work);
	bridge->fdb_notify.interval = MLXSW_SP_DEFAULT_LEARNING_INTERVAL;
	mlxsw_sp_fdb_notify_work_schedule(mlxsw_sp);
	return 0;
}

static void mlxsw_sp_fdb_fini(struct mlxsw_sp *mlxsw_sp)
{
	cancel_delayed_work_sync(&mlxsw_sp->bridge->fdb_notify.dw);
	unregister_switchdev_notifier(&mlxsw_sp_switchdev_notifier);

}

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

	return mlxsw_sp_fdb_init(mlxsw_sp);
}

void mlxsw_sp_switchdev_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_fdb_fini(mlxsw_sp);
	WARN_ON(!list_empty(&mlxsw_sp->bridge->bridges_list));
	kfree(mlxsw_sp->bridge);
}

void mlxsw_sp_port_switchdev_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_port->dev->switchdev_ops = &mlxsw_sp_port_switchdev_ops;
}

void mlxsw_sp_port_switchdev_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{
}
