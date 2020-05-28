// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include "en_accel/en_accel.h"
#include "en_accel/ktls_txrx.h"
#include "en_accel/ktls_utils.h"
#include "en_accel/fs_tcp.h"

struct accel_rule {
	struct work_struct work;
	struct mlx5e_priv *priv;
	struct mlx5_flow_handle *rule;
};

enum {
	MLX5E_PRIV_RX_FLAG_DELETING,
	MLX5E_NUM_PRIV_RX_FLAGS,
};

struct mlx5e_ktls_offload_context_rx {
	struct tls12_crypto_info_aes_gcm_128 crypto_info;
	struct accel_rule rule;
	struct sock *sk;
	struct completion add_ctx;
	u32 tirn;
	u32 key_id;
	u32 rxq;
	DECLARE_BITMAP(flags, MLX5E_NUM_PRIV_RX_FLAGS);
};

static int mlx5e_ktls_create_tir(struct mlx5_core_dev *mdev, u32 *tirn, u32 rqtn)
{
	int err, inlen;
	void *tirc;
	u32 *in;

	inlen = MLX5_ST_SZ_BYTES(create_tir_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);

	MLX5_SET(tirc, tirc, transport_domain, mdev->mlx5e_res.td.tdn);
	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);
	MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_INVERTED_XOR8);
	MLX5_SET(tirc, tirc, indirect_table, rqtn);
	MLX5_SET(tirc, tirc, tls_en, 1);
	MLX5_SET(tirc, tirc, self_lb_block,
		 MLX5_TIRC_SELF_LB_BLOCK_BLOCK_UNICAST |
		 MLX5_TIRC_SELF_LB_BLOCK_BLOCK_MULTICAST);

	err = mlx5_core_create_tir(mdev, in, tirn);

	kvfree(in);
	return err;
}

static void accel_rule_handle_work(struct work_struct *work)
{
	struct mlx5e_ktls_offload_context_rx *priv_rx;
	struct accel_rule *accel_rule;
	struct mlx5_flow_handle *rule;

	accel_rule = container_of(work, struct accel_rule, work);
	priv_rx = container_of(accel_rule, struct mlx5e_ktls_offload_context_rx, rule);
	if (unlikely(test_bit(MLX5E_PRIV_RX_FLAG_DELETING, priv_rx->flags)))
		goto out;

	rule = mlx5e_accel_fs_add_sk(accel_rule->priv, priv_rx->sk,
				     priv_rx->tirn, MLX5_FS_DEFAULT_FLOW_TAG);
	if (!IS_ERR_OR_NULL(rule))
		accel_rule->rule = rule;
out:
	complete(&priv_rx->add_ctx);
}

static void accel_rule_init(struct accel_rule *rule, struct mlx5e_priv *priv,
			    struct sock *sk)
{
	INIT_WORK(&rule->work, accel_rule_handle_work);
	rule->priv = priv;
}

static void icosq_fill_wi(struct mlx5e_icosq *sq, u16 pi,
			  struct mlx5e_icosq_wqe_info *wi)
{
	sq->db.wqe_info[pi] = *wi;
}

static struct mlx5_wqe_ctrl_seg *
post_static_params(struct mlx5e_icosq *sq,
		   struct mlx5e_ktls_offload_context_rx *priv_rx)
{
	struct mlx5e_set_tls_static_params_wqe *wqe;
	struct mlx5e_icosq_wqe_info wi;
	u16 pi, num_wqebbs, room;

	num_wqebbs = MLX5E_TLS_SET_STATIC_PARAMS_WQEBBS;
	room = mlx5e_stop_room_for_wqe(num_wqebbs);
	if (unlikely(!mlx5e_wqc_has_room_for(&sq->wq, sq->cc, sq->pc, room)))
		return ERR_PTR(-ENOSPC);

	pi = mlx5e_icosq_get_next_pi(sq, num_wqebbs);
	wqe = MLX5E_TLS_FETCH_SET_STATIC_PARAMS_WQE(sq, pi);
	mlx5e_ktls_build_static_params(wqe, sq->pc, sq->sqn, &priv_rx->crypto_info,
				       priv_rx->tirn, priv_rx->key_id, false,
				       TLS_OFFLOAD_CTX_DIR_RX);
	wi = (struct mlx5e_icosq_wqe_info) {
		.wqe_type = MLX5E_ICOSQ_WQE_UMR_TLS,
		.num_wqebbs = num_wqebbs,
		.tls_set_params.priv_rx = priv_rx,
	};
	icosq_fill_wi(sq, pi, &wi);
	sq->pc += num_wqebbs;

	return &wqe->ctrl;
}

static struct mlx5_wqe_ctrl_seg *
post_progress_params(struct mlx5e_icosq *sq,
		     struct mlx5e_ktls_offload_context_rx *priv_rx,
		     u32 next_record_tcp_sn)
{
	struct mlx5e_set_tls_progress_params_wqe *wqe;
	struct mlx5e_icosq_wqe_info wi;
	u16 pi, num_wqebbs, room;

	num_wqebbs = MLX5E_TLS_SET_PROGRESS_PARAMS_WQEBBS;
	room = mlx5e_stop_room_for_wqe(num_wqebbs);
	if (unlikely(!mlx5e_wqc_has_room_for(&sq->wq, sq->cc, sq->pc, room)))
		return ERR_PTR(-ENOSPC);

	pi = mlx5e_icosq_get_next_pi(sq, num_wqebbs);
	wqe = MLX5E_TLS_FETCH_SET_PROGRESS_PARAMS_WQE(sq, pi);
	mlx5e_ktls_build_progress_params(wqe, sq->pc, sq->sqn, priv_rx->tirn, false,
					 next_record_tcp_sn,
					 TLS_OFFLOAD_CTX_DIR_RX);
	wi = (struct mlx5e_icosq_wqe_info) {
		.wqe_type = MLX5E_ICOSQ_WQE_SET_PSV_TLS,
		.num_wqebbs = num_wqebbs,
		.tls_set_params.priv_rx = priv_rx,
	};

	icosq_fill_wi(sq, pi, &wi);
	sq->pc += num_wqebbs;

	return &wqe->ctrl;
}

static int post_rx_param_wqes(struct mlx5e_channel *c,
			      struct mlx5e_ktls_offload_context_rx *priv_rx,
			      u32 next_record_tcp_sn)
{
	struct mlx5_wqe_ctrl_seg *cseg;
	struct mlx5e_icosq *sq;
	int err;

	err = 0;
	sq = &c->async_icosq;
	spin_lock(&c->async_icosq_lock);

	cseg = post_static_params(sq, priv_rx);
	if (IS_ERR(cseg))
		goto err_out;
	cseg = post_progress_params(sq, priv_rx, next_record_tcp_sn);
	if (IS_ERR(cseg))
		goto err_out;

	mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, cseg);
unlock:
	spin_unlock(&c->async_icosq_lock);

	return err;

err_out:
	err = PTR_ERR(cseg);
	complete(&priv_rx->add_ctx);
	goto unlock;
}

static void
mlx5e_set_ktls_rx_priv_ctx(struct tls_context *tls_ctx,
			   struct mlx5e_ktls_offload_context_rx *priv_rx)
{
	struct mlx5e_ktls_offload_context_rx **ctx =
		__tls_driver_ctx(tls_ctx, TLS_OFFLOAD_CTX_DIR_RX);

	BUILD_BUG_ON(sizeof(struct mlx5e_ktls_offload_context_rx *) >
		     TLS_OFFLOAD_CONTEXT_SIZE_RX);

	*ctx = priv_rx;
}

static struct mlx5e_ktls_offload_context_rx *
mlx5e_get_ktls_rx_priv_ctx(struct tls_context *tls_ctx)
{
	struct mlx5e_ktls_offload_context_rx **ctx =
		__tls_driver_ctx(tls_ctx, TLS_OFFLOAD_CTX_DIR_RX);

	return *ctx;
}

void mlx5e_ktls_handle_rx_skb(struct mlx5e_rq *rq, struct sk_buff *skb,
			      struct mlx5_cqe64 *cqe, u32 *cqe_bcnt)
{
	u8 tls_offload = get_cqe_tls_offload(cqe);

	if (likely(tls_offload == CQE_TLS_OFFLOAD_NOT_DECRYPTED))
		return;

	switch (tls_offload) {
	case CQE_TLS_OFFLOAD_DECRYPTED:
		skb->decrypted = 1;
		break;
	case CQE_TLS_OFFLOAD_RESYNC:
		break;
	default: /* CQE_TLS_OFFLOAD_ERROR: */
		break;
	}
}

void mlx5e_ktls_handle_ctx_completion(struct mlx5e_icosq_wqe_info *wi)
{
	struct mlx5e_ktls_offload_context_rx *priv_rx = wi->tls_set_params.priv_rx;
	struct accel_rule *rule = &priv_rx->rule;

	if (unlikely(test_bit(MLX5E_PRIV_RX_FLAG_DELETING, priv_rx->flags))) {
		complete(&priv_rx->add_ctx);
		return;
	}
	queue_work(rule->priv->tls->rx_wq, &rule->work);
}

int mlx5e_ktls_add_rx(struct net_device *netdev, struct sock *sk,
		      struct tls_crypto_info *crypto_info,
		      u32 start_offload_tcp_sn)
{
	struct mlx5e_ktls_offload_context_rx *priv_rx;
	struct tls_context *tls_ctx;
	struct mlx5_core_dev *mdev;
	struct mlx5e_priv *priv;
	int rxq, err;
	u32 rqtn;

	tls_ctx = tls_get_ctx(sk);
	priv = netdev_priv(netdev);
	mdev = priv->mdev;
	priv_rx = kzalloc(sizeof(*priv_rx), GFP_KERNEL);
	if (unlikely(!priv_rx))
		return -ENOMEM;

	err = mlx5_ktls_create_key(mdev, crypto_info, &priv_rx->key_id);
	if (err)
		goto err_create_key;

	priv_rx->crypto_info  =
		*(struct tls12_crypto_info_aes_gcm_128 *)crypto_info;
	priv_rx->sk = sk;
	priv_rx->rxq = mlx5e_accel_sk_get_rxq(sk);

	mlx5e_set_ktls_rx_priv_ctx(tls_ctx, priv_rx);

	rxq = priv_rx->rxq;
	rqtn = priv->direct_tir[rxq].rqt.rqtn;

	err = mlx5e_ktls_create_tir(mdev, &priv_rx->tirn, rqtn);
	if (err)
		goto err_create_tir;

	init_completion(&priv_rx->add_ctx);
	accel_rule_init(&priv_rx->rule, priv, sk);
	err = post_rx_param_wqes(priv->channels.c[rxq], priv_rx, start_offload_tcp_sn);
	if (err)
		goto err_post_wqes;

	return 0;

err_post_wqes:
	mlx5_core_destroy_tir(mdev, priv_rx->tirn);
err_create_tir:
	mlx5_ktls_destroy_key(mdev, priv_rx->key_id);
err_create_key:
	kfree(priv_rx);
	return err;
}

void mlx5e_ktls_del_rx(struct net_device *netdev, struct tls_context *tls_ctx)
{
	struct mlx5e_ktls_offload_context_rx *priv_rx;
	struct mlx5_core_dev *mdev;
	struct mlx5e_priv *priv;

	priv = netdev_priv(netdev);
	mdev = priv->mdev;

	priv_rx = mlx5e_get_ktls_rx_priv_ctx(tls_ctx);
	set_bit(MLX5E_PRIV_RX_FLAG_DELETING, priv_rx->flags);
	if (!cancel_work_sync(&priv_rx->rule.work))
		/* completion is needed, as the priv_rx in the add flow
		 * is maintained on the wqe info (wi), not on the socket.
		 */
		wait_for_completion(&priv_rx->add_ctx);

	if (priv_rx->rule.rule)
		mlx5e_accel_fs_del_sk(priv_rx->rule.rule);

	mlx5_core_destroy_tir(mdev, priv_rx->tirn);
	mlx5_ktls_destroy_key(mdev, priv_rx->key_id);
	kfree(priv_rx);
}
