// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include "en_accel/tls.h"
#include "en_accel/ktls_txrx.h"
#include "en_accel/ktls_utils.h"

struct mlx5e_dump_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_data_seg data;
};

#define MLX5E_KTLS_DUMP_WQEBBS \
	(DIV_ROUND_UP(sizeof(struct mlx5e_dump_wqe), MLX5_SEND_WQE_BB))

static u8
mlx5e_ktls_dumps_num_wqes(struct mlx5e_params *params, unsigned int nfrags,
			  unsigned int sync_len)
{
	/* Given the MTU and sync_len, calculates an upper bound for the
	 * number of DUMP WQEs needed for the TX resync of a record.
	 */
	return nfrags + DIV_ROUND_UP(sync_len, MLX5E_SW2HW_MTU(params, params->sw_mtu));
}

u16 mlx5e_ktls_get_stop_room(struct mlx5e_params *params)
{
	u16 num_dumps, stop_room = 0;

	num_dumps = mlx5e_ktls_dumps_num_wqes(params, MAX_SKB_FRAGS, TLS_MAX_PAYLOAD_SIZE);

	stop_room += mlx5e_stop_room_for_wqe(MLX5E_TLS_SET_STATIC_PARAMS_WQEBBS);
	stop_room += mlx5e_stop_room_for_wqe(MLX5E_TLS_SET_PROGRESS_PARAMS_WQEBBS);
	stop_room += num_dumps * mlx5e_stop_room_for_wqe(MLX5E_KTLS_DUMP_WQEBBS);

	return stop_room;
}

static int mlx5e_ktls_create_tis(struct mlx5_core_dev *mdev, u32 *tisn)
{
	u32 in[MLX5_ST_SZ_DW(create_tis_in)] = {};
	void *tisc;

	tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

	MLX5_SET(tisc, tisc, tls_en, 1);

	return mlx5e_create_tis(mdev, in, tisn);
}

struct mlx5e_ktls_offload_context_tx {
	struct tls_offload_context_tx *tx_ctx;
	struct tls12_crypto_info_aes_gcm_128 crypto_info;
	struct mlx5e_tls_sw_stats *sw_stats;
	u32 expected_seq;
	u32 tisn;
	u32 key_id;
	bool ctx_post_pending;
};

static void
mlx5e_set_ktls_tx_priv_ctx(struct tls_context *tls_ctx,
			   struct mlx5e_ktls_offload_context_tx *priv_tx)
{
	struct mlx5e_ktls_offload_context_tx **ctx =
		__tls_driver_ctx(tls_ctx, TLS_OFFLOAD_CTX_DIR_TX);

	BUILD_BUG_ON(sizeof(struct mlx5e_ktls_offload_context_tx *) >
		     TLS_OFFLOAD_CONTEXT_SIZE_TX);

	*ctx = priv_tx;
}

static struct mlx5e_ktls_offload_context_tx *
mlx5e_get_ktls_tx_priv_ctx(struct tls_context *tls_ctx)
{
	struct mlx5e_ktls_offload_context_tx **ctx =
		__tls_driver_ctx(tls_ctx, TLS_OFFLOAD_CTX_DIR_TX);

	return *ctx;
}

int mlx5e_ktls_add_tx(struct net_device *netdev, struct sock *sk,
		      struct tls_crypto_info *crypto_info, u32 start_offload_tcp_sn)
{
	struct mlx5e_ktls_offload_context_tx *priv_tx;
	struct tls_context *tls_ctx;
	struct mlx5_core_dev *mdev;
	struct mlx5e_priv *priv;
	int err;

	tls_ctx = tls_get_ctx(sk);
	priv = netdev_priv(netdev);
	mdev = priv->mdev;

	priv_tx = kzalloc(sizeof(*priv_tx), GFP_KERNEL);
	if (!priv_tx)
		return -ENOMEM;

	err = mlx5_ktls_create_key(mdev, crypto_info, &priv_tx->key_id);
	if (err)
		goto err_create_key;

	priv_tx->sw_stats = &priv->tls->sw_stats;
	priv_tx->expected_seq = start_offload_tcp_sn;
	priv_tx->crypto_info  =
		*(struct tls12_crypto_info_aes_gcm_128 *)crypto_info;
	priv_tx->tx_ctx = tls_offload_ctx_tx(tls_ctx);

	mlx5e_set_ktls_tx_priv_ctx(tls_ctx, priv_tx);

	err = mlx5e_ktls_create_tis(mdev, &priv_tx->tisn);
	if (err)
		goto err_create_tis;

	priv_tx->ctx_post_pending = true;
	atomic64_inc(&priv_tx->sw_stats->tx_tls_ctx);

	return 0;

err_create_tis:
	mlx5_ktls_destroy_key(mdev, priv_tx->key_id);
err_create_key:
	kfree(priv_tx);
	return err;
}

void mlx5e_ktls_del_tx(struct net_device *netdev, struct tls_context *tls_ctx)
{
	struct mlx5e_ktls_offload_context_tx *priv_tx;
	struct mlx5_core_dev *mdev;
	struct mlx5e_priv *priv;

	priv_tx = mlx5e_get_ktls_tx_priv_ctx(tls_ctx);
	priv = netdev_priv(netdev);
	mdev = priv->mdev;

	mlx5e_destroy_tis(mdev, priv_tx->tisn);
	mlx5_ktls_destroy_key(mdev, priv_tx->key_id);
	kfree(priv_tx);
}

static void tx_fill_wi(struct mlx5e_txqsq *sq,
		       u16 pi, u8 num_wqebbs, u32 num_bytes,
		       struct page *page)
{
	struct mlx5e_tx_wqe_info *wi = &sq->db.wqe_info[pi];

	*wi = (struct mlx5e_tx_wqe_info) {
		.num_wqebbs = num_wqebbs,
		.num_bytes  = num_bytes,
		.resync_dump_frag_page = page,
	};
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
	struct mlx5e_set_tls_static_params_wqe *wqe;
	u16 pi, num_wqebbs;

	num_wqebbs = MLX5E_TLS_SET_STATIC_PARAMS_WQEBBS;
	pi = mlx5e_txqsq_get_next_pi(sq, num_wqebbs);
	wqe = MLX5E_TLS_FETCH_SET_STATIC_PARAMS_WQE(sq, pi);
	mlx5e_ktls_build_static_params(wqe, sq->pc, sq->sqn, &priv_tx->crypto_info,
				       priv_tx->tisn, priv_tx->key_id, 0, fence,
				       TLS_OFFLOAD_CTX_DIR_TX);
	tx_fill_wi(sq, pi, num_wqebbs, 0, NULL);
	sq->pc += num_wqebbs;
}

static void
post_progress_params(struct mlx5e_txqsq *sq,
		     struct mlx5e_ktls_offload_context_tx *priv_tx,
		     bool fence)
{
	struct mlx5e_set_tls_progress_params_wqe *wqe;
	u16 pi, num_wqebbs;

	num_wqebbs = MLX5E_TLS_SET_PROGRESS_PARAMS_WQEBBS;
	pi = mlx5e_txqsq_get_next_pi(sq, num_wqebbs);
	wqe = MLX5E_TLS_FETCH_SET_PROGRESS_PARAMS_WQE(sq, pi);
	mlx5e_ktls_build_progress_params(wqe, sq->pc, sq->sqn, priv_tx->tisn, fence, 0,
					 TLS_OFFLOAD_CTX_DIR_TX);
	tx_fill_wi(sq, pi, num_wqebbs, 0, NULL);
	sq->pc += num_wqebbs;
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
	u32 sync_len;
	int nr_frags;
	skb_frag_t frags[MAX_SKB_FRAGS];
};

enum mlx5e_ktls_sync_retval {
	MLX5E_KTLS_SYNC_DONE,
	MLX5E_KTLS_SYNC_FAIL,
	MLX5E_KTLS_SYNC_SKIP_NO_DATA,
};

static enum mlx5e_ktls_sync_retval
tx_sync_info_get(struct mlx5e_ktls_offload_context_tx *priv_tx,
		 u32 tcp_seq, int datalen, struct tx_sync_info *info)
{
	struct tls_offload_context_tx *tx_ctx = priv_tx->tx_ctx;
	enum mlx5e_ktls_sync_retval ret = MLX5E_KTLS_SYNC_DONE;
	struct tls_record_info *record;
	int remaining, i = 0;
	unsigned long flags;
	bool ends_before;

	spin_lock_irqsave(&tx_ctx->lock, flags);
	record = tls_get_record(tx_ctx, tcp_seq, &info->rcd_sn);

	if (unlikely(!record)) {
		ret = MLX5E_KTLS_SYNC_FAIL;
		goto out;
	}

	/* There are the following cases:
	 * 1. packet ends before start marker: bypass offload.
	 * 2. packet starts before start marker and ends after it: drop,
	 *    not supported, breaks contract with kernel.
	 * 3. packet ends before tls record info starts: drop,
	 *    this packet was already acknowledged and its record info
	 *    was released.
	 */
	ends_before = before(tcp_seq + datalen - 1, tls_record_start_seq(record));

	if (unlikely(tls_record_is_start_marker(record))) {
		ret = ends_before ? MLX5E_KTLS_SYNC_SKIP_NO_DATA : MLX5E_KTLS_SYNC_FAIL;
		goto out;
	} else if (ends_before) {
		ret = MLX5E_KTLS_SYNC_FAIL;
		goto out;
	}

	info->sync_len = tcp_seq - tls_record_start_seq(record);
	remaining = info->sync_len;
	while (remaining > 0) {
		skb_frag_t *frag = &record->frags[i];

		get_page(skb_frag_page(frag));
		remaining -= skb_frag_size(frag);
		info->frags[i++] = *frag;
	}
	/* reduce the part which will be sent with the original SKB */
	if (remaining < 0)
		skb_frag_size_add(&info->frags[i - 1], remaining);
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
	struct tls12_crypto_info_aes_gcm_128 *info = &priv_tx->crypto_info;
	__be64 rn_be = cpu_to_be64(rcd_sn);
	bool skip_static_post;
	u16 rec_seq_sz;
	char *rec_seq;

	rec_seq = info->rec_seq;
	rec_seq_sz = sizeof(info->rec_seq);

	skip_static_post = !memcmp(rec_seq, &rn_be, rec_seq_sz);
	if (!skip_static_post)
		memcpy(rec_seq, &rn_be, rec_seq_sz);

	mlx5e_ktls_tx_post_param_wqes(sq, priv_tx, skip_static_post, true);
}

static int
tx_post_resync_dump(struct mlx5e_txqsq *sq, skb_frag_t *frag, u32 tisn, bool first)
{
	struct mlx5_wqe_ctrl_seg *cseg;
	struct mlx5_wqe_data_seg *dseg;
	struct mlx5e_dump_wqe *wqe;
	dma_addr_t dma_addr = 0;
	u16 ds_cnt;
	int fsz;
	u16 pi;

	BUILD_BUG_ON(MLX5E_KTLS_DUMP_WQEBBS != 1);
	pi = mlx5_wq_cyc_ctr2ix(&sq->wq, sq->pc);
	wqe = MLX5E_TLS_FETCH_DUMP_WQE(sq, pi);

	ds_cnt = sizeof(*wqe) / MLX5_SEND_WQE_DS;

	cseg = &wqe->ctrl;
	dseg = &wqe->data;

	cseg->opmod_idx_opcode = cpu_to_be32((sq->pc << 8)  | MLX5_OPCODE_DUMP);
	cseg->qpn_ds           = cpu_to_be32((sq->sqn << 8) | ds_cnt);
	cseg->tis_tir_num      = cpu_to_be32(tisn << 8);
	cseg->fm_ce_se         = first ? MLX5_FENCE_MODE_INITIATOR_SMALL : 0;

	fsz = skb_frag_size(frag);
	dma_addr = skb_frag_dma_map(sq->pdev, frag, 0, fsz,
				    DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(sq->pdev, dma_addr)))
		return -ENOMEM;

	dseg->addr       = cpu_to_be64(dma_addr);
	dseg->lkey       = sq->mkey_be;
	dseg->byte_count = cpu_to_be32(fsz);
	mlx5e_dma_push(sq, dma_addr, fsz, MLX5E_DMA_MAP_PAGE);

	tx_fill_wi(sq, pi, MLX5E_KTLS_DUMP_WQEBBS, fsz, skb_frag_page(frag));
	sq->pc += MLX5E_KTLS_DUMP_WQEBBS;

	return 0;
}

void mlx5e_ktls_tx_handle_resync_dump_comp(struct mlx5e_txqsq *sq,
					   struct mlx5e_tx_wqe_info *wi,
					   u32 *dma_fifo_cc)
{
	struct mlx5e_sq_stats *stats;
	struct mlx5e_sq_dma *dma;

	dma = mlx5e_dma_get(sq, (*dma_fifo_cc)++);
	stats = sq->stats;

	mlx5e_tx_dma_unmap(sq->pdev, dma);
	put_page(wi->resync_dump_frag_page);
	stats->tls_dump_packets++;
	stats->tls_dump_bytes += wi->num_bytes;
}

static void tx_post_fence_nop(struct mlx5e_txqsq *sq)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	u16 pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);

	tx_fill_wi(sq, pi, 1, 0, NULL);

	mlx5e_post_nop_fence(wq, sq->sqn, &sq->pc);
}

static enum mlx5e_ktls_sync_retval
mlx5e_ktls_tx_handle_ooo(struct mlx5e_ktls_offload_context_tx *priv_tx,
			 struct mlx5e_txqsq *sq,
			 int datalen,
			 u32 seq)
{
	struct mlx5e_sq_stats *stats = sq->stats;
	enum mlx5e_ktls_sync_retval ret;
	struct tx_sync_info info = {};
	int i = 0;

	ret = tx_sync_info_get(priv_tx, seq, datalen, &info);
	if (unlikely(ret != MLX5E_KTLS_SYNC_DONE)) {
		if (ret == MLX5E_KTLS_SYNC_SKIP_NO_DATA) {
			stats->tls_skip_no_sync_data++;
			return MLX5E_KTLS_SYNC_SKIP_NO_DATA;
		}
		/* We might get here if a retransmission reaches the driver
		 * after the relevant record is acked.
		 * It should be safe to drop the packet in this case
		 */
		stats->tls_drop_no_sync_data++;
		goto err_out;
	}

	stats->tls_ooo++;

	tx_post_resync_params(sq, priv_tx, info.rcd_sn);

	/* If no dump WQE was sent, we need to have a fence NOP WQE before the
	 * actual data xmit.
	 */
	if (!info.nr_frags) {
		tx_post_fence_nop(sq);
		return MLX5E_KTLS_SYNC_DONE;
	}

	for (; i < info.nr_frags; i++) {
		unsigned int orig_fsz, frag_offset = 0, n = 0;
		skb_frag_t *f = &info.frags[i];

		orig_fsz = skb_frag_size(f);

		do {
			bool fence = !(i || frag_offset);
			unsigned int fsz;

			n++;
			fsz = min_t(unsigned int, sq->hw_mtu, orig_fsz - frag_offset);
			skb_frag_size_set(f, fsz);
			if (tx_post_resync_dump(sq, f, priv_tx->tisn, fence)) {
				page_ref_add(skb_frag_page(f), n - 1);
				goto err_out;
			}

			skb_frag_off_add(f, fsz);
			frag_offset += fsz;
		} while (frag_offset < orig_fsz);

		page_ref_add(skb_frag_page(f), n - 1);
	}

	return MLX5E_KTLS_SYNC_DONE;

err_out:
	for (; i < info.nr_frags; i++)
		/* The put_page() here undoes the page ref obtained in tx_sync_info_get().
		 * Page refs obtained for the DUMP WQEs above (by page_ref_add) will be
		 * released only upon their completions (or in mlx5e_free_txqsq_descs,
		 * if channel closes).
		 */
		put_page(skb_frag_page(&info.frags[i]));

	return MLX5E_KTLS_SYNC_FAIL;
}

bool mlx5e_ktls_handle_tx_skb(struct tls_context *tls_ctx, struct mlx5e_txqsq *sq,
			      struct sk_buff *skb, int datalen,
			      struct mlx5e_accel_tx_tls_state *state)
{
	struct mlx5e_ktls_offload_context_tx *priv_tx;
	struct mlx5e_sq_stats *stats = sq->stats;
	u32 seq;

	priv_tx = mlx5e_get_ktls_tx_priv_ctx(tls_ctx);

	if (unlikely(mlx5e_ktls_tx_offload_test_and_clear_pending(priv_tx))) {
		mlx5e_ktls_tx_post_param_wqes(sq, priv_tx, false, false);
	}

	seq = ntohl(tcp_hdr(skb)->seq);
	if (unlikely(priv_tx->expected_seq != seq)) {
		enum mlx5e_ktls_sync_retval ret =
			mlx5e_ktls_tx_handle_ooo(priv_tx, sq, datalen, seq);

		switch (ret) {
		case MLX5E_KTLS_SYNC_DONE:
			break;
		case MLX5E_KTLS_SYNC_SKIP_NO_DATA:
			if (likely(!skb->decrypted))
				goto out;
			WARN_ON_ONCE(1);
			fallthrough;
		case MLX5E_KTLS_SYNC_FAIL:
			goto err_out;
		}
	}

	priv_tx->expected_seq = seq + datalen;

	state->tls_tisn = priv_tx->tisn;

	stats->tls_encrypted_packets += skb_is_gso(skb) ? skb_shinfo(skb)->gso_segs : 1;
	stats->tls_encrypted_bytes   += datalen;

out:
	return true;

err_out:
	dev_kfree_skb_any(skb);
	return false;
}
