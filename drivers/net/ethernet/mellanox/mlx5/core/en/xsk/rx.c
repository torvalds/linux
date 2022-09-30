// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "rx.h"
#include "en/xdp.h"
#include <net/xdp_sock_drv.h>
#include <linux/filter.h>

/* RX data path */

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

	xdp->data_end = xdp->data + cqe_bcnt;
	xdp_set_data_meta_invalid(xdp);
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
	return mlx5e_xsk_construct_skb(rq, xdp->data, xdp->data_end - xdp->data);
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

	xdp->data_end = xdp->data + cqe_bcnt;
	xdp_set_data_meta_invalid(xdp);
	xsk_buff_dma_sync_for_cpu(xdp, rq->xsk_pool);
	net_prefetch(xdp->data);

	prog = rcu_dereference(rq->xdp_prog);
	if (likely(prog && mlx5e_xdp_handle(rq, NULL, prog, xdp)))
		return NULL; /* page/packet was consumed by XDP */

	/* XDP_PASS: copy the data from the UMEM to a new SKB. The frame reuse
	 * will be handled by mlx5e_put_rx_frag.
	 * On SKB allocation failure, NULL is returned.
	 */
	return mlx5e_xsk_construct_skb(rq, xdp->data, xdp->data_end - xdp->data);
}
