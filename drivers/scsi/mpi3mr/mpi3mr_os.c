// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2022 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#include "mpi3mr.h"

/* global driver scop variables */
LIST_HEAD(mrioc_list);
DEFINE_SPINLOCK(mrioc_list_lock);
static int mrioc_ids;
static int warn_non_secure_ctlr;
atomic64_t event_counter;

MODULE_AUTHOR(MPI3MR_DRIVER_AUTHOR);
MODULE_DESCRIPTION(MPI3MR_DRIVER_DESC);
MODULE_LICENSE(MPI3MR_DRIVER_LICENSE);
MODULE_VERSION(MPI3MR_DRIVER_VERSION);

/* Module parameters*/
int prot_mask = -1;
module_param(prot_mask, int, 0);
MODULE_PARM_DESC(prot_mask, "Host protection capabilities mask, def=0x07");

static int prot_guard_mask = 3;
module_param(prot_guard_mask, int, 0);
MODULE_PARM_DESC(prot_guard_mask, " Host protection guard mask, def=3");
static int logging_level;
module_param(logging_level, int, 0);
MODULE_PARM_DESC(logging_level,
	" bits for enabling additional logging info (default=0)");

/* Forward declarations*/
static void mpi3mr_send_event_ack(struct mpi3mr_ioc *mrioc, u8 event,
	struct mpi3mr_drv_cmd *cmdparam, u32 event_ctx);

#define MPI3MR_DRIVER_EVENT_TG_QD_REDUCTION	(0xFFFF)

#define MPI3_EVENT_WAIT_FOR_DEVICES_TO_REFRESH	(0xFFFE)

/**
 * mpi3mr_host_tag_for_scmd - Get host tag for a scmd
 * @mrioc: Adapter instance reference
 * @scmd: SCSI command reference
 *
 * Calculate the host tag based on block tag for a given scmd.
 *
 * Return: Valid host tag or MPI3MR_HOSTTAG_INVALID.
 */
static u16 mpi3mr_host_tag_for_scmd(struct mpi3mr_ioc *mrioc,
	struct scsi_cmnd *scmd)
{
	struct scmd_priv *priv = NULL;
	u32 unique_tag;
	u16 host_tag, hw_queue;

	unique_tag = blk_mq_unique_tag(scsi_cmd_to_rq(scmd));

	hw_queue = blk_mq_unique_tag_to_hwq(unique_tag);
	if (hw_queue >= mrioc->num_op_reply_q)
		return MPI3MR_HOSTTAG_INVALID;
	host_tag = blk_mq_unique_tag_to_tag(unique_tag);

	if (WARN_ON(host_tag >= mrioc->max_host_ios))
		return MPI3MR_HOSTTAG_INVALID;

	priv = scsi_cmd_priv(scmd);
	/*host_tag 0 is invalid hence incrementing by 1*/
	priv->host_tag = host_tag + 1;
	priv->scmd = scmd;
	priv->in_lld_scope = 1;
	priv->req_q_idx = hw_queue;
	priv->meta_chain_idx = -1;
	priv->chain_idx = -1;
	priv->meta_sg_valid = 0;
	return priv->host_tag;
}

/**
 * mpi3mr_scmd_from_host_tag - Get SCSI command from host tag
 * @mrioc: Adapter instance reference
 * @host_tag: Host tag
 * @qidx: Operational queue index
 *
 * Identify the block tag from the host tag and queue index and
 * retrieve associated scsi command using scsi_host_find_tag().
 *
 * Return: SCSI command reference or NULL.
 */
static struct scsi_cmnd *mpi3mr_scmd_from_host_tag(
	struct mpi3mr_ioc *mrioc, u16 host_tag, u16 qidx)
{
	struct scsi_cmnd *scmd = NULL;
	struct scmd_priv *priv = NULL;
	u32 unique_tag = host_tag - 1;

	if (WARN_ON(host_tag > mrioc->max_host_ios))
		goto out;

	unique_tag |= (qidx << BLK_MQ_UNIQUE_TAG_BITS);

	scmd = scsi_host_find_tag(mrioc->shost, unique_tag);
	if (scmd) {
		priv = scsi_cmd_priv(scmd);
		if (!priv->in_lld_scope)
			scmd = NULL;
	}
out:
	return scmd;
}

/**
 * mpi3mr_clear_scmd_priv - Cleanup SCSI command private date
 * @mrioc: Adapter instance reference
 * @scmd: SCSI command reference
 *
 * Invalidate the SCSI command private data to mark the command
 * is not in LLD scope anymore.
 *
 * Return: Nothing.
 */
static void mpi3mr_clear_scmd_priv(struct mpi3mr_ioc *mrioc,
	struct scsi_cmnd *scmd)
{
	struct scmd_priv *priv = NULL;

	priv = scsi_cmd_priv(scmd);

	if (WARN_ON(priv->in_lld_scope == 0))
		return;
	priv->host_tag = MPI3MR_HOSTTAG_INVALID;
	priv->req_q_idx = 0xFFFF;
	priv->scmd = NULL;
	priv->in_lld_scope = 0;
	priv->meta_sg_valid = 0;
	if (priv->chain_idx >= 0) {
		clear_bit(priv->chain_idx, mrioc->chain_bitmap);
		priv->chain_idx = -1;
	}
	if (priv->meta_chain_idx >= 0) {
		clear_bit(priv->meta_chain_idx, mrioc->chain_bitmap);
		priv->meta_chain_idx = -1;
	}
}

static void mpi3mr_dev_rmhs_send_tm(struct mpi3mr_ioc *mrioc, u16 handle,
	struct mpi3mr_drv_cmd *cmdparam, u8 iou_rc);
static void mpi3mr_fwevt_worker(struct work_struct *work);

/**
 * mpi3mr_fwevt_free - firmware event memory dealloctor
 * @r: k reference pointer of the firmware event
 *
 * Free firmware event memory when no reference.
 */
static void mpi3mr_fwevt_free(struct kref *r)
{
	kfree(container_of(r, struct mpi3mr_fwevt, ref_count));
}

/**
 * mpi3mr_fwevt_get - k reference incrementor
 * @fwevt: Firmware event reference
 *
 * Increment firmware event reference count.
 */
static void mpi3mr_fwevt_get(struct mpi3mr_fwevt *fwevt)
{
	kref_get(&fwevt->ref_count);
}

/**
 * mpi3mr_fwevt_put - k reference decrementor
 * @fwevt: Firmware event reference
 *
 * decrement firmware event reference count.
 */
static void mpi3mr_fwevt_put(struct mpi3mr_fwevt *fwevt)
{
	kref_put(&fwevt->ref_count, mpi3mr_fwevt_free);
}

/**
 * mpi3mr_alloc_fwevt - Allocate firmware event
 * @len: length of firmware event data to allocate
 *
 * Allocate firmware event with required length and initialize
 * the reference counter.
 *
 * Return: firmware event reference.
 */
static struct mpi3mr_fwevt *mpi3mr_alloc_fwevt(int len)
{
	struct mpi3mr_fwevt *fwevt;

	fwevt = kzalloc(sizeof(*fwevt) + len, GFP_ATOMIC);
	if (!fwevt)
		return NULL;

	kref_init(&fwevt->ref_count);
	return fwevt;
}

/**
 * mpi3mr_fwevt_add_to_list - Add firmware event to the list
 * @mrioc: Adapter instance reference
 * @fwevt: Firmware event reference
 *
 * Add the given firmware event to the firmware event list.
 *
 * Return: Nothing.
 */
static void mpi3mr_fwevt_add_to_list(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_fwevt *fwevt)
{
	unsigned long flags;

	if (!mrioc->fwevt_worker_thread)
		return;

	spin_lock_irqsave(&mrioc->fwevt_lock, flags);
	/* get fwevt reference count while adding it to fwevt_list */
	mpi3mr_fwevt_get(fwevt);
	INIT_LIST_HEAD(&fwevt->list);
	list_add_tail(&fwevt->list, &mrioc->fwevt_list);
	INIT_WORK(&fwevt->work, mpi3mr_fwevt_worker);
	/* get fwevt reference count while enqueueing it to worker queue */
	mpi3mr_fwevt_get(fwevt);
	queue_work(mrioc->fwevt_worker_thread, &fwevt->work);
	spin_unlock_irqrestore(&mrioc->fwevt_lock, flags);
}

/**
 * mpi3mr_fwevt_del_from_list - Delete firmware event from list
 * @mrioc: Adapter instance reference
 * @fwevt: Firmware event reference
 *
 * Delete the given firmware event from the firmware event list.
 *
 * Return: Nothing.
 */
static void mpi3mr_fwevt_del_from_list(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_fwevt *fwevt)
{
	unsigned long flags;

	spin_lock_irqsave(&mrioc->fwevt_lock, flags);
	if (!list_empty(&fwevt->list)) {
		list_del_init(&fwevt->list);
		/*
		 * Put fwevt reference count after
		 * removing it from fwevt_list
		 */
		mpi3mr_fwevt_put(fwevt);
	}
	spin_unlock_irqrestore(&mrioc->fwevt_lock, flags);
}

/**
 * mpi3mr_dequeue_fwevt - Dequeue firmware event from the list
 * @mrioc: Adapter instance reference
 *
 * Dequeue a firmware event from the firmware event list.
 *
 * Return: firmware event.
 */
static struct mpi3mr_fwevt *mpi3mr_dequeue_fwevt(
	struct mpi3mr_ioc *mrioc)
{
	unsigned long flags;
	struct mpi3mr_fwevt *fwevt = NULL;

	spin_lock_irqsave(&mrioc->fwevt_lock, flags);
	if (!list_empty(&mrioc->fwevt_list)) {
		fwevt = list_first_entry(&mrioc->fwevt_list,
		    struct mpi3mr_fwevt, list);
		list_del_init(&fwevt->list);
		/*
		 * Put fwevt reference count after
		 * removing it from fwevt_list
		 */
		mpi3mr_fwevt_put(fwevt);
	}
	spin_unlock_irqrestore(&mrioc->fwevt_lock, flags);

	return fwevt;
}

/**
 * mpi3mr_cancel_work - cancel firmware event
 * @fwevt: fwevt object which needs to be canceled
 *
 * Return: Nothing.
 */
static void mpi3mr_cancel_work(struct mpi3mr_fwevt *fwevt)
{
	/*
	 * Wait on the fwevt to complete. If this returns 1, then
	 * the event was never executed.
	 *
	 * If it did execute, we wait for it to finish, and the put will
	 * happen from mpi3mr_process_fwevt()
	 */
	if (cancel_work_sync(&fwevt->work)) {
		/*
		 * Put fwevt reference count after
		 * dequeuing it from worker queue
		 */
		mpi3mr_fwevt_put(fwevt);
		/*
		 * Put fwevt reference count to neutralize
		 * kref_init increment
		 */
		mpi3mr_fwevt_put(fwevt);
	}
}

/**
 * mpi3mr_cleanup_fwevt_list - Cleanup firmware event list
 * @mrioc: Adapter instance reference
 *
 * Flush all pending firmware events from the firmware event
 * list.
 *
 * Return: Nothing.
 */
void mpi3mr_cleanup_fwevt_list(struct mpi3mr_ioc *mrioc)
{
	struct mpi3mr_fwevt *fwevt = NULL;

	if ((list_empty(&mrioc->fwevt_list) && !mrioc->current_event) ||
	    !mrioc->fwevt_worker_thread)
		return;

	while ((fwevt = mpi3mr_dequeue_fwevt(mrioc)))
		mpi3mr_cancel_work(fwevt);

	if (mrioc->current_event) {
		fwevt = mrioc->current_event;
		/*
		 * Don't call cancel_work_sync() API for the
		 * fwevt work if the controller reset is
		 * get called as part of processing the
		 * same fwevt work (or) when worker thread is
		 * waiting for device add/remove APIs to complete.
		 * Otherwise we will see deadlock.
		 */
		if (current_work() == &fwevt->work || fwevt->pending_at_sml) {
			fwevt->discard = 1;
			return;
		}

		mpi3mr_cancel_work(fwevt);
	}
}

/**
 * mpi3mr_queue_qd_reduction_event - Queue TG QD reduction event
 * @mrioc: Adapter instance reference
 * @tg: Throttle group information pointer
 *
 * Accessor to queue on synthetically generated driver event to
 * the event worker thread, the driver event will be used to
 * reduce the QD of all VDs in the TG from the worker thread.
 *
 * Return: None.
 */
static void mpi3mr_queue_qd_reduction_event(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_throttle_group_info *tg)
{
	struct mpi3mr_fwevt *fwevt;
	u16 sz = sizeof(struct mpi3mr_throttle_group_info *);

	/*
	 * If the QD reduction event is already queued due to throttle and if
	 * the QD is not restored through device info change event
	 * then dont queue further reduction events
	 */
	if (tg->fw_qd != tg->modified_qd)
		return;

	fwevt = mpi3mr_alloc_fwevt(sz);
	if (!fwevt) {
		ioc_warn(mrioc, "failed to queue TG QD reduction event\n");
		return;
	}
	*(struct mpi3mr_throttle_group_info **)fwevt->event_data = tg;
	fwevt->mrioc = mrioc;
	fwevt->event_id = MPI3MR_DRIVER_EVENT_TG_QD_REDUCTION;
	fwevt->send_ack = 0;
	fwevt->process_evt = 1;
	fwevt->evt_ctx = 0;
	fwevt->event_data_size = sz;
	tg->modified_qd = max_t(u16, (tg->fw_qd * tg->qd_reduction) / 10, 8);

	dprint_event_bh(mrioc, "qd reduction event queued for tg_id(%d)\n",
	    tg->id);
	mpi3mr_fwevt_add_to_list(mrioc, fwevt);
}

/**
 * mpi3mr_invalidate_devhandles -Invalidate device handles
 * @mrioc: Adapter instance reference
 *
 * Invalidate the device handles in the target device structures
 * . Called post reset prior to reinitializing the controller.
 *
 * Return: Nothing.
 */
void mpi3mr_invalidate_devhandles(struct mpi3mr_ioc *mrioc)
{
	struct mpi3mr_tgt_dev *tgtdev;
	struct mpi3mr_stgt_priv_data *tgt_priv;

	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list) {
		tgtdev->dev_handle = MPI3MR_INVALID_DEV_HANDLE;
		if (tgtdev->starget && tgtdev->starget->hostdata) {
			tgt_priv = tgtdev->starget->hostdata;
			tgt_priv->dev_handle = MPI3MR_INVALID_DEV_HANDLE;
			tgt_priv->io_throttle_enabled = 0;
			tgt_priv->io_divert = 0;
			tgt_priv->throttle_group = NULL;
			if (tgtdev->host_exposed)
				atomic_set(&tgt_priv->block_io, 1);
		}
	}
}

/**
 * mpi3mr_print_scmd - print individual SCSI command
 * @rq: Block request
 * @data: Adapter instance reference
 *
 * Print the SCSI command details if it is in LLD scope.
 *
 * Return: true always.
 */
static bool mpi3mr_print_scmd(struct request *rq, void *data)
{
	struct mpi3mr_ioc *mrioc = (struct mpi3mr_ioc *)data;
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(rq);
	struct scmd_priv *priv = NULL;

	if (scmd) {
		priv = scsi_cmd_priv(scmd);
		if (!priv->in_lld_scope)
			goto out;

		ioc_info(mrioc, "%s :Host Tag = %d, qid = %d\n",
		    __func__, priv->host_tag, priv->req_q_idx + 1);
		scsi_print_command(scmd);
	}

out:
	return(true);
}

/**
 * mpi3mr_flush_scmd - Flush individual SCSI command
 * @rq: Block request
 * @data: Adapter instance reference
 *
 * Return the SCSI command to the upper layers if it is in LLD
 * scope.
 *
 * Return: true always.
 */

static bool mpi3mr_flush_scmd(struct request *rq, void *data)
{
	struct mpi3mr_ioc *mrioc = (struct mpi3mr_ioc *)data;
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(rq);
	struct scmd_priv *priv = NULL;

	if (scmd) {
		priv = scsi_cmd_priv(scmd);
		if (!priv->in_lld_scope)
			goto out;

		if (priv->meta_sg_valid)
			dma_unmap_sg(&mrioc->pdev->dev, scsi_prot_sglist(scmd),
			    scsi_prot_sg_count(scmd), scmd->sc_data_direction);
		mpi3mr_clear_scmd_priv(mrioc, scmd);
		scsi_dma_unmap(scmd);
		scmd->result = DID_RESET << 16;
		scsi_print_command(scmd);
		scsi_done(scmd);
		mrioc->flush_io_count++;
	}

out:
	return(true);
}

/**
 * mpi3mr_count_dev_pending - Count commands pending for a lun
 * @rq: Block request
 * @data: SCSI device reference
 *
 * This is an iterator function called for each SCSI command in
 * a host and if the command is pending in the LLD for the
 * specific device(lun) then device specific pending I/O counter
 * is updated in the device structure.
 *
 * Return: true always.
 */

static bool mpi3mr_count_dev_pending(struct request *rq, void *data)
{
	struct scsi_device *sdev = (struct scsi_device *)data;
	struct mpi3mr_sdev_priv_data *sdev_priv_data = sdev->hostdata;
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(rq);
	struct scmd_priv *priv;

	if (scmd) {
		priv = scsi_cmd_priv(scmd);
		if (!priv->in_lld_scope)
			goto out;
		if (scmd->device == sdev)
			sdev_priv_data->pend_count++;
	}

out:
	return true;
}

/**
 * mpi3mr_count_tgt_pending - Count commands pending for target
 * @rq: Block request
 * @data: SCSI target reference
 *
 * This is an iterator function called for each SCSI command in
 * a host and if the command is pending in the LLD for the
 * specific target then target specific pending I/O counter is
 * updated in the target structure.
 *
 * Return: true always.
 */

static bool mpi3mr_count_tgt_pending(struct request *rq, void *data)
{
	struct scsi_target *starget = (struct scsi_target *)data;
	struct mpi3mr_stgt_priv_data *stgt_priv_data = starget->hostdata;
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(rq);
	struct scmd_priv *priv;

	if (scmd) {
		priv = scsi_cmd_priv(scmd);
		if (!priv->in_lld_scope)
			goto out;
		if (scmd->device && (scsi_target(scmd->device) == starget))
			stgt_priv_data->pend_count++;
	}

out:
	return true;
}

/**
 * mpi3mr_flush_host_io -  Flush host I/Os
 * @mrioc: Adapter instance reference
 *
 * Flush all of the pending I/Os by calling
 * blk_mq_tagset_busy_iter() for each possible tag. This is
 * executed post controller reset
 *
 * Return: Nothing.
 */
void mpi3mr_flush_host_io(struct mpi3mr_ioc *mrioc)
{
	struct Scsi_Host *shost = mrioc->shost;

	mrioc->flush_io_count = 0;
	ioc_info(mrioc, "%s :Flushing Host I/O cmds post reset\n", __func__);
	blk_mq_tagset_busy_iter(&shost->tag_set,
	    mpi3mr_flush_scmd, (void *)mrioc);
	ioc_info(mrioc, "%s :Flushed %d Host I/O cmds\n", __func__,
	    mrioc->flush_io_count);
}

/**
 * mpi3mr_flush_cmds_for_unrecovered_controller - Flush all pending cmds
 * @mrioc: Adapter instance reference
 *
 * This function waits for currently running IO poll threads to
 * exit and then flushes all host I/Os and any internal pending
 * cmds. This is executed after controller is marked as
 * unrecoverable.
 *
 * Return: Nothing.
 */
void mpi3mr_flush_cmds_for_unrecovered_controller(struct mpi3mr_ioc *mrioc)
{
	struct Scsi_Host *shost = mrioc->shost;
	int i;

	if (!mrioc->unrecoverable)
		return;

	if (mrioc->op_reply_qinfo) {
		for (i = 0; i < mrioc->num_queues; i++) {
			while (atomic_read(&mrioc->op_reply_qinfo[i].in_use))
				udelay(500);
			atomic_set(&mrioc->op_reply_qinfo[i].pend_ios, 0);
		}
	}
	mrioc->flush_io_count = 0;
	blk_mq_tagset_busy_iter(&shost->tag_set,
	    mpi3mr_flush_scmd, (void *)mrioc);
	mpi3mr_flush_delayed_cmd_lists(mrioc);
	mpi3mr_flush_drv_cmds(mrioc);
}

/**
 * mpi3mr_alloc_tgtdev - target device allocator
 *
 * Allocate target device instance and initialize the reference
 * count
 *
 * Return: target device instance.
 */
static struct mpi3mr_tgt_dev *mpi3mr_alloc_tgtdev(void)
{
	struct mpi3mr_tgt_dev *tgtdev;

	tgtdev = kzalloc(sizeof(*tgtdev), GFP_ATOMIC);
	if (!tgtdev)
		return NULL;
	kref_init(&tgtdev->ref_count);
	return tgtdev;
}

/**
 * mpi3mr_tgtdev_add_to_list -Add tgtdevice to the list
 * @mrioc: Adapter instance reference
 * @tgtdev: Target device
 *
 * Add the target device to the target device list
 *
 * Return: Nothing.
 */
static void mpi3mr_tgtdev_add_to_list(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev)
{
	unsigned long flags;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	mpi3mr_tgtdev_get(tgtdev);
	INIT_LIST_HEAD(&tgtdev->list);
	list_add_tail(&tgtdev->list, &mrioc->tgtdev_list);
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
}

/**
 * mpi3mr_tgtdev_del_from_list -Delete tgtdevice from the list
 * @mrioc: Adapter instance reference
 * @tgtdev: Target device
 *
 * Remove the target device from the target device list
 *
 * Return: Nothing.
 */
static void mpi3mr_tgtdev_del_from_list(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev)
{
	unsigned long flags;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	if (!list_empty(&tgtdev->list)) {
		list_del_init(&tgtdev->list);
		mpi3mr_tgtdev_put(tgtdev);
	}
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
}

/**
 * __mpi3mr_get_tgtdev_by_handle -Get tgtdev from device handle
 * @mrioc: Adapter instance reference
 * @handle: Device handle
 *
 * Accessor to retrieve target device from the device handle.
 * Non Lock version
 *
 * Return: Target device reference.
 */
static struct mpi3mr_tgt_dev  *__mpi3mr_get_tgtdev_by_handle(
	struct mpi3mr_ioc *mrioc, u16 handle)
{
	struct mpi3mr_tgt_dev *tgtdev;

	assert_spin_locked(&mrioc->tgtdev_lock);
	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list)
		if (tgtdev->dev_handle == handle)
			goto found_tgtdev;
	return NULL;

found_tgtdev:
	mpi3mr_tgtdev_get(tgtdev);
	return tgtdev;
}

/**
 * mpi3mr_get_tgtdev_by_handle -Get tgtdev from device handle
 * @mrioc: Adapter instance reference
 * @handle: Device handle
 *
 * Accessor to retrieve target device from the device handle.
 * Lock version
 *
 * Return: Target device reference.
 */
struct mpi3mr_tgt_dev *mpi3mr_get_tgtdev_by_handle(
	struct mpi3mr_ioc *mrioc, u16 handle)
{
	struct mpi3mr_tgt_dev *tgtdev;
	unsigned long flags;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgtdev = __mpi3mr_get_tgtdev_by_handle(mrioc, handle);
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
	return tgtdev;
}

/**
 * __mpi3mr_get_tgtdev_by_perst_id -Get tgtdev from persist ID
 * @mrioc: Adapter instance reference
 * @persist_id: Persistent ID
 *
 * Accessor to retrieve target device from the Persistent ID.
 * Non Lock version
 *
 * Return: Target device reference.
 */
static struct mpi3mr_tgt_dev  *__mpi3mr_get_tgtdev_by_perst_id(
	struct mpi3mr_ioc *mrioc, u16 persist_id)
{
	struct mpi3mr_tgt_dev *tgtdev;

	assert_spin_locked(&mrioc->tgtdev_lock);
	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list)
		if (tgtdev->perst_id == persist_id)
			goto found_tgtdev;
	return NULL;

found_tgtdev:
	mpi3mr_tgtdev_get(tgtdev);
	return tgtdev;
}

/**
 * mpi3mr_get_tgtdev_by_perst_id -Get tgtdev from persistent ID
 * @mrioc: Adapter instance reference
 * @persist_id: Persistent ID
 *
 * Accessor to retrieve target device from the Persistent ID.
 * Lock version
 *
 * Return: Target device reference.
 */
static struct mpi3mr_tgt_dev *mpi3mr_get_tgtdev_by_perst_id(
	struct mpi3mr_ioc *mrioc, u16 persist_id)
{
	struct mpi3mr_tgt_dev *tgtdev;
	unsigned long flags;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgtdev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, persist_id);
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
	return tgtdev;
}

/**
 * __mpi3mr_get_tgtdev_from_tgtpriv -Get tgtdev from tgt private
 * @mrioc: Adapter instance reference
 * @tgt_priv: Target private data
 *
 * Accessor to return target device from the target private
 * data. Non Lock version
 *
 * Return: Target device reference.
 */
static struct mpi3mr_tgt_dev  *__mpi3mr_get_tgtdev_from_tgtpriv(
	struct mpi3mr_ioc *mrioc, struct mpi3mr_stgt_priv_data *tgt_priv)
{
	struct mpi3mr_tgt_dev *tgtdev;

	assert_spin_locked(&mrioc->tgtdev_lock);
	tgtdev = tgt_priv->tgt_dev;
	if (tgtdev)
		mpi3mr_tgtdev_get(tgtdev);
	return tgtdev;
}

/**
 * mpi3mr_set_io_divert_for_all_vd_in_tg -set divert for TG VDs
 * @mrioc: Adapter instance reference
 * @tg: Throttle group information pointer
 * @divert_value: 1 or 0
 *
 * Accessor to set io_divert flag for each device associated
 * with the given throttle group with the given value.
 *
 * Return: None.
 */
static void mpi3mr_set_io_divert_for_all_vd_in_tg(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_throttle_group_info *tg, u8 divert_value)
{
	unsigned long flags;
	struct mpi3mr_tgt_dev *tgtdev;
	struct mpi3mr_stgt_priv_data *tgt_priv;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list) {
		if (tgtdev->starget && tgtdev->starget->hostdata) {
			tgt_priv = tgtdev->starget->hostdata;
			if (tgt_priv->throttle_group == tg)
				tgt_priv->io_divert = divert_value;
		}
	}
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
}

/**
 * mpi3mr_print_device_event_notice - print notice related to post processing of
 *					device event after controller reset.
 *
 * @mrioc: Adapter instance reference
 * @device_add: true for device add event and false for device removal event
 *
 * Return: None.
 */
void mpi3mr_print_device_event_notice(struct mpi3mr_ioc *mrioc,
	bool device_add)
{
	ioc_notice(mrioc, "Device %s was in progress before the reset and\n",
	    (device_add ? "addition" : "removal"));
	ioc_notice(mrioc, "completed after reset, verify whether the exposed devices\n");
	ioc_notice(mrioc, "are matched with attached devices for correctness\n");
}

/**
 * mpi3mr_remove_tgtdev_from_host - Remove dev from upper layers
 * @mrioc: Adapter instance reference
 * @tgtdev: Target device structure
 *
 * Checks whether the device is exposed to upper layers and if it
 * is then remove the device from upper layers by calling
 * scsi_remove_target().
 *
 * Return: 0 on success, non zero on failure.
 */
void mpi3mr_remove_tgtdev_from_host(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev)
{
	struct mpi3mr_stgt_priv_data *tgt_priv;

	ioc_info(mrioc, "%s :Removing handle(0x%04x), wwid(0x%016llx)\n",
	    __func__, tgtdev->dev_handle, (unsigned long long)tgtdev->wwid);
	if (tgtdev->starget && tgtdev->starget->hostdata) {
		tgt_priv = tgtdev->starget->hostdata;
		atomic_set(&tgt_priv->block_io, 0);
		tgt_priv->dev_handle = MPI3MR_INVALID_DEV_HANDLE;
	}

	if (!mrioc->sas_transport_enabled || (tgtdev->dev_type !=
	    MPI3_DEVICE_DEVFORM_SAS_SATA) || tgtdev->non_stl) {
		if (tgtdev->starget) {
			if (mrioc->current_event)
				mrioc->current_event->pending_at_sml = 1;
			scsi_remove_target(&tgtdev->starget->dev);
			tgtdev->host_exposed = 0;
			if (mrioc->current_event) {
				mrioc->current_event->pending_at_sml = 0;
				if (mrioc->current_event->discard) {
					mpi3mr_print_device_event_notice(mrioc,
					    false);
					return;
				}
			}
		}
	} else
		mpi3mr_remove_tgtdev_from_sas_transport(mrioc, tgtdev);

	ioc_info(mrioc, "%s :Removed handle(0x%04x), wwid(0x%016llx)\n",
	    __func__, tgtdev->dev_handle, (unsigned long long)tgtdev->wwid);
}

/**
 * mpi3mr_report_tgtdev_to_host - Expose device to upper layers
 * @mrioc: Adapter instance reference
 * @perst_id: Persistent ID of the device
 *
 * Checks whether the device can be exposed to upper layers and
 * if it is not then expose the device to upper layers by
 * calling scsi_scan_target().
 *
 * Return: 0 on success, non zero on failure.
 */
static int mpi3mr_report_tgtdev_to_host(struct mpi3mr_ioc *mrioc,
	u16 perst_id)
{
	int retval = 0;
	struct mpi3mr_tgt_dev *tgtdev;

	if (mrioc->reset_in_progress)
		return -1;

	tgtdev = mpi3mr_get_tgtdev_by_perst_id(mrioc, perst_id);
	if (!tgtdev) {
		retval = -1;
		goto out;
	}
	if (tgtdev->is_hidden || tgtdev->host_exposed) {
		retval = -1;
		goto out;
	}
	if (!mrioc->sas_transport_enabled || (tgtdev->dev_type !=
	    MPI3_DEVICE_DEVFORM_SAS_SATA) || tgtdev->non_stl){
		tgtdev->host_exposed = 1;
		if (mrioc->current_event)
			mrioc->current_event->pending_at_sml = 1;
		scsi_scan_target(&mrioc->shost->shost_gendev,
		    mrioc->scsi_device_channel, tgtdev->perst_id,
		    SCAN_WILD_CARD, SCSI_SCAN_INITIAL);
		if (!tgtdev->starget)
			tgtdev->host_exposed = 0;
		if (mrioc->current_event) {
			mrioc->current_event->pending_at_sml = 0;
			if (mrioc->current_event->discard) {
				mpi3mr_print_device_event_notice(mrioc, true);
				goto out;
			}
		}
	} else
		mpi3mr_report_tgtdev_to_sas_transport(mrioc, tgtdev);
out:
	if (tgtdev)
		mpi3mr_tgtdev_put(tgtdev);

	return retval;
}

/**
 * mpi3mr_change_queue_depth- Change QD callback handler
 * @sdev: SCSI device reference
 * @q_depth: Queue depth
 *
 * Validate and limit QD and call scsi_change_queue_depth.
 *
 * Return: return value of scsi_change_queue_depth
 */
static int mpi3mr_change_queue_depth(struct scsi_device *sdev,
	int q_depth)
{
	struct scsi_target *starget = scsi_target(sdev);
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	int retval = 0;

	if (!sdev->tagged_supported)
		q_depth = 1;
	if (q_depth > shost->can_queue)
		q_depth = shost->can_queue;
	else if (!q_depth)
		q_depth = MPI3MR_DEFAULT_SDEV_QD;
	retval = scsi_change_queue_depth(sdev, q_depth);
	sdev->max_queue_depth = sdev->queue_depth;

	return retval;
}

/**
 * mpi3mr_update_sdev - Update SCSI device information
 * @sdev: SCSI device reference
 * @data: target device reference
 *
 * This is an iterator function called for each SCSI device in a
 * target to update the target specific information into each
 * SCSI device.
 *
 * Return: Nothing.
 */
static void
mpi3mr_update_sdev(struct scsi_device *sdev, void *data)
{
	struct mpi3mr_tgt_dev *tgtdev;

	tgtdev = (struct mpi3mr_tgt_dev *)data;
	if (!tgtdev)
		return;

	mpi3mr_change_queue_depth(sdev, tgtdev->q_depth);
	switch (tgtdev->dev_type) {
	case MPI3_DEVICE_DEVFORM_PCIE:
		/*The block layer hw sector size = 512*/
		if ((tgtdev->dev_spec.pcie_inf.dev_info &
		    MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_MASK) ==
		    MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_NVME_DEVICE) {
			blk_queue_max_hw_sectors(sdev->request_queue,
			    tgtdev->dev_spec.pcie_inf.mdts / 512);
			if (tgtdev->dev_spec.pcie_inf.pgsz == 0)
				blk_queue_virt_boundary(sdev->request_queue,
				    ((1 << MPI3MR_DEFAULT_PGSZEXP) - 1));
			else
				blk_queue_virt_boundary(sdev->request_queue,
				    ((1 << tgtdev->dev_spec.pcie_inf.pgsz) - 1));
		}
		break;
	default:
		break;
	}
}

/**
 * mpi3mr_rfresh_tgtdevs - Refresh target device exposure
 * @mrioc: Adapter instance reference
 *
 * This is executed post controller reset to identify any
 * missing devices during reset and remove from the upper layers
 * or expose any newly detected device to the upper layers.
 *
 * Return: Nothing.
 */

void mpi3mr_rfresh_tgtdevs(struct mpi3mr_ioc *mrioc)
{
	struct mpi3mr_tgt_dev *tgtdev, *tgtdev_next;

	list_for_each_entry_safe(tgtdev, tgtdev_next, &mrioc->tgtdev_list,
	    list) {
		if (tgtdev->dev_handle == MPI3MR_INVALID_DEV_HANDLE) {
			dprint_reset(mrioc, "removing target device with perst_id(%d)\n",
			    tgtdev->perst_id);
			if (tgtdev->host_exposed)
				mpi3mr_remove_tgtdev_from_host(mrioc, tgtdev);
			mpi3mr_tgtdev_del_from_list(mrioc, tgtdev);
			mpi3mr_tgtdev_put(tgtdev);
		}
	}

	tgtdev = NULL;
	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list) {
		if ((tgtdev->dev_handle != MPI3MR_INVALID_DEV_HANDLE) &&
		    !tgtdev->is_hidden && !tgtdev->host_exposed)
			mpi3mr_report_tgtdev_to_host(mrioc, tgtdev->perst_id);
	}
}

/**
 * mpi3mr_update_tgtdev - DevStatusChange evt bottomhalf
 * @mrioc: Adapter instance reference
 * @tgtdev: Target device internal structure
 * @dev_pg0: New device page0
 * @is_added: Flag to indicate the device is just added
 *
 * Update the information from the device page0 into the driver
 * cached target device structure.
 *
 * Return: Nothing.
 */
static void mpi3mr_update_tgtdev(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev, struct mpi3_device_page0 *dev_pg0,
	bool is_added)
{
	u16 flags = 0;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data = NULL;
	struct mpi3mr_enclosure_node *enclosure_dev = NULL;
	u8 prot_mask = 0;

	tgtdev->perst_id = le16_to_cpu(dev_pg0->persistent_id);
	tgtdev->dev_handle = le16_to_cpu(dev_pg0->dev_handle);
	tgtdev->dev_type = dev_pg0->device_form;
	tgtdev->io_unit_port = dev_pg0->io_unit_port;
	tgtdev->encl_handle = le16_to_cpu(dev_pg0->enclosure_handle);
	tgtdev->parent_handle = le16_to_cpu(dev_pg0->parent_dev_handle);
	tgtdev->slot = le16_to_cpu(dev_pg0->slot);
	tgtdev->q_depth = le16_to_cpu(dev_pg0->queue_depth);
	tgtdev->wwid = le64_to_cpu(dev_pg0->wwid);
	tgtdev->devpg0_flag = le16_to_cpu(dev_pg0->flags);

	if (tgtdev->encl_handle)
		enclosure_dev = mpi3mr_enclosure_find_by_handle(mrioc,
		    tgtdev->encl_handle);
	if (enclosure_dev)
		tgtdev->enclosure_logical_id = le64_to_cpu(
		    enclosure_dev->pg0.enclosure_logical_id);

	flags = tgtdev->devpg0_flag;

	tgtdev->is_hidden = (flags & MPI3_DEVICE0_FLAGS_HIDDEN);

	if (is_added == true)
		tgtdev->io_throttle_enabled =
		    (flags & MPI3_DEVICE0_FLAGS_IO_THROTTLING_REQUIRED) ? 1 : 0;


	if (tgtdev->starget && tgtdev->starget->hostdata) {
		scsi_tgt_priv_data = (struct mpi3mr_stgt_priv_data *)
		    tgtdev->starget->hostdata;
		scsi_tgt_priv_data->perst_id = tgtdev->perst_id;
		scsi_tgt_priv_data->dev_handle = tgtdev->dev_handle;
		scsi_tgt_priv_data->dev_type = tgtdev->dev_type;
		scsi_tgt_priv_data->io_throttle_enabled =
		    tgtdev->io_throttle_enabled;
		if (is_added == true)
			atomic_set(&scsi_tgt_priv_data->block_io, 0);
	}

	switch (dev_pg0->access_status) {
	case MPI3_DEVICE0_ASTATUS_NO_ERRORS:
	case MPI3_DEVICE0_ASTATUS_PREPARE:
	case MPI3_DEVICE0_ASTATUS_NEEDS_INITIALIZATION:
	case MPI3_DEVICE0_ASTATUS_DEVICE_MISSING_DELAY:
		break;
	default:
		tgtdev->is_hidden = 1;
		break;
	}

	switch (tgtdev->dev_type) {
	case MPI3_DEVICE_DEVFORM_SAS_SATA:
	{
		struct mpi3_device0_sas_sata_format *sasinf =
		    &dev_pg0->device_specific.sas_sata_format;
		u16 dev_info = le16_to_cpu(sasinf->device_info);

		tgtdev->dev_spec.sas_sata_inf.dev_info = dev_info;
		tgtdev->dev_spec.sas_sata_inf.sas_address =
		    le64_to_cpu(sasinf->sas_address);
		tgtdev->dev_spec.sas_sata_inf.phy_id = sasinf->phy_num;
		tgtdev->dev_spec.sas_sata_inf.attached_phy_id =
		    sasinf->attached_phy_identifier;
		if ((dev_info & MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_MASK) !=
		    MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_END_DEVICE)
			tgtdev->is_hidden = 1;
		else if (!(dev_info & (MPI3_SAS_DEVICE_INFO_STP_SATA_TARGET |
		    MPI3_SAS_DEVICE_INFO_SSP_TARGET)))
			tgtdev->is_hidden = 1;

		if (((tgtdev->devpg0_flag &
		    MPI3_DEVICE0_FLAGS_ATT_METHOD_DIR_ATTACHED)
		    && (tgtdev->devpg0_flag &
		    MPI3_DEVICE0_FLAGS_ATT_METHOD_VIRTUAL)) ||
		    (tgtdev->parent_handle == 0xFFFF))
			tgtdev->non_stl = 1;
		if (tgtdev->dev_spec.sas_sata_inf.hba_port)
			tgtdev->dev_spec.sas_sata_inf.hba_port->port_id =
			    dev_pg0->io_unit_port;
		break;
	}
	case MPI3_DEVICE_DEVFORM_PCIE:
	{
		struct mpi3_device0_pcie_format *pcieinf =
		    &dev_pg0->device_specific.pcie_format;
		u16 dev_info = le16_to_cpu(pcieinf->device_info);

		tgtdev->dev_spec.pcie_inf.dev_info = dev_info;
		tgtdev->dev_spec.pcie_inf.capb =
		    le32_to_cpu(pcieinf->capabilities);
		tgtdev->dev_spec.pcie_inf.mdts = MPI3MR_DEFAULT_MDTS;
		/* 2^12 = 4096 */
		tgtdev->dev_spec.pcie_inf.pgsz = 12;
		if (dev_pg0->access_status == MPI3_DEVICE0_ASTATUS_NO_ERRORS) {
			tgtdev->dev_spec.pcie_inf.mdts =
			    le32_to_cpu(pcieinf->maximum_data_transfer_size);
			tgtdev->dev_spec.pcie_inf.pgsz = pcieinf->page_size;
			tgtdev->dev_spec.pcie_inf.reset_to =
			    max_t(u8, pcieinf->controller_reset_to,
			     MPI3MR_INTADMCMD_TIMEOUT);
			tgtdev->dev_spec.pcie_inf.abort_to =
			    max_t(u8, pcieinf->nvme_abort_to,
			    MPI3MR_INTADMCMD_TIMEOUT);
		}
		if (tgtdev->dev_spec.pcie_inf.mdts > (1024 * 1024))
			tgtdev->dev_spec.pcie_inf.mdts = (1024 * 1024);
		if (((dev_info & MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_MASK) !=
		    MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_NVME_DEVICE) &&
		    ((dev_info & MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_MASK) !=
		    MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_SCSI_DEVICE))
			tgtdev->is_hidden = 1;
		tgtdev->non_stl = 1;
		if (!mrioc->shost)
			break;
		prot_mask = scsi_host_get_prot(mrioc->shost);
		if (prot_mask & SHOST_DIX_TYPE0_PROTECTION) {
			scsi_host_set_prot(mrioc->shost, prot_mask & 0x77);
			ioc_info(mrioc,
			    "%s : Disabling DIX0 prot capability\n", __func__);
			ioc_info(mrioc,
			    "because HBA does not support DIX0 operation on NVME drives\n");
		}
		break;
	}
	case MPI3_DEVICE_DEVFORM_VD:
	{
		struct mpi3_device0_vd_format *vdinf =
		    &dev_pg0->device_specific.vd_format;
		struct mpi3mr_throttle_group_info *tg = NULL;
		u16 vdinf_io_throttle_group =
		    le16_to_cpu(vdinf->io_throttle_group);

		tgtdev->dev_spec.vd_inf.state = vdinf->vd_state;
		if (vdinf->vd_state == MPI3_DEVICE0_VD_STATE_OFFLINE)
			tgtdev->is_hidden = 1;
		tgtdev->non_stl = 1;
		tgtdev->dev_spec.vd_inf.tg_id = vdinf_io_throttle_group;
		tgtdev->dev_spec.vd_inf.tg_high =
		    le16_to_cpu(vdinf->io_throttle_group_high) * 2048;
		tgtdev->dev_spec.vd_inf.tg_low =
		    le16_to_cpu(vdinf->io_throttle_group_low) * 2048;
		if (vdinf_io_throttle_group < mrioc->num_io_throttle_group) {
			tg = mrioc->throttle_groups + vdinf_io_throttle_group;
			tg->id = vdinf_io_throttle_group;
			tg->high = tgtdev->dev_spec.vd_inf.tg_high;
			tg->low = tgtdev->dev_spec.vd_inf.tg_low;
			tg->qd_reduction =
			    tgtdev->dev_spec.vd_inf.tg_qd_reduction;
			if (is_added == true)
				tg->fw_qd = tgtdev->q_depth;
			tg->modified_qd = tgtdev->q_depth;
		}
		tgtdev->dev_spec.vd_inf.tg = tg;
		if (scsi_tgt_priv_data)
			scsi_tgt_priv_data->throttle_group = tg;
		break;
	}
	default:
		break;
	}
}

/**
 * mpi3mr_devstatuschg_evt_bh - DevStatusChange evt bottomhalf
 * @mrioc: Adapter instance reference
 * @fwevt: Firmware event information.
 *
 * Process Device status Change event and based on device's new
 * information, either expose the device to the upper layers, or
 * remove the device from upper layers.
 *
 * Return: Nothing.
 */
static void mpi3mr_devstatuschg_evt_bh(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_fwevt *fwevt)
{
	u16 dev_handle = 0;
	u8 uhide = 0, delete = 0, cleanup = 0;
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	struct mpi3_event_data_device_status_change *evtdata =
	    (struct mpi3_event_data_device_status_change *)fwevt->event_data;

	dev_handle = le16_to_cpu(evtdata->dev_handle);
	ioc_info(mrioc,
	    "%s :device status change: handle(0x%04x): reason code(0x%x)\n",
	    __func__, dev_handle, evtdata->reason_code);
	switch (evtdata->reason_code) {
	case MPI3_EVENT_DEV_STAT_RC_HIDDEN:
		delete = 1;
		break;
	case MPI3_EVENT_DEV_STAT_RC_NOT_HIDDEN:
		uhide = 1;
		break;
	case MPI3_EVENT_DEV_STAT_RC_VD_NOT_RESPONDING:
		delete = 1;
		cleanup = 1;
		break;
	default:
		ioc_info(mrioc, "%s :Unhandled reason code(0x%x)\n", __func__,
		    evtdata->reason_code);
		break;
	}

	tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, dev_handle);
	if (!tgtdev)
		goto out;
	if (uhide) {
		tgtdev->is_hidden = 0;
		if (!tgtdev->host_exposed)
			mpi3mr_report_tgtdev_to_host(mrioc, tgtdev->perst_id);
	}
	if (tgtdev->starget && tgtdev->starget->hostdata) {
		if (delete)
			mpi3mr_remove_tgtdev_from_host(mrioc, tgtdev);
	}
	if (cleanup) {
		mpi3mr_tgtdev_del_from_list(mrioc, tgtdev);
		mpi3mr_tgtdev_put(tgtdev);
	}

out:
	if (tgtdev)
		mpi3mr_tgtdev_put(tgtdev);
}

/**
 * mpi3mr_devinfochg_evt_bh - DeviceInfoChange evt bottomhalf
 * @mrioc: Adapter instance reference
 * @dev_pg0: New device page0
 *
 * Process Device Info Change event and based on device's new
 * information, either expose the device to the upper layers, or
 * remove the device from upper layers or update the details of
 * the device.
 *
 * Return: Nothing.
 */
static void mpi3mr_devinfochg_evt_bh(struct mpi3mr_ioc *mrioc,
	struct mpi3_device_page0 *dev_pg0)
{
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	u16 dev_handle = 0, perst_id = 0;

	perst_id = le16_to_cpu(dev_pg0->persistent_id);
	dev_handle = le16_to_cpu(dev_pg0->dev_handle);
	ioc_info(mrioc,
	    "%s :Device info change: handle(0x%04x): persist_id(0x%x)\n",
	    __func__, dev_handle, perst_id);
	tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, dev_handle);
	if (!tgtdev)
		goto out;
	mpi3mr_update_tgtdev(mrioc, tgtdev, dev_pg0, false);
	if (!tgtdev->is_hidden && !tgtdev->host_exposed)
		mpi3mr_report_tgtdev_to_host(mrioc, perst_id);
	if (tgtdev->is_hidden && tgtdev->host_exposed)
		mpi3mr_remove_tgtdev_from_host(mrioc, tgtdev);
	if (!tgtdev->is_hidden && tgtdev->host_exposed && tgtdev->starget)
		starget_for_each_device(tgtdev->starget, (void *)tgtdev,
		    mpi3mr_update_sdev);
out:
	if (tgtdev)
		mpi3mr_tgtdev_put(tgtdev);
}

/**
 * mpi3mr_free_enclosure_list - release enclosures
 * @mrioc: Adapter instance reference
 *
 * Free memory allocated during encloure add.
 *
 * Return nothing.
 */
void mpi3mr_free_enclosure_list(struct mpi3mr_ioc *mrioc)
{
	struct mpi3mr_enclosure_node *enclosure_dev, *enclosure_dev_next;

	list_for_each_entry_safe(enclosure_dev,
	    enclosure_dev_next, &mrioc->enclosure_list, list) {
		list_del(&enclosure_dev->list);
		kfree(enclosure_dev);
	}
}

/**
 * mpi3mr_enclosure_find_by_handle - enclosure search by handle
 * @mrioc: Adapter instance reference
 * @handle: Firmware device handle of the enclosure
 *
 * This searches for enclosure device based on handle, then returns the
 * enclosure object.
 *
 * Return: Enclosure object reference or NULL
 */
struct mpi3mr_enclosure_node *mpi3mr_enclosure_find_by_handle(
	struct mpi3mr_ioc *mrioc, u16 handle)
{
	struct mpi3mr_enclosure_node *enclosure_dev, *r = NULL;

	list_for_each_entry(enclosure_dev, &mrioc->enclosure_list, list) {
		if (le16_to_cpu(enclosure_dev->pg0.enclosure_handle) != handle)
			continue;
		r = enclosure_dev;
		goto out;
	}
out:
	return r;
}

/**
 * mpi3mr_encldev_add_chg_evt_debug - debug for enclosure event
 * @mrioc: Adapter instance reference
 * @encl_pg0: Enclosure page 0.
 * @is_added: Added event or not
 *
 * Return nothing.
 */
static void mpi3mr_encldev_add_chg_evt_debug(struct mpi3mr_ioc *mrioc,
	struct mpi3_enclosure_page0 *encl_pg0, u8 is_added)
{
	char *reason_str = NULL;

	if (!(mrioc->logging_level & MPI3_DEBUG_EVENT_WORK_TASK))
		return;

	if (is_added)
		reason_str = "enclosure added";
	else
		reason_str = "enclosure dev status changed";

	ioc_info(mrioc,
	    "%s: handle(0x%04x), enclosure logical id(0x%016llx)\n",
	    reason_str, le16_to_cpu(encl_pg0->enclosure_handle),
	    (unsigned long long)le64_to_cpu(encl_pg0->enclosure_logical_id));
	ioc_info(mrioc,
	    "number of slots(%d), port(%d), flags(0x%04x), present(%d)\n",
	    le16_to_cpu(encl_pg0->num_slots), encl_pg0->io_unit_port,
	    le16_to_cpu(encl_pg0->flags),
	    ((le16_to_cpu(encl_pg0->flags) &
	      MPI3_ENCLS0_FLAGS_ENCL_DEV_PRESENT_MASK) >> 4));
}

/**
 * mpi3mr_encldev_add_chg_evt_bh - Enclosure evt bottomhalf
 * @mrioc: Adapter instance reference
 * @fwevt: Firmware event reference
 *
 * Prints information about the Enclosure device status or
 * Enclosure add events if logging is enabled and add or remove
 * the enclosure from the controller's internal list of
 * enclosures.
 *
 * Return: Nothing.
 */
static void mpi3mr_encldev_add_chg_evt_bh(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_fwevt *fwevt)
{
	struct mpi3mr_enclosure_node *enclosure_dev = NULL;
	struct mpi3_enclosure_page0 *encl_pg0;
	u16 encl_handle;
	u8 added, present;

	encl_pg0 = (struct mpi3_enclosure_page0 *) fwevt->event_data;
	added = (fwevt->event_id == MPI3_EVENT_ENCL_DEVICE_ADDED) ? 1 : 0;
	mpi3mr_encldev_add_chg_evt_debug(mrioc, encl_pg0, added);


	encl_handle = le16_to_cpu(encl_pg0->enclosure_handle);
	present = ((le16_to_cpu(encl_pg0->flags) &
	      MPI3_ENCLS0_FLAGS_ENCL_DEV_PRESENT_MASK) >> 4);

	if (encl_handle)
		enclosure_dev = mpi3mr_enclosure_find_by_handle(mrioc,
		    encl_handle);
	if (!enclosure_dev && present) {
		enclosure_dev =
			kzalloc(sizeof(struct mpi3mr_enclosure_node),
			    GFP_KERNEL);
		if (!enclosure_dev)
			return;
		list_add_tail(&enclosure_dev->list,
		    &mrioc->enclosure_list);
	}
	if (enclosure_dev) {
		if (!present) {
			list_del(&enclosure_dev->list);
			kfree(enclosure_dev);
		} else
			memcpy(&enclosure_dev->pg0, encl_pg0,
			    sizeof(enclosure_dev->pg0));

	}
}

/**
 * mpi3mr_sastopochg_evt_debug - SASTopoChange details
 * @mrioc: Adapter instance reference
 * @event_data: SAS topology change list event data
 *
 * Prints information about the SAS topology change event.
 *
 * Return: Nothing.
 */
static void
mpi3mr_sastopochg_evt_debug(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_data_sas_topology_change_list *event_data)
{
	int i;
	u16 handle;
	u8 reason_code, phy_number;
	char *status_str = NULL;
	u8 link_rate, prev_link_rate;

	switch (event_data->exp_status) {
	case MPI3_EVENT_SAS_TOPO_ES_NOT_RESPONDING:
		status_str = "remove";
		break;
	case MPI3_EVENT_SAS_TOPO_ES_RESPONDING:
		status_str =  "responding";
		break;
	case MPI3_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING:
		status_str = "remove delay";
		break;
	case MPI3_EVENT_SAS_TOPO_ES_NO_EXPANDER:
		status_str = "direct attached";
		break;
	default:
		status_str = "unknown status";
		break;
	}
	ioc_info(mrioc, "%s :sas topology change: (%s)\n",
	    __func__, status_str);
	ioc_info(mrioc,
	    "%s :\texpander_handle(0x%04x), port(%d), enclosure_handle(0x%04x) start_phy(%02d), num_entries(%d)\n",
	    __func__, le16_to_cpu(event_data->expander_dev_handle),
	    event_data->io_unit_port,
	    le16_to_cpu(event_data->enclosure_handle),
	    event_data->start_phy_num, event_data->num_entries);
	for (i = 0; i < event_data->num_entries; i++) {
		handle = le16_to_cpu(event_data->phy_entry[i].attached_dev_handle);
		if (!handle)
			continue;
		phy_number = event_data->start_phy_num + i;
		reason_code = event_data->phy_entry[i].status &
		    MPI3_EVENT_SAS_TOPO_PHY_RC_MASK;
		switch (reason_code) {
		case MPI3_EVENT_SAS_TOPO_PHY_RC_TARG_NOT_RESPONDING:
			status_str = "target remove";
			break;
		case MPI3_EVENT_SAS_TOPO_PHY_RC_DELAY_NOT_RESPONDING:
			status_str = "delay target remove";
			break;
		case MPI3_EVENT_SAS_TOPO_PHY_RC_PHY_CHANGED:
			status_str = "link status change";
			break;
		case MPI3_EVENT_SAS_TOPO_PHY_RC_NO_CHANGE:
			status_str = "link status no change";
			break;
		case MPI3_EVENT_SAS_TOPO_PHY_RC_RESPONDING:
			status_str = "target responding";
			break;
		default:
			status_str = "unknown";
			break;
		}
		link_rate = event_data->phy_entry[i].link_rate >> 4;
		prev_link_rate = event_data->phy_entry[i].link_rate & 0xF;
		ioc_info(mrioc,
		    "%s :\tphy(%02d), attached_handle(0x%04x): %s: link rate: new(0x%02x), old(0x%02x)\n",
		    __func__, phy_number, handle, status_str, link_rate,
		    prev_link_rate);
	}
}

/**
 * mpi3mr_sastopochg_evt_bh - SASTopologyChange evt bottomhalf
 * @mrioc: Adapter instance reference
 * @fwevt: Firmware event reference
 *
 * Prints information about the SAS topology change event and
 * for "not responding" event code, removes the device from the
 * upper layers.
 *
 * Return: Nothing.
 */
static void mpi3mr_sastopochg_evt_bh(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_fwevt *fwevt)
{
	struct mpi3_event_data_sas_topology_change_list *event_data =
	    (struct mpi3_event_data_sas_topology_change_list *)fwevt->event_data;
	int i;
	u16 handle;
	u8 reason_code;
	u64 exp_sas_address = 0, parent_sas_address = 0;
	struct mpi3mr_hba_port *hba_port = NULL;
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	struct mpi3mr_sas_node *sas_expander = NULL;
	unsigned long flags;
	u8 link_rate, prev_link_rate, parent_phy_number;

	mpi3mr_sastopochg_evt_debug(mrioc, event_data);
	if (mrioc->sas_transport_enabled) {
		hba_port = mpi3mr_get_hba_port_by_id(mrioc,
		    event_data->io_unit_port);
		if (le16_to_cpu(event_data->expander_dev_handle)) {
			spin_lock_irqsave(&mrioc->sas_node_lock, flags);
			sas_expander = __mpi3mr_expander_find_by_handle(mrioc,
			    le16_to_cpu(event_data->expander_dev_handle));
			if (sas_expander) {
				exp_sas_address = sas_expander->sas_address;
				hba_port = sas_expander->hba_port;
			}
			spin_unlock_irqrestore(&mrioc->sas_node_lock, flags);
			parent_sas_address = exp_sas_address;
		} else
			parent_sas_address = mrioc->sas_hba.sas_address;
	}

	for (i = 0; i < event_data->num_entries; i++) {
		if (fwevt->discard)
			return;
		handle = le16_to_cpu(event_data->phy_entry[i].attached_dev_handle);
		if (!handle)
			continue;
		tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, handle);
		if (!tgtdev)
			continue;

		reason_code = event_data->phy_entry[i].status &
		    MPI3_EVENT_SAS_TOPO_PHY_RC_MASK;

		switch (reason_code) {
		case MPI3_EVENT_SAS_TOPO_PHY_RC_TARG_NOT_RESPONDING:
			if (tgtdev->host_exposed)
				mpi3mr_remove_tgtdev_from_host(mrioc, tgtdev);
			mpi3mr_tgtdev_del_from_list(mrioc, tgtdev);
			mpi3mr_tgtdev_put(tgtdev);
			break;
		case MPI3_EVENT_SAS_TOPO_PHY_RC_RESPONDING:
		case MPI3_EVENT_SAS_TOPO_PHY_RC_PHY_CHANGED:
		case MPI3_EVENT_SAS_TOPO_PHY_RC_NO_CHANGE:
		{
			if (!mrioc->sas_transport_enabled || tgtdev->non_stl
			    || tgtdev->is_hidden)
				break;
			link_rate = event_data->phy_entry[i].link_rate >> 4;
			prev_link_rate = event_data->phy_entry[i].link_rate & 0xF;
			if (link_rate == prev_link_rate)
				break;
			if (!parent_sas_address)
				break;
			parent_phy_number = event_data->start_phy_num + i;
			mpi3mr_update_links(mrioc, parent_sas_address, handle,
			    parent_phy_number, link_rate, hba_port);
			break;
		}
		default:
			break;
		}
		if (tgtdev)
			mpi3mr_tgtdev_put(tgtdev);
	}

	if (mrioc->sas_transport_enabled && (event_data->exp_status ==
	    MPI3_EVENT_SAS_TOPO_ES_NOT_RESPONDING)) {
		if (sas_expander)
			mpi3mr_expander_remove(mrioc, exp_sas_address,
			    hba_port);
	}
}

/**
 * mpi3mr_pcietopochg_evt_debug - PCIeTopoChange details
 * @mrioc: Adapter instance reference
 * @event_data: PCIe topology change list event data
 *
 * Prints information about the PCIe topology change event.
 *
 * Return: Nothing.
 */
static void
mpi3mr_pcietopochg_evt_debug(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_data_pcie_topology_change_list *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 port_number;
	char *status_str = NULL;
	u8 link_rate, prev_link_rate;

	switch (event_data->switch_status) {
	case MPI3_EVENT_PCIE_TOPO_SS_NOT_RESPONDING:
		status_str = "remove";
		break;
	case MPI3_EVENT_PCIE_TOPO_SS_RESPONDING:
		status_str =  "responding";
		break;
	case MPI3_EVENT_PCIE_TOPO_SS_DELAY_NOT_RESPONDING:
		status_str = "remove delay";
		break;
	case MPI3_EVENT_PCIE_TOPO_SS_NO_PCIE_SWITCH:
		status_str = "direct attached";
		break;
	default:
		status_str = "unknown status";
		break;
	}
	ioc_info(mrioc, "%s :pcie topology change: (%s)\n",
	    __func__, status_str);
	ioc_info(mrioc,
	    "%s :\tswitch_handle(0x%04x), enclosure_handle(0x%04x) start_port(%02d), num_entries(%d)\n",
	    __func__, le16_to_cpu(event_data->switch_dev_handle),
	    le16_to_cpu(event_data->enclosure_handle),
	    event_data->start_port_num, event_data->num_entries);
	for (i = 0; i < event_data->num_entries; i++) {
		handle =
		    le16_to_cpu(event_data->port_entry[i].attached_dev_handle);
		if (!handle)
			continue;
		port_number = event_data->start_port_num + i;
		reason_code = event_data->port_entry[i].port_status;
		switch (reason_code) {
		case MPI3_EVENT_PCIE_TOPO_PS_NOT_RESPONDING:
			status_str = "target remove";
			break;
		case MPI3_EVENT_PCIE_TOPO_PS_DELAY_NOT_RESPONDING:
			status_str = "delay target remove";
			break;
		case MPI3_EVENT_PCIE_TOPO_PS_PORT_CHANGED:
			status_str = "link status change";
			break;
		case MPI3_EVENT_PCIE_TOPO_PS_NO_CHANGE:
			status_str = "link status no change";
			break;
		case MPI3_EVENT_PCIE_TOPO_PS_RESPONDING:
			status_str = "target responding";
			break;
		default:
			status_str = "unknown";
			break;
		}
		link_rate = event_data->port_entry[i].current_port_info &
		    MPI3_EVENT_PCIE_TOPO_PI_RATE_MASK;
		prev_link_rate = event_data->port_entry[i].previous_port_info &
		    MPI3_EVENT_PCIE_TOPO_PI_RATE_MASK;
		ioc_info(mrioc,
		    "%s :\tport(%02d), attached_handle(0x%04x): %s: link rate: new(0x%02x), old(0x%02x)\n",
		    __func__, port_number, handle, status_str, link_rate,
		    prev_link_rate);
	}
}

/**
 * mpi3mr_pcietopochg_evt_bh - PCIeTopologyChange evt bottomhalf
 * @mrioc: Adapter instance reference
 * @fwevt: Firmware event reference
 *
 * Prints information about the PCIe topology change event and
 * for "not responding" event code, removes the device from the
 * upper layers.
 *
 * Return: Nothing.
 */
static void mpi3mr_pcietopochg_evt_bh(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_fwevt *fwevt)
{
	struct mpi3_event_data_pcie_topology_change_list *event_data =
	    (struct mpi3_event_data_pcie_topology_change_list *)fwevt->event_data;
	int i;
	u16 handle;
	u8 reason_code;
	struct mpi3mr_tgt_dev *tgtdev = NULL;

	mpi3mr_pcietopochg_evt_debug(mrioc, event_data);

	for (i = 0; i < event_data->num_entries; i++) {
		if (fwevt->discard)
			return;
		handle =
		    le16_to_cpu(event_data->port_entry[i].attached_dev_handle);
		if (!handle)
			continue;
		tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, handle);
		if (!tgtdev)
			continue;

		reason_code = event_data->port_entry[i].port_status;

		switch (reason_code) {
		case MPI3_EVENT_PCIE_TOPO_PS_NOT_RESPONDING:
			if (tgtdev->host_exposed)
				mpi3mr_remove_tgtdev_from_host(mrioc, tgtdev);
			mpi3mr_tgtdev_del_from_list(mrioc, tgtdev);
			mpi3mr_tgtdev_put(tgtdev);
			break;
		default:
			break;
		}
		if (tgtdev)
			mpi3mr_tgtdev_put(tgtdev);
	}
}

/**
 * mpi3mr_logdata_evt_bh -  Log data event bottomhalf
 * @mrioc: Adapter instance reference
 * @fwevt: Firmware event reference
 *
 * Extracts the event data and calls application interfacing
 * function to process the event further.
 *
 * Return: Nothing.
 */
static void mpi3mr_logdata_evt_bh(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_fwevt *fwevt)
{
	mpi3mr_app_save_logdata(mrioc, fwevt->event_data,
	    fwevt->event_data_size);
}

/**
 * mpi3mr_update_sdev_qd - Update SCSI device queue depath
 * @sdev: SCSI device reference
 * @data: Queue depth reference
 *
 * This is an iterator function called for each SCSI device in a
 * target to update the QD of each SCSI device.
 *
 * Return: Nothing.
 */
static void mpi3mr_update_sdev_qd(struct scsi_device *sdev, void *data)
{
	u16 *q_depth = (u16 *)data;

	scsi_change_queue_depth(sdev, (int)*q_depth);
	sdev->max_queue_depth = sdev->queue_depth;
}

/**
 * mpi3mr_set_qd_for_all_vd_in_tg -set QD for TG VDs
 * @mrioc: Adapter instance reference
 * @tg: Throttle group information pointer
 *
 * Accessor to reduce QD for each device associated with the
 * given throttle group.
 *
 * Return: None.
 */
static void mpi3mr_set_qd_for_all_vd_in_tg(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_throttle_group_info *tg)
{
	unsigned long flags;
	struct mpi3mr_tgt_dev *tgtdev;
	struct mpi3mr_stgt_priv_data *tgt_priv;


	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list) {
		if (tgtdev->starget && tgtdev->starget->hostdata) {
			tgt_priv = tgtdev->starget->hostdata;
			if (tgt_priv->throttle_group == tg) {
				dprint_event_bh(mrioc,
				    "updating qd due to throttling for persist_id(%d) original_qd(%d), reduced_qd (%d)\n",
				    tgt_priv->perst_id, tgtdev->q_depth,
				    tg->modified_qd);
				starget_for_each_device(tgtdev->starget,
				    (void *)&tg->modified_qd,
				    mpi3mr_update_sdev_qd);
			}
		}
	}
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
}

/**
 * mpi3mr_fwevt_bh - Firmware event bottomhalf handler
 * @mrioc: Adapter instance reference
 * @fwevt: Firmware event reference
 *
 * Identifies the firmware event and calls corresponding bottomg
 * half handler and sends event acknowledgment if required.
 *
 * Return: Nothing.
 */
static void mpi3mr_fwevt_bh(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_fwevt *fwevt)
{
	struct mpi3_device_page0 *dev_pg0 = NULL;
	u16 perst_id, handle, dev_info;
	struct mpi3_device0_sas_sata_format *sasinf = NULL;

	mpi3mr_fwevt_del_from_list(mrioc, fwevt);
	mrioc->current_event = fwevt;

	if (mrioc->stop_drv_processing)
		goto out;

	if (mrioc->unrecoverable) {
		dprint_event_bh(mrioc,
		    "ignoring event(0x%02x) in bottom half handler due to unrecoverable controller\n",
		    fwevt->event_id);
		goto out;
	}

	if (!fwevt->process_evt)
		goto evt_ack;

	switch (fwevt->event_id) {
	case MPI3_EVENT_DEVICE_ADDED:
	{
		dev_pg0 = (struct mpi3_device_page0 *)fwevt->event_data;
		perst_id = le16_to_cpu(dev_pg0->persistent_id);
		handle = le16_to_cpu(dev_pg0->dev_handle);
		if (perst_id != MPI3_DEVICE0_PERSISTENTID_INVALID)
			mpi3mr_report_tgtdev_to_host(mrioc, perst_id);
		else if (mrioc->sas_transport_enabled &&
		    (dev_pg0->device_form == MPI3_DEVICE_DEVFORM_SAS_SATA)) {
			sasinf = &dev_pg0->device_specific.sas_sata_format;
			dev_info = le16_to_cpu(sasinf->device_info);
			if (!mrioc->sas_hba.num_phys)
				mpi3mr_sas_host_add(mrioc);
			else
				mpi3mr_sas_host_refresh(mrioc);

			if (mpi3mr_is_expander_device(dev_info))
				mpi3mr_expander_add(mrioc, handle);
		}
		break;
	}
	case MPI3_EVENT_DEVICE_INFO_CHANGED:
	{
		dev_pg0 = (struct mpi3_device_page0 *)fwevt->event_data;
		perst_id = le16_to_cpu(dev_pg0->persistent_id);
		if (perst_id != MPI3_DEVICE0_PERSISTENTID_INVALID)
			mpi3mr_devinfochg_evt_bh(mrioc, dev_pg0);
		break;
	}
	case MPI3_EVENT_DEVICE_STATUS_CHANGE:
	{
		mpi3mr_devstatuschg_evt_bh(mrioc, fwevt);
		break;
	}
	case MPI3_EVENT_ENCL_DEVICE_ADDED:
	case MPI3_EVENT_ENCL_DEVICE_STATUS_CHANGE:
	{
		mpi3mr_encldev_add_chg_evt_bh(mrioc, fwevt);
		break;
	}

	case MPI3_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
	{
		mpi3mr_sastopochg_evt_bh(mrioc, fwevt);
		break;
	}
	case MPI3_EVENT_PCIE_TOPOLOGY_CHANGE_LIST:
	{
		mpi3mr_pcietopochg_evt_bh(mrioc, fwevt);
		break;
	}
	case MPI3_EVENT_LOG_DATA:
	{
		mpi3mr_logdata_evt_bh(mrioc, fwevt);
		break;
	}
	case MPI3MR_DRIVER_EVENT_TG_QD_REDUCTION:
	{
		struct mpi3mr_throttle_group_info *tg;

		tg = *(struct mpi3mr_throttle_group_info **)fwevt->event_data;
		dprint_event_bh(mrioc,
		    "qd reduction event processed for tg_id(%d) reduction_needed(%d)\n",
		    tg->id, tg->need_qd_reduction);
		if (tg->need_qd_reduction) {
			mpi3mr_set_qd_for_all_vd_in_tg(mrioc, tg);
			tg->need_qd_reduction = 0;
		}
		break;
	}
	case MPI3_EVENT_WAIT_FOR_DEVICES_TO_REFRESH:
	{
		while (mrioc->device_refresh_on)
			msleep(500);

		dprint_event_bh(mrioc,
		    "scan for non responding and newly added devices after soft reset started\n");
		if (mrioc->sas_transport_enabled) {
			mpi3mr_refresh_sas_ports(mrioc);
			mpi3mr_refresh_expanders(mrioc);
		}
		mpi3mr_rfresh_tgtdevs(mrioc);
		ioc_info(mrioc,
		    "scan for non responding and newly added devices after soft reset completed\n");
		break;
	}
	default:
		break;
	}

evt_ack:
	if (fwevt->send_ack)
		mpi3mr_process_event_ack(mrioc, fwevt->event_id,
		    fwevt->evt_ctx);
out:
	/* Put fwevt reference count to neutralize kref_init increment */
	mpi3mr_fwevt_put(fwevt);
	mrioc->current_event = NULL;
}

/**
 * mpi3mr_fwevt_worker - Firmware event worker
 * @work: Work struct containing firmware event
 *
 * Extracts the firmware event and calls mpi3mr_fwevt_bh.
 *
 * Return: Nothing.
 */
static void mpi3mr_fwevt_worker(struct work_struct *work)
{
	struct mpi3mr_fwevt *fwevt = container_of(work, struct mpi3mr_fwevt,
	    work);
	mpi3mr_fwevt_bh(fwevt->mrioc, fwevt);
	/*
	 * Put fwevt reference count after
	 * dequeuing it from worker queue
	 */
	mpi3mr_fwevt_put(fwevt);
}

/**
 * mpi3mr_create_tgtdev - Create and add a target device
 * @mrioc: Adapter instance reference
 * @dev_pg0: Device Page 0 data
 *
 * If the device specified by the device page 0 data is not
 * present in the driver's internal list, allocate the memory
 * for the device, populate the data and add to the list, else
 * update the device data.  The key is persistent ID.
 *
 * Return: 0 on success, -ENOMEM on memory allocation failure
 */
static int mpi3mr_create_tgtdev(struct mpi3mr_ioc *mrioc,
	struct mpi3_device_page0 *dev_pg0)
{
	int retval = 0;
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	u16 perst_id = 0;

	perst_id = le16_to_cpu(dev_pg0->persistent_id);
	if (perst_id == MPI3_DEVICE0_PERSISTENTID_INVALID)
		return retval;

	tgtdev = mpi3mr_get_tgtdev_by_perst_id(mrioc, perst_id);
	if (tgtdev) {
		mpi3mr_update_tgtdev(mrioc, tgtdev, dev_pg0, true);
		mpi3mr_tgtdev_put(tgtdev);
	} else {
		tgtdev = mpi3mr_alloc_tgtdev();
		if (!tgtdev)
			return -ENOMEM;
		mpi3mr_update_tgtdev(mrioc, tgtdev, dev_pg0, true);
		mpi3mr_tgtdev_add_to_list(mrioc, tgtdev);
	}

	return retval;
}

/**
 * mpi3mr_flush_delayed_cmd_lists - Flush pending commands
 * @mrioc: Adapter instance reference
 *
 * Flush pending commands in the delayed lists due to a
 * controller reset or driver removal as a cleanup.
 *
 * Return: Nothing
 */
void mpi3mr_flush_delayed_cmd_lists(struct mpi3mr_ioc *mrioc)
{
	struct delayed_dev_rmhs_node *_rmhs_node;
	struct delayed_evt_ack_node *_evtack_node;

	dprint_reset(mrioc, "flushing delayed dev_remove_hs commands\n");
	while (!list_empty(&mrioc->delayed_rmhs_list)) {
		_rmhs_node = list_entry(mrioc->delayed_rmhs_list.next,
		    struct delayed_dev_rmhs_node, list);
		list_del(&_rmhs_node->list);
		kfree(_rmhs_node);
	}
	dprint_reset(mrioc, "flushing delayed event ack commands\n");
	while (!list_empty(&mrioc->delayed_evtack_cmds_list)) {
		_evtack_node = list_entry(mrioc->delayed_evtack_cmds_list.next,
		    struct delayed_evt_ack_node, list);
		list_del(&_evtack_node->list);
		kfree(_evtack_node);
	}
}

/**
 * mpi3mr_dev_rmhs_complete_iou - Device removal IOUC completion
 * @mrioc: Adapter instance reference
 * @drv_cmd: Internal command tracker
 *
 * Issues a target reset TM to the firmware from the device
 * removal TM pend list or retry the removal handshake sequence
 * based on the IOU control request IOC status.
 *
 * Return: Nothing
 */
static void mpi3mr_dev_rmhs_complete_iou(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd)
{
	u16 cmd_idx = drv_cmd->host_tag - MPI3MR_HOSTTAG_DEVRMCMD_MIN;
	struct delayed_dev_rmhs_node *delayed_dev_rmhs = NULL;

	if (drv_cmd->state & MPI3MR_CMD_RESET)
		goto clear_drv_cmd;

	ioc_info(mrioc,
	    "%s :dev_rmhs_iouctrl_complete:handle(0x%04x), ioc_status(0x%04x), loginfo(0x%08x)\n",
	    __func__, drv_cmd->dev_handle, drv_cmd->ioc_status,
	    drv_cmd->ioc_loginfo);
	if (drv_cmd->ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		if (drv_cmd->retry_count < MPI3MR_DEV_RMHS_RETRY_COUNT) {
			drv_cmd->retry_count++;
			ioc_info(mrioc,
			    "%s :dev_rmhs_iouctrl_complete: handle(0x%04x)retrying handshake retry=%d\n",
			    __func__, drv_cmd->dev_handle,
			    drv_cmd->retry_count);
			mpi3mr_dev_rmhs_send_tm(mrioc, drv_cmd->dev_handle,
			    drv_cmd, drv_cmd->iou_rc);
			return;
		}
		ioc_err(mrioc,
		    "%s :dev removal handshake failed after all retries: handle(0x%04x)\n",
		    __func__, drv_cmd->dev_handle);
	} else {
		ioc_info(mrioc,
		    "%s :dev removal handshake completed successfully: handle(0x%04x)\n",
		    __func__, drv_cmd->dev_handle);
		clear_bit(drv_cmd->dev_handle, mrioc->removepend_bitmap);
	}

	if (!list_empty(&mrioc->delayed_rmhs_list)) {
		delayed_dev_rmhs = list_entry(mrioc->delayed_rmhs_list.next,
		    struct delayed_dev_rmhs_node, list);
		drv_cmd->dev_handle = delayed_dev_rmhs->handle;
		drv_cmd->retry_count = 0;
		drv_cmd->iou_rc = delayed_dev_rmhs->iou_rc;
		ioc_info(mrioc,
		    "%s :dev_rmhs_iouctrl_complete: processing delayed TM: handle(0x%04x)\n",
		    __func__, drv_cmd->dev_handle);
		mpi3mr_dev_rmhs_send_tm(mrioc, drv_cmd->dev_handle, drv_cmd,
		    drv_cmd->iou_rc);
		list_del(&delayed_dev_rmhs->list);
		kfree(delayed_dev_rmhs);
		return;
	}

clear_drv_cmd:
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
	drv_cmd->callback = NULL;
	drv_cmd->retry_count = 0;
	drv_cmd->dev_handle = MPI3MR_INVALID_DEV_HANDLE;
	clear_bit(cmd_idx, mrioc->devrem_bitmap);
}

/**
 * mpi3mr_dev_rmhs_complete_tm - Device removal TM completion
 * @mrioc: Adapter instance reference
 * @drv_cmd: Internal command tracker
 *
 * Issues a target reset TM to the firmware from the device
 * removal TM pend list or issue IO unit control request as
 * part of device removal or hidden acknowledgment handshake.
 *
 * Return: Nothing
 */
static void mpi3mr_dev_rmhs_complete_tm(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd)
{
	struct mpi3_iounit_control_request iou_ctrl;
	u16 cmd_idx = drv_cmd->host_tag - MPI3MR_HOSTTAG_DEVRMCMD_MIN;
	struct mpi3_scsi_task_mgmt_reply *tm_reply = NULL;
	int retval;

	if (drv_cmd->state & MPI3MR_CMD_RESET)
		goto clear_drv_cmd;

	if (drv_cmd->state & MPI3MR_CMD_REPLY_VALID)
		tm_reply = (struct mpi3_scsi_task_mgmt_reply *)drv_cmd->reply;

	if (tm_reply)
		pr_info(IOCNAME
		    "dev_rmhs_tr_complete:handle(0x%04x), ioc_status(0x%04x), loginfo(0x%08x), term_count(%d)\n",
		    mrioc->name, drv_cmd->dev_handle, drv_cmd->ioc_status,
		    drv_cmd->ioc_loginfo,
		    le32_to_cpu(tm_reply->termination_count));

	pr_info(IOCNAME "Issuing IOU CTL: handle(0x%04x) dev_rmhs idx(%d)\n",
	    mrioc->name, drv_cmd->dev_handle, cmd_idx);

	memset(&iou_ctrl, 0, sizeof(iou_ctrl));

	drv_cmd->state = MPI3MR_CMD_PENDING;
	drv_cmd->is_waiting = 0;
	drv_cmd->callback = mpi3mr_dev_rmhs_complete_iou;
	iou_ctrl.operation = drv_cmd->iou_rc;
	iou_ctrl.param16[0] = cpu_to_le16(drv_cmd->dev_handle);
	iou_ctrl.host_tag = cpu_to_le16(drv_cmd->host_tag);
	iou_ctrl.function = MPI3_FUNCTION_IO_UNIT_CONTROL;

	retval = mpi3mr_admin_request_post(mrioc, &iou_ctrl, sizeof(iou_ctrl),
	    1);
	if (retval) {
		pr_err(IOCNAME "Issue DevRmHsTMIOUCTL: Admin post failed\n",
		    mrioc->name);
		goto clear_drv_cmd;
	}

	return;
clear_drv_cmd:
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
	drv_cmd->callback = NULL;
	drv_cmd->dev_handle = MPI3MR_INVALID_DEV_HANDLE;
	drv_cmd->retry_count = 0;
	clear_bit(cmd_idx, mrioc->devrem_bitmap);
}

/**
 * mpi3mr_dev_rmhs_send_tm - Issue TM for device removal
 * @mrioc: Adapter instance reference
 * @handle: Device handle
 * @cmdparam: Internal command tracker
 * @iou_rc: IO unit reason code
 *
 * Issues a target reset TM to the firmware or add it to a pend
 * list as part of device removal or hidden acknowledgment
 * handshake.
 *
 * Return: Nothing
 */
static void mpi3mr_dev_rmhs_send_tm(struct mpi3mr_ioc *mrioc, u16 handle,
	struct mpi3mr_drv_cmd *cmdparam, u8 iou_rc)
{
	struct mpi3_scsi_task_mgmt_request tm_req;
	int retval = 0;
	u16 cmd_idx = MPI3MR_NUM_DEVRMCMD;
	u8 retrycount = 5;
	struct mpi3mr_drv_cmd *drv_cmd = cmdparam;
	struct delayed_dev_rmhs_node *delayed_dev_rmhs = NULL;

	if (drv_cmd)
		goto issue_cmd;
	do {
		cmd_idx = find_first_zero_bit(mrioc->devrem_bitmap,
		    MPI3MR_NUM_DEVRMCMD);
		if (cmd_idx < MPI3MR_NUM_DEVRMCMD) {
			if (!test_and_set_bit(cmd_idx, mrioc->devrem_bitmap))
				break;
			cmd_idx = MPI3MR_NUM_DEVRMCMD;
		}
	} while (retrycount--);

	if (cmd_idx >= MPI3MR_NUM_DEVRMCMD) {
		delayed_dev_rmhs = kzalloc(sizeof(*delayed_dev_rmhs),
		    GFP_ATOMIC);
		if (!delayed_dev_rmhs)
			return;
		INIT_LIST_HEAD(&delayed_dev_rmhs->list);
		delayed_dev_rmhs->handle = handle;
		delayed_dev_rmhs->iou_rc = iou_rc;
		list_add_tail(&delayed_dev_rmhs->list,
		    &mrioc->delayed_rmhs_list);
		ioc_info(mrioc, "%s :DevRmHs: tr:handle(0x%04x) is postponed\n",
		    __func__, handle);
		return;
	}
	drv_cmd = &mrioc->dev_rmhs_cmds[cmd_idx];

issue_cmd:
	cmd_idx = drv_cmd->host_tag - MPI3MR_HOSTTAG_DEVRMCMD_MIN;
	ioc_info(mrioc,
	    "%s :Issuing TR TM: for devhandle 0x%04x with dev_rmhs %d\n",
	    __func__, handle, cmd_idx);

	memset(&tm_req, 0, sizeof(tm_req));
	if (drv_cmd->state & MPI3MR_CMD_PENDING) {
		ioc_err(mrioc, "%s :Issue TM: Command is in use\n", __func__);
		goto out;
	}
	drv_cmd->state = MPI3MR_CMD_PENDING;
	drv_cmd->is_waiting = 0;
	drv_cmd->callback = mpi3mr_dev_rmhs_complete_tm;
	drv_cmd->dev_handle = handle;
	drv_cmd->iou_rc = iou_rc;
	tm_req.dev_handle = cpu_to_le16(handle);
	tm_req.task_type = MPI3_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	tm_req.host_tag = cpu_to_le16(drv_cmd->host_tag);
	tm_req.task_host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INVALID);
	tm_req.function = MPI3_FUNCTION_SCSI_TASK_MGMT;

	set_bit(handle, mrioc->removepend_bitmap);
	retval = mpi3mr_admin_request_post(mrioc, &tm_req, sizeof(tm_req), 1);
	if (retval) {
		ioc_err(mrioc, "%s :Issue DevRmHsTM: Admin Post failed\n",
		    __func__);
		goto out_failed;
	}
out:
	return;
out_failed:
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
	drv_cmd->callback = NULL;
	drv_cmd->dev_handle = MPI3MR_INVALID_DEV_HANDLE;
	drv_cmd->retry_count = 0;
	clear_bit(cmd_idx, mrioc->devrem_bitmap);
}

/**
 * mpi3mr_complete_evt_ack - event ack request completion
 * @mrioc: Adapter instance reference
 * @drv_cmd: Internal command tracker
 *
 * This is the completion handler for non blocking event
 * acknowledgment sent to the firmware and this will issue any
 * pending event acknowledgment request.
 *
 * Return: Nothing
 */
static void mpi3mr_complete_evt_ack(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd)
{
	u16 cmd_idx = drv_cmd->host_tag - MPI3MR_HOSTTAG_EVTACKCMD_MIN;
	struct delayed_evt_ack_node *delayed_evtack = NULL;

	if (drv_cmd->state & MPI3MR_CMD_RESET)
		goto clear_drv_cmd;

	if (drv_cmd->ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		dprint_event_th(mrioc,
		    "immediate event ack failed with ioc_status(0x%04x) log_info(0x%08x)\n",
		    (drv_cmd->ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    drv_cmd->ioc_loginfo);
	}

	if (!list_empty(&mrioc->delayed_evtack_cmds_list)) {
		delayed_evtack =
			list_entry(mrioc->delayed_evtack_cmds_list.next,
			    struct delayed_evt_ack_node, list);
		mpi3mr_send_event_ack(mrioc, delayed_evtack->event, drv_cmd,
		    delayed_evtack->event_ctx);
		list_del(&delayed_evtack->list);
		kfree(delayed_evtack);
		return;
	}
clear_drv_cmd:
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
	drv_cmd->callback = NULL;
	clear_bit(cmd_idx, mrioc->evtack_cmds_bitmap);
}

/**
 * mpi3mr_send_event_ack - Issue event acknwoledgment request
 * @mrioc: Adapter instance reference
 * @event: MPI3 event id
 * @cmdparam: Internal command tracker
 * @event_ctx: event context
 *
 * Issues event acknowledgment request to the firmware if there
 * is a free command to send the event ack else it to a pend
 * list so that it will be processed on a completion of a prior
 * event acknowledgment .
 *
 * Return: Nothing
 */
static void mpi3mr_send_event_ack(struct mpi3mr_ioc *mrioc, u8 event,
	struct mpi3mr_drv_cmd *cmdparam, u32 event_ctx)
{
	struct mpi3_event_ack_request evtack_req;
	int retval = 0;
	u8 retrycount = 5;
	u16 cmd_idx = MPI3MR_NUM_EVTACKCMD;
	struct mpi3mr_drv_cmd *drv_cmd = cmdparam;
	struct delayed_evt_ack_node *delayed_evtack = NULL;

	if (drv_cmd) {
		dprint_event_th(mrioc,
		    "sending delayed event ack in the top half for event(0x%02x), event_ctx(0x%08x)\n",
		    event, event_ctx);
		goto issue_cmd;
	}
	dprint_event_th(mrioc,
	    "sending event ack in the top half for event(0x%02x), event_ctx(0x%08x)\n",
	    event, event_ctx);
	do {
		cmd_idx = find_first_zero_bit(mrioc->evtack_cmds_bitmap,
		    MPI3MR_NUM_EVTACKCMD);
		if (cmd_idx < MPI3MR_NUM_EVTACKCMD) {
			if (!test_and_set_bit(cmd_idx,
			    mrioc->evtack_cmds_bitmap))
				break;
			cmd_idx = MPI3MR_NUM_EVTACKCMD;
		}
	} while (retrycount--);

	if (cmd_idx >= MPI3MR_NUM_EVTACKCMD) {
		delayed_evtack = kzalloc(sizeof(*delayed_evtack),
		    GFP_ATOMIC);
		if (!delayed_evtack)
			return;
		INIT_LIST_HEAD(&delayed_evtack->list);
		delayed_evtack->event = event;
		delayed_evtack->event_ctx = event_ctx;
		list_add_tail(&delayed_evtack->list,
		    &mrioc->delayed_evtack_cmds_list);
		dprint_event_th(mrioc,
		    "event ack in the top half for event(0x%02x), event_ctx(0x%08x) is postponed\n",
		    event, event_ctx);
		return;
	}
	drv_cmd = &mrioc->evtack_cmds[cmd_idx];

issue_cmd:
	cmd_idx = drv_cmd->host_tag - MPI3MR_HOSTTAG_EVTACKCMD_MIN;

	memset(&evtack_req, 0, sizeof(evtack_req));
	if (drv_cmd->state & MPI3MR_CMD_PENDING) {
		dprint_event_th(mrioc,
		    "sending event ack failed due to command in use\n");
		goto out;
	}
	drv_cmd->state = MPI3MR_CMD_PENDING;
	drv_cmd->is_waiting = 0;
	drv_cmd->callback = mpi3mr_complete_evt_ack;
	evtack_req.host_tag = cpu_to_le16(drv_cmd->host_tag);
	evtack_req.function = MPI3_FUNCTION_EVENT_ACK;
	evtack_req.event = event;
	evtack_req.event_context = cpu_to_le32(event_ctx);
	retval = mpi3mr_admin_request_post(mrioc, &evtack_req,
	    sizeof(evtack_req), 1);
	if (retval) {
		dprint_event_th(mrioc,
		    "posting event ack request is failed\n");
		goto out_failed;
	}

	dprint_event_th(mrioc,
	    "event ack in the top half for event(0x%02x), event_ctx(0x%08x) is posted\n",
	    event, event_ctx);
out:
	return;
out_failed:
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
	drv_cmd->callback = NULL;
	clear_bit(cmd_idx, mrioc->evtack_cmds_bitmap);
}

/**
 * mpi3mr_pcietopochg_evt_th - PCIETopologyChange evt tophalf
 * @mrioc: Adapter instance reference
 * @event_reply: event data
 *
 * Checks for the reason code and based on that either block I/O
 * to device, or unblock I/O to the device, or start the device
 * removal handshake with reason as remove with the firmware for
 * PCIe devices.
 *
 * Return: Nothing
 */
static void mpi3mr_pcietopochg_evt_th(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply)
{
	struct mpi3_event_data_pcie_topology_change_list *topo_evt =
	    (struct mpi3_event_data_pcie_topology_change_list *)event_reply->event_data;
	int i;
	u16 handle;
	u8 reason_code;
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data = NULL;

	for (i = 0; i < topo_evt->num_entries; i++) {
		handle = le16_to_cpu(topo_evt->port_entry[i].attached_dev_handle);
		if (!handle)
			continue;
		reason_code = topo_evt->port_entry[i].port_status;
		scsi_tgt_priv_data =  NULL;
		tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, handle);
		if (tgtdev && tgtdev->starget && tgtdev->starget->hostdata)
			scsi_tgt_priv_data = (struct mpi3mr_stgt_priv_data *)
			    tgtdev->starget->hostdata;
		switch (reason_code) {
		case MPI3_EVENT_PCIE_TOPO_PS_NOT_RESPONDING:
			if (scsi_tgt_priv_data) {
				scsi_tgt_priv_data->dev_removed = 1;
				scsi_tgt_priv_data->dev_removedelay = 0;
				atomic_set(&scsi_tgt_priv_data->block_io, 0);
			}
			mpi3mr_dev_rmhs_send_tm(mrioc, handle, NULL,
			    MPI3_CTRL_OP_REMOVE_DEVICE);
			break;
		case MPI3_EVENT_PCIE_TOPO_PS_DELAY_NOT_RESPONDING:
			if (scsi_tgt_priv_data) {
				scsi_tgt_priv_data->dev_removedelay = 1;
				atomic_inc(&scsi_tgt_priv_data->block_io);
			}
			break;
		case MPI3_EVENT_PCIE_TOPO_PS_RESPONDING:
			if (scsi_tgt_priv_data &&
			    scsi_tgt_priv_data->dev_removedelay) {
				scsi_tgt_priv_data->dev_removedelay = 0;
				atomic_dec_if_positive
				    (&scsi_tgt_priv_data->block_io);
			}
			break;
		case MPI3_EVENT_PCIE_TOPO_PS_PORT_CHANGED:
		default:
			break;
		}
		if (tgtdev)
			mpi3mr_tgtdev_put(tgtdev);
	}
}

/**
 * mpi3mr_sastopochg_evt_th - SASTopologyChange evt tophalf
 * @mrioc: Adapter instance reference
 * @event_reply: event data
 *
 * Checks for the reason code and based on that either block I/O
 * to device, or unblock I/O to the device, or start the device
 * removal handshake with reason as remove with the firmware for
 * SAS/SATA devices.
 *
 * Return: Nothing
 */
static void mpi3mr_sastopochg_evt_th(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply)
{
	struct mpi3_event_data_sas_topology_change_list *topo_evt =
	    (struct mpi3_event_data_sas_topology_change_list *)event_reply->event_data;
	int i;
	u16 handle;
	u8 reason_code;
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data = NULL;

	for (i = 0; i < topo_evt->num_entries; i++) {
		handle = le16_to_cpu(topo_evt->phy_entry[i].attached_dev_handle);
		if (!handle)
			continue;
		reason_code = topo_evt->phy_entry[i].status &
		    MPI3_EVENT_SAS_TOPO_PHY_RC_MASK;
		scsi_tgt_priv_data =  NULL;
		tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, handle);
		if (tgtdev && tgtdev->starget && tgtdev->starget->hostdata)
			scsi_tgt_priv_data = (struct mpi3mr_stgt_priv_data *)
			    tgtdev->starget->hostdata;
		switch (reason_code) {
		case MPI3_EVENT_SAS_TOPO_PHY_RC_TARG_NOT_RESPONDING:
			if (scsi_tgt_priv_data) {
				scsi_tgt_priv_data->dev_removed = 1;
				scsi_tgt_priv_data->dev_removedelay = 0;
				atomic_set(&scsi_tgt_priv_data->block_io, 0);
			}
			mpi3mr_dev_rmhs_send_tm(mrioc, handle, NULL,
			    MPI3_CTRL_OP_REMOVE_DEVICE);
			break;
		case MPI3_EVENT_SAS_TOPO_PHY_RC_DELAY_NOT_RESPONDING:
			if (scsi_tgt_priv_data) {
				scsi_tgt_priv_data->dev_removedelay = 1;
				atomic_inc(&scsi_tgt_priv_data->block_io);
			}
			break;
		case MPI3_EVENT_SAS_TOPO_PHY_RC_RESPONDING:
			if (scsi_tgt_priv_data &&
			    scsi_tgt_priv_data->dev_removedelay) {
				scsi_tgt_priv_data->dev_removedelay = 0;
				atomic_dec_if_positive
				    (&scsi_tgt_priv_data->block_io);
			}
			break;
		case MPI3_EVENT_SAS_TOPO_PHY_RC_PHY_CHANGED:
		default:
			break;
		}
		if (tgtdev)
			mpi3mr_tgtdev_put(tgtdev);
	}
}

/**
 * mpi3mr_devstatuschg_evt_th - DeviceStatusChange evt tophalf
 * @mrioc: Adapter instance reference
 * @event_reply: event data
 *
 * Checks for the reason code and based on that either block I/O
 * to device, or unblock I/O to the device, or start the device
 * removal handshake with reason as remove/hide acknowledgment
 * with the firmware.
 *
 * Return: Nothing
 */
static void mpi3mr_devstatuschg_evt_th(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply)
{
	u16 dev_handle = 0;
	u8 ublock = 0, block = 0, hide = 0, delete = 0, remove = 0;
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data = NULL;
	struct mpi3_event_data_device_status_change *evtdata =
	    (struct mpi3_event_data_device_status_change *)event_reply->event_data;

	if (mrioc->stop_drv_processing)
		goto out;

	dev_handle = le16_to_cpu(evtdata->dev_handle);

	switch (evtdata->reason_code) {
	case MPI3_EVENT_DEV_STAT_RC_INT_DEVICE_RESET_STRT:
	case MPI3_EVENT_DEV_STAT_RC_INT_IT_NEXUS_RESET_STRT:
		block = 1;
		break;
	case MPI3_EVENT_DEV_STAT_RC_HIDDEN:
		delete = 1;
		hide = 1;
		break;
	case MPI3_EVENT_DEV_STAT_RC_VD_NOT_RESPONDING:
		delete = 1;
		remove = 1;
		break;
	case MPI3_EVENT_DEV_STAT_RC_INT_DEVICE_RESET_CMP:
	case MPI3_EVENT_DEV_STAT_RC_INT_IT_NEXUS_RESET_CMP:
		ublock = 1;
		break;
	default:
		break;
	}

	tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, dev_handle);
	if (!tgtdev)
		goto out;
	if (hide)
		tgtdev->is_hidden = hide;
	if (tgtdev->starget && tgtdev->starget->hostdata) {
		scsi_tgt_priv_data = (struct mpi3mr_stgt_priv_data *)
		    tgtdev->starget->hostdata;
		if (block)
			atomic_inc(&scsi_tgt_priv_data->block_io);
		if (delete)
			scsi_tgt_priv_data->dev_removed = 1;
		if (ublock)
			atomic_dec_if_positive(&scsi_tgt_priv_data->block_io);
	}
	if (remove)
		mpi3mr_dev_rmhs_send_tm(mrioc, dev_handle, NULL,
		    MPI3_CTRL_OP_REMOVE_DEVICE);
	if (hide)
		mpi3mr_dev_rmhs_send_tm(mrioc, dev_handle, NULL,
		    MPI3_CTRL_OP_HIDDEN_ACK);

out:
	if (tgtdev)
		mpi3mr_tgtdev_put(tgtdev);
}

/**
 * mpi3mr_preparereset_evt_th - Prepare for reset event tophalf
 * @mrioc: Adapter instance reference
 * @event_reply: event data
 *
 * Blocks and unblocks host level I/O based on the reason code
 *
 * Return: Nothing
 */
static void mpi3mr_preparereset_evt_th(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply)
{
	struct mpi3_event_data_prepare_for_reset *evtdata =
	    (struct mpi3_event_data_prepare_for_reset *)event_reply->event_data;

	if (evtdata->reason_code == MPI3_EVENT_PREPARE_RESET_RC_START) {
		dprint_event_th(mrioc,
		    "prepare for reset event top half with rc=start\n");
		if (mrioc->prepare_for_reset)
			return;
		mrioc->prepare_for_reset = 1;
		mrioc->prepare_for_reset_timeout_counter = 0;
	} else if (evtdata->reason_code == MPI3_EVENT_PREPARE_RESET_RC_ABORT) {
		dprint_event_th(mrioc,
		    "prepare for reset top half with rc=abort\n");
		mrioc->prepare_for_reset = 0;
		mrioc->prepare_for_reset_timeout_counter = 0;
	}
	if ((event_reply->msg_flags & MPI3_EVENT_NOTIFY_MSGFLAGS_ACK_MASK)
	    == MPI3_EVENT_NOTIFY_MSGFLAGS_ACK_REQUIRED)
		mpi3mr_send_event_ack(mrioc, event_reply->event, NULL,
		    le32_to_cpu(event_reply->event_context));
}

/**
 * mpi3mr_energypackchg_evt_th - Energy pack change evt tophalf
 * @mrioc: Adapter instance reference
 * @event_reply: event data
 *
 * Identifies the new shutdown timeout value and update.
 *
 * Return: Nothing
 */
static void mpi3mr_energypackchg_evt_th(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply)
{
	struct mpi3_event_data_energy_pack_change *evtdata =
	    (struct mpi3_event_data_energy_pack_change *)event_reply->event_data;
	u16 shutdown_timeout = le16_to_cpu(evtdata->shutdown_timeout);

	if (shutdown_timeout <= 0) {
		ioc_warn(mrioc,
		    "%s :Invalid Shutdown Timeout received = %d\n",
		    __func__, shutdown_timeout);
		return;
	}

	ioc_info(mrioc,
	    "%s :Previous Shutdown Timeout Value = %d New Shutdown Timeout Value = %d\n",
	    __func__, mrioc->facts.shutdown_timeout, shutdown_timeout);
	mrioc->facts.shutdown_timeout = shutdown_timeout;
}

/**
 * mpi3mr_cablemgmt_evt_th - Cable management event tophalf
 * @mrioc: Adapter instance reference
 * @event_reply: event data
 *
 * Displays Cable manegemt event details.
 *
 * Return: Nothing
 */
static void mpi3mr_cablemgmt_evt_th(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply)
{
	struct mpi3_event_data_cable_management *evtdata =
	    (struct mpi3_event_data_cable_management *)event_reply->event_data;

	switch (evtdata->status) {
	case MPI3_EVENT_CABLE_MGMT_STATUS_INSUFFICIENT_POWER:
	{
		ioc_info(mrioc, "An active cable with receptacle_id %d cannot be powered.\n"
		    "Devices connected to this cable are not detected.\n"
		    "This cable requires %d mW of power.\n",
		    evtdata->receptacle_id,
		    le32_to_cpu(evtdata->active_cable_power_requirement));
		break;
	}
	case MPI3_EVENT_CABLE_MGMT_STATUS_DEGRADED:
	{
		ioc_info(mrioc, "A cable with receptacle_id %d is not running at optimal speed\n",
		    evtdata->receptacle_id);
		break;
	}
	default:
		break;
	}
}

/**
 * mpi3mr_add_event_wait_for_device_refresh - Add Wait for Device Refresh Event
 * @mrioc: Adapter instance reference
 *
 * Add driver specific event to make sure that the driver won't process the
 * events until all the devices are refreshed during soft reset.
 *
 * Return: Nothing
 */
void mpi3mr_add_event_wait_for_device_refresh(struct mpi3mr_ioc *mrioc)
{
	struct mpi3mr_fwevt *fwevt = NULL;

	fwevt = mpi3mr_alloc_fwevt(0);
	if (!fwevt) {
		dprint_event_th(mrioc,
		    "failed to schedule bottom half handler for event(0x%02x)\n",
		    MPI3_EVENT_WAIT_FOR_DEVICES_TO_REFRESH);
		return;
	}
	fwevt->mrioc = mrioc;
	fwevt->event_id = MPI3_EVENT_WAIT_FOR_DEVICES_TO_REFRESH;
	fwevt->send_ack = 0;
	fwevt->process_evt = 1;
	fwevt->evt_ctx = 0;
	fwevt->event_data_size = 0;
	mpi3mr_fwevt_add_to_list(mrioc, fwevt);
}

/**
 * mpi3mr_os_handle_events - Firmware event handler
 * @mrioc: Adapter instance reference
 * @event_reply: event data
 *
 * Identify whteher the event has to handled and acknowledged
 * and either process the event in the tophalf and/or schedule a
 * bottom half through mpi3mr_fwevt_worker.
 *
 * Return: Nothing
 */
void mpi3mr_os_handle_events(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply)
{
	u16 evt_type, sz;
	struct mpi3mr_fwevt *fwevt = NULL;
	bool ack_req = 0, process_evt_bh = 0;

	if (mrioc->stop_drv_processing)
		return;

	if ((event_reply->msg_flags & MPI3_EVENT_NOTIFY_MSGFLAGS_ACK_MASK)
	    == MPI3_EVENT_NOTIFY_MSGFLAGS_ACK_REQUIRED)
		ack_req = 1;

	evt_type = event_reply->event;

	switch (evt_type) {
	case MPI3_EVENT_DEVICE_ADDED:
	{
		struct mpi3_device_page0 *dev_pg0 =
		    (struct mpi3_device_page0 *)event_reply->event_data;
		if (mpi3mr_create_tgtdev(mrioc, dev_pg0))
			ioc_err(mrioc,
			    "%s :Failed to add device in the device add event\n",
			    __func__);
		else
			process_evt_bh = 1;
		break;
	}
	case MPI3_EVENT_DEVICE_STATUS_CHANGE:
	{
		process_evt_bh = 1;
		mpi3mr_devstatuschg_evt_th(mrioc, event_reply);
		break;
	}
	case MPI3_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
	{
		process_evt_bh = 1;
		mpi3mr_sastopochg_evt_th(mrioc, event_reply);
		break;
	}
	case MPI3_EVENT_PCIE_TOPOLOGY_CHANGE_LIST:
	{
		process_evt_bh = 1;
		mpi3mr_pcietopochg_evt_th(mrioc, event_reply);
		break;
	}
	case MPI3_EVENT_PREPARE_FOR_RESET:
	{
		mpi3mr_preparereset_evt_th(mrioc, event_reply);
		ack_req = 0;
		break;
	}
	case MPI3_EVENT_DEVICE_INFO_CHANGED:
	case MPI3_EVENT_LOG_DATA:
	case MPI3_EVENT_ENCL_DEVICE_STATUS_CHANGE:
	case MPI3_EVENT_ENCL_DEVICE_ADDED:
	{
		process_evt_bh = 1;
		break;
	}
	case MPI3_EVENT_ENERGY_PACK_CHANGE:
	{
		mpi3mr_energypackchg_evt_th(mrioc, event_reply);
		break;
	}
	case MPI3_EVENT_CABLE_MGMT:
	{
		mpi3mr_cablemgmt_evt_th(mrioc, event_reply);
		break;
	}
	case MPI3_EVENT_SAS_DISCOVERY:
	case MPI3_EVENT_SAS_DEVICE_DISCOVERY_ERROR:
	case MPI3_EVENT_SAS_BROADCAST_PRIMITIVE:
	case MPI3_EVENT_PCIE_ENUMERATION:
		break;
	default:
		ioc_info(mrioc, "%s :event 0x%02x is not handled\n",
		    __func__, evt_type);
		break;
	}
	if (process_evt_bh || ack_req) {
		sz = event_reply->event_data_length * 4;
		fwevt = mpi3mr_alloc_fwevt(sz);
		if (!fwevt) {
			ioc_info(mrioc, "%s :failure at %s:%d/%s()!\n",
			    __func__, __FILE__, __LINE__, __func__);
			return;
		}

		memcpy(fwevt->event_data, event_reply->event_data, sz);
		fwevt->mrioc = mrioc;
		fwevt->event_id = evt_type;
		fwevt->send_ack = ack_req;
		fwevt->process_evt = process_evt_bh;
		fwevt->evt_ctx = le32_to_cpu(event_reply->event_context);
		mpi3mr_fwevt_add_to_list(mrioc, fwevt);
	}
}

/**
 * mpi3mr_setup_eedp - Setup EEDP information in MPI3 SCSI IO
 * @mrioc: Adapter instance reference
 * @scmd: SCSI command reference
 * @scsiio_req: MPI3 SCSI IO request
 *
 * Identifies the protection information flags from the SCSI
 * command and set appropriate flags in the MPI3 SCSI IO
 * request.
 *
 * Return: Nothing
 */
static void mpi3mr_setup_eedp(struct mpi3mr_ioc *mrioc,
	struct scsi_cmnd *scmd, struct mpi3_scsi_io_request *scsiio_req)
{
	u16 eedp_flags = 0;
	unsigned char prot_op = scsi_get_prot_op(scmd);

	switch (prot_op) {
	case SCSI_PROT_NORMAL:
		return;
	case SCSI_PROT_READ_STRIP:
		eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_CHECK_REMOVE;
		break;
	case SCSI_PROT_WRITE_INSERT:
		eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_INSERT;
		break;
	case SCSI_PROT_READ_INSERT:
		eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_INSERT;
		scsiio_req->msg_flags |= MPI3_SCSIIO_MSGFLAGS_METASGL_VALID;
		break;
	case SCSI_PROT_WRITE_STRIP:
		eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_CHECK_REMOVE;
		scsiio_req->msg_flags |= MPI3_SCSIIO_MSGFLAGS_METASGL_VALID;
		break;
	case SCSI_PROT_READ_PASS:
		eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_CHECK;
		scsiio_req->msg_flags |= MPI3_SCSIIO_MSGFLAGS_METASGL_VALID;
		break;
	case SCSI_PROT_WRITE_PASS:
		if (scmd->prot_flags & SCSI_PROT_IP_CHECKSUM) {
			eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_CHECK_REGEN;
			scsiio_req->sgl[0].eedp.application_tag_translation_mask =
			    0xffff;
		} else
			eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_CHECK;

		scsiio_req->msg_flags |= MPI3_SCSIIO_MSGFLAGS_METASGL_VALID;
		break;
	default:
		return;
	}

	if (scmd->prot_flags & SCSI_PROT_GUARD_CHECK)
		eedp_flags |= MPI3_EEDPFLAGS_CHK_GUARD;

	if (scmd->prot_flags & SCSI_PROT_IP_CHECKSUM)
		eedp_flags |= MPI3_EEDPFLAGS_HOST_GUARD_IP_CHKSUM;

	if (scmd->prot_flags & SCSI_PROT_REF_CHECK) {
		eedp_flags |= MPI3_EEDPFLAGS_CHK_REF_TAG |
			MPI3_EEDPFLAGS_INCR_PRI_REF_TAG;
		scsiio_req->cdb.eedp32.primary_reference_tag =
			cpu_to_be32(scsi_prot_ref_tag(scmd));
	}

	if (scmd->prot_flags & SCSI_PROT_REF_INCREMENT)
		eedp_flags |= MPI3_EEDPFLAGS_INCR_PRI_REF_TAG;

	eedp_flags |= MPI3_EEDPFLAGS_ESC_MODE_APPTAG_DISABLE;

	switch (scsi_prot_interval(scmd)) {
	case 512:
		scsiio_req->sgl[0].eedp.user_data_size = MPI3_EEDP_UDS_512;
		break;
	case 520:
		scsiio_req->sgl[0].eedp.user_data_size = MPI3_EEDP_UDS_520;
		break;
	case 4080:
		scsiio_req->sgl[0].eedp.user_data_size = MPI3_EEDP_UDS_4080;
		break;
	case 4088:
		scsiio_req->sgl[0].eedp.user_data_size = MPI3_EEDP_UDS_4088;
		break;
	case 4096:
		scsiio_req->sgl[0].eedp.user_data_size = MPI3_EEDP_UDS_4096;
		break;
	case 4104:
		scsiio_req->sgl[0].eedp.user_data_size = MPI3_EEDP_UDS_4104;
		break;
	case 4160:
		scsiio_req->sgl[0].eedp.user_data_size = MPI3_EEDP_UDS_4160;
		break;
	default:
		break;
	}

	scsiio_req->sgl[0].eedp.eedp_flags = cpu_to_le16(eedp_flags);
	scsiio_req->sgl[0].eedp.flags = MPI3_SGE_FLAGS_ELEMENT_TYPE_EXTENDED;
}

/**
 * mpi3mr_build_sense_buffer - Map sense information
 * @desc: Sense type
 * @buf: Sense buffer to populate
 * @key: Sense key
 * @asc: Additional sense code
 * @ascq: Additional sense code qualifier
 *
 * Maps the given sense information into either descriptor or
 * fixed format sense data.
 *
 * Return: Nothing
 */
static inline void mpi3mr_build_sense_buffer(int desc, u8 *buf, u8 key,
	u8 asc, u8 ascq)
{
	if (desc) {
		buf[0] = 0x72;	/* descriptor, current */
		buf[1] = key;
		buf[2] = asc;
		buf[3] = ascq;
		buf[7] = 0;
	} else {
		buf[0] = 0x70;	/* fixed, current */
		buf[2] = key;
		buf[7] = 0xa;
		buf[12] = asc;
		buf[13] = ascq;
	}
}

/**
 * mpi3mr_map_eedp_error - Map EEDP errors from IOC status
 * @scmd: SCSI command reference
 * @ioc_status: status of MPI3 request
 *
 * Maps the EEDP error status of the SCSI IO request to sense
 * data.
 *
 * Return: Nothing
 */
static void mpi3mr_map_eedp_error(struct scsi_cmnd *scmd,
	u16 ioc_status)
{
	u8 ascq = 0;

	switch (ioc_status) {
	case MPI3_IOCSTATUS_EEDP_GUARD_ERROR:
		ascq = 0x01;
		break;
	case MPI3_IOCSTATUS_EEDP_APP_TAG_ERROR:
		ascq = 0x02;
		break;
	case MPI3_IOCSTATUS_EEDP_REF_TAG_ERROR:
		ascq = 0x03;
		break;
	default:
		ascq = 0x00;
		break;
	}

	mpi3mr_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST,
	    0x10, ascq);
	scmd->result = (DID_ABORT << 16) | SAM_STAT_CHECK_CONDITION;
}

/**
 * mpi3mr_process_op_reply_desc - reply descriptor handler
 * @mrioc: Adapter instance reference
 * @reply_desc: Operational reply descriptor
 * @reply_dma: place holder for reply DMA address
 * @qidx: Operational queue index
 *
 * Process the operational reply descriptor and identifies the
 * descriptor type. Based on the descriptor map the MPI3 request
 * status to a SCSI command status and calls scsi_done call
 * back.
 *
 * Return: Nothing
 */
void mpi3mr_process_op_reply_desc(struct mpi3mr_ioc *mrioc,
	struct mpi3_default_reply_descriptor *reply_desc, u64 *reply_dma, u16 qidx)
{
	u16 reply_desc_type, host_tag = 0;
	u16 ioc_status = MPI3_IOCSTATUS_SUCCESS;
	u32 ioc_loginfo = 0;
	struct mpi3_status_reply_descriptor *status_desc = NULL;
	struct mpi3_address_reply_descriptor *addr_desc = NULL;
	struct mpi3_success_reply_descriptor *success_desc = NULL;
	struct mpi3_scsi_io_reply *scsi_reply = NULL;
	struct scsi_cmnd *scmd = NULL;
	struct scmd_priv *priv = NULL;
	u8 *sense_buf = NULL;
	u8 scsi_state = 0, scsi_status = 0, sense_state = 0;
	u32 xfer_count = 0, sense_count = 0, resp_data = 0;
	u16 dev_handle = 0xFFFF;
	struct scsi_sense_hdr sshdr;
	struct mpi3mr_stgt_priv_data *stgt_priv_data = NULL;
	struct mpi3mr_sdev_priv_data *sdev_priv_data = NULL;
	u32 ioc_pend_data_len = 0, tg_pend_data_len = 0, data_len_blks = 0;
	struct mpi3mr_throttle_group_info *tg = NULL;
	u8 throttle_enabled_dev = 0;

	*reply_dma = 0;
	reply_desc_type = le16_to_cpu(reply_desc->reply_flags) &
	    MPI3_REPLY_DESCRIPT_FLAGS_TYPE_MASK;
	switch (reply_desc_type) {
	case MPI3_REPLY_DESCRIPT_FLAGS_TYPE_STATUS:
		status_desc = (struct mpi3_status_reply_descriptor *)reply_desc;
		host_tag = le16_to_cpu(status_desc->host_tag);
		ioc_status = le16_to_cpu(status_desc->ioc_status);
		if (ioc_status &
		    MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_LOGINFOAVAIL)
			ioc_loginfo = le32_to_cpu(status_desc->ioc_log_info);
		ioc_status &= MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_STATUS_MASK;
		break;
	case MPI3_REPLY_DESCRIPT_FLAGS_TYPE_ADDRESS_REPLY:
		addr_desc = (struct mpi3_address_reply_descriptor *)reply_desc;
		*reply_dma = le64_to_cpu(addr_desc->reply_frame_address);
		scsi_reply = mpi3mr_get_reply_virt_addr(mrioc,
		    *reply_dma);
		if (!scsi_reply) {
			panic("%s: scsi_reply is NULL, this shouldn't happen\n",
			    mrioc->name);
			goto out;
		}
		host_tag = le16_to_cpu(scsi_reply->host_tag);
		ioc_status = le16_to_cpu(scsi_reply->ioc_status);
		scsi_status = scsi_reply->scsi_status;
		scsi_state = scsi_reply->scsi_state;
		dev_handle = le16_to_cpu(scsi_reply->dev_handle);
		sense_state = (scsi_state & MPI3_SCSI_STATE_SENSE_MASK);
		xfer_count = le32_to_cpu(scsi_reply->transfer_count);
		sense_count = le32_to_cpu(scsi_reply->sense_count);
		resp_data = le32_to_cpu(scsi_reply->response_data);
		sense_buf = mpi3mr_get_sensebuf_virt_addr(mrioc,
		    le64_to_cpu(scsi_reply->sense_data_buffer_address));
		if (ioc_status &
		    MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_LOGINFOAVAIL)
			ioc_loginfo = le32_to_cpu(scsi_reply->ioc_log_info);
		ioc_status &= MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_STATUS_MASK;
		if (sense_state == MPI3_SCSI_STATE_SENSE_BUFF_Q_EMPTY)
			panic("%s: Ran out of sense buffers\n", mrioc->name);
		break;
	case MPI3_REPLY_DESCRIPT_FLAGS_TYPE_SUCCESS:
		success_desc = (struct mpi3_success_reply_descriptor *)reply_desc;
		host_tag = le16_to_cpu(success_desc->host_tag);
		break;
	default:
		break;
	}
	scmd = mpi3mr_scmd_from_host_tag(mrioc, host_tag, qidx);
	if (!scmd) {
		panic("%s: Cannot Identify scmd for host_tag 0x%x\n",
		    mrioc->name, host_tag);
		goto out;
	}
	priv = scsi_cmd_priv(scmd);

	data_len_blks = scsi_bufflen(scmd) >> 9;
	sdev_priv_data = scmd->device->hostdata;
	if (sdev_priv_data) {
		stgt_priv_data = sdev_priv_data->tgt_priv_data;
		if (stgt_priv_data) {
			tg = stgt_priv_data->throttle_group;
			throttle_enabled_dev =
			    stgt_priv_data->io_throttle_enabled;
		}
	}
	if (unlikely((data_len_blks >= mrioc->io_throttle_data_length) &&
	    throttle_enabled_dev)) {
		ioc_pend_data_len = atomic_sub_return(data_len_blks,
		    &mrioc->pend_large_data_sz);
		if (tg) {
			tg_pend_data_len = atomic_sub_return(data_len_blks,
			    &tg->pend_large_data_sz);
			if (tg->io_divert  && ((ioc_pend_data_len <=
			    mrioc->io_throttle_low) &&
			    (tg_pend_data_len <= tg->low))) {
				tg->io_divert = 0;
				mpi3mr_set_io_divert_for_all_vd_in_tg(
				    mrioc, tg, 0);
			}
		} else {
			if (ioc_pend_data_len <= mrioc->io_throttle_low)
				stgt_priv_data->io_divert = 0;
		}
	} else if (unlikely((stgt_priv_data && stgt_priv_data->io_divert))) {
		ioc_pend_data_len = atomic_read(&mrioc->pend_large_data_sz);
		if (!tg) {
			if (ioc_pend_data_len <= mrioc->io_throttle_low)
				stgt_priv_data->io_divert = 0;

		} else if (ioc_pend_data_len <= mrioc->io_throttle_low) {
			tg_pend_data_len = atomic_read(&tg->pend_large_data_sz);
			if (tg->io_divert  && (tg_pend_data_len <= tg->low)) {
				tg->io_divert = 0;
				mpi3mr_set_io_divert_for_all_vd_in_tg(
				    mrioc, tg, 0);
			}
		}
	}

	if (success_desc) {
		scmd->result = DID_OK << 16;
		goto out_success;
	}

	scsi_set_resid(scmd, scsi_bufflen(scmd) - xfer_count);
	if (ioc_status == MPI3_IOCSTATUS_SCSI_DATA_UNDERRUN &&
	    xfer_count == 0 && (scsi_status == MPI3_SCSI_STATUS_BUSY ||
	    scsi_status == MPI3_SCSI_STATUS_RESERVATION_CONFLICT ||
	    scsi_status == MPI3_SCSI_STATUS_TASK_SET_FULL))
		ioc_status = MPI3_IOCSTATUS_SUCCESS;

	if ((sense_state == MPI3_SCSI_STATE_SENSE_VALID) && sense_count &&
	    sense_buf) {
		u32 sz = min_t(u32, SCSI_SENSE_BUFFERSIZE, sense_count);

		memcpy(scmd->sense_buffer, sense_buf, sz);
	}

	switch (ioc_status) {
	case MPI3_IOCSTATUS_BUSY:
	case MPI3_IOCSTATUS_INSUFFICIENT_RESOURCES:
		scmd->result = SAM_STAT_BUSY;
		break;
	case MPI3_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		scmd->result = DID_NO_CONNECT << 16;
		break;
	case MPI3_IOCSTATUS_SCSI_IOC_TERMINATED:
		scmd->result = DID_SOFT_ERROR << 16;
		break;
	case MPI3_IOCSTATUS_SCSI_TASK_TERMINATED:
	case MPI3_IOCSTATUS_SCSI_EXT_TERMINATED:
		scmd->result = DID_RESET << 16;
		break;
	case MPI3_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
		if ((xfer_count == 0) || (scmd->underflow > xfer_count))
			scmd->result = DID_SOFT_ERROR << 16;
		else
			scmd->result = (DID_OK << 16) | scsi_status;
		break;
	case MPI3_IOCSTATUS_SCSI_DATA_UNDERRUN:
		scmd->result = (DID_OK << 16) | scsi_status;
		if (sense_state == MPI3_SCSI_STATE_SENSE_VALID)
			break;
		if (xfer_count < scmd->underflow) {
			if (scsi_status == SAM_STAT_BUSY)
				scmd->result = SAM_STAT_BUSY;
			else
				scmd->result = DID_SOFT_ERROR << 16;
		} else if ((scsi_state & (MPI3_SCSI_STATE_NO_SCSI_STATUS)) ||
		    (sense_state != MPI3_SCSI_STATE_SENSE_NOT_AVAILABLE))
			scmd->result = DID_SOFT_ERROR << 16;
		else if (scsi_state & MPI3_SCSI_STATE_TERMINATED)
			scmd->result = DID_RESET << 16;
		break;
	case MPI3_IOCSTATUS_SCSI_DATA_OVERRUN:
		scsi_set_resid(scmd, 0);
		fallthrough;
	case MPI3_IOCSTATUS_SCSI_RECOVERED_ERROR:
	case MPI3_IOCSTATUS_SUCCESS:
		scmd->result = (DID_OK << 16) | scsi_status;
		if ((scsi_state & (MPI3_SCSI_STATE_NO_SCSI_STATUS)) ||
		    (sense_state == MPI3_SCSI_STATE_SENSE_FAILED) ||
			(sense_state == MPI3_SCSI_STATE_SENSE_BUFF_Q_EMPTY))
			scmd->result = DID_SOFT_ERROR << 16;
		else if (scsi_state & MPI3_SCSI_STATE_TERMINATED)
			scmd->result = DID_RESET << 16;
		break;
	case MPI3_IOCSTATUS_EEDP_GUARD_ERROR:
	case MPI3_IOCSTATUS_EEDP_REF_TAG_ERROR:
	case MPI3_IOCSTATUS_EEDP_APP_TAG_ERROR:
		mpi3mr_map_eedp_error(scmd, ioc_status);
		break;
	case MPI3_IOCSTATUS_SCSI_PROTOCOL_ERROR:
	case MPI3_IOCSTATUS_INVALID_FUNCTION:
	case MPI3_IOCSTATUS_INVALID_SGL:
	case MPI3_IOCSTATUS_INTERNAL_ERROR:
	case MPI3_IOCSTATUS_INVALID_FIELD:
	case MPI3_IOCSTATUS_INVALID_STATE:
	case MPI3_IOCSTATUS_SCSI_IO_DATA_ERROR:
	case MPI3_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
	case MPI3_IOCSTATUS_INSUFFICIENT_POWER:
	default:
		scmd->result = DID_SOFT_ERROR << 16;
		break;
	}

	if (scmd->result != (DID_OK << 16) && (scmd->cmnd[0] != ATA_12) &&
	    (scmd->cmnd[0] != ATA_16) &&
	    mrioc->logging_level & MPI3_DEBUG_SCSI_ERROR) {
		ioc_info(mrioc, "%s :scmd->result 0x%x\n", __func__,
		    scmd->result);
		scsi_print_command(scmd);
		ioc_info(mrioc,
		    "%s :Command issued to handle 0x%02x returned with error 0x%04x loginfo 0x%08x, qid %d\n",
		    __func__, dev_handle, ioc_status, ioc_loginfo,
		    priv->req_q_idx + 1);
		ioc_info(mrioc,
		    " host_tag %d scsi_state 0x%02x scsi_status 0x%02x, xfer_cnt %d resp_data 0x%x\n",
		    host_tag, scsi_state, scsi_status, xfer_count, resp_data);
		if (sense_buf) {
			scsi_normalize_sense(sense_buf, sense_count, &sshdr);
			ioc_info(mrioc,
			    "%s :sense_count 0x%x, sense_key 0x%x ASC 0x%x, ASCQ 0x%x\n",
			    __func__, sense_count, sshdr.sense_key,
			    sshdr.asc, sshdr.ascq);
		}
	}
out_success:
	if (priv->meta_sg_valid) {
		dma_unmap_sg(&mrioc->pdev->dev, scsi_prot_sglist(scmd),
		    scsi_prot_sg_count(scmd), scmd->sc_data_direction);
	}
	mpi3mr_clear_scmd_priv(mrioc, scmd);
	scsi_dma_unmap(scmd);
	scsi_done(scmd);
out:
	if (sense_buf)
		mpi3mr_repost_sense_buf(mrioc,
		    le64_to_cpu(scsi_reply->sense_data_buffer_address));
}

/**
 * mpi3mr_get_chain_idx - get free chain buffer index
 * @mrioc: Adapter instance reference
 *
 * Try to get a free chain buffer index from the free pool.
 *
 * Return: -1 on failure or the free chain buffer index
 */
static int mpi3mr_get_chain_idx(struct mpi3mr_ioc *mrioc)
{
	u8 retry_count = 5;
	int cmd_idx = -1;

	do {
		spin_lock(&mrioc->chain_buf_lock);
		cmd_idx = find_first_zero_bit(mrioc->chain_bitmap,
		    mrioc->chain_buf_count);
		if (cmd_idx < mrioc->chain_buf_count) {
			set_bit(cmd_idx, mrioc->chain_bitmap);
			spin_unlock(&mrioc->chain_buf_lock);
			break;
		}
		spin_unlock(&mrioc->chain_buf_lock);
		cmd_idx = -1;
	} while (retry_count--);
	return cmd_idx;
}

/**
 * mpi3mr_prepare_sg_scmd - build scatter gather list
 * @mrioc: Adapter instance reference
 * @scmd: SCSI command reference
 * @scsiio_req: MPI3 SCSI IO request
 *
 * This function maps SCSI command's data and protection SGEs to
 * MPI request SGEs. If required additional 4K chain buffer is
 * used to send the SGEs.
 *
 * Return: 0 on success, -ENOMEM on dma_map_sg failure
 */
static int mpi3mr_prepare_sg_scmd(struct mpi3mr_ioc *mrioc,
	struct scsi_cmnd *scmd, struct mpi3_scsi_io_request *scsiio_req)
{
	dma_addr_t chain_dma;
	struct scatterlist *sg_scmd;
	void *sg_local, *chain;
	u32 chain_length;
	int sges_left, chain_idx;
	u32 sges_in_segment;
	u8 simple_sgl_flags;
	u8 simple_sgl_flags_last;
	u8 last_chain_sgl_flags;
	struct chain_element *chain_req;
	struct scmd_priv *priv = NULL;
	u32 meta_sg = le32_to_cpu(scsiio_req->flags) &
	    MPI3_SCSIIO_FLAGS_DMAOPERATION_HOST_PI;

	priv = scsi_cmd_priv(scmd);

	simple_sgl_flags = MPI3_SGE_FLAGS_ELEMENT_TYPE_SIMPLE |
	    MPI3_SGE_FLAGS_DLAS_SYSTEM;
	simple_sgl_flags_last = simple_sgl_flags |
	    MPI3_SGE_FLAGS_END_OF_LIST;
	last_chain_sgl_flags = MPI3_SGE_FLAGS_ELEMENT_TYPE_LAST_CHAIN |
	    MPI3_SGE_FLAGS_DLAS_SYSTEM;

	if (meta_sg)
		sg_local = &scsiio_req->sgl[MPI3_SCSIIO_METASGL_INDEX];
	else
		sg_local = &scsiio_req->sgl;

	if (!scsiio_req->data_length && !meta_sg) {
		mpi3mr_build_zero_len_sge(sg_local);
		return 0;
	}

	if (meta_sg) {
		sg_scmd = scsi_prot_sglist(scmd);
		sges_left = dma_map_sg(&mrioc->pdev->dev,
		    scsi_prot_sglist(scmd),
		    scsi_prot_sg_count(scmd),
		    scmd->sc_data_direction);
		priv->meta_sg_valid = 1; /* To unmap meta sg DMA */
	} else {
		sg_scmd = scsi_sglist(scmd);
		sges_left = scsi_dma_map(scmd);
	}

	if (sges_left < 0) {
		sdev_printk(KERN_ERR, scmd->device,
		    "scsi_dma_map failed: request for %d bytes!\n",
		    scsi_bufflen(scmd));
		return -ENOMEM;
	}
	if (sges_left > MPI3MR_SG_DEPTH) {
		sdev_printk(KERN_ERR, scmd->device,
		    "scsi_dma_map returned unsupported sge count %d!\n",
		    sges_left);
		return -ENOMEM;
	}

	sges_in_segment = (mrioc->facts.op_req_sz -
	    offsetof(struct mpi3_scsi_io_request, sgl)) / sizeof(struct mpi3_sge_common);

	if (scsiio_req->sgl[0].eedp.flags ==
	    MPI3_SGE_FLAGS_ELEMENT_TYPE_EXTENDED && !meta_sg) {
		sg_local += sizeof(struct mpi3_sge_common);
		sges_in_segment--;
		/* Reserve 1st segment (scsiio_req->sgl[0]) for eedp */
	}

	if (scsiio_req->msg_flags ==
	    MPI3_SCSIIO_MSGFLAGS_METASGL_VALID && !meta_sg) {
		sges_in_segment--;
		/* Reserve last segment (scsiio_req->sgl[3]) for meta sg */
	}

	if (meta_sg)
		sges_in_segment = 1;

	if (sges_left <= sges_in_segment)
		goto fill_in_last_segment;

	/* fill in main message segment when there is a chain following */
	while (sges_in_segment > 1) {
		mpi3mr_add_sg_single(sg_local, simple_sgl_flags,
		    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += sizeof(struct mpi3_sge_common);
		sges_left--;
		sges_in_segment--;
	}

	chain_idx = mpi3mr_get_chain_idx(mrioc);
	if (chain_idx < 0)
		return -1;
	chain_req = &mrioc->chain_sgl_list[chain_idx];
	if (meta_sg)
		priv->meta_chain_idx = chain_idx;
	else
		priv->chain_idx = chain_idx;

	chain = chain_req->addr;
	chain_dma = chain_req->dma_addr;
	sges_in_segment = sges_left;
	chain_length = sges_in_segment * sizeof(struct mpi3_sge_common);

	mpi3mr_add_sg_single(sg_local, last_chain_sgl_flags,
	    chain_length, chain_dma);

	sg_local = chain;

fill_in_last_segment:
	while (sges_left > 0) {
		if (sges_left == 1)
			mpi3mr_add_sg_single(sg_local,
			    simple_sgl_flags_last, sg_dma_len(sg_scmd),
			    sg_dma_address(sg_scmd));
		else
			mpi3mr_add_sg_single(sg_local, simple_sgl_flags,
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += sizeof(struct mpi3_sge_common);
		sges_left--;
	}

	return 0;
}

/**
 * mpi3mr_build_sg_scmd - build scatter gather list for SCSI IO
 * @mrioc: Adapter instance reference
 * @scmd: SCSI command reference
 * @scsiio_req: MPI3 SCSI IO request
 *
 * This function calls mpi3mr_prepare_sg_scmd for constructing
 * both data SGEs and protection information SGEs in the MPI
 * format from the SCSI Command as appropriate .
 *
 * Return: return value of mpi3mr_prepare_sg_scmd.
 */
static int mpi3mr_build_sg_scmd(struct mpi3mr_ioc *mrioc,
	struct scsi_cmnd *scmd, struct mpi3_scsi_io_request *scsiio_req)
{
	int ret;

	ret = mpi3mr_prepare_sg_scmd(mrioc, scmd, scsiio_req);
	if (ret)
		return ret;

	if (scsiio_req->msg_flags == MPI3_SCSIIO_MSGFLAGS_METASGL_VALID) {
		/* There is a valid meta sg */
		scsiio_req->flags |=
		    cpu_to_le32(MPI3_SCSIIO_FLAGS_DMAOPERATION_HOST_PI);
		ret = mpi3mr_prepare_sg_scmd(mrioc, scmd, scsiio_req);
	}

	return ret;
}

/**
 * mpi3mr_tm_response_name -  get TM response as a string
 * @resp_code: TM response code
 *
 * Convert known task management response code as a readable
 * string.
 *
 * Return: response code string.
 */
static const char *mpi3mr_tm_response_name(u8 resp_code)
{
	char *desc;

	switch (resp_code) {
	case MPI3_SCSITASKMGMT_RSPCODE_TM_COMPLETE:
		desc = "task management request completed";
		break;
	case MPI3_SCSITASKMGMT_RSPCODE_INVALID_FRAME:
		desc = "invalid frame";
		break;
	case MPI3_SCSITASKMGMT_RSPCODE_TM_FUNCTION_NOT_SUPPORTED:
		desc = "task management request not supported";
		break;
	case MPI3_SCSITASKMGMT_RSPCODE_TM_FAILED:
		desc = "task management request failed";
		break;
	case MPI3_SCSITASKMGMT_RSPCODE_TM_SUCCEEDED:
		desc = "task management request succeeded";
		break;
	case MPI3_SCSITASKMGMT_RSPCODE_TM_INVALID_LUN:
		desc = "invalid LUN";
		break;
	case MPI3_SCSITASKMGMT_RSPCODE_TM_OVERLAPPED_TAG:
		desc = "overlapped tag attempted";
		break;
	case MPI3_SCSITASKMGMT_RSPCODE_IO_QUEUED_ON_IOC:
		desc = "task queued, however not sent to target";
		break;
	case MPI3_SCSITASKMGMT_RSPCODE_TM_NVME_DENIED:
		desc = "task management request denied by NVMe device";
		break;
	default:
		desc = "unknown";
		break;
	}

	return desc;
}

inline void mpi3mr_poll_pend_io_completions(struct mpi3mr_ioc *mrioc)
{
	int i;
	int num_of_reply_queues =
	    mrioc->num_op_reply_q + mrioc->op_reply_q_offset;

	for (i = mrioc->op_reply_q_offset; i < num_of_reply_queues; i++)
		mpi3mr_process_op_reply_q(mrioc,
		    mrioc->intr_info[i].op_reply_q);
}

/**
 * mpi3mr_issue_tm - Issue Task Management request
 * @mrioc: Adapter instance reference
 * @tm_type: Task Management type
 * @handle: Device handle
 * @lun: lun ID
 * @htag: Host tag of the TM request
 * @timeout: TM timeout value
 * @drv_cmd: Internal command tracker
 * @resp_code: Response code place holder
 * @scmd: SCSI command
 *
 * Issues a Task Management Request to the controller for a
 * specified target, lun and command and wait for its completion
 * and check TM response. Recover the TM if it timed out by
 * issuing controller reset.
 *
 * Return: 0 on success, non-zero on errors
 */
int mpi3mr_issue_tm(struct mpi3mr_ioc *mrioc, u8 tm_type,
	u16 handle, uint lun, u16 htag, ulong timeout,
	struct mpi3mr_drv_cmd *drv_cmd,
	u8 *resp_code, struct scsi_cmnd *scmd)
{
	struct mpi3_scsi_task_mgmt_request tm_req;
	struct mpi3_scsi_task_mgmt_reply *tm_reply = NULL;
	int retval = 0;
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data = NULL;
	struct scmd_priv *cmd_priv = NULL;
	struct scsi_device *sdev = NULL;
	struct mpi3mr_sdev_priv_data *sdev_priv_data = NULL;

	ioc_info(mrioc, "%s :Issue TM: TM type (0x%x) for devhandle 0x%04x\n",
	     __func__, tm_type, handle);
	if (mrioc->unrecoverable) {
		retval = -1;
		ioc_err(mrioc, "%s :Issue TM: Unrecoverable controller\n",
		    __func__);
		goto out;
	}

	memset(&tm_req, 0, sizeof(tm_req));
	mutex_lock(&drv_cmd->mutex);
	if (drv_cmd->state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "%s :Issue TM: Command is in use\n", __func__);
		mutex_unlock(&drv_cmd->mutex);
		goto out;
	}
	if (mrioc->reset_in_progress) {
		retval = -1;
		ioc_err(mrioc, "%s :Issue TM: Reset in progress\n", __func__);
		mutex_unlock(&drv_cmd->mutex);
		goto out;
	}

	drv_cmd->state = MPI3MR_CMD_PENDING;
	drv_cmd->is_waiting = 1;
	drv_cmd->callback = NULL;
	tm_req.dev_handle = cpu_to_le16(handle);
	tm_req.task_type = tm_type;
	tm_req.host_tag = cpu_to_le16(htag);

	int_to_scsilun(lun, (struct scsi_lun *)tm_req.lun);
	tm_req.function = MPI3_FUNCTION_SCSI_TASK_MGMT;

	tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, handle);

	if (scmd) {
		sdev = scmd->device;
		sdev_priv_data = sdev->hostdata;
		scsi_tgt_priv_data = ((sdev_priv_data) ?
		    sdev_priv_data->tgt_priv_data : NULL);
	} else {
		if (tgtdev && tgtdev->starget && tgtdev->starget->hostdata)
			scsi_tgt_priv_data = (struct mpi3mr_stgt_priv_data *)
			    tgtdev->starget->hostdata;
	}

	if (scsi_tgt_priv_data)
		atomic_inc(&scsi_tgt_priv_data->block_io);

	if (tgtdev && (tgtdev->dev_type == MPI3_DEVICE_DEVFORM_PCIE)) {
		if (cmd_priv && tgtdev->dev_spec.pcie_inf.abort_to)
			timeout = tgtdev->dev_spec.pcie_inf.abort_to;
		else if (!cmd_priv && tgtdev->dev_spec.pcie_inf.reset_to)
			timeout = tgtdev->dev_spec.pcie_inf.reset_to;
	}

	init_completion(&drv_cmd->done);
	retval = mpi3mr_admin_request_post(mrioc, &tm_req, sizeof(tm_req), 1);
	if (retval) {
		ioc_err(mrioc, "%s :Issue TM: Admin Post failed\n", __func__);
		goto out_unlock;
	}
	wait_for_completion_timeout(&drv_cmd->done, (timeout * HZ));

	if (!(drv_cmd->state & MPI3MR_CMD_COMPLETE)) {
		drv_cmd->is_waiting = 0;
		retval = -1;
		if (!(drv_cmd->state & MPI3MR_CMD_RESET)) {
			dprint_tm(mrioc,
			    "task management request timed out after %ld seconds\n",
			    timeout);
			if (mrioc->logging_level & MPI3_DEBUG_TM)
				dprint_dump_req(&tm_req, sizeof(tm_req)/4);
			mpi3mr_soft_reset_handler(mrioc,
			    MPI3MR_RESET_FROM_TM_TIMEOUT, 1);
		}
		goto out_unlock;
	}

	if (!(drv_cmd->state & MPI3MR_CMD_REPLY_VALID)) {
		dprint_tm(mrioc, "invalid task management reply message\n");
		retval = -1;
		goto out_unlock;
	}

	tm_reply = (struct mpi3_scsi_task_mgmt_reply *)drv_cmd->reply;

	switch (drv_cmd->ioc_status) {
	case MPI3_IOCSTATUS_SUCCESS:
		*resp_code = le32_to_cpu(tm_reply->response_data) &
			MPI3MR_RI_MASK_RESPCODE;
		break;
	case MPI3_IOCSTATUS_SCSI_IOC_TERMINATED:
		*resp_code = MPI3_SCSITASKMGMT_RSPCODE_TM_COMPLETE;
		break;
	default:
		dprint_tm(mrioc,
		    "task management request to handle(0x%04x) is failed with ioc_status(0x%04x) log_info(0x%08x)\n",
		    handle, drv_cmd->ioc_status, drv_cmd->ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}

	switch (*resp_code) {
	case MPI3_SCSITASKMGMT_RSPCODE_TM_SUCCEEDED:
	case MPI3_SCSITASKMGMT_RSPCODE_TM_COMPLETE:
		break;
	case MPI3_SCSITASKMGMT_RSPCODE_IO_QUEUED_ON_IOC:
		if (tm_type != MPI3_SCSITASKMGMT_TASKTYPE_QUERY_TASK)
			retval = -1;
		break;
	default:
		retval = -1;
		break;
	}

	dprint_tm(mrioc,
	    "task management request type(%d) completed for handle(0x%04x) with ioc_status(0x%04x), log_info(0x%08x), termination_count(%d), response:%s(0x%x)\n",
	    tm_type, handle, drv_cmd->ioc_status, drv_cmd->ioc_loginfo,
	    le32_to_cpu(tm_reply->termination_count),
	    mpi3mr_tm_response_name(*resp_code), *resp_code);

	if (!retval) {
		mpi3mr_ioc_disable_intr(mrioc);
		mpi3mr_poll_pend_io_completions(mrioc);
		mpi3mr_ioc_enable_intr(mrioc);
		mpi3mr_poll_pend_io_completions(mrioc);
	}
	switch (tm_type) {
	case MPI3_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
		if (!scsi_tgt_priv_data)
			break;
		scsi_tgt_priv_data->pend_count = 0;
		blk_mq_tagset_busy_iter(&mrioc->shost->tag_set,
		    mpi3mr_count_tgt_pending,
		    (void *)scsi_tgt_priv_data->starget);
		break;
	case MPI3_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
		if (!sdev_priv_data)
			break;
		sdev_priv_data->pend_count = 0;
		blk_mq_tagset_busy_iter(&mrioc->shost->tag_set,
		    mpi3mr_count_dev_pending, (void *)sdev);
		break;
	default:
		break;
	}

out_unlock:
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&drv_cmd->mutex);
	if (scsi_tgt_priv_data)
		atomic_dec_if_positive(&scsi_tgt_priv_data->block_io);
	if (tgtdev)
		mpi3mr_tgtdev_put(tgtdev);
out:
	return retval;
}

/**
 * mpi3mr_bios_param - BIOS param callback
 * @sdev: SCSI device reference
 * @bdev: Block device reference
 * @capacity: Capacity in logical sectors
 * @params: Parameter array
 *
 * Just the parameters with heads/secots/cylinders.
 *
 * Return: 0 always
 */
static int mpi3mr_bios_param(struct scsi_device *sdev,
	struct block_device *bdev, sector_t capacity, int params[])
{
	int heads;
	int sectors;
	sector_t cylinders;
	ulong dummy;

	heads = 64;
	sectors = 32;

	dummy = heads * sectors;
	cylinders = capacity;
	sector_div(cylinders, dummy);

	if ((ulong)capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		dummy = heads * sectors;
		cylinders = capacity;
		sector_div(cylinders, dummy);
	}

	params[0] = heads;
	params[1] = sectors;
	params[2] = cylinders;
	return 0;
}

/**
 * mpi3mr_map_queues - Map queues callback handler
 * @shost: SCSI host reference
 *
 * Maps default and poll queues.
 *
 * Return: return zero.
 */
static void mpi3mr_map_queues(struct Scsi_Host *shost)
{
	struct mpi3mr_ioc *mrioc = shost_priv(shost);
	int i, qoff, offset;
	struct blk_mq_queue_map *map = NULL;

	offset = mrioc->op_reply_q_offset;

	for (i = 0, qoff = 0; i < HCTX_MAX_TYPES; i++) {
		map = &shost->tag_set.map[i];

		map->nr_queues  = 0;

		if (i == HCTX_TYPE_DEFAULT)
			map->nr_queues = mrioc->default_qcount;
		else if (i == HCTX_TYPE_POLL)
			map->nr_queues = mrioc->active_poll_qcount;

		if (!map->nr_queues) {
			BUG_ON(i == HCTX_TYPE_DEFAULT);
			continue;
		}

		/*
		 * The poll queue(s) doesn't have an IRQ (and hence IRQ
		 * affinity), so use the regular blk-mq cpu mapping
		 */
		map->queue_offset = qoff;
		if (i != HCTX_TYPE_POLL)
			blk_mq_pci_map_queues(map, mrioc->pdev, offset);
		else
			blk_mq_map_queues(map);

		qoff += map->nr_queues;
		offset += map->nr_queues;
	}
}

/**
 * mpi3mr_get_fw_pending_ios - Calculate pending I/O count
 * @mrioc: Adapter instance reference
 *
 * Calculate the pending I/Os for the controller and return.
 *
 * Return: Number of pending I/Os
 */
static inline int mpi3mr_get_fw_pending_ios(struct mpi3mr_ioc *mrioc)
{
	u16 i;
	uint pend_ios = 0;

	for (i = 0; i < mrioc->num_op_reply_q; i++)
		pend_ios += atomic_read(&mrioc->op_reply_qinfo[i].pend_ios);
	return pend_ios;
}

/**
 * mpi3mr_print_pending_host_io - print pending I/Os
 * @mrioc: Adapter instance reference
 *
 * Print number of pending I/Os and each I/O details prior to
 * reset for debug purpose.
 *
 * Return: Nothing
 */
static void mpi3mr_print_pending_host_io(struct mpi3mr_ioc *mrioc)
{
	struct Scsi_Host *shost = mrioc->shost;

	ioc_info(mrioc, "%s :Pending commands prior to reset: %d\n",
	    __func__, mpi3mr_get_fw_pending_ios(mrioc));
	blk_mq_tagset_busy_iter(&shost->tag_set,
	    mpi3mr_print_scmd, (void *)mrioc);
}

/**
 * mpi3mr_wait_for_host_io - block for I/Os to complete
 * @mrioc: Adapter instance reference
 * @timeout: time out in seconds
 * Waits for pending I/Os for the given adapter to complete or
 * to hit the timeout.
 *
 * Return: Nothing
 */
void mpi3mr_wait_for_host_io(struct mpi3mr_ioc *mrioc, u32 timeout)
{
	enum mpi3mr_iocstate iocstate;
	int i = 0;

	iocstate = mpi3mr_get_iocstate(mrioc);
	if (iocstate != MRIOC_STATE_READY)
		return;

	if (!mpi3mr_get_fw_pending_ios(mrioc))
		return;
	ioc_info(mrioc,
	    "%s :Waiting for %d seconds prior to reset for %d I/O\n",
	    __func__, timeout, mpi3mr_get_fw_pending_ios(mrioc));

	for (i = 0; i < timeout; i++) {
		if (!mpi3mr_get_fw_pending_ios(mrioc))
			break;
		iocstate = mpi3mr_get_iocstate(mrioc);
		if (iocstate != MRIOC_STATE_READY)
			break;
		msleep(1000);
	}

	ioc_info(mrioc, "%s :Pending I/Os after wait is: %d\n", __func__,
	    mpi3mr_get_fw_pending_ios(mrioc));
}

/**
 * mpi3mr_eh_host_reset - Host reset error handling callback
 * @scmd: SCSI command reference
 *
 * Issue controller reset if the scmd is for a Physical Device,
 * if the scmd is for RAID volume, then wait for
 * MPI3MR_RAID_ERRREC_RESET_TIMEOUT and checke whether any
 * pending I/Os prior to issuing reset to the controller.
 *
 * Return: SUCCESS of successful reset else FAILED
 */
static int mpi3mr_eh_host_reset(struct scsi_cmnd *scmd)
{
	struct mpi3mr_ioc *mrioc = shost_priv(scmd->device->host);
	struct mpi3mr_stgt_priv_data *stgt_priv_data;
	struct mpi3mr_sdev_priv_data *sdev_priv_data;
	u8 dev_type = MPI3_DEVICE_DEVFORM_VD;
	int retval = FAILED, ret;

	sdev_priv_data = scmd->device->hostdata;
	if (sdev_priv_data && sdev_priv_data->tgt_priv_data) {
		stgt_priv_data = sdev_priv_data->tgt_priv_data;
		dev_type = stgt_priv_data->dev_type;
	}

	if (dev_type == MPI3_DEVICE_DEVFORM_VD) {
		mpi3mr_wait_for_host_io(mrioc,
		    MPI3MR_RAID_ERRREC_RESET_TIMEOUT);
		if (!mpi3mr_get_fw_pending_ios(mrioc)) {
			retval = SUCCESS;
			goto out;
		}
	}

	mpi3mr_print_pending_host_io(mrioc);
	ret = mpi3mr_soft_reset_handler(mrioc,
	    MPI3MR_RESET_FROM_EH_HOS, 1);
	if (ret)
		goto out;

	retval = SUCCESS;
out:
	sdev_printk(KERN_INFO, scmd->device,
	    "Host reset is %s for scmd(%p)\n",
	    ((retval == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

	return retval;
}

/**
 * mpi3mr_eh_target_reset - Target reset error handling callback
 * @scmd: SCSI command reference
 *
 * Issue Target reset Task Management and verify the scmd is
 * terminated successfully and return status accordingly.
 *
 * Return: SUCCESS of successful termination of the scmd else
 *         FAILED
 */
static int mpi3mr_eh_target_reset(struct scsi_cmnd *scmd)
{
	struct mpi3mr_ioc *mrioc = shost_priv(scmd->device->host);
	struct mpi3mr_stgt_priv_data *stgt_priv_data;
	struct mpi3mr_sdev_priv_data *sdev_priv_data;
	u16 dev_handle;
	u8 resp_code = 0;
	int retval = FAILED, ret = 0;

	sdev_printk(KERN_INFO, scmd->device,
	    "Attempting Target Reset! scmd(%p)\n", scmd);
	scsi_print_command(scmd);

	sdev_priv_data = scmd->device->hostdata;
	if (!sdev_priv_data || !sdev_priv_data->tgt_priv_data) {
		sdev_printk(KERN_INFO, scmd->device,
		    "SCSI device is not available\n");
		retval = SUCCESS;
		goto out;
	}

	stgt_priv_data = sdev_priv_data->tgt_priv_data;
	dev_handle = stgt_priv_data->dev_handle;
	if (stgt_priv_data->dev_removed) {
		sdev_printk(KERN_INFO, scmd->device,
		    "%s:target(handle = 0x%04x) is removed, target reset is not issued\n",
		    mrioc->name, dev_handle);
		retval = FAILED;
		goto out;
	}
	sdev_printk(KERN_INFO, scmd->device,
	    "Target Reset is issued to handle(0x%04x)\n",
	    dev_handle);

	ret = mpi3mr_issue_tm(mrioc,
	    MPI3_SCSITASKMGMT_TASKTYPE_TARGET_RESET, dev_handle,
	    sdev_priv_data->lun_id, MPI3MR_HOSTTAG_BLK_TMS,
	    MPI3MR_RESETTM_TIMEOUT, &mrioc->host_tm_cmds, &resp_code, scmd);

	if (ret)
		goto out;

	if (stgt_priv_data->pend_count) {
		sdev_printk(KERN_INFO, scmd->device,
		    "%s: target has %d pending commands, target reset is failed\n",
		    mrioc->name, stgt_priv_data->pend_count);
		goto out;
	}

	retval = SUCCESS;
out:
	sdev_printk(KERN_INFO, scmd->device,
	    "%s: target reset is %s for scmd(%p)\n", mrioc->name,
	    ((retval == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

	return retval;
}

/**
 * mpi3mr_eh_dev_reset- Device reset error handling callback
 * @scmd: SCSI command reference
 *
 * Issue lun reset Task Management and verify the scmd is
 * terminated successfully and return status accordingly.
 *
 * Return: SUCCESS of successful termination of the scmd else
 *         FAILED
 */
static int mpi3mr_eh_dev_reset(struct scsi_cmnd *scmd)
{
	struct mpi3mr_ioc *mrioc = shost_priv(scmd->device->host);
	struct mpi3mr_stgt_priv_data *stgt_priv_data;
	struct mpi3mr_sdev_priv_data *sdev_priv_data;
	u16 dev_handle;
	u8 resp_code = 0;
	int retval = FAILED, ret = 0;

	sdev_printk(KERN_INFO, scmd->device,
	    "Attempting Device(lun) Reset! scmd(%p)\n", scmd);
	scsi_print_command(scmd);

	sdev_priv_data = scmd->device->hostdata;
	if (!sdev_priv_data || !sdev_priv_data->tgt_priv_data) {
		sdev_printk(KERN_INFO, scmd->device,
		    "SCSI device is not available\n");
		retval = SUCCESS;
		goto out;
	}

	stgt_priv_data = sdev_priv_data->tgt_priv_data;
	dev_handle = stgt_priv_data->dev_handle;
	if (stgt_priv_data->dev_removed) {
		sdev_printk(KERN_INFO, scmd->device,
		    "%s: device(handle = 0x%04x) is removed, device(LUN) reset is not issued\n",
		    mrioc->name, dev_handle);
		retval = FAILED;
		goto out;
	}
	sdev_printk(KERN_INFO, scmd->device,
	    "Device(lun) Reset is issued to handle(0x%04x)\n", dev_handle);

	ret = mpi3mr_issue_tm(mrioc,
	    MPI3_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET, dev_handle,
	    sdev_priv_data->lun_id, MPI3MR_HOSTTAG_BLK_TMS,
	    MPI3MR_RESETTM_TIMEOUT, &mrioc->host_tm_cmds, &resp_code, scmd);

	if (ret)
		goto out;

	if (sdev_priv_data->pend_count) {
		sdev_printk(KERN_INFO, scmd->device,
		    "%s: device has %d pending commands, device(LUN) reset is failed\n",
		    mrioc->name, sdev_priv_data->pend_count);
		goto out;
	}
	retval = SUCCESS;
out:
	sdev_printk(KERN_INFO, scmd->device,
	    "%s: device(LUN) reset is %s for scmd(%p)\n", mrioc->name,
	    ((retval == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

	return retval;
}

/**
 * mpi3mr_scan_start - Scan start callback handler
 * @shost: SCSI host reference
 *
 * Issue port enable request asynchronously.
 *
 * Return: Nothing
 */
static void mpi3mr_scan_start(struct Scsi_Host *shost)
{
	struct mpi3mr_ioc *mrioc = shost_priv(shost);

	mrioc->scan_started = 1;
	ioc_info(mrioc, "%s :Issuing Port Enable\n", __func__);
	if (mpi3mr_issue_port_enable(mrioc, 1)) {
		ioc_err(mrioc, "%s :Issuing port enable failed\n", __func__);
		mrioc->scan_started = 0;
		mrioc->scan_failed = MPI3_IOCSTATUS_INTERNAL_ERROR;
	}
}

/**
 * mpi3mr_scan_finished - Scan finished callback handler
 * @shost: SCSI host reference
 * @time: Jiffies from the scan start
 *
 * Checks whether the port enable is completed or timedout or
 * failed and set the scan status accordingly after taking any
 * recovery if required.
 *
 * Return: 1 on scan finished or timed out, 0 for in progress
 */
static int mpi3mr_scan_finished(struct Scsi_Host *shost,
	unsigned long time)
{
	struct mpi3mr_ioc *mrioc = shost_priv(shost);
	u32 pe_timeout = MPI3MR_PORTENABLE_TIMEOUT;
	u32 ioc_status = readl(&mrioc->sysif_regs->ioc_status);

	if ((ioc_status & MPI3_SYSIF_IOC_STATUS_RESET_HISTORY) ||
	    (ioc_status & MPI3_SYSIF_IOC_STATUS_FAULT)) {
		ioc_err(mrioc, "port enable failed due to fault or reset\n");
		mpi3mr_print_fault_info(mrioc);
		mrioc->scan_failed = MPI3_IOCSTATUS_INTERNAL_ERROR;
		mrioc->scan_started = 0;
		mrioc->init_cmds.is_waiting = 0;
		mrioc->init_cmds.callback = NULL;
		mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	}

	if (time >= (pe_timeout * HZ)) {
		ioc_err(mrioc, "port enable failed due to time out\n");
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_PE_TIMEOUT);
		mrioc->scan_failed = MPI3_IOCSTATUS_INTERNAL_ERROR;
		mrioc->scan_started = 0;
		mrioc->init_cmds.is_waiting = 0;
		mrioc->init_cmds.callback = NULL;
		mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	}

	if (mrioc->scan_started)
		return 0;

	if (mrioc->scan_failed) {
		ioc_err(mrioc,
		    "port enable failed with status=0x%04x\n",
		    mrioc->scan_failed);
	} else
		ioc_info(mrioc, "port enable is successfully completed\n");

	mpi3mr_start_watchdog(mrioc);
	mrioc->is_driver_loading = 0;
	mrioc->stop_bsgs = 0;
	return 1;
}

/**
 * mpi3mr_slave_destroy - Slave destroy callback handler
 * @sdev: SCSI device reference
 *
 * Cleanup and free per device(lun) private data.
 *
 * Return: Nothing.
 */
static void mpi3mr_slave_destroy(struct scsi_device *sdev)
{
	struct Scsi_Host *shost;
	struct mpi3mr_ioc *mrioc;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data;
	struct mpi3mr_tgt_dev *tgt_dev = NULL;
	unsigned long flags;
	struct scsi_target *starget;
	struct sas_rphy *rphy = NULL;

	if (!sdev->hostdata)
		return;

	starget = scsi_target(sdev);
	shost = dev_to_shost(&starget->dev);
	mrioc = shost_priv(shost);
	scsi_tgt_priv_data = starget->hostdata;

	scsi_tgt_priv_data->num_luns--;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	if (starget->channel == mrioc->scsi_device_channel)
		tgt_dev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, starget->id);
	else if (mrioc->sas_transport_enabled && !starget->channel) {
		rphy = dev_to_rphy(starget->dev.parent);
		tgt_dev = __mpi3mr_get_tgtdev_by_addr_and_rphy(mrioc,
		    rphy->identify.sas_address, rphy);
	}

	if (tgt_dev && (!scsi_tgt_priv_data->num_luns))
		tgt_dev->starget = NULL;
	if (tgt_dev)
		mpi3mr_tgtdev_put(tgt_dev);
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);

	kfree(sdev->hostdata);
	sdev->hostdata = NULL;
}

/**
 * mpi3mr_target_destroy - Target destroy callback handler
 * @starget: SCSI target reference
 *
 * Cleanup and free per target private data.
 *
 * Return: Nothing.
 */
static void mpi3mr_target_destroy(struct scsi_target *starget)
{
	struct Scsi_Host *shost;
	struct mpi3mr_ioc *mrioc;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data;
	struct mpi3mr_tgt_dev *tgt_dev;
	unsigned long flags;

	if (!starget->hostdata)
		return;

	shost = dev_to_shost(&starget->dev);
	mrioc = shost_priv(shost);
	scsi_tgt_priv_data = starget->hostdata;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgt_dev = __mpi3mr_get_tgtdev_from_tgtpriv(mrioc, scsi_tgt_priv_data);
	if (tgt_dev && (tgt_dev->starget == starget) &&
	    (tgt_dev->perst_id == starget->id))
		tgt_dev->starget = NULL;
	if (tgt_dev) {
		scsi_tgt_priv_data->tgt_dev = NULL;
		scsi_tgt_priv_data->perst_id = 0;
		mpi3mr_tgtdev_put(tgt_dev);
		mpi3mr_tgtdev_put(tgt_dev);
	}
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);

	kfree(starget->hostdata);
	starget->hostdata = NULL;
}

/**
 * mpi3mr_slave_configure - Slave configure callback handler
 * @sdev: SCSI device reference
 *
 * Configure queue depth, max hardware sectors and virt boundary
 * as required
 *
 * Return: 0 always.
 */
static int mpi3mr_slave_configure(struct scsi_device *sdev)
{
	struct scsi_target *starget;
	struct Scsi_Host *shost;
	struct mpi3mr_ioc *mrioc;
	struct mpi3mr_tgt_dev *tgt_dev = NULL;
	unsigned long flags;
	int retval = 0;
	struct sas_rphy *rphy = NULL;

	starget = scsi_target(sdev);
	shost = dev_to_shost(&starget->dev);
	mrioc = shost_priv(shost);

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	if (starget->channel == mrioc->scsi_device_channel)
		tgt_dev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, starget->id);
	else if (mrioc->sas_transport_enabled && !starget->channel) {
		rphy = dev_to_rphy(starget->dev.parent);
		tgt_dev = __mpi3mr_get_tgtdev_by_addr_and_rphy(mrioc,
		    rphy->identify.sas_address, rphy);
	}
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
	if (!tgt_dev)
		return -ENXIO;

	mpi3mr_change_queue_depth(sdev, tgt_dev->q_depth);

	sdev->eh_timeout = MPI3MR_EH_SCMD_TIMEOUT;
	blk_queue_rq_timeout(sdev->request_queue, MPI3MR_SCMD_TIMEOUT);

	switch (tgt_dev->dev_type) {
	case MPI3_DEVICE_DEVFORM_PCIE:
		/*The block layer hw sector size = 512*/
		if ((tgt_dev->dev_spec.pcie_inf.dev_info &
		    MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_MASK) ==
		    MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_NVME_DEVICE) {
			blk_queue_max_hw_sectors(sdev->request_queue,
			    tgt_dev->dev_spec.pcie_inf.mdts / 512);
			if (tgt_dev->dev_spec.pcie_inf.pgsz == 0)
				blk_queue_virt_boundary(sdev->request_queue,
				    ((1 << MPI3MR_DEFAULT_PGSZEXP) - 1));
			else
				blk_queue_virt_boundary(sdev->request_queue,
				    ((1 << tgt_dev->dev_spec.pcie_inf.pgsz) - 1));
		}
		break;
	default:
		break;
	}

	mpi3mr_tgtdev_put(tgt_dev);

	return retval;
}

/**
 * mpi3mr_slave_alloc -Slave alloc callback handler
 * @sdev: SCSI device reference
 *
 * Allocate per device(lun) private data and initialize it.
 *
 * Return: 0 on success -ENOMEM on memory allocation failure.
 */
static int mpi3mr_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host *shost;
	struct mpi3mr_ioc *mrioc;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data;
	struct mpi3mr_tgt_dev *tgt_dev = NULL;
	struct mpi3mr_sdev_priv_data *scsi_dev_priv_data;
	unsigned long flags;
	struct scsi_target *starget;
	int retval = 0;
	struct sas_rphy *rphy = NULL;

	starget = scsi_target(sdev);
	shost = dev_to_shost(&starget->dev);
	mrioc = shost_priv(shost);
	scsi_tgt_priv_data = starget->hostdata;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);

	if (starget->channel == mrioc->scsi_device_channel)
		tgt_dev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, starget->id);
	else if (mrioc->sas_transport_enabled && !starget->channel) {
		rphy = dev_to_rphy(starget->dev.parent);
		tgt_dev = __mpi3mr_get_tgtdev_by_addr_and_rphy(mrioc,
		    rphy->identify.sas_address, rphy);
	}

	if (tgt_dev) {
		if (tgt_dev->starget == NULL)
			tgt_dev->starget = starget;
		mpi3mr_tgtdev_put(tgt_dev);
		retval = 0;
	} else {
		spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
		return -ENXIO;
	}

	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);

	scsi_dev_priv_data = kzalloc(sizeof(*scsi_dev_priv_data), GFP_KERNEL);
	if (!scsi_dev_priv_data)
		return -ENOMEM;

	scsi_dev_priv_data->lun_id = sdev->lun;
	scsi_dev_priv_data->tgt_priv_data = scsi_tgt_priv_data;
	sdev->hostdata = scsi_dev_priv_data;

	scsi_tgt_priv_data->num_luns++;

	return retval;
}

/**
 * mpi3mr_target_alloc - Target alloc callback handler
 * @starget: SCSI target reference
 *
 * Allocate per target private data and initialize it.
 *
 * Return: 0 on success -ENOMEM on memory allocation failure.
 */
static int mpi3mr_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct mpi3mr_ioc *mrioc = shost_priv(shost);
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data;
	struct mpi3mr_tgt_dev *tgt_dev;
	unsigned long flags;
	int retval = 0;
	struct sas_rphy *rphy = NULL;
	bool update_stgt_priv_data = false;

	scsi_tgt_priv_data = kzalloc(sizeof(*scsi_tgt_priv_data), GFP_KERNEL);
	if (!scsi_tgt_priv_data)
		return -ENOMEM;

	starget->hostdata = scsi_tgt_priv_data;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);

	if (starget->channel == mrioc->scsi_device_channel) {
		tgt_dev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, starget->id);
		if (tgt_dev && !tgt_dev->is_hidden)
			update_stgt_priv_data = true;
		else
			retval = -ENXIO;
	} else if (mrioc->sas_transport_enabled && !starget->channel) {
		rphy = dev_to_rphy(starget->dev.parent);
		tgt_dev = __mpi3mr_get_tgtdev_by_addr_and_rphy(mrioc,
		    rphy->identify.sas_address, rphy);
		if (tgt_dev && !tgt_dev->is_hidden && !tgt_dev->non_stl &&
		    (tgt_dev->dev_type == MPI3_DEVICE_DEVFORM_SAS_SATA))
			update_stgt_priv_data = true;
		else
			retval = -ENXIO;
	}

	if (update_stgt_priv_data) {
		scsi_tgt_priv_data->starget = starget;
		scsi_tgt_priv_data->dev_handle = tgt_dev->dev_handle;
		scsi_tgt_priv_data->perst_id = tgt_dev->perst_id;
		scsi_tgt_priv_data->dev_type = tgt_dev->dev_type;
		scsi_tgt_priv_data->tgt_dev = tgt_dev;
		tgt_dev->starget = starget;
		atomic_set(&scsi_tgt_priv_data->block_io, 0);
		retval = 0;
		scsi_tgt_priv_data->io_throttle_enabled =
		    tgt_dev->io_throttle_enabled;
		if (tgt_dev->dev_type == MPI3_DEVICE_DEVFORM_VD)
			scsi_tgt_priv_data->throttle_group =
			    tgt_dev->dev_spec.vd_inf.tg;
	}
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);

	return retval;
}

/**
 * mpi3mr_check_return_unmap - Whether an unmap is allowed
 * @mrioc: Adapter instance reference
 * @scmd: SCSI Command reference
 *
 * The controller hardware cannot handle certain unmap commands
 * for NVMe drives, this routine checks those and return true
 * and completes the SCSI command with proper status and sense
 * data.
 *
 * Return: TRUE for not  allowed unmap, FALSE otherwise.
 */
static bool mpi3mr_check_return_unmap(struct mpi3mr_ioc *mrioc,
	struct scsi_cmnd *scmd)
{
	unsigned char *buf;
	u16 param_len, desc_len, trunc_param_len;

	trunc_param_len = param_len = get_unaligned_be16(scmd->cmnd + 7);

	if (mrioc->pdev->revision) {
		if ((param_len > 24) && ((param_len - 8) & 0xF)) {
			trunc_param_len -= (param_len - 8) & 0xF;
			dprint_scsi_command(mrioc, scmd, MPI3_DEBUG_SCSI_ERROR);
			dprint_scsi_err(mrioc,
			    "truncating param_len from (%d) to (%d)\n",
			    param_len, trunc_param_len);
			put_unaligned_be16(trunc_param_len, scmd->cmnd + 7);
			dprint_scsi_command(mrioc, scmd, MPI3_DEBUG_SCSI_ERROR);
		}
		return false;
	}

	if (!param_len) {
		ioc_warn(mrioc,
		    "%s: cdb received with zero parameter length\n",
		    __func__);
		scsi_print_command(scmd);
		scmd->result = DID_OK << 16;
		scsi_done(scmd);
		return true;
	}

	if (param_len < 24) {
		ioc_warn(mrioc,
		    "%s: cdb received with invalid param_len: %d\n",
		    __func__, param_len);
		scsi_print_command(scmd);
		scmd->result = SAM_STAT_CHECK_CONDITION;
		scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST,
		    0x1A, 0);
		scsi_done(scmd);
		return true;
	}
	if (param_len != scsi_bufflen(scmd)) {
		ioc_warn(mrioc,
		    "%s: cdb received with param_len: %d bufflen: %d\n",
		    __func__, param_len, scsi_bufflen(scmd));
		scsi_print_command(scmd);
		scmd->result = SAM_STAT_CHECK_CONDITION;
		scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST,
		    0x1A, 0);
		scsi_done(scmd);
		return true;
	}
	buf = kzalloc(scsi_bufflen(scmd), GFP_ATOMIC);
	if (!buf) {
		scsi_print_command(scmd);
		scmd->result = SAM_STAT_CHECK_CONDITION;
		scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST,
		    0x55, 0x03);
		scsi_done(scmd);
		return true;
	}
	scsi_sg_copy_to_buffer(scmd, buf, scsi_bufflen(scmd));
	desc_len = get_unaligned_be16(&buf[2]);

	if (desc_len < 16) {
		ioc_warn(mrioc,
		    "%s: Invalid descriptor length in param list: %d\n",
		    __func__, desc_len);
		scsi_print_command(scmd);
		scmd->result = SAM_STAT_CHECK_CONDITION;
		scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST,
		    0x26, 0);
		scsi_done(scmd);
		kfree(buf);
		return true;
	}

	if (param_len > (desc_len + 8)) {
		trunc_param_len = desc_len + 8;
		scsi_print_command(scmd);
		dprint_scsi_err(mrioc,
		    "truncating param_len(%d) to desc_len+8(%d)\n",
		    param_len, trunc_param_len);
		put_unaligned_be16(trunc_param_len, scmd->cmnd + 7);
		scsi_print_command(scmd);
	}

	kfree(buf);
	return false;
}

/**
 * mpi3mr_allow_scmd_to_fw - Command is allowed during shutdown
 * @scmd: SCSI Command reference
 *
 * Checks whether a cdb is allowed during shutdown or not.
 *
 * Return: TRUE for allowed commands, FALSE otherwise.
 */

inline bool mpi3mr_allow_scmd_to_fw(struct scsi_cmnd *scmd)
{
	switch (scmd->cmnd[0]) {
	case SYNCHRONIZE_CACHE:
	case START_STOP:
		return true;
	default:
		return false;
	}
}

/**
 * mpi3mr_qcmd - I/O request despatcher
 * @shost: SCSI Host reference
 * @scmd: SCSI Command reference
 *
 * Issues the SCSI Command as an MPI3 request.
 *
 * Return: 0 on successful queueing of the request or if the
 *         request is completed with failure.
 *         SCSI_MLQUEUE_DEVICE_BUSY when the device is busy.
 *         SCSI_MLQUEUE_HOST_BUSY when the host queue is full.
 */
static int mpi3mr_qcmd(struct Scsi_Host *shost,
	struct scsi_cmnd *scmd)
{
	struct mpi3mr_ioc *mrioc = shost_priv(shost);
	struct mpi3mr_stgt_priv_data *stgt_priv_data;
	struct mpi3mr_sdev_priv_data *sdev_priv_data;
	struct scmd_priv *scmd_priv_data = NULL;
	struct mpi3_scsi_io_request *scsiio_req = NULL;
	struct op_req_qinfo *op_req_q = NULL;
	int retval = 0;
	u16 dev_handle;
	u16 host_tag;
	u32 scsiio_flags = 0, data_len_blks = 0;
	struct request *rq = scsi_cmd_to_rq(scmd);
	int iprio_class;
	u8 is_pcie_dev = 0;
	u32 tracked_io_sz = 0;
	u32 ioc_pend_data_len = 0, tg_pend_data_len = 0;
	struct mpi3mr_throttle_group_info *tg = NULL;

	if (mrioc->unrecoverable) {
		scmd->result = DID_ERROR << 16;
		scsi_done(scmd);
		goto out;
	}

	sdev_priv_data = scmd->device->hostdata;
	if (!sdev_priv_data || !sdev_priv_data->tgt_priv_data) {
		scmd->result = DID_NO_CONNECT << 16;
		scsi_done(scmd);
		goto out;
	}

	if (mrioc->stop_drv_processing &&
	    !(mpi3mr_allow_scmd_to_fw(scmd))) {
		scmd->result = DID_NO_CONNECT << 16;
		scsi_done(scmd);
		goto out;
	}

	if (mrioc->reset_in_progress) {
		retval = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	stgt_priv_data = sdev_priv_data->tgt_priv_data;

	if (atomic_read(&stgt_priv_data->block_io)) {
		if (mrioc->stop_drv_processing) {
			scmd->result = DID_NO_CONNECT << 16;
			scsi_done(scmd);
			goto out;
		}
		retval = SCSI_MLQUEUE_DEVICE_BUSY;
		goto out;
	}

	dev_handle = stgt_priv_data->dev_handle;
	if (dev_handle == MPI3MR_INVALID_DEV_HANDLE) {
		scmd->result = DID_NO_CONNECT << 16;
		scsi_done(scmd);
		goto out;
	}
	if (stgt_priv_data->dev_removed) {
		scmd->result = DID_NO_CONNECT << 16;
		scsi_done(scmd);
		goto out;
	}

	if (stgt_priv_data->dev_type == MPI3_DEVICE_DEVFORM_PCIE)
		is_pcie_dev = 1;
	if ((scmd->cmnd[0] == UNMAP) && is_pcie_dev &&
	    (mrioc->pdev->device == MPI3_MFGPAGE_DEVID_SAS4116) &&
	    mpi3mr_check_return_unmap(mrioc, scmd))
		goto out;

	host_tag = mpi3mr_host_tag_for_scmd(mrioc, scmd);
	if (host_tag == MPI3MR_HOSTTAG_INVALID) {
		scmd->result = DID_ERROR << 16;
		scsi_done(scmd);
		goto out;
	}

	if (scmd->sc_data_direction == DMA_FROM_DEVICE)
		scsiio_flags = MPI3_SCSIIO_FLAGS_DATADIRECTION_READ;
	else if (scmd->sc_data_direction == DMA_TO_DEVICE)
		scsiio_flags = MPI3_SCSIIO_FLAGS_DATADIRECTION_WRITE;
	else
		scsiio_flags = MPI3_SCSIIO_FLAGS_DATADIRECTION_NO_DATA_TRANSFER;

	scsiio_flags |= MPI3_SCSIIO_FLAGS_TASKATTRIBUTE_SIMPLEQ;

	if (sdev_priv_data->ncq_prio_enable) {
		iprio_class = IOPRIO_PRIO_CLASS(req_get_ioprio(rq));
		if (iprio_class == IOPRIO_CLASS_RT)
			scsiio_flags |= 1 << MPI3_SCSIIO_FLAGS_CMDPRI_SHIFT;
	}

	if (scmd->cmd_len > 16)
		scsiio_flags |= MPI3_SCSIIO_FLAGS_CDB_GREATER_THAN_16;

	scmd_priv_data = scsi_cmd_priv(scmd);
	memset(scmd_priv_data->mpi3mr_scsiio_req, 0, MPI3MR_ADMIN_REQ_FRAME_SZ);
	scsiio_req = (struct mpi3_scsi_io_request *)scmd_priv_data->mpi3mr_scsiio_req;
	scsiio_req->function = MPI3_FUNCTION_SCSI_IO;
	scsiio_req->host_tag = cpu_to_le16(host_tag);

	mpi3mr_setup_eedp(mrioc, scmd, scsiio_req);

	memcpy(scsiio_req->cdb.cdb32, scmd->cmnd, scmd->cmd_len);
	scsiio_req->data_length = cpu_to_le32(scsi_bufflen(scmd));
	scsiio_req->dev_handle = cpu_to_le16(dev_handle);
	scsiio_req->flags = cpu_to_le32(scsiio_flags);
	int_to_scsilun(sdev_priv_data->lun_id,
	    (struct scsi_lun *)scsiio_req->lun);

	if (mpi3mr_build_sg_scmd(mrioc, scmd, scsiio_req)) {
		mpi3mr_clear_scmd_priv(mrioc, scmd);
		retval = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}
	op_req_q = &mrioc->req_qinfo[scmd_priv_data->req_q_idx];
	data_len_blks = scsi_bufflen(scmd) >> 9;
	if ((data_len_blks >= mrioc->io_throttle_data_length) &&
	    stgt_priv_data->io_throttle_enabled) {
		tracked_io_sz = data_len_blks;
		tg = stgt_priv_data->throttle_group;
		if (tg) {
			ioc_pend_data_len = atomic_add_return(data_len_blks,
			    &mrioc->pend_large_data_sz);
			tg_pend_data_len = atomic_add_return(data_len_blks,
			    &tg->pend_large_data_sz);
			if (!tg->io_divert  && ((ioc_pend_data_len >=
			    mrioc->io_throttle_high) ||
			    (tg_pend_data_len >= tg->high))) {
				tg->io_divert = 1;
				tg->need_qd_reduction = 1;
				mpi3mr_set_io_divert_for_all_vd_in_tg(mrioc,
				    tg, 1);
				mpi3mr_queue_qd_reduction_event(mrioc, tg);
			}
		} else {
			ioc_pend_data_len = atomic_add_return(data_len_blks,
			    &mrioc->pend_large_data_sz);
			if (ioc_pend_data_len >= mrioc->io_throttle_high)
				stgt_priv_data->io_divert = 1;
		}
	}

	if (stgt_priv_data->io_divert) {
		scsiio_req->msg_flags |=
		    MPI3_SCSIIO_MSGFLAGS_DIVERT_TO_FIRMWARE;
		scsiio_flags |= MPI3_SCSIIO_FLAGS_DIVERT_REASON_IO_THROTTLING;
	}
	scsiio_req->flags = cpu_to_le32(scsiio_flags);

	if (mpi3mr_op_request_post(mrioc, op_req_q,
	    scmd_priv_data->mpi3mr_scsiio_req)) {
		mpi3mr_clear_scmd_priv(mrioc, scmd);
		retval = SCSI_MLQUEUE_HOST_BUSY;
		if (tracked_io_sz) {
			atomic_sub(tracked_io_sz, &mrioc->pend_large_data_sz);
			if (tg)
				atomic_sub(tracked_io_sz,
				    &tg->pend_large_data_sz);
		}
		goto out;
	}

out:
	return retval;
}

static struct scsi_host_template mpi3mr_driver_template = {
	.module				= THIS_MODULE,
	.name				= "MPI3 Storage Controller",
	.proc_name			= MPI3MR_DRIVER_NAME,
	.queuecommand			= mpi3mr_qcmd,
	.target_alloc			= mpi3mr_target_alloc,
	.slave_alloc			= mpi3mr_slave_alloc,
	.slave_configure		= mpi3mr_slave_configure,
	.target_destroy			= mpi3mr_target_destroy,
	.slave_destroy			= mpi3mr_slave_destroy,
	.scan_finished			= mpi3mr_scan_finished,
	.scan_start			= mpi3mr_scan_start,
	.change_queue_depth		= mpi3mr_change_queue_depth,
	.eh_device_reset_handler	= mpi3mr_eh_dev_reset,
	.eh_target_reset_handler	= mpi3mr_eh_target_reset,
	.eh_host_reset_handler		= mpi3mr_eh_host_reset,
	.bios_param			= mpi3mr_bios_param,
	.map_queues			= mpi3mr_map_queues,
	.mq_poll                        = mpi3mr_blk_mq_poll,
	.no_write_same			= 1,
	.can_queue			= 1,
	.this_id			= -1,
	.sg_tablesize			= MPI3MR_SG_DEPTH,
	/* max xfer supported is 1M (2K in 512 byte sized sectors)
	 */
	.max_sectors			= 2048,
	.cmd_per_lun			= MPI3MR_MAX_CMDS_LUN,
	.max_segment_size		= 0xffffffff,
	.track_queue_depth		= 1,
	.cmd_size			= sizeof(struct scmd_priv),
	.shost_groups			= mpi3mr_host_groups,
	.sdev_groups			= mpi3mr_dev_groups,
};

/**
 * mpi3mr_init_drv_cmd - Initialize internal command tracker
 * @cmdptr: Internal command tracker
 * @host_tag: Host tag used for the specific command
 *
 * Initialize the internal command tracker structure with
 * specified host tag.
 *
 * Return: Nothing.
 */
static inline void mpi3mr_init_drv_cmd(struct mpi3mr_drv_cmd *cmdptr,
	u16 host_tag)
{
	mutex_init(&cmdptr->mutex);
	cmdptr->reply = NULL;
	cmdptr->state = MPI3MR_CMD_NOTUSED;
	cmdptr->dev_handle = MPI3MR_INVALID_DEV_HANDLE;
	cmdptr->host_tag = host_tag;
}

/**
 * osintfc_mrioc_security_status -Check controller secure status
 * @pdev: PCI device instance
 *
 * Read the Device Serial Number capability from PCI config
 * space and decide whether the controller is secure or not.
 *
 * Return: 0 on success, non-zero on failure.
 */
static int
osintfc_mrioc_security_status(struct pci_dev *pdev)
{
	u32 cap_data;
	int base;
	u32 ctlr_status;
	u32 debug_status;
	int retval = 0;

	base = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_DSN);
	if (!base) {
		dev_err(&pdev->dev,
		    "%s: PCI_EXT_CAP_ID_DSN is not supported\n", __func__);
		return -1;
	}

	pci_read_config_dword(pdev, base + 4, &cap_data);

	debug_status = cap_data & MPI3MR_CTLR_SECURE_DBG_STATUS_MASK;
	ctlr_status = cap_data & MPI3MR_CTLR_SECURITY_STATUS_MASK;

	switch (ctlr_status) {
	case MPI3MR_INVALID_DEVICE:
		dev_err(&pdev->dev,
		    "%s: Non secure ctlr (Invalid) is detected: DID: 0x%x: SVID: 0x%x: SDID: 0x%x\n",
		    __func__, pdev->device, pdev->subsystem_vendor,
		    pdev->subsystem_device);
		retval = -1;
		break;
	case MPI3MR_CONFIG_SECURE_DEVICE:
		if (!debug_status)
			dev_info(&pdev->dev,
			    "%s: Config secure ctlr is detected\n",
			    __func__);
		break;
	case MPI3MR_HARD_SECURE_DEVICE:
		break;
	case MPI3MR_TAMPERED_DEVICE:
		dev_err(&pdev->dev,
		    "%s: Non secure ctlr (Tampered) is detected: DID: 0x%x: SVID: 0x%x: SDID: 0x%x\n",
		    __func__, pdev->device, pdev->subsystem_vendor,
		    pdev->subsystem_device);
		retval = -1;
		break;
	default:
		retval = -1;
			break;
	}

	if (!retval && debug_status) {
		dev_err(&pdev->dev,
		    "%s: Non secure ctlr (Secure Dbg) is detected: DID: 0x%x: SVID: 0x%x: SDID: 0x%x\n",
		    __func__, pdev->device, pdev->subsystem_vendor,
		    pdev->subsystem_device);
		retval = -1;
	}

	return retval;
}

/**
 * mpi3mr_probe - PCI probe callback
 * @pdev: PCI device instance
 * @id: PCI device ID details
 *
 * controller initialization routine. Checks the security status
 * of the controller and if it is invalid or tampered return the
 * probe without initializing the controller. Otherwise,
 * allocate per adapter instance through shost_priv and
 * initialize controller specific data structures, initializae
 * the controller hardware, add shost to the SCSI subsystem.
 *
 * Return: 0 on success, non-zero on failure.
 */

static int
mpi3mr_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mpi3mr_ioc *mrioc = NULL;
	struct Scsi_Host *shost = NULL;
	int retval = 0, i;

	if (osintfc_mrioc_security_status(pdev)) {
		warn_non_secure_ctlr = 1;
		return 1; /* For Invalid and Tampered device */
	}

	shost = scsi_host_alloc(&mpi3mr_driver_template,
	    sizeof(struct mpi3mr_ioc));
	if (!shost) {
		retval = -ENODEV;
		goto shost_failed;
	}

	mrioc = shost_priv(shost);
	mrioc->id = mrioc_ids++;
	sprintf(mrioc->driver_name, "%s", MPI3MR_DRIVER_NAME);
	sprintf(mrioc->name, "%s%d", mrioc->driver_name, mrioc->id);
	INIT_LIST_HEAD(&mrioc->list);
	spin_lock(&mrioc_list_lock);
	list_add_tail(&mrioc->list, &mrioc_list);
	spin_unlock(&mrioc_list_lock);

	spin_lock_init(&mrioc->admin_req_lock);
	spin_lock_init(&mrioc->reply_free_queue_lock);
	spin_lock_init(&mrioc->sbq_lock);
	spin_lock_init(&mrioc->fwevt_lock);
	spin_lock_init(&mrioc->tgtdev_lock);
	spin_lock_init(&mrioc->watchdog_lock);
	spin_lock_init(&mrioc->chain_buf_lock);
	spin_lock_init(&mrioc->sas_node_lock);

	INIT_LIST_HEAD(&mrioc->fwevt_list);
	INIT_LIST_HEAD(&mrioc->tgtdev_list);
	INIT_LIST_HEAD(&mrioc->delayed_rmhs_list);
	INIT_LIST_HEAD(&mrioc->delayed_evtack_cmds_list);
	INIT_LIST_HEAD(&mrioc->sas_expander_list);
	INIT_LIST_HEAD(&mrioc->hba_port_table_list);
	INIT_LIST_HEAD(&mrioc->enclosure_list);

	mutex_init(&mrioc->reset_mutex);
	mpi3mr_init_drv_cmd(&mrioc->init_cmds, MPI3MR_HOSTTAG_INITCMDS);
	mpi3mr_init_drv_cmd(&mrioc->host_tm_cmds, MPI3MR_HOSTTAG_BLK_TMS);
	mpi3mr_init_drv_cmd(&mrioc->bsg_cmds, MPI3MR_HOSTTAG_BSG_CMDS);
	mpi3mr_init_drv_cmd(&mrioc->cfg_cmds, MPI3MR_HOSTTAG_CFG_CMDS);
	mpi3mr_init_drv_cmd(&mrioc->transport_cmds,
	    MPI3MR_HOSTTAG_TRANSPORT_CMDS);

	for (i = 0; i < MPI3MR_NUM_DEVRMCMD; i++)
		mpi3mr_init_drv_cmd(&mrioc->dev_rmhs_cmds[i],
		    MPI3MR_HOSTTAG_DEVRMCMD_MIN + i);

	if (pdev->revision)
		mrioc->enable_segqueue = true;

	init_waitqueue_head(&mrioc->reset_waitq);
	mrioc->logging_level = logging_level;
	mrioc->shost = shost;
	mrioc->pdev = pdev;
	mrioc->stop_bsgs = 1;

	/* init shost parameters */
	shost->max_cmd_len = MPI3MR_MAX_CDB_LENGTH;
	shost->max_lun = -1;
	shost->unique_id = mrioc->id;

	shost->max_channel = 0;
	shost->max_id = 0xFFFFFFFF;

	shost->host_tagset = 1;

	if (prot_mask >= 0)
		scsi_host_set_prot(shost, prot_mask);
	else {
		prot_mask = SHOST_DIF_TYPE1_PROTECTION
		    | SHOST_DIF_TYPE2_PROTECTION
		    | SHOST_DIF_TYPE3_PROTECTION;
		scsi_host_set_prot(shost, prot_mask);
	}

	ioc_info(mrioc,
	    "%s :host protection capabilities enabled %s%s%s%s%s%s%s\n",
	    __func__,
	    (prot_mask & SHOST_DIF_TYPE1_PROTECTION) ? " DIF1" : "",
	    (prot_mask & SHOST_DIF_TYPE2_PROTECTION) ? " DIF2" : "",
	    (prot_mask & SHOST_DIF_TYPE3_PROTECTION) ? " DIF3" : "",
	    (prot_mask & SHOST_DIX_TYPE0_PROTECTION) ? " DIX0" : "",
	    (prot_mask & SHOST_DIX_TYPE1_PROTECTION) ? " DIX1" : "",
	    (prot_mask & SHOST_DIX_TYPE2_PROTECTION) ? " DIX2" : "",
	    (prot_mask & SHOST_DIX_TYPE3_PROTECTION) ? " DIX3" : "");

	if (prot_guard_mask)
		scsi_host_set_guard(shost, (prot_guard_mask & 3));
	else
		scsi_host_set_guard(shost, SHOST_DIX_GUARD_CRC);

	snprintf(mrioc->fwevt_worker_name, sizeof(mrioc->fwevt_worker_name),
	    "%s%d_fwevt_wrkr", mrioc->driver_name, mrioc->id);
	mrioc->fwevt_worker_thread = alloc_ordered_workqueue(
	    mrioc->fwevt_worker_name, 0);
	if (!mrioc->fwevt_worker_thread) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		retval = -ENODEV;
		goto fwevtthread_failed;
	}

	mrioc->is_driver_loading = 1;
	mrioc->cpu_count = num_online_cpus();
	if (mpi3mr_setup_resources(mrioc)) {
		ioc_err(mrioc, "setup resources failed\n");
		retval = -ENODEV;
		goto resource_alloc_failed;
	}
	if (mpi3mr_init_ioc(mrioc)) {
		ioc_err(mrioc, "initializing IOC failed\n");
		retval = -ENODEV;
		goto init_ioc_failed;
	}

	shost->nr_hw_queues = mrioc->num_op_reply_q;
	if (mrioc->active_poll_qcount)
		shost->nr_maps = 3;

	shost->can_queue = mrioc->max_host_ios;
	shost->sg_tablesize = MPI3MR_SG_DEPTH;
	shost->max_id = mrioc->facts.max_perids + 1;

	retval = scsi_add_host(shost, &pdev->dev);
	if (retval) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto addhost_failed;
	}

	scsi_scan_host(shost);
	mpi3mr_bsg_init(mrioc);
	return retval;

addhost_failed:
	mpi3mr_stop_watchdog(mrioc);
	mpi3mr_cleanup_ioc(mrioc);
init_ioc_failed:
	mpi3mr_free_mem(mrioc);
	mpi3mr_cleanup_resources(mrioc);
resource_alloc_failed:
	destroy_workqueue(mrioc->fwevt_worker_thread);
fwevtthread_failed:
	spin_lock(&mrioc_list_lock);
	list_del(&mrioc->list);
	spin_unlock(&mrioc_list_lock);
	scsi_host_put(shost);
shost_failed:
	return retval;
}

/**
 * mpi3mr_remove - PCI remove callback
 * @pdev: PCI device instance
 *
 * Cleanup the IOC by issuing MUR and shutdown notification.
 * Free up all memory and resources associated with the
 * controllerand target devices, unregister the shost.
 *
 * Return: Nothing.
 */
static void mpi3mr_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct mpi3mr_ioc *mrioc;
	struct workqueue_struct	*wq;
	unsigned long flags;
	struct mpi3mr_tgt_dev *tgtdev, *tgtdev_next;

	if (!shost)
		return;

	mrioc = shost_priv(shost);
	while (mrioc->reset_in_progress || mrioc->is_driver_loading)
		ssleep(1);

	if (!pci_device_is_present(mrioc->pdev)) {
		mrioc->unrecoverable = 1;
		mpi3mr_flush_cmds_for_unrecovered_controller(mrioc);
	}

	mpi3mr_bsg_exit(mrioc);
	mrioc->stop_drv_processing = 1;
	mpi3mr_cleanup_fwevt_list(mrioc);
	spin_lock_irqsave(&mrioc->fwevt_lock, flags);
	wq = mrioc->fwevt_worker_thread;
	mrioc->fwevt_worker_thread = NULL;
	spin_unlock_irqrestore(&mrioc->fwevt_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	if (mrioc->sas_transport_enabled)
		sas_remove_host(shost);
	else
		scsi_remove_host(shost);

	list_for_each_entry_safe(tgtdev, tgtdev_next, &mrioc->tgtdev_list,
	    list) {
		mpi3mr_remove_tgtdev_from_host(mrioc, tgtdev);
		mpi3mr_tgtdev_del_from_list(mrioc, tgtdev);
		mpi3mr_tgtdev_put(tgtdev);
	}
	mpi3mr_stop_watchdog(mrioc);
	mpi3mr_cleanup_ioc(mrioc);
	mpi3mr_free_mem(mrioc);
	mpi3mr_cleanup_resources(mrioc);

	spin_lock(&mrioc_list_lock);
	list_del(&mrioc->list);
	spin_unlock(&mrioc_list_lock);

	scsi_host_put(shost);
}

/**
 * mpi3mr_shutdown - PCI shutdown callback
 * @pdev: PCI device instance
 *
 * Free up all memory and resources associated with the
 * controller
 *
 * Return: Nothing.
 */
static void mpi3mr_shutdown(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct mpi3mr_ioc *mrioc;
	struct workqueue_struct	*wq;
	unsigned long flags;

	if (!shost)
		return;

	mrioc = shost_priv(shost);
	while (mrioc->reset_in_progress || mrioc->is_driver_loading)
		ssleep(1);

	mrioc->stop_drv_processing = 1;
	mpi3mr_cleanup_fwevt_list(mrioc);
	spin_lock_irqsave(&mrioc->fwevt_lock, flags);
	wq = mrioc->fwevt_worker_thread;
	mrioc->fwevt_worker_thread = NULL;
	spin_unlock_irqrestore(&mrioc->fwevt_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	mpi3mr_stop_watchdog(mrioc);
	mpi3mr_cleanup_ioc(mrioc);
	mpi3mr_cleanup_resources(mrioc);
}

/**
 * mpi3mr_suspend - PCI power management suspend callback
 * @dev: Device struct
 *
 * Change the power state to the given value and cleanup the IOC
 * by issuing MUR and shutdown notification
 *
 * Return: 0 always.
 */
static int __maybe_unused
mpi3mr_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct mpi3mr_ioc *mrioc;

	if (!shost)
		return 0;

	mrioc = shost_priv(shost);
	while (mrioc->reset_in_progress || mrioc->is_driver_loading)
		ssleep(1);
	mrioc->stop_drv_processing = 1;
	mpi3mr_cleanup_fwevt_list(mrioc);
	scsi_block_requests(shost);
	mpi3mr_stop_watchdog(mrioc);
	mpi3mr_cleanup_ioc(mrioc);

	ioc_info(mrioc, "pdev=0x%p, slot=%s, entering operating state\n",
	    pdev, pci_name(pdev));
	mpi3mr_cleanup_resources(mrioc);

	return 0;
}

/**
 * mpi3mr_resume - PCI power management resume callback
 * @dev: Device struct
 *
 * Restore the power state to D0 and reinitialize the controller
 * and resume I/O operations to the target devices
 *
 * Return: 0 on success, non-zero on failure
 */
static int __maybe_unused
mpi3mr_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct mpi3mr_ioc *mrioc;
	pci_power_t device_state = pdev->current_state;
	int r;

	if (!shost)
		return 0;

	mrioc = shost_priv(shost);

	ioc_info(mrioc, "pdev=0x%p, slot=%s, previous operating state [D%d]\n",
	    pdev, pci_name(pdev), device_state);
	mrioc->pdev = pdev;
	mrioc->cpu_count = num_online_cpus();
	r = mpi3mr_setup_resources(mrioc);
	if (r) {
		ioc_info(mrioc, "%s: Setup resources failed[%d]\n",
		    __func__, r);
		return r;
	}

	mrioc->stop_drv_processing = 0;
	mpi3mr_invalidate_devhandles(mrioc);
	mpi3mr_free_enclosure_list(mrioc);
	mpi3mr_memset_buffers(mrioc);
	r = mpi3mr_reinit_ioc(mrioc, 1);
	if (r) {
		ioc_err(mrioc, "resuming controller failed[%d]\n", r);
		return r;
	}
	ssleep(MPI3MR_RESET_TOPOLOGY_SETTLE_TIME);
	scsi_unblock_requests(shost);
	mrioc->device_refresh_on = 0;
	mpi3mr_start_watchdog(mrioc);

	return 0;
}

static const struct pci_device_id mpi3mr_pci_id_table[] = {
	{
		PCI_DEVICE_SUB(MPI3_MFGPAGE_VENDORID_BROADCOM,
		    MPI3_MFGPAGE_DEVID_SAS4116, PCI_ANY_ID, PCI_ANY_ID)
	},
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, mpi3mr_pci_id_table);

static SIMPLE_DEV_PM_OPS(mpi3mr_pm_ops, mpi3mr_suspend, mpi3mr_resume);

static struct pci_driver mpi3mr_pci_driver = {
	.name = MPI3MR_DRIVER_NAME,
	.id_table = mpi3mr_pci_id_table,
	.probe = mpi3mr_probe,
	.remove = mpi3mr_remove,
	.shutdown = mpi3mr_shutdown,
	.driver.pm = &mpi3mr_pm_ops,
};

static ssize_t event_counter_show(struct device_driver *dd, char *buf)
{
	return sprintf(buf, "%llu\n", atomic64_read(&event_counter));
}
static DRIVER_ATTR_RO(event_counter);

static int __init mpi3mr_init(void)
{
	int ret_val;

	pr_info("Loading %s version %s\n", MPI3MR_DRIVER_NAME,
	    MPI3MR_DRIVER_VERSION);

	mpi3mr_transport_template =
	    sas_attach_transport(&mpi3mr_transport_functions);
	if (!mpi3mr_transport_template) {
		pr_err("%s failed to load due to sas transport attach failure\n",
		    MPI3MR_DRIVER_NAME);
		return -ENODEV;
	}

	ret_val = pci_register_driver(&mpi3mr_pci_driver);
	if (ret_val) {
		pr_err("%s failed to load due to pci register driver failure\n",
		    MPI3MR_DRIVER_NAME);
		goto err_pci_reg_fail;
	}

	ret_val = driver_create_file(&mpi3mr_pci_driver.driver,
				     &driver_attr_event_counter);
	if (ret_val)
		goto err_event_counter;

	return ret_val;

err_event_counter:
	pci_unregister_driver(&mpi3mr_pci_driver);

err_pci_reg_fail:
	sas_release_transport(mpi3mr_transport_template);
	return ret_val;
}

static void __exit mpi3mr_exit(void)
{
	if (warn_non_secure_ctlr)
		pr_warn(
		    "Unloading %s version %s while managing a non secure controller\n",
		    MPI3MR_DRIVER_NAME, MPI3MR_DRIVER_VERSION);
	else
		pr_info("Unloading %s version %s\n", MPI3MR_DRIVER_NAME,
		    MPI3MR_DRIVER_VERSION);

	driver_remove_file(&mpi3mr_pci_driver.driver,
			   &driver_attr_event_counter);
	pci_unregister_driver(&mpi3mr_pci_driver);
	sas_release_transport(mpi3mr_transport_template);
}

module_init(mpi3mr_init);
module_exit(mpi3mr_exit);
