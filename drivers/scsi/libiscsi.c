// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iSCSI lib functions
 *
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2004 - 2006 Mike Christie
 * Copyright (C) 2004 - 2005 Dmitry Yusupov
 * Copyright (C) 2004 - 2005 Alex Aizman
 * maintained by open-iscsi@googlegroups.com
 */
#include <linux/types.h>
#include <linux/kfifo.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <net/tcp.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/iscsi_proto.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/libiscsi.h>
#include <trace/events/iscsi.h>

static int iscsi_dbg_lib_conn;
module_param_named(debug_libiscsi_conn, iscsi_dbg_lib_conn, int,
		   S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_libiscsi_conn,
		 "Turn on debugging for connections in libiscsi module. "
		 "Set to 1 to turn on, and zero to turn off. Default is off.");

static int iscsi_dbg_lib_session;
module_param_named(debug_libiscsi_session, iscsi_dbg_lib_session, int,
		   S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_libiscsi_session,
		 "Turn on debugging for sessions in libiscsi module. "
		 "Set to 1 to turn on, and zero to turn off. Default is off.");

static int iscsi_dbg_lib_eh;
module_param_named(debug_libiscsi_eh, iscsi_dbg_lib_eh, int,
		   S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_libiscsi_eh,
		 "Turn on debugging for error handling in libiscsi module. "
		 "Set to 1 to turn on, and zero to turn off. Default is off.");

#define ISCSI_DBG_CONN(_conn, dbg_fmt, arg...)			\
	do {							\
		if (iscsi_dbg_lib_conn)				\
			iscsi_conn_printk(KERN_INFO, _conn,	\
					     "%s " dbg_fmt,	\
					     __func__, ##arg);	\
		iscsi_dbg_trace(trace_iscsi_dbg_conn,		\
				&(_conn)->cls_conn->dev,	\
				"%s " dbg_fmt, __func__, ##arg);\
	} while (0);

#define ISCSI_DBG_SESSION(_session, dbg_fmt, arg...)			\
	do {								\
		if (iscsi_dbg_lib_session)				\
			iscsi_session_printk(KERN_INFO, _session,	\
					     "%s " dbg_fmt,		\
					     __func__, ##arg);		\
		iscsi_dbg_trace(trace_iscsi_dbg_session, 		\
				&(_session)->cls_session->dev,		\
				"%s " dbg_fmt, __func__, ##arg);	\
	} while (0);

#define ISCSI_DBG_EH(_session, dbg_fmt, arg...)				\
	do {								\
		if (iscsi_dbg_lib_eh)					\
			iscsi_session_printk(KERN_INFO, _session,	\
					     "%s " dbg_fmt,		\
					     __func__, ##arg);		\
		iscsi_dbg_trace(trace_iscsi_dbg_eh,			\
				&(_session)->cls_session->dev,		\
				"%s " dbg_fmt, __func__, ##arg);	\
	} while (0);

inline void iscsi_conn_queue_work(struct iscsi_conn *conn)
{
	struct Scsi_Host *shost = conn->session->host;
	struct iscsi_host *ihost = shost_priv(shost);

	if (ihost->workq)
		queue_work(ihost->workq, &conn->xmitwork);
}
EXPORT_SYMBOL_GPL(iscsi_conn_queue_work);

static void __iscsi_update_cmdsn(struct iscsi_session *session,
				 uint32_t exp_cmdsn, uint32_t max_cmdsn)
{
	/*
	 * standard specifies this check for when to update expected and
	 * max sequence numbers
	 */
	if (iscsi_sna_lt(max_cmdsn, exp_cmdsn - 1))
		return;

	if (exp_cmdsn != session->exp_cmdsn &&
	    !iscsi_sna_lt(exp_cmdsn, session->exp_cmdsn))
		session->exp_cmdsn = exp_cmdsn;

	if (max_cmdsn != session->max_cmdsn &&
	    !iscsi_sna_lt(max_cmdsn, session->max_cmdsn))
		session->max_cmdsn = max_cmdsn;
}

void iscsi_update_cmdsn(struct iscsi_session *session, struct iscsi_nopin *hdr)
{
	__iscsi_update_cmdsn(session, be32_to_cpu(hdr->exp_cmdsn),
			     be32_to_cpu(hdr->max_cmdsn));
}
EXPORT_SYMBOL_GPL(iscsi_update_cmdsn);

/**
 * iscsi_prep_data_out_pdu - initialize Data-Out
 * @task: scsi command task
 * @r2t: R2T info
 * @hdr: iscsi data in pdu
 *
 * Notes:
 *	Initialize Data-Out within this R2T sequence and finds
 *	proper data_offset within this SCSI command.
 *
 *	This function is called with connection lock taken.
 **/
void iscsi_prep_data_out_pdu(struct iscsi_task *task, struct iscsi_r2t_info *r2t,
			   struct iscsi_data *hdr)
{
	struct iscsi_conn *conn = task->conn;
	unsigned int left = r2t->data_length - r2t->sent;

	task->hdr_len = sizeof(struct iscsi_data);

	memset(hdr, 0, sizeof(struct iscsi_data));
	hdr->ttt = r2t->ttt;
	hdr->datasn = cpu_to_be32(r2t->datasn);
	r2t->datasn++;
	hdr->opcode = ISCSI_OP_SCSI_DATA_OUT;
	hdr->lun = task->lun;
	hdr->itt = task->hdr_itt;
	hdr->exp_statsn = r2t->exp_statsn;
	hdr->offset = cpu_to_be32(r2t->data_offset + r2t->sent);
	if (left > conn->max_xmit_dlength) {
		hton24(hdr->dlength, conn->max_xmit_dlength);
		r2t->data_count = conn->max_xmit_dlength;
		hdr->flags = 0;
	} else {
		hton24(hdr->dlength, left);
		r2t->data_count = left;
		hdr->flags = ISCSI_FLAG_CMD_FINAL;
	}
	conn->dataout_pdus_cnt++;
}
EXPORT_SYMBOL_GPL(iscsi_prep_data_out_pdu);

static int iscsi_add_hdr(struct iscsi_task *task, unsigned len)
{
	unsigned exp_len = task->hdr_len + len;

	if (exp_len > task->hdr_max) {
		WARN_ON(1);
		return -EINVAL;
	}

	WARN_ON(len & (ISCSI_PAD_LEN - 1)); /* caller must pad the AHS */
	task->hdr_len = exp_len;
	return 0;
}

/*
 * make an extended cdb AHS
 */
static int iscsi_prep_ecdb_ahs(struct iscsi_task *task)
{
	struct scsi_cmnd *cmd = task->sc;
	unsigned rlen, pad_len;
	unsigned short ahslength;
	struct iscsi_ecdb_ahdr *ecdb_ahdr;
	int rc;

	ecdb_ahdr = iscsi_next_hdr(task);
	rlen = cmd->cmd_len - ISCSI_CDB_SIZE;

	BUG_ON(rlen > sizeof(ecdb_ahdr->ecdb));
	ahslength = rlen + sizeof(ecdb_ahdr->reserved);

	pad_len = iscsi_padding(rlen);

	rc = iscsi_add_hdr(task, sizeof(ecdb_ahdr->ahslength) +
	                   sizeof(ecdb_ahdr->ahstype) + ahslength + pad_len);
	if (rc)
		return rc;

	if (pad_len)
		memset(&ecdb_ahdr->ecdb[rlen], 0, pad_len);

	ecdb_ahdr->ahslength = cpu_to_be16(ahslength);
	ecdb_ahdr->ahstype = ISCSI_AHSTYPE_CDB;
	ecdb_ahdr->reserved = 0;
	memcpy(ecdb_ahdr->ecdb, cmd->cmnd + ISCSI_CDB_SIZE, rlen);

	ISCSI_DBG_SESSION(task->conn->session,
			  "iscsi_prep_ecdb_ahs: varlen_cdb_len %d "
		          "rlen %d pad_len %d ahs_length %d iscsi_headers_size "
		          "%u\n", cmd->cmd_len, rlen, pad_len, ahslength,
		          task->hdr_len);
	return 0;
}

/**
 * iscsi_check_tmf_restrictions - check if a task is affected by TMF
 * @task: iscsi task
 * @opcode: opcode to check for
 *
 * During TMF a task has to be checked if it's affected.
 * All unrelated I/O can be passed through, but I/O to the
 * affected LUN should be restricted.
 * If 'fast_abort' is set we won't be sending any I/O to the
 * affected LUN.
 * Otherwise the target is waiting for all TTTs to be completed,
 * so we have to send all outstanding Data-Out PDUs to the target.
 */
static int iscsi_check_tmf_restrictions(struct iscsi_task *task, int opcode)
{
	struct iscsi_session *session = task->conn->session;
	struct iscsi_tm *tmf = &session->tmhdr;
	u64 hdr_lun;

	if (session->tmf_state == TMF_INITIAL)
		return 0;

	if ((tmf->opcode & ISCSI_OPCODE_MASK) != ISCSI_OP_SCSI_TMFUNC)
		return 0;

	switch (ISCSI_TM_FUNC_VALUE(tmf)) {
	case ISCSI_TM_FUNC_LOGICAL_UNIT_RESET:
		/*
		 * Allow PDUs for unrelated LUNs
		 */
		hdr_lun = scsilun_to_int(&tmf->lun);
		if (hdr_lun != task->sc->device->lun)
			return 0;
		fallthrough;
	case ISCSI_TM_FUNC_TARGET_WARM_RESET:
		/*
		 * Fail all SCSI cmd PDUs
		 */
		if (opcode != ISCSI_OP_SCSI_DATA_OUT) {
			iscsi_session_printk(KERN_INFO, session,
					     "task [op %x itt 0x%x/0x%x] rejected.\n",
					     opcode, task->itt, task->hdr_itt);
			return -EACCES;
		}
		/*
		 * And also all data-out PDUs in response to R2T
		 * if fast_abort is set.
		 */
		if (session->fast_abort) {
			iscsi_session_printk(KERN_INFO, session,
					     "task [op %x itt 0x%x/0x%x] fast abort.\n",
					     opcode, task->itt, task->hdr_itt);
			return -EACCES;
		}
		break;
	case ISCSI_TM_FUNC_ABORT_TASK:
		/*
		 * the caller has already checked if the task
		 * they want to abort was in the pending queue so if
		 * we are here the cmd pdu has gone out already, and
		 * we will only hit this for data-outs
		 */
		if (opcode == ISCSI_OP_SCSI_DATA_OUT &&
		    task->hdr_itt == tmf->rtt) {
			ISCSI_DBG_SESSION(session,
					  "Preventing task %x/%x from sending "
					  "data-out due to abort task in "
					  "progress\n", task->itt,
					  task->hdr_itt);
			return -EACCES;
		}
		break;
	}

	return 0;
}

/**
 * iscsi_prep_scsi_cmd_pdu - prep iscsi scsi cmd pdu
 * @task: iscsi task
 *
 * Prep basic iSCSI PDU fields for a scsi cmd pdu. The LLD should set
 * fields like dlength or final based on how much data it sends
 */
static int iscsi_prep_scsi_cmd_pdu(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *session = conn->session;
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_scsi_req *hdr;
	unsigned hdrlength, cmd_len, transfer_length;
	itt_t itt;
	int rc;

	rc = iscsi_check_tmf_restrictions(task, ISCSI_OP_SCSI_CMD);
	if (rc)
		return rc;

	if (conn->session->tt->alloc_pdu) {
		rc = conn->session->tt->alloc_pdu(task, ISCSI_OP_SCSI_CMD);
		if (rc)
			return rc;
	}
	hdr = (struct iscsi_scsi_req *)task->hdr;
	itt = hdr->itt;
	memset(hdr, 0, sizeof(*hdr));

	if (session->tt->parse_pdu_itt)
		hdr->itt = task->hdr_itt = itt;
	else
		hdr->itt = task->hdr_itt = build_itt(task->itt,
						     task->conn->session->age);
	task->hdr_len = 0;
	rc = iscsi_add_hdr(task, sizeof(*hdr));
	if (rc)
		return rc;
	hdr->opcode = ISCSI_OP_SCSI_CMD;
	hdr->flags = ISCSI_ATTR_SIMPLE;
	int_to_scsilun(sc->device->lun, &hdr->lun);
	task->lun = hdr->lun;
	hdr->exp_statsn = cpu_to_be32(conn->exp_statsn);
	cmd_len = sc->cmd_len;
	if (cmd_len < ISCSI_CDB_SIZE)
		memset(&hdr->cdb[cmd_len], 0, ISCSI_CDB_SIZE - cmd_len);
	else if (cmd_len > ISCSI_CDB_SIZE) {
		rc = iscsi_prep_ecdb_ahs(task);
		if (rc)
			return rc;
		cmd_len = ISCSI_CDB_SIZE;
	}
	memcpy(hdr->cdb, sc->cmnd, cmd_len);

	task->imm_count = 0;
	if (scsi_get_prot_op(sc) != SCSI_PROT_NORMAL)
		task->protected = true;

	transfer_length = scsi_transfer_length(sc);
	hdr->data_length = cpu_to_be32(transfer_length);
	if (sc->sc_data_direction == DMA_TO_DEVICE) {
		struct iscsi_r2t_info *r2t = &task->unsol_r2t;

		hdr->flags |= ISCSI_FLAG_CMD_WRITE;
		/*
		 * Write counters:
		 *
		 *	imm_count	bytes to be sent right after
		 *			SCSI PDU Header
		 *
		 *	unsol_count	bytes(as Data-Out) to be sent
		 *			without	R2T ack right after
		 *			immediate data
		 *
		 *	r2t data_length bytes to be sent via R2T ack's
		 *
		 *      pad_count       bytes to be sent as zero-padding
		 */
		memset(r2t, 0, sizeof(*r2t));

		if (session->imm_data_en) {
			if (transfer_length >= session->first_burst)
				task->imm_count = min(session->first_burst,
							conn->max_xmit_dlength);
			else
				task->imm_count = min(transfer_length,
						      conn->max_xmit_dlength);
			hton24(hdr->dlength, task->imm_count);
		} else
			zero_data(hdr->dlength);

		if (!session->initial_r2t_en) {
			r2t->data_length = min(session->first_burst,
					       transfer_length) -
					       task->imm_count;
			r2t->data_offset = task->imm_count;
			r2t->ttt = cpu_to_be32(ISCSI_RESERVED_TAG);
			r2t->exp_statsn = cpu_to_be32(conn->exp_statsn);
		}

		if (!task->unsol_r2t.data_length)
			/* No unsolicit Data-Out's */
			hdr->flags |= ISCSI_FLAG_CMD_FINAL;
	} else {
		hdr->flags |= ISCSI_FLAG_CMD_FINAL;
		zero_data(hdr->dlength);

		if (sc->sc_data_direction == DMA_FROM_DEVICE)
			hdr->flags |= ISCSI_FLAG_CMD_READ;
	}

	/* calculate size of additional header segments (AHSs) */
	hdrlength = task->hdr_len - sizeof(*hdr);

	WARN_ON(hdrlength & (ISCSI_PAD_LEN-1));
	hdrlength /= ISCSI_PAD_LEN;

	WARN_ON(hdrlength >= 256);
	hdr->hlength = hdrlength & 0xFF;
	hdr->cmdsn = task->cmdsn = cpu_to_be32(session->cmdsn);

	if (session->tt->init_task && session->tt->init_task(task))
		return -EIO;

	task->state = ISCSI_TASK_RUNNING;
	session->cmdsn++;

	conn->scsicmd_pdus_cnt++;
	ISCSI_DBG_SESSION(session, "iscsi prep [%s cid %d sc %p cdb 0x%x "
			  "itt 0x%x len %d cmdsn %d win %d]\n",
			  sc->sc_data_direction == DMA_TO_DEVICE ?
			  "write" : "read", conn->id, sc, sc->cmnd[0],
			  task->itt, transfer_length,
			  session->cmdsn,
			  session->max_cmdsn - session->exp_cmdsn + 1);
	return 0;
}

/**
 * iscsi_free_task - free a task
 * @task: iscsi cmd task
 *
 * Must be called with session back_lock.
 * This function returns the scsi command to scsi-ml or cleans
 * up mgmt tasks then returns the task to the pool.
 */
static void iscsi_free_task(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *session = conn->session;
	struct scsi_cmnd *sc = task->sc;
	int oldstate = task->state;

	ISCSI_DBG_SESSION(session, "freeing task itt 0x%x state %d sc %p\n",
			  task->itt, task->state, task->sc);

	session->tt->cleanup_task(task);
	task->state = ISCSI_TASK_FREE;
	task->sc = NULL;
	/*
	 * login task is preallocated so do not free
	 */
	if (conn->login_task == task)
		return;

	kfifo_in(&session->cmdpool.queue, (void*)&task, sizeof(void*));

	if (sc) {
		/* SCSI eh reuses commands to verify us */
		sc->SCp.ptr = NULL;
		/*
		 * queue command may call this to free the task, so
		 * it will decide how to return sc to scsi-ml.
		 */
		if (oldstate != ISCSI_TASK_REQUEUE_SCSIQ)
			scsi_done(sc);
	}
}

void __iscsi_get_task(struct iscsi_task *task)
{
	refcount_inc(&task->refcount);
}
EXPORT_SYMBOL_GPL(__iscsi_get_task);

void __iscsi_put_task(struct iscsi_task *task)
{
	if (refcount_dec_and_test(&task->refcount))
		iscsi_free_task(task);
}
EXPORT_SYMBOL_GPL(__iscsi_put_task);

void iscsi_put_task(struct iscsi_task *task)
{
	struct iscsi_session *session = task->conn->session;

	/* regular RX path uses back_lock */
	spin_lock_bh(&session->back_lock);
	__iscsi_put_task(task);
	spin_unlock_bh(&session->back_lock);
}
EXPORT_SYMBOL_GPL(iscsi_put_task);

/**
 * iscsi_complete_task - finish a task
 * @task: iscsi cmd task
 * @state: state to complete task with
 *
 * Must be called with session back_lock.
 */
static void iscsi_complete_task(struct iscsi_task *task, int state)
{
	struct iscsi_conn *conn = task->conn;

	ISCSI_DBG_SESSION(conn->session,
			  "complete task itt 0x%x state %d sc %p\n",
			  task->itt, task->state, task->sc);
	if (task->state == ISCSI_TASK_COMPLETED ||
	    task->state == ISCSI_TASK_ABRT_TMF ||
	    task->state == ISCSI_TASK_ABRT_SESS_RECOV ||
	    task->state == ISCSI_TASK_REQUEUE_SCSIQ)
		return;
	WARN_ON_ONCE(task->state == ISCSI_TASK_FREE);
	task->state = state;

	if (READ_ONCE(conn->ping_task) == task)
		WRITE_ONCE(conn->ping_task, NULL);

	/* release get from queueing */
	__iscsi_put_task(task);
}

/**
 * iscsi_complete_scsi_task - finish scsi task normally
 * @task: iscsi task for scsi cmd
 * @exp_cmdsn: expected cmd sn in cpu format
 * @max_cmdsn: max cmd sn in cpu format
 *
 * This is used when drivers do not need or cannot perform
 * lower level pdu processing.
 *
 * Called with session back_lock
 */
void iscsi_complete_scsi_task(struct iscsi_task *task,
			      uint32_t exp_cmdsn, uint32_t max_cmdsn)
{
	struct iscsi_conn *conn = task->conn;

	ISCSI_DBG_SESSION(conn->session, "[itt 0x%x]\n", task->itt);

	conn->last_recv = jiffies;
	__iscsi_update_cmdsn(conn->session, exp_cmdsn, max_cmdsn);
	iscsi_complete_task(task, ISCSI_TASK_COMPLETED);
}
EXPORT_SYMBOL_GPL(iscsi_complete_scsi_task);

/*
 * Must be called with back and frwd lock
 */
static bool cleanup_queued_task(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	bool early_complete = false;

	/* Bad target might have completed task while it was still running */
	if (task->state == ISCSI_TASK_COMPLETED)
		early_complete = true;

	if (!list_empty(&task->running)) {
		list_del_init(&task->running);
		/*
		 * If it's on a list but still running, this could be from
		 * a bad target sending a rsp early, cleanup from a TMF, or
		 * session recovery.
		 */
		if (task->state == ISCSI_TASK_RUNNING ||
		    task->state == ISCSI_TASK_COMPLETED)
			__iscsi_put_task(task);
	}

	if (conn->session->running_aborted_task == task) {
		conn->session->running_aborted_task = NULL;
		__iscsi_put_task(task);
	}

	if (conn->task == task) {
		conn->task = NULL;
		__iscsi_put_task(task);
	}

	return early_complete;
}

/*
 * session frwd lock must be held and if not called for a task that is still
 * pending or from the xmit thread, then xmit thread must be suspended
 */
static void fail_scsi_task(struct iscsi_task *task, int err)
{
	struct iscsi_conn *conn = task->conn;
	struct scsi_cmnd *sc;
	int state;

	spin_lock_bh(&conn->session->back_lock);
	if (cleanup_queued_task(task)) {
		spin_unlock_bh(&conn->session->back_lock);
		return;
	}

	if (task->state == ISCSI_TASK_PENDING) {
		/*
		 * cmd never made it to the xmit thread, so we should not count
		 * the cmd in the sequencing
		 */
		conn->session->queued_cmdsn--;
		/* it was never sent so just complete like normal */
		state = ISCSI_TASK_COMPLETED;
	} else if (err == DID_TRANSPORT_DISRUPTED)
		state = ISCSI_TASK_ABRT_SESS_RECOV;
	else
		state = ISCSI_TASK_ABRT_TMF;

	sc = task->sc;
	sc->result = err << 16;
	scsi_set_resid(sc, scsi_bufflen(sc));
	iscsi_complete_task(task, state);
	spin_unlock_bh(&conn->session->back_lock);
}

static int iscsi_prep_mgmt_task(struct iscsi_conn *conn,
				struct iscsi_task *task)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_hdr *hdr = task->hdr;
	struct iscsi_nopout *nop = (struct iscsi_nopout *)hdr;
	uint8_t opcode = hdr->opcode & ISCSI_OPCODE_MASK;

	if (conn->session->state == ISCSI_STATE_LOGGING_OUT)
		return -ENOTCONN;

	if (opcode != ISCSI_OP_LOGIN && opcode != ISCSI_OP_TEXT)
		nop->exp_statsn = cpu_to_be32(conn->exp_statsn);
	/*
	 * pre-format CmdSN for outgoing PDU.
	 */
	nop->cmdsn = cpu_to_be32(session->cmdsn);
	if (hdr->itt != RESERVED_ITT) {
		/*
		 * TODO: We always use immediate for normal session pdus.
		 * If we start to send tmfs or nops as non-immediate then
		 * we should start checking the cmdsn numbers for mgmt tasks.
		 *
		 * During discovery sessions iscsid sends TEXT as non immediate,
		 * but we always only send one PDU at a time.
		 */
		if (conn->c_stage == ISCSI_CONN_STARTED &&
		    !(hdr->opcode & ISCSI_OP_IMMEDIATE)) {
			session->queued_cmdsn++;
			session->cmdsn++;
		}
	}

	if (session->tt->init_task && session->tt->init_task(task))
		return -EIO;

	if ((hdr->opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_LOGOUT)
		session->state = ISCSI_STATE_LOGGING_OUT;

	task->state = ISCSI_TASK_RUNNING;
	ISCSI_DBG_SESSION(session, "mgmtpdu [op 0x%x hdr->itt 0x%x "
			  "datalen %d]\n", hdr->opcode & ISCSI_OPCODE_MASK,
			  hdr->itt, task->data_count);
	return 0;
}

static struct iscsi_task *
__iscsi_conn_send_pdu(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
		      char *data, uint32_t data_size)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_host *ihost = shost_priv(session->host);
	uint8_t opcode = hdr->opcode & ISCSI_OPCODE_MASK;
	struct iscsi_task *task;
	itt_t itt;

	if (session->state == ISCSI_STATE_TERMINATE)
		return NULL;

	if (opcode == ISCSI_OP_LOGIN || opcode == ISCSI_OP_TEXT) {
		/*
		 * Login and Text are sent serially, in
		 * request-followed-by-response sequence.
		 * Same task can be used. Same ITT must be used.
		 * Note that login_task is preallocated at conn_create().
		 */
		if (conn->login_task->state != ISCSI_TASK_FREE) {
			iscsi_conn_printk(KERN_ERR, conn, "Login/Text in "
					  "progress. Cannot start new task.\n");
			return NULL;
		}

		if (data_size > ISCSI_DEF_MAX_RECV_SEG_LEN) {
			iscsi_conn_printk(KERN_ERR, conn, "Invalid buffer len of %u for login task. Max len is %u\n", data_size, ISCSI_DEF_MAX_RECV_SEG_LEN);
			return NULL;
		}

		task = conn->login_task;
	} else {
		if (session->state != ISCSI_STATE_LOGGED_IN)
			return NULL;

		if (data_size != 0) {
			iscsi_conn_printk(KERN_ERR, conn, "Can not send data buffer of len %u for op 0x%x\n", data_size, opcode);
			return NULL;
		}

		BUG_ON(conn->c_stage == ISCSI_CONN_INITIAL_STAGE);
		BUG_ON(conn->c_stage == ISCSI_CONN_STOPPED);

		if (!kfifo_out(&session->cmdpool.queue,
				 (void*)&task, sizeof(void*)))
			return NULL;
	}
	/*
	 * released in complete pdu for task we expect a response for, and
	 * released by the lld when it has transmitted the task for
	 * pdus we do not expect a response for.
	 */
	refcount_set(&task->refcount, 1);
	task->conn = conn;
	task->sc = NULL;
	INIT_LIST_HEAD(&task->running);
	task->state = ISCSI_TASK_PENDING;

	if (data_size) {
		memcpy(task->data, data, data_size);
		task->data_count = data_size;
	} else
		task->data_count = 0;

	if (conn->session->tt->alloc_pdu) {
		if (conn->session->tt->alloc_pdu(task, hdr->opcode)) {
			iscsi_conn_printk(KERN_ERR, conn, "Could not allocate "
					 "pdu for mgmt task.\n");
			goto free_task;
		}
	}

	itt = task->hdr->itt;
	task->hdr_len = sizeof(struct iscsi_hdr);
	memcpy(task->hdr, hdr, sizeof(struct iscsi_hdr));

	if (hdr->itt != RESERVED_ITT) {
		if (session->tt->parse_pdu_itt)
			task->hdr->itt = itt;
		else
			task->hdr->itt = build_itt(task->itt,
						   task->conn->session->age);
	}

	if (unlikely(READ_ONCE(conn->ping_task) == INVALID_SCSI_TASK))
		WRITE_ONCE(conn->ping_task, task);

	if (!ihost->workq) {
		if (iscsi_prep_mgmt_task(conn, task))
			goto free_task;

		if (session->tt->xmit_task(task))
			goto free_task;
	} else {
		list_add_tail(&task->running, &conn->mgmtqueue);
		iscsi_conn_queue_work(conn);
	}

	return task;

free_task:
	/* regular RX path uses back_lock */
	spin_lock(&session->back_lock);
	__iscsi_put_task(task);
	spin_unlock(&session->back_lock);
	return NULL;
}

int iscsi_conn_send_pdu(struct iscsi_cls_conn *cls_conn, struct iscsi_hdr *hdr,
			char *data, uint32_t data_size)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	int err = 0;

	spin_lock_bh(&session->frwd_lock);
	if (!__iscsi_conn_send_pdu(conn, hdr, data, data_size))
		err = -EPERM;
	spin_unlock_bh(&session->frwd_lock);
	return err;
}
EXPORT_SYMBOL_GPL(iscsi_conn_send_pdu);

/**
 * iscsi_scsi_cmd_rsp - SCSI Command Response processing
 * @conn: iscsi connection
 * @hdr: iscsi header
 * @task: scsi command task
 * @data: cmd data buffer
 * @datalen: len of buffer
 *
 * iscsi_cmd_rsp sets up the scsi_cmnd fields based on the PDU and
 * then completes the command and task. called under back_lock
 **/
static void iscsi_scsi_cmd_rsp(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
			       struct iscsi_task *task, char *data,
			       int datalen)
{
	struct iscsi_scsi_rsp *rhdr = (struct iscsi_scsi_rsp *)hdr;
	struct iscsi_session *session = conn->session;
	struct scsi_cmnd *sc = task->sc;

	iscsi_update_cmdsn(session, (struct iscsi_nopin*)rhdr);
	conn->exp_statsn = be32_to_cpu(rhdr->statsn) + 1;

	sc->result = (DID_OK << 16) | rhdr->cmd_status;

	if (task->protected) {
		sector_t sector;
		u8 ascq;

		/**
		 * Transports that didn't implement check_protection
		 * callback but still published T10-PI support to scsi-mid
		 * deserve this BUG_ON.
		 **/
		BUG_ON(!session->tt->check_protection);

		ascq = session->tt->check_protection(task, &sector);
		if (ascq) {
			scsi_build_sense(sc, 1, ILLEGAL_REQUEST, 0x10, ascq);
			scsi_set_sense_information(sc->sense_buffer,
						   SCSI_SENSE_BUFFERSIZE,
						   sector);
			goto out;
		}
	}

	if (rhdr->response != ISCSI_STATUS_CMD_COMPLETED) {
		sc->result = DID_ERROR << 16;
		goto out;
	}

	if (rhdr->cmd_status == SAM_STAT_CHECK_CONDITION) {
		uint16_t senselen;

		if (datalen < 2) {
invalid_datalen:
			iscsi_conn_printk(KERN_ERR,  conn,
					 "Got CHECK_CONDITION but invalid data "
					 "buffer size of %d\n", datalen);
			sc->result = DID_BAD_TARGET << 16;
			goto out;
		}

		senselen = get_unaligned_be16(data);
		if (datalen < senselen)
			goto invalid_datalen;

		memcpy(sc->sense_buffer, data + 2,
		       min_t(uint16_t, senselen, SCSI_SENSE_BUFFERSIZE));
		ISCSI_DBG_SESSION(session, "copied %d bytes of sense\n",
				  min_t(uint16_t, senselen,
				  SCSI_SENSE_BUFFERSIZE));
	}

	if (rhdr->flags & (ISCSI_FLAG_CMD_BIDI_UNDERFLOW |
			   ISCSI_FLAG_CMD_BIDI_OVERFLOW)) {
		sc->result = (DID_BAD_TARGET << 16) | rhdr->cmd_status;
	}

	if (rhdr->flags & (ISCSI_FLAG_CMD_UNDERFLOW |
	                   ISCSI_FLAG_CMD_OVERFLOW)) {
		int res_count = be32_to_cpu(rhdr->residual_count);

		if (res_count > 0 &&
		    (rhdr->flags & ISCSI_FLAG_CMD_OVERFLOW ||
		     res_count <= scsi_bufflen(sc)))
			/* write side for bidi or uni-io set_resid */
			scsi_set_resid(sc, res_count);
		else
			sc->result = (DID_BAD_TARGET << 16) | rhdr->cmd_status;
	}
out:
	ISCSI_DBG_SESSION(session, "cmd rsp done [sc %p res %d itt 0x%x]\n",
			  sc, sc->result, task->itt);
	conn->scsirsp_pdus_cnt++;
	iscsi_complete_task(task, ISCSI_TASK_COMPLETED);
}

/**
 * iscsi_data_in_rsp - SCSI Data-In Response processing
 * @conn: iscsi connection
 * @hdr:  iscsi pdu
 * @task: scsi command task
 *
 * iscsi_data_in_rsp sets up the scsi_cmnd fields based on the data received
 * then completes the command and task. called under back_lock
 **/
static void
iscsi_data_in_rsp(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
		  struct iscsi_task *task)
{
	struct iscsi_data_rsp *rhdr = (struct iscsi_data_rsp *)hdr;
	struct scsi_cmnd *sc = task->sc;

	if (!(rhdr->flags & ISCSI_FLAG_DATA_STATUS))
		return;

	iscsi_update_cmdsn(conn->session, (struct iscsi_nopin *)hdr);
	sc->result = (DID_OK << 16) | rhdr->cmd_status;
	conn->exp_statsn = be32_to_cpu(rhdr->statsn) + 1;
	if (rhdr->flags & (ISCSI_FLAG_DATA_UNDERFLOW |
	                   ISCSI_FLAG_DATA_OVERFLOW)) {
		int res_count = be32_to_cpu(rhdr->residual_count);

		if (res_count > 0 &&
		    (rhdr->flags & ISCSI_FLAG_CMD_OVERFLOW ||
		     res_count <= sc->sdb.length))
			scsi_set_resid(sc, res_count);
		else
			sc->result = (DID_BAD_TARGET << 16) | rhdr->cmd_status;
	}

	ISCSI_DBG_SESSION(conn->session, "data in with status done "
			  "[sc %p res %d itt 0x%x]\n",
			  sc, sc->result, task->itt);
	conn->scsirsp_pdus_cnt++;
	iscsi_complete_task(task, ISCSI_TASK_COMPLETED);
}

static void iscsi_tmf_rsp(struct iscsi_conn *conn, struct iscsi_hdr *hdr)
{
	struct iscsi_tm_rsp *tmf = (struct iscsi_tm_rsp *)hdr;
	struct iscsi_session *session = conn->session;

	conn->exp_statsn = be32_to_cpu(hdr->statsn) + 1;
	conn->tmfrsp_pdus_cnt++;

	if (session->tmf_state != TMF_QUEUED)
		return;

	if (tmf->response == ISCSI_TMF_RSP_COMPLETE)
		session->tmf_state = TMF_SUCCESS;
	else if (tmf->response == ISCSI_TMF_RSP_NO_TASK)
		session->tmf_state = TMF_NOT_FOUND;
	else
		session->tmf_state = TMF_FAILED;
	wake_up(&session->ehwait);
}

static int iscsi_send_nopout(struct iscsi_conn *conn, struct iscsi_nopin *rhdr)
{
        struct iscsi_nopout hdr;
	struct iscsi_task *task;

	if (!rhdr) {
		if (READ_ONCE(conn->ping_task))
			return -EINVAL;
		WRITE_ONCE(conn->ping_task, INVALID_SCSI_TASK);
	}

	memset(&hdr, 0, sizeof(struct iscsi_nopout));
	hdr.opcode = ISCSI_OP_NOOP_OUT | ISCSI_OP_IMMEDIATE;
	hdr.flags = ISCSI_FLAG_CMD_FINAL;

	if (rhdr) {
		hdr.lun = rhdr->lun;
		hdr.ttt = rhdr->ttt;
		hdr.itt = RESERVED_ITT;
	} else
		hdr.ttt = RESERVED_ITT;

	task = __iscsi_conn_send_pdu(conn, (struct iscsi_hdr *)&hdr, NULL, 0);
	if (!task) {
		if (!rhdr)
			WRITE_ONCE(conn->ping_task, NULL);
		iscsi_conn_printk(KERN_ERR, conn, "Could not send nopout\n");
		return -EIO;
	} else if (!rhdr) {
		/* only track our nops */
		conn->last_ping = jiffies;
	}

	return 0;
}

/**
 * iscsi_nop_out_rsp - SCSI NOP Response processing
 * @task: scsi command task
 * @nop: the nop structure
 * @data: where to put the data
 * @datalen: length of data
 *
 * iscsi_nop_out_rsp handles nop response from use or
 * from user space. called under back_lock
 **/
static int iscsi_nop_out_rsp(struct iscsi_task *task,
			     struct iscsi_nopin *nop, char *data, int datalen)
{
	struct iscsi_conn *conn = task->conn;
	int rc = 0;

	if (READ_ONCE(conn->ping_task) != task) {
		/*
		 * If this is not in response to one of our
		 * nops then it must be from userspace.
		 */
		if (iscsi_recv_pdu(conn->cls_conn, (struct iscsi_hdr *)nop,
				   data, datalen))
			rc = ISCSI_ERR_CONN_FAILED;
	} else
		mod_timer(&conn->transport_timer, jiffies + conn->recv_timeout);
	iscsi_complete_task(task, ISCSI_TASK_COMPLETED);
	return rc;
}

static int iscsi_handle_reject(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
			       char *data, int datalen)
{
	struct iscsi_reject *reject = (struct iscsi_reject *)hdr;
	struct iscsi_hdr rejected_pdu;
	int opcode, rc = 0;

	conn->exp_statsn = be32_to_cpu(reject->statsn) + 1;

	if (ntoh24(reject->dlength) > datalen ||
	    ntoh24(reject->dlength) < sizeof(struct iscsi_hdr)) {
		iscsi_conn_printk(KERN_ERR, conn, "Cannot handle rejected "
				  "pdu. Invalid data length (pdu dlength "
				  "%u, datalen %d\n", ntoh24(reject->dlength),
				  datalen);
		return ISCSI_ERR_PROTO;
	}
	memcpy(&rejected_pdu, data, sizeof(struct iscsi_hdr));
	opcode = rejected_pdu.opcode & ISCSI_OPCODE_MASK;

	switch (reject->reason) {
	case ISCSI_REASON_DATA_DIGEST_ERROR:
		iscsi_conn_printk(KERN_ERR, conn,
				  "pdu (op 0x%x itt 0x%x) rejected "
				  "due to DataDigest error.\n",
				  opcode, rejected_pdu.itt);
		break;
	case ISCSI_REASON_IMM_CMD_REJECT:
		iscsi_conn_printk(KERN_ERR, conn,
				  "pdu (op 0x%x itt 0x%x) rejected. Too many "
				  "immediate commands.\n",
				  opcode, rejected_pdu.itt);
		/*
		 * We only send one TMF at a time so if the target could not
		 * handle it, then it should get fixed (RFC mandates that
		 * a target can handle one immediate TMF per conn).
		 *
		 * For nops-outs, we could have sent more than one if
		 * the target is sending us lots of nop-ins
		 */
		if (opcode != ISCSI_OP_NOOP_OUT)
			return 0;

		if (rejected_pdu.itt == cpu_to_be32(ISCSI_RESERVED_TAG)) {
			/*
			 * nop-out in response to target's nop-out rejected.
			 * Just resend.
			 */
			/* In RX path we are under back lock */
			spin_unlock(&conn->session->back_lock);
			spin_lock(&conn->session->frwd_lock);
			iscsi_send_nopout(conn,
					  (struct iscsi_nopin*)&rejected_pdu);
			spin_unlock(&conn->session->frwd_lock);
			spin_lock(&conn->session->back_lock);
		} else {
			struct iscsi_task *task;
			/*
			 * Our nop as ping got dropped. We know the target
			 * and transport are ok so just clean up
			 */
			task = iscsi_itt_to_task(conn, rejected_pdu.itt);
			if (!task) {
				iscsi_conn_printk(KERN_ERR, conn,
						 "Invalid pdu reject. Could "
						 "not lookup rejected task.\n");
				rc = ISCSI_ERR_BAD_ITT;
			} else
				rc = iscsi_nop_out_rsp(task,
					(struct iscsi_nopin*)&rejected_pdu,
					NULL, 0);
		}
		break;
	default:
		iscsi_conn_printk(KERN_ERR, conn,
				  "pdu (op 0x%x itt 0x%x) rejected. Reason "
				  "code 0x%x\n", rejected_pdu.opcode,
				  rejected_pdu.itt, reject->reason);
		break;
	}
	return rc;
}

/**
 * iscsi_itt_to_task - look up task by itt
 * @conn: iscsi connection
 * @itt: itt
 *
 * This should be used for mgmt tasks like login and nops, or if
 * the LDD's itt space does not include the session age.
 *
 * The session back_lock must be held.
 */
struct iscsi_task *iscsi_itt_to_task(struct iscsi_conn *conn, itt_t itt)
{
	struct iscsi_session *session = conn->session;
	int i;

	if (itt == RESERVED_ITT)
		return NULL;

	if (session->tt->parse_pdu_itt)
		session->tt->parse_pdu_itt(conn, itt, &i, NULL);
	else
		i = get_itt(itt);
	if (i >= session->cmds_max)
		return NULL;

	return session->cmds[i];
}
EXPORT_SYMBOL_GPL(iscsi_itt_to_task);

/**
 * __iscsi_complete_pdu - complete pdu
 * @conn: iscsi conn
 * @hdr: iscsi header
 * @data: data buffer
 * @datalen: len of data buffer
 *
 * Completes pdu processing by freeing any resources allocated at
 * queuecommand or send generic. session back_lock must be held and verify
 * itt must have been called.
 */
int __iscsi_complete_pdu(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
			 char *data, int datalen)
{
	struct iscsi_session *session = conn->session;
	int opcode = hdr->opcode & ISCSI_OPCODE_MASK, rc = 0;
	struct iscsi_task *task;
	uint32_t itt;

	conn->last_recv = jiffies;
	rc = iscsi_verify_itt(conn, hdr->itt);
	if (rc)
		return rc;

	if (hdr->itt != RESERVED_ITT)
		itt = get_itt(hdr->itt);
	else
		itt = ~0U;

	ISCSI_DBG_SESSION(session, "[op 0x%x cid %d itt 0x%x len %d]\n",
			  opcode, conn->id, itt, datalen);

	if (itt == ~0U) {
		iscsi_update_cmdsn(session, (struct iscsi_nopin*)hdr);

		switch(opcode) {
		case ISCSI_OP_NOOP_IN:
			if (datalen) {
				rc = ISCSI_ERR_PROTO;
				break;
			}

			if (hdr->ttt == cpu_to_be32(ISCSI_RESERVED_TAG))
				break;

			/* In RX path we are under back lock */
			spin_unlock(&session->back_lock);
			spin_lock(&session->frwd_lock);
			iscsi_send_nopout(conn, (struct iscsi_nopin*)hdr);
			spin_unlock(&session->frwd_lock);
			spin_lock(&session->back_lock);
			break;
		case ISCSI_OP_REJECT:
			rc = iscsi_handle_reject(conn, hdr, data, datalen);
			break;
		case ISCSI_OP_ASYNC_EVENT:
			conn->exp_statsn = be32_to_cpu(hdr->statsn) + 1;
			if (iscsi_recv_pdu(conn->cls_conn, hdr, data, datalen))
				rc = ISCSI_ERR_CONN_FAILED;
			break;
		default:
			rc = ISCSI_ERR_BAD_OPCODE;
			break;
		}
		goto out;
	}

	switch(opcode) {
	case ISCSI_OP_SCSI_CMD_RSP:
	case ISCSI_OP_SCSI_DATA_IN:
		task = iscsi_itt_to_ctask(conn, hdr->itt);
		if (!task)
			return ISCSI_ERR_BAD_ITT;
		task->last_xfer = jiffies;
		break;
	case ISCSI_OP_R2T:
		/*
		 * LLD handles R2Ts if they need to.
		 */
		return 0;
	case ISCSI_OP_LOGOUT_RSP:
	case ISCSI_OP_LOGIN_RSP:
	case ISCSI_OP_TEXT_RSP:
	case ISCSI_OP_SCSI_TMFUNC_RSP:
	case ISCSI_OP_NOOP_IN:
		task = iscsi_itt_to_task(conn, hdr->itt);
		if (!task)
			return ISCSI_ERR_BAD_ITT;
		break;
	default:
		return ISCSI_ERR_BAD_OPCODE;
	}

	switch(opcode) {
	case ISCSI_OP_SCSI_CMD_RSP:
		iscsi_scsi_cmd_rsp(conn, hdr, task, data, datalen);
		break;
	case ISCSI_OP_SCSI_DATA_IN:
		iscsi_data_in_rsp(conn, hdr, task);
		break;
	case ISCSI_OP_LOGOUT_RSP:
		iscsi_update_cmdsn(session, (struct iscsi_nopin*)hdr);
		if (datalen) {
			rc = ISCSI_ERR_PROTO;
			break;
		}
		conn->exp_statsn = be32_to_cpu(hdr->statsn) + 1;
		goto recv_pdu;
	case ISCSI_OP_LOGIN_RSP:
	case ISCSI_OP_TEXT_RSP:
		iscsi_update_cmdsn(session, (struct iscsi_nopin*)hdr);
		/*
		 * login related PDU's exp_statsn is handled in
		 * userspace
		 */
		goto recv_pdu;
	case ISCSI_OP_SCSI_TMFUNC_RSP:
		iscsi_update_cmdsn(session, (struct iscsi_nopin*)hdr);
		if (datalen) {
			rc = ISCSI_ERR_PROTO;
			break;
		}

		iscsi_tmf_rsp(conn, hdr);
		iscsi_complete_task(task, ISCSI_TASK_COMPLETED);
		break;
	case ISCSI_OP_NOOP_IN:
		iscsi_update_cmdsn(session, (struct iscsi_nopin*)hdr);
		if (hdr->ttt != cpu_to_be32(ISCSI_RESERVED_TAG) || datalen) {
			rc = ISCSI_ERR_PROTO;
			break;
		}
		conn->exp_statsn = be32_to_cpu(hdr->statsn) + 1;

		rc = iscsi_nop_out_rsp(task, (struct iscsi_nopin*)hdr,
				       data, datalen);
		break;
	default:
		rc = ISCSI_ERR_BAD_OPCODE;
		break;
	}

out:
	return rc;
recv_pdu:
	if (iscsi_recv_pdu(conn->cls_conn, hdr, data, datalen))
		rc = ISCSI_ERR_CONN_FAILED;
	iscsi_complete_task(task, ISCSI_TASK_COMPLETED);
	return rc;
}
EXPORT_SYMBOL_GPL(__iscsi_complete_pdu);

int iscsi_complete_pdu(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
		       char *data, int datalen)
{
	int rc;

	spin_lock(&conn->session->back_lock);
	rc = __iscsi_complete_pdu(conn, hdr, data, datalen);
	spin_unlock(&conn->session->back_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(iscsi_complete_pdu);

int iscsi_verify_itt(struct iscsi_conn *conn, itt_t itt)
{
	struct iscsi_session *session = conn->session;
	int age = 0, i = 0;

	if (itt == RESERVED_ITT)
		return 0;

	if (session->tt->parse_pdu_itt)
		session->tt->parse_pdu_itt(conn, itt, &i, &age);
	else {
		i = get_itt(itt);
		age = ((__force u32)itt >> ISCSI_AGE_SHIFT) & ISCSI_AGE_MASK;
	}

	if (age != session->age) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "received itt %x expected session age (%x)\n",
				  (__force u32)itt, session->age);
		return ISCSI_ERR_BAD_ITT;
	}

	if (i >= session->cmds_max) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "received invalid itt index %u (max cmds "
				   "%u.\n", i, session->cmds_max);
		return ISCSI_ERR_BAD_ITT;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_verify_itt);

/**
 * iscsi_itt_to_ctask - look up ctask by itt
 * @conn: iscsi connection
 * @itt: itt
 *
 * This should be used for cmd tasks.
 *
 * The session back_lock must be held.
 */
struct iscsi_task *iscsi_itt_to_ctask(struct iscsi_conn *conn, itt_t itt)
{
	struct iscsi_task *task;

	if (iscsi_verify_itt(conn, itt))
		return NULL;

	task = iscsi_itt_to_task(conn, itt);
	if (!task || !task->sc)
		return NULL;

	if (task->sc->SCp.phase != conn->session->age) {
		iscsi_session_printk(KERN_ERR, conn->session,
				  "task's session age %d, expected %d\n",
				  task->sc->SCp.phase, conn->session->age);
		return NULL;
	}

	return task;
}
EXPORT_SYMBOL_GPL(iscsi_itt_to_ctask);

void iscsi_session_failure(struct iscsi_session *session,
			   enum iscsi_err err)
{
	struct iscsi_conn *conn;

	spin_lock_bh(&session->frwd_lock);
	conn = session->leadconn;
	if (session->state == ISCSI_STATE_TERMINATE || !conn) {
		spin_unlock_bh(&session->frwd_lock);
		return;
	}

	iscsi_get_conn(conn->cls_conn);
	spin_unlock_bh(&session->frwd_lock);
	/*
	 * if the host is being removed bypass the connection
	 * recovery initialization because we are going to kill
	 * the session.
	 */
	if (err == ISCSI_ERR_INVALID_HOST)
		iscsi_conn_error_event(conn->cls_conn, err);
	else
		iscsi_conn_failure(conn, err);
	iscsi_put_conn(conn->cls_conn);
}
EXPORT_SYMBOL_GPL(iscsi_session_failure);

static bool iscsi_set_conn_failed(struct iscsi_conn *conn)
{
	struct iscsi_session *session = conn->session;

	if (session->state == ISCSI_STATE_FAILED)
		return false;

	if (conn->stop_stage == 0)
		session->state = ISCSI_STATE_FAILED;

	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_rx);
	return true;
}

void iscsi_conn_failure(struct iscsi_conn *conn, enum iscsi_err err)
{
	struct iscsi_session *session = conn->session;
	bool needs_evt;

	spin_lock_bh(&session->frwd_lock);
	needs_evt = iscsi_set_conn_failed(conn);
	spin_unlock_bh(&session->frwd_lock);

	if (needs_evt)
		iscsi_conn_error_event(conn->cls_conn, err);
}
EXPORT_SYMBOL_GPL(iscsi_conn_failure);

static int iscsi_check_cmdsn_window_closed(struct iscsi_conn *conn)
{
	struct iscsi_session *session = conn->session;

	/*
	 * Check for iSCSI window and take care of CmdSN wrap-around
	 */
	if (!iscsi_sna_lte(session->queued_cmdsn, session->max_cmdsn)) {
		ISCSI_DBG_SESSION(session, "iSCSI CmdSN closed. ExpCmdSn "
				  "%u MaxCmdSN %u CmdSN %u/%u\n",
				  session->exp_cmdsn, session->max_cmdsn,
				  session->cmdsn, session->queued_cmdsn);
		return -ENOSPC;
	}
	return 0;
}

static int iscsi_xmit_task(struct iscsi_conn *conn, struct iscsi_task *task,
			   bool was_requeue)
{
	int rc;

	spin_lock_bh(&conn->session->back_lock);

	if (!conn->task) {
		/* Take a ref so we can access it after xmit_task() */
		__iscsi_get_task(task);
	} else {
		/* Already have a ref from when we failed to send it last call */
		conn->task = NULL;
	}

	/*
	 * If this was a requeue for a R2T we have an extra ref on the task in
	 * case a bad target sends a cmd rsp before we have handled the task.
	 */
	if (was_requeue)
		__iscsi_put_task(task);

	/*
	 * Do this after dropping the extra ref because if this was a requeue
	 * it's removed from that list and cleanup_queued_task would miss it.
	 */
	if (test_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx)) {
		/*
		 * Save the task and ref in case we weren't cleaning up this
		 * task and get woken up again.
		 */
		conn->task = task;
		spin_unlock_bh(&conn->session->back_lock);
		return -ENODATA;
	}
	spin_unlock_bh(&conn->session->back_lock);

	spin_unlock_bh(&conn->session->frwd_lock);
	rc = conn->session->tt->xmit_task(task);
	spin_lock_bh(&conn->session->frwd_lock);
	if (!rc) {
		/* done with this task */
		task->last_xfer = jiffies;
	}
	/* regular RX path uses back_lock */
	spin_lock(&conn->session->back_lock);
	if (rc && task->state == ISCSI_TASK_RUNNING) {
		/*
		 * get an extra ref that is released next time we access it
		 * as conn->task above.
		 */
		__iscsi_get_task(task);
		conn->task = task;
	}

	__iscsi_put_task(task);
	spin_unlock(&conn->session->back_lock);
	return rc;
}

/**
 * iscsi_requeue_task - requeue task to run from session workqueue
 * @task: task to requeue
 *
 * Callers must have taken a ref to the task that is going to be requeued.
 */
void iscsi_requeue_task(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;

	/*
	 * this may be on the requeue list already if the xmit_task callout
	 * is handling the r2ts while we are adding new ones
	 */
	spin_lock_bh(&conn->session->frwd_lock);
	if (list_empty(&task->running)) {
		list_add_tail(&task->running, &conn->requeue);
	} else {
		/*
		 * Don't need the extra ref since it's already requeued and
		 * has a ref.
		 */
		iscsi_put_task(task);
	}
	iscsi_conn_queue_work(conn);
	spin_unlock_bh(&conn->session->frwd_lock);
}
EXPORT_SYMBOL_GPL(iscsi_requeue_task);

/**
 * iscsi_data_xmit - xmit any command into the scheduled connection
 * @conn: iscsi connection
 *
 * Notes:
 *	The function can return -EAGAIN in which case the caller must
 *	re-schedule it again later or recover. '0' return code means
 *	successful xmit.
 **/
static int iscsi_data_xmit(struct iscsi_conn *conn)
{
	struct iscsi_task *task;
	int rc = 0;

	spin_lock_bh(&conn->session->frwd_lock);
	if (test_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx)) {
		ISCSI_DBG_SESSION(conn->session, "Tx suspended!\n");
		spin_unlock_bh(&conn->session->frwd_lock);
		return -ENODATA;
	}

	if (conn->task) {
		rc = iscsi_xmit_task(conn, conn->task, false);
	        if (rc)
		        goto done;
	}

	/*
	 * process mgmt pdus like nops before commands since we should
	 * only have one nop-out as a ping from us and targets should not
	 * overflow us with nop-ins
	 */
check_mgmt:
	while (!list_empty(&conn->mgmtqueue)) {
		task = list_entry(conn->mgmtqueue.next, struct iscsi_task,
				  running);
		list_del_init(&task->running);
		if (iscsi_prep_mgmt_task(conn, task)) {
			/* regular RX path uses back_lock */
			spin_lock_bh(&conn->session->back_lock);
			__iscsi_put_task(task);
			spin_unlock_bh(&conn->session->back_lock);
			continue;
		}
		rc = iscsi_xmit_task(conn, task, false);
		if (rc)
			goto done;
	}

	/* process pending command queue */
	while (!list_empty(&conn->cmdqueue)) {
		task = list_entry(conn->cmdqueue.next, struct iscsi_task,
				  running);
		list_del_init(&task->running);
		if (conn->session->state == ISCSI_STATE_LOGGING_OUT) {
			fail_scsi_task(task, DID_IMM_RETRY);
			continue;
		}
		rc = iscsi_prep_scsi_cmd_pdu(task);
		if (rc) {
			if (rc == -ENOMEM || rc == -EACCES)
				fail_scsi_task(task, DID_IMM_RETRY);
			else
				fail_scsi_task(task, DID_ABORT);
			continue;
		}
		rc = iscsi_xmit_task(conn, task, false);
		if (rc)
			goto done;
		/*
		 * we could continuously get new task requests so
		 * we need to check the mgmt queue for nops that need to
		 * be sent to aviod starvation
		 */
		if (!list_empty(&conn->mgmtqueue))
			goto check_mgmt;
	}

	while (!list_empty(&conn->requeue)) {
		/*
		 * we always do fastlogout - conn stop code will clean up.
		 */
		if (conn->session->state == ISCSI_STATE_LOGGING_OUT)
			break;

		task = list_entry(conn->requeue.next, struct iscsi_task,
				  running);

		if (iscsi_check_tmf_restrictions(task, ISCSI_OP_SCSI_DATA_OUT))
			break;

		list_del_init(&task->running);
		rc = iscsi_xmit_task(conn, task, true);
		if (rc)
			goto done;
		if (!list_empty(&conn->mgmtqueue))
			goto check_mgmt;
	}
	spin_unlock_bh(&conn->session->frwd_lock);
	return -ENODATA;

done:
	spin_unlock_bh(&conn->session->frwd_lock);
	return rc;
}

static void iscsi_xmitworker(struct work_struct *work)
{
	struct iscsi_conn *conn =
		container_of(work, struct iscsi_conn, xmitwork);
	int rc;
	/*
	 * serialize Xmit worker on a per-connection basis.
	 */
	do {
		rc = iscsi_data_xmit(conn);
	} while (rc >= 0 || rc == -EAGAIN);
}

static inline struct iscsi_task *iscsi_alloc_task(struct iscsi_conn *conn,
						  struct scsi_cmnd *sc)
{
	struct iscsi_task *task;

	if (!kfifo_out(&conn->session->cmdpool.queue,
			 (void *) &task, sizeof(void *)))
		return NULL;

	sc->SCp.phase = conn->session->age;
	sc->SCp.ptr = (char *) task;

	refcount_set(&task->refcount, 1);
	task->state = ISCSI_TASK_PENDING;
	task->conn = conn;
	task->sc = sc;
	task->have_checked_conn = false;
	task->last_timeout = jiffies;
	task->last_xfer = jiffies;
	task->protected = false;
	INIT_LIST_HEAD(&task->running);
	return task;
}

enum {
	FAILURE_BAD_HOST = 1,
	FAILURE_SESSION_FAILED,
	FAILURE_SESSION_FREED,
	FAILURE_WINDOW_CLOSED,
	FAILURE_OOM,
	FAILURE_SESSION_TERMINATE,
	FAILURE_SESSION_IN_RECOVERY,
	FAILURE_SESSION_RECOVERY_TIMEOUT,
	FAILURE_SESSION_LOGGING_OUT,
	FAILURE_SESSION_NOT_READY,
};

int iscsi_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *sc)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_host *ihost;
	int reason = 0;
	struct iscsi_session *session;
	struct iscsi_conn *conn;
	struct iscsi_task *task = NULL;

	sc->result = 0;
	sc->SCp.ptr = NULL;

	ihost = shost_priv(host);

	cls_session = starget_to_session(scsi_target(sc->device));
	session = cls_session->dd_data;
	spin_lock_bh(&session->frwd_lock);

	reason = iscsi_session_chkready(cls_session);
	if (reason) {
		sc->result = reason;
		goto fault;
	}

	if (session->state != ISCSI_STATE_LOGGED_IN) {
		/*
		 * to handle the race between when we set the recovery state
		 * and block the session we requeue here (commands could
		 * be entering our queuecommand while a block is starting
		 * up because the block code is not locked)
		 */
		switch (session->state) {
		case ISCSI_STATE_FAILED:
			/*
			 * cmds should fail during shutdown, if the session
			 * state is bad, allowing completion to happen
			 */
			if (unlikely(system_state != SYSTEM_RUNNING)) {
				reason = FAILURE_SESSION_FAILED;
				sc->result = DID_NO_CONNECT << 16;
				break;
			}
			fallthrough;
		case ISCSI_STATE_IN_RECOVERY:
			reason = FAILURE_SESSION_IN_RECOVERY;
			sc->result = DID_IMM_RETRY << 16;
			break;
		case ISCSI_STATE_LOGGING_OUT:
			reason = FAILURE_SESSION_LOGGING_OUT;
			sc->result = DID_IMM_RETRY << 16;
			break;
		case ISCSI_STATE_RECOVERY_FAILED:
			reason = FAILURE_SESSION_RECOVERY_TIMEOUT;
			sc->result = DID_TRANSPORT_FAILFAST << 16;
			break;
		case ISCSI_STATE_TERMINATE:
			reason = FAILURE_SESSION_TERMINATE;
			sc->result = DID_NO_CONNECT << 16;
			break;
		default:
			reason = FAILURE_SESSION_FREED;
			sc->result = DID_NO_CONNECT << 16;
		}
		goto fault;
	}

	conn = session->leadconn;
	if (!conn) {
		reason = FAILURE_SESSION_FREED;
		sc->result = DID_NO_CONNECT << 16;
		goto fault;
	}

	if (test_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx)) {
		reason = FAILURE_SESSION_IN_RECOVERY;
		sc->result = DID_REQUEUE << 16;
		goto fault;
	}

	if (iscsi_check_cmdsn_window_closed(conn)) {
		reason = FAILURE_WINDOW_CLOSED;
		goto reject;
	}

	task = iscsi_alloc_task(conn, sc);
	if (!task) {
		reason = FAILURE_OOM;
		goto reject;
	}

	if (!ihost->workq) {
		reason = iscsi_prep_scsi_cmd_pdu(task);
		if (reason) {
			if (reason == -ENOMEM ||  reason == -EACCES) {
				reason = FAILURE_OOM;
				goto prepd_reject;
			} else {
				sc->result = DID_ABORT << 16;
				goto prepd_fault;
			}
		}
		if (session->tt->xmit_task(task)) {
			session->cmdsn--;
			reason = FAILURE_SESSION_NOT_READY;
			goto prepd_reject;
		}
	} else {
		list_add_tail(&task->running, &conn->cmdqueue);
		iscsi_conn_queue_work(conn);
	}

	session->queued_cmdsn++;
	spin_unlock_bh(&session->frwd_lock);
	return 0;

prepd_reject:
	spin_lock_bh(&session->back_lock);
	iscsi_complete_task(task, ISCSI_TASK_REQUEUE_SCSIQ);
	spin_unlock_bh(&session->back_lock);
reject:
	spin_unlock_bh(&session->frwd_lock);
	ISCSI_DBG_SESSION(session, "cmd 0x%x rejected (%d)\n",
			  sc->cmnd[0], reason);
	return SCSI_MLQUEUE_TARGET_BUSY;

prepd_fault:
	spin_lock_bh(&session->back_lock);
	iscsi_complete_task(task, ISCSI_TASK_REQUEUE_SCSIQ);
	spin_unlock_bh(&session->back_lock);
fault:
	spin_unlock_bh(&session->frwd_lock);
	ISCSI_DBG_SESSION(session, "iscsi: cmd 0x%x is not queued (%d)\n",
			  sc->cmnd[0], reason);
	scsi_set_resid(sc, scsi_bufflen(sc));
	scsi_done(sc);
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_queuecommand);

int iscsi_target_alloc(struct scsi_target *starget)
{
	struct iscsi_cls_session *cls_session = starget_to_session(starget);
	struct iscsi_session *session = cls_session->dd_data;

	starget->can_queue = session->scsi_cmds_max;
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_target_alloc);

static void iscsi_tmf_timedout(struct timer_list *t)
{
	struct iscsi_session *session = from_timer(session, t, tmf_timer);

	spin_lock(&session->frwd_lock);
	if (session->tmf_state == TMF_QUEUED) {
		session->tmf_state = TMF_TIMEDOUT;
		ISCSI_DBG_EH(session, "tmf timedout\n");
		/* unblock eh_abort() */
		wake_up(&session->ehwait);
	}
	spin_unlock(&session->frwd_lock);
}

static int iscsi_exec_task_mgmt_fn(struct iscsi_conn *conn,
				   struct iscsi_tm *hdr, int age,
				   int timeout)
	__must_hold(&session->frwd_lock)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_task *task;

	task = __iscsi_conn_send_pdu(conn, (struct iscsi_hdr *)hdr,
				      NULL, 0);
	if (!task) {
		spin_unlock_bh(&session->frwd_lock);
		iscsi_conn_printk(KERN_ERR, conn, "Could not send TMF.\n");
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
		spin_lock_bh(&session->frwd_lock);
		return -EPERM;
	}
	conn->tmfcmd_pdus_cnt++;
	session->tmf_timer.expires = timeout * HZ + jiffies;
	add_timer(&session->tmf_timer);
	ISCSI_DBG_EH(session, "tmf set timeout\n");

	spin_unlock_bh(&session->frwd_lock);
	mutex_unlock(&session->eh_mutex);

	/*
	 * block eh thread until:
	 *
	 * 1) tmf response
	 * 2) tmf timeout
	 * 3) session is terminated or restarted or userspace has
	 * given up on recovery
	 */
	wait_event_interruptible(session->ehwait, age != session->age ||
				 session->state != ISCSI_STATE_LOGGED_IN ||
				 session->tmf_state != TMF_QUEUED);
	if (signal_pending(current))
		flush_signals(current);
	del_timer_sync(&session->tmf_timer);

	mutex_lock(&session->eh_mutex);
	spin_lock_bh(&session->frwd_lock);
	/* if the session drops it will clean up the task */
	if (age != session->age ||
	    session->state != ISCSI_STATE_LOGGED_IN)
		return -ENOTCONN;
	return 0;
}

/*
 * Fail commands. session frwd lock held and xmit thread flushed.
 */
static void fail_scsi_tasks(struct iscsi_conn *conn, u64 lun, int error)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_task *task;
	int i;

	spin_lock_bh(&session->back_lock);
	for (i = 0; i < session->cmds_max; i++) {
		task = session->cmds[i];
		if (!task->sc || task->state == ISCSI_TASK_FREE)
			continue;

		if (lun != -1 && lun != task->sc->device->lun)
			continue;

		__iscsi_get_task(task);
		spin_unlock_bh(&session->back_lock);

		ISCSI_DBG_SESSION(session,
				  "failing sc %p itt 0x%x state %d\n",
				  task->sc, task->itt, task->state);
		fail_scsi_task(task, error);

		spin_unlock_bh(&session->frwd_lock);
		iscsi_put_task(task);
		spin_lock_bh(&session->frwd_lock);

		spin_lock_bh(&session->back_lock);
	}

	spin_unlock_bh(&session->back_lock);
}

/**
 * iscsi_suspend_queue - suspend iscsi_queuecommand
 * @conn: iscsi conn to stop queueing IO on
 *
 * This grabs the session frwd_lock to make sure no one is in
 * xmit_task/queuecommand, and then sets suspend to prevent
 * new commands from being queued. This only needs to be called
 * by offload drivers that need to sync a path like ep disconnect
 * with the iscsi_queuecommand/xmit_task. To start IO again libiscsi
 * will call iscsi_start_tx and iscsi_unblock_session when in FFP.
 */
void iscsi_suspend_queue(struct iscsi_conn *conn)
{
	spin_lock_bh(&conn->session->frwd_lock);
	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	spin_unlock_bh(&conn->session->frwd_lock);
}
EXPORT_SYMBOL_GPL(iscsi_suspend_queue);

/**
 * iscsi_suspend_tx - suspend iscsi_data_xmit
 * @conn: iscsi conn tp stop processing IO on.
 *
 * This function sets the suspend bit to prevent iscsi_data_xmit
 * from sending new IO, and if work is queued on the xmit thread
 * it will wait for it to be completed.
 */
void iscsi_suspend_tx(struct iscsi_conn *conn)
{
	struct Scsi_Host *shost = conn->session->host;
	struct iscsi_host *ihost = shost_priv(shost);

	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	if (ihost->workq)
		flush_workqueue(ihost->workq);
}
EXPORT_SYMBOL_GPL(iscsi_suspend_tx);

static void iscsi_start_tx(struct iscsi_conn *conn)
{
	clear_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	iscsi_conn_queue_work(conn);
}

/*
 * We want to make sure a ping is in flight. It has timed out.
 * And we are not busy processing a pdu that is making
 * progress but got started before the ping and is taking a while
 * to complete so the ping is just stuck behind it in a queue.
 */
static int iscsi_has_ping_timed_out(struct iscsi_conn *conn)
{
	if (READ_ONCE(conn->ping_task) &&
	    time_before_eq(conn->last_recv + (conn->recv_timeout * HZ) +
			   (conn->ping_timeout * HZ), jiffies))
		return 1;
	else
		return 0;
}

enum blk_eh_timer_return iscsi_eh_cmd_timed_out(struct scsi_cmnd *sc)
{
	enum blk_eh_timer_return rc = BLK_EH_DONE;
	struct iscsi_task *task = NULL, *running_task;
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	struct iscsi_conn *conn;
	int i;

	cls_session = starget_to_session(scsi_target(sc->device));
	session = cls_session->dd_data;

	ISCSI_DBG_EH(session, "scsi cmd %p timedout\n", sc);

	spin_lock_bh(&session->frwd_lock);
	spin_lock(&session->back_lock);
	task = (struct iscsi_task *)sc->SCp.ptr;
	if (!task) {
		/*
		 * Raced with completion. Blk layer has taken ownership
		 * so let timeout code complete it now.
		 */
		rc = BLK_EH_DONE;
		spin_unlock(&session->back_lock);
		goto done;
	}
	__iscsi_get_task(task);
	spin_unlock(&session->back_lock);

	if (session->state != ISCSI_STATE_LOGGED_IN) {
		/*
		 * During shutdown, if session is prematurely disconnected,
		 * recovery won't happen and there will be hung cmds. Not
		 * handling cmds would trigger EH, also bad in this case.
		 * Instead, handle cmd, allow completion to happen and let
		 * upper layer to deal with the result.
		 */
		if (unlikely(system_state != SYSTEM_RUNNING)) {
			sc->result = DID_NO_CONNECT << 16;
			ISCSI_DBG_EH(session, "sc on shutdown, handled\n");
			rc = BLK_EH_DONE;
			goto done;
		}
		/*
		 * We are probably in the middle of iscsi recovery so let
		 * that complete and handle the error.
		 */
		rc = BLK_EH_RESET_TIMER;
		goto done;
	}

	conn = session->leadconn;
	if (!conn) {
		/* In the middle of shuting down */
		rc = BLK_EH_RESET_TIMER;
		goto done;
	}

	/*
	 * If we have sent (at least queued to the network layer) a pdu or
	 * recvd one for the task since the last timeout ask for
	 * more time. If on the next timeout we have not made progress
	 * we can check if it is the task or connection when we send the
	 * nop as a ping.
	 */
	if (time_after(task->last_xfer, task->last_timeout)) {
		ISCSI_DBG_EH(session, "Command making progress. Asking "
			     "scsi-ml for more time to complete. "
			     "Last data xfer at %lu. Last timeout was at "
			     "%lu\n.", task->last_xfer, task->last_timeout);
		task->have_checked_conn = false;
		rc = BLK_EH_RESET_TIMER;
		goto done;
	}

	if (!conn->recv_timeout && !conn->ping_timeout)
		goto done;
	/*
	 * if the ping timedout then we are in the middle of cleaning up
	 * and can let the iscsi eh handle it
	 */
	if (iscsi_has_ping_timed_out(conn)) {
		rc = BLK_EH_RESET_TIMER;
		goto done;
	}

	spin_lock(&session->back_lock);
	for (i = 0; i < conn->session->cmds_max; i++) {
		running_task = conn->session->cmds[i];
		if (!running_task->sc || running_task == task ||
		     running_task->state != ISCSI_TASK_RUNNING)
			continue;

		/*
		 * Only check if cmds started before this one have made
		 * progress, or this could never fail
		 */
		if (time_after(running_task->sc->jiffies_at_alloc,
			       task->sc->jiffies_at_alloc))
			continue;

		if (time_after(running_task->last_xfer, task->last_timeout)) {
			/*
			 * This task has not made progress, but a task
			 * started before us has transferred data since
			 * we started/last-checked. We could be queueing
			 * too many tasks or the LU is bad.
			 *
			 * If the device is bad the cmds ahead of us on
			 * other devs will complete, and this loop will
			 * eventually fail starting the scsi eh.
			 */
			ISCSI_DBG_EH(session, "Command has not made progress "
				     "but commands ahead of it have. "
				     "Asking scsi-ml for more time to "
				     "complete. Our last xfer vs running task "
				     "last xfer %lu/%lu. Last check %lu.\n",
				     task->last_xfer, running_task->last_xfer,
				     task->last_timeout);
			spin_unlock(&session->back_lock);
			rc = BLK_EH_RESET_TIMER;
			goto done;
		}
	}
	spin_unlock(&session->back_lock);

	/* Assumes nop timeout is shorter than scsi cmd timeout */
	if (task->have_checked_conn)
		goto done;

	/*
	 * Checking the transport already or nop from a cmd timeout still
	 * running
	 */
	if (READ_ONCE(conn->ping_task)) {
		task->have_checked_conn = true;
		rc = BLK_EH_RESET_TIMER;
		goto done;
	}

	/* Make sure there is a transport check done */
	iscsi_send_nopout(conn, NULL);
	task->have_checked_conn = true;
	rc = BLK_EH_RESET_TIMER;

done:
	spin_unlock_bh(&session->frwd_lock);

	if (task) {
		task->last_timeout = jiffies;
		iscsi_put_task(task);
	}
	ISCSI_DBG_EH(session, "return %s\n", rc == BLK_EH_RESET_TIMER ?
		     "timer reset" : "shutdown or nh");
	return rc;
}
EXPORT_SYMBOL_GPL(iscsi_eh_cmd_timed_out);

static void iscsi_check_transport_timeouts(struct timer_list *t)
{
	struct iscsi_conn *conn = from_timer(conn, t, transport_timer);
	struct iscsi_session *session = conn->session;
	unsigned long recv_timeout, next_timeout = 0, last_recv;

	spin_lock(&session->frwd_lock);
	if (session->state != ISCSI_STATE_LOGGED_IN)
		goto done;

	recv_timeout = conn->recv_timeout;
	if (!recv_timeout)
		goto done;

	recv_timeout *= HZ;
	last_recv = conn->last_recv;

	if (iscsi_has_ping_timed_out(conn)) {
		iscsi_conn_printk(KERN_ERR, conn, "ping timeout of %d secs "
				  "expired, recv timeout %d, last rx %lu, "
				  "last ping %lu, now %lu\n",
				  conn->ping_timeout, conn->recv_timeout,
				  last_recv, conn->last_ping, jiffies);
		spin_unlock(&session->frwd_lock);
		iscsi_conn_failure(conn, ISCSI_ERR_NOP_TIMEDOUT);
		return;
	}

	if (time_before_eq(last_recv + recv_timeout, jiffies)) {
		/* send a ping to try to provoke some traffic */
		ISCSI_DBG_CONN(conn, "Sending nopout as ping\n");
		if (iscsi_send_nopout(conn, NULL))
			next_timeout = jiffies + (1 * HZ);
		else
			next_timeout = conn->last_ping + (conn->ping_timeout * HZ);
	} else
		next_timeout = last_recv + recv_timeout;

	ISCSI_DBG_CONN(conn, "Setting next tmo %lu\n", next_timeout);
	mod_timer(&conn->transport_timer, next_timeout);
done:
	spin_unlock(&session->frwd_lock);
}

/**
 * iscsi_conn_unbind - prevent queueing to conn.
 * @cls_conn: iscsi conn ep is bound to.
 * @is_active: is the conn in use for boot or is this for EH/termination
 *
 * This must be called by drivers implementing the ep_disconnect callout.
 * It disables queueing to the connection from libiscsi in preparation for
 * an ep_disconnect call.
 */
void iscsi_conn_unbind(struct iscsi_cls_conn *cls_conn, bool is_active)
{
	struct iscsi_session *session;
	struct iscsi_conn *conn;

	if (!cls_conn)
		return;

	conn = cls_conn->dd_data;
	session = conn->session;
	/*
	 * Wait for iscsi_eh calls to exit. We don't wait for the tmf to
	 * complete or timeout. The caller just wants to know what's running
	 * is everything that needs to be cleaned up, and no cmds will be
	 * queued.
	 */
	mutex_lock(&session->eh_mutex);

	iscsi_suspend_queue(conn);
	iscsi_suspend_tx(conn);

	spin_lock_bh(&session->frwd_lock);
	if (!is_active) {
		/*
		 * if logout timed out before userspace could even send a PDU
		 * the state might still be in ISCSI_STATE_LOGGED_IN and
		 * allowing new cmds and TMFs.
		 */
		if (session->state == ISCSI_STATE_LOGGED_IN)
			iscsi_set_conn_failed(conn);
	}
	spin_unlock_bh(&session->frwd_lock);
	mutex_unlock(&session->eh_mutex);
}
EXPORT_SYMBOL_GPL(iscsi_conn_unbind);

static void iscsi_prep_abort_task_pdu(struct iscsi_task *task,
				      struct iscsi_tm *hdr)
{
	memset(hdr, 0, sizeof(*hdr));
	hdr->opcode = ISCSI_OP_SCSI_TMFUNC | ISCSI_OP_IMMEDIATE;
	hdr->flags = ISCSI_TM_FUNC_ABORT_TASK & ISCSI_FLAG_TM_FUNC_MASK;
	hdr->flags |= ISCSI_FLAG_CMD_FINAL;
	hdr->lun = task->lun;
	hdr->rtt = task->hdr_itt;
	hdr->refcmdsn = task->cmdsn;
}

int iscsi_eh_abort(struct scsi_cmnd *sc)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	struct iscsi_conn *conn;
	struct iscsi_task *task;
	struct iscsi_tm *hdr;
	int age;

	cls_session = starget_to_session(scsi_target(sc->device));
	session = cls_session->dd_data;

	ISCSI_DBG_EH(session, "aborting sc %p\n", sc);

	mutex_lock(&session->eh_mutex);
	spin_lock_bh(&session->frwd_lock);
	/*
	 * if session was ISCSI_STATE_IN_RECOVERY then we may not have
	 * got the command.
	 */
	if (!sc->SCp.ptr) {
		ISCSI_DBG_EH(session, "sc never reached iscsi layer or "
				      "it completed.\n");
		spin_unlock_bh(&session->frwd_lock);
		mutex_unlock(&session->eh_mutex);
		return SUCCESS;
	}

	/*
	 * If we are not logged in or we have started a new session
	 * then let the host reset code handle this
	 */
	if (!session->leadconn || session->state != ISCSI_STATE_LOGGED_IN ||
	    sc->SCp.phase != session->age) {
		spin_unlock_bh(&session->frwd_lock);
		mutex_unlock(&session->eh_mutex);
		ISCSI_DBG_EH(session, "failing abort due to dropped "
				  "session.\n");
		return FAILED;
	}

	spin_lock(&session->back_lock);
	task = (struct iscsi_task *)sc->SCp.ptr;
	if (!task || !task->sc) {
		/* task completed before time out */
		ISCSI_DBG_EH(session, "sc completed while abort in progress\n");

		spin_unlock(&session->back_lock);
		spin_unlock_bh(&session->frwd_lock);
		mutex_unlock(&session->eh_mutex);
		return SUCCESS;
	}

	conn = session->leadconn;
	iscsi_get_conn(conn->cls_conn);
	conn->eh_abort_cnt++;
	age = session->age;

	ISCSI_DBG_EH(session, "aborting [sc %p itt 0x%x]\n", sc, task->itt);
	__iscsi_get_task(task);
	spin_unlock(&session->back_lock);

	if (task->state == ISCSI_TASK_PENDING) {
		fail_scsi_task(task, DID_ABORT);
		goto success;
	}

	/* only have one tmf outstanding at a time */
	if (session->tmf_state != TMF_INITIAL)
		goto failed;
	session->tmf_state = TMF_QUEUED;

	hdr = &session->tmhdr;
	iscsi_prep_abort_task_pdu(task, hdr);

	if (iscsi_exec_task_mgmt_fn(conn, hdr, age, session->abort_timeout))
		goto failed;

	switch (session->tmf_state) {
	case TMF_SUCCESS:
		spin_unlock_bh(&session->frwd_lock);
		/*
		 * stop tx side incase the target had sent a abort rsp but
		 * the initiator was still writing out data.
		 */
		iscsi_suspend_tx(conn);
		/*
		 * we do not stop the recv side because targets have been
		 * good and have never sent us a successful tmf response
		 * then sent more data for the cmd.
		 */
		spin_lock_bh(&session->frwd_lock);
		fail_scsi_task(task, DID_ABORT);
		session->tmf_state = TMF_INITIAL;
		memset(hdr, 0, sizeof(*hdr));
		spin_unlock_bh(&session->frwd_lock);
		iscsi_start_tx(conn);
		goto success_unlocked;
	case TMF_TIMEDOUT:
		session->running_aborted_task = task;
		spin_unlock_bh(&session->frwd_lock);
		iscsi_conn_failure(conn, ISCSI_ERR_SCSI_EH_SESSION_RST);
		goto failed_unlocked;
	case TMF_NOT_FOUND:
		if (iscsi_task_is_completed(task)) {
			session->tmf_state = TMF_INITIAL;
			memset(hdr, 0, sizeof(*hdr));
			/* task completed before tmf abort response */
			ISCSI_DBG_EH(session, "sc completed while abort	in "
					      "progress\n");
			goto success;
		}
		fallthrough;
	default:
		session->tmf_state = TMF_INITIAL;
		goto failed;
	}

success:
	spin_unlock_bh(&session->frwd_lock);
success_unlocked:
	ISCSI_DBG_EH(session, "abort success [sc %p itt 0x%x]\n",
		     sc, task->itt);
	iscsi_put_task(task);
	iscsi_put_conn(conn->cls_conn);
	mutex_unlock(&session->eh_mutex);
	return SUCCESS;

failed:
	spin_unlock_bh(&session->frwd_lock);
failed_unlocked:
	ISCSI_DBG_EH(session, "abort failed [sc %p itt 0x%x]\n", sc,
		     task ? task->itt : 0);
	/*
	 * The driver might be accessing the task so hold the ref. The conn
	 * stop cleanup will drop the ref after ep_disconnect so we know the
	 * driver's no longer touching the task.
	 */
	if (!session->running_aborted_task)
		iscsi_put_task(task);

	iscsi_put_conn(conn->cls_conn);
	mutex_unlock(&session->eh_mutex);
	return FAILED;
}
EXPORT_SYMBOL_GPL(iscsi_eh_abort);

static void iscsi_prep_lun_reset_pdu(struct scsi_cmnd *sc, struct iscsi_tm *hdr)
{
	memset(hdr, 0, sizeof(*hdr));
	hdr->opcode = ISCSI_OP_SCSI_TMFUNC | ISCSI_OP_IMMEDIATE;
	hdr->flags = ISCSI_TM_FUNC_LOGICAL_UNIT_RESET & ISCSI_FLAG_TM_FUNC_MASK;
	hdr->flags |= ISCSI_FLAG_CMD_FINAL;
	int_to_scsilun(sc->device->lun, &hdr->lun);
	hdr->rtt = RESERVED_ITT;
}

int iscsi_eh_device_reset(struct scsi_cmnd *sc)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	struct iscsi_conn *conn;
	struct iscsi_tm *hdr;
	int rc = FAILED;

	cls_session = starget_to_session(scsi_target(sc->device));
	session = cls_session->dd_data;

	ISCSI_DBG_EH(session, "LU Reset [sc %p lun %llu]\n", sc,
		     sc->device->lun);

	mutex_lock(&session->eh_mutex);
	spin_lock_bh(&session->frwd_lock);
	/*
	 * Just check if we are not logged in. We cannot check for
	 * the phase because the reset could come from a ioctl.
	 */
	if (!session->leadconn || session->state != ISCSI_STATE_LOGGED_IN)
		goto unlock;
	conn = session->leadconn;

	/* only have one tmf outstanding at a time */
	if (session->tmf_state != TMF_INITIAL)
		goto unlock;
	session->tmf_state = TMF_QUEUED;

	hdr = &session->tmhdr;
	iscsi_prep_lun_reset_pdu(sc, hdr);

	if (iscsi_exec_task_mgmt_fn(conn, hdr, session->age,
				    session->lu_reset_timeout)) {
		rc = FAILED;
		goto unlock;
	}

	switch (session->tmf_state) {
	case TMF_SUCCESS:
		break;
	case TMF_TIMEDOUT:
		spin_unlock_bh(&session->frwd_lock);
		iscsi_conn_failure(conn, ISCSI_ERR_SCSI_EH_SESSION_RST);
		goto done;
	default:
		session->tmf_state = TMF_INITIAL;
		goto unlock;
	}

	rc = SUCCESS;
	spin_unlock_bh(&session->frwd_lock);

	iscsi_suspend_tx(conn);

	spin_lock_bh(&session->frwd_lock);
	memset(hdr, 0, sizeof(*hdr));
	fail_scsi_tasks(conn, sc->device->lun, DID_ERROR);
	session->tmf_state = TMF_INITIAL;
	spin_unlock_bh(&session->frwd_lock);

	iscsi_start_tx(conn);
	goto done;

unlock:
	spin_unlock_bh(&session->frwd_lock);
done:
	ISCSI_DBG_EH(session, "dev reset result = %s\n",
		     rc == SUCCESS ? "SUCCESS" : "FAILED");
	mutex_unlock(&session->eh_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(iscsi_eh_device_reset);

void iscsi_session_recovery_timedout(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *session = cls_session->dd_data;

	spin_lock_bh(&session->frwd_lock);
	if (session->state != ISCSI_STATE_LOGGED_IN) {
		session->state = ISCSI_STATE_RECOVERY_FAILED;
		wake_up(&session->ehwait);
	}
	spin_unlock_bh(&session->frwd_lock);
}
EXPORT_SYMBOL_GPL(iscsi_session_recovery_timedout);

/**
 * iscsi_eh_session_reset - drop session and attempt relogin
 * @sc: scsi command
 *
 * This function will wait for a relogin, session termination from
 * userspace, or a recovery/replacement timeout.
 */
int iscsi_eh_session_reset(struct scsi_cmnd *sc)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	struct iscsi_conn *conn;

	cls_session = starget_to_session(scsi_target(sc->device));
	session = cls_session->dd_data;

	mutex_lock(&session->eh_mutex);
	spin_lock_bh(&session->frwd_lock);
	if (session->state == ISCSI_STATE_TERMINATE) {
failed:
		ISCSI_DBG_EH(session,
			     "failing session reset: Could not log back into "
			     "%s [age %d]\n", session->targetname,
			     session->age);
		spin_unlock_bh(&session->frwd_lock);
		mutex_unlock(&session->eh_mutex);
		return FAILED;
	}

	conn = session->leadconn;
	iscsi_get_conn(conn->cls_conn);

	spin_unlock_bh(&session->frwd_lock);
	mutex_unlock(&session->eh_mutex);

	iscsi_conn_failure(conn, ISCSI_ERR_SCSI_EH_SESSION_RST);
	iscsi_put_conn(conn->cls_conn);

	ISCSI_DBG_EH(session, "wait for relogin\n");
	wait_event_interruptible(session->ehwait,
				 session->state == ISCSI_STATE_TERMINATE ||
				 session->state == ISCSI_STATE_LOGGED_IN ||
				 session->state == ISCSI_STATE_RECOVERY_FAILED);
	if (signal_pending(current))
		flush_signals(current);

	mutex_lock(&session->eh_mutex);
	spin_lock_bh(&session->frwd_lock);
	if (session->state == ISCSI_STATE_LOGGED_IN) {
		ISCSI_DBG_EH(session,
			     "session reset succeeded for %s,%s\n",
			     session->targetname, conn->persistent_address);
	} else
		goto failed;
	spin_unlock_bh(&session->frwd_lock);
	mutex_unlock(&session->eh_mutex);
	return SUCCESS;
}
EXPORT_SYMBOL_GPL(iscsi_eh_session_reset);

static void iscsi_prep_tgt_reset_pdu(struct scsi_cmnd *sc, struct iscsi_tm *hdr)
{
	memset(hdr, 0, sizeof(*hdr));
	hdr->opcode = ISCSI_OP_SCSI_TMFUNC | ISCSI_OP_IMMEDIATE;
	hdr->flags = ISCSI_TM_FUNC_TARGET_WARM_RESET & ISCSI_FLAG_TM_FUNC_MASK;
	hdr->flags |= ISCSI_FLAG_CMD_FINAL;
	hdr->rtt = RESERVED_ITT;
}

/**
 * iscsi_eh_target_reset - reset target
 * @sc: scsi command
 *
 * This will attempt to send a warm target reset.
 */
static int iscsi_eh_target_reset(struct scsi_cmnd *sc)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	struct iscsi_conn *conn;
	struct iscsi_tm *hdr;
	int rc = FAILED;

	cls_session = starget_to_session(scsi_target(sc->device));
	session = cls_session->dd_data;

	ISCSI_DBG_EH(session, "tgt Reset [sc %p tgt %s]\n", sc,
		     session->targetname);

	mutex_lock(&session->eh_mutex);
	spin_lock_bh(&session->frwd_lock);
	/*
	 * Just check if we are not logged in. We cannot check for
	 * the phase because the reset could come from a ioctl.
	 */
	if (!session->leadconn || session->state != ISCSI_STATE_LOGGED_IN)
		goto unlock;
	conn = session->leadconn;

	/* only have one tmf outstanding at a time */
	if (session->tmf_state != TMF_INITIAL)
		goto unlock;
	session->tmf_state = TMF_QUEUED;

	hdr = &session->tmhdr;
	iscsi_prep_tgt_reset_pdu(sc, hdr);

	if (iscsi_exec_task_mgmt_fn(conn, hdr, session->age,
				    session->tgt_reset_timeout)) {
		rc = FAILED;
		goto unlock;
	}

	switch (session->tmf_state) {
	case TMF_SUCCESS:
		break;
	case TMF_TIMEDOUT:
		spin_unlock_bh(&session->frwd_lock);
		iscsi_conn_failure(conn, ISCSI_ERR_SCSI_EH_SESSION_RST);
		goto done;
	default:
		session->tmf_state = TMF_INITIAL;
		goto unlock;
	}

	rc = SUCCESS;
	spin_unlock_bh(&session->frwd_lock);

	iscsi_suspend_tx(conn);

	spin_lock_bh(&session->frwd_lock);
	memset(hdr, 0, sizeof(*hdr));
	fail_scsi_tasks(conn, -1, DID_ERROR);
	session->tmf_state = TMF_INITIAL;
	spin_unlock_bh(&session->frwd_lock);

	iscsi_start_tx(conn);
	goto done;

unlock:
	spin_unlock_bh(&session->frwd_lock);
done:
	ISCSI_DBG_EH(session, "tgt %s reset result = %s\n", session->targetname,
		     rc == SUCCESS ? "SUCCESS" : "FAILED");
	mutex_unlock(&session->eh_mutex);
	return rc;
}

/**
 * iscsi_eh_recover_target - reset target and possibly the session
 * @sc: scsi command
 *
 * This will attempt to send a warm target reset. If that fails,
 * we will escalate to ERL0 session recovery.
 */
int iscsi_eh_recover_target(struct scsi_cmnd *sc)
{
	int rc;

	rc = iscsi_eh_target_reset(sc);
	if (rc == FAILED)
		rc = iscsi_eh_session_reset(sc);
	return rc;
}
EXPORT_SYMBOL_GPL(iscsi_eh_recover_target);

/*
 * Pre-allocate a pool of @max items of @item_size. By default, the pool
 * should be accessed via kfifo_{get,put} on q->queue.
 * Optionally, the caller can obtain the array of object pointers
 * by passing in a non-NULL @items pointer
 */
int
iscsi_pool_init(struct iscsi_pool *q, int max, void ***items, int item_size)
{
	int i, num_arrays = 1;

	memset(q, 0, sizeof(*q));

	q->max = max;

	/* If the user passed an items pointer, he wants a copy of
	 * the array. */
	if (items)
		num_arrays++;
	q->pool = kvcalloc(num_arrays * max, sizeof(void *), GFP_KERNEL);
	if (q->pool == NULL)
		return -ENOMEM;

	kfifo_init(&q->queue, (void*)q->pool, max * sizeof(void*));

	for (i = 0; i < max; i++) {
		q->pool[i] = kzalloc(item_size, GFP_KERNEL);
		if (q->pool[i] == NULL) {
			q->max = i;
			goto enomem;
		}
		kfifo_in(&q->queue, (void*)&q->pool[i], sizeof(void*));
	}

	if (items) {
		*items = q->pool + max;
		memcpy(*items, q->pool, max * sizeof(void *));
	}

	return 0;

enomem:
	iscsi_pool_free(q);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(iscsi_pool_init);

void iscsi_pool_free(struct iscsi_pool *q)
{
	int i;

	for (i = 0; i < q->max; i++)
		kfree(q->pool[i]);
	kvfree(q->pool);
}
EXPORT_SYMBOL_GPL(iscsi_pool_free);

int iscsi_host_get_max_scsi_cmds(struct Scsi_Host *shost,
				 uint16_t requested_cmds_max)
{
	int scsi_cmds, total_cmds = requested_cmds_max;

check:
	if (!total_cmds)
		total_cmds = ISCSI_DEF_XMIT_CMDS_MAX;
	/*
	 * The iscsi layer needs some tasks for nop handling and tmfs,
	 * so the cmds_max must at least be greater than ISCSI_MGMT_CMDS_MAX
	 * + 1 command for scsi IO.
	 */
	if (total_cmds < ISCSI_TOTAL_CMDS_MIN) {
		printk(KERN_ERR "iscsi: invalid max cmds of %d. Must be a power of two that is at least %d.\n",
		       total_cmds, ISCSI_TOTAL_CMDS_MIN);
		return -EINVAL;
	}

	if (total_cmds > ISCSI_TOTAL_CMDS_MAX) {
		printk(KERN_INFO "iscsi: invalid max cmds of %d. Must be a power of 2 less than or equal to %d. Using %d.\n",
		       requested_cmds_max, ISCSI_TOTAL_CMDS_MAX,
		       ISCSI_TOTAL_CMDS_MAX);
		total_cmds = ISCSI_TOTAL_CMDS_MAX;
	}

	if (!is_power_of_2(total_cmds)) {
		total_cmds = rounddown_pow_of_two(total_cmds);
		if (total_cmds < ISCSI_TOTAL_CMDS_MIN) {
			printk(KERN_ERR "iscsi: invalid max cmds of %d. Must be a power of 2 greater than %d.\n", requested_cmds_max, ISCSI_TOTAL_CMDS_MIN);
			return -EINVAL;
		}

		printk(KERN_INFO "iscsi: invalid max cmds %d. Must be a power of 2. Rounding max cmds down to %d.\n",
		       requested_cmds_max, total_cmds);
	}

	scsi_cmds = total_cmds - ISCSI_MGMT_CMDS_MAX;
	if (shost->can_queue && scsi_cmds > shost->can_queue) {
		total_cmds = shost->can_queue;

		printk(KERN_INFO "iscsi: requested max cmds %u is higher than driver limit. Using driver limit %u\n",
		       requested_cmds_max, shost->can_queue);
		goto check;
	}

	return scsi_cmds;
}
EXPORT_SYMBOL_GPL(iscsi_host_get_max_scsi_cmds);

/**
 * iscsi_host_add - add host to system
 * @shost: scsi host
 * @pdev: parent device
 *
 * This should be called by partial offload and software iscsi drivers
 * to add a host to the system.
 */
int iscsi_host_add(struct Scsi_Host *shost, struct device *pdev)
{
	if (!shost->can_queue)
		shost->can_queue = ISCSI_DEF_XMIT_CMDS_MAX;

	if (!shost->cmd_per_lun)
		shost->cmd_per_lun = ISCSI_DEF_CMD_PER_LUN;

	return scsi_add_host(shost, pdev);
}
EXPORT_SYMBOL_GPL(iscsi_host_add);

/**
 * iscsi_host_alloc - allocate a host and driver data
 * @sht: scsi host template
 * @dd_data_size: driver host data size
 * @xmit_can_sleep: bool indicating if LLD will queue IO from a work queue
 *
 * This should be called by partial offload and software iscsi drivers.
 * To access the driver specific memory use the iscsi_host_priv() macro.
 */
struct Scsi_Host *iscsi_host_alloc(struct scsi_host_template *sht,
				   int dd_data_size, bool xmit_can_sleep)
{
	struct Scsi_Host *shost;
	struct iscsi_host *ihost;

	shost = scsi_host_alloc(sht, sizeof(struct iscsi_host) + dd_data_size);
	if (!shost)
		return NULL;
	ihost = shost_priv(shost);

	if (xmit_can_sleep) {
		snprintf(ihost->workq_name, sizeof(ihost->workq_name),
			"iscsi_q_%d", shost->host_no);
		ihost->workq = alloc_workqueue("%s",
			WQ_SYSFS | __WQ_LEGACY | WQ_MEM_RECLAIM | WQ_UNBOUND,
			1, ihost->workq_name);
		if (!ihost->workq)
			goto free_host;
	}

	spin_lock_init(&ihost->lock);
	ihost->state = ISCSI_HOST_SETUP;
	ihost->num_sessions = 0;
	init_waitqueue_head(&ihost->session_removal_wq);
	return shost;

free_host:
	scsi_host_put(shost);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_host_alloc);

static void iscsi_notify_host_removed(struct iscsi_cls_session *cls_session)
{
	iscsi_session_failure(cls_session->dd_data, ISCSI_ERR_INVALID_HOST);
}

/**
 * iscsi_host_remove - remove host and sessions
 * @shost: scsi host
 *
 * If there are any sessions left, this will initiate the removal and wait
 * for the completion.
 */
void iscsi_host_remove(struct Scsi_Host *shost)
{
	struct iscsi_host *ihost = shost_priv(shost);
	unsigned long flags;

	spin_lock_irqsave(&ihost->lock, flags);
	ihost->state = ISCSI_HOST_REMOVED;
	spin_unlock_irqrestore(&ihost->lock, flags);

	iscsi_host_for_each_session(shost, iscsi_notify_host_removed);
	wait_event_interruptible(ihost->session_removal_wq,
				 ihost->num_sessions == 0);
	if (signal_pending(current))
		flush_signals(current);

	scsi_remove_host(shost);
}
EXPORT_SYMBOL_GPL(iscsi_host_remove);

void iscsi_host_free(struct Scsi_Host *shost)
{
	struct iscsi_host *ihost = shost_priv(shost);

	if (ihost->workq)
		destroy_workqueue(ihost->workq);

	kfree(ihost->netdev);
	kfree(ihost->hwaddress);
	kfree(ihost->initiatorname);
	scsi_host_put(shost);
}
EXPORT_SYMBOL_GPL(iscsi_host_free);

static void iscsi_host_dec_session_cnt(struct Scsi_Host *shost)
{
	struct iscsi_host *ihost = shost_priv(shost);
	unsigned long flags;

	shost = scsi_host_get(shost);
	if (!shost) {
		printk(KERN_ERR "Invalid state. Cannot notify host removal "
		      "of session teardown event because host already "
		      "removed.\n");
		return;
	}

	spin_lock_irqsave(&ihost->lock, flags);
	ihost->num_sessions--;
	if (ihost->num_sessions == 0)
		wake_up(&ihost->session_removal_wq);
	spin_unlock_irqrestore(&ihost->lock, flags);
	scsi_host_put(shost);
}

/**
 * iscsi_session_setup - create iscsi cls session and host and session
 * @iscsit: iscsi transport template
 * @shost: scsi host
 * @cmds_max: session can queue
 * @dd_size: private driver data size, added to session allocation size
 * @cmd_task_size: LLD task private data size
 * @initial_cmdsn: initial CmdSN
 * @id: target ID to add to this session
 *
 * This can be used by software iscsi_transports that allocate
 * a session per scsi host.
 *
 * Callers should set cmds_max to the largest total numer (mgmt + scsi) of
 * tasks they support. The iscsi layer reserves ISCSI_MGMT_CMDS_MAX tasks
 * for nop handling and login/logout requests.
 */
struct iscsi_cls_session *
iscsi_session_setup(struct iscsi_transport *iscsit, struct Scsi_Host *shost,
		    uint16_t cmds_max, int dd_size, int cmd_task_size,
		    uint32_t initial_cmdsn, unsigned int id)
{
	struct iscsi_host *ihost = shost_priv(shost);
	struct iscsi_session *session;
	struct iscsi_cls_session *cls_session;
	int cmd_i, scsi_cmds;
	unsigned long flags;

	spin_lock_irqsave(&ihost->lock, flags);
	if (ihost->state == ISCSI_HOST_REMOVED) {
		spin_unlock_irqrestore(&ihost->lock, flags);
		return NULL;
	}
	ihost->num_sessions++;
	spin_unlock_irqrestore(&ihost->lock, flags);

	scsi_cmds = iscsi_host_get_max_scsi_cmds(shost, cmds_max);
	if (scsi_cmds < 0)
		goto dec_session_count;

	cls_session = iscsi_alloc_session(shost, iscsit,
					  sizeof(struct iscsi_session) +
					  dd_size);
	if (!cls_session)
		goto dec_session_count;
	session = cls_session->dd_data;
	session->cls_session = cls_session;
	session->host = shost;
	session->state = ISCSI_STATE_FREE;
	session->fast_abort = 1;
	session->tgt_reset_timeout = 30;
	session->lu_reset_timeout = 15;
	session->abort_timeout = 10;
	session->scsi_cmds_max = scsi_cmds;
	session->cmds_max = scsi_cmds + ISCSI_MGMT_CMDS_MAX;
	session->queued_cmdsn = session->cmdsn = initial_cmdsn;
	session->exp_cmdsn = initial_cmdsn + 1;
	session->max_cmdsn = initial_cmdsn + 1;
	session->max_r2t = 1;
	session->tt = iscsit;
	session->dd_data = cls_session->dd_data + sizeof(*session);

	session->tmf_state = TMF_INITIAL;
	timer_setup(&session->tmf_timer, iscsi_tmf_timedout, 0);
	mutex_init(&session->eh_mutex);
	init_waitqueue_head(&session->ehwait);

	spin_lock_init(&session->frwd_lock);
	spin_lock_init(&session->back_lock);

	/* initialize SCSI PDU commands pool */
	if (iscsi_pool_init(&session->cmdpool, session->cmds_max,
			    (void***)&session->cmds,
			    cmd_task_size + sizeof(struct iscsi_task)))
		goto cmdpool_alloc_fail;

	/* pre-format cmds pool with ITT */
	for (cmd_i = 0; cmd_i < session->cmds_max; cmd_i++) {
		struct iscsi_task *task = session->cmds[cmd_i];

		if (cmd_task_size)
			task->dd_data = &task[1];
		task->itt = cmd_i;
		task->state = ISCSI_TASK_FREE;
		INIT_LIST_HEAD(&task->running);
	}

	if (!try_module_get(iscsit->owner))
		goto module_get_fail;

	if (iscsi_add_session(cls_session, id))
		goto cls_session_fail;

	return cls_session;

cls_session_fail:
	module_put(iscsit->owner);
module_get_fail:
	iscsi_pool_free(&session->cmdpool);
cmdpool_alloc_fail:
	iscsi_free_session(cls_session);
dec_session_count:
	iscsi_host_dec_session_cnt(shost);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_session_setup);

/**
 * iscsi_session_teardown - destroy session, host, and cls_session
 * @cls_session: iscsi session
 */
void iscsi_session_teardown(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *session = cls_session->dd_data;
	struct module *owner = cls_session->transport->owner;
	struct Scsi_Host *shost = session->host;

	iscsi_remove_session(cls_session);

	iscsi_pool_free(&session->cmdpool);
	kfree(session->password);
	kfree(session->password_in);
	kfree(session->username);
	kfree(session->username_in);
	kfree(session->targetname);
	kfree(session->targetalias);
	kfree(session->initiatorname);
	kfree(session->boot_root);
	kfree(session->boot_nic);
	kfree(session->boot_target);
	kfree(session->ifacename);
	kfree(session->portal_type);
	kfree(session->discovery_parent_type);

	iscsi_free_session(cls_session);

	iscsi_host_dec_session_cnt(shost);
	module_put(owner);
}
EXPORT_SYMBOL_GPL(iscsi_session_teardown);

/**
 * iscsi_conn_setup - create iscsi_cls_conn and iscsi_conn
 * @cls_session: iscsi_cls_session
 * @dd_size: private driver data size
 * @conn_idx: cid
 */
struct iscsi_cls_conn *
iscsi_conn_setup(struct iscsi_cls_session *cls_session, int dd_size,
		 uint32_t conn_idx)
{
	struct iscsi_session *session = cls_session->dd_data;
	struct iscsi_conn *conn;
	struct iscsi_cls_conn *cls_conn;
	char *data;

	cls_conn = iscsi_create_conn(cls_session, sizeof(*conn) + dd_size,
				     conn_idx);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;
	memset(conn, 0, sizeof(*conn) + dd_size);

	conn->dd_data = cls_conn->dd_data + sizeof(*conn);
	conn->session = session;
	conn->cls_conn = cls_conn;
	conn->c_stage = ISCSI_CONN_INITIAL_STAGE;
	conn->id = conn_idx;
	conn->exp_statsn = 0;

	timer_setup(&conn->transport_timer, iscsi_check_transport_timeouts, 0);

	INIT_LIST_HEAD(&conn->mgmtqueue);
	INIT_LIST_HEAD(&conn->cmdqueue);
	INIT_LIST_HEAD(&conn->requeue);
	INIT_WORK(&conn->xmitwork, iscsi_xmitworker);

	/* allocate login_task used for the login/text sequences */
	spin_lock_bh(&session->frwd_lock);
	if (!kfifo_out(&session->cmdpool.queue,
                         (void*)&conn->login_task,
			 sizeof(void*))) {
		spin_unlock_bh(&session->frwd_lock);
		goto login_task_alloc_fail;
	}
	spin_unlock_bh(&session->frwd_lock);

	data = (char *) __get_free_pages(GFP_KERNEL,
					 get_order(ISCSI_DEF_MAX_RECV_SEG_LEN));
	if (!data)
		goto login_task_data_alloc_fail;
	conn->login_task->data = conn->data = data;

	return cls_conn;

login_task_data_alloc_fail:
	kfifo_in(&session->cmdpool.queue, (void*)&conn->login_task,
		    sizeof(void*));
login_task_alloc_fail:
	iscsi_destroy_conn(cls_conn);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_conn_setup);

/**
 * iscsi_conn_teardown - teardown iscsi connection
 * @cls_conn: iscsi class connection
 *
 * TODO: we may need to make this into a two step process
 * like scsi-mls remove + put host
 */
void iscsi_conn_teardown(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	char *tmp_persistent_address = conn->persistent_address;
	char *tmp_local_ipaddr = conn->local_ipaddr;

	del_timer_sync(&conn->transport_timer);

	mutex_lock(&session->eh_mutex);
	spin_lock_bh(&session->frwd_lock);
	conn->c_stage = ISCSI_CONN_CLEANUP_WAIT;
	if (session->leadconn == conn) {
		/*
		 * leading connection? then give up on recovery.
		 */
		session->state = ISCSI_STATE_TERMINATE;
		wake_up(&session->ehwait);
	}
	spin_unlock_bh(&session->frwd_lock);

	/* flush queued up work because we free the connection below */
	iscsi_suspend_tx(conn);

	spin_lock_bh(&session->frwd_lock);
	free_pages((unsigned long) conn->data,
		   get_order(ISCSI_DEF_MAX_RECV_SEG_LEN));
	/* regular RX path uses back_lock */
	spin_lock_bh(&session->back_lock);
	kfifo_in(&session->cmdpool.queue, (void*)&conn->login_task,
		    sizeof(void*));
	spin_unlock_bh(&session->back_lock);
	if (session->leadconn == conn)
		session->leadconn = NULL;
	spin_unlock_bh(&session->frwd_lock);
	mutex_unlock(&session->eh_mutex);

	iscsi_destroy_conn(cls_conn);
	kfree(tmp_persistent_address);
	kfree(tmp_local_ipaddr);
}
EXPORT_SYMBOL_GPL(iscsi_conn_teardown);

int iscsi_conn_start(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;

	if (!session) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "can't start unbound connection\n");
		return -EPERM;
	}

	if ((session->imm_data_en || !session->initial_r2t_en) &&
	     session->first_burst > session->max_burst) {
		iscsi_conn_printk(KERN_INFO, conn, "invalid burst lengths: "
				  "first_burst %d max_burst %d\n",
				  session->first_burst, session->max_burst);
		return -EINVAL;
	}

	if (conn->ping_timeout && !conn->recv_timeout) {
		iscsi_conn_printk(KERN_ERR, conn, "invalid recv timeout of "
				  "zero. Using 5 seconds\n.");
		conn->recv_timeout = 5;
	}

	if (conn->recv_timeout && !conn->ping_timeout) {
		iscsi_conn_printk(KERN_ERR, conn, "invalid ping timeout of "
				  "zero. Using 5 seconds.\n");
		conn->ping_timeout = 5;
	}

	spin_lock_bh(&session->frwd_lock);
	conn->c_stage = ISCSI_CONN_STARTED;
	session->state = ISCSI_STATE_LOGGED_IN;
	session->queued_cmdsn = session->cmdsn;

	conn->last_recv = jiffies;
	conn->last_ping = jiffies;
	if (conn->recv_timeout && conn->ping_timeout)
		mod_timer(&conn->transport_timer,
			  jiffies + (conn->recv_timeout * HZ));

	switch(conn->stop_stage) {
	case STOP_CONN_RECOVER:
		/*
		 * unblock eh_abort() if it is blocked. re-try all
		 * commands after successful recovery
		 */
		conn->stop_stage = 0;
		session->tmf_state = TMF_INITIAL;
		session->age++;
		if (session->age == 16)
			session->age = 0;
		break;
	case STOP_CONN_TERM:
		conn->stop_stage = 0;
		break;
	default:
		break;
	}
	spin_unlock_bh(&session->frwd_lock);

	iscsi_unblock_session(session->cls_session);
	wake_up(&session->ehwait);
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_conn_start);

static void
fail_mgmt_tasks(struct iscsi_session *session, struct iscsi_conn *conn)
{
	struct iscsi_task *task;
	int i, state;

	for (i = 0; i < conn->session->cmds_max; i++) {
		task = conn->session->cmds[i];
		if (task->sc)
			continue;

		if (task->state == ISCSI_TASK_FREE)
			continue;

		ISCSI_DBG_SESSION(conn->session,
				  "failing mgmt itt 0x%x state %d\n",
				  task->itt, task->state);

		spin_lock_bh(&session->back_lock);
		if (cleanup_queued_task(task)) {
			spin_unlock_bh(&session->back_lock);
			continue;
		}

		state = ISCSI_TASK_ABRT_SESS_RECOV;
		if (task->state == ISCSI_TASK_PENDING)
			state = ISCSI_TASK_COMPLETED;
		iscsi_complete_task(task, state);
		spin_unlock_bh(&session->back_lock);
	}
}

void iscsi_conn_stop(struct iscsi_cls_conn *cls_conn, int flag)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	int old_stop_stage;

	mutex_lock(&session->eh_mutex);
	spin_lock_bh(&session->frwd_lock);
	if (conn->stop_stage == STOP_CONN_TERM) {
		spin_unlock_bh(&session->frwd_lock);
		mutex_unlock(&session->eh_mutex);
		return;
	}

	/*
	 * When this is called for the in_login state, we only want to clean
	 * up the login task and connection. We do not need to block and set
	 * the recovery state again
	 */
	if (flag == STOP_CONN_TERM)
		session->state = ISCSI_STATE_TERMINATE;
	else if (conn->stop_stage != STOP_CONN_RECOVER)
		session->state = ISCSI_STATE_IN_RECOVERY;

	old_stop_stage = conn->stop_stage;
	conn->stop_stage = flag;
	spin_unlock_bh(&session->frwd_lock);

	del_timer_sync(&conn->transport_timer);
	iscsi_suspend_tx(conn);

	spin_lock_bh(&session->frwd_lock);
	conn->c_stage = ISCSI_CONN_STOPPED;
	spin_unlock_bh(&session->frwd_lock);

	/*
	 * for connection level recovery we should not calculate
	 * header digest. conn->hdr_size used for optimization
	 * in hdr_extract() and will be re-negotiated at
	 * set_param() time.
	 */
	if (flag == STOP_CONN_RECOVER) {
		conn->hdrdgst_en = 0;
		conn->datadgst_en = 0;
		if (session->state == ISCSI_STATE_IN_RECOVERY &&
		    old_stop_stage != STOP_CONN_RECOVER) {
			ISCSI_DBG_SESSION(session, "blocking session\n");
			iscsi_block_session(session->cls_session);
		}
	}

	/*
	 * flush queues.
	 */
	spin_lock_bh(&session->frwd_lock);
	fail_scsi_tasks(conn, -1, DID_TRANSPORT_DISRUPTED);
	fail_mgmt_tasks(session, conn);
	memset(&session->tmhdr, 0, sizeof(session->tmhdr));
	spin_unlock_bh(&session->frwd_lock);
	mutex_unlock(&session->eh_mutex);
}
EXPORT_SYMBOL_GPL(iscsi_conn_stop);

int iscsi_conn_bind(struct iscsi_cls_session *cls_session,
		    struct iscsi_cls_conn *cls_conn, int is_leading)
{
	struct iscsi_session *session = cls_session->dd_data;
	struct iscsi_conn *conn = cls_conn->dd_data;

	spin_lock_bh(&session->frwd_lock);
	if (is_leading)
		session->leadconn = conn;
	spin_unlock_bh(&session->frwd_lock);

	/*
	 * The target could have reduced it's window size between logins, so
	 * we have to reset max/exp cmdsn so we can see the new values.
	 */
	spin_lock_bh(&session->back_lock);
	session->max_cmdsn = session->exp_cmdsn = session->cmdsn + 1;
	spin_unlock_bh(&session->back_lock);
	/*
	 * Unblock xmitworker(), Login Phase will pass through.
	 */
	clear_bit(ISCSI_SUSPEND_BIT, &conn->suspend_rx);
	clear_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_conn_bind);

int iscsi_switch_str_param(char **param, char *new_val_buf)
{
	char *new_val;

	if (*param) {
		if (!strcmp(*param, new_val_buf))
			return 0;
	}

	new_val = kstrdup(new_val_buf, GFP_NOIO);
	if (!new_val)
		return -ENOMEM;

	kfree(*param);
	*param = new_val;
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_switch_str_param);

int iscsi_set_param(struct iscsi_cls_conn *cls_conn,
		    enum iscsi_param param, char *buf, int buflen)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	int val;

	switch(param) {
	case ISCSI_PARAM_FAST_ABORT:
		sscanf(buf, "%d", &session->fast_abort);
		break;
	case ISCSI_PARAM_ABORT_TMO:
		sscanf(buf, "%d", &session->abort_timeout);
		break;
	case ISCSI_PARAM_LU_RESET_TMO:
		sscanf(buf, "%d", &session->lu_reset_timeout);
		break;
	case ISCSI_PARAM_TGT_RESET_TMO:
		sscanf(buf, "%d", &session->tgt_reset_timeout);
		break;
	case ISCSI_PARAM_PING_TMO:
		sscanf(buf, "%d", &conn->ping_timeout);
		break;
	case ISCSI_PARAM_RECV_TMO:
		sscanf(buf, "%d", &conn->recv_timeout);
		break;
	case ISCSI_PARAM_MAX_RECV_DLENGTH:
		sscanf(buf, "%d", &conn->max_recv_dlength);
		break;
	case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		sscanf(buf, "%d", &conn->max_xmit_dlength);
		break;
	case ISCSI_PARAM_HDRDGST_EN:
		sscanf(buf, "%d", &conn->hdrdgst_en);
		break;
	case ISCSI_PARAM_DATADGST_EN:
		sscanf(buf, "%d", &conn->datadgst_en);
		break;
	case ISCSI_PARAM_INITIAL_R2T_EN:
		sscanf(buf, "%d", &session->initial_r2t_en);
		break;
	case ISCSI_PARAM_MAX_R2T:
		sscanf(buf, "%hu", &session->max_r2t);
		break;
	case ISCSI_PARAM_IMM_DATA_EN:
		sscanf(buf, "%d", &session->imm_data_en);
		break;
	case ISCSI_PARAM_FIRST_BURST:
		sscanf(buf, "%d", &session->first_burst);
		break;
	case ISCSI_PARAM_MAX_BURST:
		sscanf(buf, "%d", &session->max_burst);
		break;
	case ISCSI_PARAM_PDU_INORDER_EN:
		sscanf(buf, "%d", &session->pdu_inorder_en);
		break;
	case ISCSI_PARAM_DATASEQ_INORDER_EN:
		sscanf(buf, "%d", &session->dataseq_inorder_en);
		break;
	case ISCSI_PARAM_ERL:
		sscanf(buf, "%d", &session->erl);
		break;
	case ISCSI_PARAM_EXP_STATSN:
		sscanf(buf, "%u", &conn->exp_statsn);
		break;
	case ISCSI_PARAM_USERNAME:
		return iscsi_switch_str_param(&session->username, buf);
	case ISCSI_PARAM_USERNAME_IN:
		return iscsi_switch_str_param(&session->username_in, buf);
	case ISCSI_PARAM_PASSWORD:
		return iscsi_switch_str_param(&session->password, buf);
	case ISCSI_PARAM_PASSWORD_IN:
		return iscsi_switch_str_param(&session->password_in, buf);
	case ISCSI_PARAM_TARGET_NAME:
		return iscsi_switch_str_param(&session->targetname, buf);
	case ISCSI_PARAM_TARGET_ALIAS:
		return iscsi_switch_str_param(&session->targetalias, buf);
	case ISCSI_PARAM_TPGT:
		sscanf(buf, "%d", &session->tpgt);
		break;
	case ISCSI_PARAM_PERSISTENT_PORT:
		sscanf(buf, "%d", &conn->persistent_port);
		break;
	case ISCSI_PARAM_PERSISTENT_ADDRESS:
		return iscsi_switch_str_param(&conn->persistent_address, buf);
	case ISCSI_PARAM_IFACE_NAME:
		return iscsi_switch_str_param(&session->ifacename, buf);
	case ISCSI_PARAM_INITIATOR_NAME:
		return iscsi_switch_str_param(&session->initiatorname, buf);
	case ISCSI_PARAM_BOOT_ROOT:
		return iscsi_switch_str_param(&session->boot_root, buf);
	case ISCSI_PARAM_BOOT_NIC:
		return iscsi_switch_str_param(&session->boot_nic, buf);
	case ISCSI_PARAM_BOOT_TARGET:
		return iscsi_switch_str_param(&session->boot_target, buf);
	case ISCSI_PARAM_PORTAL_TYPE:
		return iscsi_switch_str_param(&session->portal_type, buf);
	case ISCSI_PARAM_DISCOVERY_PARENT_TYPE:
		return iscsi_switch_str_param(&session->discovery_parent_type,
					      buf);
	case ISCSI_PARAM_DISCOVERY_SESS:
		sscanf(buf, "%d", &val);
		session->discovery_sess = !!val;
		break;
	case ISCSI_PARAM_LOCAL_IPADDR:
		return iscsi_switch_str_param(&conn->local_ipaddr, buf);
	default:
		return -ENOSYS;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_set_param);

int iscsi_session_get_param(struct iscsi_cls_session *cls_session,
			    enum iscsi_param param, char *buf)
{
	struct iscsi_session *session = cls_session->dd_data;
	int len;

	switch(param) {
	case ISCSI_PARAM_FAST_ABORT:
		len = sysfs_emit(buf, "%d\n", session->fast_abort);
		break;
	case ISCSI_PARAM_ABORT_TMO:
		len = sysfs_emit(buf, "%d\n", session->abort_timeout);
		break;
	case ISCSI_PARAM_LU_RESET_TMO:
		len = sysfs_emit(buf, "%d\n", session->lu_reset_timeout);
		break;
	case ISCSI_PARAM_TGT_RESET_TMO:
		len = sysfs_emit(buf, "%d\n", session->tgt_reset_timeout);
		break;
	case ISCSI_PARAM_INITIAL_R2T_EN:
		len = sysfs_emit(buf, "%d\n", session->initial_r2t_en);
		break;
	case ISCSI_PARAM_MAX_R2T:
		len = sysfs_emit(buf, "%hu\n", session->max_r2t);
		break;
	case ISCSI_PARAM_IMM_DATA_EN:
		len = sysfs_emit(buf, "%d\n", session->imm_data_en);
		break;
	case ISCSI_PARAM_FIRST_BURST:
		len = sysfs_emit(buf, "%u\n", session->first_burst);
		break;
	case ISCSI_PARAM_MAX_BURST:
		len = sysfs_emit(buf, "%u\n", session->max_burst);
		break;
	case ISCSI_PARAM_PDU_INORDER_EN:
		len = sysfs_emit(buf, "%d\n", session->pdu_inorder_en);
		break;
	case ISCSI_PARAM_DATASEQ_INORDER_EN:
		len = sysfs_emit(buf, "%d\n", session->dataseq_inorder_en);
		break;
	case ISCSI_PARAM_DEF_TASKMGMT_TMO:
		len = sysfs_emit(buf, "%d\n", session->def_taskmgmt_tmo);
		break;
	case ISCSI_PARAM_ERL:
		len = sysfs_emit(buf, "%d\n", session->erl);
		break;
	case ISCSI_PARAM_TARGET_NAME:
		len = sysfs_emit(buf, "%s\n", session->targetname);
		break;
	case ISCSI_PARAM_TARGET_ALIAS:
		len = sysfs_emit(buf, "%s\n", session->targetalias);
		break;
	case ISCSI_PARAM_TPGT:
		len = sysfs_emit(buf, "%d\n", session->tpgt);
		break;
	case ISCSI_PARAM_USERNAME:
		len = sysfs_emit(buf, "%s\n", session->username);
		break;
	case ISCSI_PARAM_USERNAME_IN:
		len = sysfs_emit(buf, "%s\n", session->username_in);
		break;
	case ISCSI_PARAM_PASSWORD:
		len = sysfs_emit(buf, "%s\n", session->password);
		break;
	case ISCSI_PARAM_PASSWORD_IN:
		len = sysfs_emit(buf, "%s\n", session->password_in);
		break;
	case ISCSI_PARAM_IFACE_NAME:
		len = sysfs_emit(buf, "%s\n", session->ifacename);
		break;
	case ISCSI_PARAM_INITIATOR_NAME:
		len = sysfs_emit(buf, "%s\n", session->initiatorname);
		break;
	case ISCSI_PARAM_BOOT_ROOT:
		len = sysfs_emit(buf, "%s\n", session->boot_root);
		break;
	case ISCSI_PARAM_BOOT_NIC:
		len = sysfs_emit(buf, "%s\n", session->boot_nic);
		break;
	case ISCSI_PARAM_BOOT_TARGET:
		len = sysfs_emit(buf, "%s\n", session->boot_target);
		break;
	case ISCSI_PARAM_AUTO_SND_TGT_DISABLE:
		len = sysfs_emit(buf, "%u\n", session->auto_snd_tgt_disable);
		break;
	case ISCSI_PARAM_DISCOVERY_SESS:
		len = sysfs_emit(buf, "%u\n", session->discovery_sess);
		break;
	case ISCSI_PARAM_PORTAL_TYPE:
		len = sysfs_emit(buf, "%s\n", session->portal_type);
		break;
	case ISCSI_PARAM_CHAP_AUTH_EN:
		len = sysfs_emit(buf, "%u\n", session->chap_auth_en);
		break;
	case ISCSI_PARAM_DISCOVERY_LOGOUT_EN:
		len = sysfs_emit(buf, "%u\n", session->discovery_logout_en);
		break;
	case ISCSI_PARAM_BIDI_CHAP_EN:
		len = sysfs_emit(buf, "%u\n", session->bidi_chap_en);
		break;
	case ISCSI_PARAM_DISCOVERY_AUTH_OPTIONAL:
		len = sysfs_emit(buf, "%u\n", session->discovery_auth_optional);
		break;
	case ISCSI_PARAM_DEF_TIME2WAIT:
		len = sysfs_emit(buf, "%d\n", session->time2wait);
		break;
	case ISCSI_PARAM_DEF_TIME2RETAIN:
		len = sysfs_emit(buf, "%d\n", session->time2retain);
		break;
	case ISCSI_PARAM_TSID:
		len = sysfs_emit(buf, "%u\n", session->tsid);
		break;
	case ISCSI_PARAM_ISID:
		len = sysfs_emit(buf, "%02x%02x%02x%02x%02x%02x\n",
			      session->isid[0], session->isid[1],
			      session->isid[2], session->isid[3],
			      session->isid[4], session->isid[5]);
		break;
	case ISCSI_PARAM_DISCOVERY_PARENT_IDX:
		len = sysfs_emit(buf, "%u\n", session->discovery_parent_idx);
		break;
	case ISCSI_PARAM_DISCOVERY_PARENT_TYPE:
		if (session->discovery_parent_type)
			len = sysfs_emit(buf, "%s\n",
				      session->discovery_parent_type);
		else
			len = sysfs_emit(buf, "\n");
		break;
	default:
		return -ENOSYS;
	}

	return len;
}
EXPORT_SYMBOL_GPL(iscsi_session_get_param);

int iscsi_conn_get_addr_param(struct sockaddr_storage *addr,
			      enum iscsi_param param, char *buf)
{
	struct sockaddr_in6 *sin6 = NULL;
	struct sockaddr_in *sin = NULL;
	int len;

	switch (addr->ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)addr;
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)addr;
		break;
	default:
		return -EINVAL;
	}

	switch (param) {
	case ISCSI_PARAM_CONN_ADDRESS:
	case ISCSI_HOST_PARAM_IPADDRESS:
		if (sin)
			len = sysfs_emit(buf, "%pI4\n", &sin->sin_addr.s_addr);
		else
			len = sysfs_emit(buf, "%pI6\n", &sin6->sin6_addr);
		break;
	case ISCSI_PARAM_CONN_PORT:
	case ISCSI_PARAM_LOCAL_PORT:
		if (sin)
			len = sysfs_emit(buf, "%hu\n", be16_to_cpu(sin->sin_port));
		else
			len = sysfs_emit(buf, "%hu\n",
				      be16_to_cpu(sin6->sin6_port));
		break;
	default:
		return -EINVAL;
	}

	return len;
}
EXPORT_SYMBOL_GPL(iscsi_conn_get_addr_param);

int iscsi_conn_get_param(struct iscsi_cls_conn *cls_conn,
			 enum iscsi_param param, char *buf)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	int len;

	switch(param) {
	case ISCSI_PARAM_PING_TMO:
		len = sysfs_emit(buf, "%u\n", conn->ping_timeout);
		break;
	case ISCSI_PARAM_RECV_TMO:
		len = sysfs_emit(buf, "%u\n", conn->recv_timeout);
		break;
	case ISCSI_PARAM_MAX_RECV_DLENGTH:
		len = sysfs_emit(buf, "%u\n", conn->max_recv_dlength);
		break;
	case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		len = sysfs_emit(buf, "%u\n", conn->max_xmit_dlength);
		break;
	case ISCSI_PARAM_HDRDGST_EN:
		len = sysfs_emit(buf, "%d\n", conn->hdrdgst_en);
		break;
	case ISCSI_PARAM_DATADGST_EN:
		len = sysfs_emit(buf, "%d\n", conn->datadgst_en);
		break;
	case ISCSI_PARAM_IFMARKER_EN:
		len = sysfs_emit(buf, "%d\n", conn->ifmarker_en);
		break;
	case ISCSI_PARAM_OFMARKER_EN:
		len = sysfs_emit(buf, "%d\n", conn->ofmarker_en);
		break;
	case ISCSI_PARAM_EXP_STATSN:
		len = sysfs_emit(buf, "%u\n", conn->exp_statsn);
		break;
	case ISCSI_PARAM_PERSISTENT_PORT:
		len = sysfs_emit(buf, "%d\n", conn->persistent_port);
		break;
	case ISCSI_PARAM_PERSISTENT_ADDRESS:
		len = sysfs_emit(buf, "%s\n", conn->persistent_address);
		break;
	case ISCSI_PARAM_STATSN:
		len = sysfs_emit(buf, "%u\n", conn->statsn);
		break;
	case ISCSI_PARAM_MAX_SEGMENT_SIZE:
		len = sysfs_emit(buf, "%u\n", conn->max_segment_size);
		break;
	case ISCSI_PARAM_KEEPALIVE_TMO:
		len = sysfs_emit(buf, "%u\n", conn->keepalive_tmo);
		break;
	case ISCSI_PARAM_LOCAL_PORT:
		len = sysfs_emit(buf, "%u\n", conn->local_port);
		break;
	case ISCSI_PARAM_TCP_TIMESTAMP_STAT:
		len = sysfs_emit(buf, "%u\n", conn->tcp_timestamp_stat);
		break;
	case ISCSI_PARAM_TCP_NAGLE_DISABLE:
		len = sysfs_emit(buf, "%u\n", conn->tcp_nagle_disable);
		break;
	case ISCSI_PARAM_TCP_WSF_DISABLE:
		len = sysfs_emit(buf, "%u\n", conn->tcp_wsf_disable);
		break;
	case ISCSI_PARAM_TCP_TIMER_SCALE:
		len = sysfs_emit(buf, "%u\n", conn->tcp_timer_scale);
		break;
	case ISCSI_PARAM_TCP_TIMESTAMP_EN:
		len = sysfs_emit(buf, "%u\n", conn->tcp_timestamp_en);
		break;
	case ISCSI_PARAM_IP_FRAGMENT_DISABLE:
		len = sysfs_emit(buf, "%u\n", conn->fragment_disable);
		break;
	case ISCSI_PARAM_IPV4_TOS:
		len = sysfs_emit(buf, "%u\n", conn->ipv4_tos);
		break;
	case ISCSI_PARAM_IPV6_TC:
		len = sysfs_emit(buf, "%u\n", conn->ipv6_traffic_class);
		break;
	case ISCSI_PARAM_IPV6_FLOW_LABEL:
		len = sysfs_emit(buf, "%u\n", conn->ipv6_flow_label);
		break;
	case ISCSI_PARAM_IS_FW_ASSIGNED_IPV6:
		len = sysfs_emit(buf, "%u\n", conn->is_fw_assigned_ipv6);
		break;
	case ISCSI_PARAM_TCP_XMIT_WSF:
		len = sysfs_emit(buf, "%u\n", conn->tcp_xmit_wsf);
		break;
	case ISCSI_PARAM_TCP_RECV_WSF:
		len = sysfs_emit(buf, "%u\n", conn->tcp_recv_wsf);
		break;
	case ISCSI_PARAM_LOCAL_IPADDR:
		len = sysfs_emit(buf, "%s\n", conn->local_ipaddr);
		break;
	default:
		return -ENOSYS;
	}

	return len;
}
EXPORT_SYMBOL_GPL(iscsi_conn_get_param);

int iscsi_host_get_param(struct Scsi_Host *shost, enum iscsi_host_param param,
			 char *buf)
{
	struct iscsi_host *ihost = shost_priv(shost);
	int len;

	switch (param) {
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		len = sysfs_emit(buf, "%s\n", ihost->netdev);
		break;
	case ISCSI_HOST_PARAM_HWADDRESS:
		len = sysfs_emit(buf, "%s\n", ihost->hwaddress);
		break;
	case ISCSI_HOST_PARAM_INITIATOR_NAME:
		len = sysfs_emit(buf, "%s\n", ihost->initiatorname);
		break;
	default:
		return -ENOSYS;
	}

	return len;
}
EXPORT_SYMBOL_GPL(iscsi_host_get_param);

int iscsi_host_set_param(struct Scsi_Host *shost, enum iscsi_host_param param,
			 char *buf, int buflen)
{
	struct iscsi_host *ihost = shost_priv(shost);

	switch (param) {
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		return iscsi_switch_str_param(&ihost->netdev, buf);
	case ISCSI_HOST_PARAM_HWADDRESS:
		return iscsi_switch_str_param(&ihost->hwaddress, buf);
	case ISCSI_HOST_PARAM_INITIATOR_NAME:
		return iscsi_switch_str_param(&ihost->initiatorname, buf);
	default:
		return -ENOSYS;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_host_set_param);

MODULE_AUTHOR("Mike Christie");
MODULE_DESCRIPTION("iSCSI library functions");
MODULE_LICENSE("GPL");
