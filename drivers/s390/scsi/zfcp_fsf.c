/*
 * zfcp device driver
 *
 * Implementation of FSF commands.
 *
 * Copyright IBM Corporation 2002, 2009
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/blktrace_api.h>
#include "zfcp_ext.h"

#define ZFCP_REQ_AUTO_CLEANUP	0x00000002
#define ZFCP_REQ_NO_QTCB	0x00000008

static void zfcp_fsf_request_timeout_handler(unsigned long data)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) data;
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED,
				"fsrth_1", NULL);
}

static void zfcp_fsf_start_timer(struct zfcp_fsf_req *fsf_req,
				 unsigned long timeout)
{
	fsf_req->timer.function = zfcp_fsf_request_timeout_handler;
	fsf_req->timer.data = (unsigned long) fsf_req->adapter;
	fsf_req->timer.expires = jiffies + timeout;
	add_timer(&fsf_req->timer);
}

static void zfcp_fsf_start_erp_timer(struct zfcp_fsf_req *fsf_req)
{
	BUG_ON(!fsf_req->erp_action);
	fsf_req->timer.function = zfcp_erp_timeout_handler;
	fsf_req->timer.data = (unsigned long) fsf_req->erp_action;
	fsf_req->timer.expires = jiffies + 30 * HZ;
	add_timer(&fsf_req->timer);
}

/* association between FSF command and FSF QTCB type */
static u32 fsf_qtcb_type[] = {
	[FSF_QTCB_FCP_CMND] =             FSF_IO_COMMAND,
	[FSF_QTCB_ABORT_FCP_CMND] =       FSF_SUPPORT_COMMAND,
	[FSF_QTCB_OPEN_PORT_WITH_DID] =   FSF_SUPPORT_COMMAND,
	[FSF_QTCB_OPEN_LUN] =             FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_LUN] =            FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_PORT] =           FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_PHYSICAL_PORT] =  FSF_SUPPORT_COMMAND,
	[FSF_QTCB_SEND_ELS] =             FSF_SUPPORT_COMMAND,
	[FSF_QTCB_SEND_GENERIC] =         FSF_SUPPORT_COMMAND,
	[FSF_QTCB_EXCHANGE_CONFIG_DATA] = FSF_CONFIG_COMMAND,
	[FSF_QTCB_EXCHANGE_PORT_DATA] =   FSF_PORT_COMMAND,
	[FSF_QTCB_DOWNLOAD_CONTROL_FILE] = FSF_SUPPORT_COMMAND,
	[FSF_QTCB_UPLOAD_CONTROL_FILE] =  FSF_SUPPORT_COMMAND
};

static void zfcp_act_eval_err(struct zfcp_adapter *adapter, u32 table)
{
	u16 subtable = table >> 16;
	u16 rule = table & 0xffff;
	const char *act_type[] = { "unknown", "OS", "WWPN", "DID", "LUN" };

	if (subtable && subtable < ARRAY_SIZE(act_type))
		dev_warn(&adapter->ccw_device->dev,
			 "Access denied according to ACT rule type %s, "
			 "rule %d\n", act_type[subtable], rule);
}

static void zfcp_fsf_access_denied_port(struct zfcp_fsf_req *req,
					struct zfcp_port *port)
{
	struct fsf_qtcb_header *header = &req->qtcb->header;
	dev_warn(&req->adapter->ccw_device->dev,
		 "Access denied to port 0x%016Lx\n",
		 (unsigned long long)port->wwpn);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[0]);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[1]);
	zfcp_erp_port_access_denied(port, "fspad_1", req);
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

static void zfcp_fsf_access_denied_unit(struct zfcp_fsf_req *req,
					struct zfcp_unit *unit)
{
	struct fsf_qtcb_header *header = &req->qtcb->header;
	dev_warn(&req->adapter->ccw_device->dev,
		 "Access denied to unit 0x%016Lx on port 0x%016Lx\n",
		 (unsigned long long)unit->fcp_lun,
		 (unsigned long long)unit->port->wwpn);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[0]);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[1]);
	zfcp_erp_unit_access_denied(unit, "fsuad_1", req);
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

static void zfcp_fsf_class_not_supp(struct zfcp_fsf_req *req)
{
	dev_err(&req->adapter->ccw_device->dev, "FCP device not "
		"operational because of an unsupported FC class\n");
	zfcp_erp_adapter_shutdown(req->adapter, 0, "fscns_1", req);
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

/**
 * zfcp_fsf_req_free - free memory used by fsf request
 * @fsf_req: pointer to struct zfcp_fsf_req
 */
void zfcp_fsf_req_free(struct zfcp_fsf_req *req)
{
	if (likely(req->pool)) {
		mempool_free(req, req->pool);
		return;
	}

	if (req->qtcb) {
		kmem_cache_free(zfcp_data.fsf_req_qtcb_cache, req);
		return;
	}
}

/**
 * zfcp_fsf_req_dismiss_all - dismiss all fsf requests
 * @adapter: pointer to struct zfcp_adapter
 *
 * Never ever call this without shutting down the adapter first.
 * Otherwise the adapter would continue using and corrupting s390 storage.
 * Included BUG_ON() call to ensure this is done.
 * ERP is supposed to be the only user of this function.
 */
void zfcp_fsf_req_dismiss_all(struct zfcp_adapter *adapter)
{
	struct zfcp_fsf_req *req, *tmp;
	unsigned long flags;
	LIST_HEAD(remove_queue);
	unsigned int i;

	BUG_ON(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP);
	spin_lock_irqsave(&adapter->req_list_lock, flags);
	for (i = 0; i < REQUEST_LIST_SIZE; i++)
		list_splice_init(&adapter->req_list[i], &remove_queue);
	spin_unlock_irqrestore(&adapter->req_list_lock, flags);

	list_for_each_entry_safe(req, tmp, &remove_queue, list) {
		list_del(&req->list);
		req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
		zfcp_fsf_req_complete(req);
	}
}

static void zfcp_fsf_status_read_port_closed(struct zfcp_fsf_req *req)
{
	struct fsf_status_read_buffer *sr_buf = req->data;
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_port *port;
	int d_id = sr_buf->d_id & ZFCP_DID_MASK;
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list)
		if (port->d_id == d_id) {
			read_unlock_irqrestore(&zfcp_data.config_lock, flags);
			zfcp_erp_port_reopen(port, 0, "fssrpc1", req);
			return;
		}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
}

static void zfcp_fsf_link_down_info_eval(struct zfcp_fsf_req *req, char *id,
					 struct fsf_link_down_info *link_down)
{
	struct zfcp_adapter *adapter = req->adapter;
	unsigned long flags;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED)
		return;

	atomic_set_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED, &adapter->status);

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	zfcp_scsi_schedule_rports_block(adapter);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (!link_down)
		goto out;

	switch (link_down->error_code) {
	case FSF_PSQ_LINK_NO_LIGHT:
		dev_warn(&req->adapter->ccw_device->dev,
			 "There is no light signal from the local "
			 "fibre channel cable\n");
		break;
	case FSF_PSQ_LINK_WRAP_PLUG:
		dev_warn(&req->adapter->ccw_device->dev,
			 "There is a wrap plug instead of a fibre "
			 "channel cable\n");
		break;
	case FSF_PSQ_LINK_NO_FCP:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The adjacent fibre channel node does not "
			 "support FCP\n");
		break;
	case FSF_PSQ_LINK_FIRMWARE_UPDATE:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The FCP device is suspended because of a "
			 "firmware update\n");
		break;
	case FSF_PSQ_LINK_INVALID_WWPN:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The FCP device detected a WWPN that is "
			 "duplicate or not valid\n");
		break;
	case FSF_PSQ_LINK_NO_NPIV_SUPPORT:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The fibre channel fabric does not support NPIV\n");
		break;
	case FSF_PSQ_LINK_NO_FCP_RESOURCES:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The FCP adapter cannot support more NPIV ports\n");
		break;
	case FSF_PSQ_LINK_NO_FABRIC_RESOURCES:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The adjacent switch cannot support "
			 "more NPIV ports\n");
		break;
	case FSF_PSQ_LINK_FABRIC_LOGIN_UNABLE:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The FCP adapter could not log in to the "
			 "fibre channel fabric\n");
		break;
	case FSF_PSQ_LINK_WWPN_ASSIGNMENT_CORRUPTED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The WWPN assignment file on the FCP adapter "
			 "has been damaged\n");
		break;
	case FSF_PSQ_LINK_MODE_TABLE_CURRUPTED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The mode table on the FCP adapter "
			 "has been damaged\n");
		break;
	case FSF_PSQ_LINK_NO_WWPN_ASSIGNMENT:
		dev_warn(&req->adapter->ccw_device->dev,
			 "All NPIV ports on the FCP adapter have "
			 "been assigned\n");
		break;
	default:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The link between the FCP adapter and "
			 "the FC fabric is down\n");
	}
out:
	zfcp_erp_adapter_failed(adapter, id, req);
}

static void zfcp_fsf_status_read_link_down(struct zfcp_fsf_req *req)
{
	struct fsf_status_read_buffer *sr_buf = req->data;
	struct fsf_link_down_info *ldi =
		(struct fsf_link_down_info *) &sr_buf->payload;

	switch (sr_buf->status_subtype) {
	case FSF_STATUS_READ_SUB_NO_PHYSICAL_LINK:
		zfcp_fsf_link_down_info_eval(req, "fssrld1", ldi);
		break;
	case FSF_STATUS_READ_SUB_FDISC_FAILED:
		zfcp_fsf_link_down_info_eval(req, "fssrld2", ldi);
		break;
	case FSF_STATUS_READ_SUB_FIRMWARE_UPDATE:
		zfcp_fsf_link_down_info_eval(req, "fssrld3", NULL);
	};
}

static void zfcp_fsf_status_read_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_status_read_buffer *sr_buf = req->data;

	if (req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		zfcp_hba_dbf_event_fsf_unsol("dism", adapter, sr_buf);
		mempool_free(sr_buf, adapter->pool.data_status_read);
		zfcp_fsf_req_free(req);
		return;
	}

	zfcp_hba_dbf_event_fsf_unsol("read", adapter, sr_buf);

	switch (sr_buf->status_type) {
	case FSF_STATUS_READ_PORT_CLOSED:
		zfcp_fsf_status_read_port_closed(req);
		break;
	case FSF_STATUS_READ_INCOMING_ELS:
		zfcp_fc_incoming_els(req);
		break;
	case FSF_STATUS_READ_SENSE_DATA_AVAIL:
		break;
	case FSF_STATUS_READ_BIT_ERROR_THRESHOLD:
		dev_warn(&adapter->ccw_device->dev,
			 "The error threshold for checksum statistics "
			 "has been exceeded\n");
		zfcp_hba_dbf_event_berr(adapter, req);
		break;
	case FSF_STATUS_READ_LINK_DOWN:
		zfcp_fsf_status_read_link_down(req);
		break;
	case FSF_STATUS_READ_LINK_UP:
		dev_info(&adapter->ccw_device->dev,
			 "The local link has been restored\n");
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter, "fssrh_1", NULL,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
					ZFCP_STATUS_COMMON_ERP_FAILED,
					"fssrh_2", req);
		break;
	case FSF_STATUS_READ_NOTIFICATION_LOST:
		if (sr_buf->status_subtype & FSF_STATUS_READ_SUB_ACT_UPDATED)
			zfcp_erp_adapter_access_changed(adapter, "fssrh_3",
							req);
		if (sr_buf->status_subtype & FSF_STATUS_READ_SUB_INCOMING_ELS)
			schedule_work(&adapter->scan_work);
		break;
	case FSF_STATUS_READ_CFDC_UPDATED:
		zfcp_erp_adapter_access_changed(adapter, "fssrh_4", req);
		break;
	case FSF_STATUS_READ_FEATURE_UPDATE_ALERT:
		adapter->adapter_features = sr_buf->payload.word[0];
		break;
	}

	mempool_free(sr_buf, adapter->pool.data_status_read);
	zfcp_fsf_req_free(req);

	atomic_inc(&adapter->stat_miss);
	queue_work(zfcp_data.work_queue, &adapter->stat_work);
}

static void zfcp_fsf_fsfstatus_qual_eval(struct zfcp_fsf_req *req)
{
	switch (req->qtcb->header.fsf_status_qual.word[0]) {
	case FSF_SQ_FCP_RSP_AVAILABLE:
	case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
	case FSF_SQ_NO_RETRY_POSSIBLE:
	case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
		return;
	case FSF_SQ_COMMAND_ABORTED:
		req->status |= ZFCP_STATUS_FSFREQ_ABORTED;
		break;
	case FSF_SQ_NO_RECOM:
		dev_err(&req->adapter->ccw_device->dev,
			"The FCP adapter reported a problem "
			"that cannot be recovered\n");
		zfcp_erp_adapter_shutdown(req->adapter, 0, "fsfsqe1", req);
		break;
	}
	/* all non-return stats set FSFREQ_ERROR*/
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

static void zfcp_fsf_fsfstatus_eval(struct zfcp_fsf_req *req)
{
	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ERROR))
		return;

	switch (req->qtcb->header.fsf_status) {
	case FSF_UNKNOWN_COMMAND:
		dev_err(&req->adapter->ccw_device->dev,
			"The FCP adapter does not recognize the command 0x%x\n",
			req->qtcb->header.fsf_command);
		zfcp_erp_adapter_shutdown(req->adapter, 0, "fsfse_1", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		zfcp_fsf_fsfstatus_qual_eval(req);
		break;
	}
}

static void zfcp_fsf_protstatus_eval(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_qtcb *qtcb = req->qtcb;
	union fsf_prot_status_qual *psq = &qtcb->prefix.prot_status_qual;

	zfcp_hba_dbf_event_fsf_response(req);

	if (req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY; /* only for SCSI cmnds. */
		return;
	}

	switch (qtcb->prefix.prot_status) {
	case FSF_PROT_GOOD:
	case FSF_PROT_FSF_STATUS_PRESENTED:
		return;
	case FSF_PROT_QTCB_VERSION_ERROR:
		dev_err(&adapter->ccw_device->dev,
			"QTCB version 0x%x not supported by FCP adapter "
			"(0x%x to 0x%x)\n", FSF_QTCB_CURRENT_VERSION,
			psq->word[0], psq->word[1]);
		zfcp_erp_adapter_shutdown(adapter, 0, "fspse_1", req);
		break;
	case FSF_PROT_ERROR_STATE:
	case FSF_PROT_SEQ_NUMB_ERROR:
		zfcp_erp_adapter_reopen(adapter, 0, "fspse_2", req);
		req->status |= ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_PROT_UNSUPP_QTCB_TYPE:
		dev_err(&adapter->ccw_device->dev,
			"The QTCB type is not supported by the FCP adapter\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "fspse_3", req);
		break;
	case FSF_PROT_HOST_CONNECTION_INITIALIZING:
		atomic_set_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				&adapter->status);
		break;
	case FSF_PROT_DUPLICATE_REQUEST_ID:
		dev_err(&adapter->ccw_device->dev,
			"0x%Lx is an ambiguous request identifier\n",
			(unsigned long long)qtcb->bottom.support.req_handle);
		zfcp_erp_adapter_shutdown(adapter, 0, "fspse_4", req);
		break;
	case FSF_PROT_LINK_DOWN:
		zfcp_fsf_link_down_info_eval(req, "fspse_5",
					     &psq->link_down_info);
		/* FIXME: reopening adapter now? better wait for link up */
		zfcp_erp_adapter_reopen(adapter, 0, "fspse_6", req);
		break;
	case FSF_PROT_REEST_QUEUE:
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter, "fspse_7", NULL,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
					ZFCP_STATUS_COMMON_ERP_FAILED,
					"fspse_8", req);
		break;
	default:
		dev_err(&adapter->ccw_device->dev,
			"0x%x is not a valid transfer protocol status\n",
			qtcb->prefix.prot_status);
		zfcp_erp_adapter_shutdown(adapter, 0, "fspse_9", req);
	}
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

/**
 * zfcp_fsf_req_complete - process completion of a FSF request
 * @fsf_req: The FSF request that has been completed.
 *
 * When a request has been completed either from the FCP adapter,
 * or it has been dismissed due to a queue shutdown, this function
 * is called to process the completion status and trigger further
 * events related to the FSF request.
 */
void zfcp_fsf_req_complete(struct zfcp_fsf_req *req)
{
	if (unlikely(req->fsf_command == FSF_QTCB_UNSOLICITED_STATUS)) {
		zfcp_fsf_status_read_handler(req);
		return;
	}

	del_timer(&req->timer);
	zfcp_fsf_protstatus_eval(req);
	zfcp_fsf_fsfstatus_eval(req);
	req->handler(req);

	if (req->erp_action)
		zfcp_erp_notify(req->erp_action, 0);
	req->status |= ZFCP_STATUS_FSFREQ_COMPLETED;

	if (likely(req->status & ZFCP_STATUS_FSFREQ_CLEANUP))
		zfcp_fsf_req_free(req);
	else
	/* notify initiator waiting for the requests completion */
	/*
	 * FIXME: Race! We must not access fsf_req here as it might have been
	 * cleaned up already due to the set ZFCP_STATUS_FSFREQ_COMPLETED
	 * flag. It's an improbable case. But, we have the same paranoia for
	 * the cleanup flag already.
	 * Might better be handled using complete()?
	 * (setting the flag and doing wakeup ought to be atomic
	 *  with regard to checking the flag as long as waitqueue is
	 *  part of the to be released structure)
	 */
		wake_up(&req->completion_wq);
}

static int zfcp_fsf_exchange_config_evaluate(struct zfcp_fsf_req *req)
{
	struct fsf_qtcb_bottom_config *bottom;
	struct zfcp_adapter *adapter = req->adapter;
	struct Scsi_Host *shost = adapter->scsi_host;

	bottom = &req->qtcb->bottom.config;

	if (req->data)
		memcpy(req->data, bottom, sizeof(*bottom));

	fc_host_node_name(shost) = bottom->nport_serv_param.wwnn;
	fc_host_port_name(shost) = bottom->nport_serv_param.wwpn;
	fc_host_port_id(shost) = bottom->s_id & ZFCP_DID_MASK;
	fc_host_speed(shost) = bottom->fc_link_speed;
	fc_host_supported_classes(shost) = FC_COS_CLASS2 | FC_COS_CLASS3;

	adapter->hydra_version = bottom->adapter_type;
	adapter->timer_ticks = bottom->timer_interval;

	if (fc_host_permanent_port_name(shost) == -1)
		fc_host_permanent_port_name(shost) = fc_host_port_name(shost);

	switch (bottom->fc_topology) {
	case FSF_TOPO_P2P:
		adapter->peer_d_id = bottom->peer_d_id & ZFCP_DID_MASK;
		adapter->peer_wwpn = bottom->plogi_payload.wwpn;
		adapter->peer_wwnn = bottom->plogi_payload.wwnn;
		fc_host_port_type(shost) = FC_PORTTYPE_PTP;
		break;
	case FSF_TOPO_FABRIC:
		fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
		break;
	case FSF_TOPO_AL:
		fc_host_port_type(shost) = FC_PORTTYPE_NLPORT;
		/* fall through */
	default:
		dev_err(&adapter->ccw_device->dev,
			"Unknown or unsupported arbitrated loop "
			"fibre channel topology detected\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "fsece_1", req);
		return -EIO;
	}

	return 0;
}

static void zfcp_fsf_exchange_config_data_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_qtcb *qtcb = req->qtcb;
	struct fsf_qtcb_bottom_config *bottom = &qtcb->bottom.config;
	struct Scsi_Host *shost = adapter->scsi_host;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	adapter->fsf_lic_version = bottom->lic_version;
	adapter->adapter_features = bottom->adapter_features;
	adapter->connection_features = bottom->connection_features;
	adapter->peer_wwpn = 0;
	adapter->peer_wwnn = 0;
	adapter->peer_d_id = 0;

	switch (qtcb->header.fsf_status) {
	case FSF_GOOD:
		if (zfcp_fsf_exchange_config_evaluate(req))
			return;

		if (bottom->max_qtcb_size < sizeof(struct fsf_qtcb)) {
			dev_err(&adapter->ccw_device->dev,
				"FCP adapter maximum QTCB size (%d bytes) "
				"is too small\n",
				bottom->max_qtcb_size);
			zfcp_erp_adapter_shutdown(adapter, 0, "fsecdh1", req);
			return;
		}
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				&adapter->status);
		break;
	case FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE:
		fc_host_node_name(shost) = 0;
		fc_host_port_name(shost) = 0;
		fc_host_port_id(shost) = 0;
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
		fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;
		adapter->hydra_version = 0;

		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				&adapter->status);

		zfcp_fsf_link_down_info_eval(req, "fsecdh2",
			&qtcb->header.fsf_status_qual.link_down_info);
		break;
	default:
		zfcp_erp_adapter_shutdown(adapter, 0, "fsecdh3", req);
		return;
	}

	if (adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT) {
		adapter->hardware_version = bottom->hardware_version;
		memcpy(fc_host_serial_number(shost), bottom->serial_number,
		       min(FC_SERIAL_NUMBER_SIZE, 17));
		EBCASC(fc_host_serial_number(shost),
		       min(FC_SERIAL_NUMBER_SIZE, 17));
	}

	if (FSF_QTCB_CURRENT_VERSION < bottom->low_qtcb_version) {
		dev_err(&adapter->ccw_device->dev,
			"The FCP adapter only supports newer "
			"control block versions\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "fsecdh4", req);
		return;
	}
	if (FSF_QTCB_CURRENT_VERSION > bottom->high_qtcb_version) {
		dev_err(&adapter->ccw_device->dev,
			"The FCP adapter only supports older "
			"control block versions\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "fsecdh5", req);
	}
}

static void zfcp_fsf_exchange_port_evaluate(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_qtcb_bottom_port *bottom = &req->qtcb->bottom.port;
	struct Scsi_Host *shost = adapter->scsi_host;

	if (req->data)
		memcpy(req->data, bottom, sizeof(*bottom));

	if (adapter->connection_features & FSF_FEATURE_NPIV_MODE) {
		fc_host_permanent_port_name(shost) = bottom->wwpn;
		fc_host_port_type(shost) = FC_PORTTYPE_NPIV;
	} else
		fc_host_permanent_port_name(shost) = fc_host_port_name(shost);
	fc_host_maxframe_size(shost) = bottom->maximum_frame_size;
	fc_host_supported_speeds(shost) = bottom->supported_speed;
}

static void zfcp_fsf_exchange_port_data_handler(struct zfcp_fsf_req *req)
{
	struct fsf_qtcb *qtcb = req->qtcb;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	switch (qtcb->header.fsf_status) {
	case FSF_GOOD:
		zfcp_fsf_exchange_port_evaluate(req);
		break;
	case FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE:
		zfcp_fsf_exchange_port_evaluate(req);
		zfcp_fsf_link_down_info_eval(req, "fsepdh1",
			&qtcb->header.fsf_status_qual.link_down_info);
		break;
	}
}

static int zfcp_fsf_sbal_check(struct zfcp_adapter *adapter)
{
	struct zfcp_qdio_queue *req_q = &adapter->req_q;

	spin_lock_bh(&adapter->req_q_lock);
	if (atomic_read(&req_q->count))
		return 1;
	spin_unlock_bh(&adapter->req_q_lock);
	return 0;
}

static int zfcp_fsf_req_sbal_get(struct zfcp_adapter *adapter)
{
	long ret;

	spin_unlock_bh(&adapter->req_q_lock);
	ret = wait_event_interruptible_timeout(adapter->request_wq,
			       zfcp_fsf_sbal_check(adapter), 5 * HZ);
	if (ret > 0)
		return 0;
	if (!ret)
		atomic_inc(&adapter->qdio_outb_full);

	spin_lock_bh(&adapter->req_q_lock);
	return -EIO;
}

static struct zfcp_fsf_req *zfcp_fsf_alloc_noqtcb(mempool_t *pool)
{
	struct zfcp_fsf_req *req;
	req = mempool_alloc(pool, GFP_ATOMIC);
	if (!req)
		return NULL;
	memset(req, 0, sizeof(*req));
	req->pool = pool;
	return req;
}

static struct zfcp_fsf_req *zfcp_fsf_alloc_qtcb(mempool_t *pool)
{
	struct zfcp_fsf_req_qtcb *qtcb;

	if (likely(pool))
		qtcb = mempool_alloc(pool, GFP_ATOMIC);
	else
		qtcb = kmem_cache_alloc(zfcp_data.fsf_req_qtcb_cache,
					GFP_ATOMIC);
	if (unlikely(!qtcb))
		return NULL;

	memset(qtcb, 0, sizeof(*qtcb));
	qtcb->fsf_req.qtcb = &qtcb->qtcb;
	qtcb->fsf_req.pool = pool;

	return &qtcb->fsf_req;
}

static struct zfcp_fsf_req *zfcp_fsf_req_create(struct zfcp_adapter *adapter,
						u32 fsf_cmd, int req_flags,
						mempool_t *pool)
{
	struct qdio_buffer_element *sbale;

	struct zfcp_fsf_req *req;
	struct zfcp_qdio_queue *req_q = &adapter->req_q;

	if (req_flags & ZFCP_REQ_NO_QTCB)
		req = zfcp_fsf_alloc_noqtcb(pool);
	else
		req = zfcp_fsf_alloc_qtcb(pool);

	if (unlikely(!req))
		return ERR_PTR(-EIO);

	if (adapter->req_no == 0)
		adapter->req_no++;

	INIT_LIST_HEAD(&req->list);
	init_timer(&req->timer);
	init_waitqueue_head(&req->completion_wq);

	req->adapter = adapter;
	req->fsf_command = fsf_cmd;
	req->req_id = adapter->req_no;
	req->sbal_number = 1;
	req->sbal_first = req_q->first;
	req->sbal_last = req_q->first;
	req->sbale_curr = 1;

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].addr = (void *) req->req_id;
	sbale[0].flags |= SBAL_FLAGS0_COMMAND;

	if (likely(req->qtcb)) {
		req->qtcb->prefix.req_seq_no = req->adapter->fsf_req_seq_no;
		req->qtcb->prefix.req_id = req->req_id;
		req->qtcb->prefix.ulp_info = 26;
		req->qtcb->prefix.qtcb_type = fsf_qtcb_type[req->fsf_command];
		req->qtcb->prefix.qtcb_version = FSF_QTCB_CURRENT_VERSION;
		req->qtcb->header.req_handle = req->req_id;
		req->qtcb->header.fsf_command = req->fsf_command;
		req->seq_no = adapter->fsf_req_seq_no;
		req->qtcb->prefix.req_seq_no = adapter->fsf_req_seq_no;
		sbale[1].addr = (void *) req->qtcb;
		sbale[1].length = sizeof(struct fsf_qtcb);
	}

	if (!(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP)) {
		zfcp_fsf_req_free(req);
		return ERR_PTR(-EIO);
	}

	if (likely(req_flags & ZFCP_REQ_AUTO_CLEANUP))
		req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;

	return req;
}

static int zfcp_fsf_req_send(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	unsigned long	     flags;
	int		     idx;
	int		     with_qtcb = (req->qtcb != NULL);

	/* put allocated FSF request into hash table */
	spin_lock_irqsave(&adapter->req_list_lock, flags);
	idx = zfcp_reqlist_hash(req->req_id);
	list_add_tail(&req->list, &adapter->req_list[idx]);
	spin_unlock_irqrestore(&adapter->req_list_lock, flags);

	req->qdio_outb_usage = atomic_read(&adapter->req_q.count);
	req->issued = get_clock();
	if (zfcp_qdio_send(req)) {
		del_timer(&req->timer);
		spin_lock_irqsave(&adapter->req_list_lock, flags);
		/* lookup request again, list might have changed */
		if (zfcp_reqlist_find_safe(adapter, req))
			zfcp_reqlist_remove(adapter, req);
		spin_unlock_irqrestore(&adapter->req_list_lock, flags);
		zfcp_erp_adapter_reopen(adapter, 0, "fsrs__1", req);
		return -EIO;
	}

	/* Don't increase for unsolicited status */
	if (with_qtcb)
		adapter->fsf_req_seq_no++;
	adapter->req_no++;

	return 0;
}

/**
 * zfcp_fsf_status_read - send status read request
 * @adapter: pointer to struct zfcp_adapter
 * @req_flags: request flags
 * Returns: 0 on success, ERROR otherwise
 */
int zfcp_fsf_status_read(struct zfcp_adapter *adapter)
{
	struct zfcp_fsf_req *req;
	struct fsf_status_read_buffer *sr_buf;
	struct qdio_buffer_element *sbale;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_UNSOLICITED_STATUS,
				  ZFCP_REQ_NO_QTCB,
				  adapter->pool.fsf_req_status_read);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_STATUS;
	sbale[2].flags |= SBAL_FLAGS_LAST_ENTRY;
	req->sbale_curr = 2;

	sr_buf = mempool_alloc(adapter->pool.data_status_read, GFP_ATOMIC);
	if (!sr_buf) {
		retval = -ENOMEM;
		goto failed_buf;
	}
	memset(sr_buf, 0, sizeof(*sr_buf));
	req->data = sr_buf;
	sbale = zfcp_qdio_sbale_curr(req);
	sbale->addr = (void *) sr_buf;
	sbale->length = sizeof(*sr_buf);

	retval = zfcp_fsf_req_send(req);
	if (retval)
		goto failed_req_send;

	goto out;

failed_req_send:
	mempool_free(sr_buf, adapter->pool.data_status_read);
failed_buf:
	zfcp_fsf_req_free(req);
	zfcp_hba_dbf_event_fsf_unsol("fail", adapter, NULL);
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

static void zfcp_fsf_abort_fcp_command_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_unit *unit = req->data;
	union fsf_status_qual *fsq = &req->qtcb->header.fsf_status_qual;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	switch (req->qtcb->header.fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		if (fsq->word[0] == fsq->word[1]) {
			zfcp_erp_adapter_reopen(unit->port->adapter, 0,
						"fsafch1", req);
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;
	case FSF_LUN_HANDLE_NOT_VALID:
		if (fsq->word[0] == fsq->word[1]) {
			zfcp_erp_port_reopen(unit->port, 0, "fsafch2", req);
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;
	case FSF_FCP_COMMAND_DOES_NOT_EXIST:
		req->status |= ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED;
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, "fsafch3", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_LUN_BOXED:
		zfcp_erp_unit_boxed(unit, "fsafch4", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
                break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (fsq->word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			zfcp_test_link(unit->port);
			/* fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_GOOD:
		req->status |= ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED;
		break;
	}
}

/**
 * zfcp_fsf_abort_fcp_command - abort running SCSI command
 * @old_req_id: unsigned long
 * @unit: pointer to struct zfcp_unit
 * Returns: pointer to struct zfcp_fsf_req
 */

struct zfcp_fsf_req *zfcp_fsf_abort_fcp_command(unsigned long old_req_id,
						struct zfcp_unit *unit)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;
	struct zfcp_adapter *adapter = unit->port->adapter;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_ABORT_FCP_CMND,
				  0, adapter->pool.fsf_req_abort);
	if (IS_ERR(req)) {
		req = NULL;
		goto out;
	}

	if (unlikely(!(atomic_read(&unit->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		goto out_error_free;

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->data = unit;
	req->handler = zfcp_fsf_abort_fcp_command_handler;
	req->qtcb->header.lun_handle = unit->handle;
	req->qtcb->header.port_handle = unit->port->handle;
	req->qtcb->bottom.support.req_handle = (u64) old_req_id;

	zfcp_fsf_start_timer(req, ZFCP_SCSI_ER_TIMEOUT);
	if (!zfcp_fsf_req_send(req))
		goto out;

out_error_free:
	zfcp_fsf_req_free(req);
	req = NULL;
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return req;
}

static void zfcp_fsf_send_ct_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_send_ct *send_ct = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	send_ct->status = -EINVAL;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	switch (header->fsf_status) {
        case FSF_GOOD:
		zfcp_san_dbf_event_ct_response(req);
		send_ct->status = 0;
		break;
        case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		zfcp_fsf_class_not_supp(req);
		break;
        case FSF_ADAPTER_STATUS_AVAILABLE:
                switch (header->fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
                }
                break;
	case FSF_ACCESS_DENIED:
		break;
        case FSF_PORT_BOXED:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(adapter, 0, "fsscth1", req);
		/* fall through */
	case FSF_GENERIC_COMMAND_REJECTED:
	case FSF_PAYLOAD_SIZE_MISMATCH:
	case FSF_REQUEST_SIZE_TOO_LARGE:
	case FSF_RESPONSE_SIZE_TOO_LARGE:
	case FSF_SBAL_MISMATCH:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}

skip_fsfstatus:
	if (send_ct->handler)
		send_ct->handler(send_ct->handler_data);
}

static int zfcp_fsf_setup_ct_els_sbals(struct zfcp_fsf_req *req,
				       struct scatterlist *sg_req,
				       struct scatterlist *sg_resp,
				       int max_sbals)
{
	struct qdio_buffer_element *sbale = zfcp_qdio_sbale_req(req);
	u32 feat = req->adapter->adapter_features;
	int bytes;

	if (!(feat & FSF_FEATURE_ELS_CT_CHAINED_SBALS)) {
		if (sg_req->length > PAGE_SIZE || sg_resp->length > PAGE_SIZE ||
		    !sg_is_last(sg_req) || !sg_is_last(sg_resp))
			return -EOPNOTSUPP;

		sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE_READ;
		sbale[2].addr   = sg_virt(sg_req);
		sbale[2].length = sg_req->length;
		sbale[3].addr   = sg_virt(sg_resp);
		sbale[3].length = sg_resp->length;
		sbale[3].flags |= SBAL_FLAGS_LAST_ENTRY;
		return 0;
	}

	bytes = zfcp_qdio_sbals_from_sg(req, SBAL_FLAGS0_TYPE_WRITE_READ,
					sg_req, max_sbals);
	if (bytes <= 0)
		return -ENOMEM;
	req->qtcb->bottom.support.req_buf_length = bytes;
	req->sbale_curr = ZFCP_LAST_SBALE_PER_SBAL;

	bytes = zfcp_qdio_sbals_from_sg(req, SBAL_FLAGS0_TYPE_WRITE_READ,
					sg_resp, max_sbals);
	if (bytes <= 0)
		return -ENOMEM;
	req->qtcb->bottom.support.resp_buf_length = bytes;

	return 0;
}

/**
 * zfcp_fsf_send_ct - initiate a Generic Service request (FC-GS)
 * @ct: pointer to struct zfcp_send_ct with data for request
 * @pool: if non-null this mempool is used to allocate struct zfcp_fsf_req
 * @erp_action: if non-null the Generic Service request sent within ERP
 */
int zfcp_fsf_send_ct(struct zfcp_send_ct *ct, mempool_t *pool,
		     struct zfcp_erp_action *erp_action)
{
	struct zfcp_wka_port *wka_port = ct->wka_port;
	struct zfcp_adapter *adapter = wka_port->adapter;
	struct zfcp_fsf_req *req;
	int ret = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_SEND_GENERIC,
				  ZFCP_REQ_AUTO_CLEANUP, pool);
	if (IS_ERR(req)) {
		ret = PTR_ERR(req);
		goto out;
	}

	ret = zfcp_fsf_setup_ct_els_sbals(req, ct->req, ct->resp,
					  FSF_MAX_SBALS_PER_REQ);
	if (ret)
		goto failed_send;

	req->handler = zfcp_fsf_send_ct_handler;
	req->qtcb->header.port_handle = wka_port->handle;
	req->qtcb->bottom.support.service_class = FSF_CLASS_3;
	req->qtcb->bottom.support.timeout = ct->timeout;
	req->data = ct;

	zfcp_san_dbf_event_ct_request(req);

	if (erp_action) {
		erp_action->fsf_req = req;
		req->erp_action = erp_action;
		zfcp_fsf_start_erp_timer(req);
	} else
		zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);

	ret = zfcp_fsf_req_send(req);
	if (ret)
		goto failed_send;

	goto out;

failed_send:
	zfcp_fsf_req_free(req);
	if (erp_action)
		erp_action->fsf_req = NULL;
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return ret;
}

static void zfcp_fsf_send_els_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_send_els *send_els = req->data;
	struct zfcp_port *port = send_els->port;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	send_els->status = -EINVAL;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	switch (header->fsf_status) {
	case FSF_GOOD:
		zfcp_san_dbf_event_els_response(req);
		send_els->status = 0;
		break;
	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		zfcp_fsf_class_not_supp(req);
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]){
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			if (port && (send_els->ls_code != ZFCP_LS_ADISC))
				zfcp_test_link(port);
			/*fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
		case FSF_SQ_RETRY_IF_POSSIBLE:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_ELS_COMMAND_REJECTED:
	case FSF_PAYLOAD_SIZE_MISMATCH:
	case FSF_REQUEST_SIZE_TOO_LARGE:
	case FSF_RESPONSE_SIZE_TOO_LARGE:
		break;
	case FSF_ACCESS_DENIED:
		if (port)
			zfcp_fsf_access_denied_port(req, port);
		break;
	case FSF_SBAL_MISMATCH:
		/* should never occure, avoided in zfcp_fsf_send_els */
		/* fall through */
	default:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}
skip_fsfstatus:
	if (send_els->handler)
		send_els->handler(send_els->handler_data);
}

/**
 * zfcp_fsf_send_els - initiate an ELS command (FC-FS)
 * @els: pointer to struct zfcp_send_els with data for the command
 */
int zfcp_fsf_send_els(struct zfcp_send_els *els)
{
	struct zfcp_fsf_req *req;
	struct zfcp_adapter *adapter = els->adapter;
	struct fsf_qtcb_bottom_support *bottom;
	int ret = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_SEND_ELS,
				  ZFCP_REQ_AUTO_CLEANUP, NULL);
	if (IS_ERR(req)) {
		ret = PTR_ERR(req);
		goto out;
	}

	ret = zfcp_fsf_setup_ct_els_sbals(req, els->req, els->resp, 2);

	if (ret)
		goto failed_send;

	bottom = &req->qtcb->bottom.support;
	req->handler = zfcp_fsf_send_els_handler;
	bottom->d_id = els->d_id;
	bottom->service_class = FSF_CLASS_3;
	bottom->timeout = 2 * R_A_TOV;
	req->data = els;

	zfcp_san_dbf_event_els_request(req);

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	ret = zfcp_fsf_req_send(req);
	if (ret)
		goto failed_send;

	goto out;

failed_send:
	zfcp_fsf_req_free(req);
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return ret;
}

int zfcp_fsf_exchange_config_data(struct zfcp_erp_action *erp_action)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req;
	struct zfcp_adapter *adapter = erp_action->adapter;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;
	req = zfcp_fsf_req_create(adapter,
				  FSF_QTCB_EXCHANGE_CONFIG_DATA,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->qtcb->bottom.config.feature_selection =
			FSF_FEATURE_CFDC |
			FSF_FEATURE_LUN_SHARING |
			FSF_FEATURE_NOTIFICATION_LOST |
			FSF_FEATURE_UPDATE_ALERT;
	req->erp_action = erp_action;
	req->handler = zfcp_fsf_exchange_config_data_handler;
	erp_action->fsf_req = req;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

int zfcp_fsf_exchange_config_data_sync(struct zfcp_adapter *adapter,
				       struct fsf_qtcb_bottom_config *data)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out_unlock;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_CONFIG_DATA,
				  0, NULL);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out_unlock;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;
	req->handler = zfcp_fsf_exchange_config_data_handler;

	req->qtcb->bottom.config.feature_selection =
			FSF_FEATURE_CFDC |
			FSF_FEATURE_LUN_SHARING |
			FSF_FEATURE_NOTIFICATION_LOST |
			FSF_FEATURE_UPDATE_ALERT;

	if (data)
		req->data = data;

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
	spin_unlock_bh(&adapter->req_q_lock);
	if (!retval)
		wait_event(req->completion_wq,
			   req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	zfcp_fsf_req_free(req);
	return retval;

out_unlock:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

/**
 * zfcp_fsf_exchange_port_data - request information about local port
 * @erp_action: ERP action for the adapter for which port data is requested
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_exchange_port_data(struct zfcp_erp_action *erp_action)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req;
	struct zfcp_adapter *adapter = erp_action->adapter;
	int retval = -EIO;

	if (!(adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT))
		return -EOPNOTSUPP;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_PORT_DATA,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->handler = zfcp_fsf_exchange_port_data_handler;
	req->erp_action = erp_action;
	erp_action->fsf_req = req;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

/**
 * zfcp_fsf_exchange_port_data_sync - request information about local port
 * @adapter: pointer to struct zfcp_adapter
 * @data: pointer to struct fsf_qtcb_bottom_port
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_exchange_port_data_sync(struct zfcp_adapter *adapter,
				     struct fsf_qtcb_bottom_port *data)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;
	int retval = -EIO;

	if (!(adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT))
		return -EOPNOTSUPP;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out_unlock;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_PORT_DATA, 0,
				  NULL);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out_unlock;
	}

	if (data)
		req->data = data;

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->handler = zfcp_fsf_exchange_port_data_handler;
	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
	spin_unlock_bh(&adapter->req_q_lock);

	if (!retval)
		wait_event(req->completion_wq,
			   req->status & ZFCP_STATUS_FSFREQ_COMPLETED);
	zfcp_fsf_req_free(req);

	return retval;

out_unlock:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

static void zfcp_fsf_open_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_port *port = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;
	struct fsf_plogi *plogi;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto out;

	switch (header->fsf_status) {
	case FSF_PORT_ALREADY_OPEN:
		break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(req, port);
		break;
	case FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "Not enough FCP adapter resources to open "
			 "remote port 0x%016Lx\n",
			 (unsigned long long)port->wwpn);
		zfcp_erp_port_failed(port, "fsoph_1", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
		case FSF_SQ_NO_RETRY_POSSIBLE:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_GOOD:
		port->handle = header->port_handle;
		atomic_set_mask(ZFCP_STATUS_COMMON_OPEN |
				ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		atomic_clear_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED |
		                  ZFCP_STATUS_COMMON_ACCESS_BOXED,
		                  &port->status);
		/* check whether D_ID has changed during open */
		/*
		 * FIXME: This check is not airtight, as the FCP channel does
		 * not monitor closures of target port connections caused on
		 * the remote side. Thus, they might miss out on invalidating
		 * locally cached WWPNs (and other N_Port parameters) of gone
		 * target ports. So, our heroic attempt to make things safe
		 * could be undermined by 'open port' response data tagged with
		 * obsolete WWPNs. Another reason to monitor potential
		 * connection closures ourself at least (by interpreting
		 * incoming ELS' and unsolicited status). It just crosses my
		 * mind that one should be able to cross-check by means of
		 * another GID_PN straight after a port has been opened.
		 * Alternately, an ADISC/PDISC ELS should suffice, as well.
		 */
		plogi = (struct fsf_plogi *) req->qtcb->bottom.support.els;
		if (req->qtcb->bottom.support.els1_length >=
		    FSF_PLOGI_MIN_LEN) {
			if (plogi->serv_param.wwpn != port->wwpn)
				port->d_id = 0;
			else {
				port->wwnn = plogi->serv_param.wwnn;
				zfcp_fc_plogi_evaluate(port, plogi);
			}
		}
		break;
	case FSF_UNKNOWN_OP_SUBTYPE:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}

out:
	zfcp_port_put(port);
}

/**
 * zfcp_fsf_open_port - create and send open port request
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_open_port(struct zfcp_erp_action *erp_action)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	struct zfcp_port *port = erp_action->port;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter,
				  FSF_QTCB_OPEN_PORT_WITH_DID,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->handler = zfcp_fsf_open_port_handler;
	req->qtcb->bottom.support.d_id = port->d_id;
	req->data = port;
	req->erp_action = erp_action;
	erp_action->fsf_req = req;
	zfcp_port_get(port);

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
		zfcp_port_put(port);
	}
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

static void zfcp_fsf_close_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_port *port = req->data;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	switch (req->qtcb->header.fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(port->adapter, 0, "fscph_1", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		break;
	case FSF_GOOD:
		zfcp_erp_modify_port_status(port, "fscph_2", req,
					    ZFCP_STATUS_COMMON_OPEN,
					    ZFCP_CLEAR);
		break;
	}
}

/**
 * zfcp_fsf_close_port - create and send close port request
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_close_port(struct zfcp_erp_action *erp_action)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_CLOSE_PORT,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->handler = zfcp_fsf_close_port_handler;
	req->data = erp_action->port;
	req->erp_action = erp_action;
	req->qtcb->header.port_handle = erp_action->port->handle;
	erp_action->fsf_req = req;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

static void zfcp_fsf_open_wka_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_wka_port *wka_port = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		wka_port->status = ZFCP_WKA_PORT_OFFLINE;
		goto out;
	}

	switch (header->fsf_status) {
	case FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "Opening WKA port 0x%x failed\n", wka_port->d_id);
		/* fall through */
	case FSF_ADAPTER_STATUS_AVAILABLE:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		/* fall through */
	case FSF_ACCESS_DENIED:
		wka_port->status = ZFCP_WKA_PORT_OFFLINE;
		break;
	case FSF_PORT_ALREADY_OPEN:
		break;
	case FSF_GOOD:
		wka_port->handle = header->port_handle;
		wka_port->status = ZFCP_WKA_PORT_ONLINE;
	}
out:
	wake_up(&wka_port->completion_wq);
}

/**
 * zfcp_fsf_open_wka_port - create and send open wka-port request
 * @wka_port: pointer to struct zfcp_wka_port
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_open_wka_port(struct zfcp_wka_port *wka_port)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = wka_port->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter,
				  FSF_QTCB_OPEN_PORT_WITH_DID,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	if (unlikely(IS_ERR(req))) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->handler = zfcp_fsf_open_wka_port_handler;
	req->qtcb->bottom.support.d_id = wka_port->d_id;
	req->data = wka_port;

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
	if (retval)
		zfcp_fsf_req_free(req);
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

static void zfcp_fsf_close_wka_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_wka_port *wka_port = req->data;

	if (req->qtcb->header.fsf_status == FSF_PORT_HANDLE_NOT_VALID) {
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		zfcp_erp_adapter_reopen(wka_port->adapter, 0, "fscwph1", req);
	}

	wka_port->status = ZFCP_WKA_PORT_OFFLINE;
	wake_up(&wka_port->completion_wq);
}

/**
 * zfcp_fsf_close_wka_port - create and send close wka port request
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_close_wka_port(struct zfcp_wka_port *wka_port)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = wka_port->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_CLOSE_PORT,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	if (unlikely(IS_ERR(req))) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->handler = zfcp_fsf_close_wka_port_handler;
	req->data = wka_port;
	req->qtcb->header.port_handle = wka_port->handle;

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
	if (retval)
		zfcp_fsf_req_free(req);
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

static void zfcp_fsf_close_physical_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_port *port = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;
	struct zfcp_unit *unit;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	switch (header->fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(port->adapter, 0, "fscpph1", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(req, port);
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(port, "fscpph2", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		/* can't use generic zfcp_erp_modify_port_status because
		 * ZFCP_STATUS_COMMON_OPEN must not be reset for the port */
		atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		list_for_each_entry(unit, &port->unit_list_head, list)
			atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN,
					  &unit->status);
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_GOOD:
		/* can't use generic zfcp_erp_modify_port_status because
		 * ZFCP_STATUS_COMMON_OPEN must not be reset for the port
		 */
		atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		list_for_each_entry(unit, &port->unit_list_head, list)
			atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN,
					  &unit->status);
		break;
	}
}

/**
 * zfcp_fsf_close_physical_port - close physical port
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success
 */
int zfcp_fsf_close_physical_port(struct zfcp_erp_action *erp_action)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_CLOSE_PHYSICAL_PORT,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->data = erp_action->port;
	req->qtcb->header.port_handle = erp_action->port->handle;
	req->erp_action = erp_action;
	req->handler = zfcp_fsf_close_physical_port_handler;
	erp_action->fsf_req = req;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

static void zfcp_fsf_open_unit_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_unit *unit = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;
	struct fsf_qtcb_bottom_support *bottom = &req->qtcb->bottom.support;
	struct fsf_queue_designator *queue_designator =
				&header->fsf_status_qual.fsf_queue_designator;
	int exclusive, readwrite;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	atomic_clear_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_COMMON_ACCESS_BOXED |
			  ZFCP_STATUS_UNIT_SHARED |
			  ZFCP_STATUS_UNIT_READONLY,
			  &unit->status);

	switch (header->fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, "fsouh_1", req);
		/* fall through */
	case FSF_LUN_ALREADY_OPEN:
		break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_unit(req, unit);
		atomic_clear_mask(ZFCP_STATUS_UNIT_SHARED, &unit->status);
		atomic_clear_mask(ZFCP_STATUS_UNIT_READONLY, &unit->status);
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, "fsouh_2", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_LUN_SHARING_VIOLATION:
		if (header->fsf_status_qual.word[0])
			dev_warn(&adapter->ccw_device->dev,
				 "LUN 0x%Lx on port 0x%Lx is already in "
				 "use by CSS%d, MIF Image ID %x\n",
				 (unsigned long long)unit->fcp_lun,
				 (unsigned long long)unit->port->wwpn,
				 queue_designator->cssid,
				 queue_designator->hla);
		else
			zfcp_act_eval_err(adapter,
					  header->fsf_status_qual.word[2]);
		zfcp_erp_unit_access_denied(unit, "fsouh_3", req);
		atomic_clear_mask(ZFCP_STATUS_UNIT_SHARED, &unit->status);
		atomic_clear_mask(ZFCP_STATUS_UNIT_READONLY, &unit->status);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED:
		dev_warn(&adapter->ccw_device->dev,
			 "No handle is available for LUN "
			 "0x%016Lx on port 0x%016Lx\n",
			 (unsigned long long)unit->fcp_lun,
			 (unsigned long long)unit->port->wwpn);
		zfcp_erp_unit_failed(unit, "fsouh_4", req);
		/* fall through */
	case FSF_INVALID_COMMAND_OPTION:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			zfcp_test_link(unit->port);
			/* fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;

	case FSF_GOOD:
		unit->handle = header->lun_handle;
		atomic_set_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);

		if (!(adapter->connection_features & FSF_FEATURE_NPIV_MODE) &&
		    (adapter->adapter_features & FSF_FEATURE_LUN_SHARING) &&
		    !zfcp_ccw_priv_sch(adapter)) {
			exclusive = (bottom->lun_access_info &
					FSF_UNIT_ACCESS_EXCLUSIVE);
			readwrite = (bottom->lun_access_info &
					FSF_UNIT_ACCESS_OUTBOUND_TRANSFER);

			if (!exclusive)
		                atomic_set_mask(ZFCP_STATUS_UNIT_SHARED,
						&unit->status);

			if (!readwrite) {
                		atomic_set_mask(ZFCP_STATUS_UNIT_READONLY,
						&unit->status);
				dev_info(&adapter->ccw_device->dev,
					 "SCSI device at LUN 0x%016Lx on port "
					 "0x%016Lx opened read-only\n",
					 (unsigned long long)unit->fcp_lun,
					 (unsigned long long)unit->port->wwpn);
        		}

        		if (exclusive && !readwrite) {
				dev_err(&adapter->ccw_device->dev,
					"Exclusive read-only access not "
					"supported (unit 0x%016Lx, "
					"port 0x%016Lx)\n",
					(unsigned long long)unit->fcp_lun,
					(unsigned long long)unit->port->wwpn);
				zfcp_erp_unit_failed(unit, "fsouh_5", req);
				req->status |= ZFCP_STATUS_FSFREQ_ERROR;
				zfcp_erp_unit_shutdown(unit, 0, "fsouh_6", req);
        		} else if (!exclusive && readwrite) {
				dev_err(&adapter->ccw_device->dev,
					"Shared read-write access not "
					"supported (unit 0x%016Lx, port "
					"0x%016Lx)\n",
					(unsigned long long)unit->fcp_lun,
					(unsigned long long)unit->port->wwpn);
				zfcp_erp_unit_failed(unit, "fsouh_7", req);
				req->status |= ZFCP_STATUS_FSFREQ_ERROR;
				zfcp_erp_unit_shutdown(unit, 0, "fsouh_8", req);
        		}
		}
		break;
	}
}

/**
 * zfcp_fsf_open_unit - open unit
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_open_unit(struct zfcp_erp_action *erp_action)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_OPEN_LUN,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->qtcb->header.port_handle = erp_action->port->handle;
	req->qtcb->bottom.support.fcp_lun = erp_action->unit->fcp_lun;
	req->handler = zfcp_fsf_open_unit_handler;
	req->data = erp_action->unit;
	req->erp_action = erp_action;
	erp_action->fsf_req = req;

	if (!(adapter->connection_features & FSF_FEATURE_NPIV_MODE))
		req->qtcb->bottom.support.option = FSF_OPEN_LUN_SUPPRESS_BOXING;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

static void zfcp_fsf_close_unit_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_unit *unit = req->data;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	switch (req->qtcb->header.fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, "fscuh_1", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_LUN_HANDLE_NOT_VALID:
		zfcp_erp_port_reopen(unit->port, 0, "fscuh_2", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, "fscuh_3", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			zfcp_test_link(unit->port);
			/* fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_GOOD:
		atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
		break;
	}
}

/**
 * zfcp_fsf_close_unit - close zfcp unit
 * @erp_action: pointer to struct zfcp_unit
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_close_unit(struct zfcp_erp_action *erp_action)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_CLOSE_LUN,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->qtcb->header.port_handle = erp_action->port->handle;
	req->qtcb->header.lun_handle = erp_action->unit->handle;
	req->handler = zfcp_fsf_close_unit_handler;
	req->data = erp_action->unit;
	req->erp_action = erp_action;
	erp_action->fsf_req = req;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return retval;
}

static void zfcp_fsf_update_lat(struct fsf_latency_record *lat_rec, u32 lat)
{
	lat_rec->sum += lat;
	lat_rec->min = min(lat_rec->min, lat);
	lat_rec->max = max(lat_rec->max, lat);
}

static void zfcp_fsf_req_latency(struct zfcp_fsf_req *req)
{
	struct fsf_qual_latency_info *lat_inf;
	struct latency_cont *lat;
	struct zfcp_unit *unit = req->unit;

	lat_inf = &req->qtcb->prefix.prot_status_qual.latency_info;

	switch (req->qtcb->bottom.io.data_direction) {
	case FSF_DATADIR_READ:
		lat = &unit->latencies.read;
		break;
	case FSF_DATADIR_WRITE:
		lat = &unit->latencies.write;
		break;
	case FSF_DATADIR_CMND:
		lat = &unit->latencies.cmd;
		break;
	default:
		return;
	}

	spin_lock(&unit->latencies.lock);
	zfcp_fsf_update_lat(&lat->channel, lat_inf->channel_lat);
	zfcp_fsf_update_lat(&lat->fabric, lat_inf->fabric_lat);
	lat->counter++;
	spin_unlock(&unit->latencies.lock);
}

#ifdef CONFIG_BLK_DEV_IO_TRACE
static void zfcp_fsf_trace_latency(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_qual_latency_info *lat_inf;
	struct scsi_cmnd *scsi_cmnd = (struct scsi_cmnd *)fsf_req->data;
	struct request *req = scsi_cmnd->request;
	struct zfcp_blk_drv_data trace;
	int ticks = fsf_req->adapter->timer_ticks;

	trace.flags = 0;
	trace.magic = ZFCP_BLK_DRV_DATA_MAGIC;
	if (fsf_req->adapter->adapter_features & FSF_FEATURE_MEASUREMENT_DATA) {
		trace.flags |= ZFCP_BLK_LAT_VALID;
		lat_inf = &fsf_req->qtcb->prefix.prot_status_qual.latency_info;
		trace.channel_lat = lat_inf->channel_lat * ticks;
		trace.fabric_lat = lat_inf->fabric_lat * ticks;
	}
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)
		trace.flags |= ZFCP_BLK_REQ_ERROR;
	trace.inb_usage = fsf_req->qdio_inb_usage;
	trace.outb_usage = fsf_req->qdio_outb_usage;

	blk_add_driver_data(req->q, req, &trace, sizeof(trace));
}
#else
static inline void zfcp_fsf_trace_latency(struct zfcp_fsf_req *fsf_req)
{
}
#endif

static void zfcp_fsf_send_fcp_command_task_handler(struct zfcp_fsf_req *req)
{
	struct scsi_cmnd *scpnt;
	struct fcp_rsp_iu *fcp_rsp_iu = (struct fcp_rsp_iu *)
	    &(req->qtcb->bottom.io.fcp_rsp);
	u32 sns_len;
	char *fcp_rsp_info = (unsigned char *) &fcp_rsp_iu[1];
	unsigned long flags;

	read_lock_irqsave(&req->adapter->abort_lock, flags);

	scpnt = req->data;
	if (unlikely(!scpnt)) {
		read_unlock_irqrestore(&req->adapter->abort_lock, flags);
		return;
	}

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ABORTED)) {
		set_host_byte(scpnt, DID_SOFT_ERROR);
		goto skip_fsfstatus;
	}

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ERROR)) {
		set_host_byte(scpnt, DID_ERROR);
		goto skip_fsfstatus;
	}

	set_msg_byte(scpnt, COMMAND_COMPLETE);

	scpnt->result |= fcp_rsp_iu->scsi_status;

	if (req->adapter->adapter_features & FSF_FEATURE_MEASUREMENT_DATA)
		zfcp_fsf_req_latency(req);

	zfcp_fsf_trace_latency(req);

	if (unlikely(fcp_rsp_iu->validity.bits.fcp_rsp_len_valid)) {
		if (fcp_rsp_info[3] == RSP_CODE_GOOD)
			set_host_byte(scpnt, DID_OK);
		else {
			set_host_byte(scpnt, DID_ERROR);
			goto skip_fsfstatus;
		}
	}

	if (unlikely(fcp_rsp_iu->validity.bits.fcp_sns_len_valid)) {
		sns_len = FSF_FCP_RSP_SIZE - sizeof(struct fcp_rsp_iu) +
			  fcp_rsp_iu->fcp_rsp_len;
		sns_len = min(sns_len, (u32) SCSI_SENSE_BUFFERSIZE);
		sns_len = min(sns_len, fcp_rsp_iu->fcp_sns_len);

		memcpy(scpnt->sense_buffer,
		       zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu), sns_len);
	}

	if (unlikely(fcp_rsp_iu->validity.bits.fcp_resid_under)) {
		scsi_set_resid(scpnt, fcp_rsp_iu->fcp_resid);
		if (scsi_bufflen(scpnt) - scsi_get_resid(scpnt) <
		    scpnt->underflow)
			set_host_byte(scpnt, DID_ERROR);
	}
skip_fsfstatus:
	if (scpnt->result != 0)
		zfcp_scsi_dbf_event_result("erro", 3, req->adapter, scpnt, req);
	else if (scpnt->retries > 0)
		zfcp_scsi_dbf_event_result("retr", 4, req->adapter, scpnt, req);
	else
		zfcp_scsi_dbf_event_result("norm", 6, req->adapter, scpnt, req);

	scpnt->host_scribble = NULL;
	(scpnt->scsi_done) (scpnt);
	/*
	 * We must hold this lock until scsi_done has been called.
	 * Otherwise we may call scsi_done after abort regarding this
	 * command has completed.
	 * Note: scsi_done must not block!
	 */
	read_unlock_irqrestore(&req->adapter->abort_lock, flags);
}

static void zfcp_fsf_send_fcp_ctm_handler(struct zfcp_fsf_req *req)
{
	struct fcp_rsp_iu *fcp_rsp_iu = (struct fcp_rsp_iu *)
	    &(req->qtcb->bottom.io.fcp_rsp);
	char *fcp_rsp_info = (unsigned char *) &fcp_rsp_iu[1];

	if ((fcp_rsp_info[3] != RSP_CODE_GOOD) ||
	     (req->status & ZFCP_STATUS_FSFREQ_ERROR))
		req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
}


static void zfcp_fsf_send_fcp_command_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_unit *unit;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT))
		unit = req->data;
	else
		unit = req->unit;

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ERROR))
		goto skip_fsfstatus;

	switch (header->fsf_status) {
	case FSF_HANDLE_MISMATCH:
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, "fssfch1", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_FCPLUN_NOT_VALID:
	case FSF_LUN_HANDLE_NOT_VALID:
		zfcp_erp_port_reopen(unit->port, 0, "fssfch2", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		zfcp_fsf_class_not_supp(req);
		break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_unit(req, unit);
		break;
	case FSF_DIRECTION_INDICATOR_NOT_VALID:
		dev_err(&req->adapter->ccw_device->dev,
			"Incorrect direction %d, unit 0x%016Lx on port "
			"0x%016Lx closed\n",
			req->qtcb->bottom.io.data_direction,
			(unsigned long long)unit->fcp_lun,
			(unsigned long long)unit->port->wwpn);
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0, "fssfch3",
					  req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_CMND_LENGTH_NOT_VALID:
		dev_err(&req->adapter->ccw_device->dev,
			"Incorrect CDB length %d, unit 0x%016Lx on "
			"port 0x%016Lx closed\n",
			req->qtcb->bottom.io.fcp_cmnd_length,
			(unsigned long long)unit->fcp_lun,
			(unsigned long long)unit->port->wwpn);
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0, "fssfch4",
					  req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, "fssfch5", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_LUN_BOXED:
		zfcp_erp_unit_boxed(unit, "fssfch6", req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		if (header->fsf_status_qual.word[0] ==
		    FSF_SQ_INVOKE_LINK_TEST_PROCEDURE)
			zfcp_test_link(unit->port);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}
skip_fsfstatus:
	if (req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT)
		zfcp_fsf_send_fcp_ctm_handler(req);
	else {
		zfcp_fsf_send_fcp_command_task_handler(req);
		req->unit = NULL;
		zfcp_unit_put(unit);
	}
}

static void zfcp_set_fcp_dl(struct fcp_cmnd_iu *fcp_cmd, u32 fcp_dl)
{
	u32 *fcp_dl_ptr;

	/*
	 * fcp_dl_addr = start address of fcp_cmnd structure +
	 * size of fixed part + size of dynamically sized add_dcp_cdb field
	 * SEE FCP-2 documentation
	 */
	fcp_dl_ptr = (u32 *) ((unsigned char *) &fcp_cmd[1] +
			(fcp_cmd->add_fcp_cdb_length << 2));
	*fcp_dl_ptr = fcp_dl;
}

/**
 * zfcp_fsf_send_fcp_command_task - initiate an FCP command (for a SCSI command)
 * @unit: unit where command is sent to
 * @scsi_cmnd: scsi command to be sent
 */
int zfcp_fsf_send_fcp_command_task(struct zfcp_unit *unit,
				   struct scsi_cmnd *scsi_cmnd)
{
	struct zfcp_fsf_req *req;
	struct fcp_cmnd_iu *fcp_cmnd_iu;
	unsigned int sbtype = SBAL_FLAGS0_TYPE_READ;
	int real_bytes, retval = -EIO;
	struct zfcp_adapter *adapter = unit->port->adapter;

	if (unlikely(!(atomic_read(&unit->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		return -EBUSY;

	spin_lock(&adapter->req_q_lock);
	if (atomic_read(&adapter->req_q.count) <= 0) {
		atomic_inc(&adapter->qdio_outb_full);
		goto out;
	}
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_FCP_CMND,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_scsi);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	zfcp_unit_get(unit);
	req->unit = unit;
	req->data = scsi_cmnd;
	req->handler = zfcp_fsf_send_fcp_command_handler;
	req->qtcb->header.lun_handle = unit->handle;
	req->qtcb->header.port_handle = unit->port->handle;
	req->qtcb->bottom.io.service_class = FSF_CLASS_3;

	scsi_cmnd->host_scribble = (unsigned char *) req->req_id;

	fcp_cmnd_iu = (struct fcp_cmnd_iu *) &(req->qtcb->bottom.io.fcp_cmnd);
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;
	/*
	 * set depending on data direction:
	 *      data direction bits in SBALE (SB Type)
	 *      data direction bits in QTCB
	 *      data direction bits in FCP_CMND IU
	 */
	switch (scsi_cmnd->sc_data_direction) {
	case DMA_NONE:
		req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
		break;
	case DMA_FROM_DEVICE:
		req->qtcb->bottom.io.data_direction = FSF_DATADIR_READ;
		fcp_cmnd_iu->rddata = 1;
		break;
	case DMA_TO_DEVICE:
		req->qtcb->bottom.io.data_direction = FSF_DATADIR_WRITE;
		sbtype = SBAL_FLAGS0_TYPE_WRITE;
		fcp_cmnd_iu->wddata = 1;
		break;
	case DMA_BIDIRECTIONAL:
		goto failed_scsi_cmnd;
	}

	if (likely((scsi_cmnd->device->simple_tags) ||
		   ((atomic_read(&unit->status) & ZFCP_STATUS_UNIT_READONLY) &&
		    (atomic_read(&unit->status) & ZFCP_STATUS_UNIT_SHARED))))
		fcp_cmnd_iu->task_attribute = SIMPLE_Q;
	else
		fcp_cmnd_iu->task_attribute = UNTAGGED;

	if (unlikely(scsi_cmnd->cmd_len > FCP_CDB_LENGTH))
		fcp_cmnd_iu->add_fcp_cdb_length =
			(scsi_cmnd->cmd_len - FCP_CDB_LENGTH) >> 2;

	memcpy(fcp_cmnd_iu->fcp_cdb, scsi_cmnd->cmnd, scsi_cmnd->cmd_len);

	req->qtcb->bottom.io.fcp_cmnd_length = sizeof(struct fcp_cmnd_iu) +
		fcp_cmnd_iu->add_fcp_cdb_length + sizeof(u32);

	real_bytes = zfcp_qdio_sbals_from_sg(req, sbtype,
					     scsi_sglist(scsi_cmnd),
					     FSF_MAX_SBALS_PER_REQ);
	if (unlikely(real_bytes < 0)) {
		if (req->sbal_number >= FSF_MAX_SBALS_PER_REQ) {
			dev_err(&adapter->ccw_device->dev,
				"Oversize data package, unit 0x%016Lx "
				"on port 0x%016Lx closed\n",
				(unsigned long long)unit->fcp_lun,
				(unsigned long long)unit->port->wwpn);
			zfcp_erp_unit_shutdown(unit, 0, "fssfct1", req);
			retval = -EINVAL;
		}
		goto failed_scsi_cmnd;
	}

	zfcp_set_fcp_dl(fcp_cmnd_iu, real_bytes);

	retval = zfcp_fsf_req_send(req);
	if (unlikely(retval))
		goto failed_scsi_cmnd;

	goto out;

failed_scsi_cmnd:
	zfcp_unit_put(unit);
	zfcp_fsf_req_free(req);
	scsi_cmnd->host_scribble = NULL;
out:
	spin_unlock(&adapter->req_q_lock);
	return retval;
}

/**
 * zfcp_fsf_send_fcp_ctm - send SCSI task management command
 * @unit: pointer to struct zfcp_unit
 * @tm_flags: unsigned byte for task management flags
 * Returns: on success pointer to struct fsf_req, NULL otherwise
 */
struct zfcp_fsf_req *zfcp_fsf_send_fcp_ctm(struct zfcp_unit *unit, u8 tm_flags)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;
	struct fcp_cmnd_iu *fcp_cmnd_iu;
	struct zfcp_adapter *adapter = unit->port->adapter;

	if (unlikely(!(atomic_read(&unit->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		return NULL;

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_FCP_CMND, 0,
				  adapter->pool.fsf_req_scsi);
	if (IS_ERR(req)) {
		req = NULL;
		goto out;
	}

	req->status |= ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT;
	req->data = unit;
	req->handler = zfcp_fsf_send_fcp_command_handler;
	req->qtcb->header.lun_handle = unit->handle;
	req->qtcb->header.port_handle = unit->port->handle;
	req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
	req->qtcb->bottom.io.service_class = FSF_CLASS_3;
	req->qtcb->bottom.io.fcp_cmnd_length = 	sizeof(struct fcp_cmnd_iu) +
						sizeof(u32);

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fcp_cmnd_iu = (struct fcp_cmnd_iu *) &req->qtcb->bottom.io.fcp_cmnd;
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;
	fcp_cmnd_iu->task_management_flags = tm_flags;

	zfcp_fsf_start_timer(req, ZFCP_SCSI_ER_TIMEOUT);
	if (!zfcp_fsf_req_send(req))
		goto out;

	zfcp_fsf_req_free(req);
	req = NULL;
out:
	spin_unlock_bh(&adapter->req_q_lock);
	return req;
}

static void zfcp_fsf_control_file_handler(struct zfcp_fsf_req *req)
{
}

/**
 * zfcp_fsf_control_file - control file upload/download
 * @adapter: pointer to struct zfcp_adapter
 * @fsf_cfdc: pointer to struct zfcp_fsf_cfdc
 * Returns: on success pointer to struct zfcp_fsf_req, NULL otherwise
 */
struct zfcp_fsf_req *zfcp_fsf_control_file(struct zfcp_adapter *adapter,
					   struct zfcp_fsf_cfdc *fsf_cfdc)
{
	struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;
	struct fsf_qtcb_bottom_support *bottom;
	int direction, retval = -EIO, bytes;

	if (!(adapter->adapter_features & FSF_FEATURE_CFDC))
		return ERR_PTR(-EOPNOTSUPP);

	switch (fsf_cfdc->command) {
	case FSF_QTCB_DOWNLOAD_CONTROL_FILE:
		direction = SBAL_FLAGS0_TYPE_WRITE;
		break;
	case FSF_QTCB_UPLOAD_CONTROL_FILE:
		direction = SBAL_FLAGS0_TYPE_READ;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	spin_lock_bh(&adapter->req_q_lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, fsf_cfdc->command, 0, NULL);
	if (IS_ERR(req)) {
		retval = -EPERM;
		goto out;
	}

	req->handler = zfcp_fsf_control_file_handler;

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= direction;

	bottom = &req->qtcb->bottom.support;
	bottom->operation_subtype = FSF_CFDC_OPERATION_SUBTYPE;
	bottom->option = fsf_cfdc->option;

	bytes = zfcp_qdio_sbals_from_sg(req, direction, fsf_cfdc->sg,
					FSF_MAX_SBALS_PER_REQ);
	if (bytes != ZFCP_CFDC_MAX_SIZE) {
		retval = -ENOMEM;
		zfcp_fsf_req_free(req);
		goto out;
	}

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
out:
	spin_unlock_bh(&adapter->req_q_lock);

	if (!retval) {
		wait_event(req->completion_wq,
			   req->status & ZFCP_STATUS_FSFREQ_COMPLETED);
		return req;
	}
	return ERR_PTR(retval);
}
