/*
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/mm.h>

#include <linux/mlx4/cmd.h>

#include "mlx4.h"
#include "icm.h"
#include "fw.h"

/*
 * We allocate in as big chunks as we can, up to a maximum of 256 KB
 * per chunk.
 */
enum {
	MLX4_ICM_ALLOC_SIZE	= 1 << 18,
	MLX4_TABLE_CHUNK_SIZE	= 1 << 18
};

void mlx4_free_icm(struct mlx4_dev *dev, struct mlx4_icm *icm)
{
	struct mlx4_icm_chunk *chunk, *tmp;
	int i;

	list_for_each_entry_safe(chunk, tmp, &icm->chunk_list, list) {
		if (chunk->nsg > 0)
			pci_unmap_sg(dev->pdev, chunk->mem, chunk->npages,
				     PCI_DMA_BIDIRECTIONAL);

		for (i = 0; i < chunk->npages; ++i)
			__free_pages(chunk->mem[i].page,
				     get_order(chunk->mem[i].length));

		kfree(chunk);
	}

	kfree(icm);
}

struct mlx4_icm *mlx4_alloc_icm(struct mlx4_dev *dev, int npages,
				gfp_t gfp_mask)
{
	struct mlx4_icm *icm;
	struct mlx4_icm_chunk *chunk = NULL;
	int cur_order;

	icm = kmalloc(sizeof *icm, gfp_mask & ~(__GFP_HIGHMEM | __GFP_NOWARN));
	if (!icm)
		return icm;

	icm->refcount = 0;
	INIT_LIST_HEAD(&icm->chunk_list);

	cur_order = get_order(MLX4_ICM_ALLOC_SIZE);

	while (npages > 0) {
		if (!chunk) {
			chunk = kmalloc(sizeof *chunk,
					gfp_mask & ~(__GFP_HIGHMEM | __GFP_NOWARN));
			if (!chunk)
				goto fail;

			chunk->npages = 0;
			chunk->nsg    = 0;
			list_add_tail(&chunk->list, &icm->chunk_list);
		}

		while (1 << cur_order > npages)
			--cur_order;

		chunk->mem[chunk->npages].page = alloc_pages(gfp_mask, cur_order);
		if (chunk->mem[chunk->npages].page) {
			chunk->mem[chunk->npages].length = PAGE_SIZE << cur_order;
			chunk->mem[chunk->npages].offset = 0;

			if (++chunk->npages == MLX4_ICM_CHUNK_LEN) {
				chunk->nsg = pci_map_sg(dev->pdev, chunk->mem,
							chunk->npages,
							PCI_DMA_BIDIRECTIONAL);

				if (chunk->nsg <= 0)
					goto fail;

				chunk = NULL;
			}

			npages -= 1 << cur_order;
		} else {
			--cur_order;
			if (cur_order < 0)
				goto fail;
		}
	}

	if (chunk) {
		chunk->nsg = pci_map_sg(dev->pdev, chunk->mem,
					chunk->npages,
					PCI_DMA_BIDIRECTIONAL);

		if (chunk->nsg <= 0)
			goto fail;
	}

	return icm;

fail:
	mlx4_free_icm(dev, icm);
	return NULL;
}

static int mlx4_MAP_ICM(struct mlx4_dev *dev, struct mlx4_icm *icm, u64 virt)
{
	return mlx4_map_cmd(dev, MLX4_CMD_MAP_ICM, icm, virt);
}

int mlx4_UNMAP_ICM(struct mlx4_dev *dev, u64 virt, u32 page_count)
{
	return mlx4_cmd(dev, virt, page_count, 0, MLX4_CMD_UNMAP_ICM,
			MLX4_CMD_TIME_CLASS_B);
}

int mlx4_MAP_ICM_page(struct mlx4_dev *dev, u64 dma_addr, u64 virt)
{
	struct mlx4_cmd_mailbox *mailbox;
	__be64 *inbox;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	inbox = mailbox->buf;

	inbox[0] = cpu_to_be64(virt);
	inbox[1] = cpu_to_be64(dma_addr);

	err = mlx4_cmd(dev, mailbox->dma, 1, 0, MLX4_CMD_MAP_ICM,
		       MLX4_CMD_TIME_CLASS_B);

	mlx4_free_cmd_mailbox(dev, mailbox);

	if (!err)
		mlx4_dbg(dev, "Mapped page at %llx to %llx for ICM.\n",
			  (unsigned long long) dma_addr, (unsigned long long) virt);

	return err;
}

int mlx4_MAP_ICM_AUX(struct mlx4_dev *dev, struct mlx4_icm *icm)
{
	return mlx4_map_cmd(dev, MLX4_CMD_MAP_ICM_AUX, icm, -1);
}

int mlx4_UNMAP_ICM_AUX(struct mlx4_dev *dev)
{
	return mlx4_cmd(dev, 0, 0, 0, MLX4_CMD_UNMAP_ICM_AUX, MLX4_CMD_TIME_CLASS_B);
}

int mlx4_table_get(struct mlx4_dev *dev, struct mlx4_icm_table *table, int obj)
{
	int i = (obj & (table->num_obj - 1)) / (MLX4_TABLE_CHUNK_SIZE / table->obj_size);
	int ret = 0;

	mutex_lock(&table->mutex);

	if (table->icm[i]) {
		++table->icm[i]->refcount;
		goto out;
	}

	table->icm[i] = mlx4_alloc_icm(dev, MLX4_TABLE_CHUNK_SIZE >> PAGE_SHIFT,
				       (table->lowmem ? GFP_KERNEL : GFP_HIGHUSER) |
				       __GFP_NOWARN);
	if (!table->icm[i]) {
		ret = -ENOMEM;
		goto out;
	}

	if (mlx4_MAP_ICM(dev, table->icm[i], table->virt +
			 (u64) i * MLX4_TABLE_CHUNK_SIZE)) {
		mlx4_free_icm(dev, table->icm[i]);
		table->icm[i] = NULL;
		ret = -ENOMEM;
		goto out;
	}

	++table->icm[i]->refcount;

out:
	mutex_unlock(&table->mutex);
	return ret;
}

void mlx4_table_put(struct mlx4_dev *dev, struct mlx4_icm_table *table, int obj)
{
	int i;

	i = (obj & (table->num_obj - 1)) / (MLX4_TABLE_CHUNK_SIZE / table->obj_size);

	mutex_lock(&table->mutex);

	if (--table->icm[i]->refcount == 0) {
		mlx4_UNMAP_ICM(dev, table->virt + i * MLX4_TABLE_CHUNK_SIZE,
			       MLX4_TABLE_CHUNK_SIZE / MLX4_ICM_PAGE_SIZE);
		mlx4_free_icm(dev, table->icm[i]);
		table->icm[i] = NULL;
	}

	mutex_unlock(&table->mutex);
}

void *mlx4_table_find(struct mlx4_icm_table *table, int obj)
{
	int idx, offset, i;
	struct mlx4_icm_chunk *chunk;
	struct mlx4_icm *icm;
	struct page *page = NULL;

	if (!table->lowmem)
		return NULL;

	mutex_lock(&table->mutex);

	idx = obj & (table->num_obj - 1);
	icm = table->icm[idx / (MLX4_TABLE_CHUNK_SIZE / table->obj_size)];
	offset = idx % (MLX4_TABLE_CHUNK_SIZE / table->obj_size);

	if (!icm)
		goto out;

	list_for_each_entry(chunk, &icm->chunk_list, list) {
		for (i = 0; i < chunk->npages; ++i) {
			if (chunk->mem[i].length > offset) {
				page = chunk->mem[i].page;
				goto out;
			}
			offset -= chunk->mem[i].length;
		}
	}

out:
	mutex_unlock(&table->mutex);
	return page ? lowmem_page_address(page) + offset : NULL;
}

int mlx4_table_get_range(struct mlx4_dev *dev, struct mlx4_icm_table *table,
			 int start, int end)
{
	int inc = MLX4_TABLE_CHUNK_SIZE / table->obj_size;
	int i, err;

	for (i = start; i <= end; i += inc) {
		err = mlx4_table_get(dev, table, i);
		if (err)
			goto fail;
	}

	return 0;

fail:
	while (i > start) {
		i -= inc;
		mlx4_table_put(dev, table, i);
	}

	return err;
}

void mlx4_table_put_range(struct mlx4_dev *dev, struct mlx4_icm_table *table,
			  int start, int end)
{
	int i;

	for (i = start; i <= end; i += MLX4_TABLE_CHUNK_SIZE / table->obj_size)
		mlx4_table_put(dev, table, i);
}

int mlx4_init_icm_table(struct mlx4_dev *dev, struct mlx4_icm_table *table,
			u64 virt, int obj_size,	int nobj, int reserved,
			int use_lowmem)
{
	int obj_per_chunk;
	int num_icm;
	unsigned chunk_size;
	int i;

	obj_per_chunk = MLX4_TABLE_CHUNK_SIZE / obj_size;
	num_icm = (nobj + obj_per_chunk - 1) / obj_per_chunk;

	table->icm      = kcalloc(num_icm, sizeof *table->icm, GFP_KERNEL);
	if (!table->icm)
		return -ENOMEM;
	table->virt     = virt;
	table->num_icm  = num_icm;
	table->num_obj  = nobj;
	table->obj_size = obj_size;
	table->lowmem   = use_lowmem;
	mutex_init(&table->mutex);

	for (i = 0; i * MLX4_TABLE_CHUNK_SIZE < reserved * obj_size; ++i) {
		chunk_size = MLX4_TABLE_CHUNK_SIZE;
		if ((i + 1) * MLX4_TABLE_CHUNK_SIZE > nobj * obj_size)
			chunk_size = PAGE_ALIGN(nobj * obj_size - i * MLX4_TABLE_CHUNK_SIZE);

		table->icm[i] = mlx4_alloc_icm(dev, chunk_size >> PAGE_SHIFT,
					       (use_lowmem ? GFP_KERNEL : GFP_HIGHUSER) |
					       __GFP_NOWARN);
		if (!table->icm[i])
			goto err;
		if (mlx4_MAP_ICM(dev, table->icm[i], virt + i * MLX4_TABLE_CHUNK_SIZE)) {
			mlx4_free_icm(dev, table->icm[i]);
			table->icm[i] = NULL;
			goto err;
		}

		/*
		 * Add a reference to this ICM chunk so that it never
		 * gets freed (since it contains reserved firmware objects).
		 */
		++table->icm[i]->refcount;
	}

	return 0;

err:
	for (i = 0; i < num_icm; ++i)
		if (table->icm[i]) {
			mlx4_UNMAP_ICM(dev, virt + i * MLX4_TABLE_CHUNK_SIZE,
				       MLX4_TABLE_CHUNK_SIZE / MLX4_ICM_PAGE_SIZE);
			mlx4_free_icm(dev, table->icm[i]);
		}

	return -ENOMEM;
}

void mlx4_cleanup_icm_table(struct mlx4_dev *dev, struct mlx4_icm_table *table)
{
	int i;

	for (i = 0; i < table->num_icm; ++i)
		if (table->icm[i]) {
			mlx4_UNMAP_ICM(dev, table->virt + i * MLX4_TABLE_CHUNK_SIZE,
				       MLX4_TABLE_CHUNK_SIZE / MLX4_ICM_PAGE_SIZE);
			mlx4_free_icm(dev, table->icm[i]);
		}

	kfree(table->icm);
}
