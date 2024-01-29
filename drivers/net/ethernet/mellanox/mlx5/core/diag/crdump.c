// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies */

#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "lib/pci_vsc.h"
#include "lib/mlx5.h"

#define BAD_ACCESS			0xBADACCE5
#define MLX5_PROTECTED_CR_SCAN_CRSPACE	0x7

static bool mlx5_crdump_enabled(struct mlx5_core_dev *dev)
{
	return !!dev->priv.health.crdump_size;
}

static int mlx5_crdump_fill(struct mlx5_core_dev *dev, u32 *cr_data)
{
	u32 crdump_size = dev->priv.health.crdump_size;
	int i, ret;

	for (i = 0; i < (crdump_size / 4); i++)
		cr_data[i] = BAD_ACCESS;

	ret = mlx5_vsc_gw_read_block_fast(dev, cr_data, crdump_size);
	if (ret <= 0) {
		if (ret == 0)
			return -EIO;
		return ret;
	}

	if (crdump_size != ret) {
		mlx5_core_warn(dev, "failed to read full dump, read %d out of %u\n",
			       ret, crdump_size);
		return -EINVAL;
	}

	return 0;
}

int mlx5_crdump_collect(struct mlx5_core_dev *dev, u32 *cr_data)
{
	int ret;

	if (!mlx5_crdump_enabled(dev))
		return -ENODEV;

	ret = mlx5_vsc_gw_lock(dev);
	if (ret) {
		mlx5_core_warn(dev, "crdump: failed to lock vsc gw err %d\n",
			       ret);
		return ret;
	}
	/* Verify no other PF is running cr-dump or sw reset */
	ret = mlx5_vsc_sem_set_space(dev, MLX5_SEMAPHORE_SW_RESET,
				     MLX5_VSC_LOCK);
	if (ret) {
		if (ret == -EBUSY)
			mlx5_core_info(dev, "SW reset semaphore is already in use\n");
		else
			mlx5_core_warn(dev, "Failed to lock SW reset semaphore\n");
		goto unlock_gw;
	}

	ret = mlx5_vsc_gw_set_space(dev, MLX5_VSC_SPACE_SCAN_CRSPACE, NULL);
	if (ret)
		goto unlock_sem;

	ret = mlx5_crdump_fill(dev, cr_data);

unlock_sem:
	mlx5_vsc_sem_set_space(dev, MLX5_SEMAPHORE_SW_RESET, MLX5_VSC_UNLOCK);
unlock_gw:
	mlx5_vsc_gw_unlock(dev);
	return ret;
}

int mlx5_crdump_enable(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	u32 space_size;
	int ret;

	if (!mlx5_core_is_pf(dev) || !mlx5_vsc_accessible(dev) ||
	    mlx5_crdump_enabled(dev))
		return 0;

	ret = mlx5_vsc_gw_lock(dev);
	if (ret)
		return ret;

	/* Check if space is supported and get space size */
	ret = mlx5_vsc_gw_set_space(dev, MLX5_VSC_SPACE_SCAN_CRSPACE,
				    &space_size);
	if (ret) {
		/* Unlock and mask error since space is not supported */
		mlx5_vsc_gw_unlock(dev);
		return 0;
	}

	if (!space_size) {
		mlx5_core_warn(dev, "Invalid Crspace size, zero\n");
		mlx5_vsc_gw_unlock(dev);
		return -EINVAL;
	}

	ret = mlx5_vsc_gw_unlock(dev);
	if (ret)
		return ret;

	priv->health.crdump_size = space_size;
	return 0;
}

void mlx5_crdump_disable(struct mlx5_core_dev *dev)
{
	dev->priv.health.crdump_size = 0;
}
