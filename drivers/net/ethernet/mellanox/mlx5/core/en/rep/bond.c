// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Inc. All rights reserved. */

#include <linux/netdevice.h>
#include <linux/list.h>
#include <net/lag.h>

#include "mlx5_core.h"
#include "eswitch.h"
#include "esw/acl/ofld.h"
#include "en_rep.h"

struct mlx5e_rep_bond {
	struct notifier_block nb;
	struct netdev_net_notifier nn;
	struct list_head metadata_list;
};

struct mlx5e_rep_bond_slave_entry {
	struct list_head list;
	struct net_device *netdev;
};

struct mlx5e_rep_bond_metadata {
	struct list_head list; /* link to global list of rep_bond_metadata */
	struct mlx5_eswitch *esw;
	 /* private of uplink holding rep bond metadata list */
	struct net_device *lag_dev;
	u32 metadata_reg_c_0;

	struct list_head slaves_list; /* slaves list */
	int slaves;
};

static struct mlx5e_rep_bond_metadata *
mlx5e_lookup_rep_bond_metadata(struct mlx5_rep_uplink_priv *uplink_priv,
			       const struct net_device *lag_dev)
{
	struct mlx5e_rep_bond_metadata *found = NULL;
	struct mlx5e_rep_bond_metadata *cur;

	list_for_each_entry(cur, &uplink_priv->bond->metadata_list, list) {
		if (cur->lag_dev == lag_dev) {
			found = cur;
			break;
		}
	}

	return found;
}

static struct mlx5e_rep_bond_slave_entry *
mlx5e_lookup_rep_bond_slave_entry(struct mlx5e_rep_bond_metadata *mdata,
				  const struct net_device *netdev)
{
	struct mlx5e_rep_bond_slave_entry *found = NULL;
	struct mlx5e_rep_bond_slave_entry *cur;

	list_for_each_entry(cur, &mdata->slaves_list, list) {
		if (cur->netdev == netdev) {
			found = cur;
			break;
		}
	}

	return found;
}

static void mlx5e_rep_bond_metadata_release(struct mlx5e_rep_bond_metadata *mdata)
{
	netdev_dbg(mdata->lag_dev, "destroy rep_bond_metadata(%d)\n",
		   mdata->metadata_reg_c_0);
	list_del(&mdata->list);
	WARN_ON(!list_empty(&mdata->slaves_list));
	kfree(mdata);
}

/* This must be called under rtnl_lock */
int mlx5e_rep_bond_enslave(struct mlx5_eswitch *esw, struct net_device *netdev,
			   struct net_device *lag_dev)
{
	struct mlx5e_rep_bond_slave_entry *s_entry;
	struct mlx5e_rep_bond_metadata *mdata;
	struct mlx5e_rep_priv *rpriv;

	ASSERT_RTNL();

	rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	mdata = mlx5e_lookup_rep_bond_metadata(&rpriv->uplink_priv, lag_dev);
	if (!mdata) {
		/* First netdev becomes slave, no metadata presents the lag_dev. Create one */
		mdata = kzalloc(sizeof(*mdata), GFP_KERNEL);
		if (!mdata)
			return -ENOMEM;

		mdata->lag_dev = lag_dev;
		mdata->esw = esw;
		INIT_LIST_HEAD(&mdata->slaves_list);
		list_add(&mdata->list, &rpriv->uplink_priv.bond->metadata_list);

		netdev_dbg(lag_dev, "create rep_bond_metadata(%d)\n",
			   mdata->metadata_reg_c_0);
	}

	s_entry = kzalloc(sizeof(*s_entry), GFP_KERNEL);
	if (!s_entry)
		return -ENOMEM;

	s_entry->netdev = netdev;
	mdata->slaves++;
	list_add_tail(&s_entry->list, &mdata->slaves_list);

	return 0;
}

/* This must be called under rtnl_lock */
void mlx5e_rep_bond_unslave(struct mlx5_eswitch *esw,
			    const struct net_device *netdev,
			    const struct net_device *lag_dev)
{
	struct mlx5e_rep_bond_slave_entry *s_entry;
	struct mlx5e_rep_bond_metadata *mdata;
	struct mlx5e_rep_priv *rpriv;

	ASSERT_RTNL();

	rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	mdata = mlx5e_lookup_rep_bond_metadata(&rpriv->uplink_priv, lag_dev);
	if (!mdata)
		return;

	s_entry = mlx5e_lookup_rep_bond_slave_entry(mdata, netdev);
	if (!s_entry)
		return;

	list_del(&s_entry->list);
	if (--mdata->slaves == 0)
		mlx5e_rep_bond_metadata_release(mdata);
	kfree(s_entry);
}

static bool mlx5e_rep_is_lag_netdev(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	/* A given netdev is not a representor or not a slave of LAG configuration */
	if (!mlx5e_eswitch_rep(netdev) || !bond_slave_get_rtnl(netdev))
		return false;

	/* Egress acl forward to vport is supported only non-uplink representor */
	return rpriv->rep->vport != MLX5_VPORT_UPLINK;
}

static void mlx5e_rep_changelowerstate_event(struct net_device *netdev, void *ptr)
{
	struct netdev_notifier_changelowerstate_info *info;
	struct netdev_lag_lower_state_info *lag_info;
	struct mlx5e_rep_priv *rpriv;
	struct net_device *lag_dev;
	struct mlx5e_priv *priv;
	struct list_head *iter;
	struct net_device *dev;
	u16 acl_vport_num;
	u16 fwd_vport_num;

	if (!mlx5e_rep_is_lag_netdev(netdev))
		return;

	info = ptr;
	lag_info = info->lower_state_info;
	/* This is not an event of a representor becoming active slave */
	if (!lag_info->tx_enabled)
		return;

	priv = netdev_priv(netdev);
	rpriv = priv->ppriv;
	fwd_vport_num = rpriv->rep->vport;
	lag_dev = netdev_master_upper_dev_get(netdev);

	netdev_dbg(netdev, "lag_dev(%s)'s slave vport(%d) is txable(%d)\n",
		   lag_dev->name, fwd_vport_num, net_lag_port_dev_txable(netdev));

	/* Point everyone's egress acl to the vport of the active representor */
	netdev_for_each_lower_dev(lag_dev, dev, iter) {
		priv = netdev_priv(dev);
		rpriv = priv->ppriv;
		acl_vport_num = rpriv->rep->vport;
		if (acl_vport_num != fwd_vport_num) {
			mlx5_esw_acl_egress_vport_bond(priv->mdev->priv.eswitch,
						       fwd_vport_num,
						       acl_vport_num);
		}
	}
}

static void mlx5e_rep_changeupper_event(struct net_device *netdev, void *ptr)
{
	struct netdev_notifier_changeupper_info *info = ptr;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5e_priv *priv;

	if (!mlx5e_rep_is_lag_netdev(netdev))
		return;

	/* Nothing to setup for new enslaved representor */
	if (info->linking)
		return;

	priv = netdev_priv(netdev);
	rpriv = priv->ppriv;
	netdev_dbg(netdev, "Unslave, reset vport(%d) egress acl\n", rpriv->rep->vport);

	/* Reset all egress acl rules of unslave representor's vport */
	mlx5_esw_acl_egress_vport_unbond(priv->mdev->priv.eswitch,
					 rpriv->rep->vport);
}

/* Bond device of representors and netdev events are used here in specific way
 * to support eswitch vports bonding and to perform failover of eswitch vport
 * by modifying the vport's egress acl of lower dev representors. Thus this
 * also change the traditional behavior of lower dev under bond device.
 * All non-representor netdevs or representors of other vendors as lower dev
 * of bond device are not supported.
 */
static int mlx5e_rep_esw_bond_netevent(struct notifier_block *nb,
				       unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_CHANGELOWERSTATE:
		mlx5e_rep_changelowerstate_event(netdev, ptr);
		break;
	case NETDEV_CHANGEUPPER:
		mlx5e_rep_changeupper_event(netdev, ptr);
		break;
	}
	return NOTIFY_DONE;
}

/* If HW support eswitch vports bonding, register a specific notifier to
 * handle it when two or more representors are bonded
 */
int mlx5e_rep_bond_init(struct mlx5e_rep_priv *rpriv)
{
	struct mlx5_rep_uplink_priv *uplink_priv = &rpriv->uplink_priv;
	struct net_device *netdev = rpriv->netdev;
	struct mlx5e_priv *priv;
	int ret = 0;

	priv = netdev_priv(netdev);
	if (!mlx5_esw_acl_egress_fwd2vport_supported(priv->mdev->priv.eswitch))
		goto out;

	uplink_priv->bond = kvzalloc(sizeof(*uplink_priv->bond), GFP_KERNEL);
	if (!uplink_priv->bond) {
		ret = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&uplink_priv->bond->metadata_list);
	uplink_priv->bond->nb.notifier_call = mlx5e_rep_esw_bond_netevent;
	ret = register_netdevice_notifier_dev_net(netdev,
						  &uplink_priv->bond->nb,
						  &uplink_priv->bond->nn);
	if (ret) {
		netdev_err(netdev, "register bonding netevent notifier, err(%d)\n", ret);
		kvfree(uplink_priv->bond);
		uplink_priv->bond = NULL;
	}

out:
	return ret;
}

void mlx5e_rep_bond_cleanup(struct mlx5e_rep_priv *rpriv)
{
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);

	if (!mlx5_esw_acl_egress_fwd2vport_supported(priv->mdev->priv.eswitch) ||
	    !rpriv->uplink_priv.bond)
		return;

	unregister_netdevice_notifier_dev_net(rpriv->netdev,
					      &rpriv->uplink_priv.bond->nb,
					      &rpriv->uplink_priv.bond->nn);
	kvfree(rpriv->uplink_priv.bond);
}
