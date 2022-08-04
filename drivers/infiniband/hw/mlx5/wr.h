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

/* handle_post_send_edge - Check if we get to SQ edge. If yes, update to the
 * next nearby edge and get new address translation for current WQE position.
 * @sq: SQ buffer.
 * @seg: Current WQE position (16B aligned).
 * @wqe_sz: Total current WQE size [16B].
 * @cur_edge: Updated current edge.
 */
static inline void handle_post_send_edge(struct mlx5_ib_wq *sq, void **seg,
					 u32 wqe_sz, void **cur_edge)
{
	u32 idx;

	if (likely(*seg != *cur_edge))
		return;

	idx = (sq->cur_post + (wqe_sz >> 2)) & (sq->wqe_cnt - 1);
	*cur_edge = get_sq_edge(sq, idx);

	*seg = mlx5_frag_buf_get_wqe(&sq->fbc, idx);
}

/* mlx5r_memcpy_send_wqe - copy data from src to WQE and update the relevant
 * WQ's pointers. At the end @seg is aligned to 16B regardless the copied size.
 * @sq: SQ buffer.
 * @cur_edge: Updated current edge.
 * @seg: Current WQE position (16B aligned).
 * @wqe_sz: Total current WQE size [16B].
 * @src: Pointer to copy from.
 * @n: Number of bytes to copy.
 */
static inline void mlx5r_memcpy_send_wqe(struct mlx5_ib_wq *sq, void **cur_edge,
					 void **seg, u32 *wqe_sz,
					 const void *src, size_t n)
{
	while (likely(n)) {
		size_t leftlen = *cur_edge - *seg;
		size_t copysz = min_t(size_t, leftlen, n);
		size_t stride;

		memcpy(*seg, src, copysz);

		n -= copysz;
		src += copysz;
		stride = !n ? ALIGN(copysz, 16) : copysz;
		*seg += stride;
		*wqe_sz += stride >> 4;
		handle_post_send_edge(sq, seg, *wqe_sz, cur_edge);
	}
}

int mlx5r_wq_overflow(struct mlx5_ib_wq *wq, int nreq, struct ib_cq *ib_cq);
int mlx5r_begin_wqe(struct mlx5_ib_qp *qp, void **seg,
		    struct mlx5_wqe_ctrl_seg **ctrl, unsigned int *idx,
		    int *size, void **cur_edge, int nreq, __be32 general_id,
		    bool send_signaled, bool solicited);
void mlx5r_finish_wqe(struct mlx5_ib_qp *qp, struct mlx5_wqe_ctrl_seg *ctrl,
		      void *seg, u8 size, void *cur_edge, unsigned int idx,
		      u64 wr_id, int nreq, u8 fence, u32 mlx5_opcode);
void mlx5r_ring_db(struct mlx5_ib_qp *qp, unsigned int nreq,
		   struct mlx5_wqe_ctrl_seg *ctrl);
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
