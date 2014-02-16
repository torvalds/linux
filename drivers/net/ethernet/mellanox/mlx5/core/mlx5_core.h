/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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

#ifndef __MLX5_CORE_H__
#define __MLX5_CORE_H__

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>

extern int mlx5_core_debug_mask;

#define mlx5_core_dbg(dev, format, arg...)				       \
pr_debug("%s:%s:%d:(pid %d): " format, (dev)->priv.name, __func__, __LINE__,   \
	 current->pid, ##arg)

#define mlx5_core_dbg_mask(dev, mask, format, arg...)			       \
do {									       \
	if ((mask) & mlx5_core_debug_mask)				       \
		pr_debug("%s:%s:%d:(pid %d): " format, (dev)->priv.name,       \
			 __func__, __LINE__, current->pid, ##arg);	       \
} while (0)

#define mlx5_core_err(dev, format, arg...) \
pr_err("%s:%s:%d:(pid %d): " format, (dev)->priv.name, __func__, __LINE__,     \
	current->pid, ##arg)

#define mlx5_core_warn(dev, format, arg...) \
pr_warn("%s:%s:%d:(pid %d): " format, (dev)->priv.name, __func__, __LINE__,    \
	current->pid, ##arg)

enum {
	MLX5_CMD_DATA, /* print command payload only */
	MLX5_CMD_TIME, /* print command execution time */
};


int mlx5_cmd_query_hca_cap(struct mlx5_core_dev *dev,
			   struct mlx5_caps *caps);
int mlx5_cmd_query_adapter(struct mlx5_core_dev *dev);
int mlx5_cmd_init_hca(struct mlx5_core_dev *dev);
int mlx5_cmd_teardown_hca(struct mlx5_core_dev *dev);

#endif /* __MLX5_CORE_H__ */
