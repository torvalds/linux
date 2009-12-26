/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  bfad_im.c Linux driver IM module.
 */

#include "bfad_drv.h"
#include "bfad_im.h"
#include "bfad_trcmod.h"
#include "bfa_cb_ioim_macros.h"
#include <fcb/bfa_fcb_fcpim.h>

BFA_TRC_FILE(LDRV, IM);

DEFINE_IDR(bfad_im_port_index);
struct scsi_transport_template *bfad_im_scsi_transport_template;
static void bfad_im_itnim_work_handler(struct work_struct *work);
static int bfad_im_queuecommand(struct scsi_cmnd *cmnd,
		void (*done)(struct scsi_cmnd *));
static int bfad_im_slave_alloc(struct scsi_device *sdev);

void
bfa_cb_ioim_done(void *drv, struct bfad_ioim_s *dio,
			enum bfi_ioim_status io_status, u8 scsi_status,
			int sns_len, u8 *sns_info, s32 residue)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dio;
	struct bfad_s         *bfad = drv;
	struct bfad_itnim_data_s *itnim_data;
	struct bfad_itnim_s *itnim;

	switch (io_status) {
	case BFI_IOIM_STS_OK:
		bfa_trc(bfad, scsi_status);
		cmnd->result = ScsiResult(DID_OK, scsi_status);
		scsi_set_resid(cmnd, 0);

		if (sns_len > 0) {
			bfa_trc(bfad, sns_len);
			if (sns_len > SCSI_SENSE_BUFFERSIZE)
				sns_len = SCSI_SENSE_BUFFERSIZE;
			memcpy(cmnd->sense_buffer, sns_info, sns_len);
		}
		if (residue > 0)
			scsi_set_resid(cmnd, residue);
		break;

	case BFI_IOIM_STS_ABORTED:
	case BFI_IOIM_STS_TIMEDOUT:
	case BFI_IOIM_STS_PATHTOV:
	default:
		cmnd->result = ScsiResult(DID_ERROR, 0);
	}

	/* Unmap DMA, if host is NULL, it means a scsi passthru cmd */
	if (cmnd->device->host != NULL)
		scsi_dma_unmap(cmnd);

	cmnd->host_scribble = NULL;
	bfa_trc(bfad, cmnd->result);

	itnim_data = cmnd->device->hostdata;
	if (itnim_data) {
		itnim = itnim_data->itnim;
		if (!cmnd->result && itnim &&
			 (bfa_lun_queue_depth > cmnd->device->queue_depth)) {
			/* Queue depth adjustment for good status completion */
			bfad_os_ramp_up_qdepth(itnim, cmnd->device);
		} else if (cmnd->result == SAM_STAT_TASK_SET_FULL && itnim) {
			/* qfull handling */
			bfad_os_handle_qfull(itnim, cmnd->device);
		}
	}

	cmnd->scsi_done(cmnd);
}

void
bfa_cb_ioim_good_comp(void *drv, struct bfad_ioim_s *dio)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dio;
	struct bfad_itnim_data_s *itnim_data;
	struct bfad_itnim_s *itnim;

	cmnd->result = ScsiResult(DID_OK, SCSI_STATUS_GOOD);

	/* Unmap DMA, if host is NULL, it means a scsi passthru cmd */
	if (cmnd->device->host != NULL)
		scsi_dma_unmap(cmnd);

	cmnd->host_scribble = NULL;

	/* Queue depth adjustment */
	if (bfa_lun_queue_depth > cmnd->device->queue_depth) {
		itnim_data = cmnd->device->hostdata;
		if (itnim_data) {
			itnim = itnim_data->itnim;
			if (itnim)
				bfad_os_ramp_up_qdepth(itnim, cmnd->device);
		}
	}

	cmnd->scsi_done(cmnd);
}

void
bfa_cb_ioim_abort(void *drv, struct bfad_ioim_s *dio)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dio;
	struct bfad_s         *bfad = drv;

	cmnd->result = ScsiResult(DID_ERROR, 0);

	/* Unmap DMA, if host is NULL, it means a scsi passthru cmd */
	if (cmnd->device->host != NULL)
		scsi_dma_unmap(cmnd);

	bfa_trc(bfad, cmnd->result);
	cmnd->host_scribble = NULL;
}

void
bfa_cb_tskim_done(void *bfad, struct bfad_tskim_s *dtsk,
		   enum bfi_tskim_status tsk_status)
{
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)dtsk;
	wait_queue_head_t *wq;

	cmnd->SCp.Status |= tsk_status << 1;
	set_bit(IO_DONE_BIT, (unsigned long *)&cmnd->SCp.Status);
	wq = (wait_queue_head_t *) cmnd->SCp.ptr;
	cmnd->SCp.ptr = NULL;

	if (wq)
		wake_up(wq);
}

void
bfa_cb_ioim_resfree(void *drv)
{
}

/**
 *  Scsi_Host_template SCSI host template
 */
/**
 * Scsi_Host template entry, returns BFAD PCI info.
 */
static const char *
bfad_im_info(struct Scsi_Host *shost)
{
	static char     bfa_buf[256];
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfa_ioc_attr_s  ioc_attr;
	struct bfad_s         *bfad = im_port->bfad;

	memset(&ioc_attr, 0, sizeof(ioc_attr));
	bfa_get_attr(&bfad->bfa, &ioc_attr);

	memset(bfa_buf, 0, sizeof(bfa_buf));
	snprintf(bfa_buf, sizeof(bfa_buf),
		 "Brocade FC/FCOE Adapter, " "model: %s hwpath: %s driver: %s",
		 ioc_attr.adapter_attr.model, bfad->pci_name,
		 BFAD_DRIVER_VERSION);
	return bfa_buf;
}

/**
 * Scsi_Host template entry, aborts the specified SCSI command.
 *
 * Returns: SUCCESS or FAILED.
 */
static int
bfad_im_abort_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_ioim_s *hal_io;
	unsigned long   flags;
	u32        timeout;
	int             rc = FAILED;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	hal_io = (struct bfa_ioim_s *) cmnd->host_scribble;
	if (!hal_io) {
		/* IO has been completed, retrun success */
		rc = SUCCESS;
		goto out;
	}
	if (hal_io->dio != (struct bfad_ioim_s *) cmnd) {
		rc = FAILED;
		goto out;
	}

	bfa_trc(bfad, hal_io->iotag);
	bfa_log(bfad->logmod, BFA_LOG_LINUX_SCSI_ABORT,
		im_port->shost->host_no, cmnd, hal_io->iotag);
	bfa_ioim_abort(hal_io);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	/* Need to wait until the command get aborted */
	timeout = 10;
	while ((struct bfa_ioim_s *) cmnd->host_scribble == hal_io) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(timeout);
		if (timeout < 4 * HZ)
			timeout *= 2;
	}

	cmnd->scsi_done(cmnd);
	bfa_trc(bfad, hal_io->iotag);
	bfa_log(bfad->logmod, BFA_LOG_LINUX_SCSI_ABORT_COMP,
		im_port->shost->host_no, cmnd, hal_io->iotag);
	return SUCCESS;
out:
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	return rc;
}

static bfa_status_t
bfad_im_target_reset_send(struct bfad_s *bfad, struct scsi_cmnd *cmnd,
		     struct bfad_itnim_s *itnim)
{
	struct bfa_tskim_s *tskim;
	struct bfa_itnim_s *bfa_itnim;
	bfa_status_t    rc = BFA_STATUS_OK;

	bfa_itnim = bfa_fcs_itnim_get_halitn(&itnim->fcs_itnim);
	tskim = bfa_tskim_alloc(&bfad->bfa, (struct bfad_tskim_s *) cmnd);
	if (!tskim) {
		BFA_DEV_PRINTF(bfad, BFA_ERR,
			       "target reset, fail to allocate tskim\n");
		rc = BFA_STATUS_FAILED;
		goto out;
	}

	/*
	 * Set host_scribble to NULL to avoid aborting a task command if
	 * happens.
	 */
	cmnd->host_scribble = NULL;
	cmnd->SCp.Status = 0;
	bfa_itnim = bfa_fcs_itnim_get_halitn(&itnim->fcs_itnim);
	bfa_tskim_start(tskim, bfa_itnim, (lun_t)0,
			    FCP_TM_TARGET_RESET, BFAD_TARGET_RESET_TMO);
out:
	return rc;
}

/**
 * Scsi_Host template entry, resets a LUN and abort its all commands.
 *
 * Returns: SUCCESS or FAILED.
 *
 */
static int
bfad_im_reset_lun_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_itnim_data_s *itnim_data = cmnd->device->hostdata;
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_tskim_s *tskim;
	struct bfad_itnim_s   *itnim;
	struct bfa_itnim_s *bfa_itnim;
	DECLARE_WAIT_QUEUE_HEAD(wq);
	int             rc = SUCCESS;
	unsigned long   flags;
	enum bfi_tskim_status task_status;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	itnim = itnim_data->itnim;
	if (!itnim) {
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		rc = FAILED;
		goto out;
	}

	tskim = bfa_tskim_alloc(&bfad->bfa, (struct bfad_tskim_s *) cmnd);
	if (!tskim) {
		BFA_DEV_PRINTF(bfad, BFA_ERR,
				"LUN reset, fail to allocate tskim");
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		rc = FAILED;
		goto out;
	}

	/**
	 * Set host_scribble to NULL to avoid aborting a task command
	 * if happens.
	 */
	cmnd->host_scribble = NULL;
	cmnd->SCp.ptr = (char *)&wq;
	cmnd->SCp.Status = 0;
	bfa_itnim = bfa_fcs_itnim_get_halitn(&itnim->fcs_itnim);
	bfa_tskim_start(tskim, bfa_itnim,
			    bfad_int_to_lun(cmnd->device->lun),
			    FCP_TM_LUN_RESET, BFAD_LUN_RESET_TMO);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	wait_event(wq, test_bit(IO_DONE_BIT,
			(unsigned long *)&cmnd->SCp.Status));

	task_status = cmnd->SCp.Status >> 1;
	if (task_status != BFI_TSKIM_STS_OK) {
		BFA_DEV_PRINTF(bfad, BFA_ERR, "LUN reset failure, status: %d\n",
			       task_status);
		rc = FAILED;
	}

out:
	return rc;
}

/**
 * Scsi_Host template entry, resets the bus and abort all commands.
 */
static int
bfad_im_reset_bus_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct bfad_im_port_s *im_port =
				(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfad_itnim_s   *itnim;
	unsigned long   flags;
	u32        i, rc, err_cnt = 0;
	DECLARE_WAIT_QUEUE_HEAD(wq);
	enum bfi_tskim_status task_status;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	for (i = 0; i < MAX_FCP_TARGET; i++) {
		itnim = bfad_os_get_itnim(im_port, i);
		if (itnim) {
			cmnd->SCp.ptr = (char *)&wq;
			rc = bfad_im_target_reset_send(bfad, cmnd, itnim);
			if (rc != BFA_STATUS_OK) {
				err_cnt++;
				continue;
			}

			/* wait target reset to complete */
			spin_unlock_irqrestore(&bfad->bfad_lock, flags);
			wait_event(wq, test_bit(IO_DONE_BIT,
					(unsigned long *)&cmnd->SCp.Status));
			spin_lock_irqsave(&bfad->bfad_lock, flags);

			task_status = cmnd->SCp.Status >> 1;
			if (task_status != BFI_TSKIM_STS_OK) {
				BFA_DEV_PRINTF(bfad, BFA_ERR,
					"target reset failure,"
					" status: %d\n", task_status);
				err_cnt++;
			}
		}
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (err_cnt)
		return FAILED;

	return SUCCESS;
}

/**
 * Scsi_Host template entry slave_destroy.
 */
static void
bfad_im_slave_destroy(struct scsi_device *sdev)
{
	sdev->hostdata = NULL;
	return;
}

/**
 *  BFA FCS itnim callbacks
 */

/**
 * BFA FCS itnim alloc callback, after successful PRLI
 * Context: Interrupt
 */
void
bfa_fcb_itnim_alloc(struct bfad_s *bfad, struct bfa_fcs_itnim_s **itnim,
		    struct bfad_itnim_s **itnim_drv)
{
	*itnim_drv = kzalloc(sizeof(struct bfad_itnim_s), GFP_ATOMIC);
	if (*itnim_drv == NULL)
		return;

	(*itnim_drv)->im = bfad->im;
	*itnim = &(*itnim_drv)->fcs_itnim;
	(*itnim_drv)->state = ITNIM_STATE_NONE;

	/*
	 * Initiaze the itnim_work
	 */
	INIT_WORK(&(*itnim_drv)->itnim_work, bfad_im_itnim_work_handler);
	bfad->bfad_flags |= BFAD_RPORT_ONLINE;
}

/**
 * BFA FCS itnim free callback.
 * Context: Interrupt. bfad_lock is held
 */
void
bfa_fcb_itnim_free(struct bfad_s *bfad, struct bfad_itnim_s *itnim_drv)
{
	struct bfad_port_s    *port;
	wwn_t wwpn;
	u32 fcid;
	char wwpn_str[32], fcid_str[16];

	/* online to free state transtion should not happen */
	bfa_assert(itnim_drv->state != ITNIM_STATE_ONLINE);

	itnim_drv->queue_work = 1;
	/* offline request is not yet done, use the same request to free */
	if (itnim_drv->state == ITNIM_STATE_OFFLINE_PENDING)
		itnim_drv->queue_work = 0;

	itnim_drv->state = ITNIM_STATE_FREE;
	port = bfa_fcs_itnim_get_drvport(&itnim_drv->fcs_itnim);
	itnim_drv->im_port = port->im_port;
	wwpn = bfa_fcs_itnim_get_pwwn(&itnim_drv->fcs_itnim);
	fcid = bfa_fcs_itnim_get_fcid(&itnim_drv->fcs_itnim);
	wwn2str(wwpn_str, wwpn);
	fcid2str(fcid_str, fcid);
	bfa_log(bfad->logmod, BFA_LOG_LINUX_ITNIM_FREE,
		port->im_port->shost->host_no,
		fcid_str, wwpn_str);
	bfad_os_itnim_process(itnim_drv);
}

/**
 * BFA FCS itnim online callback.
 * Context: Interrupt. bfad_lock is held
 */
void
bfa_fcb_itnim_online(struct bfad_itnim_s *itnim_drv)
{
	struct bfad_port_s    *port;

	itnim_drv->bfa_itnim = bfa_fcs_itnim_get_halitn(&itnim_drv->fcs_itnim);
	port = bfa_fcs_itnim_get_drvport(&itnim_drv->fcs_itnim);
	itnim_drv->state = ITNIM_STATE_ONLINE;
	itnim_drv->queue_work = 1;
	itnim_drv->im_port = port->im_port;
	bfad_os_itnim_process(itnim_drv);
}

/**
 * BFA FCS itnim offline callback.
 * Context: Interrupt. bfad_lock is held
 */
void
bfa_fcb_itnim_offline(struct bfad_itnim_s *itnim_drv)
{
	struct bfad_port_s    *port;
	struct bfad_s *bfad;

	port = bfa_fcs_itnim_get_drvport(&itnim_drv->fcs_itnim);
	bfad = port->bfad;
	if ((bfad->pport.flags & BFAD_PORT_DELETE) ||
		 (port->flags & BFAD_PORT_DELETE)) {
		itnim_drv->state = ITNIM_STATE_OFFLINE;
		return;
	}
	itnim_drv->im_port = port->im_port;
	itnim_drv->state = ITNIM_STATE_OFFLINE_PENDING;
	itnim_drv->queue_work = 1;
	bfad_os_itnim_process(itnim_drv);
}

/**
 * BFA FCS itnim timeout callback.
 * Context: Interrupt. bfad_lock is held
 */
void bfa_fcb_itnim_tov(struct bfad_itnim_s *itnim)
{
	itnim->state = ITNIM_STATE_TIMEOUT;
}

/**
 * Path TOV processing begin notification -- dummy for linux
 */
void
bfa_fcb_itnim_tov_begin(struct bfad_itnim_s *itnim)
{
}



/**
 * Allocate a Scsi_Host for a port.
 */
int
bfad_im_scsi_host_alloc(struct bfad_s *bfad, struct bfad_im_port_s *im_port)
{
	int error = 1;

	if (!idr_pre_get(&bfad_im_port_index, GFP_KERNEL)) {
		printk(KERN_WARNING "idr_pre_get failure\n");
		goto out;
	}

	error = idr_get_new(&bfad_im_port_index, im_port,
					 &im_port->idr_id);
	if (error) {
		printk(KERN_WARNING "idr_get_new failure\n");
		goto out;
	}

	im_port->shost = bfad_os_scsi_host_alloc(im_port, bfad);
	if (!im_port->shost) {
		error = 1;
		goto out_free_idr;
	}

	im_port->shost->hostdata[0] = (unsigned long)im_port;
	im_port->shost->unique_id = im_port->idr_id;
	im_port->shost->this_id = -1;
	im_port->shost->max_id = MAX_FCP_TARGET;
	im_port->shost->max_lun = MAX_FCP_LUN;
	im_port->shost->max_cmd_len = 16;
	im_port->shost->can_queue = bfad->cfg_data.ioc_queue_depth;
	im_port->shost->transportt = bfad_im_scsi_transport_template;

	error = bfad_os_scsi_add_host(im_port->shost, im_port, bfad);
	if (error) {
		printk(KERN_WARNING "bfad_os_scsi_add_host failure %d\n",
							error);
		goto out_fc_rel;
	}

	/* setup host fixed attribute if the lk supports */
	bfad_os_fc_host_init(im_port);

	return 0;

out_fc_rel:
	scsi_host_put(im_port->shost);
out_free_idr:
	idr_remove(&bfad_im_port_index, im_port->idr_id);
out:
	return error;
}

void
bfad_im_scsi_host_free(struct bfad_s *bfad, struct bfad_im_port_s *im_port)
{
	unsigned long flags;

	bfa_trc(bfad, bfad->inst_no);
	bfa_log(bfad->logmod, BFA_LOG_LINUX_SCSI_HOST_FREE,
			im_port->shost->host_no);

	fc_remove_host(im_port->shost);

	scsi_remove_host(im_port->shost);
	scsi_host_put(im_port->shost);

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	idr_remove(&bfad_im_port_index, im_port->idr_id);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

static void
bfad_im_port_delete_handler(struct work_struct *work)
{
	struct bfad_im_port_s *im_port =
		container_of(work, struct bfad_im_port_s, port_delete_work);

	bfad_im_scsi_host_free(im_port->bfad, im_port);
	bfad_im_port_clean(im_port);
	kfree(im_port);
}

bfa_status_t
bfad_im_port_new(struct bfad_s *bfad, struct bfad_port_s *port)
{
	int             rc = BFA_STATUS_OK;
	struct bfad_im_port_s *im_port;

	im_port = kzalloc(sizeof(struct bfad_im_port_s), GFP_ATOMIC);
	if (im_port == NULL) {
		rc = BFA_STATUS_ENOMEM;
		goto ext;
	}
	port->im_port = im_port;
	im_port->port = port;
	im_port->bfad = bfad;

	INIT_WORK(&im_port->port_delete_work, bfad_im_port_delete_handler);
	INIT_LIST_HEAD(&im_port->itnim_mapped_list);
	INIT_LIST_HEAD(&im_port->binding_list);

ext:
	return rc;
}

void
bfad_im_port_delete(struct bfad_s *bfad, struct bfad_port_s *port)
{
	struct bfad_im_port_s *im_port = port->im_port;

	queue_work(bfad->im->drv_workq,
				&im_port->port_delete_work);
}

void
bfad_im_port_clean(struct bfad_im_port_s *im_port)
{
	struct bfad_fcp_binding *bp, *bp_new;
	unsigned long flags;
	struct bfad_s *bfad =  im_port->bfad;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	list_for_each_entry_safe(bp, bp_new, &im_port->binding_list,
					list_entry) {
		list_del(&bp->list_entry);
		kfree(bp);
	}

	/* the itnim_mapped_list must be empty at this time */
	bfa_assert(list_empty(&im_port->itnim_mapped_list));

	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

void
bfad_im_port_online(struct bfad_s *bfad, struct bfad_port_s *port)
{
}

void
bfad_im_port_offline(struct bfad_s *bfad, struct bfad_port_s *port)
{
}

bfa_status_t
bfad_im_probe(struct bfad_s *bfad)
{
	struct bfad_im_s      *im;
	bfa_status_t    rc = BFA_STATUS_OK;

	im = kzalloc(sizeof(struct bfad_im_s), GFP_KERNEL);
	if (im == NULL) {
		rc = BFA_STATUS_ENOMEM;
		goto ext;
	}

	bfad->im = im;
	im->bfad = bfad;

	if (bfad_os_thread_workq(bfad) != BFA_STATUS_OK) {
		kfree(im);
		rc = BFA_STATUS_FAILED;
	}

ext:
	return rc;
}

void
bfad_im_probe_undo(struct bfad_s *bfad)
{
	if (bfad->im) {
		bfad_os_destroy_workq(bfad->im);
		kfree(bfad->im);
		bfad->im = NULL;
	}
}




int
bfad_os_scsi_add_host(struct Scsi_Host *shost, struct bfad_im_port_s *im_port,
			struct bfad_s *bfad)
{
    struct device *dev;

    if (im_port->port->pvb_type == BFAD_PORT_PHYS_BASE)
		dev = &bfad->pcidev->dev;
    else
		dev = &bfad->pport.im_port->shost->shost_gendev;

    return scsi_add_host(shost, dev);
}

struct Scsi_Host *
bfad_os_scsi_host_alloc(struct bfad_im_port_s *im_port, struct bfad_s *bfad)
{
	struct scsi_host_template *sht;

	if (im_port->port->pvb_type == BFAD_PORT_PHYS_BASE)
		sht = &bfad_im_scsi_host_template;
	else
		sht = &bfad_im_vport_template;

	sht->sg_tablesize = bfad->cfg_data.io_max_sge;

	return scsi_host_alloc(sht, sizeof(unsigned long));
}

void
bfad_os_scsi_host_free(struct bfad_s *bfad, struct bfad_im_port_s *im_port)
{
	flush_workqueue(bfad->im->drv_workq);
	bfad_im_scsi_host_free(im_port->bfad, im_port);
	bfad_im_port_clean(im_port);
	kfree(im_port);
}

void
bfad_os_destroy_workq(struct bfad_im_s *im)
{
	if (im && im->drv_workq) {
		destroy_workqueue(im->drv_workq);
		im->drv_workq = NULL;
	}
}

bfa_status_t
bfad_os_thread_workq(struct bfad_s *bfad)
{
	struct bfad_im_s      *im = bfad->im;

	bfa_trc(bfad, 0);
	snprintf(im->drv_workq_name, BFAD_KOBJ_NAME_LEN, "bfad_wq_%d",
		 bfad->inst_no);
	im->drv_workq = create_singlethread_workqueue(im->drv_workq_name);
	if (!im->drv_workq)
		return BFA_STATUS_FAILED;

	return BFA_STATUS_OK;
}

/**
 * Scsi_Host template entry.
 *
 * Description:
 * OS entry point to adjust the queue_depths on a per-device basis.
 * Called once per device during the bus scan.
 * Return non-zero if fails.
 */
static int
bfad_im_slave_configure(struct scsi_device *sdev)
{
	if (sdev->tagged_supported)
		scsi_activate_tcq(sdev, bfa_lun_queue_depth);
	else
		scsi_deactivate_tcq(sdev, bfa_lun_queue_depth);

	return 0;
}

struct scsi_host_template bfad_im_scsi_host_template = {
	.module = THIS_MODULE,
	.name = BFAD_DRIVER_NAME,
	.info = bfad_im_info,
	.queuecommand = bfad_im_queuecommand,
	.eh_abort_handler = bfad_im_abort_handler,
	.eh_device_reset_handler = bfad_im_reset_lun_handler,
	.eh_bus_reset_handler = bfad_im_reset_bus_handler,

	.slave_alloc = bfad_im_slave_alloc,
	.slave_configure = bfad_im_slave_configure,
	.slave_destroy = bfad_im_slave_destroy,

	.this_id = -1,
	.sg_tablesize = BFAD_IO_MAX_SGE,
	.cmd_per_lun = 3,
	.use_clustering = ENABLE_CLUSTERING,
	.shost_attrs = bfad_im_host_attrs,
	.max_sectors = 0xFFFF,
};

struct scsi_host_template bfad_im_vport_template = {
	.module = THIS_MODULE,
	.name = BFAD_DRIVER_NAME,
	.info = bfad_im_info,
	.queuecommand = bfad_im_queuecommand,
	.eh_abort_handler = bfad_im_abort_handler,
	.eh_device_reset_handler = bfad_im_reset_lun_handler,
	.eh_bus_reset_handler = bfad_im_reset_bus_handler,

	.slave_alloc = bfad_im_slave_alloc,
	.slave_configure = bfad_im_slave_configure,
	.slave_destroy = bfad_im_slave_destroy,

	.this_id = -1,
	.sg_tablesize = BFAD_IO_MAX_SGE,
	.cmd_per_lun = 3,
	.use_clustering = ENABLE_CLUSTERING,
	.shost_attrs = bfad_im_vport_attrs,
	.max_sectors = 0xFFFF,
};

void
bfad_im_probe_post(struct bfad_im_s *im)
{
	flush_workqueue(im->drv_workq);
}

bfa_status_t
bfad_im_module_init(void)
{
	bfad_im_scsi_transport_template =
		fc_attach_transport(&bfad_im_fc_function_template);
	if (!bfad_im_scsi_transport_template)
		return BFA_STATUS_ENOMEM;

	return BFA_STATUS_OK;
}

void
bfad_im_module_exit(void)
{
	if (bfad_im_scsi_transport_template)
		fc_release_transport(bfad_im_scsi_transport_template);
}

void
bfad_os_itnim_process(struct bfad_itnim_s *itnim_drv)
{
	struct bfad_im_s      *im = itnim_drv->im;

	if (itnim_drv->queue_work)
		queue_work(im->drv_workq, &itnim_drv->itnim_work);
}

void
bfad_os_ramp_up_qdepth(struct bfad_itnim_s *itnim, struct scsi_device *sdev)
{
	struct scsi_device *tmp_sdev;

	if (((jiffies - itnim->last_ramp_up_time) >
		BFA_QUEUE_FULL_RAMP_UP_TIME * HZ) &&
		((jiffies - itnim->last_queue_full_time) >
		BFA_QUEUE_FULL_RAMP_UP_TIME * HZ)) {
		shost_for_each_device(tmp_sdev, sdev->host) {
			if (bfa_lun_queue_depth > tmp_sdev->queue_depth) {
				if (tmp_sdev->id != sdev->id)
					continue;
				if (tmp_sdev->ordered_tags)
					scsi_adjust_queue_depth(tmp_sdev,
						MSG_ORDERED_TAG,
						tmp_sdev->queue_depth + 1);
				else
					scsi_adjust_queue_depth(tmp_sdev,
						MSG_SIMPLE_TAG,
						tmp_sdev->queue_depth + 1);

				itnim->last_ramp_up_time = jiffies;
			}
		}
	}
}

void
bfad_os_handle_qfull(struct bfad_itnim_s *itnim, struct scsi_device *sdev)
{
	struct scsi_device *tmp_sdev;

	itnim->last_queue_full_time = jiffies;

	shost_for_each_device(tmp_sdev, sdev->host) {
		if (tmp_sdev->id != sdev->id)
			continue;
		scsi_track_queue_full(tmp_sdev, tmp_sdev->queue_depth - 1);
	}
}




struct bfad_itnim_s *
bfad_os_get_itnim(struct bfad_im_port_s *im_port, int id)
{
	struct bfad_itnim_s   *itnim = NULL;

	/* Search the mapped list for this target ID */
	list_for_each_entry(itnim, &im_port->itnim_mapped_list, list_entry) {
		if (id == itnim->scsi_tgt_id)
			return itnim;
	}

	return NULL;
}

/**
 * Scsi_Host template entry slave_alloc
 */
static int
bfad_im_slave_alloc(struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	sdev->hostdata = rport->dd_data;

	return 0;
}

void
bfad_os_fc_host_init(struct bfad_im_port_s *im_port)
{
	struct Scsi_Host *host = im_port->shost;
	struct bfad_s         *bfad = im_port->bfad;
	struct bfad_port_s    *port = im_port->port;
	union attr {
		struct bfa_pport_attr_s pattr;
		struct bfa_ioc_attr_s  ioc_attr;
	} attr;

	fc_host_node_name(host) =
		bfa_os_htonll((bfa_fcs_port_get_nwwn(port->fcs_port)));
	fc_host_port_name(host) =
		bfa_os_htonll((bfa_fcs_port_get_pwwn(port->fcs_port)));

	fc_host_supported_classes(host) = FC_COS_CLASS3;

	memset(fc_host_supported_fc4s(host), 0,
	       sizeof(fc_host_supported_fc4s(host)));
	if (bfad_supported_fc4s & (BFA_PORT_ROLE_FCP_IM | BFA_PORT_ROLE_FCP_TM))
		/* For FCP type 0x08 */
		fc_host_supported_fc4s(host)[2] = 1;
	if (bfad_supported_fc4s & BFA_PORT_ROLE_FCP_IPFC)
		/* For LLC/SNAP type 0x05 */
		fc_host_supported_fc4s(host)[3] = 0x20;
	/* For fibre channel services type 0x20 */
	fc_host_supported_fc4s(host)[7] = 1;

	memset(&attr.ioc_attr, 0, sizeof(attr.ioc_attr));
	bfa_get_attr(&bfad->bfa, &attr.ioc_attr);
	sprintf(fc_host_symbolic_name(host), "Brocade %s FV%s DV%s",
		attr.ioc_attr.adapter_attr.model,
		attr.ioc_attr.adapter_attr.fw_ver, BFAD_DRIVER_VERSION);

	fc_host_supported_speeds(host) = 0;
	fc_host_supported_speeds(host) |=
		FC_PORTSPEED_8GBIT | FC_PORTSPEED_4GBIT | FC_PORTSPEED_2GBIT |
		FC_PORTSPEED_1GBIT;

	memset(&attr.pattr, 0, sizeof(attr.pattr));
	bfa_pport_get_attr(&bfad->bfa, &attr.pattr);
	fc_host_maxframe_size(host) = attr.pattr.pport_cfg.maxfrsize;
}

static void
bfad_im_fc_rport_add(struct bfad_im_port_s *im_port, struct bfad_itnim_s *itnim)
{
	struct fc_rport_identifiers rport_ids;
	struct fc_rport *fc_rport;
	struct bfad_itnim_data_s *itnim_data;

	rport_ids.node_name =
		bfa_os_htonll(bfa_fcs_itnim_get_nwwn(&itnim->fcs_itnim));
	rport_ids.port_name =
		bfa_os_htonll(bfa_fcs_itnim_get_pwwn(&itnim->fcs_itnim));
	rport_ids.port_id =
		bfa_os_hton3b(bfa_fcs_itnim_get_fcid(&itnim->fcs_itnim));
	rport_ids.roles = FC_RPORT_ROLE_UNKNOWN;

	itnim->fc_rport = fc_rport =
		fc_remote_port_add(im_port->shost, 0, &rport_ids);

	if (!fc_rport)
		return;

	fc_rport->maxframe_size =
		bfa_fcs_itnim_get_maxfrsize(&itnim->fcs_itnim);
	fc_rport->supported_classes = bfa_fcs_itnim_get_cos(&itnim->fcs_itnim);

	itnim_data = fc_rport->dd_data;
	itnim_data->itnim = itnim;

	rport_ids.roles |= FC_RPORT_ROLE_FCP_TARGET;

	if (rport_ids.roles != FC_RPORT_ROLE_UNKNOWN)
		fc_remote_port_rolechg(fc_rport, rport_ids.roles);

	if ((fc_rport->scsi_target_id != -1)
	    && (fc_rport->scsi_target_id < MAX_FCP_TARGET))
		itnim->scsi_tgt_id = fc_rport->scsi_target_id;

	return;
}

/**
 * Work queue handler using FC transport service
* Context: kernel
 */
static void
bfad_im_itnim_work_handler(struct work_struct *work)
{
	struct bfad_itnim_s   *itnim = container_of(work, struct bfad_itnim_s,
							itnim_work);
	struct bfad_im_s      *im = itnim->im;
	struct bfad_s         *bfad = im->bfad;
	struct bfad_im_port_s *im_port;
	unsigned long   flags;
	struct fc_rport *fc_rport;
	wwn_t wwpn;
	u32 fcid;
	char wwpn_str[32], fcid_str[16];

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	im_port = itnim->im_port;
	bfa_trc(bfad, itnim->state);
	switch (itnim->state) {
	case ITNIM_STATE_ONLINE:
		if (!itnim->fc_rport) {
			spin_unlock_irqrestore(&bfad->bfad_lock, flags);
			bfad_im_fc_rport_add(im_port, itnim);
			spin_lock_irqsave(&bfad->bfad_lock, flags);
			wwpn = bfa_fcs_itnim_get_pwwn(&itnim->fcs_itnim);
			fcid = bfa_fcs_itnim_get_fcid(&itnim->fcs_itnim);
			wwn2str(wwpn_str, wwpn);
			fcid2str(fcid_str, fcid);
			list_add_tail(&itnim->list_entry,
				&im_port->itnim_mapped_list);
			bfa_log(bfad->logmod, BFA_LOG_LINUX_ITNIM_ONLINE,
				im_port->shost->host_no,
				itnim->scsi_tgt_id,
				fcid_str, wwpn_str);
		} else {
			printk(KERN_WARNING
				"%s: itnim %llx is already in online state\n",
				__func__,
				bfa_fcs_itnim_get_pwwn(&itnim->fcs_itnim));
		}

		break;
	case ITNIM_STATE_OFFLINE_PENDING:
		itnim->state = ITNIM_STATE_OFFLINE;
		if (itnim->fc_rport) {
			fc_rport = itnim->fc_rport;
			((struct bfad_itnim_data_s *)
				fc_rport->dd_data)->itnim = NULL;
			itnim->fc_rport = NULL;
			if (!(im_port->port->flags & BFAD_PORT_DELETE)) {
				spin_unlock_irqrestore(&bfad->bfad_lock, flags);
				fc_rport->dev_loss_tmo =
					bfa_fcpim_path_tov_get(&bfad->bfa) + 1;
				fc_remote_port_delete(fc_rport);
				spin_lock_irqsave(&bfad->bfad_lock, flags);
			}
			wwpn = bfa_fcs_itnim_get_pwwn(&itnim->fcs_itnim);
			fcid = bfa_fcs_itnim_get_fcid(&itnim->fcs_itnim);
			wwn2str(wwpn_str, wwpn);
			fcid2str(fcid_str, fcid);
			list_del(&itnim->list_entry);
			bfa_log(bfad->logmod, BFA_LOG_LINUX_ITNIM_OFFLINE,
				im_port->shost->host_no,
				itnim->scsi_tgt_id,
				fcid_str, wwpn_str);
		}
		break;
	case ITNIM_STATE_FREE:
		if (itnim->fc_rport) {
			fc_rport = itnim->fc_rport;
			((struct bfad_itnim_data_s *)
				fc_rport->dd_data)->itnim = NULL;
			itnim->fc_rport = NULL;
			if (!(im_port->port->flags & BFAD_PORT_DELETE)) {
				spin_unlock_irqrestore(&bfad->bfad_lock, flags);
				fc_rport->dev_loss_tmo =
					bfa_fcpim_path_tov_get(&bfad->bfa) + 1;
				fc_remote_port_delete(fc_rport);
				spin_lock_irqsave(&bfad->bfad_lock, flags);
			}
			list_del(&itnim->list_entry);
		}

		kfree(itnim);
		break;
	default:
		bfa_assert(0);
		break;
	}

	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

/**
 * Scsi_Host template entry, queue a SCSI command to the BFAD.
 */
static int
bfad_im_queuecommand(struct scsi_cmnd *cmnd, void (*done) (struct scsi_cmnd *))
{
	struct bfad_im_port_s *im_port =
		(struct bfad_im_port_s *) cmnd->device->host->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfad_itnim_data_s *itnim_data = cmnd->device->hostdata;
	struct bfad_itnim_s   *itnim;
	struct bfa_ioim_s *hal_io;
	unsigned long   flags;
	int             rc;
	s16        sg_cnt = 0;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));

	rc = fc_remote_port_chkready(rport);
	if (rc) {
		cmnd->result = rc;
		done(cmnd);
		return 0;
	}

	sg_cnt = scsi_dma_map(cmnd);

	if (sg_cnt < 0)
		return SCSI_MLQUEUE_HOST_BUSY;

	cmnd->scsi_done = done;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	if (!(bfad->bfad_flags & BFAD_HAL_START_DONE)) {
		printk(KERN_WARNING
			"bfad%d, queuecommand %p %x failed, BFA stopped\n",
		       bfad->inst_no, cmnd, cmnd->cmnd[0]);
		cmnd->result = ScsiResult(DID_NO_CONNECT, 0);
		goto out_fail_cmd;
	}

	itnim = itnim_data->itnim;
	if (!itnim) {
		cmnd->result = ScsiResult(DID_IMM_RETRY, 0);
		goto out_fail_cmd;
	}

	hal_io = bfa_ioim_alloc(&bfad->bfa, (struct bfad_ioim_s *) cmnd,
				    itnim->bfa_itnim, sg_cnt);
	if (!hal_io) {
		printk(KERN_WARNING "hal_io failure\n");
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
		scsi_dma_unmap(cmnd);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	cmnd->host_scribble = (char *)hal_io;
	bfa_trc_fp(bfad, hal_io->iotag);
	bfa_ioim_start(hal_io);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return 0;

out_fail_cmd:
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	scsi_dma_unmap(cmnd);
	if (done)
		done(cmnd);

	return 0;
}

void
bfad_os_rport_online_wait(struct bfad_s *bfad)
{
	int i;
	int rport_delay = 10;

	for (i = 0; !(bfad->bfad_flags & BFAD_PORT_ONLINE)
		 && i < bfa_linkup_delay; i++)
		schedule_timeout_uninterruptible(HZ);

	if (bfad->bfad_flags & BFAD_PORT_ONLINE) {
		rport_delay = rport_delay < bfa_linkup_delay ?
				 rport_delay : bfa_linkup_delay;
		for (i = 0; !(bfad->bfad_flags & BFAD_RPORT_ONLINE)
			 && i < rport_delay; i++)
			schedule_timeout_uninterruptible(HZ);

		if (rport_delay > 0 && (bfad->bfad_flags & BFAD_RPORT_ONLINE))
			schedule_timeout_uninterruptible(rport_delay * HZ);
	}
}

int
bfad_os_get_linkup_delay(struct bfad_s *bfad)
{

	u8         nwwns = 0;
	wwn_t           *wwns;
	int             ldelay;

	/*
	 * Querying for the boot target port wwns
	 * -- read from boot information in flash.
	 * If nwwns > 0 => boot over SAN and set bfa_linkup_delay = 30
	 * else => local boot machine set bfa_linkup_delay = 10
	 */

	bfa_iocfc_get_bootwwns(&bfad->bfa, &nwwns, &wwns);

	if (nwwns > 0) {
		/* If boot over SAN; linkup_delay = 30sec */
		ldelay = 30;
	} else {
		/* If local boot; linkup_delay = 10sec */
		ldelay = 0;
	}

	return ldelay;
}


