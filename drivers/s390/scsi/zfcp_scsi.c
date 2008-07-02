/*
 * zfcp device driver
 *
 * Interface to Linux SCSI midlayer.
 *
 * Copyright IBM Corporation 2002, 2008
 */

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

void zfcp_set_fcp_dl(struct fcp_cmnd_iu *fcp_cmd, fcp_dl_t fcp_dl)
{
	fcp_dl_t *fcp_dl_ptr;

	/*
	 * fcp_dl_addr = start address of fcp_cmnd structure +
	 * size of fixed part + size of dynamically sized add_dcp_cdb field
	 * SEE FCP-2 documentation
	 */
	fcp_dl_ptr = (fcp_dl_t *) ((unsigned char *) &fcp_cmd[1] +
				   (fcp_cmd->add_fcp_cdb_length << 2));
	*fcp_dl_ptr = fcp_dl;
}

static void zfcp_scsi_slave_destroy(struct scsi_device *sdpnt)
{
	struct zfcp_unit *unit = (struct zfcp_unit *) sdpnt->hostdata;
	WARN_ON(!unit);
	if (unit) {
		atomic_clear_mask(ZFCP_STATUS_UNIT_REGISTERED, &unit->status);
		sdpnt->hostdata = NULL;
		unit->device = NULL;
		zfcp_erp_unit_failed(unit, 12, NULL);
		zfcp_unit_put(unit);
	}
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
	int    status;
	int    ret;

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

	status = atomic_read(&unit->status);
	if (unlikely((status & ZFCP_STATUS_COMMON_ERP_FAILED) ||
		     !(status & ZFCP_STATUS_COMMON_RUNNING))) {
		zfcp_scsi_command_fail(scpnt, DID_ERROR);
		return 0;;
	}

	ret = zfcp_fsf_send_fcp_command_task(adapter, unit, scpnt, 0,
					     ZFCP_REQ_AUTO_CLEANUP);
	if (unlikely(ret == -EBUSY))
		zfcp_scsi_command_fail(scpnt, DID_NO_CONNECT);
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

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (!port->rport || (id != port->rport->scsi_target_id))
			continue;
		list_for_each_entry(unit, &port->unit_list_head, list)
			if (lun == unit->scsi_lun)
				return unit;
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
	if (unit &&
	    (atomic_read(&unit->status) & ZFCP_STATUS_UNIT_REGISTERED)) {
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
 	struct Scsi_Host *scsi_host;
 	struct zfcp_adapter *adapter;
	struct zfcp_unit *unit;
	struct zfcp_fsf_req *fsf_req;
	unsigned long flags;
	unsigned long old_req_id = (unsigned long) scpnt->host_scribble;
	int retval = SUCCESS;

	scsi_host = scpnt->device->host;
	adapter = (struct zfcp_adapter *) scsi_host->hostdata[0];
	unit = scpnt->device->hostdata;

	/* avoid race condition between late normal completion and abort */
	write_lock_irqsave(&adapter->abort_lock, flags);

	/* Check whether corresponding fsf_req is still pending */
	spin_lock(&adapter->req_list_lock);
	fsf_req = zfcp_reqlist_find(adapter, old_req_id);
	spin_unlock(&adapter->req_list_lock);
	if (!fsf_req) {
		write_unlock_irqrestore(&adapter->abort_lock, flags);
		zfcp_scsi_dbf_event_abort("lte1", adapter, scpnt, NULL, 0);
		return retval;
	}
	fsf_req->data = 0;
	fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTING;

	/* don't access old fsf_req after releasing the abort_lock */
	write_unlock_irqrestore(&adapter->abort_lock, flags);

	fsf_req = zfcp_fsf_abort_fcp_command(old_req_id, adapter, unit, 0);
	if (!fsf_req) {
		zfcp_scsi_dbf_event_abort("nres", adapter, scpnt, NULL,
					  old_req_id);
		retval = FAILED;
		return retval;
	}

	__wait_event(fsf_req->completion_wq,
		     fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED) {
		zfcp_scsi_dbf_event_abort("okay", adapter, scpnt, fsf_req, 0);
	} else if (fsf_req->status & ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED) {
		zfcp_scsi_dbf_event_abort("lte2", adapter, scpnt, fsf_req, 0);
	} else {
		zfcp_scsi_dbf_event_abort("fail", adapter, scpnt, fsf_req, 0);
		retval = FAILED;
	}
	zfcp_fsf_req_free(fsf_req);

	return retval;
}

static int zfcp_task_mgmt_function(struct zfcp_unit *unit, u8 tm_flags,
					 struct scsi_cmnd *scpnt)
{
	struct zfcp_adapter *adapter = unit->port->adapter;
	struct zfcp_fsf_req *fsf_req;
	int retval = SUCCESS;

	/* issue task management function */
	fsf_req = zfcp_fsf_send_fcp_command_task_management
		(adapter, unit, tm_flags, 0);
	if (!fsf_req) {
		zfcp_scsi_dbf_event_devreset("nres", tm_flags, unit, scpnt);
		return FAILED;
	}

	__wait_event(fsf_req->completion_wq,
		     fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	/*
	 * check completion status of task management function
	 */
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
	struct zfcp_unit *unit = scpnt->device->hostdata;

	if (!unit) {
		WARN_ON(1);
		return SUCCESS;
	}
	return zfcp_task_mgmt_function(unit, FCP_LOGICAL_UNIT_RESET, scpnt);
}

static int zfcp_scsi_eh_target_reset_handler(struct scsi_cmnd *scpnt)
{
	struct zfcp_unit *unit = scpnt->device->hostdata;

	if (!unit) {
		WARN_ON(1);
		return SUCCESS;
	}
	return zfcp_task_mgmt_function(unit, FCP_TARGET_RESET, scpnt);
}

static int zfcp_scsi_eh_host_reset_handler(struct scsi_cmnd *scpnt)
{
	struct zfcp_unit *unit;
	struct zfcp_adapter *adapter;

	unit = scpnt->device->hostdata;
	adapter = unit->port->adapter;
	zfcp_erp_adapter_reopen(adapter, 0, 141, scpnt);
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
			"registration with SCSI stack failed.");
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
	atomic_set_mask(ZFCP_STATUS_ADAPTER_REGISTERED, &adapter->status);

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
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_REGISTERED, &adapter->status);

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
	.show_host_port_state = 1,
	/* no functions registered for following dynamic attributes but
	   directly set by LLDD */
	.show_host_port_type = 1,
	.show_host_speed = 1,
	.show_host_port_id = 1,
	.disable_target_scan = 1,
};

#define ZFCP_DEFINE_LATENCY_ATTR(_name) 				\
static ssize_t								\
zfcp_sysfs_unit_##_name##_latency_show(struct device *dev,		\
				       struct device_attribute *attr,	\
				       char *buf) {			\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	struct zfcp_unit *unit = sdev->hostdata;			\
	struct zfcp_latencies *lat = &unit->latencies;			\
	struct zfcp_adapter *adapter = unit->port->adapter;		\
	unsigned long flags;						\
	unsigned long long fsum, fmin, fmax, csum, cmin, cmax, cc;	\
									\
	spin_lock_irqsave(&lat->lock, flags);				\
	fsum = lat->_name.fabric.sum * adapter->timer_ticks;		\
	fmin = lat->_name.fabric.min * adapter->timer_ticks;		\
	fmax = lat->_name.fabric.max * adapter->timer_ticks;		\
	csum = lat->_name.channel.sum * adapter->timer_ticks;		\
	cmin = lat->_name.channel.min * adapter->timer_ticks;		\
	cmax = lat->_name.channel.max * adapter->timer_ticks;		\
	cc  = lat->_name.counter;					\
	spin_unlock_irqrestore(&lat->lock, flags);			\
									\
	do_div(fsum, 1000);						\
	do_div(fmin, 1000);						\
	do_div(fmax, 1000);						\
	do_div(csum, 1000);						\
	do_div(cmin, 1000);						\
	do_div(cmax, 1000);						\
									\
	return sprintf(buf, "%llu %llu %llu %llu %llu %llu %llu\n",	\
		       fmin, fmax, fsum, cmin, cmax, csum, cc); 	\
}									\
static ssize_t								\
zfcp_sysfs_unit_##_name##_latency_store(struct device *dev,		\
					struct device_attribute *attr,	\
					const char *buf, size_t count)	\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	struct zfcp_unit *unit = sdev->hostdata;			\
	struct zfcp_latencies *lat = &unit->latencies;			\
	unsigned long flags;						\
									\
	spin_lock_irqsave(&lat->lock, flags);				\
	lat->_name.fabric.sum = 0;					\
	lat->_name.fabric.min = 0xFFFFFFFF;				\
	lat->_name.fabric.max = 0;					\
	lat->_name.channel.sum = 0;					\
	lat->_name.channel.min = 0xFFFFFFFF;				\
	lat->_name.channel.max = 0;					\
	lat->_name.counter = 0;						\
	spin_unlock_irqrestore(&lat->lock, flags);			\
									\
	return (ssize_t) count;						\
}									\
static DEVICE_ATTR(_name##_latency, S_IWUSR | S_IRUGO,			\
		   zfcp_sysfs_unit_##_name##_latency_show,		\
		   zfcp_sysfs_unit_##_name##_latency_store);

ZFCP_DEFINE_LATENCY_ATTR(read);
ZFCP_DEFINE_LATENCY_ATTR(write);
ZFCP_DEFINE_LATENCY_ATTR(cmd);

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

ZFCP_DEFINE_SCSI_ATTR(hba_id, "%s\n",
	unit->port->adapter->ccw_device->dev.bus_id);
ZFCP_DEFINE_SCSI_ATTR(wwpn, "0x%016llx\n", unit->port->wwpn);
ZFCP_DEFINE_SCSI_ATTR(fcp_lun, "0x%016llx\n", unit->fcp_lun);

static struct device_attribute *zfcp_sysfs_sdev_attrs[] = {
	&dev_attr_fcp_lun,
	&dev_attr_wwpn,
	&dev_attr_hba_id,
	&dev_attr_read_latency,
	&dev_attr_write_latency,
	&dev_attr_cmd_latency,
	NULL
};

static ssize_t zfcp_sysfs_adapter_util_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct Scsi_Host *scsi_host = dev_to_shost(dev);
	struct fsf_qtcb_bottom_port *qtcb_port;
	struct zfcp_adapter *adapter;
	int retval;

	adapter = (struct zfcp_adapter *) scsi_host->hostdata[0];
	if (!(adapter->adapter_features & FSF_FEATURE_MEASUREMENT_DATA))
		return -EOPNOTSUPP;

	qtcb_port = kzalloc(sizeof(struct fsf_qtcb_bottom_port), GFP_KERNEL);
	if (!qtcb_port)
		return -ENOMEM;

	retval = zfcp_fsf_exchange_port_data_sync(adapter, qtcb_port);
	if (!retval)
		retval = sprintf(buf, "%u %u %u\n", qtcb_port->cp_util,
				 qtcb_port->cb_util, qtcb_port->a_util);
	kfree(qtcb_port);
	return retval;
}

static int zfcp_sysfs_adapter_ex_config(struct device *dev,
					struct fsf_statistics_info *stat_inf)
{
	struct Scsi_Host *scsi_host = dev_to_shost(dev);
	struct fsf_qtcb_bottom_config *qtcb_config;
	struct zfcp_adapter *adapter;
	int retval;

	adapter = (struct zfcp_adapter *) scsi_host->hostdata[0];
	if (!(adapter->adapter_features & FSF_FEATURE_MEASUREMENT_DATA))
		return -EOPNOTSUPP;

	qtcb_config = kzalloc(sizeof(struct fsf_qtcb_bottom_config),
			      GFP_KERNEL);
	if (!qtcb_config)
		return -ENOMEM;

	retval = zfcp_fsf_exchange_config_data_sync(adapter, qtcb_config);
	if (!retval)
		*stat_inf = qtcb_config->stat_info;

	kfree(qtcb_config);
	return retval;
}

static ssize_t zfcp_sysfs_adapter_request_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct fsf_statistics_info stat_info;
	int retval;

	retval = zfcp_sysfs_adapter_ex_config(dev, &stat_info);
	if (retval)
		return retval;

	return sprintf(buf, "%llu %llu %llu\n",
		       (unsigned long long) stat_info.input_req,
		       (unsigned long long) stat_info.output_req,
		       (unsigned long long) stat_info.control_req);
}

static ssize_t zfcp_sysfs_adapter_mb_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct fsf_statistics_info stat_info;
	int retval;

	retval = zfcp_sysfs_adapter_ex_config(dev, &stat_info);
	if (retval)
		return retval;

	return sprintf(buf, "%llu %llu\n",
		       (unsigned long long) stat_info.input_mb,
		       (unsigned long long) stat_info.output_mb);
}

static ssize_t zfcp_sysfs_adapter_sec_active_show(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	struct fsf_statistics_info stat_info;
	int retval;

	retval = zfcp_sysfs_adapter_ex_config(dev, &stat_info);
	if (retval)
		return retval;

	return sprintf(buf, "%llu\n",
		       (unsigned long long) stat_info.seconds_act);
}

static DEVICE_ATTR(utilization, S_IRUGO, zfcp_sysfs_adapter_util_show, NULL);
static DEVICE_ATTR(requests, S_IRUGO, zfcp_sysfs_adapter_request_show, NULL);
static DEVICE_ATTR(megabytes, S_IRUGO, zfcp_sysfs_adapter_mb_show, NULL);
static DEVICE_ATTR(seconds_active, S_IRUGO,
		   zfcp_sysfs_adapter_sec_active_show, NULL);

static struct device_attribute *zfcp_a_stats_attrs[] = {
	&dev_attr_utilization,
	&dev_attr_requests,
	&dev_attr_megabytes,
	&dev_attr_seconds_active,
	NULL
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
		.shost_attrs		 = zfcp_a_stats_attrs,
	},
};

