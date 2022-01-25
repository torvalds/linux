// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "selq.h"
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>
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

static int mlx5e_get_up(struct mlx5e_priv *priv, struct sk_buff *skb)
{
#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (priv->dcbx_dp.trust_state == MLX5_QPTS_TRUST_DSCP)
		return mlx5e_get_dscp_up(priv, skb);
#endif
	if (skb_vlan_tag_present(skb))
		return skb_vlan_tag_get_prio(skb);
	return 0;
}

static u16 mlx5e_select_ptpsq(struct net_device *dev, struct sk_buff *skb,
			      struct mlx5e_selq_params *selq)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int up;

	up = selq->num_tcs > 1 ? mlx5e_get_up(priv, skb) : 0;

	return selq->num_regular_queues + up;
}

static int mlx5e_select_htb_queue(struct mlx5e_priv *priv, struct sk_buff *skb)
{
	u16 classid;

	/* Order maj_id before defcls - pairs with mlx5e_htb_root_add. */
	if ((TC_H_MAJ(skb->priority) >> 16) == smp_load_acquire(&priv->htb.maj_id))
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
	struct mlx5e_selq_params *selq;
	int txq_ix, up;

	selq = rcu_dereference_bh(priv->selq.active);

	/* This is a workaround needed only for the mlx5e_netdev_change_profile
	 * flow that zeroes out the whole priv without unregistering the netdev
	 * and without preventing ndo_select_queue from being called.
	 */
	if (unlikely(!selq))
		return 0;

	if (unlikely(selq->is_ptp || selq->is_htb)) {
		if (unlikely(selq->is_htb)) {
			txq_ix = mlx5e_select_htb_queue(priv, skb);
			if (txq_ix > 0)
				return txq_ix;
		}

		if (unlikely(selq->is_ptp && mlx5e_use_ptpsq(skb)))
			return mlx5e_select_ptpsq(dev, skb, selq);

		txq_ix = netdev_pick_tx(dev, skb, NULL);
		/* Fix netdev_pick_tx() not to choose ptp_channel and HTB txqs.
		 * If they are selected, switch to regular queues.
		 * Driver to select these queues only at mlx5e_select_ptpsq()
		 * and mlx5e_select_htb_queue().
		 */
		if (unlikely(txq_ix >= selq->num_regular_queues))
			txq_ix %= selq->num_regular_queues;
	} else {
		txq_ix = netdev_pick_tx(dev, skb, NULL);
	}

	if (selq->num_tcs <= 1)
		return txq_ix;

	up = mlx5e_get_up(priv, skb);

	/* Normalize any picked txq_ix to [0, num_channels),
	 * So we can return a txq_ix that matches the channel and
	 * packet UP.
	 */
	return txq_ix % selq->num_channels + up * selq->num_channels;
}
