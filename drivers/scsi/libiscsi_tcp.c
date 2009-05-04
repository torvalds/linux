/*
 * iSCSI over TCP/IP Data-Path lib
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

MODULE_AUTHOR("Mike Christie <michaelc@cs.wisc.edu>, "
	      "Dmitry Yusupov <dmitry_yus@yahoo.com>, "
	      "Alex Aizman <itn780@yahoo.com>");
MODULE_DESCRIPTION("iSCSI/TCP data-path");
MODULE_LICENSE("GPL");

static int iscsi_dbg_libtcp;
module_param_named(debug_libiscsi_tcp, iscsi_dbg_libtcp, int,
		   S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_libiscsi_tcp, "Turn on debugging for libiscsi_tcp "
		 "module. Set to 1 to turn on, and zero to turn off. Default "
		 "is off.");

#define ISCSI_DBG_TCP(_conn, dbg_fmt, arg...)			\
	do {							\
		if (iscsi_dbg_libtcp)				\
			iscsi_conn_printk(KERN_INFO, _conn,	\
					     "%s " dbg_fmt,	\
					     __func__, ##arg);	\
	} while (0);

static int iscsi_tcp_hdr_recv_done(struct iscsi_tcp_conn *tcp_conn,
				   struct iscsi_segment *segment);

/*
 * Scatterlist handling: inside the iscsi_segment, we
 * remember an index into the scatterlist, and set data/size
 * to the current scatterlist entry. For highmem pages, we
 * kmap as needed.
 *
 * Note that the page is unmapped when we return from
 * TCP's data_ready handler, so we may end up mapping and
 * unmapping the same page repeatedly. The whole reason
 * for this is that we shouldn't keep the page mapped
 * outside the softirq.
 */

/**
 * iscsi_tcp_segment_init_sg - init indicated scatterlist entry
 * @segment: the buffer object
 * @sg: scatterlist
 * @offset: byte offset into that sg entry
 *
 * This function sets up the segment so that subsequent
 * data is copied to the indicated sg entry, at the given
 * offset.
 */
static inline void
iscsi_tcp_segment_init_sg(struct iscsi_segment *segment,
			  struct scatterlist *sg, unsigned int offset)
{
	segment->sg = sg;
	segment->sg_offset = offset;
	segment->size = min(sg->length - offset,
			    segment->total_size - segment->total_copied);
	segment->data = NULL;
}

/**
 * iscsi_tcp_segment_map - map the current S/G page
 * @segment: iscsi_segment
 * @recv: 1 if called from recv path
 *
 * We only need to possibly kmap data if scatter lists are being used,
 * because the iscsi passthrough and internal IO paths will never use high
 * mem pages.
 */
static void iscsi_tcp_segment_map(struct iscsi_segment *segment, int recv)
{
	struct scatterlist *sg;

	if (segment->data != NULL || !segment->sg)
		return;

	sg = segment->sg;
	BUG_ON(segment->sg_mapped);
	BUG_ON(sg->length == 0);

	/*
	 * If the page count is greater than one it is ok to send
	 * to the network layer's zero copy send path. If not we
	 * have to go the slow sendmsg path. We always map for the
	 * recv path.
	 */
	if (page_count(sg_page(sg)) >= 1 && !recv)
		return;

	segment->sg_mapped = kmap_atomic(sg_page(sg), KM_SOFTIRQ0);
	segment->data = segment->sg_mapped + sg->offset + segment->sg_offset;
}

void iscsi_tcp_segment_unmap(struct iscsi_segment *segment)
{
	if (segment->sg_mapped) {
		kunmap_atomic(segment->sg_mapped, KM_SOFTIRQ0);
		segment->sg_mapped = NULL;
		segment->data = NULL;
	}
}
EXPORT_SYMBOL_GPL(iscsi_tcp_segment_unmap);

/*
 * Splice the digest buffer into the buffer
 */
static inline void
iscsi_tcp_segment_splice_digest(struct iscsi_segment *segment, void *digest)
{
	segment->data = digest;
	segment->digest_len = ISCSI_DIGEST_SIZE;
	segment->total_size += ISCSI_DIGEST_SIZE;
	segment->size = ISCSI_DIGEST_SIZE;
	segment->copied = 0;
	segment->sg = NULL;
	segment->hash = NULL;
}

/**
 * iscsi_tcp_segment_done - check whether the segment is complete
 * @tcp_conn: iscsi tcp connection
 * @segment: iscsi segment to check
 * @recv: set to one of this is called from the recv path
 * @copied: number of bytes copied
 *
 * Check if we're done receiving this segment. If the receive
 * buffer is full but we expect more data, move on to the
 * next entry in the scatterlist.
 *
 * If the amount of data we received isn't a multiple of 4,
 * we will transparently receive the pad bytes, too.
 *
 * This function must be re-entrant.
 */
int iscsi_tcp_segment_done(struct iscsi_tcp_conn *tcp_conn,
			   struct iscsi_segment *segment, int recv,
			   unsigned copied)
{
	struct scatterlist sg;
	unsigned int pad;

	ISCSI_DBG_TCP(tcp_conn->iscsi_conn, "copied %u %u size %u %s\n",
		      segment->copied, copied, segment->size,
		      recv ? "recv" : "xmit");
	if (segment->hash && copied) {
		/*
		 * If a segment is kmapd we must unmap it before sending
		 * to the crypto layer since that will try to kmap it again.
		 */
		iscsi_tcp_segment_unmap(segment);

		if (!segment->data) {
			sg_init_table(&sg, 1);
			sg_set_page(&sg, sg_page(segment->sg), copied,
				    segment->copied + segment->sg_offset +
							segment->sg->offset);
		} else
			sg_init_one(&sg, segment->data + segment->copied,
				    copied);
		crypto_hash_update(segment->hash, &sg, copied);
	}

	segment->copied += copied;
	if (segment->copied < segment->size) {
		iscsi_tcp_segment_map(segment, recv);
		return 0;
	}

	segment->total_copied += segment->copied;
	segment->copied = 0;
	segment->size = 0;

	/* Unmap the current scatterlist page, if there is one. */
	iscsi_tcp_segment_unmap(segment);

	/* Do we have more scatterlist entries? */
	ISCSI_DBG_TCP(tcp_conn->iscsi_conn, "total copied %u total size %u\n",
		      segment->total_copied, segment->total_size);
	if (segment->total_copied < segment->total_size) {
		/* Proceed to the next entry in the scatterlist. */
		iscsi_tcp_segment_init_sg(segment, sg_next(segment->sg),
					  0);
		iscsi_tcp_segment_map(segment, recv);
		BUG_ON(segment->size == 0);
		return 0;
	}

	/* Do we need to handle padding? */
	if (!(tcp_conn->iscsi_conn->session->tt->caps & CAP_PADDING_OFFLOAD)) {
		pad = iscsi_padding(segment->total_copied);
		if (pad != 0) {
			ISCSI_DBG_TCP(tcp_conn->iscsi_conn,
				      "consume %d pad bytes\n", pad);
			segment->total_size += pad;
			segment->size = pad;
			segment->data = segment->padbuf;
			return 0;
		}
	}

	/*
	 * Set us up for transferring the data digest. hdr digest
	 * is completely handled in hdr done function.
	 */
	if (segment->hash) {
		crypto_hash_final(segment->hash, segment->digest);
		iscsi_tcp_segment_splice_digest(segment,
				 recv ? segment->recv_digest : segment->digest);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(iscsi_tcp_segment_done);

/**
 * iscsi_tcp_segment_recv - copy data to segment
 * @tcp_conn: the iSCSI TCP connection
 * @segment: the buffer to copy to
 * @ptr: data pointer
 * @len: amount of data available
 *
 * This function copies up to @len bytes to the
 * given buffer, and returns the number of bytes
 * consumed, which can actually be less than @len.
 *
 * If hash digest is enabled, the function will update the
 * hash while copying.
 * Combining these two operations doesn't buy us a lot (yet),
 * but in the future we could implement combined copy+crc,
 * just way we do for network layer checksums.
 */
static int
iscsi_tcp_segment_recv(struct iscsi_tcp_conn *tcp_conn,
		       struct iscsi_segment *segment, const void *ptr,
		       unsigned int len)
{
	unsigned int copy = 0, copied = 0;

	while (!iscsi_tcp_segment_done(tcp_conn, segment, 1, copy)) {
		if (copied == len) {
			ISCSI_DBG_TCP(tcp_conn->iscsi_conn,
				      "copied %d bytes\n", len);
			break;
		}

		copy = min(len - copied, segment->size - segment->copied);
		ISCSI_DBG_TCP(tcp_conn->iscsi_conn, "copying %d\n", copy);
		memcpy(segment->data + segment->copied, ptr + copied, copy);
		copied += copy;
	}
	return copied;
}

inline void
iscsi_tcp_dgst_header(struct hash_desc *hash, const void *hdr, size_t hdrlen,
		      unsigned char digest[ISCSI_DIGEST_SIZE])
{
	struct scatterlist sg;

	sg_init_one(&sg, hdr, hdrlen);
	crypto_hash_digest(hash, &sg, hdrlen, digest);
}
EXPORT_SYMBOL_GPL(iscsi_tcp_dgst_header);

static inline int
iscsi_tcp_dgst_verify(struct iscsi_tcp_conn *tcp_conn,
		      struct iscsi_segment *segment)
{
	if (!segment->digest_len)
		return 1;

	if (memcmp(segment->recv_digest, segment->digest,
		   segment->digest_len)) {
		ISCSI_DBG_TCP(tcp_conn->iscsi_conn, "digest mismatch\n");
		return 0;
	}

	return 1;
}

/*
 * Helper function to set up segment buffer
 */
static inline void
__iscsi_segment_init(struct iscsi_segment *segment, size_t size,
		     iscsi_segment_done_fn_t *done, struct hash_desc *hash)
{
	memset(segment, 0, sizeof(*segment));
	segment->total_size = size;
	segment->done = done;

	if (hash) {
		segment->hash = hash;
		crypto_hash_init(hash);
	}
}

inline void
iscsi_segment_init_linear(struct iscsi_segment *segment, void *data,
			  size_t size, iscsi_segment_done_fn_t *done,
			  struct hash_desc *hash)
{
	__iscsi_segment_init(segment, size, done, hash);
	segment->data = data;
	segment->size = size;
}
EXPORT_SYMBOL_GPL(iscsi_segment_init_linear);

inline int
iscsi_segment_seek_sg(struct iscsi_segment *segment,
		      struct scatterlist *sg_list, unsigned int sg_count,
		      unsigned int offset, size_t size,
		      iscsi_segment_done_fn_t *done, struct hash_desc *hash)
{
	struct scatterlist *sg;
	unsigned int i;

	__iscsi_segment_init(segment, size, done, hash);
	for_each_sg(sg_list, sg, sg_count, i) {
		if (offset < sg->length) {
			iscsi_tcp_segment_init_sg(segment, sg, offset);
			return 0;
		}
		offset -= sg->length;
	}

	return ISCSI_ERR_DATA_OFFSET;
}
EXPORT_SYMBOL_GPL(iscsi_segment_seek_sg);

/**
 * iscsi_tcp_hdr_recv_prep - prep segment for hdr reception
 * @tcp_conn: iscsi connection to prep for
 *
 * This function always passes NULL for the hash argument, because when this
 * function is called we do not yet know the final size of the header and want
 * to delay the digest processing until we know that.
 */
void iscsi_tcp_hdr_recv_prep(struct iscsi_tcp_conn *tcp_conn)
{
	ISCSI_DBG_TCP(tcp_conn->iscsi_conn,
		      "(%s)\n", tcp_conn->iscsi_conn->hdrdgst_en ?
		      "digest enabled" : "digest disabled");
	iscsi_segment_init_linear(&tcp_conn->in.segment,
				tcp_conn->in.hdr_buf, sizeof(struct iscsi_hdr),
				iscsi_tcp_hdr_recv_done, NULL);
}
EXPORT_SYMBOL_GPL(iscsi_tcp_hdr_recv_prep);

/*
 * Handle incoming reply to any other type of command
 */
static int
iscsi_tcp_data_recv_done(struct iscsi_tcp_conn *tcp_conn,
			 struct iscsi_segment *segment)
{
	struct iscsi_conn *conn = tcp_conn->iscsi_conn;
	int rc = 0;

	if (!iscsi_tcp_dgst_verify(tcp_conn, segment))
		return ISCSI_ERR_DATA_DGST;

	rc = iscsi_complete_pdu(conn, tcp_conn->in.hdr,
			conn->data, tcp_conn->in.datalen);
	if (rc)
		return rc;

	iscsi_tcp_hdr_recv_prep(tcp_conn);
	return 0;
}

static void
iscsi_tcp_data_recv_prep(struct iscsi_tcp_conn *tcp_conn)
{
	struct iscsi_conn *conn = tcp_conn->iscsi_conn;
	struct hash_desc *rx_hash = NULL;

	if (conn->datadgst_en &
	    !(conn->session->tt->caps & CAP_DIGEST_OFFLOAD))
		rx_hash = tcp_conn->rx_hash;

	iscsi_segment_init_linear(&tcp_conn->in.segment,
				conn->data, tcp_conn->in.datalen,
				iscsi_tcp_data_recv_done, rx_hash);
}

/**
 * iscsi_tcp_cleanup_task - free tcp_task resources
 * @task: iscsi task
 *
 * must be called with session lock
 */
void iscsi_tcp_cleanup_task(struct iscsi_task *task)
{
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct iscsi_r2t_info *r2t;

	/* nothing to do for mgmt or pending tasks */
	if (!task->sc || task->state == ISCSI_TASK_PENDING)
		return;

	/* flush task's r2t queues */
	while (__kfifo_get(tcp_task->r2tqueue, (void*)&r2t, sizeof(void*))) {
		__kfifo_put(tcp_task->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		ISCSI_DBG_TCP(task->conn, "pending r2t dropped\n");
	}

	r2t = tcp_task->r2t;
	if (r2t != NULL) {
		__kfifo_put(tcp_task->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		tcp_task->r2t = NULL;
	}
}
EXPORT_SYMBOL_GPL(iscsi_tcp_cleanup_task);

/**
 * iscsi_tcp_data_in - SCSI Data-In Response processing
 * @conn: iscsi connection
 * @task: scsi command task
 */
static int iscsi_tcp_data_in(struct iscsi_conn *conn, struct iscsi_task *task)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct iscsi_data_rsp *rhdr = (struct iscsi_data_rsp *)tcp_conn->in.hdr;
	int datasn = be32_to_cpu(rhdr->datasn);
	unsigned total_in_length = scsi_in(task->sc)->length;

	iscsi_update_cmdsn(conn->session, (struct iscsi_nopin*)rhdr);
	if (tcp_conn->in.datalen == 0)
		return 0;

	if (tcp_task->exp_datasn != datasn) {
		ISCSI_DBG_TCP(conn, "task->exp_datasn(%d) != rhdr->datasn(%d)"
			      "\n", tcp_task->exp_datasn, datasn);
		return ISCSI_ERR_DATASN;
	}

	tcp_task->exp_datasn++;

	tcp_task->data_offset = be32_to_cpu(rhdr->offset);
	if (tcp_task->data_offset + tcp_conn->in.datalen > total_in_length) {
		ISCSI_DBG_TCP(conn, "data_offset(%d) + data_len(%d) > "
			      "total_length_in(%d)\n", tcp_task->data_offset,
			      tcp_conn->in.datalen, total_in_length);
		return ISCSI_ERR_DATA_OFFSET;
	}

	conn->datain_pdus_cnt++;
	return 0;
}

/**
 * iscsi_tcp_r2t_rsp - iSCSI R2T Response processing
 * @conn: iscsi connection
 * @task: scsi command task
 */
static int iscsi_tcp_r2t_rsp(struct iscsi_conn *conn, struct iscsi_task *task)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_r2t_rsp *rhdr = (struct iscsi_r2t_rsp *)tcp_conn->in.hdr;
	struct iscsi_r2t_info *r2t;
	int r2tsn = be32_to_cpu(rhdr->r2tsn);
	int rc;

	if (tcp_conn->in.datalen) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "invalid R2t with datalen %d\n",
				  tcp_conn->in.datalen);
		return ISCSI_ERR_DATALEN;
	}

	if (tcp_task->exp_datasn != r2tsn){
		ISCSI_DBG_TCP(conn, "task->exp_datasn(%d) != rhdr->r2tsn(%d)\n",
			      tcp_task->exp_datasn, r2tsn);
		return ISCSI_ERR_R2TSN;
	}

	/* fill-in new R2T associated with the task */
	iscsi_update_cmdsn(session, (struct iscsi_nopin*)rhdr);

	if (!task->sc || session->state != ISCSI_STATE_LOGGED_IN) {
		iscsi_conn_printk(KERN_INFO, conn,
				  "dropping R2T itt %d in recovery.\n",
				  task->itt);
		return 0;
	}

	rc = __kfifo_get(tcp_task->r2tpool.queue, (void*)&r2t, sizeof(void*));
	if (!rc) {
		iscsi_conn_printk(KERN_ERR, conn, "Could not allocate R2T. "
				  "Target has sent more R2Ts than it "
				  "negotiated for or driver has has leaked.\n");
		return ISCSI_ERR_PROTO;
	}

	r2t->exp_statsn = rhdr->statsn;
	r2t->data_length = be32_to_cpu(rhdr->data_length);
	if (r2t->data_length == 0) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "invalid R2T with zero data len\n");
		__kfifo_put(tcp_task->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		return ISCSI_ERR_DATALEN;
	}

	if (r2t->data_length > session->max_burst)
		ISCSI_DBG_TCP(conn, "invalid R2T with data len %u and max "
			      "burst %u. Attempting to execute request.\n",
			      r2t->data_length, session->max_burst);

	r2t->data_offset = be32_to_cpu(rhdr->data_offset);
	if (r2t->data_offset + r2t->data_length > scsi_out(task->sc)->length) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "invalid R2T with data len %u at offset %u "
				  "and total length %d\n", r2t->data_length,
				  r2t->data_offset, scsi_out(task->sc)->length);
		__kfifo_put(tcp_task->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		return ISCSI_ERR_DATALEN;
	}

	r2t->ttt = rhdr->ttt; /* no flip */
	r2t->datasn = 0;
	r2t->sent = 0;

	tcp_task->exp_datasn = r2tsn + 1;
	__kfifo_put(tcp_task->r2tqueue, (void*)&r2t, sizeof(void*));
	conn->r2t_pdus_cnt++;

	iscsi_requeue_task(task);
	return 0;
}

/*
 * Handle incoming reply to DataIn command
 */
static int
iscsi_tcp_process_data_in(struct iscsi_tcp_conn *tcp_conn,
			  struct iscsi_segment *segment)
{
	struct iscsi_conn *conn = tcp_conn->iscsi_conn;
	struct iscsi_hdr *hdr = tcp_conn->in.hdr;
	int rc;

	if (!iscsi_tcp_dgst_verify(tcp_conn, segment))
		return ISCSI_ERR_DATA_DGST;

	/* check for non-exceptional status */
	if (hdr->flags & ISCSI_FLAG_DATA_STATUS) {
		rc = iscsi_complete_pdu(conn, tcp_conn->in.hdr, NULL, 0);
		if (rc)
			return rc;
	}

	iscsi_tcp_hdr_recv_prep(tcp_conn);
	return 0;
}

/**
 * iscsi_tcp_hdr_dissect - process PDU header
 * @conn: iSCSI connection
 * @hdr: PDU header
 *
 * This function analyzes the header of the PDU received,
 * and performs several sanity checks. If the PDU is accompanied
 * by data, the receive buffer is set up to copy the incoming data
 * to the correct location.
 */
static int
iscsi_tcp_hdr_dissect(struct iscsi_conn *conn, struct iscsi_hdr *hdr)
{
	int rc = 0, opcode, ahslen;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_task *task;

	/* verify PDU length */
	tcp_conn->in.datalen = ntoh24(hdr->dlength);
	if (tcp_conn->in.datalen > conn->max_recv_dlength) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "iscsi_tcp: datalen %d > %d\n",
				  tcp_conn->in.datalen, conn->max_recv_dlength);
		return ISCSI_ERR_DATALEN;
	}

	/* Additional header segments. So far, we don't
	 * process additional headers.
	 */
	ahslen = hdr->hlength << 2;

	opcode = hdr->opcode & ISCSI_OPCODE_MASK;
	/* verify itt (itt encoding: age+cid+itt) */
	rc = iscsi_verify_itt(conn, hdr->itt);
	if (rc)
		return rc;

	ISCSI_DBG_TCP(conn, "opcode 0x%x ahslen %d datalen %d\n",
		      opcode, ahslen, tcp_conn->in.datalen);

	switch(opcode) {
	case ISCSI_OP_SCSI_DATA_IN:
		spin_lock(&conn->session->lock);
		task = iscsi_itt_to_ctask(conn, hdr->itt);
		if (!task)
			rc = ISCSI_ERR_BAD_ITT;
		else
			rc = iscsi_tcp_data_in(conn, task);
		if (rc) {
			spin_unlock(&conn->session->lock);
			break;
		}

		if (tcp_conn->in.datalen) {
			struct iscsi_tcp_task *tcp_task = task->dd_data;
			struct hash_desc *rx_hash = NULL;
			struct scsi_data_buffer *sdb = scsi_in(task->sc);

			/*
			 * Setup copy of Data-In into the Scsi_Cmnd
			 * Scatterlist case:
			 * We set up the iscsi_segment to point to the next
			 * scatterlist entry to copy to. As we go along,
			 * we move on to the next scatterlist entry and
			 * update the digest per-entry.
			 */
			if (conn->datadgst_en &&
			    !(conn->session->tt->caps & CAP_DIGEST_OFFLOAD))
				rx_hash = tcp_conn->rx_hash;

			ISCSI_DBG_TCP(conn, "iscsi_tcp_begin_data_in( "
				     "offset=%d, datalen=%d)\n",
				      tcp_task->data_offset,
				      tcp_conn->in.datalen);
			rc = iscsi_segment_seek_sg(&tcp_conn->in.segment,
						   sdb->table.sgl,
						   sdb->table.nents,
						   tcp_task->data_offset,
						   tcp_conn->in.datalen,
						   iscsi_tcp_process_data_in,
						   rx_hash);
			spin_unlock(&conn->session->lock);
			return rc;
		}
		rc = __iscsi_complete_pdu(conn, hdr, NULL, 0);
		spin_unlock(&conn->session->lock);
		break;
	case ISCSI_OP_SCSI_CMD_RSP:
		if (tcp_conn->in.datalen) {
			iscsi_tcp_data_recv_prep(tcp_conn);
			return 0;
		}
		rc = iscsi_complete_pdu(conn, hdr, NULL, 0);
		break;
	case ISCSI_OP_R2T:
		spin_lock(&conn->session->lock);
		task = iscsi_itt_to_ctask(conn, hdr->itt);
		if (!task)
			rc = ISCSI_ERR_BAD_ITT;
		else if (ahslen)
			rc = ISCSI_ERR_AHSLEN;
		else if (task->sc->sc_data_direction == DMA_TO_DEVICE)
			rc = iscsi_tcp_r2t_rsp(conn, task);
		else
			rc = ISCSI_ERR_PROTO;
		spin_unlock(&conn->session->lock);
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
		if (ISCSI_DEF_MAX_RECV_SEG_LEN < tcp_conn->in.datalen) {
			iscsi_conn_printk(KERN_ERR, conn,
					  "iscsi_tcp: received buffer of "
					  "len %u but conn buffer is only %u "
					  "(opcode %0x)\n",
					  tcp_conn->in.datalen,
					  ISCSI_DEF_MAX_RECV_SEG_LEN, opcode);
			rc = ISCSI_ERR_PROTO;
			break;
		}

		/* If there's data coming in with the response,
		 * receive it to the connection's buffer.
		 */
		if (tcp_conn->in.datalen) {
			iscsi_tcp_data_recv_prep(tcp_conn);
			return 0;
		}
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

	if (rc == 0) {
		/* Anything that comes with data should have
		 * been handled above. */
		if (tcp_conn->in.datalen)
			return ISCSI_ERR_PROTO;
		iscsi_tcp_hdr_recv_prep(tcp_conn);
	}

	return rc;
}

/**
 * iscsi_tcp_hdr_recv_done - process PDU header
 *
 * This is the callback invoked when the PDU header has
 * been received. If the header is followed by additional
 * header segments, we go back for more data.
 */
static int
iscsi_tcp_hdr_recv_done(struct iscsi_tcp_conn *tcp_conn,
			struct iscsi_segment *segment)
{
	struct iscsi_conn *conn = tcp_conn->iscsi_conn;
	struct iscsi_hdr *hdr;

	/* Check if there are additional header segments
	 * *prior* to computing the digest, because we
	 * may need to go back to the caller for more.
	 */
	hdr = (struct iscsi_hdr *) tcp_conn->in.hdr_buf;
	if (segment->copied == sizeof(struct iscsi_hdr) && hdr->hlength) {
		/* Bump the header length - the caller will
		 * just loop around and get the AHS for us, and
		 * call again. */
		unsigned int ahslen = hdr->hlength << 2;

		/* Make sure we don't overflow */
		if (sizeof(*hdr) + ahslen > sizeof(tcp_conn->in.hdr_buf))
			return ISCSI_ERR_AHSLEN;

		segment->total_size += ahslen;
		segment->size += ahslen;
		return 0;
	}

	/* We're done processing the header. See if we're doing
	 * header digests; if so, set up the recv_digest buffer
	 * and go back for more. */
	if (conn->hdrdgst_en &&
	    !(conn->session->tt->caps & CAP_DIGEST_OFFLOAD)) {
		if (segment->digest_len == 0) {
			/*
			 * Even if we offload the digest processing we
			 * splice it in so we can increment the skb/segment
			 * counters in preparation for the data segment.
			 */
			iscsi_tcp_segment_splice_digest(segment,
							segment->recv_digest);
			return 0;
		}

		iscsi_tcp_dgst_header(tcp_conn->rx_hash, hdr,
				      segment->total_copied - ISCSI_DIGEST_SIZE,
				      segment->digest);

		if (!iscsi_tcp_dgst_verify(tcp_conn, segment))
			return ISCSI_ERR_HDR_DGST;
	}

	tcp_conn->in.hdr = hdr;
	return iscsi_tcp_hdr_dissect(conn, hdr);
}

/**
 * iscsi_tcp_recv_segment_is_hdr - tests if we are reading in a header
 * @tcp_conn: iscsi tcp conn
 *
 * returns non zero if we are currently processing or setup to process
 * a header.
 */
inline int iscsi_tcp_recv_segment_is_hdr(struct iscsi_tcp_conn *tcp_conn)
{
	return tcp_conn->in.segment.done == iscsi_tcp_hdr_recv_done;
}
EXPORT_SYMBOL_GPL(iscsi_tcp_recv_segment_is_hdr);

/**
 * iscsi_tcp_recv_skb - Process skb
 * @conn: iscsi connection
 * @skb: network buffer with header and/or data segment
 * @offset: offset in skb
 * @offload: bool indicating if transfer was offloaded
 *
 * Will return status of transfer in status. And will return
 * number of bytes copied.
 */
int iscsi_tcp_recv_skb(struct iscsi_conn *conn, struct sk_buff *skb,
		       unsigned int offset, bool offloaded, int *status)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_segment *segment = &tcp_conn->in.segment;
	struct skb_seq_state seq;
	unsigned int consumed = 0;
	int rc = 0;

	ISCSI_DBG_TCP(conn, "in %d bytes\n", skb->len - offset);

	if (unlikely(conn->suspend_rx)) {
		ISCSI_DBG_TCP(conn, "Rx suspended!\n");
		*status = ISCSI_TCP_SUSPENDED;
		return 0;
	}

	if (offloaded) {
		segment->total_copied = segment->total_size;
		goto segment_done;
	}

	skb_prepare_seq_read(skb, offset, skb->len, &seq);
	while (1) {
		unsigned int avail;
		const u8 *ptr;

		avail = skb_seq_read(consumed, &ptr, &seq);
		if (avail == 0) {
			ISCSI_DBG_TCP(conn, "no more data avail. Consumed %d\n",
				      consumed);
			*status = ISCSI_TCP_SKB_DONE;
			skb_abort_seq_read(&seq);
			goto skb_done;
		}
		BUG_ON(segment->copied >= segment->size);

		ISCSI_DBG_TCP(conn, "skb %p ptr=%p avail=%u\n", skb, ptr,
			      avail);
		rc = iscsi_tcp_segment_recv(tcp_conn, segment, ptr, avail);
		BUG_ON(rc == 0);
		consumed += rc;

		if (segment->total_copied >= segment->total_size) {
			skb_abort_seq_read(&seq);
			goto segment_done;
		}
	}

segment_done:
	*status = ISCSI_TCP_SEGMENT_DONE;
	ISCSI_DBG_TCP(conn, "segment done\n");
	rc = segment->done(tcp_conn, segment);
	if (rc != 0) {
		*status = ISCSI_TCP_CONN_ERR;
		ISCSI_DBG_TCP(conn, "Error receiving PDU, errno=%d\n", rc);
		iscsi_conn_failure(conn, rc);
		return 0;
	}
	/* The done() functions sets up the next segment. */

skb_done:
	conn->rxdata_octets += consumed;
	return consumed;
}
EXPORT_SYMBOL_GPL(iscsi_tcp_recv_skb);

/**
 * iscsi_tcp_task_init - Initialize iSCSI SCSI_READ or SCSI_WRITE commands
 * @conn: iscsi connection
 * @task: scsi command task
 * @sc: scsi command
 */
int iscsi_tcp_task_init(struct iscsi_task *task)
{
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct scsi_cmnd *sc = task->sc;
	int err;

	if (!sc) {
		/*
		 * mgmt tasks do not have a scatterlist since they come
		 * in from the iscsi interface.
		 */
		ISCSI_DBG_TCP(conn, "mtask deq [itt 0x%x]\n", task->itt);

		return conn->session->tt->init_pdu(task, 0, task->data_count);
	}

	BUG_ON(__kfifo_len(tcp_task->r2tqueue));
	tcp_task->exp_datasn = 0;

	/* Prepare PDU, optionally w/ immediate data */
	ISCSI_DBG_TCP(conn, "task deq [itt 0x%x imm %d unsol %d]\n",
		      task->itt, task->imm_count, task->unsol_r2t.data_length);

	err = conn->session->tt->init_pdu(task, 0, task->imm_count);
	if (err)
		return err;
	task->imm_count = 0;
	return 0;
}
EXPORT_SYMBOL_GPL(iscsi_tcp_task_init);

static struct iscsi_r2t_info *iscsi_tcp_get_curr_r2t(struct iscsi_task *task)
{
	struct iscsi_session *session = task->conn->session;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct iscsi_r2t_info *r2t = NULL;

	if (iscsi_task_has_unsol_data(task))
		r2t = &task->unsol_r2t;
	else {
		spin_lock_bh(&session->lock);
		if (tcp_task->r2t) {
			r2t = tcp_task->r2t;
			/* Continue with this R2T? */
			if (r2t->data_length <= r2t->sent) {
				ISCSI_DBG_TCP(task->conn,
					      "  done with r2t %p\n", r2t);
				__kfifo_put(tcp_task->r2tpool.queue,
					    (void *)&tcp_task->r2t,
					    sizeof(void *));
				tcp_task->r2t = r2t = NULL;
			}
		}

		if (r2t == NULL) {
			__kfifo_get(tcp_task->r2tqueue,
				    (void *)&tcp_task->r2t, sizeof(void *));
			r2t = tcp_task->r2t;
		}
		spin_unlock_bh(&session->lock);
	}

	return r2t;
}

/**
 * iscsi_tcp_task_xmit - xmit normal PDU task
 * @task: iscsi command task
 *
 * We're expected to return 0 when everything was transmitted succesfully,
 * -EAGAIN if there's still data in the queue, or != 0 for any other kind
 * of error.
 */
int iscsi_tcp_task_xmit(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *session = conn->session;
	struct iscsi_r2t_info *r2t;
	int rc = 0;

flush:
	/* Flush any pending data first. */
	rc = session->tt->xmit_pdu(task);
	if (rc < 0)
		return rc;

	/* mgmt command */
	if (!task->sc) {
		if (task->hdr->itt == RESERVED_ITT)
			iscsi_put_task(task);
		return 0;
	}

	/* Are we done already? */
	if (task->sc->sc_data_direction != DMA_TO_DEVICE)
		return 0;

	r2t = iscsi_tcp_get_curr_r2t(task);
	if (r2t == NULL) {
		/* Waiting for more R2Ts to arrive. */
		ISCSI_DBG_TCP(conn, "no R2Ts yet\n");
		return 0;
	}

	rc = conn->session->tt->alloc_pdu(task, ISCSI_OP_SCSI_DATA_OUT);
	if (rc)
		return rc;
	iscsi_prep_data_out_pdu(task, r2t, (struct iscsi_data *) task->hdr);

	ISCSI_DBG_TCP(conn, "sol dout %p [dsn %d itt 0x%x doff %d dlen %d]\n",
		      r2t, r2t->datasn - 1, task->hdr->itt,
		      r2t->data_offset + r2t->sent, r2t->data_count);

	rc = conn->session->tt->init_pdu(task, r2t->data_offset + r2t->sent,
					 r2t->data_count);
	if (rc)
		return rc;
	r2t->sent += r2t->data_count;
	goto flush;
}
EXPORT_SYMBOL_GPL(iscsi_tcp_task_xmit);

struct iscsi_cls_conn *
iscsi_tcp_conn_setup(struct iscsi_cls_session *cls_session, int dd_data_size,
		      uint32_t conn_idx)

{
	struct iscsi_conn *conn;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_tcp_conn *tcp_conn;

	cls_conn = iscsi_conn_setup(cls_session, sizeof(*tcp_conn), conn_idx);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;
	/*
	 * due to strange issues with iser these are not set
	 * in iscsi_conn_setup
	 */
	conn->max_recv_dlength = ISCSI_DEF_MAX_RECV_SEG_LEN;

	tcp_conn = conn->dd_data;
	tcp_conn->iscsi_conn = conn;

	tcp_conn->dd_data = kzalloc(dd_data_size, GFP_KERNEL);
	if (!tcp_conn->dd_data) {
		iscsi_conn_teardown(cls_conn);
		return NULL;
	}
	return cls_conn;
}
EXPORT_SYMBOL_GPL(iscsi_tcp_conn_setup);

void iscsi_tcp_conn_teardown(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	kfree(tcp_conn->dd_data);
	iscsi_conn_teardown(cls_conn);
}
EXPORT_SYMBOL_GPL(iscsi_tcp_conn_teardown);

int iscsi_tcp_r2tpool_alloc(struct iscsi_session *session)
{
	int i;
	int cmd_i;

	/*
	 * initialize per-task: R2T pool and xmit queue
	 */
	for (cmd_i = 0; cmd_i < session->cmds_max; cmd_i++) {
	        struct iscsi_task *task = session->cmds[cmd_i];
		struct iscsi_tcp_task *tcp_task = task->dd_data;

		/*
		 * pre-allocated x2 as much r2ts to handle race when
		 * target acks DataOut faster than we data_xmit() queues
		 * could replenish r2tqueue.
		 */

		/* R2T pool */
		if (iscsi_pool_init(&tcp_task->r2tpool,
				    session->max_r2t * 2, NULL,
				    sizeof(struct iscsi_r2t_info))) {
			goto r2t_alloc_fail;
		}

		/* R2T xmit queue */
		tcp_task->r2tqueue = kfifo_alloc(
		      session->max_r2t * 4 * sizeof(void*), GFP_KERNEL, NULL);
		if (tcp_task->r2tqueue == ERR_PTR(-ENOMEM)) {
			iscsi_pool_free(&tcp_task->r2tpool);
			goto r2t_alloc_fail;
		}
	}

	return 0;

r2t_alloc_fail:
	for (i = 0; i < cmd_i; i++) {
		struct iscsi_task *task = session->cmds[i];
		struct iscsi_tcp_task *tcp_task = task->dd_data;

		kfifo_free(tcp_task->r2tqueue);
		iscsi_pool_free(&tcp_task->r2tpool);
	}
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(iscsi_tcp_r2tpool_alloc);

void iscsi_tcp_r2tpool_free(struct iscsi_session *session)
{
	int i;

	for (i = 0; i < session->cmds_max; i++) {
		struct iscsi_task *task = session->cmds[i];
		struct iscsi_tcp_task *tcp_task = task->dd_data;

		kfifo_free(tcp_task->r2tqueue);
		iscsi_pool_free(&tcp_task->r2tpool);
	}
}
EXPORT_SYMBOL_GPL(iscsi_tcp_r2tpool_free);

void iscsi_tcp_conn_get_stats(struct iscsi_cls_conn *cls_conn,
			      struct iscsi_stats *stats)
{
	struct iscsi_conn *conn = cls_conn->dd_data;

	stats->txdata_octets = conn->txdata_octets;
	stats->rxdata_octets = conn->rxdata_octets;
	stats->scsicmd_pdus = conn->scsicmd_pdus_cnt;
	stats->dataout_pdus = conn->dataout_pdus_cnt;
	stats->scsirsp_pdus = conn->scsirsp_pdus_cnt;
	stats->datain_pdus = conn->datain_pdus_cnt;
	stats->r2t_pdus = conn->r2t_pdus_cnt;
	stats->tmfcmd_pdus = conn->tmfcmd_pdus_cnt;
	stats->tmfrsp_pdus = conn->tmfrsp_pdus_cnt;
}
EXPORT_SYMBOL_GPL(iscsi_tcp_conn_get_stats);
