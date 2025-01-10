// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2023 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#include "mpi3mr.h"
#include <linux/bsg-lib.h>
#include <uapi/scsi/scsi_bsg_mpi3mr.h>

/**
 * mpi3mr_alloc_trace_buffer:	Allocate trace buffer
 * @mrioc: Adapter instance reference
 * @trace_size: Trace buffer size
 *
 * Allocate trace buffer
 * Return: 0 on success, non-zero on failure.
 */
static int mpi3mr_alloc_trace_buffer(struct mpi3mr_ioc *mrioc, u32 trace_size)
{
	struct diag_buffer_desc *diag_buffer = &mrioc->diag_buffers[0];

	diag_buffer->addr = dma_alloc_coherent(&mrioc->pdev->dev,
	    trace_size, &diag_buffer->dma_addr, GFP_KERNEL);
	if (diag_buffer->addr) {
		dprint_init(mrioc, "trace diag buffer is allocated successfully\n");
		return 0;
	}
	return -1;
}

/**
 * mpi3mr_alloc_diag_bufs - Allocate memory for diag buffers
 * @mrioc: Adapter instance reference
 *
 * This functions checks whether the driver defined buffer sizes
 * are greater than IOCFacts provided controller local buffer
 * sizes and if the driver defined sizes are more then the
 * driver allocates the specific buffer by reading driver page1
 *
 * Return: Nothing.
 */
void mpi3mr_alloc_diag_bufs(struct mpi3mr_ioc *mrioc)
{
	struct diag_buffer_desc *diag_buffer;
	struct mpi3_driver_page1 driver_pg1;
	u32 trace_dec_size, trace_min_size, fw_dec_size, fw_min_size,
		trace_size, fw_size;
	u16 pg_sz = sizeof(driver_pg1);
	int retval = 0;
	bool retry = false;

	if (mrioc->diag_buffers[0].addr || mrioc->diag_buffers[1].addr)
		return;

	retval = mpi3mr_cfg_get_driver_pg1(mrioc, &driver_pg1, pg_sz);
	if (retval) {
		ioc_warn(mrioc,
		    "%s: driver page 1 read failed, allocating trace\n"
		    "and firmware diag buffers of default size\n", __func__);
		trace_size = fw_size = MPI3MR_DEFAULT_HDB_MAX_SZ;
		trace_dec_size = fw_dec_size = MPI3MR_DEFAULT_HDB_DEC_SZ;
		trace_min_size = fw_min_size = MPI3MR_DEFAULT_HDB_MIN_SZ;

	} else {
		trace_size = driver_pg1.host_diag_trace_max_size * 1024;
		trace_dec_size = driver_pg1.host_diag_trace_decrement_size
			 * 1024;
		trace_min_size = driver_pg1.host_diag_trace_min_size * 1024;
		fw_size = driver_pg1.host_diag_fw_max_size * 1024;
		fw_dec_size = driver_pg1.host_diag_fw_decrement_size * 1024;
		fw_min_size = driver_pg1.host_diag_fw_min_size * 1024;
		dprint_init(mrioc,
		    "%s:trace diag buffer sizes read from driver\n"
		    "page1: maximum size = %dKB, decrement size = %dKB\n"
		    ", minimum size = %dKB\n", __func__, driver_pg1.host_diag_trace_max_size,
		    driver_pg1.host_diag_trace_decrement_size,
		    driver_pg1.host_diag_trace_min_size);
		dprint_init(mrioc,
		    "%s:firmware diag buffer sizes read from driver\n"
		    "page1: maximum size = %dKB, decrement size = %dKB\n"
		    ", minimum size = %dKB\n", __func__, driver_pg1.host_diag_fw_max_size,
		    driver_pg1.host_diag_fw_decrement_size,
		    driver_pg1.host_diag_fw_min_size);
		if ((trace_size == 0) && (fw_size == 0))
			return;
	}


retry_trace:
	diag_buffer = &mrioc->diag_buffers[0];
	diag_buffer->type = MPI3_DIAG_BUFFER_TYPE_TRACE;
	diag_buffer->status = MPI3MR_HDB_BUFSTATUS_NOT_ALLOCATED;
	if ((mrioc->facts.diag_trace_sz < trace_size) && (trace_size >=
		trace_min_size)) {
		if (!retry)
			dprint_init(mrioc,
			    "trying to allocate trace diag buffer of size = %dKB\n",
			    trace_size / 1024);
		if (get_order(trace_size) > MAX_PAGE_ORDER ||
		    mpi3mr_alloc_trace_buffer(mrioc, trace_size)) {
			retry = true;
			trace_size -= trace_dec_size;
			dprint_init(mrioc, "trace diag buffer allocation failed\n"
			"retrying smaller size %dKB\n", trace_size / 1024);
			goto retry_trace;
		} else
			diag_buffer->size = trace_size;
	}

	retry = false;
retry_fw:

	diag_buffer = &mrioc->diag_buffers[1];

	diag_buffer->type = MPI3_DIAG_BUFFER_TYPE_FW;
	diag_buffer->status = MPI3MR_HDB_BUFSTATUS_NOT_ALLOCATED;
	if ((mrioc->facts.diag_fw_sz < fw_size) && (fw_size >= fw_min_size)) {
		if (get_order(fw_size) <= MAX_PAGE_ORDER) {
			diag_buffer->addr
				= dma_alloc_coherent(&mrioc->pdev->dev, fw_size,
						     &diag_buffer->dma_addr,
						     GFP_KERNEL);
		}
		if (!retry)
			dprint_init(mrioc,
			    "%s:trying to allocate firmware diag buffer of size = %dKB\n",
			    __func__, fw_size / 1024);
		if (diag_buffer->addr) {
			dprint_init(mrioc, "%s:firmware diag buffer allocated successfully\n",
			    __func__);
			diag_buffer->size = fw_size;
		} else {
			retry = true;
			fw_size -= fw_dec_size;
			dprint_init(mrioc, "%s:trace diag buffer allocation failed,\n"
					"retrying smaller size %dKB\n",
					__func__, fw_size / 1024);
			goto retry_fw;
		}
	}
}

/**
 * mpi3mr_issue_diag_buf_post - Send diag buffer post req
 * @mrioc: Adapter instance reference
 * @diag_buffer: Diagnostic buffer descriptor
 *
 * Issue diagnostic buffer post MPI request through admin queue
 * and wait for the completion of it or time out.
 *
 * Return: 0 on success, non-zero on failures.
 */
int mpi3mr_issue_diag_buf_post(struct mpi3mr_ioc *mrioc,
	struct diag_buffer_desc *diag_buffer)
{
	struct mpi3_diag_buffer_post_request diag_buf_post_req;
	u8 prev_status;
	int retval = 0;

	memset(&diag_buf_post_req, 0, sizeof(diag_buf_post_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		dprint_bsg_err(mrioc, "%s: command is in use\n", __func__);
		mutex_unlock(&mrioc->init_cmds.mutex);
		return -1;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	diag_buf_post_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	diag_buf_post_req.function = MPI3_FUNCTION_DIAG_BUFFER_POST;
	diag_buf_post_req.type = diag_buffer->type;
	diag_buf_post_req.address = le64_to_cpu(diag_buffer->dma_addr);
	diag_buf_post_req.length = le32_to_cpu(diag_buffer->size);

	dprint_bsg_info(mrioc, "%s: posting diag buffer type %d\n", __func__,
	    diag_buffer->type);
	prev_status = diag_buffer->status;
	diag_buffer->status = MPI3MR_HDB_BUFSTATUS_POSTED_UNPAUSED;
	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &diag_buf_post_req,
	    sizeof(diag_buf_post_req), 1);
	if (retval) {
		dprint_bsg_err(mrioc, "%s: admin request post failed\n",
		    __func__);
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		mrioc->init_cmds.is_waiting = 0;
		dprint_bsg_err(mrioc, "%s: command timedout\n", __func__);
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_DIAG_BUFFER_POST_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		dprint_bsg_err(mrioc,
		    "%s: command failed, buffer_type (%d) ioc_status(0x%04x) log_info(0x%08x)\n",
		    __func__, diag_buffer->type,
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}
	dprint_bsg_info(mrioc, "%s: diag buffer type %d posted successfully\n",
	    __func__, diag_buffer->type);

out_unlock:
	if (retval)
		diag_buffer->status = prev_status;
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);
	return retval;
}

/**
 * mpi3mr_post_diag_bufs - Post diag buffers to the controller
 * @mrioc: Adapter instance reference
 *
 * This function calls helper function to post both trace and
 * firmware buffers to the controller.
 *
 * Return: None
 */
int mpi3mr_post_diag_bufs(struct mpi3mr_ioc *mrioc)
{
	u8 i;
	struct diag_buffer_desc *diag_buffer;

	for (i = 0; i < MPI3MR_MAX_NUM_HDB; i++) {
		diag_buffer = &mrioc->diag_buffers[i];
		if (!(diag_buffer->addr))
			continue;
		if (mpi3mr_issue_diag_buf_post(mrioc, diag_buffer))
			return -1;
	}
	return 0;
}

/**
 * mpi3mr_issue_diag_buf_release - Send diag buffer release req
 * @mrioc: Adapter instance reference
 * @diag_buffer: Diagnostic buffer descriptor
 *
 * Issue diagnostic buffer manage MPI request with release
 * action request through admin queue and wait for the
 * completion of it or time out.
 *
 * Return: 0 on success, non-zero on failures.
 */
int mpi3mr_issue_diag_buf_release(struct mpi3mr_ioc *mrioc,
	struct diag_buffer_desc *diag_buffer)
{
	struct mpi3_diag_buffer_manage_request diag_buf_manage_req;
	int retval = 0;

	if ((diag_buffer->status != MPI3MR_HDB_BUFSTATUS_POSTED_UNPAUSED) &&
	    (diag_buffer->status != MPI3MR_HDB_BUFSTATUS_POSTED_PAUSED))
		return retval;

	memset(&diag_buf_manage_req, 0, sizeof(diag_buf_manage_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		dprint_reset(mrioc, "%s: command is in use\n", __func__);
		mutex_unlock(&mrioc->init_cmds.mutex);
		return -1;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	diag_buf_manage_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	diag_buf_manage_req.function = MPI3_FUNCTION_DIAG_BUFFER_MANAGE;
	diag_buf_manage_req.type = diag_buffer->type;
	diag_buf_manage_req.action = MPI3_DIAG_BUFFER_ACTION_RELEASE;


	dprint_reset(mrioc, "%s: releasing diag buffer type %d\n", __func__,
	    diag_buffer->type);
	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &diag_buf_manage_req,
	    sizeof(diag_buf_manage_req), 1);
	if (retval) {
		dprint_reset(mrioc, "%s: admin request post failed\n", __func__);
		mpi3mr_set_trigger_data_in_hdb(diag_buffer,
		    MPI3MR_HDB_TRIGGER_TYPE_UNKNOWN, NULL, 1);
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		mrioc->init_cmds.is_waiting = 0;
		dprint_reset(mrioc, "%s: command timedout\n", __func__);
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_DIAG_BUFFER_RELEASE_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		dprint_reset(mrioc,
		    "%s: command failed, buffer_type (%d) ioc_status(0x%04x) log_info(0x%08x)\n",
		    __func__, diag_buffer->type,
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}
	dprint_reset(mrioc, "%s: diag buffer type %d released successfully\n",
	    __func__, diag_buffer->type);

out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);
	return retval;
}

/**
 * mpi3mr_process_trigger - Generic HDB Trigger handler
 * @mrioc: Adapter instance reference
 * @trigger_type: Trigger type
 * @trigger_data: Trigger data
 * @trigger_flags: Trigger flags
 *
 * This function checks validity of HDB, triggers and based on
 * trigger information, creates an event to be processed in the
 * firmware event worker thread .
 *
 * This function should be called with trigger spinlock held
 *
 * Return: Nothing
 */
static void mpi3mr_process_trigger(struct mpi3mr_ioc *mrioc, u8 trigger_type,
	union mpi3mr_trigger_data *trigger_data, u8 trigger_flags)
{
	struct trigger_event_data event_data;
	struct diag_buffer_desc *trace_hdb = NULL;
	struct diag_buffer_desc *fw_hdb = NULL;
	u64 global_trigger;

	trace_hdb = mpi3mr_diag_buffer_for_type(mrioc,
	    MPI3_DIAG_BUFFER_TYPE_TRACE);
	if (trace_hdb &&
	    (trace_hdb->status != MPI3MR_HDB_BUFSTATUS_POSTED_UNPAUSED) &&
	    (trace_hdb->status != MPI3MR_HDB_BUFSTATUS_POSTED_PAUSED))
		trace_hdb =  NULL;

	fw_hdb = mpi3mr_diag_buffer_for_type(mrioc, MPI3_DIAG_BUFFER_TYPE_FW);

	if (fw_hdb &&
	    (fw_hdb->status != MPI3MR_HDB_BUFSTATUS_POSTED_UNPAUSED) &&
	    (fw_hdb->status != MPI3MR_HDB_BUFSTATUS_POSTED_PAUSED))
		fw_hdb = NULL;

	if (mrioc->snapdump_trigger_active || (mrioc->fw_release_trigger_active
	    && mrioc->trace_release_trigger_active) ||
	    (!trace_hdb && !fw_hdb) || (!mrioc->driver_pg2) ||
	    ((trigger_type == MPI3MR_HDB_TRIGGER_TYPE_ELEMENT)
	     && (!mrioc->driver_pg2->num_triggers)))
		return;

	memset(&event_data, 0, sizeof(event_data));
	event_data.trigger_type = trigger_type;
	memcpy(&event_data.trigger_specific_data, trigger_data,
	    sizeof(*trigger_data));
	global_trigger = le64_to_cpu(mrioc->driver_pg2->global_trigger);

	if (global_trigger & MPI3_DRIVER2_GLOBALTRIGGER_SNAPDUMP_ENABLED) {
		event_data.snapdump = true;
		event_data.trace_hdb = trace_hdb;
		event_data.fw_hdb = fw_hdb;
		mrioc->snapdump_trigger_active = true;
	} else if (trigger_type == MPI3MR_HDB_TRIGGER_TYPE_GLOBAL) {
		if ((trace_hdb) && (global_trigger &
		    MPI3_DRIVER2_GLOBALTRIGGER_DIAG_TRACE_RELEASE) &&
		    (!mrioc->trace_release_trigger_active)) {
			event_data.trace_hdb = trace_hdb;
			mrioc->trace_release_trigger_active = true;
		}
		if ((fw_hdb) && (global_trigger &
		    MPI3_DRIVER2_GLOBALTRIGGER_DIAG_FW_RELEASE) &&
		    (!mrioc->fw_release_trigger_active)) {
			event_data.fw_hdb = fw_hdb;
			mrioc->fw_release_trigger_active = true;
		}
	} else if (trigger_type == MPI3MR_HDB_TRIGGER_TYPE_ELEMENT) {
		if ((trace_hdb) && (trigger_flags &
		    MPI3_DRIVER2_TRIGGER_FLAGS_DIAG_TRACE_RELEASE) &&
		    (!mrioc->trace_release_trigger_active)) {
			event_data.trace_hdb = trace_hdb;
			mrioc->trace_release_trigger_active = true;
		}
		if ((fw_hdb) && (trigger_flags &
		    MPI3_DRIVER2_TRIGGER_FLAGS_DIAG_FW_RELEASE) &&
		    (!mrioc->fw_release_trigger_active)) {
			event_data.fw_hdb = fw_hdb;
			mrioc->fw_release_trigger_active = true;
		}
	}

	if (event_data.trace_hdb || event_data.fw_hdb)
		mpi3mr_hdb_trigger_data_event(mrioc, &event_data);
}

/**
 * mpi3mr_global_trigger - Global HDB trigger handler
 * @mrioc: Adapter instance reference
 * @trigger_data: Trigger data
 *
 * This function checks whether the given global trigger is
 * enabled in the driver page 2 and if so calls generic trigger
 * handler to queue event for HDB release.
 *
 * Return: Nothing
 */
void mpi3mr_global_trigger(struct mpi3mr_ioc *mrioc, u64 trigger_data)
{
	unsigned long flags;
	union mpi3mr_trigger_data trigger_specific_data;

	spin_lock_irqsave(&mrioc->trigger_lock, flags);
	if (le64_to_cpu(mrioc->driver_pg2->global_trigger) & trigger_data) {
		memset(&trigger_specific_data, 0,
		    sizeof(trigger_specific_data));
		trigger_specific_data.global = trigger_data;
		mpi3mr_process_trigger(mrioc, MPI3MR_HDB_TRIGGER_TYPE_GLOBAL,
		    &trigger_specific_data, 0);
	}
	spin_unlock_irqrestore(&mrioc->trigger_lock, flags);
}

/**
 * mpi3mr_scsisense_trigger - SCSI sense HDB trigger handler
 * @mrioc: Adapter instance reference
 * @sensekey: Sense Key
 * @asc: Additional Sense Code
 * @ascq: Additional Sense Code Qualifier
 *
 * This function compares SCSI sense trigger values with driver
 * page 2 values and calls generic trigger handler to release
 * HDBs if match found
 *
 * Return: Nothing
 */
void mpi3mr_scsisense_trigger(struct mpi3mr_ioc *mrioc, u8 sensekey, u8 asc,
	u8 ascq)
{
	struct mpi3_driver2_trigger_scsi_sense *scsi_sense_trigger = NULL;
	u64 i = 0;
	unsigned long flags;
	u8 num_triggers, trigger_flags;

	if (mrioc->scsisense_trigger_present) {
		spin_lock_irqsave(&mrioc->trigger_lock, flags);
		scsi_sense_trigger = (struct mpi3_driver2_trigger_scsi_sense *)
			mrioc->driver_pg2->trigger;
		num_triggers = mrioc->driver_pg2->num_triggers;
		for (i = 0; i < num_triggers; i++, scsi_sense_trigger++) {
			if (scsi_sense_trigger->type !=
			    MPI3_DRIVER2_TRIGGER_TYPE_SCSI_SENSE)
				continue;
			if (!(scsi_sense_trigger->sense_key ==
			    MPI3_DRIVER2_TRIGGER_SCSI_SENSE_SENSE_KEY_MATCH_ALL
			      || scsi_sense_trigger->sense_key == sensekey))
				continue;
			if (!(scsi_sense_trigger->asc ==
			    MPI3_DRIVER2_TRIGGER_SCSI_SENSE_ASC_MATCH_ALL ||
			    scsi_sense_trigger->asc == asc))
				continue;
			if (!(scsi_sense_trigger->ascq ==
			    MPI3_DRIVER2_TRIGGER_SCSI_SENSE_ASCQ_MATCH_ALL ||
			    scsi_sense_trigger->ascq == ascq))
				continue;
			trigger_flags = scsi_sense_trigger->flags;
			mpi3mr_process_trigger(mrioc,
			    MPI3MR_HDB_TRIGGER_TYPE_ELEMENT,
			    (union mpi3mr_trigger_data *)scsi_sense_trigger,
			    trigger_flags);
			break;
		}
		spin_unlock_irqrestore(&mrioc->trigger_lock, flags);
	}
}

/**
 * mpi3mr_event_trigger - MPI event HDB trigger handler
 * @mrioc: Adapter instance reference
 * @event: MPI Event
 *
 * This function compares event trigger values with driver page
 * 2 values and calls generic trigger handler to release
 * HDBs if match found.
 *
 * Return: Nothing
 */
void mpi3mr_event_trigger(struct mpi3mr_ioc *mrioc, u8 event)
{
	struct mpi3_driver2_trigger_event *event_trigger = NULL;
	u64 i = 0;
	unsigned long flags;
	u8 num_triggers, trigger_flags;

	if (mrioc->event_trigger_present) {
		spin_lock_irqsave(&mrioc->trigger_lock, flags);
		event_trigger = (struct mpi3_driver2_trigger_event *)
			mrioc->driver_pg2->trigger;
		num_triggers = mrioc->driver_pg2->num_triggers;

		for (i = 0; i < num_triggers; i++, event_trigger++) {
			if (event_trigger->type !=
			    MPI3_DRIVER2_TRIGGER_TYPE_EVENT)
				continue;
			if (event_trigger->event != event)
				continue;
			trigger_flags = event_trigger->flags;
			mpi3mr_process_trigger(mrioc,
			    MPI3MR_HDB_TRIGGER_TYPE_ELEMENT,
			    (union mpi3mr_trigger_data *)event_trigger,
			    trigger_flags);
			break;
		}
		spin_unlock_irqrestore(&mrioc->trigger_lock, flags);
	}
}

/**
 * mpi3mr_reply_trigger - MPI Reply HDB trigger handler
 * @mrioc: Adapter instance reference
 * @ioc_status: Masked value of IOC Status from MPI Reply
 * @ioc_loginfo: IOC Log Info from MPI Reply
 *
 * This function compares IOC status and IOC log info trigger
 * values with driver page 2 values and calls generic trigger
 * handler to release HDBs if match found.
 *
 * Return: Nothing
 */
void mpi3mr_reply_trigger(struct mpi3mr_ioc *mrioc, u16 ioc_status,
	u32 ioc_loginfo)
{
	struct mpi3_driver2_trigger_reply *reply_trigger = NULL;
	u64 i = 0;
	unsigned long flags;
	u8 num_triggers, trigger_flags;

	if (mrioc->reply_trigger_present) {
		spin_lock_irqsave(&mrioc->trigger_lock, flags);
		reply_trigger = (struct mpi3_driver2_trigger_reply *)
			mrioc->driver_pg2->trigger;
		num_triggers = mrioc->driver_pg2->num_triggers;
		for (i = 0; i < num_triggers; i++, reply_trigger++) {
			if (reply_trigger->type !=
			    MPI3_DRIVER2_TRIGGER_TYPE_REPLY)
				continue;
			if ((le16_to_cpu(reply_trigger->ioc_status) !=
			     ioc_status)
			    && (le16_to_cpu(reply_trigger->ioc_status) !=
			    MPI3_DRIVER2_TRIGGER_REPLY_IOCSTATUS_MATCH_ALL))
				continue;
			if ((le32_to_cpu(reply_trigger->ioc_log_info) !=
			    (le32_to_cpu(reply_trigger->ioc_log_info_mask) &
			     ioc_loginfo)))
				continue;
			trigger_flags = reply_trigger->flags;
			mpi3mr_process_trigger(mrioc,
			    MPI3MR_HDB_TRIGGER_TYPE_ELEMENT,
			    (union mpi3mr_trigger_data *)reply_trigger,
			    trigger_flags);
			break;
		}
		spin_unlock_irqrestore(&mrioc->trigger_lock, flags);
	}
}

/**
 * mpi3mr_get_num_trigger - Gets number of HDB triggers
 * @mrioc: Adapter instance reference
 * @num_triggers: Number of triggers
 * @page_action: Page action
 *
 * This function reads number of triggers by reading driver page
 * 2
 *
 * Return: 0 on success and proper error codes on failure
 */
static int mpi3mr_get_num_trigger(struct mpi3mr_ioc *mrioc, u8 *num_triggers,
	u8 page_action)
{
	struct mpi3_driver_page2 drvr_page2;
	int retval = 0;

	*num_triggers = 0;

	retval = mpi3mr_cfg_get_driver_pg2(mrioc, &drvr_page2,
	    sizeof(struct mpi3_driver_page2), page_action);

	if (retval) {
		dprint_init(mrioc, "%s: driver page 2 read failed\n", __func__);
		return retval;
	}
	*num_triggers = drvr_page2.num_triggers;
	return retval;
}

/**
 * mpi3mr_refresh_trigger - Handler for Refresh trigger BSG
 * @mrioc: Adapter instance reference
 * @page_action: Page action
 *
 * This function caches the driver page 2 in the driver's memory
 * by reading driver page 2 from the controller for a given page
 * type and updates the HDB trigger values
 *
 * Return: 0 on success and proper error codes on failure
 */
int mpi3mr_refresh_trigger(struct mpi3mr_ioc *mrioc, u8 page_action)
{
	u16 pg_sz = sizeof(struct mpi3_driver_page2);
	struct mpi3_driver_page2 *drvr_page2 = NULL;
	u8 trigger_type, num_triggers;
	int retval;
	int i = 0;
	unsigned long flags;

	retval = mpi3mr_get_num_trigger(mrioc, &num_triggers, page_action);

	if (retval)
		goto out;

	pg_sz = offsetof(struct mpi3_driver_page2, trigger) +
		(num_triggers * sizeof(union mpi3_driver2_trigger_element));
	drvr_page2 = kzalloc(pg_sz, GFP_KERNEL);
	if (!drvr_page2) {
		retval = -ENOMEM;
		goto out;
	}

	retval = mpi3mr_cfg_get_driver_pg2(mrioc, drvr_page2, pg_sz, page_action);
	if (retval) {
		dprint_init(mrioc, "%s: driver page 2 read failed\n", __func__);
		kfree(drvr_page2);
		goto out;
	}
	spin_lock_irqsave(&mrioc->trigger_lock, flags);
	kfree(mrioc->driver_pg2);
	mrioc->driver_pg2 = drvr_page2;
	mrioc->reply_trigger_present = false;
	mrioc->event_trigger_present = false;
	mrioc->scsisense_trigger_present = false;

	for (i = 0; (i < mrioc->driver_pg2->num_triggers); i++) {
		trigger_type = mrioc->driver_pg2->trigger[i].event.type;
		switch (trigger_type) {
		case MPI3_DRIVER2_TRIGGER_TYPE_REPLY:
			mrioc->reply_trigger_present = true;
			break;
		case MPI3_DRIVER2_TRIGGER_TYPE_EVENT:
			mrioc->event_trigger_present = true;
			break;
		case MPI3_DRIVER2_TRIGGER_TYPE_SCSI_SENSE:
			mrioc->scsisense_trigger_present = true;
			break;
		default:
			break;
		}
	}
	spin_unlock_irqrestore(&mrioc->trigger_lock, flags);
out:
	return retval;
}

/**
 * mpi3mr_release_diag_bufs - Release diag buffers
 * @mrioc: Adapter instance reference
 * @skip_rel_action: Skip release action and set buffer state
 *
 * This function calls helper function to release both trace and
 * firmware buffers from the controller.
 *
 * Return: None
 */
void mpi3mr_release_diag_bufs(struct mpi3mr_ioc *mrioc, u8 skip_rel_action)
{
	u8 i;
	struct diag_buffer_desc *diag_buffer;

	for (i = 0; i < MPI3MR_MAX_NUM_HDB; i++) {
		diag_buffer = &mrioc->diag_buffers[i];
		if (!(diag_buffer->addr))
			continue;
		if (diag_buffer->status == MPI3MR_HDB_BUFSTATUS_RELEASED)
			continue;
		if (!skip_rel_action)
			mpi3mr_issue_diag_buf_release(mrioc, diag_buffer);
		diag_buffer->status = MPI3MR_HDB_BUFSTATUS_RELEASED;
		atomic64_inc(&event_counter);
	}
}

/**
 * mpi3mr_set_trigger_data_in_hdb - Updates HDB trigger type and
 * trigger data
 *
 * @hdb: HDB pointer
 * @type: Trigger type
 * @data: Trigger data
 * @force: Trigger overwrite flag
 * @trigger_data: Pointer to trigger data information
 *
 * Updates trigger type and trigger data based on parameter
 * passed to this function
 *
 * Return: Nothing
 */
void mpi3mr_set_trigger_data_in_hdb(struct diag_buffer_desc *hdb,
	u8 type, union mpi3mr_trigger_data *trigger_data, bool force)
{
	if ((!force) && (hdb->trigger_type != MPI3MR_HDB_TRIGGER_TYPE_UNKNOWN))
		return;
	hdb->trigger_type = type;
	if (!trigger_data)
		memset(&hdb->trigger_data, 0, sizeof(*trigger_data));
	else
		memcpy(&hdb->trigger_data, trigger_data, sizeof(*trigger_data));
}

/**
 * mpi3mr_set_trigger_data_in_all_hdb - Updates HDB trigger type
 * and trigger data for all HDB
 *
 * @mrioc: Adapter instance reference
 * @type: Trigger type
 * @data: Trigger data
 * @force: Trigger overwrite flag
 * @trigger_data: Pointer to trigger data information
 *
 * Updates trigger type and trigger data based on parameter
 * passed to this function
 *
 * Return: Nothing
 */
void mpi3mr_set_trigger_data_in_all_hdb(struct mpi3mr_ioc *mrioc,
	u8 type, union mpi3mr_trigger_data *trigger_data, bool force)
{
	struct diag_buffer_desc *hdb = NULL;

	hdb = mpi3mr_diag_buffer_for_type(mrioc, MPI3_DIAG_BUFFER_TYPE_TRACE);
	if (hdb)
		mpi3mr_set_trigger_data_in_hdb(hdb, type, trigger_data, force);
	hdb = mpi3mr_diag_buffer_for_type(mrioc, MPI3_DIAG_BUFFER_TYPE_FW);
	if (hdb)
		mpi3mr_set_trigger_data_in_hdb(hdb, type, trigger_data, force);
}

/**
 * mpi3mr_hdbstatuschg_evt_th - HDB status change evt tophalf
 * @mrioc: Adapter instance reference
 * @event_reply: event data
 *
 * Modifies the status of the applicable diag buffer descriptors
 *
 * Return: Nothing
 */
void mpi3mr_hdbstatuschg_evt_th(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply)
{
	struct mpi3_event_data_diag_buffer_status_change *evtdata;
	struct diag_buffer_desc *diag_buffer;

	evtdata = (struct mpi3_event_data_diag_buffer_status_change *)
	    event_reply->event_data;

	diag_buffer = mpi3mr_diag_buffer_for_type(mrioc, evtdata->type);
	if (!diag_buffer)
		return;
	if ((diag_buffer->status != MPI3MR_HDB_BUFSTATUS_POSTED_UNPAUSED) &&
	    (diag_buffer->status != MPI3MR_HDB_BUFSTATUS_POSTED_PAUSED))
		return;
	switch (evtdata->reason_code) {
	case MPI3_EVENT_DIAG_BUFFER_STATUS_CHANGE_RC_RELEASED:
	{
		diag_buffer->status = MPI3MR_HDB_BUFSTATUS_RELEASED;
		mpi3mr_set_trigger_data_in_hdb(diag_buffer,
		    MPI3MR_HDB_TRIGGER_TYPE_FW_RELEASED, NULL, 0);
		atomic64_inc(&event_counter);
		break;
	}
	case MPI3_EVENT_DIAG_BUFFER_STATUS_CHANGE_RC_RESUMED:
	{
		diag_buffer->status = MPI3MR_HDB_BUFSTATUS_POSTED_UNPAUSED;
		break;
	}
	case MPI3_EVENT_DIAG_BUFFER_STATUS_CHANGE_RC_PAUSED:
	{
		diag_buffer->status = MPI3MR_HDB_BUFSTATUS_POSTED_PAUSED;
		break;
	}
	default:
		dprint_event_th(mrioc, "%s: unknown reason_code(%d)\n",
		    __func__, evtdata->reason_code);
		break;
	}
}

/**
 * mpi3mr_diag_buffer_for_type - returns buffer desc for type
 * @mrioc: Adapter instance reference
 * @buf_type: Diagnostic buffer type
 *
 * Identifies matching diag descriptor from mrioc for given diag
 * buffer type.
 *
 * Return: diag buffer descriptor on success, NULL on failures.
 */

struct diag_buffer_desc *
mpi3mr_diag_buffer_for_type(struct mpi3mr_ioc *mrioc, u8 buf_type)
{
	u8 i;

	for (i = 0; i < MPI3MR_MAX_NUM_HDB; i++) {
		if (mrioc->diag_buffers[i].type == buf_type)
			return &mrioc->diag_buffers[i];
	}
	return NULL;
}

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
	if (mrioc->stop_bsgs || mrioc->block_on_pci_err) {
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
 * mpi3mr_bsg_refresh_hdb_triggers - Refresh HDB trigger data
 * @mrioc: Adapter instance reference
 * @job: BSG Job pointer
 *
 * This function reads the controller trigger config page as
 * defined by the input page type and refreshes the driver's
 * local trigger information structures with the controller's
 * config page data.
 *
 * Return: 0 on success and proper error codes on failure
 */
static long
mpi3mr_bsg_refresh_hdb_triggers(struct mpi3mr_ioc *mrioc,
				struct bsg_job *job)
{
	struct mpi3mr_bsg_out_refresh_hdb_triggers refresh_triggers;
	uint32_t data_out_sz;
	u8 page_action;
	long rval = -EINVAL;

	data_out_sz = job->request_payload.payload_len;

	if (data_out_sz != sizeof(refresh_triggers)) {
		dprint_bsg_err(mrioc, "%s: invalid size argument\n",
		    __func__);
		return rval;
	}

	if (mrioc->unrecoverable) {
		dprint_bsg_err(mrioc, "%s: unrecoverable controller\n",
		    __func__);
		return -EFAULT;
	}
	if (mrioc->reset_in_progress) {
		dprint_bsg_err(mrioc, "%s: reset in progress\n", __func__);
		return -EAGAIN;
	}

	sg_copy_to_buffer(job->request_payload.sg_list,
	    job->request_payload.sg_cnt,
	    &refresh_triggers, sizeof(refresh_triggers));

	switch (refresh_triggers.page_type) {
	case MPI3MR_HDB_REFRESH_TYPE_CURRENT:
		page_action = MPI3_CONFIG_ACTION_READ_CURRENT;
		break;
	case MPI3MR_HDB_REFRESH_TYPE_DEFAULT:
		page_action = MPI3_CONFIG_ACTION_READ_DEFAULT;
		break;
	case MPI3MR_HDB_HDB_REFRESH_TYPE_PERSISTENT:
		page_action = MPI3_CONFIG_ACTION_READ_PERSISTENT;
		break;
	default:
		dprint_bsg_err(mrioc,
		    "%s: unsupported refresh trigger, page_type %d\n",
		    __func__, refresh_triggers.page_type);
		return rval;
	}
	rval = mpi3mr_refresh_trigger(mrioc, page_action);

	return rval;
}

/**
 * mpi3mr_bsg_upload_hdb - Upload a specific HDB to user space
 * @mrioc: Adapter instance reference
 * @job: BSG Job pointer
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_bsg_upload_hdb(struct mpi3mr_ioc *mrioc,
				  struct bsg_job *job)
{
	struct mpi3mr_bsg_out_upload_hdb upload_hdb;
	struct diag_buffer_desc *diag_buffer;
	uint32_t data_out_size;
	uint32_t data_in_size;

	data_out_size = job->request_payload.payload_len;
	data_in_size = job->reply_payload.payload_len;

	if (data_out_size != sizeof(upload_hdb)) {
		dprint_bsg_err(mrioc, "%s: invalid size argument\n",
		    __func__);
		return -EINVAL;
	}

	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt,
			  &upload_hdb, sizeof(upload_hdb));

	if ((!upload_hdb.length) || (data_in_size != upload_hdb.length)) {
		dprint_bsg_err(mrioc, "%s: invalid length argument\n",
		    __func__);
		return -EINVAL;
	}
	diag_buffer = mpi3mr_diag_buffer_for_type(mrioc, upload_hdb.buf_type);
	if ((!diag_buffer) || (!diag_buffer->addr)) {
		dprint_bsg_err(mrioc, "%s: invalid buffer type %d\n",
		    __func__, upload_hdb.buf_type);
		return -EINVAL;
	}

	if ((diag_buffer->status != MPI3MR_HDB_BUFSTATUS_RELEASED) &&
	    (diag_buffer->status != MPI3MR_HDB_BUFSTATUS_POSTED_PAUSED)) {
		dprint_bsg_err(mrioc,
		    "%s: invalid buffer status %d for type %d\n",
		    __func__, diag_buffer->status, upload_hdb.buf_type);
		return -EINVAL;
	}

	if ((upload_hdb.start_offset + upload_hdb.length) > diag_buffer->size) {
		dprint_bsg_err(mrioc,
		    "%s: invalid start offset %d, length %d for type %d\n",
		    __func__, upload_hdb.start_offset, upload_hdb.length,
		    upload_hdb.buf_type);
		return -EINVAL;
	}
	sg_copy_from_buffer(job->reply_payload.sg_list,
			    job->reply_payload.sg_cnt,
	    (diag_buffer->addr + upload_hdb.start_offset),
	    data_in_size);
	return 0;
}

/**
 * mpi3mr_bsg_repost_hdb - Re-post HDB
 * @mrioc: Adapter instance reference
 * @job: BSG job pointer
 *
 * This function retrieves the HDB descriptor corresponding to a
 * given buffer type and if the HDB is in released status then
 * posts the HDB with the firmware.
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_bsg_repost_hdb(struct mpi3mr_ioc *mrioc,
				  struct bsg_job *job)
{
	struct mpi3mr_bsg_out_repost_hdb repost_hdb;
	struct diag_buffer_desc *diag_buffer;
	uint32_t data_out_sz;

	data_out_sz = job->request_payload.payload_len;

	if (data_out_sz != sizeof(repost_hdb)) {
		dprint_bsg_err(mrioc, "%s: invalid size argument\n",
		    __func__);
		return -EINVAL;
	}
	if (mrioc->unrecoverable) {
		dprint_bsg_err(mrioc, "%s: unrecoverable controller\n",
		    __func__);
		return -EFAULT;
	}
	if (mrioc->reset_in_progress) {
		dprint_bsg_err(mrioc, "%s: reset in progress\n", __func__);
		return -EAGAIN;
	}

	sg_copy_to_buffer(job->request_payload.sg_list,
			  job->request_payload.sg_cnt,
			  &repost_hdb, sizeof(repost_hdb));

	diag_buffer = mpi3mr_diag_buffer_for_type(mrioc, repost_hdb.buf_type);
	if ((!diag_buffer) || (!diag_buffer->addr)) {
		dprint_bsg_err(mrioc, "%s: invalid buffer type %d\n",
		    __func__, repost_hdb.buf_type);
		return -EINVAL;
	}

	if (diag_buffer->status != MPI3MR_HDB_BUFSTATUS_RELEASED) {
		dprint_bsg_err(mrioc,
		    "%s: invalid buffer status %d for type %d\n",
		    __func__, diag_buffer->status, repost_hdb.buf_type);
		return -EINVAL;
	}

	if (mpi3mr_issue_diag_buf_post(mrioc, diag_buffer)) {
		dprint_bsg_err(mrioc, "%s: post failed for type %d\n",
		    __func__, repost_hdb.buf_type);
		return -EFAULT;
	}
	mpi3mr_set_trigger_data_in_hdb(diag_buffer,
	    MPI3MR_HDB_TRIGGER_TYPE_UNKNOWN, NULL, 1);

	return 0;
}

/**
 * mpi3mr_bsg_query_hdb - Handler for query HDB command
 * @mrioc: Adapter instance reference
 * @job: BSG job pointer
 *
 * This function prepares and copies the host diagnostic buffer
 * entries to the user buffer.
 *
 * Return: 0 on success and proper error codes on failure
 */
static long mpi3mr_bsg_query_hdb(struct mpi3mr_ioc *mrioc,
				 struct bsg_job *job)
{
	long rval = 0;
	struct mpi3mr_bsg_in_hdb_status *hbd_status;
	struct mpi3mr_hdb_entry *hbd_status_entry;
	u32 length, min_length;
	u8 i;
	struct diag_buffer_desc *diag_buffer;
	uint32_t data_in_sz = 0;

	data_in_sz = job->request_payload.payload_len;

	length = (sizeof(*hbd_status) + ((MPI3MR_MAX_NUM_HDB - 1) *
		    sizeof(*hbd_status_entry)));
	hbd_status = kmalloc(length, GFP_KERNEL);
	if (!hbd_status)
		return -ENOMEM;
	hbd_status_entry = &hbd_status->entry[0];

	hbd_status->num_hdb_types = MPI3MR_MAX_NUM_HDB;
	for (i = 0; i < MPI3MR_MAX_NUM_HDB; i++) {
		diag_buffer = &mrioc->diag_buffers[i];
		hbd_status_entry->buf_type = diag_buffer->type;
		hbd_status_entry->status = diag_buffer->status;
		hbd_status_entry->trigger_type = diag_buffer->trigger_type;
		memcpy(&hbd_status_entry->trigger_data,
		    &diag_buffer->trigger_data,
		    sizeof(hbd_status_entry->trigger_data));
		hbd_status_entry->size = (diag_buffer->size / 1024);
		hbd_status_entry++;
	}
	hbd_status->element_trigger_format =
		MPI3MR_HDB_QUERY_ELEMENT_TRIGGER_FORMAT_DATA;

	if (data_in_sz < 4) {
		dprint_bsg_err(mrioc, "%s: invalid size passed\n", __func__);
		rval = -EINVAL;
		goto out;
	}
	min_length = min(data_in_sz, length);
	if (job->request_payload.payload_len >= min_length) {
		sg_copy_from_buffer(job->request_payload.sg_list,
				    job->request_payload.sg_cnt,
				    hbd_status, min_length);
		rval = 0;
	}
out:
	kfree(hbd_status);
	return rval;
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

	if (mrioc->unrecoverable) {
		dprint_bsg_err(mrioc, "%s: unrecoverable controller\n",
			       __func__);
		return -EFAULT;
	}

	if (mrioc->reset_in_progress) {
		dprint_bsg_err(mrioc, "%s: reset in progress\n", __func__);
		return -EAGAIN;
	}

	if (mrioc->stop_bsgs) {
		dprint_bsg_err(mrioc, "%s: bsgs are blocked\n", __func__);
		return -EAGAIN;
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
	u16 num_devices = 0, i = 0, size;
	unsigned long flags;
	struct mpi3mr_tgt_dev *tgtdev;
	struct mpi3mr_device_map_info *devmap_info = NULL;
	struct mpi3mr_all_tgt_info *alltgt_info = NULL;
	uint32_t min_entrylen = 0, kern_entrylen = 0, usr_entrylen = 0;

	if (job->request_payload.payload_len < sizeof(u32)) {
		dprint_bsg_err(mrioc, "%s: invalid size argument\n",
		    __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&mrioc->tgtdev_lock, flags);
	list_for_each_entry(tgtdev, &mrioc->tgtdev_list, list)
		num_devices++;
	spin_unlock_irqrestore(&mrioc->tgtdev_lock, flags);

	if ((job->request_payload.payload_len <= sizeof(u64)) ||
		list_empty(&mrioc->tgtdev_list)) {
		sg_copy_from_buffer(job->request_payload.sg_list,
				    job->request_payload.sg_cnt,
				    &num_devices, sizeof(num_devices));
		return 0;
	}

	kern_entrylen = num_devices * sizeof(*devmap_info);
	size = sizeof(u64) + kern_entrylen;
	alltgt_info = kzalloc(size, GFP_KERNEL);
	if (!alltgt_info)
		return -ENOMEM;

	devmap_info = alltgt_info->dmi;
	memset((u8 *)devmap_info, 0xFF, kern_entrylen);
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

	alltgt_info->num_devices = num_devices;

	usr_entrylen = (job->request_payload.payload_len - sizeof(u64)) /
		sizeof(*devmap_info);
	usr_entrylen *= sizeof(*devmap_info);
	min_entrylen = min(usr_entrylen, kern_entrylen);

	sg_copy_from_buffer(job->request_payload.sg_list,
			    job->request_payload.sg_cnt,
			    alltgt_info, (min_entrylen + sizeof(u64)));
	kfree(alltgt_info);
	return 0;
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

	if (mrioc->unrecoverable || mrioc->block_on_pci_err)
		return -EINVAL;

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
	case MPI3MR_DRVBSG_OPCODE_QUERY_HDB:
		rval = mpi3mr_bsg_query_hdb(mrioc, job);
		break;
	case MPI3MR_DRVBSG_OPCODE_REPOST_HDB:
		rval = mpi3mr_bsg_repost_hdb(mrioc, job);
		break;
	case MPI3MR_DRVBSG_OPCODE_UPLOAD_HDB:
		rval = mpi3mr_bsg_upload_hdb(mrioc, job);
		break;
	case MPI3MR_DRVBSG_OPCODE_REFRESH_HDB_TRIGGERS:
		rval = mpi3mr_bsg_refresh_hdb_triggers(mrioc, job);
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
 * mpi3mr_total_num_ioctl_sges - Count number of SGEs required
 * @drv_bufs: DMA address of the buffers to be placed in sgl
 * @bufcnt: Number of DMA buffers
 *
 * This function returns total number of data SGEs required
 * including zero length SGEs and excluding management request
 * and response buffer for the given list of data buffer
 * descriptors
 *
 * Return: Number of SGE elements needed
 */
static inline u16 mpi3mr_total_num_ioctl_sges(struct mpi3mr_buf_map *drv_bufs,
					      u8 bufcnt)
{
	u16 i, sge_count = 0;

	for (i = 0; i < bufcnt; i++, drv_bufs++) {
		if (drv_bufs->data_dir == DMA_NONE ||
		    drv_bufs->kern_buf)
			continue;
		sge_count += drv_bufs->num_dma_desc;
		if (!drv_bufs->num_dma_desc)
			sge_count++;
	}
	return sge_count;
}

/**
 * mpi3mr_bsg_build_sgl - SGL construction for MPI commands
 * @mrioc: Adapter instance reference
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
 * Return: 0 on success,-1 on failure
 */
static int mpi3mr_bsg_build_sgl(struct mpi3mr_ioc *mrioc, u8 *mpi_req,
				u32 sgl_offset, struct mpi3mr_buf_map *drv_bufs,
				u8 bufcnt, u8 is_rmc, u8 is_rmr, u8 num_datasges)
{
	struct mpi3_request_header *mpi_header =
		(struct mpi3_request_header *)mpi_req;
	u8 *sgl = (mpi_req + sgl_offset), count = 0;
	struct mpi3_mgmt_passthrough_request *rmgmt_req =
	    (struct mpi3_mgmt_passthrough_request *)mpi_req;
	struct mpi3mr_buf_map *drv_buf_iter = drv_bufs;
	u8 flag, sgl_flags, sgl_flag_eob, sgl_flags_last, last_chain_sgl_flag;
	u16 available_sges, i, sges_needed;
	u32 sge_element_size = sizeof(struct mpi3_sge_common);
	bool chain_used = false;

	sgl_flags = MPI3_SGE_FLAGS_ELEMENT_TYPE_SIMPLE |
		MPI3_SGE_FLAGS_DLAS_SYSTEM;
	sgl_flag_eob = sgl_flags | MPI3_SGE_FLAGS_END_OF_BUFFER;
	sgl_flags_last = sgl_flag_eob | MPI3_SGE_FLAGS_END_OF_LIST;
	last_chain_sgl_flag = MPI3_SGE_FLAGS_ELEMENT_TYPE_LAST_CHAIN |
	    MPI3_SGE_FLAGS_DLAS_SYSTEM;

	sges_needed = mpi3mr_total_num_ioctl_sges(drv_bufs, bufcnt);

	if (is_rmc) {
		mpi3mr_add_sg_single(&rmgmt_req->command_sgl,
		    sgl_flags_last, drv_buf_iter->kern_buf_len,
		    drv_buf_iter->kern_buf_dma);
		sgl = (u8 *)drv_buf_iter->kern_buf +
			drv_buf_iter->bsg_buf_len;
		available_sges = (drv_buf_iter->kern_buf_len -
		    drv_buf_iter->bsg_buf_len) / sge_element_size;

		if (sges_needed > available_sges)
			return -1;

		chain_used = true;
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
		if (num_datasges) {
			i = 0;
			goto build_sges;
		}
	} else {
		if (sgl_offset >= MPI3MR_ADMIN_REQ_FRAME_SZ)
			return -1;
		available_sges = (MPI3MR_ADMIN_REQ_FRAME_SZ - sgl_offset) /
		sge_element_size;
		if (!available_sges)
			return -1;
	}
	if (!num_datasges) {
		mpi3mr_build_zero_len_sge(sgl);
		return 0;
	}
	if (mpi_header->function == MPI3_BSG_FUNCTION_SMP_PASSTHROUGH) {
		if ((sges_needed > 2) || (sges_needed > available_sges))
			return -1;
		for (; count < bufcnt; count++, drv_buf_iter++) {
			if (drv_buf_iter->data_dir == DMA_NONE ||
			    !drv_buf_iter->num_dma_desc)
				continue;
			mpi3mr_add_sg_single(sgl, sgl_flags_last,
					     drv_buf_iter->dma_desc[0].size,
					     drv_buf_iter->dma_desc[0].dma_addr);
			sgl += sge_element_size;
		}
		return 0;
	}
	i = 0;

build_sges:
	for (; count < bufcnt; count++, drv_buf_iter++) {
		if (drv_buf_iter->data_dir == DMA_NONE)
			continue;
		if (!drv_buf_iter->num_dma_desc) {
			if (chain_used && !available_sges)
				return -1;
			if (!chain_used && (available_sges == 1) &&
			    (sges_needed > 1))
				goto setup_chain;
			flag = sgl_flag_eob;
			if (num_datasges == 1)
				flag = sgl_flags_last;
			mpi3mr_add_sg_single(sgl, flag, 0, 0);
			sgl += sge_element_size;
			sges_needed--;
			available_sges--;
			num_datasges--;
			continue;
		}
		for (; i < drv_buf_iter->num_dma_desc; i++) {
			if (chain_used && !available_sges)
				return -1;
			if (!chain_used && (available_sges == 1) &&
			    (sges_needed > 1))
				goto setup_chain;
			flag = sgl_flags;
			if (i == (drv_buf_iter->num_dma_desc - 1)) {
				if (num_datasges == 1)
					flag = sgl_flags_last;
				else
					flag = sgl_flag_eob;
			}

			mpi3mr_add_sg_single(sgl, flag,
					     drv_buf_iter->dma_desc[i].size,
					     drv_buf_iter->dma_desc[i].dma_addr);
			sgl += sge_element_size;
			available_sges--;
			sges_needed--;
		}
		num_datasges--;
		i = 0;
	}
	return 0;

setup_chain:
	available_sges = mrioc->ioctl_chain_sge.size / sge_element_size;
	if (sges_needed > available_sges)
		return -1;
	mpi3mr_add_sg_single(sgl, last_chain_sgl_flag,
			     (sges_needed * sge_element_size),
			     mrioc->ioctl_chain_sge.dma_addr);
	memset(mrioc->ioctl_chain_sge.addr, 0, mrioc->ioctl_chain_sge.size);
	sgl = (u8 *)mrioc->ioctl_chain_sge.addr;
	chain_used = true;
	goto build_sges;
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
	__le64 sgl_dma;
	u8 count;
	size_t length = 0;
	u16 available_sges = 0, i;
	u32 sge_element_size = sizeof(struct mpi3mr_nvme_pt_sge);
	struct mpi3mr_buf_map *drv_buf_iter = drv_bufs;
	u64 sgemod_mask = ((u64)((mrioc->facts.sge_mod_mask) <<
			    mrioc->facts.sge_mod_shift) << 32);
	u64 sgemod_val = ((u64)(mrioc->facts.sge_mod_value) <<
			  mrioc->facts.sge_mod_shift) << 32;
	u32 size;

	nvme_sgl = (struct mpi3mr_nvme_pt_sge *)
	    ((u8 *)(nvme_encap_request->command) + MPI3MR_NVME_CMD_SGL_OFFSET);

	/*
	 * Not all commands require a data transfer. If no data, just return
	 * without constructing any sgl.
	 */
	for (count = 0; count < bufcnt; count++, drv_buf_iter++) {
		if (drv_buf_iter->data_dir == DMA_NONE)
			continue;
		length = drv_buf_iter->kern_buf_len;
		break;
	}
	if (!length || !drv_buf_iter->num_dma_desc)
		return 0;

	if (drv_buf_iter->num_dma_desc == 1) {
		available_sges = 1;
		goto build_sges;
	}

	sgl_dma = cpu_to_le64(mrioc->ioctl_chain_sge.dma_addr);
	if (sgl_dma & sgemod_mask) {
		dprint_bsg_err(mrioc,
		    "%s: SGL chain address collides with SGE modifier\n",
		    __func__);
		return -1;
	}

	sgl_dma &= ~sgemod_mask;
	sgl_dma |= sgemod_val;

	memset(mrioc->ioctl_chain_sge.addr, 0, mrioc->ioctl_chain_sge.size);
	available_sges = mrioc->ioctl_chain_sge.size / sge_element_size;
	if (available_sges < drv_buf_iter->num_dma_desc)
		return -1;
	memset(nvme_sgl, 0, sizeof(struct mpi3mr_nvme_pt_sge));
	nvme_sgl->base_addr = sgl_dma;
	size = drv_buf_iter->num_dma_desc * sizeof(struct mpi3mr_nvme_pt_sge);
	nvme_sgl->length = cpu_to_le32(size);
	nvme_sgl->type = MPI3MR_NVMESGL_LAST_SEGMENT;
	nvme_sgl = (struct mpi3mr_nvme_pt_sge *)mrioc->ioctl_chain_sge.addr;

build_sges:
	for (i = 0; i < drv_buf_iter->num_dma_desc; i++) {
		sgl_dma = cpu_to_le64(drv_buf_iter->dma_desc[i].dma_addr);
		if (sgl_dma & sgemod_mask) {
			dprint_bsg_err(mrioc,
				       "%s: SGL address collides with SGE modifier\n",
				       __func__);
		return -1;
		}

		sgl_dma &= ~sgemod_mask;
		sgl_dma |= sgemod_val;

		nvme_sgl->base_addr = sgl_dma;
		nvme_sgl->length = cpu_to_le32(drv_buf_iter->dma_desc[i].size);
		nvme_sgl->type = MPI3MR_NVMESGL_DATA_SEGMENT;
		nvme_sgl++;
		available_sges--;
	}

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
	size_t length = 0, desc_len;
	u8 count;
	struct mpi3mr_buf_map *drv_buf_iter = drv_bufs;
	u64 sgemod_mask = ((u64)((mrioc->facts.sge_mod_mask) <<
			    mrioc->facts.sge_mod_shift) << 32);
	u64 sgemod_val = ((u64)(mrioc->facts.sge_mod_value) <<
			  mrioc->facts.sge_mod_shift) << 32;
	u16 dev_handle = nvme_encap_request->dev_handle;
	struct mpi3mr_tgt_dev *tgtdev;
	u16 desc_count = 0;

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
	page_mask = dev_pgsz - 1;

	if (dev_pgsz > MPI3MR_IOCTL_SGE_SIZE) {
		dprint_bsg_err(mrioc,
			       "%s: NVMe device page size(%d) is greater than ioctl data sge size(%d) for handle 0x%04x\n",
			       __func__, dev_pgsz,  MPI3MR_IOCTL_SGE_SIZE, dev_handle);
		return -1;
	}

	if (MPI3MR_IOCTL_SGE_SIZE % dev_pgsz) {
		dprint_bsg_err(mrioc,
			       "%s: ioctl data sge size(%d) is not a multiple of NVMe device page size(%d) for handle 0x%04x\n",
			       __func__, MPI3MR_IOCTL_SGE_SIZE, dev_pgsz, dev_handle);
		return -1;
	}

	/*
	 * Not all commands require a data transfer. If no data, just return
	 * without constructing any PRP.
	 */
	for (count = 0; count < bufcnt; count++, drv_buf_iter++) {
		if (drv_buf_iter->data_dir == DMA_NONE)
			continue;
		length = drv_buf_iter->kern_buf_len;
		break;
	}

	if (!length || !drv_buf_iter->num_dma_desc)
		return 0;

	for (count = 0; count < drv_buf_iter->num_dma_desc; count++) {
		dma_addr = drv_buf_iter->dma_desc[count].dma_addr;
		if (dma_addr & page_mask) {
			dprint_bsg_err(mrioc,
				       "%s:dma_addr %pad is not aligned with page size 0x%x\n",
				       __func__,  &dma_addr, dev_pgsz);
			return -1;
		}
	}

	dma_addr = drv_buf_iter->dma_desc[0].dma_addr;
	desc_len = drv_buf_iter->dma_desc[0].size;

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
			if (*prp_entry & sgemod_mask) {
				dprint_bsg_err(mrioc,
				    "%s: PRP address collides with SGE modifier\n",
				    __func__);
				goto err_out;
			}
			*prp_entry &= ~sgemod_mask;
			*prp_entry |= sgemod_val;
			prp_entry++;
			prp_entry_dma += prp_size;
		}

		/* decrement length accounting for last partial page. */
		if (entry_len >= length) {
			length = 0;
		} else {
			if (entry_len <= desc_len) {
				dma_addr += entry_len;
				desc_len -= entry_len;
			}
			if (!desc_len) {
				if ((++desc_count) >=
				   drv_buf_iter->num_dma_desc) {
					dprint_bsg_err(mrioc,
						       "%s: Invalid len %zd while building PRP\n",
						       __func__, length);
					goto err_out;
				}
				dma_addr =
				    drv_buf_iter->dma_desc[desc_count].dma_addr;
				desc_len =
				    drv_buf_iter->dma_desc[desc_count].size;
			}
			length -= entry_len;
		}
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
 * mpi3mr_map_data_buffer_dma - build dma descriptors for data
 *                              buffers
 * @mrioc: Adapter instance reference
 * @drv_buf: buffer map descriptor
 * @desc_count: Number of already consumed dma descriptors
 *
 * This function computes how many pre-allocated DMA descriptors
 * are required for the given data buffer and if those number of
 * descriptors are free, then setup the mapping of the scattered
 * DMA address to the given data buffer, if the data direction
 * of the buffer is DMA_TO_DEVICE then the actual data is copied to
 * the DMA buffers
 *
 * Return: 0 on success, -1 on failure
 */
static int mpi3mr_map_data_buffer_dma(struct mpi3mr_ioc *mrioc,
				      struct mpi3mr_buf_map *drv_buf,
				      u16 desc_count)
{
	u16 i, needed_desc = drv_buf->kern_buf_len / MPI3MR_IOCTL_SGE_SIZE;
	u32 buf_len = drv_buf->kern_buf_len, copied_len = 0;

	if (drv_buf->kern_buf_len % MPI3MR_IOCTL_SGE_SIZE)
		needed_desc++;
	if ((needed_desc + desc_count) > MPI3MR_NUM_IOCTL_SGE) {
		dprint_bsg_err(mrioc, "%s: DMA descriptor mapping error %d:%d:%d\n",
			       __func__, needed_desc, desc_count, MPI3MR_NUM_IOCTL_SGE);
		return -1;
	}
	drv_buf->dma_desc = kzalloc(sizeof(*drv_buf->dma_desc) * needed_desc,
				    GFP_KERNEL);
	if (!drv_buf->dma_desc)
		return -1;
	for (i = 0; i < needed_desc; i++, desc_count++) {
		drv_buf->dma_desc[i].addr = mrioc->ioctl_sge[desc_count].addr;
		drv_buf->dma_desc[i].dma_addr =
		    mrioc->ioctl_sge[desc_count].dma_addr;
		if (buf_len < mrioc->ioctl_sge[desc_count].size)
			drv_buf->dma_desc[i].size = buf_len;
		else
			drv_buf->dma_desc[i].size =
			    mrioc->ioctl_sge[desc_count].size;
		buf_len -= drv_buf->dma_desc[i].size;
		memset(drv_buf->dma_desc[i].addr, 0,
		       mrioc->ioctl_sge[desc_count].size);
		if (drv_buf->data_dir == DMA_TO_DEVICE) {
			memcpy(drv_buf->dma_desc[i].addr,
			       drv_buf->bsg_buf + copied_len,
			       drv_buf->dma_desc[i].size);
			copied_len += drv_buf->dma_desc[i].size;
		}
	}
	drv_buf->num_dma_desc = needed_desc;
	return 0;
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

static long mpi3mr_bsg_process_mpt_cmds(struct bsg_job *job)
{
	long rval = -EINVAL;
	struct mpi3mr_ioc *mrioc = NULL;
	u8 *mpi_req = NULL, *sense_buff_k = NULL;
	u8 mpi_msg_size = 0;
	struct mpi3mr_bsg_packet *bsg_req = NULL;
	struct mpi3mr_bsg_mptcmd *karg;
	struct mpi3mr_buf_entry *buf_entries = NULL;
	struct mpi3mr_buf_map *drv_bufs = NULL, *drv_buf_iter = NULL;
	u8 count, bufcnt = 0, is_rmcb = 0, is_rmrb = 0;
	u8 din_cnt = 0, dout_cnt = 0;
	u8 invalid_be = 0, erb_offset = 0xFF, mpirep_offset = 0xFF;
	u8 block_io = 0, nvme_fmt = 0, resp_code = 0;
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
	u16 rmc_size  = 0, desc_count = 0;

	bsg_req = job->request;
	karg = (struct mpi3mr_bsg_mptcmd *)&bsg_req->cmd.mptcmd;

	mrioc = mpi3mr_bsg_verify_adapter(karg->mrioc_id);
	if (!mrioc)
		return -ENODEV;

	if (mutex_lock_interruptible(&mrioc->bsg_cmds.mutex))
		return -ERESTARTSYS;

	if (mrioc->bsg_cmds.state & MPI3MR_CMD_PENDING) {
		dprint_bsg_err(mrioc, "%s: command is in use\n", __func__);
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		return -EAGAIN;
	}

	if (!mrioc->ioctl_sges_allocated) {
		dprint_bsg_err(mrioc, "%s: DMA memory was not allocated\n",
			       __func__);
		return -ENOMEM;
	}

	if (karg->timeout < MPI3MR_APP_DEFAULT_TIMEOUT)
		karg->timeout = MPI3MR_APP_DEFAULT_TIMEOUT;

	mpi_req = kzalloc(MPI3MR_ADMIN_REQ_FRAME_SZ, GFP_KERNEL);
	if (!mpi_req) {
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		return -ENOMEM;
	}
	mpi_header = (struct mpi3_request_header *)mpi_req;

	bufcnt = karg->buf_entry_list.num_of_entries;
	drv_bufs = kzalloc((sizeof(*drv_bufs) * bufcnt), GFP_KERNEL);
	if (!drv_bufs) {
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		rval = -ENOMEM;
		goto out;
	}

	dout_buf = kzalloc(job->request_payload.payload_len,
				      GFP_KERNEL);
	if (!dout_buf) {
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		rval = -ENOMEM;
		goto out;
	}

	din_buf = kzalloc(job->reply_payload.payload_len,
				     GFP_KERNEL);
	if (!din_buf) {
		mutex_unlock(&mrioc->bsg_cmds.mutex);
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

		switch (buf_entries->buf_type) {
		case MPI3MR_BSG_BUFTYPE_RAIDMGMT_CMD:
			sgl_iter = sgl_dout_iter;
			sgl_dout_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_TO_DEVICE;
			is_rmcb = 1;
			if ((count != 0) || !buf_entries->buf_len)
				invalid_be = 1;
			break;
		case MPI3MR_BSG_BUFTYPE_RAIDMGMT_RESP:
			sgl_iter = sgl_din_iter;
			sgl_din_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_FROM_DEVICE;
			is_rmrb = 1;
			if (count != 1 || !is_rmcb || !buf_entries->buf_len)
				invalid_be = 1;
			break;
		case MPI3MR_BSG_BUFTYPE_DATA_IN:
			sgl_iter = sgl_din_iter;
			sgl_din_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_FROM_DEVICE;
			din_cnt++;
			din_size += buf_entries->buf_len;
			if ((din_cnt > 1) && !is_rmcb)
				invalid_be = 1;
			break;
		case MPI3MR_BSG_BUFTYPE_DATA_OUT:
			sgl_iter = sgl_dout_iter;
			sgl_dout_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_TO_DEVICE;
			dout_cnt++;
			dout_size += buf_entries->buf_len;
			if ((dout_cnt > 1) && !is_rmcb)
				invalid_be = 1;
			break;
		case MPI3MR_BSG_BUFTYPE_MPI_REPLY:
			sgl_iter = sgl_din_iter;
			sgl_din_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_NONE;
			mpirep_offset = count;
			if (!buf_entries->buf_len)
				invalid_be = 1;
			break;
		case MPI3MR_BSG_BUFTYPE_ERR_RESPONSE:
			sgl_iter = sgl_din_iter;
			sgl_din_iter += buf_entries->buf_len;
			drv_buf_iter->data_dir = DMA_NONE;
			erb_offset = count;
			if (!buf_entries->buf_len)
				invalid_be = 1;
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
				mutex_unlock(&mrioc->bsg_cmds.mutex);
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
			mutex_unlock(&mrioc->bsg_cmds.mutex);
			rval = -EINVAL;
			goto out;
		}

		if (sgl_dout_iter > (dout_buf + job->request_payload.payload_len)) {
			dprint_bsg_err(mrioc, "%s: data_out buffer length mismatch\n",
				       __func__);
			mutex_unlock(&mrioc->bsg_cmds.mutex);
			rval = -EINVAL;
			goto out;
		}
		if (sgl_din_iter > (din_buf + job->reply_payload.payload_len)) {
			dprint_bsg_err(mrioc, "%s: data_in buffer length mismatch\n",
				       __func__);
			mutex_unlock(&mrioc->bsg_cmds.mutex);
			rval = -EINVAL;
			goto out;
		}

		drv_buf_iter->bsg_buf = sgl_iter;
		drv_buf_iter->bsg_buf_len = buf_entries->buf_len;
	}

	if (is_rmcb && ((din_size + dout_size) > MPI3MR_MAX_APP_XFER_SIZE)) {
		dprint_bsg_err(mrioc, "%s:%d: invalid data transfer size passed for function 0x%x din_size = %d, dout_size = %d\n",
			       __func__, __LINE__, mpi_header->function, din_size,
			       dout_size);
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		rval = -EINVAL;
		goto out;
	}

	if (din_size > MPI3MR_MAX_APP_XFER_SIZE) {
		dprint_bsg_err(mrioc,
		    "%s:%d: invalid data transfer size passed for function 0x%x din_size=%d\n",
		    __func__, __LINE__, mpi_header->function, din_size);
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		rval = -EINVAL;
		goto out;
	}
	if (dout_size > MPI3MR_MAX_APP_XFER_SIZE) {
		dprint_bsg_err(mrioc,
		    "%s:%d: invalid data transfer size passed for function 0x%x dout_size = %d\n",
		    __func__, __LINE__, mpi_header->function, dout_size);
		mutex_unlock(&mrioc->bsg_cmds.mutex);
		rval = -EINVAL;
		goto out;
	}

	if (mpi_header->function == MPI3_BSG_FUNCTION_SMP_PASSTHROUGH) {
		if (din_size > MPI3MR_IOCTL_SGE_SIZE ||
		    dout_size > MPI3MR_IOCTL_SGE_SIZE) {
			dprint_bsg_err(mrioc, "%s:%d: invalid message size passed:%d:%d:%d:%d\n",
				       __func__, __LINE__, din_cnt, dout_cnt, din_size,
			    dout_size);
			mutex_unlock(&mrioc->bsg_cmds.mutex);
			rval = -EINVAL;
			goto out;
		}
	}

	drv_buf_iter = drv_bufs;
	for (count = 0; count < bufcnt; count++, drv_buf_iter++) {
		if (drv_buf_iter->data_dir == DMA_NONE)
			continue;

		drv_buf_iter->kern_buf_len = drv_buf_iter->bsg_buf_len;
		if (is_rmcb && !count) {
			drv_buf_iter->kern_buf_len =
			    mrioc->ioctl_chain_sge.size;
			drv_buf_iter->kern_buf =
			    mrioc->ioctl_chain_sge.addr;
			drv_buf_iter->kern_buf_dma =
			    mrioc->ioctl_chain_sge.dma_addr;
			drv_buf_iter->dma_desc = NULL;
			drv_buf_iter->num_dma_desc = 0;
			memset(drv_buf_iter->kern_buf, 0,
			       drv_buf_iter->kern_buf_len);
			tmplen = min(drv_buf_iter->kern_buf_len,
				     drv_buf_iter->bsg_buf_len);
			rmc_size = tmplen;
			memcpy(drv_buf_iter->kern_buf, drv_buf_iter->bsg_buf, tmplen);
		} else if (is_rmrb && (count == 1)) {
			drv_buf_iter->kern_buf_len =
			    mrioc->ioctl_resp_sge.size;
			drv_buf_iter->kern_buf =
			    mrioc->ioctl_resp_sge.addr;
			drv_buf_iter->kern_buf_dma =
			    mrioc->ioctl_resp_sge.dma_addr;
			drv_buf_iter->dma_desc = NULL;
			drv_buf_iter->num_dma_desc = 0;
			memset(drv_buf_iter->kern_buf, 0,
			       drv_buf_iter->kern_buf_len);
			tmplen = min(drv_buf_iter->kern_buf_len,
				     drv_buf_iter->bsg_buf_len);
			drv_buf_iter->kern_buf_len = tmplen;
			memset(drv_buf_iter->bsg_buf, 0,
			       drv_buf_iter->bsg_buf_len);
		} else {
			if (!drv_buf_iter->kern_buf_len)
				continue;
			if (mpi3mr_map_data_buffer_dma(mrioc, drv_buf_iter, desc_count)) {
				rval = -ENOMEM;
				mutex_unlock(&mrioc->bsg_cmds.mutex);
				dprint_bsg_err(mrioc, "%s:%d: mapping data buffers failed\n",
					       __func__, __LINE__);
			goto out;
		}
			desc_count += drv_buf_iter->num_dma_desc;
		}
	}

	if (erb_offset != 0xFF) {
		sense_buff_k = kzalloc(erbsz, GFP_KERNEL);
		if (!sense_buff_k) {
			rval = -ENOMEM;
			mutex_unlock(&mrioc->bsg_cmds.mutex);
			goto out;
		}
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
	if (mrioc->stop_bsgs || mrioc->block_on_pci_err) {
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
		if (mpi3mr_bsg_build_sgl(mrioc, mpi_req, mpi_msg_size,
					 drv_bufs, bufcnt, is_rmcb, is_rmrb,
					 (dout_cnt + din_cnt))) {
			dprint_bsg_err(mrioc, "%s: sgl build failed\n", __func__);
			rval = -EAGAIN;
			mutex_unlock(&mrioc->bsg_cmds.mutex);
			goto out;
		}
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
			    rmc_size, "mpi3_mgmt_req");
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
		if (((mpi_header->function != MPI3_FUNCTION_SCSI_IO) &&
		    (mpi_header->function != MPI3_FUNCTION_NVME_ENCAPSULATED))
		    || (mrioc->logging_level & MPI3_DEBUG_BSG_ERROR)) {
			ioc_info(mrioc, "%s: bsg request timedout after %d seconds\n",
			    __func__, karg->timeout);
			if (!(mrioc->logging_level & MPI3_DEBUG_BSG_INFO)) {
				dprint_dump(mpi_req, MPI3MR_ADMIN_REQ_FRAME_SZ,
			    "bsg_mpi3_req");
			if (mpi_header->function ==
			    MPI3_FUNCTION_MGMT_PASSTHROUGH) {
				drv_buf_iter = &drv_bufs[0];
				dprint_dump(drv_buf_iter->kern_buf,
				    rmc_size, "mpi3_mgmt_req");
				}
			}
		}
		if ((mpi_header->function == MPI3_BSG_FUNCTION_NVME_ENCAPSULATED) ||
			(mpi_header->function == MPI3_BSG_FUNCTION_SCSI_IO)) {
			dprint_bsg_err(mrioc, "%s: bsg request timedout after %d seconds,\n"
				"issuing target reset to (0x%04x)\n", __func__,
				karg->timeout, mpi_header->function_dependent);
			mpi3mr_issue_tm(mrioc,
			    MPI3_SCSITASKMGMT_TASKTYPE_TARGET_RESET,
			    mpi_header->function_dependent, 0,
			    MPI3MR_HOSTTAG_BLK_TMS, MPI3MR_RESETTM_TIMEOUT,
			    &mrioc->host_tm_cmds, &resp_code, NULL);
		}
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
		drv_buf_iter->kern_buf_len = (sizeof(*bsg_reply_buf) +
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
		if ((count == 1) && is_rmrb) {
			memcpy(drv_buf_iter->bsg_buf,
			    drv_buf_iter->kern_buf,
			    drv_buf_iter->kern_buf_len);
		} else if (drv_buf_iter->data_dir == DMA_FROM_DEVICE) {
			tmplen = 0;
			for (desc_count = 0;
			    desc_count < drv_buf_iter->num_dma_desc;
			    desc_count++) {
				memcpy(((u8 *)drv_buf_iter->bsg_buf + tmplen),
				       drv_buf_iter->dma_desc[desc_count].addr,
				       drv_buf_iter->dma_desc[desc_count].size);
				tmplen +=
				    drv_buf_iter->dma_desc[desc_count].size;
		}
	}
	}

out_unlock:
	if (din_buf) {
		job->reply_payload_rcv_len =
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
		for (count = 0; count < bufcnt; count++, drv_buf_iter++)
			kfree(drv_buf_iter->dma_desc);
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
		rval = mpi3mr_bsg_process_mpt_cmds(job);
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
 * @mrioc: Adapter instance reference
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
 * @mrioc: Adapter instance reference
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
	struct queue_limits lim = {
		.max_hw_sectors		= MPI3MR_MAX_APP_XFER_SECTORS,
		.max_segments		= MPI3MR_MAX_APP_XFER_SEGMENTS,
	};

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

	mrioc->bsg_queue = bsg_setup_queue(bsg_dev, dev_name(bsg_dev), &lim,
			mpi3mr_bsg_request, NULL, 0);
	if (IS_ERR(mrioc->bsg_queue)) {
		ioc_err(mrioc, "%s: bsg registration failed\n",
		    dev_name(bsg_dev));
		device_del(bsg_dev);
		put_device(bsg_dev);
	}
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
	else if (mrioc->reset_in_progress || mrioc->stop_bsgs ||
		 mrioc->block_on_pci_err)
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

/**
 * sas_ncq_prio_supported_show - Indicate if device supports NCQ priority
 * @dev: pointer to embedded device
 * @attr: sas_ncq_prio_supported attribute descriptor
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' sdev attribute, only works with SATA devices
 */
static ssize_t
sas_ncq_prio_supported_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	return sysfs_emit(buf, "%d\n", sas_ata_ncq_prio_supported(sdev));
}
static DEVICE_ATTR_RO(sas_ncq_prio_supported);

/**
 * sas_ncq_prio_enable_show - send prioritized io commands to device
 * @dev: pointer to embedded device
 * @attr: sas_ncq_prio_enable attribute descriptor
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' sdev attribute, only works with SATA devices
 */
static ssize_t
sas_ncq_prio_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct mpi3mr_sdev_priv_data *sdev_priv_data =  sdev->hostdata;

	if (!sdev_priv_data)
		return 0;

	return sysfs_emit(buf, "%d\n", sdev_priv_data->ncq_prio_enable);
}

static ssize_t
sas_ncq_prio_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct mpi3mr_sdev_priv_data *sdev_priv_data =  sdev->hostdata;
	bool ncq_prio_enable = 0;

	if (kstrtobool(buf, &ncq_prio_enable))
		return -EINVAL;

	if (!sas_ata_ncq_prio_supported(sdev))
		return -EINVAL;

	sdev_priv_data->ncq_prio_enable = ncq_prio_enable;

	return strlen(buf);
}
static DEVICE_ATTR_RW(sas_ncq_prio_enable);

static struct attribute *mpi3mr_dev_attrs[] = {
	&dev_attr_sas_address.attr,
	&dev_attr_device_handle.attr,
	&dev_attr_persistent_id.attr,
	&dev_attr_sas_ncq_prio_supported.attr,
	&dev_attr_sas_ncq_prio_enable.attr,
	NULL,
};

static const struct attribute_group mpi3mr_dev_attr_group = {
	.attrs = mpi3mr_dev_attrs
};

const struct attribute_group *mpi3mr_dev_groups[] = {
	&mpi3mr_dev_attr_group,
	NULL,
};
