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

static int alloc_mr_key(struct hns_roce_dev *hr_dev, struct hns_roce_mr *mr,
			u32 pd, u64 iova, u64 size, u32 access)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	unsigned long obj = 0;
	int err;

	/* Allocate a key for mr from mr_table */
	err = hns_roce_bitmap_alloc(&hr_dev->mr_table.mtpt_bitmap, &obj);
	if (err) {
		ibdev_err(ibdev,
			  "failed to alloc bitmap for MR key, ret = %d.\n",
			  err);
		return -ENOMEM;
	}

	mr->iova = iova;			/* MR va starting addr */
	mr->size = size;			/* MR addr range */
	mr->pd = pd;				/* MR num */
	mr->access = access;			/* MR access permit */
	mr->enabled = 0;			/* MR active status */
	mr->key = hw_index_to_key(obj);		/* MR key */

	err = hns_roce_table_get(hr_dev, &hr_dev->mr_table.mtpt_table, obj);
	if (err) {
		ibdev_err(ibdev, "failed to alloc mtpt, ret = %d.\n", err);
		goto err_free_bitmap;
	}

	return 0;
err_free_bitmap:
	hns_roce_bitmap_free(&hr_dev->mr_table.mtpt_bitmap, obj, BITMAP_NO_RR);
	return err;
}

static void free_mr_key(struct hns_roce_dev *hr_dev, struct hns_roce_mr *mr)
{
	unsigned long obj = key_to_hw_index(mr->key);

	hns_roce_table_put(hr_dev, &hr_dev->mr_table.mtpt_table, obj);
	hns_roce_bitmap_free(&hr_dev->mr_table.mtpt_bitmap, obj, BITMAP_NO_RR);
}

static int alloc_mr_pbl(struct hns_roce_dev *hr_dev, struct hns_roce_mr *mr,
			size_t length, struct ib_udata *udata, u64 start,
			int access)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	bool is_fast = mr->type == MR_TYPE_FRMR;
	struct hns_roce_buf_attr buf_attr = {};
	int err;

	mr->pbl_hop_num = is_fast ? 1 : hr_dev->caps.pbl_hop_num;
	buf_attr.page_shift = is_fast ? PAGE_SHIFT :
			      hr_dev->caps.pbl_buf_pg_sz + HNS_HW_PAGE_SHIFT;
	buf_attr.region[0].size = length;
	buf_attr.region[0].hopnum = mr->pbl_hop_num;
	buf_attr.region_count = 1;
	buf_attr.fixed_page = true;
	buf_attr.user_access = access;
	/* fast MR's buffer is alloced before mapping, not at creation */
	buf_attr.mtt_only = is_fast;

	err = hns_roce_mtr_create(hr_dev, &mr->pbl_mtr, &buf_attr,
				  hr_dev->caps.pbl_ba_pg_sz + HNS_HW_PAGE_SHIFT,
				  udata, start);
	if (err)
		ibdev_err(ibdev, "failed to alloc pbl mtr, ret = %d.\n", err);
	else
		mr->npages = mr->pbl_mtr.hem_cfg.buf_pg_count;

	return err;
}

static void free_mr_pbl(struct hns_roce_dev *hr_dev, struct hns_roce_mr *mr)
{
	hns_roce_mtr_destroy(hr_dev, &mr->pbl_mtr);
}

static void hns_roce_mr_free(struct hns_roce_dev *hr_dev,
			     struct hns_roce_mr *mr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int ret;

	if (mr->enabled) {
		ret = hns_roce_hw_destroy_mpt(hr_dev, NULL,
					      key_to_hw_index(mr->key) &
					      (hr_dev->caps.num_mtpts - 1));
		if (ret)
			ibdev_warn(ibdev, "failed to destroy mpt, ret = %d.\n",
				   ret);
	}

	free_mr_pbl(hr_dev, mr);
	free_mr_key(hr_dev, mr);
}

static int hns_roce_mr_enable(struct hns_roce_dev *hr_dev,
			      struct hns_roce_mr *mr)
{
	int ret;
	unsigned long mtpt_idx = key_to_hw_index(mr->key);
	struct device *dev = hr_dev->dev;
	struct hns_roce_cmd_mailbox *mailbox;

	/* Allocate mailbox memory */
	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox)) {
		ret = PTR_ERR(mailbox);
		return ret;
	}

	if (mr->type != MR_TYPE_FRMR)
		ret = hr_dev->hw->write_mtpt(hr_dev, mailbox->buf, mr,
					     mtpt_idx);
	else
		ret = hr_dev->hw->frmr_write_mtpt(hr_dev, mailbox->buf, mr);
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
	return ret;
}

void hns_roce_cleanup_mr_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_mr_table *mr_table = &hr_dev->mr_table;

	hns_roce_bitmap_cleanup(&mr_table->mtpt_bitmap);
}

struct ib_mr *hns_roce_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct hns_roce_mr *mr;
	int ret;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (mr == NULL)
		return  ERR_PTR(-ENOMEM);

	mr->type = MR_TYPE_DMA;

	/* Allocate memory region key */
	hns_roce_hem_list_init(&mr->pbl_mtr.hem_list);
	ret = alloc_mr_key(hr_dev, mr, to_hr_pd(pd)->pdn, 0, 0, acc);
	if (ret)
		goto err_free;

	ret = hns_roce_mr_enable(to_hr_dev(pd->device), mr);
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
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->type = MR_TYPE_MR;
	ret = alloc_mr_key(hr_dev, mr, to_hr_pd(pd)->pdn, virt_addr, length,
			   access_flags);
	if (ret)
		goto err_alloc_mr;

	ret = alloc_mr_pbl(hr_dev, mr, length, udata, start, access_flags);
	if (ret)
		goto err_alloc_key;

	ret = hns_roce_mr_enable(hr_dev, mr);
	if (ret)
		goto err_alloc_pbl;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->key;
	mr->ibmr.length = length;

	return &mr->ibmr;

err_alloc_pbl:
	free_mr_pbl(hr_dev, mr);
err_alloc_key:
	free_mr_key(hr_dev, mr);
err_alloc_mr:
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
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_mr *mr = to_hr_mr(ibmr);
	int ret;

	free_mr_pbl(hr_dev, mr);
	ret = alloc_mr_pbl(hr_dev, mr, length, udata, start, mr_access_flags);
	if (ret) {
		ibdev_err(ibdev, "failed to create mr PBL, ret = %d.\n", ret);
		return ret;
	}

	ret = hr_dev->hw->rereg_write_mtpt(hr_dev, mr, flags, pdn,
					   mr_access_flags, virt_addr,
					   length, mailbox->buf);
	if (ret) {
		ibdev_err(ibdev, "failed to write mtpt, ret = %d.\n", ret);
		free_mr_pbl(hr_dev, mr);
	}

	return ret;
}

int hns_roce_rereg_user_mr(struct ib_mr *ibmr, int flags, u64 start, u64 length,
			   u64 virt_addr, int mr_access_flags, struct ib_pd *pd,
			   struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibmr->device);
	struct ib_device *ib_dev = &hr_dev->ib_dev;
	struct hns_roce_mr *mr = to_hr_mr(ibmr);
	struct hns_roce_cmd_mailbox *mailbox;
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
		ibdev_warn(ib_dev, "failed to destroy MPT, ret = %d.\n", ret);

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
		ibdev_err(ib_dev, "failed to create MPT, ret = %d.\n", ret);
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

	/* Allocate memory region key */
	length = max_num_sg * (1 << PAGE_SHIFT);
	ret = alloc_mr_key(hr_dev, mr, to_hr_pd(pd)->pdn, 0, length, 0);
	if (ret)
		goto err_free;

	ret = alloc_mr_pbl(hr_dev, mr, length, NULL, 0, 0);
	if (ret)
		goto err_key;

	ret = hns_roce_mr_enable(hr_dev, mr);
	if (ret)
		goto err_pbl;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->key;
	mr->ibmr.length = length;

	return &mr->ibmr;

err_key:
	free_mr_key(hr_dev, mr);
err_pbl:
	free_mr_pbl(hr_dev, mr);
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
	int ret = 0;

	mr->npages = 0;
	mr->page_list = kvcalloc(mr->pbl_mtr.hem_cfg.buf_pg_count,
				 sizeof(dma_addr_t), GFP_KERNEL);
	if (!mr->page_list)
		return ret;

	ret = ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, hns_roce_set_page);
	if (ret < 1) {
		ibdev_err(ibdev, "failed to store sg pages %d %d, cnt = %d.\n",
			  mr->npages, mr->pbl_mtr.hem_cfg.buf_pg_count, ret);
		goto err_page_list;
	}

	mtr->hem_cfg.region[0].offset = 0;
	mtr->hem_cfg.region[0].count = mr->npages;
	mtr->hem_cfg.region[0].hopnum = mr->pbl_hop_num;
	mtr->hem_cfg.region_count = 1;
	ret = hns_roce_mtr_map(hr_dev, mtr, mr->page_list, mr->npages);
	if (ret) {
		ibdev_err(ibdev, "failed to map sg mtr, ret = %d.\n", ret);
		ret = 0;
	} else {
		mr->pbl_mtr.hem_cfg.buf_pg_shift = ilog2(ibmr->page_size);
		ret = mr->npages;
	}

err_page_list:
	kvfree(mr->page_list);
	mr->page_list = NULL;

	return ret;
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

static int mtr_map_region(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
			  dma_addr_t *pages, struct hns_roce_buf_region *region)
{
	__le64 *mtts;
	int offset;
	int count;
	int npage;
	u64 addr;
	int end;
	int i;

	/* if hopnum is 0, buffer cannot store BAs, so skip write mtt */
	if (!region->hopnum)
		return 0;

	offset = region->offset;
	end = offset + region->count;
	npage = 0;
	while (offset < end) {
		mtts = hns_roce_hem_list_find_mtt(hr_dev, &mtr->hem_list,
						  offset, &count, NULL);
		if (!mtts)
			return -ENOBUFS;

		for (i = 0; i < count; i++) {
			if (hr_dev->hw_rev == HNS_ROCE_HW_VER1)
				addr = to_hr_hw_page_addr(pages[npage]);
			else
				addr = pages[npage];

			mtts[i] = cpu_to_le64(addr);
			npage++;
		}
		offset += count;
	}

	return 0;
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

static inline int mtr_umem_page_count(struct ib_umem *umem,
				      unsigned int page_shift)
{
	int count = ib_umem_page_count(umem);

	if (page_shift >= PAGE_SHIFT)
		count >>= page_shift - PAGE_SHIFT;
	else
		count <<= PAGE_SHIFT - page_shift;

	return count;
}

static inline size_t mtr_kmem_direct_size(bool is_direct, size_t alloc_size,
					  unsigned int page_shift)
{
	if (is_direct)
		return ALIGN(alloc_size, 1 << page_shift);
	else
		return HNS_HW_DIRECT_PAGE_COUNT << page_shift;
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
		kfree(mtr->kmem);
		mtr->kmem = NULL;
	}
}

static int mtr_alloc_bufs(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
			  struct hns_roce_buf_attr *buf_attr, bool is_direct,
			  struct ib_udata *udata, unsigned long user_addr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	unsigned int max_pg_shift = buf_attr->page_shift;
	unsigned int best_pg_shift = 0;
	int all_pg_count = 0;
	size_t direct_size;
	size_t total_size;
	unsigned long tmp;
	int ret = 0;

	total_size = mtr_bufs_size(buf_attr);
	if (total_size < 1) {
		ibdev_err(ibdev, "Failed to check mtr size\n");
		return -EINVAL;
	}

	if (udata) {
		mtr->kmem = NULL;
		mtr->umem = ib_umem_get(ibdev, user_addr, total_size,
					buf_attr->user_access);
		if (IS_ERR_OR_NULL(mtr->umem)) {
			ibdev_err(ibdev, "Failed to get umem, ret %ld\n",
				  PTR_ERR(mtr->umem));
			return -ENOMEM;
		}
		if (buf_attr->fixed_page) {
			best_pg_shift = max_pg_shift;
		} else {
			tmp = GENMASK(max_pg_shift, 0);
			ret = ib_umem_find_best_pgsz(mtr->umem, tmp, user_addr);
			best_pg_shift = (ret <= PAGE_SIZE) ?
					PAGE_SHIFT : ilog2(ret);
		}
		all_pg_count = mtr_umem_page_count(mtr->umem, best_pg_shift);
		ret = 0;
	} else {
		mtr->umem = NULL;
		mtr->kmem = kzalloc(sizeof(*mtr->kmem), GFP_KERNEL);
		if (!mtr->kmem) {
			ibdev_err(ibdev, "Failed to alloc kmem\n");
			return -ENOMEM;
		}
		direct_size = mtr_kmem_direct_size(is_direct, total_size,
						   max_pg_shift);
		ret = hns_roce_buf_alloc(hr_dev, total_size, direct_size,
					 mtr->kmem, max_pg_shift);
		if (ret) {
			ibdev_err(ibdev, "Failed to alloc kmem, ret %d\n", ret);
			goto err_alloc_mem;
		} else {
			best_pg_shift = max_pg_shift;
			all_pg_count = mtr->kmem->npages;
		}
	}

	/* must bigger than minimum hardware page shift */
	if (best_pg_shift < HNS_HW_PAGE_SHIFT || all_pg_count < 1) {
		ret = -EINVAL;
		ibdev_err(ibdev, "Failed to check mtr page shift %d count %d\n",
			  best_pg_shift, all_pg_count);
		goto err_alloc_mem;
	}

	mtr->hem_cfg.buf_pg_shift = best_pg_shift;
	mtr->hem_cfg.buf_pg_count = all_pg_count;

	return 0;
err_alloc_mem:
	mtr_free_bufs(hr_dev, mtr);
	return ret;
}

static int mtr_get_pages(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
			 dma_addr_t *pages, int count, unsigned int page_shift)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int npage;
	int err;

	if (mtr->umem)
		npage = hns_roce_get_umem_bufs(hr_dev, pages, count, 0,
					       mtr->umem, page_shift);
	else
		npage = hns_roce_get_kmem_bufs(hr_dev, pages, count, 0,
					       mtr->kmem);

	if (mtr->hem_cfg.is_direct && npage > 1) {
		err = mtr_check_direct_pages(pages, npage, page_shift);
		if (err) {
			ibdev_err(ibdev, "Failed to check %s direct page-%d\n",
				  mtr->umem ? "user" : "kernel", err);
			npage = err;
		}
	}

	return npage;
}

int hns_roce_mtr_map(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
		     dma_addr_t *pages, int page_cnt)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_buf_region *r;
	int err;
	int i;

	for (i = 0; i < mtr->hem_cfg.region_count; i++) {
		r = &mtr->hem_cfg.region[i];
		if (r->offset + r->count > page_cnt) {
			err = -EINVAL;
			ibdev_err(ibdev,
				  "Failed to check mtr%d end %d + %d, max %d\n",
				  i, r->offset, r->count, page_cnt);
			return err;
		}

		err = mtr_map_region(hr_dev, mtr, &pages[r->offset], r);
		if (err) {
			ibdev_err(ibdev,
				  "Failed to map mtr%d offset %d, err %d\n",
				  i, r->offset, err);
			return err;
		}
	}

	return 0;
}

int hns_roce_mtr_find(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
		      int offset, u64 *mtt_buf, int mtt_max, u64 *base_addr)
{
	int mtt_count;
	int total = 0;
	__le64 *mtts;
	int npage;
	u64 addr;
	int left;

	if (!mtt_buf || mtt_max < 1)
		goto done;

	/* no mtt memory in direct mode, so just return the buffer address */
	if (mtr->hem_cfg.is_direct) {
		npage = offset;
		for (total = 0; total < mtt_max; total++, npage++) {
			addr = mtr->hem_cfg.root_ba +
			       (npage << mtr->hem_cfg.buf_pg_shift);

			if (hr_dev->hw_rev == HNS_ROCE_HW_VER1)
				mtt_buf[total] = to_hr_hw_page_addr(addr);
			else
				mtt_buf[total] = addr;
		}

		goto done;
	}

	left = mtt_max;
	while (left > 0) {
		mtt_count = 0;
		mtts = hns_roce_hem_list_find_mtt(hr_dev, &mtr->hem_list,
						  offset + total,
						  &mtt_count, NULL);
		if (!mtts || !mtt_count)
			goto done;

		npage = min(mtt_count, left);
		left -= npage;
		for (mtt_count = 0; mtt_count < npage; mtt_count++)
			mtt_buf[total++] = le64_to_cpu(mtts[mtt_count]);
	}

done:
	if (base_addr)
		*base_addr = mtr->hem_cfg.root_ba;

	return total;
}

/* convert buffer size to page index and page count */
static unsigned int mtr_init_region(struct hns_roce_buf_attr *attr,
				    int page_cnt,
				    struct hns_roce_buf_region *regions,
				    int region_cnt, unsigned int page_shift)
{
	unsigned int page_size = 1 << page_shift;
	int max_region = attr->region_count;
	struct hns_roce_buf_region *r;
	unsigned int i = 0;
	int page_idx = 0;

	for (; i < region_cnt && i < max_region && page_idx < page_cnt; i++) {
		r = &regions[i];
		r->hopnum = attr->region[i].hopnum == HNS_ROCE_HOP_NUM_0 ?
			    0 : attr->region[i].hopnum;
		r->offset = page_idx;
		r->count = DIV_ROUND_UP(attr->region[i].size, page_size);
		page_idx += r->count;
	}

	return i;
}

/**
 * hns_roce_mtr_create - Create hns memory translate region.
 *
 * @mtr: memory translate region
 * @init_attr: init attribute for creating mtr
 * @page_shift: page shift for multi-hop base address table
 * @udata: user space context, if it's NULL, means kernel space
 * @user_addr: userspace virtual address to start at
 * @buf_alloced: mtr has private buffer, true means need to alloc
 */
int hns_roce_mtr_create(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr,
			struct hns_roce_buf_attr *buf_attr,
			unsigned int page_shift, struct ib_udata *udata,
			unsigned long user_addr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	dma_addr_t *pages = NULL;
	int region_cnt = 0;
	int all_pg_cnt;
	int get_pg_cnt;
	bool has_mtt;
	int err = 0;

	has_mtt = mtr_has_mtt(buf_attr);
	/* if buffer only need mtt, just init the hem cfg */
	if (buf_attr->mtt_only) {
		mtr->hem_cfg.buf_pg_shift = buf_attr->page_shift;
		mtr->hem_cfg.buf_pg_count = mtr_bufs_size(buf_attr) >>
					    buf_attr->page_shift;
		mtr->umem = NULL;
		mtr->kmem = NULL;
	} else {
		err = mtr_alloc_bufs(hr_dev, mtr, buf_attr, !has_mtt, udata,
				     user_addr);
		if (err) {
			ibdev_err(ibdev, "Failed to alloc mtr bufs, err %d\n",
				  err);
			return err;
		}
	}

	/* alloc mtt memory */
	all_pg_cnt = mtr->hem_cfg.buf_pg_count;
	hns_roce_hem_list_init(&mtr->hem_list);
	mtr->hem_cfg.is_direct = !has_mtt;
	mtr->hem_cfg.ba_pg_shift = page_shift;
	mtr->hem_cfg.region_count = 0;
	region_cnt = mtr_init_region(buf_attr, all_pg_cnt,
				     mtr->hem_cfg.region,
				     ARRAY_SIZE(mtr->hem_cfg.region),
				     mtr->hem_cfg.buf_pg_shift);
	if (region_cnt < 1) {
		err = -ENOBUFS;
		ibdev_err(ibdev, "failed to init mtr region %d\n", region_cnt);
		goto err_alloc_bufs;
	}

	mtr->hem_cfg.region_count = region_cnt;

	if (has_mtt) {
		err = hns_roce_hem_list_request(hr_dev, &mtr->hem_list,
						mtr->hem_cfg.region, region_cnt,
						page_shift);
		if (err) {
			ibdev_err(ibdev, "Failed to request mtr hem, err %d\n",
				  err);
			goto err_alloc_bufs;
		}
		mtr->hem_cfg.root_ba = mtr->hem_list.root_ba;
	}

	/* no buffer to map */
	if (buf_attr->mtt_only)
		return 0;

	/* alloc a tmp array to store buffer's dma address */
	pages = kvcalloc(all_pg_cnt, sizeof(dma_addr_t), GFP_KERNEL);
	if (!pages) {
		err = -ENOMEM;
		ibdev_err(ibdev, "Failed to alloc mtr page list %d\n",
			  all_pg_cnt);
		goto err_alloc_hem_list;
	}

	get_pg_cnt = mtr_get_pages(hr_dev, mtr, pages, all_pg_cnt,
				   mtr->hem_cfg.buf_pg_shift);
	if (get_pg_cnt != all_pg_cnt) {
		ibdev_err(ibdev, "Failed to get mtr page %d != %d\n",
			  get_pg_cnt, all_pg_cnt);
		err = -ENOBUFS;
		goto err_alloc_page_list;
	}

	if (!has_mtt) {
		mtr->hem_cfg.root_ba = pages[0];
	} else {
		/* write buffer's dma address to BA table */
		err = hns_roce_mtr_map(hr_dev, mtr, pages, all_pg_cnt);
		if (err) {
			ibdev_err(ibdev, "Failed to map mtr pages, err %d\n",
				  err);
			goto err_alloc_page_list;
		}
	}

	/* drop tmp array */
	kvfree(pages);
	return 0;
err_alloc_page_list:
	kvfree(pages);
err_alloc_hem_list:
	hns_roce_hem_list_release(hr_dev, &mtr->hem_list);
err_alloc_bufs:
	mtr_free_bufs(hr_dev, mtr);
	return err;
}

void hns_roce_mtr_destroy(struct hns_roce_dev *hr_dev, struct hns_roce_mtr *mtr)
{
	/* release multi-hop addressing resource */
	hns_roce_hem_list_release(hr_dev, &mtr->hem_list);

	/* free buffers */
	mtr_free_bufs(hr_dev, mtr);
}
