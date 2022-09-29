/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_XSK_RX_H__
#define __MLX5_EN_XSK_RX_H__

#include "en.h"
#include <net/xdp_sock_drv.h>

/* RX data path */

struct sk_buff *mlx5e_xsk_skb_from_cqe_mpwrq_linear(struct mlx5e_rq *rq,
						    struct mlx5e_mpw_info *wi,
						    u16 cqe_bcnt,
						    u32 head_offset,
						    u32 page_idx);
struct sk_buff *mlx5e_xsk_skb_from_cqe_linear(struct mlx5e_rq *rq,
					      struct mlx5e_wqe_frag_info *wi,
					      u32 cqe_bcnt);

static inline int mlx5e_xsk_page_alloc_pool(struct mlx5e_rq *rq,
					    struct mlx5e_alloc_unit *au)
{
	au->xsk = xsk_buff_alloc(rq->xsk_pool);
	if (!au->xsk)
		return -ENOMEM;

	/* Store the DMA address without headroom. In striding RQ case, we just
	 * provide pages for UMR, and headroom is counted at the setup stage
	 * when creating a WQE. In non-striding RQ case, headroom is accounted
	 * in mlx5e_alloc_rx_wqe.
	 */
	au->addr = xsk_buff_xdp_get_frame_dma(au->xsk);

	return 0;
}

static inline bool mlx5e_xsk_update_rx_wakeup(struct mlx5e_rq *rq, bool alloc_err)
{
	if (!xsk_uses_need_wakeup(rq->xsk_pool))
		return alloc_err;

	if (unlikely(alloc_err))
		xsk_set_rx_need_wakeup(rq->xsk_pool);
	else
		xsk_clear_rx_need_wakeup(rq->xsk_pool);

	return false;
}

#endif /* __MLX5_EN_XSK_RX_H__ */
