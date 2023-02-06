// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iSCSI Initiator over TCP/IP Data-Path
 *
 * Copyright (C) 2004 Dmitry Yusupov
 * Copyright (C) 2004 Alex Aizman
 * Copyright (C) 2005 - 2006 Mike Christie
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * maintained by open-iscsi@googlegroups.com
 *
 * See the file COPYING included with this distribution for more details.
 *
 * Credits:
 *	Christoph Hellwig
 *	FUJITA Tomonori
 *	Arne Redlich
 *	Zhenyu Wang
 */

#include <crypto/hash.h>
#include <linux/types.h>
#include <linux/inet.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/file.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/scatterlist.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <net/tcp.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/scsi_transport_iscsi.h>
#include <trace/events/iscsi.h>

#include "iscsi_tcp.h"

MODULE_AUTHOR("Mike Christie <michaelc@cs.wisc.edu>, "
	      "Dmitry Yusupov <dmitry_yus@yahoo.com>, "
	      "Alex Aizman <itn780@yahoo.com>");
MODULE_DESCRIPTION("iSCSI/TCP data-path");
MODULE_LICENSE("GPL");

static struct scsi_transport_template *iscsi_sw_tcp_scsi_transport;
static struct scsi_host_template iscsi_sw_tcp_sht;
static struct iscsi_transport iscsi_sw_tcp_transport;

static unsigned int iscsi_max_lun = ~0;
module_param_named(max_lun, iscsi_max_lun, uint, S_IRUGO);

static bool iscsi_recv_from_iscsi_q;
module_param_named(recv_from_iscsi_q, iscsi_recv_from_iscsi_q, bool, 0644);
MODULE_PARM_DESC(recv_from_iscsi_q, "Set to true to read iSCSI data/headers from the iscsi_q workqueue. The default is false which will perform reads from the network softirq context.");

static int iscsi_sw_tcp_dbg;
module_param_named(debug_iscsi_tcp, iscsi_sw_tcp_dbg, int,
		   S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_iscsi_tcp, "Turn on debugging for iscsi_tcp module "
		 "Set to 1 to turn on, and zero to turn off. Default is off.");

#define ISCSI_SW_TCP_DBG(_conn, dbg_fmt, arg...)		\
	do {							\
		if (iscsi_sw_tcp_dbg)				\
			iscsi_conn_printk(KERN_INFO, _conn,	\
					     "%s " dbg_fmt,	\
					     __func__, ##arg);	\
		iscsi_dbg_trace(trace_iscsi_dbg_sw_tcp,		\
				&(_conn)->cls_conn->dev,	\
				"%s " dbg_fmt, __func__, ##arg);\
	} while (0);


/**
 * iscsi_sw_tcp_recv - TCP receive in sendfile fashion
 * @rd_desc: read descriptor
 * @skb: socket buffer
 * @offset: offset in skb
 * @len: skb->len - offset
 */
static int iscsi_sw_tcp_recv(read_descriptor_t *rd_desc, struct sk_buff *skb,
			     unsigned int offset, size_t len)
{
	struct iscsi_conn *conn = rd_desc->arg.data;
	unsigned int consumed, total_consumed = 0;
	int status;

	ISCSI_SW_TCP_DBG(conn, "in %d bytes\n", skb->len - offset);

	do {
		status = 0;
		consumed = iscsi_tcp_recv_skb(conn, skb, offset, 0, &status);
		offset += consumed;
		total_consumed += consumed;
	} while (consumed != 0 && status != ISCSI_TCP_SKB_DONE);

	ISCSI_SW_TCP_DBG(conn, "read %d bytes status %d\n",
			 skb->len - offset, status);
	return total_consumed;
}

/**
 * iscsi_sw_sk_state_check - check socket state
 * @sk: socket
 *
 * If the socket is in CLOSE or CLOSE_WAIT we should
 * not close the connection if there is still some
 * data pending.
 *
 * Must be called with sk_callback_lock.
 */
static inline int iscsi_sw_sk_state_check(struct sock *sk)
{
	struct iscsi_conn *conn = sk->sk_user_data;

	if ((sk->sk_state == TCP_CLOSE_WAIT || sk->sk_state == TCP_CLOSE) &&
	    (conn->session->state != ISCSI_STATE_LOGGING_OUT) &&
	    !atomic_read(&sk->sk_rmem_alloc)) {
		ISCSI_SW_TCP_DBG(conn, "TCP_CLOSE|TCP_CLOSE_WAIT\n");
		iscsi_conn_failure(conn, ISCSI_ERR_TCP_CONN_CLOSE);
		return -ECONNRESET;
	}
	return 0;
}

static void iscsi_sw_tcp_recv_data(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct sock *sk = tcp_sw_conn->sock->sk;
	read_descriptor_t rd_desc;

	/*
	 * Use rd_desc to pass 'conn' to iscsi_tcp_recv.
	 * We set count to 1 because we want the network layer to
	 * hand us all the skbs that are available. iscsi_tcp_recv
	 * handled pdus that cross buffers or pdus that still need data.
	 */
	rd_desc.arg.data = conn;
	rd_desc.count = 1;

	tcp_read_sock(sk, &rd_desc, iscsi_sw_tcp_recv);

	/* If we had to (atomically) map a highmem page,
	 * unmap it now. */
	iscsi_tcp_segment_unmap(&tcp_conn->in.segment);

	iscsi_sw_sk_state_check(sk);
}

static void iscsi_sw_tcp_recv_data_work(struct work_struct *work)
{
	struct iscsi_conn *conn = container_of(work, struct iscsi_conn,
					       recvwork);
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct sock *sk = tcp_sw_conn->sock->sk;

	lock_sock(sk);
	iscsi_sw_tcp_recv_data(conn);
	release_sock(sk);
}

static void iscsi_sw_tcp_data_ready(struct sock *sk)
{
	struct iscsi_sw_tcp_conn *tcp_sw_conn;
	struct iscsi_tcp_conn *tcp_conn;
	struct iscsi_conn *conn;

	read_lock_bh(&sk->sk_callback_lock);
	conn = sk->sk_user_data;
	if (!conn) {
		read_unlock_bh(&sk->sk_callback_lock);
		return;
	}
	tcp_conn = conn->dd_data;
	tcp_sw_conn = tcp_conn->dd_data;

	if (tcp_sw_conn->queue_recv)
		iscsi_conn_queue_recv(conn);
	else
		iscsi_sw_tcp_recv_data(conn);
	read_unlock_bh(&sk->sk_callback_lock);
}

static void iscsi_sw_tcp_state_change(struct sock *sk)
{
	struct iscsi_tcp_conn *tcp_conn;
	struct iscsi_sw_tcp_conn *tcp_sw_conn;
	struct iscsi_conn *conn;
	void (*old_state_change)(struct sock *);

	read_lock_bh(&sk->sk_callback_lock);
	conn = sk->sk_user_data;
	if (!conn) {
		read_unlock_bh(&sk->sk_callback_lock);
		return;
	}

	iscsi_sw_sk_state_check(sk);

	tcp_conn = conn->dd_data;
	tcp_sw_conn = tcp_conn->dd_data;
	old_state_change = tcp_sw_conn->old_state_change;

	read_unlock_bh(&sk->sk_callback_lock);

	old_state_change(sk);
}

/**
 * iscsi_sw_tcp_write_space - Called when more output buffer space is available
 * @sk: socket space is available for
 **/
static void iscsi_sw_tcp_write_space(struct sock *sk)
{
	struct iscsi_conn *conn;
	struct iscsi_tcp_conn *tcp_conn;
	struct iscsi_sw_tcp_conn *tcp_sw_conn;
	void (*old_write_space)(struct sock *);

	read_lock_bh(&sk->sk_callback_lock);
	conn = sk->sk_user_data;
	if (!conn) {
		read_unlock_bh(&sk->sk_callback_lock);
		return;
	}

	tcp_conn = conn->dd_data;
	tcp_sw_conn = tcp_conn->dd_data;
	old_write_space = tcp_sw_conn->old_write_space;
	read_unlock_bh(&sk->sk_callback_lock);

	old_write_space(sk);

	ISCSI_SW_TCP_DBG(conn, "iscsi_write_space\n");
	iscsi_conn_queue_xmit(conn);
}

static void iscsi_sw_tcp_conn_set_callbacks(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct sock *sk = tcp_sw_conn->sock->sk;

	/* assign new callbacks */
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_user_data = conn;
	tcp_sw_conn->old_data_ready = sk->sk_data_ready;
	tcp_sw_conn->old_state_change = sk->sk_state_change;
	tcp_sw_conn->old_write_space = sk->sk_write_space;
	sk->sk_data_ready = iscsi_sw_tcp_data_ready;
	sk->sk_state_change = iscsi_sw_tcp_state_change;
	sk->sk_write_space = iscsi_sw_tcp_write_space;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void
iscsi_sw_tcp_conn_restore_callbacks(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct sock *sk = tcp_sw_conn->sock->sk;

	/* restore socket callbacks, see also: iscsi_conn_set_callbacks() */
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_user_data    = NULL;
	sk->sk_data_ready   = tcp_sw_conn->old_data_ready;
	sk->sk_state_change = tcp_sw_conn->old_state_change;
	sk->sk_write_space  = tcp_sw_conn->old_write_space;
	sk->sk_no_check_tx = 0;
	write_unlock_bh(&sk->sk_callback_lock);
}

/**
 * iscsi_sw_tcp_xmit_segment - transmit segment
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
static int iscsi_sw_tcp_xmit_segment(struct iscsi_tcp_conn *tcp_conn,
				     struct iscsi_segment *segment)
{
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct socket *sk = tcp_sw_conn->sock;
	unsigned int copied = 0;
	int r = 0;

	while (!iscsi_tcp_segment_done(tcp_conn, segment, 0, r)) {
		struct scatterlist *sg;
		unsigned int offset, copy;
		int flags = 0;

		r = 0;
		offset = segment->copied;
		copy = segment->size - offset;

		if (segment->total_copied + segment->size < segment->total_size)
			flags |= MSG_MORE | MSG_SENDPAGE_NOTLAST;

		if (tcp_sw_conn->queue_recv)
			flags |= MSG_DONTWAIT;

		/* Use sendpage if we can; else fall back to sendmsg */
		if (!segment->data) {
			sg = segment->sg;
			offset += segment->sg_offset + sg->offset;
			r = tcp_sw_conn->sendpage(sk, sg_page(sg), offset,
						  copy, flags);
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
			return r;
		}
		copied += r;
	}
	return copied;
}

/**
 * iscsi_sw_tcp_xmit - TCP transmit
 * @conn: iscsi connection
 **/
static int iscsi_sw_tcp_xmit(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct iscsi_segment *segment = &tcp_sw_conn->out.segment;
	unsigned int consumed = 0;
	int rc = 0;

	while (1) {
		rc = iscsi_sw_tcp_xmit_segment(tcp_conn, segment);
		/*
		 * We may not have been able to send data because the conn
		 * is getting stopped. libiscsi will know so propagate err
		 * for it to do the right thing.
		 */
		if (rc == -EAGAIN)
			return rc;
		else if (rc < 0) {
			rc = ISCSI_ERR_XMIT_FAILED;
			goto error;
		} else if (rc == 0)
			break;

		consumed += rc;

		if (segment->total_copied >= segment->total_size) {
			if (segment->done != NULL) {
				rc = segment->done(tcp_conn, segment);
				if (rc != 0)
					goto error;
			}
		}
	}

	ISCSI_SW_TCP_DBG(conn, "xmit %d bytes\n", consumed);

	conn->txdata_octets += consumed;
	return consumed;

error:
	/* Transmit error. We could initiate error recovery
	 * here. */
	ISCSI_SW_TCP_DBG(conn, "Error sending PDU, errno=%d\n", rc);
	iscsi_conn_failure(conn, rc);
	return -EIO;
}

/**
 * iscsi_sw_tcp_xmit_qlen - return the number of bytes queued for xmit
 * @conn: iscsi connection
 */
static inline int iscsi_sw_tcp_xmit_qlen(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct iscsi_segment *segment = &tcp_sw_conn->out.segment;

	return segment->total_copied - segment->total_size;
}

static int iscsi_sw_tcp_pdu_xmit(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	unsigned int noreclaim_flag;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	int rc = 0;

	if (!tcp_sw_conn->sock) {
		iscsi_conn_printk(KERN_ERR, conn,
				  "Transport not bound to socket!\n");
		return -EINVAL;
	}

	noreclaim_flag = memalloc_noreclaim_save();

	while (iscsi_sw_tcp_xmit_qlen(conn)) {
		rc = iscsi_sw_tcp_xmit(conn);
		if (rc == 0) {
			rc = -EAGAIN;
			break;
		}
		if (rc < 0)
			break;
		rc = 0;
	}

	memalloc_noreclaim_restore(noreclaim_flag);
	return rc;
}

/*
 * This is called when we're done sending the header.
 * Simply copy the data_segment to the send segment, and return.
 */
static int iscsi_sw_tcp_send_hdr_done(struct iscsi_tcp_conn *tcp_conn,
				      struct iscsi_segment *segment)
{
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;

	tcp_sw_conn->out.segment = tcp_sw_conn->out.data_segment;
	ISCSI_SW_TCP_DBG(tcp_conn->iscsi_conn,
			 "Header done. Next segment size %u total_size %u\n",
			 tcp_sw_conn->out.segment.size,
			 tcp_sw_conn->out.segment.total_size);
	return 0;
}

static void iscsi_sw_tcp_send_hdr_prep(struct iscsi_conn *conn, void *hdr,
				       size_t hdrlen)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;

	ISCSI_SW_TCP_DBG(conn, "%s\n", conn->hdrdgst_en ?
			 "digest enabled" : "digest disabled");

	/* Clear the data segment - needs to be filled in by the
	 * caller using iscsi_tcp_send_data_prep() */
	memset(&tcp_sw_conn->out.data_segment, 0,
	       sizeof(struct iscsi_segment));

	/* If header digest is enabled, compute the CRC and
	 * place the digest into the same buffer. We make
	 * sure that both iscsi_tcp_task and mtask have
	 * sufficient room.
	 */
	if (conn->hdrdgst_en) {
		iscsi_tcp_dgst_header(tcp_sw_conn->tx_hash, hdr, hdrlen,
				      hdr + hdrlen);
		hdrlen += ISCSI_DIGEST_SIZE;
	}

	/* Remember header pointer for later, when we need
	 * to decide whether there's a payload to go along
	 * with the header. */
	tcp_sw_conn->out.hdr = hdr;

	iscsi_segment_init_linear(&tcp_sw_conn->out.segment, hdr, hdrlen,
				  iscsi_sw_tcp_send_hdr_done, NULL);
}

/*
 * Prepare the send buffer for the payload data.
 * Padding and checksumming will all be taken care
 * of by the iscsi_segment routines.
 */
static int
iscsi_sw_tcp_send_data_prep(struct iscsi_conn *conn, struct scatterlist *sg,
			    unsigned int count, unsigned int offset,
			    unsigned int len)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct ahash_request *tx_hash = NULL;
	unsigned int hdr_spec_len;

	ISCSI_SW_TCP_DBG(conn, "offset=%d, datalen=%d %s\n", offset, len,
			 conn->datadgst_en ?
			 "digest enabled" : "digest disabled");

	/* Make sure the datalen matches what the caller
	   said he would send. */
	hdr_spec_len = ntoh24(tcp_sw_conn->out.hdr->dlength);
	WARN_ON(iscsi_padded(len) != iscsi_padded(hdr_spec_len));

	if (conn->datadgst_en)
		tx_hash = tcp_sw_conn->tx_hash;

	return iscsi_segment_seek_sg(&tcp_sw_conn->out.data_segment,
				     sg, count, offset, len,
				     NULL, tx_hash);
}

static void
iscsi_sw_tcp_send_linear_data_prep(struct iscsi_conn *conn, void *data,
				   size_t len)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct ahash_request *tx_hash = NULL;
	unsigned int hdr_spec_len;

	ISCSI_SW_TCP_DBG(conn, "datalen=%zd %s\n", len, conn->datadgst_en ?
			 "digest enabled" : "digest disabled");

	/* Make sure the datalen matches what the caller
	   said he would send. */
	hdr_spec_len = ntoh24(tcp_sw_conn->out.hdr->dlength);
	WARN_ON(iscsi_padded(len) != iscsi_padded(hdr_spec_len));

	if (conn->datadgst_en)
		tx_hash = tcp_sw_conn->tx_hash;

	iscsi_segment_init_linear(&tcp_sw_conn->out.data_segment,
				data, len, NULL, tx_hash);
}

static int iscsi_sw_tcp_pdu_init(struct iscsi_task *task,
				 unsigned int offset, unsigned int count)
{
	struct iscsi_conn *conn = task->conn;
	int err = 0;

	iscsi_sw_tcp_send_hdr_prep(conn, task->hdr, task->hdr_len);

	if (!count)
		return 0;

	if (!task->sc)
		iscsi_sw_tcp_send_linear_data_prep(conn, task->data, count);
	else {
		struct scsi_data_buffer *sdb = &task->sc->sdb;

		err = iscsi_sw_tcp_send_data_prep(conn, sdb->table.sgl,
						  sdb->table.nents, offset,
						  count);
	}

	if (err) {
		/* got invalid offset/len */
		return -EIO;
	}
	return 0;
}

static int iscsi_sw_tcp_pdu_alloc(struct iscsi_task *task, uint8_t opcode)
{
	struct iscsi_tcp_task *tcp_task = task->dd_data;

	task->hdr = task->dd_data + sizeof(*tcp_task);
	task->hdr_max = sizeof(struct iscsi_sw_tcp_hdrbuf) - ISCSI_DIGEST_SIZE;
	return 0;
}

static struct iscsi_cls_conn *
iscsi_sw_tcp_conn_create(struct iscsi_cls_session *cls_session,
			 uint32_t conn_idx)
{
	struct iscsi_conn *conn;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_tcp_conn *tcp_conn;
	struct iscsi_sw_tcp_conn *tcp_sw_conn;
	struct crypto_ahash *tfm;

	cls_conn = iscsi_tcp_conn_setup(cls_session, sizeof(*tcp_sw_conn),
					conn_idx);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;
	tcp_conn = conn->dd_data;
	tcp_sw_conn = tcp_conn->dd_data;
	INIT_WORK(&conn->recvwork, iscsi_sw_tcp_recv_data_work);
	tcp_sw_conn->queue_recv = iscsi_recv_from_iscsi_q;

	mutex_init(&tcp_sw_conn->sock_lock);

	tfm = crypto_alloc_ahash("crc32c", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm))
		goto free_conn;

	tcp_sw_conn->tx_hash = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!tcp_sw_conn->tx_hash)
		goto free_tfm;
	ahash_request_set_callback(tcp_sw_conn->tx_hash, 0, NULL, NULL);

	tcp_sw_conn->rx_hash = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!tcp_sw_conn->rx_hash)
		goto free_tx_hash;
	ahash_request_set_callback(tcp_sw_conn->rx_hash, 0, NULL, NULL);

	tcp_conn->rx_hash = tcp_sw_conn->rx_hash;

	return cls_conn;

free_tx_hash:
	ahash_request_free(tcp_sw_conn->tx_hash);
free_tfm:
	crypto_free_ahash(tfm);
free_conn:
	iscsi_conn_printk(KERN_ERR, conn,
			  "Could not create connection due to crc32c "
			  "loading error. Make sure the crc32c "
			  "module is built as a module or into the "
			  "kernel\n");
	iscsi_tcp_conn_teardown(cls_conn);
	return NULL;
}

static void iscsi_sw_tcp_release_conn(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct socket *sock = tcp_sw_conn->sock;

	/*
	 * The iscsi transport class will make sure we are not called in
	 * parallel with start, stop, bind and destroys. However, this can be
	 * called twice if userspace does a stop then a destroy.
	 */
	if (!sock)
		return;

	/*
	 * Make sure we start socket shutdown now in case userspace is up
	 * but delayed in releasing the socket.
	 */
	kernel_sock_shutdown(sock, SHUT_RDWR);

	sock_hold(sock->sk);
	iscsi_sw_tcp_conn_restore_callbacks(conn);
	sock_put(sock->sk);

	iscsi_suspend_rx(conn);

	mutex_lock(&tcp_sw_conn->sock_lock);
	tcp_sw_conn->sock = NULL;
	mutex_unlock(&tcp_sw_conn->sock_lock);
	sockfd_put(sock);
}

static void iscsi_sw_tcp_conn_destroy(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;

	iscsi_sw_tcp_release_conn(conn);

	ahash_request_free(tcp_sw_conn->rx_hash);
	if (tcp_sw_conn->tx_hash) {
		struct crypto_ahash *tfm;

		tfm = crypto_ahash_reqtfm(tcp_sw_conn->tx_hash);
		ahash_request_free(tcp_sw_conn->tx_hash);
		crypto_free_ahash(tfm);
	}

	iscsi_tcp_conn_teardown(cls_conn);
}

static void iscsi_sw_tcp_conn_stop(struct iscsi_cls_conn *cls_conn, int flag)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
	struct socket *sock = tcp_sw_conn->sock;

	/* userspace may have goofed up and not bound us */
	if (!sock)
		return;

	sock->sk->sk_err = EIO;
	wake_up_interruptible(sk_sleep(sock->sk));

	/* stop xmit side */
	iscsi_suspend_tx(conn);

	/* stop recv side and release socket */
	iscsi_sw_tcp_release_conn(conn);

	iscsi_conn_stop(cls_conn, flag);
}

static int
iscsi_sw_tcp_conn_bind(struct iscsi_cls_session *cls_session,
		       struct iscsi_cls_conn *cls_conn, uint64_t transport_eph,
		       int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;
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

	err = iscsi_conn_bind(cls_session, cls_conn, is_leading);
	if (err)
		goto free_socket;

	mutex_lock(&tcp_sw_conn->sock_lock);
	/* bind iSCSI connection and socket */
	tcp_sw_conn->sock = sock;
	mutex_unlock(&tcp_sw_conn->sock_lock);

	/* setup Socket parameters */
	sk = sock->sk;
	sk->sk_reuse = SK_CAN_REUSE;
	sk->sk_sndtimeo = 15 * HZ; /* FIXME: make it configurable */
	sk->sk_allocation = GFP_ATOMIC;
	sk->sk_use_task_frag = false;
	sk_set_memalloc(sk);
	sock_no_linger(sk);

	iscsi_sw_tcp_conn_set_callbacks(conn);
	tcp_sw_conn->sendpage = tcp_sw_conn->sock->ops->sendpage;
	/*
	 * set receive state machine into initial state
	 */
	iscsi_tcp_hdr_recv_prep(tcp_conn);
	return 0;

free_socket:
	sockfd_put(sock);
	return err;
}

static int iscsi_sw_tcp_conn_set_param(struct iscsi_cls_conn *cls_conn,
				       enum iscsi_param param, char *buf,
				       int buflen)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;

	switch(param) {
	case ISCSI_PARAM_HDRDGST_EN:
		iscsi_set_param(cls_conn, param, buf, buflen);
		break;
	case ISCSI_PARAM_DATADGST_EN:
		iscsi_set_param(cls_conn, param, buf, buflen);

		mutex_lock(&tcp_sw_conn->sock_lock);
		if (!tcp_sw_conn->sock) {
			mutex_unlock(&tcp_sw_conn->sock_lock);
			return -ENOTCONN;
		}
		tcp_sw_conn->sendpage = conn->datadgst_en ?
			sock_no_sendpage : tcp_sw_conn->sock->ops->sendpage;
		mutex_unlock(&tcp_sw_conn->sock_lock);
		break;
	case ISCSI_PARAM_MAX_R2T:
		return iscsi_tcp_set_max_r2t(conn, buf);
	default:
		return iscsi_set_param(cls_conn, param, buf, buflen);
	}

	return 0;
}

static int iscsi_sw_tcp_conn_get_param(struct iscsi_cls_conn *cls_conn,
				       enum iscsi_param param, char *buf)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn;
	struct iscsi_tcp_conn *tcp_conn;
	struct sockaddr_in6 addr;
	struct socket *sock;
	int rc;

	switch(param) {
	case ISCSI_PARAM_CONN_PORT:
	case ISCSI_PARAM_CONN_ADDRESS:
	case ISCSI_PARAM_LOCAL_PORT:
		spin_lock_bh(&conn->session->frwd_lock);
		if (!conn->session->leadconn) {
			spin_unlock_bh(&conn->session->frwd_lock);
			return -ENOTCONN;
		}
		/*
		 * The conn has been setup and bound, so just grab a ref
		 * incase a destroy runs while we are in the net layer.
		 */
		iscsi_get_conn(conn->cls_conn);
		spin_unlock_bh(&conn->session->frwd_lock);

		tcp_conn = conn->dd_data;
		tcp_sw_conn = tcp_conn->dd_data;

		mutex_lock(&tcp_sw_conn->sock_lock);
		sock = tcp_sw_conn->sock;
		if (!sock) {
			rc = -ENOTCONN;
			goto sock_unlock;
		}

		if (param == ISCSI_PARAM_LOCAL_PORT)
			rc = kernel_getsockname(sock,
						(struct sockaddr *)&addr);
		else
			rc = kernel_getpeername(sock,
						(struct sockaddr *)&addr);
sock_unlock:
		mutex_unlock(&tcp_sw_conn->sock_lock);
		iscsi_put_conn(conn->cls_conn);
		if (rc < 0)
			return rc;

		return iscsi_conn_get_addr_param((struct sockaddr_storage *)
						 &addr, param, buf);
	default:
		return iscsi_conn_get_param(cls_conn, param, buf);
	}

	return 0;
}

static int iscsi_sw_tcp_host_get_param(struct Scsi_Host *shost,
				       enum iscsi_host_param param, char *buf)
{
	struct iscsi_sw_tcp_host *tcp_sw_host = iscsi_host_priv(shost);
	struct iscsi_session *session;
	struct iscsi_conn *conn;
	struct iscsi_tcp_conn *tcp_conn;
	struct iscsi_sw_tcp_conn *tcp_sw_conn;
	struct sockaddr_in6 addr;
	struct socket *sock;
	int rc;

	switch (param) {
	case ISCSI_HOST_PARAM_IPADDRESS:
		session = tcp_sw_host->session;
		if (!session)
			return -ENOTCONN;

		spin_lock_bh(&session->frwd_lock);
		conn = session->leadconn;
		if (!conn) {
			spin_unlock_bh(&session->frwd_lock);
			return -ENOTCONN;
		}
		tcp_conn = conn->dd_data;
		tcp_sw_conn = tcp_conn->dd_data;
		/*
		 * The conn has been setup and bound, so just grab a ref
		 * incase a destroy runs while we are in the net layer.
		 */
		iscsi_get_conn(conn->cls_conn);
		spin_unlock_bh(&session->frwd_lock);

		mutex_lock(&tcp_sw_conn->sock_lock);
		sock = tcp_sw_conn->sock;
		if (!sock)
			rc = -ENOTCONN;
		else
			rc = kernel_getsockname(sock, (struct sockaddr *)&addr);
		mutex_unlock(&tcp_sw_conn->sock_lock);
		iscsi_put_conn(conn->cls_conn);
		if (rc < 0)
			return rc;

		return iscsi_conn_get_addr_param((struct sockaddr_storage *)
						 &addr,
						 (enum iscsi_param)param, buf);
	default:
		return iscsi_host_get_param(shost, param, buf);
	}

	return 0;
}

static void
iscsi_sw_tcp_conn_get_stats(struct iscsi_cls_conn *cls_conn,
			    struct iscsi_stats *stats)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_sw_tcp_conn *tcp_sw_conn = tcp_conn->dd_data;

	stats->custom_length = 3;
	strcpy(stats->custom[0].desc, "tx_sendpage_failures");
	stats->custom[0].value = tcp_sw_conn->sendpage_failures_cnt;
	strcpy(stats->custom[1].desc, "rx_discontiguous_hdr");
	stats->custom[1].value = tcp_sw_conn->discontiguous_hdr_cnt;
	strcpy(stats->custom[2].desc, "eh_abort_cnt");
	stats->custom[2].value = conn->eh_abort_cnt;

	iscsi_tcp_conn_get_stats(cls_conn, stats);
}

static struct iscsi_cls_session *
iscsi_sw_tcp_session_create(struct iscsi_endpoint *ep, uint16_t cmds_max,
			    uint16_t qdepth, uint32_t initial_cmdsn)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	struct iscsi_sw_tcp_host *tcp_sw_host;
	struct Scsi_Host *shost;
	int rc;

	if (ep) {
		printk(KERN_ERR "iscsi_tcp: invalid ep %p.\n", ep);
		return NULL;
	}

	shost = iscsi_host_alloc(&iscsi_sw_tcp_sht,
				 sizeof(struct iscsi_sw_tcp_host), 1);
	if (!shost)
		return NULL;
	shost->transportt = iscsi_sw_tcp_scsi_transport;
	shost->cmd_per_lun = qdepth;
	shost->max_lun = iscsi_max_lun;
	shost->max_id = 0;
	shost->max_channel = 0;
	shost->max_cmd_len = SCSI_MAX_VARLEN_CDB_SIZE;

	rc = iscsi_host_get_max_scsi_cmds(shost, cmds_max);
	if (rc < 0)
		goto free_host;
	shost->can_queue = rc;

	if (iscsi_host_add(shost, NULL))
		goto free_host;

	cls_session = iscsi_session_setup(&iscsi_sw_tcp_transport, shost,
					  cmds_max, 0,
					  sizeof(struct iscsi_tcp_task) +
					  sizeof(struct iscsi_sw_tcp_hdrbuf),
					  initial_cmdsn, 0);
	if (!cls_session)
		goto remove_host;
	session = cls_session->dd_data;

	if (iscsi_tcp_r2tpool_alloc(session))
		goto remove_session;

	/* We are now fully setup so expose the session to sysfs. */
	tcp_sw_host = iscsi_host_priv(shost);
	tcp_sw_host->session = session;
	return cls_session;

remove_session:
	iscsi_session_teardown(cls_session);
remove_host:
	iscsi_host_remove(shost, false);
free_host:
	iscsi_host_free(shost);
	return NULL;
}

static void iscsi_sw_tcp_session_destroy(struct iscsi_cls_session *cls_session)
{
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);
	struct iscsi_session *session = cls_session->dd_data;

	if (WARN_ON_ONCE(session->leadconn))
		return;

	iscsi_session_remove(cls_session);
	/*
	 * Our get_host_param needs to access the session, so remove the
	 * host from sysfs before freeing the session to make sure userspace
	 * is no longer accessing the callout.
	 */
	iscsi_host_remove(shost, false);

	iscsi_tcp_r2tpool_free(cls_session->dd_data);

	iscsi_session_free(cls_session);
	iscsi_host_free(shost);
}

static umode_t iscsi_sw_tcp_attr_is_visible(int param_type, int param)
{
	switch (param_type) {
	case ISCSI_HOST_PARAM:
		switch (param) {
		case ISCSI_HOST_PARAM_NETDEV_NAME:
		case ISCSI_HOST_PARAM_HWADDRESS:
		case ISCSI_HOST_PARAM_IPADDRESS:
		case ISCSI_HOST_PARAM_INITIATOR_NAME:
			return S_IRUGO;
		default:
			return 0;
		}
	case ISCSI_PARAM:
		switch (param) {
		case ISCSI_PARAM_MAX_RECV_DLENGTH:
		case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		case ISCSI_PARAM_HDRDGST_EN:
		case ISCSI_PARAM_DATADGST_EN:
		case ISCSI_PARAM_CONN_ADDRESS:
		case ISCSI_PARAM_CONN_PORT:
		case ISCSI_PARAM_LOCAL_PORT:
		case ISCSI_PARAM_EXP_STATSN:
		case ISCSI_PARAM_PERSISTENT_ADDRESS:
		case ISCSI_PARAM_PERSISTENT_PORT:
		case ISCSI_PARAM_PING_TMO:
		case ISCSI_PARAM_RECV_TMO:
		case ISCSI_PARAM_INITIAL_R2T_EN:
		case ISCSI_PARAM_MAX_R2T:
		case ISCSI_PARAM_IMM_DATA_EN:
		case ISCSI_PARAM_FIRST_BURST:
		case ISCSI_PARAM_MAX_BURST:
		case ISCSI_PARAM_PDU_INORDER_EN:
		case ISCSI_PARAM_DATASEQ_INORDER_EN:
		case ISCSI_PARAM_ERL:
		case ISCSI_PARAM_TARGET_NAME:
		case ISCSI_PARAM_TPGT:
		case ISCSI_PARAM_USERNAME:
		case ISCSI_PARAM_PASSWORD:
		case ISCSI_PARAM_USERNAME_IN:
		case ISCSI_PARAM_PASSWORD_IN:
		case ISCSI_PARAM_FAST_ABORT:
		case ISCSI_PARAM_ABORT_TMO:
		case ISCSI_PARAM_LU_RESET_TMO:
		case ISCSI_PARAM_TGT_RESET_TMO:
		case ISCSI_PARAM_IFACE_NAME:
		case ISCSI_PARAM_INITIATOR_NAME:
			return S_IRUGO;
		default:
			return 0;
		}
	}

	return 0;
}

static int iscsi_sw_tcp_slave_configure(struct scsi_device *sdev)
{
	struct iscsi_sw_tcp_host *tcp_sw_host = iscsi_host_priv(sdev->host);
	struct iscsi_session *session = tcp_sw_host->session;
	struct iscsi_conn *conn = session->leadconn;

	if (conn->datadgst_en)
		blk_queue_flag_set(QUEUE_FLAG_STABLE_WRITES,
				   sdev->request_queue);
	blk_queue_dma_alignment(sdev->request_queue, 0);
	return 0;
}

static struct scsi_host_template iscsi_sw_tcp_sht = {
	.module			= THIS_MODULE,
	.name			= "iSCSI Initiator over TCP/IP",
	.queuecommand           = iscsi_queuecommand,
	.change_queue_depth	= scsi_change_queue_depth,
	.can_queue		= ISCSI_TOTAL_CMDS_MAX,
	.sg_tablesize		= 4096,
	.max_sectors		= 0xFFFF,
	.cmd_per_lun		= ISCSI_DEF_CMD_PER_LUN,
	.eh_timed_out		= iscsi_eh_cmd_timed_out,
	.eh_abort_handler       = iscsi_eh_abort,
	.eh_device_reset_handler= iscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_recover_target,
	.dma_boundary		= PAGE_SIZE - 1,
	.slave_configure        = iscsi_sw_tcp_slave_configure,
	.proc_name		= "iscsi_tcp",
	.this_id		= -1,
	.track_queue_depth	= 1,
	.cmd_size		= sizeof(struct iscsi_cmd),
};

static struct iscsi_transport iscsi_sw_tcp_transport = {
	.owner			= THIS_MODULE,
	.name			= "tcp",
	.caps			= CAP_RECOVERY_L0 | CAP_MULTI_R2T | CAP_HDRDGST
				  | CAP_DATADGST,
	/* session management */
	.create_session		= iscsi_sw_tcp_session_create,
	.destroy_session	= iscsi_sw_tcp_session_destroy,
	/* connection management */
	.create_conn		= iscsi_sw_tcp_conn_create,
	.bind_conn		= iscsi_sw_tcp_conn_bind,
	.destroy_conn		= iscsi_sw_tcp_conn_destroy,
	.attr_is_visible	= iscsi_sw_tcp_attr_is_visible,
	.set_param		= iscsi_sw_tcp_conn_set_param,
	.get_conn_param		= iscsi_sw_tcp_conn_get_param,
	.get_session_param	= iscsi_session_get_param,
	.start_conn		= iscsi_conn_start,
	.stop_conn		= iscsi_sw_tcp_conn_stop,
	/* iscsi host params */
	.get_host_param		= iscsi_sw_tcp_host_get_param,
	.set_host_param		= iscsi_host_set_param,
	/* IO */
	.send_pdu		= iscsi_conn_send_pdu,
	.get_stats		= iscsi_sw_tcp_conn_get_stats,
	/* iscsi task/cmd helpers */
	.init_task		= iscsi_tcp_task_init,
	.xmit_task		= iscsi_tcp_task_xmit,
	.cleanup_task		= iscsi_tcp_cleanup_task,
	/* low level pdu helpers */
	.xmit_pdu		= iscsi_sw_tcp_pdu_xmit,
	.init_pdu		= iscsi_sw_tcp_pdu_init,
	.alloc_pdu		= iscsi_sw_tcp_pdu_alloc,
	/* recovery */
	.session_recovery_timedout = iscsi_session_recovery_timedout,
};

static int __init iscsi_sw_tcp_init(void)
{
	if (iscsi_max_lun < 1) {
		printk(KERN_ERR "iscsi_tcp: Invalid max_lun value of %u\n",
		       iscsi_max_lun);
		return -EINVAL;
	}

	iscsi_sw_tcp_scsi_transport = iscsi_register_transport(
						&iscsi_sw_tcp_transport);
	if (!iscsi_sw_tcp_scsi_transport)
		return -ENODEV;

	return 0;
}

static void __exit iscsi_sw_tcp_exit(void)
{
	iscsi_unregister_transport(&iscsi_sw_tcp_transport);
}

module_init(iscsi_sw_tcp_init);
module_exit(iscsi_sw_tcp_exit);
