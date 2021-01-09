// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments switchdev Driver
 *
 * Copyright (C) 2019 Texas Instruments
 *
 */

#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <net/switchdev.h>

#include "cpsw.h"
#include "cpsw_ale.h"
#include "cpsw_priv.h"
#include "cpsw_switchdev.h"

struct cpsw_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct cpsw_priv *priv;
	unsigned long event;
};

static int cpsw_port_stp_state_set(struct cpsw_priv *priv,
				   struct switchdev_trans *trans, u8 state)
{
	struct cpsw_common *cpsw = priv->cpsw;
	u8 cpsw_state;
	int ret = 0;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

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

	ret = cpsw_ale_control_set(cpsw->ale, priv->emac_port,
				   ALE_PORT_STATE, cpsw_state);
	dev_dbg(priv->dev, "ale state: %u\n", cpsw_state);

	return ret;
}

static int cpsw_port_attr_br_flags_set(struct cpsw_priv *priv,
				       struct switchdev_trans *trans,
				       struct net_device *orig_dev,
				       unsigned long brport_flags)
{
	struct cpsw_common *cpsw = priv->cpsw;
	bool unreg_mcast_add = false;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	if (brport_flags & BR_MCAST_FLOOD)
		unreg_mcast_add = true;
	dev_dbg(priv->dev, "BR_MCAST_FLOOD: %d port %u\n",
		unreg_mcast_add, priv->emac_port);

	cpsw_ale_set_unreg_mcast(cpsw->ale, BIT(priv->emac_port),
				 unreg_mcast_add);

	return 0;
}

static int cpsw_port_attr_br_flags_pre_set(struct net_device *netdev,
					   struct switchdev_trans *trans,
					   unsigned long flags)
{
	if (flags & ~(BR_LEARNING | BR_MCAST_FLOOD))
		return -EINVAL;

	return 0;
}

static int cpsw_port_attr_set(struct net_device *ndev,
			      const struct switchdev_attr *attr,
			      struct switchdev_trans *trans)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	int ret;

	dev_dbg(priv->dev, "attr: id %u port: %u\n", attr->id, priv->emac_port);

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		ret = cpsw_port_attr_br_flags_pre_set(ndev, trans,
						      attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		ret = cpsw_port_stp_state_set(priv, trans, attr->u.stp_state);
		dev_dbg(priv->dev, "stp state: %u\n", attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		ret = cpsw_port_attr_br_flags_set(priv, trans, attr->orig_dev,
						  attr->u.brport_flags);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static u16 cpsw_get_pvid(struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;
	u32 __iomem *port_vlan_reg;
	u32 pvid;

	if (priv->emac_port) {
		int reg = CPSW2_PORT_VLAN;

		if (cpsw->version == CPSW_VERSION_1)
			reg = CPSW1_PORT_VLAN;
		pvid = slave_read(cpsw->slaves + (priv->emac_port - 1), reg);
	} else {
		port_vlan_reg = &cpsw->host_port_regs->port_vlan;
		pvid = readl(port_vlan_reg);
	}

	pvid = pvid & 0xfff;

	return pvid;
}

static void cpsw_set_pvid(struct cpsw_priv *priv, u16 vid, bool cfi, u32 cos)
{
	struct cpsw_common *cpsw = priv->cpsw;
	void __iomem *port_vlan_reg;
	u32 pvid;

	pvid = vid;
	pvid |= cfi ? BIT(12) : 0;
	pvid |= (cos & 0x7) << 13;

	if (priv->emac_port) {
		int reg = CPSW2_PORT_VLAN;

		if (cpsw->version == CPSW_VERSION_1)
			reg = CPSW1_PORT_VLAN;
		/* no barrier */
		slave_write(cpsw->slaves + (priv->emac_port - 1), pvid, reg);
	} else {
		/* CPU port */
		port_vlan_reg = &cpsw->host_port_regs->port_vlan;
		writel(pvid, port_vlan_reg);
	}
}

static int cpsw_port_vlan_add(struct cpsw_priv *priv, bool untag, bool pvid,
			      u16 vid, struct net_device *orig_dev)
{
	bool cpu_port = netif_is_bridge_master(orig_dev);
	struct cpsw_common *cpsw = priv->cpsw;
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
		port_mask = BIT(priv->emac_port);
		flags = priv->ndev->flags;
	}

	if (flags & IFF_MULTICAST)
		reg_mcast_mask = port_mask;

	if (untag)
		untag_mask = port_mask;

	ret = cpsw_ale_vlan_add_modify(cpsw->ale, vid, port_mask, untag_mask,
				       reg_mcast_mask, unreg_mcast_mask);
	if (ret) {
		dev_err(priv->dev, "Unable to add vlan\n");
		return ret;
	}

	if (cpu_port)
		cpsw_ale_add_ucast(cpsw->ale, priv->mac_addr,
				   HOST_PORT_NUM, ALE_VLAN, vid);
	if (!pvid)
		return ret;

	cpsw_set_pvid(priv, vid, 0, 0);

	dev_dbg(priv->dev, "VID add: %s: vid:%u ports:%X\n",
		priv->ndev->name, vid, port_mask);
	return ret;
}

static int cpsw_port_vlan_del(struct cpsw_priv *priv, u16 vid,
			      struct net_device *orig_dev)
{
	bool cpu_port = netif_is_bridge_master(orig_dev);
	struct cpsw_common *cpsw = priv->cpsw;
	int port_mask;
	int ret = 0;

	if (cpu_port)
		port_mask = BIT(HOST_PORT_NUM);
	else
		port_mask = BIT(priv->emac_port);

	ret = cpsw_ale_vlan_del_modify(cpsw->ale, vid, port_mask);
	if (ret != 0)
		return ret;

	/* We don't care for the return value here, error is returned only if
	 * the unicast entry is not present
	 */
	if (cpu_port)
		cpsw_ale_del_ucast(cpsw->ale, priv->mac_addr,
				   HOST_PORT_NUM, ALE_VLAN, vid);

	if (vid == cpsw_get_pvid(priv))
		cpsw_set_pvid(priv, 0, 0, 0);

	/* We don't care for the return value here, error is returned only if
	 * the multicast entry is not present
	 */
	cpsw_ale_del_mcast(cpsw->ale, priv->ndev->broadcast,
			   port_mask, ALE_VLAN, vid);
	dev_dbg(priv->dev, "VID del: %s: vid:%u ports:%X\n",
		priv->ndev->name, vid, port_mask);

	return ret;
}

static int cpsw_port_vlans_add(struct cpsw_priv *priv,
			       const struct switchdev_obj_port_vlan *vlan)
{
	bool untag = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct net_device *orig_dev = vlan->obj.orig_dev;
	bool cpu_port = netif_is_bridge_master(orig_dev);
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;

	dev_dbg(priv->dev, "VID add: %s: vid:%u flags:%X\n",
		priv->ndev->name, vlan->vid, vlan->flags);

	if (cpu_port && !(vlan->flags & BRIDGE_VLAN_INFO_BRENTRY))
		return 0;

	return cpsw_port_vlan_add(priv, untag, pvid, vlan->vid, orig_dev);
}

static int cpsw_port_mdb_add(struct cpsw_priv *priv,
			     struct switchdev_obj_port_mdb *mdb)

{
	struct net_device *orig_dev = mdb->obj.orig_dev;
	bool cpu_port = netif_is_bridge_master(orig_dev);
	struct cpsw_common *cpsw = priv->cpsw;
	int port_mask;
	int err;

	if (cpu_port)
		port_mask = BIT(HOST_PORT_NUM);
	else
		port_mask = BIT(priv->emac_port);

	err = cpsw_ale_add_mcast(cpsw->ale, mdb->addr, port_mask,
				 ALE_VLAN, mdb->vid, 0);
	dev_dbg(priv->dev, "MDB add: %s: vid %u:%pM  ports: %X\n",
		priv->ndev->name, mdb->vid, mdb->addr, port_mask);

	return err;
}

static int cpsw_port_mdb_del(struct cpsw_priv *priv,
			     struct switchdev_obj_port_mdb *mdb)

{
	struct net_device *orig_dev = mdb->obj.orig_dev;
	bool cpu_port = netif_is_bridge_master(orig_dev);
	struct cpsw_common *cpsw = priv->cpsw;
	int del_mask;
	int err;

	if (cpu_port)
		del_mask = BIT(HOST_PORT_NUM);
	else
		del_mask = BIT(priv->emac_port);

	err = cpsw_ale_del_mcast(cpsw->ale, mdb->addr, del_mask,
				 ALE_VLAN, mdb->vid);
	dev_dbg(priv->dev, "MDB del: %s: vid %u:%pM  ports: %X\n",
		priv->ndev->name, mdb->vid, mdb->addr, del_mask);

	return err;
}

static int cpsw_port_obj_add(struct net_device *ndev,
			     const struct switchdev_obj *obj,
			     struct netlink_ext_ack *extack)
{
	struct switchdev_obj_port_vlan *vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
	struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	struct cpsw_priv *priv = netdev_priv(ndev);
	int err = 0;

	dev_dbg(priv->dev, "obj_add: id %u port: %u\n",
		obj->id, priv->emac_port);

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = cpsw_port_vlans_add(priv, vlan);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		err = cpsw_port_mdb_add(priv, mdb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int cpsw_port_obj_del(struct net_device *ndev,
			     const struct switchdev_obj *obj)
{
	struct switchdev_obj_port_vlan *vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
	struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	struct cpsw_priv *priv = netdev_priv(ndev);
	int err = 0;

	dev_dbg(priv->dev, "obj_del: id %u port: %u\n",
		obj->id, priv->emac_port);

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = cpsw_port_vlan_del(priv, vlan->vid, vlan->obj.orig_dev);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		err = cpsw_port_mdb_del(priv, mdb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static void cpsw_fdb_offload_notify(struct net_device *ndev,
				    struct switchdev_notifier_fdb_info *rcv)
{
	struct switchdev_notifier_fdb_info info;

	info.addr = rcv->addr;
	info.vid = rcv->vid;
	info.offloaded = true;
	call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED,
				 ndev, &info.info, NULL);
}

static void cpsw_switchdev_event_work(struct work_struct *work)
{
	struct cpsw_switchdev_event_work *switchdev_work =
		container_of(work, struct cpsw_switchdev_event_work, work);
	struct cpsw_priv *priv = switchdev_work->priv;
	struct switchdev_notifier_fdb_info *fdb;
	struct cpsw_common *cpsw = priv->cpsw;
	int port = priv->emac_port;

	rtnl_lock();
	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fdb = &switchdev_work->fdb_info;

		dev_dbg(cpsw->dev, "cpsw_fdb_add: MACID = %pM vid = %u flags = %u %u -- port %d\n",
			fdb->addr, fdb->vid, fdb->added_by_user,
			fdb->offloaded, port);

		if (!fdb->added_by_user)
			break;
		if (memcmp(priv->mac_addr, (u8 *)fdb->addr, ETH_ALEN) == 0)
			port = HOST_PORT_NUM;

		cpsw_ale_add_ucast(cpsw->ale, (u8 *)fdb->addr, port,
				   fdb->vid ? ALE_VLAN : 0, fdb->vid);
		cpsw_fdb_offload_notify(priv->ndev, fdb);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb = &switchdev_work->fdb_info;

		dev_dbg(cpsw->dev, "cpsw_fdb_del: MACID = %pM vid = %u flags = %u %u -- port %d\n",
			fdb->addr, fdb->vid, fdb->added_by_user,
			fdb->offloaded, port);

		if (!fdb->added_by_user)
			break;
		if (memcmp(priv->mac_addr, (u8 *)fdb->addr, ETH_ALEN) == 0)
			port = HOST_PORT_NUM;

		cpsw_ale_del_ucast(cpsw->ale, (u8 *)fdb->addr, port,
				   fdb->vid ? ALE_VLAN : 0, fdb->vid);
		break;
	default:
		break;
	}
	rtnl_unlock();

	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(priv->ndev);
}

/* called under rcu_read_lock() */
static int cpsw_switchdev_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *ndev = switchdev_notifier_info_to_dev(ptr);
	struct switchdev_notifier_fdb_info *fdb_info = ptr;
	struct cpsw_switchdev_event_work *switchdev_work;
	struct cpsw_priv *priv = netdev_priv(ndev);
	int err;

	if (event == SWITCHDEV_PORT_ATTR_SET) {
		err = switchdev_handle_port_attr_set(ndev, ptr,
						     cpsw_port_dev_check,
						     cpsw_port_attr_set);
		return notifier_from_errno(err);
	}

	if (!cpsw_port_dev_check(ndev))
		return NOTIFY_DONE;

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (WARN_ON(!switchdev_work))
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work, cpsw_switchdev_event_work);
	switchdev_work->priv = priv;
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
	.notifier_call = cpsw_switchdev_event,
};

static int cpsw_switchdev_blocking_event(struct notifier_block *unused,
					 unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = switchdev_handle_port_obj_add(dev, ptr,
						    cpsw_port_dev_check,
						    cpsw_port_obj_add);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_OBJ_DEL:
		err = switchdev_handle_port_obj_del(dev, ptr,
						    cpsw_port_dev_check,
						    cpsw_port_obj_del);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     cpsw_port_dev_check,
						     cpsw_port_attr_set);
		return notifier_from_errno(err);
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block cpsw_switchdev_bl_notifier = {
	.notifier_call = cpsw_switchdev_blocking_event,
};

int cpsw_switchdev_register_notifiers(struct cpsw_common *cpsw)
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

void cpsw_switchdev_unregister_notifiers(struct cpsw_common *cpsw)
{
	unregister_switchdev_blocking_notifier(&cpsw_switchdev_bl_notifier);
	unregister_switchdev_notifier(&cpsw_switchdev_notifier);
}
