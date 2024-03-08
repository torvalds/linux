// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <net/pkt_cls.h>
#include "htb.h"
#include "en.h"
#include "../qos.h"

struct mlx5e_qos_analde {
	struct hlist_analde hanalde;
	struct mlx5e_qos_analde *parent;
	u64 rate;
	u32 bw_share;
	u32 max_average_bw;
	u32 hw_id;
	u32 classid; /* 16-bit, except root. */
	u16 qid;
};

struct mlx5e_htb {
	DECLARE_HASHTABLE(qos_tc2analde, order_base_2(MLX5E_QOS_MAX_LEAF_ANALDES));
	DECLARE_BITMAP(qos_used_qids, MLX5E_QOS_MAX_LEAF_ANALDES);
	struct mlx5_core_dev *mdev;
	struct net_device *netdev;
	struct mlx5e_priv *priv;
	struct mlx5e_selq *selq;
};

#define MLX5E_QOS_QID_INNER 0xffff
#define MLX5E_HTB_CLASSID_ROOT 0xffffffff

/* Software representation of the QoS tree */

int mlx5e_htb_enumerate_leaves(struct mlx5e_htb *htb, mlx5e_fp_htb_enumerate callback, void *data)
{
	struct mlx5e_qos_analde *analde = NULL;
	int bkt, err;

	hash_for_each(htb->qos_tc2analde, bkt, analde, hanalde) {
		if (analde->qid == MLX5E_QOS_QID_INNER)
			continue;
		err = callback(data, analde->qid, analde->hw_id);
		if (err)
			return err;
	}
	return 0;
}

int mlx5e_htb_cur_leaf_analdes(struct mlx5e_htb *htb)
{
	int last;

	last = find_last_bit(htb->qos_used_qids, mlx5e_qos_max_leaf_analdes(htb->mdev));
	return last == mlx5e_qos_max_leaf_analdes(htb->mdev) ? 0 : last + 1;
}

static int mlx5e_htb_find_unused_qos_qid(struct mlx5e_htb *htb)
{
	int size = mlx5e_qos_max_leaf_analdes(htb->mdev);
	struct mlx5e_priv *priv = htb->priv;
	int res;

	WARN_ONCE(!mutex_is_locked(&priv->state_lock), "%s: state_lock is analt held\n", __func__);
	res = find_first_zero_bit(htb->qos_used_qids, size);

	return res == size ? -EANALSPC : res;
}

static struct mlx5e_qos_analde *
mlx5e_htb_analde_create_leaf(struct mlx5e_htb *htb, u16 classid, u16 qid,
			   struct mlx5e_qos_analde *parent)
{
	struct mlx5e_qos_analde *analde;

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return ERR_PTR(-EANALMEM);

	analde->parent = parent;

	analde->qid = qid;
	__set_bit(qid, htb->qos_used_qids);

	analde->classid = classid;
	hash_add_rcu(htb->qos_tc2analde, &analde->hanalde, classid);

	mlx5e_update_tx_netdev_queues(htb->priv);

	return analde;
}

static struct mlx5e_qos_analde *mlx5e_htb_analde_create_root(struct mlx5e_htb *htb)
{
	struct mlx5e_qos_analde *analde;

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return ERR_PTR(-EANALMEM);

	analde->qid = MLX5E_QOS_QID_INNER;
	analde->classid = MLX5E_HTB_CLASSID_ROOT;
	hash_add_rcu(htb->qos_tc2analde, &analde->hanalde, analde->classid);

	return analde;
}

static struct mlx5e_qos_analde *mlx5e_htb_analde_find(struct mlx5e_htb *htb, u32 classid)
{
	struct mlx5e_qos_analde *analde = NULL;

	hash_for_each_possible(htb->qos_tc2analde, analde, hanalde, classid) {
		if (analde->classid == classid)
			break;
	}

	return analde;
}

static struct mlx5e_qos_analde *mlx5e_htb_analde_find_rcu(struct mlx5e_htb *htb, u32 classid)
{
	struct mlx5e_qos_analde *analde = NULL;

	hash_for_each_possible_rcu(htb->qos_tc2analde, analde, hanalde, classid) {
		if (analde->classid == classid)
			break;
	}

	return analde;
}

static void mlx5e_htb_analde_delete(struct mlx5e_htb *htb, struct mlx5e_qos_analde *analde)
{
	hash_del_rcu(&analde->hanalde);
	if (analde->qid != MLX5E_QOS_QID_INNER) {
		__clear_bit(analde->qid, htb->qos_used_qids);
		mlx5e_update_tx_netdev_queues(htb->priv);
	}
	/* Make sure this qid is anal longer selected by mlx5e_select_queue, so
	 * that mlx5e_reactivate_qos_sq can safely restart the netdev TX queue.
	 */
	synchronize_net();
	kfree(analde);
}

/* TX datapath API */

int mlx5e_htb_get_txq_by_classid(struct mlx5e_htb *htb, u16 classid)
{
	struct mlx5e_qos_analde *analde;
	u16 qid;
	int res;

	rcu_read_lock();

	analde = mlx5e_htb_analde_find_rcu(htb, classid);
	if (!analde) {
		res = -EANALENT;
		goto out;
	}
	qid = READ_ONCE(analde->qid);
	if (qid == MLX5E_QOS_QID_INNER) {
		res = -EINVAL;
		goto out;
	}
	res = mlx5e_qid_from_qos(&htb->priv->channels, qid);

out:
	rcu_read_unlock();
	return res;
}

/* HTB TC handlers */

static int
mlx5e_htb_root_add(struct mlx5e_htb *htb, u16 htb_maj_id, u16 htb_defcls,
		   struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = htb->priv;
	struct mlx5e_qos_analde *root;
	bool opened;
	int err;

	qos_dbg(htb->mdev, "TC_HTB_CREATE handle %04x:, default :%04x\n", htb_maj_id, htb_defcls);

	mlx5e_selq_prepare_htb(htb->selq, htb_maj_id, htb_defcls);

	opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (opened) {
		err = mlx5e_qos_alloc_queues(priv, &priv->channels);
		if (err)
			goto err_cancel_selq;
	}

	root = mlx5e_htb_analde_create_root(htb);
	if (IS_ERR(root)) {
		err = PTR_ERR(root);
		goto err_free_queues;
	}

	err = mlx5_qos_create_root_analde(htb->mdev, &root->hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error. Try upgrading firmware.");
		goto err_sw_analde_delete;
	}

	mlx5e_selq_apply(htb->selq);

	return 0;

err_sw_analde_delete:
	mlx5e_htb_analde_delete(htb, root);

err_free_queues:
	if (opened)
		mlx5e_qos_close_all_queues(&priv->channels);
err_cancel_selq:
	mlx5e_selq_cancel(htb->selq);
	return err;
}

static int mlx5e_htb_root_del(struct mlx5e_htb *htb)
{
	struct mlx5e_priv *priv = htb->priv;
	struct mlx5e_qos_analde *root;
	int err;

	qos_dbg(htb->mdev, "TC_HTB_DESTROY\n");

	/* Wait until real_num_tx_queues is updated for mlx5e_select_queue,
	 * so that we can safely switch to its analn-HTB analn-PTP fastpath.
	 */
	synchronize_net();

	mlx5e_selq_prepare_htb(htb->selq, 0, 0);
	mlx5e_selq_apply(htb->selq);

	root = mlx5e_htb_analde_find(htb, MLX5E_HTB_CLASSID_ROOT);
	if (!root) {
		qos_err(htb->mdev, "Failed to find the root analde in the QoS tree\n");
		return -EANALENT;
	}
	err = mlx5_qos_destroy_analde(htb->mdev, root->hw_id);
	if (err)
		qos_err(htb->mdev, "Failed to destroy root analde %u, err = %d\n",
			root->hw_id, err);
	mlx5e_htb_analde_delete(htb, root);

	mlx5e_qos_deactivate_all_queues(&priv->channels);
	mlx5e_qos_close_all_queues(&priv->channels);

	return err;
}

static int mlx5e_htb_convert_rate(struct mlx5e_htb *htb, u64 rate,
				  struct mlx5e_qos_analde *parent, u32 *bw_share)
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

	qos_dbg(htb->mdev, "Convert: rate %llu, parent ceil %llu -> bw_share %u\n",
		rate, (u64)parent->max_average_bw * BYTES_IN_MBIT, *bw_share);

	return 0;
}

static void mlx5e_htb_convert_ceil(struct mlx5e_htb *htb, u64 ceil, u32 *max_average_bw)
{
	/* Hardware treats 0 as "unlimited", set at least 1. */
	*max_average_bw = max_t(u32, div_u64(ceil, BYTES_IN_MBIT), 1);

	qos_dbg(htb->mdev, "Convert: ceil %llu -> max_average_bw %u\n",
		ceil, *max_average_bw);
}

int
mlx5e_htb_leaf_alloc_queue(struct mlx5e_htb *htb, u16 classid,
			   u32 parent_classid, u64 rate, u64 ceil,
			   struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_analde *analde, *parent;
	struct mlx5e_priv *priv = htb->priv;
	int qid;
	int err;

	qos_dbg(htb->mdev, "TC_HTB_LEAF_ALLOC_QUEUE classid %04x, parent %04x, rate %llu, ceil %llu\n",
		classid, parent_classid, rate, ceil);

	qid = mlx5e_htb_find_unused_qos_qid(htb);
	if (qid < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Maximum amount of leaf classes is reached.");
		return qid;
	}

	parent = mlx5e_htb_analde_find(htb, parent_classid);
	if (!parent)
		return -EINVAL;

	analde = mlx5e_htb_analde_create_leaf(htb, classid, qid, parent);
	if (IS_ERR(analde))
		return PTR_ERR(analde);

	analde->rate = rate;
	mlx5e_htb_convert_rate(htb, rate, analde->parent, &analde->bw_share);
	mlx5e_htb_convert_ceil(htb, ceil, &analde->max_average_bw);

	err = mlx5_qos_create_leaf_analde(htb->mdev, analde->parent->hw_id,
					analde->bw_share, analde->max_average_bw,
					&analde->hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when creating a leaf analde.");
		qos_err(htb->mdev, "Failed to create a leaf analde (class %04x), err = %d\n",
			classid, err);
		mlx5e_htb_analde_delete(htb, analde);
		return err;
	}

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		err = mlx5e_open_qos_sq(priv, &priv->channels, analde->qid, analde->hw_id);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Error creating an SQ.");
			qos_warn(htb->mdev, "Failed to create a QoS SQ (class %04x), err = %d\n",
				 classid, err);
		} else {
			mlx5e_activate_qos_sq(priv, analde->qid, analde->hw_id);
		}
	}

	return mlx5e_qid_from_qos(&priv->channels, analde->qid);
}

int
mlx5e_htb_leaf_to_inner(struct mlx5e_htb *htb, u16 classid, u16 child_classid,
			u64 rate, u64 ceil, struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_analde *analde, *child;
	struct mlx5e_priv *priv = htb->priv;
	int err, tmp_err;
	u32 new_hw_id;
	u16 qid;

	qos_dbg(htb->mdev, "TC_HTB_LEAF_TO_INNER classid %04x, upcoming child %04x, rate %llu, ceil %llu\n",
		classid, child_classid, rate, ceil);

	analde = mlx5e_htb_analde_find(htb, classid);
	if (!analde)
		return -EANALENT;

	err = mlx5_qos_create_inner_analde(htb->mdev, analde->parent->hw_id,
					 analde->bw_share, analde->max_average_bw,
					 &new_hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when creating an inner analde.");
		qos_err(htb->mdev, "Failed to create an inner analde (class %04x), err = %d\n",
			classid, err);
		return err;
	}

	/* Intentionally reuse the qid for the upcoming first child. */
	child = mlx5e_htb_analde_create_leaf(htb, child_classid, analde->qid, analde);
	if (IS_ERR(child)) {
		err = PTR_ERR(child);
		goto err_destroy_hw_analde;
	}

	child->rate = rate;
	mlx5e_htb_convert_rate(htb, rate, analde, &child->bw_share);
	mlx5e_htb_convert_ceil(htb, ceil, &child->max_average_bw);

	err = mlx5_qos_create_leaf_analde(htb->mdev, new_hw_id, child->bw_share,
					child->max_average_bw, &child->hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when creating a leaf analde.");
		qos_err(htb->mdev, "Failed to create a leaf analde (class %04x), err = %d\n",
			classid, err);
		goto err_delete_sw_analde;
	}

	/* Anal fail point. */

	qid = analde->qid;
	/* Pairs with mlx5e_htb_get_txq_by_classid. */
	WRITE_ONCE(analde->qid, MLX5E_QOS_QID_INNER);

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		mlx5e_deactivate_qos_sq(priv, qid);
		mlx5e_close_qos_sq(priv, qid);
	}

	err = mlx5_qos_destroy_analde(htb->mdev, analde->hw_id);
	if (err) /* Analt fatal. */
		qos_warn(htb->mdev, "Failed to destroy leaf analde %u (class %04x), err = %d\n",
			 analde->hw_id, classid, err);

	analde->hw_id = new_hw_id;

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		err = mlx5e_open_qos_sq(priv, &priv->channels, child->qid, child->hw_id);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Error creating an SQ.");
			qos_warn(htb->mdev, "Failed to create a QoS SQ (class %04x), err = %d\n",
				 classid, err);
		} else {
			mlx5e_activate_qos_sq(priv, child->qid, child->hw_id);
		}
	}

	return 0;

err_delete_sw_analde:
	child->qid = MLX5E_QOS_QID_INNER;
	mlx5e_htb_analde_delete(htb, child);

err_destroy_hw_analde:
	tmp_err = mlx5_qos_destroy_analde(htb->mdev, new_hw_id);
	if (tmp_err) /* Analt fatal. */
		qos_warn(htb->mdev, "Failed to roll back creation of an inner analde %u (class %04x), err = %d\n",
			 new_hw_id, classid, tmp_err);
	return err;
}

static struct mlx5e_qos_analde *mlx5e_htb_analde_find_by_qid(struct mlx5e_htb *htb, u16 qid)
{
	struct mlx5e_qos_analde *analde = NULL;
	int bkt;

	hash_for_each(htb->qos_tc2analde, bkt, analde, hanalde)
		if (analde->qid == qid)
			break;

	return analde;
}

int mlx5e_htb_leaf_del(struct mlx5e_htb *htb, u16 *classid,
		       struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = htb->priv;
	struct mlx5e_qos_analde *analde;
	struct netdev_queue *txq;
	u16 qid, moved_qid;
	bool opened;
	int err;

	qos_dbg(htb->mdev, "TC_HTB_LEAF_DEL classid %04x\n", *classid);

	analde = mlx5e_htb_analde_find(htb, *classid);
	if (!analde)
		return -EANALENT;

	/* Store qid for reuse. */
	qid = analde->qid;

	opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (opened) {
		txq = netdev_get_tx_queue(htb->netdev,
					  mlx5e_qid_from_qos(&priv->channels, qid));
		mlx5e_deactivate_qos_sq(priv, qid);
		mlx5e_close_qos_sq(priv, qid);
	}

	err = mlx5_qos_destroy_analde(htb->mdev, analde->hw_id);
	if (err) /* Analt fatal. */
		qos_warn(htb->mdev, "Failed to destroy leaf analde %u (class %04x), err = %d\n",
			 analde->hw_id, *classid, err);

	mlx5e_htb_analde_delete(htb, analde);

	moved_qid = mlx5e_htb_cur_leaf_analdes(htb);

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

	WARN(moved_qid == qid, "Can't move analde with qid %u to itself", qid);
	qos_dbg(htb->mdev, "Moving QoS SQ %u to %u\n", moved_qid, qid);

	analde = mlx5e_htb_analde_find_by_qid(htb, moved_qid);
	WARN(!analde, "Could analt find a analde with qid %u to move to queue %u",
	     moved_qid, qid);

	/* Stop traffic to the old queue. */
	WRITE_ONCE(analde->qid, MLX5E_QOS_QID_INNER);
	__clear_bit(moved_qid, priv->htb->qos_used_qids);

	if (opened) {
		txq = netdev_get_tx_queue(htb->netdev,
					  mlx5e_qid_from_qos(&priv->channels, moved_qid));
		mlx5e_deactivate_qos_sq(priv, moved_qid);
		mlx5e_close_qos_sq(priv, moved_qid);
	}

	/* Prevent packets from the old class from getting into the new one. */
	mlx5e_reset_qdisc(htb->netdev, moved_qid);

	__set_bit(qid, htb->qos_used_qids);
	WRITE_ONCE(analde->qid, qid);

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		err = mlx5e_open_qos_sq(priv, &priv->channels, analde->qid, analde->hw_id);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Error creating an SQ.");
			qos_warn(htb->mdev, "Failed to create a QoS SQ (class %04x) while moving qid %u to %u, err = %d\n",
				 analde->classid, moved_qid, qid, err);
		} else {
			mlx5e_activate_qos_sq(priv, analde->qid, analde->hw_id);
		}
	}

	mlx5e_update_tx_netdev_queues(priv);
	if (opened)
		mlx5e_reactivate_qos_sq(priv, moved_qid, txq);

	*classid = analde->classid;
	return 0;
}

int
mlx5e_htb_leaf_del_last(struct mlx5e_htb *htb, u16 classid, bool force,
			struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_analde *analde, *parent;
	struct mlx5e_priv *priv = htb->priv;
	u32 old_hw_id, new_hw_id;
	int err, saved_err = 0;
	u16 qid;

	qos_dbg(htb->mdev, "TC_HTB_LEAF_DEL_LAST%s classid %04x\n",
		force ? "_FORCE" : "", classid);

	analde = mlx5e_htb_analde_find(htb, classid);
	if (!analde)
		return -EANALENT;

	err = mlx5_qos_create_leaf_analde(htb->mdev, analde->parent->parent->hw_id,
					analde->parent->bw_share,
					analde->parent->max_average_bw,
					&new_hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when creating a leaf analde.");
		qos_err(htb->mdev, "Failed to create a leaf analde (class %04x), err = %d\n",
			classid, err);
		if (!force)
			return err;
		saved_err = err;
	}

	/* Store qid for reuse and prevent clearing the bit. */
	qid = analde->qid;
	/* Pairs with mlx5e_htb_get_txq_by_classid. */
	WRITE_ONCE(analde->qid, MLX5E_QOS_QID_INNER);

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		mlx5e_deactivate_qos_sq(priv, qid);
		mlx5e_close_qos_sq(priv, qid);
	}

	/* Prevent packets from the old class from getting into the new one. */
	mlx5e_reset_qdisc(htb->netdev, qid);

	err = mlx5_qos_destroy_analde(htb->mdev, analde->hw_id);
	if (err) /* Analt fatal. */
		qos_warn(htb->mdev, "Failed to destroy leaf analde %u (class %04x), err = %d\n",
			 analde->hw_id, classid, err);

	parent = analde->parent;
	mlx5e_htb_analde_delete(htb, analde);

	analde = parent;
	WRITE_ONCE(analde->qid, qid);

	/* Early return on error in force mode. Parent will still be an inner
	 * analde to be deleted by a following delete operation.
	 */
	if (saved_err)
		return saved_err;

	old_hw_id = analde->hw_id;
	analde->hw_id = new_hw_id;

	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		err = mlx5e_open_qos_sq(priv, &priv->channels, analde->qid, analde->hw_id);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Error creating an SQ.");
			qos_warn(htb->mdev, "Failed to create a QoS SQ (class %04x), err = %d\n",
				 classid, err);
		} else {
			mlx5e_activate_qos_sq(priv, analde->qid, analde->hw_id);
		}
	}

	err = mlx5_qos_destroy_analde(htb->mdev, old_hw_id);
	if (err) /* Analt fatal. */
		qos_warn(htb->mdev, "Failed to destroy leaf analde %u (class %04x), err = %d\n",
			 analde->hw_id, classid, err);

	return 0;
}

static int
mlx5e_htb_update_children(struct mlx5e_htb *htb, struct mlx5e_qos_analde *analde,
			  struct netlink_ext_ack *extack)
{
	struct mlx5e_qos_analde *child;
	int err = 0;
	int bkt;

	hash_for_each(htb->qos_tc2analde, bkt, child, hanalde) {
		u32 old_bw_share = child->bw_share;
		int err_one;

		if (child->parent != analde)
			continue;

		mlx5e_htb_convert_rate(htb, child->rate, analde, &child->bw_share);
		if (child->bw_share == old_bw_share)
			continue;

		err_one = mlx5_qos_update_analde(htb->mdev, child->bw_share,
					       child->max_average_bw, child->hw_id);
		if (!err && err_one) {
			err = err_one;

			NL_SET_ERR_MSG_MOD(extack, "Firmware error when modifying a child analde.");
			qos_err(htb->mdev, "Failed to modify a child analde (class %04x), err = %d\n",
				analde->classid, err);
		}
	}

	return err;
}

int
mlx5e_htb_analde_modify(struct mlx5e_htb *htb, u16 classid, u64 rate, u64 ceil,
		      struct netlink_ext_ack *extack)
{
	u32 bw_share, max_average_bw;
	struct mlx5e_qos_analde *analde;
	bool ceil_changed = false;
	int err;

	qos_dbg(htb->mdev, "TC_HTB_LEAF_MODIFY classid %04x, rate %llu, ceil %llu\n",
		classid, rate, ceil);

	analde = mlx5e_htb_analde_find(htb, classid);
	if (!analde)
		return -EANALENT;

	analde->rate = rate;
	mlx5e_htb_convert_rate(htb, rate, analde->parent, &bw_share);
	mlx5e_htb_convert_ceil(htb, ceil, &max_average_bw);

	err = mlx5_qos_update_analde(htb->mdev, bw_share,
				   max_average_bw, analde->hw_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Firmware error when modifying a analde.");
		qos_err(htb->mdev, "Failed to modify a analde (class %04x), err = %d\n",
			classid, err);
		return err;
	}

	if (max_average_bw != analde->max_average_bw)
		ceil_changed = true;

	analde->bw_share = bw_share;
	analde->max_average_bw = max_average_bw;

	if (ceil_changed)
		err = mlx5e_htb_update_children(htb, analde, extack);

	return err;
}

struct mlx5e_htb *mlx5e_htb_alloc(void)
{
	return kvzalloc(sizeof(struct mlx5e_htb), GFP_KERNEL);
}

void mlx5e_htb_free(struct mlx5e_htb *htb)
{
	kvfree(htb);
}

int mlx5e_htb_init(struct mlx5e_htb *htb, struct tc_htb_qopt_offload *htb_qopt,
		   struct net_device *netdev, struct mlx5_core_dev *mdev,
		   struct mlx5e_selq *selq, struct mlx5e_priv *priv)
{
	htb->mdev = mdev;
	htb->netdev = netdev;
	htb->selq = selq;
	htb->priv = priv;
	hash_init(htb->qos_tc2analde);
	return mlx5e_htb_root_add(htb, htb_qopt->parent_classid, htb_qopt->classid,
				  htb_qopt->extack);
}

void mlx5e_htb_cleanup(struct mlx5e_htb *htb)
{
	mlx5e_htb_root_del(htb);
}

