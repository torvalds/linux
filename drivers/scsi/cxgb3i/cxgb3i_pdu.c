/*
 * cxgb3i_pdu.c: Chelsio S3xx iSCSI driver.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 * Copyright (c) 2008 Mike Christie
 * Copyright (c) 2008 Red Hat, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#include <linux/skbuff.h>
#include <linux/crypto.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>

#include "cxgb3i.h"
#include "cxgb3i_pdu.h"

#ifdef __DEBUG_CXGB3I_RX__
#define cxgb3i_rx_debug		cxgb3i_log_debug
#else
#define cxgb3i_rx_debug(fmt...)
#endif

#ifdef __DEBUG_CXGB3I_TX__
#define cxgb3i_tx_debug		cxgb3i_log_debug
#else
#define cxgb3i_tx_debug(fmt...)
#endif

static struct page *pad_page;

/*
 * pdu receive, interact with libiscsi_tcp
 */
static inline int read_pdu_skb(struct iscsi_conn *conn, struct sk_buff *skb,
			       unsigned int offset, int offloaded)
{
	int status = 0;
	int bytes_read;

	bytes_read = iscsi_tcp_recv_skb(conn, skb, offset, offloaded, &status);
	switch (status) {
	case ISCSI_TCP_CONN_ERR:
		return -EIO;
	case ISCSI_TCP_SUSPENDED:
		/* no transfer - just have caller flush queue */
		return bytes_read;
	case ISCSI_TCP_SKB_DONE:
		/*
		 * pdus should always fit in the skb and we should get
		 * segment done notifcation.
		 */
		iscsi_conn_printk(KERN_ERR, conn, "Invalid pdu or skb.");
		return -EFAULT;
	case ISCSI_TCP_SEGMENT_DONE:
		return bytes_read;
	default:
		iscsi_conn_printk(KERN_ERR, conn, "Invalid iscsi_tcp_recv_skb "
				  "status %d\n", status);
		return -EINVAL;
	}
}

static int cxgb3i_conn_read_pdu_skb(struct iscsi_conn *conn,
				    struct sk_buff *skb)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	bool offloaded = 0;
	unsigned int offset;
	int rc;

	cxgb3i_rx_debug("conn 0x%p, skb 0x%p, len %u, flag 0x%x.\n",
			conn, skb, skb->len, skb_ulp_mode(skb));

	if (!iscsi_tcp_recv_segment_is_hdr(tcp_conn)) {
		iscsi_conn_failure(conn, ISCSI_ERR_PROTO);
		return -EIO;
	}

	if (conn->hdrdgst_en && (skb_ulp_mode(skb) & ULP2_FLAG_HCRC_ERROR)) {
		iscsi_conn_failure(conn, ISCSI_ERR_HDR_DGST);
		return -EIO;
	}

	if (conn->datadgst_en && (skb_ulp_mode(skb) & ULP2_FLAG_DCRC_ERROR)) {
		iscsi_conn_failure(conn, ISCSI_ERR_DATA_DGST);
		return -EIO;
	}

	/* iscsi hdr */
	rc = read_pdu_skb(conn, skb, 0, 0);
	if (rc <= 0)
		return rc;

	if (iscsi_tcp_recv_segment_is_hdr(tcp_conn))
		return 0;

	offset = rc;
	if (conn->hdrdgst_en)
		offset += ISCSI_DIGEST_SIZE;

	/* iscsi data */
	if (skb_ulp_mode(skb) & ULP2_FLAG_DATA_DDPED) {
		cxgb3i_rx_debug("skb 0x%p, opcode 0x%x, data %u, ddp'ed, "
				"itt 0x%x.\n",
				skb,
				tcp_conn->in.hdr->opcode & ISCSI_OPCODE_MASK,
				tcp_conn->in.datalen,
				ntohl(tcp_conn->in.hdr->itt));
		offloaded = 1;
	} else {
		cxgb3i_rx_debug("skb 0x%p, opcode 0x%x, data %u, NOT ddp'ed, "
				"itt 0x%x.\n",
				skb,
				tcp_conn->in.hdr->opcode & ISCSI_OPCODE_MASK,
				tcp_conn->in.datalen,
				ntohl(tcp_conn->in.hdr->itt));
		offset += sizeof(struct cpl_iscsi_hdr_norss);
	}

	rc = read_pdu_skb(conn, skb, offset, offloaded);
	if (rc < 0)
		return rc;
	else
		return 0;
}

/*
 * pdu transmit, interact with libiscsi_tcp
 */
static inline void tx_skb_setmode(struct sk_buff *skb, int hcrc, int dcrc)
{
	u8 submode = 0;

	if (hcrc)
		submode |= 1;
	if (dcrc)
		submode |= 2;
	skb_ulp_mode(skb) = (ULP_MODE_ISCSI << 4) | submode;
}

void cxgb3i_conn_cleanup_task(struct iscsi_task *task)
{
	struct iscsi_tcp_task *tcp_task = task->dd_data;

	/* never reached the xmit task callout */
	if (tcp_task->dd_data)
		kfree_skb(tcp_task->dd_data);
	tcp_task->dd_data = NULL;

	/* MNC - Do we need a check in case this is called but
	 * cxgb3i_conn_alloc_pdu has never been called on the task */
	cxgb3i_release_itt(task, task->hdr_itt);
	iscsi_tcp_cleanup_task(task);
}

/*
 * We do not support ahs yet
 */
int cxgb3i_conn_alloc_pdu(struct iscsi_task *task, u8 opcode)
{
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct sk_buff *skb;

	task->hdr = NULL;
	/* always allocate rooms for AHS */
	skb = alloc_skb(sizeof(struct iscsi_hdr) + ISCSI_MAX_AHS_SIZE +
			TX_HEADER_LEN,  GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	cxgb3i_tx_debug("task 0x%p, opcode 0x%x, skb 0x%p.\n",
			task, opcode, skb);

	tcp_task->dd_data = skb;
	skb_reserve(skb, TX_HEADER_LEN);
	task->hdr = (struct iscsi_hdr *)skb->data;
	task->hdr_max = sizeof(struct iscsi_hdr);

	/* data_out uses scsi_cmd's itt */
	if (opcode != ISCSI_OP_SCSI_DATA_OUT)
		cxgb3i_reserve_itt(task, &task->hdr->itt);

	return 0;
}

int cxgb3i_conn_init_pdu(struct iscsi_task *task, unsigned int offset,
			      unsigned int count)
{
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct sk_buff *skb = tcp_task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct page *pg;
	unsigned int datalen = count;
	int i, padlen = iscsi_padding(count);
	skb_frag_t *frag;

	cxgb3i_tx_debug("task 0x%p,0x%p, offset %u, count %u, skb 0x%p.\n",
			task, task->sc, offset, count, skb);

	skb_put(skb, task->hdr_len);
	tx_skb_setmode(skb, conn->hdrdgst_en, datalen ? conn->datadgst_en : 0);
	if (!count)
		return 0;

	if (task->sc) {
		struct scatterlist *sg;
		struct scsi_data_buffer *sdb;
		unsigned int sgoffset = offset;
		struct page *sgpg;
		unsigned int sglen;

		sdb = scsi_out(task->sc);
		sg = sdb->table.sgl;

		for_each_sg(sdb->table.sgl, sg, sdb->table.nents, i) {
			cxgb3i_tx_debug("sg %d, page 0x%p, len %u offset %u\n",
					i, sg_page(sg), sg->length, sg->offset);

			if (sgoffset < sg->length)
				break;
			sgoffset -= sg->length;
		}
		sgpg = sg_page(sg);
		sglen = sg->length - sgoffset;

		do {
			int j = skb_shinfo(skb)->nr_frags;
			unsigned int copy;

			if (!sglen) {
				sg = sg_next(sg);
				sgpg = sg_page(sg);
				sgoffset = 0;
				sglen = sg->length;
				++i;
			}
			copy = min(sglen, datalen);
			if (j && skb_can_coalesce(skb, j, sgpg,
						  sg->offset + sgoffset)) {
				skb_shinfo(skb)->frags[j - 1].size += copy;
			} else {
				get_page(sgpg);
				skb_fill_page_desc(skb, j, sgpg,
						   sg->offset + sgoffset, copy);
			}
			sgoffset += copy;
			sglen -= copy;
			datalen -= copy;
		} while (datalen);
	} else {
		pg = virt_to_page(task->data);

		while (datalen) {
			i = skb_shinfo(skb)->nr_frags;
			frag = &skb_shinfo(skb)->frags[i];

			get_page(pg);
			frag->page = pg;
			frag->page_offset = 0;
			frag->size = min((unsigned int)PAGE_SIZE, datalen);

			skb_shinfo(skb)->nr_frags++;
			datalen -= frag->size;
			pg++;
		}
	}

	if (padlen) {
		i = skb_shinfo(skb)->nr_frags;
		frag = &skb_shinfo(skb)->frags[i];
		frag->page = pad_page;
		frag->page_offset = 0;
		frag->size = padlen;
		skb_shinfo(skb)->nr_frags++;
	}

	datalen = count + padlen;
	skb->data_len += datalen;
	skb->truesize += datalen;
	skb->len += datalen;
	return 0;
}

int cxgb3i_conn_xmit_pdu(struct iscsi_task *task)
{
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct sk_buff *skb = tcp_task->dd_data;
	struct iscsi_tcp_conn *tcp_conn = task->conn->dd_data;
	struct cxgb3i_conn *cconn = tcp_conn->dd_data;
	unsigned int datalen;
	int err;

	if (!skb)
		return 0;

	datalen = skb->data_len;
	tcp_task->dd_data = NULL;
	err = cxgb3i_c3cn_send_pdus(cconn->cep->c3cn, skb);
	cxgb3i_tx_debug("task 0x%p, skb 0x%p, len %u/%u, rv %d.\n",
			task, skb, skb->len, skb->data_len, err);
	if (err > 0) {
		int pdulen = err;

		if (task->conn->hdrdgst_en)
			pdulen += ISCSI_DIGEST_SIZE;
		if (datalen && task->conn->datadgst_en)
			pdulen += ISCSI_DIGEST_SIZE;

		task->conn->txdata_octets += pdulen;
		return 0;
	}

	if (err < 0 && err != -EAGAIN) {
		kfree_skb(skb);
		cxgb3i_tx_debug("itt 0x%x, skb 0x%p, len %u/%u, xmit err %d.\n",
				task->itt, skb, skb->len, skb->data_len, err);
		iscsi_conn_printk(KERN_ERR, task->conn, "xmit err %d.\n", err);
		iscsi_conn_failure(task->conn, ISCSI_ERR_XMIT_FAILED);
		return err;
	}
	/* reset skb to send when we are called again */
	tcp_task->dd_data = skb;
	return -EAGAIN;
}

int cxgb3i_pdu_init(void)
{
	pad_page = alloc_page(GFP_KERNEL);
	if (!pad_page)
		return -ENOMEM;
	memset(page_address(pad_page), 0, PAGE_SIZE);
	return 0;
}

void cxgb3i_pdu_cleanup(void)
{
	if (pad_page) {
		__free_page(pad_page);
		pad_page = NULL;
	}
}

void cxgb3i_conn_pdu_ready(struct s3_conn *c3cn)
{
	struct sk_buff *skb;
	unsigned int read = 0;
	struct iscsi_conn *conn = c3cn->user_data;
	int err = 0;

	cxgb3i_rx_debug("cn 0x%p.\n", c3cn);

	read_lock(&c3cn->callback_lock);
	if (unlikely(!conn || conn->suspend_rx)) {
		cxgb3i_rx_debug("conn 0x%p, id %d, suspend_rx %lu!\n",
				conn, conn ? conn->id : 0xFF,
				conn ? conn->suspend_rx : 0xFF);
		read_unlock(&c3cn->callback_lock);
		return;
	}
	skb = skb_peek(&c3cn->receive_queue);
	while (!err && skb) {
		__skb_unlink(skb, &c3cn->receive_queue);
		read += skb_ulp_pdulen(skb);
		err = cxgb3i_conn_read_pdu_skb(conn, skb);
		__kfree_skb(skb);
		skb = skb_peek(&c3cn->receive_queue);
	}
	read_unlock(&c3cn->callback_lock);
	if (c3cn) {
		c3cn->copied_seq += read;
		cxgb3i_c3cn_rx_credits(c3cn, read);
	}
	conn->rxdata_octets += read;
}

void cxgb3i_conn_tx_open(struct s3_conn *c3cn)
{
	struct iscsi_conn *conn = c3cn->user_data;

	cxgb3i_tx_debug("cn 0x%p.\n", c3cn);
	if (conn) {
		cxgb3i_tx_debug("cn 0x%p, cid %d.\n", c3cn, conn->id);
		scsi_queue_work(conn->session->host, &conn->xmitwork);
	}
}

void cxgb3i_conn_closing(struct s3_conn *c3cn)
{
	struct iscsi_conn *conn;

	read_lock(&c3cn->callback_lock);
	conn = c3cn->user_data;
	if (conn && c3cn->state != C3CN_STATE_ESTABLISHED)
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	read_unlock(&c3cn->callback_lock);
}
