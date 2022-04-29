// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2022 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#include "mpi3mr.h"
#include <linux/bsg-lib.h>
#include <uapi/scsi/scsi_bsg_mpi3mr.h>

/**
 * mpi3mr_bsg_verify_adapter - verify adapter number is valid
 * @ioc_number: Adapter number
 *
 * This function returns the adapter instance pointer of given
 * adapter number. If adapter number does not match with the
 * driver's adapter list, driver returns NULL.
 *
 * Return: adapter instance reference
 */
static struct mpi3mr_ioc *mpi3mr_bsg_verify_adapter(int ioc_number)
{
	struct mpi3mr_ioc *mrioc = NULL;

	spin_lock(&mrioc_list_lock);
	list_for_each_entry(mrioc, &mrioc_list, list) {
		if (mrioc->id == ioc_number) {
			spin_unlock(&mrioc_list_lock);
			return mrioc;
		}
	}
	spin_unlock(&mrioc_list_lock);
	return NULL;
}

/**
 * mpi3mr_enable_logdata - Handler for log data enable
 * @mrioc: Adapter instance reference
 * @job: BSG job reference
 *
 * This function enables log data caching in the driver if not
 * already enabled and return the maximum number of log data
 * entries that can be cached in the driver.
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_enable_logdata(struct mpi3mr_ioc *mrioc,
	struct bsg_job *job)
{
	struct mpi3mr_logdata_enable logdata_enable;

	if (!mrioc->logdata_buf) {
		mrioc->logdata_entry_sz =
		    (mrioc->reply_sz - (sizeof(struct mpi3_event_notification_reply) - 4))
		    + MPI3MR_BSG_LOGDATA_ENTRY_HEADER_SZ;
		mrioc->logdata_buf_idx = 0;
		mrioc->logdata_buf = kcalloc(MPI3MR_BSG_LOGDATA_MAX_ENTRIES,
		    mrioc->logdata_entry_sz, GFP_KERNEL);

		if (!mrioc->logdata_buf)
			return -ENOMEM;
	}

	memset(&logdata_enable, 0, sizeof(logdata_enable));
	logdata_enable.max_entries =
	    MPI3MR_BSG_LOGDATA_MAX_ENTRIES;
	if (job->request_payload.payload_len >= sizeof(logdata_enable)) {
		sg_copy_from_buffer(job->request_payload.sg_list,
				    job->request_payload.sg_cnt,
				    &logdata_enable, sizeof(logdata_enable));
		return 0;
	}

	return -EINVAL;
}
/**
 * mpi3mr_get_logdata - Handler for get log data
 * @mrioc: Adapter instance reference
 * @job: BSG job pointer
 * This function copies the log data entries to the user buffer
 * when log caching is enabled in the driver.
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_get_logdata(struct mpi3mr_ioc *mrioc,
	struct bsg_job *job)
{
	u16 num_entries, sz, entry_sz = mrioc->logdata_entry_sz;

	if ((!mrioc->logdata_buf) || (job->request_payload.payload_len < entry_sz))
		return -EINVAL;

	num_entries = job->request_payload.payload_len / entry_sz;
	if (num_entries > MPI3MR_BSG_LOGDATA_MAX_ENTRIES)
		num_entries = MPI3MR_BSG_LOGDATA_MAX_ENTRIES;
	sz = num_entries * entry_sz;

	if (job->request_payload.payload_len >= sz) {
		sg_copy_from_buffer(job->request_payload.sg_list,
				    job->request_payload.sg_cnt,
				    mrioc->logdata_buf, sz);
		return 0;
	}
	return -EINVAL;
}

/**
 * mpi3mr_get_all_tgt_info - Get all target information
 * @mrioc: Adapter instance reference
 * @job: BSG job reference
 *
 * This function copies the driver managed target devices device
 * handle, persistent ID, bus ID and taret ID to the user
 * provided buffer for the specific controller. This function
 * also provides the number of devices managed by the driver for
 * the specific controller.
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_get_all_tgt_info(struct mpi3mr_ioc *mrioc,
	struct bsg_job *job)
{
	long rval = -EINVAL;
	u16 num_devices = 0, i = 0, size;
	unsigned long flags;
	struct mpi3mr_tgt_dev *tgtdev;
	struct mpi3mr_device_map_info *devmap_info = NULL;
	struct mpi3mr_all_tgt_info *alltgt_info = NULL;
	uint32_t min_entrylen = 0, kern_entrylen = 0, usr_entrylen = 0;

	if (job->request_payload.payload_len < sizeof(u32)) {
		dprint_bsg_err(mrioc, "%s: invalid size argument\n",
		    __func__);
		return rval;
	}

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list)
		num_devices++;
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);

	if ((job->request_payload.payload_len == sizeof(u32)) ||
		list_empty(&mrioc->tgtdev_list)) {
		sg_copy_from_buffer(job->request_payload.sg_list,
				    job->request_payload.sg_cnt,
				    &num_devices, sizeof(num_devices));
		return 0;
	}

	kern_entrylen = (num_devices - 1) * sizeof(*devmap_info);
	size = sizeof(*alltgt_info) + kern_entrylen;
	alltgt_info = kzalloc(size, GFP_KERNEL);
	if (!alltgt_info)
		return -ENOMEM;

	devmap_info = alltgt_info->dmi;
	memset((u8 *)devmap_info, 0xFF, (kern_entrylen + sizeof(*devmap_info)));
	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list) {
		if (i < num_devices) {
			devmap_info[i].handle = tgtdev->dev_handle;
			devmap_info[i].perst_id = tgtdev->perst_id;
			if (tgtdev->host_exposed && tgtdev->starget) {
				devmap_info[i].target_id = tgtdev->starget->id;
				devmap_info[i].bus_id =
				    tgtdev->starget->channel;
			}
			i++;
		}
	}
	num_devices = i;
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);

	memcpy(&alltgt_info->num_devices, &num_devices, sizeof(num_devices));

	usr_entrylen = (job->request_payload.payload_len - sizeof(u32)) / sizeof(*devmap_info);
	usr_entrylen *= sizeof(*devmap_info);
	min_entrylen = min(usr_entrylen, kern_entrylen);
	if (min_entrylen && (!memcpy(&alltgt_info->dmi, devmap_info, min_entrylen))) {
		dprint_bsg_err(mrioc, "%s:%d: device map info copy failed\n",
		    __func__, __LINE__);
		rval = -EFAULT;
		goto out;
	}

	sg_copy_from_buffer(job->request_payload.sg_list,
			    job->request_payload.sg_cnt,
			    alltgt_info, job->request_payload.payload_len);
	rval = 0;
out:
	kfree(alltgt_info);
	return rval;
}

/**
 * mpi3mr_get_change_count - Get topology change count
 * @mrioc: Adapter instance reference
 * @job: BSG job reference
 *
 * This function copies the toplogy change count provided by the
 * driver in events and cached in the driver to the user
 * provided buffer for the specific controller.
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_get_change_count(struct mpi3mr_ioc *mrioc,
	struct bsg_job *job)
{
	struct mpi3mr_change_count chgcnt;

	memset(&chgcnt, 0, sizeof(chgcnt));
	chgcnt.change_count = mrioc->change_count;
	if (job->request_payload.payload_len >= sizeof(chgcnt)) {
		sg_copy_from_buffer(job->request_payload.sg_list,
				    job->request_payload.sg_cnt,
				    &chgcnt, sizeof(chgcnt));
		return 0;
	}
	return -EINVAL;
}

/**
 * mpi3mr_bsg_adp_reset - Issue controller reset
 * @mrioc: Adapter instance reference
 * @job: BSG job reference
 *
 * This function identifies the user provided reset type and
 * issues approporiate reset to the controller and wait for that
 * to complete and reinitialize the controller and then returns
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_bsg_adp_reset(struct mpi3mr_ioc *mrioc,
	struct bsg_job *job)
{
	long rval = -EINVAL;
	u8 save_snapdump;
	struct mpi3mr_bsg_adp_reset adpreset;

	if (job->request_payload.payload_len !=
			sizeof(adpreset)) {
		dprint_bsg_err(mrioc, "%s: invalid size argument\n",
		    __func__);
		goto out;
	}

	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt,
			  &adpreset, sizeof(adpreset));

	switch (adpreset.reset_type) {
	case MPI3MR_BSG_ADPRESET_SOFT:
		save_snapdump = 0;
		break;
	case MPI3MR_BSG_ADPRESET_DIAG_FAULT:
		save_snapdump = 1;
		break;
	default:
		dprint_bsg_err(mrioc, "%s: unknown reset_type(%d)\n",
		    __func__, adpreset.reset_type);
		goto out;
	}

	rval = mpi3mr_soft_reset_handler(mrioc, MPI3MR_RESET_FROM_APP,
	    save_snapdump);

	if (rval)
		dprint_bsg_err(mrioc,
		    "%s: reset handler returned error(%ld) for reset type %d\n",
		    __func__, rval, adpreset.reset_type);
out:
	return rval;
}

/**
 * mpi3mr_bsg_populate_adpinfo - Get adapter info command handler
 * @mrioc: Adapter instance reference
 * @job: BSG job reference
 *
 * This function provides adapter information for the given
 * controller
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_bsg_populate_adpinfo(struct mpi3mr_ioc *mrioc,
	struct bsg_job *job)
{
	enum mpi3mr_iocstate ioc_state;
	struct mpi3mr_bsg_in_adpinfo adpinfo;

	memset(&adpinfo, 0, sizeof(adpinfo));
	adpinfo.adp_type = MPI3MR_BSG_ADPTYPE_AVGFAMILY;
	adpinfo.pci_dev_id = mrioc->pdev->device;
	adpinfo.pci_dev_hw_rev = mrioc->pdev->revision;
	adpinfo.pci_subsys_dev_id = mrioc->pdev->subsystem_device;
	adpinfo.pci_subsys_ven_id = mrioc->pdev->subsystem_vendor;
	adpinfo.pci_bus = mrioc->pdev->bus->number;
	adpinfo.pci_dev = PCI_SLOT(mrioc->pdev->devfn);
	adpinfo.pci_func = PCI_FUNC(mrioc->pdev->devfn);
	adpinfo.pci_seg_id = pci_domain_nr(mrioc->pdev->bus);
	adpinfo.app_intfc_ver = MPI3MR_IOCTL_VERSION;

	ioc_state = mpi3mr_get_iocstate(mrioc);
	if (ioc_state == MRIOC_STATE_UNRECOVERABLE)
		adpinfo.adp_state = MPI3MR_BSG_ADPSTATE_UNRECOVERABLE;
	else if ((mrioc->reset_in_progress) || (mrioc->stop_bsgs))
		adpinfo.adp_state = MPI3MR_BSG_ADPSTATE_IN_RESET;
	else if (ioc_state == MRIOC_STATE_FAULT)
		adpinfo.adp_state = MPI3MR_BSG_ADPSTATE_FAULT;
	else
		adpinfo.adp_state = MPI3MR_BSG_ADPSTATE_OPERATIONAL;

	memcpy((u8 *)&adpinfo.driver_info, (u8 *)&mrioc->driver_info,
	    sizeof(adpinfo.driver_info));

	if (job->request_payload.payload_len >= sizeof(adpinfo)) {
		sg_copy_from_buffer(job->request_payload.sg_list,
				    job->request_payload.sg_cnt,
				    &adpinfo, sizeof(adpinfo));
		return 0;
	}
	return -EINVAL;
}

/**
 * mpi3mr_bsg_process_drv_cmds - Driver Command handler
 * @job: BSG job reference
 *
 * This function is the top level handler for driver commands,
 * this does basic validation of the buffer and identifies the
 * opcode and switches to correct sub handler.
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_bsg_process_drv_cmds(struct bsg_job *job)
{
	long rval = -EINVAL;
	struct mpi3mr_ioc *mrioc = NULL;
	struct mpi3mr_bsg_packet *bsg_req = NULL;
	struct mpi3mr_bsg_drv_cmd *drvrcmd = NULL;

	bsg_req = job->request;
	drvrcmd = &bsg_req->cmd.drvrcmd;

	mrioc = mpi3mr_bsg_verify_adapter(drvrcmd->mrioc_id);
	if (!mrioc)
		return -ENODEV;

	if (drvrcmd->opcode == MPI3MR_DRVBSG_OPCODE_ADPINFO) {
		rval = mpi3mr_bsg_populate_adpinfo(mrioc, job);
		return rval;
	}

	if (mutex_lock_interruptible(&mrioc->bsg_cmds.mutex))
		return -ERESTARTSYS;

	switch (drvrcmd->opcode) {
	case MPI3MR_DRVBSG_OPCODE_ADPRESET:
		rval = mpi3mr_bsg_adp_reset(mrioc, job);
		break;
	case MPI3MR_DRVBSG_OPCODE_ALLTGTDEVINFO:
		rval = mpi3mr_get_all_tgt_info(mrioc, job);
		break;
	case MPI3MR_DRVBSG_OPCODE_GETCHGCNT:
		rval = mpi3mr_get_change_count(mrioc, job);
		break;
	case MPI3MR_DRVBSG_OPCODE_LOGDATAENABLE:
		rval = mpi3mr_enable_logdata(mrioc, job);
		break;
	case MPI3MR_DRVBSG_OPCODE_GETLOGDATA:
		rval = mpi3mr_get_logdata(mrioc, job);
		break;
	case MPI3MR_DRVBSG_OPCODE_UNKNOWN:
	default:
		pr_err("%s: unsupported driver command opcode %d\n",
		    MPI3MR_DRIVER_NAME, drvrcmd->opcode);
		break;
	}
	mutex_unlock(&mrioc->bsg_cmds.mutex);
	return rval;
}

/**
 * mpi3mr_bsg_request - bsg request entry point
 * @job: BSG job reference
 *
 * This is driver's entry point for bsg requests
 *
 * Return: 0 on success and proper error codes on failure
 */
static int mpi3mr_bsg_request(struct bsg_job *job)
{
	long rval = -EINVAL;
	unsigned int reply_payload_rcv_len = 0;

	struct mpi3mr_bsg_packet *bsg_req = job->request;

	switch (bsg_req->cmd_type) {
	case MPI3MR_DRV_CMD:
		rval = mpi3mr_bsg_process_drv_cmds(job);
		break;
	default:
		pr_err("%s: unsupported BSG command(0x%08x)\n",
		    MPI3MR_DRIVER_NAME, bsg_req->cmd_type);
		break;
	}

	bsg_job_done(job, rval, reply_payload_rcv_len);

	return 0;
}

/**
 * mpi3mr_bsg_exit - de-registration from bsg layer
 *
 * This will be called during driver unload and all
 * bsg resources allocated during load will be freed.
 *
 * Return:Nothing
 */
void mpi3mr_bsg_exit(struct mpi3mr_ioc *mrioc)
{
	if (!mrioc->bsg_queue)
		return;

	bsg_remove_queue(mrioc->bsg_queue);
	mrioc->bsg_queue = NULL;

	device_del(mrioc->bsg_dev);
	put_device(mrioc->bsg_dev);
	kfree(mrioc->bsg_dev);
}

/**
 * mpi3mr_bsg_node_release -release bsg device node
 * @dev: bsg device node
 *
 * decrements bsg dev reference count
 *
 * Return:Nothing
 */
static void mpi3mr_bsg_node_release(struct device *dev)
{
	put_device(dev);
}

/**
 * mpi3mr_bsg_init -  registration with bsg layer
 *
 * This will be called during driver load and it will
 * register driver with bsg layer
 *
 * Return:Nothing
 */
void mpi3mr_bsg_init(struct mpi3mr_ioc *mrioc)
{
	mrioc->bsg_dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!mrioc->bsg_dev) {
		ioc_err(mrioc, "bsg device mem allocation failed\n");
		return;
	}

	device_initialize(mrioc->bsg_dev);
	dev_set_name(mrioc->bsg_dev, "mpi3mrctl%u", mrioc->id);

	if (device_add(mrioc->bsg_dev)) {
		ioc_err(mrioc, "%s: bsg device add failed\n",
		    dev_name(mrioc->bsg_dev));
		goto err_device_add;
	}

	mrioc->bsg_dev->release = mpi3mr_bsg_node_release;

	mrioc->bsg_queue = bsg_setup_queue(mrioc->bsg_dev, dev_name(mrioc->bsg_dev),
			mpi3mr_bsg_request, NULL, 0);
	if (!mrioc->bsg_queue) {
		ioc_err(mrioc, "%s: bsg registration failed\n",
		    dev_name(mrioc->bsg_dev));
		goto err_setup_queue;
	}

	blk_queue_max_segments(mrioc->bsg_queue, MPI3MR_MAX_APP_XFER_SEGMENTS);
	blk_queue_max_hw_sectors(mrioc->bsg_queue, MPI3MR_MAX_APP_XFER_SECTORS);

	return;

err_setup_queue:
	device_del(mrioc->bsg_dev);
	put_device(mrioc->bsg_dev);
err_device_add:
	kfree(mrioc->bsg_dev);
}
