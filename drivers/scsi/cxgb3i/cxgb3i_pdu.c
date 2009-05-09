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

/* always allocate rooms for AHS */
#define SKB_TX_PDU_HEADER_LEN	\
	(sizeof(struct iscsi_hdr) + ISCSI_MAX_AHS_SIZE)
static unsigned int skb_extra_headroom;
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
	struct cxgb3i_task_data *tdata = task->dd_data +
					sizeof(struct iscsi_tcp_task);

	/* never reached the xmit task callout */
	if (tdata->skb)
		__kfree_skb(tdata->skb);
	memset(tdata, 0, sizeof(struct cxgb3i_task_data));

	/* MNC - Do we need a check in case this is called but
	 * cxgb3i_conn_alloc_pdu has never been called on the task */
	cxgb3i_release_itt(task, task->hdr_itt);
	iscsi_tcp_cleanup_task(task);
}

static int sgl_seek_offset(struct scatterlist *sgl, unsigned int sgcnt,
				unsigned int offset, unsigned int *off,
				struct scatterlist **sgp)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, sgcnt, i) {
		if (offset < sg->length) {
			*off = offset;
			*sgp = sg;
			return 0;
		}
		offset -= sg->length;
	}
	return -EFAULT;
}

static int sgl_read_to_frags(struct scatterlist *sg, unsigned int sgoffset,
				unsigned int dlen, skb_frag_t *frags,
				int frag_max)
{
	unsigned int datalen = dlen;
	unsigned int sglen = sg->length - sgoffset;
	struct page *page = sg_page(sg);
	int i;

	i = 0;
	do {
		unsigned int copy;

		if (!sglen) {
			sg = sg_next(sg);
			if (!sg) {
				cxgb3i_log_error("%s, sg NULL, len %u/%u.\n",
						 __func__, datalen, dlen);
				return -EINVAL;
			}
			sgoffset = 0;
			sglen = sg->length;
			page = sg_page(sg);

		}
		copy = min(datalen, sglen);
		if (i && page == frags[i - 1].page &&
		    sgoffset + sg->offset ==
			frags[i - 1].page_offset + frags[i - 1].size) {
			frags[i - 1].size += copy;
		} else {
			if (i >= frag_max) {
				cxgb3i_log_error("%s, too many pages %u, "
						 "dlen %u.\n", __func__,
						 frag_max, dlen);
				return -EINVAL;
			}

			frags[i].page = page;
			frags[i].page_offset = sg->offset + sgoffset;
			frags[i].size = copy;
			i++;
		}
		datalen -= copy;
		sgoffset += copy;
		sglen -= copy;
	} while (datalen);

	return i;
}

int cxgb3i_conn_alloc_pdu(struct iscsi_task *task, u8 opcode)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct cxgb3i_task_data *tdata = task->dd_data + sizeof(*tcp_task);
	struct scsi_cmnd *sc = task->sc;
	int headroom = SKB_TX_PDU_HEADER_LEN;

	tcp_task->dd_data = tdata;
	task->hdr = NULL;

	/* write command, need to send data pdus */
	if (skb_extra_headroom && (opcode == ISCSI_OP_SCSI_DATA_OUT ||
	    (opcode == ISCSI_OP_SCSI_CMD &&
	    (scsi_bidi_cmnd(sc) || sc->sc_data_direction == DMA_TO_DEVICE))))
		headroom += min(skb_extra_headroom, conn->max_xmit_dlength);

	tdata->skb = alloc_skb(TX_HEADER_LEN + headroom, GFP_ATOMIC);
	if (!tdata->skb)
		return -ENOMEM;
	skb_reserve(tdata->skb, TX_HEADER_LEN);

	cxgb3i_tx_debug("task 0x%p, opcode 0x%x, skb 0x%p.\n",
			task, opcode, tdata->skb);

	task->hdr = (struct iscsi_hdr *)tdata->skb->data;
	task->hdr_max = SKB_TX_PDU_HEADER_LEN;

	/* data_out uses scsi_cmd's itt */
	if (opcode != ISCSI_OP_SCSI_DATA_OUT)
		cxgb3i_reserve_itt(task, &task->hdr->itt);

	return 0;
}

int cxgb3i_conn_init_pdu(struct iscsi_task *task, unsigned int offset,
			      unsigned int count)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct cxgb3i_task_data *tdata = tcp_task->dd_data;
	struct sk_buff *skb = tdata->skb;
	unsigned int datalen = count;
	int i, padlen = iscsi_padding(count);
	struct page *pg;

	cxgb3i_tx_debug("task 0x%p,0x%p, offset %u, count %u, skb 0x%p.\n",
			task, task->sc, offset, count, skb);

	skb_put(skb, task->hdr_len);
	tx_skb_setmode(skb, conn->hdrdgst_en, datalen ? conn->datadgst_en : 0);
	if (!count)
		return 0;

	if (task->sc) {
		struct scsi_data_buffer *sdb = scsi_out(task->sc);
		struct scatterlist *sg = NULL;
		int err;

		tdata->offset = offset;
		tdata->count = count;
		err = sgl_seek_offset(sdb->table.sgl, sdb->table.nents,
					tdata->offset, &tdata->sgoffset, &sg);
		if (err < 0) {
			cxgb3i_log_warn("tpdu, sgl %u, bad offset %u/%u.\n",
					sdb->table.nents, tdata->offset,
					sdb->length);
			return err;
		}
		err = sgl_read_to_frags(sg, tdata->sgoffset, tdata->count,
					tdata->frags, MAX_PDU_FRAGS);
		if (err < 0) {
			cxgb3i_log_warn("tpdu, sgl %u, bad offset %u + %u.\n",
					sdb->table.nents, tdata->offset,
					tdata->count);
			return err;
		}
		tdata->nr_frags = err;

		if (tdata->nr_frags > MAX_SKB_FRAGS ||
		    (padlen && tdata->nr_frags == MAX_SKB_FRAGS)) {
			char *dst = skb->data + task->hdr_len;
			skb_frag_t *frag = tdata->frags;

			/* data fits in the skb's headroom */
			for (i = 0; i < tdata->nr_frags; i++, frag++) {
				char *src = kmap_atomic(frag->page,
							KM_SOFTIRQ0);

				memcpy(dst, src+frag->page_offset, frag->size);
				dst += frag->size;
				kunmap_atomic(src, KM_SOFTIRQ0);
			}
			if (padlen) {
				memset(dst, 0, padlen);
				padlen = 0;
			}
			skb_put(skb, count + padlen);
		} else {
			/* data fit into frag_list */
			for (i = 0; i < tdata->nr_frags; i++)
				get_page(tdata->frags[i].page);

			memcpy(skb_shinfo(skb)->frags, tdata->frags,
				sizeof(skb_frag_t) * tdata->nr_frags);
			skb_shinfo(skb)->nr_frags = tdata->nr_frags;
			skb->len += count;
			skb->data_len += count;
			skb->truesize += count;
		}

	} else {
		pg = virt_to_page(task->data);

		get_page(pg);
		skb_fill_page_desc(skb, 0, pg, offset_in_page(task->data),
					count);
		skb->len += count;
		skb->data_len += count;
		skb->truesize += count;
	}

	if (padlen) {
		i = skb_shinfo(skb)->nr_frags;
		get_page(pad_page);
		skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags, pad_page, 0,
				 padlen);

		skb->data_len += padlen;
		skb->truesize += padlen;
		skb->len += padlen;
	}

	return 0;
}

int cxgb3i_conn_xmit_pdu(struct iscsi_task *task)
{
	struct iscsi_tcp_conn *tcp_conn = task->conn->dd_data;
	struct cxgb3i_conn *cconn = tcp_conn->dd_data;
	struct iscsi_tcp_task *tcp_task = task->dd_data;
	struct cxgb3i_task_data *tdata = tcp_task->dd_data;
	struct sk_buff *skb = tdata->skb;
	unsigned int datalen;
	int err;

	if (!skb)
		return 0;

	datalen = skb->data_len;
	tdata->skb = NULL;
	err = cxgb3i_c3cn_send_pdus(cconn->cep->c3cn, skb);
	if (err > 0) {
		int pdulen = err;

	cxgb3i_tx_debug("task 0x%p, skb 0x%p, len %u/%u, rv %d.\n",
			task, skb, skb->len, skb->data_len, err);

		if (task->conn->hdrdgst_en)
			pdulen += ISCSI_DIGEST_SIZE;
		if (datalen && task->conn->datadgst_en)
			pdulen += ISCSI_DIGEST_SIZE;

		task->conn->txdata_octets += pdulen;
		return 0;
	}

	if (err == -EAGAIN || err == -ENOBUFS) {
		/* reset skb to send when we are called again */
		tdata->skb = skb;
		return err;
	}

	kfree_skb(skb);
	cxgb3i_tx_debug("itt 0x%x, skb 0x%p, len %u/%u, xmit err %d.\n",
			task->itt, skb, skb->len, skb->data_len, err);
	iscsi_conn_printk(KERN_ERR, task->conn, "xmit err %d.\n", err);
	iscsi_conn_failure(task->conn, ISCSI_ERR_XMIT_FAILED);
	return err;
}

int cxgb3i_pdu_init(void)
{
	if (SKB_TX_HEADROOM > (512 * MAX_SKB_FRAGS))
		skb_extra_headroom = SKB_TX_HEADROOM;
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
		read += skb_rx_pdulen(skb);
		cxgb3i_rx_debug("conn 0x%p, cn 0x%p, rx skb 0x%p, pdulen %u.\n",
				conn, c3cn, skb, skb_rx_pdulen(skb));
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

	if (err) {
		cxgb3i_log_info("conn 0x%p rx failed err %d.\n", conn, err);
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	}
}

void cxgb3i_conn_tx_open(struct s3_conn *c3cn)
{
	struct iscsi_conn *conn = c3cn->user_data;

	cxgb3i_tx_debug("cn 0x%p.\n", c3cn);
	if (conn) {
		cxgb3i_tx_debug("cn 0x%p, cid %d.\n", c3cn, conn->id);
		iscsi_conn_queue_work(conn);
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
