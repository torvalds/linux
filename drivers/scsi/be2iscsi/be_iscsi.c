/**
 * Copyright (C) 2005 - 2010 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohank@serverengines.com)
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 *
 */

#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>

#include "be_iscsi.h"

extern struct iscsi_transport beiscsi_iscsi_transport;

/**
 * beiscsi_session_create - creates a new iscsi session
 * @cmds_max: max commands supported
 * @qdepth: max queue depth supported
 * @initial_cmdsn: initial iscsi CMDSN
 */
struct iscsi_cls_session *beiscsi_session_create(struct iscsi_endpoint *ep,
						 u16 cmds_max,
						 u16 qdepth,
						 u32 initial_cmdsn)
{
	struct Scsi_Host *shost;
	struct beiscsi_endpoint *beiscsi_ep;
	struct iscsi_cls_session *cls_session;
	struct beiscsi_hba *phba;
	struct iscsi_session *sess;
	struct beiscsi_session *beiscsi_sess;
	struct beiscsi_io_task *io_task;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_session_create\n");

	if (!ep) {
		SE_DEBUG(DBG_LVL_1, "beiscsi_session_create: invalid ep\n");
		return NULL;
	}
	beiscsi_ep = ep->dd_data;
	phba = beiscsi_ep->phba;
	shost = phba->shost;
	if (cmds_max > beiscsi_ep->phba->params.wrbs_per_cxn) {
		shost_printk(KERN_ERR, shost, "Cannot handle %d cmds."
			     "Max cmds per session supported is %d. Using %d. "
			     "\n", cmds_max,
			      beiscsi_ep->phba->params.wrbs_per_cxn,
			      beiscsi_ep->phba->params.wrbs_per_cxn);
		cmds_max = beiscsi_ep->phba->params.wrbs_per_cxn;
	}

	cls_session = iscsi_session_setup(&beiscsi_iscsi_transport,
					  shost, cmds_max,
					  sizeof(*beiscsi_sess),
					  sizeof(*io_task),
					  initial_cmdsn, ISCSI_MAX_TARGET);
	if (!cls_session)
		return NULL;
	sess = cls_session->dd_data;
	beiscsi_sess = sess->dd_data;
	beiscsi_sess->bhs_pool =  pci_pool_create("beiscsi_bhs_pool",
						   phba->pcidev,
						   sizeof(struct be_cmd_bhs),
						   64, 0);
	if (!beiscsi_sess->bhs_pool)
		goto destroy_sess;

	return cls_session;
destroy_sess:
	iscsi_session_teardown(cls_session);
	return NULL;
}

/**
 * beiscsi_session_destroy - destroys iscsi session
 * @cls_session:	pointer to iscsi cls session
 *
 * Destroys iSCSI session instance and releases
 * resources allocated for it.
 */
void beiscsi_session_destroy(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *sess = cls_session->dd_data;
	struct beiscsi_session *beiscsi_sess = sess->dd_data;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_session_destroy\n");
	pci_pool_destroy(beiscsi_sess->bhs_pool);
	iscsi_session_teardown(cls_session);
}

/**
 * beiscsi_conn_create - create an instance of iscsi connection
 * @cls_session: ptr to iscsi_cls_session
 * @cid: iscsi cid
 */
struct iscsi_cls_conn *
beiscsi_conn_create(struct iscsi_cls_session *cls_session, u32 cid)
{
	struct beiscsi_hba *phba;
	struct Scsi_Host *shost;
	struct iscsi_cls_conn *cls_conn;
	struct beiscsi_conn *beiscsi_conn;
	struct iscsi_conn *conn;
	struct iscsi_session *sess;
	struct beiscsi_session *beiscsi_sess;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_conn_create ,cid"
		 "from iscsi layer=%d\n", cid);
	shost = iscsi_session_to_shost(cls_session);
	phba = iscsi_host_priv(shost);

	cls_conn = iscsi_conn_setup(cls_session, sizeof(*beiscsi_conn), cid);
	if (!cls_conn)
		return NULL;

	conn = cls_conn->dd_data;
	beiscsi_conn = conn->dd_data;
	beiscsi_conn->ep = NULL;
	beiscsi_conn->phba = phba;
	beiscsi_conn->conn = conn;
	sess = cls_session->dd_data;
	beiscsi_sess = sess->dd_data;
	beiscsi_conn->beiscsi_sess = beiscsi_sess;
	return cls_conn;
}

/**
 * beiscsi_bindconn_cid - Bind the beiscsi_conn with phba connection table
 * @beiscsi_conn: The pointer to  beiscsi_conn structure
 * @phba: The phba instance
 * @cid: The cid to free
 */
static int beiscsi_bindconn_cid(struct beiscsi_hba *phba,
				struct beiscsi_conn *beiscsi_conn,
				unsigned int cid)
{
	if (phba->conn_table[cid]) {
		SE_DEBUG(DBG_LVL_1,
			 "Connection table already occupied. Detected clash\n");
		return -EINVAL;
	} else {
		SE_DEBUG(DBG_LVL_8, "phba->conn_table[%d]=%p(beiscsi_conn)\n",
			 cid, beiscsi_conn);
		phba->conn_table[cid] = beiscsi_conn;
	}
	return 0;
}

/**
 * beiscsi_conn_bind - Binds iscsi session/connection with TCP connection
 * @cls_session: pointer to iscsi cls session
 * @cls_conn: pointer to iscsi cls conn
 * @transport_fd: EP handle(64 bit)
 *
 * This function binds the TCP Conn with iSCSI Connection and Session.
 */
int beiscsi_conn_bind(struct iscsi_cls_session *cls_session,
		      struct iscsi_cls_conn *cls_conn,
		      u64 transport_fd, int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct Scsi_Host *shost =
		(struct Scsi_Host *)iscsi_session_to_shost(cls_session);
	struct beiscsi_hba *phba = (struct beiscsi_hba *)iscsi_host_priv(shost);
	struct beiscsi_endpoint *beiscsi_ep;
	struct iscsi_endpoint *ep;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_conn_bind\n");
	ep = iscsi_lookup_endpoint(transport_fd);
	if (!ep)
		return -EINVAL;

	beiscsi_ep = ep->dd_data;

	if (iscsi_conn_bind(cls_session, cls_conn, is_leading))
		return -EINVAL;

	if (beiscsi_ep->phba != phba) {
		SE_DEBUG(DBG_LVL_8,
			 "beiscsi_ep->hba=%p not equal to phba=%p\n",
			 beiscsi_ep->phba, phba);
		return -EEXIST;
	}

	beiscsi_conn->beiscsi_conn_cid = beiscsi_ep->ep_cid;
	beiscsi_conn->ep = beiscsi_ep;
	beiscsi_ep->conn = beiscsi_conn;
	SE_DEBUG(DBG_LVL_8, "beiscsi_conn=%p conn=%p ep_cid=%d\n",
		 beiscsi_conn, conn, beiscsi_ep->ep_cid);
	return beiscsi_bindconn_cid(phba, beiscsi_conn, beiscsi_ep->ep_cid);
}

/**
 * beiscsi_conn_get_param - get the iscsi parameter
 * @cls_conn: pointer to iscsi cls conn
 * @param: parameter type identifier
 * @buf: buffer pointer
 *
 * returns iscsi parameter
 */
int beiscsi_conn_get_param(struct iscsi_cls_conn *cls_conn,
			   enum iscsi_param param, char *buf)
{
	struct beiscsi_endpoint *beiscsi_ep;
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	int len = 0;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_conn_get_param, param= %d\n", param);
	beiscsi_ep = beiscsi_conn->ep;
	if (!beiscsi_ep) {
		SE_DEBUG(DBG_LVL_1,
			 "In beiscsi_conn_get_param , no beiscsi_ep\n");
		return -ENODEV;
	}

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
		len = sprintf(buf, "%hu\n", beiscsi_ep->dst_tcpport);
		break;
	case ISCSI_PARAM_CONN_ADDRESS:
		if (beiscsi_ep->ip_type == BE2_IPV4)
			len = sprintf(buf, "%pI4\n", &beiscsi_ep->dst_addr);
		else
			len = sprintf(buf, "%pI6\n", &beiscsi_ep->dst6_addr);
		break;
	default:
		return iscsi_conn_get_param(cls_conn, param, buf);
	}
	return len;
}

int beiscsi_set_param(struct iscsi_cls_conn *cls_conn,
		      enum iscsi_param param, char *buf, int buflen)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	int ret;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_conn_set_param, param= %d\n", param);
	ret = iscsi_set_param(cls_conn, param, buf, buflen);
	if (ret)
		return ret;
	/*
	 * If userspace tried to set the value to higher than we can
	 * support override here.
	 */
	switch (param) {
	case ISCSI_PARAM_FIRST_BURST:
		if (session->first_burst > 8192)
			session->first_burst = 8192;
		break;
	case ISCSI_PARAM_MAX_RECV_DLENGTH:
		if (conn->max_recv_dlength > 65536)
			conn->max_recv_dlength = 65536;
		break;
	case ISCSI_PARAM_MAX_BURST:
		if (session->max_burst > 262144)
			session->max_burst = 262144;
		break;
	case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		if ((conn->max_xmit_dlength > 65536) ||
		    (conn->max_xmit_dlength == 0))
			conn->max_xmit_dlength = 65536;
	default:
		return 0;
	}

	return 0;
}

/**
 * beiscsi_get_host_param - get the iscsi parameter
 * @shost: pointer to scsi_host structure
 * @param: parameter type identifier
 * @buf: buffer pointer
 *
 * returns host parameter
 */
int beiscsi_get_host_param(struct Scsi_Host *shost,
			   enum iscsi_host_param param, char *buf)
{
	struct beiscsi_hba *phba = (struct beiscsi_hba *)iscsi_host_priv(shost);
	int status = 0;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_get_host_param, param= %d\n", param);
	switch (param) {
	case ISCSI_HOST_PARAM_HWADDRESS:
		status = beiscsi_get_macaddr(buf, phba);
		if (status < 0) {
			SE_DEBUG(DBG_LVL_1, "beiscsi_get_macaddr Failed\n");
			return status;
		}
		break;
	default:
		return iscsi_host_get_param(shost, param, buf);
	}
	return status;
}

int beiscsi_get_macaddr(char *buf, struct beiscsi_hba *phba)
{
	struct be_cmd_resp_get_mac_addr *resp;
	struct be_mcc_wrb *wrb;
	unsigned int tag, wrb_num;
	unsigned short status, extd_status;
	struct be_queue_info *mccq = &phba->ctrl.mcc_obj.q;
	int rc;

	if (phba->read_mac_address)
		return sysfs_format_mac(buf, phba->mac_address,
					ETH_ALEN);

	tag = be_cmd_get_mac_addr(phba);
	if (!tag) {
		SE_DEBUG(DBG_LVL_1, "be_cmd_get_mac_addr Failed\n");
		return -EBUSY;
	} else
		wait_event_interruptible(phba->ctrl.mcc_wait[tag],
					 phba->ctrl.mcc_numtag[tag]);

	wrb_num = (phba->ctrl.mcc_numtag[tag] & 0x00FF0000) >> 16;
	extd_status = (phba->ctrl.mcc_numtag[tag] & 0x0000FF00) >> 8;
	status = phba->ctrl.mcc_numtag[tag] & 0x000000FF;
	if (status || extd_status) {
		SE_DEBUG(DBG_LVL_1, "Failed to get be_cmd_get_mac_addr"
				    " status = %d extd_status = %d\n",
				    status, extd_status);
		free_mcc_tag(&phba->ctrl, tag);
		return -EAGAIN;
	}
	wrb = queue_get_wrb(mccq, wrb_num);
	free_mcc_tag(&phba->ctrl, tag);
	resp = embedded_payload(wrb);
	memcpy(phba->mac_address, resp->mac_address, ETH_ALEN);
	rc = sysfs_format_mac(buf, phba->mac_address,
			       ETH_ALEN);
	phba->read_mac_address = 1;
	return rc;
}


/**
 * beiscsi_conn_get_stats - get the iscsi stats
 * @cls_conn: pointer to iscsi cls conn
 * @stats: pointer to iscsi_stats structure
 *
 * returns iscsi stats
 */
void beiscsi_conn_get_stats(struct iscsi_cls_conn *cls_conn,
			    struct iscsi_stats *stats)
{
	struct iscsi_conn *conn = cls_conn->dd_data;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_conn_get_stats\n");
	stats->txdata_octets = conn->txdata_octets;
	stats->rxdata_octets = conn->rxdata_octets;
	stats->dataout_pdus = conn->dataout_pdus_cnt;
	stats->scsirsp_pdus = conn->scsirsp_pdus_cnt;
	stats->scsicmd_pdus = conn->scsicmd_pdus_cnt;
	stats->datain_pdus = conn->datain_pdus_cnt;
	stats->tmfrsp_pdus = conn->tmfrsp_pdus_cnt;
	stats->tmfcmd_pdus = conn->tmfcmd_pdus_cnt;
	stats->r2t_pdus = conn->r2t_pdus_cnt;
	stats->digest_err = 0;
	stats->timeout_err = 0;
	stats->custom_length = 0;
	strcpy(stats->custom[0].desc, "eh_abort_cnt");
	stats->custom[0].value = conn->eh_abort_cnt;
}

/**
 * beiscsi_set_params_for_offld - get the parameters for offload
 * @beiscsi_conn: pointer to beiscsi_conn
 * @params: pointer to offload_params structure
 */
static void  beiscsi_set_params_for_offld(struct beiscsi_conn *beiscsi_conn,
					  struct beiscsi_offload_params *params)
{
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct iscsi_session *session = conn->session;

	AMAP_SET_BITS(struct amap_beiscsi_offload_params, max_burst_length,
		      params, session->max_burst);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params,
		      max_send_data_segment_length, params,
		      conn->max_xmit_dlength);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params, first_burst_length,
		      params, session->first_burst);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params, erl, params,
		      session->erl);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params, dde, params,
		      conn->datadgst_en);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params, hde, params,
		      conn->hdrdgst_en);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params, ir2t, params,
		      session->initial_r2t_en);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params, imd, params,
		      session->imm_data_en);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params, exp_statsn, params,
		      (conn->exp_statsn - 1));
}

/**
 * beiscsi_conn_start - offload of session to chip
 * @cls_conn: pointer to beiscsi_conn
 */
int beiscsi_conn_start(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_endpoint *beiscsi_ep;
	struct beiscsi_offload_params params;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_conn_start\n");
	memset(&params, 0, sizeof(struct beiscsi_offload_params));
	beiscsi_ep = beiscsi_conn->ep;
	if (!beiscsi_ep)
		SE_DEBUG(DBG_LVL_1, "In beiscsi_conn_start , no beiscsi_ep\n");

	beiscsi_conn->login_in_progress = 0;
	beiscsi_set_params_for_offld(beiscsi_conn, &params);
	beiscsi_offload_connection(beiscsi_conn, &params);
	iscsi_conn_start(cls_conn);
	return 0;
}

/**
 * beiscsi_get_cid - Allocate a cid
 * @phba: The phba instance
 */
static int beiscsi_get_cid(struct beiscsi_hba *phba)
{
	unsigned short cid = 0xFFFF;

	if (!phba->avlbl_cids)
		return cid;

	cid = phba->cid_array[phba->cid_alloc++];
	if (phba->cid_alloc == phba->params.cxns_per_ctrl)
		phba->cid_alloc = 0;
	phba->avlbl_cids--;
	return cid;
}

/**
 * beiscsi_put_cid - Free the cid
 * @phba: The phba for which the cid is being freed
 * @cid: The cid to free
 */
static void beiscsi_put_cid(struct beiscsi_hba *phba, unsigned short cid)
{
	phba->avlbl_cids++;
	phba->cid_array[phba->cid_free++] = cid;
	if (phba->cid_free == phba->params.cxns_per_ctrl)
		phba->cid_free = 0;
}

/**
 * beiscsi_free_ep - free endpoint
 * @ep:	pointer to iscsi endpoint structure
 */
static void beiscsi_free_ep(struct beiscsi_endpoint *beiscsi_ep)
{
	struct beiscsi_hba *phba = beiscsi_ep->phba;

	beiscsi_put_cid(phba, beiscsi_ep->ep_cid);
	beiscsi_ep->phba = NULL;
}

/**
 * beiscsi_open_conn - Ask FW to open a TCP connection
 * @ep:	endpoint to be used
 * @src_addr: The source IP address
 * @dst_addr: The Destination  IP address
 *
 * Asks the FW to open a TCP connection
 */
static int beiscsi_open_conn(struct iscsi_endpoint *ep,
			     struct sockaddr *src_addr,
			     struct sockaddr *dst_addr, int non_blocking)
{
	struct beiscsi_endpoint *beiscsi_ep = ep->dd_data;
	struct beiscsi_hba *phba = beiscsi_ep->phba;
	struct be_queue_info *mccq = &phba->ctrl.mcc_obj.q;
	struct be_mcc_wrb *wrb;
	struct tcp_connect_and_offload_out *ptcpcnct_out;
	unsigned short status, extd_status;
	struct be_dma_mem nonemb_cmd;
	unsigned int tag, wrb_num;
	int ret = -ENOMEM;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_open_conn\n");
	beiscsi_ep->ep_cid = beiscsi_get_cid(phba);
	if (beiscsi_ep->ep_cid == 0xFFFF) {
		SE_DEBUG(DBG_LVL_1, "No free cid available\n");
		return ret;
	}
	SE_DEBUG(DBG_LVL_8, "In beiscsi_open_conn, ep_cid=%d\n",
		 beiscsi_ep->ep_cid);
	phba->ep_array[beiscsi_ep->ep_cid -
		       phba->fw_config.iscsi_cid_start] = ep;
	if (beiscsi_ep->ep_cid > (phba->fw_config.iscsi_cid_start +
				  phba->params.cxns_per_ctrl * 2)) {
		SE_DEBUG(DBG_LVL_1, "Failed in allocate iscsi cid\n");
		beiscsi_put_cid(phba, beiscsi_ep->ep_cid);
		goto free_ep;
	}

	beiscsi_ep->cid_vld = 0;
	nonemb_cmd.va = pci_alloc_consistent(phba->ctrl.pdev,
				sizeof(struct tcp_connect_and_offload_in),
				&nonemb_cmd.dma);
	if (nonemb_cmd.va == NULL) {
		SE_DEBUG(DBG_LVL_1,
			 "Failed to allocate memory for mgmt_open_connection"
			 "\n");
		beiscsi_put_cid(phba, beiscsi_ep->ep_cid);
		return -ENOMEM;
	}
	nonemb_cmd.size = sizeof(struct tcp_connect_and_offload_in);
	memset(nonemb_cmd.va, 0, nonemb_cmd.size);
	tag = mgmt_open_connection(phba, dst_addr, beiscsi_ep, &nonemb_cmd);
	if (!tag) {
		SE_DEBUG(DBG_LVL_1,
			 "mgmt_open_connection Failed for cid=%d\n",
			 beiscsi_ep->ep_cid);
		beiscsi_put_cid(phba, beiscsi_ep->ep_cid);
		pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
				    nonemb_cmd.va, nonemb_cmd.dma);
		return -EAGAIN;
	} else {
		wait_event_interruptible(phba->ctrl.mcc_wait[tag],
					 phba->ctrl.mcc_numtag[tag]);
	}
	wrb_num = (phba->ctrl.mcc_numtag[tag] & 0x00FF0000) >> 16;
	extd_status = (phba->ctrl.mcc_numtag[tag] & 0x0000FF00) >> 8;
	status = phba->ctrl.mcc_numtag[tag] & 0x000000FF;
	if (status || extd_status) {
		SE_DEBUG(DBG_LVL_1, "mgmt_open_connection Failed"
				    " status = %d extd_status = %d\n",
				    status, extd_status);
		beiscsi_put_cid(phba, beiscsi_ep->ep_cid);
		free_mcc_tag(&phba->ctrl, tag);
		pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
			    nonemb_cmd.va, nonemb_cmd.dma);
		goto free_ep;
	} else {
		wrb = queue_get_wrb(mccq, wrb_num);
		free_mcc_tag(&phba->ctrl, tag);

		ptcpcnct_out = embedded_payload(wrb);
		beiscsi_ep = ep->dd_data;
		beiscsi_ep->fw_handle = ptcpcnct_out->connection_handle;
		beiscsi_ep->cid_vld = 1;
		SE_DEBUG(DBG_LVL_8, "mgmt_open_connection Success\n");
	}
	beiscsi_put_cid(phba, beiscsi_ep->ep_cid);
	pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
			    nonemb_cmd.va, nonemb_cmd.dma);
	return 0;

free_ep:
	beiscsi_free_ep(beiscsi_ep);
	return -EBUSY;
}

/**
 * beiscsi_ep_connect - Ask chip to create TCP Conn
 * @scsi_host: Pointer to scsi_host structure
 * @dst_addr: The IP address of Target
 * @non_blocking: blocking or non-blocking call
 *
 * This routines first asks chip to create a connection and then allocates an EP
 */
struct iscsi_endpoint *
beiscsi_ep_connect(struct Scsi_Host *shost, struct sockaddr *dst_addr,
		   int non_blocking)
{
	struct beiscsi_hba *phba;
	struct beiscsi_endpoint *beiscsi_ep;
	struct iscsi_endpoint *ep;
	int ret;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_ep_connect\n");
	if (shost)
		phba = iscsi_host_priv(shost);
	else {
		ret = -ENXIO;
		SE_DEBUG(DBG_LVL_1, "shost is NULL\n");
		return ERR_PTR(ret);
	}

	if (phba->state != BE_ADAPTER_UP) {
		ret = -EBUSY;
		SE_DEBUG(DBG_LVL_1, "The Adapter state is Not UP\n");
		return ERR_PTR(ret);
	}

	ep = iscsi_create_endpoint(sizeof(struct beiscsi_endpoint));
	if (!ep) {
		ret = -ENOMEM;
		return ERR_PTR(ret);
	}

	beiscsi_ep = ep->dd_data;
	beiscsi_ep->phba = phba;
	beiscsi_ep->openiscsi_ep = ep;
	ret = beiscsi_open_conn(ep, NULL, dst_addr, non_blocking);
	if (ret) {
		SE_DEBUG(DBG_LVL_1, "Failed in beiscsi_open_conn\n");
		goto free_ep;
	}

	return ep;

free_ep:
	iscsi_destroy_endpoint(ep);
	return ERR_PTR(ret);
}

/**
 * beiscsi_ep_poll - Poll to see if connection is established
 * @ep:	endpoint to be used
 * @timeout_ms: timeout specified in millisecs
 *
 * Poll to see if TCP connection established
 */
int beiscsi_ep_poll(struct iscsi_endpoint *ep, int timeout_ms)
{
	struct beiscsi_endpoint *beiscsi_ep = ep->dd_data;

	SE_DEBUG(DBG_LVL_8, "In  beiscsi_ep_poll\n");
	if (beiscsi_ep->cid_vld == 1)
		return 1;
	else
		return 0;
}

/**
 * beiscsi_close_conn - Upload the  connection
 * @ep: The iscsi endpoint
 * @flag: The type of connection closure
 */
static int beiscsi_close_conn(struct  beiscsi_endpoint *beiscsi_ep, int flag)
{
	int ret = 0;
	unsigned int tag;
	struct beiscsi_hba *phba = beiscsi_ep->phba;

	tag = mgmt_upload_connection(phba, beiscsi_ep->ep_cid, flag);
	if (!tag) {
		SE_DEBUG(DBG_LVL_8, "upload failed for cid 0x%x\n",
			 beiscsi_ep->ep_cid);
		ret = -EAGAIN;
	} else {
		wait_event_interruptible(phba->ctrl.mcc_wait[tag],
					 phba->ctrl.mcc_numtag[tag]);
		free_mcc_tag(&phba->ctrl, tag);
	}
	return ret;
}

/**
 * beiscsi_unbind_conn_to_cid - Unbind the beiscsi_conn from phba conn table
 * @phba: The phba instance
 * @cid: The cid to free
 */
static int beiscsi_unbind_conn_to_cid(struct beiscsi_hba *phba,
				      unsigned int cid)
{
	if (phba->conn_table[cid])
		phba->conn_table[cid] = NULL;
	else {
		SE_DEBUG(DBG_LVL_8, "Connection table Not occupied.\n");
		return -EINVAL;
	}
	return 0;
}

/**
 * beiscsi_ep_disconnect - Tears down the TCP connection
 * @ep:	endpoint to be used
 *
 * Tears down the TCP connection
 */
void beiscsi_ep_disconnect(struct iscsi_endpoint *ep)
{
	struct beiscsi_conn *beiscsi_conn;
	struct beiscsi_endpoint *beiscsi_ep;
	struct beiscsi_hba *phba;
	unsigned int tag;
	unsigned short savecfg_flag = CMD_ISCSI_SESSION_SAVE_CFG_ON_FLASH;

	beiscsi_ep = ep->dd_data;
	phba = beiscsi_ep->phba;
	SE_DEBUG(DBG_LVL_8, "In beiscsi_ep_disconnect for ep_cid = %d\n",
			     beiscsi_ep->ep_cid);

	if (!beiscsi_ep->conn) {
		SE_DEBUG(DBG_LVL_8, "In beiscsi_ep_disconnect, no "
			 "beiscsi_ep\n");
		return;
	}
	beiscsi_conn = beiscsi_ep->conn;
	iscsi_suspend_queue(beiscsi_conn->conn);

	SE_DEBUG(DBG_LVL_8, "In beiscsi_ep_disconnect ep_cid = %d\n",
		 beiscsi_ep->ep_cid);

	tag = mgmt_invalidate_connection(phba, beiscsi_ep,
					    beiscsi_ep->ep_cid, 1,
					    savecfg_flag);
	if (!tag) {
		SE_DEBUG(DBG_LVL_1,
			 "mgmt_invalidate_connection Failed for cid=%d\n",
			  beiscsi_ep->ep_cid);
	} else {
		wait_event_interruptible(phba->ctrl.mcc_wait[tag],
					 phba->ctrl.mcc_numtag[tag]);
		free_mcc_tag(&phba->ctrl, tag);
	}

	beiscsi_close_conn(beiscsi_ep, CONNECTION_UPLOAD_GRACEFUL);
	beiscsi_free_ep(beiscsi_ep);
	beiscsi_unbind_conn_to_cid(phba, beiscsi_ep->ep_cid);
	iscsi_destroy_endpoint(beiscsi_ep->openiscsi_ep);
}
