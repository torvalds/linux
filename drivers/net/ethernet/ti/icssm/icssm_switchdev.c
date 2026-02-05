// SPDX-License-Identifier: GPL-2.0

/* Texas Instruments ICSSM Ethernet Driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/remoteproc.h>
#include <net/switchdev.h>

#include "icssm_prueth.h"
#include "icssm_prueth_switch.h"
#include "icssm_prueth_fdb_tbl.h"

/* switchev event work */
struct icssm_sw_event_work {
	netdevice_tracker ndev_tracker;
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct prueth_emac *emac;
	unsigned long event;
};

void icssm_prueth_sw_set_stp_state(struct prueth *prueth,
				   enum prueth_port port, u8 state)
{
	struct fdb_tbl *t = prueth->fdb_tbl;

	writeb(state, port - 1 ? (void __iomem *)&t->port2_stp_cfg->state :
			(void __iomem *)&t->port1_stp_cfg->state);
}

u8 icssm_prueth_sw_get_stp_state(struct prueth *prueth, enum prueth_port port)
{
	struct fdb_tbl *t = prueth->fdb_tbl;
	u8 state;

	state = readb(port - 1 ? (void __iomem *)&t->port2_stp_cfg->state :
			(void __iomem *)&t->port1_stp_cfg->state);
	return state;
}

static int icssm_prueth_sw_attr_set(struct net_device *ndev, const void *ctx,
				    const struct switchdev_attr *attr,
				    struct netlink_ext_ack *extack)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	struct prueth *prueth = emac->prueth;
	int err = 0;
	u8 o_state;

	/* Interface is not up */
	if (!prueth->fdb_tbl)
		return 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		o_state = icssm_prueth_sw_get_stp_state(prueth, emac->port_id);
		icssm_prueth_sw_set_stp_state(prueth, emac->port_id,
					      attr->u.stp_state);

		if (o_state != attr->u.stp_state)
			icssm_prueth_sw_purge_fdb(emac);

		dev_dbg(prueth->dev, "attr set: stp state:%u port:%u\n",
			attr->u.stp_state, emac->port_id);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static void icssm_prueth_sw_fdb_offload(struct net_device *ndev,
					struct switchdev_notifier_fdb_info *rcv)
{
	struct switchdev_notifier_fdb_info info;

	info.addr = rcv->addr;
	info.vid = rcv->vid;
	call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED, ndev, &info.info,
				 NULL);
}

/**
 * icssm_sw_event_work - insert/delete fdb entry
 *
 * @work: work structure
 *
 */
static void icssm_sw_event_work(struct work_struct *work)
{
	struct icssm_sw_event_work *switchdev_work =
		container_of(work, struct icssm_sw_event_work, work);
	struct prueth_emac *emac = switchdev_work->emac;
	struct switchdev_notifier_fdb_info *fdb;
	struct prueth *prueth = emac->prueth;
	int port = emac->port_id;

	rtnl_lock();

	/* Interface is not up */
	if (!emac->prueth->fdb_tbl)
		goto free;

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fdb = &switchdev_work->fdb_info;
		dev_dbg(prueth->dev,
			"prueth fdb add: MACID = %pM vid = %u flags = %u -- port %d\n",
			fdb->addr, fdb->vid, fdb->added_by_user, port);

		if (!fdb->added_by_user)
			break;

		if (fdb->is_local)
			break;

		icssm_prueth_sw_fdb_add(emac, fdb);
		icssm_prueth_sw_fdb_offload(emac->ndev, fdb);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb = &switchdev_work->fdb_info;
		dev_dbg(prueth->dev,
			"prueth fdb del: MACID = %pM vid = %u flags = %u -- port %d\n",
			fdb->addr, fdb->vid, fdb->added_by_user, port);

		if (fdb->is_local)
			break;

		icssm_prueth_sw_fdb_del(emac, fdb);
		break;
	default:
		break;
	}

free:
	rtnl_unlock();

	netdev_put(emac->ndev, &switchdev_work->ndev_tracker);
	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
}

/* called under rcu_read_lock() */
static int icssm_prueth_sw_switchdev_event(struct notifier_block *unused,
					   unsigned long event, void *ptr)
{
	struct net_device *ndev = switchdev_notifier_info_to_dev(ptr);
	struct switchdev_notifier_fdb_info *fdb_info = ptr;
	struct prueth_emac *emac = netdev_priv(ndev);
	struct icssm_sw_event_work *switchdev_work;
	int err;

	if (!icssm_prueth_sw_port_dev_check(ndev))
		return NOTIFY_DONE;

	if (event == SWITCHDEV_PORT_ATTR_SET) {
		err = switchdev_handle_port_attr_set
			(ndev, ptr, icssm_prueth_sw_port_dev_check,
			 icssm_prueth_sw_attr_set);
		return notifier_from_errno(err);
	}

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (WARN_ON(!switchdev_work))
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work, icssm_sw_event_work);
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
		netdev_hold(ndev, &switchdev_work->ndev_tracker, GFP_ATOMIC);
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

static int icssm_prueth_switchdev_obj_add(struct net_device *ndev,
					  const void *ctx,
					  const struct switchdev_obj *obj,
					  struct netlink_ext_ack *extack)
{
	struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	struct prueth_emac *emac = netdev_priv(ndev);
	struct prueth *prueth = emac->prueth;
	int ret = 0;
	u8 hash;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		dev_dbg(prueth->dev, "MDB add: %s: vid %u:%pM  port: %x\n",
			ndev->name, mdb->vid, mdb->addr, emac->port_id);
		hash = icssm_emac_get_mc_hash(mdb->addr, emac->mc_filter_mask);
		icssm_emac_mc_filter_bin_allow(emac, hash);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int icssm_prueth_switchdev_obj_del(struct net_device *ndev,
					  const void *ctx,
					  const struct switchdev_obj *obj)
{
	struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	struct prueth_emac *emac = netdev_priv(ndev);
	struct prueth *prueth = emac->prueth;
	struct netdev_hw_addr *ha;
	u8 hash, tmp_hash;
	int ret = 0;
	u8 *mask;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		dev_dbg(prueth->dev, "MDB del: %s: vid %u:%pM  port: %x\n",
			ndev->name, mdb->vid, mdb->addr, emac->port_id);
		if (prueth->hw_bridge_dev) {
			mask = emac->mc_filter_mask;
			hash = icssm_emac_get_mc_hash(mdb->addr, mask);
			netdev_for_each_mc_addr(ha, prueth->hw_bridge_dev) {
				tmp_hash = icssm_emac_get_mc_hash(ha->addr,
								  mask);
				/* Another MC address is in the bin.
				 * Don't disable.
				 */
				if (tmp_hash == hash)
					return 0;
			}
			icssm_emac_mc_filter_bin_disallow(emac, hash);
		}
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

/* switchdev notifiers */
static int icssm_prueth_sw_blocking_event(struct notifier_block *unused,
					  unsigned long event, void *ptr)
{
	struct net_device *ndev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = switchdev_handle_port_obj_add
			(ndev, ptr, icssm_prueth_sw_port_dev_check,
			 icssm_prueth_switchdev_obj_add);
		return notifier_from_errno(err);

	case SWITCHDEV_PORT_OBJ_DEL:
		err = switchdev_handle_port_obj_del
			(ndev, ptr, icssm_prueth_sw_port_dev_check,
			 icssm_prueth_switchdev_obj_del);
		return notifier_from_errno(err);

	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set
			(ndev, ptr, icssm_prueth_sw_port_dev_check,
			 icssm_prueth_sw_attr_set);
		return notifier_from_errno(err);

	default:
		break;
	}

	return NOTIFY_DONE;
}

int icssm_prueth_sw_register_notifiers(struct prueth *prueth)
{
	int ret = 0;

	prueth->prueth_switchdev_nb.notifier_call =
		&icssm_prueth_sw_switchdev_event;
	ret = register_switchdev_notifier(&prueth->prueth_switchdev_nb);
	if (ret) {
		dev_err(prueth->dev,
			"register switchdev notifier failed ret:%d\n", ret);
		return ret;
	}

	prueth->prueth_switchdev_bl_nb.notifier_call =
		&icssm_prueth_sw_blocking_event;
	ret = register_switchdev_blocking_notifier
		(&prueth->prueth_switchdev_bl_nb);
	if (ret) {
		dev_err(prueth->dev,
			"register switchdev blocking notifier failed ret:%d\n",
			ret);
		unregister_switchdev_notifier(&prueth->prueth_switchdev_nb);
	}

	return ret;
}

void icssm_prueth_sw_unregister_notifiers(struct prueth *prueth)
{
	unregister_switchdev_blocking_notifier(&prueth->prueth_switchdev_bl_nb);
	unregister_switchdev_notifier(&prueth->prueth_switchdev_nb);
}
