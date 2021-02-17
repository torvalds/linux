/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
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

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <net/ip6_checksum.h>
#include <net/page_pool.h>
#include <net/inet_ecn.h>
#include "en.h"
#include "en/txrx.h"
#include "en_tc.h"
#include "eswitch.h"
#include "en_rep.h"
#include "en/rep/tc.h"
#include "ipoib/ipoib.h"
#include "accel/ipsec.h"
#include "fpga/ipsec.h"
#include "en_accel/ipsec_rxtx.h"
#include "en_accel/tls_rxtx.h"
#include "en/xdp.h"
#include "en/xsk/rx.h"
#include "en/health.h"
#include "en/params.h"
#include "devlink.h"

static struct sk_buff *
mlx5e_skb_from_cqe_mpwrq_linear(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi,
				u16 cqe_bcnt, u32 head_offset, u32 page_idx);
static struct sk_buff *
mlx5e_skb_from_cqe_mpwrq_nonlinear(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi,
				   u16 cqe_bcnt, u32 head_offset, u32 page_idx);
static void mlx5e_handle_rx_cqe(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);
static void mlx5e_handle_rx_cqe_mpwrq(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);

const struct mlx5e_rx_handlers mlx5e_rx_handlers_nic = {
	.handle_rx_cqe       = mlx5e_handle_rx_cqe,
	.handle_rx_cqe_mpwqe = mlx5e_handle_rx_cqe_mpwrq,
};

static inline bool mlx5e_rx_hw_stamp(struct hwtstamp_config *config)
{
	return config->rx_filter == HWTSTAMP_FILTER_ALL;
}

static inline void mlx5e_read_cqe_slot(struct mlx5_cqwq *wq,
				       u32 cqcc, void *data)
{
	u32 ci = mlx5_cqwq_ctr2ix(wq, cqcc);

	memcpy(data, mlx5_cqwq_get_wqe(wq, ci), sizeof(struct mlx5_cqe64));
}

static inline void mlx5e_read_title_slot(struct mlx5e_rq *rq,
					 struct mlx5_cqwq *wq,
					 u32 cqcc)
{
	struct mlx5e_cq_decomp *cqd = &rq->cqd;
	struct mlx5_cqe64 *title = &cqd->title;

	mlx5e_read_cqe_slot(wq, cqcc, title);
	cqd->left        = be32_to_cpu(title->byte_cnt);
	cqd->wqe_counter = be16_to_cpu(title->wqe_counter);
	rq->stats->cqe_compress_blks++;
}

static inline void mlx5e_read_mini_arr_slot(struct mlx5_cqwq *wq,
					    struct mlx5e_cq_decomp *cqd,
					    u32 cqcc)
{
	mlx5e_read_cqe_slot(wq, cqcc, cqd->mini_arr);
	cqd->mini_arr_idx = 0;
}

static inline void mlx5e_cqes_update_owner(struct mlx5_cqwq *wq, int n)
{
	u32 cqcc   = wq->cc;
	u8  op_own = mlx5_cqwq_get_ctr_wrap_cnt(wq, cqcc) & 1;
	u32 ci     = mlx5_cqwq_ctr2ix(wq, cqcc);
	u32 wq_sz  = mlx5_cqwq_get_size(wq);
	u32 ci_top = min_t(u32, wq_sz, ci + n);

	for (; ci < ci_top; ci++, n--) {
		struct mlx5_cqe64 *cqe = mlx5_cqwq_get_wqe(wq, ci);

		cqe->op_own = op_own;
	}

	if (unlikely(ci == wq_sz)) {
		op_own = !op_own;
		for (ci = 0; ci < n; ci++) {
			struct mlx5_cqe64 *cqe = mlx5_cqwq_get_wqe(wq, ci);

			cqe->op_own = op_own;
		}
	}
}

static inline void mlx5e_decompress_cqe(struct mlx5e_rq *rq,
					struct mlx5_cqwq *wq,
					u32 cqcc)
{
	struct mlx5e_cq_decomp *cqd = &rq->cqd;
	struct mlx5_mini_cqe8 *mini_cqe = &cqd->mini_arr[cqd->mini_arr_idx];
	struct mlx5_cqe64 *title = &cqd->title;

	title->byte_cnt     = mini_cqe->byte_cnt;
	title->check_sum    = mini_cqe->checksum;
	title->op_own      &= 0xf0;
	title->op_own      |= 0x01 & (cqcc >> wq->fbc.log_sz);

	/* state bit set implies linked-list striding RQ wq type and
	 * HW stride index capability supported
	 */
	if (test_bit(MLX5E_RQ_STATE_MINI_CQE_HW_STRIDX, &rq->state)) {
		title->wqe_counter = mini_cqe->stridx;
		return;
	}

	/* HW stride index capability not supported */
	title->wqe_counter = cpu_to_be16(cqd->wqe_counter);
	if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ)
		cqd->wqe_counter += mpwrq_get_cqe_consumed_strides(title);
	else
		cqd->wqe_counter =
			mlx5_wq_cyc_ctr2ix(&rq->wqe.wq, cqd->wqe_counter + 1);
}

static inline void mlx5e_decompress_cqe_no_hash(struct mlx5e_rq *rq,
						struct mlx5_cqwq *wq,
						u32 cqcc)
{
	struct mlx5e_cq_decomp *cqd = &rq->cqd;

	mlx5e_decompress_cqe(rq, wq, cqcc);
	cqd->title.rss_hash_type   = 0;
	cqd->title.rss_hash_result = 0;
}

static inline u32 mlx5e_decompress_cqes_cont(struct mlx5e_rq *rq,
					     struct mlx5_cqwq *wq,
					     int update_owner_only,
					     int budget_rem)
{
	struct mlx5e_cq_decomp *cqd = &rq->cqd;
	u32 cqcc = wq->cc + update_owner_only;
	u32 cqe_count;
	u32 i;

	cqe_count = min_t(u32, cqd->left, budget_rem);

	for (i = update_owner_only; i < cqe_count;
	     i++, cqd->mini_arr_idx++, cqcc++) {
		if (cqd->mini_arr_idx == MLX5_MINI_CQE_ARRAY_SIZE)
			mlx5e_read_mini_arr_slot(wq, cqd, cqcc);

		mlx5e_decompress_cqe_no_hash(rq, wq, cqcc);
		INDIRECT_CALL_2(rq->handle_rx_cqe, mlx5e_handle_rx_cqe_mpwrq,
				mlx5e_handle_rx_cqe, rq, &cqd->title);
	}
	mlx5e_cqes_update_owner(wq, cqcc - wq->cc);
	wq->cc = cqcc;
	cqd->left -= cqe_count;
	rq->stats->cqe_compress_pkts += cqe_count;

	return cqe_count;
}

static inline u32 mlx5e_decompress_cqes_start(struct mlx5e_rq *rq,
					      struct mlx5_cqwq *wq,
					      int budget_rem)
{
	struct mlx5e_cq_decomp *cqd = &rq->cqd;
	u32 cc = wq->cc;

	mlx5e_read_title_slot(rq, wq, cc);
	mlx5e_read_mini_arr_slot(wq, cqd, cc + 1);
	mlx5e_decompress_cqe(rq, wq, cc);
	INDIRECT_CALL_2(rq->handle_rx_cqe, mlx5e_handle_rx_cqe_mpwrq,
			mlx5e_handle_rx_cqe, rq, &cqd->title);
	cqd->mini_arr_idx++;

	return mlx5e_decompress_cqes_cont(rq, wq, 1, budget_rem) - 1;
}

static inline bool mlx5e_rx_cache_put(struct mlx5e_rq *rq,
				      struct mlx5e_dma_info *dma_info)
{
	struct mlx5e_page_cache *cache = &rq->page_cache;
	u32 tail_next = (cache->tail + 1) & (MLX5E_CACHE_SIZE - 1);
	struct mlx5e_rq_stats *stats = rq->stats;

	if (tail_next == cache->head) {
		stats->cache_full++;
		return false;
	}

	if (!dev_page_is_reusable(dma_info->page)) {
		stats->cache_waive++;
		return false;
	}

	cache->page_cache[cache->tail] = *dma_info;
	cache->tail = tail_next;
	return true;
}

static inline bool mlx5e_rx_cache_get(struct mlx5e_rq *rq,
				      struct mlx5e_dma_info *dma_info)
{
	struct mlx5e_page_cache *cache = &rq->page_cache;
	struct mlx5e_rq_stats *stats = rq->stats;

	if (unlikely(cache->head == cache->tail)) {
		stats->cache_empty++;
		return false;
	}

	if (page_ref_count(cache->page_cache[cache->head].page) != 1) {
		stats->cache_busy++;
		return false;
	}

	*dma_info = cache->page_cache[cache->head];
	cache->head = (cache->head + 1) & (MLX5E_CACHE_SIZE - 1);
	stats->cache_reuse++;

	dma_sync_single_for_device(rq->pdev, dma_info->addr,
				   PAGE_SIZE,
				   DMA_FROM_DEVICE);
	return true;
}

static inline int mlx5e_page_alloc_pool(struct mlx5e_rq *rq,
					struct mlx5e_dma_info *dma_info)
{
	if (mlx5e_rx_cache_get(rq, dma_info))
		return 0;

	dma_info->page = page_pool_dev_alloc_pages(rq->page_pool);
	if (unlikely(!dma_info->page))
		return -ENOMEM;

	dma_info->addr = dma_map_page(rq->pdev, dma_info->page, 0,
				      PAGE_SIZE, rq->buff.map_dir);
	if (unlikely(dma_mapping_error(rq->pdev, dma_info->addr))) {
		page_pool_recycle_direct(rq->page_pool, dma_info->page);
		dma_info->page = NULL;
		return -ENOMEM;
	}

	return 0;
}

static inline int mlx5e_page_alloc(struct mlx5e_rq *rq,
				   struct mlx5e_dma_info *dma_info)
{
	if (rq->xsk_pool)
		return mlx5e_xsk_page_alloc_pool(rq, dma_info);
	else
		return mlx5e_page_alloc_pool(rq, dma_info);
}

void mlx5e_page_dma_unmap(struct mlx5e_rq *rq, struct mlx5e_dma_info *dma_info)
{
	dma_unmap_page(rq->pdev, dma_info->addr, PAGE_SIZE, rq->buff.map_dir);
}

void mlx5e_page_release_dynamic(struct mlx5e_rq *rq,
				struct mlx5e_dma_info *dma_info,
				bool recycle)
{
	if (likely(recycle)) {
		if (mlx5e_rx_cache_put(rq, dma_info))
			return;

		mlx5e_page_dma_unmap(rq, dma_info);
		page_pool_recycle_direct(rq->page_pool, dma_info->page);
	} else {
		mlx5e_page_dma_unmap(rq, dma_info);
		page_pool_release_page(rq->page_pool, dma_info->page);
		put_page(dma_info->page);
	}
}

static inline void mlx5e_page_release(struct mlx5e_rq *rq,
				      struct mlx5e_dma_info *dma_info,
				      bool recycle)
{
	if (rq->xsk_pool)
		/* The `recycle` parameter is ignored, and the page is always
		 * put into the Reuse Ring, because there is no way to return
		 * the page to the userspace when the interface goes down.
		 */
		xsk_buff_free(dma_info->xsk);
	else
		mlx5e_page_release_dynamic(rq, dma_info, recycle);
}

static inline int mlx5e_get_rx_frag(struct mlx5e_rq *rq,
				    struct mlx5e_wqe_frag_info *frag)
{
	int err = 0;

	if (!frag->offset)
		/* On first frag (offset == 0), replenish page (dma_info actually).
		 * Other frags that point to the same dma_info (with a different
		 * offset) should just use the new one without replenishing again
		 * by themselves.
		 */
		err = mlx5e_page_alloc(rq, frag->di);

	return err;
}

static inline void mlx5e_put_rx_frag(struct mlx5e_rq *rq,
				     struct mlx5e_wqe_frag_info *frag,
				     bool recycle)
{
	if (frag->last_in_page)
		mlx5e_page_release(rq, frag->di, recycle);
}

static inline struct mlx5e_wqe_frag_info *get_frag(struct mlx5e_rq *rq, u16 ix)
{
	return &rq->wqe.frags[ix << rq->wqe.info.log_num_frags];
}

static int mlx5e_alloc_rx_wqe(struct mlx5e_rq *rq, struct mlx5e_rx_wqe_cyc *wqe,
			      u16 ix)
{
	struct mlx5e_wqe_frag_info *frag = get_frag(rq, ix);
	int err;
	int i;

	for (i = 0; i < rq->wqe.info.num_frags; i++, frag++) {
		err = mlx5e_get_rx_frag(rq, frag);
		if (unlikely(err))
			goto free_frags;

		wqe->data[i].addr = cpu_to_be64(frag->di->addr +
						frag->offset + rq->buff.headroom);
	}

	return 0;

free_frags:
	while (--i >= 0)
		mlx5e_put_rx_frag(rq, --frag, true);

	return err;
}

static inline void mlx5e_free_rx_wqe(struct mlx5e_rq *rq,
				     struct mlx5e_wqe_frag_info *wi,
				     bool recycle)
{
	int i;

	for (i = 0; i < rq->wqe.info.num_frags; i++, wi++)
		mlx5e_put_rx_frag(rq, wi, recycle);
}

static void mlx5e_dealloc_rx_wqe(struct mlx5e_rq *rq, u16 ix)
{
	struct mlx5e_wqe_frag_info *wi = get_frag(rq, ix);

	mlx5e_free_rx_wqe(rq, wi, false);
}

static int mlx5e_alloc_rx_wqes(struct mlx5e_rq *rq, u16 ix, u8 wqe_bulk)
{
	struct mlx5_wq_cyc *wq = &rq->wqe.wq;
	int err;
	int i;

	if (rq->xsk_pool) {
		int pages_desired = wqe_bulk << rq->wqe.info.log_num_frags;

		/* Check in advance that we have enough frames, instead of
		 * allocating one-by-one, failing and moving frames to the
		 * Reuse Ring.
		 */
		if (unlikely(!xsk_buff_can_alloc(rq->xsk_pool, pages_desired)))
			return -ENOMEM;
	}

	for (i = 0; i < wqe_bulk; i++) {
		struct mlx5e_rx_wqe_cyc *wqe = mlx5_wq_cyc_get_wqe(wq, ix + i);

		err = mlx5e_alloc_rx_wqe(rq, wqe, ix + i);
		if (unlikely(err))
			goto free_wqes;
	}

	return 0;

free_wqes:
	while (--i >= 0)
		mlx5e_dealloc_rx_wqe(rq, ix + i);

	return err;
}

static inline void
mlx5e_add_skb_frag(struct mlx5e_rq *rq, struct sk_buff *skb,
		   struct mlx5e_dma_info *di, u32 frag_offset, u32 len,
		   unsigned int truesize)
{
	dma_sync_single_for_cpu(rq->pdev,
				di->addr + frag_offset,
				len, DMA_FROM_DEVICE);
	page_ref_inc(di->page);
	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
			di->page, frag_offset, len, truesize);
}

static inline void
mlx5e_copy_skb_header(struct device *pdev, struct sk_buff *skb,
		      struct mlx5e_dma_info *dma_info,
		      int offset_from, u32 headlen)
{
	const void *from = page_address(dma_info->page) + offset_from;
	/* Aligning len to sizeof(long) optimizes memcpy performance */
	unsigned int len = ALIGN(headlen, sizeof(long));

	dma_sync_single_for_cpu(pdev, dma_info->addr + offset_from, len,
				DMA_FROM_DEVICE);
	skb_copy_to_linear_data(skb, from, len);
}

static void
mlx5e_free_rx_mpwqe(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi, bool recycle)
{
	bool no_xdp_xmit;
	struct mlx5e_dma_info *dma_info = wi->umr.dma_info;
	int i;

	/* A common case for AF_XDP. */
	if (bitmap_full(wi->xdp_xmit_bitmap, MLX5_MPWRQ_PAGES_PER_WQE))
		return;

	no_xdp_xmit = bitmap_empty(wi->xdp_xmit_bitmap,
				   MLX5_MPWRQ_PAGES_PER_WQE);

	for (i = 0; i < MLX5_MPWRQ_PAGES_PER_WQE; i++)
		if (no_xdp_xmit || !test_bit(i, wi->xdp_xmit_bitmap))
			mlx5e_page_release(rq, &dma_info[i], recycle);
}

static void mlx5e_post_rx_mpwqe(struct mlx5e_rq *rq, u8 n)
{
	struct mlx5_wq_ll *wq = &rq->mpwqe.wq;

	do {
		u16 next_wqe_index = mlx5_wq_ll_get_wqe_next_ix(wq, wq->head);

		mlx5_wq_ll_push(wq, next_wqe_index);
	} while (--n);

	/* ensure wqes are visible to device before updating doorbell record */
	dma_wmb();

	mlx5_wq_ll_update_db_record(wq);
}

static int mlx5e_alloc_rx_mpwqe(struct mlx5e_rq *rq, u16 ix)
{
	struct mlx5e_mpw_info *wi = &rq->mpwqe.info[ix];
	struct mlx5e_dma_info *dma_info = &wi->umr.dma_info[0];
	struct mlx5e_icosq *sq = rq->icosq;
	struct mlx5_wq_cyc *wq = &sq->wq;
	struct mlx5e_umr_wqe *umr_wqe;
	u16 xlt_offset = ix << (MLX5E_LOG_ALIGNED_MPWQE_PPW - 1);
	u16 pi;
	int err;
	int i;

	/* Check in advance that we have enough frames, instead of allocating
	 * one-by-one, failing and moving frames to the Reuse Ring.
	 */
	if (rq->xsk_pool &&
	    unlikely(!xsk_buff_can_alloc(rq->xsk_pool, MLX5_MPWRQ_PAGES_PER_WQE))) {
		err = -ENOMEM;
		goto err;
	}

	pi = mlx5e_icosq_get_next_pi(sq, MLX5E_UMR_WQEBBS);
	umr_wqe = mlx5_wq_cyc_get_wqe(wq, pi);
	memcpy(umr_wqe, &rq->mpwqe.umr_wqe, offsetof(struct mlx5e_umr_wqe, inline_mtts));

	for (i = 0; i < MLX5_MPWRQ_PAGES_PER_WQE; i++, dma_info++) {
		err = mlx5e_page_alloc(rq, dma_info);
		if (unlikely(err))
			goto err_unmap;
		umr_wqe->inline_mtts[i].ptag = cpu_to_be64(dma_info->addr | MLX5_EN_WR);
	}

	bitmap_zero(wi->xdp_xmit_bitmap, MLX5_MPWRQ_PAGES_PER_WQE);
	wi->consumed_strides = 0;

	umr_wqe->ctrl.opmod_idx_opcode =
		cpu_to_be32((sq->pc << MLX5_WQE_CTRL_WQE_INDEX_SHIFT) |
			    MLX5_OPCODE_UMR);
	umr_wqe->uctrl.xlt_offset = cpu_to_be16(xlt_offset);

	sq->db.wqe_info[pi] = (struct mlx5e_icosq_wqe_info) {
		.wqe_type   = MLX5E_ICOSQ_WQE_UMR_RX,
		.num_wqebbs = MLX5E_UMR_WQEBBS,
		.umr.rq     = rq,
	};

	sq->pc += MLX5E_UMR_WQEBBS;

	sq->doorbell_cseg = &umr_wqe->ctrl;

	return 0;

err_unmap:
	while (--i >= 0) {
		dma_info--;
		mlx5e_page_release(rq, dma_info, true);
	}

err:
	rq->stats->buff_alloc_err++;

	return err;
}

static void mlx5e_dealloc_rx_mpwqe(struct mlx5e_rq *rq, u16 ix)
{
	struct mlx5e_mpw_info *wi = &rq->mpwqe.info[ix];
	/* Don't recycle, this function is called on rq/netdev close */
	mlx5e_free_rx_mpwqe(rq, wi, false);
}

INDIRECT_CALLABLE_SCOPE bool mlx5e_post_rx_wqes(struct mlx5e_rq *rq)
{
	struct mlx5_wq_cyc *wq = &rq->wqe.wq;
	u8 wqe_bulk;
	int err;

	if (unlikely(!test_bit(MLX5E_RQ_STATE_ENABLED, &rq->state)))
		return false;

	wqe_bulk = rq->wqe.info.wqe_bulk;

	if (mlx5_wq_cyc_missing(wq) < wqe_bulk)
		return false;

	do {
		u16 head = mlx5_wq_cyc_get_head(wq);

		err = mlx5e_alloc_rx_wqes(rq, head, wqe_bulk);
		if (unlikely(err)) {
			rq->stats->buff_alloc_err++;
			break;
		}

		mlx5_wq_cyc_push_n(wq, wqe_bulk);
	} while (mlx5_wq_cyc_missing(wq) >= wqe_bulk);

	/* ensure wqes are visible to device before updating doorbell record */
	dma_wmb();

	mlx5_wq_cyc_update_db_record(wq);

	return !!err;
}

void mlx5e_free_icosq_descs(struct mlx5e_icosq *sq)
{
	u16 sqcc;

	sqcc = sq->cc;

	while (sqcc != sq->pc) {
		struct mlx5e_icosq_wqe_info *wi;
		u16 ci;

		ci = mlx5_wq_cyc_ctr2ix(&sq->wq, sqcc);
		wi = &sq->db.wqe_info[ci];
		sqcc += wi->num_wqebbs;
#ifdef CONFIG_MLX5_EN_TLS
		switch (wi->wqe_type) {
		case MLX5E_ICOSQ_WQE_SET_PSV_TLS:
			mlx5e_ktls_handle_ctx_completion(wi);
			break;
		case MLX5E_ICOSQ_WQE_GET_PSV_TLS:
			mlx5e_ktls_handle_get_psv_completion(wi, sq);
			break;
		}
#endif
	}
	sq->cc = sqcc;
}

int mlx5e_poll_ico_cq(struct mlx5e_cq *cq)
{
	struct mlx5e_icosq *sq = container_of(cq, struct mlx5e_icosq, cq);
	struct mlx5_cqe64 *cqe;
	u16 sqcc;
	int i;

	if (unlikely(!test_bit(MLX5E_SQ_STATE_ENABLED, &sq->state)))
		return 0;

	cqe = mlx5_cqwq_get_cqe(&cq->wq);
	if (likely(!cqe))
		return 0;

	/* sq->cc must be updated only after mlx5_cqwq_update_db_record(),
	 * otherwise a cq overrun may occur
	 */
	sqcc = sq->cc;

	i = 0;
	do {
		u16 wqe_counter;
		bool last_wqe;

		mlx5_cqwq_pop(&cq->wq);

		wqe_counter = be16_to_cpu(cqe->wqe_counter);

		do {
			struct mlx5e_icosq_wqe_info *wi;
			u16 ci;

			last_wqe = (sqcc == wqe_counter);

			ci = mlx5_wq_cyc_ctr2ix(&sq->wq, sqcc);
			wi = &sq->db.wqe_info[ci];
			sqcc += wi->num_wqebbs;

			if (last_wqe && unlikely(get_cqe_opcode(cqe) != MLX5_CQE_REQ)) {
				netdev_WARN_ONCE(cq->netdev,
						 "Bad OP in ICOSQ CQE: 0x%x\n",
						 get_cqe_opcode(cqe));
				mlx5e_dump_error_cqe(&sq->cq, sq->sqn,
						     (struct mlx5_err_cqe *)cqe);
				if (!test_and_set_bit(MLX5E_SQ_STATE_RECOVERING, &sq->state))
					queue_work(cq->priv->wq, &sq->recover_work);
				break;
			}

			switch (wi->wqe_type) {
			case MLX5E_ICOSQ_WQE_UMR_RX:
				wi->umr.rq->mpwqe.umr_completed++;
				break;
			case MLX5E_ICOSQ_WQE_NOP:
				break;
#ifdef CONFIG_MLX5_EN_TLS
			case MLX5E_ICOSQ_WQE_UMR_TLS:
				break;
			case MLX5E_ICOSQ_WQE_SET_PSV_TLS:
				mlx5e_ktls_handle_ctx_completion(wi);
				break;
			case MLX5E_ICOSQ_WQE_GET_PSV_TLS:
				mlx5e_ktls_handle_get_psv_completion(wi, sq);
				break;
#endif
			default:
				netdev_WARN_ONCE(cq->netdev,
						 "Bad WQE type in ICOSQ WQE info: 0x%x\n",
						 wi->wqe_type);
			}
		} while (!last_wqe);
	} while ((++i < MLX5E_TX_CQ_POLL_BUDGET) && (cqe = mlx5_cqwq_get_cqe(&cq->wq)));

	sq->cc = sqcc;

	mlx5_cqwq_update_db_record(&cq->wq);

	return i;
}

INDIRECT_CALLABLE_SCOPE bool mlx5e_post_rx_mpwqes(struct mlx5e_rq *rq)
{
	struct mlx5_wq_ll *wq = &rq->mpwqe.wq;
	u8  umr_completed = rq->mpwqe.umr_completed;
	struct mlx5e_icosq *sq = rq->icosq;
	int alloc_err = 0;
	u8  missing, i;
	u16 head;

	if (unlikely(!test_bit(MLX5E_RQ_STATE_ENABLED, &rq->state)))
		return false;

	if (umr_completed) {
		mlx5e_post_rx_mpwqe(rq, umr_completed);
		rq->mpwqe.umr_in_progress -= umr_completed;
		rq->mpwqe.umr_completed = 0;
	}

	missing = mlx5_wq_ll_missing(wq) - rq->mpwqe.umr_in_progress;

	if (unlikely(rq->mpwqe.umr_in_progress > rq->mpwqe.umr_last_bulk))
		rq->stats->congst_umr++;

#define UMR_WQE_BULK (2)
	if (likely(missing < UMR_WQE_BULK))
		return false;

	head = rq->mpwqe.actual_wq_head;
	i = missing;
	do {
		alloc_err = mlx5e_alloc_rx_mpwqe(rq, head);

		if (unlikely(alloc_err))
			break;
		head = mlx5_wq_ll_get_wqe_next_ix(wq, head);
	} while (--i);

	rq->mpwqe.umr_last_bulk    = missing - i;
	if (sq->doorbell_cseg) {
		mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, sq->doorbell_cseg);
		sq->doorbell_cseg = NULL;
	}

	rq->mpwqe.umr_in_progress += rq->mpwqe.umr_last_bulk;
	rq->mpwqe.actual_wq_head   = head;

	/* If XSK Fill Ring doesn't have enough frames, report the error, so
	 * that one of the actions can be performed:
	 * 1. If need_wakeup is used, signal that the application has to kick
	 * the driver when it refills the Fill Ring.
	 * 2. Otherwise, busy poll by rescheduling the NAPI poll.
	 */
	if (unlikely(alloc_err == -ENOMEM && rq->xsk_pool))
		return true;

	return false;
}

static void mlx5e_lro_update_tcp_hdr(struct mlx5_cqe64 *cqe, struct tcphdr *tcp)
{
	u8 l4_hdr_type = get_cqe_l4_hdr_type(cqe);
	u8 tcp_ack     = (l4_hdr_type == CQE_L4_HDR_TYPE_TCP_ACK_NO_DATA) ||
			 (l4_hdr_type == CQE_L4_HDR_TYPE_TCP_ACK_AND_DATA);

	tcp->check                      = 0;
	tcp->psh                        = get_cqe_lro_tcppsh(cqe);

	if (tcp_ack) {
		tcp->ack                = 1;
		tcp->ack_seq            = cqe->lro_ack_seq_num;
		tcp->window             = cqe->lro_tcp_win;
	}
}

static void mlx5e_lro_update_hdr(struct sk_buff *skb, struct mlx5_cqe64 *cqe,
				 u32 cqe_bcnt)
{
	struct ethhdr	*eth = (struct ethhdr *)(skb->data);
	struct tcphdr	*tcp;
	int network_depth = 0;
	__wsum check;
	__be16 proto;
	u16 tot_len;
	void *ip_p;

	proto = __vlan_get_protocol(skb, eth->h_proto, &network_depth);

	tot_len = cqe_bcnt - network_depth;
	ip_p = skb->data + network_depth;

	if (proto == htons(ETH_P_IP)) {
		struct iphdr *ipv4 = ip_p;

		tcp = ip_p + sizeof(struct iphdr);
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;

		ipv4->ttl               = cqe->lro_min_ttl;
		ipv4->tot_len           = cpu_to_be16(tot_len);
		ipv4->check             = 0;
		ipv4->check             = ip_fast_csum((unsigned char *)ipv4,
						       ipv4->ihl);

		mlx5e_lro_update_tcp_hdr(cqe, tcp);
		check = csum_partial(tcp, tcp->doff * 4,
				     csum_unfold((__force __sum16)cqe->check_sum));
		/* Almost done, don't forget the pseudo header */
		tcp->check = csum_tcpudp_magic(ipv4->saddr, ipv4->daddr,
					       tot_len - sizeof(struct iphdr),
					       IPPROTO_TCP, check);
	} else {
		u16 payload_len = tot_len - sizeof(struct ipv6hdr);
		struct ipv6hdr *ipv6 = ip_p;

		tcp = ip_p + sizeof(struct ipv6hdr);
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;

		ipv6->hop_limit         = cqe->lro_min_ttl;
		ipv6->payload_len       = cpu_to_be16(payload_len);

		mlx5e_lro_update_tcp_hdr(cqe, tcp);
		check = csum_partial(tcp, tcp->doff * 4,
				     csum_unfold((__force __sum16)cqe->check_sum));
		/* Almost done, don't forget the pseudo header */
		tcp->check = csum_ipv6_magic(&ipv6->saddr, &ipv6->daddr, payload_len,
					     IPPROTO_TCP, check);
	}
}

static inline void mlx5e_skb_set_hash(struct mlx5_cqe64 *cqe,
				      struct sk_buff *skb)
{
	u8 cht = cqe->rss_hash_type;
	int ht = (cht & CQE_RSS_HTYPE_L4) ? PKT_HASH_TYPE_L4 :
		 (cht & CQE_RSS_HTYPE_IP) ? PKT_HASH_TYPE_L3 :
					    PKT_HASH_TYPE_NONE;
	skb_set_hash(skb, be32_to_cpu(cqe->rss_hash_result), ht);
}

static inline bool is_last_ethertype_ip(struct sk_buff *skb, int *network_depth,
					__be16 *proto)
{
	*proto = ((struct ethhdr *)skb->data)->h_proto;
	*proto = __vlan_get_protocol(skb, *proto, network_depth);

	if (*proto == htons(ETH_P_IP))
		return pskb_may_pull(skb, *network_depth + sizeof(struct iphdr));

	if (*proto == htons(ETH_P_IPV6))
		return pskb_may_pull(skb, *network_depth + sizeof(struct ipv6hdr));

	return false;
}

static inline void mlx5e_enable_ecn(struct mlx5e_rq *rq, struct sk_buff *skb)
{
	int network_depth = 0;
	__be16 proto;
	void *ip;
	int rc;

	if (unlikely(!is_last_ethertype_ip(skb, &network_depth, &proto)))
		return;

	ip = skb->data + network_depth;
	rc = ((proto == htons(ETH_P_IP)) ? IP_ECN_set_ce((struct iphdr *)ip) :
					 IP6_ECN_set_ce(skb, (struct ipv6hdr *)ip));

	rq->stats->ecn_mark += !!rc;
}

static u8 get_ip_proto(struct sk_buff *skb, int network_depth, __be16 proto)
{
	void *ip_p = skb->data + network_depth;

	return (proto == htons(ETH_P_IP)) ? ((struct iphdr *)ip_p)->protocol :
					    ((struct ipv6hdr *)ip_p)->nexthdr;
}

#define short_frame(size) ((size) <= ETH_ZLEN + ETH_FCS_LEN)

#define MAX_PADDING 8

static void
tail_padding_csum_slow(struct sk_buff *skb, int offset, int len,
		       struct mlx5e_rq_stats *stats)
{
	stats->csum_complete_tail_slow++;
	skb->csum = csum_block_add(skb->csum,
				   skb_checksum(skb, offset, len, 0),
				   offset);
}

static void
tail_padding_csum(struct sk_buff *skb, int offset,
		  struct mlx5e_rq_stats *stats)
{
	u8 tail_padding[MAX_PADDING];
	int len = skb->len - offset;
	void *tail;

	if (unlikely(len > MAX_PADDING)) {
		tail_padding_csum_slow(skb, offset, len, stats);
		return;
	}

	tail = skb_header_pointer(skb, offset, len, tail_padding);
	if (unlikely(!tail)) {
		tail_padding_csum_slow(skb, offset, len, stats);
		return;
	}

	stats->csum_complete_tail++;
	skb->csum = csum_block_add(skb->csum, csum_partial(tail, len, 0), offset);
}

static void
mlx5e_skb_csum_fixup(struct sk_buff *skb, int network_depth, __be16 proto,
		     struct mlx5e_rq_stats *stats)
{
	struct ipv6hdr *ip6;
	struct iphdr   *ip4;
	int pkt_len;

	/* Fixup vlan headers, if any */
	if (network_depth > ETH_HLEN)
		/* CQE csum is calculated from the IP header and does
		 * not cover VLAN headers (if present). This will add
		 * the checksum manually.
		 */
		skb->csum = csum_partial(skb->data + ETH_HLEN,
					 network_depth - ETH_HLEN,
					 skb->csum);

	/* Fixup tail padding, if any */
	switch (proto) {
	case htons(ETH_P_IP):
		ip4 = (struct iphdr *)(skb->data + network_depth);
		pkt_len = network_depth + ntohs(ip4->tot_len);
		break;
	case htons(ETH_P_IPV6):
		ip6 = (struct ipv6hdr *)(skb->data + network_depth);
		pkt_len = network_depth + sizeof(*ip6) + ntohs(ip6->payload_len);
		break;
	default:
		return;
	}

	if (likely(pkt_len >= skb->len))
		return;

	tail_padding_csum(skb, pkt_len, stats);
}

static inline void mlx5e_handle_csum(struct net_device *netdev,
				     struct mlx5_cqe64 *cqe,
				     struct mlx5e_rq *rq,
				     struct sk_buff *skb,
				     bool   lro)
{
	struct mlx5e_rq_stats *stats = rq->stats;
	int network_depth = 0;
	__be16 proto;

	if (unlikely(!(netdev->features & NETIF_F_RXCSUM)))
		goto csum_none;

	if (lro) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		stats->csum_unnecessary++;
		return;
	}

	/* True when explicitly set via priv flag, or XDP prog is loaded */
	if (test_bit(MLX5E_RQ_STATE_NO_CSUM_COMPLETE, &rq->state))
		goto csum_unnecessary;

	/* CQE csum doesn't cover padding octets in short ethernet
	 * frames. And the pad field is appended prior to calculating
	 * and appending the FCS field.
	 *
	 * Detecting these padded frames requires to verify and parse
	 * IP headers, so we simply force all those small frames to be
	 * CHECKSUM_UNNECESSARY even if they are not padded.
	 */
	if (short_frame(skb->len))
		goto csum_unnecessary;

	if (likely(is_last_ethertype_ip(skb, &network_depth, &proto))) {
		u8 ipproto = get_ip_proto(skb, network_depth, proto);

		if (unlikely(ipproto == IPPROTO_SCTP))
			goto csum_unnecessary;

		if (unlikely(mlx5_ipsec_is_rx_flow(cqe)))
			goto csum_none;

		stats->csum_complete++;
		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum = csum_unfold((__force __sum16)cqe->check_sum);

		if (test_bit(MLX5E_RQ_STATE_CSUM_FULL, &rq->state))
			return; /* CQE csum covers all received bytes */

		/* csum might need some fixups ...*/
		mlx5e_skb_csum_fixup(skb, network_depth, proto, stats);
		return;
	}

csum_unnecessary:
	if (likely((cqe->hds_ip_ext & CQE_L3_OK) &&
		   (cqe->hds_ip_ext & CQE_L4_OK))) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		if (cqe_is_tunneled(cqe)) {
			skb->csum_level = 1;
			skb->encapsulation = 1;
			stats->csum_unnecessary_inner++;
			return;
		}
		stats->csum_unnecessary++;
		return;
	}
csum_none:
	skb->ip_summed = CHECKSUM_NONE;
	stats->csum_none++;
}

#define MLX5E_CE_BIT_MASK 0x80

static inline void mlx5e_build_rx_skb(struct mlx5_cqe64 *cqe,
				      u32 cqe_bcnt,
				      struct mlx5e_rq *rq,
				      struct sk_buff *skb)
{
	u8 lro_num_seg = be32_to_cpu(cqe->srqn) >> 24;
	struct mlx5e_rq_stats *stats = rq->stats;
	struct net_device *netdev = rq->netdev;

	skb->mac_len = ETH_HLEN;

	mlx5e_tls_handle_rx_skb(rq, skb, cqe, &cqe_bcnt);

	if (unlikely(mlx5_ipsec_is_rx_flow(cqe)))
		mlx5e_ipsec_offload_handle_rx_skb(netdev, skb, cqe);

	if (lro_num_seg > 1) {
		mlx5e_lro_update_hdr(skb, cqe, cqe_bcnt);
		skb_shinfo(skb)->gso_size = DIV_ROUND_UP(cqe_bcnt, lro_num_seg);
		/* Subtract one since we already counted this as one
		 * "regular" packet in mlx5e_complete_rx_cqe()
		 */
		stats->packets += lro_num_seg - 1;
		stats->lro_packets++;
		stats->lro_bytes += cqe_bcnt;
	}

	if (unlikely(mlx5e_rx_hw_stamp(rq->tstamp)))
		skb_hwtstamps(skb)->hwtstamp = mlx5e_cqe_ts_to_ns(rq->ptp_cyc2time,
								  rq->clock, get_cqe_ts(cqe));
	skb_record_rx_queue(skb, rq->ix);

	if (likely(netdev->features & NETIF_F_RXHASH))
		mlx5e_skb_set_hash(cqe, skb);

	if (cqe_has_vlan(cqe)) {
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       be16_to_cpu(cqe->vlan_info));
		stats->removed_vlan_packets++;
	}

	skb->mark = be32_to_cpu(cqe->sop_drop_qpn) & MLX5E_TC_FLOW_ID_MASK;

	mlx5e_handle_csum(netdev, cqe, rq, skb, !!lro_num_seg);
	/* checking CE bit in cqe - MSB in ml_path field */
	if (unlikely(cqe->ml_path & MLX5E_CE_BIT_MASK))
		mlx5e_enable_ecn(rq, skb);

	skb->protocol = eth_type_trans(skb, netdev);

	if (unlikely(mlx5e_skb_is_multicast(skb)))
		stats->mcast_packets++;
}

static inline void mlx5e_complete_rx_cqe(struct mlx5e_rq *rq,
					 struct mlx5_cqe64 *cqe,
					 u32 cqe_bcnt,
					 struct sk_buff *skb)
{
	struct mlx5e_rq_stats *stats = rq->stats;

	stats->packets++;
	stats->bytes += cqe_bcnt;
	mlx5e_build_rx_skb(cqe, cqe_bcnt, rq, skb);
}

static inline
struct sk_buff *mlx5e_build_linear_skb(struct mlx5e_rq *rq, void *va,
				       u32 frag_size, u16 headroom,
				       u32 cqe_bcnt)
{
	struct sk_buff *skb = build_skb(va, frag_size);

	if (unlikely(!skb)) {
		rq->stats->buff_alloc_err++;
		return NULL;
	}

	skb_reserve(skb, headroom);
	skb_put(skb, cqe_bcnt);

	return skb;
}

static void mlx5e_fill_xdp_buff(struct mlx5e_rq *rq, void *va, u16 headroom,
				u32 len, struct xdp_buff *xdp)
{
	xdp_init_buff(xdp, rq->buff.frame0_sz, &rq->xdp_rxq);
	xdp_prepare_buff(xdp, va, headroom, len, false);
}

static struct sk_buff *
mlx5e_skb_from_cqe_linear(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe,
			  struct mlx5e_wqe_frag_info *wi, u32 cqe_bcnt)
{
	struct mlx5e_dma_info *di = wi->di;
	u16 rx_headroom = rq->buff.headroom;
	struct xdp_buff xdp;
	struct sk_buff *skb;
	void *va, *data;
	u32 frag_size;

	va             = page_address(di->page) + wi->offset;
	data           = va + rx_headroom;
	frag_size      = MLX5_SKB_FRAG_SZ(rx_headroom + cqe_bcnt);

	dma_sync_single_range_for_cpu(rq->pdev, di->addr, wi->offset,
				      frag_size, DMA_FROM_DEVICE);
	net_prefetchw(va); /* xdp_frame data area */
	net_prefetch(data);

	mlx5e_fill_xdp_buff(rq, va, rx_headroom, cqe_bcnt, &xdp);
	if (mlx5e_xdp_handle(rq, di, &cqe_bcnt, &xdp))
		return NULL; /* page/packet was consumed by XDP */

	rx_headroom = xdp.data - xdp.data_hard_start;
	frag_size = MLX5_SKB_FRAG_SZ(rx_headroom + cqe_bcnt);
	skb = mlx5e_build_linear_skb(rq, va, frag_size, rx_headroom, cqe_bcnt);
	if (unlikely(!skb))
		return NULL;

	/* queue up for recycling/reuse */
	page_ref_inc(di->page);

	return skb;
}

static struct sk_buff *
mlx5e_skb_from_cqe_nonlinear(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe,
			     struct mlx5e_wqe_frag_info *wi, u32 cqe_bcnt)
{
	struct mlx5e_rq_frag_info *frag_info = &rq->wqe.info.arr[0];
	struct mlx5e_wqe_frag_info *head_wi = wi;
	u16 headlen      = min_t(u32, MLX5E_RX_MAX_HEAD, cqe_bcnt);
	u16 frag_headlen = headlen;
	u16 byte_cnt     = cqe_bcnt - headlen;
	struct sk_buff *skb;

	/* XDP is not supported in this configuration, as incoming packets
	 * might spread among multiple pages.
	 */
	skb = napi_alloc_skb(rq->cq.napi,
			     ALIGN(MLX5E_RX_MAX_HEAD, sizeof(long)));
	if (unlikely(!skb)) {
		rq->stats->buff_alloc_err++;
		return NULL;
	}

	net_prefetchw(skb->data);

	while (byte_cnt) {
		u16 frag_consumed_bytes =
			min_t(u16, frag_info->frag_size - frag_headlen, byte_cnt);

		mlx5e_add_skb_frag(rq, skb, wi->di, wi->offset + frag_headlen,
				   frag_consumed_bytes, frag_info->frag_stride);
		byte_cnt -= frag_consumed_bytes;
		frag_headlen = 0;
		frag_info++;
		wi++;
	}

	/* copy header */
	mlx5e_copy_skb_header(rq->pdev, skb, head_wi->di, head_wi->offset, headlen);
	/* skb linear part was allocated with headlen and aligned to long */
	skb->tail += headlen;
	skb->len  += headlen;

	return skb;
}

static void trigger_report(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe)
{
	struct mlx5_err_cqe *err_cqe = (struct mlx5_err_cqe *)cqe;
	struct mlx5e_priv *priv = rq->priv;

	if (cqe_syndrome_needs_recover(err_cqe->syndrome) &&
	    !test_and_set_bit(MLX5E_RQ_STATE_RECOVERING, &rq->state)) {
		mlx5e_dump_error_cqe(&rq->cq, rq->rqn, err_cqe);
		queue_work(priv->wq, &rq->recover_work);
	}
}

static void mlx5e_handle_rx_cqe(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe)
{
	struct mlx5_wq_cyc *wq = &rq->wqe.wq;
	struct mlx5e_wqe_frag_info *wi;
	struct sk_buff *skb;
	u32 cqe_bcnt;
	u16 ci;

	ci       = mlx5_wq_cyc_ctr2ix(wq, be16_to_cpu(cqe->wqe_counter));
	wi       = get_frag(rq, ci);
	cqe_bcnt = be32_to_cpu(cqe->byte_cnt);

	if (unlikely(MLX5E_RX_ERR_CQE(cqe))) {
		trigger_report(rq, cqe);
		rq->stats->wqe_err++;
		goto free_wqe;
	}

	skb = INDIRECT_CALL_2(rq->wqe.skb_from_cqe,
			      mlx5e_skb_from_cqe_linear,
			      mlx5e_skb_from_cqe_nonlinear,
			      rq, cqe, wi, cqe_bcnt);
	if (!skb) {
		/* probably for XDP */
		if (__test_and_clear_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags)) {
			/* do not return page to cache,
			 * it will be returned on XDP_TX completion.
			 */
			goto wq_cyc_pop;
		}
		goto free_wqe;
	}

	mlx5e_complete_rx_cqe(rq, cqe, cqe_bcnt, skb);

	if (mlx5e_cqe_regb_chain(cqe))
		if (!mlx5e_tc_update_skb(cqe, skb)) {
			dev_kfree_skb_any(skb);
			goto free_wqe;
		}

	napi_gro_receive(rq->cq.napi, skb);

free_wqe:
	mlx5e_free_rx_wqe(rq, wi, true);
wq_cyc_pop:
	mlx5_wq_cyc_pop(wq);
}

#ifdef CONFIG_MLX5_ESWITCH
static void mlx5e_handle_rx_cqe_rep(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe)
{
	struct net_device *netdev = rq->netdev;
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_rep_priv *rpriv  = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct mlx5e_tc_update_priv tc_priv = {};
	struct mlx5_wq_cyc *wq = &rq->wqe.wq;
	struct mlx5e_wqe_frag_info *wi;
	struct sk_buff *skb;
	u32 cqe_bcnt;
	u16 ci;

	ci       = mlx5_wq_cyc_ctr2ix(wq, be16_to_cpu(cqe->wqe_counter));
	wi       = get_frag(rq, ci);
	cqe_bcnt = be32_to_cpu(cqe->byte_cnt);

	if (unlikely(MLX5E_RX_ERR_CQE(cqe))) {
		rq->stats->wqe_err++;
		goto free_wqe;
	}

	skb = INDIRECT_CALL_2(rq->wqe.skb_from_cqe,
			      mlx5e_skb_from_cqe_linear,
			      mlx5e_skb_from_cqe_nonlinear,
			      rq, cqe, wi, cqe_bcnt);
	if (!skb) {
		/* probably for XDP */
		if (__test_and_clear_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags)) {
			/* do not return page to cache,
			 * it will be returned on XDP_TX completion.
			 */
			goto wq_cyc_pop;
		}
		goto free_wqe;
	}

	mlx5e_complete_rx_cqe(rq, cqe, cqe_bcnt, skb);

	if (rep->vlan && skb_vlan_tag_present(skb))
		skb_vlan_pop(skb);

	if (!mlx5e_rep_tc_update_skb(cqe, skb, &tc_priv)) {
		dev_kfree_skb_any(skb);
		goto free_wqe;
	}

	napi_gro_receive(rq->cq.napi, skb);

	mlx5_rep_tc_post_napi_receive(&tc_priv);

free_wqe:
	mlx5e_free_rx_wqe(rq, wi, true);
wq_cyc_pop:
	mlx5_wq_cyc_pop(wq);
}

static void mlx5e_handle_rx_cqe_mpwrq_rep(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe)
{
	u16 cstrides       = mpwrq_get_cqe_consumed_strides(cqe);
	u16 wqe_id         = be16_to_cpu(cqe->wqe_id);
	struct mlx5e_mpw_info *wi = &rq->mpwqe.info[wqe_id];
	u16 stride_ix      = mpwrq_get_cqe_stride_index(cqe);
	u32 wqe_offset     = stride_ix << rq->mpwqe.log_stride_sz;
	u32 head_offset    = wqe_offset & (PAGE_SIZE - 1);
	u32 page_idx       = wqe_offset >> PAGE_SHIFT;
	struct mlx5e_tc_update_priv tc_priv = {};
	struct mlx5e_rx_wqe_ll *wqe;
	struct mlx5_wq_ll *wq;
	struct sk_buff *skb;
	u16 cqe_bcnt;

	wi->consumed_strides += cstrides;

	if (unlikely(MLX5E_RX_ERR_CQE(cqe))) {
		trigger_report(rq, cqe);
		rq->stats->wqe_err++;
		goto mpwrq_cqe_out;
	}

	if (unlikely(mpwrq_is_filler_cqe(cqe))) {
		struct mlx5e_rq_stats *stats = rq->stats;

		stats->mpwqe_filler_cqes++;
		stats->mpwqe_filler_strides += cstrides;
		goto mpwrq_cqe_out;
	}

	cqe_bcnt = mpwrq_get_cqe_byte_cnt(cqe);

	skb = INDIRECT_CALL_2(rq->mpwqe.skb_from_cqe_mpwrq,
			      mlx5e_skb_from_cqe_mpwrq_linear,
			      mlx5e_skb_from_cqe_mpwrq_nonlinear,
			      rq, wi, cqe_bcnt, head_offset, page_idx);
	if (!skb)
		goto mpwrq_cqe_out;

	mlx5e_complete_rx_cqe(rq, cqe, cqe_bcnt, skb);

	if (!mlx5e_rep_tc_update_skb(cqe, skb, &tc_priv)) {
		dev_kfree_skb_any(skb);
		goto mpwrq_cqe_out;
	}

	napi_gro_receive(rq->cq.napi, skb);

	mlx5_rep_tc_post_napi_receive(&tc_priv);

mpwrq_cqe_out:
	if (likely(wi->consumed_strides < rq->mpwqe.num_strides))
		return;

	wq  = &rq->mpwqe.wq;
	wqe = mlx5_wq_ll_get_wqe(wq, wqe_id);
	mlx5e_free_rx_mpwqe(rq, wi, true);
	mlx5_wq_ll_pop(wq, cqe->wqe_id, &wqe->next.next_wqe_index);
}

const struct mlx5e_rx_handlers mlx5e_rx_handlers_rep = {
	.handle_rx_cqe       = mlx5e_handle_rx_cqe_rep,
	.handle_rx_cqe_mpwqe = mlx5e_handle_rx_cqe_mpwrq_rep,
};
#endif

static struct sk_buff *
mlx5e_skb_from_cqe_mpwrq_nonlinear(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi,
				   u16 cqe_bcnt, u32 head_offset, u32 page_idx)
{
	u16 headlen = min_t(u16, MLX5E_RX_MAX_HEAD, cqe_bcnt);
	struct mlx5e_dma_info *di = &wi->umr.dma_info[page_idx];
	u32 frag_offset    = head_offset + headlen;
	u32 byte_cnt       = cqe_bcnt - headlen;
	struct mlx5e_dma_info *head_di = di;
	struct sk_buff *skb;

	skb = napi_alloc_skb(rq->cq.napi,
			     ALIGN(MLX5E_RX_MAX_HEAD, sizeof(long)));
	if (unlikely(!skb)) {
		rq->stats->buff_alloc_err++;
		return NULL;
	}

	net_prefetchw(skb->data);

	if (unlikely(frag_offset >= PAGE_SIZE)) {
		di++;
		frag_offset -= PAGE_SIZE;
	}

	while (byte_cnt) {
		u32 pg_consumed_bytes =
			min_t(u32, PAGE_SIZE - frag_offset, byte_cnt);
		unsigned int truesize =
			ALIGN(pg_consumed_bytes, BIT(rq->mpwqe.log_stride_sz));

		mlx5e_add_skb_frag(rq, skb, di, frag_offset,
				   pg_consumed_bytes, truesize);
		byte_cnt -= pg_consumed_bytes;
		frag_offset = 0;
		di++;
	}
	/* copy header */
	mlx5e_copy_skb_header(rq->pdev, skb, head_di, head_offset, headlen);
	/* skb linear part was allocated with headlen and aligned to long */
	skb->tail += headlen;
	skb->len  += headlen;

	return skb;
}

static struct sk_buff *
mlx5e_skb_from_cqe_mpwrq_linear(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi,
				u16 cqe_bcnt, u32 head_offset, u32 page_idx)
{
	struct mlx5e_dma_info *di = &wi->umr.dma_info[page_idx];
	u16 rx_headroom = rq->buff.headroom;
	u32 cqe_bcnt32 = cqe_bcnt;
	struct xdp_buff xdp;
	struct sk_buff *skb;
	void *va, *data;
	u32 frag_size;

	/* Check packet size. Note LRO doesn't use linear SKB */
	if (unlikely(cqe_bcnt > rq->hw_mtu)) {
		rq->stats->oversize_pkts_sw_drop++;
		return NULL;
	}

	va             = page_address(di->page) + head_offset;
	data           = va + rx_headroom;
	frag_size      = MLX5_SKB_FRAG_SZ(rx_headroom + cqe_bcnt32);

	dma_sync_single_range_for_cpu(rq->pdev, di->addr, head_offset,
				      frag_size, DMA_FROM_DEVICE);
	net_prefetchw(va); /* xdp_frame data area */
	net_prefetch(data);

	mlx5e_fill_xdp_buff(rq, va, rx_headroom, cqe_bcnt32, &xdp);
	if (mlx5e_xdp_handle(rq, di, &cqe_bcnt32, &xdp)) {
		if (__test_and_clear_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags))
			__set_bit(page_idx, wi->xdp_xmit_bitmap); /* non-atomic */
		return NULL; /* page/packet was consumed by XDP */
	}

	rx_headroom = xdp.data - xdp.data_hard_start;
	frag_size = MLX5_SKB_FRAG_SZ(rx_headroom + cqe_bcnt32);
	skb = mlx5e_build_linear_skb(rq, va, frag_size, rx_headroom, cqe_bcnt32);
	if (unlikely(!skb))
		return NULL;

	/* queue up for recycling/reuse */
	page_ref_inc(di->page);

	return skb;
}

static void mlx5e_handle_rx_cqe_mpwrq(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe)
{
	u16 cstrides       = mpwrq_get_cqe_consumed_strides(cqe);
	u16 wqe_id         = be16_to_cpu(cqe->wqe_id);
	struct mlx5e_mpw_info *wi = &rq->mpwqe.info[wqe_id];
	u16 stride_ix      = mpwrq_get_cqe_stride_index(cqe);
	u32 wqe_offset     = stride_ix << rq->mpwqe.log_stride_sz;
	u32 head_offset    = wqe_offset & (PAGE_SIZE - 1);
	u32 page_idx       = wqe_offset >> PAGE_SHIFT;
	struct mlx5e_rx_wqe_ll *wqe;
	struct mlx5_wq_ll *wq;
	struct sk_buff *skb;
	u16 cqe_bcnt;

	wi->consumed_strides += cstrides;

	if (unlikely(MLX5E_RX_ERR_CQE(cqe))) {
		trigger_report(rq, cqe);
		rq->stats->wqe_err++;
		goto mpwrq_cqe_out;
	}

	if (unlikely(mpwrq_is_filler_cqe(cqe))) {
		struct mlx5e_rq_stats *stats = rq->stats;

		stats->mpwqe_filler_cqes++;
		stats->mpwqe_filler_strides += cstrides;
		goto mpwrq_cqe_out;
	}

	cqe_bcnt = mpwrq_get_cqe_byte_cnt(cqe);

	skb = INDIRECT_CALL_2(rq->mpwqe.skb_from_cqe_mpwrq,
			      mlx5e_skb_from_cqe_mpwrq_linear,
			      mlx5e_skb_from_cqe_mpwrq_nonlinear,
			      rq, wi, cqe_bcnt, head_offset, page_idx);
	if (!skb)
		goto mpwrq_cqe_out;

	mlx5e_complete_rx_cqe(rq, cqe, cqe_bcnt, skb);

	if (mlx5e_cqe_regb_chain(cqe))
		if (!mlx5e_tc_update_skb(cqe, skb)) {
			dev_kfree_skb_any(skb);
			goto mpwrq_cqe_out;
		}

	napi_gro_receive(rq->cq.napi, skb);

mpwrq_cqe_out:
	if (likely(wi->consumed_strides < rq->mpwqe.num_strides))
		return;

	wq  = &rq->mpwqe.wq;
	wqe = mlx5_wq_ll_get_wqe(wq, wqe_id);
	mlx5e_free_rx_mpwqe(rq, wi, true);
	mlx5_wq_ll_pop(wq, cqe->wqe_id, &wqe->next.next_wqe_index);
}

int mlx5e_poll_rx_cq(struct mlx5e_cq *cq, int budget)
{
	struct mlx5e_rq *rq = container_of(cq, struct mlx5e_rq, cq);
	struct mlx5_cqwq *cqwq = &cq->wq;
	struct mlx5_cqe64 *cqe;
	int work_done = 0;

	if (unlikely(!test_bit(MLX5E_RQ_STATE_ENABLED, &rq->state)))
		return 0;

	if (rq->page_pool)
		page_pool_nid_changed(rq->page_pool, numa_mem_id());

	if (rq->cqd.left) {
		work_done += mlx5e_decompress_cqes_cont(rq, cqwq, 0, budget);
		if (rq->cqd.left || work_done >= budget)
			goto out;
	}

	cqe = mlx5_cqwq_get_cqe(cqwq);
	if (!cqe) {
		if (unlikely(work_done))
			goto out;
		return 0;
	}

	do {
		if (mlx5_get_cqe_format(cqe) == MLX5_COMPRESSED) {
			work_done +=
				mlx5e_decompress_cqes_start(rq, cqwq,
							    budget - work_done);
			continue;
		}

		mlx5_cqwq_pop(cqwq);

		INDIRECT_CALL_2(rq->handle_rx_cqe, mlx5e_handle_rx_cqe_mpwrq,
				mlx5e_handle_rx_cqe, rq, cqe);
	} while ((++work_done < budget) && (cqe = mlx5_cqwq_get_cqe(cqwq)));

out:
	if (rcu_access_pointer(rq->xdp_prog))
		mlx5e_xdp_rx_poll_complete(rq);

	mlx5_cqwq_update_db_record(cqwq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	return work_done;
}

#ifdef CONFIG_MLX5_CORE_IPOIB

#define MLX5_IB_GRH_SGID_OFFSET 8
#define MLX5_IB_GRH_DGID_OFFSET 24
#define MLX5_GID_SIZE           16

static inline void mlx5i_complete_rx_cqe(struct mlx5e_rq *rq,
					 struct mlx5_cqe64 *cqe,
					 u32 cqe_bcnt,
					 struct sk_buff *skb)
{
	struct hwtstamp_config *tstamp;
	struct mlx5e_rq_stats *stats;
	struct net_device *netdev;
	struct mlx5e_priv *priv;
	char *pseudo_header;
	u32 flags_rqpn;
	u32 qpn;
	u8 *dgid;
	u8 g;

	qpn = be32_to_cpu(cqe->sop_drop_qpn) & 0xffffff;
	netdev = mlx5i_pkey_get_netdev(rq->netdev, qpn);

	/* No mapping present, cannot process SKB. This might happen if a child
	 * interface is going down while having unprocessed CQEs on parent RQ
	 */
	if (unlikely(!netdev)) {
		/* TODO: add drop counters support */
		skb->dev = NULL;
		pr_warn_once("Unable to map QPN %u to dev - dropping skb\n", qpn);
		return;
	}

	priv = mlx5i_epriv(netdev);
	tstamp = &priv->tstamp;
	stats = &priv->channel_stats[rq->ix].rq;

	flags_rqpn = be32_to_cpu(cqe->flags_rqpn);
	g = (flags_rqpn >> 28) & 3;
	dgid = skb->data + MLX5_IB_GRH_DGID_OFFSET;
	if ((!g) || dgid[0] != 0xff)
		skb->pkt_type = PACKET_HOST;
	else if (memcmp(dgid, netdev->broadcast + 4, MLX5_GID_SIZE) == 0)
		skb->pkt_type = PACKET_BROADCAST;
	else
		skb->pkt_type = PACKET_MULTICAST;

	/* Drop packets that this interface sent, ie multicast packets
	 * that the HCA has replicated.
	 */
	if (g && (qpn == (flags_rqpn & 0xffffff)) &&
	    (memcmp(netdev->dev_addr + 4, skb->data + MLX5_IB_GRH_SGID_OFFSET,
		    MLX5_GID_SIZE) == 0)) {
		skb->dev = NULL;
		return;
	}

	skb_pull(skb, MLX5_IB_GRH_BYTES);

	skb->protocol = *((__be16 *)(skb->data));

	if (netdev->features & NETIF_F_RXCSUM) {
		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum = csum_unfold((__force __sum16)cqe->check_sum);
		stats->csum_complete++;
	} else {
		skb->ip_summed = CHECKSUM_NONE;
		stats->csum_none++;
	}

	if (unlikely(mlx5e_rx_hw_stamp(tstamp)))
		skb_hwtstamps(skb)->hwtstamp = mlx5e_cqe_ts_to_ns(rq->ptp_cyc2time,
								  rq->clock, get_cqe_ts(cqe));
	skb_record_rx_queue(skb, rq->ix);

	if (likely(netdev->features & NETIF_F_RXHASH))
		mlx5e_skb_set_hash(cqe, skb);

	/* 20 bytes of ipoib header and 4 for encap existing */
	pseudo_header = skb_push(skb, MLX5_IPOIB_PSEUDO_LEN);
	memset(pseudo_header, 0, MLX5_IPOIB_PSEUDO_LEN);
	skb_reset_mac_header(skb);
	skb_pull(skb, MLX5_IPOIB_HARD_LEN);

	skb->dev = netdev;

	stats->packets++;
	stats->bytes += cqe_bcnt;
}

static void mlx5i_handle_rx_cqe(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe)
{
	struct mlx5_wq_cyc *wq = &rq->wqe.wq;
	struct mlx5e_wqe_frag_info *wi;
	struct sk_buff *skb;
	u32 cqe_bcnt;
	u16 ci;

	ci       = mlx5_wq_cyc_ctr2ix(wq, be16_to_cpu(cqe->wqe_counter));
	wi       = get_frag(rq, ci);
	cqe_bcnt = be32_to_cpu(cqe->byte_cnt);

	if (unlikely(MLX5E_RX_ERR_CQE(cqe))) {
		rq->stats->wqe_err++;
		goto wq_free_wqe;
	}

	skb = INDIRECT_CALL_2(rq->wqe.skb_from_cqe,
			      mlx5e_skb_from_cqe_linear,
			      mlx5e_skb_from_cqe_nonlinear,
			      rq, cqe, wi, cqe_bcnt);
	if (!skb)
		goto wq_free_wqe;

	mlx5i_complete_rx_cqe(rq, cqe, cqe_bcnt, skb);
	if (unlikely(!skb->dev)) {
		dev_kfree_skb_any(skb);
		goto wq_free_wqe;
	}
	napi_gro_receive(rq->cq.napi, skb);

wq_free_wqe:
	mlx5e_free_rx_wqe(rq, wi, true);
	mlx5_wq_cyc_pop(wq);
}

const struct mlx5e_rx_handlers mlx5i_rx_handlers = {
	.handle_rx_cqe       = mlx5i_handle_rx_cqe,
	.handle_rx_cqe_mpwqe = NULL, /* Not supported */
};
#endif /* CONFIG_MLX5_CORE_IPOIB */

#ifdef CONFIG_MLX5_EN_IPSEC

static void mlx5e_ipsec_handle_rx_cqe(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe)
{
	struct mlx5_wq_cyc *wq = &rq->wqe.wq;
	struct mlx5e_wqe_frag_info *wi;
	struct sk_buff *skb;
	u32 cqe_bcnt;
	u16 ci;

	ci       = mlx5_wq_cyc_ctr2ix(wq, be16_to_cpu(cqe->wqe_counter));
	wi       = get_frag(rq, ci);
	cqe_bcnt = be32_to_cpu(cqe->byte_cnt);

	if (unlikely(MLX5E_RX_ERR_CQE(cqe))) {
		rq->stats->wqe_err++;
		goto wq_free_wqe;
	}

	skb = INDIRECT_CALL_2(rq->wqe.skb_from_cqe,
			      mlx5e_skb_from_cqe_linear,
			      mlx5e_skb_from_cqe_nonlinear,
			      rq, cqe, wi, cqe_bcnt);
	if (unlikely(!skb)) /* a DROP, save the page-reuse checks */
		goto wq_free_wqe;

	skb = mlx5e_ipsec_handle_rx_skb(rq->netdev, skb, &cqe_bcnt);
	if (unlikely(!skb))
		goto wq_free_wqe;

	mlx5e_complete_rx_cqe(rq, cqe, cqe_bcnt, skb);
	napi_gro_receive(rq->cq.napi, skb);

wq_free_wqe:
	mlx5e_free_rx_wqe(rq, wi, true);
	mlx5_wq_cyc_pop(wq);
}

#endif /* CONFIG_MLX5_EN_IPSEC */

int mlx5e_rq_set_handlers(struct mlx5e_rq *rq, struct mlx5e_params *params, bool xsk)
{
	struct net_device *netdev = rq->netdev;
	struct mlx5_core_dev *mdev = rq->mdev;
	struct mlx5e_priv *priv = rq->priv;

	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		rq->mpwqe.skb_from_cqe_mpwrq = xsk ?
			mlx5e_xsk_skb_from_cqe_mpwrq_linear :
			mlx5e_rx_mpwqe_is_linear_skb(mdev, params, NULL) ?
				mlx5e_skb_from_cqe_mpwrq_linear :
				mlx5e_skb_from_cqe_mpwrq_nonlinear;
		rq->post_wqes = mlx5e_post_rx_mpwqes;
		rq->dealloc_wqe = mlx5e_dealloc_rx_mpwqe;

		rq->handle_rx_cqe = priv->profile->rx_handlers->handle_rx_cqe_mpwqe;
		if (mlx5_fpga_is_ipsec_device(mdev)) {
			netdev_err(netdev, "MPWQE RQ with Innova IPSec offload not supported\n");
			return -EINVAL;
		}
		if (!rq->handle_rx_cqe) {
			netdev_err(netdev, "RX handler of MPWQE RQ is not set\n");
			return -EINVAL;
		}
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		rq->wqe.skb_from_cqe = xsk ?
			mlx5e_xsk_skb_from_cqe_linear :
			mlx5e_rx_is_linear_skb(params, NULL) ?
				mlx5e_skb_from_cqe_linear :
				mlx5e_skb_from_cqe_nonlinear;
		rq->post_wqes = mlx5e_post_rx_wqes;
		rq->dealloc_wqe = mlx5e_dealloc_rx_wqe;

#ifdef CONFIG_MLX5_EN_IPSEC
		if ((mlx5_fpga_ipsec_device_caps(mdev) & MLX5_ACCEL_IPSEC_CAP_DEVICE) &&
		    priv->ipsec)
			rq->handle_rx_cqe = mlx5e_ipsec_handle_rx_cqe;
		else
#endif
			rq->handle_rx_cqe = priv->profile->rx_handlers->handle_rx_cqe;
		if (!rq->handle_rx_cqe) {
			netdev_err(netdev, "RX handler of RQ is not set\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void mlx5e_trap_handle_rx_cqe(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe)
{
	struct mlx5e_priv *priv = netdev_priv(rq->netdev);
	struct mlx5_wq_cyc *wq = &rq->wqe.wq;
	struct mlx5e_wqe_frag_info *wi;
	struct sk_buff *skb;
	u32 cqe_bcnt;
	u16 trap_id;
	u16 ci;

	trap_id  = get_cqe_flow_tag(cqe);
	ci       = mlx5_wq_cyc_ctr2ix(wq, be16_to_cpu(cqe->wqe_counter));
	wi       = get_frag(rq, ci);
	cqe_bcnt = be32_to_cpu(cqe->byte_cnt);

	if (unlikely(MLX5E_RX_ERR_CQE(cqe))) {
		rq->stats->wqe_err++;
		goto free_wqe;
	}

	skb = mlx5e_skb_from_cqe_nonlinear(rq, cqe, wi, cqe_bcnt);
	if (!skb)
		goto free_wqe;

	mlx5e_complete_rx_cqe(rq, cqe, cqe_bcnt, skb);
	skb_push(skb, ETH_HLEN);

	mlx5_devlink_trap_report(rq->mdev, trap_id, skb, &priv->dl_port);
	dev_kfree_skb_any(skb);

free_wqe:
	mlx5e_free_rx_wqe(rq, wi, false);
	mlx5_wq_cyc_pop(wq);
}

void mlx5e_rq_set_trap_handlers(struct mlx5e_rq *rq, struct mlx5e_params *params)
{
	rq->wqe.skb_from_cqe = mlx5e_rx_is_linear_skb(params, NULL) ?
			       mlx5e_skb_from_cqe_linear :
			       mlx5e_skb_from_cqe_nonlinear;
	rq->post_wqes = mlx5e_post_rx_wqes;
	rq->dealloc_wqe = mlx5e_dealloc_rx_wqe;
	rq->handle_rx_cqe = mlx5e_trap_handle_rx_cqe;
}
