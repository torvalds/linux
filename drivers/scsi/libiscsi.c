/*
 * iSCSI lib functions
 *
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2004 - 2006 Mike Christie
 * Copyright (C) 2004 - 2005 Dmitry Yusupov
 * Copyright (C) 2004 - 2005 Alex Aizman
 * maintained by open-iscsi@googlegroups.com
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/types.h>
#include <linux/kfifo.h>
#include <linux/delay.h>
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

struct iscsi_session *
class_to_transport_session(struct iscsi_cls_session *cls_session)
{
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);
	return iscsi_hostdata(shost->hostdata);
}
EXPORT_SYMBOL_GPL(class_to_transport_session);

/* Serial Number Arithmetic, 32 bits, less than, RFC1982 */
#define SNA32_CHECK 2147483648UL

static int iscsi_sna_lt(u32 n1, u32 n2)
{
	return n1 != n2 && ((n1 < n2 && (n2 - n1 < SNA32_CHECK)) ||
			    (n1 > n2 && (n2 - n1 < SNA32_CHECK)));
}

/* Serial Number Arithmetic, 32 bits, less than, RFC1982 */
static int iscsi_sna_lte(u32 n1, u32 n2)
{
	return n1 == n2 || ((n1 < n2 && (n2 - n1 < SNA32_CHECK)) ||
			    (n1 > n2 && (n2 - n1 < SNA32_CHECK)));
}

void
iscsi_update_cmdsn(struct iscsi_session *session, struct iscsi_nopin *hdr)
{
	uint32_t max_cmdsn = be32_to_cpu(hdr->max_cmdsn);
	uint32_t exp_cmdsn = be32_to_cpu(hdr->exp_cmdsn);

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
	    !iscsi_sna_lt(max_cmdsn, session->max_cmdsn)) {
		session->max_cmdsn = max_cmdsn;
		/*
		 * if the window closed with IO queued, then kick the
		 * xmit thread
		 */
		if (!list_empty(&session->leadconn->xmitqueue) ||
		    __kfifo_len(session->leadconn->mgmtqueue))
			scsi_queue_work(session->host,
					&session->leadconn->xmitwork);
	}
}
EXPORT_SYMBOL_GPL(iscsi_update_cmdsn);

void iscsi_prep_unsolicit_data_pdu(struct iscsi_cmd_task *ctask,
				   struct iscsi_data *hdr)
{
	struct iscsi_conn *conn = ctask->conn;

	memset(hdr, 0, sizeof(struct iscsi_data));
	hdr->ttt = cpu_to_be32(ISCSI_RESERVED_TAG);
	hdr->datasn = cpu_to_be32(ctask->unsol_datasn);
	ctask->unsol_datasn++;
	hdr->opcode = ISCSI_OP_SCSI_DATA_OUT;
	memcpy(hdr->lun, ctask->hdr->lun, sizeof(hdr->lun));

	hdr->itt = ctask->hdr->itt;
	hdr->exp_statsn = cpu_to_be32(conn->exp_statsn);
	hdr->offset = cpu_to_be32(ctask->unsol_offset);

	if (ctask->unsol_count > conn->max_xmit_dlength) {
		hton24(hdr->dlength, conn->max_xmit_dlength);
		ctask->data_count = conn->max_xmit_dlength;
		ctask->unsol_offset += ctask->data_count;
		hdr->flags = 0;
	} else {
		hton24(hdr->dlength, ctask->unsol_count);
		ctask->data_count = ctask->unsol_count;
		hdr->flags = ISCSI_FLAG_CMD_FINAL;
	}
}
EXPORT_SYMBOL_GPL(iscsi_prep_unsolicit_data_pdu);

/**
 * iscsi_prep_scsi_cmd_pdu - prep iscsi scsi cmd pdu
 * @ctask: iscsi cmd task
 *
 * Prep basic iSCSI PDU fields for a scsi cmd pdu. The LLD should set
 * fields like dlength or final based on how much data it sends
 */
static void iscsi_prep_scsi_cmd_pdu(struct iscsi_cmd_task *ctask)
{
	struct iscsi_conn *conn = ctask->conn;
	struct iscsi_session *session = conn->session;
	struct iscsi_cmd *hdr = ctask->hdr;
	struct scsi_cmnd *sc = ctask->sc;

        hdr->opcode = ISCSI_OP_SCSI_CMD;
        hdr->flags = ISCSI_ATTR_SIMPLE;
        int_to_scsilun(sc->device->lun, (struct scsi_lun *)hdr->lun);
        hdr->itt = build_itt(ctask->itt, conn->id, session->age);
        hdr->data_length = cpu_to_be32(scsi_bufflen(sc));
        hdr->cmdsn = cpu_to_be32(session->cmdsn);
        session->cmdsn++;
        hdr->exp_statsn = cpu_to_be32(conn->exp_statsn);
        memcpy(hdr->cdb, sc->cmnd, sc->cmd_len);
	if (sc->cmd_len < MAX_COMMAND_SIZE)
		memset(&hdr->cdb[sc->cmd_len], 0,
			MAX_COMMAND_SIZE - sc->cmd_len);

	ctask->data_count = 0;
	ctask->imm_count = 0;
	if (sc->sc_data_direction == DMA_TO_DEVICE) {
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
		 *	r2t_data_count	bytes to be sent via R2T ack's
		 *
		 *      pad_count       bytes to be sent as zero-padding
		 */
		ctask->unsol_count = 0;
		ctask->unsol_offset = 0;
		ctask->unsol_datasn = 0;

		if (session->imm_data_en) {
			if (scsi_bufflen(sc) >= session->first_burst)
				ctask->imm_count = min(session->first_burst,
							conn->max_xmit_dlength);
			else
				ctask->imm_count = min(scsi_bufflen(sc),
							conn->max_xmit_dlength);
			hton24(ctask->hdr->dlength, ctask->imm_count);
		} else
			zero_data(ctask->hdr->dlength);

		if (!session->initial_r2t_en) {
			ctask->unsol_count = min((session->first_burst),
				(scsi_bufflen(sc))) - ctask->imm_count;
			ctask->unsol_offset = ctask->imm_count;
		}

		if (!ctask->unsol_count)
			/* No unsolicit Data-Out's */
			ctask->hdr->flags |= ISCSI_FLAG_CMD_FINAL;
	} else {
		hdr->flags |= ISCSI_FLAG_CMD_FINAL;
		zero_data(hdr->dlength);

		if (sc->sc_data_direction == DMA_FROM_DEVICE)
			hdr->flags |= ISCSI_FLAG_CMD_READ;
	}

	conn->scsicmd_pdus_cnt++;

        debug_scsi("iscsi prep [%s cid %d sc %p cdb 0x%x itt 0x%x len %d "
		"cmdsn %d win %d]\n",
                sc->sc_data_direction == DMA_TO_DEVICE ? "write" : "read",
		conn->id, sc, sc->cmnd[0], ctask->itt, scsi_bufflen(sc),
                session->cmdsn, session->max_cmdsn - session->exp_cmdsn + 1);
}

/**
 * iscsi_complete_command - return command back to scsi-ml
 * @ctask: iscsi cmd task
 *
 * Must be called with session lock.
 * This function returns the scsi command to scsi-ml and returns
 * the cmd task to the pool of available cmd tasks.
 */
static void iscsi_complete_command(struct iscsi_cmd_task *ctask)
{
	struct iscsi_session *session = ctask->conn->session;
	struct scsi_cmnd *sc = ctask->sc;

	ctask->state = ISCSI_TASK_COMPLETED;
	ctask->sc = NULL;
	/* SCSI eh reuses commands to verify us */
	sc->SCp.ptr = NULL;
	list_del_init(&ctask->running);
	__kfifo_put(session->cmdpool.queue, (void*)&ctask, sizeof(void*));
	sc->scsi_done(sc);
}

static void __iscsi_get_ctask(struct iscsi_cmd_task *ctask)
{
	atomic_inc(&ctask->refcount);
}

static void __iscsi_put_ctask(struct iscsi_cmd_task *ctask)
{
	if (atomic_dec_and_test(&ctask->refcount))
		iscsi_complete_command(ctask);
}

/**
 * iscsi_cmd_rsp - SCSI Command Response processing
 * @conn: iscsi connection
 * @hdr: iscsi header
 * @ctask: scsi command task
 * @data: cmd data buffer
 * @datalen: len of buffer
 *
 * iscsi_cmd_rsp sets up the scsi_cmnd fields based on the PDU and
 * then completes the command and task.
 **/
static void iscsi_scsi_cmd_rsp(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
			       struct iscsi_cmd_task *ctask, char *data,
			       int datalen)
{
	struct iscsi_cmd_rsp *rhdr = (struct iscsi_cmd_rsp *)hdr;
	struct iscsi_session *session = conn->session;
	struct scsi_cmnd *sc = ctask->sc;

	iscsi_update_cmdsn(session, (struct iscsi_nopin*)rhdr);
	conn->exp_statsn = be32_to_cpu(rhdr->statsn) + 1;

	sc->result = (DID_OK << 16) | rhdr->cmd_status;

	if (rhdr->response != ISCSI_STATUS_CMD_COMPLETED) {
		sc->result = DID_ERROR << 16;
		goto out;
	}

	if (rhdr->cmd_status == SAM_STAT_CHECK_CONDITION) {
		uint16_t senselen;

		if (datalen < 2) {
invalid_datalen:
			printk(KERN_ERR "iscsi: Got CHECK_CONDITION but "
			       "invalid data buffer size of %d\n", datalen);
			sc->result = DID_BAD_TARGET << 16;
			goto out;
		}

		senselen = be16_to_cpu(get_unaligned((__be16 *) data));
		if (datalen < senselen)
			goto invalid_datalen;

		memcpy(sc->sense_buffer, data + 2,
		       min_t(uint16_t, senselen, SCSI_SENSE_BUFFERSIZE));
		debug_scsi("copied %d bytes of sense\n",
			   min_t(uint16_t, senselen, SCSI_SENSE_BUFFERSIZE));
	}

	if (sc->sc_data_direction == DMA_TO_DEVICE)
		goto out;

	if (rhdr->flags & ISCSI_FLAG_CMD_UNDERFLOW) {
		int res_count = be32_to_cpu(rhdr->residual_count);

		if (res_count > 0 && res_count <= scsi_bufflen(sc))
			scsi_set_resid(sc, res_count);
		else
			sc->result = (DID_BAD_TARGET << 16) | rhdr->cmd_status;
	} else if (rhdr->flags & ISCSI_FLAG_CMD_BIDI_UNDERFLOW)
		sc->result = (DID_BAD_TARGET << 16) | rhdr->cmd_status;
	else if (rhdr->flags & ISCSI_FLAG_CMD_OVERFLOW)
		scsi_set_resid(sc, be32_to_cpu(rhdr->residual_count));

out:
	debug_scsi("done [sc %lx res %d itt 0x%x]\n",
		   (long)sc, sc->result, ctask->itt);
	conn->scsirsp_pdus_cnt++;

	__iscsi_put_ctask(ctask);
}

static void iscsi_tmf_rsp(struct iscsi_conn *conn, struct iscsi_hdr *hdr)
{
	struct iscsi_tm_rsp *tmf = (struct iscsi_tm_rsp *)hdr;

	conn->exp_statsn = be32_to_cpu(hdr->statsn) + 1;
	conn->tmfrsp_pdus_cnt++;

	if (conn->tmabort_state != TMABORT_INITIAL)
		return;

	if (tmf->response == ISCSI_TMF_RSP_COMPLETE)
		conn->tmabort_state = TMABORT_SUCCESS;
	else if (tmf->response == ISCSI_TMF_RSP_NO_TASK)
		conn->tmabort_state = TMABORT_NOT_FOUND;
	else
		conn->tmabort_state = TMABORT_FAILED;
	wake_up(&conn->ehwait);
}

static int iscsi_handle_reject(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
			       char *data, int datalen)
{
	struct iscsi_reject *reject = (struct iscsi_reject *)hdr;
	struct iscsi_hdr rejected_pdu;
	uint32_t itt;

	conn->exp_statsn = be32_to_cpu(reject->statsn) + 1;

	if (reject->reason == ISCSI_REASON_DATA_DIGEST_ERROR) {
		if (ntoh24(reject->dlength) > datalen)
			return ISCSI_ERR_PROTO;

		if (ntoh24(reject->dlength) >= sizeof(struct iscsi_hdr)) {
			memcpy(&rejected_pdu, data, sizeof(struct iscsi_hdr));
			itt = get_itt(rejected_pdu.itt);
			printk(KERN_ERR "itt 0x%x had pdu (op 0x%x) rejected "
				"due to DataDigest error.\n", itt,
				rejected_pdu.opcode);
		}
	}
	return 0;
}

/**
 * __iscsi_complete_pdu - complete pdu
 * @conn: iscsi conn
 * @hdr: iscsi header
 * @data: data buffer
 * @datalen: len of data buffer
 *
 * Completes pdu processing by freeing any resources allocated at
 * queuecommand or send generic. session lock must be held and verify
 * itt must have been called.
 */
int __iscsi_complete_pdu(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
			 char *data, int datalen)
{
	struct iscsi_session *session = conn->session;
	int opcode = hdr->opcode & ISCSI_OPCODE_MASK, rc = 0;
	struct iscsi_cmd_task *ctask;
	struct iscsi_mgmt_task *mtask;
	uint32_t itt;

	if (hdr->itt != RESERVED_ITT)
		itt = get_itt(hdr->itt);
	else
		itt = ~0U;

	if (itt < session->cmds_max) {
		ctask = session->cmds[itt];

		debug_scsi("cmdrsp [op 0x%x cid %d itt 0x%x len %d]\n",
			   opcode, conn->id, ctask->itt, datalen);

		switch(opcode) {
		case ISCSI_OP_SCSI_CMD_RSP:
			BUG_ON((void*)ctask != ctask->sc->SCp.ptr);
			iscsi_scsi_cmd_rsp(conn, hdr, ctask, data,
					   datalen);
			break;
		case ISCSI_OP_SCSI_DATA_IN:
			BUG_ON((void*)ctask != ctask->sc->SCp.ptr);
			if (hdr->flags & ISCSI_FLAG_DATA_STATUS) {
				conn->scsirsp_pdus_cnt++;
				__iscsi_put_ctask(ctask);
			}
			break;
		case ISCSI_OP_R2T:
			/* LLD handles this for now */
			break;
		default:
			rc = ISCSI_ERR_BAD_OPCODE;
			break;
		}
	} else if (itt >= ISCSI_MGMT_ITT_OFFSET &&
		   itt < ISCSI_MGMT_ITT_OFFSET + session->mgmtpool_max) {
		mtask = session->mgmt_cmds[itt - ISCSI_MGMT_ITT_OFFSET];

		debug_scsi("immrsp [op 0x%x cid %d itt 0x%x len %d]\n",
			   opcode, conn->id, mtask->itt, datalen);

		iscsi_update_cmdsn(session, (struct iscsi_nopin*)hdr);
		switch(opcode) {
		case ISCSI_OP_LOGOUT_RSP:
			if (datalen) {
				rc = ISCSI_ERR_PROTO;
				break;
			}
			conn->exp_statsn = be32_to_cpu(hdr->statsn) + 1;
			/* fall through */
		case ISCSI_OP_LOGIN_RSP:
		case ISCSI_OP_TEXT_RSP:
			/*
			 * login related PDU's exp_statsn is handled in
			 * userspace
			 */
			if (iscsi_recv_pdu(conn->cls_conn, hdr, data, datalen))
				rc = ISCSI_ERR_CONN_FAILED;
			list_del(&mtask->running);
			if (conn->login_mtask != mtask)
				__kfifo_put(session->mgmtpool.queue,
					    (void*)&mtask, sizeof(void*));
			break;
		case ISCSI_OP_SCSI_TMFUNC_RSP:
			if (datalen) {
				rc = ISCSI_ERR_PROTO;
				break;
			}

			iscsi_tmf_rsp(conn, hdr);
			break;
		case ISCSI_OP_NOOP_IN:
			if (hdr->ttt != cpu_to_be32(ISCSI_RESERVED_TAG) || datalen) {
				rc = ISCSI_ERR_PROTO;
				break;
			}
			conn->exp_statsn = be32_to_cpu(hdr->statsn) + 1;

			if (iscsi_recv_pdu(conn->cls_conn, hdr, data, datalen))
				rc = ISCSI_ERR_CONN_FAILED;
			list_del(&mtask->running);
			if (conn->login_mtask != mtask)
				__kfifo_put(session->mgmtpool.queue,
					    (void*)&mtask, sizeof(void*));
			break;
		default:
			rc = ISCSI_ERR_BAD_OPCODE;
			break;
		}
	} else if (itt == ~0U) {
		iscsi_update_cmdsn(session, (struct iscsi_nopin*)hdr);

		switch(opcode) {
		case ISCSI_OP_NOOP_IN:
			if (datalen) {
				rc = ISCSI_ERR_PROTO;
				break;
			}

			if (hdr->ttt == cpu_to_be32(ISCSI_RESERVED_TAG))
				break;

			if (iscsi_recv_pdu(conn->cls_conn, hdr, NULL, 0))
				rc = ISCSI_ERR_CONN_FAILED;
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
	} else
		rc = ISCSI_ERR_BAD_ITT;

	return rc;
}
EXPORT_SYMBOL_GPL(__iscsi_complete_pdu);

int iscsi_complete_pdu(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
		       char *data, int datalen)
{
	int rc;

	spin_lock(&conn->session->lock);
	rc = __iscsi_complete_pdu(conn, hdr, data, datalen);
	spin_unlock(&conn->session->lock);
	return rc;
}
EXPORT_SYMBOL_GPL(iscsi_complete_pdu);

/* verify itt (itt encoding: age+cid+itt) */
int iscsi_verify_itt(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
		     uint32_t *ret_itt)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_cmd_task *ctask;
	uint32_t itt;

	if (hdr->itt != RESERVED_ITT) {
		if (((__force u32)hdr->itt & ISCSI_AGE_MASK) !=
		    (session->age << ISCSI_AGE_SHIFT)) {
			printk(KERN_ERR "iscsi: received itt %x expected "
				"session age (%x)\n", (__force u32)hdr->itt,
				session->age & ISCSI_AGE_MASK);
			return ISCSI_ERR_BAD_ITT;
		}

		if (((__force u32)hdr->itt & ISCSI_CID_MASK) !=
		    (conn->id << ISCSI_CID_SHIFT)) {
			printk(KERN_ERR "iscsi: received itt %x, expected "
				"CID (%x)\n", (__force u32)hdr->itt, conn->id);
			return ISCSI_ERR_BAD_ITT;
		}
		itt = get_itt(hdr->itt);
	} else
		itt = ~0U;

	if (itt < session->cmds_max) {
		ctask = session->cmds[itt];

		if (!ctask->sc) {
			printk(KERN_INFO "iscsi: dropping ctask with "
			       "itt 0x%x\n", ctask->itt);
			/* force drop */
			return ISCSI_ERR_NO_SCSI_CMD;
		}

		if (ctask->sc->SCp.phase != session->age) {
			printk(KERN_ERR "iscsi: ctask's session age %d, "
				"expected %d\n", ctask->sc->SCp.phase,
				session->age);
			return ISCSI_ERR_SESSION_FAILED;
		}
	}

	*ret_itt = itt;
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_verify_itt);

void iscsi_conn_failure(struct iscsi_conn *conn, enum iscsi_err err)
{
	struct iscsi_session *session = conn->session;
	unsigned long flags;

	spin_lock_irqsave(&session->lock, flags);
	if (session->state == ISCSI_STATE_FAILED) {
		spin_unlock_irqrestore(&session->lock, flags);
		return;
	}

	if (conn->stop_stage == 0)
		session->state = ISCSI_STATE_FAILED;
	spin_unlock_irqrestore(&session->lock, flags);
	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_rx);
	iscsi_conn_error(conn->cls_conn, err);
}
EXPORT_SYMBOL_GPL(iscsi_conn_failure);

static void iscsi_prep_mtask(struct iscsi_conn *conn,
			     struct iscsi_mgmt_task *mtask)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_hdr *hdr = mtask->hdr;
	struct iscsi_nopout *nop = (struct iscsi_nopout *)hdr;

	if (hdr->opcode != (ISCSI_OP_LOGIN | ISCSI_OP_IMMEDIATE) &&
	    hdr->opcode != (ISCSI_OP_TEXT | ISCSI_OP_IMMEDIATE))
		nop->exp_statsn = cpu_to_be32(conn->exp_statsn);
	/*
	 * pre-format CmdSN for outgoing PDU.
	 */
	nop->cmdsn = cpu_to_be32(session->cmdsn);
	if (hdr->itt != RESERVED_ITT) {
		hdr->itt = build_itt(mtask->itt, conn->id, session->age);
		/*
		 * TODO: We always use immediate, so we never hit this.
		 * If we start to send tmfs or nops as non-immediate then
		 * we should start checking the cmdsn numbers for mgmt tasks.
		 */
		if (conn->c_stage == ISCSI_CONN_STARTED &&
		    !(hdr->opcode & ISCSI_OP_IMMEDIATE)) {
			session->queued_cmdsn++;
			session->cmdsn++;
		}
	}

	if (session->tt->init_mgmt_task)
		session->tt->init_mgmt_task(conn, mtask);

	debug_scsi("mgmtpdu [op 0x%x hdr->itt 0x%x datalen %d]\n",
		   hdr->opcode, hdr->itt, mtask->data_count);
}

static int iscsi_xmit_mtask(struct iscsi_conn *conn)
{
	struct iscsi_hdr *hdr = conn->mtask->hdr;
	int rc, was_logout = 0;

	spin_unlock_bh(&conn->session->lock);
	if ((hdr->opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_LOGOUT) {
		conn->session->state = ISCSI_STATE_IN_RECOVERY;
		iscsi_block_session(session_to_cls(conn->session));
		was_logout = 1;
	}
	rc = conn->session->tt->xmit_mgmt_task(conn, conn->mtask);
	spin_lock_bh(&conn->session->lock);
	if (rc)
		return rc;

	/* done with this in-progress mtask */
	conn->mtask = NULL;

	if (was_logout) {
		set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
		return -ENODATA;
	}
	return 0;
}

static int iscsi_check_cmdsn_window_closed(struct iscsi_conn *conn)
{
	struct iscsi_session *session = conn->session;

	/*
	 * Check for iSCSI window and take care of CmdSN wrap-around
	 */
	if (!iscsi_sna_lte(session->queued_cmdsn, session->max_cmdsn)) {
		debug_scsi("iSCSI CmdSN closed. ExpCmdSn %u MaxCmdSN %u "
			   "CmdSN %u/%u\n", session->exp_cmdsn,
			   session->max_cmdsn, session->cmdsn,
			   session->queued_cmdsn);
		return -ENOSPC;
	}
	return 0;
}

static int iscsi_xmit_ctask(struct iscsi_conn *conn)
{
	struct iscsi_cmd_task *ctask = conn->ctask;
	int rc = 0;

	/*
	 * serialize with TMF AbortTask
	 */
	if (ctask->state == ISCSI_TASK_ABORTING)
		goto done;

	__iscsi_get_ctask(ctask);
	spin_unlock_bh(&conn->session->lock);
	rc = conn->session->tt->xmit_cmd_task(conn, ctask);
	spin_lock_bh(&conn->session->lock);
	__iscsi_put_ctask(ctask);

done:
	if (!rc)
		/* done with this ctask */
		conn->ctask = NULL;
	return rc;
}

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
	int rc = 0;

	spin_lock_bh(&conn->session->lock);
	if (unlikely(conn->suspend_tx)) {
		debug_scsi("conn %d Tx suspended!\n", conn->id);
		spin_unlock_bh(&conn->session->lock);
		return -ENODATA;
	}

	if (conn->ctask) {
		rc = iscsi_xmit_ctask(conn);
		if (rc)
			goto again;
	}

	if (conn->mtask) {
		rc = iscsi_xmit_mtask(conn);
	        if (rc)
		        goto again;
	}

	/*
	 * process mgmt pdus like nops before commands since we should
	 * only have one nop-out as a ping from us and targets should not
	 * overflow us with nop-ins
	 */
check_mgmt:
	while (__kfifo_get(conn->mgmtqueue, (void*)&conn->mtask,
			   sizeof(void*))) {
		iscsi_prep_mtask(conn, conn->mtask);
		list_add_tail(&conn->mtask->running, &conn->mgmt_run_list);
		rc = iscsi_xmit_mtask(conn);
		if (rc)
			goto again;
	}

	/* process command queue */
	while (!list_empty(&conn->xmitqueue)) {
		/*
		 * iscsi tcp may readd the task to the xmitqueue to send
		 * write data
		 */
		conn->ctask = list_entry(conn->xmitqueue.next,
					 struct iscsi_cmd_task, running);
		if (conn->ctask->state == ISCSI_TASK_PENDING) {
			iscsi_prep_scsi_cmd_pdu(conn->ctask);
			conn->session->tt->init_cmd_task(conn->ctask);
		}
		conn->ctask->state = ISCSI_TASK_RUNNING;
		list_move_tail(conn->xmitqueue.next, &conn->run_list);
		rc = iscsi_xmit_ctask(conn);
		if (rc)
			goto again;
		/*
		 * we could continuously get new ctask requests so
		 * we need to check the mgmt queue for nops that need to
		 * be sent to aviod starvation
		 */
		if (__kfifo_len(conn->mgmtqueue))
			goto check_mgmt;
	}
	spin_unlock_bh(&conn->session->lock);
	return -ENODATA;

again:
	if (unlikely(conn->suspend_tx))
		rc = -ENODATA;
	spin_unlock_bh(&conn->session->lock);
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

enum {
	FAILURE_BAD_HOST = 1,
	FAILURE_SESSION_FAILED,
	FAILURE_SESSION_FREED,
	FAILURE_WINDOW_CLOSED,
	FAILURE_OOM,
	FAILURE_SESSION_TERMINATE,
	FAILURE_SESSION_IN_RECOVERY,
	FAILURE_SESSION_RECOVERY_TIMEOUT,
};

int iscsi_queuecommand(struct scsi_cmnd *sc, void (*done)(struct scsi_cmnd *))
{
	struct Scsi_Host *host;
	int reason = 0;
	struct iscsi_session *session;
	struct iscsi_conn *conn;
	struct iscsi_cmd_task *ctask = NULL;

	sc->scsi_done = done;
	sc->result = 0;
	sc->SCp.ptr = NULL;

	host = sc->device->host;
	session = iscsi_hostdata(host->hostdata);

	spin_lock(&session->lock);

	/*
	 * ISCSI_STATE_FAILED is a temp. state. The recovery
	 * code will decide what is best to do with command queued
	 * during this time
	 */
	if (session->state != ISCSI_STATE_LOGGED_IN &&
	    session->state != ISCSI_STATE_FAILED) {
		/*
		 * to handle the race between when we set the recovery state
		 * and block the session we requeue here (commands could
		 * be entering our queuecommand while a block is starting
		 * up because the block code is not locked)
		 */
		if (session->state == ISCSI_STATE_IN_RECOVERY) {
			reason = FAILURE_SESSION_IN_RECOVERY;
			goto reject;
		}

		if (session->state == ISCSI_STATE_RECOVERY_FAILED)
			reason = FAILURE_SESSION_RECOVERY_TIMEOUT;
		else if (session->state == ISCSI_STATE_TERMINATE)
			reason = FAILURE_SESSION_TERMINATE;
		else
			reason = FAILURE_SESSION_FREED;
		goto fault;
	}

	conn = session->leadconn;
	if (!conn) {
		reason = FAILURE_SESSION_FREED;
		goto fault;
	}

	if (iscsi_check_cmdsn_window_closed(conn)) {
		reason = FAILURE_WINDOW_CLOSED;
		goto reject;
	}

	if (!__kfifo_get(session->cmdpool.queue, (void*)&ctask,
			 sizeof(void*))) {
		reason = FAILURE_OOM;
		goto reject;
	}
	session->queued_cmdsn++;

	sc->SCp.phase = session->age;
	sc->SCp.ptr = (char *)ctask;

	atomic_set(&ctask->refcount, 1);
	ctask->state = ISCSI_TASK_PENDING;
	ctask->mtask = NULL;
	ctask->conn = conn;
	ctask->sc = sc;
	INIT_LIST_HEAD(&ctask->running);

	list_add_tail(&ctask->running, &conn->xmitqueue);
	spin_unlock(&session->lock);

	scsi_queue_work(host, &conn->xmitwork);
	return 0;

reject:
	spin_unlock(&session->lock);
	debug_scsi("cmd 0x%x rejected (%d)\n", sc->cmnd[0], reason);
	return SCSI_MLQUEUE_HOST_BUSY;

fault:
	spin_unlock(&session->lock);
	printk(KERN_ERR "iscsi: cmd 0x%x is not queued (%d)\n",
	       sc->cmnd[0], reason);
	sc->result = (DID_NO_CONNECT << 16);
	scsi_set_resid(sc, scsi_bufflen(sc));
	sc->scsi_done(sc);
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_queuecommand);

int iscsi_change_queue_depth(struct scsi_device *sdev, int depth)
{
	if (depth > ISCSI_MAX_CMD_PER_LUN)
		depth = ISCSI_MAX_CMD_PER_LUN;
	scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), depth);
	return sdev->queue_depth;
}
EXPORT_SYMBOL_GPL(iscsi_change_queue_depth);

static struct iscsi_mgmt_task *
__iscsi_conn_send_pdu(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
		      char *data, uint32_t data_size)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_mgmt_task *mtask;

	if (session->state == ISCSI_STATE_TERMINATE)
		return NULL;

	if (hdr->opcode == (ISCSI_OP_LOGIN | ISCSI_OP_IMMEDIATE) ||
	    hdr->opcode == (ISCSI_OP_TEXT | ISCSI_OP_IMMEDIATE))
		/*
		 * Login and Text are sent serially, in
		 * request-followed-by-response sequence.
		 * Same mtask can be used. Same ITT must be used.
		 * Note that login_mtask is preallocated at conn_create().
		 */
		mtask = conn->login_mtask;
	else {
		BUG_ON(conn->c_stage == ISCSI_CONN_INITIAL_STAGE);
		BUG_ON(conn->c_stage == ISCSI_CONN_STOPPED);

		if (!__kfifo_get(session->mgmtpool.queue,
				 (void*)&mtask, sizeof(void*)))
			return NULL;
	}

	if (data_size) {
		memcpy(mtask->data, data, data_size);
		mtask->data_count = data_size;
	} else
		mtask->data_count = 0;

	INIT_LIST_HEAD(&mtask->running);
	memcpy(mtask->hdr, hdr, sizeof(struct iscsi_hdr));
	__kfifo_put(conn->mgmtqueue, (void*)&mtask, sizeof(void*));
	return mtask;
}

int iscsi_conn_send_pdu(struct iscsi_cls_conn *cls_conn, struct iscsi_hdr *hdr,
			char *data, uint32_t data_size)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	int err = 0;

	spin_lock_bh(&session->lock);
	if (!__iscsi_conn_send_pdu(conn, hdr, data, data_size))
		err = -EPERM;
	spin_unlock_bh(&session->lock);
	scsi_queue_work(session->host, &conn->xmitwork);
	return err;
}
EXPORT_SYMBOL_GPL(iscsi_conn_send_pdu);

void iscsi_session_recovery_timedout(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *session = class_to_transport_session(cls_session);
	struct iscsi_conn *conn = session->leadconn;

	spin_lock_bh(&session->lock);
	if (session->state != ISCSI_STATE_LOGGED_IN) {
		session->state = ISCSI_STATE_RECOVERY_FAILED;
		if (conn)
			wake_up(&conn->ehwait);
	}
	spin_unlock_bh(&session->lock);
}
EXPORT_SYMBOL_GPL(iscsi_session_recovery_timedout);

int iscsi_eh_host_reset(struct scsi_cmnd *sc)
{
	struct Scsi_Host *host = sc->device->host;
	struct iscsi_session *session = iscsi_hostdata(host->hostdata);
	struct iscsi_conn *conn = session->leadconn;
	int fail_session = 0;

	spin_lock_bh(&session->lock);
	if (session->state == ISCSI_STATE_TERMINATE) {
failed:
		debug_scsi("failing host reset: session terminated "
			   "[CID %d age %d]\n", conn->id, session->age);
		spin_unlock_bh(&session->lock);
		return FAILED;
	}

	if (sc->SCp.phase == session->age) {
		debug_scsi("failing connection CID %d due to SCSI host reset\n",
			   conn->id);
		fail_session = 1;
	}
	spin_unlock_bh(&session->lock);

	/*
	 * we drop the lock here but the leadconn cannot be destoyed while
	 * we are in the scsi eh
	 */
	if (fail_session)
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);

	debug_scsi("iscsi_eh_host_reset wait for relogin\n");
	wait_event_interruptible(conn->ehwait,
				 session->state == ISCSI_STATE_TERMINATE ||
				 session->state == ISCSI_STATE_LOGGED_IN ||
				 session->state == ISCSI_STATE_RECOVERY_FAILED);
	if (signal_pending(current))
		flush_signals(current);

	spin_lock_bh(&session->lock);
	if (session->state == ISCSI_STATE_LOGGED_IN)
		printk(KERN_INFO "iscsi: host reset succeeded\n");
	else
		goto failed;
	spin_unlock_bh(&session->lock);

	return SUCCESS;
}
EXPORT_SYMBOL_GPL(iscsi_eh_host_reset);

static void iscsi_tmabort_timedout(unsigned long data)
{
	struct iscsi_cmd_task *ctask = (struct iscsi_cmd_task *)data;
	struct iscsi_conn *conn = ctask->conn;
	struct iscsi_session *session = conn->session;

	spin_lock(&session->lock);
	if (conn->tmabort_state == TMABORT_INITIAL) {
		conn->tmabort_state = TMABORT_TIMEDOUT;
		debug_scsi("tmabort timedout [sc %p itt 0x%x]\n",
			ctask->sc, ctask->itt);
		/* unblock eh_abort() */
		wake_up(&conn->ehwait);
	}
	spin_unlock(&session->lock);
}

static int iscsi_exec_abort_task(struct scsi_cmnd *sc,
				 struct iscsi_cmd_task *ctask)
{
	struct iscsi_conn *conn = ctask->conn;
	struct iscsi_session *session = conn->session;
	struct iscsi_tm *hdr = &conn->tmhdr;

	/*
	 * ctask timed out but session is OK requests must be serialized.
	 */
	memset(hdr, 0, sizeof(struct iscsi_tm));
	hdr->opcode = ISCSI_OP_SCSI_TMFUNC | ISCSI_OP_IMMEDIATE;
	hdr->flags = ISCSI_TM_FUNC_ABORT_TASK;
	hdr->flags |= ISCSI_FLAG_CMD_FINAL;
	memcpy(hdr->lun, ctask->hdr->lun, sizeof(hdr->lun));
	hdr->rtt = ctask->hdr->itt;
	hdr->refcmdsn = ctask->hdr->cmdsn;

	ctask->mtask = __iscsi_conn_send_pdu(conn, (struct iscsi_hdr *)hdr,
					    NULL, 0);
	if (!ctask->mtask) {
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
		debug_scsi("abort sent failure [itt 0x%x]\n", ctask->itt);
		return -EPERM;
	}
	ctask->state = ISCSI_TASK_ABORTING;

	debug_scsi("abort sent [itt 0x%x]\n", ctask->itt);

	if (conn->tmabort_state == TMABORT_INITIAL) {
		conn->tmfcmd_pdus_cnt++;
		conn->tmabort_timer.expires = 20*HZ + jiffies;
		conn->tmabort_timer.function = iscsi_tmabort_timedout;
		conn->tmabort_timer.data = (unsigned long)ctask;
		add_timer(&conn->tmabort_timer);
		debug_scsi("abort set timeout [itt 0x%x]\n", ctask->itt);
	}
	spin_unlock_bh(&session->lock);
	scsi_queue_work(session->host, &conn->xmitwork);

	/*
	 * block eh thread until:
	 *
	 * 1) abort response
	 * 2) abort timeout
	 * 3) session is terminated or restarted or userspace has
	 * given up on recovery
	 */
	wait_event_interruptible(conn->ehwait,
				 sc->SCp.phase != session->age ||
				 session->state != ISCSI_STATE_LOGGED_IN ||
				 conn->tmabort_state != TMABORT_INITIAL);
	if (signal_pending(current))
		flush_signals(current);
	del_timer_sync(&conn->tmabort_timer);
	spin_lock_bh(&session->lock);
	return 0;
}

/*
 * session lock must be held
 */
static struct iscsi_mgmt_task *
iscsi_remove_mgmt_task(struct kfifo *fifo, uint32_t itt)
{
	int i, nr_tasks = __kfifo_len(fifo) / sizeof(void*);
	struct iscsi_mgmt_task *task;

	debug_scsi("searching %d tasks\n", nr_tasks);

	for (i = 0; i < nr_tasks; i++) {
		__kfifo_get(fifo, (void*)&task, sizeof(void*));
		debug_scsi("check task %u\n", task->itt);

		if (task->itt == itt) {
			debug_scsi("matched task\n");
			return task;
		}

		__kfifo_put(fifo, (void*)&task, sizeof(void*));
	}
	return NULL;
}

static int iscsi_ctask_mtask_cleanup(struct iscsi_cmd_task *ctask)
{
	struct iscsi_conn *conn = ctask->conn;
	struct iscsi_session *session = conn->session;

	if (!ctask->mtask)
		return -EINVAL;

	if (!iscsi_remove_mgmt_task(conn->mgmtqueue, ctask->mtask->itt))
		list_del(&ctask->mtask->running);
	__kfifo_put(session->mgmtpool.queue, (void*)&ctask->mtask,
		    sizeof(void*));
	ctask->mtask = NULL;
	return 0;
}

/*
 * session lock must be held
 */
static void fail_command(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask,
			 int err)
{
	struct scsi_cmnd *sc;

	sc = ctask->sc;
	if (!sc)
		return;

	if (ctask->state == ISCSI_TASK_PENDING)
		/*
		 * cmd never made it to the xmit thread, so we should not count
		 * the cmd in the sequencing
		 */
		conn->session->queued_cmdsn--;
	else
		conn->session->tt->cleanup_cmd_task(conn, ctask);
	iscsi_ctask_mtask_cleanup(ctask);

	sc->result = err;
	scsi_set_resid(sc, scsi_bufflen(sc));
	if (conn->ctask == ctask)
		conn->ctask = NULL;
	/* release ref from queuecommand */
	__iscsi_put_ctask(ctask);
}

int iscsi_eh_abort(struct scsi_cmnd *sc)
{
	struct iscsi_cmd_task *ctask;
	struct iscsi_conn *conn;
	struct iscsi_session *session;
	int rc;

	/*
	 * if session was ISCSI_STATE_IN_RECOVERY then we may not have
	 * got the command.
	 */
	if (!sc->SCp.ptr) {
		debug_scsi("sc never reached iscsi layer or it completed.\n");
		return SUCCESS;
	}

	ctask = (struct iscsi_cmd_task *)sc->SCp.ptr;
	conn = ctask->conn;
	session = conn->session;

	conn->eh_abort_cnt++;
	debug_scsi("aborting [sc %p itt 0x%x]\n", sc, ctask->itt);

	spin_lock_bh(&session->lock);

	/*
	 * If we are not logged in or we have started a new session
	 * then let the host reset code handle this
	 */
	if (session->state != ISCSI_STATE_LOGGED_IN ||
	    sc->SCp.phase != session->age)
		goto failed;

	/* ctask completed before time out */
	if (!ctask->sc) {
		debug_scsi("sc completed while abort in progress\n");
		goto success;
	}

	/* what should we do here ? */
	if (conn->ctask == ctask) {
		printk(KERN_INFO "iscsi: sc %p itt 0x%x partially sent. "
		       "Failing abort\n", sc, ctask->itt);
		goto failed;
	}

	if (ctask->state == ISCSI_TASK_PENDING) {
		fail_command(conn, ctask, DID_ABORT << 16);
		goto success;
	}

	conn->tmabort_state = TMABORT_INITIAL;
	rc = iscsi_exec_abort_task(sc, ctask);
	if (rc || sc->SCp.phase != session->age ||
	    session->state != ISCSI_STATE_LOGGED_IN)
		goto failed;
	iscsi_ctask_mtask_cleanup(ctask);

	switch (conn->tmabort_state) {
	case TMABORT_SUCCESS:
		spin_unlock_bh(&session->lock);
		/*
		 * clean up task if aborted. grab the recv lock as a writer
		 */
		write_lock_bh(conn->recv_lock);
		spin_lock(&session->lock);
		fail_command(conn, ctask, DID_ABORT << 16);
		spin_unlock(&session->lock);
		write_unlock_bh(conn->recv_lock);
		/*
		 * make sure xmit thread is not still touching the
		 * ctask/scsi_cmnd
		 */
		scsi_flush_work(session->host);
		goto success_unlocked;
	case TMABORT_NOT_FOUND:
		if (!ctask->sc) {
			/* ctask completed before tmf abort response */
			debug_scsi("sc completed while abort in progress\n");
			goto success;
		}
		/* fall through */
	default:
		/* timedout or failed */
		spin_unlock_bh(&session->lock);
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
		goto failed_unlocked;
	}

success:
	spin_unlock_bh(&session->lock);
success_unlocked:
	debug_scsi("abort success [sc %lx itt 0x%x]\n", (long)sc, ctask->itt);
	return SUCCESS;

failed:
	spin_unlock_bh(&session->lock);
failed_unlocked:
	debug_scsi("abort failed [sc %lx itt 0x%x]\n", (long)sc, ctask->itt);
	return FAILED;
}
EXPORT_SYMBOL_GPL(iscsi_eh_abort);

int
iscsi_pool_init(struct iscsi_queue *q, int max, void ***items, int item_size)
{
	int i;

	*items = kmalloc(max * sizeof(void*), GFP_KERNEL);
	if (*items == NULL)
		return -ENOMEM;

	q->max = max;
	q->pool = kmalloc(max * sizeof(void*), GFP_KERNEL);
	if (q->pool == NULL) {
		kfree(*items);
		return -ENOMEM;
	}

	q->queue = kfifo_init((void*)q->pool, max * sizeof(void*),
			      GFP_KERNEL, NULL);
	if (q->queue == ERR_PTR(-ENOMEM)) {
		kfree(q->pool);
		kfree(*items);
		return -ENOMEM;
	}

	for (i = 0; i < max; i++) {
		q->pool[i] = kmalloc(item_size, GFP_KERNEL);
		if (q->pool[i] == NULL) {
			int j;

			for (j = 0; j < i; j++)
				kfree(q->pool[j]);

			kfifo_free(q->queue);
			kfree(q->pool);
			kfree(*items);
			return -ENOMEM;
		}
		memset(q->pool[i], 0, item_size);
		(*items)[i] = q->pool[i];
		__kfifo_put(q->queue, (void*)&q->pool[i], sizeof(void*));
	}
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_pool_init);

void iscsi_pool_free(struct iscsi_queue *q, void **items)
{
	int i;

	for (i = 0; i < q->max; i++)
		kfree(items[i]);
	kfree(q->pool);
	kfree(items);
}
EXPORT_SYMBOL_GPL(iscsi_pool_free);

/*
 * iSCSI Session's hostdata organization:
 *
 *    *------------------* <== hostdata_session(host->hostdata)
 *    | ptr to class sess|
 *    |------------------| <== iscsi_hostdata(host->hostdata)
 *    | iscsi_session    |
 *    *------------------*
 */

#define hostdata_privsize(_sz)	(sizeof(unsigned long) + _sz + \
				 _sz % sizeof(unsigned long))

#define hostdata_session(_hostdata) (iscsi_ptr(*(unsigned long *)_hostdata))

/**
 * iscsi_session_setup - create iscsi cls session and host and session
 * @scsit: scsi transport template
 * @iscsit: iscsi transport template
 * @cmds_max: scsi host can queue
 * @qdepth: scsi host cmds per lun
 * @cmd_task_size: LLD ctask private data size
 * @mgmt_task_size: LLD mtask private data size
 * @initial_cmdsn: initial CmdSN
 * @hostno: host no allocated
 *
 * This can be used by software iscsi_transports that allocate
 * a session per scsi host.
 **/
struct iscsi_cls_session *
iscsi_session_setup(struct iscsi_transport *iscsit,
		    struct scsi_transport_template *scsit,
		    uint16_t cmds_max, uint16_t qdepth,
		    int cmd_task_size, int mgmt_task_size,
		    uint32_t initial_cmdsn, uint32_t *hostno)
{
	struct Scsi_Host *shost;
	struct iscsi_session *session;
	struct iscsi_cls_session *cls_session;
	int cmd_i;

	if (qdepth > ISCSI_MAX_CMD_PER_LUN || qdepth < 1) {
		if (qdepth != 0)
			printk(KERN_ERR "iscsi: invalid queue depth of %d. "
			      "Queue depth must be between 1 and %d.\n",
			      qdepth, ISCSI_MAX_CMD_PER_LUN);
		qdepth = ISCSI_DEF_CMD_PER_LUN;
	}

	if (cmds_max < 2 || (cmds_max & (cmds_max - 1)) ||
	    cmds_max >= ISCSI_MGMT_ITT_OFFSET) {
		if (cmds_max != 0)
			printk(KERN_ERR "iscsi: invalid can_queue of %d. "
			       "can_queue must be a power of 2 and between "
			       "2 and %d - setting to %d.\n", cmds_max,
			       ISCSI_MGMT_ITT_OFFSET, ISCSI_DEF_XMIT_CMDS_MAX);
		cmds_max = ISCSI_DEF_XMIT_CMDS_MAX;
	}

	shost = scsi_host_alloc(iscsit->host_template,
				hostdata_privsize(sizeof(*session)));
	if (!shost)
		return NULL;

	/* the iscsi layer takes one task for reserve */
	shost->can_queue = cmds_max - 1;
	shost->cmd_per_lun = qdepth;
	shost->max_id = 1;
	shost->max_channel = 0;
	shost->max_lun = iscsit->max_lun;
	shost->max_cmd_len = iscsit->max_cmd_len;
	shost->transportt = scsit;
	shost->transportt->create_work_queue = 1;
	*hostno = shost->host_no;

	session = iscsi_hostdata(shost->hostdata);
	memset(session, 0, sizeof(struct iscsi_session));
	session->host = shost;
	session->state = ISCSI_STATE_FREE;
	session->mgmtpool_max = ISCSI_MGMT_CMDS_MAX;
	session->cmds_max = cmds_max;
	session->queued_cmdsn = session->cmdsn = initial_cmdsn;
	session->exp_cmdsn = initial_cmdsn + 1;
	session->max_cmdsn = initial_cmdsn + 1;
	session->max_r2t = 1;
	session->tt = iscsit;

	/* initialize SCSI PDU commands pool */
	if (iscsi_pool_init(&session->cmdpool, session->cmds_max,
			    (void***)&session->cmds,
			    cmd_task_size + sizeof(struct iscsi_cmd_task)))
		goto cmdpool_alloc_fail;

	/* pre-format cmds pool with ITT */
	for (cmd_i = 0; cmd_i < session->cmds_max; cmd_i++) {
		struct iscsi_cmd_task *ctask = session->cmds[cmd_i];

		if (cmd_task_size)
			ctask->dd_data = &ctask[1];
		ctask->itt = cmd_i;
		INIT_LIST_HEAD(&ctask->running);
	}

	spin_lock_init(&session->lock);

	/* initialize immediate command pool */
	if (iscsi_pool_init(&session->mgmtpool, session->mgmtpool_max,
			   (void***)&session->mgmt_cmds,
			   mgmt_task_size + sizeof(struct iscsi_mgmt_task)))
		goto mgmtpool_alloc_fail;


	/* pre-format immediate cmds pool with ITT */
	for (cmd_i = 0; cmd_i < session->mgmtpool_max; cmd_i++) {
		struct iscsi_mgmt_task *mtask = session->mgmt_cmds[cmd_i];

		if (mgmt_task_size)
			mtask->dd_data = &mtask[1];
		mtask->itt = ISCSI_MGMT_ITT_OFFSET + cmd_i;
		INIT_LIST_HEAD(&mtask->running);
	}

	if (scsi_add_host(shost, NULL))
		goto add_host_fail;

	if (!try_module_get(iscsit->owner))
		goto cls_session_fail;

	cls_session = iscsi_create_session(shost, iscsit, 0);
	if (!cls_session)
		goto module_put;
	*(unsigned long*)shost->hostdata = (unsigned long)cls_session;

	return cls_session;

module_put:
	module_put(iscsit->owner);
cls_session_fail:
	scsi_remove_host(shost);
add_host_fail:
	iscsi_pool_free(&session->mgmtpool, (void**)session->mgmt_cmds);
mgmtpool_alloc_fail:
	iscsi_pool_free(&session->cmdpool, (void**)session->cmds);
cmdpool_alloc_fail:
	scsi_host_put(shost);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_session_setup);

/**
 * iscsi_session_teardown - destroy session, host, and cls_session
 * shost: scsi host
 *
 * This can be used by software iscsi_transports that allocate
 * a session per scsi host.
 **/
void iscsi_session_teardown(struct iscsi_cls_session *cls_session)
{
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);
	struct iscsi_session *session = iscsi_hostdata(shost->hostdata);
	struct module *owner = cls_session->transport->owner;

	iscsi_unblock_session(cls_session);
	scsi_remove_host(shost);

	iscsi_pool_free(&session->mgmtpool, (void**)session->mgmt_cmds);
	iscsi_pool_free(&session->cmdpool, (void**)session->cmds);

	kfree(session->password);
	kfree(session->password_in);
	kfree(session->username);
	kfree(session->username_in);
	kfree(session->targetname);
	kfree(session->netdev);
	kfree(session->hwaddress);
	kfree(session->initiatorname);

	iscsi_destroy_session(cls_session);
	scsi_host_put(shost);
	module_put(owner);
}
EXPORT_SYMBOL_GPL(iscsi_session_teardown);

/**
 * iscsi_conn_setup - create iscsi_cls_conn and iscsi_conn
 * @cls_session: iscsi_cls_session
 * @conn_idx: cid
 **/
struct iscsi_cls_conn *
iscsi_conn_setup(struct iscsi_cls_session *cls_session, uint32_t conn_idx)
{
	struct iscsi_session *session = class_to_transport_session(cls_session);
	struct iscsi_conn *conn;
	struct iscsi_cls_conn *cls_conn;
	char *data;

	cls_conn = iscsi_create_conn(cls_session, conn_idx);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;
	memset(conn, 0, sizeof(*conn));

	conn->session = session;
	conn->cls_conn = cls_conn;
	conn->c_stage = ISCSI_CONN_INITIAL_STAGE;
	conn->id = conn_idx;
	conn->exp_statsn = 0;
	conn->tmabort_state = TMABORT_INITIAL;
	INIT_LIST_HEAD(&conn->run_list);
	INIT_LIST_HEAD(&conn->mgmt_run_list);
	INIT_LIST_HEAD(&conn->xmitqueue);

	/* initialize general immediate & non-immediate PDU commands queue */
	conn->mgmtqueue = kfifo_alloc(session->mgmtpool_max * sizeof(void*),
			                GFP_KERNEL, NULL);
	if (conn->mgmtqueue == ERR_PTR(-ENOMEM))
		goto mgmtqueue_alloc_fail;

	INIT_WORK(&conn->xmitwork, iscsi_xmitworker);

	/* allocate login_mtask used for the login/text sequences */
	spin_lock_bh(&session->lock);
	if (!__kfifo_get(session->mgmtpool.queue,
                         (void*)&conn->login_mtask,
			 sizeof(void*))) {
		spin_unlock_bh(&session->lock);
		goto login_mtask_alloc_fail;
	}
	spin_unlock_bh(&session->lock);

	data = kmalloc(ISCSI_DEF_MAX_RECV_SEG_LEN, GFP_KERNEL);
	if (!data)
		goto login_mtask_data_alloc_fail;
	conn->login_mtask->data = conn->data = data;

	init_timer(&conn->tmabort_timer);
	init_waitqueue_head(&conn->ehwait);

	return cls_conn;

login_mtask_data_alloc_fail:
	__kfifo_put(session->mgmtpool.queue, (void*)&conn->login_mtask,
		    sizeof(void*));
login_mtask_alloc_fail:
	kfifo_free(conn->mgmtqueue);
mgmtqueue_alloc_fail:
	iscsi_destroy_conn(cls_conn);
	return NULL;
}
EXPORT_SYMBOL_GPL(iscsi_conn_setup);

/**
 * iscsi_conn_teardown - teardown iscsi connection
 * cls_conn: iscsi class connection
 *
 * TODO: we may need to make this into a two step process
 * like scsi-mls remove + put host
 */
void iscsi_conn_teardown(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	unsigned long flags;

	spin_lock_bh(&session->lock);
	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	conn->c_stage = ISCSI_CONN_CLEANUP_WAIT;
	if (session->leadconn == conn) {
		/*
		 * leading connection? then give up on recovery.
		 */
		session->state = ISCSI_STATE_TERMINATE;
		wake_up(&conn->ehwait);
	}
	spin_unlock_bh(&session->lock);

	/*
	 * Block until all in-progress commands for this connection
	 * time out or fail.
	 */
	for (;;) {
		spin_lock_irqsave(session->host->host_lock, flags);
		if (!session->host->host_busy) { /* OK for ERL == 0 */
			spin_unlock_irqrestore(session->host->host_lock, flags);
			break;
		}
		spin_unlock_irqrestore(session->host->host_lock, flags);
		msleep_interruptible(500);
		printk(KERN_INFO "iscsi: scsi conn_destroy(): host_busy %d "
		       "host_failed %d\n", session->host->host_busy,
		       session->host->host_failed);
		/*
		 * force eh_abort() to unblock
		 */
		wake_up(&conn->ehwait);
	}

	/* flush queued up work because we free the connection below */
	scsi_flush_work(session->host);

	spin_lock_bh(&session->lock);
	kfree(conn->data);
	kfree(conn->persistent_address);
	__kfifo_put(session->mgmtpool.queue, (void*)&conn->login_mtask,
		    sizeof(void*));
	if (session->leadconn == conn)
		session->leadconn = NULL;
	spin_unlock_bh(&session->lock);

	kfifo_free(conn->mgmtqueue);

	iscsi_destroy_conn(cls_conn);
}
EXPORT_SYMBOL_GPL(iscsi_conn_teardown);

int iscsi_conn_start(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;

	if (!session) {
		printk(KERN_ERR "iscsi: can't start unbound connection\n");
		return -EPERM;
	}

	if ((session->imm_data_en || !session->initial_r2t_en) &&
	     session->first_burst > session->max_burst) {
		printk("iscsi: invalid burst lengths: "
		       "first_burst %d max_burst %d\n",
		       session->first_burst, session->max_burst);
		return -EINVAL;
	}

	spin_lock_bh(&session->lock);
	conn->c_stage = ISCSI_CONN_STARTED;
	session->state = ISCSI_STATE_LOGGED_IN;
	session->queued_cmdsn = session->cmdsn;

	switch(conn->stop_stage) {
	case STOP_CONN_RECOVER:
		/*
		 * unblock eh_abort() if it is blocked. re-try all
		 * commands after successful recovery
		 */
		conn->stop_stage = 0;
		conn->tmabort_state = TMABORT_INITIAL;
		session->age++;
		spin_unlock_bh(&session->lock);

		iscsi_unblock_session(session_to_cls(session));
		wake_up(&conn->ehwait);
		return 0;
	case STOP_CONN_TERM:
		conn->stop_stage = 0;
		break;
	default:
		break;
	}
	spin_unlock_bh(&session->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_conn_start);

static void
flush_control_queues(struct iscsi_session *session, struct iscsi_conn *conn)
{
	struct iscsi_mgmt_task *mtask, *tmp;

	/* handle pending */
	while (__kfifo_get(conn->mgmtqueue, (void*)&mtask, sizeof(void*))) {
		if (mtask == conn->login_mtask)
			continue;
		debug_scsi("flushing pending mgmt task itt 0x%x\n", mtask->itt);
		__kfifo_put(session->mgmtpool.queue, (void*)&mtask,
			    sizeof(void*));
	}

	/* handle running */
	list_for_each_entry_safe(mtask, tmp, &conn->mgmt_run_list, running) {
		debug_scsi("flushing running mgmt task itt 0x%x\n", mtask->itt);
		list_del(&mtask->running);

		if (mtask == conn->login_mtask)
			continue;
		__kfifo_put(session->mgmtpool.queue, (void*)&mtask,
			   sizeof(void*));
	}

	conn->mtask = NULL;
}

/* Fail commands. Mutex and session lock held and recv side suspended */
static void fail_all_commands(struct iscsi_conn *conn)
{
	struct iscsi_cmd_task *ctask, *tmp;

	/* flush pending */
	list_for_each_entry_safe(ctask, tmp, &conn->xmitqueue, running) {
		debug_scsi("failing pending sc %p itt 0x%x\n", ctask->sc,
			   ctask->itt);
		fail_command(conn, ctask, DID_BUS_BUSY << 16);
	}

	/* fail all other running */
	list_for_each_entry_safe(ctask, tmp, &conn->run_list, running) {
		debug_scsi("failing in progress sc %p itt 0x%x\n",
			   ctask->sc, ctask->itt);
		fail_command(conn, ctask, DID_BUS_BUSY << 16);
	}

	conn->ctask = NULL;
}

static void iscsi_start_session_recovery(struct iscsi_session *session,
					 struct iscsi_conn *conn, int flag)
{
	int old_stop_stage;

	spin_lock_bh(&session->lock);
	if (conn->stop_stage == STOP_CONN_TERM) {
		spin_unlock_bh(&session->lock);
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
	conn->c_stage = ISCSI_CONN_STOPPED;
	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	spin_unlock_bh(&session->lock);
	scsi_flush_work(session->host);

	write_lock_bh(conn->recv_lock);
	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_rx);
	write_unlock_bh(conn->recv_lock);

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
			debug_scsi("blocking session\n");
			iscsi_block_session(session_to_cls(session));
		}
	}

	/*
	 * flush queues.
	 */
	spin_lock_bh(&session->lock);
	fail_all_commands(conn);
	flush_control_queues(session, conn);
	spin_unlock_bh(&session->lock);
}

void iscsi_conn_stop(struct iscsi_cls_conn *cls_conn, int flag)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;

	switch (flag) {
	case STOP_CONN_RECOVER:
	case STOP_CONN_TERM:
		iscsi_start_session_recovery(session, conn, flag);
		break;
	default:
		printk(KERN_ERR "iscsi: invalid stop flag %d\n", flag);
	}
}
EXPORT_SYMBOL_GPL(iscsi_conn_stop);

int iscsi_conn_bind(struct iscsi_cls_session *cls_session,
		    struct iscsi_cls_conn *cls_conn, int is_leading)
{
	struct iscsi_session *session = class_to_transport_session(cls_session);
	struct iscsi_conn *conn = cls_conn->dd_data;

	spin_lock_bh(&session->lock);
	if (is_leading)
		session->leadconn = conn;
	spin_unlock_bh(&session->lock);

	/*
	 * Unblock xmitworker(), Login Phase will pass through.
	 */
	clear_bit(ISCSI_SUSPEND_BIT, &conn->suspend_rx);
	clear_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_conn_bind);


int iscsi_set_param(struct iscsi_cls_conn *cls_conn,
		    enum iscsi_param param, char *buf, int buflen)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	uint32_t value;

	switch(param) {
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
		sscanf(buf, "%d", &session->max_r2t);
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
	case ISCSI_PARAM_IFMARKER_EN:
		sscanf(buf, "%d", &value);
		BUG_ON(value);
		break;
	case ISCSI_PARAM_OFMARKER_EN:
		sscanf(buf, "%d", &value);
		BUG_ON(value);
		break;
	case ISCSI_PARAM_EXP_STATSN:
		sscanf(buf, "%u", &conn->exp_statsn);
		break;
	case ISCSI_PARAM_USERNAME:
		kfree(session->username);
		session->username = kstrdup(buf, GFP_KERNEL);
		if (!session->username)
			return -ENOMEM;
		break;
	case ISCSI_PARAM_USERNAME_IN:
		kfree(session->username_in);
		session->username_in = kstrdup(buf, GFP_KERNEL);
		if (!session->username_in)
			return -ENOMEM;
		break;
	case ISCSI_PARAM_PASSWORD:
		kfree(session->password);
		session->password = kstrdup(buf, GFP_KERNEL);
		if (!session->password)
			return -ENOMEM;
		break;
	case ISCSI_PARAM_PASSWORD_IN:
		kfree(session->password_in);
		session->password_in = kstrdup(buf, GFP_KERNEL);
		if (!session->password_in)
			return -ENOMEM;
		break;
	case ISCSI_PARAM_TARGET_NAME:
		/* this should not change between logins */
		if (session->targetname)
			break;

		session->targetname = kstrdup(buf, GFP_KERNEL);
		if (!session->targetname)
			return -ENOMEM;
		break;
	case ISCSI_PARAM_TPGT:
		sscanf(buf, "%d", &session->tpgt);
		break;
	case ISCSI_PARAM_PERSISTENT_PORT:
		sscanf(buf, "%d", &conn->persistent_port);
		break;
	case ISCSI_PARAM_PERSISTENT_ADDRESS:
		/*
		 * this is the address returned in discovery so it should
		 * not change between logins.
		 */
		if (conn->persistent_address)
			break;

		conn->persistent_address = kstrdup(buf, GFP_KERNEL);
		if (!conn->persistent_address)
			return -ENOMEM;
		break;
	default:
		return -ENOSYS;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_set_param);

int iscsi_session_get_param(struct iscsi_cls_session *cls_session,
			    enum iscsi_param param, char *buf)
{
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);
	struct iscsi_session *session = iscsi_hostdata(shost->hostdata);
	int len;

	switch(param) {
	case ISCSI_PARAM_INITIAL_R2T_EN:
		len = sprintf(buf, "%d\n", session->initial_r2t_en);
		break;
	case ISCSI_PARAM_MAX_R2T:
		len = sprintf(buf, "%hu\n", session->max_r2t);
		break;
	case ISCSI_PARAM_IMM_DATA_EN:
		len = sprintf(buf, "%d\n", session->imm_data_en);
		break;
	case ISCSI_PARAM_FIRST_BURST:
		len = sprintf(buf, "%u\n", session->first_burst);
		break;
	case ISCSI_PARAM_MAX_BURST:
		len = sprintf(buf, "%u\n", session->max_burst);
		break;
	case ISCSI_PARAM_PDU_INORDER_EN:
		len = sprintf(buf, "%d\n", session->pdu_inorder_en);
		break;
	case ISCSI_PARAM_DATASEQ_INORDER_EN:
		len = sprintf(buf, "%d\n", session->dataseq_inorder_en);
		break;
	case ISCSI_PARAM_ERL:
		len = sprintf(buf, "%d\n", session->erl);
		break;
	case ISCSI_PARAM_TARGET_NAME:
		len = sprintf(buf, "%s\n", session->targetname);
		break;
	case ISCSI_PARAM_TPGT:
		len = sprintf(buf, "%d\n", session->tpgt);
		break;
	case ISCSI_PARAM_USERNAME:
		len = sprintf(buf, "%s\n", session->username);
		break;
	case ISCSI_PARAM_USERNAME_IN:
		len = sprintf(buf, "%s\n", session->username_in);
		break;
	case ISCSI_PARAM_PASSWORD:
		len = sprintf(buf, "%s\n", session->password);
		break;
	case ISCSI_PARAM_PASSWORD_IN:
		len = sprintf(buf, "%s\n", session->password_in);
		break;
	default:
		return -ENOSYS;
	}

	return len;
}
EXPORT_SYMBOL_GPL(iscsi_session_get_param);

int iscsi_conn_get_param(struct iscsi_cls_conn *cls_conn,
			 enum iscsi_param param, char *buf)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	int len;

	switch(param) {
	case ISCSI_PARAM_MAX_RECV_DLENGTH:
		len = sprintf(buf, "%u\n", conn->max_recv_dlength);
		break;
	case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		len = sprintf(buf, "%u\n", conn->max_xmit_dlength);
		break;
	case ISCSI_PARAM_HDRDGST_EN:
		len = sprintf(buf, "%d\n", conn->hdrdgst_en);
		break;
	case ISCSI_PARAM_DATADGST_EN:
		len = sprintf(buf, "%d\n", conn->datadgst_en);
		break;
	case ISCSI_PARAM_IFMARKER_EN:
		len = sprintf(buf, "%d\n", conn->ifmarker_en);
		break;
	case ISCSI_PARAM_OFMARKER_EN:
		len = sprintf(buf, "%d\n", conn->ofmarker_en);
		break;
	case ISCSI_PARAM_EXP_STATSN:
		len = sprintf(buf, "%u\n", conn->exp_statsn);
		break;
	case ISCSI_PARAM_PERSISTENT_PORT:
		len = sprintf(buf, "%d\n", conn->persistent_port);
		break;
	case ISCSI_PARAM_PERSISTENT_ADDRESS:
		len = sprintf(buf, "%s\n", conn->persistent_address);
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
	struct iscsi_session *session = iscsi_hostdata(shost->hostdata);
	int len;

	switch (param) {
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		if (!session->netdev)
			len = sprintf(buf, "%s\n", "default");
		else
			len = sprintf(buf, "%s\n", session->netdev);
		break;
	case ISCSI_HOST_PARAM_HWADDRESS:
		if (!session->hwaddress)
			len = sprintf(buf, "%s\n", "default");
		else
			len = sprintf(buf, "%s\n", session->hwaddress);
		break;
	case ISCSI_HOST_PARAM_INITIATOR_NAME:
		if (!session->initiatorname)
			len = sprintf(buf, "%s\n", "unknown");
		else
			len = sprintf(buf, "%s\n", session->initiatorname);
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
	struct iscsi_session *session = iscsi_hostdata(shost->hostdata);

	switch (param) {
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		if (!session->netdev)
			session->netdev = kstrdup(buf, GFP_KERNEL);
		break;
	case ISCSI_HOST_PARAM_HWADDRESS:
		if (!session->hwaddress)
			session->hwaddress = kstrdup(buf, GFP_KERNEL);
		break;
	case ISCSI_HOST_PARAM_INITIATOR_NAME:
		if (!session->initiatorname)
			session->initiatorname = kstrdup(buf, GFP_KERNEL);
		break;
	default:
		return -ENOSYS;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_host_set_param);

MODULE_AUTHOR("Mike Christie");
MODULE_DESCRIPTION("iSCSI library functions");
MODULE_LICENSE("GPL");
