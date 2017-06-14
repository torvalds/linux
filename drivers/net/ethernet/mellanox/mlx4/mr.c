/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>

#include <linux/mlx4/cmd.h>

#include "mlx4.h"
#include "icm.h"

static u32 mlx4_buddy_alloc(struct mlx4_buddy *buddy, int order)
{
	int o;
	int m;
	u32 seg;

	spin_lock(&buddy->lock);

	for (o = order; o <= buddy->max_order; ++o)
		if (buddy->num_free[o]) {
			m = 1 << (buddy->max_order - o);
			seg = find_first_bit(buddy->bits[o], m);
			if (seg < m)
				goto found;
		}

	spin_unlock(&buddy->lock);
	return -1;

 found:
	clear_bit(seg, buddy->bits[o]);
	--buddy->num_free[o];

	while (o > order) {
		--o;
		seg <<= 1;
		set_bit(seg ^ 1, buddy->bits[o]);
		++buddy->num_free[o];
	}

	spin_unlock(&buddy->lock);

	seg <<= order;

	return seg;
}

static void mlx4_buddy_free(struct mlx4_buddy *buddy, u32 seg, int order)
{
	seg >>= order;

	spin_lock(&buddy->lock);

	while (test_bit(seg ^ 1, buddy->bits[order])) {
		clear_bit(seg ^ 1, buddy->bits[order]);
		--buddy->num_free[order];
		seg >>= 1;
		++order;
	}

	set_bit(seg, buddy->bits[order]);
	++buddy->num_free[order];

	spin_unlock(&buddy->lock);
}

static int mlx4_buddy_init(struct mlx4_buddy *buddy, int max_order)
{
	int i, s;

	buddy->max_order = max_order;
	spin_lock_init(&buddy->lock);

	buddy->bits = kcalloc(buddy->max_order + 1, sizeof (long *),
			      GFP_KERNEL);
	buddy->num_free = kcalloc((buddy->max_order + 1), sizeof *buddy->num_free,
				  GFP_KERNEL);
	if (!buddy->bits || !buddy->num_free)
		goto err_out;

	for (i = 0; i <= buddy->max_order; ++i) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->bits[i] = kvmalloc_array(s, sizeof(long), GFP_KERNEL | __GFP_ZERO);
		if (!buddy->bits[i])
			goto err_out_free;
	}

	set_bit(0, buddy->bits[buddy->max_order]);
	buddy->num_free[buddy->max_order] = 1;

	return 0;

err_out_free:
	for (i = 0; i <= buddy->max_order; ++i)
		kvfree(buddy->bits[i]);

err_out:
	kfree(buddy->bits);
	kfree(buddy->num_free);

	return -ENOMEM;
}

static void mlx4_buddy_cleanup(struct mlx4_buddy *buddy)
{
	int i;

	for (i = 0; i <= buddy->max_order; ++i)
		kvfree(buddy->bits[i]);

	kfree(buddy->bits);
	kfree(buddy->num_free);
}

u32 __mlx4_alloc_mtt_range(struct mlx4_dev *dev, int order)
{
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;
	u32 seg;
	int seg_order;
	u32 offset;

	seg_order = max_t(int, order - log_mtts_per_seg, 0);

	seg = mlx4_buddy_alloc(&mr_table->mtt_buddy, seg_order);
	if (seg == -1)
		return -1;

	offset = seg * (1 << log_mtts_per_seg);

	if (mlx4_table_get_range(dev, &mr_table->mtt_table, offset,
				 offset + (1 << order) - 1)) {
		mlx4_buddy_free(&mr_table->mtt_buddy, seg, seg_order);
		return -1;
	}

	return offset;
}

static u32 mlx4_alloc_mtt_range(struct mlx4_dev *dev, int order)
{
	u64 in_param = 0;
	u64 out_param;
	int err;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, order);
		err = mlx4_cmd_imm(dev, in_param, &out_param, RES_MTT,
						       RES_OP_RESERVE_AND_MAP,
						       MLX4_CMD_ALLOC_RES,
						       MLX4_CMD_TIME_CLASS_A,
						       MLX4_CMD_WRAPPED);
		if (err)
			return -1;
		return get_param_l(&out_param);
	}
	return __mlx4_alloc_mtt_range(dev, order);
}

int mlx4_mtt_init(struct mlx4_dev *dev, int npages, int page_shift,
		  struct mlx4_mtt *mtt)
{
	int i;

	if (!npages) {
		mtt->order      = -1;
		mtt->page_shift = MLX4_ICM_PAGE_SHIFT;
		return 0;
	} else
		mtt->page_shift = page_shift;

	for (mtt->order = 0, i = 1; i < npages; i <<= 1)
		++mtt->order;

	mtt->offset = mlx4_alloc_mtt_range(dev, mtt->order);
	if (mtt->offset == -1)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_mtt_init);

void __mlx4_free_mtt_range(struct mlx4_dev *dev, u32 offset, int order)
{
	u32 first_seg;
	int seg_order;
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;

	seg_order = max_t(int, order - log_mtts_per_seg, 0);
	first_seg = offset / (1 << log_mtts_per_seg);

	mlx4_buddy_free(&mr_table->mtt_buddy, first_seg, seg_order);
	mlx4_table_put_range(dev, &mr_table->mtt_table, offset,
			     offset + (1 << order) - 1);
}

static void mlx4_free_mtt_range(struct mlx4_dev *dev, u32 offset, int order)
{
	u64 in_param = 0;
	int err;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, offset);
		set_param_h(&in_param, order);
		err = mlx4_cmd(dev, in_param, RES_MTT, RES_OP_RESERVE_AND_MAP,
						       MLX4_CMD_FREE_RES,
						       MLX4_CMD_TIME_CLASS_A,
						       MLX4_CMD_WRAPPED);
		if (err)
			mlx4_warn(dev, "Failed to free mtt range at:%d order:%d\n",
				  offset, order);
		return;
	}
	__mlx4_free_mtt_range(dev, offset, order);
}

void mlx4_mtt_cleanup(struct mlx4_dev *dev, struct mlx4_mtt *mtt)
{
	if (mtt->order < 0)
		return;

	mlx4_free_mtt_range(dev, mtt->offset, mtt->order);
}
EXPORT_SYMBOL_GPL(mlx4_mtt_cleanup);

u64 mlx4_mtt_addr(struct mlx4_dev *dev, struct mlx4_mtt *mtt)
{
	return (u64) mtt->offset * dev->caps.mtt_entry_sz;
}
EXPORT_SYMBOL_GPL(mlx4_mtt_addr);

static u32 hw_index_to_key(u32 ind)
{
	return (ind >> 24) | (ind << 8);
}

static u32 key_to_hw_index(u32 key)
{
	return (key << 24) | (key >> 8);
}

static int mlx4_SW2HW_MPT(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int mpt_index)
{
	return mlx4_cmd(dev, mailbox->dma, mpt_index,
			0, MLX4_CMD_SW2HW_MPT, MLX4_CMD_TIME_CLASS_B,
			MLX4_CMD_WRAPPED);
}

static int mlx4_HW2SW_MPT(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  int mpt_index)
{
	return mlx4_cmd_box(dev, 0, mailbox ? mailbox->dma : 0, mpt_index,
			    !mailbox, MLX4_CMD_HW2SW_MPT,
			    MLX4_CMD_TIME_CLASS_B, MLX4_CMD_WRAPPED);
}

/* Must protect against concurrent access */
int mlx4_mr_hw_get_mpt(struct mlx4_dev *dev, struct mlx4_mr *mmr,
		       struct mlx4_mpt_entry ***mpt_entry)
{
	int err;
	int key = key_to_hw_index(mmr->key) & (dev->caps.num_mpts - 1);
	struct mlx4_cmd_mailbox *mailbox = NULL;

	if (mmr->enabled != MLX4_MPT_EN_HW)
		return -EINVAL;

	err = mlx4_HW2SW_MPT(dev, NULL, key);
	if (err) {
		mlx4_warn(dev, "HW2SW_MPT failed (%d).", err);
		mlx4_warn(dev, "Most likely the MR has MWs bound to it.\n");
		return err;
	}

	mmr->enabled = MLX4_MPT_EN_SW;

	if (!mlx4_is_mfunc(dev)) {
		**mpt_entry = mlx4_table_find(
				&mlx4_priv(dev)->mr_table.dmpt_table,
				key, NULL);
	} else {
		mailbox = mlx4_alloc_cmd_mailbox(dev);
		if (IS_ERR(mailbox))
			return PTR_ERR(mailbox);

		err = mlx4_cmd_box(dev, 0, mailbox->dma, key,
				   0, MLX4_CMD_QUERY_MPT,
				   MLX4_CMD_TIME_CLASS_B,
				   MLX4_CMD_WRAPPED);
		if (err)
			goto free_mailbox;

		*mpt_entry = (struct mlx4_mpt_entry **)&mailbox->buf;
	}

	if (!(*mpt_entry) || !(**mpt_entry)) {
		err = -ENOMEM;
		goto free_mailbox;
	}

	return 0;

free_mailbox:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_mr_hw_get_mpt);

int mlx4_mr_hw_write_mpt(struct mlx4_dev *dev, struct mlx4_mr *mmr,
			 struct mlx4_mpt_entry **mpt_entry)
{
	int err;

	if (!mlx4_is_mfunc(dev)) {
		/* Make sure any changes to this entry are flushed */
		wmb();

		*(u8 *)(*mpt_entry) = MLX4_MPT_STATUS_HW;

		/* Make sure the new status is written */
		wmb();

		err = mlx4_SYNC_TPT(dev);
	} else {
		int key = key_to_hw_index(mmr->key) & (dev->caps.num_mpts - 1);

		struct mlx4_cmd_mailbox *mailbox =
			container_of((void *)mpt_entry, struct mlx4_cmd_mailbox,
				     buf);

		err = mlx4_SW2HW_MPT(dev, mailbox, key);
	}

	if (!err) {
		mmr->pd = be32_to_cpu((*mpt_entry)->pd_flags) & MLX4_MPT_PD_MASK;
		mmr->enabled = MLX4_MPT_EN_HW;
	}
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_mr_hw_write_mpt);

void mlx4_mr_hw_put_mpt(struct mlx4_dev *dev,
			struct mlx4_mpt_entry **mpt_entry)
{
	if (mlx4_is_mfunc(dev)) {
		struct mlx4_cmd_mailbox *mailbox =
			container_of((void *)mpt_entry, struct mlx4_cmd_mailbox,
				     buf);
		mlx4_free_cmd_mailbox(dev, mailbox);
	}
}
EXPORT_SYMBOL_GPL(mlx4_mr_hw_put_mpt);

int mlx4_mr_hw_change_pd(struct mlx4_dev *dev, struct mlx4_mpt_entry *mpt_entry,
			 u32 pdn)
{
	u32 pd_flags = be32_to_cpu(mpt_entry->pd_flags) & ~MLX4_MPT_PD_MASK;
	/* The wrapper function will put the slave's id here */
	if (mlx4_is_mfunc(dev))
		pd_flags &= ~MLX4_MPT_PD_VF_MASK;

	mpt_entry->pd_flags = cpu_to_be32(pd_flags |
					  (pdn & MLX4_MPT_PD_MASK)
					  | MLX4_MPT_PD_FLAG_EN_INV);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_mr_hw_change_pd);

int mlx4_mr_hw_change_access(struct mlx4_dev *dev,
			     struct mlx4_mpt_entry *mpt_entry,
			     u32 access)
{
	u32 flags = (be32_to_cpu(mpt_entry->flags) & ~MLX4_PERM_MASK) |
		    (access & MLX4_PERM_MASK);

	mpt_entry->flags = cpu_to_be32(flags);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_mr_hw_change_access);

static int mlx4_mr_alloc_reserved(struct mlx4_dev *dev, u32 mridx, u32 pd,
			   u64 iova, u64 size, u32 access, int npages,
			   int page_shift, struct mlx4_mr *mr)
{
	mr->iova       = iova;
	mr->size       = size;
	mr->pd	       = pd;
	mr->access     = access;
	mr->enabled    = MLX4_MPT_DISABLED;
	mr->key	       = hw_index_to_key(mridx);

	return mlx4_mtt_init(dev, npages, page_shift, &mr->mtt);
}

static int mlx4_WRITE_MTT(struct mlx4_dev *dev,
			  struct mlx4_cmd_mailbox *mailbox,
			  int num_entries)
{
	return mlx4_cmd(dev, mailbox->dma, num_entries, 0, MLX4_CMD_WRITE_MTT,
			MLX4_CMD_TIME_CLASS_A,  MLX4_CMD_WRAPPED);
}

int __mlx4_mpt_reserve(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	return mlx4_bitmap_alloc(&priv->mr_table.mpt_bitmap);
}

static int mlx4_mpt_reserve(struct mlx4_dev *dev)
{
	u64 out_param;

	if (mlx4_is_mfunc(dev)) {
		if (mlx4_cmd_imm(dev, 0, &out_param, RES_MPT, RES_OP_RESERVE,
				   MLX4_CMD_ALLOC_RES,
				   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED))
			return -1;
		return get_param_l(&out_param);
	}
	return  __mlx4_mpt_reserve(dev);
}

void __mlx4_mpt_release(struct mlx4_dev *dev, u32 index)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	mlx4_bitmap_free(&priv->mr_table.mpt_bitmap, index, MLX4_NO_RR);
}

static void mlx4_mpt_release(struct mlx4_dev *dev, u32 index)
{
	u64 in_param = 0;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, index);
		if (mlx4_cmd(dev, in_param, RES_MPT, RES_OP_RESERVE,
			       MLX4_CMD_FREE_RES,
			       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED))
			mlx4_warn(dev, "Failed to release mr index:%d\n",
				  index);
		return;
	}
	__mlx4_mpt_release(dev, index);
}

int __mlx4_mpt_alloc_icm(struct mlx4_dev *dev, u32 index, gfp_t gfp)
{
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;

	return mlx4_table_get(dev, &mr_table->dmpt_table, index, gfp);
}

static int mlx4_mpt_alloc_icm(struct mlx4_dev *dev, u32 index, gfp_t gfp)
{
	u64 param = 0;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&param, index);
		return mlx4_cmd_imm(dev, param, &param, RES_MPT, RES_OP_MAP_ICM,
							MLX4_CMD_ALLOC_RES,
							MLX4_CMD_TIME_CLASS_A,
							MLX4_CMD_WRAPPED);
	}
	return __mlx4_mpt_alloc_icm(dev, index, gfp);
}

void __mlx4_mpt_free_icm(struct mlx4_dev *dev, u32 index)
{
	struct mlx4_mr_table *mr_table = &mlx4_priv(dev)->mr_table;

	mlx4_table_put(dev, &mr_table->dmpt_table, index);
}

static void mlx4_mpt_free_icm(struct mlx4_dev *dev, u32 index)
{
	u64 in_param = 0;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, index);
		if (mlx4_cmd(dev, in_param, RES_MPT, RES_OP_MAP_ICM,
			     MLX4_CMD_FREE_RES, MLX4_CMD_TIME_CLASS_A,
			     MLX4_CMD_WRAPPED))
			mlx4_warn(dev, "Failed to free icm of mr index:%d\n",
				  index);
		return;
	}
	return __mlx4_mpt_free_icm(dev, index);
}

int mlx4_mr_alloc(struct mlx4_dev *dev, u32 pd, u64 iova, u64 size, u32 access,
		  int npages, int page_shift, struct mlx4_mr *mr)
{
	u32 index;
	int err;

	index = mlx4_mpt_reserve(dev);
	if (index == -1)
		return -ENOMEM;

	err = mlx4_mr_alloc_reserved(dev, index, pd, iova, size,
				     access, npages, page_shift, mr);
	if (err)
		mlx4_mpt_release(dev, index);

	return err;
}
EXPORT_SYMBOL_GPL(mlx4_mr_alloc);

static int mlx4_mr_free_reserved(struct mlx4_dev *dev, struct mlx4_mr *mr)
{
	int err;

	if (mr->enabled == MLX4_MPT_EN_HW) {
		err = mlx4_HW2SW_MPT(dev, NULL,
				     key_to_hw_index(mr->key) &
				     (dev->caps.num_mpts - 1));
		if (err) {
			mlx4_warn(dev, "HW2SW_MPT failed (%d), MR has MWs bound to it\n",
				  err);
			return err;
		}

		mr->enabled = MLX4_MPT_EN_SW;
	}
	mlx4_mtt_cleanup(dev, &mr->mtt);

	return 0;
}

int mlx4_mr_free(struct mlx4_dev *dev, struct mlx4_mr *mr)
{
	int ret;

	ret = mlx4_mr_free_reserved(dev, mr);
	if (ret)
		return ret;
	if (mr->enabled)
		mlx4_mpt_free_icm(dev, key_to_hw_index(mr->key));
	mlx4_mpt_release(dev, key_to_hw_index(mr->key));

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_mr_free);

void mlx4_mr_rereg_mem_cleanup(struct mlx4_dev *dev, struct mlx4_mr *mr)
{
	mlx4_mtt_cleanup(dev, &mr->mtt);
	mr->mtt.order = -1;
}
EXPORT_SYMBOL_GPL(mlx4_mr_rereg_mem_cleanup);

int mlx4_mr_rereg_mem_write(struct mlx4_dev *dev, struct mlx4_mr *mr,
			    u64 iova, u64 size, int npages,
			    int page_shift, struct mlx4_mpt_entry *mpt_entry)
{
	int err;

	err = mlx4_mtt_init(dev, npages, page_shift, &mr->mtt);
	if (err)
		return err;

	mpt_entry->start       = cpu_to_be64(iova);
	mpt_entry->length      = cpu_to_be64(size);
	mpt_entry->entity_size = cpu_to_be32(page_shift);
	mpt_entry->flags    &= ~(cpu_to_be32(MLX4_MPT_FLAG_FREE |
					   MLX4_MPT_FLAG_SW_OWNS));
	if (mr->mtt.order < 0) {
		mpt_entry->flags |= cpu_to_be32(MLX4_MPT_FLAG_PHYSICAL);
		mpt_entry->mtt_addr = 0;
	} else {
		mpt_entry->mtt_addr = cpu_to_be64(mlx4_mtt_addr(dev,
						  &mr->mtt));
		if (mr->mtt.page_shift == 0)
			mpt_entry->mtt_sz    = cpu_to_be32(1 << mr->mtt.order);
	}
	if (mr->mtt.order >= 0 && mr->mtt.page_shift == 0) {
		/* fast register MR in free state */
		mpt_entry->flags    |= cpu_to_be32(MLX4_MPT_FLAG_FREE);
		mpt_entry->pd_flags |= cpu_to_be32(MLX4_MPT_PD_FLAG_FAST_REG |
						   MLX4_MPT_PD_FLAG_RAE);
	} else {
		mpt_entry->flags    |= cpu_to_be32(MLX4_MPT_FLAG_SW_OWNS);
	}
	mr->enabled = MLX4_MPT_EN_SW;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_mr_rereg_mem_write);

int mlx4_mr_enable(struct mlx4_dev *dev, struct mlx4_mr *mr)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_mpt_entry *mpt_entry;
	int err;

	err = mlx4_mpt_alloc_icm(dev, key_to_hw_index(mr->key), GFP_KERNEL);
	if (err)
		return err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_table;
	}
	mpt_entry = mailbox->buf;
	mpt_entry->flags = cpu_to_be32(MLX4_MPT_FLAG_MIO	 |
				       MLX4_MPT_FLAG_REGION	 |
				       mr->access);

	mpt_entry->key	       = cpu_to_be32(key_to_hw_index(mr->key));
	mpt_entry->pd_flags    = cpu_to_be32(mr->pd | MLX4_MPT_PD_FLAG_EN_INV);
	mpt_entry->start       = cpu_to_be64(mr->iova);
	mpt_entry->length      = cpu_to_be64(mr->size);
	mpt_entry->entity_size = cpu_to_be32(mr->mtt.page_shift);

	if (mr->mtt.order < 0) {
		mpt_entry->flags |= cpu_to_be32(MLX4_MPT_FLAG_PHYSICAL);
		mpt_entry->mtt_addr = 0;
	} else {
		mpt_entry->mtt_addr = cpu_to_be64(mlx4_mtt_addr(dev,
						  &mr->mtt));
	}

	if (mr->mtt.order >= 0 && mr->mtt.page_shift == 0) {
		/* fast register MR in free state */
		mpt_entry->flags    |= cpu_to_be32(MLX4_MPT_FLAG_FREE);
		mpt_entry->pd_flags |= cpu_to_be32(MLX4_MPT_PD_FLAG_FAST_REG |
						   MLX4_MPT_PD_FLAG_RAE);
		mpt_entry->mtt_sz    = cpu_to_be32(1 << mr->mtt.order);
	} else {
		mpt_entry->flags    |= cpu_to_be32(MLX4_MPT_FLAG_SW_OWNS);
	}

	err = mlx4_SW2HW_MPT(dev, mailbox,
			     key_to_hw_index(mr->key) & (dev->caps.num_mpts - 1));
	if (err) {
		mlx4_warn(dev, "SW2HW_MPT failed (%d)\n", err);
		goto err_cmd;
	}
	mr->enabled = MLX4_MPT_EN_HW;

	mlx4_free_cmd_mailbox(dev, mailbox);

	return 0;

err_cmd:
	mlx4_free_cmd_mailbox(dev, mailbox);

err_table:
	mlx4_mpt_free_icm(dev, key_to_hw_index(mr->key));
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_mr_enable);

static int mlx4_write_mtt_chunk(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
				int start_index, int npages, u64 *page_list)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	__be64 *mtts;
	dma_addr_t dma_handle;
	int i;

	mtts = mlx4_table_find(&priv->mr_table.mtt_table, mtt->offset +
			       start_index, &dma_handle);

	if (!mtts)
		return -ENOMEM;

	dma_sync_single_for_cpu(&dev->persist->pdev->dev, dma_handle,
				npages * sizeof (u64), DMA_TO_DEVICE);

	for (i = 0; i < npages; ++i)
		mtts[i] = cpu_to_be64(page_list[i] | MLX4_MTT_FLAG_PRESENT);

	dma_sync_single_for_device(&dev->persist->pdev->dev, dma_handle,
				   npages * sizeof (u64), DMA_TO_DEVICE);

	return 0;
}

int __mlx4_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		     int start_index, int npages, u64 *page_list)
{
	int err = 0;
	int chunk;
	int mtts_per_page;
	int max_mtts_first_page;

	/* compute how may mtts fit in the first page */
	mtts_per_page = PAGE_SIZE / sizeof(u64);
	max_mtts_first_page = mtts_per_page - (mtt->offset + start_index)
			      % mtts_per_page;

	chunk = min_t(int, max_mtts_first_page, npages);

	while (npages > 0) {
		err = mlx4_write_mtt_chunk(dev, mtt, start_index, chunk, page_list);
		if (err)
			return err;
		npages      -= chunk;
		start_index += chunk;
		page_list   += chunk;

		chunk = min_t(int, mtts_per_page, npages);
	}
	return err;
}

int mlx4_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   int start_index, int npages, u64 *page_list)
{
	struct mlx4_cmd_mailbox *mailbox = NULL;
	__be64 *inbox = NULL;
	int chunk;
	int err = 0;
	int i;

	if (mtt->order < 0)
		return -EINVAL;

	if (mlx4_is_mfunc(dev)) {
		mailbox = mlx4_alloc_cmd_mailbox(dev);
		if (IS_ERR(mailbox))
			return PTR_ERR(mailbox);
		inbox = mailbox->buf;

		while (npages > 0) {
			chunk = min_t(int, MLX4_MAILBOX_SIZE / sizeof(u64) - 2,
				      npages);
			inbox[0] = cpu_to_be64(mtt->offset + start_index);
			inbox[1] = 0;
			for (i = 0; i < chunk; ++i)
				inbox[i + 2] = cpu_to_be64(page_list[i] |
					       MLX4_MTT_FLAG_PRESENT);
			err = mlx4_WRITE_MTT(dev, mailbox, chunk);
			if (err) {
				mlx4_free_cmd_mailbox(dev, mailbox);
				return err;
			}

			npages      -= chunk;
			start_index += chunk;
			page_list   += chunk;
		}
		mlx4_free_cmd_mailbox(dev, mailbox);
		return err;
	}

	return __mlx4_write_mtt(dev, mtt, start_index, npages, page_list);
}
EXPORT_SYMBOL_GPL(mlx4_write_mtt);

int mlx4_buf_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		       struct mlx4_buf *buf, gfp_t gfp)
{
	u64 *page_list;
	int err;
	int i;

	page_list = kmalloc(buf->npages * sizeof *page_list,
			    gfp);
	if (!page_list)
		return -ENOMEM;

	for (i = 0; i < buf->npages; ++i)
		if (buf->nbufs == 1)
			page_list[i] = buf->direct.map + (i << buf->page_shift);
		else
			page_list[i] = buf->page_list[i].map;

	err = mlx4_write_mtt(dev, mtt, 0, buf->npages, page_list);

	kfree(page_list);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_buf_write_mtt);

int mlx4_mw_alloc(struct mlx4_dev *dev, u32 pd, enum mlx4_mw_type type,
		  struct mlx4_mw *mw)
{
	u32 index;

	if ((type == MLX4_MW_TYPE_1 &&
	     !(dev->caps.flags & MLX4_DEV_CAP_FLAG_MEM_WINDOW)) ||
	     (type == MLX4_MW_TYPE_2 &&
	     !(dev->caps.bmme_flags & MLX4_BMME_FLAG_TYPE_2_WIN)))
		return -EOPNOTSUPP;

	index = mlx4_mpt_reserve(dev);
	if (index == -1)
		return -ENOMEM;

	mw->key	    = hw_index_to_key(index);
	mw->pd      = pd;
	mw->type    = type;
	mw->enabled = MLX4_MPT_DISABLED;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_mw_alloc);

int mlx4_mw_enable(struct mlx4_dev *dev, struct mlx4_mw *mw)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_mpt_entry *mpt_entry;
	int err;

	err = mlx4_mpt_alloc_icm(dev, key_to_hw_index(mw->key), GFP_KERNEL);
	if (err)
		return err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_table;
	}
	mpt_entry = mailbox->buf;

	/* Note that the MLX4_MPT_FLAG_REGION bit in mpt_entry->flags is turned
	 * off, thus creating a memory window and not a memory region.
	 */
	mpt_entry->key	       = cpu_to_be32(key_to_hw_index(mw->key));
	mpt_entry->pd_flags    = cpu_to_be32(mw->pd);
	if (mw->type == MLX4_MW_TYPE_2) {
		mpt_entry->flags    |= cpu_to_be32(MLX4_MPT_FLAG_FREE);
		mpt_entry->qpn       = cpu_to_be32(MLX4_MPT_QP_FLAG_BOUND_QP);
		mpt_entry->pd_flags |= cpu_to_be32(MLX4_MPT_PD_FLAG_EN_INV);
	}

	err = mlx4_SW2HW_MPT(dev, mailbox,
			     key_to_hw_index(mw->key) &
			     (dev->caps.num_mpts - 1));
	if (err) {
		mlx4_warn(dev, "SW2HW_MPT failed (%d)\n", err);
		goto err_cmd;
	}
	mw->enabled = MLX4_MPT_EN_HW;

	mlx4_free_cmd_mailbox(dev, mailbox);

	return 0;

err_cmd:
	mlx4_free_cmd_mailbox(dev, mailbox);

err_table:
	mlx4_mpt_free_icm(dev, key_to_hw_index(mw->key));
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_mw_enable);

void mlx4_mw_free(struct mlx4_dev *dev, struct mlx4_mw *mw)
{
	int err;

	if (mw->enabled == MLX4_MPT_EN_HW) {
		err = mlx4_HW2SW_MPT(dev, NULL,
				     key_to_hw_index(mw->key) &
				     (dev->caps.num_mpts - 1));
		if (err)
			mlx4_warn(dev, "xxx HW2SW_MPT failed (%d)\n", err);

		mw->enabled = MLX4_MPT_EN_SW;
	}
	if (mw->enabled)
		mlx4_mpt_free_icm(dev, key_to_hw_index(mw->key));
	mlx4_mpt_release(dev, key_to_hw_index(mw->key));
}
EXPORT_SYMBOL_GPL(mlx4_mw_free);

int mlx4_init_mr_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_mr_table *mr_table = &priv->mr_table;
	int err;

	/* Nothing to do for slaves - all MR handling is forwarded
	* to the master */
	if (mlx4_is_slave(dev))
		return 0;

	if (!is_power_of_2(dev->caps.num_mpts))
		return -EINVAL;

	err = mlx4_bitmap_init(&mr_table->mpt_bitmap, dev->caps.num_mpts,
			       ~0, dev->caps.reserved_mrws, 0);
	if (err)
		return err;

	err = mlx4_buddy_init(&mr_table->mtt_buddy,
			      ilog2((u32)dev->caps.num_mtts /
			      (1 << log_mtts_per_seg)));
	if (err)
		goto err_buddy;

	if (dev->caps.reserved_mtts) {
		priv->reserved_mtts =
			mlx4_alloc_mtt_range(dev,
					     fls(dev->caps.reserved_mtts - 1));
		if (priv->reserved_mtts < 0) {
			mlx4_warn(dev, "MTT table of order %u is too small\n",
				  mr_table->mtt_buddy.max_order);
			err = -ENOMEM;
			goto err_reserve_mtts;
		}
	}

	return 0;

err_reserve_mtts:
	mlx4_buddy_cleanup(&mr_table->mtt_buddy);

err_buddy:
	mlx4_bitmap_cleanup(&mr_table->mpt_bitmap);

	return err;
}

void mlx4_cleanup_mr_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_mr_table *mr_table = &priv->mr_table;

	if (mlx4_is_slave(dev))
		return;
	if (priv->reserved_mtts >= 0)
		mlx4_free_mtt_range(dev, priv->reserved_mtts,
				    fls(dev->caps.reserved_mtts - 1));
	mlx4_buddy_cleanup(&mr_table->mtt_buddy);
	mlx4_bitmap_cleanup(&mr_table->mpt_bitmap);
}

static inline int mlx4_check_fmr(struct mlx4_fmr *fmr, u64 *page_list,
				  int npages, u64 iova)
{
	int i, page_mask;

	if (npages > fmr->max_pages)
		return -EINVAL;

	page_mask = (1 << fmr->page_shift) - 1;

	/* We are getting page lists, so va must be page aligned. */
	if (iova & page_mask)
		return -EINVAL;

	/* Trust the user not to pass misaligned data in page_list */
	if (0)
		for (i = 0; i < npages; ++i) {
			if (page_list[i] & ~page_mask)
				return -EINVAL;
		}

	if (fmr->maps >= fmr->max_maps)
		return -EINVAL;

	return 0;
}

int mlx4_map_phys_fmr(struct mlx4_dev *dev, struct mlx4_fmr *fmr, u64 *page_list,
		      int npages, u64 iova, u32 *lkey, u32 *rkey)
{
	u32 key;
	int i, err;

	err = mlx4_check_fmr(fmr, page_list, npages, iova);
	if (err)
		return err;

	++fmr->maps;

	key = key_to_hw_index(fmr->mr.key);
	key += dev->caps.num_mpts;
	*lkey = *rkey = fmr->mr.key = hw_index_to_key(key);

	*(u8 *) fmr->mpt = MLX4_MPT_STATUS_SW;

	/* Make sure MPT status is visible before writing MTT entries */
	wmb();

	dma_sync_single_for_cpu(&dev->persist->pdev->dev, fmr->dma_handle,
				npages * sizeof(u64), DMA_TO_DEVICE);

	for (i = 0; i < npages; ++i)
		fmr->mtts[i] = cpu_to_be64(page_list[i] | MLX4_MTT_FLAG_PRESENT);

	dma_sync_single_for_device(&dev->persist->pdev->dev, fmr->dma_handle,
				   npages * sizeof(u64), DMA_TO_DEVICE);

	fmr->mpt->key    = cpu_to_be32(key);
	fmr->mpt->lkey   = cpu_to_be32(key);
	fmr->mpt->length = cpu_to_be64(npages * (1ull << fmr->page_shift));
	fmr->mpt->start  = cpu_to_be64(iova);

	/* Make MTT entries are visible before setting MPT status */
	wmb();

	*(u8 *) fmr->mpt = MLX4_MPT_STATUS_HW;

	/* Make sure MPT status is visible before consumer can use FMR */
	wmb();

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_map_phys_fmr);

int mlx4_fmr_alloc(struct mlx4_dev *dev, u32 pd, u32 access, int max_pages,
		   int max_maps, u8 page_shift, struct mlx4_fmr *fmr)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err = -ENOMEM;

	if (max_maps > dev->caps.max_fmr_maps)
		return -EINVAL;

	if (page_shift < (ffs(dev->caps.page_size_cap) - 1) || page_shift >= 32)
		return -EINVAL;

	/* All MTTs must fit in the same page */
	if (max_pages * sizeof *fmr->mtts > PAGE_SIZE)
		return -EINVAL;

	fmr->page_shift = page_shift;
	fmr->max_pages  = max_pages;
	fmr->max_maps   = max_maps;
	fmr->maps = 0;

	err = mlx4_mr_alloc(dev, pd, 0, 0, access, max_pages,
			    page_shift, &fmr->mr);
	if (err)
		return err;

	fmr->mtts = mlx4_table_find(&priv->mr_table.mtt_table,
				    fmr->mr.mtt.offset,
				    &fmr->dma_handle);

	if (!fmr->mtts) {
		err = -ENOMEM;
		goto err_free;
	}

	return 0;

err_free:
	(void) mlx4_mr_free(dev, &fmr->mr);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_fmr_alloc);

int mlx4_fmr_enable(struct mlx4_dev *dev, struct mlx4_fmr *fmr)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;

	err = mlx4_mr_enable(dev, &fmr->mr);
	if (err)
		return err;

	fmr->mpt = mlx4_table_find(&priv->mr_table.dmpt_table,
				    key_to_hw_index(fmr->mr.key), NULL);
	if (!fmr->mpt)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_fmr_enable);

void mlx4_fmr_unmap(struct mlx4_dev *dev, struct mlx4_fmr *fmr,
		    u32 *lkey, u32 *rkey)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;

	if (!fmr->maps)
		return;

	fmr->maps = 0;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		pr_warn("mlx4_ib: mlx4_alloc_cmd_mailbox failed (%d)\n", err);
		return;
	}

	err = mlx4_HW2SW_MPT(dev, NULL,
			     key_to_hw_index(fmr->mr.key) &
			     (dev->caps.num_mpts - 1));
	mlx4_free_cmd_mailbox(dev, mailbox);
	if (err) {
		pr_warn("mlx4_ib: mlx4_HW2SW_MPT failed (%d)\n", err);
		return;
	}
	fmr->mr.enabled = MLX4_MPT_EN_SW;
}
EXPORT_SYMBOL_GPL(mlx4_fmr_unmap);

int mlx4_fmr_free(struct mlx4_dev *dev, struct mlx4_fmr *fmr)
{
	int ret;

	if (fmr->maps)
		return -EBUSY;

	ret = mlx4_mr_free(dev, &fmr->mr);
	if (ret)
		return ret;
	fmr->mr.enabled = MLX4_MPT_DISABLED;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_fmr_free);

int mlx4_SYNC_TPT(struct mlx4_dev *dev)
{
	return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_SYNC_TPT,
			MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);
}
EXPORT_SYMBOL_GPL(mlx4_SYNC_TPT);
