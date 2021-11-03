// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/mlx5/driver.h>
#include "lib/tout.h"

struct mlx5_timeouts {
	u64 to[MAX_TIMEOUT_TYPES];
};

static const u32 tout_def_sw_val[MAX_TIMEOUT_TYPES] = {
	[MLX5_TO_FW_PRE_INIT_TIMEOUT_MS] = 120000,
	[MLX5_TO_FW_PRE_INIT_WARN_MESSAGE_INTERVAL_MS] = 20000,
	[MLX5_TO_FW_PRE_INIT_WAIT_MS] = 2,
	[MLX5_TO_FW_INIT_MS] = 2000,
	[MLX5_TO_CMD_MS] = 60000,
	[MLX5_TO_PCI_TOGGLE_MS] =  2000,
	[MLX5_TO_HEALTH_POLL_INTERVAL_MS] =  2000,
	[MLX5_TO_FULL_CRDUMP_MS] = 60000,
	[MLX5_TO_FW_RESET_MS] = 60000,
	[MLX5_TO_FLUSH_ON_ERROR_MS] = 2000,
	[MLX5_TO_PCI_SYNC_UPDATE_MS] = 5000,
	[MLX5_TO_TEARDOWN_MS] = 3000,
	[MLX5_TO_FSM_REACTIVATE_MS] = 5000,
	[MLX5_TO_RECLAIM_PAGES_MS] = 5000,
	[MLX5_TO_RECLAIM_VFS_PAGES_MS] = 120000
};

static void tout_set(struct mlx5_core_dev *dev, u64 val, enum mlx5_timeouts_types type)
{
	dev->timeouts->to[type] = val;
}

static void tout_set_def_val(struct mlx5_core_dev *dev)
{
	int i;

	for (i = MLX5_TO_FW_PRE_INIT_TIMEOUT_MS; i < MAX_TIMEOUT_TYPES; i++)
		tout_set(dev, tout_def_sw_val[i], i);
}

int mlx5_tout_init(struct mlx5_core_dev *dev)
{
	dev->timeouts = kmalloc(sizeof(*dev->timeouts), GFP_KERNEL);
	if (!dev->timeouts)
		return -ENOMEM;

	tout_set_def_val(dev);
	return 0;
}

void mlx5_tout_cleanup(struct mlx5_core_dev *dev)
{
	kfree(dev->timeouts);
}

/* Time register consists of two fields to_multiplier(time out multiplier)
 * and to_value(time out value). to_value is the quantity of the time units and
 * to_multiplier is the type and should be one off these four values.
 * 0x0: millisecond
 * 0x1: seconds
 * 0x2: minutes
 * 0x3: hours
 * this function converts the time stored in the two register fields into
 * millisecond.
 */
static u64 tout_convert_reg_field_to_ms(u32 to_mul, u32 to_val)
{
	u64 msec = to_val;

	to_mul &= 0x3;
	/* convert hours/minutes/seconds to miliseconds */
	if (to_mul)
		msec *= 1000 * int_pow(60, to_mul - 1);

	return msec;
}

static u64 tout_convert_iseg_to_ms(u32 iseg_to)
{
	return tout_convert_reg_field_to_ms(iseg_to >> 29, iseg_to & 0xfffff);
}

static bool tout_is_supported(struct mlx5_core_dev *dev)
{
	return !!ioread32be(&dev->iseg->cmd_q_init_to);
}

void mlx5_tout_query_iseg(struct mlx5_core_dev *dev)
{
	u32 to;

	if (!tout_is_supported(dev))
		return;

	to = ioread32be(&dev->iseg->cmd_q_init_to);
	tout_set(dev, tout_convert_iseg_to_ms(to), MLX5_TO_FW_INIT_MS);

	to = ioread32be(&dev->iseg->cmd_exec_to);
	tout_set(dev, tout_convert_iseg_to_ms(to), MLX5_TO_CMD_MS);
}

u64 _mlx5_tout_ms(struct mlx5_core_dev *dev, enum mlx5_timeouts_types type)
{
	return dev->timeouts->to[type];
}

#define MLX5_TIMEOUT_QUERY(fld, reg_out) \
	({ \
	struct mlx5_ifc_default_timeout_bits *time_field; \
	u32 to_multi, to_value; \
	u64 to_val_ms; \
	\
	time_field = MLX5_ADDR_OF(dtor_reg, reg_out, fld); \
	to_multi = MLX5_GET(default_timeout, time_field, to_multiplier); \
	to_value = MLX5_GET(default_timeout, time_field, to_value); \
	to_val_ms = tout_convert_reg_field_to_ms(to_multi, to_value); \
	to_val_ms; \
	})

#define MLX5_TIMEOUT_FILL(fld, reg_out, dev, to_type, to_extra) \
	({ \
	u64 fw_to = MLX5_TIMEOUT_QUERY(fld, reg_out); \
	tout_set(dev, fw_to + (to_extra), to_type); \
	fw_to; \
	})

static int tout_query_dtor(struct mlx5_core_dev *dev)
{
	u64 pcie_toggle_to_val, tear_down_to_val;
	u32 out[MLX5_ST_SZ_DW(dtor_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(dtor_reg)] = {};
	int err;

	err = mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out), MLX5_REG_DTOR, 0, 0);
	if (err)
		return err;

	pcie_toggle_to_val = MLX5_TIMEOUT_FILL(pcie_toggle_to, out, dev, MLX5_TO_PCI_TOGGLE_MS, 0);
	MLX5_TIMEOUT_FILL(fw_reset_to, out, dev, MLX5_TO_FW_RESET_MS, pcie_toggle_to_val);

	tear_down_to_val = MLX5_TIMEOUT_FILL(tear_down_to, out, dev, MLX5_TO_TEARDOWN_MS, 0);
	MLX5_TIMEOUT_FILL(pci_sync_update_to, out, dev, MLX5_TO_PCI_SYNC_UPDATE_MS,
			  tear_down_to_val);

	MLX5_TIMEOUT_FILL(health_poll_to, out, dev, MLX5_TO_HEALTH_POLL_INTERVAL_MS, 0);
	MLX5_TIMEOUT_FILL(full_crdump_to, out, dev, MLX5_TO_FULL_CRDUMP_MS, 0);
	MLX5_TIMEOUT_FILL(flush_on_err_to, out, dev, MLX5_TO_FLUSH_ON_ERROR_MS, 0);
	MLX5_TIMEOUT_FILL(fsm_reactivate_to, out, dev, MLX5_TO_FSM_REACTIVATE_MS, 0);
	MLX5_TIMEOUT_FILL(reclaim_pages_to, out, dev, MLX5_TO_RECLAIM_PAGES_MS, 0);
	MLX5_TIMEOUT_FILL(reclaim_vfs_pages_to, out, dev, MLX5_TO_RECLAIM_VFS_PAGES_MS, 0);

	return 0;
}

int mlx5_tout_query_dtor(struct mlx5_core_dev *dev)
{
	if (tout_is_supported(dev))
		return tout_query_dtor(dev);

	return 0;
}
