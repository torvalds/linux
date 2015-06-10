/*
 * Copyright (c) 2004, 2005, 2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2013-2014 Mellanox Technologies. All rights reserved.
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>

#include "iscsi_iser.h"

static void
iser_free_bounce_sg(struct iser_data_buf *data)
{
	struct scatterlist *sg;
	int count;

	for_each_sg(data->sg, sg, data->size, count)
		__free_page(sg_page(sg));

	kfree(data->sg);

	data->sg = data->orig_sg;
	data->size = data->orig_size;
	data->orig_sg = NULL;
	data->orig_size = 0;
}

static int
iser_alloc_bounce_sg(struct iser_data_buf *data)
{
	struct scatterlist *sg;
	struct page *page;
	unsigned long length = data->data_len;
	int i = 0, nents = DIV_ROUND_UP(length, PAGE_SIZE);

	sg = kcalloc(nents, sizeof(*sg), GFP_ATOMIC);
	if (!sg)
		goto err;

	sg_init_table(sg, nents);
	while (length) {
		u32 page_len = min_t(u32, length, PAGE_SIZE);

		page = alloc_page(GFP_ATOMIC);
		if (!page)
			goto err;

		sg_set_page(&sg[i], page, page_len, 0);
		length -= page_len;
		i++;
	}

	data->orig_sg = data->sg;
	data->orig_size = data->size;
	data->sg = sg;
	data->size = nents;

	return 0;

err:
	for (; i > 0; i--)
		__free_page(sg_page(&sg[i - 1]));
	kfree(sg);

	return -ENOMEM;
}

static void
iser_copy_bounce(struct iser_data_buf *data, bool to_buffer)
{
	struct scatterlist *osg, *bsg = data->sg;
	void *oaddr, *baddr;
	unsigned int left = data->data_len;
	unsigned int bsg_off = 0;
	int i;

	for_each_sg(data->orig_sg, osg, data->orig_size, i) {
		unsigned int copy_len, osg_off = 0;

		oaddr = kmap_atomic(sg_page(osg)) + osg->offset;
		copy_len = min(left, osg->length);
		while (copy_len) {
			unsigned int len = min(copy_len, bsg->length - bsg_off);

			baddr = kmap_atomic(sg_page(bsg)) + bsg->offset;
			if (to_buffer)
				memcpy(baddr + bsg_off, oaddr + osg_off, len);
			else
				memcpy(oaddr + osg_off, baddr + bsg_off, len);

			kunmap_atomic(baddr - bsg->offset);
			osg_off += len;
			bsg_off += len;
			copy_len -= len;

			if (bsg_off >= bsg->length) {
				bsg = sg_next(bsg);
				bsg_off = 0;
			}
		}
		kunmap_atomic(oaddr - osg->offset);
		left -= osg_off;
	}
}

static inline void
iser_copy_from_bounce(struct iser_data_buf *data)
{
	iser_copy_bounce(data, false);
}

static inline void
iser_copy_to_bounce(struct iser_data_buf *data)
{
	iser_copy_bounce(data, true);
}

struct fast_reg_descriptor *
iser_reg_desc_get(struct ib_conn *ib_conn)
{
	struct fast_reg_descriptor *desc;
	unsigned long flags;

	spin_lock_irqsave(&ib_conn->lock, flags);
	desc = list_first_entry(&ib_conn->fastreg.pool,
				struct fast_reg_descriptor, list);
	list_del(&desc->list);
	spin_unlock_irqrestore(&ib_conn->lock, flags);

	return desc;
}

void
iser_reg_desc_put(struct ib_conn *ib_conn,
		  struct fast_reg_descriptor *desc)
{
	unsigned long flags;

	spin_lock_irqsave(&ib_conn->lock, flags);
	list_add(&desc->list, &ib_conn->fastreg.pool);
	spin_unlock_irqrestore(&ib_conn->lock, flags);
}

/**
 * iser_start_rdma_unaligned_sg
 */
static int iser_start_rdma_unaligned_sg(struct iscsi_iser_task *iser_task,
					struct iser_data_buf *data,
					enum iser_data_dir cmd_dir)
{
	struct ib_device *dev = iser_task->iser_conn->ib_conn.device->ib_device;
	int rc;

	rc = iser_alloc_bounce_sg(data);
	if (rc) {
		iser_err("Failed to allocate bounce for data len %lu\n",
			 data->data_len);
		return rc;
	}

	if (cmd_dir == ISER_DIR_OUT)
		iser_copy_to_bounce(data);

	data->dma_nents = ib_dma_map_sg(dev, data->sg, data->size,
					(cmd_dir == ISER_DIR_OUT) ?
					DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (!data->dma_nents) {
		iser_err("Got dma_nents %d, something went wrong...\n",
			 data->dma_nents);
		rc = -ENOMEM;
		goto err;
	}

	return 0;
err:
	iser_free_bounce_sg(data);
	return rc;
}

/**
 * iser_finalize_rdma_unaligned_sg
 */

void iser_finalize_rdma_unaligned_sg(struct iscsi_iser_task *iser_task,
				     struct iser_data_buf *data,
				     enum iser_data_dir cmd_dir)
{
	struct ib_device *dev = iser_task->iser_conn->ib_conn.device->ib_device;

	ib_dma_unmap_sg(dev, data->sg, data->size,
			(cmd_dir == ISER_DIR_OUT) ?
			DMA_TO_DEVICE : DMA_FROM_DEVICE);

	if (cmd_dir == ISER_DIR_IN)
		iser_copy_from_bounce(data);

	iser_free_bounce_sg(data);
}

#define IS_4K_ALIGNED(addr)	((((unsigned long)addr) & ~MASK_4K) == 0)

/**
 * iser_sg_to_page_vec - Translates scatterlist entries to physical addresses
 * and returns the length of resulting physical address array (may be less than
 * the original due to possible compaction).
 *
 * we build a "page vec" under the assumption that the SG meets the RDMA
 * alignment requirements. Other then the first and last SG elements, all
 * the "internal" elements can be compacted into a list whose elements are
 * dma addresses of physical pages. The code supports also the weird case
 * where --few fragments of the same page-- are present in the SG as
 * consecutive elements. Also, it handles one entry SG.
 */

static int iser_sg_to_page_vec(struct iser_data_buf *data,
			       struct ib_device *ibdev, u64 *pages,
			       int *offset, int *data_size)
{
	struct scatterlist *sg, *sgl = data->sg;
	u64 start_addr, end_addr, page, chunk_start = 0;
	unsigned long total_sz = 0;
	unsigned int dma_len;
	int i, new_chunk, cur_page, last_ent = data->dma_nents - 1;

	/* compute the offset of first element */
	*offset = (u64) sgl[0].offset & ~MASK_4K;

	new_chunk = 1;
	cur_page  = 0;
	for_each_sg(sgl, sg, data->dma_nents, i) {
		start_addr = ib_sg_dma_address(ibdev, sg);
		if (new_chunk)
			chunk_start = start_addr;
		dma_len = ib_sg_dma_len(ibdev, sg);
		end_addr = start_addr + dma_len;
		total_sz += dma_len;

		/* collect page fragments until aligned or end of SG list */
		if (!IS_4K_ALIGNED(end_addr) && i < last_ent) {
			new_chunk = 0;
			continue;
		}
		new_chunk = 1;

		/* address of the first page in the contiguous chunk;
		   masking relevant for the very first SG entry,
		   which might be unaligned */
		page = chunk_start & MASK_4K;
		do {
			pages[cur_page++] = page;
			page += SIZE_4K;
		} while (page < end_addr);
	}

	*data_size = total_sz;
	iser_dbg("page_vec->data_size:%d cur_page %d\n",
		 *data_size, cur_page);
	return cur_page;
}


/**
 * iser_data_buf_aligned_len - Tries to determine the maximal correctly aligned
 * for RDMA sub-list of a scatter-gather list of memory buffers, and  returns
 * the number of entries which are aligned correctly. Supports the case where
 * consecutive SG elements are actually fragments of the same physcial page.
 */
static int iser_data_buf_aligned_len(struct iser_data_buf *data,
				      struct ib_device *ibdev)
{
	struct scatterlist *sg, *sgl, *next_sg = NULL;
	u64 start_addr, end_addr;
	int i, ret_len, start_check = 0;

	if (data->dma_nents == 1)
		return 1;

	sgl = data->sg;
	start_addr  = ib_sg_dma_address(ibdev, sgl);

	for_each_sg(sgl, sg, data->dma_nents, i) {
		if (start_check && !IS_4K_ALIGNED(start_addr))
			break;

		next_sg = sg_next(sg);
		if (!next_sg)
			break;

		end_addr    = start_addr + ib_sg_dma_len(ibdev, sg);
		start_addr  = ib_sg_dma_address(ibdev, next_sg);

		if (end_addr == start_addr) {
			start_check = 0;
			continue;
		} else
			start_check = 1;

		if (!IS_4K_ALIGNED(end_addr))
			break;
	}
	ret_len = (next_sg) ? i : i+1;
	iser_dbg("Found %d aligned entries out of %d in sg:0x%p\n",
		 ret_len, data->dma_nents, data);
	return ret_len;
}

static void iser_data_buf_dump(struct iser_data_buf *data,
			       struct ib_device *ibdev)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(data->sg, sg, data->dma_nents, i)
		iser_dbg("sg[%d] dma_addr:0x%lX page:0x%p "
			 "off:0x%x sz:0x%x dma_len:0x%x\n",
			 i, (unsigned long)ib_sg_dma_address(ibdev, sg),
			 sg_page(sg), sg->offset,
			 sg->length, ib_sg_dma_len(ibdev, sg));
}

static void iser_dump_page_vec(struct iser_page_vec *page_vec)
{
	int i;

	iser_err("page vec length %d data size %d\n",
		 page_vec->length, page_vec->data_size);
	for (i = 0; i < page_vec->length; i++)
		iser_err("%d %lx\n",i,(unsigned long)page_vec->pages[i]);
}

int iser_dma_map_task_data(struct iscsi_iser_task *iser_task,
			    struct iser_data_buf *data,
			    enum iser_data_dir iser_dir,
			    enum dma_data_direction dma_dir)
{
	struct ib_device *dev;

	iser_task->dir[iser_dir] = 1;
	dev = iser_task->iser_conn->ib_conn.device->ib_device;

	data->dma_nents = ib_dma_map_sg(dev, data->sg, data->size, dma_dir);
	if (data->dma_nents == 0) {
		iser_err("dma_map_sg failed!!!\n");
		return -EINVAL;
	}
	return 0;
}

void iser_dma_unmap_task_data(struct iscsi_iser_task *iser_task,
			      struct iser_data_buf *data,
			      enum dma_data_direction dir)
{
	struct ib_device *dev;

	dev = iser_task->iser_conn->ib_conn.device->ib_device;
	ib_dma_unmap_sg(dev, data->sg, data->size, dir);
}

static int
iser_reg_dma(struct iser_device *device, struct iser_data_buf *mem,
	     struct iser_mem_reg *reg)
{
	struct scatterlist *sg = mem->sg;

	reg->sge.lkey = device->mr->lkey;
	reg->rkey = device->mr->rkey;
	reg->sge.addr = ib_sg_dma_address(device->ib_device, &sg[0]);
	reg->sge.length = ib_sg_dma_len(device->ib_device, &sg[0]);

	iser_dbg("Single DMA entry: lkey=0x%x, rkey=0x%x, addr=0x%llx,"
		 " length=0x%x\n", reg->sge.lkey, reg->rkey,
		 reg->sge.addr, reg->sge.length);

	return 0;
}

static int fall_to_bounce_buf(struct iscsi_iser_task *iser_task,
			      struct iser_data_buf *mem,
			      enum iser_data_dir cmd_dir,
			      int aligned_len)
{
	struct iscsi_conn *iscsi_conn = iser_task->iser_conn->iscsi_conn;
	struct iser_device *device = iser_task->iser_conn->ib_conn.device;

	iscsi_conn->fmr_unalign_cnt++;
	iser_warn("rdma alignment violation (%d/%d aligned) or FMR not supported\n",
		  aligned_len, mem->size);

	if (iser_debug_level > 0)
		iser_data_buf_dump(mem, device->ib_device);

	/* unmap the command data before accessing it */
	iser_dma_unmap_task_data(iser_task, mem,
				 (cmd_dir == ISER_DIR_OUT) ?
				 DMA_TO_DEVICE : DMA_FROM_DEVICE);

	/* allocate copy buf, if we are writing, copy the */
	/* unaligned scatterlist, dma map the copy        */
	if (iser_start_rdma_unaligned_sg(iser_task, mem, cmd_dir) != 0)
		return -ENOMEM;

	return 0;
}

/**
 * iser_reg_page_vec - Register physical memory
 *
 * returns: 0 on success, errno code on failure
 */
static
int iser_reg_page_vec(struct iscsi_iser_task *iser_task,
		      struct iser_data_buf *mem,
		      struct iser_page_vec *page_vec,
		      struct iser_mem_reg *mem_reg)
{
	struct ib_conn *ib_conn = &iser_task->iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;
	struct ib_pool_fmr *fmr;
	int ret, plen;

	plen = iser_sg_to_page_vec(mem, device->ib_device,
				   page_vec->pages,
				   &page_vec->offset,
				   &page_vec->data_size);
	page_vec->length = plen;
	if (plen * SIZE_4K < page_vec->data_size) {
		iser_err("page vec too short to hold this SG\n");
		iser_data_buf_dump(mem, device->ib_device);
		iser_dump_page_vec(page_vec);
		return -EINVAL;
	}

	fmr  = ib_fmr_pool_map_phys(ib_conn->fmr.pool,
				    page_vec->pages,
				    page_vec->length,
				    page_vec->pages[0]);
	if (IS_ERR(fmr)) {
		ret = PTR_ERR(fmr);
		iser_err("ib_fmr_pool_map_phys failed: %d\n", ret);
		return ret;
	}

	mem_reg->sge.lkey = fmr->fmr->lkey;
	mem_reg->rkey = fmr->fmr->rkey;
	mem_reg->sge.addr = page_vec->pages[0] + page_vec->offset;
	mem_reg->sge.length = page_vec->data_size;
	mem_reg->mem_h = fmr;

	return 0;
}

/**
 * Unregister (previosuly registered using FMR) memory.
 * If memory is non-FMR does nothing.
 */
void iser_unreg_mem_fmr(struct iscsi_iser_task *iser_task,
			enum iser_data_dir cmd_dir)
{
	struct iser_mem_reg *reg = &iser_task->rdma_reg[cmd_dir];
	int ret;

	if (!reg->mem_h)
		return;

	iser_dbg("PHYSICAL Mem.Unregister mem_h %p\n", reg->mem_h);

	ret = ib_fmr_pool_unmap((struct ib_pool_fmr *)reg->mem_h);
	if (ret)
		iser_err("ib_fmr_pool_unmap failed %d\n", ret);

	reg->mem_h = NULL;
}

void iser_unreg_mem_fastreg(struct iscsi_iser_task *iser_task,
			    enum iser_data_dir cmd_dir)
{
	struct iser_mem_reg *reg = &iser_task->rdma_reg[cmd_dir];

	if (!reg->mem_h)
		return;

	iser_reg_desc_put(&iser_task->iser_conn->ib_conn,
			  reg->mem_h);
	reg->mem_h = NULL;
}

/**
 * iser_reg_rdma_mem_fmr - Registers memory intended for RDMA,
 * using FMR (if possible) obtaining rkey and va
 *
 * returns 0 on success, errno code on failure
 */
int iser_reg_rdma_mem_fmr(struct iscsi_iser_task *iser_task,
			  enum iser_data_dir cmd_dir)
{
	struct ib_conn *ib_conn = &iser_task->iser_conn->ib_conn;
	struct iser_device   *device = ib_conn->device;
	struct ib_device     *ibdev = device->ib_device;
	struct iser_data_buf *mem = &iser_task->data[cmd_dir];
	struct iser_mem_reg *mem_reg;
	int aligned_len;
	int err;
	int i;

	mem_reg = &iser_task->rdma_reg[cmd_dir];

	aligned_len = iser_data_buf_aligned_len(mem, ibdev);
	if (aligned_len != mem->dma_nents) {
		err = fall_to_bounce_buf(iser_task, mem,
					 cmd_dir, aligned_len);
		if (err) {
			iser_err("failed to allocate bounce buffer\n");
			return err;
		}
	}

	/* if there a single dma entry, FMR is not needed */
	if (mem->dma_nents == 1) {
		return iser_reg_dma(device, mem, mem_reg);
	} else { /* use FMR for multiple dma entries */
		err = iser_reg_page_vec(iser_task, mem, ib_conn->fmr.page_vec,
					mem_reg);
		if (err && err != -EAGAIN) {
			iser_data_buf_dump(mem, ibdev);
			iser_err("mem->dma_nents = %d (dlength = 0x%x)\n",
				 mem->dma_nents,
				 ntoh24(iser_task->desc.iscsi_header.dlength));
			iser_err("page_vec: data_size = 0x%x, length = %d, offset = 0x%x\n",
				 ib_conn->fmr.page_vec->data_size,
				 ib_conn->fmr.page_vec->length,
				 ib_conn->fmr.page_vec->offset);
			for (i = 0; i < ib_conn->fmr.page_vec->length; i++)
				iser_err("page_vec[%d] = 0x%llx\n", i,
					 (unsigned long long)ib_conn->fmr.page_vec->pages[i]);
		}
		if (err)
			return err;
	}
	return 0;
}

static void
iser_set_dif_domain(struct scsi_cmnd *sc, struct ib_sig_attrs *sig_attrs,
		    struct ib_sig_domain *domain)
{
	domain->sig_type = IB_SIG_TYPE_T10_DIF;
	domain->sig.dif.pi_interval = scsi_prot_interval(sc);
	domain->sig.dif.ref_tag = scsi_prot_ref_tag(sc);
	/*
	 * At the moment we hard code those, but in the future
	 * we will take them from sc.
	 */
	domain->sig.dif.apptag_check_mask = 0xffff;
	domain->sig.dif.app_escape = true;
	domain->sig.dif.ref_escape = true;
	if (sc->prot_flags & SCSI_PROT_REF_INCREMENT)
		domain->sig.dif.ref_remap = true;
};

static int
iser_set_sig_attrs(struct scsi_cmnd *sc, struct ib_sig_attrs *sig_attrs)
{
	switch (scsi_get_prot_op(sc)) {
	case SCSI_PROT_WRITE_INSERT:
	case SCSI_PROT_READ_STRIP:
		sig_attrs->mem.sig_type = IB_SIG_TYPE_NONE;
		iser_set_dif_domain(sc, sig_attrs, &sig_attrs->wire);
		sig_attrs->wire.sig.dif.bg_type = IB_T10DIF_CRC;
		break;
	case SCSI_PROT_READ_INSERT:
	case SCSI_PROT_WRITE_STRIP:
		sig_attrs->wire.sig_type = IB_SIG_TYPE_NONE;
		iser_set_dif_domain(sc, sig_attrs, &sig_attrs->mem);
		sig_attrs->mem.sig.dif.bg_type = sc->prot_flags & SCSI_PROT_IP_CHECKSUM ?
						IB_T10DIF_CSUM : IB_T10DIF_CRC;
		break;
	case SCSI_PROT_READ_PASS:
	case SCSI_PROT_WRITE_PASS:
		iser_set_dif_domain(sc, sig_attrs, &sig_attrs->wire);
		sig_attrs->wire.sig.dif.bg_type = IB_T10DIF_CRC;
		iser_set_dif_domain(sc, sig_attrs, &sig_attrs->mem);
		sig_attrs->mem.sig.dif.bg_type = sc->prot_flags & SCSI_PROT_IP_CHECKSUM ?
						IB_T10DIF_CSUM : IB_T10DIF_CRC;
		break;
	default:
		iser_err("Unsupported PI operation %d\n",
			 scsi_get_prot_op(sc));
		return -EINVAL;
	}

	return 0;
}

static inline void
iser_set_prot_checks(struct scsi_cmnd *sc, u8 *mask)
{
	*mask = 0;
	if (sc->prot_flags & SCSI_PROT_REF_CHECK)
		*mask |= ISER_CHECK_REFTAG;
	if (sc->prot_flags & SCSI_PROT_GUARD_CHECK)
		*mask |= ISER_CHECK_GUARD;
}

static void
iser_inv_rkey(struct ib_send_wr *inv_wr, struct ib_mr *mr)
{
	u32 rkey;

	memset(inv_wr, 0, sizeof(*inv_wr));
	inv_wr->opcode = IB_WR_LOCAL_INV;
	inv_wr->wr_id = ISER_FASTREG_LI_WRID;
	inv_wr->ex.invalidate_rkey = mr->rkey;

	rkey = ib_inc_rkey(mr->rkey);
	ib_update_fast_reg_key(mr, rkey);
}

static int
iser_reg_sig_mr(struct iscsi_iser_task *iser_task,
		struct fast_reg_descriptor *desc,
		struct iser_mem_reg *data_reg,
		struct iser_mem_reg *prot_reg,
		struct iser_mem_reg *sig_reg)
{
	struct ib_conn *ib_conn = &iser_task->iser_conn->ib_conn;
	struct iser_pi_context *pi_ctx = desc->pi_ctx;
	struct ib_send_wr sig_wr, inv_wr;
	struct ib_send_wr *bad_wr, *wr = NULL;
	struct ib_sig_attrs sig_attrs;
	int ret;

	memset(&sig_attrs, 0, sizeof(sig_attrs));
	ret = iser_set_sig_attrs(iser_task->sc, &sig_attrs);
	if (ret)
		goto err;

	iser_set_prot_checks(iser_task->sc, &sig_attrs.check_mask);

	if (!(desc->reg_indicators & ISER_SIG_KEY_VALID)) {
		iser_inv_rkey(&inv_wr, pi_ctx->sig_mr);
		wr = &inv_wr;
	}

	memset(&sig_wr, 0, sizeof(sig_wr));
	sig_wr.opcode = IB_WR_REG_SIG_MR;
	sig_wr.wr_id = ISER_FASTREG_LI_WRID;
	sig_wr.sg_list = &data_reg->sge;
	sig_wr.num_sge = 1;
	sig_wr.wr.sig_handover.sig_attrs = &sig_attrs;
	sig_wr.wr.sig_handover.sig_mr = pi_ctx->sig_mr;
	if (scsi_prot_sg_count(iser_task->sc))
		sig_wr.wr.sig_handover.prot = &prot_reg->sge;
	sig_wr.wr.sig_handover.access_flags = IB_ACCESS_LOCAL_WRITE |
					      IB_ACCESS_REMOTE_READ |
					      IB_ACCESS_REMOTE_WRITE;

	if (!wr)
		wr = &sig_wr;
	else
		wr->next = &sig_wr;

	ret = ib_post_send(ib_conn->qp, wr, &bad_wr);
	if (ret) {
		iser_err("reg_sig_mr failed, ret:%d\n", ret);
		goto err;
	}
	desc->reg_indicators &= ~ISER_SIG_KEY_VALID;

	sig_reg->sge.lkey = pi_ctx->sig_mr->lkey;
	sig_reg->rkey = pi_ctx->sig_mr->rkey;
	sig_reg->sge.addr = 0;
	sig_reg->sge.length = scsi_transfer_length(iser_task->sc);

	iser_dbg("sig_sge: lkey: 0x%x, rkey: 0x%x, addr: 0x%llx, length: %u\n",
		 sig_reg->sge.lkey, sig_reg->rkey, sig_reg->sge.addr,
		 sig_reg->sge.length);
err:
	return ret;
}

static int iser_fast_reg_mr(struct iscsi_iser_task *iser_task,
			    struct iser_data_buf *mem,
			    struct fast_reg_descriptor *desc,
			    enum iser_reg_indicator ind,
			    struct iser_mem_reg *reg)
{
	struct ib_conn *ib_conn = &iser_task->iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;
	struct ib_mr *mr;
	struct ib_fast_reg_page_list *frpl;
	struct ib_send_wr fastreg_wr, inv_wr;
	struct ib_send_wr *bad_wr, *wr = NULL;
	int ret, offset, size, plen;

	/* if there a single dma entry, dma mr suffices */
	if (mem->dma_nents == 1)
		return iser_reg_dma(device, mem, reg);

	if (ind == ISER_DATA_KEY_VALID) {
		mr = desc->data_mr;
		frpl = desc->data_frpl;
	} else {
		mr = desc->pi_ctx->prot_mr;
		frpl = desc->pi_ctx->prot_frpl;
	}

	plen = iser_sg_to_page_vec(mem, device->ib_device, frpl->page_list,
				   &offset, &size);
	if (plen * SIZE_4K < size) {
		iser_err("fast reg page_list too short to hold this SG\n");
		return -EINVAL;
	}

	if (!(desc->reg_indicators & ind)) {
		iser_inv_rkey(&inv_wr, mr);
		wr = &inv_wr;
	}

	/* Prepare FASTREG WR */
	memset(&fastreg_wr, 0, sizeof(fastreg_wr));
	fastreg_wr.wr_id = ISER_FASTREG_LI_WRID;
	fastreg_wr.opcode = IB_WR_FAST_REG_MR;
	fastreg_wr.wr.fast_reg.iova_start = frpl->page_list[0] + offset;
	fastreg_wr.wr.fast_reg.page_list = frpl;
	fastreg_wr.wr.fast_reg.page_list_len = plen;
	fastreg_wr.wr.fast_reg.page_shift = SHIFT_4K;
	fastreg_wr.wr.fast_reg.length = size;
	fastreg_wr.wr.fast_reg.rkey = mr->rkey;
	fastreg_wr.wr.fast_reg.access_flags = (IB_ACCESS_LOCAL_WRITE  |
					       IB_ACCESS_REMOTE_WRITE |
					       IB_ACCESS_REMOTE_READ);

	if (!wr)
		wr = &fastreg_wr;
	else
		wr->next = &fastreg_wr;

	ret = ib_post_send(ib_conn->qp, wr, &bad_wr);
	if (ret) {
		iser_err("fast registration failed, ret:%d\n", ret);
		return ret;
	}
	desc->reg_indicators &= ~ind;

	reg->sge.lkey = mr->lkey;
	reg->rkey = mr->rkey;
	reg->sge.addr = frpl->page_list[0] + offset;
	reg->sge.length = size;

	return ret;
}

/**
 * iser_reg_rdma_mem_fastreg - Registers memory intended for RDMA,
 * using Fast Registration WR (if possible) obtaining rkey and va
 *
 * returns 0 on success, errno code on failure
 */
int iser_reg_rdma_mem_fastreg(struct iscsi_iser_task *iser_task,
			      enum iser_data_dir cmd_dir)
{
	struct ib_conn *ib_conn = &iser_task->iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;
	struct ib_device *ibdev = device->ib_device;
	struct iser_data_buf *mem = &iser_task->data[cmd_dir];
	struct iser_mem_reg *mem_reg = &iser_task->rdma_reg[cmd_dir];
	struct fast_reg_descriptor *desc = NULL;
	int err, aligned_len;

	aligned_len = iser_data_buf_aligned_len(mem, ibdev);
	if (aligned_len != mem->dma_nents) {
		err = fall_to_bounce_buf(iser_task, mem,
					 cmd_dir, aligned_len);
		if (err) {
			iser_err("failed to allocate bounce buffer\n");
			return err;
		}
	}

	if (mem->dma_nents != 1 ||
	    scsi_get_prot_op(iser_task->sc) != SCSI_PROT_NORMAL) {
		desc = iser_reg_desc_get(ib_conn);
		mem_reg->mem_h = desc;
	}

	err = iser_fast_reg_mr(iser_task, mem, desc,
			       ISER_DATA_KEY_VALID, mem_reg);
	if (err)
		goto err_reg;

	if (scsi_get_prot_op(iser_task->sc) != SCSI_PROT_NORMAL) {
		struct iser_mem_reg prot_reg;

		memset(&prot_reg, 0, sizeof(prot_reg));
		if (scsi_prot_sg_count(iser_task->sc)) {
			mem = &iser_task->prot[cmd_dir];
			aligned_len = iser_data_buf_aligned_len(mem, ibdev);
			if (aligned_len != mem->dma_nents) {
				err = fall_to_bounce_buf(iser_task, mem,
							 cmd_dir, aligned_len);
				if (err) {
					iser_err("failed to allocate bounce buffer\n");
					return err;
				}
			}

			err = iser_fast_reg_mr(iser_task, mem, desc,
					       ISER_PROT_KEY_VALID, &prot_reg);
			if (err)
				goto err_reg;
		}

		err = iser_reg_sig_mr(iser_task, desc, mem_reg,
				      &prot_reg, mem_reg);
		if (err) {
			iser_err("Failed to register signature mr\n");
			return err;
		}
		desc->reg_indicators |= ISER_FASTREG_PROTECTED;
	}

	return 0;
err_reg:
	if (desc)
		iser_reg_desc_put(ib_conn, desc);

	return err;
}
