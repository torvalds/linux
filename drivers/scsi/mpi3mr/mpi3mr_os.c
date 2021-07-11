// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2021 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#include "mpi3mr.h"

/* global driver scop variables */
LIST_HEAD(mrioc_list);
DEFINE_SPINLOCK(mrioc_list_lock);
static int mrioc_ids;
static int warn_non_secure_ctlr;

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

	unique_tag = blk_mq_unique_tag(scmd->request);

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

	while ((fwevt = mpi3mr_dequeue_fwevt(mrioc)) ||
	    (fwevt = mrioc->current_event)) {
		/*
		 * Wait on the fwevt to complete. If this returns 1, then
		 * the event was never executed, and we need a put for the
		 * reference the work had on the fwevt.
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
		}
	}
}

/**
 * mpi3mr_print_scmd - print individual SCSI command
 * @rq: Block request
 * @data: Adapter instance reference
 * @reserved: N/A. Currently not used
 *
 * Print the SCSI command details if it is in LLD scope.
 *
 * Return: true always.
 */
static bool mpi3mr_print_scmd(struct request *rq,
	void *data, bool reserved)
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
 * @reserved: N/A. Currently not used
 *
 * Return the SCSI command to the upper layers if it is in LLD
 * scope.
 *
 * Return: true always.
 */

static bool mpi3mr_flush_scmd(struct request *rq,
	void *data, bool reserved)
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
		scmd->scsi_done(scmd);
		mrioc->flush_io_count++;
	}

out:
	return(true);
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
static struct mpi3mr_tgt_dev *mpi3mr_get_tgtdev_by_handle(
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
static void mpi3mr_remove_tgtdev_from_host(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev)
{
	struct mpi3mr_stgt_priv_data *tgt_priv;

	ioc_info(mrioc, "%s :Removing handle(0x%04x), wwid(0x%016llx)\n",
	    __func__, tgtdev->dev_handle, (unsigned long long)tgtdev->wwid);
	if (tgtdev->starget && tgtdev->starget->hostdata) {
		tgt_priv = tgtdev->starget->hostdata;
		tgt_priv->dev_handle = MPI3MR_INVALID_DEV_HANDLE;
	}

	if (tgtdev->starget) {
		scsi_remove_target(&tgtdev->starget->dev);
		tgtdev->host_exposed = 0;
	}
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

	tgtdev = mpi3mr_get_tgtdev_by_perst_id(mrioc, perst_id);
	if (!tgtdev) {
		retval = -1;
		goto out;
	}
	if (tgtdev->is_hidden) {
		retval = -1;
		goto out;
	}
	if (!tgtdev->host_exposed && !mrioc->reset_in_progress) {
		tgtdev->host_exposed = 1;
		scsi_scan_target(&mrioc->shost->shost_gendev, 0,
		    tgtdev->perst_id,
		    SCAN_WILD_CARD, SCSI_SCAN_INITIAL);
		if (!tgtdev->starget)
			tgtdev->host_exposed = 0;
	}
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
		blk_queue_max_hw_sectors(sdev->request_queue,
		    tgtdev->dev_spec.pcie_inf.mdts / 512);
		blk_queue_virt_boundary(sdev->request_queue,
		    ((1 << tgtdev->dev_spec.pcie_inf.pgsz) - 1));

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
		if ((tgtdev->dev_handle == MPI3MR_INVALID_DEV_HANDLE) &&
		    tgtdev->host_exposed) {
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
 *
 * Update the information from the device page0 into the driver
 * cached target device structure.
 *
 * Return: Nothing.
 */
static void mpi3mr_update_tgtdev(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev, struct mpi3_device_page0 *dev_pg0)
{
	u16 flags = 0;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data;
	u8 prot_mask = 0;

	tgtdev->perst_id = le16_to_cpu(dev_pg0->persistent_id);
	tgtdev->dev_handle = le16_to_cpu(dev_pg0->dev_handle);
	tgtdev->dev_type = dev_pg0->device_form;
	tgtdev->encl_handle = le16_to_cpu(dev_pg0->enclosure_handle);
	tgtdev->parent_handle = le16_to_cpu(dev_pg0->parent_dev_handle);
	tgtdev->slot = le16_to_cpu(dev_pg0->slot);
	tgtdev->q_depth = le16_to_cpu(dev_pg0->queue_depth);
	tgtdev->wwid = le64_to_cpu(dev_pg0->wwid);

	flags = le16_to_cpu(dev_pg0->flags);
	tgtdev->is_hidden = (flags & MPI3_DEVICE0_FLAGS_HIDDEN);

	if (tgtdev->starget && tgtdev->starget->hostdata) {
		scsi_tgt_priv_data = (struct mpi3mr_stgt_priv_data *)
		    tgtdev->starget->hostdata;
		scsi_tgt_priv_data->perst_id = tgtdev->perst_id;
		scsi_tgt_priv_data->dev_handle = tgtdev->dev_handle;
		scsi_tgt_priv_data->dev_type = tgtdev->dev_type;
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
		if ((dev_info & MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_MASK) !=
		    MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_END_DEVICE)
			tgtdev->is_hidden = 1;
		else if (!(dev_info & (MPI3_SAS_DEVICE_INFO_STP_SATA_TARGET |
		    MPI3_SAS_DEVICE_INFO_SSP_TARGET)))
			tgtdev->is_hidden = 1;
		break;
	}
	case MPI3_DEVICE_DEVFORM_PCIE:
	{
		struct mpi3_device0_pcie_format *pcieinf =
		    &dev_pg0->device_specific.pcie_format;
		u16 dev_info = le16_to_cpu(pcieinf->device_info);

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
			    pcieinf->controller_reset_to;
			tgtdev->dev_spec.pcie_inf.abort_to =
			    pcieinf->nv_me_abort_to;
		}
		if (tgtdev->dev_spec.pcie_inf.mdts > (1024 * 1024))
			tgtdev->dev_spec.pcie_inf.mdts = (1024 * 1024);
		if ((dev_info & MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_MASK) !=
		    MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_NVME_DEVICE)
			tgtdev->is_hidden = 1;
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

		tgtdev->dev_spec.vol_inf.state = vdinf->vd_state;
		if (vdinf->vd_state == MPI3_DEVICE0_VD_STATE_OFFLINE)
			tgtdev->is_hidden = 1;
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
	mpi3mr_update_tgtdev(mrioc, tgtdev, dev_pg0);
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
	    "%s :\texpander_handle(0x%04x), enclosure_handle(0x%04x) start_phy(%02d), num_entries(%d)\n",
	    __func__, le16_to_cpu(event_data->expander_dev_handle),
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
	struct mpi3mr_tgt_dev *tgtdev = NULL;

	mpi3mr_sastopochg_evt_debug(mrioc, event_data);

	for (i = 0; i < event_data->num_entries; i++) {
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
		default:
			break;
		}
		if (tgtdev)
			mpi3mr_tgtdev_put(tgtdev);
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
	mrioc->current_event = fwevt;
	mpi3mr_fwevt_del_from_list(mrioc, fwevt);

	if (mrioc->stop_drv_processing)
		goto out;

	if (!fwevt->process_evt)
		goto evt_ack;

	switch (fwevt->event_id) {
	case MPI3_EVENT_DEVICE_ADDED:
	{
		struct mpi3_device_page0 *dev_pg0 =
		    (struct mpi3_device_page0 *)fwevt->event_data;
		mpi3mr_report_tgtdev_to_host(mrioc,
		    le16_to_cpu(dev_pg0->persistent_id));
		break;
	}
	case MPI3_EVENT_DEVICE_INFO_CHANGED:
	{
		mpi3mr_devinfochg_evt_bh(mrioc,
		    (struct mpi3_device_page0 *)fwevt->event_data);
		break;
	}
	case MPI3_EVENT_DEVICE_STATUS_CHANGE:
	{
		mpi3mr_devstatuschg_evt_bh(mrioc, fwevt);
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
	default:
		break;
	}

evt_ack:
	if (fwevt->send_ack)
		mpi3mr_send_event_ack(mrioc, fwevt->event_id,
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
	tgtdev = mpi3mr_get_tgtdev_by_perst_id(mrioc, perst_id);
	if (tgtdev) {
		mpi3mr_update_tgtdev(mrioc, tgtdev, dev_pg0);
		mpi3mr_tgtdev_put(tgtdev);
	} else {
		tgtdev = mpi3mr_alloc_tgtdev();
		if (!tgtdev)
			return -ENOMEM;
		mpi3mr_update_tgtdev(mrioc, tgtdev, dev_pg0);
		mpi3mr_tgtdev_add_to_list(mrioc, tgtdev);
	}

	return retval;
}

/**
 * mpi3mr_flush_delayed_rmhs_list - Flush pending commands
 * @mrioc: Adapter instance reference
 *
 * Flush pending commands in the delayed removal handshake list
 * due to a controller reset or driver removal as a cleanup.
 *
 * Return: Nothing
 */
void mpi3mr_flush_delayed_rmhs_list(struct mpi3mr_ioc *mrioc)
{
	struct delayed_dev_rmhs_node *_rmhs_node;

	while (!list_empty(&mrioc->delayed_rmhs_list)) {
		_rmhs_node = list_entry(mrioc->delayed_rmhs_list.next,
		    struct delayed_dev_rmhs_node, list);
		list_del(&_rmhs_node->list);
		kfree(_rmhs_node);
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
		goto out_failed;
	}

	return;
out_failed:
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
	case MPI3_EVENT_DEVICE_INFO_CHANGED:
	{
		process_evt_bh = 1;
		break;
	}
	case MPI3_EVENT_ENERGY_PACK_CHANGE:
	{
		mpi3mr_energypackchg_evt_th(mrioc, event_reply);
		break;
	}
	case MPI3_EVENT_ENCL_DEVICE_STATUS_CHANGE:
	case MPI3_EVENT_SAS_DISCOVERY:
	case MPI3_EVENT_CABLE_MGMT:
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
	unsigned char prot_type = scsi_get_prot_type(scmd);

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
		eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_CHECK |
		    MPI3_EEDPFLAGS_CHK_REF_TAG | MPI3_EEDPFLAGS_CHK_APP_TAG |
		    MPI3_EEDPFLAGS_CHK_GUARD;
		scsiio_req->msg_flags |= MPI3_SCSIIO_MSGFLAGS_METASGL_VALID;
		break;
	case SCSI_PROT_WRITE_PASS:
		if (scsi_host_get_guard(scmd->device->host)
		    & SHOST_DIX_GUARD_IP) {
			eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_CHECK_REGEN |
			    MPI3_EEDPFLAGS_CHK_APP_TAG |
			    MPI3_EEDPFLAGS_CHK_GUARD |
			    MPI3_EEDPFLAGS_INCR_PRI_REF_TAG;
			scsiio_req->sgl[0].eedp.application_tag_translation_mask =
			    0xffff;
		} else {
			eedp_flags = MPI3_EEDPFLAGS_EEDP_OP_CHECK |
			    MPI3_EEDPFLAGS_CHK_REF_TAG |
			    MPI3_EEDPFLAGS_CHK_APP_TAG |
			    MPI3_EEDPFLAGS_CHK_GUARD;
		}
		scsiio_req->msg_flags |= MPI3_SCSIIO_MSGFLAGS_METASGL_VALID;
		break;
	default:
		return;
	}

	if (scsi_host_get_guard(scmd->device->host) & SHOST_DIX_GUARD_IP)
		eedp_flags |= MPI3_EEDPFLAGS_HOST_GUARD_IP_CHKSUM;

	switch (prot_type) {
	case SCSI_PROT_DIF_TYPE0:
		eedp_flags |= MPI3_EEDPFLAGS_INCR_PRI_REF_TAG;
		scsiio_req->cdb.eedp32.primary_reference_tag =
		    cpu_to_be32(t10_pi_ref_tag(scmd->request));
		break;
	case SCSI_PROT_DIF_TYPE1:
	case SCSI_PROT_DIF_TYPE2:
		eedp_flags |= MPI3_EEDPFLAGS_INCR_PRI_REF_TAG |
		    MPI3_EEDPFLAGS_ESC_MODE_APPTAG_DISABLE |
		    MPI3_EEDPFLAGS_CHK_GUARD;
		scsiio_req->cdb.eedp32.primary_reference_tag =
		    cpu_to_be32(t10_pi_ref_tag(scmd->request));
		break;
	case SCSI_PROT_DIF_TYPE3:
		eedp_flags |= MPI3_EEDPFLAGS_CHK_GUARD |
		    MPI3_EEDPFLAGS_ESC_MODE_APPTAG_DISABLE;
		break;

	default:
		scsiio_req->msg_flags &= ~(MPI3_SCSIIO_MSGFLAGS_METASGL_VALID);
		return;
	}

	switch (scmd->device->sector_size) {
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
	if (success_desc) {
		scmd->result = DID_OK << 16;
		goto out_success;
	}
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
	    (scmd->cmnd[0] != ATA_16)) {
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
	scmd->scsi_done(scmd);
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
 * mpi3mr_print_response_code - print TM response as a string
 * @mrioc: Adapter instance reference
 * @resp_code: TM response code
 *
 * Print TM response code as a readable string.
 *
 * Return: Nothing.
 */
static void mpi3mr_print_response_code(struct mpi3mr_ioc *mrioc, u8 resp_code)
{
	char *desc;

	switch (resp_code) {
	case MPI3MR_RSP_TM_COMPLETE:
		desc = "task management request completed";
		break;
	case MPI3MR_RSP_INVALID_FRAME:
		desc = "invalid frame";
		break;
	case MPI3MR_RSP_TM_NOT_SUPPORTED:
		desc = "task management request not supported";
		break;
	case MPI3MR_RSP_TM_FAILED:
		desc = "task management request failed";
		break;
	case MPI3MR_RSP_TM_SUCCEEDED:
		desc = "task management request succeeded";
		break;
	case MPI3MR_RSP_TM_INVALID_LUN:
		desc = "invalid lun";
		break;
	case MPI3MR_RSP_TM_OVERLAPPED_TAG:
		desc = "overlapped tag attempted";
		break;
	case MPI3MR_RSP_IO_QUEUED_ON_IOC:
		desc = "task queued, however not sent to target";
		break;
	default:
		desc = "unknown";
		break;
	}
	ioc_info(mrioc, "%s :response_code(0x%01x): %s\n", __func__,
	    resp_code, desc);
}

/**
 * mpi3mr_issue_tm - Issue Task Management request
 * @mrioc: Adapter instance reference
 * @tm_type: Task Management type
 * @handle: Device handle
 * @lun: lun ID
 * @htag: Host tag of the TM request
 * @drv_cmd: Internal command tracker
 * @resp_code: Response code place holder
 * @cmd_priv: SCSI command private data
 *
 * Issues a Task Management Request to the controller for a
 * specified target, lun and command and wait for its completion
 * and check TM response. Recover the TM if it timed out by
 * issuing controller reset.
 *
 * Return: 0 on success, non-zero on errors
 */
static int mpi3mr_issue_tm(struct mpi3mr_ioc *mrioc, u8 tm_type,
	u16 handle, uint lun, u16 htag, ulong timeout,
	struct mpi3mr_drv_cmd *drv_cmd,
	u8 *resp_code, struct scmd_priv *cmd_priv)
{
	struct mpi3_scsi_task_mgmt_request tm_req;
	struct mpi3_scsi_task_mgmt_reply *tm_reply = NULL;
	int retval = 0;
	struct mpi3mr_tgt_dev *tgtdev = NULL;
	struct mpi3mr_stgt_priv_data *scsi_tgt_priv_data = NULL;
	struct op_req_qinfo *op_req_q = NULL;

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
	if (tgtdev && tgtdev->starget && tgtdev->starget->hostdata) {
		scsi_tgt_priv_data = (struct mpi3mr_stgt_priv_data *)
		    tgtdev->starget->hostdata;
		atomic_inc(&scsi_tgt_priv_data->block_io);
	}
	if (cmd_priv) {
		op_req_q = &mrioc->req_qinfo[cmd_priv->req_q_idx];
		tm_req.task_host_tag = cpu_to_le16(cmd_priv->host_tag);
		tm_req.task_request_queue_id = cpu_to_le16(op_req_q->qid);
	}
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
		ioc_err(mrioc, "%s :Issue TM: command timed out\n", __func__);
		drv_cmd->is_waiting = 0;
		retval = -1;
		mpi3mr_soft_reset_handler(mrioc,
		    MPI3MR_RESET_FROM_TM_TIMEOUT, 1);
		goto out_unlock;
	}

	if (drv_cmd->state & MPI3MR_CMD_REPLY_VALID)
		tm_reply = (struct mpi3_scsi_task_mgmt_reply *)drv_cmd->reply;

	if (drv_cmd->ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc,
		    "%s :Issue TM: handle(0x%04x) Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
		    __func__, handle, drv_cmd->ioc_status,
		    drv_cmd->ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}

	if (!tm_reply) {
		ioc_err(mrioc, "%s :Issue TM: No TM Reply message\n", __func__);
		retval = -1;
		goto out_unlock;
	}

	*resp_code = le32_to_cpu(tm_reply->response_data) &
	    MPI3MR_RI_MASK_RESPCODE;
	switch (*resp_code) {
	case MPI3MR_RSP_TM_SUCCEEDED:
	case MPI3MR_RSP_TM_COMPLETE:
		break;
	case MPI3MR_RSP_IO_QUEUED_ON_IOC:
		if (tm_type != MPI3_SCSITASKMGMT_TASKTYPE_QUERY_TASK)
			retval = -1;
		break;
	default:
		retval = -1;
		break;
	}

	ioc_info(mrioc,
	    "%s :Issue TM: Completed TM type (0x%x) handle(0x%04x) ",
	    __func__, tm_type, handle);
	ioc_info(mrioc,
	    "with ioc_status(0x%04x), loginfo(0x%08x), term_count(0x%08x)\n",
	    drv_cmd->ioc_status, drv_cmd->ioc_loginfo,
	    le32_to_cpu(tm_reply->termination_count));
	mpi3mr_print_response_code(mrioc, *resp_code);

out_unlock:
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&drv_cmd->mutex);
	if (scsi_tgt_priv_data)
		atomic_dec_if_positive(&scsi_tgt_priv_data->block_io);
	if (tgtdev)
		mpi3mr_tgtdev_put(tgtdev);
	if (!retval) {
		/*
		 * Flush all IRQ handlers by calling synchronize_irq().
		 * mpi3mr_ioc_disable_intr() takes care of it.
		 */
		mpi3mr_ioc_disable_intr(mrioc);
		mpi3mr_ioc_enable_intr(mrioc);
	}
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
 * Call the blk_mq_pci_map_queues with from which operational
 * queue the mapping has to be done
 *
 * Return: return of blk_mq_pci_map_queues
 */
static int mpi3mr_map_queues(struct Scsi_Host *shost)
{
	struct mpi3mr_ioc *mrioc = shost_priv(shost);

	return blk_mq_pci_map_queues(&shost->tag_set.map[HCTX_TYPE_DEFAULT],
	    mrioc->pdev, mrioc->op_reply_q_offset);
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
	sdev_printk(KERN_INFO, scmd->device,
	    "Target Reset is issued to handle(0x%04x)\n",
	    dev_handle);

	ret = mpi3mr_issue_tm(mrioc,
	    MPI3_SCSITASKMGMT_TASKTYPE_TARGET_RESET, dev_handle,
	    sdev_priv_data->lun_id, MPI3MR_HOSTTAG_BLK_TMS,
	    MPI3MR_RESETTM_TIMEOUT, &mrioc->host_tm_cmds, &resp_code, NULL);

	if (ret)
		goto out;

	retval = SUCCESS;
out:
	sdev_printk(KERN_INFO, scmd->device,
	    "Target reset is %s for scmd(%p)\n",
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
	sdev_printk(KERN_INFO, scmd->device,
	    "Device(lun) Reset is issued to handle(0x%04x)\n", dev_handle);

	ret = mpi3mr_issue_tm(mrioc,
	    MPI3_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET, dev_handle,
	    sdev_priv_data->lun_id, MPI3MR_HOSTTAG_BLK_TMS,
	    MPI3MR_RESETTM_TIMEOUT, &mrioc->host_tm_cmds, &resp_code, NULL);

	if (ret)
		goto out;

	retval = SUCCESS;
out:
	sdev_printk(KERN_INFO, scmd->device,
	    "Device(lun) reset is %s for scmd(%p)\n",
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

	if (time >= (pe_timeout * HZ)) {
		mrioc->init_cmds.is_waiting = 0;
		mrioc->init_cmds.callback = NULL;
		mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
		ioc_err(mrioc, "%s :port enable request timed out\n", __func__);
		mrioc->is_driver_loading = 0;
		mpi3mr_soft_reset_handler(mrioc,
		    MPI3MR_RESET_FROM_PE_TIMEOUT, 1);
	}

	if (mrioc->scan_failed) {
		ioc_err(mrioc,
		    "%s :port enable failed with (ioc_status=0x%08x)\n",
		    __func__, mrioc->scan_failed);
		mrioc->is_driver_loading = 0;
		mrioc->stop_drv_processing = 1;
		return 1;
	}

	if (mrioc->scan_started)
		return 0;
	ioc_info(mrioc, "%s :port enable: SUCCESS\n", __func__);
	mpi3mr_start_watchdog(mrioc);
	mrioc->is_driver_loading = 0;

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
	struct mpi3mr_tgt_dev *tgt_dev;
	unsigned long flags;
	struct scsi_target *starget;

	if (!sdev->hostdata)
		return;

	starget = scsi_target(sdev);
	shost = dev_to_shost(&starget->dev);
	mrioc = shost_priv(shost);
	scsi_tgt_priv_data = starget->hostdata;

	scsi_tgt_priv_data->num_luns--;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgt_dev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, starget->id);
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
	struct mpi3mr_tgt_dev *tgt_dev;
	unsigned long flags;
	int retval = 0;

	starget = scsi_target(sdev);
	shost = dev_to_shost(&starget->dev);
	mrioc = shost_priv(shost);

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgt_dev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, starget->id);
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);
	if (!tgt_dev)
		return -ENXIO;

	mpi3mr_change_queue_depth(sdev, tgt_dev->q_depth);
	switch (tgt_dev->dev_type) {
	case MPI3_DEVICE_DEVFORM_PCIE:
		/*The block layer hw sector size = 512*/
		blk_queue_max_hw_sectors(sdev->request_queue,
		    tgt_dev->dev_spec.pcie_inf.mdts / 512);
		blk_queue_virt_boundary(sdev->request_queue,
		    ((1 << tgt_dev->dev_spec.pcie_inf.pgsz) - 1));
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
	struct mpi3mr_tgt_dev *tgt_dev;
	struct mpi3mr_sdev_priv_data *scsi_dev_priv_data;
	unsigned long flags;
	struct scsi_target *starget;
	int retval = 0;

	starget = scsi_target(sdev);
	shost = dev_to_shost(&starget->dev);
	mrioc = shost_priv(shost);
	scsi_tgt_priv_data = starget->hostdata;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgt_dev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, starget->id);

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

	scsi_tgt_priv_data = kzalloc(sizeof(*scsi_tgt_priv_data), GFP_KERNEL);
	if (!scsi_tgt_priv_data)
		return -ENOMEM;

	starget->hostdata = scsi_tgt_priv_data;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgt_dev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, starget->id);
	if (tgt_dev && !tgt_dev->is_hidden) {
		scsi_tgt_priv_data->starget = starget;
		scsi_tgt_priv_data->dev_handle = tgt_dev->dev_handle;
		scsi_tgt_priv_data->perst_id = tgt_dev->perst_id;
		scsi_tgt_priv_data->dev_type = tgt_dev->dev_type;
		scsi_tgt_priv_data->tgt_dev = tgt_dev;
		tgt_dev->starget = starget;
		atomic_set(&scsi_tgt_priv_data->block_io, 0);
		retval = 0;
	} else
		retval = -ENXIO;
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
	u16 param_len, desc_len;

	param_len = get_unaligned_be16(scmd->cmnd + 7);

	if (!param_len) {
		ioc_warn(mrioc,
		    "%s: cdb received with zero parameter length\n",
		    __func__);
		scsi_print_command(scmd);
		scmd->result = DID_OK << 16;
		scmd->scsi_done(scmd);
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
		scmd->scsi_done(scmd);
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
		scmd->scsi_done(scmd);
		return true;
	}
	buf = kzalloc(scsi_bufflen(scmd), GFP_ATOMIC);
	if (!buf) {
		scsi_print_command(scmd);
		scmd->result = SAM_STAT_CHECK_CONDITION;
		scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST,
		    0x55, 0x03);
		scmd->scsi_done(scmd);
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
		scmd->scsi_done(scmd);
		kfree(buf);
		return true;
	}

	if (param_len > (desc_len + 8)) {
		scsi_print_command(scmd);
		ioc_warn(mrioc,
		    "%s: Truncating param_len(%d) to desc_len+8(%d)\n",
		    __func__, param_len, (desc_len + 8));
		param_len = desc_len + 8;
		put_unaligned_be16(param_len, scmd->cmnd + 7);
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
	u32 scsiio_flags = 0;
	struct request *rq = scmd->request;
	int iprio_class;

	sdev_priv_data = scmd->device->hostdata;
	if (!sdev_priv_data || !sdev_priv_data->tgt_priv_data) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		goto out;
	}

	if (mrioc->stop_drv_processing &&
	    !(mpi3mr_allow_scmd_to_fw(scmd))) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		goto out;
	}

	if (mrioc->reset_in_progress) {
		retval = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	stgt_priv_data = sdev_priv_data->tgt_priv_data;

	dev_handle = stgt_priv_data->dev_handle;
	if (dev_handle == MPI3MR_INVALID_DEV_HANDLE) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		goto out;
	}
	if (stgt_priv_data->dev_removed) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		goto out;
	}

	if (atomic_read(&stgt_priv_data->block_io)) {
		if (mrioc->stop_drv_processing) {
			scmd->result = DID_NO_CONNECT << 16;
			scmd->scsi_done(scmd);
			goto out;
		}
		retval = SCSI_MLQUEUE_DEVICE_BUSY;
		goto out;
	}

	if ((scmd->cmnd[0] == UNMAP) &&
	    (stgt_priv_data->dev_type == MPI3_DEVICE_DEVFORM_PCIE) &&
	    mpi3mr_check_return_unmap(mrioc, scmd))
		goto out;

	host_tag = mpi3mr_host_tag_for_scmd(mrioc, scmd);
	if (host_tag == MPI3MR_HOSTTAG_INVALID) {
		scmd->result = DID_ERROR << 16;
		scmd->scsi_done(scmd);
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

	if (mpi3mr_op_request_post(mrioc, op_req_q,
	    scmd_priv_data->mpi3mr_scsiio_req)) {
		mpi3mr_clear_scmd_priv(mrioc, scmd);
		retval = SCSI_MLQUEUE_HOST_BUSY;
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
	.no_write_same			= 1,
	.can_queue			= 1,
	.this_id			= -1,
	.sg_tablesize			= MPI3MR_SG_DEPTH,
	/* max xfer supported is 1M (2K in 512 byte sized sectors)
	 */
	.max_sectors			= 2048,
	.cmd_per_lun			= MPI3MR_MAX_CMDS_LUN,
	.track_queue_depth		= 1,
	.cmd_size			= sizeof(struct scmd_priv),
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

	INIT_LIST_HEAD(&mrioc->fwevt_list);
	INIT_LIST_HEAD(&mrioc->tgtdev_list);
	INIT_LIST_HEAD(&mrioc->delayed_rmhs_list);

	mutex_init(&mrioc->reset_mutex);
	mpi3mr_init_drv_cmd(&mrioc->init_cmds, MPI3MR_HOSTTAG_INITCMDS);
	mpi3mr_init_drv_cmd(&mrioc->host_tm_cmds, MPI3MR_HOSTTAG_BLK_TMS);

	for (i = 0; i < MPI3MR_NUM_DEVRMCMD; i++)
		mpi3mr_init_drv_cmd(&mrioc->dev_rmhs_cmds[i],
		    MPI3MR_HOSTTAG_DEVRMCMD_MIN + i);

	if (pdev->revision)
		mrioc->enable_segqueue = true;

	init_waitqueue_head(&mrioc->reset_waitq);
	mrioc->logging_level = logging_level;
	mrioc->shost = shost;
	mrioc->pdev = pdev;

	/* init shost parameters */
	shost->max_cmd_len = MPI3MR_MAX_CDB_LENGTH;
	shost->max_lun = -1;
	shost->unique_id = mrioc->id;

	shost->max_channel = 1;
	shost->max_id = 0xFFFFFFFF;

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
	    mrioc->fwevt_worker_name, WQ_MEM_RECLAIM);
	if (!mrioc->fwevt_worker_thread) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		retval = -ENODEV;
		goto out_fwevtthread_failed;
	}

	mrioc->is_driver_loading = 1;
	if (mpi3mr_init_ioc(mrioc, 0)) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		retval = -ENODEV;
		goto out_iocinit_failed;
	}

	shost->nr_hw_queues = mrioc->num_op_reply_q;
	shost->can_queue = mrioc->max_host_ios;
	shost->sg_tablesize = MPI3MR_SG_DEPTH;
	shost->max_id = mrioc->facts.max_perids;

	retval = scsi_add_host(shost, &pdev->dev);
	if (retval) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto addhost_failed;
	}

	scsi_scan_host(shost);
	return retval;

addhost_failed:
	mpi3mr_cleanup_ioc(mrioc, 0);
out_iocinit_failed:
	destroy_workqueue(mrioc->fwevt_worker_thread);
out_fwevtthread_failed:
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

	mrioc->stop_drv_processing = 1;
	mpi3mr_cleanup_fwevt_list(mrioc);
	spin_lock_irqsave(&mrioc->fwevt_lock, flags);
	wq = mrioc->fwevt_worker_thread;
	mrioc->fwevt_worker_thread = NULL;
	spin_unlock_irqrestore(&mrioc->fwevt_lock, flags);
	if (wq)
		destroy_workqueue(wq);
	scsi_remove_host(shost);

	list_for_each_entry_safe(tgtdev, tgtdev_next, &mrioc->tgtdev_list,
	    list) {
		mpi3mr_remove_tgtdev_from_host(mrioc, tgtdev);
		mpi3mr_tgtdev_del_from_list(mrioc, tgtdev);
		mpi3mr_tgtdev_put(tgtdev);
	}
	mpi3mr_cleanup_ioc(mrioc, 0);

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
	mpi3mr_cleanup_ioc(mrioc, 0);
}

#ifdef CONFIG_PM
/**
 * mpi3mr_suspend - PCI power management suspend callback
 * @pdev: PCI device instance
 * @state: New power state
 *
 * Change the power state to the given value and cleanup the IOC
 * by issuing MUR and shutdown notification
 *
 * Return: 0 always.
 */
static int mpi3mr_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct mpi3mr_ioc *mrioc;
	pci_power_t device_state;

	if (!shost)
		return 0;

	mrioc = shost_priv(shost);
	while (mrioc->reset_in_progress || mrioc->is_driver_loading)
		ssleep(1);
	mrioc->stop_drv_processing = 1;
	mpi3mr_cleanup_fwevt_list(mrioc);
	scsi_block_requests(shost);
	mpi3mr_stop_watchdog(mrioc);
	mpi3mr_cleanup_ioc(mrioc, 1);

	device_state = pci_choose_state(pdev, state);
	ioc_info(mrioc, "pdev=0x%p, slot=%s, entering operating state [D%d]\n",
	    pdev, pci_name(pdev), device_state);
	pci_save_state(pdev);
	pci_set_power_state(pdev, device_state);
	mpi3mr_cleanup_resources(mrioc);

	return 0;
}

/**
 * mpi3mr_resume - PCI power management resume callback
 * @pdev: PCI device instance
 *
 * Restore the power state to D0 and reinitialize the controller
 * and resume I/O operations to the target devices
 *
 * Return: 0 on success, non-zero on failure
 */
static int mpi3mr_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct mpi3mr_ioc *mrioc;
	pci_power_t device_state = pdev->current_state;
	int r;

	if (!shost)
		return 0;

	mrioc = shost_priv(shost);

	ioc_info(mrioc, "pdev=0x%p, slot=%s, previous operating state [D%d]\n",
	    pdev, pci_name(pdev), device_state);
	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);
	mrioc->pdev = pdev;
	mrioc->cpu_count = num_online_cpus();
	r = mpi3mr_setup_resources(mrioc);
	if (r) {
		ioc_info(mrioc, "%s: Setup resources failed[%d]\n",
		    __func__, r);
		return r;
	}

	mrioc->stop_drv_processing = 0;
	mpi3mr_init_ioc(mrioc, 1);
	scsi_unblock_requests(shost);
	mpi3mr_start_watchdog(mrioc);

	return 0;
}
#endif

static const struct pci_device_id mpi3mr_pci_id_table[] = {
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_LSI_LOGIC, 0x00A5,
		    PCI_ANY_ID, PCI_ANY_ID)
	},
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, mpi3mr_pci_id_table);

static struct pci_driver mpi3mr_pci_driver = {
	.name = MPI3MR_DRIVER_NAME,
	.id_table = mpi3mr_pci_id_table,
	.probe = mpi3mr_probe,
	.remove = mpi3mr_remove,
	.shutdown = mpi3mr_shutdown,
#ifdef CONFIG_PM
	.suspend = mpi3mr_suspend,
	.resume = mpi3mr_resume,
#endif
};

static int __init mpi3mr_init(void)
{
	int ret_val;

	pr_info("Loading %s version %s\n", MPI3MR_DRIVER_NAME,
	    MPI3MR_DRIVER_VERSION);

	ret_val = pci_register_driver(&mpi3mr_pci_driver);

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

	pci_unregister_driver(&mpi3mr_pci_driver);
}

module_init(mpi3mr_init);
module_exit(mpi3mr_exit);
