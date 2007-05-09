/* 
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * (C) Copyright IBM Corp. 2002, 2006
 * 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 */

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI

#include "zfcp_ext.h"
#include <asm/atomic.h>

static void zfcp_scsi_slave_destroy(struct scsi_device *sdp);
static int zfcp_scsi_slave_alloc(struct scsi_device *sdp);
static int zfcp_scsi_slave_configure(struct scsi_device *sdp);
static int zfcp_scsi_queuecommand(struct scsi_cmnd *,
				  void (*done) (struct scsi_cmnd *));
static int zfcp_scsi_eh_abort_handler(struct scsi_cmnd *);
static int zfcp_scsi_eh_device_reset_handler(struct scsi_cmnd *);
static int zfcp_scsi_eh_host_reset_handler(struct scsi_cmnd *);
static int zfcp_task_management_function(struct zfcp_unit *, u8,
					 struct scsi_cmnd *);

static struct zfcp_unit *zfcp_unit_lookup(struct zfcp_adapter *, int,
					  unsigned int, unsigned int);

static struct device_attribute *zfcp_sysfs_sdev_attrs[];

struct zfcp_data zfcp_data = {
	.scsi_host_template = {
		.name			= ZFCP_NAME,
		.module			= THIS_MODULE,
		.proc_name		= "zfcp",
		.slave_alloc		= zfcp_scsi_slave_alloc,
		.slave_configure	= zfcp_scsi_slave_configure,
		.slave_destroy		= zfcp_scsi_slave_destroy,
		.queuecommand		= zfcp_scsi_queuecommand,
		.eh_abort_handler	= zfcp_scsi_eh_abort_handler,
		.eh_device_reset_handler = zfcp_scsi_eh_device_reset_handler,
		.eh_bus_reset_handler	= zfcp_scsi_eh_host_reset_handler,
		.eh_host_reset_handler	= zfcp_scsi_eh_host_reset_handler,
		.can_queue		= 4096,
		.this_id		= -1,
		.sg_tablesize		= ZFCP_MAX_SBALES_PER_REQ,
		.cmd_per_lun		= 1,
		.use_clustering		= 1,
		.sdev_attrs		= zfcp_sysfs_sdev_attrs,
		.max_sectors		= ZFCP_MAX_SECTORS,
	},
	.driver_version = ZFCP_VERSION,
};

/* Find start of Response Information in FCP response unit*/
char *
zfcp_get_fcp_rsp_info_ptr(struct fcp_rsp_iu *fcp_rsp_iu)
{
	char *fcp_rsp_info_ptr;

	fcp_rsp_info_ptr =
	    (unsigned char *) fcp_rsp_iu + (sizeof (struct fcp_rsp_iu));

	return fcp_rsp_info_ptr;
}

/* Find start of Sense Information in FCP response unit*/
char *
zfcp_get_fcp_sns_info_ptr(struct fcp_rsp_iu *fcp_rsp_iu)
{
	char *fcp_sns_info_ptr;

	fcp_sns_info_ptr =
	    (unsigned char *) fcp_rsp_iu + (sizeof (struct fcp_rsp_iu));
	if (fcp_rsp_iu->validity.bits.fcp_rsp_len_valid)
		fcp_sns_info_ptr = (char *) fcp_sns_info_ptr +
		    fcp_rsp_iu->fcp_rsp_len;

	return fcp_sns_info_ptr;
}

static fcp_dl_t *
zfcp_get_fcp_dl_ptr(struct fcp_cmnd_iu * fcp_cmd)
{
	int additional_length = fcp_cmd->add_fcp_cdb_length << 2;
	fcp_dl_t *fcp_dl_addr;

	fcp_dl_addr = (fcp_dl_t *)
		((unsigned char *) fcp_cmd +
		 sizeof (struct fcp_cmnd_iu) + additional_length);
	/*
	 * fcp_dl_addr = start address of fcp_cmnd structure + 
	 * size of fixed part + size of dynamically sized add_dcp_cdb field
	 * SEE FCP-2 documentation
	 */
	return fcp_dl_addr;
}

fcp_dl_t
zfcp_get_fcp_dl(struct fcp_cmnd_iu * fcp_cmd)
{
	return *zfcp_get_fcp_dl_ptr(fcp_cmd);
}

void
zfcp_set_fcp_dl(struct fcp_cmnd_iu *fcp_cmd, fcp_dl_t fcp_dl)
{
	*zfcp_get_fcp_dl_ptr(fcp_cmd) = fcp_dl;
}

/*
 * note: it's a bit-or operation not an assignment
 * regarding the specified byte
 */
static inline void
set_byte(int *result, char status, char pos)
{
	*result |= status << (pos * 8);
}

void
set_host_byte(int *result, char status)
{
	set_byte(result, status, 2);
}

void
set_driver_byte(int *result, char status)
{
	set_byte(result, status, 3);
}

static int
zfcp_scsi_slave_alloc(struct scsi_device *sdp)
{
	struct zfcp_adapter *adapter;
	struct zfcp_unit *unit;
	unsigned long flags;
	int retval = -ENXIO;

	adapter = (struct zfcp_adapter *) sdp->host->hostdata[0];
	if (!adapter)
		goto out;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	unit = zfcp_unit_lookup(adapter, sdp->channel, sdp->id, sdp->lun);
	if (unit && atomic_test_mask(ZFCP_STATUS_UNIT_REGISTERED,
				     &unit->status)) {
		sdp->hostdata = unit;
		unit->device = sdp;
		zfcp_unit_get(unit);
		retval = 0;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
 out:
	return retval;
}

/**
 * zfcp_scsi_slave_destroy - called when scsi device is removed
 *
 * Remove reference to associated scsi device for an zfcp_unit.
 * Mark zfcp_unit as failed. The scsi device might be deleted via sysfs
 * or a scan for this device might have failed.
 */
static void zfcp_scsi_slave_destroy(struct scsi_device *sdpnt)
{
	struct zfcp_unit *unit = (struct zfcp_unit *) sdpnt->hostdata;

	if (unit) {
		zfcp_erp_wait(unit->port->adapter);
		wait_event(unit->scsi_scan_wq,
			   atomic_test_mask(ZFCP_STATUS_UNIT_SCSI_WORK_PENDING,
					    &unit->status) == 0);
		atomic_clear_mask(ZFCP_STATUS_UNIT_REGISTERED, &unit->status);
		sdpnt->hostdata = NULL;
		unit->device = NULL;
		zfcp_erp_unit_failed(unit);
		zfcp_unit_put(unit);
	} else {
		ZFCP_LOG_NORMAL("bug: no unit associated with SCSI device at "
				"address %p\n", sdpnt);
	}
}

/* 
 * called from scsi midlayer to allow finetuning of a device.
 */
static int
zfcp_scsi_slave_configure(struct scsi_device *sdp)
{
	if (sdp->tagged_supported)
		scsi_adjust_queue_depth(sdp, MSG_SIMPLE_TAG, ZFCP_CMND_PER_LUN);
	else
		scsi_adjust_queue_depth(sdp, 0, 1);
	return 0;
}

/**
 * zfcp_scsi_command_fail - set result in scsi_cmnd and call scsi_done function
 * @scpnt: pointer to struct scsi_cmnd where result is set
 * @result: result to be set in scpnt (e.g. DID_ERROR)
 */
static void
zfcp_scsi_command_fail(struct scsi_cmnd *scpnt, int result)
{
	set_host_byte(&scpnt->result, result);
	if ((scpnt->device != NULL) && (scpnt->device->host != NULL))
		zfcp_scsi_dbf_event_result("fail", 4,
			(struct zfcp_adapter*) scpnt->device->host->hostdata[0],
			scpnt, NULL);
	/* return directly */
	scpnt->scsi_done(scpnt);
}

/**
 * zfcp_scsi_command_async - worker for zfcp_scsi_queuecommand and
 *	zfcp_scsi_command_sync
 * @adapter: adapter where scsi command is issued
 * @unit: unit to which scsi command is sent
 * @scpnt: scsi command to be sent
 * @timer: timer to be started if request is successfully initiated
 *
 * Note: In scsi_done function must be set in scpnt.
 */
int
zfcp_scsi_command_async(struct zfcp_adapter *adapter, struct zfcp_unit *unit,
			struct scsi_cmnd *scpnt, int use_timer)
{
	int tmp;
	int retval;

	retval = 0;

	BUG_ON((adapter == NULL) || (adapter != unit->port->adapter));
	BUG_ON(scpnt->scsi_done == NULL);

	if (unlikely(NULL == unit)) {
		zfcp_scsi_command_fail(scpnt, DID_NO_CONNECT);
		goto out;
	}

	if (unlikely(
	      atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &unit->status) ||
	     !atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status))) {
		ZFCP_LOG_DEBUG("stopping SCSI I/O on unit 0x%016Lx on port "
			       "0x%016Lx on adapter %s\n",
			       unit->fcp_lun, unit->port->wwpn,
			       zfcp_get_busid_by_adapter(adapter));
		zfcp_scsi_command_fail(scpnt, DID_ERROR);
		goto out;
	}

	if (unlikely(
	     !atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &unit->status))) {
		ZFCP_LOG_DEBUG("adapter %s not ready or unit 0x%016Lx "
			       "on port 0x%016Lx in recovery\n",
			       zfcp_get_busid_by_unit(unit),
			       unit->fcp_lun, unit->port->wwpn);
		zfcp_scsi_command_fail(scpnt, DID_NO_CONNECT);
		goto out;
	}

	tmp = zfcp_fsf_send_fcp_command_task(adapter, unit, scpnt, use_timer,
					     ZFCP_REQ_AUTO_CLEANUP);

	if (unlikely(tmp < 0)) {
		ZFCP_LOG_DEBUG("error: initiation of Send FCP Cmnd failed\n");
		retval = SCSI_MLQUEUE_HOST_BUSY;
	}

out:
	return retval;
}

static void
zfcp_scsi_command_sync_handler(struct scsi_cmnd *scpnt)
{
	struct completion *wait = (struct completion *) scpnt->SCp.ptr;
	complete(wait);
}


/**
 * zfcp_scsi_command_sync - send a SCSI command and wait for completion
 * @unit: unit where command is sent to
 * @scpnt: scsi command to be sent
 * @use_timer: indicates whether timer should be setup or not
 * Return: 0
 *
 * Errors are indicated in scpnt->result
 */
int
zfcp_scsi_command_sync(struct zfcp_unit *unit, struct scsi_cmnd *scpnt,
		       int use_timer)
{
	int ret;
	DECLARE_COMPLETION_ONSTACK(wait);

	scpnt->SCp.ptr = (void *) &wait;  /* silent re-use */
	scpnt->scsi_done = zfcp_scsi_command_sync_handler;
	ret = zfcp_scsi_command_async(unit->port->adapter, unit, scpnt,
				      use_timer);
	if (ret == 0)
		wait_for_completion(&wait);

	scpnt->SCp.ptr = NULL;

	return 0;
}

/*
 * function:	zfcp_scsi_queuecommand
 *
 * purpose:	enqueues a SCSI command to the specified target device
 *
 * returns:	0 - success, SCSI command enqueued
 *		!0 - failure
 */
static int
zfcp_scsi_queuecommand(struct scsi_cmnd *scpnt,
		       void (*done) (struct scsi_cmnd *))
{
	struct zfcp_unit *unit;
	struct zfcp_adapter *adapter;

	/* reset the status for this request */
	scpnt->result = 0;
	scpnt->host_scribble = NULL;
	scpnt->scsi_done = done;

	/*
	 * figure out adapter and target device
	 * (stored there by zfcp_scsi_slave_alloc)
	 */
	adapter = (struct zfcp_adapter *) scpnt->device->host->hostdata[0];
	unit = (struct zfcp_unit *) scpnt->device->hostdata;

	return zfcp_scsi_command_async(adapter, unit, scpnt, 0);
}

static struct zfcp_unit *
zfcp_unit_lookup(struct zfcp_adapter *adapter, int channel, unsigned int id,
		 unsigned int lun)
{
	struct zfcp_port *port;
	struct zfcp_unit *unit, *retval = NULL;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (!port->rport || (id != port->rport->scsi_target_id))
			continue;
		list_for_each_entry(unit, &port->unit_list_head, list) {
			if (lun == unit->scsi_lun) {
				retval = unit;
				goto out;
			}
		}
	}
 out:
	return retval;
}

/**
 * zfcp_scsi_eh_abort_handler - abort the specified SCSI command
 * @scpnt: pointer to scsi_cmnd to be aborted 
 * Return: SUCCESS - command has been aborted and cleaned up in internal
 *          bookkeeping, SCSI stack won't be called for aborted command
 *         FAILED - otherwise
 *
 * We do not need to care for a SCSI command which completes normally
 * but late during this abort routine runs.  We are allowed to return
 * late commands to the SCSI stack.  It tracks the state of commands and
 * will handle late commands.  (Usually, the normal completion of late
 * commands is ignored with respect to the running abort operation.)
 */
static int zfcp_scsi_eh_abort_handler(struct scsi_cmnd *scpnt)
{
 	struct Scsi_Host *scsi_host;
 	struct zfcp_adapter *adapter;
	struct zfcp_unit *unit;
	struct zfcp_fsf_req *fsf_req;
	unsigned long flags;
	unsigned long old_req_id;
	int retval = SUCCESS;

	scsi_host = scpnt->device->host;
	adapter = (struct zfcp_adapter *) scsi_host->hostdata[0];
	unit = (struct zfcp_unit *) scpnt->device->hostdata;

	ZFCP_LOG_INFO("aborting scsi_cmnd=%p on adapter %s\n",
		      scpnt, zfcp_get_busid_by_adapter(adapter));

	/* avoid race condition between late normal completion and abort */
	write_lock_irqsave(&adapter->abort_lock, flags);

	/* Check whether corresponding fsf_req is still pending */
	spin_lock(&adapter->req_list_lock);
	fsf_req = zfcp_reqlist_find(adapter,
				    (unsigned long) scpnt->host_scribble);
	spin_unlock(&adapter->req_list_lock);
	if (!fsf_req) {
		write_unlock_irqrestore(&adapter->abort_lock, flags);
		zfcp_scsi_dbf_event_abort("lte1", adapter, scpnt, NULL, 0);
		retval = SUCCESS;
		goto out;
	}
	fsf_req->data = 0;
	fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTING;
	old_req_id = fsf_req->req_id;

	/* don't access old fsf_req after releasing the abort_lock */
	write_unlock_irqrestore(&adapter->abort_lock, flags);

	fsf_req = zfcp_fsf_abort_fcp_command(old_req_id, adapter, unit, 0);
	if (!fsf_req) {
		ZFCP_LOG_INFO("error: initiation of Abort FCP Cmnd failed\n");
		zfcp_scsi_dbf_event_abort("nres", adapter, scpnt, NULL,
					  old_req_id);
		retval = FAILED;
		goto out;
	}

	__wait_event(fsf_req->completion_wq,
		     fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED) {
		zfcp_scsi_dbf_event_abort("okay", adapter, scpnt, fsf_req, 0);
		retval = SUCCESS;
	} else if (fsf_req->status & ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED) {
		zfcp_scsi_dbf_event_abort("lte2", adapter, scpnt, fsf_req, 0);
		retval = SUCCESS;
	} else {
		zfcp_scsi_dbf_event_abort("fail", adapter, scpnt, fsf_req, 0);
		retval = FAILED;
	}
	zfcp_fsf_req_free(fsf_req);
 out:
	return retval;
}

static int
zfcp_scsi_eh_device_reset_handler(struct scsi_cmnd *scpnt)
{
	int retval;
	struct zfcp_unit *unit = (struct zfcp_unit *) scpnt->device->hostdata;

	if (!unit) {
		ZFCP_LOG_NORMAL("bug: Tried reset for nonexistent unit\n");
		retval = SUCCESS;
		goto out;
	}
	ZFCP_LOG_NORMAL("resetting unit 0x%016Lx\n", unit->fcp_lun);

	/*
	 * If we do not know whether the unit supports 'logical unit reset'
	 * then try 'logical unit reset' and proceed with 'target reset'
	 * if 'logical unit reset' fails.
	 * If the unit is known not to support 'logical unit reset' then
	 * skip 'logical unit reset' and try 'target reset' immediately.
	 */
	if (!atomic_test_mask(ZFCP_STATUS_UNIT_NOTSUPPUNITRESET,
			      &unit->status)) {
		retval = zfcp_task_management_function(unit,
						       FCP_LOGICAL_UNIT_RESET,
						       scpnt);
		if (retval) {
			ZFCP_LOG_DEBUG("unit reset failed (unit=%p)\n", unit);
			if (retval == -ENOTSUPP)
				atomic_set_mask
				    (ZFCP_STATUS_UNIT_NOTSUPPUNITRESET,
				     &unit->status);
			/* fall through and try 'target reset' next */
		} else {
			ZFCP_LOG_DEBUG("unit reset succeeded (unit=%p)\n",
				       unit);
			/* avoid 'target reset' */
			retval = SUCCESS;
			goto out;
		}
	}
	retval = zfcp_task_management_function(unit, FCP_TARGET_RESET, scpnt);
	if (retval) {
		ZFCP_LOG_DEBUG("target reset failed (unit=%p)\n", unit);
		retval = FAILED;
	} else {
		ZFCP_LOG_DEBUG("target reset succeeded (unit=%p)\n", unit);
		retval = SUCCESS;
	}
 out:
	return retval;
}

static int
zfcp_task_management_function(struct zfcp_unit *unit, u8 tm_flags,
			      struct scsi_cmnd *scpnt)
{
	struct zfcp_adapter *adapter = unit->port->adapter;
	struct zfcp_fsf_req *fsf_req;
	int retval = 0;

	/* issue task management function */
	fsf_req = zfcp_fsf_send_fcp_command_task_management
		(adapter, unit, tm_flags, 0);
	if (!fsf_req) {
		ZFCP_LOG_INFO("error: creation of task management request "
			      "failed for unit 0x%016Lx on port 0x%016Lx on  "
			      "adapter %s\n", unit->fcp_lun, unit->port->wwpn,
			      zfcp_get_busid_by_adapter(adapter));
		zfcp_scsi_dbf_event_devreset("nres", tm_flags, unit, scpnt);
		retval = -ENOMEM;
		goto out;
	}

	__wait_event(fsf_req->completion_wq,
		     fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	/*
	 * check completion status of task management function
	 */
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_TMFUNCFAILED) {
		zfcp_scsi_dbf_event_devreset("fail", tm_flags, unit, scpnt);
		retval = -EIO;
	} else if (fsf_req->status & ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP) {
		zfcp_scsi_dbf_event_devreset("nsup", tm_flags, unit, scpnt);
		retval = -ENOTSUPP;
	} else
		zfcp_scsi_dbf_event_devreset("okay", tm_flags, unit, scpnt);

	zfcp_fsf_req_free(fsf_req);
 out:
	return retval;
}

/**
 * zfcp_scsi_eh_host_reset_handler - handler for host and bus reset
 */
static int zfcp_scsi_eh_host_reset_handler(struct scsi_cmnd *scpnt)
{
	struct zfcp_unit *unit;
	struct zfcp_adapter *adapter;

	unit = (struct zfcp_unit*) scpnt->device->hostdata;
	adapter = unit->port->adapter;

	ZFCP_LOG_NORMAL("host/bus reset because of problems with "
			"unit 0x%016Lx\n", unit->fcp_lun);

	zfcp_erp_adapter_reopen(adapter, 0);
	zfcp_erp_wait(adapter);

	return SUCCESS;
}

int
zfcp_adapter_scsi_register(struct zfcp_adapter *adapter)
{
	int retval = 0;
	static unsigned int unique_id = 0;

	if (adapter->scsi_host)
		goto out;

	/* register adapter as SCSI host with mid layer of SCSI stack */
	adapter->scsi_host = scsi_host_alloc(&zfcp_data.scsi_host_template,
					     sizeof (struct zfcp_adapter *));
	if (!adapter->scsi_host) {
		ZFCP_LOG_NORMAL("error: registration with SCSI stack failed "
				"for adapter %s ",
				zfcp_get_busid_by_adapter(adapter));
		retval = -EIO;
		goto out;
	}
	ZFCP_LOG_DEBUG("host registered, scsi_host=%p\n", adapter->scsi_host);

	/* tell the SCSI stack some characteristics of this adapter */
	adapter->scsi_host->max_id = 1;
	adapter->scsi_host->max_lun = 1;
	adapter->scsi_host->max_channel = 0;
	adapter->scsi_host->unique_id = unique_id++;	/* FIXME */
	adapter->scsi_host->max_cmd_len = ZFCP_MAX_SCSI_CMND_LENGTH;
	adapter->scsi_host->transportt = zfcp_data.scsi_transport_template;

	/*
	 * save a pointer to our own adapter data structure within
	 * hostdata field of SCSI host data structure
	 */
	adapter->scsi_host->hostdata[0] = (unsigned long) adapter;

	if (scsi_add_host(adapter->scsi_host, &adapter->ccw_device->dev)) {
		scsi_host_put(adapter->scsi_host);
		retval = -EIO;
		goto out;
	}
	atomic_set_mask(ZFCP_STATUS_ADAPTER_REGISTERED, &adapter->status);
 out:
	return retval;
}

void
zfcp_adapter_scsi_unregister(struct zfcp_adapter *adapter)
{
	struct Scsi_Host *shost;
	struct zfcp_port *port;

	shost = adapter->scsi_host;
	if (!shost)
		return;
	read_lock_irq(&zfcp_data.config_lock);
	list_for_each_entry(port, &adapter->port_list_head, list)
		if (port->rport)
			port->rport = NULL;
	read_unlock_irq(&zfcp_data.config_lock);
	fc_remove_host(shost);
	scsi_remove_host(shost);
	scsi_host_put(shost);
	adapter->scsi_host = NULL;
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_REGISTERED, &adapter->status);

	return;
}

/*
 * Support functions for FC transport class
 */
static struct fc_host_statistics*
zfcp_init_fc_host_stats(struct zfcp_adapter *adapter)
{
	struct fc_host_statistics *fc_stats;

	if (!adapter->fc_stats) {
		fc_stats = kmalloc(sizeof(*fc_stats), GFP_KERNEL);
		if (!fc_stats)
			return NULL;
		adapter->fc_stats = fc_stats; /* freed in adater_dequeue */
	}
	memset(adapter->fc_stats, 0, sizeof(*adapter->fc_stats));
	return adapter->fc_stats;
}

static void
zfcp_adjust_fc_host_stats(struct fc_host_statistics *fc_stats,
			  struct fsf_qtcb_bottom_port *data,
			  struct fsf_qtcb_bottom_port *old)
{
	fc_stats->seconds_since_last_reset = data->seconds_since_last_reset -
		old->seconds_since_last_reset;
	fc_stats->tx_frames = data->tx_frames - old->tx_frames;
	fc_stats->tx_words = data->tx_words - old->tx_words;
	fc_stats->rx_frames = data->rx_frames - old->rx_frames;
	fc_stats->rx_words = data->rx_words - old->rx_words;
	fc_stats->lip_count = data->lip - old->lip;
	fc_stats->nos_count = data->nos - old->nos;
	fc_stats->error_frames = data->error_frames - old->error_frames;
	fc_stats->dumped_frames = data->dumped_frames - old->dumped_frames;
	fc_stats->link_failure_count = data->link_failure - old->link_failure;
	fc_stats->loss_of_sync_count = data->loss_of_sync - old->loss_of_sync;
	fc_stats->loss_of_signal_count = data->loss_of_signal -
		old->loss_of_signal;
	fc_stats->prim_seq_protocol_err_count = data->psp_error_counts -
		old->psp_error_counts;
	fc_stats->invalid_tx_word_count = data->invalid_tx_words -
		old->invalid_tx_words;
	fc_stats->invalid_crc_count = data->invalid_crcs - old->invalid_crcs;
	fc_stats->fcp_input_requests = data->input_requests -
		old->input_requests;
	fc_stats->fcp_output_requests = data->output_requests -
		old->output_requests;
	fc_stats->fcp_control_requests = data->control_requests -
		old->control_requests;
	fc_stats->fcp_input_megabytes = data->input_mb - old->input_mb;
	fc_stats->fcp_output_megabytes = data->output_mb - old->output_mb;
}

static void
zfcp_set_fc_host_stats(struct fc_host_statistics *fc_stats,
		       struct fsf_qtcb_bottom_port *data)
{
	fc_stats->seconds_since_last_reset = data->seconds_since_last_reset;
	fc_stats->tx_frames = data->tx_frames;
	fc_stats->tx_words = data->tx_words;
	fc_stats->rx_frames = data->rx_frames;
	fc_stats->rx_words = data->rx_words;
	fc_stats->lip_count = data->lip;
	fc_stats->nos_count = data->nos;
	fc_stats->error_frames = data->error_frames;
	fc_stats->dumped_frames = data->dumped_frames;
	fc_stats->link_failure_count = data->link_failure;
	fc_stats->loss_of_sync_count = data->loss_of_sync;
	fc_stats->loss_of_signal_count = data->loss_of_signal;
	fc_stats->prim_seq_protocol_err_count = data->psp_error_counts;
	fc_stats->invalid_tx_word_count = data->invalid_tx_words;
	fc_stats->invalid_crc_count = data->invalid_crcs;
	fc_stats->fcp_input_requests = data->input_requests;
	fc_stats->fcp_output_requests = data->output_requests;
	fc_stats->fcp_control_requests = data->control_requests;
	fc_stats->fcp_input_megabytes = data->input_mb;
	fc_stats->fcp_output_megabytes = data->output_mb;
}

/**
 * zfcp_get_fc_host_stats - provide fc_host_statistics for scsi_transport_fc
 *
 * assumption: scsi_transport_fc synchronizes calls of
 *             get_fc_host_stats and reset_fc_host_stats
 *             (XXX to be checked otherwise introduce locking)
 */
static struct fc_host_statistics *
zfcp_get_fc_host_stats(struct Scsi_Host *shost)
{
	struct zfcp_adapter *adapter;
	struct fc_host_statistics *fc_stats;
	struct fsf_qtcb_bottom_port *data;
	int ret;

	adapter = (struct zfcp_adapter *)shost->hostdata[0];
	fc_stats = zfcp_init_fc_host_stats(adapter);
	if (!fc_stats)
		return NULL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	ret = zfcp_fsf_exchange_port_data(NULL, adapter, data);
	if (ret) {
		kfree(data);
		return NULL; /* XXX return zeroed fc_stats? */
	}

	if (adapter->stats_reset &&
	    ((jiffies/HZ - adapter->stats_reset) <
	     data->seconds_since_last_reset)) {
		zfcp_adjust_fc_host_stats(fc_stats, data,
					  adapter->stats_reset_data);
	} else
		zfcp_set_fc_host_stats(fc_stats, data);

	kfree(data);
	return fc_stats;
}

static void
zfcp_reset_fc_host_stats(struct Scsi_Host *shost)
{
	struct zfcp_adapter *adapter;
	struct fsf_qtcb_bottom_port *data, *old_data;
	int ret;

	adapter = (struct zfcp_adapter *)shost->hostdata[0];
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	ret = zfcp_fsf_exchange_port_data(NULL, adapter, data);
	if (ret == 0) {
		adapter->stats_reset = jiffies/HZ;
		old_data = adapter->stats_reset_data;
		adapter->stats_reset_data = data; /* finally freed in
						     adater_dequeue */
		kfree(old_data);
	}
}

static void zfcp_set_rport_dev_loss_tmo(struct fc_rport *rport, u32 timeout)
{
	rport->dev_loss_tmo = timeout;
}

struct fc_function_template zfcp_transport_functions = {
	.show_starget_port_id = 1,
	.show_starget_port_name = 1,
	.show_starget_node_name = 1,
	.show_rport_supported_classes = 1,
	.show_rport_maxframe_size = 1,
	.show_rport_dev_loss_tmo = 1,
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_permanent_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,
	.show_host_serial_number = 1,
	.get_fc_host_stats = zfcp_get_fc_host_stats,
	.reset_fc_host_stats = zfcp_reset_fc_host_stats,
	.set_rport_dev_loss_tmo = zfcp_set_rport_dev_loss_tmo,
	/* no functions registered for following dynamic attributes but
	   directly set by LLDD */
	.show_host_port_type = 1,
	.show_host_speed = 1,
	.show_host_port_id = 1,
};

/**
 * ZFCP_DEFINE_SCSI_ATTR
 * @_name:   name of show attribute
 * @_format: format string
 * @_value:  value to print
 *
 * Generates attribute for a unit.
 */
#define ZFCP_DEFINE_SCSI_ATTR(_name, _format, _value)                    \
static ssize_t zfcp_sysfs_scsi_##_name##_show(struct device *dev, struct device_attribute *attr,        \
                                              char *buf)                 \
{                                                                        \
        struct scsi_device *sdev;                                        \
        struct zfcp_unit *unit;                                          \
                                                                         \
        sdev = to_scsi_device(dev);                                      \
        unit = sdev->hostdata;                                           \
        return sprintf(buf, _format, _value);                            \
}                                                                        \
                                                                         \
static DEVICE_ATTR(_name, S_IRUGO, zfcp_sysfs_scsi_##_name##_show, NULL);

ZFCP_DEFINE_SCSI_ATTR(hba_id, "%s\n", zfcp_get_busid_by_unit(unit));
ZFCP_DEFINE_SCSI_ATTR(wwpn, "0x%016llx\n", unit->port->wwpn);
ZFCP_DEFINE_SCSI_ATTR(fcp_lun, "0x%016llx\n", unit->fcp_lun);

static struct device_attribute *zfcp_sysfs_sdev_attrs[] = {
	&dev_attr_fcp_lun,
	&dev_attr_wwpn,
	&dev_attr_hba_id,
	NULL
};

#undef ZFCP_LOG_AREA
