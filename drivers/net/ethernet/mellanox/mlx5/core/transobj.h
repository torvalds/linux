/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __TRANSOBJ_H__
#define __TRANSOBJ_H__

int mlx5_create_rq(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *rqn);
int mlx5_modify_rq(struct mlx5_core_dev *dev, u32 rqn, u32 *in, int inlen);
void mlx5_destroy_rq(struct mlx5_core_dev *dev, u32 rqn);
int mlx5_create_sq(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *sqn);
int mlx5_modify_sq(struct mlx5_core_dev *dev, u32 sqn, u32 *in, int inlen);
void mlx5_destroy_sq(struct mlx5_core_dev *dev, u32 sqn);
int mlx5_create_tir(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *tirn);
void mlx5_destroy_tir(struct mlx5_core_dev *dev, u32 tirn);
int mlx5_create_tis(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *tisn);
void mlx5_destroy_tis(struct mlx5_core_dev *dev, u32 tisn);

#endif /* __TRANSOBJ_H__ */
