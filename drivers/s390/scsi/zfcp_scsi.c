/*
 * zfcp device driver
 *
 * Interface to Linux SCSI midlayer.
 *
 * Copyright IBM Corporation 2002, 2010
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/types.h>
#include <linux/slab.h>
#include <scsi/fc/fc_fcp.h>
#include <scsi/scsi_eh.h>
#include <asm/atomic.h>
#include "zfcp_ext.h"
#include "zfcp_dbf.h"
#include "zfcp_fc.h"
#include "zfcp_reqlist.h"

static unsigned int default_depth = 32;
module_param_named(queue_depth, default_depth, uint, 0600);
MODULE_PARM_DESC(queue_depth, "Default queue depth for new SCSI devices");

static bool enable_dif;

#ifdef CONFIG_ZFCP_DIF
module_param_named(dif, enable_dif, bool, 0600);
MODULE_PARM_DESC(dif, "Enable DIF/DIX data integrity support");
#endif

static bool allow_lun_scan = 1;
module_param(allow_lun_scan, bool, 0600);
MODULE_PARM_DESC(allow_lun_scan, "For NPIV, scan and attach all storage LUNs");

static int zfcp_scsi_change_queue_depth(struct scsi_device *sdev, int depth,
					int reason)
{
	switch (reason) {
	case SCSI_QDEPTH_DEFAULT:
		scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), depth);
		break;
	case SCSI_QDEPTH_QFULL:
		scsi_track_queue_full(sdev, depth);
		break;
	case SCSI_QDEPTH_RAMP_UP:
		scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), depth);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return sdev->queue_depth;
}

static void zfcp_scsi_slave_destroy(struct scsi_device *sdev)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	zfcp_erp_lun_shutdown_wait(sdev, "scssd_1");
	put_device(&zfcp_sdev->port->dev);
}

static int zfcp_scsi_slave_configure(struct scsi_device *sdp)
{
	if (sdp->tagged_supported)
		scsi_adjust_queue_depth(sdp, MSG_SIMPLE_TAG, default_depth);
	else
		scsi_adjust_queue_depth(sdp, 0, 1);
	return 0;
}

static void zfcp_scsi_command_fail(struct scsi_cmnd *scpnt, int result)
{
	set_host_byte(scpnt, result);
	zfcp_dbf_scsi_fail_send(scpnt);
	scpnt->scsi_done(scpnt);
}

static
int zfcp_scsi_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *scpnt)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(scpnt->device);
	struct fc_rport *rport = starget_to_rport(scsi_target(scpnt->device));
	int    status, scsi_result, ret;

	/* reset the status for this request */
	scpnt->result = 0;
	scpnt->host_scribble = NULL;

	scsi_result = fc_remote_port_chkready(rport);
	if (unlikely(scsi_result)) {
		scpnt->result = scsi_result;
		zfcp_dbf_scsi_fail_send(scpnt);
		scpnt->scsi_done(scpnt);
		return 0;
	}

	status = atomic_read(&zfcp_sdev->status);
	if (unlikely(status & ZFCP_STATUS_COMMON_ERP_FAILED) &&
		     !(atomic_read(&zfcp_sdev->port->status) &
		       ZFCP_STATUS_COMMON_ERP_FAILED)) {
		/* only LUN access denied, but port is good
		 * not covered by FC transport, have to fail here */
		zfcp_scsi_command_fail(scpnt, DID_ERROR);
		return 0;
	}

	if (unlikely(!(status & ZFCP_STATUS_COMMON_UNBLOCKED))) {
		/* This could be either
		 * open LUN pending: this is temporary, will result in
		 *	open LUN or ERP_FAILED, so retry command
		 * call to rport_delete pending: mimic retry from
		 * 	fc_remote_port_chkready until rport is BLOCKED
		 */
		zfcp_scsi_command_fail(scpnt, DID_IMM_RETRY);
		return 0;
	}

	ret = zfcp_fsf_fcp_cmnd(scpnt);
	if (unlikely(ret == -EBUSY))
		return SCSI_MLQUEUE_DEVICE_BUSY;
	else if (unlikely(ret < 0))
		return SCSI_MLQUEUE_HOST_BUSY;

	return ret;
}

static int zfcp_scsi_slave_alloc(struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	struct zfcp_adapter *adapter =
		(struct zfcp_adapter *) sdev->host->hostdata[0];
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	int npiv = adapter->connection_features & FSF_FEATURE_NPIV_MODE;

	port = zfcp_get_port_by_wwpn(adapter, rport->port_name);
	if (!port)
		return -ENXIO;

	unit = zfcp_unit_find(port, zfcp_scsi_dev_lun(sdev));
	if (unit)
		put_device(&unit->dev);

	if (!unit && !(allow_lun_scan && npiv)) {
		put_device(&port->dev);
		return -ENXIO;
	}

	zfcp_sdev->port = port;
	zfcp_sdev->latencies.write.channel.min = 0xFFFFFFFF;
	zfcp_sdev->latencies.write.fabric.min = 0xFFFFFFFF;
	zfcp_sdev->latencies.read.channel.min = 0xFFFFFFFF;
	zfcp_sdev->latencies.read.fabric.min = 0xFFFFFFFF;
	zfcp_sdev->latencies.cmd.channel.min = 0xFFFFFFFF;
	zfcp_sdev->latencies.cmd.fabric.min = 0xFFFFFFFF;
	spin_lock_init(&zfcp_sdev->latencies.lock);

	zfcp_erp_set_lun_status(sdev, ZFCP_STATUS_COMMON_RUNNING);
	zfcp_erp_lun_reopen(sdev, 0, "scsla_1");
	zfcp_erp_wait(port->adapter);

	return 0;
}

static int zfcp_scsi_eh_abort_handler(struct scsi_cmnd *scpnt)
{
	struct Scsi_Host *scsi_host = scpnt->device->host;
	struct zfcp_adapter *adapter =
		(struct zfcp_adapter *) scsi_host->hostdata[0];
	struct zfcp_fsf_req *old_req, *abrt_req;
	unsigned long flags;
	unsigned long old_reqid = (unsigned long) scpnt->host_scribble;
	int retval = SUCCESS, ret;
	int retry = 3;
	char *dbf_tag;

	/* avoid race condition between late normal completion and abort */
	write_lock_irqsave(&adapter->abort_lock, flags);

	old_req = zfcp_reqlist_find(adapter->req_list, old_reqid);
	if (!old_req) {
		write_unlock_irqrestore(&adapter->abort_lock, flags);
		zfcp_dbf_scsi_abort("abrt_or", scpnt, NULL);
		return FAILED; /* completion could be in progress */
	}
	old_req->data = NULL;

	/* don't access old fsf_req after releasing the abort_lock */
	write_unlock_irqrestore(&adapter->abort_lock, flags);

	while (retry--) {
		abrt_req = zfcp_fsf_abort_fcp_cmnd(scpnt);
		if (abrt_req)
			break;

		zfcp_erp_wait(adapter);
		ret = fc_block_scsi_eh(scpnt);
		if (ret) {
			zfcp_dbf_scsi_abort("abrt_bl", scpnt, NULL);
			return ret;
		}
		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_COMMON_RUNNING)) {
			zfcp_dbf_scsi_abort("abrt_ru", scpnt, NULL);
			return SUCCESS;
		}
	}
	if (!abrt_req) {
		zfcp_dbf_scsi_abort("abrt_ar", scpnt, NULL);
		return FAILED;
	}

	wait_for_completion(&abrt_req->completion);

	if (abrt_req->status & ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED)
		dbf_tag = "abrt_ok";
	else if (abrt_req->status & ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED)
		dbf_tag = "abrt_nn";
	else {
		dbf_tag = "abrt_fa";
		retval = FAILED;
	}
	zfcp_dbf_scsi_abort(dbf_tag, scpnt, abrt_req);
	zfcp_fsf_req_free(abrt_req);
	return retval;
}

static int zfcp_task_mgmt_function(struct scsi_cmnd *scpnt, u8 tm_flags)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(scpnt->device);
	struct zfcp_adapter *adapter = zfcp_sdev->port->adapter;
	struct zfcp_fsf_req *fsf_req = NULL;
	int retval = SUCCESS, ret;
	int retry = 3;

	while (retry--) {
		fsf_req = zfcp_fsf_fcp_task_mgmt(scpnt, tm_flags);
		if (fsf_req)
			break;

		zfcp_erp_wait(adapter);
		ret = fc_block_scsi_eh(scpnt);
		if (ret)
			return ret;

		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_COMMON_RUNNING)) {
			zfcp_dbf_scsi_devreset("nres", scpnt, tm_flags);
			return SUCCESS;
		}
	}
	if (!fsf_req)
		return FAILED;

	wait_for_completion(&fsf_req->completion);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_TMFUNCFAILED) {
		zfcp_dbf_scsi_devreset("fail", scpnt, tm_flags);
		retval = FAILED;
	} else
		zfcp_dbf_scsi_devreset("okay", scpnt, tm_flags);

	zfcp_fsf_req_free(fsf_req);
	return retval;
}

static int zfcp_scsi_eh_device_reset_handler(struct scsi_cmnd *scpnt)
{
	return zfcp_task_mgmt_function(scpnt, FCP_TMF_LUN_RESET);
}

static int zfcp_scsi_eh_target_reset_handler(struct scsi_cmnd *scpnt)
{
	return zfcp_task_mgmt_function(scpnt, FCP_TMF_TGT_RESET);
}

static int zfcp_scsi_eh_host_reset_handler(struct scsi_cmnd *scpnt)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(scpnt->device);
	struct zfcp_adapter *adapter = zfcp_sdev->port->adapter;
	int ret;

	zfcp_erp_adapter_reopen(adapter, 0, "schrh_1");
	zfcp_erp_wait(adapter);
	ret = fc_block_scsi_eh(scpnt);
	if (ret)
		return ret;

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
	adapter->scsi_host->max_id = 511;
	adapter->scsi_host->max_lun = 0xFFFFFFFF;
	adapter->scsi_host->max_channel = 0;
	adapter->scsi_host->unique_id = dev_id.devno;
	adapter->scsi_host->max_cmd_len = 16; /* in struct fcp_cmnd */
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

	read_lock_irq(&adapter->port_list_lock);
	list_for_each_entry(port, &adapter->port_list, list)
		port->rport = NULL;
	read_unlock_irq(&adapter->port_list_lock);

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
		adapter->fc_stats = fc_stats; /* freed in adapter_release */
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

	ret = zfcp_fsf_exchange_port_data_sync(adapter->qdio, data);
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

	ret = zfcp_fsf_exchange_port_data_sync(adapter->qdio, data);
	if (ret)
		kfree(data);
	else {
		adapter->stats_reset = jiffies/HZ;
		kfree(adapter->stats_reset_data);
		adapter->stats_reset_data = data; /* finally freed in
						     adapter_release */
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
 * zfcp_scsi_terminate_rport_io - Terminate all I/O on a rport
 * @rport: The FC rport where to teminate I/O
 *
 * Abort all pending SCSI commands for a port by closing the
 * port. Using a reopen avoids a conflict with a shutdown
 * overwriting a reopen. The "forced" ensures that a disappeared port
 * is not opened again as valid due to the cached plogi data in
 * non-NPIV mode.
 */
static void zfcp_scsi_terminate_rport_io(struct fc_rport *rport)
{
	struct zfcp_port *port;
	struct Scsi_Host *shost = rport_to_shost(rport);
	struct zfcp_adapter *adapter =
		(struct zfcp_adapter *)shost->hostdata[0];

	port = zfcp_get_port_by_wwpn(adapter, rport->port_name);

	if (port) {
		zfcp_erp_port_forced_reopen(port, 0, "sctrpi1");
		put_device(&port->dev);
	}
}

static void zfcp_scsi_rport_register(struct zfcp_port *port)
{
	struct fc_rport_identifiers ids;
	struct fc_rport *rport;

	if (port->rport)
		return;

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

	rport->maxframe_size = port->maxframe_size;
	rport->supported_classes = port->supported_classes;
	port->rport = rport;
	port->starget_id = rport->scsi_target_id;

	zfcp_unit_queue_scsi_scan(port);
}

static void zfcp_scsi_rport_block(struct zfcp_port *port)
{
	struct fc_rport *rport = port->rport;

	if (rport) {
		fc_remote_port_delete(rport);
		port->rport = NULL;
	}
}

void zfcp_scsi_schedule_rport_register(struct zfcp_port *port)
{
	get_device(&port->dev);
	port->rport_task = RPORT_ADD;

	if (!queue_work(port->adapter->work_queue, &port->rport_work))
		put_device(&port->dev);
}

void zfcp_scsi_schedule_rport_block(struct zfcp_port *port)
{
	get_device(&port->dev);
	port->rport_task = RPORT_DEL;

	if (port->rport && queue_work(port->adapter->work_queue,
				      &port->rport_work))
		return;

	put_device(&port->dev);
}

void zfcp_scsi_schedule_rports_block(struct zfcp_adapter *adapter)
{
	unsigned long flags;
	struct zfcp_port *port;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list)
		zfcp_scsi_schedule_rport_block(port);
	read_unlock_irqrestore(&adapter->port_list_lock, flags);
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

	put_device(&port->dev);
}

/**
 * zfcp_scsi_set_prot - Configure DIF/DIX support in scsi_host
 * @adapter: The adapter where to configure DIF/DIX for the SCSI host
 */
void zfcp_scsi_set_prot(struct zfcp_adapter *adapter)
{
	unsigned int mask = 0;
	unsigned int data_div;
	struct Scsi_Host *shost = adapter->scsi_host;

	data_div = atomic_read(&adapter->status) &
		   ZFCP_STATUS_ADAPTER_DATA_DIV_ENABLED;

	if (enable_dif &&
	    adapter->adapter_features & FSF_FEATURE_DIF_PROT_TYPE1)
		mask |= SHOST_DIF_TYPE1_PROTECTION;

	if (enable_dif && data_div &&
	    adapter->adapter_features & FSF_FEATURE_DIX_PROT_TCPIP) {
		mask |= SHOST_DIX_TYPE1_PROTECTION;
		scsi_host_set_guard(shost, SHOST_DIX_GUARD_IP);
		shost->sg_prot_tablesize = ZFCP_QDIO_MAX_SBALES_PER_REQ / 2;
		shost->sg_tablesize = ZFCP_QDIO_MAX_SBALES_PER_REQ / 2;
		shost->max_sectors = ZFCP_QDIO_MAX_SBALES_PER_REQ * 8 / 2;
	}

	scsi_host_set_prot(shost, mask);
}

/**
 * zfcp_scsi_dif_sense_error - Report DIF/DIX error as driver sense error
 * @scmd: The SCSI command to report the error for
 * @ascq: The ASCQ to put in the sense buffer
 *
 * See the error handling in sd_done for the sense codes used here.
 * Set DID_SOFT_ERROR to retry the request, if possible.
 */
void zfcp_scsi_dif_sense_error(struct scsi_cmnd *scmd, int ascq)
{
	scsi_build_sense_buffer(1, scmd->sense_buffer,
				ILLEGAL_REQUEST, 0x10, ascq);
	set_driver_byte(scmd, DRIVER_SENSE);
	scmd->result |= SAM_STAT_CHECK_CONDITION;
	set_host_byte(scmd, DID_SOFT_ERROR);
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
	.show_host_supported_fc4s = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,
	.show_host_serial_number = 1,
	.get_fc_host_stats = zfcp_get_fc_host_stats,
	.reset_fc_host_stats = zfcp_reset_fc_host_stats,
	.set_rport_dev_loss_tmo = zfcp_set_rport_dev_loss_tmo,
	.get_host_port_state = zfcp_get_host_port_state,
	.terminate_rport_io = zfcp_scsi_terminate_rport_io,
	.show_host_port_state = 1,
	.show_host_active_fc4s = 1,
	.bsg_request = zfcp_fc_exec_bsg_job,
	.bsg_timeout = zfcp_fc_timeout_bsg_job,
	/* no functions registered for following dynamic attributes but
	   directly set by LLDD */
	.show_host_port_type = 1,
	.show_host_speed = 1,
	.show_host_port_id = 1,
	.dd_bsg_size = sizeof(struct zfcp_fsf_ct_els),
};

struct zfcp_data zfcp_data = {
	.scsi_host_template = {
		.name			 = "zfcp",
		.module			 = THIS_MODULE,
		.proc_name		 = "zfcp",
		.change_queue_depth	 = zfcp_scsi_change_queue_depth,
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
		.sg_tablesize		 = ZFCP_QDIO_MAX_SBALES_PER_REQ,
		.cmd_per_lun		 = 1,
		.use_clustering		 = 1,
		.sdev_attrs		 = zfcp_sysfs_sdev_attrs,
		.max_sectors		 = (ZFCP_QDIO_MAX_SBALES_PER_REQ * 8),
		.dma_boundary		 = ZFCP_QDIO_SBALE_LEN - 1,
		.shost_attrs		 = zfcp_sysfs_shost_attrs,
	},
};
