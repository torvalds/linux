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
	int retval = 0;
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
	int retval = 0;
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
	int retval = -ENODEV;
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
	int retval = 0;

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
	spin_lock_init(&mrioc->watchdog_lock);
	spin_lock_init(&mrioc->chain_buf_lock);

	mpi3mr_init_drv_cmd(&mrioc->init_cmds, MPI3MR_HOSTTAG_INITCMDS);

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

	mrioc = shost_priv(shost);
	while (mrioc->reset_in_progress || mrioc->is_driver_loading)
		ssleep(1);

	mrioc->stop_drv_processing = 1;
	scsi_remove_host(shost);

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

	if (!shost)
		return;

	mrioc = shost_priv(shost);
	while (mrioc->reset_in_progress || mrioc->is_driver_loading)
		ssleep(1);

	mrioc->stop_drv_processing = 1;
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
