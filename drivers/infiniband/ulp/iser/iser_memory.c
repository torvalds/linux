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

#define ISER_KMALLOC_THRESHOLD 0x20000 /* 128K - kmalloc limit */

/**
 * iser_start_rdma_unaligned_sg
 */
static int iser_start_rdma_unaligned_sg(struct iscsi_iser_task *iser_task,
					struct iser_data_buf *data,
					struct iser_data_buf *data_copy,
					enum iser_data_dir cmd_dir)
{
	struct ib_device *dev = iser_task->iser_conn->ib_conn.device->ib_device;
	struct scatterlist *sgl = (struct scatterlist *)data->buf;
	struct scatterlist *sg;
	char *mem = NULL;
	unsigned long  cmd_data_len = 0;
	int dma_nents, i;

	for_each_sg(sgl, sg, data->size, i)
		cmd_data_len += ib_sg_dma_len(dev, sg);

	if (cmd_data_len > ISER_KMALLOC_THRESHOLD)
		mem = (void *)__get_free_pages(GFP_ATOMIC,
		      ilog2(roundup_pow_of_two(cmd_data_len)) - PAGE_SHIFT);
	else
		mem = kmalloc(cmd_data_len, GFP_ATOMIC);

	if (mem == NULL) {
		iser_err("Failed to allocate mem size %d %d for copying sglist\n",
			 data->size, (int)cmd_data_len);
		return -ENOMEM;
	}

	if (cmd_dir == ISER_DIR_OUT) {
		/* copy the unaligned sg the buffer which is used for RDMA */
		int i;
		char *p, *from;

		sgl = (struct scatterlist *)data->buf;
		p = mem;
		for_each_sg(sgl, sg, data->size, i) {
			from = kmap_atomic(sg_page(sg));
			memcpy(p,
			       from + sg->offset,
			       sg->length);
			kunmap_atomic(from);
			p += sg->length;
		}
	}

	sg_init_one(&data_copy->sg_single, mem, cmd_data_len);
	data_copy->buf = &data_copy->sg_single;
	data_copy->size = 1;
	data_copy->copy_buf = mem;

	dma_nents = ib_dma_map_sg(dev, &data_copy->sg_single, 1,
				  (cmd_dir == ISER_DIR_OUT) ?
				  DMA_TO_DEVICE : DMA_FROM_DEVICE);
	BUG_ON(dma_nents == 0);

	data_copy->dma_nents = dma_nents;
	data_copy->data_len = cmd_data_len;

	return 0;
}

/**
 * iser_finalize_rdma_unaligned_sg
 */

void iser_finalize_rdma_unaligned_sg(struct iscsi_iser_task *iser_task,
				     struct iser_data_buf *data,
				     struct iser_data_buf *data_copy,
				     enum iser_data_dir cmd_dir)
{
	struct ib_device *dev;
	unsigned long  cmd_data_len;

	dev = iser_task->iser_conn->ib_conn.device->ib_device;

	ib_dma_unmap_sg(dev, &data_copy->sg_single, 1,
			(cmd_dir == ISER_DIR_OUT) ?
			DMA_TO_DEVICE : DMA_FROM_DEVICE);

	if (cmd_dir == ISER_DIR_IN) {
		char *mem;
		struct scatterlist *sgl, *sg;
		unsigned char *p, *to;
		unsigned int sg_size;
		int i;

		/* copy back read RDMA to unaligned sg */
		mem = data_copy->copy_buf;

		sgl = (struct scatterlist *)data->buf;
		sg_size = data->size;

		p = mem;
		for_each_sg(sgl, sg, sg_size, i) {
			to = kmap_atomic(sg_page(sg));
			memcpy(to + sg->offset,
			       p,
			       sg->length);
			kunmap_atomic(to);
			p += sg->length;
		}
	}

	cmd_data_len = data->data_len;

	if (cmd_data_len > ISER_KMALLOC_THRESHOLD)
		free_pages((unsigned long)data_copy->copy_buf,
			   ilog2(roundup_pow_of_two(cmd_data_len)) - PAGE_SHIFT);
	else
		kfree(data_copy->copy_buf);

	data_copy->copy_buf = NULL;
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
	struct scatterlist *sg, *sgl = (struct scatterlist *)data->buf;
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
	struct scatterlist *sgl, *sg, *next_sg = NULL;
	u64 start_addr, end_addr;
	int i, ret_len, start_check = 0;

	if (data->dma_nents == 1)
		return 1;

	sgl = (struct scatterlist *)data->buf;
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
	struct scatterlist *sgl = (struct scatterlist *)data->buf;
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, data->dma_nents, i)
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

static void iser_page_vec_build(struct iser_data_buf *data,
				struct iser_page_vec *page_vec,
				struct ib_device *ibdev)
{
	int page_vec_len = 0;

	page_vec->length = 0;
	page_vec->offset = 0;

	iser_dbg("Translating sg sz: %d\n", data->dma_nents);
	page_vec_len = iser_sg_to_page_vec(data, ibdev, page_vec->pages,
					   &page_vec->offset,
					   &page_vec->data_size);
	iser_dbg("sg len %d page_vec_len %d\n", data->dma_nents, page_vec_len);

	page_vec->length = page_vec_len;

	if (page_vec_len * SIZE_4K < page_vec->data_size) {
		iser_err("page_vec too short to hold this SG\n");
		iser_data_buf_dump(data, ibdev);
		iser_dump_page_vec(page_vec);
		BUG();
	}
}

int iser_dma_map_task_data(struct iscsi_iser_task *iser_task,
			    struct iser_data_buf *data,
			    enum iser_data_dir iser_dir,
			    enum dma_data_direction dma_dir)
{
	struct ib_device *dev;

	iser_task->dir[iser_dir] = 1;
	dev = iser_task->iser_conn->ib_conn.device->ib_device;

	data->dma_nents = ib_dma_map_sg(dev, data->buf, data->size, dma_dir);
	if (data->dma_nents == 0) {
		iser_err("dma_map_sg failed!!!\n");
		return -EINVAL;
	}
	return 0;
}

void iser_dma_unmap_task_data(struct iscsi_iser_task *iser_task,
			      struct iser_data_buf *data)
{
	struct ib_device *dev;

	dev = iser_task->iser_conn->ib_conn.device->ib_device;
	ib_dma_unmap_sg(dev, data->buf, data->size, DMA_FROM_DEVICE);
}

static int fall_to_bounce_buf(struct iscsi_iser_task *iser_task,
			      struct ib_device *ibdev,
			      struct iser_data_buf *mem,
			      struct iser_data_buf *mem_copy,
			      enum iser_data_dir cmd_dir,
			      int aligned_len)
{
	struct iscsi_conn    *iscsi_conn = iser_task->iser_conn->iscsi_conn;

	iscsi_conn->fmr_unalign_cnt++;
	iser_warn("rdma alignment violation (%d/%d aligned) or FMR not supported\n",
		  aligned_len, mem->size);

	if (iser_debug_level > 0)
		iser_data_buf_dump(mem, ibdev);

	/* unmap the command data before accessing it */
	iser_dma_unmap_task_data(iser_task, mem);

	/* allocate copy buf, if we are writing, copy the */
	/* unaligned scatterlist, dma map the copy        */
	if (iser_start_rdma_unaligned_sg(iser_task, mem, mem_copy, cmd_dir) != 0)
		return -ENOMEM;

	return 0;
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
	struct iser_regd_buf *regd_buf;
	int aligned_len;
	int err;
	int i;
	struct scatterlist *sg;

	regd_buf = &iser_task->rdma_regd[cmd_dir];

	aligned_len = iser_data_buf_aligned_len(mem, ibdev);
	if (aligned_len != mem->dma_nents) {
		err = fall_to_bounce_buf(iser_task, ibdev, mem,
					 &iser_task->data_copy[cmd_dir],
					 cmd_dir, aligned_len);
		if (err) {
			iser_err("failed to allocate bounce buffer\n");
			return err;
		}
		mem = &iser_task->data_copy[cmd_dir];
	}

	/* if there a single dma entry, FMR is not needed */
	if (mem->dma_nents == 1) {
		sg = (struct scatterlist *)mem->buf;

		regd_buf->reg.lkey = device->mr->lkey;
		regd_buf->reg.rkey = device->mr->rkey;
		regd_buf->reg.len  = ib_sg_dma_len(ibdev, &sg[0]);
		regd_buf->reg.va   = ib_sg_dma_address(ibdev, &sg[0]);
		regd_buf->reg.is_mr = 0;

		iser_dbg("PHYSICAL Mem.register: lkey: 0x%08X rkey: 0x%08X  "
			 "va: 0x%08lX sz: %ld]\n",
			 (unsigned int)regd_buf->reg.lkey,
			 (unsigned int)regd_buf->reg.rkey,
			 (unsigned long)regd_buf->reg.va,
			 (unsigned long)regd_buf->reg.len);
	} else { /* use FMR for multiple dma entries */
		iser_page_vec_build(mem, ib_conn->fmr.page_vec, ibdev);
		err = iser_reg_page_vec(ib_conn, ib_conn->fmr.page_vec,
					&regd_buf->reg);
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

static inline enum ib_t10_dif_type
scsi2ib_prot_type(unsigned char prot_type)
{
	switch (prot_type) {
	case SCSI_PROT_DIF_TYPE0:
		return IB_T10DIF_NONE;
	case SCSI_PROT_DIF_TYPE1:
		return IB_T10DIF_TYPE1;
	case SCSI_PROT_DIF_TYPE2:
		return IB_T10DIF_TYPE2;
	case SCSI_PROT_DIF_TYPE3:
		return IB_T10DIF_TYPE3;
	default:
		return IB_T10DIF_NONE;
	}
}


static int
iser_set_sig_attrs(struct scsi_cmnd *sc, struct ib_sig_attrs *sig_attrs)
{
	unsigned char scsi_ptype = scsi_get_prot_type(sc);

	sig_attrs->mem.sig_type = IB_SIG_TYPE_T10_DIF;
	sig_attrs->wire.sig_type = IB_SIG_TYPE_T10_DIF;
	sig_attrs->mem.sig.dif.pi_interval = sc->device->sector_size;
	sig_attrs->wire.sig.dif.pi_interval = sc->device->sector_size;

	switch (scsi_get_prot_op(sc)) {
	case SCSI_PROT_WRITE_INSERT:
	case SCSI_PROT_READ_STRIP:
		sig_attrs->mem.sig.dif.type = IB_T10DIF_NONE;
		sig_attrs->wire.sig.dif.type = scsi2ib_prot_type(scsi_ptype);
		sig_attrs->wire.sig.dif.bg_type = IB_T10DIF_CRC;
		sig_attrs->wire.sig.dif.ref_tag = scsi_get_lba(sc) &
						  0xffffffff;
		break;
	case SCSI_PROT_READ_INSERT:
	case SCSI_PROT_WRITE_STRIP:
		sig_attrs->mem.sig.dif.type = scsi2ib_prot_type(scsi_ptype);
		sig_attrs->mem.sig.dif.bg_type = IB_T10DIF_CRC;
		sig_attrs->mem.sig.dif.ref_tag = scsi_get_lba(sc) &
						 0xffffffff;
		sig_attrs->wire.sig.dif.type = IB_T10DIF_NONE;
		break;
	case SCSI_PROT_READ_PASS:
	case SCSI_PROT_WRITE_PASS:
		sig_attrs->mem.sig.dif.type = scsi2ib_prot_type(scsi_ptype);
		sig_attrs->mem.sig.dif.bg_type = IB_T10DIF_CRC;
		sig_attrs->mem.sig.dif.ref_tag = scsi_get_lba(sc) &
						 0xffffffff;
		sig_attrs->wire.sig.dif.type = scsi2ib_prot_type(scsi_ptype);
		sig_attrs->wire.sig.dif.bg_type = IB_T10DIF_CRC;
		sig_attrs->wire.sig.dif.ref_tag = scsi_get_lba(sc) &
						  0xffffffff;
		break;
	default:
		iser_err("Unsupported PI operation %d\n",
			 scsi_get_prot_op(sc));
		return -EINVAL;
	}
	return 0;
}


static int
iser_set_prot_checks(struct scsi_cmnd *sc, u8 *mask)
{
	switch (scsi_get_prot_type(sc)) {
	case SCSI_PROT_DIF_TYPE0:
		*mask = 0x0;
		break;
	case SCSI_PROT_DIF_TYPE1:
	case SCSI_PROT_DIF_TYPE2:
		*mask = ISER_CHECK_GUARD | ISER_CHECK_REFTAG;
		break;
	case SCSI_PROT_DIF_TYPE3:
		*mask = ISER_CHECK_GUARD;
		break;
	default:
		iser_err("Unsupported protection type %d\n",
			 scsi_get_prot_type(sc));
		return -EINVAL;
	}

	return 0;
}

static int
iser_reg_sig_mr(struct iscsi_iser_task *iser_task,
		struct fast_reg_descriptor *desc, struct ib_sge *data_sge,
		struct ib_sge *prot_sge, struct ib_sge *sig_sge)
{
	struct ib_conn *ib_conn = &iser_task->iser_conn->ib_conn;
	struct iser_pi_context *pi_ctx = desc->pi_ctx;
	struct ib_send_wr sig_wr, inv_wr;
	struct ib_send_wr *bad_wr, *wr = NULL;
	struct ib_sig_attrs sig_attrs;
	int ret;
	u32 key;

	memset(&sig_attrs, 0, sizeof(sig_attrs));
	ret = iser_set_sig_attrs(iser_task->sc, &sig_attrs);
	if (ret)
		goto err;

	ret = iser_set_prot_checks(iser_task->sc, &sig_attrs.check_mask);
	if (ret)
		goto err;

	if (!(desc->reg_indicators & ISER_SIG_KEY_VALID)) {
		memset(&inv_wr, 0, sizeof(inv_wr));
		inv_wr.opcode = IB_WR_LOCAL_INV;
		inv_wr.wr_id = ISER_FASTREG_LI_WRID;
		inv_wr.ex.invalidate_rkey = pi_ctx->sig_mr->rkey;
		wr = &inv_wr;
		/* Bump the key */
		key = (u8)(pi_ctx->sig_mr->rkey & 0x000000FF);
		ib_update_fast_reg_key(pi_ctx->sig_mr, ++key);
	}

	memset(&sig_wr, 0, sizeof(sig_wr));
	sig_wr.opcode = IB_WR_REG_SIG_MR;
	sig_wr.wr_id = ISER_FASTREG_LI_WRID;
	sig_wr.sg_list = data_sge;
	sig_wr.num_sge = 1;
	sig_wr.wr.sig_handover.sig_attrs = &sig_attrs;
	sig_wr.wr.sig_handover.sig_mr = pi_ctx->sig_mr;
	if (scsi_prot_sg_count(iser_task->sc))
		sig_wr.wr.sig_handover.prot = prot_sge;
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

	sig_sge->lkey = pi_ctx->sig_mr->lkey;
	sig_sge->addr = 0;
	sig_sge->length = data_sge->length + prot_sge->length;
	if (scsi_get_prot_op(iser_task->sc) == SCSI_PROT_WRITE_INSERT ||
	    scsi_get_prot_op(iser_task->sc) == SCSI_PROT_READ_STRIP) {
		sig_sge->length += (data_sge->length /
				   iser_task->sc->device->sector_size) * 8;
	}

	iser_dbg("sig_sge: addr: 0x%llx  length: %u lkey: 0x%x\n",
		 sig_sge->addr, sig_sge->length,
		 sig_sge->lkey);
err:
	return ret;
}

static int iser_fast_reg_mr(struct iscsi_iser_task *iser_task,
			    struct iser_regd_buf *regd_buf,
			    struct iser_data_buf *mem,
			    enum iser_reg_indicator ind,
			    struct ib_sge *sge)
{
	struct fast_reg_descriptor *desc = regd_buf->reg.mem_h;
	struct ib_conn *ib_conn = &iser_task->iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;
	struct ib_device *ibdev = device->ib_device;
	struct ib_mr *mr;
	struct ib_fast_reg_page_list *frpl;
	struct ib_send_wr fastreg_wr, inv_wr;
	struct ib_send_wr *bad_wr, *wr = NULL;
	u8 key;
	int ret, offset, size, plen;

	/* if there a single dma entry, dma mr suffices */
	if (mem->dma_nents == 1) {
		struct scatterlist *sg = (struct scatterlist *)mem->buf;

		sge->lkey = device->mr->lkey;
		sge->addr   = ib_sg_dma_address(ibdev, &sg[0]);
		sge->length  = ib_sg_dma_len(ibdev, &sg[0]);

		iser_dbg("Single DMA entry: lkey=0x%x, addr=0x%llx, length=0x%x\n",
			 sge->lkey, sge->addr, sge->length);
		return 0;
	}

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
		memset(&inv_wr, 0, sizeof(inv_wr));
		inv_wr.wr_id = ISER_FASTREG_LI_WRID;
		inv_wr.opcode = IB_WR_LOCAL_INV;
		inv_wr.ex.invalidate_rkey = mr->rkey;
		wr = &inv_wr;
		/* Bump the key */
		key = (u8)(mr->rkey & 0x000000FF);
		ib_update_fast_reg_key(mr, ++key);
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

	sge->lkey = mr->lkey;
	sge->addr = frpl->page_list[0] + offset;
	sge->length = size;

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
	struct iser_regd_buf *regd_buf = &iser_task->rdma_regd[cmd_dir];
	struct fast_reg_descriptor *desc = NULL;
	struct ib_sge data_sge;
	int err, aligned_len;
	unsigned long flags;

	aligned_len = iser_data_buf_aligned_len(mem, ibdev);
	if (aligned_len != mem->dma_nents) {
		err = fall_to_bounce_buf(iser_task, ibdev, mem,
					 &iser_task->data_copy[cmd_dir],
					 cmd_dir, aligned_len);
		if (err) {
			iser_err("failed to allocate bounce buffer\n");
			return err;
		}
		mem = &iser_task->data_copy[cmd_dir];
	}

	if (mem->dma_nents != 1 ||
	    scsi_get_prot_op(iser_task->sc) != SCSI_PROT_NORMAL) {
		spin_lock_irqsave(&ib_conn->lock, flags);
		desc = list_first_entry(&ib_conn->fastreg.pool,
					struct fast_reg_descriptor, list);
		list_del(&desc->list);
		spin_unlock_irqrestore(&ib_conn->lock, flags);
		regd_buf->reg.mem_h = desc;
	}

	err = iser_fast_reg_mr(iser_task, regd_buf, mem,
			       ISER_DATA_KEY_VALID, &data_sge);
	if (err)
		goto err_reg;

	if (scsi_get_prot_op(iser_task->sc) != SCSI_PROT_NORMAL) {
		struct ib_sge prot_sge, sig_sge;

		memset(&prot_sge, 0, sizeof(prot_sge));
		if (scsi_prot_sg_count(iser_task->sc)) {
			mem = &iser_task->prot[cmd_dir];
			aligned_len = iser_data_buf_aligned_len(mem, ibdev);
			if (aligned_len != mem->dma_nents) {
				err = fall_to_bounce_buf(iser_task, ibdev, mem,
							 &iser_task->prot_copy[cmd_dir],
							 cmd_dir, aligned_len);
				if (err) {
					iser_err("failed to allocate bounce buffer\n");
					return err;
				}
				mem = &iser_task->prot_copy[cmd_dir];
			}

			err = iser_fast_reg_mr(iser_task, regd_buf, mem,
					       ISER_PROT_KEY_VALID, &prot_sge);
			if (err)
				goto err_reg;
		}

		err = iser_reg_sig_mr(iser_task, desc, &data_sge,
				      &prot_sge, &sig_sge);
		if (err) {
			iser_err("Failed to register signature mr\n");
			return err;
		}
		desc->reg_indicators |= ISER_FASTREG_PROTECTED;

		regd_buf->reg.lkey = sig_sge.lkey;
		regd_buf->reg.rkey = desc->pi_ctx->sig_mr->rkey;
		regd_buf->reg.va = sig_sge.addr;
		regd_buf->reg.len = sig_sge.length;
		regd_buf->reg.is_mr = 1;
	} else {
		if (desc) {
			regd_buf->reg.rkey = desc->data_mr->rkey;
			regd_buf->reg.is_mr = 1;
		} else {
			regd_buf->reg.rkey = device->mr->rkey;
			regd_buf->reg.is_mr = 0;
		}

		regd_buf->reg.lkey = data_sge.lkey;
		regd_buf->reg.va = data_sge.addr;
		regd_buf->reg.len = data_sge.length;
	}

	return 0;
err_reg:
	if (desc) {
		spin_lock_irqsave(&ib_conn->lock, flags);
		list_add_tail(&desc->list, &ib_conn->fastreg.pool);
		spin_unlock_irqrestore(&ib_conn->lock, flags);
	}

	return err;
}
