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

#define HEM_INDEX_BUF			BIT(0)
#define HEM_INDEX_L0			BIT(1)
#define HEM_INDEX_L1			BIT(2)
struct hns_roce_hem_index {
	u64 buf;
	u64 l0;
	u64 l1;
	u32 inited; /* indicate which index is available */
};

bool hns_roce_check_whether_mhop(struct hns_roce_dev *hr_dev, u32 type)
{
	int hop_num = 0;

	switch (type) {
	case HEM_TYPE_QPC:
		hop_num = hr_dev->caps.qpc_hop_num;
		break;
	case HEM_TYPE_MTPT:
		hop_num = hr_dev->caps.mpt_hop_num;
		break;
	case HEM_TYPE_CQC:
		hop_num = hr_dev->caps.cqc_hop_num;
		break;
	case HEM_TYPE_SRQC:
		hop_num = hr_dev->caps.srqc_hop_num;
		break;
	case HEM_TYPE_SCCC:
		hop_num = hr_dev->caps.sccc_hop_num;
		break;
	case HEM_TYPE_QPC_TIMER:
		hop_num = hr_dev->caps.qpc_timer_hop_num;
		break;
	case HEM_TYPE_CQC_TIMER:
		hop_num = hr_dev->caps.cqc_timer_hop_num;
		break;
	default:
		return false;
	}

	return hop_num ? true : false;
}

static bool hns_roce_check_hem_null(struct hns_roce_hem **hem, u64 hem_idx,
				    u32 bt_chunk_num, u64 hem_max_num)
{
	u64 start_idx = round_down(hem_idx, bt_chunk_num);
	u64 check_max_num = start_idx + bt_chunk_num;
	u64 i;

	for (i = start_idx; (i < check_max_num) && (i < hem_max_num); i++)
		if (i != hem_idx && hem[i])
			return false;

	return true;
}

static bool hns_roce_check_bt_null(u64 **bt, u64 ba_idx, u32 bt_chunk_num)
{
	u64 start_idx = round_down(ba_idx, bt_chunk_num);
	int i;

	for (i = 0; i < bt_chunk_num; i++)
		if (i != ba_idx && bt[start_idx + i])
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

static int get_hem_table_config(struct hns_roce_dev *hr_dev,
				struct hns_roce_hem_mhop *mhop,
				u32 type)
{
	struct device *dev = hr_dev->dev;

	switch (type) {
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
	case HEM_TYPE_SCCC:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.sccc_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.sccc_ba_pg_sz
					    + PAGE_SHIFT);
		mhop->ba_l0_num = hr_dev->caps.sccc_bt_num;
		mhop->hop_num = hr_dev->caps.sccc_hop_num;
		break;
	case HEM_TYPE_QPC_TIMER:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.qpc_timer_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.qpc_timer_ba_pg_sz
					    + PAGE_SHIFT);
		mhop->ba_l0_num = hr_dev->caps.qpc_timer_bt_num;
		mhop->hop_num = hr_dev->caps.qpc_timer_hop_num;
		break;
	case HEM_TYPE_CQC_TIMER:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.cqc_timer_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.cqc_timer_ba_pg_sz
					    + PAGE_SHIFT);
		mhop->ba_l0_num = hr_dev->caps.cqc_timer_bt_num;
		mhop->hop_num = hr_dev->caps.cqc_timer_hop_num;
		break;
	case HEM_TYPE_SRQC:
		mhop->buf_chunk_size = 1 << (hr_dev->caps.srqc_buf_pg_sz
					     + PAGE_SHIFT);
		mhop->bt_chunk_size = 1 << (hr_dev->caps.srqc_ba_pg_sz
					     + PAGE_SHIFT);
		mhop->ba_l0_num = hr_dev->caps.srqc_bt_num;
		mhop->hop_num = hr_dev->caps.srqc_hop_num;
		break;
	default:
		dev_err(dev, "Table %d not support multi-hop addressing!\n",
			type);
		return -EINVAL;
	}

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

	if (get_hem_table_config(hr_dev, mhop, table->type))
		return -EINVAL;

	if (!obj)
		return 0;

	/*
	 * QPC/MTPT/CQC/SRQC/SCCC alloc hem for buffer pages.
	 * MTT/CQE alloc hem for bt pages.
	 */
	bt_num = hns_roce_get_bt_num(table->type, mhop->hop_num);
	chunk_ba_num = mhop->bt_chunk_size / BA_BYTE_LEN;
	chunk_size = table->type < HEM_TYPE_MTT ? mhop->buf_chunk_size :
			      mhop->bt_chunk_size;
	table_idx = (*obj & (table->num_obj - 1)) /
		     (chunk_size / table->obj_size);
	switch (bt_num) {
	case 3:
		mhop->l2_idx = table_idx & (chunk_ba_num - 1);
		mhop->l1_idx = table_idx / chunk_ba_num & (chunk_ba_num - 1);
		mhop->l0_idx = (table_idx / chunk_ba_num) / chunk_ba_num;
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
	long end;
	unsigned long flags;
	struct hns_roce_hem_iter iter;
	void __iomem *bt_cmd;
	__le32 bt_cmd_val[2];
	__le32 bt_cmd_h = 0;
	__le32 bt_cmd_l = 0;
	u64 bt_ba = 0;
	int ret = 0;

	/* Find the HEM(Hardware Entry Memory) entry */
	unsigned long i = (obj & (table->num_obj - 1)) /
			  (table->table_chunk_size / table->obj_size);

	switch (table->type) {
	case HEM_TYPE_QPC:
	case HEM_TYPE_MTPT:
	case HEM_TYPE_CQC:
	case HEM_TYPE_SRQC:
		roce_set_field(bt_cmd_h, ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_M,
			ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S, table->type);
		break;
	default:
		return ret;
	}

	roce_set_field(bt_cmd_h, ROCEE_BT_CMD_H_ROCEE_BT_CMD_IN_MDF_M,
		       ROCEE_BT_CMD_H_ROCEE_BT_CMD_IN_MDF_S, obj);
	roce_set_bit(bt_cmd_h, ROCEE_BT_CMD_H_ROCEE_BT_CMD_S, 0);
	roce_set_bit(bt_cmd_h, ROCEE_BT_CMD_H_ROCEE_BT_CMD_HW_SYNS_S, 1);

	/* Currently iter only a chunk */
	for (hns_roce_hem_first(table->hem[i], &iter);
	     !hns_roce_hem_last(&iter); hns_roce_hem_next(&iter)) {
		bt_ba = hns_roce_hem_addr(&iter) >> DMA_ADDR_T_SHIFT;

		spin_lock_irqsave(lock, flags);

		bt_cmd = hr_dev->reg_base + ROCEE_BT_CMD_H_REG;

		end = HW_SYNC_TIMEOUT_MSECS;
		while (end > 0) {
			if (!(readl(bt_cmd) >> BT_CMD_SYNC_SHIFT))
				break;

			mdelay(HW_SYNC_SLEEP_TIME_INTERVAL);
			end -= HW_SYNC_SLEEP_TIME_INTERVAL;
		}

		if (end <= 0) {
			dev_err(dev, "Write bt_cmd err,hw_sync is not zero.\n");
			spin_unlock_irqrestore(lock, flags);
			return -EBUSY;
		}

		bt_cmd_l = cpu_to_le32(bt_ba);
		roce_set_field(bt_cmd_h, ROCEE_BT_CMD_H_ROCEE_BT_CMD_BA_H_M,
			       ROCEE_BT_CMD_H_ROCEE_BT_CMD_BA_H_S,
			       bt_ba >> BT_BA_SHIFT);

		bt_cmd_val[0] = bt_cmd_l;
		bt_cmd_val[1] = bt_cmd_h;
		hns_roce_write64_k(bt_cmd_val,
				   hr_dev->reg_base + ROCEE_BT_CMD_L_REG);
		spin_unlock_irqrestore(lock, flags);
	}

	return ret;
}

static int calc_hem_config(struct hns_roce_dev *hr_dev,
			   struct hns_roce_hem_table *table, unsigned long obj,
			   struct hns_roce_hem_mhop *mhop,
			   struct hns_roce_hem_index *index)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	unsigned long mhop_obj = obj;
	u32 l0_idx, l1_idx, l2_idx;
	u32 chunk_ba_num;
	u32 bt_num;
	int ret;

	ret = hns_roce_calc_hem_mhop(hr_dev, table, &mhop_obj, mhop);
	if (ret)
		return ret;

	l0_idx = mhop->l0_idx;
	l1_idx = mhop->l1_idx;
	l2_idx = mhop->l2_idx;
	chunk_ba_num = mhop->bt_chunk_size / BA_BYTE_LEN;
	bt_num = hns_roce_get_bt_num(table->type, mhop->hop_num);
	switch (bt_num) {
	case 3:
		index->l1 = l0_idx * chunk_ba_num + l1_idx;
		index->l0 = l0_idx;
		index->buf = l0_idx * chunk_ba_num * chunk_ba_num +
			     l1_idx * chunk_ba_num + l2_idx;
		break;
	case 2:
		index->l0 = l0_idx;
		index->buf = l0_idx * chunk_ba_num + l1_idx;
		break;
	case 1:
		index->buf = l0_idx;
		break;
	default:
		ibdev_err(ibdev, "Table %d not support mhop.hop_num = %d!\n",
			  table->type, mhop->hop_num);
		return -EINVAL;
	}

	if (unlikely(index->buf >= table->num_hem)) {
		ibdev_err(ibdev, "Table %d exceed hem limt idx %llu,max %lu!\n",
			  table->type, index->buf, table->num_hem);
		return -EINVAL;
	}

	return 0;
}

static void free_mhop_hem(struct hns_roce_dev *hr_dev,
			  struct hns_roce_hem_table *table,
			  struct hns_roce_hem_mhop *mhop,
			  struct hns_roce_hem_index *index)
{
	u32 bt_size = mhop->bt_chunk_size;
	struct device *dev = hr_dev->dev;

	if (index->inited & HEM_INDEX_BUF) {
		hns_roce_free_hem(hr_dev, table->hem[index->buf]);
		table->hem[index->buf] = NULL;
	}

	if (index->inited & HEM_INDEX_L1) {
		dma_free_coherent(dev, bt_size, table->bt_l1[index->l1],
				  table->bt_l1_dma_addr[index->l1]);
		table->bt_l1[index->l1] = NULL;
	}

	if (index->inited & HEM_INDEX_L0) {
		dma_free_coherent(dev, bt_size, table->bt_l0[index->l0],
				  table->bt_l0_dma_addr[index->l0]);
		table->bt_l0[index->l0] = NULL;
	}
}

static int alloc_mhop_hem(struct hns_roce_dev *hr_dev,
			  struct hns_roce_hem_table *table,
			  struct hns_roce_hem_mhop *mhop,
			  struct hns_roce_hem_index *index)
{
	u32 bt_size = mhop->bt_chunk_size;
	struct device *dev = hr_dev->dev;
	struct hns_roce_hem_iter iter;
	gfp_t flag;
	u64 bt_ba;
	u32 size;
	int ret;

	/* alloc L1 BA's chunk */
	if ((check_whether_bt_num_3(table->type, mhop->hop_num) ||
	     check_whether_bt_num_2(table->type, mhop->hop_num)) &&
	     !table->bt_l0[index->l0]) {
		table->bt_l0[index->l0] = dma_alloc_coherent(dev, bt_size,
					    &table->bt_l0_dma_addr[index->l0],
					    GFP_KERNEL);
		if (!table->bt_l0[index->l0]) {
			ret = -ENOMEM;
			goto out;
		}
		index->inited |= HEM_INDEX_L0;
	}

	/* alloc L2 BA's chunk */
	if (check_whether_bt_num_3(table->type, mhop->hop_num) &&
	    !table->bt_l1[index->l1])  {
		table->bt_l1[index->l1] = dma_alloc_coherent(dev, bt_size,
					    &table->bt_l1_dma_addr[index->l1],
					    GFP_KERNEL);
		if (!table->bt_l1[index->l1]) {
			ret = -ENOMEM;
			goto err_alloc_hem;
		}
		index->inited |= HEM_INDEX_L1;
		*(table->bt_l0[index->l0] + mhop->l1_idx) =
					       table->bt_l1_dma_addr[index->l1];
	}

	/*
	 * alloc buffer space chunk for QPC/MTPT/CQC/SRQC/SCCC.
	 * alloc bt space chunk for MTT/CQE.
	 */
	size = table->type < HEM_TYPE_MTT ? mhop->buf_chunk_size : bt_size;
	flag = (table->lowmem ? GFP_KERNEL : GFP_HIGHUSER) | __GFP_NOWARN;
	table->hem[index->buf] = hns_roce_alloc_hem(hr_dev, size >> PAGE_SHIFT,
						    size, flag);
	if (!table->hem[index->buf]) {
		ret = -ENOMEM;
		goto err_alloc_hem;
	}

	index->inited |= HEM_INDEX_BUF;
	hns_roce_hem_first(table->hem[index->buf], &iter);
	bt_ba = hns_roce_hem_addr(&iter);
	if (table->type < HEM_TYPE_MTT) {
		if (mhop->hop_num == 2)
			*(table->bt_l1[index->l1] + mhop->l2_idx) = bt_ba;
		else if (mhop->hop_num == 1)
			*(table->bt_l0[index->l0] + mhop->l1_idx) = bt_ba;
	} else if (mhop->hop_num == 2) {
		*(table->bt_l0[index->l0] + mhop->l1_idx) = bt_ba;
	}

	return 0;
err_alloc_hem:
	free_mhop_hem(hr_dev, table, mhop, index);
out:
	return ret;
}

static int set_mhop_hem(struct hns_roce_dev *hr_dev,
			struct hns_roce_hem_table *table, unsigned long obj,
			struct hns_roce_hem_mhop *mhop,
			struct hns_roce_hem_index *index)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int step_idx;
	int ret = 0;

	if (index->inited & HEM_INDEX_L0) {
		ret = hr_dev->hw->set_hem(hr_dev, table, obj, 0);
		if (ret) {
			ibdev_err(ibdev, "set HEM step 0 failed!\n");
			goto out;
		}
	}

	if (index->inited & HEM_INDEX_L1) {
		ret = hr_dev->hw->set_hem(hr_dev, table, obj, 1);
		if (ret) {
			ibdev_err(ibdev, "set HEM step 1 failed!\n");
			goto out;
		}
	}

	if (index->inited & HEM_INDEX_BUF) {
		if (mhop->hop_num == HNS_ROCE_HOP_NUM_0)
			step_idx = 0;
		else
			step_idx = mhop->hop_num;
		ret = hr_dev->hw->set_hem(hr_dev, table, obj, step_idx);
		if (ret)
			ibdev_err(ibdev, "set HEM step last failed!\n");
	}
out:
	return ret;
}

static int hns_roce_table_mhop_get(struct hns_roce_dev *hr_dev,
				   struct hns_roce_hem_table *table,
				   unsigned long obj)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_hem_index index = {};
	struct hns_roce_hem_mhop mhop = {};
	int ret;

	ret = calc_hem_config(hr_dev, table, obj, &mhop, &index);
	if (ret) {
		ibdev_err(ibdev, "calc hem config failed!\n");
		return ret;
	}

	mutex_lock(&table->mutex);
	if (table->hem[index.buf]) {
		++table->hem[index.buf]->refcount;
		goto out;
	}

	ret = alloc_mhop_hem(hr_dev, table, &mhop, &index);
	if (ret) {
		ibdev_err(ibdev, "alloc mhop hem failed!\n");
		goto out;
	}

	/* set HEM base address to hardware */
	if (table->type < HEM_TYPE_MTT) {
		ret = set_mhop_hem(hr_dev, table, obj, &mhop, &index);
		if (ret) {
			ibdev_err(ibdev, "set HEM address to HW failed!\n");
			goto err_alloc;
		}
	}

	++table->hem[index.buf]->refcount;
	goto out;

err_alloc:
	free_mhop_hem(hr_dev, table, &mhop, &index);
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

static void clear_mhop_hem(struct hns_roce_dev *hr_dev,
			   struct hns_roce_hem_table *table, unsigned long obj,
			   struct hns_roce_hem_mhop *mhop,
			   struct hns_roce_hem_index *index)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	u32 hop_num = mhop->hop_num;
	u32 chunk_ba_num;
	int step_idx;

	index->inited = HEM_INDEX_BUF;
	chunk_ba_num = mhop->bt_chunk_size / BA_BYTE_LEN;
	if (check_whether_bt_num_2(table->type, hop_num)) {
		if (hns_roce_check_hem_null(table->hem, index->buf,
					    chunk_ba_num, table->num_hem))
			index->inited |= HEM_INDEX_L0;
	} else if (check_whether_bt_num_3(table->type, hop_num)) {
		if (hns_roce_check_hem_null(table->hem, index->buf,
					    chunk_ba_num, table->num_hem)) {
			index->inited |= HEM_INDEX_L1;
			if (hns_roce_check_bt_null(table->bt_l1, index->l1,
						   chunk_ba_num))
				index->inited |= HEM_INDEX_L0;
		}
	}

	if (table->type < HEM_TYPE_MTT) {
		if (hop_num == HNS_ROCE_HOP_NUM_0)
			step_idx = 0;
		else
			step_idx = hop_num;

		if (hr_dev->hw->clear_hem(hr_dev, table, obj, step_idx))
			ibdev_warn(ibdev, "Clear hop%d HEM failed.\n", hop_num);

		if (index->inited & HEM_INDEX_L1)
			if (hr_dev->hw->clear_hem(hr_dev, table, obj, 1))
				ibdev_warn(ibdev, "Clear HEM step 1 failed.\n");

		if (index->inited & HEM_INDEX_L0)
			if (hr_dev->hw->clear_hem(hr_dev, table, obj, 0))
				ibdev_warn(ibdev, "Clear HEM step 0 failed.\n");
	}
}

static void hns_roce_table_mhop_put(struct hns_roce_dev *hr_dev,
				    struct hns_roce_hem_table *table,
				    unsigned long obj,
				    int check_refcount)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_hem_index index = {};
	struct hns_roce_hem_mhop mhop = {};
	int ret;

	ret = calc_hem_config(hr_dev, table, obj, &mhop, &index);
	if (ret) {
		ibdev_err(ibdev, "calc hem config failed!\n");
		return;
	}

	mutex_lock(&table->mutex);
	if (check_refcount && (--table->hem[index.buf]->refcount > 0)) {
		mutex_unlock(&table->mutex);
		return;
	}

	clear_mhop_hem(hr_dev, table, obj, &mhop, &index);
	free_mhop_hem(hr_dev, table, &mhop, &index);

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
		u32 seg_size = 64; /* 8 bytes per BA and 8 BA per segment */

		if (hns_roce_calc_hem_mhop(hr_dev, table, &mhop_obj, &mhop))
			goto out;
		/* mtt mhop */
		i = mhop.l0_idx;
		j = mhop.l1_idx;
		if (mhop.hop_num == 2)
			hem_idx = i * (mhop.bt_chunk_size / BA_BYTE_LEN) + j;
		else if (mhop.hop_num == 1 ||
			 mhop.hop_num == HNS_ROCE_HOP_NUM_0)
			hem_idx = i;

		hem = table->hem[hem_idx];
		dma_offset = offset = (obj & (table->num_obj - 1)) * seg_size %
				       mhop.bt_chunk_size;
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

int hns_roce_init_hem_table(struct hns_roce_dev *hr_dev,
			    struct hns_roce_hem_table *table, u32 type,
			    unsigned long obj_size, unsigned long nobj,
			    int use_lowmem)
{
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
		struct hns_roce_hem_mhop mhop = {};
		unsigned long buf_chunk_size;
		unsigned long bt_chunk_size;
		unsigned long bt_chunk_num;
		unsigned long num_bt_l0 = 0;
		u32 hop_num;

		if (get_hem_table_config(hr_dev, &mhop, type))
			return -EINVAL;

		buf_chunk_size = mhop.buf_chunk_size;
		bt_chunk_size = mhop.bt_chunk_size;
		num_bt_l0 = mhop.ba_l0_num;
		hop_num = mhop.hop_num;

		obj_per_chunk = buf_chunk_size / obj_size;
		num_hem = (nobj + obj_per_chunk - 1) / obj_per_chunk;
		bt_chunk_num = bt_chunk_size / BA_BYTE_LEN;
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

	if (hns_roce_calc_hem_mhop(hr_dev, table, NULL, &mhop))
		return;
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
	if (hr_dev->caps.srqc_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->srq_table.table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->cq_table.table);
	if (hr_dev->caps.qpc_timer_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->qpc_timer_table);
	if (hr_dev->caps.cqc_timer_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->cqc_timer_table);
	if (hr_dev->caps.sccc_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->qp_table.sccc_table);
	if (hr_dev->caps.trrl_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->qp_table.trrl_table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->qp_table.irrl_table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->qp_table.qp_table);
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->mr_table.mtpt_table);
}

struct roce_hem_item {
	struct list_head list; /* link all hems in the same bt level */
	struct list_head sibling; /* link all hems in last hop for mtt */
	void *addr;
	dma_addr_t dma_addr;
	size_t count; /* max ba numbers */
	int start; /* start buf offset in this hem */
	int end; /* end buf offset in this hem */
};

static struct roce_hem_item *hem_list_alloc_item(struct hns_roce_dev *hr_dev,
						   int start, int end,
						   int count, bool exist_bt,
						   int bt_level)
{
	struct roce_hem_item *hem;

	hem = kzalloc(sizeof(*hem), GFP_KERNEL);
	if (!hem)
		return NULL;

	if (exist_bt) {
		hem->addr = dma_alloc_coherent(hr_dev->dev,
						   count * BA_BYTE_LEN,
						   &hem->dma_addr, GFP_KERNEL);
		if (!hem->addr) {
			kfree(hem);
			return NULL;
		}
	}

	hem->count = count;
	hem->start = start;
	hem->end = end;
	INIT_LIST_HEAD(&hem->list);
	INIT_LIST_HEAD(&hem->sibling);

	return hem;
}

static void hem_list_free_item(struct hns_roce_dev *hr_dev,
			       struct roce_hem_item *hem, bool exist_bt)
{
	if (exist_bt)
		dma_free_coherent(hr_dev->dev, hem->count * BA_BYTE_LEN,
				  hem->addr, hem->dma_addr);
	kfree(hem);
}

static void hem_list_free_all(struct hns_roce_dev *hr_dev,
			      struct list_head *head, bool exist_bt)
{
	struct roce_hem_item *hem, *temp_hem;

	list_for_each_entry_safe(hem, temp_hem, head, list) {
		list_del(&hem->list);
		hem_list_free_item(hr_dev, hem, exist_bt);
	}
}

static void hem_list_link_bt(struct hns_roce_dev *hr_dev, void *base_addr,
			     u64 table_addr)
{
	*(u64 *)(base_addr) = table_addr;
}

/* assign L0 table address to hem from root bt */
static void hem_list_assign_bt(struct hns_roce_dev *hr_dev,
			       struct roce_hem_item *hem, void *cpu_addr,
			       u64 phy_addr)
{
	hem->addr = cpu_addr;
	hem->dma_addr = (dma_addr_t)phy_addr;
}

static inline bool hem_list_page_is_in_range(struct roce_hem_item *hem,
					     int offset)
{
	return (hem->start <= offset && offset <= hem->end);
}

static struct roce_hem_item *hem_list_search_item(struct list_head *ba_list,
						    int page_offset)
{
	struct roce_hem_item *hem, *temp_hem;
	struct roce_hem_item *found = NULL;

	list_for_each_entry_safe(hem, temp_hem, ba_list, list) {
		if (hem_list_page_is_in_range(hem, page_offset)) {
			found = hem;
			break;
		}
	}

	return found;
}

static bool hem_list_is_bottom_bt(int hopnum, int bt_level)
{
	/*
	 * hopnum    base address table levels
	 * 0		L0(buf)
	 * 1		L0 -> buf
	 * 2		L0 -> L1 -> buf
	 * 3		L0 -> L1 -> L2 -> buf
	 */
	return bt_level >= (hopnum ? hopnum - 1 : hopnum);
}

/**
 * calc base address entries num
 * @hopnum: num of mutihop addressing
 * @bt_level: base address table level
 * @unit: ba entries per bt page
 */
static u32 hem_list_calc_ba_range(int hopnum, int bt_level, int unit)
{
	u32 step;
	int max;
	int i;

	if (hopnum <= bt_level)
		return 0;
	/*
	 * hopnum  bt_level   range
	 * 1	      0       unit
	 * ------------
	 * 2	      0       unit * unit
	 * 2	      1       unit
	 * ------------
	 * 3	      0       unit * unit * unit
	 * 3	      1       unit * unit
	 * 3	      2       unit
	 */
	step = 1;
	max = hopnum - bt_level;
	for (i = 0; i < max; i++)
		step = step * unit;

	return step;
}

/**
 * calc the root ba entries which could cover all regions
 * @regions: buf region array
 * @region_cnt: array size of @regions
 * @unit: ba entries per bt page
 */
int hns_roce_hem_list_calc_root_ba(const struct hns_roce_buf_region *regions,
				   int region_cnt, int unit)
{
	struct hns_roce_buf_region *r;
	int total = 0;
	int step;
	int i;

	for (i = 0; i < region_cnt; i++) {
		r = (struct hns_roce_buf_region *)&regions[i];
		if (r->hopnum > 1) {
			step = hem_list_calc_ba_range(r->hopnum, 1, unit);
			if (step > 0)
				total += (r->count + step - 1) / step;
		} else {
			total += r->count;
		}
	}

	return total;
}

static int hem_list_alloc_mid_bt(struct hns_roce_dev *hr_dev,
				 const struct hns_roce_buf_region *r, int unit,
				 int offset, struct list_head *mid_bt,
				 struct list_head *btm_bt)
{
	struct roce_hem_item *hem_ptrs[HNS_ROCE_MAX_BT_LEVEL] = { NULL };
	struct list_head temp_list[HNS_ROCE_MAX_BT_LEVEL];
	struct roce_hem_item *cur, *pre;
	const int hopnum = r->hopnum;
	int start_aligned;
	int distance;
	int ret = 0;
	int max_ofs;
	int level;
	u32 step;
	int end;

	if (hopnum <= 1)
		return 0;

	if (hopnum > HNS_ROCE_MAX_BT_LEVEL) {
		dev_err(hr_dev->dev, "invalid hopnum %d!\n", hopnum);
		return -EINVAL;
	}

	if (offset < r->offset) {
		dev_err(hr_dev->dev, "invalid offset %d,min %d!\n",
			offset, r->offset);
		return -EINVAL;
	}

	distance = offset - r->offset;
	max_ofs = r->offset + r->count - 1;
	for (level = 0; level < hopnum; level++)
		INIT_LIST_HEAD(&temp_list[level]);

	/* config L1 bt to last bt and link them to corresponding parent */
	for (level = 1; level < hopnum; level++) {
		cur = hem_list_search_item(&mid_bt[level], offset);
		if (cur) {
			hem_ptrs[level] = cur;
			continue;
		}

		step = hem_list_calc_ba_range(hopnum, level, unit);
		if (step < 1) {
			ret = -EINVAL;
			goto err_exit;
		}

		start_aligned = (distance / step) * step + r->offset;
		end = min_t(int, start_aligned + step - 1, max_ofs);
		cur = hem_list_alloc_item(hr_dev, start_aligned, end, unit,
					  true, level);
		if (!cur) {
			ret = -ENOMEM;
			goto err_exit;
		}
		hem_ptrs[level] = cur;
		list_add(&cur->list, &temp_list[level]);
		if (hem_list_is_bottom_bt(hopnum, level))
			list_add(&cur->sibling, &temp_list[0]);

		/* link bt to parent bt */
		if (level > 1) {
			pre = hem_ptrs[level - 1];
			step = (cur->start - pre->start) / step * BA_BYTE_LEN;
			hem_list_link_bt(hr_dev, pre->addr + step,
					 cur->dma_addr);
		}
	}

	list_splice(&temp_list[0], btm_bt);
	for (level = 1; level < hopnum; level++)
		list_splice(&temp_list[level], &mid_bt[level]);

	return 0;

err_exit:
	for (level = 1; level < hopnum; level++)
		hem_list_free_all(hr_dev, &temp_list[level], true);

	return ret;
}

static int hem_list_alloc_root_bt(struct hns_roce_dev *hr_dev,
				  struct hns_roce_hem_list *hem_list, int unit,
				  const struct hns_roce_buf_region *regions,
				  int region_cnt)
{
	struct roce_hem_item *hem, *temp_hem, *root_hem;
	struct list_head temp_list[HNS_ROCE_MAX_BT_REGION];
	const struct hns_roce_buf_region *r;
	struct list_head temp_root;
	struct list_head temp_btm;
	void *cpu_base;
	u64 phy_base;
	int ret = 0;
	int ba_num;
	int offset;
	int total;
	int step;
	int i;

	r = &regions[0];
	root_hem = hem_list_search_item(&hem_list->root_bt, r->offset);
	if (root_hem)
		return 0;

	ba_num = hns_roce_hem_list_calc_root_ba(regions, region_cnt, unit);
	if (ba_num < 1)
		return -ENOMEM;

	INIT_LIST_HEAD(&temp_root);
	offset = r->offset;
	/* indicate to last region */
	r = &regions[region_cnt - 1];
	root_hem = hem_list_alloc_item(hr_dev, offset, r->offset + r->count - 1,
				       ba_num, true, 0);
	if (!root_hem)
		return -ENOMEM;
	list_add(&root_hem->list, &temp_root);

	hem_list->root_ba = root_hem->dma_addr;

	INIT_LIST_HEAD(&temp_btm);
	for (i = 0; i < region_cnt; i++)
		INIT_LIST_HEAD(&temp_list[i]);

	total = 0;
	for (i = 0; i < region_cnt && total < ba_num; i++) {
		r = &regions[i];
		if (!r->count)
			continue;

		/* all regions's mid[x][0] shared the root_bt's trunk */
		cpu_base = root_hem->addr + total * BA_BYTE_LEN;
		phy_base = root_hem->dma_addr + total * BA_BYTE_LEN;

		/* if hopnum is 0 or 1, cut a new fake hem from the root bt
		 * which's address share to all regions.
		 */
		if (hem_list_is_bottom_bt(r->hopnum, 0)) {
			hem = hem_list_alloc_item(hr_dev, r->offset,
						  r->offset + r->count - 1,
						  r->count, false, 0);
			if (!hem) {
				ret = -ENOMEM;
				goto err_exit;
			}
			hem_list_assign_bt(hr_dev, hem, cpu_base, phy_base);
			list_add(&hem->list, &temp_list[i]);
			list_add(&hem->sibling, &temp_btm);
			total += r->count;
		} else {
			step = hem_list_calc_ba_range(r->hopnum, 1, unit);
			if (step < 1) {
				ret = -EINVAL;
				goto err_exit;
			}
			/* if exist mid bt, link L1 to L0 */
			list_for_each_entry_safe(hem, temp_hem,
					  &hem_list->mid_bt[i][1], list) {
				offset = (hem->start - r->offset) / step *
					  BA_BYTE_LEN;
				hem_list_link_bt(hr_dev, cpu_base + offset,
						 hem->dma_addr);
				total++;
			}
		}
	}

	list_splice(&temp_btm, &hem_list->btm_bt);
	list_splice(&temp_root, &hem_list->root_bt);
	for (i = 0; i < region_cnt; i++)
		list_splice(&temp_list[i], &hem_list->mid_bt[i][0]);

	return 0;

err_exit:
	for (i = 0; i < region_cnt; i++)
		hem_list_free_all(hr_dev, &temp_list[i], false);

	hem_list_free_all(hr_dev, &temp_root, true);

	return ret;
}

/* construct the base address table and link them by address hop config */
int hns_roce_hem_list_request(struct hns_roce_dev *hr_dev,
			      struct hns_roce_hem_list *hem_list,
			      const struct hns_roce_buf_region *regions,
			      int region_cnt, unsigned int bt_pg_shift)
{
	const struct hns_roce_buf_region *r;
	int ofs, end;
	int ret = 0;
	int unit;
	int i;

	if (region_cnt > HNS_ROCE_MAX_BT_REGION) {
		dev_err(hr_dev->dev, "invalid region region_cnt %d!\n",
			region_cnt);
		return -EINVAL;
	}

	unit = (1 << bt_pg_shift) / BA_BYTE_LEN;
	for (i = 0; i < region_cnt; i++) {
		r = &regions[i];
		if (!r->count)
			continue;

		end = r->offset + r->count;
		for (ofs = r->offset; ofs < end; ofs += unit) {
			ret = hem_list_alloc_mid_bt(hr_dev, r, unit, ofs,
						    hem_list->mid_bt[i],
						    &hem_list->btm_bt);
			if (ret) {
				dev_err(hr_dev->dev,
					"alloc hem trunk fail ret=%d!\n", ret);
				goto err_alloc;
			}
		}
	}

	ret = hem_list_alloc_root_bt(hr_dev, hem_list, unit, regions,
				     region_cnt);
	if (ret)
		dev_err(hr_dev->dev, "alloc hem root fail ret=%d!\n", ret);
	else
		return 0;

err_alloc:
	hns_roce_hem_list_release(hr_dev, hem_list);

	return ret;
}

void hns_roce_hem_list_release(struct hns_roce_dev *hr_dev,
			       struct hns_roce_hem_list *hem_list)
{
	int i, j;

	for (i = 0; i < HNS_ROCE_MAX_BT_REGION; i++)
		for (j = 0; j < HNS_ROCE_MAX_BT_LEVEL; j++)
			hem_list_free_all(hr_dev, &hem_list->mid_bt[i][j],
					  j != 0);

	hem_list_free_all(hr_dev, &hem_list->root_bt, true);
	INIT_LIST_HEAD(&hem_list->btm_bt);
	hem_list->root_ba = 0;
}

void hns_roce_hem_list_init(struct hns_roce_hem_list *hem_list)
{
	int i, j;

	INIT_LIST_HEAD(&hem_list->root_bt);
	INIT_LIST_HEAD(&hem_list->btm_bt);
	for (i = 0; i < HNS_ROCE_MAX_BT_REGION; i++)
		for (j = 0; j < HNS_ROCE_MAX_BT_LEVEL; j++)
			INIT_LIST_HEAD(&hem_list->mid_bt[i][j]);
}

void *hns_roce_hem_list_find_mtt(struct hns_roce_dev *hr_dev,
				 struct hns_roce_hem_list *hem_list,
				 int offset, int *mtt_cnt, u64 *phy_addr)
{
	struct list_head *head = &hem_list->btm_bt;
	struct roce_hem_item *hem, *temp_hem;
	void *cpu_base = NULL;
	u64 phy_base = 0;
	int nr = 0;

	list_for_each_entry_safe(hem, temp_hem, head, sibling) {
		if (hem_list_page_is_in_range(hem, offset)) {
			nr = offset - hem->start;
			cpu_base = hem->addr + nr * BA_BYTE_LEN;
			phy_base = hem->dma_addr + nr * BA_BYTE_LEN;
			nr = hem->end + 1 - offset;
			break;
		}
	}

	if (mtt_cnt)
		*mtt_cnt = nr;

	if (phy_addr)
		*phy_addr = phy_base;

	return cpu_base;
}
