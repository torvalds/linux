// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2018 Mellanox Technologies

#include <linux/hyperv.h>
#include "mlx5_core.h"
#include "lib/hv.h"

static int mlx5_hv_config_common(struct mlx5_core_dev *dev, void *buf, int len,
				 int offset, bool read)
{
	int rc = -EOPNOTSUPP;
	int bytes_returned;
	int block_id;

	if (offset % HV_CONFIG_BLOCK_SIZE_MAX || len != HV_CONFIG_BLOCK_SIZE_MAX)
		return -EINVAL;

	block_id = offset / HV_CONFIG_BLOCK_SIZE_MAX;

	rc = read ?
	     hyperv_read_cfg_blk(dev->pdev, buf,
				 HV_CONFIG_BLOCK_SIZE_MAX, block_id,
				 &bytes_returned) :
	     hyperv_write_cfg_blk(dev->pdev, buf,
				  HV_CONFIG_BLOCK_SIZE_MAX, block_id);

	/* Make sure len bytes were read successfully  */
	if (read && !rc && len != bytes_returned)
		rc = -EIO;

	if (rc) {
		mlx5_core_err(dev, "Failed to %s hv config, err = %d, len = %d, offset = %d\n",
			      read ? "read" : "write", rc, len,
			      offset);
		return rc;
	}

	return 0;
}

int mlx5_hv_read_config(struct mlx5_core_dev *dev, void *buf, int len,
			int offset)
{
	return mlx5_hv_config_common(dev, buf, len, offset, true);
}

int mlx5_hv_write_config(struct mlx5_core_dev *dev, void *buf, int len,
			 int offset)
{
	return mlx5_hv_config_common(dev, buf, len, offset, false);
}

int mlx5_hv_register_invalidate(struct mlx5_core_dev *dev, void *context,
				void (*block_invalidate)(void *context,
							 u64 block_mask))
{
	return hyperv_reg_block_invalidate(dev->pdev, context,
					   block_invalidate);
}

void mlx5_hv_unregister_invalidate(struct mlx5_core_dev *dev)
{
	hyperv_reg_block_invalidate(dev->pdev, NULL, NULL);
}
