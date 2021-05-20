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
int logging_level;
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
	priv->chain_idx = -1;
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
	if (priv->chain_idx >= 0) {
		clear_bit(priv->chain_idx, mrioc->chain_bitmap);
		priv->chain_idx = -1;
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

	priv = scsi_cmd_priv(scmd);

	simple_sgl_flags = MPI3_SGE_FLAGS_ELEMENT_TYPE_SIMPLE |
	    MPI3_SGE_FLAGS_DLAS_SYSTEM;
	simple_sgl_flags_last = simple_sgl_flags |
	    MPI3_SGE_FLAGS_END_OF_LIST;
	last_chain_sgl_flags = MPI3_SGE_FLAGS_ELEMENT_TYPE_LAST_CHAIN |
	    MPI3_SGE_FLAGS_DLAS_SYSTEM;

	sg_local = &scsiio_req->sgl;

	if (!scsiio_req->data_length) {
		mpi3mr_build_zero_len_sge(sg_local);
		return 0;
	}

	sg_scmd = scsi_sglist(scmd);
	sges_left = scsi_dma_map(scmd);

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

	return ret;
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
	scsi_tgt_priv_data->starget = starget;
	scsi_tgt_priv_data->dev_handle = MPI3MR_INVALID_DEV_HANDLE;

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	tgt_dev = __mpi3mr_get_tgtdev_by_perst_id(mrioc, starget->id);
	if (tgt_dev && !tgt_dev->is_hidden) {
		starget->hostdata = scsi_tgt_priv_data;
		scsi_tgt_priv_data->starget = starget;
		scsi_tgt_priv_data->dev_handle = tgt_dev->dev_handle;
		scsi_tgt_priv_data->perst_id = tgt_dev->perst_id;
		scsi_tgt_priv_data->dev_type = tgt_dev->dev_type;
		scsi_tgt_priv_data->tgt_dev = tgt_dev;
		tgt_dev->starget = starget;
		atomic_set(&scsi_tgt_priv_data->block_io, 0);
		retval = 0;
	} else {
		kfree(scsi_tgt_priv_data);
		retval = -ENXIO;
	}
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);

	return retval;
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

	if (mrioc->stop_drv_processing) {
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

	mpi3mr_init_drv_cmd(&mrioc->init_cmds, MPI3MR_HOSTTAG_INITCMDS);

	for (i = 0; i < MPI3MR_NUM_DEVRMCMD; i++)
		mpi3mr_init_drv_cmd(&mrioc->dev_rmhs_cmds[i],
		    MPI3MR_HOSTTAG_DEVRMCMD_MIN + i);

	if (pdev->revision)
		mrioc->enable_segqueue = true;

	mrioc->logging_level = logging_level;
	mrioc->shost = shost;
	mrioc->pdev = pdev;

	/* init shost parameters */
	shost->max_cmd_len = MPI3MR_MAX_CDB_LENGTH;
	shost->max_lun = -1;
	shost->unique_id = mrioc->id;

	shost->max_channel = 1;
	shost->max_id = 0xFFFFFFFF;

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
	if (mpi3mr_init_ioc(mrioc)) {
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
	mpi3mr_cleanup_ioc(mrioc);
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
	mpi3mr_cleanup_ioc(mrioc);

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
	mpi3mr_cleanup_ioc(mrioc);
}

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
