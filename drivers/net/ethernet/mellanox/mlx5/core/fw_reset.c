// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved. */

#include "fw_reset.h"

static int mlx5_reg_mfrl_set(struct mlx5_core_dev *dev, u8 reset_level,
			     u8 reset_type_sel, u8 sync_resp, bool sync_start)
{
	u32 out[MLX5_ST_SZ_DW(mfrl_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(mfrl_reg)] = {};

	MLX5_SET(mfrl_reg, in, reset_level, reset_level);
	MLX5_SET(mfrl_reg, in, rst_type_sel, reset_type_sel);
	MLX5_SET(mfrl_reg, in, pci_sync_for_fw_update_resp, sync_resp);
	MLX5_SET(mfrl_reg, in, pci_sync_for_fw_update_start, sync_start);

	return mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out), MLX5_REG_MFRL, 0, 1);
}

static int mlx5_reg_mfrl_query(struct mlx5_core_dev *dev, u8 *reset_level, u8 *reset_type)
{
	u32 out[MLX5_ST_SZ_DW(mfrl_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(mfrl_reg)] = {};
	int err;

	err = mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out), MLX5_REG_MFRL, 0, 0);
	if (err)
		return err;

	if (reset_level)
		*reset_level = MLX5_GET(mfrl_reg, out, reset_level);
	if (reset_type)
		*reset_type = MLX5_GET(mfrl_reg, out, reset_type);

	return 0;
}

int mlx5_fw_reset_query(struct mlx5_core_dev *dev, u8 *reset_level, u8 *reset_type)
{
	return mlx5_reg_mfrl_query(dev, reset_level, reset_type);
}

int mlx5_fw_reset_set_reset_sync(struct mlx5_core_dev *dev, u8 reset_type_sel)
{
	return mlx5_reg_mfrl_set(dev, MLX5_MFRL_REG_RESET_LEVEL3, reset_type_sel, 0, true);
}

int mlx5_fw_reset_set_live_patch(struct mlx5_core_dev *dev)
{
	return mlx5_reg_mfrl_set(dev, MLX5_MFRL_REG_RESET_LEVEL0, 0, 0, false);
}
