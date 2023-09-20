// SPDX-License-Identifier: GPL-2.0-or-later
/*******************************************************************************
 * This file contains main functions related to iSCSI Parameter negotiation.
 *
 * (c) Copyright 2007-2013 Datera, Inc.
 *
 * Author: Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 ******************************************************************************/

#include <linux/slab.h>
#include <linux/uio.h> /* struct kvec */
#include <target/iscsi/iscsi_target_core.h>
#include "iscsi_target_util.h"
#include "iscsi_target_parameters.h"

int iscsi_login_rx_data(
	struct iscsit_conn *conn,
	char *buf,
	int length)
{
	int rx_got;
	struct kvec iov;

	memset(&iov, 0, sizeof(struct kvec));
	iov.iov_len	= length;
	iov.iov_base	= buf;

	rx_got = rx_data(conn, &iov, 1, length);
	if (rx_got != length) {
		pr_err("rx_data returned %d, expecting %d.\n",
				rx_got, length);
		return -1;
	}

	return 0 ;
}

int iscsi_login_tx_data(
	struct iscsit_conn *conn,
	char *pdu_buf,
	char *text_buf,
	int text_length)
{
	int length, tx_sent, iov_cnt = 1;
	struct kvec iov[2];

	length = (ISCSI_HDR_LEN + text_length);

	memset(&iov[0], 0, 2 * sizeof(struct kvec));
	iov[0].iov_len		= ISCSI_HDR_LEN;
	iov[0].iov_base		= pdu_buf;

	if (text_buf && text_length) {
		iov[1].iov_len	= text_length;
		iov[1].iov_base	= text_buf;
		iov_cnt++;
	}

	tx_sent = tx_data(conn, &iov[0], iov_cnt, length);
	if (tx_sent != length) {
		pr_err("tx_data returned %d, expecting %d.\n",
				tx_sent, length);
		return -1;
	}

	return 0;
}

void iscsi_dump_conn_ops(struct iscsi_conn_ops *conn_ops)
{
	pr_debug("HeaderDigest: %s\n", (conn_ops->HeaderDigest) ?
				"CRC32C" : "None");
	pr_debug("DataDigest: %s\n", (conn_ops->DataDigest) ?
				"CRC32C" : "None");
	pr_debug("MaxRecvDataSegmentLength: %u\n",
				conn_ops->MaxRecvDataSegmentLength);
}

void iscsi_dump_sess_ops(struct iscsi_sess_ops *sess_ops)
{
	pr_debug("InitiatorName: %s\n", sess_ops->InitiatorName);
	pr_debug("InitiatorAlias: %s\n", sess_ops->InitiatorAlias);
	pr_debug("TargetName: %s\n", sess_ops->TargetName);
	pr_debug("TargetAlias: %s\n", sess_ops->TargetAlias);
	pr_debug("TargetPortalGroupTag: %hu\n",
			sess_ops->TargetPortalGroupTag);
	pr_debug("MaxConnections: %hu\n", sess_ops->MaxConnections);
	pr_debug("InitialR2T: %s\n",
			(sess_ops->InitialR2T) ? "Yes" : "No");
	pr_debug("ImmediateData: %s\n", (sess_ops->ImmediateData) ?
			"Yes" : "No");
	pr_debug("MaxBurstLength: %u\n", sess_ops->MaxBurstLength);
	pr_debug("FirstBurstLength: %u\n", sess_ops->FirstBurstLength);
	pr_debug("DefaultTime2Wait: %hu\n", sess_ops->DefaultTime2Wait);
	pr_debug("DefaultTime2Retain: %hu\n",
			sess_ops->DefaultTime2Retain);
	pr_debug("MaxOutstandingR2T: %hu\n",
			sess_ops->MaxOutstandingR2T);
	pr_debug("DataPDUInOrder: %s\n",
			(sess_ops->DataPDUInOrder) ? "Yes" : "No");
	pr_debug("DataSequenceInOrder: %s\n",
			(sess_ops->DataSequenceInOrder) ? "Yes" : "No");
	pr_debug("ErrorRecoveryLevel: %hu\n",
			sess_ops->ErrorRecoveryLevel);
	pr_debug("SessionType: %s\n", (sess_ops->SessionType) ?
			"Discovery" : "Normal");
}

void iscsi_print_params(struct iscsi_param_list *param_list)
{
	struct iscsi_param *param;

	list_for_each_entry(param, &param_list->param_list, p_list)
		pr_debug("%s: %s\n", param->name, param->value);
}

static struct iscsi_param *iscsi_set_default_param(struct iscsi_param_list *param_list,
		char *name, char *value, u8 phase, u8 scope, u8 sender,
		u16 type_range, u8 use)
{
	struct iscsi_param *param = NULL;

	param = kzalloc(sizeof(struct iscsi_param), GFP_KERNEL);
	if (!param) {
		pr_err("Unable to allocate memory for parameter.\n");
		goto out;
	}
	INIT_LIST_HEAD(&param->p_list);

	param->name = kstrdup(name, GFP_KERNEL);
	if (!param->name) {
		pr_err("Unable to allocate memory for parameter name.\n");
		goto out;
	}

	param->value = kstrdup(value, GFP_KERNEL);
	if (!param->value) {
		pr_err("Unable to allocate memory for parameter value.\n");
		goto out;
	}

	param->phase		= phase;
	param->scope		= scope;
	param->sender		= sender;
	param->use		= use;
	param->type_range	= type_range;

	switch (param->type_range) {
	case TYPERANGE_BOOL_AND:
		param->type = TYPE_BOOL_AND;
		break;
	case TYPERANGE_BOOL_OR:
		param->type = TYPE_BOOL_OR;
		break;
	case TYPERANGE_0_TO_2:
	case TYPERANGE_0_TO_3600:
	case TYPERANGE_0_TO_32767:
	case TYPERANGE_0_TO_65535:
	case TYPERANGE_1_TO_65535:
	case TYPERANGE_2_TO_3600:
	case TYPERANGE_512_TO_16777215:
		param->type = TYPE_NUMBER;
		break;
	case TYPERANGE_AUTH:
	case TYPERANGE_DIGEST:
		param->type = TYPE_VALUE_LIST | TYPE_STRING;
		break;
	case TYPERANGE_ISCSINAME:
	case TYPERANGE_SESSIONTYPE:
	case TYPERANGE_TARGETADDRESS:
	case TYPERANGE_UTF8:
		param->type = TYPE_STRING;
		break;
	default:
		pr_err("Unknown type_range 0x%02x\n",
				param->type_range);
		goto out;
	}
	list_add_tail(&param->p_list, &param_list->param_list);

	return param;
out:
	if (param) {
		kfree(param->value);
		kfree(param->name);
		kfree(param);
	}

	return NULL;
}

/* #warning Add extension keys */
int iscsi_create_default_params(struct iscsi_param_list **param_list_ptr)
{
	struct iscsi_param *param = NULL;
	struct iscsi_param_list *pl;

	pl = kzalloc(sizeof(struct iscsi_param_list), GFP_KERNEL);
	if (!pl) {
		pr_err("Unable to allocate memory for"
				" struct iscsi_param_list.\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&pl->param_list);
	INIT_LIST_HEAD(&pl->extra_response_list);

	/*
	 * The format for setting the initial parameter definitions are:
	 *
	 * Parameter name:
	 * Initial value:
	 * Allowable phase:
	 * Scope:
	 * Allowable senders:
	 * Typerange:
	 * Use:
	 */
	param = iscsi_set_default_param(pl, AUTHMETHOD, INITIAL_AUTHMETHOD,
			PHASE_SECURITY, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_AUTH, USE_INITIAL_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, HEADERDIGEST, INITIAL_HEADERDIGEST,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_DIGEST, USE_INITIAL_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, DATADIGEST, INITIAL_DATADIGEST,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_DIGEST, USE_INITIAL_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, MAXCONNECTIONS,
			INITIAL_MAXCONNECTIONS, PHASE_OPERATIONAL,
			SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_1_TO_65535, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, SENDTARGETS, INITIAL_SENDTARGETS,
			PHASE_FFP0, SCOPE_SESSION_WIDE, SENDER_INITIATOR,
			TYPERANGE_UTF8, 0);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, TARGETNAME, INITIAL_TARGETNAME,
			PHASE_DECLARATIVE, SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_ISCSINAME, USE_ALL);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, INITIATORNAME,
			INITIAL_INITIATORNAME, PHASE_DECLARATIVE,
			SCOPE_SESSION_WIDE, SENDER_INITIATOR,
			TYPERANGE_ISCSINAME, USE_INITIAL_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, TARGETALIAS, INITIAL_TARGETALIAS,
			PHASE_DECLARATIVE, SCOPE_SESSION_WIDE, SENDER_TARGET,
			TYPERANGE_UTF8, USE_ALL);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, INITIATORALIAS,
			INITIAL_INITIATORALIAS, PHASE_DECLARATIVE,
			SCOPE_SESSION_WIDE, SENDER_INITIATOR, TYPERANGE_UTF8,
			USE_ALL);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, TARGETADDRESS,
			INITIAL_TARGETADDRESS, PHASE_DECLARATIVE,
			SCOPE_SESSION_WIDE, SENDER_TARGET,
			TYPERANGE_TARGETADDRESS, USE_ALL);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, TARGETPORTALGROUPTAG,
			INITIAL_TARGETPORTALGROUPTAG,
			PHASE_DECLARATIVE, SCOPE_SESSION_WIDE, SENDER_TARGET,
			TYPERANGE_0_TO_65535, USE_INITIAL_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, INITIALR2T, INITIAL_INITIALR2T,
			PHASE_OPERATIONAL, SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_BOOL_OR, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, IMMEDIATEDATA,
			INITIAL_IMMEDIATEDATA, PHASE_OPERATIONAL,
			SCOPE_SESSION_WIDE, SENDER_BOTH, TYPERANGE_BOOL_AND,
			USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, MAXXMITDATASEGMENTLENGTH,
			INITIAL_MAXXMITDATASEGMENTLENGTH,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_512_TO_16777215, USE_ALL);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, MAXRECVDATASEGMENTLENGTH,
			INITIAL_MAXRECVDATASEGMENTLENGTH,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_512_TO_16777215, USE_ALL);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, MAXBURSTLENGTH,
			INITIAL_MAXBURSTLENGTH, PHASE_OPERATIONAL,
			SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_512_TO_16777215, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, FIRSTBURSTLENGTH,
			INITIAL_FIRSTBURSTLENGTH,
			PHASE_OPERATIONAL, SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_512_TO_16777215, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, DEFAULTTIME2WAIT,
			INITIAL_DEFAULTTIME2WAIT,
			PHASE_OPERATIONAL, SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_0_TO_3600, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, DEFAULTTIME2RETAIN,
			INITIAL_DEFAULTTIME2RETAIN,
			PHASE_OPERATIONAL, SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_0_TO_3600, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, MAXOUTSTANDINGR2T,
			INITIAL_MAXOUTSTANDINGR2T,
			PHASE_OPERATIONAL, SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_1_TO_65535, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, DATAPDUINORDER,
			INITIAL_DATAPDUINORDER, PHASE_OPERATIONAL,
			SCOPE_SESSION_WIDE, SENDER_BOTH, TYPERANGE_BOOL_OR,
			USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, DATASEQUENCEINORDER,
			INITIAL_DATASEQUENCEINORDER,
			PHASE_OPERATIONAL, SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_BOOL_OR, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, ERRORRECOVERYLEVEL,
			INITIAL_ERRORRECOVERYLEVEL,
			PHASE_OPERATIONAL, SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_0_TO_2, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, SESSIONTYPE, INITIAL_SESSIONTYPE,
			PHASE_DECLARATIVE, SCOPE_SESSION_WIDE, SENDER_INITIATOR,
			TYPERANGE_SESSIONTYPE, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, IFMARKER, INITIAL_IFMARKER,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_BOOL_AND, USE_INITIAL_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, OFMARKER, INITIAL_OFMARKER,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_BOOL_AND, USE_INITIAL_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, IFMARKINT, INITIAL_IFMARKINT,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_UTF8, USE_INITIAL_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, OFMARKINT, INITIAL_OFMARKINT,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_UTF8, USE_INITIAL_ONLY);
	if (!param)
		goto out;

	/*
	 * Extra parameters for ISER from RFC-5046
	 */
	param = iscsi_set_default_param(pl, RDMAEXTENSIONS, INITIAL_RDMAEXTENSIONS,
			PHASE_OPERATIONAL, SCOPE_SESSION_WIDE, SENDER_BOTH,
			TYPERANGE_BOOL_AND, USE_LEADING_ONLY);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, INITIATORRECVDATASEGMENTLENGTH,
			INITIAL_INITIATORRECVDATASEGMENTLENGTH,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_512_TO_16777215, USE_ALL);
	if (!param)
		goto out;

	param = iscsi_set_default_param(pl, TARGETRECVDATASEGMENTLENGTH,
			INITIAL_TARGETRECVDATASEGMENTLENGTH,
			PHASE_OPERATIONAL, SCOPE_CONNECTION_ONLY, SENDER_BOTH,
			TYPERANGE_512_TO_16777215, USE_ALL);
	if (!param)
		goto out;

	*param_list_ptr = pl;
	return 0;
out:
	iscsi_release_param_list(pl);
	return -1;
}

int iscsi_set_keys_to_negotiate(
	struct iscsi_param_list *param_list,
	bool iser)
{
	struct iscsi_param *param;

	param_list->iser = iser;

	list_for_each_entry(param, &param_list->param_list, p_list) {
		param->state = 0;
		if (!strcmp(param->name, AUTHMETHOD)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, HEADERDIGEST)) {
			if (!iser)
				SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, DATADIGEST)) {
			if (!iser)
				SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, MAXCONNECTIONS)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, TARGETNAME)) {
			continue;
		} else if (!strcmp(param->name, INITIATORNAME)) {
			continue;
		} else if (!strcmp(param->name, TARGETALIAS)) {
			if (param->value)
				SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, INITIATORALIAS)) {
			continue;
		} else if (!strcmp(param->name, TARGETPORTALGROUPTAG)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, INITIALR2T)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, IMMEDIATEDATA)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, MAXRECVDATASEGMENTLENGTH)) {
			if (!iser)
				SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, MAXXMITDATASEGMENTLENGTH)) {
			continue;
		} else if (!strcmp(param->name, MAXBURSTLENGTH)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, FIRSTBURSTLENGTH)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, DEFAULTTIME2WAIT)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, DEFAULTTIME2RETAIN)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, MAXOUTSTANDINGR2T)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, DATAPDUINORDER)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, DATASEQUENCEINORDER)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, ERRORRECOVERYLEVEL)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, SESSIONTYPE)) {
			SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, IFMARKER)) {
			SET_PSTATE_REJECT(param);
		} else if (!strcmp(param->name, OFMARKER)) {
			SET_PSTATE_REJECT(param);
		} else if (!strcmp(param->name, IFMARKINT)) {
			SET_PSTATE_REJECT(param);
		} else if (!strcmp(param->name, OFMARKINT)) {
			SET_PSTATE_REJECT(param);
		} else if (!strcmp(param->name, RDMAEXTENSIONS)) {
			if (iser)
				SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, INITIATORRECVDATASEGMENTLENGTH)) {
			if (iser)
				SET_PSTATE_NEGOTIATE(param);
		} else if (!strcmp(param->name, TARGETRECVDATASEGMENTLENGTH)) {
			if (iser)
				SET_PSTATE_NEGOTIATE(param);
		}
	}

	return 0;
}

int iscsi_set_keys_irrelevant_for_discovery(
	struct iscsi_param_list *param_list)
{
	struct iscsi_param *param;

	list_for_each_entry(param, &param_list->param_list, p_list) {
		if (!strcmp(param->name, MAXCONNECTIONS))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, INITIALR2T))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, IMMEDIATEDATA))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, MAXBURSTLENGTH))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, FIRSTBURSTLENGTH))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, MAXOUTSTANDINGR2T))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, DATAPDUINORDER))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, DATASEQUENCEINORDER))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, ERRORRECOVERYLEVEL))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, DEFAULTTIME2WAIT))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, DEFAULTTIME2RETAIN))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, IFMARKER))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, OFMARKER))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, IFMARKINT))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, OFMARKINT))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, RDMAEXTENSIONS))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, INITIATORRECVDATASEGMENTLENGTH))
			param->state &= ~PSTATE_NEGOTIATE;
		else if (!strcmp(param->name, TARGETRECVDATASEGMENTLENGTH))
			param->state &= ~PSTATE_NEGOTIATE;
	}

	return 0;
}

int iscsi_copy_param_list(
	struct iscsi_param_list **dst_param_list,
	struct iscsi_param_list *src_param_list,
	int leading)
{
	struct iscsi_param *param = NULL;
	struct iscsi_param *new_param = NULL;
	struct iscsi_param_list *param_list = NULL;

	param_list = kzalloc(sizeof(struct iscsi_param_list), GFP_KERNEL);
	if (!param_list) {
		pr_err("Unable to allocate memory for struct iscsi_param_list.\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&param_list->param_list);
	INIT_LIST_HEAD(&param_list->extra_response_list);

	list_for_each_entry(param, &src_param_list->param_list, p_list) {
		if (!leading && (param->scope & SCOPE_SESSION_WIDE)) {
			if ((strcmp(param->name, "TargetName") != 0) &&
			    (strcmp(param->name, "InitiatorName") != 0) &&
			    (strcmp(param->name, "TargetPortalGroupTag") != 0))
				continue;
		}

		new_param = kzalloc(sizeof(struct iscsi_param), GFP_KERNEL);
		if (!new_param) {
			pr_err("Unable to allocate memory for struct iscsi_param.\n");
			goto err_out;
		}

		new_param->name = kstrdup(param->name, GFP_KERNEL);
		new_param->value = kstrdup(param->value, GFP_KERNEL);
		if (!new_param->value || !new_param->name) {
			kfree(new_param->value);
			kfree(new_param->name);
			kfree(new_param);
			pr_err("Unable to allocate memory for parameter name/value.\n");
			goto err_out;
		}

		new_param->set_param = param->set_param;
		new_param->phase = param->phase;
		new_param->scope = param->scope;
		new_param->sender = param->sender;
		new_param->type = param->type;
		new_param->use = param->use;
		new_param->type_range = param->type_range;

		list_add_tail(&new_param->p_list, &param_list->param_list);
	}

	if (!list_empty(&param_list->param_list)) {
		*dst_param_list = param_list;
	} else {
		pr_err("No parameters allocated.\n");
		goto err_out;
	}

	return 0;

err_out:
	iscsi_release_param_list(param_list);
	return -ENOMEM;
}

static void iscsi_release_extra_responses(struct iscsi_param_list *param_list)
{
	struct iscsi_extra_response *er, *er_tmp;

	list_for_each_entry_safe(er, er_tmp, &param_list->extra_response_list,
			er_list) {
		list_del(&er->er_list);
		kfree(er);
	}
}

void iscsi_release_param_list(struct iscsi_param_list *param_list)
{
	struct iscsi_param *param, *param_tmp;

	list_for_each_entry_safe(param, param_tmp, &param_list->param_list,
			p_list) {
		list_del(&param->p_list);

		kfree(param->name);
		kfree(param->value);
		kfree(param);
	}

	iscsi_release_extra_responses(param_list);

	kfree(param_list);
}

struct iscsi_param *iscsi_find_param_from_key(
	char *key,
	struct iscsi_param_list *param_list)
{
	struct iscsi_param *param;

	if (!key || !param_list) {
		pr_err("Key or parameter list pointer is NULL.\n");
		return NULL;
	}

	list_for_each_entry(param, &param_list->param_list, p_list) {
		if (!strcmp(key, param->name))
			return param;
	}

	pr_err("Unable to locate key \"%s\".\n", key);
	return NULL;
}
EXPORT_SYMBOL(iscsi_find_param_from_key);

int iscsi_extract_key_value(char *textbuf, char **key, char **value)
{
	*value = strchr(textbuf, '=');
	if (!*value) {
		pr_err("Unable to locate \"=\" separator for key,"
				" ignoring request.\n");
		return -1;
	}

	*key = textbuf;
	**value = '\0';
	*value = *value + 1;

	return 0;
}

int iscsi_update_param_value(struct iscsi_param *param, char *value)
{
	kfree(param->value);

	param->value = kstrdup(value, GFP_KERNEL);
	if (!param->value) {
		pr_err("Unable to allocate memory for value.\n");
		return -ENOMEM;
	}

	pr_debug("iSCSI Parameter updated to %s=%s\n",
			param->name, param->value);
	return 0;
}

static int iscsi_add_notunderstood_response(
	char *key,
	char *value,
	struct iscsi_param_list *param_list)
{
	struct iscsi_extra_response *extra_response;

	if (strlen(value) > VALUE_MAXLEN) {
		pr_err("Value for notunderstood key \"%s\" exceeds %d,"
			" protocol error.\n", key, VALUE_MAXLEN);
		return -1;
	}

	extra_response = kzalloc(sizeof(struct iscsi_extra_response), GFP_KERNEL);
	if (!extra_response) {
		pr_err("Unable to allocate memory for"
			" struct iscsi_extra_response.\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&extra_response->er_list);

	strscpy(extra_response->key, key, sizeof(extra_response->key));
	strscpy(extra_response->value, NOTUNDERSTOOD,
		sizeof(extra_response->value));

	list_add_tail(&extra_response->er_list,
			&param_list->extra_response_list);
	return 0;
}

static int iscsi_check_for_auth_key(char *key)
{
	/*
	 * RFC 1994
	 */
	if (!strcmp(key, "CHAP_A") || !strcmp(key, "CHAP_I") ||
	    !strcmp(key, "CHAP_C") || !strcmp(key, "CHAP_N") ||
	    !strcmp(key, "CHAP_R"))
		return 1;

	/*
	 * RFC 2945
	 */
	if (!strcmp(key, "SRP_U") || !strcmp(key, "SRP_N") ||
	    !strcmp(key, "SRP_g") || !strcmp(key, "SRP_s") ||
	    !strcmp(key, "SRP_A") || !strcmp(key, "SRP_B") ||
	    !strcmp(key, "SRP_M") || !strcmp(key, "SRP_HM"))
		return 1;

	return 0;
}

static void iscsi_check_proposer_for_optional_reply(struct iscsi_param *param,
						    bool keys_workaround)
{
	if (IS_TYPE_BOOL_AND(param)) {
		if (!strcmp(param->value, NO))
			SET_PSTATE_REPLY_OPTIONAL(param);
	} else if (IS_TYPE_BOOL_OR(param)) {
		if (!strcmp(param->value, YES))
			SET_PSTATE_REPLY_OPTIONAL(param);

		if (keys_workaround) {
			/*
			 * Required for gPXE iSCSI boot client
			 */
			if (!strcmp(param->name, IMMEDIATEDATA))
				SET_PSTATE_REPLY_OPTIONAL(param);
		}
	} else if (IS_TYPE_NUMBER(param)) {
		if (!strcmp(param->name, MAXRECVDATASEGMENTLENGTH))
			SET_PSTATE_REPLY_OPTIONAL(param);

		if (keys_workaround) {
			/*
			 * Required for Mellanox Flexboot PXE boot ROM
			 */
			if (!strcmp(param->name, FIRSTBURSTLENGTH))
				SET_PSTATE_REPLY_OPTIONAL(param);

			/*
			 * Required for gPXE iSCSI boot client
			 */
			if (!strcmp(param->name, MAXCONNECTIONS))
				SET_PSTATE_REPLY_OPTIONAL(param);
		}
	} else if (IS_PHASE_DECLARATIVE(param))
		SET_PSTATE_REPLY_OPTIONAL(param);
}

static int iscsi_check_boolean_value(struct iscsi_param *param, char *value)
{
	if (strcmp(value, YES) && strcmp(value, NO)) {
		pr_err("Illegal value for \"%s\", must be either"
			" \"%s\" or \"%s\".\n", param->name, YES, NO);
		return -1;
	}

	return 0;
}

static int iscsi_check_numerical_value(struct iscsi_param *param, char *value_ptr)
{
	char *tmpptr;
	int value = 0;

	value = simple_strtoul(value_ptr, &tmpptr, 0);

	if (IS_TYPERANGE_0_TO_2(param)) {
		if ((value < 0) || (value > 2)) {
			pr_err("Illegal value for \"%s\", must be"
				" between 0 and 2.\n", param->name);
			return -1;
		}
		return 0;
	}
	if (IS_TYPERANGE_0_TO_3600(param)) {
		if ((value < 0) || (value > 3600)) {
			pr_err("Illegal value for \"%s\", must be"
				" between 0 and 3600.\n", param->name);
			return -1;
		}
		return 0;
	}
	if (IS_TYPERANGE_0_TO_32767(param)) {
		if ((value < 0) || (value > 32767)) {
			pr_err("Illegal value for \"%s\", must be"
				" between 0 and 32767.\n", param->name);
			return -1;
		}
		return 0;
	}
	if (IS_TYPERANGE_0_TO_65535(param)) {
		if ((value < 0) || (value > 65535)) {
			pr_err("Illegal value for \"%s\", must be"
				" between 0 and 65535.\n", param->name);
			return -1;
		}
		return 0;
	}
	if (IS_TYPERANGE_1_TO_65535(param)) {
		if ((value < 1) || (value > 65535)) {
			pr_err("Illegal value for \"%s\", must be"
				" between 1 and 65535.\n", param->name);
			return -1;
		}
		return 0;
	}
	if (IS_TYPERANGE_2_TO_3600(param)) {
		if ((value < 2) || (value > 3600)) {
			pr_err("Illegal value for \"%s\", must be"
				" between 2 and 3600.\n", param->name);
			return -1;
		}
		return 0;
	}
	if (IS_TYPERANGE_512_TO_16777215(param)) {
		if ((value < 512) || (value > 16777215)) {
			pr_err("Illegal value for \"%s\", must be"
				" between 512 and 16777215.\n", param->name);
			return -1;
		}
		return 0;
	}

	return 0;
}

static int iscsi_check_string_or_list_value(struct iscsi_param *param, char *value)
{
	if (IS_PSTATE_PROPOSER(param))
		return 0;

	if (IS_TYPERANGE_AUTH_PARAM(param)) {
		if (strcmp(value, KRB5) && strcmp(value, SPKM1) &&
		    strcmp(value, SPKM2) && strcmp(value, SRP) &&
		    strcmp(value, CHAP) && strcmp(value, NONE)) {
			pr_err("Illegal value for \"%s\", must be"
				" \"%s\", \"%s\", \"%s\", \"%s\", \"%s\""
				" or \"%s\".\n", param->name, KRB5,
					SPKM1, SPKM2, SRP, CHAP, NONE);
			return -1;
		}
	}
	if (IS_TYPERANGE_DIGEST_PARAM(param)) {
		if (strcmp(value, CRC32C) && strcmp(value, NONE)) {
			pr_err("Illegal value for \"%s\", must be"
				" \"%s\" or \"%s\".\n", param->name,
					CRC32C, NONE);
			return -1;
		}
	}
	if (IS_TYPERANGE_SESSIONTYPE(param)) {
		if (strcmp(value, DISCOVERY) && strcmp(value, NORMAL)) {
			pr_err("Illegal value for \"%s\", must be"
				" \"%s\" or \"%s\".\n", param->name,
					DISCOVERY, NORMAL);
			return -1;
		}
	}

	return 0;
}

static char *iscsi_check_valuelist_for_support(
	struct iscsi_param *param,
	char *value)
{
	char *tmp1 = NULL, *tmp2 = NULL;
	char *acceptor_values = NULL, *proposer_values = NULL;

	acceptor_values = param->value;
	proposer_values = value;

	do {
		if (!proposer_values)
			return NULL;
		tmp1 = strchr(proposer_values, ',');
		if (tmp1)
			*tmp1 = '\0';
		acceptor_values = param->value;
		do {
			if (!acceptor_values) {
				if (tmp1)
					*tmp1 = ',';
				return NULL;
			}
			tmp2 = strchr(acceptor_values, ',');
			if (tmp2)
				*tmp2 = '\0';
			if (!strcmp(acceptor_values, proposer_values)) {
				if (tmp2)
					*tmp2 = ',';
				goto out;
			}
			if (tmp2)
				*tmp2++ = ',';

			acceptor_values = tmp2;
		} while (acceptor_values);
		if (tmp1)
			*tmp1++ = ',';
		proposer_values = tmp1;
	} while (proposer_values);

out:
	return proposer_values;
}

static int iscsi_check_acceptor_state(struct iscsi_param *param, char *value,
				struct iscsit_conn *conn)
{
	u8 acceptor_boolean_value = 0, proposer_boolean_value = 0;
	char *negotiated_value = NULL;

	if (IS_PSTATE_ACCEPTOR(param)) {
		pr_err("Received key \"%s\" twice, protocol error.\n",
				param->name);
		return -1;
	}

	if (IS_PSTATE_REJECT(param))
		return 0;

	if (IS_TYPE_BOOL_AND(param)) {
		if (!strcmp(value, YES))
			proposer_boolean_value = 1;
		if (!strcmp(param->value, YES))
			acceptor_boolean_value = 1;
		if (acceptor_boolean_value && proposer_boolean_value)
			do {} while (0);
		else {
			if (iscsi_update_param_value(param, NO) < 0)
				return -1;
			if (!proposer_boolean_value)
				SET_PSTATE_REPLY_OPTIONAL(param);
		}
	} else if (IS_TYPE_BOOL_OR(param)) {
		if (!strcmp(value, YES))
			proposer_boolean_value = 1;
		if (!strcmp(param->value, YES))
			acceptor_boolean_value = 1;
		if (acceptor_boolean_value || proposer_boolean_value) {
			if (iscsi_update_param_value(param, YES) < 0)
				return -1;
			if (proposer_boolean_value)
				SET_PSTATE_REPLY_OPTIONAL(param);
		}
	} else if (IS_TYPE_NUMBER(param)) {
		char *tmpptr, buf[11];
		u32 acceptor_value = simple_strtoul(param->value, &tmpptr, 0);
		u32 proposer_value = simple_strtoul(value, &tmpptr, 0);

		memset(buf, 0, sizeof(buf));

		if (!strcmp(param->name, MAXCONNECTIONS) ||
		    !strcmp(param->name, MAXBURSTLENGTH) ||
		    !strcmp(param->name, FIRSTBURSTLENGTH) ||
		    !strcmp(param->name, MAXOUTSTANDINGR2T) ||
		    !strcmp(param->name, DEFAULTTIME2RETAIN) ||
		    !strcmp(param->name, ERRORRECOVERYLEVEL)) {
			if (proposer_value > acceptor_value) {
				sprintf(buf, "%u", acceptor_value);
				if (iscsi_update_param_value(param,
						&buf[0]) < 0)
					return -1;
			} else {
				if (iscsi_update_param_value(param, value) < 0)
					return -1;
			}
		} else if (!strcmp(param->name, DEFAULTTIME2WAIT)) {
			if (acceptor_value > proposer_value) {
				sprintf(buf, "%u", acceptor_value);
				if (iscsi_update_param_value(param,
						&buf[0]) < 0)
					return -1;
			} else {
				if (iscsi_update_param_value(param, value) < 0)
					return -1;
			}
		} else {
			if (iscsi_update_param_value(param, value) < 0)
				return -1;
		}

		if (!strcmp(param->name, MAXRECVDATASEGMENTLENGTH)) {
			struct iscsi_param *param_mxdsl;
			unsigned long long tmp;
			int rc;

			rc = kstrtoull(param->value, 0, &tmp);
			if (rc < 0)
				return -1;

			conn->conn_ops->MaxRecvDataSegmentLength = tmp;
			pr_debug("Saving op->MaxRecvDataSegmentLength from"
				" original initiator received value: %u\n",
				conn->conn_ops->MaxRecvDataSegmentLength);

			param_mxdsl = iscsi_find_param_from_key(
						MAXXMITDATASEGMENTLENGTH,
						conn->param_list);
			if (!param_mxdsl)
				return -1;

			rc = iscsi_update_param_value(param,
						param_mxdsl->value);
			if (rc < 0)
				return -1;

			pr_debug("Updated %s to target MXDSL value: %s\n",
					param->name, param->value);
		}
	} else if (IS_TYPE_VALUE_LIST(param)) {
		negotiated_value = iscsi_check_valuelist_for_support(
					param, value);
		if (!negotiated_value) {
			pr_err("Proposer's value list \"%s\" contains"
				" no valid values from Acceptor's value list"
				" \"%s\".\n", value, param->value);
			return -1;
		}
		if (iscsi_update_param_value(param, negotiated_value) < 0)
			return -1;
	} else if (IS_PHASE_DECLARATIVE(param)) {
		if (iscsi_update_param_value(param, value) < 0)
			return -1;
		SET_PSTATE_REPLY_OPTIONAL(param);
	}

	return 0;
}

static int iscsi_check_proposer_state(struct iscsi_param *param, char *value)
{
	if (IS_PSTATE_RESPONSE_GOT(param)) {
		pr_err("Received key \"%s\" twice, protocol error.\n",
				param->name);
		return -1;
	}

	if (IS_TYPE_VALUE_LIST(param)) {
		char *comma_ptr = NULL, *tmp_ptr = NULL;

		comma_ptr = strchr(value, ',');
		if (comma_ptr) {
			pr_err("Illegal \",\" in response for \"%s\".\n",
					param->name);
			return -1;
		}

		tmp_ptr = iscsi_check_valuelist_for_support(param, value);
		if (!tmp_ptr)
			return -1;
	}

	if (iscsi_update_param_value(param, value) < 0)
		return -1;

	return 0;
}

static int iscsi_check_value(struct iscsi_param *param, char *value)
{
	char *comma_ptr = NULL;

	if (!strcmp(value, REJECT)) {
		if (!strcmp(param->name, IFMARKINT) ||
		    !strcmp(param->name, OFMARKINT)) {
			/*
			 * Reject is not fatal for [I,O]FMarkInt,  and causes
			 * [I,O]FMarker to be reset to No. (See iSCSI v20 A.3.2)
			 */
			SET_PSTATE_REJECT(param);
			return 0;
		}
		pr_err("Received %s=%s\n", param->name, value);
		return -1;
	}
	if (!strcmp(value, IRRELEVANT)) {
		pr_debug("Received %s=%s\n", param->name, value);
		SET_PSTATE_IRRELEVANT(param);
		return 0;
	}
	if (!strcmp(value, NOTUNDERSTOOD)) {
		if (!IS_PSTATE_PROPOSER(param)) {
			pr_err("Received illegal offer %s=%s\n",
				param->name, value);
			return -1;
		}

/* #warning FIXME: Add check for X-ExtensionKey here */
		pr_err("Standard iSCSI key \"%s\" cannot be answered"
			" with \"%s\", protocol error.\n", param->name, value);
		return -1;
	}

	do {
		comma_ptr = NULL;
		comma_ptr = strchr(value, ',');

		if (comma_ptr && !IS_TYPE_VALUE_LIST(param)) {
			pr_err("Detected value separator \",\", but"
				" key \"%s\" does not allow a value list,"
				" protocol error.\n", param->name);
			return -1;
		}
		if (comma_ptr)
			*comma_ptr = '\0';

		if (strlen(value) > VALUE_MAXLEN) {
			pr_err("Value for key \"%s\" exceeds %d,"
				" protocol error.\n", param->name,
				VALUE_MAXLEN);
			return -1;
		}

		if (IS_TYPE_BOOL_AND(param) || IS_TYPE_BOOL_OR(param)) {
			if (iscsi_check_boolean_value(param, value) < 0)
				return -1;
		} else if (IS_TYPE_NUMBER(param)) {
			if (iscsi_check_numerical_value(param, value) < 0)
				return -1;
		} else if (IS_TYPE_STRING(param) || IS_TYPE_VALUE_LIST(param)) {
			if (iscsi_check_string_or_list_value(param, value) < 0)
				return -1;
		} else {
			pr_err("Huh? 0x%02x\n", param->type);
			return -1;
		}

		if (comma_ptr)
			*comma_ptr++ = ',';

		value = comma_ptr;
	} while (value);

	return 0;
}

static struct iscsi_param *__iscsi_check_key(
	char *key,
	int sender,
	struct iscsi_param_list *param_list)
{
	struct iscsi_param *param;

	if (strlen(key) > KEY_MAXLEN) {
		pr_err("Length of key name \"%s\" exceeds %d.\n",
			key, KEY_MAXLEN);
		return NULL;
	}

	param = iscsi_find_param_from_key(key, param_list);
	if (!param)
		return NULL;

	if ((sender & SENDER_INITIATOR) && !IS_SENDER_INITIATOR(param)) {
		pr_err("Key \"%s\" may not be sent to %s,"
			" protocol error.\n", param->name,
			(sender & SENDER_RECEIVER) ? "target" : "initiator");
		return NULL;
	}

	if ((sender & SENDER_TARGET) && !IS_SENDER_TARGET(param)) {
		pr_err("Key \"%s\" may not be sent to %s,"
			" protocol error.\n", param->name,
			(sender & SENDER_RECEIVER) ? "initiator" : "target");
		return NULL;
	}

	return param;
}

static struct iscsi_param *iscsi_check_key(
	char *key,
	int phase,
	int sender,
	struct iscsi_param_list *param_list)
{
	struct iscsi_param *param;
	/*
	 * Key name length must not exceed 63 bytes. (See iSCSI v20 5.1)
	 */
	if (strlen(key) > KEY_MAXLEN) {
		pr_err("Length of key name \"%s\" exceeds %d.\n",
			key, KEY_MAXLEN);
		return NULL;
	}

	param = iscsi_find_param_from_key(key, param_list);
	if (!param)
		return NULL;

	if ((sender & SENDER_INITIATOR) && !IS_SENDER_INITIATOR(param)) {
		pr_err("Key \"%s\" may not be sent to %s,"
			" protocol error.\n", param->name,
			(sender & SENDER_RECEIVER) ? "target" : "initiator");
		return NULL;
	}
	if ((sender & SENDER_TARGET) && !IS_SENDER_TARGET(param)) {
		pr_err("Key \"%s\" may not be sent to %s,"
				" protocol error.\n", param->name,
			(sender & SENDER_RECEIVER) ? "initiator" : "target");
		return NULL;
	}

	if (IS_PSTATE_ACCEPTOR(param)) {
		pr_err("Key \"%s\" received twice, protocol error.\n",
				key);
		return NULL;
	}

	if (!phase)
		return param;

	if (!(param->phase & phase)) {
		char *phase_name;

		switch (phase) {
		case PHASE_SECURITY:
			phase_name = "Security";
			break;
		case PHASE_OPERATIONAL:
			phase_name = "Operational";
			break;
		default:
			phase_name = "Unknown";
		}
		pr_err("Key \"%s\" may not be negotiated during %s phase.\n",
				param->name, phase_name);
		return NULL;
	}

	return param;
}

static int iscsi_enforce_integrity_rules(
	u8 phase,
	struct iscsi_param_list *param_list)
{
	char *tmpptr;
	u8 DataSequenceInOrder = 0;
	u8 ErrorRecoveryLevel = 0, SessionType = 0;
	u32 FirstBurstLength = 0, MaxBurstLength = 0;
	struct iscsi_param *param = NULL;

	list_for_each_entry(param, &param_list->param_list, p_list) {
		if (!(param->phase & phase))
			continue;
		if (!strcmp(param->name, SESSIONTYPE))
			if (!strcmp(param->value, NORMAL))
				SessionType = 1;
		if (!strcmp(param->name, ERRORRECOVERYLEVEL))
			ErrorRecoveryLevel = simple_strtoul(param->value,
					&tmpptr, 0);
		if (!strcmp(param->name, DATASEQUENCEINORDER))
			if (!strcmp(param->value, YES))
				DataSequenceInOrder = 1;
		if (!strcmp(param->name, MAXBURSTLENGTH))
			MaxBurstLength = simple_strtoul(param->value,
					&tmpptr, 0);
	}

	list_for_each_entry(param, &param_list->param_list, p_list) {
		if (!(param->phase & phase))
			continue;
		if (!SessionType && !IS_PSTATE_ACCEPTOR(param))
			continue;
		if (!strcmp(param->name, MAXOUTSTANDINGR2T) &&
		    DataSequenceInOrder && (ErrorRecoveryLevel > 0)) {
			if (strcmp(param->value, "1")) {
				if (iscsi_update_param_value(param, "1") < 0)
					return -1;
				pr_debug("Reset \"%s\" to \"%s\".\n",
					param->name, param->value);
			}
		}
		if (!strcmp(param->name, MAXCONNECTIONS) && !SessionType) {
			if (strcmp(param->value, "1")) {
				if (iscsi_update_param_value(param, "1") < 0)
					return -1;
				pr_debug("Reset \"%s\" to \"%s\".\n",
					param->name, param->value);
			}
		}
		if (!strcmp(param->name, FIRSTBURSTLENGTH)) {
			FirstBurstLength = simple_strtoul(param->value,
					&tmpptr, 0);
			if (FirstBurstLength > MaxBurstLength) {
				char tmpbuf[11];
				memset(tmpbuf, 0, sizeof(tmpbuf));
				sprintf(tmpbuf, "%u", MaxBurstLength);
				if (iscsi_update_param_value(param, tmpbuf))
					return -1;
				pr_debug("Reset \"%s\" to \"%s\".\n",
					param->name, param->value);
			}
		}
	}

	return 0;
}

int iscsi_decode_text_input(
	u8 phase,
	u8 sender,
	char *textbuf,
	u32 length,
	struct iscsit_conn *conn)
{
	struct iscsi_param_list *param_list = conn->param_list;
	char *tmpbuf, *start = NULL, *end = NULL;

	tmpbuf = kmemdup_nul(textbuf, length, GFP_KERNEL);
	if (!tmpbuf) {
		pr_err("Unable to allocate %u + 1 bytes for tmpbuf.\n", length);
		return -ENOMEM;
	}

	start = tmpbuf;
	end = (start + length);

	while (start < end) {
		char *key, *value;
		struct iscsi_param *param;

		if (iscsi_extract_key_value(start, &key, &value) < 0)
			goto free_buffer;

		pr_debug("Got key: %s=%s\n", key, value);

		if (phase & PHASE_SECURITY) {
			if (iscsi_check_for_auth_key(key) > 0) {
				kfree(tmpbuf);
				return 1;
			}
		}

		param = iscsi_check_key(key, phase, sender, param_list);
		if (!param) {
			if (iscsi_add_notunderstood_response(key, value,
							     param_list) < 0)
				goto free_buffer;

			start += strlen(key) + strlen(value) + 2;
			continue;
		}
		if (iscsi_check_value(param, value) < 0)
			goto free_buffer;

		start += strlen(key) + strlen(value) + 2;

		if (IS_PSTATE_PROPOSER(param)) {
			if (iscsi_check_proposer_state(param, value) < 0)
				goto free_buffer;

			SET_PSTATE_RESPONSE_GOT(param);
		} else {
			if (iscsi_check_acceptor_state(param, value, conn) < 0)
				goto free_buffer;

			SET_PSTATE_ACCEPTOR(param);
		}
	}

	kfree(tmpbuf);
	return 0;

free_buffer:
	kfree(tmpbuf);
	return -1;
}

int iscsi_encode_text_output(
	u8 phase,
	u8 sender,
	char *textbuf,
	u32 *length,
	struct iscsi_param_list *param_list,
	bool keys_workaround)
{
	char *output_buf = NULL;
	struct iscsi_extra_response *er;
	struct iscsi_param *param;

	output_buf = textbuf + *length;

	if (iscsi_enforce_integrity_rules(phase, param_list) < 0)
		return -1;

	list_for_each_entry(param, &param_list->param_list, p_list) {
		if (!(param->sender & sender))
			continue;
		if (IS_PSTATE_ACCEPTOR(param) &&
		    !IS_PSTATE_RESPONSE_SENT(param) &&
		    !IS_PSTATE_REPLY_OPTIONAL(param) &&
		    (param->phase & phase)) {
			*length += sprintf(output_buf, "%s=%s",
				param->name, param->value);
			*length += 1;
			output_buf = textbuf + *length;
			SET_PSTATE_RESPONSE_SENT(param);
			pr_debug("Sending key: %s=%s\n",
				param->name, param->value);
			continue;
		}
		if (IS_PSTATE_NEGOTIATE(param) &&
		    !IS_PSTATE_ACCEPTOR(param) &&
		    !IS_PSTATE_PROPOSER(param) &&
		    (param->phase & phase)) {
			*length += sprintf(output_buf, "%s=%s",
				param->name, param->value);
			*length += 1;
			output_buf = textbuf + *length;
			SET_PSTATE_PROPOSER(param);
			iscsi_check_proposer_for_optional_reply(param,
							        keys_workaround);
			pr_debug("Sending key: %s=%s\n",
				param->name, param->value);
		}
	}

	list_for_each_entry(er, &param_list->extra_response_list, er_list) {
		*length += sprintf(output_buf, "%s=%s", er->key, er->value);
		*length += 1;
		output_buf = textbuf + *length;
		pr_debug("Sending key: %s=%s\n", er->key, er->value);
	}
	iscsi_release_extra_responses(param_list);

	return 0;
}

int iscsi_check_negotiated_keys(struct iscsi_param_list *param_list)
{
	int ret = 0;
	struct iscsi_param *param;

	list_for_each_entry(param, &param_list->param_list, p_list) {
		if (IS_PSTATE_NEGOTIATE(param) &&
		    IS_PSTATE_PROPOSER(param) &&
		    !IS_PSTATE_RESPONSE_GOT(param) &&
		    !IS_PSTATE_REPLY_OPTIONAL(param) &&
		    !IS_PHASE_DECLARATIVE(param)) {
			pr_err("No response for proposed key \"%s\".\n",
					param->name);
			ret = -1;
		}
	}

	return ret;
}

int iscsi_change_param_value(
	char *keyvalue,
	struct iscsi_param_list *param_list,
	int check_key)
{
	char *key = NULL, *value = NULL;
	struct iscsi_param *param;
	int sender = 0;

	if (iscsi_extract_key_value(keyvalue, &key, &value) < 0)
		return -1;

	if (!check_key) {
		param = __iscsi_check_key(keyvalue, sender, param_list);
		if (!param)
			return -1;
	} else {
		param = iscsi_check_key(keyvalue, 0, sender, param_list);
		if (!param)
			return -1;

		param->set_param = 1;
		if (iscsi_check_value(param, value) < 0) {
			param->set_param = 0;
			return -1;
		}
		param->set_param = 0;
	}

	if (iscsi_update_param_value(param, value) < 0)
		return -1;

	return 0;
}

void iscsi_set_connection_parameters(
	struct iscsi_conn_ops *ops,
	struct iscsi_param_list *param_list)
{
	char *tmpptr;
	struct iscsi_param *param;

	pr_debug("---------------------------------------------------"
			"---------------\n");
	list_for_each_entry(param, &param_list->param_list, p_list) {
		/*
		 * Special case to set MAXXMITDATASEGMENTLENGTH from the
		 * target requested MaxRecvDataSegmentLength, even though
		 * this key is not sent over the wire.
		 */
		if (!strcmp(param->name, MAXXMITDATASEGMENTLENGTH)) {
			ops->MaxXmitDataSegmentLength =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("MaxXmitDataSegmentLength:     %s\n",
				param->value);
		}

		if (!IS_PSTATE_ACCEPTOR(param) && !IS_PSTATE_PROPOSER(param))
			continue;
		if (!strcmp(param->name, AUTHMETHOD)) {
			pr_debug("AuthMethod:                   %s\n",
				param->value);
		} else if (!strcmp(param->name, HEADERDIGEST)) {
			ops->HeaderDigest = !strcmp(param->value, CRC32C);
			pr_debug("HeaderDigest:                 %s\n",
				param->value);
		} else if (!strcmp(param->name, DATADIGEST)) {
			ops->DataDigest = !strcmp(param->value, CRC32C);
			pr_debug("DataDigest:                   %s\n",
				param->value);
		} else if (!strcmp(param->name, MAXRECVDATASEGMENTLENGTH)) {
			/*
			 * At this point iscsi_check_acceptor_state() will have
			 * set ops->MaxRecvDataSegmentLength from the original
			 * initiator provided value.
			 */
			pr_debug("MaxRecvDataSegmentLength:     %u\n",
				ops->MaxRecvDataSegmentLength);
		} else if (!strcmp(param->name, INITIATORRECVDATASEGMENTLENGTH)) {
			ops->InitiatorRecvDataSegmentLength =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("InitiatorRecvDataSegmentLength: %s\n",
				param->value);
			ops->MaxRecvDataSegmentLength =
					ops->InitiatorRecvDataSegmentLength;
			pr_debug("Set MRDSL from InitiatorRecvDataSegmentLength\n");
		} else if (!strcmp(param->name, TARGETRECVDATASEGMENTLENGTH)) {
			ops->TargetRecvDataSegmentLength =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("TargetRecvDataSegmentLength:  %s\n",
				param->value);
			ops->MaxXmitDataSegmentLength =
					ops->TargetRecvDataSegmentLength;
			pr_debug("Set MXDSL from TargetRecvDataSegmentLength\n");
		}
	}
	pr_debug("----------------------------------------------------"
			"--------------\n");
}

void iscsi_set_session_parameters(
	struct iscsi_sess_ops *ops,
	struct iscsi_param_list *param_list,
	int leading)
{
	char *tmpptr;
	struct iscsi_param *param;

	pr_debug("----------------------------------------------------"
			"--------------\n");
	list_for_each_entry(param, &param_list->param_list, p_list) {
		if (!IS_PSTATE_ACCEPTOR(param) && !IS_PSTATE_PROPOSER(param))
			continue;
		if (!strcmp(param->name, INITIATORNAME)) {
			if (!param->value)
				continue;
			if (leading)
				snprintf(ops->InitiatorName,
						sizeof(ops->InitiatorName),
						"%s", param->value);
			pr_debug("InitiatorName:                %s\n",
				param->value);
		} else if (!strcmp(param->name, INITIATORALIAS)) {
			if (!param->value)
				continue;
			snprintf(ops->InitiatorAlias,
						sizeof(ops->InitiatorAlias),
						"%s", param->value);
			pr_debug("InitiatorAlias:               %s\n",
				param->value);
		} else if (!strcmp(param->name, TARGETNAME)) {
			if (!param->value)
				continue;
			if (leading)
				snprintf(ops->TargetName,
						sizeof(ops->TargetName),
						"%s", param->value);
			pr_debug("TargetName:                   %s\n",
				param->value);
		} else if (!strcmp(param->name, TARGETALIAS)) {
			if (!param->value)
				continue;
			snprintf(ops->TargetAlias, sizeof(ops->TargetAlias),
					"%s", param->value);
			pr_debug("TargetAlias:                  %s\n",
				param->value);
		} else if (!strcmp(param->name, TARGETPORTALGROUPTAG)) {
			ops->TargetPortalGroupTag =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("TargetPortalGroupTag:         %s\n",
				param->value);
		} else if (!strcmp(param->name, MAXCONNECTIONS)) {
			ops->MaxConnections =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("MaxConnections:               %s\n",
				param->value);
		} else if (!strcmp(param->name, INITIALR2T)) {
			ops->InitialR2T = !strcmp(param->value, YES);
			pr_debug("InitialR2T:                   %s\n",
				param->value);
		} else if (!strcmp(param->name, IMMEDIATEDATA)) {
			ops->ImmediateData = !strcmp(param->value, YES);
			pr_debug("ImmediateData:                %s\n",
				param->value);
		} else if (!strcmp(param->name, MAXBURSTLENGTH)) {
			ops->MaxBurstLength =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("MaxBurstLength:               %s\n",
				param->value);
		} else if (!strcmp(param->name, FIRSTBURSTLENGTH)) {
			ops->FirstBurstLength =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("FirstBurstLength:             %s\n",
				param->value);
		} else if (!strcmp(param->name, DEFAULTTIME2WAIT)) {
			ops->DefaultTime2Wait =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("DefaultTime2Wait:             %s\n",
				param->value);
		} else if (!strcmp(param->name, DEFAULTTIME2RETAIN)) {
			ops->DefaultTime2Retain =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("DefaultTime2Retain:           %s\n",
				param->value);
		} else if (!strcmp(param->name, MAXOUTSTANDINGR2T)) {
			ops->MaxOutstandingR2T =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("MaxOutstandingR2T:            %s\n",
				param->value);
		} else if (!strcmp(param->name, DATAPDUINORDER)) {
			ops->DataPDUInOrder = !strcmp(param->value, YES);
			pr_debug("DataPDUInOrder:               %s\n",
				param->value);
		} else if (!strcmp(param->name, DATASEQUENCEINORDER)) {
			ops->DataSequenceInOrder = !strcmp(param->value, YES);
			pr_debug("DataSequenceInOrder:          %s\n",
				param->value);
		} else if (!strcmp(param->name, ERRORRECOVERYLEVEL)) {
			ops->ErrorRecoveryLevel =
				simple_strtoul(param->value, &tmpptr, 0);
			pr_debug("ErrorRecoveryLevel:           %s\n",
				param->value);
		} else if (!strcmp(param->name, SESSIONTYPE)) {
			ops->SessionType = !strcmp(param->value, DISCOVERY);
			pr_debug("SessionType:                  %s\n",
				param->value);
		} else if (!strcmp(param->name, RDMAEXTENSIONS)) {
			ops->RDMAExtensions = !strcmp(param->value, YES);
			pr_debug("RDMAExtensions:               %s\n",
				param->value);
		}
	}
	pr_debug("----------------------------------------------------"
			"--------------\n");

}
