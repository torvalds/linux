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
	phys_addr_t		dev_addr;
	u32			type;
	size_t			size;
	union {
		struct {
			u32	obj_id;
		} icm_dm;
		/* other dm types specific params should be added here */
	};
	struct mlx5_user_mmap_entry mentry;
};

static inline struct mlx5_ib_dm *to_mdm(struct ib_dm *ibdm)
{
	return container_of(ibdm, struct mlx5_ib_dm, ibdm);
}

struct ib_dm *mlx5_ib_alloc_dm(struct ib_device *ibdev,
			       struct ib_ucontext *context,
			       struct ib_dm_alloc_attr *attr,
			       struct uverbs_attr_bundle *attrs);
int mlx5_ib_dealloc_dm(struct ib_dm *ibdm, struct uverbs_attr_bundle *attrs);
void mlx5_cmd_dealloc_memic(struct mlx5_dm *dm, phys_addr_t addr,
			    u64 length);

#endif /* _MLX5_IB_DM_H */
