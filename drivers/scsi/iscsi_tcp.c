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
#undef DEBUG_TCP
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
static inline void
iscsi_tcp_segment_map(struct iscsi_segment *segment, int recv)
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

	debug_tcp("iscsi_tcp_segment_map %s %p\n", recv ? "recv" : "xmit",
		  segment);
	segment->sg_mapped = kmap_atomic(sg_page(sg), KM_SOFTIRQ0);
	segment->data = segment->sg_mapped + sg->offset + segment->sg_offset;
}

static inline void
iscsi_tcp_segment_unmap(struct iscsi_segment *segment)
{
	debug_tcp("iscsi_tcp_segment_unmap %p\n", segment);

	if (segment->sg_mapped) {
		debug_tcp("iscsi_tcp_segment_unmap valid\n");
		kunmap_atomic(segment->sg_mapped, KM_SOFTIRQ0);
		segment->sg_mapped = NULL;
		segment->data = NULL;
	}
}

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
static inline int
iscsi_tcp_segment_done(struct iscsi_segment *segment, int recv, unsigned copied)
{
	static unsigned char padbuf[ISCSI_PAD_LEN];
	struct scatterlist sg;
	unsigned int pad;

	debug_tcp("copied %u %u size %u %s\n", segment->copied, copied,
		  segment->size, recv ? "recv" : "xmit");
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
	debug_tcp("total copied %u total size %u\n", segment->total_copied,
		   segment->total_size);
	if (segment->total_copied < segment->total_size) {
		/* Proceed to the next entry in the scatterlist. */
		iscsi_tcp_segment_init_sg(segment, sg_next(segment->sg),
					  0);
		iscsi_tcp_segment_map(segment, recv);
		BUG_ON(segment->size == 0);
		return 0;
	}

	/* Do we need to handle padding? */
	pad = iscsi_padding(segment->total_copied);
	if (pad != 0) {
		debug_tcp("consume %d pad bytes\n", pad);
		segment->total_size += pad;
		segment->size = pad;
		segment->data = padbuf;
		return 0;
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

/**
 * iscsi_tcp_xmit_segment - transmit segment
 * @tcp_conn: the iSCSI TCP connection
 * @segment: the buffer to transmnit
 *
 * This function transmits as much of the buffer as
 * the network layer will accept, and returns the number of
 * bytes transmitted.
 *
 * If CRC hashing is enabled, the function will compute the
 * hash as it goes. When the entire segment has been transmitted,
 * it will retrieve the hash value and send it as well.
 */
static int
iscsi_tcp_xmit_segment(struct iscsi_tcp_conn *tcp_conn,
		       struct iscsi_segment *segment)
{
	struct socket *sk = tcp_conn->sock;
	unsigned int copied = 0;
	int r = 0;

	while (!iscsi_tcp_segment_done(segment, 0, r)) {
		struct scatterlist *sg;
		unsigned int offset, copy;
		int flags = 0;

		r = 0;
		offset = segment->copied;
		copy = segment->size - offset;

		if (segment->total_copied + segment->size < segment->total_size)
			flags |= MSG_MORE;

		/* Use sendpage if we can; else fall back to sendmsg */
		if (!segment->data) {
			sg = segment->sg;
			offset += segment->sg_offset + sg->offset;
			r = tcp_conn->sendpage(sk, sg_page(sg), offset, copy,
					       flags);
		} else {
			struct msghdr msg = { .msg_flags = flags };
			struct kvec iov = {
				.iov_base = segment->data + offset,
				.iov_len = copy
			};

			r = kernel_sendmsg(sk, &msg, &iov, 1, copy);
		}

		if (r < 0) {
			iscsi_tcp_segment_unmap(segment);
			if (copied || r == -EAGAIN)
				break;
			return r;
		}
		copied += r;
	}
	return copied;
}

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

	while (!iscsi_tcp_segment_done(segment, 1, copy)) {
		if (copied == len) {
			debug_tcp("iscsi_tcp_segment_recv copied %d bytes\n",
				  len);
			break;
		}

		copy = min(len - copied, segment->size - segment->copied);
		debug_tcp("iscsi_tcp_segment_recv copying %d\n", copy);
		memcpy(segment->data + segment->copied, ptr + copied, copy);
		copied += copy;
	}
	return copied;
}

static inline void
iscsi_tcp_dgst_header(struct hash_desc *hash, const void *hdr, size_t hdrlen,
		      unsigned char digest[ISCSI_DIGEST_SIZE])
{
	struct scatterlist sg;

	sg_init_one(&sg, hdr, hdrlen);
	crypto_hash_digest(hash, &sg, hdrlen, digest);
}

static inline int
iscsi_tcp_dgst_verify(struct iscsi_tcp_conn *tcp_conn,
		      struct iscsi_segment *segment)
{
	if (!segment->digest_len)
		return 1;

	if (memcmp(segment->recv_digest, segment->digest,
		   segment->digest_len)) {
		debug_scsi("digest mismatch\n");
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

static inline void
iscsi_segment_init_linear(struct iscsi_segment *segment, void *data,
			  size_t size, iscsi_segment_done_fn_t *done,
			  struct hash_desc *hash)
{
	__iscsi_segment_init(segment, size, done, hash);
	segment->data = data;
	segment->size = size;
}

static inline int
iscsi_segment_seek_sg(struct iscsi_segment *segment,
		      struct scatterlist *sg_list, unsigned int sg_count,
		      unsigned int offset, size_t size,
		      iscsi_segment_done_fn_t *done, struct hash_desc *hash)
{
	struct scatterlist *sg;
	unsigned int i;

	debug_scsi("iscsi_segment_seek_sg offset %u size %llu\n",
		  offset, size);
	__iscsi_segment_init(segment, size, done, hash);
	for_each_sg(sg_list, sg, sg_count, i) {
		debug_scsi("sg %d, len %u offset %u\n", i, sg->length,
			   sg->offset);
		if (offset < sg->length) {
			iscsi_tcp_segment_init_sg(segment, sg, offset);
			return 0;
		}
		offset -= sg->length;
	}

	return ISCSI_ERR_DATA_OFFSET;
}

/**
 * iscsi_tcp_hdr_recv_prep - prep segment for hdr reception
 * @tcp_conn: iscsi connection to prep for
 *
 * This function always passes NULL for the hash argument, because when this
 * function is called we do not yet know the final size of the header and want
 * to delay the digest processing until we know that.
 */
static void
iscsi_tcp_hdr_recv_prep(struct iscsi_tcp_conn *tcp_conn)
{
	debug_tcp("iscsi_tcp_hdr_recv_prep(%p%s)\n", tcp_conn,
		  tcp_conn->iscsi_conn->hdrdgst_en ? ", digest enabled" : "");
	iscsi_segment_init_linear(&tcp_conn->in.segment,
				tcp_conn->in.hdr_buf, sizeof(struct iscsi_hdr),
				iscsi_tcp_hdr_recv_done, NULL);
}

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

	if (conn->datadgst_en)
		rx_hash = &tcp_conn->rx_hash;

	iscsi_segment_init_linear(&tcp_conn->in.segment,
				conn->data, tcp_conn->in.datalen,
				iscsi_tcp_data_recv_done, rx_hash);
}

/*
 * must be called with session lock
 */
static void
iscsi_tcp_cleanup_ctask(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_r2t_info *r2t;

	/* flush ctask's r2t queues */
	while (__kfifo_get(tcp_ctask->r2tqueue, (void*)&r2t, sizeof(void*))) {
		__kfifo_put(tcp_ctask->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		debug_scsi("iscsi_tcp_cleanup_ctask pending r2t dropped\n");
	}

	r2t = tcp_ctask->r2t;
	if (r2t != NULL) {
		__kfifo_put(tcp_ctask->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		tcp_ctask->r2t = NULL;
	}
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
		sc->result = (DID_OK << 16) | rhdr->cmd_status;
		conn->exp_statsn = be32_to_cpu(rhdr->statsn) + 1;
		if (rhdr->flags & (ISCSI_FLAG_DATA_UNDERFLOW |
		                   ISCSI_FLAG_DATA_OVERFLOW)) {
			int res_count = be32_to_cpu(rhdr->residual_count);

			if (res_count > 0 &&
			    (rhdr->flags & ISCSI_FLAG_CMD_OVERFLOW ||
			     res_count <= scsi_bufflen(sc)))
				scsi_set_resid(sc, res_count);
			else
				sc->result = (DID_BAD_TARGET << 16) |
					rhdr->cmd_status;
		}
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
		iscsi_conn_printk(KERN_ERR, conn,
				  "invalid R2t with datalen %d\n",
				  tcp_conn->in.datalen);
		return ISCSI_ERR_DATALEN;
	}

	if (tcp_ctask->exp_datasn != r2tsn){
		debug_tcp("%s: ctask->exp_datasn(%d) != rhdr->r2tsn(%d)\n",
		          __FUNCTION__, tcp_ctask->exp_datasn, r2tsn);
		return ISCSI_ERR_R2TSN;
	}

	/* fill-in new R2T associated with the task */
	iscsi_update_cmdsn(session, (struct iscsi_nopin*)rhdr);

	if (!ctask->sc || session->state != ISCSI_STATE_LOGGED_IN) {
		iscsi_conn_printk(KERN_INFO, conn,
				  "dropping R2T itt %d in recovery.\n",
				  ctask->itt);
		return 0;
	}

	rc = __kfifo_get(tcp_ctask->r2tpool.queue, (void*)&r2t, sizeof(void*));
	BUG_ON(!rc);

	r2t->exp_statsn = rhdr->statsn;
	r2t->data_length = be32_to_cpu(rhdr->data_length);
	if (r2t->data_length == 0) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "invalid R2T with zero data len\n");
		__kfifo_put(tcp_ctask->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		return ISCSI_ERR_DATALEN;
	}

	if (r2t->data_length > session->max_burst)
		debug_scsi("invalid R2T with data len %u and max burst %u."
			   "Attempting to execute request.\n",
			    r2t->data_length, session->max_burst);

	r2t->data_offset = be32_to_cpu(rhdr->data_offset);
	if (r2t->data_offset + r2t->data_length > scsi_bufflen(ctask->sc)) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "invalid R2T with data len %u at offset %u "
				  "and total length %d\n", r2t->data_length,
				  r2t->data_offset, scsi_bufflen(ctask->sc));
		__kfifo_put(tcp_ctask->r2tpool.queue, (void*)&r2t,
			    sizeof(void*));
		return ISCSI_ERR_DATALEN;
	}

	r2t->ttt = rhdr->ttt; /* no flip */
	r2t->solicit_datasn = 0;

	iscsi_solicit_data_init(conn, ctask, r2t);

	tcp_ctask->exp_datasn = r2tsn + 1;
	__kfifo_put(tcp_ctask->r2tqueue, (void*)&r2t, sizeof(void*));
	conn->r2t_pdus_cnt++;

	iscsi_requeue_ctask(ctask);
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
	struct iscsi_session *session = conn->session;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_cmd_task *ctask;
	uint32_t itt;

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
	rc = iscsi_verify_itt(conn, hdr, &itt);
	if (rc)
		return rc;

	debug_tcp("opcode 0x%x ahslen %d datalen %d\n",
		  opcode, ahslen, tcp_conn->in.datalen);

	switch(opcode) {
	case ISCSI_OP_SCSI_DATA_IN:
		ctask = session->cmds[itt];
		spin_lock(&conn->session->lock);
		rc = iscsi_data_rsp(conn, ctask);
		spin_unlock(&conn->session->lock);
		if (rc)
			return rc;
		if (tcp_conn->in.datalen) {
			struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
			struct hash_desc *rx_hash = NULL;

			/*
			 * Setup copy of Data-In into the Scsi_Cmnd
			 * Scatterlist case:
			 * We set up the iscsi_segment to point to the next
			 * scatterlist entry to copy to. As we go along,
			 * we move on to the next scatterlist entry and
			 * update the digest per-entry.
			 */
			if (conn->datadgst_en)
				rx_hash = &tcp_conn->rx_hash;

			debug_tcp("iscsi_tcp_begin_data_in(%p, offset=%d, "
				  "datalen=%d)\n", tcp_conn,
				  tcp_ctask->data_offset,
				  tcp_conn->in.datalen);
			return iscsi_segment_seek_sg(&tcp_conn->in.segment,
						     scsi_sglist(ctask->sc),
						     scsi_sg_count(ctask->sc),
						     tcp_ctask->data_offset,
						     tcp_conn->in.datalen,
						     iscsi_tcp_process_data_in,
						     rx_hash);
		}
		/* fall through */
	case ISCSI_OP_SCSI_CMD_RSP:
		if (tcp_conn->in.datalen) {
			iscsi_tcp_data_recv_prep(tcp_conn);
			return 0;
		}
		rc = iscsi_complete_pdu(conn, hdr, NULL, 0);
		break;
	case ISCSI_OP_R2T:
		ctask = session->cmds[itt];
		if (ahslen)
			rc = ISCSI_ERR_AHSLEN;
		else if (ctask->sc->sc_data_direction == DMA_TO_DEVICE) {
			spin_lock(&session->lock);
			rc = iscsi_r2t_rsp(conn, ctask);
			spin_unlock(&session->lock);
		} else
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
	if (conn->hdrdgst_en) {
		if (segment->digest_len == 0) {
			iscsi_tcp_segment_splice_digest(segment,
							segment->recv_digest);
			return 0;
		}
		iscsi_tcp_dgst_header(&tcp_conn->rx_hash, hdr,
				      segment->total_copied - ISCSI_DIGEST_SIZE,
				      segment->digest);

		if (!iscsi_tcp_dgst_verify(tcp_conn, segment))
			return ISCSI_ERR_HDR_DGST;
	}

	tcp_conn->in.hdr = hdr;
	return iscsi_tcp_hdr_dissect(conn, hdr);
}

/**
 * iscsi_tcp_recv - TCP receive in sendfile fashion
 * @rd_desc: read descriptor
 * @skb: socket buffer
 * @offset: offset in skb
 * @len: skb->len - offset
 **/
static int
iscsi_tcp_recv(read_descriptor_t *rd_desc, struct sk_buff *skb,
	       unsigned int offset, size_t len)
{
	struct iscsi_conn *conn = rd_desc->arg.data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_segment *segment = &tcp_conn->in.segment;
	struct skb_seq_state seq;
	unsigned int consumed = 0;
	int rc = 0;

	debug_tcp("in %d bytes\n", skb->len - offset);

	if (unlikely(conn->suspend_rx)) {
		debug_tcp("conn %d Rx suspended!\n", conn->id);
		return 0;
	}

	skb_prepare_seq_read(skb, offset, skb->len, &seq);
	while (1) {
		unsigned int avail;
		const u8 *ptr;

		avail = skb_seq_read(consumed, &ptr, &seq);
		if (avail == 0) {
			debug_tcp("no more data avail. Consumed %d\n",
				  consumed);
			break;
		}
		BUG_ON(segment->copied >= segment->size);

		debug_tcp("skb %p ptr=%p avail=%u\n", skb, ptr, avail);
		rc = iscsi_tcp_segment_recv(tcp_conn, segment, ptr, avail);
		BUG_ON(rc == 0);
		consumed += rc;

		if (segment->total_copied >= segment->total_size) {
			debug_tcp("segment done\n");
			rc = segment->done(tcp_conn, segment);
			if (rc != 0) {
				skb_abort_seq_read(&seq);
				goto error;
			}

			/* The done() functions sets up the
			 * next segment. */
		}
	}
	skb_abort_seq_read(&seq);
	conn->rxdata_octets += consumed;
	return consumed;

error:
	debug_tcp("Error receiving PDU, errno=%d\n", rc);
	iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	return 0;
}

static void
iscsi_tcp_data_ready(struct sock *sk, int flag)
{
	struct iscsi_conn *conn = sk->sk_user_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	read_descriptor_t rd_desc;

	read_lock(&sk->sk_callback_lock);

	/*
	 * Use rd_desc to pass 'conn' to iscsi_tcp_recv.
	 * We set count to 1 because we want the network layer to
	 * hand us all the skbs that are available. iscsi_tcp_recv
	 * handled pdus that cross buffers or pdus that still need data.
	 */
	rd_desc.arg.data = conn;
	rd_desc.count = 1;
	tcp_read_sock(sk, &rd_desc, iscsi_tcp_recv);

	read_unlock(&sk->sk_callback_lock);

	/* If we had to (atomically) map a highmem page,
	 * unmap it now. */
	iscsi_tcp_segment_unmap(&tcp_conn->in.segment);
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
 * iscsi_xmit - TCP transmit
 **/
static int
iscsi_xmit(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_segment *segment = &tcp_conn->out.segment;
	unsigned int consumed = 0;
	int rc = 0;

	while (1) {
		rc = iscsi_tcp_xmit_segment(tcp_conn, segment);
		if (rc < 0)
			goto error;
		if (rc == 0)
			break;

		consumed += rc;

		if (segment->total_copied >= segment->total_size) {
			if (segment->done != NULL) {
				rc = segment->done(tcp_conn, segment);
				if (rc < 0)
					goto error;
			}
		}
	}

	debug_tcp("xmit %d bytes\n", consumed);

	conn->txdata_octets += consumed;
	return consumed;

error:
	/* Transmit error. We could initiate error recovery
	 * here. */
	debug_tcp("Error sending PDU, errno=%d\n", rc);
	iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	return rc;
}

/**
 * iscsi_tcp_xmit_qlen - return the number of bytes queued for xmit
 */
static inline int
iscsi_tcp_xmit_qlen(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_segment *segment = &tcp_conn->out.segment;

	return segment->total_copied - segment->total_size;
}

static inline int
iscsi_tcp_flush(struct iscsi_conn *conn)
{
	int rc;

	while (iscsi_tcp_xmit_qlen(conn)) {
		rc = iscsi_xmit(conn);
		if (rc == 0)
			return -EAGAIN;
		if (rc < 0)
			return rc;
	}

	return 0;
}

/*
 * This is called when we're done sending the header.
 * Simply copy the data_segment to the send segment, and return.
 */
static int
iscsi_tcp_send_hdr_done(struct iscsi_tcp_conn *tcp_conn,
			struct iscsi_segment *segment)
{
	tcp_conn->out.segment = tcp_conn->out.data_segment;
	debug_tcp("Header done. Next segment size %u total_size %u\n",
		  tcp_conn->out.segment.size, tcp_conn->out.segment.total_size);
	return 0;
}

static void
iscsi_tcp_send_hdr_prep(struct iscsi_conn *conn, void *hdr, size_t hdrlen)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;

	debug_tcp("%s(%p%s)\n", __FUNCTION__, tcp_conn,
			conn->hdrdgst_en? ", digest enabled" : "");

	/* Clear the data segment - needs to be filled in by the
	 * caller using iscsi_tcp_send_data_prep() */
	memset(&tcp_conn->out.data_segment, 0, sizeof(struct iscsi_segment));

	/* If header digest is enabled, compute the CRC and
	 * place the digest into the same buffer. We make
	 * sure that both iscsi_tcp_ctask and mtask have
	 * sufficient room.
	 */
	if (conn->hdrdgst_en) {
		iscsi_tcp_dgst_header(&tcp_conn->tx_hash, hdr, hdrlen,
				      hdr + hdrlen);
		hdrlen += ISCSI_DIGEST_SIZE;
	}

	/* Remember header pointer for later, when we need
	 * to decide whether there's a payload to go along
	 * with the header. */
	tcp_conn->out.hdr = hdr;

	iscsi_segment_init_linear(&tcp_conn->out.segment, hdr, hdrlen,
				iscsi_tcp_send_hdr_done, NULL);
}

/*
 * Prepare the send buffer for the payload data.
 * Padding and checksumming will all be taken care
 * of by the iscsi_segment routines.
 */
static int
iscsi_tcp_send_data_prep(struct iscsi_conn *conn, struct scatterlist *sg,
			 unsigned int count, unsigned int offset,
			 unsigned int len)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct hash_desc *tx_hash = NULL;
	unsigned int hdr_spec_len;

	debug_tcp("%s(%p, offset=%d, datalen=%d%s)\n", __FUNCTION__,
			tcp_conn, offset, len,
			conn->datadgst_en? ", digest enabled" : "");

	/* Make sure the datalen matches what the caller
	   said he would send. */
	hdr_spec_len = ntoh24(tcp_conn->out.hdr->dlength);
	WARN_ON(iscsi_padded(len) != iscsi_padded(hdr_spec_len));

	if (conn->datadgst_en)
		tx_hash = &tcp_conn->tx_hash;

	return iscsi_segment_seek_sg(&tcp_conn->out.data_segment,
				   sg, count, offset, len,
				   NULL, tx_hash);
}

static void
iscsi_tcp_send_linear_data_prepare(struct iscsi_conn *conn, void *data,
				   size_t len)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct hash_desc *tx_hash = NULL;
	unsigned int hdr_spec_len;

	debug_tcp("%s(%p, datalen=%d%s)\n", __FUNCTION__, tcp_conn, len,
		  conn->datadgst_en? ", digest enabled" : "");

	/* Make sure the datalen matches what the caller
	   said he would send. */
	hdr_spec_len = ntoh24(tcp_conn->out.hdr->dlength);
	WARN_ON(iscsi_padded(len) != iscsi_padded(hdr_spec_len));

	if (conn->datadgst_en)
		tx_hash = &tcp_conn->tx_hash;

	iscsi_segment_init_linear(&tcp_conn->out.data_segment,
				data, len, NULL, tx_hash);
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
static int
iscsi_solicit_data_cont(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask,
			struct iscsi_r2t_info *r2t)
{
	struct iscsi_data *hdr;
	int new_offset, left;

	BUG_ON(r2t->data_length - r2t->sent < 0);
	left = r2t->data_length - r2t->sent;
	if (left == 0)
		return 0;

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
	return 1;
}

/**
 * iscsi_tcp_ctask - Initialize iSCSI SCSI_READ or SCSI_WRITE commands
 * @conn: iscsi connection
 * @ctask: scsi command task
 * @sc: scsi command
 **/
static int
iscsi_tcp_ctask_init(struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct iscsi_conn *conn = ctask->conn;
	struct scsi_cmnd *sc = ctask->sc;
	int err;

	BUG_ON(__kfifo_len(tcp_ctask->r2tqueue));
	tcp_ctask->sent = 0;
	tcp_ctask->exp_datasn = 0;

	/* Prepare PDU, optionally w/ immediate data */
	debug_scsi("ctask deq [cid %d itt 0x%x imm %d unsol %d]\n",
		    conn->id, ctask->itt, ctask->imm_count,
		    ctask->unsol_count);
	iscsi_tcp_send_hdr_prep(conn, ctask->hdr, ctask->hdr_len);

	if (!ctask->imm_count)
		return 0;

	/* If we have immediate data, attach a payload */
	err = iscsi_tcp_send_data_prep(conn, scsi_sglist(sc), scsi_sg_count(sc),
				       0, ctask->imm_count);
	if (err)
		return err;
	tcp_ctask->sent += ctask->imm_count;
	ctask->imm_count = 0;
	return 0;
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
 **/
static int
iscsi_tcp_mtask_xmit(struct iscsi_conn *conn, struct iscsi_mgmt_task *mtask)
{
	int rc;

	/* Flush any pending data first. */
	rc = iscsi_tcp_flush(conn);
	if (rc < 0)
		return rc;

	if (mtask->hdr->itt == RESERVED_ITT) {
		struct iscsi_session *session = conn->session;

		spin_lock_bh(&session->lock);
		iscsi_free_mgmt_task(conn, mtask);
		spin_unlock_bh(&session->lock);
	}

	return 0;
}

/*
 * iscsi_tcp_ctask_xmit - xmit normal PDU task
 * @conn: iscsi connection
 * @ctask: iscsi command task
 *
 * We're expected to return 0 when everything was transmitted succesfully,
 * -EAGAIN if there's still data in the queue, or != 0 for any other kind
 * of error.
 */
static int
iscsi_tcp_ctask_xmit(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;
	struct scsi_cmnd *sc = ctask->sc;
	int rc = 0;

flush:
	/* Flush any pending data first. */
	rc = iscsi_tcp_flush(conn);
	if (rc < 0)
		return rc;

	/* Are we done already? */
	if (sc->sc_data_direction != DMA_TO_DEVICE)
		return 0;

	if (ctask->unsol_count != 0) {
		struct iscsi_data *hdr = &tcp_ctask->unsol_dtask.hdr;

		/* Prepare a header for the unsolicited PDU.
		 * The amount of data we want to send will be
		 * in ctask->data_count.
		 * FIXME: return the data count instead.
		 */
		iscsi_prep_unsolicit_data_pdu(ctask, hdr);

		debug_tcp("unsol dout [itt 0x%x doff %d dlen %d]\n",
				ctask->itt, tcp_ctask->sent, ctask->data_count);

		iscsi_tcp_send_hdr_prep(conn, hdr, sizeof(*hdr));
		rc = iscsi_tcp_send_data_prep(conn, scsi_sglist(sc),
					      scsi_sg_count(sc),
					      tcp_ctask->sent,
					      ctask->data_count);
		if (rc)
			goto fail;
		tcp_ctask->sent += ctask->data_count;
		ctask->unsol_count -= ctask->data_count;
		goto flush;
	} else {
		struct iscsi_session *session = conn->session;
		struct iscsi_r2t_info *r2t;

		/* All unsolicited PDUs sent. Check for solicited PDUs.
		 */
		spin_lock_bh(&session->lock);
		r2t = tcp_ctask->r2t;
		if (r2t != NULL) {
			/* Continue with this R2T? */
			if (!iscsi_solicit_data_cont(conn, ctask, r2t)) {
				debug_scsi("  done with r2t %p\n", r2t);

				__kfifo_put(tcp_ctask->r2tpool.queue,
					    (void*)&r2t, sizeof(void*));
				tcp_ctask->r2t = r2t = NULL;
			}
		}

		if (r2t == NULL) {
			__kfifo_get(tcp_ctask->r2tqueue, (void*)&tcp_ctask->r2t,
				    sizeof(void*));
			r2t = tcp_ctask->r2t;
		}
		spin_unlock_bh(&session->lock);

		/* Waiting for more R2Ts to arrive. */
		if (r2t == NULL) {
			debug_tcp("no R2Ts yet\n");
			return 0;
		}

		debug_scsi("sol dout %p [dsn %d itt 0x%x doff %d dlen %d]\n",
			r2t, r2t->solicit_datasn - 1, ctask->itt,
			r2t->data_offset + r2t->sent, r2t->data_count);

		iscsi_tcp_send_hdr_prep(conn, &r2t->dtask.hdr,
					sizeof(struct iscsi_hdr));

		rc = iscsi_tcp_send_data_prep(conn, scsi_sglist(sc),
					      scsi_sg_count(sc),
					      r2t->data_offset + r2t->sent,
					      r2t->data_count);
		if (rc)
			goto fail;
		tcp_ctask->sent += r2t->data_count;
		r2t->sent += r2t->data_count;
		goto flush;
	}
	return 0;
fail:
	iscsi_conn_failure(conn, rc);
	return -EIO;
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

	tcp_conn->tx_hash.tfm = crypto_alloc_hash("crc32c", 0,
						  CRYPTO_ALG_ASYNC);
	tcp_conn->tx_hash.flags = 0;
	if (IS_ERR(tcp_conn->tx_hash.tfm))
		goto free_tcp_conn;

	tcp_conn->rx_hash.tfm = crypto_alloc_hash("crc32c", 0,
						  CRYPTO_ALG_ASYNC);
	tcp_conn->rx_hash.flags = 0;
	if (IS_ERR(tcp_conn->rx_hash.tfm))
		goto free_tx_tfm;

	return cls_conn;

free_tx_tfm:
	crypto_free_hash(tcp_conn->tx_hash.tfm);
free_tcp_conn:
	iscsi_conn_printk(KERN_ERR, conn,
			  "Could not create connection due to crc32c "
			  "loading error. Make sure the crc32c "
			  "module is built as a module or into the "
			  "kernel\n");
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

	iscsi_conn_stop(cls_conn, flag);
	iscsi_tcp_release_conn(conn);
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
		iscsi_conn_printk(KERN_ERR, conn,
				  "sockfd_lookup failed %d\n", err);
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
	iscsi_tcp_hdr_recv_prep(tcp_conn);
	return 0;

free_socket:
	sockfd_put(sock);
	return err;
}

/* called with host lock */
static void
iscsi_tcp_mtask_init(struct iscsi_conn *conn, struct iscsi_mgmt_task *mtask)
{
	debug_scsi("mtask deq [cid %d itt 0x%x]\n", conn->id, mtask->itt);

	/* Prepare PDU, optionally w/ immediate data */
	iscsi_tcp_send_hdr_prep(conn, mtask->hdr, sizeof(*mtask->hdr));

	/* If we have immediate data, attach a payload */
	if (mtask->data_count)
		iscsi_tcp_send_linear_data_prepare(conn, mtask->data,
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
		if (iscsi_pool_init(&tcp_ctask->r2tpool, session->max_r2t * 4, NULL,
				    sizeof(struct iscsi_r2t_info))) {
			goto r2t_alloc_fail;
		}

		/* R2T xmit queue */
		tcp_ctask->r2tqueue = kfifo_alloc(
		      session->max_r2t * 4 * sizeof(void*), GFP_KERNEL, NULL);
		if (tcp_ctask->r2tqueue == ERR_PTR(-ENOMEM)) {
			iscsi_pool_free(&tcp_ctask->r2tpool);
			goto r2t_alloc_fail;
		}
	}

	return 0;

r2t_alloc_fail:
	for (i = 0; i < cmd_i; i++) {
		struct iscsi_cmd_task *ctask = session->cmds[i];
		struct iscsi_tcp_cmd_task *tcp_ctask = ctask->dd_data;

		kfifo_free(tcp_ctask->r2tqueue);
		iscsi_pool_free(&tcp_ctask->r2tpool);
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
		iscsi_pool_free(&tcp_ctask->r2tpool);
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
		break;
	case ISCSI_PARAM_DATADGST_EN:
		iscsi_set_param(cls_conn, param, buf, buflen);
		tcp_conn->sendpage = conn->datadgst_en ?
			sock_no_sendpage : tcp_conn->sock->ops->sendpage;
		break;
	case ISCSI_PARAM_MAX_R2T:
		sscanf(buf, "%d", &value);
		if (value <= 0 || !is_power_of_2(value))
			return -EINVAL;
		if (session->max_r2t == value)
			break;
		iscsi_r2tpool_free(session);
		iscsi_set_param(cls_conn, param, buf, buflen);
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

		ctask->hdr = &tcp_ctask->hdr.cmd_hdr;
		ctask->hdr_max = sizeof(tcp_ctask->hdr) - ISCSI_DIGEST_SIZE;
	}

	for (cmd_i = 0; cmd_i < session->mgmtpool_max; cmd_i++) {
		struct iscsi_mgmt_task *mtask = session->mgmt_cmds[cmd_i];
		struct iscsi_tcp_mgmt_task *tcp_mtask = mtask->dd_data;

		mtask->hdr = (struct iscsi_hdr *) &tcp_mtask->hdr;
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
	.sg_tablesize		= 4096,
	.max_sectors		= 0xFFFF,
	.cmd_per_lun		= ISCSI_DEF_CMD_PER_LUN,
	.eh_abort_handler       = iscsi_eh_abort,
	.eh_device_reset_handler= iscsi_eh_device_reset,
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
				  ISCSI_USERNAME_IN | ISCSI_PASSWORD_IN |
				  ISCSI_FAST_ABORT | ISCSI_ABORT_TMO |
				  ISCSI_LU_RESET_TMO |
				  ISCSI_PING_TMO | ISCSI_RECV_TMO,
	.host_param_mask	= ISCSI_HOST_HWADDRESS | ISCSI_HOST_IPADDRESS |
				  ISCSI_HOST_INITIATOR_NAME |
				  ISCSI_HOST_NETDEV_NAME,
	.host_template		= &iscsi_sht,
	.conndata_size		= sizeof(struct iscsi_conn),
	.max_conn		= 1,
	.max_cmd_len		= 16,
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
	.init_cmd_task		= iscsi_tcp_ctask_init,
	.init_mgmt_task		= iscsi_tcp_mtask_init,
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
