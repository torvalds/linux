/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2019-2020, Mellanox Technologies inc. All rights reserved.
 */

#ifndef _MLX5_IB_DEVX_H
#define _MLX5_IB_DEVX_H

#include "mlx5_ib.h"

#define MLX5_MAX_DESTROY_INBOX_SIZE_DW MLX5_ST_SZ_DW(delete_fte_in)
struct devx_obj {
	struct mlx5_ib_dev	*ib_dev;
	u64			obj_id;
	u32			dinlen; /* destroy inbox length */
	u32			dinbox[MLX5_MAX_DESTROY_INBOX_SIZE_DW];
	u32			flags;
	union {
		struct mlx5_ib_mkey	mkey;
		struct mlx5_core_dct	core_dct;
		struct mlx5_core_cq	core_cq;
		u32			flow_counter_bulk_size;
	};
	struct list_head event_sub; /* holds devx_event_subscription entries */
};
#if IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS)
int mlx5_ib_devx_create(struct mlx5_ib_dev *dev, bool is_user, u64 req_ucaps);
void mlx5_ib_devx_destroy(struct mlx5_ib_dev *dev, u16 uid);
int mlx5_ib_devx_init(struct mlx5_ib_dev *dev);
void mlx5_ib_devx_cleanup(struct mlx5_ib_dev *dev);
void mlx5_ib_ufile_hw_cleanup(struct ib_uverbs_file *ufile);
#else
static inline int mlx5_ib_devx_create(struct mlx5_ib_dev *dev, bool is_user,
				      u64 req_ucaps)
{
	return -EOPNOTSUPP;
}
static inline void mlx5_ib_devx_destroy(struct mlx5_ib_dev *dev, u16 uid) {}
static inline int mlx5_ib_devx_init(struct mlx5_ib_dev *dev)
{
	return 0;
}
static inline void mlx5_ib_devx_cleanup(struct mlx5_ib_dev *dev)
{
}
static inline void mlx5_ib_ufile_hw_cleanup(struct ib_uverbs_file *ufile)
{
}
#endif
#endif /* _MLX5_IB_DEVX_H */
