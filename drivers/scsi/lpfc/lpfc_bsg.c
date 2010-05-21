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
#include <linux/slab.h>
#include <linux/delay.h>

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

struct lpfc_bsg_mbox {
	LPFC_MBOXQ_t *pmboxq;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *rxbmp; /* for BIU diags */
	struct lpfc_dmabufext *dmp; /* for BIU diags */
	uint8_t *ext; /* extended mailbox data */
	uint32_t mbOffset; /* from app */
	uint32_t inExtWLen; /* from app */
	uint32_t outExtWLen; /* from app */

	/* job waiting for this mbox command to finish */
	struct fc_bsg_job *set_job;
};

#define MENLO_DID 0x0000FC0E

struct lpfc_bsg_menlo {
	struct lpfc_iocbq *cmdiocbq;
	struct lpfc_iocbq *rspiocbq;
	struct lpfc_dmabuf *bmp;

	/* job waiting for this iocb to finish */
	struct fc_bsg_job *set_job;
};

#define TYPE_EVT 	1
#define TYPE_IOCB	2
#define TYPE_MBOX	3
#define TYPE_MENLO	4
struct bsg_job_data {
	uint32_t type;
	union {
		struct lpfc_bsg_event *evt;
		struct lpfc_bsg_iocb iocb;
		struct lpfc_bsg_mbox mbox;
		struct lpfc_bsg_menlo menlo;
	} context_un;
};

struct event_data {
	struct list_head node;
	uint32_t type;
	uint32_t immed_dat;
	void *data;
	uint32_t len;
};

#define BUF_SZ_4K 4096
#define SLI_CT_ELX_LOOPBACK 0x10

enum ELX_LOOPBACK_CMD {
	ELX_LOOPBACK_XRI_SETUP,
	ELX_LOOPBACK_DATA,
};

#define ELX_LOOPBACK_HEADER_SZ \
	(size_t)(&((struct lpfc_sli_ct_request *)NULL)->un)

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
 **/
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
 * This function copies the contents of the response iocb to the
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
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
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
 **/
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

/**
 * lpfc_bsg_event_free - frees an allocated event structure
 * @kref: Pointer to a kref.
 *
 * Called from kref_put. Back cast the kref into an event structure address.
 * Free any events to get, delete associated nodes, free any events to see,
 * free any data then free the event itself.
 **/
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

/**
 * lpfc_bsg_event_ref - increments the kref for an event
 * @evt: Pointer to an event structure.
 **/
static inline void
lpfc_bsg_event_ref(struct lpfc_bsg_event *evt)
{
	kref_get(&evt->kref);
}

/**
 * lpfc_bsg_event_unref - Uses kref_put to free an event structure
 * @evt: Pointer to an event structure.
 **/
static inline void
lpfc_bsg_event_unref(struct lpfc_bsg_event *evt)
{
	kref_put(&evt->kref, lpfc_bsg_event_free);
}

/**
 * lpfc_bsg_event_new - allocate and initialize a event structure
 * @ev_mask: Mask of events.
 * @ev_reg_id: Event reg id.
 * @ev_req_id: Event request id.
 **/
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

/**
 * diag_cmd_data_free - Frees an lpfc dma buffer extension
 * @phba: Pointer to HBA context object.
 * @mlist: Pointer to an lpfc dma buffer extension.
 **/
static int
diag_cmd_data_free(struct lpfc_hba *phba, struct lpfc_dmabufext *mlist)
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
 **/
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
						diag_cmd_data_free(phba,
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
 **/
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
 **/
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
 * lpfc_issue_ct_rsp_cmp - lpfc_issue_ct_rsp's completion handler
 * @phba: Pointer to HBA context object.
 * @cmdiocbq: Pointer to command iocb.
 * @rspiocbq: Pointer to response iocb.
 *
 * This function is the completion handler for iocbs issued using
 * lpfc_issue_ct_rsp_cmp function. This function is called by the
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
lpfc_issue_ct_rsp_cmp(struct lpfc_hba *phba,
			struct lpfc_iocbq *cmdiocbq,
			struct lpfc_iocbq *rspiocbq)
{
	struct bsg_job_data *dd_data;
	struct fc_bsg_job *job;
	IOCB_t *rsp;
	struct lpfc_dmabuf *bmp;
	struct lpfc_nodelist *ndlp;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	dd_data = cmdiocbq->context1;
	/* normal completion and timeout crossed paths, already done */
	if (!dd_data) {
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		return;
	}

	job = dd_data->context_un.iocb.set_job;
	bmp = dd_data->context_un.iocb.bmp;
	rsp = &rspiocbq->iocb;
	ndlp = dd_data->context_un.iocb.ndlp;

	pci_unmap_sg(phba->pcidev, job->request_payload.sg_list,
		     job->request_payload.sg_cnt, DMA_TO_DEVICE);

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
	lpfc_sli_release_iocbq(phba, cmdiocbq);
	lpfc_nlp_put(ndlp);
	kfree(bmp);
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
 * lpfc_issue_ct_rsp - issue a ct response
 * @phba: Pointer to HBA context object.
 * @job: Pointer to the job object.
 * @tag: tag index value into the ports context exchange array.
 * @bmp: Pointer to a dma buffer descriptor.
 * @num_entry: Number of enties in the bde.
 **/
static int
lpfc_issue_ct_rsp(struct lpfc_hba *phba, struct fc_bsg_job *job, uint32_t tag,
		  struct lpfc_dmabuf *bmp, int num_entry)
{
	IOCB_t *icmd;
	struct lpfc_iocbq *ctiocb = NULL;
	int rc = 0;
	struct lpfc_nodelist *ndlp = NULL;
	struct bsg_job_data *dd_data;
	uint32_t creg_val;

	/* allocate our bsg tracking structure */
	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (!dd_data) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2736 Failed allocation of dd_data\n");
		rc = -ENOMEM;
		goto no_dd_data;
	}

	/* Allocate buffer for  command iocb */
	ctiocb = lpfc_sli_get_iocbq(phba);
	if (!ctiocb) {
		rc = ENOMEM;
		goto no_ctiocb;
	}

	icmd = &ctiocb->iocb;
	icmd->un.xseq64.bdl.ulpIoTag32 = 0;
	icmd->un.xseq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	icmd->un.xseq64.bdl.addrLow = putPaddrLow(bmp->phys);
	icmd->un.xseq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	icmd->un.xseq64.bdl.bdeSize = (num_entry * sizeof(struct ulp_bde64));
	icmd->un.xseq64.w5.hcsw.Fctl = (LS | LA);
	icmd->un.xseq64.w5.hcsw.Dfctl = 0;
	icmd->un.xseq64.w5.hcsw.Rctl = FC_RCTL_DD_SOL_CTL;
	icmd->un.xseq64.w5.hcsw.Type = FC_TYPE_CT;

	/* Fill in rest of iocb */
	icmd->ulpCommand = CMD_XMIT_SEQUENCE64_CX;
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;
	if (phba->sli_rev == LPFC_SLI_REV4) {
		/* Do not issue unsol response if oxid not marked as valid */
		if (!(phba->ct_ctx[tag].flags & UNSOL_VALID)) {
			rc = IOCB_ERROR;
			goto issue_ct_rsp_exit;
		}
		icmd->ulpContext = phba->ct_ctx[tag].oxid;
		ndlp = lpfc_findnode_did(phba->pport, phba->ct_ctx[tag].SID);
		if (!ndlp) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_ELS,
				 "2721 ndlp null for oxid %x SID %x\n",
					icmd->ulpContext,
					phba->ct_ctx[tag].SID);
			rc = IOCB_ERROR;
			goto issue_ct_rsp_exit;
		}
		icmd->un.ulpWord[3] = ndlp->nlp_rpi;
		/* The exchange is done, mark the entry as invalid */
		phba->ct_ctx[tag].flags &= ~UNSOL_VALID;
	} else
		icmd->ulpContext = (ushort) tag;

	icmd->ulpTimeout = phba->fc_ratov * 2;

	/* Xmit CT response on exchange <xid> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"2722 Xmit CT response on exchange x%x Data: x%x x%x\n",
			icmd->ulpContext, icmd->ulpIoTag, phba->link_state);

	ctiocb->iocb_cmpl = NULL;
	ctiocb->iocb_flag |= LPFC_IO_LIBDFC;
	ctiocb->vport = phba->pport;
	ctiocb->context3 = bmp;

	ctiocb->iocb_cmpl = lpfc_issue_ct_rsp_cmp;
	ctiocb->context1 = dd_data;
	ctiocb->context2 = NULL;
	dd_data->type = TYPE_IOCB;
	dd_data->context_un.iocb.cmdiocbq = ctiocb;
	dd_data->context_un.iocb.rspiocbq = NULL;
	dd_data->context_un.iocb.set_job = job;
	dd_data->context_un.iocb.bmp = bmp;
	dd_data->context_un.iocb.ndlp = ndlp;

	if (phba->cfg_poll & DISABLE_FCP_RING_INT) {
		creg_val = readl(phba->HCregaddr);
		creg_val |= (HC_R0INT_ENA << LPFC_FCP_RING);
		writel(creg_val, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, ctiocb, 0);

	if (rc == IOCB_SUCCESS)
		return 0; /* done for now */

issue_ct_rsp_exit:
	lpfc_sli_release_iocbq(phba, ctiocb);
no_ctiocb:
	kfree(dd_data);
no_dd_data:
	return rc;
}

/**
 * lpfc_bsg_send_mgmt_rsp - process a SEND_MGMT_RESP bsg vendor command
 * @job: SEND_MGMT_RESP fc_bsg_job
 **/
static int
lpfc_bsg_send_mgmt_rsp(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct send_mgmt_resp *mgmt_resp = (struct send_mgmt_resp *)
		job->request->rqst_data.h_vendor.vendor_cmd;
	struct ulp_bde64 *bpl;
	struct lpfc_dmabuf *bmp = NULL;
	struct scatterlist *sgel = NULL;
	int request_nseg;
	int numbde;
	dma_addr_t busaddr;
	uint32_t tag = mgmt_resp->tag;
	unsigned long reqbfrcnt =
			(unsigned long)job->request_payload.payload_len;
	int rc = 0;

	/* in case no data is transferred */
	job->reply->reply_payload_rcv_len = 0;

	if (!reqbfrcnt || (reqbfrcnt > (80 * BUF_SZ_4K))) {
		rc = -ERANGE;
		goto send_mgmt_rsp_exit;
	}

	bmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp) {
		rc = -ENOMEM;
		goto send_mgmt_rsp_exit;
	}

	bmp->virt = lpfc_mbuf_alloc(phba, 0, &bmp->phys);
	if (!bmp->virt) {
		rc = -ENOMEM;
		goto send_mgmt_rsp_free_bmp;
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

	rc = lpfc_issue_ct_rsp(phba, job, tag, bmp, request_nseg);

	if (rc == IOCB_SUCCESS)
		return 0; /* done for now */

	/* TBD need to handle a timeout */
	pci_unmap_sg(phba->pcidev, job->request_payload.sg_list,
			  job->request_payload.sg_cnt, DMA_TO_DEVICE);
	rc = -EACCES;
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);

send_mgmt_rsp_free_bmp:
	kfree(bmp);
send_mgmt_rsp_exit:
	/* make error code available to userspace */
	job->reply->result = rc;
	job->dd_data = NULL;
	return rc;
}

/**
 * lpfc_bsg_diag_mode - process a LPFC_BSG_VENDOR_DIAG_MODE bsg vendor command
 * @job: LPFC_BSG_VENDOR_DIAG_MODE
 *
 * This function is responsible for placing a port into diagnostic loopback
 * mode in order to perform a diagnostic loopback test.
 * All new scsi requests are blocked, a small delay is used to allow the
 * scsi requests to complete then the link is brought down. If the link is
 * is placed in loopback mode then scsi requests are again allowed
 * so the scsi mid-layer doesn't give up on the port.
 * All of this is done in-line.
 */
static int
lpfc_bsg_diag_mode(struct fc_bsg_job *job)
{
	struct Scsi_Host *shost = job->shost;
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct diag_mode_set *loopback_mode;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_FCP_RING];
	uint32_t link_flags;
	uint32_t timeout;
	struct lpfc_vport **vports;
	LPFC_MBOXQ_t *pmboxq;
	int mbxstatus;
	int i = 0;
	int rc = 0;

	/* no data to return just the return code */
	job->reply->reply_payload_rcv_len = 0;

	if (job->request_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct diag_mode_set)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2738 Received DIAG MODE request below minimum "
				"size\n");
		rc = -EINVAL;
		goto job_error;
	}

	loopback_mode = (struct diag_mode_set *)
		job->request->rqst_data.h_vendor.vendor_cmd;
	link_flags = loopback_mode->type;
	timeout = loopback_mode->timeout;

	if ((phba->link_state == LPFC_HBA_ERROR) ||
	    (psli->sli_flag & LPFC_BLOCK_MGMT_IO) ||
	    (!(psli->sli_flag & LPFC_SLI_ACTIVE))) {
		rc = -EACCES;
		goto job_error;
	}

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq) {
		rc = -ENOMEM;
		goto job_error;
	}

	vports = lpfc_create_vport_work_array(phba);
	if (vports) {
		for (i = 0; i <= phba->max_vpi && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			scsi_block_requests(shost);
		}

		lpfc_destroy_vport_work_array(phba, vports);
	} else {
		shost = lpfc_shost_from_vport(phba->pport);
		scsi_block_requests(shost);
	}

	while (pring->txcmplq_cnt) {
		if (i++ > 500)	/* wait up to 5 seconds */
			break;

		msleep(10);
	}

	memset((void *)pmboxq, 0, sizeof(LPFC_MBOXQ_t));
	pmboxq->u.mb.mbxCommand = MBX_DOWN_LINK;
	pmboxq->u.mb.mbxOwner = OWN_HOST;

	mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq, LPFC_MBOX_TMO);

	if ((mbxstatus == MBX_SUCCESS) && (pmboxq->u.mb.mbxStatus == 0)) {
		/* wait for link down before proceeding */
		i = 0;
		while (phba->link_state != LPFC_LINK_DOWN) {
			if (i++ > timeout) {
				rc = -ETIMEDOUT;
				goto loopback_mode_exit;
			}

			msleep(10);
		}

		memset((void *)pmboxq, 0, sizeof(LPFC_MBOXQ_t));
		if (link_flags == INTERNAL_LOOP_BACK)
			pmboxq->u.mb.un.varInitLnk.link_flags = FLAGS_LOCAL_LB;
		else
			pmboxq->u.mb.un.varInitLnk.link_flags =
				FLAGS_TOPOLOGY_MODE_LOOP;

		pmboxq->u.mb.mbxCommand = MBX_INIT_LINK;
		pmboxq->u.mb.mbxOwner = OWN_HOST;

		mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq,
						     LPFC_MBOX_TMO);

		if ((mbxstatus != MBX_SUCCESS) || (pmboxq->u.mb.mbxStatus))
			rc = -ENODEV;
		else {
			phba->link_flag |= LS_LOOPBACK_MODE;
			/* wait for the link attention interrupt */
			msleep(100);

			i = 0;
			while (phba->link_state != LPFC_HBA_READY) {
				if (i++ > timeout) {
					rc = -ETIMEDOUT;
					break;
				}

				msleep(10);
			}
		}

	} else
		rc = -ENODEV;

loopback_mode_exit:
	vports = lpfc_create_vport_work_array(phba);
	if (vports) {
		for (i = 0; i <= phba->max_vpi && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			scsi_unblock_requests(shost);
		}
		lpfc_destroy_vport_work_array(phba, vports);
	} else {
		shost = lpfc_shost_from_vport(phba->pport);
		scsi_unblock_requests(shost);
	}

	/*
	 * Let SLI layer release mboxq if mbox command completed after timeout.
	 */
	if (mbxstatus != MBX_TIMEOUT)
		mempool_free(pmboxq, phba->mbox_mem_pool);

job_error:
	/* make error code available to userspace */
	job->reply->result = rc;
	/* complete the job back to userspace if no error */
	if (rc == 0)
		job->job_done(job);
	return rc;
}

/**
 * lpfcdiag_loop_self_reg - obtains a remote port login id
 * @phba: Pointer to HBA context object
 * @rpi: Pointer to a remote port login id
 *
 * This function obtains a remote port login id so the diag loopback test
 * can send and receive its own unsolicited CT command.
 **/
static int lpfcdiag_loop_self_reg(struct lpfc_hba *phba, uint16_t * rpi)
{
	LPFC_MBOXQ_t *mbox;
	struct lpfc_dmabuf *dmabuff;
	int status;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox)
		return ENOMEM;

	status = lpfc_reg_rpi(phba, 0, phba->pport->fc_myDID,
				(uint8_t *)&phba->pport->fc_sparam, mbox, 0);
	if (status) {
		mempool_free(mbox, phba->mbox_mem_pool);
		return ENOMEM;
	}

	dmabuff = (struct lpfc_dmabuf *) mbox->context1;
	mbox->context1 = NULL;
	status = lpfc_sli_issue_mbox_wait(phba, mbox, LPFC_MBOX_TMO);

	if ((status != MBX_SUCCESS) || (mbox->u.mb.mbxStatus)) {
		lpfc_mbuf_free(phba, dmabuff->virt, dmabuff->phys);
		kfree(dmabuff);
		if (status != MBX_TIMEOUT)
			mempool_free(mbox, phba->mbox_mem_pool);
		return ENODEV;
	}

	*rpi = mbox->u.mb.un.varWords[0];

	lpfc_mbuf_free(phba, dmabuff->virt, dmabuff->phys);
	kfree(dmabuff);
	mempool_free(mbox, phba->mbox_mem_pool);
	return 0;
}

/**
 * lpfcdiag_loop_self_unreg - unregs from the rpi
 * @phba: Pointer to HBA context object
 * @rpi: Remote port login id
 *
 * This function unregisters the rpi obtained in lpfcdiag_loop_self_reg
 **/
static int lpfcdiag_loop_self_unreg(struct lpfc_hba *phba, uint16_t rpi)
{
	LPFC_MBOXQ_t *mbox;
	int status;

	/* Allocate mboxq structure */
	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mbox == NULL)
		return ENOMEM;

	lpfc_unreg_login(phba, 0, rpi, mbox);
	status = lpfc_sli_issue_mbox_wait(phba, mbox, LPFC_MBOX_TMO);

	if ((status != MBX_SUCCESS) || (mbox->u.mb.mbxStatus)) {
		if (status != MBX_TIMEOUT)
			mempool_free(mbox, phba->mbox_mem_pool);
		return EIO;
	}

	mempool_free(mbox, phba->mbox_mem_pool);
	return 0;
}

/**
 * lpfcdiag_loop_get_xri - obtains the transmit and receive ids
 * @phba: Pointer to HBA context object
 * @rpi: Remote port login id
 * @txxri: Pointer to transmit exchange id
 * @rxxri: Pointer to response exchabge id
 *
 * This function obtains the transmit and receive ids required to send
 * an unsolicited ct command with a payload. A special lpfc FsType and CmdRsp
 * flags are used to the unsolicted response handler is able to process
 * the ct command sent on the same port.
 **/
static int lpfcdiag_loop_get_xri(struct lpfc_hba *phba, uint16_t rpi,
			 uint16_t *txxri, uint16_t * rxxri)
{
	struct lpfc_bsg_event *evt;
	struct lpfc_iocbq *cmdiocbq, *rspiocbq;
	IOCB_t *cmd, *rsp;
	struct lpfc_dmabuf *dmabuf;
	struct ulp_bde64 *bpl = NULL;
	struct lpfc_sli_ct_request *ctreq = NULL;
	int ret_val = 0;
	unsigned long flags;

	*txxri = 0;
	*rxxri = 0;
	evt = lpfc_bsg_event_new(FC_REG_CT_EVENT, current->pid,
				SLI_CT_ELX_LOOPBACK);
	if (!evt)
		return ENOMEM;

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	list_add(&evt->node, &phba->ct_ev_waiters);
	lpfc_bsg_event_ref(evt);
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	rspiocbq = lpfc_sli_get_iocbq(phba);

	dmabuf = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (dmabuf) {
		dmabuf->virt = lpfc_mbuf_alloc(phba, 0, &dmabuf->phys);
		if (dmabuf->virt) {
			INIT_LIST_HEAD(&dmabuf->list);
			bpl = (struct ulp_bde64 *) dmabuf->virt;
			memset(bpl, 0, sizeof(*bpl));
			ctreq = (struct lpfc_sli_ct_request *)(bpl + 1);
			bpl->addrHigh =
				le32_to_cpu(putPaddrHigh(dmabuf->phys +
					sizeof(*bpl)));
			bpl->addrLow =
				le32_to_cpu(putPaddrLow(dmabuf->phys +
					sizeof(*bpl)));
			bpl->tus.f.bdeFlags = 0;
			bpl->tus.f.bdeSize = ELX_LOOPBACK_HEADER_SZ;
			bpl->tus.w = le32_to_cpu(bpl->tus.w);
		}
	}

	if (cmdiocbq == NULL || rspiocbq == NULL ||
	    dmabuf == NULL || bpl == NULL || ctreq == NULL ||
		dmabuf->virt == NULL) {
		ret_val = ENOMEM;
		goto err_get_xri_exit;
	}

	cmd = &cmdiocbq->iocb;
	rsp = &rspiocbq->iocb;

	memset(ctreq, 0, ELX_LOOPBACK_HEADER_SZ);

	ctreq->RevisionId.bits.Revision = SLI_CT_REVISION;
	ctreq->RevisionId.bits.InId = 0;
	ctreq->FsType = SLI_CT_ELX_LOOPBACK;
	ctreq->FsSubType = 0;
	ctreq->CommandResponse.bits.CmdRsp = ELX_LOOPBACK_XRI_SETUP;
	ctreq->CommandResponse.bits.Size = 0;


	cmd->un.xseq64.bdl.addrHigh = putPaddrHigh(dmabuf->phys);
	cmd->un.xseq64.bdl.addrLow = putPaddrLow(dmabuf->phys);
	cmd->un.xseq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	cmd->un.xseq64.bdl.bdeSize = sizeof(*bpl);

	cmd->un.xseq64.w5.hcsw.Fctl = LA;
	cmd->un.xseq64.w5.hcsw.Dfctl = 0;
	cmd->un.xseq64.w5.hcsw.Rctl = FC_RCTL_DD_UNSOL_CTL;
	cmd->un.xseq64.w5.hcsw.Type = FC_TYPE_CT;

	cmd->ulpCommand = CMD_XMIT_SEQUENCE64_CR;
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = rpi;

	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	cmdiocbq->vport = phba->pport;

	ret_val = lpfc_sli_issue_iocb_wait(phba, LPFC_ELS_RING, cmdiocbq,
				rspiocbq,
				(phba->fc_ratov * 2)
				+ LPFC_DRVR_TIMEOUT);
	if (ret_val)
		goto err_get_xri_exit;

	*txxri =  rsp->ulpContext;

	evt->waiting = 1;
	evt->wait_time_stamp = jiffies;
	ret_val = wait_event_interruptible_timeout(
		evt->wq, !list_empty(&evt->events_to_see),
		((phba->fc_ratov * 2) + LPFC_DRVR_TIMEOUT) * HZ);
	if (list_empty(&evt->events_to_see))
		ret_val = (ret_val) ? EINTR : ETIMEDOUT;
	else {
		ret_val = IOCB_SUCCESS;
		spin_lock_irqsave(&phba->ct_ev_lock, flags);
		list_move(evt->events_to_see.prev, &evt->events_to_get);
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		*rxxri = (list_entry(evt->events_to_get.prev,
				     typeof(struct event_data),
				     node))->immed_dat;
	}
	evt->waiting = 0;

err_get_xri_exit:
	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	lpfc_bsg_event_unref(evt); /* release ref */
	lpfc_bsg_event_unref(evt); /* delete */
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	if (dmabuf) {
		if (dmabuf->virt)
			lpfc_mbuf_free(phba, dmabuf->virt, dmabuf->phys);
		kfree(dmabuf);
	}

	if (cmdiocbq && (ret_val != IOCB_TIMEDOUT))
		lpfc_sli_release_iocbq(phba, cmdiocbq);
	if (rspiocbq)
		lpfc_sli_release_iocbq(phba, rspiocbq);
	return ret_val;
}

/**
 * diag_cmd_data_alloc - fills in a bde struct with dma buffers
 * @phba: Pointer to HBA context object
 * @bpl: Pointer to 64 bit bde structure
 * @size: Number of bytes to process
 * @nocopydata: Flag to copy user data into the allocated buffer
 *
 * This function allocates page size buffers and populates an lpfc_dmabufext.
 * If allowed the user data pointed to with indataptr is copied into the kernel
 * memory. The chained list of page size buffers is returned.
 **/
static struct lpfc_dmabufext *
diag_cmd_data_alloc(struct lpfc_hba *phba,
		   struct ulp_bde64 *bpl, uint32_t size,
		   int nocopydata)
{
	struct lpfc_dmabufext *mlist = NULL;
	struct lpfc_dmabufext *dmp;
	int cnt, offset = 0, i = 0;
	struct pci_dev *pcidev;

	pcidev = phba->pcidev;

	while (size) {
		/* We get chunks of 4K */
		if (size > BUF_SZ_4K)
			cnt = BUF_SZ_4K;
		else
			cnt = size;

		/* allocate struct lpfc_dmabufext buffer header */
		dmp = kmalloc(sizeof(struct lpfc_dmabufext), GFP_KERNEL);
		if (!dmp)
			goto out;

		INIT_LIST_HEAD(&dmp->dma.list);

		/* Queue it to a linked list */
		if (mlist)
			list_add_tail(&dmp->dma.list, &mlist->dma.list);
		else
			mlist = dmp;

		/* allocate buffer */
		dmp->dma.virt = dma_alloc_coherent(&pcidev->dev,
						   cnt,
						   &(dmp->dma.phys),
						   GFP_KERNEL);

		if (!dmp->dma.virt)
			goto out;

		dmp->size = cnt;

		if (nocopydata) {
			bpl->tus.f.bdeFlags = 0;
			pci_dma_sync_single_for_device(phba->pcidev,
				dmp->dma.phys, LPFC_BPL_SIZE, PCI_DMA_TODEVICE);

		} else {
			memset((uint8_t *)dmp->dma.virt, 0, cnt);
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
		}

		/* build buffer ptr list for IOCB */
		bpl->addrLow = le32_to_cpu(putPaddrLow(dmp->dma.phys));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(dmp->dma.phys));
		bpl->tus.f.bdeSize = (ushort) cnt;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;

		i++;
		offset += cnt;
		size -= cnt;
	}

	mlist->flag = i;
	return mlist;
out:
	diag_cmd_data_free(phba, mlist);
	return NULL;
}

/**
 * lpfcdiag_loop_post_rxbufs - post the receive buffers for an unsol CT cmd
 * @phba: Pointer to HBA context object
 * @rxxri: Receive exchange id
 * @len: Number of data bytes
 *
 * This function allocates and posts a data buffer of sufficient size to recieve
 * an unsolicted CT command.
 **/
static int lpfcdiag_loop_post_rxbufs(struct lpfc_hba *phba, uint16_t rxxri,
			     size_t len)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	struct lpfc_iocbq *cmdiocbq;
	IOCB_t *cmd = NULL;
	struct list_head head, *curr, *next;
	struct lpfc_dmabuf *rxbmp;
	struct lpfc_dmabuf *dmp;
	struct lpfc_dmabuf *mp[2] = {NULL, NULL};
	struct ulp_bde64 *rxbpl = NULL;
	uint32_t num_bde;
	struct lpfc_dmabufext *rxbuffer = NULL;
	int ret_val = 0;
	int i = 0;

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	rxbmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (rxbmp != NULL) {
		rxbmp->virt = lpfc_mbuf_alloc(phba, 0, &rxbmp->phys);
		if (rxbmp->virt) {
			INIT_LIST_HEAD(&rxbmp->list);
			rxbpl = (struct ulp_bde64 *) rxbmp->virt;
			rxbuffer = diag_cmd_data_alloc(phba, rxbpl, len, 0);
		}
	}

	if (!cmdiocbq || !rxbmp || !rxbpl || !rxbuffer) {
		ret_val = ENOMEM;
		goto err_post_rxbufs_exit;
	}

	/* Queue buffers for the receive exchange */
	num_bde = (uint32_t)rxbuffer->flag;
	dmp = &rxbuffer->dma;

	cmd = &cmdiocbq->iocb;
	i = 0;

	INIT_LIST_HEAD(&head);
	list_add_tail(&head, &dmp->list);
	list_for_each_safe(curr, next, &head) {
		mp[i] = list_entry(curr, struct lpfc_dmabuf, list);
		list_del(curr);

		if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
			mp[i]->buffer_tag = lpfc_sli_get_buffer_tag(phba);
			cmd->un.quexri64cx.buff.bde.addrHigh =
				putPaddrHigh(mp[i]->phys);
			cmd->un.quexri64cx.buff.bde.addrLow =
				putPaddrLow(mp[i]->phys);
			cmd->un.quexri64cx.buff.bde.tus.f.bdeSize =
				((struct lpfc_dmabufext *)mp[i])->size;
			cmd->un.quexri64cx.buff.buffer_tag = mp[i]->buffer_tag;
			cmd->ulpCommand = CMD_QUE_XRI64_CX;
			cmd->ulpPU = 0;
			cmd->ulpLe = 1;
			cmd->ulpBdeCount = 1;
			cmd->unsli3.que_xri64cx_ext_words.ebde_count = 0;

		} else {
			cmd->un.cont64[i].addrHigh = putPaddrHigh(mp[i]->phys);
			cmd->un.cont64[i].addrLow = putPaddrLow(mp[i]->phys);
			cmd->un.cont64[i].tus.f.bdeSize =
				((struct lpfc_dmabufext *)mp[i])->size;
					cmd->ulpBdeCount = ++i;

			if ((--num_bde > 0) && (i < 2))
				continue;

			cmd->ulpCommand = CMD_QUE_XRI_BUF64_CX;
			cmd->ulpLe = 1;
		}

		cmd->ulpClass = CLASS3;
		cmd->ulpContext = rxxri;

		ret_val = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, cmdiocbq, 0);

		if (ret_val == IOCB_ERROR) {
			diag_cmd_data_free(phba,
				(struct lpfc_dmabufext *)mp[0]);
			if (mp[1])
				diag_cmd_data_free(phba,
					  (struct lpfc_dmabufext *)mp[1]);
			dmp = list_entry(next, struct lpfc_dmabuf, list);
			ret_val = EIO;
			goto err_post_rxbufs_exit;
		}

		lpfc_sli_ringpostbuf_put(phba, pring, mp[0]);
		if (mp[1]) {
			lpfc_sli_ringpostbuf_put(phba, pring, mp[1]);
			mp[1] = NULL;
		}

		/* The iocb was freed by lpfc_sli_issue_iocb */
		cmdiocbq = lpfc_sli_get_iocbq(phba);
		if (!cmdiocbq) {
			dmp = list_entry(next, struct lpfc_dmabuf, list);
			ret_val = EIO;
			goto err_post_rxbufs_exit;
		}

		cmd = &cmdiocbq->iocb;
		i = 0;
	}
	list_del(&head);

err_post_rxbufs_exit:

	if (rxbmp) {
		if (rxbmp->virt)
			lpfc_mbuf_free(phba, rxbmp->virt, rxbmp->phys);
		kfree(rxbmp);
	}

	if (cmdiocbq)
		lpfc_sli_release_iocbq(phba, cmdiocbq);
	return ret_val;
}

/**
 * lpfc_bsg_diag_test - with a port in loopback issues a Ct cmd to itself
 * @job: LPFC_BSG_VENDOR_DIAG_TEST fc_bsg_job
 *
 * This function receives a user data buffer to be transmitted and received on
 * the same port, the link must be up and in loopback mode prior
 * to being called.
 * 1. A kernel buffer is allocated to copy the user data into.
 * 2. The port registers with "itself".
 * 3. The transmit and receive exchange ids are obtained.
 * 4. The receive exchange id is posted.
 * 5. A new els loopback event is created.
 * 6. The command and response iocbs are allocated.
 * 7. The cmd iocb FsType is set to elx loopback and the CmdRsp to looppback.
 *
 * This function is meant to be called n times while the port is in loopback
 * so it is the apps responsibility to issue a reset to take the port out
 * of loopback mode.
 **/
static int
lpfc_bsg_diag_test(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct diag_mode_test *diag_mode;
	struct lpfc_bsg_event *evt;
	struct event_data *evdat;
	struct lpfc_sli *psli = &phba->sli;
	uint32_t size;
	uint32_t full_size;
	size_t segment_len = 0, segment_offset = 0, current_offset = 0;
	uint16_t rpi;
	struct lpfc_iocbq *cmdiocbq, *rspiocbq;
	IOCB_t *cmd, *rsp;
	struct lpfc_sli_ct_request *ctreq;
	struct lpfc_dmabuf *txbmp;
	struct ulp_bde64 *txbpl = NULL;
	struct lpfc_dmabufext *txbuffer = NULL;
	struct list_head head;
	struct lpfc_dmabuf  *curr;
	uint16_t txxri, rxxri;
	uint32_t num_bde;
	uint8_t *ptr = NULL, *rx_databuf = NULL;
	int rc = 0;
	unsigned long flags;
	void *dataout = NULL;
	uint32_t total_mem;

	/* in case no data is returned return just the return code */
	job->reply->reply_payload_rcv_len = 0;

	if (job->request_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct diag_mode_test)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2739 Received DIAG TEST request below minimum "
				"size\n");
		rc = -EINVAL;
		goto loopback_test_exit;
	}

	if (job->request_payload.payload_len !=
		job->reply_payload.payload_len) {
		rc = -EINVAL;
		goto loopback_test_exit;
	}

	diag_mode = (struct diag_mode_test *)
		job->request->rqst_data.h_vendor.vendor_cmd;

	if ((phba->link_state == LPFC_HBA_ERROR) ||
	    (psli->sli_flag & LPFC_BLOCK_MGMT_IO) ||
	    (!(psli->sli_flag & LPFC_SLI_ACTIVE))) {
		rc = -EACCES;
		goto loopback_test_exit;
	}

	if (!lpfc_is_link_up(phba) || !(phba->link_flag & LS_LOOPBACK_MODE)) {
		rc = -EACCES;
		goto loopback_test_exit;
	}

	size = job->request_payload.payload_len;
	full_size = size + ELX_LOOPBACK_HEADER_SZ; /* plus the header */

	if ((size == 0) || (size > 80 * BUF_SZ_4K)) {
		rc = -ERANGE;
		goto loopback_test_exit;
	}

	if (size >= BUF_SZ_4K) {
		/*
		 * Allocate memory for ioctl data. If buffer is bigger than 64k,
		 * then we allocate 64k and re-use that buffer over and over to
		 * xfer the whole block. This is because Linux kernel has a
		 * problem allocating more than 120k of kernel space memory. Saw
		 * problem with GET_FCPTARGETMAPPING...
		 */
		if (size <= (64 * 1024))
			total_mem = size;
		else
			total_mem = 64 * 1024;
	} else
		/* Allocate memory for ioctl data */
		total_mem = BUF_SZ_4K;

	dataout = kmalloc(total_mem, GFP_KERNEL);
	if (dataout == NULL) {
		rc = -ENOMEM;
		goto loopback_test_exit;
	}

	ptr = dataout;
	ptr += ELX_LOOPBACK_HEADER_SZ;
	sg_copy_to_buffer(job->request_payload.sg_list,
				job->request_payload.sg_cnt,
				ptr, size);

	rc = lpfcdiag_loop_self_reg(phba, &rpi);
	if (rc) {
		rc = -ENOMEM;
		goto loopback_test_exit;
	}

	rc = lpfcdiag_loop_get_xri(phba, rpi, &txxri, &rxxri);
	if (rc) {
		lpfcdiag_loop_self_unreg(phba, rpi);
		rc = -ENOMEM;
		goto loopback_test_exit;
	}

	rc = lpfcdiag_loop_post_rxbufs(phba, rxxri, full_size);
	if (rc) {
		lpfcdiag_loop_self_unreg(phba, rpi);
		rc = -ENOMEM;
		goto loopback_test_exit;
	}

	evt = lpfc_bsg_event_new(FC_REG_CT_EVENT, current->pid,
				SLI_CT_ELX_LOOPBACK);
	if (!evt) {
		lpfcdiag_loop_self_unreg(phba, rpi);
		rc = -ENOMEM;
		goto loopback_test_exit;
	}

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	list_add(&evt->node, &phba->ct_ev_waiters);
	lpfc_bsg_event_ref(evt);
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	rspiocbq = lpfc_sli_get_iocbq(phba);
	txbmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);

	if (txbmp) {
		txbmp->virt = lpfc_mbuf_alloc(phba, 0, &txbmp->phys);
		if (txbmp->virt) {
			INIT_LIST_HEAD(&txbmp->list);
			txbpl = (struct ulp_bde64 *) txbmp->virt;
			txbuffer = diag_cmd_data_alloc(phba,
							txbpl, full_size, 0);
		}
	}

	if (!cmdiocbq || !rspiocbq || !txbmp || !txbpl || !txbuffer ||
		!txbmp->virt) {
		rc = -ENOMEM;
		goto err_loopback_test_exit;
	}

	cmd = &cmdiocbq->iocb;
	rsp = &rspiocbq->iocb;

	INIT_LIST_HEAD(&head);
	list_add_tail(&head, &txbuffer->dma.list);
	list_for_each_entry(curr, &head, list) {
		segment_len = ((struct lpfc_dmabufext *)curr)->size;
		if (current_offset == 0) {
			ctreq = curr->virt;
			memset(ctreq, 0, ELX_LOOPBACK_HEADER_SZ);
			ctreq->RevisionId.bits.Revision = SLI_CT_REVISION;
			ctreq->RevisionId.bits.InId = 0;
			ctreq->FsType = SLI_CT_ELX_LOOPBACK;
			ctreq->FsSubType = 0;
			ctreq->CommandResponse.bits.CmdRsp = ELX_LOOPBACK_DATA;
			ctreq->CommandResponse.bits.Size   = size;
			segment_offset = ELX_LOOPBACK_HEADER_SZ;
		} else
			segment_offset = 0;

		BUG_ON(segment_offset >= segment_len);
		memcpy(curr->virt + segment_offset,
			ptr + current_offset,
			segment_len - segment_offset);

		current_offset += segment_len - segment_offset;
		BUG_ON(current_offset > size);
	}
	list_del(&head);

	/* Build the XMIT_SEQUENCE iocb */

	num_bde = (uint32_t)txbuffer->flag;

	cmd->un.xseq64.bdl.addrHigh = putPaddrHigh(txbmp->phys);
	cmd->un.xseq64.bdl.addrLow = putPaddrLow(txbmp->phys);
	cmd->un.xseq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	cmd->un.xseq64.bdl.bdeSize = (num_bde * sizeof(struct ulp_bde64));

	cmd->un.xseq64.w5.hcsw.Fctl = (LS | LA);
	cmd->un.xseq64.w5.hcsw.Dfctl = 0;
	cmd->un.xseq64.w5.hcsw.Rctl = FC_RCTL_DD_UNSOL_CTL;
	cmd->un.xseq64.w5.hcsw.Type = FC_TYPE_CT;

	cmd->ulpCommand = CMD_XMIT_SEQUENCE64_CX;
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = txxri;

	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	cmdiocbq->vport = phba->pport;

	rc = lpfc_sli_issue_iocb_wait(phba, LPFC_ELS_RING, cmdiocbq, rspiocbq,
				      (phba->fc_ratov * 2) + LPFC_DRVR_TIMEOUT);

	if ((rc != IOCB_SUCCESS) || (rsp->ulpStatus != IOCB_SUCCESS)) {
		rc = -EIO;
		goto err_loopback_test_exit;
	}

	evt->waiting = 1;
	rc = wait_event_interruptible_timeout(
		evt->wq, !list_empty(&evt->events_to_see),
		((phba->fc_ratov * 2) + LPFC_DRVR_TIMEOUT) * HZ);
	evt->waiting = 0;
	if (list_empty(&evt->events_to_see))
		rc = (rc) ? -EINTR : -ETIMEDOUT;
	else {
		spin_lock_irqsave(&phba->ct_ev_lock, flags);
		list_move(evt->events_to_see.prev, &evt->events_to_get);
		evdat = list_entry(evt->events_to_get.prev,
				   typeof(*evdat), node);
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		rx_databuf = evdat->data;
		if (evdat->len != full_size) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"1603 Loopback test did not receive expected "
				"data length. actual length 0x%x expected "
				"length 0x%x\n",
				evdat->len, full_size);
			rc = -EIO;
		} else if (rx_databuf == NULL)
			rc = -EIO;
		else {
			rc = IOCB_SUCCESS;
			/* skip over elx loopback header */
			rx_databuf += ELX_LOOPBACK_HEADER_SZ;
			job->reply->reply_payload_rcv_len =
				sg_copy_from_buffer(job->reply_payload.sg_list,
						    job->reply_payload.sg_cnt,
						    rx_databuf, size);
			job->reply->reply_payload_rcv_len = size;
		}
	}

err_loopback_test_exit:
	lpfcdiag_loop_self_unreg(phba, rpi);

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	lpfc_bsg_event_unref(evt); /* release ref */
	lpfc_bsg_event_unref(evt); /* delete */
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	if (cmdiocbq != NULL)
		lpfc_sli_release_iocbq(phba, cmdiocbq);

	if (rspiocbq != NULL)
		lpfc_sli_release_iocbq(phba, rspiocbq);

	if (txbmp != NULL) {
		if (txbpl != NULL) {
			if (txbuffer != NULL)
				diag_cmd_data_free(phba, txbuffer);
			lpfc_mbuf_free(phba, txbmp->virt, txbmp->phys);
		}
		kfree(txbmp);
	}

loopback_test_exit:
	kfree(dataout);
	/* make error code available to userspace */
	job->reply->result = rc;
	job->dd_data = NULL;
	/* complete the job back to userspace if no error */
	if (rc == 0)
		job->job_done(job);
	return rc;
}

/**
 * lpfc_bsg_get_dfc_rev - process a GET_DFC_REV bsg vendor command
 * @job: GET_DFC_REV fc_bsg_job
 **/
static int
lpfc_bsg_get_dfc_rev(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct get_mgmt_rev *event_req;
	struct get_mgmt_rev_reply *event_reply;
	int rc = 0;

	if (job->request_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct get_mgmt_rev)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2740 Received GET_DFC_REV request below "
				"minimum size\n");
		rc = -EINVAL;
		goto job_error;
	}

	event_req = (struct get_mgmt_rev *)
		job->request->rqst_data.h_vendor.vendor_cmd;

	event_reply = (struct get_mgmt_rev_reply *)
		job->reply->reply_data.vendor_reply.vendor_rsp;

	if (job->reply_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct get_mgmt_rev_reply)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2741 Received GET_DFC_REV reply below "
				"minimum size\n");
		rc = -EINVAL;
		goto job_error;
	}

	event_reply->info.a_Major = MANAGEMENT_MAJOR_REV;
	event_reply->info.a_Minor = MANAGEMENT_MINOR_REV;
job_error:
	job->reply->result = rc;
	if (rc == 0)
		job->job_done(job);
	return rc;
}

/**
 * lpfc_bsg_wake_mbox_wait - lpfc_bsg_issue_mbox mbox completion handler
 * @phba: Pointer to HBA context object.
 * @pmboxq: Pointer to mailbox command.
 *
 * This is completion handler function for mailbox commands issued from
 * lpfc_bsg_issue_mbox function. This function is called by the
 * mailbox event handler function with no lock held. This function
 * will wake up thread waiting on the wait queue pointed by context1
 * of the mailbox.
 **/
void
lpfc_bsg_wake_mbox_wait(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmboxq)
{
	struct bsg_job_data *dd_data;
	struct fc_bsg_job *job;
	uint32_t size;
	unsigned long flags;
	uint8_t *to;
	uint8_t *from;

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	dd_data = pmboxq->context1;
	/* job already timed out? */
	if (!dd_data) {
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		return;
	}

	/* build the outgoing buffer to do an sg copy
	 * the format is the response mailbox followed by any extended
	 * mailbox data
	 */
	from = (uint8_t *)&pmboxq->u.mb;
	to = (uint8_t *)dd_data->context_un.mbox.mb;
	memcpy(to, from, sizeof(MAILBOX_t));
	if (pmboxq->u.mb.mbxStatus == MBX_SUCCESS) {
		/* copy the extended data if any, count is in words */
		if (dd_data->context_un.mbox.outExtWLen) {
			from = (uint8_t *)dd_data->context_un.mbox.ext;
			to += sizeof(MAILBOX_t);
			size = dd_data->context_un.mbox.outExtWLen *
					sizeof(uint32_t);
			memcpy(to, from, size);
		} else if (pmboxq->u.mb.mbxCommand == MBX_RUN_BIU_DIAG64) {
			from = (uint8_t *)dd_data->context_un.mbox.
						dmp->dma.virt;
			to += sizeof(MAILBOX_t);
			size = dd_data->context_un.mbox.dmp->size;
			memcpy(to, from, size);
		} else if ((phba->sli_rev == LPFC_SLI_REV4) &&
			(pmboxq->u.mb.mbxCommand == MBX_DUMP_MEMORY)) {
			from = (uint8_t *)dd_data->context_un.mbox.dmp->dma.
						virt;
			to += sizeof(MAILBOX_t);
			size = pmboxq->u.mb.un.varWords[5];
			memcpy(to, from, size);
		} else if (pmboxq->u.mb.mbxCommand == MBX_READ_EVENT_LOG) {
			from = (uint8_t *)dd_data->context_un.
						mbox.dmp->dma.virt;
			to += sizeof(MAILBOX_t);
			size = dd_data->context_un.mbox.dmp->size;
			memcpy(to, from, size);
		}
	}

	from = (uint8_t *)dd_data->context_un.mbox.mb;
	job = dd_data->context_un.mbox.set_job;
	size = job->reply_payload.payload_len;
	job->reply->reply_payload_rcv_len =
		sg_copy_from_buffer(job->reply_payload.sg_list,
				job->reply_payload.sg_cnt,
				from, size);
	job->reply->result = 0;

	dd_data->context_un.mbox.set_job = NULL;
	job->dd_data = NULL;
	job->job_done(job);
	/* need to hold the lock until we call job done to hold off
	 * the timeout handler returning to the midlayer while
	 * we are stillprocessing the job
	 */
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	kfree(dd_data->context_un.mbox.mb);
	mempool_free(dd_data->context_un.mbox.pmboxq, phba->mbox_mem_pool);
	kfree(dd_data->context_un.mbox.ext);
	if (dd_data->context_un.mbox.dmp) {
		dma_free_coherent(&phba->pcidev->dev,
			dd_data->context_un.mbox.dmp->size,
			dd_data->context_un.mbox.dmp->dma.virt,
			dd_data->context_un.mbox.dmp->dma.phys);
		kfree(dd_data->context_un.mbox.dmp);
	}
	if (dd_data->context_un.mbox.rxbmp) {
		lpfc_mbuf_free(phba, dd_data->context_un.mbox.rxbmp->virt,
			dd_data->context_un.mbox.rxbmp->phys);
		kfree(dd_data->context_un.mbox.rxbmp);
	}
	kfree(dd_data);
	return;
}

/**
 * lpfc_bsg_check_cmd_access - test for a supported mailbox command
 * @phba: Pointer to HBA context object.
 * @mb: Pointer to a mailbox object.
 * @vport: Pointer to a vport object.
 *
 * Some commands require the port to be offline, some may not be called from
 * the application.
 **/
static int lpfc_bsg_check_cmd_access(struct lpfc_hba *phba,
	MAILBOX_t *mb, struct lpfc_vport *vport)
{
	/* return negative error values for bsg job */
	switch (mb->mbxCommand) {
	/* Offline only */
	case MBX_INIT_LINK:
	case MBX_DOWN_LINK:
	case MBX_CONFIG_LINK:
	case MBX_CONFIG_RING:
	case MBX_RESET_RING:
	case MBX_UNREG_LOGIN:
	case MBX_CLEAR_LA:
	case MBX_DUMP_CONTEXT:
	case MBX_RUN_DIAGS:
	case MBX_RESTART:
	case MBX_SET_MASK:
		if (!(vport->fc_flag & FC_OFFLINE_MODE)) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2743 Command 0x%x is illegal in on-line "
				"state\n",
				mb->mbxCommand);
			return -EPERM;
		}
	case MBX_WRITE_NV:
	case MBX_WRITE_VPARMS:
	case MBX_LOAD_SM:
	case MBX_READ_NV:
	case MBX_READ_CONFIG:
	case MBX_READ_RCONFIG:
	case MBX_READ_STATUS:
	case MBX_READ_XRI:
	case MBX_READ_REV:
	case MBX_READ_LNK_STAT:
	case MBX_DUMP_MEMORY:
	case MBX_DOWN_LOAD:
	case MBX_UPDATE_CFG:
	case MBX_KILL_BOARD:
	case MBX_LOAD_AREA:
	case MBX_LOAD_EXP_ROM:
	case MBX_BEACON:
	case MBX_DEL_LD_ENTRY:
	case MBX_SET_DEBUG:
	case MBX_WRITE_WWN:
	case MBX_SLI4_CONFIG:
	case MBX_READ_EVENT_LOG:
	case MBX_READ_EVENT_LOG_STATUS:
	case MBX_WRITE_EVENT_LOG:
	case MBX_PORT_CAPABILITIES:
	case MBX_PORT_IOV_CONTROL:
	case MBX_RUN_BIU_DIAG64:
		break;
	case MBX_SET_VARIABLE:
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"1226 mbox: set_variable 0x%x, 0x%x\n",
			mb->un.varWords[0],
			mb->un.varWords[1]);
		if ((mb->un.varWords[0] == SETVAR_MLOMNT)
			&& (mb->un.varWords[1] == 1)) {
			phba->wait_4_mlo_maint_flg = 1;
		} else if (mb->un.varWords[0] == SETVAR_MLORST) {
			phba->link_flag &= ~LS_LOOPBACK_MODE;
			phba->fc_topology = TOPOLOGY_PT_PT;
		}
		break;
	case MBX_READ_SPARM64:
	case MBX_READ_LA:
	case MBX_READ_LA64:
	case MBX_REG_LOGIN:
	case MBX_REG_LOGIN64:
	case MBX_CONFIG_PORT:
	case MBX_RUN_BIU_DIAG:
	default:
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
			"2742 Unknown Command 0x%x\n",
			mb->mbxCommand);
		return -EPERM;
	}

	return 0; /* ok */
}

/**
 * lpfc_bsg_issue_mbox - issues a mailbox command on behalf of an app
 * @phba: Pointer to HBA context object.
 * @mb: Pointer to a mailbox object.
 * @vport: Pointer to a vport object.
 *
 * Allocate a tracking object, mailbox command memory, get a mailbox
 * from the mailbox pool, copy the caller mailbox command.
 *
 * If offline and the sli is active we need to poll for the command (port is
 * being reset) and com-plete the job, otherwise issue the mailbox command and
 * let our completion handler finish the command.
 **/
static uint32_t
lpfc_bsg_issue_mbox(struct lpfc_hba *phba, struct fc_bsg_job *job,
	struct lpfc_vport *vport)
{
	LPFC_MBOXQ_t *pmboxq = NULL; /* internal mailbox queue */
	MAILBOX_t *pmb; /* shortcut to the pmboxq mailbox */
	/* a 4k buffer to hold the mb and extended data from/to the bsg */
	MAILBOX_t *mb = NULL;
	struct bsg_job_data *dd_data = NULL; /* bsg data tracking structure */
	uint32_t size;
	struct lpfc_dmabuf *rxbmp = NULL; /* for biu diag */
	struct lpfc_dmabufext *dmp = NULL; /* for biu diag */
	struct ulp_bde64 *rxbpl = NULL;
	struct dfc_mbox_req *mbox_req = (struct dfc_mbox_req *)
		job->request->rqst_data.h_vendor.vendor_cmd;
	uint8_t *ext = NULL;
	int rc = 0;
	uint8_t *from;

	/* in case no data is transferred */
	job->reply->reply_payload_rcv_len = 0;

	/* check if requested extended data lengths are valid */
	if ((mbox_req->inExtWLen > MAILBOX_EXT_SIZE) ||
		(mbox_req->outExtWLen > MAILBOX_EXT_SIZE)) {
		rc = -ERANGE;
		goto job_done;
	}

	/* allocate our bsg tracking structure */
	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (!dd_data) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2727 Failed allocation of dd_data\n");
		rc = -ENOMEM;
		goto job_done;
	}

	mb = kzalloc(BSG_MBOX_SIZE, GFP_KERNEL);
	if (!mb) {
		rc = -ENOMEM;
		goto job_done;
	}

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq) {
		rc = -ENOMEM;
		goto job_done;
	}
	memset(pmboxq, 0, sizeof(LPFC_MBOXQ_t));

	size = job->request_payload.payload_len;
	sg_copy_to_buffer(job->request_payload.sg_list,
			job->request_payload.sg_cnt,
			mb, size);

	rc = lpfc_bsg_check_cmd_access(phba, mb, vport);
	if (rc != 0)
		goto job_done; /* must be negative */

	pmb = &pmboxq->u.mb;
	memcpy(pmb, mb, sizeof(*pmb));
	pmb->mbxOwner = OWN_HOST;
	pmboxq->vport = vport;

	/* If HBA encountered an error attention, allow only DUMP
	 * or RESTART mailbox commands until the HBA is restarted.
	 */
	if (phba->pport->stopped &&
	    pmb->mbxCommand != MBX_DUMP_MEMORY &&
	    pmb->mbxCommand != MBX_RESTART &&
	    pmb->mbxCommand != MBX_WRITE_VPARMS &&
	    pmb->mbxCommand != MBX_WRITE_WWN)
		lpfc_printf_log(phba, KERN_WARNING, LOG_MBOX,
				"2797 mbox: Issued mailbox cmd "
				"0x%x while in stopped state.\n",
				pmb->mbxCommand);

	/* Don't allow mailbox commands to be sent when blocked
	 * or when in the middle of discovery
	 */
	if (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO) {
		rc = -EAGAIN;
		goto job_done;
	}

	/* extended mailbox commands will need an extended buffer */
	if (mbox_req->inExtWLen || mbox_req->outExtWLen) {
		ext = kzalloc(MAILBOX_EXT_SIZE, GFP_KERNEL);
		if (!ext) {
			rc = -ENOMEM;
			goto job_done;
		}

		/* any data for the device? */
		if (mbox_req->inExtWLen) {
			from = (uint8_t *)mb;
			from += sizeof(MAILBOX_t);
			memcpy((uint8_t *)ext, from,
				mbox_req->inExtWLen * sizeof(uint32_t));
		}

		pmboxq->context2 = ext;
		pmboxq->in_ext_byte_len =
			mbox_req->inExtWLen *
			sizeof(uint32_t);
		pmboxq->out_ext_byte_len =
			mbox_req->outExtWLen *
			sizeof(uint32_t);
		pmboxq->mbox_offset_word =
			mbox_req->mbOffset;
		pmboxq->context2 = ext;
		pmboxq->in_ext_byte_len =
			mbox_req->inExtWLen * sizeof(uint32_t);
		pmboxq->out_ext_byte_len =
			mbox_req->outExtWLen * sizeof(uint32_t);
		pmboxq->mbox_offset_word = mbox_req->mbOffset;
	}

	/* biu diag will need a kernel buffer to transfer the data
	 * allocate our own buffer and setup the mailbox command to
	 * use ours
	 */
	if (pmb->mbxCommand == MBX_RUN_BIU_DIAG64) {
		uint32_t transmit_length = pmb->un.varWords[1];
		uint32_t receive_length = pmb->un.varWords[4];
		/* transmit length cannot be greater than receive length or
		 * mailbox extension size
		 */
		if ((transmit_length > receive_length) ||
			(transmit_length > MAILBOX_EXT_SIZE)) {
			rc = -ERANGE;
			goto job_done;
		}

		rxbmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
		if (!rxbmp) {
			rc = -ENOMEM;
			goto job_done;
		}

		rxbmp->virt = lpfc_mbuf_alloc(phba, 0, &rxbmp->phys);
		if (!rxbmp->virt) {
			rc = -ENOMEM;
			goto job_done;
		}

		INIT_LIST_HEAD(&rxbmp->list);
		rxbpl = (struct ulp_bde64 *) rxbmp->virt;
		dmp = diag_cmd_data_alloc(phba, rxbpl, transmit_length, 0);
		if (!dmp) {
			rc = -ENOMEM;
			goto job_done;
		}

		INIT_LIST_HEAD(&dmp->dma.list);
		pmb->un.varBIUdiag.un.s2.xmit_bde64.addrHigh =
			putPaddrHigh(dmp->dma.phys);
		pmb->un.varBIUdiag.un.s2.xmit_bde64.addrLow =
			putPaddrLow(dmp->dma.phys);

		pmb->un.varBIUdiag.un.s2.rcv_bde64.addrHigh =
			putPaddrHigh(dmp->dma.phys +
				pmb->un.varBIUdiag.un.s2.
					xmit_bde64.tus.f.bdeSize);
		pmb->un.varBIUdiag.un.s2.rcv_bde64.addrLow =
			putPaddrLow(dmp->dma.phys +
				pmb->un.varBIUdiag.un.s2.
					xmit_bde64.tus.f.bdeSize);

		/* copy the transmit data found in the mailbox extension area */
		from = (uint8_t *)mb;
		from += sizeof(MAILBOX_t);
		memcpy((uint8_t *)dmp->dma.virt, from, transmit_length);
	} else if (pmb->mbxCommand == MBX_READ_EVENT_LOG) {
		struct READ_EVENT_LOG_VAR *rdEventLog =
			&pmb->un.varRdEventLog ;
		uint32_t receive_length = rdEventLog->rcv_bde64.tus.f.bdeSize;
		uint32_t mode =	 bf_get(lpfc_event_log, rdEventLog);

		/* receive length cannot be greater than mailbox
		 * extension size
		 */
		if (receive_length > MAILBOX_EXT_SIZE) {
			rc = -ERANGE;
			goto job_done;
		}

		/* mode zero uses a bde like biu diags command */
		if (mode == 0) {

			/* rebuild the command for sli4 using our own buffers
			* like we do for biu diags
			*/

			rxbmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
			if (!rxbmp) {
				rc = -ENOMEM;
				goto job_done;
			}

			rxbmp->virt = lpfc_mbuf_alloc(phba, 0, &rxbmp->phys);
			rxbpl = (struct ulp_bde64 *) rxbmp->virt;
			if (rxbpl) {
				INIT_LIST_HEAD(&rxbmp->list);
				dmp = diag_cmd_data_alloc(phba, rxbpl,
					receive_length, 0);
			}

			if (!dmp) {
				rc = -ENOMEM;
				goto job_done;
			}

			INIT_LIST_HEAD(&dmp->dma.list);
			pmb->un.varWords[3] = putPaddrLow(dmp->dma.phys);
			pmb->un.varWords[4] = putPaddrHigh(dmp->dma.phys);
		}
	} else if (phba->sli_rev == LPFC_SLI_REV4) {
		if (pmb->mbxCommand == MBX_DUMP_MEMORY) {
			/* rebuild the command for sli4 using our own buffers
			* like we do for biu diags
			*/
			uint32_t receive_length = pmb->un.varWords[2];
			/* receive length cannot be greater than mailbox
			 * extension size
			 */
			if ((receive_length == 0) ||
				(receive_length > MAILBOX_EXT_SIZE)) {
				rc = -ERANGE;
				goto job_done;
			}

			rxbmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
			if (!rxbmp) {
				rc = -ENOMEM;
				goto job_done;
			}

			rxbmp->virt = lpfc_mbuf_alloc(phba, 0, &rxbmp->phys);
			if (!rxbmp->virt) {
				rc = -ENOMEM;
				goto job_done;
			}

			INIT_LIST_HEAD(&rxbmp->list);
			rxbpl = (struct ulp_bde64 *) rxbmp->virt;
			dmp = diag_cmd_data_alloc(phba, rxbpl, receive_length,
						0);
			if (!dmp) {
				rc = -ENOMEM;
				goto job_done;
			}

			INIT_LIST_HEAD(&dmp->dma.list);
			pmb->un.varWords[3] = putPaddrLow(dmp->dma.phys);
			pmb->un.varWords[4] = putPaddrHigh(dmp->dma.phys);
		} else if ((pmb->mbxCommand == MBX_UPDATE_CFG) &&
			pmb->un.varUpdateCfg.co) {
			struct ulp_bde64 *bde =
				(struct ulp_bde64 *)&pmb->un.varWords[4];

			/* bde size cannot be greater than mailbox ext size */
			if (bde->tus.f.bdeSize > MAILBOX_EXT_SIZE) {
				rc = -ERANGE;
				goto job_done;
			}

			rxbmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
			if (!rxbmp) {
				rc = -ENOMEM;
				goto job_done;
			}

			rxbmp->virt = lpfc_mbuf_alloc(phba, 0, &rxbmp->phys);
			if (!rxbmp->virt) {
				rc = -ENOMEM;
				goto job_done;
			}

			INIT_LIST_HEAD(&rxbmp->list);
			rxbpl = (struct ulp_bde64 *) rxbmp->virt;
			dmp = diag_cmd_data_alloc(phba, rxbpl,
					bde->tus.f.bdeSize, 0);
			if (!dmp) {
				rc = -ENOMEM;
				goto job_done;
			}

			INIT_LIST_HEAD(&dmp->dma.list);
			bde->addrHigh = putPaddrHigh(dmp->dma.phys);
			bde->addrLow = putPaddrLow(dmp->dma.phys);

			/* copy the transmit data found in the mailbox
			 * extension area
			 */
			from = (uint8_t *)mb;
			from += sizeof(MAILBOX_t);
			memcpy((uint8_t *)dmp->dma.virt, from,
				bde->tus.f.bdeSize);
		}
	}

	dd_data->context_un.mbox.rxbmp = rxbmp;
	dd_data->context_un.mbox.dmp = dmp;

	/* setup wake call as IOCB callback */
	pmboxq->mbox_cmpl = lpfc_bsg_wake_mbox_wait;

	/* setup context field to pass wait_queue pointer to wake function */
	pmboxq->context1 = dd_data;
	dd_data->type = TYPE_MBOX;
	dd_data->context_un.mbox.pmboxq = pmboxq;
	dd_data->context_un.mbox.mb = mb;
	dd_data->context_un.mbox.set_job = job;
	dd_data->context_un.mbox.ext = ext;
	dd_data->context_un.mbox.mbOffset = mbox_req->mbOffset;
	dd_data->context_un.mbox.inExtWLen = mbox_req->inExtWLen;
	dd_data->context_un.mbox.outExtWLen = mbox_req->outExtWLen;
	job->dd_data = dd_data;

	if ((vport->fc_flag & FC_OFFLINE_MODE) ||
	    (!(phba->sli.sli_flag & LPFC_SLI_ACTIVE))) {
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
		if (rc != MBX_SUCCESS) {
			rc = (rc == MBX_TIMEOUT) ? -ETIME : -ENODEV;
			goto job_done;
		}

		/* job finished, copy the data */
		memcpy(mb, pmb, sizeof(*pmb));
		job->reply->reply_payload_rcv_len =
			sg_copy_from_buffer(job->reply_payload.sg_list,
					job->reply_payload.sg_cnt,
					mb, size);
		/* not waiting mbox already done */
		rc = 0;
		goto job_done;
	}

	rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_NOWAIT);
	if ((rc == MBX_SUCCESS) || (rc == MBX_BUSY))
		return 1; /* job started */

job_done:
	/* common exit for error or job completed inline */
	kfree(mb);
	if (pmboxq)
		mempool_free(pmboxq, phba->mbox_mem_pool);
	kfree(ext);
	if (dmp) {
		dma_free_coherent(&phba->pcidev->dev,
			dmp->size, dmp->dma.virt,
				dmp->dma.phys);
		kfree(dmp);
	}
	if (rxbmp) {
		lpfc_mbuf_free(phba, rxbmp->virt, rxbmp->phys);
		kfree(rxbmp);
	}
	kfree(dd_data);

	return rc;
}

/**
 * lpfc_bsg_mbox_cmd - process an fc bsg LPFC_BSG_VENDOR_MBOX command
 * @job: MBOX fc_bsg_job for LPFC_BSG_VENDOR_MBOX.
 **/
static int
lpfc_bsg_mbox_cmd(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	int rc = 0;

	/* in case no data is transferred */
	job->reply->reply_payload_rcv_len = 0;
	if (job->request_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct dfc_mbox_req)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2737 Received MBOX_REQ request below "
				"minimum size\n");
		rc = -EINVAL;
		goto job_error;
	}

	if (job->request_payload.payload_len != BSG_MBOX_SIZE) {
		rc = -EINVAL;
		goto job_error;
	}

	if (job->reply_payload.payload_len != BSG_MBOX_SIZE) {
		rc = -EINVAL;
		goto job_error;
	}

	if (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO) {
		rc = -EAGAIN;
		goto job_error;
	}

	rc = lpfc_bsg_issue_mbox(phba, job, vport);

job_error:
	if (rc == 0) {
		/* job done */
		job->reply->result = 0;
		job->dd_data = NULL;
		job->job_done(job);
	} else if (rc == 1)
		/* job submitted, will complete later*/
		rc = 0; /* return zero, no error */
	else {
		/* some error occurred */
		job->reply->result = rc;
		job->dd_data = NULL;
	}

	return rc;
}

/**
 * lpfc_bsg_menlo_cmd_cmp - lpfc_menlo_cmd completion handler
 * @phba: Pointer to HBA context object.
 * @cmdiocbq: Pointer to command iocb.
 * @rspiocbq: Pointer to response iocb.
 *
 * This function is the completion handler for iocbs issued using
 * lpfc_menlo_cmd function. This function is called by the
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
lpfc_bsg_menlo_cmd_cmp(struct lpfc_hba *phba,
			struct lpfc_iocbq *cmdiocbq,
			struct lpfc_iocbq *rspiocbq)
{
	struct bsg_job_data *dd_data;
	struct fc_bsg_job *job;
	IOCB_t *rsp;
	struct lpfc_dmabuf *bmp;
	struct lpfc_bsg_menlo *menlo;
	unsigned long flags;
	struct menlo_response *menlo_resp;
	int rc = 0;

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	dd_data = cmdiocbq->context1;
	if (!dd_data) {
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		return;
	}

	menlo = &dd_data->context_un.menlo;
	job = menlo->set_job;
	job->dd_data = NULL; /* so timeout handler does not reply */

	spin_lock_irqsave(&phba->hbalock, flags);
	cmdiocbq->iocb_flag |= LPFC_IO_WAKE;
	if (cmdiocbq->context2 && rspiocbq)
		memcpy(&((struct lpfc_iocbq *)cmdiocbq->context2)->iocb,
		       &rspiocbq->iocb, sizeof(IOCB_t));
	spin_unlock_irqrestore(&phba->hbalock, flags);

	bmp = menlo->bmp;
	rspiocbq = menlo->rspiocbq;
	rsp = &rspiocbq->iocb;

	pci_unmap_sg(phba->pcidev, job->request_payload.sg_list,
		     job->request_payload.sg_cnt, DMA_TO_DEVICE);
	pci_unmap_sg(phba->pcidev, job->reply_payload.sg_list,
		     job->reply_payload.sg_cnt, DMA_FROM_DEVICE);

	/* always return the xri, this would be used in the case
	 * of a menlo download to allow the data to be sent as a continuation
	 * of the exchange.
	 */
	menlo_resp = (struct menlo_response *)
		job->reply->reply_data.vendor_reply.vendor_rsp;
	menlo_resp->xri = rsp->ulpContext;
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
 * lpfc_menlo_cmd - send an ioctl for menlo hardware
 * @job: fc_bsg_job to handle
 *
 * This function issues a gen request 64 CR ioctl for all menlo cmd requests,
 * all the command completions will return the xri for the command.
 * For menlo data requests a gen request 64 CX is used to continue the exchange
 * supplied in the menlo request header xri field.
 **/
static int
lpfc_menlo_cmd(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *cmdiocbq, *rspiocbq;
	IOCB_t *cmd, *rsp;
	int rc = 0;
	struct menlo_command *menlo_cmd;
	struct menlo_response *menlo_resp;
	struct lpfc_dmabuf *bmp = NULL;
	int request_nseg;
	int reply_nseg;
	struct scatterlist *sgel = NULL;
	int numbde;
	dma_addr_t busaddr;
	struct bsg_job_data *dd_data;
	struct ulp_bde64 *bpl = NULL;

	/* in case no data is returned return just the return code */
	job->reply->reply_payload_rcv_len = 0;

	if (job->request_len <
	    sizeof(struct fc_bsg_request) +
		sizeof(struct menlo_command)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2784 Received MENLO_CMD request below "
				"minimum size\n");
		rc = -ERANGE;
		goto no_dd_data;
	}

	if (job->reply_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct menlo_response)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2785 Received MENLO_CMD reply below "
				"minimum size\n");
		rc = -ERANGE;
		goto no_dd_data;
	}

	if (!(phba->menlo_flag & HBA_MENLO_SUPPORT)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2786 Adapter does not support menlo "
				"commands\n");
		rc = -EPERM;
		goto no_dd_data;
	}

	menlo_cmd = (struct menlo_command *)
		job->request->rqst_data.h_vendor.vendor_cmd;

	menlo_resp = (struct menlo_response *)
		job->reply->reply_data.vendor_reply.vendor_rsp;

	/* allocate our bsg tracking structure */
	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (!dd_data) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2787 Failed allocation of dd_data\n");
		rc = -ENOMEM;
		goto no_dd_data;
	}

	bmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp) {
		rc = -ENOMEM;
		goto free_dd;
	}

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	if (!cmdiocbq) {
		rc = -ENOMEM;
		goto free_bmp;
	}

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

	cmd = &cmdiocbq->iocb;
	cmd->un.genreq64.bdl.ulpIoTag32 = 0;
	cmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	cmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	cmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	cmd->un.genreq64.bdl.bdeSize =
	    (request_nseg + reply_nseg) * sizeof(struct ulp_bde64);
	cmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	cmd->un.genreq64.w5.hcsw.Dfctl = 0;
	cmd->un.genreq64.w5.hcsw.Rctl = FC_RCTL_DD_UNSOL_CMD;
	cmd->un.genreq64.w5.hcsw.Type = MENLO_TRANSPORT_TYPE; /* 0xfe */
	cmd->ulpBdeCount = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpOwner = OWN_CHIP;
	cmd->ulpLe = 1; /* Limited Edition */
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	cmdiocbq->vport = phba->pport;
	/* We want the firmware to timeout before we do */
	cmd->ulpTimeout = MENLO_TIMEOUT - 5;
	cmdiocbq->context3 = bmp;
	cmdiocbq->context2 = rspiocbq;
	cmdiocbq->iocb_cmpl = lpfc_bsg_menlo_cmd_cmp;
	cmdiocbq->context1 = dd_data;
	cmdiocbq->context2 = rspiocbq;
	if (menlo_cmd->cmd == LPFC_BSG_VENDOR_MENLO_CMD) {
		cmd->ulpCommand = CMD_GEN_REQUEST64_CR;
		cmd->ulpPU = MENLO_PU; /* 3 */
		cmd->un.ulpWord[4] = MENLO_DID; /* 0x0000FC0E */
		cmd->ulpContext = MENLO_CONTEXT; /* 0 */
	} else {
		cmd->ulpCommand = CMD_GEN_REQUEST64_CX;
		cmd->ulpPU = 1;
		cmd->un.ulpWord[4] = 0;
		cmd->ulpContext = menlo_cmd->xri;
	}

	dd_data->type = TYPE_MENLO;
	dd_data->context_un.menlo.cmdiocbq = cmdiocbq;
	dd_data->context_un.menlo.rspiocbq = rspiocbq;
	dd_data->context_un.menlo.set_job = job;
	dd_data->context_un.menlo.bmp = bmp;

	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, cmdiocbq,
		MENLO_TIMEOUT - 5);
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
free_dd:
	kfree(dd_data);
no_dd_data:
	/* make error code available to userspace */
	job->reply->result = rc;
	job->dd_data = NULL;
	return rc;
}
/**
 * lpfc_bsg_hst_vendor - process a vendor-specific fc_bsg_job
 * @job: fc_bsg_job to handle
 **/
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
	case LPFC_BSG_VENDOR_SEND_MGMT_RESP:
		rc = lpfc_bsg_send_mgmt_rsp(job);
		break;
	case LPFC_BSG_VENDOR_DIAG_MODE:
		rc = lpfc_bsg_diag_mode(job);
		break;
	case LPFC_BSG_VENDOR_DIAG_TEST:
		rc = lpfc_bsg_diag_test(job);
		break;
	case LPFC_BSG_VENDOR_GET_MGMT_REV:
		rc = lpfc_bsg_get_dfc_rev(job);
		break;
	case LPFC_BSG_VENDOR_MBOX:
		rc = lpfc_bsg_mbox_cmd(job);
		break;
	case LPFC_BSG_VENDOR_MENLO_CMD:
	case LPFC_BSG_VENDOR_MENLO_DATA:
		rc = lpfc_menlo_cmd(job);
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
 **/
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
 **/
int
lpfc_bsg_timeout(struct fc_bsg_job *job)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)job->shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_bsg_event *evt;
	struct lpfc_bsg_iocb *iocb;
	struct lpfc_bsg_mbox *mbox;
	struct lpfc_bsg_menlo *menlo;
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
	case TYPE_MBOX:
		mbox = &dd_data->context_un.mbox;
		/* this mbox has no job anymore */
		mbox->set_job = NULL;
		job->dd_data = NULL;
		job->reply->reply_payload_rcv_len = 0;
		job->reply->result = -EAGAIN;
		/* the mbox completion handler can now be run */
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		job->job_done(job);
		break;
	case TYPE_MENLO:
		menlo = &dd_data->context_un.menlo;
		cmdiocb = menlo->cmdiocbq;
		/* hint to completion handler that the job timed out */
		job->reply->result = -EAGAIN;
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		/* this will call our completion handler */
		spin_lock_irq(&phba->hbalock);
		lpfc_sli_issue_abort_iotag(phba, pring, cmdiocb);
		spin_unlock_irq(&phba->hbalock);
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
