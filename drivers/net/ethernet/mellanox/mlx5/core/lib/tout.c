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
	[MLX5_TO_CMD_MS] = 60000
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
