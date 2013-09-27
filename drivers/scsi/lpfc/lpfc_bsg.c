/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2009-2013 Emulex.  All rights reserved.           *
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
#include <linux/list.h>

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
#include "lpfc_debugfs.h"
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

	/* driver data associated with the job */
	void *dd_data;
};

struct lpfc_bsg_iocb {
	struct lpfc_iocbq *cmdiocbq;
	struct lpfc_dmabuf *rmp;
	struct lpfc_nodelist *ndlp;
};

struct lpfc_bsg_mbox {
	LPFC_MBOXQ_t *pmboxq;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *dmabuffers; /* for BIU diags */
	uint8_t *ext; /* extended mailbox data */
	uint32_t mbOffset; /* from app */
	uint32_t inExtWLen; /* from app */
	uint32_t outExtWLen; /* from app */
};

#define MENLO_DID 0x0000FC0E

struct lpfc_bsg_menlo {
	struct lpfc_iocbq *cmdiocbq;
	struct lpfc_dmabuf *rmp;
};

#define TYPE_EVT 	1
#define TYPE_IOCB	2
#define TYPE_MBOX	3
#define TYPE_MENLO	4
struct bsg_job_data {
	uint32_t type;
	struct fc_bsg_job *set_job; /* job waiting for this iocb to finish */
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

static void
lpfc_free_bsg_buffers(struct lpfc_hba *phba, struct lpfc_dmabuf *mlist)
{
	struct lpfc_dmabuf *mlast, *next_mlast;

	if (mlist) {
		list_for_each_entry_safe(mlast, next_mlast, &mlist->list,
					 list) {
			lpfc_mbuf_free(phba, mlast->virt, mlast->phys);
			list_del(&mlast->list);
			kfree(mlast);
		}
		lpfc_mbuf_free(phba, mlist->virt, mlist->phys);
		kfree(mlist);
	}
	return;
}

static struct lpfc_dmabuf *
lpfc_alloc_bsg_buffers(struct lpfc_hba *phba, unsigned int size,
		       int outbound_buffers, struct ulp_bde64 *bpl,
		       int *bpl_entries)
{
	struct lpfc_dmabuf *mlist = NULL;
	struct lpfc_dmabuf *mp;
	unsigned int bytes_left = size;

	/* Verify we can support the size specified */
	if (!size || (size > (*bpl_entries * LPFC_BPL_SIZE)))
		return NULL;

	/* Determine the number of dma buffers to allocate */
	*bpl_entries = (size % LPFC_BPL_SIZE ? size/LPFC_BPL_SIZE + 1 :
			size/LPFC_BPL_SIZE);

	/* Allocate dma buffer and place in BPL passed */
	while (bytes_left) {
		/* Allocate dma buffer  */
		mp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
		if (!mp) {
			if (mlist)
				lpfc_free_bsg_buffers(phba, mlist);
			return NULL;
		}

		INIT_LIST_HEAD(&mp->list);
		mp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(mp->phys));

		if (!mp->virt) {
			kfree(mp);
			if (mlist)
				lpfc_free_bsg_buffers(phba, mlist);
			return NULL;
		}

		/* Queue it to a linked list */
		if (!mlist)
			mlist = mp;
		else
			list_add_tail(&mp->list, &mlist->list);

		/* Add buffer to buffer pointer list */
		if (outbound_buffers)
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		else
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
		bpl->addrLow = le32_to_cpu(putPaddrLow(mp->phys));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(mp->phys));
		bpl->tus.f.bdeSize = (uint16_t)
			(bytes_left >= LPFC_BPL_SIZE ? LPFC_BPL_SIZE :
			 bytes_left);
		bytes_left -= bpl->tus.f.bdeSize;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;
	}
	return mlist;
}

static unsigned int
lpfc_bsg_copy_data(struct lpfc_dmabuf *dma_buffers,
		   struct fc_bsg_buffer *bsg_buffers,
		   unsigned int bytes_to_transfer, int to_buffers)
{

	struct lpfc_dmabuf *mp;
	unsigned int transfer_bytes, bytes_copied = 0;
	unsigned int sg_offset, dma_offset;
	unsigned char *dma_address, *sg_address;
	LIST_HEAD(temp_list);
	struct sg_mapping_iter miter;
	unsigned long flags;
	unsigned int sg_flags = SG_MITER_ATOMIC;
	bool sg_valid;

	list_splice_init(&dma_buffers->list, &temp_list);
	list_add(&dma_buffers->list, &temp_list);
	sg_offset = 0;
	if (to_buffers)
		sg_flags |= SG_MITER_FROM_SG;
	else
		sg_flags |= SG_MITER_TO_SG;
	sg_miter_start(&miter, bsg_buffers->sg_list, bsg_buffers->sg_cnt,
		       sg_flags);
	local_irq_save(flags);
	sg_valid = sg_miter_next(&miter);
	list_for_each_entry(mp, &temp_list, list) {
		dma_offset = 0;
		while (bytes_to_transfer && sg_valid &&
		       (dma_offset < LPFC_BPL_SIZE)) {
			dma_address = mp->virt + dma_offset;
			if (sg_offset) {
				/* Continue previous partial transfer of sg */
				sg_address = miter.addr + sg_offset;
				transfer_bytes = miter.length - sg_offset;
			} else {
				sg_address = miter.addr;
				transfer_bytes = miter.length;
			}
			if (bytes_to_transfer < transfer_bytes)
				transfer_bytes = bytes_to_transfer;
			if (transfer_bytes > (LPFC_BPL_SIZE - dma_offset))
				transfer_bytes = LPFC_BPL_SIZE - dma_offset;
			if (to_buffers)
				memcpy(dma_address, sg_address, transfer_bytes);
			else
				memcpy(sg_address, dma_address, transfer_bytes);
			dma_offset += transfer_bytes;
			sg_offset += transfer_bytes;
			bytes_to_transfer -= transfer_bytes;
			bytes_copied += transfer_bytes;
			if (sg_offset >= miter.length) {
				sg_offset = 0;
				sg_valid = sg_miter_next(&miter);
			}
		}
	}
	sg_miter_stop(&miter);
	local_irq_restore(flags);
	list_del_init(&dma_buffers->list);
	list_splice(&temp_list, &dma_buffers->list);
	return bytes_copied;
}

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
	struct bsg_job_data *dd_data;
	struct fc_bsg_job *job;
	IOCB_t *rsp;
	struct lpfc_dmabuf *bmp, *cmp, *rmp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_bsg_iocb *iocb;
	unsigned long flags;
	unsigned int rsp_size;
	int rc = 0;

	dd_data = cmdiocbq->context1;

	/* Determine if job has been aborted */
	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	job = dd_data->set_job;
	if (job) {
		/* Prevent timeout handling from trying to abort job */
		job->dd_data = NULL;
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	iocb = &dd_data->context_un.iocb;
	ndlp = iocb->ndlp;
	rmp = iocb->rmp;
	cmp = cmdiocbq->context2;
	bmp = cmdiocbq->context3;
	rsp = &rspiocbq->iocb;

	/* Copy the completed data or set the error status */

	if (job) {
		if (rsp->ulpStatus) {
			if (rsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
				switch (rsp->un.ulpWord[4] & IOERR_PARAM_MASK) {
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
			} else {
				rc = -EACCES;
			}
		} else {
			rsp_size = rsp->un.genreq64.bdl.bdeSize;
			job->reply->reply_payload_rcv_len =
				lpfc_bsg_copy_data(rmp, &job->reply_payload,
						   rsp_size, 0);
		}
	}

	lpfc_free_bsg_buffers(phba, cmp);
	lpfc_free_bsg_buffers(phba, rmp);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(bmp);
	lpfc_sli_release_iocbq(phba, cmdiocbq);
	lpfc_nlp_put(ndlp);
	kfree(dd_data);

	/* Complete the job if the job is still active */

	if (job) {
		job->reply->result = rc;
		job->job_done(job);
	}
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
	IOCB_t *cmd;
	struct lpfc_dmabuf *bmp = NULL, *cmp = NULL, *rmp = NULL;
	int request_nseg;
	int reply_nseg;
	struct bsg_job_data *dd_data;
	uint32_t creg_val;
	int rc = 0;
	int iocb_stat;

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

	if (ndlp->nlp_flag & NLP_ELS_SND_MASK) {
		rc = -ENODEV;
		goto free_ndlp;
	}

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	if (!cmdiocbq) {
		rc = -ENOMEM;
		goto free_ndlp;
	}

	cmd = &cmdiocbq->iocb;

	bmp = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp) {
		rc = -ENOMEM;
		goto free_cmdiocbq;
	}
	bmp->virt = lpfc_mbuf_alloc(phba, 0, &bmp->phys);
	if (!bmp->virt) {
		rc = -ENOMEM;
		goto free_bmp;
	}

	INIT_LIST_HEAD(&bmp->list);

	bpl = (struct ulp_bde64 *) bmp->virt;
	request_nseg = LPFC_BPL_SIZE/sizeof(struct ulp_bde64);
	cmp = lpfc_alloc_bsg_buffers(phba, job->request_payload.payload_len,
				     1, bpl, &request_nseg);
	if (!cmp) {
		rc = -ENOMEM;
		goto free_bmp;
	}
	lpfc_bsg_copy_data(cmp, &job->request_payload,
			   job->request_payload.payload_len, 1);

	bpl += request_nseg;
	reply_nseg = LPFC_BPL_SIZE/sizeof(struct ulp_bde64) - request_nseg;
	rmp = lpfc_alloc_bsg_buffers(phba, job->reply_payload.payload_len, 0,
				     bpl, &reply_nseg);
	if (!rmp) {
		rc = -ENOMEM;
		goto free_cmp;
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
	if (phba->sli_rev == LPFC_SLI_REV4)
		cmd->ulpContext = phba->sli4_hba.rpi_ids[ndlp->nlp_rpi];
	cmd->ulpOwner = OWN_CHIP;
	cmdiocbq->vport = phba->pport;
	cmdiocbq->context3 = bmp;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	timeout = phba->fc_ratov * 2;
	cmd->ulpTimeout = timeout;

	cmdiocbq->iocb_cmpl = lpfc_bsg_send_mgmt_cmd_cmp;
	cmdiocbq->context1 = dd_data;
	cmdiocbq->context2 = cmp;
	cmdiocbq->context3 = bmp;
	cmdiocbq->context_un.ndlp = ndlp;
	dd_data->type = TYPE_IOCB;
	dd_data->set_job = job;
	dd_data->context_un.iocb.cmdiocbq = cmdiocbq;
	dd_data->context_un.iocb.ndlp = ndlp;
	dd_data->context_un.iocb.rmp = rmp;
	job->dd_data = dd_data;

	if (phba->cfg_poll & DISABLE_FCP_RING_INT) {
		if (lpfc_readl(phba->HCregaddr, &creg_val)) {
			rc = -EIO ;
			goto free_rmp;
		}
		creg_val |= (HC_R0INT_ENA << LPFC_FCP_RING);
		writel(creg_val, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	iocb_stat = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, cmdiocbq, 0);
	if (iocb_stat == IOCB_SUCCESS)
		return 0; /* done for now */
	else if (iocb_stat == IOCB_BUSY)
		rc = -EAGAIN;
	else
		rc = -EIO;

	/* iocb failed so cleanup */

free_rmp:
	lpfc_free_bsg_buffers(phba, rmp);
free_cmp:
	lpfc_free_bsg_buffers(phba, cmp);
free_bmp:
	if (bmp->virt)
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(bmp);
free_cmdiocbq:
	lpfc_sli_release_iocbq(phba, cmdiocbq);
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
	struct lpfc_dmabuf *pcmd = NULL, *prsp = NULL;
	struct fc_bsg_ctels_reply *els_reply;
	uint8_t *rjt_data;
	unsigned long flags;
	unsigned int rsp_size;
	int rc = 0;

	dd_data = cmdiocbq->context1;
	ndlp = dd_data->context_un.iocb.ndlp;
	cmdiocbq->context1 = ndlp;

	/* Determine if job has been aborted */
	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	job = dd_data->set_job;
	if (job) {
		/* Prevent timeout handling from trying to abort job  */
		job->dd_data = NULL;
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	rsp = &rspiocbq->iocb;
	pcmd = (struct lpfc_dmabuf *)cmdiocbq->context2;
	prsp = (struct lpfc_dmabuf *)pcmd->list.next;

	/* Copy the completed job data or determine the job status if job is
	 * still active
	 */

	if (job) {
		if (rsp->ulpStatus == IOSTAT_SUCCESS) {
			rsp_size = rsp->un.elsreq64.bdl.bdeSize;
			job->reply->reply_payload_rcv_len =
				sg_copy_from_buffer(job->reply_payload.sg_list,
						    job->reply_payload.sg_cnt,
						    prsp->virt,
						    rsp_size);
		} else if (rsp->ulpStatus == IOSTAT_LS_RJT) {
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
		} else {
			rc = -EIO;
		}
	}

	lpfc_nlp_put(ndlp);
	lpfc_els_free_iocb(phba, cmdiocbq);
	kfree(dd_data);

	/* Complete the job if the job is still active */

	if (job) {
		job->reply->result = rc;
		job->job_done(job);
	}
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
	struct lpfc_iocbq *cmdiocbq;
	uint16_t rpi = 0;
	struct bsg_job_data *dd_data;
	uint32_t creg_val;
	int rc = 0;

	/* in case no data is transferred */
	job->reply->reply_payload_rcv_len = 0;

	/* verify the els command is not greater than the
	 * maximum ELS transfer size.
	 */

	if (job->request_payload.payload_len > FCELSSIZE) {
		rc = -EINVAL;
		goto no_dd_data;
	}

	/* allocate our bsg tracking structure */
	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (!dd_data) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2735 Failed allocation of dd_data\n");
		rc = -ENOMEM;
		goto no_dd_data;
	}

	elscmd = job->request->rqst_data.r_els.els_code;
	cmdsize = job->request_payload.payload_len;
	rspsize = job->reply_payload.payload_len;

	if (!lpfc_nlp_get(ndlp)) {
		rc = -ENODEV;
		goto free_dd_data;
	}

	/* We will use the allocated dma buffers by prep els iocb for command
	 * and response to ensure if the job times out and the request is freed,
	 * we won't be dma into memory that is no longer allocated to for the
	 * request.
	 */

	cmdiocbq = lpfc_prep_els_iocb(vport, 1, cmdsize, 0, ndlp,
				      ndlp->nlp_DID, elscmd);
	if (!cmdiocbq) {
		rc = -EIO;
		goto release_ndlp;
	}

	rpi = ndlp->nlp_rpi;

	/* Transfer the request payload to allocated command dma buffer */

	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt,
			  ((struct lpfc_dmabuf *)cmdiocbq->context2)->virt,
			  cmdsize);

	if (phba->sli_rev == LPFC_SLI_REV4)
		cmdiocbq->iocb.ulpContext = phba->sli4_hba.rpi_ids[rpi];
	else
		cmdiocbq->iocb.ulpContext = rpi;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	cmdiocbq->context1 = dd_data;
	cmdiocbq->context_un.ndlp = ndlp;
	cmdiocbq->iocb_cmpl = lpfc_bsg_rport_els_cmp;
	dd_data->type = TYPE_IOCB;
	dd_data->set_job = job;
	dd_data->context_un.iocb.cmdiocbq = cmdiocbq;
	dd_data->context_un.iocb.ndlp = ndlp;
	dd_data->context_un.iocb.rmp = NULL;
	job->dd_data = dd_data;

	if (phba->cfg_poll & DISABLE_FCP_RING_INT) {
		if (lpfc_readl(phba->HCregaddr, &creg_val)) {
			rc = -EIO;
			goto linkdown_err;
		}
		creg_val |= (HC_R0INT_ENA << LPFC_FCP_RING);
		writel(creg_val, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, cmdiocbq, 0);

	if (rc == IOCB_SUCCESS)
		return 0; /* done for now */
	else if (rc == IOCB_BUSY)
		rc = -EAGAIN;
	else
		rc = -EIO;

linkdown_err:

	cmdiocbq->context1 = ndlp;
	lpfc_els_free_iocb(phba, cmdiocbq);

release_ndlp:
	lpfc_nlp_put(ndlp);

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

	kfree(evt->dd_data);
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
	evt->dd_data = NULL;
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
	struct bsg_job_data *dd_data = NULL;
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
						if (phba->sli_rev <
						    LPFC_SLI_REV4)
							diag_cmd_data_free(phba,
							(struct lpfc_dmabufext
							 *)dmabuf);
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
			phba->ctx_idx = (phba->ctx_idx + 1) % LPFC_CT_CTX_MAX;
			/* Provide warning for over-run of the ct_ctx array */
			if (phba->ct_ctx[evt_dat->immed_dat].valid ==
			    UNSOL_VALID)
				lpfc_printf_log(phba, KERN_WARNING, LOG_ELS,
						"2717 CT context array entry "
						"[%d] over-run: oxid:x%x, "
						"sid:x%x\n", phba->ctx_idx,
						phba->ct_ctx[
						    evt_dat->immed_dat].oxid,
						phba->ct_ctx[
						    evt_dat->immed_dat].SID);
			phba->ct_ctx[evt_dat->immed_dat].rxid =
				piocbq->iocb.ulpContext;
			phba->ct_ctx[evt_dat->immed_dat].oxid =
				piocbq->iocb.unsli3.rcvsli3.ox_id;
			phba->ct_ctx[evt_dat->immed_dat].SID =
				piocbq->iocb.un.rcvels.remoteID;
			phba->ct_ctx[evt_dat->immed_dat].valid = UNSOL_VALID;
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

		dd_data = (struct bsg_job_data *)evt->dd_data;
		job = dd_data->set_job;
		dd_data->set_job = NULL;
		lpfc_bsg_event_unref(evt);
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
	if ((phba->sli_rev < LPFC_SLI_REV4) &&
	    (evt_req_id == SLI_CT_ELX_LOOPBACK))
		return 0;
	return 1;
}

/**
 * lpfc_bsg_ct_unsol_abort - handler ct abort to management plane
 * @phba: Pointer to HBA context object.
 * @dmabuf: pointer to a dmabuf that describes the FC sequence
 *
 * This function handles abort to the CT command toward management plane
 * for SLI4 port.
 *
 * If the pending context of a CT command to management plane present, clears
 * such context and returns 1 for handled; otherwise, it returns 0 indicating
 * no context exists.
 **/
int
lpfc_bsg_ct_unsol_abort(struct lpfc_hba *phba, struct hbq_dmabuf *dmabuf)
{
	struct fc_frame_header fc_hdr;
	struct fc_frame_header *fc_hdr_ptr = &fc_hdr;
	int ctx_idx, handled = 0;
	uint16_t oxid, rxid;
	uint32_t sid;

	memcpy(fc_hdr_ptr, dmabuf->hbuf.virt, sizeof(struct fc_frame_header));
	sid = sli4_sid_from_fc_hdr(fc_hdr_ptr);
	oxid = be16_to_cpu(fc_hdr_ptr->fh_ox_id);
	rxid = be16_to_cpu(fc_hdr_ptr->fh_rx_id);

	for (ctx_idx = 0; ctx_idx < LPFC_CT_CTX_MAX; ctx_idx++) {
		if (phba->ct_ctx[ctx_idx].valid != UNSOL_VALID)
			continue;
		if (phba->ct_ctx[ctx_idx].rxid != rxid)
			continue;
		if (phba->ct_ctx[ctx_idx].oxid != oxid)
			continue;
		if (phba->ct_ctx[ctx_idx].SID != sid)
			continue;
		phba->ct_ctx[ctx_idx].valid = UNSOL_INVALID;
		handled = 1;
	}
	return handled;
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

	event_req = (struct set_ct_event *)
		job->request->rqst_data.h_vendor.vendor_cmd;
	ev_mask = ((uint32_t)(unsigned long)event_req->type_mask &
				FC_REG_EVENT_MASK);
	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	list_for_each_entry(evt, &phba->ct_ev_waiters, node) {
		if (evt->reg_id == event_req->ev_reg_id) {
			lpfc_bsg_event_ref(evt);
			evt->wait_time_stamp = jiffies;
			dd_data = (struct bsg_job_data *)evt->dd_data;
			break;
		}
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	if (&evt->node == &phba->ct_ev_waiters) {
		/* no event waiting struct yet - first call */
		dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
		if (dd_data == NULL) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
					"2734 Failed allocation of dd_data\n");
			rc = -ENOMEM;
			goto job_error;
		}
		evt = lpfc_bsg_event_new(ev_mask, event_req->ev_reg_id,
					event_req->ev_req_id);
		if (!evt) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
					"2617 Failed allocation of event "
					"waiter\n");
			rc = -ENOMEM;
			goto job_error;
		}
		dd_data->type = TYPE_EVT;
		dd_data->set_job = NULL;
		dd_data->context_un.evt = evt;
		evt->dd_data = (void *)dd_data;
		spin_lock_irqsave(&phba->ct_ev_lock, flags);
		list_add(&evt->node, &phba->ct_ev_waiters);
		lpfc_bsg_event_ref(evt);
		evt->wait_time_stamp = jiffies;
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
	}

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	evt->waiting = 1;
	dd_data->set_job = job; /* for unsolicited command */
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
	struct lpfc_dmabuf *bmp, *cmp;
	struct lpfc_nodelist *ndlp;
	unsigned long flags;
	int rc = 0;

	dd_data = cmdiocbq->context1;

	/* Determine if job has been aborted */
	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	job = dd_data->set_job;
	if (job) {
		/* Prevent timeout handling from trying to abort job  */
		job->dd_data = NULL;
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	ndlp = dd_data->context_un.iocb.ndlp;
	cmp = cmdiocbq->context2;
	bmp = cmdiocbq->context3;
	rsp = &rspiocbq->iocb;

	/* Copy the completed job data or set the error status */

	if (job) {
		if (rsp->ulpStatus) {
			if (rsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
				switch (rsp->un.ulpWord[4] & IOERR_PARAM_MASK) {
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
			} else {
				rc = -EACCES;
			}
		} else {
			job->reply->reply_payload_rcv_len = 0;
		}
	}

	lpfc_free_bsg_buffers(phba, cmp);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(bmp);
	lpfc_sli_release_iocbq(phba, cmdiocbq);
	lpfc_nlp_put(ndlp);
	kfree(dd_data);

	/* Complete the job if the job is still active */

	if (job) {
		job->reply->result = rc;
		job->job_done(job);
	}
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
		  struct lpfc_dmabuf *cmp, struct lpfc_dmabuf *bmp,
		  int num_entry)
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
		rc = -ENOMEM;
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
		if (phba->ct_ctx[tag].valid != UNSOL_VALID) {
			rc = IOCB_ERROR;
			goto issue_ct_rsp_exit;
		}
		icmd->ulpContext = phba->ct_ctx[tag].rxid;
		icmd->unsli3.rcvsli3.ox_id = phba->ct_ctx[tag].oxid;
		ndlp = lpfc_findnode_did(phba->pport, phba->ct_ctx[tag].SID);
		if (!ndlp) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_ELS,
				 "2721 ndlp null for oxid %x SID %x\n",
					icmd->ulpContext,
					phba->ct_ctx[tag].SID);
			rc = IOCB_ERROR;
			goto issue_ct_rsp_exit;
		}

		/* Check if the ndlp is active */
		if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
			rc = IOCB_ERROR;
			goto issue_ct_rsp_exit;
		}

		/* get a refernece count so the ndlp doesn't go away while
		 * we respond
		 */
		if (!lpfc_nlp_get(ndlp)) {
			rc = IOCB_ERROR;
			goto issue_ct_rsp_exit;
		}

		icmd->un.ulpWord[3] =
				phba->sli4_hba.rpi_ids[ndlp->nlp_rpi];

		/* The exchange is done, mark the entry as invalid */
		phba->ct_ctx[tag].valid = UNSOL_INVALID;
	} else
		icmd->ulpContext = (ushort) tag;

	icmd->ulpTimeout = phba->fc_ratov * 2;

	/* Xmit CT response on exchange <xid> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
		"2722 Xmit CT response on exchange x%x Data: x%x x%x x%x\n",
		icmd->ulpContext, icmd->ulpIoTag, tag, phba->link_state);

	ctiocb->iocb_cmpl = NULL;
	ctiocb->iocb_flag |= LPFC_IO_LIBDFC;
	ctiocb->vport = phba->pport;
	ctiocb->context1 = dd_data;
	ctiocb->context2 = cmp;
	ctiocb->context3 = bmp;
	ctiocb->context_un.ndlp = ndlp;
	ctiocb->iocb_cmpl = lpfc_issue_ct_rsp_cmp;

	dd_data->type = TYPE_IOCB;
	dd_data->set_job = job;
	dd_data->context_un.iocb.cmdiocbq = ctiocb;
	dd_data->context_un.iocb.ndlp = ndlp;
	dd_data->context_un.iocb.rmp = NULL;
	job->dd_data = dd_data;

	if (phba->cfg_poll & DISABLE_FCP_RING_INT) {
		if (lpfc_readl(phba->HCregaddr, &creg_val)) {
			rc = -IOCB_ERROR;
			goto issue_ct_rsp_exit;
		}
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
	struct lpfc_dmabuf *bmp = NULL, *cmp = NULL;
	int bpl_entries;
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
	bpl_entries = (LPFC_BPL_SIZE/sizeof(struct ulp_bde64));
	cmp = lpfc_alloc_bsg_buffers(phba, job->request_payload.payload_len,
				     1, bpl, &bpl_entries);
	if (!cmp) {
		rc = -ENOMEM;
		goto send_mgmt_rsp_free_bmp;
	}
	lpfc_bsg_copy_data(cmp, &job->request_payload,
			   job->request_payload.payload_len, 1);

	rc = lpfc_issue_ct_rsp(phba, job, tag, cmp, bmp, bpl_entries);

	if (rc == IOCB_SUCCESS)
		return 0; /* done for now */

	rc = -EACCES;

	lpfc_free_bsg_buffers(phba, cmp);

send_mgmt_rsp_free_bmp:
	if (bmp->virt)
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(bmp);
send_mgmt_rsp_exit:
	/* make error code available to userspace */
	job->reply->result = rc;
	job->dd_data = NULL;
	return rc;
}

/**
 * lpfc_bsg_diag_mode_enter - process preparing into device diag loopback mode
 * @phba: Pointer to HBA context object.
 *
 * This function is responsible for preparing driver for diag loopback
 * on device.
 */
static int
lpfc_bsg_diag_mode_enter(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host *shost;
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	int i = 0;

	psli = &phba->sli;
	if (!psli)
		return -ENODEV;

	pring = &psli->ring[LPFC_FCP_RING];
	if (!pring)
		return -ENODEV;

	if ((phba->link_state == LPFC_HBA_ERROR) ||
	    (psli->sli_flag & LPFC_BLOCK_MGMT_IO) ||
	    (!(psli->sli_flag & LPFC_SLI_ACTIVE)))
		return -EACCES;

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

	while (!list_empty(&pring->txcmplq)) {
		if (i++ > 500)  /* wait up to 5 seconds */
			break;
		msleep(10);
	}
	return 0;
}

/**
 * lpfc_bsg_diag_mode_exit - exit process from device diag loopback mode
 * @phba: Pointer to HBA context object.
 *
 * This function is responsible for driver exit processing of setting up
 * diag loopback mode on device.
 */
static void
lpfc_bsg_diag_mode_exit(struct lpfc_hba *phba)
{
	struct Scsi_Host *shost;
	struct lpfc_vport **vports;
	int i;

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
	return;
}

/**
 * lpfc_sli3_bsg_diag_loopback_mode - process an sli3 bsg vendor command
 * @phba: Pointer to HBA context object.
 * @job: LPFC_BSG_VENDOR_DIAG_MODE
 *
 * This function is responsible for placing an sli3  port into diagnostic
 * loopback mode in order to perform a diagnostic loopback test.
 * All new scsi requests are blocked, a small delay is used to allow the
 * scsi requests to complete then the link is brought down. If the link is
 * is placed in loopback mode then scsi requests are again allowed
 * so the scsi mid-layer doesn't give up on the port.
 * All of this is done in-line.
 */
static int
lpfc_sli3_bsg_diag_loopback_mode(struct lpfc_hba *phba, struct fc_bsg_job *job)
{
	struct diag_mode_set *loopback_mode;
	uint32_t link_flags;
	uint32_t timeout;
	LPFC_MBOXQ_t *pmboxq  = NULL;
	int mbxstatus = MBX_SUCCESS;
	int i = 0;
	int rc = 0;

	/* no data to return just the return code */
	job->reply->reply_payload_rcv_len = 0;

	if (job->request_len < sizeof(struct fc_bsg_request) +
	    sizeof(struct diag_mode_set)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2738 Received DIAG MODE request size:%d "
				"below the minimum size:%d\n",
				job->request_len,
				(int)(sizeof(struct fc_bsg_request) +
				sizeof(struct diag_mode_set)));
		rc = -EINVAL;
		goto job_error;
	}

	rc = lpfc_bsg_diag_mode_enter(phba);
	if (rc)
		goto job_error;

	/* bring the link to diagnostic mode */
	loopback_mode = (struct diag_mode_set *)
		job->request->rqst_data.h_vendor.vendor_cmd;
	link_flags = loopback_mode->type;
	timeout = loopback_mode->timeout * 100;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq) {
		rc = -ENOMEM;
		goto loopback_mode_exit;
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
			spin_lock_irq(&phba->hbalock);
			phba->link_flag |= LS_LOOPBACK_MODE;
			spin_unlock_irq(&phba->hbalock);
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
	lpfc_bsg_diag_mode_exit(phba);

	/*
	 * Let SLI layer release mboxq if mbox command completed after timeout.
	 */
	if (pmboxq && mbxstatus != MBX_TIMEOUT)
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
 * lpfc_sli4_bsg_set_link_diag_state - set sli4 link diag state
 * @phba: Pointer to HBA context object.
 * @diag: Flag for set link to diag or nomral operation state.
 *
 * This function is responsible for issuing a sli4 mailbox command for setting
 * link to either diag state or normal operation state.
 */
static int
lpfc_sli4_bsg_set_link_diag_state(struct lpfc_hba *phba, uint32_t diag)
{
	LPFC_MBOXQ_t *pmboxq;
	struct lpfc_mbx_set_link_diag_state *link_diag_state;
	uint32_t req_len, alloc_len;
	int mbxstatus = MBX_SUCCESS, rc;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq)
		return -ENOMEM;

	req_len = (sizeof(struct lpfc_mbx_set_link_diag_state) -
		   sizeof(struct lpfc_sli4_cfg_mhdr));
	alloc_len = lpfc_sli4_config(phba, pmboxq, LPFC_MBOX_SUBSYSTEM_FCOE,
				LPFC_MBOX_OPCODE_FCOE_LINK_DIAG_STATE,
				req_len, LPFC_SLI4_MBX_EMBED);
	if (alloc_len != req_len) {
		rc = -ENOMEM;
		goto link_diag_state_set_out;
	}
	lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"3128 Set link to diagnostic state:x%x (x%x/x%x)\n",
			diag, phba->sli4_hba.lnk_info.lnk_tp,
			phba->sli4_hba.lnk_info.lnk_no);

	link_diag_state = &pmboxq->u.mqe.un.link_diag_state;
	bf_set(lpfc_mbx_set_diag_state_diag_bit_valid, &link_diag_state->u.req,
	       LPFC_DIAG_STATE_DIAG_BIT_VALID_CHANGE);
	bf_set(lpfc_mbx_set_diag_state_link_num, &link_diag_state->u.req,
	       phba->sli4_hba.lnk_info.lnk_no);
	bf_set(lpfc_mbx_set_diag_state_link_type, &link_diag_state->u.req,
	       phba->sli4_hba.lnk_info.lnk_tp);
	if (diag)
		bf_set(lpfc_mbx_set_diag_state_diag,
		       &link_diag_state->u.req, 1);
	else
		bf_set(lpfc_mbx_set_diag_state_diag,
		       &link_diag_state->u.req, 0);

	mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq, LPFC_MBOX_TMO);

	if ((mbxstatus == MBX_SUCCESS) && (pmboxq->u.mb.mbxStatus == 0))
		rc = 0;
	else
		rc = -ENODEV;

link_diag_state_set_out:
	if (pmboxq && (mbxstatus != MBX_TIMEOUT))
		mempool_free(pmboxq, phba->mbox_mem_pool);

	return rc;
}

/**
 * lpfc_sli4_bsg_set_internal_loopback - set sli4 internal loopback diagnostic
 * @phba: Pointer to HBA context object.
 *
 * This function is responsible for issuing a sli4 mailbox command for setting
 * up internal loopback diagnostic.
 */
static int
lpfc_sli4_bsg_set_internal_loopback(struct lpfc_hba *phba)
{
	LPFC_MBOXQ_t *pmboxq;
	uint32_t req_len, alloc_len;
	struct lpfc_mbx_set_link_diag_loopback *link_diag_loopback;
	int mbxstatus = MBX_SUCCESS, rc = 0;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq)
		return -ENOMEM;
	req_len = (sizeof(struct lpfc_mbx_set_link_diag_loopback) -
		   sizeof(struct lpfc_sli4_cfg_mhdr));
	alloc_len = lpfc_sli4_config(phba, pmboxq, LPFC_MBOX_SUBSYSTEM_FCOE,
				LPFC_MBOX_OPCODE_FCOE_LINK_DIAG_LOOPBACK,
				req_len, LPFC_SLI4_MBX_EMBED);
	if (alloc_len != req_len) {
		mempool_free(pmboxq, phba->mbox_mem_pool);
		return -ENOMEM;
	}
	link_diag_loopback = &pmboxq->u.mqe.un.link_diag_loopback;
	bf_set(lpfc_mbx_set_diag_state_link_num,
	       &link_diag_loopback->u.req, phba->sli4_hba.lnk_info.lnk_no);
	bf_set(lpfc_mbx_set_diag_state_link_type,
	       &link_diag_loopback->u.req, phba->sli4_hba.lnk_info.lnk_tp);
	bf_set(lpfc_mbx_set_diag_lpbk_type, &link_diag_loopback->u.req,
	       LPFC_DIAG_LOOPBACK_TYPE_INTERNAL);

	mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq, LPFC_MBOX_TMO);
	if ((mbxstatus != MBX_SUCCESS) || (pmboxq->u.mb.mbxStatus)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"3127 Failed setup loopback mode mailbox "
				"command, rc:x%x, status:x%x\n", mbxstatus,
				pmboxq->u.mb.mbxStatus);
		rc = -ENODEV;
	}
	if (pmboxq && (mbxstatus != MBX_TIMEOUT))
		mempool_free(pmboxq, phba->mbox_mem_pool);
	return rc;
}

/**
 * lpfc_sli4_diag_fcport_reg_setup - setup port registrations for diagnostic
 * @phba: Pointer to HBA context object.
 *
 * This function set up SLI4 FC port registrations for diagnostic run, which
 * includes all the rpis, vfi, and also vpi.
 */
static int
lpfc_sli4_diag_fcport_reg_setup(struct lpfc_hba *phba)
{
	int rc;

	if (phba->pport->fc_flag & FC_VFI_REGISTERED) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"3136 Port still had vfi registered: "
				"mydid:x%x, fcfi:%d, vfi:%d, vpi:%d\n",
				phba->pport->fc_myDID, phba->fcf.fcfi,
				phba->sli4_hba.vfi_ids[phba->pport->vfi],
				phba->vpi_ids[phba->pport->vpi]);
		return -EINVAL;
	}
	rc = lpfc_issue_reg_vfi(phba->pport);
	return rc;
}

/**
 * lpfc_sli4_bsg_diag_loopback_mode - process an sli4 bsg vendor command
 * @phba: Pointer to HBA context object.
 * @job: LPFC_BSG_VENDOR_DIAG_MODE
 *
 * This function is responsible for placing an sli4 port into diagnostic
 * loopback mode in order to perform a diagnostic loopback test.
 */
static int
lpfc_sli4_bsg_diag_loopback_mode(struct lpfc_hba *phba, struct fc_bsg_job *job)
{
	struct diag_mode_set *loopback_mode;
	uint32_t link_flags, timeout;
	int i, rc = 0;

	/* no data to return just the return code */
	job->reply->reply_payload_rcv_len = 0;

	if (job->request_len < sizeof(struct fc_bsg_request) +
	    sizeof(struct diag_mode_set)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"3011 Received DIAG MODE request size:%d "
				"below the minimum size:%d\n",
				job->request_len,
				(int)(sizeof(struct fc_bsg_request) +
				sizeof(struct diag_mode_set)));
		rc = -EINVAL;
		goto job_error;
	}

	rc = lpfc_bsg_diag_mode_enter(phba);
	if (rc)
		goto job_error;

	/* indicate we are in loobpack diagnostic mode */
	spin_lock_irq(&phba->hbalock);
	phba->link_flag |= LS_LOOPBACK_MODE;
	spin_unlock_irq(&phba->hbalock);

	/* reset port to start frome scratch */
	rc = lpfc_selective_reset(phba);
	if (rc)
		goto job_error;

	/* bring the link to diagnostic mode */
	lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"3129 Bring link to diagnostic state.\n");
	loopback_mode = (struct diag_mode_set *)
		job->request->rqst_data.h_vendor.vendor_cmd;
	link_flags = loopback_mode->type;
	timeout = loopback_mode->timeout * 100;

	rc = lpfc_sli4_bsg_set_link_diag_state(phba, 1);
	if (rc) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"3130 Failed to bring link to diagnostic "
				"state, rc:x%x\n", rc);
		goto loopback_mode_exit;
	}

	/* wait for link down before proceeding */
	i = 0;
	while (phba->link_state != LPFC_LINK_DOWN) {
		if (i++ > timeout) {
			rc = -ETIMEDOUT;
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"3131 Timeout waiting for link to "
					"diagnostic mode, timeout:%d ms\n",
					timeout * 10);
			goto loopback_mode_exit;
		}
		msleep(10);
	}

	/* set up loopback mode */
	lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"3132 Set up loopback mode:x%x\n", link_flags);

	if (link_flags == INTERNAL_LOOP_BACK)
		rc = lpfc_sli4_bsg_set_internal_loopback(phba);
	else if (link_flags == EXTERNAL_LOOP_BACK)
		rc = lpfc_hba_init_link_fc_topology(phba,
						    FLAGS_TOPOLOGY_MODE_PT_PT,
						    MBX_NOWAIT);
	else {
		rc = -EINVAL;
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"3141 Loopback mode:x%x not supported\n",
				link_flags);
		goto loopback_mode_exit;
	}

	if (!rc) {
		/* wait for the link attention interrupt */
		msleep(100);
		i = 0;
		while (phba->link_state < LPFC_LINK_UP) {
			if (i++ > timeout) {
				rc = -ETIMEDOUT;
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"3137 Timeout waiting for link up "
					"in loopback mode, timeout:%d ms\n",
					timeout * 10);
				break;
			}
			msleep(10);
		}
	}

	/* port resource registration setup for loopback diagnostic */
	if (!rc) {
		/* set up a none zero myDID for loopback test */
		phba->pport->fc_myDID = 1;
		rc = lpfc_sli4_diag_fcport_reg_setup(phba);
	} else
		goto loopback_mode_exit;

	if (!rc) {
		/* wait for the port ready */
		msleep(100);
		i = 0;
		while (phba->link_state != LPFC_HBA_READY) {
			if (i++ > timeout) {
				rc = -ETIMEDOUT;
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"3133 Timeout waiting for port "
					"loopback mode ready, timeout:%d ms\n",
					timeout * 10);
				break;
			}
			msleep(10);
		}
	}

loopback_mode_exit:
	/* clear loopback diagnostic mode */
	if (rc) {
		spin_lock_irq(&phba->hbalock);
		phba->link_flag &= ~LS_LOOPBACK_MODE;
		spin_unlock_irq(&phba->hbalock);
	}
	lpfc_bsg_diag_mode_exit(phba);

job_error:
	/* make error code available to userspace */
	job->reply->result = rc;
	/* complete the job back to userspace if no error */
	if (rc == 0)
		job->job_done(job);
	return rc;
}

/**
 * lpfc_bsg_diag_loopback_mode - bsg vendor command for diag loopback mode
 * @job: LPFC_BSG_VENDOR_DIAG_MODE
 *
 * This function is responsible for responding to check and dispatch bsg diag
 * command from the user to proper driver action routines.
 */
static int
lpfc_bsg_diag_loopback_mode(struct fc_bsg_job *job)
{
	struct Scsi_Host *shost;
	struct lpfc_vport *vport;
	struct lpfc_hba *phba;
	int rc;

	shost = job->shost;
	if (!shost)
		return -ENODEV;
	vport = (struct lpfc_vport *)job->shost->hostdata;
	if (!vport)
		return -ENODEV;
	phba = vport->phba;
	if (!phba)
		return -ENODEV;

	if (phba->sli_rev < LPFC_SLI_REV4)
		rc = lpfc_sli3_bsg_diag_loopback_mode(phba, job);
	else if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) ==
		 LPFC_SLI_INTF_IF_TYPE_2)
		rc = lpfc_sli4_bsg_diag_loopback_mode(phba, job);
	else
		rc = -ENODEV;

	return rc;
}

/**
 * lpfc_sli4_bsg_diag_mode_end - sli4 bsg vendor command for ending diag mode
 * @job: LPFC_BSG_VENDOR_DIAG_MODE_END
 *
 * This function is responsible for responding to check and dispatch bsg diag
 * command from the user to proper driver action routines.
 */
static int
lpfc_sli4_bsg_diag_mode_end(struct fc_bsg_job *job)
{
	struct Scsi_Host *shost;
	struct lpfc_vport *vport;
	struct lpfc_hba *phba;
	struct diag_mode_set *loopback_mode_end_cmd;
	uint32_t timeout;
	int rc, i;

	shost = job->shost;
	if (!shost)
		return -ENODEV;
	vport = (struct lpfc_vport *)job->shost->hostdata;
	if (!vport)
		return -ENODEV;
	phba = vport->phba;
	if (!phba)
		return -ENODEV;

	if (phba->sli_rev < LPFC_SLI_REV4)
		return -ENODEV;
	if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) !=
	    LPFC_SLI_INTF_IF_TYPE_2)
		return -ENODEV;

	/* clear loopback diagnostic mode */
	spin_lock_irq(&phba->hbalock);
	phba->link_flag &= ~LS_LOOPBACK_MODE;
	spin_unlock_irq(&phba->hbalock);
	loopback_mode_end_cmd = (struct diag_mode_set *)
			job->request->rqst_data.h_vendor.vendor_cmd;
	timeout = loopback_mode_end_cmd->timeout * 100;

	rc = lpfc_sli4_bsg_set_link_diag_state(phba, 0);
	if (rc) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"3139 Failed to bring link to diagnostic "
				"state, rc:x%x\n", rc);
		goto loopback_mode_end_exit;
	}

	/* wait for link down before proceeding */
	i = 0;
	while (phba->link_state != LPFC_LINK_DOWN) {
		if (i++ > timeout) {
			rc = -ETIMEDOUT;
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"3140 Timeout waiting for link to "
					"diagnostic mode_end, timeout:%d ms\n",
					timeout * 10);
			/* there is nothing much we can do here */
			break;
		}
		msleep(10);
	}

	/* reset port resource registrations */
	rc = lpfc_selective_reset(phba);
	phba->pport->fc_myDID = 0;

loopback_mode_end_exit:
	/* make return code available to userspace */
	job->reply->result = rc;
	/* complete the job back to userspace if no error */
	if (rc == 0)
		job->job_done(job);
	return rc;
}

/**
 * lpfc_sli4_bsg_link_diag_test - sli4 bsg vendor command for diag link test
 * @job: LPFC_BSG_VENDOR_DIAG_LINK_TEST
 *
 * This function is to perform SLI4 diag link test request from the user
 * applicaiton.
 */
static int
lpfc_sli4_bsg_link_diag_test(struct fc_bsg_job *job)
{
	struct Scsi_Host *shost;
	struct lpfc_vport *vport;
	struct lpfc_hba *phba;
	LPFC_MBOXQ_t *pmboxq;
	struct sli4_link_diag *link_diag_test_cmd;
	uint32_t req_len, alloc_len;
	uint32_t timeout;
	struct lpfc_mbx_run_link_diag_test *run_link_diag_test;
	union lpfc_sli4_cfg_shdr *shdr;
	uint32_t shdr_status, shdr_add_status;
	struct diag_status *diag_status_reply;
	int mbxstatus, rc = 0;

	shost = job->shost;
	if (!shost) {
		rc = -ENODEV;
		goto job_error;
	}
	vport = (struct lpfc_vport *)job->shost->hostdata;
	if (!vport) {
		rc = -ENODEV;
		goto job_error;
	}
	phba = vport->phba;
	if (!phba) {
		rc = -ENODEV;
		goto job_error;
	}

	if (phba->sli_rev < LPFC_SLI_REV4) {
		rc = -ENODEV;
		goto job_error;
	}
	if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) !=
	    LPFC_SLI_INTF_IF_TYPE_2) {
		rc = -ENODEV;
		goto job_error;
	}

	if (job->request_len < sizeof(struct fc_bsg_request) +
	    sizeof(struct sli4_link_diag)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"3013 Received LINK DIAG TEST request "
				" size:%d below the minimum size:%d\n",
				job->request_len,
				(int)(sizeof(struct fc_bsg_request) +
				sizeof(struct sli4_link_diag)));
		rc = -EINVAL;
		goto job_error;
	}

	rc = lpfc_bsg_diag_mode_enter(phba);
	if (rc)
		goto job_error;

	link_diag_test_cmd = (struct sli4_link_diag *)
			 job->request->rqst_data.h_vendor.vendor_cmd;
	timeout = link_diag_test_cmd->timeout * 100;

	rc = lpfc_sli4_bsg_set_link_diag_state(phba, 1);

	if (rc)
		goto job_error;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq) {
		rc = -ENOMEM;
		goto link_diag_test_exit;
	}

	req_len = (sizeof(struct lpfc_mbx_set_link_diag_state) -
		   sizeof(struct lpfc_sli4_cfg_mhdr));
	alloc_len = lpfc_sli4_config(phba, pmboxq, LPFC_MBOX_SUBSYSTEM_FCOE,
				     LPFC_MBOX_OPCODE_FCOE_LINK_DIAG_STATE,
				     req_len, LPFC_SLI4_MBX_EMBED);
	if (alloc_len != req_len) {
		rc = -ENOMEM;
		goto link_diag_test_exit;
	}
	run_link_diag_test = &pmboxq->u.mqe.un.link_diag_test;
	bf_set(lpfc_mbx_run_diag_test_link_num, &run_link_diag_test->u.req,
	       phba->sli4_hba.lnk_info.lnk_no);
	bf_set(lpfc_mbx_run_diag_test_link_type, &run_link_diag_test->u.req,
	       phba->sli4_hba.lnk_info.lnk_tp);
	bf_set(lpfc_mbx_run_diag_test_test_id, &run_link_diag_test->u.req,
	       link_diag_test_cmd->test_id);
	bf_set(lpfc_mbx_run_diag_test_loops, &run_link_diag_test->u.req,
	       link_diag_test_cmd->loops);
	bf_set(lpfc_mbx_run_diag_test_test_ver, &run_link_diag_test->u.req,
	       link_diag_test_cmd->test_version);
	bf_set(lpfc_mbx_run_diag_test_err_act, &run_link_diag_test->u.req,
	       link_diag_test_cmd->error_action);

	mbxstatus = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);

	shdr = (union lpfc_sli4_cfg_shdr *)
		&pmboxq->u.mqe.un.sli4_config.header.cfg_shdr;
	shdr_status = bf_get(lpfc_mbox_hdr_status, &shdr->response);
	shdr_add_status = bf_get(lpfc_mbox_hdr_add_status, &shdr->response);
	if (shdr_status || shdr_add_status || mbxstatus) {
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"3010 Run link diag test mailbox failed with "
				"mbx_status x%x status x%x, add_status x%x\n",
				mbxstatus, shdr_status, shdr_add_status);
	}

	diag_status_reply = (struct diag_status *)
			    job->reply->reply_data.vendor_reply.vendor_rsp;

	if (job->reply_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct diag_status)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"3012 Received Run link diag test reply "
				"below minimum size (%d): reply_len:%d\n",
				(int)(sizeof(struct fc_bsg_request) +
				sizeof(struct diag_status)),
				job->reply_len);
		rc = -EINVAL;
		goto job_error;
	}

	diag_status_reply->mbox_status = mbxstatus;
	diag_status_reply->shdr_status = shdr_status;
	diag_status_reply->shdr_add_status = shdr_add_status;

link_diag_test_exit:
	rc = lpfc_sli4_bsg_set_link_diag_state(phba, 0);

	if (pmboxq)
		mempool_free(pmboxq, phba->mbox_mem_pool);

	lpfc_bsg_diag_mode_exit(phba);

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
static int lpfcdiag_loop_self_reg(struct lpfc_hba *phba, uint16_t *rpi)
{
	LPFC_MBOXQ_t *mbox;
	struct lpfc_dmabuf *dmabuff;
	int status;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	if (phba->sli_rev < LPFC_SLI_REV4)
		status = lpfc_reg_rpi(phba, 0, phba->pport->fc_myDID,
				(uint8_t *)&phba->pport->fc_sparam,
				mbox, *rpi);
	else {
		*rpi = lpfc_sli4_alloc_rpi(phba);
		status = lpfc_reg_rpi(phba, phba->pport->vpi,
				phba->pport->fc_myDID,
				(uint8_t *)&phba->pport->fc_sparam,
				mbox, *rpi);
	}

	if (status) {
		mempool_free(mbox, phba->mbox_mem_pool);
		if (phba->sli_rev == LPFC_SLI_REV4)
			lpfc_sli4_free_rpi(phba, *rpi);
		return -ENOMEM;
	}

	dmabuff = (struct lpfc_dmabuf *) mbox->context1;
	mbox->context1 = NULL;
	mbox->context2 = NULL;
	status = lpfc_sli_issue_mbox_wait(phba, mbox, LPFC_MBOX_TMO);

	if ((status != MBX_SUCCESS) || (mbox->u.mb.mbxStatus)) {
		lpfc_mbuf_free(phba, dmabuff->virt, dmabuff->phys);
		kfree(dmabuff);
		if (status != MBX_TIMEOUT)
			mempool_free(mbox, phba->mbox_mem_pool);
		if (phba->sli_rev == LPFC_SLI_REV4)
			lpfc_sli4_free_rpi(phba, *rpi);
		return -ENODEV;
	}

	if (phba->sli_rev < LPFC_SLI_REV4)
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
		return -ENOMEM;

	if (phba->sli_rev < LPFC_SLI_REV4)
		lpfc_unreg_login(phba, 0, rpi, mbox);
	else
		lpfc_unreg_login(phba, phba->pport->vpi,
				 phba->sli4_hba.rpi_ids[rpi], mbox);

	status = lpfc_sli_issue_mbox_wait(phba, mbox, LPFC_MBOX_TMO);

	if ((status != MBX_SUCCESS) || (mbox->u.mb.mbxStatus)) {
		if (status != MBX_TIMEOUT)
			mempool_free(mbox, phba->mbox_mem_pool);
		return -EIO;
	}
	mempool_free(mbox, phba->mbox_mem_pool);
	if (phba->sli_rev == LPFC_SLI_REV4)
		lpfc_sli4_free_rpi(phba, rpi);
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
	int time_left;
	int iocb_stat = IOCB_SUCCESS;
	unsigned long flags;

	*txxri = 0;
	*rxxri = 0;
	evt = lpfc_bsg_event_new(FC_REG_CT_EVENT, current->pid,
				SLI_CT_ELX_LOOPBACK);
	if (!evt)
		return -ENOMEM;

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
		ret_val = -ENOMEM;
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
	cmdiocbq->iocb_cmpl = NULL;

	iocb_stat = lpfc_sli_issue_iocb_wait(phba, LPFC_ELS_RING, cmdiocbq,
				rspiocbq,
				(phba->fc_ratov * 2)
				+ LPFC_DRVR_TIMEOUT);
	if (iocb_stat) {
		ret_val = -EIO;
		goto err_get_xri_exit;
	}
	*txxri =  rsp->ulpContext;

	evt->waiting = 1;
	evt->wait_time_stamp = jiffies;
	time_left = wait_event_interruptible_timeout(
		evt->wq, !list_empty(&evt->events_to_see),
		msecs_to_jiffies(1000 *
			((phba->fc_ratov * 2) + LPFC_DRVR_TIMEOUT)));
	if (list_empty(&evt->events_to_see))
		ret_val = (time_left) ? -EINTR : -ETIMEDOUT;
	else {
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

	if (cmdiocbq && (iocb_stat != IOCB_TIMEDOUT))
		lpfc_sli_release_iocbq(phba, cmdiocbq);
	if (rspiocbq)
		lpfc_sli_release_iocbq(phba, rspiocbq);
	return ret_val;
}

/**
 * lpfc_bsg_dma_page_alloc - allocate a bsg mbox page sized dma buffers
 * @phba: Pointer to HBA context object
 *
 * This function allocates BSG_MBOX_SIZE (4KB) page size dma buffer and.
 * returns the pointer to the buffer.
 **/
static struct lpfc_dmabuf *
lpfc_bsg_dma_page_alloc(struct lpfc_hba *phba)
{
	struct lpfc_dmabuf *dmabuf;
	struct pci_dev *pcidev = phba->pcidev;

	/* allocate dma buffer struct */
	dmabuf = kmalloc(sizeof(struct lpfc_dmabuf), GFP_KERNEL);
	if (!dmabuf)
		return NULL;

	INIT_LIST_HEAD(&dmabuf->list);

	/* now, allocate dma buffer */
	dmabuf->virt = dma_alloc_coherent(&pcidev->dev, BSG_MBOX_SIZE,
					  &(dmabuf->phys), GFP_KERNEL);

	if (!dmabuf->virt) {
		kfree(dmabuf);
		return NULL;
	}
	memset((uint8_t *)dmabuf->virt, 0, BSG_MBOX_SIZE);

	return dmabuf;
}

/**
 * lpfc_bsg_dma_page_free - free a bsg mbox page sized dma buffer
 * @phba: Pointer to HBA context object.
 * @dmabuf: Pointer to the bsg mbox page sized dma buffer descriptor.
 *
 * This routine just simply frees a dma buffer and its associated buffer
 * descriptor referred by @dmabuf.
 **/
static void
lpfc_bsg_dma_page_free(struct lpfc_hba *phba, struct lpfc_dmabuf *dmabuf)
{
	struct pci_dev *pcidev = phba->pcidev;

	if (!dmabuf)
		return;

	if (dmabuf->virt)
		dma_free_coherent(&pcidev->dev, BSG_MBOX_SIZE,
				  dmabuf->virt, dmabuf->phys);
	kfree(dmabuf);
	return;
}

/**
 * lpfc_bsg_dma_page_list_free - free a list of bsg mbox page sized dma buffers
 * @phba: Pointer to HBA context object.
 * @dmabuf_list: Pointer to a list of bsg mbox page sized dma buffer descs.
 *
 * This routine just simply frees all dma buffers and their associated buffer
 * descriptors referred by @dmabuf_list.
 **/
static void
lpfc_bsg_dma_page_list_free(struct lpfc_hba *phba,
			    struct list_head *dmabuf_list)
{
	struct lpfc_dmabuf *dmabuf, *next_dmabuf;

	if (list_empty(dmabuf_list))
		return;

	list_for_each_entry_safe(dmabuf, next_dmabuf, dmabuf_list, list) {
		list_del_init(&dmabuf->list);
		lpfc_bsg_dma_page_free(phba, dmabuf);
	}
	return;
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
 * This function allocates and posts a data buffer of sufficient size to receive
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
	int iocb_stat;
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
		ret_val = -ENOMEM;
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

		iocb_stat = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, cmdiocbq,
						0);
		if (iocb_stat == IOCB_ERROR) {
			diag_cmd_data_free(phba,
				(struct lpfc_dmabufext *)mp[0]);
			if (mp[1])
				diag_cmd_data_free(phba,
					  (struct lpfc_dmabufext *)mp[1]);
			dmp = list_entry(next, struct lpfc_dmabuf, list);
			ret_val = -EIO;
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
			ret_val = -EIO;
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
 * lpfc_bsg_diag_loopback_run - run loopback on a port by issue ct cmd to itself
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
lpfc_bsg_diag_loopback_run(struct fc_bsg_job *job)
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
	uint16_t rpi = 0;
	struct lpfc_iocbq *cmdiocbq, *rspiocbq = NULL;
	IOCB_t *cmd, *rsp = NULL;
	struct lpfc_sli_ct_request *ctreq;
	struct lpfc_dmabuf *txbmp;
	struct ulp_bde64 *txbpl = NULL;
	struct lpfc_dmabufext *txbuffer = NULL;
	struct list_head head;
	struct lpfc_dmabuf  *curr;
	uint16_t txxri = 0, rxxri;
	uint32_t num_bde;
	uint8_t *ptr = NULL, *rx_databuf = NULL;
	int rc = 0;
	int time_left;
	int iocb_stat = IOCB_SUCCESS;
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

	if (full_size >= BUF_SZ_4K) {
		/*
		 * Allocate memory for ioctl data. If buffer is bigger than 64k,
		 * then we allocate 64k and re-use that buffer over and over to
		 * xfer the whole block. This is because Linux kernel has a
		 * problem allocating more than 120k of kernel space memory. Saw
		 * problem with GET_FCPTARGETMAPPING...
		 */
		if (size <= (64 * 1024))
			total_mem = full_size;
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
	if (rc)
		goto loopback_test_exit;

	if (phba->sli_rev < LPFC_SLI_REV4) {
		rc = lpfcdiag_loop_get_xri(phba, rpi, &txxri, &rxxri);
		if (rc) {
			lpfcdiag_loop_self_unreg(phba, rpi);
			goto loopback_test_exit;
		}

		rc = lpfcdiag_loop_post_rxbufs(phba, rxxri, full_size);
		if (rc) {
			lpfcdiag_loop_self_unreg(phba, rpi);
			goto loopback_test_exit;
		}
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
	if (phba->sli_rev < LPFC_SLI_REV4)
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

	if (!cmdiocbq || !txbmp || !txbpl || !txbuffer || !txbmp->virt) {
		rc = -ENOMEM;
		goto err_loopback_test_exit;
	}
	if ((phba->sli_rev < LPFC_SLI_REV4) && !rspiocbq) {
		rc = -ENOMEM;
		goto err_loopback_test_exit;
	}

	cmd = &cmdiocbq->iocb;
	if (phba->sli_rev < LPFC_SLI_REV4)
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

	if (phba->sli_rev < LPFC_SLI_REV4) {
		cmd->ulpContext = txxri;
	} else {
		cmd->un.xseq64.bdl.ulpIoTag32 = 0;
		cmd->un.ulpWord[3] = phba->sli4_hba.rpi_ids[rpi];
		cmdiocbq->context3 = txbmp;
		cmdiocbq->sli4_xritag = NO_XRI;
		cmd->unsli3.rcvsli3.ox_id = 0xffff;
	}
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	cmdiocbq->vport = phba->pport;
	cmdiocbq->iocb_cmpl = NULL;
	iocb_stat = lpfc_sli_issue_iocb_wait(phba, LPFC_ELS_RING, cmdiocbq,
					     rspiocbq, (phba->fc_ratov * 2) +
					     LPFC_DRVR_TIMEOUT);

	if ((iocb_stat != IOCB_SUCCESS) || ((phba->sli_rev < LPFC_SLI_REV4) &&
					   (rsp->ulpStatus != IOCB_SUCCESS))) {
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"3126 Failed loopback test issue iocb: "
				"iocb_stat:x%x\n", iocb_stat);
		rc = -EIO;
		goto err_loopback_test_exit;
	}

	evt->waiting = 1;
	time_left = wait_event_interruptible_timeout(
		evt->wq, !list_empty(&evt->events_to_see),
		msecs_to_jiffies(1000 *
			((phba->fc_ratov * 2) + LPFC_DRVR_TIMEOUT)));
	evt->waiting = 0;
	if (list_empty(&evt->events_to_see)) {
		rc = (time_left) ? -EINTR : -ETIMEDOUT;
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"3125 Not receiving unsolicited event, "
				"rc:x%x\n", rc);
	} else {
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

	if ((cmdiocbq != NULL) && (iocb_stat != IOCB_TIMEDOUT))
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
	if (rc == IOCB_SUCCESS)
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
 * lpfc_bsg_issue_mbox_cmpl - lpfc_bsg_issue_mbox mbox completion handler
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
lpfc_bsg_issue_mbox_cmpl(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmboxq)
{
	struct bsg_job_data *dd_data;
	struct fc_bsg_job *job;
	uint32_t size;
	unsigned long flags;
	uint8_t *pmb, *pmb_buf;

	dd_data = pmboxq->context1;

	/*
	 * The outgoing buffer is readily referred from the dma buffer,
	 * just need to get header part from mailboxq structure.
	 */
	pmb = (uint8_t *)&pmboxq->u.mb;
	pmb_buf = (uint8_t *)dd_data->context_un.mbox.mb;
	memcpy(pmb_buf, pmb, sizeof(MAILBOX_t));

	/* Determine if job has been aborted */

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	job = dd_data->set_job;
	if (job) {
		/* Prevent timeout handling from trying to abort job  */
		job->dd_data = NULL;
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	/* Copy the mailbox data to the job if it is still active */

	if (job) {
		size = job->reply_payload.payload_len;
		job->reply->reply_payload_rcv_len =
			sg_copy_from_buffer(job->reply_payload.sg_list,
					    job->reply_payload.sg_cnt,
					    pmb_buf, size);
	}

	dd_data->set_job = NULL;
	mempool_free(dd_data->context_un.mbox.pmboxq, phba->mbox_mem_pool);
	lpfc_bsg_dma_page_free(phba, dd_data->context_un.mbox.dmabuffers);
	kfree(dd_data);

	/* Complete the job if the job is still active */

	if (job) {
		job->reply->result = 0;
		job->job_done(job);
	}
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
	case MBX_READ_TOPOLOGY:
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
			spin_lock_irq(&phba->hbalock);
			phba->link_flag &= ~LS_LOOPBACK_MODE;
			spin_unlock_irq(&phba->hbalock);
			phba->fc_topology = LPFC_TOPOLOGY_PT_PT;
		}
		break;
	case MBX_READ_SPARM64:
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
 * lpfc_bsg_mbox_ext_cleanup - clean up context of multi-buffer mbox session
 * @phba: Pointer to HBA context object.
 *
 * This is routine clean up and reset BSG handling of multi-buffer mbox
 * command session.
 **/
static void
lpfc_bsg_mbox_ext_session_reset(struct lpfc_hba *phba)
{
	if (phba->mbox_ext_buf_ctx.state == LPFC_BSG_MBOX_IDLE)
		return;

	/* free all memory, including dma buffers */
	lpfc_bsg_dma_page_list_free(phba,
				    &phba->mbox_ext_buf_ctx.ext_dmabuf_list);
	lpfc_bsg_dma_page_free(phba, phba->mbox_ext_buf_ctx.mbx_dmabuf);
	/* multi-buffer write mailbox command pass-through complete */
	memset((char *)&phba->mbox_ext_buf_ctx, 0,
	       sizeof(struct lpfc_mbox_ext_buf_ctx));
	INIT_LIST_HEAD(&phba->mbox_ext_buf_ctx.ext_dmabuf_list);

	return;
}

/**
 * lpfc_bsg_issue_mbox_ext_handle_job - job handler for multi-buffer mbox cmpl
 * @phba: Pointer to HBA context object.
 * @pmboxq: Pointer to mailbox command.
 *
 * This is routine handles BSG job for mailbox commands completions with
 * multiple external buffers.
 **/
static struct fc_bsg_job *
lpfc_bsg_issue_mbox_ext_handle_job(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmboxq)
{
	struct bsg_job_data *dd_data;
	struct fc_bsg_job *job;
	uint8_t *pmb, *pmb_buf;
	unsigned long flags;
	uint32_t size;
	int rc = 0;
	struct lpfc_dmabuf *dmabuf;
	struct lpfc_sli_config_mbox *sli_cfg_mbx;
	uint8_t *pmbx;

	dd_data = pmboxq->context1;

	/* Determine if job has been aborted */
	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	job = dd_data->set_job;
	if (job) {
		/* Prevent timeout handling from trying to abort job  */
		job->dd_data = NULL;
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	/*
	 * The outgoing buffer is readily referred from the dma buffer,
	 * just need to get header part from mailboxq structure.
	 */

	pmb = (uint8_t *)&pmboxq->u.mb;
	pmb_buf = (uint8_t *)dd_data->context_un.mbox.mb;
	/* Copy the byte swapped response mailbox back to the user */
	memcpy(pmb_buf, pmb, sizeof(MAILBOX_t));
	/* if there is any non-embedded extended data copy that too */
	dmabuf = phba->mbox_ext_buf_ctx.mbx_dmabuf;
	sli_cfg_mbx = (struct lpfc_sli_config_mbox *)dmabuf->virt;
	if (!bsg_bf_get(lpfc_mbox_hdr_emb,
	    &sli_cfg_mbx->un.sli_config_emb0_subsys.sli_config_hdr)) {
		pmbx = (uint8_t *)dmabuf->virt;
		/* byte swap the extended data following the mailbox command */
		lpfc_sli_pcimem_bcopy(&pmbx[sizeof(MAILBOX_t)],
			&pmbx[sizeof(MAILBOX_t)],
			sli_cfg_mbx->un.sli_config_emb0_subsys.mse[0].buf_len);
	}

	/* Complete the job if the job is still active */

	if (job) {
		size = job->reply_payload.payload_len;
		job->reply->reply_payload_rcv_len =
			sg_copy_from_buffer(job->reply_payload.sg_list,
					    job->reply_payload.sg_cnt,
					    pmb_buf, size);

		/* result for successful */
		job->reply->result = 0;

		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2937 SLI_CONFIG ext-buffer maibox command "
				"(x%x/x%x) complete bsg job done, bsize:%d\n",
				phba->mbox_ext_buf_ctx.nembType,
				phba->mbox_ext_buf_ctx.mboxType, size);
		lpfc_idiag_mbxacc_dump_bsg_mbox(phba,
					phba->mbox_ext_buf_ctx.nembType,
					phba->mbox_ext_buf_ctx.mboxType,
					dma_ebuf, sta_pos_addr,
					phba->mbox_ext_buf_ctx.mbx_dmabuf, 0);
	} else {
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"2938 SLI_CONFIG ext-buffer maibox "
				"command (x%x/x%x) failure, rc:x%x\n",
				phba->mbox_ext_buf_ctx.nembType,
				phba->mbox_ext_buf_ctx.mboxType, rc);
	}


	/* state change */
	phba->mbox_ext_buf_ctx.state = LPFC_BSG_MBOX_DONE;
	kfree(dd_data);
	return job;
}

/**
 * lpfc_bsg_issue_read_mbox_ext_cmpl - compl handler for multi-buffer read mbox
 * @phba: Pointer to HBA context object.
 * @pmboxq: Pointer to mailbox command.
 *
 * This is completion handler function for mailbox read commands with multiple
 * external buffers.
 **/
static void
lpfc_bsg_issue_read_mbox_ext_cmpl(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmboxq)
{
	struct fc_bsg_job *job;

	job = lpfc_bsg_issue_mbox_ext_handle_job(phba, pmboxq);

	/* handle the BSG job with mailbox command */
	if (!job)
		pmboxq->u.mb.mbxStatus = MBXERR_ERROR;

	lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"2939 SLI_CONFIG ext-buffer rd maibox command "
			"complete, ctxState:x%x, mbxStatus:x%x\n",
			phba->mbox_ext_buf_ctx.state, pmboxq->u.mb.mbxStatus);

	if (pmboxq->u.mb.mbxStatus || phba->mbox_ext_buf_ctx.numBuf == 1)
		lpfc_bsg_mbox_ext_session_reset(phba);

	/* free base driver mailbox structure memory */
	mempool_free(pmboxq, phba->mbox_mem_pool);

	/* if the job is still active, call job done */
	if (job)
		job->job_done(job);

	return;
}

/**
 * lpfc_bsg_issue_write_mbox_ext_cmpl - cmpl handler for multi-buffer write mbox
 * @phba: Pointer to HBA context object.
 * @pmboxq: Pointer to mailbox command.
 *
 * This is completion handler function for mailbox write commands with multiple
 * external buffers.
 **/
static void
lpfc_bsg_issue_write_mbox_ext_cmpl(struct lpfc_hba *phba, LPFC_MBOXQ_t *pmboxq)
{
	struct fc_bsg_job *job;

	job = lpfc_bsg_issue_mbox_ext_handle_job(phba, pmboxq);

	/* handle the BSG job with the mailbox command */
	if (!job)
		pmboxq->u.mb.mbxStatus = MBXERR_ERROR;

	lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"2940 SLI_CONFIG ext-buffer wr maibox command "
			"complete, ctxState:x%x, mbxStatus:x%x\n",
			phba->mbox_ext_buf_ctx.state, pmboxq->u.mb.mbxStatus);

	/* free all memory, including dma buffers */
	mempool_free(pmboxq, phba->mbox_mem_pool);
	lpfc_bsg_mbox_ext_session_reset(phba);

	/* if the job is still active, call job done */
	if (job)
		job->job_done(job);

	return;
}

static void
lpfc_bsg_sli_cfg_dma_desc_setup(struct lpfc_hba *phba, enum nemb_type nemb_tp,
				uint32_t index, struct lpfc_dmabuf *mbx_dmabuf,
				struct lpfc_dmabuf *ext_dmabuf)
{
	struct lpfc_sli_config_mbox *sli_cfg_mbx;

	/* pointer to the start of mailbox command */
	sli_cfg_mbx = (struct lpfc_sli_config_mbox *)mbx_dmabuf->virt;

	if (nemb_tp == nemb_mse) {
		if (index == 0) {
			sli_cfg_mbx->un.sli_config_emb0_subsys.
				mse[index].pa_hi =
				putPaddrHigh(mbx_dmabuf->phys +
					     sizeof(MAILBOX_t));
			sli_cfg_mbx->un.sli_config_emb0_subsys.
				mse[index].pa_lo =
				putPaddrLow(mbx_dmabuf->phys +
					    sizeof(MAILBOX_t));
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"2943 SLI_CONFIG(mse)[%d], "
					"bufLen:%d, addrHi:x%x, addrLo:x%x\n",
					index,
					sli_cfg_mbx->un.sli_config_emb0_subsys.
					mse[index].buf_len,
					sli_cfg_mbx->un.sli_config_emb0_subsys.
					mse[index].pa_hi,
					sli_cfg_mbx->un.sli_config_emb0_subsys.
					mse[index].pa_lo);
		} else {
			sli_cfg_mbx->un.sli_config_emb0_subsys.
				mse[index].pa_hi =
				putPaddrHigh(ext_dmabuf->phys);
			sli_cfg_mbx->un.sli_config_emb0_subsys.
				mse[index].pa_lo =
				putPaddrLow(ext_dmabuf->phys);
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"2944 SLI_CONFIG(mse)[%d], "
					"bufLen:%d, addrHi:x%x, addrLo:x%x\n",
					index,
					sli_cfg_mbx->un.sli_config_emb0_subsys.
					mse[index].buf_len,
					sli_cfg_mbx->un.sli_config_emb0_subsys.
					mse[index].pa_hi,
					sli_cfg_mbx->un.sli_config_emb0_subsys.
					mse[index].pa_lo);
		}
	} else {
		if (index == 0) {
			sli_cfg_mbx->un.sli_config_emb1_subsys.
				hbd[index].pa_hi =
				putPaddrHigh(mbx_dmabuf->phys +
					     sizeof(MAILBOX_t));
			sli_cfg_mbx->un.sli_config_emb1_subsys.
				hbd[index].pa_lo =
				putPaddrLow(mbx_dmabuf->phys +
					    sizeof(MAILBOX_t));
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"3007 SLI_CONFIG(hbd)[%d], "
					"bufLen:%d, addrHi:x%x, addrLo:x%x\n",
				index,
				bsg_bf_get(lpfc_mbox_sli_config_ecmn_hbd_len,
				&sli_cfg_mbx->un.
				sli_config_emb1_subsys.hbd[index]),
				sli_cfg_mbx->un.sli_config_emb1_subsys.
				hbd[index].pa_hi,
				sli_cfg_mbx->un.sli_config_emb1_subsys.
				hbd[index].pa_lo);

		} else {
			sli_cfg_mbx->un.sli_config_emb1_subsys.
				hbd[index].pa_hi =
				putPaddrHigh(ext_dmabuf->phys);
			sli_cfg_mbx->un.sli_config_emb1_subsys.
				hbd[index].pa_lo =
				putPaddrLow(ext_dmabuf->phys);
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"3008 SLI_CONFIG(hbd)[%d], "
					"bufLen:%d, addrHi:x%x, addrLo:x%x\n",
				index,
				bsg_bf_get(lpfc_mbox_sli_config_ecmn_hbd_len,
				&sli_cfg_mbx->un.
				sli_config_emb1_subsys.hbd[index]),
				sli_cfg_mbx->un.sli_config_emb1_subsys.
				hbd[index].pa_hi,
				sli_cfg_mbx->un.sli_config_emb1_subsys.
				hbd[index].pa_lo);
		}
	}
	return;
}

/**
 * lpfc_bsg_sli_cfg_mse_read_cmd_ext - sli_config non-embedded mailbox cmd read
 * @phba: Pointer to HBA context object.
 * @mb: Pointer to a BSG mailbox object.
 * @nemb_tp: Enumerate of non-embedded mailbox command type.
 * @dmabuff: Pointer to a DMA buffer descriptor.
 *
 * This routine performs SLI_CONFIG (0x9B) read mailbox command operation with
 * non-embedded external bufffers.
 **/
static int
lpfc_bsg_sli_cfg_read_cmd_ext(struct lpfc_hba *phba, struct fc_bsg_job *job,
			      enum nemb_type nemb_tp,
			      struct lpfc_dmabuf *dmabuf)
{
	struct lpfc_sli_config_mbox *sli_cfg_mbx;
	struct dfc_mbox_req *mbox_req;
	struct lpfc_dmabuf *curr_dmabuf, *next_dmabuf;
	uint32_t ext_buf_cnt, ext_buf_index;
	struct lpfc_dmabuf *ext_dmabuf = NULL;
	struct bsg_job_data *dd_data = NULL;
	LPFC_MBOXQ_t *pmboxq = NULL;
	MAILBOX_t *pmb;
	uint8_t *pmbx;
	int rc, i;

	mbox_req =
	   (struct dfc_mbox_req *)job->request->rqst_data.h_vendor.vendor_cmd;

	/* pointer to the start of mailbox command */
	sli_cfg_mbx = (struct lpfc_sli_config_mbox *)dmabuf->virt;

	if (nemb_tp == nemb_mse) {
		ext_buf_cnt = bsg_bf_get(lpfc_mbox_hdr_mse_cnt,
			&sli_cfg_mbx->un.sli_config_emb0_subsys.sli_config_hdr);
		if (ext_buf_cnt > LPFC_MBX_SLI_CONFIG_MAX_MSE) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
					"2945 Handled SLI_CONFIG(mse) rd, "
					"ext_buf_cnt(%d) out of range(%d)\n",
					ext_buf_cnt,
					LPFC_MBX_SLI_CONFIG_MAX_MSE);
			rc = -ERANGE;
			goto job_error;
		}
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2941 Handled SLI_CONFIG(mse) rd, "
				"ext_buf_cnt:%d\n", ext_buf_cnt);
	} else {
		/* sanity check on interface type for support */
		if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) !=
		    LPFC_SLI_INTF_IF_TYPE_2) {
			rc = -ENODEV;
			goto job_error;
		}
		/* nemb_tp == nemb_hbd */
		ext_buf_cnt = sli_cfg_mbx->un.sli_config_emb1_subsys.hbd_count;
		if (ext_buf_cnt > LPFC_MBX_SLI_CONFIG_MAX_HBD) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
					"2946 Handled SLI_CONFIG(hbd) rd, "
					"ext_buf_cnt(%d) out of range(%d)\n",
					ext_buf_cnt,
					LPFC_MBX_SLI_CONFIG_MAX_HBD);
			rc = -ERANGE;
			goto job_error;
		}
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2942 Handled SLI_CONFIG(hbd) rd, "
				"ext_buf_cnt:%d\n", ext_buf_cnt);
	}

	/* before dma descriptor setup */
	lpfc_idiag_mbxacc_dump_bsg_mbox(phba, nemb_tp, mbox_rd, dma_mbox,
					sta_pre_addr, dmabuf, ext_buf_cnt);

	/* reject non-embedded mailbox command with none external buffer */
	if (ext_buf_cnt == 0) {
		rc = -EPERM;
		goto job_error;
	} else if (ext_buf_cnt > 1) {
		/* additional external read buffers */
		for (i = 1; i < ext_buf_cnt; i++) {
			ext_dmabuf = lpfc_bsg_dma_page_alloc(phba);
			if (!ext_dmabuf) {
				rc = -ENOMEM;
				goto job_error;
			}
			list_add_tail(&ext_dmabuf->list,
				      &phba->mbox_ext_buf_ctx.ext_dmabuf_list);
		}
	}

	/* bsg tracking structure */
	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (!dd_data) {
		rc = -ENOMEM;
		goto job_error;
	}

	/* mailbox command structure for base driver */
	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq) {
		rc = -ENOMEM;
		goto job_error;
	}
	memset(pmboxq, 0, sizeof(LPFC_MBOXQ_t));

	/* for the first external buffer */
	lpfc_bsg_sli_cfg_dma_desc_setup(phba, nemb_tp, 0, dmabuf, dmabuf);

	/* for the rest of external buffer descriptors if any */
	if (ext_buf_cnt > 1) {
		ext_buf_index = 1;
		list_for_each_entry_safe(curr_dmabuf, next_dmabuf,
				&phba->mbox_ext_buf_ctx.ext_dmabuf_list, list) {
			lpfc_bsg_sli_cfg_dma_desc_setup(phba, nemb_tp,
						ext_buf_index, dmabuf,
						curr_dmabuf);
			ext_buf_index++;
		}
	}

	/* after dma descriptor setup */
	lpfc_idiag_mbxacc_dump_bsg_mbox(phba, nemb_tp, mbox_rd, dma_mbox,
					sta_pos_addr, dmabuf, ext_buf_cnt);

	/* construct base driver mbox command */
	pmb = &pmboxq->u.mb;
	pmbx = (uint8_t *)dmabuf->virt;
	memcpy(pmb, pmbx, sizeof(*pmb));
	pmb->mbxOwner = OWN_HOST;
	pmboxq->vport = phba->pport;

	/* multi-buffer handling context */
	phba->mbox_ext_buf_ctx.nembType = nemb_tp;
	phba->mbox_ext_buf_ctx.mboxType = mbox_rd;
	phba->mbox_ext_buf_ctx.numBuf = ext_buf_cnt;
	phba->mbox_ext_buf_ctx.mbxTag = mbox_req->extMboxTag;
	phba->mbox_ext_buf_ctx.seqNum = mbox_req->extSeqNum;
	phba->mbox_ext_buf_ctx.mbx_dmabuf = dmabuf;

	/* callback for multi-buffer read mailbox command */
	pmboxq->mbox_cmpl = lpfc_bsg_issue_read_mbox_ext_cmpl;

	/* context fields to callback function */
	pmboxq->context1 = dd_data;
	dd_data->type = TYPE_MBOX;
	dd_data->set_job = job;
	dd_data->context_un.mbox.pmboxq = pmboxq;
	dd_data->context_un.mbox.mb = (MAILBOX_t *)pmbx;
	job->dd_data = dd_data;

	/* state change */
	phba->mbox_ext_buf_ctx.state = LPFC_BSG_MBOX_PORT;

	/*
	 * Non-embedded mailbox subcommand data gets byte swapped here because
	 * the lower level driver code only does the first 64 mailbox words.
	 */
	if ((!bsg_bf_get(lpfc_mbox_hdr_emb,
	    &sli_cfg_mbx->un.sli_config_emb0_subsys.sli_config_hdr)) &&
		(nemb_tp == nemb_mse))
		lpfc_sli_pcimem_bcopy(&pmbx[sizeof(MAILBOX_t)],
			&pmbx[sizeof(MAILBOX_t)],
				sli_cfg_mbx->un.sli_config_emb0_subsys.
					mse[0].buf_len);

	rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_NOWAIT);
	if ((rc == MBX_SUCCESS) || (rc == MBX_BUSY)) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2947 Issued SLI_CONFIG ext-buffer "
				"maibox command, rc:x%x\n", rc);
		return SLI_CONFIG_HANDLED;
	}
	lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
			"2948 Failed to issue SLI_CONFIG ext-buffer "
			"maibox command, rc:x%x\n", rc);
	rc = -EPIPE;

job_error:
	if (pmboxq)
		mempool_free(pmboxq, phba->mbox_mem_pool);
	lpfc_bsg_dma_page_list_free(phba,
				    &phba->mbox_ext_buf_ctx.ext_dmabuf_list);
	kfree(dd_data);
	phba->mbox_ext_buf_ctx.state = LPFC_BSG_MBOX_IDLE;
	return rc;
}

/**
 * lpfc_bsg_sli_cfg_write_cmd_ext - sli_config non-embedded mailbox cmd write
 * @phba: Pointer to HBA context object.
 * @mb: Pointer to a BSG mailbox object.
 * @dmabuff: Pointer to a DMA buffer descriptor.
 *
 * This routine performs SLI_CONFIG (0x9B) write mailbox command operation with
 * non-embedded external bufffers.
 **/
static int
lpfc_bsg_sli_cfg_write_cmd_ext(struct lpfc_hba *phba, struct fc_bsg_job *job,
			       enum nemb_type nemb_tp,
			       struct lpfc_dmabuf *dmabuf)
{
	struct dfc_mbox_req *mbox_req;
	struct lpfc_sli_config_mbox *sli_cfg_mbx;
	uint32_t ext_buf_cnt;
	struct bsg_job_data *dd_data = NULL;
	LPFC_MBOXQ_t *pmboxq = NULL;
	MAILBOX_t *pmb;
	uint8_t *mbx;
	int rc = SLI_CONFIG_NOT_HANDLED, i;

	mbox_req =
	   (struct dfc_mbox_req *)job->request->rqst_data.h_vendor.vendor_cmd;

	/* pointer to the start of mailbox command */
	sli_cfg_mbx = (struct lpfc_sli_config_mbox *)dmabuf->virt;

	if (nemb_tp == nemb_mse) {
		ext_buf_cnt = bsg_bf_get(lpfc_mbox_hdr_mse_cnt,
			&sli_cfg_mbx->un.sli_config_emb0_subsys.sli_config_hdr);
		if (ext_buf_cnt > LPFC_MBX_SLI_CONFIG_MAX_MSE) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
					"2953 Failed SLI_CONFIG(mse) wr, "
					"ext_buf_cnt(%d) out of range(%d)\n",
					ext_buf_cnt,
					LPFC_MBX_SLI_CONFIG_MAX_MSE);
			return -ERANGE;
		}
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2949 Handled SLI_CONFIG(mse) wr, "
				"ext_buf_cnt:%d\n", ext_buf_cnt);
	} else {
		/* sanity check on interface type for support */
		if (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) !=
		    LPFC_SLI_INTF_IF_TYPE_2)
			return -ENODEV;
		/* nemb_tp == nemb_hbd */
		ext_buf_cnt = sli_cfg_mbx->un.sli_config_emb1_subsys.hbd_count;
		if (ext_buf_cnt > LPFC_MBX_SLI_CONFIG_MAX_HBD) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
					"2954 Failed SLI_CONFIG(hbd) wr, "
					"ext_buf_cnt(%d) out of range(%d)\n",
					ext_buf_cnt,
					LPFC_MBX_SLI_CONFIG_MAX_HBD);
			return -ERANGE;
		}
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2950 Handled SLI_CONFIG(hbd) wr, "
				"ext_buf_cnt:%d\n", ext_buf_cnt);
	}

	/* before dma buffer descriptor setup */
	lpfc_idiag_mbxacc_dump_bsg_mbox(phba, nemb_tp, mbox_wr, dma_mbox,
					sta_pre_addr, dmabuf, ext_buf_cnt);

	if (ext_buf_cnt == 0)
		return -EPERM;

	/* for the first external buffer */
	lpfc_bsg_sli_cfg_dma_desc_setup(phba, nemb_tp, 0, dmabuf, dmabuf);

	/* after dma descriptor setup */
	lpfc_idiag_mbxacc_dump_bsg_mbox(phba, nemb_tp, mbox_wr, dma_mbox,
					sta_pos_addr, dmabuf, ext_buf_cnt);

	/* log for looking forward */
	for (i = 1; i < ext_buf_cnt; i++) {
		if (nemb_tp == nemb_mse)
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2951 SLI_CONFIG(mse), buf[%d]-length:%d\n",
				i, sli_cfg_mbx->un.sli_config_emb0_subsys.
				mse[i].buf_len);
		else
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2952 SLI_CONFIG(hbd), buf[%d]-length:%d\n",
				i, bsg_bf_get(lpfc_mbox_sli_config_ecmn_hbd_len,
				&sli_cfg_mbx->un.sli_config_emb1_subsys.
				hbd[i]));
	}

	/* multi-buffer handling context */
	phba->mbox_ext_buf_ctx.nembType = nemb_tp;
	phba->mbox_ext_buf_ctx.mboxType = mbox_wr;
	phba->mbox_ext_buf_ctx.numBuf = ext_buf_cnt;
	phba->mbox_ext_buf_ctx.mbxTag = mbox_req->extMboxTag;
	phba->mbox_ext_buf_ctx.seqNum = mbox_req->extSeqNum;
	phba->mbox_ext_buf_ctx.mbx_dmabuf = dmabuf;

	if (ext_buf_cnt == 1) {
		/* bsg tracking structure */
		dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
		if (!dd_data) {
			rc = -ENOMEM;
			goto job_error;
		}

		/* mailbox command structure for base driver */
		pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!pmboxq) {
			rc = -ENOMEM;
			goto job_error;
		}
		memset(pmboxq, 0, sizeof(LPFC_MBOXQ_t));
		pmb = &pmboxq->u.mb;
		mbx = (uint8_t *)dmabuf->virt;
		memcpy(pmb, mbx, sizeof(*pmb));
		pmb->mbxOwner = OWN_HOST;
		pmboxq->vport = phba->pport;

		/* callback for multi-buffer read mailbox command */
		pmboxq->mbox_cmpl = lpfc_bsg_issue_write_mbox_ext_cmpl;

		/* context fields to callback function */
		pmboxq->context1 = dd_data;
		dd_data->type = TYPE_MBOX;
		dd_data->set_job = job;
		dd_data->context_un.mbox.pmboxq = pmboxq;
		dd_data->context_un.mbox.mb = (MAILBOX_t *)mbx;
		job->dd_data = dd_data;

		/* state change */

		phba->mbox_ext_buf_ctx.state = LPFC_BSG_MBOX_PORT;
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_NOWAIT);
		if ((rc == MBX_SUCCESS) || (rc == MBX_BUSY)) {
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"2955 Issued SLI_CONFIG ext-buffer "
					"maibox command, rc:x%x\n", rc);
			return SLI_CONFIG_HANDLED;
		}
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"2956 Failed to issue SLI_CONFIG ext-buffer "
				"maibox command, rc:x%x\n", rc);
		rc = -EPIPE;
		goto job_error;
	}

	/* wait for additoinal external buffers */

	job->reply->result = 0;
	job->job_done(job);
	return SLI_CONFIG_HANDLED;

job_error:
	if (pmboxq)
		mempool_free(pmboxq, phba->mbox_mem_pool);
	kfree(dd_data);

	return rc;
}

/**
 * lpfc_bsg_handle_sli_cfg_mbox - handle sli-cfg mailbox cmd with ext buffer
 * @phba: Pointer to HBA context object.
 * @mb: Pointer to a BSG mailbox object.
 * @dmabuff: Pointer to a DMA buffer descriptor.
 *
 * This routine handles SLI_CONFIG (0x9B) mailbox command with non-embedded
 * external bufffers, including both 0x9B with non-embedded MSEs and 0x9B
 * with embedded sussystem 0x1 and opcodes with external HBDs.
 **/
static int
lpfc_bsg_handle_sli_cfg_mbox(struct lpfc_hba *phba, struct fc_bsg_job *job,
			     struct lpfc_dmabuf *dmabuf)
{
	struct lpfc_sli_config_mbox *sli_cfg_mbx;
	uint32_t subsys;
	uint32_t opcode;
	int rc = SLI_CONFIG_NOT_HANDLED;

	/* state change on new multi-buffer pass-through mailbox command */
	phba->mbox_ext_buf_ctx.state = LPFC_BSG_MBOX_HOST;

	sli_cfg_mbx = (struct lpfc_sli_config_mbox *)dmabuf->virt;

	if (!bsg_bf_get(lpfc_mbox_hdr_emb,
	    &sli_cfg_mbx->un.sli_config_emb0_subsys.sli_config_hdr)) {
		subsys = bsg_bf_get(lpfc_emb0_subcmnd_subsys,
				    &sli_cfg_mbx->un.sli_config_emb0_subsys);
		opcode = bsg_bf_get(lpfc_emb0_subcmnd_opcode,
				    &sli_cfg_mbx->un.sli_config_emb0_subsys);
		if (subsys == SLI_CONFIG_SUBSYS_FCOE) {
			switch (opcode) {
			case FCOE_OPCODE_READ_FCF:
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
						"2957 Handled SLI_CONFIG "
						"subsys_fcoe, opcode:x%x\n",
						opcode);
				rc = lpfc_bsg_sli_cfg_read_cmd_ext(phba, job,
							nemb_mse, dmabuf);
				break;
			case FCOE_OPCODE_ADD_FCF:
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
						"2958 Handled SLI_CONFIG "
						"subsys_fcoe, opcode:x%x\n",
						opcode);
				rc = lpfc_bsg_sli_cfg_write_cmd_ext(phba, job,
							nemb_mse, dmabuf);
				break;
			default:
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
						"2959 Reject SLI_CONFIG "
						"subsys_fcoe, opcode:x%x\n",
						opcode);
				rc = -EPERM;
				break;
			}
		} else if (subsys == SLI_CONFIG_SUBSYS_COMN) {
			switch (opcode) {
			case COMN_OPCODE_GET_CNTL_ADDL_ATTRIBUTES:
			case COMN_OPCODE_GET_CNTL_ATTRIBUTES:
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
						"3106 Handled SLI_CONFIG "
						"subsys_comn, opcode:x%x\n",
						opcode);
				rc = lpfc_bsg_sli_cfg_read_cmd_ext(phba, job,
							nemb_mse, dmabuf);
				break;
			default:
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
						"3107 Reject SLI_CONFIG "
						"subsys_comn, opcode:x%x\n",
						opcode);
				rc = -EPERM;
				break;
			}
		} else {
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"2977 Reject SLI_CONFIG "
					"subsys:x%d, opcode:x%x\n",
					subsys, opcode);
			rc = -EPERM;
		}
	} else {
		subsys = bsg_bf_get(lpfc_emb1_subcmnd_subsys,
				    &sli_cfg_mbx->un.sli_config_emb1_subsys);
		opcode = bsg_bf_get(lpfc_emb1_subcmnd_opcode,
				    &sli_cfg_mbx->un.sli_config_emb1_subsys);
		if (subsys == SLI_CONFIG_SUBSYS_COMN) {
			switch (opcode) {
			case COMN_OPCODE_READ_OBJECT:
			case COMN_OPCODE_READ_OBJECT_LIST:
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
						"2960 Handled SLI_CONFIG "
						"subsys_comn, opcode:x%x\n",
						opcode);
				rc = lpfc_bsg_sli_cfg_read_cmd_ext(phba, job,
							nemb_hbd, dmabuf);
				break;
			case COMN_OPCODE_WRITE_OBJECT:
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
						"2961 Handled SLI_CONFIG "
						"subsys_comn, opcode:x%x\n",
						opcode);
				rc = lpfc_bsg_sli_cfg_write_cmd_ext(phba, job,
							nemb_hbd, dmabuf);
				break;
			default:
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
						"2962 Not handled SLI_CONFIG "
						"subsys_comn, opcode:x%x\n",
						opcode);
				rc = SLI_CONFIG_NOT_HANDLED;
				break;
			}
		} else {
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"2978 Not handled SLI_CONFIG "
					"subsys:x%d, opcode:x%x\n",
					subsys, opcode);
			rc = SLI_CONFIG_NOT_HANDLED;
		}
	}

	/* state reset on not handled new multi-buffer mailbox command */
	if (rc != SLI_CONFIG_HANDLED)
		phba->mbox_ext_buf_ctx.state = LPFC_BSG_MBOX_IDLE;

	return rc;
}

/**
 * lpfc_bsg_mbox_ext_abort_req - request to abort mbox command with ext buffers
 * @phba: Pointer to HBA context object.
 *
 * This routine is for requesting to abort a pass-through mailbox command with
 * multiple external buffers due to error condition.
 **/
static void
lpfc_bsg_mbox_ext_abort(struct lpfc_hba *phba)
{
	if (phba->mbox_ext_buf_ctx.state == LPFC_BSG_MBOX_PORT)
		phba->mbox_ext_buf_ctx.state = LPFC_BSG_MBOX_ABTS;
	else
		lpfc_bsg_mbox_ext_session_reset(phba);
	return;
}

/**
 * lpfc_bsg_read_ebuf_get - get the next mailbox read external buffer
 * @phba: Pointer to HBA context object.
 * @dmabuf: Pointer to a DMA buffer descriptor.
 *
 * This routine extracts the next mailbox read external buffer back to
 * user space through BSG.
 **/
static int
lpfc_bsg_read_ebuf_get(struct lpfc_hba *phba, struct fc_bsg_job *job)
{
	struct lpfc_sli_config_mbox *sli_cfg_mbx;
	struct lpfc_dmabuf *dmabuf;
	uint8_t *pbuf;
	uint32_t size;
	uint32_t index;

	index = phba->mbox_ext_buf_ctx.seqNum;
	phba->mbox_ext_buf_ctx.seqNum++;

	sli_cfg_mbx = (struct lpfc_sli_config_mbox *)
			phba->mbox_ext_buf_ctx.mbx_dmabuf->virt;

	if (phba->mbox_ext_buf_ctx.nembType == nemb_mse) {
		size = bsg_bf_get(lpfc_mbox_sli_config_mse_len,
			&sli_cfg_mbx->un.sli_config_emb0_subsys.mse[index]);
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2963 SLI_CONFIG (mse) ext-buffer rd get "
				"buffer[%d], size:%d\n", index, size);
	} else {
		size = bsg_bf_get(lpfc_mbox_sli_config_ecmn_hbd_len,
			&sli_cfg_mbx->un.sli_config_emb1_subsys.hbd[index]);
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2964 SLI_CONFIG (hbd) ext-buffer rd get "
				"buffer[%d], size:%d\n", index, size);
	}
	if (list_empty(&phba->mbox_ext_buf_ctx.ext_dmabuf_list))
		return -EPIPE;
	dmabuf = list_first_entry(&phba->mbox_ext_buf_ctx.ext_dmabuf_list,
				  struct lpfc_dmabuf, list);
	list_del_init(&dmabuf->list);

	/* after dma buffer descriptor setup */
	lpfc_idiag_mbxacc_dump_bsg_mbox(phba, phba->mbox_ext_buf_ctx.nembType,
					mbox_rd, dma_ebuf, sta_pos_addr,
					dmabuf, index);

	pbuf = (uint8_t *)dmabuf->virt;
	job->reply->reply_payload_rcv_len =
		sg_copy_from_buffer(job->reply_payload.sg_list,
				    job->reply_payload.sg_cnt,
				    pbuf, size);

	lpfc_bsg_dma_page_free(phba, dmabuf);

	if (phba->mbox_ext_buf_ctx.seqNum == phba->mbox_ext_buf_ctx.numBuf) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2965 SLI_CONFIG (hbd) ext-buffer rd mbox "
				"command session done\n");
		lpfc_bsg_mbox_ext_session_reset(phba);
	}

	job->reply->result = 0;
	job->job_done(job);

	return SLI_CONFIG_HANDLED;
}

/**
 * lpfc_bsg_write_ebuf_set - set the next mailbox write external buffer
 * @phba: Pointer to HBA context object.
 * @dmabuf: Pointer to a DMA buffer descriptor.
 *
 * This routine sets up the next mailbox read external buffer obtained
 * from user space through BSG.
 **/
static int
lpfc_bsg_write_ebuf_set(struct lpfc_hba *phba, struct fc_bsg_job *job,
			struct lpfc_dmabuf *dmabuf)
{
	struct lpfc_sli_config_mbox *sli_cfg_mbx;
	struct bsg_job_data *dd_data = NULL;
	LPFC_MBOXQ_t *pmboxq = NULL;
	MAILBOX_t *pmb;
	enum nemb_type nemb_tp;
	uint8_t *pbuf;
	uint32_t size;
	uint32_t index;
	int rc;

	index = phba->mbox_ext_buf_ctx.seqNum;
	phba->mbox_ext_buf_ctx.seqNum++;
	nemb_tp = phba->mbox_ext_buf_ctx.nembType;

	sli_cfg_mbx = (struct lpfc_sli_config_mbox *)
			phba->mbox_ext_buf_ctx.mbx_dmabuf->virt;

	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (!dd_data) {
		rc = -ENOMEM;
		goto job_error;
	}

	pbuf = (uint8_t *)dmabuf->virt;
	size = job->request_payload.payload_len;
	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt,
			  pbuf, size);

	if (phba->mbox_ext_buf_ctx.nembType == nemb_mse) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2966 SLI_CONFIG (mse) ext-buffer wr set "
				"buffer[%d], size:%d\n",
				phba->mbox_ext_buf_ctx.seqNum, size);

	} else {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2967 SLI_CONFIG (hbd) ext-buffer wr set "
				"buffer[%d], size:%d\n",
				phba->mbox_ext_buf_ctx.seqNum, size);

	}

	/* set up external buffer descriptor and add to external buffer list */
	lpfc_bsg_sli_cfg_dma_desc_setup(phba, nemb_tp, index,
					phba->mbox_ext_buf_ctx.mbx_dmabuf,
					dmabuf);
	list_add_tail(&dmabuf->list, &phba->mbox_ext_buf_ctx.ext_dmabuf_list);

	/* after write dma buffer */
	lpfc_idiag_mbxacc_dump_bsg_mbox(phba, phba->mbox_ext_buf_ctx.nembType,
					mbox_wr, dma_ebuf, sta_pos_addr,
					dmabuf, index);

	if (phba->mbox_ext_buf_ctx.seqNum == phba->mbox_ext_buf_ctx.numBuf) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2968 SLI_CONFIG ext-buffer wr all %d "
				"ebuffers received\n",
				phba->mbox_ext_buf_ctx.numBuf);
		/* mailbox command structure for base driver */
		pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!pmboxq) {
			rc = -ENOMEM;
			goto job_error;
		}
		memset(pmboxq, 0, sizeof(LPFC_MBOXQ_t));
		pbuf = (uint8_t *)phba->mbox_ext_buf_ctx.mbx_dmabuf->virt;
		pmb = &pmboxq->u.mb;
		memcpy(pmb, pbuf, sizeof(*pmb));
		pmb->mbxOwner = OWN_HOST;
		pmboxq->vport = phba->pport;

		/* callback for multi-buffer write mailbox command */
		pmboxq->mbox_cmpl = lpfc_bsg_issue_write_mbox_ext_cmpl;

		/* context fields to callback function */
		pmboxq->context1 = dd_data;
		dd_data->type = TYPE_MBOX;
		dd_data->set_job = job;
		dd_data->context_un.mbox.pmboxq = pmboxq;
		dd_data->context_un.mbox.mb = (MAILBOX_t *)pbuf;
		job->dd_data = dd_data;

		/* state change */
		phba->mbox_ext_buf_ctx.state = LPFC_BSG_MBOX_PORT;

		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_NOWAIT);
		if ((rc == MBX_SUCCESS) || (rc == MBX_BUSY)) {
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"2969 Issued SLI_CONFIG ext-buffer "
					"maibox command, rc:x%x\n", rc);
			return SLI_CONFIG_HANDLED;
		}
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"2970 Failed to issue SLI_CONFIG ext-buffer "
				"maibox command, rc:x%x\n", rc);
		rc = -EPIPE;
		goto job_error;
	}

	/* wait for additoinal external buffers */
	job->reply->result = 0;
	job->job_done(job);
	return SLI_CONFIG_HANDLED;

job_error:
	lpfc_bsg_dma_page_free(phba, dmabuf);
	kfree(dd_data);

	return rc;
}

/**
 * lpfc_bsg_handle_sli_cfg_ebuf - handle ext buffer with sli-cfg mailbox cmd
 * @phba: Pointer to HBA context object.
 * @mb: Pointer to a BSG mailbox object.
 * @dmabuff: Pointer to a DMA buffer descriptor.
 *
 * This routine handles the external buffer with SLI_CONFIG (0x9B) mailbox
 * command with multiple non-embedded external buffers.
 **/
static int
lpfc_bsg_handle_sli_cfg_ebuf(struct lpfc_hba *phba, struct fc_bsg_job *job,
			     struct lpfc_dmabuf *dmabuf)
{
	int rc;

	lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"2971 SLI_CONFIG buffer (type:x%x)\n",
			phba->mbox_ext_buf_ctx.mboxType);

	if (phba->mbox_ext_buf_ctx.mboxType == mbox_rd) {
		if (phba->mbox_ext_buf_ctx.state != LPFC_BSG_MBOX_DONE) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
					"2972 SLI_CONFIG rd buffer state "
					"mismatch:x%x\n",
					phba->mbox_ext_buf_ctx.state);
			lpfc_bsg_mbox_ext_abort(phba);
			return -EPIPE;
		}
		rc = lpfc_bsg_read_ebuf_get(phba, job);
		if (rc == SLI_CONFIG_HANDLED)
			lpfc_bsg_dma_page_free(phba, dmabuf);
	} else { /* phba->mbox_ext_buf_ctx.mboxType == mbox_wr */
		if (phba->mbox_ext_buf_ctx.state != LPFC_BSG_MBOX_HOST) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
					"2973 SLI_CONFIG wr buffer state "
					"mismatch:x%x\n",
					phba->mbox_ext_buf_ctx.state);
			lpfc_bsg_mbox_ext_abort(phba);
			return -EPIPE;
		}
		rc = lpfc_bsg_write_ebuf_set(phba, job, dmabuf);
	}
	return rc;
}

/**
 * lpfc_bsg_handle_sli_cfg_ext - handle sli-cfg mailbox with external buffer
 * @phba: Pointer to HBA context object.
 * @mb: Pointer to a BSG mailbox object.
 * @dmabuff: Pointer to a DMA buffer descriptor.
 *
 * This routine checkes and handles non-embedded multi-buffer SLI_CONFIG
 * (0x9B) mailbox commands and external buffers.
 **/
static int
lpfc_bsg_handle_sli_cfg_ext(struct lpfc_hba *phba, struct fc_bsg_job *job,
			    struct lpfc_dmabuf *dmabuf)
{
	struct dfc_mbox_req *mbox_req;
	int rc = SLI_CONFIG_NOT_HANDLED;

	mbox_req =
	   (struct dfc_mbox_req *)job->request->rqst_data.h_vendor.vendor_cmd;

	/* mbox command with/without single external buffer */
	if (mbox_req->extMboxTag == 0 && mbox_req->extSeqNum == 0)
		return rc;

	/* mbox command and first external buffer */
	if (phba->mbox_ext_buf_ctx.state == LPFC_BSG_MBOX_IDLE) {
		if (mbox_req->extSeqNum == 1) {
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"2974 SLI_CONFIG mailbox: tag:%d, "
					"seq:%d\n", mbox_req->extMboxTag,
					mbox_req->extSeqNum);
			rc = lpfc_bsg_handle_sli_cfg_mbox(phba, job, dmabuf);
			return rc;
		} else
			goto sli_cfg_ext_error;
	}

	/*
	 * handle additional external buffers
	 */

	/* check broken pipe conditions */
	if (mbox_req->extMboxTag != phba->mbox_ext_buf_ctx.mbxTag)
		goto sli_cfg_ext_error;
	if (mbox_req->extSeqNum > phba->mbox_ext_buf_ctx.numBuf)
		goto sli_cfg_ext_error;
	if (mbox_req->extSeqNum != phba->mbox_ext_buf_ctx.seqNum + 1)
		goto sli_cfg_ext_error;

	lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"2975 SLI_CONFIG mailbox external buffer: "
			"extSta:x%x, tag:%d, seq:%d\n",
			phba->mbox_ext_buf_ctx.state, mbox_req->extMboxTag,
			mbox_req->extSeqNum);
	rc = lpfc_bsg_handle_sli_cfg_ebuf(phba, job, dmabuf);
	return rc;

sli_cfg_ext_error:
	/* all other cases, broken pipe */
	lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
			"2976 SLI_CONFIG mailbox broken pipe: "
			"ctxSta:x%x, ctxNumBuf:%d "
			"ctxTag:%d, ctxSeq:%d, tag:%d, seq:%d\n",
			phba->mbox_ext_buf_ctx.state,
			phba->mbox_ext_buf_ctx.numBuf,
			phba->mbox_ext_buf_ctx.mbxTag,
			phba->mbox_ext_buf_ctx.seqNum,
			mbox_req->extMboxTag, mbox_req->extSeqNum);

	lpfc_bsg_mbox_ext_session_reset(phba);

	return -EPIPE;
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
	uint8_t *pmbx = NULL;
	struct bsg_job_data *dd_data = NULL; /* bsg data tracking structure */
	struct lpfc_dmabuf *dmabuf = NULL;
	struct dfc_mbox_req *mbox_req;
	struct READ_EVENT_LOG_VAR *rdEventLog;
	uint32_t transmit_length, receive_length, mode;
	struct lpfc_mbx_sli4_config *sli4_config;
	struct lpfc_mbx_nembed_cmd *nembed_sge;
	struct mbox_header *header;
	struct ulp_bde64 *bde;
	uint8_t *ext = NULL;
	int rc = 0;
	uint8_t *from;
	uint32_t size;

	/* in case no data is transferred */
	job->reply->reply_payload_rcv_len = 0;

	/* sanity check to protect driver */
	if (job->reply_payload.payload_len > BSG_MBOX_SIZE ||
	    job->request_payload.payload_len > BSG_MBOX_SIZE) {
		rc = -ERANGE;
		goto job_done;
	}

	/*
	 * Don't allow mailbox commands to be sent when blocked or when in
	 * the middle of discovery
	 */
	 if (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO) {
		rc = -EAGAIN;
		goto job_done;
	}

	mbox_req =
	    (struct dfc_mbox_req *)job->request->rqst_data.h_vendor.vendor_cmd;

	/* check if requested extended data lengths are valid */
	if ((mbox_req->inExtWLen > BSG_MBOX_SIZE/sizeof(uint32_t)) ||
	    (mbox_req->outExtWLen > BSG_MBOX_SIZE/sizeof(uint32_t))) {
		rc = -ERANGE;
		goto job_done;
	}

	dmabuf = lpfc_bsg_dma_page_alloc(phba);
	if (!dmabuf || !dmabuf->virt) {
		rc = -ENOMEM;
		goto job_done;
	}

	/* Get the mailbox command or external buffer from BSG */
	pmbx = (uint8_t *)dmabuf->virt;
	size = job->request_payload.payload_len;
	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt, pmbx, size);

	/* Handle possible SLI_CONFIG with non-embedded payloads */
	if (phba->sli_rev == LPFC_SLI_REV4) {
		rc = lpfc_bsg_handle_sli_cfg_ext(phba, job, dmabuf);
		if (rc == SLI_CONFIG_HANDLED)
			goto job_cont;
		if (rc)
			goto job_done;
		/* SLI_CONFIG_NOT_HANDLED for other mailbox commands */
	}

	rc = lpfc_bsg_check_cmd_access(phba, (MAILBOX_t *)pmbx, vport);
	if (rc != 0)
		goto job_done; /* must be negative */

	/* allocate our bsg tracking structure */
	dd_data = kmalloc(sizeof(struct bsg_job_data), GFP_KERNEL);
	if (!dd_data) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"2727 Failed allocation of dd_data\n");
		rc = -ENOMEM;
		goto job_done;
	}

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq) {
		rc = -ENOMEM;
		goto job_done;
	}
	memset(pmboxq, 0, sizeof(LPFC_MBOXQ_t));

	pmb = &pmboxq->u.mb;
	memcpy(pmb, pmbx, sizeof(*pmb));
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

	/* extended mailbox commands will need an extended buffer */
	if (mbox_req->inExtWLen || mbox_req->outExtWLen) {
		from = pmbx;
		ext = from + sizeof(MAILBOX_t);
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
		transmit_length = pmb->un.varWords[1];
		receive_length = pmb->un.varWords[4];
		/* transmit length cannot be greater than receive length or
		 * mailbox extension size
		 */
		if ((transmit_length > receive_length) ||
			(transmit_length > BSG_MBOX_SIZE - sizeof(MAILBOX_t))) {
			rc = -ERANGE;
			goto job_done;
		}
		pmb->un.varBIUdiag.un.s2.xmit_bde64.addrHigh =
			putPaddrHigh(dmabuf->phys + sizeof(MAILBOX_t));
		pmb->un.varBIUdiag.un.s2.xmit_bde64.addrLow =
			putPaddrLow(dmabuf->phys + sizeof(MAILBOX_t));

		pmb->un.varBIUdiag.un.s2.rcv_bde64.addrHigh =
			putPaddrHigh(dmabuf->phys + sizeof(MAILBOX_t)
			  + pmb->un.varBIUdiag.un.s2.xmit_bde64.tus.f.bdeSize);
		pmb->un.varBIUdiag.un.s2.rcv_bde64.addrLow =
			putPaddrLow(dmabuf->phys + sizeof(MAILBOX_t)
			  + pmb->un.varBIUdiag.un.s2.xmit_bde64.tus.f.bdeSize);
	} else if (pmb->mbxCommand == MBX_READ_EVENT_LOG) {
		rdEventLog = &pmb->un.varRdEventLog;
		receive_length = rdEventLog->rcv_bde64.tus.f.bdeSize;
		mode = bf_get(lpfc_event_log, rdEventLog);

		/* receive length cannot be greater than mailbox
		 * extension size
		 */
		if (receive_length > BSG_MBOX_SIZE - sizeof(MAILBOX_t)) {
			rc = -ERANGE;
			goto job_done;
		}

		/* mode zero uses a bde like biu diags command */
		if (mode == 0) {
			pmb->un.varWords[3] = putPaddrLow(dmabuf->phys
							+ sizeof(MAILBOX_t));
			pmb->un.varWords[4] = putPaddrHigh(dmabuf->phys
							+ sizeof(MAILBOX_t));
		}
	} else if (phba->sli_rev == LPFC_SLI_REV4) {
		/* Let type 4 (well known data) through because the data is
		 * returned in varwords[4-8]
		 * otherwise check the recieve length and fetch the buffer addr
		 */
		if ((pmb->mbxCommand == MBX_DUMP_MEMORY) &&
			(pmb->un.varDmp.type != DMP_WELL_KNOWN)) {
			/* rebuild the command for sli4 using our own buffers
			* like we do for biu diags
			*/
			receive_length = pmb->un.varWords[2];
			/* receive length cannot be greater than mailbox
			 * extension size
			 */
			if (receive_length == 0) {
				rc = -ERANGE;
				goto job_done;
			}
			pmb->un.varWords[3] = putPaddrLow(dmabuf->phys
						+ sizeof(MAILBOX_t));
			pmb->un.varWords[4] = putPaddrHigh(dmabuf->phys
						+ sizeof(MAILBOX_t));
		} else if ((pmb->mbxCommand == MBX_UPDATE_CFG) &&
			pmb->un.varUpdateCfg.co) {
			bde = (struct ulp_bde64 *)&pmb->un.varWords[4];

			/* bde size cannot be greater than mailbox ext size */
			if (bde->tus.f.bdeSize >
			    BSG_MBOX_SIZE - sizeof(MAILBOX_t)) {
				rc = -ERANGE;
				goto job_done;
			}
			bde->addrHigh = putPaddrHigh(dmabuf->phys
						+ sizeof(MAILBOX_t));
			bde->addrLow = putPaddrLow(dmabuf->phys
						+ sizeof(MAILBOX_t));
		} else if (pmb->mbxCommand == MBX_SLI4_CONFIG) {
			/* Handling non-embedded SLI_CONFIG mailbox command */
			sli4_config = &pmboxq->u.mqe.un.sli4_config;
			if (!bf_get(lpfc_mbox_hdr_emb,
			    &sli4_config->header.cfg_mhdr)) {
				/* rebuild the command for sli4 using our
				 * own buffers like we do for biu diags
				 */
				header = (struct mbox_header *)
						&pmb->un.varWords[0];
				nembed_sge = (struct lpfc_mbx_nembed_cmd *)
						&pmb->un.varWords[0];
				receive_length = nembed_sge->sge[0].length;

				/* receive length cannot be greater than
				 * mailbox extension size
				 */
				if ((receive_length == 0) ||
				    (receive_length >
				     BSG_MBOX_SIZE - sizeof(MAILBOX_t))) {
					rc = -ERANGE;
					goto job_done;
				}

				nembed_sge->sge[0].pa_hi =
						putPaddrHigh(dmabuf->phys
						   + sizeof(MAILBOX_t));
				nembed_sge->sge[0].pa_lo =
						putPaddrLow(dmabuf->phys
						   + sizeof(MAILBOX_t));
			}
		}
	}

	dd_data->context_un.mbox.dmabuffers = dmabuf;

	/* setup wake call as IOCB callback */
	pmboxq->mbox_cmpl = lpfc_bsg_issue_mbox_cmpl;

	/* setup context field to pass wait_queue pointer to wake function */
	pmboxq->context1 = dd_data;
	dd_data->type = TYPE_MBOX;
	dd_data->set_job = job;
	dd_data->context_un.mbox.pmboxq = pmboxq;
	dd_data->context_un.mbox.mb = (MAILBOX_t *)pmbx;
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
		memcpy(pmbx, pmb, sizeof(*pmb));
		job->reply->reply_payload_rcv_len =
			sg_copy_from_buffer(job->reply_payload.sg_list,
					    job->reply_payload.sg_cnt,
					    pmbx, size);
		/* not waiting mbox already done */
		rc = 0;
		goto job_done;
	}

	rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_NOWAIT);
	if ((rc == MBX_SUCCESS) || (rc == MBX_BUSY))
		return 1; /* job started */

job_done:
	/* common exit for error or job completed inline */
	if (pmboxq)
		mempool_free(pmboxq, phba->mbox_mem_pool);
	lpfc_bsg_dma_page_free(phba, dmabuf);
	kfree(dd_data);

job_cont:
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
	struct dfc_mbox_req *mbox_req;
	int rc = 0;

	/* mix-and-match backward compatibility */
	job->reply->reply_payload_rcv_len = 0;
	if (job->request_len <
	    sizeof(struct fc_bsg_request) + sizeof(struct dfc_mbox_req)) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"2737 Mix-and-match backward compatibility "
				"between MBOX_REQ old size:%d and "
				"new request size:%d\n",
				(int)(job->request_len -
				      sizeof(struct fc_bsg_request)),
				(int)sizeof(struct dfc_mbox_req));
		mbox_req = (struct dfc_mbox_req *)
				job->request->rqst_data.h_vendor.vendor_cmd;
		mbox_req->extMboxTag = 0;
		mbox_req->extSeqNum = 0;
	}

	rc = lpfc_bsg_issue_mbox(phba, job, vport);

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
	struct lpfc_dmabuf *bmp, *cmp, *rmp;
	struct lpfc_bsg_menlo *menlo;
	unsigned long flags;
	struct menlo_response *menlo_resp;
	unsigned int rsp_size;
	int rc = 0;

	dd_data = cmdiocbq->context1;
	cmp = cmdiocbq->context2;
	bmp = cmdiocbq->context3;
	menlo = &dd_data->context_un.menlo;
	rmp = menlo->rmp;
	rsp = &rspiocbq->iocb;

	/* Determine if job has been aborted */
	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	job = dd_data->set_job;
	if (job) {
		/* Prevent timeout handling from trying to abort job  */
		job->dd_data = NULL;
	}
	spin_unlock_irqrestore(&phba->ct_ev_lock, flags);

	/* Copy the job data or set the failing status for the job */

	if (job) {
		/* always return the xri, this would be used in the case
		 * of a menlo download to allow the data to be sent as a
		 * continuation of the exchange.
		 */

		menlo_resp = (struct menlo_response *)
			job->reply->reply_data.vendor_reply.vendor_rsp;
		menlo_resp->xri = rsp->ulpContext;
		if (rsp->ulpStatus) {
			if (rsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
				switch (rsp->un.ulpWord[4] & IOERR_PARAM_MASK) {
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
			} else {
				rc = -EACCES;
			}
		} else {
			rsp_size = rsp->un.genreq64.bdl.bdeSize;
			job->reply->reply_payload_rcv_len =
				lpfc_bsg_copy_data(rmp, &job->reply_payload,
						   rsp_size, 0);
		}

	}

	lpfc_sli_release_iocbq(phba, cmdiocbq);
	lpfc_free_bsg_buffers(phba, cmp);
	lpfc_free_bsg_buffers(phba, rmp);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(bmp);
	kfree(dd_data);

	/* Complete the job if active */

	if (job) {
		job->reply->result = rc;
		job->job_done(job);
	}

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
	struct lpfc_iocbq *cmdiocbq;
	IOCB_t *cmd;
	int rc = 0;
	struct menlo_command *menlo_cmd;
	struct menlo_response *menlo_resp;
	struct lpfc_dmabuf *bmp = NULL, *cmp = NULL, *rmp = NULL;
	int request_nseg;
	int reply_nseg;
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

	bmp->virt = lpfc_mbuf_alloc(phba, 0, &bmp->phys);
	if (!bmp->virt) {
		rc = -ENOMEM;
		goto free_bmp;
	}

	INIT_LIST_HEAD(&bmp->list);

	bpl = (struct ulp_bde64 *)bmp->virt;
	request_nseg = LPFC_BPL_SIZE/sizeof(struct ulp_bde64);
	cmp = lpfc_alloc_bsg_buffers(phba, job->request_payload.payload_len,
				     1, bpl, &request_nseg);
	if (!cmp) {
		rc = -ENOMEM;
		goto free_bmp;
	}
	lpfc_bsg_copy_data(cmp, &job->request_payload,
			   job->request_payload.payload_len, 1);

	bpl += request_nseg;
	reply_nseg = LPFC_BPL_SIZE/sizeof(struct ulp_bde64) - request_nseg;
	rmp = lpfc_alloc_bsg_buffers(phba, job->reply_payload.payload_len, 0,
				     bpl, &reply_nseg);
	if (!rmp) {
		rc = -ENOMEM;
		goto free_cmp;
	}

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	if (!cmdiocbq) {
		rc = -ENOMEM;
		goto free_rmp;
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
	cmdiocbq->iocb_cmpl = lpfc_bsg_menlo_cmd_cmp;
	cmdiocbq->context1 = dd_data;
	cmdiocbq->context2 = cmp;
	cmdiocbq->context3 = bmp;
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
	dd_data->set_job = job;
	dd_data->context_un.menlo.cmdiocbq = cmdiocbq;
	dd_data->context_un.menlo.rmp = rmp;
	job->dd_data = dd_data;

	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, cmdiocbq,
		MENLO_TIMEOUT - 5);
	if (rc == IOCB_SUCCESS)
		return 0; /* done for now */

	lpfc_sli_release_iocbq(phba, cmdiocbq);

free_rmp:
	lpfc_free_bsg_buffers(phba, rmp);
free_cmp:
	lpfc_free_bsg_buffers(phba, cmp);
free_bmp:
	if (bmp->virt)
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
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
		rc = lpfc_bsg_diag_loopback_mode(job);
		break;
	case LPFC_BSG_VENDOR_DIAG_MODE_END:
		rc = lpfc_sli4_bsg_diag_mode_end(job);
		break;
	case LPFC_BSG_VENDOR_DIAG_RUN_LOOPBACK:
		rc = lpfc_bsg_diag_loopback_run(job);
		break;
	case LPFC_BSG_VENDOR_LINK_DIAG_TEST:
		rc = lpfc_sli4_bsg_link_diag_test(job);
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
	struct lpfc_sli_ring *pring = &phba->sli.ring[LPFC_ELS_RING];
	struct bsg_job_data *dd_data;
	unsigned long flags;
	int rc = 0;
	LIST_HEAD(completions);
	struct lpfc_iocbq *check_iocb, *next_iocb;

	/* if job's driver data is NULL, the command completed or is in the
	 * the process of completing.  In this case, return status to request
	 * so the timeout is retried.  This avoids double completion issues
	 * and the request will be pulled off the timer queue when the
	 * command's completion handler executes.  Otherwise, prevent the
	 * command's completion handler from executing the job done callback
	 * and continue processing to abort the outstanding the command.
	 */

	spin_lock_irqsave(&phba->ct_ev_lock, flags);
	dd_data = (struct bsg_job_data *)job->dd_data;
	if (dd_data) {
		dd_data->set_job = NULL;
		job->dd_data = NULL;
	} else {
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		return -EAGAIN;
	}

	switch (dd_data->type) {
	case TYPE_IOCB:
		/* Check to see if IOCB was issued to the port or not. If not,
		 * remove it from the txq queue and call cancel iocbs.
		 * Otherwise, call abort iotag
		 */

		cmdiocb = dd_data->context_un.iocb.cmdiocbq;
		spin_lock_irq(&phba->hbalock);
		list_for_each_entry_safe(check_iocb, next_iocb, &pring->txq,
					 list) {
			if (check_iocb == cmdiocb) {
				list_move_tail(&check_iocb->list, &completions);
				break;
			}
		}
		if (list_empty(&completions))
			lpfc_sli_issue_abort_iotag(phba, pring, cmdiocb);
		spin_unlock_irq(&phba->hbalock);
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		if (!list_empty(&completions)) {
			lpfc_sli_cancel_iocbs(phba, &completions,
					      IOSTAT_LOCAL_REJECT,
					      IOERR_SLI_ABORTED);
		}
		break;

	case TYPE_EVT:
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		break;

	case TYPE_MBOX:
		/* Update the ext buf ctx state if needed */

		if (phba->mbox_ext_buf_ctx.state == LPFC_BSG_MBOX_PORT)
			phba->mbox_ext_buf_ctx.state = LPFC_BSG_MBOX_ABTS;
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		break;
	case TYPE_MENLO:
		/* Check to see if IOCB was issued to the port or not. If not,
		 * remove it from the txq queue and call cancel iocbs.
		 * Otherwise, call abort iotag.
		 */

		cmdiocb = dd_data->context_un.menlo.cmdiocbq;
		spin_lock_irq(&phba->hbalock);
		list_for_each_entry_safe(check_iocb, next_iocb, &pring->txq,
					 list) {
			if (check_iocb == cmdiocb) {
				list_move_tail(&check_iocb->list, &completions);
				break;
			}
		}
		if (list_empty(&completions))
			lpfc_sli_issue_abort_iotag(phba, pring, cmdiocb);
		spin_unlock_irq(&phba->hbalock);
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		if (!list_empty(&completions)) {
			lpfc_sli_cancel_iocbs(phba, &completions,
					      IOSTAT_LOCAL_REJECT,
					      IOERR_SLI_ABORTED);
		}
		break;
	default:
		spin_unlock_irqrestore(&phba->ct_ev_lock, flags);
		break;
	}

	/* scsi transport fc fc_bsg_job_timeout expects a zero return code,
	 * otherwise an error message will be displayed on the console
	 * so always return success (zero)
	 */
	return rc;
}
