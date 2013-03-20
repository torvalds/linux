/*******************************************************************************
 * This file contains the configfs implementation for iSCSI Target mode
 * from the LIO-Target Project.
 *
 * \u00a9 Copyright 2007-2011 RisingTide Systems LLC.
 *
 * Licensed to the Linux Foundation under the General Public License (GPL) version 2.
 *
 * Author: Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 ****************************************************************************/

#include <linux/configfs.h>
#include <linux/export.h>
#include <linux/inet.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_fabric_configfs.h>
#include <target/target_core_configfs.h>
#include <target/configfs_macros.h>
#include <target/iscsi/iscsi_transport.h>

#include "iscsi_target_core.h"
#include "iscsi_target_parameters.h"
#include "iscsi_target_device.h"
#include "iscsi_target_erl0.h"
#include "iscsi_target_nodeattrib.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target.h"
#include "iscsi_target_stat.h"
#include "iscsi_target_configfs.h"

struct target_fabric_configfs *lio_target_fabric_configfs;

struct lio_target_configfs_attribute {
	struct configfs_attribute attr;
	ssize_t (*show)(void *, char *);
	ssize_t (*store)(void *, const char *, size_t);
};

/* Start items for lio_target_portal_cit */

static ssize_t lio_target_np_show_sctp(
	struct se_tpg_np *se_tpg_np,
	char *page)
{
	struct iscsi_tpg_np *tpg_np = container_of(se_tpg_np,
				struct iscsi_tpg_np, se_tpg_np);
	struct iscsi_tpg_np *tpg_np_sctp;
	ssize_t rb;

	tpg_np_sctp = iscsit_tpg_locate_child_np(tpg_np, ISCSI_SCTP_TCP);
	if (tpg_np_sctp)
		rb = sprintf(page, "1\n");
	else
		rb = sprintf(page, "0\n");

	return rb;
}

static ssize_t lio_target_np_store_sctp(
	struct se_tpg_np *se_tpg_np,
	const char *page,
	size_t count)
{
	struct iscsi_np *np;
	struct iscsi_portal_group *tpg;
	struct iscsi_tpg_np *tpg_np = container_of(se_tpg_np,
				struct iscsi_tpg_np, se_tpg_np);
	struct iscsi_tpg_np *tpg_np_sctp = NULL;
	char *endptr;
	u32 op;
	int ret;

	op = simple_strtoul(page, &endptr, 0);
	if ((op != 1) && (op != 0)) {
		pr_err("Illegal value for tpg_enable: %u\n", op);
		return -EINVAL;
	}
	np = tpg_np->tpg_np;
	if (!np) {
		pr_err("Unable to locate struct iscsi_np from"
				" struct iscsi_tpg_np\n");
		return -EINVAL;
	}

	tpg = tpg_np->tpg;
	if (iscsit_get_tpg(tpg) < 0)
		return -EINVAL;

	if (op) {
		/*
		 * Use existing np->np_sockaddr for SCTP network portal reference
		 */
		tpg_np_sctp = iscsit_tpg_add_network_portal(tpg, &np->np_sockaddr,
					np->np_ip, tpg_np, ISCSI_SCTP_TCP);
		if (!tpg_np_sctp || IS_ERR(tpg_np_sctp))
			goto out;
	} else {
		tpg_np_sctp = iscsit_tpg_locate_child_np(tpg_np, ISCSI_SCTP_TCP);
		if (!tpg_np_sctp)
			goto out;

		ret = iscsit_tpg_del_network_portal(tpg, tpg_np_sctp);
		if (ret < 0)
			goto out;
	}

	iscsit_put_tpg(tpg);
	return count;
out:
	iscsit_put_tpg(tpg);
	return -EINVAL;
}

TF_NP_BASE_ATTR(lio_target, sctp, S_IRUGO | S_IWUSR);

static struct configfs_attribute *lio_target_portal_attrs[] = {
	&lio_target_np_sctp.attr,
	NULL,
};

/* Stop items for lio_target_portal_cit */

/* Start items for lio_target_np_cit */

#define MAX_PORTAL_LEN		256

static struct se_tpg_np *lio_target_call_addnptotpg(
	struct se_portal_group *se_tpg,
	struct config_group *group,
	const char *name)
{
	struct iscsi_portal_group *tpg;
	struct iscsi_tpg_np *tpg_np;
	char *str, *str2, *ip_str, *port_str;
	struct __kernel_sockaddr_storage sockaddr;
	struct sockaddr_in *sock_in;
	struct sockaddr_in6 *sock_in6;
	unsigned long port;
	int ret;
	char buf[MAX_PORTAL_LEN + 1];

	if (strlen(name) > MAX_PORTAL_LEN) {
		pr_err("strlen(name): %d exceeds MAX_PORTAL_LEN: %d\n",
			(int)strlen(name), MAX_PORTAL_LEN);
		return ERR_PTR(-EOVERFLOW);
	}
	memset(buf, 0, MAX_PORTAL_LEN + 1);
	snprintf(buf, MAX_PORTAL_LEN + 1, "%s", name);

	memset(&sockaddr, 0, sizeof(struct __kernel_sockaddr_storage));

	str = strstr(buf, "[");
	if (str) {
		const char *end;

		str2 = strstr(str, "]");
		if (!str2) {
			pr_err("Unable to locate trailing \"]\""
				" in IPv6 iSCSI network portal address\n");
			return ERR_PTR(-EINVAL);
		}
		str++; /* Skip over leading "[" */
		*str2 = '\0'; /* Terminate the IPv6 address */
		str2++; /* Skip over the "]" */
		port_str = strstr(str2, ":");
		if (!port_str) {
			pr_err("Unable to locate \":port\""
				" in IPv6 iSCSI network portal address\n");
			return ERR_PTR(-EINVAL);
		}
		*port_str = '\0'; /* Terminate string for IP */
		port_str++; /* Skip over ":" */

		ret = strict_strtoul(port_str, 0, &port);
		if (ret < 0) {
			pr_err("strict_strtoul() failed for port_str: %d\n", ret);
			return ERR_PTR(ret);
		}
		sock_in6 = (struct sockaddr_in6 *)&sockaddr;
		sock_in6->sin6_family = AF_INET6;
		sock_in6->sin6_port = htons((unsigned short)port);
		ret = in6_pton(str, IPV6_ADDRESS_SPACE,
				(void *)&sock_in6->sin6_addr.in6_u, -1, &end);
		if (ret <= 0) {
			pr_err("in6_pton returned: %d\n", ret);
			return ERR_PTR(-EINVAL);
		}
	} else {
		str = ip_str = &buf[0];
		port_str = strstr(ip_str, ":");
		if (!port_str) {
			pr_err("Unable to locate \":port\""
				" in IPv4 iSCSI network portal address\n");
			return ERR_PTR(-EINVAL);
		}
		*port_str = '\0'; /* Terminate string for IP */
		port_str++; /* Skip over ":" */

		ret = strict_strtoul(port_str, 0, &port);
		if (ret < 0) {
			pr_err("strict_strtoul() failed for port_str: %d\n", ret);
			return ERR_PTR(ret);
		}
		sock_in = (struct sockaddr_in *)&sockaddr;
		sock_in->sin_family = AF_INET;
		sock_in->sin_port = htons((unsigned short)port);
		sock_in->sin_addr.s_addr = in_aton(ip_str);
	}
	tpg = container_of(se_tpg, struct iscsi_portal_group, tpg_se_tpg);
	ret = iscsit_get_tpg(tpg);
	if (ret < 0)
		return ERR_PTR(-EINVAL);

	pr_debug("LIO_Target_ConfigFS: REGISTER -> %s TPGT: %hu"
		" PORTAL: %s\n",
		config_item_name(&se_tpg->se_tpg_wwn->wwn_group.cg_item),
		tpg->tpgt, name);
	/*
	 * Assume ISCSI_TCP by default.  Other network portals for other
	 * iSCSI fabrics:
	 *
	 * Traditional iSCSI over SCTP (initial support)
	 * iSER/TCP (TODO, hardware available)
	 * iSER/SCTP (TODO, software emulation with osc-iwarp)
	 * iSER/IB (TODO, hardware available)
	 *
	 * can be enabled with attributes under
	 * sys/kernel/config/iscsi/$IQN/$TPG/np/$IP:$PORT/
	 *
	 */
	tpg_np = iscsit_tpg_add_network_portal(tpg, &sockaddr, str, NULL,
				ISCSI_TCP);
	if (IS_ERR(tpg_np)) {
		iscsit_put_tpg(tpg);
		return ERR_CAST(tpg_np);
	}
	pr_debug("LIO_Target_ConfigFS: addnptotpg done!\n");

	iscsit_put_tpg(tpg);
	return &tpg_np->se_tpg_np;
}

static void lio_target_call_delnpfromtpg(
	struct se_tpg_np *se_tpg_np)
{
	struct iscsi_portal_group *tpg;
	struct iscsi_tpg_np *tpg_np;
	struct se_portal_group *se_tpg;
	int ret;

	tpg_np = container_of(se_tpg_np, struct iscsi_tpg_np, se_tpg_np);
	tpg = tpg_np->tpg;
	ret = iscsit_get_tpg(tpg);
	if (ret < 0)
		return;

	se_tpg = &tpg->tpg_se_tpg;
	pr_debug("LIO_Target_ConfigFS: DEREGISTER -> %s TPGT: %hu"
		" PORTAL: %s:%hu\n", config_item_name(&se_tpg->se_tpg_wwn->wwn_group.cg_item),
		tpg->tpgt, tpg_np->tpg_np->np_ip, tpg_np->tpg_np->np_port);

	ret = iscsit_tpg_del_network_portal(tpg, tpg_np);
	if (ret < 0)
		goto out;

	pr_debug("LIO_Target_ConfigFS: delnpfromtpg done!\n");
out:
	iscsit_put_tpg(tpg);
}

/* End items for lio_target_np_cit */

/* Start items for lio_target_nacl_attrib_cit */

#define DEF_NACL_ATTRIB(name)						\
static ssize_t iscsi_nacl_attrib_show_##name(				\
	struct se_node_acl *se_nacl,					\
	char *page)							\
{									\
	struct iscsi_node_acl *nacl = container_of(se_nacl, struct iscsi_node_acl, \
					se_node_acl);			\
									\
	return sprintf(page, "%u\n", ISCSI_NODE_ATTRIB(nacl)->name);	\
}									\
									\
static ssize_t iscsi_nacl_attrib_store_##name(				\
	struct se_node_acl *se_nacl,					\
	const char *page,						\
	size_t count)							\
{									\
	struct iscsi_node_acl *nacl = container_of(se_nacl, struct iscsi_node_acl, \
					se_node_acl);			\
	char *endptr;							\
	u32 val;							\
	int ret;							\
									\
	val = simple_strtoul(page, &endptr, 0);				\
	ret = iscsit_na_##name(nacl, val);				\
	if (ret < 0)							\
		return ret;						\
									\
	return count;							\
}

#define NACL_ATTR(_name, _mode) TF_NACL_ATTRIB_ATTR(iscsi, _name, _mode);
/*
 * Define iscsi_node_attrib_s_dataout_timeout
 */
DEF_NACL_ATTRIB(dataout_timeout);
NACL_ATTR(dataout_timeout, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_node_attrib_s_dataout_timeout_retries
 */
DEF_NACL_ATTRIB(dataout_timeout_retries);
NACL_ATTR(dataout_timeout_retries, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_node_attrib_s_default_erl
 */
DEF_NACL_ATTRIB(default_erl);
NACL_ATTR(default_erl, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_node_attrib_s_nopin_timeout
 */
DEF_NACL_ATTRIB(nopin_timeout);
NACL_ATTR(nopin_timeout, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_node_attrib_s_nopin_response_timeout
 */
DEF_NACL_ATTRIB(nopin_response_timeout);
NACL_ATTR(nopin_response_timeout, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_node_attrib_s_random_datain_pdu_offsets
 */
DEF_NACL_ATTRIB(random_datain_pdu_offsets);
NACL_ATTR(random_datain_pdu_offsets, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_node_attrib_s_random_datain_seq_offsets
 */
DEF_NACL_ATTRIB(random_datain_seq_offsets);
NACL_ATTR(random_datain_seq_offsets, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_node_attrib_s_random_r2t_offsets
 */
DEF_NACL_ATTRIB(random_r2t_offsets);
NACL_ATTR(random_r2t_offsets, S_IRUGO | S_IWUSR);

static struct configfs_attribute *lio_target_nacl_attrib_attrs[] = {
	&iscsi_nacl_attrib_dataout_timeout.attr,
	&iscsi_nacl_attrib_dataout_timeout_retries.attr,
	&iscsi_nacl_attrib_default_erl.attr,
	&iscsi_nacl_attrib_nopin_timeout.attr,
	&iscsi_nacl_attrib_nopin_response_timeout.attr,
	&iscsi_nacl_attrib_random_datain_pdu_offsets.attr,
	&iscsi_nacl_attrib_random_datain_seq_offsets.attr,
	&iscsi_nacl_attrib_random_r2t_offsets.attr,
	NULL,
};

/* End items for lio_target_nacl_attrib_cit */

/* Start items for lio_target_nacl_auth_cit */

#define __DEF_NACL_AUTH_STR(prefix, name, flags)			\
static ssize_t __iscsi_##prefix##_show_##name(				\
	struct iscsi_node_acl *nacl,					\
	char *page)							\
{									\
	struct iscsi_node_auth *auth = &nacl->node_auth;		\
									\
	if (!capable(CAP_SYS_ADMIN))					\
		return -EPERM;						\
	return snprintf(page, PAGE_SIZE, "%s\n", auth->name);		\
}									\
									\
static ssize_t __iscsi_##prefix##_store_##name(				\
	struct iscsi_node_acl *nacl,					\
	const char *page,						\
	size_t count)							\
{									\
	struct iscsi_node_auth *auth = &nacl->node_auth;		\
									\
	if (!capable(CAP_SYS_ADMIN))					\
		return -EPERM;						\
									\
	snprintf(auth->name, PAGE_SIZE, "%s", page);			\
	if (!strncmp("NULL", auth->name, 4))				\
		auth->naf_flags &= ~flags;				\
	else								\
		auth->naf_flags |= flags;				\
									\
	if ((auth->naf_flags & NAF_USERID_IN_SET) &&			\
	    (auth->naf_flags & NAF_PASSWORD_IN_SET))			\
		auth->authenticate_target = 1;				\
	else								\
		auth->authenticate_target = 0;				\
									\
	return count;							\
}

#define __DEF_NACL_AUTH_INT(prefix, name)				\
static ssize_t __iscsi_##prefix##_show_##name(				\
	struct iscsi_node_acl *nacl,					\
	char *page)							\
{									\
	struct iscsi_node_auth *auth = &nacl->node_auth;		\
									\
	if (!capable(CAP_SYS_ADMIN))					\
		return -EPERM;						\
									\
	return snprintf(page, PAGE_SIZE, "%d\n", auth->name);		\
}

#define DEF_NACL_AUTH_STR(name, flags)					\
	__DEF_NACL_AUTH_STR(nacl_auth, name, flags)			\
static ssize_t iscsi_nacl_auth_show_##name(				\
	struct se_node_acl *nacl,					\
	char *page)							\
{									\
	return __iscsi_nacl_auth_show_##name(container_of(nacl,		\
			struct iscsi_node_acl, se_node_acl), page);		\
}									\
static ssize_t iscsi_nacl_auth_store_##name(				\
	struct se_node_acl *nacl,					\
	const char *page,						\
	size_t count)							\
{									\
	return __iscsi_nacl_auth_store_##name(container_of(nacl,	\
			struct iscsi_node_acl, se_node_acl), page, count);	\
}

#define DEF_NACL_AUTH_INT(name)						\
	__DEF_NACL_AUTH_INT(nacl_auth, name)				\
static ssize_t iscsi_nacl_auth_show_##name(				\
	struct se_node_acl *nacl,					\
	char *page)							\
{									\
	return __iscsi_nacl_auth_show_##name(container_of(nacl,		\
			struct iscsi_node_acl, se_node_acl), page);		\
}

#define AUTH_ATTR(_name, _mode)	TF_NACL_AUTH_ATTR(iscsi, _name, _mode);
#define AUTH_ATTR_RO(_name) TF_NACL_AUTH_ATTR_RO(iscsi, _name);

/*
 * One-way authentication userid
 */
DEF_NACL_AUTH_STR(userid, NAF_USERID_SET);
AUTH_ATTR(userid, S_IRUGO | S_IWUSR);
/*
 * One-way authentication password
 */
DEF_NACL_AUTH_STR(password, NAF_PASSWORD_SET);
AUTH_ATTR(password, S_IRUGO | S_IWUSR);
/*
 * Enforce mutual authentication
 */
DEF_NACL_AUTH_INT(authenticate_target);
AUTH_ATTR_RO(authenticate_target);
/*
 * Mutual authentication userid
 */
DEF_NACL_AUTH_STR(userid_mutual, NAF_USERID_IN_SET);
AUTH_ATTR(userid_mutual, S_IRUGO | S_IWUSR);
/*
 * Mutual authentication password
 */
DEF_NACL_AUTH_STR(password_mutual, NAF_PASSWORD_IN_SET);
AUTH_ATTR(password_mutual, S_IRUGO | S_IWUSR);

static struct configfs_attribute *lio_target_nacl_auth_attrs[] = {
	&iscsi_nacl_auth_userid.attr,
	&iscsi_nacl_auth_password.attr,
	&iscsi_nacl_auth_authenticate_target.attr,
	&iscsi_nacl_auth_userid_mutual.attr,
	&iscsi_nacl_auth_password_mutual.attr,
	NULL,
};

/* End items for lio_target_nacl_auth_cit */

/* Start items for lio_target_nacl_param_cit */

#define DEF_NACL_PARAM(name)						\
static ssize_t iscsi_nacl_param_show_##name(				\
	struct se_node_acl *se_nacl,					\
	char *page)							\
{									\
	struct iscsi_session *sess;					\
	struct se_session *se_sess;					\
	ssize_t rb;							\
									\
	spin_lock_bh(&se_nacl->nacl_sess_lock);				\
	se_sess = se_nacl->nacl_sess;					\
	if (!se_sess) {							\
		rb = snprintf(page, PAGE_SIZE,				\
			"No Active iSCSI Session\n");			\
	} else {							\
		sess = se_sess->fabric_sess_ptr;			\
		rb = snprintf(page, PAGE_SIZE, "%u\n",			\
			(u32)sess->sess_ops->name);			\
	}								\
	spin_unlock_bh(&se_nacl->nacl_sess_lock);			\
									\
	return rb;							\
}

#define NACL_PARAM_ATTR(_name) TF_NACL_PARAM_ATTR_RO(iscsi, _name);

DEF_NACL_PARAM(MaxConnections);
NACL_PARAM_ATTR(MaxConnections);

DEF_NACL_PARAM(InitialR2T);
NACL_PARAM_ATTR(InitialR2T);

DEF_NACL_PARAM(ImmediateData);
NACL_PARAM_ATTR(ImmediateData);

DEF_NACL_PARAM(MaxBurstLength);
NACL_PARAM_ATTR(MaxBurstLength);

DEF_NACL_PARAM(FirstBurstLength);
NACL_PARAM_ATTR(FirstBurstLength);

DEF_NACL_PARAM(DefaultTime2Wait);
NACL_PARAM_ATTR(DefaultTime2Wait);

DEF_NACL_PARAM(DefaultTime2Retain);
NACL_PARAM_ATTR(DefaultTime2Retain);

DEF_NACL_PARAM(MaxOutstandingR2T);
NACL_PARAM_ATTR(MaxOutstandingR2T);

DEF_NACL_PARAM(DataPDUInOrder);
NACL_PARAM_ATTR(DataPDUInOrder);

DEF_NACL_PARAM(DataSequenceInOrder);
NACL_PARAM_ATTR(DataSequenceInOrder);

DEF_NACL_PARAM(ErrorRecoveryLevel);
NACL_PARAM_ATTR(ErrorRecoveryLevel);

static struct configfs_attribute *lio_target_nacl_param_attrs[] = {
	&iscsi_nacl_param_MaxConnections.attr,
	&iscsi_nacl_param_InitialR2T.attr,
	&iscsi_nacl_param_ImmediateData.attr,
	&iscsi_nacl_param_MaxBurstLength.attr,
	&iscsi_nacl_param_FirstBurstLength.attr,
	&iscsi_nacl_param_DefaultTime2Wait.attr,
	&iscsi_nacl_param_DefaultTime2Retain.attr,
	&iscsi_nacl_param_MaxOutstandingR2T.attr,
	&iscsi_nacl_param_DataPDUInOrder.attr,
	&iscsi_nacl_param_DataSequenceInOrder.attr,
	&iscsi_nacl_param_ErrorRecoveryLevel.attr,
	NULL,
};

/* End items for lio_target_nacl_param_cit */

/* Start items for lio_target_acl_cit */

static ssize_t lio_target_nacl_show_info(
	struct se_node_acl *se_nacl,
	char *page)
{
	struct iscsi_session *sess;
	struct iscsi_conn *conn;
	struct se_session *se_sess;
	ssize_t rb = 0;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (!se_sess) {
		rb += sprintf(page+rb, "No active iSCSI Session for Initiator"
			" Endpoint: %s\n", se_nacl->initiatorname);
	} else {
		sess = se_sess->fabric_sess_ptr;

		if (sess->sess_ops->InitiatorName)
			rb += sprintf(page+rb, "InitiatorName: %s\n",
				sess->sess_ops->InitiatorName);
		if (sess->sess_ops->InitiatorAlias)
			rb += sprintf(page+rb, "InitiatorAlias: %s\n",
				sess->sess_ops->InitiatorAlias);

		rb += sprintf(page+rb, "LIO Session ID: %u   "
			"ISID: 0x%02x %02x %02x %02x %02x %02x  "
			"TSIH: %hu  ", sess->sid,
			sess->isid[0], sess->isid[1], sess->isid[2],
			sess->isid[3], sess->isid[4], sess->isid[5],
			sess->tsih);
		rb += sprintf(page+rb, "SessionType: %s\n",
				(sess->sess_ops->SessionType) ?
				"Discovery" : "Normal");
		rb += sprintf(page+rb, "Session State: ");
		switch (sess->session_state) {
		case TARG_SESS_STATE_FREE:
			rb += sprintf(page+rb, "TARG_SESS_FREE\n");
			break;
		case TARG_SESS_STATE_ACTIVE:
			rb += sprintf(page+rb, "TARG_SESS_STATE_ACTIVE\n");
			break;
		case TARG_SESS_STATE_LOGGED_IN:
			rb += sprintf(page+rb, "TARG_SESS_STATE_LOGGED_IN\n");
			break;
		case TARG_SESS_STATE_FAILED:
			rb += sprintf(page+rb, "TARG_SESS_STATE_FAILED\n");
			break;
		case TARG_SESS_STATE_IN_CONTINUE:
			rb += sprintf(page+rb, "TARG_SESS_STATE_IN_CONTINUE\n");
			break;
		default:
			rb += sprintf(page+rb, "ERROR: Unknown Session"
					" State!\n");
			break;
		}

		rb += sprintf(page+rb, "---------------------[iSCSI Session"
				" Values]-----------------------\n");
		rb += sprintf(page+rb, "  CmdSN/WR  :  CmdSN/WC  :  ExpCmdSN"
				"  :  MaxCmdSN  :     ITT    :     TTT\n");
		rb += sprintf(page+rb, " 0x%08x   0x%08x   0x%08x   0x%08x"
				"   0x%08x   0x%08x\n",
			sess->cmdsn_window,
			(sess->max_cmd_sn - sess->exp_cmd_sn) + 1,
			sess->exp_cmd_sn, sess->max_cmd_sn,
			sess->init_task_tag, sess->targ_xfer_tag);
		rb += sprintf(page+rb, "----------------------[iSCSI"
				" Connections]-------------------------\n");

		spin_lock(&sess->conn_lock);
		list_for_each_entry(conn, &sess->sess_conn_list, conn_list) {
			rb += sprintf(page+rb, "CID: %hu  Connection"
					" State: ", conn->cid);
			switch (conn->conn_state) {
			case TARG_CONN_STATE_FREE:
				rb += sprintf(page+rb,
					"TARG_CONN_STATE_FREE\n");
				break;
			case TARG_CONN_STATE_XPT_UP:
				rb += sprintf(page+rb,
					"TARG_CONN_STATE_XPT_UP\n");
				break;
			case TARG_CONN_STATE_IN_LOGIN:
				rb += sprintf(page+rb,
					"TARG_CONN_STATE_IN_LOGIN\n");
				break;
			case TARG_CONN_STATE_LOGGED_IN:
				rb += sprintf(page+rb,
					"TARG_CONN_STATE_LOGGED_IN\n");
				break;
			case TARG_CONN_STATE_IN_LOGOUT:
				rb += sprintf(page+rb,
					"TARG_CONN_STATE_IN_LOGOUT\n");
				break;
			case TARG_CONN_STATE_LOGOUT_REQUESTED:
				rb += sprintf(page+rb,
					"TARG_CONN_STATE_LOGOUT_REQUESTED\n");
				break;
			case TARG_CONN_STATE_CLEANUP_WAIT:
				rb += sprintf(page+rb,
					"TARG_CONN_STATE_CLEANUP_WAIT\n");
				break;
			default:
				rb += sprintf(page+rb,
					"ERROR: Unknown Connection State!\n");
				break;
			}

			rb += sprintf(page+rb, "   Address %s %s", conn->login_ip,
				(conn->network_transport == ISCSI_TCP) ?
				"TCP" : "SCTP");
			rb += sprintf(page+rb, "  StatSN: 0x%08x\n",
				conn->stat_sn);
		}
		spin_unlock(&sess->conn_lock);
	}
	spin_unlock_bh(&se_nacl->nacl_sess_lock);

	return rb;
}

TF_NACL_BASE_ATTR_RO(lio_target, info);

static ssize_t lio_target_nacl_show_cmdsn_depth(
	struct se_node_acl *se_nacl,
	char *page)
{
	return sprintf(page, "%u\n", se_nacl->queue_depth);
}

static ssize_t lio_target_nacl_store_cmdsn_depth(
	struct se_node_acl *se_nacl,
	const char *page,
	size_t count)
{
	struct se_portal_group *se_tpg = se_nacl->se_tpg;
	struct iscsi_portal_group *tpg = container_of(se_tpg,
			struct iscsi_portal_group, tpg_se_tpg);
	struct config_item *acl_ci, *tpg_ci, *wwn_ci;
	char *endptr;
	u32 cmdsn_depth = 0;
	int ret;

	cmdsn_depth = simple_strtoul(page, &endptr, 0);
	if (cmdsn_depth > TA_DEFAULT_CMDSN_DEPTH_MAX) {
		pr_err("Passed cmdsn_depth: %u exceeds"
			" TA_DEFAULT_CMDSN_DEPTH_MAX: %u\n", cmdsn_depth,
			TA_DEFAULT_CMDSN_DEPTH_MAX);
		return -EINVAL;
	}
	acl_ci = &se_nacl->acl_group.cg_item;
	if (!acl_ci) {
		pr_err("Unable to locatel acl_ci\n");
		return -EINVAL;
	}
	tpg_ci = &acl_ci->ci_parent->ci_group->cg_item;
	if (!tpg_ci) {
		pr_err("Unable to locate tpg_ci\n");
		return -EINVAL;
	}
	wwn_ci = &tpg_ci->ci_group->cg_item;
	if (!wwn_ci) {
		pr_err("Unable to locate config_item wwn_ci\n");
		return -EINVAL;
	}

	if (iscsit_get_tpg(tpg) < 0)
		return -EINVAL;
	/*
	 * iscsit_tpg_set_initiator_node_queue_depth() assumes force=1
	 */
	ret = iscsit_tpg_set_initiator_node_queue_depth(tpg,
				config_item_name(acl_ci), cmdsn_depth, 1);

	pr_debug("LIO_Target_ConfigFS: %s/%s Set CmdSN Window: %u for"
		"InitiatorName: %s\n", config_item_name(wwn_ci),
		config_item_name(tpg_ci), cmdsn_depth,
		config_item_name(acl_ci));

	iscsit_put_tpg(tpg);
	return (!ret) ? count : (ssize_t)ret;
}

TF_NACL_BASE_ATTR(lio_target, cmdsn_depth, S_IRUGO | S_IWUSR);

static ssize_t lio_target_nacl_show_tag(
	struct se_node_acl *se_nacl,
	char *page)
{
	return snprintf(page, PAGE_SIZE, "%s", se_nacl->acl_tag);
}

static ssize_t lio_target_nacl_store_tag(
	struct se_node_acl *se_nacl,
	const char *page,
	size_t count)
{
	int ret;

	ret = core_tpg_set_initiator_node_tag(se_nacl->se_tpg, se_nacl, page);

	if (ret < 0)
		return ret;
	return count;
}

TF_NACL_BASE_ATTR(lio_target, tag, S_IRUGO | S_IWUSR);

static struct configfs_attribute *lio_target_initiator_attrs[] = {
	&lio_target_nacl_info.attr,
	&lio_target_nacl_cmdsn_depth.attr,
	&lio_target_nacl_tag.attr,
	NULL,
};

static struct se_node_acl *lio_tpg_alloc_fabric_acl(
	struct se_portal_group *se_tpg)
{
	struct iscsi_node_acl *acl;

	acl = kzalloc(sizeof(struct iscsi_node_acl), GFP_KERNEL);
	if (!acl) {
		pr_err("Unable to allocate memory for struct iscsi_node_acl\n");
		return NULL;
	}

	return &acl->se_node_acl;
}

static struct se_node_acl *lio_target_make_nodeacl(
	struct se_portal_group *se_tpg,
	struct config_group *group,
	const char *name)
{
	struct config_group *stats_cg;
	struct iscsi_node_acl *acl;
	struct se_node_acl *se_nacl_new, *se_nacl;
	struct iscsi_portal_group *tpg = container_of(se_tpg,
			struct iscsi_portal_group, tpg_se_tpg);
	u32 cmdsn_depth;

	se_nacl_new = lio_tpg_alloc_fabric_acl(se_tpg);
	if (!se_nacl_new)
		return ERR_PTR(-ENOMEM);

	cmdsn_depth = ISCSI_TPG_ATTRIB(tpg)->default_cmdsn_depth;
	/*
	 * se_nacl_new may be released by core_tpg_add_initiator_node_acl()
	 * when converting a NdoeACL from demo mode -> explict
	 */
	se_nacl = core_tpg_add_initiator_node_acl(se_tpg, se_nacl_new,
				name, cmdsn_depth);
	if (IS_ERR(se_nacl))
		return se_nacl;

	acl = container_of(se_nacl, struct iscsi_node_acl, se_node_acl);
	stats_cg = &se_nacl->acl_fabric_stat_group;

	stats_cg->default_groups = kmalloc(sizeof(struct config_group *) * 2,
				GFP_KERNEL);
	if (!stats_cg->default_groups) {
		pr_err("Unable to allocate memory for"
				" stats_cg->default_groups\n");
		core_tpg_del_initiator_node_acl(se_tpg, se_nacl, 1);
		kfree(acl);
		return ERR_PTR(-ENOMEM);
	}

	stats_cg->default_groups[0] = &NODE_STAT_GRPS(acl)->iscsi_sess_stats_group;
	stats_cg->default_groups[1] = NULL;
	config_group_init_type_name(&NODE_STAT_GRPS(acl)->iscsi_sess_stats_group,
			"iscsi_sess_stats", &iscsi_stat_sess_cit);

	return se_nacl;
}

static void lio_target_drop_nodeacl(
	struct se_node_acl *se_nacl)
{
	struct se_portal_group *se_tpg = se_nacl->se_tpg;
	struct iscsi_node_acl *acl = container_of(se_nacl,
			struct iscsi_node_acl, se_node_acl);
	struct config_item *df_item;
	struct config_group *stats_cg;
	int i;

	stats_cg = &acl->se_node_acl.acl_fabric_stat_group;
	for (i = 0; stats_cg->default_groups[i]; i++) {
		df_item = &stats_cg->default_groups[i]->cg_item;
		stats_cg->default_groups[i] = NULL;
		config_item_put(df_item);
	}
	kfree(stats_cg->default_groups);

	core_tpg_del_initiator_node_acl(se_tpg, se_nacl, 1);
	kfree(acl);
}

/* End items for lio_target_acl_cit */

/* Start items for lio_target_tpg_attrib_cit */

#define DEF_TPG_ATTRIB(name)						\
									\
static ssize_t iscsi_tpg_attrib_show_##name(				\
	struct se_portal_group *se_tpg,				\
	char *page)							\
{									\
	struct iscsi_portal_group *tpg = container_of(se_tpg,		\
			struct iscsi_portal_group, tpg_se_tpg);	\
	ssize_t rb;							\
									\
	if (iscsit_get_tpg(tpg) < 0)					\
		return -EINVAL;						\
									\
	rb = sprintf(page, "%u\n", ISCSI_TPG_ATTRIB(tpg)->name);	\
	iscsit_put_tpg(tpg);						\
	return rb;							\
}									\
									\
static ssize_t iscsi_tpg_attrib_store_##name(				\
	struct se_portal_group *se_tpg,				\
	const char *page,						\
	size_t count)							\
{									\
	struct iscsi_portal_group *tpg = container_of(se_tpg,		\
			struct iscsi_portal_group, tpg_se_tpg);	\
	char *endptr;							\
	u32 val;							\
	int ret;							\
									\
	if (iscsit_get_tpg(tpg) < 0)					\
		return -EINVAL;						\
									\
	val = simple_strtoul(page, &endptr, 0);				\
	ret = iscsit_ta_##name(tpg, val);				\
	if (ret < 0)							\
		goto out;						\
									\
	iscsit_put_tpg(tpg);						\
	return count;							\
out:									\
	iscsit_put_tpg(tpg);						\
	return ret;							\
}

#define TPG_ATTR(_name, _mode) TF_TPG_ATTRIB_ATTR(iscsi, _name, _mode);

/*
 * Define iscsi_tpg_attrib_s_authentication
 */
DEF_TPG_ATTRIB(authentication);
TPG_ATTR(authentication, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_tpg_attrib_s_login_timeout
 */
DEF_TPG_ATTRIB(login_timeout);
TPG_ATTR(login_timeout, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_tpg_attrib_s_netif_timeout
 */
DEF_TPG_ATTRIB(netif_timeout);
TPG_ATTR(netif_timeout, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_tpg_attrib_s_generate_node_acls
 */
DEF_TPG_ATTRIB(generate_node_acls);
TPG_ATTR(generate_node_acls, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_tpg_attrib_s_default_cmdsn_depth
 */
DEF_TPG_ATTRIB(default_cmdsn_depth);
TPG_ATTR(default_cmdsn_depth, S_IRUGO | S_IWUSR);
/*
 Define iscsi_tpg_attrib_s_cache_dynamic_acls
 */
DEF_TPG_ATTRIB(cache_dynamic_acls);
TPG_ATTR(cache_dynamic_acls, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_tpg_attrib_s_demo_mode_write_protect
 */
DEF_TPG_ATTRIB(demo_mode_write_protect);
TPG_ATTR(demo_mode_write_protect, S_IRUGO | S_IWUSR);
/*
 * Define iscsi_tpg_attrib_s_prod_mode_write_protect
 */
DEF_TPG_ATTRIB(prod_mode_write_protect);
TPG_ATTR(prod_mode_write_protect, S_IRUGO | S_IWUSR);

static struct configfs_attribute *lio_target_tpg_attrib_attrs[] = {
	&iscsi_tpg_attrib_authentication.attr,
	&iscsi_tpg_attrib_login_timeout.attr,
	&iscsi_tpg_attrib_netif_timeout.attr,
	&iscsi_tpg_attrib_generate_node_acls.attr,
	&iscsi_tpg_attrib_default_cmdsn_depth.attr,
	&iscsi_tpg_attrib_cache_dynamic_acls.attr,
	&iscsi_tpg_attrib_demo_mode_write_protect.attr,
	&iscsi_tpg_attrib_prod_mode_write_protect.attr,
	NULL,
};

/* End items for lio_target_tpg_attrib_cit */

/* Start items for lio_target_tpg_param_cit */

#define DEF_TPG_PARAM(name)						\
static ssize_t iscsi_tpg_param_show_##name(				\
	struct se_portal_group *se_tpg,					\
	char *page)							\
{									\
	struct iscsi_portal_group *tpg = container_of(se_tpg,		\
			struct iscsi_portal_group, tpg_se_tpg);		\
	struct iscsi_param *param;					\
	ssize_t rb;							\
									\
	if (iscsit_get_tpg(tpg) < 0)					\
		return -EINVAL;						\
									\
	param = iscsi_find_param_from_key(__stringify(name),		\
				tpg->param_list);			\
	if (!param) {							\
		iscsit_put_tpg(tpg);					\
		return -EINVAL;						\
	}								\
	rb = snprintf(page, PAGE_SIZE, "%s\n", param->value);		\
									\
	iscsit_put_tpg(tpg);						\
	return rb;							\
}									\
static ssize_t iscsi_tpg_param_store_##name(				\
	struct se_portal_group *se_tpg,				\
	const char *page,						\
	size_t count)							\
{									\
	struct iscsi_portal_group *tpg = container_of(se_tpg,		\
			struct iscsi_portal_group, tpg_se_tpg);		\
	char *buf;							\
	int ret;							\
									\
	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);				\
	if (!buf)							\
		return -ENOMEM;						\
	snprintf(buf, PAGE_SIZE, "%s=%s", __stringify(name), page);	\
	buf[strlen(buf)-1] = '\0'; /* Kill newline */			\
									\
	if (iscsit_get_tpg(tpg) < 0) {					\
		kfree(buf);						\
		return -EINVAL;						\
	}								\
									\
	ret = iscsi_change_param_value(buf, tpg->param_list, 1);	\
	if (ret < 0)							\
		goto out;						\
									\
	kfree(buf);							\
	iscsit_put_tpg(tpg);						\
	return count;							\
out:									\
	kfree(buf);							\
	iscsit_put_tpg(tpg);						\
	return -EINVAL;						\
}

#define TPG_PARAM_ATTR(_name, _mode) TF_TPG_PARAM_ATTR(iscsi, _name, _mode);

DEF_TPG_PARAM(AuthMethod);
TPG_PARAM_ATTR(AuthMethod, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(HeaderDigest);
TPG_PARAM_ATTR(HeaderDigest, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(DataDigest);
TPG_PARAM_ATTR(DataDigest, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(MaxConnections);
TPG_PARAM_ATTR(MaxConnections, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(TargetAlias);
TPG_PARAM_ATTR(TargetAlias, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(InitialR2T);
TPG_PARAM_ATTR(InitialR2T, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(ImmediateData);
TPG_PARAM_ATTR(ImmediateData, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(MaxRecvDataSegmentLength);
TPG_PARAM_ATTR(MaxRecvDataSegmentLength, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(MaxXmitDataSegmentLength);
TPG_PARAM_ATTR(MaxXmitDataSegmentLength, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(MaxBurstLength);
TPG_PARAM_ATTR(MaxBurstLength, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(FirstBurstLength);
TPG_PARAM_ATTR(FirstBurstLength, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(DefaultTime2Wait);
TPG_PARAM_ATTR(DefaultTime2Wait, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(DefaultTime2Retain);
TPG_PARAM_ATTR(DefaultTime2Retain, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(MaxOutstandingR2T);
TPG_PARAM_ATTR(MaxOutstandingR2T, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(DataPDUInOrder);
TPG_PARAM_ATTR(DataPDUInOrder, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(DataSequenceInOrder);
TPG_PARAM_ATTR(DataSequenceInOrder, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(ErrorRecoveryLevel);
TPG_PARAM_ATTR(ErrorRecoveryLevel, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(IFMarker);
TPG_PARAM_ATTR(IFMarker, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(OFMarker);
TPG_PARAM_ATTR(OFMarker, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(IFMarkInt);
TPG_PARAM_ATTR(IFMarkInt, S_IRUGO | S_IWUSR);

DEF_TPG_PARAM(OFMarkInt);
TPG_PARAM_ATTR(OFMarkInt, S_IRUGO | S_IWUSR);

static struct configfs_attribute *lio_target_tpg_param_attrs[] = {
	&iscsi_tpg_param_AuthMethod.attr,
	&iscsi_tpg_param_HeaderDigest.attr,
	&iscsi_tpg_param_DataDigest.attr,
	&iscsi_tpg_param_MaxConnections.attr,
	&iscsi_tpg_param_TargetAlias.attr,
	&iscsi_tpg_param_InitialR2T.attr,
	&iscsi_tpg_param_ImmediateData.attr,
	&iscsi_tpg_param_MaxRecvDataSegmentLength.attr,
	&iscsi_tpg_param_MaxXmitDataSegmentLength.attr,
	&iscsi_tpg_param_MaxBurstLength.attr,
	&iscsi_tpg_param_FirstBurstLength.attr,
	&iscsi_tpg_param_DefaultTime2Wait.attr,
	&iscsi_tpg_param_DefaultTime2Retain.attr,
	&iscsi_tpg_param_MaxOutstandingR2T.attr,
	&iscsi_tpg_param_DataPDUInOrder.attr,
	&iscsi_tpg_param_DataSequenceInOrder.attr,
	&iscsi_tpg_param_ErrorRecoveryLevel.attr,
	&iscsi_tpg_param_IFMarker.attr,
	&iscsi_tpg_param_OFMarker.attr,
	&iscsi_tpg_param_IFMarkInt.attr,
	&iscsi_tpg_param_OFMarkInt.attr,
	NULL,
};

/* End items for lio_target_tpg_param_cit */

/* Start items for lio_target_tpg_cit */

static ssize_t lio_target_tpg_show_enable(
	struct se_portal_group *se_tpg,
	char *page)
{
	struct iscsi_portal_group *tpg = container_of(se_tpg,
			struct iscsi_portal_group, tpg_se_tpg);
	ssize_t len;

	spin_lock(&tpg->tpg_state_lock);
	len = sprintf(page, "%d\n",
			(tpg->tpg_state == TPG_STATE_ACTIVE) ? 1 : 0);
	spin_unlock(&tpg->tpg_state_lock);

	return len;
}

static ssize_t lio_target_tpg_store_enable(
	struct se_portal_group *se_tpg,
	const char *page,
	size_t count)
{
	struct iscsi_portal_group *tpg = container_of(se_tpg,
			struct iscsi_portal_group, tpg_se_tpg);
	char *endptr;
	u32 op;
	int ret = 0;

	op = simple_strtoul(page, &endptr, 0);
	if ((op != 1) && (op != 0)) {
		pr_err("Illegal value for tpg_enable: %u\n", op);
		return -EINVAL;
	}

	ret = iscsit_get_tpg(tpg);
	if (ret < 0)
		return -EINVAL;

	if (op) {
		ret = iscsit_tpg_enable_portal_group(tpg);
		if (ret < 0)
			goto out;
	} else {
		/*
		 * iscsit_tpg_disable_portal_group() assumes force=1
		 */
		ret = iscsit_tpg_disable_portal_group(tpg, 1);
		if (ret < 0)
			goto out;
	}

	iscsit_put_tpg(tpg);
	return count;
out:
	iscsit_put_tpg(tpg);
	return -EINVAL;
}

TF_TPG_BASE_ATTR(lio_target, enable, S_IRUGO | S_IWUSR);

static struct configfs_attribute *lio_target_tpg_attrs[] = {
	&lio_target_tpg_enable.attr,
	NULL,
};

/* End items for lio_target_tpg_cit */

/* Start items for lio_target_tiqn_cit */

static struct se_portal_group *lio_target_tiqn_addtpg(
	struct se_wwn *wwn,
	struct config_group *group,
	const char *name)
{
	struct iscsi_portal_group *tpg;
	struct iscsi_tiqn *tiqn;
	char *tpgt_str, *end_ptr;
	int ret = 0;
	unsigned short int tpgt;

	tiqn = container_of(wwn, struct iscsi_tiqn, tiqn_wwn);
	/*
	 * Only tpgt_# directory groups can be created below
	 * target/iscsi/iqn.superturodiskarry/
	*/
	tpgt_str = strstr(name, "tpgt_");
	if (!tpgt_str) {
		pr_err("Unable to locate \"tpgt_#\" directory"
				" group\n");
		return NULL;
	}
	tpgt_str += 5; /* Skip ahead of "tpgt_" */
	tpgt = (unsigned short int) simple_strtoul(tpgt_str, &end_ptr, 0);

	tpg = iscsit_alloc_portal_group(tiqn, tpgt);
	if (!tpg)
		return NULL;

	ret = core_tpg_register(
			&lio_target_fabric_configfs->tf_ops,
			wwn, &tpg->tpg_se_tpg, tpg,
			TRANSPORT_TPG_TYPE_NORMAL);
	if (ret < 0)
		return NULL;

	ret = iscsit_tpg_add_portal_group(tiqn, tpg);
	if (ret != 0)
		goto out;

	pr_debug("LIO_Target_ConfigFS: REGISTER -> %s\n", tiqn->tiqn);
	pr_debug("LIO_Target_ConfigFS: REGISTER -> Allocated TPG: %s\n",
			name);
	return &tpg->tpg_se_tpg;
out:
	core_tpg_deregister(&tpg->tpg_se_tpg);
	kfree(tpg);
	return NULL;
}

static void lio_target_tiqn_deltpg(struct se_portal_group *se_tpg)
{
	struct iscsi_portal_group *tpg;
	struct iscsi_tiqn *tiqn;

	tpg = container_of(se_tpg, struct iscsi_portal_group, tpg_se_tpg);
	tiqn = tpg->tpg_tiqn;
	/*
	 * iscsit_tpg_del_portal_group() assumes force=1
	 */
	pr_debug("LIO_Target_ConfigFS: DEREGISTER -> Releasing TPG\n");
	iscsit_tpg_del_portal_group(tiqn, tpg, 1);
}

/* End items for lio_target_tiqn_cit */

/* Start LIO-Target TIQN struct contig_item lio_target_cit */

static ssize_t lio_target_wwn_show_attr_lio_version(
	struct target_fabric_configfs *tf,
	char *page)
{
	return sprintf(page, "RisingTide Systems Linux-iSCSI Target "ISCSIT_VERSION"\n");
}

TF_WWN_ATTR_RO(lio_target, lio_version);

static struct configfs_attribute *lio_target_wwn_attrs[] = {
	&lio_target_wwn_lio_version.attr,
	NULL,
};

static struct se_wwn *lio_target_call_coreaddtiqn(
	struct target_fabric_configfs *tf,
	struct config_group *group,
	const char *name)
{
	struct config_group *stats_cg;
	struct iscsi_tiqn *tiqn;

	tiqn = iscsit_add_tiqn((unsigned char *)name);
	if (IS_ERR(tiqn))
		return ERR_CAST(tiqn);
	/*
	 * Setup struct iscsi_wwn_stat_grps for se_wwn->fabric_stat_group.
	 */
	stats_cg = &tiqn->tiqn_wwn.fabric_stat_group;

	stats_cg->default_groups = kmalloc(sizeof(struct config_group *) * 6,
				GFP_KERNEL);
	if (!stats_cg->default_groups) {
		pr_err("Unable to allocate memory for"
				" stats_cg->default_groups\n");
		iscsit_del_tiqn(tiqn);
		return ERR_PTR(-ENOMEM);
	}

	stats_cg->default_groups[0] = &WWN_STAT_GRPS(tiqn)->iscsi_instance_group;
	stats_cg->default_groups[1] = &WWN_STAT_GRPS(tiqn)->iscsi_sess_err_group;
	stats_cg->default_groups[2] = &WWN_STAT_GRPS(tiqn)->iscsi_tgt_attr_group;
	stats_cg->default_groups[3] = &WWN_STAT_GRPS(tiqn)->iscsi_login_stats_group;
	stats_cg->default_groups[4] = &WWN_STAT_GRPS(tiqn)->iscsi_logout_stats_group;
	stats_cg->default_groups[5] = NULL;
	config_group_init_type_name(&WWN_STAT_GRPS(tiqn)->iscsi_instance_group,
			"iscsi_instance", &iscsi_stat_instance_cit);
	config_group_init_type_name(&WWN_STAT_GRPS(tiqn)->iscsi_sess_err_group,
			"iscsi_sess_err", &iscsi_stat_sess_err_cit);
	config_group_init_type_name(&WWN_STAT_GRPS(tiqn)->iscsi_tgt_attr_group,
			"iscsi_tgt_attr", &iscsi_stat_tgt_attr_cit);
	config_group_init_type_name(&WWN_STAT_GRPS(tiqn)->iscsi_login_stats_group,
			"iscsi_login_stats", &iscsi_stat_login_cit);
	config_group_init_type_name(&WWN_STAT_GRPS(tiqn)->iscsi_logout_stats_group,
			"iscsi_logout_stats", &iscsi_stat_logout_cit);

	pr_debug("LIO_Target_ConfigFS: REGISTER -> %s\n", tiqn->tiqn);
	pr_debug("LIO_Target_ConfigFS: REGISTER -> Allocated Node:"
			" %s\n", name);
	return &tiqn->tiqn_wwn;
}

static void lio_target_call_coredeltiqn(
	struct se_wwn *wwn)
{
	struct iscsi_tiqn *tiqn = container_of(wwn, struct iscsi_tiqn, tiqn_wwn);
	struct config_item *df_item;
	struct config_group *stats_cg;
	int i;

	stats_cg = &tiqn->tiqn_wwn.fabric_stat_group;
	for (i = 0; stats_cg->default_groups[i]; i++) {
		df_item = &stats_cg->default_groups[i]->cg_item;
		stats_cg->default_groups[i] = NULL;
		config_item_put(df_item);
	}
	kfree(stats_cg->default_groups);

	pr_debug("LIO_Target_ConfigFS: DEREGISTER -> %s\n",
			tiqn->tiqn);
	iscsit_del_tiqn(tiqn);
}

/* End LIO-Target TIQN struct contig_lio_target_cit */

/* Start lio_target_discovery_auth_cit */

#define DEF_DISC_AUTH_STR(name, flags)					\
	__DEF_NACL_AUTH_STR(disc, name, flags)				\
static ssize_t iscsi_disc_show_##name(					\
	struct target_fabric_configfs *tf,				\
	char *page)							\
{									\
	return __iscsi_disc_show_##name(&iscsit_global->discovery_acl,	\
		page);							\
}									\
static ssize_t iscsi_disc_store_##name(					\
	struct target_fabric_configfs *tf,				\
	const char *page,						\
	size_t count)							\
{									\
	return __iscsi_disc_store_##name(&iscsit_global->discovery_acl,	\
		page, count);						\
}

#define DEF_DISC_AUTH_INT(name)						\
	__DEF_NACL_AUTH_INT(disc, name)					\
static ssize_t iscsi_disc_show_##name(					\
	struct target_fabric_configfs *tf,				\
	char *page)							\
{									\
	return __iscsi_disc_show_##name(&iscsit_global->discovery_acl,	\
			page);						\
}

#define DISC_AUTH_ATTR(_name, _mode) TF_DISC_ATTR(iscsi, _name, _mode)
#define DISC_AUTH_ATTR_RO(_name) TF_DISC_ATTR_RO(iscsi, _name)

/*
 * One-way authentication userid
 */
DEF_DISC_AUTH_STR(userid, NAF_USERID_SET);
DISC_AUTH_ATTR(userid, S_IRUGO | S_IWUSR);
/*
 * One-way authentication password
 */
DEF_DISC_AUTH_STR(password, NAF_PASSWORD_SET);
DISC_AUTH_ATTR(password, S_IRUGO | S_IWUSR);
/*
 * Enforce mutual authentication
 */
DEF_DISC_AUTH_INT(authenticate_target);
DISC_AUTH_ATTR_RO(authenticate_target);
/*
 * Mutual authentication userid
 */
DEF_DISC_AUTH_STR(userid_mutual, NAF_USERID_IN_SET);
DISC_AUTH_ATTR(userid_mutual, S_IRUGO | S_IWUSR);
/*
 * Mutual authentication password
 */
DEF_DISC_AUTH_STR(password_mutual, NAF_PASSWORD_IN_SET);
DISC_AUTH_ATTR(password_mutual, S_IRUGO | S_IWUSR);

/*
 * enforce_discovery_auth
 */
static ssize_t iscsi_disc_show_enforce_discovery_auth(
	struct target_fabric_configfs *tf,
	char *page)
{
	struct iscsi_node_auth *discovery_auth = &iscsit_global->discovery_acl.node_auth;

	return sprintf(page, "%d\n", discovery_auth->enforce_discovery_auth);
}

static ssize_t iscsi_disc_store_enforce_discovery_auth(
	struct target_fabric_configfs *tf,
	const char *page,
	size_t count)
{
	struct iscsi_param *param;
	struct iscsi_portal_group *discovery_tpg = iscsit_global->discovery_tpg;
	char *endptr;
	u32 op;

	op = simple_strtoul(page, &endptr, 0);
	if ((op != 1) && (op != 0)) {
		pr_err("Illegal value for enforce_discovery_auth:"
				" %u\n", op);
		return -EINVAL;
	}

	if (!discovery_tpg) {
		pr_err("iscsit_global->discovery_tpg is NULL\n");
		return -EINVAL;
	}

	param = iscsi_find_param_from_key(AUTHMETHOD,
				discovery_tpg->param_list);
	if (!param)
		return -EINVAL;

	if (op) {
		/*
		 * Reset the AuthMethod key to CHAP.
		 */
		if (iscsi_update_param_value(param, CHAP) < 0)
			return -EINVAL;

		discovery_tpg->tpg_attrib.authentication = 1;
		iscsit_global->discovery_acl.node_auth.enforce_discovery_auth = 1;
		pr_debug("LIO-CORE[0] Successfully enabled"
			" authentication enforcement for iSCSI"
			" Discovery TPG\n");
	} else {
		/*
		 * Reset the AuthMethod key to CHAP,None
		 */
		if (iscsi_update_param_value(param, "CHAP,None") < 0)
			return -EINVAL;

		discovery_tpg->tpg_attrib.authentication = 0;
		iscsit_global->discovery_acl.node_auth.enforce_discovery_auth = 0;
		pr_debug("LIO-CORE[0] Successfully disabled"
			" authentication enforcement for iSCSI"
			" Discovery TPG\n");
	}

	return count;
}

DISC_AUTH_ATTR(enforce_discovery_auth, S_IRUGO | S_IWUSR);

static struct configfs_attribute *lio_target_discovery_auth_attrs[] = {
	&iscsi_disc_userid.attr,
	&iscsi_disc_password.attr,
	&iscsi_disc_authenticate_target.attr,
	&iscsi_disc_userid_mutual.attr,
	&iscsi_disc_password_mutual.attr,
	&iscsi_disc_enforce_discovery_auth.attr,
	NULL,
};

/* End lio_target_discovery_auth_cit */

/* Start functions for target_core_fabric_ops */

static char *iscsi_get_fabric_name(void)
{
	return "iSCSI";
}

static u32 iscsi_get_task_tag(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);

	/* only used for printks or comparism with ->ref_task_tag */
	return (__force u32)cmd->init_task_tag;
}

static int iscsi_get_cmd_state(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);

	return cmd->i_state;
}

static u32 lio_sess_get_index(struct se_session *se_sess)
{
	struct iscsi_session *sess = se_sess->fabric_sess_ptr;

	return sess->session_index;
}

static u32 lio_sess_get_initiator_sid(
	struct se_session *se_sess,
	unsigned char *buf,
	u32 size)
{
	struct iscsi_session *sess = se_sess->fabric_sess_ptr;
	/*
	 * iSCSI Initiator Session Identifier from RFC-3720.
	 */
	return snprintf(buf, size, "%02x%02x%02x%02x%02x%02x",
		sess->isid[0], sess->isid[1], sess->isid[2],
		sess->isid[3], sess->isid[4], sess->isid[5]);
}

static int lio_queue_data_in(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);

	cmd->i_state = ISTATE_SEND_DATAIN;
	cmd->conn->conn_transport->iscsit_queue_data_in(cmd->conn, cmd);

	return 0;
}

static int lio_write_pending(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);
	struct iscsi_conn *conn = cmd->conn;

	if (!cmd->immediate_data && !cmd->unsolicited_data)
		return conn->conn_transport->iscsit_get_dataout(conn, cmd, false);

	return 0;
}

static int lio_write_pending_status(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);
	int ret;

	spin_lock_bh(&cmd->istate_lock);
	ret = !(cmd->cmd_flags & ICF_GOT_LAST_DATAOUT);
	spin_unlock_bh(&cmd->istate_lock);

	return ret;
}

static int lio_queue_status(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);

	cmd->i_state = ISTATE_SEND_STATUS;
	cmd->conn->conn_transport->iscsit_queue_status(cmd->conn, cmd);

	return 0;
}

static int lio_queue_tm_rsp(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);

	cmd->i_state = ISTATE_SEND_TASKMGTRSP;
	iscsit_add_cmd_to_response_queue(cmd, cmd->conn, cmd->i_state);
	return 0;
}

static char *lio_tpg_get_endpoint_wwn(struct se_portal_group *se_tpg)
{
	struct iscsi_portal_group *tpg = se_tpg->se_tpg_fabric_ptr;

	return &tpg->tpg_tiqn->tiqn[0];
}

static u16 lio_tpg_get_tag(struct se_portal_group *se_tpg)
{
	struct iscsi_portal_group *tpg = se_tpg->se_tpg_fabric_ptr;

	return tpg->tpgt;
}

static u32 lio_tpg_get_default_depth(struct se_portal_group *se_tpg)
{
	struct iscsi_portal_group *tpg = se_tpg->se_tpg_fabric_ptr;

	return ISCSI_TPG_ATTRIB(tpg)->default_cmdsn_depth;
}

static int lio_tpg_check_demo_mode(struct se_portal_group *se_tpg)
{
	struct iscsi_portal_group *tpg = se_tpg->se_tpg_fabric_ptr;

	return ISCSI_TPG_ATTRIB(tpg)->generate_node_acls;
}

static int lio_tpg_check_demo_mode_cache(struct se_portal_group *se_tpg)
{
	struct iscsi_portal_group *tpg = se_tpg->se_tpg_fabric_ptr;

	return ISCSI_TPG_ATTRIB(tpg)->cache_dynamic_acls;
}

static int lio_tpg_check_demo_mode_write_protect(
	struct se_portal_group *se_tpg)
{
	struct iscsi_portal_group *tpg = se_tpg->se_tpg_fabric_ptr;

	return ISCSI_TPG_ATTRIB(tpg)->demo_mode_write_protect;
}

static int lio_tpg_check_prod_mode_write_protect(
	struct se_portal_group *se_tpg)
{
	struct iscsi_portal_group *tpg = se_tpg->se_tpg_fabric_ptr;

	return ISCSI_TPG_ATTRIB(tpg)->prod_mode_write_protect;
}

static void lio_tpg_release_fabric_acl(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_acl)
{
	struct iscsi_node_acl *acl = container_of(se_acl,
				struct iscsi_node_acl, se_node_acl);
	kfree(acl);
}

/*
 * Called with spin_lock_bh(struct se_portal_group->session_lock) held..
 *
 * Also, this function calls iscsit_inc_session_usage_count() on the
 * struct iscsi_session in question.
 */
static int lio_tpg_shutdown_session(struct se_session *se_sess)
{
	struct iscsi_session *sess = se_sess->fabric_sess_ptr;

	spin_lock(&sess->conn_lock);
	if (atomic_read(&sess->session_fall_back_to_erl0) ||
	    atomic_read(&sess->session_logout) ||
	    (sess->time2retain_timer_flags & ISCSI_TF_EXPIRED)) {
		spin_unlock(&sess->conn_lock);
		return 0;
	}
	atomic_set(&sess->session_reinstatement, 1);
	spin_unlock(&sess->conn_lock);

	iscsit_stop_time2retain_timer(sess);
	iscsit_stop_session(sess, 1, 1);

	return 1;
}

/*
 * Calls iscsit_dec_session_usage_count() as inverse of
 * lio_tpg_shutdown_session()
 */
static void lio_tpg_close_session(struct se_session *se_sess)
{
	struct iscsi_session *sess = se_sess->fabric_sess_ptr;
	/*
	 * If the iSCSI Session for the iSCSI Initiator Node exists,
	 * forcefully shutdown the iSCSI NEXUS.
	 */
	iscsit_close_session(sess);
}

static u32 lio_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	struct iscsi_portal_group *tpg = se_tpg->se_tpg_fabric_ptr;

	return tpg->tpg_tiqn->tiqn_index;
}

static void lio_set_default_node_attributes(struct se_node_acl *se_acl)
{
	struct iscsi_node_acl *acl = container_of(se_acl, struct iscsi_node_acl,
				se_node_acl);

	ISCSI_NODE_ATTRIB(acl)->nacl = acl;
	iscsit_set_default_node_attribues(acl);
}

static int lio_check_stop_free(struct se_cmd *se_cmd)
{
	return target_put_sess_cmd(se_cmd->se_sess, se_cmd);
}

static void lio_release_cmd(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);

	pr_debug("Entering lio_release_cmd for se_cmd: %p\n", se_cmd);
	cmd->release_cmd(cmd);
}

/* End functions for target_core_fabric_ops */

int iscsi_target_register_configfs(void)
{
	struct target_fabric_configfs *fabric;
	int ret;

	lio_target_fabric_configfs = NULL;
	fabric = target_fabric_configfs_init(THIS_MODULE, "iscsi");
	if (IS_ERR(fabric)) {
		pr_err("target_fabric_configfs_init() for"
				" LIO-Target failed!\n");
		return PTR_ERR(fabric);
	}
	/*
	 * Setup the fabric API of function pointers used by target_core_mod..
	 */
	fabric->tf_ops.get_fabric_name = &iscsi_get_fabric_name;
	fabric->tf_ops.get_fabric_proto_ident = &iscsi_get_fabric_proto_ident;
	fabric->tf_ops.tpg_get_wwn = &lio_tpg_get_endpoint_wwn;
	fabric->tf_ops.tpg_get_tag = &lio_tpg_get_tag;
	fabric->tf_ops.tpg_get_default_depth = &lio_tpg_get_default_depth;
	fabric->tf_ops.tpg_get_pr_transport_id = &iscsi_get_pr_transport_id;
	fabric->tf_ops.tpg_get_pr_transport_id_len =
				&iscsi_get_pr_transport_id_len;
	fabric->tf_ops.tpg_parse_pr_out_transport_id =
				&iscsi_parse_pr_out_transport_id;
	fabric->tf_ops.tpg_check_demo_mode = &lio_tpg_check_demo_mode;
	fabric->tf_ops.tpg_check_demo_mode_cache =
				&lio_tpg_check_demo_mode_cache;
	fabric->tf_ops.tpg_check_demo_mode_write_protect =
				&lio_tpg_check_demo_mode_write_protect;
	fabric->tf_ops.tpg_check_prod_mode_write_protect =
				&lio_tpg_check_prod_mode_write_protect;
	fabric->tf_ops.tpg_alloc_fabric_acl = &lio_tpg_alloc_fabric_acl;
	fabric->tf_ops.tpg_release_fabric_acl = &lio_tpg_release_fabric_acl;
	fabric->tf_ops.tpg_get_inst_index = &lio_tpg_get_inst_index;
	fabric->tf_ops.check_stop_free = &lio_check_stop_free,
	fabric->tf_ops.release_cmd = &lio_release_cmd;
	fabric->tf_ops.shutdown_session = &lio_tpg_shutdown_session;
	fabric->tf_ops.close_session = &lio_tpg_close_session;
	fabric->tf_ops.sess_get_index = &lio_sess_get_index;
	fabric->tf_ops.sess_get_initiator_sid = &lio_sess_get_initiator_sid;
	fabric->tf_ops.write_pending = &lio_write_pending;
	fabric->tf_ops.write_pending_status = &lio_write_pending_status;
	fabric->tf_ops.set_default_node_attributes =
				&lio_set_default_node_attributes;
	fabric->tf_ops.get_task_tag = &iscsi_get_task_tag;
	fabric->tf_ops.get_cmd_state = &iscsi_get_cmd_state;
	fabric->tf_ops.queue_data_in = &lio_queue_data_in;
	fabric->tf_ops.queue_status = &lio_queue_status;
	fabric->tf_ops.queue_tm_rsp = &lio_queue_tm_rsp;
	/*
	 * Setup function pointers for generic logic in target_core_fabric_configfs.c
	 */
	fabric->tf_ops.fabric_make_wwn = &lio_target_call_coreaddtiqn;
	fabric->tf_ops.fabric_drop_wwn = &lio_target_call_coredeltiqn;
	fabric->tf_ops.fabric_make_tpg = &lio_target_tiqn_addtpg;
	fabric->tf_ops.fabric_drop_tpg = &lio_target_tiqn_deltpg;
	fabric->tf_ops.fabric_post_link	= NULL;
	fabric->tf_ops.fabric_pre_unlink = NULL;
	fabric->tf_ops.fabric_make_np = &lio_target_call_addnptotpg;
	fabric->tf_ops.fabric_drop_np = &lio_target_call_delnpfromtpg;
	fabric->tf_ops.fabric_make_nodeacl = &lio_target_make_nodeacl;
	fabric->tf_ops.fabric_drop_nodeacl = &lio_target_drop_nodeacl;
	/*
	 * Setup default attribute lists for various fabric->tf_cit_tmpl
	 * sturct config_item_type's
	 */
	TF_CIT_TMPL(fabric)->tfc_discovery_cit.ct_attrs = lio_target_discovery_auth_attrs;
	TF_CIT_TMPL(fabric)->tfc_wwn_cit.ct_attrs = lio_target_wwn_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_base_cit.ct_attrs = lio_target_tpg_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_attrib_cit.ct_attrs = lio_target_tpg_attrib_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_param_cit.ct_attrs = lio_target_tpg_param_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_np_base_cit.ct_attrs = lio_target_portal_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_base_cit.ct_attrs = lio_target_initiator_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_attrib_cit.ct_attrs = lio_target_nacl_attrib_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_auth_cit.ct_attrs = lio_target_nacl_auth_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_param_cit.ct_attrs = lio_target_nacl_param_attrs;

	ret = target_fabric_configfs_register(fabric);
	if (ret < 0) {
		pr_err("target_fabric_configfs_register() for"
				" LIO-Target failed!\n");
		target_fabric_configfs_free(fabric);
		return ret;
	}

	lio_target_fabric_configfs = fabric;
	pr_debug("LIO_TARGET[0] - Set fabric ->"
			" lio_target_fabric_configfs\n");
	return 0;
}


void iscsi_target_deregister_configfs(void)
{
	if (!lio_target_fabric_configfs)
		return;
	/*
	 * Shutdown discovery sessions and disable discovery TPG
	 */
	if (iscsit_global->discovery_tpg)
		iscsit_tpg_disable_portal_group(iscsit_global->discovery_tpg, 1);

	target_fabric_configfs_deregister(lio_target_fabric_configfs);
	lio_target_fabric_configfs = NULL;
	pr_debug("LIO_TARGET[0] - Cleared"
				" lio_target_fabric_configfs\n");
}
