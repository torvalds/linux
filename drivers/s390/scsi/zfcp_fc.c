/*
 * zfcp device driver
 *
 * Fibre Channel related functions for the zfcp device driver.
 *
 * Copyright IBM Corporation 2008
 */

#include "zfcp_ext.h"

static void _zfcp_fc_incoming_rscn(struct zfcp_fsf_req *fsf_req, u32 range,
				   struct fcp_rscn_element *elem)
{
	unsigned long flags;
	struct zfcp_port *port;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &fsf_req->adapter->port_list_head, list) {
		if (atomic_test_mask(ZFCP_STATUS_PORT_WKA, &port->status))
			continue;
		/* FIXME: ZFCP_STATUS_PORT_DID_DID check is racy */
		if (!atomic_test_mask(ZFCP_STATUS_PORT_DID_DID, &port->status))
			/* Try to connect to unused ports anyway. */
			zfcp_erp_port_reopen(port,
					     ZFCP_STATUS_COMMON_ERP_FAILED,
					     82, fsf_req);
		else if ((port->d_id & range) == (elem->nport_did & range))
			/* Check connection status for connected ports */
			zfcp_test_link(port);
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
}

static void zfcp_fc_incoming_rscn(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *status_buffer = (void *)fsf_req->data;
	struct fcp_rscn_head *fcp_rscn_head;
	struct fcp_rscn_element *fcp_rscn_element;
	u16 i;
	u16 no_entries;
	u32 range_mask;

	fcp_rscn_head = (struct fcp_rscn_head *) status_buffer->payload;
	fcp_rscn_element = (struct fcp_rscn_element *) status_buffer->payload;

	/* see FC-FS */
	no_entries = fcp_rscn_head->payload_len /
			sizeof(struct fcp_rscn_element);

	for (i = 1; i < no_entries; i++) {
		/* skip head and start with 1st element */
		fcp_rscn_element++;
		switch (fcp_rscn_element->addr_format) {
		case ZFCP_PORT_ADDRESS:
			range_mask = ZFCP_PORTS_RANGE_PORT;
			break;
		case ZFCP_AREA_ADDRESS:
			range_mask = ZFCP_PORTS_RANGE_AREA;
			break;
		case ZFCP_DOMAIN_ADDRESS:
			range_mask = ZFCP_PORTS_RANGE_DOMAIN;
			break;
		case ZFCP_FABRIC_ADDRESS:
			range_mask = ZFCP_PORTS_RANGE_FABRIC;
			break;
		default:
			continue;
		}
		_zfcp_fc_incoming_rscn(fsf_req, range_mask, fcp_rscn_element);
	}
}

static void zfcp_fc_incoming_wwpn(struct zfcp_fsf_req *req, wwn_t wwpn)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_port *port;
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list)
		if (port->wwpn == wwpn)
			break;
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (port && (port->wwpn == wwpn))
		zfcp_erp_port_forced_reopen(port, 0, 83, req);
}

static void zfcp_fc_incoming_plogi(struct zfcp_fsf_req *req)
{
	struct fsf_status_read_buffer *status_buffer =
		(struct fsf_status_read_buffer *)req->data;
	struct fsf_plogi *els_plogi =
		(struct fsf_plogi *) status_buffer->payload;

	zfcp_fc_incoming_wwpn(req, els_plogi->serv_param.wwpn);
}

static void zfcp_fc_incoming_logo(struct zfcp_fsf_req *req)
{
	struct fsf_status_read_buffer *status_buffer =
		(struct fsf_status_read_buffer *)req->data;
	struct fcp_logo *els_logo = (struct fcp_logo *) status_buffer->payload;

	zfcp_fc_incoming_wwpn(req, els_logo->nport_wwpn);
}

/**
 * zfcp_fc_incoming_els - handle incoming ELS
 * @fsf_req - request which contains incoming ELS
 */
void zfcp_fc_incoming_els(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *status_buffer =
		(struct fsf_status_read_buffer *) fsf_req->data;
	unsigned int els_type = status_buffer->payload[0];

	zfcp_san_dbf_event_incoming_els(fsf_req);
	if (els_type == LS_PLOGI)
		zfcp_fc_incoming_plogi(fsf_req);
	else if (els_type == LS_LOGO)
		zfcp_fc_incoming_logo(fsf_req);
	else if (els_type == LS_RSCN)
		zfcp_fc_incoming_rscn(fsf_req);
}

static void zfcp_ns_gid_pn_handler(unsigned long data)
{
	struct zfcp_gid_pn_data *gid_pn = (struct zfcp_gid_pn_data *) data;
	struct zfcp_send_ct *ct = &gid_pn->ct;
	struct ct_iu_gid_pn_req *ct_iu_req = sg_virt(ct->req);
	struct ct_iu_gid_pn_resp *ct_iu_resp = sg_virt(ct->resp);
	struct zfcp_port *port = gid_pn->port;

	if (ct->status)
		goto out;
	if (ct_iu_resp->header.cmd_rsp_code != ZFCP_CT_ACCEPT) {
		atomic_set_mask(ZFCP_STATUS_PORT_INVALID_WWPN, &port->status);
		goto out;
	}
	/* paranoia */
	if (ct_iu_req->wwpn != port->wwpn)
		goto out;
	/* looks like a valid d_id */
	port->d_id = ct_iu_resp->d_id & ZFCP_DID_MASK;
	atomic_set_mask(ZFCP_STATUS_PORT_DID_DID, &port->status);
out:
	mempool_free(gid_pn, port->adapter->pool.data_gid_pn);
}

/**
 * zfcp_fc_ns_gid_pn_request - initiate GID_PN nameserver request
 * @erp_action: pointer to zfcp_erp_action where GID_PN request is needed
 * return: -ENOMEM on error, 0 otherwise
 */
int zfcp_fc_ns_gid_pn_request(struct zfcp_erp_action *erp_action)
{
	int ret;
	struct zfcp_gid_pn_data *gid_pn;
	struct zfcp_adapter *adapter = erp_action->adapter;

	gid_pn = mempool_alloc(adapter->pool.data_gid_pn, GFP_ATOMIC);
	if (!gid_pn)
		return -ENOMEM;

	memset(gid_pn, 0, sizeof(*gid_pn));

	/* setup parameters for send generic command */
	gid_pn->port = erp_action->port;
	gid_pn->ct.port = adapter->nameserver_port;
	gid_pn->ct.handler = zfcp_ns_gid_pn_handler;
	gid_pn->ct.handler_data = (unsigned long) gid_pn;
	gid_pn->ct.timeout = ZFCP_NS_GID_PN_TIMEOUT;
	gid_pn->ct.req = &gid_pn->req;
	gid_pn->ct.resp = &gid_pn->resp;
	gid_pn->ct.req_count = 1;
	gid_pn->ct.resp_count = 1;
	sg_init_one(&gid_pn->req, &gid_pn->ct_iu_req,
		    sizeof(struct ct_iu_gid_pn_req));
	sg_init_one(&gid_pn->resp, &gid_pn->ct_iu_resp,
		    sizeof(struct ct_iu_gid_pn_resp));

	/* setup nameserver request */
	gid_pn->ct_iu_req.header.revision = ZFCP_CT_REVISION;
	gid_pn->ct_iu_req.header.gs_type = ZFCP_CT_DIRECTORY_SERVICE;
	gid_pn->ct_iu_req.header.gs_subtype = ZFCP_CT_NAME_SERVER;
	gid_pn->ct_iu_req.header.options = ZFCP_CT_SYNCHRONOUS;
	gid_pn->ct_iu_req.header.cmd_rsp_code = ZFCP_CT_GID_PN;
	gid_pn->ct_iu_req.header.max_res_size = ZFCP_CT_MAX_SIZE;
	gid_pn->ct_iu_req.wwpn = erp_action->port->wwpn;

	ret = zfcp_fsf_send_ct(&gid_pn->ct, adapter->pool.fsf_req_erp,
			       erp_action);
	if (ret)
		mempool_free(gid_pn, adapter->pool.data_gid_pn);
	return ret;
}

/**
 * zfcp_fc_plogi_evaluate - evaluate PLOGI playload
 * @port: zfcp_port structure
 * @plogi: plogi payload
 *
 * Evaluate PLOGI playload and copy important fields into zfcp_port structure
 */
void zfcp_fc_plogi_evaluate(struct zfcp_port *port, struct fsf_plogi *plogi)
{
	port->maxframe_size = plogi->serv_param.common_serv_param[7] |
		((plogi->serv_param.common_serv_param[6] & 0x0F) << 8);
	if (plogi->serv_param.class1_serv_param[0] & 0x80)
		port->supported_classes |= FC_COS_CLASS1;
	if (plogi->serv_param.class2_serv_param[0] & 0x80)
		port->supported_classes |= FC_COS_CLASS2;
	if (plogi->serv_param.class3_serv_param[0] & 0x80)
		port->supported_classes |= FC_COS_CLASS3;
	if (plogi->serv_param.class4_serv_param[0] & 0x80)
		port->supported_classes |= FC_COS_CLASS4;
}

struct zfcp_els_adisc {
	struct zfcp_send_els els;
	struct scatterlist req;
	struct scatterlist resp;
	struct zfcp_ls_adisc ls_adisc;
	struct zfcp_ls_adisc_acc ls_adisc_acc;
};

static void zfcp_fc_adisc_handler(unsigned long data)
{
	struct zfcp_els_adisc *adisc = (struct zfcp_els_adisc *) data;
	struct zfcp_port *port = adisc->els.port;
	struct zfcp_ls_adisc_acc *ls_adisc = &adisc->ls_adisc_acc;

	if (!adisc->els.status) {
		/* request rejected or timed out */
		zfcp_erp_port_forced_reopen(port, 0, 63, NULL);
		goto out;
	}

	if (!port->wwnn)
		port->wwnn = ls_adisc->wwnn;

	if (port->wwpn != ls_adisc->wwpn)
		zfcp_erp_port_reopen(port, 0, 64, NULL);

 out:
	zfcp_port_put(port);
	kfree(adisc);
}

static int zfcp_fc_adisc(struct zfcp_port *port)
{
	struct zfcp_els_adisc *adisc;
	struct zfcp_adapter *adapter = port->adapter;

	adisc = kzalloc(sizeof(struct zfcp_els_adisc), GFP_ATOMIC);
	if (!adisc)
		return -ENOMEM;

	adisc->els.req = &adisc->req;
	adisc->els.resp = &adisc->resp;
	sg_init_one(adisc->els.req, &adisc->ls_adisc,
		    sizeof(struct zfcp_ls_adisc));
	sg_init_one(adisc->els.resp, &adisc->ls_adisc_acc,
		    sizeof(struct zfcp_ls_adisc_acc));

	adisc->els.req_count = 1;
	adisc->els.resp_count = 1;
	adisc->els.adapter = adapter;
	adisc->els.port = port;
	adisc->els.d_id = port->d_id;
	adisc->els.handler = zfcp_fc_adisc_handler;
	adisc->els.handler_data = (unsigned long) adisc;
	adisc->els.ls_code = adisc->ls_adisc.code = ZFCP_LS_ADISC;

	/* acc. to FC-FS, hard_nport_id in ADISC should not be set for ports
	   without FC-AL-2 capability, so we don't set it */
	adisc->ls_adisc.wwpn = fc_host_port_name(adapter->scsi_host);
	adisc->ls_adisc.wwnn = fc_host_node_name(adapter->scsi_host);
	adisc->ls_adisc.nport_id = fc_host_port_id(adapter->scsi_host);

	return zfcp_fsf_send_els(&adisc->els);
}

/**
 * zfcp_test_link - lightweight link test procedure
 * @port: port to be tested
 *
 * Test status of a link to a remote port using the ELS command ADISC.
 * If there is a problem with the remote port, error recovery steps
 * will be triggered.
 */
void zfcp_test_link(struct zfcp_port *port)
{
	int retval;

	zfcp_port_get(port);
	retval = zfcp_fc_adisc(port);
	if (retval == 0 || retval == -EBUSY)
		return;

	/* send of ADISC was not possible */
	zfcp_port_put(port);
	zfcp_erp_port_forced_reopen(port, 0, 65, NULL);
}
