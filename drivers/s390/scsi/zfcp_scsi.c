/*
 * zfcp device driver
 *
 * Interface to Linux SCSI midlayer.
 *
 * Copyright IBM Corporation 2002, 2009
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include "zfcp_ext.h"
#include <asm/atomic.h>

/* Find start of Sense Information in FCP response unit*/
char *zfcp_get_fcp_sns_info_ptr(struct fcp_rsp_iu *fcp_rsp_iu)
{
	char *fcp_sns_info_ptr;

	fcp_sns_info_ptr = (unsigned char *) &fcp_rsp_iu[1];
	if (fcp_rsp_iu->validity.bits.fcp_rsp_len_valid)
		fcp_sns_info_ptr += fcp_rsp_iu->fcp_rsp_len;

	return fcp_sns_info_ptr;
}

static void zfcp_scsi_slave_destroy(struct scsi_device *sdpnt)
{
	struct zfcp_unit *unit = (struct zfcp_unit *) sdpnt->hostdata;
	unit->device = NULL;
	zfcp_unit_put(unit);
}

static int zfcp_scsi_slave_configure(struct scsi_device *sdp)
{
	if (sdp->tagged_supported)
		scsi_adjust_queue_depth(sdp, MSG_SIMPLE_TAG, 32);
	else
		scsi_adjust_queue_depth(sdp, 0, 1);
	return 0;
}

static void zfcp_scsi_command_fail(struct scsi_cmnd *scpnt, int result)
{
	set_host_byte(scpnt, result);
	if ((scpnt->device != NULL) && (scpnt->device->host != NULL))
		zfcp_scsi_dbf_event_result("fail", 4,
			(struct zfcp_adapter*) scpnt->device->host->hostdata[0],
			scpnt, NULL);
	/* return directly */
	scpnt->scsi_done(scpnt);
}

static int zfcp_scsi_queuecommand(struct scsi_cmnd *scpnt,
				  void (*done) (struct scsi_cmnd *))
{
	struct zfcp_unit *unit;
	struct zfcp_adapter *adapter;
	int    status, scsi_result, ret;
	struct fc_rport *rport = starget_to_rport(scsi_target(scpnt->device));

	/* reset the status for this request */
	scpnt->result = 0;
	scpnt->host_scribble = NULL;
	scpnt->scsi_done = done;

	/*
	 * figure out adapter and target device
	 * (stored there by zfcp_scsi_slave_alloc)
	 */
	adapter = (struct zfcp_adapter *) scpnt->device->host->hostdata[0];
	unit = scpnt->device->hostdata;

	BUG_ON(!adapter || (adapter != unit->port->adapter));
	BUG_ON(!scpnt->scsi_done);

	if (unlikely(!unit)) {
		zfcp_scsi_command_fail(scpnt, DID_NO_CONNECT);
		return 0;
	}

	scsi_result = fc_remote_port_chkready(rport);
	if (unlikely(scsi_result)) {
		scpnt->result = scsi_result;
		zfcp_scsi_dbf_event_result("fail", 4, adapter, scpnt, NULL);
		scpnt->scsi_done(scpnt);
		return 0;
	}

	status = atomic_read(&unit->status);
	if (unlikely((status & ZFCP_STATUS_COMMON_ERP_FAILED) ||
		     !(status & ZFCP_STATUS_COMMON_RUNNING))) {
		zfcp_scsi_command_fail(scpnt, DID_ERROR);
		return 0;;
	}

	ret = zfcp_fsf_send_fcp_command_task(unit, scpnt);
	if (unlikely(ret == -EBUSY))
		return SCSI_MLQUEUE_DEVICE_BUSY;
	else if (unlikely(ret < 0))
		return SCSI_MLQUEUE_HOST_BUSY;

	return ret;
}

static struct zfcp_unit *zfcp_unit_lookup(struct zfcp_adapter *adapter,
					  int channel, unsigned int id,
					  unsigned int lun)
{
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	int scsi_lun;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (!port->rport || (id != port->rport->scsi_target_id))
			continue;
		list_for_each_entry(unit, &port->unit_list_head, list) {
			scsi_lun = scsilun_to_int(
				(struct scsi_lun *)&unit->fcp_lun);
			if (lun == scsi_lun)
				return unit;
		}
	}

	return NULL;
}

static int zfcp_scsi_slave_alloc(struct scsi_device *sdp)
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
	if (unit) {
		sdp->hostdata = unit;
		unit->device = sdp;
		zfcp_unit_get(unit);
		retval = 0;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
out:
	return retval;
}

static int zfcp_scsi_eh_abort_handler(struct scsi_cmnd *scpnt)
{
	struct Scsi_Host *scsi_host = scpnt->device->host;
	struct zfcp_adapter *adapter =
		(struct zfcp_adapter *) scsi_host->hostdata[0];
	struct zfcp_unit *unit = scpnt->device->hostdata;
	struct zfcp_fsf_req *old_req, *abrt_req;
	unsigned long flags;
	unsigned long old_req_id = (unsigned long) scpnt->host_scribble;
	int retval = SUCCESS;
	int retry = 3;

	/* avoid race condition between late normal completion and abort */
	write_lock_irqsave(&adapter->abort_lock, flags);

	spin_lock(&adapter->req_list_lock);
	old_req = zfcp_reqlist_find(adapter, old_req_id);
	spin_unlock(&adapter->req_list_lock);
	if (!old_req) {
		write_unlock_irqrestore(&adapter->abort_lock, flags);
		zfcp_scsi_dbf_event_abort("lte1", adapter, scpnt, NULL,
					  old_req_id);
		return FAILED; /* completion could be in progress */
	}
	old_req->data = NULL;

	/* don't access old fsf_req after releasing the abort_lock */
	write_unlock_irqrestore(&adapter->abort_lock, flags);

	while (retry--) {
		abrt_req = zfcp_fsf_abort_fcp_command(old_req_id, unit);
		if (abrt_req)
			break;

		zfcp_erp_wait(adapter);
		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_COMMON_RUNNING)) {
			zfcp_scsi_dbf_event_abort("nres", adapter, scpnt, NULL,
						  old_req_id);
			return SUCCESS;
		}
	}
	if (!abrt_req)
		return FAILED;

	wait_event(abrt_req->completion_wq,
		   abrt_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	if (abrt_req->status & ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED)
		zfcp_scsi_dbf_event_abort("okay", adapter, scpnt, abrt_req, 0);
	else if (abrt_req->status & ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED)
		zfcp_scsi_dbf_event_abort("lte2", adapter, scpnt, abrt_req, 0);
	else {
		zfcp_scsi_dbf_event_abort("fail", adapter, scpnt, abrt_req, 0);
		retval = FAILED;
	}
	zfcp_fsf_req_free(abrt_req);
	return retval;
}

static int zfcp_task_mgmt_function(struct scsi_cmnd *scpnt, u8 tm_flags)
{
	struct zfcp_unit *unit = scpnt->device->hostdata;
	struct zfcp_adapter *adapter = unit->port->adapter;
	struct zfcp_fsf_req *fsf_req;
	int retval = SUCCESS;
	int retry = 3;

	while (retry--) {
		fsf_req = zfcp_fsf_send_fcp_ctm(unit, tm_flags);
		if (fsf_req)
			break;

		zfcp_erp_wait(adapter);
		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_COMMON_RUNNING)) {
			zfcp_scsi_dbf_event_devreset("nres", tm_flags, unit,
						     scpnt);
			return SUCCESS;
		}
	}
	if (!fsf_req)
		return FAILED;

	wait_event(fsf_req->completion_wq,
		   fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_TMFUNCFAILED) {
		zfcp_scsi_dbf_event_devreset("fail", tm_flags, unit, scpnt);
		retval = FAILED;
	} else if (fsf_req->status & ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP) {
		zfcp_scsi_dbf_event_devreset("nsup", tm_flags, unit, scpnt);
		retval = FAILED;
	} else
		zfcp_scsi_dbf_event_devreset("okay", tm_flags, unit, scpnt);

	zfcp_fsf_req_free(fsf_req);
	return retval;
}

static int zfcp_scsi_eh_device_reset_handler(struct scsi_cmnd *scpnt)
{
	return zfcp_task_mgmt_function(scpnt, FCP_LOGICAL_UNIT_RESET);
}

static int zfcp_scsi_eh_target_reset_handler(struct scsi_cmnd *scpnt)
{
	return zfcp_task_mgmt_function(scpnt, FCP_TARGET_RESET);
}

static int zfcp_scsi_eh_host_reset_handler(struct scsi_cmnd *scpnt)
{
	struct zfcp_unit *unit = scpnt->device->hostdata;
	struct zfcp_adapter *adapter = unit->port->adapter;

	zfcp_erp_adapter_reopen(adapter, 0, "schrh_1", scpnt);
	zfcp_erp_wait(adapter);

	return SUCCESS;
}

int zfcp_adapter_scsi_register(struct zfcp_adapter *adapter)
{
	struct ccw_dev_id dev_id;

	if (adapter->scsi_host)
		return 0;

	ccw_device_get_id(adapter->ccw_device, &dev_id);
	/* register adapter as SCSI host with mid layer of SCSI stack */
	adapter->scsi_host = scsi_host_alloc(&zfcp_data.scsi_host_template,
					     sizeof (struct zfcp_adapter *));
	if (!adapter->scsi_host) {
		dev_err(&adapter->ccw_device->dev,
			"Registering the FCP device with the "
			"SCSI stack failed\n");
		return -EIO;
	}

	/* tell the SCSI stack some characteristics of this adapter */
	adapter->scsi_host->max_id = 1;
	adapter->scsi_host->max_lun = 1;
	adapter->scsi_host->max_channel = 0;
	adapter->scsi_host->unique_id = dev_id.devno;
	adapter->scsi_host->max_cmd_len = 255;
	adapter->scsi_host->transportt = zfcp_data.scsi_transport_template;

	adapter->scsi_host->hostdata[0] = (unsigned long) adapter;

	if (scsi_add_host(adapter->scsi_host, &adapter->ccw_device->dev)) {
		scsi_host_put(adapter->scsi_host);
		return -EIO;
	}

	return 0;
}

void zfcp_adapter_scsi_unregister(struct zfcp_adapter *adapter)
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

	return;
}

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

static void zfcp_adjust_fc_host_stats(struct fc_host_statistics *fc_stats,
				      struct fsf_qtcb_bottom_port *data,
				      struct fsf_qtcb_bottom_port *old)
{
	fc_stats->seconds_since_last_reset =
		data->seconds_since_last_reset - old->seconds_since_last_reset;
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
	fc_stats->loss_of_signal_count =
		data->loss_of_signal - old->loss_of_signal;
	fc_stats->prim_seq_protocol_err_count =
		data->psp_error_counts - old->psp_error_counts;
	fc_stats->invalid_tx_word_count =
		data->invalid_tx_words - old->invalid_tx_words;
	fc_stats->invalid_crc_count = data->invalid_crcs - old->invalid_crcs;
	fc_stats->fcp_input_requests =
		data->input_requests - old->input_requests;
	fc_stats->fcp_output_requests =
		data->output_requests - old->output_requests;
	fc_stats->fcp_control_requests =
		data->control_requests - old->control_requests;
	fc_stats->fcp_input_megabytes = data->input_mb - old->input_mb;
	fc_stats->fcp_output_megabytes = data->output_mb - old->output_mb;
}

static void zfcp_set_fc_host_stats(struct fc_host_statistics *fc_stats,
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

static struct fc_host_statistics *zfcp_get_fc_host_stats(struct Scsi_Host *host)
{
	struct zfcp_adapter *adapter;
	struct fc_host_statistics *fc_stats;
	struct fsf_qtcb_bottom_port *data;
	int ret;

	adapter = (struct zfcp_adapter *)host->hostdata[0];
	fc_stats = zfcp_init_fc_host_stats(adapter);
	if (!fc_stats)
		return NULL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	ret = zfcp_fsf_exchange_port_data_sync(adapter, data);
	if (ret) {
		kfree(data);
		return NULL;
	}

	if (adapter->stats_reset &&
	    ((jiffies/HZ - adapter->stats_reset) <
	     data->seconds_since_last_reset))
		zfcp_adjust_fc_host_stats(fc_stats, data,
					  adapter->stats_reset_data);
	else
		zfcp_set_fc_host_stats(fc_stats, data);

	kfree(data);
	return fc_stats;
}

static void zfcp_reset_fc_host_stats(struct Scsi_Host *shost)
{
	struct zfcp_adapter *adapter;
	struct fsf_qtcb_bottom_port *data;
	int ret;

	adapter = (struct zfcp_adapter *)shost->hostdata[0];
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	ret = zfcp_fsf_exchange_port_data_sync(adapter, data);
	if (ret)
		kfree(data);
	else {
		adapter->stats_reset = jiffies/HZ;
		kfree(adapter->stats_reset_data);
		adapter->stats_reset_data = data; /* finally freed in
						     adapter_dequeue */
	}
}

static void zfcp_get_host_port_state(struct Scsi_Host *shost)
{
	struct zfcp_adapter *adapter =
		(struct zfcp_adapter *)shost->hostdata[0];
	int status = atomic_read(&adapter->status);

	if ((status & ZFCP_STATUS_COMMON_RUNNING) &&
	    !(status & ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED))
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
	else if (status & ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED)
		fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
	else if (status & ZFCP_STATUS_COMMON_ERP_FAILED)
		fc_host_port_state(shost) = FC_PORTSTATE_ERROR;
	else
		fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
}

static void zfcp_set_rport_dev_loss_tmo(struct fc_rport *rport, u32 timeout)
{
	rport->dev_loss_tmo = timeout;
}

/**
 * zfcp_scsi_dev_loss_tmo_callbk - Free any reference to rport
 * @rport: The rport that is about to be deleted.
 */
static void zfcp_scsi_dev_loss_tmo_callbk(struct fc_rport *rport)
{
	struct zfcp_port *port;

	write_lock_irq(&zfcp_data.config_lock);
	port = rport->dd_data;
	if (port)
		port->rport = NULL;
	write_unlock_irq(&zfcp_data.config_lock);
}

/**
 * zfcp_scsi_terminate_rport_io - Terminate all I/O on a rport
 * @rport: The FC rport where to teminate I/O
 *
 * Abort all pending SCSI commands for a port by closing the
 * port. Using a reopen for avoids a conflict with a shutdown
 * overwriting a reopen.
 */
static void zfcp_scsi_terminate_rport_io(struct fc_rport *rport)
{
	struct zfcp_port *port;

	write_lock_irq(&zfcp_data.config_lock);
	port = rport->dd_data;
	if (port)
		zfcp_port_get(port);
	write_unlock_irq(&zfcp_data.config_lock);

	if (port) {
		zfcp_erp_port_reopen(port, 0, "sctrpi1", NULL);
		zfcp_port_put(port);
	}
}

static void zfcp_scsi_rport_register(struct zfcp_port *port)
{
	struct fc_rport_identifiers ids;
	struct fc_rport *rport;

	ids.node_name = port->wwnn;
	ids.port_name = port->wwpn;
	ids.port_id = port->d_id;
	ids.roles = FC_RPORT_ROLE_FCP_TARGET;

	rport = fc_remote_port_add(port->adapter->scsi_host, 0, &ids);
	if (!rport) {
		dev_err(&port->adapter->ccw_device->dev,
			"Registering port 0x%016Lx failed\n",
			(unsigned long long)port->wwpn);
		return;
	}

	rport->dd_data = port;
	rport->maxframe_size = port->maxframe_size;
	rport->supported_classes = port->supported_classes;
	port->rport = rport;
}

static void zfcp_scsi_rport_block(struct zfcp_port *port)
{
	struct fc_rport *rport = port->rport;

	if (rport)
		fc_remote_port_delete(rport);
}

void zfcp_scsi_schedule_rport_register(struct zfcp_port *port)
{
	zfcp_port_get(port);
	port->rport_task = RPORT_ADD;

	if (!queue_work(zfcp_data.work_queue, &port->rport_work))
		zfcp_port_put(port);
}

void zfcp_scsi_schedule_rport_block(struct zfcp_port *port)
{
	zfcp_port_get(port);
	port->rport_task = RPORT_DEL;

	if (!queue_work(zfcp_data.work_queue, &port->rport_work))
		zfcp_port_put(port);
}

void zfcp_scsi_schedule_rports_block(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;

	list_for_each_entry(port, &adapter->port_list_head, list)
		zfcp_scsi_schedule_rport_block(port);
}

void zfcp_scsi_rport_work(struct work_struct *work)
{
	struct zfcp_port *port = container_of(work, struct zfcp_port,
					      rport_work);

	while (port->rport_task) {
		if (port->rport_task == RPORT_ADD) {
			port->rport_task = RPORT_NONE;
			zfcp_scsi_rport_register(port);
		} else {
			port->rport_task = RPORT_NONE;
			zfcp_scsi_rport_block(port);
		}
	}

	zfcp_port_put(port);
}


void zfcp_scsi_scan(struct work_struct *work)
{
	struct zfcp_unit *unit = container_of(work, struct zfcp_unit,
					      scsi_work);
	struct fc_rport *rport;

	flush_work(&unit->port->rport_work);
	rport = unit->port->rport;

	if (rport && rport->port_state == FC_PORTSTATE_ONLINE)
		scsi_scan_target(&rport->dev, 0, rport->scsi_target_id,
				 scsilun_to_int((struct scsi_lun *)
						&unit->fcp_lun), 0);

	zfcp_unit_put(unit);
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
	.get_host_port_state = zfcp_get_host_port_state,
	.dev_loss_tmo_callbk = zfcp_scsi_dev_loss_tmo_callbk,
	.terminate_rport_io = zfcp_scsi_terminate_rport_io,
	.show_host_port_state = 1,
	/* no functions registered for following dynamic attributes but
	   directly set by LLDD */
	.show_host_port_type = 1,
	.show_host_speed = 1,
	.show_host_port_id = 1,
	.disable_target_scan = 1,
};

struct zfcp_data zfcp_data = {
	.scsi_host_template = {
		.name			 = "zfcp",
		.module			 = THIS_MODULE,
		.proc_name		 = "zfcp",
		.slave_alloc		 = zfcp_scsi_slave_alloc,
		.slave_configure	 = zfcp_scsi_slave_configure,
		.slave_destroy		 = zfcp_scsi_slave_destroy,
		.queuecommand		 = zfcp_scsi_queuecommand,
		.eh_abort_handler	 = zfcp_scsi_eh_abort_handler,
		.eh_device_reset_handler = zfcp_scsi_eh_device_reset_handler,
		.eh_target_reset_handler = zfcp_scsi_eh_target_reset_handler,
		.eh_host_reset_handler	 = zfcp_scsi_eh_host_reset_handler,
		.can_queue		 = 4096,
		.this_id		 = -1,
		.sg_tablesize		 = ZFCP_MAX_SBALES_PER_REQ,
		.cmd_per_lun		 = 1,
		.use_clustering		 = 1,
		.sdev_attrs		 = zfcp_sysfs_sdev_attrs,
		.max_sectors		 = (ZFCP_MAX_SBALES_PER_REQ * 8),
		.shost_attrs		 = zfcp_sysfs_shost_attrs,
	},
};
