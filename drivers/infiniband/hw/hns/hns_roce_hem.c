/*
 * Copyright (c) 2016 Hisilicon Limited.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#include <linux/platform_device.h>
#include "hns_roce_device.h"
#include "hns_roce_hem.h"
#include "hns_roce_common.h"

#define HNS_ROCE_HEM_ALLOC_SIZE		(1 << 17)
#define HNS_ROCE_TABLE_CHUNK_SIZE	(1 << 17)

#define DMA_ADDR_T_SHIFT		12
#define BT_BA_SHIFT			32

struct hns_roce_hem *hns_roce_alloc_hem(struct hns_roce_dev *hr_dev, int npages,
					gfp_t gfp_mask)
{
	struct hns_roce_hem_chunk *chunk = NULL;
	struct hns_roce_hem *hem;
	struct scatterlist *mem;
	int order;
	void *buf;

	WARN_ON(gfp_mask & __GFP_HIGHMEM);

	hem = kmalloc(sizeof(*hem),
		      gfp_mask & ~(__GFP_HIGHMEM | __GFP_NOWARN));
	if (!hem)
		return NULL;

	hem->refcount = 0;
	INIT_LIST_HEAD(&hem->chunk_list);

	order = get_order(HNS_ROCE_HEM_ALLOC_SIZE);

	while (npages > 0) {
		if (!chunk) {
			chunk = kmalloc(sizeof(*chunk),
				gfp_mask & ~(__GFP_HIGHMEM | __GFP_NOWARN));
			if (!chunk)
				goto fail;

			sg_init_table(chunk->mem, HNS_ROCE_HEM_CHUNK_LEN);
			chunk->npages = 0;
			chunk->nsg = 0;
			list_add_tail(&chunk->list, &hem->chunk_list);
		}

		while (1 << order > npages)
			--order;

		/*
		 * Alloc memory one time. If failed, don't alloc small block
		 * memory, directly return fail.
		 */
		mem = &chunk->mem[chunk->npages];
		buf = dma_alloc_coherent(&hr_dev->pdev->dev, PAGE_SIZE << order,
				&sg_dma_address(mem), gfp_mask);
		if (!buf)
			goto fail;

		sg_set_buf(mem, buf, PAGE_SIZE << order);
		WARN_ON(mem->offset);
		sg_dma_len(mem) = PAGE_SIZE << order;

		++chunk->npages;
		++chunk->nsg;
		npages -= 1 << order;
	}

	return hem;

fail:
	hns_roce_free_hem(hr_dev, hem);
	return NULL;
}

void hns_roce_free_hem(struct hns_roce_dev *hr_dev, struct hns_roce_hem *hem)
{
	struct hns_roce_hem_chunk *chunk, *tmp;
	int i;

	if (!hem)
		return;

	list_for_each_entry_safe(chunk, tmp, &hem->chunk_list, list) {
		for (i = 0; i < chunk->npages; ++i)
			dma_free_coherent(&hr_dev->pdev->dev,
				   chunk->mem[i].length,
				   lowmem_page_address(sg_page(&chunk->mem[i])),
				   sg_dma_address(&chunk->mem[i]));
		kfree(chunk);
	}

	kfree(hem);
}

static int hns_roce_set_hem(struct hns_roce_dev *hr_dev,
			    struct hns_roce_hem_table *table, unsigned long obj)
{
	struct device *dev = &hr_dev->pdev->dev;
	spinlock_t *lock = &hr_dev->bt_cmd_lock;
	unsigned long end = 0;
	unsigned long flags;
	struct hns_roce_hem_iter iter;
	void __iomem *bt_cmd;
	u32 bt_cmd_h_val = 0;
	u32 bt_cmd_val[2];
	u32 bt_cmd_l = 0;
	u64 bt_ba = 0;
	int ret = 0;

	/* Find the HEM(Hardware Entry Memory) entry */
	unsigned long i = (obj & (table->num_obj - 1)) /
			  (HNS_ROCE_TABLE_CHUNK_SIZE / table->obj_size);

	switch (table->type) {
	case HEM_TYPE_QPC:
		roce_set_field(bt_cmd_h_val, ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_M,
			       ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S, HEM_TYPE_QPC);
		break;
	case HEM_TYPE_MTPT:
		roce_set_field(bt_cmd_h_val, ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_M,
			       ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S,
			       HEM_TYPE_MTPT);
		break;
	case HEM_TYPE_CQC:
		roce_set_field(bt_cmd_h_val, ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_M,
			       ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S, HEM_TYPE_CQC);
		break;
	case HEM_TYPE_SRQC:
		roce_set_field(bt_cmd_h_val, ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_M,
			       ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S,
			       HEM_TYPE_SRQC);
		break;
	default:
		return ret;
	}
	roce_set_field(bt_cmd_h_val, ROCEE_BT_CMD_H_ROCEE_BT_CMD_IN_MDF_M,
		       ROCEE_BT_CMD_H_ROCEE_BT_CMD_IN_MDF_S, obj);
	roce_set_bit(bt_cmd_h_val, ROCEE_BT_CMD_H_ROCEE_BT_CMD_S, 0);
	roce_set_bit(bt_cmd_h_val, ROCEE_BT_CMD_H_ROCEE_BT_CMD_HW_SYNS_S, 1);

	/* Currently iter only a chunk */
	for (hns_roce_hem_first(table->hem[i], &iter);
	     !hns_roce_hem_last(&iter); hns_roce_hem_next(&iter)) {
		bt_ba = hns_roce_hem_addr(&iter) >> DMA_ADDR_T_SHIFT;

		spin_lock_irqsave(lock, flags);

		bt_cmd = hr_dev->reg_base + ROCEE_BT_CMD_H_REG;

		end = msecs_to_jiffies(HW_SYNC_TIMEOUT_MSECS) + jiffies;
		while (1) {
			if (readl(bt_cmd) >> BT_CMD_SYNC_SHIFT) {
				if (!(time_before(jiffies, end))) {
					dev_err(dev, "Write bt_cmd err,hw_sync is not zero.\n");
					spin_unlock_irqrestore(lock, flags);
					return -EBUSY;
				}
			} else {
				break;
			}
			msleep(HW_SYNC_SLEEP_TIME_INTERVAL);
		}

		bt_cmd_l = (u32)bt_ba;
		roce_set_field(bt_cmd_h_val, ROCEE_BT_CMD_H_ROCEE_BT_CMD_BA_H_M,
			       ROCEE_BT_CMD_H_ROCEE_BT_CMD_BA_H_S,
			       bt_ba >> BT_BA_SHIFT);

		bt_cmd_val[0] = bt_cmd_l;
		bt_cmd_val[1] = bt_cmd_h_val;
		hns_roce_write64_k(bt_cmd_val,
				   hr_dev->reg_base + ROCEE_BT_CMD_L_REG);
		spin_unlock_irqrestore(lock, flags);
	}

	return ret;
}

int hns_roce_table_get(struct hns_roce_dev *hr_dev,
		       struct hns_roce_hem_table *table, unsigned long obj)
{
	struct device *dev = &hr_dev->pdev->dev;
	int ret = 0;
	unsigned long i;

	i = (obj & (table->num_obj - 1)) / (HNS_ROCE_TABLE_CHUNK_SIZE /
	     table->obj_size);

	mutex_lock(&table->mutex);

	if (table->hem[i]) {
		++table->hem[i]->refcount;
		goto out;
	}

	table->hem[i] = hns_roce_alloc_hem(hr_dev,
				       HNS_ROCE_TABLE_CHUNK_SIZE >> PAGE_SHIFT,
				       (table->lowmem ? GFP_KERNEL :
					GFP_HIGHUSER) | __GFP_NOWARN);
	if (!table->hem[i]) {
		ret = -ENOMEM;
		goto out;
	}

	/* Set HEM base address(128K/page, pa) to Hardware */
	if (hns_roce_set_hem(hr_dev, table, obj)) {
		ret = -ENODEV;
		dev_err(dev, "set HEM base address to HW failed.\n");
		goto out;
	}

	++table->hem[i]->refcount;
out:
	mutex_unlock(&table->mutex);
	return ret;
}

void hns_roce_table_put(struct hns_roce_dev *hr_dev,
			struct hns_roce_hem_table *table, unsigned long obj)
{
	struct device *dev = &hr_dev->pdev->dev;
	unsigned long i;

	i = (obj & (table->num_obj - 1)) /
	    (HNS_ROCE_TABLE_CHUNK_SIZE / table->obj_size);

	mutex_lock(&table->mutex);

	if (--table->hem[i]->refcount == 0) {
		/* Clear HEM base address */
		if (hr_dev->hw->clear_hem(hr_dev, table, obj))
			dev_warn(dev, "Clear HEM base address failed.\n");

		hns_roce_free_hem(hr_dev, table->hem[i]);
		table->hem[i] = NULL;
	}

	mutex_unlock(&table->mutex);
}

void *hns_roce_table_find(struct hns_roce_hem_table *table, unsigned long obj,
			  dma_addr_t *dma_handle)
{
	struct hns_roce_hem_chunk *chunk;
	unsigned long idx;
	int i;
	int offset, dma_offset;
	struct hns_roce_hem *hem;
	struct page *page = NULL;

	if (!table->lowmem)
		return NULL;

	mutex_lock(&table->mutex);
	idx = (obj & (table->num_obj - 1)) * table->obj_size;
	hem = table->hem[idx / HNS_ROCE_TABLE_CHUNK_SIZE];
	dma_offset = offset = idx % HNS_ROCE_TABLE_CHUNK_SIZE;

	if (!hem)
		goto out;

	list_for_each_entry(chunk, &hem->chunk_list, list) {
		for (i = 0; i < chunk->npages; ++i) {
			if (dma_handle && dma_offset >= 0) {
				if (sg_dma_len(&chunk->mem[i]) >
				    (u32)dma_offset)
					*dma_handle = sg_dma_address(
						&chunk->mem[i]) + dma_offset;
				dma_offset -= sg_dma_len(&chunk->mem[i]);
			}

			if (chunk->mem[i].length > (u32)offset) {
				page = sg_page(&chunk->mem[i]);
				goto out;
			}
			offset -= chunk->mem[i].length;
		}
	}

out:
	mutex_unlock(&table->mutex);
	return page ? lowmem_page_address(page) + offset : NULL;
}

int hns_roce_table_get_range(struct hns_roce_dev *hr_dev,
			     struct hns_roce_hem_table *table,
			     unsigned long start, unsigned long end)
{
	unsigned long inc = HNS_ROCE_TABLE_CHUNK_SIZE / table->obj_size;
	unsigned long i = 0;
	int ret = 0;

	/* Allocate MTT entry memory according to chunk(128K) */
	for (i = start; i <= end; i += inc) {
		ret = hns_roce_table_get(hr_dev, table, i);
		if (ret)
			goto fail;
	}

	return 0;

fail:
	while (i > start) {
		i -= inc;
		hns_roce_table_put(hr_dev, table, i);
	}
	return ret;
}

void hns_roce_table_put_range(struct hns_roce_dev *hr_dev,
			      struct hns_roce_hem_table *table,
			      unsigned long start, unsigned long end)
{
	unsigned long i;

	for (i = start; i <= end;
		i += HNS_ROCE_TABLE_CHUNK_SIZE / table->obj_size)
		hns_roce_table_put(hr_dev, table, i);
}

int hns_roce_init_hem_table(struct hns_roce_dev *hr_dev,
			    struct hns_roce_hem_table *table, u32 type,
			    unsigned long obj_size, unsigned long nobj,
			    int use_lowmem)
{
	unsigned long obj_per_chunk;
	unsigned long num_hem;

	obj_per_chunk = HNS_ROCE_TABLE_CHUNK_SIZE / obj_size;
	num_hem = (nobj + obj_per_chunk - 1) / obj_per_chunk;

	table->hem = kcalloc(num_hem, sizeof(*table->hem), GFP_KERNEL);
	if (!table->hem)
		return -ENOMEM;

	table->type = type;
	table->num_hem = num_hem;
	table->num_obj = nobj;
	table->obj_size = obj_size;
	table->lowmem = use_lowmem;
	mutex_init(&table->mutex);

	return 0;
}

void hns_roce_cleanup_hem_table(struct hns_roce_dev *hr_dev,
				struct hns_roce_hem_table *table)
{
	struct device *dev = &hr_dev->pdev->dev;
	unsigned long i;

	for (i = 0; i < table->num_hem; ++i)
		if (table->hem[i]) {
			if (hr_dev->hw->clear_hem(hr_dev, table,
			    i * HNS_ROCE_TABLE_CHUNK_SIZE / table->obj_size))
				dev_err(dev, "Clear HEM base address failed.\n");

			hns_roce_free_hem(hr_dev, table->hem[i]);
		}

	kfree(table->hem);
}

void hns_roce_cleanup_hem(struct hns_roce_dev *hr_dev)
{
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->cq_table.table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->qp_table.irrl_table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->qp_table.qp_table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->mr_table.mtpt_table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->mr_table.mtt_table);
}
