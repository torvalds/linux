/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2019 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.  *
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

#include <linux/nvme.h>
#include <linux/nvme-fc-driver.h>
#include <linux/nvme-fc.h>
#include "lpfc_version.h"
#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_nvme.h"
#include "lpfc_scsi.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_debugfs.h"

/* NVME initiator-based functions */

static struct lpfc_io_buf *
lpfc_get_nvme_buf(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		  int idx, int expedite);

static void
lpfc_release_nvme_buf(struct lpfc_hba *, struct lpfc_io_buf *);

static struct nvme_fc_port_template lpfc_nvme_template;

static union lpfc_wqe128 lpfc_iread_cmd_template;
static union lpfc_wqe128 lpfc_iwrite_cmd_template;
static union lpfc_wqe128 lpfc_icmnd_cmd_template;

/* Setup WQE templates for NVME IOs */
void
lpfc_nvme_cmd_template(void)
{
	union lpfc_wqe128 *wqe;

	/* IREAD template */
	wqe = &lpfc_iread_cmd_template;
	memset(wqe, 0, sizeof(union lpfc_wqe128));

	/* Word 0, 1, 2 - BDE is variable */

	/* Word 3 - cmd_buff_len, payload_offset_len is zero */

	/* Word 4 - total_xfer_len is variable */

	/* Word 5 - is zero */

	/* Word 6 - ctxt_tag, xri_tag is variable */

	/* Word 7 */
	bf_set(wqe_cmnd, &wqe->fcp_iread.wqe_com, CMD_FCP_IREAD64_WQE);
	bf_set(wqe_pu, &wqe->fcp_iread.wqe_com, PARM_READ_CHECK);
	bf_set(wqe_class, &wqe->fcp_iread.wqe_com, CLASS3);
	bf_set(wqe_ct, &wqe->fcp_iread.wqe_com, SLI4_CT_RPI);

	/* Word 8 - abort_tag is variable */

	/* Word 9  - reqtag is variable */

	/* Word 10 - dbde, wqes is variable */
	bf_set(wqe_qosd, &wqe->fcp_iread.wqe_com, 0);
	bf_set(wqe_nvme, &wqe->fcp_iread.wqe_com, 1);
	bf_set(wqe_iod, &wqe->fcp_iread.wqe_com, LPFC_WQE_IOD_READ);
	bf_set(wqe_lenloc, &wqe->fcp_iread.wqe_com, LPFC_WQE_LENLOC_WORD4);
	bf_set(wqe_dbde, &wqe->fcp_iread.wqe_com, 0);
	bf_set(wqe_wqes, &wqe->fcp_iread.wqe_com, 1);

	/* Word 11 - pbde is variable */
	bf_set(wqe_cmd_type, &wqe->fcp_iread.wqe_com, NVME_READ_CMD);
	bf_set(wqe_cqid, &wqe->fcp_iread.wqe_com, LPFC_WQE_CQ_ID_DEFAULT);
	bf_set(wqe_pbde, &wqe->fcp_iread.wqe_com, 1);

	/* Word 12 - is zero */

	/* Word 13, 14, 15 - PBDE is variable */

	/* IWRITE template */
	wqe = &lpfc_iwrite_cmd_template;
	memset(wqe, 0, sizeof(union lpfc_wqe128));

	/* Word 0, 1, 2 - BDE is variable */

	/* Word 3 - cmd_buff_len, payload_offset_len is zero */

	/* Word 4 - total_xfer_len is variable */

	/* Word 5 - initial_xfer_len is variable */

	/* Word 6 - ctxt_tag, xri_tag is variable */

	/* Word 7 */
	bf_set(wqe_cmnd, &wqe->fcp_iwrite.wqe_com, CMD_FCP_IWRITE64_WQE);
	bf_set(wqe_pu, &wqe->fcp_iwrite.wqe_com, PARM_READ_CHECK);
	bf_set(wqe_class, &wqe->fcp_iwrite.wqe_com, CLASS3);
	bf_set(wqe_ct, &wqe->fcp_iwrite.wqe_com, SLI4_CT_RPI);

	/* Word 8 - abort_tag is variable */

	/* Word 9  - reqtag is variable */

	/* Word 10 - dbde, wqes is variable */
	bf_set(wqe_qosd, &wqe->fcp_iwrite.wqe_com, 0);
	bf_set(wqe_nvme, &wqe->fcp_iwrite.wqe_com, 1);
	bf_set(wqe_iod, &wqe->fcp_iwrite.wqe_com, LPFC_WQE_IOD_WRITE);
	bf_set(wqe_lenloc, &wqe->fcp_iwrite.wqe_com, LPFC_WQE_LENLOC_WORD4);
	bf_set(wqe_dbde, &wqe->fcp_iwrite.wqe_com, 0);
	bf_set(wqe_wqes, &wqe->fcp_iwrite.wqe_com, 1);

	/* Word 11 - pbde is variable */
	bf_set(wqe_cmd_type, &wqe->fcp_iwrite.wqe_com, NVME_WRITE_CMD);
	bf_set(wqe_cqid, &wqe->fcp_iwrite.wqe_com, LPFC_WQE_CQ_ID_DEFAULT);
	bf_set(wqe_pbde, &wqe->fcp_iwrite.wqe_com, 1);

	/* Word 12 - is zero */

	/* Word 13, 14, 15 - PBDE is variable */

	/* ICMND template */
	wqe = &lpfc_icmnd_cmd_template;
	memset(wqe, 0, sizeof(union lpfc_wqe128));

	/* Word 0, 1, 2 - BDE is variable */

	/* Word 3 - payload_offset_len is variable */

	/* Word 4, 5 - is zero */

	/* Word 6 - ctxt_tag, xri_tag is variable */

	/* Word 7 */
	bf_set(wqe_cmnd, &wqe->fcp_icmd.wqe_com, CMD_FCP_ICMND64_WQE);
	bf_set(wqe_pu, &wqe->fcp_icmd.wqe_com, 0);
	bf_set(wqe_class, &wqe->fcp_icmd.wqe_com, CLASS3);
	bf_set(wqe_ct, &wqe->fcp_icmd.wqe_com, SLI4_CT_RPI);

	/* Word 8 - abort_tag is variable */

	/* Word 9  - reqtag is variable */

	/* Word 10 - dbde, wqes is variable */
	bf_set(wqe_qosd, &wqe->fcp_icmd.wqe_com, 1);
	bf_set(wqe_nvme, &wqe->fcp_icmd.wqe_com, 1);
	bf_set(wqe_iod, &wqe->fcp_icmd.wqe_com, LPFC_WQE_IOD_NONE);
	bf_set(wqe_lenloc, &wqe->fcp_icmd.wqe_com, LPFC_WQE_LENLOC_NONE);
	bf_set(wqe_dbde, &wqe->fcp_icmd.wqe_com, 0);
	bf_set(wqe_wqes, &wqe->fcp_icmd.wqe_com, 1);

	/* Word 11 */
	bf_set(wqe_cmd_type, &wqe->fcp_icmd.wqe_com, FCP_COMMAND);
	bf_set(wqe_cqid, &wqe->fcp_icmd.wqe_com, LPFC_WQE_CQ_ID_DEFAULT);
	bf_set(wqe_pbde, &wqe->fcp_icmd.wqe_com, 0);

	/* Word 12, 13, 14, 15 - is zero */
}

/**
 * lpfc_nvme_create_queue -
 * @lpfc_pnvme: Pointer to the driver's nvme instance data
 * @qidx: An cpu index used to affinitize IO queues and MSIX vectors.
 * @handle: An opaque driver handle used in follow-up calls.
 *
 * Driver registers this routine to preallocate and initialize any
 * internal data structures to bind the @qidx to its internal IO queues.
 * A hardware queue maps (qidx) to a specific driver MSI-X vector/EQ/CQ/WQ.
 *
 * Return value :
 *   0 - Success
 *   -EINVAL - Unsupported input value.
 *   -ENOMEM - Could not alloc necessary memory
 **/
static int
lpfc_nvme_create_queue(struct nvme_fc_local_port *pnvme_lport,
		       unsigned int qidx, u16 qsize,
		       void **handle)
{
	struct lpfc_nvme_lport *lport;
	struct lpfc_vport *vport;
	struct lpfc_nvme_qhandle *qhandle;
	char *str;

	if (!pnvme_lport->private)
		return -ENOMEM;

	lport = (struct lpfc_nvme_lport *)pnvme_lport->private;
	vport = lport->vport;
	qhandle = kzalloc(sizeof(struct lpfc_nvme_qhandle), GFP_KERNEL);
	if (qhandle == NULL)
		return -ENOMEM;

	qhandle->cpu_id = raw_smp_processor_id();
	qhandle->qidx = qidx;
	/*
	 * NVME qidx == 0 is the admin queue, so both admin queue
	 * and first IO queue will use MSI-X vector and associated
	 * EQ/CQ/WQ at index 0. After that they are sequentially assigned.
	 */
	if (qidx) {
		str = "IO ";  /* IO queue */
		qhandle->index = ((qidx - 1) %
			lpfc_nvme_template.max_hw_queues);
	} else {
		str = "ADM";  /* Admin queue */
		qhandle->index = qidx;
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME,
			 "6073 Binding %s HdwQueue %d  (cpu %d) to "
			 "hdw_queue %d qhandle %p\n", str,
			 qidx, qhandle->cpu_id, qhandle->index, qhandle);
	*handle = (void *)qhandle;
	return 0;
}

/**
 * lpfc_nvme_delete_queue -
 * @lpfc_pnvme: Pointer to the driver's nvme instance data
 * @qidx: An cpu index used to affinitize IO queues and MSIX vectors.
 * @handle: An opaque driver handle from lpfc_nvme_create_queue
 *
 * Driver registers this routine to free
 * any internal data structures to bind the @qidx to its internal
 * IO queues.
 *
 * Return value :
 *   0 - Success
 *   TODO:  What are the failure codes.
 **/
static void
lpfc_nvme_delete_queue(struct nvme_fc_local_port *pnvme_lport,
		       unsigned int qidx,
		       void *handle)
{
	struct lpfc_nvme_lport *lport;
	struct lpfc_vport *vport;

	if (!pnvme_lport->private)
		return;

	lport = (struct lpfc_nvme_lport *)pnvme_lport->private;
	vport = lport->vport;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME,
			"6001 ENTER.  lpfc_pnvme %p, qidx x%x qhandle %p\n",
			lport, qidx, handle);
	kfree(handle);
}

static void
lpfc_nvme_localport_delete(struct nvme_fc_local_port *localport)
{
	struct lpfc_nvme_lport *lport = localport->private;

	lpfc_printf_vlog(lport->vport, KERN_INFO, LOG_NVME,
			 "6173 localport %p delete complete\n",
			 lport);

	/* release any threads waiting for the unreg to complete */
	if (lport->vport->localport)
		complete(lport->lport_unreg_cmp);
}

/* lpfc_nvme_remoteport_delete
 *
 * @remoteport: Pointer to an nvme transport remoteport instance.
 *
 * This is a template downcall.  NVME transport calls this function
 * when it has completed the unregistration of a previously
 * registered remoteport.
 *
 * Return value :
 * None
 */
static void
lpfc_nvme_remoteport_delete(struct nvme_fc_remote_port *remoteport)
{
	struct lpfc_nvme_rport *rport = remoteport->private;
	struct lpfc_vport *vport;
	struct lpfc_nodelist *ndlp;

	ndlp = rport->ndlp;
	if (!ndlp)
		goto rport_err;

	vport = ndlp->vport;
	if (!vport)
		goto rport_err;

	/* Remove this rport from the lport's list - memory is owned by the
	 * transport. Remove the ndlp reference for the NVME transport before
	 * calling state machine to remove the node.
	 */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_DISC,
			"6146 remoteport delete of remoteport %p\n",
			remoteport);
	spin_lock_irq(&vport->phba->hbalock);

	/* The register rebind might have occurred before the delete
	 * downcall.  Guard against this race.
	 */
	if (ndlp->upcall_flags & NLP_WAIT_FOR_UNREG) {
		ndlp->nrport = NULL;
		ndlp->upcall_flags &= ~NLP_WAIT_FOR_UNREG;
	}
	spin_unlock_irq(&vport->phba->hbalock);

	/* Remove original register reference. The host transport
	 * won't reference this rport/remoteport any further.
	 */
	lpfc_nlp_put(ndlp);

 rport_err:
	return;
}

static void
lpfc_nvme_cmpl_gen_req(struct lpfc_hba *phba, struct lpfc_iocbq *cmdwqe,
		       struct lpfc_wcqe_complete *wcqe)
{
	struct lpfc_vport *vport = cmdwqe->vport;
	struct lpfc_nvme_lport *lport;
	uint32_t status;
	struct nvmefc_ls_req *pnvme_lsreq;
	struct lpfc_dmabuf *buf_ptr;
	struct lpfc_nodelist *ndlp;

	pnvme_lsreq = (struct nvmefc_ls_req *)cmdwqe->context2;
	status = bf_get(lpfc_wcqe_c_status, wcqe) & LPFC_IOCB_STATUS_MASK;

	if (vport->localport) {
		lport = (struct lpfc_nvme_lport *)vport->localport->private;
		if (lport) {
			atomic_inc(&lport->fc4NvmeLsCmpls);
			if (status) {
				if (bf_get(lpfc_wcqe_c_xb, wcqe))
					atomic_inc(&lport->cmpl_ls_xb);
				atomic_inc(&lport->cmpl_ls_err);
			}
		}
	}

	ndlp = (struct lpfc_nodelist *)cmdwqe->context1;
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_DISC,
			 "6047 nvme cmpl Enter "
			 "Data %p DID %x Xri: %x status %x reason x%x cmd:%p "
			 "lsreg:%p bmp:%p ndlp:%p\n",
			 pnvme_lsreq, ndlp ? ndlp->nlp_DID : 0,
			 cmdwqe->sli4_xritag, status,
			 (wcqe->parameter & 0xffff),
			 cmdwqe, pnvme_lsreq, cmdwqe->context3, ndlp);

	lpfc_nvmeio_data(phba, "NVME LS  CMPL: xri x%x stat x%x parm x%x\n",
			 cmdwqe->sli4_xritag, status, wcqe->parameter);

	if (cmdwqe->context3) {
		buf_ptr = (struct lpfc_dmabuf *)cmdwqe->context3;
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
		cmdwqe->context3 = NULL;
	}
	if (pnvme_lsreq->done)
		pnvme_lsreq->done(pnvme_lsreq, status);
	else
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_DISC,
				 "6046 nvme cmpl without done call back? "
				 "Data %p DID %x Xri: %x status %x\n",
				pnvme_lsreq, ndlp ? ndlp->nlp_DID : 0,
				cmdwqe->sli4_xritag, status);
	if (ndlp) {
		lpfc_nlp_put(ndlp);
		cmdwqe->context1 = NULL;
	}
	lpfc_sli_release_iocbq(phba, cmdwqe);
}

static int
lpfc_nvme_gen_req(struct lpfc_vport *vport, struct lpfc_dmabuf *bmp,
		  struct lpfc_dmabuf *inp,
		  struct nvmefc_ls_req *pnvme_lsreq,
		  void (*cmpl)(struct lpfc_hba *, struct lpfc_iocbq *,
			       struct lpfc_wcqe_complete *),
		  struct lpfc_nodelist *ndlp, uint32_t num_entry,
		  uint32_t tmo, uint8_t retry)
{
	struct lpfc_hba *phba = vport->phba;
	union lpfc_wqe128 *wqe;
	struct lpfc_iocbq *genwqe;
	struct ulp_bde64 *bpl;
	struct ulp_bde64 bde;
	int i, rc, xmit_len, first_len;

	/* Allocate buffer for  command WQE */
	genwqe = lpfc_sli_get_iocbq(phba);
	if (genwqe == NULL)
		return 1;

	wqe = &genwqe->wqe;
	memset(wqe, 0, sizeof(union lpfc_wqe));

	genwqe->context3 = (uint8_t *)bmp;
	genwqe->iocb_flag |= LPFC_IO_NVME_LS;

	/* Save for completion so we can release these resources */
	genwqe->context1 = lpfc_nlp_get(ndlp);
	genwqe->context2 = (uint8_t *)pnvme_lsreq;
	/* Fill in payload, bp points to frame payload */

	if (!tmo)
		/* FC spec states we need 3 * ratov for CT requests */
		tmo = (3 * phba->fc_ratov);

	/* For this command calculate the xmit length of the request bde. */
	xmit_len = 0;
	first_len = 0;
	bpl = (struct ulp_bde64 *)bmp->virt;
	for (i = 0; i < num_entry; i++) {
		bde.tus.w = bpl[i].tus.w;
		if (bde.tus.f.bdeFlags != BUFF_TYPE_BDE_64)
			break;
		xmit_len += bde.tus.f.bdeSize;
		if (i == 0)
			first_len = xmit_len;
	}

	genwqe->rsvd2 = num_entry;
	genwqe->hba_wqidx = 0;

	/* Words 0 - 2 */
	wqe->generic.bde.tus.f.bdeFlags = BUFF_TYPE_BDE_64;
	wqe->generic.bde.tus.f.bdeSize = first_len;
	wqe->generic.bde.addrLow = bpl[0].addrLow;
	wqe->generic.bde.addrHigh = bpl[0].addrHigh;

	/* Word 3 */
	wqe->gen_req.request_payload_len = first_len;

	/* Word 4 */

	/* Word 5 */
	bf_set(wqe_dfctl, &wqe->gen_req.wge_ctl, 0);
	bf_set(wqe_si, &wqe->gen_req.wge_ctl, 1);
	bf_set(wqe_la, &wqe->gen_req.wge_ctl, 1);
	bf_set(wqe_rctl, &wqe->gen_req.wge_ctl, FC_RCTL_ELS4_REQ);
	bf_set(wqe_type, &wqe->gen_req.wge_ctl, FC_TYPE_NVME);

	/* Word 6 */
	bf_set(wqe_ctxt_tag, &wqe->gen_req.wqe_com,
	       phba->sli4_hba.rpi_ids[ndlp->nlp_rpi]);
	bf_set(wqe_xri_tag, &wqe->gen_req.wqe_com, genwqe->sli4_xritag);

	/* Word 7 */
	bf_set(wqe_tmo, &wqe->gen_req.wqe_com, (vport->phba->fc_ratov-1));
	bf_set(wqe_class, &wqe->gen_req.wqe_com, CLASS3);
	bf_set(wqe_cmnd, &wqe->gen_req.wqe_com, CMD_GEN_REQUEST64_WQE);
	bf_set(wqe_ct, &wqe->gen_req.wqe_com, SLI4_CT_RPI);

	/* Word 8 */
	wqe->gen_req.wqe_com.abort_tag = genwqe->iotag;

	/* Word 9 */
	bf_set(wqe_reqtag, &wqe->gen_req.wqe_com, genwqe->iotag);

	/* Word 10 */
	bf_set(wqe_dbde, &wqe->gen_req.wqe_com, 1);
	bf_set(wqe_iod, &wqe->gen_req.wqe_com, LPFC_WQE_IOD_READ);
	bf_set(wqe_qosd, &wqe->gen_req.wqe_com, 1);
	bf_set(wqe_lenloc, &wqe->gen_req.wqe_com, LPFC_WQE_LENLOC_NONE);
	bf_set(wqe_ebde_cnt, &wqe->gen_req.wqe_com, 0);

	/* Word 11 */
	bf_set(wqe_cqid, &wqe->gen_req.wqe_com, LPFC_WQE_CQ_ID_DEFAULT);
	bf_set(wqe_cmd_type, &wqe->gen_req.wqe_com, OTHER_COMMAND);


	/* Issue GEN REQ WQE for NPORT <did> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS,
			 "6050 Issue GEN REQ WQE to NPORT x%x "
			 "Data: x%x x%x wq:%p lsreq:%p bmp:%p xmit:%d 1st:%d\n",
			 ndlp->nlp_DID, genwqe->iotag,
			 vport->port_state,
			genwqe, pnvme_lsreq, bmp, xmit_len, first_len);
	genwqe->wqe_cmpl = cmpl;
	genwqe->iocb_cmpl = NULL;
	genwqe->drvrTimeout = tmo + LPFC_DRVR_TIMEOUT;
	genwqe->vport = vport;
	genwqe->retry = retry;

	lpfc_nvmeio_data(phba, "NVME LS  XMIT: xri x%x iotag x%x to x%06x\n",
			 genwqe->sli4_xritag, genwqe->iotag, ndlp->nlp_DID);

	rc = lpfc_sli4_issue_wqe(phba, &phba->sli4_hba.hdwq[0], genwqe);
	if (rc) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_ELS,
				 "6045 Issue GEN REQ WQE to NPORT x%x "
				 "Data: x%x x%x\n",
				 ndlp->nlp_DID, genwqe->iotag,
				 vport->port_state);
		lpfc_sli_release_iocbq(phba, genwqe);
		return 1;
	}
	return 0;
}

/**
 * lpfc_nvme_ls_req - Issue an Link Service request
 * @lpfc_pnvme: Pointer to the driver's nvme instance data
 * @lpfc_nvme_lport: Pointer to the driver's local port data
 * @lpfc_nvme_rport: Pointer to the rport getting the @lpfc_nvme_ereq
 *
 * Driver registers this routine to handle any link service request
 * from the nvme_fc transport to a remote nvme-aware port.
 *
 * Return value :
 *   0 - Success
 *   TODO: What are the failure codes.
 **/
static int
lpfc_nvme_ls_req(struct nvme_fc_local_port *pnvme_lport,
		 struct nvme_fc_remote_port *pnvme_rport,
		 struct nvmefc_ls_req *pnvme_lsreq)
{
	int ret = 0;
	struct lpfc_nvme_lport *lport;
	struct lpfc_nvme_rport *rport;
	struct lpfc_vport *vport;
	struct lpfc_nodelist *ndlp;
	struct ulp_bde64 *bpl;
	struct lpfc_dmabuf *bmp;
	uint16_t ntype, nstate;

	/* there are two dma buf in the request, actually there is one and
	 * the second one is just the start address + cmd size.
	 * Before calling lpfc_nvme_gen_req these buffers need to be wrapped
	 * in a lpfc_dmabuf struct. When freeing we just free the wrapper
	 * because the nvem layer owns the data bufs.
	 * We do not have to break these packets open, we don't care what is in
	 * them. And we do not have to look at the resonse data, we only care
	 * that we got a response. All of the caring is going to happen in the
	 * nvme-fc layer.
	 */

	lport = (struct lpfc_nvme_lport *)pnvme_lport->private;
	rport = (struct lpfc_nvme_rport *)pnvme_rport->private;
	if (unlikely(!lport) || unlikely(!rport))
		return -EINVAL;

	vport = lport->vport;

	if (vport->load_flag & FC_UNLOADING)
		return -ENODEV;

	/* Need the ndlp.  It is stored in the driver's rport. */
	ndlp = rport->ndlp;
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NODE | LOG_NVME_IOERR,
				 "6051 Remoteport %p, rport has invalid ndlp. "
				 "Failing LS Req\n", pnvme_rport);
		return -ENODEV;
	}

	/* The remote node has to be a mapped nvme target or an
	 * unmapped nvme initiator or it's an error.
	 */
	ntype = ndlp->nlp_type;
	nstate = ndlp->nlp_state;
	if ((ntype & NLP_NVME_TARGET && nstate != NLP_STE_MAPPED_NODE) ||
	    (ntype & NLP_NVME_INITIATOR && nstate != NLP_STE_UNMAPPED_NODE)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NODE | LOG_NVME_IOERR,
				 "6088 DID x%06x not ready for "
				 "IO. State x%x, Type x%x\n",
				 pnvme_rport->port_id,
				 ndlp->nlp_state, ndlp->nlp_type);
		return -ENODEV;
	}
	bmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp) {

		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_DISC,
				 "6044 Could not find node for DID %x\n",
				 pnvme_rport->port_id);
		return 2;
	}
	INIT_LIST_HEAD(&bmp->list);
	bmp->virt = lpfc_mbuf_alloc(vport->phba, MEM_PRI, &(bmp->phys));
	if (!bmp->virt) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_DISC,
				 "6042 Could not find node for DID %x\n",
				 pnvme_rport->port_id);
		kfree(bmp);
		return 3;
	}
	bpl = (struct ulp_bde64 *)bmp->virt;
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pnvme_lsreq->rqstdma));
	bpl->addrLow = le32_to_cpu(putPaddrLow(pnvme_lsreq->rqstdma));
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.f.bdeSize = pnvme_lsreq->rqstlen;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);
	bpl++;

	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pnvme_lsreq->rspdma));
	bpl->addrLow = le32_to_cpu(putPaddrLow(pnvme_lsreq->rspdma));
	bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
	bpl->tus.f.bdeSize = pnvme_lsreq->rsplen;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	/* Expand print to include key fields. */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_DISC,
			 "6149 Issue LS Req to DID 0x%06x lport %p, rport %p "
			 "lsreq%p rqstlen:%d rsplen:%d %pad %pad\n",
			 ndlp->nlp_DID,
			 pnvme_lport, pnvme_rport,
			 pnvme_lsreq, pnvme_lsreq->rqstlen,
			 pnvme_lsreq->rsplen, &pnvme_lsreq->rqstdma,
			 &pnvme_lsreq->rspdma);

	atomic_inc(&lport->fc4NvmeLsRequests);

	/* Hardcode the wait to 30 seconds.  Connections are failing otherwise.
	 * This code allows it all to work.
	 */
	ret = lpfc_nvme_gen_req(vport, bmp, pnvme_lsreq->rqstaddr,
				pnvme_lsreq, lpfc_nvme_cmpl_gen_req,
				ndlp, 2, 30, 0);
	if (ret != WQE_SUCCESS) {
		atomic_inc(&lport->xmt_ls_err);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_DISC,
				 "6052 EXIT. issue ls wqe failed lport %p, "
				 "rport %p lsreq%p Status %x DID %x\n",
				 pnvme_lport, pnvme_rport, pnvme_lsreq,
				 ret, ndlp->nlp_DID);
		lpfc_mbuf_free(vport->phba, bmp->virt, bmp->phys);
		kfree(bmp);
		return ret;
	}

	/* Stub in routine and return 0 for now. */
	return ret;
}

/**
 * lpfc_nvme_ls_abort - Issue an Link Service request
 * @lpfc_pnvme: Pointer to the driver's nvme instance data
 * @lpfc_nvme_lport: Pointer to the driver's local port data
 * @lpfc_nvme_rport: Pointer to the rport getting the @lpfc_nvme_ereq
 *
 * Driver registers this routine to handle any link service request
 * from the nvme_fc transport to a remote nvme-aware port.
 *
 * Return value :
 *   0 - Success
 *   TODO: What are the failure codes.
 **/
static void
lpfc_nvme_ls_abort(struct nvme_fc_local_port *pnvme_lport,
		   struct nvme_fc_remote_port *pnvme_rport,
		   struct nvmefc_ls_req *pnvme_lsreq)
{
	struct lpfc_nvme_lport *lport;
	struct lpfc_vport *vport;
	struct lpfc_hba *phba;
	struct lpfc_nodelist *ndlp;
	LIST_HEAD(abort_list);
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *wqe, *next_wqe;

	lport = (struct lpfc_nvme_lport *)pnvme_lport->private;
	if (unlikely(!lport))
		return;
	vport = lport->vport;
	phba = vport->phba;

	if (vport->load_flag & FC_UNLOADING)
		return;

	ndlp = lpfc_findnode_did(vport, pnvme_rport->port_id);
	if (!ndlp) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_ABTS,
				 "6049 Could not find node for DID %x\n",
				 pnvme_rport->port_id);
		return;
	}

	/* Expand print to include key fields. */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_ABTS,
			 "6040 ENTER.  lport %p, rport %p lsreq %p rqstlen:%d "
			 "rsplen:%d %pad %pad\n",
			 pnvme_lport, pnvme_rport,
			 pnvme_lsreq, pnvme_lsreq->rqstlen,
			 pnvme_lsreq->rsplen, &pnvme_lsreq->rqstdma,
			 &pnvme_lsreq->rspdma);

	/*
	 * Lock the ELS ring txcmplq and build a local list of all ELS IOs
	 * that need an ABTS.  The IOs need to stay on the txcmplq so that
	 * the abort operation completes them successfully.
	 */
	pring = phba->sli4_hba.nvmels_wq->pring;
	spin_lock_irq(&phba->hbalock);
	spin_lock(&pring->ring_lock);
	list_for_each_entry_safe(wqe, next_wqe, &pring->txcmplq, list) {
		/* Add to abort_list on on NDLP match. */
		if (lpfc_check_sli_ndlp(phba, pring, wqe, ndlp)) {
			wqe->iocb_flag |= LPFC_DRIVER_ABORTED;
			list_add_tail(&wqe->dlist, &abort_list);
		}
	}
	spin_unlock(&pring->ring_lock);
	spin_unlock_irq(&phba->hbalock);

	/* Abort the targeted IOs and remove them from the abort list. */
	list_for_each_entry_safe(wqe, next_wqe, &abort_list, dlist) {
		atomic_inc(&lport->xmt_ls_abort);
		spin_lock_irq(&phba->hbalock);
		list_del_init(&wqe->dlist);
		lpfc_sli_issue_abort_iotag(phba, pring, wqe);
		spin_unlock_irq(&phba->hbalock);
	}
}

/* Fix up the existing sgls for NVME IO. */
static inline void
lpfc_nvme_adj_fcp_sgls(struct lpfc_vport *vport,
		       struct lpfc_io_buf *lpfc_ncmd,
		       struct nvmefc_fcp_req *nCmd)
{
	struct lpfc_hba  *phba = vport->phba;
	struct sli4_sge *sgl;
	union lpfc_wqe128 *wqe;
	uint32_t *wptr, *dptr;

	/*
	 * Get a local pointer to the built-in wqe and correct
	 * the cmd size to match NVME's 96 bytes and fix
	 * the dma address.
	 */

	wqe = &lpfc_ncmd->cur_iocbq.wqe;

	/*
	 * Adjust the FCP_CMD and FCP_RSP DMA data and sge_len to
	 * match NVME.  NVME sends 96 bytes. Also, use the
	 * nvme commands command and response dma addresses
	 * rather than the virtual memory to ease the restore
	 * operation.
	 */
	sgl = lpfc_ncmd->dma_sgl;
	sgl->sge_len = cpu_to_le32(nCmd->cmdlen);
	if (phba->cfg_nvme_embed_cmd) {
		sgl->addr_hi = 0;
		sgl->addr_lo = 0;

		/* Word 0-2 - NVME CMND IU (embedded payload) */
		wqe->generic.bde.tus.f.bdeFlags = BUFF_TYPE_BDE_IMMED;
		wqe->generic.bde.tus.f.bdeSize = 56;
		wqe->generic.bde.addrHigh = 0;
		wqe->generic.bde.addrLow =  64;  /* Word 16 */

		/* Word 10  - dbde is 0, wqes is 1 in template */

		/*
		 * Embed the payload in the last half of the WQE
		 * WQE words 16-30 get the NVME CMD IU payload
		 *
		 * WQE words 16-19 get payload Words 1-4
		 * WQE words 20-21 get payload Words 6-7
		 * WQE words 22-29 get payload Words 16-23
		 */
		wptr = &wqe->words[16];  /* WQE ptr */
		dptr = (uint32_t *)nCmd->cmdaddr;  /* payload ptr */
		dptr++;			/* Skip Word 0 in payload */

		*wptr++ = *dptr++;	/* Word 1 */
		*wptr++ = *dptr++;	/* Word 2 */
		*wptr++ = *dptr++;	/* Word 3 */
		*wptr++ = *dptr++;	/* Word 4 */
		dptr++;			/* Skip Word 5 in payload */
		*wptr++ = *dptr++;	/* Word 6 */
		*wptr++ = *dptr++;	/* Word 7 */
		dptr += 8;		/* Skip Words 8-15 in payload */
		*wptr++ = *dptr++;	/* Word 16 */
		*wptr++ = *dptr++;	/* Word 17 */
		*wptr++ = *dptr++;	/* Word 18 */
		*wptr++ = *dptr++;	/* Word 19 */
		*wptr++ = *dptr++;	/* Word 20 */
		*wptr++ = *dptr++;	/* Word 21 */
		*wptr++ = *dptr++;	/* Word 22 */
		*wptr   = *dptr;	/* Word 23 */
	} else {
		sgl->addr_hi = cpu_to_le32(putPaddrHigh(nCmd->cmddma));
		sgl->addr_lo = cpu_to_le32(putPaddrLow(nCmd->cmddma));

		/* Word 0-2 - NVME CMND IU Inline BDE */
		wqe->generic.bde.tus.f.bdeFlags =  BUFF_TYPE_BDE_64;
		wqe->generic.bde.tus.f.bdeSize = nCmd->cmdlen;
		wqe->generic.bde.addrHigh = sgl->addr_hi;
		wqe->generic.bde.addrLow =  sgl->addr_lo;

		/* Word 10 */
		bf_set(wqe_dbde, &wqe->generic.wqe_com, 1);
		bf_set(wqe_wqes, &wqe->generic.wqe_com, 0);
	}

	sgl++;

	/* Setup the physical region for the FCP RSP */
	sgl->addr_hi = cpu_to_le32(putPaddrHigh(nCmd->rspdma));
	sgl->addr_lo = cpu_to_le32(putPaddrLow(nCmd->rspdma));
	sgl->word2 = le32_to_cpu(sgl->word2);
	if (nCmd->sg_cnt)
		bf_set(lpfc_sli4_sge_last, sgl, 0);
	else
		bf_set(lpfc_sli4_sge_last, sgl, 1);
	sgl->word2 = cpu_to_le32(sgl->word2);
	sgl->sge_len = cpu_to_le32(nCmd->rsplen);
}

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
static void
lpfc_nvme_ktime(struct lpfc_hba *phba,
		struct lpfc_io_buf *lpfc_ncmd)
{
	uint64_t seg1, seg2, seg3, seg4;
	uint64_t segsum;

	if (!lpfc_ncmd->ts_last_cmd ||
	    !lpfc_ncmd->ts_cmd_start ||
	    !lpfc_ncmd->ts_cmd_wqput ||
	    !lpfc_ncmd->ts_isr_cmpl ||
	    !lpfc_ncmd->ts_data_nvme)
		return;

	if (lpfc_ncmd->ts_data_nvme < lpfc_ncmd->ts_cmd_start)
		return;
	if (lpfc_ncmd->ts_cmd_start < lpfc_ncmd->ts_last_cmd)
		return;
	if (lpfc_ncmd->ts_cmd_wqput < lpfc_ncmd->ts_cmd_start)
		return;
	if (lpfc_ncmd->ts_isr_cmpl < lpfc_ncmd->ts_cmd_wqput)
		return;
	if (lpfc_ncmd->ts_data_nvme < lpfc_ncmd->ts_isr_cmpl)
		return;
	/*
	 * Segment 1 - Time from Last FCP command cmpl is handed
	 * off to NVME Layer to start of next command.
	 * Segment 2 - Time from Driver receives a IO cmd start
	 * from NVME Layer to WQ put is done on IO cmd.
	 * Segment 3 - Time from Driver WQ put is done on IO cmd
	 * to MSI-X ISR for IO cmpl.
	 * Segment 4 - Time from MSI-X ISR for IO cmpl to when
	 * cmpl is handled off to the NVME Layer.
	 */
	seg1 = lpfc_ncmd->ts_cmd_start - lpfc_ncmd->ts_last_cmd;
	if (seg1 > 5000000)  /* 5 ms - for sequential IOs only */
		seg1 = 0;

	/* Calculate times relative to start of IO */
	seg2 = (lpfc_ncmd->ts_cmd_wqput - lpfc_ncmd->ts_cmd_start);
	segsum = seg2;
	seg3 = lpfc_ncmd->ts_isr_cmpl - lpfc_ncmd->ts_cmd_start;
	if (segsum > seg3)
		return;
	seg3 -= segsum;
	segsum += seg3;

	seg4 = lpfc_ncmd->ts_data_nvme - lpfc_ncmd->ts_cmd_start;
	if (segsum > seg4)
		return;
	seg4 -= segsum;

	phba->ktime_data_samples++;
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

	lpfc_ncmd->ts_last_cmd = 0;
	lpfc_ncmd->ts_cmd_start = 0;
	lpfc_ncmd->ts_cmd_wqput  = 0;
	lpfc_ncmd->ts_isr_cmpl = 0;
	lpfc_ncmd->ts_data_nvme = 0;
}
#endif

/**
 * lpfc_nvme_io_cmd_wqe_cmpl - Complete an NVME-over-FCP IO
 * @lpfc_pnvme: Pointer to the driver's nvme instance data
 * @lpfc_nvme_lport: Pointer to the driver's local port data
 * @lpfc_nvme_rport: Pointer to the rport getting the @lpfc_nvme_ereq
 *
 * Driver registers this routine as it io request handler.  This
 * routine issues an fcp WQE with data from the @lpfc_nvme_fcpreq
 * data structure to the rport indicated in @lpfc_nvme_rport.
 *
 * Return value :
 *   0 - Success
 *   TODO: What are the failure codes.
 **/
static void
lpfc_nvme_io_cmd_wqe_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *pwqeIn,
			  struct lpfc_wcqe_complete *wcqe)
{
	struct lpfc_io_buf *lpfc_ncmd =
		(struct lpfc_io_buf *)pwqeIn->context1;
	struct lpfc_vport *vport = pwqeIn->vport;
	struct nvmefc_fcp_req *nCmd;
	struct nvme_fc_ersp_iu *ep;
	struct nvme_fc_cmd_iu *cp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_nvme_fcpreq_priv *freqpriv;
	struct lpfc_nvme_lport *lport;
	uint32_t code, status, idx;
	uint16_t cid, sqhd, data;
	uint32_t *ptr;

	/* Sanity check on return of outstanding command */
	if (!lpfc_ncmd) {
		lpfc_printf_vlog(vport, KERN_ERR,
				 LOG_NODE | LOG_NVME_IOERR,
				 "6071 Null lpfc_ncmd pointer. No "
				 "release, skip completion\n");
		return;
	}

	/* Guard against abort handler being called at same time */
	spin_lock(&lpfc_ncmd->buf_lock);

	if (!lpfc_ncmd->nvmeCmd) {
		spin_unlock(&lpfc_ncmd->buf_lock);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NODE | LOG_NVME_IOERR,
				 "6066 Missing cmpl ptrs: lpfc_ncmd %p, "
				 "nvmeCmd %p\n",
				 lpfc_ncmd, lpfc_ncmd->nvmeCmd);

		/* Release the lpfc_ncmd regardless of the missing elements. */
		lpfc_release_nvme_buf(phba, lpfc_ncmd);
		return;
	}
	nCmd = lpfc_ncmd->nvmeCmd;
	status = bf_get(lpfc_wcqe_c_status, wcqe);

	idx = lpfc_ncmd->cur_iocbq.hba_wqidx;
	phba->sli4_hba.hdwq[idx].nvme_cstat.io_cmpls++;

	if (vport->localport) {
		lport = (struct lpfc_nvme_lport *)vport->localport->private;
		if (lport && status) {
			if (bf_get(lpfc_wcqe_c_xb, wcqe))
				atomic_inc(&lport->cmpl_fcp_xb);
			atomic_inc(&lport->cmpl_fcp_err);
		}
	}

	lpfc_nvmeio_data(phba, "NVME FCP CMPL: xri x%x stat x%x parm x%x\n",
			 lpfc_ncmd->cur_iocbq.sli4_xritag,
			 status, wcqe->parameter);
	/*
	 * Catch race where our node has transitioned, but the
	 * transport is still transitioning.
	 */
	ndlp = lpfc_ncmd->ndlp;
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_IOERR,
				 "6062 Ignoring NVME cmpl.  No ndlp\n");
		goto out_err;
	}

	code = bf_get(lpfc_wcqe_c_code, wcqe);
	if (code == CQE_CODE_NVME_ERSP) {
		/* For this type of CQE, we need to rebuild the rsp */
		ep = (struct nvme_fc_ersp_iu *)nCmd->rspaddr;

		/*
		 * Get Command Id from cmd to plug into response. This
		 * code is not needed in the next NVME Transport drop.
		 */
		cp = (struct nvme_fc_cmd_iu *)nCmd->cmdaddr;
		cid = cp->sqe.common.command_id;

		/*
		 * RSN is in CQE word 2
		 * SQHD is in CQE Word 3 bits 15:0
		 * Cmd Specific info is in CQE Word 1
		 * and in CQE Word 0 bits 15:0
		 */
		sqhd = bf_get(lpfc_wcqe_c_sqhead, wcqe);

		/* Now lets build the NVME ERSP IU */
		ep->iu_len = cpu_to_be16(8);
		ep->rsn = wcqe->parameter;
		ep->xfrd_len = cpu_to_be32(nCmd->payload_length);
		ep->rsvd12 = 0;
		ptr = (uint32_t *)&ep->cqe.result.u64;
		*ptr++ = wcqe->total_data_placed;
		data = bf_get(lpfc_wcqe_c_ersp0, wcqe);
		*ptr = (uint32_t)data;
		ep->cqe.sq_head = sqhd;
		ep->cqe.sq_id =  nCmd->sqid;
		ep->cqe.command_id = cid;
		ep->cqe.status = 0;

		lpfc_ncmd->status = IOSTAT_SUCCESS;
		lpfc_ncmd->result = 0;
		nCmd->rcv_rsplen = LPFC_NVME_ERSP_LEN;
		nCmd->transferred_length = nCmd->payload_length;
	} else {
		lpfc_ncmd->status = (status & LPFC_IOCB_STATUS_MASK);
		lpfc_ncmd->result = (wcqe->parameter & IOERR_PARAM_MASK);

		/* For NVME, the only failure path that results in an
		 * IO error is when the adapter rejects it.  All other
		 * conditions are a success case and resolved by the
		 * transport.
		 * IOSTAT_FCP_RSP_ERROR means:
		 * 1. Length of data received doesn't match total
		 *    transfer length in WQE
		 * 2. If the RSP payload does NOT match these cases:
		 *    a. RSP length 12/24 bytes and all zeros
		 *    b. NVME ERSP
		 */
		switch (lpfc_ncmd->status) {
		case IOSTAT_SUCCESS:
			nCmd->transferred_length = wcqe->total_data_placed;
			nCmd->rcv_rsplen = 0;
			nCmd->status = 0;
			break;
		case IOSTAT_FCP_RSP_ERROR:
			nCmd->transferred_length = wcqe->total_data_placed;
			nCmd->rcv_rsplen = wcqe->parameter;
			nCmd->status = 0;
			/* Sanity check */
			if (nCmd->rcv_rsplen == LPFC_NVME_ERSP_LEN)
				break;
			lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_IOERR,
					 "6081 NVME Completion Protocol Error: "
					 "xri %x status x%x result x%x "
					 "placed x%x\n",
					 lpfc_ncmd->cur_iocbq.sli4_xritag,
					 lpfc_ncmd->status, lpfc_ncmd->result,
					 wcqe->total_data_placed);
			break;
		case IOSTAT_LOCAL_REJECT:
			/* Let fall through to set command final state. */
			if (lpfc_ncmd->result == IOERR_ABORT_REQUESTED)
				lpfc_printf_vlog(vport, KERN_INFO,
					 LOG_NVME_IOERR,
					 "6032 Delay Aborted cmd %p "
					 "nvme cmd %p, xri x%x, "
					 "xb %d\n",
					 lpfc_ncmd, nCmd,
					 lpfc_ncmd->cur_iocbq.sli4_xritag,
					 bf_get(lpfc_wcqe_c_xb, wcqe));
			/* fall through */
		default:
out_err:
			lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_IOERR,
					 "6072 NVME Completion Error: xri %x "
					 "status x%x result x%x [x%x] "
					 "placed x%x\n",
					 lpfc_ncmd->cur_iocbq.sli4_xritag,
					 lpfc_ncmd->status, lpfc_ncmd->result,
					 wcqe->parameter,
					 wcqe->total_data_placed);
			nCmd->transferred_length = 0;
			nCmd->rcv_rsplen = 0;
			nCmd->status = NVME_SC_INTERNAL;
		}
	}

	/* pick up SLI4 exhange busy condition */
	if (bf_get(lpfc_wcqe_c_xb, wcqe))
		lpfc_ncmd->flags |= LPFC_SBUF_XBUSY;
	else
		lpfc_ncmd->flags &= ~LPFC_SBUF_XBUSY;

	/* Update stats and complete the IO.  There is
	 * no need for dma unprep because the nvme_transport
	 * owns the dma address.
	 */
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (lpfc_ncmd->ts_cmd_start) {
		lpfc_ncmd->ts_isr_cmpl = pwqeIn->isr_timestamp;
		lpfc_ncmd->ts_data_nvme = ktime_get_ns();
		phba->ktime_last_cmd = lpfc_ncmd->ts_data_nvme;
		lpfc_nvme_ktime(phba, lpfc_ncmd);
	}
	if (phba->cpucheck_on & LPFC_CHECK_NVME_IO) {
		uint32_t cpu;
		idx = lpfc_ncmd->cur_iocbq.hba_wqidx;
		cpu = raw_smp_processor_id();
		if (cpu < LPFC_CHECK_CPU_CNT) {
			if (lpfc_ncmd->cpu != cpu)
				lpfc_printf_vlog(vport,
						 KERN_INFO, LOG_NVME_IOERR,
						 "6701 CPU Check cmpl: "
						 "cpu %d expect %d\n",
						 cpu, lpfc_ncmd->cpu);
			phba->sli4_hba.hdwq[idx].cpucheck_cmpl_io[cpu]++;
		}
	}
#endif

	/* NVME targets need completion held off until the abort exchange
	 * completes unless the NVME Rport is getting unregistered.
	 */

	if (!(lpfc_ncmd->flags & LPFC_SBUF_XBUSY)) {
		freqpriv = nCmd->private;
		freqpriv->nvme_buf = NULL;
		lpfc_ncmd->nvmeCmd = NULL;
		spin_unlock(&lpfc_ncmd->buf_lock);
		nCmd->done(nCmd);
	} else
		spin_unlock(&lpfc_ncmd->buf_lock);

	/* Call release with XB=1 to queue the IO into the abort list. */
	lpfc_release_nvme_buf(phba, lpfc_ncmd);
}


/**
 * lpfc_nvme_prep_io_cmd - Issue an NVME-over-FCP IO
 * @lpfc_pnvme: Pointer to the driver's nvme instance data
 * @lpfc_nvme_lport: Pointer to the driver's local port data
 * @lpfc_nvme_rport: Pointer to the rport getting the @lpfc_nvme_ereq
 * @lpfc_nvme_fcreq: IO request from nvme fc to driver.
 * @hw_queue_handle: Driver-returned handle in lpfc_nvme_create_queue
 *
 * Driver registers this routine as it io request handler.  This
 * routine issues an fcp WQE with data from the @lpfc_nvme_fcpreq
 * data structure to the rport indicated in @lpfc_nvme_rport.
 *
 * Return value :
 *   0 - Success
 *   TODO: What are the failure codes.
 **/
static int
lpfc_nvme_prep_io_cmd(struct lpfc_vport *vport,
		      struct lpfc_io_buf *lpfc_ncmd,
		      struct lpfc_nodelist *pnode,
		      struct lpfc_fc4_ctrl_stat *cstat)
{
	struct lpfc_hba *phba = vport->phba;
	struct nvmefc_fcp_req *nCmd = lpfc_ncmd->nvmeCmd;
	struct lpfc_iocbq *pwqeq = &(lpfc_ncmd->cur_iocbq);
	union lpfc_wqe128 *wqe = &pwqeq->wqe;
	uint32_t req_len;

	if (!NLP_CHK_NODE_ACT(pnode))
		return -EINVAL;

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.
	 */
	if (nCmd->sg_cnt) {
		if (nCmd->io_dir == NVMEFC_FCP_WRITE) {
			/* From the iwrite template, initialize words 7 - 11 */
			memcpy(&wqe->words[7],
			       &lpfc_iwrite_cmd_template.words[7],
			       sizeof(uint32_t) * 5);

			/* Word 4 */
			wqe->fcp_iwrite.total_xfer_len = nCmd->payload_length;

			/* Word 5 */
			if ((phba->cfg_nvme_enable_fb) &&
			    (pnode->nlp_flag & NLP_FIRSTBURST)) {
				req_len = lpfc_ncmd->nvmeCmd->payload_length;
				if (req_len < pnode->nvme_fb_size)
					wqe->fcp_iwrite.initial_xfer_len =
						req_len;
				else
					wqe->fcp_iwrite.initial_xfer_len =
						pnode->nvme_fb_size;
			} else {
				wqe->fcp_iwrite.initial_xfer_len = 0;
			}
			cstat->output_requests++;
		} else {
			/* From the iread template, initialize words 7 - 11 */
			memcpy(&wqe->words[7],
			       &lpfc_iread_cmd_template.words[7],
			       sizeof(uint32_t) * 5);

			/* Word 4 */
			wqe->fcp_iread.total_xfer_len = nCmd->payload_length;

			/* Word 5 */
			wqe->fcp_iread.rsrvd5 = 0;

			cstat->input_requests++;
		}
	} else {
		/* From the icmnd template, initialize words 4 - 11 */
		memcpy(&wqe->words[4], &lpfc_icmnd_cmd_template.words[4],
		       sizeof(uint32_t) * 8);
		cstat->control_requests++;
	}
	/*
	 * Finish initializing those WQE fields that are independent
	 * of the nvme_cmnd request_buffer
	 */

	/* Word 3 */
	bf_set(payload_offset_len, &wqe->fcp_icmd,
	       (nCmd->rsplen + nCmd->cmdlen));

	/* Word 6 */
	bf_set(wqe_ctxt_tag, &wqe->generic.wqe_com,
	       phba->sli4_hba.rpi_ids[pnode->nlp_rpi]);
	bf_set(wqe_xri_tag, &wqe->generic.wqe_com, pwqeq->sli4_xritag);

	/* Word 8 */
	wqe->generic.wqe_com.abort_tag = pwqeq->iotag;

	/* Word 9 */
	bf_set(wqe_reqtag, &wqe->generic.wqe_com, pwqeq->iotag);

	/* Words 13 14 15 are for PBDE support */

	pwqeq->vport = vport;
	return 0;
}


/**
 * lpfc_nvme_prep_io_dma - Issue an NVME-over-FCP IO
 * @lpfc_pnvme: Pointer to the driver's nvme instance data
 * @lpfc_nvme_lport: Pointer to the driver's local port data
 * @lpfc_nvme_rport: Pointer to the rport getting the @lpfc_nvme_ereq
 * @lpfc_nvme_fcreq: IO request from nvme fc to driver.
 * @hw_queue_handle: Driver-returned handle in lpfc_nvme_create_queue
 *
 * Driver registers this routine as it io request handler.  This
 * routine issues an fcp WQE with data from the @lpfc_nvme_fcpreq
 * data structure to the rport indicated in @lpfc_nvme_rport.
 *
 * Return value :
 *   0 - Success
 *   TODO: What are the failure codes.
 **/
static int
lpfc_nvme_prep_io_dma(struct lpfc_vport *vport,
		      struct lpfc_io_buf *lpfc_ncmd)
{
	struct lpfc_hba *phba = vport->phba;
	struct nvmefc_fcp_req *nCmd = lpfc_ncmd->nvmeCmd;
	union lpfc_wqe128 *wqe = &lpfc_ncmd->cur_iocbq.wqe;
	struct sli4_sge *sgl = lpfc_ncmd->dma_sgl;
	struct scatterlist *data_sg;
	struct sli4_sge *first_data_sgl;
	struct ulp_bde64 *bde;
	dma_addr_t physaddr;
	uint32_t num_bde = 0;
	uint32_t dma_len;
	uint32_t dma_offset = 0;
	int nseg, i;

	/* Fix up the command and response DMA stuff. */
	lpfc_nvme_adj_fcp_sgls(vport, lpfc_ncmd, nCmd);

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.
	 */
	if (nCmd->sg_cnt) {
		/*
		 * Jump over the cmd and rsp SGEs.  The fix routine
		 * has already adjusted for this.
		 */
		sgl += 2;

		first_data_sgl = sgl;
		lpfc_ncmd->seg_cnt = nCmd->sg_cnt;
		if (lpfc_ncmd->seg_cnt > lpfc_nvme_template.max_sgl_segments) {
			lpfc_printf_log(phba, KERN_ERR, LOG_NVME_IOERR,
					"6058 Too many sg segments from "
					"NVME Transport.  Max %d, "
					"nvmeIO sg_cnt %d\n",
					phba->cfg_nvme_seg_cnt + 1,
					lpfc_ncmd->seg_cnt);
			lpfc_ncmd->seg_cnt = 0;
			return 1;
		}

		/*
		 * The driver established a maximum scatter-gather segment count
		 * during probe that limits the number of sg elements in any
		 * single nvme command.  Just run through the seg_cnt and format
		 * the sge's.
		 */
		nseg = nCmd->sg_cnt;
		data_sg = nCmd->first_sgl;
		for (i = 0; i < nseg; i++) {
			if (data_sg == NULL) {
				lpfc_printf_log(phba, KERN_ERR, LOG_NVME_IOERR,
						"6059 dptr err %d, nseg %d\n",
						i, nseg);
				lpfc_ncmd->seg_cnt = 0;
				return 1;
			}
			physaddr = data_sg->dma_address;
			dma_len = data_sg->length;
			sgl->addr_lo = cpu_to_le32(putPaddrLow(physaddr));
			sgl->addr_hi = cpu_to_le32(putPaddrHigh(physaddr));
			sgl->word2 = le32_to_cpu(sgl->word2);
			if ((num_bde + 1) == nseg)
				bf_set(lpfc_sli4_sge_last, sgl, 1);
			else
				bf_set(lpfc_sli4_sge_last, sgl, 0);
			bf_set(lpfc_sli4_sge_offset, sgl, dma_offset);
			bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DATA);
			sgl->word2 = cpu_to_le32(sgl->word2);
			sgl->sge_len = cpu_to_le32(dma_len);

			dma_offset += dma_len;
			data_sg = sg_next(data_sg);
			sgl++;
		}
		if (phba->cfg_enable_pbde) {
			/* Use PBDE support for first SGL only, offset == 0 */
			/* Words 13-15 */
			bde = (struct ulp_bde64 *)
				&wqe->words[13];
			bde->addrLow = first_data_sgl->addr_lo;
			bde->addrHigh = first_data_sgl->addr_hi;
			bde->tus.f.bdeSize =
				le32_to_cpu(first_data_sgl->sge_len);
			bde->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
			bde->tus.w = cpu_to_le32(bde->tus.w);
			/* wqe_pbde is 1 in template */
		} else {
			memset(&wqe->words[13], 0, (sizeof(uint32_t) * 3));
			bf_set(wqe_pbde, &wqe->generic.wqe_com, 0);
		}

	} else {
		lpfc_ncmd->seg_cnt = 0;

		/* For this clause to be valid, the payload_length
		 * and sg_cnt must zero.
		 */
		if (nCmd->payload_length != 0) {
			lpfc_printf_log(phba, KERN_ERR, LOG_NVME_IOERR,
					"6063 NVME DMA Prep Err: sg_cnt %d "
					"payload_length x%x\n",
					nCmd->sg_cnt, nCmd->payload_length);
			return 1;
		}
	}
	return 0;
}

/**
 * lpfc_nvme_fcp_io_submit - Issue an NVME-over-FCP IO
 * @lpfc_pnvme: Pointer to the driver's nvme instance data
 * @lpfc_nvme_lport: Pointer to the driver's local port data
 * @lpfc_nvme_rport: Pointer to the rport getting the @lpfc_nvme_ereq
 * @lpfc_nvme_fcreq: IO request from nvme fc to driver.
 * @hw_queue_handle: Driver-returned handle in lpfc_nvme_create_queue
 *
 * Driver registers this routine as it io request handler.  This
 * routine issues an fcp WQE with data from the @lpfc_nvme_fcpreq
 * data structure to the rport
 indicated in @lpfc_nvme_rport.
 *
 * Return value :
 *   0 - Success
 *   TODO: What are the failure codes.
 **/
static int
lpfc_nvme_fcp_io_submit(struct nvme_fc_local_port *pnvme_lport,
			struct nvme_fc_remote_port *pnvme_rport,
			void *hw_queue_handle,
			struct nvmefc_fcp_req *pnvme_fcreq)
{
	int ret = 0;
	int expedite = 0;
	int idx, cpu;
	struct lpfc_nvme_lport *lport;
	struct lpfc_fc4_ctrl_stat *cstat;
	struct lpfc_vport *vport;
	struct lpfc_hba *phba;
	struct lpfc_nodelist *ndlp;
	struct lpfc_io_buf *lpfc_ncmd;
	struct lpfc_nvme_rport *rport;
	struct lpfc_nvme_qhandle *lpfc_queue_info;
	struct lpfc_nvme_fcpreq_priv *freqpriv;
	struct nvme_common_command *sqe;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint64_t start = 0;
#endif

	/* Validate pointers. LLDD fault handling with transport does
	 * have timing races.
	 */
	lport = (struct lpfc_nvme_lport *)pnvme_lport->private;
	if (unlikely(!lport)) {
		ret = -EINVAL;
		goto out_fail;
	}

	vport = lport->vport;

	if (unlikely(!hw_queue_handle)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_IOERR,
				 "6117 Fail IO, NULL hw_queue_handle\n");
		atomic_inc(&lport->xmt_fcp_err);
		ret = -EBUSY;
		goto out_fail;
	}

	phba = vport->phba;

	if (vport->load_flag & FC_UNLOADING) {
		ret = -ENODEV;
		goto out_fail;
	}

	if (vport->load_flag & FC_UNLOADING) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_IOERR,
				 "6124 Fail IO, Driver unload\n");
		atomic_inc(&lport->xmt_fcp_err);
		ret = -ENODEV;
		goto out_fail;
	}

	freqpriv = pnvme_fcreq->private;
	if (unlikely(!freqpriv)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_IOERR,
				 "6158 Fail IO, NULL request data\n");
		atomic_inc(&lport->xmt_fcp_err);
		ret = -EINVAL;
		goto out_fail;
	}

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (phba->ktime_on)
		start = ktime_get_ns();
#endif
	rport = (struct lpfc_nvme_rport *)pnvme_rport->private;
	lpfc_queue_info = (struct lpfc_nvme_qhandle *)hw_queue_handle;

	/*
	 * Catch race where our node has transitioned, but the
	 * transport is still transitioning.
	 */
	ndlp = rport->ndlp;
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE | LOG_NVME_IOERR,
				 "6053 Fail IO, ndlp not ready: rport %p "
				  "ndlp %p, DID x%06x\n",
				 rport, ndlp, pnvme_rport->port_id);
		atomic_inc(&lport->xmt_fcp_err);
		ret = -EBUSY;
		goto out_fail;
	}

	/* The remote node has to be a mapped target or it's an error. */
	if ((ndlp->nlp_type & NLP_NVME_TARGET) &&
	    (ndlp->nlp_state != NLP_STE_MAPPED_NODE)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NODE | LOG_NVME_IOERR,
				 "6036 Fail IO, DID x%06x not ready for "
				 "IO. State x%x, Type x%x Flg x%x\n",
				 pnvme_rport->port_id,
				 ndlp->nlp_state, ndlp->nlp_type,
				 ndlp->upcall_flags);
		atomic_inc(&lport->xmt_fcp_bad_ndlp);
		ret = -EBUSY;
		goto out_fail;

	}

	/* Currently only NVME Keep alive commands should be expedited
	 * if the driver runs out of a resource. These should only be
	 * issued on the admin queue, qidx 0
	 */
	if (!lpfc_queue_info->qidx && !pnvme_fcreq->sg_cnt) {
		sqe = &((struct nvme_fc_cmd_iu *)
			pnvme_fcreq->cmdaddr)->sqe.common;
		if (sqe->opcode == nvme_admin_keep_alive)
			expedite = 1;
	}

	/* The node is shared with FCP IO, make sure the IO pending count does
	 * not exceed the programmed depth.
	 */
	if (lpfc_ndlp_check_qdepth(phba, ndlp)) {
		if ((atomic_read(&ndlp->cmd_pending) >= ndlp->cmd_qdepth) &&
		    !expedite) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_IOERR,
					 "6174 Fail IO, ndlp qdepth exceeded: "
					 "idx %d DID %x pend %d qdepth %d\n",
					 lpfc_queue_info->index, ndlp->nlp_DID,
					 atomic_read(&ndlp->cmd_pending),
					 ndlp->cmd_qdepth);
			atomic_inc(&lport->xmt_fcp_qdepth);
			ret = -EBUSY;
			goto out_fail;
		}
	}

	/* Lookup Hardware Queue index based on fcp_io_sched module parameter */
	if (phba->cfg_fcp_io_sched == LPFC_FCP_SCHED_BY_HDWQ) {
		idx = lpfc_queue_info->index;
	} else {
		cpu = raw_smp_processor_id();
		idx = phba->sli4_hba.cpu_map[cpu].hdwq;
	}

	lpfc_ncmd = lpfc_get_nvme_buf(phba, ndlp, idx, expedite);
	if (lpfc_ncmd == NULL) {
		atomic_inc(&lport->xmt_fcp_noxri);
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_IOERR,
				 "6065 Fail IO, driver buffer pool is empty: "
				 "idx %d DID %x\n",
				 lpfc_queue_info->index, ndlp->nlp_DID);
		ret = -EBUSY;
		goto out_fail;
	}
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (start) {
		lpfc_ncmd->ts_cmd_start = start;
		lpfc_ncmd->ts_last_cmd = phba->ktime_last_cmd;
	} else {
		lpfc_ncmd->ts_cmd_start = 0;
	}
#endif

	/*
	 * Store the data needed by the driver to issue, abort, and complete
	 * an IO.
	 * Do not let the IO hang out forever.  There is no midlayer issuing
	 * an abort so inform the FW of the maximum IO pending time.
	 */
	freqpriv->nvme_buf = lpfc_ncmd;
	lpfc_ncmd->nvmeCmd = pnvme_fcreq;
	lpfc_ncmd->ndlp = ndlp;
	lpfc_ncmd->qidx = lpfc_queue_info->qidx;

	/*
	 * Issue the IO on the WQ indicated by index in the hw_queue_handle.
	 * This identfier was create in our hardware queue create callback
	 * routine. The driver now is dependent on the IO queue steering from
	 * the transport.  We are trusting the upper NVME layers know which
	 * index to use and that they have affinitized a CPU to this hardware
	 * queue. A hardware queue maps to a driver MSI-X vector/EQ/CQ/WQ.
	 */
	lpfc_ncmd->cur_iocbq.hba_wqidx = idx;
	cstat = &phba->sli4_hba.hdwq[idx].nvme_cstat;

	lpfc_nvme_prep_io_cmd(vport, lpfc_ncmd, ndlp, cstat);
	ret = lpfc_nvme_prep_io_dma(vport, lpfc_ncmd);
	if (ret) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_IOERR,
				 "6175 Fail IO, Prep DMA: "
				 "idx %d DID %x\n",
				 lpfc_queue_info->index, ndlp->nlp_DID);
		atomic_inc(&lport->xmt_fcp_err);
		ret = -ENOMEM;
		goto out_free_nvme_buf;
	}

	lpfc_nvmeio_data(phba, "NVME FCP XMIT: xri x%x idx %d to %06x\n",
			 lpfc_ncmd->cur_iocbq.sli4_xritag,
			 lpfc_queue_info->index, ndlp->nlp_DID);

	ret = lpfc_sli4_issue_wqe(phba, lpfc_ncmd->hdwq, &lpfc_ncmd->cur_iocbq);
	if (ret) {
		atomic_inc(&lport->xmt_fcp_wqerr);
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_IOERR,
				 "6113 Fail IO, Could not issue WQE err %x "
				 "sid: x%x did: x%x oxid: x%x\n",
				 ret, vport->fc_myDID, ndlp->nlp_DID,
				 lpfc_ncmd->cur_iocbq.sli4_xritag);
		goto out_free_nvme_buf;
	}

	if (phba->cfg_xri_rebalancing)
		lpfc_keep_pvt_pool_above_lowwm(phba, lpfc_ncmd->hdwq_no);

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (lpfc_ncmd->ts_cmd_start)
		lpfc_ncmd->ts_cmd_wqput = ktime_get_ns();

	if (phba->cpucheck_on & LPFC_CHECK_NVME_IO) {
		cpu = raw_smp_processor_id();
		if (cpu < LPFC_CHECK_CPU_CNT) {
			lpfc_ncmd->cpu = cpu;
			if (idx != cpu)
				lpfc_printf_vlog(vport,
						 KERN_INFO, LOG_NVME_IOERR,
						"6702 CPU Check cmd: "
						"cpu %d wq %d\n",
						lpfc_ncmd->cpu,
						lpfc_queue_info->index);
			phba->sli4_hba.hdwq[idx].cpucheck_xmt_io[cpu]++;
		}
	}
#endif
	return 0;

 out_free_nvme_buf:
	if (lpfc_ncmd->nvmeCmd->sg_cnt) {
		if (lpfc_ncmd->nvmeCmd->io_dir == NVMEFC_FCP_WRITE)
			cstat->output_requests--;
		else
			cstat->input_requests--;
	} else
		cstat->control_requests--;
	lpfc_release_nvme_buf(phba, lpfc_ncmd);
 out_fail:
	return ret;
}

/**
 * lpfc_nvme_abort_fcreq_cmpl - Complete an NVME FCP abort request.
 * @phba: Pointer to HBA context object
 * @cmdiocb: Pointer to command iocb object.
 * @rspiocb: Pointer to response iocb object.
 *
 * This is the callback function for any NVME FCP IO that was aborted.
 *
 * Return value:
 *   None
 **/
void
lpfc_nvme_abort_fcreq_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			   struct lpfc_wcqe_complete *abts_cmpl)
{
	lpfc_printf_log(phba, KERN_INFO, LOG_NVME,
			"6145 ABORT_XRI_CN completing on rpi x%x "
			"original iotag x%x, abort cmd iotag x%x "
			"req_tag x%x, status x%x, hwstatus x%x\n",
			cmdiocb->iocb.un.acxri.abortContextTag,
			cmdiocb->iocb.un.acxri.abortIoTag,
			cmdiocb->iotag,
			bf_get(lpfc_wcqe_c_request_tag, abts_cmpl),
			bf_get(lpfc_wcqe_c_status, abts_cmpl),
			bf_get(lpfc_wcqe_c_hw_status, abts_cmpl));
	lpfc_sli_release_iocbq(phba, cmdiocb);
}

/**
 * lpfc_nvme_fcp_abort - Issue an NVME-over-FCP ABTS
 * @lpfc_pnvme: Pointer to the driver's nvme instance data
 * @lpfc_nvme_lport: Pointer to the driver's local port data
 * @lpfc_nvme_rport: Pointer to the rport getting the @lpfc_nvme_ereq
 * @lpfc_nvme_fcreq: IO request from nvme fc to driver.
 * @hw_queue_handle: Driver-returned handle in lpfc_nvme_create_queue
 *
 * Driver registers this routine as its nvme request io abort handler.  This
 * routine issues an fcp Abort WQE with data from the @lpfc_nvme_fcpreq
 * data structure to the rport indicated in @lpfc_nvme_rport.  This routine
 * is executed asynchronously - one the target is validated as "MAPPED" and
 * ready for IO, the driver issues the abort request and returns.
 *
 * Return value:
 *   None
 **/
static void
lpfc_nvme_fcp_abort(struct nvme_fc_local_port *pnvme_lport,
		    struct nvme_fc_remote_port *pnvme_rport,
		    void *hw_queue_handle,
		    struct nvmefc_fcp_req *pnvme_fcreq)
{
	struct lpfc_nvme_lport *lport;
	struct lpfc_vport *vport;
	struct lpfc_hba *phba;
	struct lpfc_io_buf *lpfc_nbuf;
	struct lpfc_iocbq *abts_buf;
	struct lpfc_iocbq *nvmereq_wqe;
	struct lpfc_nvme_fcpreq_priv *freqpriv;
	union lpfc_wqe128 *abts_wqe;
	unsigned long flags;
	int ret_val;

	/* Validate pointers. LLDD fault handling with transport does
	 * have timing races.
	 */
	lport = (struct lpfc_nvme_lport *)pnvme_lport->private;
	if (unlikely(!lport))
		return;

	vport = lport->vport;

	if (unlikely(!hw_queue_handle)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_ABTS,
				 "6129 Fail Abort, HW Queue Handle NULL.\n");
		return;
	}

	phba = vport->phba;
	freqpriv = pnvme_fcreq->private;

	if (unlikely(!freqpriv))
		return;
	if (vport->load_flag & FC_UNLOADING)
		return;

	/* Announce entry to new IO submit field. */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_ABTS,
			 "6002 Abort Request to rport DID x%06x "
			 "for nvme_fc_req %p\n",
			 pnvme_rport->port_id,
			 pnvme_fcreq);

	/* If the hba is getting reset, this flag is set.  It is
	 * cleared when the reset is complete and rings reestablished.
	 */
	spin_lock_irqsave(&phba->hbalock, flags);
	/* driver queued commands are in process of being flushed */
	if (phba->hba_flag & HBA_NVME_IOQ_FLUSH) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_ABTS,
				 "6139 Driver in reset cleanup - flushing "
				 "NVME Req now.  hba_flag x%x\n",
				 phba->hba_flag);
		return;
	}

	lpfc_nbuf = freqpriv->nvme_buf;
	if (!lpfc_nbuf) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_ABTS,
				 "6140 NVME IO req has no matching lpfc nvme "
				 "io buffer.  Skipping abort req.\n");
		return;
	} else if (!lpfc_nbuf->nvmeCmd) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_ABTS,
				 "6141 lpfc NVME IO req has no nvme_fcreq "
				 "io buffer.  Skipping abort req.\n");
		return;
	}
	nvmereq_wqe = &lpfc_nbuf->cur_iocbq;

	/* Guard against IO completion being called at same time */
	spin_lock(&lpfc_nbuf->buf_lock);

	/*
	 * The lpfc_nbuf and the mapped nvme_fcreq in the driver's
	 * state must match the nvme_fcreq passed by the nvme
	 * transport.  If they don't match, it is likely the driver
	 * has already completed the NVME IO and the nvme transport
	 * has not seen it yet.
	 */
	if (lpfc_nbuf->nvmeCmd != pnvme_fcreq) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_ABTS,
				 "6143 NVME req mismatch: "
				 "lpfc_nbuf %p nvmeCmd %p, "
				 "pnvme_fcreq %p.  Skipping Abort xri x%x\n",
				 lpfc_nbuf, lpfc_nbuf->nvmeCmd,
				 pnvme_fcreq, nvmereq_wqe->sli4_xritag);
		goto out_unlock;
	}

	/* Don't abort IOs no longer on the pending queue. */
	if (!(nvmereq_wqe->iocb_flag & LPFC_IO_ON_TXCMPLQ)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_ABTS,
				 "6142 NVME IO req %p not queued - skipping "
				 "abort req xri x%x\n",
				 pnvme_fcreq, nvmereq_wqe->sli4_xritag);
		goto out_unlock;
	}

	atomic_inc(&lport->xmt_fcp_abort);
	lpfc_nvmeio_data(phba, "NVME FCP ABORT: xri x%x idx %d to %06x\n",
			 nvmereq_wqe->sli4_xritag,
			 nvmereq_wqe->hba_wqidx, pnvme_rport->port_id);

	/* Outstanding abort is in progress */
	if (nvmereq_wqe->iocb_flag & LPFC_DRIVER_ABORTED) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_ABTS,
				 "6144 Outstanding NVME I/O Abort Request "
				 "still pending on nvme_fcreq %p, "
				 "lpfc_ncmd %p xri x%x\n",
				 pnvme_fcreq, lpfc_nbuf,
				 nvmereq_wqe->sli4_xritag);
		goto out_unlock;
	}

	abts_buf = __lpfc_sli_get_iocbq(phba);
	if (!abts_buf) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_ABTS,
				 "6136 No available abort wqes. Skipping "
				 "Abts req for nvme_fcreq %p xri x%x\n",
				 pnvme_fcreq, nvmereq_wqe->sli4_xritag);
		goto out_unlock;
	}

	/* Ready - mark outstanding as aborted by driver. */
	nvmereq_wqe->iocb_flag |= LPFC_DRIVER_ABORTED;

	/* Complete prepping the abort wqe and issue to the FW. */
	abts_wqe = &abts_buf->wqe;

	/* WQEs are reused.  Clear stale data and set key fields to
	 * zero like ia, iaab, iaar, xri_tag, and ctxt_tag.
	 */
	memset(abts_wqe, 0, sizeof(union lpfc_wqe));
	bf_set(abort_cmd_criteria, &abts_wqe->abort_cmd, T_XRI_TAG);

	/* word 7 */
	bf_set(wqe_cmnd, &abts_wqe->abort_cmd.wqe_com, CMD_ABORT_XRI_CX);
	bf_set(wqe_class, &abts_wqe->abort_cmd.wqe_com,
	       nvmereq_wqe->iocb.ulpClass);

	/* word 8 - tell the FW to abort the IO associated with this
	 * outstanding exchange ID.
	 */
	abts_wqe->abort_cmd.wqe_com.abort_tag = nvmereq_wqe->sli4_xritag;

	/* word 9 - this is the iotag for the abts_wqe completion. */
	bf_set(wqe_reqtag, &abts_wqe->abort_cmd.wqe_com,
	       abts_buf->iotag);

	/* word 10 */
	bf_set(wqe_qosd, &abts_wqe->abort_cmd.wqe_com, 1);
	bf_set(wqe_lenloc, &abts_wqe->abort_cmd.wqe_com, LPFC_WQE_LENLOC_NONE);

	/* word 11 */
	bf_set(wqe_cmd_type, &abts_wqe->abort_cmd.wqe_com, OTHER_COMMAND);
	bf_set(wqe_wqec, &abts_wqe->abort_cmd.wqe_com, 1);
	bf_set(wqe_cqid, &abts_wqe->abort_cmd.wqe_com, LPFC_WQE_CQ_ID_DEFAULT);

	/* ABTS WQE must go to the same WQ as the WQE to be aborted */
	abts_buf->iocb_flag |= LPFC_IO_NVME;
	abts_buf->hba_wqidx = nvmereq_wqe->hba_wqidx;
	abts_buf->vport = vport;
	abts_buf->wqe_cmpl = lpfc_nvme_abort_fcreq_cmpl;
	ret_val = lpfc_sli4_issue_wqe(phba, lpfc_nbuf->hdwq, abts_buf);
	spin_unlock(&lpfc_nbuf->buf_lock);
	spin_unlock_irqrestore(&phba->hbalock, flags);
	if (ret_val) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_ABTS,
				 "6137 Failed abts issue_wqe with status x%x "
				 "for nvme_fcreq %p.\n",
				 ret_val, pnvme_fcreq);
		lpfc_sli_release_iocbq(phba, abts_buf);
		return;
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_ABTS,
			 "6138 Transport Abort NVME Request Issued for "
			 "ox_id x%x on reqtag x%x\n",
			 nvmereq_wqe->sli4_xritag,
			 abts_buf->iotag);
	return;

out_unlock:
	spin_unlock(&lpfc_nbuf->buf_lock);
	spin_unlock_irqrestore(&phba->hbalock, flags);
	return;
}

/* Declare and initialization an instance of the FC NVME template. */
static struct nvme_fc_port_template lpfc_nvme_template = {
	/* initiator-based functions */
	.localport_delete  = lpfc_nvme_localport_delete,
	.remoteport_delete = lpfc_nvme_remoteport_delete,
	.create_queue = lpfc_nvme_create_queue,
	.delete_queue = lpfc_nvme_delete_queue,
	.ls_req       = lpfc_nvme_ls_req,
	.fcp_io       = lpfc_nvme_fcp_io_submit,
	.ls_abort     = lpfc_nvme_ls_abort,
	.fcp_abort    = lpfc_nvme_fcp_abort,

	.max_hw_queues = 1,
	.max_sgl_segments = LPFC_NVME_DEFAULT_SEGS,
	.max_dif_sgl_segments = LPFC_NVME_DEFAULT_SEGS,
	.dma_boundary = 0xFFFFFFFF,

	/* Sizes of additional private data for data structures.
	 * No use for the last two sizes at this time.
	 */
	.local_priv_sz = sizeof(struct lpfc_nvme_lport),
	.remote_priv_sz = sizeof(struct lpfc_nvme_rport),
	.lsrqst_priv_sz = 0,
	.fcprqst_priv_sz = sizeof(struct lpfc_nvme_fcpreq_priv),
};

/**
 * lpfc_get_nvme_buf - Get a nvme buffer from io_buf_list of the HBA
 * @phba: The HBA for which this call is being executed.
 *
 * This routine removes a nvme buffer from head of @hdwq io_buf_list
 * and returns to caller.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_nvme_buf - Success
 **/
static struct lpfc_io_buf *
lpfc_get_nvme_buf(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		  int idx, int expedite)
{
	struct lpfc_io_buf *lpfc_ncmd;
	struct lpfc_sli4_hdw_queue *qp;
	struct sli4_sge *sgl;
	struct lpfc_iocbq *pwqeq;
	union lpfc_wqe128 *wqe;

	lpfc_ncmd = lpfc_get_io_buf(phba, NULL, idx, expedite);

	if (lpfc_ncmd) {
		pwqeq = &(lpfc_ncmd->cur_iocbq);
		wqe = &pwqeq->wqe;

		/* Setup key fields in buffer that may have been changed
		 * if other protocols used this buffer.
		 */
		pwqeq->iocb_flag = LPFC_IO_NVME;
		pwqeq->wqe_cmpl = lpfc_nvme_io_cmd_wqe_cmpl;
		lpfc_ncmd->start_time = jiffies;
		lpfc_ncmd->flags = 0;

		/* Rsp SGE will be filled in when we rcv an IO
		 * from the NVME Layer to be sent.
		 * The cmd is going to be embedded so we need a SKIP SGE.
		 */
		sgl = lpfc_ncmd->dma_sgl;
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_SKIP);
		bf_set(lpfc_sli4_sge_last, sgl, 0);
		sgl->word2 = cpu_to_le32(sgl->word2);
		/* Fill in word 3 / sgl_len during cmd submission */

		/* Initialize WQE */
		memset(wqe, 0, sizeof(union lpfc_wqe));

		if (lpfc_ndlp_check_qdepth(phba, ndlp)) {
			atomic_inc(&ndlp->cmd_pending);
			lpfc_ncmd->flags |= LPFC_SBUF_BUMP_QDEPTH;
		}

	} else {
		qp = &phba->sli4_hba.hdwq[idx];
		qp->empty_io_bufs++;
	}

	return  lpfc_ncmd;
}

/**
 * lpfc_release_nvme_buf: Return a nvme buffer back to hba nvme buf list.
 * @phba: The Hba for which this call is being executed.
 * @lpfc_ncmd: The nvme buffer which is being released.
 *
 * This routine releases @lpfc_ncmd nvme buffer by adding it to tail of @phba
 * lpfc_io_buf_list list. For SLI4 XRI's are tied to the nvme buffer
 * and cannot be reused for at least RA_TOV amount of time if it was
 * aborted.
 **/
static void
lpfc_release_nvme_buf(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_ncmd)
{
	struct lpfc_sli4_hdw_queue *qp;
	unsigned long iflag = 0;

	if ((lpfc_ncmd->flags & LPFC_SBUF_BUMP_QDEPTH) && lpfc_ncmd->ndlp)
		atomic_dec(&lpfc_ncmd->ndlp->cmd_pending);

	lpfc_ncmd->ndlp = NULL;
	lpfc_ncmd->flags &= ~LPFC_SBUF_BUMP_QDEPTH;

	qp = lpfc_ncmd->hdwq;
	if (lpfc_ncmd->flags & LPFC_SBUF_XBUSY) {
		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
				"6310 XB release deferred for "
				"ox_id x%x on reqtag x%x\n",
				lpfc_ncmd->cur_iocbq.sli4_xritag,
				lpfc_ncmd->cur_iocbq.iotag);

		spin_lock_irqsave(&qp->abts_nvme_buf_list_lock, iflag);
		list_add_tail(&lpfc_ncmd->list,
			&qp->lpfc_abts_nvme_buf_list);
		qp->abts_nvme_io_bufs++;
		spin_unlock_irqrestore(&qp->abts_nvme_buf_list_lock, iflag);
	} else
		lpfc_release_io_buf(phba, (struct lpfc_io_buf *)lpfc_ncmd, qp);
}

/**
 * lpfc_nvme_create_localport - Create/Bind an nvme localport instance.
 * @pvport - the lpfc_vport instance requesting a localport.
 *
 * This routine is invoked to create an nvme localport instance to bind
 * to the nvme_fc_transport.  It is called once during driver load
 * like lpfc_create_shost after all other services are initialized.
 * It requires a vport, vpi, and wwns at call time.  Other localport
 * parameters are modified as the driver's FCID and the Fabric WWN
 * are established.
 *
 * Return codes
 *      0 - successful
 *      -ENOMEM - no heap memory available
 *      other values - from nvme registration upcall
 **/
int
lpfc_nvme_create_localport(struct lpfc_vport *vport)
{
	int ret = 0;
	struct lpfc_hba  *phba = vport->phba;
	struct nvme_fc_port_info nfcp_info;
	struct nvme_fc_local_port *localport;
	struct lpfc_nvme_lport *lport;

	/* Initialize this localport instance.  The vport wwn usage ensures
	 * that NPIV is accounted for.
	 */
	memset(&nfcp_info, 0, sizeof(struct nvme_fc_port_info));
	nfcp_info.port_role = FC_PORT_ROLE_NVME_INITIATOR;
	nfcp_info.node_name = wwn_to_u64(vport->fc_nodename.u.wwn);
	nfcp_info.port_name = wwn_to_u64(vport->fc_portname.u.wwn);

	/* We need to tell the transport layer + 1 because it takes page
	 * alignment into account. When space for the SGL is allocated we
	 * allocate + 3, one for cmd, one for rsp and one for this alignment
	 */
	lpfc_nvme_template.max_sgl_segments = phba->cfg_nvme_seg_cnt + 1;

	/* Advertise how many hw queues we support based on fcp_io_sched */
	if (phba->cfg_fcp_io_sched == LPFC_FCP_SCHED_BY_HDWQ)
		lpfc_nvme_template.max_hw_queues = phba->cfg_hdw_queue;
	else
		lpfc_nvme_template.max_hw_queues =
			phba->sli4_hba.num_present_cpu;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return ret;

	/* localport is allocated from the stack, but the registration
	 * call allocates heap memory as well as the private area.
	 */

	ret = nvme_fc_register_localport(&nfcp_info, &lpfc_nvme_template,
					 &vport->phba->pcidev->dev, &localport);
	if (!ret) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME | LOG_NVME_DISC,
				 "6005 Successfully registered local "
				 "NVME port num %d, localP %p, private %p, "
				 "sg_seg %d\n",
				 localport->port_num, localport,
				 localport->private,
				 lpfc_nvme_template.max_sgl_segments);

		/* Private is our lport size declared in the template. */
		lport = (struct lpfc_nvme_lport *)localport->private;
		vport->localport = localport;
		lport->vport = vport;
		vport->nvmei_support = 1;

		atomic_set(&lport->xmt_fcp_noxri, 0);
		atomic_set(&lport->xmt_fcp_bad_ndlp, 0);
		atomic_set(&lport->xmt_fcp_qdepth, 0);
		atomic_set(&lport->xmt_fcp_err, 0);
		atomic_set(&lport->xmt_fcp_wqerr, 0);
		atomic_set(&lport->xmt_fcp_abort, 0);
		atomic_set(&lport->xmt_ls_abort, 0);
		atomic_set(&lport->xmt_ls_err, 0);
		atomic_set(&lport->cmpl_fcp_xb, 0);
		atomic_set(&lport->cmpl_fcp_err, 0);
		atomic_set(&lport->cmpl_ls_xb, 0);
		atomic_set(&lport->cmpl_ls_err, 0);
		atomic_set(&lport->fc4NvmeLsRequests, 0);
		atomic_set(&lport->fc4NvmeLsCmpls, 0);
	}

	return ret;
}

#if (IS_ENABLED(CONFIG_NVME_FC))
/* lpfc_nvme_lport_unreg_wait - Wait for the host to complete an lport unreg.
 *
 * The driver has to wait for the host nvme transport to callback
 * indicating the localport has successfully unregistered all
 * resources.  Since this is an uninterruptible wait, loop every ten
 * seconds and print a message indicating no progress.
 *
 * An uninterruptible wait is used because of the risk of transport-to-
 * driver state mismatch.
 */
static void
lpfc_nvme_lport_unreg_wait(struct lpfc_vport *vport,
			   struct lpfc_nvme_lport *lport,
			   struct completion *lport_unreg_cmp)
{
	u32 wait_tmo;
	int ret, i, pending = 0;
	struct lpfc_sli_ring  *pring;
	struct lpfc_hba  *phba = vport->phba;

	/* Host transport has to clean up and confirm requiring an indefinite
	 * wait. Print a message if a 10 second wait expires and renew the
	 * wait. This is unexpected.
	 */
	wait_tmo = msecs_to_jiffies(LPFC_NVME_WAIT_TMO * 1000);
	while (true) {
		ret = wait_for_completion_timeout(lport_unreg_cmp, wait_tmo);
		if (unlikely(!ret)) {
			pending = 0;
			for (i = 0; i < phba->cfg_hdw_queue; i++) {
				pring = phba->sli4_hba.hdwq[i].nvme_wq->pring;
				if (!pring)
					continue;
				if (pring->txcmplq_cnt)
					pending += pring->txcmplq_cnt;
			}
			lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_IOERR,
					 "6176 Lport %p Localport %p wait "
					 "timed out. Pending %d. Renewing.\n",
					 lport, vport->localport, pending);
			continue;
		}
		break;
	}
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_IOERR,
			 "6177 Lport %p Localport %p Complete Success\n",
			 lport, vport->localport);
}
#endif

/**
 * lpfc_nvme_destroy_localport - Destroy lpfc_nvme bound to nvme transport.
 * @pnvme: pointer to lpfc nvme data structure.
 *
 * This routine is invoked to destroy all lports bound to the phba.
 * The lport memory was allocated by the nvme fc transport and is
 * released there.  This routine ensures all rports bound to the
 * lport have been disconnected.
 *
 **/
void
lpfc_nvme_destroy_localport(struct lpfc_vport *vport)
{
#if (IS_ENABLED(CONFIG_NVME_FC))
	struct nvme_fc_local_port *localport;
	struct lpfc_nvme_lport *lport;
	int ret;
	DECLARE_COMPLETION_ONSTACK(lport_unreg_cmp);

	if (vport->nvmei_support == 0)
		return;

	localport = vport->localport;
	lport = (struct lpfc_nvme_lport *)localport->private;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME,
			 "6011 Destroying NVME localport %p\n",
			 localport);

	/* lport's rport list is clear.  Unregister
	 * lport and release resources.
	 */
	lport->lport_unreg_cmp = &lport_unreg_cmp;
	ret = nvme_fc_unregister_localport(localport);

	/* Wait for completion.  This either blocks
	 * indefinitely or succeeds
	 */
	lpfc_nvme_lport_unreg_wait(vport, lport, &lport_unreg_cmp);
	vport->localport = NULL;

	/* Regardless of the unregister upcall response, clear
	 * nvmei_support.  All rports are unregistered and the
	 * driver will clean up.
	 */
	vport->nvmei_support = 0;
	if (ret == 0) {
		lpfc_printf_vlog(vport,
				 KERN_INFO, LOG_NVME_DISC,
				 "6009 Unregistered lport Success\n");
	} else {
		lpfc_printf_vlog(vport,
				 KERN_INFO, LOG_NVME_DISC,
				 "6010 Unregistered lport "
				 "Failed, status x%x\n",
				 ret);
	}
#endif
}

void
lpfc_nvme_update_localport(struct lpfc_vport *vport)
{
#if (IS_ENABLED(CONFIG_NVME_FC))
	struct nvme_fc_local_port *localport;
	struct lpfc_nvme_lport *lport;

	localport = vport->localport;
	if (!localport) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_NVME,
				 "6710 Update NVME fail. No localport\n");
		return;
	}
	lport = (struct lpfc_nvme_lport *)localport->private;
	if (!lport) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_NVME,
				 "6171 Update NVME fail. localP %p, No lport\n",
				 localport);
		return;
	}
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME,
			 "6012 Update NVME lport %p did x%x\n",
			 localport, vport->fc_myDID);

	localport->port_id = vport->fc_myDID;
	if (localport->port_id == 0)
		localport->port_role = FC_PORT_ROLE_NVME_DISCOVERY;
	else
		localport->port_role = FC_PORT_ROLE_NVME_INITIATOR;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_DISC,
			 "6030 bound lport %p to DID x%06x\n",
			 lport, localport->port_id);
#endif
}

int
lpfc_nvme_register_port(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
#if (IS_ENABLED(CONFIG_NVME_FC))
	int ret = 0;
	struct nvme_fc_local_port *localport;
	struct lpfc_nvme_lport *lport;
	struct lpfc_nvme_rport *rport;
	struct lpfc_nvme_rport *oldrport;
	struct nvme_fc_remote_port *remote_port;
	struct nvme_fc_port_info rpinfo;
	struct lpfc_nodelist *prev_ndlp = NULL;

	lpfc_printf_vlog(ndlp->vport, KERN_INFO, LOG_NVME_DISC,
			 "6006 Register NVME PORT. DID x%06x nlptype x%x\n",
			 ndlp->nlp_DID, ndlp->nlp_type);

	localport = vport->localport;
	if (!localport)
		return 0;

	lport = (struct lpfc_nvme_lport *)localport->private;

	/* NVME rports are not preserved across devloss.
	 * Just register this instance.  Note, rpinfo->dev_loss_tmo
	 * is left 0 to indicate accept transport defaults.  The
	 * driver communicates port role capabilities consistent
	 * with the PRLI response data.
	 */
	memset(&rpinfo, 0, sizeof(struct nvme_fc_port_info));
	rpinfo.port_id = ndlp->nlp_DID;
	if (ndlp->nlp_type & NLP_NVME_TARGET)
		rpinfo.port_role |= FC_PORT_ROLE_NVME_TARGET;
	if (ndlp->nlp_type & NLP_NVME_INITIATOR)
		rpinfo.port_role |= FC_PORT_ROLE_NVME_INITIATOR;

	if (ndlp->nlp_type & NLP_NVME_DISCOVERY)
		rpinfo.port_role |= FC_PORT_ROLE_NVME_DISCOVERY;

	rpinfo.port_name = wwn_to_u64(ndlp->nlp_portname.u.wwn);
	rpinfo.node_name = wwn_to_u64(ndlp->nlp_nodename.u.wwn);

	spin_lock_irq(&vport->phba->hbalock);
	oldrport = lpfc_ndlp_get_nrport(ndlp);
	if (oldrport) {
		prev_ndlp = oldrport->ndlp;
		spin_unlock_irq(&vport->phba->hbalock);
	} else {
		spin_unlock_irq(&vport->phba->hbalock);
		lpfc_nlp_get(ndlp);
	}

	ret = nvme_fc_register_remoteport(localport, &rpinfo, &remote_port);
	if (!ret) {
		/* If the ndlp already has an nrport, this is just
		 * a resume of the existing rport.  Else this is a
		 * new rport.
		 */
		/* Guard against an unregister/reregister
		 * race that leaves the WAIT flag set.
		 */
		spin_lock_irq(&vport->phba->hbalock);
		ndlp->upcall_flags &= ~NLP_WAIT_FOR_UNREG;
		spin_unlock_irq(&vport->phba->hbalock);
		rport = remote_port->private;
		if (oldrport) {
			/* New remoteport record does not guarantee valid
			 * host private memory area.
			 */
			if (oldrport == remote_port->private) {
				/* Same remoteport - ndlp should match.
				 * Just reuse.
				 */
				lpfc_printf_vlog(ndlp->vport, KERN_INFO,
						 LOG_NVME_DISC,
						 "6014 Rebind lport to current "
						 "remoteport %p wwpn 0x%llx, "
						 "Data: x%x x%x %p %p x%x x%06x\n",
						 remote_port,
						 remote_port->port_name,
						 remote_port->port_id,
						 remote_port->port_role,
						 oldrport->ndlp,
						 ndlp,
						 ndlp->nlp_type,
						 ndlp->nlp_DID);

				/* It's a complete rebind only if the driver
				 * is registering with the same ndlp. Otherwise
				 * the driver likely executed a node swap
				 * prior to this registration and the ndlp to
				 * remoteport binding needs to be redone.
				 */
				if (prev_ndlp == ndlp)
					return 0;

			}

			/* Sever the ndlp<->rport association
			 * before dropping the ndlp ref from
			 * register.
			 */
			spin_lock_irq(&vport->phba->hbalock);
			ndlp->nrport = NULL;
			ndlp->upcall_flags &= ~NLP_WAIT_FOR_UNREG;
			spin_unlock_irq(&vport->phba->hbalock);
			rport->ndlp = NULL;
			rport->remoteport = NULL;

			/* Reference only removed if previous NDLP is no longer
			 * active. It might be just a swap and removing the
			 * reference would cause a premature cleanup.
			 */
			if (prev_ndlp && prev_ndlp != ndlp) {
				if ((!NLP_CHK_NODE_ACT(prev_ndlp)) ||
				    (!prev_ndlp->nrport))
					lpfc_nlp_put(prev_ndlp);
			}
		}

		/* Clean bind the rport to the ndlp. */
		rport->remoteport = remote_port;
		rport->lport = lport;
		rport->ndlp = ndlp;
		spin_lock_irq(&vport->phba->hbalock);
		ndlp->nrport = rport;
		spin_unlock_irq(&vport->phba->hbalock);
		lpfc_printf_vlog(vport, KERN_INFO,
				 LOG_NVME_DISC | LOG_NODE,
				 "6022 Bind lport x%px to remoteport x%px "
				 "rport x%px WWNN 0x%llx, "
				 "Rport WWPN 0x%llx DID "
				 "x%06x Role x%x, ndlp %p prev_ndlp %p\n",
				 lport, remote_port, rport,
				 rpinfo.node_name, rpinfo.port_name,
				 rpinfo.port_id, rpinfo.port_role,
				 ndlp, prev_ndlp);
	} else {
		lpfc_printf_vlog(vport, KERN_ERR,
				 LOG_NVME_DISC | LOG_NODE,
				 "6031 RemotePort Registration failed "
				 "err: %d, DID x%06x\n",
				 ret, ndlp->nlp_DID);
	}

	return ret;
#else
	return 0;
#endif
}

/**
 * lpfc_nvme_rescan_port - Check to see if we should rescan this remoteport
 *
 * If the ndlp represents an NVME Target, that we are logged into,
 * ping the NVME FC Transport layer to initiate a device rescan
 * on this remote NPort.
 */
void
lpfc_nvme_rescan_port(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
#if (IS_ENABLED(CONFIG_NVME_FC))
	struct lpfc_nvme_rport *nrport;
	struct nvme_fc_remote_port *remoteport = NULL;

	spin_lock_irq(&vport->phba->hbalock);
	nrport = lpfc_ndlp_get_nrport(ndlp);
	if (nrport)
		remoteport = nrport->remoteport;
	spin_unlock_irq(&vport->phba->hbalock);

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_DISC,
			 "6170 Rescan NPort DID x%06x type x%x "
			 "state x%x nrport x%px remoteport x%px\n",
			 ndlp->nlp_DID, ndlp->nlp_type, ndlp->nlp_state,
			 nrport, remoteport);

	if (!nrport || !remoteport)
		goto rescan_exit;

	/* Only rescan if we are an NVME target in the MAPPED state */
	if (remoteport->port_role & FC_PORT_ROLE_NVME_DISCOVERY &&
	    ndlp->nlp_state == NLP_STE_MAPPED_NODE) {
		nvme_fc_rescan_remoteport(remoteport);

		lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_DISC,
				 "6172 NVME rescanned DID x%06x "
				 "port_state x%x\n",
				 ndlp->nlp_DID, remoteport->port_state);
	}
	return;
 rescan_exit:
	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_DISC,
			 "6169 Skip NVME Rport Rescan, NVME remoteport "
			 "unregistered\n");
#endif
}

/* lpfc_nvme_unregister_port - unbind the DID and port_role from this rport.
 *
 * There is no notion of Devloss or rport recovery from the current
 * nvme_transport perspective.  Loss of an rport just means IO cannot
 * be sent and recovery is completely up to the initator.
 * For now, the driver just unbinds the DID and port_role so that
 * no further IO can be issued.  Changes are planned for later.
 *
 * Notes - the ndlp reference count is not decremented here since
 * since there is no nvme_transport api for devloss.  Node ref count
 * is only adjusted in driver unload.
 */
void
lpfc_nvme_unregister_port(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
#if (IS_ENABLED(CONFIG_NVME_FC))
	int ret;
	struct nvme_fc_local_port *localport;
	struct lpfc_nvme_lport *lport;
	struct lpfc_nvme_rport *rport;
	struct nvme_fc_remote_port *remoteport = NULL;

	localport = vport->localport;

	/* This is fundamental error.  The localport is always
	 * available until driver unload.  Just exit.
	 */
	if (!localport)
		return;

	lport = (struct lpfc_nvme_lport *)localport->private;
	if (!lport)
		goto input_err;

	spin_lock_irq(&vport->phba->hbalock);
	rport = lpfc_ndlp_get_nrport(ndlp);
	if (rport)
		remoteport = rport->remoteport;
	spin_unlock_irq(&vport->phba->hbalock);
	if (!remoteport)
		goto input_err;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_NVME_DISC,
			 "6033 Unreg nvme remoteport %p, portname x%llx, "
			 "port_id x%06x, portstate x%x port type x%x\n",
			 remoteport, remoteport->port_name,
			 remoteport->port_id, remoteport->port_state,
			 ndlp->nlp_type);

	/* Sanity check ndlp type.  Only call for NVME ports. Don't
	 * clear any rport state until the transport calls back.
	 */

	if (ndlp->nlp_type & NLP_NVME_TARGET) {
		/* No concern about the role change on the nvme remoteport.
		 * The transport will update it.
		 */
		ndlp->upcall_flags |= NLP_WAIT_FOR_UNREG;

		/* Don't let the host nvme transport keep sending keep-alives
		 * on this remoteport. Vport is unloading, no recovery. The
		 * return values is ignored.  The upcall is a courtesy to the
		 * transport.
		 */
		if (vport->load_flag & FC_UNLOADING)
			(void)nvme_fc_set_remoteport_devloss(remoteport, 0);

		ret = nvme_fc_unregister_remoteport(remoteport);
		if (ret != 0) {
			lpfc_nlp_put(ndlp);
			lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_DISC,
					 "6167 NVME unregister failed %d "
					 "port_state x%x\n",
					 ret, remoteport->port_state);
		}
	}
	return;

 input_err:
#endif
	lpfc_printf_vlog(vport, KERN_ERR, LOG_NVME_DISC,
			 "6168 State error: lport %p, rport%p FCID x%06x\n",
			 vport->localport, ndlp->rport, ndlp->nlp_DID);
}

/**
 * lpfc_sli4_nvme_xri_aborted - Fast-path process of NVME xri abort
 * @phba: pointer to lpfc hba data structure.
 * @axri: pointer to the fcp xri abort wcqe structure.
 *
 * This routine is invoked by the worker thread to process a SLI4 fast-path
 * NVME aborted xri.  Aborted NVME IO commands are completed to the transport
 * here.
 **/
void
lpfc_sli4_nvme_xri_aborted(struct lpfc_hba *phba,
			   struct sli4_wcqe_xri_aborted *axri, int idx)
{
	uint16_t xri = bf_get(lpfc_wcqe_xa_xri, axri);
	struct lpfc_io_buf *lpfc_ncmd, *next_lpfc_ncmd;
	struct nvmefc_fcp_req *nvme_cmd = NULL;
	struct lpfc_nodelist *ndlp;
	struct lpfc_sli4_hdw_queue *qp;
	unsigned long iflag = 0;

	if (!(phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME))
		return;
	qp = &phba->sli4_hba.hdwq[idx];
	spin_lock_irqsave(&phba->hbalock, iflag);
	spin_lock(&qp->abts_nvme_buf_list_lock);
	list_for_each_entry_safe(lpfc_ncmd, next_lpfc_ncmd,
				 &qp->lpfc_abts_nvme_buf_list, list) {
		if (lpfc_ncmd->cur_iocbq.sli4_xritag == xri) {
			list_del_init(&lpfc_ncmd->list);
			qp->abts_nvme_io_bufs--;
			lpfc_ncmd->flags &= ~LPFC_SBUF_XBUSY;
			lpfc_ncmd->status = IOSTAT_SUCCESS;
			spin_unlock(&qp->abts_nvme_buf_list_lock);

			spin_unlock_irqrestore(&phba->hbalock, iflag);
			ndlp = lpfc_ncmd->ndlp;
			if (ndlp)
				lpfc_sli4_abts_err_handler(phba, ndlp, axri);

			lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
					"6311 nvme_cmd %p xri x%x tag x%x "
					"abort complete and xri released\n",
					lpfc_ncmd->nvmeCmd, xri,
					lpfc_ncmd->cur_iocbq.iotag);

			/* Aborted NVME commands are required to not complete
			 * before the abort exchange command fully completes.
			 * Once completed, it is available via the put list.
			 */
			if (lpfc_ncmd->nvmeCmd) {
				nvme_cmd = lpfc_ncmd->nvmeCmd;
				nvme_cmd->done(nvme_cmd);
				lpfc_ncmd->nvmeCmd = NULL;
			}
			lpfc_release_nvme_buf(phba, lpfc_ncmd);
			return;
		}
	}
	spin_unlock(&qp->abts_nvme_buf_list_lock);
	spin_unlock_irqrestore(&phba->hbalock, iflag);

	lpfc_printf_log(phba, KERN_INFO, LOG_NVME_ABTS,
			"6312 XRI Aborted xri x%x not found\n", xri);

}

/**
 * lpfc_nvme_wait_for_io_drain - Wait for all NVME wqes to complete
 * @phba: Pointer to HBA context object.
 *
 * This function flushes all wqes in the nvme rings and frees all resources
 * in the txcmplq. This function does not issue abort wqes for the IO
 * commands in txcmplq, they will just be returned with
 * IOERR_SLI_DOWN. This function is invoked with EEH when device's PCI
 * slot has been permanently disabled.
 **/
void
lpfc_nvme_wait_for_io_drain(struct lpfc_hba *phba)
{
	struct lpfc_sli_ring  *pring;
	u32 i, wait_cnt = 0;

	if (phba->sli_rev < LPFC_SLI_REV4 || !phba->sli4_hba.hdwq)
		return;

	/* Cycle through all NVME rings and make sure all outstanding
	 * WQEs have been removed from the txcmplqs.
	 */
	for (i = 0; i < phba->cfg_hdw_queue; i++) {
		if (!phba->sli4_hba.hdwq[i].nvme_wq)
			continue;
		pring = phba->sli4_hba.hdwq[i].nvme_wq->pring;

		if (!pring)
			continue;

		/* Retrieve everything on the txcmplq */
		while (!list_empty(&pring->txcmplq)) {
			msleep(LPFC_XRI_EXCH_BUSY_WAIT_T1);
			wait_cnt++;

			/* The sleep is 10mS.  Every ten seconds,
			 * dump a message.  Something is wrong.
			 */
			if ((wait_cnt % 1000) == 0) {
				lpfc_printf_log(phba, KERN_ERR, LOG_NVME_IOERR,
						"6178 NVME IO not empty, "
						"cnt %d\n", wait_cnt);
			}
		}
	}
}
