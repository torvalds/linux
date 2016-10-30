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
#include <rdma/ib_umem.h>
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"

static u32 hw_index_to_key(unsigned long ind)
{
	return (u32)(ind >> 24) | (ind << 8);
}

static unsigned long key_to_hw_index(u32 key)
{
	return (key << 24) | (key >> 8);
}

static int hns_roce_sw2hw_mpt(struct hns_roce_dev *hr_dev,
			      struct hns_roce_cmd_mailbox *mailbox,
			      unsigned long mpt_index)
{
	return hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0, mpt_index, 0,
				 HNS_ROCE_CMD_SW2HW_MPT,
				 HNS_ROCE_CMD_TIME_CLASS_B);
}

static int hns_roce_hw2sw_mpt(struct hns_roce_dev *hr_dev,
			      struct hns_roce_cmd_mailbox *mailbox,
			      unsigned long mpt_index)
{
	return hns_roce_cmd_mbox(hr_dev, 0, mailbox ? mailbox->dma : 0,
				 mpt_index, !mailbox, HNS_ROCE_CMD_HW2SW_MPT,
				 HNS_ROCE_CMD_TIME_CLASS_B);
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
	return -1;

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

	buddy->bits = kzalloc((buddy->max_order + 1) * sizeof(long *),
			       GFP_KERNEL);
	buddy->num_free = kzalloc((buddy->max_order + 1) * sizeof(int *),
				   GFP_KERNEL);
	if (!buddy->bits || !buddy->num_free)
		goto err_out;

	for (i = 0; i <= buddy->max_order; ++i) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->bits[i] = kmalloc_array(s, sizeof(long), GFP_KERNEL);
		if (!buddy->bits[i])
			goto err_out_free;

		bitmap_zero(buddy->bits[i], 1 << (buddy->max_order - i));
	}

	set_bit(0, buddy->bits[buddy->max_order]);
	buddy->num_free[buddy->max_order] = 1;

	return 0;

err_out_free:
	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->bits[i]);

err_out:
	kfree(buddy->bits);
	kfree(buddy->num_free);
	return -ENOMEM;
}

static void hns_roce_buddy_cleanup(struct hns_roce_buddy *buddy)
{
	int i;

	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->bits[i]);

	kfree(buddy->bits);
	kfree(buddy->num_free);
}

static int hns_roce_alloc_mtt_range(struct hns_roce_dev *hr_dev, int order,
				    unsigned long *seg)
{
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;
	int ret = 0;

	ret = hns_roce_buddy_alloc(&mr_table->mtt_buddy, order, seg);
	if (ret == -1)
		return -1;

	if (hns_roce_table_get_range(hr_dev, &mr_table->mtt_table, *seg,
				     *seg + (1 << order) - 1)) {
		hns_roce_buddy_free(&mr_table->mtt_buddy, *seg, order);
		return -1;
	}

	return 0;
}

int hns_roce_mtt_init(struct hns_roce_dev *hr_dev, int npages, int page_shift,
		      struct hns_roce_mtt *mtt)
{
	int ret = 0;
	int i;

	/* Page num is zero, correspond to DMA memory register */
	if (!npages) {
		mtt->order = -1;
		mtt->page_shift = HNS_ROCE_HEM_PAGE_SHIFT;
		return 0;
	}

	/* Note: if page_shift is zero, FAST memory regsiter */
	mtt->page_shift = page_shift;

	/* Compute MTT entry necessary */
	for (mtt->order = 0, i = HNS_ROCE_MTT_ENTRY_PER_SEG; i < npages;
	     i <<= 1)
		++mtt->order;

	/* Allocate MTT entry */
	ret = hns_roce_alloc_mtt_range(hr_dev, mtt->order, &mtt->first_seg);
	if (ret == -1)
		return -ENOMEM;

	return 0;
}

void hns_roce_mtt_cleanup(struct hns_roce_dev *hr_dev, struct hns_roce_mtt *mtt)
{
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;

	if (mtt->order < 0)
		return;

	hns_roce_buddy_free(&mr_table->mtt_buddy, mtt->first_seg, mtt->order);
	hns_roce_table_put_range(hr_dev, &mr_table->mtt_table, mtt->first_seg,
				 mtt->first_seg + (1 << mtt->order) - 1);
}

static int hns_roce_mr_alloc(struct hns_roce_dev *hr_dev, u32 pd, u64 iova,
			     u64 size, u32 access, int npages,
			     struct hns_roce_mr *mr)
{
	unsigned long index = 0;
	int ret = 0;
	struct device *dev = &hr_dev->pdev->dev;

	/* Allocate a key for mr from mr_table */
	ret = hns_roce_bitmap_alloc(&hr_dev->mr_table.mtpt_bitmap, &index);
	if (ret == -1)
		return -ENOMEM;

	mr->iova = iova;			/* MR va starting addr */
	mr->size = size;			/* MR addr range */
	mr->pd = pd;				/* MR num */
	mr->access = access;			/* MR access permit */
	mr->enabled = 0;			/* MR active status */
	mr->key = hw_index_to_key(index);	/* MR key */

	if (size == ~0ull) {
		mr->type = MR_TYPE_DMA;
		mr->pbl_buf = NULL;
		mr->pbl_dma_addr = 0;
	} else {
		mr->type = MR_TYPE_MR;
		mr->pbl_buf = dma_alloc_coherent(dev, npages * 8,
						 &(mr->pbl_dma_addr),
						 GFP_KERNEL);
		if (!mr->pbl_buf)
			return -ENOMEM;
	}

	return 0;
}

static void hns_roce_mr_free(struct hns_roce_dev *hr_dev,
			     struct hns_roce_mr *mr)
{
	struct device *dev = &hr_dev->pdev->dev;
	int npages = 0;
	int ret;

	if (mr->enabled) {
		ret = hns_roce_hw2sw_mpt(hr_dev, NULL, key_to_hw_index(mr->key)
					 & (hr_dev->caps.num_mtpts - 1));
		if (ret)
			dev_warn(dev, "HW2SW_MPT failed (%d)\n", ret);
	}

	if (mr->size != ~0ULL) {
		npages = ib_umem_page_count(mr->umem);
		dma_free_coherent(dev, (unsigned int)(npages * 8), mr->pbl_buf,
				  mr->pbl_dma_addr);
	}

	hns_roce_bitmap_free(&hr_dev->mr_table.mtpt_bitmap,
			     key_to_hw_index(mr->key));
}

static int hns_roce_mr_enable(struct hns_roce_dev *hr_dev,
			      struct hns_roce_mr *mr)
{
	int ret;
	unsigned long mtpt_idx = key_to_hw_index(mr->key);
	struct device *dev = &hr_dev->pdev->dev;
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

	ret = hr_dev->hw->write_mtpt(mailbox->buf, mr, mtpt_idx);
	if (ret) {
		dev_err(dev, "Write mtpt fail!\n");
		goto err_page;
	}

	ret = hns_roce_sw2hw_mpt(hr_dev, mailbox,
				 mtpt_idx & (hr_dev->caps.num_mtpts - 1));
	if (ret) {
		dev_err(dev, "SW2HW_MPT failed (%d)\n", ret);
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
	u32 i = 0;
	__le64 *mtts = NULL;
	dma_addr_t dma_handle;
	u32 s = start_index * sizeof(u64);

	/* All MTTs must fit in the same page */
	if (start_index / (PAGE_SIZE / sizeof(u64)) !=
		(start_index + npages - 1) / (PAGE_SIZE / sizeof(u64)))
		return -EINVAL;

	if (start_index & (HNS_ROCE_MTT_ENTRY_PER_SEG - 1))
		return -EINVAL;

	mtts = hns_roce_table_find(&hr_dev->mr_table.mtt_table,
				mtt->first_seg + s / hr_dev->caps.mtt_entry_sz,
				&dma_handle);
	if (!mtts)
		return -ENOMEM;

	/* Save page addr, low 12 bits : 0 */
	for (i = 0; i < npages; ++i)
		mtts[i] = (cpu_to_le64(page_list[i])) >> PAGE_ADDR_SHIFT;

	return 0;
}

static int hns_roce_write_mtt(struct hns_roce_dev *hr_dev,
			      struct hns_roce_mtt *mtt, u32 start_index,
			      u32 npages, u64 *page_list)
{
	int chunk;
	int ret;

	if (mtt->order < 0)
		return -EINVAL;

	while (npages > 0) {
		chunk = min_t(int, PAGE_SIZE / sizeof(u64), npages);

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
	u32 i = 0;
	int ret = 0;
	u64 *page_list = NULL;

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
	int ret = 0;

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

	return 0;

err_buddy:
	hns_roce_bitmap_cleanup(&mr_table->mtpt_bitmap);
	return ret;
}

void hns_roce_cleanup_mr_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;

	hns_roce_buddy_cleanup(&mr_table->mtt_buddy);
	hns_roce_bitmap_cleanup(&mr_table->mtpt_bitmap);
}

struct ib_mr *hns_roce_get_dma_mr(struct ib_pd *pd, int acc)
{
	int ret = 0;
	struct hns_roce_mr *mr = NULL;

	mr = kmalloc(sizeof(*mr), GFP_KERNEL);
	if (mr == NULL)
		return  ERR_PTR(-ENOMEM);

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
	struct scatterlist *sg;
	int i, k, entry;
	int ret = 0;
	u64 *pages;
	u32 n;
	int len;

	pages = (u64 *) __get_free_page(GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	i = n = 0;

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		len = sg_dma_len(sg) >> mtt->page_shift;
		for (k = 0; k < len; ++k) {
			pages[i++] = sg_dma_address(sg) + umem->page_size * k;
			if (i == PAGE_SIZE / sizeof(u64)) {
				ret = hns_roce_write_mtt(hr_dev, mtt, n, i,
							 pages);
				if (ret)
					goto out;
				n += i;
				i = 0;
			}
		}
	}

	if (i)
		ret = hns_roce_write_mtt(hr_dev, mtt, n, i, pages);

out:
	free_page((unsigned long) pages);
	return ret;
}

static int hns_roce_ib_umem_write_mr(struct hns_roce_mr *mr,
				     struct ib_umem *umem)
{
	int i = 0;
	int entry;
	struct scatterlist *sg;

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		mr->pbl_buf[i] = ((u64)sg_dma_address(sg)) >> 12;
		i++;
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
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_mr *mr = NULL;
	int ret = 0;
	int n = 0;

	mr = kmalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->umem = ib_umem_get(pd->uobject->context, start, length,
			       access_flags, 0);
	if (IS_ERR(mr->umem)) {
		ret = PTR_ERR(mr->umem);
		goto err_free;
	}

	n = ib_umem_page_count(mr->umem);
	if (mr->umem->page_size != HNS_ROCE_HEM_PAGE_SIZE) {
		dev_err(dev, "Just support 4K page size but is 0x%x now!\n",
			mr->umem->page_size);
		ret = -EINVAL;
		goto err_umem;
	}

	if (n > HNS_ROCE_MAX_MTPT_PBL_NUM) {
		dev_err(dev, " MR len %lld err. MR is limited to 4G at most!\n",
			length);
		ret = -EINVAL;
		goto err_umem;
	}

	ret = hns_roce_mr_alloc(hr_dev, to_hr_pd(pd)->pdn, virt_addr, length,
				access_flags, n, mr);
	if (ret)
		goto err_umem;

	ret = hns_roce_ib_umem_write_mr(mr, mr->umem);
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

int hns_roce_dereg_mr(struct ib_mr *ibmr)
{
	struct hns_roce_mr *mr = to_hr_mr(ibmr);

	hns_roce_mr_free(to_hr_dev(ibmr->device), mr);
	if (mr->umem)
		ib_umem_release(mr->umem);

	kfree(mr);

	return 0;
}
