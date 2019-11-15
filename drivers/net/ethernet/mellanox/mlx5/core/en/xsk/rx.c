// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "rx.h"
#include "en/xdp.h"
#include <net/xdp_sock.h>

/* RX data path */

bool mlx5e_xsk_pages_enough_umem(struct mlx5e_rq *rq, int count)
{
	/* Check in advance that we have enough frames, instead of allocating
	 * one-by-one, failing and moving frames to the Reuse Ring.
	 */
	return xsk_umem_has_addrs_rq(rq->umem, count);
}

int mlx5e_xsk_page_alloc_umem(struct mlx5e_rq *rq,
			      struct mlx5e_dma_info *dma_info)
{
	struct xdp_umem *umem = rq->umem;
	u64 handle;

	if (!xsk_umem_peek_addr_rq(umem, &handle))
		return -ENOMEM;

	dma_info->xsk.handle = xsk_umem_adjust_offset(umem, handle,
						      rq->buff.umem_headroom);
	dma_info->xsk.data = xdp_umem_get_data(umem, dma_info->xsk.handle);

	/* No need to add headroom to the DMA address. In striding RQ case, we
	 * just provide pages for UMR, and headroom is counted at the setup
	 * stage when creating a WQE. In non-striding RQ case, headroom is
	 * accounted in mlx5e_alloc_rx_wqe.
	 */
	dma_info->addr = xdp_umem_get_dma(umem, handle);

	xsk_umem_discard_addr_rq(umem);

	dma_sync_single_for_device(rq->pdev, dma_info->addr, PAGE_SIZE,
				   DMA_BIDIRECTIONAL);

	return 0;
}

static inline void mlx5e_xsk_recycle_frame(struct mlx5e_rq *rq, u64 handle)
{
	xsk_umem_fq_reuse(rq->umem, handle & rq->umem->chunk_mask);
}

/* XSKRQ uses pages from UMEM, they must not be released. They are returned to
 * the userspace if possible, and if not, this function is called to reuse them
 * in the driver.
 */
void mlx5e_xsk_page_release(struct mlx5e_rq *rq,
			    struct mlx5e_dma_info *dma_info)
{
	mlx5e_xsk_recycle_frame(rq, dma_info->xsk.handle);
}

/* Return a frame back to the hardware to fill in again. It is used by XDP when
 * the XDP program returns XDP_TX or XDP_REDIRECT not to an XSKMAP.
 */
void mlx5e_xsk_zca_free(struct zero_copy_allocator *zca, unsigned long handle)
{
	struct mlx5e_rq *rq = container_of(zca, struct mlx5e_rq, zca);

	mlx5e_xsk_recycle_frame(rq, handle);
}

static struct sk_buff *mlx5e_xsk_construct_skb(struct mlx5e_rq *rq, void *data,
					       u32 cqe_bcnt)
{
	struct sk_buff *skb;

	skb = napi_alloc_skb(rq->cq.napi, cqe_bcnt);
	if (unlikely(!skb)) {
		rq->stats->buff_alloc_err++;
		return NULL;
	}

	skb_put_data(skb, data, cqe_bcnt);

	return skb;
}

struct sk_buff *mlx5e_xsk_skb_from_cqe_mpwrq_linear(struct mlx5e_rq *rq,
						    struct mlx5e_mpw_info *wi,
						    u16 cqe_bcnt,
						    u32 head_offset,
						    u32 page_idx)
{
	struct mlx5e_dma_info *di = &wi->umr.dma_info[page_idx];
	u16 rx_headroom = rq->buff.headroom - rq->buff.umem_headroom;
	u32 cqe_bcnt32 = cqe_bcnt;
	void *va, *data;
	u32 frag_size;
	bool consumed;

	/* Check packet size. Note LRO doesn't use linear SKB */
	if (unlikely(cqe_bcnt > rq->hw_mtu)) {
		rq->stats->oversize_pkts_sw_drop++;
		return NULL;
	}

	/* head_offset is not used in this function, because di->xsk.data and
	 * di->addr point directly to the necessary place. Furthermore, in the
	 * current implementation, UMR pages are mapped to XSK frames, so
	 * head_offset should always be 0.
	 */
	WARN_ON_ONCE(head_offset);

	va             = di->xsk.data;
	data           = va + rx_headroom;
	frag_size      = rq->buff.headroom + cqe_bcnt32;

	dma_sync_single_for_cpu(rq->pdev, di->addr, frag_size, DMA_BIDIRECTIONAL);
	prefetch(data);

	rcu_read_lock();
	consumed = mlx5e_xdp_handle(rq, di, va, &rx_headroom, &cqe_bcnt32, true);
	rcu_read_unlock();

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

	if (likely(consumed)) {
		if (likely(__test_and_clear_bit(MLX5E_RQ_FLAG_XDP_XMIT, rq->flags)))
			__set_bit(page_idx, wi->xdp_xmit_bitmap); /* non-atomic */
		return NULL; /* page/packet was consumed by XDP */
	}

	/* XDP_PASS: copy the data from the UMEM to a new SKB and reuse the
	 * frame. On SKB allocation failure, NULL is returned.
	 */
	return mlx5e_xsk_construct_skb(rq, data, cqe_bcnt32);
}

struct sk_buff *mlx5e_xsk_skb_from_cqe_linear(struct mlx5e_rq *rq,
					      struct mlx5_cqe64 *cqe,
					      struct mlx5e_wqe_frag_info *wi,
					      u32 cqe_bcnt)
{
	struct mlx5e_dma_info *di = wi->di;
	u16 rx_headroom = rq->buff.headroom - rq->buff.umem_headroom;
	void *va, *data;
	bool consumed;
	u32 frag_size;

	/* wi->offset is not used in this function, because di->xsk.data and
	 * di->addr point directly to the necessary place. Furthermore, in the
	 * current implementation, one page = one packet = one frame, so
	 * wi->offset should always be 0.
	 */
	WARN_ON_ONCE(wi->offset);

	va             = di->xsk.data;
	data           = va + rx_headroom;
	frag_size      = rq->buff.headroom + cqe_bcnt;

	dma_sync_single_for_cpu(rq->pdev, di->addr, frag_size, DMA_BIDIRECTIONAL);
	prefetch(data);

	if (unlikely(get_cqe_opcode(cqe) != MLX5_CQE_RESP_SEND)) {
		rq->stats->wqe_err++;
		return NULL;
	}

	rcu_read_lock();
	consumed = mlx5e_xdp_handle(rq, di, va, &rx_headroom, &cqe_bcnt, true);
	rcu_read_unlock();

	if (likely(consumed))
		return NULL; /* page/packet was consumed by XDP */

	/* XDP_PASS: copy the data from the UMEM to a new SKB. The frame reuse
	 * will be handled by mlx5e_put_rx_frag.
	 * On SKB allocation failure, NULL is returned.
	 */
	return mlx5e_xsk_construct_skb(rq, data, cqe_bcnt);
}
