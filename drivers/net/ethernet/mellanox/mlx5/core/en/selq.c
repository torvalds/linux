// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "selq.h"
#include <linux/slab.h>
#include <linux/netdevice.h>
#include "en.h"
#include "en/ptp.h"

struct mlx5e_selq_params {
	unsigned int num_regular_queues;
	unsigned int num_channels;
	unsigned int num_tcs;
	bool is_htb;
	bool is_ptp;
};

int mlx5e_selq_init(struct mlx5e_selq *selq, struct mutex *state_lock)
{
	struct mlx5e_selq_params *init_params;

	selq->state_lock = state_lock;

	selq->standby = kvzalloc(sizeof(*selq->standby), GFP_KERNEL);
	if (!selq->standby)
		return -ENOMEM;

	init_params = kvzalloc(sizeof(*selq->active), GFP_KERNEL);
	if (!init_params) {
		kvfree(selq->standby);
		selq->standby = NULL;
		return -ENOMEM;
	}
	/* Assign dummy values, so that mlx5e_select_queue won't crash. */
	*init_params = (struct mlx5e_selq_params) {
		.num_regular_queues = 1,
		.num_channels = 1,
		.num_tcs = 1,
		.is_htb = false,
		.is_ptp = false,
	};
	rcu_assign_pointer(selq->active, init_params);

	return 0;
}

void mlx5e_selq_cleanup(struct mlx5e_selq *selq)
{
	WARN_ON_ONCE(selq->is_prepared);

	kvfree(selq->standby);
	selq->standby = NULL;
	selq->is_prepared = true;

	mlx5e_selq_apply(selq);

	kvfree(selq->standby);
	selq->standby = NULL;
}

void mlx5e_selq_prepare(struct mlx5e_selq *selq, struct mlx5e_params *params, bool htb)
{
	lockdep_assert_held(selq->state_lock);
	WARN_ON_ONCE(selq->is_prepared);

	selq->is_prepared = true;

	selq->standby->num_channels = params->num_channels;
	selq->standby->num_tcs = mlx5e_get_dcb_num_tc(params);
	selq->standby->num_regular_queues =
		selq->standby->num_channels * selq->standby->num_tcs;
	selq->standby->is_htb = htb;
	selq->standby->is_ptp = MLX5E_GET_PFLAG(params, MLX5E_PFLAG_TX_PORT_TS);
}

void mlx5e_selq_apply(struct mlx5e_selq *selq)
{
	struct mlx5e_selq_params *old_params;

	WARN_ON_ONCE(!selq->is_prepared);

	selq->is_prepared = false;

	old_params = rcu_replace_pointer(selq->active, selq->standby,
					 lockdep_is_held(selq->state_lock));
	synchronize_net(); /* Wait until ndo_select_queue starts emitting correct values. */
	selq->standby = old_params;
}

void mlx5e_selq_cancel(struct mlx5e_selq *selq)
{
	lockdep_assert_held(selq->state_lock);
	WARN_ON_ONCE(!selq->is_prepared);

	selq->is_prepared = false;
}

#ifdef CONFIG_MLX5_CORE_EN_DCB
static int mlx5e_get_dscp_up(struct mlx5e_priv *priv, struct sk_buff *skb)
{
	int dscp_cp = 0;

	if (skb->protocol == htons(ETH_P_IP))
		dscp_cp = ipv4_get_dsfield(ip_hdr(skb)) >> 2;
	else if (skb->protocol == htons(ETH_P_IPV6))
		dscp_cp = ipv6_get_dsfield(ipv6_hdr(skb)) >> 2;

	return priv->dcbx_dp.dscp2prio[dscp_cp];
}
#endif

static u16 mlx5e_select_ptpsq(struct net_device *dev, struct sk_buff *skb)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int up = 0;

	if (!netdev_get_num_tc(dev))
		goto return_txq;

#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (priv->dcbx_dp.trust_state == MLX5_QPTS_TRUST_DSCP)
		up = mlx5e_get_dscp_up(priv, skb);
	else
#endif
		if (skb_vlan_tag_present(skb))
			up = skb_vlan_tag_get_prio(skb);

return_txq:
	return priv->port_ptp_tc2realtxq[up];
}

static int mlx5e_select_htb_queue(struct mlx5e_priv *priv, struct sk_buff *skb,
				  u16 htb_maj_id)
{
	u16 classid;

	if ((TC_H_MAJ(skb->priority) >> 16) == htb_maj_id)
		classid = TC_H_MIN(skb->priority);
	else
		classid = READ_ONCE(priv->htb.defcls);

	if (!classid)
		return 0;

	return mlx5e_get_txq_by_classid(priv, classid);
}

u16 mlx5e_select_queue(struct net_device *dev, struct sk_buff *skb,
		       struct net_device *sb_dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int num_tc_x_num_ch;
	int txq_ix;
	int up = 0;
	int ch_ix;

	/* Sync with mlx5e_update_num_tc_x_num_ch - avoid refetching. */
	num_tc_x_num_ch = READ_ONCE(priv->num_tc_x_num_ch);
	if (unlikely(dev->real_num_tx_queues > num_tc_x_num_ch)) {
		struct mlx5e_ptp *ptp_channel;

		/* Order maj_id before defcls - pairs with mlx5e_htb_root_add. */
		u16 htb_maj_id = smp_load_acquire(&priv->htb.maj_id);

		if (unlikely(htb_maj_id)) {
			txq_ix = mlx5e_select_htb_queue(priv, skb, htb_maj_id);
			if (txq_ix > 0)
				return txq_ix;
		}

		ptp_channel = READ_ONCE(priv->channels.ptp);
		if (unlikely(ptp_channel &&
			     test_bit(MLX5E_PTP_STATE_TX, ptp_channel->state) &&
			     mlx5e_use_ptpsq(skb)))
			return mlx5e_select_ptpsq(dev, skb);

		txq_ix = netdev_pick_tx(dev, skb, NULL);
		/* Fix netdev_pick_tx() not to choose ptp_channel and HTB txqs.
		 * If they are selected, switch to regular queues.
		 * Driver to select these queues only at mlx5e_select_ptpsq()
		 * and mlx5e_select_htb_queue().
		 */
		if (unlikely(txq_ix >= num_tc_x_num_ch))
			txq_ix %= num_tc_x_num_ch;
	} else {
		txq_ix = netdev_pick_tx(dev, skb, NULL);
	}

	if (!netdev_get_num_tc(dev))
		return txq_ix;

#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (priv->dcbx_dp.trust_state == MLX5_QPTS_TRUST_DSCP)
		up = mlx5e_get_dscp_up(priv, skb);
	else
#endif
		if (skb_vlan_tag_present(skb))
			up = skb_vlan_tag_get_prio(skb);

	/* Normalize any picked txq_ix to [0, num_channels),
	 * So we can return a txq_ix that matches the channel and
	 * packet UP.
	 */
	ch_ix = priv->txq2sq[txq_ix]->ch_ix;

	return priv->channel_tc2realtxq[ch_ix][up];
}
