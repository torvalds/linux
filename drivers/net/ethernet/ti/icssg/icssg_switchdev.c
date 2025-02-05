// SPDX-License-Identifier: GPL-2.0

/* Texas Instruments K3 ICSSG Ethernet Switchdev Driver
 *
 * Copyright (C) 2021 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <net/switchdev.h>

#include "icssg_prueth.h"
#include "icssg_switchdev.h"
#include "icssg_mii_rt.h"

struct prueth_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct prueth_emac *emac;
	unsigned long event;
};

static int prueth_switchdev_stp_state_set(struct prueth_emac *emac,
					  u8 state)
{
	enum icssg_port_state_cmd emac_state;
	int ret = 0;

	switch (state) {
	case BR_STATE_FORWARDING:
		emac_state = ICSSG_EMAC_PORT_FORWARD;
		break;
	case BR_STATE_DISABLED:
		emac_state = ICSSG_EMAC_PORT_DISABLE;
		break;
	case BR_STATE_LISTENING:
	case BR_STATE_BLOCKING:
		emac_state = ICSSG_EMAC_PORT_BLOCK;
		break;
	default:
		return -EOPNOTSUPP;
	}

	icssg_set_port_state(emac, emac_state);
	netdev_dbg(emac->ndev, "STP state: %u\n", emac_state);

	return ret;
}

static int prueth_switchdev_attr_br_flags_set(struct prueth_emac *emac,
					      struct net_device *orig_dev,
					      struct switchdev_brport_flags brport_flags)
{
	enum icssg_port_state_cmd emac_state;

	if (brport_flags.mask & BR_MCAST_FLOOD)
		emac_state = ICSSG_EMAC_PORT_MC_FLOODING_ENABLE;
	else
		emac_state = ICSSG_EMAC_PORT_MC_FLOODING_DISABLE;

	netdev_dbg(emac->ndev, "BR_MCAST_FLOOD: %d port %u\n",
		   emac_state, emac->port_id);

	icssg_set_port_state(emac, emac_state);

	return 0;
}

static int prueth_switchdev_attr_br_flags_pre_set(struct net_device *netdev,
						  struct switchdev_brport_flags brport_flags)
{
	if (brport_flags.mask & ~(BR_LEARNING | BR_MCAST_FLOOD))
		return -EINVAL;

	return 0;
}

static int prueth_switchdev_attr_set(struct net_device *ndev, const void *ctx,
				     const struct switchdev_attr *attr,
				     struct netlink_ext_ack *extack)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	int ret;

	netdev_dbg(ndev, "attr: id %u port: %u\n", attr->id, emac->port_id);

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		ret = prueth_switchdev_attr_br_flags_pre_set(ndev,
							     attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		ret = prueth_switchdev_stp_state_set(emac,
						     attr->u.stp_state);
		netdev_dbg(ndev, "stp state: %u\n", attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		ret = prueth_switchdev_attr_br_flags_set(emac, attr->orig_dev,
							 attr->u.brport_flags);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static void prueth_switchdev_fdb_offload_notify(struct net_device *ndev,
						struct switchdev_notifier_fdb_info *rcv)
{
	struct switchdev_notifier_fdb_info info;

	memset(&info, 0, sizeof(info));
	info.addr = rcv->addr;
	info.vid = rcv->vid;
	info.offloaded = true;
	call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED,
				 ndev, &info.info, NULL);
}

static void prueth_switchdev_event_work(struct work_struct *work)
{
	struct prueth_switchdev_event_work *switchdev_work =
		container_of(work, struct prueth_switchdev_event_work, work);
	struct prueth_emac *emac = switchdev_work->emac;
	struct switchdev_notifier_fdb_info *fdb;
	int port_id = emac->port_id;
	int ret;

	rtnl_lock();
	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fdb = &switchdev_work->fdb_info;

		netdev_dbg(emac->ndev, "prueth_fdb_add: MACID = %pM vid = %u flags = %u %u -- port %d\n",
			   fdb->addr, fdb->vid, fdb->added_by_user,
			   fdb->offloaded, port_id);

		if (!fdb->added_by_user)
			break;
		if (!ether_addr_equal(emac->mac_addr, fdb->addr))
			break;

		ret = icssg_fdb_add_del(emac, fdb->addr, fdb->vid,
					BIT(port_id), true);
		if (!ret)
			prueth_switchdev_fdb_offload_notify(emac->ndev, fdb);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb = &switchdev_work->fdb_info;

		netdev_dbg(emac->ndev, "prueth_fdb_del: MACID = %pM vid = %u flags = %u %u -- port %d\n",
			   fdb->addr, fdb->vid, fdb->added_by_user,
			   fdb->offloaded, port_id);

		if (!fdb->added_by_user)
			break;
		if (!ether_addr_equal(emac->mac_addr, fdb->addr))
			break;
		icssg_fdb_add_del(emac, fdb->addr, fdb->vid,
				  BIT(port_id), false);
		break;
	default:
		break;
	}
	rtnl_unlock();

	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(emac->ndev);
}

static int prueth_switchdev_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct net_device *ndev = switchdev_notifier_info_to_dev(ptr);
	struct prueth_switchdev_event_work *switchdev_work;
	struct switchdev_notifier_fdb_info *fdb_info = ptr;
	struct prueth_emac *emac = netdev_priv(ndev);
	int err;

	if (!prueth_dev_check(ndev))
		return NOTIFY_DONE;

	if (event == SWITCHDEV_PORT_ATTR_SET) {
		err = switchdev_handle_port_attr_set(ndev, ptr,
						     prueth_dev_check,
						     prueth_switchdev_attr_set);
		return notifier_from_errno(err);
	}

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (WARN_ON(!switchdev_work))
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work, prueth_switchdev_event_work);
	switchdev_work->emac = emac;
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

static int prueth_switchdev_vlan_add(struct prueth_emac *emac, bool untag, bool pvid,
				     u8 vid, struct net_device *orig_dev)
{
	bool cpu_port = netif_is_bridge_master(orig_dev);
	int untag_mask = 0;
	int port_mask;
	int ret = 0;

	if (cpu_port)
		port_mask = BIT(PRUETH_PORT_HOST);
	else
		port_mask = BIT(emac->port_id);

	if (untag)
		untag_mask = port_mask;

	icssg_vtbl_modify(emac, vid, port_mask, untag_mask, true);

	netdev_dbg(emac->ndev, "VID add vid:%u port_mask:%X untag_mask %X PVID %d\n",
		   vid, port_mask, untag_mask, pvid);

	if (!pvid)
		return ret;

	icssg_set_pvid(emac->prueth, vid, emac->port_id);

	return ret;
}

static int prueth_switchdev_vlan_del(struct prueth_emac *emac, u16 vid,
				     struct net_device *orig_dev)
{
	bool cpu_port = netif_is_bridge_master(orig_dev);
	int port_mask;
	int ret = 0;

	if (cpu_port)
		port_mask = BIT(PRUETH_PORT_HOST);
	else
		port_mask = BIT(emac->port_id);

	icssg_vtbl_modify(emac, vid, port_mask, 0, false);

	if (cpu_port)
		icssg_fdb_add_del(emac, emac->mac_addr, vid,
				  BIT(PRUETH_PORT_HOST), false);

	if (vid == icssg_get_pvid(emac))
		icssg_set_pvid(emac->prueth, 0, emac->port_id);

	netdev_dbg(emac->ndev, "VID del vid:%u port_mask:%X\n",
		   vid, port_mask);

	return ret;
}

static int prueth_switchdev_vlans_add(struct prueth_emac *emac,
				      const struct switchdev_obj_port_vlan *vlan)
{
	bool untag = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct net_device *orig_dev = vlan->obj.orig_dev;
	bool cpu_port = netif_is_bridge_master(orig_dev);
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;

	netdev_dbg(emac->ndev, "VID add vid:%u flags:%X\n",
		   vlan->vid, vlan->flags);

	if (cpu_port && !(vlan->flags & BRIDGE_VLAN_INFO_BRENTRY))
		return 0;

	if (vlan->vid > 0xff)
		return 0;

	return prueth_switchdev_vlan_add(emac, untag, pvid, vlan->vid,
					 orig_dev);
}

static int prueth_switchdev_vlans_del(struct prueth_emac *emac,
				      const struct switchdev_obj_port_vlan *vlan)
{
	if (vlan->vid > 0xff)
		return 0;

	return prueth_switchdev_vlan_del(emac, vlan->vid,
					 vlan->obj.orig_dev);
}

static int prueth_switchdev_mdb_add(struct prueth_emac *emac,
				    struct switchdev_obj_port_mdb *mdb)
{
	struct net_device *orig_dev = mdb->obj.orig_dev;
	u8 port_mask, fid_c2;
	bool cpu_port;
	int err;

	cpu_port = netif_is_bridge_master(orig_dev);

	if (cpu_port)
		port_mask = BIT(PRUETH_PORT_HOST);
	else
		port_mask = BIT(emac->port_id);

	fid_c2 = icssg_fdb_lookup(emac, mdb->addr, mdb->vid);

	err = icssg_fdb_add_del(emac, mdb->addr, mdb->vid, fid_c2 | port_mask, true);
	netdev_dbg(emac->ndev, "MDB add vid %u:%pM  ports: %X\n",
		   mdb->vid, mdb->addr, port_mask);

	return err;
}

static int prueth_switchdev_mdb_del(struct prueth_emac *emac,
				    struct switchdev_obj_port_mdb *mdb)
{
	struct net_device *orig_dev = mdb->obj.orig_dev;
	int del_mask, ret, fid_c2;
	bool cpu_port;

	cpu_port = netif_is_bridge_master(orig_dev);

	if (cpu_port)
		del_mask = BIT(PRUETH_PORT_HOST);
	else
		del_mask = BIT(emac->port_id);

	fid_c2 = icssg_fdb_lookup(emac, mdb->addr, mdb->vid);

	if (fid_c2 & ~del_mask)
		ret = icssg_fdb_add_del(emac, mdb->addr, mdb->vid, fid_c2 & ~del_mask, true);
	else
		ret = icssg_fdb_add_del(emac, mdb->addr, mdb->vid, 0, false);

	netdev_dbg(emac->ndev, "MDB del vid %u:%pM  ports: %X\n",
		   mdb->vid, mdb->addr, del_mask);

	return ret;
}

static int prueth_switchdev_obj_add(struct net_device *ndev, const void *ctx,
				    const struct switchdev_obj *obj,
				    struct netlink_ext_ack *extack)
{
	struct switchdev_obj_port_vlan *vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
	struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	struct prueth_emac *emac = netdev_priv(ndev);
	int err = 0;

	netdev_dbg(ndev, "obj_add: id %u port: %u\n", obj->id, emac->port_id);

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = prueth_switchdev_vlans_add(emac, vlan);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		err = prueth_switchdev_mdb_add(emac, mdb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int prueth_switchdev_obj_del(struct net_device *ndev, const void *ctx,
				    const struct switchdev_obj *obj)
{
	struct switchdev_obj_port_vlan *vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
	struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	struct prueth_emac *emac = netdev_priv(ndev);
	int err = 0;

	netdev_dbg(ndev, "obj_del: id %u port: %u\n", obj->id, emac->port_id);

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = prueth_switchdev_vlans_del(emac, vlan);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		err = prueth_switchdev_mdb_del(emac, mdb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int prueth_switchdev_blocking_event(struct notifier_block *unused,
					   unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = switchdev_handle_port_obj_add(dev, ptr,
						    prueth_dev_check,
						    prueth_switchdev_obj_add);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_OBJ_DEL:
		err = switchdev_handle_port_obj_del(dev, ptr,
						    prueth_dev_check,
						    prueth_switchdev_obj_del);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     prueth_dev_check,
						     prueth_switchdev_attr_set);
		return notifier_from_errno(err);
	default:
		break;
	}

	return NOTIFY_DONE;
}

int prueth_switchdev_register_notifiers(struct prueth *prueth)
{
	int ret = 0;

	prueth->prueth_switchdev_nb.notifier_call = &prueth_switchdev_event;
	ret = register_switchdev_notifier(&prueth->prueth_switchdev_nb);
	if (ret) {
		dev_err(prueth->dev, "register switchdev notifier fail ret:%d\n",
			ret);
		return ret;
	}

	prueth->prueth_switchdev_bl_nb.notifier_call = &prueth_switchdev_blocking_event;
	ret = register_switchdev_blocking_notifier(&prueth->prueth_switchdev_bl_nb);
	if (ret) {
		dev_err(prueth->dev, "register switchdev blocking notifier ret:%d\n",
			ret);
		unregister_switchdev_notifier(&prueth->prueth_switchdev_nb);
	}

	return ret;
}

void prueth_switchdev_unregister_notifiers(struct prueth *prueth)
{
	unregister_switchdev_blocking_notifier(&prueth->prueth_switchdev_bl_nb);
	unregister_switchdev_notifier(&prueth->prueth_switchdev_nb);
}
