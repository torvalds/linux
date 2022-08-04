// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies. */

#include <net/dst_metadata.h>
#include <linux/netdevice.h>
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

struct mlx5e_rep_indr_block_priv {
	struct net_device *netdev;
	struct mlx5e_rep_priv *rpriv;

	struct list_head list;
};

int mlx5e_rep_encap_entry_attach(struct mlx5e_priv *priv,
				 struct mlx5e_encap_entry *e)
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
	nhe = mlx5e_rep_neigh_entry_lookup(priv, &e->m_neigh);
	if (!nhe) {
		err = mlx5e_rep_neigh_entry_create(priv, e, &nhe);
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

	/* wait for encap to be fully initialized */
	wait_for_completion(&e->res_ready);

	mutex_lock(&esw->offloads.encap_tbl_lock);
	encap_connected = !!(e->flags & MLX5_ENCAP_ENTRY_VALID);
	if (e->compl_result < 0 || (encap_connected == neigh_connected &&
				    ether_addr_equal(e->h_dest, ha)))
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
	mlx5e_put_encap_flow_list(priv, &flow_list);
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
	err = mlx5e_tc_esw_init(&uplink_priv->tc_ht);
	return err;
}

void mlx5e_rep_tc_cleanup(struct mlx5e_rep_priv *rpriv)
{
	/* delete shared tc flow table */
	mlx5e_tc_esw_cleanup(&rpriv->uplink_priv.tc_ht);
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
				 struct net_device *netdev)
{
	struct mlx5e_rep_indr_block_priv *cb_priv;

	/* All callback list access should be protected by RTNL. */
	ASSERT_RTNL();

	list_for_each_entry(cb_priv,
			    &rpriv->uplink_priv.tc_indr_block_priv_list,
			    list)
		if (cb_priv->netdev == netdev)
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
	unsigned long flags = MLX5_TC_FLAG(EGRESS) | MLX5_TC_FLAG(ESW_OFFLOAD);
	struct mlx5e_rep_indr_block_priv *priv = indr_priv;

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

static int
mlx5e_rep_indr_setup_block(struct net_device *netdev, struct Qdisc *sch,
			   struct mlx5e_rep_priv *rpriv,
			   struct flow_block_offload *f,
			   flow_setup_cb_t *setup_cb,
			   void *data,
			   void (*cleanup)(struct flow_block_cb *block_cb))
{
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);
	struct mlx5e_rep_indr_block_priv *indr_priv;
	struct flow_block_cb *block_cb;

	if (!mlx5e_tc_tun_device_to_offload(priv, netdev) &&
	    !(is_vlan_dev(netdev) && vlan_dev_real_dev(netdev) == rpriv->netdev))
		return -EOPNOTSUPP;

	if (f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	f->unlocked_driver_cb = true;
	f->driver_block_list = &mlx5e_block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		indr_priv = mlx5e_rep_indr_block_priv_lookup(rpriv, netdev);
		if (indr_priv)
			return -EEXIST;

		indr_priv = kmalloc(sizeof(*indr_priv), GFP_KERNEL);
		if (!indr_priv)
			return -ENOMEM;

		indr_priv->netdev = netdev;
		indr_priv->rpriv = rpriv;
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
		indr_priv = mlx5e_rep_indr_block_priv_lookup(rpriv, netdev);
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

static
int mlx5e_rep_indr_setup_cb(struct net_device *netdev, struct Qdisc *sch, void *cb_priv,
			    enum tc_setup_type type, void *type_data,
			    void *data,
			    void (*cleanup)(struct flow_block_cb *block_cb))
{
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

#if IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
static bool mlx5e_restore_tunnel(struct mlx5e_priv *priv, struct sk_buff *skb,
				 struct mlx5e_tc_update_priv *tc_priv,
				 u32 tunnel_id)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct tunnel_match_enc_opts enc_opts = {};
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct metadata_dst *tun_dst;
	struct tunnel_match_key key;
	u32 tun_id, enc_opts_id;
	struct net_device *dev;
	int err;

	enc_opts_id = tunnel_id & ENC_OPTS_BITS_MASK;
	tun_id = tunnel_id >> ENC_OPTS_BITS;

	if (!tun_id)
		return true;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;

	err = mapping_find(uplink_priv->tunnel_mapping, tun_id, &key);
	if (err) {
		WARN_ON_ONCE(true);
		netdev_dbg(priv->netdev,
			   "Couldn't find tunnel for tun_id: %d, err: %d\n",
			   tun_id, err);
		return false;
	}

	if (enc_opts_id) {
		err = mapping_find(uplink_priv->tunnel_enc_opts_mapping,
				   enc_opts_id, &enc_opts);
		if (err) {
			netdev_dbg(priv->netdev,
				   "Couldn't find tunnel (opts) for tun_id: %d, err: %d\n",
				   enc_opts_id, err);
			return false;
		}
	}

	if (key.enc_control.addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		tun_dst = __ip_tun_set_dst(key.enc_ipv4.src, key.enc_ipv4.dst,
					   key.enc_ip.tos, key.enc_ip.ttl,
					   key.enc_tp.dst, TUNNEL_KEY,
					   key32_to_tunnel_id(key.enc_key_id.keyid),
					   enc_opts.key.len);
	} else if (key.enc_control.addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		tun_dst = __ipv6_tun_set_dst(&key.enc_ipv6.src, &key.enc_ipv6.dst,
					     key.enc_ip.tos, key.enc_ip.ttl,
					     key.enc_tp.dst, 0, TUNNEL_KEY,
					     key32_to_tunnel_id(key.enc_key_id.keyid),
					     enc_opts.key.len);
	} else {
		netdev_dbg(priv->netdev,
			   "Couldn't restore tunnel, unsupported addr_type: %d\n",
			   key.enc_control.addr_type);
		return false;
	}

	if (!tun_dst) {
		netdev_dbg(priv->netdev, "Couldn't restore tunnel, no tun_dst\n");
		return false;
	}

	tun_dst->u.tun_info.key.tp_src = key.enc_tp.src;

	if (enc_opts.key.len)
		ip_tunnel_info_opts_set(&tun_dst->u.tun_info,
					enc_opts.key.data,
					enc_opts.key.len,
					enc_opts.key.dst_opt_type);

	skb_dst_set(skb, (struct dst_entry *)tun_dst);
	dev = dev_get_by_index(&init_net, key.filter_ifindex);
	if (!dev) {
		netdev_dbg(priv->netdev,
			   "Couldn't find tunnel device with ifindex: %d\n",
			   key.filter_ifindex);
		return false;
	}

	/* Set tun_dev so we do dev_put() after datapath */
	tc_priv->tun_dev = dev;

	skb->dev = dev;

	return true;
}
#endif /* CONFIG_NET_TC_SKB_EXT */

bool mlx5e_rep_tc_update_skb(struct mlx5_cqe64 *cqe,
			     struct sk_buff *skb,
			     struct mlx5e_tc_update_priv *tc_priv)
{
#if IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
	u32 chain = 0, reg_c0, reg_c1, tunnel_id, zone_restore_id;
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct tc_skb_ext *tc_skb_ext;
	struct mlx5_eswitch *esw;
	struct mlx5e_priv *priv;
	int err;

	reg_c0 = (be32_to_cpu(cqe->sop_drop_qpn) & MLX5E_TC_FLOW_ID_MASK);
	if (reg_c0 == MLX5_FS_DEFAULT_FLOW_TAG)
		reg_c0 = 0;
	reg_c1 = be32_to_cpu(cqe->ft_metadata);

	if (!reg_c0)
		return true;

	/* If reg_c0 is not equal to the default flow tag then skb->mark
	 * is not supported and must be reset back to 0.
	 */
	skb->mark = 0;

	priv = netdev_priv(skb->dev);
	esw = priv->mdev->priv.eswitch;

	err = mlx5_get_chain_for_tag(esw_chains(esw), reg_c0, &chain);
	if (err) {
		netdev_dbg(priv->netdev,
			   "Couldn't find chain for chain tag: %d, err: %d\n",
			   reg_c0, err);
		return false;
	}

	if (chain) {
		tc_skb_ext = skb_ext_add(skb, TC_SKB_EXT);
		if (!tc_skb_ext) {
			WARN_ON(1);
			return false;
		}

		tc_skb_ext->chain = chain;

		zone_restore_id = reg_c1 & ZONE_RESTORE_MAX;

		uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
		uplink_priv = &uplink_rpriv->uplink_priv;
		if (!mlx5e_tc_ct_restore_flow(uplink_priv->ct_priv, skb,
					      zone_restore_id))
			return false;
	}

	tunnel_id = reg_c1 >> REG_MAPPING_SHIFT(TUNNEL_TO_REG);
	return mlx5e_restore_tunnel(priv, skb, tc_priv, tunnel_id);
#endif /* CONFIG_NET_TC_SKB_EXT */

	return true;
}

void mlx5_rep_tc_post_napi_receive(struct mlx5e_tc_update_priv *tc_priv)
{
	if (tc_priv->tun_dev)
		dev_put(tc_priv->tun_dev);
}
