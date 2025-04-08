/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2013-2020, Mellanox Technologies inc. All rights reserved.
 */

#ifndef _MLX5_IB_QP_H
#define _MLX5_IB_QP_H

struct mlx5_ib_dev;

struct mlx5_qp_table {
	struct notifier_block nb;
	struct xarray dct_xa;

	/* protect radix tree
	 */
	spinlock_t lock;
	struct radix_tree_root tree;
};

int mlx5_init_qp_table(struct mlx5_ib_dev *dev);
void mlx5_cleanup_qp_table(struct mlx5_ib_dev *dev);

int mlx5_core_create_dct(struct mlx5_ib_dev *dev, struct mlx5_core_dct *qp,
			 u32 *in, int inlen, u32 *out, int outlen);
int mlx5_qpc_create_qp(struct mlx5_ib_dev *dev, struct mlx5_core_qp *qp,
		       u32 *in, int inlen, u32 *out);
int mlx5_core_qp_modify(struct mlx5_ib_dev *dev, u16 opcode, u32 opt_param_mask,
			void *qpc, struct mlx5_core_qp *qp, u32 *ece);
int mlx5_core_destroy_qp(struct mlx5_ib_dev *dev, struct mlx5_core_qp *qp);
int mlx5_core_destroy_dct(struct mlx5_ib_dev *dev, struct mlx5_core_dct *dct);
int mlx5_core_qp_query(struct mlx5_ib_dev *dev, struct mlx5_core_qp *qp,
		       u32 *out, int outlen, bool qpc_ext);
int mlx5_core_dct_query(struct mlx5_ib_dev *dev, struct mlx5_core_dct *dct,
			u32 *out, int outlen);

int mlx5_core_set_delay_drop(struct mlx5_ib_dev *dev, u32 timeout_usec);

int mlx5_core_destroy_rq_tracked(struct mlx5_ib_dev *dev,
				 struct mlx5_core_qp *rq);
int mlx5_core_create_sq_tracked(struct mlx5_ib_dev *dev, u32 *in, int inlen,
				struct mlx5_core_qp *sq);
void mlx5_core_destroy_sq_tracked(struct mlx5_ib_dev *dev,
				  struct mlx5_core_qp *sq);

int mlx5_core_create_rq_tracked(struct mlx5_ib_dev *dev, u32 *in, int inlen,
				struct mlx5_core_qp *rq);

struct mlx5_core_rsc_common *mlx5_core_res_hold(struct mlx5_ib_dev *dev,
						int res_num,
						enum mlx5_res_type res_type);
void mlx5_core_res_put(struct mlx5_core_rsc_common *res);

int mlx5_core_xrcd_alloc(struct mlx5_ib_dev *dev, u32 *xrcdn);
int mlx5_core_xrcd_dealloc(struct mlx5_ib_dev *dev, u32 xrcdn);
int mlx5_ib_qp_set_counter(struct ib_qp *qp, struct rdma_counter *counter);
int mlx5_ib_qp_event_init(void);
void mlx5_ib_qp_event_cleanup(void);
int mlx5r_ib_rate(struct mlx5_ib_dev *dev, u8 rate);
#endif /* _MLX5_IB_QP_H */
