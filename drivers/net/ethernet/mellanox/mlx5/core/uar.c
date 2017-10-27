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

static int uars_per_sys_page(struct mlx5_core_dev *mdev)
{
	if (MLX5_CAP_GEN(mdev, uar_4k))
		return MLX5_CAP_GEN(mdev, num_of_uars_per_page);

	return 1;
}

static u64 uar2pfn(struct mlx5_core_dev *mdev, u32 index)
{
	u32 system_page_index;

	if (MLX5_CAP_GEN(mdev, uar_4k))
		system_page_index = index >> (PAGE_SHIFT - MLX5_ADAPTER_PAGE_SHIFT);
	else
		system_page_index = index;

	return (pci_resource_start(mdev->pdev, 0) >> PAGE_SHIFT) + system_page_index;
}

static void up_rel_func(struct kref *kref)
{
	struct mlx5_uars_page *up = container_of(kref, struct mlx5_uars_page, ref_count);

	list_del(&up->list);
	iounmap(up->map);
	if (mlx5_cmd_free_uar(up->mdev, up->index))
		mlx5_core_warn(up->mdev, "failed to free uar index %d\n", up->index);
	kfree(up->reg_bitmap);
	kfree(up->fp_bitmap);
	kfree(up);
}

static struct mlx5_uars_page *alloc_uars_page(struct mlx5_core_dev *mdev,
					      bool map_wc)
{
	struct mlx5_uars_page *up;
	int err = -ENOMEM;
	phys_addr_t pfn;
	int bfregs;
	int i;

	bfregs = uars_per_sys_page(mdev) * MLX5_BFREGS_PER_UAR;
	up = kzalloc(sizeof(*up), GFP_KERNEL);
	if (!up)
		return ERR_PTR(err);

	up->mdev = mdev;
	up->reg_bitmap = kcalloc(BITS_TO_LONGS(bfregs), sizeof(unsigned long), GFP_KERNEL);
	if (!up->reg_bitmap)
		goto error1;

	up->fp_bitmap = kcalloc(BITS_TO_LONGS(bfregs), sizeof(unsigned long), GFP_KERNEL);
	if (!up->fp_bitmap)
		goto error1;

	for (i = 0; i < bfregs; i++)
		if ((i % MLX5_BFREGS_PER_UAR) < MLX5_NON_FP_BFREGS_PER_UAR)
			set_bit(i, up->reg_bitmap);
		else
			set_bit(i, up->fp_bitmap);

	up->bfregs = bfregs;
	up->fp_avail = bfregs * MLX5_FP_BFREGS_PER_UAR / MLX5_BFREGS_PER_UAR;
	up->reg_avail = bfregs * MLX5_NON_FP_BFREGS_PER_UAR / MLX5_BFREGS_PER_UAR;

	err = mlx5_cmd_alloc_uar(mdev, &up->index);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_cmd_alloc_uar() failed, %d\n", err);
		goto error1;
	}

	pfn = uar2pfn(mdev, up->index);
	if (map_wc) {
		up->map = ioremap_wc(pfn << PAGE_SHIFT, PAGE_SIZE);
		if (!up->map) {
			err = -EAGAIN;
			goto error2;
		}
	} else {
		up->map = ioremap(pfn << PAGE_SHIFT, PAGE_SIZE);
		if (!up->map) {
			err = -ENOMEM;
			goto error2;
		}
	}
	kref_init(&up->ref_count);
	mlx5_core_dbg(mdev, "allocated UAR page: index %d, total bfregs %d\n",
		      up->index, up->bfregs);
	return up;

error2:
	if (mlx5_cmd_free_uar(mdev, up->index))
		mlx5_core_warn(mdev, "failed to free uar index %d\n", up->index);
error1:
	kfree(up->fp_bitmap);
	kfree(up->reg_bitmap);
	kfree(up);
	return ERR_PTR(err);
}

struct mlx5_uars_page *mlx5_get_uars_page(struct mlx5_core_dev *mdev)
{
	struct mlx5_uars_page *ret;

	mutex_lock(&mdev->priv.bfregs.reg_head.lock);
	if (list_empty(&mdev->priv.bfregs.reg_head.list)) {
		ret = alloc_uars_page(mdev, false);
		if (IS_ERR(ret)) {
			ret = NULL;
			goto out;
		}
		list_add(&ret->list, &mdev->priv.bfregs.reg_head.list);
	} else {
		ret = list_first_entry(&mdev->priv.bfregs.reg_head.list,
				       struct mlx5_uars_page, list);
		kref_get(&ret->ref_count);
	}
out:
	mutex_unlock(&mdev->priv.bfregs.reg_head.lock);

	return ret;
}
EXPORT_SYMBOL(mlx5_get_uars_page);

void mlx5_put_uars_page(struct mlx5_core_dev *mdev, struct mlx5_uars_page *up)
{
	mutex_lock(&mdev->priv.bfregs.reg_head.lock);
	kref_put(&up->ref_count, up_rel_func);
	mutex_unlock(&mdev->priv.bfregs.reg_head.lock);
}
EXPORT_SYMBOL(mlx5_put_uars_page);

static unsigned long map_offset(struct mlx5_core_dev *mdev, int dbi)
{
	/* return the offset in bytes from the start of the page to the
	 * blue flame area of the UAR
	 */
	return dbi / MLX5_BFREGS_PER_UAR * MLX5_ADAPTER_PAGE_SIZE +
	       (dbi % MLX5_BFREGS_PER_UAR) *
	       (1 << MLX5_CAP_GEN(mdev, log_bf_reg_size)) + MLX5_BF_OFFSET;
}

static int alloc_bfreg(struct mlx5_core_dev *mdev, struct mlx5_sq_bfreg *bfreg,
		       bool map_wc, bool fast_path)
{
	struct mlx5_bfreg_data *bfregs;
	struct mlx5_uars_page *up;
	struct list_head *head;
	unsigned long *bitmap;
	unsigned int *avail;
	struct mutex *lock;  /* pointer to right mutex */
	int dbi;

	bfregs = &mdev->priv.bfregs;
	if (map_wc) {
		head = &bfregs->wc_head.list;
		lock = &bfregs->wc_head.lock;
	} else {
		head = &bfregs->reg_head.list;
		lock = &bfregs->reg_head.lock;
	}
	mutex_lock(lock);
	if (list_empty(head)) {
		up = alloc_uars_page(mdev, map_wc);
		if (IS_ERR(up)) {
			mutex_unlock(lock);
			return PTR_ERR(up);
		}
		list_add(&up->list, head);
	} else {
		up = list_entry(head->next, struct mlx5_uars_page, list);
		kref_get(&up->ref_count);
	}
	if (fast_path) {
		bitmap = up->fp_bitmap;
		avail = &up->fp_avail;
	} else {
		bitmap = up->reg_bitmap;
		avail = &up->reg_avail;
	}
	dbi = find_first_bit(bitmap, up->bfregs);
	clear_bit(dbi, bitmap);
	(*avail)--;
	if (!(*avail))
		list_del(&up->list);

	bfreg->map = up->map + map_offset(mdev, dbi);
	bfreg->up = up;
	bfreg->wc = map_wc;
	bfreg->index = up->index + dbi / MLX5_BFREGS_PER_UAR;
	mutex_unlock(lock);

	return 0;
}

int mlx5_alloc_bfreg(struct mlx5_core_dev *mdev, struct mlx5_sq_bfreg *bfreg,
		     bool map_wc, bool fast_path)
{
	int err;

	err = alloc_bfreg(mdev, bfreg, map_wc, fast_path);
	if (!err)
		return 0;

	if (err == -EAGAIN && map_wc)
		return alloc_bfreg(mdev, bfreg, false, fast_path);

	return err;
}
EXPORT_SYMBOL(mlx5_alloc_bfreg);

static unsigned int addr_to_dbi_in_syspage(struct mlx5_core_dev *dev,
					   struct mlx5_uars_page *up,
					   struct mlx5_sq_bfreg *bfreg)
{
	unsigned int uar_idx;
	unsigned int bfreg_idx;
	unsigned int bf_reg_size;

	bf_reg_size = 1 << MLX5_CAP_GEN(dev, log_bf_reg_size);

	uar_idx = (bfreg->map - up->map) >> MLX5_ADAPTER_PAGE_SHIFT;
	bfreg_idx = (((uintptr_t)bfreg->map % MLX5_ADAPTER_PAGE_SIZE) - MLX5_BF_OFFSET) / bf_reg_size;

	return uar_idx * MLX5_BFREGS_PER_UAR + bfreg_idx;
}

void mlx5_free_bfreg(struct mlx5_core_dev *mdev, struct mlx5_sq_bfreg *bfreg)
{
	struct mlx5_bfreg_data *bfregs;
	struct mlx5_uars_page *up;
	struct mutex *lock; /* pointer to right mutex */
	unsigned int dbi;
	bool fp;
	unsigned int *avail;
	unsigned long *bitmap;
	struct list_head *head;

	bfregs = &mdev->priv.bfregs;
	if (bfreg->wc) {
		head = &bfregs->wc_head.list;
		lock = &bfregs->wc_head.lock;
	} else {
		head = &bfregs->reg_head.list;
		lock = &bfregs->reg_head.lock;
	}
	up = bfreg->up;
	dbi = addr_to_dbi_in_syspage(mdev, up, bfreg);
	fp = (dbi % MLX5_BFREGS_PER_UAR) >= MLX5_NON_FP_BFREGS_PER_UAR;
	if (fp) {
		avail = &up->fp_avail;
		bitmap = up->fp_bitmap;
	} else {
		avail = &up->reg_avail;
		bitmap = up->reg_bitmap;
	}
	mutex_lock(lock);
	(*avail)++;
	set_bit(dbi, bitmap);
	if (*avail == 1)
		list_add_tail(&up->list, head);

	kref_put(&up->ref_count, up_rel_func);
	mutex_unlock(lock);
}
EXPORT_SYMBOL(mlx5_free_bfreg);
