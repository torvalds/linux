/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2021, Mellanox Technologies inc. All rights reserved.
 */

#ifndef _MLX5_IB_DM_H
#define _MLX5_IB_DM_H

#include "mlx5_ib.h"

extern const struct ib_device_ops mlx5_ib_dev_dm_ops;
extern const struct uapi_definition mlx5_ib_dm_defs[];

struct mlx5_ib_dm {
	struct ib_dm		ibdm;
	u32			type;
	phys_addr_t		dev_addr;
	size_t			size;
};

struct mlx5_ib_dm_op_entry {
	struct mlx5_user_mmap_entry	mentry;
	phys_addr_t			op_addr;
	struct mlx5_ib_dm_memic		*dm;
	u8				op;
};

struct mlx5_ib_dm_memic {
	struct mlx5_ib_dm           base;
	struct mlx5_user_mmap_entry mentry;
	struct xarray               ops;
	struct mutex                ops_xa_lock;
	struct kref                 ref;
	size_t                      req_length;
};

struct mlx5_ib_dm_icm {
	struct mlx5_ib_dm      base;
	u32                    obj_id;
};

static inline struct mlx5_ib_dm *to_mdm(struct ib_dm *ibdm)
{
	return container_of(ibdm, struct mlx5_ib_dm, ibdm);
}

static inline struct mlx5_ib_dm_memic *to_memic(struct ib_dm *ibdm)
{
	return container_of(ibdm, struct mlx5_ib_dm_memic, base.ibdm);
}

static inline struct mlx5_ib_dm_icm *to_icm(struct ib_dm *ibdm)
{
	return container_of(ibdm, struct mlx5_ib_dm_icm, base.ibdm);
}

struct ib_dm *mlx5_ib_alloc_dm(struct ib_device *ibdev,
			       struct ib_ucontext *context,
			       struct ib_dm_alloc_attr *attr,
			       struct uverbs_attr_bundle *attrs);
void mlx5_ib_dm_mmap_free(struct mlx5_ib_dev *dev,
			  struct mlx5_user_mmap_entry *mentry);
void mlx5_cmd_dealloc_memic(struct mlx5_dm *dm, phys_addr_t addr,
			    u64 length);
void mlx5_cmd_dealloc_memic_op(struct mlx5_dm *dm, phys_addr_t addr,
			       u8 operation);

#endif /* _MLX5_IB_DM_H */
