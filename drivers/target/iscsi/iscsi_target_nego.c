/*******************************************************************************
 * This file contains main functions related to iSCSI Parameter negotiation.
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
 ******************************************************************************/

#include <linux/ctype.h>
#include <scsi/iscsi_proto.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/iscsi/iscsi_transport.h>

#include "iscsi_target_core.h"
#include "iscsi_target_parameters.h"
#include "iscsi_target_login.h"
#include "iscsi_target_nego.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target.h"
#include "iscsi_target_auth.h"

#define MAX_LOGIN_PDUS  7
#define TEXT_LEN	4096

void convert_null_to_semi(char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (buf[i] == '\0')
			buf[i] = ';';
}

static int strlen_semi(char *buf)
{
	int i = 0;

	while (buf[i] != '\0') {
		if (buf[i] == ';')
			return i;
		i++;
	}

	return -1;
}

int extract_param(
	const char *in_buf,
	const char *pattern,
	unsigned int max_length,
	char *out_buf,
	unsigned char *type)
{
	char *ptr;
	int len;

	if (!in_buf || !pattern || !out_buf || !type)
		return -1;

	ptr = strstr(in_buf, pattern);
	if (!ptr)
		return -1;

	ptr = strstr(ptr, "=");
	if (!ptr)
		return -1;

	ptr += 1;
	if (*ptr == '0' && (*(ptr+1) == 'x' || *(ptr+1) == 'X')) {
		ptr += 2; /* skip 0x */
		*type = HEX;
	} else
		*type = DECIMAL;

	len = strlen_semi(ptr);
	if (len < 0)
		return -1;

	if (len > max_length) {
		pr_err("Length of input: %d exceeds max_length:"
			" %d\n", len, max_length);
		return -1;
	}
	memcpy(out_buf, ptr, len);
	out_buf[len] = '\0';

	return 0;
}

static u32 iscsi_handle_authentication(
	struct iscsi_conn *conn,
	char *in_buf,
	char *out_buf,
	int in_length,
	int *out_length,
	unsigned char *authtype)
{
	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_auth *auth;
	struct iscsi_node_acl *iscsi_nacl;
	struct iscsi_portal_group *iscsi_tpg;
	struct se_node_acl *se_nacl;

	if (!sess->sess_ops->SessionType) {
		/*
		 * For SessionType=Normal
		 */
		se_nacl = conn->sess->se_sess->se_node_acl;
		if (!se_nacl) {
			pr_err("Unable to locate struct se_node_acl for"
					" CHAP auth\n");
			return -1;
		}
		iscsi_nacl = container_of(se_nacl, struct iscsi_node_acl,
				se_node_acl);
		if (!iscsi_nacl) {
			pr_err("Unable to locate struct iscsi_node_acl for"
					" CHAP auth\n");
			return -1;
		}

		if (se_nacl->dynamic_node_acl) {
			iscsi_tpg = container_of(se_nacl->se_tpg,
					struct iscsi_portal_group, tpg_se_tpg);

			auth = &iscsi_tpg->tpg_demo_auth;
		} else {
			iscsi_nacl = container_of(se_nacl, struct iscsi_node_acl,
						  se_node_acl);

			auth = ISCSI_NODE_AUTH(iscsi_nacl);
		}
	} else {
		/*
		 * For SessionType=Discovery
		 */
		auth = &iscsit_global->discovery_acl.node_auth;
	}

	if (strstr("CHAP", authtype))
		strcpy(conn->sess->auth_type, "CHAP");
	else
		strcpy(conn->sess->auth_type, NONE);

	if (strstr("None", authtype))
		return 1;
#ifdef CANSRP
	else if (strstr("SRP", authtype))
		return srp_main_loop(conn, auth, in_buf, out_buf,
				&in_length, out_length);
#endif
	else if (strstr("CHAP", authtype))
		return chap_main_loop(conn, auth, in_buf, out_buf,
				&in_length, out_length);
	else if (strstr("SPKM1", authtype))
		return 2;
	else if (strstr("SPKM2", authtype))
		return 2;
	else if (strstr("KRB5", authtype))
		return 2;
	else
		return 2;
}

static void iscsi_remove_failed_auth_entry(struct iscsi_conn *conn)
{
	kfree(conn->auth_protocol);
}

int iscsi_target_check_login_request(
	struct iscsi_conn *conn,
	struct iscsi_login *login)
{
	int req_csg, req_nsg;
	u32 payload_length;
	struct iscsi_login_req *login_req;

	login_req = (struct iscsi_login_req *) login->req;
	payload_length = ntoh24(login_req->dlength);

	switch (login_req->opcode & ISCSI_OPCODE_MASK) {
	case ISCSI_OP_LOGIN:
		break;
	default:
		pr_err("Received unknown opcode 0x%02x.\n",
				login_req->opcode & ISCSI_OPCODE_MASK);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_INIT_ERR);
		return -1;
	}

	if ((login_req->flags & ISCSI_FLAG_LOGIN_CONTINUE) &&
	    (login_req->flags & ISCSI_FLAG_LOGIN_TRANSIT)) {
		pr_err("Login request has both ISCSI_FLAG_LOGIN_CONTINUE"
			" and ISCSI_FLAG_LOGIN_TRANSIT set, protocol error.\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_INIT_ERR);
		return -1;
	}

	req_csg = ISCSI_LOGIN_CURRENT_STAGE(login_req->flags);
	req_nsg = ISCSI_LOGIN_NEXT_STAGE(login_req->flags);

	if (req_csg != login->current_stage) {
		pr_err("Initiator unexpectedly changed login stage"
			" from %d to %d, login failed.\n", login->current_stage,
			req_csg);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_INIT_ERR);
		return -1;
	}

	if ((req_nsg == 2) || (req_csg >= 2) ||
	   ((login_req->flags & ISCSI_FLAG_LOGIN_TRANSIT) &&
	    (req_nsg <= req_csg))) {
		pr_err("Illegal login_req->flags Combination, CSG: %d,"
			" NSG: %d, ISCSI_FLAG_LOGIN_TRANSIT: %d.\n", req_csg,
			req_nsg, (login_req->flags & ISCSI_FLAG_LOGIN_TRANSIT));
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_INIT_ERR);
		return -1;
	}

	if ((login_req->max_version != login->version_max) ||
	    (login_req->min_version != login->version_min)) {
		pr_err("Login request changed Version Max/Nin"
			" unexpectedly to 0x%02x/0x%02x, protocol error\n",
			login_req->max_version, login_req->min_version);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_INIT_ERR);
		return -1;
	}

	if (memcmp(login_req->isid, login->isid, 6) != 0) {
		pr_err("Login request changed ISID unexpectedly,"
				" protocol error.\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_INIT_ERR);
		return -1;
	}

	if (login_req->itt != login->init_task_tag) {
		pr_err("Login request changed ITT unexpectedly to"
			" 0x%08x, protocol error.\n", login_req->itt);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_INIT_ERR);
		return -1;
	}

	if (payload_length > MAX_KEY_VALUE_PAIRS) {
		pr_err("Login request payload exceeds default"
			" MaxRecvDataSegmentLength: %u, protocol error.\n",
				MAX_KEY_VALUE_PAIRS);
		return -1;
	}

	return 0;
}

static int iscsi_target_check_first_request(
	struct iscsi_conn *conn,
	struct iscsi_login *login)
{
	struct iscsi_param *param = NULL;
	struct se_node_acl *se_nacl;

	login->first_request = 0;

	list_for_each_entry(param, &conn->param_list->param_list, p_list) {
		if (!strncmp(param->name, SESSIONTYPE, 11)) {
			if (!IS_PSTATE_ACCEPTOR(param)) {
				pr_err("SessionType key not received"
					" in first login request.\n");
				iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
					ISCSI_LOGIN_STATUS_MISSING_FIELDS);
				return -1;
			}
			if (!strncmp(param->value, DISCOVERY, 9))
				return 0;
		}

		if (!strncmp(param->name, INITIATORNAME, 13)) {
			if (!IS_PSTATE_ACCEPTOR(param)) {
				if (!login->leading_connection)
					continue;

				pr_err("InitiatorName key not received"
					" in first login request.\n");
				iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
					ISCSI_LOGIN_STATUS_MISSING_FIELDS);
				return -1;
			}

			/*
			 * For non-leading connections, double check that the
			 * received InitiatorName matches the existing session's
			 * struct iscsi_node_acl.
			 */
			if (!login->leading_connection) {
				se_nacl = conn->sess->se_sess->se_node_acl;
				if (!se_nacl) {
					pr_err("Unable to locate"
						" struct se_node_acl\n");
					iscsit_tx_login_rsp(conn,
							ISCSI_STATUS_CLS_INITIATOR_ERR,
							ISCSI_LOGIN_STATUS_TGT_NOT_FOUND);
					return -1;
				}

				if (strcmp(param->value,
						se_nacl->initiatorname)) {
					pr_err("Incorrect"
						" InitiatorName: %s for this"
						" iSCSI Initiator Node.\n",
						param->value);
					iscsit_tx_login_rsp(conn,
							ISCSI_STATUS_CLS_INITIATOR_ERR,
							ISCSI_LOGIN_STATUS_TGT_NOT_FOUND);
					return -1;
				}
			}
		}
	}

	return 0;
}

static int iscsi_target_do_tx_login_io(struct iscsi_conn *conn, struct iscsi_login *login)
{
	u32 padding = 0;
	struct iscsi_session *sess = conn->sess;
	struct iscsi_login_rsp *login_rsp;

	login_rsp = (struct iscsi_login_rsp *) login->rsp;

	login_rsp->opcode		= ISCSI_OP_LOGIN_RSP;
	hton24(login_rsp->dlength, login->rsp_length);
	memcpy(login_rsp->isid, login->isid, 6);
	login_rsp->tsih			= cpu_to_be16(login->tsih);
	login_rsp->itt			= login->init_task_tag;
	login_rsp->statsn		= cpu_to_be32(conn->stat_sn++);
	login_rsp->exp_cmdsn		= cpu_to_be32(conn->sess->exp_cmd_sn);
	login_rsp->max_cmdsn		= cpu_to_be32(conn->sess->max_cmd_sn);

	pr_debug("Sending Login Response, Flags: 0x%02x, ITT: 0x%08x,"
		" ExpCmdSN; 0x%08x, MaxCmdSN: 0x%08x, StatSN: 0x%08x, Length:"
		" %u\n", login_rsp->flags, (__force u32)login_rsp->itt,
		ntohl(login_rsp->exp_cmdsn), ntohl(login_rsp->max_cmdsn),
		ntohl(login_rsp->statsn), login->rsp_length);

	padding = ((-login->rsp_length) & 3);

	if (conn->conn_transport->iscsit_put_login_tx(conn, login,
					login->rsp_length + padding) < 0)
		return -1;

	login->rsp_length		= 0;
	mutex_lock(&sess->cmdsn_mutex);
	login_rsp->exp_cmdsn		= cpu_to_be32(sess->exp_cmd_sn);
	login_rsp->max_cmdsn		= cpu_to_be32(sess->max_cmd_sn);
	mutex_unlock(&sess->cmdsn_mutex);

	return 0;
}

static void iscsi_target_sk_data_ready(struct sock *sk, int count)
{
	struct iscsi_conn *conn = sk->sk_user_data;
	bool rc;

	pr_debug("Entering iscsi_target_sk_data_ready: conn: %p\n", conn);

	write_lock_bh(&sk->sk_callback_lock);
	if (!sk->sk_user_data) {
		write_unlock_bh(&sk->sk_callback_lock);
		return;
	}
	if (!test_bit(LOGIN_FLAGS_READY, &conn->login_flags)) {
		write_unlock_bh(&sk->sk_callback_lock);
		pr_debug("Got LOGIN_FLAGS_READY=0, conn: %p >>>>\n", conn);
		return;
	}
	if (test_bit(LOGIN_FLAGS_CLOSED, &conn->login_flags)) {
		write_unlock_bh(&sk->sk_callback_lock);
		pr_debug("Got LOGIN_FLAGS_CLOSED=1, conn: %p >>>>\n", conn);
		return;
	}
	if (test_and_set_bit(LOGIN_FLAGS_READ_ACTIVE, &conn->login_flags)) {
		write_unlock_bh(&sk->sk_callback_lock);
		pr_debug("Got LOGIN_FLAGS_READ_ACTIVE=1, conn: %p >>>>\n", conn);
		return;
	}

	rc = schedule_delayed_work(&conn->login_work, 0);
	if (rc == false) {
		pr_debug("iscsi_target_sk_data_ready, schedule_delayed_work"
			 " got false\n");
	}
	write_unlock_bh(&sk->sk_callback_lock);
}

static void iscsi_target_sk_state_change(struct sock *);

static void iscsi_target_set_sock_callbacks(struct iscsi_conn *conn)
{
	struct sock *sk;

	if (!conn->sock)
		return;

	sk = conn->sock->sk;
	pr_debug("Entering iscsi_target_set_sock_callbacks: conn: %p\n", conn);

	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_user_data = conn;
	conn->orig_data_ready = sk->sk_data_ready;
	conn->orig_state_change = sk->sk_state_change;
	sk->sk_data_ready = iscsi_target_sk_data_ready;
	sk->sk_state_change = iscsi_target_sk_state_change;
	write_unlock_bh(&sk->sk_callback_lock);

	sk->sk_sndtimeo = TA_LOGIN_TIMEOUT * HZ;
	sk->sk_rcvtimeo = TA_LOGIN_TIMEOUT * HZ;
}

static void iscsi_target_restore_sock_callbacks(struct iscsi_conn *conn)
{
	struct sock *sk;

	if (!conn->sock)
		return;

	sk = conn->sock->sk;
	pr_debug("Entering iscsi_target_restore_sock_callbacks: conn: %p\n", conn);

	write_lock_bh(&sk->sk_callback_lock);
	if (!sk->sk_user_data) {
		write_unlock_bh(&sk->sk_callback_lock);
		return;
	}
	sk->sk_user_data = NULL;
	sk->sk_data_ready = conn->orig_data_ready;
	sk->sk_state_change = conn->orig_state_change;
	write_unlock_bh(&sk->sk_callback_lock);

	sk->sk_sndtimeo = MAX_SCHEDULE_TIMEOUT;
	sk->sk_rcvtimeo = MAX_SCHEDULE_TIMEOUT;
}

static int iscsi_target_do_login(struct iscsi_conn *, struct iscsi_login *);

static bool iscsi_target_sk_state_check(struct sock *sk)
{
	if (sk->sk_state == TCP_CLOSE_WAIT || sk->sk_state == TCP_CLOSE) {
		pr_debug("iscsi_target_sk_state_check: TCP_CLOSE_WAIT|TCP_CLOSE,"
			"returning FALSE\n");
		return false;
	}
	return true;
}

static void iscsi_target_login_drop(struct iscsi_conn *conn, struct iscsi_login *login)
{
	struct iscsi_np *np = login->np;
	bool zero_tsih = login->zero_tsih;

	iscsi_remove_failed_auth_entry(conn);
	iscsi_target_nego_release(conn);
	iscsi_target_login_sess_out(conn, np, zero_tsih, true);
}

static void iscsi_target_login_timeout(unsigned long data)
{
	struct iscsi_conn *conn = (struct iscsi_conn *)data;

	pr_debug("Entering iscsi_target_login_timeout >>>>>>>>>>>>>>>>>>>\n");

	if (conn->login_kworker) {
		pr_debug("Sending SIGINT to conn->login_kworker %s/%d\n",
			 conn->login_kworker->comm, conn->login_kworker->pid);
		send_sig(SIGINT, conn->login_kworker, 1);
	}
}

static void iscsi_target_do_login_rx(struct work_struct *work)
{
	struct iscsi_conn *conn = container_of(work,
				struct iscsi_conn, login_work.work);
	struct iscsi_login *login = conn->login;
	struct iscsi_np *np = login->np;
	struct iscsi_portal_group *tpg = conn->tpg;
	struct iscsi_tpg_np *tpg_np = conn->tpg_np;
	struct timer_list login_timer;
	int rc, zero_tsih = login->zero_tsih;
	bool state;

	pr_debug("entering iscsi_target_do_login_rx, conn: %p, %s:%d\n",
			conn, current->comm, current->pid);

	spin_lock(&tpg->tpg_state_lock);
	state = (tpg->tpg_state == TPG_STATE_ACTIVE);
	spin_unlock(&tpg->tpg_state_lock);

	if (state == false) {
		pr_debug("iscsi_target_do_login_rx: tpg_state != TPG_STATE_ACTIVE\n");
		iscsi_target_restore_sock_callbacks(conn);
		iscsi_target_login_drop(conn, login);
		iscsit_deaccess_np(np, tpg, tpg_np);
		return;
	}

	if (conn->sock) {
		struct sock *sk = conn->sock->sk;

		read_lock_bh(&sk->sk_callback_lock);
		state = iscsi_target_sk_state_check(sk);
		read_unlock_bh(&sk->sk_callback_lock);

		if (state == false) {
			pr_debug("iscsi_target_do_login_rx, TCP state CLOSE\n");
			iscsi_target_restore_sock_callbacks(conn);
			iscsi_target_login_drop(conn, login);
			iscsit_deaccess_np(np, tpg, tpg_np);
			return;
		}
	}

	conn->login_kworker = current;
	allow_signal(SIGINT);

	init_timer(&login_timer);
	login_timer.expires = (get_jiffies_64() + TA_LOGIN_TIMEOUT * HZ);
	login_timer.data = (unsigned long)conn;
	login_timer.function = iscsi_target_login_timeout;
	add_timer(&login_timer);
	pr_debug("Starting login_timer for %s/%d\n", current->comm, current->pid);

	rc = conn->conn_transport->iscsit_get_login_rx(conn, login);
	del_timer_sync(&login_timer);
	flush_signals(current);
	conn->login_kworker = NULL;

	if (rc < 0) {
		iscsi_target_restore_sock_callbacks(conn);
		iscsi_target_login_drop(conn, login);
		iscsit_deaccess_np(np, tpg, tpg_np);
		return;
	}

	pr_debug("iscsi_target_do_login_rx after rx_login_io, %p, %s:%d\n",
			conn, current->comm, current->pid);

	rc = iscsi_target_do_login(conn, login);
	if (rc < 0) {
		iscsi_target_restore_sock_callbacks(conn);
		iscsi_target_login_drop(conn, login);
		iscsit_deaccess_np(np, tpg, tpg_np);
	} else if (!rc) {
		if (conn->sock) {
			struct sock *sk = conn->sock->sk;

			write_lock_bh(&sk->sk_callback_lock);
			clear_bit(LOGIN_FLAGS_READ_ACTIVE, &conn->login_flags);
			write_unlock_bh(&sk->sk_callback_lock);
		}
	} else if (rc == 1) {
		iscsi_target_nego_release(conn);
		iscsi_post_login_handler(np, conn, zero_tsih);
		iscsit_deaccess_np(np, tpg, tpg_np);
	}
}

static void iscsi_target_do_cleanup(struct work_struct *work)
{
	struct iscsi_conn *conn = container_of(work,
				struct iscsi_conn, login_cleanup_work.work);
	struct sock *sk = conn->sock->sk;
	struct iscsi_login *login = conn->login;
	struct iscsi_np *np = login->np;
	struct iscsi_portal_group *tpg = conn->tpg;
	struct iscsi_tpg_np *tpg_np = conn->tpg_np;

	pr_debug("Entering iscsi_target_do_cleanup\n");

	cancel_delayed_work_sync(&conn->login_work);
	conn->orig_state_change(sk);

	iscsi_target_restore_sock_callbacks(conn);
	iscsi_target_login_drop(conn, login);
	iscsit_deaccess_np(np, tpg, tpg_np);

	pr_debug("iscsi_target_do_cleanup done()\n");
}

static void iscsi_target_sk_state_change(struct sock *sk)
{
	struct iscsi_conn *conn;
	void (*orig_state_change)(struct sock *);
	bool state;

	pr_debug("Entering iscsi_target_sk_state_change\n");

	write_lock_bh(&sk->sk_callback_lock);
	conn = sk->sk_user_data;
	if (!conn) {
		write_unlock_bh(&sk->sk_callback_lock);
		return;
	}
	orig_state_change = conn->orig_state_change;

	if (!test_bit(LOGIN_FLAGS_READY, &conn->login_flags)) {
		pr_debug("Got LOGIN_FLAGS_READY=0 sk_state_change conn: %p\n",
			 conn);
		write_unlock_bh(&sk->sk_callback_lock);
		orig_state_change(sk);
		return;
	}
	if (test_bit(LOGIN_FLAGS_READ_ACTIVE, &conn->login_flags)) {
		pr_debug("Got LOGIN_FLAGS_READ_ACTIVE=1 sk_state_change"
			 " conn: %p\n", conn);
		write_unlock_bh(&sk->sk_callback_lock);
		orig_state_change(sk);
		return;
	}
	if (test_and_set_bit(LOGIN_FLAGS_CLOSED, &conn->login_flags)) {
		pr_debug("Got LOGIN_FLAGS_CLOSED=1 sk_state_change conn: %p\n",
			 conn);
		write_unlock_bh(&sk->sk_callback_lock);
		orig_state_change(sk);
		return;
	}

	state = iscsi_target_sk_state_check(sk);
	write_unlock_bh(&sk->sk_callback_lock);

	pr_debug("iscsi_target_sk_state_change: state: %d\n", state);

	if (!state) {
		pr_debug("iscsi_target_sk_state_change got failed state\n");
		schedule_delayed_work(&conn->login_cleanup_work, 0);
		return;
	}
	orig_state_change(sk);
}

/*
 *	NOTE: We check for existing sessions or connections AFTER the initiator
 *	has been successfully authenticated in order to protect against faked
 *	ISID/TSIH combinations.
 */
static int iscsi_target_check_for_existing_instances(
	struct iscsi_conn *conn,
	struct iscsi_login *login)
{
	if (login->checked_for_existing)
		return 0;

	login->checked_for_existing = 1;

	if (!login->tsih)
		return iscsi_check_for_session_reinstatement(conn);
	else
		return iscsi_login_post_auth_non_zero_tsih(conn, login->cid,
				login->initial_exp_statsn);
}

static int iscsi_target_do_authentication(
	struct iscsi_conn *conn,
	struct iscsi_login *login)
{
	int authret;
	u32 payload_length;
	struct iscsi_param *param;
	struct iscsi_login_req *login_req;
	struct iscsi_login_rsp *login_rsp;

	login_req = (struct iscsi_login_req *) login->req;
	login_rsp = (struct iscsi_login_rsp *) login->rsp;
	payload_length = ntoh24(login_req->dlength);

	param = iscsi_find_param_from_key(AUTHMETHOD, conn->param_list);
	if (!param)
		return -1;

	authret = iscsi_handle_authentication(
			conn,
			login->req_buf,
			login->rsp_buf,
			payload_length,
			&login->rsp_length,
			param->value);
	switch (authret) {
	case 0:
		pr_debug("Received OK response"
		" from LIO Authentication, continuing.\n");
		break;
	case 1:
		pr_debug("iSCSI security negotiation"
			" completed successfully.\n");
		login->auth_complete = 1;
		if ((login_req->flags & ISCSI_FLAG_LOGIN_NEXT_STAGE1) &&
		    (login_req->flags & ISCSI_FLAG_LOGIN_TRANSIT)) {
			login_rsp->flags |= (ISCSI_FLAG_LOGIN_NEXT_STAGE1 |
					     ISCSI_FLAG_LOGIN_TRANSIT);
			login->current_stage = 1;
		}
		return iscsi_target_check_for_existing_instances(
				conn, login);
	case 2:
		pr_err("Security negotiation"
			" failed.\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_AUTH_FAILED);
		return -1;
	default:
		pr_err("Received unknown error %d from LIO"
				" Authentication\n", authret);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_TARGET_ERROR);
		return -1;
	}

	return 0;
}

static int iscsi_target_handle_csg_zero(
	struct iscsi_conn *conn,
	struct iscsi_login *login)
{
	int ret;
	u32 payload_length;
	struct iscsi_param *param;
	struct iscsi_login_req *login_req;
	struct iscsi_login_rsp *login_rsp;

	login_req = (struct iscsi_login_req *) login->req;
	login_rsp = (struct iscsi_login_rsp *) login->rsp;
	payload_length = ntoh24(login_req->dlength);

	param = iscsi_find_param_from_key(AUTHMETHOD, conn->param_list);
	if (!param)
		return -1;

	ret = iscsi_decode_text_input(
			PHASE_SECURITY|PHASE_DECLARATIVE,
			SENDER_INITIATOR|SENDER_RECEIVER,
			login->req_buf,
			payload_length,
			conn);
	if (ret < 0)
		return -1;

	if (ret > 0) {
		if (login->auth_complete) {
			pr_err("Initiator has already been"
				" successfully authenticated, but is still"
				" sending %s keys.\n", param->value);
			iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
					ISCSI_LOGIN_STATUS_INIT_ERR);
			return -1;
		}

		goto do_auth;
	}

	if (login->first_request)
		if (iscsi_target_check_first_request(conn, login) < 0)
			return -1;

	ret = iscsi_encode_text_output(
			PHASE_SECURITY|PHASE_DECLARATIVE,
			SENDER_TARGET,
			login->rsp_buf,
			&login->rsp_length,
			conn->param_list);
	if (ret < 0)
		return -1;

	if (!iscsi_check_negotiated_keys(conn->param_list)) {
		if (ISCSI_TPG_ATTRIB(ISCSI_TPG_C(conn))->authentication &&
		    !strncmp(param->value, NONE, 4)) {
			pr_err("Initiator sent AuthMethod=None but"
				" Target is enforcing iSCSI Authentication,"
					" login failed.\n");
			iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
					ISCSI_LOGIN_STATUS_AUTH_FAILED);
			return -1;
		}

		if (ISCSI_TPG_ATTRIB(ISCSI_TPG_C(conn))->authentication &&
		    !login->auth_complete)
			return 0;

		if (strncmp(param->value, NONE, 4) && !login->auth_complete)
			return 0;

		if ((login_req->flags & ISCSI_FLAG_LOGIN_NEXT_STAGE1) &&
		    (login_req->flags & ISCSI_FLAG_LOGIN_TRANSIT)) {
			login_rsp->flags |= ISCSI_FLAG_LOGIN_NEXT_STAGE1 |
					    ISCSI_FLAG_LOGIN_TRANSIT;
			login->current_stage = 1;
		}
	}

	return 0;
do_auth:
	return iscsi_target_do_authentication(conn, login);
}

static int iscsi_target_handle_csg_one(struct iscsi_conn *conn, struct iscsi_login *login)
{
	int ret;
	u32 payload_length;
	struct iscsi_login_req *login_req;
	struct iscsi_login_rsp *login_rsp;

	login_req = (struct iscsi_login_req *) login->req;
	login_rsp = (struct iscsi_login_rsp *) login->rsp;
	payload_length = ntoh24(login_req->dlength);

	ret = iscsi_decode_text_input(
			PHASE_OPERATIONAL|PHASE_DECLARATIVE,
			SENDER_INITIATOR|SENDER_RECEIVER,
			login->req_buf,
			payload_length,
			conn);
	if (ret < 0) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_INIT_ERR);
		return -1;
	}

	if (login->first_request)
		if (iscsi_target_check_first_request(conn, login) < 0)
			return -1;

	if (iscsi_target_check_for_existing_instances(conn, login) < 0)
		return -1;

	ret = iscsi_encode_text_output(
			PHASE_OPERATIONAL|PHASE_DECLARATIVE,
			SENDER_TARGET,
			login->rsp_buf,
			&login->rsp_length,
			conn->param_list);
	if (ret < 0) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_INIT_ERR);
		return -1;
	}

	if (!login->auth_complete &&
	     ISCSI_TPG_ATTRIB(ISCSI_TPG_C(conn))->authentication) {
		pr_err("Initiator is requesting CSG: 1, has not been"
			 " successfully authenticated, and the Target is"
			" enforcing iSCSI Authentication, login failed.\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_AUTH_FAILED);
		return -1;
	}

	if (!iscsi_check_negotiated_keys(conn->param_list))
		if ((login_req->flags & ISCSI_FLAG_LOGIN_NEXT_STAGE3) &&
		    (login_req->flags & ISCSI_FLAG_LOGIN_TRANSIT))
			login_rsp->flags |= ISCSI_FLAG_LOGIN_NEXT_STAGE3 |
					    ISCSI_FLAG_LOGIN_TRANSIT;

	return 0;
}

static int iscsi_target_do_login(struct iscsi_conn *conn, struct iscsi_login *login)
{
	int pdu_count = 0;
	struct iscsi_login_req *login_req;
	struct iscsi_login_rsp *login_rsp;

	login_req = (struct iscsi_login_req *) login->req;
	login_rsp = (struct iscsi_login_rsp *) login->rsp;

	while (1) {
		if (++pdu_count > MAX_LOGIN_PDUS) {
			pr_err("MAX_LOGIN_PDUS count reached.\n");
			iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
					ISCSI_LOGIN_STATUS_TARGET_ERROR);
			return -1;
		}

		switch (ISCSI_LOGIN_CURRENT_STAGE(login_req->flags)) {
		case 0:
			login_rsp->flags &= ~ISCSI_FLAG_LOGIN_CURRENT_STAGE_MASK;
			if (iscsi_target_handle_csg_zero(conn, login) < 0)
				return -1;
			break;
		case 1:
			login_rsp->flags |= ISCSI_FLAG_LOGIN_CURRENT_STAGE1;
			if (iscsi_target_handle_csg_one(conn, login) < 0)
				return -1;
			if (login_rsp->flags & ISCSI_FLAG_LOGIN_TRANSIT) {
				login->tsih = conn->sess->tsih;
				login->login_complete = 1;
				iscsi_target_restore_sock_callbacks(conn);
				if (iscsi_target_do_tx_login_io(conn,
						login) < 0)
					return -1;
				return 1;
			}
			break;
		default:
			pr_err("Illegal CSG: %d received from"
				" Initiator, protocol error.\n",
				ISCSI_LOGIN_CURRENT_STAGE(login_req->flags));
			break;
		}

		if (iscsi_target_do_tx_login_io(conn, login) < 0)
			return -1;

		if (login_rsp->flags & ISCSI_FLAG_LOGIN_TRANSIT) {
			login_rsp->flags &= ~ISCSI_FLAG_LOGIN_TRANSIT;
			login_rsp->flags &= ~ISCSI_FLAG_LOGIN_NEXT_STAGE_MASK;
		}
		break;
	}

	if (conn->sock) {
		struct sock *sk = conn->sock->sk;
		bool state;

		read_lock_bh(&sk->sk_callback_lock);
		state = iscsi_target_sk_state_check(sk);
		read_unlock_bh(&sk->sk_callback_lock);

		if (!state) {
			pr_debug("iscsi_target_do_login() failed state for"
				 " conn: %p\n", conn);
			return -1;
		}
	}

	return 0;
}

static void iscsi_initiatorname_tolower(
	char *param_buf)
{
	char *c;
	u32 iqn_size = strlen(param_buf), i;

	for (i = 0; i < iqn_size; i++) {
		c = &param_buf[i];
		if (!isupper(*c))
			continue;

		*c = tolower(*c);
	}
}

/*
 * Processes the first Login Request..
 */
int iscsi_target_locate_portal(
	struct iscsi_np *np,
	struct iscsi_conn *conn,
	struct iscsi_login *login)
{
	char *i_buf = NULL, *s_buf = NULL, *t_buf = NULL;
	char *tmpbuf, *start = NULL, *end = NULL, *key, *value;
	struct iscsi_session *sess = conn->sess;
	struct iscsi_tiqn *tiqn;
	struct iscsi_tpg_np *tpg_np = NULL;
	struct iscsi_login_req *login_req;
	struct se_node_acl *se_nacl;
	u32 payload_length, queue_depth = 0;
	int sessiontype = 0, ret = 0, tag_num, tag_size;

	INIT_DELAYED_WORK(&conn->login_work, iscsi_target_do_login_rx);
	INIT_DELAYED_WORK(&conn->login_cleanup_work, iscsi_target_do_cleanup);
	iscsi_target_set_sock_callbacks(conn);

	login->np = np;

	login_req = (struct iscsi_login_req *) login->req;
	payload_length = ntoh24(login_req->dlength);

	tmpbuf = kzalloc(payload_length + 1, GFP_KERNEL);
	if (!tmpbuf) {
		pr_err("Unable to allocate memory for tmpbuf.\n");
		return -1;
	}

	memcpy(tmpbuf, login->req_buf, payload_length);
	tmpbuf[payload_length] = '\0';
	start = tmpbuf;
	end = (start + payload_length);

	/*
	 * Locate the initial keys expected from the Initiator node in
	 * the first login request in order to progress with the login phase.
	 */
	while (start < end) {
		if (iscsi_extract_key_value(start, &key, &value) < 0) {
			ret = -1;
			goto out;
		}

		if (!strncmp(key, "InitiatorName", 13))
			i_buf = value;
		else if (!strncmp(key, "SessionType", 11))
			s_buf = value;
		else if (!strncmp(key, "TargetName", 10))
			t_buf = value;

		start += strlen(key) + strlen(value) + 2;
	}
	/*
	 * See 5.3.  Login Phase.
	 */
	if (!i_buf) {
		pr_err("InitiatorName key not received"
			" in first login request.\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
			ISCSI_LOGIN_STATUS_MISSING_FIELDS);
		ret = -1;
		goto out;
	}
	/*
	 * Convert the incoming InitiatorName to lowercase following
	 * RFC-3720 3.2.6.1. section c) that says that iSCSI IQNs
	 * are NOT case sensitive.
	 */
	iscsi_initiatorname_tolower(i_buf);

	if (!s_buf) {
		if (!login->leading_connection)
			goto get_target;

		pr_err("SessionType key not received"
			" in first login request.\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
			ISCSI_LOGIN_STATUS_MISSING_FIELDS);
		ret = -1;
		goto out;
	}

	/*
	 * Use default portal group for discovery sessions.
	 */
	sessiontype = strncmp(s_buf, DISCOVERY, 9);
	if (!sessiontype) {
		conn->tpg = iscsit_global->discovery_tpg;
		if (!login->leading_connection)
			goto get_target;

		sess->sess_ops->SessionType = 1;
		/*
		 * Setup crc32c modules from libcrypto
		 */
		if (iscsi_login_setup_crypto(conn) < 0) {
			pr_err("iscsi_login_setup_crypto() failed\n");
			ret = -1;
			goto out;
		}
		/*
		 * Serialize access across the discovery struct iscsi_portal_group to
		 * process login attempt.
		 */
		if (iscsit_access_np(np, conn->tpg) < 0) {
			iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_SVC_UNAVAILABLE);
			ret = -1;
			goto out;
		}
		ret = 0;
		goto alloc_tags;
	}

get_target:
	if (!t_buf) {
		pr_err("TargetName key not received"
			" in first login request while"
			" SessionType=Normal.\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
			ISCSI_LOGIN_STATUS_MISSING_FIELDS);
		ret = -1;
		goto out;
	}

	/*
	 * Locate Target IQN from Storage Node.
	 */
	tiqn = iscsit_get_tiqn_for_login(t_buf);
	if (!tiqn) {
		pr_err("Unable to locate Target IQN: %s in"
			" Storage Node\n", t_buf);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_SVC_UNAVAILABLE);
		ret = -1;
		goto out;
	}
	pr_debug("Located Storage Object: %s\n", tiqn->tiqn);

	/*
	 * Locate Target Portal Group from Storage Node.
	 */
	conn->tpg = iscsit_get_tpg_from_np(tiqn, np, &tpg_np);
	if (!conn->tpg) {
		pr_err("Unable to locate Target Portal Group"
				" on %s\n", tiqn->tiqn);
		iscsit_put_tiqn_for_login(tiqn);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_SVC_UNAVAILABLE);
		ret = -1;
		goto out;
	}
	conn->tpg_np = tpg_np;
	pr_debug("Located Portal Group Object: %hu\n", conn->tpg->tpgt);
	/*
	 * Setup crc32c modules from libcrypto
	 */
	if (iscsi_login_setup_crypto(conn) < 0) {
		pr_err("iscsi_login_setup_crypto() failed\n");
		kref_put(&tpg_np->tpg_np_kref, iscsit_login_kref_put);
		iscsit_put_tiqn_for_login(tiqn);
		conn->tpg = NULL;
		ret = -1;
		goto out;
	}
	/*
	 * Serialize access across the struct iscsi_portal_group to
	 * process login attempt.
	 */
	if (iscsit_access_np(np, conn->tpg) < 0) {
		kref_put(&tpg_np->tpg_np_kref, iscsit_login_kref_put);
		iscsit_put_tiqn_for_login(tiqn);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_SVC_UNAVAILABLE);
		conn->tpg = NULL;
		ret = -1;
		goto out;
	}

	/*
	 * conn->sess->node_acl will be set when the referenced
	 * struct iscsi_session is located from received ISID+TSIH in
	 * iscsi_login_non_zero_tsih_s2().
	 */
	if (!login->leading_connection) {
		ret = 0;
		goto out;
	}

	/*
	 * This value is required in iscsi_login_zero_tsih_s2()
	 */
	sess->sess_ops->SessionType = 0;

	/*
	 * Locate incoming Initiator IQN reference from Storage Node.
	 */
	sess->se_sess->se_node_acl = core_tpg_check_initiator_node_acl(
			&conn->tpg->tpg_se_tpg, i_buf);
	if (!sess->se_sess->se_node_acl) {
		pr_err("iSCSI Initiator Node: %s is not authorized to"
			" access iSCSI target portal group: %hu.\n",
				i_buf, conn->tpg->tpgt);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_TGT_FORBIDDEN);
		ret = -1;
		goto out;
	}
	se_nacl = sess->se_sess->se_node_acl;
	queue_depth = se_nacl->queue_depth;
	/*
	 * Setup pre-allocated tags based upon allowed per NodeACL CmdSN
	 * depth for non immediate commands, plus extra tags for immediate
	 * commands.
	 *
	 * Also enforce a ISCSIT_MIN_TAGS to prevent unnecessary contention
	 * in per-cpu-ida tag allocation logic + small queue_depth.
	 */
alloc_tags:
	tag_num = max_t(u32, ISCSIT_MIN_TAGS, queue_depth);
	tag_num += (tag_num / 2) + ISCSIT_EXTRA_TAGS;
	tag_size = sizeof(struct iscsi_cmd) + conn->conn_transport->priv_size;

	ret = transport_alloc_session_tags(sess->se_sess, tag_num, tag_size);
	if (ret < 0) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				    ISCSI_LOGIN_STATUS_NO_RESOURCES);
		ret = -1;
	}
out:
	kfree(tmpbuf);
	return ret;
}

int iscsi_target_start_negotiation(
	struct iscsi_login *login,
	struct iscsi_conn *conn)
{
	int ret;

	ret = iscsi_target_do_login(conn, login);
	if (!ret) {
		if (conn->sock) {
			struct sock *sk = conn->sock->sk;

			write_lock_bh(&sk->sk_callback_lock);
			set_bit(LOGIN_FLAGS_READY, &conn->login_flags);
			write_unlock_bh(&sk->sk_callback_lock);
		}
	} else if (ret < 0) {
		cancel_delayed_work_sync(&conn->login_work);
		cancel_delayed_work_sync(&conn->login_cleanup_work);
		iscsi_target_restore_sock_callbacks(conn);
		iscsi_remove_failed_auth_entry(conn);
	}
	if (ret != 0)
		iscsi_target_nego_release(conn);

	return ret;
}

void iscsi_target_nego_release(struct iscsi_conn *conn)
{
	struct iscsi_login *login = conn->conn_login;

	if (!login)
		return;

	kfree(login->req_buf);
	kfree(login->rsp_buf);
	kfree(login);

	conn->conn_login = NULL;
}
