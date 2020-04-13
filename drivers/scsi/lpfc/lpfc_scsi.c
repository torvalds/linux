/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2020 Broadcom. All Rights Reserved. The term *
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
 *******************************************************************/
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <asm/unaligned.h>
#include <linux/t10-pi.h>
#include <linux/crc-t10dif.h>
#include <net/checksum.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_version.h"
#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"

#define LPFC_RESET_WAIT  2
#define LPFC_ABORT_WAIT  2

static char *dif_op_str[] = {
	"PROT_NORMAL",
	"PROT_READ_INSERT",
	"PROT_WRITE_STRIP",
	"PROT_READ_STRIP",
	"PROT_WRITE_INSERT",
	"PROT_READ_PASS",
	"PROT_WRITE_PASS",
};

struct scsi_dif_tuple {
	__be16 guard_tag;       /* Checksum */
	__be16 app_tag;         /* Opaque storage */
	__be32 ref_tag;         /* Target LBA or indirect LBA */
};

static struct lpfc_rport_data *
lpfc_rport_data_from_scsi_device(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)sdev->host->hostdata;

	if (vport->phba->cfg_fof)
		return ((struct lpfc_device_data *)sdev->hostdata)->rport_data;
	else
		return (struct lpfc_rport_data *)sdev->hostdata;
}

static void
lpfc_release_scsi_buf_s4(struct lpfc_hba *phba, struct lpfc_io_buf *psb);
static void
lpfc_release_scsi_buf_s3(struct lpfc_hba *phba, struct lpfc_io_buf *psb);
static int
lpfc_prot_group_type(struct lpfc_hba *phba, struct scsi_cmnd *sc);

static inline unsigned
lpfc_cmd_blksize(struct scsi_cmnd *sc)
{
	return sc->device->sector_size;
}

#define LPFC_CHECK_PROTECT_GUARD	1
#define LPFC_CHECK_PROTECT_REF		2
static inline unsigned
lpfc_cmd_protect(struct scsi_cmnd *sc, int flag)
{
	return 1;
}

static inline unsigned
lpfc_cmd_guard_csum(struct scsi_cmnd *sc)
{
	if (lpfc_prot_group_type(NULL, sc) == LPFC_PG_TYPE_NO_DIF)
		return 0;
	if (scsi_host_get_guard(sc->device->host) == SHOST_DIX_GUARD_IP)
		return 1;
	return 0;
}

/**
 * lpfc_sli4_set_rsp_sgl_last - Set the last bit in the response sge.
 * @phba: Pointer to HBA object.
 * @lpfc_cmd: lpfc scsi command object pointer.
 *
 * This function is called from the lpfc_prep_task_mgmt_cmd function to
 * set the last bit in the response sge entry.
 **/
static void
lpfc_sli4_set_rsp_sgl_last(struct lpfc_hba *phba,
				struct lpfc_io_buf *lpfc_cmd)
{
	struct sli4_sge *sgl = (struct sli4_sge *)lpfc_cmd->dma_sgl;
	if (sgl) {
		sgl += 1;
		sgl->word2 = le32_to_cpu(sgl->word2);
		bf_set(lpfc_sli4_sge_last, sgl, 1);
		sgl->word2 = cpu_to_le32(sgl->word2);
	}
}

/**
 * lpfc_update_stats - Update statistical data for the command completion
 * @vport: The virtual port on which this call is executing.
 * @lpfc_cmd: lpfc scsi command object pointer.
 *
 * This function is called when there is a command completion and this
 * function updates the statistical data for the command completion.
 **/
static void
lpfc_update_stats(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	unsigned long flags;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	unsigned long latency;
	int i;

	if (!vport->stat_data_enabled ||
	    vport->stat_data_blocked ||
	    (cmd->result))
		return;

	latency = jiffies_to_msecs((long)jiffies - (long)lpfc_cmd->start_time);
	rdata = lpfc_cmd->rdata;
	pnode = rdata->pnode;

	spin_lock_irqsave(shost->host_lock, flags);
	if (!pnode ||
	    !pnode->lat_data ||
	    (phba->bucket_type == LPFC_NO_BUCKET)) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		return;
	}

	if (phba->bucket_type == LPFC_LINEAR_BUCKET) {
		i = (latency + phba->bucket_step - 1 - phba->bucket_base)/
			phba->bucket_step;
		/* check array subscript bounds */
		if (i < 0)
			i = 0;
		else if (i >= LPFC_MAX_BUCKET_COUNT)
			i = LPFC_MAX_BUCKET_COUNT - 1;
	} else {
		for (i = 0; i < LPFC_MAX_BUCKET_COUNT-1; i++)
			if (latency <= (phba->bucket_base +
				((1<<i)*phba->bucket_step)))
				break;
	}

	pnode->lat_data[i].cmd_count++;
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/**
 * lpfc_rampdown_queue_depth - Post RAMP_DOWN_QUEUE event to worker thread
 * @phba: The Hba for which this call is being executed.
 *
 * This routine is called when there is resource error in driver or firmware.
 * This routine posts WORKER_RAMP_DOWN_QUEUE event for @phba. This routine
 * posts at most 1 event each second. This routine wakes up worker thread of
 * @phba to process WORKER_RAM_DOWN_EVENT event.
 *
 * This routine should be called with no lock held.
 **/
void
lpfc_rampdown_queue_depth(struct lpfc_hba *phba)
{
	unsigned long flags;
	uint32_t evt_posted;
	unsigned long expires;

	spin_lock_irqsave(&phba->hbalock, flags);
	atomic_inc(&phba->num_rsrc_err);
	phba->last_rsrc_error_time = jiffies;

	expires = phba->last_ramp_down_time + QUEUE_RAMP_DOWN_INTERVAL;
	if (time_after(expires, jiffies)) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}

	phba->last_ramp_down_time = jiffies;

	spin_unlock_irqrestore(&phba->hbalock, flags);

	spin_lock_irqsave(&phba->pport->work_port_lock, flags);
	evt_posted = phba->pport->work_port_events & WORKER_RAMP_DOWN_QUEUE;
	if (!evt_posted)
		phba->pport->work_port_events |= WORKER_RAMP_DOWN_QUEUE;
	spin_unlock_irqrestore(&phba->pport->work_port_lock, flags);

	if (!evt_posted)
		lpfc_worker_wake_up(phba);
	return;
}

/**
 * lpfc_ramp_down_queue_handler - WORKER_RAMP_DOWN_QUEUE event handler
 * @phba: The Hba for which this call is being executed.
 *
 * This routine is called to  process WORKER_RAMP_DOWN_QUEUE event for worker
 * thread.This routine reduces queue depth for all scsi device on each vport
 * associated with @phba.
 **/
void
lpfc_ramp_down_queue_handler(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	struct scsi_device *sdev;
	unsigned long new_queue_depth;
	unsigned long num_rsrc_err, num_cmd_success;
	int i;

	num_rsrc_err = atomic_read(&phba->num_rsrc_err);
	num_cmd_success = atomic_read(&phba->num_cmd_success);

	/*
	 * The error and success command counters are global per
	 * driver instance.  If another handler has already
	 * operated on this error event, just exit.
	 */
	if (num_rsrc_err == 0)
		return;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			shost_for_each_device(sdev, shost) {
				new_queue_depth =
					sdev->queue_depth * num_rsrc_err /
					(num_rsrc_err + num_cmd_success);
				if (!new_queue_depth)
					new_queue_depth = sdev->queue_depth - 1;
				else
					new_queue_depth = sdev->queue_depth -
								new_queue_depth;
				scsi_change_queue_depth(sdev, new_queue_depth);
			}
		}
	lpfc_destroy_vport_work_array(phba, vports);
	atomic_set(&phba->num_rsrc_err, 0);
	atomic_set(&phba->num_cmd_success, 0);
}

/**
 * lpfc_scsi_dev_block - set all scsi hosts to block state
 * @phba: Pointer to HBA context object.
 *
 * This function walks vport list and set each SCSI host to block state
 * by invoking fc_remote_port_delete() routine. This function is invoked
 * with EEH when device's PCI slot has been permanently disabled.
 **/
void
lpfc_scsi_dev_block(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	struct scsi_device *sdev;
	struct fc_rport *rport;
	int i;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			shost_for_each_device(sdev, shost) {
				rport = starget_to_rport(scsi_target(sdev));
				fc_remote_port_delete(rport);
			}
		}
	lpfc_destroy_vport_work_array(phba, vports);
}

/**
 * lpfc_new_scsi_buf_s3 - Scsi buffer allocator for HBA with SLI3 IF spec
 * @vport: The virtual port for which this call being executed.
 * @num_to_allocate: The requested number of buffers to allocate.
 *
 * This routine allocates a scsi buffer for device with SLI-3 interface spec,
 * the scsi buffer contains all the necessary information needed to initiate
 * a SCSI I/O. The non-DMAable buffer region contains information to build
 * the IOCB. The DMAable region contains memory for the FCP CMND, FCP RSP,
 * and the initial BPL. In addition to allocating memory, the FCP CMND and
 * FCP RSP BDEs are setup in the BPL and the BPL BDE is setup in the IOCB.
 *
 * Return codes:
 *   int - number of scsi buffers that were allocated.
 *   0 = failure, less than num_to_alloc is a partial failure.
 **/
static int
lpfc_new_scsi_buf_s3(struct lpfc_vport *vport, int num_to_alloc)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_io_buf *psb;
	struct ulp_bde64 *bpl;
	IOCB_t *iocb;
	dma_addr_t pdma_phys_fcp_cmd;
	dma_addr_t pdma_phys_fcp_rsp;
	dma_addr_t pdma_phys_sgl;
	uint16_t iotag;
	int bcnt, bpl_size;

	bpl_size = phba->cfg_sg_dma_buf_size -
		(sizeof(struct fcp_cmnd) + sizeof(struct fcp_rsp));

	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			 "9067 ALLOC %d scsi_bufs: %d (%d + %d + %d)\n",
			 num_to_alloc, phba->cfg_sg_dma_buf_size,
			 (int)sizeof(struct fcp_cmnd),
			 (int)sizeof(struct fcp_rsp), bpl_size);

	for (bcnt = 0; bcnt < num_to_alloc; bcnt++) {
		psb = kzalloc(sizeof(struct lpfc_io_buf), GFP_KERNEL);
		if (!psb)
			break;

		/*
		 * Get memory from the pci pool to map the virt space to pci
		 * bus space for an I/O.  The DMA buffer includes space for the
		 * struct fcp_cmnd, struct fcp_rsp and the number of bde's
		 * necessary to support the sg_tablesize.
		 */
		psb->data = dma_pool_zalloc(phba->lpfc_sg_dma_buf_pool,
					GFP_KERNEL, &psb->dma_handle);
		if (!psb->data) {
			kfree(psb);
			break;
		}


		/* Allocate iotag for psb->cur_iocbq. */
		iotag = lpfc_sli_next_iotag(phba, &psb->cur_iocbq);
		if (iotag == 0) {
			dma_pool_free(phba->lpfc_sg_dma_buf_pool,
				      psb->data, psb->dma_handle);
			kfree(psb);
			break;
		}
		psb->cur_iocbq.iocb_flag |= LPFC_IO_FCP;

		psb->fcp_cmnd = psb->data;
		psb->fcp_rsp = psb->data + sizeof(struct fcp_cmnd);
		psb->dma_sgl = psb->data + sizeof(struct fcp_cmnd) +
			sizeof(struct fcp_rsp);

		/* Initialize local short-hand pointers. */
		bpl = (struct ulp_bde64 *)psb->dma_sgl;
		pdma_phys_fcp_cmd = psb->dma_handle;
		pdma_phys_fcp_rsp = psb->dma_handle + sizeof(struct fcp_cmnd);
		pdma_phys_sgl = psb->dma_handle + sizeof(struct fcp_cmnd) +
			sizeof(struct fcp_rsp);

		/*
		 * The first two bdes are the FCP_CMD and FCP_RSP. The balance
		 * are sg list bdes.  Initialize the first two and leave the
		 * rest for queuecommand.
		 */
		bpl[0].addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys_fcp_cmd));
		bpl[0].addrLow = le32_to_cpu(putPaddrLow(pdma_phys_fcp_cmd));
		bpl[0].tus.f.bdeSize = sizeof(struct fcp_cmnd);
		bpl[0].tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		bpl[0].tus.w = le32_to_cpu(bpl[0].tus.w);

		/* Setup the physical region for the FCP RSP */
		bpl[1].addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys_fcp_rsp));
		bpl[1].addrLow = le32_to_cpu(putPaddrLow(pdma_phys_fcp_rsp));
		bpl[1].tus.f.bdeSize = sizeof(struct fcp_rsp);
		bpl[1].tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		bpl[1].tus.w = le32_to_cpu(bpl[1].tus.w);

		/*
		 * Since the IOCB for the FCP I/O is built into this
		 * lpfc_scsi_buf, initialize it with all known data now.
		 */
		iocb = &psb->cur_iocbq.iocb;
		iocb->un.fcpi64.bdl.ulpIoTag32 = 0;
		if ((phba->sli_rev == 3) &&
				!(phba->sli3_options & LPFC_SLI3_BG_ENABLED)) {
			/* fill in immediate fcp command BDE */
			iocb->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDE_IMMED;
			iocb->un.fcpi64.bdl.bdeSize = sizeof(struct fcp_cmnd);
			iocb->un.fcpi64.bdl.addrLow = offsetof(IOCB_t,
					unsli3.fcp_ext.icd);
			iocb->un.fcpi64.bdl.addrHigh = 0;
			iocb->ulpBdeCount = 0;
			iocb->ulpLe = 0;
			/* fill in response BDE */
			iocb->unsli3.fcp_ext.rbde.tus.f.bdeFlags =
							BUFF_TYPE_BDE_64;
			iocb->unsli3.fcp_ext.rbde.tus.f.bdeSize =
				sizeof(struct fcp_rsp);
			iocb->unsli3.fcp_ext.rbde.addrLow =
				putPaddrLow(pdma_phys_fcp_rsp);
			iocb->unsli3.fcp_ext.rbde.addrHigh =
				putPaddrHigh(pdma_phys_fcp_rsp);
		} else {
			iocb->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
			iocb->un.fcpi64.bdl.bdeSize =
					(2 * sizeof(struct ulp_bde64));
			iocb->un.fcpi64.bdl.addrLow =
					putPaddrLow(pdma_phys_sgl);
			iocb->un.fcpi64.bdl.addrHigh =
					putPaddrHigh(pdma_phys_sgl);
			iocb->ulpBdeCount = 1;
			iocb->ulpLe = 1;
		}
		iocb->ulpClass = CLASS3;
		psb->status = IOSTAT_SUCCESS;
		/* Put it back into the SCSI buffer list */
		psb->cur_iocbq.context1  = psb;
		spin_lock_init(&psb->buf_lock);
		lpfc_release_scsi_buf_s3(phba, psb);

	}

	return bcnt;
}

/**
 * lpfc_sli4_vport_delete_fcp_xri_aborted -Remove all ndlp references for vport
 * @vport: pointer to lpfc vport data structure.
 *
 * This routine is invoked by the vport cleanup for deletions and the cleanup
 * for an ndlp on removal.
 **/
void
lpfc_sli4_vport_delete_fcp_xri_aborted(struct lpfc_vport *vport)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_io_buf *psb, *next_psb;
	struct lpfc_sli4_hdw_queue *qp;
	unsigned long iflag = 0;
	int idx;

	if (!(vport->cfg_enable_fc4_type & LPFC_ENABLE_FCP))
		return;

	spin_lock_irqsave(&phba->hbalock, iflag);
	for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
		qp = &phba->sli4_hba.hdwq[idx];

		spin_lock(&qp->abts_io_buf_list_lock);
		list_for_each_entry_safe(psb, next_psb,
					 &qp->lpfc_abts_io_buf_list, list) {
			if (psb->cur_iocbq.iocb_flag & LPFC_IO_NVME)
				continue;

			if (psb->rdata && psb->rdata->pnode &&
			    psb->rdata->pnode->vport == vport)
				psb->rdata = NULL;
		}
		spin_unlock(&qp->abts_io_buf_list_lock);
	}
	spin_unlock_irqrestore(&phba->hbalock, iflag);
}

/**
 * lpfc_sli4_io_xri_aborted - Fast-path process of fcp xri abort
 * @phba: pointer to lpfc hba data structure.
 * @axri: pointer to the fcp xri abort wcqe structure.
 *
 * This routine is invoked by the worker thread to process a SLI4 fast-path
 * FCP or NVME aborted xri.
 **/
void
lpfc_sli4_io_xri_aborted(struct lpfc_hba *phba,
			 struct sli4_wcqe_xri_aborted *axri, int idx)
{
	uint16_t xri = bf_get(lpfc_wcqe_xa_xri, axri);
	uint16_t rxid = bf_get(lpfc_wcqe_xa_remote_xid, axri);
	struct lpfc_io_buf *psb, *next_psb;
	struct lpfc_sli4_hdw_queue *qp;
	unsigned long iflag = 0;
	struct lpfc_iocbq *iocbq;
	int i;
	struct lpfc_nodelist *ndlp;
	int rrq_empty = 0;
	struct lpfc_sli_ring *pring = phba->sli4_hba.els_wq->pring;

	if (!(phba->cfg_enable_fc4_type & LPFC_ENABLE_FCP))
		return;

	qp = &phba->sli4_hba.hdwq[idx];
	spin_lock_irqsave(&phba->hbalock, iflag);
	spin_lock(&qp->abts_io_buf_list_lock);
	list_for_each_entry_safe(psb, next_psb,
		&qp->lpfc_abts_io_buf_list, list) {
		if (psb->cur_iocbq.sli4_xritag == xri) {
			list_del_init(&psb->list);
			psb->flags &= ~LPFC_SBUF_XBUSY;
			psb->status = IOSTAT_SUCCESS;
			if (psb->cur_iocbq.iocb_flag & LPFC_IO_NVME) {
				qp->abts_nvme_io_bufs--;
				spin_unlock(&qp->abts_io_buf_list_lock);
				spin_unlock_irqrestore(&phba->hbalock, iflag);
				lpfc_sli4_nvme_xri_aborted(phba, axri, psb);
				return;
			}
			qp->abts_scsi_io_bufs--;
			spin_unlock(&qp->abts_io_buf_list_lock);

			if (psb->rdata && psb->rdata->pnode)
				ndlp = psb->rdata->pnode;
			else
				ndlp = NULL;

			rrq_empty = list_empty(&phba->active_rrq_list);
			spin_unlock_irqrestore(&phba->hbalock, iflag);
			if (ndlp) {
				lpfc_set_rrq_active(phba, ndlp,
					psb->cur_iocbq.sli4_lxritag, rxid, 1);
				lpfc_sli4_abts_err_handler(phba, ndlp, axri);
			}
			lpfc_release_scsi_buf_s4(phba, psb);
			if (rrq_empty)
				lpfc_worker_wake_up(phba);
			return;
		}
	}
	spin_unlock(&qp->abts_io_buf_list_lock);
	for (i = 1; i <= phba->sli.last_iotag; i++) {
		iocbq = phba->sli.iocbq_lookup[i];

		if (!(iocbq->iocb_flag & LPFC_IO_FCP) ||
		    (iocbq->iocb_flag & LPFC_IO_LIBDFC))
			continue;
		if (iocbq->sli4_xritag != xri)
			continue;
		psb = container_of(iocbq, struct lpfc_io_buf, cur_iocbq);
		psb->flags &= ~LPFC_SBUF_XBUSY;
		spin_unlock_irqrestore(&phba->hbalock, iflag);
		if (!list_empty(&pring->txq))
			lpfc_worker_wake_up(phba);
		return;

	}
	spin_unlock_irqrestore(&phba->hbalock, iflag);
}

/**
 * lpfc_get_scsi_buf_s3 - Get a scsi buffer from lpfc_scsi_buf_list of the HBA
 * @phba: The HBA for which this call is being executed.
 *
 * This routine removes a scsi buffer from head of @phba lpfc_scsi_buf_list list
 * and returns to caller.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_scsi_buf - Success
 **/
static struct lpfc_io_buf *
lpfc_get_scsi_buf_s3(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		     struct scsi_cmnd *cmnd)
{
	struct lpfc_io_buf *lpfc_cmd = NULL;
	struct list_head *scsi_buf_list_get = &phba->lpfc_scsi_buf_list_get;
	unsigned long iflag = 0;

	spin_lock_irqsave(&phba->scsi_buf_list_get_lock, iflag);
	list_remove_head(scsi_buf_list_get, lpfc_cmd, struct lpfc_io_buf,
			 list);
	if (!lpfc_cmd) {
		spin_lock(&phba->scsi_buf_list_put_lock);
		list_splice(&phba->lpfc_scsi_buf_list_put,
			    &phba->lpfc_scsi_buf_list_get);
		INIT_LIST_HEAD(&phba->lpfc_scsi_buf_list_put);
		list_remove_head(scsi_buf_list_get, lpfc_cmd,
				 struct lpfc_io_buf, list);
		spin_unlock(&phba->scsi_buf_list_put_lock);
	}
	spin_unlock_irqrestore(&phba->scsi_buf_list_get_lock, iflag);

	if (lpfc_ndlp_check_qdepth(phba, ndlp) && lpfc_cmd) {
		atomic_inc(&ndlp->cmd_pending);
		lpfc_cmd->flags |= LPFC_SBUF_BUMP_QDEPTH;
	}
	return  lpfc_cmd;
}
/**
 * lpfc_get_scsi_buf_s4 - Get a scsi buffer from io_buf_list of the HBA
 * @phba: The HBA for which this call is being executed.
 *
 * This routine removes a scsi buffer from head of @hdwq io_buf_list
 * and returns to caller.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_scsi_buf - Success
 **/
static struct lpfc_io_buf *
lpfc_get_scsi_buf_s4(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		     struct scsi_cmnd *cmnd)
{
	struct lpfc_io_buf *lpfc_cmd;
	struct lpfc_sli4_hdw_queue *qp;
	struct sli4_sge *sgl;
	IOCB_t *iocb;
	dma_addr_t pdma_phys_fcp_rsp;
	dma_addr_t pdma_phys_fcp_cmd;
	uint32_t cpu, idx;
	int tag;
	struct fcp_cmd_rsp_buf *tmp = NULL;

	cpu = raw_smp_processor_id();
	if (cmnd && phba->cfg_fcp_io_sched == LPFC_FCP_SCHED_BY_HDWQ) {
		tag = blk_mq_unique_tag(cmnd->request);
		idx = blk_mq_unique_tag_to_hwq(tag);
	} else {
		idx = phba->sli4_hba.cpu_map[cpu].hdwq;
	}

	lpfc_cmd = lpfc_get_io_buf(phba, ndlp, idx,
				   !phba->cfg_xri_rebalancing);
	if (!lpfc_cmd) {
		qp = &phba->sli4_hba.hdwq[idx];
		qp->empty_io_bufs++;
		return NULL;
	}

	/* Setup key fields in buffer that may have been changed
	 * if other protocols used this buffer.
	 */
	lpfc_cmd->cur_iocbq.iocb_flag = LPFC_IO_FCP;
	lpfc_cmd->prot_seg_cnt = 0;
	lpfc_cmd->seg_cnt = 0;
	lpfc_cmd->timeout = 0;
	lpfc_cmd->flags = 0;
	lpfc_cmd->start_time = jiffies;
	lpfc_cmd->waitq = NULL;
	lpfc_cmd->cpu = cpu;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	lpfc_cmd->prot_data_type = 0;
#endif
	tmp = lpfc_get_cmd_rsp_buf_per_hdwq(phba, lpfc_cmd);
	if (!tmp) {
		lpfc_release_io_buf(phba, lpfc_cmd, lpfc_cmd->hdwq);
		return NULL;
	}

	lpfc_cmd->fcp_cmnd = tmp->fcp_cmnd;
	lpfc_cmd->fcp_rsp = tmp->fcp_rsp;

	/*
	 * The first two SGEs are the FCP_CMD and FCP_RSP.
	 * The balance are sg list bdes. Initialize the
	 * first two and leave the rest for queuecommand.
	 */
	sgl = (struct sli4_sge *)lpfc_cmd->dma_sgl;
	pdma_phys_fcp_cmd = tmp->fcp_cmd_rsp_dma_handle;
	sgl->addr_hi = cpu_to_le32(putPaddrHigh(pdma_phys_fcp_cmd));
	sgl->addr_lo = cpu_to_le32(putPaddrLow(pdma_phys_fcp_cmd));
	sgl->word2 = le32_to_cpu(sgl->word2);
	bf_set(lpfc_sli4_sge_last, sgl, 0);
	sgl->word2 = cpu_to_le32(sgl->word2);
	sgl->sge_len = cpu_to_le32(sizeof(struct fcp_cmnd));
	sgl++;

	/* Setup the physical region for the FCP RSP */
	pdma_phys_fcp_rsp = pdma_phys_fcp_cmd + sizeof(struct fcp_cmnd);
	sgl->addr_hi = cpu_to_le32(putPaddrHigh(pdma_phys_fcp_rsp));
	sgl->addr_lo = cpu_to_le32(putPaddrLow(pdma_phys_fcp_rsp));
	sgl->word2 = le32_to_cpu(sgl->word2);
	bf_set(lpfc_sli4_sge_last, sgl, 1);
	sgl->word2 = cpu_to_le32(sgl->word2);
	sgl->sge_len = cpu_to_le32(sizeof(struct fcp_rsp));

	/*
	 * Since the IOCB for the FCP I/O is built into this
	 * lpfc_io_buf, initialize it with all known data now.
	 */
	iocb = &lpfc_cmd->cur_iocbq.iocb;
	iocb->un.fcpi64.bdl.ulpIoTag32 = 0;
	iocb->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDE_64;
	/* setting the BLP size to 2 * sizeof BDE may not be correct.
	 * We are setting the bpl to point to out sgl. An sgl's
	 * entries are 16 bytes, a bpl entries are 12 bytes.
	 */
	iocb->un.fcpi64.bdl.bdeSize = sizeof(struct fcp_cmnd);
	iocb->un.fcpi64.bdl.addrLow = putPaddrLow(pdma_phys_fcp_cmd);
	iocb->un.fcpi64.bdl.addrHigh = putPaddrHigh(pdma_phys_fcp_cmd);
	iocb->ulpBdeCount = 1;
	iocb->ulpLe = 1;
	iocb->ulpClass = CLASS3;

	if (lpfc_ndlp_check_qdepth(phba, ndlp)) {
		atomic_inc(&ndlp->cmd_pending);
		lpfc_cmd->flags |= LPFC_SBUF_BUMP_QDEPTH;
	}
	return  lpfc_cmd;
}
/**
 * lpfc_get_scsi_buf - Get a scsi buffer from lpfc_scsi_buf_list of the HBA
 * @phba: The HBA for which this call is being executed.
 *
 * This routine removes a scsi buffer from head of @phba lpfc_scsi_buf_list list
 * and returns to caller.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_scsi_buf - Success
 **/
static struct lpfc_io_buf*
lpfc_get_scsi_buf(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		  struct scsi_cmnd *cmnd)
{
	return  phba->lpfc_get_scsi_buf(phba, ndlp, cmnd);
}

/**
 * lpfc_release_scsi_buf - Return a scsi buffer back to hba scsi buf list
 * @phba: The Hba for which this call is being executed.
 * @psb: The scsi buffer which is being released.
 *
 * This routine releases @psb scsi buffer by adding it to tail of @phba
 * lpfc_scsi_buf_list list.
 **/
static void
lpfc_release_scsi_buf_s3(struct lpfc_hba *phba, struct lpfc_io_buf *psb)
{
	unsigned long iflag = 0;

	psb->seg_cnt = 0;
	psb->prot_seg_cnt = 0;

	spin_lock_irqsave(&phba->scsi_buf_list_put_lock, iflag);
	psb->pCmd = NULL;
	psb->cur_iocbq.iocb_flag = LPFC_IO_FCP;
	list_add_tail(&psb->list, &phba->lpfc_scsi_buf_list_put);
	spin_unlock_irqrestore(&phba->scsi_buf_list_put_lock, iflag);
}

/**
 * lpfc_release_scsi_buf_s4: Return a scsi buffer back to hba scsi buf list.
 * @phba: The Hba for which this call is being executed.
 * @psb: The scsi buffer which is being released.
 *
 * This routine releases @psb scsi buffer by adding it to tail of @hdwq
 * io_buf_list list. For SLI4 XRI's are tied to the scsi buffer
 * and cannot be reused for at least RA_TOV amount of time if it was
 * aborted.
 **/
static void
lpfc_release_scsi_buf_s4(struct lpfc_hba *phba, struct lpfc_io_buf *psb)
{
	struct lpfc_sli4_hdw_queue *qp;
	unsigned long iflag = 0;

	psb->seg_cnt = 0;
	psb->prot_seg_cnt = 0;

	qp = psb->hdwq;
	if (psb->flags & LPFC_SBUF_XBUSY) {
		spin_lock_irqsave(&qp->abts_io_buf_list_lock, iflag);
		psb->pCmd = NULL;
		list_add_tail(&psb->list, &qp->lpfc_abts_io_buf_list);
		qp->abts_scsi_io_bufs++;
		spin_unlock_irqrestore(&qp->abts_io_buf_list_lock, iflag);
	} else {
		lpfc_release_io_buf(phba, (struct lpfc_io_buf *)psb, qp);
	}
}

/**
 * lpfc_release_scsi_buf: Return a scsi buffer back to hba scsi buf list.
 * @phba: The Hba for which this call is being executed.
 * @psb: The scsi buffer which is being released.
 *
 * This routine releases @psb scsi buffer by adding it to tail of @phba
 * lpfc_scsi_buf_list list.
 **/
static void
lpfc_release_scsi_buf(struct lpfc_hba *phba, struct lpfc_io_buf *psb)
{
	if ((psb->flags & LPFC_SBUF_BUMP_QDEPTH) && psb->ndlp)
		atomic_dec(&psb->ndlp->cmd_pending);

	psb->flags &= ~LPFC_SBUF_BUMP_QDEPTH;
	phba->lpfc_release_scsi_buf(phba, psb);
}

/**
 * lpfc_scsi_prep_dma_buf_s3 - DMA mapping for scsi buffer to SLI3 IF spec
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This routine does the pci dma mapping for scatter-gather list of scsi cmnd
 * field of @lpfc_cmd for device with SLI-3 interface spec. This routine scans
 * through sg elements and format the bde. This routine also initializes all
 * IOCB fields which are dependent on scsi command request buffer.
 *
 * Return codes:
 *   1 - Error
 *   0 - Success
 **/
static int
lpfc_scsi_prep_dma_buf_s3(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct scatterlist *sgel = NULL;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct ulp_bde64 *bpl = (struct ulp_bde64 *)lpfc_cmd->dma_sgl;
	struct lpfc_iocbq *iocbq = &lpfc_cmd->cur_iocbq;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	struct ulp_bde64 *data_bde = iocb_cmd->unsli3.fcp_ext.dbde;
	dma_addr_t physaddr;
	uint32_t num_bde = 0;
	int nseg, datadir = scsi_cmnd->sc_data_direction;

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	bpl += 2;
	if (scsi_sg_count(scsi_cmnd)) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */

		nseg = dma_map_sg(&phba->pcidev->dev, scsi_sglist(scsi_cmnd),
				  scsi_sg_count(scsi_cmnd), datadir);
		if (unlikely(!nseg))
			return 1;

		lpfc_cmd->seg_cnt = nseg;
		if (lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt) {
			lpfc_printf_log(phba, KERN_ERR, LOG_BG,
				"9064 BLKGRD: %s: Too many sg segments from "
			       "dma_map_sg.  Config %d, seg_cnt %d\n",
			       __func__, phba->cfg_sg_seg_cnt,
			       lpfc_cmd->seg_cnt);
			WARN_ON_ONCE(lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt);
			lpfc_cmd->seg_cnt = 0;
			scsi_dma_unmap(scsi_cmnd);
			return 2;
		}

		/*
		 * The driver established a maximum scatter-gather segment count
		 * during probe that limits the number of sg elements in any
		 * single scsi command.  Just run through the seg_cnt and format
		 * the bde's.
		 * When using SLI-3 the driver will try to fit all the BDEs into
		 * the IOCB. If it can't then the BDEs get added to a BPL as it
		 * does for SLI-2 mode.
		 */
		scsi_for_each_sg(scsi_cmnd, sgel, nseg, num_bde) {
			physaddr = sg_dma_address(sgel);
			if (phba->sli_rev == 3 &&
			    !(phba->sli3_options & LPFC_SLI3_BG_ENABLED) &&
			    !(iocbq->iocb_flag & DSS_SECURITY_OP) &&
			    nseg <= LPFC_EXT_DATA_BDE_COUNT) {
				data_bde->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
				data_bde->tus.f.bdeSize = sg_dma_len(sgel);
				data_bde->addrLow = putPaddrLow(physaddr);
				data_bde->addrHigh = putPaddrHigh(physaddr);
				data_bde++;
			} else {
				bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
				bpl->tus.f.bdeSize = sg_dma_len(sgel);
				bpl->tus.w = le32_to_cpu(bpl->tus.w);
				bpl->addrLow =
					le32_to_cpu(putPaddrLow(physaddr));
				bpl->addrHigh =
					le32_to_cpu(putPaddrHigh(physaddr));
				bpl++;
			}
		}
	}

	/*
	 * Finish initializing those IOCB fields that are dependent on the
	 * scsi_cmnd request_buffer.  Note that for SLI-2 the bdeSize is
	 * explicitly reinitialized and for SLI-3 the extended bde count is
	 * explicitly reinitialized since all iocb memory resources are reused.
	 */
	if (phba->sli_rev == 3 &&
	    !(phba->sli3_options & LPFC_SLI3_BG_ENABLED) &&
	    !(iocbq->iocb_flag & DSS_SECURITY_OP)) {
		if (num_bde > LPFC_EXT_DATA_BDE_COUNT) {
			/*
			 * The extended IOCB format can only fit 3 BDE or a BPL.
			 * This I/O has more than 3 BDE so the 1st data bde will
			 * be a BPL that is filled in here.
			 */
			physaddr = lpfc_cmd->dma_handle;
			data_bde->tus.f.bdeFlags = BUFF_TYPE_BLP_64;
			data_bde->tus.f.bdeSize = (num_bde *
						   sizeof(struct ulp_bde64));
			physaddr += (sizeof(struct fcp_cmnd) +
				     sizeof(struct fcp_rsp) +
				     (2 * sizeof(struct ulp_bde64)));
			data_bde->addrHigh = putPaddrHigh(physaddr);
			data_bde->addrLow = putPaddrLow(physaddr);
			/* ebde count includes the response bde and data bpl */
			iocb_cmd->unsli3.fcp_ext.ebde_count = 2;
		} else {
			/* ebde count includes the response bde and data bdes */
			iocb_cmd->unsli3.fcp_ext.ebde_count = (num_bde + 1);
		}
	} else {
		iocb_cmd->un.fcpi64.bdl.bdeSize =
			((num_bde + 2) * sizeof(struct ulp_bde64));
		iocb_cmd->unsli3.fcp_ext.ebde_count = (num_bde + 1);
	}
	fcp_cmnd->fcpDl = cpu_to_be32(scsi_bufflen(scsi_cmnd));

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = scsi_bufflen(scsi_cmnd);
	return 0;
}

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS

/* Return BG_ERR_INIT if error injection is detected by Initiator */
#define BG_ERR_INIT	0x1
/* Return BG_ERR_TGT if error injection is detected by Target */
#define BG_ERR_TGT	0x2
/* Return BG_ERR_SWAP if swapping CSUM<-->CRC is required for error injection */
#define BG_ERR_SWAP	0x10
/**
 * Return BG_ERR_CHECK if disabling Guard/Ref/App checking is required for
 * error injection
 **/
#define BG_ERR_CHECK	0x20

/**
 * lpfc_bg_err_inject - Determine if we should inject an error
 * @phba: The Hba for which this call is being executed.
 * @sc: The SCSI command to examine
 * @reftag: (out) BlockGuard reference tag for transmitted data
 * @apptag: (out) BlockGuard application tag for transmitted data
 * @new_guard (in) Value to replace CRC with if needed
 *
 * Returns BG_ERR_* bit mask or 0 if request ignored
 **/
static int
lpfc_bg_err_inject(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		uint32_t *reftag, uint16_t *apptag, uint32_t new_guard)
{
	struct scatterlist *sgpe; /* s/g prot entry */
	struct lpfc_io_buf *lpfc_cmd = NULL;
	struct scsi_dif_tuple *src = NULL;
	struct lpfc_nodelist *ndlp;
	struct lpfc_rport_data *rdata;
	uint32_t op = scsi_get_prot_op(sc);
	uint32_t blksize;
	uint32_t numblks;
	sector_t lba;
	int rc = 0;
	int blockoff = 0;

	if (op == SCSI_PROT_NORMAL)
		return 0;

	sgpe = scsi_prot_sglist(sc);
	lba = scsi_get_lba(sc);

	/* First check if we need to match the LBA */
	if (phba->lpfc_injerr_lba != LPFC_INJERR_LBA_OFF) {
		blksize = lpfc_cmd_blksize(sc);
		numblks = (scsi_bufflen(sc) + blksize - 1) / blksize;

		/* Make sure we have the right LBA if one is specified */
		if ((phba->lpfc_injerr_lba < lba) ||
			(phba->lpfc_injerr_lba >= (lba + numblks)))
			return 0;
		if (sgpe) {
			blockoff = phba->lpfc_injerr_lba - lba;
			numblks = sg_dma_len(sgpe) /
				sizeof(struct scsi_dif_tuple);
			if (numblks < blockoff)
				blockoff = numblks;
		}
	}

	/* Next check if we need to match the remote NPortID or WWPN */
	rdata = lpfc_rport_data_from_scsi_device(sc->device);
	if (rdata && rdata->pnode) {
		ndlp = rdata->pnode;

		/* Make sure we have the right NPortID if one is specified */
		if (phba->lpfc_injerr_nportid  &&
			(phba->lpfc_injerr_nportid != ndlp->nlp_DID))
			return 0;

		/*
		 * Make sure we have the right WWPN if one is specified.
		 * wwn[0] should be a non-zero NAA in a good WWPN.
		 */
		if (phba->lpfc_injerr_wwpn.u.wwn[0]  &&
			(memcmp(&ndlp->nlp_portname, &phba->lpfc_injerr_wwpn,
				sizeof(struct lpfc_name)) != 0))
			return 0;
	}

	/* Setup a ptr to the protection data if the SCSI host provides it */
	if (sgpe) {
		src = (struct scsi_dif_tuple *)sg_virt(sgpe);
		src += blockoff;
		lpfc_cmd = (struct lpfc_io_buf *)sc->host_scribble;
	}

	/* Should we change the Reference Tag */
	if (reftag) {
		if (phba->lpfc_injerr_wref_cnt) {
			switch (op) {
			case SCSI_PROT_WRITE_PASS:
				if (src) {
					/*
					 * For WRITE_PASS, force the error
					 * to be sent on the wire. It should
					 * be detected by the Target.
					 * If blockoff != 0 error will be
					 * inserted in middle of the IO.
					 */

					lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"9076 BLKGRD: Injecting reftag error: "
					"write lba x%lx + x%x oldrefTag x%x\n",
					(unsigned long)lba, blockoff,
					be32_to_cpu(src->ref_tag));

					/*
					 * Save the old ref_tag so we can
					 * restore it on completion.
					 */
					if (lpfc_cmd) {
						lpfc_cmd->prot_data_type =
							LPFC_INJERR_REFTAG;
						lpfc_cmd->prot_data_segment =
							src;
						lpfc_cmd->prot_data =
							src->ref_tag;
					}
					src->ref_tag = cpu_to_be32(0xDEADBEEF);
					phba->lpfc_injerr_wref_cnt--;
					if (phba->lpfc_injerr_wref_cnt == 0) {
						phba->lpfc_injerr_nportid = 0;
						phba->lpfc_injerr_lba =
							LPFC_INJERR_LBA_OFF;
						memset(&phba->lpfc_injerr_wwpn,
						  0, sizeof(struct lpfc_name));
					}
					rc = BG_ERR_TGT | BG_ERR_CHECK;

					break;
				}
				/* fall through */
			case SCSI_PROT_WRITE_INSERT:
				/*
				 * For WRITE_INSERT, force the error
				 * to be sent on the wire. It should be
				 * detected by the Target.
				 */
				/* DEADBEEF will be the reftag on the wire */
				*reftag = 0xDEADBEEF;
				phba->lpfc_injerr_wref_cnt--;
				if (phba->lpfc_injerr_wref_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
					LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_TGT | BG_ERR_CHECK;

				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"9078 BLKGRD: Injecting reftag error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			case SCSI_PROT_WRITE_STRIP:
				/*
				 * For WRITE_STRIP and WRITE_PASS,
				 * force the error on data
				 * being copied from SLI-Host to SLI-Port.
				 */
				*reftag = 0xDEADBEEF;
				phba->lpfc_injerr_wref_cnt--;
				if (phba->lpfc_injerr_wref_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_INIT;

				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"9077 BLKGRD: Injecting reftag error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
		if (phba->lpfc_injerr_rref_cnt) {
			switch (op) {
			case SCSI_PROT_READ_INSERT:
			case SCSI_PROT_READ_STRIP:
			case SCSI_PROT_READ_PASS:
				/*
				 * For READ_STRIP and READ_PASS, force the
				 * error on data being read off the wire. It
				 * should force an IO error to the driver.
				 */
				*reftag = 0xDEADBEEF;
				phba->lpfc_injerr_rref_cnt--;
				if (phba->lpfc_injerr_rref_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_INIT;

				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"9079 BLKGRD: Injecting reftag error: "
					"read lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
	}

	/* Should we change the Application Tag */
	if (apptag) {
		if (phba->lpfc_injerr_wapp_cnt) {
			switch (op) {
			case SCSI_PROT_WRITE_PASS:
				if (src) {
					/*
					 * For WRITE_PASS, force the error
					 * to be sent on the wire. It should
					 * be detected by the Target.
					 * If blockoff != 0 error will be
					 * inserted in middle of the IO.
					 */

					lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"9080 BLKGRD: Injecting apptag error: "
					"write lba x%lx + x%x oldappTag x%x\n",
					(unsigned long)lba, blockoff,
					be16_to_cpu(src->app_tag));

					/*
					 * Save the old app_tag so we can
					 * restore it on completion.
					 */
					if (lpfc_cmd) {
						lpfc_cmd->prot_data_type =
							LPFC_INJERR_APPTAG;
						lpfc_cmd->prot_data_segment =
							src;
						lpfc_cmd->prot_data =
							src->app_tag;
					}
					src->app_tag = cpu_to_be16(0xDEAD);
					phba->lpfc_injerr_wapp_cnt--;
					if (phba->lpfc_injerr_wapp_cnt == 0) {
						phba->lpfc_injerr_nportid = 0;
						phba->lpfc_injerr_lba =
							LPFC_INJERR_LBA_OFF;
						memset(&phba->lpfc_injerr_wwpn,
						  0, sizeof(struct lpfc_name));
					}
					rc = BG_ERR_TGT | BG_ERR_CHECK;
					break;
				}
				/* fall through */
			case SCSI_PROT_WRITE_INSERT:
				/*
				 * For WRITE_INSERT, force the
				 * error to be sent on the wire. It should be
				 * detected by the Target.
				 */
				/* DEAD will be the apptag on the wire */
				*apptag = 0xDEAD;
				phba->lpfc_injerr_wapp_cnt--;
				if (phba->lpfc_injerr_wapp_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_TGT | BG_ERR_CHECK;

				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"0813 BLKGRD: Injecting apptag error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			case SCSI_PROT_WRITE_STRIP:
				/*
				 * For WRITE_STRIP and WRITE_PASS,
				 * force the error on data
				 * being copied from SLI-Host to SLI-Port.
				 */
				*apptag = 0xDEAD;
				phba->lpfc_injerr_wapp_cnt--;
				if (phba->lpfc_injerr_wapp_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_INIT;

				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"0812 BLKGRD: Injecting apptag error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
		if (phba->lpfc_injerr_rapp_cnt) {
			switch (op) {
			case SCSI_PROT_READ_INSERT:
			case SCSI_PROT_READ_STRIP:
			case SCSI_PROT_READ_PASS:
				/*
				 * For READ_STRIP and READ_PASS, force the
				 * error on data being read off the wire. It
				 * should force an IO error to the driver.
				 */
				*apptag = 0xDEAD;
				phba->lpfc_injerr_rapp_cnt--;
				if (phba->lpfc_injerr_rapp_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_INIT;

				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"0814 BLKGRD: Injecting apptag error: "
					"read lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
	}


	/* Should we change the Guard Tag */
	if (new_guard) {
		if (phba->lpfc_injerr_wgrd_cnt) {
			switch (op) {
			case SCSI_PROT_WRITE_PASS:
				rc = BG_ERR_CHECK;
				/* fall through */

			case SCSI_PROT_WRITE_INSERT:
				/*
				 * For WRITE_INSERT, force the
				 * error to be sent on the wire. It should be
				 * detected by the Target.
				 */
				phba->lpfc_injerr_wgrd_cnt--;
				if (phba->lpfc_injerr_wgrd_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}

				rc |= BG_ERR_TGT | BG_ERR_SWAP;
				/* Signals the caller to swap CRC->CSUM */

				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"0817 BLKGRD: Injecting guard error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			case SCSI_PROT_WRITE_STRIP:
				/*
				 * For WRITE_STRIP and WRITE_PASS,
				 * force the error on data
				 * being copied from SLI-Host to SLI-Port.
				 */
				phba->lpfc_injerr_wgrd_cnt--;
				if (phba->lpfc_injerr_wgrd_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}

				rc = BG_ERR_INIT | BG_ERR_SWAP;
				/* Signals the caller to swap CRC->CSUM */

				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"0816 BLKGRD: Injecting guard error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
		if (phba->lpfc_injerr_rgrd_cnt) {
			switch (op) {
			case SCSI_PROT_READ_INSERT:
			case SCSI_PROT_READ_STRIP:
			case SCSI_PROT_READ_PASS:
				/*
				 * For READ_STRIP and READ_PASS, force the
				 * error on data being read off the wire. It
				 * should force an IO error to the driver.
				 */
				phba->lpfc_injerr_rgrd_cnt--;
				if (phba->lpfc_injerr_rgrd_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}

				rc = BG_ERR_INIT | BG_ERR_SWAP;
				/* Signals the caller to swap CRC->CSUM */

				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"0818 BLKGRD: Injecting guard error: "
					"read lba x%lx\n", (unsigned long)lba);
			}
		}
	}

	return rc;
}
#endif

/**
 * lpfc_sc_to_bg_opcodes - Determine the BlockGuard opcodes to be used with
 * the specified SCSI command.
 * @phba: The Hba for which this call is being executed.
 * @sc: The SCSI command to examine
 * @txopt: (out) BlockGuard operation for transmitted data
 * @rxopt: (out) BlockGuard operation for received data
 *
 * Returns: zero on success; non-zero if tx and/or rx op cannot be determined
 *
 **/
static int
lpfc_sc_to_bg_opcodes(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		uint8_t *txop, uint8_t *rxop)
{
	uint8_t ret = 0;

	if (lpfc_cmd_guard_csum(sc)) {
		switch (scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			*rxop = BG_OP_IN_NODIF_OUT_CSUM;
			*txop = BG_OP_IN_CSUM_OUT_NODIF;
			break;

		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			*rxop = BG_OP_IN_CRC_OUT_NODIF;
			*txop = BG_OP_IN_NODIF_OUT_CRC;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			*rxop = BG_OP_IN_CRC_OUT_CSUM;
			*txop = BG_OP_IN_CSUM_OUT_CRC;
			break;

		case SCSI_PROT_NORMAL:
		default:
			lpfc_printf_log(phba, KERN_ERR, LOG_BG,
				"9063 BLKGRD: Bad op/guard:%d/IP combination\n",
					scsi_get_prot_op(sc));
			ret = 1;
			break;

		}
	} else {
		switch (scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			*rxop = BG_OP_IN_CRC_OUT_NODIF;
			*txop = BG_OP_IN_NODIF_OUT_CRC;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			*rxop = BG_OP_IN_CRC_OUT_CRC;
			*txop = BG_OP_IN_CRC_OUT_CRC;
			break;

		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			*rxop = BG_OP_IN_NODIF_OUT_CRC;
			*txop = BG_OP_IN_CRC_OUT_NODIF;
			break;

		case SCSI_PROT_NORMAL:
		default:
			lpfc_printf_log(phba, KERN_ERR, LOG_BG,
				"9075 BLKGRD: Bad op/guard:%d/CRC combination\n",
					scsi_get_prot_op(sc));
			ret = 1;
			break;
		}
	}

	return ret;
}

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
/**
 * lpfc_bg_err_opcodes - reDetermine the BlockGuard opcodes to be used with
 * the specified SCSI command in order to force a guard tag error.
 * @phba: The Hba for which this call is being executed.
 * @sc: The SCSI command to examine
 * @txopt: (out) BlockGuard operation for transmitted data
 * @rxopt: (out) BlockGuard operation for received data
 *
 * Returns: zero on success; non-zero if tx and/or rx op cannot be determined
 *
 **/
static int
lpfc_bg_err_opcodes(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		uint8_t *txop, uint8_t *rxop)
{
	uint8_t ret = 0;

	if (lpfc_cmd_guard_csum(sc)) {
		switch (scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			*rxop = BG_OP_IN_NODIF_OUT_CRC;
			*txop = BG_OP_IN_CRC_OUT_NODIF;
			break;

		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			*rxop = BG_OP_IN_CSUM_OUT_NODIF;
			*txop = BG_OP_IN_NODIF_OUT_CSUM;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			*rxop = BG_OP_IN_CSUM_OUT_CRC;
			*txop = BG_OP_IN_CRC_OUT_CSUM;
			break;

		case SCSI_PROT_NORMAL:
		default:
			break;

		}
	} else {
		switch (scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			*rxop = BG_OP_IN_CSUM_OUT_NODIF;
			*txop = BG_OP_IN_NODIF_OUT_CSUM;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			*rxop = BG_OP_IN_CSUM_OUT_CSUM;
			*txop = BG_OP_IN_CSUM_OUT_CSUM;
			break;

		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			*rxop = BG_OP_IN_NODIF_OUT_CSUM;
			*txop = BG_OP_IN_CSUM_OUT_NODIF;
			break;

		case SCSI_PROT_NORMAL:
		default:
			break;
		}
	}

	return ret;
}
#endif

/**
 * lpfc_bg_setup_bpl - Setup BlockGuard BPL with no protection data
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 * @bpl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 *
 * This function sets up BPL buffer list for protection groups of
 * type LPFC_PG_TYPE_NO_DIF
 *
 * This is usually used when the HBA is instructed to generate
 * DIFs and insert them into data stream (or strip DIF from
 * incoming data stream)
 *
 * The buffer list consists of just one protection group described
 * below:
 *                                +-------------------------+
 *   start of prot group  -->     |          PDE_5          |
 *                                +-------------------------+
 *                                |          PDE_6          |
 *                                +-------------------------+
 *                                |         Data BDE        |
 *                                +-------------------------+
 *                                |more Data BDE's ... (opt)|
 *                                +-------------------------+
 *
 *
 * Note: Data s/g buffers have been dma mapped
 *
 * Returns the number of BDEs added to the BPL.
 **/
static int
lpfc_bg_setup_bpl(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct ulp_bde64 *bpl, int datasegcnt)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct lpfc_pde5 *pde5 = NULL;
	struct lpfc_pde6 *pde6 = NULL;
	dma_addr_t physaddr;
	int i = 0, num_bde = 0, status;
	int datadir = sc->sc_data_direction;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t rc;
#endif
	uint32_t checking = 1;
	uint32_t reftag;
	uint8_t txop, rxop;

	status  = lpfc_sc_to_bg_opcodes(phba, sc, &txop, &rxop);
	if (status)
		goto out;

	/* extract some info from the scsi command for pde*/
	reftag = (uint32_t)scsi_get_lba(sc); /* Truncate LBA */

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	rc = lpfc_bg_err_inject(phba, sc, &reftag, NULL, 1);
	if (rc) {
		if (rc & BG_ERR_SWAP)
			lpfc_bg_err_opcodes(phba, sc, &txop, &rxop);
		if (rc & BG_ERR_CHECK)
			checking = 0;
	}
#endif

	/* setup PDE5 with what we have */
	pde5 = (struct lpfc_pde5 *) bpl;
	memset(pde5, 0, sizeof(struct lpfc_pde5));
	bf_set(pde5_type, pde5, LPFC_PDE5_DESCRIPTOR);

	/* Endianness conversion if necessary for PDE5 */
	pde5->word0 = cpu_to_le32(pde5->word0);
	pde5->reftag = cpu_to_le32(reftag);

	/* advance bpl and increment bde count */
	num_bde++;
	bpl++;
	pde6 = (struct lpfc_pde6 *) bpl;

	/* setup PDE6 with the rest of the info */
	memset(pde6, 0, sizeof(struct lpfc_pde6));
	bf_set(pde6_type, pde6, LPFC_PDE6_DESCRIPTOR);
	bf_set(pde6_optx, pde6, txop);
	bf_set(pde6_oprx, pde6, rxop);

	/*
	 * We only need to check the data on READs, for WRITEs
	 * protection data is automatically generated, not checked.
	 */
	if (datadir == DMA_FROM_DEVICE) {
		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_GUARD))
			bf_set(pde6_ce, pde6, checking);
		else
			bf_set(pde6_ce, pde6, 0);

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_REF))
			bf_set(pde6_re, pde6, checking);
		else
			bf_set(pde6_re, pde6, 0);
	}
	bf_set(pde6_ai, pde6, 1);
	bf_set(pde6_ae, pde6, 0);
	bf_set(pde6_apptagval, pde6, 0);

	/* Endianness conversion if necessary for PDE6 */
	pde6->word0 = cpu_to_le32(pde6->word0);
	pde6->word1 = cpu_to_le32(pde6->word1);
	pde6->word2 = cpu_to_le32(pde6->word2);

	/* advance bpl and increment bde count */
	num_bde++;
	bpl++;

	/* assumption: caller has already run dma_map_sg on command data */
	scsi_for_each_sg(sc, sgde, datasegcnt, i) {
		physaddr = sg_dma_address(sgde);
		bpl->addrLow = le32_to_cpu(putPaddrLow(physaddr));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(physaddr));
		bpl->tus.f.bdeSize = sg_dma_len(sgde);
		if (datadir == DMA_TO_DEVICE)
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		else
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;
		num_bde++;
	}

out:
	return num_bde;
}

/**
 * lpfc_bg_setup_bpl_prot - Setup BlockGuard BPL with protection data
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 * @bpl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 * @protcnt: number of segment of protection data that have been dma mapped
 *
 * This function sets up BPL buffer list for protection groups of
 * type LPFC_PG_TYPE_DIF
 *
 * This is usually used when DIFs are in their own buffers,
 * separate from the data. The HBA can then by instructed
 * to place the DIFs in the outgoing stream.  For read operations,
 * The HBA could extract the DIFs and place it in DIF buffers.
 *
 * The buffer list for this type consists of one or more of the
 * protection groups described below:
 *                                    +-------------------------+
 *   start of first prot group  -->   |          PDE_5          |
 *                                    +-------------------------+
 *                                    |          PDE_6          |
 *                                    +-------------------------+
 *                                    |      PDE_7 (Prot BDE)   |
 *                                    +-------------------------+
 *                                    |        Data BDE         |
 *                                    +-------------------------+
 *                                    |more Data BDE's ... (opt)|
 *                                    +-------------------------+
 *   start of new  prot group  -->    |          PDE_5          |
 *                                    +-------------------------+
 *                                    |          ...            |
 *                                    +-------------------------+
 *
 * Note: It is assumed that both data and protection s/g buffers have been
 *       mapped for DMA
 *
 * Returns the number of BDEs added to the BPL.
 **/
static int
lpfc_bg_setup_bpl_prot(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct ulp_bde64 *bpl, int datacnt, int protcnt)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct scatterlist *sgpe = NULL; /* s/g prot entry */
	struct lpfc_pde5 *pde5 = NULL;
	struct lpfc_pde6 *pde6 = NULL;
	struct lpfc_pde7 *pde7 = NULL;
	dma_addr_t dataphysaddr, protphysaddr;
	unsigned short curr_data = 0, curr_prot = 0;
	unsigned int split_offset;
	unsigned int protgroup_len, protgroup_offset = 0, protgroup_remainder;
	unsigned int protgrp_blks, protgrp_bytes;
	unsigned int remainder, subtotal;
	int status;
	int datadir = sc->sc_data_direction;
	unsigned char pgdone = 0, alldone = 0;
	unsigned blksize;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t rc;
#endif
	uint32_t checking = 1;
	uint32_t reftag;
	uint8_t txop, rxop;
	int num_bde = 0;

	sgpe = scsi_prot_sglist(sc);
	sgde = scsi_sglist(sc);

	if (!sgpe || !sgde) {
		lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
				"9020 Invalid s/g entry: data=x%px prot=x%px\n",
				sgpe, sgde);
		return 0;
	}

	status = lpfc_sc_to_bg_opcodes(phba, sc, &txop, &rxop);
	if (status)
		goto out;

	/* extract some info from the scsi command */
	blksize = lpfc_cmd_blksize(sc);
	reftag = (uint32_t)scsi_get_lba(sc); /* Truncate LBA */

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	rc = lpfc_bg_err_inject(phba, sc, &reftag, NULL, 1);
	if (rc) {
		if (rc & BG_ERR_SWAP)
			lpfc_bg_err_opcodes(phba, sc, &txop, &rxop);
		if (rc & BG_ERR_CHECK)
			checking = 0;
	}
#endif

	split_offset = 0;
	do {
		/* Check to see if we ran out of space */
		if (num_bde >= (phba->cfg_total_seg_cnt - 2))
			return num_bde + 3;

		/* setup PDE5 with what we have */
		pde5 = (struct lpfc_pde5 *) bpl;
		memset(pde5, 0, sizeof(struct lpfc_pde5));
		bf_set(pde5_type, pde5, LPFC_PDE5_DESCRIPTOR);

		/* Endianness conversion if necessary for PDE5 */
		pde5->word0 = cpu_to_le32(pde5->word0);
		pde5->reftag = cpu_to_le32(reftag);

		/* advance bpl and increment bde count */
		num_bde++;
		bpl++;
		pde6 = (struct lpfc_pde6 *) bpl;

		/* setup PDE6 with the rest of the info */
		memset(pde6, 0, sizeof(struct lpfc_pde6));
		bf_set(pde6_type, pde6, LPFC_PDE6_DESCRIPTOR);
		bf_set(pde6_optx, pde6, txop);
		bf_set(pde6_oprx, pde6, rxop);

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_GUARD))
			bf_set(pde6_ce, pde6, checking);
		else
			bf_set(pde6_ce, pde6, 0);

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_REF))
			bf_set(pde6_re, pde6, checking);
		else
			bf_set(pde6_re, pde6, 0);

		bf_set(pde6_ai, pde6, 1);
		bf_set(pde6_ae, pde6, 0);
		bf_set(pde6_apptagval, pde6, 0);

		/* Endianness conversion if necessary for PDE6 */
		pde6->word0 = cpu_to_le32(pde6->word0);
		pde6->word1 = cpu_to_le32(pde6->word1);
		pde6->word2 = cpu_to_le32(pde6->word2);

		/* advance bpl and increment bde count */
		num_bde++;
		bpl++;

		/* setup the first BDE that points to protection buffer */
		protphysaddr = sg_dma_address(sgpe) + protgroup_offset;
		protgroup_len = sg_dma_len(sgpe) - protgroup_offset;

		/* must be integer multiple of the DIF block length */
		BUG_ON(protgroup_len % 8);

		pde7 = (struct lpfc_pde7 *) bpl;
		memset(pde7, 0, sizeof(struct lpfc_pde7));
		bf_set(pde7_type, pde7, LPFC_PDE7_DESCRIPTOR);

		pde7->addrHigh = le32_to_cpu(putPaddrHigh(protphysaddr));
		pde7->addrLow = le32_to_cpu(putPaddrLow(protphysaddr));

		protgrp_blks = protgroup_len / 8;
		protgrp_bytes = protgrp_blks * blksize;

		/* check if this pde is crossing the 4K boundary; if so split */
		if ((pde7->addrLow & 0xfff) + protgroup_len > 0x1000) {
			protgroup_remainder = 0x1000 - (pde7->addrLow & 0xfff);
			protgroup_offset += protgroup_remainder;
			protgrp_blks = protgroup_remainder / 8;
			protgrp_bytes = protgrp_blks * blksize;
		} else {
			protgroup_offset = 0;
			curr_prot++;
		}

		num_bde++;

		/* setup BDE's for data blocks associated with DIF data */
		pgdone = 0;
		subtotal = 0; /* total bytes processed for current prot grp */
		while (!pgdone) {
			/* Check to see if we ran out of space */
			if (num_bde >= phba->cfg_total_seg_cnt)
				return num_bde + 1;

			if (!sgde) {
				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"9065 BLKGRD:%s Invalid data segment\n",
						__func__);
				return 0;
			}
			bpl++;
			dataphysaddr = sg_dma_address(sgde) + split_offset;
			bpl->addrLow = le32_to_cpu(putPaddrLow(dataphysaddr));
			bpl->addrHigh = le32_to_cpu(putPaddrHigh(dataphysaddr));

			remainder = sg_dma_len(sgde) - split_offset;

			if ((subtotal + remainder) <= protgrp_bytes) {
				/* we can use this whole buffer */
				bpl->tus.f.bdeSize = remainder;
				split_offset = 0;

				if ((subtotal + remainder) == protgrp_bytes)
					pgdone = 1;
			} else {
				/* must split this buffer with next prot grp */
				bpl->tus.f.bdeSize = protgrp_bytes - subtotal;
				split_offset += bpl->tus.f.bdeSize;
			}

			subtotal += bpl->tus.f.bdeSize;

			if (datadir == DMA_TO_DEVICE)
				bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
			else
				bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
			bpl->tus.w = le32_to_cpu(bpl->tus.w);

			num_bde++;
			curr_data++;

			if (split_offset)
				break;

			/* Move to the next s/g segment if possible */
			sgde = sg_next(sgde);

		}

		if (protgroup_offset) {
			/* update the reference tag */
			reftag += protgrp_blks;
			bpl++;
			continue;
		}

		/* are we done ? */
		if (curr_prot == protcnt) {
			alldone = 1;
		} else if (curr_prot < protcnt) {
			/* advance to next prot buffer */
			sgpe = sg_next(sgpe);
			bpl++;

			/* update the reference tag */
			reftag += protgrp_blks;
		} else {
			/* if we're here, we have a bug */
			lpfc_printf_log(phba, KERN_ERR, LOG_BG,
				"9054 BLKGRD: bug in %s\n", __func__);
		}

	} while (!alldone);
out:

	return num_bde;
}

/**
 * lpfc_bg_setup_sgl - Setup BlockGuard SGL with no protection data
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 * @sgl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 *
 * This function sets up SGL buffer list for protection groups of
 * type LPFC_PG_TYPE_NO_DIF
 *
 * This is usually used when the HBA is instructed to generate
 * DIFs and insert them into data stream (or strip DIF from
 * incoming data stream)
 *
 * The buffer list consists of just one protection group described
 * below:
 *                                +-------------------------+
 *   start of prot group  -->     |         DI_SEED         |
 *                                +-------------------------+
 *                                |         Data SGE        |
 *                                +-------------------------+
 *                                |more Data SGE's ... (opt)|
 *                                +-------------------------+
 *
 *
 * Note: Data s/g buffers have been dma mapped
 *
 * Returns the number of SGEs added to the SGL.
 **/
static int
lpfc_bg_setup_sgl(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct sli4_sge *sgl, int datasegcnt,
		struct lpfc_io_buf *lpfc_cmd)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct sli4_sge_diseed *diseed = NULL;
	dma_addr_t physaddr;
	int i = 0, num_sge = 0, status;
	uint32_t reftag;
	uint8_t txop, rxop;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t rc;
#endif
	uint32_t checking = 1;
	uint32_t dma_len;
	uint32_t dma_offset = 0;
	struct sli4_hybrid_sgl *sgl_xtra = NULL;
	int j;
	bool lsp_just_set = false;

	status  = lpfc_sc_to_bg_opcodes(phba, sc, &txop, &rxop);
	if (status)
		goto out;

	/* extract some info from the scsi command for pde*/
	reftag = (uint32_t)scsi_get_lba(sc); /* Truncate LBA */

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	rc = lpfc_bg_err_inject(phba, sc, &reftag, NULL, 1);
	if (rc) {
		if (rc & BG_ERR_SWAP)
			lpfc_bg_err_opcodes(phba, sc, &txop, &rxop);
		if (rc & BG_ERR_CHECK)
			checking = 0;
	}
#endif

	/* setup DISEED with what we have */
	diseed = (struct sli4_sge_diseed *) sgl;
	memset(diseed, 0, sizeof(struct sli4_sge_diseed));
	bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DISEED);

	/* Endianness conversion if necessary */
	diseed->ref_tag = cpu_to_le32(reftag);
	diseed->ref_tag_tran = diseed->ref_tag;

	/*
	 * We only need to check the data on READs, for WRITEs
	 * protection data is automatically generated, not checked.
	 */
	if (sc->sc_data_direction == DMA_FROM_DEVICE) {
		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_GUARD))
			bf_set(lpfc_sli4_sge_dif_ce, diseed, checking);
		else
			bf_set(lpfc_sli4_sge_dif_ce, diseed, 0);

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_REF))
			bf_set(lpfc_sli4_sge_dif_re, diseed, checking);
		else
			bf_set(lpfc_sli4_sge_dif_re, diseed, 0);
	}

	/* setup DISEED with the rest of the info */
	bf_set(lpfc_sli4_sge_dif_optx, diseed, txop);
	bf_set(lpfc_sli4_sge_dif_oprx, diseed, rxop);

	bf_set(lpfc_sli4_sge_dif_ai, diseed, 1);
	bf_set(lpfc_sli4_sge_dif_me, diseed, 0);

	/* Endianness conversion if necessary for DISEED */
	diseed->word2 = cpu_to_le32(diseed->word2);
	diseed->word3 = cpu_to_le32(diseed->word3);

	/* advance bpl and increment sge count */
	num_sge++;
	sgl++;

	/* assumption: caller has already run dma_map_sg on command data */
	sgde = scsi_sglist(sc);
	j = 3;
	for (i = 0; i < datasegcnt; i++) {
		/* clear it */
		sgl->word2 = 0;

		/* do we need to expand the segment */
		if (!lsp_just_set && !((j + 1) % phba->border_sge_num) &&
		    ((datasegcnt - 1) != i)) {
			/* set LSP type */
			bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_LSP);

			sgl_xtra = lpfc_get_sgl_per_hdwq(phba, lpfc_cmd);

			if (unlikely(!sgl_xtra)) {
				lpfc_cmd->seg_cnt = 0;
				return 0;
			}
			sgl->addr_lo = cpu_to_le32(putPaddrLow(
						sgl_xtra->dma_phys_sgl));
			sgl->addr_hi = cpu_to_le32(putPaddrHigh(
						sgl_xtra->dma_phys_sgl));

		} else {
			bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DATA);
		}

		if (!(bf_get(lpfc_sli4_sge_type, sgl) & LPFC_SGE_TYPE_LSP)) {
			if ((datasegcnt - 1) == i)
				bf_set(lpfc_sli4_sge_last, sgl, 1);
			physaddr = sg_dma_address(sgde);
			dma_len = sg_dma_len(sgde);
			sgl->addr_lo = cpu_to_le32(putPaddrLow(physaddr));
			sgl->addr_hi = cpu_to_le32(putPaddrHigh(physaddr));

			bf_set(lpfc_sli4_sge_offset, sgl, dma_offset);
			sgl->word2 = cpu_to_le32(sgl->word2);
			sgl->sge_len = cpu_to_le32(dma_len);

			dma_offset += dma_len;
			sgde = sg_next(sgde);

			sgl++;
			num_sge++;
			lsp_just_set = false;

		} else {
			sgl->word2 = cpu_to_le32(sgl->word2);
			sgl->sge_len = cpu_to_le32(phba->cfg_sg_dma_buf_size);

			sgl = (struct sli4_sge *)sgl_xtra->dma_sgl;
			i = i - 1;

			lsp_just_set = true;
		}

		j++;

	}

out:
	return num_sge;
}

/**
 * lpfc_bg_setup_sgl_prot - Setup BlockGuard SGL with protection data
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 * @sgl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 * @protcnt: number of segment of protection data that have been dma mapped
 *
 * This function sets up SGL buffer list for protection groups of
 * type LPFC_PG_TYPE_DIF
 *
 * This is usually used when DIFs are in their own buffers,
 * separate from the data. The HBA can then by instructed
 * to place the DIFs in the outgoing stream.  For read operations,
 * The HBA could extract the DIFs and place it in DIF buffers.
 *
 * The buffer list for this type consists of one or more of the
 * protection groups described below:
 *                                    +-------------------------+
 *   start of first prot group  -->   |         DISEED          |
 *                                    +-------------------------+
 *                                    |      DIF (Prot SGE)     |
 *                                    +-------------------------+
 *                                    |        Data SGE         |
 *                                    +-------------------------+
 *                                    |more Data SGE's ... (opt)|
 *                                    +-------------------------+
 *   start of new  prot group  -->    |         DISEED          |
 *                                    +-------------------------+
 *                                    |          ...            |
 *                                    +-------------------------+
 *
 * Note: It is assumed that both data and protection s/g buffers have been
 *       mapped for DMA
 *
 * Returns the number of SGEs added to the SGL.
 **/
static int
lpfc_bg_setup_sgl_prot(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct sli4_sge *sgl, int datacnt, int protcnt,
		struct lpfc_io_buf *lpfc_cmd)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct scatterlist *sgpe = NULL; /* s/g prot entry */
	struct sli4_sge_diseed *diseed = NULL;
	dma_addr_t dataphysaddr, protphysaddr;
	unsigned short curr_data = 0, curr_prot = 0;
	unsigned int split_offset;
	unsigned int protgroup_len, protgroup_offset = 0, protgroup_remainder;
	unsigned int protgrp_blks, protgrp_bytes;
	unsigned int remainder, subtotal;
	int status;
	unsigned char pgdone = 0, alldone = 0;
	unsigned blksize;
	uint32_t reftag;
	uint8_t txop, rxop;
	uint32_t dma_len;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t rc;
#endif
	uint32_t checking = 1;
	uint32_t dma_offset = 0;
	int num_sge = 0, j = 2;
	struct sli4_hybrid_sgl *sgl_xtra = NULL;

	sgpe = scsi_prot_sglist(sc);
	sgde = scsi_sglist(sc);

	if (!sgpe || !sgde) {
		lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
				"9082 Invalid s/g entry: data=x%px prot=x%px\n",
				sgpe, sgde);
		return 0;
	}

	status = lpfc_sc_to_bg_opcodes(phba, sc, &txop, &rxop);
	if (status)
		goto out;

	/* extract some info from the scsi command */
	blksize = lpfc_cmd_blksize(sc);
	reftag = (uint32_t)scsi_get_lba(sc); /* Truncate LBA */

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	rc = lpfc_bg_err_inject(phba, sc, &reftag, NULL, 1);
	if (rc) {
		if (rc & BG_ERR_SWAP)
			lpfc_bg_err_opcodes(phba, sc, &txop, &rxop);
		if (rc & BG_ERR_CHECK)
			checking = 0;
	}
#endif

	split_offset = 0;
	do {
		/* Check to see if we ran out of space */
		if ((num_sge >= (phba->cfg_total_seg_cnt - 2)) &&
		    !(phba->cfg_xpsgl))
			return num_sge + 3;

		/* DISEED and DIF have to be together */
		if (!((j + 1) % phba->border_sge_num) ||
		    !((j + 2) % phba->border_sge_num) ||
		    !((j + 3) % phba->border_sge_num)) {
			sgl->word2 = 0;

			/* set LSP type */
			bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_LSP);

			sgl_xtra = lpfc_get_sgl_per_hdwq(phba, lpfc_cmd);

			if (unlikely(!sgl_xtra)) {
				goto out;
			} else {
				sgl->addr_lo = cpu_to_le32(putPaddrLow(
						sgl_xtra->dma_phys_sgl));
				sgl->addr_hi = cpu_to_le32(putPaddrHigh(
						       sgl_xtra->dma_phys_sgl));
			}

			sgl->word2 = cpu_to_le32(sgl->word2);
			sgl->sge_len = cpu_to_le32(phba->cfg_sg_dma_buf_size);

			sgl = (struct sli4_sge *)sgl_xtra->dma_sgl;
			j = 0;
		}

		/* setup DISEED with what we have */
		diseed = (struct sli4_sge_diseed *) sgl;
		memset(diseed, 0, sizeof(struct sli4_sge_diseed));
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DISEED);

		/* Endianness conversion if necessary */
		diseed->ref_tag = cpu_to_le32(reftag);
		diseed->ref_tag_tran = diseed->ref_tag;

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_GUARD)) {
			bf_set(lpfc_sli4_sge_dif_ce, diseed, checking);

		} else {
			bf_set(lpfc_sli4_sge_dif_ce, diseed, 0);
			/*
			 * When in this mode, the hardware will replace
			 * the guard tag from the host with a
			 * newly generated good CRC for the wire.
			 * Switch to raw mode here to avoid this
			 * behavior. What the host sends gets put on the wire.
			 */
			if (txop == BG_OP_IN_CRC_OUT_CRC) {
				txop = BG_OP_RAW_MODE;
				rxop = BG_OP_RAW_MODE;
			}
		}


		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_REF))
			bf_set(lpfc_sli4_sge_dif_re, diseed, checking);
		else
			bf_set(lpfc_sli4_sge_dif_re, diseed, 0);

		/* setup DISEED with the rest of the info */
		bf_set(lpfc_sli4_sge_dif_optx, diseed, txop);
		bf_set(lpfc_sli4_sge_dif_oprx, diseed, rxop);

		bf_set(lpfc_sli4_sge_dif_ai, diseed, 1);
		bf_set(lpfc_sli4_sge_dif_me, diseed, 0);

		/* Endianness conversion if necessary for DISEED */
		diseed->word2 = cpu_to_le32(diseed->word2);
		diseed->word3 = cpu_to_le32(diseed->word3);

		/* advance sgl and increment bde count */
		num_sge++;

		sgl++;
		j++;

		/* setup the first BDE that points to protection buffer */
		protphysaddr = sg_dma_address(sgpe) + protgroup_offset;
		protgroup_len = sg_dma_len(sgpe) - protgroup_offset;

		/* must be integer multiple of the DIF block length */
		BUG_ON(protgroup_len % 8);

		/* Now setup DIF SGE */
		sgl->word2 = 0;
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DIF);
		sgl->addr_hi = le32_to_cpu(putPaddrHigh(protphysaddr));
		sgl->addr_lo = le32_to_cpu(putPaddrLow(protphysaddr));
		sgl->word2 = cpu_to_le32(sgl->word2);
		sgl->sge_len = 0;

		protgrp_blks = protgroup_len / 8;
		protgrp_bytes = protgrp_blks * blksize;

		/* check if DIF SGE is crossing the 4K boundary; if so split */
		if ((sgl->addr_lo & 0xfff) + protgroup_len > 0x1000) {
			protgroup_remainder = 0x1000 - (sgl->addr_lo & 0xfff);
			protgroup_offset += protgroup_remainder;
			protgrp_blks = protgroup_remainder / 8;
			protgrp_bytes = protgrp_blks * blksize;
		} else {
			protgroup_offset = 0;
			curr_prot++;
		}

		num_sge++;

		/* setup SGE's for data blocks associated with DIF data */
		pgdone = 0;
		subtotal = 0; /* total bytes processed for current prot grp */

		sgl++;
		j++;

		while (!pgdone) {
			/* Check to see if we ran out of space */
			if ((num_sge >= phba->cfg_total_seg_cnt) &&
			    !phba->cfg_xpsgl)
				return num_sge + 1;

			if (!sgde) {
				lpfc_printf_log(phba, KERN_ERR, LOG_BG,
					"9086 BLKGRD:%s Invalid data segment\n",
						__func__);
				return 0;
			}

			if (!((j + 1) % phba->border_sge_num)) {
				sgl->word2 = 0;

				/* set LSP type */
				bf_set(lpfc_sli4_sge_type, sgl,
				       LPFC_SGE_TYPE_LSP);

				sgl_xtra = lpfc_get_sgl_per_hdwq(phba,
								 lpfc_cmd);

				if (unlikely(!sgl_xtra)) {
					goto out;
				} else {
					sgl->addr_lo = cpu_to_le32(
					  putPaddrLow(sgl_xtra->dma_phys_sgl));
					sgl->addr_hi = cpu_to_le32(
					  putPaddrHigh(sgl_xtra->dma_phys_sgl));
				}

				sgl->word2 = cpu_to_le32(sgl->word2);
				sgl->sge_len = cpu_to_le32(
						     phba->cfg_sg_dma_buf_size);

				sgl = (struct sli4_sge *)sgl_xtra->dma_sgl;
			} else {
				dataphysaddr = sg_dma_address(sgde) +
								   split_offset;

				remainder = sg_dma_len(sgde) - split_offset;

				if ((subtotal + remainder) <= protgrp_bytes) {
					/* we can use this whole buffer */
					dma_len = remainder;
					split_offset = 0;

					if ((subtotal + remainder) ==
								  protgrp_bytes)
						pgdone = 1;
				} else {
					/* must split this buffer with next
					 * prot grp
					 */
					dma_len = protgrp_bytes - subtotal;
					split_offset += dma_len;
				}

				subtotal += dma_len;

				sgl->word2 = 0;
				sgl->addr_lo = cpu_to_le32(putPaddrLow(
								 dataphysaddr));
				sgl->addr_hi = cpu_to_le32(putPaddrHigh(
								 dataphysaddr));
				bf_set(lpfc_sli4_sge_last, sgl, 0);
				bf_set(lpfc_sli4_sge_offset, sgl, dma_offset);
				bf_set(lpfc_sli4_sge_type, sgl,
				       LPFC_SGE_TYPE_DATA);

				sgl->sge_len = cpu_to_le32(dma_len);
				dma_offset += dma_len;

				num_sge++;
				curr_data++;

				if (split_offset) {
					sgl++;
					j++;
					break;
				}

				/* Move to the next s/g segment if possible */
				sgde = sg_next(sgde);

				sgl++;
			}

			j++;
		}

		if (protgroup_offset) {
			/* update the reference tag */
			reftag += protgrp_blks;
			continue;
		}

		/* are we done ? */
		if (curr_prot == protcnt) {
			/* mark the last SGL */
			sgl--;
			bf_set(lpfc_sli4_sge_last, sgl, 1);
			alldone = 1;
		} else if (curr_prot < protcnt) {
			/* advance to next prot buffer */
			sgpe = sg_next(sgpe);

			/* update the reference tag */
			reftag += protgrp_blks;
		} else {
			/* if we're here, we have a bug */
			lpfc_printf_log(phba, KERN_ERR, LOG_BG,
				"9085 BLKGRD: bug in %s\n", __func__);
		}

	} while (!alldone);

out:

	return num_sge;
}

/**
 * lpfc_prot_group_type - Get prtotection group type of SCSI command
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 *
 * Given a SCSI command that supports DIF, determine composition of protection
 * groups involved in setting up buffer lists
 *
 * Returns: Protection group type (with or without DIF)
 *
 **/
static int
lpfc_prot_group_type(struct lpfc_hba *phba, struct scsi_cmnd *sc)
{
	int ret = LPFC_PG_TYPE_INVALID;
	unsigned char op = scsi_get_prot_op(sc);

	switch (op) {
	case SCSI_PROT_READ_STRIP:
	case SCSI_PROT_WRITE_INSERT:
		ret = LPFC_PG_TYPE_NO_DIF;
		break;
	case SCSI_PROT_READ_INSERT:
	case SCSI_PROT_WRITE_STRIP:
	case SCSI_PROT_READ_PASS:
	case SCSI_PROT_WRITE_PASS:
		ret = LPFC_PG_TYPE_DIF_BUF;
		break;
	default:
		if (phba)
			lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
					"9021 Unsupported protection op:%d\n",
					op);
		break;
	}
	return ret;
}

/**
 * lpfc_bg_scsi_adjust_dl - Adjust SCSI data length for BlockGuard
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be adjusted.
 *
 * Adjust the data length to account for how much data
 * is actually on the wire.
 *
 * returns the adjusted data length
 **/
static int
lpfc_bg_scsi_adjust_dl(struct lpfc_hba *phba,
		       struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *sc = lpfc_cmd->pCmd;
	int fcpdl;

	fcpdl = scsi_bufflen(sc);

	/* Check if there is protection data on the wire */
	if (sc->sc_data_direction == DMA_FROM_DEVICE) {
		/* Read check for protection data */
		if (scsi_get_prot_op(sc) ==  SCSI_PROT_READ_INSERT)
			return fcpdl;

	} else {
		/* Write check for protection data */
		if (scsi_get_prot_op(sc) ==  SCSI_PROT_WRITE_STRIP)
			return fcpdl;
	}

	/*
	 * If we are in DIF Type 1 mode every data block has a 8 byte
	 * DIF (trailer) attached to it. Must ajust FCP data length
	 * to account for the protection data.
	 */
	fcpdl += (fcpdl / lpfc_cmd_blksize(sc)) * 8;

	return fcpdl;
}

/**
 * lpfc_bg_scsi_prep_dma_buf_s3 - DMA mapping for scsi buffer to SLI3 IF spec
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be prep'ed.
 *
 * This is the protection/DIF aware version of
 * lpfc_scsi_prep_dma_buf(). It may be a good idea to combine the
 * two functions eventually, but for now, it's here.
 * RETURNS 0 - SUCCESS,
 *         1 - Failed DMA map, retry.
 *         2 - Invalid scsi cmd or prot-type. Do not rety.
 **/
static int
lpfc_bg_scsi_prep_dma_buf_s3(struct lpfc_hba *phba,
		struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct ulp_bde64 *bpl = (struct ulp_bde64 *)lpfc_cmd->dma_sgl;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	uint32_t num_bde = 0;
	int datasegcnt, protsegcnt, datadir = scsi_cmnd->sc_data_direction;
	int prot_group_type = 0;
	int fcpdl;
	int ret = 1;
	struct lpfc_vport *vport = phba->pport;

	/*
	 * Start the lpfc command prep by bumping the bpl beyond fcp_cmnd
	 *  fcp_rsp regions to the first data bde entry
	 */
	bpl += 2;
	if (scsi_sg_count(scsi_cmnd)) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */
		datasegcnt = dma_map_sg(&phba->pcidev->dev,
					scsi_sglist(scsi_cmnd),
					scsi_sg_count(scsi_cmnd), datadir);
		if (unlikely(!datasegcnt))
			return 1;

		lpfc_cmd->seg_cnt = datasegcnt;

		/* First check if data segment count from SCSI Layer is good */
		if (lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt) {
			WARN_ON_ONCE(lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt);
			ret = 2;
			goto err;
		}

		prot_group_type = lpfc_prot_group_type(phba, scsi_cmnd);

		switch (prot_group_type) {
		case LPFC_PG_TYPE_NO_DIF:

			/* Here we need to add a PDE5 and PDE6 to the count */
			if ((lpfc_cmd->seg_cnt + 2) > phba->cfg_total_seg_cnt) {
				ret = 2;
				goto err;
			}

			num_bde = lpfc_bg_setup_bpl(phba, scsi_cmnd, bpl,
					datasegcnt);
			/* we should have 2 or more entries in buffer list */
			if (num_bde < 2) {
				ret = 2;
				goto err;
			}
			break;

		case LPFC_PG_TYPE_DIF_BUF:
			/*
			 * This type indicates that protection buffers are
			 * passed to the driver, so that needs to be prepared
			 * for DMA
			 */
			protsegcnt = dma_map_sg(&phba->pcidev->dev,
					scsi_prot_sglist(scsi_cmnd),
					scsi_prot_sg_count(scsi_cmnd), datadir);
			if (unlikely(!protsegcnt)) {
				scsi_dma_unmap(scsi_cmnd);
				return 1;
			}

			lpfc_cmd->prot_seg_cnt = protsegcnt;

			/*
			 * There is a minimun of 4 BPLs used for every
			 * protection data segment.
			 */
			if ((lpfc_cmd->prot_seg_cnt * 4) >
			    (phba->cfg_total_seg_cnt - 2)) {
				ret = 2;
				goto err;
			}

			num_bde = lpfc_bg_setup_bpl_prot(phba, scsi_cmnd, bpl,
					datasegcnt, protsegcnt);
			/* we should have 3 or more entries in buffer list */
			if ((num_bde < 3) ||
			    (num_bde > phba->cfg_total_seg_cnt)) {
				ret = 2;
				goto err;
			}
			break;

		case LPFC_PG_TYPE_INVALID:
		default:
			scsi_dma_unmap(scsi_cmnd);
			lpfc_cmd->seg_cnt = 0;

			lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
					"9022 Unexpected protection group %i\n",
					prot_group_type);
			return 2;
		}
	}

	/*
	 * Finish initializing those IOCB fields that are dependent on the
	 * scsi_cmnd request_buffer.  Note that the bdeSize is explicitly
	 * reinitialized since all iocb memory resources are used many times
	 * for transmit, receive, and continuation bpl's.
	 */
	iocb_cmd->un.fcpi64.bdl.bdeSize = (2 * sizeof(struct ulp_bde64));
	iocb_cmd->un.fcpi64.bdl.bdeSize += (num_bde * sizeof(struct ulp_bde64));
	iocb_cmd->ulpBdeCount = 1;
	iocb_cmd->ulpLe = 1;

	fcpdl = lpfc_bg_scsi_adjust_dl(phba, lpfc_cmd);
	fcp_cmnd->fcpDl = be32_to_cpu(fcpdl);

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = fcpdl;

	/*
	 * For First burst, we may need to adjust the initial transfer
	 * length for DIF
	 */
	if (iocb_cmd->un.fcpi.fcpi_XRdy &&
	    (fcpdl < vport->cfg_first_burst_size))
		iocb_cmd->un.fcpi.fcpi_XRdy = fcpdl;

	return 0;
err:
	if (lpfc_cmd->seg_cnt)
		scsi_dma_unmap(scsi_cmnd);
	if (lpfc_cmd->prot_seg_cnt)
		dma_unmap_sg(&phba->pcidev->dev, scsi_prot_sglist(scsi_cmnd),
			     scsi_prot_sg_count(scsi_cmnd),
			     scsi_cmnd->sc_data_direction);

	lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
			"9023 Cannot setup S/G List for HBA"
			"IO segs %d/%d BPL %d SCSI %d: %d %d\n",
			lpfc_cmd->seg_cnt, lpfc_cmd->prot_seg_cnt,
			phba->cfg_total_seg_cnt, phba->cfg_sg_seg_cnt,
			prot_group_type, num_bde);

	lpfc_cmd->seg_cnt = 0;
	lpfc_cmd->prot_seg_cnt = 0;
	return ret;
}

/*
 * This function calcuates the T10 DIF guard tag
 * on the specified data using a CRC algorithmn
 * using crc_t10dif.
 */
static uint16_t
lpfc_bg_crc(uint8_t *data, int count)
{
	uint16_t crc = 0;
	uint16_t x;

	crc = crc_t10dif(data, count);
	x = cpu_to_be16(crc);
	return x;
}

/*
 * This function calcuates the T10 DIF guard tag
 * on the specified data using a CSUM algorithmn
 * using ip_compute_csum.
 */
static uint16_t
lpfc_bg_csum(uint8_t *data, int count)
{
	uint16_t ret;

	ret = ip_compute_csum(data, count);
	return ret;
}

/*
 * This function examines the protection data to try to determine
 * what type of T10-DIF error occurred.
 */
static void
lpfc_calc_bg_err(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	struct scatterlist *sgpe; /* s/g prot entry */
	struct scatterlist *sgde; /* s/g data entry */
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	struct scsi_dif_tuple *src = NULL;
	uint8_t *data_src = NULL;
	uint16_t guard_tag;
	uint16_t start_app_tag, app_tag;
	uint32_t start_ref_tag, ref_tag;
	int prot, protsegcnt;
	int err_type, len, data_len;
	int chk_ref, chk_app, chk_guard;
	uint16_t sum;
	unsigned blksize;

	err_type = BGS_GUARD_ERR_MASK;
	sum = 0;
	guard_tag = 0;

	/* First check to see if there is protection data to examine */
	prot = scsi_get_prot_op(cmd);
	if ((prot == SCSI_PROT_READ_STRIP) ||
	    (prot == SCSI_PROT_WRITE_INSERT) ||
	    (prot == SCSI_PROT_NORMAL))
		goto out;

	/* Currently the driver just supports ref_tag and guard_tag checking */
	chk_ref = 1;
	chk_app = 0;
	chk_guard = 0;

	/* Setup a ptr to the protection data provided by the SCSI host */
	sgpe = scsi_prot_sglist(cmd);
	protsegcnt = lpfc_cmd->prot_seg_cnt;

	if (sgpe && protsegcnt) {

		/*
		 * We will only try to verify guard tag if the segment
		 * data length is a multiple of the blksize.
		 */
		sgde = scsi_sglist(cmd);
		blksize = lpfc_cmd_blksize(cmd);
		data_src = (uint8_t *)sg_virt(sgde);
		data_len = sgde->length;
		if ((data_len & (blksize - 1)) == 0)
			chk_guard = 1;

		src = (struct scsi_dif_tuple *)sg_virt(sgpe);
		start_ref_tag = (uint32_t)scsi_get_lba(cmd); /* Truncate LBA */
		start_app_tag = src->app_tag;
		len = sgpe->length;
		while (src && protsegcnt) {
			while (len) {

				/*
				 * First check to see if a protection data
				 * check is valid
				 */
				if ((src->ref_tag == T10_PI_REF_ESCAPE) ||
				    (src->app_tag == T10_PI_APP_ESCAPE)) {
					start_ref_tag++;
					goto skipit;
				}

				/* First Guard Tag checking */
				if (chk_guard) {
					guard_tag = src->guard_tag;
					if (lpfc_cmd_guard_csum(cmd))
						sum = lpfc_bg_csum(data_src,
								   blksize);
					else
						sum = lpfc_bg_crc(data_src,
								  blksize);
					if ((guard_tag != sum)) {
						err_type = BGS_GUARD_ERR_MASK;
						goto out;
					}
				}

				/* Reference Tag checking */
				ref_tag = be32_to_cpu(src->ref_tag);
				if (chk_ref && (ref_tag != start_ref_tag)) {
					err_type = BGS_REFTAG_ERR_MASK;
					goto out;
				}
				start_ref_tag++;

				/* App Tag checking */
				app_tag = src->app_tag;
				if (chk_app && (app_tag != start_app_tag)) {
					err_type = BGS_APPTAG_ERR_MASK;
					goto out;
				}
skipit:
				len -= sizeof(struct scsi_dif_tuple);
				if (len < 0)
					len = 0;
				src++;

				data_src += blksize;
				data_len -= blksize;

				/*
				 * Are we at the end of the Data segment?
				 * The data segment is only used for Guard
				 * tag checking.
				 */
				if (chk_guard && (data_len == 0)) {
					chk_guard = 0;
					sgde = sg_next(sgde);
					if (!sgde)
						goto out;

					data_src = (uint8_t *)sg_virt(sgde);
					data_len = sgde->length;
					if ((data_len & (blksize - 1)) == 0)
						chk_guard = 1;
				}
			}

			/* Goto the next Protection data segment */
			sgpe = sg_next(sgpe);
			if (sgpe) {
				src = (struct scsi_dif_tuple *)sg_virt(sgpe);
				len = sgpe->length;
			} else {
				src = NULL;
			}
			protsegcnt--;
		}
	}
out:
	if (err_type == BGS_GUARD_ERR_MASK) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
					0x10, 0x1);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;
		phba->bg_guard_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9069 BLKGRD: LBA %lx grd_tag error %x != %x\n",
				(unsigned long)scsi_get_lba(cmd),
				sum, guard_tag);

	} else if (err_type == BGS_REFTAG_ERR_MASK) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
					0x10, 0x3);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;

		phba->bg_reftag_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9066 BLKGRD: LBA %lx ref_tag error %x != %x\n",
				(unsigned long)scsi_get_lba(cmd),
				ref_tag, start_ref_tag);

	} else if (err_type == BGS_APPTAG_ERR_MASK) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
					0x10, 0x2);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;

		phba->bg_apptag_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9041 BLKGRD: LBA %lx app_tag error %x != %x\n",
				(unsigned long)scsi_get_lba(cmd),
				app_tag, start_app_tag);
	}
}


/*
 * This function checks for BlockGuard errors detected by
 * the HBA.  In case of errors, the ASC/ASCQ fields in the
 * sense buffer will be set accordingly, paired with
 * ILLEGAL_REQUEST to signal to the kernel that the HBA
 * detected corruption.
 *
 * Returns:
 *  0 - No error found
 *  1 - BlockGuard error found
 * -1 - Internal error (bad profile, ...etc)
 */
static int
lpfc_parse_bg_err(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd,
		  struct lpfc_iocbq *pIocbOut)
{
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	struct sli3_bg_fields *bgf = &pIocbOut->iocb.unsli3.sli3_bg;
	int ret = 0;
	uint32_t bghm = bgf->bghm;
	uint32_t bgstat = bgf->bgstat;
	uint64_t failing_sector = 0;

	if (lpfc_bgs_get_invalid_prof(bgstat)) {
		cmd->result = DID_ERROR << 16;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9072 BLKGRD: Invalid BG Profile in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
		ret = (-1);
		goto out;
	}

	if (lpfc_bgs_get_uninit_dif_block(bgstat)) {
		cmd->result = DID_ERROR << 16;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9073 BLKGRD: Invalid BG PDIF Block in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
		ret = (-1);
		goto out;
	}

	if (lpfc_bgs_get_guard_err(bgstat)) {
		ret = 1;

		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
				0x10, 0x1);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;
		phba->bg_guard_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9055 BLKGRD: Guard Tag error in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
	}

	if (lpfc_bgs_get_reftag_err(bgstat)) {
		ret = 1;

		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
				0x10, 0x3);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;

		phba->bg_reftag_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9056 BLKGRD: Ref Tag error in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
	}

	if (lpfc_bgs_get_apptag_err(bgstat)) {
		ret = 1;

		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
				0x10, 0x2);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;

		phba->bg_apptag_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9061 BLKGRD: App Tag error in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
	}

	if (lpfc_bgs_get_hi_water_mark_present(bgstat)) {
		/*
		 * setup sense data descriptor 0 per SPC-4 as an information
		 * field, and put the failing LBA in it.
		 * This code assumes there was also a guard/app/ref tag error
		 * indication.
		 */
		cmd->sense_buffer[7] = 0xc;   /* Additional sense length */
		cmd->sense_buffer[8] = 0;     /* Information descriptor type */
		cmd->sense_buffer[9] = 0xa;   /* Additional descriptor length */
		cmd->sense_buffer[10] = 0x80; /* Validity bit */

		/* bghm is a "on the wire" FC frame based count */
		switch (scsi_get_prot_op(cmd)) {
		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			bghm /= cmd->device->sector_size;
			break;
		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			bghm /= (cmd->device->sector_size +
				sizeof(struct scsi_dif_tuple));
			break;
		}

		failing_sector = scsi_get_lba(cmd);
		failing_sector += bghm;

		/* Descriptor Information */
		put_unaligned_be64(failing_sector, &cmd->sense_buffer[12]);
	}

	if (!ret) {
		/* No error was reported - problem in FW? */
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9057 BLKGRD: Unknown error in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);

		/* Calcuate what type of error it was */
		lpfc_calc_bg_err(phba, lpfc_cmd);
	}
out:
	return ret;
}

/**
 * lpfc_scsi_prep_dma_buf_s4 - DMA mapping for scsi buffer to SLI4 IF spec
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This routine does the pci dma mapping for scatter-gather list of scsi cmnd
 * field of @lpfc_cmd for device with SLI-4 interface spec.
 *
 * Return codes:
 *	2 - Error - Do not retry
 *	1 - Error - Retry
 *	0 - Success
 **/
static int
lpfc_scsi_prep_dma_buf_s4(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct scatterlist *sgel = NULL;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct sli4_sge *sgl = (struct sli4_sge *)lpfc_cmd->dma_sgl;
	struct sli4_sge *first_data_sgl;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	dma_addr_t physaddr;
	uint32_t num_bde = 0;
	uint32_t dma_len;
	uint32_t dma_offset = 0;
	int nseg, i, j;
	struct ulp_bde64 *bde;
	bool lsp_just_set = false;
	struct sli4_hybrid_sgl *sgl_xtra = NULL;

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	if (scsi_sg_count(scsi_cmnd)) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */

		nseg = scsi_dma_map(scsi_cmnd);
		if (unlikely(nseg <= 0))
			return 1;
		sgl += 1;
		/* clear the last flag in the fcp_rsp map entry */
		sgl->word2 = le32_to_cpu(sgl->word2);
		bf_set(lpfc_sli4_sge_last, sgl, 0);
		sgl->word2 = cpu_to_le32(sgl->word2);
		sgl += 1;
		first_data_sgl = sgl;
		lpfc_cmd->seg_cnt = nseg;
		if (!phba->cfg_xpsgl &&
		    lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt) {
			lpfc_printf_log(phba, KERN_ERR, LOG_BG, "9074 BLKGRD:"
				" %s: Too many sg segments from "
				"dma_map_sg.  Config %d, seg_cnt %d\n",
				__func__, phba->cfg_sg_seg_cnt,
			       lpfc_cmd->seg_cnt);
			WARN_ON_ONCE(lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt);
			lpfc_cmd->seg_cnt = 0;
			scsi_dma_unmap(scsi_cmnd);
			return 2;
		}

		/*
		 * The driver established a maximum scatter-gather segment count
		 * during probe that limits the number of sg elements in any
		 * single scsi command.  Just run through the seg_cnt and format
		 * the sge's.
		 * When using SLI-3 the driver will try to fit all the BDEs into
		 * the IOCB. If it can't then the BDEs get added to a BPL as it
		 * does for SLI-2 mode.
		 */

		/* for tracking segment boundaries */
		sgel = scsi_sglist(scsi_cmnd);
		j = 2;
		for (i = 0; i < nseg; i++) {
			sgl->word2 = 0;
			if ((num_bde + 1) == nseg) {
				bf_set(lpfc_sli4_sge_last, sgl, 1);
				bf_set(lpfc_sli4_sge_type, sgl,
				       LPFC_SGE_TYPE_DATA);
			} else {
				bf_set(lpfc_sli4_sge_last, sgl, 0);

				/* do we need to expand the segment */
				if (!lsp_just_set &&
				    !((j + 1) % phba->border_sge_num) &&
				    ((nseg - 1) != i)) {
					/* set LSP type */
					bf_set(lpfc_sli4_sge_type, sgl,
					       LPFC_SGE_TYPE_LSP);

					sgl_xtra = lpfc_get_sgl_per_hdwq(
							phba, lpfc_cmd);

					if (unlikely(!sgl_xtra)) {
						lpfc_cmd->seg_cnt = 0;
						scsi_dma_unmap(scsi_cmnd);
						return 1;
					}
					sgl->addr_lo = cpu_to_le32(putPaddrLow(
						       sgl_xtra->dma_phys_sgl));
					sgl->addr_hi = cpu_to_le32(putPaddrHigh(
						       sgl_xtra->dma_phys_sgl));

				} else {
					bf_set(lpfc_sli4_sge_type, sgl,
					       LPFC_SGE_TYPE_DATA);
				}
			}

			if (!(bf_get(lpfc_sli4_sge_type, sgl) &
				     LPFC_SGE_TYPE_LSP)) {
				if ((nseg - 1) == i)
					bf_set(lpfc_sli4_sge_last, sgl, 1);

				physaddr = sg_dma_address(sgel);
				dma_len = sg_dma_len(sgel);
				sgl->addr_lo = cpu_to_le32(putPaddrLow(
							   physaddr));
				sgl->addr_hi = cpu_to_le32(putPaddrHigh(
							   physaddr));

				bf_set(lpfc_sli4_sge_offset, sgl, dma_offset);
				sgl->word2 = cpu_to_le32(sgl->word2);
				sgl->sge_len = cpu_to_le32(dma_len);

				dma_offset += dma_len;
				sgel = sg_next(sgel);

				sgl++;
				lsp_just_set = false;

			} else {
				sgl->word2 = cpu_to_le32(sgl->word2);
				sgl->sge_len = cpu_to_le32(
						     phba->cfg_sg_dma_buf_size);

				sgl = (struct sli4_sge *)sgl_xtra->dma_sgl;
				i = i - 1;

				lsp_just_set = true;
			}

			j++;
		}
		/*
		 * Setup the first Payload BDE. For FCoE we just key off
		 * Performance Hints, for FC we use lpfc_enable_pbde.
		 * We populate words 13-15 of IOCB/WQE.
		 */
		if ((phba->sli3_options & LPFC_SLI4_PERFH_ENABLED) ||
		    phba->cfg_enable_pbde) {
			bde = (struct ulp_bde64 *)
				&(iocb_cmd->unsli3.sli3Words[5]);
			bde->addrLow = first_data_sgl->addr_lo;
			bde->addrHigh = first_data_sgl->addr_hi;
			bde->tus.f.bdeSize =
					le32_to_cpu(first_data_sgl->sge_len);
			bde->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
			bde->tus.w = cpu_to_le32(bde->tus.w);
		}
	} else {
		sgl += 1;
		/* clear the last flag in the fcp_rsp map entry */
		sgl->word2 = le32_to_cpu(sgl->word2);
		bf_set(lpfc_sli4_sge_last, sgl, 1);
		sgl->word2 = cpu_to_le32(sgl->word2);

		if ((phba->sli3_options & LPFC_SLI4_PERFH_ENABLED) ||
		    phba->cfg_enable_pbde) {
			bde = (struct ulp_bde64 *)
				&(iocb_cmd->unsli3.sli3Words[5]);
			memset(bde, 0, (sizeof(uint32_t) * 3));
		}
	}

	/*
	 * Finish initializing those IOCB fields that are dependent on the
	 * scsi_cmnd request_buffer.  Note that for SLI-2 the bdeSize is
	 * explicitly reinitialized.
	 * all iocb memory resources are reused.
	 */
	fcp_cmnd->fcpDl = cpu_to_be32(scsi_bufflen(scsi_cmnd));

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = scsi_bufflen(scsi_cmnd);

	/*
	 * If the OAS driver feature is enabled and the lun is enabled for
	 * OAS, set the oas iocb related flags.
	 */
	if ((phba->cfg_fof) && ((struct lpfc_device_data *)
		scsi_cmnd->device->hostdata)->oas_enabled) {
		lpfc_cmd->cur_iocbq.iocb_flag |= (LPFC_IO_OAS | LPFC_IO_FOF);
		lpfc_cmd->cur_iocbq.priority = ((struct lpfc_device_data *)
			scsi_cmnd->device->hostdata)->priority;
	}

	return 0;
}

/**
 * lpfc_bg_scsi_prep_dma_buf_s4 - DMA mapping for scsi buffer to SLI4 IF spec
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This is the protection/DIF aware version of
 * lpfc_scsi_prep_dma_buf(). It may be a good idea to combine the
 * two functions eventually, but for now, it's here
 * Return codes:
 *	2 - Error - Do not retry
 *	1 - Error - Retry
 *	0 - Success
 **/
static int
lpfc_bg_scsi_prep_dma_buf_s4(struct lpfc_hba *phba,
		struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct sli4_sge *sgl = (struct sli4_sge *)(lpfc_cmd->dma_sgl);
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	uint32_t num_sge = 0;
	int datasegcnt, protsegcnt, datadir = scsi_cmnd->sc_data_direction;
	int prot_group_type = 0;
	int fcpdl;
	int ret = 1;
	struct lpfc_vport *vport = phba->pport;

	/*
	 * Start the lpfc command prep by bumping the sgl beyond fcp_cmnd
	 *  fcp_rsp regions to the first data sge entry
	 */
	if (scsi_sg_count(scsi_cmnd)) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */
		datasegcnt = dma_map_sg(&phba->pcidev->dev,
					scsi_sglist(scsi_cmnd),
					scsi_sg_count(scsi_cmnd), datadir);
		if (unlikely(!datasegcnt))
			return 1;

		sgl += 1;
		/* clear the last flag in the fcp_rsp map entry */
		sgl->word2 = le32_to_cpu(sgl->word2);
		bf_set(lpfc_sli4_sge_last, sgl, 0);
		sgl->word2 = cpu_to_le32(sgl->word2);

		sgl += 1;
		lpfc_cmd->seg_cnt = datasegcnt;

		/* First check if data segment count from SCSI Layer is good */
		if (lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt &&
		    !phba->cfg_xpsgl) {
			WARN_ON_ONCE(lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt);
			ret = 2;
			goto err;
		}

		prot_group_type = lpfc_prot_group_type(phba, scsi_cmnd);

		switch (prot_group_type) {
		case LPFC_PG_TYPE_NO_DIF:
			/* Here we need to add a DISEED to the count */
			if (((lpfc_cmd->seg_cnt + 1) >
					phba->cfg_total_seg_cnt) &&
			    !phba->cfg_xpsgl) {
				ret = 2;
				goto err;
			}

			num_sge = lpfc_bg_setup_sgl(phba, scsi_cmnd, sgl,
					datasegcnt, lpfc_cmd);

			/* we should have 2 or more entries in buffer list */
			if (num_sge < 2) {
				ret = 2;
				goto err;
			}
			break;

		case LPFC_PG_TYPE_DIF_BUF:
			/*
			 * This type indicates that protection buffers are
			 * passed to the driver, so that needs to be prepared
			 * for DMA
			 */
			protsegcnt = dma_map_sg(&phba->pcidev->dev,
					scsi_prot_sglist(scsi_cmnd),
					scsi_prot_sg_count(scsi_cmnd), datadir);
			if (unlikely(!protsegcnt)) {
				scsi_dma_unmap(scsi_cmnd);
				return 1;
			}

			lpfc_cmd->prot_seg_cnt = protsegcnt;
			/*
			 * There is a minimun of 3 SGEs used for every
			 * protection data segment.
			 */
			if (((lpfc_cmd->prot_seg_cnt * 3) >
					(phba->cfg_total_seg_cnt - 2)) &&
			    !phba->cfg_xpsgl) {
				ret = 2;
				goto err;
			}

			num_sge = lpfc_bg_setup_sgl_prot(phba, scsi_cmnd, sgl,
					datasegcnt, protsegcnt, lpfc_cmd);

			/* we should have 3 or more entries in buffer list */
			if (num_sge < 3 ||
			    (num_sge > phba->cfg_total_seg_cnt &&
			     !phba->cfg_xpsgl)) {
				ret = 2;
				goto err;
			}
			break;

		case LPFC_PG_TYPE_INVALID:
		default:
			scsi_dma_unmap(scsi_cmnd);
			lpfc_cmd->seg_cnt = 0;

			lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
					"9083 Unexpected protection group %i\n",
					prot_group_type);
			return 2;
		}
	}

	switch (scsi_get_prot_op(scsi_cmnd)) {
	case SCSI_PROT_WRITE_STRIP:
	case SCSI_PROT_READ_STRIP:
		lpfc_cmd->cur_iocbq.iocb_flag |= LPFC_IO_DIF_STRIP;
		break;
	case SCSI_PROT_WRITE_INSERT:
	case SCSI_PROT_READ_INSERT:
		lpfc_cmd->cur_iocbq.iocb_flag |= LPFC_IO_DIF_INSERT;
		break;
	case SCSI_PROT_WRITE_PASS:
	case SCSI_PROT_READ_PASS:
		lpfc_cmd->cur_iocbq.iocb_flag |= LPFC_IO_DIF_PASS;
		break;
	}

	fcpdl = lpfc_bg_scsi_adjust_dl(phba, lpfc_cmd);
	fcp_cmnd->fcpDl = be32_to_cpu(fcpdl);

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = fcpdl;

	/*
	 * For First burst, we may need to adjust the initial transfer
	 * length for DIF
	 */
	if (iocb_cmd->un.fcpi.fcpi_XRdy &&
	    (fcpdl < vport->cfg_first_burst_size))
		iocb_cmd->un.fcpi.fcpi_XRdy = fcpdl;

	/*
	 * If the OAS driver feature is enabled and the lun is enabled for
	 * OAS, set the oas iocb related flags.
	 */
	if ((phba->cfg_fof) && ((struct lpfc_device_data *)
		scsi_cmnd->device->hostdata)->oas_enabled)
		lpfc_cmd->cur_iocbq.iocb_flag |= (LPFC_IO_OAS | LPFC_IO_FOF);

	return 0;
err:
	if (lpfc_cmd->seg_cnt)
		scsi_dma_unmap(scsi_cmnd);
	if (lpfc_cmd->prot_seg_cnt)
		dma_unmap_sg(&phba->pcidev->dev, scsi_prot_sglist(scsi_cmnd),
			     scsi_prot_sg_count(scsi_cmnd),
			     scsi_cmnd->sc_data_direction);

	lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
			"9084 Cannot setup S/G List for HBA"
			"IO segs %d/%d SGL %d SCSI %d: %d %d\n",
			lpfc_cmd->seg_cnt, lpfc_cmd->prot_seg_cnt,
			phba->cfg_total_seg_cnt, phba->cfg_sg_seg_cnt,
			prot_group_type, num_sge);

	lpfc_cmd->seg_cnt = 0;
	lpfc_cmd->prot_seg_cnt = 0;
	return ret;
}

/**
 * lpfc_scsi_prep_dma_buf - Wrapper function for DMA mapping of scsi buffer
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This routine wraps the actual DMA mapping function pointer from the
 * lpfc_hba struct.
 *
 * Return codes:
 *	1 - Error
 *	0 - Success
 **/
static inline int
lpfc_scsi_prep_dma_buf(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	return phba->lpfc_scsi_prep_dma_buf(phba, lpfc_cmd);
}

/**
 * lpfc_bg_scsi_prep_dma_buf - Wrapper function for DMA mapping of scsi buffer
 * using BlockGuard.
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This routine wraps the actual DMA mapping function pointer from the
 * lpfc_hba struct.
 *
 * Return codes:
 *	1 - Error
 *	0 - Success
 **/
static inline int
lpfc_bg_scsi_prep_dma_buf(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	return phba->lpfc_bg_scsi_prep_dma_buf(phba, lpfc_cmd);
}

/**
 * lpfc_send_scsi_error_event - Posts an event when there is SCSI error
 * @phba: Pointer to hba context object.
 * @vport: Pointer to vport object.
 * @lpfc_cmd: Pointer to lpfc scsi command which reported the error.
 * @rsp_iocb: Pointer to response iocb object which reported error.
 *
 * This function posts an event when there is a SCSI command reporting
 * error from the scsi device.
 **/
static void
lpfc_send_scsi_error_event(struct lpfc_hba *phba, struct lpfc_vport *vport,
		struct lpfc_io_buf *lpfc_cmd, struct lpfc_iocbq *rsp_iocb) {
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	uint32_t resp_info = fcprsp->rspStatus2;
	uint32_t scsi_status = fcprsp->rspStatus3;
	uint32_t fcpi_parm = rsp_iocb->iocb.un.fcpi.fcpi_parm;
	struct lpfc_fast_path_event *fast_path_evt = NULL;
	struct lpfc_nodelist *pnode = lpfc_cmd->rdata->pnode;
	unsigned long flags;

	if (!pnode || !NLP_CHK_NODE_ACT(pnode))
		return;

	/* If there is queuefull or busy condition send a scsi event */
	if ((cmnd->result == SAM_STAT_TASK_SET_FULL) ||
		(cmnd->result == SAM_STAT_BUSY)) {
		fast_path_evt = lpfc_alloc_fast_evt(phba);
		if (!fast_path_evt)
			return;
		fast_path_evt->un.scsi_evt.event_type =
			FC_REG_SCSI_EVENT;
		fast_path_evt->un.scsi_evt.subcategory =
		(cmnd->result == SAM_STAT_TASK_SET_FULL) ?
		LPFC_EVENT_QFULL : LPFC_EVENT_DEVBSY;
		fast_path_evt->un.scsi_evt.lun = cmnd->device->lun;
		memcpy(&fast_path_evt->un.scsi_evt.wwpn,
			&pnode->nlp_portname, sizeof(struct lpfc_name));
		memcpy(&fast_path_evt->un.scsi_evt.wwnn,
			&pnode->nlp_nodename, sizeof(struct lpfc_name));
	} else if ((resp_info & SNS_LEN_VALID) && fcprsp->rspSnsLen &&
		((cmnd->cmnd[0] == READ_10) || (cmnd->cmnd[0] == WRITE_10))) {
		fast_path_evt = lpfc_alloc_fast_evt(phba);
		if (!fast_path_evt)
			return;
		fast_path_evt->un.check_cond_evt.scsi_event.event_type =
			FC_REG_SCSI_EVENT;
		fast_path_evt->un.check_cond_evt.scsi_event.subcategory =
			LPFC_EVENT_CHECK_COND;
		fast_path_evt->un.check_cond_evt.scsi_event.lun =
			cmnd->device->lun;
		memcpy(&fast_path_evt->un.check_cond_evt.scsi_event.wwpn,
			&pnode->nlp_portname, sizeof(struct lpfc_name));
		memcpy(&fast_path_evt->un.check_cond_evt.scsi_event.wwnn,
			&pnode->nlp_nodename, sizeof(struct lpfc_name));
		fast_path_evt->un.check_cond_evt.sense_key =
			cmnd->sense_buffer[2] & 0xf;
		fast_path_evt->un.check_cond_evt.asc = cmnd->sense_buffer[12];
		fast_path_evt->un.check_cond_evt.ascq = cmnd->sense_buffer[13];
	} else if ((cmnd->sc_data_direction == DMA_FROM_DEVICE) &&
		     fcpi_parm &&
		     ((be32_to_cpu(fcprsp->rspResId) != fcpi_parm) ||
			((scsi_status == SAM_STAT_GOOD) &&
			!(resp_info & (RESID_UNDER | RESID_OVER))))) {
		/*
		 * If status is good or resid does not match with fcp_param and
		 * there is valid fcpi_parm, then there is a read_check error
		 */
		fast_path_evt = lpfc_alloc_fast_evt(phba);
		if (!fast_path_evt)
			return;
		fast_path_evt->un.read_check_error.header.event_type =
			FC_REG_FABRIC_EVENT;
		fast_path_evt->un.read_check_error.header.subcategory =
			LPFC_EVENT_FCPRDCHKERR;
		memcpy(&fast_path_evt->un.read_check_error.header.wwpn,
			&pnode->nlp_portname, sizeof(struct lpfc_name));
		memcpy(&fast_path_evt->un.read_check_error.header.wwnn,
			&pnode->nlp_nodename, sizeof(struct lpfc_name));
		fast_path_evt->un.read_check_error.lun = cmnd->device->lun;
		fast_path_evt->un.read_check_error.opcode = cmnd->cmnd[0];
		fast_path_evt->un.read_check_error.fcpiparam =
			fcpi_parm;
	} else
		return;

	fast_path_evt->vport = vport;
	spin_lock_irqsave(&phba->hbalock, flags);
	list_add_tail(&fast_path_evt->work_evt.evt_listp, &phba->work_list);
	spin_unlock_irqrestore(&phba->hbalock, flags);
	lpfc_worker_wake_up(phba);
	return;
}

/**
 * lpfc_scsi_unprep_dma_buf - Un-map DMA mapping of SG-list for dev
 * @phba: The HBA for which this call is being executed.
 * @psb: The scsi buffer which is going to be un-mapped.
 *
 * This routine does DMA un-mapping of scatter gather list of scsi command
 * field of @lpfc_cmd for device with SLI-3 interface spec.
 **/
static void
lpfc_scsi_unprep_dma_buf(struct lpfc_hba *phba, struct lpfc_io_buf *psb)
{
	/*
	 * There are only two special cases to consider.  (1) the scsi command
	 * requested scatter-gather usage or (2) the scsi command allocated
	 * a request buffer, but did not request use_sg.  There is a third
	 * case, but it does not require resource deallocation.
	 */
	if (psb->seg_cnt > 0)
		scsi_dma_unmap(psb->pCmd);
	if (psb->prot_seg_cnt > 0)
		dma_unmap_sg(&phba->pcidev->dev, scsi_prot_sglist(psb->pCmd),
				scsi_prot_sg_count(psb->pCmd),
				psb->pCmd->sc_data_direction);
}

/**
 * lpfc_handler_fcp_err - FCP response handler
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: Pointer to lpfc_io_buf data structure.
 * @rsp_iocb: The response IOCB which contains FCP error.
 *
 * This routine is called to process response IOCB with status field
 * IOSTAT_FCP_RSP_ERROR. This routine sets result field of scsi command
 * based upon SCSI and FCP error.
 **/
static void
lpfc_handle_fcp_err(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd,
		    struct lpfc_iocbq *rsp_iocb)
{
	struct lpfc_hba *phba = vport->phba;
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcpcmd = lpfc_cmd->fcp_cmnd;
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	uint32_t fcpi_parm = rsp_iocb->iocb.un.fcpi.fcpi_parm;
	uint32_t resp_info = fcprsp->rspStatus2;
	uint32_t scsi_status = fcprsp->rspStatus3;
	uint32_t *lp;
	uint32_t host_status = DID_OK;
	uint32_t rsplen = 0;
	uint32_t fcpDl;
	uint32_t logit = LOG_FCP | LOG_FCP_ERROR;


	/*
	 *  If this is a task management command, there is no
	 *  scsi packet associated with this lpfc_cmd.  The driver
	 *  consumes it.
	 */
	if (fcpcmd->fcpCntl2) {
		scsi_status = 0;
		goto out;
	}

	if (resp_info & RSP_LEN_VALID) {
		rsplen = be32_to_cpu(fcprsp->rspRspLen);
		if (rsplen != 0 && rsplen != 4 && rsplen != 8) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "2719 Invalid response length: "
				 "tgt x%x lun x%llx cmnd x%x rsplen x%x\n",
				 cmnd->device->id,
				 cmnd->device->lun, cmnd->cmnd[0],
				 rsplen);
			host_status = DID_ERROR;
			goto out;
		}
		if (fcprsp->rspInfo3 != RSP_NO_FAILURE) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "2757 Protocol failure detected during "
				 "processing of FCP I/O op: "
				 "tgt x%x lun x%llx cmnd x%x rspInfo3 x%x\n",
				 cmnd->device->id,
				 cmnd->device->lun, cmnd->cmnd[0],
				 fcprsp->rspInfo3);
			host_status = DID_ERROR;
			goto out;
		}
	}

	if ((resp_info & SNS_LEN_VALID) && fcprsp->rspSnsLen) {
		uint32_t snslen = be32_to_cpu(fcprsp->rspSnsLen);
		if (snslen > SCSI_SENSE_BUFFERSIZE)
			snslen = SCSI_SENSE_BUFFERSIZE;

		if (resp_info & RSP_LEN_VALID)
		  rsplen = be32_to_cpu(fcprsp->rspRspLen);
		memcpy(cmnd->sense_buffer, &fcprsp->rspInfo0 + rsplen, snslen);
	}
	lp = (uint32_t *)cmnd->sense_buffer;

	/* special handling for under run conditions */
	if (!scsi_status && (resp_info & RESID_UNDER)) {
		/* don't log under runs if fcp set... */
		if (vport->cfg_log_verbose & LOG_FCP)
			logit = LOG_FCP_ERROR;
		/* unless operator says so */
		if (vport->cfg_log_verbose & LOG_FCP_UNDER)
			logit = LOG_FCP_UNDER;
	}

	lpfc_printf_vlog(vport, KERN_WARNING, logit,
			 "9024 FCP command x%x failed: x%x SNS x%x x%x "
			 "Data: x%x x%x x%x x%x x%x\n",
			 cmnd->cmnd[0], scsi_status,
			 be32_to_cpu(*lp), be32_to_cpu(*(lp + 3)), resp_info,
			 be32_to_cpu(fcprsp->rspResId),
			 be32_to_cpu(fcprsp->rspSnsLen),
			 be32_to_cpu(fcprsp->rspRspLen),
			 fcprsp->rspInfo3);

	scsi_set_resid(cmnd, 0);
	fcpDl = be32_to_cpu(fcpcmd->fcpDl);
	if (resp_info & RESID_UNDER) {
		scsi_set_resid(cmnd, be32_to_cpu(fcprsp->rspResId));

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP_UNDER,
				 "9025 FCP Underrun, expected %d, "
				 "residual %d Data: x%x x%x x%x\n",
				 fcpDl,
				 scsi_get_resid(cmnd), fcpi_parm, cmnd->cmnd[0],
				 cmnd->underflow);

		/*
		 * If there is an under run, check if under run reported by
		 * storage array is same as the under run reported by HBA.
		 * If this is not same, there is a dropped frame.
		 */
		if (fcpi_parm && (scsi_get_resid(cmnd) != fcpi_parm)) {
			lpfc_printf_vlog(vport, KERN_WARNING,
					 LOG_FCP | LOG_FCP_ERROR,
					 "9026 FCP Read Check Error "
					 "and Underrun Data: x%x x%x x%x x%x\n",
					 fcpDl,
					 scsi_get_resid(cmnd), fcpi_parm,
					 cmnd->cmnd[0]);
			scsi_set_resid(cmnd, scsi_bufflen(cmnd));
			host_status = DID_ERROR;
		}
		/*
		 * The cmnd->underflow is the minimum number of bytes that must
		 * be transferred for this command.  Provided a sense condition
		 * is not present, make sure the actual amount transferred is at
		 * least the underflow value or fail.
		 */
		if (!(resp_info & SNS_LEN_VALID) &&
		    (scsi_status == SAM_STAT_GOOD) &&
		    (scsi_bufflen(cmnd) - scsi_get_resid(cmnd)
		     < cmnd->underflow)) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
					 "9027 FCP command x%x residual "
					 "underrun converted to error "
					 "Data: x%x x%x x%x\n",
					 cmnd->cmnd[0], scsi_bufflen(cmnd),
					 scsi_get_resid(cmnd), cmnd->underflow);
			host_status = DID_ERROR;
		}
	} else if (resp_info & RESID_OVER) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "9028 FCP command x%x residual overrun error. "
				 "Data: x%x x%x\n", cmnd->cmnd[0],
				 scsi_bufflen(cmnd), scsi_get_resid(cmnd));
		host_status = DID_ERROR;

	/*
	 * Check SLI validation that all the transfer was actually done
	 * (fcpi_parm should be zero). Apply check only to reads.
	 */
	} else if (fcpi_parm) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP | LOG_FCP_ERROR,
				 "9029 FCP %s Check Error xri x%x  Data: "
				 "x%x x%x x%x x%x x%x\n",
				 ((cmnd->sc_data_direction == DMA_FROM_DEVICE) ?
				 "Read" : "Write"),
				 ((phba->sli_rev == LPFC_SLI_REV4) ?
				 lpfc_cmd->cur_iocbq.sli4_xritag :
				 rsp_iocb->iocb.ulpContext),
				 fcpDl, be32_to_cpu(fcprsp->rspResId),
				 fcpi_parm, cmnd->cmnd[0], scsi_status);

		/* There is some issue with the LPe12000 that causes it
		 * to miscalculate the fcpi_parm and falsely trip this
		 * recovery logic.  Detect this case and don't error when true.
		 */
		if (fcpi_parm > fcpDl)
			goto out;

		switch (scsi_status) {
		case SAM_STAT_GOOD:
		case SAM_STAT_CHECK_CONDITION:
			/* Fabric dropped a data frame. Fail any successful
			 * command in which we detected dropped frames.
			 * A status of good or some check conditions could
			 * be considered a successful command.
			 */
			host_status = DID_ERROR;
			break;
		}
		scsi_set_resid(cmnd, scsi_bufflen(cmnd));
	}

 out:
	cmnd->result = host_status << 16 | scsi_status;
	lpfc_send_scsi_error_event(vport->phba, vport, lpfc_cmd, rsp_iocb);
}

/**
 * lpfc_scsi_cmd_iocb_cmpl - Scsi cmnd IOCB completion routine
 * @phba: The Hba for which this call is being executed.
 * @pIocbIn: The command IOCBQ for the scsi cmnd.
 * @pIocbOut: The response IOCBQ for the scsi cmnd.
 *
 * This routine assigns scsi command result by looking into response IOCB
 * status field appropriately. This routine handles QUEUE FULL condition as
 * well by ramping down device queue depth.
 **/
static void
lpfc_scsi_cmd_iocb_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *pIocbIn,
			struct lpfc_iocbq *pIocbOut)
{
	struct lpfc_io_buf *lpfc_cmd =
		(struct lpfc_io_buf *) pIocbIn->context1;
	struct lpfc_vport      *vport = pIocbIn->vport;
	struct lpfc_rport_data *rdata = lpfc_cmd->rdata;
	struct lpfc_nodelist *pnode = rdata->pnode;
	struct scsi_cmnd *cmd;
	unsigned long flags;
	struct lpfc_fast_path_event *fast_path_evt;
	struct Scsi_Host *shost;
	int idx;
	uint32_t logit = LOG_FCP;

	/* Guard against abort handler being called at same time */
	spin_lock(&lpfc_cmd->buf_lock);

	/* Sanity check on return of outstanding command */
	cmd = lpfc_cmd->pCmd;
	if (!cmd || !phba) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "2621 IO completion: Not an active IO\n");
		spin_unlock(&lpfc_cmd->buf_lock);
		return;
	}

	idx = lpfc_cmd->cur_iocbq.hba_wqidx;
	if (phba->sli4_hba.hdwq)
		phba->sli4_hba.hdwq[idx].scsi_cstat.io_cmpls++;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (unlikely(phba->hdwqstat_on & LPFC_CHECK_SCSI_IO))
		this_cpu_inc(phba->sli4_hba.c_stat->cmpl_io);
#endif
	shost = cmd->device->host;

	lpfc_cmd->result = (pIocbOut->iocb.un.ulpWord[4] & IOERR_PARAM_MASK);
	lpfc_cmd->status = pIocbOut->iocb.ulpStatus;
	/* pick up SLI4 exhange busy status from HBA */
	if (pIocbOut->iocb_flag & LPFC_EXCHANGE_BUSY)
		lpfc_cmd->flags |= LPFC_SBUF_XBUSY;
	else
		lpfc_cmd->flags &= ~LPFC_SBUF_XBUSY;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (lpfc_cmd->prot_data_type) {
		struct scsi_dif_tuple *src = NULL;

		src =  (struct scsi_dif_tuple *)lpfc_cmd->prot_data_segment;
		/*
		 * Used to restore any changes to protection
		 * data for error injection.
		 */
		switch (lpfc_cmd->prot_data_type) {
		case LPFC_INJERR_REFTAG:
			src->ref_tag =
				lpfc_cmd->prot_data;
			break;
		case LPFC_INJERR_APPTAG:
			src->app_tag =
				(uint16_t)lpfc_cmd->prot_data;
			break;
		case LPFC_INJERR_GUARD:
			src->guard_tag =
				(uint16_t)lpfc_cmd->prot_data;
			break;
		default:
			break;
		}

		lpfc_cmd->prot_data = 0;
		lpfc_cmd->prot_data_type = 0;
		lpfc_cmd->prot_data_segment = NULL;
	}
#endif

	if (unlikely(lpfc_cmd->status)) {
		if (lpfc_cmd->status == IOSTAT_LOCAL_REJECT &&
		    (lpfc_cmd->result & IOERR_DRVR_MASK))
			lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
		else if (lpfc_cmd->status >= IOSTAT_CNT)
			lpfc_cmd->status = IOSTAT_DEFAULT;
		if (lpfc_cmd->status == IOSTAT_FCP_RSP_ERROR &&
		    !lpfc_cmd->fcp_rsp->rspStatus3 &&
		    (lpfc_cmd->fcp_rsp->rspStatus2 & RESID_UNDER) &&
		    !(vport->cfg_log_verbose & LOG_FCP_UNDER))
			logit = 0;
		else
			logit = LOG_FCP | LOG_FCP_UNDER;
		lpfc_printf_vlog(vport, KERN_WARNING, logit,
			 "9030 FCP cmd x%x failed <%d/%lld> "
			 "status: x%x result: x%x "
			 "sid: x%x did: x%x oxid: x%x "
			 "Data: x%x x%x\n",
			 cmd->cmnd[0],
			 cmd->device ? cmd->device->id : 0xffff,
			 cmd->device ? cmd->device->lun : 0xffff,
			 lpfc_cmd->status, lpfc_cmd->result,
			 vport->fc_myDID,
			 (pnode) ? pnode->nlp_DID : 0,
			 phba->sli_rev == LPFC_SLI_REV4 ?
			     lpfc_cmd->cur_iocbq.sli4_xritag : 0xffff,
			 pIocbOut->iocb.ulpContext,
			 lpfc_cmd->cur_iocbq.iocb.ulpIoTag);

		switch (lpfc_cmd->status) {
		case IOSTAT_FCP_RSP_ERROR:
			/* Call FCP RSP handler to determine result */
			lpfc_handle_fcp_err(vport, lpfc_cmd, pIocbOut);
			break;
		case IOSTAT_NPORT_BSY:
		case IOSTAT_FABRIC_BSY:
			cmd->result = DID_TRANSPORT_DISRUPTED << 16;
			fast_path_evt = lpfc_alloc_fast_evt(phba);
			if (!fast_path_evt)
				break;
			fast_path_evt->un.fabric_evt.event_type =
				FC_REG_FABRIC_EVENT;
			fast_path_evt->un.fabric_evt.subcategory =
				(lpfc_cmd->status == IOSTAT_NPORT_BSY) ?
				LPFC_EVENT_PORT_BUSY : LPFC_EVENT_FABRIC_BUSY;
			if (pnode && NLP_CHK_NODE_ACT(pnode)) {
				memcpy(&fast_path_evt->un.fabric_evt.wwpn,
					&pnode->nlp_portname,
					sizeof(struct lpfc_name));
				memcpy(&fast_path_evt->un.fabric_evt.wwnn,
					&pnode->nlp_nodename,
					sizeof(struct lpfc_name));
			}
			fast_path_evt->vport = vport;
			fast_path_evt->work_evt.evt =
				LPFC_EVT_FASTPATH_MGMT_EVT;
			spin_lock_irqsave(&phba->hbalock, flags);
			list_add_tail(&fast_path_evt->work_evt.evt_listp,
				&phba->work_list);
			spin_unlock_irqrestore(&phba->hbalock, flags);
			lpfc_worker_wake_up(phba);
			break;
		case IOSTAT_LOCAL_REJECT:
		case IOSTAT_REMOTE_STOP:
			if (lpfc_cmd->result == IOERR_ELXSEC_KEY_UNWRAP_ERROR ||
			    lpfc_cmd->result ==
					IOERR_ELXSEC_KEY_UNWRAP_COMPARE_ERROR ||
			    lpfc_cmd->result == IOERR_ELXSEC_CRYPTO_ERROR ||
			    lpfc_cmd->result ==
					IOERR_ELXSEC_CRYPTO_COMPARE_ERROR) {
				cmd->result = DID_NO_CONNECT << 16;
				break;
			}
			if (lpfc_cmd->result == IOERR_INVALID_RPI ||
			    lpfc_cmd->result == IOERR_NO_RESOURCES ||
			    lpfc_cmd->result == IOERR_ABORT_REQUESTED ||
			    lpfc_cmd->result == IOERR_SLER_CMD_RCV_FAILURE) {
				cmd->result = DID_REQUEUE << 16;
				break;
			}
			if ((lpfc_cmd->result == IOERR_RX_DMA_FAILED ||
			     lpfc_cmd->result == IOERR_TX_DMA_FAILED) &&
			     pIocbOut->iocb.unsli3.sli3_bg.bgstat) {
				if (scsi_get_prot_op(cmd) != SCSI_PROT_NORMAL) {
					/*
					 * This is a response for a BG enabled
					 * cmd. Parse BG error
					 */
					lpfc_parse_bg_err(phba, lpfc_cmd,
							pIocbOut);
					break;
				} else {
					lpfc_printf_vlog(vport, KERN_WARNING,
							LOG_BG,
							"9031 non-zero BGSTAT "
							"on unprotected cmd\n");
				}
			}
			if ((lpfc_cmd->status == IOSTAT_REMOTE_STOP)
				&& (phba->sli_rev == LPFC_SLI_REV4)
				&& (pnode && NLP_CHK_NODE_ACT(pnode))) {
				/* This IO was aborted by the target, we don't
				 * know the rxid and because we did not send the
				 * ABTS we cannot generate and RRQ.
				 */
				lpfc_set_rrq_active(phba, pnode,
					lpfc_cmd->cur_iocbq.sli4_lxritag,
					0, 0);
			}
			/* fall through */
		default:
			cmd->result = DID_ERROR << 16;
			break;
		}

		if (!pnode || !NLP_CHK_NODE_ACT(pnode)
		    || (pnode->nlp_state != NLP_STE_MAPPED_NODE))
			cmd->result = DID_TRANSPORT_DISRUPTED << 16 |
				      SAM_STAT_BUSY;
	} else
		cmd->result = DID_OK << 16;

	if (cmd->result || lpfc_cmd->fcp_rsp->rspSnsLen) {
		uint32_t *lp = (uint32_t *)cmd->sense_buffer;

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "0710 Iodone <%d/%llu> cmd x%px, error "
				 "x%x SNS x%x x%x Data: x%x x%x\n",
				 cmd->device->id, cmd->device->lun, cmd,
				 cmd->result, *lp, *(lp + 3), cmd->retries,
				 scsi_get_resid(cmd));
	}

	lpfc_update_stats(vport, lpfc_cmd);
	if (vport->cfg_max_scsicmpl_time &&
	   time_after(jiffies, lpfc_cmd->start_time +
		msecs_to_jiffies(vport->cfg_max_scsicmpl_time))) {
		spin_lock_irqsave(shost->host_lock, flags);
		if (pnode && NLP_CHK_NODE_ACT(pnode)) {
			if (pnode->cmd_qdepth >
				atomic_read(&pnode->cmd_pending) &&
				(atomic_read(&pnode->cmd_pending) >
				LPFC_MIN_TGT_QDEPTH) &&
				((cmd->cmnd[0] == READ_10) ||
				(cmd->cmnd[0] == WRITE_10)))
				pnode->cmd_qdepth =
					atomic_read(&pnode->cmd_pending);

			pnode->last_change_time = jiffies;
		}
		spin_unlock_irqrestore(shost->host_lock, flags);
	}
	lpfc_scsi_unprep_dma_buf(phba, lpfc_cmd);

	lpfc_cmd->pCmd = NULL;
	spin_unlock(&lpfc_cmd->buf_lock);

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (lpfc_cmd->ts_cmd_start) {
		lpfc_cmd->ts_isr_cmpl = pIocbIn->isr_timestamp;
		lpfc_cmd->ts_data_io = ktime_get_ns();
		phba->ktime_last_cmd = lpfc_cmd->ts_data_io;
		lpfc_io_ktime(phba, lpfc_cmd);
	}
#endif
	/* The sdev is not guaranteed to be valid post scsi_done upcall. */
	cmd->scsi_done(cmd);

	/*
	 * If there is an abort thread waiting for command completion
	 * wake up the thread.
	 */
	spin_lock(&lpfc_cmd->buf_lock);
	lpfc_cmd->cur_iocbq.iocb_flag &= ~LPFC_DRIVER_ABORTED;
	if (lpfc_cmd->waitq)
		wake_up(lpfc_cmd->waitq);
	spin_unlock(&lpfc_cmd->buf_lock);

	lpfc_release_scsi_buf(phba, lpfc_cmd);
}

/**
 * lpfc_fcpcmd_to_iocb - copy the fcp_cmd data into the IOCB
 * @data: A pointer to the immediate command data portion of the IOCB.
 * @fcp_cmnd: The FCP Command that is provided by the SCSI layer.
 *
 * The routine copies the entire FCP command from @fcp_cmnd to @data while
 * byte swapping the data to big endian format for transmission on the wire.
 **/
static void
lpfc_fcpcmd_to_iocb(uint8_t *data, struct fcp_cmnd *fcp_cmnd)
{
	int i, j;
	for (i = 0, j = 0; i < sizeof(struct fcp_cmnd);
	     i += sizeof(uint32_t), j++) {
		((uint32_t *)data)[j] = cpu_to_be32(((uint32_t *)fcp_cmnd)[j]);
	}
}

/**
 * lpfc_scsi_prep_cmnd - Wrapper func for convert scsi cmnd to FCP info unit
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: The scsi command which needs to send.
 * @pnode: Pointer to lpfc_nodelist.
 *
 * This routine initializes fcp_cmnd and iocb data structure from scsi command
 * to transfer for device with SLI3 interface spec.
 **/
static void
lpfc_scsi_prep_cmnd(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd,
		    struct lpfc_nodelist *pnode)
{
	struct lpfc_hba *phba = vport->phba;
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	struct lpfc_iocbq *piocbq = &(lpfc_cmd->cur_iocbq);
	struct lpfc_sli4_hdw_queue *hdwq = NULL;
	int datadir = scsi_cmnd->sc_data_direction;
	int idx;
	uint8_t *ptr;
	bool sli4;
	uint32_t fcpdl;

	if (!pnode || !NLP_CHK_NODE_ACT(pnode))
		return;

	lpfc_cmd->fcp_rsp->rspSnsLen = 0;
	/* clear task management bits */
	lpfc_cmd->fcp_cmnd->fcpCntl2 = 0;

	int_to_scsilun(lpfc_cmd->pCmd->device->lun,
			&lpfc_cmd->fcp_cmnd->fcp_lun);

	ptr = &fcp_cmnd->fcpCdb[0];
	memcpy(ptr, scsi_cmnd->cmnd, scsi_cmnd->cmd_len);
	if (scsi_cmnd->cmd_len < LPFC_FCP_CDB_LEN) {
		ptr += scsi_cmnd->cmd_len;
		memset(ptr, 0, (LPFC_FCP_CDB_LEN - scsi_cmnd->cmd_len));
	}

	fcp_cmnd->fcpCntl1 = SIMPLE_Q;

	sli4 = (phba->sli_rev == LPFC_SLI_REV4);
	piocbq->iocb.un.fcpi.fcpi_XRdy = 0;
	idx = lpfc_cmd->hdwq_no;
	if (phba->sli4_hba.hdwq)
		hdwq = &phba->sli4_hba.hdwq[idx];

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	if (scsi_sg_count(scsi_cmnd)) {
		if (datadir == DMA_TO_DEVICE) {
			iocb_cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
			iocb_cmd->ulpPU = PARM_READ_CHECK;
			if (vport->cfg_first_burst_size &&
			    (pnode->nlp_flag & NLP_FIRSTBURST)) {
				fcpdl = scsi_bufflen(scsi_cmnd);
				if (fcpdl < vport->cfg_first_burst_size)
					piocbq->iocb.un.fcpi.fcpi_XRdy = fcpdl;
				else
					piocbq->iocb.un.fcpi.fcpi_XRdy =
						vport->cfg_first_burst_size;
			}
			fcp_cmnd->fcpCntl3 = WRITE_DATA;
			if (hdwq)
				hdwq->scsi_cstat.output_requests++;
		} else {
			iocb_cmd->ulpCommand = CMD_FCP_IREAD64_CR;
			iocb_cmd->ulpPU = PARM_READ_CHECK;
			fcp_cmnd->fcpCntl3 = READ_DATA;
			if (hdwq)
				hdwq->scsi_cstat.input_requests++;
		}
	} else {
		iocb_cmd->ulpCommand = CMD_FCP_ICMND64_CR;
		iocb_cmd->un.fcpi.fcpi_parm = 0;
		iocb_cmd->ulpPU = 0;
		fcp_cmnd->fcpCntl3 = 0;
		if (hdwq)
			hdwq->scsi_cstat.control_requests++;
	}
	if (phba->sli_rev == 3 &&
	    !(phba->sli3_options & LPFC_SLI3_BG_ENABLED))
		lpfc_fcpcmd_to_iocb(iocb_cmd->unsli3.fcp_ext.icd, fcp_cmnd);
	/*
	 * Finish initializing those IOCB fields that are independent
	 * of the scsi_cmnd request_buffer
	 */
	piocbq->iocb.ulpContext = pnode->nlp_rpi;
	if (sli4)
		piocbq->iocb.ulpContext =
		  phba->sli4_hba.rpi_ids[pnode->nlp_rpi];
	if (pnode->nlp_fcp_info & NLP_FCP_2_DEVICE)
		piocbq->iocb.ulpFCP2Rcvy = 1;
	else
		piocbq->iocb.ulpFCP2Rcvy = 0;

	piocbq->iocb.ulpClass = (pnode->nlp_fcp_info & 0x0f);
	piocbq->context1  = lpfc_cmd;
	piocbq->iocb_cmpl = lpfc_scsi_cmd_iocb_cmpl;
	piocbq->iocb.ulpTimeout = lpfc_cmd->timeout;
	piocbq->vport = vport;
}

/**
 * lpfc_scsi_prep_task_mgmt_cmd - Convert SLI3 scsi TM cmd to FCP info unit
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: Pointer to lpfc_io_buf data structure.
 * @lun: Logical unit number.
 * @task_mgmt_cmd: SCSI task management command.
 *
 * This routine creates FCP information unit corresponding to @task_mgmt_cmd
 * for device with SLI-3 interface spec.
 *
 * Return codes:
 *   0 - Error
 *   1 - Success
 **/
static int
lpfc_scsi_prep_task_mgmt_cmd(struct lpfc_vport *vport,
			     struct lpfc_io_buf *lpfc_cmd,
			     uint64_t lun,
			     uint8_t task_mgmt_cmd)
{
	struct lpfc_iocbq *piocbq;
	IOCB_t *piocb;
	struct fcp_cmnd *fcp_cmnd;
	struct lpfc_rport_data *rdata = lpfc_cmd->rdata;
	struct lpfc_nodelist *ndlp = rdata->pnode;

	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp) ||
	    ndlp->nlp_state != NLP_STE_MAPPED_NODE)
		return 0;

	piocbq = &(lpfc_cmd->cur_iocbq);
	piocbq->vport = vport;

	piocb = &piocbq->iocb;

	fcp_cmnd = lpfc_cmd->fcp_cmnd;
	/* Clear out any old data in the FCP command area */
	memset(fcp_cmnd, 0, sizeof(struct fcp_cmnd));
	int_to_scsilun(lun, &fcp_cmnd->fcp_lun);
	fcp_cmnd->fcpCntl2 = task_mgmt_cmd;
	if (vport->phba->sli_rev == 3 &&
	    !(vport->phba->sli3_options & LPFC_SLI3_BG_ENABLED))
		lpfc_fcpcmd_to_iocb(piocb->unsli3.fcp_ext.icd, fcp_cmnd);
	piocb->ulpCommand = CMD_FCP_ICMND64_CR;
	piocb->ulpContext = ndlp->nlp_rpi;
	if (vport->phba->sli_rev == LPFC_SLI_REV4) {
		piocb->ulpContext =
		  vport->phba->sli4_hba.rpi_ids[ndlp->nlp_rpi];
	}
	piocb->ulpFCP2Rcvy = (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) ? 1 : 0;
	piocb->ulpClass = (ndlp->nlp_fcp_info & 0x0f);
	piocb->ulpPU = 0;
	piocb->un.fcpi.fcpi_parm = 0;

	/* ulpTimeout is only one byte */
	if (lpfc_cmd->timeout > 0xff) {
		/*
		 * Do not timeout the command at the firmware level.
		 * The driver will provide the timeout mechanism.
		 */
		piocb->ulpTimeout = 0;
	} else
		piocb->ulpTimeout = lpfc_cmd->timeout;

	if (vport->phba->sli_rev == LPFC_SLI_REV4)
		lpfc_sli4_set_rsp_sgl_last(vport->phba, lpfc_cmd);

	return 1;
}

/**
 * lpfc_scsi_api_table_setup - Set up scsi api function jump table
 * @phba: The hba struct for which this call is being executed.
 * @dev_grp: The HBA PCI-Device group number.
 *
 * This routine sets up the SCSI interface API function jump table in @phba
 * struct.
 * Returns: 0 - success, -ENODEV - failure.
 **/
int
lpfc_scsi_api_table_setup(struct lpfc_hba *phba, uint8_t dev_grp)
{

	phba->lpfc_scsi_unprep_dma_buf = lpfc_scsi_unprep_dma_buf;
	phba->lpfc_scsi_prep_cmnd = lpfc_scsi_prep_cmnd;

	switch (dev_grp) {
	case LPFC_PCI_DEV_LP:
		phba->lpfc_scsi_prep_dma_buf = lpfc_scsi_prep_dma_buf_s3;
		phba->lpfc_bg_scsi_prep_dma_buf = lpfc_bg_scsi_prep_dma_buf_s3;
		phba->lpfc_release_scsi_buf = lpfc_release_scsi_buf_s3;
		phba->lpfc_get_scsi_buf = lpfc_get_scsi_buf_s3;
		break;
	case LPFC_PCI_DEV_OC:
		phba->lpfc_scsi_prep_dma_buf = lpfc_scsi_prep_dma_buf_s4;
		phba->lpfc_bg_scsi_prep_dma_buf = lpfc_bg_scsi_prep_dma_buf_s4;
		phba->lpfc_release_scsi_buf = lpfc_release_scsi_buf_s4;
		phba->lpfc_get_scsi_buf = lpfc_get_scsi_buf_s4;
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"1418 Invalid HBA PCI-device group: 0x%x\n",
				dev_grp);
		return -ENODEV;
		break;
	}
	phba->lpfc_rampdown_queue_depth = lpfc_rampdown_queue_depth;
	phba->lpfc_scsi_cmd_iocb_cmpl = lpfc_scsi_cmd_iocb_cmpl;
	return 0;
}

/**
 * lpfc_taskmgmt_def_cmpl - IOCB completion routine for task management command
 * @phba: The Hba for which this call is being executed.
 * @cmdiocbq: Pointer to lpfc_iocbq data structure.
 * @rspiocbq: Pointer to lpfc_iocbq data structure.
 *
 * This routine is IOCB completion routine for device reset and target reset
 * routine. This routine release scsi buffer associated with lpfc_cmd.
 **/
static void
lpfc_tskmgmt_def_cmpl(struct lpfc_hba *phba,
			struct lpfc_iocbq *cmdiocbq,
			struct lpfc_iocbq *rspiocbq)
{
	struct lpfc_io_buf *lpfc_cmd =
		(struct lpfc_io_buf *) cmdiocbq->context1;
	if (lpfc_cmd)
		lpfc_release_scsi_buf(phba, lpfc_cmd);
	return;
}

/**
 * lpfc_check_pci_resettable - Walks list of devices on pci_dev's bus to check
 *                             if issuing a pci_bus_reset is possibly unsafe
 * @phba: lpfc_hba pointer.
 *
 * Description:
 * Walks the bus_list to ensure only PCI devices with Emulex
 * vendor id, device ids that support hot reset, and only one occurrence
 * of function 0.
 *
 * Returns:
 * -EBADSLT,  detected invalid device
 *      0,    successful
 */
int
lpfc_check_pci_resettable(const struct lpfc_hba *phba)
{
	const struct pci_dev *pdev = phba->pcidev;
	struct pci_dev *ptr = NULL;
	u8 counter = 0;

	/* Walk the list of devices on the pci_dev's bus */
	list_for_each_entry(ptr, &pdev->bus->devices, bus_list) {
		/* Check for Emulex Vendor ID */
		if (ptr->vendor != PCI_VENDOR_ID_EMULEX) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"8346 Non-Emulex vendor found: "
					"0x%04x\n", ptr->vendor);
			return -EBADSLT;
		}

		/* Check for valid Emulex Device ID */
		switch (ptr->device) {
		case PCI_DEVICE_ID_LANCER_FC:
		case PCI_DEVICE_ID_LANCER_G6_FC:
		case PCI_DEVICE_ID_LANCER_G7_FC:
			break;
		default:
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"8347 Invalid device found: "
					"0x%04x\n", ptr->device);
			return -EBADSLT;
		}

		/* Check for only one function 0 ID to ensure only one HBA on
		 * secondary bus
		 */
		if (ptr->devfn == 0) {
			if (++counter > 1) {
				lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
						"8348 More than one device on "
						"secondary bus found\n");
				return -EBADSLT;
			}
		}
	}

	return 0;
}

/**
 * lpfc_info - Info entry point of scsi_host_template data structure
 * @host: The scsi host for which this call is being executed.
 *
 * This routine provides module information about hba.
 *
 * Reutrn code:
 *   Pointer to char - Success.
 **/
const char *
lpfc_info(struct Scsi_Host *host)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int link_speed = 0;
	static char lpfcinfobuf[384];
	char tmp[384] = {0};

	memset(lpfcinfobuf, 0, sizeof(lpfcinfobuf));
	if (phba && phba->pcidev){
		/* Model Description */
		scnprintf(tmp, sizeof(tmp), phba->ModelDesc);
		if (strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf)) >=
		    sizeof(lpfcinfobuf))
			goto buffer_done;

		/* PCI Info */
		scnprintf(tmp, sizeof(tmp),
			  " on PCI bus %02x device %02x irq %d",
			  phba->pcidev->bus->number, phba->pcidev->devfn,
			  phba->pcidev->irq);
		if (strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf)) >=
		    sizeof(lpfcinfobuf))
			goto buffer_done;

		/* Port Number */
		if (phba->Port[0]) {
			scnprintf(tmp, sizeof(tmp), " port %s", phba->Port);
			if (strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf)) >=
			    sizeof(lpfcinfobuf))
				goto buffer_done;
		}

		/* Link Speed */
		link_speed = lpfc_sli_port_speed_get(phba);
		if (link_speed != 0) {
			scnprintf(tmp, sizeof(tmp),
				  " Logical Link Speed: %d Mbps", link_speed);
			if (strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf)) >=
			    sizeof(lpfcinfobuf))
				goto buffer_done;
		}

		/* PCI resettable */
		if (!lpfc_check_pci_resettable(phba)) {
			scnprintf(tmp, sizeof(tmp), " PCI resettable");
			strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf));
		}
	}

buffer_done:
	return lpfcinfobuf;
}

/**
 * lpfc_poll_rearm_time - Routine to modify fcp_poll timer of hba
 * @phba: The Hba for which this call is being executed.
 *
 * This routine modifies fcp_poll_timer  field of @phba by cfg_poll_tmo.
 * The default value of cfg_poll_tmo is 10 milliseconds.
 **/
static __inline__ void lpfc_poll_rearm_timer(struct lpfc_hba * phba)
{
	unsigned long  poll_tmo_expires =
		(jiffies + msecs_to_jiffies(phba->cfg_poll_tmo));

	if (!list_empty(&phba->sli.sli3_ring[LPFC_FCP_RING].txcmplq))
		mod_timer(&phba->fcp_poll_timer,
			  poll_tmo_expires);
}

/**
 * lpfc_poll_start_timer - Routine to start fcp_poll_timer of HBA
 * @phba: The Hba for which this call is being executed.
 *
 * This routine starts the fcp_poll_timer of @phba.
 **/
void lpfc_poll_start_timer(struct lpfc_hba * phba)
{
	lpfc_poll_rearm_timer(phba);
}

/**
 * lpfc_poll_timeout - Restart polling timer
 * @ptr: Map to lpfc_hba data structure pointer.
 *
 * This routine restarts fcp_poll timer, when FCP ring  polling is enable
 * and FCP Ring interrupt is disable.
 **/

void lpfc_poll_timeout(struct timer_list *t)
{
	struct lpfc_hba *phba = from_timer(phba, t, fcp_poll_timer);

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_handle_fast_ring_event(phba,
			&phba->sli.sli3_ring[LPFC_FCP_RING], HA_R0RE_REQ);

		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}
}

/**
 * lpfc_queuecommand - scsi_host_template queuecommand entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 * @done: Pointer to done routine.
 *
 * Driver registers this routine to scsi midlayer to submit a @cmd to process.
 * This routine prepares an IOCB from scsi command and provides to firmware.
 * The @done callback is invoked after driver finished processing the command.
 *
 * Return value :
 *   0 - Success
 *   SCSI_MLQUEUE_HOST_BUSY - Block all devices served by this host temporarily.
 **/
static int
lpfc_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *cmnd)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *ndlp;
	struct lpfc_io_buf *lpfc_cmd;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));
	int err, idx;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint64_t start = 0L;

	if (phba->ktime_on)
		start = ktime_get_ns();
#endif

	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);

	/* sanity check on references */
	if (unlikely(!rdata) || unlikely(!rport))
		goto out_fail_command;

	err = fc_remote_port_chkready(rport);
	if (err) {
		cmnd->result = err;
		goto out_fail_command;
	}
	ndlp = rdata->pnode;

	if ((scsi_get_prot_op(cmnd) != SCSI_PROT_NORMAL) &&
		(!(phba->sli3_options & LPFC_SLI3_BG_ENABLED))) {

		lpfc_printf_log(phba, KERN_ERR, LOG_BG,
				"9058 BLKGRD: ERROR: rcvd protected cmd:%02x"
				" op:%02x str=%s without registering for"
				" BlockGuard - Rejecting command\n",
				cmnd->cmnd[0], scsi_get_prot_op(cmnd),
				dif_op_str[scsi_get_prot_op(cmnd)]);
		goto out_fail_command;
	}

	/*
	 * Catch race where our node has transitioned, but the
	 * transport is still transitioning.
	 */
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp))
		goto out_tgt_busy;
	if (lpfc_ndlp_check_qdepth(phba, ndlp)) {
		if (atomic_read(&ndlp->cmd_pending) >= ndlp->cmd_qdepth) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP_ERROR,
					 "3377 Target Queue Full, scsi Id:%d "
					 "Qdepth:%d Pending command:%d"
					 " WWNN:%02x:%02x:%02x:%02x:"
					 "%02x:%02x:%02x:%02x, "
					 " WWPN:%02x:%02x:%02x:%02x:"
					 "%02x:%02x:%02x:%02x",
					 ndlp->nlp_sid, ndlp->cmd_qdepth,
					 atomic_read(&ndlp->cmd_pending),
					 ndlp->nlp_nodename.u.wwn[0],
					 ndlp->nlp_nodename.u.wwn[1],
					 ndlp->nlp_nodename.u.wwn[2],
					 ndlp->nlp_nodename.u.wwn[3],
					 ndlp->nlp_nodename.u.wwn[4],
					 ndlp->nlp_nodename.u.wwn[5],
					 ndlp->nlp_nodename.u.wwn[6],
					 ndlp->nlp_nodename.u.wwn[7],
					 ndlp->nlp_portname.u.wwn[0],
					 ndlp->nlp_portname.u.wwn[1],
					 ndlp->nlp_portname.u.wwn[2],
					 ndlp->nlp_portname.u.wwn[3],
					 ndlp->nlp_portname.u.wwn[4],
					 ndlp->nlp_portname.u.wwn[5],
					 ndlp->nlp_portname.u.wwn[6],
					 ndlp->nlp_portname.u.wwn[7]);
			goto out_tgt_busy;
		}
	}

	lpfc_cmd = lpfc_get_scsi_buf(phba, ndlp, cmnd);
	if (lpfc_cmd == NULL) {
		lpfc_rampdown_queue_depth(phba);

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP_ERROR,
				 "0707 driver's buffer pool is empty, "
				 "IO busied\n");
		goto out_host_busy;
	}

	/*
	 * Store the midlayer's command structure for the completion phase
	 * and complete the command initialization.
	 */
	lpfc_cmd->pCmd  = cmnd;
	lpfc_cmd->rdata = rdata;
	lpfc_cmd->ndlp = ndlp;
	cmnd->host_scribble = (unsigned char *)lpfc_cmd;

	if (scsi_get_prot_op(cmnd) != SCSI_PROT_NORMAL) {
		if (vport->phba->cfg_enable_bg) {
			lpfc_printf_vlog(vport,
					 KERN_INFO, LOG_SCSI_CMD,
					 "9033 BLKGRD: rcvd %s cmd:x%x "
					 "sector x%llx cnt %u pt %x\n",
					 dif_op_str[scsi_get_prot_op(cmnd)],
					 cmnd->cmnd[0],
					 (unsigned long long)scsi_get_lba(cmnd),
					 blk_rq_sectors(cmnd->request),
					 (cmnd->cmnd[1]>>5));
		}
		err = lpfc_bg_scsi_prep_dma_buf(phba, lpfc_cmd);
	} else {
		if (vport->phba->cfg_enable_bg) {
			lpfc_printf_vlog(vport,
					 KERN_INFO, LOG_SCSI_CMD,
					 "9038 BLKGRD: rcvd PROT_NORMAL cmd: "
					 "x%x sector x%llx cnt %u pt %x\n",
					 cmnd->cmnd[0],
					 (unsigned long long)scsi_get_lba(cmnd),
					 blk_rq_sectors(cmnd->request),
					 (cmnd->cmnd[1]>>5));
		}
		err = lpfc_scsi_prep_dma_buf(phba, lpfc_cmd);
	}

	if (unlikely(err)) {
		if (err == 2) {
			cmnd->result = DID_ERROR << 16;
			goto out_fail_command_release_buf;
		}
		goto out_host_busy_free_buf;
	}

	lpfc_scsi_prep_cmnd(vport, lpfc_cmd, ndlp);

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (unlikely(phba->hdwqstat_on & LPFC_CHECK_SCSI_IO))
		this_cpu_inc(phba->sli4_hba.c_stat->xmt_io);
#endif
	err = lpfc_sli_issue_iocb(phba, LPFC_FCP_RING,
				  &lpfc_cmd->cur_iocbq, SLI_IOCB_RET_IOCB);
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (start) {
		lpfc_cmd->ts_cmd_start = start;
		lpfc_cmd->ts_last_cmd = phba->ktime_last_cmd;
		lpfc_cmd->ts_cmd_wqput = ktime_get_ns();
	} else {
		lpfc_cmd->ts_cmd_start = 0;
	}
#endif
	if (err) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "3376 FCP could not issue IOCB err %x"
				 "FCP cmd x%x <%d/%llu> "
				 "sid: x%x did: x%x oxid: x%x "
				 "Data: x%x x%x x%x x%x\n",
				 err, cmnd->cmnd[0],
				 cmnd->device ? cmnd->device->id : 0xffff,
				 cmnd->device ? cmnd->device->lun : (u64) -1,
				 vport->fc_myDID, ndlp->nlp_DID,
				 phba->sli_rev == LPFC_SLI_REV4 ?
				 lpfc_cmd->cur_iocbq.sli4_xritag : 0xffff,
				 lpfc_cmd->cur_iocbq.iocb.ulpContext,
				 lpfc_cmd->cur_iocbq.iocb.ulpIoTag,
				 lpfc_cmd->cur_iocbq.iocb.ulpTimeout,
				 (uint32_t)
				 (cmnd->request->timeout / 1000));

		goto out_host_busy_free_buf;
	}
	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_handle_fast_ring_event(phba,
			&phba->sli.sli3_ring[LPFC_FCP_RING], HA_R0RE_REQ);

		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}

	if (phba->cfg_xri_rebalancing)
		lpfc_keep_pvt_pool_above_lowwm(phba, lpfc_cmd->hdwq_no);

	return 0;

 out_host_busy_free_buf:
	idx = lpfc_cmd->hdwq_no;
	lpfc_scsi_unprep_dma_buf(phba, lpfc_cmd);
	if (phba->sli4_hba.hdwq) {
		switch (lpfc_cmd->fcp_cmnd->fcpCntl3) {
		case WRITE_DATA:
			phba->sli4_hba.hdwq[idx].scsi_cstat.output_requests--;
			break;
		case READ_DATA:
			phba->sli4_hba.hdwq[idx].scsi_cstat.input_requests--;
			break;
		default:
			phba->sli4_hba.hdwq[idx].scsi_cstat.control_requests--;
		}
	}
	lpfc_release_scsi_buf(phba, lpfc_cmd);
 out_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

 out_tgt_busy:
	return SCSI_MLQUEUE_TARGET_BUSY;

 out_fail_command_release_buf:
	lpfc_release_scsi_buf(phba, lpfc_cmd);

 out_fail_command:
	cmnd->scsi_done(cmnd);
	return 0;
}


/**
 * lpfc_abort_handler - scsi_host_template eh_abort_handler entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine aborts @cmnd pending in base driver.
 *
 * Return code :
 *   0x2003 - Error
 *   0x2002 - Success
 **/
static int
lpfc_abort_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_iocbq *iocb;
	struct lpfc_iocbq *abtsiocb;
	struct lpfc_io_buf *lpfc_cmd;
	IOCB_t *cmd, *icmd;
	int ret = SUCCESS, status = 0;
	struct lpfc_sli_ring *pring_s4 = NULL;
	int ret_val;
	unsigned long flags;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(waitq);

	status = fc_block_scsi_eh(cmnd);
	if (status != 0 && status != SUCCESS)
		return status;

	lpfc_cmd = (struct lpfc_io_buf *)cmnd->host_scribble;
	if (!lpfc_cmd)
		return ret;

	spin_lock_irqsave(&phba->hbalock, flags);
	/* driver queued commands are in process of being flushed */
	if (phba->hba_flag & HBA_IOQ_FLUSH) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			"3168 SCSI Layer abort requested I/O has been "
			"flushed by LLD.\n");
		ret = FAILED;
		goto out_unlock;
	}

	/* Guard against IO completion being called at same time */
	spin_lock(&lpfc_cmd->buf_lock);

	if (!lpfc_cmd->pCmd) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			 "2873 SCSI Layer I/O Abort Request IO CMPL Status "
			 "x%x ID %d LUN %llu\n",
			 SUCCESS, cmnd->device->id, cmnd->device->lun);
		goto out_unlock_buf;
	}

	iocb = &lpfc_cmd->cur_iocbq;
	if (phba->sli_rev == LPFC_SLI_REV4) {
		pring_s4 = phba->sli4_hba.hdwq[iocb->hba_wqidx].io_wq->pring;
		if (!pring_s4) {
			ret = FAILED;
			goto out_unlock_buf;
		}
		spin_lock(&pring_s4->ring_lock);
	}
	/* the command is in process of being cancelled */
	if (!(iocb->iocb_flag & LPFC_IO_ON_TXCMPLQ)) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			"3169 SCSI Layer abort requested I/O has been "
			"cancelled by LLD.\n");
		ret = FAILED;
		goto out_unlock_ring;
	}
	/*
	 * If pCmd field of the corresponding lpfc_io_buf structure
	 * points to a different SCSI command, then the driver has
	 * already completed this command, but the midlayer did not
	 * see the completion before the eh fired. Just return SUCCESS.
	 */
	if (lpfc_cmd->pCmd != cmnd) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			"3170 SCSI Layer abort requested I/O has been "
			"completed by LLD.\n");
		goto out_unlock_ring;
	}

	BUG_ON(iocb->context1 != lpfc_cmd);

	/* abort issued in recovery is still in progress */
	if (iocb->iocb_flag & LPFC_DRIVER_ABORTED) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			 "3389 SCSI Layer I/O Abort Request is pending\n");
		if (phba->sli_rev == LPFC_SLI_REV4)
			spin_unlock(&pring_s4->ring_lock);
		spin_unlock(&lpfc_cmd->buf_lock);
		spin_unlock_irqrestore(&phba->hbalock, flags);
		goto wait_for_cmpl;
	}

	abtsiocb = __lpfc_sli_get_iocbq(phba);
	if (abtsiocb == NULL) {
		ret = FAILED;
		goto out_unlock_ring;
	}

	/* Indicate the IO is being aborted by the driver. */
	iocb->iocb_flag |= LPFC_DRIVER_ABORTED;

	/*
	 * The scsi command can not be in txq and it is in flight because the
	 * pCmd is still pointig at the SCSI command we have to abort. There
	 * is no need to search the txcmplq. Just send an abort to the FW.
	 */

	cmd = &iocb->iocb;
	icmd = &abtsiocb->iocb;
	icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
	icmd->un.acxri.abortContextTag = cmd->ulpContext;
	if (phba->sli_rev == LPFC_SLI_REV4)
		icmd->un.acxri.abortIoTag = iocb->sli4_xritag;
	else
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

	icmd->ulpLe = 1;
	icmd->ulpClass = cmd->ulpClass;

	/* ABTS WQE must go to the same WQ as the WQE to be aborted */
	abtsiocb->hba_wqidx = iocb->hba_wqidx;
	abtsiocb->iocb_flag |= LPFC_USE_FCPWQIDX;
	if (iocb->iocb_flag & LPFC_IO_FOF)
		abtsiocb->iocb_flag |= LPFC_IO_FOF;

	if (lpfc_is_link_up(phba))
		icmd->ulpCommand = CMD_ABORT_XRI_CN;
	else
		icmd->ulpCommand = CMD_CLOSE_XRI_CN;

	abtsiocb->iocb_cmpl = lpfc_sli_abort_fcp_cmpl;
	abtsiocb->vport = vport;
	lpfc_cmd->waitq = &waitq;
	if (phba->sli_rev == LPFC_SLI_REV4) {
		/* Note: both hbalock and ring_lock must be set here */
		ret_val = __lpfc_sli_issue_iocb(phba, pring_s4->ringno,
						abtsiocb, 0);
		spin_unlock(&pring_s4->ring_lock);
	} else {
		ret_val = __lpfc_sli_issue_iocb(phba, LPFC_FCP_RING,
						abtsiocb, 0);
	}

	if (ret_val == IOCB_ERROR) {
		/* Indicate the IO is not being aborted by the driver. */
		iocb->iocb_flag &= ~LPFC_DRIVER_ABORTED;
		lpfc_cmd->waitq = NULL;
		spin_unlock(&lpfc_cmd->buf_lock);
		spin_unlock_irqrestore(&phba->hbalock, flags);
		lpfc_sli_release_iocbq(phba, abtsiocb);
		ret = FAILED;
		goto out;
	}

	/* no longer need the lock after this point */
	spin_unlock(&lpfc_cmd->buf_lock);
	spin_unlock_irqrestore(&phba->hbalock, flags);

	if (phba->cfg_poll & DISABLE_FCP_RING_INT)
		lpfc_sli_handle_fast_ring_event(phba,
			&phba->sli.sli3_ring[LPFC_FCP_RING], HA_R0RE_REQ);

wait_for_cmpl:
	/* Wait for abort to complete */
	wait_event_timeout(waitq,
			  (lpfc_cmd->pCmd != cmnd),
			   msecs_to_jiffies(2*vport->cfg_devloss_tmo*1000));

	spin_lock(&lpfc_cmd->buf_lock);

	if (lpfc_cmd->pCmd == cmnd) {
		ret = FAILED;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0748 abort handler timed out waiting "
				 "for aborting I/O (xri:x%x) to complete: "
				 "ret %#x, ID %d, LUN %llu\n",
				 iocb->sli4_xritag, ret,
				 cmnd->device->id, cmnd->device->lun);
	}

	lpfc_cmd->waitq = NULL;

	spin_unlock(&lpfc_cmd->buf_lock);
	goto out;

out_unlock_ring:
	if (phba->sli_rev == LPFC_SLI_REV4)
		spin_unlock(&pring_s4->ring_lock);
out_unlock_buf:
	spin_unlock(&lpfc_cmd->buf_lock);
out_unlock:
	spin_unlock_irqrestore(&phba->hbalock, flags);
out:
	lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			 "0749 SCSI Layer I/O Abort Request Status x%x ID %d "
			 "LUN %llu\n", ret, cmnd->device->id,
			 cmnd->device->lun);
	return ret;
}

static char *
lpfc_taskmgmt_name(uint8_t task_mgmt_cmd)
{
	switch (task_mgmt_cmd) {
	case FCP_ABORT_TASK_SET:
		return "ABORT_TASK_SET";
	case FCP_CLEAR_TASK_SET:
		return "FCP_CLEAR_TASK_SET";
	case FCP_BUS_RESET:
		return "FCP_BUS_RESET";
	case FCP_LUN_RESET:
		return "FCP_LUN_RESET";
	case FCP_TARGET_RESET:
		return "FCP_TARGET_RESET";
	case FCP_CLEAR_ACA:
		return "FCP_CLEAR_ACA";
	case FCP_TERMINATE_TASK:
		return "FCP_TERMINATE_TASK";
	default:
		return "unknown";
	}
}


/**
 * lpfc_check_fcp_rsp - check the returned fcp_rsp to see if task failed
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: Pointer to lpfc_io_buf data structure.
 *
 * This routine checks the FCP RSP INFO to see if the tsk mgmt command succeded
 *
 * Return code :
 *   0x2003 - Error
 *   0x2002 - Success
 **/
static int
lpfc_check_fcp_rsp(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd)
{
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	uint32_t rsp_info;
	uint32_t rsp_len;
	uint8_t  rsp_info_code;
	int ret = FAILED;


	if (fcprsp == NULL)
		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "0703 fcp_rsp is missing\n");
	else {
		rsp_info = fcprsp->rspStatus2;
		rsp_len = be32_to_cpu(fcprsp->rspRspLen);
		rsp_info_code = fcprsp->rspInfo3;


		lpfc_printf_vlog(vport, KERN_INFO,
				 LOG_FCP,
				 "0706 fcp_rsp valid 0x%x,"
				 " rsp len=%d code 0x%x\n",
				 rsp_info,
				 rsp_len, rsp_info_code);

		/* If FCP_RSP_LEN_VALID bit is one, then the FCP_RSP_LEN
		 * field specifies the number of valid bytes of FCP_RSP_INFO.
		 * The FCP_RSP_LEN field shall be set to 0x04 or 0x08
		 */
		if ((fcprsp->rspStatus2 & RSP_LEN_VALID) &&
		    ((rsp_len == 8) || (rsp_len == 4))) {
			switch (rsp_info_code) {
			case RSP_NO_FAILURE:
				lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
						 "0715 Task Mgmt No Failure\n");
				ret = SUCCESS;
				break;
			case RSP_TM_NOT_SUPPORTED: /* TM rejected */
				lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
						 "0716 Task Mgmt Target "
						"reject\n");
				break;
			case RSP_TM_NOT_COMPLETED: /* TM failed */
				lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
						 "0717 Task Mgmt Target "
						"failed TM\n");
				break;
			case RSP_TM_INVALID_LU: /* TM to invalid LU! */
				lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
						 "0718 Task Mgmt to invalid "
						"LUN\n");
				break;
			}
		}
	}
	return ret;
}


/**
 * lpfc_send_taskmgmt - Generic SCSI Task Mgmt Handler
 * @vport: The virtual port for which this call is being executed.
 * @rdata: Pointer to remote port local data
 * @tgt_id: Target ID of remote device.
 * @lun_id: Lun number for the TMF
 * @task_mgmt_cmd: type of TMF to send
 *
 * This routine builds and sends a TMF (SCSI Task Mgmt Function) to
 * a remote port.
 *
 * Return Code:
 *   0x2003 - Error
 *   0x2002 - Success.
 **/
static int
lpfc_send_taskmgmt(struct lpfc_vport *vport, struct scsi_cmnd *cmnd,
		   unsigned int tgt_id, uint64_t lun_id,
		   uint8_t task_mgmt_cmd)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_io_buf *lpfc_cmd;
	struct lpfc_iocbq *iocbq;
	struct lpfc_iocbq *iocbqrsp;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	int ret;
	int status;

	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
	if (!rdata || !rdata->pnode || !NLP_CHK_NODE_ACT(rdata->pnode))
		return FAILED;
	pnode = rdata->pnode;

	lpfc_cmd = lpfc_get_scsi_buf(phba, pnode, NULL);
	if (lpfc_cmd == NULL)
		return FAILED;
	lpfc_cmd->timeout = phba->cfg_task_mgmt_tmo;
	lpfc_cmd->rdata = rdata;
	lpfc_cmd->pCmd = cmnd;
	lpfc_cmd->ndlp = pnode;

	status = lpfc_scsi_prep_task_mgmt_cmd(vport, lpfc_cmd, lun_id,
					   task_mgmt_cmd);
	if (!status) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}

	iocbq = &lpfc_cmd->cur_iocbq;
	iocbqrsp = lpfc_sli_get_iocbq(phba);
	if (iocbqrsp == NULL) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}
	iocbq->iocb_cmpl = lpfc_tskmgmt_def_cmpl;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			 "0702 Issue %s to TGT %d LUN %llu "
			 "rpi x%x nlp_flag x%x Data: x%x x%x\n",
			 lpfc_taskmgmt_name(task_mgmt_cmd), tgt_id, lun_id,
			 pnode->nlp_rpi, pnode->nlp_flag, iocbq->sli4_xritag,
			 iocbq->iocb_flag);

	status = lpfc_sli_issue_iocb_wait(phba, LPFC_FCP_RING,
					  iocbq, iocbqrsp, lpfc_cmd->timeout);
	if ((status != IOCB_SUCCESS) ||
	    (iocbqrsp->iocb.ulpStatus != IOSTAT_SUCCESS)) {
		if (status != IOCB_SUCCESS ||
		    iocbqrsp->iocb.ulpStatus != IOSTAT_FCP_RSP_ERROR)
			lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
					 "0727 TMF %s to TGT %d LUN %llu "
					 "failed (%d, %d) iocb_flag x%x\n",
					 lpfc_taskmgmt_name(task_mgmt_cmd),
					 tgt_id, lun_id,
					 iocbqrsp->iocb.ulpStatus,
					 iocbqrsp->iocb.un.ulpWord[4],
					 iocbq->iocb_flag);
		/* if ulpStatus != IOCB_SUCCESS, then status == IOCB_SUCCESS */
		if (status == IOCB_SUCCESS) {
			if (iocbqrsp->iocb.ulpStatus == IOSTAT_FCP_RSP_ERROR)
				/* Something in the FCP_RSP was invalid.
				 * Check conditions */
				ret = lpfc_check_fcp_rsp(vport, lpfc_cmd);
			else
				ret = FAILED;
		} else if (status == IOCB_TIMEDOUT) {
			ret = TIMEOUT_ERROR;
		} else {
			ret = FAILED;
		}
	} else
		ret = SUCCESS;

	lpfc_sli_release_iocbq(phba, iocbqrsp);

	if (ret != TIMEOUT_ERROR)
		lpfc_release_scsi_buf(phba, lpfc_cmd);

	return ret;
}

/**
 * lpfc_chk_tgt_mapped -
 * @vport: The virtual port to check on
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine delays until the scsi target (aka rport) for the
 * command exists (is present and logged in) or we declare it non-existent.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_chk_tgt_mapped(struct lpfc_vport *vport, struct scsi_cmnd *cmnd)
{
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	unsigned long later;

	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
	if (!rdata) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			"0797 Tgt Map rport failure: rdata x%px\n", rdata);
		return FAILED;
	}
	pnode = rdata->pnode;
	/*
	 * If target is not in a MAPPED state, delay until
	 * target is rediscovered or devloss timeout expires.
	 */
	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies)) {
		if (!pnode || !NLP_CHK_NODE_ACT(pnode))
			return FAILED;
		if (pnode->nlp_state == NLP_STE_MAPPED_NODE)
			return SUCCESS;
		schedule_timeout_uninterruptible(msecs_to_jiffies(500));
		rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
		if (!rdata)
			return FAILED;
		pnode = rdata->pnode;
	}
	if (!pnode || !NLP_CHK_NODE_ACT(pnode) ||
	    (pnode->nlp_state != NLP_STE_MAPPED_NODE))
		return FAILED;
	return SUCCESS;
}

/**
 * lpfc_reset_flush_io_context -
 * @vport: The virtual port (scsi_host) for the flush context
 * @tgt_id: If aborting by Target contect - specifies the target id
 * @lun_id: If aborting by Lun context - specifies the lun id
 * @context: specifies the context level to flush at.
 *
 * After a reset condition via TMF, we need to flush orphaned i/o
 * contexts from the adapter. This routine aborts any contexts
 * outstanding, then waits for their completions. The wait is
 * bounded by devloss_tmo though.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_reset_flush_io_context(struct lpfc_vport *vport, uint16_t tgt_id,
			uint64_t lun_id, lpfc_ctx_cmd context)
{
	struct lpfc_hba   *phba = vport->phba;
	unsigned long later;
	int cnt;

	cnt = lpfc_sli_sum_iocb(vport, tgt_id, lun_id, context);
	if (cnt)
		lpfc_sli_abort_taskmgmt(vport,
					&phba->sli.sli3_ring[LPFC_FCP_RING],
					tgt_id, lun_id, context);
	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies) && cnt) {
		schedule_timeout_uninterruptible(msecs_to_jiffies(20));
		cnt = lpfc_sli_sum_iocb(vport, tgt_id, lun_id, context);
	}
	if (cnt) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			"0724 I/O flush failure for context %s : cnt x%x\n",
			((context == LPFC_CTX_LUN) ? "LUN" :
			 ((context == LPFC_CTX_TGT) ? "TGT" :
			  ((context == LPFC_CTX_HOST) ? "HOST" : "Unknown"))),
			cnt);
		return FAILED;
	}
	return SUCCESS;
}

/**
 * lpfc_device_reset_handler - scsi_host_template eh_device_reset entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does a device reset by sending a LUN_RESET task management
 * command.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_device_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	unsigned tgt_id = cmnd->device->id;
	uint64_t lun_id = cmnd->device->lun;
	struct lpfc_scsi_event_header scsi_event;
	int status;

	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
	if (!rdata || !rdata->pnode) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0798 Device Reset rdata failure: rdata x%px\n",
				 rdata);
		return FAILED;
	}
	pnode = rdata->pnode;
	status = fc_block_scsi_eh(cmnd);
	if (status != 0 && status != SUCCESS)
		return status;

	status = lpfc_chk_tgt_mapped(vport, cmnd);
	if (status == FAILED) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			"0721 Device Reset rport failure: rdata x%px\n", rdata);
		return FAILED;
	}

	scsi_event.event_type = FC_REG_SCSI_EVENT;
	scsi_event.subcategory = LPFC_EVENT_LUNRESET;
	scsi_event.lun = lun_id;
	memcpy(scsi_event.wwpn, &pnode->nlp_portname, sizeof(struct lpfc_name));
	memcpy(scsi_event.wwnn, &pnode->nlp_nodename, sizeof(struct lpfc_name));

	fc_host_post_vendor_event(shost, fc_get_event_number(),
		sizeof(scsi_event), (char *)&scsi_event, LPFC_NL_VENDOR_ID);

	status = lpfc_send_taskmgmt(vport, cmnd, tgt_id, lun_id,
						FCP_LUN_RESET);

	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "0713 SCSI layer issued Device Reset (%d, %llu) "
			 "return x%x\n", tgt_id, lun_id, status);

	/*
	 * We have to clean up i/o as : they may be orphaned by the TMF;
	 * or if the TMF failed, they may be in an indeterminate state.
	 * So, continue on.
	 * We will report success if all the i/o aborts successfully.
	 */
	if (status == SUCCESS)
		status = lpfc_reset_flush_io_context(vport, tgt_id, lun_id,
						LPFC_CTX_LUN);

	return status;
}

/**
 * lpfc_target_reset_handler - scsi_host_template eh_target_reset entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does a target reset by sending a TARGET_RESET task management
 * command.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_target_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	unsigned tgt_id = cmnd->device->id;
	uint64_t lun_id = cmnd->device->lun;
	struct lpfc_scsi_event_header scsi_event;
	int status;

	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
	if (!rdata || !rdata->pnode) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0799 Target Reset rdata failure: rdata x%px\n",
				 rdata);
		return FAILED;
	}
	pnode = rdata->pnode;
	status = fc_block_scsi_eh(cmnd);
	if (status != 0 && status != SUCCESS)
		return status;

	status = lpfc_chk_tgt_mapped(vport, cmnd);
	if (status == FAILED) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			"0722 Target Reset rport failure: rdata x%px\n", rdata);
		if (pnode) {
			spin_lock_irq(shost->host_lock);
			pnode->nlp_flag &= ~NLP_NPR_ADISC;
			pnode->nlp_fcp_info &= ~NLP_FCP_2_DEVICE;
			spin_unlock_irq(shost->host_lock);
		}
		lpfc_reset_flush_io_context(vport, tgt_id, lun_id,
					  LPFC_CTX_TGT);
		return FAST_IO_FAIL;
	}

	scsi_event.event_type = FC_REG_SCSI_EVENT;
	scsi_event.subcategory = LPFC_EVENT_TGTRESET;
	scsi_event.lun = 0;
	memcpy(scsi_event.wwpn, &pnode->nlp_portname, sizeof(struct lpfc_name));
	memcpy(scsi_event.wwnn, &pnode->nlp_nodename, sizeof(struct lpfc_name));

	fc_host_post_vendor_event(shost, fc_get_event_number(),
		sizeof(scsi_event), (char *)&scsi_event, LPFC_NL_VENDOR_ID);

	status = lpfc_send_taskmgmt(vport, cmnd, tgt_id, lun_id,
					FCP_TARGET_RESET);

	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "0723 SCSI layer issued Target Reset (%d, %llu) "
			 "return x%x\n", tgt_id, lun_id, status);

	/*
	 * We have to clean up i/o as : they may be orphaned by the TMF;
	 * or if the TMF failed, they may be in an indeterminate state.
	 * So, continue on.
	 * We will report success if all the i/o aborts successfully.
	 */
	if (status == SUCCESS)
		status = lpfc_reset_flush_io_context(vport, tgt_id, lun_id,
					  LPFC_CTX_TGT);
	return status;
}

/**
 * lpfc_bus_reset_handler - scsi_host_template eh_bus_reset_handler entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does target reset to all targets on @cmnd->device->host.
 * This emulates Parallel SCSI Bus Reset Semantics.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_bus_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_scsi_event_header scsi_event;
	int match;
	int ret = SUCCESS, status, i;

	scsi_event.event_type = FC_REG_SCSI_EVENT;
	scsi_event.subcategory = LPFC_EVENT_BUSRESET;
	scsi_event.lun = 0;
	memcpy(scsi_event.wwpn, &vport->fc_portname, sizeof(struct lpfc_name));
	memcpy(scsi_event.wwnn, &vport->fc_nodename, sizeof(struct lpfc_name));

	fc_host_post_vendor_event(shost, fc_get_event_number(),
		sizeof(scsi_event), (char *)&scsi_event, LPFC_NL_VENDOR_ID);

	status = fc_block_scsi_eh(cmnd);
	if (status != 0 && status != SUCCESS)
		return status;

	/*
	 * Since the driver manages a single bus device, reset all
	 * targets known to the driver.  Should any target reset
	 * fail, this routine returns failure to the midlayer.
	 */
	for (i = 0; i < LPFC_MAX_TARGET; i++) {
		/* Search for mapped node by target ID */
		match = 0;
		spin_lock_irq(shost->host_lock);
		list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
			if (!NLP_CHK_NODE_ACT(ndlp))
				continue;
			if (vport->phba->cfg_fcp2_no_tgt_reset &&
			    (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE))
				continue;
			if (ndlp->nlp_state == NLP_STE_MAPPED_NODE &&
			    ndlp->nlp_sid == i &&
			    ndlp->rport &&
			    ndlp->nlp_type & NLP_FCP_TARGET) {
				match = 1;
				break;
			}
		}
		spin_unlock_irq(shost->host_lock);
		if (!match)
			continue;

		status = lpfc_send_taskmgmt(vport, cmnd,
					i, 0, FCP_TARGET_RESET);

		if (status != SUCCESS) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
					 "0700 Bus Reset on target %d failed\n",
					 i);
			ret = FAILED;
		}
	}
	/*
	 * We have to clean up i/o as : they may be orphaned by the TMFs
	 * above; or if any of the TMFs failed, they may be in an
	 * indeterminate state.
	 * We will report success if all the i/o aborts successfully.
	 */

	status = lpfc_reset_flush_io_context(vport, 0, 0, LPFC_CTX_HOST);
	if (status != SUCCESS)
		ret = FAILED;

	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "0714 SCSI layer issued Bus Reset Data: x%x\n", ret);
	return ret;
}

/**
 * lpfc_host_reset_handler - scsi_host_template eh_host_reset_handler entry pt
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does host reset to the adaptor port. It brings the HBA
 * offline, performs a board restart, and then brings the board back online.
 * The lpfc_offline calls lpfc_sli_hba_down which will abort and local
 * reject all outstanding SCSI commands to the host and error returned
 * back to SCSI mid-level. As this will be SCSI mid-level's last resort
 * of error handling, it will only return error if resetting of the adapter
 * is not successful; in all other cases, will return success.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_host_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	int rc, ret = SUCCESS;

	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "3172 SCSI layer issued Host Reset Data:\n");

	lpfc_offline_prep(phba, LPFC_MBX_WAIT);
	lpfc_offline(phba);
	rc = lpfc_sli_brdrestart(phba);
	if (rc)
		goto error;

	rc = lpfc_online(phba);
	if (rc)
		goto error;

	lpfc_unblock_mgmt_io(phba);

	return ret;
error:
	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "3323 Failed host reset\n");
	lpfc_unblock_mgmt_io(phba);
	return FAILED;
}

/**
 * lpfc_slave_alloc - scsi_host_template slave_alloc entry point
 * @sdev: Pointer to scsi_device.
 *
 * This routine populates the cmds_per_lun count + 2 scsi_bufs into  this host's
 * globally available list of scsi buffers. This routine also makes sure scsi
 * buffer is not allocated more than HBA limit conveyed to midlayer. This list
 * of scsi buffer exists for the lifetime of the driver.
 *
 * Return codes:
 *   non-0 - Error
 *   0 - Success
 **/
static int
lpfc_slave_alloc(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) sdev->host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	uint32_t total = 0;
	uint32_t num_to_alloc = 0;
	int num_allocated = 0;
	uint32_t sdev_cnt;
	struct lpfc_device_data *device_data;
	unsigned long flags;
	struct lpfc_name target_wwpn;

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	if (phba->cfg_fof) {

		/*
		 * Check to see if the device data structure for the lun
		 * exists.  If not, create one.
		 */

		u64_to_wwn(rport->port_name, target_wwpn.u.wwn);
		spin_lock_irqsave(&phba->devicelock, flags);
		device_data = __lpfc_get_device_data(phba,
						     &phba->luns,
						     &vport->fc_portname,
						     &target_wwpn,
						     sdev->lun);
		if (!device_data) {
			spin_unlock_irqrestore(&phba->devicelock, flags);
			device_data = lpfc_create_device_data(phba,
							&vport->fc_portname,
							&target_wwpn,
							sdev->lun,
							phba->cfg_XLanePriority,
							true);
			if (!device_data)
				return -ENOMEM;
			spin_lock_irqsave(&phba->devicelock, flags);
			list_add_tail(&device_data->listentry, &phba->luns);
		}
		device_data->rport_data = rport->dd_data;
		device_data->available = true;
		spin_unlock_irqrestore(&phba->devicelock, flags);
		sdev->hostdata = device_data;
	} else {
		sdev->hostdata = rport->dd_data;
	}
	sdev_cnt = atomic_inc_return(&phba->sdev_cnt);

	/* For SLI4, all IO buffers are pre-allocated */
	if (phba->sli_rev == LPFC_SLI_REV4)
		return 0;

	/* This code path is now ONLY for SLI3 adapters */

	/*
	 * Populate the cmds_per_lun count scsi_bufs into this host's globally
	 * available list of scsi buffers.  Don't allocate more than the
	 * HBA limit conveyed to the midlayer via the host structure.  The
	 * formula accounts for the lun_queue_depth + error handlers + 1
	 * extra.  This list of scsi bufs exists for the lifetime of the driver.
	 */
	total = phba->total_scsi_bufs;
	num_to_alloc = vport->cfg_lun_queue_depth + 2;

	/* If allocated buffers are enough do nothing */
	if ((sdev_cnt * (vport->cfg_lun_queue_depth + 2)) < total)
		return 0;

	/* Allow some exchanges to be available always to complete discovery */
	if (total >= phba->cfg_hba_queue_depth - LPFC_DISC_IOCB_BUFF_COUNT ) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "0704 At limitation of %d preallocated "
				 "command buffers\n", total);
		return 0;
	/* Allow some exchanges to be available always to complete discovery */
	} else if (total + num_to_alloc >
		phba->cfg_hba_queue_depth - LPFC_DISC_IOCB_BUFF_COUNT ) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "0705 Allocation request of %d "
				 "command buffers will exceed max of %d.  "
				 "Reducing allocation request to %d.\n",
				 num_to_alloc, phba->cfg_hba_queue_depth,
				 (phba->cfg_hba_queue_depth - total));
		num_to_alloc = phba->cfg_hba_queue_depth - total;
	}
	num_allocated = lpfc_new_scsi_buf_s3(vport, num_to_alloc);
	if (num_to_alloc != num_allocated) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
					 "0708 Allocation request of %d "
					 "command buffers did not succeed.  "
					 "Allocated %d buffers.\n",
					 num_to_alloc, num_allocated);
	}
	if (num_allocated > 0)
		phba->total_scsi_bufs += num_allocated;
	return 0;
}

/**
 * lpfc_slave_configure - scsi_host_template slave_configure entry point
 * @sdev: Pointer to scsi_device.
 *
 * This routine configures following items
 *   - Tag command queuing support for @sdev if supported.
 *   - Enable SLI polling for fcp ring if ENABLE_FCP_RING_POLLING flag is set.
 *
 * Return codes:
 *   0 - Success
 **/
static int
lpfc_slave_configure(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) sdev->host->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	scsi_change_queue_depth(sdev, vport->cfg_lun_queue_depth);

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_handle_fast_ring_event(phba,
			&phba->sli.sli3_ring[LPFC_FCP_RING], HA_R0RE_REQ);
		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}

	return 0;
}

/**
 * lpfc_slave_destroy - slave_destroy entry point of SHT data structure
 * @sdev: Pointer to scsi_device.
 *
 * This routine sets @sdev hostatdata filed to null.
 **/
static void
lpfc_slave_destroy(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) sdev->host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	unsigned long flags;
	struct lpfc_device_data *device_data = sdev->hostdata;

	atomic_dec(&phba->sdev_cnt);
	if ((phba->cfg_fof) && (device_data)) {
		spin_lock_irqsave(&phba->devicelock, flags);
		device_data->available = false;
		if (!device_data->oas_enabled)
			lpfc_delete_device_data(phba, device_data);
		spin_unlock_irqrestore(&phba->devicelock, flags);
	}
	sdev->hostdata = NULL;
	return;
}

/**
 * lpfc_create_device_data - creates and initializes device data structure for OAS
 * @pha: Pointer to host bus adapter structure.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @lun: Lun on target
 * @atomic_create: Flag to indicate if memory should be allocated using the
 *		  GFP_ATOMIC flag or not.
 *
 * This routine creates a device data structure which will contain identifying
 * information for the device (host wwpn, target wwpn, lun), state of OAS,
 * whether or not the corresponding lun is available by the system,
 * and pointer to the rport data.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_device_data - Success
 **/
struct lpfc_device_data*
lpfc_create_device_data(struct lpfc_hba *phba, struct lpfc_name *vport_wwpn,
			struct lpfc_name *target_wwpn, uint64_t lun,
			uint32_t pri, bool atomic_create)
{

	struct lpfc_device_data *lun_info;
	int memory_flags;

	if (unlikely(!phba) || !vport_wwpn || !target_wwpn  ||
	    !(phba->cfg_fof))
		return NULL;

	/* Attempt to create the device data to contain lun info */

	if (atomic_create)
		memory_flags = GFP_ATOMIC;
	else
		memory_flags = GFP_KERNEL;
	lun_info = mempool_alloc(phba->device_data_mem_pool, memory_flags);
	if (!lun_info)
		return NULL;
	INIT_LIST_HEAD(&lun_info->listentry);
	lun_info->rport_data  = NULL;
	memcpy(&lun_info->device_id.vport_wwpn, vport_wwpn,
	       sizeof(struct lpfc_name));
	memcpy(&lun_info->device_id.target_wwpn, target_wwpn,
	       sizeof(struct lpfc_name));
	lun_info->device_id.lun = lun;
	lun_info->oas_enabled = false;
	lun_info->priority = pri;
	lun_info->available = false;
	return lun_info;
}

/**
 * lpfc_delete_device_data - frees a device data structure for OAS
 * @pha: Pointer to host bus adapter structure.
 * @lun_info: Pointer to device data structure to free.
 *
 * This routine frees the previously allocated device data structure passed.
 *
 **/
void
lpfc_delete_device_data(struct lpfc_hba *phba,
			struct lpfc_device_data *lun_info)
{

	if (unlikely(!phba) || !lun_info  ||
	    !(phba->cfg_fof))
		return;

	if (!list_empty(&lun_info->listentry))
		list_del(&lun_info->listentry);
	mempool_free(lun_info, phba->device_data_mem_pool);
	return;
}

/**
 * __lpfc_get_device_data - returns the device data for the specified lun
 * @pha: Pointer to host bus adapter structure.
 * @list: Point to list to search.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @lun: Lun on target
 *
 * This routine searches the list passed for the specified lun's device data.
 * This function does not hold locks, it is the responsibility of the caller
 * to ensure the proper lock is held before calling the function.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_device_data - Success
 **/
struct lpfc_device_data*
__lpfc_get_device_data(struct lpfc_hba *phba, struct list_head *list,
		       struct lpfc_name *vport_wwpn,
		       struct lpfc_name *target_wwpn, uint64_t lun)
{

	struct lpfc_device_data *lun_info;

	if (unlikely(!phba) || !list || !vport_wwpn || !target_wwpn ||
	    !phba->cfg_fof)
		return NULL;

	/* Check to see if the lun is already enabled for OAS. */

	list_for_each_entry(lun_info, list, listentry) {
		if ((memcmp(&lun_info->device_id.vport_wwpn, vport_wwpn,
			    sizeof(struct lpfc_name)) == 0) &&
		    (memcmp(&lun_info->device_id.target_wwpn, target_wwpn,
			    sizeof(struct lpfc_name)) == 0) &&
		    (lun_info->device_id.lun == lun))
			return lun_info;
	}

	return NULL;
}

/**
 * lpfc_find_next_oas_lun - searches for the next oas lun
 * @pha: Pointer to host bus adapter structure.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @starting_lun: Pointer to the lun to start searching for
 * @found_vport_wwpn: Pointer to the found lun's vport wwpn information
 * @found_target_wwpn: Pointer to the found lun's target wwpn information
 * @found_lun: Pointer to the found lun.
 * @found_lun_status: Pointer to status of the found lun.
 *
 * This routine searches the luns list for the specified lun
 * or the first lun for the vport/target.  If the vport wwpn contains
 * a zero value then a specific vport is not specified. In this case
 * any vport which contains the lun will be considered a match.  If the
 * target wwpn contains a zero value then a specific target is not specified.
 * In this case any target which contains the lun will be considered a
 * match.  If the lun is found, the lun, vport wwpn, target wwpn and lun status
 * are returned.  The function will also return the next lun if available.
 * If the next lun is not found, starting_lun parameter will be set to
 * NO_MORE_OAS_LUN.
 *
 * Return codes:
 *   non-0 - Error
 *   0 - Success
 **/
bool
lpfc_find_next_oas_lun(struct lpfc_hba *phba, struct lpfc_name *vport_wwpn,
		       struct lpfc_name *target_wwpn, uint64_t *starting_lun,
		       struct lpfc_name *found_vport_wwpn,
		       struct lpfc_name *found_target_wwpn,
		       uint64_t *found_lun,
		       uint32_t *found_lun_status,
		       uint32_t *found_lun_pri)
{

	unsigned long flags;
	struct lpfc_device_data *lun_info;
	struct lpfc_device_id *device_id;
	uint64_t lun;
	bool found = false;

	if (unlikely(!phba) || !vport_wwpn || !target_wwpn ||
	    !starting_lun || !found_vport_wwpn ||
	    !found_target_wwpn || !found_lun || !found_lun_status ||
	    (*starting_lun == NO_MORE_OAS_LUN) ||
	    !phba->cfg_fof)
		return false;

	lun = *starting_lun;
	*found_lun = NO_MORE_OAS_LUN;
	*starting_lun = NO_MORE_OAS_LUN;

	/* Search for lun or the lun closet in value */

	spin_lock_irqsave(&phba->devicelock, flags);
	list_for_each_entry(lun_info, &phba->luns, listentry) {
		if (((wwn_to_u64(vport_wwpn->u.wwn) == 0) ||
		     (memcmp(&lun_info->device_id.vport_wwpn, vport_wwpn,
			    sizeof(struct lpfc_name)) == 0)) &&
		    ((wwn_to_u64(target_wwpn->u.wwn) == 0) ||
		     (memcmp(&lun_info->device_id.target_wwpn, target_wwpn,
			    sizeof(struct lpfc_name)) == 0)) &&
		    (lun_info->oas_enabled)) {
			device_id = &lun_info->device_id;
			if ((!found) &&
			    ((lun == FIND_FIRST_OAS_LUN) ||
			     (device_id->lun == lun))) {
				*found_lun = device_id->lun;
				memcpy(found_vport_wwpn,
				       &device_id->vport_wwpn,
				       sizeof(struct lpfc_name));
				memcpy(found_target_wwpn,
				       &device_id->target_wwpn,
				       sizeof(struct lpfc_name));
				if (lun_info->available)
					*found_lun_status =
						OAS_LUN_STATUS_EXISTS;
				else
					*found_lun_status = 0;
				*found_lun_pri = lun_info->priority;
				if (phba->cfg_oas_flags & OAS_FIND_ANY_VPORT)
					memset(vport_wwpn, 0x0,
					       sizeof(struct lpfc_name));
				if (phba->cfg_oas_flags & OAS_FIND_ANY_TARGET)
					memset(target_wwpn, 0x0,
					       sizeof(struct lpfc_name));
				found = true;
			} else if (found) {
				*starting_lun = device_id->lun;
				memcpy(vport_wwpn, &device_id->vport_wwpn,
				       sizeof(struct lpfc_name));
				memcpy(target_wwpn, &device_id->target_wwpn,
				       sizeof(struct lpfc_name));
				break;
			}
		}
	}
	spin_unlock_irqrestore(&phba->devicelock, flags);
	return found;
}

/**
 * lpfc_enable_oas_lun - enables a lun for OAS operations
 * @pha: Pointer to host bus adapter structure.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @lun: Lun
 *
 * This routine enables a lun for oas operations.  The routines does so by
 * doing the following :
 *
 *   1) Checks to see if the device data for the lun has been created.
 *   2) If found, sets the OAS enabled flag if not set and returns.
 *   3) Otherwise, creates a device data structure.
 *   4) If successfully created, indicates the device data is for an OAS lun,
 *   indicates the lun is not available and add to the list of luns.
 *
 * Return codes:
 *   false - Error
 *   true - Success
 **/
bool
lpfc_enable_oas_lun(struct lpfc_hba *phba, struct lpfc_name *vport_wwpn,
		    struct lpfc_name *target_wwpn, uint64_t lun, uint8_t pri)
{

	struct lpfc_device_data *lun_info;
	unsigned long flags;

	if (unlikely(!phba) || !vport_wwpn || !target_wwpn ||
	    !phba->cfg_fof)
		return false;

	spin_lock_irqsave(&phba->devicelock, flags);

	/* Check to see if the device data for the lun has been created */
	lun_info = __lpfc_get_device_data(phba, &phba->luns, vport_wwpn,
					  target_wwpn, lun);
	if (lun_info) {
		if (!lun_info->oas_enabled)
			lun_info->oas_enabled = true;
		lun_info->priority = pri;
		spin_unlock_irqrestore(&phba->devicelock, flags);
		return true;
	}

	/* Create an lun info structure and add to list of luns */
	lun_info = lpfc_create_device_data(phba, vport_wwpn, target_wwpn, lun,
					   pri, true);
	if (lun_info) {
		lun_info->oas_enabled = true;
		lun_info->priority = pri;
		lun_info->available = false;
		list_add_tail(&lun_info->listentry, &phba->luns);
		spin_unlock_irqrestore(&phba->devicelock, flags);
		return true;
	}
	spin_unlock_irqrestore(&phba->devicelock, flags);
	return false;
}

/**
 * lpfc_disable_oas_lun - disables a lun for OAS operations
 * @pha: Pointer to host bus adapter structure.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @lun: Lun
 *
 * This routine disables a lun for oas operations.  The routines does so by
 * doing the following :
 *
 *   1) Checks to see if the device data for the lun is created.
 *   2) If present, clears the flag indicating this lun is for OAS.
 *   3) If the lun is not available by the system, the device data is
 *   freed.
 *
 * Return codes:
 *   false - Error
 *   true - Success
 **/
bool
lpfc_disable_oas_lun(struct lpfc_hba *phba, struct lpfc_name *vport_wwpn,
		     struct lpfc_name *target_wwpn, uint64_t lun, uint8_t pri)
{

	struct lpfc_device_data *lun_info;
	unsigned long flags;

	if (unlikely(!phba) || !vport_wwpn || !target_wwpn ||
	    !phba->cfg_fof)
		return false;

	spin_lock_irqsave(&phba->devicelock, flags);

	/* Check to see if the lun is available. */
	lun_info = __lpfc_get_device_data(phba,
					  &phba->luns, vport_wwpn,
					  target_wwpn, lun);
	if (lun_info) {
		lun_info->oas_enabled = false;
		lun_info->priority = pri;
		if (!lun_info->available)
			lpfc_delete_device_data(phba, lun_info);
		spin_unlock_irqrestore(&phba->devicelock, flags);
		return true;
	}

	spin_unlock_irqrestore(&phba->devicelock, flags);
	return false;
}

static int
lpfc_no_command(struct Scsi_Host *shost, struct scsi_cmnd *cmnd)
{
	return SCSI_MLQUEUE_HOST_BUSY;
}

static int
lpfc_no_handler(struct scsi_cmnd *cmnd)
{
	return FAILED;
}

static int
lpfc_no_slave(struct scsi_device *sdev)
{
	return -ENODEV;
}

struct scsi_host_template lpfc_template_nvme = {
	.module			= THIS_MODULE,
	.name			= LPFC_DRIVER_NAME,
	.proc_name		= LPFC_DRIVER_NAME,
	.info			= lpfc_info,
	.queuecommand		= lpfc_no_command,
	.eh_abort_handler	= lpfc_no_handler,
	.eh_device_reset_handler = lpfc_no_handler,
	.eh_target_reset_handler = lpfc_no_handler,
	.eh_bus_reset_handler	= lpfc_no_handler,
	.eh_host_reset_handler  = lpfc_no_handler,
	.slave_alloc		= lpfc_no_slave,
	.slave_configure	= lpfc_no_slave,
	.scan_finished		= lpfc_scan_finished,
	.this_id		= -1,
	.sg_tablesize		= 1,
	.cmd_per_lun		= 1,
	.shost_attrs		= lpfc_hba_attrs,
	.max_sectors		= 0xFFFF,
	.vendor_id		= LPFC_NL_VENDOR_ID,
	.track_queue_depth	= 0,
};

struct scsi_host_template lpfc_template = {
	.module			= THIS_MODULE,
	.name			= LPFC_DRIVER_NAME,
	.proc_name		= LPFC_DRIVER_NAME,
	.info			= lpfc_info,
	.queuecommand		= lpfc_queuecommand,
	.eh_timed_out		= fc_eh_timed_out,
	.eh_abort_handler	= lpfc_abort_handler,
	.eh_device_reset_handler = lpfc_device_reset_handler,
	.eh_target_reset_handler = lpfc_target_reset_handler,
	.eh_bus_reset_handler	= lpfc_bus_reset_handler,
	.eh_host_reset_handler  = lpfc_host_reset_handler,
	.slave_alloc		= lpfc_slave_alloc,
	.slave_configure	= lpfc_slave_configure,
	.slave_destroy		= lpfc_slave_destroy,
	.scan_finished		= lpfc_scan_finished,
	.this_id		= -1,
	.sg_tablesize		= LPFC_DEFAULT_SG_SEG_CNT,
	.cmd_per_lun		= LPFC_CMD_PER_LUN,
	.shost_attrs		= lpfc_hba_attrs,
	.max_sectors		= 0xFFFF,
	.vendor_id		= LPFC_NL_VENDOR_ID,
	.change_queue_depth	= scsi_change_queue_depth,
	.track_queue_depth	= 1,
};
