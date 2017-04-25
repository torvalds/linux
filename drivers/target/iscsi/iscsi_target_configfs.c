/*******************************************************************************
 * This file contains the configfs implementation for iSCSI Target mode
 * from the LIO-Target Project.
 *
 * (c) Copyright 2007-2013 Datera, Inc.
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
#include <linux/ctype.h>
#include <linux/export.h>
#include <linux/inet.h>
#include <linux/module.h>
#include <net/ipv6.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/iscsi/iscsi_transport.h>
#include <target/iscsi/iscsi_target_core.h>
#include "iscsi_target_parameters.h"
#include "iscsi_target_device.h"
#include "iscsi_target_erl0.h"
#include "iscsi_target_nodeattrib.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target.h"
#include <target/iscsi/iscsi_target_stat.h>


/* Start items for lio_target_portal_cit */

static inline struct iscsi_tpg_np *to_iscsi_tpg_np(struct config_item *item)
{
	return container_of(to_tpg_np(item), struct iscsi_tpg_np, se_tpg_np);
}

static ssize_t lio_target_np_driver_show(struct config_item *item, char *page,
					 enum iscsit_transport_type type)
{
	struct iscsi_tpg_np *tpg_np = to_iscsi_tpg_np(item);
	struct iscsi_tpg_np *tpg_np_new;
	ssize_t rb;

	tpg_np_new = iscsit_tpg_locate_child_np(tpg_np, type);
	if (tpg_np_new)
		rb = sprintf(page, "1\n");
	else
		rb = sprintf(page, "0\n");

	return rb;
}

static ssize_t lio_target_np_driver_store(struct config_item *item,
		const char *page, size_t count, enum iscsit_transport_type type,
		const char *mod_name)
{
	struct iscsi_tpg_np *tpg_np = to_iscsi_tpg_np(item);
	struct iscsi_np *np;
	struct iscsi_portal_group *tpg;
	struct iscsi_tpg_np *tpg_np_new = NULL;
	u32 op;
	int rc;

	rc = kstrtou32(page, 0, &op);
	if (rc)
		return rc;
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
		if (strlen(mod_name)) {
			rc = request_module(mod_name);
			if (rc != 0) {
				pr_warn("Unable to request_module for %s\n",
					mod_name);
				rc = 0;
			}
		}

		tpg_np_new = iscsit_tpg_add_network_portal(tpg,
					&np->np_sockaddr, tpg_np, type);
		if (IS_ERR(tpg_np_new)) {
			rc = PTR_ERR(tpg_np_new);
			goto out;
		}
	} else {
		tpg_np_new = iscsit_tpg_locate_child_np(tpg_np, type);
		if (tpg_np_new) {
			rc = iscsit_tpg_del_network_portal(tpg, tpg_np_new);
			if (rc < 0)
				goto out;
		}
	}

	iscsit_put_tpg(tpg);
	return count;
out:
	iscsit_put_tpg(tpg);
	return rc;
}

static ssize_t lio_target_np_iser_show(struct config_item *item, char *page)
{
	return lio_target_np_driver_show(item, page, ISCSI_INFINIBAND);
}

static ssize_t lio_target_np_iser_store(struct config_item *item,
					const char *page, size_t count)
{
	return lio_target_np_driver_store(item, page, count,
					  ISCSI_INFINIBAND, "ib_isert");
}
CONFIGFS_ATTR(lio_target_np_, iser);

static ssize_t lio_target_np_cxgbit_show(struct config_item *item, char *page)
{
	return lio_target_np_driver_show(item, page, ISCSI_CXGBIT);
}

static ssize_t lio_target_np_cxgbit_store(struct config_item *item,
					  const char *page, size_t count)
{
	return lio_target_np_driver_store(item, page, count,
					  ISCSI_CXGBIT, "cxgbit");
}
CONFIGFS_ATTR(lio_target_np_, cxgbit);

static struct configfs_attribute *lio_target_portal_attrs[] = {
	&lio_target_np_attr_iser,
	&lio_target_np_attr_cxgbit,
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
	struct sockaddr_storage sockaddr = { };
	int ret;
	char buf[MAX_PORTAL_LEN + 1];

	if (strlen(name) > MAX_PORTAL_LEN) {
		pr_err("strlen(name): %d exceeds MAX_PORTAL_LEN: %d\n",
			(int)strlen(name), MAX_PORTAL_LEN);
		return ERR_PTR(-EOVERFLOW);
	}
	memset(buf, 0, MAX_PORTAL_LEN + 1);
	snprintf(buf, MAX_PORTAL_LEN + 1, "%s", name);

	str = strstr(buf, "[");
	if (str) {
		str2 = strstr(str, "]");
		if (!str2) {
			pr_err("Unable to locate trailing \"]\""
				" in IPv6 iSCSI network portal address\n");
			return ERR_PTR(-EINVAL);
		}

		ip_str = str + 1; /* Skip over leading "[" */
		*str2 = '\0'; /* Terminate the unbracketed IPv6 address */
		str2++; /* Skip over the \0 */

		port_str = strstr(str2, ":");
		if (!port_str) {
			pr_err("Unable to locate \":port\""
				" in IPv6 iSCSI network portal address\n");
			return ERR_PTR(-EINVAL);
		}
		*port_str = '\0'; /* Terminate string for IP */
		port_str++; /* Skip over ":" */
	} else {
		ip_str = &buf[0];
		port_str = strstr(ip_str, ":");
		if (!port_str) {
			pr_err("Unable to locate \":port\""
				" in IPv4 iSCSI network portal address\n");
			return ERR_PTR(-EINVAL);
		}
		*port_str = '\0'; /* Terminate string for IP */
		port_str++; /* Skip over ":" */
	}

	ret = inet_pton_with_scope(&init_net, AF_UNSPEC, ip_str,
			port_str, &sockaddr);
	if (ret) {
		pr_err("malformed ip/port passed: %s\n", name);
		return ERR_PTR(ret);
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
	tpg_np = iscsit_tpg_add_network_portal(tpg, &sockaddr, NULL,
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
		" PORTAL: %pISpc\n", config_item_name(&se_tpg->se_tpg_wwn->wwn_group.cg_item),
		tpg->tpgt, &tpg_np->tpg_np->np_sockaddr);

	ret = iscsit_tpg_del_network_portal(tpg, tpg_np);
	if (ret < 0)
		goto out;

	pr_debug("LIO_Target_ConfigFS: delnpfromtpg done!\n");
out:
	iscsit_put_tpg(tpg);
}

/* End items for lio_target_np_cit */

/* Start items for lio_target_nacl_attrib_cit */

#define ISCSI_NACL_ATTR(name)						\
static ssize_t iscsi_nacl_attrib_##name##_show(struct config_item *item,\
		char *page)						\
{									\
	struct se_node_acl *se_nacl = attrib_to_nacl(item);		\
	struct iscsi_node_acl *nacl = container_of(se_nacl, struct iscsi_node_acl, \
					se_node_acl);			\
									\
	return sprintf(page, "%u\n", nacl->node_attrib.name);		\
}									\
									\
static ssize_t iscsi_nacl_attrib_##name##_store(struct config_item *item,\
		const char *page, size_t count)				\
{									\
	struct se_node_acl *se_nacl = attrib_to_nacl(item);		\
	struct iscsi_node_acl *nacl = container_of(se_nacl, struct iscsi_node_acl, \
					se_node_acl);			\
	u32 val;							\
	int ret;							\
									\
	ret = kstrtou32(page, 0, &val);					\
	if (ret)							\
		return ret;						\
	ret = iscsit_na_##name(nacl, val);				\
	if (ret < 0)							\
		return ret;						\
									\
	return count;							\
}									\
									\
CONFIGFS_ATTR(iscsi_nacl_attrib_, name)

ISCSI_NACL_ATTR(dataout_timeout);
ISCSI_NACL_ATTR(dataout_timeout_retries);
ISCSI_NACL_ATTR(default_erl);
ISCSI_NACL_ATTR(nopin_timeout);
ISCSI_NACL_ATTR(nopin_response_timeout);
ISCSI_NACL_ATTR(random_datain_pdu_offsets);
ISCSI_NACL_ATTR(random_datain_seq_offsets);
ISCSI_NACL_ATTR(random_r2t_offsets);

static struct configfs_attribute *lio_target_nacl_attrib_attrs[] = {
	&iscsi_nacl_attrib_attr_dataout_timeout,
	&iscsi_nacl_attrib_attr_dataout_timeout_retries,
	&iscsi_nacl_attrib_attr_default_erl,
	&iscsi_nacl_attrib_attr_nopin_timeout,
	&iscsi_nacl_attrib_attr_nopin_response_timeout,
	&iscsi_nacl_attrib_attr_random_datain_pdu_offsets,
	&iscsi_nacl_attrib_attr_random_datain_seq_offsets,
	&iscsi_nacl_attrib_attr_random_r2t_offsets,
	NULL,
};

/* End items for lio_target_nacl_attrib_cit */

/* Start items for lio_target_nacl_auth_cit */

#define __DEF_NACL_AUTH_STR(prefix, name, flags)			\
static ssize_t __iscsi_##prefix##_##name##_show(			\
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
static ssize_t __iscsi_##prefix##_##name##_store(			\
	struct iscsi_node_acl *nacl,					\
	const char *page,						\
	size_t count)							\
{									\
	struct iscsi_node_auth *auth = &nacl->node_auth;		\
									\
	if (!capable(CAP_SYS_ADMIN))					\
		return -EPERM;						\
	if (count >= sizeof(auth->name))				\
		return -EINVAL;						\
	snprintf(auth->name, sizeof(auth->name), "%s", page);		\
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

#define DEF_NACL_AUTH_STR(name, flags)					\
	__DEF_NACL_AUTH_STR(nacl_auth, name, flags)			\
static ssize_t iscsi_nacl_auth_##name##_show(struct config_item *item,	\
		char *page)						\
{									\
	struct se_node_acl *nacl = auth_to_nacl(item);			\
	return __iscsi_nacl_auth_##name##_show(container_of(nacl,	\
			struct iscsi_node_acl, se_node_acl), page);	\
}									\
static ssize_t iscsi_nacl_auth_##name##_store(struct config_item *item,	\
		const char *page, size_t count)				\
{									\
	struct se_node_acl *nacl = auth_to_nacl(item);			\
	return __iscsi_nacl_auth_##name##_store(container_of(nacl,	\
			struct iscsi_node_acl, se_node_acl), page, count); \
}									\
									\
CONFIGFS_ATTR(iscsi_nacl_auth_, name)

/*
 * One-way authentication userid
 */
DEF_NACL_AUTH_STR(userid, NAF_USERID_SET);
DEF_NACL_AUTH_STR(password, NAF_PASSWORD_SET);
DEF_NACL_AUTH_STR(userid_mutual, NAF_USERID_IN_SET);
DEF_NACL_AUTH_STR(password_mutual, NAF_PASSWORD_IN_SET);

#define __DEF_NACL_AUTH_INT(prefix, name)				\
static ssize_t __iscsi_##prefix##_##name##_show(				\
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

#define DEF_NACL_AUTH_INT(name)						\
	__DEF_NACL_AUTH_INT(nacl_auth, name)				\
static ssize_t iscsi_nacl_auth_##name##_show(struct config_item *item,	\
		char *page)						\
{									\
	struct se_node_acl *nacl = auth_to_nacl(item);			\
	return __iscsi_nacl_auth_##name##_show(container_of(nacl,	\
			struct iscsi_node_acl, se_node_acl), page);	\
}									\
									\
CONFIGFS_ATTR_RO(iscsi_nacl_auth_, name)

DEF_NACL_AUTH_INT(authenticate_target);

static struct configfs_attribute *lio_target_nacl_auth_attrs[] = {
	&iscsi_nacl_auth_attr_userid,
	&iscsi_nacl_auth_attr_password,
	&iscsi_nacl_auth_attr_authenticate_target,
	&iscsi_nacl_auth_attr_userid_mutual,
	&iscsi_nacl_auth_attr_password_mutual,
	NULL,
};

/* End items for lio_target_nacl_auth_cit */

/* Start items for lio_target_nacl_param_cit */

#define ISCSI_NACL_PARAM(name)						\
static ssize_t iscsi_nacl_param_##name##_show(struct config_item *item,	\
		char *page)						\
{									\
	struct se_node_acl *se_nacl = param_to_nacl(item);		\
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
}									\
									\
CONFIGFS_ATTR_RO(iscsi_nacl_param_, name)

ISCSI_NACL_PARAM(MaxConnections);
ISCSI_NACL_PARAM(InitialR2T);
ISCSI_NACL_PARAM(ImmediateData);
ISCSI_NACL_PARAM(MaxBurstLength);
ISCSI_NACL_PARAM(FirstBurstLength);
ISCSI_NACL_PARAM(DefaultTime2Wait);
ISCSI_NACL_PARAM(DefaultTime2Retain);
ISCSI_NACL_PARAM(MaxOutstandingR2T);
ISCSI_NACL_PARAM(DataPDUInOrder);
ISCSI_NACL_PARAM(DataSequenceInOrder);
ISCSI_NACL_PARAM(ErrorRecoveryLevel);

static struct configfs_attribute *lio_target_nacl_param_attrs[] = {
	&iscsi_nacl_param_attr_MaxConnections,
	&iscsi_nacl_param_attr_InitialR2T,
	&iscsi_nacl_param_attr_ImmediateData,
	&iscsi_nacl_param_attr_MaxBurstLength,
	&iscsi_nacl_param_attr_FirstBurstLength,
	&iscsi_nacl_param_attr_DefaultTime2Wait,
	&iscsi_nacl_param_attr_DefaultTime2Retain,
	&iscsi_nacl_param_attr_MaxOutstandingR2T,
	&iscsi_nacl_param_attr_DataPDUInOrder,
	&iscsi_nacl_param_attr_DataSequenceInOrder,
	&iscsi_nacl_param_attr_ErrorRecoveryLevel,
	NULL,
};

/* End items for lio_target_nacl_param_cit */

/* Start items for lio_target_acl_cit */

static ssize_t lio_target_nacl_info_show(struct config_item *item, char *page)
{
	struct se_node_acl *se_nacl = acl_to_nacl(item);
	struct iscsi_session *sess;
	struct iscsi_conn *conn;
	struct se_session *se_sess;
	ssize_t rb = 0;
	u32 max_cmd_sn;

	spin_lock_bh(&se_nacl->nacl_sess_lock);
	se_sess = se_nacl->nacl_sess;
	if (!se_sess) {
		rb += sprintf(page+rb, "No active iSCSI Session for Initiator"
			" Endpoint: %s\n", se_nacl->initiatorname);
	} else {
		sess = se_sess->fabric_sess_ptr;

		rb += sprintf(page+rb, "InitiatorName: %s\n",
			sess->sess_ops->InitiatorName);
		rb += sprintf(page+rb, "InitiatorAlias: %s\n",
			sess->sess_ops->InitiatorAlias);

		rb += sprintf(page+rb,
			      "LIO Session ID: %u   ISID: 0x%6ph  TSIH: %hu  ",
			      sess->sid, sess->isid, sess->tsih);
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
		max_cmd_sn = (u32) atomic_read(&sess->max_cmd_sn);
		rb += sprintf(page+rb, " 0x%08x   0x%08x   0x%08x   0x%08x"
				"   0x%08x   0x%08x\n",
			sess->cmdsn_window,
			(max_cmd_sn - sess->exp_cmd_sn) + 1,
			sess->exp_cmd_sn, max_cmd_sn,
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

			rb += sprintf(page+rb, "   Address %pISc %s", &conn->login_sockaddr,
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

static ssize_t lio_target_nacl_cmdsn_depth_show(struct config_item *item,
		char *page)
{
	return sprintf(page, "%u\n", acl_to_nacl(item)->queue_depth);
}

static ssize_t lio_target_nacl_cmdsn_depth_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_node_acl *se_nacl = acl_to_nacl(item);
	struct se_portal_group *se_tpg = se_nacl->se_tpg;
	struct iscsi_portal_group *tpg = container_of(se_tpg,
			struct iscsi_portal_group, tpg_se_tpg);
	struct config_item *acl_ci, *tpg_ci, *wwn_ci;
	u32 cmdsn_depth = 0;
	int ret;

	ret = kstrtou32(page, 0, &cmdsn_depth);
	if (ret)
		return ret;
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

	ret = core_tpg_set_initiator_node_queue_depth(se_nacl, cmdsn_depth);

	pr_debug("LIO_Target_ConfigFS: %s/%s Set CmdSN Window: %u for"
		"InitiatorName: %s\n", config_item_name(wwn_ci),
		config_item_name(tpg_ci), cmdsn_depth,
		config_item_name(acl_ci));

	iscsit_put_tpg(tpg);
	return (!ret) ? count : (ssize_t)ret;
}

static ssize_t lio_target_nacl_tag_show(struct config_item *item, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s", acl_to_nacl(item)->acl_tag);
}

static ssize_t lio_target_nacl_tag_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_node_acl *se_nacl = acl_to_nacl(item);
	int ret;

	ret = core_tpg_set_initiator_node_tag(se_nacl->se_tpg, se_nacl, page);

	if (ret < 0)
		return ret;
	return count;
}

CONFIGFS_ATTR_RO(lio_target_nacl_, info);
CONFIGFS_ATTR(lio_target_nacl_, cmdsn_depth);
CONFIGFS_ATTR(lio_target_nacl_, tag);

static struct configfs_attribute *lio_target_initiator_attrs[] = {
	&lio_target_nacl_attr_info,
	&lio_target_nacl_attr_cmdsn_depth,
	&lio_target_nacl_attr_tag,
	NULL,
};

static int lio_target_init_nodeacl(struct se_node_acl *se_nacl,
		const char *name)
{
	struct iscsi_node_acl *acl =
		container_of(se_nacl, struct iscsi_node_acl, se_node_acl);

	config_group_init_type_name(&acl->node_stat_grps.iscsi_sess_stats_group,
			"iscsi_sess_stats", &iscsi_stat_sess_cit);
	configfs_add_default_group(&acl->node_stat_grps.iscsi_sess_stats_group,
			&se_nacl->acl_fabric_stat_group);
	return 0;
}

/* End items for lio_target_acl_cit */

/* Start items for lio_target_tpg_attrib_cit */

#define DEF_TPG_ATTRIB(name)						\
									\
static ssize_t iscsi_tpg_attrib_##name##_show(struct config_item *item,	\
		char *page)						\
{									\
	struct se_portal_group *se_tpg = attrib_to_tpg(item);		\
	struct iscsi_portal_group *tpg = container_of(se_tpg,		\
			struct iscsi_portal_group, tpg_se_tpg);	\
	ssize_t rb;							\
									\
	if (iscsit_get_tpg(tpg) < 0)					\
		return -EINVAL;						\
									\
	rb = sprintf(page, "%u\n", tpg->tpg_attrib.name);		\
	iscsit_put_tpg(tpg);						\
	return rb;							\
}									\
									\
static ssize_t iscsi_tpg_attrib_##name##_store(struct config_item *item,\
		const char *page, size_t count)				\
{									\
	struct se_portal_group *se_tpg = attrib_to_tpg(item);		\
	struct iscsi_portal_group *tpg = container_of(se_tpg,		\
			struct iscsi_portal_group, tpg_se_tpg);	\
	u32 val;							\
	int ret;							\
									\
	if (iscsit_get_tpg(tpg) < 0)					\
		return -EINVAL;						\
									\
	ret = kstrtou32(page, 0, &val);					\
	if (ret)							\
		goto out;						\
	ret = iscsit_ta_##name(tpg, val);				\
	if (ret < 0)							\
		goto out;						\
									\
	iscsit_put_tpg(tpg);						\
	return count;							\
out:									\
	iscsit_put_tpg(tpg);						\
	return ret;							\
}									\
CONFIGFS_ATTR(iscsi_tpg_attrib_, name)

DEF_TPG_ATTRIB(authentication);
DEF_TPG_ATTRIB(login_timeout);
DEF_TPG_ATTRIB(netif_timeout);
DEF_TPG_ATTRIB(generate_node_acls);
DEF_TPG_ATTRIB(default_cmdsn_depth);
DEF_TPG_ATTRIB(cache_dynamic_acls);
DEF_TPG_ATTRIB(demo_mode_write_protect);
DEF_TPG_ATTRIB(prod_mode_write_protect);
DEF_TPG_ATTRIB(demo_mode_discovery);
DEF_TPG_ATTRIB(default_erl);
DEF_TPG_ATTRIB(t10_pi);
DEF_TPG_ATTRIB(fabric_prot_type);
DEF_TPG_ATTRIB(tpg_enabled_sendtargets);

static struct configfs_attribute *lio_target_tpg_attrib_attrs[] = {
	&iscsi_tpg_attrib_attr_authentication,
	&iscsi_tpg_attrib_attr_login_timeout,
	&iscsi_tpg_attrib_attr_netif_timeout,
	&iscsi_tpg_attrib_attr_generate_node_acls,
	&iscsi_tpg_attrib_attr_default_cmdsn_depth,
	&iscsi_tpg_attrib_attr_cache_dynamic_acls,
	&iscsi_tpg_attrib_attr_demo_mode_write_protect,
	&iscsi_tpg_attrib_attr_prod_mode_write_protect,
	&iscsi_tpg_attrib_attr_demo_mode_discovery,
	&iscsi_tpg_attrib_attr_default_erl,
	&iscsi_tpg_attrib_attr_t10_pi,
	&iscsi_tpg_attrib_attr_fabric_prot_type,
	&iscsi_tpg_attrib_attr_tpg_enabled_sendtargets,
	NULL,
};

/* End items for lio_target_tpg_attrib_cit */

/* Start items for lio_target_tpg_auth_cit */

#define __DEF_TPG_AUTH_STR(prefix, name, flags)					\
static ssize_t __iscsi_##prefix##_##name##_show(struct se_portal_group *se_tpg,	\
		char *page)							\
{										\
	struct iscsi_portal_group *tpg = container_of(se_tpg,			\
				struct iscsi_portal_group, tpg_se_tpg);		\
	struct iscsi_node_auth *auth = &tpg->tpg_demo_auth;			\
										\
	if (!capable(CAP_SYS_ADMIN))						\
		return -EPERM;							\
										\
	return snprintf(page, PAGE_SIZE, "%s\n", auth->name);			\
}										\
										\
static ssize_t __iscsi_##prefix##_##name##_store(struct se_portal_group *se_tpg,\
		const char *page, size_t count)					\
{										\
	struct iscsi_portal_group *tpg = container_of(se_tpg,			\
				struct iscsi_portal_group, tpg_se_tpg);		\
	struct iscsi_node_auth *auth = &tpg->tpg_demo_auth;			\
										\
	if (!capable(CAP_SYS_ADMIN))						\
		return -EPERM;							\
										\
	snprintf(auth->name, sizeof(auth->name), "%s", page);			\
	if (!(strncmp("NULL", auth->name, 4)))					\
		auth->naf_flags &= ~flags;					\
	else									\
		auth->naf_flags |= flags;					\
										\
	if ((auth->naf_flags & NAF_USERID_IN_SET) &&				\
	    (auth->naf_flags & NAF_PASSWORD_IN_SET))				\
		auth->authenticate_target = 1;					\
	else									\
		auth->authenticate_target = 0;					\
										\
	return count;								\
}

#define DEF_TPG_AUTH_STR(name, flags)						\
	__DEF_TPG_AUTH_STR(tpg_auth, name, flags)				\
static ssize_t iscsi_tpg_auth_##name##_show(struct config_item *item,		\
		char *page)							\
{										\
	return __iscsi_tpg_auth_##name##_show(auth_to_tpg(item), page);		\
}										\
										\
static ssize_t iscsi_tpg_auth_##name##_store(struct config_item *item,		\
		const char *page, size_t count)					\
{										\
	return __iscsi_tpg_auth_##name##_store(auth_to_tpg(item), page, count);	\
}										\
										\
CONFIGFS_ATTR(iscsi_tpg_auth_, name);


DEF_TPG_AUTH_STR(userid, NAF_USERID_SET);
DEF_TPG_AUTH_STR(password, NAF_PASSWORD_SET);
DEF_TPG_AUTH_STR(userid_mutual, NAF_USERID_IN_SET);
DEF_TPG_AUTH_STR(password_mutual, NAF_PASSWORD_IN_SET);

#define __DEF_TPG_AUTH_INT(prefix, name)					\
static ssize_t __iscsi_##prefix##_##name##_show(struct se_portal_group *se_tpg,	\
		char *page)								\
{										\
	struct iscsi_portal_group *tpg = container_of(se_tpg,			\
				struct iscsi_portal_group, tpg_se_tpg);		\
	struct iscsi_node_auth *auth = &tpg->tpg_demo_auth;			\
										\
	if (!capable(CAP_SYS_ADMIN))						\
		return -EPERM;							\
										\
	return snprintf(page, PAGE_SIZE, "%d\n", auth->name);			\
}

#define DEF_TPG_AUTH_INT(name)							\
	__DEF_TPG_AUTH_INT(tpg_auth, name)					\
static ssize_t iscsi_tpg_auth_##name##_show(struct config_item *item,		\
		char *page) \
{										\
	return __iscsi_tpg_auth_##name##_show(auth_to_tpg(item), page);		\
}										\
CONFIGFS_ATTR_RO(iscsi_tpg_auth_, name);

DEF_TPG_AUTH_INT(authenticate_target);

static struct configfs_attribute *lio_target_tpg_auth_attrs[] = {
	&iscsi_tpg_auth_attr_userid,
	&iscsi_tpg_auth_attr_password,
	&iscsi_tpg_auth_attr_authenticate_target,
	&iscsi_tpg_auth_attr_userid_mutual,
	&iscsi_tpg_auth_attr_password_mutual,
	NULL,
};

/* End items for lio_target_tpg_auth_cit */

/* Start items for lio_target_tpg_param_cit */

#define DEF_TPG_PARAM(name)						\
static ssize_t iscsi_tpg_param_##name##_show(struct config_item *item,	\
		char *page)						\
{									\
	struct se_portal_group *se_tpg = param_to_tpg(item);		\
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
static ssize_t iscsi_tpg_param_##name##_store(struct config_item *item, \
		const char *page, size_t count)				\
{									\
	struct se_portal_group *se_tpg = param_to_tpg(item);		\
	struct iscsi_portal_group *tpg = container_of(se_tpg,		\
			struct iscsi_portal_group, tpg_se_tpg);		\
	char *buf;							\
	int ret, len;							\
									\
	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);				\
	if (!buf)							\
		return -ENOMEM;						\
	len = snprintf(buf, PAGE_SIZE, "%s=%s", __stringify(name), page);	\
	if (isspace(buf[len-1]))					\
		buf[len-1] = '\0'; /* Kill newline */			\
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
	return -EINVAL;							\
}									\
CONFIGFS_ATTR(iscsi_tpg_param_, name)

DEF_TPG_PARAM(AuthMethod);
DEF_TPG_PARAM(HeaderDigest);
DEF_TPG_PARAM(DataDigest);
DEF_TPG_PARAM(MaxConnections);
DEF_TPG_PARAM(TargetAlias);
DEF_TPG_PARAM(InitialR2T);
DEF_TPG_PARAM(ImmediateData);
DEF_TPG_PARAM(MaxRecvDataSegmentLength);
DEF_TPG_PARAM(MaxXmitDataSegmentLength);
DEF_TPG_PARAM(MaxBurstLength);
DEF_TPG_PARAM(FirstBurstLength);
DEF_TPG_PARAM(DefaultTime2Wait);
DEF_TPG_PARAM(DefaultTime2Retain);
DEF_TPG_PARAM(MaxOutstandingR2T);
DEF_TPG_PARAM(DataPDUInOrder);
DEF_TPG_PARAM(DataSequenceInOrder);
DEF_TPG_PARAM(ErrorRecoveryLevel);
DEF_TPG_PARAM(IFMarker);
DEF_TPG_PARAM(OFMarker);
DEF_TPG_PARAM(IFMarkInt);
DEF_TPG_PARAM(OFMarkInt);

static struct configfs_attribute *lio_target_tpg_param_attrs[] = {
	&iscsi_tpg_param_attr_AuthMethod,
	&iscsi_tpg_param_attr_HeaderDigest,
	&iscsi_tpg_param_attr_DataDigest,
	&iscsi_tpg_param_attr_MaxConnections,
	&iscsi_tpg_param_attr_TargetAlias,
	&iscsi_tpg_param_attr_InitialR2T,
	&iscsi_tpg_param_attr_ImmediateData,
	&iscsi_tpg_param_attr_MaxRecvDataSegmentLength,
	&iscsi_tpg_param_attr_MaxXmitDataSegmentLength,
	&iscsi_tpg_param_attr_MaxBurstLength,
	&iscsi_tpg_param_attr_FirstBurstLength,
	&iscsi_tpg_param_attr_DefaultTime2Wait,
	&iscsi_tpg_param_attr_DefaultTime2Retain,
	&iscsi_tpg_param_attr_MaxOutstandingR2T,
	&iscsi_tpg_param_attr_DataPDUInOrder,
	&iscsi_tpg_param_attr_DataSequenceInOrder,
	&iscsi_tpg_param_attr_ErrorRecoveryLevel,
	&iscsi_tpg_param_attr_IFMarker,
	&iscsi_tpg_param_attr_OFMarker,
	&iscsi_tpg_param_attr_IFMarkInt,
	&iscsi_tpg_param_attr_OFMarkInt,
	NULL,
};

/* End items for lio_target_tpg_param_cit */

/* Start items for lio_target_tpg_cit */

static ssize_t lio_target_tpg_enable_show(struct config_item *item, char *page)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct iscsi_portal_group *tpg = container_of(se_tpg,
			struct iscsi_portal_group, tpg_se_tpg);
	ssize_t len;

	spin_lock(&tpg->tpg_state_lock);
	len = sprintf(page, "%d\n",
			(tpg->tpg_state == TPG_STATE_ACTIVE) ? 1 : 0);
	spin_unlock(&tpg->tpg_state_lock);

	return len;
}

static ssize_t lio_target_tpg_enable_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct iscsi_portal_group *tpg = container_of(se_tpg,
			struct iscsi_portal_group, tpg_se_tpg);
	u32 op;
	int ret;

	ret = kstrtou32(page, 0, &op);
	if (ret)
		return ret;
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


static ssize_t lio_target_tpg_dynamic_sessions_show(struct config_item *item,
		char *page)
{
	return target_show_dynamic_sessions(to_tpg(item), page);
}

CONFIGFS_ATTR(lio_target_tpg_, enable);
CONFIGFS_ATTR_RO(lio_target_tpg_, dynamic_sessions);

static struct configfs_attribute *lio_target_tpg_attrs[] = {
	&lio_target_tpg_attr_enable,
	&lio_target_tpg_attr_dynamic_sessions,
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
	char *tpgt_str;
	int ret;
	u16 tpgt;

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
	ret = kstrtou16(tpgt_str, 0, &tpgt);
	if (ret)
		return NULL;

	tpg = iscsit_alloc_portal_group(tiqn, tpgt);
	if (!tpg)
		return NULL;

	ret = core_tpg_register(wwn, &tpg->tpg_se_tpg, SCSI_PROTOCOL_ISCSI);
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

static ssize_t lio_target_wwn_lio_version_show(struct config_item *item,
		char *page)
{
	return sprintf(page, "Datera Inc. iSCSI Target "ISCSIT_VERSION"\n");
}

CONFIGFS_ATTR_RO(lio_target_wwn_, lio_version);

static struct configfs_attribute *lio_target_wwn_attrs[] = {
	&lio_target_wwn_attr_lio_version,
	NULL,
};

static struct se_wwn *lio_target_call_coreaddtiqn(
	struct target_fabric_configfs *tf,
	struct config_group *group,
	const char *name)
{
	struct iscsi_tiqn *tiqn;

	tiqn = iscsit_add_tiqn((unsigned char *)name);
	if (IS_ERR(tiqn))
		return ERR_CAST(tiqn);

	pr_debug("LIO_Target_ConfigFS: REGISTER -> %s\n", tiqn->tiqn);
	pr_debug("LIO_Target_ConfigFS: REGISTER -> Allocated Node:"
			" %s\n", name);
	return &tiqn->tiqn_wwn;
}

static void lio_target_add_wwn_groups(struct se_wwn *wwn)
{
	struct iscsi_tiqn *tiqn = container_of(wwn, struct iscsi_tiqn, tiqn_wwn);

	config_group_init_type_name(&tiqn->tiqn_stat_grps.iscsi_instance_group,
			"iscsi_instance", &iscsi_stat_instance_cit);
	configfs_add_default_group(&tiqn->tiqn_stat_grps.iscsi_instance_group,
			&tiqn->tiqn_wwn.fabric_stat_group);

	config_group_init_type_name(&tiqn->tiqn_stat_grps.iscsi_sess_err_group,
			"iscsi_sess_err", &iscsi_stat_sess_err_cit);
	configfs_add_default_group(&tiqn->tiqn_stat_grps.iscsi_sess_err_group,
			&tiqn->tiqn_wwn.fabric_stat_group);

	config_group_init_type_name(&tiqn->tiqn_stat_grps.iscsi_tgt_attr_group,
			"iscsi_tgt_attr", &iscsi_stat_tgt_attr_cit);
	configfs_add_default_group(&tiqn->tiqn_stat_grps.iscsi_tgt_attr_group,
			&tiqn->tiqn_wwn.fabric_stat_group);

	config_group_init_type_name(&tiqn->tiqn_stat_grps.iscsi_login_stats_group,
			"iscsi_login_stats", &iscsi_stat_login_cit);
	configfs_add_default_group(&tiqn->tiqn_stat_grps.iscsi_login_stats_group,
			&tiqn->tiqn_wwn.fabric_stat_group);

	config_group_init_type_name(&tiqn->tiqn_stat_grps.iscsi_logout_stats_group,
			"iscsi_logout_stats", &iscsi_stat_logout_cit);
	configfs_add_default_group(&tiqn->tiqn_stat_grps.iscsi_logout_stats_group,
			&tiqn->tiqn_wwn.fabric_stat_group);
}

static void lio_target_call_coredeltiqn(
	struct se_wwn *wwn)
{
	struct iscsi_tiqn *tiqn = container_of(wwn, struct iscsi_tiqn, tiqn_wwn);

	pr_debug("LIO_Target_ConfigFS: DEREGISTER -> %s\n",
			tiqn->tiqn);
	iscsit_del_tiqn(tiqn);
}

/* End LIO-Target TIQN struct contig_lio_target_cit */

/* Start lio_target_discovery_auth_cit */

#define DEF_DISC_AUTH_STR(name, flags)					\
	__DEF_NACL_AUTH_STR(disc, name, flags)				\
static ssize_t iscsi_disc_##name##_show(struct config_item *item, char *page) \
{									\
	return __iscsi_disc_##name##_show(&iscsit_global->discovery_acl,\
		page);							\
}									\
static ssize_t iscsi_disc_##name##_store(struct config_item *item,	\
		const char *page, size_t count)				\
{									\
	return __iscsi_disc_##name##_store(&iscsit_global->discovery_acl,	\
		page, count);						\
									\
}									\
CONFIGFS_ATTR(iscsi_disc_, name)

DEF_DISC_AUTH_STR(userid, NAF_USERID_SET);
DEF_DISC_AUTH_STR(password, NAF_PASSWORD_SET);
DEF_DISC_AUTH_STR(userid_mutual, NAF_USERID_IN_SET);
DEF_DISC_AUTH_STR(password_mutual, NAF_PASSWORD_IN_SET);

#define DEF_DISC_AUTH_INT(name)						\
	__DEF_NACL_AUTH_INT(disc, name)					\
static ssize_t iscsi_disc_##name##_show(struct config_item *item, char *page) \
{									\
	return __iscsi_disc_##name##_show(&iscsit_global->discovery_acl, \
			page);						\
}									\
CONFIGFS_ATTR_RO(iscsi_disc_, name)

DEF_DISC_AUTH_INT(authenticate_target);


static ssize_t iscsi_disc_enforce_discovery_auth_show(struct config_item *item,
		char *page)
{
	struct iscsi_node_auth *discovery_auth = &iscsit_global->discovery_acl.node_auth;

	return sprintf(page, "%d\n", discovery_auth->enforce_discovery_auth);
}

static ssize_t iscsi_disc_enforce_discovery_auth_store(struct config_item *item,
		const char *page, size_t count)
{
	struct iscsi_param *param;
	struct iscsi_portal_group *discovery_tpg = iscsit_global->discovery_tpg;
	u32 op;
	int err;

	err = kstrtou32(page, 0, &op);
	if (err)
		return -EINVAL;
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

CONFIGFS_ATTR(iscsi_disc_, enforce_discovery_auth);

static struct configfs_attribute *lio_target_discovery_auth_attrs[] = {
	&iscsi_disc_attr_userid,
	&iscsi_disc_attr_password,
	&iscsi_disc_attr_authenticate_target,
	&iscsi_disc_attr_userid_mutual,
	&iscsi_disc_attr_password_mutual,
	&iscsi_disc_attr_enforce_discovery_auth,
	NULL,
};

/* End lio_target_discovery_auth_cit */

/* Start functions for target_core_fabric_ops */

static char *iscsi_get_fabric_name(void)
{
	return "iSCSI";
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
	return snprintf(buf, size, "%6phN", sess->isid);
}

static int lio_queue_data_in(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);
	struct iscsi_conn *conn = cmd->conn;

	cmd->i_state = ISTATE_SEND_DATAIN;
	return conn->conn_transport->iscsit_queue_data_in(conn, cmd);
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
	struct iscsi_conn *conn = cmd->conn;

	cmd->i_state = ISTATE_SEND_STATUS;

	if (cmd->se_cmd.scsi_status || cmd->sense_reason) {
		return iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
	}
	return conn->conn_transport->iscsit_queue_status(conn, cmd);
}

static void lio_queue_tm_rsp(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);

	cmd->i_state = ISTATE_SEND_TASKMGTRSP;
	iscsit_add_cmd_to_response_queue(cmd, cmd->conn, cmd->i_state);
}

static void lio_aborted_task(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);

	cmd->conn->conn_transport->iscsit_aborted_task(cmd->conn, cmd);
}

static inline struct iscsi_portal_group *iscsi_tpg(struct se_portal_group *se_tpg)
{
	return container_of(se_tpg, struct iscsi_portal_group, tpg_se_tpg);
}

static char *lio_tpg_get_endpoint_wwn(struct se_portal_group *se_tpg)
{
	return iscsi_tpg(se_tpg)->tpg_tiqn->tiqn;
}

static u16 lio_tpg_get_tag(struct se_portal_group *se_tpg)
{
	return iscsi_tpg(se_tpg)->tpgt;
}

static u32 lio_tpg_get_default_depth(struct se_portal_group *se_tpg)
{
	return iscsi_tpg(se_tpg)->tpg_attrib.default_cmdsn_depth;
}

static int lio_tpg_check_demo_mode(struct se_portal_group *se_tpg)
{
	return iscsi_tpg(se_tpg)->tpg_attrib.generate_node_acls;
}

static int lio_tpg_check_demo_mode_cache(struct se_portal_group *se_tpg)
{
	return iscsi_tpg(se_tpg)->tpg_attrib.cache_dynamic_acls;
}

static int lio_tpg_check_demo_mode_write_protect(
	struct se_portal_group *se_tpg)
{
	return iscsi_tpg(se_tpg)->tpg_attrib.demo_mode_write_protect;
}

static int lio_tpg_check_prod_mode_write_protect(
	struct se_portal_group *se_tpg)
{
	return iscsi_tpg(se_tpg)->tpg_attrib.prod_mode_write_protect;
}

static int lio_tpg_check_prot_fabric_only(
	struct se_portal_group *se_tpg)
{
	/*
	 * Only report fabric_prot_type if t10_pi has also been enabled
	 * for incoming ib_isert sessions.
	 */
	if (!iscsi_tpg(se_tpg)->tpg_attrib.t10_pi)
		return 0;
	return iscsi_tpg(se_tpg)->tpg_attrib.fabric_prot_type;
}

/*
 * This function calls iscsit_inc_session_usage_count() on the
 * struct iscsi_session in question.
 */
static void lio_tpg_close_session(struct se_session *se_sess)
{
	struct iscsi_session *sess = se_sess->fabric_sess_ptr;
	struct se_portal_group *se_tpg = &sess->tpg->tpg_se_tpg;

	spin_lock_bh(&se_tpg->session_lock);
	spin_lock(&sess->conn_lock);
	if (atomic_read(&sess->session_fall_back_to_erl0) ||
	    atomic_read(&sess->session_logout) ||
	    (sess->time2retain_timer_flags & ISCSI_TF_EXPIRED)) {
		spin_unlock(&sess->conn_lock);
		spin_unlock_bh(&se_tpg->session_lock);
		return;
	}
	atomic_set(&sess->session_reinstatement, 1);
	spin_unlock(&sess->conn_lock);

	iscsit_stop_time2retain_timer(sess);
	spin_unlock_bh(&se_tpg->session_lock);

	iscsit_stop_session(sess, 1, 1);
	iscsit_close_session(sess);
}

static u32 lio_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	return iscsi_tpg(se_tpg)->tpg_tiqn->tiqn_index;
}

static void lio_set_default_node_attributes(struct se_node_acl *se_acl)
{
	struct iscsi_node_acl *acl = container_of(se_acl, struct iscsi_node_acl,
				se_node_acl);
	struct se_portal_group *se_tpg = se_acl->se_tpg;
	struct iscsi_portal_group *tpg = container_of(se_tpg,
				struct iscsi_portal_group, tpg_se_tpg);

	acl->node_attrib.nacl = acl;
	iscsit_set_default_node_attribues(acl, tpg);
}

static int lio_check_stop_free(struct se_cmd *se_cmd)
{
	return target_put_sess_cmd(se_cmd);
}

static void lio_release_cmd(struct se_cmd *se_cmd)
{
	struct iscsi_cmd *cmd = container_of(se_cmd, struct iscsi_cmd, se_cmd);

	pr_debug("Entering lio_release_cmd for se_cmd: %p\n", se_cmd);
	iscsit_release_cmd(cmd);
}

const struct target_core_fabric_ops iscsi_ops = {
	.module				= THIS_MODULE,
	.name				= "iscsi",
	.node_acl_size			= sizeof(struct iscsi_node_acl),
	.get_fabric_name		= iscsi_get_fabric_name,
	.tpg_get_wwn			= lio_tpg_get_endpoint_wwn,
	.tpg_get_tag			= lio_tpg_get_tag,
	.tpg_get_default_depth		= lio_tpg_get_default_depth,
	.tpg_check_demo_mode		= lio_tpg_check_demo_mode,
	.tpg_check_demo_mode_cache	= lio_tpg_check_demo_mode_cache,
	.tpg_check_demo_mode_write_protect =
			lio_tpg_check_demo_mode_write_protect,
	.tpg_check_prod_mode_write_protect =
			lio_tpg_check_prod_mode_write_protect,
	.tpg_check_prot_fabric_only	= &lio_tpg_check_prot_fabric_only,
	.tpg_get_inst_index		= lio_tpg_get_inst_index,
	.check_stop_free		= lio_check_stop_free,
	.release_cmd			= lio_release_cmd,
	.close_session			= lio_tpg_close_session,
	.sess_get_index			= lio_sess_get_index,
	.sess_get_initiator_sid		= lio_sess_get_initiator_sid,
	.write_pending			= lio_write_pending,
	.write_pending_status		= lio_write_pending_status,
	.set_default_node_attributes	= lio_set_default_node_attributes,
	.get_cmd_state			= iscsi_get_cmd_state,
	.queue_data_in			= lio_queue_data_in,
	.queue_status			= lio_queue_status,
	.queue_tm_rsp			= lio_queue_tm_rsp,
	.aborted_task			= lio_aborted_task,
	.fabric_make_wwn		= lio_target_call_coreaddtiqn,
	.fabric_drop_wwn		= lio_target_call_coredeltiqn,
	.add_wwn_groups			= lio_target_add_wwn_groups,
	.fabric_make_tpg		= lio_target_tiqn_addtpg,
	.fabric_drop_tpg		= lio_target_tiqn_deltpg,
	.fabric_make_np			= lio_target_call_addnptotpg,
	.fabric_drop_np			= lio_target_call_delnpfromtpg,
	.fabric_init_nodeacl		= lio_target_init_nodeacl,

	.tfc_discovery_attrs		= lio_target_discovery_auth_attrs,
	.tfc_wwn_attrs			= lio_target_wwn_attrs,
	.tfc_tpg_base_attrs		= lio_target_tpg_attrs,
	.tfc_tpg_attrib_attrs		= lio_target_tpg_attrib_attrs,
	.tfc_tpg_auth_attrs		= lio_target_tpg_auth_attrs,
	.tfc_tpg_param_attrs		= lio_target_tpg_param_attrs,
	.tfc_tpg_np_base_attrs		= lio_target_portal_attrs,
	.tfc_tpg_nacl_base_attrs	= lio_target_initiator_attrs,
	.tfc_tpg_nacl_attrib_attrs	= lio_target_nacl_attrib_attrs,
	.tfc_tpg_nacl_auth_attrs	= lio_target_nacl_auth_attrs,
	.tfc_tpg_nacl_param_attrs	= lio_target_nacl_param_attrs,
};
