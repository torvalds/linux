/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MLX5_IB_CMD_H
#define MLX5_IB_CMD_H

#include "mlx5_ib.h"
#include <linux/kernel.h>
#include <linux/mlx5/driver.h>

int mlx5_cmd_dump_fill_mkey(struct mlx5_core_dev *dev, u32 *mkey);
int mlx5_cmd_null_mkey(struct mlx5_core_dev *dev, u32 *null_mkey);
int mlx5_cmd_query_cong_params(struct mlx5_core_dev *dev, int cong_point,
			       void *out, int out_size);
int mlx5_cmd_query_ext_ppcnt_counters(struct mlx5_core_dev *dev, void *out);
int mlx5_cmd_modify_cong_params(struct mlx5_core_dev *mdev,
				void *in, int in_size);
int mlx5_cmd_alloc_memic(struct mlx5_memic *memic, phys_addr_t *addr,
			 u64 length, u32 alignment);
int mlx5_cmd_dealloc_memic(struct mlx5_memic *memic, u64 addr, u64 length);
#endif /* MLX5_IB_CMD_H */
