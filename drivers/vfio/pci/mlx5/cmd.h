/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef MLX5_VFIO_CMD_H
#define MLX5_VFIO_CMD_H

#include <linux/kernel.h>
#include <linux/vfio_pci_core.h>
#include <linux/mlx5/driver.h>

struct mlx5_vf_migration_file {
	struct file *filp;
	struct mutex lock;
	bool disabled;

	struct sg_append_table table;
	size_t total_length;
	size_t allocated_length;

	/* Optimize mlx5vf_get_migration_page() for sequential access */
	struct scatterlist *last_offset_sg;
	unsigned int sg_last_entry;
	unsigned long last_offset;
};

struct mlx5vf_pci_core_device {
	struct vfio_pci_core_device core_device;
	int vf_id;
	u16 vhca_id;
	u8 migrate_cap:1;
	u8 deferred_reset:1;
	u8 mdev_detach:1;
	/* protect migration state */
	struct mutex state_mutex;
	enum vfio_device_mig_state mig_state;
	/* protect the reset_done flow */
	spinlock_t reset_lock;
	struct mlx5_vf_migration_file *resuming_migf;
	struct mlx5_vf_migration_file *saving_migf;
	struct notifier_block nb;
	struct mlx5_core_dev *mdev;
};

int mlx5vf_cmd_suspend_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod);
int mlx5vf_cmd_resume_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod);
int mlx5vf_cmd_query_vhca_migration_state(struct pci_dev *pdev, u16 vhca_id,
					  size_t *state_size);
int mlx5vf_cmd_get_vhca_id(struct pci_dev *pdev, u16 function_id, u16 *vhca_id);
void mlx5vf_cmd_set_migratable(struct mlx5vf_pci_core_device *mvdev);
void mlx5vf_cmd_remove_migratable(struct mlx5vf_pci_core_device *mvdev);
int mlx5vf_cmd_save_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       struct mlx5_vf_migration_file *migf);
int mlx5vf_cmd_load_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       struct mlx5_vf_migration_file *migf);
void mlx5vf_state_mutex_unlock(struct mlx5vf_pci_core_device *mvdev);
#endif /* MLX5_VFIO_CMD_H */
