// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies. */

#include <linux/netdevice.h>
#include <linux/if_macvlan.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/rtnetlink.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include "tc.h"
#include "neigh.h"
#include "en_rep.h"
#include "eswitch.h"
#include "lib/fs_chains.h"
#include "en/tc_ct.h"
#include "en/mapping.h"
#include "en/tc_tun.h"
#include "lib/port_tun.h"
#include "en/tc/sample.h"
#include "en_accel/ipsec_rxtx.h"
#include "en/tc/int_port.h"
#include "en/tc/act/act.h"

struct mlx5e_rep_indr_block_priv {
	struct net_device *netdev;
	struct mlx5e_rep_priv *rpriv;
	enum flow_block_binder_type binder_type;

	struct list_head list;
};

int mlx5e_rep_encap_entry_attach(struct mlx5e_priv *priv,
				 struct mlx5e_encap_entry *e,
				 struct mlx5e_neigh *m_neigh,
				 struct net_device *neigh_dev)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_rep_uplink_priv *uplink_priv = &rpriv->uplink_priv;
	struct mlx5_tun_entropy *tun_entropy = &uplink_priv->tun_entropy;
	struct mlx5e_neigh_hash_entry *nhe;
	int err;

	err = mlx5_tun_entropy_refcount_inc(tun_entropy, e->reformat_type);
	if (err)
		return err;

	mutex_lock(&rpriv->neigh_update.encap_lock);
	nhe = mlx5e_rep_neigh_entry_lookup(priv, m_neigh);
	if (!nhe) {
		err = mlx5e_rep_neigh_entry_create(priv, m_neigh, neigh_dev, &nhe);
		if (err) {
			mutex_unlock(&rpriv->neigh_update.encap_lock);
			mlx5_tun_entropy_refcount_dec(tun_entropy,
						      e->reformat_type);
			return err;
		}
	}

	e->nhe = nhe;
	spin_lock(&nhe->encap_list_lock);
	list_add_rcu(&e->encap_list, &nhe->encap_list);
	spin_unlock(&nhe->encap_list_lock);

	mutex_unlock(&rpriv->neigh_update.encap_lock);

	return 0;
}

void mlx5e_rep_encap_entry_detach(struct mlx5e_priv *priv,
				  struct mlx5e_encap_entry *e)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_rep_uplink_priv *uplink_priv = &rpriv->uplink_priv;
	struct mlx5_tun_entropy *tun_entropy = &uplink_priv->tun_entropy;

	if (!e->nhe)
		return;

	spin_lock(&e->nhe->encap_list_lock);
	list_del_rcu(&e->encap_list);
	spin_unlock(&e->nhe->encap_list_lock);

	mlx5e_rep_neigh_entry_release(e->nhe);
	e->nhe = NULL;
	mlx5_tun_entropy_refcount_dec(tun_entropy, e->reformat_type);
}

void mlx5e_rep_update_flows(struct mlx5e_priv *priv,
			    struct mlx5e_encap_entry *e,
			    bool neigh_connected,
			    unsigned char ha[ETH_ALEN])
{
	struct ethhdr *eth = (struct ethhdr *)e->encap_header;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	bool encap_connected;
	LIST_HEAD(flow_list);

	ASSERT_RTNL();

	mutex_lock(&esw->offloads.encap_tbl_lock);
	encap_connected = !!(e->flags & MLX5_ENCAP_ENTRY_VALID);
	if (encap_connected == neigh_connected && ether_addr_equal(e->h_dest, ha))
		goto unlock;

	mlx5e_take_all_encap_flows(e, &flow_list);

	if ((e->flags & MLX5_ENCAP_ENTRY_VALID) &&
	    (!neigh_connected || !ether_addr_equal(e->h_dest, ha)))
		mlx5e_tc_encap_flows_del(priv, e, &flow_list);

	if (neigh_connected && !(e->flags & MLX5_ENCAP_ENTRY_VALID)) {
		struct net_device *route_dev;

		ether_addr_copy(e->h_dest, ha);
		ether_addr_copy(eth->h_dest, ha);
		/* Update the encap source mac, in case that we delete
		 * the flows when encap source mac changed.
		 */
		route_dev = __dev_get_by_index(dev_net(priv->netdev), e->route_dev_ifindex);
		if (route_dev)
			ether_addr_copy(eth->h_source, route_dev->dev_addr);

		mlx5e_tc_encap_flows_add(priv, e, &flow_list);
	}
unlock:
	mutex_unlock(&esw->offloads.encap_tbl_lock);
	mlx5e_put_flow_list(priv, &flow_list);
}

static int
mlx5e_rep_setup_tc_cls_flower(struct mlx5e_priv *priv,
			      struct flow_cls_offload *cls_flower, int flags)
{
	switch (cls_flower->command) {
	case FLOW_CLS_REPLACE:
		return mlx5e_configure_flower(priv->netdev, priv, cls_flower,
					      flags);
	case FLOW_CLS_DESTROY:
		return mlx5e_delete_flower(priv->netdev, priv, cls_flower,
					   flags);
	case FLOW_CLS_STATS:
		return mlx5e_stats_flower(priv->netdev, priv, cls_flower,
					  flags);
	default:
		return -EOPNOTSUPP;
	}
}

static
int mlx5e_rep_setup_tc_cls_matchall(struct mlx5e_priv *priv,
				    struct tc_cls_matchall_offload *ma)
{
	switch (ma->command) {
	case TC_CLSMATCHALL_REPLACE:
		return mlx5e_tc_configure_matchall(priv, ma);
	case TC_CLSMATCHALL_DESTROY:
		return mlx5e_tc_delete_matchall(priv, ma);
	case TC_CLSMATCHALL_STATS:
		mlx5e_tc_stats_matchall(priv, ma);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int mlx5e_rep_setup_tc_cb(enum tc_setup_type type, void *type_data,
				 void *cb_priv)
{
	unsigned long flags = MLX5_TC_FLAG(INGRESS) | MLX5_TC_FLAG(ESW_OFFLOAD);
	struct mlx5e_priv *priv = cb_priv;

	if (!priv->netdev || !netif_device_present(priv->netdev))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return mlx5e_rep_setup_tc_cls_flower(priv, type_data, flags);
	case TC_SETUP_CLSMATCHALL:
		return mlx5e_rep_setup_tc_cls_matchall(priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int mlx5e_rep_setup_ft_cb(enum tc_setup_type type, void *type_data,
				 void *cb_priv)
{
	struct flow_cls_offload tmp, *f = type_data;
	struct mlx5e_priv *priv = cb_priv;
	struct mlx5_eswitch *esw;
	unsigned long flags;
	int err;

	flags = MLX5_TC_FLAG(INGRESS) |
		MLX5_TC_FLAG(ESW_OFFLOAD) |
		MLX5_TC_FLAG(FT_OFFLOAD);
	esw = priv->mdev->priv.eswitch;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		memcpy(&tmp, f, sizeof(*f));

		if (!mlx5_chains_prios_supported(esw_chains(esw)))
			return -EOPNOTSUPP;

		/* Re-use tc offload path by moving the ft flow to the
		 * reserved ft chain.
		 *
		 * FT offload can use prio range [0, INT_MAX], so we normalize
		 * it to range [1, mlx5_esw_chains_get_prio_range(esw)]
		 * as with tc, where prio 0 isn't supported.
		 *
		 * We only support chain 0 of FT offload.
		 */
		if (tmp.common.prio >= mlx5_chains_get_prio_range(esw_chains(esw)))
			return -EOPNOTSUPP;
		if (tmp.common.chain_index != 0)
			return -EOPNOTSUPP;

		tmp.common.chain_index = mlx5_chains_get_nf_ft_chain(esw_chains(esw));
		tmp.common.prio++;
		err = mlx5e_rep_setup_tc_cls_flower(priv, &tmp, flags);
		memcpy(&f->stats, &tmp.stats, sizeof(f->stats));
		return err;
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(mlx5e_rep_block_tc_cb_list);
static LIST_HEAD(mlx5e_rep_block_ft_cb_list);
int mlx5e_rep_setup_tc(struct net_device *dev, enum tc_setup_type type,
		       void *type_data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct flow_block_offload *f = type_data;

	f->unlocked_driver_cb = true;

	switch (type) {
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &mlx5e_rep_block_tc_cb_list,
						  mlx5e_rep_setup_tc_cb,
						  priv, priv, true);
	case TC_SETUP_FT:
		return flow_block_cb_setup_simple(type_data,
						  &mlx5e_rep_block_ft_cb_list,
						  mlx5e_rep_setup_ft_cb,
						  priv, priv, true);
	default:
		return -EOPNOTSUPP;
	}
}

int mlx5e_rep_tc_init(struct mlx5e_rep_priv *rpriv)
{
	struct mlx5_rep_uplink_priv *uplink_priv = &rpriv->uplink_priv;
	int err;

	mutex_init(&uplink_priv->unready_flows_lock);
	INIT_LIST_HEAD(&uplink_priv->unready_flows);

	/* init shared tc flow table */
	err = mlx5e_tc_esw_init(uplink_priv);
	return err;
}

void mlx5e_rep_tc_cleanup(struct mlx5e_rep_priv *rpriv)
{
	/* delete shared tc flow table */
	mlx5e_tc_esw_cleanup(&rpriv->uplink_priv);
	mutex_destroy(&rpriv->uplink_priv.unready_flows_lock);
}

void mlx5e_rep_tc_enable(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	INIT_WORK(&rpriv->uplink_priv.reoffload_flows_work,
		  mlx5e_tc_reoffload_flows_work);
}

void mlx5e_rep_tc_disable(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	cancel_work_sync(&rpriv->uplink_priv.reoffload_flows_work);
}

int mlx5e_rep_tc_event_port_affinity(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	queue_work(priv->wq, &rpriv->uplink_priv.reoffload_flows_work);

	return NOTIFY_OK;
}

static struct mlx5e_rep_indr_block_priv *
mlx5e_rep_indr_block_priv_lookup(struct mlx5e_rep_priv *rpriv,
				 struct net_device *netdev,
				 enum flow_block_binder_type binder_type)
{
	struct mlx5e_rep_indr_block_priv *cb_priv;

	list_for_each_entry(cb_priv,
			    &rpriv->uplink_priv.tc_indr_block_priv_list,
			    list)
		if (cb_priv->netdev == netdev &&
		    cb_priv->binder_type == binder_type)
			return cb_priv;

	return NULL;
}

static int
mlx5e_rep_indr_offload(struct net_device *netdev,
		       struct flow_cls_offload *flower,
		       struct mlx5e_rep_indr_block_priv *indr_priv,
		       unsigned long flags)
{
	struct mlx5e_priv *priv = netdev_priv(indr_priv->rpriv->netdev);
	int err = 0;

	if (!netif_device_present(indr_priv->rpriv->netdev))
		return -EOPNOTSUPP;

	switch (flower->command) {
	case FLOW_CLS_REPLACE:
		err = mlx5e_configure_flower(netdev, priv, flower, flags);
		break;
	case FLOW_CLS_DESTROY:
		err = mlx5e_delete_flower(netdev, priv, flower, flags);
		break;
	case FLOW_CLS_STATS:
		err = mlx5e_stats_flower(netdev, priv, flower, flags);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static int mlx5e_rep_indr_setup_tc_cb(enum tc_setup_type type,
				      void *type_data, void *indr_priv)
{
	unsigned long flags = MLX5_TC_FLAG(ESW_OFFLOAD);
	struct mlx5e_rep_indr_block_priv *priv = indr_priv;

	flags |= (priv->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS) ?
		MLX5_TC_FLAG(EGRESS) :
		MLX5_TC_FLAG(INGRESS);

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return mlx5e_rep_indr_offload(priv->netdev, type_data, priv,
					      flags);
	default:
		return -EOPNOTSUPP;
	}
}

static int mlx5e_rep_indr_setup_ft_cb(enum tc_setup_type type,
				      void *type_data, void *indr_priv)
{
	struct mlx5e_rep_indr_block_priv *priv = indr_priv;
	struct flow_cls_offload *f = type_data;
	struct flow_cls_offload tmp;
	struct mlx5e_priv *mpriv;
	struct mlx5_eswitch *esw;
	unsigned long flags;
	int err;

	mpriv = netdev_priv(priv->rpriv->netdev);
	esw = mpriv->mdev->priv.eswitch;

	flags = MLX5_TC_FLAG(EGRESS) |
		MLX5_TC_FLAG(ESW_OFFLOAD) |
		MLX5_TC_FLAG(FT_OFFLOAD);

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		memcpy(&tmp, f, sizeof(*f));

		/* Re-use tc offload path by moving the ft flow to the
		 * reserved ft chain.
		 *
		 * FT offload can use prio range [0, INT_MAX], so we normalize
		 * it to range [1, mlx5_esw_chains_get_prio_range(esw)]
		 * as with tc, where prio 0 isn't supported.
		 *
		 * We only support chain 0 of FT offload.
		 */
		if (!mlx5_chains_prios_supported(esw_chains(esw)) ||
		    tmp.common.prio >= mlx5_chains_get_prio_range(esw_chains(esw)) ||
		    tmp.common.chain_index)
			return -EOPNOTSUPP;

		tmp.common.chain_index = mlx5_chains_get_nf_ft_chain(esw_chains(esw));
		tmp.common.prio++;
		err = mlx5e_rep_indr_offload(priv->netdev, &tmp, priv, flags);
		memcpy(&f->stats, &tmp.stats, sizeof(f->stats));
		return err;
	default:
		return -EOPNOTSUPP;
	}
}

static void mlx5e_rep_indr_block_unbind(void *cb_priv)
{
	struct mlx5e_rep_indr_block_priv *indr_priv = cb_priv;

	list_del(&indr_priv->list);
	kfree(indr_priv);
}

static LIST_HEAD(mlx5e_block_cb_list);

static bool mlx5e_rep_macvlan_mode_supported(const struct net_device *dev)
{
	struct macvlan_dev *macvlan = netdev_priv(dev);

	return macvlan->mode == MACVLAN_MODE_PASSTHRU;
}

static bool
mlx5e_rep_check_indr_block_supported(struct mlx5e_rep_priv *rpriv,
				     struct net_device *netdev,
				     struct flow_block_offload *f)
{
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct net_device *macvlan_real_dev;

	if (f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS &&
	    f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS)
		return false;

	if (mlx5e_tc_tun_device_to_offload(priv, netdev))
		return true;

	if (is_vlan_dev(netdev) && vlan_dev_real_dev(netdev) == rpriv->netdev)
		return true;

	if (netif_is_macvlan(netdev)) {
		if (!mlx5e_rep_macvlan_mode_supported(netdev)) {
			netdev_warn(netdev, "Offloading ingress filter is supported only with macvlan passthru mode");
			return false;
		}

		macvlan_real_dev = macvlan_dev_real_dev(netdev);

		if (macvlan_real_dev == rpriv->netdev)
			return true;
		if (netif_is_bond_master(macvlan_real_dev))
			return true;
	}

	if (netif_is_ovs_master(netdev) && f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS &&
	    mlx5e_tc_int_port_supported(esw))
		return true;

	return false;
}

static int
mlx5e_rep_indr_setup_block(struct net_device *netdev, struct Qdisc *sch,
			   struct mlx5e_rep_priv *rpriv,
			   struct flow_block_offload *f,
			   flow_setup_cb_t *setup_cb,
			   void *data,
			   void (*cleanup)(struct flow_block_cb *block_cb))
{
	struct mlx5e_rep_indr_block_priv *indr_priv;
	struct flow_block_cb *block_cb;

	if (!mlx5e_rep_check_indr_block_supported(rpriv, netdev, f))
		return -EOPNOTSUPP;

	f->unlocked_driver_cb = true;
	f->driver_block_list = &mlx5e_block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		indr_priv = mlx5e_rep_indr_block_priv_lookup(rpriv, netdev, f->binder_type);
		if (indr_priv)
			return -EEXIST;

		indr_priv = kmalloc(sizeof(*indr_priv), GFP_KERNEL);
		if (!indr_priv)
			return -ENOMEM;

		indr_priv->netdev = netdev;
		indr_priv->rpriv = rpriv;
		indr_priv->binder_type = f->binder_type;
		list_add(&indr_priv->list,
			 &rpriv->uplink_priv.tc_indr_block_priv_list);

		block_cb = flow_indr_block_cb_alloc(setup_cb, indr_priv, indr_priv,
						    mlx5e_rep_indr_block_unbind,
						    f, netdev, sch, data, rpriv,
						    cleanup);
		if (IS_ERR(block_cb)) {
			list_del(&indr_priv->list);
			kfree(indr_priv);
			return PTR_ERR(block_cb);
		}
		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, &mlx5e_block_cb_list);

		return 0;
	case FLOW_BLOCK_UNBIND:
		indr_priv = mlx5e_rep_indr_block_priv_lookup(rpriv, netdev, f->binder_type);
		if (!indr_priv)
			return -ENOENT;

		block_cb = flow_block_cb_lookup(f->block, setup_cb, indr_priv);
		if (!block_cb)
			return -ENOENT;

		flow_indr_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int
mlx5e_rep_indr_replace_act(struct mlx5e_rep_priv *rpriv,
			   struct flow_offload_action *fl_act)

{
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	enum mlx5_flow_namespace_type ns_type;
	struct flow_action_entry *action;
	struct mlx5e_tc_act *act;
	bool add = false;
	int i;

	/* There is no use case currently for more than one action (e.g. pedit).
	 * when there will be, need to handle cleaning multiple actions on err.
	 */
	if (!flow_offload_has_one_action(&fl_act->action))
		return -EOPNOTSUPP;

	if (esw && esw->mode == MLX5_ESWITCH_OFFLOADS)
		ns_type = MLX5_FLOW_NAMESPACE_FDB;
	else
		ns_type = MLX5_FLOW_NAMESPACE_KERNEL;

	flow_action_for_each(i, action, &fl_act->action) {
		act = mlx5e_tc_act_get(action->id, ns_type);
		if (!act)
			continue;

		if (!act->offload_action)
			continue;

		if (!act->offload_action(priv, fl_act, action))
			add = true;
	}

	return add ? 0 : -EOPNOTSUPP;
}

static int
mlx5e_rep_indr_destroy_act(struct mlx5e_rep_priv *rpriv,
			   struct flow_offload_action *fl_act)
{
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	enum mlx5_flow_namespace_type ns_type;
	struct mlx5e_tc_act *act;

	if (esw && esw->mode == MLX5_ESWITCH_OFFLOADS)
		ns_type = MLX5_FLOW_NAMESPACE_FDB;
	else
		ns_type = MLX5_FLOW_NAMESPACE_KERNEL;

	act = mlx5e_tc_act_get(fl_act->id, ns_type);
	if (!act || !act->destroy_action)
		return -EOPNOTSUPP;

	return act->destroy_action(priv, fl_act);
}

static int
mlx5e_rep_indr_stats_act(struct mlx5e_rep_priv *rpriv,
			 struct flow_offload_action *fl_act)

{
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	enum mlx5_flow_namespace_type ns_type;
	struct mlx5e_tc_act *act;

	if (esw && esw->mode == MLX5_ESWITCH_OFFLOADS)
		ns_type = MLX5_FLOW_NAMESPACE_FDB;
	else
		ns_type = MLX5_FLOW_NAMESPACE_KERNEL;

	act = mlx5e_tc_act_get(fl_act->id, ns_type);
	if (!act || !act->stats_action)
		return mlx5e_tc_fill_action_stats(priv, fl_act);

	return act->stats_action(priv, fl_act);
}

static int
mlx5e_rep_indr_setup_act(struct mlx5e_rep_priv *rpriv,
			 struct flow_offload_action *fl_act)
{
	switch (fl_act->command) {
	case FLOW_ACT_REPLACE:
		return mlx5e_rep_indr_replace_act(rpriv, fl_act);
	case FLOW_ACT_DESTROY:
		return mlx5e_rep_indr_destroy_act(rpriv, fl_act);
	case FLOW_ACT_STATS:
		return mlx5e_rep_indr_stats_act(rpriv, fl_act);
	default:
		return -EOPNOTSUPP;
	}
}

static int
mlx5e_rep_indr_no_dev_setup(struct mlx5e_rep_priv *rpriv,
			    enum tc_setup_type type,
			    void *data)
{
	if (!data)
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_ACT:
		return mlx5e_rep_indr_setup_act(rpriv, data);
	default:
		return -EOPNOTSUPP;
	}
}

static
int mlx5e_rep_indr_setup_cb(struct net_device *netdev, struct Qdisc *sch, void *cb_priv,
			    enum tc_setup_type type, void *type_data,
			    void *data,
			    void (*cleanup)(struct flow_block_cb *block_cb))
{
	if (!netdev)
		return mlx5e_rep_indr_no_dev_setup(cb_priv, type, data);

	switch (type) {
	case TC_SETUP_BLOCK:
		return mlx5e_rep_indr_setup_block(netdev, sch, cb_priv, type_data,
						  mlx5e_rep_indr_setup_tc_cb,
						  data, cleanup);
	case TC_SETUP_FT:
		return mlx5e_rep_indr_setup_block(netdev, sch, cb_priv, type_data,
						  mlx5e_rep_indr_setup_ft_cb,
						  data, cleanup);
	default:
		return -EOPNOTSUPP;
	}
}

int mlx5e_rep_tc_netdevice_event_register(struct mlx5e_rep_priv *rpriv)
{
	struct mlx5_rep_uplink_priv *uplink_priv = &rpriv->uplink_priv;

	/* init indirect block notifications */
	INIT_LIST_HEAD(&uplink_priv->tc_indr_block_priv_list);

	return flow_indr_dev_register(mlx5e_rep_indr_setup_cb, rpriv);
}

void mlx5e_rep_tc_netdevice_event_unregister(struct mlx5e_rep_priv *rpriv)
{
	flow_indr_dev_unregister(mlx5e_rep_indr_setup_cb, rpriv,
				 mlx5e_rep_indr_block_unbind);
}

void mlx5e_rep_tc_receive(struct mlx5_cqe64 *cqe, struct mlx5e_rq *rq,
			  struct sk_buff *skb)
{
	u32 reg_c0, reg_c1, zone_restore_id, tunnel_id;
	struct mlx5e_tc_update_priv tc_priv = {};
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5_tc_ct_priv *ct_priv;
	struct mapping_ctx *mapping_ctx;
	struct mlx5_eswitch *esw;
	struct mlx5e_priv *priv;

	reg_c0 = (be32_to_cpu(cqe->sop_drop_qpn) & MLX5E_TC_FLOW_ID_MASK);
	if (!reg_c0 || reg_c0 == MLX5_FS_DEFAULT_FLOW_TAG)
		goto forward;

	/* If mapped_obj_id is not equal to the default flow tag then skb->mark
	 * is not supported and must be reset back to 0.
	 */
	skb->mark = 0;

	priv = netdev_priv(skb->dev);
	esw = priv->mdev->priv.eswitch;
	mapping_ctx = esw->offloads.reg_c0_obj_pool;
	reg_c1 = be32_to_cpu(cqe->ft_metadata);
	zone_restore_id = reg_c1 & ESW_ZONE_ID_MASK;
	tunnel_id = (reg_c1 >> ESW_TUN_OFFSET) & TUNNEL_ID_MASK;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;
	ct_priv = uplink_priv->ct_priv;

	if (!mlx5_ipsec_is_rx_flow(cqe) &&
	    !mlx5e_tc_update_skb(cqe, skb, mapping_ctx, reg_c0, ct_priv, zone_restore_id, tunnel_id,
				 &tc_priv))
		goto free_skb;

forward:
	if (tc_priv.skb_done)
		goto free_skb;

	if (tc_priv.forward_tx)
		dev_queue_xmit(skb);
	else
		napi_gro_receive(rq->cq.napi, skb);

	dev_put(tc_priv.fwd_dev);

	return;

free_skb:
	dev_put(tc_priv.fwd_dev);
	dev_kfree_skb_any(skb);
}
