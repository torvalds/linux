/*******************************************************************************
 * This file contains the iSCSI Target specific utility functions.
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

#include <linux/list.h>
#include <linux/percpu_ida.h>
#include <net/ipv6.h>         /* ipv6_addr_equal() */
#include <scsi/scsi_tcq.h>
#include <scsi/iscsi_proto.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/iscsi/iscsi_transport.h>

#include <target/iscsi/iscsi_target_core.h>
#include "iscsi_target_parameters.h"
#include "iscsi_target_seq_pdu_list.h"
#include "iscsi_target_datain_values.h"
#include "iscsi_target_erl0.h"
#include "iscsi_target_erl1.h"
#include "iscsi_target_erl2.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target.h"

#define PRINT_BUFF(buff, len)					\
{								\
	int zzz;						\
								\
	pr_debug("%d:\n", __LINE__);				\
	for (zzz = 0; zzz < len; zzz++) {			\
		if (zzz % 16 == 0) {				\
			if (zzz)				\
				pr_debug("\n");			\
			pr_debug("%4i: ", zzz);			\
		}						\
		pr_debug("%02x ", (unsigned char) (buff)[zzz]);	\
	}							\
	if ((len + 1) % 16)					\
		pr_debug("\n");					\
}

extern struct list_head g_tiqn_list;
extern spinlock_t tiqn_lock;

/*
 *	Called with cmd->r2t_lock held.
 */
int iscsit_add_r2t_to_list(
	struct iscsi_cmd *cmd,
	u32 offset,
	u32 xfer_len,
	int recovery,
	u32 r2t_sn)
{
	struct iscsi_r2t *r2t;

	r2t = kmem_cache_zalloc(lio_r2t_cache, GFP_ATOMIC);
	if (!r2t) {
		pr_err("Unable to allocate memory for struct iscsi_r2t.\n");
		return -1;
	}
	INIT_LIST_HEAD(&r2t->r2t_list);

	r2t->recovery_r2t = recovery;
	r2t->r2t_sn = (!r2t_sn) ? cmd->r2t_sn++ : r2t_sn;
	r2t->offset = offset;
	r2t->xfer_len = xfer_len;
	list_add_tail(&r2t->r2t_list, &cmd->cmd_r2t_list);
	spin_unlock_bh(&cmd->r2t_lock);

	iscsit_add_cmd_to_immediate_queue(cmd, cmd->conn, ISTATE_SEND_R2T);

	spin_lock_bh(&cmd->r2t_lock);
	return 0;
}

struct iscsi_r2t *iscsit_get_r2t_for_eos(
	struct iscsi_cmd *cmd,
	u32 offset,
	u32 length)
{
	struct iscsi_r2t *r2t;

	spin_lock_bh(&cmd->r2t_lock);
	list_for_each_entry(r2t, &cmd->cmd_r2t_list, r2t_list) {
		if ((r2t->offset <= offset) &&
		    (r2t->offset + r2t->xfer_len) >= (offset + length)) {
			spin_unlock_bh(&cmd->r2t_lock);
			return r2t;
		}
	}
	spin_unlock_bh(&cmd->r2t_lock);

	pr_err("Unable to locate R2T for Offset: %u, Length:"
			" %u\n", offset, length);
	return NULL;
}

struct iscsi_r2t *iscsit_get_r2t_from_list(struct iscsi_cmd *cmd)
{
	struct iscsi_r2t *r2t;

	spin_lock_bh(&cmd->r2t_lock);
	list_for_each_entry(r2t, &cmd->cmd_r2t_list, r2t_list) {
		if (!r2t->sent_r2t) {
			spin_unlock_bh(&cmd->r2t_lock);
			return r2t;
		}
	}
	spin_unlock_bh(&cmd->r2t_lock);

	pr_err("Unable to locate next R2T to send for ITT:"
			" 0x%08x.\n", cmd->init_task_tag);
	return NULL;
}

/*
 *	Called with cmd->r2t_lock held.
 */
void iscsit_free_r2t(struct iscsi_r2t *r2t, struct iscsi_cmd *cmd)
{
	list_del(&r2t->r2t_list);
	kmem_cache_free(lio_r2t_cache, r2t);
}

void iscsit_free_r2ts_from_list(struct iscsi_cmd *cmd)
{
	struct iscsi_r2t *r2t, *r2t_tmp;

	spin_lock_bh(&cmd->r2t_lock);
	list_for_each_entry_safe(r2t, r2t_tmp, &cmd->cmd_r2t_list, r2t_list)
		iscsit_free_r2t(r2t, cmd);
	spin_unlock_bh(&cmd->r2t_lock);
}

/*
 * May be called from software interrupt (timer) context for allocating
 * iSCSI NopINs.
 */
struct iscsi_cmd *iscsit_allocate_cmd(struct iscsi_conn *conn, int state)
{
	struct iscsi_cmd *cmd;
	struct se_session *se_sess = conn->sess->se_sess;
	int size, tag;

	tag = percpu_ida_alloc(&se_sess->sess_tag_pool, state);
	if (tag < 0)
		return NULL;

	size = sizeof(struct iscsi_cmd) + conn->conn_transport->priv_size;
	cmd = (struct iscsi_cmd *)(se_sess->sess_cmd_map + (tag * size));
	memset(cmd, 0, size);

	cmd->se_cmd.map_tag = tag;
	cmd->conn = conn;
	cmd->data_direction = DMA_NONE;
	INIT_LIST_HEAD(&cmd->i_conn_node);
	INIT_LIST_HEAD(&cmd->datain_list);
	INIT_LIST_HEAD(&cmd->cmd_r2t_list);
	spin_lock_init(&cmd->datain_lock);
	spin_lock_init(&cmd->dataout_timeout_lock);
	spin_lock_init(&cmd->istate_lock);
	spin_lock_init(&cmd->error_lock);
	spin_lock_init(&cmd->r2t_lock);
	timer_setup(&cmd->dataout_timer, iscsit_handle_dataout_timeout, 0);

	return cmd;
}
EXPORT_SYMBOL(iscsit_allocate_cmd);

struct iscsi_seq *iscsit_get_seq_holder_for_datain(
	struct iscsi_cmd *cmd,
	u32 seq_send_order)
{
	u32 i;

	for (i = 0; i < cmd->seq_count; i++)
		if (cmd->seq_list[i].seq_send_order == seq_send_order)
			return &cmd->seq_list[i];

	return NULL;
}

struct iscsi_seq *iscsit_get_seq_holder_for_r2t(struct iscsi_cmd *cmd)
{
	u32 i;

	if (!cmd->seq_list) {
		pr_err("struct iscsi_cmd->seq_list is NULL!\n");
		return NULL;
	}

	for (i = 0; i < cmd->seq_count; i++) {
		if (cmd->seq_list[i].type != SEQTYPE_NORMAL)
			continue;
		if (cmd->seq_list[i].seq_send_order == cmd->seq_send_order) {
			cmd->seq_send_order++;
			return &cmd->seq_list[i];
		}
	}

	return NULL;
}

struct iscsi_r2t *iscsit_get_holder_for_r2tsn(
	struct iscsi_cmd *cmd,
	u32 r2t_sn)
{
	struct iscsi_r2t *r2t;

	spin_lock_bh(&cmd->r2t_lock);
	list_for_each_entry(r2t, &cmd->cmd_r2t_list, r2t_list) {
		if (r2t->r2t_sn == r2t_sn) {
			spin_unlock_bh(&cmd->r2t_lock);
			return r2t;
		}
	}
	spin_unlock_bh(&cmd->r2t_lock);

	return NULL;
}

static inline int iscsit_check_received_cmdsn(struct iscsi_session *sess, u32 cmdsn)
{
	u32 max_cmdsn;
	int ret;

	/*
	 * This is the proper method of checking received CmdSN against
	 * ExpCmdSN and MaxCmdSN values, as well as accounting for out
	 * or order CmdSNs due to multiple connection sessions and/or
	 * CRC failures.
	 */
	max_cmdsn = atomic_read(&sess->max_cmd_sn);
	if (iscsi_sna_gt(cmdsn, max_cmdsn)) {
		pr_err("Received CmdSN: 0x%08x is greater than"
		       " MaxCmdSN: 0x%08x, ignoring.\n", cmdsn, max_cmdsn);
		ret = CMDSN_MAXCMDSN_OVERRUN;

	} else if (cmdsn == sess->exp_cmd_sn) {
		sess->exp_cmd_sn++;
		pr_debug("Received CmdSN matches ExpCmdSN,"
		      " incremented ExpCmdSN to: 0x%08x\n",
		      sess->exp_cmd_sn);
		ret = CMDSN_NORMAL_OPERATION;

	} else if (iscsi_sna_gt(cmdsn, sess->exp_cmd_sn)) {
		pr_debug("Received CmdSN: 0x%08x is greater"
		      " than ExpCmdSN: 0x%08x, not acknowledging.\n",
		      cmdsn, sess->exp_cmd_sn);
		ret = CMDSN_HIGHER_THAN_EXP;

	} else {
		pr_err("Received CmdSN: 0x%08x is less than"
		       " ExpCmdSN: 0x%08x, ignoring.\n", cmdsn,
		       sess->exp_cmd_sn);
		ret = CMDSN_LOWER_THAN_EXP;
	}

	return ret;
}

/*
 * Commands may be received out of order if MC/S is in use.
 * Ensure they are executed in CmdSN order.
 */
int iscsit_sequence_cmd(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			unsigned char *buf, __be32 cmdsn)
{
	int ret, cmdsn_ret;
	bool reject = false;
	u8 reason = ISCSI_REASON_BOOKMARK_NO_RESOURCES;

	mutex_lock(&conn->sess->cmdsn_mutex);

	cmdsn_ret = iscsit_check_received_cmdsn(conn->sess, be32_to_cpu(cmdsn));
	switch (cmdsn_ret) {
	case CMDSN_NORMAL_OPERATION:
		ret = iscsit_execute_cmd(cmd, 0);
		if ((ret >= 0) && !list_empty(&conn->sess->sess_ooo_cmdsn_list))
			iscsit_execute_ooo_cmdsns(conn->sess);
		else if (ret < 0) {
			reject = true;
			ret = CMDSN_ERROR_CANNOT_RECOVER;
		}
		break;
	case CMDSN_HIGHER_THAN_EXP:
		ret = iscsit_handle_ooo_cmdsn(conn->sess, cmd, be32_to_cpu(cmdsn));
		if (ret < 0) {
			reject = true;
			ret = CMDSN_ERROR_CANNOT_RECOVER;
			break;
		}
		ret = CMDSN_HIGHER_THAN_EXP;
		break;
	case CMDSN_LOWER_THAN_EXP:
	case CMDSN_MAXCMDSN_OVERRUN:
	default:
		cmd->i_state = ISTATE_REMOVE;
		iscsit_add_cmd_to_immediate_queue(cmd, conn, cmd->i_state);
		/*
		 * Existing callers for iscsit_sequence_cmd() will silently
		 * ignore commands with CMDSN_LOWER_THAN_EXP, so force this
		 * return for CMDSN_MAXCMDSN_OVERRUN as well..
		 */
		ret = CMDSN_LOWER_THAN_EXP;
		break;
	}
	mutex_unlock(&conn->sess->cmdsn_mutex);

	if (reject)
		iscsit_reject_cmd(cmd, reason, buf);

	return ret;
}
EXPORT_SYMBOL(iscsit_sequence_cmd);

int iscsit_check_unsolicited_dataout(struct iscsi_cmd *cmd, unsigned char *buf)
{
	struct iscsi_conn *conn = cmd->conn;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct iscsi_data *hdr = (struct iscsi_data *) buf;
	u32 payload_length = ntoh24(hdr->dlength);

	if (conn->sess->sess_ops->InitialR2T) {
		pr_err("Received unexpected unsolicited data"
			" while InitialR2T=Yes, protocol error.\n");
		transport_send_check_condition_and_sense(se_cmd,
				TCM_UNEXPECTED_UNSOLICITED_DATA, 0);
		return -1;
	}

	if ((cmd->first_burst_len + payload_length) >
	     conn->sess->sess_ops->FirstBurstLength) {
		pr_err("Total %u bytes exceeds FirstBurstLength: %u"
			" for this Unsolicited DataOut Burst.\n",
			(cmd->first_burst_len + payload_length),
				conn->sess->sess_ops->FirstBurstLength);
		transport_send_check_condition_and_sense(se_cmd,
				TCM_INCORRECT_AMOUNT_OF_DATA, 0);
		return -1;
	}

	if (!(hdr->flags & ISCSI_FLAG_CMD_FINAL))
		return 0;

	if (((cmd->first_burst_len + payload_length) != cmd->se_cmd.data_length) &&
	    ((cmd->first_burst_len + payload_length) !=
	      conn->sess->sess_ops->FirstBurstLength)) {
		pr_err("Unsolicited non-immediate data received %u"
			" does not equal FirstBurstLength: %u, and does"
			" not equal ExpXferLen %u.\n",
			(cmd->first_burst_len + payload_length),
			conn->sess->sess_ops->FirstBurstLength, cmd->se_cmd.data_length);
		transport_send_check_condition_and_sense(se_cmd,
				TCM_INCORRECT_AMOUNT_OF_DATA, 0);
		return -1;
	}
	return 0;
}

struct iscsi_cmd *iscsit_find_cmd_from_itt(
	struct iscsi_conn *conn,
	itt_t init_task_tag)
{
	struct iscsi_cmd *cmd;

	spin_lock_bh(&conn->cmd_lock);
	list_for_each_entry(cmd, &conn->conn_cmd_list, i_conn_node) {
		if (cmd->init_task_tag == init_task_tag) {
			spin_unlock_bh(&conn->cmd_lock);
			return cmd;
		}
	}
	spin_unlock_bh(&conn->cmd_lock);

	pr_err("Unable to locate ITT: 0x%08x on CID: %hu",
			init_task_tag, conn->cid);
	return NULL;
}
EXPORT_SYMBOL(iscsit_find_cmd_from_itt);

struct iscsi_cmd *iscsit_find_cmd_from_itt_or_dump(
	struct iscsi_conn *conn,
	itt_t init_task_tag,
	u32 length)
{
	struct iscsi_cmd *cmd;

	spin_lock_bh(&conn->cmd_lock);
	list_for_each_entry(cmd, &conn->conn_cmd_list, i_conn_node) {
		if (cmd->cmd_flags & ICF_GOT_LAST_DATAOUT)
			continue;
		if (cmd->init_task_tag == init_task_tag) {
			spin_unlock_bh(&conn->cmd_lock);
			return cmd;
		}
	}
	spin_unlock_bh(&conn->cmd_lock);

	pr_err("Unable to locate ITT: 0x%08x on CID: %hu,"
			" dumping payload\n", init_task_tag, conn->cid);
	if (length)
		iscsit_dump_data_payload(conn, length, 1);

	return NULL;
}
EXPORT_SYMBOL(iscsit_find_cmd_from_itt_or_dump);

struct iscsi_cmd *iscsit_find_cmd_from_ttt(
	struct iscsi_conn *conn,
	u32 targ_xfer_tag)
{
	struct iscsi_cmd *cmd = NULL;

	spin_lock_bh(&conn->cmd_lock);
	list_for_each_entry(cmd, &conn->conn_cmd_list, i_conn_node) {
		if (cmd->targ_xfer_tag == targ_xfer_tag) {
			spin_unlock_bh(&conn->cmd_lock);
			return cmd;
		}
	}
	spin_unlock_bh(&conn->cmd_lock);

	pr_err("Unable to locate TTT: 0x%08x on CID: %hu\n",
			targ_xfer_tag, conn->cid);
	return NULL;
}

int iscsit_find_cmd_for_recovery(
	struct iscsi_session *sess,
	struct iscsi_cmd **cmd_ptr,
	struct iscsi_conn_recovery **cr_ptr,
	itt_t init_task_tag)
{
	struct iscsi_cmd *cmd = NULL;
	struct iscsi_conn_recovery *cr;
	/*
	 * Scan through the inactive connection recovery list's command list.
	 * If init_task_tag matches the command is still alligent.
	 */
	spin_lock(&sess->cr_i_lock);
	list_for_each_entry(cr, &sess->cr_inactive_list, cr_list) {
		spin_lock(&cr->conn_recovery_cmd_lock);
		list_for_each_entry(cmd, &cr->conn_recovery_cmd_list, i_conn_node) {
			if (cmd->init_task_tag == init_task_tag) {
				spin_unlock(&cr->conn_recovery_cmd_lock);
				spin_unlock(&sess->cr_i_lock);

				*cr_ptr = cr;
				*cmd_ptr = cmd;
				return -2;
			}
		}
		spin_unlock(&cr->conn_recovery_cmd_lock);
	}
	spin_unlock(&sess->cr_i_lock);
	/*
	 * Scan through the active connection recovery list's command list.
	 * If init_task_tag matches the command is ready to be reassigned.
	 */
	spin_lock(&sess->cr_a_lock);
	list_for_each_entry(cr, &sess->cr_active_list, cr_list) {
		spin_lock(&cr->conn_recovery_cmd_lock);
		list_for_each_entry(cmd, &cr->conn_recovery_cmd_list, i_conn_node) {
			if (cmd->init_task_tag == init_task_tag) {
				spin_unlock(&cr->conn_recovery_cmd_lock);
				spin_unlock(&sess->cr_a_lock);

				*cr_ptr = cr;
				*cmd_ptr = cmd;
				return 0;
			}
		}
		spin_unlock(&cr->conn_recovery_cmd_lock);
	}
	spin_unlock(&sess->cr_a_lock);

	return -1;
}

void iscsit_add_cmd_to_immediate_queue(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn,
	u8 state)
{
	struct iscsi_queue_req *qr;

	qr = kmem_cache_zalloc(lio_qr_cache, GFP_ATOMIC);
	if (!qr) {
		pr_err("Unable to allocate memory for"
				" struct iscsi_queue_req\n");
		return;
	}
	INIT_LIST_HEAD(&qr->qr_list);
	qr->cmd = cmd;
	qr->state = state;

	spin_lock_bh(&conn->immed_queue_lock);
	list_add_tail(&qr->qr_list, &conn->immed_queue_list);
	atomic_inc(&cmd->immed_queue_count);
	atomic_set(&conn->check_immediate_queue, 1);
	spin_unlock_bh(&conn->immed_queue_lock);

	wake_up(&conn->queues_wq);
}
EXPORT_SYMBOL(iscsit_add_cmd_to_immediate_queue);

struct iscsi_queue_req *iscsit_get_cmd_from_immediate_queue(struct iscsi_conn *conn)
{
	struct iscsi_queue_req *qr;

	spin_lock_bh(&conn->immed_queue_lock);
	if (list_empty(&conn->immed_queue_list)) {
		spin_unlock_bh(&conn->immed_queue_lock);
		return NULL;
	}
	qr = list_first_entry(&conn->immed_queue_list,
			      struct iscsi_queue_req, qr_list);

	list_del(&qr->qr_list);
	if (qr->cmd)
		atomic_dec(&qr->cmd->immed_queue_count);
	spin_unlock_bh(&conn->immed_queue_lock);

	return qr;
}

static void iscsit_remove_cmd_from_immediate_queue(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	struct iscsi_queue_req *qr, *qr_tmp;

	spin_lock_bh(&conn->immed_queue_lock);
	if (!atomic_read(&cmd->immed_queue_count)) {
		spin_unlock_bh(&conn->immed_queue_lock);
		return;
	}

	list_for_each_entry_safe(qr, qr_tmp, &conn->immed_queue_list, qr_list) {
		if (qr->cmd != cmd)
			continue;

		atomic_dec(&qr->cmd->immed_queue_count);
		list_del(&qr->qr_list);
		kmem_cache_free(lio_qr_cache, qr);
	}
	spin_unlock_bh(&conn->immed_queue_lock);

	if (atomic_read(&cmd->immed_queue_count)) {
		pr_err("ITT: 0x%08x immed_queue_count: %d\n",
			cmd->init_task_tag,
			atomic_read(&cmd->immed_queue_count));
	}
}

int iscsit_add_cmd_to_response_queue(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn,
	u8 state)
{
	struct iscsi_queue_req *qr;

	qr = kmem_cache_zalloc(lio_qr_cache, GFP_ATOMIC);
	if (!qr) {
		pr_err("Unable to allocate memory for"
			" struct iscsi_queue_req\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&qr->qr_list);
	qr->cmd = cmd;
	qr->state = state;

	spin_lock_bh(&conn->response_queue_lock);
	list_add_tail(&qr->qr_list, &conn->response_queue_list);
	atomic_inc(&cmd->response_queue_count);
	spin_unlock_bh(&conn->response_queue_lock);

	wake_up(&conn->queues_wq);
	return 0;
}

struct iscsi_queue_req *iscsit_get_cmd_from_response_queue(struct iscsi_conn *conn)
{
	struct iscsi_queue_req *qr;

	spin_lock_bh(&conn->response_queue_lock);
	if (list_empty(&conn->response_queue_list)) {
		spin_unlock_bh(&conn->response_queue_lock);
		return NULL;
	}

	qr = list_first_entry(&conn->response_queue_list,
			      struct iscsi_queue_req, qr_list);

	list_del(&qr->qr_list);
	if (qr->cmd)
		atomic_dec(&qr->cmd->response_queue_count);
	spin_unlock_bh(&conn->response_queue_lock);

	return qr;
}

static void iscsit_remove_cmd_from_response_queue(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	struct iscsi_queue_req *qr, *qr_tmp;

	spin_lock_bh(&conn->response_queue_lock);
	if (!atomic_read(&cmd->response_queue_count)) {
		spin_unlock_bh(&conn->response_queue_lock);
		return;
	}

	list_for_each_entry_safe(qr, qr_tmp, &conn->response_queue_list,
				qr_list) {
		if (qr->cmd != cmd)
			continue;

		atomic_dec(&qr->cmd->response_queue_count);
		list_del(&qr->qr_list);
		kmem_cache_free(lio_qr_cache, qr);
	}
	spin_unlock_bh(&conn->response_queue_lock);

	if (atomic_read(&cmd->response_queue_count)) {
		pr_err("ITT: 0x%08x response_queue_count: %d\n",
			cmd->init_task_tag,
			atomic_read(&cmd->response_queue_count));
	}
}

bool iscsit_conn_all_queues_empty(struct iscsi_conn *conn)
{
	bool empty;

	spin_lock_bh(&conn->immed_queue_lock);
	empty = list_empty(&conn->immed_queue_list);
	spin_unlock_bh(&conn->immed_queue_lock);

	if (!empty)
		return empty;

	spin_lock_bh(&conn->response_queue_lock);
	empty = list_empty(&conn->response_queue_list);
	spin_unlock_bh(&conn->response_queue_lock);

	return empty;
}

void iscsit_free_queue_reqs_for_conn(struct iscsi_conn *conn)
{
	struct iscsi_queue_req *qr, *qr_tmp;

	spin_lock_bh(&conn->immed_queue_lock);
	list_for_each_entry_safe(qr, qr_tmp, &conn->immed_queue_list, qr_list) {
		list_del(&qr->qr_list);
		if (qr->cmd)
			atomic_dec(&qr->cmd->immed_queue_count);

		kmem_cache_free(lio_qr_cache, qr);
	}
	spin_unlock_bh(&conn->immed_queue_lock);

	spin_lock_bh(&conn->response_queue_lock);
	list_for_each_entry_safe(qr, qr_tmp, &conn->response_queue_list,
			qr_list) {
		list_del(&qr->qr_list);
		if (qr->cmd)
			atomic_dec(&qr->cmd->response_queue_count);

		kmem_cache_free(lio_qr_cache, qr);
	}
	spin_unlock_bh(&conn->response_queue_lock);
}

void iscsit_release_cmd(struct iscsi_cmd *cmd)
{
	struct iscsi_session *sess;
	struct se_cmd *se_cmd = &cmd->se_cmd;

	WARN_ON(!list_empty(&cmd->i_conn_node));

	if (cmd->conn)
		sess = cmd->conn->sess;
	else
		sess = cmd->sess;

	BUG_ON(!sess || !sess->se_sess);

	kfree(cmd->buf_ptr);
	kfree(cmd->pdu_list);
	kfree(cmd->seq_list);
	kfree(cmd->tmr_req);
	kfree(cmd->iov_data);
	kfree(cmd->text_in_ptr);

	percpu_ida_free(&sess->se_sess->sess_tag_pool, se_cmd->map_tag);
}
EXPORT_SYMBOL(iscsit_release_cmd);

void __iscsit_free_cmd(struct iscsi_cmd *cmd, bool check_queues)
{
	struct iscsi_conn *conn = cmd->conn;

	WARN_ON(!list_empty(&cmd->i_conn_node));

	if (cmd->data_direction == DMA_TO_DEVICE) {
		iscsit_stop_dataout_timer(cmd);
		iscsit_free_r2ts_from_list(cmd);
	}
	if (cmd->data_direction == DMA_FROM_DEVICE)
		iscsit_free_all_datain_reqs(cmd);

	if (conn && check_queues) {
		iscsit_remove_cmd_from_immediate_queue(cmd, conn);
		iscsit_remove_cmd_from_response_queue(cmd, conn);
	}

	if (conn && conn->conn_transport->iscsit_release_cmd)
		conn->conn_transport->iscsit_release_cmd(conn, cmd);
}

void iscsit_free_cmd(struct iscsi_cmd *cmd, bool shutdown)
{
	struct se_cmd *se_cmd = cmd->se_cmd.se_tfo ? &cmd->se_cmd : NULL;
	int rc;

	__iscsit_free_cmd(cmd, shutdown);
	if (se_cmd) {
		rc = transport_generic_free_cmd(se_cmd, shutdown);
		if (!rc && shutdown && se_cmd->se_sess) {
			__iscsit_free_cmd(cmd, shutdown);
			target_put_sess_cmd(se_cmd);
		}
	} else {
		iscsit_release_cmd(cmd);
	}
}
EXPORT_SYMBOL(iscsit_free_cmd);

int iscsit_check_session_usage_count(struct iscsi_session *sess)
{
	spin_lock_bh(&sess->session_usage_lock);
	if (sess->session_usage_count != 0) {
		sess->session_waiting_on_uc = 1;
		spin_unlock_bh(&sess->session_usage_lock);
		if (in_interrupt())
			return 2;

		wait_for_completion(&sess->session_waiting_on_uc_comp);
		return 1;
	}
	spin_unlock_bh(&sess->session_usage_lock);

	return 0;
}

void iscsit_dec_session_usage_count(struct iscsi_session *sess)
{
	spin_lock_bh(&sess->session_usage_lock);
	sess->session_usage_count--;

	if (!sess->session_usage_count && sess->session_waiting_on_uc)
		complete(&sess->session_waiting_on_uc_comp);

	spin_unlock_bh(&sess->session_usage_lock);
}

void iscsit_inc_session_usage_count(struct iscsi_session *sess)
{
	spin_lock_bh(&sess->session_usage_lock);
	sess->session_usage_count++;
	spin_unlock_bh(&sess->session_usage_lock);
}

struct iscsi_conn *iscsit_get_conn_from_cid(struct iscsi_session *sess, u16 cid)
{
	struct iscsi_conn *conn;

	spin_lock_bh(&sess->conn_lock);
	list_for_each_entry(conn, &sess->sess_conn_list, conn_list) {
		if ((conn->cid == cid) &&
		    (conn->conn_state == TARG_CONN_STATE_LOGGED_IN)) {
			iscsit_inc_conn_usage_count(conn);
			spin_unlock_bh(&sess->conn_lock);
			return conn;
		}
	}
	spin_unlock_bh(&sess->conn_lock);

	return NULL;
}

struct iscsi_conn *iscsit_get_conn_from_cid_rcfr(struct iscsi_session *sess, u16 cid)
{
	struct iscsi_conn *conn;

	spin_lock_bh(&sess->conn_lock);
	list_for_each_entry(conn, &sess->sess_conn_list, conn_list) {
		if (conn->cid == cid) {
			iscsit_inc_conn_usage_count(conn);
			spin_lock(&conn->state_lock);
			atomic_set(&conn->connection_wait_rcfr, 1);
			spin_unlock(&conn->state_lock);
			spin_unlock_bh(&sess->conn_lock);
			return conn;
		}
	}
	spin_unlock_bh(&sess->conn_lock);

	return NULL;
}

void iscsit_check_conn_usage_count(struct iscsi_conn *conn)
{
	spin_lock_bh(&conn->conn_usage_lock);
	if (conn->conn_usage_count != 0) {
		conn->conn_waiting_on_uc = 1;
		spin_unlock_bh(&conn->conn_usage_lock);

		wait_for_completion(&conn->conn_waiting_on_uc_comp);
		return;
	}
	spin_unlock_bh(&conn->conn_usage_lock);
}

void iscsit_dec_conn_usage_count(struct iscsi_conn *conn)
{
	spin_lock_bh(&conn->conn_usage_lock);
	conn->conn_usage_count--;

	if (!conn->conn_usage_count && conn->conn_waiting_on_uc)
		complete(&conn->conn_waiting_on_uc_comp);

	spin_unlock_bh(&conn->conn_usage_lock);
}

void iscsit_inc_conn_usage_count(struct iscsi_conn *conn)
{
	spin_lock_bh(&conn->conn_usage_lock);
	conn->conn_usage_count++;
	spin_unlock_bh(&conn->conn_usage_lock);
}

static int iscsit_add_nopin(struct iscsi_conn *conn, int want_response)
{
	u8 state;
	struct iscsi_cmd *cmd;

	cmd = iscsit_allocate_cmd(conn, TASK_RUNNING);
	if (!cmd)
		return -1;

	cmd->iscsi_opcode = ISCSI_OP_NOOP_IN;
	state = (want_response) ? ISTATE_SEND_NOPIN_WANT_RESPONSE :
				ISTATE_SEND_NOPIN_NO_RESPONSE;
	cmd->init_task_tag = RESERVED_ITT;
	cmd->targ_xfer_tag = (want_response) ?
			     session_get_next_ttt(conn->sess) : 0xFFFFFFFF;
	spin_lock_bh(&conn->cmd_lock);
	list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
	spin_unlock_bh(&conn->cmd_lock);

	if (want_response)
		iscsit_start_nopin_response_timer(conn);
	iscsit_add_cmd_to_immediate_queue(cmd, conn, state);

	return 0;
}

void iscsit_handle_nopin_response_timeout(struct timer_list *t)
{
	struct iscsi_conn *conn = from_timer(conn, t, nopin_response_timer);

	iscsit_inc_conn_usage_count(conn);

	spin_lock_bh(&conn->nopin_timer_lock);
	if (conn->nopin_response_timer_flags & ISCSI_TF_STOP) {
		spin_unlock_bh(&conn->nopin_timer_lock);
		iscsit_dec_conn_usage_count(conn);
		return;
	}

	pr_debug("Did not receive response to NOPIN on CID: %hu on"
		" SID: %u, failing connection.\n", conn->cid,
			conn->sess->sid);
	conn->nopin_response_timer_flags &= ~ISCSI_TF_RUNNING;
	spin_unlock_bh(&conn->nopin_timer_lock);

	{
	struct iscsi_portal_group *tpg = conn->sess->tpg;
	struct iscsi_tiqn *tiqn = tpg->tpg_tiqn;

	if (tiqn) {
		spin_lock_bh(&tiqn->sess_err_stats.lock);
		strcpy(tiqn->sess_err_stats.last_sess_fail_rem_name,
				conn->sess->sess_ops->InitiatorName);
		tiqn->sess_err_stats.last_sess_failure_type =
				ISCSI_SESS_ERR_CXN_TIMEOUT;
		tiqn->sess_err_stats.cxn_timeout_errors++;
		atomic_long_inc(&conn->sess->conn_timeout_errors);
		spin_unlock_bh(&tiqn->sess_err_stats.lock);
	}
	}

	iscsit_cause_connection_reinstatement(conn, 0);
	iscsit_dec_conn_usage_count(conn);
}

void iscsit_mod_nopin_response_timer(struct iscsi_conn *conn)
{
	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_attrib *na = iscsit_tpg_get_node_attrib(sess);

	spin_lock_bh(&conn->nopin_timer_lock);
	if (!(conn->nopin_response_timer_flags & ISCSI_TF_RUNNING)) {
		spin_unlock_bh(&conn->nopin_timer_lock);
		return;
	}

	mod_timer(&conn->nopin_response_timer,
		(get_jiffies_64() + na->nopin_response_timeout * HZ));
	spin_unlock_bh(&conn->nopin_timer_lock);
}

/*
 *	Called with conn->nopin_timer_lock held.
 */
void iscsit_start_nopin_response_timer(struct iscsi_conn *conn)
{
	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_attrib *na = iscsit_tpg_get_node_attrib(sess);

	spin_lock_bh(&conn->nopin_timer_lock);
	if (conn->nopin_response_timer_flags & ISCSI_TF_RUNNING) {
		spin_unlock_bh(&conn->nopin_timer_lock);
		return;
	}

	conn->nopin_response_timer_flags &= ~ISCSI_TF_STOP;
	conn->nopin_response_timer_flags |= ISCSI_TF_RUNNING;
	mod_timer(&conn->nopin_response_timer,
		  jiffies + na->nopin_response_timeout * HZ);

	pr_debug("Started NOPIN Response Timer on CID: %d to %u"
		" seconds\n", conn->cid, na->nopin_response_timeout);
	spin_unlock_bh(&conn->nopin_timer_lock);
}

void iscsit_stop_nopin_response_timer(struct iscsi_conn *conn)
{
	spin_lock_bh(&conn->nopin_timer_lock);
	if (!(conn->nopin_response_timer_flags & ISCSI_TF_RUNNING)) {
		spin_unlock_bh(&conn->nopin_timer_lock);
		return;
	}
	conn->nopin_response_timer_flags |= ISCSI_TF_STOP;
	spin_unlock_bh(&conn->nopin_timer_lock);

	del_timer_sync(&conn->nopin_response_timer);

	spin_lock_bh(&conn->nopin_timer_lock);
	conn->nopin_response_timer_flags &= ~ISCSI_TF_RUNNING;
	spin_unlock_bh(&conn->nopin_timer_lock);
}

void iscsit_handle_nopin_timeout(struct timer_list *t)
{
	struct iscsi_conn *conn = from_timer(conn, t, nopin_timer);

	iscsit_inc_conn_usage_count(conn);

	spin_lock_bh(&conn->nopin_timer_lock);
	if (conn->nopin_timer_flags & ISCSI_TF_STOP) {
		spin_unlock_bh(&conn->nopin_timer_lock);
		iscsit_dec_conn_usage_count(conn);
		return;
	}
	conn->nopin_timer_flags &= ~ISCSI_TF_RUNNING;
	spin_unlock_bh(&conn->nopin_timer_lock);

	iscsit_add_nopin(conn, 1);
	iscsit_dec_conn_usage_count(conn);
}

/*
 * Called with conn->nopin_timer_lock held.
 */
void __iscsit_start_nopin_timer(struct iscsi_conn *conn)
{
	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_attrib *na = iscsit_tpg_get_node_attrib(sess);
	/*
	* NOPIN timeout is disabled.
	 */
	if (!na->nopin_timeout)
		return;

	if (conn->nopin_timer_flags & ISCSI_TF_RUNNING)
		return;

	conn->nopin_timer_flags &= ~ISCSI_TF_STOP;
	conn->nopin_timer_flags |= ISCSI_TF_RUNNING;
	mod_timer(&conn->nopin_timer, jiffies + na->nopin_timeout * HZ);

	pr_debug("Started NOPIN Timer on CID: %d at %u second"
		" interval\n", conn->cid, na->nopin_timeout);
}

void iscsit_start_nopin_timer(struct iscsi_conn *conn)
{
	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_attrib *na = iscsit_tpg_get_node_attrib(sess);
	/*
	 * NOPIN timeout is disabled..
	 */
	if (!na->nopin_timeout)
		return;

	spin_lock_bh(&conn->nopin_timer_lock);
	if (conn->nopin_timer_flags & ISCSI_TF_RUNNING) {
		spin_unlock_bh(&conn->nopin_timer_lock);
		return;
	}

	conn->nopin_timer_flags &= ~ISCSI_TF_STOP;
	conn->nopin_timer_flags |= ISCSI_TF_RUNNING;
	mod_timer(&conn->nopin_timer, jiffies + na->nopin_timeout * HZ);

	pr_debug("Started NOPIN Timer on CID: %d at %u second"
			" interval\n", conn->cid, na->nopin_timeout);
	spin_unlock_bh(&conn->nopin_timer_lock);
}

void iscsit_stop_nopin_timer(struct iscsi_conn *conn)
{
	spin_lock_bh(&conn->nopin_timer_lock);
	if (!(conn->nopin_timer_flags & ISCSI_TF_RUNNING)) {
		spin_unlock_bh(&conn->nopin_timer_lock);
		return;
	}
	conn->nopin_timer_flags |= ISCSI_TF_STOP;
	spin_unlock_bh(&conn->nopin_timer_lock);

	del_timer_sync(&conn->nopin_timer);

	spin_lock_bh(&conn->nopin_timer_lock);
	conn->nopin_timer_flags &= ~ISCSI_TF_RUNNING;
	spin_unlock_bh(&conn->nopin_timer_lock);
}

int iscsit_send_tx_data(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn,
	int use_misc)
{
	int tx_sent, tx_size;
	u32 iov_count;
	struct kvec *iov;

send_data:
	tx_size = cmd->tx_size;

	if (!use_misc) {
		iov = &cmd->iov_data[0];
		iov_count = cmd->iov_data_count;
	} else {
		iov = &cmd->iov_misc[0];
		iov_count = cmd->iov_misc_count;
	}

	tx_sent = tx_data(conn, &iov[0], iov_count, tx_size);
	if (tx_size != tx_sent) {
		if (tx_sent == -EAGAIN) {
			pr_err("tx_data() returned -EAGAIN\n");
			goto send_data;
		} else
			return -1;
	}
	cmd->tx_size = 0;

	return 0;
}

int iscsit_fe_sendpage_sg(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	struct scatterlist *sg = cmd->first_data_sg;
	struct kvec iov;
	u32 tx_hdr_size, data_len;
	u32 offset = cmd->first_data_sg_off;
	int tx_sent, iov_off;

send_hdr:
	tx_hdr_size = ISCSI_HDR_LEN;
	if (conn->conn_ops->HeaderDigest)
		tx_hdr_size += ISCSI_CRC_LEN;

	iov.iov_base = cmd->pdu;
	iov.iov_len = tx_hdr_size;

	tx_sent = tx_data(conn, &iov, 1, tx_hdr_size);
	if (tx_hdr_size != tx_sent) {
		if (tx_sent == -EAGAIN) {
			pr_err("tx_data() returned -EAGAIN\n");
			goto send_hdr;
		}
		return -1;
	}

	data_len = cmd->tx_size - tx_hdr_size - cmd->padding;
	/*
	 * Set iov_off used by padding and data digest tx_data() calls below
	 * in order to determine proper offset into cmd->iov_data[]
	 */
	if (conn->conn_ops->DataDigest) {
		data_len -= ISCSI_CRC_LEN;
		if (cmd->padding)
			iov_off = (cmd->iov_data_count - 2);
		else
			iov_off = (cmd->iov_data_count - 1);
	} else {
		iov_off = (cmd->iov_data_count - 1);
	}
	/*
	 * Perform sendpage() for each page in the scatterlist
	 */
	while (data_len) {
		u32 space = (sg->length - offset);
		u32 sub_len = min_t(u32, data_len, space);
send_pg:
		tx_sent = conn->sock->ops->sendpage(conn->sock,
					sg_page(sg), sg->offset + offset, sub_len, 0);
		if (tx_sent != sub_len) {
			if (tx_sent == -EAGAIN) {
				pr_err("tcp_sendpage() returned"
						" -EAGAIN\n");
				goto send_pg;
			}

			pr_err("tcp_sendpage() failure: %d\n",
					tx_sent);
			return -1;
		}

		data_len -= sub_len;
		offset = 0;
		sg = sg_next(sg);
	}

send_padding:
	if (cmd->padding) {
		struct kvec *iov_p = &cmd->iov_data[iov_off++];

		tx_sent = tx_data(conn, iov_p, 1, cmd->padding);
		if (cmd->padding != tx_sent) {
			if (tx_sent == -EAGAIN) {
				pr_err("tx_data() returned -EAGAIN\n");
				goto send_padding;
			}
			return -1;
		}
	}

send_datacrc:
	if (conn->conn_ops->DataDigest) {
		struct kvec *iov_d = &cmd->iov_data[iov_off];

		tx_sent = tx_data(conn, iov_d, 1, ISCSI_CRC_LEN);
		if (ISCSI_CRC_LEN != tx_sent) {
			if (tx_sent == -EAGAIN) {
				pr_err("tx_data() returned -EAGAIN\n");
				goto send_datacrc;
			}
			return -1;
		}
	}

	return 0;
}

/*
 *      This function is used for mainly sending a ISCSI_TARG_LOGIN_RSP PDU
 *      back to the Initiator when an expection condition occurs with the
 *      errors set in status_class and status_detail.
 *
 *      Parameters:     iSCSI Connection, Status Class, Status Detail.
 *      Returns:        0 on success, -1 on error.
 */
int iscsit_tx_login_rsp(struct iscsi_conn *conn, u8 status_class, u8 status_detail)
{
	struct iscsi_login_rsp *hdr;
	struct iscsi_login *login = conn->conn_login;

	login->login_failed = 1;
	iscsit_collect_login_stats(conn, status_class, status_detail);

	memset(&login->rsp[0], 0, ISCSI_HDR_LEN);

	hdr	= (struct iscsi_login_rsp *)&login->rsp[0];
	hdr->opcode		= ISCSI_OP_LOGIN_RSP;
	hdr->status_class	= status_class;
	hdr->status_detail	= status_detail;
	hdr->itt		= conn->login_itt;

	return conn->conn_transport->iscsit_put_login_tx(conn, login, 0);
}

void iscsit_print_session_params(struct iscsi_session *sess)
{
	struct iscsi_conn *conn;

	pr_debug("-----------------------------[Session Params for"
		" SID: %u]-----------------------------\n", sess->sid);
	spin_lock_bh(&sess->conn_lock);
	list_for_each_entry(conn, &sess->sess_conn_list, conn_list)
		iscsi_dump_conn_ops(conn->conn_ops);
	spin_unlock_bh(&sess->conn_lock);

	iscsi_dump_sess_ops(sess->sess_ops);
}

static int iscsit_do_rx_data(
	struct iscsi_conn *conn,
	struct iscsi_data_count *count)
{
	int data = count->data_length, rx_loop = 0, total_rx = 0;
	struct msghdr msg;

	if (!conn || !conn->sock || !conn->conn_ops)
		return -1;

	memset(&msg, 0, sizeof(struct msghdr));
	iov_iter_kvec(&msg.msg_iter, READ | ITER_KVEC,
		      count->iov, count->iov_count, data);

	while (msg_data_left(&msg)) {
		rx_loop = sock_recvmsg(conn->sock, &msg, MSG_WAITALL);
		if (rx_loop <= 0) {
			pr_debug("rx_loop: %d total_rx: %d\n",
				rx_loop, total_rx);
			return rx_loop;
		}
		total_rx += rx_loop;
		pr_debug("rx_loop: %d, total_rx: %d, data: %d\n",
				rx_loop, total_rx, data);
	}

	return total_rx;
}

int rx_data(
	struct iscsi_conn *conn,
	struct kvec *iov,
	int iov_count,
	int data)
{
	struct iscsi_data_count c;

	if (!conn || !conn->sock || !conn->conn_ops)
		return -1;

	memset(&c, 0, sizeof(struct iscsi_data_count));
	c.iov = iov;
	c.iov_count = iov_count;
	c.data_length = data;
	c.type = ISCSI_RX_DATA;

	return iscsit_do_rx_data(conn, &c);
}

int tx_data(
	struct iscsi_conn *conn,
	struct kvec *iov,
	int iov_count,
	int data)
{
	struct msghdr msg;
	int total_tx = 0;

	if (!conn || !conn->sock || !conn->conn_ops)
		return -1;

	if (data <= 0) {
		pr_err("Data length is: %d\n", data);
		return -1;
	}

	memset(&msg, 0, sizeof(struct msghdr));

	iov_iter_kvec(&msg.msg_iter, WRITE | ITER_KVEC,
		      iov, iov_count, data);

	while (msg_data_left(&msg)) {
		int tx_loop = sock_sendmsg(conn->sock, &msg);
		if (tx_loop <= 0) {
			pr_debug("tx_loop: %d total_tx %d\n",
				tx_loop, total_tx);
			return tx_loop;
		}
		total_tx += tx_loop;
		pr_debug("tx_loop: %d, total_tx: %d, data: %d\n",
					tx_loop, total_tx, data);
	}

	return total_tx;
}

void iscsit_collect_login_stats(
	struct iscsi_conn *conn,
	u8 status_class,
	u8 status_detail)
{
	struct iscsi_param *intrname = NULL;
	struct iscsi_tiqn *tiqn;
	struct iscsi_login_stats *ls;

	tiqn = iscsit_snmp_get_tiqn(conn);
	if (!tiqn)
		return;

	ls = &tiqn->login_stats;

	spin_lock(&ls->lock);
	if (status_class == ISCSI_STATUS_CLS_SUCCESS)
		ls->accepts++;
	else if (status_class == ISCSI_STATUS_CLS_REDIRECT) {
		ls->redirects++;
		ls->last_fail_type = ISCSI_LOGIN_FAIL_REDIRECT;
	} else if ((status_class == ISCSI_STATUS_CLS_INITIATOR_ERR)  &&
		 (status_detail == ISCSI_LOGIN_STATUS_AUTH_FAILED)) {
		ls->authenticate_fails++;
		ls->last_fail_type =  ISCSI_LOGIN_FAIL_AUTHENTICATE;
	} else if ((status_class == ISCSI_STATUS_CLS_INITIATOR_ERR)  &&
		 (status_detail == ISCSI_LOGIN_STATUS_TGT_FORBIDDEN)) {
		ls->authorize_fails++;
		ls->last_fail_type = ISCSI_LOGIN_FAIL_AUTHORIZE;
	} else if ((status_class == ISCSI_STATUS_CLS_INITIATOR_ERR) &&
		 (status_detail == ISCSI_LOGIN_STATUS_INIT_ERR)) {
		ls->negotiate_fails++;
		ls->last_fail_type = ISCSI_LOGIN_FAIL_NEGOTIATE;
	} else {
		ls->other_fails++;
		ls->last_fail_type = ISCSI_LOGIN_FAIL_OTHER;
	}

	/* Save initiator name, ip address and time, if it is a failed login */
	if (status_class != ISCSI_STATUS_CLS_SUCCESS) {
		if (conn->param_list)
			intrname = iscsi_find_param_from_key(INITIATORNAME,
							     conn->param_list);
		strlcpy(ls->last_intr_fail_name,
		       (intrname ? intrname->value : "Unknown"),
		       sizeof(ls->last_intr_fail_name));

		ls->last_intr_fail_ip_family = conn->login_family;

		ls->last_intr_fail_sockaddr = conn->login_sockaddr;
		ls->last_fail_time = get_jiffies_64();
	}

	spin_unlock(&ls->lock);
}

struct iscsi_tiqn *iscsit_snmp_get_tiqn(struct iscsi_conn *conn)
{
	struct iscsi_portal_group *tpg;

	if (!conn)
		return NULL;

	tpg = conn->tpg;
	if (!tpg)
		return NULL;

	if (!tpg->tpg_tiqn)
		return NULL;

	return tpg->tpg_tiqn;
}
