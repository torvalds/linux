/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2013-2020, Mellanox Technologies inc. All rights reserved.
 */

#ifndef _MLX5_IB_FS_H
#define _MLX5_IB_FS_H

#include "mlx5_ib.h"

#if IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS)
int mlx5_ib_fs_init(struct mlx5_ib_dev *dev);
#else
static inline int mlx5_ib_fs_init(struct mlx5_ib_dev *dev)
{
	dev->flow_db = kzalloc(sizeof(*dev->flow_db), GFP_KERNEL);

	if (!dev->flow_db)
		return -ENOMEM;

	mutex_init(&dev->flow_db->lock);
	return 0;
}
#endif
static inline void mlx5_ib_fs_cleanup(struct mlx5_ib_dev *dev)
{
	kfree(dev->flow_db);
}
#endif /* _MLX5_IB_FS_H */
