/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2005 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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
 *******************************************************************/

#include <linux/pci.h>
#include <linux/interrupt.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_version.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"

#define LPFC_RESET_WAIT  2
#define LPFC_ABORT_WAIT  2


/*
 * This routine allocates a scsi buffer, which contains all the necessary
 * information needed to initiate a SCSI I/O.  The non-DMAable buffer region
 * contains information to build the IOCB.  The DMAable region contains
 * memory for the FCP CMND, FCP RSP, and the inital BPL.  In addition to
 * allocating memeory, the FCP CMND and FCP RSP BDEs are setup in the BPL
 * and the BPL BDE is setup in the IOCB.
 */
static struct lpfc_scsi_buf *
lpfc_get_scsi_buf(struct lpfc_hba * phba)
{
	struct lpfc_scsi_buf *psb;
	struct ulp_bde64 *bpl;
	IOCB_t *iocb;
	dma_addr_t pdma_phys;

	psb = kmalloc(sizeof(struct lpfc_scsi_buf), GFP_KERNEL);
	if (!psb)
		return NULL;
	memset(psb, 0, sizeof (struct lpfc_scsi_buf));
	psb->scsi_hba = phba;

	/*
	 * Get memory from the pci pool to map the virt space to pci bus space
	 * for an I/O.  The DMA buffer includes space for the struct fcp_cmnd,
	 * struct fcp_rsp and the number of bde's necessary to support the
	 * sg_tablesize.
	 */
	psb->data = pci_pool_alloc(phba->lpfc_scsi_dma_buf_pool, GFP_KERNEL,
							&psb->dma_handle);
	if (!psb->data) {
		kfree(psb);
		return NULL;
	}

	/* Initialize virtual ptrs to dma_buf region. */
	memset(psb->data, 0, phba->cfg_sg_dma_buf_size);

	psb->fcp_cmnd = psb->data;
	psb->fcp_rsp = psb->data + sizeof(struct fcp_cmnd);
	psb->fcp_bpl = psb->data + sizeof(struct fcp_cmnd) +
							sizeof(struct fcp_rsp);

	/* Initialize local short-hand pointers. */
	bpl = psb->fcp_bpl;
	pdma_phys = psb->dma_handle;

	/*
	 * The first two bdes are the FCP_CMD and FCP_RSP.  The balance are sg
	 * list bdes.  Initialize the first two and leave the rest for
	 * queuecommand.
	 */
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys));
	bpl->addrLow = le32_to_cpu(putPaddrLow(pdma_phys));
	bpl->tus.f.bdeSize = sizeof (struct fcp_cmnd);
	bpl->tus.f.bdeFlags = BUFF_USE_CMND;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);
	bpl++;

	/* Setup the physical region for the FCP RSP */
	pdma_phys += sizeof (struct fcp_cmnd);
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys));
	bpl->addrLow = le32_to_cpu(putPaddrLow(pdma_phys));
	bpl->tus.f.bdeSize = sizeof (struct fcp_rsp);
	bpl->tus.f.bdeFlags = (BUFF_USE_CMND | BUFF_USE_RCV);
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	/*
	 * Since the IOCB for the FCP I/O is built into this lpfc_scsi_buf,
	 * initialize it with all known data now.
	 */
	pdma_phys += (sizeof (struct fcp_rsp));
	iocb = &psb->cur_iocbq.iocb;
	iocb->un.fcpi64.bdl.ulpIoTag32 = 0;
	iocb->un.fcpi64.bdl.addrHigh = putPaddrHigh(pdma_phys);
	iocb->un.fcpi64.bdl.addrLow = putPaddrLow(pdma_phys);
	iocb->un.fcpi64.bdl.bdeSize = (2 * sizeof (struct ulp_bde64));
	iocb->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDL;
	iocb->ulpBdeCount = 1;
	iocb->ulpClass = CLASS3;

	return psb;
}

static void
lpfc_free_scsi_buf(struct lpfc_scsi_buf * psb)
{
	struct lpfc_hba *phba = psb->scsi_hba;

	/*
	 * There are only two special cases to consider.  (1) the scsi command
	 * requested scatter-gather usage or (2) the scsi command allocated
	 * a request buffer, but did not request use_sg.  There is a third
	 * case, but it does not require resource deallocation.
	 */
	if ((psb->seg_cnt > 0) && (psb->pCmd->use_sg)) {
		dma_unmap_sg(&phba->pcidev->dev, psb->pCmd->request_buffer,
				psb->seg_cnt, psb->pCmd->sc_data_direction);
	} else {
		 if ((psb->nonsg_phys) && (psb->pCmd->request_bufflen)) {
			dma_unmap_single(&phba->pcidev->dev, psb->nonsg_phys,
						psb->pCmd->request_bufflen,
						psb->pCmd->sc_data_direction);
		 }
	}

	list_add_tail(&psb->list, &phba->lpfc_scsi_buf_list);
}

static int
lpfc_scsi_prep_dma_buf(struct lpfc_hba * phba, struct lpfc_scsi_buf * lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct scatterlist *sgel = NULL;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct ulp_bde64 *bpl = lpfc_cmd->fcp_bpl;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	dma_addr_t physaddr;
	uint32_t i, num_bde = 0;
	int datadir = scsi_cmnd->sc_data_direction;
	int dma_error;

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	bpl += 2;
	if (scsi_cmnd->use_sg) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */
		sgel = (struct scatterlist *)scsi_cmnd->request_buffer;
		lpfc_cmd->seg_cnt = dma_map_sg(&phba->pcidev->dev, sgel,
						scsi_cmnd->use_sg, datadir);
		if (lpfc_cmd->seg_cnt == 0)
			return 1;

		if (lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt) {
			printk(KERN_ERR "%s: Too many sg segments from "
			       "dma_map_sg.  Config %d, seg_cnt %d",
			       __FUNCTION__, phba->cfg_sg_seg_cnt,
			       lpfc_cmd->seg_cnt);
			dma_unmap_sg(&phba->pcidev->dev, sgel,
				     lpfc_cmd->seg_cnt, datadir);
			return 1;
		}

		/*
		 * The driver established a maximum scatter-gather segment count
		 * during probe that limits the number of sg elements in any
		 * single scsi command.  Just run through the seg_cnt and format
		 * the bde's.
		 */
		for (i = 0; i < lpfc_cmd->seg_cnt; i++) {
			physaddr = sg_dma_address(sgel);
			bpl->addrLow = le32_to_cpu(putPaddrLow(physaddr));
			bpl->addrHigh = le32_to_cpu(putPaddrHigh(physaddr));
			bpl->tus.f.bdeSize = sg_dma_len(sgel);
			if (datadir == DMA_TO_DEVICE)
				bpl->tus.f.bdeFlags = 0;
			else
				bpl->tus.f.bdeFlags = BUFF_USE_RCV;
			bpl->tus.w = le32_to_cpu(bpl->tus.w);
			bpl++;
			sgel++;
			num_bde++;
		}
	} else if (scsi_cmnd->request_buffer && scsi_cmnd->request_bufflen) {
		physaddr = dma_map_single(&phba->pcidev->dev,
					  scsi_cmnd->request_buffer,
					  scsi_cmnd->request_bufflen,
					  datadir);
		dma_error = dma_mapping_error(physaddr);
		if (dma_error) {
			lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
				"%d:0718 Unable to dma_map_single "
				"request_buffer: x%x\n",
				phba->brd_no, dma_error);
			return 1;
		}

		lpfc_cmd->nonsg_phys = physaddr;
		bpl->addrLow = le32_to_cpu(putPaddrLow(physaddr));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(physaddr));
		bpl->tus.f.bdeSize = scsi_cmnd->request_bufflen;
		if (datadir == DMA_TO_DEVICE)
			bpl->tus.f.bdeFlags = 0;
		else
			bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		num_bde = 1;
		bpl++;
	}

	/*
	 * Finish initializing those IOCB fields that are dependent on the
	 * scsi_cmnd request_buffer.  Note that the bdeSize is explicitly
	 * reinitialized since all iocb memory resources are used many times
	 * for transmit, receive, and continuation bpl's.
	 */
	iocb_cmd->un.fcpi64.bdl.bdeSize = (2 * sizeof (struct ulp_bde64));
	iocb_cmd->un.fcpi64.bdl.bdeSize +=
		(num_bde * sizeof (struct ulp_bde64));
	iocb_cmd->ulpBdeCount = 1;
	iocb_cmd->ulpLe = 1;
	fcp_cmnd->fcpDl = be32_to_cpu(scsi_cmnd->request_bufflen);
	return 0;
}

static void
lpfc_handle_fcp_err(struct lpfc_scsi_buf *lpfc_cmd)
{
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcpcmd = lpfc_cmd->fcp_cmnd;
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	struct lpfc_hba *phba = lpfc_cmd->scsi_hba;
	uint32_t fcpi_parm = lpfc_cmd->cur_iocbq.iocb.un.fcpi.fcpi_parm;
	uint32_t resp_info = fcprsp->rspStatus2;
	uint32_t scsi_status = fcprsp->rspStatus3;
	uint32_t host_status = DID_OK;
	uint32_t rsplen = 0;

	/*
	 *  If this is a task management command, there is no
	 *  scsi packet associated with this lpfc_cmd.  The driver
	 *  consumes it.
	 */
	if (fcpcmd->fcpCntl2) {
		scsi_status = 0;
		goto out;
	}

	lpfc_printf_log(phba, KERN_WARNING, LOG_FCP,
			"%d:0730 FCP command failed: RSP "
			"Data: x%x x%x x%x x%x x%x x%x\n",
			phba->brd_no, resp_info, scsi_status,
			be32_to_cpu(fcprsp->rspResId),
			be32_to_cpu(fcprsp->rspSnsLen),
			be32_to_cpu(fcprsp->rspRspLen),
			fcprsp->rspInfo3);

	if (resp_info & RSP_LEN_VALID) {
		rsplen = be32_to_cpu(fcprsp->rspRspLen);
		if ((rsplen != 0 && rsplen != 4 && rsplen != 8) ||
		    (fcprsp->rspInfo3 != RSP_NO_FAILURE)) {
			host_status = DID_ERROR;
			goto out;
		}
	}

	if ((resp_info & SNS_LEN_VALID) && fcprsp->rspSnsLen) {
		uint32_t snslen = be32_to_cpu(fcprsp->rspSnsLen);
		if (snslen > SCSI_SENSE_BUFFERSIZE)
			snslen = SCSI_SENSE_BUFFERSIZE;

		memcpy(cmnd->sense_buffer, &fcprsp->rspInfo0 + rsplen, snslen);
	}

	cmnd->resid = 0;
	if (resp_info & RESID_UNDER) {
		cmnd->resid = be32_to_cpu(fcprsp->rspResId);

		lpfc_printf_log(phba, KERN_INFO, LOG_FCP,
				"%d:0716 FCP Read Underrun, expected %d, "
				"residual %d Data: x%x x%x x%x\n", phba->brd_no,
				be32_to_cpu(fcpcmd->fcpDl), cmnd->resid,
				fcpi_parm, cmnd->cmnd[0], cmnd->underflow);

		/*
		 * The cmnd->underflow is the minimum number of bytes that must
		 * be transfered for this command.  Provided a sense condition
		 * is not present, make sure the actual amount transferred is at
		 * least the underflow value or fail.
		 */
		if (!(resp_info & SNS_LEN_VALID) &&
		    (scsi_status == SAM_STAT_GOOD) &&
		    (cmnd->request_bufflen - cmnd->resid) < cmnd->underflow) {
			lpfc_printf_log(phba, KERN_INFO, LOG_FCP,
					"%d:0717 FCP command x%x residual "
					"underrun converted to error "
					"Data: x%x x%x x%x\n", phba->brd_no,
					cmnd->cmnd[0], cmnd->request_bufflen,
					cmnd->resid, cmnd->underflow);

			host_status = DID_ERROR;
		}
	} else if (resp_info & RESID_OVER) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP,
				"%d:0720 FCP command x%x residual "
				"overrun error. Data: x%x x%x \n",
				phba->brd_no, cmnd->cmnd[0],
				cmnd->request_bufflen, cmnd->resid);
		host_status = DID_ERROR;

	/*
	 * Check SLI validation that all the transfer was actually done
	 * (fcpi_parm should be zero). Apply check only to reads.
	 */
	} else if ((scsi_status == SAM_STAT_GOOD) && fcpi_parm &&
			(cmnd->sc_data_direction == DMA_FROM_DEVICE)) {
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP,
			"%d:0734 FCP Read Check Error Data: "
			"x%x x%x x%x x%x\n", phba->brd_no,
			be32_to_cpu(fcpcmd->fcpDl),
			be32_to_cpu(fcprsp->rspResId),
			fcpi_parm, cmnd->cmnd[0]);
		host_status = DID_ERROR;
		cmnd->resid = cmnd->request_bufflen;
	}

 out:
	cmnd->result = ScsiResult(host_status, scsi_status);
}

static void
lpfc_scsi_cmd_iocb_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *pIocbIn,
			struct lpfc_iocbq *pIocbOut)
{
	struct lpfc_scsi_buf *lpfc_cmd =
		(struct lpfc_scsi_buf *) pIocbIn->context1;
	struct lpfc_rport_data *rdata = lpfc_cmd->rdata;
	struct lpfc_nodelist *pnode = rdata->pnode;
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	unsigned long iflag;

	lpfc_cmd->result = pIocbOut->iocb.un.ulpWord[4];
	lpfc_cmd->status = pIocbOut->iocb.ulpStatus;

	if (lpfc_cmd->status) {
		if (lpfc_cmd->status == IOSTAT_LOCAL_REJECT &&
		    (lpfc_cmd->result & IOERR_DRVR_MASK))
			lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
		else if (lpfc_cmd->status >= IOSTAT_CNT)
			lpfc_cmd->status = IOSTAT_DEFAULT;

		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP,
				"%d:0729 FCP cmd x%x failed <%d/%d> status: "
				"x%x result: x%x Data: x%x x%x\n",
				phba->brd_no, cmd->cmnd[0], cmd->device->id,
				cmd->device->lun, lpfc_cmd->status,
				lpfc_cmd->result, pIocbOut->iocb.ulpContext,
				lpfc_cmd->cur_iocbq.iocb.ulpIoTag);

		switch (lpfc_cmd->status) {
		case IOSTAT_FCP_RSP_ERROR:
			/* Call FCP RSP handler to determine result */
			lpfc_handle_fcp_err(lpfc_cmd);
			break;
		case IOSTAT_NPORT_BSY:
		case IOSTAT_FABRIC_BSY:
			cmd->result = ScsiResult(DID_BUS_BUSY, 0);
			break;
		default:
			cmd->result = ScsiResult(DID_ERROR, 0);
			break;
		}

		if (pnode) {
			if (pnode->nlp_state != NLP_STE_MAPPED_NODE)
				cmd->result = ScsiResult(DID_BUS_BUSY,
					SAM_STAT_BUSY);
		}
		else {
			cmd->result = ScsiResult(DID_NO_CONNECT, 0);
		}
	} else {
		cmd->result = ScsiResult(DID_OK, 0);
	}

	if (cmd->result || lpfc_cmd->fcp_rsp->rspSnsLen) {
		uint32_t *lp = (uint32_t *)cmd->sense_buffer;

		lpfc_printf_log(phba, KERN_INFO, LOG_FCP,
				"%d:0710 Iodone <%d/%d> cmd %p, error x%x "
				"SNS x%x x%x Data: x%x x%x\n",
				phba->brd_no, cmd->device->id,
				cmd->device->lun, cmd, cmd->result,
				*lp, *(lp + 3), cmd->retries, cmd->resid);
	}

	spin_lock_irqsave(phba->host->host_lock, iflag);
	lpfc_free_scsi_buf(lpfc_cmd);
	cmd->host_scribble = NULL;
	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	cmd->scsi_done(cmd);
}

static void
lpfc_scsi_prep_cmnd(struct lpfc_hba * phba, struct lpfc_scsi_buf * lpfc_cmd,
			struct lpfc_nodelist *pnode)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	struct lpfc_iocbq *piocbq = &(lpfc_cmd->cur_iocbq);
	int datadir = scsi_cmnd->sc_data_direction;

	lpfc_cmd->fcp_rsp->rspSnsLen = 0;
	/* clear task management bits */
	lpfc_cmd->fcp_cmnd->fcpCntl2 = 0;

	int_to_scsilun(lpfc_cmd->pCmd->device->lun,
			&lpfc_cmd->fcp_cmnd->fcp_lun);

	memcpy(&fcp_cmnd->fcpCdb[0], scsi_cmnd->cmnd, 16);

	if (scsi_cmnd->device->tagged_supported) {
		switch (scsi_cmnd->tag) {
		case HEAD_OF_QUEUE_TAG:
			fcp_cmnd->fcpCntl1 = HEAD_OF_Q;
			break;
		case ORDERED_QUEUE_TAG:
			fcp_cmnd->fcpCntl1 = ORDERED_Q;
			break;
		default:
			fcp_cmnd->fcpCntl1 = SIMPLE_Q;
			break;
		}
	} else
		fcp_cmnd->fcpCntl1 = 0;

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	if (scsi_cmnd->use_sg) {
		if (datadir == DMA_TO_DEVICE) {
			iocb_cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
			iocb_cmd->un.fcpi.fcpi_parm = 0;
			iocb_cmd->ulpPU = 0;
			fcp_cmnd->fcpCntl3 = WRITE_DATA;
			phba->fc4OutputRequests++;
		} else {
			iocb_cmd->ulpCommand = CMD_FCP_IREAD64_CR;
			iocb_cmd->ulpPU = PARM_READ_CHECK;
			iocb_cmd->un.fcpi.fcpi_parm =
				scsi_cmnd->request_bufflen;
			fcp_cmnd->fcpCntl3 = READ_DATA;
			phba->fc4InputRequests++;
		}
	} else if (scsi_cmnd->request_buffer && scsi_cmnd->request_bufflen) {
		if (datadir == DMA_TO_DEVICE) {
			iocb_cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
			iocb_cmd->un.fcpi.fcpi_parm = 0;
			iocb_cmd->ulpPU = 0;
			fcp_cmnd->fcpCntl3 = WRITE_DATA;
			phba->fc4OutputRequests++;
		} else {
			iocb_cmd->ulpCommand = CMD_FCP_IREAD64_CR;
			iocb_cmd->ulpPU = PARM_READ_CHECK;
			iocb_cmd->un.fcpi.fcpi_parm =
				scsi_cmnd->request_bufflen;
			fcp_cmnd->fcpCntl3 = READ_DATA;
			phba->fc4InputRequests++;
		}
	} else {
		iocb_cmd->ulpCommand = CMD_FCP_ICMND64_CR;
		iocb_cmd->un.fcpi.fcpi_parm = 0;
		iocb_cmd->ulpPU = 0;
		fcp_cmnd->fcpCntl3 = 0;
		phba->fc4ControlRequests++;
	}

	/*
	 * Finish initializing those IOCB fields that are independent
	 * of the scsi_cmnd request_buffer
	 */
	piocbq->iocb.ulpContext = pnode->nlp_rpi;
	if (pnode->nlp_fcp_info & NLP_FCP_2_DEVICE)
		piocbq->iocb.ulpFCP2Rcvy = 1;

	piocbq->iocb.ulpClass = (pnode->nlp_fcp_info & 0x0f);
	piocbq->context1  = lpfc_cmd;
	piocbq->iocb_cmpl = lpfc_scsi_cmd_iocb_cmpl;
	piocbq->iocb.ulpTimeout = lpfc_cmd->timeout;
}

static int
lpfc_scsi_prep_task_mgmt_cmd(struct lpfc_hba *phba,
			     struct lpfc_scsi_buf *lpfc_cmd,
			     uint8_t task_mgmt_cmd)
{
	struct lpfc_sli *psli;
	struct lpfc_iocbq *piocbq;
	IOCB_t *piocb;
	struct fcp_cmnd *fcp_cmnd;
	struct scsi_device *scsi_dev = lpfc_cmd->pCmd->device;
	struct lpfc_rport_data *rdata = scsi_dev->hostdata;
	struct lpfc_nodelist *ndlp = rdata->pnode;

	if ((ndlp == 0) || (ndlp->nlp_state != NLP_STE_MAPPED_NODE)) {
		return 0;
	}

	psli = &phba->sli;
	piocbq = &(lpfc_cmd->cur_iocbq);
	piocb = &piocbq->iocb;

	fcp_cmnd = lpfc_cmd->fcp_cmnd;
	int_to_scsilun(lpfc_cmd->pCmd->device->lun,
			&lpfc_cmd->fcp_cmnd->fcp_lun);
	fcp_cmnd->fcpCntl2 = task_mgmt_cmd;

	piocb->ulpCommand = CMD_FCP_ICMND64_CR;

	piocb->ulpContext = ndlp->nlp_rpi;
	if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
		piocb->ulpFCP2Rcvy = 1;
	}
	piocb->ulpClass = (ndlp->nlp_fcp_info & 0x0f);

	/* ulpTimeout is only one byte */
	if (lpfc_cmd->timeout > 0xff) {
		/*
		 * Do not timeout the command at the firmware level.
		 * The driver will provide the timeout mechanism.
		 */
		piocb->ulpTimeout = 0;
	} else {
		piocb->ulpTimeout = lpfc_cmd->timeout;
	}

	lpfc_cmd->rdata = rdata;

	switch (task_mgmt_cmd) {
	case FCP_LUN_RESET:
		/* Issue LUN Reset to TGT <num> LUN <num> */
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_FCP,
				"%d:0703 Issue LUN Reset to TGT %d LUN %d "
				"Data: x%x x%x\n",
				phba->brd_no,
				scsi_dev->id, scsi_dev->lun,
				ndlp->nlp_rpi, ndlp->nlp_flag);

		break;
	case FCP_ABORT_TASK_SET:
		/* Issue Abort Task Set to TGT <num> LUN <num> */
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_FCP,
				"%d:0701 Issue Abort Task Set to TGT %d LUN %d "
				"Data: x%x x%x\n",
				phba->brd_no,
				scsi_dev->id, scsi_dev->lun,
				ndlp->nlp_rpi, ndlp->nlp_flag);

		break;
	case FCP_TARGET_RESET:
		/* Issue Target Reset to TGT <num> */
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_FCP,
				"%d:0702 Issue Target Reset to TGT %d "
				"Data: x%x x%x\n",
				phba->brd_no,
				scsi_dev->id, ndlp->nlp_rpi,
				ndlp->nlp_flag);
		break;
	}

	return (1);
}

static int
lpfc_scsi_tgt_reset(struct lpfc_scsi_buf * lpfc_cmd, struct lpfc_hba * phba)
{
	struct lpfc_iocbq *iocbq;
	struct lpfc_iocbq *iocbqrsp = NULL;
	struct list_head *lpfc_iocb_list = &phba->lpfc_iocb_list;
	int ret;

	ret = lpfc_scsi_prep_task_mgmt_cmd(phba, lpfc_cmd, FCP_TARGET_RESET);
	if (!ret)
		return FAILED;

	lpfc_cmd->scsi_hba = phba;
	iocbq = &lpfc_cmd->cur_iocbq;
	list_remove_head(lpfc_iocb_list, iocbqrsp, struct lpfc_iocbq, list);
	if (!iocbqrsp)
		return FAILED;
	memset(iocbqrsp, 0, sizeof (struct lpfc_iocbq));

	iocbq->iocb_flag |= LPFC_IO_POLL;
	ret = lpfc_sli_issue_iocb_wait_high_priority(phba,
		     &phba->sli.ring[phba->sli.fcp_ring],
		     iocbq, SLI_IOCB_HIGH_PRIORITY,
		     iocbqrsp,
		     lpfc_cmd->timeout);
	if (ret != IOCB_SUCCESS) {
		lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
		ret = FAILED;
	} else {
		ret = SUCCESS;
		lpfc_cmd->result = iocbqrsp->iocb.un.ulpWord[4];
		lpfc_cmd->status = iocbqrsp->iocb.ulpStatus;
		if (lpfc_cmd->status == IOSTAT_LOCAL_REJECT &&
			(lpfc_cmd->result & IOERR_DRVR_MASK))
				lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
	}

	/*
	 * All outstanding txcmplq I/Os should have been aborted by the target.
	 * Unfortunately, some targets do not abide by this forcing the driver
	 * to double check.
	 */
	lpfc_sli_abort_iocb(phba, &phba->sli.ring[phba->sli.fcp_ring],
			    lpfc_cmd->pCmd->device->id,
			    lpfc_cmd->pCmd->device->lun, 0, LPFC_CTX_TGT);

	/* Return response IOCB to free list. */
	list_add_tail(&iocbqrsp->list, lpfc_iocb_list);
	return ret;
}

static void
lpfc_scsi_cmd_iocb_cleanup (struct lpfc_hba *phba, struct lpfc_iocbq *pIocbIn,
			    struct lpfc_iocbq *pIocbOut)
{
	unsigned long iflag;
	struct lpfc_scsi_buf *lpfc_cmd =
		(struct lpfc_scsi_buf *) pIocbIn->context1;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	lpfc_free_scsi_buf(lpfc_cmd);
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
}

static void
lpfc_scsi_cmd_iocb_cmpl_aborted(struct lpfc_hba *phba,
				struct lpfc_iocbq *pIocbIn,
				struct lpfc_iocbq *pIocbOut)
{
	struct scsi_cmnd *ml_cmd =
		((struct lpfc_scsi_buf *) pIocbIn->context1)->pCmd;

	lpfc_scsi_cmd_iocb_cleanup (phba, pIocbIn, pIocbOut);
	ml_cmd->host_scribble = NULL;
}

const char *
lpfc_info(struct Scsi_Host *host)
{
	struct lpfc_hba    *phba = (struct lpfc_hba *) host->hostdata[0];
	int len;
	static char  lpfcinfobuf[384];

	memset(lpfcinfobuf,0,384);
	if (phba && phba->pcidev){
		strncpy(lpfcinfobuf, phba->ModelDesc, 256);
		len = strlen(lpfcinfobuf);
		snprintf(lpfcinfobuf + len,
			384-len,
			" on PCI bus %02x device %02x irq %d",
			phba->pcidev->bus->number,
			phba->pcidev->devfn,
			phba->pcidev->irq);
		len = strlen(lpfcinfobuf);
		if (phba->Port[0]) {
			snprintf(lpfcinfobuf + len,
				 384-len,
				 " port %s",
				 phba->Port);
		}
	}
	return lpfcinfobuf;
}

static int
lpfc_queuecommand(struct scsi_cmnd *cmnd, void (*done) (struct scsi_cmnd *))
{
	struct lpfc_hba *phba =
		(struct lpfc_hba *) cmnd->device->host->hostdata[0];
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_rport_data *rdata = cmnd->device->hostdata;
	struct lpfc_nodelist *ndlp = rdata->pnode;
	struct lpfc_scsi_buf *lpfc_cmd = NULL;
	struct list_head *scsi_buf_list = &phba->lpfc_scsi_buf_list;
	int err = 0;

	/*
	 * The target pointer is guaranteed not to be NULL because the driver
	 * only clears the device->hostdata field in lpfc_slave_destroy.  This
	 * approach guarantees no further IO calls on this target.
	 */
	if (!ndlp) {
		cmnd->result = ScsiResult(DID_NO_CONNECT, 0);
		goto out_fail_command;
	}

	/*
	 * A Fibre Channel target is present and functioning only when the node
	 * state is MAPPED.  Any other state is a failure.
	 */
	if (ndlp->nlp_state != NLP_STE_MAPPED_NODE) {
		if ((ndlp->nlp_state == NLP_STE_UNMAPPED_NODE) ||
		    (ndlp->nlp_state == NLP_STE_UNUSED_NODE)) {
			cmnd->result = ScsiResult(DID_NO_CONNECT, 0);
			goto out_fail_command;
		}
		else if (ndlp->nlp_state == NLP_STE_NPR_NODE) {
			cmnd->result = ScsiResult(DID_BUS_BUSY, 0);
			goto out_fail_command;
		}
		/*
		 * The device is most likely recovered and the driver
		 * needs a bit more time to finish.  Ask the midlayer
		 * to retry.
		 */
		goto out_host_busy;
	}

	list_remove_head(scsi_buf_list, lpfc_cmd, struct lpfc_scsi_buf, list);
	if (lpfc_cmd == NULL) {
		printk(KERN_WARNING "%s: No buffer available - list empty, "
		       "total count %d\n", __FUNCTION__, phba->total_scsi_bufs);
		goto out_host_busy;
	}

	/*
	 * Store the midlayer's command structure for the completion phase
	 * and complete the command initialization.
	 */
	lpfc_cmd->pCmd  = cmnd;
	lpfc_cmd->rdata = rdata;
	lpfc_cmd->timeout = 0;
	cmnd->host_scribble = (unsigned char *)lpfc_cmd;
	cmnd->scsi_done = done;

	err = lpfc_scsi_prep_dma_buf(phba, lpfc_cmd);
	if (err)
		goto out_host_busy_free_buf;

	lpfc_scsi_prep_cmnd(phba, lpfc_cmd, ndlp);

	err = lpfc_sli_issue_iocb(phba, &phba->sli.ring[psli->fcp_ring],
				&lpfc_cmd->cur_iocbq, SLI_IOCB_RET_IOCB);
	if (err)
		goto out_host_busy_free_buf;
	return 0;

 out_host_busy_free_buf:
	lpfc_free_scsi_buf(lpfc_cmd);
	cmnd->host_scribble = NULL;
 out_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

 out_fail_command:
	done(cmnd);
	return 0;
}

static int
__lpfc_abort_handler(struct scsi_cmnd *cmnd)
{
	struct lpfc_hba *phba =
			(struct lpfc_hba *)cmnd->device->host->hostdata[0];
	struct lpfc_sli_ring *pring = &phba->sli.ring[phba->sli.fcp_ring];
	struct lpfc_iocbq *iocb, *next_iocb;
	struct lpfc_iocbq *abtsiocb = NULL;
	struct lpfc_scsi_buf *lpfc_cmd;
	struct list_head *lpfc_iocb_list = &phba->lpfc_iocb_list;
	IOCB_t *cmd, *icmd;
	unsigned long snum;
	unsigned int id, lun;
	unsigned int loop_count = 0;
	int ret = IOCB_SUCCESS;

	/*
	 * If the host_scribble data area is NULL, then the driver has already
	 * completed this command, but the midlayer did not see the completion
	 * before the eh fired.  Just return SUCCESS.
	 */
	lpfc_cmd = (struct lpfc_scsi_buf *)cmnd->host_scribble;
	if (!lpfc_cmd)
		return SUCCESS;

	/* save these now since lpfc_cmd can be freed */
	id   = lpfc_cmd->pCmd->device->id;
	lun  = lpfc_cmd->pCmd->device->lun;
	snum = lpfc_cmd->pCmd->serial_number;

	list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
		cmd = &iocb->iocb;
		if (iocb->context1 != lpfc_cmd)
			continue;

		list_del_init(&iocb->list);
		pring->txq_cnt--;
		if (!iocb->iocb_cmpl) {
			list_add_tail(&iocb->list, lpfc_iocb_list);
		}
		else {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			lpfc_scsi_cmd_iocb_cmpl_aborted(phba, iocb, iocb);
		}

		goto out;
	}

	list_remove_head(lpfc_iocb_list, abtsiocb, struct lpfc_iocbq, list);
	if (abtsiocb == NULL)
		return FAILED;

	memset(abtsiocb, 0, sizeof (struct lpfc_iocbq));

	/*
	 * The scsi command was not in the txq.  Check the txcmplq and if it is
	 * found, send an abort to the FW.
	 */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list) {
		if (iocb->context1 != lpfc_cmd)
			continue;

		iocb->iocb_cmpl = lpfc_scsi_cmd_iocb_cmpl_aborted;
		cmd = &iocb->iocb;
		icmd = &abtsiocb->iocb;
		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= LPFC_LINK_UP)
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		else
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;

		abtsiocb->iocb_cmpl = lpfc_sli_abort_fcp_cmpl;
		if (lpfc_sli_issue_iocb(phba, pring, abtsiocb, 0) ==
								IOCB_ERROR) {
			list_add_tail(&abtsiocb->list, lpfc_iocb_list);
			ret = IOCB_ERROR;
			break;
		}

		/* Wait for abort to complete */
		while (cmnd->host_scribble)
		{
			spin_unlock_irq(phba->host->host_lock);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(LPFC_ABORT_WAIT*HZ);
			spin_lock_irq(phba->host->host_lock);
			if (++loop_count
			    > (2 * phba->cfg_nodev_tmo)/LPFC_ABORT_WAIT)
				break;
		}

		if(cmnd->host_scribble) {
			lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
					"%d:0748 abort handler timed "
					"out waiting for abort to "
					"complete. Data: "
					"x%x x%x x%x x%lx\n",
					phba->brd_no, ret, id, lun, snum);
			cmnd->host_scribble = NULL;
			iocb->iocb_cmpl = lpfc_scsi_cmd_iocb_cleanup;
			ret = IOCB_ERROR;
		}

		break;
	}

 out:
	lpfc_printf_log(phba, KERN_WARNING, LOG_FCP,
			"%d:0749 SCSI layer issued abort device "
			"Data: x%x x%x x%x x%lx\n",
			phba->brd_no, ret, id, lun, snum);

	return ret == IOCB_SUCCESS ? SUCCESS : FAILED;
}

static int
lpfc_abort_handler(struct scsi_cmnd *cmnd)
{
	int rc;
	spin_lock_irq(cmnd->device->host->host_lock);
	rc = __lpfc_abort_handler(cmnd);
	spin_unlock_irq(cmnd->device->host->host_lock);
	return rc;
}

static int
__lpfc_reset_lun_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct lpfc_hba *phba = (struct lpfc_hba *)shost->hostdata[0];
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_scsi_buf *lpfc_cmd = NULL;
	struct list_head *scsi_buf_list = &phba->lpfc_scsi_buf_list;
	struct list_head *lpfc_iocb_list = &phba->lpfc_iocb_list;
	struct lpfc_iocbq *iocbq, *iocbqrsp = NULL;
	struct lpfc_rport_data *rdata = cmnd->device->hostdata;
	struct lpfc_nodelist *pnode = rdata->pnode;
	int ret = FAILED;
	int cnt, loopcnt;

	/*
	 * If target is not in a MAPPED state, delay the reset until
	 * target is rediscovered or nodev timeout expires.
	 */
	while ( 1 ) {
		if (!pnode)
			break;

		if (pnode->nlp_state != NLP_STE_MAPPED_NODE) {
			spin_unlock_irq(phba->host->host_lock);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout( HZ/2);
			spin_lock_irq(phba->host->host_lock);
		}
		if ((pnode) && (pnode->nlp_state == NLP_STE_MAPPED_NODE))
			break;
	}

	list_remove_head(scsi_buf_list, lpfc_cmd, struct lpfc_scsi_buf, list);
	if (lpfc_cmd == NULL)
		goto out;

	lpfc_cmd->pCmd = cmnd;
	lpfc_cmd->timeout = 60;
	lpfc_cmd->scsi_hba = phba;

	ret = lpfc_scsi_prep_task_mgmt_cmd(phba, lpfc_cmd, FCP_LUN_RESET);
	if (!ret)
		goto out_free_scsi_buf;

	iocbq = &lpfc_cmd->cur_iocbq;

	/* get a buffer for this IOCB command response */
	list_remove_head(lpfc_iocb_list, iocbqrsp, struct lpfc_iocbq, list);
	if (iocbqrsp == NULL)
		goto out_free_scsi_buf;

	memset(iocbqrsp, 0, sizeof (struct lpfc_iocbq));

	iocbq->iocb_flag |= LPFC_IO_POLL;
	iocbq->iocb_cmpl = lpfc_sli_wake_iocb_high_priority;

	ret = lpfc_sli_issue_iocb_wait_high_priority(phba,
		     &phba->sli.ring[psli->fcp_ring],
		     iocbq, 0, iocbqrsp, 60);
	if (ret == IOCB_SUCCESS)
		ret = SUCCESS;

	lpfc_cmd->result = iocbqrsp->iocb.un.ulpWord[4];
	lpfc_cmd->status = iocbqrsp->iocb.ulpStatus;
	if (lpfc_cmd->status == IOSTAT_LOCAL_REJECT)
		if (lpfc_cmd->result & IOERR_DRVR_MASK)
			lpfc_cmd->status = IOSTAT_DRIVER_REJECT;

	/*
	 * All outstanding txcmplq I/Os should have been aborted by the target.
	 * Unfortunately, some targets do not abide by this forcing the driver
	 * to double check.
	 */
	lpfc_sli_abort_iocb(phba, &phba->sli.ring[phba->sli.fcp_ring],
			    cmnd->device->id, cmnd->device->lun, 0,
			    LPFC_CTX_LUN);

	loopcnt = 0;
	while((cnt = lpfc_sli_sum_iocb(phba,
				       &phba->sli.ring[phba->sli.fcp_ring],
				       cmnd->device->id, cmnd->device->lun,
				       LPFC_CTX_LUN))) {
		spin_unlock_irq(phba->host->host_lock);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(LPFC_RESET_WAIT*HZ);
		spin_lock_irq(phba->host->host_lock);

		if (++loopcnt
		    > (2 * phba->cfg_nodev_tmo)/LPFC_RESET_WAIT)
			break;
	}

	if (cnt) {
		lpfc_printf_log(phba, KERN_INFO, LOG_FCP,
			"%d:0719 LUN Reset I/O flush failure: cnt x%x\n",
			phba->brd_no, cnt);
	}

	list_add_tail(&iocbqrsp->list, lpfc_iocb_list);

out_free_scsi_buf:
	lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
			"%d:0713 SCSI layer issued LUN reset (%d, %d) "
			"Data: x%x x%x x%x\n",
			phba->brd_no, lpfc_cmd->pCmd->device->id,
			lpfc_cmd->pCmd->device->lun, ret, lpfc_cmd->status,
			lpfc_cmd->result);
	lpfc_free_scsi_buf(lpfc_cmd);
out:
	return ret;
}

static int
lpfc_reset_lun_handler(struct scsi_cmnd *cmnd)
{
	int rc;
	spin_lock_irq(cmnd->device->host->host_lock);
	rc = __lpfc_reset_lun_handler(cmnd);
	spin_unlock_irq(cmnd->device->host->host_lock);
	return rc;
}

/*
 * Note: midlayer calls this function with the host_lock held
 */
static int
__lpfc_reset_bus_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct lpfc_hba *phba = (struct lpfc_hba *)shost->hostdata[0];
	struct lpfc_nodelist *ndlp = NULL;
	int match;
	int ret = FAILED, i, err_count = 0;
	int cnt, loopcnt;
	unsigned int midlayer_id = 0;
	struct lpfc_scsi_buf * lpfc_cmd = NULL;
	struct list_head *scsi_buf_list = &phba->lpfc_scsi_buf_list;

	list_remove_head(scsi_buf_list, lpfc_cmd, struct lpfc_scsi_buf, list);
	if (lpfc_cmd == NULL)
		goto out;

	/* The lpfc_cmd storage is reused.  Set all loop invariants. */
	lpfc_cmd->timeout = 60;
	lpfc_cmd->pCmd = cmnd;
	lpfc_cmd->scsi_hba = phba;

	/*
	 * Since the driver manages a single bus device, reset all
	 * targets known to the driver.  Should any target reset
	 * fail, this routine returns failure to the midlayer.
	 */
	midlayer_id = cmnd->device->id;
	for (i = 0; i < MAX_FCP_TARGET; i++) {
		/* Search the mapped list for this target ID */
		match = 0;
		list_for_each_entry(ndlp, &phba->fc_nlpmap_list, nlp_listp) {
			if ((i == ndlp->nlp_sid) && ndlp->rport) {
				match = 1;
				break;
			}
		}
		if (!match)
			continue;

		lpfc_cmd->pCmd->device->id = i;
		lpfc_cmd->pCmd->device->hostdata = ndlp->rport->dd_data;
		ret = lpfc_scsi_tgt_reset(lpfc_cmd, phba);
		if (ret != SUCCESS) {
			lpfc_printf_log(phba, KERN_INFO, LOG_FCP,
				"%d:0713 Bus Reset on target %d failed\n",
				phba->brd_no, i);
			err_count++;
		}
	}

	cmnd->device->id = midlayer_id;
	loopcnt = 0;
	while((cnt = lpfc_sli_sum_iocb(phba,
				&phba->sli.ring[phba->sli.fcp_ring],
				0, 0, LPFC_CTX_HOST))) {
		spin_unlock_irq(phba->host->host_lock);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(LPFC_RESET_WAIT*HZ);
		spin_lock_irq(phba->host->host_lock);

		if (++loopcnt
		    > (2 * phba->cfg_nodev_tmo)/LPFC_RESET_WAIT)
			break;
	}

	if (cnt) {
		/* flush all outstanding commands on the host */
		i = lpfc_sli_abort_iocb(phba,
				&phba->sli.ring[phba->sli.fcp_ring], 0, 0, 0,
				LPFC_CTX_HOST);

		lpfc_printf_log(phba, KERN_INFO, LOG_FCP,
		   "%d:0715 Bus Reset I/O flush failure: cnt x%x left x%x\n",
		   phba->brd_no, cnt, i);
	}

	if (!err_count)
		ret = SUCCESS;

	lpfc_free_scsi_buf(lpfc_cmd);
	lpfc_printf_log(phba,
			KERN_ERR,
			LOG_FCP,
			"%d:0714 SCSI layer issued Bus Reset Data: x%x\n",
			phba->brd_no, ret);
out:
	return ret;
}

static int
lpfc_reset_bus_handler(struct scsi_cmnd *cmnd)
{
	int rc;
	spin_lock_irq(cmnd->device->host->host_lock);
	rc = __lpfc_reset_bus_handler(cmnd);
	spin_unlock_irq(cmnd->device->host->host_lock);
	return rc;
}

static int
lpfc_slave_alloc(struct scsi_device *sdev)
{
	struct lpfc_hba *phba = (struct lpfc_hba *)sdev->host->hostdata[0];
	struct lpfc_nodelist *ndlp = NULL;
	int match = 0;
	struct lpfc_scsi_buf *scsi_buf = NULL;
	uint32_t total = 0, i;
	uint32_t num_to_alloc = 0;
	unsigned long flags;
	struct list_head *listp;
	struct list_head *node_list[6];

	/*
	 * Store the target pointer in the scsi_device hostdata pointer provided
	 * the driver has already discovered the target id.
	 */

	/* Search the nlp lists other than unmap_list for this target ID */
	node_list[0] = &phba->fc_npr_list;
	node_list[1] = &phba->fc_nlpmap_list;
	node_list[2] = &phba->fc_prli_list;
	node_list[3] = &phba->fc_reglogin_list;
	node_list[4] = &phba->fc_adisc_list;
	node_list[5] = &phba->fc_plogi_list;

	for (i = 0; i < 6 && !match; i++) {
		listp = node_list[i];
		if (list_empty(listp))
			continue;
		list_for_each_entry(ndlp, listp, nlp_listp) {
			if ((sdev->id == ndlp->nlp_sid) && ndlp->rport) {
				match = 1;
				break;
			}
		}
	}

	if (!match)
		return -ENXIO;

	sdev->hostdata = ndlp->rport->dd_data;

	/*
	 * Populate the cmds_per_lun count scsi_bufs into this host's globally
	 * available list of scsi buffers.  Don't allocate more than the
	 * HBA limit conveyed to the midlayer via the host structure.  Note
	 * that this list of scsi bufs exists for the lifetime of the driver.
	 */
	total = phba->total_scsi_bufs;
	num_to_alloc = LPFC_CMD_PER_LUN;
	if (total >= phba->cfg_hba_queue_depth) {
		printk(KERN_WARNING "%s, At config limitation of "
		       "%d allocated scsi_bufs\n", __FUNCTION__, total);
		return 0;
	} else if (total + num_to_alloc > phba->cfg_hba_queue_depth) {
		num_to_alloc = phba->cfg_hba_queue_depth - total;
	}

	for (i = 0; i < num_to_alloc; i++) {
		scsi_buf = lpfc_get_scsi_buf(phba);
		if (!scsi_buf) {
			printk(KERN_ERR "%s, failed to allocate "
			       "scsi_buf\n", __FUNCTION__);
			break;
		}

		spin_lock_irqsave(phba->host->host_lock, flags);
		phba->total_scsi_bufs++;
		list_add_tail(&scsi_buf->list, &phba->lpfc_scsi_buf_list);
		spin_unlock_irqrestore(phba->host->host_lock, flags);
	}
	return 0;
}

static int
lpfc_slave_configure(struct scsi_device *sdev)
{
	struct lpfc_hba *phba = (struct lpfc_hba *) sdev->host->hostdata[0];
	struct fc_rport *rport = starget_to_rport(sdev->sdev_target);

	if (sdev->tagged_supported)
		scsi_activate_tcq(sdev, phba->cfg_lun_queue_depth);
	else
		scsi_deactivate_tcq(sdev, phba->cfg_lun_queue_depth);

	/*
	 * Initialize the fc transport attributes for the target
	 * containing this scsi device.  Also note that the driver's
	 * target pointer is stored in the starget_data for the
	 * driver's sysfs entry point functions.
	 */
	rport->dev_loss_tmo = phba->cfg_nodev_tmo + 5;

	return 0;
}

static void
lpfc_slave_destroy(struct scsi_device *sdev)
{
	sdev->hostdata = NULL;
	return;
}

struct scsi_host_template lpfc_template = {
	.module			= THIS_MODULE,
	.name			= LPFC_DRIVER_NAME,
	.info			= lpfc_info,
	.queuecommand		= lpfc_queuecommand,
	.eh_abort_handler	= lpfc_abort_handler,
	.eh_device_reset_handler= lpfc_reset_lun_handler,
	.eh_bus_reset_handler	= lpfc_reset_bus_handler,
	.slave_alloc		= lpfc_slave_alloc,
	.slave_configure	= lpfc_slave_configure,
	.slave_destroy		= lpfc_slave_destroy,
	.this_id		= -1,
	.sg_tablesize		= LPFC_SG_SEG_CNT,
	.cmd_per_lun		= LPFC_CMD_PER_LUN,
	.use_clustering		= ENABLE_CLUSTERING,
	.shost_attrs		= lpfc_host_attrs,
	.max_sectors		= 0xFFFF,
};
