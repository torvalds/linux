/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
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

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/unaligned.h>
#include <asm/page.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_fc.h>

#include "csio_hw.h"
#include "csio_lnode.h"
#include "csio_rnode.h"
#include "csio_scsi.h"
#include "csio_init.h"

int csio_scsi_eqsize = 65536;
int csio_scsi_iqlen = 128;
int csio_scsi_ioreqs = 2048;
uint32_t csio_max_scan_tmo;
uint32_t csio_delta_scan_tmo = 5;
int csio_lun_qdepth = 32;

static int csio_ddp_descs = 128;

static int csio_do_abrt_cls(struct csio_hw *,
				      struct csio_ioreq *, bool);

static void csio_scsis_uninit(struct csio_ioreq *, enum csio_scsi_ev);
static void csio_scsis_io_active(struct csio_ioreq *, enum csio_scsi_ev);
static void csio_scsis_tm_active(struct csio_ioreq *, enum csio_scsi_ev);
static void csio_scsis_aborting(struct csio_ioreq *, enum csio_scsi_ev);
static void csio_scsis_closing(struct csio_ioreq *, enum csio_scsi_ev);
static void csio_scsis_shost_cmpl_await(struct csio_ioreq *, enum csio_scsi_ev);

/*
 * csio_scsi_match_io - Match an ioreq with the given SCSI level data.
 * @ioreq: The I/O request
 * @sld: Level information
 *
 * Should be called with lock held.
 *
 */
static bool
csio_scsi_match_io(struct csio_ioreq *ioreq, struct csio_scsi_level_data *sld)
{
	struct scsi_cmnd *scmnd = csio_scsi_cmnd(ioreq);

	switch (sld->level) {
	case CSIO_LEV_LUN:
		if (scmnd == NULL)
			return false;

		return ((ioreq->lnode == sld->lnode) &&
			(ioreq->rnode == sld->rnode) &&
			((uint64_t)scmnd->device->lun == sld->oslun));

	case CSIO_LEV_RNODE:
		return ((ioreq->lnode == sld->lnode) &&
				(ioreq->rnode == sld->rnode));
	case CSIO_LEV_LNODE:
		return (ioreq->lnode == sld->lnode);
	case CSIO_LEV_ALL:
		return true;
	default:
		return false;
	}
}

/*
 * csio_scsi_gather_active_ios - Gather active I/Os based on level
 * @scm: SCSI module
 * @sld: Level information
 * @dest: The queue where these I/Os have to be gathered.
 *
 * Should be called with lock held.
 */
static void
csio_scsi_gather_active_ios(struct csio_scsim *scm,
			    struct csio_scsi_level_data *sld,
			    struct list_head *dest)
{
	struct list_head *tmp, *next;

	if (list_empty(&scm->active_q))
		return;

	/* Just splice the entire active_q into dest */
	if (sld->level == CSIO_LEV_ALL) {
		list_splice_tail_init(&scm->active_q, dest);
		return;
	}

	list_for_each_safe(tmp, next, &scm->active_q) {
		if (csio_scsi_match_io((struct csio_ioreq *)tmp, sld)) {
			list_del_init(tmp);
			list_add_tail(tmp, dest);
		}
	}
}

static inline bool
csio_scsi_itnexus_loss_error(uint16_t error)
{
	switch (error) {
	case FW_ERR_LINK_DOWN:
	case FW_RDEV_NOT_READY:
	case FW_ERR_RDEV_LOST:
	case FW_ERR_RDEV_LOGO:
	case FW_ERR_RDEV_IMPL_LOGO:
		return true;
	}
	return false;
}

/*
 * csio_scsi_fcp_cmnd - Frame the SCSI FCP command paylod.
 * @req: IO req structure.
 * @addr: DMA location to place the payload.
 *
 * This routine is shared between FCP_WRITE, FCP_READ and FCP_CMD requests.
 */
static inline void
csio_scsi_fcp_cmnd(struct csio_ioreq *req, void *addr)
{
	struct fcp_cmnd *fcp_cmnd = (struct fcp_cmnd *)addr;
	struct scsi_cmnd *scmnd = csio_scsi_cmnd(req);

	/* Check for Task Management */
	if (likely(csio_priv(scmnd)->fc_tm_flags == 0)) {
		int_to_scsilun(scmnd->device->lun, &fcp_cmnd->fc_lun);
		fcp_cmnd->fc_tm_flags = 0;
		fcp_cmnd->fc_cmdref = 0;

		memcpy(fcp_cmnd->fc_cdb, scmnd->cmnd, 16);
		fcp_cmnd->fc_pri_ta = FCP_PTA_SIMPLE;
		fcp_cmnd->fc_dl = cpu_to_be32(scsi_bufflen(scmnd));

		if (req->nsge)
			if (req->datadir == DMA_TO_DEVICE)
				fcp_cmnd->fc_flags = FCP_CFL_WRDATA;
			else
				fcp_cmnd->fc_flags = FCP_CFL_RDDATA;
		else
			fcp_cmnd->fc_flags = 0;
	} else {
		memset(fcp_cmnd, 0, sizeof(*fcp_cmnd));
		int_to_scsilun(scmnd->device->lun, &fcp_cmnd->fc_lun);
		fcp_cmnd->fc_tm_flags = csio_priv(scmnd)->fc_tm_flags;
	}
}

/*
 * csio_scsi_init_cmd_wr - Initialize the SCSI CMD WR.
 * @req: IO req structure.
 * @addr: DMA location to place the payload.
 * @size: Size of WR (including FW WR + immed data + rsp SG entry
 *
 * Wrapper for populating fw_scsi_cmd_wr.
 */
static inline void
csio_scsi_init_cmd_wr(struct csio_ioreq *req, void *addr, uint32_t size)
{
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_rnode *rn = req->rnode;
	struct fw_scsi_cmd_wr *wr = (struct fw_scsi_cmd_wr *)addr;
	struct csio_dma_buf *dma_buf;
	uint8_t imm = csio_hw_to_scsim(hw)->proto_cmd_len;

	wr->op_immdlen = cpu_to_be32(FW_WR_OP_V(FW_SCSI_CMD_WR) |
					  FW_SCSI_CMD_WR_IMMDLEN(imm));
	wr->flowid_len16 = cpu_to_be32(FW_WR_FLOWID_V(rn->flowid) |
					    FW_WR_LEN16_V(
						DIV_ROUND_UP(size, 16)));

	wr->cookie = (uintptr_t) req;
	wr->iqid = cpu_to_be16(csio_q_physiqid(hw, req->iq_idx));
	wr->tmo_val = (uint8_t) req->tmo;
	wr->r3 = 0;
	memset(&wr->r5, 0, 8);

	/* Get RSP DMA buffer */
	dma_buf = &req->dma_buf;

	/* Prepare RSP SGL */
	wr->rsp_dmalen = cpu_to_be32(dma_buf->len);
	wr->rsp_dmaaddr = cpu_to_be64(dma_buf->paddr);

	wr->r6 = 0;

	wr->u.fcoe.ctl_pri = 0;
	wr->u.fcoe.cp_en_class = 0;
	wr->u.fcoe.r4_lo[0] = 0;
	wr->u.fcoe.r4_lo[1] = 0;

	/* Frame a FCP command */
	csio_scsi_fcp_cmnd(req, (void *)((uintptr_t)addr +
				    sizeof(struct fw_scsi_cmd_wr)));
}

#define CSIO_SCSI_CMD_WR_SZ(_imm)					\
	(sizeof(struct fw_scsi_cmd_wr) +		/* WR size */	\
	 ALIGN((_imm), 16))				/* Immed data */

#define CSIO_SCSI_CMD_WR_SZ_16(_imm)					\
			(ALIGN(CSIO_SCSI_CMD_WR_SZ((_imm)), 16))

/*
 * csio_scsi_cmd - Create a SCSI CMD WR.
 * @req: IO req structure.
 *
 * Gets a WR slot in the ingress queue and initializes it with SCSI CMD WR.
 *
 */
static inline void
csio_scsi_cmd(struct csio_ioreq *req)
{
	struct csio_wr_pair wrp;
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_scsim *scsim = csio_hw_to_scsim(hw);
	uint32_t size = CSIO_SCSI_CMD_WR_SZ_16(scsim->proto_cmd_len);

	req->drv_status = csio_wr_get(hw, req->eq_idx, size, &wrp);
	if (unlikely(req->drv_status != 0))
		return;

	if (wrp.size1 >= size) {
		/* Initialize WR in one shot */
		csio_scsi_init_cmd_wr(req, wrp.addr1, size);
	} else {
		uint8_t *tmpwr = csio_q_eq_wrap(hw, req->eq_idx);

		/*
		 * Make a temporary copy of the WR and write back
		 * the copy into the WR pair.
		 */
		csio_scsi_init_cmd_wr(req, (void *)tmpwr, size);
		memcpy(wrp.addr1, tmpwr, wrp.size1);
		memcpy(wrp.addr2, tmpwr + wrp.size1, size - wrp.size1);
	}
}

/*
 * csio_scsi_init_ulptx_dsgl - Fill in a ULP_TX_SC_DSGL
 * @hw: HW module
 * @req: IO request
 * @sgl: ULP TX SGL pointer.
 *
 */
static inline void
csio_scsi_init_ultptx_dsgl(struct csio_hw *hw, struct csio_ioreq *req,
			   struct ulptx_sgl *sgl)
{
	struct ulptx_sge_pair *sge_pair = NULL;
	struct scatterlist *sgel;
	uint32_t i = 0;
	uint32_t xfer_len;
	struct list_head *tmp;
	struct csio_dma_buf *dma_buf;
	struct scsi_cmnd *scmnd = csio_scsi_cmnd(req);

	sgl->cmd_nsge = htonl(ULPTX_CMD_V(ULP_TX_SC_DSGL) | ULPTX_MORE_F |
				     ULPTX_NSGE_V(req->nsge));
	/* Now add the data SGLs */
	if (likely(!req->dcopy)) {
		scsi_for_each_sg(scmnd, sgel, req->nsge, i) {
			if (i == 0) {
				sgl->addr0 = cpu_to_be64(sg_dma_address(sgel));
				sgl->len0 = cpu_to_be32(sg_dma_len(sgel));
				sge_pair = (struct ulptx_sge_pair *)(sgl + 1);
				continue;
			}
			if ((i - 1) & 0x1) {
				sge_pair->addr[1] = cpu_to_be64(
							sg_dma_address(sgel));
				sge_pair->len[1] = cpu_to_be32(
							sg_dma_len(sgel));
				sge_pair++;
			} else {
				sge_pair->addr[0] = cpu_to_be64(
							sg_dma_address(sgel));
				sge_pair->len[0] = cpu_to_be32(
							sg_dma_len(sgel));
			}
		}
	} else {
		/* Program sg elements with driver's DDP buffer */
		xfer_len = scsi_bufflen(scmnd);
		list_for_each(tmp, &req->gen_list) {
			dma_buf = (struct csio_dma_buf *)tmp;
			if (i == 0) {
				sgl->addr0 = cpu_to_be64(dma_buf->paddr);
				sgl->len0 = cpu_to_be32(
						min(xfer_len, dma_buf->len));
				sge_pair = (struct ulptx_sge_pair *)(sgl + 1);
			} else if ((i - 1) & 0x1) {
				sge_pair->addr[1] = cpu_to_be64(dma_buf->paddr);
				sge_pair->len[1] = cpu_to_be32(
						min(xfer_len, dma_buf->len));
				sge_pair++;
			} else {
				sge_pair->addr[0] = cpu_to_be64(dma_buf->paddr);
				sge_pair->len[0] = cpu_to_be32(
						min(xfer_len, dma_buf->len));
			}
			xfer_len -= min(xfer_len, dma_buf->len);
			i++;
		}
	}
}

/*
 * csio_scsi_init_read_wr - Initialize the READ SCSI WR.
 * @req: IO req structure.
 * @wrp: DMA location to place the payload.
 * @size: Size of WR (including FW WR + immed data + rsp SG entry + data SGL
 *
 * Wrapper for populating fw_scsi_read_wr.
 */
static inline void
csio_scsi_init_read_wr(struct csio_ioreq *req, void *wrp, uint32_t size)
{
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_rnode *rn = req->rnode;
	struct fw_scsi_read_wr *wr = (struct fw_scsi_read_wr *)wrp;
	struct ulptx_sgl *sgl;
	struct csio_dma_buf *dma_buf;
	uint8_t imm = csio_hw_to_scsim(hw)->proto_cmd_len;
	struct scsi_cmnd *scmnd = csio_scsi_cmnd(req);

	wr->op_immdlen = cpu_to_be32(FW_WR_OP_V(FW_SCSI_READ_WR) |
				     FW_SCSI_READ_WR_IMMDLEN(imm));
	wr->flowid_len16 = cpu_to_be32(FW_WR_FLOWID_V(rn->flowid) |
				       FW_WR_LEN16_V(DIV_ROUND_UP(size, 16)));
	wr->cookie = (uintptr_t)req;
	wr->iqid = cpu_to_be16(csio_q_physiqid(hw, req->iq_idx));
	wr->tmo_val = (uint8_t)(req->tmo);
	wr->use_xfer_cnt = 1;
	wr->xfer_cnt = cpu_to_be32(scsi_bufflen(scmnd));
	wr->ini_xfer_cnt = cpu_to_be32(scsi_bufflen(scmnd));
	/* Get RSP DMA buffer */
	dma_buf = &req->dma_buf;

	/* Prepare RSP SGL */
	wr->rsp_dmalen = cpu_to_be32(dma_buf->len);
	wr->rsp_dmaaddr = cpu_to_be64(dma_buf->paddr);

	wr->r4 = 0;

	wr->u.fcoe.ctl_pri = 0;
	wr->u.fcoe.cp_en_class = 0;
	wr->u.fcoe.r3_lo[0] = 0;
	wr->u.fcoe.r3_lo[1] = 0;
	csio_scsi_fcp_cmnd(req, (void *)((uintptr_t)wrp +
					sizeof(struct fw_scsi_read_wr)));

	/* Move WR pointer past command and immediate data */
	sgl = (struct ulptx_sgl *)((uintptr_t)wrp +
			      sizeof(struct fw_scsi_read_wr) + ALIGN(imm, 16));

	/* Fill in the DSGL */
	csio_scsi_init_ultptx_dsgl(hw, req, sgl);
}

/*
 * csio_scsi_init_write_wr - Initialize the WRITE SCSI WR.
 * @req: IO req structure.
 * @wrp: DMA location to place the payload.
 * @size: Size of WR (including FW WR + immed data + rsp SG entry + data SGL
 *
 * Wrapper for populating fw_scsi_write_wr.
 */
static inline void
csio_scsi_init_write_wr(struct csio_ioreq *req, void *wrp, uint32_t size)
{
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_rnode *rn = req->rnode;
	struct fw_scsi_write_wr *wr = (struct fw_scsi_write_wr *)wrp;
	struct ulptx_sgl *sgl;
	struct csio_dma_buf *dma_buf;
	uint8_t imm = csio_hw_to_scsim(hw)->proto_cmd_len;
	struct scsi_cmnd *scmnd = csio_scsi_cmnd(req);

	wr->op_immdlen = cpu_to_be32(FW_WR_OP_V(FW_SCSI_WRITE_WR) |
				     FW_SCSI_WRITE_WR_IMMDLEN(imm));
	wr->flowid_len16 = cpu_to_be32(FW_WR_FLOWID_V(rn->flowid) |
				       FW_WR_LEN16_V(DIV_ROUND_UP(size, 16)));
	wr->cookie = (uintptr_t)req;
	wr->iqid = cpu_to_be16(csio_q_physiqid(hw, req->iq_idx));
	wr->tmo_val = (uint8_t)(req->tmo);
	wr->use_xfer_cnt = 1;
	wr->xfer_cnt = cpu_to_be32(scsi_bufflen(scmnd));
	wr->ini_xfer_cnt = cpu_to_be32(scsi_bufflen(scmnd));
	/* Get RSP DMA buffer */
	dma_buf = &req->dma_buf;

	/* Prepare RSP SGL */
	wr->rsp_dmalen = cpu_to_be32(dma_buf->len);
	wr->rsp_dmaaddr = cpu_to_be64(dma_buf->paddr);

	wr->r4 = 0;

	wr->u.fcoe.ctl_pri = 0;
	wr->u.fcoe.cp_en_class = 0;
	wr->u.fcoe.r3_lo[0] = 0;
	wr->u.fcoe.r3_lo[1] = 0;
	csio_scsi_fcp_cmnd(req, (void *)((uintptr_t)wrp +
					sizeof(struct fw_scsi_write_wr)));

	/* Move WR pointer past command and immediate data */
	sgl = (struct ulptx_sgl *)((uintptr_t)wrp +
			      sizeof(struct fw_scsi_write_wr) + ALIGN(imm, 16));

	/* Fill in the DSGL */
	csio_scsi_init_ultptx_dsgl(hw, req, sgl);
}

/* Calculate WR size needed for fw_scsi_read_wr/fw_scsi_write_wr */
#define CSIO_SCSI_DATA_WRSZ(req, oper, sz, imm)				       \
do {									       \
	(sz) = sizeof(struct fw_scsi_##oper##_wr) +	/* WR size */          \
	       ALIGN((imm), 16) +			/* Immed data */       \
	       sizeof(struct ulptx_sgl);		/* ulptx_sgl */	       \
									       \
	if (unlikely((req)->nsge > 1))				               \
		(sz) += (sizeof(struct ulptx_sge_pair) *		       \
				(ALIGN(((req)->nsge - 1), 2) / 2));            \
							/* Data SGE */	       \
} while (0)

/*
 * csio_scsi_read - Create a SCSI READ WR.
 * @req: IO req structure.
 *
 * Gets a WR slot in the ingress queue and initializes it with
 * SCSI READ WR.
 *
 */
static inline void
csio_scsi_read(struct csio_ioreq *req)
{
	struct csio_wr_pair wrp;
	uint32_t size;
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_scsim *scsim = csio_hw_to_scsim(hw);

	CSIO_SCSI_DATA_WRSZ(req, read, size, scsim->proto_cmd_len);
	size = ALIGN(size, 16);

	req->drv_status = csio_wr_get(hw, req->eq_idx, size, &wrp);
	if (likely(req->drv_status == 0)) {
		if (likely(wrp.size1 >= size)) {
			/* Initialize WR in one shot */
			csio_scsi_init_read_wr(req, wrp.addr1, size);
		} else {
			uint8_t *tmpwr = csio_q_eq_wrap(hw, req->eq_idx);
			/*
			 * Make a temporary copy of the WR and write back
			 * the copy into the WR pair.
			 */
			csio_scsi_init_read_wr(req, (void *)tmpwr, size);
			memcpy(wrp.addr1, tmpwr, wrp.size1);
			memcpy(wrp.addr2, tmpwr + wrp.size1, size - wrp.size1);
		}
	}
}

/*
 * csio_scsi_write - Create a SCSI WRITE WR.
 * @req: IO req structure.
 *
 * Gets a WR slot in the ingress queue and initializes it with
 * SCSI WRITE WR.
 *
 */
static inline void
csio_scsi_write(struct csio_ioreq *req)
{
	struct csio_wr_pair wrp;
	uint32_t size;
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_scsim *scsim = csio_hw_to_scsim(hw);

	CSIO_SCSI_DATA_WRSZ(req, write, size, scsim->proto_cmd_len);
	size = ALIGN(size, 16);

	req->drv_status = csio_wr_get(hw, req->eq_idx, size, &wrp);
	if (likely(req->drv_status == 0)) {
		if (likely(wrp.size1 >= size)) {
			/* Initialize WR in one shot */
			csio_scsi_init_write_wr(req, wrp.addr1, size);
		} else {
			uint8_t *tmpwr = csio_q_eq_wrap(hw, req->eq_idx);
			/*
			 * Make a temporary copy of the WR and write back
			 * the copy into the WR pair.
			 */
			csio_scsi_init_write_wr(req, (void *)tmpwr, size);
			memcpy(wrp.addr1, tmpwr, wrp.size1);
			memcpy(wrp.addr2, tmpwr + wrp.size1, size - wrp.size1);
		}
	}
}

/*
 * csio_setup_ddp - Setup DDP buffers for Read request.
 * @req: IO req structure.
 *
 * Checks SGLs/Data buffers are virtually contiguous required for DDP.
 * If contiguous,driver posts SGLs in the WR otherwise post internal
 * buffers for such request for DDP.
 */
static inline void
csio_setup_ddp(struct csio_scsim *scsim, struct csio_ioreq *req)
{
#ifdef __CSIO_DEBUG__
	struct csio_hw *hw = req->lnode->hwp;
#endif
	struct scatterlist *sgel = NULL;
	struct scsi_cmnd *scmnd = csio_scsi_cmnd(req);
	uint64_t sg_addr = 0;
	uint32_t ddp_pagesz = 4096;
	uint32_t buf_off;
	struct csio_dma_buf *dma_buf = NULL;
	uint32_t alloc_len = 0;
	uint32_t xfer_len = 0;
	uint32_t sg_len = 0;
	uint32_t i;

	scsi_for_each_sg(scmnd, sgel, req->nsge, i) {
		sg_addr = sg_dma_address(sgel);
		sg_len	= sg_dma_len(sgel);

		buf_off = sg_addr & (ddp_pagesz - 1);

		/* Except 1st buffer,all buffer addr have to be Page aligned */
		if (i != 0 && buf_off) {
			csio_dbg(hw, "SGL addr not DDP aligned (%llx:%d)\n",
				 sg_addr, sg_len);
			goto unaligned;
		}

		/* Except last buffer,all buffer must end on page boundary */
		if ((i != (req->nsge - 1)) &&
			((buf_off + sg_len) & (ddp_pagesz - 1))) {
			csio_dbg(hw,
				 "SGL addr not ending on page boundary"
				 "(%llx:%d)\n", sg_addr, sg_len);
			goto unaligned;
		}
	}

	/* SGL's are virtually contiguous. HW will DDP to SGLs */
	req->dcopy = 0;
	csio_scsi_read(req);

	return;

unaligned:
	CSIO_INC_STATS(scsim, n_unaligned);
	/*
	 * For unaligned SGLs, driver will allocate internal DDP buffer.
	 * Once command is completed data from DDP buffer copied to SGLs
	 */
	req->dcopy = 1;

	/* Use gen_list to store the DDP buffers */
	INIT_LIST_HEAD(&req->gen_list);
	xfer_len = scsi_bufflen(scmnd);

	i = 0;
	/* Allocate ddp buffers for this request */
	while (alloc_len < xfer_len) {
		dma_buf = csio_get_scsi_ddp(scsim);
		if (dma_buf == NULL || i > scsim->max_sge) {
			req->drv_status = -EBUSY;
			break;
		}
		alloc_len += dma_buf->len;
		/* Added to IO req */
		list_add_tail(&dma_buf->list, &req->gen_list);
		i++;
	}

	if (!req->drv_status) {
		/* set number of ddp bufs used */
		req->nsge = i;
		csio_scsi_read(req);
		return;
	}

	 /* release dma descs */
	if (i > 0)
		csio_put_scsi_ddp_list(scsim, &req->gen_list, i);
}

/*
 * csio_scsi_init_abrt_cls_wr - Initialize an ABORT/CLOSE WR.
 * @req: IO req structure.
 * @addr: DMA location to place the payload.
 * @size: Size of WR
 * @abort: abort OR close
 *
 * Wrapper for populating fw_scsi_cmd_wr.
 */
static inline void
csio_scsi_init_abrt_cls_wr(struct csio_ioreq *req, void *addr, uint32_t size,
			   bool abort)
{
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_rnode *rn = req->rnode;
	struct fw_scsi_abrt_cls_wr *wr = (struct fw_scsi_abrt_cls_wr *)addr;

	wr->op_immdlen = cpu_to_be32(FW_WR_OP_V(FW_SCSI_ABRT_CLS_WR));
	wr->flowid_len16 = cpu_to_be32(FW_WR_FLOWID_V(rn->flowid) |
					    FW_WR_LEN16_V(
						DIV_ROUND_UP(size, 16)));

	wr->cookie = (uintptr_t) req;
	wr->iqid = cpu_to_be16(csio_q_physiqid(hw, req->iq_idx));
	wr->tmo_val = (uint8_t) req->tmo;
	/* 0 for CHK_ALL_IO tells FW to look up t_cookie */
	wr->sub_opcode_to_chk_all_io =
				(FW_SCSI_ABRT_CLS_WR_SUB_OPCODE(abort) |
				 FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO(0));
	wr->r3[0] = 0;
	wr->r3[1] = 0;
	wr->r3[2] = 0;
	wr->r3[3] = 0;
	/* Since we re-use the same ioreq for abort as well */
	wr->t_cookie = (uintptr_t) req;
}

static inline void
csio_scsi_abrt_cls(struct csio_ioreq *req, bool abort)
{
	struct csio_wr_pair wrp;
	struct csio_hw *hw = req->lnode->hwp;
	uint32_t size = ALIGN(sizeof(struct fw_scsi_abrt_cls_wr), 16);

	req->drv_status = csio_wr_get(hw, req->eq_idx, size, &wrp);
	if (req->drv_status != 0)
		return;

	if (wrp.size1 >= size) {
		/* Initialize WR in one shot */
		csio_scsi_init_abrt_cls_wr(req, wrp.addr1, size, abort);
	} else {
		uint8_t *tmpwr = csio_q_eq_wrap(hw, req->eq_idx);
		/*
		 * Make a temporary copy of the WR and write back
		 * the copy into the WR pair.
		 */
		csio_scsi_init_abrt_cls_wr(req, (void *)tmpwr, size, abort);
		memcpy(wrp.addr1, tmpwr, wrp.size1);
		memcpy(wrp.addr2, tmpwr + wrp.size1, size - wrp.size1);
	}
}

/*****************************************************************************/
/* START: SCSI SM                                                            */
/*****************************************************************************/
static void
csio_scsis_uninit(struct csio_ioreq *req, enum csio_scsi_ev evt)
{
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_scsim *scsim = csio_hw_to_scsim(hw);

	switch (evt) {
	case CSIO_SCSIE_START_IO:

		if (req->nsge) {
			if (req->datadir == DMA_TO_DEVICE) {
				req->dcopy = 0;
				csio_scsi_write(req);
			} else
				csio_setup_ddp(scsim, req);
		} else {
			csio_scsi_cmd(req);
		}

		if (likely(req->drv_status == 0)) {
			/* change state and enqueue on active_q */
			csio_set_state(&req->sm, csio_scsis_io_active);
			list_add_tail(&req->sm.sm_list, &scsim->active_q);
			csio_wr_issue(hw, req->eq_idx, false);
			CSIO_INC_STATS(scsim, n_active);

			return;
		}
		break;

	case CSIO_SCSIE_START_TM:
		csio_scsi_cmd(req);
		if (req->drv_status == 0) {
			/*
			 * NOTE: We collect the affected I/Os prior to issuing
			 * LUN reset, and not after it. This is to prevent
			 * aborting I/Os that get issued after the LUN reset,
			 * but prior to LUN reset completion (in the event that
			 * the host stack has not blocked I/Os to a LUN that is
			 * being reset.
			 */
			csio_set_state(&req->sm, csio_scsis_tm_active);
			list_add_tail(&req->sm.sm_list, &scsim->active_q);
			csio_wr_issue(hw, req->eq_idx, false);
			CSIO_INC_STATS(scsim, n_tm_active);
		}
		return;

	case CSIO_SCSIE_ABORT:
	case CSIO_SCSIE_CLOSE:
		/*
		 * NOTE:
		 * We could get here due to  :
		 * - a window in the cleanup path of the SCSI module
		 *   (csio_scsi_abort_io()). Please see NOTE in this function.
		 * - a window in the time we tried to issue an abort/close
		 *   of a request to FW, and the FW completed the request
		 *   itself.
		 *   Print a message for now, and return INVAL either way.
		 */
		req->drv_status = -EINVAL;
		csio_warn(hw, "Trying to abort/close completed IO:%p!\n", req);
		break;

	default:
		csio_dbg(hw, "Unhandled event:%d sent to req:%p\n", evt, req);
		CSIO_DB_ASSERT(0);
	}
}

static void
csio_scsis_io_active(struct csio_ioreq *req, enum csio_scsi_ev evt)
{
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_scsim *scm = csio_hw_to_scsim(hw);
	struct csio_rnode *rn;

	switch (evt) {
	case CSIO_SCSIE_COMPLETED:
		CSIO_DEC_STATS(scm, n_active);
		list_del_init(&req->sm.sm_list);
		csio_set_state(&req->sm, csio_scsis_uninit);
		/*
		 * In MSIX mode, with multiple queues, the SCSI compeltions
		 * could reach us sooner than the FW events sent to indicate
		 * I-T nexus loss (link down, remote device logo etc). We
		 * dont want to be returning such I/Os to the upper layer
		 * immediately, since we wouldnt have reported the I-T nexus
		 * loss itself. This forces us to serialize such completions
		 * with the reporting of the I-T nexus loss. Therefore, we
		 * internally queue up such up such completions in the rnode.
		 * The reporting of I-T nexus loss to the upper layer is then
		 * followed by the returning of I/Os in this internal queue.
		 * Having another state alongwith another queue helps us take
		 * actions for events such as ABORT received while we are
		 * in this rnode queue.
		 */
		if (unlikely(req->wr_status != FW_SUCCESS)) {
			rn = req->rnode;
			/*
			 * FW says remote device is lost, but rnode
			 * doesn't reflect it.
			 */
			if (csio_scsi_itnexus_loss_error(req->wr_status) &&
						csio_is_rnode_ready(rn)) {
				csio_set_state(&req->sm,
						csio_scsis_shost_cmpl_await);
				list_add_tail(&req->sm.sm_list,
					      &rn->host_cmpl_q);
			}
		}

		break;

	case CSIO_SCSIE_ABORT:
		csio_scsi_abrt_cls(req, SCSI_ABORT);
		if (req->drv_status == 0) {
			csio_wr_issue(hw, req->eq_idx, false);
			csio_set_state(&req->sm, csio_scsis_aborting);
		}
		break;

	case CSIO_SCSIE_CLOSE:
		csio_scsi_abrt_cls(req, SCSI_CLOSE);
		if (req->drv_status == 0) {
			csio_wr_issue(hw, req->eq_idx, false);
			csio_set_state(&req->sm, csio_scsis_closing);
		}
		break;

	case CSIO_SCSIE_DRVCLEANUP:
		req->wr_status = FW_HOSTERROR;
		CSIO_DEC_STATS(scm, n_active);
		csio_set_state(&req->sm, csio_scsis_uninit);
		break;

	default:
		csio_dbg(hw, "Unhandled event:%d sent to req:%p\n", evt, req);
		CSIO_DB_ASSERT(0);
	}
}

static void
csio_scsis_tm_active(struct csio_ioreq *req, enum csio_scsi_ev evt)
{
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_scsim *scm = csio_hw_to_scsim(hw);

	switch (evt) {
	case CSIO_SCSIE_COMPLETED:
		CSIO_DEC_STATS(scm, n_tm_active);
		list_del_init(&req->sm.sm_list);
		csio_set_state(&req->sm, csio_scsis_uninit);

		break;

	case CSIO_SCSIE_ABORT:
		csio_scsi_abrt_cls(req, SCSI_ABORT);
		if (req->drv_status == 0) {
			csio_wr_issue(hw, req->eq_idx, false);
			csio_set_state(&req->sm, csio_scsis_aborting);
		}
		break;


	case CSIO_SCSIE_CLOSE:
		csio_scsi_abrt_cls(req, SCSI_CLOSE);
		if (req->drv_status == 0) {
			csio_wr_issue(hw, req->eq_idx, false);
			csio_set_state(&req->sm, csio_scsis_closing);
		}
		break;

	case CSIO_SCSIE_DRVCLEANUP:
		req->wr_status = FW_HOSTERROR;
		CSIO_DEC_STATS(scm, n_tm_active);
		csio_set_state(&req->sm, csio_scsis_uninit);
		break;

	default:
		csio_dbg(hw, "Unhandled event:%d sent to req:%p\n", evt, req);
		CSIO_DB_ASSERT(0);
	}
}

static void
csio_scsis_aborting(struct csio_ioreq *req, enum csio_scsi_ev evt)
{
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_scsim *scm = csio_hw_to_scsim(hw);

	switch (evt) {
	case CSIO_SCSIE_COMPLETED:
		csio_dbg(hw,
			 "ioreq %p recvd cmpltd (wr_status:%d) "
			 "in aborting st\n", req, req->wr_status);
		/*
		 * Use -ECANCELED to explicitly tell the ABORTED event that
		 * the original I/O was returned to driver by FW.
		 * We dont really care if the I/O was returned with success by
		 * FW (because the ABORT and completion of the I/O crossed each
		 * other), or any other return value. Once we are in aborting
		 * state, the success or failure of the I/O is unimportant to
		 * us.
		 */
		req->drv_status = -ECANCELED;
		break;

	case CSIO_SCSIE_ABORT:
		CSIO_INC_STATS(scm, n_abrt_dups);
		break;

	case CSIO_SCSIE_ABORTED:

		csio_dbg(hw, "abort of %p return status:0x%x drv_status:%x\n",
			 req, req->wr_status, req->drv_status);
		/*
		 * Check if original I/O WR completed before the Abort
		 * completion.
		 */
		if (req->drv_status != -ECANCELED) {
			csio_warn(hw,
				  "Abort completed before original I/O,"
				   " req:%p\n", req);
			CSIO_DB_ASSERT(0);
		}

		/*
		 * There are the following possible scenarios:
		 * 1. The abort completed successfully, FW returned FW_SUCCESS.
		 * 2. The completion of an I/O and the receipt of
		 *    abort for that I/O by the FW crossed each other.
		 *    The FW returned FW_EINVAL. The original I/O would have
		 *    returned with FW_SUCCESS or any other SCSI error.
		 * 3. The FW couldn't sent the abort out on the wire, as there
		 *    was an I-T nexus loss (link down, remote device logged
		 *    out etc). FW sent back an appropriate IT nexus loss status
		 *    for the abort.
		 * 4. FW sent an abort, but abort timed out (remote device
		 *    didnt respond). FW replied back with
		 *    FW_SCSI_ABORT_TIMEDOUT.
		 * 5. FW couldn't genuinely abort the request for some reason,
		 *    and sent us an error.
		 *
		 * The first 3 scenarios are treated as  succesful abort
		 * operations by the host, while the last 2 are failed attempts
		 * to abort. Manipulate the return value of the request
		 * appropriately, so that host can convey these results
		 * back to the upper layer.
		 */
		if ((req->wr_status == FW_SUCCESS) ||
		    (req->wr_status == FW_EINVAL) ||
		    csio_scsi_itnexus_loss_error(req->wr_status))
			req->wr_status = FW_SCSI_ABORT_REQUESTED;

		CSIO_DEC_STATS(scm, n_active);
		list_del_init(&req->sm.sm_list);
		csio_set_state(&req->sm, csio_scsis_uninit);
		break;

	case CSIO_SCSIE_DRVCLEANUP:
		req->wr_status = FW_HOSTERROR;
		CSIO_DEC_STATS(scm, n_active);
		csio_set_state(&req->sm, csio_scsis_uninit);
		break;

	case CSIO_SCSIE_CLOSE:
		/*
		 * We can receive this event from the module
		 * cleanup paths, if the FW forgot to reply to the ABORT WR
		 * and left this ioreq in this state. For now, just ignore
		 * the event. The CLOSE event is sent to this state, as
		 * the LINK may have already gone down.
		 */
		break;

	default:
		csio_dbg(hw, "Unhandled event:%d sent to req:%p\n", evt, req);
		CSIO_DB_ASSERT(0);
	}
}

static void
csio_scsis_closing(struct csio_ioreq *req, enum csio_scsi_ev evt)
{
	struct csio_hw *hw = req->lnode->hwp;
	struct csio_scsim *scm = csio_hw_to_scsim(hw);

	switch (evt) {
	case CSIO_SCSIE_COMPLETED:
		csio_dbg(hw,
			 "ioreq %p recvd cmpltd (wr_status:%d) "
			 "in closing st\n", req, req->wr_status);
		/*
		 * Use -ECANCELED to explicitly tell the CLOSED event that
		 * the original I/O was returned to driver by FW.
		 * We dont really care if the I/O was returned with success by
		 * FW (because the CLOSE and completion of the I/O crossed each
		 * other), or any other return value. Once we are in aborting
		 * state, the success or failure of the I/O is unimportant to
		 * us.
		 */
		req->drv_status = -ECANCELED;
		break;

	case CSIO_SCSIE_CLOSED:
		/*
		 * Check if original I/O WR completed before the Close
		 * completion.
		 */
		if (req->drv_status != -ECANCELED) {
			csio_fatal(hw,
				   "Close completed before original I/O,"
				   " req:%p\n", req);
			CSIO_DB_ASSERT(0);
		}

		/*
		 * Either close succeeded, or we issued close to FW at the
		 * same time FW compelted it to us. Either way, the I/O
		 * is closed.
		 */
		CSIO_DB_ASSERT((req->wr_status == FW_SUCCESS) ||
					(req->wr_status == FW_EINVAL));
		req->wr_status = FW_SCSI_CLOSE_REQUESTED;

		CSIO_DEC_STATS(scm, n_active);
		list_del_init(&req->sm.sm_list);
		csio_set_state(&req->sm, csio_scsis_uninit);
		break;

	case CSIO_SCSIE_CLOSE:
		break;

	case CSIO_SCSIE_DRVCLEANUP:
		req->wr_status = FW_HOSTERROR;
		CSIO_DEC_STATS(scm, n_active);
		csio_set_state(&req->sm, csio_scsis_uninit);
		break;

	default:
		csio_dbg(hw, "Unhandled event:%d sent to req:%p\n", evt, req);
		CSIO_DB_ASSERT(0);
	}
}

static void
csio_scsis_shost_cmpl_await(struct csio_ioreq *req, enum csio_scsi_ev evt)
{
	switch (evt) {
	case CSIO_SCSIE_ABORT:
	case CSIO_SCSIE_CLOSE:
		/*
		 * Just succeed the abort request, and hope that
		 * the remote device unregister path will cleanup
		 * this I/O to the upper layer within a sane
		 * amount of time.
		 */
		/*
		 * A close can come in during a LINK DOWN. The FW would have
		 * returned us the I/O back, but not the remote device lost
		 * FW event. In this interval, if the I/O times out at the upper
		 * layer, a close can come in. Take the same action as abort:
		 * return success, and hope that the remote device unregister
		 * path will cleanup this I/O. If the FW still doesnt send
		 * the msg, the close times out, and the upper layer resorts
		 * to the next level of error recovery.
		 */
		req->drv_status = 0;
		break;
	case CSIO_SCSIE_DRVCLEANUP:
		csio_set_state(&req->sm, csio_scsis_uninit);
		break;
	default:
		csio_dbg(req->lnode->hwp, "Unhandled event:%d sent to req:%p\n",
			 evt, req);
		CSIO_DB_ASSERT(0);
	}
}

/*
 * csio_scsi_cmpl_handler - WR completion handler for SCSI.
 * @hw: HW module.
 * @wr: The completed WR from the ingress queue.
 * @len: Length of the WR.
 * @flb: Freelist buffer array.
 * @priv: Private object
 * @scsiwr: Pointer to SCSI WR.
 *
 * This is the WR completion handler called per completion from the
 * ISR. It is called with lock held. It walks past the RSS and CPL message
 * header where the actual WR is present.
 * It then gets the status, WR handle (ioreq pointer) and the len of
 * the WR, based on WR opcode. Only on a non-good status is the entire
 * WR copied into the WR cache (ioreq->fw_wr).
 * The ioreq corresponding to the WR is returned to the caller.
 * NOTE: The SCSI queue doesnt allocate a freelist today, hence
 * no freelist buffer is expected.
 */
struct csio_ioreq *
csio_scsi_cmpl_handler(struct csio_hw *hw, void *wr, uint32_t len,
		     struct csio_fl_dma_buf *flb, void *priv, uint8_t **scsiwr)
{
	struct csio_ioreq *ioreq = NULL;
	struct cpl_fw6_msg *cpl;
	uint8_t *tempwr;
	uint8_t	status;
	struct csio_scsim *scm = csio_hw_to_scsim(hw);

	/* skip RSS header */
	cpl = (struct cpl_fw6_msg *)((uintptr_t)wr + sizeof(__be64));

	if (unlikely(cpl->opcode != CPL_FW6_MSG)) {
		csio_warn(hw, "Error: Invalid CPL msg %x recvd on SCSI q\n",
			  cpl->opcode);
		CSIO_INC_STATS(scm, n_inval_cplop);
		return NULL;
	}

	tempwr = (uint8_t *)(cpl->data);
	status = csio_wr_status(tempwr);
	*scsiwr = tempwr;

	if (likely((*tempwr == FW_SCSI_READ_WR) ||
			(*tempwr == FW_SCSI_WRITE_WR) ||
			(*tempwr == FW_SCSI_CMD_WR))) {
		ioreq = (struct csio_ioreq *)((uintptr_t)
				 (((struct fw_scsi_read_wr *)tempwr)->cookie));
		CSIO_DB_ASSERT(virt_addr_valid(ioreq));

		ioreq->wr_status = status;

		return ioreq;
	}

	if (*tempwr == FW_SCSI_ABRT_CLS_WR) {
		ioreq = (struct csio_ioreq *)((uintptr_t)
			 (((struct fw_scsi_abrt_cls_wr *)tempwr)->cookie));
		CSIO_DB_ASSERT(virt_addr_valid(ioreq));

		ioreq->wr_status = status;
		return ioreq;
	}

	csio_warn(hw, "WR with invalid opcode in SCSI IQ: %x\n", *tempwr);
	CSIO_INC_STATS(scm, n_inval_scsiop);
	return NULL;
}

/*
 * csio_scsi_cleanup_io_q - Cleanup the given queue.
 * @scm: SCSI module.
 * @q: Queue to be cleaned up.
 *
 * Called with lock held. Has to exit with lock held.
 */
void
csio_scsi_cleanup_io_q(struct csio_scsim *scm, struct list_head *q)
{
	struct csio_hw *hw = scm->hw;
	struct csio_ioreq *ioreq;
	struct list_head *tmp, *next;
	struct scsi_cmnd *scmnd;

	/* Call back the completion routines of the active_q */
	list_for_each_safe(tmp, next, q) {
		ioreq = (struct csio_ioreq *)tmp;
		csio_scsi_drvcleanup(ioreq);
		list_del_init(&ioreq->sm.sm_list);
		scmnd = csio_scsi_cmnd(ioreq);
		spin_unlock_irq(&hw->lock);

		/*
		 * Upper layers may have cleared this command, hence this
		 * check to avoid accessing stale references.
		 */
		if (scmnd != NULL)
			ioreq->io_cbfn(hw, ioreq);

		spin_lock_irq(&scm->freelist_lock);
		csio_put_scsi_ioreq(scm, ioreq);
		spin_unlock_irq(&scm->freelist_lock);

		spin_lock_irq(&hw->lock);
	}
}

#define CSIO_SCSI_ABORT_Q_POLL_MS		2000

static void
csio_abrt_cls(struct csio_ioreq *ioreq, struct scsi_cmnd *scmnd)
{
	struct csio_lnode *ln = ioreq->lnode;
	struct csio_hw *hw = ln->hwp;
	int ready = 0;
	struct csio_scsim *scsim = csio_hw_to_scsim(hw);
	int rv;

	if (csio_scsi_cmnd(ioreq) != scmnd) {
		CSIO_INC_STATS(scsim, n_abrt_race_comp);
		return;
	}

	ready = csio_is_lnode_ready(ln);

	rv = csio_do_abrt_cls(hw, ioreq, (ready ? SCSI_ABORT : SCSI_CLOSE));
	if (rv != 0) {
		if (ready)
			CSIO_INC_STATS(scsim, n_abrt_busy_error);
		else
			CSIO_INC_STATS(scsim, n_cls_busy_error);
	}
}

/*
 * csio_scsi_abort_io_q - Abort all I/Os on given queue
 * @scm: SCSI module.
 * @q: Queue to abort.
 * @tmo: Timeout in ms
 *
 * Attempt to abort all I/Os on given queue, and wait for a max
 * of tmo milliseconds for them to complete. Returns success
 * if all I/Os are aborted. Else returns -ETIMEDOUT.
 * Should be entered with lock held. Exits with lock held.
 * NOTE:
 * Lock has to be held across the loop that aborts I/Os, since dropping the lock
 * in between can cause the list to be corrupted. As a result, the caller
 * of this function has to ensure that the number of I/os to be aborted
 * is finite enough to not cause lock-held-for-too-long issues.
 */
static int
csio_scsi_abort_io_q(struct csio_scsim *scm, struct list_head *q, uint32_t tmo)
{
	struct csio_hw *hw = scm->hw;
	struct list_head *tmp, *next;
	int count = DIV_ROUND_UP(tmo, CSIO_SCSI_ABORT_Q_POLL_MS);
	struct scsi_cmnd *scmnd;

	if (list_empty(q))
		return 0;

	csio_dbg(hw, "Aborting SCSI I/Os\n");

	/* Now abort/close I/Os in the queue passed */
	list_for_each_safe(tmp, next, q) {
		scmnd = csio_scsi_cmnd((struct csio_ioreq *)tmp);
		csio_abrt_cls((struct csio_ioreq *)tmp, scmnd);
	}

	/* Wait till all active I/Os are completed/aborted/closed */
	while (!list_empty(q) && count--) {
		spin_unlock_irq(&hw->lock);
		msleep(CSIO_SCSI_ABORT_Q_POLL_MS);
		spin_lock_irq(&hw->lock);
	}

	/* all aborts completed */
	if (list_empty(q))
		return 0;

	return -ETIMEDOUT;
}

/*
 * csio_scsim_cleanup_io - Cleanup all I/Os in SCSI module.
 * @scm: SCSI module.
 * @abort: abort required.
 * Called with lock held, should exit with lock held.
 * Can sleep when waiting for I/Os to complete.
 */
int
csio_scsim_cleanup_io(struct csio_scsim *scm, bool abort)
{
	struct csio_hw *hw = scm->hw;
	int rv = 0;
	int count = DIV_ROUND_UP(60 * 1000, CSIO_SCSI_ABORT_Q_POLL_MS);

	/* No I/Os pending */
	if (list_empty(&scm->active_q))
		return 0;

	/* Wait until all active I/Os are completed */
	while (!list_empty(&scm->active_q) && count--) {
		spin_unlock_irq(&hw->lock);
		msleep(CSIO_SCSI_ABORT_Q_POLL_MS);
		spin_lock_irq(&hw->lock);
	}

	/* all I/Os completed */
	if (list_empty(&scm->active_q))
		return 0;

	/* Else abort */
	if (abort) {
		rv = csio_scsi_abort_io_q(scm, &scm->active_q, 30000);
		if (rv == 0)
			return rv;
		csio_dbg(hw, "Some I/O aborts timed out, cleaning up..\n");
	}

	csio_scsi_cleanup_io_q(scm, &scm->active_q);

	CSIO_DB_ASSERT(list_empty(&scm->active_q));

	return rv;
}

/*
 * csio_scsim_cleanup_io_lnode - Cleanup all I/Os of given lnode.
 * @scm: SCSI module.
 * @lnode: lnode
 *
 * Called with lock held, should exit with lock held.
 * Can sleep (with dropped lock) when waiting for I/Os to complete.
 */
int
csio_scsim_cleanup_io_lnode(struct csio_scsim *scm, struct csio_lnode *ln)
{
	struct csio_hw *hw = scm->hw;
	struct csio_scsi_level_data sld;
	int rv;
	int count = DIV_ROUND_UP(60 * 1000, CSIO_SCSI_ABORT_Q_POLL_MS);

	csio_dbg(hw, "Gathering all SCSI I/Os on lnode %p\n", ln);

	sld.level = CSIO_LEV_LNODE;
	sld.lnode = ln;
	INIT_LIST_HEAD(&ln->cmpl_q);
	csio_scsi_gather_active_ios(scm, &sld, &ln->cmpl_q);

	/* No I/Os pending on this lnode  */
	if (list_empty(&ln->cmpl_q))
		return 0;

	/* Wait until all active I/Os on this lnode are completed */
	while (!list_empty(&ln->cmpl_q) && count--) {
		spin_unlock_irq(&hw->lock);
		msleep(CSIO_SCSI_ABORT_Q_POLL_MS);
		spin_lock_irq(&hw->lock);
	}

	/* all I/Os completed */
	if (list_empty(&ln->cmpl_q))
		return 0;

	csio_dbg(hw, "Some I/Os pending on ln:%p, aborting them..\n", ln);

	/* I/Os are pending, abort them */
	rv = csio_scsi_abort_io_q(scm, &ln->cmpl_q, 30000);
	if (rv != 0) {
		csio_dbg(hw, "Some I/O aborts timed out, cleaning up..\n");
		csio_scsi_cleanup_io_q(scm, &ln->cmpl_q);
	}

	CSIO_DB_ASSERT(list_empty(&ln->cmpl_q));

	return rv;
}

static ssize_t
csio_show_hw_state(struct device *dev,
		   struct device_attribute *attr, char *buf)
{
	struct csio_lnode *ln = shost_priv(class_to_shost(dev));
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	if (csio_is_hw_ready(hw))
		return sysfs_emit(buf, "ready\n");

	return sysfs_emit(buf, "not ready\n");
}

/* Device reset */
static ssize_t
csio_device_reset(struct device *dev,
		   struct device_attribute *attr, const char *buf, size_t count)
{
	struct csio_lnode *ln = shost_priv(class_to_shost(dev));
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	if (*buf != '1')
		return -EINVAL;

	/* Delete NPIV lnodes */
	csio_lnodes_exit(hw, 1);

	/* Block upper IOs */
	csio_lnodes_block_request(hw);

	spin_lock_irq(&hw->lock);
	csio_hw_reset(hw);
	spin_unlock_irq(&hw->lock);

	/* Unblock upper IOs */
	csio_lnodes_unblock_request(hw);
	return count;
}

/* disable port */
static ssize_t
csio_disable_port(struct device *dev,
		   struct device_attribute *attr, const char *buf, size_t count)
{
	struct csio_lnode *ln = shost_priv(class_to_shost(dev));
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	bool disable;

	if (*buf == '1' || *buf == '0')
		disable = (*buf == '1') ? true : false;
	else
		return -EINVAL;

	/* Block upper IOs */
	csio_lnodes_block_by_port(hw, ln->portid);

	spin_lock_irq(&hw->lock);
	csio_disable_lnodes(hw, ln->portid, disable);
	spin_unlock_irq(&hw->lock);

	/* Unblock upper IOs */
	csio_lnodes_unblock_by_port(hw, ln->portid);
	return count;
}

/* Show debug level */
static ssize_t
csio_show_dbg_level(struct device *dev,
		   struct device_attribute *attr, char *buf)
{
	struct csio_lnode *ln = shost_priv(class_to_shost(dev));

	return sysfs_emit(buf, "%x\n", ln->params.log_level);
}

/* Store debug level */
static ssize_t
csio_store_dbg_level(struct device *dev,
		   struct device_attribute *attr, const char *buf, size_t count)
{
	struct csio_lnode *ln = shost_priv(class_to_shost(dev));
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	uint32_t dbg_level = 0;

	if (!isdigit(buf[0]))
		return -EINVAL;

	if (sscanf(buf, "%i", &dbg_level))
		return -EINVAL;

	ln->params.log_level = dbg_level;
	hw->params.log_level = dbg_level;

	return 0;
}

static DEVICE_ATTR(hw_state, S_IRUGO, csio_show_hw_state, NULL);
static DEVICE_ATTR(device_reset, S_IWUSR, NULL, csio_device_reset);
static DEVICE_ATTR(disable_port, S_IWUSR, NULL, csio_disable_port);
static DEVICE_ATTR(dbg_level, S_IRUGO | S_IWUSR, csio_show_dbg_level,
		  csio_store_dbg_level);

static struct attribute *csio_fcoe_lport_attrs[] = {
	&dev_attr_hw_state.attr,
	&dev_attr_device_reset.attr,
	&dev_attr_disable_port.attr,
	&dev_attr_dbg_level.attr,
	NULL,
};

ATTRIBUTE_GROUPS(csio_fcoe_lport);

static ssize_t
csio_show_num_reg_rnodes(struct device *dev,
		     struct device_attribute *attr, char *buf)
{
	struct csio_lnode *ln = shost_priv(class_to_shost(dev));

	return sysfs_emit(buf, "%d\n", ln->num_reg_rnodes);
}

static DEVICE_ATTR(num_reg_rnodes, S_IRUGO, csio_show_num_reg_rnodes, NULL);

static struct attribute *csio_fcoe_vport_attrs[] = {
	&dev_attr_num_reg_rnodes.attr,
	&dev_attr_dbg_level.attr,
	NULL,
};

ATTRIBUTE_GROUPS(csio_fcoe_vport);

static inline uint32_t
csio_scsi_copy_to_sgl(struct csio_hw *hw, struct csio_ioreq *req)
{
	struct scsi_cmnd *scmnd  = (struct scsi_cmnd *)csio_scsi_cmnd(req);
	struct scatterlist *sg;
	uint32_t bytes_left;
	uint32_t bytes_copy;
	uint32_t buf_off = 0;
	uint32_t start_off = 0;
	uint32_t sg_off = 0;
	void *sg_addr;
	void *buf_addr;
	struct csio_dma_buf *dma_buf;

	bytes_left = scsi_bufflen(scmnd);
	sg = scsi_sglist(scmnd);
	dma_buf = (struct csio_dma_buf *)csio_list_next(&req->gen_list);

	/* Copy data from driver buffer to SGs of SCSI CMD */
	while (bytes_left > 0 && sg && dma_buf) {
		if (buf_off >= dma_buf->len) {
			buf_off = 0;
			dma_buf = (struct csio_dma_buf *)
					csio_list_next(dma_buf);
			continue;
		}

		if (start_off >= sg->length) {
			start_off -= sg->length;
			sg = sg_next(sg);
			continue;
		}

		buf_addr = dma_buf->vaddr + buf_off;
		sg_off = sg->offset + start_off;
		bytes_copy = min((dma_buf->len - buf_off),
				sg->length - start_off);
		bytes_copy = min((uint32_t)(PAGE_SIZE - (sg_off & ~PAGE_MASK)),
				 bytes_copy);

		sg_addr = kmap_atomic(sg_page(sg) + (sg_off >> PAGE_SHIFT));
		if (!sg_addr) {
			csio_err(hw, "failed to kmap sg:%p of ioreq:%p\n",
				sg, req);
			break;
		}

		csio_dbg(hw, "copy_to_sgl:sg_addr %p sg_off %d buf %p len %d\n",
				sg_addr, sg_off, buf_addr, bytes_copy);
		memcpy(sg_addr + (sg_off & ~PAGE_MASK), buf_addr, bytes_copy);
		kunmap_atomic(sg_addr);

		start_off +=  bytes_copy;
		buf_off += bytes_copy;
		bytes_left -= bytes_copy;
	}

	if (bytes_left > 0)
		return DID_ERROR;
	else
		return DID_OK;
}

/*
 * csio_scsi_err_handler - SCSI error handler.
 * @hw: HW module.
 * @req: IO request.
 *
 */
static inline void
csio_scsi_err_handler(struct csio_hw *hw, struct csio_ioreq *req)
{
	struct scsi_cmnd *cmnd  = (struct scsi_cmnd *)csio_scsi_cmnd(req);
	struct csio_scsim *scm = csio_hw_to_scsim(hw);
	struct fcp_resp_with_ext *fcp_resp;
	struct fcp_resp_rsp_info *rsp_info;
	struct csio_dma_buf *dma_buf;
	uint8_t flags, scsi_status = 0;
	uint32_t host_status = DID_OK;
	uint32_t rsp_len = 0, sns_len = 0;
	struct csio_rnode *rn = (struct csio_rnode *)(cmnd->device->hostdata);


	switch (req->wr_status) {
	case FW_HOSTERROR:
		if (unlikely(!csio_is_hw_ready(hw)))
			return;

		host_status = DID_ERROR;
		CSIO_INC_STATS(scm, n_hosterror);

		break;
	case FW_SCSI_RSP_ERR:
		dma_buf = &req->dma_buf;
		fcp_resp = (struct fcp_resp_with_ext *)dma_buf->vaddr;
		rsp_info = (struct fcp_resp_rsp_info *)(fcp_resp + 1);
		flags = fcp_resp->resp.fr_flags;
		scsi_status = fcp_resp->resp.fr_status;

		if (flags & FCP_RSP_LEN_VAL) {
			rsp_len = be32_to_cpu(fcp_resp->ext.fr_rsp_len);
			if ((rsp_len != 0 && rsp_len != 4 && rsp_len != 8) ||
				(rsp_info->rsp_code != FCP_TMF_CMPL)) {
				host_status = DID_ERROR;
				goto out;
			}
		}

		if ((flags & FCP_SNS_LEN_VAL) && fcp_resp->ext.fr_sns_len) {
			sns_len = be32_to_cpu(fcp_resp->ext.fr_sns_len);
			if (sns_len > SCSI_SENSE_BUFFERSIZE)
				sns_len = SCSI_SENSE_BUFFERSIZE;

			memcpy(cmnd->sense_buffer,
			       &rsp_info->_fr_resvd[0] + rsp_len, sns_len);
			CSIO_INC_STATS(scm, n_autosense);
		}

		scsi_set_resid(cmnd, 0);

		/* Under run */
		if (flags & FCP_RESID_UNDER) {
			scsi_set_resid(cmnd,
				       be32_to_cpu(fcp_resp->ext.fr_resid));

			if (!(flags & FCP_SNS_LEN_VAL) &&
			    (scsi_status == SAM_STAT_GOOD) &&
			    ((scsi_bufflen(cmnd) - scsi_get_resid(cmnd))
							< cmnd->underflow))
				host_status = DID_ERROR;
		} else if (flags & FCP_RESID_OVER)
			host_status = DID_ERROR;

		CSIO_INC_STATS(scm, n_rsperror);
		break;

	case FW_SCSI_OVER_FLOW_ERR:
		csio_warn(hw,
			  "Over-flow error,cmnd:0x%x expected len:0x%x"
			  " resid:0x%x\n", cmnd->cmnd[0],
			  scsi_bufflen(cmnd), scsi_get_resid(cmnd));
		host_status = DID_ERROR;
		CSIO_INC_STATS(scm, n_ovflerror);
		break;

	case FW_SCSI_UNDER_FLOW_ERR:
		csio_warn(hw,
			  "Under-flow error,cmnd:0x%x expected"
			  " len:0x%x resid:0x%x lun:0x%llx ssn:0x%x\n",
			  cmnd->cmnd[0], scsi_bufflen(cmnd),
			  scsi_get_resid(cmnd), cmnd->device->lun,
			  rn->flowid);
		host_status = DID_ERROR;
		CSIO_INC_STATS(scm, n_unflerror);
		break;

	case FW_SCSI_ABORT_REQUESTED:
	case FW_SCSI_ABORTED:
	case FW_SCSI_CLOSE_REQUESTED:
		csio_dbg(hw, "Req %p cmd:%p op:%x %s\n", req, cmnd,
			     cmnd->cmnd[0],
			    (req->wr_status == FW_SCSI_CLOSE_REQUESTED) ?
			    "closed" : "aborted");
		/*
		 * csio_eh_abort_handler checks this value to
		 * succeed or fail the abort request.
		 */
		host_status = DID_REQUEUE;
		if (req->wr_status == FW_SCSI_CLOSE_REQUESTED)
			CSIO_INC_STATS(scm, n_closed);
		else
			CSIO_INC_STATS(scm, n_aborted);
		break;

	case FW_SCSI_ABORT_TIMEDOUT:
		/* FW timed out the abort itself */
		csio_dbg(hw, "FW timed out abort req:%p cmnd:%p status:%x\n",
			 req, cmnd, req->wr_status);
		host_status = DID_ERROR;
		CSIO_INC_STATS(scm, n_abrt_timedout);
		break;

	case FW_RDEV_NOT_READY:
		/*
		 * In firmware, a RDEV can get into this state
		 * temporarily, before moving into dissapeared/lost
		 * state. So, the driver should complete the request equivalent
		 * to device-disappeared!
		 */
		CSIO_INC_STATS(scm, n_rdev_nr_error);
		host_status = DID_ERROR;
		break;

	case FW_ERR_RDEV_LOST:
		CSIO_INC_STATS(scm, n_rdev_lost_error);
		host_status = DID_ERROR;
		break;

	case FW_ERR_RDEV_LOGO:
		CSIO_INC_STATS(scm, n_rdev_logo_error);
		host_status = DID_ERROR;
		break;

	case FW_ERR_RDEV_IMPL_LOGO:
		host_status = DID_ERROR;
		break;

	case FW_ERR_LINK_DOWN:
		CSIO_INC_STATS(scm, n_link_down_error);
		host_status = DID_ERROR;
		break;

	case FW_FCOE_NO_XCHG:
		CSIO_INC_STATS(scm, n_no_xchg_error);
		host_status = DID_ERROR;
		break;

	default:
		csio_err(hw, "Unknown SCSI FW WR status:%d req:%p cmnd:%p\n",
			    req->wr_status, req, cmnd);
		CSIO_DB_ASSERT(0);

		CSIO_INC_STATS(scm, n_unknown_error);
		host_status = DID_ERROR;
		break;
	}

out:
	if (req->nsge > 0) {
		scsi_dma_unmap(cmnd);
		if (req->dcopy && (host_status == DID_OK))
			host_status = csio_scsi_copy_to_sgl(hw, req);
	}

	cmnd->result = (((host_status) << 16) | scsi_status);
	scsi_done(cmnd);

	/* Wake up waiting threads */
	csio_scsi_cmnd(req) = NULL;
	complete(&req->cmplobj);
}

/*
 * csio_scsi_cbfn - SCSI callback function.
 * @hw: HW module.
 * @req: IO request.
 *
 */
static void
csio_scsi_cbfn(struct csio_hw *hw, struct csio_ioreq *req)
{
	struct scsi_cmnd *cmnd  = (struct scsi_cmnd *)csio_scsi_cmnd(req);
	uint8_t scsi_status = SAM_STAT_GOOD;
	uint32_t host_status = DID_OK;

	if (likely(req->wr_status == FW_SUCCESS)) {
		if (req->nsge > 0) {
			scsi_dma_unmap(cmnd);
			if (req->dcopy)
				host_status = csio_scsi_copy_to_sgl(hw, req);
		}

		cmnd->result = (((host_status) << 16) | scsi_status);
		scsi_done(cmnd);
		csio_scsi_cmnd(req) = NULL;
		CSIO_INC_STATS(csio_hw_to_scsim(hw), n_tot_success);
	} else {
		/* Error handling */
		csio_scsi_err_handler(hw, req);
	}
}

/**
 * csio_queuecommand - Entry point to kickstart an I/O request.
 * @host:	The scsi_host pointer.
 * @cmnd:	The I/O request from ML.
 *
 * This routine does the following:
 *	- Checks for HW and Rnode module readiness.
 *	- Gets a free ioreq structure (which is already initialized
 *	  to uninit during its allocation).
 *	- Maps SG elements.
 *	- Initializes ioreq members.
 *	- Kicks off the SCSI state machine for this IO.
 *	- Returns busy status on error.
 */
static int
csio_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmnd)
{
	struct csio_lnode *ln = shost_priv(host);
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	struct csio_scsim *scsim = csio_hw_to_scsim(hw);
	struct csio_rnode *rn = (struct csio_rnode *)(cmnd->device->hostdata);
	struct csio_ioreq *ioreq = NULL;
	unsigned long flags;
	int nsge = 0;
	int rv = SCSI_MLQUEUE_HOST_BUSY, nr;
	int retval;
	struct csio_scsi_qset *sqset;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));

	sqset = &hw->sqset[ln->portid][blk_mq_rq_cpu(scsi_cmd_to_rq(cmnd))];

	nr = fc_remote_port_chkready(rport);
	if (nr) {
		cmnd->result = nr;
		CSIO_INC_STATS(scsim, n_rn_nr_error);
		goto err_done;
	}

	if (unlikely(!csio_is_hw_ready(hw))) {
		cmnd->result = (DID_REQUEUE << 16);
		CSIO_INC_STATS(scsim, n_hw_nr_error);
		goto err_done;
	}

	/* Get req->nsge, if there are SG elements to be mapped  */
	nsge = scsi_dma_map(cmnd);
	if (unlikely(nsge < 0)) {
		CSIO_INC_STATS(scsim, n_dmamap_error);
		goto err;
	}

	/* Do we support so many mappings? */
	if (unlikely(nsge > scsim->max_sge)) {
		csio_warn(hw,
			  "More SGEs than can be supported."
			  " SGEs: %d, Max SGEs: %d\n", nsge, scsim->max_sge);
		CSIO_INC_STATS(scsim, n_unsupp_sge_error);
		goto err_dma_unmap;
	}

	/* Get a free ioreq structure - SM is already set to uninit */
	ioreq = csio_get_scsi_ioreq_lock(hw, scsim);
	if (!ioreq) {
		csio_err(hw, "Out of I/O request elements. Active #:%d\n",
			 scsim->stats.n_active);
		CSIO_INC_STATS(scsim, n_no_req_error);
		goto err_dma_unmap;
	}

	ioreq->nsge		= nsge;
	ioreq->lnode		= ln;
	ioreq->rnode		= rn;
	ioreq->iq_idx		= sqset->iq_idx;
	ioreq->eq_idx		= sqset->eq_idx;
	ioreq->wr_status	= 0;
	ioreq->drv_status	= 0;
	csio_scsi_cmnd(ioreq)	= (void *)cmnd;
	ioreq->tmo		= 0;
	ioreq->datadir		= cmnd->sc_data_direction;

	if (cmnd->sc_data_direction == DMA_TO_DEVICE) {
		CSIO_INC_STATS(ln, n_output_requests);
		ln->stats.n_output_bytes += scsi_bufflen(cmnd);
	} else if (cmnd->sc_data_direction == DMA_FROM_DEVICE) {
		CSIO_INC_STATS(ln, n_input_requests);
		ln->stats.n_input_bytes += scsi_bufflen(cmnd);
	} else
		CSIO_INC_STATS(ln, n_control_requests);

	/* Set cbfn */
	ioreq->io_cbfn = csio_scsi_cbfn;

	/* Needed during abort */
	cmnd->host_scribble = (unsigned char *)ioreq;
	csio_priv(cmnd)->fc_tm_flags = 0;

	/* Kick off SCSI IO SM on the ioreq */
	spin_lock_irqsave(&hw->lock, flags);
	retval = csio_scsi_start_io(ioreq);
	spin_unlock_irqrestore(&hw->lock, flags);

	if (retval != 0) {
		csio_err(hw, "ioreq: %p couldn't be started, status:%d\n",
			 ioreq, retval);
		CSIO_INC_STATS(scsim, n_busy_error);
		goto err_put_req;
	}

	return 0;

err_put_req:
	csio_put_scsi_ioreq_lock(hw, scsim, ioreq);
err_dma_unmap:
	if (nsge > 0)
		scsi_dma_unmap(cmnd);
err:
	return rv;

err_done:
	scsi_done(cmnd);
	return 0;
}

static int
csio_do_abrt_cls(struct csio_hw *hw, struct csio_ioreq *ioreq, bool abort)
{
	int rv;
	int cpu = smp_processor_id();
	struct csio_lnode *ln = ioreq->lnode;
	struct csio_scsi_qset *sqset = &hw->sqset[ln->portid][cpu];

	ioreq->tmo = CSIO_SCSI_ABRT_TMO_MS;
	/*
	 * Use current processor queue for posting the abort/close, but retain
	 * the ingress queue ID of the original I/O being aborted/closed - we
	 * need the abort/close completion to be received on the same queue
	 * as the original I/O.
	 */
	ioreq->eq_idx = sqset->eq_idx;

	if (abort == SCSI_ABORT)
		rv = csio_scsi_abort(ioreq);
	else
		rv = csio_scsi_close(ioreq);

	return rv;
}

static int
csio_eh_abort_handler(struct scsi_cmnd *cmnd)
{
	struct csio_ioreq *ioreq;
	struct csio_lnode *ln = shost_priv(cmnd->device->host);
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	struct csio_scsim *scsim = csio_hw_to_scsim(hw);
	int ready = 0, ret;
	unsigned long tmo = 0;
	int rv;
	struct csio_rnode *rn = (struct csio_rnode *)(cmnd->device->hostdata);

	ret = fc_block_scsi_eh(cmnd);
	if (ret)
		return ret;

	ioreq = (struct csio_ioreq *)cmnd->host_scribble;
	if (!ioreq)
		return SUCCESS;

	if (!rn)
		return FAILED;

	csio_dbg(hw,
		 "Request to abort ioreq:%p cmd:%p cdb:%08llx"
		 " ssni:0x%x lun:%llu iq:0x%x\n",
		ioreq, cmnd, *((uint64_t *)cmnd->cmnd), rn->flowid,
		cmnd->device->lun, csio_q_physiqid(hw, ioreq->iq_idx));

	if (((struct scsi_cmnd *)csio_scsi_cmnd(ioreq)) != cmnd) {
		CSIO_INC_STATS(scsim, n_abrt_race_comp);
		return SUCCESS;
	}

	ready = csio_is_lnode_ready(ln);
	tmo = CSIO_SCSI_ABRT_TMO_MS;

	reinit_completion(&ioreq->cmplobj);
	spin_lock_irq(&hw->lock);
	rv = csio_do_abrt_cls(hw, ioreq, (ready ? SCSI_ABORT : SCSI_CLOSE));
	spin_unlock_irq(&hw->lock);

	if (rv != 0) {
		if (rv == -EINVAL) {
			/* Return success, if abort/close request issued on
			 * already completed IO
			 */
			return SUCCESS;
		}
		if (ready)
			CSIO_INC_STATS(scsim, n_abrt_busy_error);
		else
			CSIO_INC_STATS(scsim, n_cls_busy_error);

		goto inval_scmnd;
	}

	wait_for_completion_timeout(&ioreq->cmplobj, msecs_to_jiffies(tmo));

	/* FW didnt respond to abort within our timeout */
	if (((struct scsi_cmnd *)csio_scsi_cmnd(ioreq)) == cmnd) {

		csio_err(hw, "Abort timed out -- req: %p\n", ioreq);
		CSIO_INC_STATS(scsim, n_abrt_timedout);

inval_scmnd:
		if (ioreq->nsge > 0)
			scsi_dma_unmap(cmnd);

		spin_lock_irq(&hw->lock);
		csio_scsi_cmnd(ioreq) = NULL;
		spin_unlock_irq(&hw->lock);

		cmnd->result = (DID_ERROR << 16);
		scsi_done(cmnd);

		return FAILED;
	}

	/* FW successfully aborted the request */
	if (host_byte(cmnd->result) == DID_REQUEUE) {
		csio_info(hw,
			"Aborted SCSI command to (%d:%llu) tag %u\n",
			cmnd->device->id, cmnd->device->lun,
			scsi_cmd_to_rq(cmnd)->tag);
		return SUCCESS;
	} else {
		csio_info(hw,
			"Failed to abort SCSI command, (%d:%llu) tag %u\n",
			cmnd->device->id, cmnd->device->lun,
			scsi_cmd_to_rq(cmnd)->tag);
		return FAILED;
	}
}

/*
 * csio_tm_cbfn - TM callback function.
 * @hw: HW module.
 * @req: IO request.
 *
 * Cache the result in 'cmnd', since ioreq will be freed soon
 * after we return from here, and the waiting thread shouldnt trust
 * the ioreq contents.
 */
static void
csio_tm_cbfn(struct csio_hw *hw, struct csio_ioreq *req)
{
	struct scsi_cmnd *cmnd  = (struct scsi_cmnd *)csio_scsi_cmnd(req);
	struct csio_dma_buf *dma_buf;
	uint8_t flags = 0;
	struct fcp_resp_with_ext *fcp_resp;
	struct fcp_resp_rsp_info *rsp_info;

	csio_dbg(hw, "req: %p in csio_tm_cbfn status: %d\n",
		      req, req->wr_status);

	/* Cache FW return status */
	csio_priv(cmnd)->wr_status = req->wr_status;

	/* Special handling based on FCP response */

	/*
	 * FW returns us this error, if flags were set. FCP4 says
	 * FCP_RSP_LEN_VAL in flags shall be set for TM completions.
	 * So if a target were to set this bit, we expect that the
	 * rsp_code is set to FCP_TMF_CMPL for a successful TM
	 * completion. Any other rsp_code means TM operation failed.
	 * If a target were to just ignore setting flags, we treat
	 * the TM operation as success, and FW returns FW_SUCCESS.
	 */
	if (req->wr_status == FW_SCSI_RSP_ERR) {
		dma_buf = &req->dma_buf;
		fcp_resp = (struct fcp_resp_with_ext *)dma_buf->vaddr;
		rsp_info = (struct fcp_resp_rsp_info *)(fcp_resp + 1);

		flags = fcp_resp->resp.fr_flags;

		/* Modify return status if flags indicate success */
		if (flags & FCP_RSP_LEN_VAL)
			if (rsp_info->rsp_code == FCP_TMF_CMPL)
				csio_priv(cmnd)->wr_status = FW_SUCCESS;

		csio_dbg(hw, "TM FCP rsp code: %d\n", rsp_info->rsp_code);
	}

	/* Wake up the TM handler thread */
	csio_scsi_cmnd(req) = NULL;
}

static int
csio_eh_lun_reset_handler(struct scsi_cmnd *cmnd)
{
	struct csio_lnode *ln = shost_priv(cmnd->device->host);
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	struct csio_scsim *scsim = csio_hw_to_scsim(hw);
	struct csio_rnode *rn = (struct csio_rnode *)(cmnd->device->hostdata);
	struct csio_ioreq *ioreq = NULL;
	struct csio_scsi_qset *sqset;
	unsigned long flags;
	int retval;
	int count, ret;
	LIST_HEAD(local_q);
	struct csio_scsi_level_data sld;

	if (!rn)
		goto fail;

	csio_dbg(hw, "Request to reset LUN:%llu (ssni:0x%x tgtid:%d)\n",
		      cmnd->device->lun, rn->flowid, rn->scsi_id);

	if (!csio_is_lnode_ready(ln)) {
		csio_err(hw,
			 "LUN reset cannot be issued on non-ready"
			 " local node vnpi:0x%x (LUN:%llu)\n",
			 ln->vnp_flowid, cmnd->device->lun);
		goto fail;
	}

	/* Lnode is ready, now wait on rport node readiness */
	ret = fc_block_scsi_eh(cmnd);
	if (ret)
		return ret;

	/*
	 * If we have blocked in the previous call, at this point, either the
	 * remote node has come back online, or device loss timer has fired
	 * and the remote node is destroyed. Allow the LUN reset only for
	 * the former case, since LUN reset is a TMF I/O on the wire, and we
	 * need a valid session to issue it.
	 */
	if (fc_remote_port_chkready(rn->rport)) {
		csio_err(hw,
			 "LUN reset cannot be issued on non-ready"
			 " remote node ssni:0x%x (LUN:%llu)\n",
			 rn->flowid, cmnd->device->lun);
		goto fail;
	}

	/* Get a free ioreq structure - SM is already set to uninit */
	ioreq = csio_get_scsi_ioreq_lock(hw, scsim);

	if (!ioreq) {
		csio_err(hw, "Out of IO request elements. Active # :%d\n",
			 scsim->stats.n_active);
		goto fail;
	}

	sqset			= &hw->sqset[ln->portid][smp_processor_id()];
	ioreq->nsge		= 0;
	ioreq->lnode		= ln;
	ioreq->rnode		= rn;
	ioreq->iq_idx		= sqset->iq_idx;
	ioreq->eq_idx		= sqset->eq_idx;

	csio_scsi_cmnd(ioreq)	= cmnd;
	cmnd->host_scribble	= (unsigned char *)ioreq;
	csio_priv(cmnd)->wr_status = 0;

	csio_priv(cmnd)->fc_tm_flags = FCP_TMF_LUN_RESET;
	ioreq->tmo		= CSIO_SCSI_LUNRST_TMO_MS / 1000;

	/*
	 * FW times the LUN reset for ioreq->tmo, so we got to wait a little
	 * longer (10s for now) than that to allow FW to return the timed
	 * out command.
	 */
	count = DIV_ROUND_UP((ioreq->tmo + 10) * 1000, CSIO_SCSI_TM_POLL_MS);

	/* Set cbfn */
	ioreq->io_cbfn = csio_tm_cbfn;

	/* Save of the ioreq info for later use */
	sld.level = CSIO_LEV_LUN;
	sld.lnode = ioreq->lnode;
	sld.rnode = ioreq->rnode;
	sld.oslun = cmnd->device->lun;

	spin_lock_irqsave(&hw->lock, flags);
	/* Kick off TM SM on the ioreq */
	retval = csio_scsi_start_tm(ioreq);
	spin_unlock_irqrestore(&hw->lock, flags);

	if (retval != 0) {
		csio_err(hw, "Failed to issue LUN reset, req:%p, status:%d\n",
			    ioreq, retval);
		goto fail_ret_ioreq;
	}

	csio_dbg(hw, "Waiting max %d secs for LUN reset completion\n",
		    count * (CSIO_SCSI_TM_POLL_MS / 1000));
	/* Wait for completion */
	while ((((struct scsi_cmnd *)csio_scsi_cmnd(ioreq)) == cmnd)
								&& count--)
		msleep(CSIO_SCSI_TM_POLL_MS);

	/* LUN reset timed-out */
	if (((struct scsi_cmnd *)csio_scsi_cmnd(ioreq)) == cmnd) {
		csio_err(hw, "LUN reset (%d:%llu) timed out\n",
			 cmnd->device->id, cmnd->device->lun);

		spin_lock_irq(&hw->lock);
		csio_scsi_drvcleanup(ioreq);
		list_del_init(&ioreq->sm.sm_list);
		spin_unlock_irq(&hw->lock);

		goto fail_ret_ioreq;
	}

	/* LUN reset returned, check cached status */
	if (csio_priv(cmnd)->wr_status != FW_SUCCESS) {
		csio_err(hw, "LUN reset failed (%d:%llu), status: %d\n",
			 cmnd->device->id, cmnd->device->lun,
			 csio_priv(cmnd)->wr_status);
		goto fail;
	}

	/* LUN reset succeeded, Start aborting affected I/Os */
	/*
	 * Since the host guarantees during LUN reset that there
	 * will not be any more I/Os to that LUN, until the LUN reset
	 * completes, we gather pending I/Os after the LUN reset.
	 */
	spin_lock_irq(&hw->lock);
	csio_scsi_gather_active_ios(scsim, &sld, &local_q);

	retval = csio_scsi_abort_io_q(scsim, &local_q, 30000);
	spin_unlock_irq(&hw->lock);

	/* Aborts may have timed out */
	if (retval != 0) {
		csio_err(hw,
			 "Attempt to abort I/Os during LUN reset of %llu"
			 " returned %d\n", cmnd->device->lun, retval);
		/* Return I/Os back to active_q */
		spin_lock_irq(&hw->lock);
		list_splice_tail_init(&local_q, &scsim->active_q);
		spin_unlock_irq(&hw->lock);
		goto fail;
	}

	CSIO_INC_STATS(rn, n_lun_rst);

	csio_info(hw, "LUN reset occurred (%d:%llu)\n",
		  cmnd->device->id, cmnd->device->lun);

	return SUCCESS;

fail_ret_ioreq:
	csio_put_scsi_ioreq_lock(hw, scsim, ioreq);
fail:
	CSIO_INC_STATS(rn, n_lun_rst_fail);
	return FAILED;
}

static int
csio_sdev_init(struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	sdev->hostdata = *((struct csio_lnode **)(rport->dd_data));

	return 0;
}

static int
csio_sdev_configure(struct scsi_device *sdev, struct queue_limits *lim)
{
	scsi_change_queue_depth(sdev, csio_lun_qdepth);
	return 0;
}

static void
csio_sdev_destroy(struct scsi_device *sdev)
{
	sdev->hostdata = NULL;
}

static int
csio_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct csio_lnode *ln = shost_priv(shost);
	int rv = 1;

	spin_lock_irq(shost->host_lock);
	if (!ln->hwp || csio_list_deleted(&ln->sm.sm_list))
		goto out;

	rv = csio_scan_done(ln, jiffies, time, csio_max_scan_tmo * HZ,
			    csio_delta_scan_tmo * HZ);
out:
	spin_unlock_irq(shost->host_lock);

	return rv;
}

struct scsi_host_template csio_fcoe_shost_template = {
	.module			= THIS_MODULE,
	.name			= CSIO_DRV_DESC,
	.proc_name		= KBUILD_MODNAME,
	.queuecommand		= csio_queuecommand,
	.cmd_size		= sizeof(struct csio_cmd_priv),
	.eh_timed_out		= fc_eh_timed_out,
	.eh_abort_handler	= csio_eh_abort_handler,
	.eh_device_reset_handler = csio_eh_lun_reset_handler,
	.sdev_init		= csio_sdev_init,
	.sdev_configure		= csio_sdev_configure,
	.sdev_destroy		= csio_sdev_destroy,
	.scan_finished		= csio_scan_finished,
	.this_id		= -1,
	.sg_tablesize		= CSIO_SCSI_MAX_SGE,
	.cmd_per_lun		= CSIO_MAX_CMD_PER_LUN,
	.shost_groups		= csio_fcoe_lport_groups,
	.max_sectors		= CSIO_MAX_SECTOR_SIZE,
};

struct scsi_host_template csio_fcoe_shost_vport_template = {
	.module			= THIS_MODULE,
	.name			= CSIO_DRV_DESC,
	.proc_name		= KBUILD_MODNAME,
	.queuecommand		= csio_queuecommand,
	.eh_timed_out		= fc_eh_timed_out,
	.eh_abort_handler	= csio_eh_abort_handler,
	.eh_device_reset_handler = csio_eh_lun_reset_handler,
	.sdev_init		= csio_sdev_init,
	.sdev_configure		= csio_sdev_configure,
	.sdev_destroy		= csio_sdev_destroy,
	.scan_finished		= csio_scan_finished,
	.this_id		= -1,
	.sg_tablesize		= CSIO_SCSI_MAX_SGE,
	.cmd_per_lun		= CSIO_MAX_CMD_PER_LUN,
	.shost_groups		= csio_fcoe_vport_groups,
	.max_sectors		= CSIO_MAX_SECTOR_SIZE,
};

/*
 * csio_scsi_alloc_ddp_bufs - Allocate buffers for DDP of unaligned SGLs.
 * @scm: SCSI Module
 * @hw: HW device.
 * @buf_size: buffer size
 * @num_buf : Number of buffers.
 *
 * This routine allocates DMA buffers required for SCSI Data xfer, if
 * each SGL buffer for a SCSI Read request posted by SCSI midlayer are
 * not virtually contiguous.
 */
static int
csio_scsi_alloc_ddp_bufs(struct csio_scsim *scm, struct csio_hw *hw,
			 int buf_size, int num_buf)
{
	int n = 0;
	struct list_head *tmp;
	struct csio_dma_buf *ddp_desc = NULL;
	uint32_t unit_size = 0;

	if (!num_buf)
		return 0;

	if (!buf_size)
		return -EINVAL;

	INIT_LIST_HEAD(&scm->ddp_freelist);

	/* Align buf size to page size */
	buf_size = (buf_size + PAGE_SIZE - 1) & PAGE_MASK;
	/* Initialize dma descriptors */
	for (n = 0; n < num_buf; n++) {
		/* Set unit size to request size */
		unit_size = buf_size;
		ddp_desc = kzalloc(sizeof(struct csio_dma_buf), GFP_KERNEL);
		if (!ddp_desc) {
			csio_err(hw,
				 "Failed to allocate ddp descriptors,"
				 " Num allocated = %d.\n",
				 scm->stats.n_free_ddp);
			goto no_mem;
		}

		/* Allocate Dma buffers for DDP */
		ddp_desc->vaddr = dma_alloc_coherent(&hw->pdev->dev, unit_size,
				&ddp_desc->paddr, GFP_KERNEL);
		if (!ddp_desc->vaddr) {
			csio_err(hw,
				 "SCSI response DMA buffer (ddp) allocation"
				 " failed!\n");
			kfree(ddp_desc);
			goto no_mem;
		}

		ddp_desc->len = unit_size;

		/* Added it to scsi ddp freelist */
		list_add_tail(&ddp_desc->list, &scm->ddp_freelist);
		CSIO_INC_STATS(scm, n_free_ddp);
	}

	return 0;
no_mem:
	/* release dma descs back to freelist and free dma memory */
	list_for_each(tmp, &scm->ddp_freelist) {
		ddp_desc = (struct csio_dma_buf *) tmp;
		tmp = csio_list_prev(tmp);
		dma_free_coherent(&hw->pdev->dev, ddp_desc->len,
				  ddp_desc->vaddr, ddp_desc->paddr);
		list_del_init(&ddp_desc->list);
		kfree(ddp_desc);
	}
	scm->stats.n_free_ddp = 0;

	return -ENOMEM;
}

/*
 * csio_scsi_free_ddp_bufs - free DDP buffers of unaligned SGLs.
 * @scm: SCSI Module
 * @hw: HW device.
 *
 * This routine frees ddp buffers.
 */
static void
csio_scsi_free_ddp_bufs(struct csio_scsim *scm, struct csio_hw *hw)
{
	struct list_head *tmp;
	struct csio_dma_buf *ddp_desc;

	/* release dma descs back to freelist and free dma memory */
	list_for_each(tmp, &scm->ddp_freelist) {
		ddp_desc = (struct csio_dma_buf *) tmp;
		tmp = csio_list_prev(tmp);
		dma_free_coherent(&hw->pdev->dev, ddp_desc->len,
				  ddp_desc->vaddr, ddp_desc->paddr);
		list_del_init(&ddp_desc->list);
		kfree(ddp_desc);
	}
	scm->stats.n_free_ddp = 0;
}

/**
 * csio_scsim_init - Initialize SCSI Module
 * @scm:	SCSI Module
 * @hw:		HW module
 *
 */
int
csio_scsim_init(struct csio_scsim *scm, struct csio_hw *hw)
{
	int i;
	struct csio_ioreq *ioreq;
	struct csio_dma_buf *dma_buf;

	INIT_LIST_HEAD(&scm->active_q);
	scm->hw = hw;

	scm->proto_cmd_len = sizeof(struct fcp_cmnd);
	scm->proto_rsp_len = CSIO_SCSI_RSP_LEN;
	scm->max_sge = CSIO_SCSI_MAX_SGE;

	spin_lock_init(&scm->freelist_lock);

	/* Pre-allocate ioreqs and initialize them */
	INIT_LIST_HEAD(&scm->ioreq_freelist);
	for (i = 0; i < csio_scsi_ioreqs; i++) {

		ioreq = kzalloc(sizeof(struct csio_ioreq), GFP_KERNEL);
		if (!ioreq) {
			csio_err(hw,
				 "I/O request element allocation failed, "
				 " Num allocated = %d.\n",
				 scm->stats.n_free_ioreq);

			goto free_ioreq;
		}

		/* Allocate Dma buffers for Response Payload */
		dma_buf = &ioreq->dma_buf;
		dma_buf->vaddr = dma_pool_alloc(hw->scsi_dma_pool, GFP_KERNEL,
						&dma_buf->paddr);
		if (!dma_buf->vaddr) {
			csio_err(hw,
				 "SCSI response DMA buffer allocation"
				 " failed!\n");
			kfree(ioreq);
			goto free_ioreq;
		}

		dma_buf->len = scm->proto_rsp_len;

		/* Set state to uninit */
		csio_init_state(&ioreq->sm, csio_scsis_uninit);
		INIT_LIST_HEAD(&ioreq->gen_list);
		init_completion(&ioreq->cmplobj);

		list_add_tail(&ioreq->sm.sm_list, &scm->ioreq_freelist);
		CSIO_INC_STATS(scm, n_free_ioreq);
	}

	if (csio_scsi_alloc_ddp_bufs(scm, hw, PAGE_SIZE, csio_ddp_descs))
		goto free_ioreq;

	return 0;

free_ioreq:
	/*
	 * Free up existing allocations, since an error
	 * from here means we are returning for good
	 */
	while (!list_empty(&scm->ioreq_freelist)) {
		struct csio_sm *tmp;

		tmp = list_first_entry(&scm->ioreq_freelist,
				       struct csio_sm, sm_list);
		list_del_init(&tmp->sm_list);
		ioreq = (struct csio_ioreq *)tmp;

		dma_buf = &ioreq->dma_buf;
		dma_pool_free(hw->scsi_dma_pool, dma_buf->vaddr,
			      dma_buf->paddr);

		kfree(ioreq);
	}

	scm->stats.n_free_ioreq = 0;

	return -ENOMEM;
}

/**
 * csio_scsim_exit: Uninitialize SCSI Module
 * @scm: SCSI Module
 *
 */
void
csio_scsim_exit(struct csio_scsim *scm)
{
	struct csio_ioreq *ioreq;
	struct csio_dma_buf *dma_buf;

	while (!list_empty(&scm->ioreq_freelist)) {
		struct csio_sm *tmp;

		tmp = list_first_entry(&scm->ioreq_freelist,
				       struct csio_sm, sm_list);
		list_del_init(&tmp->sm_list);
		ioreq = (struct csio_ioreq *)tmp;

		dma_buf = &ioreq->dma_buf;
		dma_pool_free(scm->hw->scsi_dma_pool, dma_buf->vaddr,
			      dma_buf->paddr);

		kfree(ioreq);
	}

	scm->stats.n_free_ioreq = 0;

	csio_scsi_free_ddp_bufs(scm, scm->hw);
}
