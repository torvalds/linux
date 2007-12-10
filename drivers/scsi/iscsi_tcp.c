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
#include <linux/file.h>
#include <linux/blkdev.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/scatterlist.h>
#include <net/tcp.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/scsi_transport_iscsi.h>

#include "iscsi_tcp.h"

MODULE_AUTHOR("Dmitry Yusupov <dmitry_yus@yahoo.com>, "
	      "Alex Aizman <itn780@yahoo.com>");
MODULE_DESCRIPTION("iSCSI/TCP data-path");
MODULE_LICENSE("GPL");
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
	sg_init_one(&ibuf->sg, vbuf, size);
	ibuf->sent = 0;
	ibuf->use_sendmsg = 1;
}

static inline void
iscsi_buf_init_sg(struct iscsi_buf *ibuf, struct scatterlist *sg)
{
	sg_init_table(&ibuf->sg, 1);
	sg_set_page(&ibuf->sg, sg_page(sg), sg->length, sg->offset);
	/*
	 * Fastpath: sg element fits into single page
	 */
	if (sg->length + sg->offset <= PAGE_SIZE && !PageSlab(sg_page(sg)))
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

	crypto_hash_digest(&tcp_conn->tx_hash, &buf->sg, buf->sg.length, crc);
	buf->sg.length += sizeof(u32);
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
iscsi_tcp_cleanup_ctask(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_r2t_info *r2t;
	struct scsi_cmnd *sc;

	/* flush ctask's r2t queues */
	while (__kfifo_get(tcp_ctask->r2tqueue, (void*)&r2t, sizeof(void*))) {
		__kfifo_put(tcp_ctask->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		debug_scsi("iscsi_tcp_cleanup_ctask pending r2t dropped\n");
	}

	sc = ctask->sc;
	if (unlikely(!sc))
		return;

	tcp_ctask->xmstate = XMSTATE_VALUE_IDLE;
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
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_data_rsp *rhdr = (struct iscsi_data_rsp *)tcp_conn->in.hdr;
	struct iscsi_session *session = conn->session;
	struct scsi_cmnd *sc = ctask->sc;
	int datasn = be32_to_cpu(rhdr->datasn);

	iscsi_update_cmdsn(session, (struct iscsi_nopin*)rhdr);
	/*
	 * setup Data-In byte counter (gets decremented..)
	 */
	ctask->data_count = tcp_conn->in.datalen;

	if (tcp_conn->in.datalen == 0)
		return 0;

	if (tcp_ctask->exp_datasn != datasn) {
		debug_tcp("%s: ctask->exp_datasn(%d) != rhdr->datasn(%d)\n",
		          __FUNCTION__, tcp_ctask->exp_datasn, datasn);
		return ISCSI_ERR_DATASN;
	}

	tcp_ctask->exp_datasn++;

	tcp_ctask->data_offset = be32_to_cpu(rhdr->offset);
	if (tcp_ctask->data_offset + tcp_conn->in.datalen > scsi_bufflen(sc)) {
		debug_tcp("%s: data_offset(%d) + data_len(%d) > total_length_in(%d)\n",
		          __FUNCTION__, tcp_ctask->data_offset,
		          tcp_conn->in.datalen, scsi_bufflen(sc));
		return ISCSI_ERR_DATA_OFFSET;
	}

	if (rhdr->flags & ISCSI_FLAG_DATA_STATUS) {
		conn->exp_statsn = be32_to_cpu(rhdr->statsn) + 1;
		if (rhdr->flags & ISCSI_FLAG_DATA_UNDERFLOW) {
			int res_count = be32_to_cpu(rhdr->residual_count);

			if (res_count > 0 &&
			    res_count <= scsi_bufflen(sc)) {
				scsi_set_resid(sc, res_count);
				sc->result = (DID_OK << 16) | rhdr->cmd_status;
			} else
				sc->result = (DID_BAD_TARGET << 16) |
					rhdr->cmd_status;
		} else if (rhdr->flags & ISCSI_FLAG_DATA_OVERFLOW) {
			scsi_set_resid(sc, be32_to_cpu(rhdr->residual_count));
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
	int i, sg_count = 0;
	struct scatterlist *sg;

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

	sg = scsi_sglist(sc);
	r2t->sg = NULL;
	for (i = 0; i < scsi_sg_count(sc); i++, sg += 1) {
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

	if (tcp_conn->in.datalen) {
		printk(KERN_ERR "iscsi_tcp: invalid R2t with datalen %d\n",
		       tcp_conn->in.datalen);
		return ISCSI_ERR_DATALEN;
	}

	if (tcp_ctask->exp_datasn != r2tsn){
		debug_tcp("%s: ctask->exp_datasn(%d) != rhdr->r2tsn(%d)\n",
		          __FUNCTION__, tcp_ctask->exp_datasn, r2tsn);
		return ISCSI_ERR_R2TSN;
	}

	/* fill-in new R2T associated with the task */
	spin_lock(&session->lock);
	iscsi_update_cmdsn(session, (struct iscsi_nopin*)rhdr);

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
	if (r2t->data_length == 0) {
		printk(KERN_ERR "iscsi_tcp: invalid R2T with zero data len\n");
		spin_unlock(&session->lock);
		return ISCSI_ERR_DATALEN;
	}

	if (r2t->data_length > session->max_burst)
		debug_scsi("invalid R2T with data len %u and max burst %u."
			   "Attempting to execute request.\n",
			    r2t->data_length, session->max_burst);

	r2t->data_offset = be32_to_cpu(rhdr->data_offset);
	if (r2t->data_offset + r2t->data_length > scsi_bufflen(ctask->sc)) {
		spin_unlock(&session->lock);
		printk(KERN_ERR "iscsi_tcp: invalid R2T with data len %u at "
		       "offset %u and total length %d\n", r2t->data_length,
		       r2t->data_offset, scsi_bufflen(ctask->sc));
		return ISCSI_ERR_DATALEN;
	}

	r2t->ttt = rhdr->ttt; /* no flip */
	r2t->solicit_datasn = 0;

	iscsi_solicit_data_init(conn, ctask, r2t);

	tcp_ctask->exp_datasn = r2tsn + 1;
	__kfifo_put(tcp_ctask->r2tqueue, (void*)&r2t, sizeof(void*));
	set_bit(XMSTATE_BIT_SOL_HDR_INIT, &tcp_ctask->xmstate);
	list_move_tail(&ctask->running, &conn->xmitqueue);

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
		crypto_hash_digest(&tcp_conn->rx_hash, &sg, sg.length,
				   (u8 *)&cdgst);
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
		if (rc)
			return rc;
		/* fall through */
	case ISCSI_OP_SCSI_CMD_RSP:
		tcp_conn->in.ctask = session->cmds[itt];
		if (tcp_conn->in.datalen)
			goto copy_hdr;

		spin_lock(&session->lock);
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
	case ISCSI_OP_REJECT:
	case ISCSI_OP_ASYNC_EVENT:
		/*
		 * It is possible that we could get a PDU with a buffer larger
		 * than 8K, but there are no targets that currently do this.
		 * For now we fail until we find a vendor that needs it
		 */
		if (ISCSI_DEF_MAX_RECV_SEG_LEN <
		    tcp_conn->in.datalen) {
			printk(KERN_ERR "iscsi_tcp: received buffer of len %u "
			      "but conn buffer is only %u (opcode %0x)\n",
			      tcp_conn->in.datalen,
			      ISCSI_DEF_MAX_RECV_SEG_LEN, opcode);
			rc = ISCSI_ERR_PROTO;
			break;
		}

		if (tcp_conn->in.datalen)
			goto copy_hdr;
	/* fall through */
	case ISCSI_OP_LOGOUT_RSP:
	case ISCSI_OP_NOOP_IN:
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
	if (tcp_conn->in.zero_copy_hdr && tcp_conn->in.copy <=
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
	unsigned size = min(tcp_conn->in.copy, buf_left);
	int rc;

	size = min(size, ctask->data_count);

	debug_tcp("ctask_copy %d bytes at offset %d copied %d\n",
	       size, tcp_conn->in.offset, tcp_conn->in.copied);

	BUG_ON(size <= 0);
	BUG_ON(tcp_ctask->sent + size > scsi_bufflen(ctask->sc));

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
iscsi_tcp_copy(struct iscsi_conn *conn, int buf_size)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	int buf_left = buf_size - tcp_conn->data_copied;
	int size = min(tcp_conn->in.copy, buf_left);
	int rc;

	debug_tcp("tcp_copy %d bytes at offset %d copied %d\n",
	       size, tcp_conn->in.offset, tcp_conn->data_copied);
	BUG_ON(size <= 0);

	rc = skb_copy_bits(tcp_conn->in.skb, tcp_conn->in.offset,
			   (char*)conn->data + tcp_conn->data_copied, size);
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
partial_sg_digest_update(struct hash_desc *desc, struct scatterlist *sg,
			 int offset, int length)
{
	struct scatterlist temp;

	sg_init_table(&temp, 1);
	sg_set_page(&temp, sg_page(sg), length, offset);
	crypto_hash_update(desc, &temp, length);
}

static void
iscsi_recv_digest_update(struct iscsi_tcp_conn *tcp_conn, char* buf, int len)
{
	struct scatterlist tmp;

	sg_init_one(&tmp, buf, len);
	crypto_hash_update(&tcp_conn->rx_hash, &tmp, len);
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

	offset = tcp_ctask->data_offset;
	sg = scsi_sglist(sc);

	if (tcp_ctask->data_offset)
		for (i = 0; i < tcp_ctask->sg_count; i++)
			offset -= sg[i].length;
	/* we've passed through partial sg*/
	if (offset < 0)
		offset = 0;

	for (i = tcp_ctask->sg_count; i < scsi_sg_count(sc); i++) {
		char *dest;

		dest = kmap_atomic(sg_page(&sg[i]), KM_SOFTIRQ0);
		rc = iscsi_ctask_copy(tcp_conn, ctask, dest + sg[i].offset,
				      sg[i].length, offset);
		kunmap_atomic(dest, KM_SOFTIRQ0);
		if (rc == -EAGAIN)
			/* continue with the next SKB/PDU */
			return rc;
		if (!rc) {
			if (conn->datadgst_en) {
				if (!offset)
					crypto_hash_update(
							&tcp_conn->rx_hash,
							&sg[i], sg[i].length);
				else
					partial_sg_digest_update(
							&tcp_conn->rx_hash,
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
				partial_sg_digest_update(&tcp_conn->rx_hash,
							 &sg[i],
							 sg[i].offset,
							 sg[i].length-rc);
			rc = 0;
			break;
		}

		if (!tcp_conn->in.copy)
			return -EAGAIN;
	}
	BUG_ON(ctask->data_count);

	/* check for non-exceptional status */
	if (tcp_conn->in.hdr->flags & ISCSI_FLAG_DATA_STATUS) {
		debug_scsi("done [sc %lx res %d itt 0x%x flags 0x%x]\n",
			   (long)sc, sc->result, ctask->itt,
			   tcp_conn->in.hdr->flags);
		spin_lock(&conn->session->lock);
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
	case ISCSI_OP_TEXT_RSP:
	case ISCSI_OP_LOGIN_RSP:
	case ISCSI_OP_ASYNC_EVENT:
	case ISCSI_OP_REJECT:
		/*
		 * Collect data segment to the connection's data
		 * placeholder
		 */
		if (iscsi_tcp_copy(conn, tcp_conn->in.datalen)) {
			rc = -EAGAIN;
			goto exit;
		}

		rc = iscsi_complete_pdu(conn, tcp_conn->in.hdr, conn->data,
					tcp_conn->in.datalen);
		if (!rc && conn->datadgst_en && opcode != ISCSI_OP_LOGIN_RSP)
			iscsi_recv_digest_update(tcp_conn, conn->data,
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
				iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
				return 0;
		       }
		}

		/*
		 * Verify and process incoming PDU header.
		 */
		rc = iscsi_tcp_hdr_recv(conn);
		if (!rc && tcp_conn->in.datalen) {
			if (conn->datadgst_en)
				crypto_hash_init(&tcp_conn->rx_hash);
			tcp_conn->in_progress = IN_PROGRESS_DATA_RECV;
		} else if (rc) {
			iscsi_conn_failure(conn, rc);
			return 0;
		}
	}

	if (tcp_conn->in_progress == IN_PROGRESS_DDIGEST_RECV &&
	    tcp_conn->in.copy) {
		uint32_t recv_digest;

		debug_tcp("extra data_recv offset %d copy %d\n",
			  tcp_conn->in.offset, tcp_conn->in.copy);

		if (!tcp_conn->data_copied) {
			if (tcp_conn->in.padding) {
				debug_tcp("padding -> %d\n",
					  tcp_conn->in.padding);
				memset(pad, 0, tcp_conn->in.padding);
				sg_init_one(&sg, pad, tcp_conn->in.padding);
				crypto_hash_update(&tcp_conn->rx_hash,
						   &sg, sg.length);
			}
			crypto_hash_final(&tcp_conn->rx_hash,
					  (u8 *) &tcp_conn->in.datadgst);
			debug_tcp("rx digest 0x%x\n", tcp_conn->in.datadgst);
		}

		rc = iscsi_tcp_copy(conn, sizeof(uint32_t));
		if (rc) {
			if (rc == -EAGAIN)
				goto again;
			iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
			return 0;
		}

		memcpy(&recv_digest, conn->data, sizeof(uint32_t));
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
			iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
			return 0;
		}

		if (tcp_conn->in.padding)
			tcp_conn->in_progress = IN_PROGRESS_PAD_RECV;
		else if (conn->datadgst_en)
			tcp_conn->in_progress = IN_PROGRESS_DDIGEST_RECV;
		else
			tcp_conn->in_progress = IN_PROGRESS_WAIT_HEADER;
		tcp_conn->data_copied = 0;
	}

	if (tcp_conn->in_progress == IN_PROGRESS_PAD_RECV &&
	    tcp_conn->in.copy) {
		int copylen = min(tcp_conn->in.padding - tcp_conn->data_copied,
				  tcp_conn->in.copy);

		tcp_conn->in.copy -= copylen;
		tcp_conn->in.offset += copylen;
		tcp_conn->data_copied += copylen;

		if (tcp_conn->data_copied != tcp_conn->in.padding)
			tcp_conn->in_progress = IN_PROGRESS_PAD_RECV;
		else if (conn->datadgst_en)
			tcp_conn->in_progress = IN_PROGRESS_DDIGEST_RECV;
		else
			tcp_conn->in_progress = IN_PROGRESS_WAIT_HEADER;
		tcp_conn->data_copied = 0;
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
iscsi_conn_restore_callbacks(struct iscsi_tcp_conn *tcp_conn)
{
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
	int offset = buf->sg.offset + buf->sent, res;

	/*
	 * if we got use_sg=0 or are sending something we kmallocd
	 * then we did not have to do kmap (kmap returns page_address)
	 *
	 * if we got use_sg > 0, but had to drop down, we do not
	 * set clustering so this should only happen for that
	 * slab case.
	 */
	if (buf->use_sendmsg)
		res = sock_no_sendpage(sk, sg_page(&buf->sg), offset, size, flags);
	else
		res = tcp_conn->sendpage(sk, sg_page(&buf->sg), offset, size, flags);

	if (res >= 0) {
		conn->txdata_octets += res;
		buf->sent += res;
		return res;
	}

	tcp_conn->sendpage_failures_cnt++;
	if (res == -EAGAIN)
		res = -ENOBUFS;
	else
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	return res;
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
	int flags = 0; /* MSG_DONTWAIT; */
	int res, size;

	size = buf->sg.length - buf->sent;
	BUG_ON(buf->sent + size > buf->sg.length);
	if (buf->sent + size != buf->sg.length || datalen)
		flags |= MSG_MORE;

	res = iscsi_send(conn, buf, size, flags);
	debug_tcp("sendhdr %d bytes, sent %d res %d\n", size, buf->sent, res);
	if (res >= 0) {
		if (size != res)
			return -EAGAIN;
		return 0;
	}

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
		*count -= res;
		*sent += res;
		if (size != res)
			return -EAGAIN;
		return 0;
	}

	return res;
}

static inline void
iscsi_data_digest_init(struct iscsi_tcp_conn *tcp_conn,
		      struct iscsi_tcp_cmd_task *tcp_ctask)
{
	crypto_hash_init(&tcp_conn->tx_hash);
	tcp_ctask->digest_count = 4;
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
	struct iscsi_data *hdr;
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

	if (iscsi_buf_left(&r2t->sendbuf))
		return;

	iscsi_buf_init_sg(&r2t->sendbuf, r2t->sg);
	r2t->sg += 1;
}

static void iscsi_set_padding(struct iscsi_tcp_cmd_task *tcp_ctask,
			      unsigned long len)
{
	tcp_ctask->pad_count = len & (ISCSI_PAD_LEN - 1);
	if (!tcp_ctask->pad_count)
		return;

	tcp_ctask->pad_count = ISCSI_PAD_LEN - tcp_ctask->pad_count;
	debug_scsi("write padding %d bytes\n", tcp_ctask->pad_count);
	set_bit(XMSTATE_BIT_W_PAD, &tcp_ctask->xmstate);
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
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

	BUG_ON(__kfifo_len(tcp_ctask->r2tqueue));
	tcp_ctask->xmstate = 1 << XMSTATE_BIT_CMD_HDR_INIT;
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
 *	Management xmit state machine consists of these states:
 *		XMSTATE_BIT_IMM_HDR_INIT - calculate digest of PDU Header
 *		XMSTATE_BIT_IMM_HDR      - PDU Header xmit in progress
 *		XMSTATE_BIT_IMM_DATA     - PDU Data xmit in progress
 *		XMSTATE_VALUE_IDLE       - management PDU is done
 **/
static int
iscsi_tcp_mtask_xmit(struct iscsi_conn *conn, struct iscsi_mgmt_task *mtask)
{
	struct iscsi_tcp_mgmt_task *tcp_mtask = mtask->dd_data;
	int rc;

	debug_scsi("mtask deq [cid %d state %x itt 0x%x]\n",
		conn->id, tcp_mtask->xmstate, mtask->itt);

	if (test_bit(XMSTATE_BIT_IMM_HDR_INIT, &tcp_mtask->xmstate)) {
		iscsi_buf_init_iov(&tcp_mtask->headbuf, (char*)mtask->hdr,
				   sizeof(struct iscsi_hdr));

		if (mtask->data_count) {
			set_bit(XMSTATE_BIT_IMM_DATA, &tcp_mtask->xmstate);
			iscsi_buf_init_iov(&tcp_mtask->sendbuf,
					   (char*)mtask->data,
					   mtask->data_count);
		}

		if (conn->c_stage != ISCSI_CONN_INITIAL_STAGE &&
		    conn->stop_stage != STOP_CONN_RECOVER &&
		    conn->hdrdgst_en)
			iscsi_hdr_digest(conn, &tcp_mtask->headbuf,
					(u8*)tcp_mtask->hdrext);

		tcp_mtask->sent = 0;
		clear_bit(XMSTATE_BIT_IMM_HDR_INIT, &tcp_mtask->xmstate);
		set_bit(XMSTATE_BIT_IMM_HDR, &tcp_mtask->xmstate);
	}

	if (test_bit(XMSTATE_BIT_IMM_HDR, &tcp_mtask->xmstate)) {
		rc = iscsi_sendhdr(conn, &tcp_mtask->headbuf,
				   mtask->data_count);
		if (rc)
			return rc;
		clear_bit(XMSTATE_BIT_IMM_HDR, &tcp_mtask->xmstate);
	}

	if (test_and_clear_bit(XMSTATE_BIT_IMM_DATA, &tcp_mtask->xmstate)) {
		BUG_ON(!mtask->data_count);
		/* FIXME: implement.
		 * Virtual buffer could be spreaded across multiple pages...
		 */
		do {
			int rc;

			rc = iscsi_sendpage(conn, &tcp_mtask->sendbuf,
					&mtask->data_count, &tcp_mtask->sent);
			if (rc) {
				set_bit(XMSTATE_BIT_IMM_DATA, &tcp_mtask->xmstate);
				return rc;
			}
		} while (mtask->data_count);
	}

	BUG_ON(tcp_mtask->xmstate != XMSTATE_VALUE_IDLE);
	if (mtask->hdr->itt == RESERVED_ITT) {
		struct iscsi_session *session = conn->session;

		spin_lock_bh(&session->lock);
		list_del(&conn->mtask->running);
		__kfifo_put(session->mgmtpool.queue, (void*)&conn->mtask,
			    sizeof(void*));
		spin_unlock_bh(&session->lock);
	}
	return 0;
}

static int
iscsi_send_cmd_hdr(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct scsi_cmnd *sc = ctask->sc;
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	int rc = 0;

	if (test_bit(XMSTATE_BIT_CMD_HDR_INIT, &tcp_ctask->xmstate)) {
		tcp_ctask->sent = 0;
		tcp_ctask->sg_count = 0;
		tcp_ctask->exp_datasn = 0;

		if (sc->sc_data_direction == DMA_TO_DEVICE) {
			struct scatterlist *sg = scsi_sglist(sc);

			iscsi_buf_init_sg(&tcp_ctask->sendbuf, sg);
			tcp_ctask->sg = sg + 1;
			tcp_ctask->bad_sg = sg + scsi_sg_count(sc);

			debug_scsi("cmd [itt 0x%x total %d imm_data %d "
				   "unsol count %d, unsol offset %d]\n",
				   ctask->itt, scsi_bufflen(sc),
				   ctask->imm_count, ctask->unsol_count,
				   ctask->unsol_offset);
		}

		iscsi_buf_init_iov(&tcp_ctask->headbuf, (char*)ctask->hdr,
				  sizeof(struct iscsi_hdr));

		if (conn->hdrdgst_en)
			iscsi_hdr_digest(conn, &tcp_ctask->headbuf,
					 (u8*)tcp_ctask->hdrext);
		clear_bit(XMSTATE_BIT_CMD_HDR_INIT, &tcp_ctask->xmstate);
		set_bit(XMSTATE_BIT_CMD_HDR_XMIT, &tcp_ctask->xmstate);
	}

	if (test_bit(XMSTATE_BIT_CMD_HDR_XMIT, &tcp_ctask->xmstate)) {
		rc = iscsi_sendhdr(conn, &tcp_ctask->headbuf, ctask->imm_count);
		if (rc)
			return rc;
		clear_bit(XMSTATE_BIT_CMD_HDR_XMIT, &tcp_ctask->xmstate);

		if (sc->sc_data_direction != DMA_TO_DEVICE)
			return 0;

		if (ctask->imm_count) {
			set_bit(XMSTATE_BIT_IMM_DATA, &tcp_ctask->xmstate);
			iscsi_set_padding(tcp_ctask, ctask->imm_count);

			if (ctask->conn->datadgst_en) {
				iscsi_data_digest_init(ctask->conn->dd_data,
						       tcp_ctask);
				tcp_ctask->immdigest = 0;
			}
		}

		if (ctask->unsol_count) {
			set_bit(XMSTATE_BIT_UNS_HDR, &tcp_ctask->xmstate);
			set_bit(XMSTATE_BIT_UNS_INIT, &tcp_ctask->xmstate);
		}
	}
	return rc;
}

static int
iscsi_send_padding(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	int sent = 0, rc;

	if (test_bit(XMSTATE_BIT_W_PAD, &tcp_ctask->xmstate)) {
		iscsi_buf_init_iov(&tcp_ctask->sendbuf, (char*)&tcp_ctask->pad,
				   tcp_ctask->pad_count);
		if (conn->datadgst_en)
			crypto_hash_update(&tcp_conn->tx_hash,
					   &tcp_ctask->sendbuf.sg,
					   tcp_ctask->sendbuf.sg.length);
	} else if (!test_bit(XMSTATE_BIT_W_RESEND_PAD, &tcp_ctask->xmstate))
		return 0;

	clear_bit(XMSTATE_BIT_W_PAD, &tcp_ctask->xmstate);
	clear_bit(XMSTATE_BIT_W_RESEND_PAD, &tcp_ctask->xmstate);
	debug_scsi("sending %d pad bytes for itt 0x%x\n",
		   tcp_ctask->pad_count, ctask->itt);
	rc = iscsi_sendpage(conn, &tcp_ctask->sendbuf, &tcp_ctask->pad_count,
			   &sent);
	if (rc) {
		debug_scsi("padding send failed %d\n", rc);
		set_bit(XMSTATE_BIT_W_RESEND_PAD, &tcp_ctask->xmstate);
	}
	return rc;
}

static int
iscsi_send_digest(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask,
			struct iscsi_buf *buf, uint32_t *digest)
{
	struct iscsi_tcp_cmd_task *tcp_ctask;
	struct iscsi_tcp_conn *tcp_conn;
	int rc, sent = 0;

	if (!conn->datadgst_en)
		return 0;

	tcp_ctask = ctask->dd_data;
	tcp_conn = conn->dd_data;

	if (!test_bit(XMSTATE_BIT_W_RESEND_DATA_DIGEST, &tcp_ctask->xmstate)) {
		crypto_hash_final(&tcp_conn->tx_hash, (u8*)digest);
		iscsi_buf_init_iov(buf, (char*)digest, 4);
	}
	clear_bit(XMSTATE_BIT_W_RESEND_DATA_DIGEST, &tcp_ctask->xmstate);

	rc = iscsi_sendpage(conn, buf, &tcp_ctask->digest_count, &sent);
	if (!rc)
		debug_scsi("sent digest 0x%x for itt 0x%x\n", *digest,
			  ctask->itt);
	else {
		debug_scsi("sending digest 0x%x failed for itt 0x%x!\n",
			  *digest, ctask->itt);
		set_bit(XMSTATE_BIT_W_RESEND_DATA_DIGEST, &tcp_ctask->xmstate);
	}
	return rc;
}

static int
iscsi_send_data(struct iscsi_cmd_task *ctask, struct iscsi_buf *sendbuf,
		struct scatterlist **sg, int *sent, int *count,
		struct iscsi_buf *digestbuf, uint32_t *digest)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_conn *conn = ctask->conn;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	int rc, buf_sent, offset;

	while (*count) {
		buf_sent = 0;
		offset = sendbuf->sent;

		rc = iscsi_sendpage(conn, sendbuf, count, &buf_sent);
		*sent = *sent + buf_sent;
		if (buf_sent && conn->datadgst_en)
			partial_sg_digest_update(&tcp_conn->tx_hash,
				&sendbuf->sg, sendbuf->sg.offset + offset,
				buf_sent);
		if (!iscsi_buf_left(sendbuf) && *sg != tcp_ctask->bad_sg) {
			iscsi_buf_init_sg(sendbuf, *sg);
			*sg = *sg + 1;
		}

		if (rc)
			return rc;
	}

	rc = iscsi_send_padding(conn, ctask);
	if (rc)
		return rc;

	return iscsi_send_digest(conn, ctask, digestbuf, digest);
}

static int
iscsi_send_unsol_hdr(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_data_task *dtask;
	int rc;

	set_bit(XMSTATE_BIT_UNS_DATA, &tcp_ctask->xmstate);
	if (test_bit(XMSTATE_BIT_UNS_INIT, &tcp_ctask->xmstate)) {
		dtask = &tcp_ctask->unsol_dtask;

		iscsi_prep_unsolicit_data_pdu(ctask, &dtask->hdr);
		iscsi_buf_init_iov(&tcp_ctask->headbuf, (char*)&dtask->hdr,
				   sizeof(struct iscsi_hdr));
		if (conn->hdrdgst_en)
			iscsi_hdr_digest(conn, &tcp_ctask->headbuf,
					(u8*)dtask->hdrext);

		clear_bit(XMSTATE_BIT_UNS_INIT, &tcp_ctask->xmstate);
		iscsi_set_padding(tcp_ctask, ctask->data_count);
	}

	rc = iscsi_sendhdr(conn, &tcp_ctask->headbuf, ctask->data_count);
	if (rc) {
		clear_bit(XMSTATE_BIT_UNS_DATA, &tcp_ctask->xmstate);
		set_bit(XMSTATE_BIT_UNS_HDR, &tcp_ctask->xmstate);
		return rc;
	}

	if (conn->datadgst_en) {
		dtask = &tcp_ctask->unsol_dtask;
		iscsi_data_digest_init(ctask->conn->dd_data, tcp_ctask);
		dtask->digest = 0;
	}

	debug_scsi("uns dout [itt 0x%x dlen %d sent %d]\n",
		   ctask->itt, ctask->unsol_count, tcp_ctask->sent);
	return 0;
}

static int
iscsi_send_unsol_pdu(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	int rc;

	if (test_and_clear_bit(XMSTATE_BIT_UNS_HDR, &tcp_ctask->xmstate)) {
		BUG_ON(!ctask->unsol_count);
send_hdr:
		rc = iscsi_send_unsol_hdr(conn, ctask);
		if (rc)
			return rc;
	}

	if (test_bit(XMSTATE_BIT_UNS_DATA, &tcp_ctask->xmstate)) {
		struct iscsi_data_task *dtask = &tcp_ctask->unsol_dtask;
		int start = tcp_ctask->sent;

		rc = iscsi_send_data(ctask, &tcp_ctask->sendbuf, &tcp_ctask->sg,
				     &tcp_ctask->sent, &ctask->data_count,
				     &dtask->digestbuf, &dtask->digest);
		ctask->unsol_count -= tcp_ctask->sent - start;
		if (rc)
			return rc;
		clear_bit(XMSTATE_BIT_UNS_DATA, &tcp_ctask->xmstate);
		/*
		 * Done with the Data-Out. Next, check if we need
		 * to send another unsolicited Data-Out.
		 */
		if (ctask->unsol_count) {
			debug_scsi("sending more uns\n");
			set_bit(XMSTATE_BIT_UNS_INIT, &tcp_ctask->xmstate);
			goto send_hdr;
		}
	}
	return 0;
}

static int iscsi_send_sol_pdu(struct iscsi_conn *conn,
			      struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_r2t_info *r2t;
	struct iscsi_data_task *dtask;
	int left, rc;

	if (test_bit(XMSTATE_BIT_SOL_HDR_INIT, &tcp_ctask->xmstate)) {
		if (!tcp_ctask->r2t) {
			spin_lock_bh(&session->lock);
			__kfifo_get(tcp_ctask->r2tqueue, (void*)&tcp_ctask->r2t,
				    sizeof(void*));
			spin_unlock_bh(&session->lock);
		}
send_hdr:
		r2t = tcp_ctask->r2t;
		dtask = &r2t->dtask;

		if (conn->hdrdgst_en)
			iscsi_hdr_digest(conn, &r2t->headbuf,
					(u8*)dtask->hdrext);
		clear_bit(XMSTATE_BIT_SOL_HDR_INIT, &tcp_ctask->xmstate);
		set_bit(XMSTATE_BIT_SOL_HDR, &tcp_ctask->xmstate);
	}

	if (test_bit(XMSTATE_BIT_SOL_HDR, &tcp_ctask->xmstate)) {
		r2t = tcp_ctask->r2t;
		dtask = &r2t->dtask;

		rc = iscsi_sendhdr(conn, &r2t->headbuf, r2t->data_count);
		if (rc)
			return rc;
		clear_bit(XMSTATE_BIT_SOL_HDR, &tcp_ctask->xmstate);
		set_bit(XMSTATE_BIT_SOL_DATA, &tcp_ctask->xmstate);

		if (conn->datadgst_en) {
			iscsi_data_digest_init(conn->dd_data, tcp_ctask);
			dtask->digest = 0;
		}

		iscsi_set_padding(tcp_ctask, r2t->data_count);
		debug_scsi("sol dout [dsn %d itt 0x%x dlen %d sent %d]\n",
			r2t->solicit_datasn - 1, ctask->itt, r2t->data_count,
			r2t->sent);
	}

	if (test_bit(XMSTATE_BIT_SOL_DATA, &tcp_ctask->xmstate)) {
		r2t = tcp_ctask->r2t;
		dtask = &r2t->dtask;

		rc = iscsi_send_data(ctask, &r2t->sendbuf, &r2t->sg,
				     &r2t->sent, &r2t->data_count,
				     &dtask->digestbuf, &dtask->digest);
		if (rc)
			return rc;
		clear_bit(XMSTATE_BIT_SOL_DATA, &tcp_ctask->xmstate);

		/*
		 * Done with this Data-Out. Next, check if we have
		 * to send another Data-Out for this R2T.
		 */
		BUG_ON(r2t->data_length - r2t->sent < 0);
		left = r2t->data_length - r2t->sent;
		if (left) {
			iscsi_solicit_data_cont(conn, ctask, r2t, left);
			goto send_hdr;
		}

		/*
		 * Done with this R2T. Check if there are more
		 * outstanding R2Ts ready to be processed.
		 */
		spin_lock_bh(&session->lock);
		tcp_ctask->r2t = NULL;
		__kfifo_put(tcp_ctask->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		if (__kfifo_get(tcp_ctask->r2tqueue, (void*)&r2t,
				sizeof(void*))) {
			tcp_ctask->r2t = r2t;
			spin_unlock_bh(&session->lock);
			goto send_hdr;
		}
		spin_unlock_bh(&session->lock);
	}
	return 0;
}

/**
 * iscsi_tcp_ctask_xmit - xmit normal PDU task
 * @conn: iscsi connection
 * @ctask: iscsi command task
 *
 * Notes:
 *	The function can return -EAGAIN in which case caller must
 *	call it again later, or recover. '0' return code means successful
 *	xmit.
 *	The function is devided to logical helpers (above) for the different
 *	xmit stages.
 *
 *iscsi_send_cmd_hdr()
 *	XMSTATE_BIT_CMD_HDR_INIT - prepare Header and Data buffers Calculate
 *	                           Header Digest
 *	XMSTATE_BIT_CMD_HDR_XMIT - Transmit header in progress
 *
 *iscsi_send_padding
 *	XMSTATE_BIT_W_PAD        - Prepare and send pading
 *	XMSTATE_BIT_W_RESEND_PAD - retry send pading
 *
 *iscsi_send_digest
 *	XMSTATE_BIT_W_RESEND_DATA_DIGEST - Finalize and send Data Digest
 *	XMSTATE_BIT_W_RESEND_DATA_DIGEST - retry sending digest
 *
 *iscsi_send_unsol_hdr
 *	XMSTATE_BIT_UNS_INIT     - prepare un-solicit data header and digest
 *	XMSTATE_BIT_UNS_HDR      - send un-solicit header
 *
 *iscsi_send_unsol_pdu
 *	XMSTATE_BIT_UNS_DATA     - send un-solicit data in progress
 *
 *iscsi_send_sol_pdu
 *	XMSTATE_BIT_SOL_HDR_INIT - solicit data header and digest initialize
 *	XMSTATE_BIT_SOL_HDR      - send solicit header
 *	XMSTATE_BIT_SOL_DATA     - send solicit data
 *
 *iscsi_tcp_ctask_xmit
 *	XMSTATE_BIT_IMM_DATA     - xmit managment data (??)
 **/
static int
iscsi_tcp_ctask_xmit(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	int rc = 0;

	debug_scsi("ctask deq [cid %d xmstate %x itt 0x%x]\n",
		conn->id, tcp_ctask->xmstate, ctask->itt);

	rc = iscsi_send_cmd_hdr(conn, ctask);
	if (rc)
		return rc;
	if (ctask->sc->sc_data_direction != DMA_TO_DEVICE)
		return 0;

	if (test_bit(XMSTATE_BIT_IMM_DATA, &tcp_ctask->xmstate)) {
		rc = iscsi_send_data(ctask, &tcp_ctask->sendbuf, &tcp_ctask->sg,
				     &tcp_ctask->sent, &ctask->imm_count,
				     &tcp_ctask->immbuf, &tcp_ctask->immdigest);
		if (rc)
			return rc;
		clear_bit(XMSTATE_BIT_IMM_DATA, &tcp_ctask->xmstate);
	}

	rc = iscsi_send_unsol_pdu(conn, ctask);
	if (rc)
		return rc;

	rc = iscsi_send_sol_pdu(conn, ctask);
	if (rc)
		return rc;

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
	conn->max_recv_dlength = ISCSI_DEF_MAX_RECV_SEG_LEN;

	tcp_conn = kzalloc(sizeof(*tcp_conn), GFP_KERNEL);
	if (!tcp_conn)
		goto tcp_conn_alloc_fail;

	conn->dd_data = tcp_conn;
	tcp_conn->iscsi_conn = conn;
	tcp_conn->in_progress = IN_PROGRESS_WAIT_HEADER;
	/* initial operational parameters */
	tcp_conn->hdr_size = sizeof(struct iscsi_hdr);

	tcp_conn->tx_hash.tfm = crypto_alloc_hash("crc32c", 0,
						  CRYPTO_ALG_ASYNC);
	tcp_conn->tx_hash.flags = 0;
	if (IS_ERR(tcp_conn->tx_hash.tfm)) {
		printk(KERN_ERR "Could not create connection due to crc32c "
		       "loading error %ld. Make sure the crc32c module is "
		       "built as a module or into the kernel\n",
			PTR_ERR(tcp_conn->tx_hash.tfm));
		goto free_tcp_conn;
	}

	tcp_conn->rx_hash.tfm = crypto_alloc_hash("crc32c", 0,
						  CRYPTO_ALG_ASYNC);
	tcp_conn->rx_hash.flags = 0;
	if (IS_ERR(tcp_conn->rx_hash.tfm)) {
		printk(KERN_ERR "Could not create connection due to crc32c "
		       "loading error %ld. Make sure the crc32c module is "
		       "built as a module or into the kernel\n",
			PTR_ERR(tcp_conn->rx_hash.tfm));
		goto free_tx_tfm;
	}

	return cls_conn;

free_tx_tfm:
	crypto_free_hash(tcp_conn->tx_hash.tfm);
free_tcp_conn:
	kfree(tcp_conn);
tcp_conn_alloc_fail:
	iscsi_conn_teardown(cls_conn);
	return NULL;
}

static void
iscsi_tcp_release_conn(struct iscsi_conn *conn)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct socket *sock = tcp_conn->sock;

	if (!sock)
		return;

	sock_hold(sock->sk);
	iscsi_conn_restore_callbacks(tcp_conn);
	sock_put(sock->sk);

	spin_lock_bh(&session->lock);
	tcp_conn->sock = NULL;
	conn->recv_lock = NULL;
	spin_unlock_bh(&session->lock);
	sockfd_put(sock);
}

static void
iscsi_tcp_conn_destroy(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	iscsi_tcp_release_conn(conn);
	iscsi_conn_teardown(cls_conn);

	if (tcp_conn->tx_hash.tfm)
		crypto_free_hash(tcp_conn->tx_hash.tfm);
	if (tcp_conn->rx_hash.tfm)
		crypto_free_hash(tcp_conn->rx_hash.tfm);

	kfree(tcp_conn);
}

static void
iscsi_tcp_conn_stop(struct iscsi_cls_conn *cls_conn, int flag)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	iscsi_conn_stop(cls_conn, flag);
	iscsi_tcp_release_conn(conn);
	tcp_conn->hdr_size = sizeof(struct iscsi_hdr);
}

static int iscsi_tcp_get_addr(struct iscsi_conn *conn, struct socket *sock,
			      char *buf, int *port,
			      int (*getname)(struct socket *, struct sockaddr *,
					int *addrlen))
{
	struct sockaddr_storage *addr;
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	int rc = 0, len;

	addr = kmalloc(sizeof(*addr), GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	if (getname(sock, (struct sockaddr *) addr, &len)) {
		rc = -ENODEV;
		goto free_addr;
	}

	switch (addr->ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)addr;
		spin_lock_bh(&conn->session->lock);
		sprintf(buf, NIPQUAD_FMT, NIPQUAD(sin->sin_addr.s_addr));
		*port = be16_to_cpu(sin->sin_port);
		spin_unlock_bh(&conn->session->lock);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)addr;
		spin_lock_bh(&conn->session->lock);
		sprintf(buf, NIP6_FMT, NIP6(sin6->sin6_addr));
		*port = be16_to_cpu(sin6->sin6_port);
		spin_unlock_bh(&conn->session->lock);
		break;
	}
free_addr:
	kfree(addr);
	return rc;
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
	/*
	 * copy these values now because if we drop the session
	 * userspace may still want to query the values since we will
	 * be using them for the reconnect
	 */
	err = iscsi_tcp_get_addr(conn, sock, conn->portal_address,
				 &conn->portal_port, kernel_getpeername);
	if (err)
		goto free_socket;

	err = iscsi_tcp_get_addr(conn, sock, conn->local_address,
				&conn->local_port, kernel_getsockname);
	if (err)
		goto free_socket;

	err = iscsi_conn_bind(cls_session, cls_conn, is_leading);
	if (err)
		goto free_socket;

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
	return 0;

free_socket:
	sockfd_put(sock);
	return err;
}

/* called with host lock */
static void
iscsi_tcp_mgmt_init(struct iscsi_conn *conn, struct iscsi_mgmt_task *mtask)
{
	struct iscsi_tcp_mgmt_task *tcp_mtask = mtask->dd_data;
	tcp_mtask->xmstate = 1 << XMSTATE_BIT_IMM_HDR_INIT;
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
		     char *buf, int buflen)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	int value;

	switch(param) {
	case ISCSI_PARAM_HDRDGST_EN:
		iscsi_set_param(cls_conn, param, buf, buflen);
		tcp_conn->hdr_size = sizeof(struct iscsi_hdr);
		if (conn->hdrdgst_en)
			tcp_conn->hdr_size += sizeof(__u32);
		break;
	case ISCSI_PARAM_DATADGST_EN:
		iscsi_set_param(cls_conn, param, buf, buflen);
		tcp_conn->sendpage = conn->datadgst_en ?
			sock_no_sendpage : tcp_conn->sock->ops->sendpage;
		break;
	case ISCSI_PARAM_MAX_R2T:
		sscanf(buf, "%d", &value);
		if (session->max_r2t == roundup_pow_of_two(value))
			break;
		iscsi_r2tpool_free(session);
		iscsi_set_param(cls_conn, param, buf, buflen);
		if (session->max_r2t & (session->max_r2t - 1))
			session->max_r2t = roundup_pow_of_two(session->max_r2t);
		if (iscsi_r2tpool_alloc(session))
			return -ENOMEM;
		break;
	default:
		return iscsi_set_param(cls_conn, param, buf, buflen);
	}

	return 0;
}

static int
iscsi_tcp_conn_get_param(struct iscsi_cls_conn *cls_conn,
			 enum iscsi_param param, char *buf)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	int len;

	switch(param) {
	case ISCSI_PARAM_CONN_PORT:
		spin_lock_bh(&conn->session->lock);
		len = sprintf(buf, "%hu\n", conn->portal_port);
		spin_unlock_bh(&conn->session->lock);
		break;
	case ISCSI_PARAM_CONN_ADDRESS:
		spin_lock_bh(&conn->session->lock);
		len = sprintf(buf, "%s\n", conn->portal_address);
		spin_unlock_bh(&conn->session->lock);
		break;
	default:
		return iscsi_conn_get_param(cls_conn, param, buf);
	}

	return len;
}

static int
iscsi_tcp_host_get_param(struct Scsi_Host *shost, enum iscsi_host_param param,
			 char *buf)
{
        struct iscsi_session *session = iscsi_hostdata(shost->hostdata);
	int len;

	switch (param) {
	case ISCSI_HOST_PARAM_IPADDRESS:
		spin_lock_bh(&session->lock);
		if (!session->leadconn)
			len = -ENODEV;
		else
			len = sprintf(buf, "%s\n",
				     session->leadconn->local_address);
		spin_unlock_bh(&session->lock);
		break;
	default:
		return iscsi_host_get_param(shost, param, buf);
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
			 uint16_t cmds_max, uint16_t qdepth,
			 uint32_t initial_cmdsn, uint32_t *hostno)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	uint32_t hn;
	int cmd_i;

	cls_session = iscsi_session_setup(iscsit, scsit, cmds_max, qdepth,
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

static int iscsi_tcp_slave_configure(struct scsi_device *sdev)
{
	blk_queue_bounce_limit(sdev->request_queue, BLK_BOUNCE_ANY);
	blk_queue_dma_alignment(sdev->request_queue, 0);
	return 0;
}

static struct scsi_host_template iscsi_sht = {
	.module			= THIS_MODULE,
	.name			= "iSCSI Initiator over TCP/IP",
	.queuecommand           = iscsi_queuecommand,
	.change_queue_depth	= iscsi_change_queue_depth,
	.can_queue		= ISCSI_DEF_XMIT_CMDS_MAX - 1,
	.sg_tablesize		= ISCSI_SG_TABLESIZE,
	.max_sectors		= 0xFFFF,
	.cmd_per_lun		= ISCSI_DEF_CMD_PER_LUN,
	.eh_abort_handler       = iscsi_eh_abort,
	.eh_host_reset_handler	= iscsi_eh_host_reset,
	.use_clustering         = DISABLE_CLUSTERING,
	.slave_configure        = iscsi_tcp_slave_configure,
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
				  ISCSI_EXP_STATSN |
				  ISCSI_PERSISTENT_PORT |
				  ISCSI_PERSISTENT_ADDRESS |
				  ISCSI_TARGET_NAME | ISCSI_TPGT |
				  ISCSI_USERNAME | ISCSI_PASSWORD |
				  ISCSI_USERNAME_IN | ISCSI_PASSWORD_IN,
	.host_param_mask	= ISCSI_HOST_HWADDRESS | ISCSI_HOST_IPADDRESS |
				  ISCSI_HOST_INITIATOR_NAME |
				  ISCSI_HOST_NETDEV_NAME,
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
	.get_conn_param		= iscsi_tcp_conn_get_param,
	.get_session_param	= iscsi_session_get_param,
	.start_conn		= iscsi_conn_start,
	.stop_conn		= iscsi_tcp_conn_stop,
	/* iscsi host params */
	.get_host_param		= iscsi_tcp_host_get_param,
	.set_host_param		= iscsi_host_set_param,
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
