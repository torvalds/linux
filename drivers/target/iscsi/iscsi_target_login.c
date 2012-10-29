/*******************************************************************************
 * This file contains the login functions used by the iSCSI Target driver.
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
 ******************************************************************************/

#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/crypto.h>
#include <linux/idr.h>
#include <scsi/iscsi_proto.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include "iscsi_target_core.h"
#include "iscsi_target_tq.h"
#include "iscsi_target_device.h"
#include "iscsi_target_nego.h"
#include "iscsi_target_erl0.h"
#include "iscsi_target_erl2.h"
#include "iscsi_target_login.h"
#include "iscsi_target_stat.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target.h"
#include "iscsi_target_parameters.h"

static int iscsi_login_init_conn(struct iscsi_conn *conn)
{
	INIT_LIST_HEAD(&conn->conn_list);
	INIT_LIST_HEAD(&conn->conn_cmd_list);
	INIT_LIST_HEAD(&conn->immed_queue_list);
	INIT_LIST_HEAD(&conn->response_queue_list);
	init_completion(&conn->conn_post_wait_comp);
	init_completion(&conn->conn_wait_comp);
	init_completion(&conn->conn_wait_rcfr_comp);
	init_completion(&conn->conn_waiting_on_uc_comp);
	init_completion(&conn->conn_logout_comp);
	init_completion(&conn->rx_half_close_comp);
	init_completion(&conn->tx_half_close_comp);
	spin_lock_init(&conn->cmd_lock);
	spin_lock_init(&conn->conn_usage_lock);
	spin_lock_init(&conn->immed_queue_lock);
	spin_lock_init(&conn->nopin_timer_lock);
	spin_lock_init(&conn->response_queue_lock);
	spin_lock_init(&conn->state_lock);

	if (!zalloc_cpumask_var(&conn->conn_cpumask, GFP_KERNEL)) {
		pr_err("Unable to allocate conn->conn_cpumask\n");
		return -ENOMEM;
	}

	return 0;
}

/*
 * Used by iscsi_target_nego.c:iscsi_target_locate_portal() to setup
 * per struct iscsi_conn libcrypto contexts for crc32c and crc32-intel
 */
int iscsi_login_setup_crypto(struct iscsi_conn *conn)
{
	/*
	 * Setup slicing by CRC32C algorithm for RX and TX libcrypto contexts
	 * which will default to crc32c_intel.ko for cpu_has_xmm4_2, or fallback
	 * to software 1x8 byte slicing from crc32c.ko
	 */
	conn->conn_rx_hash.flags = 0;
	conn->conn_rx_hash.tfm = crypto_alloc_hash("crc32c", 0,
						CRYPTO_ALG_ASYNC);
	if (IS_ERR(conn->conn_rx_hash.tfm)) {
		pr_err("crypto_alloc_hash() failed for conn_rx_tfm\n");
		return -ENOMEM;
	}

	conn->conn_tx_hash.flags = 0;
	conn->conn_tx_hash.tfm = crypto_alloc_hash("crc32c", 0,
						CRYPTO_ALG_ASYNC);
	if (IS_ERR(conn->conn_tx_hash.tfm)) {
		pr_err("crypto_alloc_hash() failed for conn_tx_tfm\n");
		crypto_free_hash(conn->conn_rx_hash.tfm);
		return -ENOMEM;
	}

	return 0;
}

static int iscsi_login_check_initiator_version(
	struct iscsi_conn *conn,
	u8 version_max,
	u8 version_min)
{
	if ((version_max != 0x00) || (version_min != 0x00)) {
		pr_err("Unsupported iSCSI IETF Pre-RFC Revision,"
			" version Min/Max 0x%02x/0x%02x, rejecting login.\n",
			version_min, version_max);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_NO_VERSION);
		return -1;
	}

	return 0;
}

int iscsi_check_for_session_reinstatement(struct iscsi_conn *conn)
{
	int sessiontype;
	struct iscsi_param *initiatorname_param = NULL, *sessiontype_param = NULL;
	struct iscsi_portal_group *tpg = conn->tpg;
	struct iscsi_session *sess = NULL, *sess_p = NULL;
	struct se_portal_group *se_tpg = &tpg->tpg_se_tpg;
	struct se_session *se_sess, *se_sess_tmp;

	initiatorname_param = iscsi_find_param_from_key(
			INITIATORNAME, conn->param_list);
	if (!initiatorname_param)
		return -1;

	sessiontype_param = iscsi_find_param_from_key(
			SESSIONTYPE, conn->param_list);
	if (!sessiontype_param)
		return -1;

	sessiontype = (strncmp(sessiontype_param->value, NORMAL, 6)) ? 1 : 0;

	spin_lock_bh(&se_tpg->session_lock);
	list_for_each_entry_safe(se_sess, se_sess_tmp, &se_tpg->tpg_sess_list,
			sess_list) {

		sess_p = se_sess->fabric_sess_ptr;
		spin_lock(&sess_p->conn_lock);
		if (atomic_read(&sess_p->session_fall_back_to_erl0) ||
		    atomic_read(&sess_p->session_logout) ||
		    (sess_p->time2retain_timer_flags & ISCSI_TF_EXPIRED)) {
			spin_unlock(&sess_p->conn_lock);
			continue;
		}
		if (!memcmp(sess_p->isid, conn->sess->isid, 6) &&
		   (!strcmp(sess_p->sess_ops->InitiatorName,
			    initiatorname_param->value) &&
		   (sess_p->sess_ops->SessionType == sessiontype))) {
			atomic_set(&sess_p->session_reinstatement, 1);
			spin_unlock(&sess_p->conn_lock);
			iscsit_inc_session_usage_count(sess_p);
			iscsit_stop_time2retain_timer(sess_p);
			sess = sess_p;
			break;
		}
		spin_unlock(&sess_p->conn_lock);
	}
	spin_unlock_bh(&se_tpg->session_lock);
	/*
	 * If the Time2Retain handler has expired, the session is already gone.
	 */
	if (!sess)
		return 0;

	pr_debug("%s iSCSI Session SID %u is still active for %s,"
		" preforming session reinstatement.\n", (sessiontype) ?
		"Discovery" : "Normal", sess->sid,
		sess->sess_ops->InitiatorName);

	spin_lock_bh(&sess->conn_lock);
	if (sess->session_state == TARG_SESS_STATE_FAILED) {
		spin_unlock_bh(&sess->conn_lock);
		iscsit_dec_session_usage_count(sess);
		target_put_session(sess->se_sess);
		return 0;
	}
	spin_unlock_bh(&sess->conn_lock);

	iscsit_stop_session(sess, 1, 1);
	iscsit_dec_session_usage_count(sess);

	target_put_session(sess->se_sess);
	return 0;
}

static void iscsi_login_set_conn_values(
	struct iscsi_session *sess,
	struct iscsi_conn *conn,
	__be16 cid)
{
	conn->sess		= sess;
	conn->cid		= be16_to_cpu(cid);
	/*
	 * Generate a random Status sequence number (statsn) for the new
	 * iSCSI connection.
	 */
	get_random_bytes(&conn->stat_sn, sizeof(u32));

	mutex_lock(&auth_id_lock);
	conn->auth_id		= iscsit_global->auth_id++;
	mutex_unlock(&auth_id_lock);
}

/*
 *	This is the leading connection of a new session,
 *	or session reinstatement.
 */
static int iscsi_login_zero_tsih_s1(
	struct iscsi_conn *conn,
	unsigned char *buf)
{
	struct iscsi_session *sess = NULL;
	struct iscsi_login_req *pdu = (struct iscsi_login_req *)buf;
	int ret;

	sess = kzalloc(sizeof(struct iscsi_session), GFP_KERNEL);
	if (!sess) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		pr_err("Could not allocate memory for session\n");
		return -ENOMEM;
	}

	iscsi_login_set_conn_values(sess, conn, pdu->cid);
	sess->init_task_tag	= pdu->itt;
	memcpy(&sess->isid, pdu->isid, 6);
	sess->exp_cmd_sn	= be32_to_cpu(pdu->cmdsn);
	INIT_LIST_HEAD(&sess->sess_conn_list);
	INIT_LIST_HEAD(&sess->sess_ooo_cmdsn_list);
	INIT_LIST_HEAD(&sess->cr_active_list);
	INIT_LIST_HEAD(&sess->cr_inactive_list);
	init_completion(&sess->async_msg_comp);
	init_completion(&sess->reinstatement_comp);
	init_completion(&sess->session_wait_comp);
	init_completion(&sess->session_waiting_on_uc_comp);
	mutex_init(&sess->cmdsn_mutex);
	spin_lock_init(&sess->conn_lock);
	spin_lock_init(&sess->cr_a_lock);
	spin_lock_init(&sess->cr_i_lock);
	spin_lock_init(&sess->session_usage_lock);
	spin_lock_init(&sess->ttt_lock);

	if (!idr_pre_get(&sess_idr, GFP_KERNEL)) {
		pr_err("idr_pre_get() for sess_idr failed\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		kfree(sess);
		return -ENOMEM;
	}
	spin_lock(&sess_idr_lock);
	ret = idr_get_new(&sess_idr, NULL, &sess->session_index);
	spin_unlock(&sess_idr_lock);

	if (ret < 0) {
		pr_err("idr_get_new() for sess_idr failed\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		kfree(sess);
		return -ENOMEM;
	}

	sess->creation_time = get_jiffies_64();
	spin_lock_init(&sess->session_stats_lock);
	/*
	 * The FFP CmdSN window values will be allocated from the TPG's
	 * Initiator Node's ACL once the login has been successfully completed.
	 */
	sess->max_cmd_sn	= be32_to_cpu(pdu->cmdsn);

	sess->sess_ops = kzalloc(sizeof(struct iscsi_sess_ops), GFP_KERNEL);
	if (!sess->sess_ops) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		pr_err("Unable to allocate memory for"
				" struct iscsi_sess_ops.\n");
		kfree(sess);
		return -ENOMEM;
	}

	sess->se_sess = transport_init_session();
	if (IS_ERR(sess->se_sess)) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		kfree(sess);
		return -ENOMEM;
	}

	return 0;
}

static int iscsi_login_zero_tsih_s2(
	struct iscsi_conn *conn)
{
	struct iscsi_node_attrib *na;
	struct iscsi_session *sess = conn->sess;
	unsigned char buf[32];

	sess->tpg = conn->tpg;

	/*
	 * Assign a new TPG Session Handle.  Note this is protected with
	 * struct iscsi_portal_group->np_login_sem from iscsit_access_np().
	 */
	sess->tsih = ++ISCSI_TPG_S(sess)->ntsih;
	if (!sess->tsih)
		sess->tsih = ++ISCSI_TPG_S(sess)->ntsih;

	/*
	 * Create the default params from user defined values..
	 */
	if (iscsi_copy_param_list(&conn->param_list,
				ISCSI_TPG_C(conn)->param_list, 1) < 0) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		return -1;
	}

	iscsi_set_keys_to_negotiate(0, conn->param_list);

	if (sess->sess_ops->SessionType)
		return iscsi_set_keys_irrelevant_for_discovery(
				conn->param_list);

	na = iscsit_tpg_get_node_attrib(sess);

	/*
	 * Need to send TargetPortalGroupTag back in first login response
	 * on any iSCSI connection where the Initiator provides TargetName.
	 * See 5.3.1.  Login Phase Start
	 *
	 * In our case, we have already located the struct iscsi_tiqn at this point.
	 */
	memset(buf, 0, 32);
	sprintf(buf, "TargetPortalGroupTag=%hu", ISCSI_TPG_S(sess)->tpgt);
	if (iscsi_change_param_value(buf, conn->param_list, 0) < 0) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		return -1;
	}

	/*
	 * Workaround for Initiators that have broken connection recovery logic.
	 *
	 * "We would really like to get rid of this." Linux-iSCSI.org team
	 */
	memset(buf, 0, 32);
	sprintf(buf, "ErrorRecoveryLevel=%d", na->default_erl);
	if (iscsi_change_param_value(buf, conn->param_list, 0) < 0) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		return -1;
	}

	if (iscsi_login_disable_FIM_keys(conn->param_list, conn) < 0)
		return -1;

	return 0;
}

/*
 * Remove PSTATE_NEGOTIATE for the four FIM related keys.
 * The Initiator node will be able to enable FIM by proposing them itself.
 */
int iscsi_login_disable_FIM_keys(
	struct iscsi_param_list *param_list,
	struct iscsi_conn *conn)
{
	struct iscsi_param *param;

	param = iscsi_find_param_from_key("OFMarker", param_list);
	if (!param) {
		pr_err("iscsi_find_param_from_key() for"
				" OFMarker failed\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		return -1;
	}
	param->state &= ~PSTATE_NEGOTIATE;

	param = iscsi_find_param_from_key("OFMarkInt", param_list);
	if (!param) {
		pr_err("iscsi_find_param_from_key() for"
				" IFMarker failed\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		return -1;
	}
	param->state &= ~PSTATE_NEGOTIATE;

	param = iscsi_find_param_from_key("IFMarker", param_list);
	if (!param) {
		pr_err("iscsi_find_param_from_key() for"
				" IFMarker failed\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		return -1;
	}
	param->state &= ~PSTATE_NEGOTIATE;

	param = iscsi_find_param_from_key("IFMarkInt", param_list);
	if (!param) {
		pr_err("iscsi_find_param_from_key() for"
				" IFMarker failed\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		return -1;
	}
	param->state &= ~PSTATE_NEGOTIATE;

	return 0;
}

static int iscsi_login_non_zero_tsih_s1(
	struct iscsi_conn *conn,
	unsigned char *buf)
{
	struct iscsi_login_req *pdu = (struct iscsi_login_req *)buf;

	iscsi_login_set_conn_values(NULL, conn, pdu->cid);
	return 0;
}

/*
 *	Add a new connection to an existing session.
 */
static int iscsi_login_non_zero_tsih_s2(
	struct iscsi_conn *conn,
	unsigned char *buf)
{
	struct iscsi_portal_group *tpg = conn->tpg;
	struct iscsi_session *sess = NULL, *sess_p = NULL;
	struct se_portal_group *se_tpg = &tpg->tpg_se_tpg;
	struct se_session *se_sess, *se_sess_tmp;
	struct iscsi_login_req *pdu = (struct iscsi_login_req *)buf;

	spin_lock_bh(&se_tpg->session_lock);
	list_for_each_entry_safe(se_sess, se_sess_tmp, &se_tpg->tpg_sess_list,
			sess_list) {

		sess_p = (struct iscsi_session *)se_sess->fabric_sess_ptr;
		if (atomic_read(&sess_p->session_fall_back_to_erl0) ||
		    atomic_read(&sess_p->session_logout) ||
		   (sess_p->time2retain_timer_flags & ISCSI_TF_EXPIRED))
			continue;
		if (!memcmp(sess_p->isid, pdu->isid, 6) &&
		     (sess_p->tsih == be16_to_cpu(pdu->tsih))) {
			iscsit_inc_session_usage_count(sess_p);
			iscsit_stop_time2retain_timer(sess_p);
			sess = sess_p;
			break;
		}
	}
	spin_unlock_bh(&se_tpg->session_lock);

	/*
	 * If the Time2Retain handler has expired, the session is already gone.
	 */
	if (!sess) {
		pr_err("Initiator attempting to add a connection to"
			" a non-existent session, rejecting iSCSI Login.\n");
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_NO_SESSION);
		return -1;
	}

	/*
	 * Stop the Time2Retain timer if this is a failed session, we restart
	 * the timer if the login is not successful.
	 */
	spin_lock_bh(&sess->conn_lock);
	if (sess->session_state == TARG_SESS_STATE_FAILED)
		atomic_set(&sess->session_continuation, 1);
	spin_unlock_bh(&sess->conn_lock);

	iscsi_login_set_conn_values(sess, conn, pdu->cid);

	if (iscsi_copy_param_list(&conn->param_list,
			ISCSI_TPG_C(conn)->param_list, 0) < 0) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		return -1;
	}

	iscsi_set_keys_to_negotiate(0, conn->param_list);
	/*
	 * Need to send TargetPortalGroupTag back in first login response
	 * on any iSCSI connection where the Initiator provides TargetName.
	 * See 5.3.1.  Login Phase Start
	 *
	 * In our case, we have already located the struct iscsi_tiqn at this point.
	 */
	memset(buf, 0, 32);
	sprintf(buf, "TargetPortalGroupTag=%hu", ISCSI_TPG_S(sess)->tpgt);
	if (iscsi_change_param_value(buf, conn->param_list, 0) < 0) {
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_NO_RESOURCES);
		return -1;
	}

	return iscsi_login_disable_FIM_keys(conn->param_list, conn);
}

int iscsi_login_post_auth_non_zero_tsih(
	struct iscsi_conn *conn,
	u16 cid,
	u32 exp_statsn)
{
	struct iscsi_conn *conn_ptr = NULL;
	struct iscsi_conn_recovery *cr = NULL;
	struct iscsi_session *sess = conn->sess;

	/*
	 * By following item 5 in the login table,  if we have found
	 * an existing ISID and a valid/existing TSIH and an existing
	 * CID we do connection reinstatement.  Currently we dont not
	 * support it so we send back an non-zero status class to the
	 * initiator and release the new connection.
	 */
	conn_ptr = iscsit_get_conn_from_cid_rcfr(sess, cid);
	if (conn_ptr) {
		pr_err("Connection exists with CID %hu for %s,"
			" performing connection reinstatement.\n",
			conn_ptr->cid, sess->sess_ops->InitiatorName);

		iscsit_connection_reinstatement_rcfr(conn_ptr);
		iscsit_dec_conn_usage_count(conn_ptr);
	}

	/*
	 * Check for any connection recovery entires containing CID.
	 * We use the original ExpStatSN sent in the first login request
	 * to acknowledge commands for the failed connection.
	 *
	 * Also note that an explict logout may have already been sent,
	 * but the response may not be sent due to additional connection
	 * loss.
	 */
	if (sess->sess_ops->ErrorRecoveryLevel == 2) {
		cr = iscsit_get_inactive_connection_recovery_entry(
				sess, cid);
		if (cr) {
			pr_debug("Performing implicit logout"
				" for connection recovery on CID: %hu\n",
					conn->cid);
			iscsit_discard_cr_cmds_by_expstatsn(cr, exp_statsn);
		}
	}

	/*
	 * Else we follow item 4 from the login table in that we have
	 * found an existing ISID and a valid/existing TSIH and a new
	 * CID we go ahead and continue to add a new connection to the
	 * session.
	 */
	pr_debug("Adding CID %hu to existing session for %s.\n",
			cid, sess->sess_ops->InitiatorName);

	if ((atomic_read(&sess->nconn) + 1) > sess->sess_ops->MaxConnections) {
		pr_err("Adding additional connection to this session"
			" would exceed MaxConnections %d, login failed.\n",
				sess->sess_ops->MaxConnections);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				ISCSI_LOGIN_STATUS_ISID_ERROR);
		return -1;
	}

	return 0;
}

static void iscsi_post_login_start_timers(struct iscsi_conn *conn)
{
	struct iscsi_session *sess = conn->sess;

	if (!sess->sess_ops->SessionType)
		iscsit_start_nopin_timer(conn);
}

static int iscsi_post_login_handler(
	struct iscsi_np *np,
	struct iscsi_conn *conn,
	u8 zero_tsih)
{
	int stop_timer = 0;
	struct iscsi_session *sess = conn->sess;
	struct se_session *se_sess = sess->se_sess;
	struct iscsi_portal_group *tpg = ISCSI_TPG_S(sess);
	struct se_portal_group *se_tpg = &tpg->tpg_se_tpg;
	struct iscsi_thread_set *ts;

	iscsit_inc_conn_usage_count(conn);

	iscsit_collect_login_stats(conn, ISCSI_STATUS_CLS_SUCCESS,
			ISCSI_LOGIN_STATUS_ACCEPT);

	pr_debug("Moving to TARG_CONN_STATE_LOGGED_IN.\n");
	conn->conn_state = TARG_CONN_STATE_LOGGED_IN;

	iscsi_set_connection_parameters(conn->conn_ops, conn->param_list);
	iscsit_set_sync_and_steering_values(conn);
	/*
	 * SCSI Initiator -> SCSI Target Port Mapping
	 */
	ts = iscsi_get_thread_set();
	if (!zero_tsih) {
		iscsi_set_session_parameters(sess->sess_ops,
				conn->param_list, 0);
		iscsi_release_param_list(conn->param_list);
		conn->param_list = NULL;

		spin_lock_bh(&sess->conn_lock);
		atomic_set(&sess->session_continuation, 0);
		if (sess->session_state == TARG_SESS_STATE_FAILED) {
			pr_debug("Moving to"
					" TARG_SESS_STATE_LOGGED_IN.\n");
			sess->session_state = TARG_SESS_STATE_LOGGED_IN;
			stop_timer = 1;
		}

		pr_debug("iSCSI Login successful on CID: %hu from %s to"
			" %s:%hu,%hu\n", conn->cid, conn->login_ip,
			conn->local_ip, conn->local_port, tpg->tpgt);

		list_add_tail(&conn->conn_list, &sess->sess_conn_list);
		atomic_inc(&sess->nconn);
		pr_debug("Incremented iSCSI Connection count to %hu"
			" from node: %s\n", atomic_read(&sess->nconn),
			sess->sess_ops->InitiatorName);
		spin_unlock_bh(&sess->conn_lock);

		iscsi_post_login_start_timers(conn);
		iscsi_activate_thread_set(conn, ts);
		/*
		 * Determine CPU mask to ensure connection's RX and TX kthreads
		 * are scheduled on the same CPU.
		 */
		iscsit_thread_get_cpumask(conn);
		conn->conn_rx_reset_cpumask = 1;
		conn->conn_tx_reset_cpumask = 1;

		iscsit_dec_conn_usage_count(conn);
		if (stop_timer) {
			spin_lock_bh(&se_tpg->session_lock);
			iscsit_stop_time2retain_timer(sess);
			spin_unlock_bh(&se_tpg->session_lock);
		}
		iscsit_dec_session_usage_count(sess);
		return 0;
	}

	iscsi_set_session_parameters(sess->sess_ops, conn->param_list, 1);
	iscsi_release_param_list(conn->param_list);
	conn->param_list = NULL;

	iscsit_determine_maxcmdsn(sess);

	spin_lock_bh(&se_tpg->session_lock);
	__transport_register_session(&sess->tpg->tpg_se_tpg,
			se_sess->se_node_acl, se_sess, sess);
	pr_debug("Moving to TARG_SESS_STATE_LOGGED_IN.\n");
	sess->session_state = TARG_SESS_STATE_LOGGED_IN;

	pr_debug("iSCSI Login successful on CID: %hu from %s to %s:%hu,%hu\n",
		conn->cid, conn->login_ip, conn->local_ip, conn->local_port,
		tpg->tpgt);

	spin_lock_bh(&sess->conn_lock);
	list_add_tail(&conn->conn_list, &sess->sess_conn_list);
	atomic_inc(&sess->nconn);
	pr_debug("Incremented iSCSI Connection count to %hu from node:"
		" %s\n", atomic_read(&sess->nconn),
		sess->sess_ops->InitiatorName);
	spin_unlock_bh(&sess->conn_lock);

	sess->sid = tpg->sid++;
	if (!sess->sid)
		sess->sid = tpg->sid++;
	pr_debug("Established iSCSI session from node: %s\n",
			sess->sess_ops->InitiatorName);

	tpg->nsessions++;
	if (tpg->tpg_tiqn)
		tpg->tpg_tiqn->tiqn_nsessions++;

	pr_debug("Incremented number of active iSCSI sessions to %u on"
		" iSCSI Target Portal Group: %hu\n", tpg->nsessions, tpg->tpgt);
	spin_unlock_bh(&se_tpg->session_lock);

	iscsi_post_login_start_timers(conn);
	iscsi_activate_thread_set(conn, ts);
	/*
	 * Determine CPU mask to ensure connection's RX and TX kthreads
	 * are scheduled on the same CPU.
	 */
	iscsit_thread_get_cpumask(conn);
	conn->conn_rx_reset_cpumask = 1;
	conn->conn_tx_reset_cpumask = 1;

	iscsit_dec_conn_usage_count(conn);

	return 0;
}

static void iscsi_handle_login_thread_timeout(unsigned long data)
{
	struct iscsi_np *np = (struct iscsi_np *) data;

	spin_lock_bh(&np->np_thread_lock);
	pr_err("iSCSI Login timeout on Network Portal %s:%hu\n",
			np->np_ip, np->np_port);

	if (np->np_login_timer_flags & ISCSI_TF_STOP) {
		spin_unlock_bh(&np->np_thread_lock);
		return;
	}

	if (np->np_thread)
		send_sig(SIGINT, np->np_thread, 1);

	np->np_login_timer_flags &= ~ISCSI_TF_RUNNING;
	spin_unlock_bh(&np->np_thread_lock);
}

static void iscsi_start_login_thread_timer(struct iscsi_np *np)
{
	/*
	 * This used the TA_LOGIN_TIMEOUT constant because at this
	 * point we do not have access to ISCSI_TPG_ATTRIB(tpg)->login_timeout
	 */
	spin_lock_bh(&np->np_thread_lock);
	init_timer(&np->np_login_timer);
	np->np_login_timer.expires = (get_jiffies_64() + TA_LOGIN_TIMEOUT * HZ);
	np->np_login_timer.data = (unsigned long)np;
	np->np_login_timer.function = iscsi_handle_login_thread_timeout;
	np->np_login_timer_flags &= ~ISCSI_TF_STOP;
	np->np_login_timer_flags |= ISCSI_TF_RUNNING;
	add_timer(&np->np_login_timer);

	pr_debug("Added timeout timer to iSCSI login request for"
			" %u seconds.\n", TA_LOGIN_TIMEOUT);
	spin_unlock_bh(&np->np_thread_lock);
}

static void iscsi_stop_login_thread_timer(struct iscsi_np *np)
{
	spin_lock_bh(&np->np_thread_lock);
	if (!(np->np_login_timer_flags & ISCSI_TF_RUNNING)) {
		spin_unlock_bh(&np->np_thread_lock);
		return;
	}
	np->np_login_timer_flags |= ISCSI_TF_STOP;
	spin_unlock_bh(&np->np_thread_lock);

	del_timer_sync(&np->np_login_timer);

	spin_lock_bh(&np->np_thread_lock);
	np->np_login_timer_flags &= ~ISCSI_TF_RUNNING;
	spin_unlock_bh(&np->np_thread_lock);
}

int iscsi_target_setup_login_socket(
	struct iscsi_np *np,
	struct __kernel_sockaddr_storage *sockaddr)
{
	struct socket *sock;
	int backlog = 5, ret, opt = 0, len;

	switch (np->np_network_transport) {
	case ISCSI_TCP:
		np->np_ip_proto = IPPROTO_TCP;
		np->np_sock_type = SOCK_STREAM;
		break;
	case ISCSI_SCTP_TCP:
		np->np_ip_proto = IPPROTO_SCTP;
		np->np_sock_type = SOCK_STREAM;
		break;
	case ISCSI_SCTP_UDP:
		np->np_ip_proto = IPPROTO_SCTP;
		np->np_sock_type = SOCK_SEQPACKET;
		break;
	case ISCSI_IWARP_TCP:
	case ISCSI_IWARP_SCTP:
	case ISCSI_INFINIBAND:
	default:
		pr_err("Unsupported network_transport: %d\n",
				np->np_network_transport);
		return -EINVAL;
	}

	ret = sock_create(sockaddr->ss_family, np->np_sock_type,
			np->np_ip_proto, &sock);
	if (ret < 0) {
		pr_err("sock_create() failed.\n");
		return ret;
	}
	np->np_socket = sock;
	/*
	 * Setup the np->np_sockaddr from the passed sockaddr setup
	 * in iscsi_target_configfs.c code..
	 */
	memcpy(&np->np_sockaddr, sockaddr,
			sizeof(struct __kernel_sockaddr_storage));

	if (sockaddr->ss_family == AF_INET6)
		len = sizeof(struct sockaddr_in6);
	else
		len = sizeof(struct sockaddr_in);
	/*
	 * Set SO_REUSEADDR, and disable Nagel Algorithm with TCP_NODELAY.
	 */
	/* FIXME: Someone please explain why this is endian-safe */
	opt = 1;
	if (np->np_network_transport == ISCSI_TCP) {
		ret = kernel_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
				(char *)&opt, sizeof(opt));
		if (ret < 0) {
			pr_err("kernel_setsockopt() for TCP_NODELAY"
				" failed: %d\n", ret);
			goto fail;
		}
	}

	/* FIXME: Someone please explain why this is endian-safe */
	ret = kernel_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
			(char *)&opt, sizeof(opt));
	if (ret < 0) {
		pr_err("kernel_setsockopt() for SO_REUSEADDR"
			" failed\n");
		goto fail;
	}

	ret = kernel_setsockopt(sock, IPPROTO_IP, IP_FREEBIND,
			(char *)&opt, sizeof(opt));
	if (ret < 0) {
		pr_err("kernel_setsockopt() for IP_FREEBIND"
			" failed\n");
		goto fail;
	}

	ret = kernel_bind(sock, (struct sockaddr *)&np->np_sockaddr, len);
	if (ret < 0) {
		pr_err("kernel_bind() failed: %d\n", ret);
		goto fail;
	}

	ret = kernel_listen(sock, backlog);
	if (ret != 0) {
		pr_err("kernel_listen() failed: %d\n", ret);
		goto fail;
	}

	return 0;

fail:
	np->np_socket = NULL;
	if (sock)
		sock_release(sock);
	return ret;
}

static int __iscsi_target_login_thread(struct iscsi_np *np)
{
	u8 buffer[ISCSI_HDR_LEN], iscsi_opcode, zero_tsih = 0;
	int err, ret = 0, stop;
	struct iscsi_conn *conn = NULL;
	struct iscsi_login *login;
	struct iscsi_portal_group *tpg = NULL;
	struct socket *new_sock, *sock;
	struct kvec iov;
	struct iscsi_login_req *pdu;
	struct sockaddr_in sock_in;
	struct sockaddr_in6 sock_in6;

	flush_signals(current);
	sock = np->np_socket;

	spin_lock_bh(&np->np_thread_lock);
	if (np->np_thread_state == ISCSI_NP_THREAD_RESET) {
		np->np_thread_state = ISCSI_NP_THREAD_ACTIVE;
		complete(&np->np_restart_comp);
	} else {
		np->np_thread_state = ISCSI_NP_THREAD_ACTIVE;
	}
	spin_unlock_bh(&np->np_thread_lock);

	if (kernel_accept(sock, &new_sock, 0) < 0) {
		spin_lock_bh(&np->np_thread_lock);
		if (np->np_thread_state == ISCSI_NP_THREAD_RESET) {
			spin_unlock_bh(&np->np_thread_lock);
			complete(&np->np_restart_comp);
			/* Get another socket */
			return 1;
		}
		spin_unlock_bh(&np->np_thread_lock);
		goto out;
	}
	iscsi_start_login_thread_timer(np);

	conn = kzalloc(sizeof(struct iscsi_conn), GFP_KERNEL);
	if (!conn) {
		pr_err("Could not allocate memory for"
			" new connection\n");
		sock_release(new_sock);
		/* Get another socket */
		return 1;
	}

	pr_debug("Moving to TARG_CONN_STATE_FREE.\n");
	conn->conn_state = TARG_CONN_STATE_FREE;
	conn->sock = new_sock;

	pr_debug("Moving to TARG_CONN_STATE_XPT_UP.\n");
	conn->conn_state = TARG_CONN_STATE_XPT_UP;

	/*
	 * Allocate conn->conn_ops early as a failure calling
	 * iscsit_tx_login_rsp() below will call tx_data().
	 */
	conn->conn_ops = kzalloc(sizeof(struct iscsi_conn_ops), GFP_KERNEL);
	if (!conn->conn_ops) {
		pr_err("Unable to allocate memory for"
			" struct iscsi_conn_ops.\n");
		goto new_sess_out;
	}
	/*
	 * Perform the remaining iSCSI connection initialization items..
	 */
	if (iscsi_login_init_conn(conn) < 0)
		goto new_sess_out;

	memset(buffer, 0, ISCSI_HDR_LEN);
	memset(&iov, 0, sizeof(struct kvec));
	iov.iov_base	= buffer;
	iov.iov_len	= ISCSI_HDR_LEN;

	if (rx_data(conn, &iov, 1, ISCSI_HDR_LEN) <= 0) {
		pr_err("rx_data() returned an error.\n");
		goto new_sess_out;
	}

	iscsi_opcode = (buffer[0] & ISCSI_OPCODE_MASK);
	if (!(iscsi_opcode & ISCSI_OP_LOGIN)) {
		pr_err("First opcode is not login request,"
			" failing login request.\n");
		goto new_sess_out;
	}

	pdu			= (struct iscsi_login_req *) buffer;

	/*
	 * Used by iscsit_tx_login_rsp() for Login Resonses PDUs
	 * when Status-Class != 0.
	*/
	conn->login_itt		= pdu->itt;

	spin_lock_bh(&np->np_thread_lock);
	if (np->np_thread_state != ISCSI_NP_THREAD_ACTIVE) {
		spin_unlock_bh(&np->np_thread_lock);
		pr_err("iSCSI Network Portal on %s:%hu currently not"
			" active.\n", np->np_ip, np->np_port);
		iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
				ISCSI_LOGIN_STATUS_SVC_UNAVAILABLE);
		goto new_sess_out;
	}
	spin_unlock_bh(&np->np_thread_lock);

	if (np->np_sockaddr.ss_family == AF_INET6) {
		memset(&sock_in6, 0, sizeof(struct sockaddr_in6));

		if (conn->sock->ops->getname(conn->sock,
				(struct sockaddr *)&sock_in6, &err, 1) < 0) {
			pr_err("sock_ops->getname() failed.\n");
			iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
					ISCSI_LOGIN_STATUS_TARGET_ERROR);
			goto new_sess_out;
		}
		snprintf(conn->login_ip, sizeof(conn->login_ip), "%pI6c",
				&sock_in6.sin6_addr.in6_u);
		conn->login_port = ntohs(sock_in6.sin6_port);

		if (conn->sock->ops->getname(conn->sock,
				(struct sockaddr *)&sock_in6, &err, 0) < 0) {
			pr_err("sock_ops->getname() failed.\n");
			iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
					ISCSI_LOGIN_STATUS_TARGET_ERROR);
			goto new_sess_out;
		}
		snprintf(conn->local_ip, sizeof(conn->local_ip), "%pI6c",
				&sock_in6.sin6_addr.in6_u);
		conn->local_port = ntohs(sock_in6.sin6_port);

	} else {
		memset(&sock_in, 0, sizeof(struct sockaddr_in));

		if (conn->sock->ops->getname(conn->sock,
				(struct sockaddr *)&sock_in, &err, 1) < 0) {
			pr_err("sock_ops->getname() failed.\n");
			iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
					ISCSI_LOGIN_STATUS_TARGET_ERROR);
			goto new_sess_out;
		}
		sprintf(conn->login_ip, "%pI4", &sock_in.sin_addr.s_addr);
		conn->login_port = ntohs(sock_in.sin_port);

		if (conn->sock->ops->getname(conn->sock,
				(struct sockaddr *)&sock_in, &err, 0) < 0) {
			pr_err("sock_ops->getname() failed.\n");
			iscsit_tx_login_rsp(conn, ISCSI_STATUS_CLS_TARGET_ERR,
					ISCSI_LOGIN_STATUS_TARGET_ERROR);
			goto new_sess_out;
		}
		sprintf(conn->local_ip, "%pI4", &sock_in.sin_addr.s_addr);
		conn->local_port = ntohs(sock_in.sin_port);
	}

	conn->network_transport = np->np_network_transport;

	pr_debug("Received iSCSI login request from %s on %s Network"
			" Portal %s:%hu\n", conn->login_ip,
		(conn->network_transport == ISCSI_TCP) ? "TCP" : "SCTP",
			conn->local_ip, conn->local_port);

	pr_debug("Moving to TARG_CONN_STATE_IN_LOGIN.\n");
	conn->conn_state	= TARG_CONN_STATE_IN_LOGIN;

	if (iscsi_login_check_initiator_version(conn, pdu->max_version,
			pdu->min_version) < 0)
		goto new_sess_out;

	zero_tsih = (pdu->tsih == 0x0000);
	if (zero_tsih) {
		/*
		 * This is the leading connection of a new session.
		 * We wait until after authentication to check for
		 * session reinstatement.
		 */
		if (iscsi_login_zero_tsih_s1(conn, buffer) < 0)
			goto new_sess_out;
	} else {
		/*
		 * Add a new connection to an existing session.
		 * We check for a non-existant session in
		 * iscsi_login_non_zero_tsih_s2() below based
		 * on ISID/TSIH, but wait until after authentication
		 * to check for connection reinstatement, etc.
		 */
		if (iscsi_login_non_zero_tsih_s1(conn, buffer) < 0)
			goto new_sess_out;
	}

	/*
	 * This will process the first login request, and call
	 * iscsi_target_locate_portal(), and return a valid struct iscsi_login.
	 */
	login = iscsi_target_init_negotiation(np, conn, buffer);
	if (!login) {
		tpg = conn->tpg;
		goto new_sess_out;
	}

	tpg = conn->tpg;
	if (!tpg) {
		pr_err("Unable to locate struct iscsi_conn->tpg\n");
		goto new_sess_out;
	}

	if (zero_tsih) {
		if (iscsi_login_zero_tsih_s2(conn) < 0) {
			iscsi_target_nego_release(login, conn);
			goto new_sess_out;
		}
	} else {
		if (iscsi_login_non_zero_tsih_s2(conn, buffer) < 0) {
			iscsi_target_nego_release(login, conn);
			goto old_sess_out;
		}
	}

	if (iscsi_target_start_negotiation(login, conn) < 0)
		goto new_sess_out;

	if (!conn->sess) {
		pr_err("struct iscsi_conn session pointer is NULL!\n");
		goto new_sess_out;
	}

	iscsi_stop_login_thread_timer(np);

	if (signal_pending(current))
		goto new_sess_out;

	ret = iscsi_post_login_handler(np, conn, zero_tsih);

	if (ret < 0)
		goto new_sess_out;

	iscsit_deaccess_np(np, tpg);
	tpg = NULL;
	/* Get another socket */
	return 1;

new_sess_out:
	pr_err("iSCSI Login negotiation failed.\n");
	iscsit_collect_login_stats(conn, ISCSI_STATUS_CLS_INITIATOR_ERR,
				  ISCSI_LOGIN_STATUS_INIT_ERR);
	if (!zero_tsih || !conn->sess)
		goto old_sess_out;
	if (conn->sess->se_sess)
		transport_free_session(conn->sess->se_sess);
	if (conn->sess->session_index != 0) {
		spin_lock_bh(&sess_idr_lock);
		idr_remove(&sess_idr, conn->sess->session_index);
		spin_unlock_bh(&sess_idr_lock);
	}
	if (conn->sess->sess_ops)
		kfree(conn->sess->sess_ops);
	if (conn->sess)
		kfree(conn->sess);
old_sess_out:
	iscsi_stop_login_thread_timer(np);
	/*
	 * If login negotiation fails check if the Time2Retain timer
	 * needs to be restarted.
	 */
	if (!zero_tsih && conn->sess) {
		spin_lock_bh(&conn->sess->conn_lock);
		if (conn->sess->session_state == TARG_SESS_STATE_FAILED) {
			struct se_portal_group *se_tpg =
					&ISCSI_TPG_C(conn)->tpg_se_tpg;

			atomic_set(&conn->sess->session_continuation, 0);
			spin_unlock_bh(&conn->sess->conn_lock);
			spin_lock_bh(&se_tpg->session_lock);
			iscsit_start_time2retain_handler(conn->sess);
			spin_unlock_bh(&se_tpg->session_lock);
		} else
			spin_unlock_bh(&conn->sess->conn_lock);
		iscsit_dec_session_usage_count(conn->sess);
	}

	if (!IS_ERR(conn->conn_rx_hash.tfm))
		crypto_free_hash(conn->conn_rx_hash.tfm);
	if (!IS_ERR(conn->conn_tx_hash.tfm))
		crypto_free_hash(conn->conn_tx_hash.tfm);

	if (conn->conn_cpumask)
		free_cpumask_var(conn->conn_cpumask);

	kfree(conn->conn_ops);

	if (conn->param_list) {
		iscsi_release_param_list(conn->param_list);
		conn->param_list = NULL;
	}
	if (conn->sock)
		sock_release(conn->sock);
	kfree(conn);

	if (tpg) {
		iscsit_deaccess_np(np, tpg);
		tpg = NULL;
	}

out:
	stop = kthread_should_stop();
	if (!stop && signal_pending(current)) {
		spin_lock_bh(&np->np_thread_lock);
		stop = (np->np_thread_state == ISCSI_NP_THREAD_SHUTDOWN);
		spin_unlock_bh(&np->np_thread_lock);
	}
	/* Wait for another socket.. */
	if (!stop)
		return 1;

	iscsi_stop_login_thread_timer(np);
	spin_lock_bh(&np->np_thread_lock);
	np->np_thread_state = ISCSI_NP_THREAD_EXIT;
	spin_unlock_bh(&np->np_thread_lock);
	return 0;
}

int iscsi_target_login_thread(void *arg)
{
	struct iscsi_np *np = arg;
	int ret;

	allow_signal(SIGINT);

	while (!kthread_should_stop()) {
		ret = __iscsi_target_login_thread(np);
		/*
		 * We break and exit here unless another sock_accept() call
		 * is expected.
		 */
		if (ret != 1)
			break;
	}

	return 0;
}
