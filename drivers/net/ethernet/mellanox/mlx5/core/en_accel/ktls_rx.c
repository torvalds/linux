// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include <net/inet6_hashtables.h>
#include "en_accel/en_accel.h"
#include "en_accel/tls.h"
#include "en_accel/ktls_txrx.h"
#include "en_accel/ktls_utils.h"
#include "en_accel/fs_tcp.h"

struct accel_rule {
	struct work_struct work;
	struct mlx5e_priv *priv;
	struct mlx5_flow_handle *rule;
};

#define PROGRESS_PARAMS_WRITE_UNIT	64
#define PROGRESS_PARAMS_PADDED_SIZE	\
		(ALIGN(sizeof(struct mlx5_wqe_tls_progress_params_seg), \
		       PROGRESS_PARAMS_WRITE_UNIT))

struct mlx5e_ktls_rx_resync_buf {
	union {
		struct mlx5_wqe_tls_progress_params_seg progress;
		u8 pad[PROGRESS_PARAMS_PADDED_SIZE];
	} ____cacheline_aligned_in_smp;
	dma_addr_t dma_addr;
	struct mlx5e_ktls_offload_context_rx *priv_rx;
};

enum {
	MLX5E_PRIV_RX_FLAG_DELETING,
	MLX5E_NUM_PRIV_RX_FLAGS,
};

struct mlx5e_ktls_rx_resync_ctx {
	struct tls_offload_resync_async core;
	struct work_struct work;
	struct mlx5e_priv *priv;
	refcount_t refcnt;
	__be64 sw_rcd_sn_be;
	u32 seq;
};

struct mlx5e_ktls_offload_context_rx {
	struct tls12_crypto_info_aes_gcm_128 crypto_info;
	struct accel_rule rule;
	struct sock *sk;
	struct mlx5e_rq_stats *stats;
	struct completion add_ctx;
	u32 tirn;
	u32 key_id;
	u32 rxq;
	DECLARE_BITMAP(flags, MLX5E_NUM_PRIV_RX_FLAGS);

	/* resync */
	struct mlx5e_ktls_rx_resync_ctx resync;
};

static bool mlx5e_ktls_priv_rx_put(struct mlx5e_ktls_offload_context_rx *priv_rx)
{
	if (!refcount_dec_and_test(&priv_rx->resync.refcnt))
		return false;

	kfree(priv_rx);
	return true;
}

static void mlx5e_ktls_priv_rx_get(struct mlx5e_ktls_offload_context_rx *priv_rx)
{
	refcount_inc(&priv_rx->resync.refcnt);
}

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
				       priv_rx->tirn, priv_rx->key_id,
				       priv_rx->resync.seq, false,
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
	spin_lock_bh(&c->async_icosq_lock);

	cseg = post_static_params(sq, priv_rx);
	if (IS_ERR(cseg))
		goto err_out;
	cseg = post_progress_params(sq, priv_rx, next_record_tcp_sn);
	if (IS_ERR(cseg))
		goto err_out;

	mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, cseg);
unlock:
	spin_unlock_bh(&c->async_icosq_lock);

	return err;

err_out:
	priv_rx->stats->tls_resync_req_skip++;
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

/* Re-sync */
/* Runs in work context */
static int
resync_post_get_progress_params(struct mlx5e_icosq *sq,
				struct mlx5e_ktls_offload_context_rx *priv_rx)
{
	struct mlx5e_get_tls_progress_params_wqe *wqe;
	struct mlx5e_ktls_rx_resync_buf *buf;
	struct mlx5e_icosq_wqe_info wi;
	struct mlx5_wqe_ctrl_seg *cseg;
	struct mlx5_seg_get_psv *psv;
	struct device *pdev;
	int err;
	u16 pi;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (unlikely(!buf)) {
		err = -ENOMEM;
		goto err_out;
	}

	pdev = mlx5_core_dma_dev(sq->channel->priv->mdev);
	buf->dma_addr = dma_map_single(pdev, &buf->progress,
				       PROGRESS_PARAMS_PADDED_SIZE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(pdev, buf->dma_addr))) {
		err = -ENOMEM;
		goto err_free;
	}

	buf->priv_rx = priv_rx;

	BUILD_BUG_ON(MLX5E_KTLS_GET_PROGRESS_WQEBBS != 1);

	spin_lock_bh(&sq->channel->async_icosq_lock);

	if (unlikely(!mlx5e_wqc_has_room_for(&sq->wq, sq->cc, sq->pc, 1))) {
		spin_unlock_bh(&sq->channel->async_icosq_lock);
		err = -ENOSPC;
		goto err_dma_unmap;
	}

	pi = mlx5e_icosq_get_next_pi(sq, 1);
	wqe = MLX5E_TLS_FETCH_GET_PROGRESS_PARAMS_WQE(sq, pi);

#define GET_PSV_DS_CNT (DIV_ROUND_UP(sizeof(*wqe), MLX5_SEND_WQE_DS))

	cseg = &wqe->ctrl;
	cseg->opmod_idx_opcode =
		cpu_to_be32((sq->pc << 8) | MLX5_OPCODE_GET_PSV |
			    (MLX5_OPC_MOD_TLS_TIR_PROGRESS_PARAMS << 24));
	cseg->qpn_ds =
		cpu_to_be32((sq->sqn << MLX5_WQE_CTRL_QPN_SHIFT) | GET_PSV_DS_CNT);

	psv = &wqe->psv;
	psv->num_psv      = 1 << 4;
	psv->l_key        = sq->channel->mkey_be;
	psv->psv_index[0] = cpu_to_be32(priv_rx->tirn);
	psv->va           = cpu_to_be64(buf->dma_addr);

	wi = (struct mlx5e_icosq_wqe_info) {
		.wqe_type = MLX5E_ICOSQ_WQE_GET_PSV_TLS,
		.num_wqebbs = 1,
		.tls_get_params.buf = buf,
	};
	icosq_fill_wi(sq, pi, &wi);
	sq->pc++;
	mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, cseg);
	spin_unlock_bh(&sq->channel->async_icosq_lock);

	return 0;

err_dma_unmap:
	dma_unmap_single(pdev, buf->dma_addr, PROGRESS_PARAMS_PADDED_SIZE, DMA_FROM_DEVICE);
err_free:
	kfree(buf);
err_out:
	priv_rx->stats->tls_resync_req_skip++;
	return err;
}

/* Function is called with elevated refcount.
 * It decreases it only if no WQE is posted.
 */
static void resync_handle_work(struct work_struct *work)
{
	struct mlx5e_ktls_offload_context_rx *priv_rx;
	struct mlx5e_ktls_rx_resync_ctx *resync;
	struct mlx5e_channel *c;
	struct mlx5e_icosq *sq;

	resync = container_of(work, struct mlx5e_ktls_rx_resync_ctx, work);
	priv_rx = container_of(resync, struct mlx5e_ktls_offload_context_rx, resync);

	if (unlikely(test_bit(MLX5E_PRIV_RX_FLAG_DELETING, priv_rx->flags))) {
		mlx5e_ktls_priv_rx_put(priv_rx);
		return;
	}

	c = resync->priv->channels.c[priv_rx->rxq];
	sq = &c->async_icosq;

	if (resync_post_get_progress_params(sq, priv_rx))
		mlx5e_ktls_priv_rx_put(priv_rx);
}

static void resync_init(struct mlx5e_ktls_rx_resync_ctx *resync,
			struct mlx5e_priv *priv)
{
	INIT_WORK(&resync->work, resync_handle_work);
	resync->priv = priv;
	refcount_set(&resync->refcnt, 1);
}

/* Function can be called with the refcount being either elevated or not.
 * It does not affect the refcount.
 */
static int resync_handle_seq_match(struct mlx5e_ktls_offload_context_rx *priv_rx,
				   struct mlx5e_channel *c)
{
	struct tls12_crypto_info_aes_gcm_128 *info = &priv_rx->crypto_info;
	struct mlx5_wqe_ctrl_seg *cseg;
	struct mlx5e_icosq *sq;
	int err;

	memcpy(info->rec_seq, &priv_rx->resync.sw_rcd_sn_be, sizeof(info->rec_seq));
	err = 0;

	sq = &c->async_icosq;
	spin_lock_bh(&c->async_icosq_lock);

	cseg = post_static_params(sq, priv_rx);
	if (IS_ERR(cseg)) {
		priv_rx->stats->tls_resync_res_skip++;
		err = PTR_ERR(cseg);
		goto unlock;
	}
	/* Do not increment priv_rx refcnt, CQE handling is empty */
	mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, cseg);
	priv_rx->stats->tls_resync_res_ok++;
unlock:
	spin_unlock_bh(&c->async_icosq_lock);

	return err;
}

/* Function can be called with the refcount being either elevated or not.
 * It decreases the refcount and may free the kTLS priv context.
 * Refcount is not elevated only if tls_dev_del has been called, but GET_PSV was
 * already in flight.
 */
void mlx5e_ktls_handle_get_psv_completion(struct mlx5e_icosq_wqe_info *wi,
					  struct mlx5e_icosq *sq)
{
	struct mlx5e_ktls_rx_resync_buf *buf = wi->tls_get_params.buf;
	struct mlx5e_ktls_offload_context_rx *priv_rx;
	struct mlx5e_ktls_rx_resync_ctx *resync;
	u8 tracker_state, auth_state, *ctx;
	struct device *dev;
	u32 hw_seq;

	priv_rx = buf->priv_rx;
	resync = &priv_rx->resync;
	dev = mlx5_core_dma_dev(resync->priv->mdev);
	if (unlikely(test_bit(MLX5E_PRIV_RX_FLAG_DELETING, priv_rx->flags)))
		goto out;

	dma_sync_single_for_cpu(dev, buf->dma_addr, PROGRESS_PARAMS_PADDED_SIZE,
				DMA_FROM_DEVICE);

	ctx = buf->progress.ctx;
	tracker_state = MLX5_GET(tls_progress_params, ctx, record_tracker_state);
	auth_state = MLX5_GET(tls_progress_params, ctx, auth_state);
	if (tracker_state != MLX5E_TLS_PROGRESS_PARAMS_RECORD_TRACKER_STATE_TRACKING ||
	    auth_state != MLX5E_TLS_PROGRESS_PARAMS_AUTH_STATE_NO_OFFLOAD) {
		priv_rx->stats->tls_resync_req_skip++;
		goto out;
	}

	hw_seq = MLX5_GET(tls_progress_params, ctx, hw_resync_tcp_sn);
	tls_offload_rx_resync_async_request_end(priv_rx->sk, cpu_to_be32(hw_seq));
	priv_rx->stats->tls_resync_req_end++;
out:
	mlx5e_ktls_priv_rx_put(priv_rx);
	dma_unmap_single(dev, buf->dma_addr, PROGRESS_PARAMS_PADDED_SIZE, DMA_FROM_DEVICE);
	kfree(buf);
}

/* Runs in NAPI.
 * Function elevates the refcount, unless no work is queued.
 */
static bool resync_queue_get_psv(struct sock *sk)
{
	struct mlx5e_ktls_offload_context_rx *priv_rx;
	struct mlx5e_ktls_rx_resync_ctx *resync;

	priv_rx = mlx5e_get_ktls_rx_priv_ctx(tls_get_ctx(sk));
	if (unlikely(!priv_rx))
		return false;

	if (unlikely(test_bit(MLX5E_PRIV_RX_FLAG_DELETING, priv_rx->flags)))
		return false;

	resync = &priv_rx->resync;
	mlx5e_ktls_priv_rx_get(priv_rx);
	if (unlikely(!queue_work(resync->priv->tls->rx_wq, &resync->work)))
		mlx5e_ktls_priv_rx_put(priv_rx);

	return true;
}

/* Runs in NAPI */
static void resync_update_sn(struct mlx5e_rq *rq, struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)(skb->data);
	struct net_device *netdev = rq->netdev;
	struct sock *sk = NULL;
	unsigned int datalen;
	struct iphdr *iph;
	struct tcphdr *th;
	__be32 seq;
	int depth = 0;

	__vlan_get_protocol(skb, eth->h_proto, &depth);
	iph = (struct iphdr *)(skb->data + depth);

	if (iph->version == 4) {
		depth += sizeof(struct iphdr);
		th = (void *)iph + sizeof(struct iphdr);

		sk = inet_lookup_established(dev_net(netdev), &tcp_hashinfo,
					     iph->saddr, th->source, iph->daddr,
					     th->dest, netdev->ifindex);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		struct ipv6hdr *ipv6h = (struct ipv6hdr *)iph;

		depth += sizeof(struct ipv6hdr);
		th = (void *)ipv6h + sizeof(struct ipv6hdr);

		sk = __inet6_lookup_established(dev_net(netdev), &tcp_hashinfo,
						&ipv6h->saddr, th->source,
						&ipv6h->daddr, ntohs(th->dest),
						netdev->ifindex, 0);
#endif
	}

	depth += sizeof(struct tcphdr);

	if (unlikely(!sk))
		return;

	if (unlikely(sk->sk_state == TCP_TIME_WAIT))
		goto unref;

	if (unlikely(!resync_queue_get_psv(sk)))
		goto unref;

	seq = th->seq;
	datalen = skb->len - depth;
	tls_offload_rx_resync_async_request_start(sk, seq, datalen);
	rq->stats->tls_resync_req_start++;

unref:
	sock_gen_put(sk);
}

void mlx5e_ktls_rx_resync(struct net_device *netdev, struct sock *sk,
			  u32 seq, u8 *rcd_sn)
{
	struct mlx5e_ktls_offload_context_rx *priv_rx;
	struct mlx5e_ktls_rx_resync_ctx *resync;
	struct mlx5e_priv *priv;
	struct mlx5e_channel *c;

	priv_rx = mlx5e_get_ktls_rx_priv_ctx(tls_get_ctx(sk));
	if (unlikely(!priv_rx))
		return;

	resync = &priv_rx->resync;
	resync->sw_rcd_sn_be = *(__be64 *)rcd_sn;
	resync->seq = seq;

	priv = netdev_priv(netdev);
	c = priv->channels.c[priv_rx->rxq];

	resync_handle_seq_match(priv_rx, c);
}

/* End of resync section */

void mlx5e_ktls_handle_rx_skb(struct mlx5e_rq *rq, struct sk_buff *skb,
			      struct mlx5_cqe64 *cqe, u32 *cqe_bcnt)
{
	struct mlx5e_rq_stats *stats = rq->stats;

	switch (get_cqe_tls_offload(cqe)) {
	case CQE_TLS_OFFLOAD_DECRYPTED:
		skb->decrypted = 1;
		stats->tls_decrypted_packets++;
		stats->tls_decrypted_bytes += *cqe_bcnt;
		break;
	case CQE_TLS_OFFLOAD_RESYNC:
		stats->tls_resync_req_pkt++;
		resync_update_sn(rq, skb);
		break;
	default: /* CQE_TLS_OFFLOAD_ERROR: */
		stats->tls_err++;
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

static int mlx5e_ktls_sk_get_rxq(struct sock *sk)
{
	int rxq = sk_rx_queue_get(sk);

	if (unlikely(rxq == -1))
		rxq = 0;

	return rxq;
}

int mlx5e_ktls_add_rx(struct net_device *netdev, struct sock *sk,
		      struct tls_crypto_info *crypto_info,
		      u32 start_offload_tcp_sn)
{
	struct mlx5e_ktls_offload_context_rx *priv_rx;
	struct mlx5e_ktls_rx_resync_ctx *resync;
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

	rxq = mlx5e_ktls_sk_get_rxq(sk);
	priv_rx->rxq = rxq;
	priv_rx->sk = sk;

	priv_rx->stats = &priv->channel_stats[rxq].rq;
	mlx5e_set_ktls_rx_priv_ctx(tls_ctx, priv_rx);

	rqtn = priv->direct_tir[rxq].rqt.rqtn;

	err = mlx5e_ktls_create_tir(mdev, &priv_rx->tirn, rqtn);
	if (err)
		goto err_create_tir;

	init_completion(&priv_rx->add_ctx);

	accel_rule_init(&priv_rx->rule, priv, sk);
	resync = &priv_rx->resync;
	resync_init(resync, priv);
	tls_offload_ctx_rx(tls_ctx)->resync_async = &resync->core;
	tls_offload_rx_resync_set_type(sk, TLS_OFFLOAD_SYNC_TYPE_DRIVER_REQ_ASYNC);

	err = post_rx_param_wqes(priv->channels.c[rxq], priv_rx, start_offload_tcp_sn);
	if (err)
		goto err_post_wqes;

	priv_rx->stats->tls_ctx++;

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
	struct mlx5e_ktls_rx_resync_ctx *resync;
	struct mlx5_core_dev *mdev;
	struct mlx5e_priv *priv;

	priv = netdev_priv(netdev);
	mdev = priv->mdev;

	priv_rx = mlx5e_get_ktls_rx_priv_ctx(tls_ctx);
	set_bit(MLX5E_PRIV_RX_FLAG_DELETING, priv_rx->flags);
	mlx5e_set_ktls_rx_priv_ctx(tls_ctx, NULL);
	synchronize_net(); /* Sync with NAPI */
	if (!cancel_work_sync(&priv_rx->rule.work))
		/* completion is needed, as the priv_rx in the add flow
		 * is maintained on the wqe info (wi), not on the socket.
		 */
		wait_for_completion(&priv_rx->add_ctx);
	resync = &priv_rx->resync;
	if (cancel_work_sync(&resync->work))
		mlx5e_ktls_priv_rx_put(priv_rx);

	priv_rx->stats->tls_del++;
	if (priv_rx->rule.rule)
		mlx5e_accel_fs_del_sk(priv_rx->rule.rule);

	mlx5_core_destroy_tir(mdev, priv_rx->tirn);
	mlx5_ktls_destroy_key(mdev, priv_rx->key_id);
	/* priv_rx should normally be freed here, but if there is an outstanding
	 * GET_PSV, deallocation will be delayed until the CQE for GET_PSV is
	 * processed.
	 */
	mlx5e_ktls_priv_rx_put(priv_rx);
}
