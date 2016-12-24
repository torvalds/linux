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
	NUM_LOW_LAT_UUARS	= 4,
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

static int need_uuar_lock(int uuarn)
{
	int tot_uuars = NUM_DRIVER_UARS * MLX5_BF_REGS_PER_PAGE;

	if (uuarn == 0 || tot_uuars - NUM_LOW_LAT_UUARS)
		return 0;

	return 1;
}

int mlx5_alloc_uuars(struct mlx5_core_dev *dev, struct mlx5_uuar_info *uuari)
{
	int tot_uuars = NUM_DRIVER_UARS * MLX5_BF_REGS_PER_PAGE;
	struct mlx5_bf *bf;
	phys_addr_t addr;
	int err;
	int i;

	uuari->num_uars = NUM_DRIVER_UARS;
	uuari->num_low_latency_uuars = NUM_LOW_LAT_UUARS;

	mutex_init(&uuari->lock);
	uuari->uars = kcalloc(uuari->num_uars, sizeof(*uuari->uars), GFP_KERNEL);
	if (!uuari->uars)
		return -ENOMEM;

	uuari->bfs = kcalloc(tot_uuars, sizeof(*uuari->bfs), GFP_KERNEL);
	if (!uuari->bfs) {
		err = -ENOMEM;
		goto out_uars;
	}

	uuari->bitmap = kcalloc(BITS_TO_LONGS(tot_uuars), sizeof(*uuari->bitmap),
				GFP_KERNEL);
	if (!uuari->bitmap) {
		err = -ENOMEM;
		goto out_bfs;
	}

	uuari->count = kcalloc(tot_uuars, sizeof(*uuari->count), GFP_KERNEL);
	if (!uuari->count) {
		err = -ENOMEM;
		goto out_bitmap;
	}

	for (i = 0; i < uuari->num_uars; i++) {
		err = mlx5_cmd_alloc_uar(dev, &uuari->uars[i].index);
		if (err)
			goto out_count;

		addr = dev->iseg_base + ((phys_addr_t)(uuari->uars[i].index) << PAGE_SHIFT);
		uuari->uars[i].map = ioremap(addr, PAGE_SIZE);
		if (!uuari->uars[i].map) {
			mlx5_cmd_free_uar(dev, uuari->uars[i].index);
			err = -ENOMEM;
			goto out_count;
		}
		mlx5_core_dbg(dev, "allocated uar index 0x%x, mmaped at %p\n",
			      uuari->uars[i].index, uuari->uars[i].map);
	}

	for (i = 0; i < tot_uuars; i++) {
		bf = &uuari->bfs[i];

		bf->buf_size = (1 << MLX5_CAP_GEN(dev, log_bf_reg_size)) / 2;
		bf->uar = &uuari->uars[i / MLX5_BF_REGS_PER_PAGE];
		bf->regreg = uuari->uars[i / MLX5_BF_REGS_PER_PAGE].map;
		bf->reg = NULL; /* Add WC support */
		bf->offset = (i % MLX5_BF_REGS_PER_PAGE) *
			     (1 << MLX5_CAP_GEN(dev, log_bf_reg_size)) +
			     MLX5_BF_OFFSET;
		bf->need_lock = need_uuar_lock(i);
		spin_lock_init(&bf->lock);
		spin_lock_init(&bf->lock32);
		bf->uuarn = i;
	}

	return 0;

out_count:
	for (i--; i >= 0; i--) {
		iounmap(uuari->uars[i].map);
		mlx5_cmd_free_uar(dev, uuari->uars[i].index);
	}
	kfree(uuari->count);

out_bitmap:
	kfree(uuari->bitmap);

out_bfs:
	kfree(uuari->bfs);

out_uars:
	kfree(uuari->uars);
	return err;
}

int mlx5_free_uuars(struct mlx5_core_dev *dev, struct mlx5_uuar_info *uuari)
{
	int i = uuari->num_uars;

	for (i--; i >= 0; i--) {
		iounmap(uuari->uars[i].map);
		mlx5_cmd_free_uar(dev, uuari->uars[i].index);
	}

	kfree(uuari->count);
	kfree(uuari->bitmap);
	kfree(uuari->bfs);
	kfree(uuari->uars);

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
