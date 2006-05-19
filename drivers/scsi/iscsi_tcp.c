/*
 * iSCSI Initiator over TCP/IP Data-Path
 *
 * Copyright (C) 2004 Dmitry Yusupov
 * Copyright (C) 2004 Alex Aizman
 * Copyright (C) 2005 - 2006 Mike Christie
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * maintained by open-iscsi@googlegroups.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 *
 * Credits:
 *	Christoph Hellwig
 *	FUJITA Tomonori
 *	Arne Redlich
 *	Zhenyu Wang
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/inet.h>
#include <linux/blkdev.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/scatterlist.h>
#include <linux/mutex.h>
#include <net/tcp.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/scsi_transport_iscsi.h>

#include "iscsi_tcp.h"

#define ISCSI_TCP_VERSION "1.0-574"

MODULE_AUTHOR("Dmitry Yusupov <dmitry_yus@yahoo.com>, "
	      "Alex Aizman <itn780@yahoo.com>");
MODULE_DESCRIPTION("iSCSI/TCP data-path");
MODULE_LICENSE("GPL");
MODULE_VERSION(ISCSI_TCP_VERSION);
/* #define DEBUG_TCP */
#define DEBUG_ASSERT

#ifdef DEBUG_TCP
#define debug_tcp(fmt...) printk(KERN_INFO "tcp: " fmt)
#else
#define debug_tcp(fmt...)
#endif

#ifndef DEBUG_ASSERT
#ifdef BUG_ON
#undef BUG_ON
#endif
#define BUG_ON(expr)
#endif

static unsigned int iscsi_max_lun = 512;
module_param_named(max_lun, iscsi_max_lun, uint, S_IRUGO);

static inline void
iscsi_buf_init_iov(struct iscsi_buf *ibuf, char *vbuf, int size)
{
	ibuf->sg.page = virt_to_page(vbuf);
	ibuf->sg.offset = offset_in_page(vbuf);
	ibuf->sg.length = size;
	ibuf->sent = 0;
	ibuf->use_sendmsg = 1;
}

static inline void
iscsi_buf_init_sg(struct iscsi_buf *ibuf, struct scatterlist *sg)
{
	ibuf->sg.page = sg->page;
	ibuf->sg.offset = sg->offset;
	ibuf->sg.length = sg->length;
	/*
	 * Fastpath: sg element fits into single page
	 */
	if (sg->length + sg->offset <= PAGE_SIZE && !PageSlab(sg->page))
		ibuf->use_sendmsg = 0;
	else
		ibuf->use_sendmsg = 1;
	ibuf->sent = 0;
}

static inline int
iscsi_buf_left(struct iscsi_buf *ibuf)
{
	int rc;

	rc = ibuf->sg.length - ibuf->sent;
	BUG_ON(rc < 0);
	return rc;
}

static inline void
iscsi_hdr_digest(struct iscsi_conn *conn, struct iscsi_buf *buf,
		 u8* crc)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	crypto_digest_digest(tcp_conn->tx_tfm, &buf->sg, 1, crc);
	buf->sg.length += sizeof(uint32_t);
}

static inline int
iscsi_hdr_extract(struct iscsi_tcp_conn *tcp_conn)
{
	struct sk_buff *skb = tcp_conn->in.skb;

	tcp_conn->in.zero_copy_hdr = 0;

	if (tcp_conn->in.copy >= tcp_conn->hdr_size &&
	    tcp_conn->in_progress == IN_PROGRESS_WAIT_HEADER) {
		/*
		 * Zero-copy PDU Header: using connection context
		 * to store header pointer.
		 */
		if (skb_shinfo(skb)->frag_list == NULL &&
		    !skb_shinfo(skb)->nr_frags) {
			tcp_conn->in.hdr = (struct iscsi_hdr *)
				((char*)skb->data + tcp_conn->in.offset);
			tcp_conn->in.zero_copy_hdr = 1;
		} else {
			/* ignoring return code since we checked
			 * in.copy before */
			skb_copy_bits(skb, tcp_conn->in.offset,
				&tcp_conn->hdr, tcp_conn->hdr_size);
			tcp_conn->in.hdr = &tcp_conn->hdr;
		}
		tcp_conn->in.offset += tcp_conn->hdr_size;
		tcp_conn->in.copy -= tcp_conn->hdr_size;
	} else {
		int hdr_remains;
		int copylen;

		/*
		 * PDU header scattered across SKB's,
		 * copying it... This'll happen quite rarely.
		 */

		if (tcp_conn->in_progress == IN_PROGRESS_WAIT_HEADER)
			tcp_conn->in.hdr_offset = 0;

		hdr_remains = tcp_conn->hdr_size - tcp_conn->in.hdr_offset;
		BUG_ON(hdr_remains <= 0);

		copylen = min(tcp_conn->in.copy, hdr_remains);
		skb_copy_bits(skb, tcp_conn->in.offset,
			(char*)&tcp_conn->hdr + tcp_conn->in.hdr_offset,
			copylen);

		debug_tcp("PDU gather offset %d bytes %d in.offset %d "
		       "in.copy %d\n", tcp_conn->in.hdr_offset, copylen,
		       tcp_conn->in.offset, tcp_conn->in.copy);

		tcp_conn->in.offset += copylen;
		tcp_conn->in.copy -= copylen;
		if (copylen < hdr_remains)  {
			tcp_conn->in_progress = IN_PROGRESS_HEADER_GATHER;
			tcp_conn->in.hdr_offset += copylen;
		        return -EAGAIN;
		}
		tcp_conn->in.hdr = &tcp_conn->hdr;
		tcp_conn->discontiguous_hdr_cnt++;
	        tcp_conn->in_progress = IN_PROGRESS_WAIT_HEADER;
	}

	return 0;
}

/*
 * must be called with session lock
 */
static void
__iscsi_ctask_cleanup(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct scsi_cmnd *sc;

	sc = ctask->sc;
	if (unlikely(!sc))
		return;

	tcp_ctask->xmstate = XMSTATE_IDLE;
	tcp_ctask->r2t = NULL;
}

/**
 * iscsi_data_rsp - SCSI Data-In Response processing
 * @conn: iscsi connection
 * @ctask: scsi command task
 **/
static int
iscsi_data_rsp(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	int rc;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_data_rsp *rhdr = (struct iscsi_data_rsp *)tcp_conn->in.hdr;
	struct iscsi_session *session = conn->session;
	int datasn = be32_to_cpu(rhdr->datasn);

	rc = iscsi_check_assign_cmdsn(session, (struct iscsi_nopin*)rhdr);
	if (rc)
		return rc;
	/*
	 * setup Data-In byte counter (gets decremented..)
	 */
	ctask->data_count = tcp_conn->in.datalen;

	if (tcp_conn->in.datalen == 0)
		return 0;

	if (ctask->datasn != datasn)
		return ISCSI_ERR_DATASN;

	ctask->datasn++;

	tcp_ctask->data_offset = be32_to_cpu(rhdr->offset);
	if (tcp_ctask->data_offset + tcp_conn->in.datalen > ctask->total_length)
		return ISCSI_ERR_DATA_OFFSET;

	if (rhdr->flags & ISCSI_FLAG_DATA_STATUS) {
		struct scsi_cmnd *sc = ctask->sc;

		conn->exp_statsn = be32_to_cpu(rhdr->statsn) + 1;
		if (rhdr->flags & ISCSI_FLAG_DATA_UNDERFLOW) {
			int res_count = be32_to_cpu(rhdr->residual_count);

			if (res_count > 0 &&
			    res_count <= sc->request_bufflen) {
				sc->resid = res_count;
				sc->result = (DID_OK << 16) | rhdr->cmd_status;
			} else
				sc->result = (DID_BAD_TARGET << 16) |
					rhdr->cmd_status;
		} else if (rhdr->flags & ISCSI_FLAG_DATA_OVERFLOW) {
			sc->resid = be32_to_cpu(rhdr->residual_count);
			sc->result = (DID_OK << 16) | rhdr->cmd_status;
		} else
			sc->result = (DID_OK << 16) | rhdr->cmd_status;
	}

	conn->datain_pdus_cnt++;
	return 0;
}

/**
 * iscsi_solicit_data_init - initialize first Data-Out
 * @conn: iscsi connection
 * @ctask: scsi command task
 * @r2t: R2T info
 *
 * Notes:
 *	Initialize first Data-Out within this R2T sequence and finds
 *	proper data_offset within this SCSI command.
 *
 *	This function is called with connection lock taken.
 **/
static void
iscsi_solicit_data_init(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask,
			struct iscsi_r2t_info *r2t)
{
	struct iscsi_data *hdr;
	struct scsi_cmnd *sc = ctask->sc;
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

	hdr = &r2t->dtask.hdr;
	memset(hdr, 0, sizeof(struct iscsi_data));
	hdr->ttt = r2t->ttt;
	hdr->datasn = cpu_to_be32(r2t->solicit_datasn);
	r2t->solicit_datasn++;
	hdr->opcode = ISCSI_OP_SCSI_DATA_OUT;
	memcpy(hdr->lun, ctask->hdr->lun, sizeof(hdr->lun));
	hdr->itt = ctask->hdr->itt;
	hdr->exp_statsn = r2t->exp_statsn;
	hdr->offset = cpu_to_be32(r2t->data_offset);
	if (r2t->data_length > conn->max_xmit_dlength) {
		hton24(hdr->dlength, conn->max_xmit_dlength);
		r2t->data_count = conn->max_xmit_dlength;
		hdr->flags = 0;
	} else {
		hton24(hdr->dlength, r2t->data_length);
		r2t->data_count = r2t->data_length;
		hdr->flags = ISCSI_FLAG_CMD_FINAL;
	}
	conn->dataout_pdus_cnt++;

	r2t->sent = 0;

	iscsi_buf_init_iov(&r2t->headbuf, (char*)hdr,
			   sizeof(struct iscsi_hdr));

	if (sc->use_sg) {
		int i, sg_count = 0;
		struct scatterlist *sg = sc->request_buffer;

		r2t->sg = NULL;
		for (i = 0; i < sc->use_sg; i++, sg += 1) {
			/* FIXME: prefetch ? */
			if (sg_count + sg->length > r2t->data_offset) {
				int page_offset;

				/* sg page found! */

				/* offset within this page */
				page_offset = r2t->data_offset - sg_count;

				/* fill in this buffer */
				iscsi_buf_init_sg(&r2t->sendbuf, sg);
				r2t->sendbuf.sg.offset += page_offset;
				r2t->sendbuf.sg.length -= page_offset;

				/* xmit logic will continue with next one */
				r2t->sg = sg + 1;
				break;
			}
			sg_count += sg->length;
		}
		BUG_ON(r2t->sg == NULL);
	} else
		iscsi_buf_init_iov(&tcp_ctask->sendbuf,
			    (char*)sc->request_buffer + r2t->data_offset,
			    r2t->data_count);
}

/**
 * iscsi_r2t_rsp - iSCSI R2T Response processing
 * @conn: iscsi connection
 * @ctask: scsi command task
 **/
static int
iscsi_r2t_rsp(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_r2t_info *r2t;
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_r2t_rsp *rhdr = (struct iscsi_r2t_rsp *)tcp_conn->in.hdr;
	int r2tsn = be32_to_cpu(rhdr->r2tsn);
	int rc;

	if (tcp_conn->in.datalen)
		return ISCSI_ERR_DATALEN;

	if (tcp_ctask->exp_r2tsn && tcp_ctask->exp_r2tsn != r2tsn)
		return ISCSI_ERR_R2TSN;

	rc = iscsi_check_assign_cmdsn(session, (struct iscsi_nopin*)rhdr);
	if (rc)
		return rc;

	/* FIXME: use R2TSN to detect missing R2T */

	/* fill-in new R2T associated with the task */
	spin_lock(&session->lock);
	if (!ctask->sc || ctask->mtask ||
	     session->state != ISCSI_STATE_LOGGED_IN) {
		printk(KERN_INFO "iscsi_tcp: dropping R2T itt %d in "
		       "recovery...\n", ctask->itt);
		spin_unlock(&session->lock);
		return 0;
	}
	rc = __kfifo_get(tcp_ctask->r2tpool.queue, (void*)&r2t, sizeof(void*));
	BUG_ON(!rc);

	r2t->exp_statsn = rhdr->statsn;
	r2t->data_length = be32_to_cpu(rhdr->data_length);
	if (r2t->data_length == 0 ||
	    r2t->data_length > session->max_burst) {
		spin_unlock(&session->lock);
		return ISCSI_ERR_DATALEN;
	}

	r2t->data_offset = be32_to_cpu(rhdr->data_offset);
	if (r2t->data_offset + r2t->data_length > ctask->total_length) {
		spin_unlock(&session->lock);
		return ISCSI_ERR_DATALEN;
	}

	r2t->ttt = rhdr->ttt; /* no flip */
	r2t->solicit_datasn = 0;

	iscsi_solicit_data_init(conn, ctask, r2t);

	tcp_ctask->exp_r2tsn = r2tsn + 1;
	tcp_ctask->xmstate |= XMSTATE_SOL_HDR;
	__kfifo_put(tcp_ctask->r2tqueue, (void*)&r2t, sizeof(void*));
	__kfifo_put(conn->xmitqueue, (void*)&ctask, sizeof(void*));

	scsi_queue_work(session->host, &conn->xmitwork);
	conn->r2t_pdus_cnt++;
	spin_unlock(&session->lock);

	return 0;
}

static int
iscsi_tcp_hdr_recv(struct iscsi_conn *conn)
{
	int rc = 0, opcode, ahslen;
	struct iscsi_hdr *hdr;
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	uint32_t cdgst, rdgst = 0, itt;

	hdr = tcp_conn->in.hdr;

	/* verify PDU length */
	tcp_conn->in.datalen = ntoh24(hdr->dlength);
	if (tcp_conn->in.datalen > conn->max_recv_dlength) {
		printk(KERN_ERR "iscsi_tcp: datalen %d > %d\n",
		       tcp_conn->in.datalen, conn->max_recv_dlength);
		return ISCSI_ERR_DATALEN;
	}
	tcp_conn->data_copied = 0;

	/* read AHS */
	ahslen = hdr->hlength << 2;
	tcp_conn->in.offset += ahslen;
	tcp_conn->in.copy -= ahslen;
	if (tcp_conn->in.copy < 0) {
		printk(KERN_ERR "iscsi_tcp: can't handle AHS with length "
		       "%d bytes\n", ahslen);
		return ISCSI_ERR_AHSLEN;
	}

	/* calculate read padding */
	tcp_conn->in.padding = tcp_conn->in.datalen & (ISCSI_PAD_LEN-1);
	if (tcp_conn->in.padding) {
		tcp_conn->in.padding = ISCSI_PAD_LEN - tcp_conn->in.padding;
		debug_scsi("read padding %d bytes\n", tcp_conn->in.padding);
	}

	if (conn->hdrdgst_en) {
		struct scatterlist sg;

		sg_init_one(&sg, (u8 *)hdr,
			    sizeof(struct iscsi_hdr) + ahslen);
		crypto_digest_digest(tcp_conn->rx_tfm, &sg, 1, (u8 *)&cdgst);
		rdgst = *(uint32_t*)((char*)hdr + sizeof(struct iscsi_hdr) +
				     ahslen);
		if (cdgst != rdgst) {
			printk(KERN_ERR "iscsi_tcp: hdrdgst error "
			       "recv 0x%x calc 0x%x\n", rdgst, cdgst);
			return ISCSI_ERR_HDR_DGST;
		}
	}

	opcode = hdr->opcode & ISCSI_OPCODE_MASK;
	/* verify itt (itt encoding: age+cid+itt) */
	rc = iscsi_verify_itt(conn, hdr, &itt);
	if (rc == ISCSI_ERR_NO_SCSI_CMD) {
		tcp_conn->in.datalen = 0; /* force drop */
		return 0;
	} else if (rc)
		return rc;

	debug_tcp("opcode 0x%x offset %d copy %d ahslen %d datalen %d\n",
		  opcode, tcp_conn->in.offset, tcp_conn->in.copy,
		  ahslen, tcp_conn->in.datalen);

	switch(opcode) {
	case ISCSI_OP_SCSI_DATA_IN:
		tcp_conn->in.ctask = session->cmds[itt];
		rc = iscsi_data_rsp(conn, tcp_conn->in.ctask);
		/* fall through */
	case ISCSI_OP_SCSI_CMD_RSP:
		tcp_conn->in.ctask = session->cmds[itt];
		if (tcp_conn->in.datalen)
			goto copy_hdr;

		spin_lock(&session->lock);
		__iscsi_ctask_cleanup(conn, tcp_conn->in.ctask);
		rc = __iscsi_complete_pdu(conn, hdr, NULL, 0);
		spin_unlock(&session->lock);
		break;
	case ISCSI_OP_R2T:
		tcp_conn->in.ctask = session->cmds[itt];
		if (ahslen)
			rc = ISCSI_ERR_AHSLEN;
		else if (tcp_conn->in.ctask->sc->sc_data_direction ==
								DMA_TO_DEVICE)
			rc = iscsi_r2t_rsp(conn, tcp_conn->in.ctask);
		else
			rc = ISCSI_ERR_PROTO;
		break;
	case ISCSI_OP_LOGIN_RSP:
	case ISCSI_OP_TEXT_RSP:
	case ISCSI_OP_LOGOUT_RSP:
	case ISCSI_OP_NOOP_IN:
	case ISCSI_OP_REJECT:
	case ISCSI_OP_ASYNC_EVENT:
		if (tcp_conn->in.datalen)
			goto copy_hdr;
	/* fall through */
	case ISCSI_OP_SCSI_TMFUNC_RSP:
		rc = iscsi_complete_pdu(conn, hdr, NULL, 0);
		break;
	default:
		rc = ISCSI_ERR_BAD_OPCODE;
		break;
	}

	return rc;

copy_hdr:
	/*
	 * if we did zero copy for the header but we will need multiple
	 * skbs to complete the command then we have to copy the header
	 * for later use
	 */
	if (tcp_conn->in.zero_copy_hdr && tcp_conn->in.copy <
	   (tcp_conn->in.datalen + tcp_conn->in.padding +
	    (conn->datadgst_en ? 4 : 0))) {
		debug_tcp("Copying header for later use. in.copy %d in.datalen"
			  " %d\n", tcp_conn->in.copy, tcp_conn->in.datalen);
		memcpy(&tcp_conn->hdr, tcp_conn->in.hdr,
		       sizeof(struct iscsi_hdr));
		tcp_conn->in.hdr = &tcp_conn->hdr;
		tcp_conn->in.zero_copy_hdr = 0;
	}
	return 0;
}

/**
 * iscsi_ctask_copy - copy skb bits to the destanation cmd task
 * @conn: iscsi tcp connection
 * @ctask: scsi command task
 * @buf: buffer to copy to
 * @buf_size: size of buffer
 * @offset: offset within the buffer
 *
 * Notes:
 *	The function calls skb_copy_bits() and updates per-connection and
 *	per-cmd byte counters.
 *
 *	Read counters (in bytes):
 *
 *	conn->in.offset		offset within in progress SKB
 *	conn->in.copy		left to copy from in progress SKB
 *				including padding
 *	conn->in.copied		copied already from in progress SKB
 *	conn->data_copied	copied already from in progress buffer
 *	ctask->sent		total bytes sent up to the MidLayer
 *	ctask->data_count	left to copy from in progress Data-In
 *	buf_left		left to copy from in progress buffer
 **/
static inline int
iscsi_ctask_copy(struct iscsi_tcp_conn *tcp_conn, struct iscsi_cmd_task *ctask,
		void *buf, int buf_size, int offset)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	int buf_left = buf_size - (tcp_conn->data_copied + offset);
	int size = min(tcp_conn->in.copy, buf_left);
	int rc;

	size = min(size, ctask->data_count);

	debug_tcp("ctask_copy %d bytes at offset %d copied %d\n",
	       size, tcp_conn->in.offset, tcp_conn->in.copied);

	BUG_ON(size <= 0);
	BUG_ON(tcp_ctask->sent + size > ctask->total_length);

	rc = skb_copy_bits(tcp_conn->in.skb, tcp_conn->in.offset,
			   (char*)buf + (offset + tcp_conn->data_copied), size);
	/* must fit into skb->len */
	BUG_ON(rc);

	tcp_conn->in.offset += size;
	tcp_conn->in.copy -= size;
	tcp_conn->in.copied += size;
	tcp_conn->data_copied += size;
	tcp_ctask->sent += size;
	ctask->data_count -= size;

	BUG_ON(tcp_conn->in.copy < 0);
	BUG_ON(ctask->data_count < 0);

	if (buf_size != (tcp_conn->data_copied + offset)) {
		if (!ctask->data_count) {
			BUG_ON(buf_size - tcp_conn->data_copied < 0);
			/* done with this PDU */
			return buf_size - tcp_conn->data_copied;
		}
		return -EAGAIN;
	}

	/* done with this buffer or with both - PDU and buffer */
	tcp_conn->data_copied = 0;
	return 0;
}

/**
 * iscsi_tcp_copy - copy skb bits to the destanation buffer
 * @conn: iscsi tcp connection
 *
 * Notes:
 *	The function calls skb_copy_bits() and updates per-connection
 *	byte counters.
 **/
static inline int
iscsi_tcp_copy(struct iscsi_tcp_conn *tcp_conn)
{
	void *buf = tcp_conn->data;
	int buf_size = tcp_conn->in.datalen;
	int buf_left = buf_size - tcp_conn->data_copied;
	int size = min(tcp_conn->in.copy, buf_left);
	int rc;

	debug_tcp("tcp_copy %d bytes at offset %d copied %d\n",
	       size, tcp_conn->in.offset, tcp_conn->data_copied);
	BUG_ON(size <= 0);

	rc = skb_copy_bits(tcp_conn->in.skb, tcp_conn->in.offset,
			   (char*)buf + tcp_conn->data_copied, size);
	BUG_ON(rc);

	tcp_conn->in.offset += size;
	tcp_conn->in.copy -= size;
	tcp_conn->in.copied += size;
	tcp_conn->data_copied += size;

	if (buf_size != tcp_conn->data_copied)
		return -EAGAIN;

	return 0;
}

static inline void
partial_sg_digest_update(struct iscsi_tcp_conn *tcp_conn,
			 struct scatterlist *sg, int offset, int length)
{
	struct scatterlist temp;

	memcpy(&temp, sg, sizeof(struct scatterlist));
	temp.offset = offset;
	temp.length = length;
	crypto_digest_update(tcp_conn->data_rx_tfm, &temp, 1);
}

static void
iscsi_recv_digest_update(struct iscsi_tcp_conn *tcp_conn, char* buf, int len)
{
	struct scatterlist tmp;

	sg_init_one(&tmp, buf, len);
	crypto_digest_update(tcp_conn->data_rx_tfm, &tmp, 1);
}

static int iscsi_scsi_data_in(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_cmd_task *ctask = tcp_conn->in.ctask;
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct scsi_cmnd *sc = ctask->sc;
	struct scatterlist *sg;
	int i, offset, rc = 0;

	BUG_ON((void*)ctask != sc->SCp.ptr);

	/*
	 * copying Data-In into the Scsi_Cmnd
	 */
	if (!sc->use_sg) {
		i = ctask->data_count;
		rc = iscsi_ctask_copy(tcp_conn, ctask, sc->request_buffer,
				      sc->request_bufflen,
				      tcp_ctask->data_offset);
		if (rc == -EAGAIN)
			return rc;
		if (conn->datadgst_en)
			iscsi_recv_digest_update(tcp_conn, sc->request_buffer,
						 i);
		rc = 0;
		goto done;
	}

	offset = tcp_ctask->data_offset;
	sg = sc->request_buffer;

	if (tcp_ctask->data_offset)
		for (i = 0; i < tcp_ctask->sg_count; i++)
			offset -= sg[i].length;
	/* we've passed through partial sg*/
	if (offset < 0)
		offset = 0;

	for (i = tcp_ctask->sg_count; i < sc->use_sg; i++) {
		char *dest;

		dest = kmap_atomic(sg[i].page, KM_SOFTIRQ0);
		rc = iscsi_ctask_copy(tcp_conn, ctask, dest + sg[i].offset,
				      sg[i].length, offset);
		kunmap_atomic(dest, KM_SOFTIRQ0);
		if (rc == -EAGAIN)
			/* continue with the next SKB/PDU */
			return rc;
		if (!rc) {
			if (conn->datadgst_en) {
				if (!offset)
					crypto_digest_update(
							tcp_conn->data_rx_tfm,
							&sg[i], 1);
				else
					partial_sg_digest_update(tcp_conn,
							&sg[i],
							sg[i].offset + offset,
							sg[i].length - offset);
			}
			offset = 0;
			tcp_ctask->sg_count++;
		}

		if (!ctask->data_count) {
			if (rc && conn->datadgst_en)
				/*
				 * data-in is complete, but buffer not...
				 */
				partial_sg_digest_update(tcp_conn, &sg[i],
						sg[i].offset, sg[i].length-rc);
			rc = 0;
			break;
		}

		if (!tcp_conn->in.copy)
			return -EAGAIN;
	}
	BUG_ON(ctask->data_count);

done:
	/* check for non-exceptional status */
	if (tcp_conn->in.hdr->flags & ISCSI_FLAG_DATA_STATUS) {
		debug_scsi("done [sc %lx res %d itt 0x%x]\n",
			   (long)sc, sc->result, ctask->itt);
		spin_lock(&conn->session->lock);
		__iscsi_ctask_cleanup(conn, ctask);
		__iscsi_complete_pdu(conn, tcp_conn->in.hdr, NULL, 0);
		spin_unlock(&conn->session->lock);
	}

	return rc;
}

static int
iscsi_data_recv(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	int rc = 0, opcode;

	opcode = tcp_conn->in.hdr->opcode & ISCSI_OPCODE_MASK;
	switch (opcode) {
	case ISCSI_OP_SCSI_DATA_IN:
		rc = iscsi_scsi_data_in(conn);
		break;
	case ISCSI_OP_SCSI_CMD_RSP:
		spin_lock(&conn->session->lock);
		__iscsi_ctask_cleanup(conn, tcp_conn->in.ctask);
		spin_unlock(&conn->session->lock);
	case ISCSI_OP_TEXT_RSP:
	case ISCSI_OP_LOGIN_RSP:
	case ISCSI_OP_NOOP_IN:
	case ISCSI_OP_ASYNC_EVENT:
	case ISCSI_OP_REJECT:
		/*
		 * Collect data segment to the connection's data
		 * placeholder
		 */
		if (iscsi_tcp_copy(tcp_conn)) {
			rc = -EAGAIN;
			goto exit;
		}

		rc = iscsi_complete_pdu(conn, tcp_conn->in.hdr, tcp_conn->data,
					tcp_conn->in.datalen);
		if (!rc && conn->datadgst_en && opcode != ISCSI_OP_LOGIN_RSP)
			iscsi_recv_digest_update(tcp_conn, tcp_conn->data,
			  			tcp_conn->in.datalen);
		break;
	default:
		BUG_ON(1);
	}
exit:
	return rc;
}

/**
 * iscsi_tcp_data_recv - TCP receive in sendfile fashion
 * @rd_desc: read descriptor
 * @skb: socket buffer
 * @offset: offset in skb
 * @len: skb->len - offset
 **/
static int
iscsi_tcp_data_recv(read_descriptor_t *rd_desc, struct sk_buff *skb,
		unsigned int offset, size_t len)
{
	int rc;
	struct iscsi_conn *conn = rd_desc->arg.data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	int processed;
	char pad[ISCSI_PAD_LEN];
	struct scatterlist sg;

	/*
	 * Save current SKB and its offset in the corresponding
	 * connection context.
	 */
	tcp_conn->in.copy = skb->len - offset;
	tcp_conn->in.offset = offset;
	tcp_conn->in.skb = skb;
	tcp_conn->in.len = tcp_conn->in.copy;
	BUG_ON(tcp_conn->in.copy <= 0);
	debug_tcp("in %d bytes\n", tcp_conn->in.copy);

more:
	tcp_conn->in.copied = 0;
	rc = 0;

	if (unlikely(conn->suspend_rx)) {
		debug_tcp("conn %d Rx suspended!\n", conn->id);
		return 0;
	}

	if (tcp_conn->in_progress == IN_PROGRESS_WAIT_HEADER ||
	    tcp_conn->in_progress == IN_PROGRESS_HEADER_GATHER) {
		rc = iscsi_hdr_extract(tcp_conn);
		if (rc) {
		       if (rc == -EAGAIN)
				goto nomore;
		       else {
				iscsi_conn_failure(conn, rc);
				return 0;
		       }
		}

		/*
		 * Verify and process incoming PDU header.
		 */
		rc = iscsi_tcp_hdr_recv(conn);
		if (!rc && tcp_conn->in.datalen) {
			if (conn->datadgst_en) {
				BUG_ON(!tcp_conn->data_rx_tfm);
				crypto_digest_init(tcp_conn->data_rx_tfm);
			}
			tcp_conn->in_progress = IN_PROGRESS_DATA_RECV;
		} else if (rc) {
			iscsi_conn_failure(conn, rc);
			return 0;
		}
	}

	if (tcp_conn->in_progress == IN_PROGRESS_DDIGEST_RECV) {
		uint32_t recv_digest;

		debug_tcp("extra data_recv offset %d copy %d\n",
			  tcp_conn->in.offset, tcp_conn->in.copy);
		skb_copy_bits(tcp_conn->in.skb, tcp_conn->in.offset,
				&recv_digest, 4);
		tcp_conn->in.offset += 4;
		tcp_conn->in.copy -= 4;
		if (recv_digest != tcp_conn->in.datadgst) {
			debug_tcp("iscsi_tcp: data digest error!"
				  "0x%x != 0x%x\n", recv_digest,
				  tcp_conn->in.datadgst);
			iscsi_conn_failure(conn, ISCSI_ERR_DATA_DGST);
			return 0;
		} else {
			debug_tcp("iscsi_tcp: data digest match!"
				  "0x%x == 0x%x\n", recv_digest,
				  tcp_conn->in.datadgst);
			tcp_conn->in_progress = IN_PROGRESS_WAIT_HEADER;
		}
	}

	if (tcp_conn->in_progress == IN_PROGRESS_DATA_RECV &&
	   tcp_conn->in.copy) {

		debug_tcp("data_recv offset %d copy %d\n",
		       tcp_conn->in.offset, tcp_conn->in.copy);

		rc = iscsi_data_recv(conn);
		if (rc) {
			if (rc == -EAGAIN)
				goto again;
			iscsi_conn_failure(conn, rc);
			return 0;
		}
		tcp_conn->in.copy -= tcp_conn->in.padding;
		tcp_conn->in.offset += tcp_conn->in.padding;
		if (conn->datadgst_en) {
			if (tcp_conn->in.padding) {
				debug_tcp("padding -> %d\n",
					  tcp_conn->in.padding);
				memset(pad, 0, tcp_conn->in.padding);
				sg_init_one(&sg, pad, tcp_conn->in.padding);
				crypto_digest_update(tcp_conn->data_rx_tfm,
						     &sg, 1);
			}
			crypto_digest_final(tcp_conn->data_rx_tfm,
					    (u8 *) & tcp_conn->in.datadgst);
			debug_tcp("rx digest 0x%x\n", tcp_conn->in.datadgst);
			tcp_conn->in_progress = IN_PROGRESS_DDIGEST_RECV;
		} else
			tcp_conn->in_progress = IN_PROGRESS_WAIT_HEADER;
	}

	debug_tcp("f, processed %d from out of %d padding %d\n",
	       tcp_conn->in.offset - offset, (int)len, tcp_conn->in.padding);
	BUG_ON(tcp_conn->in.offset - offset > len);

	if (tcp_conn->in.offset - offset != len) {
		debug_tcp("continue to process %d bytes\n",
		       (int)len - (tcp_conn->in.offset - offset));
		goto more;
	}

nomore:
	processed = tcp_conn->in.offset - offset;
	BUG_ON(processed == 0);
	return processed;

again:
	processed = tcp_conn->in.offset - offset;
	debug_tcp("c, processed %d from out of %d rd_desc_cnt %d\n",
	          processed, (int)len, (int)rd_desc->count);
	BUG_ON(processed == 0);
	BUG_ON(processed > len);

	conn->rxdata_octets += processed;
	return processed;
}

static void
iscsi_tcp_data_ready(struct sock *sk, int flag)
{
	struct iscsi_conn *conn = sk->sk_user_data;
	read_descriptor_t rd_desc;

	read_lock(&sk->sk_callback_lock);

	/*
	 * Use rd_desc to pass 'conn' to iscsi_tcp_data_recv.
	 * We set count to 1 because we want the network layer to
	 * hand us all the skbs that are available. iscsi_tcp_data_recv
	 * handled pdus that cross buffers or pdus that still need data.
	 */
	rd_desc.arg.data = conn;
	rd_desc.count = 1;
	tcp_read_sock(sk, &rd_desc, iscsi_tcp_data_recv);

	read_unlock(&sk->sk_callback_lock);
}

static void
iscsi_tcp_state_change(struct sock *sk)
{
	struct iscsi_tcp_conn *tcp_conn;
	struct iscsi_conn *conn;
	struct iscsi_session *session;
	void (*old_state_change)(struct sock *);

	read_lock(&sk->sk_callback_lock);

	conn = (struct iscsi_conn*)sk->sk_user_data;
	session = conn->session;

	if ((sk->sk_state == TCP_CLOSE_WAIT ||
	     sk->sk_state == TCP_CLOSE) &&
	    !atomic_read(&sk->sk_rmem_alloc)) {
		debug_tcp("iscsi_tcp_state_change: TCP_CLOSE|TCP_CLOSE_WAIT\n");
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	}

	tcp_conn = conn->dd_data;
	old_state_change = tcp_conn->old_state_change;

	read_unlock(&sk->sk_callback_lock);

	old_state_change(sk);
}

/**
 * iscsi_write_space - Called when more output buffer space is available
 * @sk: socket space is available for
 **/
static void
iscsi_write_space(struct sock *sk)
{
	struct iscsi_conn *conn = (struct iscsi_conn*)sk->sk_user_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	tcp_conn->old_write_space(sk);
	debug_tcp("iscsi_write_space: cid %d\n", conn->id);
	clear_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	scsi_queue_work(conn->session->host, &conn->xmitwork);
}

static void
iscsi_conn_set_callbacks(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct sock *sk = tcp_conn->sock->sk;

	/* assign new callbacks */
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_user_data = conn;
	tcp_conn->old_data_ready = sk->sk_data_ready;
	tcp_conn->old_state_change = sk->sk_state_change;
	tcp_conn->old_write_space = sk->sk_write_space;
	sk->sk_data_ready = iscsi_tcp_data_ready;
	sk->sk_state_change = iscsi_tcp_state_change;
	sk->sk_write_space = iscsi_write_space;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void
iscsi_conn_restore_callbacks(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct sock *sk = tcp_conn->sock->sk;

	/* restore socket callbacks, see also: iscsi_conn_set_callbacks() */
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_user_data    = NULL;
	sk->sk_data_ready   = tcp_conn->old_data_ready;
	sk->sk_state_change = tcp_conn->old_state_change;
	sk->sk_write_space  = tcp_conn->old_write_space;
	sk->sk_no_check	 = 0;
	write_unlock_bh(&sk->sk_callback_lock);
}

/**
 * iscsi_send - generic send routine
 * @sk: kernel's socket
 * @buf: buffer to write from
 * @size: actual size to write
 * @flags: socket's flags
 */
static inline int
iscsi_send(struct iscsi_conn *conn, struct iscsi_buf *buf, int size, int flags)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct socket *sk = tcp_conn->sock;
	int offset = buf->sg.offset + buf->sent;

	/*
	 * if we got use_sg=0 or are sending something we kmallocd
	 * then we did not have to do kmap (kmap returns page_address)
	 *
	 * if we got use_sg > 0, but had to drop down, we do not
	 * set clustering so this should only happen for that
	 * slab case.
	 */
	if (buf->use_sendmsg)
		return sock_no_sendpage(sk, buf->sg.page, offset, size, flags);
	else
		return tcp_conn->sendpage(sk, buf->sg.page, offset, size,
					  flags);
}

/**
 * iscsi_sendhdr - send PDU Header via tcp_sendpage()
 * @conn: iscsi connection
 * @buf: buffer to write from
 * @datalen: lenght of data to be sent after the header
 *
 * Notes:
 *	(Tx, Fast Path)
 **/
static inline int
iscsi_sendhdr(struct iscsi_conn *conn, struct iscsi_buf *buf, int datalen)
{
	struct iscsi_tcp_conn *tcp_conn;
	int flags = 0; /* MSG_DONTWAIT; */
	int res, size;

	size = buf->sg.length - buf->sent;
	BUG_ON(buf->sent + size > buf->sg.length);
	if (buf->sent + size != buf->sg.length || datalen)
		flags |= MSG_MORE;

	res = iscsi_send(conn, buf, size, flags);
	debug_tcp("sendhdr %d bytes, sent %d res %d\n", size, buf->sent, res);
	if (res >= 0) {
		conn->txdata_octets += res;
		buf->sent += res;
		if (size != res)
			return -EAGAIN;
		return 0;
	} else if (res == -EAGAIN) {
		tcp_conn = conn->dd_data;
		tcp_conn->sendpage_failures_cnt++;
		set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	} else if (res == -EPIPE)
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);

	return res;
}

/**
 * iscsi_sendpage - send one page of iSCSI Data-Out.
 * @conn: iscsi connection
 * @buf: buffer to write from
 * @count: remaining data
 * @sent: number of bytes sent
 *
 * Notes:
 *	(Tx, Fast Path)
 **/
static inline int
iscsi_sendpage(struct iscsi_conn *conn, struct iscsi_buf *buf,
	       int *count, int *sent)
{
	struct iscsi_tcp_conn *tcp_conn;
	int flags = 0; /* MSG_DONTWAIT; */
	int res, size;

	size = buf->sg.length - buf->sent;
	BUG_ON(buf->sent + size > buf->sg.length);
	if (size > *count)
		size = *count;
	if (buf->sent + size != buf->sg.length || *count != size)
		flags |= MSG_MORE;

	res = iscsi_send(conn, buf, size, flags);
	debug_tcp("sendpage: %d bytes, sent %d left %d sent %d res %d\n",
		  size, buf->sent, *count, *sent, res);
	if (res >= 0) {
		conn->txdata_octets += res;
		buf->sent += res;
		*count -= res;
		*sent += res;
		if (size != res)
			return -EAGAIN;
		return 0;
	} else if (res == -EAGAIN) {
		tcp_conn = conn->dd_data;
		tcp_conn->sendpage_failures_cnt++;
		set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_tx);
	} else if (res == -EPIPE)
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);

	return res;
}

static inline void
iscsi_data_digest_init(struct iscsi_tcp_conn *tcp_conn,
		      struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

	BUG_ON(!tcp_conn->data_tx_tfm);
	crypto_digest_init(tcp_conn->data_tx_tfm);
	tcp_ctask->digest_count = 4;
}

static int
iscsi_digest_final_send(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask,
			struct iscsi_buf *buf, uint32_t *digest, int final)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	int rc = 0;
	int sent = 0;

	if (final)
		crypto_digest_final(tcp_conn->data_tx_tfm, (u8*)digest);

	iscsi_buf_init_iov(buf, (char*)digest, 4);
	rc = iscsi_sendpage(conn, buf, &tcp_ctask->digest_count, &sent);
	if (rc) {
		tcp_ctask->datadigest = *digest;
		tcp_ctask->xmstate |= XMSTATE_DATA_DIGEST;
	} else
		tcp_ctask->digest_count = 4;
	return rc;
}

/**
 * iscsi_solicit_data_cont - initialize next Data-Out
 * @conn: iscsi connection
 * @ctask: scsi command task
 * @r2t: R2T info
 * @left: bytes left to transfer
 *
 * Notes:
 *	Initialize next Data-Out within this R2T sequence and continue
 *	to process next Scatter-Gather element(if any) of this SCSI command.
 *
 *	Called under connection lock.
 **/
static void
iscsi_solicit_data_cont(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask,
			struct iscsi_r2t_info *r2t, int left)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_data *hdr;
	struct scsi_cmnd *sc = ctask->sc;
	int new_offset;

	hdr = &r2t->dtask.hdr;
	memset(hdr, 0, sizeof(struct iscsi_data));
	hdr->ttt = r2t->ttt;
	hdr->datasn = cpu_to_be32(r2t->solicit_datasn);
	r2t->solicit_datasn++;
	hdr->opcode = ISCSI_OP_SCSI_DATA_OUT;
	memcpy(hdr->lun, ctask->hdr->lun, sizeof(hdr->lun));
	hdr->itt = ctask->hdr->itt;
	hdr->exp_statsn = r2t->exp_statsn;
	new_offset = r2t->data_offset + r2t->sent;
	hdr->offset = cpu_to_be32(new_offset);
	if (left > conn->max_xmit_dlength) {
		hton24(hdr->dlength, conn->max_xmit_dlength);
		r2t->data_count = conn->max_xmit_dlength;
	} else {
		hton24(hdr->dlength, left);
		r2t->data_count = left;
		hdr->flags = ISCSI_FLAG_CMD_FINAL;
	}
	conn->dataout_pdus_cnt++;

	iscsi_buf_init_iov(&r2t->headbuf, (char*)hdr,
			   sizeof(struct iscsi_hdr));

	if (sc->use_sg && !iscsi_buf_left(&r2t->sendbuf)) {
		BUG_ON(tcp_ctask->bad_sg == r2t->sg);
		iscsi_buf_init_sg(&r2t->sendbuf, r2t->sg);
		r2t->sg += 1;
	} else
		iscsi_buf_init_iov(&tcp_ctask->sendbuf,
			    (char*)sc->request_buffer + new_offset,
			    r2t->data_count);
}

static void
iscsi_unsolicit_data_init(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_data_task *dtask;

	dtask = tcp_ctask->dtask = &tcp_ctask->unsol_dtask;
	iscsi_prep_unsolicit_data_pdu(ctask, &dtask->hdr,
				      tcp_ctask->r2t_data_count);
	iscsi_buf_init_iov(&tcp_ctask->headbuf, (char*)&dtask->hdr,
			   sizeof(struct iscsi_hdr));
}

/**
 * iscsi_tcp_cmd_init - Initialize iSCSI SCSI_READ or SCSI_WRITE commands
 * @conn: iscsi connection
 * @ctask: scsi command task
 * @sc: scsi command
 **/
static void
iscsi_tcp_cmd_init(struct iscsi_cmd_task *ctask)
{
	struct scsi_cmnd *sc = ctask->sc;
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

	BUG_ON(__kfifo_len(tcp_ctask->r2tqueue));

	tcp_ctask->sent = 0;
	tcp_ctask->sg_count = 0;

	if (sc->sc_data_direction == DMA_TO_DEVICE) {
		tcp_ctask->xmstate = XMSTATE_W_HDR;
		tcp_ctask->exp_r2tsn = 0;
		BUG_ON(ctask->total_length == 0);

		if (sc->use_sg) {
			struct scatterlist *sg = sc->request_buffer;

			iscsi_buf_init_sg(&tcp_ctask->sendbuf,
					  &sg[tcp_ctask->sg_count++]);
			tcp_ctask->sg = sg;
			tcp_ctask->bad_sg = sg + sc->use_sg;
		} else
			iscsi_buf_init_iov(&tcp_ctask->sendbuf,
					   sc->request_buffer,
					   sc->request_bufflen);

		if (ctask->imm_count)
			tcp_ctask->xmstate |= XMSTATE_IMM_DATA;

		tcp_ctask->pad_count = ctask->total_length & (ISCSI_PAD_LEN-1);
		if (tcp_ctask->pad_count) {
			tcp_ctask->pad_count = ISCSI_PAD_LEN -
							tcp_ctask->pad_count;
			debug_scsi("write padding %d bytes\n",
				   tcp_ctask->pad_count);
			tcp_ctask->xmstate |= XMSTATE_W_PAD;
		}

		if (ctask->unsol_count)
			tcp_ctask->xmstate |= XMSTATE_UNS_HDR |
						XMSTATE_UNS_INIT;
		tcp_ctask->r2t_data_count = ctask->total_length -
				    ctask->imm_count -
				    ctask->unsol_count;

		debug_scsi("cmd [itt %x total %d imm %d imm_data %d "
			   "r2t_data %d]\n",
			   ctask->itt, ctask->total_length, ctask->imm_count,
			   ctask->unsol_count, tcp_ctask->r2t_data_count);
	} else
		tcp_ctask->xmstate = XMSTATE_R_HDR;

	iscsi_buf_init_iov(&tcp_ctask->headbuf, (char*)ctask->hdr,
			    sizeof(struct iscsi_hdr));
}

/**
 * iscsi_tcp_mtask_xmit - xmit management(immediate) task
 * @conn: iscsi connection
 * @mtask: task management task
 *
 * Notes:
 *	The function can return -EAGAIN in which case caller must
 *	call it again later, or recover. '0' return code means successful
 *	xmit.
 *
 *	Management xmit state machine consists of two states:
 *		IN_PROGRESS_IMM_HEAD - PDU Header xmit in progress
 *		IN_PROGRESS_IMM_DATA - PDU Data xmit in progress
 **/
static int
iscsi_tcp_mtask_xmit(struct iscsi_conn *conn, struct iscsi_mgmt_task *mtask)
{
	struct iscsi_tcp_mgmt_task *tcp_mtask = mtask->dd_data;

	debug_scsi("mtask deq [cid %d state %x itt 0x%x]\n",
		conn->id, tcp_mtask->xmstate, mtask->itt);

	if (tcp_mtask->xmstate & XMSTATE_IMM_HDR) {
		tcp_mtask->xmstate &= ~XMSTATE_IMM_HDR;
		if (mtask->data_count)
			tcp_mtask->xmstate |= XMSTATE_IMM_DATA;
		if (conn->c_stage != ISCSI_CONN_INITIAL_STAGE &&
		    conn->stop_stage != STOP_CONN_RECOVER &&
		    conn->hdrdgst_en)
			iscsi_hdr_digest(conn, &tcp_mtask->headbuf,
					(u8*)tcp_mtask->hdrext);
		if (iscsi_sendhdr(conn, &tcp_mtask->headbuf,
				  mtask->data_count)) {
			tcp_mtask->xmstate |= XMSTATE_IMM_HDR;
			if (mtask->data_count)
				tcp_mtask->xmstate &= ~XMSTATE_IMM_DATA;
			return -EAGAIN;
		}
	}

	if (tcp_mtask->xmstate & XMSTATE_IMM_DATA) {
		BUG_ON(!mtask->data_count);
		tcp_mtask->xmstate &= ~XMSTATE_IMM_DATA;
		/* FIXME: implement.
		 * Virtual buffer could be spreaded across multiple pages...
		 */
		do {
			if (iscsi_sendpage(conn, &tcp_mtask->sendbuf,
				   &mtask->data_count, &tcp_mtask->sent)) {
				tcp_mtask->xmstate |= XMSTATE_IMM_DATA;
				return -EAGAIN;
			}
		} while (mtask->data_count);
	}

	BUG_ON(tcp_mtask->xmstate != XMSTATE_IDLE);
	if (mtask->hdr->itt == cpu_to_be32(ISCSI_RESERVED_TAG)) {
		struct iscsi_session *session = conn->session;

		spin_lock_bh(&session->lock);
		list_del(&conn->mtask->running);
		__kfifo_put(session->mgmtpool.queue, (void*)&conn->mtask,
			    sizeof(void*));
		spin_unlock_bh(&session->lock);
	}
	return 0;
}

static inline int
handle_xmstate_r_hdr(struct iscsi_conn *conn,
		     struct iscsi_tcp_cmd_task *tcp_ctask)
{
	tcp_ctask->xmstate &= ~XMSTATE_R_HDR;
	if (conn->hdrdgst_en)
		iscsi_hdr_digest(conn, &tcp_ctask->headbuf,
				 (u8*)tcp_ctask->hdrext);
	if (!iscsi_sendhdr(conn, &tcp_ctask->headbuf, 0)) {
		BUG_ON(tcp_ctask->xmstate != XMSTATE_IDLE);
		return 0; /* wait for Data-In */
	}
	tcp_ctask->xmstate |= XMSTATE_R_HDR;
	return -EAGAIN;
}

static inline int
handle_xmstate_w_hdr(struct iscsi_conn *conn,
		     struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

	tcp_ctask->xmstate &= ~XMSTATE_W_HDR;
	if (conn->hdrdgst_en)
		iscsi_hdr_digest(conn, &tcp_ctask->headbuf,
				 (u8*)tcp_ctask->hdrext);
	if (iscsi_sendhdr(conn, &tcp_ctask->headbuf, ctask->imm_count)) {
		tcp_ctask->xmstate |= XMSTATE_W_HDR;
		return -EAGAIN;
	}
	return 0;
}

static inline int
handle_xmstate_data_digest(struct iscsi_conn *conn,
			   struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

	tcp_ctask->xmstate &= ~XMSTATE_DATA_DIGEST;
	debug_tcp("resent data digest 0x%x\n", tcp_ctask->datadigest);
	if (iscsi_digest_final_send(conn, ctask, &tcp_ctask->immbuf,
				    &tcp_ctask->datadigest, 0)) {
		tcp_ctask->xmstate |= XMSTATE_DATA_DIGEST;
		debug_tcp("resent data digest 0x%x fail!\n",
			  tcp_ctask->datadigest);
		return -EAGAIN;
	}
	return 0;
}

static inline int
handle_xmstate_imm_data(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	BUG_ON(!ctask->imm_count);
	tcp_ctask->xmstate &= ~XMSTATE_IMM_DATA;

	if (conn->datadgst_en) {
		iscsi_data_digest_init(tcp_conn, ctask);
		tcp_ctask->immdigest = 0;
	}

	for (;;) {
		if (iscsi_sendpage(conn, &tcp_ctask->sendbuf, &ctask->imm_count,
				   &tcp_ctask->sent)) {
			tcp_ctask->xmstate |= XMSTATE_IMM_DATA;
			if (conn->datadgst_en) {
				crypto_digest_final(tcp_conn->data_tx_tfm,
						(u8*)&tcp_ctask->immdigest);
				debug_tcp("tx imm sendpage fail 0x%x\n",
					  tcp_ctask->datadigest);
			}
			return -EAGAIN;
		}
		if (conn->datadgst_en)
			crypto_digest_update(tcp_conn->data_tx_tfm,
					     &tcp_ctask->sendbuf.sg, 1);

		if (!ctask->imm_count)
			break;
		iscsi_buf_init_sg(&tcp_ctask->sendbuf,
				  &tcp_ctask->sg[tcp_ctask->sg_count++]);
	}

	if (conn->datadgst_en && !(tcp_ctask->xmstate & XMSTATE_W_PAD)) {
		if (iscsi_digest_final_send(conn, ctask, &tcp_ctask->immbuf,
				            &tcp_ctask->immdigest, 1)) {
			debug_tcp("sending imm digest 0x%x fail!\n",
				  tcp_ctask->immdigest);
			return -EAGAIN;
		}
		debug_tcp("sending imm digest 0x%x\n", tcp_ctask->immdigest);
	}

	return 0;
}

static inline int
handle_xmstate_uns_hdr(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_data_task *dtask;

	tcp_ctask->xmstate |= XMSTATE_UNS_DATA;
	if (tcp_ctask->xmstate & XMSTATE_UNS_INIT) {
		iscsi_unsolicit_data_init(conn, ctask);
		dtask = tcp_ctask->dtask;
		if (conn->hdrdgst_en)
			iscsi_hdr_digest(conn, &tcp_ctask->headbuf,
					(u8*)dtask->hdrext);
		tcp_ctask->xmstate &= ~XMSTATE_UNS_INIT;
	}
	if (iscsi_sendhdr(conn, &tcp_ctask->headbuf, ctask->data_count)) {
		tcp_ctask->xmstate &= ~XMSTATE_UNS_DATA;
		tcp_ctask->xmstate |= XMSTATE_UNS_HDR;
		return -EAGAIN;
	}

	debug_scsi("uns dout [itt 0x%x dlen %d sent %d]\n",
		   ctask->itt, ctask->unsol_count, tcp_ctask->sent);
	return 0;
}

static inline int
handle_xmstate_uns_data(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_data_task *dtask = tcp_ctask->dtask;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	BUG_ON(!ctask->data_count);
	tcp_ctask->xmstate &= ~XMSTATE_UNS_DATA;

	if (conn->datadgst_en) {
		iscsi_data_digest_init(tcp_conn, ctask);
		dtask->digest = 0;
	}

	for (;;) {
		int start = tcp_ctask->sent;

		if (iscsi_sendpage(conn, &tcp_ctask->sendbuf,
				   &ctask->data_count, &tcp_ctask->sent)) {
			ctask->unsol_count -= tcp_ctask->sent - start;
			tcp_ctask->xmstate |= XMSTATE_UNS_DATA;
			/* will continue with this ctask later.. */
			if (conn->datadgst_en) {
				crypto_digest_final(tcp_conn->data_tx_tfm,
						(u8 *)&dtask->digest);
				debug_tcp("tx uns data fail 0x%x\n",
					  dtask->digest);
			}
			return -EAGAIN;
		}

		BUG_ON(tcp_ctask->sent > ctask->total_length);
		ctask->unsol_count -= tcp_ctask->sent - start;

		/*
		 * XXX:we may run here with un-initial sendbuf.
		 * so pass it
		 */
		if (conn->datadgst_en && tcp_ctask->sent - start > 0)
			crypto_digest_update(tcp_conn->data_tx_tfm,
					     &tcp_ctask->sendbuf.sg, 1);

		if (!ctask->data_count)
			break;
		iscsi_buf_init_sg(&tcp_ctask->sendbuf,
				  &tcp_ctask->sg[tcp_ctask->sg_count++]);
	}
	BUG_ON(ctask->unsol_count < 0);

	/*
	 * Done with the Data-Out. Next, check if we need
	 * to send another unsolicited Data-Out.
	 */
	if (ctask->unsol_count) {
		if (conn->datadgst_en) {
			if (iscsi_digest_final_send(conn, ctask,
						    &dtask->digestbuf,
						    &dtask->digest, 1)) {
				debug_tcp("send uns digest 0x%x fail\n",
					  dtask->digest);
				return -EAGAIN;
			}
			debug_tcp("sending uns digest 0x%x, more uns\n",
				  dtask->digest);
		}
		tcp_ctask->xmstate |= XMSTATE_UNS_INIT;
		return 1;
	}

	if (conn->datadgst_en && !(tcp_ctask->xmstate & XMSTATE_W_PAD)) {
		if (iscsi_digest_final_send(conn, ctask,
					    &dtask->digestbuf,
					    &dtask->digest, 1)) {
			debug_tcp("send last uns digest 0x%x fail\n",
				   dtask->digest);
			return -EAGAIN;
		}
		debug_tcp("sending uns digest 0x%x\n",dtask->digest);
	}

	return 0;
}

static inline int
handle_xmstate_sol_data(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_r2t_info *r2t = tcp_ctask->r2t;
	struct iscsi_data_task *dtask = &r2t->dtask;
	int left;

	tcp_ctask->xmstate &= ~XMSTATE_SOL_DATA;
	tcp_ctask->dtask = dtask;

	if (conn->datadgst_en) {
		iscsi_data_digest_init(tcp_conn, ctask);
		dtask->digest = 0;
	}
solicit_again:
	/*
	 * send Data-Out whitnin this R2T sequence.
	 */
	if (!r2t->data_count)
		goto data_out_done;

	if (iscsi_sendpage(conn, &r2t->sendbuf, &r2t->data_count, &r2t->sent)) {
		tcp_ctask->xmstate |= XMSTATE_SOL_DATA;
		/* will continue with this ctask later.. */
		if (conn->datadgst_en) {
			crypto_digest_final(tcp_conn->data_tx_tfm,
					  (u8 *)&dtask->digest);
			debug_tcp("r2t data send fail 0x%x\n", dtask->digest);
		}
		return -EAGAIN;
	}

	BUG_ON(r2t->data_count < 0);
	if (conn->datadgst_en)
		crypto_digest_update(tcp_conn->data_tx_tfm, &r2t->sendbuf.sg,
				     1);

	if (r2t->data_count) {
		BUG_ON(ctask->sc->use_sg == 0);
		if (!iscsi_buf_left(&r2t->sendbuf)) {
			BUG_ON(tcp_ctask->bad_sg == r2t->sg);
			iscsi_buf_init_sg(&r2t->sendbuf, r2t->sg);
			r2t->sg += 1;
		}
		goto solicit_again;
	}

data_out_done:
	/*
	 * Done with this Data-Out. Next, check if we have
	 * to send another Data-Out for this R2T.
	 */
	BUG_ON(r2t->data_length - r2t->sent < 0);
	left = r2t->data_length - r2t->sent;
	if (left) {
		if (conn->datadgst_en) {
			if (iscsi_digest_final_send(conn, ctask,
						    &dtask->digestbuf,
						    &dtask->digest, 1)) {
				debug_tcp("send r2t data digest 0x%x"
					  "fail\n", dtask->digest);
				return -EAGAIN;
			}
			debug_tcp("r2t data send digest 0x%x\n",
				  dtask->digest);
		}
		iscsi_solicit_data_cont(conn, ctask, r2t, left);
		tcp_ctask->xmstate |= XMSTATE_SOL_DATA;
		tcp_ctask->xmstate &= ~XMSTATE_SOL_HDR;
		return 1;
	}

	/*
	 * Done with this R2T. Check if there are more
	 * outstanding R2Ts ready to be processed.
	 */
	BUG_ON(tcp_ctask->r2t_data_count - r2t->data_length < 0);
	if (conn->datadgst_en) {
		if (iscsi_digest_final_send(conn, ctask, &dtask->digestbuf,
					    &dtask->digest, 1)) {
			debug_tcp("send last r2t data digest 0x%x"
				  "fail\n", dtask->digest);
			return -EAGAIN;
		}
		debug_tcp("r2t done dout digest 0x%x\n", dtask->digest);
	}

	tcp_ctask->r2t_data_count -= r2t->data_length;
	tcp_ctask->r2t = NULL;
	spin_lock_bh(&session->lock);
	__kfifo_put(tcp_ctask->r2tpool.queue, (void*)&r2t, sizeof(void*));
	spin_unlock_bh(&session->lock);
	if (__kfifo_get(tcp_ctask->r2tqueue, (void*)&r2t, sizeof(void*))) {
		tcp_ctask->r2t = r2t;
		tcp_ctask->xmstate |= XMSTATE_SOL_DATA;
		tcp_ctask->xmstate &= ~XMSTATE_SOL_HDR;
		return 1;
	}

	return 0;
}

static inline int
handle_xmstate_w_pad(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_data_task *dtask = tcp_ctask->dtask;
	int sent;

	tcp_ctask->xmstate &= ~XMSTATE_W_PAD;
	iscsi_buf_init_iov(&tcp_ctask->sendbuf, (char*)&tcp_ctask->pad,
			    tcp_ctask->pad_count);
	if (iscsi_sendpage(conn, &tcp_ctask->sendbuf, &tcp_ctask->pad_count,
			   &sent)) {
		tcp_ctask->xmstate |= XMSTATE_W_PAD;
		return -EAGAIN;
	}

	if (conn->datadgst_en) {
		crypto_digest_update(tcp_conn->data_tx_tfm,
				     &tcp_ctask->sendbuf.sg, 1);
		/* imm data? */
		if (!dtask) {
			if (iscsi_digest_final_send(conn, ctask,
						    &tcp_ctask->immbuf,
						    &tcp_ctask->immdigest, 1)) {
				debug_tcp("send padding digest 0x%x"
					  "fail!\n", tcp_ctask->immdigest);
				return -EAGAIN;
			}
			debug_tcp("done with padding, digest 0x%x\n",
				  tcp_ctask->datadigest);
		} else {
			if (iscsi_digest_final_send(conn, ctask,
						    &dtask->digestbuf,
						    &dtask->digest, 1)) {
				debug_tcp("send padding digest 0x%x"
				          "fail\n", dtask->digest);
				return -EAGAIN;
			}
			debug_tcp("done with padding, digest 0x%x\n",
				  dtask->digest);
		}
	}

	return 0;
}

static int
iscsi_tcp_ctask_xmit(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	int rc = 0;

	debug_scsi("ctask deq [cid %d xmstate %x itt 0x%x]\n",
		conn->id, tcp_ctask->xmstate, ctask->itt);

	/*
	 * serialize with TMF AbortTask
	 */
	if (ctask->mtask)
		return rc;

	if (tcp_ctask->xmstate & XMSTATE_R_HDR) {
		rc = handle_xmstate_r_hdr(conn, tcp_ctask);
		return rc;
	}

	if (tcp_ctask->xmstate & XMSTATE_W_HDR) {
		rc = handle_xmstate_w_hdr(conn, ctask);
		if (rc)
			return rc;
	}

	/* XXX: for data digest xmit recover */
	if (tcp_ctask->xmstate & XMSTATE_DATA_DIGEST) {
		rc = handle_xmstate_data_digest(conn, ctask);
		if (rc)
			return rc;
	}

	if (tcp_ctask->xmstate & XMSTATE_IMM_DATA) {
		rc = handle_xmstate_imm_data(conn, ctask);
		if (rc)
			return rc;
	}

	if (tcp_ctask->xmstate & XMSTATE_UNS_HDR) {
		BUG_ON(!ctask->unsol_count);
		tcp_ctask->xmstate &= ~XMSTATE_UNS_HDR;
unsolicit_head_again:
		rc = handle_xmstate_uns_hdr(conn, ctask);
		if (rc)
			return rc;
	}

	if (tcp_ctask->xmstate & XMSTATE_UNS_DATA) {
		rc = handle_xmstate_uns_data(conn, ctask);
		if (rc == 1)
			goto unsolicit_head_again;
		else if (rc)
			return rc;
		goto done;
	}

	if (tcp_ctask->xmstate & XMSTATE_SOL_HDR) {
		struct iscsi_r2t_info *r2t;

		tcp_ctask->xmstate &= ~XMSTATE_SOL_HDR;
		tcp_ctask->xmstate |= XMSTATE_SOL_DATA;
		if (!tcp_ctask->r2t)
			__kfifo_get(tcp_ctask->r2tqueue, (void*)&tcp_ctask->r2t,
				    sizeof(void*));
solicit_head_again:
		r2t = tcp_ctask->r2t;
		if (conn->hdrdgst_en)
			iscsi_hdr_digest(conn, &r2t->headbuf,
					(u8*)r2t->dtask.hdrext);
		if (iscsi_sendhdr(conn, &r2t->headbuf, r2t->data_count)) {
			tcp_ctask->xmstate &= ~XMSTATE_SOL_DATA;
			tcp_ctask->xmstate |= XMSTATE_SOL_HDR;
			return -EAGAIN;
		}

		debug_scsi("sol dout [dsn %d itt 0x%x dlen %d sent %d]\n",
			r2t->solicit_datasn - 1, ctask->itt, r2t->data_count,
			r2t->sent);
	}

	if (tcp_ctask->xmstate & XMSTATE_SOL_DATA) {
		rc = handle_xmstate_sol_data(conn, ctask);
		if (rc == 1)
			goto solicit_head_again;
		if (rc)
			return rc;
	}

done:
	/*
	 * Last thing to check is whether we need to send write
	 * padding. Note that we check for xmstate equality, not just the bit.
	 */
	if (tcp_ctask->xmstate == XMSTATE_W_PAD)
		rc = handle_xmstate_w_pad(conn, ctask);

	return rc;
}

static struct iscsi_cls_conn *
iscsi_tcp_conn_create(struct iscsi_cls_session *cls_session, uint32_t conn_idx)
{
	struct iscsi_conn *conn;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_tcp_conn *tcp_conn;

	cls_conn = iscsi_conn_setup(cls_session, conn_idx);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;
	/*
	 * due to strange issues with iser these are not set
	 * in iscsi_conn_setup
	 */
	conn->max_recv_dlength = DEFAULT_MAX_RECV_DATA_SEGMENT_LENGTH;

	tcp_conn = kzalloc(sizeof(*tcp_conn), GFP_KERNEL);
	if (!tcp_conn)
		goto tcp_conn_alloc_fail;

	conn->dd_data = tcp_conn;
	tcp_conn->iscsi_conn = conn;
	tcp_conn->in_progress = IN_PROGRESS_WAIT_HEADER;
	/* initial operational parameters */
	tcp_conn->hdr_size = sizeof(struct iscsi_hdr);
	tcp_conn->data_size = DEFAULT_MAX_RECV_DATA_SEGMENT_LENGTH;

	/* allocate initial PDU receive place holder */
	if (tcp_conn->data_size <= PAGE_SIZE)
		tcp_conn->data = kmalloc(tcp_conn->data_size, GFP_KERNEL);
	else
		tcp_conn->data = (void*)__get_free_pages(GFP_KERNEL,
					get_order(tcp_conn->data_size));
	if (!tcp_conn->data)
		goto max_recv_dlenght_alloc_fail;

	return cls_conn;

max_recv_dlenght_alloc_fail:
	kfree(tcp_conn);
tcp_conn_alloc_fail:
	iscsi_conn_teardown(cls_conn);
	return NULL;
}

static void
iscsi_tcp_conn_destroy(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	int digest = 0;

	if (conn->hdrdgst_en || conn->datadgst_en)
		digest = 1;

	iscsi_conn_teardown(cls_conn);

	/* now free tcp_conn */
	if (digest) {
		if (tcp_conn->tx_tfm)
			crypto_free_tfm(tcp_conn->tx_tfm);
		if (tcp_conn->rx_tfm)
			crypto_free_tfm(tcp_conn->rx_tfm);
		if (tcp_conn->data_tx_tfm)
			crypto_free_tfm(tcp_conn->data_tx_tfm);
		if (tcp_conn->data_rx_tfm)
			crypto_free_tfm(tcp_conn->data_rx_tfm);
	}

	/* free conn->data, size = MaxRecvDataSegmentLength */
	if (tcp_conn->data_size <= PAGE_SIZE)
		kfree(tcp_conn->data);
	else
		free_pages((unsigned long)tcp_conn->data,
			   get_order(tcp_conn->data_size));
	kfree(tcp_conn);
}

static int
iscsi_tcp_conn_bind(struct iscsi_cls_session *cls_session,
		    struct iscsi_cls_conn *cls_conn, uint64_t transport_eph,
		    int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct sock *sk;
	struct socket *sock;
	int err;

	/* lookup for existing socket */
	sock = sockfd_lookup((int)transport_eph, &err);
	if (!sock) {
		printk(KERN_ERR "iscsi_tcp: sockfd_lookup failed %d\n", err);
		return -EEXIST;
	}

	err = iscsi_conn_bind(cls_session, cls_conn, is_leading);
	if (err)
		return err;

	if (conn->stop_stage != STOP_CONN_SUSPEND) {
		/* bind iSCSI connection and socket */
		tcp_conn->sock = sock;

		/* setup Socket parameters */
		sk = sock->sk;
		sk->sk_reuse = 1;
		sk->sk_sndtimeo = 15 * HZ; /* FIXME: make it configurable */
		sk->sk_allocation = GFP_ATOMIC;

		/* FIXME: disable Nagle's algorithm */

		/*
		 * Intercept TCP callbacks for sendfile like receive
		 * processing.
		 */
		conn->recv_lock = &sk->sk_callback_lock;
		iscsi_conn_set_callbacks(conn);
		tcp_conn->sendpage = tcp_conn->sock->ops->sendpage;
		/*
		 * set receive state machine into initial state
		 */
		tcp_conn->in_progress = IN_PROGRESS_WAIT_HEADER;
	}

	return 0;
}

static void
iscsi_tcp_cleanup_ctask(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_r2t_info *r2t;

	/* flush ctask's r2t queues */
	while (__kfifo_get(tcp_ctask->r2tqueue, (void*)&r2t, sizeof(void*)))
		__kfifo_put(tcp_ctask->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));

	__iscsi_ctask_cleanup(conn, ctask);
}

static void
iscsi_tcp_suspend_conn_rx(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct sock *sk;

	if (!tcp_conn->sock)
		return;

	sk = tcp_conn->sock->sk;
	write_lock_bh(&sk->sk_callback_lock);
	set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_rx);
	write_unlock_bh(&sk->sk_callback_lock);
}

static void
iscsi_tcp_terminate_conn(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	if (!tcp_conn->sock)
		return;

	sock_hold(tcp_conn->sock->sk);
	iscsi_conn_restore_callbacks(conn);
	sock_put(tcp_conn->sock->sk);

	sock_release(tcp_conn->sock);
	tcp_conn->sock = NULL;
	conn->recv_lock = NULL;
}

/* called with host lock */
static void
iscsi_tcp_mgmt_init(struct iscsi_conn *conn, struct iscsi_mgmt_task *mtask,
		    char *data, uint32_t data_size)
{
	struct iscsi_tcp_mgmt_task *tcp_mtask = mtask->dd_data;

	iscsi_buf_init_iov(&tcp_mtask->headbuf, (char*)mtask->hdr,
			   sizeof(struct iscsi_hdr));
	tcp_mtask->xmstate = XMSTATE_IMM_HDR;

	if (mtask->data_count)
		iscsi_buf_init_iov(&tcp_mtask->sendbuf, (char*)mtask->data,
				    mtask->data_count);
}

static int
iscsi_r2tpool_alloc(struct iscsi_session *session)
{
	int i;
	int cmd_i;

	/*
	 * initialize per-task: R2T pool and xmit queue
	 */
	for (cmd_i = 0; cmd_i < session->cmds_max; cmd_i++) {
	        struct iscsi_cmd_task *ctask = session->cmds[cmd_i];
		struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

		/*
		 * pre-allocated x4 as much r2ts to handle race when
		 * target acks DataOut faster than we data_xmit() queues
		 * could replenish r2tqueue.
		 */

		/* R2T pool */
		if (iscsi_pool_init(&tcp_ctask->r2tpool, session->max_r2t * 4,
				    (void***)&tcp_ctask->r2ts,
				    sizeof(struct iscsi_r2t_info))) {
			goto r2t_alloc_fail;
		}

		/* R2T xmit queue */
		tcp_ctask->r2tqueue = kfifo_alloc(
		      session->max_r2t * 4 * sizeof(void*), GFP_KERNEL, NULL);
		if (tcp_ctask->r2tqueue == ERR_PTR(-ENOMEM)) {
			iscsi_pool_free(&tcp_ctask->r2tpool,
					(void**)tcp_ctask->r2ts);
			goto r2t_alloc_fail;
		}
	}

	return 0;

r2t_alloc_fail:
	for (i = 0; i < cmd_i; i++) {
		struct iscsi_cmd_task *ctask = session->cmds[i];
		struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

		kfifo_free(tcp_ctask->r2tqueue);
		iscsi_pool_free(&tcp_ctask->r2tpool,
				(void**)tcp_ctask->r2ts);
	}
	return -ENOMEM;
}

static void
iscsi_r2tpool_free(struct iscsi_session *session)
{
	int i;

	for (i = 0; i < session->cmds_max; i++) {
		struct iscsi_cmd_task *ctask = session->cmds[i];
		struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

		kfifo_free(tcp_ctask->r2tqueue);
		iscsi_pool_free(&tcp_ctask->r2tpool,
				(void**)tcp_ctask->r2ts);
	}
}

static int
iscsi_conn_set_param(struct iscsi_cls_conn *cls_conn, enum iscsi_param param,
		     uint32_t value)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	switch(param) {
	case ISCSI_PARAM_MAX_RECV_DLENGTH: {
		char *saveptr = tcp_conn->data;
		gfp_t flags = GFP_KERNEL;

		if (tcp_conn->data_size >= value) {
			conn->max_recv_dlength = value;
			break;
		}

		spin_lock_bh(&session->lock);
		if (conn->stop_stage == STOP_CONN_RECOVER)
			flags = GFP_ATOMIC;
		spin_unlock_bh(&session->lock);

		if (value <= PAGE_SIZE)
			tcp_conn->data = kmalloc(value, flags);
		else
			tcp_conn->data = (void*)__get_free_pages(flags,
							     get_order(value));
		if (tcp_conn->data == NULL) {
			tcp_conn->data = saveptr;
			return -ENOMEM;
		}
		if (tcp_conn->data_size <= PAGE_SIZE)
			kfree(saveptr);
		else
			free_pages((unsigned long)saveptr,
				   get_order(tcp_conn->data_size));
		conn->max_recv_dlength = value;
		tcp_conn->data_size = value;
		}
		break;
	case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		conn->max_xmit_dlength =  value;
		break;
	case ISCSI_PARAM_HDRDGST_EN:
		conn->hdrdgst_en = value;
		tcp_conn->hdr_size = sizeof(struct iscsi_hdr);
		if (conn->hdrdgst_en) {
			tcp_conn->hdr_size += sizeof(__u32);
			if (!tcp_conn->tx_tfm)
				tcp_conn->tx_tfm = crypto_alloc_tfm("crc32c",
								    0);
			if (!tcp_conn->tx_tfm)
				return -ENOMEM;
			if (!tcp_conn->rx_tfm)
				tcp_conn->rx_tfm = crypto_alloc_tfm("crc32c",
								    0);
			if (!tcp_conn->rx_tfm) {
				crypto_free_tfm(tcp_conn->tx_tfm);
				return -ENOMEM;
			}
		} else {
			if (tcp_conn->tx_tfm)
				crypto_free_tfm(tcp_conn->tx_tfm);
			if (tcp_conn->rx_tfm)
				crypto_free_tfm(tcp_conn->rx_tfm);
		}
		break;
	case ISCSI_PARAM_DATADGST_EN:
		conn->datadgst_en = value;
		if (conn->datadgst_en) {
			if (!tcp_conn->data_tx_tfm)
				tcp_conn->data_tx_tfm =
				    crypto_alloc_tfm("crc32c", 0);
			if (!tcp_conn->data_tx_tfm)
				return -ENOMEM;
			if (!tcp_conn->data_rx_tfm)
				tcp_conn->data_rx_tfm =
				    crypto_alloc_tfm("crc32c", 0);
			if (!tcp_conn->data_rx_tfm) {
				crypto_free_tfm(tcp_conn->data_tx_tfm);
				return -ENOMEM;
			}
		} else {
			if (tcp_conn->data_tx_tfm)
				crypto_free_tfm(tcp_conn->data_tx_tfm);
			if (tcp_conn->data_rx_tfm)
				crypto_free_tfm(tcp_conn->data_rx_tfm);
		}
		tcp_conn->sendpage = conn->datadgst_en ?
			sock_no_sendpage : tcp_conn->sock->ops->sendpage;
		break;
	case ISCSI_PARAM_INITIAL_R2T_EN:
		session->initial_r2t_en = value;
		break;
	case ISCSI_PARAM_MAX_R2T:
		if (session->max_r2t == roundup_pow_of_two(value))
			break;
		iscsi_r2tpool_free(session);
		session->max_r2t = value;
		if (session->max_r2t & (session->max_r2t - 1))
			session->max_r2t = roundup_pow_of_two(session->max_r2t);
		if (iscsi_r2tpool_alloc(session))
			return -ENOMEM;
		break;
	case ISCSI_PARAM_IMM_DATA_EN:
		session->imm_data_en = value;
		break;
	case ISCSI_PARAM_FIRST_BURST:
		session->first_burst = value;
		break;
	case ISCSI_PARAM_MAX_BURST:
		session->max_burst = value;
		break;
	case ISCSI_PARAM_PDU_INORDER_EN:
		session->pdu_inorder_en = value;
		break;
	case ISCSI_PARAM_DATASEQ_INORDER_EN:
		session->dataseq_inorder_en = value;
		break;
	case ISCSI_PARAM_ERL:
		session->erl = value;
		break;
	case ISCSI_PARAM_IFMARKER_EN:
		BUG_ON(value);
		session->ifmarker_en = value;
		break;
	case ISCSI_PARAM_OFMARKER_EN:
		BUG_ON(value);
		session->ofmarker_en = value;
		break;
	case ISCSI_PARAM_EXP_STATSN:
		conn->exp_statsn = value;
		break;
	default:
		break;
	}

	return 0;
}

static int
iscsi_session_get_param(struct iscsi_cls_session *cls_session,
			enum iscsi_param param, uint32_t *value)
{
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);
	struct iscsi_session *session = iscsi_hostdata(shost->hostdata);

	switch(param) {
	case ISCSI_PARAM_INITIAL_R2T_EN:
		*value = session->initial_r2t_en;
		break;
	case ISCSI_PARAM_MAX_R2T:
		*value = session->max_r2t;
		break;
	case ISCSI_PARAM_IMM_DATA_EN:
		*value = session->imm_data_en;
		break;
	case ISCSI_PARAM_FIRST_BURST:
		*value = session->first_burst;
		break;
	case ISCSI_PARAM_MAX_BURST:
		*value = session->max_burst;
		break;
	case ISCSI_PARAM_PDU_INORDER_EN:
		*value = session->pdu_inorder_en;
		break;
	case ISCSI_PARAM_DATASEQ_INORDER_EN:
		*value = session->dataseq_inorder_en;
		break;
	case ISCSI_PARAM_ERL:
		*value = session->erl;
		break;
	case ISCSI_PARAM_IFMARKER_EN:
		*value = session->ifmarker_en;
		break;
	case ISCSI_PARAM_OFMARKER_EN:
		*value = session->ofmarker_en;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
iscsi_conn_get_param(struct iscsi_cls_conn *cls_conn,
		     enum iscsi_param param, uint32_t *value)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct inet_sock *inet;

	switch(param) {
	case ISCSI_PARAM_MAX_RECV_DLENGTH:
		*value = conn->max_recv_dlength;
		break;
	case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		*value = conn->max_xmit_dlength;
		break;
	case ISCSI_PARAM_HDRDGST_EN:
		*value = conn->hdrdgst_en;
		break;
	case ISCSI_PARAM_DATADGST_EN:
		*value = conn->datadgst_en;
		break;
	case ISCSI_PARAM_CONN_PORT:
		mutex_lock(&conn->xmitmutex);
		if (!tcp_conn->sock) {
			mutex_unlock(&conn->xmitmutex);
			return -EINVAL;
		}

		inet = inet_sk(tcp_conn->sock->sk);
		*value = be16_to_cpu(inet->dport);
		mutex_unlock(&conn->xmitmutex);
	case ISCSI_PARAM_EXP_STATSN:
		*value = conn->exp_statsn;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
iscsi_conn_get_str_param(struct iscsi_cls_conn *cls_conn,
			 enum iscsi_param param, char *buf)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct sock *sk;
	struct inet_sock *inet;
	struct ipv6_pinfo *np;
	int len = 0;

	switch (param) {
	case ISCSI_PARAM_CONN_ADDRESS:
		mutex_lock(&conn->xmitmutex);
		if (!tcp_conn->sock) {
			mutex_unlock(&conn->xmitmutex);
			return -EINVAL;
		}

		sk = tcp_conn->sock->sk;
		if (sk->sk_family == PF_INET) {
			inet = inet_sk(sk);
			len = sprintf(buf, "%u.%u.%u.%u\n",
				      NIPQUAD(inet->daddr));
		} else {
			np = inet6_sk(sk);
			len = sprintf(buf,
				"%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
				NIP6(np->daddr));
		}
		mutex_unlock(&conn->xmitmutex);
		break;
	default:
		return -EINVAL;
	}

	return len;
}

static void
iscsi_conn_get_stats(struct iscsi_cls_conn *cls_conn, struct iscsi_stats *stats)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	stats->txdata_octets = conn->txdata_octets;
	stats->rxdata_octets = conn->rxdata_octets;
	stats->scsicmd_pdus = conn->scsicmd_pdus_cnt;
	stats->dataout_pdus = conn->dataout_pdus_cnt;
	stats->scsirsp_pdus = conn->scsirsp_pdus_cnt;
	stats->datain_pdus = conn->datain_pdus_cnt;
	stats->r2t_pdus = conn->r2t_pdus_cnt;
	stats->tmfcmd_pdus = conn->tmfcmd_pdus_cnt;
	stats->tmfrsp_pdus = conn->tmfrsp_pdus_cnt;
	stats->custom_length = 3;
	strcpy(stats->custom[0].desc, "tx_sendpage_failures");
	stats->custom[0].value = tcp_conn->sendpage_failures_cnt;
	strcpy(stats->custom[1].desc, "rx_discontiguous_hdr");
	stats->custom[1].value = tcp_conn->discontiguous_hdr_cnt;
	strcpy(stats->custom[2].desc, "eh_abort_cnt");
	stats->custom[2].value = conn->eh_abort_cnt;
}

static struct iscsi_cls_session *
iscsi_tcp_session_create(struct iscsi_transport *iscsit,
			 struct scsi_transport_template *scsit,
			 uint32_t initial_cmdsn, uint32_t *hostno)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	uint32_t hn;
	int cmd_i;

	cls_session = iscsi_session_setup(iscsit, scsit,
					 sizeof(struct iscsi_tcp_cmd_task),
					 sizeof(struct iscsi_tcp_mgmt_task),
					 initial_cmdsn, &hn);
	if (!cls_session)
		return NULL;
	*hostno = hn;

	session = class_to_transport_session(cls_session);
	for (cmd_i = 0; cmd_i < session->cmds_max; cmd_i++) {
		struct iscsi_cmd_task *ctask = session->cmds[cmd_i];
		struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

		ctask->hdr = &tcp_ctask->hdr;
	}

	for (cmd_i = 0; cmd_i < session->mgmtpool_max; cmd_i++) {
		struct iscsi_mgmt_task *mtask = session->mgmt_cmds[cmd_i];
		struct iscsi_tcp_mgmt_task *tcp_mtask = mtask->dd_data;

		mtask->hdr = &tcp_mtask->hdr;
	}

	if (iscsi_r2tpool_alloc(class_to_transport_session(cls_session)))
		goto r2tpool_alloc_fail;

	return cls_session;

r2tpool_alloc_fail:
	iscsi_session_teardown(cls_session);
	return NULL;
}

static void iscsi_tcp_session_destroy(struct iscsi_cls_session *cls_session)
{
	iscsi_r2tpool_free(class_to_transport_session(cls_session));
	iscsi_session_teardown(cls_session);
}

static struct scsi_host_template iscsi_sht = {
	.name			= "iSCSI Initiator over TCP/IP, v"
				  ISCSI_TCP_VERSION,
	.queuecommand           = iscsi_queuecommand,
	.change_queue_depth	= iscsi_change_queue_depth,
	.can_queue		= ISCSI_XMIT_CMDS_MAX - 1,
	.sg_tablesize		= ISCSI_SG_TABLESIZE,
	.cmd_per_lun		= ISCSI_DEF_CMD_PER_LUN,
	.eh_abort_handler       = iscsi_eh_abort,
	.eh_host_reset_handler	= iscsi_eh_host_reset,
	.use_clustering         = DISABLE_CLUSTERING,
	.proc_name		= "iscsi_tcp",
	.this_id		= -1,
};

static struct iscsi_transport iscsi_tcp_transport = {
	.owner			= THIS_MODULE,
	.name			= "tcp",
	.caps			= CAP_RECOVERY_L0 | CAP_MULTI_R2T | CAP_HDRDGST
				  | CAP_DATADGST,
	.param_mask		= ISCSI_MAX_RECV_DLENGTH |
				  ISCSI_MAX_XMIT_DLENGTH |
				  ISCSI_HDRDGST_EN |
				  ISCSI_DATADGST_EN |
				  ISCSI_INITIAL_R2T_EN |
				  ISCSI_MAX_R2T |
				  ISCSI_IMM_DATA_EN |
				  ISCSI_FIRST_BURST |
				  ISCSI_MAX_BURST |
				  ISCSI_PDU_INORDER_EN |
				  ISCSI_DATASEQ_INORDER_EN |
				  ISCSI_ERL |
				  ISCSI_CONN_PORT |
				  ISCSI_CONN_ADDRESS |
				  ISCSI_EXP_STATSN,
	.host_template		= &iscsi_sht,
	.conndata_size		= sizeof(struct iscsi_conn),
	.max_conn		= 1,
	.max_cmd_len		= ISCSI_TCP_MAX_CMD_LEN,
	/* session management */
	.create_session		= iscsi_tcp_session_create,
	.destroy_session	= iscsi_tcp_session_destroy,
	/* connection management */
	.create_conn		= iscsi_tcp_conn_create,
	.bind_conn		= iscsi_tcp_conn_bind,
	.destroy_conn		= iscsi_tcp_conn_destroy,
	.set_param		= iscsi_conn_set_param,
	.get_conn_param		= iscsi_conn_get_param,
	.get_conn_str_param	= iscsi_conn_get_str_param,
	.get_session_param	= iscsi_session_get_param,
	.start_conn		= iscsi_conn_start,
	.stop_conn		= iscsi_conn_stop,
	/* these are called as part of conn recovery */
	.suspend_conn_recv	= iscsi_tcp_suspend_conn_rx,
	.terminate_conn		= iscsi_tcp_terminate_conn,
	/* IO */
	.send_pdu		= iscsi_conn_send_pdu,
	.get_stats		= iscsi_conn_get_stats,
	.init_cmd_task		= iscsi_tcp_cmd_init,
	.init_mgmt_task		= iscsi_tcp_mgmt_init,
	.xmit_cmd_task		= iscsi_tcp_ctask_xmit,
	.xmit_mgmt_task		= iscsi_tcp_mtask_xmit,
	.cleanup_cmd_task	= iscsi_tcp_cleanup_ctask,
	/* recovery */
	.session_recovery_timedout = iscsi_session_recovery_timedout,
};

static int __init
iscsi_tcp_init(void)
{
	if (iscsi_max_lun < 1) {
		printk(KERN_ERR "iscsi_tcp: Invalid max_lun value of %u\n",
		       iscsi_max_lun);
		return -EINVAL;
	}
	iscsi_tcp_transport.max_lun = iscsi_max_lun;

	if (!iscsi_register_transport(&iscsi_tcp_transport))
		return -ENODEV;

	return 0;
}

static void __exit
iscsi_tcp_exit(void)
{
	iscsi_unregister_transport(&iscsi_tcp_transport);
}

module_init(iscsi_tcp_init);
module_exit(iscsi_tcp_exit);
