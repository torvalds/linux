/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2013-2020, Mellanox Technologies inc. All rights reserved.
 */

#ifndef _MLX5_IB_FS_H
#define _MLX5_IB_FS_H

#include "mlx5_ib.h"

#if IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS)
int mlx5_ib_fs_init(struct mlx5_ib_dev *dev);
void mlx5_ib_fs_cleanup_anchor(struct mlx5_ib_dev *dev);
#else
static inline int mlx5_ib_fs_init(struct mlx5_ib_dev *dev)
{
	dev->flow_db = kzalloc(sizeof(*dev->flow_db), GFP_KERNEL);

	if (!dev->flow_db)
		return -ENOMEM;

	mutex_init(&dev->flow_db->lock);
	return 0;
}

inline void mlx5_ib_fs_cleanup_anchor(struct mlx5_ib_dev *dev) {}
#endif

static inline void mlx5_ib_fs_cleanup(struct mlx5_ib_dev *dev)
{
	/* When a steering anchor is created, a special flow table is also
	 * created for the user to reference. Since the user can reference it,
	 * the kernel cannot trust that when the user destroys the steering
	 * anchor, they no longer reference the flow table.
	 *
	 * To address this issue, when a user destroys a steering anchor, only
	 * the flow steering rule in the table is destroyed, but the table
	 * itself is kept to deal with the above scenario. The remaining
	 * resources are only removed when the RDMA device is destroyed, which
	 * is a safe assumption that all references are gone.
	 */
	mlx5_ib_fs_cleanup_anchor(dev);
	kfree(dev->flow_db);
}
#endif /* _MLX5_IB_FS_H */
