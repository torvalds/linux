/* SPDX-License-Identifier: GPL-2.0 */
/* Texas Instruments K3 AM65 Ethernet Switchdev Driver
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <net/switchdev.h>

#include "am65-cpsw-nuss.h"
#include "am65-cpsw-switchdev.h"
#include "cpsw_ale.h"

struct am65_cpsw_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct am65_cpsw_port *port;
	unsigned long event;
};

static int am65_cpsw_port_stp_state_set(struct am65_cpsw_port *port, u8 state)
{
	struct am65_cpsw_common *cpsw = port->common;
	u8 cpsw_state;
	int ret = 0;

	switch (state) {
	case BR_STATE_FORWARDING:
		cpsw_state = ALE_PORT_STATE_FORWARD;
		break;
	case BR_STATE_LEARNING:
		cpsw_state = ALE_PORT_STATE_LEARN;
		break;
	case BR_STATE_DISABLED:
		cpsw_state = ALE_PORT_STATE_DISABLE;
		break;
	case BR_STATE_LISTENING:
	case BR_STATE_BLOCKING:
		cpsw_state = ALE_PORT_STATE_BLOCK;
		break;
	default:
		return -EOPNOTSUPP;
	}

	ret = cpsw_ale_control_set(cpsw->ale, port->port_id,
				   ALE_PORT_STATE, cpsw_state);
	netdev_dbg(port->ndev, "ale state: %u\n", cpsw_state);

	return ret;
}

static int am65_cpsw_port_attr_br_flags_set(struct am65_cpsw_port *port,
					    struct net_device *orig_dev,
					    struct switchdev_brport_flags flags)
{
	struct am65_cpsw_common *cpsw = port->common;

	if (flags.mask & BR_MCAST_FLOOD) {
		bool unreg_mcast_add = false;

		if (flags.val & BR_MCAST_FLOOD)
			unreg_mcast_add = true;

		netdev_dbg(port->ndev, "BR_MCAST_FLOOD: %d port %u\n",
			   unreg_mcast_add, port->port_id);

		cpsw_ale_set_unreg_mcast(cpsw->ale, BIT(port->port_id),
					 unreg_mcast_add);
	}

	return 0;
}

static int am65_cpsw_port_attr_br_flags_pre_set(struct net_device *netdev,
						struct switchdev_brport_flags flags)
{
	if (flags.mask & ~(BR_LEARNING | BR_MCAST_FLOOD))
		return -EINVAL;

	return 0;
}

static int am65_cpsw_port_attr_set(struct net_device *ndev,
				   const struct switchdev_attr *attr,
				   struct netlink_ext_ack *extack)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	int ret;

	netdev_dbg(ndev, "attr: id %u port: %u\n", attr->id, port->port_id);

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		ret = am65_cpsw_port_attr_br_flags_pre_set(ndev,
							   attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		ret = am65_cpsw_port_stp_state_set(port, attr->u.stp_state);
		netdev_dbg(ndev, "stp state: %u\n", attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		ret = am65_cpsw_port_attr_br_flags_set(port, attr->orig_dev,
						       attr->u.brport_flags);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static u16 am65_cpsw_get_pvid(struct am65_cpsw_port *port)
{
	struct am65_cpsw_common *cpsw = port->common;
	struct am65_cpsw_host *host_p = am65_common_get_host(cpsw);
	u32 pvid;

	if (port->port_id)
		pvid = readl(port->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);
	else
		pvid = readl(host_p->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);

	pvid = pvid & 0xfff;

	return pvid;
}

static void am65_cpsw_set_pvid(struct am65_cpsw_port *port, u16 vid, bool cfi, u32 cos)
{
	struct am65_cpsw_common *cpsw = port->common;
	struct am65_cpsw_host *host_p = am65_common_get_host(cpsw);
	u32 pvid;

	pvid = vid;
	pvid |= cfi ? BIT(12) : 0;
	pvid |= (cos & 0x7) << 13;

	if (port->port_id)
		writel(pvid, port->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);
	else
		writel(pvid, host_p->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);
}

static int am65_cpsw_port_vlan_add(struct am65_cpsw_port *port, bool untag, bool pvid,
				   u16 vid, struct net_device *orig_dev)
{
	bool cpu_port = netif_is_bridge_master(orig_dev);
	struct am65_cpsw_common *cpsw = port->common;
	int unreg_mcast_mask = 0;
	int reg_mcast_mask = 0;
	int untag_mask = 0;
	int port_mask;
	int ret = 0;
	u32 flags;

	if (cpu_port) {
		port_mask = BIT(HOST_PORT_NUM);
		flags = orig_dev->flags;
		unreg_mcast_mask = port_mask;
	} else {
		port_mask = BIT(port->port_id);
		flags = port->ndev->flags;
	}

	if (flags & IFF_MULTICAST)
		reg_mcast_mask = port_mask;

	if (untag)
		untag_mask = port_mask;

	ret = cpsw_ale_vlan_add_modify(cpsw->ale, vid, port_mask, untag_mask,
				       reg_mcast_mask, unreg_mcast_mask);
	if (ret) {
		netdev_err(port->ndev, "Unable to add vlan\n");
		return ret;
	}

	if (cpu_port)
		cpsw_ale_add_ucast(cpsw->ale, port->slave.mac_addr,
				   HOST_PORT_NUM, ALE_VLAN | ALE_SECURE, vid);
	if (!pvid)
		return ret;

	am65_cpsw_set_pvid(port, vid, 0, 0);

	netdev_dbg(port->ndev, "VID add: %s: vid:%u ports:%X\n",
		   port->ndev->name, vid, port_mask);

	return ret;
}

static int am65_cpsw_port_vlan_del(struct am65_cpsw_port *port, u16 vid,
				   struct net_device *orig_dev)
{
	bool cpu_port = netif_is_bridge_master(orig_dev);
	struct am65_cpsw_common *cpsw = port->common;
	int port_mask;
	int ret = 0;

	if (cpu_port)
		port_mask = BIT(HOST_PORT_NUM);
	else
		port_mask = BIT(port->port_id);

	ret = cpsw_ale_del_vlan(cpsw->ale, vid, port_mask);
	if (ret != 0)
		return ret;

	/* We don't care for the return value here, error is returned only if
	 * the unicast entry is not present
	 */
	if (cpu_port)
		cpsw_ale_del_ucast(cpsw->ale, port->slave.mac_addr,
				   HOST_PORT_NUM, ALE_VLAN, vid);

	if (vid == am65_cpsw_get_pvid(port))
		am65_cpsw_set_pvid(port, 0, 0, 0);

	/* We don't care for the return value here, error is returned only if
	 * the multicast entry is not present
	 */
	cpsw_ale_del_mcast(cpsw->ale, port->ndev->broadcast, port_mask,
			   ALE_VLAN, vid);
	netdev_dbg(port->ndev, "VID del: %s: vid:%u ports:%X\n",
		   port->ndev->name, vid, port_mask);

	return ret;
}

static int am65_cpsw_port_vlans_add(struct am65_cpsw_port *port,
				    const struct switchdev_obj_port_vlan *vlan)
{
	bool untag = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct net_device *orig_dev = vlan->obj.orig_dev;
	bool cpu_port = netif_is_bridge_master(orig_dev);
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;

	netdev_dbg(port->ndev, "VID add: %s: vid:%u flags:%X\n",
		   port->ndev->name, vlan->vid, vlan->flags);

	if (cpu_port && !(vlan->flags & BRIDGE_VLAN_INFO_BRENTRY))
		return 0;

	return am65_cpsw_port_vlan_add(port, untag, pvid, vlan->vid, orig_dev);
}

static int am65_cpsw_port_vlans_del(struct am65_cpsw_port *port,
				    const struct switchdev_obj_port_vlan *vlan)

{
	return am65_cpsw_port_vlan_del(port, vlan->vid, vlan->obj.orig_dev);
}

static int am65_cpsw_port_mdb_add(struct am65_cpsw_port *port,
				  struct switchdev_obj_port_mdb *mdb)

{
	struct net_device *orig_dev = mdb->obj.orig_dev;
	bool cpu_port = netif_is_bridge_master(orig_dev);
	struct am65_cpsw_common *cpsw = port->common;
	int port_mask;
	int err;

	if (cpu_port)
		port_mask = BIT(HOST_PORT_NUM);
	else
		port_mask = BIT(port->port_id);

	err = cpsw_ale_add_mcast(cpsw->ale, mdb->addr, port_mask,
				 ALE_VLAN, mdb->vid, 0);
	netdev_dbg(port->ndev, "MDB add: %s: vid %u:%pM  ports: %X\n",
		   port->ndev->name, mdb->vid, mdb->addr, port_mask);

	return err;
}

static int am65_cpsw_port_mdb_del(struct am65_cpsw_port *port,
				  struct switchdev_obj_port_mdb *mdb)

{
	struct net_device *orig_dev = mdb->obj.orig_dev;
	bool cpu_port = netif_is_bridge_master(orig_dev);
	struct am65_cpsw_common *cpsw = port->common;
	int del_mask;

	if (cpu_port)
		del_mask = BIT(HOST_PORT_NUM);
	else
		del_mask = BIT(port->port_id);

	/* Ignore error as error code is returned only when entry is already removed */
	cpsw_ale_del_mcast(cpsw->ale, mdb->addr, del_mask,
			   ALE_VLAN, mdb->vid);
	netdev_dbg(port->ndev, "MDB del: %s: vid %u:%pM  ports: %X\n",
		   port->ndev->name, mdb->vid, mdb->addr, del_mask);

	return 0;
}

static int am65_cpsw_port_obj_add(struct net_device *ndev,
				  const struct switchdev_obj *obj,
				  struct netlink_ext_ack *extack)
{
	struct switchdev_obj_port_vlan *vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
	struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	int err = 0;

	netdev_dbg(ndev, "obj_add: id %u port: %u\n", obj->id, port->port_id);

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = am65_cpsw_port_vlans_add(port, vlan);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		err = am65_cpsw_port_mdb_add(port, mdb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int am65_cpsw_port_obj_del(struct net_device *ndev,
				  const struct switchdev_obj *obj)
{
	struct switchdev_obj_port_vlan *vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
	struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	int err = 0;

	netdev_dbg(ndev, "obj_del: id %u port: %u\n", obj->id, port->port_id);

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = am65_cpsw_port_vlans_del(port, vlan);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		err = am65_cpsw_port_mdb_del(port, mdb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static void am65_cpsw_fdb_offload_notify(struct net_device *ndev,
					 struct switchdev_notifier_fdb_info *rcv)
{
	struct switchdev_notifier_fdb_info info;

	info.addr = rcv->addr;
	info.vid = rcv->vid;
	info.offloaded = true;
	call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED,
				 ndev, &info.info, NULL);
}

static void am65_cpsw_switchdev_event_work(struct work_struct *work)
{
	struct am65_cpsw_switchdev_event_work *switchdev_work =
		container_of(work, struct am65_cpsw_switchdev_event_work, work);
	struct am65_cpsw_port *port = switchdev_work->port;
	struct switchdev_notifier_fdb_info *fdb;
	struct am65_cpsw_common *cpsw = port->common;
	int port_id = port->port_id;

	rtnl_lock();
	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fdb = &switchdev_work->fdb_info;

		netdev_dbg(port->ndev, "cpsw_fdb_add: MACID = %pM vid = %u flags = %u %u -- port %d\n",
			   fdb->addr, fdb->vid, fdb->added_by_user,
			   fdb->offloaded, port_id);

		if (!fdb->added_by_user || fdb->is_local)
			break;
		if (memcmp(port->slave.mac_addr, (u8 *)fdb->addr, ETH_ALEN) == 0)
			port_id = HOST_PORT_NUM;

		cpsw_ale_add_ucast(cpsw->ale, (u8 *)fdb->addr, port_id,
				   fdb->vid ? ALE_VLAN : 0, fdb->vid);
		am65_cpsw_fdb_offload_notify(port->ndev, fdb);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb = &switchdev_work->fdb_info;

		netdev_dbg(port->ndev, "cpsw_fdb_del: MACID = %pM vid = %u flags = %u %u -- port %d\n",
			   fdb->addr, fdb->vid, fdb->added_by_user,
			   fdb->offloaded, port_id);

		if (!fdb->added_by_user || fdb->is_local)
			break;
		if (memcmp(port->slave.mac_addr, (u8 *)fdb->addr, ETH_ALEN) == 0)
			port_id = HOST_PORT_NUM;

		cpsw_ale_del_ucast(cpsw->ale, (u8 *)fdb->addr, port_id,
				   fdb->vid ? ALE_VLAN : 0, fdb->vid);
		break;
	default:
		break;
	}
	rtnl_unlock();

	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(port->ndev);
}

/* called under rcu_read_lock() */
static int am65_cpsw_switchdev_event(struct notifier_block *unused,
				     unsigned long event, void *ptr)
{
	struct net_device *ndev = switchdev_notifier_info_to_dev(ptr);
	struct am65_cpsw_switchdev_event_work *switchdev_work;
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct switchdev_notifier_fdb_info *fdb_info = ptr;
	int err;

	if (event == SWITCHDEV_PORT_ATTR_SET) {
		err = switchdev_handle_port_attr_set(ndev, ptr,
						     am65_cpsw_port_dev_check,
						     am65_cpsw_port_attr_set);
		return notifier_from_errno(err);
	}

	if (!am65_cpsw_port_dev_check(ndev))
		return NOTIFY_DONE;

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (WARN_ON(!switchdev_work))
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work, am65_cpsw_switchdev_event_work);
	switchdev_work->port = port;
	switchdev_work->event = event;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		memcpy(&switchdev_work->fdb_info, ptr,
		       sizeof(switchdev_work->fdb_info));
		switchdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
		if (!switchdev_work->fdb_info.addr)
			goto err_addr_alloc;
		ether_addr_copy((u8 *)switchdev_work->fdb_info.addr,
				fdb_info->addr);
		dev_hold(ndev);
		break;
	default:
		kfree(switchdev_work);
		return NOTIFY_DONE;
	}

	queue_work(system_long_wq, &switchdev_work->work);

	return NOTIFY_DONE;

err_addr_alloc:
	kfree(switchdev_work);
	return NOTIFY_BAD;
}

static struct notifier_block cpsw_switchdev_notifier = {
	.notifier_call = am65_cpsw_switchdev_event,
};

static int am65_cpsw_switchdev_blocking_event(struct notifier_block *unused,
					      unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = switchdev_handle_port_obj_add(dev, ptr,
						    am65_cpsw_port_dev_check,
						    am65_cpsw_port_obj_add);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_OBJ_DEL:
		err = switchdev_handle_port_obj_del(dev, ptr,
						    am65_cpsw_port_dev_check,
						    am65_cpsw_port_obj_del);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     am65_cpsw_port_dev_check,
						     am65_cpsw_port_attr_set);
		return notifier_from_errno(err);
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block cpsw_switchdev_bl_notifier = {
	.notifier_call = am65_cpsw_switchdev_blocking_event,
};

int am65_cpsw_switchdev_register_notifiers(struct am65_cpsw_common *cpsw)
{
	int ret = 0;

	ret = register_switchdev_notifier(&cpsw_switchdev_notifier);
	if (ret) {
		dev_err(cpsw->dev, "register switchdev notifier fail ret:%d\n",
			ret);
		return ret;
	}

	ret = register_switchdev_blocking_notifier(&cpsw_switchdev_bl_notifier);
	if (ret) {
		dev_err(cpsw->dev, "register switchdev blocking notifier ret:%d\n",
			ret);
		unregister_switchdev_notifier(&cpsw_switchdev_notifier);
	}

	return ret;
}

void am65_cpsw_switchdev_unregister_notifiers(struct am65_cpsw_common *cpsw)
{
	unregister_switchdev_blocking_notifier(&cpsw_switchdev_bl_notifier);
	unregister_switchdev_notifier(&cpsw_switchdev_notifier);
}
