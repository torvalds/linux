/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io-mapping.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include "mlx5_core.h"

enum {
	NUM_DRIVER_UARS		= 4,
	NUM_LOW_LAT_BFREGS	= 4,
};

int mlx5_cmd_alloc_uar(struct mlx5_core_dev *dev, u32 *uarn)
{
	u32 out[MLX5_ST_SZ_DW(alloc_uar_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(alloc_uar_in)]   = {0};
	int err;

	MLX5_SET(alloc_uar_in, in, opcode, MLX5_CMD_OP_ALLOC_UAR);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*uarn = MLX5_GET(alloc_uar_out, out, uar);
	return err;
}
EXPORT_SYMBOL(mlx5_cmd_alloc_uar);

int mlx5_cmd_free_uar(struct mlx5_core_dev *dev, u32 uarn)
{
	u32 out[MLX5_ST_SZ_DW(dealloc_uar_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(dealloc_uar_in)]   = {0};

	MLX5_SET(dealloc_uar_in, in, opcode, MLX5_CMD_OP_DEALLOC_UAR);
	MLX5_SET(dealloc_uar_in, in, uar, uarn);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_cmd_free_uar);

static int need_bfreg_lock(int bfregn)
{
	int tot_bfregs = NUM_DRIVER_UARS * MLX5_BFREGS_PER_UAR;

	if (bfregn == 0 || tot_bfregs - NUM_LOW_LAT_BFREGS)
		return 0;

	return 1;
}

int mlx5_alloc_bfregs(struct mlx5_core_dev *dev, struct mlx5_bfreg_info *bfregi)
{
	int tot_bfregs = NUM_DRIVER_UARS * MLX5_BFREGS_PER_UAR;
	struct mlx5_bf *bf;
	phys_addr_t addr;
	int err;
	int i;

	bfregi->num_uars = NUM_DRIVER_UARS;
	bfregi->num_low_latency_bfregs = NUM_LOW_LAT_BFREGS;

	mutex_init(&bfregi->lock);
	bfregi->uars = kcalloc(bfregi->num_uars, sizeof(*bfregi->uars), GFP_KERNEL);
	if (!bfregi->uars)
		return -ENOMEM;

	bfregi->bfs = kcalloc(tot_bfregs, sizeof(*bfregi->bfs), GFP_KERNEL);
	if (!bfregi->bfs) {
		err = -ENOMEM;
		goto out_uars;
	}

	bfregi->bitmap = kcalloc(BITS_TO_LONGS(tot_bfregs), sizeof(*bfregi->bitmap),
				GFP_KERNEL);
	if (!bfregi->bitmap) {
		err = -ENOMEM;
		goto out_bfs;
	}

	bfregi->count = kcalloc(tot_bfregs, sizeof(*bfregi->count), GFP_KERNEL);
	if (!bfregi->count) {
		err = -ENOMEM;
		goto out_bitmap;
	}

	for (i = 0; i < bfregi->num_uars; i++) {
		err = mlx5_cmd_alloc_uar(dev, &bfregi->uars[i].index);
		if (err)
			goto out_count;

		addr = dev->iseg_base + ((phys_addr_t)(bfregi->uars[i].index) << PAGE_SHIFT);
		bfregi->uars[i].map = ioremap(addr, PAGE_SIZE);
		if (!bfregi->uars[i].map) {
			mlx5_cmd_free_uar(dev, bfregi->uars[i].index);
			err = -ENOMEM;
			goto out_count;
		}
		mlx5_core_dbg(dev, "allocated uar index 0x%x, mmaped at %p\n",
			      bfregi->uars[i].index, bfregi->uars[i].map);
	}

	for (i = 0; i < tot_bfregs; i++) {
		bf = &bfregi->bfs[i];

		bf->buf_size = (1 << MLX5_CAP_GEN(dev, log_bf_reg_size)) / 2;
		bf->uar = &bfregi->uars[i / MLX5_BFREGS_PER_UAR];
		bf->regreg = bfregi->uars[i / MLX5_BFREGS_PER_UAR].map;
		bf->reg = NULL; /* Add WC support */
		bf->offset = (i % MLX5_BFREGS_PER_UAR) *
			     (1 << MLX5_CAP_GEN(dev, log_bf_reg_size)) +
			     MLX5_BF_OFFSET;
		bf->need_lock = need_bfreg_lock(i);
		spin_lock_init(&bf->lock);
		spin_lock_init(&bf->lock32);
		bf->bfregn = i;
	}

	return 0;

out_count:
	for (i--; i >= 0; i--) {
		iounmap(bfregi->uars[i].map);
		mlx5_cmd_free_uar(dev, bfregi->uars[i].index);
	}
	kfree(bfregi->count);

out_bitmap:
	kfree(bfregi->bitmap);

out_bfs:
	kfree(bfregi->bfs);

out_uars:
	kfree(bfregi->uars);
	return err;
}

int mlx5_free_bfregs(struct mlx5_core_dev *dev, struct mlx5_bfreg_info *bfregi)
{
	int i = bfregi->num_uars;

	for (i--; i >= 0; i--) {
		iounmap(bfregi->uars[i].map);
		mlx5_cmd_free_uar(dev, bfregi->uars[i].index);
	}

	kfree(bfregi->count);
	kfree(bfregi->bitmap);
	kfree(bfregi->bfs);
	kfree(bfregi->uars);

	return 0;
}

int mlx5_alloc_map_uar(struct mlx5_core_dev *mdev, struct mlx5_uar *uar,
		       bool map_wc)
{
	phys_addr_t pfn;
	phys_addr_t uar_bar_start;
	int err;

	err = mlx5_cmd_alloc_uar(mdev, &uar->index);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_cmd_alloc_uar() failed, %d\n", err);
		return err;
	}

	uar_bar_start = pci_resource_start(mdev->pdev, 0);
	pfn           = (uar_bar_start >> PAGE_SHIFT) + uar->index;

	if (map_wc) {
		uar->bf_map = ioremap_wc(pfn << PAGE_SHIFT, PAGE_SIZE);
		if (!uar->bf_map) {
			mlx5_core_warn(mdev, "ioremap_wc() failed\n");
			uar->map = ioremap(pfn << PAGE_SHIFT, PAGE_SIZE);
			if (!uar->map)
				goto err_free_uar;
		}
	} else {
		uar->map = ioremap(pfn << PAGE_SHIFT, PAGE_SIZE);
		if (!uar->map)
			goto err_free_uar;
	}

	return 0;

err_free_uar:
	mlx5_core_warn(mdev, "ioremap() failed\n");
	err = -ENOMEM;
	mlx5_cmd_free_uar(mdev, uar->index);

	return err;
}
EXPORT_SYMBOL(mlx5_alloc_map_uar);

void mlx5_unmap_free_uar(struct mlx5_core_dev *mdev, struct mlx5_uar *uar)
{
	if (uar->map)
		iounmap(uar->map);
	else
		iounmap(uar->bf_map);
	mlx5_cmd_free_uar(mdev, uar->index);
}
EXPORT_SYMBOL(mlx5_unmap_free_uar);
