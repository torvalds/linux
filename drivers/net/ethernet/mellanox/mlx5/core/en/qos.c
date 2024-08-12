// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */
#include <net/sch_generic.h>

#include <net/pkt_cls.h>
#include "en.h"
#include "params.h"
#include "../qos.h"
#include "en/htb.h"

struct qos_sq_callback_params {
	struct mlx5e_priv *priv;
	struct mlx5e_channels *chs;
};

int mlx5e_qos_bytes_rate_check(struct mlx5_core_dev *mdev, u64 nbytes)
{
	if (nbytes < BYTES_IN_MBIT) {
		qos_warn(mdev, "Input rate (%llu Bytes/sec) below minimum supported (%u Bytes/sec)\n",
			 nbytes, BYTES_IN_MBIT);
		return -EINVAL;
	}
	return 0;
}

static u32 mlx5e_qos_bytes2mbits(struct mlx5_core_dev *mdev, u64 nbytes)
{
	return div_u64(nbytes, BYTES_IN_MBIT);
}

int mlx5e_qos_max_leaf_nodes(struct mlx5_core_dev *mdev)
{
	return min(MLX5E_QOS_MAX_LEAF_NODES, mlx5_qos_max_leaf_nodes(mdev));
}

/* TX datapath API */

u16 mlx5e_qid_from_qos(struct mlx5e_channels *chs, u16 qid)
{
	/* These channel params are safe to access from the datapath, because:
	 * 1. This function is called only after checking selq->htb_maj_id != 0,
	 *    and the number of queues can't change while HTB offload is active.
	 * 2. When selq->htb_maj_id becomes 0, synchronize_rcu waits for
	 *    mlx5e_select_queue to finish while holding priv->state_lock,
	 *    preventing other code from changing the number of queues.
	 */
	bool is_ptp = MLX5E_GET_PFLAG(&chs->params, MLX5E_PFLAG_TX_PORT_TS);

	return (chs->params.num_channels + is_ptp) * mlx5e_get_dcb_num_tc(&chs->params) + qid;
}

/* SQ lifecycle */

static struct mlx5e_txqsq *mlx5e_get_qos_sq(struct mlx5e_priv *priv, int qid)
{
	struct mlx5e_params *params = &priv->channels.params;
	struct mlx5e_txqsq __rcu **qos_sqs;
	struct mlx5e_channel *c;
	int ix;

	ix = qid % params->num_channels;
	qid /= params->num_channels;
	c = priv->channels.c[ix];

	qos_sqs = mlx5e_state_dereference(priv, c->qos_sqs);
	return mlx5e_state_dereference(priv, qos_sqs[qid]);
}

int mlx5e_open_qos_sq(struct mlx5e_priv *priv, struct mlx5e_channels *chs,
		      u16 node_qid, u32 hw_id)
{
	struct mlx5e_create_cq_param ccp = {};
	struct mlx5e_txqsq __rcu **qos_sqs;
	struct mlx5e_sq_param param_sq;
	struct mlx5e_cq_param param_cq;
	int txq_ix, ix, qid, err = 0;
	struct mlx5e_params *params;
	struct mlx5e_channel *c;
	struct mlx5e_txqsq *sq;
	u32 tisn;

	params = &chs->params;

	txq_ix = mlx5e_qid_from_qos(chs, node_qid);

	WARN_ON(node_qid >= mlx5e_htb_cur_leaf_nodes(priv->htb));
	if (!priv->htb_qos_sq_stats) {
		struct mlx5e_sq_stats **stats_list;

		stats_list = kvcalloc(mlx5e_qos_max_leaf_nodes(priv->mdev),
				      sizeof(*stats_list), GFP_KERNEL);
		if (!stats_list)
			return -ENOMEM;

		WRITE_ONCE(priv->htb_qos_sq_stats, stats_list);
	}

	if (!priv->htb_qos_sq_stats[node_qid]) {
		struct mlx5e_sq_stats *stats;

		stats = kzalloc(sizeof(*stats), GFP_KERNEL);
		if (!stats)
			return -ENOMEM;

		WRITE_ONCE(priv->htb_qos_sq_stats[node_qid], stats);
		/* Order htb_max_qos_sqs increment after writing the array pointer.
		 * Pairs with smp_load_acquire in en_stats.c.
		 */
		smp_store_release(&priv->htb_max_qos_sqs, priv->htb_max_qos_sqs + 1);
	}

	ix = node_qid % params->num_channels;
	qid = node_qid / params->num_channels;
	c = chs->c[ix];

	qos_sqs = mlx5e_state_dereference(priv, c->qos_sqs);
	sq = kzalloc(sizeof(*sq), GFP_KERNEL);

	if (!sq)
		return -ENOMEM;

	mlx5e_build_create_cq_param(&ccp, c);

	memset(&param_sq, 0, sizeof(param_sq));
	memset(&param_cq, 0, sizeof(param_cq));
	mlx5e_build_sq_param(c->mdev, params, &param_sq);
	mlx5e_build_tx_cq_param(c->mdev, params, &param_cq);
	err = mlx5e_open_cq(c->mdev, params->tx_cq_moderation, &param_cq, &ccp, &sq->cq);
	if (err)
		goto err_free_sq;

	tisn = mlx5e_profile_get_tisn(c->mdev, c->priv, c->priv->profile,
				      c->lag_port, 0);
	err = mlx5e_open_txqsq(c, tisn, txq_ix, params, &param_sq, sq, 0, hw_id,
			       priv->htb_qos_sq_stats[node_qid]);
	if (err)
		goto err_close_cq;

	rcu_assign_pointer(qos_sqs[qid], sq);

	return 0;

err_close_cq:
	mlx5e_close_cq(&sq->cq);
err_free_sq:
	kfree(sq);
	return err;
}

static int mlx5e_open_qos_sq_cb_wrapper(void *data, u16 node_qid, u32 hw_id)
{
	struct qos_sq_callback_params *cb_params = data;

	return mlx5e_open_qos_sq(cb_params->priv, cb_params->chs, node_qid, hw_id);
}

int mlx5e_activate_qos_sq(void *data, u16 node_qid, u32 hw_id)
{
	struct mlx5e_priv *priv = data;
	struct mlx5e_txqsq *sq;
	u16 qid;

	sq = mlx5e_get_qos_sq(priv, node_qid);

	qid = mlx5e_qid_from_qos(&priv->channels, node_qid);

	/* If it's a new queue, it will be marked as started at this point.
	 * Stop it before updating txq2sq.
	 */
	mlx5e_tx_disable_queue(netdev_get_tx_queue(priv->netdev, qid));

	priv->txq2sq[qid] = sq;
	priv->txq2sq_stats[qid] = sq->stats;

	/* Make the change to txq2sq visible before the queue is started.
	 * As mlx5e_xmit runs under a spinlock, there is an implicit ACQUIRE,
	 * which pairs with this barrier.
	 */
	smp_wmb();

	qos_dbg(sq->mdev, "Activate QoS SQ qid %u\n", node_qid);
	mlx5e_activate_txqsq(sq);

	return 0;
}

void mlx5e_deactivate_qos_sq(struct mlx5e_priv *priv, u16 qid)
{
	struct mlx5e_txqsq *sq;
	u16 txq_ix;

	sq = mlx5e_get_qos_sq(priv, qid);
	if (!sq) /* Handle the case when the SQ failed to open. */
		return;

	qos_dbg(sq->mdev, "Deactivate QoS SQ qid %u\n", qid);
	mlx5e_deactivate_txqsq(sq);

	txq_ix = mlx5e_qid_from_qos(&priv->channels, qid);

	priv->txq2sq[txq_ix] = NULL;
	priv->txq2sq_stats[txq_ix] = NULL;

	/* Make the change to txq2sq visible before the queue is started again.
	 * As mlx5e_xmit runs under a spinlock, there is an implicit ACQUIRE,
	 * which pairs with this barrier.
	 */
	smp_wmb();
}

void mlx5e_close_qos_sq(struct mlx5e_priv *priv, u16 qid)
{
	struct mlx5e_txqsq __rcu **qos_sqs;
	struct mlx5e_params *params;
	struct mlx5e_channel *c;
	struct mlx5e_txqsq *sq;
	int ix;

	params = &priv->channels.params;

	ix = qid % params->num_channels;
	qid /= params->num_channels;
	c = priv->channels.c[ix];
	qos_sqs = mlx5e_state_dereference(priv, c->qos_sqs);
	sq = rcu_replace_pointer(qos_sqs[qid], NULL, lockdep_is_held(&priv->state_lock));
	if (!sq) /* Handle the case when the SQ failed to open. */
		return;

	synchronize_rcu(); /* Sync with NAPI. */

	mlx5e_close_txqsq(sq);
	mlx5e_close_cq(&sq->cq);
	kfree(sq);
}

void mlx5e_qos_close_queues(struct mlx5e_channel *c)
{
	struct mlx5e_txqsq __rcu **qos_sqs;
	int i;

	qos_sqs = rcu_replace_pointer(c->qos_sqs, NULL, lockdep_is_held(&c->priv->state_lock));
	if (!qos_sqs)
		return;
	synchronize_rcu(); /* Sync with NAPI. */

	for (i = 0; i < c->qos_sqs_size; i++) {
		struct mlx5e_txqsq *sq;

		sq = mlx5e_state_dereference(c->priv, qos_sqs[i]);
		if (!sq) /* Handle the case when the SQ failed to open. */
			continue;

		mlx5e_close_txqsq(sq);
		mlx5e_close_cq(&sq->cq);
		kfree(sq);
	}

	kvfree(qos_sqs);
}

void mlx5e_qos_close_all_queues(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_qos_close_queues(chs->c[i]);
}

int mlx5e_qos_alloc_queues(struct mlx5e_priv *priv, struct mlx5e_channels *chs)
{
	u16 qos_sqs_size;
	int i;

	qos_sqs_size = DIV_ROUND_UP(mlx5e_qos_max_leaf_nodes(priv->mdev), chs->num);

	for (i = 0; i < chs->num; i++) {
		struct mlx5e_txqsq **sqs;

		sqs = kvcalloc(qos_sqs_size, sizeof(struct mlx5e_txqsq *), GFP_KERNEL);
		if (!sqs)
			goto err_free;

		WRITE_ONCE(chs->c[i]->qos_sqs_size, qos_sqs_size);
		smp_wmb(); /* Pairs with mlx5e_napi_poll. */
		rcu_assign_pointer(chs->c[i]->qos_sqs, sqs);
	}

	return 0;

err_free:
	while (--i >= 0) {
		struct mlx5e_txqsq **sqs;

		sqs = rcu_replace_pointer(chs->c[i]->qos_sqs, NULL,
					  lockdep_is_held(&priv->state_lock));

		synchronize_rcu(); /* Sync with NAPI. */
		kvfree(sqs);
	}
	return -ENOMEM;
}

int mlx5e_qos_open_queues(struct mlx5e_priv *priv, struct mlx5e_channels *chs)
{
	struct qos_sq_callback_params callback_params;
	int err;

	err = mlx5e_qos_alloc_queues(priv, chs);
	if (err)
		return err;

	callback_params.priv = priv;
	callback_params.chs = chs;

	err = mlx5e_htb_enumerate_leaves(priv->htb, mlx5e_open_qos_sq_cb_wrapper, &callback_params);
	if (err) {
		mlx5e_qos_close_all_queues(chs);
		return err;
	}

	return 0;
}

void mlx5e_qos_activate_queues(struct mlx5e_priv *priv)
{
	mlx5e_htb_enumerate_leaves(priv->htb, mlx5e_activate_qos_sq, priv);
}

void mlx5e_qos_deactivate_queues(struct mlx5e_channel *c)
{
	struct mlx5e_params *params = &c->priv->channels.params;
	struct mlx5e_txqsq __rcu **qos_sqs;
	u16 txq_ix;
	int i;

	qos_sqs = mlx5e_state_dereference(c->priv, c->qos_sqs);
	if (!qos_sqs)
		return;

	for (i = 0; i < c->qos_sqs_size; i++) {
		u16 qid = params->num_channels * i + c->ix;
		struct mlx5e_txqsq *sq;

		sq = mlx5e_state_dereference(c->priv, qos_sqs[i]);
		if (!sq) /* Handle the case when the SQ failed to open. */
			continue;

		qos_dbg(c->mdev, "Deactivate QoS SQ qid %u\n", qid);
		mlx5e_deactivate_txqsq(sq);

		txq_ix = mlx5e_qid_from_qos(&c->priv->channels, qid);

		/* The queue is disabled, no synchronization with datapath is needed. */
		c->priv->txq2sq[txq_ix] = NULL;
		c->priv->txq2sq_stats[txq_ix] = NULL;
	}
}

void mlx5e_qos_deactivate_all_queues(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_qos_deactivate_queues(chs->c[i]);
}

void mlx5e_reactivate_qos_sq(struct mlx5e_priv *priv, u16 qid, struct netdev_queue *txq)
{
	qos_dbg(priv->mdev, "Reactivate QoS SQ qid %u\n", qid);
	netdev_tx_reset_queue(txq);
	netif_tx_start_queue(txq);
}

void mlx5e_reset_qdisc(struct net_device *dev, u16 qid)
{
	struct netdev_queue *dev_queue = netdev_get_tx_queue(dev, qid);
	struct Qdisc *qdisc = dev_queue->qdisc_sleeping;

	if (!qdisc)
		return;

	spin_lock_bh(qdisc_lock(qdisc));
	qdisc_reset(qdisc);
	spin_unlock_bh(qdisc_lock(qdisc));
}

int mlx5e_htb_setup_tc(struct mlx5e_priv *priv, struct tc_htb_qopt_offload *htb_qopt)
{
	struct mlx5e_htb *htb = priv->htb;
	int res;

	if (!htb && htb_qopt->command != TC_HTB_CREATE)
		return -EINVAL;

	if (htb_qopt->prio || htb_qopt->quantum) {
		NL_SET_ERR_MSG_MOD(htb_qopt->extack,
				   "prio and quantum parameters are not supported by device with HTB offload enabled.");
		return -EOPNOTSUPP;
	}

	switch (htb_qopt->command) {
	case TC_HTB_CREATE:
		if (!mlx5_qos_is_supported(priv->mdev)) {
			NL_SET_ERR_MSG_MOD(htb_qopt->extack,
					   "Missing QoS capabilities. Try disabling SRIOV or use a supported device.");
			return -EOPNOTSUPP;
		}
		priv->htb = mlx5e_htb_alloc();
		htb = priv->htb;
		if (!htb)
			return -ENOMEM;
		res = mlx5e_htb_init(htb, htb_qopt, priv->netdev, priv->mdev, &priv->selq, priv);
		if (res) {
			mlx5e_htb_free(htb);
			priv->htb = NULL;
		}
		return res;
	case TC_HTB_DESTROY:
		mlx5e_htb_cleanup(htb);
		mlx5e_htb_free(htb);
		priv->htb = NULL;
		return 0;
	case TC_HTB_LEAF_ALLOC_QUEUE:
		res = mlx5e_htb_leaf_alloc_queue(htb, htb_qopt->classid, htb_qopt->parent_classid,
						 htb_qopt->rate, htb_qopt->ceil, htb_qopt->extack);
		if (res < 0)
			return res;
		htb_qopt->qid = res;
		return 0;
	case TC_HTB_LEAF_TO_INNER:
		return mlx5e_htb_leaf_to_inner(htb, htb_qopt->parent_classid, htb_qopt->classid,
					       htb_qopt->rate, htb_qopt->ceil, htb_qopt->extack);
	case TC_HTB_LEAF_DEL:
		return mlx5e_htb_leaf_del(htb, &htb_qopt->classid, htb_qopt->extack);
	case TC_HTB_LEAF_DEL_LAST:
	case TC_HTB_LEAF_DEL_LAST_FORCE:
		return mlx5e_htb_leaf_del_last(htb, htb_qopt->classid,
					       htb_qopt->command == TC_HTB_LEAF_DEL_LAST_FORCE,
					       htb_qopt->extack);
	case TC_HTB_NODE_MODIFY:
		return mlx5e_htb_node_modify(htb, htb_qopt->classid, htb_qopt->rate, htb_qopt->ceil,
					     htb_qopt->extack);
	case TC_HTB_LEAF_QUERY_QUEUE:
		res = mlx5e_htb_get_txq_by_classid(htb, htb_qopt->classid);
		if (res < 0)
			return res;
		htb_qopt->qid = res;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

struct mlx5e_mqprio_rl {
	struct mlx5_core_dev *mdev;
	u32 root_id;
	u32 *leaves_id;
	u8 num_tc;
};

struct mlx5e_mqprio_rl *mlx5e_mqprio_rl_alloc(void)
{
	return kvzalloc(sizeof(struct mlx5e_mqprio_rl), GFP_KERNEL);
}

void mlx5e_mqprio_rl_free(struct mlx5e_mqprio_rl *rl)
{
	kvfree(rl);
}

int mlx5e_mqprio_rl_init(struct mlx5e_mqprio_rl *rl, struct mlx5_core_dev *mdev, u8 num_tc,
			 u64 max_rate[])
{
	int err;
	int tc;

	if (!mlx5_qos_is_supported(mdev)) {
		qos_warn(mdev, "Missing QoS capabilities. Try disabling SRIOV or use a supported device.");
		return -EOPNOTSUPP;
	}
	if (num_tc > mlx5e_qos_max_leaf_nodes(mdev))
		return -EINVAL;

	rl->mdev = mdev;
	rl->num_tc = num_tc;
	rl->leaves_id = kvcalloc(num_tc, sizeof(*rl->leaves_id), GFP_KERNEL);
	if (!rl->leaves_id)
		return -ENOMEM;

	err = mlx5_qos_create_root_node(mdev, &rl->root_id);
	if (err)
		goto err_free_leaves;

	qos_dbg(mdev, "Root created, id %#x\n", rl->root_id);

	for (tc = 0; tc < num_tc; tc++) {
		u32 max_average_bw;

		max_average_bw = mlx5e_qos_bytes2mbits(mdev, max_rate[tc]);
		err = mlx5_qos_create_leaf_node(mdev, rl->root_id, 0, max_average_bw,
						&rl->leaves_id[tc]);
		if (err)
			goto err_destroy_leaves;

		qos_dbg(mdev, "Leaf[%d] created, id %#x, max average bw %u Mbits/sec\n",
			tc, rl->leaves_id[tc], max_average_bw);
	}
	return 0;

err_destroy_leaves:
	while (--tc >= 0)
		mlx5_qos_destroy_node(mdev, rl->leaves_id[tc]);
	mlx5_qos_destroy_node(mdev, rl->root_id);
err_free_leaves:
	kvfree(rl->leaves_id);
	return err;
}

void mlx5e_mqprio_rl_cleanup(struct mlx5e_mqprio_rl *rl)
{
	int tc;

	for (tc = 0; tc < rl->num_tc; tc++)
		mlx5_qos_destroy_node(rl->mdev, rl->leaves_id[tc]);
	mlx5_qos_destroy_node(rl->mdev, rl->root_id);
	kvfree(rl->leaves_id);
}

int mlx5e_mqprio_rl_get_node_hw_id(struct mlx5e_mqprio_rl *rl, int tc, u32 *hw_id)
{
	if (tc >= rl->num_tc)
		return -EINVAL;

	*hw_id = rl->leaves_id[tc];
	return 0;
}
