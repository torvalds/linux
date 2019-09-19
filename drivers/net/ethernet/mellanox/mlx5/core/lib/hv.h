/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __LIB_HV_H__
#define __LIB_HV_H__

#if IS_ENABLED(CONFIG_PCI_HYPERV_INTERFACE)

#include <linux/hyperv.h>
#include <linux/mlx5/driver.h>

int mlx5_hv_read_config(struct mlx5_core_dev *dev, void *buf, int len,
			int offset);
int mlx5_hv_write_config(struct mlx5_core_dev *dev, void *buf, int len,
			 int offset);
int mlx5_hv_register_invalidate(struct mlx5_core_dev *dev, void *context,
				void (*block_invalidate)(void *context,
							 u64 block_mask));
void mlx5_hv_unregister_invalidate(struct mlx5_core_dev *dev);
#endif

#endif /* __LIB_HV_H__ */
