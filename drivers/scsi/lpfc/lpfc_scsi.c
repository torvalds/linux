/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2008 Emulex.  All rights reserved.           *
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
#include <linux/delay.h>
#include <asm/unaligned.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_version.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"

#define LPFC_RESET_WAIT  2
#define LPFC_ABORT_WAIT  2

int _dump_buf_done;

static char *dif_op_str[] = {
	"SCSI_PROT_NORMAL",
	"SCSI_PROT_READ_INSERT",
	"SCSI_PROT_WRITE_STRIP",
	"SCSI_PROT_READ_STRIP",
	"SCSI_PROT_WRITE_INSERT",
	"SCSI_PROT_READ_PASS",
	"SCSI_PROT_WRITE_PASS",
	"SCSI_PROT_READ_CONVERT",
	"SCSI_PROT_WRITE_CONVERT"
};

static void
lpfc_debug_save_data(struct scsi_cmnd *cmnd)
{
	void *src, *dst;
	struct scatterlist *sgde = scsi_sglist(cmnd);

	if (!_dump_buf_data) {
		printk(KERN_ERR "BLKGRD ERROR %s _dump_buf_data is NULL\n",
				__func__);
		return;
	}


	if (!sgde) {
		printk(KERN_ERR "BLKGRD ERROR: data scatterlist is null\n");
		return;
	}

	dst = (void *) _dump_buf_data;
	while (sgde) {
		src = sg_virt(sgde);
		memcpy(dst, src, sgde->length);
		dst += sgde->length;
		sgde = sg_next(sgde);
	}
}

static void
lpfc_debug_save_dif(struct scsi_cmnd *cmnd)
{
	void *src, *dst;
	struct scatterlist *sgde = scsi_prot_sglist(cmnd);

	if (!_dump_buf_dif) {
		printk(KERN_ERR "BLKGRD ERROR %s _dump_buf_data is NULL\n",
				__func__);
		return;
	}

	if (!sgde) {
		printk(KERN_ERR "BLKGRD ERROR: prot scatterlist is null\n");
		return;
	}

	dst = _dump_buf_dif;
	while (sgde) {
		src = sg_virt(sgde);
		memcpy(dst, src, sgde->length);
		dst += sgde->length;
		sgde = sg_next(sgde);
	}
}

/**
 * lpfc_update_stats: Update statistical data for the command completion.
 * @phba: Pointer to HBA object.
 * @lpfc_cmd: lpfc scsi command object pointer.
 *
 * This function is called when there is a command completion and this
 * function updates the statistical data for the command completion.
 **/
static void
lpfc_update_stats(struct lpfc_hba *phba, struct  lpfc_scsi_buf *lpfc_cmd)
{
	struct lpfc_rport_data *rdata = lpfc_cmd->rdata;
	struct lpfc_nodelist *pnode = rdata->pnode;
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	unsigned long flags;
	struct Scsi_Host  *shost = cmd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	unsigned long latency;
	int i;

	if (cmd->result)
		return;

	latency = jiffies_to_msecs((long)jiffies - (long)lpfc_cmd->start_time);

	spin_lock_irqsave(shost->host_lock, flags);
	if (!vport->stat_data_enabled ||
		vport->stat_data_blocked ||
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
 * lpfc_send_sdev_queuedepth_change_event: Posts a queuedepth change
 *                   event.
 * @phba: Pointer to HBA context object.
 * @vport: Pointer to vport object.
 * @ndlp: Pointer to FC node associated with the target.
 * @lun: Lun number of the scsi device.
 * @old_val: Old value of the queue depth.
 * @new_val: New value of the queue depth.
 *
 * This function sends an event to the mgmt application indicating
 * there is a change in the scsi device queue depth.
 **/
static void
lpfc_send_sdev_queuedepth_change_event(struct lpfc_hba *phba,
		struct lpfc_vport  *vport,
		struct lpfc_nodelist *ndlp,
		uint32_t lun,
		uint32_t old_val,
		uint32_t new_val)
{
	struct lpfc_fast_path_event *fast_path_evt;
	unsigned long flags;

	fast_path_evt = lpfc_alloc_fast_evt(phba);
	if (!fast_path_evt)
		return;

	fast_path_evt->un.queue_depth_evt.scsi_event.event_type =
		FC_REG_SCSI_EVENT;
	fast_path_evt->un.queue_depth_evt.scsi_event.subcategory =
		LPFC_EVENT_VARQUEDEPTH;

	/* Report all luns with change in queue depth */
	fast_path_evt->un.queue_depth_evt.scsi_event.lun = lun;
	if (ndlp && NLP_CHK_NODE_ACT(ndlp)) {
		memcpy(&fast_path_evt->un.queue_depth_evt.scsi_event.wwpn,
			&ndlp->nlp_portname, sizeof(struct lpfc_name));
		memcpy(&fast_path_evt->un.queue_depth_evt.scsi_event.wwnn,
			&ndlp->nlp_nodename, sizeof(struct lpfc_name));
	}

	fast_path_evt->un.queue_depth_evt.oldval = old_val;
	fast_path_evt->un.queue_depth_evt.newval = new_val;
	fast_path_evt->vport = vport;

	fast_path_evt->work_evt.evt = LPFC_EVT_FASTPATH_MGMT_EVT;
	spin_lock_irqsave(&phba->hbalock, flags);
	list_add_tail(&fast_path_evt->work_evt.evt_listp, &phba->work_list);
	spin_unlock_irqrestore(&phba->hbalock, flags);
	lpfc_worker_wake_up(phba);

	return;
}

/**
 * lpfc_rampdown_queue_depth: Post RAMP_DOWN_QUEUE event to worker thread.
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

	spin_lock_irqsave(&phba->hbalock, flags);
	atomic_inc(&phba->num_rsrc_err);
	phba->last_rsrc_error_time = jiffies;

	if ((phba->last_ramp_down_time + QUEUE_RAMP_DOWN_INTERVAL) > jiffies) {
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
 * lpfc_rampup_queue_depth: Post RAMP_UP_QUEUE event for worker thread.
 * @phba: The Hba for which this call is being executed.
 *
 * This routine post WORKER_RAMP_UP_QUEUE event for @phba vport. This routine
 * post at most 1 event every 5 minute after last_ramp_up_time or
 * last_rsrc_error_time.  This routine wakes up worker thread of @phba
 * to process WORKER_RAM_DOWN_EVENT event.
 *
 * This routine should be called with no lock held.
 **/
static inline void
lpfc_rampup_queue_depth(struct lpfc_vport  *vport,
			struct scsi_device *sdev)
{
	unsigned long flags;
	struct lpfc_hba *phba = vport->phba;
	uint32_t evt_posted;
	atomic_inc(&phba->num_cmd_success);

	if (vport->cfg_lun_queue_depth <= sdev->queue_depth)
		return;
	spin_lock_irqsave(&phba->hbalock, flags);
	if (((phba->last_ramp_up_time + QUEUE_RAMP_UP_INTERVAL) > jiffies) ||
	 ((phba->last_rsrc_error_time + QUEUE_RAMP_UP_INTERVAL ) > jiffies)) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}
	phba->last_ramp_up_time = jiffies;
	spin_unlock_irqrestore(&phba->hbalock, flags);

	spin_lock_irqsave(&phba->pport->work_port_lock, flags);
	evt_posted = phba->pport->work_port_events & WORKER_RAMP_UP_QUEUE;
	if (!evt_posted)
		phba->pport->work_port_events |= WORKER_RAMP_UP_QUEUE;
	spin_unlock_irqrestore(&phba->pport->work_port_lock, flags);

	if (!evt_posted)
		lpfc_worker_wake_up(phba);
	return;
}

/**
 * lpfc_ramp_down_queue_handler: WORKER_RAMP_DOWN_QUEUE event handler.
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
	unsigned long new_queue_depth, old_queue_depth;
	unsigned long num_rsrc_err, num_cmd_success;
	int i;
	struct lpfc_rport_data *rdata;

	num_rsrc_err = atomic_read(&phba->num_rsrc_err);
	num_cmd_success = atomic_read(&phba->num_cmd_success);

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for(i = 0; i <= phba->max_vpi && vports[i] != NULL; i++) {
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
				old_queue_depth = sdev->queue_depth;
				if (sdev->ordered_tags)
					scsi_adjust_queue_depth(sdev,
							MSG_ORDERED_TAG,
							new_queue_depth);
				else
					scsi_adjust_queue_depth(sdev,
							MSG_SIMPLE_TAG,
							new_queue_depth);
				rdata = sdev->hostdata;
				if (rdata)
					lpfc_send_sdev_queuedepth_change_event(
						phba, vports[i],
						rdata->pnode,
						sdev->lun, old_queue_depth,
						new_queue_depth);
			}
		}
	lpfc_destroy_vport_work_array(phba, vports);
	atomic_set(&phba->num_rsrc_err, 0);
	atomic_set(&phba->num_cmd_success, 0);
}

/**
 * lpfc_ramp_up_queue_handler: WORKER_RAMP_UP_QUEUE event handler.
 * @phba: The Hba for which this call is being executed.
 *
 * This routine is called to  process WORKER_RAMP_UP_QUEUE event for worker
 * thread.This routine increases queue depth for all scsi device on each vport
 * associated with @phba by 1. This routine also sets @phba num_rsrc_err and
 * num_cmd_success to zero.
 **/
void
lpfc_ramp_up_queue_handler(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	struct scsi_device *sdev;
	int i;
	struct lpfc_rport_data *rdata;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for(i = 0; i <= phba->max_vpi && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			shost_for_each_device(sdev, shost) {
				if (vports[i]->cfg_lun_queue_depth <=
				    sdev->queue_depth)
					continue;
				if (sdev->ordered_tags)
					scsi_adjust_queue_depth(sdev,
							MSG_ORDERED_TAG,
							sdev->queue_depth+1);
				else
					scsi_adjust_queue_depth(sdev,
							MSG_SIMPLE_TAG,
							sdev->queue_depth+1);
				rdata = sdev->hostdata;
				if (rdata)
					lpfc_send_sdev_queuedepth_change_event(
						phba, vports[i],
						rdata->pnode,
						sdev->lun,
						sdev->queue_depth - 1,
						sdev->queue_depth);
			}
		}
	lpfc_destroy_vport_work_array(phba, vports);
	atomic_set(&phba->num_rsrc_err, 0);
	atomic_set(&phba->num_cmd_success, 0);
}

/**
 * lpfc_scsi_dev_block: set all scsi hosts to block state.
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
		for (i = 0; i <= phba->max_vpi && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			shost_for_each_device(sdev, shost) {
				rport = starget_to_rport(scsi_target(sdev));
				fc_remote_port_delete(rport);
			}
		}
	lpfc_destroy_vport_work_array(phba, vports);
}

/**
 * lpfc_new_scsi_buf: Scsi buffer allocator.
 * @vport: The virtual port for which this call being executed.
 *
 * This routine allocates a scsi buffer, which contains all the necessary
 * information needed to initiate a SCSI I/O.  The non-DMAable buffer region
 * contains information to build the IOCB.  The DMAable region contains
 * memory for the FCP CMND, FCP RSP, and the initial BPL.  In addition to
 * allocating memory, the FCP CMND and FCP RSP BDEs are setup in the BPL
 * and the BPL BDE is setup in the IOCB.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_scsi_buf data structure - Success
 **/
static struct lpfc_scsi_buf *
lpfc_new_scsi_buf(struct lpfc_vport *vport)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_scsi_buf *psb;
	struct ulp_bde64 *bpl;
	IOCB_t *iocb;
	dma_addr_t pdma_phys_fcp_cmd;
	dma_addr_t pdma_phys_fcp_rsp;
	dma_addr_t pdma_phys_bpl;
	uint16_t iotag;

	psb = kzalloc(sizeof(struct lpfc_scsi_buf), GFP_KERNEL);
	if (!psb)
		return NULL;

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

	/* Allocate iotag for psb->cur_iocbq. */
	iotag = lpfc_sli_next_iotag(phba, &psb->cur_iocbq);
	if (iotag == 0) {
		pci_pool_free(phba->lpfc_scsi_dma_buf_pool,
			      psb->data, psb->dma_handle);
		kfree (psb);
		return NULL;
	}
	psb->cur_iocbq.iocb_flag |= LPFC_IO_FCP;

	psb->fcp_cmnd = psb->data;
	psb->fcp_rsp = psb->data + sizeof(struct fcp_cmnd);
	psb->fcp_bpl = psb->data + sizeof(struct fcp_cmnd) +
							sizeof(struct fcp_rsp);

	/* Initialize local short-hand pointers. */
	bpl = psb->fcp_bpl;
	pdma_phys_fcp_cmd = psb->dma_handle;
	pdma_phys_fcp_rsp = psb->dma_handle + sizeof(struct fcp_cmnd);
	pdma_phys_bpl = psb->dma_handle + sizeof(struct fcp_cmnd) +
			sizeof(struct fcp_rsp);

	/*
	 * The first two bdes are the FCP_CMD and FCP_RSP.  The balance are sg
	 * list bdes.  Initialize the first two and leave the rest for
	 * queuecommand.
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
	 * Since the IOCB for the FCP I/O is built into this lpfc_scsi_buf,
	 * initialize it with all known data now.
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
		/* fill in responce BDE */
		iocb->unsli3.fcp_ext.rbde.tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		iocb->unsli3.fcp_ext.rbde.tus.f.bdeSize =
						sizeof(struct fcp_rsp);
		iocb->unsli3.fcp_ext.rbde.addrLow =
						putPaddrLow(pdma_phys_fcp_rsp);
		iocb->unsli3.fcp_ext.rbde.addrHigh =
						putPaddrHigh(pdma_phys_fcp_rsp);
	} else {
		iocb->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
		iocb->un.fcpi64.bdl.bdeSize = (2 * sizeof(struct ulp_bde64));
		iocb->un.fcpi64.bdl.addrLow = putPaddrLow(pdma_phys_bpl);
		iocb->un.fcpi64.bdl.addrHigh = putPaddrHigh(pdma_phys_bpl);
		iocb->ulpBdeCount = 1;
		iocb->ulpLe = 1;
	}
	iocb->ulpClass = CLASS3;

	return psb;
}

/**
 * lpfc_get_scsi_buf: Get a scsi buffer from lpfc_scsi_buf_list list of Hba.
 * @phba: The Hba for which this call is being executed.
 *
 * This routine removes a scsi buffer from head of @phba lpfc_scsi_buf_list list
 * and returns to caller.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_scsi_buf - Success
 **/
static struct lpfc_scsi_buf*
lpfc_get_scsi_buf(struct lpfc_hba * phba)
{
	struct  lpfc_scsi_buf * lpfc_cmd = NULL;
	struct list_head *scsi_buf_list = &phba->lpfc_scsi_buf_list;
	unsigned long iflag = 0;

	spin_lock_irqsave(&phba->scsi_buf_list_lock, iflag);
	list_remove_head(scsi_buf_list, lpfc_cmd, struct lpfc_scsi_buf, list);
	if (lpfc_cmd) {
		lpfc_cmd->seg_cnt = 0;
		lpfc_cmd->nonsg_phys = 0;
		lpfc_cmd->prot_seg_cnt = 0;
	}
	spin_unlock_irqrestore(&phba->scsi_buf_list_lock, iflag);
	return  lpfc_cmd;
}

/**
 * lpfc_release_scsi_buf: Return a scsi buffer back to hba lpfc_scsi_buf_list list.
 * @phba: The Hba for which this call is being executed.
 * @psb: The scsi buffer which is being released.
 *
 * This routine releases @psb scsi buffer by adding it to tail of @phba
 * lpfc_scsi_buf_list list.
 **/
static void
lpfc_release_scsi_buf(struct lpfc_hba *phba, struct lpfc_scsi_buf *psb)
{
	unsigned long iflag = 0;

	spin_lock_irqsave(&phba->scsi_buf_list_lock, iflag);
	psb->pCmd = NULL;
	list_add_tail(&psb->list, &phba->lpfc_scsi_buf_list);
	spin_unlock_irqrestore(&phba->scsi_buf_list_lock, iflag);
}

/**
 * lpfc_scsi_prep_dma_buf: Routine to do DMA mapping for scsi buffer.
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This routine does the pci dma mapping for scatter-gather list of scsi cmnd
 * field of @lpfc_cmd. This routine scans through sg elements and format the
 * bdea. This routine also initializes all IOCB fields which are dependent on
 * scsi command request buffer.
 *
 * Return codes:
 *   1 - Error
 *   0 - Success
 **/
static int
lpfc_scsi_prep_dma_buf(struct lpfc_hba *phba, struct lpfc_scsi_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct scatterlist *sgel = NULL;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct ulp_bde64 *bpl = lpfc_cmd->fcp_bpl;
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
			printk(KERN_ERR "%s: Too many sg segments from "
			       "dma_map_sg.  Config %d, seg_cnt %d\n",
			       __func__, phba->cfg_sg_seg_cnt,
			       lpfc_cmd->seg_cnt);
			scsi_dma_unmap(scsi_cmnd);
			return 1;
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
	    !(phba->sli3_options & LPFC_SLI3_BG_ENABLED)) {
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
			/* ebde count includes the responce bde and data bpl */
			iocb_cmd->unsli3.fcp_ext.ebde_count = 2;
		} else {
			/* ebde count includes the responce bde and data bdes */
			iocb_cmd->unsli3.fcp_ext.ebde_count = (num_bde + 1);
		}
	} else {
		iocb_cmd->un.fcpi64.bdl.bdeSize =
			((num_bde + 2) * sizeof(struct ulp_bde64));
	}
	fcp_cmnd->fcpDl = cpu_to_be32(scsi_bufflen(scsi_cmnd));

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = le32_to_cpu(scsi_bufflen(scsi_cmnd));
	return 0;
}

/*
 * Given a scsi cmnd, determine the BlockGuard profile to be used
 * with the cmd
 */
static int
lpfc_sc_to_sli_prof(struct scsi_cmnd *sc)
{
	uint8_t guard_type = scsi_host_get_guard(sc->device->host);
	uint8_t ret_prof = LPFC_PROF_INVALID;

	if (guard_type == SHOST_DIX_GUARD_IP) {
		switch (scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			ret_prof = LPFC_PROF_AST2;
			break;

		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			ret_prof = LPFC_PROF_A1;
			break;

		case SCSI_PROT_READ_CONVERT:
		case SCSI_PROT_WRITE_CONVERT:
			ret_prof = LPFC_PROF_AST1;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
		case SCSI_PROT_NORMAL:
		default:
			printk(KERN_ERR "Bad op/guard:%d/%d combination\n",
					scsi_get_prot_op(sc), guard_type);
			break;

		}
	} else if (guard_type == SHOST_DIX_GUARD_CRC) {
		switch (scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			ret_prof = LPFC_PROF_A1;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			ret_prof = LPFC_PROF_C1;
			break;

		case SCSI_PROT_READ_CONVERT:
		case SCSI_PROT_WRITE_CONVERT:
		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
		case SCSI_PROT_NORMAL:
		default:
			printk(KERN_ERR "Bad op/guard:%d/%d combination\n",
					scsi_get_prot_op(sc), guard_type);
			break;
		}
	} else {
		/* unsupported format */
		BUG();
	}

	return ret_prof;
}

struct scsi_dif_tuple {
	__be16 guard_tag;       /* Checksum */
	__be16 app_tag;         /* Opaque storage */
	__be32 ref_tag;         /* Target LBA or indirect LBA */
};

static inline unsigned
lpfc_cmd_blksize(struct scsi_cmnd *sc)
{
	return sc->device->sector_size;
}

/**
 * lpfc_get_cmd_dif_parms - Extract DIF parameters from SCSI command
 * @sc:             in: SCSI command
 * @apptagmask      out: app tag mask
 * @apptagval       out: app tag value
 * @reftag          out: ref tag (reference tag)
 *
 * Description:
 *   Extract DIF paramters from the command if possible.  Otherwise,
 *   use default paratmers.
 *
 **/
static inline void
lpfc_get_cmd_dif_parms(struct scsi_cmnd *sc, uint16_t *apptagmask,
		uint16_t *apptagval, uint32_t *reftag)
{
	struct  scsi_dif_tuple *spt;
	unsigned char op = scsi_get_prot_op(sc);
	unsigned int protcnt = scsi_prot_sg_count(sc);
	static int cnt;

	if (protcnt && (op == SCSI_PROT_WRITE_STRIP ||
				op == SCSI_PROT_WRITE_PASS ||
				op == SCSI_PROT_WRITE_CONVERT)) {

		cnt++;
		spt = page_address(sg_page(scsi_prot_sglist(sc))) +
			scsi_prot_sglist(sc)[0].offset;
		*apptagmask = 0;
		*apptagval = 0;
		*reftag = cpu_to_be32(spt->ref_tag);

	} else {
		/* SBC defines ref tag to be lower 32bits of LBA */
		*reftag = (uint32_t) (0xffffffff & scsi_get_lba(sc));
		*apptagmask = 0;
		*apptagval = 0;
	}
}

/*
 * This function sets up buffer list for protection groups of
 * type LPFC_PG_TYPE_NO_DIF
 *
 * This is usually used when the HBA is instructed to generate
 * DIFs and insert them into data stream (or strip DIF from
 * incoming data stream)
 *
 * The buffer list consists of just one protection group described
 * below:
 *                                +-------------------------+
 *   start of prot group  -->     |          PDE_1          |
 *                                +-------------------------+
 *                                |         Data BDE        |
 *                                +-------------------------+
 *                                |more Data BDE's ... (opt)|
 *                                +-------------------------+
 *
 * @sc: pointer to scsi command we're working on
 * @bpl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 *
 * Note: Data s/g buffers have been dma mapped
 */
static int
lpfc_bg_setup_bpl(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct ulp_bde64 *bpl, int datasegcnt)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct lpfc_pde *pde1 = NULL;
	dma_addr_t physaddr;
	int i = 0, num_bde = 0;
	int datadir = sc->sc_data_direction;
	int prof = LPFC_PROF_INVALID;
	unsigned blksize;
	uint32_t reftag;
	uint16_t apptagmask, apptagval;

	pde1 = (struct lpfc_pde *) bpl;
	prof = lpfc_sc_to_sli_prof(sc);

	if (prof == LPFC_PROF_INVALID)
		goto out;

	/* extract some info from the scsi command for PDE1*/
	blksize = lpfc_cmd_blksize(sc);
	lpfc_get_cmd_dif_parms(sc, &apptagmask, &apptagval, &reftag);

	/* setup PDE1 with what we have */
	lpfc_pde_set_bg_parms(pde1, LPFC_PDE1_DESCRIPTOR, prof, blksize,
			BG_EC_STOP_ERR);
	lpfc_pde_set_dif_parms(pde1, apptagmask, apptagval, reftag);

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

/*
 * This function sets up buffer list for protection groups of
 * type LPFC_PG_TYPE_DIF_BUF
 *
 * This is usually used when DIFs are in their own buffers,
 * separate from the data. The HBA can then by instructed
 * to place the DIFs in the outgoing stream.  For read operations,
 * The HBA could extract the DIFs and place it in DIF buffers.
 *
 * The buffer list for this type consists of one or more of the
 * protection groups described below:
 *                                    +-------------------------+
 *   start of first prot group  -->   |          PDE_1          |
 *                                    +-------------------------+
 *                                    |      PDE_3 (Prot BDE)   |
 *                                    +-------------------------+
 *                                    |        Data BDE         |
 *                                    +-------------------------+
 *                                    |more Data BDE's ... (opt)|
 *                                    +-------------------------+
 *   start of new  prot group  -->    |          PDE_1          |
 *                                    +-------------------------+
 *                                    |          ...            |
 *                                    +-------------------------+
 *
 * @sc: pointer to scsi command we're working on
 * @bpl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 * @protcnt: number of segment of protection data that have been dma mapped
 *
 * Note: It is assumed that both data and protection s/g buffers have been
 *       mapped for DMA
 */
static int
lpfc_bg_setup_bpl_prot(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct ulp_bde64 *bpl, int datacnt, int protcnt)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct scatterlist *sgpe = NULL; /* s/g prot entry */
	struct lpfc_pde *pde1 = NULL;
	struct ulp_bde64 *prot_bde = NULL;
	dma_addr_t dataphysaddr, protphysaddr;
	unsigned short curr_data = 0, curr_prot = 0;
	unsigned int split_offset, protgroup_len;
	unsigned int protgrp_blks, protgrp_bytes;
	unsigned int remainder, subtotal;
	int prof = LPFC_PROF_INVALID;
	int datadir = sc->sc_data_direction;
	unsigned char pgdone = 0, alldone = 0;
	unsigned blksize;
	uint32_t reftag;
	uint16_t apptagmask, apptagval;
	int num_bde = 0;

	sgpe = scsi_prot_sglist(sc);
	sgde = scsi_sglist(sc);

	if (!sgpe || !sgde) {
		lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
				"9020 Invalid s/g entry: data=0x%p prot=0x%p\n",
				sgpe, sgde);
		return 0;
	}

	prof = lpfc_sc_to_sli_prof(sc);
	if (prof == LPFC_PROF_INVALID)
		goto out;

	/* extract some info from the scsi command for PDE1*/
	blksize = lpfc_cmd_blksize(sc);
	lpfc_get_cmd_dif_parms(sc, &apptagmask, &apptagval, &reftag);

	split_offset = 0;
	do {
		/* setup the first PDE_1 */
		pde1 = (struct lpfc_pde *) bpl;

		lpfc_pde_set_bg_parms(pde1, LPFC_PDE1_DESCRIPTOR, prof, blksize,
				BG_EC_STOP_ERR);
		lpfc_pde_set_dif_parms(pde1, apptagmask, apptagval, reftag);

		num_bde++;
		bpl++;

		/* setup the first BDE that points to protection buffer */
		prot_bde = (struct ulp_bde64 *) bpl;
		protphysaddr = sg_dma_address(sgpe);
		prot_bde->addrLow = le32_to_cpu(putPaddrLow(protphysaddr));
		prot_bde->addrHigh = le32_to_cpu(putPaddrHigh(protphysaddr));
		protgroup_len = sg_dma_len(sgpe);


		/* must be integer multiple of the DIF block length */
		BUG_ON(protgroup_len % 8);

		protgrp_blks = protgroup_len / 8;
		protgrp_bytes = protgrp_blks * blksize;

		prot_bde->tus.f.bdeSize = protgroup_len;
		if (datadir == DMA_TO_DEVICE)
			prot_bde->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		else
			prot_bde->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
		prot_bde->tus.w = le32_to_cpu(bpl->tus.w);

		curr_prot++;
		num_bde++;

		/* setup BDE's for data blocks associated with DIF data */
		pgdone = 0;
		subtotal = 0; /* total bytes processed for current prot grp */
		while (!pgdone) {
			if (!sgde) {
				printk(KERN_ERR "%s Invalid data segment\n",
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
			printk(KERN_ERR "BLKGRD: bug in %s\n", __func__);
		}

	} while (!alldone);

out:


	return num_bde;
}
/*
 * Given a SCSI command that supports DIF, determine composition of protection
 * groups involved in setting up buffer lists
 *
 * Returns:
 *			      for DIF (for both read and write)
 * */
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
	case SCSI_PROT_WRITE_CONVERT:
	case SCSI_PROT_READ_CONVERT:
		ret = LPFC_PG_TYPE_DIF_BUF;
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
				"9021 Unsupported protection op:%d\n", op);
		break;
	}

	return ret;
}

/*
 * This is the protection/DIF aware version of
 * lpfc_scsi_prep_dma_buf(). It may be a good idea to combine the
 * two functions eventually, but for now, it's here
 */
static int
lpfc_bg_scsi_prep_dma_buf(struct lpfc_hba *phba,
		struct lpfc_scsi_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct ulp_bde64 *bpl = lpfc_cmd->fcp_bpl;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	uint32_t num_bde = 0;
	int datasegcnt, protsegcnt, datadir = scsi_cmnd->sc_data_direction;
	int prot_group_type = 0;
	int diflen, fcpdl;
	unsigned blksize;

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
		if (lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt) {
			printk(KERN_ERR "%s: Too many sg segments from "
					"dma_map_sg.  Config %d, seg_cnt %d\n",
					__func__, phba->cfg_sg_seg_cnt,
					lpfc_cmd->seg_cnt);
			scsi_dma_unmap(scsi_cmnd);
			return 1;
		}

		prot_group_type = lpfc_prot_group_type(phba, scsi_cmnd);

		switch (prot_group_type) {
		case LPFC_PG_TYPE_NO_DIF:
			num_bde = lpfc_bg_setup_bpl(phba, scsi_cmnd, bpl,
					datasegcnt);
			/* we shoud have 2 or more entries in buffer list */
			if (num_bde < 2)
				goto err;
			break;
		case LPFC_PG_TYPE_DIF_BUF:{
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
			if (lpfc_cmd->prot_seg_cnt
			    > phba->cfg_prot_sg_seg_cnt) {
				printk(KERN_ERR "%s: Too many prot sg segments "
						"from dma_map_sg.  Config %d,"
						"prot_seg_cnt %d\n", __func__,
						phba->cfg_prot_sg_seg_cnt,
						lpfc_cmd->prot_seg_cnt);
				dma_unmap_sg(&phba->pcidev->dev,
					     scsi_prot_sglist(scsi_cmnd),
					     scsi_prot_sg_count(scsi_cmnd),
					     datadir);
				scsi_dma_unmap(scsi_cmnd);
				return 1;
			}

			num_bde = lpfc_bg_setup_bpl_prot(phba, scsi_cmnd, bpl,
					datasegcnt, protsegcnt);
			/* we shoud have 3 or more entries in buffer list */
			if (num_bde < 3)
				goto err;
			break;
		}
		case LPFC_PG_TYPE_INVALID:
		default:
			lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
					"9022 Unexpected protection group %i\n",
					prot_group_type);
			return 1;
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

	fcpdl = scsi_bufflen(scsi_cmnd);

	if (scsi_get_prot_type(scsi_cmnd) == SCSI_PROT_DIF_TYPE1) {
		/*
		 * We are in DIF Type 1 mode
		 * Every data block has a 8 byte DIF (trailer)
		 * attached to it.  Must ajust FCP data length
		 */
		blksize = lpfc_cmd_blksize(scsi_cmnd);
		diflen = (fcpdl / blksize) * 8;
		fcpdl += diflen;
	}
	fcp_cmnd->fcpDl = be32_to_cpu(fcpdl);

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = fcpdl;

	return 0;
err:
	lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
			"9023 Could not setup all needed BDE's"
			"prot_group_type=%d, num_bde=%d\n",
			prot_group_type, num_bde);
	return 1;
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
lpfc_parse_bg_err(struct lpfc_hba *phba, struct lpfc_scsi_buf *lpfc_cmd,
			struct lpfc_iocbq *pIocbOut)
{
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	struct sli3_bg_fields *bgf = &pIocbOut->iocb.unsli3.sli3_bg;
	int ret = 0;
	uint32_t bghm = bgf->bghm;
	uint32_t bgstat = bgf->bgstat;
	uint64_t failing_sector = 0;

	printk(KERN_ERR "BG ERROR in cmd 0x%x lba 0x%llx blk cnt 0x%lx "
			"bgstat=0x%x bghm=0x%x\n",
			cmd->cmnd[0], (unsigned long long)scsi_get_lba(cmd),
			cmd->request->nr_sectors, bgstat, bghm);

	spin_lock(&_dump_buf_lock);
	if (!_dump_buf_done) {
		printk(KERN_ERR "Saving Data for %u blocks to debugfs\n",
				(cmd->cmnd[7] << 8 | cmd->cmnd[8]));
		lpfc_debug_save_data(cmd);

		/* If we have a prot sgl, save the DIF buffer */
		if (lpfc_prot_group_type(phba, cmd) ==
				LPFC_PG_TYPE_DIF_BUF) {
			printk(KERN_ERR "Saving DIF for %u blocks to debugfs\n",
					(cmd->cmnd[7] << 8 | cmd->cmnd[8]));
			lpfc_debug_save_dif(cmd);
		}

		_dump_buf_done = 1;
	}
	spin_unlock(&_dump_buf_lock);

	if (lpfc_bgs_get_invalid_prof(bgstat)) {
		cmd->result = ScsiResult(DID_ERROR, 0);
		printk(KERN_ERR "Invalid BlockGuard profile. bgstat:0x%x\n",
				bgstat);
		ret = (-1);
		goto out;
	}

	if (lpfc_bgs_get_uninit_dif_block(bgstat)) {
		cmd->result = ScsiResult(DID_ERROR, 0);
		printk(KERN_ERR "Invalid BlockGuard DIF Block. bgstat:0x%x\n",
				bgstat);
		ret = (-1);
		goto out;
	}

	if (lpfc_bgs_get_guard_err(bgstat)) {
		ret = 1;

		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
				0x10, 0x1);
		cmd->result = DRIVER_SENSE << 24
			| ScsiResult(DID_ABORT, SAM_STAT_CHECK_CONDITION);
		phba->bg_guard_err_cnt++;
		printk(KERN_ERR "BLKGRD: guard_tag error\n");
	}

	if (lpfc_bgs_get_reftag_err(bgstat)) {
		ret = 1;

		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
				0x10, 0x3);
		cmd->result = DRIVER_SENSE << 24
			| ScsiResult(DID_ABORT, SAM_STAT_CHECK_CONDITION);

		phba->bg_reftag_err_cnt++;
		printk(KERN_ERR "BLKGRD: ref_tag error\n");
	}

	if (lpfc_bgs_get_apptag_err(bgstat)) {
		ret = 1;

		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
				0x10, 0x2);
		cmd->result = DRIVER_SENSE << 24
			| ScsiResult(DID_ABORT, SAM_STAT_CHECK_CONDITION);

		phba->bg_apptag_err_cnt++;
		printk(KERN_ERR "BLKGRD: app_tag error\n");
	}

	if (lpfc_bgs_get_hi_water_mark_present(bgstat)) {
		/*
		 * setup sense data descriptor 0 per SPC-4 as an information
		 * field, and put the failing LBA in it
		 */
		cmd->sense_buffer[8] = 0;     /* Information */
		cmd->sense_buffer[9] = 0xa;   /* Add. length */
		bghm /= cmd->device->sector_size;

		failing_sector = scsi_get_lba(cmd);
		failing_sector += bghm;

		put_unaligned_be64(failing_sector, &cmd->sense_buffer[10]);
	}

	if (!ret) {
		/* No error was reported - problem in FW? */
		cmd->result = ScsiResult(DID_ERROR, 0);
		printk(KERN_ERR "BLKGRD: no errors reported!\n");
	}

out:
	return ret;
}

/**
 * lpfc_send_scsi_error_event: Posts an event when there is SCSI error.
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
		struct lpfc_scsi_buf *lpfc_cmd, struct lpfc_iocbq *rsp_iocb) {
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	uint32_t resp_info = fcprsp->rspStatus2;
	uint32_t scsi_status = fcprsp->rspStatus3;
	uint32_t fcpi_parm = rsp_iocb->iocb.un.fcpi.fcpi_parm;
	struct lpfc_fast_path_event *fast_path_evt = NULL;
	struct lpfc_nodelist *pnode = lpfc_cmd->rdata->pnode;
	unsigned long flags;

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
 * lpfc_scsi_unprep_dma_buf: Routine to un-map DMA mapping of scatter gather.
 * @phba: The Hba for which this call is being executed.
 * @psb: The scsi buffer which is going to be un-mapped.
 *
 * This routine does DMA un-mapping of scatter gather list of scsi command
 * field of @lpfc_cmd.
 **/
static void
lpfc_scsi_unprep_dma_buf(struct lpfc_hba * phba, struct lpfc_scsi_buf * psb)
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
 * lpfc_handler_fcp_err: FCP response handler.
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: Pointer to lpfc_scsi_buf data structure.
 * @rsp_iocb: The response IOCB which contains FCP error.
 *
 * This routine is called to process response IOCB with status field
 * IOSTAT_FCP_RSP_ERROR. This routine sets result field of scsi command
 * based upon SCSI and FCP error.
 **/
static void
lpfc_handle_fcp_err(struct lpfc_vport *vport, struct lpfc_scsi_buf *lpfc_cmd,
		    struct lpfc_iocbq *rsp_iocb)
{
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcpcmd = lpfc_cmd->fcp_cmnd;
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	uint32_t fcpi_parm = rsp_iocb->iocb.un.fcpi.fcpi_parm;
	uint32_t resp_info = fcprsp->rspStatus2;
	uint32_t scsi_status = fcprsp->rspStatus3;
	uint32_t *lp;
	uint32_t host_status = DID_OK;
	uint32_t rsplen = 0;
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

	if ((resp_info & SNS_LEN_VALID) && fcprsp->rspSnsLen) {
		uint32_t snslen = be32_to_cpu(fcprsp->rspSnsLen);
		if (snslen > SCSI_SENSE_BUFFERSIZE)
			snslen = SCSI_SENSE_BUFFERSIZE;

		if (resp_info & RSP_LEN_VALID)
		  rsplen = be32_to_cpu(fcprsp->rspRspLen);
		memcpy(cmnd->sense_buffer, &fcprsp->rspInfo0 + rsplen, snslen);
	}
	lp = (uint32_t *)cmnd->sense_buffer;

	if (!scsi_status && (resp_info & RESID_UNDER))
		logit = LOG_FCP;

	lpfc_printf_vlog(vport, KERN_WARNING, logit,
			 "9024 FCP command x%x failed: x%x SNS x%x x%x "
			 "Data: x%x x%x x%x x%x x%x\n",
			 cmnd->cmnd[0], scsi_status,
			 be32_to_cpu(*lp), be32_to_cpu(*(lp + 3)), resp_info,
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

	scsi_set_resid(cmnd, 0);
	if (resp_info & RESID_UNDER) {
		scsi_set_resid(cmnd, be32_to_cpu(fcprsp->rspResId));

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "9025 FCP Read Underrun, expected %d, "
				 "residual %d Data: x%x x%x x%x\n",
				 be32_to_cpu(fcpcmd->fcpDl),
				 scsi_get_resid(cmnd), fcpi_parm, cmnd->cmnd[0],
				 cmnd->underflow);

		/*
		 * If there is an under run check if under run reported by
		 * storage array is same as the under run reported by HBA.
		 * If this is not same, there is a dropped frame.
		 */
		if ((cmnd->sc_data_direction == DMA_FROM_DEVICE) &&
			fcpi_parm &&
			(scsi_get_resid(cmnd) != fcpi_parm)) {
			lpfc_printf_vlog(vport, KERN_WARNING,
					 LOG_FCP | LOG_FCP_ERROR,
					 "9026 FCP Read Check Error "
					 "and Underrun Data: x%x x%x x%x x%x\n",
					 be32_to_cpu(fcpcmd->fcpDl),
					 scsi_get_resid(cmnd), fcpi_parm,
					 cmnd->cmnd[0]);
			scsi_set_resid(cmnd, scsi_bufflen(cmnd));
			host_status = DID_ERROR;
		}
		/*
		 * The cmnd->underflow is the minimum number of bytes that must
		 * be transfered for this command.  Provided a sense condition
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
				 "Data: x%x x%x \n", cmnd->cmnd[0],
				 scsi_bufflen(cmnd), scsi_get_resid(cmnd));
		host_status = DID_ERROR;

	/*
	 * Check SLI validation that all the transfer was actually done
	 * (fcpi_parm should be zero). Apply check only to reads.
	 */
	} else if ((scsi_status == SAM_STAT_GOOD) && fcpi_parm &&
			(cmnd->sc_data_direction == DMA_FROM_DEVICE)) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP | LOG_FCP_ERROR,
				 "9029 FCP Read Check Error Data: "
				 "x%x x%x x%x x%x\n",
				 be32_to_cpu(fcpcmd->fcpDl),
				 be32_to_cpu(fcprsp->rspResId),
				 fcpi_parm, cmnd->cmnd[0]);
		host_status = DID_ERROR;
		scsi_set_resid(cmnd, scsi_bufflen(cmnd));
	}

 out:
	cmnd->result = ScsiResult(host_status, scsi_status);
	lpfc_send_scsi_error_event(vport->phba, vport, lpfc_cmd, rsp_iocb);
}

/**
 * lpfc_scsi_cmd_iocb_cmpl: Scsi cmnd IOCB completion routine.
 * @phba: The Hba for which this call is being executed.
 * @pIocbIn: The command IOCBQ for the scsi cmnd.
 * @pIocbOut: The response IOCBQ for the scsi cmnd .
 *
 * This routine assigns scsi command result by looking into response IOCB
 * status field appropriately. This routine handles QUEUE FULL condition as
 * well by ramping down device queue depth.
 **/
static void
lpfc_scsi_cmd_iocb_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *pIocbIn,
			struct lpfc_iocbq *pIocbOut)
{
	struct lpfc_scsi_buf *lpfc_cmd =
		(struct lpfc_scsi_buf *) pIocbIn->context1;
	struct lpfc_vport      *vport = pIocbIn->vport;
	struct lpfc_rport_data *rdata = lpfc_cmd->rdata;
	struct lpfc_nodelist *pnode = rdata->pnode;
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	int result;
	struct scsi_device *sdev, *tmp_sdev;
	int depth = 0;
	unsigned long flags;
	struct lpfc_fast_path_event *fast_path_evt;

	lpfc_cmd->result = pIocbOut->iocb.un.ulpWord[4];
	lpfc_cmd->status = pIocbOut->iocb.ulpStatus;
	if (pnode && NLP_CHK_NODE_ACT(pnode))
		atomic_dec(&pnode->cmd_pending);

	if (lpfc_cmd->status) {
		if (lpfc_cmd->status == IOSTAT_LOCAL_REJECT &&
		    (lpfc_cmd->result & IOERR_DRVR_MASK))
			lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
		else if (lpfc_cmd->status >= IOSTAT_CNT)
			lpfc_cmd->status = IOSTAT_DEFAULT;

		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "9030 FCP cmd x%x failed <%d/%d> "
				 "status: x%x result: x%x Data: x%x x%x\n",
				 cmd->cmnd[0],
				 cmd->device ? cmd->device->id : 0xffff,
				 cmd->device ? cmd->device->lun : 0xffff,
				 lpfc_cmd->status, lpfc_cmd->result,
				 pIocbOut->iocb.ulpContext,
				 lpfc_cmd->cur_iocbq.iocb.ulpIoTag);

		switch (lpfc_cmd->status) {
		case IOSTAT_FCP_RSP_ERROR:
			/* Call FCP RSP handler to determine result */
			lpfc_handle_fcp_err(vport, lpfc_cmd, pIocbOut);
			break;
		case IOSTAT_NPORT_BSY:
		case IOSTAT_FABRIC_BSY:
			cmd->result = ScsiResult(DID_TRANSPORT_DISRUPTED, 0);
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
			if (lpfc_cmd->result == IOERR_INVALID_RPI ||
			    lpfc_cmd->result == IOERR_NO_RESOURCES ||
			    lpfc_cmd->result == IOERR_ABORT_REQUESTED) {
				cmd->result = ScsiResult(DID_REQUEUE, 0);
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
							"on unprotected cmd");
				}
			}

		/* else: fall through */
		default:
			cmd->result = ScsiResult(DID_ERROR, 0);
			break;
		}

		if (!pnode || !NLP_CHK_NODE_ACT(pnode)
		    || (pnode->nlp_state != NLP_STE_MAPPED_NODE))
			cmd->result = ScsiResult(DID_TRANSPORT_DISRUPTED,
						 SAM_STAT_BUSY);
	} else {
		cmd->result = ScsiResult(DID_OK, 0);
	}

	if (cmd->result || lpfc_cmd->fcp_rsp->rspSnsLen) {
		uint32_t *lp = (uint32_t *)cmd->sense_buffer;

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "0710 Iodone <%d/%d> cmd %p, error "
				 "x%x SNS x%x x%x Data: x%x x%x\n",
				 cmd->device->id, cmd->device->lun, cmd,
				 cmd->result, *lp, *(lp + 3), cmd->retries,
				 scsi_get_resid(cmd));
	}

	lpfc_update_stats(phba, lpfc_cmd);
	result = cmd->result;
	sdev = cmd->device;
	if (vport->cfg_max_scsicmpl_time &&
	   time_after(jiffies, lpfc_cmd->start_time +
		msecs_to_jiffies(vport->cfg_max_scsicmpl_time))) {
		spin_lock_irqsave(sdev->host->host_lock, flags);
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
		spin_unlock_irqrestore(sdev->host->host_lock, flags);
	} else if (pnode && NLP_CHK_NODE_ACT(pnode)) {
		if ((pnode->cmd_qdepth < LPFC_MAX_TGT_QDEPTH) &&
		   time_after(jiffies, pnode->last_change_time +
			      msecs_to_jiffies(LPFC_TGTQ_INTERVAL))) {
			spin_lock_irqsave(sdev->host->host_lock, flags);
			pnode->cmd_qdepth += pnode->cmd_qdepth *
				LPFC_TGTQ_RAMPUP_PCENT / 100;
			if (pnode->cmd_qdepth > LPFC_MAX_TGT_QDEPTH)
				pnode->cmd_qdepth = LPFC_MAX_TGT_QDEPTH;
			pnode->last_change_time = jiffies;
			spin_unlock_irqrestore(sdev->host->host_lock, flags);
		}
	}

	lpfc_scsi_unprep_dma_buf(phba, lpfc_cmd);
	cmd->scsi_done(cmd);

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		/*
		 * If there is a thread waiting for command completion
		 * wake up the thread.
		 */
		spin_lock_irqsave(sdev->host->host_lock, flags);
		lpfc_cmd->pCmd = NULL;
		if (lpfc_cmd->waitq)
			wake_up(lpfc_cmd->waitq);
		spin_unlock_irqrestore(sdev->host->host_lock, flags);
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return;
	}


	if (!result)
		lpfc_rampup_queue_depth(vport, sdev);

	if (!result && pnode && NLP_CHK_NODE_ACT(pnode) &&
	   ((jiffies - pnode->last_ramp_up_time) >
		LPFC_Q_RAMP_UP_INTERVAL * HZ) &&
	   ((jiffies - pnode->last_q_full_time) >
		LPFC_Q_RAMP_UP_INTERVAL * HZ) &&
	   (vport->cfg_lun_queue_depth > sdev->queue_depth)) {
		shost_for_each_device(tmp_sdev, sdev->host) {
			if (vport->cfg_lun_queue_depth > tmp_sdev->queue_depth){
				if (tmp_sdev->id != sdev->id)
					continue;
				if (tmp_sdev->ordered_tags)
					scsi_adjust_queue_depth(tmp_sdev,
						MSG_ORDERED_TAG,
						tmp_sdev->queue_depth+1);
				else
					scsi_adjust_queue_depth(tmp_sdev,
						MSG_SIMPLE_TAG,
						tmp_sdev->queue_depth+1);

				pnode->last_ramp_up_time = jiffies;
			}
		}
		lpfc_send_sdev_queuedepth_change_event(phba, vport, pnode,
			0xFFFFFFFF,
			sdev->queue_depth - 1, sdev->queue_depth);
	}

	/*
	 * Check for queue full.  If the lun is reporting queue full, then
	 * back off the lun queue depth to prevent target overloads.
	 */
	if (result == SAM_STAT_TASK_SET_FULL && pnode &&
	    NLP_CHK_NODE_ACT(pnode)) {
		pnode->last_q_full_time = jiffies;

		shost_for_each_device(tmp_sdev, sdev->host) {
			if (tmp_sdev->id != sdev->id)
				continue;
			depth = scsi_track_queue_full(tmp_sdev,
					tmp_sdev->queue_depth - 1);
		}
		/*
		 * The queue depth cannot be lowered any more.
		 * Modify the returned error code to store
		 * the final depth value set by
		 * scsi_track_queue_full.
		 */
		if (depth == -1)
			depth = sdev->host->cmd_per_lun;

		if (depth) {
			lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
					 "0711 detected queue full - lun queue "
					 "depth adjusted to %d.\n", depth);
			lpfc_send_sdev_queuedepth_change_event(phba, vport,
				pnode, 0xFFFFFFFF,
				depth+1, depth);
		}
	}

	/*
	 * If there is a thread waiting for command completion
	 * wake up the thread.
	 */
	spin_lock_irqsave(sdev->host->host_lock, flags);
	lpfc_cmd->pCmd = NULL;
	if (lpfc_cmd->waitq)
		wake_up(lpfc_cmd->waitq);
	spin_unlock_irqrestore(sdev->host->host_lock, flags);

	lpfc_release_scsi_buf(phba, lpfc_cmd);
}

/**
 * lpfc_fcpcmd_to_iocb - copy the fcp_cmd data into the IOCB.
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
 * lpfc_scsi_prep_cmnd:  Routine to convert scsi cmnd to FCP information unit.
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: The scsi command which needs to send.
 * @pnode: Pointer to lpfc_nodelist.
 *
 * This routine initializes fcp_cmnd and iocb data structure from scsi command
 * to transfer.
 **/
static void
lpfc_scsi_prep_cmnd(struct lpfc_vport *vport, struct lpfc_scsi_buf *lpfc_cmd,
		    struct lpfc_nodelist *pnode)
{
	struct lpfc_hba *phba = vport->phba;
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	struct lpfc_iocbq *piocbq = &(lpfc_cmd->cur_iocbq);
	int datadir = scsi_cmnd->sc_data_direction;
	char tag[2];

	if (!pnode || !NLP_CHK_NODE_ACT(pnode))
		return;

	lpfc_cmd->fcp_rsp->rspSnsLen = 0;
	/* clear task management bits */
	lpfc_cmd->fcp_cmnd->fcpCntl2 = 0;

	int_to_scsilun(lpfc_cmd->pCmd->device->lun,
			&lpfc_cmd->fcp_cmnd->fcp_lun);

	memcpy(&fcp_cmnd->fcpCdb[0], scsi_cmnd->cmnd, 16);

	if (scsi_populate_tag_msg(scsi_cmnd, tag)) {
		switch (tag[0]) {
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
	if (scsi_sg_count(scsi_cmnd)) {
		if (datadir == DMA_TO_DEVICE) {
			iocb_cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
			iocb_cmd->un.fcpi.fcpi_parm = 0;
			iocb_cmd->ulpPU = 0;
			fcp_cmnd->fcpCntl3 = WRITE_DATA;
			phba->fc4OutputRequests++;
		} else {
			iocb_cmd->ulpCommand = CMD_FCP_IREAD64_CR;
			iocb_cmd->ulpPU = PARM_READ_CHECK;
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
	if (phba->sli_rev == 3 &&
	    !(phba->sli3_options & LPFC_SLI3_BG_ENABLED))
		lpfc_fcpcmd_to_iocb(iocb_cmd->unsli3.fcp_ext.icd, fcp_cmnd);
	/*
	 * Finish initializing those IOCB fields that are independent
	 * of the scsi_cmnd request_buffer
	 */
	piocbq->iocb.ulpContext = pnode->nlp_rpi;
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
 * lpfc_scsi_prep_task_mgmt_cmnd: Convert scsi TM cmnd to FCP information unit.
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: Pointer to lpfc_scsi_buf data structure.
 * @lun: Logical unit number.
 * @task_mgmt_cmd: SCSI task management command.
 *
 * This routine creates FCP information unit corresponding to @task_mgmt_cmd.
 *
 * Return codes:
 *   0 - Error
 *   1 - Success
 **/
static int
lpfc_scsi_prep_task_mgmt_cmd(struct lpfc_vport *vport,
			     struct lpfc_scsi_buf *lpfc_cmd,
			     unsigned int lun,
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

	return 1;
}

/**
 * lpc_taskmgmt_def_cmpl: IOCB completion routine for task management command.
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
	struct lpfc_scsi_buf *lpfc_cmd =
		(struct lpfc_scsi_buf *) cmdiocbq->context1;
	if (lpfc_cmd)
		lpfc_release_scsi_buf(phba, lpfc_cmd);
	return;
}

/**
 * lpfc_scsi_tgt_reset: Target reset handler.
 * @lpfc_cmd: Pointer to lpfc_scsi_buf data structure
 * @vport: The virtual port for which this call is being executed.
 * @tgt_id: Target ID.
 * @lun: Lun number.
 * @rdata: Pointer to lpfc_rport_data.
 *
 * This routine issues a TARGET RESET iocb to reset a target with @tgt_id ID.
 *
 * Return Code:
 *   0x2003 - Error
 *   0x2002 - Success.
 **/
static int
lpfc_scsi_tgt_reset(struct lpfc_scsi_buf *lpfc_cmd, struct lpfc_vport *vport,
		    unsigned  tgt_id, unsigned int lun,
		    struct lpfc_rport_data *rdata)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_iocbq *iocbq;
	struct lpfc_iocbq *iocbqrsp;
	int ret;
	int status;

	if (!rdata->pnode || !NLP_CHK_NODE_ACT(rdata->pnode))
		return FAILED;

	lpfc_cmd->rdata = rdata;
	status = lpfc_scsi_prep_task_mgmt_cmd(vport, lpfc_cmd, lun,
					   FCP_TARGET_RESET);
	if (!status)
		return FAILED;

	iocbq = &lpfc_cmd->cur_iocbq;
	iocbqrsp = lpfc_sli_get_iocbq(phba);

	if (!iocbqrsp)
		return FAILED;

	/* Issue Target Reset to TGT <num> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			 "0702 Issue Target Reset to TGT %d Data: x%x x%x\n",
			 tgt_id, rdata->pnode->nlp_rpi, rdata->pnode->nlp_flag);
	status = lpfc_sli_issue_iocb_wait(phba,
				       &phba->sli.ring[phba->sli.fcp_ring],
				       iocbq, iocbqrsp, lpfc_cmd->timeout);
	if (status != IOCB_SUCCESS) {
		if (status == IOCB_TIMEDOUT) {
			iocbq->iocb_cmpl = lpfc_tskmgmt_def_cmpl;
			ret = TIMEOUT_ERROR;
		} else
			ret = FAILED;
		lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
	} else {
		ret = SUCCESS;
		lpfc_cmd->result = iocbqrsp->iocb.un.ulpWord[4];
		lpfc_cmd->status = iocbqrsp->iocb.ulpStatus;
		if (lpfc_cmd->status == IOSTAT_LOCAL_REJECT &&
			(lpfc_cmd->result & IOERR_DRVR_MASK))
				lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
	}

	lpfc_sli_release_iocbq(phba, iocbqrsp);
	return ret;
}

/**
 * lpfc_info: Info entry point of scsi_host_template data structure.
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

/**
 * lpfc_poll_rearm_time: Routine to modify fcp_poll timer of hba.
 * @phba: The Hba for which this call is being executed.
 *
 * This routine modifies fcp_poll_timer  field of @phba by cfg_poll_tmo.
 * The default value of cfg_poll_tmo is 10 milliseconds.
 **/
static __inline__ void lpfc_poll_rearm_timer(struct lpfc_hba * phba)
{
	unsigned long  poll_tmo_expires =
		(jiffies + msecs_to_jiffies(phba->cfg_poll_tmo));

	if (phba->sli.ring[LPFC_FCP_RING].txcmplq_cnt)
		mod_timer(&phba->fcp_poll_timer,
			  poll_tmo_expires);
}

/**
 * lpfc_poll_start_timer: Routine to start fcp_poll_timer of HBA.
 * @phba: The Hba for which this call is being executed.
 *
 * This routine starts the fcp_poll_timer of @phba.
 **/
void lpfc_poll_start_timer(struct lpfc_hba * phba)
{
	lpfc_poll_rearm_timer(phba);
}

/**
 * lpfc_poll_timeout: Restart polling timer.
 * @ptr: Map to lpfc_hba data structure pointer.
 *
 * This routine restarts fcp_poll timer, when FCP ring  polling is enable
 * and FCP Ring interrupt is disable.
 **/

void lpfc_poll_timeout(unsigned long ptr)
{
	struct lpfc_hba *phba = (struct lpfc_hba *) ptr;

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_poll_fcp_ring (phba);
		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}
}

/**
 * lpfc_queuecommand: Queuecommand entry point of Scsi Host Templater data
 * structure.
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
lpfc_queuecommand(struct scsi_cmnd *cmnd, void (*done) (struct scsi_cmnd *))
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_sli   *psli = &phba->sli;
	struct lpfc_rport_data *rdata = cmnd->device->hostdata;
	struct lpfc_nodelist *ndlp = rdata->pnode;
	struct lpfc_scsi_buf *lpfc_cmd;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));
	int err;

	err = fc_remote_port_chkready(rport);
	if (err) {
		cmnd->result = err;
		goto out_fail_command;
	}

	if (!(phba->sli3_options & LPFC_SLI3_BG_ENABLED) &&
		scsi_get_prot_op(cmnd) != SCSI_PROT_NORMAL) {

		printk(KERN_ERR "BLKGRD ERROR: rcvd protected cmd:%02x op:%02x "
				"str=%s without registering for BlockGuard - "
				"Rejecting command\n",
				cmnd->cmnd[0], scsi_get_prot_op(cmnd),
				dif_op_str[scsi_get_prot_op(cmnd)]);
		goto out_fail_command;
	}

	/*
	 * Catch race where our node has transitioned, but the
	 * transport is still transitioning.
	 */
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		cmnd->result = ScsiResult(DID_TRANSPORT_DISRUPTED, 0);
		goto out_fail_command;
	}
	if (vport->cfg_max_scsicmpl_time &&
		(atomic_read(&ndlp->cmd_pending) >= ndlp->cmd_qdepth))
		goto out_host_busy;

	lpfc_cmd = lpfc_get_scsi_buf(phba);
	if (lpfc_cmd == NULL) {
		lpfc_rampdown_queue_depth(phba);

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
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
	lpfc_cmd->timeout = 0;
	lpfc_cmd->start_time = jiffies;
	cmnd->host_scribble = (unsigned char *)lpfc_cmd;
	cmnd->scsi_done = done;

	if (scsi_get_prot_op(cmnd) != SCSI_PROT_NORMAL) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_BG,
				"9033 BLKGRD: rcvd protected cmd:%02x op:%02x "
				"str=%s\n",
				cmnd->cmnd[0], scsi_get_prot_op(cmnd),
				dif_op_str[scsi_get_prot_op(cmnd)]);
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_BG,
				"9034 BLKGRD: CDB: %02x %02x %02x %02x %02x "
				"%02x %02x %02x %02x %02x \n",
				cmnd->cmnd[0], cmnd->cmnd[1], cmnd->cmnd[2],
				cmnd->cmnd[3], cmnd->cmnd[4], cmnd->cmnd[5],
				cmnd->cmnd[6], cmnd->cmnd[7], cmnd->cmnd[8],
				cmnd->cmnd[9]);
		if (cmnd->cmnd[0] == READ_10)
			lpfc_printf_vlog(vport, KERN_WARNING, LOG_BG,
					"9035 BLKGRD: READ @ sector %llu, "
					 "count %lu\n",
					 (unsigned long long)scsi_get_lba(cmnd),
					cmnd->request->nr_sectors);
		else if (cmnd->cmnd[0] == WRITE_10)
			lpfc_printf_vlog(vport, KERN_WARNING, LOG_BG,
					"9036 BLKGRD: WRITE @ sector %llu, "
					"count %lu cmd=%p\n",
					(unsigned long long)scsi_get_lba(cmnd),
					cmnd->request->nr_sectors,
					cmnd);

		err = lpfc_bg_scsi_prep_dma_buf(phba, lpfc_cmd);
	} else {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_BG,
				"9038 BLKGRD: rcvd unprotected cmd:%02x op:%02x"
				" str=%s\n",
				cmnd->cmnd[0], scsi_get_prot_op(cmnd),
				dif_op_str[scsi_get_prot_op(cmnd)]);
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_BG,
				 "9039 BLKGRD: CDB: %02x %02x %02x %02x %02x "
				 "%02x %02x %02x %02x %02x \n",
				 cmnd->cmnd[0], cmnd->cmnd[1], cmnd->cmnd[2],
				 cmnd->cmnd[3], cmnd->cmnd[4], cmnd->cmnd[5],
				 cmnd->cmnd[6], cmnd->cmnd[7], cmnd->cmnd[8],
				 cmnd->cmnd[9]);
		if (cmnd->cmnd[0] == READ_10)
			lpfc_printf_vlog(vport, KERN_WARNING, LOG_BG,
					 "9040 dbg: READ @ sector %llu, "
					 "count %lu\n",
					 (unsigned long long)scsi_get_lba(cmnd),
					 cmnd->request->nr_sectors);
		else if (cmnd->cmnd[0] == WRITE_10)
			lpfc_printf_vlog(vport, KERN_WARNING, LOG_BG,
					 "9041 dbg: WRITE @ sector %llu, "
					 "count %lu cmd=%p\n",
					 (unsigned long long)scsi_get_lba(cmnd),
					 cmnd->request->nr_sectors, cmnd);
		else
			lpfc_printf_vlog(vport, KERN_WARNING, LOG_BG,
					 "9042 dbg: parser not implemented\n");
		err = lpfc_scsi_prep_dma_buf(phba, lpfc_cmd);
	}

	if (err)
		goto out_host_busy_free_buf;

	lpfc_scsi_prep_cmnd(vport, lpfc_cmd, ndlp);

	atomic_inc(&ndlp->cmd_pending);
	err = lpfc_sli_issue_iocb(phba, &phba->sli.ring[psli->fcp_ring],
				  &lpfc_cmd->cur_iocbq, SLI_IOCB_RET_IOCB);
	if (err) {
		atomic_dec(&ndlp->cmd_pending);
		goto out_host_busy_free_buf;
	}
	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_poll_fcp_ring(phba);
		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}

	return 0;

 out_host_busy_free_buf:
	lpfc_scsi_unprep_dma_buf(phba, lpfc_cmd);
	lpfc_release_scsi_buf(phba, lpfc_cmd);
 out_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

 out_fail_command:
	done(cmnd);
	return 0;
}

/**
 * lpfc_block_error_handler: Routine to block error  handler.
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 *  This routine blocks execution till fc_rport state is not FC_PORSTAT_BLCOEKD.
 **/
static void
lpfc_block_error_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));

	spin_lock_irq(shost->host_lock);
	while (rport->port_state == FC_PORTSTATE_BLOCKED) {
		spin_unlock_irq(shost->host_lock);
		msleep(1000);
		spin_lock_irq(shost->host_lock);
	}
	spin_unlock_irq(shost->host_lock);
	return;
}

/**
 * lpfc_abort_handler: Eh_abort_handler entry point of Scsi Host Template data
 *structure.
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
	struct lpfc_sli_ring *pring = &phba->sli.ring[phba->sli.fcp_ring];
	struct lpfc_iocbq *iocb;
	struct lpfc_iocbq *abtsiocb;
	struct lpfc_scsi_buf *lpfc_cmd;
	IOCB_t *cmd, *icmd;
	int ret = SUCCESS;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(waitq);

	lpfc_block_error_handler(cmnd);
	lpfc_cmd = (struct lpfc_scsi_buf *)cmnd->host_scribble;
	BUG_ON(!lpfc_cmd);

	/*
	 * If pCmd field of the corresponding lpfc_scsi_buf structure
	 * points to a different SCSI command, then the driver has
	 * already completed this command, but the midlayer did not
	 * see the completion before the eh fired.  Just return
	 * SUCCESS.
	 */
	iocb = &lpfc_cmd->cur_iocbq;
	if (lpfc_cmd->pCmd != cmnd)
		goto out;

	BUG_ON(iocb->context1 != lpfc_cmd);

	abtsiocb = lpfc_sli_get_iocbq(phba);
	if (abtsiocb == NULL) {
		ret = FAILED;
		goto out;
	}

	/*
	 * The scsi command can not be in txq and it is in flight because the
	 * pCmd is still pointig at the SCSI command we have to abort. There
	 * is no need to search the txcmplq. Just send an abort to the FW.
	 */

	cmd = &iocb->iocb;
	icmd = &abtsiocb->iocb;
	icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
	icmd->un.acxri.abortContextTag = cmd->ulpContext;
	icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

	icmd->ulpLe = 1;
	icmd->ulpClass = cmd->ulpClass;
	if (lpfc_is_link_up(phba))
		icmd->ulpCommand = CMD_ABORT_XRI_CN;
	else
		icmd->ulpCommand = CMD_CLOSE_XRI_CN;

	abtsiocb->iocb_cmpl = lpfc_sli_abort_fcp_cmpl;
	abtsiocb->vport = vport;
	if (lpfc_sli_issue_iocb(phba, pring, abtsiocb, 0) == IOCB_ERROR) {
		lpfc_sli_release_iocbq(phba, abtsiocb);
		ret = FAILED;
		goto out;
	}

	if (phba->cfg_poll & DISABLE_FCP_RING_INT)
		lpfc_sli_poll_fcp_ring (phba);

	lpfc_cmd->waitq = &waitq;
	/* Wait for abort to complete */
	wait_event_timeout(waitq,
			  (lpfc_cmd->pCmd != cmnd),
			   (2*vport->cfg_devloss_tmo*HZ));

	spin_lock_irq(shost->host_lock);
	lpfc_cmd->waitq = NULL;
	spin_unlock_irq(shost->host_lock);

	if (lpfc_cmd->pCmd == cmnd) {
		ret = FAILED;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0748 abort handler timed out waiting "
				 "for abort to complete: ret %#x, ID %d, "
				 "LUN %d, snum %#lx\n",
				 ret, cmnd->device->id, cmnd->device->lun,
				 cmnd->serial_number);
	}

 out:
	lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			 "0749 SCSI Layer I/O Abort Request Status x%x ID %d "
			 "LUN %d snum %#lx\n", ret, cmnd->device->id,
			 cmnd->device->lun, cmnd->serial_number);
	return ret;
}

/**
 * lpfc_device_reset_handler: eh_device_reset entry point of Scsi Host Template
 *data structure.
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does a device reset by sending a TARGET_RESET task management
 * command.
 *
 * Return code :
 *  0x2003 - Error
 *  0ex2002 - Success
 **/
static int
lpfc_device_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_scsi_buf *lpfc_cmd;
	struct lpfc_iocbq *iocbq, *iocbqrsp;
	struct lpfc_rport_data *rdata = cmnd->device->hostdata;
	struct lpfc_nodelist *pnode = rdata->pnode;
	unsigned long later;
	int ret = SUCCESS;
	int status;
	int cnt;
	struct lpfc_scsi_event_header scsi_event;

	lpfc_block_error_handler(cmnd);
	/*
	 * If target is not in a MAPPED state, delay the reset until
	 * target is rediscovered or devloss timeout expires.
	 */
	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies)) {
		if (!pnode || !NLP_CHK_NODE_ACT(pnode))
			return FAILED;
		if (pnode->nlp_state == NLP_STE_MAPPED_NODE)
			break;
		schedule_timeout_uninterruptible(msecs_to_jiffies(500));
		rdata = cmnd->device->hostdata;
		if (!rdata)
			break;
		pnode = rdata->pnode;
	}

	scsi_event.event_type = FC_REG_SCSI_EVENT;
	scsi_event.subcategory = LPFC_EVENT_TGTRESET;
	scsi_event.lun = 0;
	memcpy(scsi_event.wwpn, &pnode->nlp_portname, sizeof(struct lpfc_name));
	memcpy(scsi_event.wwnn, &pnode->nlp_nodename, sizeof(struct lpfc_name));

	fc_host_post_vendor_event(shost,
		fc_get_event_number(),
		sizeof(scsi_event),
		(char *)&scsi_event,
		LPFC_NL_VENDOR_ID);

	if (!rdata || pnode->nlp_state != NLP_STE_MAPPED_NODE) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0721 LUN Reset rport "
				 "failure: msec x%x rdata x%p\n",
				 jiffies_to_msecs(jiffies - later), rdata);
		return FAILED;
	}
	lpfc_cmd = lpfc_get_scsi_buf(phba);
	if (lpfc_cmd == NULL)
		return FAILED;
	lpfc_cmd->timeout = 60;
	lpfc_cmd->rdata = rdata;

	status = lpfc_scsi_prep_task_mgmt_cmd(vport, lpfc_cmd,
					      cmnd->device->lun,
					      FCP_TARGET_RESET);
	if (!status) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}
	iocbq = &lpfc_cmd->cur_iocbq;

	/* get a buffer for this IOCB command response */
	iocbqrsp = lpfc_sli_get_iocbq(phba);
	if (iocbqrsp == NULL) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}
	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			 "0703 Issue target reset to TGT %d LUN %d "
			 "rpi x%x nlp_flag x%x\n", cmnd->device->id,
			 cmnd->device->lun, pnode->nlp_rpi, pnode->nlp_flag);
	status = lpfc_sli_issue_iocb_wait(phba,
					  &phba->sli.ring[phba->sli.fcp_ring],
					  iocbq, iocbqrsp, lpfc_cmd->timeout);
	if (status == IOCB_TIMEDOUT) {
		iocbq->iocb_cmpl = lpfc_tskmgmt_def_cmpl;
		ret = TIMEOUT_ERROR;
	} else {
		if (status != IOCB_SUCCESS)
			ret = FAILED;
		lpfc_release_scsi_buf(phba, lpfc_cmd);
	}
	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "0713 SCSI layer issued device reset (%d, %d) "
			 "return x%x status x%x result x%x\n",
			 cmnd->device->id, cmnd->device->lun, ret,
			 iocbqrsp->iocb.ulpStatus,
			 iocbqrsp->iocb.un.ulpWord[4]);
	lpfc_sli_release_iocbq(phba, iocbqrsp);
	cnt = lpfc_sli_sum_iocb(vport, cmnd->device->id, cmnd->device->lun,
				LPFC_CTX_TGT);
	if (cnt)
		lpfc_sli_abort_iocb(vport, &phba->sli.ring[phba->sli.fcp_ring],
				    cmnd->device->id, cmnd->device->lun,
				    LPFC_CTX_TGT);
	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies) && cnt) {
		schedule_timeout_uninterruptible(msecs_to_jiffies(20));
		cnt = lpfc_sli_sum_iocb(vport, cmnd->device->id,
					cmnd->device->lun, LPFC_CTX_TGT);
	}
	if (cnt) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0719 device reset I/O flush failure: "
				 "cnt x%x\n", cnt);
		ret = FAILED;
	}
	return ret;
}

/**
 * lpfc_bus_reset_handler: eh_bus_reset_handler entry point of Scsi Host
 * Template data structure.
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does target reset to all target on @cmnd->device->host.
 *
 * Return Code:
 *   0x2003 - Error
 *   0x2002 - Success
 **/
static int
lpfc_bus_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_nodelist *ndlp = NULL;
	int match;
	int ret = SUCCESS, status = SUCCESS, i;
	int cnt;
	struct lpfc_scsi_buf * lpfc_cmd;
	unsigned long later;
	struct lpfc_scsi_event_header scsi_event;

	scsi_event.event_type = FC_REG_SCSI_EVENT;
	scsi_event.subcategory = LPFC_EVENT_BUSRESET;
	scsi_event.lun = 0;
	memcpy(scsi_event.wwpn, &vport->fc_portname, sizeof(struct lpfc_name));
	memcpy(scsi_event.wwnn, &vport->fc_nodename, sizeof(struct lpfc_name));

	fc_host_post_vendor_event(shost,
		fc_get_event_number(),
		sizeof(scsi_event),
		(char *)&scsi_event,
		LPFC_NL_VENDOR_ID);

	lpfc_block_error_handler(cmnd);
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
			if (ndlp->nlp_state == NLP_STE_MAPPED_NODE &&
			    ndlp->nlp_sid == i &&
			    ndlp->rport) {
				match = 1;
				break;
			}
		}
		spin_unlock_irq(shost->host_lock);
		if (!match)
			continue;
		lpfc_cmd = lpfc_get_scsi_buf(phba);
		if (lpfc_cmd) {
			lpfc_cmd->timeout = 60;
			status = lpfc_scsi_tgt_reset(lpfc_cmd, vport, i,
						     cmnd->device->lun,
						     ndlp->rport->dd_data);
			if (status != TIMEOUT_ERROR)
				lpfc_release_scsi_buf(phba, lpfc_cmd);
		}
		if (!lpfc_cmd || status != SUCCESS) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
					 "0700 Bus Reset on target %d failed\n",
					 i);
			ret = FAILED;
		}
	}
	/*
	 * All outstanding txcmplq I/Os should have been aborted by
	 * the targets.  Unfortunately, some targets do not abide by
	 * this forcing the driver to double check.
	 */
	cnt = lpfc_sli_sum_iocb(vport, 0, 0, LPFC_CTX_HOST);
	if (cnt)
		lpfc_sli_abort_iocb(vport, &phba->sli.ring[phba->sli.fcp_ring],
				    0, 0, LPFC_CTX_HOST);
	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies) && cnt) {
		schedule_timeout_uninterruptible(msecs_to_jiffies(20));
		cnt = lpfc_sli_sum_iocb(vport, 0, 0, LPFC_CTX_HOST);
	}
	if (cnt) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0715 Bus Reset I/O flush failure: "
				 "cnt x%x left x%x\n", cnt, i);
		ret = FAILED;
	}
	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "0714 SCSI layer issued Bus Reset Data: x%x\n", ret);
	return ret;
}

/**
 * lpfc_slave_alloc: slave_alloc entry point of Scsi Host Template data
 * structure.
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
	struct lpfc_scsi_buf *scsi_buf = NULL;
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	uint32_t total = 0, i;
	uint32_t num_to_alloc = 0;
	unsigned long flags;

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	sdev->hostdata = rport->dd_data;

	/*
	 * Populate the cmds_per_lun count scsi_bufs into this host's globally
	 * available list of scsi buffers.  Don't allocate more than the
	 * HBA limit conveyed to the midlayer via the host structure.  The
	 * formula accounts for the lun_queue_depth + error handlers + 1
	 * extra.  This list of scsi bufs exists for the lifetime of the driver.
	 */
	total = phba->total_scsi_bufs;
	num_to_alloc = vport->cfg_lun_queue_depth + 2;

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

	for (i = 0; i < num_to_alloc; i++) {
		scsi_buf = lpfc_new_scsi_buf(vport);
		if (!scsi_buf) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
					 "0706 Failed to allocate "
					 "command buffer\n");
			break;
		}

		spin_lock_irqsave(&phba->scsi_buf_list_lock, flags);
		phba->total_scsi_bufs++;
		list_add_tail(&scsi_buf->list, &phba->lpfc_scsi_buf_list);
		spin_unlock_irqrestore(&phba->scsi_buf_list_lock, flags);
	}
	return 0;
}

/**
 * lpfc_slave_configure: slave_configure entry point of Scsi Host Templater data
 *  structure.
 * @sdev: Pointer to scsi_device.
 *
 * This routine configures following items
 *   - Tag command queuing support for @sdev if supported.
 *   - Dev loss time out value of fc_rport.
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
	struct fc_rport   *rport = starget_to_rport(sdev->sdev_target);

	if (sdev->tagged_supported)
		scsi_activate_tcq(sdev, vport->cfg_lun_queue_depth);
	else
		scsi_deactivate_tcq(sdev, vport->cfg_lun_queue_depth);

	/*
	 * Initialize the fc transport attributes for the target
	 * containing this scsi device.  Also note that the driver's
	 * target pointer is stored in the starget_data for the
	 * driver's sysfs entry point functions.
	 */
	rport->dev_loss_tmo = vport->cfg_devloss_tmo;

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_poll_fcp_ring(phba);
		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}

	return 0;
}

/**
 * lpfc_slave_destroy: slave_destroy entry point of SHT data structure.
 * @sdev: Pointer to scsi_device.
 *
 * This routine sets @sdev hostatdata filed to null.
 **/
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
	.eh_device_reset_handler= lpfc_device_reset_handler,
	.eh_bus_reset_handler	= lpfc_bus_reset_handler,
	.slave_alloc		= lpfc_slave_alloc,
	.slave_configure	= lpfc_slave_configure,
	.slave_destroy		= lpfc_slave_destroy,
	.scan_finished		= lpfc_scan_finished,
	.this_id		= -1,
	.sg_tablesize		= LPFC_DEFAULT_SG_SEG_CNT,
	.cmd_per_lun		= LPFC_CMD_PER_LUN,
	.use_clustering		= ENABLE_CLUSTERING,
	.shost_attrs		= lpfc_hba_attrs,
	.max_sectors		= 0xFFFF,
};

struct scsi_host_template lpfc_vport_template = {
	.module			= THIS_MODULE,
	.name			= LPFC_DRIVER_NAME,
	.info			= lpfc_info,
	.queuecommand		= lpfc_queuecommand,
	.eh_abort_handler	= lpfc_abort_handler,
	.eh_device_reset_handler= lpfc_device_reset_handler,
	.eh_bus_reset_handler	= lpfc_bus_reset_handler,
	.slave_alloc		= lpfc_slave_alloc,
	.slave_configure	= lpfc_slave_configure,
	.slave_destroy		= lpfc_slave_destroy,
	.scan_finished		= lpfc_scan_finished,
	.this_id		= -1,
	.sg_tablesize		= LPFC_DEFAULT_SG_SEG_CNT,
	.cmd_per_lun		= LPFC_CMD_PER_LUN,
	.use_clustering		= ENABLE_CLUSTERING,
	.shost_attrs		= lpfc_vport_attrs,
	.max_sectors		= 0xFFFF,
};
