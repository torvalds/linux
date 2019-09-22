// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include <linux/tls.h>
#include "en.h"
#include "en/txrx.h"
#include "en_accel/ktls.h"

enum {
	MLX5E_STATIC_PARAMS_CONTEXT_TLS_1_2 = 0x2,
};

enum {
	MLX5E_ENCRYPTION_STANDARD_TLS = 0x1,
};

#define EXTRACT_INFO_FIELDS do { \
	salt    = info->salt;    \
	rec_seq = info->rec_seq; \
	salt_sz    = sizeof(info->salt);    \
	rec_seq_sz = sizeof(info->rec_seq); \
} while (0)

static void
fill_static_params_ctx(void *ctx, struct mlx5e_ktls_offload_context_tx *priv_tx)
{
	struct tls_crypto_info *crypto_info = priv_tx->crypto_info;
	struct tls12_crypto_info_aes_gcm_128 *info;
	char *initial_rn, *gcm_iv;
	u16 salt_sz, rec_seq_sz;
	char *salt, *rec_seq;
	u8 tls_version;

	if (WARN_ON(crypto_info->cipher_type != TLS_CIPHER_AES_GCM_128))
		return;

	info = (struct tls12_crypto_info_aes_gcm_128 *)crypto_info;
	EXTRACT_INFO_FIELDS;

	gcm_iv      = MLX5_ADDR_OF(tls_static_params, ctx, gcm_iv);
	initial_rn  = MLX5_ADDR_OF(tls_static_params, ctx, initial_record_number);

	memcpy(gcm_iv,      salt,    salt_sz);
	memcpy(initial_rn,  rec_seq, rec_seq_sz);

	tls_version = MLX5E_STATIC_PARAMS_CONTEXT_TLS_1_2;

	MLX5_SET(tls_static_params, ctx, tls_version, tls_version);
	MLX5_SET(tls_static_params, ctx, const_1, 1);
	MLX5_SET(tls_static_params, ctx, const_2, 2);
	MLX5_SET(tls_static_params, ctx, encryption_standard,
		 MLX5E_ENCRYPTION_STANDARD_TLS);
	MLX5_SET(tls_static_params, ctx, dek_index, priv_tx->key_id);
}

static void
build_static_params(struct mlx5e_umr_wqe *wqe, u16 pc, u32 sqn,
		    struct mlx5e_ktls_offload_context_tx *priv_tx,
		    bool fence)
{
	struct mlx5_wqe_ctrl_seg     *cseg  = &wqe->ctrl;
	struct mlx5_wqe_umr_ctrl_seg *ucseg = &wqe->uctrl;

#define STATIC_PARAMS_DS_CNT \
	DIV_ROUND_UP(MLX5E_KTLS_STATIC_UMR_WQE_SZ, MLX5_SEND_WQE_DS)

	cseg->opmod_idx_opcode = cpu_to_be32((pc << 8) | MLX5_OPCODE_UMR |
					     (MLX5_OPC_MOD_TLS_TIS_STATIC_PARAMS << 24));
	cseg->qpn_ds           = cpu_to_be32((sqn << MLX5_WQE_CTRL_QPN_SHIFT) |
					     STATIC_PARAMS_DS_CNT);
	cseg->fm_ce_se         = fence ? MLX5_FENCE_MODE_INITIATOR_SMALL : 0;
	cseg->tisn             = cpu_to_be32(priv_tx->tisn << 8);

	ucseg->flags = MLX5_UMR_INLINE;
	ucseg->bsf_octowords = cpu_to_be16(MLX5_ST_SZ_BYTES(tls_static_params) / 16);

	fill_static_params_ctx(wqe->tls_static_params_ctx, priv_tx);
}

static void
fill_progress_params_ctx(void *ctx, struct mlx5e_ktls_offload_context_tx *priv_tx)
{
	MLX5_SET(tls_progress_params, ctx, tisn, priv_tx->tisn);
	MLX5_SET(tls_progress_params, ctx, record_tracker_state,
		 MLX5E_TLS_PROGRESS_PARAMS_RECORD_TRACKER_STATE_START);
	MLX5_SET(tls_progress_params, ctx, auth_state,
		 MLX5E_TLS_PROGRESS_PARAMS_AUTH_STATE_NO_OFFLOAD);
}

static void
build_progress_params(struct mlx5e_tx_wqe *wqe, u16 pc, u32 sqn,
		      struct mlx5e_ktls_offload_context_tx *priv_tx,
		      bool fence)
{
	struct mlx5_wqe_ctrl_seg *cseg = &wqe->ctrl;

#define PROGRESS_PARAMS_DS_CNT \
	DIV_ROUND_UP(MLX5E_KTLS_PROGRESS_WQE_SZ, MLX5_SEND_WQE_DS)

	cseg->opmod_idx_opcode =
		cpu_to_be32((pc << 8) | MLX5_OPCODE_SET_PSV |
			    (MLX5_OPC_MOD_TLS_TIS_PROGRESS_PARAMS << 24));
	cseg->qpn_ds           = cpu_to_be32((sqn << MLX5_WQE_CTRL_QPN_SHIFT) |
					     PROGRESS_PARAMS_DS_CNT);
	cseg->fm_ce_se         = fence ? MLX5_FENCE_MODE_INITIATOR_SMALL : 0;

	fill_progress_params_ctx(wqe->tls_progress_params_ctx, priv_tx);
}

static void tx_fill_wi(struct mlx5e_txqsq *sq,
		       u16 pi, u8 num_wqebbs,
		       skb_frag_t *resync_dump_frag)
{
	struct mlx5e_tx_wqe_info *wi = &sq->db.wqe_info[pi];

	wi->skb              = NULL;
	wi->num_wqebbs       = num_wqebbs;
	wi->resync_dump_frag = resync_dump_frag;
}

void mlx5e_ktls_tx_offload_set_pending(struct mlx5e_ktls_offload_context_tx *priv_tx)
{
	priv_tx->ctx_post_pending = true;
}

static bool
mlx5e_ktls_tx_offload_test_and_clear_pending(struct mlx5e_ktls_offload_context_tx *priv_tx)
{
	bool ret = priv_tx->ctx_post_pending;

	priv_tx->ctx_post_pending = false;

	return ret;
}

static void
post_static_params(struct mlx5e_txqsq *sq,
		   struct mlx5e_ktls_offload_context_tx *priv_tx,
		   bool fence)
{
	struct mlx5e_umr_wqe *umr_wqe;
	u16 pi;

	umr_wqe = mlx5e_sq_fetch_wqe(sq, MLX5E_KTLS_STATIC_UMR_WQE_SZ, &pi);
	build_static_params(umr_wqe, sq->pc, sq->sqn, priv_tx, fence);
	tx_fill_wi(sq, pi, MLX5E_KTLS_STATIC_WQEBBS, NULL);
	sq->pc += MLX5E_KTLS_STATIC_WQEBBS;
}

static void
post_progress_params(struct mlx5e_txqsq *sq,
		     struct mlx5e_ktls_offload_context_tx *priv_tx,
		     bool fence)
{
	struct mlx5e_tx_wqe *wqe;
	u16 pi;

	wqe = mlx5e_sq_fetch_wqe(sq, MLX5E_KTLS_PROGRESS_WQE_SZ, &pi);
	build_progress_params(wqe, sq->pc, sq->sqn, priv_tx, fence);
	tx_fill_wi(sq, pi, MLX5E_KTLS_PROGRESS_WQEBBS, NULL);
	sq->pc += MLX5E_KTLS_PROGRESS_WQEBBS;
}

static void
mlx5e_ktls_tx_post_param_wqes(struct mlx5e_txqsq *sq,
			      struct mlx5e_ktls_offload_context_tx *priv_tx,
			      bool skip_static_post, bool fence_first_post)
{
	bool progress_fence = skip_static_post || !fence_first_post;

	if (!skip_static_post)
		post_static_params(sq, priv_tx, fence_first_post);

	post_progress_params(sq, priv_tx, progress_fence);
}

struct tx_sync_info {
	u64 rcd_sn;
	s32 sync_len;
	int nr_frags;
	skb_frag_t *frags[MAX_SKB_FRAGS];
};

static bool tx_sync_info_get(struct mlx5e_ktls_offload_context_tx *priv_tx,
			     u32 tcp_seq, struct tx_sync_info *info)
{
	struct tls_offload_context_tx *tx_ctx = priv_tx->tx_ctx;
	struct tls_record_info *record;
	int remaining, i = 0;
	unsigned long flags;
	bool ret = true;

	spin_lock_irqsave(&tx_ctx->lock, flags);
	record = tls_get_record(tx_ctx, tcp_seq, &info->rcd_sn);

	if (unlikely(!record)) {
		ret = false;
		goto out;
	}

	if (unlikely(tcp_seq < tls_record_start_seq(record))) {
		if (!tls_record_is_start_marker(record))
			ret = false;
		goto out;
	}

	info->sync_len = tcp_seq - tls_record_start_seq(record);
	remaining = info->sync_len;
	while (remaining > 0) {
		skb_frag_t *frag = &record->frags[i];

		__skb_frag_ref(frag);
		remaining -= skb_frag_size(frag);
		info->frags[i++] = frag;
	}
	/* reduce the part which will be sent with the original SKB */
	if (remaining < 0)
		skb_frag_size_add(info->frags[i - 1], remaining);
	info->nr_frags = i;
out:
	spin_unlock_irqrestore(&tx_ctx->lock, flags);
	return ret;
}

static void
tx_post_resync_params(struct mlx5e_txqsq *sq,
		      struct mlx5e_ktls_offload_context_tx *priv_tx,
		      u64 rcd_sn)
{
	struct tls_crypto_info *crypto_info = priv_tx->crypto_info;
	struct tls12_crypto_info_aes_gcm_128 *info;
	__be64 rn_be = cpu_to_be64(rcd_sn);
	bool skip_static_post;
	u16 rec_seq_sz;
	char *rec_seq;

	if (WARN_ON(crypto_info->cipher_type != TLS_CIPHER_AES_GCM_128))
		return;

	info = (struct tls12_crypto_info_aes_gcm_128 *)crypto_info;
	rec_seq = info->rec_seq;
	rec_seq_sz = sizeof(info->rec_seq);

	skip_static_post = !memcmp(rec_seq, &rn_be, rec_seq_sz);
	if (!skip_static_post)
		memcpy(rec_seq, &rn_be, rec_seq_sz);

	mlx5e_ktls_tx_post_param_wqes(sq, priv_tx, skip_static_post, true);
}

static int
tx_post_resync_dump(struct mlx5e_txqsq *sq, struct sk_buff *skb,
		    skb_frag_t *frag, u32 tisn, bool first)
{
	struct mlx5_wqe_ctrl_seg *cseg;
	struct mlx5_wqe_eth_seg  *eseg;
	struct mlx5_wqe_data_seg *dseg;
	struct mlx5e_tx_wqe *wqe;
	dma_addr_t dma_addr = 0;
	u16 ds_cnt, ds_cnt_inl;
	u8  num_wqebbs;
	u16 pi, ihs;
	int fsz;

	ds_cnt = sizeof(*wqe) / MLX5_SEND_WQE_DS;
	ihs    = eth_get_headlen(skb->dev, skb->data, skb_headlen(skb));
	ds_cnt_inl = DIV_ROUND_UP(ihs - INL_HDR_START_SZ, MLX5_SEND_WQE_DS);
	ds_cnt += ds_cnt_inl;
	ds_cnt += 1; /* one frag */

	wqe = mlx5e_sq_fetch_wqe(sq, sizeof(*wqe), &pi);

	num_wqebbs = DIV_ROUND_UP(ds_cnt, MLX5_SEND_WQEBB_NUM_DS);

	cseg = &wqe->ctrl;
	eseg = &wqe->eth;
	dseg =  wqe->data;

	cseg->opmod_idx_opcode = cpu_to_be32((sq->pc << 8)  | MLX5_OPCODE_DUMP);
	cseg->qpn_ds           = cpu_to_be32((sq->sqn << 8) | ds_cnt);
	cseg->tisn             = cpu_to_be32(tisn << 8);
	cseg->fm_ce_se         = first ? MLX5_FENCE_MODE_INITIATOR_SMALL : 0;

	eseg->inline_hdr.sz = cpu_to_be16(ihs);
	memcpy(eseg->inline_hdr.start, skb->data, ihs);
	dseg += ds_cnt_inl;

	fsz = skb_frag_size(frag);
	dma_addr = skb_frag_dma_map(sq->pdev, frag, 0, fsz,
				    DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(sq->pdev, dma_addr)))
		return -ENOMEM;

	dseg->addr       = cpu_to_be64(dma_addr);
	dseg->lkey       = sq->mkey_be;
	dseg->byte_count = cpu_to_be32(fsz);
	mlx5e_dma_push(sq, dma_addr, fsz, MLX5E_DMA_MAP_PAGE);

	tx_fill_wi(sq, pi, num_wqebbs, frag);
	sq->pc += num_wqebbs;

	WARN(num_wqebbs > MLX5E_KTLS_MAX_DUMP_WQEBBS,
	     "unexpected DUMP num_wqebbs, %d > %d",
	     num_wqebbs, MLX5E_KTLS_MAX_DUMP_WQEBBS);

	return 0;
}

void mlx5e_ktls_tx_handle_resync_dump_comp(struct mlx5e_txqsq *sq,
					   struct mlx5e_tx_wqe_info *wi,
					   struct mlx5e_sq_dma *dma)
{
	struct mlx5e_sq_stats *stats = sq->stats;

	mlx5e_tx_dma_unmap(sq->pdev, dma);
	__skb_frag_unref(wi->resync_dump_frag);
	stats->tls_dump_packets++;
	stats->tls_dump_bytes += wi->num_bytes;
}

static void tx_post_fence_nop(struct mlx5e_txqsq *sq)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	u16 pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);

	tx_fill_wi(sq, pi, 1, NULL);

	mlx5e_post_nop_fence(wq, sq->sqn, &sq->pc);
}

static struct sk_buff *
mlx5e_ktls_tx_handle_ooo(struct mlx5e_ktls_offload_context_tx *priv_tx,
			 struct mlx5e_txqsq *sq,
			 struct sk_buff *skb,
			 u32 seq)
{
	struct mlx5e_sq_stats *stats = sq->stats;
	struct mlx5_wq_cyc *wq = &sq->wq;
	struct tx_sync_info info = {};
	u16 contig_wqebbs_room, pi;
	u8 num_wqebbs;
	int i;

	if (!tx_sync_info_get(priv_tx, seq, &info)) {
		/* We might get here if a retransmission reaches the driver
		 * after the relevant record is acked.
		 * It should be safe to drop the packet in this case
		 */
		stats->tls_drop_no_sync_data++;
		goto err_out;
	}

	if (unlikely(info.sync_len < 0)) {
		u32 payload;
		int headln;

		headln = skb_transport_offset(skb) + tcp_hdrlen(skb);
		payload = skb->len - headln;
		if (likely(payload <= -info.sync_len))
			return skb;

		stats->tls_drop_bypass_req++;
		goto err_out;
	}

	stats->tls_ooo++;

	num_wqebbs = MLX5E_KTLS_STATIC_WQEBBS + MLX5E_KTLS_PROGRESS_WQEBBS +
		(info.nr_frags ? info.nr_frags * MLX5E_KTLS_MAX_DUMP_WQEBBS : 1);
	pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	contig_wqebbs_room = mlx5_wq_cyc_get_contig_wqebbs(wq, pi);
	if (unlikely(contig_wqebbs_room < num_wqebbs))
		mlx5e_fill_sq_frag_edge(sq, wq, pi, contig_wqebbs_room);

	tx_post_resync_params(sq, priv_tx, info.rcd_sn);

	for (i = 0; i < info.nr_frags; i++)
		if (tx_post_resync_dump(sq, skb, info.frags[i],
					priv_tx->tisn, !i))
			goto err_out;

	/* If no dump WQE was sent, we need to have a fence NOP WQE before the
	 * actual data xmit.
	 */
	if (!info.nr_frags)
		tx_post_fence_nop(sq);

	return skb;

err_out:
	dev_kfree_skb_any(skb);
	return NULL;
}

struct sk_buff *mlx5e_ktls_handle_tx_skb(struct net_device *netdev,
					 struct mlx5e_txqsq *sq,
					 struct sk_buff *skb,
					 struct mlx5e_tx_wqe **wqe, u16 *pi)
{
	struct mlx5e_ktls_offload_context_tx *priv_tx;
	struct mlx5e_sq_stats *stats = sq->stats;
	struct mlx5_wqe_ctrl_seg *cseg;
	struct tls_context *tls_ctx;
	int datalen;
	u32 seq;

	if (!skb->sk || !tls_is_sk_tx_device_offloaded(skb->sk))
		goto out;

	datalen = skb->len - (skb_transport_offset(skb) + tcp_hdrlen(skb));
	if (!datalen)
		goto out;

	tls_ctx = tls_get_ctx(skb->sk);
	if (unlikely(WARN_ON_ONCE(tls_ctx->netdev != netdev)))
		goto err_out;

	priv_tx = mlx5e_get_ktls_tx_priv_ctx(tls_ctx);

	if (unlikely(mlx5e_ktls_tx_offload_test_and_clear_pending(priv_tx))) {
		mlx5e_ktls_tx_post_param_wqes(sq, priv_tx, false, false);
		*wqe = mlx5e_sq_fetch_wqe(sq, sizeof(**wqe), pi);
		stats->tls_ctx++;
	}

	seq = ntohl(tcp_hdr(skb)->seq);
	if (unlikely(priv_tx->expected_seq != seq)) {
		skb = mlx5e_ktls_tx_handle_ooo(priv_tx, sq, skb, seq);
		if (unlikely(!skb))
			goto out;
		*wqe = mlx5e_sq_fetch_wqe(sq, sizeof(**wqe), pi);
	}

	priv_tx->expected_seq = seq + datalen;

	cseg = &(*wqe)->ctrl;
	cseg->tisn = cpu_to_be32(priv_tx->tisn << 8);

	stats->tls_encrypted_packets += skb_is_gso(skb) ? skb_shinfo(skb)->gso_segs : 1;
	stats->tls_encrypted_bytes   += datalen;

out:
	return skb;

err_out:
	dev_kfree_skb_any(skb);
	return NULL;
}
