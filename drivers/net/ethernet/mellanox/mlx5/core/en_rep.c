/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <generated/utsrelease.h>
#include <linux/mlx5/fs.h>
#include <net/switchdev.h>
#include <net/pkt_cls.h>
#include <net/act_api.h>
#include <net/netevent.h>
#include <net/arp.h>

#include "eswitch.h"
#include "en.h"
#include "en_rep.h"
#include "en_tc.h"
#include "fs_core.h"

static const char mlx5e_rep_driver_name[] = "mlx5e_rep";

static void mlx5e_rep_get_drvinfo(struct net_device *dev,
				  struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, mlx5e_rep_driver_name,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, UTS_RELEASE, sizeof(drvinfo->version));
}

static const struct counter_desc sw_rep_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_bytes) },
};

#define NUM_VPORT_REP_COUNTERS	ARRAY_SIZE(sw_rep_stats_desc)

static void mlx5e_rep_get_strings(struct net_device *dev,
				  u32 stringset, uint8_t *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < NUM_VPORT_REP_COUNTERS; i++)
			strcpy(data + (i * ETH_GSTRING_LEN),
			       sw_rep_stats_desc[i].format);
		break;
	}
}

static void mlx5e_rep_update_hw_counters(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct rtnl_link_stats64 *vport_stats;
	struct ifla_vf_stats vf_stats;
	int err;

	err = mlx5_eswitch_get_vport_stats(esw, rep->vport, &vf_stats);
	if (err) {
		pr_warn("vport %d error %d reading stats\n", rep->vport, err);
		return;
	}

	vport_stats = &priv->stats.vf_vport;
	/* flip tx/rx as we are reporting the counters for the switch vport */
	vport_stats->rx_packets = vf_stats.tx_packets;
	vport_stats->rx_bytes   = vf_stats.tx_bytes;
	vport_stats->tx_packets = vf_stats.rx_packets;
	vport_stats->tx_bytes   = vf_stats.rx_bytes;
}

static void mlx5e_rep_update_sw_counters(struct mlx5e_priv *priv)
{
	struct mlx5e_sw_stats *s = &priv->stats.sw;
	struct mlx5e_rq_stats *rq_stats;
	struct mlx5e_sq_stats *sq_stats;
	int i, j;

	memset(s, 0, sizeof(*s));
	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_channel *c = priv->channels.c[i];

		rq_stats = &c->rq.stats;

		s->rx_packets	+= rq_stats->packets;
		s->rx_bytes	+= rq_stats->bytes;

		for (j = 0; j < priv->channels.params.num_tc; j++) {
			sq_stats = &c->sq[j].stats;

			s->tx_packets		+= sq_stats->packets;
			s->tx_bytes		+= sq_stats->bytes;
		}
	}
}

static void mlx5e_rep_update_stats(struct mlx5e_priv *priv)
{
	mlx5e_rep_update_sw_counters(priv);
	mlx5e_rep_update_hw_counters(priv);
}

static void mlx5e_rep_get_ethtool_stats(struct net_device *dev,
					struct ethtool_stats *stats, u64 *data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int i;

	if (!data)
		return;

	mutex_lock(&priv->state_lock);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		mlx5e_rep_update_sw_counters(priv);
	mutex_unlock(&priv->state_lock);

	for (i = 0; i < NUM_VPORT_REP_COUNTERS; i++)
		data[i] = MLX5E_READ_CTR64_CPU(&priv->stats.sw,
					       sw_rep_stats_desc, i);
}

static int mlx5e_rep_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return NUM_VPORT_REP_COUNTERS;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ethtool_ops mlx5e_rep_ethtool_ops = {
	.get_drvinfo	   = mlx5e_rep_get_drvinfo,
	.get_link	   = ethtool_op_get_link,
	.get_strings       = mlx5e_rep_get_strings,
	.get_sset_count    = mlx5e_rep_get_sset_count,
	.get_ethtool_stats = mlx5e_rep_get_ethtool_stats,
};

int mlx5e_attr_get(struct net_device *dev, struct switchdev_attr *attr)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	if (esw->mode == SRIOV_NONE)
		return -EOPNOTSUPP;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID:
		attr->u.ppid.id_len = ETH_ALEN;
		ether_addr_copy(attr->u.ppid.id, rep->hw_id);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void mlx5e_sqs2vport_stop(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_sq *rep_sq, *tmp;
	struct mlx5e_rep_priv *rpriv;

	if (esw->mode != SRIOV_OFFLOADS)
		return;

	rpriv = mlx5e_rep_to_rep_priv(rep);
	list_for_each_entry_safe(rep_sq, tmp, &rpriv->vport_sqs_list, list) {
		mlx5_eswitch_del_send_to_vport_rule(rep_sq->send_to_vport_rule);
		list_del(&rep_sq->list);
		kfree(rep_sq);
	}
}

static int mlx5e_sqs2vport_start(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep,
				 u32 *sqns_array, int sqns_num)
{
	struct mlx5_flow_handle *flow_rule;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5e_rep_sq *rep_sq;
	int err;
	int i;

	if (esw->mode != SRIOV_OFFLOADS)
		return 0;

	rpriv = mlx5e_rep_to_rep_priv(rep);
	for (i = 0; i < sqns_num; i++) {
		rep_sq = kzalloc(sizeof(*rep_sq), GFP_KERNEL);
		if (!rep_sq) {
			err = -ENOMEM;
			goto out_err;
		}

		/* Add re-inject rule to the PF/representor sqs */
		flow_rule = mlx5_eswitch_add_send_to_vport_rule(esw,
								rep->vport,
								sqns_array[i]);
		if (IS_ERR(flow_rule)) {
			err = PTR_ERR(flow_rule);
			kfree(rep_sq);
			goto out_err;
		}
		rep_sq->send_to_vport_rule = flow_rule;
		list_add(&rep_sq->list, &rpriv->vport_sqs_list);
	}
	return 0;

out_err:
	mlx5e_sqs2vport_stop(esw, rep);
	return err;
}

int mlx5e_add_sqs_fwd_rules(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct mlx5e_channel *c;
	int n, tc, num_sqs = 0;
	int err = -ENOMEM;
	u32 *sqs;

	sqs = kcalloc(priv->channels.num * priv->channels.params.num_tc, sizeof(*sqs), GFP_KERNEL);
	if (!sqs)
		goto out;

	for (n = 0; n < priv->channels.num; n++) {
		c = priv->channels.c[n];
		for (tc = 0; tc < c->num_tc; tc++)
			sqs[num_sqs++] = c->sq[tc].sqn;
	}

	err = mlx5e_sqs2vport_start(esw, rep, sqs, num_sqs);
	kfree(sqs);

out:
	if (err)
		netdev_warn(priv->netdev, "Failed to add SQs FWD rules %d\n", err);
	return err;
}

void mlx5e_remove_sqs_fwd_rules(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;

	mlx5e_sqs2vport_stop(esw, rep);
}

static void mlx5e_rep_neigh_update_init_interval(struct mlx5e_rep_priv *rpriv)
{
#if IS_ENABLED(CONFIG_IPV6)
	unsigned long ipv6_interval = NEIGH_VAR(&ipv6_stub->nd_tbl->parms,
						DELAY_PROBE_TIME);
#else
	unsigned long ipv6_interval = ~0UL;
#endif
	unsigned long ipv4_interval = NEIGH_VAR(&arp_tbl.parms,
						DELAY_PROBE_TIME);
	struct net_device *netdev = rpriv->netdev;
	struct mlx5e_priv *priv = netdev_priv(netdev);

	rpriv->neigh_update.min_interval = min_t(unsigned long, ipv6_interval, ipv4_interval);
	mlx5_fc_update_sampling_interval(priv->mdev, rpriv->neigh_update.min_interval);
}

void mlx5e_rep_queue_neigh_stats_work(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5e_neigh_update_table *neigh_update = &rpriv->neigh_update;

	mlx5_fc_queue_stats_work(priv->mdev,
				 &neigh_update->neigh_stats_work,
				 neigh_update->min_interval);
}

static void mlx5e_rep_neigh_stats_work(struct work_struct *work)
{
	struct mlx5e_rep_priv *rpriv = container_of(work, struct mlx5e_rep_priv,
						    neigh_update.neigh_stats_work.work);
	struct net_device *netdev = rpriv->netdev;
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_neigh_hash_entry *nhe;

	rtnl_lock();
	if (!list_empty(&rpriv->neigh_update.neigh_list))
		mlx5e_rep_queue_neigh_stats_work(priv);

	list_for_each_entry(nhe, &rpriv->neigh_update.neigh_list, neigh_list)
		mlx5e_tc_update_neigh_used_value(nhe);

	rtnl_unlock();
}

static void mlx5e_rep_neigh_entry_hold(struct mlx5e_neigh_hash_entry *nhe)
{
	refcount_inc(&nhe->refcnt);
}

static void mlx5e_rep_neigh_entry_release(struct mlx5e_neigh_hash_entry *nhe)
{
	if (refcount_dec_and_test(&nhe->refcnt))
		kfree(nhe);
}

static void mlx5e_rep_update_flows(struct mlx5e_priv *priv,
				   struct mlx5e_encap_entry *e,
				   bool neigh_connected,
				   unsigned char ha[ETH_ALEN])
{
	struct ethhdr *eth = (struct ethhdr *)e->encap_header;

	ASSERT_RTNL();

	if ((!neigh_connected && (e->flags & MLX5_ENCAP_ENTRY_VALID)) ||
	    !ether_addr_equal(e->h_dest, ha))
		mlx5e_tc_encap_flows_del(priv, e);

	if (neigh_connected && !(e->flags & MLX5_ENCAP_ENTRY_VALID)) {
		ether_addr_copy(e->h_dest, ha);
		ether_addr_copy(eth->h_dest, ha);

		mlx5e_tc_encap_flows_add(priv, e);
	}
}

static void mlx5e_rep_neigh_update(struct work_struct *work)
{
	struct mlx5e_neigh_hash_entry *nhe =
		container_of(work, struct mlx5e_neigh_hash_entry, neigh_update_work);
	struct neighbour *n = nhe->n;
	struct mlx5e_encap_entry *e;
	unsigned char ha[ETH_ALEN];
	struct mlx5e_priv *priv;
	bool neigh_connected;
	bool encap_connected;
	u8 nud_state, dead;

	rtnl_lock();

	/* If these parameters are changed after we release the lock,
	 * we'll receive another event letting us know about it.
	 * We use this lock to avoid inconsistency between the neigh validity
	 * and it's hw address.
	 */
	read_lock_bh(&n->lock);
	memcpy(ha, n->ha, ETH_ALEN);
	nud_state = n->nud_state;
	dead = n->dead;
	read_unlock_bh(&n->lock);

	neigh_connected = (nud_state & NUD_VALID) && !dead;

	list_for_each_entry(e, &nhe->encap_list, encap_list) {
		encap_connected = !!(e->flags & MLX5_ENCAP_ENTRY_VALID);
		priv = netdev_priv(e->out_dev);

		if (encap_connected != neigh_connected ||
		    !ether_addr_equal(e->h_dest, ha))
			mlx5e_rep_update_flows(priv, e, neigh_connected, ha);
	}
	mlx5e_rep_neigh_entry_release(nhe);
	rtnl_unlock();
	neigh_release(n);
}

static struct mlx5e_neigh_hash_entry *
mlx5e_rep_neigh_entry_lookup(struct mlx5e_priv *priv,
			     struct mlx5e_neigh *m_neigh);

static int mlx5e_rep_netevent_event(struct notifier_block *nb,
				    unsigned long event, void *ptr)
{
	struct mlx5e_rep_priv *rpriv = container_of(nb, struct mlx5e_rep_priv,
						    neigh_update.netevent_nb);
	struct mlx5e_neigh_update_table *neigh_update = &rpriv->neigh_update;
	struct net_device *netdev = rpriv->netdev;
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_neigh_hash_entry *nhe = NULL;
	struct mlx5e_neigh m_neigh = {};
	struct neigh_parms *p;
	struct neighbour *n;
	bool found = false;

	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		n = ptr;
#if IS_ENABLED(CONFIG_IPV6)
		if (n->tbl != ipv6_stub->nd_tbl && n->tbl != &arp_tbl)
#else
		if (n->tbl != &arp_tbl)
#endif
			return NOTIFY_DONE;

		m_neigh.dev = n->dev;
		m_neigh.family = n->ops->family;
		memcpy(&m_neigh.dst_ip, n->primary_key, n->tbl->key_len);

		/* We are in atomic context and can't take RTNL mutex, so use
		 * spin_lock_bh to lookup the neigh table. bh is used since
		 * netevent can be called from a softirq context.
		 */
		spin_lock_bh(&neigh_update->encap_lock);
		nhe = mlx5e_rep_neigh_entry_lookup(priv, &m_neigh);
		if (!nhe) {
			spin_unlock_bh(&neigh_update->encap_lock);
			return NOTIFY_DONE;
		}

		/* This assignment is valid as long as the the neigh reference
		 * is taken
		 */
		nhe->n = n;

		/* Take a reference to ensure the neighbour and mlx5 encap
		 * entry won't be destructed until we drop the reference in
		 * delayed work.
		 */
		neigh_hold(n);
		mlx5e_rep_neigh_entry_hold(nhe);

		if (!queue_work(priv->wq, &nhe->neigh_update_work)) {
			mlx5e_rep_neigh_entry_release(nhe);
			neigh_release(n);
		}
		spin_unlock_bh(&neigh_update->encap_lock);
		break;

	case NETEVENT_DELAY_PROBE_TIME_UPDATE:
		p = ptr;

		/* We check the device is present since we don't care about
		 * changes in the default table, we only care about changes
		 * done per device delay prob time parameter.
		 */
#if IS_ENABLED(CONFIG_IPV6)
		if (!p->dev || (p->tbl != ipv6_stub->nd_tbl && p->tbl != &arp_tbl))
#else
		if (!p->dev || p->tbl != &arp_tbl)
#endif
			return NOTIFY_DONE;

		/* We are in atomic context and can't take RTNL mutex,
		 * so use spin_lock_bh to walk the neigh list and look for
		 * the relevant device. bh is used since netevent can be
		 * called from a softirq context.
		 */
		spin_lock_bh(&neigh_update->encap_lock);
		list_for_each_entry(nhe, &neigh_update->neigh_list, neigh_list) {
			if (p->dev == nhe->m_neigh.dev) {
				found = true;
				break;
			}
		}
		spin_unlock_bh(&neigh_update->encap_lock);
		if (!found)
			return NOTIFY_DONE;

		neigh_update->min_interval = min_t(unsigned long,
						   NEIGH_VAR(p, DELAY_PROBE_TIME),
						   neigh_update->min_interval);
		mlx5_fc_update_sampling_interval(priv->mdev,
						 neigh_update->min_interval);
		break;
	}
	return NOTIFY_DONE;
}

static const struct rhashtable_params mlx5e_neigh_ht_params = {
	.head_offset = offsetof(struct mlx5e_neigh_hash_entry, rhash_node),
	.key_offset = offsetof(struct mlx5e_neigh_hash_entry, m_neigh),
	.key_len = sizeof(struct mlx5e_neigh),
	.automatic_shrinking = true,
};

static int mlx5e_rep_neigh_init(struct mlx5e_rep_priv *rpriv)
{
	struct mlx5e_neigh_update_table *neigh_update = &rpriv->neigh_update;
	int err;

	err = rhashtable_init(&neigh_update->neigh_ht, &mlx5e_neigh_ht_params);
	if (err)
		return err;

	INIT_LIST_HEAD(&neigh_update->neigh_list);
	spin_lock_init(&neigh_update->encap_lock);
	INIT_DELAYED_WORK(&neigh_update->neigh_stats_work,
			  mlx5e_rep_neigh_stats_work);
	mlx5e_rep_neigh_update_init_interval(rpriv);

	rpriv->neigh_update.netevent_nb.notifier_call = mlx5e_rep_netevent_event;
	err = register_netevent_notifier(&rpriv->neigh_update.netevent_nb);
	if (err)
		goto out_err;
	return 0;

out_err:
	rhashtable_destroy(&neigh_update->neigh_ht);
	return err;
}

static void mlx5e_rep_neigh_cleanup(struct mlx5e_rep_priv *rpriv)
{
	struct mlx5e_neigh_update_table *neigh_update = &rpriv->neigh_update;
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);

	unregister_netevent_notifier(&neigh_update->netevent_nb);

	flush_workqueue(priv->wq); /* flush neigh update works */

	cancel_delayed_work_sync(&rpriv->neigh_update.neigh_stats_work);

	rhashtable_destroy(&neigh_update->neigh_ht);
}

static int mlx5e_rep_neigh_entry_insert(struct mlx5e_priv *priv,
					struct mlx5e_neigh_hash_entry *nhe)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	int err;

	err = rhashtable_insert_fast(&rpriv->neigh_update.neigh_ht,
				     &nhe->rhash_node,
				     mlx5e_neigh_ht_params);
	if (err)
		return err;

	list_add(&nhe->neigh_list, &rpriv->neigh_update.neigh_list);

	return err;
}

static void mlx5e_rep_neigh_entry_remove(struct mlx5e_priv *priv,
					 struct mlx5e_neigh_hash_entry *nhe)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	spin_lock_bh(&rpriv->neigh_update.encap_lock);

	list_del(&nhe->neigh_list);

	rhashtable_remove_fast(&rpriv->neigh_update.neigh_ht,
			       &nhe->rhash_node,
			       mlx5e_neigh_ht_params);
	spin_unlock_bh(&rpriv->neigh_update.encap_lock);
}

/* This function must only be called under RTNL lock or under the
 * representor's encap_lock in case RTNL mutex can't be held.
 */
static struct mlx5e_neigh_hash_entry *
mlx5e_rep_neigh_entry_lookup(struct mlx5e_priv *priv,
			     struct mlx5e_neigh *m_neigh)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5e_neigh_update_table *neigh_update = &rpriv->neigh_update;

	return rhashtable_lookup_fast(&neigh_update->neigh_ht, m_neigh,
				      mlx5e_neigh_ht_params);
}

static int mlx5e_rep_neigh_entry_create(struct mlx5e_priv *priv,
					struct mlx5e_encap_entry *e,
					struct mlx5e_neigh_hash_entry **nhe)
{
	int err;

	*nhe = kzalloc(sizeof(**nhe), GFP_KERNEL);
	if (!*nhe)
		return -ENOMEM;

	memcpy(&(*nhe)->m_neigh, &e->m_neigh, sizeof(e->m_neigh));
	INIT_WORK(&(*nhe)->neigh_update_work, mlx5e_rep_neigh_update);
	INIT_LIST_HEAD(&(*nhe)->encap_list);
	refcount_set(&(*nhe)->refcnt, 1);

	err = mlx5e_rep_neigh_entry_insert(priv, *nhe);
	if (err)
		goto out_free;
	return 0;

out_free:
	kfree(*nhe);
	return err;
}

static void mlx5e_rep_neigh_entry_destroy(struct mlx5e_priv *priv,
					  struct mlx5e_neigh_hash_entry *nhe)
{
	/* The neigh hash entry must be removed from the hash table regardless
	 * of the reference count value, so it won't be found by the next
	 * neigh notification call. The neigh hash entry reference count is
	 * incremented only during creation and neigh notification calls and
	 * protects from freeing the nhe struct.
	 */
	mlx5e_rep_neigh_entry_remove(priv, nhe);
	mlx5e_rep_neigh_entry_release(nhe);
}

int mlx5e_rep_encap_entry_attach(struct mlx5e_priv *priv,
				 struct mlx5e_encap_entry *e)
{
	struct mlx5e_neigh_hash_entry *nhe;
	int err;

	nhe = mlx5e_rep_neigh_entry_lookup(priv, &e->m_neigh);
	if (!nhe) {
		err = mlx5e_rep_neigh_entry_create(priv, e, &nhe);
		if (err)
			return err;
	}
	list_add(&e->encap_list, &nhe->encap_list);
	return 0;
}

void mlx5e_rep_encap_entry_detach(struct mlx5e_priv *priv,
				  struct mlx5e_encap_entry *e)
{
	struct mlx5e_neigh_hash_entry *nhe;

	list_del(&e->encap_list);
	nhe = mlx5e_rep_neigh_entry_lookup(priv, &e->m_neigh);

	if (list_empty(&nhe->encap_list))
		mlx5e_rep_neigh_entry_destroy(priv, nhe);
}

static int mlx5e_rep_open(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	int err;

	mutex_lock(&priv->state_lock);
	err = mlx5e_open_locked(dev);
	if (err)
		goto unlock;

	if (!mlx5_modify_vport_admin_state(priv->mdev,
			MLX5_QUERY_VPORT_STATE_IN_OP_MOD_ESW_VPORT,
			rep->vport, MLX5_ESW_VPORT_ADMIN_STATE_UP))
		netif_carrier_on(dev);

unlock:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_rep_close(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	int ret;

	mutex_lock(&priv->state_lock);
	mlx5_modify_vport_admin_state(priv->mdev,
			MLX5_QUERY_VPORT_STATE_IN_OP_MOD_ESW_VPORT,
			rep->vport, MLX5_ESW_VPORT_ADMIN_STATE_DOWN);
	ret = mlx5e_close_locked(dev);
	mutex_unlock(&priv->state_lock);
	return ret;
}

static int mlx5e_rep_get_phys_port_name(struct net_device *dev,
					char *buf, size_t len)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	int ret;

	ret = snprintf(buf, len, "%d", rep->vport - 1);
	if (ret >= len)
		return -EOPNOTSUPP;

	return 0;
}

static int
mlx5e_rep_setup_tc_cls_flower(struct mlx5e_priv *priv,
			      struct tc_cls_flower_offload *cls_flower)
{
	switch (cls_flower->command) {
	case TC_CLSFLOWER_REPLACE:
		return mlx5e_configure_flower(priv, cls_flower);
	case TC_CLSFLOWER_DESTROY:
		return mlx5e_delete_flower(priv, cls_flower);
	case TC_CLSFLOWER_STATS:
		return mlx5e_stats_flower(priv, cls_flower);
	default:
		return -EOPNOTSUPP;
	}
}

static int mlx5e_rep_setup_tc_cb(enum tc_setup_type type, void *type_data,
				 void *cb_priv)
{
	struct mlx5e_priv *priv = cb_priv;

	if (!tc_cls_can_offload_and_chain0(priv->netdev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return mlx5e_rep_setup_tc_cls_flower(priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int mlx5e_rep_setup_tc_block(struct net_device *dev,
				    struct tc_block_offload *f)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	if (f->binder_type != TCF_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	switch (f->command) {
	case TC_BLOCK_BIND:
		return tcf_block_cb_register(f->block, mlx5e_rep_setup_tc_cb,
					     priv, priv);
	case TC_BLOCK_UNBIND:
		tcf_block_cb_unregister(f->block, mlx5e_rep_setup_tc_cb, priv);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int mlx5e_rep_setup_tc(struct net_device *dev, enum tc_setup_type type,
			      void *type_data)
{
	switch (type) {
	case TC_SETUP_BLOCK:
		return mlx5e_rep_setup_tc_block(dev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

bool mlx5e_is_uplink_rep(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep;

	if (!MLX5_CAP_GEN(priv->mdev, vport_group_manager))
		return false;

	rep = rpriv->rep;
	if (esw->mode == SRIOV_OFFLOADS &&
	    rep && rep->vport == FDB_UPLINK_VPORT)
		return true;

	return false;
}

static bool mlx5e_is_vf_vport_rep(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;

	if (rep && rep->vport != FDB_UPLINK_VPORT)
		return true;

	return false;
}

bool mlx5e_has_offload_stats(const struct net_device *dev, int attr_id)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		if (mlx5e_is_vf_vport_rep(priv) || mlx5e_is_uplink_rep(priv))
			return true;
	}

	return false;
}

static int
mlx5e_get_sw_stats64(const struct net_device *dev,
		     struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_sw_stats *sstats = &priv->stats.sw;

	stats->rx_packets = sstats->rx_packets;
	stats->rx_bytes   = sstats->rx_bytes;
	stats->tx_packets = sstats->tx_packets;
	stats->tx_bytes   = sstats->tx_bytes;

	stats->tx_dropped = sstats->tx_queue_dropped;

	return 0;
}

int mlx5e_get_offload_stats(int attr_id, const struct net_device *dev,
			    void *sp)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		return mlx5e_get_sw_stats64(dev, sp);
	}

	return -EINVAL;
}

static void
mlx5e_rep_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	memcpy(stats, &priv->stats.vf_vport, sizeof(*stats));
}

static const struct switchdev_ops mlx5e_rep_switchdev_ops = {
	.switchdev_port_attr_get	= mlx5e_attr_get,
};

static const struct net_device_ops mlx5e_netdev_ops_rep = {
	.ndo_open                = mlx5e_rep_open,
	.ndo_stop                = mlx5e_rep_close,
	.ndo_start_xmit          = mlx5e_xmit,
	.ndo_get_phys_port_name  = mlx5e_rep_get_phys_port_name,
	.ndo_setup_tc            = mlx5e_rep_setup_tc,
	.ndo_get_stats64         = mlx5e_rep_get_stats,
	.ndo_has_offload_stats	 = mlx5e_has_offload_stats,
	.ndo_get_offload_stats	 = mlx5e_get_offload_stats,
};

static void mlx5e_build_rep_params(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params)
{
	u8 cq_period_mode = MLX5_CAP_GEN(mdev, cq_period_start_from_cqe) ?
					 MLX5_CQ_PERIOD_MODE_START_FROM_CQE :
					 MLX5_CQ_PERIOD_MODE_START_FROM_EQE;

	params->log_sq_size = MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE;
	params->rq_wq_type  = MLX5_WQ_TYPE_LINKED_LIST;
	params->log_rq_size = MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE;

	params->rx_dim_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	mlx5e_set_rx_cq_mode_params(params, cq_period_mode);

	params->tx_max_inline         = mlx5e_get_max_inline_cap(mdev);
	params->num_tc                = 1;
	params->lro_wqe_sz            = MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ;

	mlx5_query_min_inline(mdev, &params->tx_min_inline_mode);
}

static void mlx5e_build_rep_netdev(struct net_device *netdev)
{
	netdev->netdev_ops = &mlx5e_netdev_ops_rep;

	netdev->watchdog_timeo    = 15 * HZ;

	netdev->ethtool_ops	  = &mlx5e_rep_ethtool_ops;

#ifdef CONFIG_NET_SWITCHDEV
	netdev->switchdev_ops = &mlx5e_rep_switchdev_ops;
#endif

	netdev->features	 |= NETIF_F_VLAN_CHALLENGED | NETIF_F_HW_TC | NETIF_F_NETNS_LOCAL;
	netdev->hw_features      |= NETIF_F_HW_TC;

	eth_hw_addr_random(netdev);
}

static void mlx5e_init_rep(struct mlx5_core_dev *mdev,
			   struct net_device *netdev,
			   const struct mlx5e_profile *profile,
			   void *ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	priv->mdev                         = mdev;
	priv->netdev                       = netdev;
	priv->profile                      = profile;
	priv->ppriv                        = ppriv;

	mutex_init(&priv->state_lock);

	INIT_DELAYED_WORK(&priv->update_stats_work, mlx5e_update_stats_work);

	priv->channels.params.num_channels = profile->max_nch(mdev);

	priv->hard_mtu = MLX5E_ETH_HARD_MTU;

	mlx5e_build_rep_params(mdev, &priv->channels.params);
	mlx5e_build_rep_netdev(netdev);

	mlx5e_timestamp_init(priv);
}

static int mlx5e_init_rep_rx(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct mlx5_flow_handle *flow_rule;
	int err;

	mlx5e_init_l2_addr(priv);

	err = mlx5e_create_direct_rqts(priv);
	if (err)
		return err;

	err = mlx5e_create_direct_tirs(priv);
	if (err)
		goto err_destroy_direct_rqts;

	flow_rule = mlx5_eswitch_create_vport_rx_rule(esw,
						      rep->vport,
						      priv->direct_tir[0].tirn);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		goto err_destroy_direct_tirs;
	}
	rpriv->vport_rx_rule = flow_rule;

	err = mlx5e_tc_init(priv);
	if (err)
		goto err_del_flow_rule;

	return 0;

err_del_flow_rule:
	mlx5_del_flow_rules(rpriv->vport_rx_rule);
err_destroy_direct_tirs:
	mlx5e_destroy_direct_tirs(priv);
err_destroy_direct_rqts:
	mlx5e_destroy_direct_rqts(priv);
	return err;
}

static void mlx5e_cleanup_rep_rx(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	mlx5e_tc_cleanup(priv);
	mlx5_del_flow_rules(rpriv->vport_rx_rule);
	mlx5e_destroy_direct_tirs(priv);
	mlx5e_destroy_direct_rqts(priv);
}

static int mlx5e_init_rep_tx(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_create_tises(priv);
	if (err) {
		mlx5_core_warn(priv->mdev, "create tises failed, %d\n", err);
		return err;
	}
	return 0;
}

static int mlx5e_get_rep_max_num_channels(struct mlx5_core_dev *mdev)
{
#define	MLX5E_PORT_REPRESENTOR_NCH 1
	return MLX5E_PORT_REPRESENTOR_NCH;
}

static const struct mlx5e_profile mlx5e_rep_profile = {
	.init			= mlx5e_init_rep,
	.init_rx		= mlx5e_init_rep_rx,
	.cleanup_rx		= mlx5e_cleanup_rep_rx,
	.init_tx		= mlx5e_init_rep_tx,
	.cleanup_tx		= mlx5e_cleanup_nic_tx,
	.update_stats           = mlx5e_rep_update_stats,
	.max_nch		= mlx5e_get_rep_max_num_channels,
	.update_carrier		= NULL,
	.rx_handlers.handle_rx_cqe       = mlx5e_handle_rx_cqe_rep,
	.rx_handlers.handle_rx_cqe_mpwqe = NULL /* Not supported */,
	.max_tc			= 1,
};

/* e-Switch vport representors */

static int
mlx5e_nic_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_priv *rpriv = mlx5e_rep_to_rep_priv(rep);
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);

	int err;

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		err = mlx5e_add_sqs_fwd_rules(priv);
		if (err)
			return err;
	}

	err = mlx5e_rep_neigh_init(rpriv);
	if (err)
		goto err_remove_sqs;

	return 0;

err_remove_sqs:
	mlx5e_remove_sqs_fwd_rules(priv);
	return err;
}

static void
mlx5e_nic_rep_unload(struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_priv *rpriv = mlx5e_rep_to_rep_priv(rep);
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);

	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		mlx5e_remove_sqs_fwd_rules(priv);

	/* clean (and re-init) existing uplink offloaded TC rules */
	mlx5e_tc_cleanup(priv);
	mlx5e_tc_init(priv);

	mlx5e_rep_neigh_cleanup(rpriv);
}

static int
mlx5e_vport_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5e_rep_priv *rpriv;
	struct net_device *netdev;
	struct mlx5e_priv *upriv;
	int err;

	rpriv = kzalloc(sizeof(*rpriv), GFP_KERNEL);
	if (!rpriv)
		return -ENOMEM;

	netdev = mlx5e_create_netdev(dev, &mlx5e_rep_profile, rpriv);
	if (!netdev) {
		pr_warn("Failed to create representor netdev for vport %d\n",
			rep->vport);
		kfree(rpriv);
		return -EINVAL;
	}

	rpriv->netdev = netdev;
	rpriv->rep = rep;
	rep->rep_if[REP_ETH].priv = rpriv;
	INIT_LIST_HEAD(&rpriv->vport_sqs_list);

	err = mlx5e_attach_netdev(netdev_priv(netdev));
	if (err) {
		pr_warn("Failed to attach representor netdev for vport %d\n",
			rep->vport);
		goto err_destroy_netdev;
	}

	err = mlx5e_rep_neigh_init(rpriv);
	if (err) {
		pr_warn("Failed to initialized neighbours handling for vport %d\n",
			rep->vport);
		goto err_detach_netdev;
	}

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(dev->priv.eswitch, REP_ETH);
	upriv = netdev_priv(uplink_rpriv->netdev);
	err = tc_setup_cb_egdev_register(netdev, mlx5e_setup_tc_block_cb,
					 upriv);
	if (err)
		goto err_neigh_cleanup;

	err = register_netdev(netdev);
	if (err) {
		pr_warn("Failed to register representor netdev for vport %d\n",
			rep->vport);
		goto err_egdev_cleanup;
	}

	return 0;

err_egdev_cleanup:
	tc_setup_cb_egdev_unregister(netdev, mlx5e_setup_tc_block_cb,
				     upriv);

err_neigh_cleanup:
	mlx5e_rep_neigh_cleanup(rpriv);

err_detach_netdev:
	mlx5e_detach_netdev(netdev_priv(netdev));

err_destroy_netdev:
	mlx5e_destroy_netdev(netdev_priv(netdev));
	kfree(rpriv);
	return err;
}

static void
mlx5e_vport_rep_unload(struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_priv *rpriv = mlx5e_rep_to_rep_priv(rep);
	struct net_device *netdev = rpriv->netdev;
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_rep_priv *uplink_rpriv;
	void *ppriv = priv->ppriv;
	struct mlx5e_priv *upriv;

	unregister_netdev(netdev);
	uplink_rpriv = mlx5_eswitch_get_uplink_priv(priv->mdev->priv.eswitch,
						    REP_ETH);
	upriv = netdev_priv(uplink_rpriv->netdev);
	tc_setup_cb_egdev_unregister(netdev, mlx5e_setup_tc_block_cb,
				     upriv);
	mlx5e_rep_neigh_cleanup(rpriv);
	mlx5e_detach_netdev(priv);
	mlx5e_destroy_netdev(priv);
	kfree(ppriv); /* mlx5e_rep_priv */
}

static void mlx5e_rep_register_vf_vports(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_eswitch *esw   = mdev->priv.eswitch;
	int total_vfs = MLX5_TOTAL_VPORTS(mdev);
	int vport;

	for (vport = 1; vport < total_vfs; vport++) {
		struct mlx5_eswitch_rep_if rep_if = {};

		rep_if.load = mlx5e_vport_rep_load;
		rep_if.unload = mlx5e_vport_rep_unload;
		mlx5_eswitch_register_vport_rep(esw, vport, &rep_if, REP_ETH);
	}
}

static void mlx5e_rep_unregister_vf_vports(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	int total_vfs = MLX5_TOTAL_VPORTS(mdev);
	int vport;

	for (vport = 1; vport < total_vfs; vport++)
		mlx5_eswitch_unregister_vport_rep(esw, vport, REP_ETH);
}

void mlx5e_register_vport_reps(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_eswitch *esw   = mdev->priv.eswitch;
	struct mlx5_eswitch_rep_if rep_if;
	struct mlx5e_rep_priv *rpriv;

	rpriv = priv->ppriv;
	rpriv->netdev = priv->netdev;

	rep_if.load = mlx5e_nic_rep_load;
	rep_if.unload = mlx5e_nic_rep_unload;
	rep_if.priv = rpriv;
	INIT_LIST_HEAD(&rpriv->vport_sqs_list);
	mlx5_eswitch_register_vport_rep(esw, 0, &rep_if, REP_ETH); /* UPLINK PF vport*/

	mlx5e_rep_register_vf_vports(priv); /* VFs vports */
}

void mlx5e_unregister_vport_reps(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_eswitch *esw   = mdev->priv.eswitch;

	mlx5e_rep_unregister_vf_vports(priv); /* VFs vports */
	mlx5_eswitch_unregister_vport_rep(esw, 0, REP_ETH); /* UPLINK PF*/
}

void *mlx5e_alloc_nic_rep_priv(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv;

	rpriv = kzalloc(sizeof(*rpriv), GFP_KERNEL);
	if (!rpriv)
		return NULL;

	rpriv->rep = &esw->offloads.vport_reps[0];
	return rpriv;
}
