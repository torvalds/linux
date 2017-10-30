/*
 * Copyright (c) 2017, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __MLX5_FPGA_CORE_H__
#define __MLX5_FPGA_CORE_H__

#ifdef CONFIG_MLX5_FPGA

#include "fpga/cmd.h"

/* Represents an Innova device */
struct mlx5_fpga_device {
	struct mlx5_core_dev *mdev;
	spinlock_t state_lock; /* Protects state transitions */
	enum mlx5_fpga_status state;
	enum mlx5_fpga_image last_admin_image;
	enum mlx5_fpga_image last_oper_image;

	/* QP Connection resources */
	struct {
		u32 pdn;
		struct mlx5_core_mkey mkey;
		struct mlx5_uars_page *uar;
	} conn_res;

	struct mlx5_fpga_ipsec *ipsec;
};

#define mlx5_fpga_dbg(__adev, format, ...) \
	dev_dbg(&(__adev)->mdev->pdev->dev, "FPGA: %s:%d:(pid %d): " format, \
		 __func__, __LINE__, current->pid, ##__VA_ARGS__)

#define mlx5_fpga_err(__adev, format, ...) \
	dev_err(&(__adev)->mdev->pdev->dev, "FPGA: %s:%d:(pid %d): " format, \
		__func__, __LINE__, current->pid, ##__VA_ARGS__)

#define mlx5_fpga_warn(__adev, format, ...) \
	dev_warn(&(__adev)->mdev->pdev->dev, "FPGA: %s:%d:(pid %d): " format, \
		__func__, __LINE__, current->pid, ##__VA_ARGS__)

#define mlx5_fpga_warn_ratelimited(__adev, format, ...) \
	dev_warn_ratelimited(&(__adev)->mdev->pdev->dev, "FPGA: %s:%d: " \
		format, __func__, __LINE__, ##__VA_ARGS__)

#define mlx5_fpga_notice(__adev, format, ...) \
	dev_notice(&(__adev)->mdev->pdev->dev, "FPGA: " format, ##__VA_ARGS__)

#define mlx5_fpga_info(__adev, format, ...) \
	dev_info(&(__adev)->mdev->pdev->dev, "FPGA: " format, ##__VA_ARGS__)

int mlx5_fpga_init(struct mlx5_core_dev *mdev);
void mlx5_fpga_cleanup(struct mlx5_core_dev *mdev);
int mlx5_fpga_device_start(struct mlx5_core_dev *mdev);
void mlx5_fpga_device_stop(struct mlx5_core_dev *mdev);
void mlx5_fpga_event(struct mlx5_core_dev *mdev, u8 event, void *data);

#else

static inline int mlx5_fpga_init(struct mlx5_core_dev *mdev)
{
	return 0;
}

static inline void mlx5_fpga_cleanup(struct mlx5_core_dev *mdev)
{
}

static inline int mlx5_fpga_device_start(struct mlx5_core_dev *mdev)
{
	return 0;
}

static inline void mlx5_fpga_device_stop(struct mlx5_core_dev *mdev)
{
}

static inline void mlx5_fpga_event(struct mlx5_core_dev *mdev, u8 event,
				   void *data)
{
}

#endif

#endif /* __MLX5_FPGA_CORE_H__ */
