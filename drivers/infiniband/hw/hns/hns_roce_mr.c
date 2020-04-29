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
#include <linux/vmalloc.h>
#include <rdma/ib_umem.h>
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"

static u32 hw_index_to_key(unsigned long ind)
{
	return (u32)(ind >> 24) | (ind << 8);
}

unsigned long key_to_hw_index(u32 key)
{
	return (key << 24) | (key >> 8);
}

static int hns_roce_hw_create_mpt(struct hns_roce_dev *hr_dev,
				  struct hns_roce_cmd_mailbox *mailbox,
				  unsigned long mpt_index)
{
	return hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0, mpt_index, 0,
				 HNS_ROCE_CMD_CREATE_MPT,
				 HNS_ROCE_CMD_TIMEOUT_MSECS);
}

int hns_roce_hw_destroy_mpt(struct hns_roce_dev *hr_dev,
			    struct hns_roce_cmd_mailbox *mailbox,
			    unsigned long mpt_index)
{
	return hns_roce_cmd_mbox(hr_dev, 0, mailbox ? mailbox->dma : 0,
				 mpt_index, !mailbox, HNS_ROCE_CMD_DESTROY_MPT,
				 HNS_ROCE_CMD_TIMEOUT_MSECS);
}

static int hns_roce_buddy_alloc(struct hns_roce_buddy *buddy, int order,
				unsigned long *seg)
{
	int o;
	u32 m;

	spin_lock(&buddy->lock);

	for (o = order; o <= buddy->max_order; ++o) {
		if (buddy->num_free[o]) {
			m = 1 << (buddy->max_order - o);
			*seg = find_first_bit(buddy->bits[o], m);
			if (*seg < m)
				goto found;
		}
	}
	spin_unlock(&buddy->lock);
	return -EINVAL;

 found:
	clear_bit(*seg, buddy->bits[o]);
	--buddy->num_free[o];

	while (o > order) {
		--o;
		*seg <<= 1;
		set_bit(*seg ^ 1, buddy->bits[o]);
		++buddy->num_free[o];
	}

	spin_unlock(&buddy->lock);

	*seg <<= order;
	return 0;
}

static void hns_roce_buddy_free(struct hns_roce_buddy *buddy, unsigned long seg,
				int order)
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

static int hns_roce_buddy_init(struct hns_roce_buddy *buddy, int max_order)
{
	int i, s;

	buddy->max_order = max_order;
	spin_lock_init(&buddy->lock);
	buddy->bits = kcalloc(buddy->max_order + 1,
			      sizeof(*buddy->bits),
			      GFP_KERNEL);
	buddy->num_free = kcalloc(buddy->max_order + 1,
				  sizeof(*buddy->num_free),
				  GFP_KERNEL);
	if (!buddy->bits || !buddy->num_free)
		goto err_out;

	for (i = 0; i <= buddy->max_order; ++i) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->bits[i] = kcalloc(s, sizeof(long), GFP_KERNEL |
					 __GFP_NOWARN);
		if (!buddy->bits[i]) {
			buddy->bits[i] = vzalloc(array_size(s, sizeof(long)));
			if (!buddy->bits[i])
				goto err_out_free;
		}
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

static void hns_roce_buddy_cleanup(struct hns_roce_buddy *buddy)
{
	int i;

	for (i = 0; i <= buddy->max_order; ++i)
		kvfree(buddy->bits[i]);

	kfree(buddy->bits);
	kfree(buddy->num_free);
}

static int hns_roce_alloc_mtt_range(struct hns_roce_dev *hr_dev, int order,
				    unsigned long *seg, u32 mtt_type)
{
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;
	struct hns_roce_hem_table *table;
	struct hns_roce_buddy *buddy;
	int ret;

	switch (mtt_type) {
	case MTT_TYPE_WQE:
		buddy = &mr_table->mtt_buddy;
		table = &mr_table->mtt_table;
		break;
	case MTT_TYPE_CQE:
		buddy = &mr_table->mtt_cqe_buddy;
		table = &mr_table->mtt_cqe_table;
		break;
	case MTT_TYPE_SRQWQE:
		buddy = &mr_table->mtt_srqwqe_buddy;
		table = &mr_table->mtt_srqwqe_table;
		break;
	case MTT_TYPE_IDX:
		buddy = &mr_table->mtt_idx_buddy;
		table = &mr_table->mtt_idx_table;
		break;
	default:
		dev_err(hr_dev->dev, "Unsupport MTT table type: %d\n",
			mtt_type);
		return -EINVAL;
	}

	ret = hns_roce_buddy_alloc(buddy, order, seg);
	if (ret)
		return ret;

	ret = hns_roce_table_get_range(hr_dev, table, *seg,
				       *seg + (1 << order) - 1);
	if (ret) {
		hns_roce_buddy_free(buddy, *seg, order);
		return ret;
	}

	return 0;
}

int hns_roce_mtt_init(struct hns_roce_dev *hr_dev, int npages, int page_shift,
		      struct hns_roce_mtt *mtt)
{
	int ret;
	int i;

	/* Page num is zero, correspond to DMA memory register */
	if (!npages) {
		mtt->order = -1;
		mtt->page_shift = HNS_ROCE_HEM_PAGE_SHIFT;
		return 0;
	}

	/* Note: if page_shift is zero, FAST memory register */
	mtt->page_shift = page_shift;

	/* Compute MTT entry necessary */
	for (mtt->order = 0, i = HNS_ROCE_MTT_ENTRY_PER_SEG; i < npages;
	     i <<= 1)
		++mtt->order;

	/* Allocate MTT entry */
	ret = hns_roce_alloc_mtt_range(hr_dev, mtt->order, &mtt->first_seg,
				       mtt->mtt_type);
	if (ret)
		return -ENOMEM;

	return 0;
}

void hns_roce_mtt_cleanup(struct hns_roce_dev *hr_dev, struct hns_roce_mtt *mtt)
{
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;

	if (mtt->order < 0)
		return;

	switch (mtt->mtt_type) {
	case MTT_TYPE_WQE:
		hns_roce_buddy_free(&mr_table->mtt_buddy, mtt->first_seg,
				    mtt->order);
		hns_roce_table_put_range(hr_dev, &mr_table->mtt_table,
					mtt->first_seg,
					mtt->first_seg + (1 << mtt->order) - 1);
		break;
	case MTT_TYPE_CQE:
		hns_roce_buddy_free(&mr_table->mtt_cqe_buddy, mtt->first_seg,
				    mtt->order);
		hns_roce_table_put_range(hr_dev, &mr_table->mtt_cqe_table,
					mtt->first_seg,
					mtt->first_seg + (1 << mtt->order) - 1);
		break;
	case MTT_TYPE_SRQWQE:
		hns_roce_buddy_free(&mr_table->mtt_srqwqe_buddy, mtt->first_seg,
				    mtt->order);
		hns_roce_table_put_range(hr_dev, &mr_table->mtt_srqwqe_table,
					mtt->first_seg,
					mtt->first_seg + (1 << mtt->order) - 1);
		break;
	case MTT_TYPE_IDX:
		hns_roce_buddy_free(&mr_table->mtt_idx_buddy, mtt->first_seg,
				    mtt->order);
		hns_roce_table_put_range(hr_dev, &mr_table->mtt_idx_table,
					mtt->first_seg,
					mtt->first_seg + (1 << mtt->order) - 1);
		break;
	default:
		dev_err(hr_dev->dev,
			"Unsupport mtt type %d, clean mtt failed\n",
			mtt->mtt_type);
		break;
	}
}

static void hns_roce_loop_free(struct hns_roce_dev *hr_dev,
			       struct hns_roce_mr *mr, int err_loop_index,
			       int loop_i, int loop_j)
{
	struct device *dev = hr_dev->dev;
	u32 mhop_num;
	u32 pbl_bt_sz;
	u64 bt_idx;
	int i, j;

	pbl_bt_sz = 1 << (hr_dev->caps.pbl_ba_pg_sz + PAGE_SHIFT);
	mhop_num = hr_dev->caps.pbl_hop_num;

	i = loop_i;
	if (mhop_num == 3 && err_loop_index == 2) {
		for (; i >= 0; i--) {
			dma_free_coherent(dev, pbl_bt_sz, mr->pbl_bt_l1[i],
					  mr->pbl_l1_dma_addr[i]);

			for (j = 0; j < pbl_bt_sz / BA_BYTE_LEN; j++) {
				if (i == loop_i && j >= loop_j)
					break;

				bt_idx = i * pbl_bt_sz / BA_BYTE_LEN + j;
				dma_free_coherent(dev, pbl_bt_sz,
						  mr->pbl_bt_l2[bt_idx],
						  mr->pbl_l2_dma_addr[bt_idx]);
			}
		}
	} else if (mhop_num == 3 && err_loop_index == 1) {
		for (i -= 1; i >= 0; i--) {
			dma_free_coherent(dev, pbl_bt_sz, mr->pbl_bt_l1[i],
					  mr->pbl_l1_dma_addr[i]);

			for (j = 0; j < pbl_bt_sz / BA_BYTE_LEN; j++) {
				bt_idx = i * pbl_bt_sz / BA_BYTE_LEN + j;
				dma_free_coherent(dev, pbl_bt_sz,
						  mr->pbl_bt_l2[bt_idx],
						  mr->pbl_l2_dma_addr[bt_idx]);
			}
		}
	} else if (mhop_num == 2 && err_loop_index == 1) {
		for (i -= 1; i >= 0; i--)
			dma_free_coherent(dev, pbl_bt_sz, mr->pbl_bt_l1[i],
					  mr->pbl_l1_dma_addr[i]);
	} else {
		dev_warn(dev, "not support: mhop_num=%d, err_loop_index=%d.",
			 mhop_num, err_loop_index);
		return;
	}

	dma_free_coherent(dev, pbl_bt_sz, mr->pbl_bt_l0, mr->pbl_l0_dma_addr);
	mr->pbl_bt_l0 = NULL;
	mr->pbl_l0_dma_addr = 0;
}
static int pbl_1hop_alloc(struct hns_roce_dev *hr_dev, int npages,
			       struct hns_roce_mr *mr, u32 pbl_bt_sz)
{
	struct device *dev = hr_dev->dev;

	if (npages > pbl_bt_sz / 8) {
		dev_err(dev, "npages %d is larger than buf_pg_sz!",
			npages);
		return -EINVAL;
	}
	mr->pbl_buf = dma_alloc_coherent(dev, npages * 8,
					 &(mr->pbl_dma_addr),
					 GFP_KERNEL);
	if (!mr->pbl_buf)
		return -ENOMEM;

	mr->pbl_size = npages;
	mr->pbl_ba = mr->pbl_dma_addr;
	mr->pbl_hop_num = 1;
	mr->pbl_ba_pg_sz = hr_dev->caps.pbl_ba_pg_sz;
	mr->pbl_buf_pg_sz = hr_dev->caps.pbl_buf_pg_sz;
	return 0;

}


static int pbl_2hop_alloc(struct hns_roce_dev *hr_dev, int npages,
			       struct hns_roce_mr *mr, u32 pbl_bt_sz)
{
	struct device *dev = hr_dev->dev;
	int npages_allocated;
	u64 pbl_last_bt_num;
	u64 pbl_bt_cnt = 0;
	u64 size;
	int i;

	pbl_last_bt_num = (npages + pbl_bt_sz / 8 - 1) / (pbl_bt_sz / 8);

	/* alloc L1 BT */
	for (i = 0; i < pbl_bt_sz / 8; i++) {
		if (pbl_bt_cnt + 1 < pbl_last_bt_num) {
			size = pbl_bt_sz;
		} else {
			npages_allocated = i * (pbl_bt_sz / 8);
			size = (npages - npages_allocated) * 8;
		}
		mr->pbl_bt_l1[i] = dma_alloc_coherent(dev, size,
					    &(mr->pbl_l1_dma_addr[i]),
					    GFP_KERNEL);
		if (!mr->pbl_bt_l1[i]) {
			hns_roce_loop_free(hr_dev, mr, 1, i, 0);
			return -ENOMEM;
		}

		*(mr->pbl_bt_l0 + i) = mr->pbl_l1_dma_addr[i];

		pbl_bt_cnt++;
		if (pbl_bt_cnt >= pbl_last_bt_num)
			break;
	}

	mr->l0_chunk_last_num = i + 1;

	return 0;
}

static int pbl_3hop_alloc(struct hns_roce_dev *hr_dev, int npages,
			       struct hns_roce_mr *mr, u32 pbl_bt_sz)
{
	struct device *dev = hr_dev->dev;
	int mr_alloc_done = 0;
	int npages_allocated;
	u64 pbl_last_bt_num;
	u64 pbl_bt_cnt = 0;
	u64 bt_idx;
	u64 size;
	int i;
	int j = 0;

	pbl_last_bt_num = (npages + pbl_bt_sz / 8 - 1) / (pbl_bt_sz / 8);

	mr->pbl_l2_dma_addr = kcalloc(pbl_last_bt_num,
				      sizeof(*mr->pbl_l2_dma_addr),
				      GFP_KERNEL);
	if (!mr->pbl_l2_dma_addr)
		return -ENOMEM;

	mr->pbl_bt_l2 = kcalloc(pbl_last_bt_num,
				sizeof(*mr->pbl_bt_l2),
				GFP_KERNEL);
	if (!mr->pbl_bt_l2)
		goto err_kcalloc_bt_l2;

	/* alloc L1, L2 BT */
	for (i = 0; i < pbl_bt_sz / 8; i++) {
		mr->pbl_bt_l1[i] = dma_alloc_coherent(dev, pbl_bt_sz,
					    &(mr->pbl_l1_dma_addr[i]),
					    GFP_KERNEL);
		if (!mr->pbl_bt_l1[i]) {
			hns_roce_loop_free(hr_dev, mr, 1, i, 0);
			goto err_dma_alloc_l0;
		}

		*(mr->pbl_bt_l0 + i) = mr->pbl_l1_dma_addr[i];

		for (j = 0; j < pbl_bt_sz / 8; j++) {
			bt_idx = i * pbl_bt_sz / 8 + j;

			if (pbl_bt_cnt + 1 < pbl_last_bt_num) {
				size = pbl_bt_sz;
			} else {
				npages_allocated = bt_idx *
						   (pbl_bt_sz / 8);
				size = (npages - npages_allocated) * 8;
			}
			mr->pbl_bt_l2[bt_idx] = dma_alloc_coherent(
				      dev, size,
				      &(mr->pbl_l2_dma_addr[bt_idx]),
				      GFP_KERNEL);
			if (!mr->pbl_bt_l2[bt_idx]) {
				hns_roce_loop_free(hr_dev, mr, 2, i, j);
				goto err_dma_alloc_l0;
			}

			*(mr->pbl_bt_l1[i] + j) =
					mr->pbl_l2_dma_addr[bt_idx];

			pbl_bt_cnt++;
			if (pbl_bt_cnt >= pbl_last_bt_num) {
				mr_alloc_done = 1;
				break;
			}
		}

		if (mr_alloc_done)
			break;
	}

	mr->l0_chunk_last_num = i + 1;
	mr->l1_chunk_last_num = j + 1;


	return 0;

err_dma_alloc_l0:
	kfree(mr->pbl_bt_l2);
	mr->pbl_bt_l2 = NULL;

err_kcalloc_bt_l2:
	kfree(mr->pbl_l2_dma_addr);
	mr->pbl_l2_dma_addr = NULL;

	return -ENOMEM;
}


/* PBL multi hop addressing */
static int hns_roce_mhop_alloc(struct hns_roce_dev *hr_dev, int npages,
			       struct hns_roce_mr *mr)
{
	struct device *dev = hr_dev->dev;
	u32 pbl_bt_sz;
	u32 mhop_num;

	mhop_num = (mr->type == MR_TYPE_FRMR ? 1 : hr_dev->caps.pbl_hop_num);
	pbl_bt_sz = 1 << (hr_dev->caps.pbl_ba_pg_sz + PAGE_SHIFT);

	if (mhop_num == HNS_ROCE_HOP_NUM_0)
		return 0;

	if (mhop_num == 1)
		return pbl_1hop_alloc(hr_dev, npages, mr, pbl_bt_sz);

	mr->pbl_l1_dma_addr = kcalloc(pbl_bt_sz / 8,
				      sizeof(*mr->pbl_l1_dma_addr),
				      GFP_KERNEL);
	if (!mr->pbl_l1_dma_addr)
		return -ENOMEM;

	mr->pbl_bt_l1 = kcalloc(pbl_bt_sz / 8, sizeof(*mr->pbl_bt_l1),
				GFP_KERNEL);
	if (!mr->pbl_bt_l1)
		goto err_kcalloc_bt_l1;

	/* alloc L0 BT */
	mr->pbl_bt_l0 = dma_alloc_coherent(dev, pbl_bt_sz,
					   &(mr->pbl_l0_dma_addr),
					   GFP_KERNEL);
	if (!mr->pbl_bt_l0)
		goto err_kcalloc_l2_dma;

	if (mhop_num == 2) {
		if (pbl_2hop_alloc(hr_dev, npages, mr, pbl_bt_sz))
			goto err_kcalloc_l2_dma;
	}

	if (mhop_num == 3) {
		if (pbl_3hop_alloc(hr_dev, npages, mr, pbl_bt_sz))
			goto err_kcalloc_l2_dma;
	}


	mr->pbl_size = npages;
	mr->pbl_ba = mr->pbl_l0_dma_addr;
	mr->pbl_hop_num = hr_dev->caps.pbl_hop_num;
	mr->pbl_ba_pg_sz = hr_dev->caps.pbl_ba_pg_sz;
	mr->pbl_buf_pg_sz = hr_dev->caps.pbl_buf_pg_sz;

	return 0;

err_kcalloc_l2_dma:
	kfree(mr->pbl_bt_l1);
	mr->pbl_bt_l1 = NULL;

err_kcalloc_bt_l1:
	kfree(mr->pbl_l1_dma_addr);
	mr->pbl_l1_dma_addr = NULL;

	return -ENOMEM;
}

static int hns_roce_mr_alloc(struct hns_roce_dev *hr_dev, u32 pd, u64 iova,
			     u64 size, u32 access, int npages,
			     struct hns_roce_mr *mr)
{
	struct device *dev = hr_dev->dev;
	unsigned long index = 0;
	int ret;

	/* Allocate a key for mr from mr_table */
	ret = hns_roce_bitmap_alloc(&hr_dev->mr_table.mtpt_bitmap, &index);
	if (ret)
		return -ENOMEM;

	mr->iova = iova;			/* MR va starting addr */
	mr->size = size;			/* MR addr range */
	mr->pd = pd;				/* MR num */
	mr->access = access;			/* MR access permit */
	mr->enabled = 0;			/* MR active status */
	mr->key = hw_index_to_key(index);	/* MR key */

	if (size == ~0ull) {
		mr->pbl_buf = NULL;
		mr->pbl_dma_addr = 0;
		/* PBL multi-hop addressing parameters */
		mr->pbl_bt_l2 = NULL;
		mr->pbl_bt_l1 = NULL;
		mr->pbl_bt_l0 = NULL;
		mr->pbl_l2_dma_addr = NULL;
		mr->pbl_l1_dma_addr = NULL;
		mr->pbl_l0_dma_addr = 0;
	} else {
		if (!hr_dev->caps.pbl_hop_num) {
			mr->pbl_buf = dma_alloc_coherent(dev,
							 npages * BA_BYTE_LEN,
							 &(mr->pbl_dma_addr),
							 GFP_KERNEL);
			if (!mr->pbl_buf)
				return -ENOMEM;
		} else {
			ret = hns_roce_mhop_alloc(hr_dev, npages, mr);
		}
	}

	return ret;
}

static void hns_roce_mhop_free(struct hns_roce_dev *hr_dev,
			       struct hns_roce_mr *mr)
{
	struct device *dev = hr_dev->dev;
	int npages_allocated;
	int npages;
	int i, j;
	u32 pbl_bt_sz;
	u32 mhop_num;
	u64 bt_idx;

	npages = mr->pbl_size;
	pbl_bt_sz = 1 << (hr_dev->caps.pbl_ba_pg_sz + PAGE_SHIFT);
	mhop_num = (mr->type == MR_TYPE_FRMR) ? 1 : hr_dev->caps.pbl_hop_num;

	if (mhop_num == HNS_ROCE_HOP_NUM_0)
		return;

	if (mhop_num == 1) {
		dma_free_coherent(dev, (unsigned int)(npages * BA_BYTE_LEN),
				  mr->pbl_buf, mr->pbl_dma_addr);
		return;
	}

	dma_free_coherent(dev, pbl_bt_sz, mr->pbl_bt_l0,
			  mr->pbl_l0_dma_addr);

	if (mhop_num == 2) {
		for (i = 0; i < mr->l0_chunk_last_num; i++) {
			if (i == mr->l0_chunk_last_num - 1) {
				npages_allocated =
						i * (pbl_bt_sz / BA_BYTE_LEN);

				dma_free_coherent(dev,
				      (npages - npages_allocated) * BA_BYTE_LEN,
				       mr->pbl_bt_l1[i],
				       mr->pbl_l1_dma_addr[i]);

				break;
			}

			dma_free_coherent(dev, pbl_bt_sz, mr->pbl_bt_l1[i],
					  mr->pbl_l1_dma_addr[i]);
		}
	} else if (mhop_num == 3) {
		for (i = 0; i < mr->l0_chunk_last_num; i++) {
			dma_free_coherent(dev, pbl_bt_sz, mr->pbl_bt_l1[i],
					  mr->pbl_l1_dma_addr[i]);

			for (j = 0; j < pbl_bt_sz / BA_BYTE_LEN; j++) {
				bt_idx = i * (pbl_bt_sz / BA_BYTE_LEN) + j;

				if ((i == mr->l0_chunk_last_num - 1)
				    && j == mr->l1_chunk_last_num - 1) {
					npages_allocated = bt_idx *
						      (pbl_bt_sz / BA_BYTE_LEN);

					dma_free_coherent(dev,
					      (npages - npages_allocated) *
					      BA_BYTE_LEN,
					      mr->pbl_bt_l2[bt_idx],
					      mr->pbl_l2_dma_addr[bt_idx]);

					break;
				}

				dma_free_coherent(dev, pbl_bt_sz,
						mr->pbl_bt_l2[bt_idx],
						mr->pbl_l2_dma_addr[bt_idx]);
			}
		}
	}

	kfree(mr->pbl_bt_l1);
	kfree(mr->pbl_l1_dma_addr);
	mr->pbl_bt_l1 = NULL;
	mr->pbl_l1_dma_addr = NULL;
	if (mhop_num == 3) {
		kfree(mr->pbl_bt_l2);
		kfree(mr->pbl_l2_dma_addr);
		mr->pbl_bt_l2 = NULL;
		mr->pbl_l2_dma_addr = NULL;
	}
}

static void hns_roce_mr_free(struct hns_roce_dev *hr_dev,
			     struct hns_roce_mr *mr)
{
	struct device *dev = hr_dev->dev;
	int npages = 0;
	int ret;

	if (mr->enabled) {
		ret = hns_roce_hw_destroy_mpt(hr_dev, NULL,
					      key_to_hw_index(mr->key) &
					      (hr_dev->caps.num_mtpts - 1));
		if (ret)
			dev_warn(dev, "DESTROY_MPT failed (%d)\n", ret);
	}

	if (mr->size != ~0ULL) {
		if (mr->type == MR_TYPE_MR)
			npages = ib_umem_page_count(mr->umem);

		if (!hr_dev->caps.pbl_hop_num)
			dma_free_coherent(dev,
					  (unsigned int)(npages * BA_BYTE_LEN),
					  mr->pbl_buf, mr->pbl_dma_addr);
		else
			hns_roce_mhop_free(hr_dev, mr);
	}

	if (mr->enabled)
		hns_roce_table_put(hr_dev, &hr_dev->mr_table.mtpt_table,
				   key_to_hw_index(mr->key));

	hns_roce_bitmap_free(&hr_dev->mr_table.mtpt_bitmap,
			     key_to_hw_index(mr->key), BITMAP_NO_RR);
}

static int hns_roce_mr_enable(struct hns_roce_dev *hr_dev,
			      struct hns_roce_mr *mr)
{
	int ret;
	unsigned long mtpt_idx = key_to_hw_index(mr->key);
	struct device *dev = hr_dev->dev;
	struct hns_roce_cmd_mailbox *mailbox;
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;

	/* Prepare HEM entry memory */
	ret = hns_roce_table_get(hr_dev, &mr_table->mtpt_table, mtpt_idx);
	if (ret)
		return ret;

	/* Allocate mailbox memory */
	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox)) {
		ret = PTR_ERR(mailbox);
		goto err_table;
	}

	if (mr->type != MR_TYPE_FRMR)
		ret = hr_dev->hw->write_mtpt(mailbox->buf, mr, mtpt_idx);
	else
		ret = hr_dev->hw->frmr_write_mtpt(mailbox->buf, mr);
	if (ret) {
		dev_err(dev, "Write mtpt fail!\n");
		goto err_page;
	}

	ret = hns_roce_hw_create_mpt(hr_dev, mailbox,
				     mtpt_idx & (hr_dev->caps.num_mtpts - 1));
	if (ret) {
		dev_err(dev, "CREATE_MPT failed (%d)\n", ret);
		goto err_page;
	}

	mr->enabled = 1;
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return 0;

err_page:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

err_table:
	hns_roce_table_put(hr_dev, &mr_table->mtpt_table, mtpt_idx);
	return ret;
}

static int hns_roce_write_mtt_chunk(struct hns_roce_dev *hr_dev,
				    struct hns_roce_mtt *mtt, u32 start_index,
				    u32 npages, u64 *page_list)
{
	struct hns_roce_hem_table *table;
	dma_addr_t dma_handle;
	__le64 *mtts;
	u32 bt_page_size;
	u32 i;

	switch (mtt->mtt_type) {
	case MTT_TYPE_WQE:
		table = &hr_dev->mr_table.mtt_table;
		bt_page_size = 1 << (hr_dev->caps.mtt_ba_pg_sz + PAGE_SHIFT);
		break;
	case MTT_TYPE_CQE:
		table = &hr_dev->mr_table.mtt_cqe_table;
		bt_page_size = 1 << (hr_dev->caps.cqe_ba_pg_sz + PAGE_SHIFT);
		break;
	case MTT_TYPE_SRQWQE:
		table = &hr_dev->mr_table.mtt_srqwqe_table;
		bt_page_size = 1 << (hr_dev->caps.srqwqe_ba_pg_sz + PAGE_SHIFT);
		break;
	case MTT_TYPE_IDX:
		table = &hr_dev->mr_table.mtt_idx_table;
		bt_page_size = 1 << (hr_dev->caps.idx_ba_pg_sz + PAGE_SHIFT);
		break;
	default:
		return -EINVAL;
	}

	/* All MTTs must fit in the same page */
	if (start_index / (bt_page_size / sizeof(u64)) !=
		(start_index + npages - 1) / (bt_page_size / sizeof(u64)))
		return -EINVAL;

	if (start_index & (HNS_ROCE_MTT_ENTRY_PER_SEG - 1))
		return -EINVAL;

	mtts = hns_roce_table_find(hr_dev, table,
				mtt->first_seg +
				start_index / HNS_ROCE_MTT_ENTRY_PER_SEG,
				&dma_handle);
	if (!mtts)
		return -ENOMEM;

	/* Save page addr, low 12 bits : 0 */
	for (i = 0; i < npages; ++i) {
		if (!hr_dev->caps.mtt_hop_num)
			mtts[i] = cpu_to_le64(page_list[i] >> PAGE_ADDR_SHIFT);
		else
			mtts[i] = cpu_to_le64(page_list[i]);
	}

	return 0;
}

static int hns_roce_write_mtt(struct hns_roce_dev *hr_dev,
			      struct hns_roce_mtt *mtt, u32 start_index,
			      u32 npages, u64 *page_list)
{
	int chunk;
	int ret;
	u32 bt_page_size;

	if (mtt->order < 0)
		return -EINVAL;

	switch (mtt->mtt_type) {
	case MTT_TYPE_WQE:
		bt_page_size = 1 << (hr_dev->caps.mtt_ba_pg_sz + PAGE_SHIFT);
		break;
	case MTT_TYPE_CQE:
		bt_page_size = 1 << (hr_dev->caps.cqe_ba_pg_sz + PAGE_SHIFT);
		break;
	case MTT_TYPE_SRQWQE:
		bt_page_size = 1 << (hr_dev->caps.srqwqe_ba_pg_sz + PAGE_SHIFT);
		break;
	case MTT_TYPE_IDX:
		bt_page_size = 1 << (hr_dev->caps.idx_ba_pg_sz + PAGE_SHIFT);
		break;
	default:
		dev_err(hr_dev->dev,
			"Unsupport mtt type %d, write mtt failed\n",
			mtt->mtt_type);
		return -EINVAL;
	}

	while (npages > 0) {
		chunk = min_t(int, bt_page_size / sizeof(u64), npages);

		ret = hns_roce_write_mtt_chunk(hr_dev, mtt, start_index, chunk,
					       page_list);
		if (ret)
			return ret;

		npages -= chunk;
		start_index += chunk;
		page_list += chunk;
	}

	return 0;
}

int hns_roce_buf_write_mtt(struct hns_roce_dev *hr_dev,
			   struct hns_roce_mtt *mtt, struct hns_roce_buf *buf)
{
	u64 *page_list;
	int ret;
	u32 i;

	page_list = kmalloc_array(buf->npages, sizeof(*page_list), GFP_KERNEL);
	if (!page_list)
		return -ENOMEM;

	for (i = 0; i < buf->npages; ++i) {
		if (buf->nbufs == 1)
			page_list[i] = buf->direct.map + (i << buf->page_shift);
		else
			page_list[i] = buf->page_list[i].map;

	}
	ret = hns_roce_write_mtt(hr_dev, mtt, 0, buf->npages, page_list);

	kfree(page_list);

	return ret;
}

int hns_roce_init_mr_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;
	int ret;

	ret = hns_roce_bitmap_init(&mr_table->mtpt_bitmap,
				   hr_dev->caps.num_mtpts,
				   hr_dev->caps.num_mtpts - 1,
				   hr_dev->caps.reserved_mrws, 0);
	if (ret)
		return ret;

	ret = hns_roce_buddy_init(&mr_table->mtt_buddy,
				  ilog2(hr_dev->caps.num_mtt_segs));
	if (ret)
		goto err_buddy;

	if (hns_roce_check_whether_mhop(hr_dev, HEM_TYPE_CQE)) {
		ret = hns_roce_buddy_init(&mr_table->mtt_cqe_buddy,
					  ilog2(hr_dev->caps.num_cqe_segs));
		if (ret)
			goto err_buddy_cqe;
	}

	if (hr_dev->caps.num_srqwqe_segs) {
		ret = hns_roce_buddy_init(&mr_table->mtt_srqwqe_buddy,
					  ilog2(hr_dev->caps.num_srqwqe_segs));
		if (ret)
			goto err_buddy_srqwqe;
	}

	if (hr_dev->caps.num_idx_segs) {
		ret = hns_roce_buddy_init(&mr_table->mtt_idx_buddy,
					  ilog2(hr_dev->caps.num_idx_segs));
		if (ret)
			goto err_buddy_idx;
	}

	return 0;

err_buddy_idx:
	if (hr_dev->caps.num_srqwqe_segs)
		hns_roce_buddy_cleanup(&mr_table->mtt_srqwqe_buddy);

err_buddy_srqwqe:
	if (hns_roce_check_whether_mhop(hr_dev, HEM_TYPE_CQE))
		hns_roce_buddy_cleanup(&mr_table->mtt_cqe_buddy);

err_buddy_cqe:
	hns_roce_buddy_cleanup(&mr_table->mtt_buddy);

err_buddy:
	hns_roce_bitmap_cleanup(&mr_table->mtpt_bitmap);
	return ret;
}

void hns_roce_cleanup_mr_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;

	if (hr_dev->caps.num_idx_segs)
		hns_roce_buddy_cleanup(&mr_table->mtt_idx_buddy);
	if (hr_dev->caps.num_srqwqe_segs)
		hns_roce_buddy_cleanup(&mr_table->mtt_srqwqe_buddy);
	hns_roce_buddy_cleanup(&mr_table->mtt_buddy);
	if (hns_roce_check_whether_mhop(hr_dev, HEM_TYPE_CQE))
		hns_roce_buddy_cleanup(&mr_table->mtt_cqe_buddy);
	hns_roce_bitmap_cleanup(&mr_table->mtpt_bitmap);
}

struct ib_mr *hns_roce_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct hns_roce_mr *mr;
	int ret;

	mr = kmalloc(sizeof(*mr), GFP_KERNEL);
	if (mr == NULL)
		return  ERR_PTR(-ENOMEM);

	mr->type = MR_TYPE_DMA;

	/* Allocate memory region key */
	ret = hns_roce_mr_alloc(to_hr_dev(pd->device), to_hr_pd(pd)->pdn, 0,
				~0ULL, acc, 0, mr);
	if (ret)
		goto err_free;

	ret = hns_roce_mr_enable(to_hr_dev(pd->device), mr);
	if (ret)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->key;
	mr->umem = NULL;

	return &mr->ibmr;

err_mr:
	hns_roce_mr_free(to_hr_dev(pd->device), mr);

err_free:
	kfree(mr);
	return ERR_PTR(ret);
}

int hns_roce_ib_umem_write_mtt(struct hns_roce_dev *hr_dev,
			       struct hns_roce_mtt *mtt, struct ib_umem *umem)
{
	struct device *dev = hr_dev->dev;
	struct sg_dma_page_iter sg_iter;
	unsigned int order;
	int npage = 0;
	int ret = 0;
	int i;
	u64 page_addr;
	u64 *pages;
	u32 bt_page_size;
	u32 n;

	switch (mtt->mtt_type) {
	case MTT_TYPE_WQE:
		order = hr_dev->caps.mtt_ba_pg_sz;
		break;
	case MTT_TYPE_CQE:
		order = hr_dev->caps.cqe_ba_pg_sz;
		break;
	case MTT_TYPE_SRQWQE:
		order = hr_dev->caps.srqwqe_ba_pg_sz;
		break;
	case MTT_TYPE_IDX:
		order = hr_dev->caps.idx_ba_pg_sz;
		break;
	default:
		dev_err(dev, "Unsupport mtt type %d, write mtt failed\n",
			mtt->mtt_type);
		return -EINVAL;
	}

	bt_page_size = 1 << (order + PAGE_SHIFT);

	pages = (u64 *) __get_free_pages(GFP_KERNEL, order);
	if (!pages)
		return -ENOMEM;

	i = n = 0;

	for_each_sg_dma_page(umem->sg_head.sgl, &sg_iter, umem->nmap, 0) {
		page_addr = sg_page_iter_dma_address(&sg_iter);
		if (!(npage % (1 << (mtt->page_shift - PAGE_SHIFT)))) {
			if (page_addr & ((1 << mtt->page_shift) - 1)) {
				dev_err(dev,
					"page_addr is not page_shift %d alignment!\n",
					mtt->page_shift);
				ret = -EINVAL;
				goto out;
			}
			pages[i++] = page_addr;
		}
		npage++;
		if (i == bt_page_size / sizeof(u64)) {
			ret = hns_roce_write_mtt(hr_dev, mtt, n, i, pages);
			if (ret)
				goto out;
			n += i;
			i = 0;
		}
	}

	if (i)
		ret = hns_roce_write_mtt(hr_dev, mtt, n, i, pages);

out:
	free_pages((unsigned long) pages, order);
	return ret;
}

static int hns_roce_ib_umem_write_mr(struct hns_roce_dev *hr_dev,
				     struct hns_roce_mr *mr,
				     struct ib_umem *umem)
{
	struct sg_dma_page_iter sg_iter;
	int i = 0, j = 0;
	u64 page_addr;
	u32 pbl_bt_sz;

	if (hr_dev->caps.pbl_hop_num == HNS_ROCE_HOP_NUM_0)
		return 0;

	pbl_bt_sz = 1 << (hr_dev->caps.pbl_ba_pg_sz + PAGE_SHIFT);
	for_each_sg_dma_page(umem->sg_head.sgl, &sg_iter, umem->nmap, 0) {
		page_addr = sg_page_iter_dma_address(&sg_iter);
		if (!hr_dev->caps.pbl_hop_num) {
			/* for hip06, page addr is aligned to 4K */
			mr->pbl_buf[i++] = page_addr >> 12;
		} else if (hr_dev->caps.pbl_hop_num == 1) {
			mr->pbl_buf[i++] = page_addr;
		} else {
			if (hr_dev->caps.pbl_hop_num == 2)
				mr->pbl_bt_l1[i][j] = page_addr;
			else if (hr_dev->caps.pbl_hop_num == 3)
				mr->pbl_bt_l2[i][j] = page_addr;

			j++;
			if (j >= (pbl_bt_sz / BA_BYTE_LEN)) {
				i++;
				j = 0;
			}
		}
	}

	/* Memory barrier */
	mb();

	return 0;
}

struct ib_mr *hns_roce_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				   u64 virt_addr, int access_flags,
				   struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct device *dev = hr_dev->dev;
	struct hns_roce_mr *mr;
	int bt_size;
	int ret;
	int n;
	int i;

	mr = kmalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->umem = ib_umem_get(pd->device, start, length, access_flags);
	if (IS_ERR(mr->umem)) {
		ret = PTR_ERR(mr->umem);
		goto err_free;
	}

	n = ib_umem_page_count(mr->umem);

	if (!hr_dev->caps.pbl_hop_num) {
		if (n > HNS_ROCE_MAX_MTPT_PBL_NUM) {
			dev_err(dev,
			     " MR len %lld err. MR is limited to 4G at most!\n",
			     length);
			ret = -EINVAL;
			goto err_umem;
		}
	} else {
		u64 pbl_size = 1;

		bt_size = (1 << (hr_dev->caps.pbl_ba_pg_sz + PAGE_SHIFT)) /
			  BA_BYTE_LEN;
		for (i = 0; i < hr_dev->caps.pbl_hop_num; i++)
			pbl_size *= bt_size;
		if (n > pbl_size) {
			dev_err(dev,
			    " MR len %lld err. MR page num is limited to %lld!\n",
			    length, pbl_size);
			ret = -EINVAL;
			goto err_umem;
		}
	}

	mr->type = MR_TYPE_MR;

	ret = hns_roce_mr_alloc(hr_dev, to_hr_pd(pd)->pdn, virt_addr, length,
				access_flags, n, mr);
	if (ret)
		goto err_umem;

	ret = hns_roce_ib_umem_write_mr(hr_dev, mr, mr->umem);
	if (ret)
		goto err_mr;

	ret = hns_roce_mr_enable(hr_dev, mr);
	if (ret)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->key;

	return &mr->ibmr;

err_mr:
	hns_roce_mr_free(hr_dev, mr);

err_umem:
	ib_umem_release(mr->umem);

err_free:
	kfree(mr);
	return ERR_PTR(ret);
}

static int rereg_mr_trans(struct ib_mr *ibmr, int flags,
			  u64 start, u64 length,
			  u64 virt_addr, int mr_access_flags,
			  struct hns_roce_cmd_mailbox *mailbox,
			  u32 pdn, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmr->device);
	struct hns_roce_mr *mr = to_hr_mr(ibmr);
	struct device *dev = hr_dev->dev;
	int npages;
	int ret;

	if (mr->size != ~0ULL) {
		npages = ib_umem_page_count(mr->umem);

		if (hr_dev->caps.pbl_hop_num)
			hns_roce_mhop_free(hr_dev, mr);
		else
			dma_free_coherent(dev, npages * 8,
					  mr->pbl_buf, mr->pbl_dma_addr);
	}
	ib_umem_release(mr->umem);

	mr->umem = ib_umem_get(ibmr->device, start, length, mr_access_flags);
	if (IS_ERR(mr->umem)) {
		ret = PTR_ERR(mr->umem);
		mr->umem = NULL;
		return -ENOMEM;
	}
	npages = ib_umem_page_count(mr->umem);

	if (hr_dev->caps.pbl_hop_num) {
		ret = hns_roce_mhop_alloc(hr_dev, npages, mr);
		if (ret)
			goto release_umem;
	} else {
		mr->pbl_buf = dma_alloc_coherent(dev, npages * 8,
						 &(mr->pbl_dma_addr),
						 GFP_KERNEL);
		if (!mr->pbl_buf) {
			ret = -ENOMEM;
			goto release_umem;
		}
	}

	ret = hr_dev->hw->rereg_write_mtpt(hr_dev, mr, flags, pdn,
					   mr_access_flags, virt_addr,
					   length, mailbox->buf);
	if (ret)
		goto release_umem;


	ret = hns_roce_ib_umem_write_mr(hr_dev, mr, mr->umem);
	if (ret) {
		if (mr->size != ~0ULL) {
			npages = ib_umem_page_count(mr->umem);

			if (hr_dev->caps.pbl_hop_num)
				hns_roce_mhop_free(hr_dev, mr);
			else
				dma_free_coherent(dev, npages * 8,
						  mr->pbl_buf,
						  mr->pbl_dma_addr);
		}

		goto release_umem;
	}

	return 0;

release_umem:
	ib_umem_release(mr->umem);
	return ret;

}


int hns_roce_rereg_user_mr(struct ib_mr *ibmr, int flags, u64 start, u64 length,
			   u64 virt_addr, int mr_access_flags, struct ib_pd *pd,
			   struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmr->device);
	struct hns_roce_mr *mr = to_hr_mr(ibmr);
	struct hns_roce_cmd_mailbox *mailbox;
	struct device *dev = hr_dev->dev;
	unsigned long mtpt_idx;
	u32 pdn = 0;
	int ret;

	if (!mr->enabled)
		return -EINVAL;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	mtpt_idx = key_to_hw_index(mr->key) & (hr_dev->caps.num_mtpts - 1);
	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, mtpt_idx, 0,
				HNS_ROCE_CMD_QUERY_MPT,
				HNS_ROCE_CMD_TIMEOUT_MSECS);
	if (ret)
		goto free_cmd_mbox;

	ret = hns_roce_hw_destroy_mpt(hr_dev, NULL, mtpt_idx);
	if (ret)
		dev_warn(dev, "DESTROY_MPT failed (%d)\n", ret);

	mr->enabled = 0;

	if (flags & IB_MR_REREG_PD)
		pdn = to_hr_pd(pd)->pdn;

	if (flags & IB_MR_REREG_TRANS) {
		ret = rereg_mr_trans(ibmr, flags,
				     start, length,
				     virt_addr, mr_access_flags,
				     mailbox, pdn, udata);
		if (ret)
			goto free_cmd_mbox;
	} else {
		ret = hr_dev->hw->rereg_write_mtpt(hr_dev, mr, flags, pdn,
						   mr_access_flags, virt_addr,
						   length, mailbox->buf);
		if (ret)
			goto free_cmd_mbox;
	}

	ret = hns_roce_hw_create_mpt(hr_dev, mailbox, mtpt_idx);
	if (ret) {
		dev_err(dev, "CREATE_MPT failed (%d)\n", ret);
		ib_umem_release(mr->umem);
		goto free_cmd_mbox;
	}

	mr->enabled = 1;
	if (flags & IB_MR_REREG_ACCESS)
		mr->access = mr_access_flags;

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return 0;

free_cmd_mbox:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return ret;
}

int hns_roce_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmr->device);
	struct hns_roce_mr *mr = to_hr_mr(ibmr);
	int ret = 0;

	if (hr_dev->hw->dereg_mr) {
		ret = hr_dev->hw->dereg_mr(hr_dev, mr, udata);
	} else {
		hns_roce_mr_free(hr_dev, mr);

		ib_umem_release(mr->umem);
		kfree(mr);
	}

	return ret;
}

struct ib_mr *hns_roce_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
				u32 max_num_sg, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct device *dev = hr_dev->dev;
	struct hns_roce_mr *mr;
	u64 length;
	u32 page_size;
	int ret;

	page_size = 1 << (hr_dev->caps.pbl_buf_pg_sz + PAGE_SHIFT);
	length = max_num_sg * page_size;

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EINVAL);

	if (max_num_sg > HNS_ROCE_FRMR_MAX_PA) {
		dev_err(dev, "max_num_sg larger than %d\n",
			HNS_ROCE_FRMR_MAX_PA);
		return ERR_PTR(-EINVAL);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->type = MR_TYPE_FRMR;

	/* Allocate memory region key */
	ret = hns_roce_mr_alloc(hr_dev, to_hr_pd(pd)->pdn, 0, length,
				0, max_num_sg, mr);
	if (ret)
		goto err_free;

	ret = hns_roce_mr_enable(hr_dev, mr);
	if (ret)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->key;
	mr->umem = NULL;

	return &mr->ibmr;

err_mr:
	hns_roce_mr_free(to_hr_dev(pd->device), mr);

err_free:
	kfree(mr);
	return ERR_PTR(ret);
}

static int hns_roce_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct hns_roce_mr *mr = to_hr_mr(ibmr);

	mr->pbl_buf[mr->npages++] = addr;

	return 0;
}

int hns_roce_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		       unsigned int *sg_offset)
{
	struct hns_roce_mr *mr = to_hr_mr(ibmr);

	mr->npages = 0;

	return ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, hns_roce_set_page);
}

static void hns_roce_mw_free(struct hns_roce_dev *hr_dev,
			     struct hns_roce_mw *mw)
{
	struct device *dev = hr_dev->dev;
	int ret;

	if (mw->enabled) {
		ret = hns_roce_hw_destroy_mpt(hr_dev, NULL,
					      key_to_hw_index(mw->rkey) &
					      (hr_dev->caps.num_mtpts - 1));
		if (ret)
			dev_warn(dev, "MW DESTROY_MPT failed (%d)\n", ret);

		hns_roce_table_put(hr_dev, &hr_dev->mr_table.mtpt_table,
				   key_to_hw_index(mw->rkey));
	}

	hns_roce_bitmap_free(&hr_dev->mr_table.mtpt_bitmap,
			     key_to_hw_index(mw->rkey), BITMAP_NO_RR);
}

static int hns_roce_mw_enable(struct hns_roce_dev *hr_dev,
			      struct hns_roce_mw *mw)
{
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;
	struct hns_roce_cmd_mailbox *mailbox;
	struct device *dev = hr_dev->dev;
	unsigned long mtpt_idx = key_to_hw_index(mw->rkey);
	int ret;

	/* prepare HEM entry memory */
	ret = hns_roce_table_get(hr_dev, &mr_table->mtpt_table, mtpt_idx);
	if (ret)
		return ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox)) {
		ret = PTR_ERR(mailbox);
		goto err_table;
	}

	ret = hr_dev->hw->mw_write_mtpt(mailbox->buf, mw);
	if (ret) {
		dev_err(dev, "MW write mtpt fail!\n");
		goto err_page;
	}

	ret = hns_roce_hw_create_mpt(hr_dev, mailbox,
				     mtpt_idx & (hr_dev->caps.num_mtpts - 1));
	if (ret) {
		dev_err(dev, "MW CREATE_MPT failed (%d)\n", ret);
		goto err_page;
	}

	mw->enabled = 1;

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return 0;

err_page:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

err_table:
	hns_roce_table_put(hr_dev, &mr_table->mtpt_table, mtpt_idx);

	return ret;
}

struct ib_mw *hns_roce_alloc_mw(struct ib_pd *ib_pd, enum ib_mw_type type,
				struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_pd->device);
	struct hns_roce_mw *mw;
	unsigned long index = 0;
	int ret;

	mw = kmalloc(sizeof(*mw), GFP_KERNEL);
	if (!mw)
		return ERR_PTR(-ENOMEM);

	/* Allocate a key for mw from bitmap */
	ret = hns_roce_bitmap_alloc(&hr_dev->mr_table.mtpt_bitmap, &index);
	if (ret)
		goto err_bitmap;

	mw->rkey = hw_index_to_key(index);

	mw->ibmw.rkey = mw->rkey;
	mw->ibmw.type = type;
	mw->pdn = to_hr_pd(ib_pd)->pdn;
	mw->pbl_hop_num = hr_dev->caps.pbl_hop_num;
	mw->pbl_ba_pg_sz = hr_dev->caps.pbl_ba_pg_sz;
	mw->pbl_buf_pg_sz = hr_dev->caps.pbl_buf_pg_sz;

	ret = hns_roce_mw_enable(hr_dev, mw);
	if (ret)
		goto err_mw;

	return &mw->ibmw;

err_mw:
	hns_roce_mw_free(hr_dev, mw);

err_bitmap:
	kfree(mw);

	return ERR_PTR(ret);
}

int hns_roce_dealloc_mw(struct ib_mw *ibmw)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmw->device);
	struct hns_roce_mw *mw = to_hr_mw(ibmw);

	hns_roce_mw_free(hr_dev, mw);
	kfree(mw);

	return 0;
}

void hns_roce_mtr_init(struct hns_roce_mtr *mtr, int bt_pg_shift,
		       int buf_pg_shift)
{
	hns_roce_hem_list_init(&mtr->hem_list, bt_pg_shift);
	mtr->buf_pg_shift = buf_pg_shift;
}

void hns_roce_mtr_cleanup(struct hns_roce_dev *hr_dev,
			  struct hns_roce_mtr *mtr)
{
	hns_roce_hem_list_release(hr_dev, &mtr->hem_list);
}

static int hns_roce_write_mtr(struct hns_roce_dev *hr_dev,
			      struct hns_roce_mtr *mtr, dma_addr_t *bufs,
			      struct hns_roce_buf_region *r)
{
	int offset;
	int count;
	int npage;
	u64 *mtts;
	int end;
	int i;

	offset = r->offset;
	end = offset + r->count;
	npage = 0;
	while (offset < end) {
		mtts = hns_roce_hem_list_find_mtt(hr_dev, &mtr->hem_list,
						  offset, &count, NULL);
		if (!mtts)
			return -ENOBUFS;

		/* Save page addr, low 12 bits : 0 */
		for (i = 0; i < count; i++) {
			if (hr_dev->hw_rev == HNS_ROCE_HW_VER1)
				mtts[i] = bufs[npage] >> PAGE_ADDR_SHIFT;
			else
				mtts[i] = bufs[npage];

			npage++;
		}
		offset += count;
	}

	return 0;
}

int hns_roce_mtr_attach(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
			dma_addr_t **bufs, struct hns_roce_buf_region *regions,
			int region_cnt)
{
	struct hns_roce_buf_region *r;
	int ret;
	int i;

	ret = hns_roce_hem_list_request(hr_dev, &mtr->hem_list, regions,
					region_cnt);
	if (ret)
		return ret;

	for (i = 0; i < region_cnt; i++) {
		r = &regions[i];
		ret = hns_roce_write_mtr(hr_dev, mtr, bufs[i], r);
		if (ret) {
			dev_err(hr_dev->dev,
				"write mtr[%d/%d] err %d,offset=%d.\n",
				i, region_cnt, ret,  r->offset);
			goto err_write;
		}
	}

	return 0;

err_write:
	hns_roce_hem_list_release(hr_dev, &mtr->hem_list);

	return ret;
}

int hns_roce_mtr_find(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
		      int offset, u64 *mtt_buf, int mtt_max, u64 *base_addr)
{
	u64 *mtts = mtt_buf;
	int mtt_count;
	int total = 0;
	u64 *addr;
	int npage;
	int left;

	if (mtts == NULL || mtt_max < 1)
		goto done;

	left = mtt_max;
	while (left > 0) {
		mtt_count = 0;
		addr = hns_roce_hem_list_find_mtt(hr_dev, &mtr->hem_list,
						  offset + total,
						  &mtt_count, NULL);
		if (!addr || !mtt_count)
			goto done;

		npage = min(mtt_count, left);
		memcpy(&mtts[total], addr, BA_BYTE_LEN * npage);
		left -= npage;
		total += npage;
	}

done:
	if (base_addr)
		*base_addr = mtr->hem_list.root_ba;

	return total;
}
