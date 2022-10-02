// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "rx.h"
#include "en/xdp.h"
#include <net/xdp_sock_drv.h>
#include <linux/filter.h>

/* RX data path */

int mlx5e_xsk_alloc_rx_mpwqe(struct mlx5e_rq *rq, u16 ix)
{
	struct mlx5e_mpw_info *wi = mlx5e_get_mpw_info(rq, ix);
	struct mlx5e_icosq *icosq = rq->icosq;
	struct mlx5_wq_cyc *wq = &icosq->wq;
	struct mlx5e_umr_wqe *umr_wqe;
	int batch, i;
	u32 offset; /* 17-bit value with MTT. */
	u16 pi;

	if (unlikely(!xsk_buff_can_alloc(rq->xsk_pool, rq->mpwqe.pages_per_wqe)))
		goto err;

	BUILD_BUG_ON(sizeof(wi->alloc_units[0]) != sizeof(wi->alloc_units[0].xsk));
	batch = xsk_buff_alloc_batch(rq->xsk_pool, (struct xdp_buff **)wi->alloc_units,
				     rq->mpwqe.pages_per_wqe);

	/* If batch < pages_per_wqe, either:
	 * 1. Some (or all) descriptors were invalid.
	 * 2. dma_need_sync is true, and it fell back to allocating one frame.
	 * In either case, try to continue allocating frames one by one, until
	 * the first error, which will mean there are no more valid descriptors.
	 */
	for (; batch < rq->mpwqe.pages_per_wqe; batch++) {
		wi->alloc_units[batch].xsk = xsk_buff_alloc(rq->xsk_pool);
		if (unlikely(!wi->alloc_units[batch].xsk))
			goto err_reuse_batch;
	}

	pi = mlx5e_icosq_get_next_pi(icosq, rq->mpwqe.umr_wqebbs);
	umr_wqe = mlx5_wq_cyc_get_wqe(wq, pi);
	memcpy(umr_wqe, &rq->mpwqe.umr_wqe, sizeof(struct mlx5e_umr_wqe));

	if (likely(rq->mpwqe.umr_mode == MLX5E_MPWRQ_UMR_MODE_ALIGNED)) {
		for (i = 0; i < batch; i++) {
			dma_addr_t addr = xsk_buff_xdp_get_frame_dma(wi->alloc_units[i].xsk);

			umr_wqe->inline_mtts[i] = (struct mlx5_mtt) {
				.ptag = cpu_to_be64(addr | MLX5_EN_WR),
			};
		}
	} else if (unlikely(rq->mpwqe.umr_mode == MLX5E_MPWRQ_UMR_MODE_UNALIGNED)) {
		for (i = 0; i < batch; i++) {
			dma_addr_t addr = xsk_buff_xdp_get_frame_dma(wi->alloc_units[i].xsk);

			umr_wqe->inline_ksms[i] = (struct mlx5_ksm) {
				.key = rq->mkey_be,
				.va = cpu_to_be64(addr),
			};
		}
	} else {
		__be32 pad_size = cpu_to_be32((1 << rq->mpwqe.page_shift) -
					      rq->xsk_pool->chunk_size);
		__be32 frame_size = cpu_to_be32(rq->xsk_pool->chunk_size);

		for (i = 0; i < batch; i++) {
			dma_addr_t addr = xsk_buff_xdp_get_frame_dma(wi->alloc_units[i].xsk);

			umr_wqe->inline_klms[i << 1] = (struct mlx5_klm) {
				.key = rq->mkey_be,
				.va = cpu_to_be64(addr),
				.bcount = frame_size,
			};
			umr_wqe->inline_klms[(i << 1) + 1] = (struct mlx5_klm) {
				.key = rq->mkey_be,
				.va = cpu_to_be64(rq->wqe_overflow.addr),
				.bcount = pad_size,
			};
		}
	}

	bitmap_zero(wi->xdp_xmit_bitmap, rq->mpwqe.pages_per_wqe);
	wi->consumed_strides = 0;

	umr_wqe->ctrl.opmod_idx_opcode =
		cpu_to_be32((icosq->pc << MLX5_WQE_CTRL_WQE_INDEX_SHIFT) | MLX5_OPCODE_UMR);

	/* Optimized for speed: keep in sync with mlx5e_mpwrq_umr_entry_size. */
	offset = ix * rq->mpwqe.mtts_per_wqe;
	if (likely(rq->mpwqe.umr_mode == MLX5E_MPWRQ_UMR_MODE_ALIGNED))
		offset = offset * sizeof(struct mlx5_mtt) / MLX5_OCTWORD;
	else if (unlikely(rq->mpwqe.umr_mode == MLX5E_MPWRQ_UMR_MODE_OVERSIZED))
		offset = offset * sizeof(struct mlx5_klm) * 2 / MLX5_OCTWORD;
	umr_wqe->uctrl.xlt_offset = cpu_to_be16(offset);

	icosq->db.wqe_info[pi] = (struct mlx5e_icosq_wqe_info) {
		.wqe_type = MLX5E_ICOSQ_WQE_UMR_RX,
		.num_wqebbs = rq->mpwqe.umr_wqebbs,
		.umr.rq = rq,
	};

	icosq->pc += rq->mpwqe.umr_wqebbs;

	icosq->doorbell_cseg = &umr_wqe->ctrl;

	return 0;

err_reuse_batch:
	while (--batch >= 0)
		xsk_buff_free(wi->alloc_units[batch].xsk);

err:
	rq->stats->buff_alloc_err++;
	return -ENOMEM;
}

int mlx5e_xsk_alloc_rx_wqes_batched(struct mlx5e_rq *rq, u16 ix, int wqe_bulk)
{
	struct mlx5_wq_cyc *wq = &rq->wqe.wq;
	struct xdp_buff **buffs;
	u32 contig, alloc;
	int i;

	/* mlx5e_init_frags_partition creates a 1:1 mapping between
	 * rq->wqe.frags and rq->wqe.alloc_units, which allows us to
	 * allocate XDP buffers straight into alloc_units.
	 */
	BUILD_BUG_ON(sizeof(rq->wqe.alloc_units[0]) !=
		     sizeof(rq->wqe.alloc_units[0].xsk));
	buffs = (struct xdp_buff **)rq->wqe.alloc_units;
	contig = mlx5_wq_cyc_get_size(wq) - ix;
	if (wqe_bulk <= contig) {
		alloc = xsk_buff_alloc_batch(rq->xsk_pool, buffs + ix, wqe_bulk);
	} else {
		alloc = xsk_buff_alloc_batch(rq->xsk_pool, buffs + ix, contig);
		if (likely(alloc == contig))
			alloc += xsk_buff_alloc_batch(rq->xsk_pool, buffs, wqe_bulk - contig);
	}

	for (i = 0; i < alloc; i++) {
		int j = mlx5_wq_cyc_ctr2ix(wq, ix + i);
		struct mlx5e_wqe_frag_info *frag;
		struct mlx5e_rx_wqe_cyc *wqe;
		dma_addr_t addr;

		wqe = mlx5_wq_cyc_get_wqe(wq, j);
		/* Assumes log_num_frags == 0. */
		frag = &rq->wqe.frags[j];

		addr = xsk_buff_xdp_get_frame_dma(frag->au->xsk);
		wqe->data[0].addr = cpu_to_be64(addr + rq->buff.headroom);
	}

	return alloc;
}

int mlx5e_xsk_alloc_rx_wqes(struct mlx5e_rq *rq, u16 ix, int wqe_bulk)
{
	struct mlx5_wq_cyc *wq = &rq->wqe.wq;
	int i;

	for (i = 0; i < wqe_bulk; i++) {
		int j = mlx5_wq_cyc_ctr2ix(wq, ix + i);
		struct mlx5e_wqe_frag_info *frag;
		struct mlx5e_rx_wqe_cyc *wqe;
		dma_addr_t addr;

		wqe = mlx5_wq_cyc_get_wqe(wq, j);
		/* Assumes log_num_frags == 0. */
		frag = &rq->wqe.frags[j];

		frag->au->xsk = xsk_buff_alloc(rq->xsk_pool);
		if (unlikely(!frag->au->xsk))
			return i;

		addr = xsk_buff_xdp_get_frame_dma(frag->au->xsk);
		wqe->data[0].addr = cpu_to_be64(addr + rq->buff.headroom);
	}

	return wqe_bulk;
}

static struct sk_buff *mlx5e_xsk_construct_skb(struct mlx5e_rq *rq, struct xdp_buff *xdp)
{
	u32 totallen = xdp->data_end - xdp->data_meta;
	u32 metalen = xdp->data - xdp->data_meta;
	struct sk_buff *skb;

	skb = napi_alloc_skb(rq->cq.napi, totallen);
	if (unlikely(!skb)) {
		rq->stats->buff_alloc_err++;
		return NULL;
	}

	skb_put_data(skb, xdp->data_meta, totallen);

	if (metalen) {
		skb_metadata_set(skb, metalen);
		__skb_pull(skb, metalen);
	}

	return skb;
}

struct sk_buff *mlx5e_xsk_skb_from_cqe_mpwrq_linear(struct mlx5e_rq *rq,
						    struct mlx5e_mpw_info *wi,
						    u16 cqe_bcnt,
						    u32 head_offset,
						    u32 page_idx)
{
	struct xdp_buff *xdp = wi->alloc_units[page_idx].xsk;
	struct bpf_prog *prog;

	/* Check packet size. Note LRO doesn't use linear SKB */
	if (unlikely(cqe_bcnt > rq->hw_mtu)) {
		rq->stats->oversize_pkts_sw_drop++;
		return NULL;
	}

	/* head_offset is not used in this function, because xdp->data and the
	 * DMA address point directly to the necessary place. Furthermore, in
	 * the current implementation, UMR pages are mapped to XSK frames, so
	 * head_offset should always be 0.
	 */
	WARN_ON_ONCE(head_offset);

	xsk_buff_set_size(xdp, cqe_bcnt);
	xsk_buff_dma_sync_for_cpu(xdp, rq->xsk_pool);
	net_prefetch(xdp->data);

	/* Possible flows:
	 * - XDP_REDIRECT to XSKMAP:
	 *   The page is owned by the userspace from now.
	 * - XDP_TX and other XDP_REDIRECTs:
	 *   The page was returned by ZCA and recycled.
	 * - XDP_DROP:
	 *   Recycle the page.
	 * - XDP_PASS:
	 *   Allocate an SKB, copy the data and recycle the page.
	 *
	 * Pages to be recycled go to the Reuse Ring on MPWQE deallocation. Its
	 * size is the same as the Driver RX Ring's size, and pages for WQEs are
	 * allocated first from the Reuse Ring, so it has enough space.
	 */

	prog = rcu_dereference(rq->xdp_prog);
	if (likely(prog && mlx5e_xdp_handle(rq, NULL, prog, xdp))) {
		if (likely(__test_and_clear_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags)))
			__set_bit(page_idx, wi->xdp_xmit_bitmap); /* non-atomic */
		return NULL; /* page/packet was consumed by XDP */
	}

	/* XDP_PASS: copy the data from the UMEM to a new SKB and reuse the
	 * frame. On SKB allocation failure, NULL is returned.
	 */
	return mlx5e_xsk_construct_skb(rq, xdp);
}

struct sk_buff *mlx5e_xsk_skb_from_cqe_linear(struct mlx5e_rq *rq,
					      struct mlx5e_wqe_frag_info *wi,
					      u32 cqe_bcnt)
{
	struct xdp_buff *xdp = wi->au->xsk;
	struct bpf_prog *prog;

	/* wi->offset is not used in this function, because xdp->data and the
	 * DMA address point directly to the necessary place. Furthermore, the
	 * XSK allocator allocates frames per packet, instead of pages, so
	 * wi->offset should always be 0.
	 */
	WARN_ON_ONCE(wi->offset);

	xsk_buff_set_size(xdp, cqe_bcnt);
	xsk_buff_dma_sync_for_cpu(xdp, rq->xsk_pool);
	net_prefetch(xdp->data);

	prog = rcu_dereference(rq->xdp_prog);
	if (likely(prog && mlx5e_xdp_handle(rq, NULL, prog, xdp)))
		return NULL; /* page/packet was consumed by XDP */

	/* XDP_PASS: copy the data from the UMEM to a new SKB. The frame reuse
	 * will be handled by mlx5e_free_rx_wqe.
	 * On SKB allocation failure, NULL is returned.
	 */
	return mlx5e_xsk_construct_skb(rq, xdp);
}
