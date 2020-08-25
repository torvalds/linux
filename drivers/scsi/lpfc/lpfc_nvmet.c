/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channsel Host Bus Adapters.                               *
 * Copyright (C) 2017-2019 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 ********************************************************************/
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/unaligned.h>
#include <linux/crc-t10dif.h>
#include <net/checksum.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/fc/fc_fs.h>

#include "lpfc_version.h"
#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "lpfc_nvme.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_debugfs.h"

static struct lpfc_iocbq *lpfc_nvmet_prep_ls_wqe(struct lpfc_hba *,
						 struct lpfc_async_xchg_ctx *,
						 dma_addr_t rspbuf,
						 uint16_t rspsize);
static struct lpfc_iocbq *lpfc_nvmet_prep_fcp_wqe(struct lpfc_hba *,
						  struct lpfc_async_xchg_ctx *);
static int lpfc_nvmet_sol_fcp_issue_abort(struct lpfc_hba *,
					  struct lpfc_async_xchg_ctx *,
					  uint32_t, uint16_t);
static int lpfc_nvmet_unsol_fcp_issue_abort(struct lpfc_hba *,
					    struct lpfc_async_xchg_ctx *,
					    uint32_t, uint16_t);
static void lpfc_nvmet_wqfull_flush(struct lpfc_hba *, struct lpfc_queue *,
				    struct lpfc_async_xchg_ctx *);
static void lpfc_nvmet_fcp_rqst_defer_work(struct work_struct *);

static void lpfc_nvmet_process_rcv_fcp_req(struct lpfc_nvmet_ctxbuf *ctx_buf);

static union lpfc_wqe128 lpfc_tsend_cmd_template;
static union lpfc_wqe128 lpfc_treceive_cmd_template;
static union lpfc_wqe128 lpfc_trsp_cmd_template;

/* Setup WQE templates for NVME IOs */
void
lpfc_nvmet_cmd_template(void)
{
	union lpfc_wqe128 *wqe;

	/* TSEND template */
	wqe = &lpfc_tsend_cmd_template;
	memset(wqe, 0, sizeof(union lpfc_wqe128));

	/* Word 0, 1, 2 - BDE is variable */

	/* Word 3 - payload_offset_len is zero */

	/* Word 4 - relative_offset is variable */

	/* Word 5 - is zero */

	/* Word 6 - ctxt_tag, xri_tag is variable */

	/* Word 7 - wqe_ar is variable */
	bf_set(wqe_cmnd, &wqe->fcp_tsend.wqe_com, CMD_FCP_TSEND64_WQE);
	bf_set(wqe_pu, &wqe->fcp_tsend.wqe_com, PARM_REL_OFF);
	bf_set(wqe_class, &wqe->fcp_tsend.wqe_com, CLASS3);
	bf_set(wqe_ct, &wqe->fcp_tsend.wqe_com, SLI4_CT_RPI);
	bf_set(wqe_ar, &wqe->fcp_tsend.wqe_com, 1);

	/* Word 8 - abort_tag is variable */

	/* Word 9  - reqtag, rcvoxid is variable */

	/* Word 10 - wqes, xc is variable */
	bf_set(wqe_nvme, &wqe->fcp_tsend.wqe_com, 1);
	bf_set(wqe_dbde, &wqe->fcp_tsend.wqe_com, 1);
	bf_set(wqe_wqes, &wqe->fcp_tsend.wqe_com, 0);
	bf_set(wqe_xc, &wqe->fcp_tsend.wqe_com, 1);
	bf_set(wqe_iod, &wqe->fcp_tsend.wqe_com, LPFC_WQE_IOD_WRITE);
	bf_set(wqe_lenloc, &wqe->fcp_tsend.wqe_com, LPFC_WQE_LENLOC_WORD12);

	/* Word 11 - sup, irsp, irsplen is variable */
	bf_set(wqe_cmd_type, &wqe->fcp_tsend.wqe_com, FCP_COMMAND_TSEND);
	bf_set(wqe_cqid, &wqe->fcp_tsend.wqe_com, LPFC_WQE_CQ_ID_DEFAULT);
	bf_set(wqe_sup, &wqe->fcp_tsend.wqe_com, 0);
	bf_set(wqe_irsp, &wqe->fcp_tsend.wqe_com, 0);
	bf_set(wqe_irsplen, &wqe->fcp_tsend.wqe_com, 0);
	bf_set(wqe_pbde, &wqe->fcp_tsend.wqe_com, 0);

	/* Word 12 - fcp_data_len is variable */

	/* Word 13, 14, 15 - PBDE is zero */

	/* TRECEIVE template */
	wqe = &lpfc_treceive_cmd_template;
	memset(wqe, 0, sizeof(union lpfc_wqe128));

	/* Word 0, 1, 2 - BDE is variable */

	/* Word 3 */
	wqe->fcp_treceive.payload_offset_len = TXRDY_PAYLOAD_LEN;

	/* Word 4 - relative_offset is variable */

	/* Word 5 - is zero */

	/* Word 6 - ctxt_tag, xri_tag is variable */

	/* Word 7 */
	bf_set(wqe_cmnd, &wqe->fcp_treceive.wqe_com, CMD_FCP_TRECEIVE64_WQE);
	bf_set(wqe_pu, &wqe->fcp_treceive.wqe_com, PARM_REL_OFF);
	bf_set(wqe_class, &wqe->fcp_treceive.wqe_com, CLASS3);
	bf_set(wqe_ct, &wqe->fcp_treceive.wqe_com, SLI4_CT_RPI);
	bf_set(wqe_ar, &wqe->fcp_treceive.wqe_com, 0);

	/* Word 8 - abort_tag is variable */

	/* Word 9  - reqtag, rcvoxid is variable */

	/* Word 10 - xc is variable */
	bf_set(wqe_dbde, &wqe->fcp_treceive.wqe_com, 1);
	bf_set(wqe_wqes, &wqe->fcp_treceive.wqe_com, 0);
	bf_set(wqe_nvme, &wqe->fcp_treceive.wqe_com, 1);
	bf_set(wqe_iod, &wqe->fcp_treceive.wqe_com, LPFC_WQE_IOD_READ);
	bf_set(wqe_lenloc, &wqe->fcp_treceive.wqe_com, LPFC_WQE_LENLOC_WORD12);
	bf_set(wqe_xc, &wqe->fcp_tsend.wqe_com, 1);

	/* Word 11 - pbde is variable */
	bf_set(wqe_cmd_type, &wqe->fcp_treceive.wqe_com, FCP_COMMAND_TRECEIVE);
	bf_set(wqe_cqid, &wqe->fcp_treceive.wqe_com, LPFC_WQE_CQ_ID_DEFAULT);
	bf_set(wqe_sup, &wqe->fcp_treceive.wqe_com, 0);
	bf_set(wqe_irsp, &wqe->fcp_treceive.wqe_com, 0);
	bf_set(wqe_irsplen, &wqe->fcp_treceive.wqe_com, 0);
	bf_set(wqe_pbde, &wqe->fcp_treceive.wqe_com, 1);

	/* Word 12 - fcp_data_len is variable */

	/* Word 13, 14, 15 - PBDE is variable */

	/* TRSP template */
	wqe = &lpfc_trsp_cmd_template;
	memset(wqe, 0, sizeof(union lpfc_wqe128));

	/* Word 0, 1, 2 - BDE is variable */

	/* Word 3 - response_len is variable */

	/* Word 4, 5 - is zero */

	/* Word 6 - ctxt_tag, xri_tag is variable */

	/* Word 7 */
	bf_set(wqe_cmnd, &wqe->fcp_trsp.wqe_com, CMD_FCP_TRSP64_WQE);
	bf_set(wqe_pu, &wqe->fcp_trsp.wqe_com, PARM_UNUSED);
	bf_set(wqe_class, &wqe->fcp_trsp.wqe_com, CLASS3);
	bf_set(wqe_ct, &wqe->fcp_trsp.wqe_com, SLI4_CT_RPI);
	bf_set(wqe_ag, &wqe->fcp_trsp.wqe_com, 1); /* wqe_ar */

	/* Word 8 - abort_tag is variable */

	/* Word 9  - reqtag is variable */

	/* Word 10 wqes, xc is variable */
	bf_set(wqe_dbde, &wqe->fcp_trsp.wqe_com, 1);
	bf_set(wqe_nvme, &wqe->fcp_trsp.wqe_com, 1);
	bf_set(wqe_wqes, &wqe->fcp_trsp.wqe_com, 0);
	bf_set(wqe_xc, &wqe->fcp_trsp.wqe_com, 0);
	bf_set(wqe_iod, &wqe->fcp_trsp.wqe_com, LPFC_WQE_IOD_NONE);
	bf_set(wqe_lenloc, &wqe->fcp_trsp.wqe_com, LPFC_WQE_LENLOC_WORD3);

	/* Word 11 irsp, irsplen is variable */
	bf_set(wqe_cmd_type, &wqe->fcp_trsp.wqe_com, FCP_COMMAND_TRSP);
	bf_set(wqe_cqid, &wqe->fcp_trsp.wqe_com, LPFC_WQE_CQ_ID_DEFAULT);
	bf_set(wqe_sup, &wqe->fcp_trsp.wqe_com, 0);
	bf_set(wqe_irsp, &wqe->fcp_trsp.wqe_com, 0);
	bf_set(wqe_irsplen, &wqe->fcp_trsp.wqe_com, 0);
	bf_set(wqe_pbde, &wqe->fcp_trsp.wqe_com, 0);

	/* Word 12, 13, 14, 15 - is zero */
}

#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
static struct lpfc_async_xchg_ctx *
lpfc_nvmet_get_ctx_for_xri(struct lpfc_hba *phba, u16 xri)
{
	struct lpfc_async_xchg_ctx *ctxp;
	unsigned long iflag;
	bool found = false;

	spin_lock_irqsave(&phba->sli4_hba.t_active_list_lock, iflag);
	list_for_each_entry(ctxp, &phba->sli4_hba.t_active_ctx_list, list) {
		if (ctxp->ctxbuf->sglq->sli4_xritag != xri)
			continue;

		found = true;
		break;
	}
	spin_unlock_irqrestore(&phba->sli4_hba.t_active_list_lock, iflag);
	if (found)
		return ctxp;

	return NULL;
}

static struct lpfc_async_xchg_ctx *
lpfc_nvmet_get_ctx_for_oxid(struct lpfc_hba *phba, u16 oxid, u32 sid)
{
	struct lpfc_async_xchg_ctx *ctxp;
	unsigned long iflag;
	bool found = false;

	spin_lock_irqsave(&phba->sli4_hba.t_active_list_lock, iflag);
	list_for_each_entry(ctxp, &phba->sli4_hba.t_active_ctx_list, list) {
		if (ctxp->oxid != oxid || ctxp->sid != sid)
			continue;

		found = true;
		break;
	}
	spin_unlock_irqrestore(&phba->sli4_hba.t_active_list_lock, iflag);
	if (found)
		return ctxp;

	return NULL;
}
#endif

static void
lpfc_nvmet_defer_release(struct lpfc_hba *phba,
			struct lpfc_async_xchg_ctx *ctxp)
{
	lockdep_assert_held(&ctxp->ctxlock);

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6313 NVMET Defer ctx release oxid x%x flg x%x\n",
			ctxp->oxid, ctxp->flag);

	if (ctxp->flag & LPFC_NVME_CTX_RLS)
		return;

	ctxp->flag |= LPFC_NVME_CTX_RLS;
	spin_lock(&phba->sli4_hba.t_active_list_lock);
	list_del(&ctxp->list);
	spin_unlock(&phba->sli4_hba.t_active_list_lock);
	spin_lock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
	list_add_tail(&ctxp->list, &phba->sli4_hba.lpfc_abts_nvmet_ctx_list);
	spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
}

/**
 * __lpfc_nvme_xmt_ls_rsp_cmp - Generic completion handler for the
 *         transmission of an NVME LS response.
 * @phba: Pointer to HBA context object.
 * @cmdwqe: Pointer to driver command WQE object.
 * @wcqe: Pointer to driver response CQE object.
 *
 * The function is called from SLI ring event handler with no
 * lock held. The function frees memory resources used for the command
 * used to send the NVME LS RSP.
 **/
void
__lpfc_nvme_xmt_ls_rsp_cmp(struct lpfc_hba *phba, struct lpfc_iocbq *cmdwqe,
			   struct lpfc_wcqe_complete *wcqe)
{
	struct lpfc_async_xchg_ctx *axchg = cmdwqe->context2;
	struct nvmefc_ls_rsp *ls_rsp = &axchg->ls_rsp;
	uint32_t status, result;

	status = bf_get(lpfc_wcqe_c_status, wcqe) & LPFC_IOCB_STATUS_MASK;
	result = wcqe->parameter;

	if (axchg->state != LPFC_NVME_STE_LS_RSP || axchg->entry_cnt != 2) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6410 NVMEx LS cmpl state mismatch IO x%x: "
				"%d %d\n",
				axchg->oxid, axchg->state, axchg->entry_cnt);
	}

	lpfc_nvmeio_data(phba, "NVMEx LS  CMPL: xri x%x stat x%x result x%x\n",
			 axchg->oxid, status, result);

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_DISC,
			"6038 NVMEx LS rsp cmpl: %d %d oxid x%x\n",
			status, result, axchg->oxid);

	lpfc_nlp_put(cmdwqe->context1);
	cmdwqe->context2 = NULL;
	cmdwqe->context3 = NULL;
	lpfc_sli_release_iocbq(phba, cmdwqe);
	ls_rsp->done(ls_rsp);
	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_DISC,
			"6200 NVMEx LS rsp cmpl done status %d oxid x%x\n",
			status, axchg->oxid);
	kfree(axchg);
}

/**
 * lpfc_nvmet_xmt_ls_rsp_cmp - Completion handler for LS Response
 * @phba: Pointer to HBA context object.
 * @cmdwqe: Pointer to driver command WQE object.
 * @wcqe: Pointer to driver response CQE object.
 *
 * The function is called from SLI ring event handler with no
 * lock held. This function is the completion handler for NVME LS commands
 * The function updates any states and statistics, then calls the
 * generic completion handler to free resources.
 **/
static void
lpfc_nvmet_xmt_ls_rsp_cmp(struct lpfc_hba *phba, struct lpfc_iocbq *cmdwqe,
			  struct lpfc_wcqe_complete *wcqe)
{
	struct lpfc_nvmet_tgtport *tgtp;
	uint32_t status, result;

	if (!phba->targetport)
		goto finish;

	status = bf_get(lpfc_wcqe_c_status, wcqe) & LPFC_IOCB_STATUS_MASK;
	result = wcqe->parameter;

	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	if (tgtp) {
		if (status) {
			atomic_inc(&tgtp->xmt_ls_rsp_error);
			if (result == IOERR_ABORT_REQUESTED)
				atomic_inc(&tgtp->xmt_ls_rsp_aborted);
			if (bf_get(lpfc_wcqe_c_xb, wcqe))
				atomic_inc(&tgtp->xmt_ls_rsp_xb_set);
		} else {
			atomic_inc(&tgtp->xmt_ls_rsp_cmpl);
		}
	}

finish:
	__lpfc_nvme_xmt_ls_rsp_cmp(phba, cmdwqe, wcqe);
}

/**
 * lpfc_nvmet_ctxbuf_post - Repost a NVMET RQ DMA buffer and clean up context
 * @phba: HBA buffer is associated with
 * @ctxp: context to clean up
 * @mp: Buffer to free
 *
 * Description: Frees the given DMA buffer in the appropriate way given by
 * reposting it to its associated RQ so it can be reused.
 *
 * Notes: Takes phba->hbalock.  Can be called with or without other locks held.
 *
 * Returns: None
 **/
void
lpfc_nvmet_ctxbuf_post(struct lpfc_hba *phba, struct lpfc_nvmet_ctxbuf *ctx_buf)
{
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	struct lpfc_async_xchg_ctx *ctxp = ctx_buf->context;
	struct lpfc_nvmet_tgtport *tgtp;
	struct fc_frame_header *fc_hdr;
	struct rqb_dmabuf *nvmebuf;
	struct lpfc_nvmet_ctx_info *infop;
	uint32_t size, oxid, sid;
	int cpu;
	unsigned long iflag;

	if (ctxp->state == LPFC_NVME_STE_FREE) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6411 NVMET free, already free IO x%x: %d %d\n",
				ctxp->oxid, ctxp->state, ctxp->entry_cnt);
	}

	if (ctxp->rqb_buffer) {
		spin_lock_irqsave(&ctxp->ctxlock, iflag);
		nvmebuf = ctxp->rqb_buffer;
		/* check if freed in another path whilst acquiring lock */
		if (nvmebuf) {
			ctxp->rqb_buffer = NULL;
			if (ctxp->flag & LPFC_NVME_CTX_REUSE_WQ) {
				ctxp->flag &= ~LPFC_NVME_CTX_REUSE_WQ;
				spin_unlock_irqrestore(&ctxp->ctxlock, iflag);
				nvmebuf->hrq->rqbp->rqb_free_buffer(phba,
								    nvmebuf);
			} else {
				spin_unlock_irqrestore(&ctxp->ctxlock, iflag);
				/* repost */
				lpfc_rq_buf_free(phba, &nvmebuf->hbuf);
			}
		} else {
			spin_unlock_irqrestore(&ctxp->ctxlock, iflag);
		}
	}
	ctxp->state = LPFC_NVME_STE_FREE;

	spin_lock_irqsave(&phba->sli4_hba.nvmet_io_wait_lock, iflag);
	if (phba->sli4_hba.nvmet_io_wait_cnt) {
		list_remove_head(&phba->sli4_hba.lpfc_nvmet_io_wait_list,
				 nvmebuf, struct rqb_dmabuf,
				 hbuf.list);
		phba->sli4_hba.nvmet_io_wait_cnt--;
		spin_unlock_irqrestore(&phba->sli4_hba.nvmet_io_wait_lock,
				       iflag);

		fc_hdr = (struct fc_frame_header *)(nvmebuf->hbuf.virt);
		oxid = be16_to_cpu(fc_hdr->fh_ox_id);
		tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
		size = nvmebuf->bytes_recv;
		sid = sli4_sid_from_fc_hdr(fc_hdr);

		ctxp = (struct lpfc_async_xchg_ctx *)ctx_buf->context;
		ctxp->wqeq = NULL;
		ctxp->offset = 0;
		ctxp->phba = phba;
		ctxp->size = size;
		ctxp->oxid = oxid;
		ctxp->sid = sid;
		ctxp->state = LPFC_NVME_STE_RCV;
		ctxp->entry_cnt = 1;
		ctxp->flag = 0;
		ctxp->ctxbuf = ctx_buf;
		ctxp->rqb_buffer = (void *)nvmebuf;
		spin_lock_init(&ctxp->ctxlock);

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
		/* NOTE: isr time stamp is stale when context is re-assigned*/
		if (ctxp->ts_isr_cmd) {
			ctxp->ts_cmd_nvme = 0;
			ctxp->ts_nvme_data = 0;
			ctxp->ts_data_wqput = 0;
			ctxp->ts_isr_data = 0;
			ctxp->ts_data_nvme = 0;
			ctxp->ts_nvme_status = 0;
			ctxp->ts_status_wqput = 0;
			ctxp->ts_isr_status = 0;
			ctxp->ts_status_nvme = 0;
		}
#endif
		atomic_inc(&tgtp->rcv_fcp_cmd_in);

		/* Indicate that a replacement buffer has been posted */
		spin_lock_irqsave(&ctxp->ctxlock, iflag);
		ctxp->flag |= LPFC_NVME_CTX_REUSE_WQ;
		spin_unlock_irqrestore(&ctxp->ctxlock, iflag);

		if (!queue_work(phba->wq, &ctx_buf->defer_work)) {
			atomic_inc(&tgtp->rcv_fcp_cmd_drop);
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"6181 Unable to queue deferred work "
					"for oxid x%x. "
					"FCP Drop IO [x%x x%x x%x]\n",
					ctxp->oxid,
					atomic_read(&tgtp->rcv_fcp_cmd_in),
					atomic_read(&tgtp->rcv_fcp_cmd_out),
					atomic_read(&tgtp->xmt_fcp_release));

			spin_lock_irqsave(&ctxp->ctxlock, iflag);
			lpfc_nvmet_defer_release(phba, ctxp);
			spin_unlock_irqrestore(&ctxp->ctxlock, iflag);
			lpfc_nvmet_unsol_fcp_issue_abort(phba, ctxp, sid, oxid);
		}
		return;
	}
	spin_unlock_irqrestore(&phba->sli4_hba.nvmet_io_wait_lock, iflag);

	/*
	 * Use the CPU context list, from the MRQ the IO was received on
	 * (ctxp->idx), to save context structure.
	 */
	spin_lock_irqsave(&phba->sli4_hba.t_active_list_lock, iflag);
	list_del_init(&ctxp->list);
	spin_unlock_irqrestore(&phba->sli4_hba.t_active_list_lock, iflag);
	cpu = raw_smp_processor_id();
	infop = lpfc_get_ctx_list(phba, cpu, ctxp->idx);
	spin_lock_irqsave(&infop->nvmet_ctx_list_lock, iflag);
	list_add_tail(&ctx_buf->list, &infop->nvmet_ctx_list);
	infop->nvmet_ctx_list_cnt++;
	spin_unlock_irqrestore(&infop->nvmet_ctx_list_lock, iflag);
#endif
}

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
static void
lpfc_nvmet_ktime(struct lpfc_hba *phba,
		 struct lpfc_async_xchg_ctx *ctxp)
{
	uint64_t seg1, seg2, seg3, seg4, seg5;
	uint64_t seg6, seg7, seg8, seg9, seg10;
	uint64_t segsum;

	if (!ctxp->ts_isr_cmd || !ctxp->ts_cmd_nvme ||
	    !ctxp->ts_nvme_data || !ctxp->ts_data_wqput ||
	    !ctxp->ts_isr_data || !ctxp->ts_data_nvme ||
	    !ctxp->ts_nvme_status || !ctxp->ts_status_wqput ||
	    !ctxp->ts_isr_status || !ctxp->ts_status_nvme)
		return;

	if (ctxp->ts_status_nvme < ctxp->ts_isr_cmd)
		return;
	if (ctxp->ts_isr_cmd  > ctxp->ts_cmd_nvme)
		return;
	if (ctxp->ts_cmd_nvme > ctxp->ts_nvme_data)
		return;
	if (ctxp->ts_nvme_data > ctxp->ts_data_wqput)
		return;
	if (ctxp->ts_data_wqput > ctxp->ts_isr_data)
		return;
	if (ctxp->ts_isr_data > ctxp->ts_data_nvme)
		return;
	if (ctxp->ts_data_nvme > ctxp->ts_nvme_status)
		return;
	if (ctxp->ts_nvme_status > ctxp->ts_status_wqput)
		return;
	if (ctxp->ts_status_wqput > ctxp->ts_isr_status)
		return;
	if (ctxp->ts_isr_status > ctxp->ts_status_nvme)
		return;
	/*
	 * Segment 1 - Time from FCP command received by MSI-X ISR
	 * to FCP command is passed to NVME Layer.
	 * Segment 2 - Time from FCP command payload handed
	 * off to NVME Layer to Driver receives a Command op
	 * from NVME Layer.
	 * Segment 3 - Time from Driver receives a Command op
	 * from NVME Layer to Command is put on WQ.
	 * Segment 4 - Time from Driver WQ put is done
	 * to MSI-X ISR for Command cmpl.
	 * Segment 5 - Time from MSI-X ISR for Command cmpl to
	 * Command cmpl is passed to NVME Layer.
	 * Segment 6 - Time from Command cmpl is passed to NVME
	 * Layer to Driver receives a RSP op from NVME Layer.
	 * Segment 7 - Time from Driver receives a RSP op from
	 * NVME Layer to WQ put is done on TRSP FCP Status.
	 * Segment 8 - Time from Driver WQ put is done on TRSP
	 * FCP Status to MSI-X ISR for TRSP cmpl.
	 * Segment 9 - Time from MSI-X ISR for TRSP cmpl to
	 * TRSP cmpl is passed to NVME Layer.
	 * Segment 10 - Time from FCP command received by
	 * MSI-X ISR to command is completed on wire.
	 * (Segments 1 thru 8) for READDATA / WRITEDATA
	 * (Segments 1 thru 4) for READDATA_RSP
	 */
	seg1 = ctxp->ts_cmd_nvme - ctxp->ts_isr_cmd;
	segsum = seg1;

	seg2 = ctxp->ts_nvme_data - ctxp->ts_isr_cmd;
	if (segsum > seg2)
		return;
	seg2 -= segsum;
	segsum += seg2;

	seg3 = ctxp->ts_data_wqput - ctxp->ts_isr_cmd;
	if (segsum > seg3)
		return;
	seg3 -= segsum;
	segsum += seg3;

	seg4 = ctxp->ts_isr_data - ctxp->ts_isr_cmd;
	if (segsum > seg4)
		return;
	seg4 -= segsum;
	segsum += seg4;

	seg5 = ctxp->ts_data_nvme - ctxp->ts_isr_cmd;
	if (segsum > seg5)
		return;
	seg5 -= segsum;
	segsum += seg5;


	/* For auto rsp commands seg6 thru seg10 will be 0 */
	if (ctxp->ts_nvme_status > ctxp->ts_data_nvme) {
		seg6 = ctxp->ts_nvme_status - ctxp->ts_isr_cmd;
		if (segsum > seg6)
			return;
		seg6 -= segsum;
		segsum += seg6;

		seg7 = ctxp->ts_status_wqput - ctxp->ts_isr_cmd;
		if (segsum > seg7)
			return;
		seg7 -= segsum;
		segsum += seg7;

		seg8 = ctxp->ts_isr_status - ctxp->ts_isr_cmd;
		if (segsum > seg8)
			return;
		seg8 -= segsum;
		segsum += seg8;

		seg9 = ctxp->ts_status_nvme - ctxp->ts_isr_cmd;
		if (segsum > seg9)
			return;
		seg9 -= segsum;
		segsum += seg9;

		if (ctxp->ts_isr_status < ctxp->ts_isr_cmd)
			return;
		seg10 = (ctxp->ts_isr_status -
			ctxp->ts_isr_cmd);
	} else {
		if (ctxp->ts_isr_data < ctxp->ts_isr_cmd)
			return;
		seg6 =  0;
		seg7 =  0;
		seg8 =  0;
		seg9 =  0;
		seg10 = (ctxp->ts_isr_data - ctxp->ts_isr_cmd);
	}

	phba->ktime_seg1_total += seg1;
	if (seg1 < phba->ktime_seg1_min)
		phba->ktime_seg1_min = seg1;
	else if (seg1 > phba->ktime_seg1_max)
		phba->ktime_seg1_max = seg1;

	phba->ktime_seg2_total += seg2;
	if (seg2 < phba->ktime_seg2_min)
		phba->ktime_seg2_min = seg2;
	else if (seg2 > phba->ktime_seg2_max)
		phba->ktime_seg2_max = seg2;

	phba->ktime_seg3_total += seg3;
	if (seg3 < phba->ktime_seg3_min)
		phba->ktime_seg3_min = seg3;
	else if (seg3 > phba->ktime_seg3_max)
		phba->ktime_seg3_max = seg3;

	phba->ktime_seg4_total += seg4;
	if (seg4 < phba->ktime_seg4_min)
		phba->ktime_seg4_min = seg4;
	else if (seg4 > phba->ktime_seg4_max)
		phba->ktime_seg4_max = seg4;

	phba->ktime_seg5_total += seg5;
	if (seg5 < phba->ktime_seg5_min)
		phba->ktime_seg5_min = seg5;
	else if (seg5 > phba->ktime_seg5_max)
		phba->ktime_seg5_max = seg5;

	phba->ktime_data_samples++;
	if (!seg6)
		goto out;

	phba->ktime_seg6_total += seg6;
	if (seg6 < phba->ktime_seg6_min)
		phba->ktime_seg6_min = seg6;
	else if (seg6 > phba->ktime_seg6_max)
		phba->ktime_seg6_max = seg6;

	phba->ktime_seg7_total += seg7;
	if (seg7 < phba->ktime_seg7_min)
		phba->ktime_seg7_min = seg7;
	else if (seg7 > phba->ktime_seg7_max)
		phba->ktime_seg7_max = seg7;

	phba->ktime_seg8_total += seg8;
	if (seg8 < phba->ktime_seg8_min)
		phba->ktime_seg8_min = seg8;
	else if (seg8 > phba->ktime_seg8_max)
		phba->ktime_seg8_max = seg8;

	phba->ktime_seg9_total += seg9;
	if (seg9 < phba->ktime_seg9_min)
		phba->ktime_seg9_min = seg9;
	else if (seg9 > phba->ktime_seg9_max)
		phba->ktime_seg9_max = seg9;
out:
	phba->ktime_seg10_total += seg10;
	if (seg10 < phba->ktime_seg10_min)
		phba->ktime_seg10_min = seg10;
	else if (seg10 > phba->ktime_seg10_max)
		phba->ktime_seg10_max = seg10;
	phba->ktime_status_samples++;
}
#endif

/**
 * lpfc_nvmet_xmt_fcp_op_cmp - Completion handler for FCP Response
 * @phba: Pointer to HBA context object.
 * @cmdwqe: Pointer to driver command WQE object.
 * @wcqe: Pointer to driver response CQE object.
 *
 * The function is called from SLI ring event handler with no
 * lock held. This function is the completion handler for NVME FCP commands
 * The function frees memory resources used for the NVME commands.
 **/
static void
lpfc_nvmet_xmt_fcp_op_cmp(struct lpfc_hba *phba, struct lpfc_iocbq *cmdwqe,
			  struct lpfc_wcqe_complete *wcqe)
{
	struct lpfc_nvmet_tgtport *tgtp;
	struct nvmefc_tgt_fcp_req *rsp;
	struct lpfc_async_xchg_ctx *ctxp;
	uint32_t status, result, op, start_clean, logerr;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	int id;
#endif

	ctxp = cmdwqe->context2;
	ctxp->flag &= ~LPFC_NVME_IO_INP;

	rsp = &ctxp->hdlrctx.fcp_req;
	op = rsp->op;

	status = bf_get(lpfc_wcqe_c_status, wcqe);
	result = wcqe->parameter;

	if (phba->targetport)
		tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	else
		tgtp = NULL;

	lpfc_nvmeio_data(phba, "NVMET FCP CMPL: xri x%x op x%x status x%x\n",
			 ctxp->oxid, op, status);

	if (status) {
		rsp->fcp_error = NVME_SC_DATA_XFER_ERROR;
		rsp->transferred_length = 0;
		if (tgtp) {
			atomic_inc(&tgtp->xmt_fcp_rsp_error);
			if (result == IOERR_ABORT_REQUESTED)
				atomic_inc(&tgtp->xmt_fcp_rsp_aborted);
		}

		logerr = LOG_NVME_IOERR;

		/* pick up SLI4 exhange busy condition */
		if (bf_get(lpfc_wcqe_c_xb, wcqe)) {
			ctxp->flag |= LPFC_NVME_XBUSY;
			logerr |= LOG_NVME_ABTS;
			if (tgtp)
				atomic_inc(&tgtp->xmt_fcp_rsp_xb_set);

		} else {
			ctxp->flag &= ~LPFC_NVME_XBUSY;
		}

		lpfc_printf_log(phba, KERN_INFO, logerr,
				"6315 IO Error Cmpl oxid: x%x xri: x%x %x/%x "
				"XBUSY:x%x\n",
				ctxp->oxid, ctxp->ctxbuf->sglq->sli4_xritag,
				status, result, ctxp->flag);

	} else {
		rsp->fcp_error = NVME_SC_SUCCESS;
		if (op == NVMET_FCOP_RSP)
			rsp->transferred_length = rsp->rsplen;
		else
			rsp->transferred_length = rsp->transfer_length;
		if (tgtp)
			atomic_inc(&tgtp->xmt_fcp_rsp_cmpl);
	}

	if ((op == NVMET_FCOP_READDATA_RSP) ||
	    (op == NVMET_FCOP_RSP)) {
		/* Sanity check */
		ctxp->state = LPFC_NVME_STE_DONE;
		ctxp->entry_cnt++;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
		if (ctxp->ts_cmd_nvme) {
			if (rsp->op == NVMET_FCOP_READDATA_RSP) {
				ctxp->ts_isr_data =
					cmdwqe->isr_timestamp;
				ctxp->ts_data_nvme =
					ktime_get_ns();
				ctxp->ts_nvme_status =
					ctxp->ts_data_nvme;
				ctxp->ts_status_wqput =
					ctxp->ts_data_nvme;
				ctxp->ts_isr_status =
					ctxp->ts_data_nvme;
				ctxp->ts_status_nvme =
					ctxp->ts_data_nvme;
			} else {
				ctxp->ts_isr_status =
					cmdwqe->isr_timestamp;
				ctxp->ts_status_nvme =
					ktime_get_ns();
			}
		}
#endif
		rsp->done(rsp);
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
		if (ctxp->ts_cmd_nvme)
			lpfc_nvmet_ktime(phba, ctxp);
#endif
		/* lpfc_nvmet_xmt_fcp_release() will recycle the context */
	} else {
		ctxp->entry_cnt++;
		start_clean = offsetof(struct lpfc_iocbq, iocb_flag);
		memset(((char *)cmdwqe) + start_clean, 0,
		       (sizeof(struct lpfc_iocbq) - start_clean));
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
		if (ctxp->ts_cmd_nvme) {
			ctxp->ts_isr_data = cmdwqe->isr_timestamp;
			ctxp->ts_data_nvme = ktime_get_ns();
		}
#endif
		rsp->done(rsp);
	}
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (phba->hdwqstat_on & LPFC_CHECK_NVMET_IO) {
		id = raw_smp_processor_id();
		this_cpu_inc(phba->sli4_hba.c_stat->cmpl_io);
		if (ctxp->cpu != id)
			lpfc_printf_log(phba, KERN_INFO, LOG_NVME_IOERR,
					"6704 CPU Check cmdcmpl: "
					"cpu %d expect %d\n",
					id, ctxp->cpu);
	}
#endif
}

/**
 * __lpfc_nvme_xmt_ls_rsp - Generic service routine to issue transmit
 *         an NVME LS rsp for a prior NVME LS request that was received.
 * @axchg: pointer to exchange context for the NVME LS request the response
 *         is for.
 * @ls_rsp: pointer to the transport LS RSP that is to be sent
 * @xmt_ls_rsp_cmp: completion routine to call upon RSP transmit done
 *
 * This routine is used to format and send a WQE to transmit a NVME LS
 * Response.  The response is for a prior NVME LS request that was
 * received and posted to the transport.
 *
 * Returns:
 *  0 : if response successfully transmit
 *  non-zero : if response failed to transmit, of the form -Exxx.
 **/
int
__lpfc_nvme_xmt_ls_rsp(struct lpfc_async_xchg_ctx *axchg,
			struct nvmefc_ls_rsp *ls_rsp,
			void (*xmt_ls_rsp_cmp)(struct lpfc_hba *phba,
				struct lpfc_iocbq *cmdwqe,
				struct lpfc_wcqe_complete *wcqe))
{
	struct lpfc_hba *phba = axchg->phba;
	struct hbq_dmabuf *nvmebuf = (struct hbq_dmabuf *)axchg->rqb_buffer;
	struct lpfc_iocbq *nvmewqeq;
	struct lpfc_dmabuf dmabuf;
	struct ulp_bde64 bpl;
	int rc;

	if (phba->pport->load_flag & FC_UNLOADING)
		return -ENODEV;

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_DISC,
			"6023 NVMEx LS rsp oxid x%x\n", axchg->oxid);

	if (axchg->state != LPFC_NVME_STE_LS_RCV || axchg->entry_cnt != 1) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6412 NVMEx LS rsp state mismatch "
				"oxid x%x: %d %d\n",
				axchg->oxid, axchg->state, axchg->entry_cnt);
		return -EALREADY;
	}
	axchg->state = LPFC_NVME_STE_LS_RSP;
	axchg->entry_cnt++;

	nvmewqeq = lpfc_nvmet_prep_ls_wqe(phba, axchg, ls_rsp->rspdma,
					 ls_rsp->rsplen);
	if (nvmewqeq == NULL) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6150 NVMEx LS Drop Rsp x%x: Prep\n",
				axchg->oxid);
		rc = -ENOMEM;
		goto out_free_buf;
	}

	/* Save numBdes for bpl2sgl */
	nvmewqeq->rsvd2 = 1;
	nvmewqeq->hba_wqidx = 0;
	nvmewqeq->context3 = &dmabuf;
	dmabuf.virt = &bpl;
	bpl.addrLow = nvmewqeq->wqe.xmit_sequence.bde.addrLow;
	bpl.addrHigh = nvmewqeq->wqe.xmit_sequence.bde.addrHigh;
	bpl.tus.f.bdeSize = ls_rsp->rsplen;
	bpl.tus.f.bdeFlags = 0;
	bpl.tus.w = le32_to_cpu(bpl.tus.w);
	/*
	 * Note: although we're using stack space for the dmabuf, the
	 * call to lpfc_sli4_issue_wqe is synchronous, so it will not
	 * be referenced after it returns back to this routine.
	 */

	nvmewqeq->wqe_cmpl = xmt_ls_rsp_cmp;
	nvmewqeq->iocb_cmpl = NULL;
	nvmewqeq->context2 = axchg;

	lpfc_nvmeio_data(phba, "NVMEx LS RSP: xri x%x wqidx x%x len x%x\n",
			 axchg->oxid, nvmewqeq->hba_wqidx, ls_rsp->rsplen);

	rc = lpfc_sli4_issue_wqe(phba, axchg->hdwq, nvmewqeq);

	/* clear to be sure there's no reference */
	nvmewqeq->context3 = NULL;

	if (rc == WQE_SUCCESS) {
		/*
		 * Okay to repost buffer here, but wait till cmpl
		 * before freeing ctxp and iocbq.
		 */
		lpfc_in_buf_free(phba, &nvmebuf->dbuf);
		return 0;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"6151 NVMEx LS RSP x%x: failed to transmit %d\n",
			axchg->oxid, rc);

	rc = -ENXIO;

	lpfc_nlp_put(nvmewqeq->context1);

out_free_buf:
	/* Give back resources */
	lpfc_in_buf_free(phba, &nvmebuf->dbuf);

	/*
	 * As transport doesn't track completions of responses, if the rsp
	 * fails to send, the transport will effectively ignore the rsp
	 * and consider the LS done. However, the driver has an active
	 * exchange open for the LS - so be sure to abort the exchange
	 * if the response isn't sent.
	 */
	lpfc_nvme_unsol_ls_issue_abort(phba, axchg, axchg->sid, axchg->oxid);
	return rc;
}

/**
 * lpfc_nvmet_xmt_ls_rsp - Transmit NVME LS response
 * @tgtport: pointer to target port that NVME LS is to be transmit from.
 * @ls_rsp: pointer to the transport LS RSP that is to be sent
 *
 * Driver registers this routine to transmit responses for received NVME
 * LS requests.
 *
 * This routine is used to format and send a WQE to transmit a NVME LS
 * Response. The ls_rsp is used to reverse-map the LS to the original
 * NVME LS request sequence, which provides addressing information for
 * the remote port the LS to be sent to, as well as the exchange id
 * that is the LS is bound to.
 *
 * Returns:
 *  0 : if response successfully transmit
 *  non-zero : if response failed to transmit, of the form -Exxx.
 **/
static int
lpfc_nvmet_xmt_ls_rsp(struct nvmet_fc_target_port *tgtport,
		      struct nvmefc_ls_rsp *ls_rsp)
{
	struct lpfc_async_xchg_ctx *axchg =
		container_of(ls_rsp, struct lpfc_async_xchg_ctx, ls_rsp);
	struct lpfc_nvmet_tgtport *nvmep = tgtport->private;
	int rc;

	if (axchg->phba->pport->load_flag & FC_UNLOADING)
		return -ENODEV;

	rc = __lpfc_nvme_xmt_ls_rsp(axchg, ls_rsp, lpfc_nvmet_xmt_ls_rsp_cmp);

	if (rc) {
		atomic_inc(&nvmep->xmt_ls_drop);
		/*
		 * unless the failure is due to having already sent
		 * the response, an abort will be generated for the
		 * exchange if the rsp can't be sent.
		 */
		if (rc != -EALREADY)
			atomic_inc(&nvmep->xmt_ls_abort);
		return rc;
	}

	atomic_inc(&nvmep->xmt_ls_rsp);
	return 0;
}

static int
lpfc_nvmet_xmt_fcp_op(struct nvmet_fc_target_port *tgtport,
		      struct nvmefc_tgt_fcp_req *rsp)
{
	struct lpfc_nvmet_tgtport *lpfc_nvmep = tgtport->private;
	struct lpfc_async_xchg_ctx *ctxp =
		container_of(rsp, struct lpfc_async_xchg_ctx, hdlrctx.fcp_req);
	struct lpfc_hba *phba = ctxp->phba;
	struct lpfc_queue *wq;
	struct lpfc_iocbq *nvmewqeq;
	struct lpfc_sli_ring *pring;
	unsigned long iflags;
	int rc;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	int id;
#endif

	if (phba->pport->load_flag & FC_UNLOADING) {
		rc = -ENODEV;
		goto aerr;
	}

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (ctxp->ts_cmd_nvme) {
		if (rsp->op == NVMET_FCOP_RSP)
			ctxp->ts_nvme_status = ktime_get_ns();
		else
			ctxp->ts_nvme_data = ktime_get_ns();
	}

	/* Setup the hdw queue if not already set */
	if (!ctxp->hdwq)
		ctxp->hdwq = &phba->sli4_hba.hdwq[rsp->hwqid];

	if (phba->hdwqstat_on & LPFC_CHECK_NVMET_IO) {
		id = raw_smp_processor_id();
		this_cpu_inc(phba->sli4_hba.c_stat->xmt_io);
		if (rsp->hwqid != id)
			lpfc_printf_log(phba, KERN_INFO, LOG_NVME_IOERR,
					"6705 CPU Check OP: "
					"cpu %d expect %d\n",
					id, rsp->hwqid);
		ctxp->cpu = id; /* Setup cpu for cmpl check */
	}
#endif

	/* Sanity check */
	if ((ctxp->flag & LPFC_NVME_ABTS_RCV) ||
	    (ctxp->state == LPFC_NVME_STE_ABORT)) {
		atomic_inc(&lpfc_nvmep->xmt_fcp_drop);
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6102 IO oxid x%x aborted\n",
				ctxp->oxid);
		rc = -ENXIO;
		goto aerr;
	}

	nvmewqeq = lpfc_nvmet_prep_fcp_wqe(phba, ctxp);
	if (nvmewqeq == NULL) {
		atomic_inc(&lpfc_nvmep->xmt_fcp_drop);
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6152 FCP Drop IO x%x: Prep\n",
				ctxp->oxid);
		rc = -ENXIO;
		goto aerr;
	}

	nvmewqeq->wqe_cmpl = lpfc_nvmet_xmt_fcp_op_cmp;
	nvmewqeq->iocb_cmpl = NULL;
	nvmewqeq->context2 = ctxp;
	nvmewqeq->iocb_flag |=  LPFC_IO_NVMET;
	ctxp->wqeq->hba_wqidx = rsp->hwqid;

	lpfc_nvmeio_data(phba, "NVMET FCP CMND: xri x%x op x%x len x%x\n",
			 ctxp->oxid, rsp->op, rsp->rsplen);

	ctxp->flag |= LPFC_NVME_IO_INP;
	rc = lpfc_sli4_issue_wqe(phba, ctxp->hdwq, nvmewqeq);
	if (rc == WQE_SUCCESS) {
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
		if (!ctxp->ts_cmd_nvme)
			return 0;
		if (rsp->op == NVMET_FCOP_RSP)
			ctxp->ts_status_wqput = ktime_get_ns();
		else
			ctxp->ts_data_wqput = ktime_get_ns();
#endif
		return 0;
	}

	if (rc == -EBUSY) {
		/*
		 * WQ was full, so queue nvmewqeq to be sent after
		 * WQE release CQE
		 */
		ctxp->flag |= LPFC_NVME_DEFER_WQFULL;
		wq = ctxp->hdwq->io_wq;
		pring = wq->pring;
		spin_lock_irqsave(&pring->ring_lock, iflags);
		list_add_tail(&nvmewqeq->list, &wq->wqfull_list);
		wq->q_flag |= HBA_NVMET_WQFULL;
		spin_unlock_irqrestore(&pring->ring_lock, iflags);
		atomic_inc(&lpfc_nvmep->defer_wqfull);
		return 0;
	}

	/* Give back resources */
	atomic_inc(&lpfc_nvmep->xmt_fcp_drop);
	lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"6153 FCP Drop IO x%x: Issue: %d\n",
			ctxp->oxid, rc);

	ctxp->wqeq->hba_wqidx = 0;
	nvmewqeq->context2 = NULL;
	nvmewqeq->context3 = NULL;
	rc = -EBUSY;
aerr:
	return rc;
}

static void
lpfc_nvmet_targetport_delete(struct nvmet_fc_target_port *targetport)
{
	struct lpfc_nvmet_tgtport *tport = targetport->private;

	/* release any threads waiting for the unreg to complete */
	if (tport->phba->targetport)
		complete(tport->tport_unreg_cmp);
}

static void
lpfc_nvmet_xmt_fcp_abort(struct nvmet_fc_target_port *tgtport,
			 struct nvmefc_tgt_fcp_req *req)
{
	struct lpfc_nvmet_tgtport *lpfc_nvmep = tgtport->private;
	struct lpfc_async_xchg_ctx *ctxp =
		container_of(req, struct lpfc_async_xchg_ctx, hdlrctx.fcp_req);
	struct lpfc_hba *phba = ctxp->phba;
	struct lpfc_queue *wq;
	unsigned long flags;

	if (phba->pport->load_flag & FC_UNLOADING)
		return;

	if (!ctxp->hdwq)
		ctxp->hdwq = &phba->sli4_hba.hdwq[0];

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6103 NVMET Abort op: oxid x%x flg x%x ste %d\n",
			ctxp->oxid, ctxp->flag, ctxp->state);

	lpfc_nvmeio_data(phba, "NVMET FCP ABRT: xri x%x flg x%x ste x%x\n",
			 ctxp->oxid, ctxp->flag, ctxp->state);

	atomic_inc(&lpfc_nvmep->xmt_fcp_abort);

	spin_lock_irqsave(&ctxp->ctxlock, flags);

	/* Since iaab/iaar are NOT set, we need to check
	 * if the firmware is in process of aborting IO
	 */
	if (ctxp->flag & (LPFC_NVME_XBUSY | LPFC_NVME_ABORT_OP)) {
		spin_unlock_irqrestore(&ctxp->ctxlock, flags);
		return;
	}
	ctxp->flag |= LPFC_NVME_ABORT_OP;

	if (ctxp->flag & LPFC_NVME_DEFER_WQFULL) {
		spin_unlock_irqrestore(&ctxp->ctxlock, flags);
		lpfc_nvmet_unsol_fcp_issue_abort(phba, ctxp, ctxp->sid,
						 ctxp->oxid);
		wq = ctxp->hdwq->io_wq;
		lpfc_nvmet_wqfull_flush(phba, wq, ctxp);
		return;
	}
	spin_unlock_irqrestore(&ctxp->ctxlock, flags);

	/* A state of LPFC_NVME_STE_RCV means we have just received
	 * the NVME command and have not started processing it.
	 * (by issuing any IO WQEs on this exchange yet)
	 */
	if (ctxp->state == LPFC_NVME_STE_RCV)
		lpfc_nvmet_unsol_fcp_issue_abort(phba, ctxp, ctxp->sid,
						 ctxp->oxid);
	else
		lpfc_nvmet_sol_fcp_issue_abort(phba, ctxp, ctxp->sid,
					       ctxp->oxid);
}

static void
lpfc_nvmet_xmt_fcp_release(struct nvmet_fc_target_port *tgtport,
			   struct nvmefc_tgt_fcp_req *rsp)
{
	struct lpfc_nvmet_tgtport *lpfc_nvmep = tgtport->private;
	struct lpfc_async_xchg_ctx *ctxp =
		container_of(rsp, struct lpfc_async_xchg_ctx, hdlrctx.fcp_req);
	struct lpfc_hba *phba = ctxp->phba;
	unsigned long flags;
	bool aborting = false;

	spin_lock_irqsave(&ctxp->ctxlock, flags);
	if (ctxp->flag & LPFC_NVME_XBUSY)
		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_IOERR,
				"6027 NVMET release with XBUSY flag x%x"
				" oxid x%x\n",
				ctxp->flag, ctxp->oxid);
	else if (ctxp->state != LPFC_NVME_STE_DONE &&
		 ctxp->state != LPFC_NVME_STE_ABORT)
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6413 NVMET release bad state %d %d oxid x%x\n",
				ctxp->state, ctxp->entry_cnt, ctxp->oxid);

	if ((ctxp->flag & LPFC_NVME_ABORT_OP) ||
	    (ctxp->flag & LPFC_NVME_XBUSY)) {
		aborting = true;
		/* let the abort path do the real release */
		lpfc_nvmet_defer_release(phba, ctxp);
	}
	spin_unlock_irqrestore(&ctxp->ctxlock, flags);

	lpfc_nvmeio_data(phba, "NVMET FCP FREE: xri x%x ste %d abt %d\n", ctxp->oxid,
			 ctxp->state, aborting);

	atomic_inc(&lpfc_nvmep->xmt_fcp_release);
	ctxp->flag &= ~LPFC_NVME_TNOTIFY;

	if (aborting)
		return;

	lpfc_nvmet_ctxbuf_post(phba, ctxp->ctxbuf);
}

static void
lpfc_nvmet_defer_rcv(struct nvmet_fc_target_port *tgtport,
		     struct nvmefc_tgt_fcp_req *rsp)
{
	struct lpfc_nvmet_tgtport *tgtp;
	struct lpfc_async_xchg_ctx *ctxp =
		container_of(rsp, struct lpfc_async_xchg_ctx, hdlrctx.fcp_req);
	struct rqb_dmabuf *nvmebuf = ctxp->rqb_buffer;
	struct lpfc_hba *phba = ctxp->phba;
	unsigned long iflag;


	lpfc_nvmeio_data(phba, "NVMET DEFERRCV: xri x%x sz %d CPU %02x\n",
			 ctxp->oxid, ctxp->size, raw_smp_processor_id());

	if (!nvmebuf) {
		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_IOERR,
				"6425 Defer rcv: no buffer oxid x%x: "
				"flg %x ste %x\n",
				ctxp->oxid, ctxp->flag, ctxp->state);
		return;
	}

	tgtp = phba->targetport->private;
	if (tgtp)
		atomic_inc(&tgtp->rcv_fcp_cmd_defer);

	/* Free the nvmebuf since a new buffer already replaced it */
	nvmebuf->hrq->rqbp->rqb_free_buffer(phba, nvmebuf);
	spin_lock_irqsave(&ctxp->ctxlock, iflag);
	ctxp->rqb_buffer = NULL;
	spin_unlock_irqrestore(&ctxp->ctxlock, iflag);
}

/**
 * lpfc_nvmet_ls_req_cmp - completion handler for a nvme ls request
 * @phba: Pointer to HBA context object
 * @cmdwqe: Pointer to driver command WQE object.
 * @wcqe: Pointer to driver response CQE object.
 *
 * This function is the completion handler for NVME LS requests.
 * The function updates any states and statistics, then calls the
 * generic completion handler to finish completion of the request.
 **/
static void
lpfc_nvmet_ls_req_cmp(struct lpfc_hba *phba, struct lpfc_iocbq *cmdwqe,
		       struct lpfc_wcqe_complete *wcqe)
{
	__lpfc_nvme_ls_req_cmp(phba, cmdwqe->vport, cmdwqe, wcqe);
}

/**
 * lpfc_nvmet_ls_req - Issue an Link Service request
 * @targetport - pointer to target instance registered with nvmet transport.
 * @hosthandle - hosthandle set by the driver in a prior ls_rqst_rcv.
 *               Driver sets this value to the ndlp pointer.
 * @pnvme_lsreq - the transport nvme_ls_req structure for the LS
 *
 * Driver registers this routine to handle any link service request
 * from the nvme_fc transport to a remote nvme-aware port.
 *
 * Return value :
 *   0 - Success
 *   non-zero: various error codes, in form of -Exxx
 **/
static int
lpfc_nvmet_ls_req(struct nvmet_fc_target_port *targetport,
		  void *hosthandle,
		  struct nvmefc_ls_req *pnvme_lsreq)
{
	struct lpfc_nvmet_tgtport *lpfc_nvmet = targetport->private;
	struct lpfc_hba *phba;
	struct lpfc_nodelist *ndlp;
	int ret;
	u32 hstate;

	if (!lpfc_nvmet)
		return -EINVAL;

	phba = lpfc_nvmet->phba;
	if (phba->pport->load_flag & FC_UNLOADING)
		return -EINVAL;

	hstate = atomic_read(&lpfc_nvmet->state);
	if (hstate == LPFC_NVMET_INV_HOST_ACTIVE)
		return -EACCES;

	ndlp = (struct lpfc_nodelist *)hosthandle;

	ret = __lpfc_nvme_ls_req(phba->pport, ndlp, pnvme_lsreq,
				 lpfc_nvmet_ls_req_cmp);

	return ret;
}

/**
 * lpfc_nvmet_ls_abort - Abort a prior NVME LS request
 * @targetport: Transport targetport, that LS was issued from.
 * @hosthandle - hosthandle set by the driver in a prior ls_rqst_rcv.
 *               Driver sets this value to the ndlp pointer.
 * @pnvme_lsreq - the transport nvme_ls_req structure for LS to be aborted
 *
 * Driver registers this routine to abort an NVME LS request that is
 * in progress (from the transports perspective).
 **/
static void
lpfc_nvmet_ls_abort(struct nvmet_fc_target_port *targetport,
		    void *hosthandle,
		    struct nvmefc_ls_req *pnvme_lsreq)
{
	struct lpfc_nvmet_tgtport *lpfc_nvmet = targetport->private;
	struct lpfc_hba *phba;
	struct lpfc_nodelist *ndlp;
	int ret;

	phba = lpfc_nvmet->phba;
	if (phba->pport->load_flag & FC_UNLOADING)
		return;

	ndlp = (struct lpfc_nodelist *)hosthandle;

	ret = __lpfc_nvme_ls_abort(phba->pport, ndlp, pnvme_lsreq);
	if (!ret)
		atomic_inc(&lpfc_nvmet->xmt_ls_abort);
}

static void
lpfc_nvmet_host_release(void *hosthandle)
{
	struct lpfc_nodelist *ndlp = hosthandle;
	struct lpfc_hba *phba = NULL;
	struct lpfc_nvmet_tgtport *tgtp;

	phba = ndlp->phba;
	if (!phba->targetport || !phba->targetport->private)
		return;

	lpfc_printf_log(phba, KERN_ERR, LOG_NVME,
			"6202 NVMET XPT releasing hosthandle x%px\n",
			hosthandle);
	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	atomic_set(&tgtp->state, 0);
}

static void
lpfc_nvmet_discovery_event(struct nvmet_fc_target_port *tgtport)
{
	struct lpfc_nvmet_tgtport *tgtp;
	struct lpfc_hba *phba;
	uint32_t rc;

	tgtp = tgtport->private;
	phba = tgtp->phba;

	rc = lpfc_issue_els_rscn(phba->pport, 0);
	lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"6420 NVMET subsystem change: Notification %s\n",
			(rc) ? "Failed" : "Sent");
}

static struct nvmet_fc_target_template lpfc_tgttemplate = {
	.targetport_delete = lpfc_nvmet_targetport_delete,
	.xmt_ls_rsp     = lpfc_nvmet_xmt_ls_rsp,
	.fcp_op         = lpfc_nvmet_xmt_fcp_op,
	.fcp_abort      = lpfc_nvmet_xmt_fcp_abort,
	.fcp_req_release = lpfc_nvmet_xmt_fcp_release,
	.defer_rcv	= lpfc_nvmet_defer_rcv,
	.discovery_event = lpfc_nvmet_discovery_event,
	.ls_req         = lpfc_nvmet_ls_req,
	.ls_abort       = lpfc_nvmet_ls_abort,
	.host_release   = lpfc_nvmet_host_release,

	.max_hw_queues  = 1,
	.max_sgl_segments = LPFC_NVMET_DEFAULT_SEGS,
	.max_dif_sgl_segments = LPFC_NVMET_DEFAULT_SEGS,
	.dma_boundary = 0xFFFFFFFF,

	/* optional features */
	.target_features = 0,
	/* sizes of additional private data for data structures */
	.target_priv_sz = sizeof(struct lpfc_nvmet_tgtport),
	.lsrqst_priv_sz = 0,
};

static void
__lpfc_nvmet_clean_io_for_cpu(struct lpfc_hba *phba,
		struct lpfc_nvmet_ctx_info *infop)
{
	struct lpfc_nvmet_ctxbuf *ctx_buf, *next_ctx_buf;
	unsigned long flags;

	spin_lock_irqsave(&infop->nvmet_ctx_list_lock, flags);
	list_for_each_entry_safe(ctx_buf, next_ctx_buf,
				&infop->nvmet_ctx_list, list) {
		spin_lock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		list_del_init(&ctx_buf->list);
		spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);

		__lpfc_clear_active_sglq(phba, ctx_buf->sglq->sli4_lxritag);
		ctx_buf->sglq->state = SGL_FREED;
		ctx_buf->sglq->ndlp = NULL;

		spin_lock(&phba->sli4_hba.sgl_list_lock);
		list_add_tail(&ctx_buf->sglq->list,
				&phba->sli4_hba.lpfc_nvmet_sgl_list);
		spin_unlock(&phba->sli4_hba.sgl_list_lock);

		lpfc_sli_release_iocbq(phba, ctx_buf->iocbq);
		kfree(ctx_buf->context);
	}
	spin_unlock_irqrestore(&infop->nvmet_ctx_list_lock, flags);
}

static void
lpfc_nvmet_cleanup_io_context(struct lpfc_hba *phba)
{
	struct lpfc_nvmet_ctx_info *infop;
	int i, j;

	/* The first context list, MRQ 0 CPU 0 */
	infop = phba->sli4_hba.nvmet_ctx_info;
	if (!infop)
		return;

	/* Cycle the the entire CPU context list for every MRQ */
	for (i = 0; i < phba->cfg_nvmet_mrq; i++) {
		for_each_present_cpu(j) {
			infop = lpfc_get_ctx_list(phba, j, i);
			__lpfc_nvmet_clean_io_for_cpu(phba, infop);
		}
	}
	kfree(phba->sli4_hba.nvmet_ctx_info);
	phba->sli4_hba.nvmet_ctx_info = NULL;
}

static int
lpfc_nvmet_setup_io_context(struct lpfc_hba *phba)
{
	struct lpfc_nvmet_ctxbuf *ctx_buf;
	struct lpfc_iocbq *nvmewqe;
	union lpfc_wqe128 *wqe;
	struct lpfc_nvmet_ctx_info *last_infop;
	struct lpfc_nvmet_ctx_info *infop;
	int i, j, idx, cpu;

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME,
			"6403 Allocate NVMET resources for %d XRIs\n",
			phba->sli4_hba.nvmet_xri_cnt);

	phba->sli4_hba.nvmet_ctx_info = kcalloc(
		phba->sli4_hba.num_possible_cpu * phba->cfg_nvmet_mrq,
		sizeof(struct lpfc_nvmet_ctx_info), GFP_KERNEL);
	if (!phba->sli4_hba.nvmet_ctx_info) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6419 Failed allocate memory for "
				"nvmet context lists\n");
		return -ENOMEM;
	}

	/*
	 * Assuming X CPUs in the system, and Y MRQs, allocate some
	 * lpfc_nvmet_ctx_info structures as follows:
	 *
	 * cpu0/mrq0 cpu1/mrq0 ... cpuX/mrq0
	 * cpu0/mrq1 cpu1/mrq1 ... cpuX/mrq1
	 * ...
	 * cpuX/mrqY cpuX/mrqY ... cpuX/mrqY
	 *
	 * Each line represents a MRQ "silo" containing an entry for
	 * every CPU.
	 *
	 * MRQ X is initially assumed to be associated with CPU X, thus
	 * contexts are initially distributed across all MRQs using
	 * the MRQ index (N) as follows cpuN/mrqN. When contexts are
	 * freed, the are freed to the MRQ silo based on the CPU number
	 * of the IO completion. Thus a context that was allocated for MRQ A
	 * whose IO completed on CPU B will be freed to cpuB/mrqA.
	 */
	for_each_possible_cpu(i) {
		for (j = 0; j < phba->cfg_nvmet_mrq; j++) {
			infop = lpfc_get_ctx_list(phba, i, j);
			INIT_LIST_HEAD(&infop->nvmet_ctx_list);
			spin_lock_init(&infop->nvmet_ctx_list_lock);
			infop->nvmet_ctx_list_cnt = 0;
		}
	}

	/*
	 * Setup the next CPU context info ptr for each MRQ.
	 * MRQ 0 will cycle thru CPUs 0 - X separately from
	 * MRQ 1 cycling thru CPUs 0 - X, and so on.
	 */
	for (j = 0; j < phba->cfg_nvmet_mrq; j++) {
		last_infop = lpfc_get_ctx_list(phba,
					       cpumask_first(cpu_present_mask),
					       j);
		for (i = phba->sli4_hba.num_possible_cpu - 1;  i >= 0; i--) {
			infop = lpfc_get_ctx_list(phba, i, j);
			infop->nvmet_ctx_next_cpu = last_infop;
			last_infop = infop;
		}
	}

	/* For all nvmet xris, allocate resources needed to process a
	 * received command on a per xri basis.
	 */
	idx = 0;
	cpu = cpumask_first(cpu_present_mask);
	for (i = 0; i < phba->sli4_hba.nvmet_xri_cnt; i++) {
		ctx_buf = kzalloc(sizeof(*ctx_buf), GFP_KERNEL);
		if (!ctx_buf) {
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"6404 Ran out of memory for NVMET\n");
			return -ENOMEM;
		}

		ctx_buf->context = kzalloc(sizeof(*ctx_buf->context),
					   GFP_KERNEL);
		if (!ctx_buf->context) {
			kfree(ctx_buf);
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"6405 Ran out of NVMET "
					"context memory\n");
			return -ENOMEM;
		}
		ctx_buf->context->ctxbuf = ctx_buf;
		ctx_buf->context->state = LPFC_NVME_STE_FREE;

		ctx_buf->iocbq = lpfc_sli_get_iocbq(phba);
		if (!ctx_buf->iocbq) {
			kfree(ctx_buf->context);
			kfree(ctx_buf);
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"6406 Ran out of NVMET iocb/WQEs\n");
			return -ENOMEM;
		}
		ctx_buf->iocbq->iocb_flag = LPFC_IO_NVMET;
		nvmewqe = ctx_buf->iocbq;
		wqe = &nvmewqe->wqe;

		/* Initialize WQE */
		memset(wqe, 0, sizeof(union lpfc_wqe));

		ctx_buf->iocbq->context1 = NULL;
		spin_lock(&phba->sli4_hba.sgl_list_lock);
		ctx_buf->sglq = __lpfc_sli_get_nvmet_sglq(phba, ctx_buf->iocbq);
		spin_unlock(&phba->sli4_hba.sgl_list_lock);
		if (!ctx_buf->sglq) {
			lpfc_sli_release_iocbq(phba, ctx_buf->iocbq);
			kfree(ctx_buf->context);
			kfree(ctx_buf);
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"6407 Ran out of NVMET XRIs\n");
			return -ENOMEM;
		}
		INIT_WORK(&ctx_buf->defer_work, lpfc_nvmet_fcp_rqst_defer_work);

		/*
		 * Add ctx to MRQidx context list. Our initial assumption
		 * is MRQidx will be associated with CPUidx. This association
		 * can change on the fly.
		 */
		infop = lpfc_get_ctx_list(phba, cpu, idx);
		spin_lock(&infop->nvmet_ctx_list_lock);
		list_add_tail(&ctx_buf->list, &infop->nvmet_ctx_list);
		infop->nvmet_ctx_list_cnt++;
		spin_unlock(&infop->nvmet_ctx_list_lock);

		/* Spread ctx structures evenly across all MRQs */
		idx++;
		if (idx >= phba->cfg_nvmet_mrq) {
			idx = 0;
			cpu = cpumask_first(cpu_present_mask);
			continue;
		}
		cpu = cpumask_next(cpu, cpu_present_mask);
		if (cpu == nr_cpu_ids)
			cpu = cpumask_first(cpu_present_mask);

	}

	for_each_present_cpu(i) {
		for (j = 0; j < phba->cfg_nvmet_mrq; j++) {
			infop = lpfc_get_ctx_list(phba, i, j);
			lpfc_printf_log(phba, KERN_INFO, LOG_NVME | LOG_INIT,
					"6408 TOTAL NVMET ctx for CPU %d "
					"MRQ %d: cnt %d nextcpu x%px\n",
					i, j, infop->nvmet_ctx_list_cnt,
					infop->nvmet_ctx_next_cpu);
		}
	}
	return 0;
}

int
lpfc_nvmet_create_targetport(struct lpfc_hba *phba)
{
	struct lpfc_vport  *vport = phba->pport;
	struct lpfc_nvmet_tgtport *tgtp;
	struct nvmet_fc_port_info pinfo;
	int error;

	if (phba->targetport)
		return 0;

	error = lpfc_nvmet_setup_io_context(phba);
	if (error)
		return error;

	memset(&pinfo, 0, sizeof(struct nvmet_fc_port_info));
	pinfo.node_name = wwn_to_u64(vport->fc_nodename.u.wwn);
	pinfo.port_name = wwn_to_u64(vport->fc_portname.u.wwn);
	pinfo.port_id = vport->fc_myDID;

	/* We need to tell the transport layer + 1 because it takes page
	 * alignment into account. When space for the SGL is allocated we
	 * allocate + 3, one for cmd, one for rsp and one for this alignment
	 */
	lpfc_tgttemplate.max_sgl_segments = phba->cfg_nvme_seg_cnt + 1;
	lpfc_tgttemplate.max_hw_queues = phba->cfg_hdw_queue;
	lpfc_tgttemplate.target_features = NVMET_FCTGTFEAT_READDATA_RSP;

#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	error = nvmet_fc_register_targetport(&pinfo, &lpfc_tgttemplate,
					     &phba->pcidev->dev,
					     &phba->targetport);
#else
	error = -ENOENT;
#endif
	if (error) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6025 Cannot register NVME targetport x%x: "
				"portnm %llx nodenm %llx segs %d qs %d\n",
				error,
				pinfo.port_name, pinfo.node_name,
				lpfc_tgttemplate.max_sgl_segments,
				lpfc_tgttemplate.max_hw_queues);
		phba->targetport = NULL;
		phba->nvmet_support = 0;

		lpfc_nvmet_cleanup_io_context(phba);

	} else {
		tgtp = (struct lpfc_nvmet_tgtport *)
			phba->targetport->private;
		tgtp->phba = phba;

		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_DISC,
				"6026 Registered NVME "
				"targetport: x%px, private x%px "
				"portnm %llx nodenm %llx segs %d qs %d\n",
				phba->targetport, tgtp,
				pinfo.port_name, pinfo.node_name,
				lpfc_tgttemplate.max_sgl_segments,
				lpfc_tgttemplate.max_hw_queues);

		atomic_set(&tgtp->rcv_ls_req_in, 0);
		atomic_set(&tgtp->rcv_ls_req_out, 0);
		atomic_set(&tgtp->rcv_ls_req_drop, 0);
		atomic_set(&tgtp->xmt_ls_abort, 0);
		atomic_set(&tgtp->xmt_ls_abort_cmpl, 0);
		atomic_set(&tgtp->xmt_ls_rsp, 0);
		atomic_set(&tgtp->xmt_ls_drop, 0);
		atomic_set(&tgtp->xmt_ls_rsp_error, 0);
		atomic_set(&tgtp->xmt_ls_rsp_xb_set, 0);
		atomic_set(&tgtp->xmt_ls_rsp_aborted, 0);
		atomic_set(&tgtp->xmt_ls_rsp_cmpl, 0);
		atomic_set(&tgtp->rcv_fcp_cmd_in, 0);
		atomic_set(&tgtp->rcv_fcp_cmd_out, 0);
		atomic_set(&tgtp->rcv_fcp_cmd_drop, 0);
		atomic_set(&tgtp->xmt_fcp_drop, 0);
		atomic_set(&tgtp->xmt_fcp_read_rsp, 0);
		atomic_set(&tgtp->xmt_fcp_read, 0);
		atomic_set(&tgtp->xmt_fcp_write, 0);
		atomic_set(&tgtp->xmt_fcp_rsp, 0);
		atomic_set(&tgtp->xmt_fcp_release, 0);
		atomic_set(&tgtp->xmt_fcp_rsp_cmpl, 0);
		atomic_set(&tgtp->xmt_fcp_rsp_error, 0);
		atomic_set(&tgtp->xmt_fcp_rsp_xb_set, 0);
		atomic_set(&tgtp->xmt_fcp_rsp_aborted, 0);
		atomic_set(&tgtp->xmt_fcp_rsp_drop, 0);
		atomic_set(&tgtp->xmt_fcp_xri_abort_cqe, 0);
		atomic_set(&tgtp->xmt_fcp_abort, 0);
		atomic_set(&tgtp->xmt_fcp_abort_cmpl, 0);
		atomic_set(&tgtp->xmt_abort_unsol, 0);
		atomic_set(&tgtp->xmt_abort_sol, 0);
		atomic_set(&tgtp->xmt_abort_rsp, 0);
		atomic_set(&tgtp->xmt_abort_rsp_error, 0);
		atomic_set(&tgtp->defer_ctx, 0);
		atomic_set(&tgtp->defer_fod, 0);
		atomic_set(&tgtp->defer_wqfull, 0);
	}
	return error;
}

int
lpfc_nvmet_update_targetport(struct lpfc_hba *phba)
{
	struct lpfc_vport  *vport = phba->pport;

	if (!phba->targetport)
		return 0;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME,
			 "6007 Update NVMET port x%px did x%x\n",
			 phba->targetport, vport->fc_myDID);

	phba->targetport->port_id = vport->fc_myDID;
	return 0;
}

/**
 * lpfc_sli4_nvmet_xri_aborted - Fast-path process of nvmet xri abort
 * @phba: pointer to lpfc hba data structure.
 * @axri: pointer to the nvmet xri abort wcqe structure.
 *
 * This routine is invoked by the worker thread to process a SLI4 fast-path
 * NVMET aborted xri.
 **/
void
lpfc_sli4_nvmet_xri_aborted(struct lpfc_hba *phba,
			    struct sli4_wcqe_xri_aborted *axri)
{
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	uint16_t xri = bf_get(lpfc_wcqe_xa_xri, axri);
	uint16_t rxid = bf_get(lpfc_wcqe_xa_remote_xid, axri);
	struct lpfc_async_xchg_ctx *ctxp, *next_ctxp;
	struct lpfc_nvmet_tgtport *tgtp;
	struct nvmefc_tgt_fcp_req *req = NULL;
	struct lpfc_nodelist *ndlp;
	unsigned long iflag = 0;
	int rrq_empty = 0;
	bool released = false;

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6317 XB aborted xri x%x rxid x%x\n", xri, rxid);

	if (!(phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME))
		return;

	if (phba->targetport) {
		tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
		atomic_inc(&tgtp->xmt_fcp_xri_abort_cqe);
	}

	spin_lock_irqsave(&phba->hbalock, iflag);
	spin_lock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
	list_for_each_entry_safe(ctxp, next_ctxp,
				 &phba->sli4_hba.lpfc_abts_nvmet_ctx_list,
				 list) {
		if (ctxp->ctxbuf->sglq->sli4_xritag != xri)
			continue;

		spin_lock(&ctxp->ctxlock);
		/* Check if we already received a free context call
		 * and we have completed processing an abort situation.
		 */
		if (ctxp->flag & LPFC_NVME_CTX_RLS &&
		    !(ctxp->flag & LPFC_NVME_ABORT_OP)) {
			list_del_init(&ctxp->list);
			released = true;
		}
		ctxp->flag &= ~LPFC_NVME_XBUSY;
		spin_unlock(&ctxp->ctxlock);
		spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);

		rrq_empty = list_empty(&phba->active_rrq_list);
		spin_unlock_irqrestore(&phba->hbalock, iflag);
		ndlp = lpfc_findnode_did(phba->pport, ctxp->sid);
		if (ndlp && NLP_CHK_NODE_ACT(ndlp) &&
		    (ndlp->nlp_state == NLP_STE_UNMAPPED_NODE ||
		     ndlp->nlp_state == NLP_STE_MAPPED_NODE)) {
			lpfc_set_rrq_active(phba, ndlp,
				ctxp->ctxbuf->sglq->sli4_lxritag,
				rxid, 1);
			lpfc_sli4_abts_err_handler(phba, ndlp, axri);
		}

		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
				"6318 XB aborted oxid x%x flg x%x (%x)\n",
				ctxp->oxid, ctxp->flag, released);
		if (released)
			lpfc_nvmet_ctxbuf_post(phba, ctxp->ctxbuf);

		if (rrq_empty)
			lpfc_worker_wake_up(phba);
		return;
	}
	spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
	spin_unlock_irqrestore(&phba->hbalock, iflag);

	ctxp = lpfc_nvmet_get_ctx_for_xri(phba, xri);
	if (ctxp) {
		/*
		 *  Abort already done by FW, so BA_ACC sent.
		 *  However, the transport may be unaware.
		 */
		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
				"6323 NVMET Rcv ABTS xri x%x ctxp state x%x "
				"flag x%x oxid x%x rxid x%x\n",
				xri, ctxp->state, ctxp->flag, ctxp->oxid,
				rxid);

		spin_lock_irqsave(&ctxp->ctxlock, iflag);
		ctxp->flag |= LPFC_NVME_ABTS_RCV;
		ctxp->state = LPFC_NVME_STE_ABORT;
		spin_unlock_irqrestore(&ctxp->ctxlock, iflag);

		lpfc_nvmeio_data(phba,
				 "NVMET ABTS RCV: xri x%x CPU %02x rjt %d\n",
				 xri, raw_smp_processor_id(), 0);

		req = &ctxp->hdlrctx.fcp_req;
		if (req)
			nvmet_fc_rcv_fcp_abort(phba->targetport, req);
	}
#endif
}

int
lpfc_nvmet_rcv_unsol_abort(struct lpfc_vport *vport,
			   struct fc_frame_header *fc_hdr)
{
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_async_xchg_ctx *ctxp, *next_ctxp;
	struct nvmefc_tgt_fcp_req *rsp;
	uint32_t sid;
	uint16_t oxid, xri;
	unsigned long iflag = 0;

	sid = sli4_sid_from_fc_hdr(fc_hdr);
	oxid = be16_to_cpu(fc_hdr->fh_ox_id);

	spin_lock_irqsave(&phba->hbalock, iflag);
	spin_lock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
	list_for_each_entry_safe(ctxp, next_ctxp,
				 &phba->sli4_hba.lpfc_abts_nvmet_ctx_list,
				 list) {
		if (ctxp->oxid != oxid || ctxp->sid != sid)
			continue;

		xri = ctxp->ctxbuf->sglq->sli4_xritag;

		spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		spin_unlock_irqrestore(&phba->hbalock, iflag);

		spin_lock_irqsave(&ctxp->ctxlock, iflag);
		ctxp->flag |= LPFC_NVME_ABTS_RCV;
		spin_unlock_irqrestore(&ctxp->ctxlock, iflag);

		lpfc_nvmeio_data(phba,
			"NVMET ABTS RCV: xri x%x CPU %02x rjt %d\n",
			xri, raw_smp_processor_id(), 0);

		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
				"6319 NVMET Rcv ABTS:acc xri x%x\n", xri);

		rsp = &ctxp->hdlrctx.fcp_req;
		nvmet_fc_rcv_fcp_abort(phba->targetport, rsp);

		/* Respond with BA_ACC accordingly */
		lpfc_sli4_seq_abort_rsp(vport, fc_hdr, 1);
		return 0;
	}
	spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
	spin_unlock_irqrestore(&phba->hbalock, iflag);

	/* check the wait list */
	if (phba->sli4_hba.nvmet_io_wait_cnt) {
		struct rqb_dmabuf *nvmebuf;
		struct fc_frame_header *fc_hdr_tmp;
		u32 sid_tmp;
		u16 oxid_tmp;
		bool found = false;

		spin_lock_irqsave(&phba->sli4_hba.nvmet_io_wait_lock, iflag);

		/* match by oxid and s_id */
		list_for_each_entry(nvmebuf,
				    &phba->sli4_hba.lpfc_nvmet_io_wait_list,
				    hbuf.list) {
			fc_hdr_tmp = (struct fc_frame_header *)
					(nvmebuf->hbuf.virt);
			oxid_tmp = be16_to_cpu(fc_hdr_tmp->fh_ox_id);
			sid_tmp = sli4_sid_from_fc_hdr(fc_hdr_tmp);
			if (oxid_tmp != oxid || sid_tmp != sid)
				continue;

			lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
					"6321 NVMET Rcv ABTS oxid x%x from x%x "
					"is waiting for a ctxp\n",
					oxid, sid);

			list_del_init(&nvmebuf->hbuf.list);
			phba->sli4_hba.nvmet_io_wait_cnt--;
			found = true;
			break;
		}
		spin_unlock_irqrestore(&phba->sli4_hba.nvmet_io_wait_lock,
				       iflag);

		/* free buffer since already posted a new DMA buffer to RQ */
		if (found) {
			nvmebuf->hrq->rqbp->rqb_free_buffer(phba, nvmebuf);
			/* Respond with BA_ACC accordingly */
			lpfc_sli4_seq_abort_rsp(vport, fc_hdr, 1);
			return 0;
		}
	}

	/* check active list */
	ctxp = lpfc_nvmet_get_ctx_for_oxid(phba, oxid, sid);
	if (ctxp) {
		xri = ctxp->ctxbuf->sglq->sli4_xritag;

		spin_lock_irqsave(&ctxp->ctxlock, iflag);
		ctxp->flag |= (LPFC_NVME_ABTS_RCV | LPFC_NVME_ABORT_OP);
		spin_unlock_irqrestore(&ctxp->ctxlock, iflag);

		lpfc_nvmeio_data(phba,
				 "NVMET ABTS RCV: xri x%x CPU %02x rjt %d\n",
				 xri, raw_smp_processor_id(), 0);

		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
				"6322 NVMET Rcv ABTS:acc oxid x%x xri x%x "
				"flag x%x state x%x\n",
				ctxp->oxid, xri, ctxp->flag, ctxp->state);

		if (ctxp->flag & LPFC_NVME_TNOTIFY) {
			/* Notify the transport */
			nvmet_fc_rcv_fcp_abort(phba->targetport,
					       &ctxp->hdlrctx.fcp_req);
		} else {
			cancel_work_sync(&ctxp->ctxbuf->defer_work);
			spin_lock_irqsave(&ctxp->ctxlock, iflag);
			lpfc_nvmet_defer_release(phba, ctxp);
			spin_unlock_irqrestore(&ctxp->ctxlock, iflag);
		}
		lpfc_nvmet_sol_fcp_issue_abort(phba, ctxp, ctxp->sid,
					       ctxp->oxid);

		lpfc_sli4_seq_abort_rsp(vport, fc_hdr, 1);
		return 0;
	}

	lpfc_nvmeio_data(phba, "NVMET ABTS RCV: oxid x%x CPU %02x rjt %d\n",
			 oxid, raw_smp_processor_id(), 1);

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6320 NVMET Rcv ABTS:rjt oxid x%x\n", oxid);

	/* Respond with BA_RJT accordingly */
	lpfc_sli4_seq_abort_rsp(vport, fc_hdr, 0);
#endif
	return 0;
}

static void
lpfc_nvmet_wqfull_flush(struct lpfc_hba *phba, struct lpfc_queue *wq,
			struct lpfc_async_xchg_ctx *ctxp)
{
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *nvmewqeq;
	struct lpfc_iocbq *next_nvmewqeq;
	unsigned long iflags;
	struct lpfc_wcqe_complete wcqe;
	struct lpfc_wcqe_complete *wcqep;

	pring = wq->pring;
	wcqep = &wcqe;

	/* Fake an ABORT error code back to cmpl routine */
	memset(wcqep, 0, sizeof(struct lpfc_wcqe_complete));
	bf_set(lpfc_wcqe_c_status, wcqep, IOSTAT_LOCAL_REJECT);
	wcqep->parameter = IOERR_ABORT_REQUESTED;

	spin_lock_irqsave(&pring->ring_lock, iflags);
	list_for_each_entry_safe(nvmewqeq, next_nvmewqeq,
				 &wq->wqfull_list, list) {
		if (ctxp) {
			/* Checking for a specific IO to flush */
			if (nvmewqeq->context2 == ctxp) {
				list_del(&nvmewqeq->list);
				spin_unlock_irqrestore(&pring->ring_lock,
						       iflags);
				lpfc_nvmet_xmt_fcp_op_cmp(phba, nvmewqeq,
							  wcqep);
				return;
			}
			continue;
		} else {
			/* Flush all IOs */
			list_del(&nvmewqeq->list);
			spin_unlock_irqrestore(&pring->ring_lock, iflags);
			lpfc_nvmet_xmt_fcp_op_cmp(phba, nvmewqeq, wcqep);
			spin_lock_irqsave(&pring->ring_lock, iflags);
		}
	}
	if (!ctxp)
		wq->q_flag &= ~HBA_NVMET_WQFULL;
	spin_unlock_irqrestore(&pring->ring_lock, iflags);
}

void
lpfc_nvmet_wqfull_process(struct lpfc_hba *phba,
			  struct lpfc_queue *wq)
{
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *nvmewqeq;
	struct lpfc_async_xchg_ctx *ctxp;
	unsigned long iflags;
	int rc;

	/*
	 * Some WQE slots are available, so try to re-issue anything
	 * on the WQ wqfull_list.
	 */
	pring = wq->pring;
	spin_lock_irqsave(&pring->ring_lock, iflags);
	while (!list_empty(&wq->wqfull_list)) {
		list_remove_head(&wq->wqfull_list, nvmewqeq, struct lpfc_iocbq,
				 list);
		spin_unlock_irqrestore(&pring->ring_lock, iflags);
		ctxp = (struct lpfc_async_xchg_ctx *)nvmewqeq->context2;
		rc = lpfc_sli4_issue_wqe(phba, ctxp->hdwq, nvmewqeq);
		spin_lock_irqsave(&pring->ring_lock, iflags);
		if (rc == -EBUSY) {
			/* WQ was full again, so put it back on the list */
			list_add(&nvmewqeq->list, &wq->wqfull_list);
			spin_unlock_irqrestore(&pring->ring_lock, iflags);
			return;
		}
		if (rc == WQE_SUCCESS) {
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
			if (ctxp->ts_cmd_nvme) {
				if (ctxp->hdlrctx.fcp_req.op == NVMET_FCOP_RSP)
					ctxp->ts_status_wqput = ktime_get_ns();
				else
					ctxp->ts_data_wqput = ktime_get_ns();
			}
#endif
		} else {
			WARN_ON(rc);
		}
	}
	wq->q_flag &= ~HBA_NVMET_WQFULL;
	spin_unlock_irqrestore(&pring->ring_lock, iflags);

#endif
}

void
lpfc_nvmet_destroy_targetport(struct lpfc_hba *phba)
{
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	struct lpfc_nvmet_tgtport *tgtp;
	struct lpfc_queue *wq;
	uint32_t qidx;
	DECLARE_COMPLETION_ONSTACK(tport_unreg_cmp);

	if (phba->nvmet_support == 0)
		return;
	if (phba->targetport) {
		tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
		for (qidx = 0; qidx < phba->cfg_hdw_queue; qidx++) {
			wq = phba->sli4_hba.hdwq[qidx].io_wq;
			lpfc_nvmet_wqfull_flush(phba, wq, NULL);
		}
		tgtp->tport_unreg_cmp = &tport_unreg_cmp;
		nvmet_fc_unregister_targetport(phba->targetport);
		if (!wait_for_completion_timeout(&tport_unreg_cmp,
					msecs_to_jiffies(LPFC_NVMET_WAIT_TMO)))
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"6179 Unreg targetport x%px timeout "
					"reached.\n", phba->targetport);
		lpfc_nvmet_cleanup_io_context(phba);
	}
	phba->targetport = NULL;
#endif
}

/**
 * lpfc_nvmet_handle_lsreq - Process an NVME LS request
 * @phba: pointer to lpfc hba data structure.
 * @axchg: pointer to exchange context for the NVME LS request
 *
 * This routine is used for processing an asychronously received NVME LS
 * request. Any remaining validation is done and the LS is then forwarded
 * to the nvmet-fc transport via nvmet_fc_rcv_ls_req().
 *
 * The calling sequence should be: nvmet_fc_rcv_ls_req() -> (processing)
 * -> lpfc_nvmet_xmt_ls_rsp/cmp -> req->done.
 * lpfc_nvme_xmt_ls_rsp_cmp should free the allocated axchg.
 *
 * Returns 0 if LS was handled and delivered to the transport
 * Returns 1 if LS failed to be handled and should be dropped
 */
int
lpfc_nvmet_handle_lsreq(struct lpfc_hba *phba,
			struct lpfc_async_xchg_ctx *axchg)
{
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	struct lpfc_nvmet_tgtport *tgtp = phba->targetport->private;
	uint32_t *payload = axchg->payload;
	int rc;

	atomic_inc(&tgtp->rcv_ls_req_in);

	/*
	 * Driver passes the ndlp as the hosthandle argument allowing
	 * the transport to generate LS requests for any associateions
	 * that are created.
	 */
	rc = nvmet_fc_rcv_ls_req(phba->targetport, axchg->ndlp, &axchg->ls_rsp,
				 axchg->payload, axchg->size);

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_DISC,
			"6037 NVMET Unsol rcv: sz %d rc %d: %08x %08x %08x "
			"%08x %08x %08x\n", axchg->size, rc,
			*payload, *(payload+1), *(payload+2),
			*(payload+3), *(payload+4), *(payload+5));

	if (!rc) {
		atomic_inc(&tgtp->rcv_ls_req_out);
		return 0;
	}

	atomic_inc(&tgtp->rcv_ls_req_drop);
#endif
	return 1;
}

static void
lpfc_nvmet_process_rcv_fcp_req(struct lpfc_nvmet_ctxbuf *ctx_buf)
{
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	struct lpfc_async_xchg_ctx *ctxp = ctx_buf->context;
	struct lpfc_hba *phba = ctxp->phba;
	struct rqb_dmabuf *nvmebuf = ctxp->rqb_buffer;
	struct lpfc_nvmet_tgtport *tgtp;
	uint32_t *payload, qno;
	uint32_t rc;
	unsigned long iflags;

	if (!nvmebuf) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"6159 process_rcv_fcp_req, nvmebuf is NULL, "
			"oxid: x%x flg: x%x state: x%x\n",
			ctxp->oxid, ctxp->flag, ctxp->state);
		spin_lock_irqsave(&ctxp->ctxlock, iflags);
		lpfc_nvmet_defer_release(phba, ctxp);
		spin_unlock_irqrestore(&ctxp->ctxlock, iflags);
		lpfc_nvmet_unsol_fcp_issue_abort(phba, ctxp, ctxp->sid,
						 ctxp->oxid);
		return;
	}

	if (ctxp->flag & LPFC_NVME_ABTS_RCV) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6324 IO oxid x%x aborted\n",
				ctxp->oxid);
		return;
	}

	payload = (uint32_t *)(nvmebuf->dbuf.virt);
	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	ctxp->flag |= LPFC_NVME_TNOTIFY;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (ctxp->ts_isr_cmd)
		ctxp->ts_cmd_nvme = ktime_get_ns();
#endif
	/*
	 * The calling sequence should be:
	 * nvmet_fc_rcv_fcp_req->lpfc_nvmet_xmt_fcp_op/cmp- req->done
	 * lpfc_nvmet_xmt_fcp_op_cmp should free the allocated ctxp.
	 * When we return from nvmet_fc_rcv_fcp_req, all relevant info
	 * the NVME command / FC header is stored.
	 * A buffer has already been reposted for this IO, so just free
	 * the nvmebuf.
	 */
	rc = nvmet_fc_rcv_fcp_req(phba->targetport, &ctxp->hdlrctx.fcp_req,
				  payload, ctxp->size);
	/* Process FCP command */
	if (rc == 0) {
		atomic_inc(&tgtp->rcv_fcp_cmd_out);
		spin_lock_irqsave(&ctxp->ctxlock, iflags);
		if ((ctxp->flag & LPFC_NVME_CTX_REUSE_WQ) ||
		    (nvmebuf != ctxp->rqb_buffer)) {
			spin_unlock_irqrestore(&ctxp->ctxlock, iflags);
			return;
		}
		ctxp->rqb_buffer = NULL;
		spin_unlock_irqrestore(&ctxp->ctxlock, iflags);
		lpfc_rq_buf_free(phba, &nvmebuf->hbuf); /* repost */
		return;
	}

	/* Processing of FCP command is deferred */
	if (rc == -EOVERFLOW) {
		lpfc_nvmeio_data(phba, "NVMET RCV BUSY: xri x%x sz %d "
				 "from %06x\n",
				 ctxp->oxid, ctxp->size, ctxp->sid);
		atomic_inc(&tgtp->rcv_fcp_cmd_out);
		atomic_inc(&tgtp->defer_fod);
		spin_lock_irqsave(&ctxp->ctxlock, iflags);
		if (ctxp->flag & LPFC_NVME_CTX_REUSE_WQ) {
			spin_unlock_irqrestore(&ctxp->ctxlock, iflags);
			return;
		}
		spin_unlock_irqrestore(&ctxp->ctxlock, iflags);
		/*
		 * Post a replacement DMA buffer to RQ and defer
		 * freeing rcv buffer till .defer_rcv callback
		 */
		qno = nvmebuf->idx;
		lpfc_post_rq_buffer(
			phba, phba->sli4_hba.nvmet_mrq_hdr[qno],
			phba->sli4_hba.nvmet_mrq_data[qno], 1, qno);
		return;
	}
	ctxp->flag &= ~LPFC_NVME_TNOTIFY;
	atomic_inc(&tgtp->rcv_fcp_cmd_drop);
	lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"2582 FCP Drop IO x%x: err x%x: x%x x%x x%x\n",
			ctxp->oxid, rc,
			atomic_read(&tgtp->rcv_fcp_cmd_in),
			atomic_read(&tgtp->rcv_fcp_cmd_out),
			atomic_read(&tgtp->xmt_fcp_release));
	lpfc_nvmeio_data(phba, "NVMET FCP DROP: xri x%x sz %d from %06x\n",
			 ctxp->oxid, ctxp->size, ctxp->sid);
	spin_lock_irqsave(&ctxp->ctxlock, iflags);
	lpfc_nvmet_defer_release(phba, ctxp);
	spin_unlock_irqrestore(&ctxp->ctxlock, iflags);
	lpfc_nvmet_unsol_fcp_issue_abort(phba, ctxp, ctxp->sid, ctxp->oxid);
#endif
}

static void
lpfc_nvmet_fcp_rqst_defer_work(struct work_struct *work)
{
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	struct lpfc_nvmet_ctxbuf *ctx_buf =
		container_of(work, struct lpfc_nvmet_ctxbuf, defer_work);

	lpfc_nvmet_process_rcv_fcp_req(ctx_buf);
#endif
}

static struct lpfc_nvmet_ctxbuf *
lpfc_nvmet_replenish_context(struct lpfc_hba *phba,
			     struct lpfc_nvmet_ctx_info *current_infop)
{
#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	struct lpfc_nvmet_ctxbuf *ctx_buf = NULL;
	struct lpfc_nvmet_ctx_info *get_infop;
	int i;

	/*
	 * The current_infop for the MRQ a NVME command IU was received
	 * on is empty. Our goal is to replenish this MRQs context
	 * list from a another CPUs.
	 *
	 * First we need to pick a context list to start looking on.
	 * nvmet_ctx_start_cpu has available context the last time
	 * we needed to replenish this CPU where nvmet_ctx_next_cpu
	 * is just the next sequential CPU for this MRQ.
	 */
	if (current_infop->nvmet_ctx_start_cpu)
		get_infop = current_infop->nvmet_ctx_start_cpu;
	else
		get_infop = current_infop->nvmet_ctx_next_cpu;

	for (i = 0; i < phba->sli4_hba.num_possible_cpu; i++) {
		if (get_infop == current_infop) {
			get_infop = get_infop->nvmet_ctx_next_cpu;
			continue;
		}
		spin_lock(&get_infop->nvmet_ctx_list_lock);

		/* Just take the entire context list, if there are any */
		if (get_infop->nvmet_ctx_list_cnt) {
			list_splice_init(&get_infop->nvmet_ctx_list,
				    &current_infop->nvmet_ctx_list);
			current_infop->nvmet_ctx_list_cnt =
				get_infop->nvmet_ctx_list_cnt - 1;
			get_infop->nvmet_ctx_list_cnt = 0;
			spin_unlock(&get_infop->nvmet_ctx_list_lock);

			current_infop->nvmet_ctx_start_cpu = get_infop;
			list_remove_head(&current_infop->nvmet_ctx_list,
					 ctx_buf, struct lpfc_nvmet_ctxbuf,
					 list);
			return ctx_buf;
		}

		/* Otherwise, move on to the next CPU for this MRQ */
		spin_unlock(&get_infop->nvmet_ctx_list_lock);
		get_infop = get_infop->nvmet_ctx_next_cpu;
	}

#endif
	/* Nothing found, all contexts for the MRQ are in-flight */
	return NULL;
}

/**
 * lpfc_nvmet_unsol_fcp_buffer - Process an unsolicited event data buffer
 * @phba: pointer to lpfc hba data structure.
 * @idx: relative index of MRQ vector
 * @nvmebuf: pointer to lpfc nvme command HBQ data structure.
 * @isr_timestamp: in jiffies.
 * @cqflag: cq processing information regarding workload.
 *
 * This routine is used for processing the WQE associated with a unsolicited
 * event. It first determines whether there is an existing ndlp that matches
 * the DID from the unsolicited WQE. If not, it will create a new one with
 * the DID from the unsolicited WQE. The ELS command from the unsolicited
 * WQE is then used to invoke the proper routine and to set up proper state
 * of the discovery state machine.
 **/
static void
lpfc_nvmet_unsol_fcp_buffer(struct lpfc_hba *phba,
			    uint32_t idx,
			    struct rqb_dmabuf *nvmebuf,
			    uint64_t isr_timestamp,
			    uint8_t cqflag)
{
	struct lpfc_async_xchg_ctx *ctxp;
	struct lpfc_nvmet_tgtport *tgtp;
	struct fc_frame_header *fc_hdr;
	struct lpfc_nvmet_ctxbuf *ctx_buf;
	struct lpfc_nvmet_ctx_info *current_infop;
	uint32_t size, oxid, sid, qno;
	unsigned long iflag;
	int current_cpu;

	if (!IS_ENABLED(CONFIG_NVME_TARGET_FC))
		return;

	ctx_buf = NULL;
	if (!nvmebuf || !phba->targetport) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6157 NVMET FCP Drop IO\n");
		if (nvmebuf)
			lpfc_rq_buf_free(phba, &nvmebuf->hbuf);
		return;
	}

	/*
	 * Get a pointer to the context list for this MRQ based on
	 * the CPU this MRQ IRQ is associated with. If the CPU association
	 * changes from our initial assumption, the context list could
	 * be empty, thus it would need to be replenished with the
	 * context list from another CPU for this MRQ.
	 */
	current_cpu = raw_smp_processor_id();
	current_infop = lpfc_get_ctx_list(phba, current_cpu, idx);
	spin_lock_irqsave(&current_infop->nvmet_ctx_list_lock, iflag);
	if (current_infop->nvmet_ctx_list_cnt) {
		list_remove_head(&current_infop->nvmet_ctx_list,
				 ctx_buf, struct lpfc_nvmet_ctxbuf, list);
		current_infop->nvmet_ctx_list_cnt--;
	} else {
		ctx_buf = lpfc_nvmet_replenish_context(phba, current_infop);
	}
	spin_unlock_irqrestore(&current_infop->nvmet_ctx_list_lock, iflag);

	fc_hdr = (struct fc_frame_header *)(nvmebuf->hbuf.virt);
	oxid = be16_to_cpu(fc_hdr->fh_ox_id);
	size = nvmebuf->bytes_recv;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (phba->hdwqstat_on & LPFC_CHECK_NVMET_IO) {
		this_cpu_inc(phba->sli4_hba.c_stat->rcv_io);
		if (idx != current_cpu)
			lpfc_printf_log(phba, KERN_INFO, LOG_NVME_IOERR,
					"6703 CPU Check rcv: "
					"cpu %d expect %d\n",
					current_cpu, idx);
	}
#endif

	lpfc_nvmeio_data(phba, "NVMET FCP  RCV: xri x%x sz %d CPU %02x\n",
			 oxid, size, raw_smp_processor_id());

	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;

	if (!ctx_buf) {
		/* Queue this NVME IO to process later */
		spin_lock_irqsave(&phba->sli4_hba.nvmet_io_wait_lock, iflag);
		list_add_tail(&nvmebuf->hbuf.list,
			      &phba->sli4_hba.lpfc_nvmet_io_wait_list);
		phba->sli4_hba.nvmet_io_wait_cnt++;
		phba->sli4_hba.nvmet_io_wait_total++;
		spin_unlock_irqrestore(&phba->sli4_hba.nvmet_io_wait_lock,
				       iflag);

		/* Post a brand new DMA buffer to RQ */
		qno = nvmebuf->idx;
		lpfc_post_rq_buffer(
			phba, phba->sli4_hba.nvmet_mrq_hdr[qno],
			phba->sli4_hba.nvmet_mrq_data[qno], 1, qno);

		atomic_inc(&tgtp->defer_ctx);
		return;
	}

	sid = sli4_sid_from_fc_hdr(fc_hdr);

	ctxp = (struct lpfc_async_xchg_ctx *)ctx_buf->context;
	spin_lock_irqsave(&phba->sli4_hba.t_active_list_lock, iflag);
	list_add_tail(&ctxp->list, &phba->sli4_hba.t_active_ctx_list);
	spin_unlock_irqrestore(&phba->sli4_hba.t_active_list_lock, iflag);
	if (ctxp->state != LPFC_NVME_STE_FREE) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6414 NVMET Context corrupt %d %d oxid x%x\n",
				ctxp->state, ctxp->entry_cnt, ctxp->oxid);
	}
	ctxp->wqeq = NULL;
	ctxp->offset = 0;
	ctxp->phba = phba;
	ctxp->size = size;
	ctxp->oxid = oxid;
	ctxp->sid = sid;
	ctxp->idx = idx;
	ctxp->state = LPFC_NVME_STE_RCV;
	ctxp->entry_cnt = 1;
	ctxp->flag = 0;
	ctxp->ctxbuf = ctx_buf;
	ctxp->rqb_buffer = (void *)nvmebuf;
	ctxp->hdwq = NULL;
	spin_lock_init(&ctxp->ctxlock);

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (isr_timestamp)
		ctxp->ts_isr_cmd = isr_timestamp;
	ctxp->ts_cmd_nvme = 0;
	ctxp->ts_nvme_data = 0;
	ctxp->ts_data_wqput = 0;
	ctxp->ts_isr_data = 0;
	ctxp->ts_data_nvme = 0;
	ctxp->ts_nvme_status = 0;
	ctxp->ts_status_wqput = 0;
	ctxp->ts_isr_status = 0;
	ctxp->ts_status_nvme = 0;
#endif

	atomic_inc(&tgtp->rcv_fcp_cmd_in);
	/* check for cq processing load */
	if (!cqflag) {
		lpfc_nvmet_process_rcv_fcp_req(ctx_buf);
		return;
	}

	if (!queue_work(phba->wq, &ctx_buf->defer_work)) {
		atomic_inc(&tgtp->rcv_fcp_cmd_drop);
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6325 Unable to queue work for oxid x%x. "
				"FCP Drop IO [x%x x%x x%x]\n",
				ctxp->oxid,
				atomic_read(&tgtp->rcv_fcp_cmd_in),
				atomic_read(&tgtp->rcv_fcp_cmd_out),
				atomic_read(&tgtp->xmt_fcp_release));

		spin_lock_irqsave(&ctxp->ctxlock, iflag);
		lpfc_nvmet_defer_release(phba, ctxp);
		spin_unlock_irqrestore(&ctxp->ctxlock, iflag);
		lpfc_nvmet_unsol_fcp_issue_abort(phba, ctxp, sid, oxid);
	}
}

/**
 * lpfc_nvmet_unsol_fcp_event - Process an unsolicited event from an nvme nport
 * @phba: pointer to lpfc hba data structure.
 * @idx: relative index of MRQ vector
 * @nvmebuf: pointer to received nvme data structure.
 * @isr_timestamp: in jiffies.
 * @cqflag: cq processing information regarding workload.
 *
 * This routine is used to process an unsolicited event received from a SLI
 * (Service Level Interface) ring. The actual processing of the data buffer
 * associated with the unsolicited event is done by invoking the routine
 * lpfc_nvmet_unsol_fcp_buffer() after properly set up the buffer from the
 * SLI RQ on which the unsolicited event was received.
 **/
void
lpfc_nvmet_unsol_fcp_event(struct lpfc_hba *phba,
			   uint32_t idx,
			   struct rqb_dmabuf *nvmebuf,
			   uint64_t isr_timestamp,
			   uint8_t cqflag)
{
	if (!nvmebuf) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"3167 NVMET FCP Drop IO\n");
		return;
	}
	if (phba->nvmet_support == 0) {
		lpfc_rq_buf_free(phba, &nvmebuf->hbuf);
		return;
	}
	lpfc_nvmet_unsol_fcp_buffer(phba, idx, nvmebuf, isr_timestamp, cqflag);
}

/**
 * lpfc_nvmet_prep_ls_wqe - Allocate and prepare a lpfc wqe data structure
 * @phba: pointer to a host N_Port data structure.
 * @ctxp: Context info for NVME LS Request
 * @rspbuf: DMA buffer of NVME command.
 * @rspsize: size of the NVME command.
 *
 * This routine is used for allocating a lpfc-WQE data structure from
 * the driver lpfc-WQE free-list and prepare the WQE with the parameters
 * passed into the routine for discovery state machine to issue an Extended
 * Link Service (NVME) commands. It is a generic lpfc-WQE allocation
 * and preparation routine that is used by all the discovery state machine
 * routines and the NVME command-specific fields will be later set up by
 * the individual discovery machine routines after calling this routine
 * allocating and preparing a generic WQE data structure. It fills in the
 * Buffer Descriptor Entries (BDEs), allocates buffers for both command
 * payload and response payload (if expected). The reference count on the
 * ndlp is incremented by 1 and the reference to the ndlp is put into
 * context1 of the WQE data structure for this WQE to hold the ndlp
 * reference for the command's callback function to access later.
 *
 * Return code
 *   Pointer to the newly allocated/prepared nvme wqe data structure
 *   NULL - when nvme wqe data structure allocation/preparation failed
 **/
static struct lpfc_iocbq *
lpfc_nvmet_prep_ls_wqe(struct lpfc_hba *phba,
		       struct lpfc_async_xchg_ctx *ctxp,
		       dma_addr_t rspbuf, uint16_t rspsize)
{
	struct lpfc_nodelist *ndlp;
	struct lpfc_iocbq *nvmewqe;
	union lpfc_wqe128 *wqe;

	if (!lpfc_is_link_up(phba)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6104 NVMET prep LS wqe: link err: "
				"NPORT x%x oxid:x%x ste %d\n",
				ctxp->sid, ctxp->oxid, ctxp->state);
		return NULL;
	}

	/* Allocate buffer for  command wqe */
	nvmewqe = lpfc_sli_get_iocbq(phba);
	if (nvmewqe == NULL) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6105 NVMET prep LS wqe: No WQE: "
				"NPORT x%x oxid x%x ste %d\n",
				ctxp->sid, ctxp->oxid, ctxp->state);
		return NULL;
	}

	ndlp = lpfc_findnode_did(phba->pport, ctxp->sid);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp) ||
	    ((ndlp->nlp_state != NLP_STE_UNMAPPED_NODE) &&
	    (ndlp->nlp_state != NLP_STE_MAPPED_NODE))) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6106 NVMET prep LS wqe: No ndlp: "
				"NPORT x%x oxid x%x ste %d\n",
				ctxp->sid, ctxp->oxid, ctxp->state);
		goto nvme_wqe_free_wqeq_exit;
	}
	ctxp->wqeq = nvmewqe;

	/* prevent preparing wqe with NULL ndlp reference */
	nvmewqe->context1 = lpfc_nlp_get(ndlp);
	if (nvmewqe->context1 == NULL)
		goto nvme_wqe_free_wqeq_exit;
	nvmewqe->context2 = ctxp;

	wqe = &nvmewqe->wqe;
	memset(wqe, 0, sizeof(union lpfc_wqe));

	/* Words 0 - 2 */
	wqe->xmit_sequence.bde.tus.f.bdeFlags = BUFF_TYPE_BDE_64;
	wqe->xmit_sequence.bde.tus.f.bdeSize = rspsize;
	wqe->xmit_sequence.bde.addrLow = le32_to_cpu(putPaddrLow(rspbuf));
	wqe->xmit_sequence.bde.addrHigh = le32_to_cpu(putPaddrHigh(rspbuf));

	/* Word 3 */

	/* Word 4 */

	/* Word 5 */
	bf_set(wqe_dfctl, &wqe->xmit_sequence.wge_ctl, 0);
	bf_set(wqe_ls, &wqe->xmit_sequence.wge_ctl, 1);
	bf_set(wqe_la, &wqe->xmit_sequence.wge_ctl, 0);
	bf_set(wqe_rctl, &wqe->xmit_sequence.wge_ctl, FC_RCTL_ELS4_REP);
	bf_set(wqe_type, &wqe->xmit_sequence.wge_ctl, FC_TYPE_NVME);

	/* Word 6 */
	bf_set(wqe_ctxt_tag, &wqe->xmit_sequence.wqe_com,
	       phba->sli4_hba.rpi_ids[ndlp->nlp_rpi]);
	bf_set(wqe_xri_tag, &wqe->xmit_sequence.wqe_com, nvmewqe->sli4_xritag);

	/* Word 7 */
	bf_set(wqe_cmnd, &wqe->xmit_sequence.wqe_com,
	       CMD_XMIT_SEQUENCE64_WQE);
	bf_set(wqe_ct, &wqe->xmit_sequence.wqe_com, SLI4_CT_RPI);
	bf_set(wqe_class, &wqe->xmit_sequence.wqe_com, CLASS3);
	bf_set(wqe_pu, &wqe->xmit_sequence.wqe_com, 0);

	/* Word 8 */
	wqe->xmit_sequence.wqe_com.abort_tag = nvmewqe->iotag;

	/* Word 9 */
	bf_set(wqe_reqtag, &wqe->xmit_sequence.wqe_com, nvmewqe->iotag);
	/* Needs to be set by caller */
	bf_set(wqe_rcvoxid, &wqe->xmit_sequence.wqe_com, ctxp->oxid);

	/* Word 10 */
	bf_set(wqe_dbde, &wqe->xmit_sequence.wqe_com, 1);
	bf_set(wqe_iod, &wqe->xmit_sequence.wqe_com, LPFC_WQE_IOD_WRITE);
	bf_set(wqe_lenloc, &wqe->xmit_sequence.wqe_com,
	       LPFC_WQE_LENLOC_WORD12);
	bf_set(wqe_ebde_cnt, &wqe->xmit_sequence.wqe_com, 0);

	/* Word 11 */
	bf_set(wqe_cqid, &wqe->xmit_sequence.wqe_com,
	       LPFC_WQE_CQ_ID_DEFAULT);
	bf_set(wqe_cmd_type, &wqe->xmit_sequence.wqe_com,
	       OTHER_COMMAND);

	/* Word 12 */
	wqe->xmit_sequence.xmit_len = rspsize;

	nvmewqe->retry = 1;
	nvmewqe->vport = phba->pport;
	nvmewqe->drvrTimeout = (phba->fc_ratov * 3) + LPFC_DRVR_TIMEOUT;
	nvmewqe->iocb_flag |= LPFC_IO_NVME_LS;

	/* Xmit NVMET response to remote NPORT <did> */
	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_DISC,
			"6039 Xmit NVMET LS response to remote "
			"NPORT x%x iotag:x%x oxid:x%x size:x%x\n",
			ndlp->nlp_DID, nvmewqe->iotag, ctxp->oxid,
			rspsize);
	return nvmewqe;

nvme_wqe_free_wqeq_exit:
	nvmewqe->context2 = NULL;
	nvmewqe->context3 = NULL;
	lpfc_sli_release_iocbq(phba, nvmewqe);
	return NULL;
}


static struct lpfc_iocbq *
lpfc_nvmet_prep_fcp_wqe(struct lpfc_hba *phba,
			struct lpfc_async_xchg_ctx *ctxp)
{
	struct nvmefc_tgt_fcp_req *rsp = &ctxp->hdlrctx.fcp_req;
	struct lpfc_nvmet_tgtport *tgtp;
	struct sli4_sge *sgl;
	struct lpfc_nodelist *ndlp;
	struct lpfc_iocbq *nvmewqe;
	struct scatterlist *sgel;
	union lpfc_wqe128 *wqe;
	struct ulp_bde64 *bde;
	dma_addr_t physaddr;
	int i, cnt, nsegs;
	int do_pbde;
	int xc = 1;

	if (!lpfc_is_link_up(phba)) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6107 NVMET prep FCP wqe: link err:"
				"NPORT x%x oxid x%x ste %d\n",
				ctxp->sid, ctxp->oxid, ctxp->state);
		return NULL;
	}

	ndlp = lpfc_findnode_did(phba->pport, ctxp->sid);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp) ||
	    ((ndlp->nlp_state != NLP_STE_UNMAPPED_NODE) &&
	     (ndlp->nlp_state != NLP_STE_MAPPED_NODE))) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6108 NVMET prep FCP wqe: no ndlp: "
				"NPORT x%x oxid x%x ste %d\n",
				ctxp->sid, ctxp->oxid, ctxp->state);
		return NULL;
	}

	if (rsp->sg_cnt > lpfc_tgttemplate.max_sgl_segments) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6109 NVMET prep FCP wqe: seg cnt err: "
				"NPORT x%x oxid x%x ste %d cnt %d\n",
				ctxp->sid, ctxp->oxid, ctxp->state,
				phba->cfg_nvme_seg_cnt);
		return NULL;
	}
	nsegs = rsp->sg_cnt;

	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	nvmewqe = ctxp->wqeq;
	if (nvmewqe == NULL) {
		/* Allocate buffer for  command wqe */
		nvmewqe = ctxp->ctxbuf->iocbq;
		if (nvmewqe == NULL) {
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"6110 NVMET prep FCP wqe: No "
					"WQE: NPORT x%x oxid x%x ste %d\n",
					ctxp->sid, ctxp->oxid, ctxp->state);
			return NULL;
		}
		ctxp->wqeq = nvmewqe;
		xc = 0; /* create new XRI */
		nvmewqe->sli4_lxritag = NO_XRI;
		nvmewqe->sli4_xritag = NO_XRI;
	}

	/* Sanity check */
	if (((ctxp->state == LPFC_NVME_STE_RCV) &&
	    (ctxp->entry_cnt == 1)) ||
	    (ctxp->state == LPFC_NVME_STE_DATA)) {
		wqe = &nvmewqe->wqe;
	} else {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6111 Wrong state NVMET FCP: %d  cnt %d\n",
				ctxp->state, ctxp->entry_cnt);
		return NULL;
	}

	sgl  = (struct sli4_sge *)ctxp->ctxbuf->sglq->sgl;
	switch (rsp->op) {
	case NVMET_FCOP_READDATA:
	case NVMET_FCOP_READDATA_RSP:
		/* From the tsend template, initialize words 7 - 11 */
		memcpy(&wqe->words[7],
		       &lpfc_tsend_cmd_template.words[7],
		       sizeof(uint32_t) * 5);

		/* Words 0 - 2 : The first sg segment */
		sgel = &rsp->sg[0];
		physaddr = sg_dma_address(sgel);
		wqe->fcp_tsend.bde.tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		wqe->fcp_tsend.bde.tus.f.bdeSize = sg_dma_len(sgel);
		wqe->fcp_tsend.bde.addrLow = cpu_to_le32(putPaddrLow(physaddr));
		wqe->fcp_tsend.bde.addrHigh =
			cpu_to_le32(putPaddrHigh(physaddr));

		/* Word 3 */
		wqe->fcp_tsend.payload_offset_len = 0;

		/* Word 4 */
		wqe->fcp_tsend.relative_offset = ctxp->offset;

		/* Word 5 */
		wqe->fcp_tsend.reserved = 0;

		/* Word 6 */
		bf_set(wqe_ctxt_tag, &wqe->fcp_tsend.wqe_com,
		       phba->sli4_hba.rpi_ids[ndlp->nlp_rpi]);
		bf_set(wqe_xri_tag, &wqe->fcp_tsend.wqe_com,
		       nvmewqe->sli4_xritag);

		/* Word 7 - set ar later */

		/* Word 8 */
		wqe->fcp_tsend.wqe_com.abort_tag = nvmewqe->iotag;

		/* Word 9 */
		bf_set(wqe_reqtag, &wqe->fcp_tsend.wqe_com, nvmewqe->iotag);
		bf_set(wqe_rcvoxid, &wqe->fcp_tsend.wqe_com, ctxp->oxid);

		/* Word 10 - set wqes later, in template xc=1 */
		if (!xc)
			bf_set(wqe_xc, &wqe->fcp_tsend.wqe_com, 0);

		/* Word 11 - set sup, irsp, irsplen later */
		do_pbde = 0;

		/* Word 12 */
		wqe->fcp_tsend.fcp_data_len = rsp->transfer_length;

		/* Setup 2 SKIP SGEs */
		sgl->addr_hi = 0;
		sgl->addr_lo = 0;
		sgl->word2 = 0;
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_SKIP);
		sgl->word2 = cpu_to_le32(sgl->word2);
		sgl->sge_len = 0;
		sgl++;
		sgl->addr_hi = 0;
		sgl->addr_lo = 0;
		sgl->word2 = 0;
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_SKIP);
		sgl->word2 = cpu_to_le32(sgl->word2);
		sgl->sge_len = 0;
		sgl++;
		if (rsp->op == NVMET_FCOP_READDATA_RSP) {
			atomic_inc(&tgtp->xmt_fcp_read_rsp);

			/* In template ar=1 wqes=0 sup=0 irsp=0 irsplen=0 */

			if (rsp->rsplen == LPFC_NVMET_SUCCESS_LEN) {
				if (ndlp->nlp_flag & NLP_SUPPRESS_RSP)
					bf_set(wqe_sup,
					       &wqe->fcp_tsend.wqe_com, 1);
			} else {
				bf_set(wqe_wqes, &wqe->fcp_tsend.wqe_com, 1);
				bf_set(wqe_irsp, &wqe->fcp_tsend.wqe_com, 1);
				bf_set(wqe_irsplen, &wqe->fcp_tsend.wqe_com,
				       ((rsp->rsplen >> 2) - 1));
				memcpy(&wqe->words[16], rsp->rspaddr,
				       rsp->rsplen);
			}
		} else {
			atomic_inc(&tgtp->xmt_fcp_read);

			/* In template ar=1 wqes=0 sup=0 irsp=0 irsplen=0 */
			bf_set(wqe_ar, &wqe->fcp_tsend.wqe_com, 0);
		}
		break;

	case NVMET_FCOP_WRITEDATA:
		/* From the treceive template, initialize words 3 - 11 */
		memcpy(&wqe->words[3],
		       &lpfc_treceive_cmd_template.words[3],
		       sizeof(uint32_t) * 9);

		/* Words 0 - 2 : First SGE is skipped, set invalid BDE type */
		wqe->fcp_treceive.bde.tus.f.bdeFlags = LPFC_SGE_TYPE_SKIP;
		wqe->fcp_treceive.bde.tus.f.bdeSize = 0;
		wqe->fcp_treceive.bde.addrLow = 0;
		wqe->fcp_treceive.bde.addrHigh = 0;

		/* Word 4 */
		wqe->fcp_treceive.relative_offset = ctxp->offset;

		/* Word 6 */
		bf_set(wqe_ctxt_tag, &wqe->fcp_treceive.wqe_com,
		       phba->sli4_hba.rpi_ids[ndlp->nlp_rpi]);
		bf_set(wqe_xri_tag, &wqe->fcp_treceive.wqe_com,
		       nvmewqe->sli4_xritag);

		/* Word 7 */

		/* Word 8 */
		wqe->fcp_treceive.wqe_com.abort_tag = nvmewqe->iotag;

		/* Word 9 */
		bf_set(wqe_reqtag, &wqe->fcp_treceive.wqe_com, nvmewqe->iotag);
		bf_set(wqe_rcvoxid, &wqe->fcp_treceive.wqe_com, ctxp->oxid);

		/* Word 10 - in template xc=1 */
		if (!xc)
			bf_set(wqe_xc, &wqe->fcp_treceive.wqe_com, 0);

		/* Word 11 - set pbde later */
		if (phba->cfg_enable_pbde) {
			do_pbde = 1;
		} else {
			bf_set(wqe_pbde, &wqe->fcp_treceive.wqe_com, 0);
			do_pbde = 0;
		}

		/* Word 12 */
		wqe->fcp_tsend.fcp_data_len = rsp->transfer_length;

		/* Setup 2 SKIP SGEs */
		sgl->addr_hi = 0;
		sgl->addr_lo = 0;
		sgl->word2 = 0;
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_SKIP);
		sgl->word2 = cpu_to_le32(sgl->word2);
		sgl->sge_len = 0;
		sgl++;
		sgl->addr_hi = 0;
		sgl->addr_lo = 0;
		sgl->word2 = 0;
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_SKIP);
		sgl->word2 = cpu_to_le32(sgl->word2);
		sgl->sge_len = 0;
		sgl++;
		atomic_inc(&tgtp->xmt_fcp_write);
		break;

	case NVMET_FCOP_RSP:
		/* From the treceive template, initialize words 4 - 11 */
		memcpy(&wqe->words[4],
		       &lpfc_trsp_cmd_template.words[4],
		       sizeof(uint32_t) * 8);

		/* Words 0 - 2 */
		physaddr = rsp->rspdma;
		wqe->fcp_trsp.bde.tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		wqe->fcp_trsp.bde.tus.f.bdeSize = rsp->rsplen;
		wqe->fcp_trsp.bde.addrLow =
			cpu_to_le32(putPaddrLow(physaddr));
		wqe->fcp_trsp.bde.addrHigh =
			cpu_to_le32(putPaddrHigh(physaddr));

		/* Word 3 */
		wqe->fcp_trsp.response_len = rsp->rsplen;

		/* Word 6 */
		bf_set(wqe_ctxt_tag, &wqe->fcp_trsp.wqe_com,
		       phba->sli4_hba.rpi_ids[ndlp->nlp_rpi]);
		bf_set(wqe_xri_tag, &wqe->fcp_trsp.wqe_com,
		       nvmewqe->sli4_xritag);

		/* Word 7 */

		/* Word 8 */
		wqe->fcp_trsp.wqe_com.abort_tag = nvmewqe->iotag;

		/* Word 9 */
		bf_set(wqe_reqtag, &wqe->fcp_trsp.wqe_com, nvmewqe->iotag);
		bf_set(wqe_rcvoxid, &wqe->fcp_trsp.wqe_com, ctxp->oxid);

		/* Word 10 */
		if (xc)
			bf_set(wqe_xc, &wqe->fcp_trsp.wqe_com, 1);

		/* Word 11 */
		/* In template wqes=0 irsp=0 irsplen=0 - good response */
		if (rsp->rsplen != LPFC_NVMET_SUCCESS_LEN) {
			/* Bad response - embed it */
			bf_set(wqe_wqes, &wqe->fcp_trsp.wqe_com, 1);
			bf_set(wqe_irsp, &wqe->fcp_trsp.wqe_com, 1);
			bf_set(wqe_irsplen, &wqe->fcp_trsp.wqe_com,
			       ((rsp->rsplen >> 2) - 1));
			memcpy(&wqe->words[16], rsp->rspaddr, rsp->rsplen);
		}
		do_pbde = 0;

		/* Word 12 */
		wqe->fcp_trsp.rsvd_12_15[0] = 0;

		/* Use rspbuf, NOT sg list */
		nsegs = 0;
		sgl->word2 = 0;
		atomic_inc(&tgtp->xmt_fcp_rsp);
		break;

	default:
		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_IOERR,
				"6064 Unknown Rsp Op %d\n",
				rsp->op);
		return NULL;
	}

	nvmewqe->retry = 1;
	nvmewqe->vport = phba->pport;
	nvmewqe->drvrTimeout = (phba->fc_ratov * 3) + LPFC_DRVR_TIMEOUT;
	nvmewqe->context1 = ndlp;

	for_each_sg(rsp->sg, sgel, nsegs, i) {
		physaddr = sg_dma_address(sgel);
		cnt = sg_dma_len(sgel);
		sgl->addr_hi = putPaddrHigh(physaddr);
		sgl->addr_lo = putPaddrLow(physaddr);
		sgl->word2 = 0;
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DATA);
		bf_set(lpfc_sli4_sge_offset, sgl, ctxp->offset);
		if ((i+1) == rsp->sg_cnt)
			bf_set(lpfc_sli4_sge_last, sgl, 1);
		sgl->word2 = cpu_to_le32(sgl->word2);
		sgl->sge_len = cpu_to_le32(cnt);
		if (i == 0) {
			bde = (struct ulp_bde64 *)&wqe->words[13];
			if (do_pbde) {
				/* Words 13-15  (PBDE) */
				bde->addrLow = sgl->addr_lo;
				bde->addrHigh = sgl->addr_hi;
				bde->tus.f.bdeSize =
					le32_to_cpu(sgl->sge_len);
				bde->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
				bde->tus.w = cpu_to_le32(bde->tus.w);
			} else {
				memset(bde, 0, sizeof(struct ulp_bde64));
			}
		}
		sgl++;
		ctxp->offset += cnt;
	}
	ctxp->state = LPFC_NVME_STE_DATA;
	ctxp->entry_cnt++;
	return nvmewqe;
}

/**
 * lpfc_nvmet_sol_fcp_abort_cmp - Completion handler for ABTS
 * @phba: Pointer to HBA context object.
 * @cmdwqe: Pointer to driver command WQE object.
 * @wcqe: Pointer to driver response CQE object.
 *
 * The function is called from SLI ring event handler with no
 * lock held. This function is the completion handler for NVME ABTS for FCP cmds
 * The function frees memory resources used for the NVME commands.
 **/
static void
lpfc_nvmet_sol_fcp_abort_cmp(struct lpfc_hba *phba, struct lpfc_iocbq *cmdwqe,
			     struct lpfc_wcqe_complete *wcqe)
{
	struct lpfc_async_xchg_ctx *ctxp;
	struct lpfc_nvmet_tgtport *tgtp;
	uint32_t result;
	unsigned long flags;
	bool released = false;

	ctxp = cmdwqe->context2;
	result = wcqe->parameter;

	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	if (ctxp->flag & LPFC_NVME_ABORT_OP)
		atomic_inc(&tgtp->xmt_fcp_abort_cmpl);

	spin_lock_irqsave(&ctxp->ctxlock, flags);
	ctxp->state = LPFC_NVME_STE_DONE;

	/* Check if we already received a free context call
	 * and we have completed processing an abort situation.
	 */
	if ((ctxp->flag & LPFC_NVME_CTX_RLS) &&
	    !(ctxp->flag & LPFC_NVME_XBUSY)) {
		spin_lock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		list_del_init(&ctxp->list);
		spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		released = true;
	}
	ctxp->flag &= ~LPFC_NVME_ABORT_OP;
	spin_unlock_irqrestore(&ctxp->ctxlock, flags);
	atomic_inc(&tgtp->xmt_abort_rsp);

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6165 ABORT cmpl: oxid x%x flg x%x (%d) "
			"WCQE: %08x %08x %08x %08x\n",
			ctxp->oxid, ctxp->flag, released,
			wcqe->word0, wcqe->total_data_placed,
			result, wcqe->word3);

	cmdwqe->context2 = NULL;
	cmdwqe->context3 = NULL;
	/*
	 * if transport has released ctx, then can reuse it. Otherwise,
	 * will be recycled by transport release call.
	 */
	if (released)
		lpfc_nvmet_ctxbuf_post(phba, ctxp->ctxbuf);

	/* This is the iocbq for the abort, not the command */
	lpfc_sli_release_iocbq(phba, cmdwqe);

	/* Since iaab/iaar are NOT set, there is no work left.
	 * For LPFC_NVME_XBUSY, lpfc_sli4_nvmet_xri_aborted
	 * should have been called already.
	 */
}

/**
 * lpfc_nvmet_unsol_fcp_abort_cmp - Completion handler for ABTS
 * @phba: Pointer to HBA context object.
 * @cmdwqe: Pointer to driver command WQE object.
 * @wcqe: Pointer to driver response CQE object.
 *
 * The function is called from SLI ring event handler with no
 * lock held. This function is the completion handler for NVME ABTS for FCP cmds
 * The function frees memory resources used for the NVME commands.
 **/
static void
lpfc_nvmet_unsol_fcp_abort_cmp(struct lpfc_hba *phba, struct lpfc_iocbq *cmdwqe,
			       struct lpfc_wcqe_complete *wcqe)
{
	struct lpfc_async_xchg_ctx *ctxp;
	struct lpfc_nvmet_tgtport *tgtp;
	unsigned long flags;
	uint32_t result;
	bool released = false;

	ctxp = cmdwqe->context2;
	result = wcqe->parameter;

	if (!ctxp) {
		/* if context is clear, related io alrady complete */
		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
				"6070 ABTS cmpl: WCQE: %08x %08x %08x %08x\n",
				wcqe->word0, wcqe->total_data_placed,
				result, wcqe->word3);
		return;
	}

	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	spin_lock_irqsave(&ctxp->ctxlock, flags);
	if (ctxp->flag & LPFC_NVME_ABORT_OP)
		atomic_inc(&tgtp->xmt_fcp_abort_cmpl);

	/* Sanity check */
	if (ctxp->state != LPFC_NVME_STE_ABORT) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6112 ABTS Wrong state:%d oxid x%x\n",
				ctxp->state, ctxp->oxid);
	}

	/* Check if we already received a free context call
	 * and we have completed processing an abort situation.
	 */
	ctxp->state = LPFC_NVME_STE_DONE;
	if ((ctxp->flag & LPFC_NVME_CTX_RLS) &&
	    !(ctxp->flag & LPFC_NVME_XBUSY)) {
		spin_lock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		list_del_init(&ctxp->list);
		spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		released = true;
	}
	ctxp->flag &= ~LPFC_NVME_ABORT_OP;
	spin_unlock_irqrestore(&ctxp->ctxlock, flags);
	atomic_inc(&tgtp->xmt_abort_rsp);

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6316 ABTS cmpl oxid x%x flg x%x (%x) "
			"WCQE: %08x %08x %08x %08x\n",
			ctxp->oxid, ctxp->flag, released,
			wcqe->word0, wcqe->total_data_placed,
			result, wcqe->word3);

	cmdwqe->context2 = NULL;
	cmdwqe->context3 = NULL;
	/*
	 * if transport has released ctx, then can reuse it. Otherwise,
	 * will be recycled by transport release call.
	 */
	if (released)
		lpfc_nvmet_ctxbuf_post(phba, ctxp->ctxbuf);

	/* Since iaab/iaar are NOT set, there is no work left.
	 * For LPFC_NVME_XBUSY, lpfc_sli4_nvmet_xri_aborted
	 * should have been called already.
	 */
}

/**
 * lpfc_nvmet_xmt_ls_abort_cmp - Completion handler for ABTS
 * @phba: Pointer to HBA context object.
 * @cmdwqe: Pointer to driver command WQE object.
 * @wcqe: Pointer to driver response CQE object.
 *
 * The function is called from SLI ring event handler with no
 * lock held. This function is the completion handler for NVME ABTS for LS cmds
 * The function frees memory resources used for the NVME commands.
 **/
static void
lpfc_nvmet_xmt_ls_abort_cmp(struct lpfc_hba *phba, struct lpfc_iocbq *cmdwqe,
			    struct lpfc_wcqe_complete *wcqe)
{
	struct lpfc_async_xchg_ctx *ctxp;
	struct lpfc_nvmet_tgtport *tgtp;
	uint32_t result;

	ctxp = cmdwqe->context2;
	result = wcqe->parameter;

	if (phba->nvmet_support) {
		tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
		atomic_inc(&tgtp->xmt_ls_abort_cmpl);
	}

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6083 Abort cmpl: ctx x%px WCQE:%08x %08x %08x %08x\n",
			ctxp, wcqe->word0, wcqe->total_data_placed,
			result, wcqe->word3);

	if (!ctxp) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6415 NVMET LS Abort No ctx: WCQE: "
				 "%08x %08x %08x %08x\n",
				wcqe->word0, wcqe->total_data_placed,
				result, wcqe->word3);

		lpfc_sli_release_iocbq(phba, cmdwqe);
		return;
	}

	if (ctxp->state != LPFC_NVME_STE_LS_ABORT) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6416 NVMET LS abort cmpl state mismatch: "
				"oxid x%x: %d %d\n",
				ctxp->oxid, ctxp->state, ctxp->entry_cnt);
	}

	cmdwqe->context2 = NULL;
	cmdwqe->context3 = NULL;
	lpfc_sli_release_iocbq(phba, cmdwqe);
	kfree(ctxp);
}

static int
lpfc_nvmet_unsol_issue_abort(struct lpfc_hba *phba,
			     struct lpfc_async_xchg_ctx *ctxp,
			     uint32_t sid, uint16_t xri)
{
	struct lpfc_nvmet_tgtport *tgtp = NULL;
	struct lpfc_iocbq *abts_wqeq;
	union lpfc_wqe128 *wqe_abts;
	struct lpfc_nodelist *ndlp;

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6067 ABTS: sid %x xri x%x/x%x\n",
			sid, xri, ctxp->wqeq->sli4_xritag);

	if (phba->nvmet_support && phba->targetport)
		tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;

	ndlp = lpfc_findnode_did(phba->pport, sid);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp) ||
	    ((ndlp->nlp_state != NLP_STE_UNMAPPED_NODE) &&
	    (ndlp->nlp_state != NLP_STE_MAPPED_NODE))) {
		if (tgtp)
			atomic_inc(&tgtp->xmt_abort_rsp_error);
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6134 Drop ABTS - wrong NDLP state x%x.\n",
				(ndlp) ? ndlp->nlp_state : NLP_STE_MAX_STATE);

		/* No failure to an ABTS request. */
		return 0;
	}

	abts_wqeq = ctxp->wqeq;
	wqe_abts = &abts_wqeq->wqe;

	/*
	 * Since we zero the whole WQE, we need to ensure we set the WQE fields
	 * that were initialized in lpfc_sli4_nvmet_alloc.
	 */
	memset(wqe_abts, 0, sizeof(union lpfc_wqe));

	/* Word 5 */
	bf_set(wqe_dfctl, &wqe_abts->xmit_sequence.wge_ctl, 0);
	bf_set(wqe_ls, &wqe_abts->xmit_sequence.wge_ctl, 1);
	bf_set(wqe_la, &wqe_abts->xmit_sequence.wge_ctl, 0);
	bf_set(wqe_rctl, &wqe_abts->xmit_sequence.wge_ctl, FC_RCTL_BA_ABTS);
	bf_set(wqe_type, &wqe_abts->xmit_sequence.wge_ctl, FC_TYPE_BLS);

	/* Word 6 */
	bf_set(wqe_ctxt_tag, &wqe_abts->xmit_sequence.wqe_com,
	       phba->sli4_hba.rpi_ids[ndlp->nlp_rpi]);
	bf_set(wqe_xri_tag, &wqe_abts->xmit_sequence.wqe_com,
	       abts_wqeq->sli4_xritag);

	/* Word 7 */
	bf_set(wqe_cmnd, &wqe_abts->xmit_sequence.wqe_com,
	       CMD_XMIT_SEQUENCE64_WQE);
	bf_set(wqe_ct, &wqe_abts->xmit_sequence.wqe_com, SLI4_CT_RPI);
	bf_set(wqe_class, &wqe_abts->xmit_sequence.wqe_com, CLASS3);
	bf_set(wqe_pu, &wqe_abts->xmit_sequence.wqe_com, 0);

	/* Word 8 */
	wqe_abts->xmit_sequence.wqe_com.abort_tag = abts_wqeq->iotag;

	/* Word 9 */
	bf_set(wqe_reqtag, &wqe_abts->xmit_sequence.wqe_com, abts_wqeq->iotag);
	/* Needs to be set by caller */
	bf_set(wqe_rcvoxid, &wqe_abts->xmit_sequence.wqe_com, xri);

	/* Word 10 */
	bf_set(wqe_dbde, &wqe_abts->xmit_sequence.wqe_com, 1);
	bf_set(wqe_iod, &wqe_abts->xmit_sequence.wqe_com, LPFC_WQE_IOD_WRITE);
	bf_set(wqe_lenloc, &wqe_abts->xmit_sequence.wqe_com,
	       LPFC_WQE_LENLOC_WORD12);
	bf_set(wqe_ebde_cnt, &wqe_abts->xmit_sequence.wqe_com, 0);
	bf_set(wqe_qosd, &wqe_abts->xmit_sequence.wqe_com, 0);

	/* Word 11 */
	bf_set(wqe_cqid, &wqe_abts->xmit_sequence.wqe_com,
	       LPFC_WQE_CQ_ID_DEFAULT);
	bf_set(wqe_cmd_type, &wqe_abts->xmit_sequence.wqe_com,
	       OTHER_COMMAND);

	abts_wqeq->vport = phba->pport;
	abts_wqeq->context1 = ndlp;
	abts_wqeq->context2 = ctxp;
	abts_wqeq->context3 = NULL;
	abts_wqeq->rsvd2 = 0;
	/* hba_wqidx should already be setup from command we are aborting */
	abts_wqeq->iocb.ulpCommand = CMD_XMIT_SEQUENCE64_CR;
	abts_wqeq->iocb.ulpLe = 1;

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6069 Issue ABTS to xri x%x reqtag x%x\n",
			xri, abts_wqeq->iotag);
	return 1;
}

static int
lpfc_nvmet_sol_fcp_issue_abort(struct lpfc_hba *phba,
			       struct lpfc_async_xchg_ctx *ctxp,
			       uint32_t sid, uint16_t xri)
{
	struct lpfc_nvmet_tgtport *tgtp;
	struct lpfc_iocbq *abts_wqeq;
	struct lpfc_nodelist *ndlp;
	unsigned long flags;
	u8 opt;
	int rc;

	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	if (!ctxp->wqeq) {
		ctxp->wqeq = ctxp->ctxbuf->iocbq;
		ctxp->wqeq->hba_wqidx = 0;
	}

	ndlp = lpfc_findnode_did(phba->pport, sid);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp) ||
	    ((ndlp->nlp_state != NLP_STE_UNMAPPED_NODE) &&
	    (ndlp->nlp_state != NLP_STE_MAPPED_NODE))) {
		atomic_inc(&tgtp->xmt_abort_rsp_error);
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6160 Drop ABORT - wrong NDLP state x%x.\n",
				(ndlp) ? ndlp->nlp_state : NLP_STE_MAX_STATE);

		/* No failure to an ABTS request. */
		spin_lock_irqsave(&ctxp->ctxlock, flags);
		ctxp->flag &= ~LPFC_NVME_ABORT_OP;
		spin_unlock_irqrestore(&ctxp->ctxlock, flags);
		return 0;
	}

	/* Issue ABTS for this WQE based on iotag */
	ctxp->abort_wqeq = lpfc_sli_get_iocbq(phba);
	spin_lock_irqsave(&ctxp->ctxlock, flags);
	if (!ctxp->abort_wqeq) {
		atomic_inc(&tgtp->xmt_abort_rsp_error);
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6161 ABORT failed: No wqeqs: "
				"xri: x%x\n", ctxp->oxid);
		/* No failure to an ABTS request. */
		ctxp->flag &= ~LPFC_NVME_ABORT_OP;
		spin_unlock_irqrestore(&ctxp->ctxlock, flags);
		return 0;
	}
	abts_wqeq = ctxp->abort_wqeq;
	ctxp->state = LPFC_NVME_STE_ABORT;
	opt = (ctxp->flag & LPFC_NVME_ABTS_RCV) ? INHIBIT_ABORT : 0;
	spin_unlock_irqrestore(&ctxp->ctxlock, flags);

	/* Announce entry to new IO submit field. */
	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6162 ABORT Request to rport DID x%06x "
			"for xri x%x x%x\n",
			ctxp->sid, ctxp->oxid, ctxp->wqeq->sli4_xritag);

	/* If the hba is getting reset, this flag is set.  It is
	 * cleared when the reset is complete and rings reestablished.
	 */
	spin_lock_irqsave(&phba->hbalock, flags);
	/* driver queued commands are in process of being flushed */
	if (phba->hba_flag & HBA_IOQ_FLUSH) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		atomic_inc(&tgtp->xmt_abort_rsp_error);
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6163 Driver in reset cleanup - flushing "
				"NVME Req now. hba_flag x%x oxid x%x\n",
				phba->hba_flag, ctxp->oxid);
		lpfc_sli_release_iocbq(phba, abts_wqeq);
		spin_lock_irqsave(&ctxp->ctxlock, flags);
		ctxp->flag &= ~LPFC_NVME_ABORT_OP;
		spin_unlock_irqrestore(&ctxp->ctxlock, flags);
		return 0;
	}

	/* Outstanding abort is in progress */
	if (abts_wqeq->iocb_flag & LPFC_DRIVER_ABORTED) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		atomic_inc(&tgtp->xmt_abort_rsp_error);
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6164 Outstanding NVME I/O Abort Request "
				"still pending on oxid x%x\n",
				ctxp->oxid);
		lpfc_sli_release_iocbq(phba, abts_wqeq);
		spin_lock_irqsave(&ctxp->ctxlock, flags);
		ctxp->flag &= ~LPFC_NVME_ABORT_OP;
		spin_unlock_irqrestore(&ctxp->ctxlock, flags);
		return 0;
	}

	/* Ready - mark outstanding as aborted by driver. */
	abts_wqeq->iocb_flag |= LPFC_DRIVER_ABORTED;

	lpfc_nvme_prep_abort_wqe(abts_wqeq, ctxp->wqeq->sli4_xritag, opt);

	/* ABTS WQE must go to the same WQ as the WQE to be aborted */
	abts_wqeq->hba_wqidx = ctxp->wqeq->hba_wqidx;
	abts_wqeq->wqe_cmpl = lpfc_nvmet_sol_fcp_abort_cmp;
	abts_wqeq->iocb_cmpl = NULL;
	abts_wqeq->iocb_flag |= LPFC_IO_NVME;
	abts_wqeq->context2 = ctxp;
	abts_wqeq->vport = phba->pport;
	if (!ctxp->hdwq)
		ctxp->hdwq = &phba->sli4_hba.hdwq[abts_wqeq->hba_wqidx];

	rc = lpfc_sli4_issue_wqe(phba, ctxp->hdwq, abts_wqeq);
	spin_unlock_irqrestore(&phba->hbalock, flags);
	if (rc == WQE_SUCCESS) {
		atomic_inc(&tgtp->xmt_abort_sol);
		return 0;
	}

	atomic_inc(&tgtp->xmt_abort_rsp_error);
	spin_lock_irqsave(&ctxp->ctxlock, flags);
	ctxp->flag &= ~LPFC_NVME_ABORT_OP;
	spin_unlock_irqrestore(&ctxp->ctxlock, flags);
	lpfc_sli_release_iocbq(phba, abts_wqeq);
	lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"6166 Failed ABORT issue_wqe with status x%x "
			"for oxid x%x.\n",
			rc, ctxp->oxid);
	return 1;
}

static int
lpfc_nvmet_unsol_fcp_issue_abort(struct lpfc_hba *phba,
				 struct lpfc_async_xchg_ctx *ctxp,
				 uint32_t sid, uint16_t xri)
{
	struct lpfc_nvmet_tgtport *tgtp;
	struct lpfc_iocbq *abts_wqeq;
	unsigned long flags;
	bool released = false;
	int rc;

	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	if (!ctxp->wqeq) {
		ctxp->wqeq = ctxp->ctxbuf->iocbq;
		ctxp->wqeq->hba_wqidx = 0;
	}

	if (ctxp->state == LPFC_NVME_STE_FREE) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6417 NVMET ABORT ctx freed %d %d oxid x%x\n",
				ctxp->state, ctxp->entry_cnt, ctxp->oxid);
		rc = WQE_BUSY;
		goto aerr;
	}
	ctxp->state = LPFC_NVME_STE_ABORT;
	ctxp->entry_cnt++;
	rc = lpfc_nvmet_unsol_issue_abort(phba, ctxp, sid, xri);
	if (rc == 0)
		goto aerr;

	spin_lock_irqsave(&phba->hbalock, flags);
	abts_wqeq = ctxp->wqeq;
	abts_wqeq->wqe_cmpl = lpfc_nvmet_unsol_fcp_abort_cmp;
	abts_wqeq->iocb_cmpl = NULL;
	abts_wqeq->iocb_flag |= LPFC_IO_NVMET;
	if (!ctxp->hdwq)
		ctxp->hdwq = &phba->sli4_hba.hdwq[abts_wqeq->hba_wqidx];

	rc = lpfc_sli4_issue_wqe(phba, ctxp->hdwq, abts_wqeq);
	spin_unlock_irqrestore(&phba->hbalock, flags);
	if (rc == WQE_SUCCESS) {
		return 0;
	}

aerr:
	spin_lock_irqsave(&ctxp->ctxlock, flags);
	if (ctxp->flag & LPFC_NVME_CTX_RLS) {
		spin_lock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		list_del_init(&ctxp->list);
		spin_unlock(&phba->sli4_hba.abts_nvmet_buf_list_lock);
		released = true;
	}
	ctxp->flag &= ~(LPFC_NVME_ABORT_OP | LPFC_NVME_CTX_RLS);
	spin_unlock_irqrestore(&ctxp->ctxlock, flags);

	atomic_inc(&tgtp->xmt_abort_rsp_error);
	lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"6135 Failed to Issue ABTS for oxid x%x. Status x%x "
			"(%x)\n",
			ctxp->oxid, rc, released);
	if (released)
		lpfc_nvmet_ctxbuf_post(phba, ctxp->ctxbuf);
	return 1;
}

/**
 * lpfc_nvme_unsol_ls_issue_abort - issue ABTS on an exchange received
 *        via async frame receive where the frame is not handled.
 * @phba: pointer to adapter structure
 * @ctxp: pointer to the asynchronously received received sequence
 * @sid: address of the remote port to send the ABTS to
 * @xri: oxid value to for the ABTS (other side's exchange id).
 **/
int
lpfc_nvme_unsol_ls_issue_abort(struct lpfc_hba *phba,
				struct lpfc_async_xchg_ctx *ctxp,
				uint32_t sid, uint16_t xri)
{
	struct lpfc_nvmet_tgtport *tgtp = NULL;
	struct lpfc_iocbq *abts_wqeq;
	unsigned long flags;
	int rc;

	if ((ctxp->state == LPFC_NVME_STE_LS_RCV && ctxp->entry_cnt == 1) ||
	    (ctxp->state == LPFC_NVME_STE_LS_RSP && ctxp->entry_cnt == 2)) {
		ctxp->state = LPFC_NVME_STE_LS_ABORT;
		ctxp->entry_cnt++;
	} else {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"6418 NVMET LS abort state mismatch "
				"IO x%x: %d %d\n",
				ctxp->oxid, ctxp->state, ctxp->entry_cnt);
		ctxp->state = LPFC_NVME_STE_LS_ABORT;
	}

	if (phba->nvmet_support && phba->targetport)
		tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;

	if (!ctxp->wqeq) {
		/* Issue ABTS for this WQE based on iotag */
		ctxp->wqeq = lpfc_sli_get_iocbq(phba);
		if (!ctxp->wqeq) {
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"6068 Abort failed: No wqeqs: "
					"xri: x%x\n", xri);
			/* No failure to an ABTS request. */
			kfree(ctxp);
			return 0;
		}
	}
	abts_wqeq = ctxp->wqeq;

	if (lpfc_nvmet_unsol_issue_abort(phba, ctxp, sid, xri) == 0) {
		rc = WQE_BUSY;
		goto out;
	}

	spin_lock_irqsave(&phba->hbalock, flags);
	abts_wqeq->wqe_cmpl = lpfc_nvmet_xmt_ls_abort_cmp;
	abts_wqeq->iocb_cmpl = NULL;
	abts_wqeq->iocb_flag |=  LPFC_IO_NVME_LS;
	rc = lpfc_sli4_issue_wqe(phba, ctxp->hdwq, abts_wqeq);
	spin_unlock_irqrestore(&phba->hbalock, flags);
	if (rc == WQE_SUCCESS) {
		if (tgtp)
			atomic_inc(&tgtp->xmt_abort_unsol);
		return 0;
	}
out:
	if (tgtp)
		atomic_inc(&tgtp->xmt_abort_rsp_error);
	abts_wqeq->context2 = NULL;
	abts_wqeq->context3 = NULL;
	lpfc_sli_release_iocbq(phba, abts_wqeq);
	lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"6056 Failed to Issue ABTS. Status x%x\n", rc);
	return 1;
}

/**
 * lpfc_nvmet_invalidate_host
 *
 * @phba - pointer to the driver instance bound to an adapter port.
 * @ndlp - pointer to an lpfc_nodelist type
 *
 * This routine upcalls the nvmet transport to invalidate an NVME
 * host to which this target instance had active connections.
 */
void
lpfc_nvmet_invalidate_host(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp)
{
	struct lpfc_nvmet_tgtport *tgtp;

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME | LOG_NVME_ABTS,
			"6203 Invalidating hosthandle x%px\n",
			ndlp);

	tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
	atomic_set(&tgtp->state, LPFC_NVMET_INV_HOST_ACTIVE);

#if (IS_ENABLED(CONFIG_NVME_TARGET_FC))
	/* Need to get the nvmet_fc_target_port pointer here.*/
	nvmet_fc_invalidate_host(phba->targetport, ndlp);
#endif
}
