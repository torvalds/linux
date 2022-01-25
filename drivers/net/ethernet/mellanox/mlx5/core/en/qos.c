// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */
#include <net/sch_generic.h>

#include "en.h"
#include "params.h"
#include "../qos.h"

#define BYTES_IN_MBIT 125000

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

int mlx5e_qos_cur_leaf_nodes(struct mlx5e_priv *priv)
{
	int last = find_last_bit(priv->htb.qos_used_qids, mlx5e_qos_max_leaf_nodes(priv->mdev));

	return last == mlx5e_qos_max_leaf_nodes(priv->mdev) ? 0 : last + 1;
}

/* Software representation of the QoS tree (internal to this file) */

static int mlx5e_find_unused_qos_qid(struct mlx5e_priv *priv)
{
	int size = mlx5e_qos_max_leaf_nodes(priv->mdev);
	int res;

	WARN_ONCE(!mutex_is_locked(&priv->state_lock), "%s: state_lock is not held\n", __func__);
	res = find_first_zero_bit(priv->htb.qos_used_qids, size);

	return res == size ? -ENOSPC : res;
}

struct mlx5e_qos_node {
	struct hlist_node hnode;
	struct mlx5e_qos_node *parent;
	u64 rate;
	u32 bw_share;
	u32 max_average_bw;
	u32 hw_id;
	u32 classid; /* 16-bit, except root. */
	u16 qid;
};

#define MLX5E_QOS_QID_INNER 0xffff
#define MLX5E_HTB_CLASSID_ROOT 0xffffffff

static struct mlx5e_qos_node *
mlx5e_sw_node_create_leaf(struct mlx5e_priv *priv, u16 classid, u16 qid,
			  struct mlx5e_qos_node *parent)
{
	struct mlx5e_qos_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	node->parent = parent;

	node->qid = qid;
	__set_bit(qid, priv->htb.qos_used_qids);

	node->classid = classid;
	hash_add_rcu(priv->htb.qos_tc2node, &node->hnode, classid);

	mlx5e_update_tx_netdev_queues(priv);

	return node;
}

static struct mlx5e_qos_node *mlx5e_sw_node_create_root(struct mlx5e_priv *priv)
{
	struct mlx5e_qos_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	node->qid = MLX5E_QOS_QID_INNER;
	node->classid = MLX5E_HTB_CLASSID_ROOT;
	hash_add_rcu(priv->htb.qos_tc2node, &node->hnode, node->classid);

	return node;
}

static struct mlx5e_qos_node *mlx5e_sw_node_find(struct mlx5e_priv *priv, u32 classid)
{
	struct mlx5e_qos_node *node = NULL;

	hash_for_each_possible(priv->htb.qos_tc2node, node, hnode, classid) {
		if (node->classid == classid)
			break;
	}

	return node;
}

static struct mlx5e_qos_node *mlx5e_sw_node_find_rcu(struct mlx5e_priv *priv, u32 classid)
{
	struct mlx5e_qos_node *node = NULL;

	hash_for_each_possible_rcu(priv->htb.qos_tc2node, node, hnode, classid) {
		if (node->classid == classid)
			break;
	}

	return node;
}

static void mlx5e_sw_node_delete(struct mlx5e_priv *priv, struct mlx5e_qos_node *node)
{
	hash_del_rcu(&node->hnode);
	if (node->qid != MLX5E_QOS_QID_INNER) {
		__clear_bit(node->qid, priv->htb.qos_used_qids);
		mlx5e_update_tx_netdev_queues(priv);
	}
	/* Make sure this qid is no longer selected by mlx5e_select_queue, so
	 * that mlx5e_reactivate_qos_sq can safely restart the netdev TX queue.
	 */
	synchronize_net();
	kfree(node);
}

/* TX datapath API */

static u16 mlx5e_qid_from_qos(struct mlx5e_channels *chs, u16 qid)
{
	/* These channel params are safe to access from the datapath, because:
	 * 1. This function is called only after checking priv->htb.maj_id != 0,
	 *    and the number of queues can't change while HTB offload is active.
	 * 2. When priv->htb.maj_id becomes 0, synchronize_rcu waits for
	 *    mlx5e_select_queue to finish while holding priv->state_lock,
	 *    preventing other code from changing the number of queues.
	 */
	bool is_ptp = MLX5E_GET_PFLAG(&chs->params, MLX5E_PFLAG_TX_PORT_TS);

	return (chs->params.num_channels + is_ptp) * mlx5e_get_dcb_num_tc(&chs->params) + qid;
}

int mlx5e_get_txq_by_classid(struct mlx5e_priv *priv, u16 classid)
{
	struct mlx5e_qos_node *node;
	u16 qid;
	int res;

	rcu_read_lock();

	node = mlx5e_sw_node_find_rcu(priv, classid);
	if (!node) {
		res = -ENOENT;
		goto out;
	}
	qid = READ_ONCE(node->qid);
	if (qid == MLX5E_QOS_QID_INNER) {
		res = -EINVAL;
		goto out;
	}
	res = mlx5e_qid_from_qos(&priv->channels, qid);

out:
	rcu_read_unlock();
	return res;
}

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

/* SQ lifecycle */

static int mlx5e_open_qos_sq(struct mlx5e_priv *priv, struct mlx5e_channels *chs,
			     struct mlx5e_qos_node *node)
{
	struct mlx5e_create_cq_param ccp = {};
	struct mlx5e_txqsq __rcu **qos_sqs;
	struct mlx5e_sq_param param_sq;
	struct mlx5e_cq_param param_cq;
	int txq_ix, ix, qid, err = 0;
	struct mlx5e_params *params;
	struct mlx5e_channel *c;
	struct mlx5e_txqsq *sq;

	params = &chs->params;

	txq_ix = mlx5e_qid_from_qos(chs, node->qid);

	WARN_ON(node->qid > priv->htb.max_qos_sqs);
	if (node->qid == priv->htb.max_qos_sqs) {
		struct mlx5e_sq_stats *stats, **stats_list = NULL;

		if (priv->htb.max_qos_sqs == 0) {
			stats_list = kvcalloc(mlx5e_qos_max_leaf_nodes(priv->mdev),
					      sizeof(*stats_list),
					      GFP_KERNEL);
			if (!stats_list)
				return -ENOMEM;
		}
		stats = kzalloc(sizeof(*stats), GFP_KERNEL);
		if (!stats) {
			kvfree(stats_list);
			return -ENOMEM;
		}
		if (stats_list)
			WRITE_ONCE(priv->htb.qos_sq_stats, stats_list);
		WRITE_ONCE(priv->htb.qos_sq_stats[node->qid], stats);
		/* Order max_qos_sqs increment after writing the array pointer.
		 * Pairs with smp_load_acquire in en_stats.c.
		 */
		smp_store_release(&priv->htb.max_qos_sqs, priv->htb.max_qos_sqs + 1);
	}

	ix = node->qid % params->num_channels;
	qid = node->qid / params->num_channels;
	c = chs->c[ix];

	qos_sqs = mlx5e_state_dereference(priv, c->qos_sqs);
	sq = kzalloc(sizeof(*sq), GFP_KERNEL);

	if (!sq)
		return -ENOMEM;

	mlx5e_build_create_cq_param(&ccp, c);

	memset(&param_sq, 0, sizeof(param_sq));
	memset(&param_cq, 0, sizeof(param_cq));
	mlx5e_build_sq_param(priv->mdev, params, &param_sq);
	mlx5e_build_tx_cq_param(priv->mdev, params, &param_cq);
	err = mlx5e_open_cq(priv, params->tx_cq_moderation, &param_cq, &ccp, &sq->cq);
	if (err)
		goto err_free_sq;
	err = mlx5e_open_txqsq(c, priv->tisn[c->lag_port][0], txq_ix, params,
			       &param_sq, sq, 0, node->hw_id,
			       priv->htb.qos_sq_stats[node->qid]);
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

static void mlx5e_activate_qos_sq(struct mlx5e_priv *priv, struct mlx5e_qos_node *node)
{
	struct mlx5e_txqsq *sq;
	u16 qid;

	sq = mlx5e_get_qos_sq(priv, node->qid);

	qid = mlx5e_qid_from_qos(&priv->channels, node->qid);

	/* If it's a new queue, it will be marked as started at this point.
	 * Stop it before updating txq2sq.
	 */
	mlx5e_tx_disable_queue(netdev_get_tx_queue(priv->netdev, qid));

	priv->txq2sq[qid] = sq;

	/* Make the change to txq2sq visible before the queue is started.
	 * As mlx5e_xmit runs under a spinlock, there is an implicit ACQUIRE,
	 * which pairs with this barrier.
	 */
	smp_wmb();

	qos_dbg(priv->mdev, "Activate QoS SQ qid %u\n", node->qid);
	mlx5e_activate_txqsq(sq);
}

static void mlx5e_deactivate_qos_sq(struct mlx5e_priv *priv, u16 qid)
{
	struct mlx5e_txqsq *sq;

	sq = mlx5e_get_qos_sq(priv, qid);
	if (!sq) /* Handle the case when the SQ failed to open. */
		return;

	qos_dbg(priv->mdev, "Deactivate QoS SQ qid %u\n", qid);
	mlx5e_deactivate_txqsq(sq);

	priv->txq2sq[mlx5e_qid_from_qos(&priv->channels, qid)] = NULL;

	/* Make the change to txq2sq visible before the queue is started again.
	 * As mlx5e_xmit runs under a spinlock, there is an implicit ACQUIRE,
	 * which pairs with this barrier.
	 */
	smp_wmb();
}

static void mlx5e_close_qos_sq(struct mlx5e_priv *priv, u16 qid)
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

static void mlx5e_qos_close_all_queues(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_qos_close_queues(chs->c[i]);
}

static int mlx5e_qos_alloc_queues(struct mlx5e_priv *priv, struct mlx5e_channels *chs)
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
	struct mlx5e_qos_node *node = NULL;
	int bkt, err;

	if (!priv->htb.maj_id)
		return 0;

	err = mlx5e_qos_alloc_queues(priv, chs);
	if (err)
		return err;

	hash_for_each(priv->htb.qos_tc2node, bkt, node, hnode) {
		if (node->qid == MLX5E_QOS_QID_INNER)
			continue;
		err = mlx5e_open_qos_sq(priv, chs, node);
		if (err) {
			mlx5e_qos_close_all_queues(chs);
			return err;
		}
	}

	return 0;
}

void mlx5e_qos_activate_queues(struct mlx5e_priv *priv)
{
	struct mlx5e_qos_node *node = NULL;
	int bkt;

	hash_for_each(priv->htb.qos_tc2node, bkt, node, hnode) {
		if (node->qid == MLX5E_QOS_QID_INNER)
			continue;
		mlx5e_activate_qos_sq(priv, node);
	}
}

void mlx5e_qos_deactivate_queues(struct mlx5e_channel *c)
{
	struct mlx5e_params *params = &c->priv->channels.params;
	struct mlx5e_txqsq __rcu **qos_sqs;
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

		/* The queue is disabled, no synchronization with datapath is needed. */
		c->priv->txq2sq[mlx5e_qid_from_qos(&c->priv->channels, qid)] = NULL;
	}
}

static void mlx5e_qos_deactivate_all_queues(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_qos_deactivate_queues(chs->c[i]);
}

/* HTB API */

int mlx5e_htb_root_add(struct mlx5e_priv *priv, u16 htb_maj_id, u16 htb_defcls,
		       struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_node *root;
	bool opened;
	int err;

	qos_dbg(priv->mdev, "TC_HTB_CREATE handle %04x:, default :%04x\n", htb_maj_id, htb_defcls);

	if (!mlx5_qos_is_supported(priv->mdev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing QoS capabilities. Try disabling SRIOV or use a supported device.");
		return -EOPNOTSUPP;
	}

	opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (opened) {
		mlx5e_selq_prepare(&priv->selq, &priv->channels.params, true);

		err = mlx5e_qos_alloc_queues(priv, &priv->channels);
		if (err)
			goto err_cancel_selq;
	}

	root = mlx5e_sw_node_create_root(priv);
	if (IS_ERR(root)) {
		err = PTR_ERR(root);
		goto err_free_queues;
	}

	err = mlx5_qos_create_root_node(priv->mdev, &root->hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error. Try upgrading firmware.");
		goto err_sw_node_delete;
	}

	WRITE_ONCE(priv->htb.defcls, htb_defcls);
	/* Order maj_id after defcls - pairs with
	 * mlx5e_select_queue/mlx5e_select_htb_queues.
	 */
	smp_store_release(&priv->htb.maj_id, htb_maj_id);

	if (opened)
		mlx5e_selq_apply(&priv->selq);

	return 0;

err_sw_node_delete:
	mlx5e_sw_node_delete(priv, root);

err_free_queues:
	if (opened)
		mlx5e_qos_close_all_queues(&priv->channels);
err_cancel_selq:
	mlx5e_selq_cancel(&priv->selq);
	return err;
}

int mlx5e_htb_root_del(struct mlx5e_priv *priv)
{
	struct mlx5e_qos_node *root;
	int err;

	qos_dbg(priv->mdev, "TC_HTB_DESTROY\n");

	mlx5e_selq_prepare(&priv->selq, &priv->channels.params, false);
	mlx5e_selq_apply(&priv->selq);

	WRITE_ONCE(priv->htb.maj_id, 0);
	synchronize_rcu(); /* Sync with mlx5e_select_htb_queue and TX data path. */

	root = mlx5e_sw_node_find(priv, MLX5E_HTB_CLASSID_ROOT);
	if (!root) {
		qos_err(priv->mdev, "Failed to find the root node in the QoS tree\n");
		return -ENOENT;
	}
	err = mlx5_qos_destroy_node(priv->mdev, root->hw_id);
	if (err)
		qos_err(priv->mdev, "Failed to destroy root node %u, err = %d\n",
			root->hw_id, err);
	mlx5e_sw_node_delete(priv, root);

	mlx5e_qos_deactivate_all_queues(&priv->channels);
	mlx5e_qos_close_all_queues(&priv->channels);

	return err;
}

static int mlx5e_htb_convert_rate(struct mlx5e_priv *priv, u64 rate,
				  struct mlx5e_qos_node *parent, u32 *bw_share)
{
	u64 share = 0;

	while (parent->classid != MLX5E_HTB_CLASSID_ROOT && !parent->max_average_bw)
		parent = parent->parent;

	if (parent->max_average_bw)
		share = div64_u64(div_u64(rate * 100, BYTES_IN_MBIT),
				  parent->max_average_bw);
	else
		share = 101;

	*bw_share = share == 0 ? 1 : share > 100 ? 0 : share;

	qos_dbg(priv->mdev, "Convert: rate %llu, parent ceil %llu -> bw_share %u\n",
		rate, (u64)parent->max_average_bw * BYTES_IN_MBIT, *bw_share);

	return 0;
}

static void mlx5e_htb_convert_ceil(struct mlx5e_priv *priv, u64 ceil, u32 *max_average_bw)
{
	/* Hardware treats 0 as "unlimited", set at least 1. */
	*max_average_bw = max_t(u32, div_u64(ceil, BYTES_IN_MBIT), 1);

	qos_dbg(priv->mdev, "Convert: ceil %llu -> max_average_bw %u\n",
		ceil, *max_average_bw);
}

int mlx5e_htb_leaf_alloc_queue(struct mlx5e_priv *priv, u16 classid,
			       u32 parent_classid, u64 rate, u64 ceil,
			       struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_node *node, *parent;
	int qid;
	int err;

	qos_dbg(priv->mdev, "TC_HTB_LEAF_ALLOC_QUEUE classid %04x, parent %04x, rate %llu, ceil %llu\n",
		classid, parent_classid, rate, ceil);

	qid = mlx5e_find_unused_qos_qid(priv);
	if (qid < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Maximum amount of leaf classes is reached.");
		return qid;
	}

	parent = mlx5e_sw_node_find(priv, parent_classid);
	if (!parent)
		return -EINVAL;

	node = mlx5e_sw_node_create_leaf(priv, classid, qid, parent);
	if (IS_ERR(node))
		return PTR_ERR(node);

	node->rate = rate;
	mlx5e_htb_convert_rate(priv, rate, node->parent, &node->bw_share);
	mlx5e_htb_convert_ceil(priv, ceil, &node->max_average_bw);

	err = mlx5_qos_create_leaf_node(priv->mdev, node->parent->hw_id,
					node->bw_share, node->max_average_bw,
					&node->hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when creating a leaf node.");
		qos_err(priv->mdev, "Failed to create a leaf node (class %04x), err = %d\n",
			classid, err);
		mlx5e_sw_node_delete(priv, node);
		return err;
	}

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		err = mlx5e_open_qos_sq(priv, &priv->channels, node);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Error creating an SQ.");
			qos_warn(priv->mdev, "Failed to create a QoS SQ (class %04x), err = %d\n",
				 classid, err);
		} else {
			mlx5e_activate_qos_sq(priv, node);
		}
	}

	return mlx5e_qid_from_qos(&priv->channels, node->qid);
}

int mlx5e_htb_leaf_to_inner(struct mlx5e_priv *priv, u16 classid, u16 child_classid,
			    u64 rate, u64 ceil, struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_node *node, *child;
	int err, tmp_err;
	u32 new_hw_id;
	u16 qid;

	qos_dbg(priv->mdev, "TC_HTB_LEAF_TO_INNER classid %04x, upcoming child %04x, rate %llu, ceil %llu\n",
		classid, child_classid, rate, ceil);

	node = mlx5e_sw_node_find(priv, classid);
	if (!node)
		return -ENOENT;

	err = mlx5_qos_create_inner_node(priv->mdev, node->parent->hw_id,
					 node->bw_share, node->max_average_bw,
					 &new_hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when creating an inner node.");
		qos_err(priv->mdev, "Failed to create an inner node (class %04x), err = %d\n",
			classid, err);
		return err;
	}

	/* Intentionally reuse the qid for the upcoming first child. */
	child = mlx5e_sw_node_create_leaf(priv, child_classid, node->qid, node);
	if (IS_ERR(child)) {
		err = PTR_ERR(child);
		goto err_destroy_hw_node;
	}

	child->rate = rate;
	mlx5e_htb_convert_rate(priv, rate, node, &child->bw_share);
	mlx5e_htb_convert_ceil(priv, ceil, &child->max_average_bw);

	err = mlx5_qos_create_leaf_node(priv->mdev, new_hw_id, child->bw_share,
					child->max_average_bw, &child->hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when creating a leaf node.");
		qos_err(priv->mdev, "Failed to create a leaf node (class %04x), err = %d\n",
			classid, err);
		goto err_delete_sw_node;
	}

	/* No fail point. */

	qid = node->qid;
	/* Pairs with mlx5e_get_txq_by_classid. */
	WRITE_ONCE(node->qid, MLX5E_QOS_QID_INNER);

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		mlx5e_deactivate_qos_sq(priv, qid);
		mlx5e_close_qos_sq(priv, qid);
	}

	err = mlx5_qos_destroy_node(priv->mdev, node->hw_id);
	if (err) /* Not fatal. */
		qos_warn(priv->mdev, "Failed to destroy leaf node %u (class %04x), err = %d\n",
			 node->hw_id, classid, err);

	node->hw_id = new_hw_id;

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		err = mlx5e_open_qos_sq(priv, &priv->channels, child);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Error creating an SQ.");
			qos_warn(priv->mdev, "Failed to create a QoS SQ (class %04x), err = %d\n",
				 classid, err);
		} else {
			mlx5e_activate_qos_sq(priv, child);
		}
	}

	return 0;

err_delete_sw_node:
	child->qid = MLX5E_QOS_QID_INNER;
	mlx5e_sw_node_delete(priv, child);

err_destroy_hw_node:
	tmp_err = mlx5_qos_destroy_node(priv->mdev, new_hw_id);
	if (tmp_err) /* Not fatal. */
		qos_warn(priv->mdev, "Failed to roll back creation of an inner node %u (class %04x), err = %d\n",
			 new_hw_id, classid, tmp_err);
	return err;
}

static struct mlx5e_qos_node *mlx5e_sw_node_find_by_qid(struct mlx5e_priv *priv, u16 qid)
{
	struct mlx5e_qos_node *node = NULL;
	int bkt;

	hash_for_each(priv->htb.qos_tc2node, bkt, node, hnode)
		if (node->qid == qid)
			break;

	return node;
}

static void mlx5e_reactivate_qos_sq(struct mlx5e_priv *priv, u16 qid, struct netdev_queue *txq)
{
	qos_dbg(priv->mdev, "Reactivate QoS SQ qid %u\n", qid);
	netdev_tx_reset_queue(txq);
	netif_tx_start_queue(txq);
}

static void mlx5e_reset_qdisc(struct net_device *dev, u16 qid)
{
	struct netdev_queue *dev_queue = netdev_get_tx_queue(dev, qid);
	struct Qdisc *qdisc = dev_queue->qdisc_sleeping;

	if (!qdisc)
		return;

	spin_lock_bh(qdisc_lock(qdisc));
	qdisc_reset(qdisc);
	spin_unlock_bh(qdisc_lock(qdisc));
}

int mlx5e_htb_leaf_del(struct mlx5e_priv *priv, u16 *classid,
		       struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_node *node;
	struct netdev_queue *txq;
	u16 qid, moved_qid;
	bool opened;
	int err;

	qos_dbg(priv->mdev, "TC_HTB_LEAF_DEL classid %04x\n", *classid);

	node = mlx5e_sw_node_find(priv, *classid);
	if (!node)
		return -ENOENT;

	/* Store qid for reuse. */
	qid = node->qid;

	opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (opened) {
		txq = netdev_get_tx_queue(priv->netdev,
					  mlx5e_qid_from_qos(&priv->channels, qid));
		mlx5e_deactivate_qos_sq(priv, qid);
		mlx5e_close_qos_sq(priv, qid);
	}

	err = mlx5_qos_destroy_node(priv->mdev, node->hw_id);
	if (err) /* Not fatal. */
		qos_warn(priv->mdev, "Failed to destroy leaf node %u (class %04x), err = %d\n",
			 node->hw_id, *classid, err);

	mlx5e_sw_node_delete(priv, node);

	moved_qid = mlx5e_qos_cur_leaf_nodes(priv);

	if (moved_qid == 0) {
		/* The last QoS SQ was just destroyed. */
		if (opened)
			mlx5e_reactivate_qos_sq(priv, qid, txq);
		return 0;
	}
	moved_qid--;

	if (moved_qid < qid) {
		/* The highest QoS SQ was just destroyed. */
		WARN(moved_qid != qid - 1, "Gaps in queue numeration: destroyed queue %u, the highest queue is %u",
		     qid, moved_qid);
		if (opened)
			mlx5e_reactivate_qos_sq(priv, qid, txq);
		return 0;
	}

	WARN(moved_qid == qid, "Can't move node with qid %u to itself", qid);
	qos_dbg(priv->mdev, "Moving QoS SQ %u to %u\n", moved_qid, qid);

	node = mlx5e_sw_node_find_by_qid(priv, moved_qid);
	WARN(!node, "Could not find a node with qid %u to move to queue %u",
	     moved_qid, qid);

	/* Stop traffic to the old queue. */
	WRITE_ONCE(node->qid, MLX5E_QOS_QID_INNER);
	__clear_bit(moved_qid, priv->htb.qos_used_qids);

	if (opened) {
		txq = netdev_get_tx_queue(priv->netdev,
					  mlx5e_qid_from_qos(&priv->channels, moved_qid));
		mlx5e_deactivate_qos_sq(priv, moved_qid);
		mlx5e_close_qos_sq(priv, moved_qid);
	}

	/* Prevent packets from the old class from getting into the new one. */
	mlx5e_reset_qdisc(priv->netdev, moved_qid);

	__set_bit(qid, priv->htb.qos_used_qids);
	WRITE_ONCE(node->qid, qid);

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		err = mlx5e_open_qos_sq(priv, &priv->channels, node);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Error creating an SQ.");
			qos_warn(priv->mdev, "Failed to create a QoS SQ (class %04x) while moving qid %u to %u, err = %d\n",
				 node->classid, moved_qid, qid, err);
		} else {
			mlx5e_activate_qos_sq(priv, node);
		}
	}

	mlx5e_update_tx_netdev_queues(priv);
	if (opened)
		mlx5e_reactivate_qos_sq(priv, moved_qid, txq);

	*classid = node->classid;
	return 0;
}

int mlx5e_htb_leaf_del_last(struct mlx5e_priv *priv, u16 classid, bool force,
			    struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_node *node, *parent;
	u32 old_hw_id, new_hw_id;
	int err, saved_err = 0;
	u16 qid;

	qos_dbg(priv->mdev, "TC_HTB_LEAF_DEL_LAST%s classid %04x\n",
		force ? "_FORCE" : "", classid);

	node = mlx5e_sw_node_find(priv, classid);
	if (!node)
		return -ENOENT;

	err = mlx5_qos_create_leaf_node(priv->mdev, node->parent->parent->hw_id,
					node->parent->bw_share,
					node->parent->max_average_bw,
					&new_hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when creating a leaf node.");
		qos_err(priv->mdev, "Failed to create a leaf node (class %04x), err = %d\n",
			classid, err);
		if (!force)
			return err;
		saved_err = err;
	}

	/* Store qid for reuse and prevent clearing the bit. */
	qid = node->qid;
	/* Pairs with mlx5e_get_txq_by_classid. */
	WRITE_ONCE(node->qid, MLX5E_QOS_QID_INNER);

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		mlx5e_deactivate_qos_sq(priv, qid);
		mlx5e_close_qos_sq(priv, qid);
	}

	/* Prevent packets from the old class from getting into the new one. */
	mlx5e_reset_qdisc(priv->netdev, qid);

	err = mlx5_qos_destroy_node(priv->mdev, node->hw_id);
	if (err) /* Not fatal. */
		qos_warn(priv->mdev, "Failed to destroy leaf node %u (class %04x), err = %d\n",
			 node->hw_id, classid, err);

	parent = node->parent;
	mlx5e_sw_node_delete(priv, node);

	node = parent;
	WRITE_ONCE(node->qid, qid);

	/* Early return on error in force mode. Parent will still be an inner
	 * node to be deleted by a following delete operation.
	 */
	if (saved_err)
		return saved_err;

	old_hw_id = node->hw_id;
	node->hw_id = new_hw_id;

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		err = mlx5e_open_qos_sq(priv, &priv->channels, node);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Error creating an SQ.");
			qos_warn(priv->mdev, "Failed to create a QoS SQ (class %04x), err = %d\n",
				 classid, err);
		} else {
			mlx5e_activate_qos_sq(priv, node);
		}
	}

	err = mlx5_qos_destroy_node(priv->mdev, old_hw_id);
	if (err) /* Not fatal. */
		qos_warn(priv->mdev, "Failed to destroy leaf node %u (class %04x), err = %d\n",
			 node->hw_id, classid, err);

	return 0;
}

static int mlx5e_qos_update_children(struct mlx5e_priv *priv, struct mlx5e_qos_node *node,
				     struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_node *child;
	int err = 0;
	int bkt;

	hash_for_each(priv->htb.qos_tc2node, bkt, child, hnode) {
		u32 old_bw_share = child->bw_share;
		int err_one;

		if (child->parent != node)
			continue;

		mlx5e_htb_convert_rate(priv, child->rate, node, &child->bw_share);
		if (child->bw_share == old_bw_share)
			continue;

		err_one = mlx5_qos_update_node(priv->mdev, child->hw_id, child->bw_share,
					       child->max_average_bw, child->hw_id);
		if (!err && err_one) {
			err = err_one;

			NL_SET_ERR_MSG_MOD(extack, "Firmware error when modifying a child node.");
			qos_err(priv->mdev, "Failed to modify a child node (class %04x), err = %d\n",
				node->classid, err);
		}
	}

	return err;
}

int mlx5e_htb_node_modify(struct mlx5e_priv *priv, u16 classid, u64 rate, u64 ceil,
			  struct netlink_ext_ack *extack)
{
	u32 bw_share, max_average_bw;
	struct mlx5e_qos_node *node;
	bool ceil_changed = false;
	int err;

	qos_dbg(priv->mdev, "TC_HTB_LEAF_MODIFY classid %04x, rate %llu, ceil %llu\n",
		classid, rate, ceil);

	node = mlx5e_sw_node_find(priv, classid);
	if (!node)
		return -ENOENT;

	node->rate = rate;
	mlx5e_htb_convert_rate(priv, rate, node->parent, &bw_share);
	mlx5e_htb_convert_ceil(priv, ceil, &max_average_bw);

	err = mlx5_qos_update_node(priv->mdev, node->parent->hw_id, bw_share,
				   max_average_bw, node->hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when modifying a node.");
		qos_err(priv->mdev, "Failed to modify a node (class %04x), err = %d\n",
			classid, err);
		return err;
	}

	if (max_average_bw != node->max_average_bw)
		ceil_changed = true;

	node->bw_share = bw_share;
	node->max_average_bw = max_average_bw;

	if (ceil_changed)
		err = mlx5e_qos_update_children(priv, node, extack);

	return err;
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
