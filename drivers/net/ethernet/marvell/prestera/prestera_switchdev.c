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
	bool vlan_enabled;
	u16 bridge_id;
};

struct prestera_bridge_port {
	struct list_head head;
	struct net_device *dev;
	struct prestera_bridge *bridge;
	struct list_head vlan_list;
	refcount_t ref_count;
	unsigned long flags;
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

static struct workqueue_struct *swdev_wq;

static void prestera_bridge_port_put(struct prestera_bridge_port *br_port);

static int prestera_port_vid_stp_set(struct prestera_port *port, u16 vid,
				     u8 state);

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
		prestera_hw_fdb_flush_port(port, fdb_flush_mode);
	else if (last_port)
		prestera_hw_fdb_flush_vlan(port->sw, vid, fdb_flush_mode);
	else
		prestera_hw_fdb_flush_port_vlan(port, vid, fdb_flush_mode);

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

	INIT_LIST_HEAD(&bridge->port_list);

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

static struct prestera_bridge_port *
prestera_bridge_port_by_dev(struct prestera_switchdev *swdev,
			    struct net_device *dev)
{
	struct net_device *br_dev = netdev_master_upper_dev_get(dev);
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

	return br_port;
}

static void
prestera_bridge_port_destroy(struct prestera_bridge_port *br_port)
{
	list_del(&br_port->head);
	WARN_ON(!list_empty(&br_port->vlan_list));
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

	err = prestera_hw_port_flood_set(port, br_port->flags & BR_FLOOD);
	if (err)
		goto err_port_flood_set;

	err = prestera_hw_port_learning_set(port, br_port->flags & BR_LEARNING);
	if (err)
		goto err_port_learning_set;

	return 0;

err_port_learning_set:
	prestera_hw_port_flood_set(port, false);
err_port_flood_set:
	prestera_hw_bridge_port_delete(port, bridge->bridge_id);

	return err;
}

static int prestera_port_bridge_join(struct prestera_port *port,
				     struct net_device *upper)
{
	struct prestera_switchdev *swdev = port->sw->swdev;
	struct prestera_bridge_port *br_port;
	struct prestera_bridge *bridge;
	int err;

	bridge = prestera_bridge_by_dev(swdev, upper);
	if (!bridge) {
		bridge = prestera_bridge_create(swdev, upper);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
	}

	br_port = prestera_bridge_port_add(bridge, port->dev);
	if (IS_ERR(br_port)) {
		err = PTR_ERR(br_port);
		goto err_brport_create;
	}

	if (bridge->vlan_enabled)
		return 0;

	err = prestera_bridge_1d_port_join(br_port);
	if (err)
		goto err_port_join;

	return 0;

err_port_join:
	prestera_bridge_port_put(br_port);
err_brport_create:
	prestera_bridge_put(bridge);
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

static void prestera_port_bridge_leave(struct prestera_port *port,
				       struct net_device *upper)
{
	struct prestera_switchdev *swdev = port->sw->swdev;
	struct prestera_bridge_port *br_port;
	struct prestera_bridge *bridge;

	bridge = prestera_bridge_by_dev(swdev, upper);
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

	prestera_hw_port_learning_set(port, false);
	prestera_hw_port_flood_set(port, false);
	prestera_port_vid_stp_set(port, PRESTERA_VID_ALL, BR_STATE_FORWARDING);
	prestera_bridge_port_put(br_port);
}

int prestera_bridge_port_event(struct net_device *dev, unsigned long event,
			       void *ptr)
{
	struct netdev_notifier_changeupper_info *info = ptr;
	struct netlink_ext_ack *extack;
	struct prestera_port *port;
	struct net_device *upper;
	int err;

	extack = netdev_notifier_info_to_extack(&info->info);
	port = netdev_priv(dev);
	upper = info->upper_dev;

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		if (!netif_is_bridge_master(upper)) {
			NL_SET_ERR_MSG_MOD(extack, "Unknown upper device type");
			return -EINVAL;
		}

		if (!info->linking)
			break;

		if (netdev_has_any_upper_dev(upper)) {
			NL_SET_ERR_MSG_MOD(extack, "Upper device is already enslaved");
			return -EINVAL;
		}
		break;

	case NETDEV_CHANGEUPPER:
		if (!netif_is_bridge_master(upper))
			break;

		if (info->linking) {
			err = prestera_port_bridge_join(port, upper);
			if (err)
				return err;
		} else {
			prestera_port_bridge_leave(port, upper);
		}
		break;
	}

	return 0;
}

static int prestera_port_attr_br_flags_set(struct prestera_port *port,
					   struct net_device *dev,
					   struct switchdev_brport_flags flags)
{
	struct prestera_bridge_port *br_port;
	int err;

	br_port = prestera_bridge_port_by_dev(port->sw->swdev, dev);
	if (!br_port)
		return 0;

	if (flags.mask & BR_FLOOD) {
		err = prestera_hw_port_flood_set(port, flags.val & BR_FLOOD);
		if (err)
			return err;
	}

	if (flags.mask & BR_LEARNING) {
		err = prestera_hw_port_learning_set(port,
						    flags.val & BR_LEARNING);
		if (err)
			return err;
	}

	memcpy(&br_port->flags, &flags.val, sizeof(flags.val));

	return 0;
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

static int prestera_port_obj_attr_set(struct net_device *dev,
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
		    ~(BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD))
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
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static void
prestera_fdb_offload_notify(struct prestera_port *port,
			    struct switchdev_notifier_fdb_info *info)
{
	struct switchdev_notifier_fdb_info send_info;

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
		err = prestera_hw_fdb_add(port, fdb_info->addr, vid, false);
	else
		err = prestera_hw_fdb_del(port, fdb_info->addr, vid);

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
		if (!fdb_info->added_by_user)
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

	err = prestera_hw_port_flood_set(port, br_port->flags & BR_FLOOD);
	if (err)
		return err;

	err = prestera_hw_port_learning_set(port, br_port->flags & BR_LEARNING);
	if (err)
		goto err_port_learning_set;

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
	prestera_hw_port_learning_set(port, false);
err_port_learning_set:
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
	struct net_device *dev = vlan->obj.orig_dev;
	struct prestera_bridge_port *br_port;
	struct prestera_switch *sw = port->sw;
	struct prestera_bridge *bridge;

	if (netif_is_bridge_master(dev))
		return 0;

	br_port = prestera_bridge_port_by_dev(sw->swdev, dev);
	if (WARN_ON(!br_port))
		return -EINVAL;

	bridge = br_port->bridge;
	if (!bridge->vlan_enabled)
		return 0;

	return prestera_bridge_port_vlan_add(port, br_port,
					     vlan->vid, flag_untagged,
					     flag_pvid, extack);
}

static int prestera_port_obj_add(struct net_device *dev,
				 const struct switchdev_obj *obj,
				 struct netlink_ext_ack *extack)
{
	struct prestera_port *port = netdev_priv(dev);
	const struct switchdev_obj_port_vlan *vlan;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
		return prestera_port_vlans_add(port, vlan, extack);
	default:
		return -EOPNOTSUPP;
	}
}

static int prestera_port_vlans_del(struct prestera_port *port,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct net_device *dev = vlan->obj.orig_dev;
	struct prestera_bridge_port *br_port;
	struct prestera_switch *sw = port->sw;

	if (netif_is_bridge_master(dev))
		return -EOPNOTSUPP;

	br_port = prestera_bridge_port_by_dev(sw->swdev, dev);
	if (WARN_ON(!br_port))
		return -EINVAL;

	if (!br_port->bridge->vlan_enabled)
		return 0;

	prestera_bridge_port_vlan_del(port, br_port, vlan->vid);

	return 0;
}

static int prestera_port_obj_del(struct net_device *dev,
				 const struct switchdev_obj *obj)
{
	struct prestera_port *port = netdev_priv(dev);

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		return prestera_port_vlans_del(port, SWITCHDEV_OBJ_PORT_VLAN(obj));
	default:
		return -EOPNOTSUPP;
	}
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
		err = -EOPNOTSUPP;
	}

	return notifier_from_errno(err);
}

static void prestera_fdb_event(struct prestera_switch *sw,
			       struct prestera_event *evt, void *arg)
{
	struct switchdev_notifier_fdb_info info;
	struct prestera_port *port;

	port = prestera_find_port(sw, evt->fdb_evt.port_id);
	if (!port)
		return;

	info.addr = evt->fdb_evt.data.mac;
	info.vid = evt->fdb_evt.vid;
	info.offloaded = true;

	rtnl_lock();

	switch (evt->id) {
	case PRESTERA_FDB_EVENT_LEARNED:
		call_switchdev_notifiers(SWITCHDEV_FDB_ADD_TO_BRIDGE,
					 port->dev, &info.info, NULL);
		break;
	case PRESTERA_FDB_EVENT_AGED:
		call_switchdev_notifiers(SWITCHDEV_FDB_DEL_TO_BRIDGE,
					 port->dev, &info.info, NULL);
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
