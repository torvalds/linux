// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023, Intel Corporation. */

#include "ice.h"
#include "ice_eswitch_br.h"
#include "ice_repr.h"
#include "ice_switch.h"

static const struct rhashtable_params ice_fdb_ht_params = {
	.key_offset = offsetof(struct ice_esw_br_fdb_entry, data),
	.key_len = sizeof(struct ice_esw_br_fdb_data),
	.head_offset = offsetof(struct ice_esw_br_fdb_entry, ht_node),
	.automatic_shrinking = true,
};

static bool ice_eswitch_br_is_dev_valid(const struct net_device *dev)
{
	/* Accept only PF netdev and PRs */
	return ice_is_port_repr_netdev(dev) || netif_is_ice(dev);
}

static struct ice_esw_br_port *
ice_eswitch_br_netdev_to_port(struct net_device *dev)
{
	if (ice_is_port_repr_netdev(dev)) {
		struct ice_repr *repr = ice_netdev_to_repr(dev);

		return repr->br_port;
	} else if (netif_is_ice(dev)) {
		struct ice_pf *pf = ice_netdev_to_pf(dev);

		return pf->br_port;
	}

	return NULL;
}

static void
ice_eswitch_br_ingress_rule_setup(struct ice_adv_rule_info *rule_info,
				  u8 pf_id, u16 vf_vsi_idx)
{
	rule_info->sw_act.vsi_handle = vf_vsi_idx;
	rule_info->sw_act.flag |= ICE_FLTR_RX;
	rule_info->sw_act.src = pf_id;
	rule_info->priority = 5;
}

static void
ice_eswitch_br_egress_rule_setup(struct ice_adv_rule_info *rule_info,
				 u16 pf_vsi_idx)
{
	rule_info->sw_act.vsi_handle = pf_vsi_idx;
	rule_info->sw_act.flag |= ICE_FLTR_TX;
	rule_info->flags_info.act = ICE_SINGLE_ACT_LAN_ENABLE;
	rule_info->flags_info.act_valid = true;
	rule_info->priority = 5;
}

static int
ice_eswitch_br_rule_delete(struct ice_hw *hw, struct ice_rule_query_data *rule)
{
	int err;

	if (!rule)
		return -EINVAL;

	err = ice_rem_adv_rule_by_id(hw, rule);
	kfree(rule);

	return err;
}

static struct ice_rule_query_data *
ice_eswitch_br_fwd_rule_create(struct ice_hw *hw, int vsi_idx, int port_type,
			       const unsigned char *mac)
{
	struct ice_adv_rule_info rule_info = { 0 };
	struct ice_rule_query_data *rule;
	struct ice_adv_lkup_elem *list;
	u16 lkups_cnt = 1;
	int err;

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return ERR_PTR(-ENOMEM);

	list = kcalloc(lkups_cnt, sizeof(*list), GFP_ATOMIC);
	if (!list) {
		err = -ENOMEM;
		goto err_list_alloc;
	}

	switch (port_type) {
	case ICE_ESWITCH_BR_UPLINK_PORT:
		ice_eswitch_br_egress_rule_setup(&rule_info, vsi_idx);
		break;
	case ICE_ESWITCH_BR_VF_REPR_PORT:
		ice_eswitch_br_ingress_rule_setup(&rule_info, hw->pf_id,
						  vsi_idx);
		break;
	default:
		err = -EINVAL;
		goto err_add_rule;
	}

	list[0].type = ICE_MAC_OFOS;
	ether_addr_copy(list[0].h_u.eth_hdr.dst_addr, mac);
	eth_broadcast_addr(list[0].m_u.eth_hdr.dst_addr);

	rule_info.sw_act.fltr_act = ICE_FWD_TO_VSI;

	err = ice_add_adv_rule(hw, list, lkups_cnt, &rule_info, rule);
	if (err)
		goto err_add_rule;

	kfree(list);

	return rule;

err_add_rule:
	kfree(list);
err_list_alloc:
	kfree(rule);

	return ERR_PTR(err);
}

static struct ice_esw_br_flow *
ice_eswitch_br_flow_create(struct device *dev, struct ice_hw *hw, int vsi_idx,
			   int port_type, const unsigned char *mac)
{
	struct ice_rule_query_data *fwd_rule;
	struct ice_esw_br_flow *flow;
	int err;

	flow = kzalloc(sizeof(*flow), GFP_KERNEL);
	if (!flow)
		return ERR_PTR(-ENOMEM);

	fwd_rule = ice_eswitch_br_fwd_rule_create(hw, vsi_idx, port_type, mac);
	err = PTR_ERR_OR_ZERO(fwd_rule);
	if (err) {
		dev_err(dev, "Failed to create eswitch bridge %sgress forward rule, err: %d\n",
			port_type == ICE_ESWITCH_BR_UPLINK_PORT ? "e" : "in",
			err);
		goto err_fwd_rule;
	}

	flow->fwd_rule = fwd_rule;

	return flow;

err_fwd_rule:
	kfree(flow);

	return ERR_PTR(err);
}

static struct ice_esw_br_fdb_entry *
ice_eswitch_br_fdb_find(struct ice_esw_br *bridge, const unsigned char *mac,
			u16 vid)
{
	struct ice_esw_br_fdb_data data = {
		.vid = vid,
	};

	ether_addr_copy(data.addr, mac);
	return rhashtable_lookup_fast(&bridge->fdb_ht, &data,
				      ice_fdb_ht_params);
}

static void
ice_eswitch_br_flow_delete(struct ice_pf *pf, struct ice_esw_br_flow *flow)
{
	struct device *dev = ice_pf_to_dev(pf);
	int err;

	err = ice_eswitch_br_rule_delete(&pf->hw, flow->fwd_rule);
	if (err)
		dev_err(dev, "Failed to delete FDB forward rule, err: %d\n",
			err);

	kfree(flow);
}

static void
ice_eswitch_br_fdb_entry_delete(struct ice_esw_br *bridge,
				struct ice_esw_br_fdb_entry *fdb_entry)
{
	struct ice_pf *pf = bridge->br_offloads->pf;

	rhashtable_remove_fast(&bridge->fdb_ht, &fdb_entry->ht_node,
			       ice_fdb_ht_params);
	list_del(&fdb_entry->list);

	ice_eswitch_br_flow_delete(pf, fdb_entry->flow);

	kfree(fdb_entry);
}

static void
ice_eswitch_br_fdb_offload_notify(struct net_device *dev,
				  const unsigned char *mac, u16 vid,
				  unsigned long val)
{
	struct switchdev_notifier_fdb_info fdb_info = {
		.addr = mac,
		.vid = vid,
		.offloaded = true,
	};

	call_switchdev_notifiers(val, dev, &fdb_info.info, NULL);
}

static void
ice_eswitch_br_fdb_entry_notify_and_cleanup(struct ice_esw_br *bridge,
					    struct ice_esw_br_fdb_entry *entry)
{
	if (!(entry->flags & ICE_ESWITCH_BR_FDB_ADDED_BY_USER))
		ice_eswitch_br_fdb_offload_notify(entry->dev, entry->data.addr,
						  entry->data.vid,
						  SWITCHDEV_FDB_DEL_TO_BRIDGE);
	ice_eswitch_br_fdb_entry_delete(bridge, entry);
}

static void
ice_eswitch_br_fdb_entry_find_and_delete(struct ice_esw_br *bridge,
					 const unsigned char *mac, u16 vid)
{
	struct ice_pf *pf = bridge->br_offloads->pf;
	struct ice_esw_br_fdb_entry *fdb_entry;
	struct device *dev = ice_pf_to_dev(pf);

	fdb_entry = ice_eswitch_br_fdb_find(bridge, mac, vid);
	if (!fdb_entry) {
		dev_err(dev, "FDB entry with mac: %pM and vid: %u not found\n",
			mac, vid);
		return;
	}

	ice_eswitch_br_fdb_entry_notify_and_cleanup(bridge, fdb_entry);
}

static void
ice_eswitch_br_fdb_entry_create(struct net_device *netdev,
				struct ice_esw_br_port *br_port,
				bool added_by_user,
				const unsigned char *mac, u16 vid)
{
	struct ice_esw_br *bridge = br_port->bridge;
	struct ice_pf *pf = bridge->br_offloads->pf;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_esw_br_fdb_entry *fdb_entry;
	struct ice_esw_br_flow *flow;
	struct ice_hw *hw = &pf->hw;
	unsigned long event;
	int err;

	fdb_entry = ice_eswitch_br_fdb_find(bridge, mac, vid);
	if (fdb_entry)
		ice_eswitch_br_fdb_entry_notify_and_cleanup(bridge, fdb_entry);

	fdb_entry = kzalloc(sizeof(*fdb_entry), GFP_KERNEL);
	if (!fdb_entry) {
		err = -ENOMEM;
		goto err_exit;
	}

	flow = ice_eswitch_br_flow_create(dev, hw, br_port->vsi_idx,
					  br_port->type, mac);
	if (IS_ERR(flow)) {
		err = PTR_ERR(flow);
		goto err_add_flow;
	}

	ether_addr_copy(fdb_entry->data.addr, mac);
	fdb_entry->data.vid = vid;
	fdb_entry->br_port = br_port;
	fdb_entry->flow = flow;
	fdb_entry->dev = netdev;
	event = SWITCHDEV_FDB_ADD_TO_BRIDGE;

	if (added_by_user) {
		fdb_entry->flags |= ICE_ESWITCH_BR_FDB_ADDED_BY_USER;
		event = SWITCHDEV_FDB_OFFLOADED;
	}

	err = rhashtable_insert_fast(&bridge->fdb_ht, &fdb_entry->ht_node,
				     ice_fdb_ht_params);
	if (err)
		goto err_fdb_insert;

	list_add(&fdb_entry->list, &bridge->fdb_list);

	ice_eswitch_br_fdb_offload_notify(netdev, mac, vid, event);

	return;

err_fdb_insert:
	ice_eswitch_br_flow_delete(pf, flow);
err_add_flow:
	kfree(fdb_entry);
err_exit:
	dev_err(dev, "Failed to create fdb entry, err: %d\n", err);
}

static void
ice_eswitch_br_fdb_work_dealloc(struct ice_esw_br_fdb_work *fdb_work)
{
	kfree(fdb_work->fdb_info.addr);
	kfree(fdb_work);
}

static void
ice_eswitch_br_fdb_event_work(struct work_struct *work)
{
	struct ice_esw_br_fdb_work *fdb_work = ice_work_to_fdb_work(work);
	bool added_by_user = fdb_work->fdb_info.added_by_user;
	const unsigned char *mac = fdb_work->fdb_info.addr;
	u16 vid = fdb_work->fdb_info.vid;
	struct ice_esw_br_port *br_port;

	rtnl_lock();

	br_port = ice_eswitch_br_netdev_to_port(fdb_work->dev);
	if (!br_port)
		goto err_exit;

	switch (fdb_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		ice_eswitch_br_fdb_entry_create(fdb_work->dev, br_port,
						added_by_user, mac, vid);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		ice_eswitch_br_fdb_entry_find_and_delete(br_port->bridge,
							 mac, vid);
		break;
	default:
		goto err_exit;
	}

err_exit:
	rtnl_unlock();
	dev_put(fdb_work->dev);
	ice_eswitch_br_fdb_work_dealloc(fdb_work);
}

static struct ice_esw_br_fdb_work *
ice_eswitch_br_fdb_work_alloc(struct switchdev_notifier_fdb_info *fdb_info,
			      struct net_device *dev,
			      unsigned long event)
{
	struct ice_esw_br_fdb_work *work;
	unsigned char *mac;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&work->work, ice_eswitch_br_fdb_event_work);
	memcpy(&work->fdb_info, fdb_info, sizeof(work->fdb_info));

	mac = kzalloc(ETH_ALEN, GFP_ATOMIC);
	if (!mac) {
		kfree(work);
		return ERR_PTR(-ENOMEM);
	}

	ether_addr_copy(mac, fdb_info->addr);
	work->fdb_info.addr = mac;
	work->event = event;
	work->dev = dev;

	return work;
}

static int
ice_eswitch_br_switchdev_event(struct notifier_block *nb,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct switchdev_notifier_fdb_info *fdb_info;
	struct switchdev_notifier_info *info = ptr;
	struct ice_esw_br_offloads *br_offloads;
	struct ice_esw_br_fdb_work *work;
	struct netlink_ext_ack *extack;
	struct net_device *upper;

	br_offloads = ice_nb_to_br_offloads(nb, switchdev_nb);
	extack = switchdev_notifier_info_to_extack(ptr);

	upper = netdev_master_upper_dev_get_rcu(dev);
	if (!upper)
		return NOTIFY_DONE;

	if (!netif_is_bridge_master(upper))
		return NOTIFY_DONE;

	if (!ice_eswitch_br_is_dev_valid(dev))
		return NOTIFY_DONE;

	if (!ice_eswitch_br_netdev_to_port(dev))
		return NOTIFY_DONE;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb_info = container_of(info, typeof(*fdb_info), info);

		work = ice_eswitch_br_fdb_work_alloc(fdb_info, dev, event);
		if (IS_ERR(work)) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to init switchdev fdb work");
			return notifier_from_errno(PTR_ERR(work));
		}
		dev_hold(dev);

		queue_work(br_offloads->wq, &work->work);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static void
ice_eswitch_br_port_deinit(struct ice_esw_br *bridge,
			   struct ice_esw_br_port *br_port)
{
	struct ice_esw_br_fdb_entry *fdb_entry, *tmp;
	struct ice_vsi *vsi = br_port->vsi;

	list_for_each_entry_safe(fdb_entry, tmp, &bridge->fdb_list, list) {
		if (br_port == fdb_entry->br_port)
			ice_eswitch_br_fdb_entry_delete(bridge, fdb_entry);
	}

	if (br_port->type == ICE_ESWITCH_BR_UPLINK_PORT && vsi->back)
		vsi->back->br_port = NULL;
	else if (vsi->vf && vsi->vf->repr)
		vsi->vf->repr->br_port = NULL;

	xa_erase(&bridge->ports, br_port->vsi_idx);
	kfree(br_port);
}

static struct ice_esw_br_port *
ice_eswitch_br_port_init(struct ice_esw_br *bridge)
{
	struct ice_esw_br_port *br_port;

	br_port = kzalloc(sizeof(*br_port), GFP_KERNEL);
	if (!br_port)
		return ERR_PTR(-ENOMEM);

	br_port->bridge = bridge;

	return br_port;
}

static int
ice_eswitch_br_vf_repr_port_init(struct ice_esw_br *bridge,
				 struct ice_repr *repr)
{
	struct ice_esw_br_port *br_port;
	int err;

	br_port = ice_eswitch_br_port_init(bridge);
	if (IS_ERR(br_port))
		return PTR_ERR(br_port);

	br_port->vsi = repr->src_vsi;
	br_port->vsi_idx = br_port->vsi->idx;
	br_port->type = ICE_ESWITCH_BR_VF_REPR_PORT;
	repr->br_port = br_port;

	err = xa_insert(&bridge->ports, br_port->vsi_idx, br_port, GFP_KERNEL);
	if (err) {
		ice_eswitch_br_port_deinit(bridge, br_port);
		return err;
	}

	return 0;
}

static int
ice_eswitch_br_uplink_port_init(struct ice_esw_br *bridge, struct ice_pf *pf)
{
	struct ice_vsi *vsi = pf->switchdev.uplink_vsi;
	struct ice_esw_br_port *br_port;
	int err;

	br_port = ice_eswitch_br_port_init(bridge);
	if (IS_ERR(br_port))
		return PTR_ERR(br_port);

	br_port->vsi = vsi;
	br_port->vsi_idx = br_port->vsi->idx;
	br_port->type = ICE_ESWITCH_BR_UPLINK_PORT;
	pf->br_port = br_port;

	err = xa_insert(&bridge->ports, br_port->vsi_idx, br_port, GFP_KERNEL);
	if (err) {
		ice_eswitch_br_port_deinit(bridge, br_port);
		return err;
	}

	return 0;
}

static void
ice_eswitch_br_ports_flush(struct ice_esw_br *bridge)
{
	struct ice_esw_br_port *port;
	unsigned long i;

	xa_for_each(&bridge->ports, i, port)
		ice_eswitch_br_port_deinit(bridge, port);
}

static void
ice_eswitch_br_deinit(struct ice_esw_br_offloads *br_offloads,
		      struct ice_esw_br *bridge)
{
	if (!bridge)
		return;

	/* Cleanup all the ports that were added asynchronously
	 * through NETDEV_CHANGEUPPER event.
	 */
	ice_eswitch_br_ports_flush(bridge);
	WARN_ON(!xa_empty(&bridge->ports));
	xa_destroy(&bridge->ports);
	rhashtable_destroy(&bridge->fdb_ht);

	br_offloads->bridge = NULL;
	kfree(bridge);
}

static struct ice_esw_br *
ice_eswitch_br_init(struct ice_esw_br_offloads *br_offloads, int ifindex)
{
	struct ice_esw_br *bridge;
	int err;

	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return ERR_PTR(-ENOMEM);

	err = rhashtable_init(&bridge->fdb_ht, &ice_fdb_ht_params);
	if (err) {
		kfree(bridge);
		return ERR_PTR(err);
	}

	INIT_LIST_HEAD(&bridge->fdb_list);
	bridge->br_offloads = br_offloads;
	bridge->ifindex = ifindex;
	xa_init(&bridge->ports);
	br_offloads->bridge = bridge;

	return bridge;
}

static struct ice_esw_br *
ice_eswitch_br_get(struct ice_esw_br_offloads *br_offloads, int ifindex,
		   struct netlink_ext_ack *extack)
{
	struct ice_esw_br *bridge = br_offloads->bridge;

	if (bridge) {
		if (bridge->ifindex != ifindex) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one bridge is supported per eswitch");
			return ERR_PTR(-EOPNOTSUPP);
		}
		return bridge;
	}

	/* Create the bridge if it doesn't exist yet */
	bridge = ice_eswitch_br_init(br_offloads, ifindex);
	if (IS_ERR(bridge))
		NL_SET_ERR_MSG_MOD(extack, "Failed to init the bridge");

	return bridge;
}

static void
ice_eswitch_br_verify_deinit(struct ice_esw_br_offloads *br_offloads,
			     struct ice_esw_br *bridge)
{
	/* Remove the bridge if it exists and there are no ports left */
	if (!bridge || !xa_empty(&bridge->ports))
		return;

	ice_eswitch_br_deinit(br_offloads, bridge);
}

static int
ice_eswitch_br_port_unlink(struct ice_esw_br_offloads *br_offloads,
			   struct net_device *dev, int ifindex,
			   struct netlink_ext_ack *extack)
{
	struct ice_esw_br_port *br_port = ice_eswitch_br_netdev_to_port(dev);
	struct ice_esw_br *bridge;

	if (!br_port) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Port representor is not attached to any bridge");
		return -EINVAL;
	}

	if (br_port->bridge->ifindex != ifindex) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Port representor is attached to another bridge");
		return -EINVAL;
	}

	bridge = br_port->bridge;

	ice_eswitch_br_port_deinit(br_port->bridge, br_port);
	ice_eswitch_br_verify_deinit(br_offloads, bridge);

	return 0;
}

static int
ice_eswitch_br_port_link(struct ice_esw_br_offloads *br_offloads,
			 struct net_device *dev, int ifindex,
			 struct netlink_ext_ack *extack)
{
	struct ice_esw_br *bridge;
	int err;

	if (ice_eswitch_br_netdev_to_port(dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Port is already attached to the bridge");
		return -EINVAL;
	}

	bridge = ice_eswitch_br_get(br_offloads, ifindex, extack);
	if (IS_ERR(bridge))
		return PTR_ERR(bridge);

	if (ice_is_port_repr_netdev(dev)) {
		struct ice_repr *repr = ice_netdev_to_repr(dev);

		err = ice_eswitch_br_vf_repr_port_init(bridge, repr);
	} else {
		struct ice_pf *pf = ice_netdev_to_pf(dev);

		err = ice_eswitch_br_uplink_port_init(bridge, pf);
	}
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to init bridge port");
		goto err_port_init;
	}

	return 0;

err_port_init:
	ice_eswitch_br_verify_deinit(br_offloads, bridge);
	return err;
}

static int
ice_eswitch_br_port_changeupper(struct notifier_block *nb, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct ice_esw_br_offloads *br_offloads;
	struct netlink_ext_ack *extack;
	struct net_device *upper;

	br_offloads = ice_nb_to_br_offloads(nb, netdev_nb);

	if (!ice_eswitch_br_is_dev_valid(dev))
		return 0;

	upper = info->upper_dev;
	if (!netif_is_bridge_master(upper))
		return 0;

	extack = netdev_notifier_info_to_extack(&info->info);

	if (info->linking)
		return ice_eswitch_br_port_link(br_offloads, dev,
						upper->ifindex, extack);
	else
		return ice_eswitch_br_port_unlink(br_offloads, dev,
						  upper->ifindex, extack);
}

static int
ice_eswitch_br_port_event(struct notifier_block *nb,
			  unsigned long event, void *ptr)
{
	int err = 0;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		err = ice_eswitch_br_port_changeupper(nb, ptr);
		break;
	}

	return notifier_from_errno(err);
}

static void
ice_eswitch_br_offloads_dealloc(struct ice_pf *pf)
{
	struct ice_esw_br_offloads *br_offloads = pf->switchdev.br_offloads;

	ASSERT_RTNL();

	if (!br_offloads)
		return;

	ice_eswitch_br_deinit(br_offloads, br_offloads->bridge);

	pf->switchdev.br_offloads = NULL;
	kfree(br_offloads);
}

static struct ice_esw_br_offloads *
ice_eswitch_br_offloads_alloc(struct ice_pf *pf)
{
	struct ice_esw_br_offloads *br_offloads;

	ASSERT_RTNL();

	if (pf->switchdev.br_offloads)
		return ERR_PTR(-EEXIST);

	br_offloads = kzalloc(sizeof(*br_offloads), GFP_KERNEL);
	if (!br_offloads)
		return ERR_PTR(-ENOMEM);

	pf->switchdev.br_offloads = br_offloads;
	br_offloads->pf = pf;

	return br_offloads;
}

void
ice_eswitch_br_offloads_deinit(struct ice_pf *pf)
{
	struct ice_esw_br_offloads *br_offloads;

	br_offloads = pf->switchdev.br_offloads;
	if (!br_offloads)
		return;

	unregister_netdevice_notifier(&br_offloads->netdev_nb);
	unregister_switchdev_notifier(&br_offloads->switchdev_nb);
	destroy_workqueue(br_offloads->wq);
	/* Although notifier block is unregistered just before,
	 * so we don't get any new events, some events might be
	 * already in progress. Hold the rtnl lock and wait for
	 * them to finished.
	 */
	rtnl_lock();
	ice_eswitch_br_offloads_dealloc(pf);
	rtnl_unlock();
}

int
ice_eswitch_br_offloads_init(struct ice_pf *pf)
{
	struct ice_esw_br_offloads *br_offloads;
	struct device *dev = ice_pf_to_dev(pf);
	int err;

	rtnl_lock();
	br_offloads = ice_eswitch_br_offloads_alloc(pf);
	rtnl_unlock();
	if (IS_ERR(br_offloads)) {
		dev_err(dev, "Failed to init eswitch bridge\n");
		return PTR_ERR(br_offloads);
	}

	br_offloads->wq = alloc_ordered_workqueue("ice_bridge_wq", 0);
	if (!br_offloads->wq) {
		err = -ENOMEM;
		dev_err(dev, "Failed to allocate bridge workqueue\n");
		goto err_alloc_wq;
	}

	br_offloads->switchdev_nb.notifier_call =
		ice_eswitch_br_switchdev_event;
	err = register_switchdev_notifier(&br_offloads->switchdev_nb);
	if (err) {
		dev_err(dev,
			"Failed to register switchdev notifier\n");
		goto err_reg_switchdev_nb;
	}

	br_offloads->netdev_nb.notifier_call = ice_eswitch_br_port_event;
	err = register_netdevice_notifier(&br_offloads->netdev_nb);
	if (err) {
		dev_err(dev,
			"Failed to register bridge port event notifier\n");
		goto err_reg_netdev_nb;
	}

	return 0;

err_reg_netdev_nb:
	unregister_switchdev_notifier(&br_offloads->switchdev_nb);
err_reg_switchdev_nb:
	destroy_workqueue(br_offloads->wq);
err_alloc_wq:
	rtnl_lock();
	ice_eswitch_br_offloads_dealloc(pf);
	rtnl_unlock();

	return err;
}
