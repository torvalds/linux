// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/if_bridge.h>
#include <net/switchdev.h>

#include "sparx5_main_regs.h"
#include "sparx5_main.h"

static struct workqueue_struct *sparx5_owq;

struct sparx5_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *dev;
	unsigned long event;
};

static int sparx5_port_attr_pre_bridge_flags(struct sparx5_port *port,
					     struct switchdev_brport_flags flags)
{
	if (flags.mask & ~(BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD))
		return -EINVAL;

	return 0;
}

static void sparx5_port_attr_bridge_flags(struct sparx5_port *port,
					  struct switchdev_brport_flags flags)
{
	int pgid;

	if (flags.mask & BR_MCAST_FLOOD)
		for (pgid = PGID_MC_FLOOD; pgid <= PGID_IPV6_MC_CTRL; pgid++)
			sparx5_pgid_update_mask(port, pgid, !!(flags.val & BR_MCAST_FLOOD));
	if (flags.mask & BR_FLOOD)
		sparx5_pgid_update_mask(port, PGID_UC_FLOOD, !!(flags.val & BR_FLOOD));
	if (flags.mask & BR_BCAST_FLOOD)
		sparx5_pgid_update_mask(port, PGID_BCAST, !!(flags.val & BR_BCAST_FLOOD));
}

static void sparx5_attr_stp_state_set(struct sparx5_port *port,
				      u8 state)
{
	struct sparx5 *sparx5 = port->sparx5;

	if (!test_bit(port->portno, sparx5->bridge_mask)) {
		netdev_err(port->ndev,
			   "Controlling non-bridged port %d?\n", port->portno);
		return;
	}

	switch (state) {
	case BR_STATE_FORWARDING:
		set_bit(port->portno, sparx5->bridge_fwd_mask);
		fallthrough;
	case BR_STATE_LEARNING:
		set_bit(port->portno, sparx5->bridge_lrn_mask);
		break;

	default:
		/* All other states treated as blocking */
		clear_bit(port->portno, sparx5->bridge_fwd_mask);
		clear_bit(port->portno, sparx5->bridge_lrn_mask);
		break;
	}

	/* apply the bridge_fwd_mask to all the ports */
	sparx5_update_fwd(sparx5);
}

static void sparx5_port_attr_ageing_set(struct sparx5_port *port,
					unsigned long ageing_clock_t)
{
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock_t);
	u32 ageing_time = jiffies_to_msecs(ageing_jiffies);

	sparx5_set_ageing(port->sparx5, ageing_time);
}

static int sparx5_port_attr_set(struct net_device *dev, const void *ctx,
				const struct switchdev_attr *attr,
				struct netlink_ext_ack *extack)
{
	struct sparx5_port *port = netdev_priv(dev);

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		return sparx5_port_attr_pre_bridge_flags(port,
							 attr->u.brport_flags);
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		sparx5_port_attr_bridge_flags(port, attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		sparx5_attr_stp_state_set(port, attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		sparx5_port_attr_ageing_set(port, attr->u.ageing_time);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		port->vlan_aware = attr->u.vlan_filtering;
		sparx5_vlan_port_apply(port->sparx5, port);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int sparx5_port_bridge_join(struct sparx5_port *port,
				   struct net_device *bridge,
				   struct netlink_ext_ack *extack)
{
	struct sparx5 *sparx5 = port->sparx5;
	struct net_device *ndev = port->ndev;
	int err;

	if (bitmap_empty(sparx5->bridge_mask, SPX5_PORTS))
		/* First bridged port */
		sparx5->hw_bridge_dev = bridge;
	else
		if (sparx5->hw_bridge_dev != bridge)
			/* This is adding the port to a second bridge, this is
			 * unsupported
			 */
			return -ENODEV;

	set_bit(port->portno, sparx5->bridge_mask);

	err = switchdev_bridge_port_offload(ndev, ndev, NULL, NULL, NULL,
					    false, extack);
	if (err)
		goto err_switchdev_offload;

	/* Port enters in bridge mode therefor don't need to copy to CPU
	 * frames for multicast in case the bridge is not requesting them
	 */
	__dev_mc_unsync(ndev, sparx5_mc_unsync);

	return 0;

err_switchdev_offload:
	clear_bit(port->portno, sparx5->bridge_mask);
	return err;
}

static void sparx5_port_bridge_leave(struct sparx5_port *port,
				     struct net_device *bridge)
{
	struct sparx5 *sparx5 = port->sparx5;

	switchdev_bridge_port_unoffload(port->ndev, NULL, NULL, NULL);

	clear_bit(port->portno, sparx5->bridge_mask);
	if (bitmap_empty(sparx5->bridge_mask, SPX5_PORTS))
		sparx5->hw_bridge_dev = NULL;

	/* Clear bridge vlan settings before updating the port settings */
	port->vlan_aware = 0;
	port->pvid = NULL_VID;
	port->vid = NULL_VID;

	/* Port enters in host more therefore restore mc list */
	__dev_mc_sync(port->ndev, sparx5_mc_sync, sparx5_mc_unsync);
}

static int sparx5_port_changeupper(struct net_device *dev,
				   struct netdev_notifier_changeupper_info *info)
{
	struct sparx5_port *port = netdev_priv(dev);
	struct netlink_ext_ack *extack;
	int err = 0;

	extack = netdev_notifier_info_to_extack(&info->info);

	if (netif_is_bridge_master(info->upper_dev)) {
		if (info->linking)
			err = sparx5_port_bridge_join(port, info->upper_dev,
						      extack);
		else
			sparx5_port_bridge_leave(port, info->upper_dev);

		sparx5_vlan_port_apply(port->sparx5, port);
	}

	return err;
}

static int sparx5_port_add_addr(struct net_device *dev, bool up)
{
	struct sparx5_port *port = netdev_priv(dev);
	struct sparx5 *sparx5 = port->sparx5;
	u16 vid = port->pvid;

	if (up)
		sparx5_mact_learn(sparx5, PGID_CPU, port->ndev->dev_addr, vid);
	else
		sparx5_mact_forget(sparx5, port->ndev->dev_addr, vid);

	return 0;
}

static int sparx5_netdevice_port_event(struct net_device *dev,
				       struct notifier_block *nb,
				       unsigned long event, void *ptr)
{
	int err = 0;

	if (!sparx5_netdevice_check(dev))
		return 0;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		err = sparx5_port_changeupper(dev, ptr);
		break;
	case NETDEV_PRE_UP:
		err = sparx5_port_add_addr(dev, true);
		break;
	case NETDEV_DOWN:
		err = sparx5_port_add_addr(dev, false);
		break;
	}

	return err;
}

static int sparx5_netdevice_event(struct notifier_block *nb,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int ret = 0;

	ret = sparx5_netdevice_port_event(dev, nb, event, ptr);

	return notifier_from_errno(ret);
}

static void sparx5_switchdev_bridge_fdb_event_work(struct work_struct *work)
{
	struct sparx5_switchdev_event_work *switchdev_work =
		container_of(work, struct sparx5_switchdev_event_work, work);
	struct net_device *dev = switchdev_work->dev;
	struct switchdev_notifier_fdb_info *fdb_info;
	struct sparx5_port *port;
	struct sparx5 *sparx5;

	rtnl_lock();
	if (!sparx5_netdevice_check(dev))
		goto out;

	port = netdev_priv(dev);
	sparx5 = port->sparx5;

	fdb_info = &switchdev_work->fdb_info;

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		if (!fdb_info->added_by_user)
			break;
		sparx5_add_mact_entry(sparx5, port, fdb_info->addr,
				      fdb_info->vid);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		if (!fdb_info->added_by_user)
			break;
		sparx5_del_mact_entry(sparx5, fdb_info->addr, fdb_info->vid);
		break;
	}

out:
	rtnl_unlock();
	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(dev);
}

static void sparx5_schedule_work(struct work_struct *work)
{
	queue_work(sparx5_owq, work);
}

static int sparx5_switchdev_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct sparx5_switchdev_event_work *switchdev_work;
	struct switchdev_notifier_fdb_info *fdb_info;
	struct switchdev_notifier_info *info = ptr;
	int err;

	switch (event) {
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     sparx5_netdevice_check,
						     sparx5_port_attr_set);
		return notifier_from_errno(err);
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fallthrough;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
		if (!switchdev_work)
			return NOTIFY_BAD;

		switchdev_work->dev = dev;
		switchdev_work->event = event;

		fdb_info = container_of(info,
					struct switchdev_notifier_fdb_info,
					info);
		INIT_WORK(&switchdev_work->work,
			  sparx5_switchdev_bridge_fdb_event_work);
		memcpy(&switchdev_work->fdb_info, ptr,
		       sizeof(switchdev_work->fdb_info));
		switchdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
		if (!switchdev_work->fdb_info.addr)
			goto err_addr_alloc;

		ether_addr_copy((u8 *)switchdev_work->fdb_info.addr,
				fdb_info->addr);
		dev_hold(dev);

		sparx5_schedule_work(&switchdev_work->work);
		break;
	}

	return NOTIFY_DONE;
err_addr_alloc:
	kfree(switchdev_work);
	return NOTIFY_BAD;
}

static void sparx5_sync_port_dev_addr(struct sparx5 *sparx5,
				      struct sparx5_port *port,
				      u16 vid, bool add)
{
	if (!port ||
	    !test_bit(port->portno, sparx5->bridge_mask))
		return; /* Skip null/host interfaces */

	/* Bridge connects to vid? */
	if (add) {
		/* Add port MAC address from the VLAN */
		sparx5_mact_learn(sparx5, PGID_CPU,
				  port->ndev->dev_addr, vid);
	} else {
		/* Control port addr visibility depending on
		 * port VLAN connectivity.
		 */
		if (test_bit(port->portno, sparx5->vlan_mask[vid]))
			sparx5_mact_learn(sparx5, PGID_CPU,
					  port->ndev->dev_addr, vid);
		else
			sparx5_mact_forget(sparx5,
					   port->ndev->dev_addr, vid);
	}
}

static void sparx5_sync_bridge_dev_addr(struct net_device *dev,
					struct sparx5 *sparx5,
					u16 vid, bool add)
{
	int i;

	/* First, handle bridge address'es */
	if (add) {
		sparx5_mact_learn(sparx5, PGID_CPU, dev->dev_addr,
				  vid);
		sparx5_mact_learn(sparx5, PGID_BCAST, dev->broadcast,
				  vid);
	} else {
		sparx5_mact_forget(sparx5, dev->dev_addr, vid);
		sparx5_mact_forget(sparx5, dev->broadcast, vid);
	}

	/* Now look at bridged ports */
	for (i = 0; i < SPX5_PORTS; i++)
		sparx5_sync_port_dev_addr(sparx5, sparx5->ports[i], vid, add);
}

static int sparx5_handle_port_vlan_add(struct net_device *dev,
				       struct notifier_block *nb,
				       const struct switchdev_obj_port_vlan *v)
{
	struct sparx5_port *port = netdev_priv(dev);

	if (netif_is_bridge_master(dev)) {
		struct sparx5 *sparx5 =
			container_of(nb, struct sparx5,
				     switchdev_blocking_nb);

		sparx5_sync_bridge_dev_addr(dev, sparx5, v->vid, true);
		return 0;
	}

	if (!sparx5_netdevice_check(dev))
		return -EOPNOTSUPP;

	return sparx5_vlan_vid_add(port, v->vid,
				  v->flags & BRIDGE_VLAN_INFO_PVID,
				  v->flags & BRIDGE_VLAN_INFO_UNTAGGED);
}

static int sparx5_handle_port_obj_add(struct net_device *dev,
				      struct notifier_block *nb,
				      struct switchdev_notifier_port_obj_info *info)
{
	const struct switchdev_obj *obj = info->obj;
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = sparx5_handle_port_vlan_add(dev, nb,
						  SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	info->handled = true;
	return err;
}

static int sparx5_handle_port_vlan_del(struct net_device *dev,
				       struct notifier_block *nb,
				       u16 vid)
{
	struct sparx5_port *port = netdev_priv(dev);
	int ret;

	/* Master bridge? */
	if (netif_is_bridge_master(dev)) {
		struct sparx5 *sparx5 =
			container_of(nb, struct sparx5,
				     switchdev_blocking_nb);

		sparx5_sync_bridge_dev_addr(dev, sparx5, vid, false);
		return 0;
	}

	if (!sparx5_netdevice_check(dev))
		return -EOPNOTSUPP;

	ret = sparx5_vlan_vid_del(port, vid);
	if (ret)
		return ret;

	/* Delete the port MAC address with the matching VLAN information */
	sparx5_mact_forget(port->sparx5, port->ndev->dev_addr, vid);

	return 0;
}

static int sparx5_handle_port_obj_del(struct net_device *dev,
				      struct notifier_block *nb,
				      struct switchdev_notifier_port_obj_info *info)
{
	const struct switchdev_obj *obj = info->obj;
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = sparx5_handle_port_vlan_del(dev, nb,
						  SWITCHDEV_OBJ_PORT_VLAN(obj)->vid);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	info->handled = true;
	return err;
}

static int sparx5_switchdev_blocking_event(struct notifier_block *nb,
					   unsigned long event,
					   void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = sparx5_handle_port_obj_add(dev, nb, ptr);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_OBJ_DEL:
		err = sparx5_handle_port_obj_del(dev, nb, ptr);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     sparx5_netdevice_check,
						     sparx5_port_attr_set);
		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

int sparx5_register_notifier_blocks(struct sparx5 *s5)
{
	int err;

	s5->netdevice_nb.notifier_call = sparx5_netdevice_event;
	err = register_netdevice_notifier(&s5->netdevice_nb);
	if (err)
		return err;

	s5->switchdev_nb.notifier_call = sparx5_switchdev_event;
	err = register_switchdev_notifier(&s5->switchdev_nb);
	if (err)
		goto err_switchdev_nb;

	s5->switchdev_blocking_nb.notifier_call = sparx5_switchdev_blocking_event;
	err = register_switchdev_blocking_notifier(&s5->switchdev_blocking_nb);
	if (err)
		goto err_switchdev_blocking_nb;

	sparx5_owq = alloc_ordered_workqueue("sparx5_order", 0);
	if (!sparx5_owq) {
		err = -ENOMEM;
		goto err_switchdev_blocking_nb;
	}

	return 0;

err_switchdev_blocking_nb:
	unregister_switchdev_notifier(&s5->switchdev_nb);
err_switchdev_nb:
	unregister_netdevice_notifier(&s5->netdevice_nb);

	return err;
}

void sparx5_unregister_notifier_blocks(struct sparx5 *s5)
{
	destroy_workqueue(sparx5_owq);

	unregister_switchdev_blocking_notifier(&s5->switchdev_blocking_nb);
	unregister_switchdev_notifier(&s5->switchdev_nb);
	unregister_netdevice_notifier(&s5->netdevice_nb);
}
