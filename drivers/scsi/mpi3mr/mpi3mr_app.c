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
 * mpi3mr_bsg_pel_abort - sends PEL abort request
 * @mrioc: Adapter instance reference
 *
 * This function sends PEL abort request to the firmware through
 * admin request queue.
 *
 * Return: 0 on success, -1 on failure
 */
static int mpi3mr_bsg_pel_abort(struct mpi3mr_ioc *mrioc)
{
	struct mpi3_pel_req_action_abort pel_abort_req;
	struct mpi3_pel_reply *pel_reply;
	int retval = 0;
	u16 pe_log_status;

	if (mrioc->reset_in_progress) {
		dprint_bsg_err(mrioc, "%s: reset in progress\n", __func__);
		return -1;
	}
	if (mrioc->stop_bsgs) {
		dprint_bsg_err(mrioc, "%s: bsgs are blocked\n", __func__);
		return -1;
	}

	memset(&pel_abort_req, 0, sizeof(pel_abort_req));
	mutex_lock(&mrioc->pel_abort_cmd.mutex);
	if (mrioc->pel_abort_cmd.state & MPI3MR_CMD_PENDING) {
		dprint_bsg_err(mrioc, "%s: command is in use\n", __func__);
		mutex_unlock(&mrioc->pel_abort_cmd.mutex);
		return -1;
	}
	mrioc->pel_abort_cmd.state = MPI3MR_CMD_PENDING;
	mrioc->pel_abort_cmd.is_waiting = 1;
	mrioc->pel_abort_cmd.callback = NULL;
	pel_abort_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_PEL_ABORT);
	pel_abort_req.function = MPI3_FUNCTION_PERSISTENT_EVENT_LOG;
	pel_abort_req.action = MPI3_PEL_ACTION_ABORT;
	pel_abort_req.abort_host_tag = cpu_to_le16(MPI3MR_HOSTTAG_PEL_WAIT);

	mrioc->pel_abort_requested = 1;
	init_completion(&mrioc->pel_abort_cmd.done);
	retval = mpi3mr_admin_request_post(mrioc, &pel_abort_req,
	    sizeof(pel_abort_req), 0);
	if (retval) {
		retval = -1;
		dprint_bsg_err(mrioc, "%s: admin request post failed\n",
		    __func__);
		mrioc->pel_abort_requested = 0;
		goto out_unlock;
	}

	wait_for_completion_timeout(&mrioc->pel_abort_cmd.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->pel_abort_cmd.state & MPI3MR_CMD_COMPLETE)) {
		mrioc->pel_abort_cmd.is_waiting = 0;
		dprint_bsg_err(mrioc, "%s: command timedout\n", __func__);
		if (!(mrioc->pel_abort_cmd.state & MPI3MR_CMD_RESET))
			mpi3mr_soft_reset_handler(mrioc,
			    MPI3MR_RESET_FROM_PELABORT_TIMEOUT, 1);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->pel_abort_cmd.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	     != MPI3_IOCSTATUS_SUCCESS) {
		dprint_bsg_err(mrioc,
		    "%s: command failed, ioc_status(0x%04x) log_info(0x%08x)\n",
		    __func__, (mrioc->pel_abort_cmd.ioc_status &
		    MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->pel_abort_cmd.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}
	if (mrioc->pel_abort_cmd.state & MPI3MR_CMD_REPLY_VALID) {
		pel_reply = (struct mpi3_pel_reply *)mrioc->pel_abort_cmd.reply;
		pe_log_status = le16_to_cpu(pel_reply->pe_log_status);
		if (pe_log_status != MPI3_PEL_STATUS_SUCCESS) {
			dprint_bsg_err(mrioc,
			    "%s: command failed, pel_status(0x%04x)\n",
			    __func__, pe_log_status);
			retval = -1;
		}
	}

out_unlock:
	mrioc->pel_abort_cmd.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->pel_abort_cmd.mutex);
	return retval;
}
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
 * mpi3mr_bsg_pel_enable - Handler for PEL enable driver
 * @mrioc: Adapter instance reference
 * @job: BSG job pointer
 *
 * This function is the handler for PEL enable driver.
 * Validates the application given class and locale and if
 * requires aborts the existing PEL wait request and/or issues
 * new PEL wait request to the firmware and returns.
 *
 * Return: 0 on success and proper error codes on failure.
 */
static long mpi3mr_bsg_pel_enable(struct mpi3mr_ioc *mrioc,
				  struct bsg_job *job)
{
	long rval = -EINVAL;
	struct mpi3mr_bsg_out_pel_enable pel_enable;
	u8 issue_pel_wait;
	u8 tmp_class;
	u16 tmp_locale;

	if (job->request_payload.payload_len != sizeof(pel_enable)) {
		dprint_bsg_err(mrioc, "%s: invalid size argument\n",
		    __func__);
		return rval;
	}

	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt,
			  &pel_enable, sizeof(pel_enable));

	if (pel_enable.pel_class > MPI3_PEL_CLASS_FAULT) {
		dprint_bsg_err(mrioc, "%s: out of range class %d sent\n",
			__func__, pel_enable.pel_class);
		rval = 0;
		goto out;
	}
	if (!mrioc->pel_enabled)
		issue_pel_wait = 1;
	else {
		if ((mrioc->pel_class <= pel_enable.pel_class) &&
		    !((mrioc->pel_locale & pel_enable.pel_locale) ^
		      pel_enable.pel_locale)) {
			issue_pel_wait = 0;
			rval = 0;
		} else {
			pel_enable.pel_locale |= mrioc->pel_locale;

			if (mrioc->pel_class < pel_enable.pel_class)
				pel_enable.pel_class = mrioc->pel_class;

			rval = mpi3mr_bsg_pel_abort(mrioc);
			if (rval) {
				dprint_bsg_err(mrioc,
				    "%s: pel_abort failed, status(%ld)\n",
				    __func__, rval);
				goto out;
			}
			issue_pel_wait = 1;
		}
	}
	if (issue_pel_wait) {
		tmp_class = mrioc->pel_class;
		tmp_locale = mrioc->pel_locale;
		mrioc->pel_class = pel_enable.pel_class;
		mrioc->pel_locale = pel_enable.pel_locale;
		mrioc->pel_enabled = 1;
		rval = mpi3mr_pel_get_seqnum_post(mrioc, NULL);
		if (rval) {
			mrioc->pel_class = tmp_class;
			mrioc->pel_locale = tmp_locale;
			mrioc->pel_enabled = 0;
			dprint_bsg_err(mrioc,
			    "%s: pel get sequence number failed, status(%ld)\n",
			    __func__, rval);
		}
	}

out:
	return rval;
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
	case MPI3MR_DRVBSG_OPCODE_PELENABLE:
		rval = mpi3mr_bsg_pel_enable(mrioc, job);
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
 * mpi3mr_bsg_build_sgl - SGL construction for MPI commands
 * @mpi_req: MPI request
 * @sgl_offset: offset to start sgl in the MPI request
 * @drv_bufs: DMA address of the buffers to be placed in sgl
 * @bufcnt: Number of DMA buffers
 * @is_rmc: Does the buffer list has management command buffer
 * @is_rmr: Does the buffer list has management response buffer
 * @num_datasges: Number of data buffers in the list
 *
 * This function places the DMA address of the given buffers in
 * proper format as SGEs in the given MPI request.
 *
 * Return: Nothing
 */
static void mpi3mr_bsg_build_sgl(u8 *mpi_req, uint32_t sgl_offset,
	struct mpi3mr_buf_map *drv_bufs, u8 bufcnt, u8 is_rmc,
	u8 is_rmr, u8 num_datasges)
{
	u8 *sgl = (mpi_req + sgl_offset), count = 0;
	struct mpi3_mgmt_passthrough_request *rmgmt_req =
	    (struct mpi3_mgmt_passthrough_request *)mpi_req;
	struct mpi3mr_buf_map *drv_buf_iter = drv_bufs;
	u8 sgl_flags, sgl_flags_last;

	sgl_flags = MPI3_SGE_FLAGS_ELEMENT_TYPE_SIMPLE |
		MPI3_SGE_FLAGS_DLAS_SYSTEM | MPI3_SGE_FLAGS_END_OF_BUFFER;
	sgl_flags_last = sgl_flags | MPI3_SGE_FLAGS_END_OF_LIST;

	if (is_rmc) {
		mpi3mr_add_sg_single(&rmgmt_req->command_sgl,
		    sgl_flags_last, drv_buf_iter->kern_buf_len,
		    drv_buf_iter->kern_buf_dma);
		sgl = (u8 *)drv_buf_iter->kern_buf + drv_buf_iter->bsg_buf_len;
		drv_buf_iter++;
		count++;
		if (is_rmr) {
			mpi3mr_add_sg_single(&rmgmt_req->response_sgl,
			    sgl_flags_last, drv_buf_iter->kern_buf_len,
			    drv_buf_iter->kern_buf_dma);
			drv_buf_iter++;
			count++;
		} else
			mpi3mr_build_zero_len_sge(
			    &rmgmt_req->response_sgl);
	}
	if (!num_datasges) {
		mpi3mr_build_zero_len_sge(sgl);
		return;
	}
	for (; count < bufcnt; count++, drv_buf_iter++) {
		if (drv_buf_iter->data_dir == DMA_NONE)
			continue;
		if (num_datasges == 1 || !is_rmc)
			mpi3mr_add_sg_single(sgl, sgl_flags_last,
			    drv_buf_iter->kern_buf_len, drv_buf_iter->kern_buf_dma);
		else
			mpi3mr_add_sg_single(sgl, sgl_flags,
			    drv_buf_iter->kern_buf_len, drv_buf_iter->kern_buf_dma);
		sgl += sizeof(struct mpi3_sge_common);
		num_datasges--;
	}
}

/**
 * mpi3mr_get_nvme_data_fmt - returns the NVMe data format
 * @nvme_encap_request: NVMe encapsulated MPI request
 *
 * This function returns the type of the data format specified
 * in user provided NVMe command in NVMe encapsulated request.
 *
 * Return: Data format of the NVMe command (PRP/SGL etc)
 */
static unsigned int mpi3mr_get_nvme_data_fmt(
	struct mpi3_nvme_encapsulated_request *nvme_encap_request)
{
	u8 format = 0;

	format = ((nvme_encap_request->command[0] & 0xc000) >> 14);
	return format;

}

/**
 * mpi3mr_build_nvme_sgl - SGL constructor for NVME
 *				   encapsulated request
 * @mrioc: Adapter instance reference
 * @nvme_encap_request: NVMe encapsulated MPI request
 * @drv_bufs: DMA address of the buffers to be placed in sgl
 * @bufcnt: Number of DMA buffers
 *
 * This function places the DMA address of the given buffers in
 * proper format as SGEs in the given NVMe encapsulated request.
 *
 * Return: 0 on success, -1 on failure
 */
static int mpi3mr_build_nvme_sgl(struct mpi3mr_ioc *mrioc,
	struct mpi3_nvme_encapsulated_request *nvme_encap_request,
	struct mpi3mr_buf_map *drv_bufs, u8 bufcnt)
{
	struct mpi3mr_nvme_pt_sge *nvme_sgl;
	u64 sgl_ptr;
	u8 count;
	size_t length = 0;
	struct mpi3mr_buf_map *drv_buf_iter = drv_bufs;
	u64 sgemod_mask = ((u64)((mrioc->facts.sge_mod_mask) <<
			    mrioc->facts.sge_mod_shift) << 32);
	u64 sgemod_val = ((u64)(mrioc->facts.sge_mod_value) <<
			  mrioc->facts.sge_mod_shift) << 32;

	/*
	 * Not all commands require a data transfer. If no data, just return
	 * without constructing any sgl.
	 */
	for (count = 0; count < bufcnt; count++, drv_buf_iter++) {
		if (drv_buf_iter->data_dir == DMA_NONE)
			continue;
		sgl_ptr = (u64)drv_buf_iter->kern_buf_dma;
		length = drv_buf_iter->kern_buf_len;
		break;
	}
	if (!length)
		return 0;

	if (sgl_ptr & sgemod_mask) {
		dprint_bsg_err(mrioc,
		    "%s: SGL address collides with SGE modifier\n",
		    __func__);
		return -1;
	}

	sgl_ptr &= ~sgemod_mask;
	sgl_ptr |= sgemod_val;
	nvme_sgl = (struct mpi3mr_nvme_pt_sge *)
	    ((u8 *)(nvme_encap_request->command) + MPI3MR_NVME_CMD_SGL_OFFSET);
	memset(nvme_sgl, 0, sizeof(struct mpi3mr_nvme_pt_sge));
	nvme_sgl->base_addr = sgl_ptr;
	nvme_sgl->length = length;
	return 0;
}

/**
 * mpi3mr_build_nvme_prp - PRP constructor for NVME
 *			       encapsulated request
 * @mrioc: Adapter instance reference
 * @nvme_encap_request: NVMe encapsulated MPI request
 * @drv_bufs: DMA address of the buffers to be placed in SGL
 * @bufcnt: Number of DMA buffers
 *
 * This function places the DMA address of the given buffers in
 * proper format as PRP entries in the given NVMe encapsulated
 * request.
 *
 * Return: 0 on success, -1 on failure
 */
static int mpi3mr_build_nvme_prp(struct mpi3mr_ioc *mrioc,
	struct mpi3_nvme_encapsulated_request *nvme_encap_request,
	struct mpi3mr_buf_map *drv_bufs, u8 bufcnt)
{
	int prp_size = MPI3MR_NVME_PRP_SIZE;
	__le64 *prp_entry, *prp1_entry, *prp2_entry;
	__le64 *prp_page;
	dma_addr_t prp_entry_dma, prp_page_dma, dma_addr;
	u32 offset, entry_len, dev_pgsz;
	u32 page_mask_result, page_mask;
	size_t length = 0;
	u8 count;
	struct mpi3mr_buf_map *drv_buf_iter = drv_bufs;
	u64 sgemod_mask = ((u64)((mrioc->facts.sge_mod_mask) <<
			    mrioc->facts.sge_mod_shift) << 32);
	u64 sgemod_val = ((u64)(mrioc->facts.sge_mod_value) <<
			  mrioc->facts.sge_mod_shift) << 32;
	u16 dev_handle = nvme_encap_request->dev_handle;
	struct mpi3mr_tgt_dev *tgtdev;

	tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, dev_handle);
	if (!tgtdev) {
		dprint_bsg_err(mrioc, "%s: invalid device handle 0x%04x\n",
			__func__, dev_handle);
		return -1;
	}

	if (tgtdev->dev_spec.pcie_inf.pgsz == 0) {
		dprint_bsg_err(mrioc,
		    "%s: NVMe device page size is zero for handle 0x%04x\n",
		    __func__, dev_handle);
		mpi3mr_tgtdev_put(tgtdev);
		return -1;
	}

	dev_pgsz = 1 << (tgtdev->dev_spec.pcie_inf.pgsz);
	mpi3mr_tgtdev_put(tgtdev);

	/*
	 * Not all commands require a data transfer. If no data, just return
	 * without constructing any PRP.
	 */
	for (count = 0; count < bufcnt; count++, drv_buf_iter++) {
		if (drv_buf_iter->data_dir == DMA_NONE)
			continue;
		dma_addr = drv_buf_iter->kern_buf_dma;
		length = drv_buf_iter->kern_buf_len;
		break;
	}

	if (!length)
		return 0;

	mrioc->prp_sz = 0;
	mrioc->prp_list_virt = dma_alloc_coherent(&mrioc->pdev->dev,
	    dev_pgsz, &mrioc->prp_list_dma, GFP_KERNEL);

	if (!mrioc->prp_list_virt)
		return -1;
	mrioc->prp_sz = dev_pgsz;

	/*
	 * Set pointers to PRP1 and PRP2, which are in the NVMe command.
	 * PRP1 is located at a 24 byte offset from the start of the NVMe
	 * command.  Then set the current PRP entry pointer to PRP1.
	 */
	prp1_entry = (__le64 *)((u8 *)(nvme_encap_request->command) +
	    MPI3MR_NVME_CMD_PRP1_OFFSET);
	prp2_entry = (__le64 *)((u8 *)(nvme_encap_request->command) +
	    MPI3MR_NVME_CMD_PRP2_OFFSET);
	prp_entry = prp1_entry;
	/*
	 * For the PRP entries, use the specially allocated buffer of
	 * contiguous memory.
	 */
	prp_page = (__le64 *)mrioc->prp_list_virt;
	prp_page_dma = mrioc->prp_list_dma;

	/*
	 * Check if we are within 1 entry of a page boundary we don't
	 * want our first entry to be a PRP List entry.
	 */
	page_mask = dev_pgsz - 1;
	page_mask_result = (uintptr_t)((u8 *)prp_page + prp_size) & page_mask;
	if (!page_mask_result) {
		dprint_bsg_err(mrioc, "%s: PRP page is not page aligned\n",
		    __func__);
		goto err_out;
	}

	/*
	 * Set PRP physical pointer, which initially points to the current PRP
	 * DMA memory page.
	 */
	prp_entry_dma = prp_page_dma;


	/* Loop while the length is not zero. */
	while (length) {
		page_mask_result = (prp_entry_dma + prp_size) & page_mask;
		if (!page_mask_result && (length >  dev_pgsz)) {
			dprint_bsg_err(mrioc,
			    "%s: single PRP page is not sufficient\n",
			    __func__);
			goto err_out;
		}

		/* Need to handle if entry will be part of a page. */
		offset = dma_addr & page_mask;
		entry_len = dev_pgsz - offset;

		if (prp_entry == prp1_entry) {
			/*
			 * Must fill in the first PRP pointer (PRP1) before
			 * moving on.
			 */
			*prp1_entry = cpu_to_le64(dma_addr);
			if (*prp1_entry & sgemod_mask) {
				dprint_bsg_err(mrioc,
				    "%s: PRP1 address collides with SGE modifier\n",
				    __func__);
				goto err_out;
			}
			*prp1_entry &= ~sgemod_mask;
			*prp1_entry |= sgemod_val;

			/*
			 * Now point to the second PRP entry within the
			 * command (PRP2).
			 */
			prp_entry = prp2_entry;
		} else if (prp_entry == prp2_entry) {
			/*
			 * Should the PRP2 entry be a PRP List pointer or just
			 * a regular PRP pointer?  If there is more than one
			 * more page of data, must use a PRP List pointer.
			 */
			if (length > dev_pgsz) {
				/*
				 * PRP2 will contain a PRP List pointer because
				 * more PRP's are needed with this command. The
				 * list will start at the beginning of the
				 * contiguous buffer.
				 */
				*prp2_entry = cpu_to_le64(prp_entry_dma);
				if (*prp2_entry & sgemod_mask) {
					dprint_bsg_err(mrioc,
					    "%s: PRP list address collides with SGE modifier\n",
					    __func__);
					goto err_out;
				}
				*prp2_entry &= ~sgemod_mask;
				*prp2_entry |= sgemod_val;

				/*
				 * The next PRP Entry will be the start of the
				 * first PRP List.
				 */
				prp_entry = prp_page;
				continue;
			} else {
				/*
				 * After this, the PRP Entries are complete.
				 * This command uses 2 PRP's and no PRP list.
				 */
				*prp2_entry = cpu_to_le64(dma_addr);
				if (*prp2_entry & sgemod_mask) {
					dprint_bsg_err(mrioc,
					    "%s: PRP2 collides with SGE modifier\n",
					    __func__);
					goto err_out;
				}
				*prp2_entry &= ~sgemod_mask;
				*prp2_entry |= sgemod_val;
			}
		} else {
			/*
			 * Put entry in list and bump the addresses.
			 *
			 * After PRP1 and PRP2 are filled in, this will fill in
			 * all remaining PRP entries in a PRP List, one per
			 * each time through the loop.
			 */
			*prp_entry = cpu_to_le64(dma_addr);
			if (*prp1_entry & sgemod_mask) {
				dprint_bsg_err(mrioc,
				    "%s: PRP address collides with SGE modifier\n",
				    __func__);
				goto err_out;
			}
			*prp_entry &= ~sgemod_mask;
			*prp_entry |= sgemod_val;
			prp_entry++;
			prp_entry_dma++;
		}

		/*
		 * Bump the phys address of the command's data buffer by the
		 * entry_len.
		 */
		dma_addr += entry_len;

		/* decrement length accounting for last partial page. */
		if (entry_len > length)
			length = 0;
		else
			length -= entry_len;
	}
	return 0;
err_out:
	if (mrioc->prp_list_virt) {
		dma_free_coherent(&mrioc->pdev->dev, mrioc->prp_sz,
		    mrioc->prp_list_virt, mrioc->prp_list_dma);
		mrioc->prp_list_virt = NULL;
	}
	return -1;
}
/**
 * mpi3mr_bsg_process_mpt_cmds - MPI Pass through BSG handler
 * @job: BSG job reference
 *
 * This function is the top level handler for MPI Pass through
 * command, this does basic validation of the input data buffers,
 * identifies the given buffer types and MPI command, allocates
 * DMAable memory for user given buffers, construstcs SGL
 * properly and passes the command to the firmware.
 *
 * Once the MPI command is completed the driver copies the data
 * if any and reply, sense information to user provided buffers.
 * If the command is timed out then issues controller reset
 * prior to returning.
 *
 * Return: 0 on success and proper error codes on failure
 */

static long mpi3mr_bsg_process_mpt_cmds(struct bsg_job *job, unsigned int *reply_payload_rcv_len)
{
	long rval = -EINVAL;

	struct mpi3mr_ioc *mrioc = NULL;
	u8 *mpi_req = NULL, *sense_buff_k = NULL;
	u8 mpi_msg_size = 0;
	struct mpi3mr_bsg_packet *bsg_req = NULL;
	struct mpi3mr_bsg_mptcmd *karg;
	struct mpi3mr_buf_entry *buf_entries = NULL;
	struct mpi3mr_buf_map *drv_bufs = NULL, *drv_buf_iter = NULL;
	u8 count, bufcnt = 0, is_rmcb = 0, is_rmrb = 0, din_cnt = 0, dout_cnt = 0;
	u8 invalid_be = 0, erb_offset = 0xFF, mpirep_offset = 0xFF, sg_entries = 0;
	u8 block_io = 0, resp_code = 0, nvme_fmt = 0;
	struct mpi3_request_header *mpi_header = NULL;
	struct mpi3_status_reply_descriptor *status_desc;
	struct mpi3_scsi_task_mgmt_request *tm_req;
	u32 erbsz = MPI3MR_SENSE_BUF_SZ, tmplen;
	u16 dev_handle;
	struct mpi3mr_tgt_dev *tgtdev;
	struct mpi3mr_stgt_priv_data *stgt_priv = NULL;
	struct mpi3mr_bsg_in_reply_buf *bsg_reply_buf = NULL;
	u32 din_size = 0, dout_size = 0;
	u8 *din_buf = NULL, *dout_buf = NULL;
	u8 *sgl_iter = NULL, *sgl_din_iter = NULL, *sgl_dout_iter = NULL;

	bsg_req = job->request;
	karg = (struct mpi3mr_bsg_mptcmd *)&bsg_req->cmd.mptcmd;

	mrioc = mpi3mr_bsg_verify_adapter(karg->mrioc_id);
	if (!mrioc)
		return -ENODEV;

	if (karg->timeout < MPI3MR_APP_DEFAULT_TIMEOUT)
		karg->timeout = MPI3MR_APP_DEFAULT_TIMEOUT;

	mpi_req = kzalloc(MPI3MR_ADMIN_REQ_FRAME_SZ, GFP_KERNEL);
	if (!mpi_req)
		return -ENOMEM;
	mpi_header = (struct mpi3_request_header *)mpi_req;

	bufcnt = karg->buf_entry_list.num_of_entries;
	drv_bufs = kzalloc((sizeof(*drv_bufs) * bufcnt), GFP_KERNEL);
	if (!drv_bufs) {
		rval = -ENOMEM;
		goto out;
	}

	dout_buf = kzalloc(job->request_payload.payload_len,
				      GFP_KERNEL);
	if (!dout_buf) {
		rval = -ENOMEM;
		goto out;
	}

	din_buf = kzalloc(job->reply_payload.payload_len,
				     GFP_KERNEL);
	if (!din_buf) {
		rval = -ENOMEM;
		goto out;
	}

	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt,
			  dout_buf, job->request_payload.payload_len);

	buf_entries = karg->buf_entry_list.buf_entry;
	sgl_din_iter = din_buf;
	sgl_dout_iter = dout_buf;
	drv_buf_iter = drv_bufs;

	for (count = 0; count < bufcnt; count++, buf_entries++, drv_buf_iter++) {

		if (sgl_dout_iter > (dout_buf + job->request_payload.payload_len)) {
			dprint_bsg_err(mrioc, "%s: data_out buffer length mismatch\n",
				__func__);
			rval = -EINVAL;
			goto out;
		}
		if (sgl_din_iter > (din_buf + job->reply_payload.payload_len)) {
			dprint_bsg_err(mrioc, "%s: data_in buffer length mismatch\n",
				__func__);
			rval = -EINVAL;
			goto out;
		}

		switch (buf_entries->buf_type) {
		case MPI3MR_BSG_BUFTYPE_RAIDMGMT_CMD:
			sgl_iter = sgl_dout_iter;
			sgl_dout_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_TO_DEVICE;
			is_rmcb = 1;
			if (count != 0)
				invalid_be = 1;
			break;
		case MPI3MR_BSG_BUFTYPE_RAIDMGMT_RESP:
			sgl_iter = sgl_din_iter;
			sgl_din_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_FROM_DEVICE;
			is_rmrb = 1;
			if (count != 1 || !is_rmcb)
				invalid_be = 1;
			break;
		case MPI3MR_BSG_BUFTYPE_DATA_IN:
			sgl_iter = sgl_din_iter;
			sgl_din_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_FROM_DEVICE;
			din_cnt++;
			din_size += drv_buf_iter->bsg_buf_len;
			if ((din_cnt > 1) && !is_rmcb)
				invalid_be = 1;
			break;
		case MPI3MR_BSG_BUFTYPE_DATA_OUT:
			sgl_iter = sgl_dout_iter;
			sgl_dout_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_TO_DEVICE;
			dout_cnt++;
			dout_size += drv_buf_iter->bsg_buf_len;
			if ((dout_cnt > 1) && !is_rmcb)
				invalid_be = 1;
			break;
		case MPI3MR_BSG_BUFTYPE_MPI_REPLY:
			sgl_iter = sgl_din_iter;
			sgl_din_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_NONE;
			mpirep_offset = count;
			break;
		case MPI3MR_BSG_BUFTYPE_ERR_RESPONSE:
			sgl_iter = sgl_din_iter;
			sgl_din_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_NONE;
			erb_offset = count;
			break;
		case MPI3MR_BSG_BUFTYPE_MPI_REQUEST:
			sgl_iter = sgl_dout_iter;
			sgl_dout_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_NONE;
			mpi_msg_size = buf_entries->buf_len;
			if ((!mpi_msg_size || (mpi_msg_size % 4)) ||
					(mpi_msg_size > MPI3MR_ADMIN_REQ_FRAME_SZ)) {
				dprint_bsg_err(mrioc, "%s: invalid MPI message size\n",
					__func__);
				rval = -EINVAL;
				goto out;
			}
			memcpy(mpi_req, sgl_iter, buf_entries->buf_len);
			break;
		default:
			invalid_be = 1;
			break;
		}
		if (invalid_be) {
			dprint_bsg_err(mrioc, "%s: invalid buffer entries passed\n",
				__func__);
			rval = -EINVAL;
			goto out;
		}

		drv_buf_iter->bsg_buf = sgl_iter;
		drv_buf_iter->bsg_buf_len = buf_entries->buf_len;

	}
	if (!is_rmcb && (dout_cnt || din_cnt)) {
		sg_entries = dout_cnt + din_cnt;
		if (((mpi_msg_size) + (sg_entries *
		      sizeof(struct mpi3_sge_common))) > MPI3MR_ADMIN_REQ_FRAME_SZ) {
			dprint_bsg_err(mrioc,
			    "%s:%d: invalid message size passed\n",
			    __func__, __LINE__);
			rval = -EINVAL;
			goto out;
		}
	}
	if (din_size > MPI3MR_MAX_APP_XFER_SIZE) {
		dprint_bsg_err(mrioc,
		    "%s:%d: invalid data transfer size passed for function 0x%x din_size=%d\n",
		    __func__, __LINE__, mpi_header->function, din_size);
		rval = -EINVAL;
		goto out;
	}
	if (dout_size > MPI3MR_MAX_APP_XFER_SIZE) {
		dprint_bsg_err(mrioc,
		    "%s:%d: invalid data transfer size passed for function 0x%x dout_size = %d\n",
		    __func__, __LINE__, mpi_header->function, dout_size);
		rval = -EINVAL;
		goto out;
	}

	drv_buf_iter = drv_bufs;
	for (count = 0; count < bufcnt; count++, drv_buf_iter++) {
		if (drv_buf_iter->data_dir == DMA_NONE)
			continue;

		drv_buf_iter->kern_buf_len = drv_buf_iter->bsg_buf_len;
		if (is_rmcb && !count)
			drv_buf_iter->kern_buf_len += ((dout_cnt + din_cnt) *
			    sizeof(struct mpi3_sge_common));

		if (!drv_buf_iter->kern_buf_len)
			continue;

		drv_buf_iter->kern_buf = dma_alloc_coherent(&mrioc->pdev->dev,
		    drv_buf_iter->kern_buf_len, &drv_buf_iter->kern_buf_dma,
		    GFP_KERNEL);
		if (!drv_buf_iter->kern_buf) {
			rval = -ENOMEM;
			goto out;
		}
		if (drv_buf_iter->data_dir == DMA_TO_DEVICE) {
			tmplen = min(drv_buf_iter->kern_buf_len,
			    drv_buf_iter->bsg_buf_len);
			memcpy(drv_buf_iter->kern_buf, drv_buf_iter->bsg_buf, tmplen);
		}
	}

	if (erb_offset != 0xFF) {
		sense_buff_k = kzalloc(erbsz, GFP_KERNEL);
		if (!sense_buff_k) {
			rval = -ENOMEM;
			goto out;
		}
	}

	if (mutex_lock_interruptible(&mrioc->bsg_cmds.mutex)) {
		rval = -ERESTARTSYS;
		goto out;
	}
	if (mrioc->bsg_cmds.state & MPI3MR_CMD_PENDING) {
		rval = -EAGAIN;
		dprint_bsg_err(mrioc, "%s: command is in use\n", __func__);
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		goto out;
	}
	if (mrioc->unrecoverable) {
		dprint_bsg_err(mrioc, "%s: unrecoverable controller\n",
		    __func__);
		rval = -EFAULT;
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		goto out;
	}
	if (mrioc->reset_in_progress) {
		dprint_bsg_err(mrioc, "%s: reset in progress\n", __func__);
		rval = -EAGAIN;
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		goto out;
	}
	if (mrioc->stop_bsgs) {
		dprint_bsg_err(mrioc, "%s: bsgs are blocked\n", __func__);
		rval = -EAGAIN;
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		goto out;
	}

	if (mpi_header->function == MPI3_BSG_FUNCTION_NVME_ENCAPSULATED) {
		nvme_fmt = mpi3mr_get_nvme_data_fmt(
			(struct mpi3_nvme_encapsulated_request *)mpi_req);
		if (nvme_fmt == MPI3MR_NVME_DATA_FORMAT_PRP) {
			if (mpi3mr_build_nvme_prp(mrioc,
			    (struct mpi3_nvme_encapsulated_request *)mpi_req,
			    drv_bufs, bufcnt)) {
				rval = -ENOMEM;
				mutex_unlock(&mrioc->bsg_cmds.mutex);
				goto out;
			}
		} else if (nvme_fmt == MPI3MR_NVME_DATA_FORMAT_SGL1 ||
			nvme_fmt == MPI3MR_NVME_DATA_FORMAT_SGL2) {
			if (mpi3mr_build_nvme_sgl(mrioc,
			    (struct mpi3_nvme_encapsulated_request *)mpi_req,
			    drv_bufs, bufcnt)) {
				rval = -EINVAL;
				mutex_unlock(&mrioc->bsg_cmds.mutex);
				goto out;
			}
		} else {
			dprint_bsg_err(mrioc,
			    "%s:invalid NVMe command format\n", __func__);
			rval = -EINVAL;
			mutex_unlock(&mrioc->bsg_cmds.mutex);
			goto out;
		}
	} else {
		mpi3mr_bsg_build_sgl(mpi_req, (mpi_msg_size),
		    drv_bufs, bufcnt, is_rmcb, is_rmrb,
		    (dout_cnt + din_cnt));
	}

	if (mpi_header->function == MPI3_BSG_FUNCTION_SCSI_TASK_MGMT) {
		tm_req = (struct mpi3_scsi_task_mgmt_request *)mpi_req;
		if (tm_req->task_type !=
		    MPI3_SCSITASKMGMT_TASKTYPE_ABORT_TASK) {
			dev_handle = tm_req->dev_handle;
			block_io = 1;
		}
	}
	if (block_io) {
		tgtdev = mpi3mr_get_tgtdev_by_handle(mrioc, dev_handle);
		if (tgtdev && tgtdev->starget && tgtdev->starget->hostdata) {
			stgt_priv = (struct mpi3mr_stgt_priv_data *)
			    tgtdev->starget->hostdata;
			atomic_inc(&stgt_priv->block_io);
			mpi3mr_tgtdev_put(tgtdev);
		}
	}

	mrioc->bsg_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->bsg_cmds.is_waiting = 1;
	mrioc->bsg_cmds.callback = NULL;
	mrioc->bsg_cmds.is_sense = 0;
	mrioc->bsg_cmds.sensebuf = sense_buff_k;
	memset(mrioc->bsg_cmds.reply, 0, mrioc->reply_sz);
	mpi_header->host_tag = cpu_to_le16(MPI3MR_HOSTTAG_BSG_CMDS);
	if (mrioc->logging_level & MPI3_DEBUG_BSG_INFO) {
		dprint_bsg_info(mrioc,
		    "%s: posting bsg request to the controller\n", __func__);
		dprint_dump(mpi_req, MPI3MR_ADMIN_REQ_FRAME_SZ,
		    "bsg_mpi3_req");
		if (mpi_header->function == MPI3_BSG_FUNCTION_MGMT_PASSTHROUGH) {
			drv_buf_iter = &drv_bufs[0];
			dprint_dump(drv_buf_iter->kern_buf,
			    drv_buf_iter->kern_buf_len, "mpi3_mgmt_req");
		}
	}

	init_completion(&mrioc->bsg_cmds.done);
	rval = mpi3mr_admin_request_post(mrioc, mpi_req,
	    MPI3MR_ADMIN_REQ_FRAME_SZ, 0);


	if (rval) {
		mrioc->bsg_cmds.is_waiting = 0;
		dprint_bsg_err(mrioc,
		    "%s: posting bsg request is failed\n", __func__);
		rval = -EAGAIN;
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->bsg_cmds.done,
	    (karg->timeout * HZ));
	if (block_io && stgt_priv)
		atomic_dec(&stgt_priv->block_io);
	if (!(mrioc->bsg_cmds.state & MPI3MR_CMD_COMPLETE)) {
		mrioc->bsg_cmds.is_waiting = 0;
		rval = -EAGAIN;
		if (mrioc->bsg_cmds.state & MPI3MR_CMD_RESET)
			goto out_unlock;
		dprint_bsg_err(mrioc,
		    "%s: bsg request timedout after %d seconds\n", __func__,
		    karg->timeout);
		if (mrioc->logging_level & MPI3_DEBUG_BSG_ERROR) {
			dprint_dump(mpi_req, MPI3MR_ADMIN_REQ_FRAME_SZ,
			    "bsg_mpi3_req");
			if (mpi_header->function ==
			    MPI3_BSG_FUNCTION_MGMT_PASSTHROUGH) {
				drv_buf_iter = &drv_bufs[0];
				dprint_dump(drv_buf_iter->kern_buf,
				    drv_buf_iter->kern_buf_len, "mpi3_mgmt_req");
			}
		}

		if ((mpi_header->function == MPI3_BSG_FUNCTION_NVME_ENCAPSULATED) ||
		    (mpi_header->function == MPI3_BSG_FUNCTION_SCSI_IO))
			mpi3mr_issue_tm(mrioc,
			    MPI3_SCSITASKMGMT_TASKTYPE_TARGET_RESET,
			    mpi_header->function_dependent, 0,
			    MPI3MR_HOSTTAG_BLK_TMS, MPI3MR_RESETTM_TIMEOUT,
			    &mrioc->host_tm_cmds, &resp_code, NULL);
		if (!(mrioc->bsg_cmds.state & MPI3MR_CMD_COMPLETE) &&
		    !(mrioc->bsg_cmds.state & MPI3MR_CMD_RESET))
			mpi3mr_soft_reset_handler(mrioc,
			    MPI3MR_RESET_FROM_APP_TIMEOUT, 1);
		goto out_unlock;
	}
	dprint_bsg_info(mrioc, "%s: bsg request is completed\n", __func__);

	if (mrioc->prp_list_virt) {
		dma_free_coherent(&mrioc->pdev->dev, mrioc->prp_sz,
		    mrioc->prp_list_virt, mrioc->prp_list_dma);
		mrioc->prp_list_virt = NULL;
	}

	if ((mrioc->bsg_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	     != MPI3_IOCSTATUS_SUCCESS) {
		dprint_bsg_info(mrioc,
		    "%s: command failed, ioc_status(0x%04x) log_info(0x%08x)\n",
		    __func__,
		    (mrioc->bsg_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->bsg_cmds.ioc_loginfo);
	}

	if ((mpirep_offset != 0xFF) &&
	    drv_bufs[mpirep_offset].bsg_buf_len) {
		drv_buf_iter = &drv_bufs[mpirep_offset];
		drv_buf_iter->kern_buf_len = (sizeof(*bsg_reply_buf) - 1 +
					   mrioc->reply_sz);
		bsg_reply_buf = kzalloc(drv_buf_iter->kern_buf_len, GFP_KERNEL);

		if (!bsg_reply_buf) {
			rval = -ENOMEM;
			goto out_unlock;
		}
		if (mrioc->bsg_cmds.state & MPI3MR_CMD_REPLY_VALID) {
			bsg_reply_buf->mpi_reply_type =
				MPI3MR_BSG_MPI_REPLY_BUFTYPE_ADDRESS;
			memcpy(bsg_reply_buf->reply_buf,
			    mrioc->bsg_cmds.reply, mrioc->reply_sz);
		} else {
			bsg_reply_buf->mpi_reply_type =
				MPI3MR_BSG_MPI_REPLY_BUFTYPE_STATUS;
			status_desc = (struct mpi3_status_reply_descriptor *)
			    bsg_reply_buf->reply_buf;
			status_desc->ioc_status = mrioc->bsg_cmds.ioc_status;
			status_desc->ioc_log_info = mrioc->bsg_cmds.ioc_loginfo;
		}
		tmplen = min(drv_buf_iter->kern_buf_len,
			drv_buf_iter->bsg_buf_len);
		memcpy(drv_buf_iter->bsg_buf, bsg_reply_buf, tmplen);
	}

	if (erb_offset != 0xFF && mrioc->bsg_cmds.sensebuf &&
	    mrioc->bsg_cmds.is_sense) {
		drv_buf_iter = &drv_bufs[erb_offset];
		tmplen = min(erbsz, drv_buf_iter->bsg_buf_len);
		memcpy(drv_buf_iter->bsg_buf, sense_buff_k, tmplen);
	}

	drv_buf_iter = drv_bufs;
	for (count = 0; count < bufcnt; count++, drv_buf_iter++) {
		if (drv_buf_iter->data_dir == DMA_NONE)
			continue;
		if (drv_buf_iter->data_dir == DMA_FROM_DEVICE) {
			tmplen = min(drv_buf_iter->kern_buf_len,
				     drv_buf_iter->bsg_buf_len);
			memcpy(drv_buf_iter->bsg_buf,
			       drv_buf_iter->kern_buf, tmplen);
		}
	}

out_unlock:
	if (din_buf) {
		*reply_payload_rcv_len =
			sg_copy_from_buffer(job->reply_payload.sg_list,
					    job->reply_payload.sg_cnt,
					    din_buf, job->reply_payload.payload_len);
	}
	mrioc->bsg_cmds.is_sense = 0;
	mrioc->bsg_cmds.sensebuf = NULL;
	mrioc->bsg_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->bsg_cmds.mutex);
out:
	kfree(sense_buff_k);
	kfree(dout_buf);
	kfree(din_buf);
	kfree(mpi_req);
	if (drv_bufs) {
		drv_buf_iter = drv_bufs;
		for (count = 0; count < bufcnt; count++, drv_buf_iter++) {
			if (drv_buf_iter->kern_buf && drv_buf_iter->kern_buf_dma)
				dma_free_coherent(&mrioc->pdev->dev,
				    drv_buf_iter->kern_buf_len,
				    drv_buf_iter->kern_buf,
				    drv_buf_iter->kern_buf_dma);
		}
		kfree(drv_bufs);
	}
	kfree(bsg_reply_buf);
	return rval;
}

/**
 * mpi3mr_app_save_logdata - Save Log Data events
 * @mrioc: Adapter instance reference
 * @event_data: event data associated with log data event
 * @event_data_size: event data size to copy
 *
 * If log data event caching is enabled by the applicatiobns,
 * then this function saves the log data in the circular queue
 * and Sends async signal SIGIO to indicate there is an async
 * event from the firmware to the event monitoring applications.
 *
 * Return:Nothing
 */
void mpi3mr_app_save_logdata(struct mpi3mr_ioc *mrioc, char *event_data,
	u16 event_data_size)
{
	u32 index = mrioc->logdata_buf_idx, sz;
	struct mpi3mr_logdata_entry *entry;

	if (!(mrioc->logdata_buf))
		return;

	entry = (struct mpi3mr_logdata_entry *)
		(mrioc->logdata_buf + (index * mrioc->logdata_entry_sz));
	entry->valid_entry = 1;
	sz = min(mrioc->logdata_entry_sz, event_data_size);
	memcpy(entry->data, event_data, sz);
	mrioc->logdata_buf_idx =
		((++index) % MPI3MR_BSG_LOGDATA_MAX_ENTRIES);
	atomic64_inc(&event_counter);
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
	case MPI3MR_MPT_CMD:
		rval = mpi3mr_bsg_process_mpt_cmds(job, &reply_payload_rcv_len);
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
	struct device *bsg_dev = &mrioc->bsg_dev;
	if (!mrioc->bsg_queue)
		return;

	bsg_remove_queue(mrioc->bsg_queue);
	mrioc->bsg_queue = NULL;

	device_del(bsg_dev);
	put_device(bsg_dev);
}

/**
 * mpi3mr_bsg_node_release -release bsg device node
 * @dev: bsg device node
 *
 * decrements bsg dev parent reference count
 *
 * Return:Nothing
 */
static void mpi3mr_bsg_node_release(struct device *dev)
{
	put_device(dev->parent);
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
	struct device *bsg_dev = &mrioc->bsg_dev;
	struct device *parent = &mrioc->shost->shost_gendev;

	device_initialize(bsg_dev);

	bsg_dev->parent = get_device(parent);
	bsg_dev->release = mpi3mr_bsg_node_release;

	dev_set_name(bsg_dev, "mpi3mrctl%u", mrioc->id);

	if (device_add(bsg_dev)) {
		ioc_err(mrioc, "%s: bsg device add failed\n",
		    dev_name(bsg_dev));
		put_device(bsg_dev);
		return;
	}

	mrioc->bsg_queue = bsg_setup_queue(bsg_dev, dev_name(bsg_dev),
			mpi3mr_bsg_request, NULL, 0);
	if (IS_ERR(mrioc->bsg_queue)) {
		ioc_err(mrioc, "%s: bsg registration failed\n",
		    dev_name(bsg_dev));
		device_del(bsg_dev);
		put_device(bsg_dev);
		return;
	}

	blk_queue_max_segments(mrioc->bsg_queue, MPI3MR_MAX_APP_XFER_SEGMENTS);
	blk_queue_max_hw_sectors(mrioc->bsg_queue, MPI3MR_MAX_APP_XFER_SECTORS);

	return;
}

/**
 * version_fw_show - SysFS callback for firmware version read
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 *
 * Return: sysfs_emit() return after copying firmware version
 */
static ssize_t
version_fw_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct mpi3mr_ioc *mrioc = shost_priv(shost);
	struct mpi3mr_compimg_ver *fwver = &mrioc->facts.fw_ver;

	return sysfs_emit(buf, "%d.%d.%d.%d.%05d-%05d\n",
	    fwver->gen_major, fwver->gen_minor, fwver->ph_major,
	    fwver->ph_minor, fwver->cust_id, fwver->build_num);
}
static DEVICE_ATTR_RO(version_fw);

/**
 * fw_queue_depth_show - SysFS callback for firmware max cmds
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 *
 * Return: sysfs_emit() return after copying firmware max commands
 */
static ssize_t
fw_queue_depth_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct mpi3mr_ioc *mrioc = shost_priv(shost);

	return sysfs_emit(buf, "%d\n", mrioc->facts.max_reqs);
}
static DEVICE_ATTR_RO(fw_queue_depth);

/**
 * op_req_q_count_show - SysFS callback for request queue count
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 *
 * Return: sysfs_emit() return after copying request queue count
 */
static ssize_t
op_req_q_count_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct mpi3mr_ioc *mrioc = shost_priv(shost);

	return sysfs_emit(buf, "%d\n", mrioc->num_op_req_q);
}
static DEVICE_ATTR_RO(op_req_q_count);

/**
 * reply_queue_count_show - SysFS callback for reply queue count
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 *
 * Return: sysfs_emit() return after copying reply queue count
 */
static ssize_t
reply_queue_count_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct mpi3mr_ioc *mrioc = shost_priv(shost);

	return sysfs_emit(buf, "%d\n", mrioc->num_op_reply_q);
}

static DEVICE_ATTR_RO(reply_queue_count);

/**
 * logging_level_show - Show controller debug level
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 *
 * A sysfs 'read/write' shost attribute, to show the current
 * debug log level used by the driver for the specific
 * controller.
 *
 * Return: sysfs_emit() return
 */
static ssize_t
logging_level_show(struct device *dev,
	struct device_attribute *attr, char *buf)

{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct mpi3mr_ioc *mrioc = shost_priv(shost);

	return sysfs_emit(buf, "%08xh\n", mrioc->logging_level);
}

/**
 * logging_level_store- Change controller debug level
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 * @count: size of the buffer
 *
 * A sysfs 'read/write' shost attribute, to change the current
 * debug log level used by the driver for the specific
 * controller.
 *
 * Return: strlen() return
 */
static ssize_t
logging_level_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct mpi3mr_ioc *mrioc = shost_priv(shost);
	int val = 0;

	if (kstrtoint(buf, 0, &val) != 0)
		return -EINVAL;

	mrioc->logging_level = val;
	ioc_info(mrioc, "logging_level=%08xh\n", mrioc->logging_level);
	return strlen(buf);
}
static DEVICE_ATTR_RW(logging_level);

/**
 * adp_state_show() - SysFS callback for adapter state show
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 *
 * Return: sysfs_emit() return after copying adapter state
 */
static ssize_t
adp_state_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct mpi3mr_ioc *mrioc = shost_priv(shost);
	enum mpi3mr_iocstate ioc_state;
	uint8_t adp_state;

	ioc_state = mpi3mr_get_iocstate(mrioc);
	if (ioc_state == MRIOC_STATE_UNRECOVERABLE)
		adp_state = MPI3MR_BSG_ADPSTATE_UNRECOVERABLE;
	else if ((mrioc->reset_in_progress) || (mrioc->stop_bsgs))
		adp_state = MPI3MR_BSG_ADPSTATE_IN_RESET;
	else if (ioc_state == MRIOC_STATE_FAULT)
		adp_state = MPI3MR_BSG_ADPSTATE_FAULT;
	else
		adp_state = MPI3MR_BSG_ADPSTATE_OPERATIONAL;

	return sysfs_emit(buf, "%u\n", adp_state);
}

static DEVICE_ATTR_RO(adp_state);

static struct attribute *mpi3mr_host_attrs[] = {
	&dev_attr_version_fw.attr,
	&dev_attr_fw_queue_depth.attr,
	&dev_attr_op_req_q_count.attr,
	&dev_attr_reply_queue_count.attr,
	&dev_attr_logging_level.attr,
	&dev_attr_adp_state.attr,
	NULL,
};

static const struct attribute_group mpi3mr_host_attr_group = {
	.attrs = mpi3mr_host_attrs
};

const struct attribute_group *mpi3mr_host_groups[] = {
	&mpi3mr_host_attr_group,
	NULL,
};


/*
 * SCSI Device attributes under sysfs
 */

/**
 * sas_address_show - SysFS callback for dev SASaddress display
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 *
 * Return: sysfs_emit() return after copying SAS address of the
 * specific SAS/SATA end device.
 */
static ssize_t
sas_address_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct mpi3mr_sdev_priv_data *sdev_priv_data;
	struct mpi3mr_stgt_priv_data *tgt_priv_data;
	struct mpi3mr_tgt_dev *tgtdev;

	sdev_priv_data = sdev->hostdata;
	if (!sdev_priv_data)
		return 0;

	tgt_priv_data = sdev_priv_data->tgt_priv_data;
	if (!tgt_priv_data)
		return 0;
	tgtdev = tgt_priv_data->tgt_dev;
	if (!tgtdev || tgtdev->dev_type != MPI3_DEVICE_DEVFORM_SAS_SATA)
		return 0;
	return sysfs_emit(buf, "0x%016llx\n",
	    (unsigned long long)tgtdev->dev_spec.sas_sata_inf.sas_address);
}

static DEVICE_ATTR_RO(sas_address);

/**
 * device_handle_show - SysFS callback for device handle display
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 *
 * Return: sysfs_emit() return after copying firmware internal
 * device handle of the specific device.
 */
static ssize_t
device_handle_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct mpi3mr_sdev_priv_data *sdev_priv_data;
	struct mpi3mr_stgt_priv_data *tgt_priv_data;
	struct mpi3mr_tgt_dev *tgtdev;

	sdev_priv_data = sdev->hostdata;
	if (!sdev_priv_data)
		return 0;

	tgt_priv_data = sdev_priv_data->tgt_priv_data;
	if (!tgt_priv_data)
		return 0;
	tgtdev = tgt_priv_data->tgt_dev;
	if (!tgtdev)
		return 0;
	return sysfs_emit(buf, "0x%04x\n", tgtdev->dev_handle);
}

static DEVICE_ATTR_RO(device_handle);

/**
 * persistent_id_show - SysFS callback for persisten ID display
 * @dev: class device
 * @attr: Device attributes
 * @buf: Buffer to copy
 *
 * Return: sysfs_emit() return after copying persistent ID of the
 * of the specific device.
 */
static ssize_t
persistent_id_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct mpi3mr_sdev_priv_data *sdev_priv_data;
	struct mpi3mr_stgt_priv_data *tgt_priv_data;
	struct mpi3mr_tgt_dev *tgtdev;

	sdev_priv_data = sdev->hostdata;
	if (!sdev_priv_data)
		return 0;

	tgt_priv_data = sdev_priv_data->tgt_priv_data;
	if (!tgt_priv_data)
		return 0;
	tgtdev = tgt_priv_data->tgt_dev;
	if (!tgtdev)
		return 0;
	return sysfs_emit(buf, "%d\n", tgtdev->perst_id);
}
static DEVICE_ATTR_RO(persistent_id);

static struct attribute *mpi3mr_dev_attrs[] = {
	&dev_attr_sas_address.attr,
	&dev_attr_device_handle.attr,
	&dev_attr_persistent_id.attr,
	NULL,
};

static const struct attribute_group mpi3mr_dev_attr_group = {
	.attrs = mpi3mr_dev_attrs
};

const struct attribute_group *mpi3mr_dev_groups[] = {
	&mpi3mr_dev_attr_group,
	NULL,
};
