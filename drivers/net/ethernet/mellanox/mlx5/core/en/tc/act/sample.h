/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_TC_ACT_SAMPLE_H__
#define __MLX5_EN_TC_ACT_SAMPLE_H__

#include <net/flow_offload.h>
#include "en/tc_priv.h"

bool
mlx5e_tc_act_sample_is_multi_table(struct mlx5_core_dev *mdev,
				   struct mlx5_flow_attr *attr);

#endif /* __MLX5_EN_TC_ACT_SAMPLE_H__ */
