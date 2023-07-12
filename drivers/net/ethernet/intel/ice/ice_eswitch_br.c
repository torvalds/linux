// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023, Intel Corporation. */

#include "ice.h"
#include "ice_eswitch_br.h"
#include "ice_repr.h"

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
ice_eswitch_br_port_deinit(struct ice_esw_br *bridge,
			   struct ice_esw_br_port *br_port)
{
	struct ice_vsi *vsi = br_port->vsi;

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
	br_offloads->bridge = NULL;
	kfree(bridge);
}

static struct ice_esw_br *
ice_eswitch_br_init(struct ice_esw_br_offloads *br_offloads, int ifindex)
{
	struct ice_esw_br *bridge;

	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return ERR_PTR(-ENOMEM);

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

	br_offloads->netdev_nb.notifier_call = ice_eswitch_br_port_event;
	err = register_netdevice_notifier(&br_offloads->netdev_nb);
	if (err) {
		dev_err(dev,
			"Failed to register bridge port event notifier\n");
		goto err_reg_netdev_nb;
	}

	return 0;

err_reg_netdev_nb:
	rtnl_lock();
	ice_eswitch_br_offloads_dealloc(pf);
	rtnl_unlock();

	return err;
}
