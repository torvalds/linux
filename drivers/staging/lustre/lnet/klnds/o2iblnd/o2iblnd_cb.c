/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/klnds/o2iblnd/o2iblnd_cb.c
 *
 * Author: Eric Barton <eric@bartonsoftware.com>
 */

#include "o2iblnd.h"

#define MAX_CONN_RACES_BEFORE_ABORT 20

static void kiblnd_peer_alive(struct kib_peer *peer);
static void kiblnd_peer_connect_failed(struct kib_peer *peer, int active, int error);
static void kiblnd_init_tx_msg(lnet_ni_t *ni, struct kib_tx *tx,
			       int type, int body_nob);
static int kiblnd_init_rdma(struct kib_conn *conn, struct kib_tx *tx, int type,
			    int resid, struct kib_rdma_desc *dstrd,
			    __u64 dstcookie);
static void kiblnd_queue_tx_locked(struct kib_tx *tx, struct kib_conn *conn);
static void kiblnd_queue_tx(struct kib_tx *tx, struct kib_conn *conn);
static void kiblnd_unmap_tx(lnet_ni_t *ni, struct kib_tx *tx);
static void kiblnd_check_sends_locked(struct kib_conn *conn);

static void
kiblnd_tx_done(lnet_ni_t *ni, struct kib_tx *tx)
{
	lnet_msg_t *lntmsg[2];
	struct kib_net *net = ni->ni_data;
	int rc;
	int i;

	LASSERT(net);
	LASSERT(!in_interrupt());
	LASSERT(!tx->tx_queued);	       /* mustn't be queued for sending */
	LASSERT(!tx->tx_sending);	  /* mustn't be awaiting sent callback */
	LASSERT(!tx->tx_waiting);	      /* mustn't be awaiting peer response */
	LASSERT(tx->tx_pool);

	kiblnd_unmap_tx(ni, tx);

	/* tx may have up to 2 lnet msgs to finalise */
	lntmsg[0] = tx->tx_lntmsg[0]; tx->tx_lntmsg[0] = NULL;
	lntmsg[1] = tx->tx_lntmsg[1]; tx->tx_lntmsg[1] = NULL;
	rc = tx->tx_status;

	if (tx->tx_conn) {
		LASSERT(ni == tx->tx_conn->ibc_peer->ibp_ni);

		kiblnd_conn_decref(tx->tx_conn);
		tx->tx_conn = NULL;
	}

	tx->tx_nwrq = 0;
	tx->tx_status = 0;

	kiblnd_pool_free_node(&tx->tx_pool->tpo_pool, &tx->tx_list);

	/* delay finalize until my descs have been freed */
	for (i = 0; i < 2; i++) {
		if (!lntmsg[i])
			continue;

		lnet_finalize(ni, lntmsg[i], rc);
	}
}

void
kiblnd_txlist_done(lnet_ni_t *ni, struct list_head *txlist, int status)
{
	struct kib_tx *tx;

	while (!list_empty(txlist)) {
		tx = list_entry(txlist->next, struct kib_tx, tx_list);

		list_del(&tx->tx_list);
		/* complete now */
		tx->tx_waiting = 0;
		tx->tx_status = status;
		kiblnd_tx_done(ni, tx);
	}
}

static struct kib_tx *
kiblnd_get_idle_tx(lnet_ni_t *ni, lnet_nid_t target)
{
	struct kib_net *net = (struct kib_net *)ni->ni_data;
	struct list_head *node;
	struct kib_tx *tx;
	struct kib_tx_poolset *tps;

	tps = net->ibn_tx_ps[lnet_cpt_of_nid(target)];
	node = kiblnd_pool_alloc_node(&tps->tps_poolset);
	if (!node)
		return NULL;
	tx = list_entry(node, struct kib_tx, tx_list);

	LASSERT(!tx->tx_nwrq);
	LASSERT(!tx->tx_queued);
	LASSERT(!tx->tx_sending);
	LASSERT(!tx->tx_waiting);
	LASSERT(!tx->tx_status);
	LASSERT(!tx->tx_conn);
	LASSERT(!tx->tx_lntmsg[0]);
	LASSERT(!tx->tx_lntmsg[1]);
	LASSERT(!tx->tx_nfrags);

	return tx;
}

static void
kiblnd_drop_rx(struct kib_rx *rx)
{
	struct kib_conn *conn = rx->rx_conn;
	struct kib_sched_info *sched = conn->ibc_sched;
	unsigned long flags;

	spin_lock_irqsave(&sched->ibs_lock, flags);
	LASSERT(conn->ibc_nrx > 0);
	conn->ibc_nrx--;
	spin_unlock_irqrestore(&sched->ibs_lock, flags);

	kiblnd_conn_decref(conn);
}

int
kiblnd_post_rx(struct kib_rx *rx, int credit)
{
	struct kib_conn *conn = rx->rx_conn;
	struct kib_net *net = conn->ibc_peer->ibp_ni->ni_data;
	struct ib_recv_wr *bad_wrq = NULL;
	struct ib_mr *mr = conn->ibc_hdev->ibh_mrs;
	int rc;

	LASSERT(net);
	LASSERT(!in_interrupt());
	LASSERT(credit == IBLND_POSTRX_NO_CREDIT ||
		credit == IBLND_POSTRX_PEER_CREDIT ||
		credit == IBLND_POSTRX_RSRVD_CREDIT);
	LASSERT(mr);

	rx->rx_sge.lkey   = mr->lkey;
	rx->rx_sge.addr   = rx->rx_msgaddr;
	rx->rx_sge.length = IBLND_MSG_SIZE;

	rx->rx_wrq.next    = NULL;
	rx->rx_wrq.sg_list = &rx->rx_sge;
	rx->rx_wrq.num_sge = 1;
	rx->rx_wrq.wr_id   = kiblnd_ptr2wreqid(rx, IBLND_WID_RX);

	LASSERT(conn->ibc_state >= IBLND_CONN_INIT);
	LASSERT(rx->rx_nob >= 0);	      /* not posted */

	if (conn->ibc_state > IBLND_CONN_ESTABLISHED) {
		kiblnd_drop_rx(rx);	     /* No more posts for this rx */
		return 0;
	}

	rx->rx_nob = -1;			/* flag posted */

	/* NB: need an extra reference after ib_post_recv because we don't
	 * own this rx (and rx::rx_conn) anymore, LU-5678.
	 */
	kiblnd_conn_addref(conn);
	rc = ib_post_recv(conn->ibc_cmid->qp, &rx->rx_wrq, &bad_wrq);
	if (unlikely(rc)) {
		CERROR("Can't post rx for %s: %d, bad_wrq: %p\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid), rc, bad_wrq);
		rx->rx_nob = 0;
	}

	if (conn->ibc_state < IBLND_CONN_ESTABLISHED) /* Initial post */
		goto out;

	if (unlikely(rc)) {
		kiblnd_close_conn(conn, rc);
		kiblnd_drop_rx(rx);	     /* No more posts for this rx */
		goto out;
	}

	if (credit == IBLND_POSTRX_NO_CREDIT)
		goto out;

	spin_lock(&conn->ibc_lock);
	if (credit == IBLND_POSTRX_PEER_CREDIT)
		conn->ibc_outstanding_credits++;
	else
		conn->ibc_reserved_credits++;
	kiblnd_check_sends_locked(conn);
	spin_unlock(&conn->ibc_lock);

out:
	kiblnd_conn_decref(conn);
	return rc;
}

static struct kib_tx *
kiblnd_find_waiting_tx_locked(struct kib_conn *conn, int txtype, __u64 cookie)
{
	struct list_head *tmp;

	list_for_each(tmp, &conn->ibc_active_txs) {
		struct kib_tx *tx = list_entry(tmp, struct kib_tx, tx_list);

		LASSERT(!tx->tx_queued);
		LASSERT(tx->tx_sending || tx->tx_waiting);

		if (tx->tx_cookie != cookie)
			continue;

		if (tx->tx_waiting &&
		    tx->tx_msg->ibm_type == txtype)
			return tx;

		CWARN("Bad completion: %swaiting, type %x (wanted %x)\n",
		      tx->tx_waiting ? "" : "NOT ",
		      tx->tx_msg->ibm_type, txtype);
	}
	return NULL;
}

static void
kiblnd_handle_completion(struct kib_conn *conn, int txtype, int status, __u64 cookie)
{
	struct kib_tx *tx;
	lnet_ni_t *ni = conn->ibc_peer->ibp_ni;
	int idle;

	spin_lock(&conn->ibc_lock);

	tx = kiblnd_find_waiting_tx_locked(conn, txtype, cookie);
	if (!tx) {
		spin_unlock(&conn->ibc_lock);

		CWARN("Unmatched completion type %x cookie %#llx from %s\n",
		      txtype, cookie, libcfs_nid2str(conn->ibc_peer->ibp_nid));
		kiblnd_close_conn(conn, -EPROTO);
		return;
	}

	if (!tx->tx_status) {	       /* success so far */
		if (status < 0) /* failed? */
			tx->tx_status = status;
		else if (txtype == IBLND_MSG_GET_REQ)
			lnet_set_reply_msg_len(ni, tx->tx_lntmsg[1], status);
	}

	tx->tx_waiting = 0;

	idle = !tx->tx_queued && !tx->tx_sending;
	if (idle)
		list_del(&tx->tx_list);

	spin_unlock(&conn->ibc_lock);

	if (idle)
		kiblnd_tx_done(ni, tx);
}

static void
kiblnd_send_completion(struct kib_conn *conn, int type, int status, __u64 cookie)
{
	lnet_ni_t *ni = conn->ibc_peer->ibp_ni;
	struct kib_tx *tx = kiblnd_get_idle_tx(ni, conn->ibc_peer->ibp_nid);

	if (!tx) {
		CERROR("Can't get tx for completion %x for %s\n",
		       type, libcfs_nid2str(conn->ibc_peer->ibp_nid));
		return;
	}

	tx->tx_msg->ibm_u.completion.ibcm_status = status;
	tx->tx_msg->ibm_u.completion.ibcm_cookie = cookie;
	kiblnd_init_tx_msg(ni, tx, type, sizeof(struct kib_completion_msg));

	kiblnd_queue_tx(tx, conn);
}

static void
kiblnd_handle_rx(struct kib_rx *rx)
{
	struct kib_msg *msg = rx->rx_msg;
	struct kib_conn *conn = rx->rx_conn;
	lnet_ni_t *ni = conn->ibc_peer->ibp_ni;
	int credits = msg->ibm_credits;
	struct kib_tx *tx;
	int rc = 0;
	int rc2;
	int post_credit;

	LASSERT(conn->ibc_state >= IBLND_CONN_ESTABLISHED);

	CDEBUG(D_NET, "Received %x[%d] from %s\n",
	       msg->ibm_type, credits,
	       libcfs_nid2str(conn->ibc_peer->ibp_nid));

	if (credits) {
		/* Have I received credits that will let me send? */
		spin_lock(&conn->ibc_lock);

		if (conn->ibc_credits + credits >
		    conn->ibc_queue_depth) {
			rc2 = conn->ibc_credits;
			spin_unlock(&conn->ibc_lock);

			CERROR("Bad credits from %s: %d + %d > %d\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid),
			       rc2, credits, conn->ibc_queue_depth);

			kiblnd_close_conn(conn, -EPROTO);
			kiblnd_post_rx(rx, IBLND_POSTRX_NO_CREDIT);
			return;
		}

		conn->ibc_credits += credits;

		/* This ensures the credit taken by NOOP can be returned */
		if (msg->ibm_type == IBLND_MSG_NOOP &&
		    !IBLND_OOB_CAPABLE(conn->ibc_version)) /* v1 only */
			conn->ibc_outstanding_credits++;

		kiblnd_check_sends_locked(conn);
		spin_unlock(&conn->ibc_lock);
	}

	switch (msg->ibm_type) {
	default:
		CERROR("Bad IBLND message type %x from %s\n",
		       msg->ibm_type, libcfs_nid2str(conn->ibc_peer->ibp_nid));
		post_credit = IBLND_POSTRX_NO_CREDIT;
		rc = -EPROTO;
		break;

	case IBLND_MSG_NOOP:
		if (IBLND_OOB_CAPABLE(conn->ibc_version)) {
			post_credit = IBLND_POSTRX_NO_CREDIT;
			break;
		}

		if (credits) /* credit already posted */
			post_credit = IBLND_POSTRX_NO_CREDIT;
		else	      /* a keepalive NOOP */
			post_credit = IBLND_POSTRX_PEER_CREDIT;
		break;

	case IBLND_MSG_IMMEDIATE:
		post_credit = IBLND_POSTRX_DONT_POST;
		rc = lnet_parse(ni, &msg->ibm_u.immediate.ibim_hdr,
				msg->ibm_srcnid, rx, 0);
		if (rc < 0)		     /* repost on error */
			post_credit = IBLND_POSTRX_PEER_CREDIT;
		break;

	case IBLND_MSG_PUT_REQ:
		post_credit = IBLND_POSTRX_DONT_POST;
		rc = lnet_parse(ni, &msg->ibm_u.putreq.ibprm_hdr,
				msg->ibm_srcnid, rx, 1);
		if (rc < 0)		     /* repost on error */
			post_credit = IBLND_POSTRX_PEER_CREDIT;
		break;

	case IBLND_MSG_PUT_NAK:
		CWARN("PUT_NACK from %s\n",
		      libcfs_nid2str(conn->ibc_peer->ibp_nid));
		post_credit = IBLND_POSTRX_RSRVD_CREDIT;
		kiblnd_handle_completion(conn, IBLND_MSG_PUT_REQ,
					 msg->ibm_u.completion.ibcm_status,
					 msg->ibm_u.completion.ibcm_cookie);
		break;

	case IBLND_MSG_PUT_ACK:
		post_credit = IBLND_POSTRX_RSRVD_CREDIT;

		spin_lock(&conn->ibc_lock);
		tx = kiblnd_find_waiting_tx_locked(conn, IBLND_MSG_PUT_REQ,
						   msg->ibm_u.putack.ibpam_src_cookie);
		if (tx)
			list_del(&tx->tx_list);
		spin_unlock(&conn->ibc_lock);

		if (!tx) {
			CERROR("Unmatched PUT_ACK from %s\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid));
			rc = -EPROTO;
			break;
		}

		LASSERT(tx->tx_waiting);
		/*
		 * CAVEAT EMPTOR: I could be racing with tx_complete, but...
		 * (a) I can overwrite tx_msg since my peer has received it!
		 * (b) tx_waiting set tells tx_complete() it's not done.
		 */
		tx->tx_nwrq = 0;		/* overwrite PUT_REQ */

		rc2 = kiblnd_init_rdma(conn, tx, IBLND_MSG_PUT_DONE,
				       kiblnd_rd_size(&msg->ibm_u.putack.ibpam_rd),
				       &msg->ibm_u.putack.ibpam_rd,
				       msg->ibm_u.putack.ibpam_dst_cookie);
		if (rc2 < 0)
			CERROR("Can't setup rdma for PUT to %s: %d\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid), rc2);

		spin_lock(&conn->ibc_lock);
		tx->tx_waiting = 0;	/* clear waiting and queue atomically */
		kiblnd_queue_tx_locked(tx, conn);
		spin_unlock(&conn->ibc_lock);
		break;

	case IBLND_MSG_PUT_DONE:
		post_credit = IBLND_POSTRX_PEER_CREDIT;
		kiblnd_handle_completion(conn, IBLND_MSG_PUT_ACK,
					 msg->ibm_u.completion.ibcm_status,
					 msg->ibm_u.completion.ibcm_cookie);
		break;

	case IBLND_MSG_GET_REQ:
		post_credit = IBLND_POSTRX_DONT_POST;
		rc = lnet_parse(ni, &msg->ibm_u.get.ibgm_hdr,
				msg->ibm_srcnid, rx, 1);
		if (rc < 0)		     /* repost on error */
			post_credit = IBLND_POSTRX_PEER_CREDIT;
		break;

	case IBLND_MSG_GET_DONE:
		post_credit = IBLND_POSTRX_RSRVD_CREDIT;
		kiblnd_handle_completion(conn, IBLND_MSG_GET_REQ,
					 msg->ibm_u.completion.ibcm_status,
					 msg->ibm_u.completion.ibcm_cookie);
		break;
	}

	if (rc < 0)			     /* protocol error */
		kiblnd_close_conn(conn, rc);

	if (post_credit != IBLND_POSTRX_DONT_POST)
		kiblnd_post_rx(rx, post_credit);
}

static void
kiblnd_rx_complete(struct kib_rx *rx, int status, int nob)
{
	struct kib_msg *msg = rx->rx_msg;
	struct kib_conn *conn = rx->rx_conn;
	lnet_ni_t *ni = conn->ibc_peer->ibp_ni;
	struct kib_net *net = ni->ni_data;
	int rc;
	int err = -EIO;

	LASSERT(net);
	LASSERT(rx->rx_nob < 0);	       /* was posted */
	rx->rx_nob = 0;			 /* isn't now */

	if (conn->ibc_state > IBLND_CONN_ESTABLISHED)
		goto ignore;

	if (status != IB_WC_SUCCESS) {
		CNETERR("Rx from %s failed: %d\n",
			libcfs_nid2str(conn->ibc_peer->ibp_nid), status);
		goto failed;
	}

	LASSERT(nob >= 0);
	rx->rx_nob = nob;

	rc = kiblnd_unpack_msg(msg, rx->rx_nob);
	if (rc) {
		CERROR("Error %d unpacking rx from %s\n",
		       rc, libcfs_nid2str(conn->ibc_peer->ibp_nid));
		goto failed;
	}

	if (msg->ibm_srcnid != conn->ibc_peer->ibp_nid ||
	    msg->ibm_dstnid != ni->ni_nid ||
	    msg->ibm_srcstamp != conn->ibc_incarnation ||
	    msg->ibm_dststamp != net->ibn_incarnation) {
		CERROR("Stale rx from %s\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid));
		err = -ESTALE;
		goto failed;
	}

	/* set time last known alive */
	kiblnd_peer_alive(conn->ibc_peer);

	/* racing with connection establishment/teardown! */

	if (conn->ibc_state < IBLND_CONN_ESTABLISHED) {
		rwlock_t *g_lock = &kiblnd_data.kib_global_lock;
		unsigned long flags;

		write_lock_irqsave(g_lock, flags);
		/* must check holding global lock to eliminate race */
		if (conn->ibc_state < IBLND_CONN_ESTABLISHED) {
			list_add_tail(&rx->rx_list, &conn->ibc_early_rxs);
			write_unlock_irqrestore(g_lock, flags);
			return;
		}
		write_unlock_irqrestore(g_lock, flags);
	}
	kiblnd_handle_rx(rx);
	return;

 failed:
	CDEBUG(D_NET, "rx %p conn %p\n", rx, conn);
	kiblnd_close_conn(conn, err);
 ignore:
	kiblnd_drop_rx(rx);		     /* Don't re-post rx. */
}

static struct page *
kiblnd_kvaddr_to_page(unsigned long vaddr)
{
	struct page *page;

	if (is_vmalloc_addr((void *)vaddr)) {
		page = vmalloc_to_page((void *)vaddr);
		LASSERT(page);
		return page;
	}
#ifdef CONFIG_HIGHMEM
	if (vaddr >= PKMAP_BASE &&
	    vaddr < (PKMAP_BASE + LAST_PKMAP * PAGE_SIZE)) {
		/* No highmem pages only used for bulk (kiov) I/O */
		CERROR("find page for address in highmem\n");
		LBUG();
	}
#endif
	page = virt_to_page(vaddr);
	LASSERT(page);
	return page;
}

static int
kiblnd_fmr_map_tx(struct kib_net *net, struct kib_tx *tx, struct kib_rdma_desc *rd, __u32 nob)
{
	struct kib_hca_dev *hdev;
	struct kib_fmr_poolset *fps;
	int cpt;
	int rc;

	LASSERT(tx->tx_pool);
	LASSERT(tx->tx_pool->tpo_pool.po_owner);

	hdev = tx->tx_pool->tpo_hdev;
	cpt = tx->tx_pool->tpo_pool.po_owner->ps_cpt;

	fps = net->ibn_fmr_ps[cpt];
	rc = kiblnd_fmr_pool_map(fps, tx, rd, nob, 0, &tx->fmr);
	if (rc) {
		CERROR("Can't map %u bytes: %d\n", nob, rc);
		return rc;
	}

	/*
	 * If rd is not tx_rd, it's going to get sent to a peer, who will need
	 * the rkey
	 */
	rd->rd_key = tx->fmr.fmr_key;
	rd->rd_frags[0].rf_addr &= ~hdev->ibh_page_mask;
	rd->rd_frags[0].rf_nob = nob;
	rd->rd_nfrags = 1;

	return 0;
}

static void kiblnd_unmap_tx(lnet_ni_t *ni, struct kib_tx *tx)
{
	struct kib_net *net = ni->ni_data;

	LASSERT(net);

	if (net->ibn_fmr_ps)
		kiblnd_fmr_pool_unmap(&tx->fmr, tx->tx_status);

	if (tx->tx_nfrags) {
		kiblnd_dma_unmap_sg(tx->tx_pool->tpo_hdev->ibh_ibdev,
				    tx->tx_frags, tx->tx_nfrags, tx->tx_dmadir);
		tx->tx_nfrags = 0;
	}
}

static int kiblnd_map_tx(lnet_ni_t *ni, struct kib_tx *tx, struct kib_rdma_desc *rd,
			 int nfrags)
{
	struct kib_net *net = ni->ni_data;
	struct kib_hca_dev *hdev = net->ibn_dev->ibd_hdev;
	struct ib_mr *mr    = NULL;
	__u32 nob;
	int i;

	/*
	 * If rd is not tx_rd, it's going to get sent to a peer and I'm the
	 * RDMA sink
	 */
	tx->tx_dmadir = (rd != tx->tx_rd) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	tx->tx_nfrags = nfrags;

	rd->rd_nfrags = kiblnd_dma_map_sg(hdev->ibh_ibdev, tx->tx_frags,
					  tx->tx_nfrags, tx->tx_dmadir);

	for (i = 0, nob = 0; i < rd->rd_nfrags; i++) {
		rd->rd_frags[i].rf_nob  = kiblnd_sg_dma_len(
			hdev->ibh_ibdev, &tx->tx_frags[i]);
		rd->rd_frags[i].rf_addr = kiblnd_sg_dma_address(
			hdev->ibh_ibdev, &tx->tx_frags[i]);
		nob += rd->rd_frags[i].rf_nob;
	}

	mr = kiblnd_find_rd_dma_mr(ni, rd, tx->tx_conn ?
				   tx->tx_conn->ibc_max_frags : -1);
	if (mr) {
		/* found pre-mapping MR */
		rd->rd_key = (rd != tx->tx_rd) ? mr->rkey : mr->lkey;
		return 0;
	}

	if (net->ibn_fmr_ps)
		return kiblnd_fmr_map_tx(net, tx, rd, nob);

	return -EINVAL;
}

static int
kiblnd_setup_rd_iov(lnet_ni_t *ni, struct kib_tx *tx, struct kib_rdma_desc *rd,
		    unsigned int niov, const struct kvec *iov, int offset, int nob)
{
	struct kib_net *net = ni->ni_data;
	struct page *page;
	struct scatterlist *sg;
	unsigned long vaddr;
	int fragnob;
	int page_offset;

	LASSERT(nob > 0);
	LASSERT(niov > 0);
	LASSERT(net);

	while (offset >= iov->iov_len) {
		offset -= iov->iov_len;
		niov--;
		iov++;
		LASSERT(niov > 0);
	}

	sg = tx->tx_frags;
	do {
		LASSERT(niov > 0);

		vaddr = ((unsigned long)iov->iov_base) + offset;
		page_offset = vaddr & (PAGE_SIZE - 1);
		page = kiblnd_kvaddr_to_page(vaddr);
		if (!page) {
			CERROR("Can't find page\n");
			return -EFAULT;
		}

		fragnob = min((int)(iov->iov_len - offset), nob);
		fragnob = min(fragnob, (int)PAGE_SIZE - page_offset);

		sg_set_page(sg, page, fragnob, page_offset);
		sg = sg_next(sg);
		if (!sg) {
			CERROR("lacking enough sg entries to map tx\n");
			return -EFAULT;
		}

		if (offset + fragnob < iov->iov_len) {
			offset += fragnob;
		} else {
			offset = 0;
			iov++;
			niov--;
		}
		nob -= fragnob;
	} while (nob > 0);

	return kiblnd_map_tx(ni, tx, rd, sg - tx->tx_frags);
}

static int
kiblnd_setup_rd_kiov(lnet_ni_t *ni, struct kib_tx *tx, struct kib_rdma_desc *rd,
		     int nkiov, const lnet_kiov_t *kiov, int offset, int nob)
{
	struct kib_net *net = ni->ni_data;
	struct scatterlist *sg;
	int fragnob;

	CDEBUG(D_NET, "niov %d offset %d nob %d\n", nkiov, offset, nob);

	LASSERT(nob > 0);
	LASSERT(nkiov > 0);
	LASSERT(net);

	while (offset >= kiov->bv_len) {
		offset -= kiov->bv_len;
		nkiov--;
		kiov++;
		LASSERT(nkiov > 0);
	}

	sg = tx->tx_frags;
	do {
		LASSERT(nkiov > 0);

		fragnob = min((int)(kiov->bv_len - offset), nob);

		sg_set_page(sg, kiov->bv_page, fragnob,
			    kiov->bv_offset + offset);
		sg = sg_next(sg);
		if (!sg) {
			CERROR("lacking enough sg entries to map tx\n");
			return -EFAULT;
		}

		offset = 0;
		kiov++;
		nkiov--;
		nob -= fragnob;
	} while (nob > 0);

	return kiblnd_map_tx(ni, tx, rd, sg - tx->tx_frags);
}

static int
kiblnd_post_tx_locked(struct kib_conn *conn, struct kib_tx *tx, int credit)
	__must_hold(&conn->ibc_lock)
{
	struct kib_msg *msg = tx->tx_msg;
	struct kib_peer *peer = conn->ibc_peer;
	struct lnet_ni *ni = peer->ibp_ni;
	int ver = conn->ibc_version;
	int rc;
	int done;

	LASSERT(tx->tx_queued);
	/* We rely on this for QP sizing */
	LASSERT(tx->tx_nwrq > 0);

	LASSERT(!credit || credit == 1);
	LASSERT(conn->ibc_outstanding_credits >= 0);
	LASSERT(conn->ibc_outstanding_credits <= conn->ibc_queue_depth);
	LASSERT(conn->ibc_credits >= 0);
	LASSERT(conn->ibc_credits <= conn->ibc_queue_depth);

	if (conn->ibc_nsends_posted == kiblnd_concurrent_sends(ver, ni)) {
		/* tx completions outstanding... */
		CDEBUG(D_NET, "%s: posted enough\n",
		       libcfs_nid2str(peer->ibp_nid));
		return -EAGAIN;
	}

	if (credit && !conn->ibc_credits) {   /* no credits */
		CDEBUG(D_NET, "%s: no credits\n",
		       libcfs_nid2str(peer->ibp_nid));
		return -EAGAIN;
	}

	if (credit && !IBLND_OOB_CAPABLE(ver) &&
	    conn->ibc_credits == 1 &&   /* last credit reserved */
	    msg->ibm_type != IBLND_MSG_NOOP) {      /* for NOOP */
		CDEBUG(D_NET, "%s: not using last credit\n",
		       libcfs_nid2str(peer->ibp_nid));
		return -EAGAIN;
	}

	/* NB don't drop ibc_lock before bumping tx_sending */
	list_del(&tx->tx_list);
	tx->tx_queued = 0;

	if (msg->ibm_type == IBLND_MSG_NOOP &&
	    (!kiblnd_need_noop(conn) ||     /* redundant NOOP */
	     (IBLND_OOB_CAPABLE(ver) && /* posted enough NOOP */
	      conn->ibc_noops_posted == IBLND_OOB_MSGS(ver)))) {
		/*
		 * OK to drop when posted enough NOOPs, since
		 * kiblnd_check_sends_locked will queue NOOP again when
		 * posted NOOPs complete
		 */
		spin_unlock(&conn->ibc_lock);
		kiblnd_tx_done(peer->ibp_ni, tx);
		spin_lock(&conn->ibc_lock);
		CDEBUG(D_NET, "%s(%d): redundant or enough NOOP\n",
		       libcfs_nid2str(peer->ibp_nid),
		       conn->ibc_noops_posted);
		return 0;
	}

	kiblnd_pack_msg(peer->ibp_ni, msg, ver, conn->ibc_outstanding_credits,
			peer->ibp_nid, conn->ibc_incarnation);

	conn->ibc_credits -= credit;
	conn->ibc_outstanding_credits = 0;
	conn->ibc_nsends_posted++;
	if (msg->ibm_type == IBLND_MSG_NOOP)
		conn->ibc_noops_posted++;

	/*
	 * CAVEAT EMPTOR!  This tx could be the PUT_DONE of an RDMA
	 * PUT.  If so, it was first queued here as a PUT_REQ, sent and
	 * stashed on ibc_active_txs, matched by an incoming PUT_ACK,
	 * and then re-queued here.  It's (just) possible that
	 * tx_sending is non-zero if we've not done the tx_complete()
	 * from the first send; hence the ++ rather than = below.
	 */
	tx->tx_sending++;
	list_add(&tx->tx_list, &conn->ibc_active_txs);

	/* I'm still holding ibc_lock! */
	if (conn->ibc_state != IBLND_CONN_ESTABLISHED) {
		rc = -ECONNABORTED;
	} else if (tx->tx_pool->tpo_pool.po_failed ||
		 conn->ibc_hdev != tx->tx_pool->tpo_hdev) {
		/* close_conn will launch failover */
		rc = -ENETDOWN;
	} else {
		struct kib_fast_reg_descriptor *frd = tx->fmr.fmr_frd;
		struct ib_send_wr *bad = &tx->tx_wrq[tx->tx_nwrq - 1].wr;
		struct ib_send_wr *wrq = &tx->tx_wrq[0].wr;

		if (frd) {
			if (!frd->frd_valid) {
				wrq = &frd->frd_inv_wr;
				wrq->next = &frd->frd_fastreg_wr.wr;
			} else {
				wrq = &frd->frd_fastreg_wr.wr;
			}
			frd->frd_fastreg_wr.wr.next = &tx->tx_wrq[0].wr;
		}

		LASSERTF(bad->wr_id == kiblnd_ptr2wreqid(tx, IBLND_WID_TX),
			 "bad wr_id %llx, opc %d, flags %d, peer: %s\n",
			 bad->wr_id, bad->opcode, bad->send_flags,
			 libcfs_nid2str(conn->ibc_peer->ibp_nid));
		bad = NULL;
		rc = ib_post_send(conn->ibc_cmid->qp, wrq, &bad);
	}

	conn->ibc_last_send = jiffies;

	if (!rc)
		return 0;

	/*
	 * NB credits are transferred in the actual
	 * message, which can only be the last work item
	 */
	conn->ibc_credits += credit;
	conn->ibc_outstanding_credits += msg->ibm_credits;
	conn->ibc_nsends_posted--;
	if (msg->ibm_type == IBLND_MSG_NOOP)
		conn->ibc_noops_posted--;

	tx->tx_status = rc;
	tx->tx_waiting = 0;
	tx->tx_sending--;

	done = !tx->tx_sending;
	if (done)
		list_del(&tx->tx_list);

	spin_unlock(&conn->ibc_lock);

	if (conn->ibc_state == IBLND_CONN_ESTABLISHED)
		CERROR("Error %d posting transmit to %s\n",
		       rc, libcfs_nid2str(peer->ibp_nid));
	else
		CDEBUG(D_NET, "Error %d posting transmit to %s\n",
		       rc, libcfs_nid2str(peer->ibp_nid));

	kiblnd_close_conn(conn, rc);

	if (done)
		kiblnd_tx_done(peer->ibp_ni, tx);

	spin_lock(&conn->ibc_lock);

	return -EIO;
}

static void
kiblnd_check_sends_locked(struct kib_conn *conn)
{
	int ver = conn->ibc_version;
	lnet_ni_t *ni = conn->ibc_peer->ibp_ni;
	struct kib_tx *tx;

	/* Don't send anything until after the connection is established */
	if (conn->ibc_state < IBLND_CONN_ESTABLISHED) {
		CDEBUG(D_NET, "%s too soon\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid));
		return;
	}

	LASSERT(conn->ibc_nsends_posted <= kiblnd_concurrent_sends(ver, ni));
	LASSERT(!IBLND_OOB_CAPABLE(ver) ||
		conn->ibc_noops_posted <= IBLND_OOB_MSGS(ver));
	LASSERT(conn->ibc_reserved_credits >= 0);

	while (conn->ibc_reserved_credits > 0 &&
	       !list_empty(&conn->ibc_tx_queue_rsrvd)) {
		tx = list_entry(conn->ibc_tx_queue_rsrvd.next,
				struct kib_tx, tx_list);
		list_del(&tx->tx_list);
		list_add_tail(&tx->tx_list, &conn->ibc_tx_queue);
		conn->ibc_reserved_credits--;
	}

	if (kiblnd_need_noop(conn)) {
		spin_unlock(&conn->ibc_lock);

		tx = kiblnd_get_idle_tx(ni, conn->ibc_peer->ibp_nid);
		if (tx)
			kiblnd_init_tx_msg(ni, tx, IBLND_MSG_NOOP, 0);

		spin_lock(&conn->ibc_lock);
		if (tx)
			kiblnd_queue_tx_locked(tx, conn);
	}

	for (;;) {
		int credit;

		if (!list_empty(&conn->ibc_tx_queue_nocred)) {
			credit = 0;
			tx = list_entry(conn->ibc_tx_queue_nocred.next,
					struct kib_tx, tx_list);
		} else if (!list_empty(&conn->ibc_tx_noops)) {
			LASSERT(!IBLND_OOB_CAPABLE(ver));
			credit = 1;
			tx = list_entry(conn->ibc_tx_noops.next,
					struct kib_tx, tx_list);
		} else if (!list_empty(&conn->ibc_tx_queue)) {
			credit = 1;
			tx = list_entry(conn->ibc_tx_queue.next,
					struct kib_tx, tx_list);
		} else {
			break;
		}

		if (kiblnd_post_tx_locked(conn, tx, credit))
			break;
	}
}

static void
kiblnd_tx_complete(struct kib_tx *tx, int status)
{
	int failed = (status != IB_WC_SUCCESS);
	struct kib_conn *conn = tx->tx_conn;
	int idle;

	LASSERT(tx->tx_sending > 0);

	if (failed) {
		if (conn->ibc_state == IBLND_CONN_ESTABLISHED)
			CNETERR("Tx -> %s cookie %#llx sending %d waiting %d: failed %d\n",
				libcfs_nid2str(conn->ibc_peer->ibp_nid),
				tx->tx_cookie, tx->tx_sending, tx->tx_waiting,
				status);

		kiblnd_close_conn(conn, -EIO);
	} else {
		kiblnd_peer_alive(conn->ibc_peer);
	}

	spin_lock(&conn->ibc_lock);

	/*
	 * I could be racing with rdma completion.  Whoever makes 'tx' idle
	 * gets to free it, which also drops its ref on 'conn'.
	 */
	tx->tx_sending--;
	conn->ibc_nsends_posted--;
	if (tx->tx_msg->ibm_type == IBLND_MSG_NOOP)
		conn->ibc_noops_posted--;

	if (failed) {
		tx->tx_waiting = 0;	     /* don't wait for peer */
		tx->tx_status = -EIO;
	}

	idle = !tx->tx_sending &&	 /* This is the final callback */
	       !tx->tx_waiting &&	       /* Not waiting for peer */
	       !tx->tx_queued;		  /* Not re-queued (PUT_DONE) */
	if (idle)
		list_del(&tx->tx_list);

	kiblnd_check_sends_locked(conn);
	spin_unlock(&conn->ibc_lock);

	if (idle)
		kiblnd_tx_done(conn->ibc_peer->ibp_ni, tx);
}

static void
kiblnd_init_tx_msg(lnet_ni_t *ni, struct kib_tx *tx, int type, int body_nob)
{
	struct kib_hca_dev *hdev = tx->tx_pool->tpo_hdev;
	struct ib_sge *sge = &tx->tx_sge[tx->tx_nwrq];
	struct ib_rdma_wr *wrq = &tx->tx_wrq[tx->tx_nwrq];
	int nob = offsetof(struct kib_msg, ibm_u) + body_nob;
	struct ib_mr *mr = hdev->ibh_mrs;

	LASSERT(tx->tx_nwrq >= 0);
	LASSERT(tx->tx_nwrq < IBLND_MAX_RDMA_FRAGS + 1);
	LASSERT(nob <= IBLND_MSG_SIZE);
	LASSERT(mr);

	kiblnd_init_msg(tx->tx_msg, type, body_nob);

	sge->lkey   = mr->lkey;
	sge->addr   = tx->tx_msgaddr;
	sge->length = nob;

	memset(wrq, 0, sizeof(*wrq));

	wrq->wr.next       = NULL;
	wrq->wr.wr_id      = kiblnd_ptr2wreqid(tx, IBLND_WID_TX);
	wrq->wr.sg_list    = sge;
	wrq->wr.num_sge    = 1;
	wrq->wr.opcode     = IB_WR_SEND;
	wrq->wr.send_flags = IB_SEND_SIGNALED;

	tx->tx_nwrq++;
}

static int
kiblnd_init_rdma(struct kib_conn *conn, struct kib_tx *tx, int type,
		 int resid, struct kib_rdma_desc *dstrd, __u64 dstcookie)
{
	struct kib_msg *ibmsg = tx->tx_msg;
	struct kib_rdma_desc *srcrd = tx->tx_rd;
	struct ib_sge *sge = &tx->tx_sge[0];
	struct ib_rdma_wr *wrq, *next;
	int rc  = resid;
	int srcidx = 0;
	int dstidx = 0;
	int wrknob;

	LASSERT(!in_interrupt());
	LASSERT(!tx->tx_nwrq);
	LASSERT(type == IBLND_MSG_GET_DONE ||
		type == IBLND_MSG_PUT_DONE);

	if (kiblnd_rd_size(srcrd) > conn->ibc_max_frags << PAGE_SHIFT) {
		CERROR("RDMA is too large for peer %s (%d), src size: %d dst size: %d\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid),
		       conn->ibc_max_frags << PAGE_SHIFT,
		       kiblnd_rd_size(srcrd), kiblnd_rd_size(dstrd));
		rc = -EMSGSIZE;
		goto too_big;
	}

	while (resid > 0) {
		if (srcidx >= srcrd->rd_nfrags) {
			CERROR("Src buffer exhausted: %d frags\n", srcidx);
			rc = -EPROTO;
			break;
		}

		if (dstidx == dstrd->rd_nfrags) {
			CERROR("Dst buffer exhausted: %d frags\n", dstidx);
			rc = -EPROTO;
			break;
		}

		if (tx->tx_nwrq >= IBLND_MAX_RDMA_FRAGS) {
			CERROR("RDMA has too many fragments for peer %s (%d), src idx/frags: %d/%d dst idx/frags: %d/%d\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid),
			       IBLND_MAX_RDMA_FRAGS,
			       srcidx, srcrd->rd_nfrags,
			       dstidx, dstrd->rd_nfrags);
			rc = -EMSGSIZE;
			break;
		}

		wrknob = min(min(kiblnd_rd_frag_size(srcrd, srcidx),
				 kiblnd_rd_frag_size(dstrd, dstidx)),
			     (__u32)resid);

		sge = &tx->tx_sge[tx->tx_nwrq];
		sge->addr   = kiblnd_rd_frag_addr(srcrd, srcidx);
		sge->lkey   = kiblnd_rd_frag_key(srcrd, srcidx);
		sge->length = wrknob;

		wrq = &tx->tx_wrq[tx->tx_nwrq];
		next = wrq + 1;

		wrq->wr.next       = &next->wr;
		wrq->wr.wr_id      = kiblnd_ptr2wreqid(tx, IBLND_WID_RDMA);
		wrq->wr.sg_list    = sge;
		wrq->wr.num_sge    = 1;
		wrq->wr.opcode     = IB_WR_RDMA_WRITE;
		wrq->wr.send_flags = 0;

		wrq->remote_addr = kiblnd_rd_frag_addr(dstrd, dstidx);
		wrq->rkey        = kiblnd_rd_frag_key(dstrd, dstidx);

		srcidx = kiblnd_rd_consume_frag(srcrd, srcidx, wrknob);
		dstidx = kiblnd_rd_consume_frag(dstrd, dstidx, wrknob);

		resid -= wrknob;

		tx->tx_nwrq++;
		wrq++;
		sge++;
	}
too_big:
	if (rc < 0)			     /* no RDMA if completing with failure */
		tx->tx_nwrq = 0;

	ibmsg->ibm_u.completion.ibcm_status = rc;
	ibmsg->ibm_u.completion.ibcm_cookie = dstcookie;
	kiblnd_init_tx_msg(conn->ibc_peer->ibp_ni, tx,
			   type, sizeof(struct kib_completion_msg));

	return rc;
}

static void
kiblnd_queue_tx_locked(struct kib_tx *tx, struct kib_conn *conn)
{
	struct list_head *q;

	LASSERT(tx->tx_nwrq > 0);	      /* work items set up */
	LASSERT(!tx->tx_queued);	       /* not queued for sending already */
	LASSERT(conn->ibc_state >= IBLND_CONN_ESTABLISHED);

	tx->tx_queued = 1;
	tx->tx_deadline = jiffies +
			  msecs_to_jiffies(*kiblnd_tunables.kib_timeout *
					   MSEC_PER_SEC);

	if (!tx->tx_conn) {
		kiblnd_conn_addref(conn);
		tx->tx_conn = conn;
		LASSERT(tx->tx_msg->ibm_type != IBLND_MSG_PUT_DONE);
	} else {
		/* PUT_DONE first attached to conn as a PUT_REQ */
		LASSERT(tx->tx_conn == conn);
		LASSERT(tx->tx_msg->ibm_type == IBLND_MSG_PUT_DONE);
	}

	switch (tx->tx_msg->ibm_type) {
	default:
		LBUG();

	case IBLND_MSG_PUT_REQ:
	case IBLND_MSG_GET_REQ:
		q = &conn->ibc_tx_queue_rsrvd;
		break;

	case IBLND_MSG_PUT_NAK:
	case IBLND_MSG_PUT_ACK:
	case IBLND_MSG_PUT_DONE:
	case IBLND_MSG_GET_DONE:
		q = &conn->ibc_tx_queue_nocred;
		break;

	case IBLND_MSG_NOOP:
		if (IBLND_OOB_CAPABLE(conn->ibc_version))
			q = &conn->ibc_tx_queue_nocred;
		else
			q = &conn->ibc_tx_noops;
		break;

	case IBLND_MSG_IMMEDIATE:
		q = &conn->ibc_tx_queue;
		break;
	}

	list_add_tail(&tx->tx_list, q);
}

static void
kiblnd_queue_tx(struct kib_tx *tx, struct kib_conn *conn)
{
	spin_lock(&conn->ibc_lock);
	kiblnd_queue_tx_locked(tx, conn);
	kiblnd_check_sends_locked(conn);
	spin_unlock(&conn->ibc_lock);
}

static int kiblnd_resolve_addr(struct rdma_cm_id *cmid,
			       struct sockaddr_in *srcaddr,
			       struct sockaddr_in *dstaddr,
			       int timeout_ms)
{
	unsigned short port;
	int rc;

	/* allow the port to be reused */
	rc = rdma_set_reuseaddr(cmid, 1);
	if (rc) {
		CERROR("Unable to set reuse on cmid: %d\n", rc);
		return rc;
	}

	/* look for a free privileged port */
	for (port = PROT_SOCK - 1; port > 0; port--) {
		srcaddr->sin_port = htons(port);
		rc = rdma_resolve_addr(cmid,
				       (struct sockaddr *)srcaddr,
				       (struct sockaddr *)dstaddr,
				       timeout_ms);
		if (!rc) {
			CDEBUG(D_NET, "bound to port %hu\n", port);
			return 0;
		} else if (rc == -EADDRINUSE || rc == -EADDRNOTAVAIL) {
			CDEBUG(D_NET, "bind to port %hu failed: %d\n",
			       port, rc);
		} else {
			return rc;
		}
	}

	CERROR("Failed to bind to a free privileged port\n");
	return rc;
}

static void
kiblnd_connect_peer(struct kib_peer *peer)
{
	struct rdma_cm_id *cmid;
	struct kib_dev *dev;
	struct kib_net *net = peer->ibp_ni->ni_data;
	struct sockaddr_in srcaddr;
	struct sockaddr_in dstaddr;
	int rc;

	LASSERT(net);
	LASSERT(peer->ibp_connecting > 0);
	LASSERT(!peer->ibp_reconnecting);

	cmid = kiblnd_rdma_create_id(kiblnd_cm_callback, peer, RDMA_PS_TCP,
				     IB_QPT_RC);

	if (IS_ERR(cmid)) {
		CERROR("Can't create CMID for %s: %ld\n",
		       libcfs_nid2str(peer->ibp_nid), PTR_ERR(cmid));
		rc = PTR_ERR(cmid);
		goto failed;
	}

	dev = net->ibn_dev;
	memset(&srcaddr, 0, sizeof(srcaddr));
	srcaddr.sin_family = AF_INET;
	srcaddr.sin_addr.s_addr = htonl(dev->ibd_ifip);

	memset(&dstaddr, 0, sizeof(dstaddr));
	dstaddr.sin_family = AF_INET;
	dstaddr.sin_port = htons(*kiblnd_tunables.kib_service);
	dstaddr.sin_addr.s_addr = htonl(LNET_NIDADDR(peer->ibp_nid));

	kiblnd_peer_addref(peer);	       /* cmid's ref */

	if (*kiblnd_tunables.kib_use_priv_port) {
		rc = kiblnd_resolve_addr(cmid, &srcaddr, &dstaddr,
					 *kiblnd_tunables.kib_timeout * 1000);
	} else {
		rc = rdma_resolve_addr(cmid,
				       (struct sockaddr *)&srcaddr,
				       (struct sockaddr *)&dstaddr,
				       *kiblnd_tunables.kib_timeout * 1000);
	}
	if (rc) {
		/* Can't initiate address resolution:  */
		CERROR("Can't resolve addr for %s: %d\n",
		       libcfs_nid2str(peer->ibp_nid), rc);
		goto failed2;
	}

	LASSERT(cmid->device);
	CDEBUG(D_NET, "%s: connection bound to %s:%pI4h:%s\n",
	       libcfs_nid2str(peer->ibp_nid), dev->ibd_ifname,
	       &dev->ibd_ifip, cmid->device->name);

	return;

 failed2:
	kiblnd_peer_connect_failed(peer, 1, rc);
	kiblnd_peer_decref(peer);	       /* cmid's ref */
	rdma_destroy_id(cmid);
	return;
 failed:
	kiblnd_peer_connect_failed(peer, 1, rc);
}

bool
kiblnd_reconnect_peer(struct kib_peer *peer)
{
	rwlock_t *glock = &kiblnd_data.kib_global_lock;
	char *reason = NULL;
	struct list_head txs;
	unsigned long flags;

	INIT_LIST_HEAD(&txs);

	write_lock_irqsave(glock, flags);
	if (!peer->ibp_reconnecting) {
		if (peer->ibp_accepting)
			reason = "accepting";
		else if (peer->ibp_connecting)
			reason = "connecting";
		else if (!list_empty(&peer->ibp_conns))
			reason = "connected";
		else /* connected then closed */
			reason = "closed";

		goto no_reconnect;
	}

	LASSERT(!peer->ibp_accepting && !peer->ibp_connecting &&
		list_empty(&peer->ibp_conns));
	peer->ibp_reconnecting = 0;

	if (!kiblnd_peer_active(peer)) {
		list_splice_init(&peer->ibp_tx_queue, &txs);
		reason = "unlinked";
		goto no_reconnect;
	}

	peer->ibp_connecting++;
	peer->ibp_reconnected++;
	write_unlock_irqrestore(glock, flags);

	kiblnd_connect_peer(peer);
	return true;

no_reconnect:
	write_unlock_irqrestore(glock, flags);

	CWARN("Abort reconnection of %s: %s\n",
	      libcfs_nid2str(peer->ibp_nid), reason);
	kiblnd_txlist_done(peer->ibp_ni, &txs, -ECONNABORTED);
	return false;
}

void
kiblnd_launch_tx(lnet_ni_t *ni, struct kib_tx *tx, lnet_nid_t nid)
{
	struct kib_peer *peer;
	struct kib_peer *peer2;
	struct kib_conn *conn;
	rwlock_t *g_lock = &kiblnd_data.kib_global_lock;
	unsigned long flags;
	int rc;

	/*
	 * If I get here, I've committed to send, so I complete the tx with
	 * failure on any problems
	 */
	LASSERT(!tx || !tx->tx_conn); /* only set when assigned a conn */
	LASSERT(!tx || tx->tx_nwrq > 0);     /* work items have been set up */

	/*
	 * First time, just use a read lock since I expect to find my peer
	 * connected
	 */
	read_lock_irqsave(g_lock, flags);

	peer = kiblnd_find_peer_locked(nid);
	if (peer && !list_empty(&peer->ibp_conns)) {
		/* Found a peer with an established connection */
		conn = kiblnd_get_conn_locked(peer);
		kiblnd_conn_addref(conn); /* 1 ref for me... */

		read_unlock_irqrestore(g_lock, flags);

		if (tx)
			kiblnd_queue_tx(tx, conn);
		kiblnd_conn_decref(conn); /* ...to here */
		return;
	}

	read_unlock(g_lock);
	/* Re-try with a write lock */
	write_lock(g_lock);

	peer = kiblnd_find_peer_locked(nid);
	if (peer) {
		if (list_empty(&peer->ibp_conns)) {
			/* found a peer, but it's still connecting... */
			LASSERT(kiblnd_peer_connecting(peer));
			if (tx)
				list_add_tail(&tx->tx_list,
					      &peer->ibp_tx_queue);
			write_unlock_irqrestore(g_lock, flags);
		} else {
			conn = kiblnd_get_conn_locked(peer);
			kiblnd_conn_addref(conn); /* 1 ref for me... */

			write_unlock_irqrestore(g_lock, flags);

			if (tx)
				kiblnd_queue_tx(tx, conn);
			kiblnd_conn_decref(conn); /* ...to here */
		}
		return;
	}

	write_unlock_irqrestore(g_lock, flags);

	/* Allocate a peer ready to add to the peer table and retry */
	rc = kiblnd_create_peer(ni, &peer, nid);
	if (rc) {
		CERROR("Can't create peer %s\n", libcfs_nid2str(nid));
		if (tx) {
			tx->tx_status = -EHOSTUNREACH;
			tx->tx_waiting = 0;
			kiblnd_tx_done(ni, tx);
		}
		return;
	}

	write_lock_irqsave(g_lock, flags);

	peer2 = kiblnd_find_peer_locked(nid);
	if (peer2) {
		if (list_empty(&peer2->ibp_conns)) {
			/* found a peer, but it's still connecting... */
			LASSERT(kiblnd_peer_connecting(peer2));
			if (tx)
				list_add_tail(&tx->tx_list,
					      &peer2->ibp_tx_queue);
			write_unlock_irqrestore(g_lock, flags);
		} else {
			conn = kiblnd_get_conn_locked(peer2);
			kiblnd_conn_addref(conn); /* 1 ref for me... */

			write_unlock_irqrestore(g_lock, flags);

			if (tx)
				kiblnd_queue_tx(tx, conn);
			kiblnd_conn_decref(conn); /* ...to here */
		}

		kiblnd_peer_decref(peer);
		return;
	}

	/* Brand new peer */
	LASSERT(!peer->ibp_connecting);
	peer->ibp_connecting = 1;

	/* always called with a ref on ni, which prevents ni being shutdown */
	LASSERT(!((struct kib_net *)ni->ni_data)->ibn_shutdown);

	if (tx)
		list_add_tail(&tx->tx_list, &peer->ibp_tx_queue);

	kiblnd_peer_addref(peer);
	list_add_tail(&peer->ibp_list, kiblnd_nid2peerlist(nid));

	write_unlock_irqrestore(g_lock, flags);

	kiblnd_connect_peer(peer);
	kiblnd_peer_decref(peer);
}

int
kiblnd_send(lnet_ni_t *ni, void *private, lnet_msg_t *lntmsg)
{
	lnet_hdr_t *hdr = &lntmsg->msg_hdr;
	int type = lntmsg->msg_type;
	lnet_process_id_t target = lntmsg->msg_target;
	int target_is_router = lntmsg->msg_target_is_router;
	int routing = lntmsg->msg_routing;
	unsigned int payload_niov = lntmsg->msg_niov;
	struct kvec *payload_iov = lntmsg->msg_iov;
	lnet_kiov_t *payload_kiov = lntmsg->msg_kiov;
	unsigned int payload_offset = lntmsg->msg_offset;
	unsigned int payload_nob = lntmsg->msg_len;
	struct iov_iter from;
	struct kib_msg *ibmsg;
	struct kib_rdma_desc  *rd;
	struct kib_tx *tx;
	int nob;
	int rc;

	/* NB 'private' is different depending on what we're sending.... */

	CDEBUG(D_NET, "sending %d bytes in %d frags to %s\n",
	       payload_nob, payload_niov, libcfs_id2str(target));

	LASSERT(!payload_nob || payload_niov > 0);
	LASSERT(payload_niov <= LNET_MAX_IOV);

	/* Thread context */
	LASSERT(!in_interrupt());
	/* payload is either all vaddrs or all pages */
	LASSERT(!(payload_kiov && payload_iov));

	if (payload_kiov)
		iov_iter_bvec(&from, ITER_BVEC | WRITE,
			      payload_kiov, payload_niov,
			      payload_nob + payload_offset);
	else
		iov_iter_kvec(&from, ITER_KVEC | WRITE,
			      payload_iov, payload_niov,
			      payload_nob + payload_offset);

	iov_iter_advance(&from, payload_offset);

	switch (type) {
	default:
		LBUG();
		return -EIO;

	case LNET_MSG_ACK:
		LASSERT(!payload_nob);
		break;

	case LNET_MSG_GET:
		if (routing || target_is_router)
			break;		  /* send IMMEDIATE */

		/* is the REPLY message too small for RDMA? */
		nob = offsetof(struct kib_msg, ibm_u.immediate.ibim_payload[lntmsg->msg_md->md_length]);
		if (nob <= IBLND_MSG_SIZE)
			break;		  /* send IMMEDIATE */

		tx = kiblnd_get_idle_tx(ni, target.nid);
		if (!tx) {
			CERROR("Can't allocate txd for GET to %s\n",
			       libcfs_nid2str(target.nid));
			return -ENOMEM;
		}

		ibmsg = tx->tx_msg;
		rd = &ibmsg->ibm_u.get.ibgm_rd;
		if (!(lntmsg->msg_md->md_options & LNET_MD_KIOV))
			rc = kiblnd_setup_rd_iov(ni, tx, rd,
						 lntmsg->msg_md->md_niov,
						 lntmsg->msg_md->md_iov.iov,
						 0, lntmsg->msg_md->md_length);
		else
			rc = kiblnd_setup_rd_kiov(ni, tx, rd,
						  lntmsg->msg_md->md_niov,
						  lntmsg->msg_md->md_iov.kiov,
						  0, lntmsg->msg_md->md_length);
		if (rc) {
			CERROR("Can't setup GET sink for %s: %d\n",
			       libcfs_nid2str(target.nid), rc);
			kiblnd_tx_done(ni, tx);
			return -EIO;
		}

		nob = offsetof(struct kib_get_msg, ibgm_rd.rd_frags[rd->rd_nfrags]);
		ibmsg->ibm_u.get.ibgm_cookie = tx->tx_cookie;
		ibmsg->ibm_u.get.ibgm_hdr = *hdr;

		kiblnd_init_tx_msg(ni, tx, IBLND_MSG_GET_REQ, nob);

		tx->tx_lntmsg[1] = lnet_create_reply_msg(ni, lntmsg);
		if (!tx->tx_lntmsg[1]) {
			CERROR("Can't create reply for GET -> %s\n",
			       libcfs_nid2str(target.nid));
			kiblnd_tx_done(ni, tx);
			return -EIO;
		}

		tx->tx_lntmsg[0] = lntmsg;      /* finalise lntmsg[0,1] on completion */
		tx->tx_waiting = 1;	     /* waiting for GET_DONE */
		kiblnd_launch_tx(ni, tx, target.nid);
		return 0;

	case LNET_MSG_REPLY:
	case LNET_MSG_PUT:
		/* Is the payload small enough not to need RDMA? */
		nob = offsetof(struct kib_msg, ibm_u.immediate.ibim_payload[payload_nob]);
		if (nob <= IBLND_MSG_SIZE)
			break;		  /* send IMMEDIATE */

		tx = kiblnd_get_idle_tx(ni, target.nid);
		if (!tx) {
			CERROR("Can't allocate %s txd for %s\n",
			       type == LNET_MSG_PUT ? "PUT" : "REPLY",
			       libcfs_nid2str(target.nid));
			return -ENOMEM;
		}

		if (!payload_kiov)
			rc = kiblnd_setup_rd_iov(ni, tx, tx->tx_rd,
						 payload_niov, payload_iov,
						 payload_offset, payload_nob);
		else
			rc = kiblnd_setup_rd_kiov(ni, tx, tx->tx_rd,
						  payload_niov, payload_kiov,
						  payload_offset, payload_nob);
		if (rc) {
			CERROR("Can't setup PUT src for %s: %d\n",
			       libcfs_nid2str(target.nid), rc);
			kiblnd_tx_done(ni, tx);
			return -EIO;
		}

		ibmsg = tx->tx_msg;
		ibmsg->ibm_u.putreq.ibprm_hdr = *hdr;
		ibmsg->ibm_u.putreq.ibprm_cookie = tx->tx_cookie;
		kiblnd_init_tx_msg(ni, tx, IBLND_MSG_PUT_REQ, sizeof(struct kib_putreq_msg));

		tx->tx_lntmsg[0] = lntmsg;      /* finalise lntmsg on completion */
		tx->tx_waiting = 1;	     /* waiting for PUT_{ACK,NAK} */
		kiblnd_launch_tx(ni, tx, target.nid);
		return 0;
	}

	/* send IMMEDIATE */

	LASSERT(offsetof(struct kib_msg, ibm_u.immediate.ibim_payload[payload_nob])
		 <= IBLND_MSG_SIZE);

	tx = kiblnd_get_idle_tx(ni, target.nid);
	if (!tx) {
		CERROR("Can't send %d to %s: tx descs exhausted\n",
		       type, libcfs_nid2str(target.nid));
		return -ENOMEM;
	}

	ibmsg = tx->tx_msg;
	ibmsg->ibm_u.immediate.ibim_hdr = *hdr;

	copy_from_iter(&ibmsg->ibm_u.immediate.ibim_payload, IBLND_MSG_SIZE,
		       &from);
	nob = offsetof(struct kib_immediate_msg, ibim_payload[payload_nob]);
	kiblnd_init_tx_msg(ni, tx, IBLND_MSG_IMMEDIATE, nob);

	tx->tx_lntmsg[0] = lntmsg;	      /* finalise lntmsg on completion */
	kiblnd_launch_tx(ni, tx, target.nid);
	return 0;
}

static void
kiblnd_reply(lnet_ni_t *ni, struct kib_rx *rx, lnet_msg_t *lntmsg)
{
	lnet_process_id_t target = lntmsg->msg_target;
	unsigned int niov = lntmsg->msg_niov;
	struct kvec *iov = lntmsg->msg_iov;
	lnet_kiov_t *kiov = lntmsg->msg_kiov;
	unsigned int offset = lntmsg->msg_offset;
	unsigned int nob = lntmsg->msg_len;
	struct kib_tx *tx;
	int rc;

	tx = kiblnd_get_idle_tx(ni, rx->rx_conn->ibc_peer->ibp_nid);
	if (!tx) {
		CERROR("Can't get tx for REPLY to %s\n",
		       libcfs_nid2str(target.nid));
		goto failed_0;
	}

	if (!nob)
		rc = 0;
	else if (!kiov)
		rc = kiblnd_setup_rd_iov(ni, tx, tx->tx_rd,
					 niov, iov, offset, nob);
	else
		rc = kiblnd_setup_rd_kiov(ni, tx, tx->tx_rd,
					  niov, kiov, offset, nob);

	if (rc) {
		CERROR("Can't setup GET src for %s: %d\n",
		       libcfs_nid2str(target.nid), rc);
		goto failed_1;
	}

	rc = kiblnd_init_rdma(rx->rx_conn, tx,
			      IBLND_MSG_GET_DONE, nob,
			      &rx->rx_msg->ibm_u.get.ibgm_rd,
			      rx->rx_msg->ibm_u.get.ibgm_cookie);
	if (rc < 0) {
		CERROR("Can't setup rdma for GET from %s: %d\n",
		       libcfs_nid2str(target.nid), rc);
		goto failed_1;
	}

	if (!nob) {
		/* No RDMA: local completion may happen now! */
		lnet_finalize(ni, lntmsg, 0);
	} else {
		/* RDMA: lnet_finalize(lntmsg) when it completes */
		tx->tx_lntmsg[0] = lntmsg;
	}

	kiblnd_queue_tx(tx, rx->rx_conn);
	return;

 failed_1:
	kiblnd_tx_done(ni, tx);
 failed_0:
	lnet_finalize(ni, lntmsg, -EIO);
}

int
kiblnd_recv(lnet_ni_t *ni, void *private, lnet_msg_t *lntmsg, int delayed,
	    struct iov_iter *to, unsigned int rlen)
{
	struct kib_rx *rx = private;
	struct kib_msg *rxmsg = rx->rx_msg;
	struct kib_conn *conn = rx->rx_conn;
	struct kib_tx *tx;
	int nob;
	int post_credit = IBLND_POSTRX_PEER_CREDIT;
	int rc = 0;

	LASSERT(iov_iter_count(to) <= rlen);
	LASSERT(!in_interrupt());
	/* Either all pages or all vaddrs */

	switch (rxmsg->ibm_type) {
	default:
		LBUG();

	case IBLND_MSG_IMMEDIATE:
		nob = offsetof(struct kib_msg, ibm_u.immediate.ibim_payload[rlen]);
		if (nob > rx->rx_nob) {
			CERROR("Immediate message from %s too big: %d(%d)\n",
			       libcfs_nid2str(rxmsg->ibm_u.immediate.ibim_hdr.src_nid),
			       nob, rx->rx_nob);
			rc = -EPROTO;
			break;
		}

		copy_to_iter(&rxmsg->ibm_u.immediate.ibim_payload,
			     IBLND_MSG_SIZE, to);
		lnet_finalize(ni, lntmsg, 0);
		break;

	case IBLND_MSG_PUT_REQ: {
		struct kib_msg	*txmsg;
		struct kib_rdma_desc *rd;

		if (!iov_iter_count(to)) {
			lnet_finalize(ni, lntmsg, 0);
			kiblnd_send_completion(rx->rx_conn, IBLND_MSG_PUT_NAK, 0,
					       rxmsg->ibm_u.putreq.ibprm_cookie);
			break;
		}

		tx = kiblnd_get_idle_tx(ni, conn->ibc_peer->ibp_nid);
		if (!tx) {
			CERROR("Can't allocate tx for %s\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid));
			/* Not replying will break the connection */
			rc = -ENOMEM;
			break;
		}

		txmsg = tx->tx_msg;
		rd = &txmsg->ibm_u.putack.ibpam_rd;
		if (!(to->type & ITER_BVEC))
			rc = kiblnd_setup_rd_iov(ni, tx, rd,
						 to->nr_segs, to->kvec,
						 to->iov_offset,
						 iov_iter_count(to));
		else
			rc = kiblnd_setup_rd_kiov(ni, tx, rd,
						  to->nr_segs, to->bvec,
						  to->iov_offset,
						  iov_iter_count(to));
		if (rc) {
			CERROR("Can't setup PUT sink for %s: %d\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid), rc);
			kiblnd_tx_done(ni, tx);
			/* tell peer it's over */
			kiblnd_send_completion(rx->rx_conn, IBLND_MSG_PUT_NAK, rc,
					       rxmsg->ibm_u.putreq.ibprm_cookie);
			break;
		}

		nob = offsetof(struct kib_putack_msg, ibpam_rd.rd_frags[rd->rd_nfrags]);
		txmsg->ibm_u.putack.ibpam_src_cookie = rxmsg->ibm_u.putreq.ibprm_cookie;
		txmsg->ibm_u.putack.ibpam_dst_cookie = tx->tx_cookie;

		kiblnd_init_tx_msg(ni, tx, IBLND_MSG_PUT_ACK, nob);

		tx->tx_lntmsg[0] = lntmsg;      /* finalise lntmsg on completion */
		tx->tx_waiting = 1;	     /* waiting for PUT_DONE */
		kiblnd_queue_tx(tx, conn);

		/* reposted buffer reserved for PUT_DONE */
		post_credit = IBLND_POSTRX_NO_CREDIT;
		break;
		}

	case IBLND_MSG_GET_REQ:
		if (lntmsg) {
			/* Optimized GET; RDMA lntmsg's payload */
			kiblnd_reply(ni, rx, lntmsg);
		} else {
			/* GET didn't match anything */
			kiblnd_send_completion(rx->rx_conn, IBLND_MSG_GET_DONE,
					       -ENODATA,
					       rxmsg->ibm_u.get.ibgm_cookie);
		}
		break;
	}

	kiblnd_post_rx(rx, post_credit);
	return rc;
}

int
kiblnd_thread_start(int (*fn)(void *arg), void *arg, char *name)
{
	struct task_struct *task = kthread_run(fn, arg, "%s", name);

	if (IS_ERR(task))
		return PTR_ERR(task);

	atomic_inc(&kiblnd_data.kib_nthreads);
	return 0;
}

static void
kiblnd_thread_fini(void)
{
	atomic_dec(&kiblnd_data.kib_nthreads);
}

static void
kiblnd_peer_alive(struct kib_peer *peer)
{
	/* This is racy, but everyone's only writing cfs_time_current() */
	peer->ibp_last_alive = cfs_time_current();
	mb();
}

static void
kiblnd_peer_notify(struct kib_peer *peer)
{
	int error = 0;
	unsigned long last_alive = 0;
	unsigned long flags;

	read_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	if (kiblnd_peer_idle(peer) && peer->ibp_error) {
		error = peer->ibp_error;
		peer->ibp_error = 0;

		last_alive = peer->ibp_last_alive;
	}

	read_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	if (error)
		lnet_notify(peer->ibp_ni,
			    peer->ibp_nid, 0, last_alive);
}

void
kiblnd_close_conn_locked(struct kib_conn *conn, int error)
{
	/*
	 * This just does the immediate housekeeping. 'error' is zero for a
	 * normal shutdown which can happen only after the connection has been
	 * established.  If the connection is established, schedule the
	 * connection to be finished off by the connd. Otherwise the connd is
	 * already dealing with it (either to set it up or tear it down).
	 * Caller holds kib_global_lock exclusively in irq context
	 */
	struct kib_peer *peer = conn->ibc_peer;
	struct kib_dev *dev;
	unsigned long flags;

	LASSERT(error || conn->ibc_state >= IBLND_CONN_ESTABLISHED);

	if (error && !conn->ibc_comms_error)
		conn->ibc_comms_error = error;

	if (conn->ibc_state != IBLND_CONN_ESTABLISHED)
		return; /* already being handled  */

	if (!error &&
	    list_empty(&conn->ibc_tx_noops) &&
	    list_empty(&conn->ibc_tx_queue) &&
	    list_empty(&conn->ibc_tx_queue_rsrvd) &&
	    list_empty(&conn->ibc_tx_queue_nocred) &&
	    list_empty(&conn->ibc_active_txs)) {
		CDEBUG(D_NET, "closing conn to %s\n",
		       libcfs_nid2str(peer->ibp_nid));
	} else {
		CNETERR("Closing conn to %s: error %d%s%s%s%s%s\n",
			libcfs_nid2str(peer->ibp_nid), error,
			list_empty(&conn->ibc_tx_queue) ? "" : "(sending)",
			list_empty(&conn->ibc_tx_noops) ? "" : "(sending_noops)",
			list_empty(&conn->ibc_tx_queue_rsrvd) ? "" : "(sending_rsrvd)",
			list_empty(&conn->ibc_tx_queue_nocred) ? "" : "(sending_nocred)",
			list_empty(&conn->ibc_active_txs) ? "" : "(waiting)");
	}

	dev = ((struct kib_net *)peer->ibp_ni->ni_data)->ibn_dev;
	list_del(&conn->ibc_list);
	/* connd (see below) takes over ibc_list's ref */

	if (list_empty(&peer->ibp_conns) &&    /* no more conns */
	    kiblnd_peer_active(peer)) {	 /* still in peer table */
		kiblnd_unlink_peer_locked(peer);

		/* set/clear error on last conn */
		peer->ibp_error = conn->ibc_comms_error;
	}

	kiblnd_set_conn_state(conn, IBLND_CONN_CLOSING);

	if (error &&
	    kiblnd_dev_can_failover(dev)) {
		list_add_tail(&dev->ibd_fail_list,
			      &kiblnd_data.kib_failed_devs);
		wake_up(&kiblnd_data.kib_failover_waitq);
	}

	spin_lock_irqsave(&kiblnd_data.kib_connd_lock, flags);

	list_add_tail(&conn->ibc_list, &kiblnd_data.kib_connd_conns);
	wake_up(&kiblnd_data.kib_connd_waitq);

	spin_unlock_irqrestore(&kiblnd_data.kib_connd_lock, flags);
}

void
kiblnd_close_conn(struct kib_conn *conn, int error)
{
	unsigned long flags;

	write_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	kiblnd_close_conn_locked(conn, error);

	write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);
}

static void
kiblnd_handle_early_rxs(struct kib_conn *conn)
{
	unsigned long flags;
	struct kib_rx *rx;
	struct kib_rx *tmp;

	LASSERT(!in_interrupt());
	LASSERT(conn->ibc_state >= IBLND_CONN_ESTABLISHED);

	write_lock_irqsave(&kiblnd_data.kib_global_lock, flags);
	list_for_each_entry_safe(rx, tmp, &conn->ibc_early_rxs, rx_list) {
		list_del(&rx->rx_list);
		write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

		kiblnd_handle_rx(rx);

		write_lock_irqsave(&kiblnd_data.kib_global_lock, flags);
	}
	write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);
}

static void
kiblnd_abort_txs(struct kib_conn *conn, struct list_head *txs)
{
	LIST_HEAD(zombies);
	struct list_head *tmp;
	struct list_head *nxt;
	struct kib_tx *tx;

	spin_lock(&conn->ibc_lock);

	list_for_each_safe(tmp, nxt, txs) {
		tx = list_entry(tmp, struct kib_tx, tx_list);

		if (txs == &conn->ibc_active_txs) {
			LASSERT(!tx->tx_queued);
			LASSERT(tx->tx_waiting || tx->tx_sending);
		} else {
			LASSERT(tx->tx_queued);
		}

		tx->tx_status = -ECONNABORTED;
		tx->tx_waiting = 0;

		if (!tx->tx_sending) {
			tx->tx_queued = 0;
			list_del(&tx->tx_list);
			list_add(&tx->tx_list, &zombies);
		}
	}

	spin_unlock(&conn->ibc_lock);

	kiblnd_txlist_done(conn->ibc_peer->ibp_ni, &zombies, -ECONNABORTED);
}

static void
kiblnd_finalise_conn(struct kib_conn *conn)
{
	LASSERT(!in_interrupt());
	LASSERT(conn->ibc_state > IBLND_CONN_INIT);

	kiblnd_set_conn_state(conn, IBLND_CONN_DISCONNECTED);

	/*
	 * abort_receives moves QP state to IB_QPS_ERR.  This is only required
	 * for connections that didn't get as far as being connected, because
	 * rdma_disconnect() does this for free.
	 */
	kiblnd_abort_receives(conn);

	/*
	 * Complete all tx descs not waiting for sends to complete.
	 * NB we should be safe from RDMA now that the QP has changed state
	 */
	kiblnd_abort_txs(conn, &conn->ibc_tx_noops);
	kiblnd_abort_txs(conn, &conn->ibc_tx_queue);
	kiblnd_abort_txs(conn, &conn->ibc_tx_queue_rsrvd);
	kiblnd_abort_txs(conn, &conn->ibc_tx_queue_nocred);
	kiblnd_abort_txs(conn, &conn->ibc_active_txs);

	kiblnd_handle_early_rxs(conn);
}

static void
kiblnd_peer_connect_failed(struct kib_peer *peer, int active, int error)
{
	LIST_HEAD(zombies);
	unsigned long flags;

	LASSERT(error);
	LASSERT(!in_interrupt());

	write_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	if (active) {
		LASSERT(peer->ibp_connecting > 0);
		peer->ibp_connecting--;
	} else {
		LASSERT(peer->ibp_accepting > 0);
		peer->ibp_accepting--;
	}

	if (kiblnd_peer_connecting(peer)) {
		/* another connection attempt under way... */
		write_unlock_irqrestore(&kiblnd_data.kib_global_lock,
					flags);
		return;
	}

	peer->ibp_reconnected = 0;
	if (list_empty(&peer->ibp_conns)) {
		/* Take peer's blocked transmits to complete with error */
		list_add(&zombies, &peer->ibp_tx_queue);
		list_del_init(&peer->ibp_tx_queue);

		if (kiblnd_peer_active(peer))
			kiblnd_unlink_peer_locked(peer);

		peer->ibp_error = error;
	} else {
		/* Can't have blocked transmits if there are connections */
		LASSERT(list_empty(&peer->ibp_tx_queue));
	}

	write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	kiblnd_peer_notify(peer);

	if (list_empty(&zombies))
		return;

	CNETERR("Deleting messages for %s: connection failed\n",
		libcfs_nid2str(peer->ibp_nid));

	kiblnd_txlist_done(peer->ibp_ni, &zombies, -EHOSTUNREACH);
}

static void
kiblnd_connreq_done(struct kib_conn *conn, int status)
{
	struct kib_peer *peer = conn->ibc_peer;
	struct kib_tx *tx;
	struct kib_tx *tmp;
	struct list_head txs;
	unsigned long flags;
	int active;

	active = (conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT);

	CDEBUG(D_NET, "%s: active(%d), version(%x), status(%d)\n",
	       libcfs_nid2str(peer->ibp_nid), active,
	       conn->ibc_version, status);

	LASSERT(!in_interrupt());
	LASSERT((conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT &&
		 peer->ibp_connecting > 0) ||
		 (conn->ibc_state == IBLND_CONN_PASSIVE_WAIT &&
		 peer->ibp_accepting > 0));

	LIBCFS_FREE(conn->ibc_connvars, sizeof(*conn->ibc_connvars));
	conn->ibc_connvars = NULL;

	if (status) {
		/* failed to establish connection */
		kiblnd_peer_connect_failed(peer, active, status);
		kiblnd_finalise_conn(conn);
		return;
	}

	/* connection established */
	write_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	conn->ibc_last_send = jiffies;
	kiblnd_set_conn_state(conn, IBLND_CONN_ESTABLISHED);
	kiblnd_peer_alive(peer);

	/*
	 * Add conn to peer's list and nuke any dangling conns from a different
	 * peer instance...
	 */
	kiblnd_conn_addref(conn);	       /* +1 ref for ibc_list */
	list_add(&conn->ibc_list, &peer->ibp_conns);
	peer->ibp_reconnected = 0;
	if (active)
		peer->ibp_connecting--;
	else
		peer->ibp_accepting--;

	if (!peer->ibp_version) {
		peer->ibp_version     = conn->ibc_version;
		peer->ibp_incarnation = conn->ibc_incarnation;
	}

	if (peer->ibp_version     != conn->ibc_version ||
	    peer->ibp_incarnation != conn->ibc_incarnation) {
		kiblnd_close_stale_conns_locked(peer, conn->ibc_version,
						conn->ibc_incarnation);
		peer->ibp_version     = conn->ibc_version;
		peer->ibp_incarnation = conn->ibc_incarnation;
	}

	/* grab pending txs while I have the lock */
	list_add(&txs, &peer->ibp_tx_queue);
	list_del_init(&peer->ibp_tx_queue);

	if (!kiblnd_peer_active(peer) ||	/* peer has been deleted */
	    conn->ibc_comms_error) {       /* error has happened already */
		lnet_ni_t *ni = peer->ibp_ni;

		/* start to shut down connection */
		kiblnd_close_conn_locked(conn, -ECONNABORTED);
		write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

		kiblnd_txlist_done(ni, &txs, -ECONNABORTED);

		return;
	}

	/*
	 * +1 ref for myself, this connection is visible to other threads
	 * now, refcount of peer:ibp_conns can be released by connection
	 * close from either a different thread, or the calling of
	 * kiblnd_check_sends_locked() below. See bz21911 for details.
	 */
	kiblnd_conn_addref(conn);
	write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	/* Schedule blocked txs */
	spin_lock(&conn->ibc_lock);
	list_for_each_entry_safe(tx, tmp, &txs, tx_list) {
		list_del(&tx->tx_list);

		kiblnd_queue_tx_locked(tx, conn);
	}
	kiblnd_check_sends_locked(conn);
	spin_unlock(&conn->ibc_lock);

	/* schedule blocked rxs */
	kiblnd_handle_early_rxs(conn);

	kiblnd_conn_decref(conn);
}

static void
kiblnd_reject(struct rdma_cm_id *cmid, struct kib_rej *rej)
{
	int rc;

	rc = rdma_reject(cmid, rej, sizeof(*rej));

	if (rc)
		CWARN("Error %d sending reject\n", rc);
}

static int
kiblnd_passive_connect(struct rdma_cm_id *cmid, void *priv, int priv_nob)
{
	rwlock_t *g_lock = &kiblnd_data.kib_global_lock;
	struct kib_msg *reqmsg = priv;
	struct kib_msg *ackmsg;
	struct kib_dev *ibdev;
	struct kib_peer *peer;
	struct kib_peer *peer2;
	struct kib_conn *conn;
	lnet_ni_t *ni  = NULL;
	struct kib_net *net = NULL;
	lnet_nid_t nid;
	struct rdma_conn_param cp;
	struct kib_rej rej;
	int version = IBLND_MSG_VERSION;
	unsigned long flags;
	int max_frags;
	int rc;
	struct sockaddr_in *peer_addr;

	LASSERT(!in_interrupt());

	/* cmid inherits 'context' from the corresponding listener id */
	ibdev = (struct kib_dev *)cmid->context;
	LASSERT(ibdev);

	memset(&rej, 0, sizeof(rej));
	rej.ibr_magic = IBLND_MSG_MAGIC;
	rej.ibr_why = IBLND_REJECT_FATAL;
	rej.ibr_cp.ibcp_max_msg_size = IBLND_MSG_SIZE;

	peer_addr = (struct sockaddr_in *)&cmid->route.addr.dst_addr;
	if (*kiblnd_tunables.kib_require_priv_port &&
	    ntohs(peer_addr->sin_port) >= PROT_SOCK) {
		__u32 ip = ntohl(peer_addr->sin_addr.s_addr);

		CERROR("Peer's port (%pI4h:%hu) is not privileged\n",
		       &ip, ntohs(peer_addr->sin_port));
		goto failed;
	}

	if (priv_nob < offsetof(struct kib_msg, ibm_type)) {
		CERROR("Short connection request\n");
		goto failed;
	}

	/*
	 * Future protocol version compatibility support!  If the
	 * o2iblnd-specific protocol changes, or when LNET unifies
	 * protocols over all LNDs, the initial connection will
	 * negotiate a protocol version.  I trap this here to avoid
	 * console errors; the reject tells the peer which protocol I
	 * speak.
	 */
	if (reqmsg->ibm_magic == LNET_PROTO_MAGIC ||
	    reqmsg->ibm_magic == __swab32(LNET_PROTO_MAGIC))
		goto failed;
	if (reqmsg->ibm_magic == IBLND_MSG_MAGIC &&
	    reqmsg->ibm_version != IBLND_MSG_VERSION &&
	    reqmsg->ibm_version != IBLND_MSG_VERSION_1)
		goto failed;
	if (reqmsg->ibm_magic == __swab32(IBLND_MSG_MAGIC) &&
	    reqmsg->ibm_version != __swab16(IBLND_MSG_VERSION) &&
	    reqmsg->ibm_version != __swab16(IBLND_MSG_VERSION_1))
		goto failed;

	rc = kiblnd_unpack_msg(reqmsg, priv_nob);
	if (rc) {
		CERROR("Can't parse connection request: %d\n", rc);
		goto failed;
	}

	nid = reqmsg->ibm_srcnid;
	ni = lnet_net2ni(LNET_NIDNET(reqmsg->ibm_dstnid));

	if (ni) {
		net = (struct kib_net *)ni->ni_data;
		rej.ibr_incarnation = net->ibn_incarnation;
	}

	if (!ni ||			 /* no matching net */
	    ni->ni_nid != reqmsg->ibm_dstnid ||   /* right NET, wrong NID! */
	    net->ibn_dev != ibdev) {	      /* wrong device */
		CERROR("Can't accept conn from %s on %s (%s:%d:%pI4h): bad dst nid %s\n",
		       libcfs_nid2str(nid),
		       !ni ? "NA" : libcfs_nid2str(ni->ni_nid),
		       ibdev->ibd_ifname, ibdev->ibd_nnets,
		       &ibdev->ibd_ifip,
		       libcfs_nid2str(reqmsg->ibm_dstnid));

		goto failed;
	}

       /* check time stamp as soon as possible */
	if (reqmsg->ibm_dststamp &&
	    reqmsg->ibm_dststamp != net->ibn_incarnation) {
		CWARN("Stale connection request\n");
		rej.ibr_why = IBLND_REJECT_CONN_STALE;
		goto failed;
	}

	/* I can accept peer's version */
	version = reqmsg->ibm_version;

	if (reqmsg->ibm_type != IBLND_MSG_CONNREQ) {
		CERROR("Unexpected connreq msg type: %x from %s\n",
		       reqmsg->ibm_type, libcfs_nid2str(nid));
		goto failed;
	}

	if (reqmsg->ibm_u.connparams.ibcp_queue_depth >
	    kiblnd_msg_queue_size(version, ni)) {
		CERROR("Can't accept conn from %s, queue depth too large: %d (<=%d wanted)\n",
		       libcfs_nid2str(nid),
		       reqmsg->ibm_u.connparams.ibcp_queue_depth,
		       kiblnd_msg_queue_size(version, ni));

		if (version == IBLND_MSG_VERSION)
			rej.ibr_why = IBLND_REJECT_MSG_QUEUE_SIZE;

		goto failed;
	}

	max_frags = reqmsg->ibm_u.connparams.ibcp_max_frags >> IBLND_FRAG_SHIFT;
	if (max_frags > kiblnd_rdma_frags(version, ni)) {
		CWARN("Can't accept conn from %s (version %x): max message size %d is too large (%d wanted)\n",
		      libcfs_nid2str(nid), version, max_frags,
		      kiblnd_rdma_frags(version, ni));

		if (version >= IBLND_MSG_VERSION)
			rej.ibr_why = IBLND_REJECT_RDMA_FRAGS;

		goto failed;
	} else if (max_frags < kiblnd_rdma_frags(version, ni) &&
		   !net->ibn_fmr_ps) {
		CWARN("Can't accept conn from %s (version %x): max message size %d incompatible without FMR pool (%d wanted)\n",
		      libcfs_nid2str(nid), version, max_frags,
		      kiblnd_rdma_frags(version, ni));

		if (version == IBLND_MSG_VERSION)
			rej.ibr_why = IBLND_REJECT_RDMA_FRAGS;

		goto failed;
	}

	if (reqmsg->ibm_u.connparams.ibcp_max_msg_size > IBLND_MSG_SIZE) {
		CERROR("Can't accept %s: message size %d too big (%d max)\n",
		       libcfs_nid2str(nid),
		       reqmsg->ibm_u.connparams.ibcp_max_msg_size,
		       IBLND_MSG_SIZE);
		goto failed;
	}

	/* assume 'nid' is a new peer; create  */
	rc = kiblnd_create_peer(ni, &peer, nid);
	if (rc) {
		CERROR("Can't create peer for %s\n", libcfs_nid2str(nid));
		rej.ibr_why = IBLND_REJECT_NO_RESOURCES;
		goto failed;
	}

	/* We have validated the peer's parameters so use those */
	peer->ibp_max_frags = max_frags;
	peer->ibp_queue_depth = reqmsg->ibm_u.connparams.ibcp_queue_depth;

	write_lock_irqsave(g_lock, flags);

	peer2 = kiblnd_find_peer_locked(nid);
	if (peer2) {
		if (!peer2->ibp_version) {
			peer2->ibp_version     = version;
			peer2->ibp_incarnation = reqmsg->ibm_srcstamp;
		}

		/* not the guy I've talked with */
		if (peer2->ibp_incarnation != reqmsg->ibm_srcstamp ||
		    peer2->ibp_version     != version) {
			kiblnd_close_peer_conns_locked(peer2, -ESTALE);

			if (kiblnd_peer_active(peer2)) {
				peer2->ibp_incarnation = reqmsg->ibm_srcstamp;
				peer2->ibp_version = version;
			}
			write_unlock_irqrestore(g_lock, flags);

			CWARN("Conn stale %s version %x/%x incarnation %llu/%llu\n",
			      libcfs_nid2str(nid), peer2->ibp_version, version,
			      peer2->ibp_incarnation, reqmsg->ibm_srcstamp);

			kiblnd_peer_decref(peer);
			rej.ibr_why = IBLND_REJECT_CONN_STALE;
			goto failed;
		}

		/*
		 * Tie-break connection race in favour of the higher NID.
		 * If we keep running into a race condition multiple times,
		 * we have to assume that the connection attempt with the
		 * higher NID is stuck in a connecting state and will never
		 * recover.  As such, we pass through this if-block and let
		 * the lower NID connection win so we can move forward.
		 */
		if (peer2->ibp_connecting &&
		    nid < ni->ni_nid && peer2->ibp_races <
		    MAX_CONN_RACES_BEFORE_ABORT) {
			peer2->ibp_races++;
			write_unlock_irqrestore(g_lock, flags);

			CDEBUG(D_NET, "Conn race %s\n",
			       libcfs_nid2str(peer2->ibp_nid));

			kiblnd_peer_decref(peer);
			rej.ibr_why = IBLND_REJECT_CONN_RACE;
			goto failed;
		}
		if (peer2->ibp_races >= MAX_CONN_RACES_BEFORE_ABORT)
			CNETERR("Conn race %s: unresolved after %d attempts, letting lower NID win\n",
				libcfs_nid2str(peer2->ibp_nid),
				MAX_CONN_RACES_BEFORE_ABORT);
		/**
		 * passive connection is allowed even this peer is waiting for
		 * reconnection.
		 */
		peer2->ibp_reconnecting = 0;
		peer2->ibp_races = 0;
		peer2->ibp_accepting++;
		kiblnd_peer_addref(peer2);

		/**
		 * Race with kiblnd_launch_tx (active connect) to create peer
		 * so copy validated parameters since we now know what the
		 * peer's limits are
		 */
		peer2->ibp_max_frags = peer->ibp_max_frags;
		peer2->ibp_queue_depth = peer->ibp_queue_depth;

		write_unlock_irqrestore(g_lock, flags);
		kiblnd_peer_decref(peer);
		peer = peer2;
	} else {
		/* Brand new peer */
		LASSERT(!peer->ibp_accepting);
		LASSERT(!peer->ibp_version &&
			!peer->ibp_incarnation);

		peer->ibp_accepting   = 1;
		peer->ibp_version     = version;
		peer->ibp_incarnation = reqmsg->ibm_srcstamp;

		/* I have a ref on ni that prevents it being shutdown */
		LASSERT(!net->ibn_shutdown);

		kiblnd_peer_addref(peer);
		list_add_tail(&peer->ibp_list, kiblnd_nid2peerlist(nid));

		write_unlock_irqrestore(g_lock, flags);
	}

	conn = kiblnd_create_conn(peer, cmid, IBLND_CONN_PASSIVE_WAIT,
				  version);
	if (!conn) {
		kiblnd_peer_connect_failed(peer, 0, -ENOMEM);
		kiblnd_peer_decref(peer);
		rej.ibr_why = IBLND_REJECT_NO_RESOURCES;
		goto failed;
	}

	/*
	 * conn now "owns" cmid, so I return success from here on to ensure the
	 * CM callback doesn't destroy cmid.
	 */
	conn->ibc_incarnation      = reqmsg->ibm_srcstamp;
	conn->ibc_credits          = conn->ibc_queue_depth;
	conn->ibc_reserved_credits = conn->ibc_queue_depth;
	LASSERT(conn->ibc_credits + conn->ibc_reserved_credits +
		IBLND_OOB_MSGS(version) <= IBLND_RX_MSGS(conn));

	ackmsg = &conn->ibc_connvars->cv_msg;
	memset(ackmsg, 0, sizeof(*ackmsg));

	kiblnd_init_msg(ackmsg, IBLND_MSG_CONNACK,
			sizeof(ackmsg->ibm_u.connparams));
	ackmsg->ibm_u.connparams.ibcp_queue_depth = conn->ibc_queue_depth;
	ackmsg->ibm_u.connparams.ibcp_max_frags = conn->ibc_max_frags << IBLND_FRAG_SHIFT;
	ackmsg->ibm_u.connparams.ibcp_max_msg_size = IBLND_MSG_SIZE;

	kiblnd_pack_msg(ni, ackmsg, version, 0, nid, reqmsg->ibm_srcstamp);

	memset(&cp, 0, sizeof(cp));
	cp.private_data	= ackmsg;
	cp.private_data_len = ackmsg->ibm_nob;
	cp.responder_resources = 0;	     /* No atomic ops or RDMA reads */
	cp.initiator_depth = 0;
	cp.flow_control	= 1;
	cp.retry_count = *kiblnd_tunables.kib_retry_count;
	cp.rnr_retry_count = *kiblnd_tunables.kib_rnr_retry_count;

	CDEBUG(D_NET, "Accept %s\n", libcfs_nid2str(nid));

	rc = rdma_accept(cmid, &cp);
	if (rc) {
		CERROR("Can't accept %s: %d\n", libcfs_nid2str(nid), rc);
		rej.ibr_version = version;
		rej.ibr_why     = IBLND_REJECT_FATAL;

		kiblnd_reject(cmid, &rej);
		kiblnd_connreq_done(conn, rc);
		kiblnd_conn_decref(conn);
	}

	lnet_ni_decref(ni);
	return 0;

 failed:
	if (ni) {
		rej.ibr_cp.ibcp_queue_depth = kiblnd_msg_queue_size(version, ni);
		rej.ibr_cp.ibcp_max_frags = kiblnd_rdma_frags(version, ni);
		lnet_ni_decref(ni);
	}

	rej.ibr_version             = version;
	kiblnd_reject(cmid, &rej);

	return -ECONNREFUSED;
}

static void
kiblnd_check_reconnect(struct kib_conn *conn, int version,
		       __u64 incarnation, int why, struct kib_connparams *cp)
{
	rwlock_t *glock = &kiblnd_data.kib_global_lock;
	struct kib_peer *peer = conn->ibc_peer;
	char *reason;
	int msg_size = IBLND_MSG_SIZE;
	int frag_num = -1;
	int queue_dep = -1;
	bool reconnect;
	unsigned long flags;

	LASSERT(conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT);
	LASSERT(peer->ibp_connecting > 0);     /* 'conn' at least */
	LASSERT(!peer->ibp_reconnecting);

	if (cp) {
		msg_size = cp->ibcp_max_msg_size;
		frag_num	= cp->ibcp_max_frags << IBLND_FRAG_SHIFT;
		queue_dep = cp->ibcp_queue_depth;
	}

	write_lock_irqsave(glock, flags);
	/**
	 * retry connection if it's still needed and no other connection
	 * attempts (active or passive) are in progress
	 * NB: reconnect is still needed even when ibp_tx_queue is
	 * empty if ibp_version != version because reconnect may be
	 * initiated by kiblnd_query()
	 */
	reconnect = (!list_empty(&peer->ibp_tx_queue) ||
		     peer->ibp_version != version) &&
		    peer->ibp_connecting == 1 &&
		    !peer->ibp_accepting;
	if (!reconnect) {
		reason = "no need";
		goto out;
	}

	switch (why) {
	default:
		reason = "Unknown";
		break;

	case IBLND_REJECT_RDMA_FRAGS: {
		struct lnet_ioctl_config_lnd_tunables *tunables;

		if (!cp) {
			reason = "can't negotiate max frags";
			goto out;
		}
		tunables = peer->ibp_ni->ni_lnd_tunables;
		if (!tunables->lt_tun_u.lt_o2ib.lnd_map_on_demand) {
			reason = "map_on_demand must be enabled";
			goto out;
		}
		if (conn->ibc_max_frags <= frag_num) {
			reason = "unsupported max frags";
			goto out;
		}

		peer->ibp_max_frags = frag_num;
		reason = "rdma fragments";
		break;
	}
	case IBLND_REJECT_MSG_QUEUE_SIZE:
		if (!cp) {
			reason = "can't negotiate queue depth";
			goto out;
		}
		if (conn->ibc_queue_depth <= queue_dep) {
			reason = "unsupported queue depth";
			goto out;
		}

		peer->ibp_queue_depth = queue_dep;
		reason = "queue depth";
		break;

	case IBLND_REJECT_CONN_STALE:
		reason = "stale";
		break;

	case IBLND_REJECT_CONN_RACE:
		reason = "conn race";
		break;

	case IBLND_REJECT_CONN_UNCOMPAT:
		reason = "version negotiation";
		break;
	}

	conn->ibc_reconnect = 1;
	peer->ibp_reconnecting = 1;
	peer->ibp_version = version;
	if (incarnation)
		peer->ibp_incarnation = incarnation;
out:
	write_unlock_irqrestore(glock, flags);

	CNETERR("%s: %s (%s), %x, %x, msg_size: %d, queue_depth: %d/%d, max_frags: %d/%d\n",
		libcfs_nid2str(peer->ibp_nid),
		reconnect ? "reconnect" : "don't reconnect",
		reason, IBLND_MSG_VERSION, version, msg_size,
		conn->ibc_queue_depth, queue_dep,
		conn->ibc_max_frags, frag_num);
	/**
	 * if conn::ibc_reconnect is TRUE, connd will reconnect to the peer
	 * while destroying the zombie
	 */
}

static void
kiblnd_rejected(struct kib_conn *conn, int reason, void *priv, int priv_nob)
{
	struct kib_peer *peer = conn->ibc_peer;

	LASSERT(!in_interrupt());
	LASSERT(conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT);

	switch (reason) {
	case IB_CM_REJ_STALE_CONN:
		kiblnd_check_reconnect(conn, IBLND_MSG_VERSION, 0,
				       IBLND_REJECT_CONN_STALE, NULL);
		break;

	case IB_CM_REJ_INVALID_SERVICE_ID:
		CNETERR("%s rejected: no listener at %d\n",
			libcfs_nid2str(peer->ibp_nid),
			*kiblnd_tunables.kib_service);
		break;

	case IB_CM_REJ_CONSUMER_DEFINED:
		if (priv_nob >= offsetof(struct kib_rej, ibr_padding)) {
			struct kib_rej *rej = priv;
			struct kib_connparams *cp = NULL;
			int flip = 0;
			__u64 incarnation = -1;

			/* NB. default incarnation is -1 because:
			 * a) V1 will ignore dst incarnation in connreq.
			 * b) V2 will provide incarnation while rejecting me,
			 *    -1 will be overwrote.
			 *
			 * if I try to connect to a V1 peer with V2 protocol,
			 * it rejected me then upgrade to V2, I have no idea
			 * about the upgrading and try to reconnect with V1,
			 * in this case upgraded V2 can find out I'm trying to
			 * talk to the old guy and reject me(incarnation is -1).
			 */

			if (rej->ibr_magic == __swab32(IBLND_MSG_MAGIC) ||
			    rej->ibr_magic == __swab32(LNET_PROTO_MAGIC)) {
				__swab32s(&rej->ibr_magic);
				__swab16s(&rej->ibr_version);
				flip = 1;
			}

			if (priv_nob >= sizeof(struct kib_rej) &&
			    rej->ibr_version > IBLND_MSG_VERSION_1) {
				/*
				 * priv_nob is always 148 in current version
				 * of OFED, so we still need to check version.
				 * (define of IB_CM_REJ_PRIVATE_DATA_SIZE)
				 */
				cp = &rej->ibr_cp;

				if (flip) {
					__swab64s(&rej->ibr_incarnation);
					__swab16s(&cp->ibcp_queue_depth);
					__swab16s(&cp->ibcp_max_frags);
					__swab32s(&cp->ibcp_max_msg_size);
				}

				incarnation = rej->ibr_incarnation;
			}

			if (rej->ibr_magic != IBLND_MSG_MAGIC &&
			    rej->ibr_magic != LNET_PROTO_MAGIC) {
				CERROR("%s rejected: consumer defined fatal error\n",
				       libcfs_nid2str(peer->ibp_nid));
				break;
			}

			if (rej->ibr_version != IBLND_MSG_VERSION &&
			    rej->ibr_version != IBLND_MSG_VERSION_1) {
				CERROR("%s rejected: o2iblnd version %x error\n",
				       libcfs_nid2str(peer->ibp_nid),
				       rej->ibr_version);
				break;
			}

			if (rej->ibr_why     == IBLND_REJECT_FATAL &&
			    rej->ibr_version == IBLND_MSG_VERSION_1) {
				CDEBUG(D_NET, "rejected by old version peer %s: %x\n",
				       libcfs_nid2str(peer->ibp_nid), rej->ibr_version);

				if (conn->ibc_version != IBLND_MSG_VERSION_1)
					rej->ibr_why = IBLND_REJECT_CONN_UNCOMPAT;
			}

			switch (rej->ibr_why) {
			case IBLND_REJECT_CONN_RACE:
			case IBLND_REJECT_CONN_STALE:
			case IBLND_REJECT_CONN_UNCOMPAT:
			case IBLND_REJECT_MSG_QUEUE_SIZE:
			case IBLND_REJECT_RDMA_FRAGS:
				kiblnd_check_reconnect(conn, rej->ibr_version,
						       incarnation,
						       rej->ibr_why, cp);
				break;

			case IBLND_REJECT_NO_RESOURCES:
				CERROR("%s rejected: o2iblnd no resources\n",
				       libcfs_nid2str(peer->ibp_nid));
				break;

			case IBLND_REJECT_FATAL:
				CERROR("%s rejected: o2iblnd fatal error\n",
				       libcfs_nid2str(peer->ibp_nid));
				break;

			default:
				CERROR("%s rejected: o2iblnd reason %d\n",
				       libcfs_nid2str(peer->ibp_nid),
				       rej->ibr_why);
				break;
			}
			break;
		}
		/* fall through */
	default:
		CNETERR("%s rejected: reason %d, size %d\n",
			libcfs_nid2str(peer->ibp_nid), reason, priv_nob);
		break;
	}

	kiblnd_connreq_done(conn, -ECONNREFUSED);
}

static void
kiblnd_check_connreply(struct kib_conn *conn, void *priv, int priv_nob)
{
	struct kib_peer *peer = conn->ibc_peer;
	lnet_ni_t *ni = peer->ibp_ni;
	struct kib_net *net = ni->ni_data;
	struct kib_msg *msg = priv;
	int ver = conn->ibc_version;
	int rc = kiblnd_unpack_msg(msg, priv_nob);
	unsigned long flags;

	LASSERT(net);

	if (rc) {
		CERROR("Can't unpack connack from %s: %d\n",
		       libcfs_nid2str(peer->ibp_nid), rc);
		goto failed;
	}

	if (msg->ibm_type != IBLND_MSG_CONNACK) {
		CERROR("Unexpected message %d from %s\n",
		       msg->ibm_type, libcfs_nid2str(peer->ibp_nid));
		rc = -EPROTO;
		goto failed;
	}

	if (ver != msg->ibm_version) {
		CERROR("%s replied version %x is different with requested version %x\n",
		       libcfs_nid2str(peer->ibp_nid), msg->ibm_version, ver);
		rc = -EPROTO;
		goto failed;
	}

	if (msg->ibm_u.connparams.ibcp_queue_depth >
	    conn->ibc_queue_depth) {
		CERROR("%s has incompatible queue depth %d (<=%d wanted)\n",
		       libcfs_nid2str(peer->ibp_nid),
		       msg->ibm_u.connparams.ibcp_queue_depth,
		       conn->ibc_queue_depth);
		rc = -EPROTO;
		goto failed;
	}

	if ((msg->ibm_u.connparams.ibcp_max_frags >> IBLND_FRAG_SHIFT) >
	    conn->ibc_max_frags) {
		CERROR("%s has incompatible max_frags %d (<=%d wanted)\n",
		       libcfs_nid2str(peer->ibp_nid),
		       msg->ibm_u.connparams.ibcp_max_frags >> IBLND_FRAG_SHIFT,
		       conn->ibc_max_frags);
		rc = -EPROTO;
		goto failed;
	}

	if (msg->ibm_u.connparams.ibcp_max_msg_size > IBLND_MSG_SIZE) {
		CERROR("%s max message size %d too big (%d max)\n",
		       libcfs_nid2str(peer->ibp_nid),
		       msg->ibm_u.connparams.ibcp_max_msg_size,
		       IBLND_MSG_SIZE);
		rc = -EPROTO;
		goto failed;
	}

	read_lock_irqsave(&kiblnd_data.kib_global_lock, flags);
	if (msg->ibm_dstnid == ni->ni_nid &&
	    msg->ibm_dststamp == net->ibn_incarnation)
		rc = 0;
	else
		rc = -ESTALE;
	read_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	if (rc) {
		CERROR("Bad connection reply from %s, rc = %d, version: %x max_frags: %d\n",
		       libcfs_nid2str(peer->ibp_nid), rc,
		       msg->ibm_version, msg->ibm_u.connparams.ibcp_max_frags);
		goto failed;
	}

	conn->ibc_incarnation = msg->ibm_srcstamp;
	conn->ibc_credits = msg->ibm_u.connparams.ibcp_queue_depth;
	conn->ibc_reserved_credits = msg->ibm_u.connparams.ibcp_queue_depth;
	conn->ibc_queue_depth = msg->ibm_u.connparams.ibcp_queue_depth;
	conn->ibc_max_frags = msg->ibm_u.connparams.ibcp_max_frags >> IBLND_FRAG_SHIFT;
	LASSERT(conn->ibc_credits + conn->ibc_reserved_credits +
		IBLND_OOB_MSGS(ver) <= IBLND_RX_MSGS(conn));

	kiblnd_connreq_done(conn, 0);
	return;

 failed:
	/*
	 * NB My QP has already established itself, so I handle anything going
	 * wrong here by setting ibc_comms_error.
	 * kiblnd_connreq_done(0) moves the conn state to ESTABLISHED, but then
	 * immediately tears it down.
	 */
	LASSERT(rc);
	conn->ibc_comms_error = rc;
	kiblnd_connreq_done(conn, 0);
}

static int
kiblnd_active_connect(struct rdma_cm_id *cmid)
{
	struct kib_peer *peer = (struct kib_peer *)cmid->context;
	struct kib_conn *conn;
	struct kib_msg *msg;
	struct rdma_conn_param cp;
	int version;
	__u64 incarnation;
	unsigned long flags;
	int rc;

	read_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	incarnation = peer->ibp_incarnation;
	version = !peer->ibp_version ? IBLND_MSG_VERSION :
				       peer->ibp_version;

	read_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	conn = kiblnd_create_conn(peer, cmid, IBLND_CONN_ACTIVE_CONNECT,
				  version);
	if (!conn) {
		kiblnd_peer_connect_failed(peer, 1, -ENOMEM);
		kiblnd_peer_decref(peer); /* lose cmid's ref */
		return -ENOMEM;
	}

	/*
	 * conn "owns" cmid now, so I return success from here on to ensure the
	 * CM callback doesn't destroy cmid. conn also takes over cmid's ref
	 * on peer
	 */
	msg = &conn->ibc_connvars->cv_msg;

	memset(msg, 0, sizeof(*msg));
	kiblnd_init_msg(msg, IBLND_MSG_CONNREQ, sizeof(msg->ibm_u.connparams));
	msg->ibm_u.connparams.ibcp_queue_depth = conn->ibc_queue_depth;
	msg->ibm_u.connparams.ibcp_max_frags = conn->ibc_max_frags << IBLND_FRAG_SHIFT;
	msg->ibm_u.connparams.ibcp_max_msg_size = IBLND_MSG_SIZE;

	kiblnd_pack_msg(peer->ibp_ni, msg, version,
			0, peer->ibp_nid, incarnation);

	memset(&cp, 0, sizeof(cp));
	cp.private_data	= msg;
	cp.private_data_len    = msg->ibm_nob;
	cp.responder_resources = 0;	     /* No atomic ops or RDMA reads */
	cp.initiator_depth     = 0;
	cp.flow_control        = 1;
	cp.retry_count         = *kiblnd_tunables.kib_retry_count;
	cp.rnr_retry_count     = *kiblnd_tunables.kib_rnr_retry_count;

	LASSERT(cmid->context == (void *)conn);
	LASSERT(conn->ibc_cmid == cmid);

	rc = rdma_connect(cmid, &cp);
	if (rc) {
		CERROR("Can't connect to %s: %d\n",
		       libcfs_nid2str(peer->ibp_nid), rc);
		kiblnd_connreq_done(conn, rc);
		kiblnd_conn_decref(conn);
	}

	return 0;
}

int
kiblnd_cm_callback(struct rdma_cm_id *cmid, struct rdma_cm_event *event)
{
	struct kib_peer *peer;
	struct kib_conn *conn;
	int rc;

	switch (event->event) {
	default:
		CERROR("Unexpected event: %d, status: %d\n",
		       event->event, event->status);
		LBUG();

	case RDMA_CM_EVENT_CONNECT_REQUEST:
		/* destroy cmid on failure */
		rc = kiblnd_passive_connect(cmid,
					    (void *)KIBLND_CONN_PARAM(event),
					    KIBLND_CONN_PARAM_LEN(event));
		CDEBUG(D_NET, "connreq: %d\n", rc);
		return rc;

	case RDMA_CM_EVENT_ADDR_ERROR:
		peer = (struct kib_peer *)cmid->context;
		CNETERR("%s: ADDR ERROR %d\n",
			libcfs_nid2str(peer->ibp_nid), event->status);
		kiblnd_peer_connect_failed(peer, 1, -EHOSTUNREACH);
		kiblnd_peer_decref(peer);
		return -EHOSTUNREACH;      /* rc destroys cmid */

	case RDMA_CM_EVENT_ADDR_RESOLVED:
		peer = (struct kib_peer *)cmid->context;

		CDEBUG(D_NET, "%s Addr resolved: %d\n",
		       libcfs_nid2str(peer->ibp_nid), event->status);

		if (event->status) {
			CNETERR("Can't resolve address for %s: %d\n",
				libcfs_nid2str(peer->ibp_nid), event->status);
			rc = event->status;
		} else {
			rc = rdma_resolve_route(
				cmid, *kiblnd_tunables.kib_timeout * 1000);
			if (!rc)
				return 0;
			/* Can't initiate route resolution */
			CERROR("Can't resolve route for %s: %d\n",
			       libcfs_nid2str(peer->ibp_nid), rc);
		}
		kiblnd_peer_connect_failed(peer, 1, rc);
		kiblnd_peer_decref(peer);
		return rc;		      /* rc destroys cmid */

	case RDMA_CM_EVENT_ROUTE_ERROR:
		peer = (struct kib_peer *)cmid->context;
		CNETERR("%s: ROUTE ERROR %d\n",
			libcfs_nid2str(peer->ibp_nid), event->status);
		kiblnd_peer_connect_failed(peer, 1, -EHOSTUNREACH);
		kiblnd_peer_decref(peer);
		return -EHOSTUNREACH;	   /* rc destroys cmid */

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		peer = (struct kib_peer *)cmid->context;
		CDEBUG(D_NET, "%s Route resolved: %d\n",
		       libcfs_nid2str(peer->ibp_nid), event->status);

		if (!event->status)
			return kiblnd_active_connect(cmid);

		CNETERR("Can't resolve route for %s: %d\n",
			libcfs_nid2str(peer->ibp_nid), event->status);
		kiblnd_peer_connect_failed(peer, 1, event->status);
		kiblnd_peer_decref(peer);
		return event->status;	   /* rc destroys cmid */

	case RDMA_CM_EVENT_UNREACHABLE:
		conn = (struct kib_conn *)cmid->context;
		LASSERT(conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT ||
			conn->ibc_state == IBLND_CONN_PASSIVE_WAIT);
		CNETERR("%s: UNREACHABLE %d\n",
			libcfs_nid2str(conn->ibc_peer->ibp_nid), event->status);
		kiblnd_connreq_done(conn, -ENETDOWN);
		kiblnd_conn_decref(conn);
		return 0;

	case RDMA_CM_EVENT_CONNECT_ERROR:
		conn = (struct kib_conn *)cmid->context;
		LASSERT(conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT ||
			conn->ibc_state == IBLND_CONN_PASSIVE_WAIT);
		CNETERR("%s: CONNECT ERROR %d\n",
			libcfs_nid2str(conn->ibc_peer->ibp_nid), event->status);
		kiblnd_connreq_done(conn, -ENOTCONN);
		kiblnd_conn_decref(conn);
		return 0;

	case RDMA_CM_EVENT_REJECTED:
		conn = (struct kib_conn *)cmid->context;
		switch (conn->ibc_state) {
		default:
			LBUG();

		case IBLND_CONN_PASSIVE_WAIT:
			CERROR("%s: REJECTED %d\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid),
			       event->status);
			kiblnd_connreq_done(conn, -ECONNRESET);
			break;

		case IBLND_CONN_ACTIVE_CONNECT:
			kiblnd_rejected(conn, event->status,
					(void *)KIBLND_CONN_PARAM(event),
					KIBLND_CONN_PARAM_LEN(event));
			break;
		}
		kiblnd_conn_decref(conn);
		return 0;

	case RDMA_CM_EVENT_ESTABLISHED:
		conn = (struct kib_conn *)cmid->context;
		switch (conn->ibc_state) {
		default:
			LBUG();

		case IBLND_CONN_PASSIVE_WAIT:
			CDEBUG(D_NET, "ESTABLISHED (passive): %s\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid));
			kiblnd_connreq_done(conn, 0);
			break;

		case IBLND_CONN_ACTIVE_CONNECT:
			CDEBUG(D_NET, "ESTABLISHED(active): %s\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid));
			kiblnd_check_connreply(conn,
					       (void *)KIBLND_CONN_PARAM(event),
					       KIBLND_CONN_PARAM_LEN(event));
			break;
		}
		/* net keeps its ref on conn! */
		return 0;

	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		CDEBUG(D_NET, "Ignore TIMEWAIT_EXIT event\n");
		return 0;
	case RDMA_CM_EVENT_DISCONNECTED:
		conn = (struct kib_conn *)cmid->context;
		if (conn->ibc_state < IBLND_CONN_ESTABLISHED) {
			CERROR("%s DISCONNECTED\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid));
			kiblnd_connreq_done(conn, -ECONNRESET);
		} else {
			kiblnd_close_conn(conn, 0);
		}
		kiblnd_conn_decref(conn);
		cmid->context = NULL;
		return 0;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		LCONSOLE_ERROR_MSG(0x131,
				   "Received notification of device removal\n"
				   "Please shutdown LNET to allow this to proceed\n");
		/*
		 * Can't remove network from underneath LNET for now, so I have
		 * to ignore this
		 */
		return 0;

	case RDMA_CM_EVENT_ADDR_CHANGE:
		LCONSOLE_INFO("Physical link changed (eg hca/port)\n");
		return 0;
	}
}

static int
kiblnd_check_txs_locked(struct kib_conn *conn, struct list_head *txs)
{
	struct kib_tx *tx;
	struct list_head *ttmp;

	list_for_each(ttmp, txs) {
		tx = list_entry(ttmp, struct kib_tx, tx_list);

		if (txs != &conn->ibc_active_txs) {
			LASSERT(tx->tx_queued);
		} else {
			LASSERT(!tx->tx_queued);
			LASSERT(tx->tx_waiting || tx->tx_sending);
		}

		if (cfs_time_aftereq(jiffies, tx->tx_deadline)) {
			CERROR("Timed out tx: %s, %lu seconds\n",
			       kiblnd_queue2str(conn, txs),
			       cfs_duration_sec(jiffies - tx->tx_deadline));
			return 1;
		}
	}

	return 0;
}

static int
kiblnd_conn_timed_out_locked(struct kib_conn *conn)
{
	return  kiblnd_check_txs_locked(conn, &conn->ibc_tx_queue) ||
		kiblnd_check_txs_locked(conn, &conn->ibc_tx_noops) ||
		kiblnd_check_txs_locked(conn, &conn->ibc_tx_queue_rsrvd) ||
		kiblnd_check_txs_locked(conn, &conn->ibc_tx_queue_nocred) ||
		kiblnd_check_txs_locked(conn, &conn->ibc_active_txs);
}

static void
kiblnd_check_conns(int idx)
{
	LIST_HEAD(closes);
	LIST_HEAD(checksends);
	struct list_head *peers = &kiblnd_data.kib_peers[idx];
	struct list_head *ptmp;
	struct kib_peer *peer;
	struct kib_conn *conn;
	struct kib_conn *temp;
	struct kib_conn *tmp;
	struct list_head *ctmp;
	unsigned long flags;

	/*
	 * NB. We expect to have a look at all the peers and not find any
	 * RDMAs to time out, so we just use a shared lock while we
	 * take a look...
	 */
	read_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	list_for_each(ptmp, peers) {
		peer = list_entry(ptmp, struct kib_peer, ibp_list);

		list_for_each(ctmp, &peer->ibp_conns) {
			int timedout;
			int sendnoop;

			conn = list_entry(ctmp, struct kib_conn, ibc_list);

			LASSERT(conn->ibc_state == IBLND_CONN_ESTABLISHED);

			spin_lock(&conn->ibc_lock);

			sendnoop = kiblnd_need_noop(conn);
			timedout = kiblnd_conn_timed_out_locked(conn);
			if (!sendnoop && !timedout) {
				spin_unlock(&conn->ibc_lock);
				continue;
			}

			if (timedout) {
				CERROR("Timed out RDMA with %s (%lu): c: %u, oc: %u, rc: %u\n",
				       libcfs_nid2str(peer->ibp_nid),
				       cfs_duration_sec(cfs_time_current() -
							peer->ibp_last_alive),
				       conn->ibc_credits,
				       conn->ibc_outstanding_credits,
				       conn->ibc_reserved_credits);
				list_add(&conn->ibc_connd_list, &closes);
			} else {
				list_add(&conn->ibc_connd_list, &checksends);
			}
			/* +ref for 'closes' or 'checksends' */
			kiblnd_conn_addref(conn);

			spin_unlock(&conn->ibc_lock);
		}
	}

	read_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	/*
	 * Handle timeout by closing the whole
	 * connection. We can only be sure RDMA activity
	 * has ceased once the QP has been modified.
	 */
	list_for_each_entry_safe(conn, tmp, &closes, ibc_connd_list) {
		list_del(&conn->ibc_connd_list);
		kiblnd_close_conn(conn, -ETIMEDOUT);
		kiblnd_conn_decref(conn);
	}

	/*
	 * In case we have enough credits to return via a
	 * NOOP, but there were no non-blocking tx descs
	 * free to do it last time...
	 */
	list_for_each_entry_safe(conn, temp, &checksends, ibc_connd_list) {
		list_del(&conn->ibc_connd_list);

		spin_lock(&conn->ibc_lock);
		kiblnd_check_sends_locked(conn);
		spin_unlock(&conn->ibc_lock);

		kiblnd_conn_decref(conn);
	}
}

static void
kiblnd_disconnect_conn(struct kib_conn *conn)
{
	LASSERT(!in_interrupt());
	LASSERT(current == kiblnd_data.kib_connd);
	LASSERT(conn->ibc_state == IBLND_CONN_CLOSING);

	rdma_disconnect(conn->ibc_cmid);
	kiblnd_finalise_conn(conn);

	kiblnd_peer_notify(conn->ibc_peer);
}

/**
 * High-water for reconnection to the same peer, reconnection attempt should
 * be delayed after trying more than KIB_RECONN_HIGH_RACE.
 */
#define KIB_RECONN_HIGH_RACE	10
/**
 * Allow connd to take a break and handle other things after consecutive
 * reconnection attempts.
 */
#define KIB_RECONN_BREAK	100

int
kiblnd_connd(void *arg)
{
	spinlock_t *lock = &kiblnd_data.kib_connd_lock;
	wait_queue_t wait;
	unsigned long flags;
	struct kib_conn *conn;
	int timeout;
	int i;
	int dropped_lock;
	int peer_index = 0;
	unsigned long deadline = jiffies;

	cfs_block_allsigs();

	init_waitqueue_entry(&wait, current);
	kiblnd_data.kib_connd = current;

	spin_lock_irqsave(lock, flags);

	while (!kiblnd_data.kib_shutdown) {
		int reconn = 0;

		dropped_lock = 0;

		if (!list_empty(&kiblnd_data.kib_connd_zombies)) {
			struct kib_peer *peer = NULL;

			conn = list_entry(kiblnd_data.kib_connd_zombies.next,
					  struct kib_conn, ibc_list);
			list_del(&conn->ibc_list);
			if (conn->ibc_reconnect) {
				peer = conn->ibc_peer;
				kiblnd_peer_addref(peer);
			}

			spin_unlock_irqrestore(lock, flags);
			dropped_lock = 1;

			kiblnd_destroy_conn(conn, !peer);

			spin_lock_irqsave(lock, flags);
			if (!peer)
				continue;

			conn->ibc_peer = peer;
			if (peer->ibp_reconnected < KIB_RECONN_HIGH_RACE)
				list_add_tail(&conn->ibc_list,
					      &kiblnd_data.kib_reconn_list);
			else
				list_add_tail(&conn->ibc_list,
					      &kiblnd_data.kib_reconn_wait);
		}

		if (!list_empty(&kiblnd_data.kib_connd_conns)) {
			conn = list_entry(kiblnd_data.kib_connd_conns.next,
					  struct kib_conn, ibc_list);
			list_del(&conn->ibc_list);

			spin_unlock_irqrestore(lock, flags);
			dropped_lock = 1;

			kiblnd_disconnect_conn(conn);
			kiblnd_conn_decref(conn);

			spin_lock_irqsave(lock, flags);
		}

		while (reconn < KIB_RECONN_BREAK) {
			if (kiblnd_data.kib_reconn_sec !=
			    ktime_get_real_seconds()) {
				kiblnd_data.kib_reconn_sec = ktime_get_real_seconds();
				list_splice_init(&kiblnd_data.kib_reconn_wait,
						 &kiblnd_data.kib_reconn_list);
			}

			if (list_empty(&kiblnd_data.kib_reconn_list))
				break;

			conn = list_entry(kiblnd_data.kib_reconn_list.next,
					  struct kib_conn, ibc_list);
			list_del(&conn->ibc_list);

			spin_unlock_irqrestore(lock, flags);
			dropped_lock = 1;

			reconn += kiblnd_reconnect_peer(conn->ibc_peer);
			kiblnd_peer_decref(conn->ibc_peer);
			LIBCFS_FREE(conn, sizeof(*conn));

			spin_lock_irqsave(lock, flags);
		}

		/* careful with the jiffy wrap... */
		timeout = (int)(deadline - jiffies);
		if (timeout <= 0) {
			const int n = 4;
			const int p = 1;
			int chunk = kiblnd_data.kib_peer_hash_size;

			spin_unlock_irqrestore(lock, flags);
			dropped_lock = 1;

			/*
			 * Time to check for RDMA timeouts on a few more
			 * peers: I do checks every 'p' seconds on a
			 * proportion of the peer table and I need to check
			 * every connection 'n' times within a timeout
			 * interval, to ensure I detect a timeout on any
			 * connection within (n+1)/n times the timeout
			 * interval.
			 */
			if (*kiblnd_tunables.kib_timeout > n * p)
				chunk = (chunk * n * p) /
					*kiblnd_tunables.kib_timeout;
			if (!chunk)
				chunk = 1;

			for (i = 0; i < chunk; i++) {
				kiblnd_check_conns(peer_index);
				peer_index = (peer_index + 1) %
					     kiblnd_data.kib_peer_hash_size;
			}

			deadline += msecs_to_jiffies(p * MSEC_PER_SEC);
			spin_lock_irqsave(lock, flags);
		}

		if (dropped_lock)
			continue;

		/* Nothing to do for 'timeout'  */
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&kiblnd_data.kib_connd_waitq, &wait);
		spin_unlock_irqrestore(lock, flags);

		schedule_timeout(timeout);

		remove_wait_queue(&kiblnd_data.kib_connd_waitq, &wait);
		spin_lock_irqsave(lock, flags);
	}

	spin_unlock_irqrestore(lock, flags);

	kiblnd_thread_fini();
	return 0;
}

void
kiblnd_qp_event(struct ib_event *event, void *arg)
{
	struct kib_conn *conn = arg;

	switch (event->event) {
	case IB_EVENT_COMM_EST:
		CDEBUG(D_NET, "%s established\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid));
		/*
		 * We received a packet but connection isn't established
		 * probably handshake packet was lost, so free to
		 * force make connection established
		 */
		rdma_notify(conn->ibc_cmid, IB_EVENT_COMM_EST);
		return;

	default:
		CERROR("%s: Async QP event type %d\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid), event->event);
		return;
	}
}

static void
kiblnd_complete(struct ib_wc *wc)
{
	switch (kiblnd_wreqid2type(wc->wr_id)) {
	default:
		LBUG();

	case IBLND_WID_MR:
		if (wc->status != IB_WC_SUCCESS &&
		    wc->status != IB_WC_WR_FLUSH_ERR)
			CNETERR("FastReg failed: %d\n", wc->status);
		break;

	case IBLND_WID_RDMA:
		/*
		 * We only get RDMA completion notification if it fails.  All
		 * subsequent work items, including the final SEND will fail
		 * too.  However we can't print out any more info about the
		 * failing RDMA because 'tx' might be back on the idle list or
		 * even reused already if we didn't manage to post all our work
		 * items
		 */
		CNETERR("RDMA (tx: %p) failed: %d\n",
			kiblnd_wreqid2ptr(wc->wr_id), wc->status);
		return;

	case IBLND_WID_TX:
		kiblnd_tx_complete(kiblnd_wreqid2ptr(wc->wr_id), wc->status);
		return;

	case IBLND_WID_RX:
		kiblnd_rx_complete(kiblnd_wreqid2ptr(wc->wr_id), wc->status,
				   wc->byte_len);
		return;
	}
}

void
kiblnd_cq_completion(struct ib_cq *cq, void *arg)
{
	/*
	 * NB I'm not allowed to schedule this conn once its refcount has
	 * reached 0.  Since fundamentally I'm racing with scheduler threads
	 * consuming my CQ I could be called after all completions have
	 * occurred.  But in this case, !ibc_nrx && !ibc_nsends_posted
	 * and this CQ is about to be destroyed so I NOOP.
	 */
	struct kib_conn *conn = arg;
	struct kib_sched_info *sched = conn->ibc_sched;
	unsigned long flags;

	LASSERT(cq == conn->ibc_cq);

	spin_lock_irqsave(&sched->ibs_lock, flags);

	conn->ibc_ready = 1;

	if (!conn->ibc_scheduled &&
	    (conn->ibc_nrx > 0 ||
	     conn->ibc_nsends_posted > 0)) {
		kiblnd_conn_addref(conn); /* +1 ref for sched_conns */
		conn->ibc_scheduled = 1;
		list_add_tail(&conn->ibc_sched_list, &sched->ibs_conns);

		if (waitqueue_active(&sched->ibs_waitq))
			wake_up(&sched->ibs_waitq);
	}

	spin_unlock_irqrestore(&sched->ibs_lock, flags);
}

void
kiblnd_cq_event(struct ib_event *event, void *arg)
{
	struct kib_conn *conn = arg;

	CERROR("%s: async CQ event type %d\n",
	       libcfs_nid2str(conn->ibc_peer->ibp_nid), event->event);
}

int
kiblnd_scheduler(void *arg)
{
	long id = (long)arg;
	struct kib_sched_info *sched;
	struct kib_conn *conn;
	wait_queue_t wait;
	unsigned long flags;
	struct ib_wc wc;
	int did_something;
	int busy_loops = 0;
	int rc;

	cfs_block_allsigs();

	init_waitqueue_entry(&wait, current);

	sched = kiblnd_data.kib_scheds[KIB_THREAD_CPT(id)];

	rc = cfs_cpt_bind(lnet_cpt_table(), sched->ibs_cpt);
	if (rc) {
		CWARN("Failed to bind on CPT %d, please verify whether all CPUs are healthy and reload modules if necessary, otherwise your system might under risk of low performance\n",
		      sched->ibs_cpt);
	}

	spin_lock_irqsave(&sched->ibs_lock, flags);

	while (!kiblnd_data.kib_shutdown) {
		if (busy_loops++ >= IBLND_RESCHED) {
			spin_unlock_irqrestore(&sched->ibs_lock, flags);

			cond_resched();
			busy_loops = 0;

			spin_lock_irqsave(&sched->ibs_lock, flags);
		}

		did_something = 0;

		if (!list_empty(&sched->ibs_conns)) {
			conn = list_entry(sched->ibs_conns.next, struct kib_conn,
					  ibc_sched_list);
			/* take over kib_sched_conns' ref on conn... */
			LASSERT(conn->ibc_scheduled);
			list_del(&conn->ibc_sched_list);
			conn->ibc_ready = 0;

			spin_unlock_irqrestore(&sched->ibs_lock, flags);

			wc.wr_id = IBLND_WID_INVAL;

			rc = ib_poll_cq(conn->ibc_cq, 1, &wc);
			if (!rc) {
				rc = ib_req_notify_cq(conn->ibc_cq,
						      IB_CQ_NEXT_COMP);
				if (rc < 0) {
					CWARN("%s: ib_req_notify_cq failed: %d, closing connection\n",
					      libcfs_nid2str(conn->ibc_peer->ibp_nid), rc);
					kiblnd_close_conn(conn, -EIO);
					kiblnd_conn_decref(conn);
					spin_lock_irqsave(&sched->ibs_lock,
							  flags);
					continue;
				}

				rc = ib_poll_cq(conn->ibc_cq, 1, &wc);
			}

			if (unlikely(rc > 0 && wc.wr_id == IBLND_WID_INVAL)) {
				LCONSOLE_ERROR("ib_poll_cq (rc: %d) returned invalid wr_id, opcode %d, status: %d, vendor_err: %d, conn: %s status: %d\nplease upgrade firmware and OFED or contact vendor.\n",
					       rc, wc.opcode, wc.status,
					       wc.vendor_err,
					       libcfs_nid2str(conn->ibc_peer->ibp_nid),
					       conn->ibc_state);
				rc = -EINVAL;
			}

			if (rc < 0) {
				CWARN("%s: ib_poll_cq failed: %d, closing connection\n",
				      libcfs_nid2str(conn->ibc_peer->ibp_nid),
				      rc);
				kiblnd_close_conn(conn, -EIO);
				kiblnd_conn_decref(conn);
				spin_lock_irqsave(&sched->ibs_lock, flags);
				continue;
			}

			spin_lock_irqsave(&sched->ibs_lock, flags);

			if (rc || conn->ibc_ready) {
				/*
				 * There may be another completion waiting; get
				 * another scheduler to check while I handle
				 * this one...
				 */
				/* +1 ref for sched_conns */
				kiblnd_conn_addref(conn);
				list_add_tail(&conn->ibc_sched_list,
					      &sched->ibs_conns);
				if (waitqueue_active(&sched->ibs_waitq))
					wake_up(&sched->ibs_waitq);
			} else {
				conn->ibc_scheduled = 0;
			}

			if (rc) {
				spin_unlock_irqrestore(&sched->ibs_lock, flags);
				kiblnd_complete(&wc);

				spin_lock_irqsave(&sched->ibs_lock, flags);
			}

			kiblnd_conn_decref(conn); /* ...drop my ref from above */
			did_something = 1;
		}

		if (did_something)
			continue;

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue_exclusive(&sched->ibs_waitq, &wait);
		spin_unlock_irqrestore(&sched->ibs_lock, flags);

		schedule();
		busy_loops = 0;

		remove_wait_queue(&sched->ibs_waitq, &wait);
		spin_lock_irqsave(&sched->ibs_lock, flags);
	}

	spin_unlock_irqrestore(&sched->ibs_lock, flags);

	kiblnd_thread_fini();
	return 0;
}

int
kiblnd_failover_thread(void *arg)
{
	rwlock_t *glock = &kiblnd_data.kib_global_lock;
	struct kib_dev *dev;
	wait_queue_t wait;
	unsigned long flags;
	int rc;

	LASSERT(*kiblnd_tunables.kib_dev_failover);

	cfs_block_allsigs();

	init_waitqueue_entry(&wait, current);
	write_lock_irqsave(glock, flags);

	while (!kiblnd_data.kib_shutdown) {
		int do_failover = 0;
		int long_sleep;

		list_for_each_entry(dev, &kiblnd_data.kib_failed_devs,
				    ibd_fail_list) {
			if (time_before(cfs_time_current(),
					dev->ibd_next_failover))
				continue;
			do_failover = 1;
			break;
		}

		if (do_failover) {
			list_del_init(&dev->ibd_fail_list);
			dev->ibd_failover = 1;
			write_unlock_irqrestore(glock, flags);

			rc = kiblnd_dev_failover(dev);

			write_lock_irqsave(glock, flags);

			LASSERT(dev->ibd_failover);
			dev->ibd_failover = 0;
			if (rc >= 0) { /* Device is OK or failover succeed */
				dev->ibd_next_failover = cfs_time_shift(3);
				continue;
			}

			/* failed to failover, retry later */
			dev->ibd_next_failover =
				cfs_time_shift(min(dev->ibd_failed_failover, 10));
			if (kiblnd_dev_can_failover(dev)) {
				list_add_tail(&dev->ibd_fail_list,
					      &kiblnd_data.kib_failed_devs);
			}

			continue;
		}

		/* long sleep if no more pending failover */
		long_sleep = list_empty(&kiblnd_data.kib_failed_devs);

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&kiblnd_data.kib_failover_waitq, &wait);
		write_unlock_irqrestore(glock, flags);

		rc = schedule_timeout(long_sleep ? cfs_time_seconds(10) :
						   cfs_time_seconds(1));
		remove_wait_queue(&kiblnd_data.kib_failover_waitq, &wait);
		write_lock_irqsave(glock, flags);

		if (!long_sleep || rc)
			continue;

		/*
		 * have a long sleep, routine check all active devices,
		 * we need checking like this because if there is not active
		 * connection on the dev and no SEND from local, we may listen
		 * on wrong HCA for ever while there is a bonding failover
		 */
		list_for_each_entry(dev, &kiblnd_data.kib_devs, ibd_list) {
			if (kiblnd_dev_can_failover(dev)) {
				list_add_tail(&dev->ibd_fail_list,
					      &kiblnd_data.kib_failed_devs);
			}
		}
	}

	write_unlock_irqrestore(glock, flags);

	kiblnd_thread_fini();
	return 0;
}
