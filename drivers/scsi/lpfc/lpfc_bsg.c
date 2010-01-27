/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2009-2010 Emulex.  All rights reserved.                *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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
 *******************************************************************/

#include <linux/interrupt.h>
#include <linux/mempool.h>
#include <linux/pci.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_bsg_fc.h>
#include <scsi/fc/fc_fs.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_bsg.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_version.h"

struct lpfc_bsg_event {
	struct list_head node;
	struct kref kref;
	wait_queue_head_t wq;

	/* Event type and waiter identifiers */
	uint32_t type_mask;
	uint32_t req_id;
	uint32_t reg_id;

	/* next two flags are here for the auto-delete logic */
	unsigned long wait_time_stamp;
	int waiting;

	/* seen and not seen events */
	struct list_head events_to_get;
	struct list_head events_to_see;

	/* job waiting for this event to finish */
	struct fc_bsg_job *set_job;
};

struct lpfc_bsg_iocb {
	struct lpfc_iocbq *cmdiocbq;
	struct lpfc_iocbq *rspiocbq;
	struct lpfc_dmabuf *bmp;
	struct lpfc_nodelist *ndlp;

	/* job waiting for this iocb to finish */
	struct fc_bsg_job *set_job;
};

#define TYPE_EVT 	1
#define TYPE_IOCB	2
struct bsg_job_data {
	uint32_t type;
	union {
		struct lpfc_bsg_event *evt;
		struct lpfc_bsg_iocb iocb;
	} context_un;
};

struct event_data {
	struct list_head node;
	uint32_t type;
	uint32_t immed_dat;
	void *data;
	uint32_t len;
};

#define SLI_CT_ELX_LOOPBACK 0x10

enum ELX_LOOPBACK_CMD {
	ELX_LOOPBACK_XRI_SETUP,
	ELX_LOOPBACK_DATA,
};

struct lpfc_dmabufext {
	struct lpfc_dmabuf dma;
	uint32_t size;
	uint32_t flag;
};

/**
 * lpfc_bsg_send_mgmt_cmd_cmp - lpfc_bsg_send_mgmt_cmd's completion handler
 * @phba: Pointer to HBA context object.
 * @cmdiocbq: Pointer to command iocb.
 * @rspiocbq: Pointer to response iocb.
 *
 * This function is the completion handler for iocbs issued using
 * lpfc_bsg_send_mgmt_cmd function. This function is called by the
 * ring event handler function without any lock held. This function
 * can be called from both worker thread context and interrupt
 * context. This function also can be called from another thread which
 * cleans up the SLI layer objects.
 * This function copies the contents of the response iocb to the
 * response iocb memory object provided by the caller of
 * lpfc_sli_issue_iocb_wait and then wakes up the thread which
 * sleeps for the iocb completion.
 **/
static void
lpfc_bsg_send_mgmt_cmd_cmp(struct lpfc_hba *phba,
			struct lpfc_iocbq *cmdiocbq,
			struct lpfc_iocbq *rspiocbq)
{
	unsigned long iflags;
	struct bsg_job_data *dd_data;
	struct fc_bsg_job *job;
	IOCB_t *rsp;
	struct lpfc_dmabuf *bmp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_bsg_iocb *iocb;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	dd_data = cmdiocbq->context1;
	if (!dd_data) {
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		return;
	}

	iocb = &dd_data->context_un.iocb;
	job = iocb->set_job;
	job->dd_data = NULL; /* so timeout handler does not reply */

	spin_lock_irqsave(&phba->hbalock, iflags);
	cmdiocbq->iocb_flag |= LPFC_IO_WAKE;
	if (cmdiocbq->context2 && rspiocbq)
		memcpy(&((struct lpfc_iocbq *)cmdiocbq->context2)->iocb,
		       &rspiocbq->iocb, sizeof(IOCB_t));
	spin_unlock_irqrestore(&phba->hbalock, iflags);

	bmp = iocb->bmp;
	rspiocbq = iocb->rspiocbq;
	rsp = &rspiocbq->iocb;
	ndlp = iocb->ndlp;

	pci_unmap_sg(phba->pcidev, job->request_payload.sg_list,
		     job->request_payload.sg_cnt, DMA_TO_DEVICE);
	pci_unmap_sg(phba->pcidev, job->reply_payload.sg_list,
		     job->reply_payload.sg_cnt, DMA_FROM_DEVICE);

	if (rsp->ulpStatus) {
		if (rsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
			switch (rsp->un.ulpWord[4] & 0xff) {
			case IOERR_SEQUENCE_TIMEOUT:
				rc = -ETIMEDOUT;
				break;
			case IOERR_INVALID_RPI:
				rc = -EFAULT;
				break;
			default:
				rc = -EACCES;
				break;
			}
		} else
			rc = -EACCES;
	} else
		job->reply->reply_payload_rcv_len =
			rsp->un.genreq64.bdl.bdeSize;

	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	lpfc_sli_release_iocbq(phba, rspiocbq);
	lpfc_sli_release_iocbq(phba, cmdiocbq);
	lpfc_nlp_put(ndlp);
	kfree(bmp);
	kfree(dd_data);
	/* make error code available to userspace */
	job->reply->result = rc;
	/* complete the job back to userspace */
	job->job_done(job);
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
	return;
}

/**
 * lpfc_bsg_send_mgmt_cmd - send a CT command from a bsg request
 * @job: fc_bsg_job to handle
 */
static int
lpfc_bsg_send_mgmt_cmd(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_rport_data *rdata = job->rport->dd_data;
	struct lpfc_nodelist *ndlp = rdata->pnode;
	struct ulp_bde64 *bpl = NULL;
	uint32_t timeout;
	struct lpfc_iocbq *cmdiocbq = NULL;
	struct lpfc_iocbq *rspiocbq = NULL;
	IOCB_t *cmd;
	IOCB_t *rsp;
	struct lpfc_dmabuf *bmp = NULL;
	int request_nseg;
	int reply_nseg;
	struct scatterlist *sgel = NULL;
	int numbde;
	dma_addr_t busaddr;
	struct bsg_job_data *dd_data;
	uint32_t creg_val;
	int rc = 0;

	/* in case no data is transferred */
	job->reply->reply_payload_rcv_len = 0;

	/* allocate our bsg tracking structure */
	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (!dd_data) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2733 Failed allocation of dd_data\n");
		rc = -ENOMEM;
		goto no_dd_data;
	}

	if (!lpfc_nlp_get(ndlp)) {
		rc = -ENODEV;
		goto no_ndlp;
	}

	bmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp) {
		rc = -ENOMEM;
		goto free_ndlp;
	}

	if (ndlp->nlp_flag & NLP_ELS_SND_MASK) {
		rc = -ENODEV;
		goto free_bmp;
	}

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	if (!cmdiocbq) {
		rc = -ENOMEM;
		goto free_bmp;
	}

	cmd = &cmdiocbq->iocb;
	rspiocbq = lpfc_sli_get_iocbq(phba);
	if (!rspiocbq) {
		rc = -ENOMEM;
		goto free_cmdiocbq;
	}

	rsp = &rspiocbq->iocb;
	bmp->virt = lpfc_mbuf_alloc(phba, 0, &bmp->phys);
	if (!bmp->virt) {
		rc = -ENOMEM;
		goto free_rspiocbq;
	}

	INIT_LIST_HEAD(&bmp->list);
	bpl = (struct ulp_bde64 *) bmp->virt;
	request_nseg = pci_map_sg(phba->pcidev, job->request_payload.sg_list,
				  job->request_payload.sg_cnt, DMA_TO_DEVICE);
	for_each_sg(job->request_payload.sg_list, sgel, request_nseg, numbde) {
		busaddr = sg_dma_address(sgel);
		bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		bpl->tus.f.bdeSize = sg_dma_len(sgel);
		bpl->tus.w = cpu_to_le32(bpl->tus.w);
		bpl->addrLow = cpu_to_le32(putPaddrLow(busaddr));
		bpl->addrHigh = cpu_to_le32(putPaddrHigh(busaddr));
		bpl++;
	}

	reply_nseg = pci_map_sg(phba->pcidev, job->reply_payload.sg_list,
				job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
	for_each_sg(job->reply_payload.sg_list, sgel, reply_nseg, numbde) {
		busaddr = sg_dma_address(sgel);
		bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
		bpl->tus.f.bdeSize = sg_dma_len(sgel);
		bpl->tus.w = cpu_to_le32(bpl->tus.w);
		bpl->addrLow = cpu_to_le32(putPaddrLow(busaddr));
		bpl->addrHigh = cpu_to_le32(putPaddrHigh(busaddr));
		bpl++;
	}

	cmd->un.genreq64.bdl.ulpIoTag32 = 0;
	cmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	cmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	cmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	cmd->un.genreq64.bdl.bdeSize =
		(request_nseg + reply_nseg) * sizeof(struct ulp_bde64);
	cmd->ulpCommand = CMD_GEN_REQUEST64_CR;
	cmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	cmd->un.genreq64.w5.hcsw.Dfctl = 0;
	cmd->un.genreq64.w5.hcsw.Rctl = FC_RCTL_DD_UNSOL_CTL;
	cmd->un.genreq64.w5.hcsw.Type = FC_TYPE_CT;
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = ndlp->nlp_rpi;
	cmd->ulpOwner = OWN_CHIP;
	cmdiocbq->vport = phba->pport;
	cmdiocbq->context3 = bmp;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	timeout = phba->fc_ratov * 2;
	cmd->ulpTimeout = timeout;

	cmdiocbq->iocb_cmpl = lpfc_bsg_send_mgmt_cmd_cmp;
	cmdiocbq->context1 = dd_data;
	cmdiocbq->context2 = rspiocbq;
	dd_data->type = TYPE_IOCB;
	dd_data->context_un.iocb.cmdiocbq = cmdiocbq;
	dd_data->context_un.iocb.rspiocbq = rspiocbq;
	dd_data->context_un.iocb.set_job = job;
	dd_data->context_un.iocb.bmp = bmp;
	dd_data->context_un.iocb.ndlp = ndlp;

	if (phba->cfg_poll & DISABLE_FCP_RING_INT) {
		creg_val = readl(phba->HCregaddr);
		creg_val |= (HC_R0INT_ENA << LPFC_FCP_RING);
		writel(creg_val, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, cmdiocbq, 0);

	if (rc == IOCB_SUCCESS)
		return 0; /* done for now */

	/* iocb failed so cleanup */
	pci_unmap_sg(phba->pcidev, job->request_payload.sg_list,
		     job->request_payload.sg_cnt, DMA_TO_DEVICE);
	pci_unmap_sg(phba->pcidev, job->reply_payload.sg_list,
		     job->reply_payload.sg_cnt, DMA_FROM_DEVICE);

	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);

free_rspiocbq:
	lpfc_sli_release_iocbq(phba, rspiocbq);
free_cmdiocbq:
	lpfc_sli_release_iocbq(phba, cmdiocbq);
free_bmp:
	kfree(bmp);
free_ndlp:
	lpfc_nlp_put(ndlp);
no_ndlp:
	kfree(dd_data);
no_dd_data:
	/* make error code available to userspace */
	job->reply->result = rc;
	job->dd_data = NULL;
	return rc;
}

/**
 * lpfc_bsg_rport_els_cmp - lpfc_bsg_rport_els's completion handler
 * @phba: Pointer to HBA context object.
 * @cmdiocbq: Pointer to command iocb.
 * @rspiocbq: Pointer to response iocb.
 *
 * This function is the completion handler for iocbs issued using
 * lpfc_bsg_rport_els_cmp function. This function is called by the
 * ring event handler function without any lock held. This function
 * can be called from both worker thread context and interrupt
 * context. This function also can be called from other thread which
 * cleans up the SLI layer objects.
 * This function copy the contents of the response iocb to the
 * response iocb memory object provided by the caller of
 * lpfc_sli_issue_iocb_wait and then wakes up the thread which
 * sleeps for the iocb completion.
 **/
static void
lpfc_bsg_rport_els_cmp(struct lpfc_hba *phba,
			struct lpfc_iocbq *cmdiocbq,
			struct lpfc_iocbq *rspiocbq)
{
	struct bsg_job_data *dd_data;
	struct fc_bsg_job *job;
	IOCB_t *rsp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_dmabuf *pbuflist = NULL;
	struct fc_bsg_ctels_reply *els_reply;
	uint8_t *rjt_data;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	dd_data = cmdiocbq->context1;
	/* normal completion and timeout crossed paths, already done */
	if (!dd_data) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}

	cmdiocbq->iocb_flag |= LPFC_IO_WAKE;
	if (cmdiocbq->context2 && rspiocbq)
		memcpy(&((struct lpfc_iocbq *)cmdiocbq->context2)->iocb,
		       &rspiocbq->iocb, sizeof(IOCB_t));

	job = dd_data->context_un.iocb.set_job;
	cmdiocbq = dd_data->context_un.iocb.cmdiocbq;
	rspiocbq = dd_data->context_un.iocb.rspiocbq;
	rsp = &rspiocbq->iocb;
	ndlp = dd_data->context_un.iocb.ndlp;

	pci_unmap_sg(phba->pcidev, job->request_payload.sg_list,
		     job->request_payload.sg_cnt, DMA_TO_DEVICE);
	pci_unmap_sg(phba->pcidev, job->reply_payload.sg_list,
		     job->reply_payload.sg_cnt, DMA_FROM_DEVICE);

	if (job->reply->result == -EAGAIN)
		rc = -EAGAIN;
	else if (rsp->ulpStatus == IOSTAT_SUCCESS)
		job->reply->reply_payload_rcv_len =
			rsp->un.elsreq64.bdl.bdeSize;
	else if (rsp->ulpStatus == IOSTAT_LS_RJT) {
		job->reply->reply_payload_rcv_len =
			sizeof(struct fc_bsg_ctels_reply);
		/* LS_RJT data returned in word 4 */
		rjt_data = (uint8_t *)&rsp->un.ulpWord[4];
		els_reply = &job->reply->reply_data.ctels_reply;
		els_reply->status = FC_CTELS_STATUS_REJECT;
		els_reply->rjt_data.action = rjt_data[3];
		els_reply->rjt_data.reason_code = rjt_data[2];
		els_reply->rjt_data.reason_explanation = rjt_data[1];
		els_reply->rjt_data.vendor_unique = rjt_data[0];
	} else
		rc = -EIO;

	pbuflist = (struct lpfc_dmabuf *) cmdiocbq->context3;
	lpfc_mbuf_free(phba, pbuflist->virt, pbuflist->phys);
	lpfc_sli_release_iocbq(phba, rspiocbq);
	lpfc_sli_release_iocbq(phba, cmdiocbq);
	lpfc_nlp_put(ndlp);
	kfree(dd_data);
	/* make error code available to userspace */
	job->reply->result = rc;
	job->dd_data = NULL;
	/* complete the job back to userspace */
	job->job_done(job);
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
	return;
}

/**
 * lpfc_bsg_rport_els - send an ELS command from a bsg request
 * @job: fc_bsg_job to handle
 */
static int
lpfc_bsg_rport_els(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_rport_data *rdata = job->rport->dd_data;
	struct lpfc_nodelist *ndlp = rdata->pnode;
	uint32_t elscmd;
	uint32_t cmdsize;
	uint32_t rspsize;
	struct lpfc_iocbq *rspiocbq;
	struct lpfc_iocbq *cmdiocbq;
	IOCB_t *rsp;
	uint16_t rpi = 0;
	struct lpfc_dmabuf *pcmd;
	struct lpfc_dmabuf *prsp;
	struct lpfc_dmabuf *pbuflist = NULL;
	struct ulp_bde64 *bpl;
	int request_nseg;
	int reply_nseg;
	struct scatterlist *sgel = NULL;
	int numbde;
	dma_addr_t busaddr;
	struct bsg_job_data *dd_data;
	uint32_t creg_val;
	int rc = 0;

	/* in case no data is transferred */
	job->reply->reply_payload_rcv_len = 0;

	/* allocate our bsg tracking structure */
	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (!dd_data) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2735 Failed allocation of dd_data\n");
		rc = -ENOMEM;
		goto no_dd_data;
	}

	if (!lpfc_nlp_get(ndlp)) {
		rc = -ENODEV;
		goto free_dd_data;
	}

	elscmd = job->request->rqst_data.r_els.els_code;
	cmdsize = job->request_payload.payload_len;
	rspsize = job->reply_payload.payload_len;
	rspiocbq = lpfc_sli_get_iocbq(phba);
	if (!rspiocbq) {
		lpfc_nlp_put(ndlp);
		rc = -ENOMEM;
		goto free_dd_data;
	}

	rsp = &rspiocbq->iocb;
	rpi = ndlp->nlp_rpi;

	cmdiocbq = lpfc_prep_els_iocb(vport, 1, cmdsize, 0, ndlp,
				      ndlp->nlp_DID, elscmd);
	if (!cmdiocbq) {
		rc = -EIO;
		goto free_rspiocbq;
	}

	/* prep els iocb set context1 to the ndlp, context2 to the command
	* dmabuf, context3 holds the data dmabuf
	*/
	pcmd = (struct lpfc_dmabuf *) cmdiocbq->context2;
	prsp = (struct lpfc_dmabuf *) pcmd->list.next;
	lpfc_mbuf_free(phba, pcmd->virt, pcmd->phys);
	kfree(pcmd);
	lpfc_mbuf_free(phba, prsp->virt, prsp->phys);
	kfree(prsp);
	cmdiocbq->context2 = NULL;

	pbuflist = (struct lpfc_dmabuf *) cmdiocbq->context3;
	bpl = (struct ulp_bde64 *) pbuflist->virt;

	request_nseg = pci_map_sg(phba->pcidev, job->request_payload.sg_list,
				  job->request_payload.sg_cnt, DMA_TO_DEVICE);
	for_each_sg(job->request_payload.sg_list, sgel, request_nseg, numbde) {
		busaddr = sg_dma_address(sgel);
		bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		bpl->tus.f.bdeSize = sg_dma_len(sgel);
		bpl->tus.w = cpu_to_le32(bpl->tus.w);
		bpl->addrLow = cpu_to_le32(putPaddrLow(busaddr));
		bpl->addrHigh = cpu_to_le32(putPaddrHigh(busaddr));
		bpl++;
	}

	reply_nseg = pci_map_sg(phba->pcidev, job->reply_payload.sg_list,
				job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
	for_each_sg(job->reply_payload.sg_list, sgel, reply_nseg, numbde) {
		busaddr = sg_dma_address(sgel);
		bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
		bpl->tus.f.bdeSize = sg_dma_len(sgel);
		bpl->tus.w = cpu_to_le32(bpl->tus.w);
		bpl->addrLow = cpu_to_le32(putPaddrLow(busaddr));
		bpl->addrHigh = cpu_to_le32(putPaddrHigh(busaddr));
		bpl++;
	}
	cmdiocbq->iocb.un.elsreq64.bdl.bdeSize =
		(request_nseg + reply_nseg) * sizeof(struct ulp_bde64);
	cmdiocbq->iocb.ulpContext = rpi;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	cmdiocbq->context1 = NULL;
	cmdiocbq->context2 = NULL;

	cmdiocbq->iocb_cmpl = lpfc_bsg_rport_els_cmp;
	cmdiocbq->context1 = dd_data;
	cmdiocbq->context2 = rspiocbq;
	dd_data->type = TYPE_IOCB;
	dd_data->context_un.iocb.cmdiocbq = cmdiocbq;
	dd_data->context_un.iocb.rspiocbq = rspiocbq;
	dd_data->context_un.iocb.set_job = job;
	dd_data->context_un.iocb.bmp = NULL;;
	dd_data->context_un.iocb.ndlp = ndlp;

	if (phba->cfg_poll & DISABLE_FCP_RING_INT) {
		creg_val = readl(phba->HCregaddr);
		creg_val |= (HC_R0INT_ENA << LPFC_FCP_RING);
		writel(creg_val, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}
	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, cmdiocbq, 0);
	lpfc_nlp_put(ndlp);
	if (rc == IOCB_SUCCESS)
		return 0; /* done for now */

	pci_unmap_sg(phba->pcidev, job->request_payload.sg_list,
		     job->request_payload.sg_cnt, DMA_TO_DEVICE);
	pci_unmap_sg(phba->pcidev, job->reply_payload.sg_list,
		     job->reply_payload.sg_cnt, DMA_FROM_DEVICE);

	lpfc_mbuf_free(phba, pbuflist->virt, pbuflist->phys);

	lpfc_sli_release_iocbq(phba, cmdiocbq);

free_rspiocbq:
	lpfc_sli_release_iocbq(phba, rspiocbq);

free_dd_data:
	kfree(dd_data);

no_dd_data:
	/* make error code available to userspace */
	job->reply->result = rc;
	job->dd_data = NULL;
	return rc;
}

static void
lpfc_bsg_event_free(struct kref *kref)
{
	struct lpfc_bsg_event *evt = container_of(kref, struct lpfc_bsg_event,
						  kref);
	struct event_data *ed;

	list_del(&evt->node);

	while (!list_empty(&evt->events_to_get)) {
		ed = list_entry(evt->events_to_get.next, typeof(*ed), node);
		list_del(&ed->node);
		kfree(ed->data);
		kfree(ed);
	}

	while (!list_empty(&evt->events_to_see)) {
		ed = list_entry(evt->events_to_see.next, typeof(*ed), node);
		list_del(&ed->node);
		kfree(ed->data);
		kfree(ed);
	}

	kfree(evt);
}

static inline void
lpfc_bsg_event_ref(struct lpfc_bsg_event *evt)
{
	kref_get(&evt->kref);
}

static inline void
lpfc_bsg_event_unref(struct lpfc_bsg_event *evt)
{
	kref_put(&evt->kref, lpfc_bsg_event_free);
}

static struct lpfc_bsg_event *
lpfc_bsg_event_new(uint32_t ev_mask, int ev_reg_id, uint32_t ev_req_id)
{
	struct lpfc_bsg_event *evt = kzalloc(sizeof(*evt), GFP_KERNEL);

	if (!evt)
		return NULL;

	INIT_LIST_HEAD(&evt->events_to_get);
	INIT_LIST_HEAD(&evt->events_to_see);
	evt->type_mask = ev_mask;
	evt->req_id = ev_req_id;
	evt->reg_id = ev_reg_id;
	evt->wait_time_stamp = jiffies;
	init_waitqueue_head(&evt->wq);
	kref_init(&evt->kref);
	return evt;
}

static int
dfc_cmd_data_free(struct lpfc_hba *phba, struct lpfc_dmabufext *mlist)
{
	struct lpfc_dmabufext *mlast;
	struct pci_dev *pcidev;
	struct list_head head, *curr, *next;

	if ((!mlist) || (!lpfc_is_link_up(phba) &&
		(phba->link_flag & LS_LOOPBACK_MODE))) {
		return 0;
	}

	pcidev = phba->pcidev;
	list_add_tail(&head, &mlist->dma.list);

	list_for_each_safe(curr, next, &head) {
		mlast = list_entry(curr, struct lpfc_dmabufext , dma.list);
		if (mlast->dma.virt)
			dma_free_coherent(&pcidev->dev,
					  mlast->size,
					  mlast->dma.virt,
					  mlast->dma.phys);
		kfree(mlast);
	}
	return 0;
}

/**
 * lpfc_bsg_ct_unsol_event - process an unsolicited CT command
 * @phba:
 * @pring:
 * @piocbq:
 *
 * This function is called when an unsolicited CT command is received.  It
 * forwards the event to any processes registered to receive CT events.
 */
int
lpfc_bsg_ct_unsol_event(struct lpfc_hba *phba, struct lpfc_sli_ring *pring,
			struct lpfc_iocbq *piocbq)
{
	uint32_t evt_req_id = 0;
	uint32_t cmd;
	uint32_t len;
	struct lpfc_dmabuf *dmabuf = NULL;
	struct lpfc_bsg_event *evt;
	struct event_data *evt_dat = NULL;
	struct lpfc_iocbq *iocbq;
	size_t offset = 0;
	struct list_head head;
	struct ulp_bde64 *bde;
	dma_addr_t dma_addr;
	int i;
	struct lpfc_dmabuf *bdeBuf1 = piocbq->context2;
	struct lpfc_dmabuf *bdeBuf2 = piocbq->context3;
	struct lpfc_hbq_entry *hbqe;
	struct lpfc_sli_ct_request *ct_req;
	struct fc_bsg_job *job = NULL;
	unsigned long flags;
	int size = 0;

	INIT_LIST_HEAD(&head);
	list_add_tail(&head, &piocbq->list);

	if (piocbq->iocb.ulpBdeCount == 0 ||
	    piocbq->iocb.un.cont64[0].tus.f.bdeSize == 0)
		goto error_ct_unsol_exit;

	if (phba->link_state == LPFC_HBA_ERROR ||
		(!(phba->sli.sli_flag & LPFC_SLI_ACTIVE)))
		goto error_ct_unsol_exit;

	if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED)
		dmabuf = bdeBuf1;
	else {
		dma_addr = getPaddr(piocbq->iocb.un.cont64[0].addrHigh,
				    piocbq->iocb.un.cont64[0].addrLow);
		dmabuf = lpfc_sli_ringpostbuf_get(phba, pring, dma_addr);
	}
	if (dmabuf == NULL)
		goto error_ct_unsol_exit;
	ct_req = (struct lpfc_sli_ct_request *)dmabuf->virt;
	evt_req_id = ct_req->FsType;
	cmd = ct_req->CommandResponse.bits.CmdRsp;
	len = ct_req->CommandResponse.bits.Size;
	if (!(phba->sli3_options & LPFC_SLI3_HBQ_ENABLED))
		lpfc_sli_ringpostbuf_put(phba, pring, dmabuf);

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	list_for_each_entry(evt, &phba->ct_ev_waiters, node) {
		if (!(evt->type_mask & FC_REG_CT_EVENT) ||
			evt->req_id != evt_req_id)
			continue;

		lpfc_bsg_event_ref(evt);
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		evt_dat = kzalloc(sizeof(*evt_dat), GFP_KERNEL);
		if (evt_dat == NULL) {
			spin_lock_irqsave(&phba->ct_ev_lock, flags);
			lpfc_bsg_event_unref(evt);
			lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
					"2614 Memory allocation failed for "
					"CT event\n");
			break;
		}

		if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
			/* take accumulated byte count from the last iocbq */
			iocbq = list_entry(head.prev, typeof(*iocbq), list);
			evt_dat->len = iocbq->iocb.unsli3.rcvsli3.acc_len;
		} else {
			list_for_each_entry(iocbq, &head, list) {
				for (i = 0; i < iocbq->iocb.ulpBdeCount; i++)
					evt_dat->len +=
					iocbq->iocb.un.cont64[i].tus.f.bdeSize;
			}
		}

		evt_dat->data = kzalloc(evt_dat->len, GFP_KERNEL);
		if (evt_dat->data == NULL) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
					"2615 Memory allocation failed for "
					"CT event data, size %d\n",
					evt_dat->len);
			kfree(evt_dat);
			spin_lock_irqsave(&phba->ct_ev_lock, flags);
			lpfc_bsg_event_unref(evt);
			spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
			goto error_ct_unsol_exit;
		}

		list_for_each_entry(iocbq, &head, list) {
			size = 0;
			if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
				bdeBuf1 = iocbq->context2;
				bdeBuf2 = iocbq->context3;
			}
			for (i = 0; i < iocbq->iocb.ulpBdeCount; i++) {
				if (phba->sli3_options &
				    LPFC_SLI3_HBQ_ENABLED) {
					if (i == 0) {
						hbqe = (struct lpfc_hbq_entry *)
						  &iocbq->iocb.un.ulpWord[0];
						size = hbqe->bde.tus.f.bdeSize;
						dmabuf = bdeBuf1;
					} else if (i == 1) {
						hbqe = (struct lpfc_hbq_entry *)
							&iocbq->iocb.unsli3.
							sli3Words[4];
						size = hbqe->bde.tus.f.bdeSize;
						dmabuf = bdeBuf2;
					}
					if ((offset + size) > evt_dat->len)
						size = evt_dat->len - offset;
				} else {
					size = iocbq->iocb.un.cont64[i].
						tus.f.bdeSize;
					bde = &iocbq->iocb.un.cont64[i];
					dma_addr = getPaddr(bde->addrHigh,
							    bde->addrLow);
					dmabuf = lpfc_sli_ringpostbuf_get(phba,
							pring, dma_addr);
				}
				if (!dmabuf) {
					lpfc_printf_log(phba, KERN_ERR,
						LOG_LIBDFC, "2616 No dmabuf "
						"found for iocbq 0x%p\n",
						iocbq);
					kfree(evt_dat->data);
					kfree(evt_dat);
					spin_lock_irqsave(&phba->ct_ev_lock,
						flags);
					lpfc_bsg_event_unref(evt);
					spin_unlock_irqrestore(
						&phba->ct_ev_lock, flags);
					goto error_ct_unsol_exit;
				}
				memcpy((char *)(evt_dat->data) + offset,
				       dmabuf->virt, size);
				offset += size;
				if (evt_req_id != SLI_CT_ELX_LOOPBACK &&
				    !(phba->sli3_options &
				      LPFC_SLI3_HBQ_ENABLED)) {
					lpfc_sli_ringpostbuf_put(phba, pring,
								 dmabuf);
				} else {
					switch (cmd) {
					case ELX_LOOPBACK_DATA:
						dfc_cmd_data_free(phba,
						(struct lpfc_dmabufext *)
							dmabuf);
						break;
					case ELX_LOOPBACK_XRI_SETUP:
						if ((phba->sli_rev ==
							LPFC_SLI_REV2) ||
							(phba->sli3_options &
							LPFC_SLI3_HBQ_ENABLED
							)) {
							lpfc_in_buf_free(phba,
									dmabuf);
						} else {
							lpfc_post_buffer(phba,
									 pring,
									 1);
						}
						break;
					default:
						if (!(phba->sli3_options &
						      LPFC_SLI3_HBQ_ENABLED))
							lpfc_post_buffer(phba,
									 pring,
									 1);
						break;
					}
				}
			}
		}

		spin_lock_irqsave(&phba->ct_ev_lock, flags);
		if (phba->sli_rev == LPFC_SLI_REV4) {
			evt_dat->immed_dat = phba->ctx_idx;
			phba->ctx_idx = (phba->ctx_idx + 1) % 64;
			phba->ct_ctx[evt_dat->immed_dat].oxid =
						piocbq->iocb.ulpContext;
			phba->ct_ctx[evt_dat->immed_dat].SID =
				piocbq->iocb.un.rcvels.remoteID;
		} else
			evt_dat->immed_dat = piocbq->iocb.ulpContext;

		evt_dat->type = FC_REG_CT_EVENT;
		list_add(&evt_dat->node, &evt->events_to_see);
		if (evt_req_id == SLI_CT_ELX_LOOPBACK) {
			wake_up_interruptible(&evt->wq);
			lpfc_bsg_event_unref(evt);
			break;
		}

		list_move(evt->events_to_see.prev, &evt->events_to_get);
		lpfc_bsg_event_unref(evt);

		job = evt->set_job;
		evt->set_job = NULL;
		if (job) {
			job->reply->reply_payload_rcv_len = size;
			/* make error code available to userspace */
			job->reply->result = 0;
			job->dd_data = NULL;
			/* complete the job back to userspace */
			spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
			job->job_done(job);
			spin_lock_irqsave(&phba->ct_ev_lock, flags);
		}
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

error_ct_unsol_exit:
	if (!list_empty(&head))
		list_del(&head);
	if (evt_req_id == SLI_CT_ELX_LOOPBACK)
		return 0;
	return 1;
}

/**
 * lpfc_bsg_hba_set_event - process a SET_EVENT bsg vendor command
 * @job: SET_EVENT fc_bsg_job
 */
static int
lpfc_bsg_hba_set_event(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct set_ct_event *event_req;
	struct lpfc_bsg_event *evt;
	int rc = 0;
	struct bsg_job_data *dd_data = NULL;
	uint32_t ev_mask;
	unsigned long flags;

	if (job->request_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct set_ct_event)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2612 Received SET_CT_EVENT below minimum "
				"size\n");
		rc = -EINVAL;
		goto job_error;
	}

	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (dd_data == NULL) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2734 Failed allocation of dd_data\n");
		rc = -ENOMEM;
		goto job_error;
	}

	event_req = (struct set_ct_event *)
		job->request->rqst_data.h_vendor.vendor_cmd;
	ev_mask = ((uint32_t)(unsigned long)event_req->type_mask &
				FC_REG_EVENT_MASK);
	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	list_for_each_entry(evt, &phba->ct_ev_waiters, node) {
		if (evt->reg_id == event_req->ev_reg_id) {
			lpfc_bsg_event_ref(evt);
			evt->wait_time_stamp = jiffies;
			break;
		}
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	if (&evt->node == &phba->ct_ev_waiters) {
		/* no event waiting struct yet - first call */
		evt = lpfc_bsg_event_new(ev_mask, event_req->ev_reg_id,
					event_req->ev_req_id);
		if (!evt) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
					"2617 Failed allocation of event "
					"waiter\n");
			rc = -ENOMEM;
			goto job_error;
		}

		spin_lock_irqsave(&phba->ct_ev_lock, flags);
		list_add(&evt->node, &phba->ct_ev_waiters);
		lpfc_bsg_event_ref(evt);
		evt->wait_time_stamp = jiffies;
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
	}

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	evt->waiting = 1;
	dd_data->type = TYPE_EVT;
	dd_data->context_un.evt = evt;
	evt->set_job = job; /* for unsolicited command */
	job->dd_data = dd_data; /* for fc transport timeout callback*/
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
	return 0; /* call job done later */

job_error:
	if (dd_data != NULL)
		kfree(dd_data);

	job->dd_data = NULL;
	return rc;
}

/**
 * lpfc_bsg_hba_get_event - process a GET_EVENT bsg vendor command
 * @job: GET_EVENT fc_bsg_job
 */
static int
lpfc_bsg_hba_get_event(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct get_ct_event *event_req;
	struct get_ct_event_reply *event_reply;
	struct lpfc_bsg_event *evt;
	struct event_data *evt_dat = NULL;
	unsigned long flags;
	uint32_t rc = 0;

	if (job->request_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct get_ct_event)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2613 Received GET_CT_EVENT request below "
				"minimum size\n");
		rc = -EINVAL;
		goto job_error;
	}

	event_req = (struct get_ct_event *)
		job->request->rqst_data.h_vendor.vendor_cmd;

	event_reply = (struct get_ct_event_reply *)
		job->reply->reply_data.vendor_reply.vendor_rsp;
	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	list_for_each_entry(evt, &phba->ct_ev_waiters, node) {
		if (evt->reg_id == event_req->ev_reg_id) {
			if (list_empty(&evt->events_to_get))
				break;
			lpfc_bsg_event_ref(evt);
			evt->wait_time_stamp = jiffies;
			evt_dat = list_entry(evt->events_to_get.prev,
					     struct event_data, node);
			list_del(&evt_dat->node);
			break;
		}
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	/* The app may continue to ask for event data until it gets
	 * an error indicating that there isn't anymore
	 */
	if (evt_dat == NULL) {
		job->reply->reply_payload_rcv_len = 0;
		rc = -ENOENT;
		goto job_error;
	}

	if (evt_dat->len > job->request_payload.payload_len) {
		evt_dat->len = job->request_payload.payload_len;
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2618 Truncated event data at %d "
				"bytes\n",
				job->request_payload.payload_len);
	}

	event_reply->type = evt_dat->type;
	event_reply->immed_data = evt_dat->immed_dat;
	if (evt_dat->len > 0)
		job->reply->reply_payload_rcv_len =
			sg_copy_from_buffer(job->request_payload.sg_list,
					    job->request_payload.sg_cnt,
					    evt_dat->data, evt_dat->len);
	else
		job->reply->reply_payload_rcv_len = 0;

	if (evt_dat) {
		kfree(evt_dat->data);
		kfree(evt_dat);
	}

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	lpfc_bsg_event_unref(evt);
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
	job->dd_data = NULL;
	job->reply->result = 0;
	job->job_done(job);
	return 0;

job_error:
	job->dd_data = NULL;
	job->reply->result = rc;
	return rc;
}

/**
 * lpfc_bsg_hst_vendor - process a vendor-specific fc_bsg_job
 * @job: fc_bsg_job to handle
 */
static int
lpfc_bsg_hst_vendor(struct fc_bsg_job *job)
{
	int command = job->request->rqst_data.h_vendor.vendor_cmd[0];
	int rc;

	switch (command) {
	case LPFC_BSG_VENDOR_SET_CT_EVENT:
		rc = lpfc_bsg_hba_set_event(job);
		break;

	case LPFC_BSG_VENDOR_GET_CT_EVENT:
		rc = lpfc_bsg_hba_get_event(job);
		break;
	default:
		rc = -EINVAL;
		job->reply->reply_payload_rcv_len = 0;
		/* make error code available to userspace */
		job->reply->result = rc;
		break;
	}

	return rc;
}

/**
 * lpfc_bsg_request - handle a bsg request from the FC transport
 * @job: fc_bsg_job to handle
 */
int
lpfc_bsg_request(struct fc_bsg_job *job)
{
	uint32_t msgcode;
	int rc;

	msgcode = job->request->msgcode;
	switch (msgcode) {
	case FC_BSG_HST_VENDOR:
		rc = lpfc_bsg_hst_vendor(job);
		break;
	case FC_BSG_RPT_ELS:
		rc = lpfc_bsg_rport_els(job);
		break;
	case FC_BSG_RPT_CT:
		rc = lpfc_bsg_send_mgmt_cmd(job);
		break;
	default:
		rc = -EINVAL;
		job->reply->reply_payload_rcv_len = 0;
		/* make error code available to userspace */
		job->reply->result = rc;
		break;
	}

	return rc;
}

/**
 * lpfc_bsg_timeout - handle timeout of a bsg request from the FC transport
 * @job: fc_bsg_job that has timed out
 *
 * This function just aborts the job's IOCB.  The aborted IOCB will return to
 * the waiting function which will handle passing the error back to userspace
 */
int
lpfc_bsg_timeout(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_bsg_event *evt;
	struct lpfc_bsg_iocb *iocb;
	struct lpfc_sli_ring *pring = &phba->sli.ring[LPFC_ELS_RING];
	struct bsg_job_data *dd_data;
	unsigned long flags;

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	dd_data = (struct bsg_job_data *)job->dd_data;
	/* timeout and completion crossed paths if no dd_data */
	if (!dd_data) {
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		return 0;
	}

	switch (dd_data->type) {
	case TYPE_IOCB:
		iocb = &dd_data->context_un.iocb;
		cmdiocb = iocb->cmdiocbq;
		/* hint to completion handler that the job timed out */
		job->reply->result = -EAGAIN;
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		/* this will call our completion handler */
		spin_lock_irq(&phba->hbalock);
		lpfc_sli_issue_abort_iotag(phba, pring, cmdiocb);
		spin_unlock_irq(&phba->hbalock);
		break;
	case TYPE_EVT:
		evt = dd_data->context_un.evt;
		/* this event has no job anymore */
		evt->set_job = NULL;
		job->dd_data = NULL;
		job->reply->reply_payload_rcv_len = 0;
		/* Return -EAGAIN which is our way of signallying the
		 * app to retry.
		 */
		job->reply->result = -EAGAIN;
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		job->job_done(job);
		break;
	default:
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		break;
	}

	/* scsi transport fc fc_bsg_job_timeout expects a zero return code,
	 * otherwise an error message will be displayed on the console
	 * so always return success (zero)
	 */
	return 0;
}
