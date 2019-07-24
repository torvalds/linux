/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies */

#ifndef __MLX5_PCI_VSC_H__
#define __MLX5_PCI_VSC_H__

enum mlx5_vsc_state {
	MLX5_VSC_UNLOCK,
	MLX5_VSC_LOCK,
};

enum {
	MLX5_VSC_SPACE_SCAN_CRSPACE = 0x7,
};

void mlx5_pci_vsc_init(struct mlx5_core_dev *dev);
int mlx5_vsc_gw_lock(struct mlx5_core_dev *dev);
int mlx5_vsc_gw_unlock(struct mlx5_core_dev *dev);
int mlx5_vsc_gw_set_space(struct mlx5_core_dev *dev, u16 space,
			  u32 *ret_space_size);
int mlx5_vsc_gw_read_block_fast(struct mlx5_core_dev *dev, u32 *data,
				int length);

static inline bool mlx5_vsc_accessible(struct mlx5_core_dev *dev)
{
	return !!dev->vsc_addr;
}

int mlx5_vsc_sem_set_space(struct mlx5_core_dev *dev, u16 space,
			   enum mlx5_vsc_state state);

#endif /* __MLX5_PCI_VSC_H__ */
