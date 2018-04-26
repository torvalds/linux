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

#define DMA_ADDR_T_SHIFT		12
#define BT_BA_SHIFT			32

bool hns_roce_check_whether_mhop(struct hns_roce_dev *hr_dev, u32 type)
{
	if ((hr_dev->caps.qpc_hop_num && type == HEM_TYPE_QPC) ||
	    (hr_dev->caps.mpt_hop_num && type == HEM_TYPE_MTPT) ||
	    (hr_dev->caps.cqc_hop_num && type == HEM_TYPE_CQC) ||
	    (hr_dev->caps.srqc_hop_num && type == HEM_TYPE_SRQC) ||
	    (hr_dev->caps.cqe_hop_num && type == HEM_TYPE_CQE) ||
	    (hr_dev->caps.mtt_hop_num && type == HEM_TYPE_MTT))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(hns_roce_check_whether_mhop);

static bool hns_roce_check_hem_null(struct hns_roce_hem **hem, u64 start_idx,
			    u32 bt_chunk_num)
{
	int i;

	for (i = 0; i < bt_chunk_num; i++)
		if (hem[start_idx + i])
			return false;

	return true;
}

static bool hns_roce_check_bt_null(u64 **bt, u64 start_idx, u32 bt_chunk_num)
{
	int i;

	for (i = 0; i < bt_chunk_num; i++)
		if (bt[start_idx + i])
			return false;

	return true;
}

static int hns_roce_get_bt_num(u32 table_type, u32 hop_num)
{
	if (check_whether_bt_num_3(table_type, hop_num))
		return 3;
	else if (check_whether_bt_num_2(table_type, hop_num))
		return 2;
	else if (check_whether_bt_num_1(table_type, hop_num))
		return 1;
	else
		return 0;
}

int hns_roce_calc_hem_mhop(struct hns_roce_dev *hr_dev,
			   struct hns_roce_hem_table *table, unsigned long *obj,
			   struct hns_roce_hem_mhop *mhop)
{
	struct device *dev = hr_dev->dev;
	u32 chunk_ba_num;
	u32 table_idx;
	u32 bt_num;
	u32 chunk_size;

	switch (table->type) {
	case HEM_TYPE_QPC:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.qpc_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.qpc_ba_pg_sz
					     + PAGE_SHIFT);
		mhop->ba_l0_num = hr_dev->caps.qpc_bt_num;
		mhop->hop_num = hr_dev->caps.qpc_hop_num;
		break;
	case HEM_TYPE_MTPT:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.mpt_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.mpt_ba_pg_sz
					     + PAGE_SHIFT);
		mhop->ba_l0_num = hr_dev->caps.mpt_bt_num;
		mhop->hop_num = hr_dev->caps.mpt_hop_num;
		break;
	case HEM_TYPE_CQC:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.cqc_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.cqc_ba_pg_sz
					    + PAGE_SHIFT);
		mhop->ba_l0_num = hr_dev->caps.cqc_bt_num;
		mhop->hop_num = hr_dev->caps.cqc_hop_num;
		break;
	case HEM_TYPE_SRQC:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.srqc_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.srqc_ba_pg_sz
					     + PAGE_SHIFT);
		mhop->ba_l0_num = hr_dev->caps.srqc_bt_num;
		mhop->hop_num = hr_dev->caps.srqc_hop_num;
		break;
	case HEM_TYPE_MTT:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.mtt_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.mtt_ba_pg_sz
					     + PAGE_SHIFT);
		mhop->ba_l0_num = mhop->bt_chunk_size / 8;
		mhop->hop_num = hr_dev->caps.mtt_hop_num;
		break;
	case HEM_TYPE_CQE:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.cqe_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.cqe_ba_pg_sz
					     + PAGE_SHIFT);
		mhop->ba_l0_num = mhop->bt_chunk_size / 8;
		mhop->hop_num = hr_dev->caps.cqe_hop_num;
		break;
	default:
		dev_err(dev, "Table %d not support multi-hop addressing!\n",
			 table->type);
		return -EINVAL;
	}

	if (!obj)
		return 0;

	/*
	 * QPC/MTPT/CQC/SRQC alloc hem for buffer pages.
	 * MTT/CQE alloc hem for bt pages.
	 */
	bt_num = hns_roce_get_bt_num(table->type, mhop->hop_num);
	chunk_ba_num = mhop->bt_chunk_size / 8;
	chunk_size = table->type < HEM_TYPE_MTT ? mhop->buf_chunk_size :
			      mhop->bt_chunk_size;
	table_idx = (*obj & (table->num_obj - 1)) /
		     (chunk_size / table->obj_size);
	switch (bt_num) {
	case 3:
		mhop->l2_idx = table_idx & (chunk_ba_num - 1);
		mhop->l1_idx = table_idx / chunk_ba_num & (chunk_ba_num - 1);
		mhop->l0_idx = table_idx / chunk_ba_num / chunk_ba_num;
		break;
	case 2:
		mhop->l1_idx = table_idx & (chunk_ba_num - 1);
		mhop->l0_idx = table_idx / chunk_ba_num;
		break;
	case 1:
		mhop->l0_idx = table_idx;
		break;
	default:
		dev_err(dev, "Table %d not support hop_num = %d!\n",
			     table->type, mhop->hop_num);
		return -EINVAL;
	}
	if (mhop->l0_idx >= mhop->ba_l0_num)
		mhop->l0_idx %= mhop->ba_l0_num;

	return 0;
}
EXPORT_SYMBOL_GPL(hns_roce_calc_hem_mhop);

static struct hns_roce_hem *hns_roce_alloc_hem(struct hns_roce_dev *hr_dev,
					       int npages,
					       unsigned long hem_alloc_size,
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

	order = get_order(hem_alloc_size);

	while (npages > 0) {
		if (!chunk) {
			chunk = kmalloc(sizeof(*chunk),
				gfp_mask & ~(__GFP_HIGHMEM | __GFP_NOWARN));
			if (!chunk)
				goto fail;

			sg_init_table(chunk->mem, HNS_ROCE_HEM_CHUNK_LEN);
			chunk->npages = 0;
			chunk->nsg = 0;
			memset(chunk->buf, 0, sizeof(chunk->buf));
			list_add_tail(&chunk->list, &hem->chunk_list);
		}

		while (1 << order > npages)
			--order;

		/*
		 * Alloc memory one time. If failed, don't alloc small block
		 * memory, directly return fail.
		 */
		mem = &chunk->mem[chunk->npages];
		buf = dma_alloc_coherent(hr_dev->dev, PAGE_SIZE << order,
				&sg_dma_address(mem), gfp_mask);
		if (!buf)
			goto fail;

		chunk->buf[chunk->npages] = buf;
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
			dma_free_coherent(hr_dev->dev,
				   sg_dma_len(&chunk->mem[i]),
				   chunk->buf[i],
				   sg_dma_address(&chunk->mem[i]));
		kfree(chunk);
	}

	kfree(hem);
}

static int hns_roce_set_hem(struct hns_roce_dev *hr_dev,
			    struct hns_roce_hem_table *table, unsigned long obj)
{
	spinlock_t *lock = &hr_dev->bt_cmd_lock;
	struct device *dev = hr_dev->dev;
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
			  (table->table_chunk_size / table->obj_size);

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

static int hns_roce_table_mhop_get(struct hns_roce_dev *hr_dev,
				   struct hns_roce_hem_table *table,
				   unsigned long obj)
{
	struct device *dev = hr_dev->dev;
	struct hns_roce_hem_mhop mhop;
	struct hns_roce_hem_iter iter;
	u32 buf_chunk_size;
	u32 bt_chunk_size;
	u32 chunk_ba_num;
	u32 hop_num;
	u32 size;
	u32 bt_num;
	u64 hem_idx;
	u64 bt_l1_idx = 0;
	u64 bt_l0_idx = 0;
	u64 bt_ba;
	unsigned long mhop_obj = obj;
	int bt_l1_allocated = 0;
	int bt_l0_allocated = 0;
	int step_idx;
	int ret;

	ret = hns_roce_calc_hem_mhop(hr_dev, table, &mhop_obj, &mhop);
	if (ret)
		return ret;

	buf_chunk_size = mhop.buf_chunk_size;
	bt_chunk_size = mhop.bt_chunk_size;
	hop_num = mhop.hop_num;
	chunk_ba_num = bt_chunk_size / 8;

	bt_num = hns_roce_get_bt_num(table->type, hop_num);
	switch (bt_num) {
	case 3:
		hem_idx = mhop.l0_idx * chunk_ba_num * chunk_ba_num +
			  mhop.l1_idx * chunk_ba_num + mhop.l2_idx;
		bt_l1_idx = mhop.l0_idx * chunk_ba_num + mhop.l1_idx;
		bt_l0_idx = mhop.l0_idx;
		break;
	case 2:
		hem_idx = mhop.l0_idx * chunk_ba_num + mhop.l1_idx;
		bt_l0_idx = mhop.l0_idx;
		break;
	case 1:
		hem_idx = mhop.l0_idx;
		break;
	default:
		dev_err(dev, "Table %d not support hop_num = %d!\n",
			     table->type, hop_num);
		return -EINVAL;
	}

	mutex_lock(&table->mutex);

	if (table->hem[hem_idx]) {
		++table->hem[hem_idx]->refcount;
		goto out;
	}

	/* alloc L1 BA's chunk */
	if ((check_whether_bt_num_3(table->type, hop_num) ||
		check_whether_bt_num_2(table->type, hop_num)) &&
		!table->bt_l0[bt_l0_idx]) {
		table->bt_l0[bt_l0_idx] = dma_alloc_coherent(dev, bt_chunk_size,
					    &(table->bt_l0_dma_addr[bt_l0_idx]),
					    GFP_KERNEL);
		if (!table->bt_l0[bt_l0_idx]) {
			ret = -ENOMEM;
			goto out;
		}
		bt_l0_allocated = 1;

		/* set base address to hardware */
		if (table->type < HEM_TYPE_MTT) {
			step_idx = 0;
			if (hr_dev->hw->set_hem(hr_dev, table, obj, step_idx)) {
				ret = -ENODEV;
				dev_err(dev, "set HEM base address to HW failed!\n");
				goto err_dma_alloc_l1;
			}
		}
	}

	/* alloc L2 BA's chunk */
	if (check_whether_bt_num_3(table->type, hop_num) &&
	    !table->bt_l1[bt_l1_idx])  {
		table->bt_l1[bt_l1_idx] = dma_alloc_coherent(dev, bt_chunk_size,
					    &(table->bt_l1_dma_addr[bt_l1_idx]),
					    GFP_KERNEL);
		if (!table->bt_l1[bt_l1_idx]) {
			ret = -ENOMEM;
			goto err_dma_alloc_l1;
		}
		bt_l1_allocated = 1;
		*(table->bt_l0[bt_l0_idx] + mhop.l1_idx) =
					       table->bt_l1_dma_addr[bt_l1_idx];

		/* set base address to hardware */
		step_idx = 1;
		if (hr_dev->hw->set_hem(hr_dev, table, obj, step_idx)) {
			ret = -ENODEV;
			dev_err(dev, "set HEM base address to HW failed!\n");
			goto err_alloc_hem_buf;
		}
	}

	/*
	 * alloc buffer space chunk for QPC/MTPT/CQC/SRQC.
	 * alloc bt space chunk for MTT/CQE.
	 */
	size = table->type < HEM_TYPE_MTT ? buf_chunk_size : bt_chunk_size;
	table->hem[hem_idx] = hns_roce_alloc_hem(hr_dev,
						size >> PAGE_SHIFT,
						size,
						(table->lowmem ? GFP_KERNEL :
						GFP_HIGHUSER) | __GFP_NOWARN);
	if (!table->hem[hem_idx]) {
		ret = -ENOMEM;
		goto err_alloc_hem_buf;
	}

	hns_roce_hem_first(table->hem[hem_idx], &iter);
	bt_ba = hns_roce_hem_addr(&iter);

	if (table->type < HEM_TYPE_MTT) {
		if (hop_num == 2) {
			*(table->bt_l1[bt_l1_idx] + mhop.l2_idx) = bt_ba;
			step_idx = 2;
		} else if (hop_num == 1) {
			*(table->bt_l0[bt_l0_idx] + mhop.l1_idx) = bt_ba;
			step_idx = 1;
		} else if (hop_num == HNS_ROCE_HOP_NUM_0) {
			step_idx = 0;
		}

		/* set HEM base address to hardware */
		if (hr_dev->hw->set_hem(hr_dev, table, obj, step_idx)) {
			ret = -ENODEV;
			dev_err(dev, "set HEM base address to HW failed!\n");
			goto err_alloc_hem_buf;
		}
	} else if (hop_num == 2) {
		*(table->bt_l0[bt_l0_idx] + mhop.l1_idx) = bt_ba;
	}

	++table->hem[hem_idx]->refcount;
	goto out;

err_alloc_hem_buf:
	if (bt_l1_allocated) {
		dma_free_coherent(dev, bt_chunk_size, table->bt_l1[bt_l1_idx],
				  table->bt_l1_dma_addr[bt_l1_idx]);
		table->bt_l1[bt_l1_idx] = NULL;
	}

err_dma_alloc_l1:
	if (bt_l0_allocated) {
		dma_free_coherent(dev, bt_chunk_size, table->bt_l0[bt_l0_idx],
				  table->bt_l0_dma_addr[bt_l0_idx]);
		table->bt_l0[bt_l0_idx] = NULL;
	}

out:
	mutex_unlock(&table->mutex);
	return ret;
}

int hns_roce_table_get(struct hns_roce_dev *hr_dev,
		       struct hns_roce_hem_table *table, unsigned long obj)
{
	struct device *dev = hr_dev->dev;
	int ret = 0;
	unsigned long i;

	if (hns_roce_check_whether_mhop(hr_dev, table->type))
		return hns_roce_table_mhop_get(hr_dev, table, obj);

	i = (obj & (table->num_obj - 1)) / (table->table_chunk_size /
	     table->obj_size);

	mutex_lock(&table->mutex);

	if (table->hem[i]) {
		++table->hem[i]->refcount;
		goto out;
	}

	table->hem[i] = hns_roce_alloc_hem(hr_dev,
				       table->table_chunk_size >> PAGE_SHIFT,
				       table->table_chunk_size,
				       (table->lowmem ? GFP_KERNEL :
					GFP_HIGHUSER) | __GFP_NOWARN);
	if (!table->hem[i]) {
		ret = -ENOMEM;
		goto out;
	}

	/* Set HEM base address(128K/page, pa) to Hardware */
	if (hns_roce_set_hem(hr_dev, table, obj)) {
		hns_roce_free_hem(hr_dev, table->hem[i]);
		table->hem[i] = NULL;
		ret = -ENODEV;
		dev_err(dev, "set HEM base address to HW failed.\n");
		goto out;
	}

	++table->hem[i]->refcount;
out:
	mutex_unlock(&table->mutex);
	return ret;
}

static void hns_roce_table_mhop_put(struct hns_roce_dev *hr_dev,
				    struct hns_roce_hem_table *table,
				    unsigned long obj,
				    int check_refcount)
{
	struct device *dev = hr_dev->dev;
	struct hns_roce_hem_mhop mhop;
	unsigned long mhop_obj = obj;
	u32 bt_chunk_size;
	u32 chunk_ba_num;
	u32 hop_num;
	u32 start_idx;
	u32 bt_num;
	u64 hem_idx;
	u64 bt_l1_idx = 0;
	int ret;

	ret = hns_roce_calc_hem_mhop(hr_dev, table, &mhop_obj, &mhop);
	if (ret)
		return;

	bt_chunk_size = mhop.bt_chunk_size;
	hop_num = mhop.hop_num;
	chunk_ba_num = bt_chunk_size / 8;

	bt_num = hns_roce_get_bt_num(table->type, hop_num);
	switch (bt_num) {
	case 3:
		hem_idx = mhop.l0_idx * chunk_ba_num * chunk_ba_num +
			  mhop.l1_idx * chunk_ba_num + mhop.l2_idx;
		bt_l1_idx = mhop.l0_idx * chunk_ba_num + mhop.l1_idx;
		break;
	case 2:
		hem_idx = mhop.l0_idx * chunk_ba_num + mhop.l1_idx;
		break;
	case 1:
		hem_idx = mhop.l0_idx;
		break;
	default:
		dev_err(dev, "Table %d not support hop_num = %d!\n",
			     table->type, hop_num);
		return;
	}

	mutex_lock(&table->mutex);

	if (check_refcount && (--table->hem[hem_idx]->refcount > 0)) {
		mutex_unlock(&table->mutex);
		return;
	}

	if (table->type < HEM_TYPE_MTT && hop_num == 1) {
		if (hr_dev->hw->clear_hem(hr_dev, table, obj, 1))
			dev_warn(dev, "Clear HEM base address failed.\n");
	} else if (table->type < HEM_TYPE_MTT && hop_num == 2) {
		if (hr_dev->hw->clear_hem(hr_dev, table, obj, 2))
			dev_warn(dev, "Clear HEM base address failed.\n");
	} else if (table->type < HEM_TYPE_MTT &&
		   hop_num == HNS_ROCE_HOP_NUM_0) {
		if (hr_dev->hw->clear_hem(hr_dev, table, obj, 0))
			dev_warn(dev, "Clear HEM base address failed.\n");
	}

	/*
	 * free buffer space chunk for QPC/MTPT/CQC/SRQC.
	 * free bt space chunk for MTT/CQE.
	 */
	hns_roce_free_hem(hr_dev, table->hem[hem_idx]);
	table->hem[hem_idx] = NULL;

	if (check_whether_bt_num_2(table->type, hop_num)) {
		start_idx = mhop.l0_idx * chunk_ba_num;
		if (hns_roce_check_hem_null(table->hem, start_idx,
					    chunk_ba_num)) {
			if (table->type < HEM_TYPE_MTT &&
			    hr_dev->hw->clear_hem(hr_dev, table, obj, 0))
				dev_warn(dev, "Clear HEM base address failed.\n");

			dma_free_coherent(dev, bt_chunk_size,
					  table->bt_l0[mhop.l0_idx],
					  table->bt_l0_dma_addr[mhop.l0_idx]);
			table->bt_l0[mhop.l0_idx] = NULL;
		}
	} else if (check_whether_bt_num_3(table->type, hop_num)) {
		start_idx = mhop.l0_idx * chunk_ba_num * chunk_ba_num +
			    mhop.l1_idx * chunk_ba_num;
		if (hns_roce_check_hem_null(table->hem, start_idx,
					    chunk_ba_num)) {
			if (hr_dev->hw->clear_hem(hr_dev, table, obj, 1))
				dev_warn(dev, "Clear HEM base address failed.\n");

			dma_free_coherent(dev, bt_chunk_size,
					  table->bt_l1[bt_l1_idx],
					  table->bt_l1_dma_addr[bt_l1_idx]);
			table->bt_l1[bt_l1_idx] = NULL;

			start_idx = mhop.l0_idx * chunk_ba_num;
			if (hns_roce_check_bt_null(table->bt_l1, start_idx,
						   chunk_ba_num)) {
				if (hr_dev->hw->clear_hem(hr_dev, table, obj,
							  0))
					dev_warn(dev, "Clear HEM base address failed.\n");

				dma_free_coherent(dev, bt_chunk_size,
					    table->bt_l0[mhop.l0_idx],
					    table->bt_l0_dma_addr[mhop.l0_idx]);
				table->bt_l0[mhop.l0_idx] = NULL;
			}
		}
	}

	mutex_unlock(&table->mutex);
}

void hns_roce_table_put(struct hns_roce_dev *hr_dev,
			struct hns_roce_hem_table *table, unsigned long obj)
{
	struct device *dev = hr_dev->dev;
	unsigned long i;

	if (hns_roce_check_whether_mhop(hr_dev, table->type)) {
		hns_roce_table_mhop_put(hr_dev, table, obj, 1);
		return;
	}

	i = (obj & (table->num_obj - 1)) /
	    (table->table_chunk_size / table->obj_size);

	mutex_lock(&table->mutex);

	if (--table->hem[i]->refcount == 0) {
		/* Clear HEM base address */
		if (hr_dev->hw->clear_hem(hr_dev, table, obj, 0))
			dev_warn(dev, "Clear HEM base address failed.\n");

		hns_roce_free_hem(hr_dev, table->hem[i]);
		table->hem[i] = NULL;
	}

	mutex_unlock(&table->mutex);
}

void *hns_roce_table_find(struct hns_roce_dev *hr_dev,
			  struct hns_roce_hem_table *table,
			  unsigned long obj, dma_addr_t *dma_handle)
{
	struct hns_roce_hem_chunk *chunk;
	struct hns_roce_hem_mhop mhop;
	struct hns_roce_hem *hem;
	void *addr = NULL;
	unsigned long mhop_obj = obj;
	unsigned long obj_per_chunk;
	unsigned long idx_offset;
	int offset, dma_offset;
	int length;
	int i, j;
	u32 hem_idx = 0;

	if (!table->lowmem)
		return NULL;

	mutex_lock(&table->mutex);

	if (!hns_roce_check_whether_mhop(hr_dev, table->type)) {
		obj_per_chunk = table->table_chunk_size / table->obj_size;
		hem = table->hem[(obj & (table->num_obj - 1)) / obj_per_chunk];
		idx_offset = (obj & (table->num_obj - 1)) % obj_per_chunk;
		dma_offset = offset = idx_offset * table->obj_size;
	} else {
		hns_roce_calc_hem_mhop(hr_dev, table, &mhop_obj, &mhop);
		/* mtt mhop */
		i = mhop.l0_idx;
		j = mhop.l1_idx;
		if (mhop.hop_num == 2)
			hem_idx = i * (mhop.bt_chunk_size / 8) + j;
		else if (mhop.hop_num == 1 ||
			 mhop.hop_num == HNS_ROCE_HOP_NUM_0)
			hem_idx = i;

		hem = table->hem[hem_idx];
		dma_offset = offset = (obj & (table->num_obj - 1)) *
				       table->obj_size % mhop.bt_chunk_size;
		if (mhop.hop_num == 2)
			dma_offset = offset = 0;
	}

	if (!hem)
		goto out;

	list_for_each_entry(chunk, &hem->chunk_list, list) {
		for (i = 0; i < chunk->npages; ++i) {
			length = sg_dma_len(&chunk->mem[i]);
			if (dma_handle && dma_offset >= 0) {
				if (length > (u32)dma_offset)
					*dma_handle = sg_dma_address(
						&chunk->mem[i]) + dma_offset;
				dma_offset -= length;
			}

			if (length > (u32)offset) {
				addr = chunk->buf[i] + offset;
				goto out;
			}
			offset -= length;
		}
	}

out:
	mutex_unlock(&table->mutex);
	return addr;
}
EXPORT_SYMBOL_GPL(hns_roce_table_find);

int hns_roce_table_get_range(struct hns_roce_dev *hr_dev,
			     struct hns_roce_hem_table *table,
			     unsigned long start, unsigned long end)
{
	struct hns_roce_hem_mhop mhop;
	unsigned long inc = table->table_chunk_size / table->obj_size;
	unsigned long i;
	int ret;

	if (hns_roce_check_whether_mhop(hr_dev, table->type)) {
		hns_roce_calc_hem_mhop(hr_dev, table, NULL, &mhop);
		inc = mhop.bt_chunk_size / table->obj_size;
	}

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
	struct hns_roce_hem_mhop mhop;
	unsigned long inc = table->table_chunk_size / table->obj_size;
	unsigned long i;

	if (hns_roce_check_whether_mhop(hr_dev, table->type)) {
		hns_roce_calc_hem_mhop(hr_dev, table, NULL, &mhop);
		inc = mhop.bt_chunk_size / table->obj_size;
	}

	for (i = start; i <= end; i += inc)
		hns_roce_table_put(hr_dev, table, i);
}

int hns_roce_init_hem_table(struct hns_roce_dev *hr_dev,
			    struct hns_roce_hem_table *table, u32 type,
			    unsigned long obj_size, unsigned long nobj,
			    int use_lowmem)
{
	struct device *dev = hr_dev->dev;
	unsigned long obj_per_chunk;
	unsigned long num_hem;

	if (!hns_roce_check_whether_mhop(hr_dev, type)) {
		table->table_chunk_size = hr_dev->caps.chunk_sz;
		obj_per_chunk = table->table_chunk_size / obj_size;
		num_hem = (nobj + obj_per_chunk - 1) / obj_per_chunk;

		table->hem = kcalloc(num_hem, sizeof(*table->hem), GFP_KERNEL);
		if (!table->hem)
			return -ENOMEM;
	} else {
		unsigned long buf_chunk_size;
		unsigned long bt_chunk_size;
		unsigned long bt_chunk_num;
		unsigned long num_bt_l0 = 0;
		u32 hop_num;

		switch (type) {
		case HEM_TYPE_QPC:
			buf_chunk_size = 1 << (hr_dev->caps.qpc_buf_pg_sz
					+ PAGE_SHIFT);
			bt_chunk_size = 1 << (hr_dev->caps.qpc_ba_pg_sz
					+ PAGE_SHIFT);
			num_bt_l0 = hr_dev->caps.qpc_bt_num;
			hop_num = hr_dev->caps.qpc_hop_num;
			break;
		case HEM_TYPE_MTPT:
			buf_chunk_size = 1 << (hr_dev->caps.mpt_buf_pg_sz
					+ PAGE_SHIFT);
			bt_chunk_size = 1 << (hr_dev->caps.mpt_ba_pg_sz
					+ PAGE_SHIFT);
			num_bt_l0 = hr_dev->caps.mpt_bt_num;
			hop_num = hr_dev->caps.mpt_hop_num;
			break;
		case HEM_TYPE_CQC:
			buf_chunk_size = 1 << (hr_dev->caps.cqc_buf_pg_sz
					+ PAGE_SHIFT);
			bt_chunk_size = 1 << (hr_dev->caps.cqc_ba_pg_sz
					+ PAGE_SHIFT);
			num_bt_l0 = hr_dev->caps.cqc_bt_num;
			hop_num = hr_dev->caps.cqc_hop_num;
			break;
		case HEM_TYPE_SRQC:
			buf_chunk_size = 1 << (hr_dev->caps.srqc_buf_pg_sz
					+ PAGE_SHIFT);
			bt_chunk_size = 1 << (hr_dev->caps.srqc_ba_pg_sz
					+ PAGE_SHIFT);
			num_bt_l0 = hr_dev->caps.srqc_bt_num;
			hop_num = hr_dev->caps.srqc_hop_num;
			break;
		case HEM_TYPE_MTT:
			buf_chunk_size = 1 << (hr_dev->caps.mtt_ba_pg_sz
					+ PAGE_SHIFT);
			bt_chunk_size = buf_chunk_size;
			hop_num = hr_dev->caps.mtt_hop_num;
			break;
		case HEM_TYPE_CQE:
			buf_chunk_size = 1 << (hr_dev->caps.cqe_ba_pg_sz
					+ PAGE_SHIFT);
			bt_chunk_size = buf_chunk_size;
			hop_num = hr_dev->caps.cqe_hop_num;
			break;
		default:
			dev_err(dev,
			  "Table %d not support to init hem table here!\n",
			  type);
			return -EINVAL;
		}
		obj_per_chunk = buf_chunk_size / obj_size;
		num_hem = (nobj + obj_per_chunk - 1) / obj_per_chunk;
		bt_chunk_num = bt_chunk_size / 8;
		if (type >= HEM_TYPE_MTT)
			num_bt_l0 = bt_chunk_num;

		table->hem = kcalloc(num_hem, sizeof(*table->hem),
					 GFP_KERNEL);
		if (!table->hem)
			goto err_kcalloc_hem_buf;

		if (check_whether_bt_num_3(type, hop_num)) {
			unsigned long num_bt_l1;

			num_bt_l1 = (num_hem + bt_chunk_num - 1) /
					     bt_chunk_num;
			table->bt_l1 = kcalloc(num_bt_l1,
					       sizeof(*table->bt_l1),
					       GFP_KERNEL);
			if (!table->bt_l1)
				goto err_kcalloc_bt_l1;

			table->bt_l1_dma_addr = kcalloc(num_bt_l1,
						 sizeof(*table->bt_l1_dma_addr),
						 GFP_KERNEL);

			if (!table->bt_l1_dma_addr)
				goto err_kcalloc_l1_dma;
		}

		if (check_whether_bt_num_2(type, hop_num) ||
			check_whether_bt_num_3(type, hop_num)) {
			table->bt_l0 = kcalloc(num_bt_l0, sizeof(*table->bt_l0),
					       GFP_KERNEL);
			if (!table->bt_l0)
				goto err_kcalloc_bt_l0;

			table->bt_l0_dma_addr = kcalloc(num_bt_l0,
						 sizeof(*table->bt_l0_dma_addr),
						 GFP_KERNEL);
			if (!table->bt_l0_dma_addr)
				goto err_kcalloc_l0_dma;
		}
	}

	table->type = type;
	table->num_hem = num_hem;
	table->num_obj = nobj;
	table->obj_size = obj_size;
	table->lowmem = use_lowmem;
	mutex_init(&table->mutex);

	return 0;

err_kcalloc_l0_dma:
	kfree(table->bt_l0);
	table->bt_l0 = NULL;

err_kcalloc_bt_l0:
	kfree(table->bt_l1_dma_addr);
	table->bt_l1_dma_addr = NULL;

err_kcalloc_l1_dma:
	kfree(table->bt_l1);
	table->bt_l1 = NULL;

err_kcalloc_bt_l1:
	kfree(table->hem);
	table->hem = NULL;

err_kcalloc_hem_buf:
	return -ENOMEM;
}

static void hns_roce_cleanup_mhop_hem_table(struct hns_roce_dev *hr_dev,
					    struct hns_roce_hem_table *table)
{
	struct hns_roce_hem_mhop mhop;
	u32 buf_chunk_size;
	int i;
	u64 obj;

	hns_roce_calc_hem_mhop(hr_dev, table, NULL, &mhop);
	buf_chunk_size = table->type < HEM_TYPE_MTT ? mhop.buf_chunk_size :
					mhop.bt_chunk_size;

	for (i = 0; i < table->num_hem; ++i) {
		obj = i * buf_chunk_size / table->obj_size;
		if (table->hem[i])
			hns_roce_table_mhop_put(hr_dev, table, obj, 0);
	}

	kfree(table->hem);
	table->hem = NULL;
	kfree(table->bt_l1);
	table->bt_l1 = NULL;
	kfree(table->bt_l1_dma_addr);
	table->bt_l1_dma_addr = NULL;
	kfree(table->bt_l0);
	table->bt_l0 = NULL;
	kfree(table->bt_l0_dma_addr);
	table->bt_l0_dma_addr = NULL;
}

void hns_roce_cleanup_hem_table(struct hns_roce_dev *hr_dev,
				struct hns_roce_hem_table *table)
{
	struct device *dev = hr_dev->dev;
	unsigned long i;

	if (hns_roce_check_whether_mhop(hr_dev, table->type)) {
		hns_roce_cleanup_mhop_hem_table(hr_dev, table);
		return;
	}

	for (i = 0; i < table->num_hem; ++i)
		if (table->hem[i]) {
			if (hr_dev->hw->clear_hem(hr_dev, table,
			    i * table->table_chunk_size / table->obj_size, 0))
				dev_err(dev, "Clear HEM base address failed.\n");

			hns_roce_free_hem(hr_dev, table->hem[i]);
		}

	kfree(table->hem);
}

void hns_roce_cleanup_hem(struct hns_roce_dev *hr_dev)
{
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->cq_table.table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->qp_table.irrl_table);
	if (hr_dev->caps.trrl_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->qp_table.trrl_table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->qp_table.qp_table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->mr_table.mtpt_table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->mr_table.mtt_table);
	if (hns_roce_check_whether_mhop(hr_dev, HEM_TYPE_CQE))
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->mr_table.mtt_cqe_table);
}
