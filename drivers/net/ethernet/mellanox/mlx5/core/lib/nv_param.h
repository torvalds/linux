/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_NV_PARAM_H
#define __MLX5_NV_PARAM_H

#include <linux/mlx5/driver.h>
#include "devlink.h"

int mlx5_nv_param_register_dl_params(struct devlink *devlink);
void mlx5_nv_param_unregister_dl_params(struct devlink *devlink);

#endif

