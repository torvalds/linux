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

#include <linux/vmalloc.h>
#include <linux/count_zeros.h>
#include <rdma/ib_umem.h>
#include <linux/math.h>
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"

static u32 hw_index_to_key(int ind)
{
	return ((u32)ind >> 24) | ((u32)ind << 8);
}

unsigned long key_to_hw_index(u32 key)
{
	return (key << 24) | (key >> 8);
}

static int alloc_mr_key(struct hns_roce_dev *hr_dev, struct hns_roce_mr *mr)
{
	struct hns_roce_ida *mtpt_ida = &hr_dev->mr_table.mtpt_ida;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int err;
	int id;

	/* Allocate a key for mr from mr_table */
	id = ida_alloc_range(&mtpt_ida->ida, mtpt_ida->min, mtpt_ida->max,
			     GFP_KERNEL);
	if (id < 0) {
		ibdev_err(ibdev, "failed to alloc id for MR key, id(%d)\n", id);
		return -ENOMEM;
	}

	mr->key = hw_index_to_key(id); /* MR key */

	err = hns_roce_table_get(hr_dev, &hr_dev->mr_table.mtpt_table,
				 (unsigned long)id);
	if (err) {
		ibdev_err(ibdev, "failed to alloc mtpt, ret = %d.\n", err);
		goto err_free_bitmap;
	}

	return 0;
err_free_bitmap:
	ida_free(&mtpt_ida->ida, id);
	return err;
}

static void free_mr_key(struct hns_roce_dev *hr_dev, struct hns_roce_mr *mr)
{
	unsigned long obj = key_to_hw_index(mr->key);

	hns_roce_table_put(hr_dev, &hr_dev->mr_table.mtpt_table, obj);
	ida_free(&hr_dev->mr_table.mtpt_ida.ida, (int)obj);
}

static int alloc_mr_pbl(struct hns_roce_dev *hr_dev, struct hns_roce_mr *mr,
			struct ib_udata *udata, u64 start)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	bool is_fast = mr->type == MR_TYPE_FRMR;
	struct hns_roce_buf_attr buf_attr = {};
	int err;

	mr->pbl_hop_num = is_fast ? 1 : hr_dev->caps.pbl_hop_num;
	buf_attr.page_shift = is_fast ? PAGE_SHIFT :
			      hr_dev->caps.pbl_buf_pg_sz + PAGE_SHIFT;
	buf_attr.region[0].size = mr->size;
	buf_attr.region[0].hopnum = mr->pbl_hop_num;
	buf_attr.region_count = 1;
	buf_attr.user_access = mr->access;
	/* fast MR's buffer is alloced before mapping, not at creation */
	buf_attr.mtt_only = is_fast;
	buf_attr.iova = mr->iova;
	/* pagesize and hopnum is fixed for fast MR */
	buf_attr.adaptive = !is_fast;
	buf_attr.type = MTR_PBL;

	err = hns_roce_mtr_create(hr_dev, &mr->pbl_mtr, &buf_attr,
				  hr_dev->caps.pbl_ba_pg_sz + PAGE_SHIFT,
				  udata, start);
	if (err) {
		ibdev_err(ibdev, "failed to alloc pbl mtr, ret = %d.\n", err);
		return err;
	}

	mr->npages = mr->pbl_mtr.hem_cfg.buf_pg_count;
	mr->pbl_hop_num = buf_attr.region[0].hopnum;

	return err;
}

static void free_mr_pbl(struct hns_roce_dev *hr_dev, struct hns_roce_mr *mr)
{
	hns_roce_mtr_destroy(hr_dev, &mr->pbl_mtr);
}

static void hns_roce_mr_free(struct hns_roce_dev *hr_dev, struct hns_roce_mr *mr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int ret;

	if (mr->enabled) {
		ret = hns_roce_destroy_hw_ctx(hr_dev, HNS_ROCE_CMD_DESTROY_MPT,
					      key_to_hw_index(mr->key) &
					      (hr_dev->caps.num_mtpts - 1));
		if (ret)
			ibdev_warn_ratelimited(ibdev, "failed to destroy mpt, ret = %d.\n",
					       ret);
	}

	free_mr_pbl(hr_dev, mr);
	free_mr_key(hr_dev, mr);
}

static int hns_roce_mr_enable(struct hns_roce_dev *hr_dev,
			      struct hns_roce_mr *mr)
{
	unsigned long mtpt_idx = key_to_hw_index(mr->key);
	struct hns_roce_cmd_mailbox *mailbox;
	struct device *dev = hr_dev->dev;
	int ret;

	/* Allocate mailbox memory */
	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	if (mr->type != MR_TYPE_FRMR)
		ret = hr_dev->hw->write_mtpt(hr_dev, mailbox->buf, mr);
	else
		ret = hr_dev->hw->frmr_write_mtpt(mailbox->buf, mr);
	if (ret) {
		dev_err(dev, "failed to write mtpt, ret = %d.\n", ret);
		goto err_page;
	}

	ret = hns_roce_create_hw_ctx(hr_dev, mailbox, HNS_ROCE_CMD_CREATE_MPT,
				     mtpt_idx & (hr_dev->caps.num_mtpts - 1));
	if (ret) {
		dev_err(dev, "failed to create mpt, ret = %d.\n", ret);
		goto err_page;
	}

	mr->enabled = 1;

err_page:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return ret;
}

void hns_roce_init_mr_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_ida *mtpt_ida = &hr_dev->mr_table.mtpt_ida;

	ida_init(&mtpt_ida->ida);
	mtpt_ida->max = hr_dev->caps.num_mtpts - 1;
	mtpt_ida->min = hr_dev->caps.reserved_mrws;
}

struct ib_mr *hns_roce_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct hns_roce_mr *mr;
	int ret;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return  ERR_PTR(-ENOMEM);

	mr->type = MR_TYPE_DMA;
	mr->pd = to_hr_pd(pd)->pdn;
	mr->access = acc;

	/* Allocate memory region key */
	hns_roce_hem_list_init(&mr->pbl_mtr.hem_list);
	ret = alloc_mr_key(hr_dev, mr);
	if (ret)
		goto err_free;

	ret = hns_roce_mr_enable(hr_dev, mr);
	if (ret)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->key;

	return &mr->ibmr;
err_mr:
	free_mr_key(hr_dev, mr);

err_free:
	kfree(mr);
	return ERR_PTR(ret);
}

struct ib_mr *hns_roce_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				   u64 virt_addr, int access_flags,
				   struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct hns_roce_mr *mr;
	int ret;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		ret = -ENOMEM;
		goto err_out;
	}

	mr->iova = virt_addr;
	mr->size = length;
	mr->pd = to_hr_pd(pd)->pdn;
	mr->access = access_flags;
	mr->type = MR_TYPE_MR;

	ret = alloc_mr_key(hr_dev, mr);
	if (ret)
		goto err_alloc_mr;

	ret = alloc_mr_pbl(hr_dev, mr, udata, start);
	if (ret)
		goto err_alloc_key;

	ret = hns_roce_mr_enable(hr_dev, mr);
	if (ret)
		goto err_alloc_pbl;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->key;

	return &mr->ibmr;

err_alloc_pbl:
	free_mr_pbl(hr_dev, mr);
err_alloc_key:
	free_mr_key(hr_dev, mr);
err_alloc_mr:
	kfree(mr);
err_out:
	atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_MR_REG_ERR_CNT]);

	return ERR_PTR(ret);
}

struct ib_mr *hns_roce_rereg_user_mr(struct ib_mr *ibmr, int flags, u64 start,
				     u64 length, u64 virt_addr,
				     int mr_access_flags, struct ib_pd *pd,
				     struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmr->device);
	struct ib_device *ib_dev = &hr_dev->ib_dev;
	struct hns_roce_mr *mr = to_hr_mr(ibmr);
	struct hns_roce_cmd_mailbox *mailbox;
	unsigned long mtpt_idx;
	int ret;

	if (!mr->enabled) {
		ret = -EINVAL;
		goto err_out;
	}

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	ret = PTR_ERR_OR_ZERO(mailbox);
	if (ret)
		goto err_out;

	mtpt_idx = key_to_hw_index(mr->key) & (hr_dev->caps.num_mtpts - 1);

	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, HNS_ROCE_CMD_QUERY_MPT,
				mtpt_idx);
	if (ret)
		goto free_cmd_mbox;

	ret = hns_roce_destroy_hw_ctx(hr_dev, HNS_ROCE_CMD_DESTROY_MPT,
				      mtpt_idx);
	if (ret)
		ibdev_warn(ib_dev, "failed to destroy MPT, ret = %d.\n", ret);

	mr->enabled = 0;
	mr->iova = virt_addr;
	mr->size = length;

	if (flags & IB_MR_REREG_PD)
		mr->pd = to_hr_pd(pd)->pdn;

	if (flags & IB_MR_REREG_ACCESS)
		mr->access = mr_access_flags;

	if (flags & IB_MR_REREG_TRANS) {
		free_mr_pbl(hr_dev, mr);
		ret = alloc_mr_pbl(hr_dev, mr, udata, start);
		if (ret) {
			ibdev_err(ib_dev, "failed to alloc mr PBL, ret = %d.\n",
				  ret);
			goto free_cmd_mbox;
		}
	}

	ret = hr_dev->hw->rereg_write_mtpt(hr_dev, mr, flags, mailbox->buf);
	if (ret) {
		ibdev_err(ib_dev, "failed to write mtpt, ret = %d.\n", ret);
		goto free_cmd_mbox;
	}

	ret = hns_roce_create_hw_ctx(hr_dev, mailbox, HNS_ROCE_CMD_CREATE_MPT,
				     mtpt_idx);
	if (ret) {
		ibdev_err(ib_dev, "failed to create MPT, ret = %d.\n", ret);
		goto free_cmd_mbox;
	}

	mr->enabled = 1;

free_cmd_mbox:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

err_out:
	if (ret) {
		atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_MR_REREG_ERR_CNT]);
		return ERR_PTR(ret);
	}

	return NULL;
}

int hns_roce_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmr->device);
	struct hns_roce_mr *mr = to_hr_mr(ibmr);

	if (hr_dev->hw->dereg_mr)
		hr_dev->hw->dereg_mr(hr_dev);

	hns_roce_mr_free(hr_dev, mr);
	kfree(mr);

	return 0;
}

struct ib_mr *hns_roce_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
				u32 max_num_sg)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct device *dev = hr_dev->dev;
	struct hns_roce_mr *mr;
	int ret;

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
	mr->pd = to_hr_pd(pd)->pdn;
	mr->size = max_num_sg * (1 << PAGE_SHIFT);

	/* Allocate memory region key */
	ret = alloc_mr_key(hr_dev, mr);
	if (ret)
		goto err_free;

	ret = alloc_mr_pbl(hr_dev, mr, NULL, 0);
	if (ret)
		goto err_key;

	ret = hns_roce_mr_enable(hr_dev, mr);
	if (ret)
		goto err_pbl;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->key;
	mr->ibmr.length = mr->size;

	return &mr->ibmr;

err_pbl:
	free_mr_pbl(hr_dev, mr);
err_key:
	free_mr_key(hr_dev, mr);
err_free:
	kfree(mr);
	return ERR_PTR(ret);
}

static int hns_roce_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct hns_roce_mr *mr = to_hr_mr(ibmr);

	if (likely(mr->npages < mr->pbl_mtr.hem_cfg.buf_pg_count)) {
		mr->page_list[mr->npages++] = addr;
		return 0;
	}

	return -ENOBUFS;
}

int hns_roce_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		       unsigned int *sg_offset)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmr->device);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_mr *mr = to_hr_mr(ibmr);
	struct hns_roce_mtr *mtr = &mr->pbl_mtr;
	int ret, sg_num = 0;

	if (!IS_ALIGNED(*sg_offset, HNS_ROCE_FRMR_ALIGN_SIZE) ||
	    ibmr->page_size < HNS_HW_PAGE_SIZE ||
	    ibmr->page_size > HNS_HW_MAX_PAGE_SIZE)
		return sg_num;

	mr->npages = 0;
	mr->page_list = kvcalloc(mr->pbl_mtr.hem_cfg.buf_pg_count,
				 sizeof(dma_addr_t), GFP_KERNEL);
	if (!mr->page_list)
		return sg_num;

	sg_num = ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, hns_roce_set_page);
	if (sg_num < 1) {
		ibdev_err(ibdev, "failed to store sg pages %u %u, cnt = %d.\n",
			  mr->npages, mr->pbl_mtr.hem_cfg.buf_pg_count, sg_num);
		goto err_page_list;
	}

	mtr->hem_cfg.region[0].offset = 0;
	mtr->hem_cfg.region[0].count = mr->npages;
	mtr->hem_cfg.region[0].hopnum = mr->pbl_hop_num;
	mtr->hem_cfg.region_count = 1;
	ret = hns_roce_mtr_map(hr_dev, mtr, mr->page_list, mr->npages);
	if (ret) {
		ibdev_err(ibdev, "failed to map sg mtr, ret = %d.\n", ret);
		sg_num = 0;
	} else {
		mr->pbl_mtr.hem_cfg.buf_pg_shift = (u32)ilog2(ibmr->page_size);
	}

err_page_list:
	kvfree(mr->page_list);
	mr->page_list = NULL;

	return sg_num;
}

static void hns_roce_mw_free(struct hns_roce_dev *hr_dev,
			     struct hns_roce_mw *mw)
{
	struct device *dev = hr_dev->dev;
	int ret;

	if (mw->enabled) {
		ret = hns_roce_destroy_hw_ctx(hr_dev, HNS_ROCE_CMD_DESTROY_MPT,
					      key_to_hw_index(mw->rkey) &
					      (hr_dev->caps.num_mtpts - 1));
		if (ret)
			dev_warn(dev, "MW DESTROY_MPT failed (%d)\n", ret);

		hns_roce_table_put(hr_dev, &hr_dev->mr_table.mtpt_table,
				   key_to_hw_index(mw->rkey));
	}

	ida_free(&hr_dev->mr_table.mtpt_ida.ida,
		 (int)key_to_hw_index(mw->rkey));
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

	ret = hns_roce_create_hw_ctx(hr_dev, mailbox, HNS_ROCE_CMD_CREATE_MPT,
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

int hns_roce_alloc_mw(struct ib_mw *ibmw, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmw->device);
	struct hns_roce_ida *mtpt_ida = &hr_dev->mr_table.mtpt_ida;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_mw *mw = to_hr_mw(ibmw);
	int ret;
	int id;

	/* Allocate a key for mw from mr_table */
	id = ida_alloc_range(&mtpt_ida->ida, mtpt_ida->min, mtpt_ida->max,
			     GFP_KERNEL);
	if (id < 0) {
		ibdev_err(ibdev, "failed to alloc id for MW key, id(%d)\n", id);
		return -ENOMEM;
	}

	mw->rkey = hw_index_to_key(id);

	ibmw->rkey = mw->rkey;
	mw->pdn = to_hr_pd(ibmw->pd)->pdn;
	mw->pbl_hop_num = hr_dev->caps.pbl_hop_num;
	mw->pbl_ba_pg_sz = hr_dev->caps.pbl_ba_pg_sz;
	mw->pbl_buf_pg_sz = hr_dev->caps.pbl_buf_pg_sz;

	ret = hns_roce_mw_enable(hr_dev, mw);
	if (ret)
		goto err_mw;

	return 0;

err_mw:
	hns_roce_mw_free(hr_dev, mw);
	return ret;
}

int hns_roce_dealloc_mw(struct ib_mw *ibmw)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmw->device);
	struct hns_roce_mw *mw = to_hr_mw(ibmw);

	hns_roce_mw_free(hr_dev, mw);
	return 0;
}

static int mtr_map_region(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
			  struct hns_roce_buf_region *region, dma_addr_t *pages,
			  int max_count)
{
	int count, npage;
	int offset, end;
	__le64 *mtts;
	u64 addr;
	int i;

	offset = region->offset;
	end = offset + region->count;
	npage = 0;
	while (offset < end && npage < max_count) {
		count = 0;
		mtts = hns_roce_hem_list_find_mtt(hr_dev, &mtr->hem_list,
						  offset, &count);
		if (!mtts)
			return -ENOBUFS;

		for (i = 0; i < count && npage < max_count; i++) {
			addr = pages[npage];

			mtts[i] = cpu_to_le64(addr);
			npage++;
		}
		offset += count;
	}

	return npage;
}

static inline bool mtr_has_mtt(struct hns_roce_buf_attr *attr)
{
	int i;

	for (i = 0; i < attr->region_count; i++)
		if (attr->region[i].hopnum != HNS_ROCE_HOP_NUM_0 &&
		    attr->region[i].hopnum > 0)
			return true;

	/* because the mtr only one root base address, when hopnum is 0 means
	 * root base address equals the first buffer address, thus all alloced
	 * memory must in a continuous space accessed by direct mode.
	 */
	return false;
}

static inline size_t mtr_bufs_size(struct hns_roce_buf_attr *attr)
{
	size_t size = 0;
	int i;

	for (i = 0; i < attr->region_count; i++)
		size += attr->region[i].size;

	return size;
}

/*
 * check the given pages in continuous address space
 * Returns 0 on success, or the error page num.
 */
static inline int mtr_check_direct_pages(dma_addr_t *pages, int page_count,
					 unsigned int page_shift)
{
	size_t page_size = 1 << page_shift;
	int i;

	for (i = 1; i < page_count; i++)
		if (pages[i] - pages[i - 1] != page_size)
			return i;

	return 0;
}

static void mtr_free_bufs(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr)
{
	/* release user buffers */
	if (mtr->umem) {
		ib_umem_release(mtr->umem);
		mtr->umem = NULL;
	}

	/* release kernel buffers */
	if (mtr->kmem) {
		hns_roce_buf_free(hr_dev, mtr->kmem);
		mtr->kmem = NULL;
	}
}

static int mtr_alloc_bufs(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
			  struct hns_roce_buf_attr *buf_attr,
			  struct ib_udata *udata, unsigned long user_addr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	size_t total_size;

	total_size = mtr_bufs_size(buf_attr);

	if (udata) {
		mtr->kmem = NULL;
		mtr->umem = ib_umem_get(ibdev, user_addr, total_size,
					buf_attr->user_access);
		if (IS_ERR(mtr->umem)) {
			ibdev_err(ibdev, "failed to get umem, ret = %ld.\n",
				  PTR_ERR(mtr->umem));
			return -ENOMEM;
		}
	} else {
		mtr->umem = NULL;
		mtr->kmem = hns_roce_buf_alloc(hr_dev, total_size,
					       buf_attr->page_shift,
					       !mtr_has_mtt(buf_attr) ?
					       HNS_ROCE_BUF_DIRECT : 0);
		if (IS_ERR(mtr->kmem)) {
			ibdev_err(ibdev, "failed to alloc kmem, ret = %ld.\n",
				  PTR_ERR(mtr->kmem));
			return PTR_ERR(mtr->kmem);
		}
	}

	return 0;
}

static int cal_mtr_pg_cnt(struct hns_roce_mtr *mtr)
{
	struct hns_roce_buf_region *region;
	int page_cnt = 0;
	int i;

	for (i = 0; i < mtr->hem_cfg.region_count; i++) {
		region = &mtr->hem_cfg.region[i];
		page_cnt += region->count;
	}

	return page_cnt;
}

static bool need_split_huge_page(struct hns_roce_mtr *mtr)
{
	/* When HEM buffer uses 0-level addressing, the page size is
	 * equal to the whole buffer size. If the current MTR has multiple
	 * regions, we split the buffer into small pages(4k, required by hns
	 * ROCEE). These pages will be used in multiple regions.
	 */
	return mtr->hem_cfg.is_direct && mtr->hem_cfg.region_count > 1;
}

static int mtr_map_bufs(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int page_count = cal_mtr_pg_cnt(mtr);
	unsigned int page_shift;
	dma_addr_t *pages;
	int npage;
	int ret;

	page_shift = need_split_huge_page(mtr) ? HNS_HW_PAGE_SHIFT :
						 mtr->hem_cfg.buf_pg_shift;
	/* alloc a tmp array to store buffer's dma address */
	pages = kvcalloc(page_count, sizeof(dma_addr_t), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	if (mtr->umem)
		npage = hns_roce_get_umem_bufs(pages, page_count,
					       mtr->umem, page_shift);
	else
		npage = hns_roce_get_kmem_bufs(hr_dev, pages, page_count,
					       mtr->kmem, page_shift);

	if (npage != page_count) {
		ibdev_err(ibdev, "failed to get mtr page %d != %d.\n", npage,
			  page_count);
		ret = -ENOBUFS;
		goto err_alloc_list;
	}

	if (need_split_huge_page(mtr) && npage > 1) {
		ret = mtr_check_direct_pages(pages, npage, page_shift);
		if (ret) {
			ibdev_err(ibdev, "failed to check %s page: %d / %d.\n",
				  mtr->umem ? "umtr" : "kmtr", ret, npage);
			ret = -ENOBUFS;
			goto err_alloc_list;
		}
	}

	ret = hns_roce_mtr_map(hr_dev, mtr, pages, page_count);
	if (ret)
		ibdev_err(ibdev, "failed to map mtr pages, ret = %d.\n", ret);

err_alloc_list:
	kvfree(pages);

	return ret;
}

int hns_roce_mtr_map(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
		     dma_addr_t *pages, unsigned int page_cnt)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_buf_region *r;
	unsigned int i, mapped_cnt;
	int ret = 0;

	/*
	 * Only use the first page address as root ba when hopnum is 0, this
	 * is because the addresses of all pages are consecutive in this case.
	 */
	if (mtr->hem_cfg.is_direct) {
		mtr->hem_cfg.root_ba = pages[0];
		return 0;
	}

	for (i = 0, mapped_cnt = 0; i < mtr->hem_cfg.region_count &&
	     mapped_cnt < page_cnt; i++) {
		r = &mtr->hem_cfg.region[i];
		/* if hopnum is 0, no need to map pages in this region */
		if (!r->hopnum) {
			mapped_cnt += r->count;
			continue;
		}

		if (r->offset + r->count > page_cnt) {
			ret = -EINVAL;
			ibdev_err(ibdev,
				  "failed to check mtr%u count %u + %u > %u.\n",
				  i, r->offset, r->count, page_cnt);
			return ret;
		}

		ret = mtr_map_region(hr_dev, mtr, r, &pages[r->offset],
				     page_cnt - mapped_cnt);
		if (ret < 0) {
			ibdev_err(ibdev,
				  "failed to map mtr%u offset %u, ret = %d.\n",
				  i, r->offset, ret);
			return ret;
		}
		mapped_cnt += ret;
		ret = 0;
	}

	if (mapped_cnt < page_cnt) {
		ret = -ENOBUFS;
		ibdev_err(ibdev, "failed to map mtr pages count: %u < %u.\n",
			  mapped_cnt, page_cnt);
	}

	return ret;
}

static int hns_roce_get_direct_addr_mtt(struct hns_roce_hem_cfg *cfg,
					u32 start_index, u64 *mtt_buf,
					int mtt_cnt)
{
	int mtt_count;
	int total = 0;
	u32 npage;
	u64 addr;

	if (mtt_cnt > cfg->region_count)
		return -EINVAL;

	for (mtt_count = 0; mtt_count < cfg->region_count && total < mtt_cnt;
	     mtt_count++) {
		npage = cfg->region[mtt_count].offset;
		if (npage < start_index)
			continue;

		addr = cfg->root_ba + (npage << HNS_HW_PAGE_SHIFT);
		mtt_buf[total] = addr;

		total++;
	}

	if (!total)
		return -ENOENT;

	return 0;
}

static int hns_roce_get_mhop_mtt(struct hns_roce_dev *hr_dev,
				 struct hns_roce_mtr *mtr, u32 start_index,
				 u64 *mtt_buf, int mtt_cnt)
{
	int left = mtt_cnt;
	int total = 0;
	int mtt_count;
	__le64 *mtts;
	u32 npage;

	while (left > 0) {
		mtt_count = 0;
		mtts = hns_roce_hem_list_find_mtt(hr_dev, &mtr->hem_list,
						  start_index + total,
						  &mtt_count);
		if (!mtts || !mtt_count)
			break;

		npage = min(mtt_count, left);
		left -= npage;
		for (mtt_count = 0; mtt_count < npage; mtt_count++)
			mtt_buf[total++] = le64_to_cpu(mtts[mtt_count]);
	}

	if (!total)
		return -ENOENT;

	return 0;
}

int hns_roce_mtr_find(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
		      u32 offset, u64 *mtt_buf, int mtt_max)
{
	struct hns_roce_hem_cfg *cfg = &mtr->hem_cfg;
	u32 start_index;
	int ret;

	if (!mtt_buf || mtt_max < 1)
		return -EINVAL;

	/* no mtt memory in direct mode, so just return the buffer address */
	if (cfg->is_direct) {
		start_index = offset >> HNS_HW_PAGE_SHIFT;
		ret = hns_roce_get_direct_addr_mtt(cfg, start_index,
						   mtt_buf, mtt_max);
	} else {
		start_index = offset >> cfg->buf_pg_shift;
		ret = hns_roce_get_mhop_mtt(hr_dev, mtr, start_index,
					    mtt_buf, mtt_max);
	}
	return ret;
}

static int get_best_page_shift(struct hns_roce_dev *hr_dev,
			       struct hns_roce_mtr *mtr,
			       struct hns_roce_buf_attr *buf_attr)
{
	unsigned int page_sz;

	if (!buf_attr->adaptive || buf_attr->type != MTR_PBL || !mtr->umem)
		return 0;

	page_sz = ib_umem_find_best_pgsz(mtr->umem,
					 hr_dev->caps.page_size_cap,
					 buf_attr->iova);
	if (!page_sz)
		return -EINVAL;

	buf_attr->page_shift = order_base_2(page_sz);
	return 0;
}

static int get_best_hop_num(struct hns_roce_dev *hr_dev,
			    struct hns_roce_mtr *mtr,
			    struct hns_roce_buf_attr *buf_attr,
			    unsigned int ba_pg_shift)
{
#define INVALID_HOPNUM -1
#define MIN_BA_CNT 1
	size_t buf_pg_sz = 1 << buf_attr->page_shift;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	size_t ba_pg_sz = 1 << ba_pg_shift;
	int hop_num = INVALID_HOPNUM;
	size_t unit = MIN_BA_CNT;
	size_t ba_cnt;
	int j;

	if (!buf_attr->adaptive || buf_attr->type != MTR_PBL)
		return 0;

	/* Caculating the number of buf pages, each buf page need a BA */
	if (mtr->umem)
		ba_cnt = ib_umem_num_dma_blocks(mtr->umem, buf_pg_sz);
	else
		ba_cnt = DIV_ROUND_UP(buf_attr->region[0].size, buf_pg_sz);

	for (j = 0; j <= HNS_ROCE_MAX_HOP_NUM; j++) {
		if (ba_cnt <= unit) {
			hop_num = j;
			break;
		}
		/* Number of BAs can be represented at per hop */
		unit *= ba_pg_sz / BA_BYTE_LEN;
	}

	if (hop_num < 0) {
		ibdev_err(ibdev,
			  "failed to calculate a valid hopnum.\n");
		return -EINVAL;
	}

	buf_attr->region[0].hopnum = hop_num;

	return 0;
}

static bool is_buf_attr_valid(struct hns_roce_dev *hr_dev,
			      struct hns_roce_buf_attr *attr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;

	if (attr->region_count > ARRAY_SIZE(attr->region) ||
	    attr->region_count < 1 || attr->page_shift < HNS_HW_PAGE_SHIFT) {
		ibdev_err(ibdev,
			  "invalid buf attr, region count %d, page shift %u.\n",
			  attr->region_count, attr->page_shift);
		return false;
	}

	return true;
}

static int mtr_init_buf_cfg(struct hns_roce_dev *hr_dev,
			    struct hns_roce_mtr *mtr,
			    struct hns_roce_buf_attr *attr)
{
	struct hns_roce_hem_cfg *cfg = &mtr->hem_cfg;
	struct hns_roce_buf_region *r;
	size_t buf_pg_sz;
	size_t buf_size;
	int page_cnt, i;
	u64 pgoff = 0;

	if (!is_buf_attr_valid(hr_dev, attr))
		return -EINVAL;

	/* If mtt is disabled, all pages must be within a continuous range */
	cfg->is_direct = !mtr_has_mtt(attr);
	cfg->region_count = attr->region_count;
	buf_size = mtr_bufs_size(attr);
	if (need_split_huge_page(mtr)) {
		buf_pg_sz = HNS_HW_PAGE_SIZE;
		cfg->buf_pg_count = 1;
		/* The ROCEE requires the page size to be 4K * 2 ^ N. */
		cfg->buf_pg_shift = HNS_HW_PAGE_SHIFT +
			order_base_2(DIV_ROUND_UP(buf_size, HNS_HW_PAGE_SIZE));
	} else {
		buf_pg_sz = 1 << attr->page_shift;
		cfg->buf_pg_count = mtr->umem ?
			ib_umem_num_dma_blocks(mtr->umem, buf_pg_sz) :
			DIV_ROUND_UP(buf_size, buf_pg_sz);
		cfg->buf_pg_shift = attr->page_shift;
		pgoff = mtr->umem ? mtr->umem->address & ~PAGE_MASK : 0;
	}

	/* Convert buffer size to page index and page count for each region and
	 * the buffer's offset needs to be appended to the first region.
	 */
	for (page_cnt = 0, i = 0; i < attr->region_count; i++) {
		r = &cfg->region[i];
		r->offset = page_cnt;
		buf_size = hr_hw_page_align(attr->region[i].size + pgoff);
		if (attr->type == MTR_PBL && mtr->umem)
			r->count = ib_umem_num_dma_blocks(mtr->umem, buf_pg_sz);
		else
			r->count = DIV_ROUND_UP(buf_size, buf_pg_sz);

		pgoff = 0;
		page_cnt += r->count;
		r->hopnum = to_hr_hem_hopnum(attr->region[i].hopnum, r->count);
	}

	return 0;
}

static u64 cal_pages_per_l1ba(unsigned int ba_per_bt, unsigned int hopnum)
{
	return int_pow(ba_per_bt, hopnum - 1);
}

static unsigned int cal_best_bt_pg_sz(struct hns_roce_dev *hr_dev,
				      struct hns_roce_mtr *mtr,
				      unsigned int pg_shift)
{
	unsigned long cap = hr_dev->caps.page_size_cap;
	struct hns_roce_buf_region *re;
	unsigned int pgs_per_l1ba;
	unsigned int ba_per_bt;
	unsigned int ba_num;
	int i;

	for_each_set_bit_from(pg_shift, &cap, sizeof(cap) * BITS_PER_BYTE) {
		if (!(BIT(pg_shift) & cap))
			continue;

		ba_per_bt = BIT(pg_shift) / BA_BYTE_LEN;
		ba_num = 0;
		for (i = 0; i < mtr->hem_cfg.region_count; i++) {
			re = &mtr->hem_cfg.region[i];
			if (re->hopnum == 0)
				continue;

			pgs_per_l1ba = cal_pages_per_l1ba(ba_per_bt, re->hopnum);
			ba_num += DIV_ROUND_UP(re->count, pgs_per_l1ba);
		}

		if (ba_num <= ba_per_bt)
			return pg_shift;
	}

	return 0;
}

static int mtr_alloc_mtt(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
			 unsigned int ba_page_shift)
{
	struct hns_roce_hem_cfg *cfg = &mtr->hem_cfg;
	int ret;

	hns_roce_hem_list_init(&mtr->hem_list);
	if (!cfg->is_direct) {
		ba_page_shift = cal_best_bt_pg_sz(hr_dev, mtr, ba_page_shift);
		if (!ba_page_shift)
			return -ERANGE;

		ret = hns_roce_hem_list_request(hr_dev, &mtr->hem_list,
						cfg->region, cfg->region_count,
						ba_page_shift);
		if (ret)
			return ret;
		cfg->root_ba = mtr->hem_list.root_ba;
		cfg->ba_pg_shift = ba_page_shift;
	} else {
		cfg->ba_pg_shift = cfg->buf_pg_shift;
	}

	return 0;
}

static void mtr_free_mtt(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr)
{
	hns_roce_hem_list_release(hr_dev, &mtr->hem_list);
}

/**
 * hns_roce_mtr_create - Create hns memory translate region.
 *
 * @hr_dev: RoCE device struct pointer
 * @mtr: memory translate region
 * @buf_attr: buffer attribute for creating mtr
 * @ba_page_shift: page shift for multi-hop base address table
 * @udata: user space context, if it's NULL, means kernel space
 * @user_addr: userspace virtual address to start at
 */
int hns_roce_mtr_create(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
			struct hns_roce_buf_attr *buf_attr,
			unsigned int ba_page_shift, struct ib_udata *udata,
			unsigned long user_addr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int ret;

	/* The caller has its own buffer list and invokes the hns_roce_mtr_map()
	 * to finish the MTT configuration.
	 */
	if (buf_attr->mtt_only) {
		mtr->umem = NULL;
		mtr->kmem = NULL;
	} else {
		ret = mtr_alloc_bufs(hr_dev, mtr, buf_attr, udata, user_addr);
		if (ret) {
			ibdev_err(ibdev,
				  "failed to alloc mtr bufs, ret = %d.\n", ret);
			return ret;
		}

		ret = get_best_page_shift(hr_dev, mtr, buf_attr);
		if (ret)
			goto err_init_buf;

		ret = get_best_hop_num(hr_dev, mtr, buf_attr, ba_page_shift);
		if (ret)
			goto err_init_buf;
	}

	ret = mtr_init_buf_cfg(hr_dev, mtr, buf_attr);
	if (ret)
		goto err_init_buf;

	ret = mtr_alloc_mtt(hr_dev, mtr, ba_page_shift);
	if (ret) {
		ibdev_err(ibdev, "failed to alloc mtr mtt, ret = %d.\n", ret);
		goto err_init_buf;
	}

	if (buf_attr->mtt_only)
		return 0;

	/* Write buffer's dma address to MTT */
	ret = mtr_map_bufs(hr_dev, mtr);
	if (ret) {
		ibdev_err(ibdev, "failed to map mtr bufs, ret = %d.\n", ret);
		goto err_alloc_mtt;
	}

	return 0;

err_alloc_mtt:
	mtr_free_mtt(hr_dev, mtr);
err_init_buf:
	mtr_free_bufs(hr_dev, mtr);

	return ret;
}

void hns_roce_mtr_destroy(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr)
{
	/* release multi-hop addressing resource */
	hns_roce_hem_list_release(hr_dev, &mtr->hem_list);

	/* free buffers */
	mtr_free_bufs(hr_dev, mtr);
}
