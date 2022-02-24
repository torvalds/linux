/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef MLX5_VFIO_CMD_H
#define MLX5_VFIO_CMD_H

#include <linux/kernel.h>
#include <linux/mlx5/driver.h>

struct mlx5_vf_migration_file {
	struct file *filp;
	struct mutex lock;

	struct sg_append_table table;
	size_t total_length;
	size_t allocated_length;

	/* Optimize mlx5vf_get_migration_page() for sequential access */
	struct scatterlist *last_offset_sg;
	unsigned int sg_last_entry;
	unsigned long last_offset;
};

int mlx5vf_cmd_suspend_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod);
int mlx5vf_cmd_resume_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod);
int mlx5vf_cmd_query_vhca_migration_state(struct pci_dev *pdev, u16 vhca_id,
					  size_t *state_size);
int mlx5vf_cmd_get_vhca_id(struct pci_dev *pdev, u16 function_id, u16 *vhca_id);
int mlx5vf_cmd_save_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       struct mlx5_vf_migration_file *migf);
int mlx5vf_cmd_load_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       struct mlx5_vf_migration_file *migf);
#endif /* MLX5_VFIO_CMD_H */
