/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_XSK_RX_H__
#define __MLX5_EN_XSK_RX_H__

#include "en.h"

/* RX data path */

int mlx5e_xsk_alloc_rx_mpwqe(struct mlx5e_rq *rq, u16 ix);
int mlx5e_xsk_alloc_rx_wqes_batched(struct mlx5e_rq *rq, u16 ix, int wqe_bulk);
int mlx5e_xsk_alloc_rx_wqes(struct mlx5e_rq *rq, u16 ix, int wqe_bulk);
struct sk_buff *mlx5e_xsk_skb_from_cqe_mpwrq_linear(struct mlx5e_rq *rq,
						    struct mlx5e_mpw_info *wi,
						    u16 cqe_bcnt,
						    u32 head_offset,
						    u32 page_idx);
struct sk_buff *mlx5e_xsk_skb_from_cqe_linear(struct mlx5e_rq *rq,
					      struct mlx5e_wqe_frag_info *wi,
					      u32 cqe_bcnt);

#endif /* __MLX5_EN_XSK_RX_H__ */
