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

#include "cmd.h"

int mlx5_cmd_dump_fill_mkey(struct mlx5_core_dev *dev, u32 *mkey)
{
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)]   = {0};
	int err;

	MLX5_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*mkey = MLX5_GET(query_special_contexts_out, out,
				 dump_fill_mkey);
	return err;
}

int mlx5_cmd_null_mkey(struct mlx5_core_dev *dev, u32 *null_mkey)
{
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)]   = {};
	int err;

	MLX5_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*null_mkey = MLX5_GET(query_special_contexts_out, out,
				      null_mkey);
	return err;
}

int mlx5_cmd_query_cong_params(struct mlx5_core_dev *dev, int cong_point,
			       void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_params_in)] = { };

	MLX5_SET(query_cong_params_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_PARAMS);
	MLX5_SET(query_cong_params_in, in, cong_protocol, cong_point);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, out_size);
}

int mlx5_cmd_modify_cong_params(struct mlx5_core_dev *dev,
				void *in, int in_size)
{
	u32 out[MLX5_ST_SZ_DW(modify_cong_params_out)] = { };

	return mlx5_cmd_exec(dev, in, in_size, out, sizeof(out));
}

int mlx5_cmd_alloc_memic(struct mlx5_memic *memic, phys_addr_t *addr,
			  u64 length, u32 alignment)
{
	struct mlx5_core_dev *dev = memic->dev;
	u64 num_memic_hw_pages = MLX5_CAP_DEV_MEM(dev, memic_bar_size)
					>> PAGE_SHIFT;
	u64 hw_start_addr = MLX5_CAP64_DEV_MEM(dev, memic_bar_start_addr);
	u32 max_alignment = MLX5_CAP_DEV_MEM(dev, log_max_memic_addr_alignment);
	u32 num_pages = DIV_ROUND_UP(length, PAGE_SIZE);
	u32 out[MLX5_ST_SZ_DW(alloc_memic_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_memic_in)] = {};
	u32 mlx5_alignment;
	u64 page_idx = 0;
	int ret = 0;

	if (!length || (length & MLX5_MEMIC_ALLOC_SIZE_MASK))
		return -EINVAL;

	/* mlx5 device sets alignment as 64*2^driver_value
	 * so normalizing is needed.
	 */
	mlx5_alignment = (alignment < MLX5_MEMIC_BASE_ALIGN) ? 0 :
			 alignment - MLX5_MEMIC_BASE_ALIGN;
	if (mlx5_alignment > max_alignment)
		return -EINVAL;

	MLX5_SET(alloc_memic_in, in, opcode, MLX5_CMD_OP_ALLOC_MEMIC);
	MLX5_SET(alloc_memic_in, in, range_size, num_pages * PAGE_SIZE);
	MLX5_SET(alloc_memic_in, in, memic_size, length);
	MLX5_SET(alloc_memic_in, in, log_memic_addr_alignment,
		 mlx5_alignment);

	while (page_idx < num_memic_hw_pages) {
		spin_lock(&memic->memic_lock);
		page_idx = bitmap_find_next_zero_area(memic->memic_alloc_pages,
						      num_memic_hw_pages,
						      page_idx,
						      num_pages, 0);

		if (page_idx < num_memic_hw_pages)
			bitmap_set(memic->memic_alloc_pages,
				   page_idx, num_pages);

		spin_unlock(&memic->memic_lock);

		if (page_idx >= num_memic_hw_pages)
			break;

		MLX5_SET64(alloc_memic_in, in, range_start_addr,
			   hw_start_addr + (page_idx * PAGE_SIZE));

		ret = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
		if (ret) {
			spin_lock(&memic->memic_lock);
			bitmap_clear(memic->memic_alloc_pages,
				     page_idx, num_pages);
			spin_unlock(&memic->memic_lock);

			if (ret == -EAGAIN) {
				page_idx++;
				continue;
			}

			return ret;
		}

		*addr = pci_resource_start(dev->pdev, 0) +
			MLX5_GET64(alloc_memic_out, out, memic_start_addr);

		return 0;
	}

	return -ENOMEM;
}

int mlx5_cmd_dealloc_memic(struct mlx5_memic *memic, u64 addr, u64 length)
{
	struct mlx5_core_dev *dev = memic->dev;
	u64 hw_start_addr = MLX5_CAP64_DEV_MEM(dev, memic_bar_start_addr);
	u32 num_pages = DIV_ROUND_UP(length, PAGE_SIZE);
	u32 out[MLX5_ST_SZ_DW(dealloc_memic_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(dealloc_memic_in)] = {0};
	u64 start_page_idx;
	int err;

	addr -= pci_resource_start(dev->pdev, 0);
	start_page_idx = (addr - hw_start_addr) >> PAGE_SHIFT;

	MLX5_SET(dealloc_memic_in, in, opcode, MLX5_CMD_OP_DEALLOC_MEMIC);
	MLX5_SET64(dealloc_memic_in, in, memic_start_addr, addr);
	MLX5_SET(dealloc_memic_in, in, memic_size, length);

	err =  mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	if (!err) {
		spin_lock(&memic->memic_lock);
		bitmap_clear(memic->memic_alloc_pages,
			     start_page_idx, num_pages);
		spin_unlock(&memic->memic_lock);
	}

	return err;
}

int mlx5_cmd_query_ext_ppcnt_counters(struct mlx5_core_dev *dev, void *out)
{
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);

	MLX5_SET(ppcnt_reg, in, local_port, 1);

	MLX5_SET(ppcnt_reg, in, grp, MLX5_ETHERNET_EXTENDED_COUNTERS_GROUP);
	return  mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PPCNT,
				     0, 0);
}
