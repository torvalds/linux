/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2020, Mellanox Technologies inc. All rights reserved.
 */

#ifndef _MLX5_IB_WR_H
#define _MLX5_IB_WR_H

#include "mlx5_ib.h"

enum {
	MLX5_IB_SQ_UMR_INLINE_THRESHOLD = 64,
};

struct mlx5_wqe_eth_pad {
	u8 rsvd0[16];
};


/* get_sq_edge - Get the next nearby edge.
 *
 * An 'edge' is defined as the first following address after the end
 * of the fragment or the SQ. Accordingly, during the WQE construction
 * which repetitively increases the pointer to write the next data, it
 * simply should check if it gets to an edge.
 *
 * @sq - SQ buffer.
 * @idx - Stride index in the SQ buffer.
 *
 * Return:
 *	The new edge.
 */
static inline void *get_sq_edge(struct mlx5_ib_wq *sq, u32 idx)
{
	void *fragment_end;

	fragment_end = mlx5_frag_buf_get_wqe
		(&sq->fbc,
		 mlx5_frag_buf_get_idx_last_contig_stride(&sq->fbc, idx));

	return fragment_end + MLX5_SEND_WQE_BB;
}

int mlx5_ib_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		      const struct ib_send_wr **bad_wr, bool drain);
int mlx5_ib_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr, bool drain);

static inline int mlx5_ib_post_send_nodrain(struct ib_qp *ibqp,
					    const struct ib_send_wr *wr,
					    const struct ib_send_wr **bad_wr)
{
	return mlx5_ib_post_send(ibqp, wr, bad_wr, false);
}

static inline int mlx5_ib_post_send_drain(struct ib_qp *ibqp,
					  const struct ib_send_wr *wr,
					  const struct ib_send_wr **bad_wr)
{
	return mlx5_ib_post_send(ibqp, wr, bad_wr, true);
}

static inline int mlx5_ib_post_recv_nodrain(struct ib_qp *ibqp,
					    const struct ib_recv_wr *wr,
					    const struct ib_recv_wr **bad_wr)
{
	return mlx5_ib_post_recv(ibqp, wr, bad_wr, false);
}

static inline int mlx5_ib_post_recv_drain(struct ib_qp *ibqp,
					  const struct ib_recv_wr *wr,
					  const struct ib_recv_wr **bad_wr)
{
	return mlx5_ib_post_recv(ibqp, wr, bad_wr, true);
}
#endif /* _MLX5_IB_WR_H */
