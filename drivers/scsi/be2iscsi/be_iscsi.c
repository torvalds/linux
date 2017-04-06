/**
 * Copyright (C) 2005 - 2016 Broadcom
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohan.kallickal@broadcom.com)
 *
 * Contact Information:
 * linux-drivers@broadcom.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_netlink.h>
#include <net/netlink.h>
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


	if (!ep) {
		pr_err("beiscsi_session_create: invalid ep\n");
		return NULL;
	}
	beiscsi_ep = ep->dd_data;
	phba = beiscsi_ep->phba;

	if (!beiscsi_hba_is_online(phba)) {
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BS_%d : HBA in error 0x%lx\n", phba->state);
		return NULL;
	}

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_session_create\n");
	if (cmds_max > beiscsi_ep->phba->params.wrbs_per_cxn) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : Cannot handle %d cmds."
			    "Max cmds per session supported is %d. Using %d."
			    "\n", cmds_max,
			    beiscsi_ep->phba->params.wrbs_per_cxn,
			    beiscsi_ep->phba->params.wrbs_per_cxn);

		cmds_max = beiscsi_ep->phba->params.wrbs_per_cxn;
	}

	shost = phba->shost;
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

	printk(KERN_INFO "In beiscsi_session_destroy\n");
	pci_pool_destroy(beiscsi_sess->bhs_pool);
	iscsi_session_teardown(cls_session);
}

/**
 * beiscsi_session_fail(): Closing session with appropriate error
 * @cls_session: ptr to session
 **/
void beiscsi_session_fail(struct iscsi_cls_session *cls_session)
{
	iscsi_session_failure(cls_session->dd_data, ISCSI_ERR_CONN_FAILED);
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

	shost = iscsi_session_to_shost(cls_session);
	phba = iscsi_host_priv(shost);

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_conn_create ,cid"
		    "from iscsi layer=%d\n", cid);

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
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);
	struct beiscsi_hba *phba = iscsi_host_priv(shost);
	struct hwi_controller *phwi_ctrlr = phba->phwi_ctrlr;
	struct hwi_wrb_context *pwrb_context;
	struct beiscsi_endpoint *beiscsi_ep;
	struct iscsi_endpoint *ep;
	uint16_t cri_index;

	ep = iscsi_lookup_endpoint(transport_fd);
	if (!ep)
		return -EINVAL;

	beiscsi_ep = ep->dd_data;

	if (iscsi_conn_bind(cls_session, cls_conn, is_leading))
		return -EINVAL;

	if (beiscsi_ep->phba != phba) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : beiscsi_ep->hba=%p not equal to phba=%p\n",
			    beiscsi_ep->phba, phba);

		return -EEXIST;
	}
	cri_index = BE_GET_CRI_FROM_CID(beiscsi_ep->ep_cid);
	if (phba->conn_table[cri_index]) {
		if (beiscsi_conn != phba->conn_table[cri_index] ||
		    beiscsi_ep != phba->conn_table[cri_index]->ep) {
			__beiscsi_log(phba, KERN_ERR,
				      "BS_%d : conn_table not empty at %u: cid %u conn %p:%p\n",
				      cri_index,
				      beiscsi_ep->ep_cid,
				      beiscsi_conn,
				      phba->conn_table[cri_index]);
			return -EINVAL;
		}
	}

	beiscsi_conn->beiscsi_conn_cid = beiscsi_ep->ep_cid;
	beiscsi_conn->ep = beiscsi_ep;
	beiscsi_ep->conn = beiscsi_conn;
	/**
	 * Each connection is associated with a WRBQ kept in wrb_context.
	 * Store doorbell offset for transmit path.
	 */
	pwrb_context = &phwi_ctrlr->wrb_context[cri_index];
	beiscsi_conn->doorbell_offset = pwrb_context->doorbell_offset;
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : cid %d phba->conn_table[%u]=%p\n",
		    beiscsi_ep->ep_cid, cri_index, beiscsi_conn);
	phba->conn_table[cri_index] = beiscsi_conn;
	return 0;
}

static int beiscsi_iface_create_ipv4(struct beiscsi_hba *phba)
{
	if (phba->ipv4_iface)
		return 0;

	phba->ipv4_iface = iscsi_create_iface(phba->shost,
					      &beiscsi_iscsi_transport,
					      ISCSI_IFACE_TYPE_IPV4,
					      0, 0);
	if (!phba->ipv4_iface) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : Could not "
			    "create default IPv4 address.\n");
		return -ENODEV;
	}

	return 0;
}

static int beiscsi_iface_create_ipv6(struct beiscsi_hba *phba)
{
	if (phba->ipv6_iface)
		return 0;

	phba->ipv6_iface = iscsi_create_iface(phba->shost,
					      &beiscsi_iscsi_transport,
					      ISCSI_IFACE_TYPE_IPV6,
					      0, 0);
	if (!phba->ipv6_iface) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : Could not "
			    "create default IPv6 address.\n");
		return -ENODEV;
	}

	return 0;
}

void beiscsi_iface_create_default(struct beiscsi_hba *phba)
{
	struct be_cmd_get_if_info_resp *if_info;

	if (!beiscsi_if_get_info(phba, BEISCSI_IP_TYPE_V4, &if_info)) {
		beiscsi_iface_create_ipv4(phba);
		kfree(if_info);
	}

	if (!beiscsi_if_get_info(phba, BEISCSI_IP_TYPE_V6, &if_info)) {
		beiscsi_iface_create_ipv6(phba);
		kfree(if_info);
	}
}

void beiscsi_iface_destroy_default(struct beiscsi_hba *phba)
{
	if (phba->ipv6_iface) {
		iscsi_destroy_iface(phba->ipv6_iface);
		phba->ipv6_iface = NULL;
	}
	if (phba->ipv4_iface) {
		iscsi_destroy_iface(phba->ipv4_iface);
		phba->ipv4_iface = NULL;
	}
}

/**
 * beiscsi_set_vlan_tag()- Set the VLAN TAG
 * @shost: Scsi Host for the driver instance
 * @iface_param: Interface paramters
 *
 * Set the VLAN TAG for the adapter or disable
 * the VLAN config
 *
 * returns
 *	Success: 0
 *	Failure: Non-Zero Value
 **/
static int
beiscsi_iface_config_vlan(struct Scsi_Host *shost,
			  struct iscsi_iface_param_info *iface_param)
{
	struct beiscsi_hba *phba = iscsi_host_priv(shost);
	int ret = -EPERM;

	switch (iface_param->param) {
	case ISCSI_NET_PARAM_VLAN_ENABLED:
		ret = 0;
		if (iface_param->value[0] != ISCSI_VLAN_ENABLE)
			ret = beiscsi_if_set_vlan(phba, BEISCSI_VLAN_DISABLE);
		break;
	case ISCSI_NET_PARAM_VLAN_TAG:
		ret = beiscsi_if_set_vlan(phba,
					  *((uint16_t *)iface_param->value));
		break;
	}
	return ret;
}


static int
beiscsi_iface_config_ipv4(struct Scsi_Host *shost,
			  struct iscsi_iface_param_info *info,
			  void *data, uint32_t dt_len)
{
	struct beiscsi_hba *phba = iscsi_host_priv(shost);
	u8 *ip = NULL, *subnet = NULL, *gw;
	struct nlattr *nla;
	int ret = -EPERM;

	/* Check the param */
	switch (info->param) {
	case ISCSI_NET_PARAM_IFACE_ENABLE:
		if (info->value[0] == ISCSI_IFACE_ENABLE)
			ret = beiscsi_iface_create_ipv4(phba);
		else {
			iscsi_destroy_iface(phba->ipv4_iface);
			phba->ipv4_iface = NULL;
		}
		break;
	case ISCSI_NET_PARAM_IPV4_GW:
		gw = info->value;
		ret = beiscsi_if_set_gw(phba, BEISCSI_IP_TYPE_V4, gw);
		break;
	case ISCSI_NET_PARAM_IPV4_BOOTPROTO:
		if (info->value[0] == ISCSI_BOOTPROTO_DHCP)
			ret = beiscsi_if_en_dhcp(phba, BEISCSI_IP_TYPE_V4);
		else if (info->value[0] == ISCSI_BOOTPROTO_STATIC)
			/* release DHCP IP address */
			ret = beiscsi_if_en_static(phba, BEISCSI_IP_TYPE_V4,
						   NULL, NULL);
		else
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
				    "BS_%d : Invalid BOOTPROTO: %d\n",
				    info->value[0]);
		break;
	case ISCSI_NET_PARAM_IPV4_ADDR:
		ip = info->value;
		nla = nla_find(data, dt_len, ISCSI_NET_PARAM_IPV4_SUBNET);
		if (nla) {
			info = nla_data(nla);
			subnet = info->value;
		}
		ret = beiscsi_if_en_static(phba, BEISCSI_IP_TYPE_V4,
					   ip, subnet);
		break;
	case ISCSI_NET_PARAM_IPV4_SUBNET:
		/*
		 * OPCODE_COMMON_ISCSI_NTWK_MODIFY_IP_ADDR ioctl needs IP
		 * and subnet both. Find IP to be applied for this subnet.
		 */
		subnet = info->value;
		nla = nla_find(data, dt_len, ISCSI_NET_PARAM_IPV4_ADDR);
		if (nla) {
			info = nla_data(nla);
			ip = info->value;
		}
		ret = beiscsi_if_en_static(phba, BEISCSI_IP_TYPE_V4,
					   ip, subnet);
		break;
	}

	return ret;
}

static int
beiscsi_iface_config_ipv6(struct Scsi_Host *shost,
			  struct iscsi_iface_param_info *iface_param,
			  void *data, uint32_t dt_len)
{
	struct beiscsi_hba *phba = iscsi_host_priv(shost);
	int ret = -EPERM;

	switch (iface_param->param) {
	case ISCSI_NET_PARAM_IFACE_ENABLE:
		if (iface_param->value[0] == ISCSI_IFACE_ENABLE)
			ret = beiscsi_iface_create_ipv6(phba);
		else {
			iscsi_destroy_iface(phba->ipv6_iface);
			phba->ipv6_iface = NULL;
		}
		break;
	case ISCSI_NET_PARAM_IPV6_ADDR:
		ret = beiscsi_if_en_static(phba, BEISCSI_IP_TYPE_V6,
					   iface_param->value, NULL);
		break;
	}

	return ret;
}

int beiscsi_iface_set_param(struct Scsi_Host *shost,
			    void *data, uint32_t dt_len)
{
	struct iscsi_iface_param_info *iface_param = NULL;
	struct beiscsi_hba *phba = iscsi_host_priv(shost);
	struct nlattr *attrib;
	uint32_t rm_len = dt_len;
	int ret;

	if (!beiscsi_hba_is_online(phba)) {
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BS_%d : HBA in error 0x%lx\n", phba->state);
		return -EBUSY;
	}

	/* update interface_handle */
	ret = beiscsi_if_get_handle(phba);
	if (ret) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : Getting Interface Handle Failed\n");
		return ret;
	}

	nla_for_each_attr(attrib, data, dt_len, rm_len) {
		iface_param = nla_data(attrib);

		if (iface_param->param_type != ISCSI_NET_PARAM)
			continue;

		/*
		 * BE2ISCSI only supports 1 interface
		 */
		if (iface_param->iface_num) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
				    "BS_%d : Invalid iface_num %d."
				    "Only iface_num 0 is supported.\n",
				    iface_param->iface_num);

			return -EINVAL;
		}

		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BS_%d : %s.0 set param %d",
			    (iface_param->iface_type == ISCSI_IFACE_TYPE_IPV4) ?
			    "ipv4" : "ipv6", iface_param->param);

		ret = -EPERM;
		switch (iface_param->param) {
		case ISCSI_NET_PARAM_VLAN_ENABLED:
		case ISCSI_NET_PARAM_VLAN_TAG:
			ret = beiscsi_iface_config_vlan(shost, iface_param);
			break;
		default:
			switch (iface_param->iface_type) {
			case ISCSI_IFACE_TYPE_IPV4:
				ret = beiscsi_iface_config_ipv4(shost,
								iface_param,
								data, dt_len);
				break;
			case ISCSI_IFACE_TYPE_IPV6:
				ret = beiscsi_iface_config_ipv6(shost,
								iface_param,
								data, dt_len);
				break;
			}
		}

		if (ret == -EPERM) {
			__beiscsi_log(phba, KERN_ERR,
				      "BS_%d : %s.0 set param %d not permitted",
				      (iface_param->iface_type ==
				       ISCSI_IFACE_TYPE_IPV4) ? "ipv4" : "ipv6",
				      iface_param->param);
			ret = 0;
		}
		if (ret)
			break;
	}

	return ret;
}

static int __beiscsi_iface_get_param(struct beiscsi_hba *phba,
				     struct iscsi_iface *iface,
				     int param, char *buf)
{
	struct be_cmd_get_if_info_resp *if_info;
	int len, ip_type = BEISCSI_IP_TYPE_V4;

	if (iface->iface_type == ISCSI_IFACE_TYPE_IPV6)
		ip_type = BEISCSI_IP_TYPE_V6;

	len = beiscsi_if_get_info(phba, ip_type, &if_info);
	if (len)
		return len;

	switch (param) {
	case ISCSI_NET_PARAM_IPV4_ADDR:
		len = sprintf(buf, "%pI4\n", if_info->ip_addr.addr);
		break;
	case ISCSI_NET_PARAM_IPV6_ADDR:
		len = sprintf(buf, "%pI6\n", if_info->ip_addr.addr);
		break;
	case ISCSI_NET_PARAM_IPV4_BOOTPROTO:
		if (!if_info->dhcp_state)
			len = sprintf(buf, "static\n");
		else
			len = sprintf(buf, "dhcp\n");
		break;
	case ISCSI_NET_PARAM_IPV4_SUBNET:
		len = sprintf(buf, "%pI4\n", if_info->ip_addr.subnet_mask);
		break;
	case ISCSI_NET_PARAM_VLAN_ENABLED:
		len = sprintf(buf, "%s\n",
			      (if_info->vlan_priority == BEISCSI_VLAN_DISABLE) ?
			      "disable" : "enable");
		break;
	case ISCSI_NET_PARAM_VLAN_ID:
		if (if_info->vlan_priority == BEISCSI_VLAN_DISABLE)
			len = -EINVAL;
		else
			len = sprintf(buf, "%d\n",
				      (if_info->vlan_priority &
				       ISCSI_MAX_VLAN_ID));
		break;
	case ISCSI_NET_PARAM_VLAN_PRIORITY:
		if (if_info->vlan_priority == BEISCSI_VLAN_DISABLE)
			len = -EINVAL;
		else
			len = sprintf(buf, "%d\n",
				      ((if_info->vlan_priority >> 13) &
				       ISCSI_MAX_VLAN_PRIORITY));
		break;
	default:
		WARN_ON(1);
	}

	kfree(if_info);
	return len;
}

int beiscsi_iface_get_param(struct iscsi_iface *iface,
			    enum iscsi_param_type param_type,
			    int param, char *buf)
{
	struct Scsi_Host *shost = iscsi_iface_to_shost(iface);
	struct beiscsi_hba *phba = iscsi_host_priv(shost);
	struct be_cmd_get_def_gateway_resp gateway;
	int len = -EPERM;

	if (param_type != ISCSI_NET_PARAM)
		return 0;
	if (!beiscsi_hba_is_online(phba)) {
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BS_%d : HBA in error 0x%lx\n", phba->state);
		return -EBUSY;
	}

	switch (param) {
	case ISCSI_NET_PARAM_IPV4_ADDR:
	case ISCSI_NET_PARAM_IPV4_SUBNET:
	case ISCSI_NET_PARAM_IPV4_BOOTPROTO:
	case ISCSI_NET_PARAM_IPV6_ADDR:
	case ISCSI_NET_PARAM_VLAN_ENABLED:
	case ISCSI_NET_PARAM_VLAN_ID:
	case ISCSI_NET_PARAM_VLAN_PRIORITY:
		len = __beiscsi_iface_get_param(phba, iface, param, buf);
		break;
	case ISCSI_NET_PARAM_IFACE_ENABLE:
		if (iface->iface_type == ISCSI_IFACE_TYPE_IPV4)
			len = sprintf(buf, "%s\n",
				      phba->ipv4_iface ? "enable" : "disable");
		else if (iface->iface_type == ISCSI_IFACE_TYPE_IPV6)
			len = sprintf(buf, "%s\n",
				      phba->ipv6_iface ? "enable" : "disable");
		break;
	case ISCSI_NET_PARAM_IPV4_GW:
		memset(&gateway, 0, sizeof(gateway));
		len = beiscsi_if_get_gw(phba, BEISCSI_IP_TYPE_V4, &gateway);
		if (!len)
			len = sprintf(buf, "%pI4\n", &gateway.ip_addr.addr);
		break;
	}

	return len;
}

/**
 * beiscsi_ep_get_param - get the iscsi parameter
 * @ep: pointer to iscsi ep
 * @param: parameter type identifier
 * @buf: buffer pointer
 *
 * returns iscsi parameter
 */
int beiscsi_ep_get_param(struct iscsi_endpoint *ep,
			   enum iscsi_param param, char *buf)
{
	struct beiscsi_endpoint *beiscsi_ep = ep->dd_data;
	int len;

	beiscsi_log(beiscsi_ep->phba, KERN_INFO,
		    BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_ep_get_param,"
		    " param= %d\n", param);

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
		len = sprintf(buf, "%hu\n", beiscsi_ep->dst_tcpport);
		break;
	case ISCSI_PARAM_CONN_ADDRESS:
		if (beiscsi_ep->ip_type == BEISCSI_IP_TYPE_V4)
			len = sprintf(buf, "%pI4\n", &beiscsi_ep->dst_addr);
		else
			len = sprintf(buf, "%pI6\n", &beiscsi_ep->dst6_addr);
		break;
	default:
		len = -EPERM;
	}
	return len;
}

int beiscsi_set_param(struct iscsi_cls_conn *cls_conn,
		      enum iscsi_param param, char *buf, int buflen)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct beiscsi_hba *phba = NULL;
	int ret;

	phba = ((struct beiscsi_conn *)conn->dd_data)->phba;
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_conn_set_param,"
		    " param= %d\n", param);

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
		if (conn->max_xmit_dlength > 65536)
			conn->max_xmit_dlength = 65536;
	default:
		return 0;
	}

	return 0;
}

/**
 * beiscsi_get_initname - Read Initiator Name from flash
 * @buf: buffer bointer
 * @phba: The device priv structure instance
 *
 * returns number of bytes
 */
static int beiscsi_get_initname(char *buf, struct beiscsi_hba *phba)
{
	int rc;
	unsigned int tag;
	struct be_mcc_wrb *wrb;
	struct be_cmd_hba_name *resp;

	tag = be_cmd_get_initname(phba);
	if (!tag) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : Getting Initiator Name Failed\n");

		return -EBUSY;
	}

	rc = beiscsi_mccq_compl_wait(phba, tag, &wrb, NULL);
	if (rc) {
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
			    "BS_%d : Initiator Name MBX Failed\n");
		return rc;
	}

	resp = embedded_payload(wrb);
	rc = sprintf(buf, "%s\n", resp->initiator_name);
	return rc;
}

/**
 * beiscsi_get_port_state - Get the Port State
 * @shost : pointer to scsi_host structure
 *
 */
static void beiscsi_get_port_state(struct Scsi_Host *shost)
{
	struct beiscsi_hba *phba = iscsi_host_priv(shost);
	struct iscsi_cls_host *ihost = shost->shost_data;

	ihost->port_state = test_bit(BEISCSI_HBA_LINK_UP, &phba->state) ?
		ISCSI_PORT_STATE_UP : ISCSI_PORT_STATE_DOWN;
}

/**
 * beiscsi_get_port_speed  - Get the Port Speed from Adapter
 * @shost : pointer to scsi_host structure
 *
 */
static void beiscsi_get_port_speed(struct Scsi_Host *shost)
{
	struct beiscsi_hba *phba = iscsi_host_priv(shost);
	struct iscsi_cls_host *ihost = shost->shost_data;

	switch (phba->port_speed) {
	case BE2ISCSI_LINK_SPEED_10MBPS:
		ihost->port_speed = ISCSI_PORT_SPEED_10MBPS;
		break;
	case BE2ISCSI_LINK_SPEED_100MBPS:
		ihost->port_speed = ISCSI_PORT_SPEED_100MBPS;
		break;
	case BE2ISCSI_LINK_SPEED_1GBPS:
		ihost->port_speed = ISCSI_PORT_SPEED_1GBPS;
		break;
	case BE2ISCSI_LINK_SPEED_10GBPS:
		ihost->port_speed = ISCSI_PORT_SPEED_10GBPS;
		break;
	case BE2ISCSI_LINK_SPEED_25GBPS:
		ihost->port_speed = ISCSI_PORT_SPEED_25GBPS;
		break;
	case BE2ISCSI_LINK_SPEED_40GBPS:
		ihost->port_speed = ISCSI_PORT_SPEED_40GBPS;
		break;
	default:
		ihost->port_speed = ISCSI_PORT_SPEED_UNKNOWN;
	}
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
	struct beiscsi_hba *phba = iscsi_host_priv(shost);
	int status = 0;

	if (!beiscsi_hba_is_online(phba)) {
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BS_%d : HBA in error 0x%lx\n", phba->state);
		return -EBUSY;
	}
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_get_host_param, param = %d\n", param);

	switch (param) {
	case ISCSI_HOST_PARAM_HWADDRESS:
		status = beiscsi_get_macaddr(buf, phba);
		if (status < 0) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
				    "BS_%d : beiscsi_get_macaddr Failed\n");
			return status;
		}
		break;
	case ISCSI_HOST_PARAM_INITIATOR_NAME:
		status = beiscsi_get_initname(buf, phba);
		if (status < 0) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
				    "BS_%d : Retreiving Initiator Name Failed\n");
			return status;
		}
		break;
	case ISCSI_HOST_PARAM_PORT_STATE:
		beiscsi_get_port_state(shost);
		status = sprintf(buf, "%s\n", iscsi_get_port_state_name(shost));
		break;
	case ISCSI_HOST_PARAM_PORT_SPEED:
		beiscsi_get_port_speed(shost);
		status = sprintf(buf, "%s\n", iscsi_get_port_speed_name(shost));
		break;
	default:
		return iscsi_host_get_param(shost, param, buf);
	}
	return status;
}

int beiscsi_get_macaddr(char *buf, struct beiscsi_hba *phba)
{
	struct be_cmd_get_nic_conf_resp resp;
	int rc;

	if (phba->mac_addr_set)
		return sysfs_format_mac(buf, phba->mac_address, ETH_ALEN);

	memset(&resp, 0, sizeof(resp));
	rc = mgmt_get_nic_conf(phba, &resp);
	if (rc)
		return rc;

	phba->mac_addr_set = true;
	memcpy(phba->mac_address, resp.mac_address, ETH_ALEN);
	return sysfs_format_mac(buf, phba->mac_address, ETH_ALEN);
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
	struct beiscsi_hba *phba = NULL;

	phba = ((struct beiscsi_conn *)conn->dd_data)->phba;
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_conn_get_stats\n");

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
	stats->custom_length = 1;
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
	AMAP_SET_BITS(struct amap_beiscsi_offload_params,
		      data_seq_inorder, params,
		      session->dataseq_inorder_en);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params,
		      pdu_seq_inorder, params,
		      session->pdu_inorder_en);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params, max_r2t, params,
		      session->max_r2t);
	AMAP_SET_BITS(struct amap_beiscsi_offload_params, exp_statsn, params,
		      (conn->exp_statsn - 1));
	AMAP_SET_BITS(struct amap_beiscsi_offload_params,
		      max_recv_data_segment_length, params,
		      conn->max_recv_dlength);

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
	struct beiscsi_hba *phba;

	phba = ((struct beiscsi_conn *)conn->dd_data)->phba;

	if (!beiscsi_hba_is_online(phba)) {
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BS_%d : HBA in error 0x%lx\n", phba->state);
		return -EBUSY;
	}
	beiscsi_log(beiscsi_conn->phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_conn_start\n");

	memset(&params, 0, sizeof(struct beiscsi_offload_params));
	beiscsi_ep = beiscsi_conn->ep;
	if (!beiscsi_ep)
		beiscsi_log(beiscsi_conn->phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG,
			    "BS_%d : In beiscsi_conn_start , no beiscsi_ep\n");

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
	uint16_t cid_avlbl_ulp0, cid_avlbl_ulp1;
	unsigned short cid, cid_from_ulp;
	struct ulp_cid_info *cid_info;

	/* Find the ULP which has more CID available */
	cid_avlbl_ulp0 = (phba->cid_array_info[BEISCSI_ULP0]) ?
			  BEISCSI_ULP0_AVLBL_CID(phba) : 0;
	cid_avlbl_ulp1 = (phba->cid_array_info[BEISCSI_ULP1]) ?
			  BEISCSI_ULP1_AVLBL_CID(phba) : 0;
	cid_from_ulp = (cid_avlbl_ulp0 > cid_avlbl_ulp1) ?
			BEISCSI_ULP0 : BEISCSI_ULP1;
	/**
	 * If iSCSI protocol is loaded only on ULP 0, and when cid_avlbl_ulp
	 * is ZERO for both, ULP 1 is returned.
	 * Check if ULP is loaded before getting new CID.
	 */
	if (!test_bit(cid_from_ulp, (void *)&phba->fw_config.ulp_supported))
		return BE_INVALID_CID;

	cid_info = phba->cid_array_info[cid_from_ulp];
	cid = cid_info->cid_array[cid_info->cid_alloc];
	if (!cid_info->avlbl_cids || cid == BE_INVALID_CID) {
		__beiscsi_log(phba, KERN_ERR,
				"BS_%d : failed to get cid: available %u:%u\n",
				cid_info->avlbl_cids, cid_info->cid_free);
		return BE_INVALID_CID;
	}
	/* empty the slot */
	cid_info->cid_array[cid_info->cid_alloc++] = BE_INVALID_CID;
	if (cid_info->cid_alloc == BEISCSI_GET_CID_COUNT(phba, cid_from_ulp))
		cid_info->cid_alloc = 0;
	cid_info->avlbl_cids--;
	return cid;
}

/**
 * beiscsi_put_cid - Free the cid
 * @phba: The phba for which the cid is being freed
 * @cid: The cid to free
 */
static void beiscsi_put_cid(struct beiscsi_hba *phba, unsigned short cid)
{
	uint16_t cri_index = BE_GET_CRI_FROM_CID(cid);
	struct hwi_wrb_context *pwrb_context;
	struct hwi_controller *phwi_ctrlr;
	struct ulp_cid_info *cid_info;
	uint16_t cid_post_ulp;

	phwi_ctrlr = phba->phwi_ctrlr;
	pwrb_context = &phwi_ctrlr->wrb_context[cri_index];
	cid_post_ulp = pwrb_context->ulp_num;

	cid_info = phba->cid_array_info[cid_post_ulp];
	/* fill only in empty slot */
	if (cid_info->cid_array[cid_info->cid_free] != BE_INVALID_CID) {
		__beiscsi_log(phba, KERN_ERR,
			      "BS_%d : failed to put cid %u: available %u:%u\n",
			      cid, cid_info->avlbl_cids, cid_info->cid_free);
		return;
	}
	cid_info->cid_array[cid_info->cid_free++] = cid;
	if (cid_info->cid_free == BEISCSI_GET_CID_COUNT(phba, cid_post_ulp))
		cid_info->cid_free = 0;
	cid_info->avlbl_cids++;
}

/**
 * beiscsi_free_ep - free endpoint
 * @ep:	pointer to iscsi endpoint structure
 */
static void beiscsi_free_ep(struct beiscsi_endpoint *beiscsi_ep)
{
	struct beiscsi_hba *phba = beiscsi_ep->phba;
	struct beiscsi_conn *beiscsi_conn;

	beiscsi_put_cid(phba, beiscsi_ep->ep_cid);
	beiscsi_ep->phba = NULL;
	/* clear this to track freeing in beiscsi_ep_disconnect */
	phba->ep_array[BE_GET_CRI_FROM_CID(beiscsi_ep->ep_cid)] = NULL;

	/**
	 * Check if any connection resource allocated by driver
	 * is to be freed.This case occurs when target redirection
	 * or connection retry is done.
	 **/
	if (!beiscsi_ep->conn)
		return;

	beiscsi_conn = beiscsi_ep->conn;
	/**
	 * Break ep->conn link here so that completions after
	 * this are ignored.
	 */
	beiscsi_ep->conn = NULL;
	if (beiscsi_conn->login_in_progress) {
		beiscsi_free_mgmt_task_handles(beiscsi_conn,
					       beiscsi_conn->task);
		beiscsi_conn->login_in_progress = 0;
	}
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
	struct tcp_connect_and_offload_out *ptcpcnct_out;
	struct be_dma_mem nonemb_cmd;
	unsigned int tag, req_memsize;
	int ret = -ENOMEM;

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_open_conn\n");

	beiscsi_ep->ep_cid = beiscsi_get_cid(phba);
	if (beiscsi_ep->ep_cid == BE_INVALID_CID) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : No free cid available\n");
		return ret;
	}

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_open_conn, ep_cid=%d\n",
		    beiscsi_ep->ep_cid);

	phba->ep_array[BE_GET_CRI_FROM_CID
		       (beiscsi_ep->ep_cid)] = ep;

	beiscsi_ep->cid_vld = 0;

	if (is_chip_be2_be3r(phba))
		req_memsize = sizeof(struct tcp_connect_and_offload_in);
	else
		req_memsize = sizeof(struct tcp_connect_and_offload_in_v1);

	nonemb_cmd.va = pci_alloc_consistent(phba->ctrl.pdev,
				req_memsize,
				&nonemb_cmd.dma);
	if (nonemb_cmd.va == NULL) {

		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : Failed to allocate memory for"
			    " mgmt_open_connection\n");

		beiscsi_free_ep(beiscsi_ep);
		return -ENOMEM;
	}
	nonemb_cmd.size = req_memsize;
	memset(nonemb_cmd.va, 0, nonemb_cmd.size);
	tag = mgmt_open_connection(phba, dst_addr, beiscsi_ep, &nonemb_cmd);
	if (!tag) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : mgmt_open_connection Failed for cid=%d\n",
			    beiscsi_ep->ep_cid);

		pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
				    nonemb_cmd.va, nonemb_cmd.dma);
		beiscsi_free_ep(beiscsi_ep);
		return -EAGAIN;
	}

	ret = beiscsi_mccq_compl_wait(phba, tag, NULL, &nonemb_cmd);
	if (ret) {
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
			    "BS_%d : mgmt_open_connection Failed");

		if (ret != -EBUSY)
			pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
					    nonemb_cmd.va, nonemb_cmd.dma);

		beiscsi_free_ep(beiscsi_ep);
		return ret;
	}

	ptcpcnct_out = (struct tcp_connect_and_offload_out *)nonemb_cmd.va;
	beiscsi_ep = ep->dd_data;
	beiscsi_ep->fw_handle = ptcpcnct_out->connection_handle;
	beiscsi_ep->cid_vld = 1;
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : mgmt_open_connection Success\n");

	pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
			    nonemb_cmd.va, nonemb_cmd.dma);
	return 0;
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

	if (!shost) {
		ret = -ENXIO;
		pr_err("beiscsi_ep_connect shost is NULL\n");
		return ERR_PTR(ret);
	}

	phba = iscsi_host_priv(shost);
	if (!beiscsi_hba_is_online(phba)) {
		ret = -EIO;
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BS_%d : HBA in error 0x%lx\n", phba->state);
		return ERR_PTR(ret);
	}
	if (!test_bit(BEISCSI_HBA_LINK_UP, &phba->state)) {
		ret = -EBUSY;
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
			    "BS_%d : The Adapter Port state is Down!!!\n");
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
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : Failed in beiscsi_open_conn\n");
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

	beiscsi_log(beiscsi_ep->phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In  beiscsi_ep_poll\n");

	if (beiscsi_ep->cid_vld == 1)
		return 1;
	else
		return 0;
}

/**
 * beiscsi_flush_cq()- Flush the CQ created.
 * @phba: ptr device priv structure.
 *
 * Before the connection resource are freed flush
 * all the CQ enteries
 **/
static void beiscsi_flush_cq(struct beiscsi_hba *phba)
{
	uint16_t i;
	struct be_eq_obj *pbe_eq;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;

	for (i = 0; i < phba->num_cpus; i++) {
		pbe_eq = &phwi_context->be_eq[i];
		irq_poll_disable(&pbe_eq->iopoll);
		beiscsi_process_cq(pbe_eq, BE2_MAX_NUM_CQ_PROC);
		irq_poll_enable(&pbe_eq->iopoll);
	}
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
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BS_%d : upload failed for cid 0x%x\n",
			    beiscsi_ep->ep_cid);

		ret = -EAGAIN;
	}

	ret = beiscsi_mccq_compl_wait(phba, tag, NULL, NULL);

	/* Flush the CQ entries */
	beiscsi_flush_cq(phba);

	return ret;
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
	uint8_t mgmt_invalidate_flag, tcp_upload_flag;
	unsigned short savecfg_flag = CMD_ISCSI_SESSION_SAVE_CFG_ON_FLASH;
	uint16_t cri_index;

	beiscsi_ep = ep->dd_data;
	phba = beiscsi_ep->phba;
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BS_%d : In beiscsi_ep_disconnect for ep_cid = %u\n",
		    beiscsi_ep->ep_cid);

	cri_index = BE_GET_CRI_FROM_CID(beiscsi_ep->ep_cid);
	if (!phba->ep_array[cri_index]) {
		__beiscsi_log(phba, KERN_ERR,
			      "BS_%d : ep_array at %u cid %u empty\n",
			      cri_index,
			      beiscsi_ep->ep_cid);
		return;
	}

	if (beiscsi_ep->conn) {
		beiscsi_conn = beiscsi_ep->conn;
		iscsi_suspend_queue(beiscsi_conn->conn);
		mgmt_invalidate_flag = ~BEISCSI_NO_RST_ISSUE;
		tcp_upload_flag = CONNECTION_UPLOAD_GRACEFUL;
	} else {
		mgmt_invalidate_flag = BEISCSI_NO_RST_ISSUE;
		tcp_upload_flag = CONNECTION_UPLOAD_ABORT;
	}

	if (!beiscsi_hba_is_online(phba)) {
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BS_%d : HBA in error 0x%lx\n", phba->state);
		goto free_ep;
	}

	tag = mgmt_invalidate_connection(phba, beiscsi_ep,
					  beiscsi_ep->ep_cid,
					  mgmt_invalidate_flag,
					  savecfg_flag);
	if (!tag) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BS_%d : mgmt_invalidate_connection Failed for cid=%d\n",
			    beiscsi_ep->ep_cid);
	}

	beiscsi_mccq_compl_wait(phba, tag, NULL, NULL);
	beiscsi_close_conn(beiscsi_ep, tcp_upload_flag);
free_ep:
	msleep(BEISCSI_LOGOUT_SYNC_DELAY);
	beiscsi_free_ep(beiscsi_ep);
	if (!phba->conn_table[cri_index])
		__beiscsi_log(phba, KERN_ERR,
				"BS_%d : conn_table empty at %u: cid %u\n",
				cri_index,
				beiscsi_ep->ep_cid);
	phba->conn_table[cri_index] = NULL;
	iscsi_destroy_endpoint(beiscsi_ep->openiscsi_ep);
}

umode_t beiscsi_attr_is_visible(int param_type, int param)
{
	switch (param_type) {
	case ISCSI_NET_PARAM:
		switch (param) {
		case ISCSI_NET_PARAM_IFACE_ENABLE:
		case ISCSI_NET_PARAM_IPV4_ADDR:
		case ISCSI_NET_PARAM_IPV4_SUBNET:
		case ISCSI_NET_PARAM_IPV4_BOOTPROTO:
		case ISCSI_NET_PARAM_IPV4_GW:
		case ISCSI_NET_PARAM_IPV6_ADDR:
		case ISCSI_NET_PARAM_VLAN_ID:
		case ISCSI_NET_PARAM_VLAN_PRIORITY:
		case ISCSI_NET_PARAM_VLAN_ENABLED:
			return S_IRUGO;
		default:
			return 0;
		}
	case ISCSI_HOST_PARAM:
		switch (param) {
		case ISCSI_HOST_PARAM_HWADDRESS:
		case ISCSI_HOST_PARAM_INITIATOR_NAME:
		case ISCSI_HOST_PARAM_PORT_STATE:
		case ISCSI_HOST_PARAM_PORT_SPEED:
			return S_IRUGO;
		default:
			return 0;
		}
	case ISCSI_PARAM:
		switch (param) {
		case ISCSI_PARAM_MAX_RECV_DLENGTH:
		case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		case ISCSI_PARAM_HDRDGST_EN:
		case ISCSI_PARAM_DATADGST_EN:
		case ISCSI_PARAM_CONN_ADDRESS:
		case ISCSI_PARAM_CONN_PORT:
		case ISCSI_PARAM_EXP_STATSN:
		case ISCSI_PARAM_PERSISTENT_ADDRESS:
		case ISCSI_PARAM_PERSISTENT_PORT:
		case ISCSI_PARAM_PING_TMO:
		case ISCSI_PARAM_RECV_TMO:
		case ISCSI_PARAM_INITIAL_R2T_EN:
		case ISCSI_PARAM_MAX_R2T:
		case ISCSI_PARAM_IMM_DATA_EN:
		case ISCSI_PARAM_FIRST_BURST:
		case ISCSI_PARAM_MAX_BURST:
		case ISCSI_PARAM_PDU_INORDER_EN:
		case ISCSI_PARAM_DATASEQ_INORDER_EN:
		case ISCSI_PARAM_ERL:
		case ISCSI_PARAM_TARGET_NAME:
		case ISCSI_PARAM_TPGT:
		case ISCSI_PARAM_USERNAME:
		case ISCSI_PARAM_PASSWORD:
		case ISCSI_PARAM_USERNAME_IN:
		case ISCSI_PARAM_PASSWORD_IN:
		case ISCSI_PARAM_FAST_ABORT:
		case ISCSI_PARAM_ABORT_TMO:
		case ISCSI_PARAM_LU_RESET_TMO:
		case ISCSI_PARAM_IFACE_NAME:
		case ISCSI_PARAM_INITIATOR_NAME:
			return S_IRUGO;
		default:
			return 0;
		}
	}

	return 0;
}
