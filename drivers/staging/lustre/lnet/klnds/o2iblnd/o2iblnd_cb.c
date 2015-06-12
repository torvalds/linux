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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
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

static void
kiblnd_tx_done(lnet_ni_t *ni, kib_tx_t *tx)
{
	lnet_msg_t *lntmsg[2];
	kib_net_t  *net = ni->ni_data;
	int	 rc;
	int	 i;

	LASSERT(net != NULL);
	LASSERT(!in_interrupt());
	LASSERT(!tx->tx_queued);	       /* mustn't be queued for sending */
	LASSERT(tx->tx_sending == 0);	  /* mustn't be awaiting sent callback */
	LASSERT(!tx->tx_waiting);	      /* mustn't be awaiting peer response */
	LASSERT(tx->tx_pool != NULL);

	kiblnd_unmap_tx(ni, tx);

	/* tx may have up to 2 lnet msgs to finalise */
	lntmsg[0] = tx->tx_lntmsg[0]; tx->tx_lntmsg[0] = NULL;
	lntmsg[1] = tx->tx_lntmsg[1]; tx->tx_lntmsg[1] = NULL;
	rc = tx->tx_status;

	if (tx->tx_conn != NULL) {
		LASSERT(ni == tx->tx_conn->ibc_peer->ibp_ni);

		kiblnd_conn_decref(tx->tx_conn);
		tx->tx_conn = NULL;
	}

	tx->tx_nwrq = 0;
	tx->tx_status = 0;

	kiblnd_pool_free_node(&tx->tx_pool->tpo_pool, &tx->tx_list);

	/* delay finalize until my descs have been freed */
	for (i = 0; i < 2; i++) {
		if (lntmsg[i] == NULL)
			continue;

		lnet_finalize(ni, lntmsg[i], rc);
	}
}

void
kiblnd_txlist_done(lnet_ni_t *ni, struct list_head *txlist, int status)
{
	kib_tx_t *tx;

	while (!list_empty(txlist)) {
		tx = list_entry(txlist->next, kib_tx_t, tx_list);

		list_del(&tx->tx_list);
		/* complete now */
		tx->tx_waiting = 0;
		tx->tx_status = status;
		kiblnd_tx_done(ni, tx);
	}
}

static kib_tx_t *
kiblnd_get_idle_tx(lnet_ni_t *ni, lnet_nid_t target)
{
	kib_net_t		*net = (kib_net_t *)ni->ni_data;
	struct list_head		*node;
	kib_tx_t		*tx;
	kib_tx_poolset_t	*tps;

	tps = net->ibn_tx_ps[lnet_cpt_of_nid(target)];
	node = kiblnd_pool_alloc_node(&tps->tps_poolset);
	if (node == NULL)
		return NULL;
	tx = container_of(node, kib_tx_t, tx_list);

	LASSERT(tx->tx_nwrq == 0);
	LASSERT(!tx->tx_queued);
	LASSERT(tx->tx_sending == 0);
	LASSERT(!tx->tx_waiting);
	LASSERT(tx->tx_status == 0);
	LASSERT(tx->tx_conn == NULL);
	LASSERT(tx->tx_lntmsg[0] == NULL);
	LASSERT(tx->tx_lntmsg[1] == NULL);
	LASSERT(tx->tx_u.pmr == NULL);
	LASSERT(tx->tx_nfrags == 0);

	return tx;
}

static void
kiblnd_drop_rx(kib_rx_t *rx)
{
	kib_conn_t		*conn	= rx->rx_conn;
	struct kib_sched_info	*sched	= conn->ibc_sched;
	unsigned long		flags;

	spin_lock_irqsave(&sched->ibs_lock, flags);
	LASSERT(conn->ibc_nrx > 0);
	conn->ibc_nrx--;
	spin_unlock_irqrestore(&sched->ibs_lock, flags);

	kiblnd_conn_decref(conn);
}

int
kiblnd_post_rx(kib_rx_t *rx, int credit)
{
	kib_conn_t	 *conn = rx->rx_conn;
	kib_net_t	  *net = conn->ibc_peer->ibp_ni->ni_data;
	struct ib_recv_wr  *bad_wrq = NULL;
	struct ib_mr       *mr;
	int		 rc;

	LASSERT(net != NULL);
	LASSERT(!in_interrupt());
	LASSERT(credit == IBLND_POSTRX_NO_CREDIT ||
		credit == IBLND_POSTRX_PEER_CREDIT ||
		credit == IBLND_POSTRX_RSRVD_CREDIT);

	mr = kiblnd_find_dma_mr(conn->ibc_hdev, rx->rx_msgaddr, IBLND_MSG_SIZE);
	LASSERT(mr != NULL);

	rx->rx_sge.lkey   = mr->lkey;
	rx->rx_sge.addr   = rx->rx_msgaddr;
	rx->rx_sge.length = IBLND_MSG_SIZE;

	rx->rx_wrq.next = NULL;
	rx->rx_wrq.sg_list = &rx->rx_sge;
	rx->rx_wrq.num_sge = 1;
	rx->rx_wrq.wr_id = kiblnd_ptr2wreqid(rx, IBLND_WID_RX);

	LASSERT(conn->ibc_state >= IBLND_CONN_INIT);
	LASSERT(rx->rx_nob >= 0);	      /* not posted */

	if (conn->ibc_state > IBLND_CONN_ESTABLISHED) {
		kiblnd_drop_rx(rx);	     /* No more posts for this rx */
		return 0;
	}

	rx->rx_nob = -1;			/* flag posted */

	rc = ib_post_recv(conn->ibc_cmid->qp, &rx->rx_wrq, &bad_wrq);
	if (rc != 0) {
		CERROR("Can't post rx for %s: %d, bad_wrq: %p\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid), rc, bad_wrq);
		rx->rx_nob = 0;
	}

	if (conn->ibc_state < IBLND_CONN_ESTABLISHED) /* Initial post */
		return rc;

	if (rc != 0) {
		kiblnd_close_conn(conn, rc);
		kiblnd_drop_rx(rx);	     /* No more posts for this rx */
		return rc;
	}

	if (credit == IBLND_POSTRX_NO_CREDIT)
		return 0;

	spin_lock(&conn->ibc_lock);
	if (credit == IBLND_POSTRX_PEER_CREDIT)
		conn->ibc_outstanding_credits++;
	else
		conn->ibc_reserved_credits++;
	spin_unlock(&conn->ibc_lock);

	kiblnd_check_sends(conn);
	return 0;
}

static kib_tx_t *
kiblnd_find_waiting_tx_locked(kib_conn_t *conn, int txtype, __u64 cookie)
{
	struct list_head   *tmp;

	list_for_each(tmp, &conn->ibc_active_txs) {
		kib_tx_t *tx = list_entry(tmp, kib_tx_t, tx_list);

		LASSERT(!tx->tx_queued);
		LASSERT(tx->tx_sending != 0 || tx->tx_waiting);

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
kiblnd_handle_completion(kib_conn_t *conn, int txtype, int status, __u64 cookie)
{
	kib_tx_t    *tx;
	lnet_ni_t   *ni = conn->ibc_peer->ibp_ni;
	int	  idle;

	spin_lock(&conn->ibc_lock);

	tx = kiblnd_find_waiting_tx_locked(conn, txtype, cookie);
	if (tx == NULL) {
		spin_unlock(&conn->ibc_lock);

		CWARN("Unmatched completion type %x cookie %#llx from %s\n",
		      txtype, cookie, libcfs_nid2str(conn->ibc_peer->ibp_nid));
		kiblnd_close_conn(conn, -EPROTO);
		return;
	}

	if (tx->tx_status == 0) {	       /* success so far */
		if (status < 0) {	       /* failed? */
			tx->tx_status = status;
		} else if (txtype == IBLND_MSG_GET_REQ) {
			lnet_set_reply_msg_len(ni, tx->tx_lntmsg[1], status);
		}
	}

	tx->tx_waiting = 0;

	idle = !tx->tx_queued && (tx->tx_sending == 0);
	if (idle)
		list_del(&tx->tx_list);

	spin_unlock(&conn->ibc_lock);

	if (idle)
		kiblnd_tx_done(ni, tx);
}

static void
kiblnd_send_completion(kib_conn_t *conn, int type, int status, __u64 cookie)
{
	lnet_ni_t   *ni = conn->ibc_peer->ibp_ni;
	kib_tx_t    *tx = kiblnd_get_idle_tx(ni, conn->ibc_peer->ibp_nid);

	if (tx == NULL) {
		CERROR("Can't get tx for completion %x for %s\n",
		       type, libcfs_nid2str(conn->ibc_peer->ibp_nid));
		return;
	}

	tx->tx_msg->ibm_u.completion.ibcm_status = status;
	tx->tx_msg->ibm_u.completion.ibcm_cookie = cookie;
	kiblnd_init_tx_msg(ni, tx, type, sizeof(kib_completion_msg_t));

	kiblnd_queue_tx(tx, conn);
}

static void
kiblnd_handle_rx(kib_rx_t *rx)
{
	kib_msg_t    *msg = rx->rx_msg;
	kib_conn_t   *conn = rx->rx_conn;
	lnet_ni_t    *ni = conn->ibc_peer->ibp_ni;
	int	   credits = msg->ibm_credits;
	kib_tx_t     *tx;
	int	   rc = 0;
	int	   rc2;
	int	   post_credit;

	LASSERT(conn->ibc_state >= IBLND_CONN_ESTABLISHED);

	CDEBUG(D_NET, "Received %x[%d] from %s\n",
	       msg->ibm_type, credits,
	       libcfs_nid2str(conn->ibc_peer->ibp_nid));

	if (credits != 0) {
		/* Have I received credits that will let me send? */
		spin_lock(&conn->ibc_lock);

		if (conn->ibc_credits + credits >
		    IBLND_MSG_QUEUE_SIZE(conn->ibc_version)) {
			rc2 = conn->ibc_credits;
			spin_unlock(&conn->ibc_lock);

			CERROR("Bad credits from %s: %d + %d > %d\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid),
			       rc2, credits,
			       IBLND_MSG_QUEUE_SIZE(conn->ibc_version));

			kiblnd_close_conn(conn, -EPROTO);
			kiblnd_post_rx(rx, IBLND_POSTRX_NO_CREDIT);
			return;
		}

		conn->ibc_credits += credits;

		/* This ensures the credit taken by NOOP can be returned */
		if (msg->ibm_type == IBLND_MSG_NOOP &&
		    !IBLND_OOB_CAPABLE(conn->ibc_version)) /* v1 only */
			conn->ibc_outstanding_credits++;

		spin_unlock(&conn->ibc_lock);
		kiblnd_check_sends(conn);
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

		if (credits != 0) /* credit already posted */
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
		if (tx != NULL)
			list_del(&tx->tx_list);
		spin_unlock(&conn->ibc_lock);

		if (tx == NULL) {
			CERROR("Unmatched PUT_ACK from %s\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid));
			rc = -EPROTO;
			break;
		}

		LASSERT(tx->tx_waiting);
		/* CAVEAT EMPTOR: I could be racing with tx_complete, but...
		 * (a) I can overwrite tx_msg since my peer has received it!
		 * (b) tx_waiting set tells tx_complete() it's not done. */

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
kiblnd_rx_complete(kib_rx_t *rx, int status, int nob)
{
	kib_msg_t    *msg = rx->rx_msg;
	kib_conn_t   *conn = rx->rx_conn;
	lnet_ni_t    *ni = conn->ibc_peer->ibp_ni;
	kib_net_t    *net = ni->ni_data;
	int	   rc;
	int	   err = -EIO;

	LASSERT(net != NULL);
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
	if (rc != 0) {
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
		rwlock_t  *g_lock = &kiblnd_data.kib_global_lock;
		unsigned long  flags;

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
		LASSERT(page != NULL);
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
	LASSERT(page != NULL);
	return page;
}

static int
kiblnd_fmr_map_tx(kib_net_t *net, kib_tx_t *tx, kib_rdma_desc_t *rd, int nob)
{
	kib_hca_dev_t		*hdev;
	__u64			*pages = tx->tx_pages;
	kib_fmr_poolset_t	*fps;
	int			npages;
	int			size;
	int			cpt;
	int			rc;
	int			i;

	LASSERT(tx->tx_pool != NULL);
	LASSERT(tx->tx_pool->tpo_pool.po_owner != NULL);

	hdev  = tx->tx_pool->tpo_hdev;

	for (i = 0, npages = 0; i < rd->rd_nfrags; i++) {
		for (size = 0; size <  rd->rd_frags[i].rf_nob;
			       size += hdev->ibh_page_size) {
			pages[npages++] = (rd->rd_frags[i].rf_addr &
					    hdev->ibh_page_mask) + size;
		}
	}

	cpt = tx->tx_pool->tpo_pool.po_owner->ps_cpt;

	fps = net->ibn_fmr_ps[cpt];
	rc = kiblnd_fmr_pool_map(fps, pages, npages, 0, &tx->tx_u.fmr);
	if (rc != 0) {
		CERROR("Can't map %d pages: %d\n", npages, rc);
		return rc;
	}

	/* If rd is not tx_rd, it's going to get sent to a peer, who will need
	 * the rkey */
	rd->rd_key = (rd != tx->tx_rd) ? tx->tx_u.fmr.fmr_pfmr->fmr->rkey :
					 tx->tx_u.fmr.fmr_pfmr->fmr->lkey;
	rd->rd_frags[0].rf_addr &= ~hdev->ibh_page_mask;
	rd->rd_frags[0].rf_nob   = nob;
	rd->rd_nfrags = 1;

	return 0;
}

static int
kiblnd_pmr_map_tx(kib_net_t *net, kib_tx_t *tx, kib_rdma_desc_t *rd, int nob)
{
	kib_hca_dev_t		*hdev;
	kib_pmr_poolset_t	*pps;
	__u64			iova;
	int			cpt;
	int			rc;

	LASSERT(tx->tx_pool != NULL);
	LASSERT(tx->tx_pool->tpo_pool.po_owner != NULL);

	hdev = tx->tx_pool->tpo_hdev;

	iova = rd->rd_frags[0].rf_addr & ~hdev->ibh_page_mask;

	cpt = tx->tx_pool->tpo_pool.po_owner->ps_cpt;

	pps = net->ibn_pmr_ps[cpt];
	rc = kiblnd_pmr_pool_map(pps, hdev, rd, &iova, &tx->tx_u.pmr);
	if (rc != 0) {
		CERROR("Failed to create MR by phybuf: %d\n", rc);
		return rc;
	}

	/* If rd is not tx_rd, it's going to get sent to a peer, who will need
	 * the rkey */
	rd->rd_key = (rd != tx->tx_rd) ? tx->tx_u.pmr->pmr_mr->rkey :
					 tx->tx_u.pmr->pmr_mr->lkey;
	rd->rd_nfrags = 1;
	rd->rd_frags[0].rf_addr = iova;
	rd->rd_frags[0].rf_nob  = nob;

	return 0;
}

void
kiblnd_unmap_tx(lnet_ni_t *ni, kib_tx_t *tx)
{
	kib_net_t  *net = ni->ni_data;

	LASSERT(net != NULL);

	if (net->ibn_fmr_ps != NULL && tx->tx_u.fmr.fmr_pfmr != NULL) {
		kiblnd_fmr_pool_unmap(&tx->tx_u.fmr, tx->tx_status);
		tx->tx_u.fmr.fmr_pfmr = NULL;

	} else if (net->ibn_pmr_ps != NULL && tx->tx_u.pmr != NULL) {
		kiblnd_pmr_pool_unmap(tx->tx_u.pmr);
		tx->tx_u.pmr = NULL;
	}

	if (tx->tx_nfrags != 0) {
		kiblnd_dma_unmap_sg(tx->tx_pool->tpo_hdev->ibh_ibdev,
				    tx->tx_frags, tx->tx_nfrags, tx->tx_dmadir);
		tx->tx_nfrags = 0;
	}
}

int
kiblnd_map_tx(lnet_ni_t *ni, kib_tx_t *tx,
	      kib_rdma_desc_t *rd, int nfrags)
{
	kib_hca_dev_t      *hdev  = tx->tx_pool->tpo_hdev;
	kib_net_t	  *net   = ni->ni_data;
	struct ib_mr       *mr    = NULL;
	__u32	       nob;
	int		 i;

	/* If rd is not tx_rd, it's going to get sent to a peer and I'm the
	 * RDMA sink */
	tx->tx_dmadir = (rd != tx->tx_rd) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	tx->tx_nfrags = nfrags;

	rd->rd_nfrags =
		kiblnd_dma_map_sg(hdev->ibh_ibdev,
				  tx->tx_frags, tx->tx_nfrags, tx->tx_dmadir);

	for (i = 0, nob = 0; i < rd->rd_nfrags; i++) {
		rd->rd_frags[i].rf_nob  = kiblnd_sg_dma_len(
			hdev->ibh_ibdev, &tx->tx_frags[i]);
		rd->rd_frags[i].rf_addr = kiblnd_sg_dma_address(
			hdev->ibh_ibdev, &tx->tx_frags[i]);
		nob += rd->rd_frags[i].rf_nob;
	}

	/* looking for pre-mapping MR */
	mr = kiblnd_find_rd_dma_mr(hdev, rd);
	if (mr != NULL) {
		/* found pre-mapping MR */
		rd->rd_key = (rd != tx->tx_rd) ? mr->rkey : mr->lkey;
		return 0;
	}

	if (net->ibn_fmr_ps != NULL)
		return kiblnd_fmr_map_tx(net, tx, rd, nob);
	else if (net->ibn_pmr_ps != NULL)
		return kiblnd_pmr_map_tx(net, tx, rd, nob);

	return -EINVAL;
}


static int
kiblnd_setup_rd_iov(lnet_ni_t *ni, kib_tx_t *tx, kib_rdma_desc_t *rd,
		    unsigned int niov, struct kvec *iov, int offset, int nob)
{
	kib_net_t	  *net = ni->ni_data;
	struct page	*page;
	struct scatterlist *sg;
	unsigned long       vaddr;
	int		 fragnob;
	int		 page_offset;

	LASSERT(nob > 0);
	LASSERT(niov > 0);
	LASSERT(net != NULL);

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
		if (page == NULL) {
			CERROR("Can't find page\n");
			return -EFAULT;
		}

		fragnob = min((int)(iov->iov_len - offset), nob);
		fragnob = min(fragnob, (int)PAGE_SIZE - page_offset);

		sg_set_page(sg, page, fragnob, page_offset);
		sg++;

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
kiblnd_setup_rd_kiov(lnet_ni_t *ni, kib_tx_t *tx, kib_rdma_desc_t *rd,
		      int nkiov, lnet_kiov_t *kiov, int offset, int nob)
{
	kib_net_t	  *net = ni->ni_data;
	struct scatterlist *sg;
	int		 fragnob;

	CDEBUG(D_NET, "niov %d offset %d nob %d\n", nkiov, offset, nob);

	LASSERT(nob > 0);
	LASSERT(nkiov > 0);
	LASSERT(net != NULL);

	while (offset >= kiov->kiov_len) {
		offset -= kiov->kiov_len;
		nkiov--;
		kiov++;
		LASSERT(nkiov > 0);
	}

	sg = tx->tx_frags;
	do {
		LASSERT(nkiov > 0);

		fragnob = min((int)(kiov->kiov_len - offset), nob);

		sg_set_page(sg, kiov->kiov_page, fragnob,
			    kiov->kiov_offset + offset);
		sg++;

		offset = 0;
		kiov++;
		nkiov--;
		nob -= fragnob;
	} while (nob > 0);

	return kiblnd_map_tx(ni, tx, rd, sg - tx->tx_frags);
}

static int
kiblnd_post_tx_locked(kib_conn_t *conn, kib_tx_t *tx, int credit)
	__releases(conn->ibc_lock)
	__acquires(conn->ibc_lock)
{
	kib_msg_t	 *msg = tx->tx_msg;
	kib_peer_t	*peer = conn->ibc_peer;
	int		ver = conn->ibc_version;
	int		rc;
	int		done;
	struct ib_send_wr *bad_wrq;

	LASSERT(tx->tx_queued);
	/* We rely on this for QP sizing */
	LASSERT(tx->tx_nwrq > 0);
	LASSERT(tx->tx_nwrq <= 1 + IBLND_RDMA_FRAGS(ver));

	LASSERT(credit == 0 || credit == 1);
	LASSERT(conn->ibc_outstanding_credits >= 0);
	LASSERT(conn->ibc_outstanding_credits <= IBLND_MSG_QUEUE_SIZE(ver));
	LASSERT(conn->ibc_credits >= 0);
	LASSERT(conn->ibc_credits <= IBLND_MSG_QUEUE_SIZE(ver));

	if (conn->ibc_nsends_posted == IBLND_CONCURRENT_SENDS(ver)) {
		/* tx completions outstanding... */
		CDEBUG(D_NET, "%s: posted enough\n",
		       libcfs_nid2str(peer->ibp_nid));
		return -EAGAIN;
	}

	if (credit != 0 && conn->ibc_credits == 0) {   /* no credits */
		CDEBUG(D_NET, "%s: no credits\n",
		       libcfs_nid2str(peer->ibp_nid));
		return -EAGAIN;
	}

	if (credit != 0 && !IBLND_OOB_CAPABLE(ver) &&
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
		/* OK to drop when posted enough NOOPs, since
		 * kiblnd_check_sends will queue NOOP again when
		 * posted NOOPs complete */
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

	/* CAVEAT EMPTOR!  This tx could be the PUT_DONE of an RDMA
	 * PUT.  If so, it was first queued here as a PUT_REQ, sent and
	 * stashed on ibc_active_txs, matched by an incoming PUT_ACK,
	 * and then re-queued here.  It's (just) possible that
	 * tx_sending is non-zero if we've not done the tx_complete()
	 * from the first send; hence the ++ rather than = below. */
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
		rc = ib_post_send(conn->ibc_cmid->qp,
				  tx->tx_wrq, &bad_wrq);
	}

	conn->ibc_last_send = jiffies;

	if (rc == 0)
		return 0;

	/* NB credits are transferred in the actual
	 * message, which can only be the last work item */
	conn->ibc_credits += credit;
	conn->ibc_outstanding_credits += msg->ibm_credits;
	conn->ibc_nsends_posted--;
	if (msg->ibm_type == IBLND_MSG_NOOP)
		conn->ibc_noops_posted--;

	tx->tx_status = rc;
	tx->tx_waiting = 0;
	tx->tx_sending--;

	done = (tx->tx_sending == 0);
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

void
kiblnd_check_sends(kib_conn_t *conn)
{
	int	ver = conn->ibc_version;
	lnet_ni_t *ni = conn->ibc_peer->ibp_ni;
	kib_tx_t  *tx;

	/* Don't send anything until after the connection is established */
	if (conn->ibc_state < IBLND_CONN_ESTABLISHED) {
		CDEBUG(D_NET, "%s too soon\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid));
		return;
	}

	spin_lock(&conn->ibc_lock);

	LASSERT(conn->ibc_nsends_posted <= IBLND_CONCURRENT_SENDS(ver));
	LASSERT(!IBLND_OOB_CAPABLE(ver) ||
		 conn->ibc_noops_posted <= IBLND_OOB_MSGS(ver));
	LASSERT(conn->ibc_reserved_credits >= 0);

	while (conn->ibc_reserved_credits > 0 &&
	       !list_empty(&conn->ibc_tx_queue_rsrvd)) {
		tx = list_entry(conn->ibc_tx_queue_rsrvd.next,
				    kib_tx_t, tx_list);
		list_del(&tx->tx_list);
		list_add_tail(&tx->tx_list, &conn->ibc_tx_queue);
		conn->ibc_reserved_credits--;
	}

	if (kiblnd_need_noop(conn)) {
		spin_unlock(&conn->ibc_lock);

		tx = kiblnd_get_idle_tx(ni, conn->ibc_peer->ibp_nid);
		if (tx != NULL)
			kiblnd_init_tx_msg(ni, tx, IBLND_MSG_NOOP, 0);

		spin_lock(&conn->ibc_lock);
		if (tx != NULL)
			kiblnd_queue_tx_locked(tx, conn);
	}

	kiblnd_conn_addref(conn); /* 1 ref for me.... (see b21911) */

	for (;;) {
		int credit;

		if (!list_empty(&conn->ibc_tx_queue_nocred)) {
			credit = 0;
			tx = list_entry(conn->ibc_tx_queue_nocred.next,
					    kib_tx_t, tx_list);
		} else if (!list_empty(&conn->ibc_tx_noops)) {
			LASSERT(!IBLND_OOB_CAPABLE(ver));
			credit = 1;
			tx = list_entry(conn->ibc_tx_noops.next,
					kib_tx_t, tx_list);
		} else if (!list_empty(&conn->ibc_tx_queue)) {
			credit = 1;
			tx = list_entry(conn->ibc_tx_queue.next,
					    kib_tx_t, tx_list);
		} else
			break;

		if (kiblnd_post_tx_locked(conn, tx, credit) != 0)
			break;
	}

	spin_unlock(&conn->ibc_lock);

	kiblnd_conn_decref(conn); /* ...until here */
}

static void
kiblnd_tx_complete(kib_tx_t *tx, int status)
{
	int	   failed = (status != IB_WC_SUCCESS);
	kib_conn_t   *conn = tx->tx_conn;
	int	   idle;

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

	/* I could be racing with rdma completion.  Whoever makes 'tx' idle
	 * gets to free it, which also drops its ref on 'conn'. */

	tx->tx_sending--;
	conn->ibc_nsends_posted--;
	if (tx->tx_msg->ibm_type == IBLND_MSG_NOOP)
		conn->ibc_noops_posted--;

	if (failed) {
		tx->tx_waiting = 0;	     /* don't wait for peer */
		tx->tx_status = -EIO;
	}

	idle = (tx->tx_sending == 0) &&	 /* This is the final callback */
	       !tx->tx_waiting &&	       /* Not waiting for peer */
	       !tx->tx_queued;		  /* Not re-queued (PUT_DONE) */
	if (idle)
		list_del(&tx->tx_list);

	kiblnd_conn_addref(conn);	       /* 1 ref for me.... */

	spin_unlock(&conn->ibc_lock);

	if (idle)
		kiblnd_tx_done(conn->ibc_peer->ibp_ni, tx);

	kiblnd_check_sends(conn);

	kiblnd_conn_decref(conn);	       /* ...until here */
}

void
kiblnd_init_tx_msg(lnet_ni_t *ni, kib_tx_t *tx, int type, int body_nob)
{
	kib_hca_dev_t     *hdev = tx->tx_pool->tpo_hdev;
	struct ib_sge     *sge = &tx->tx_sge[tx->tx_nwrq];
	struct ib_send_wr *wrq = &tx->tx_wrq[tx->tx_nwrq];
	int		nob = offsetof(kib_msg_t, ibm_u) + body_nob;
	struct ib_mr      *mr;

	LASSERT(tx->tx_nwrq >= 0);
	LASSERT(tx->tx_nwrq < IBLND_MAX_RDMA_FRAGS + 1);
	LASSERT(nob <= IBLND_MSG_SIZE);

	kiblnd_init_msg(tx->tx_msg, type, body_nob);

	mr = kiblnd_find_dma_mr(hdev, tx->tx_msgaddr, nob);
	LASSERT(mr != NULL);

	sge->lkey   = mr->lkey;
	sge->addr   = tx->tx_msgaddr;
	sge->length = nob;

	memset(wrq, 0, sizeof(*wrq));

	wrq->next       = NULL;
	wrq->wr_id      = kiblnd_ptr2wreqid(tx, IBLND_WID_TX);
	wrq->sg_list    = sge;
	wrq->num_sge    = 1;
	wrq->opcode     = IB_WR_SEND;
	wrq->send_flags = IB_SEND_SIGNALED;

	tx->tx_nwrq++;
}

int
kiblnd_init_rdma(kib_conn_t *conn, kib_tx_t *tx, int type,
		  int resid, kib_rdma_desc_t *dstrd, __u64 dstcookie)
{
	kib_msg_t	 *ibmsg = tx->tx_msg;
	kib_rdma_desc_t   *srcrd = tx->tx_rd;
	struct ib_sge     *sge = &tx->tx_sge[0];
	struct ib_send_wr *wrq = &tx->tx_wrq[0];
	int		rc  = resid;
	int		srcidx;
	int		dstidx;
	int		wrknob;

	LASSERT(!in_interrupt());
	LASSERT(tx->tx_nwrq == 0);
	LASSERT(type == IBLND_MSG_GET_DONE ||
		 type == IBLND_MSG_PUT_DONE);

	srcidx = dstidx = 0;

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

		if (tx->tx_nwrq == IBLND_RDMA_FRAGS(conn->ibc_version)) {
			CERROR("RDMA too fragmented for %s (%d): %d/%d src %d/%d dst frags\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid),
			       IBLND_RDMA_FRAGS(conn->ibc_version),
			       srcidx, srcrd->rd_nfrags,
			       dstidx, dstrd->rd_nfrags);
			rc = -EMSGSIZE;
			break;
		}

		wrknob = min(min(kiblnd_rd_frag_size(srcrd, srcidx),
				 kiblnd_rd_frag_size(dstrd, dstidx)),
			     (__u32) resid);

		sge = &tx->tx_sge[tx->tx_nwrq];
		sge->addr   = kiblnd_rd_frag_addr(srcrd, srcidx);
		sge->lkey   = kiblnd_rd_frag_key(srcrd, srcidx);
		sge->length = wrknob;

		wrq = &tx->tx_wrq[tx->tx_nwrq];

		wrq->next       = wrq + 1;
		wrq->wr_id      = kiblnd_ptr2wreqid(tx, IBLND_WID_RDMA);
		wrq->sg_list    = sge;
		wrq->num_sge    = 1;
		wrq->opcode     = IB_WR_RDMA_WRITE;
		wrq->send_flags = 0;

		wrq->wr.rdma.remote_addr = kiblnd_rd_frag_addr(dstrd, dstidx);
		wrq->wr.rdma.rkey	= kiblnd_rd_frag_key(dstrd, dstidx);

		srcidx = kiblnd_rd_consume_frag(srcrd, srcidx, wrknob);
		dstidx = kiblnd_rd_consume_frag(dstrd, dstidx, wrknob);

		resid -= wrknob;

		tx->tx_nwrq++;
		wrq++;
		sge++;
	}

	if (rc < 0)			     /* no RDMA if completing with failure */
		tx->tx_nwrq = 0;

	ibmsg->ibm_u.completion.ibcm_status = rc;
	ibmsg->ibm_u.completion.ibcm_cookie = dstcookie;
	kiblnd_init_tx_msg(conn->ibc_peer->ibp_ni, tx,
			   type, sizeof(kib_completion_msg_t));

	return rc;
}

void
kiblnd_queue_tx_locked(kib_tx_t *tx, kib_conn_t *conn)
{
	struct list_head   *q;

	LASSERT(tx->tx_nwrq > 0);	      /* work items set up */
	LASSERT(!tx->tx_queued);	       /* not queued for sending already */
	LASSERT(conn->ibc_state >= IBLND_CONN_ESTABLISHED);

	tx->tx_queued = 1;
	tx->tx_deadline = jiffies + (*kiblnd_tunables.kib_timeout * HZ);

	if (tx->tx_conn == NULL) {
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

void
kiblnd_queue_tx(kib_tx_t *tx, kib_conn_t *conn)
{
	spin_lock(&conn->ibc_lock);
	kiblnd_queue_tx_locked(tx, conn);
	spin_unlock(&conn->ibc_lock);

	kiblnd_check_sends(conn);
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
	if (rc != 0) {
		CERROR("Unable to set reuse on cmid: %d\n", rc);
		return rc;
	}

	/* look for a free privileged port */
	for (port = PROT_SOCK-1; port > 0; port--) {
		srcaddr->sin_port = htons(port);
		rc = rdma_resolve_addr(cmid,
				       (struct sockaddr *)srcaddr,
				       (struct sockaddr *)dstaddr,
				       timeout_ms);
		if (rc == 0) {
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
kiblnd_connect_peer(kib_peer_t *peer)
{
	struct rdma_cm_id *cmid;
	kib_dev_t	 *dev;
	kib_net_t	 *net = peer->ibp_ni->ni_data;
	struct sockaddr_in srcaddr;
	struct sockaddr_in dstaddr;
	int		rc;

	LASSERT(net != NULL);
	LASSERT(peer->ibp_connecting > 0);

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
	if (rc != 0) {
		/* Can't initiate address resolution:  */
		CERROR("Can't resolve addr for %s: %d\n",
		       libcfs_nid2str(peer->ibp_nid), rc);
		goto failed2;
	}

	LASSERT(cmid->device != NULL);
	CDEBUG(D_NET, "%s: connection bound to %s:%pI4h:%s\n",
	       libcfs_nid2str(peer->ibp_nid), dev->ibd_ifname,
	       &dev->ibd_ifip, cmid->device->name);

	return;

 failed2:
	kiblnd_peer_decref(peer);	       /* cmid's ref */
	rdma_destroy_id(cmid);
 failed:
	kiblnd_peer_connect_failed(peer, 1, rc);
}

void
kiblnd_launch_tx(lnet_ni_t *ni, kib_tx_t *tx, lnet_nid_t nid)
{
	kib_peer_t	*peer;
	kib_peer_t	*peer2;
	kib_conn_t	*conn;
	rwlock_t	*g_lock = &kiblnd_data.kib_global_lock;
	unsigned long      flags;
	int		rc;

	/* If I get here, I've committed to send, so I complete the tx with
	 * failure on any problems */

	LASSERT(tx == NULL || tx->tx_conn == NULL); /* only set when assigned a conn */
	LASSERT(tx == NULL || tx->tx_nwrq > 0);     /* work items have been set up */

	/* First time, just use a read lock since I expect to find my peer
	 * connected */
	read_lock_irqsave(g_lock, flags);

	peer = kiblnd_find_peer_locked(nid);
	if (peer != NULL && !list_empty(&peer->ibp_conns)) {
		/* Found a peer with an established connection */
		conn = kiblnd_get_conn_locked(peer);
		kiblnd_conn_addref(conn); /* 1 ref for me... */

		read_unlock_irqrestore(g_lock, flags);

		if (tx != NULL)
			kiblnd_queue_tx(tx, conn);
		kiblnd_conn_decref(conn); /* ...to here */
		return;
	}

	read_unlock(g_lock);
	/* Re-try with a write lock */
	write_lock(g_lock);

	peer = kiblnd_find_peer_locked(nid);
	if (peer != NULL) {
		if (list_empty(&peer->ibp_conns)) {
			/* found a peer, but it's still connecting... */
			LASSERT(peer->ibp_connecting != 0 ||
				 peer->ibp_accepting != 0);
			if (tx != NULL)
				list_add_tail(&tx->tx_list,
						  &peer->ibp_tx_queue);
			write_unlock_irqrestore(g_lock, flags);
		} else {
			conn = kiblnd_get_conn_locked(peer);
			kiblnd_conn_addref(conn); /* 1 ref for me... */

			write_unlock_irqrestore(g_lock, flags);

			if (tx != NULL)
				kiblnd_queue_tx(tx, conn);
			kiblnd_conn_decref(conn); /* ...to here */
		}
		return;
	}

	write_unlock_irqrestore(g_lock, flags);

	/* Allocate a peer ready to add to the peer table and retry */
	rc = kiblnd_create_peer(ni, &peer, nid);
	if (rc != 0) {
		CERROR("Can't create peer %s\n", libcfs_nid2str(nid));
		if (tx != NULL) {
			tx->tx_status = -EHOSTUNREACH;
			tx->tx_waiting = 0;
			kiblnd_tx_done(ni, tx);
		}
		return;
	}

	write_lock_irqsave(g_lock, flags);

	peer2 = kiblnd_find_peer_locked(nid);
	if (peer2 != NULL) {
		if (list_empty(&peer2->ibp_conns)) {
			/* found a peer, but it's still connecting... */
			LASSERT(peer2->ibp_connecting != 0 ||
				 peer2->ibp_accepting != 0);
			if (tx != NULL)
				list_add_tail(&tx->tx_list,
						  &peer2->ibp_tx_queue);
			write_unlock_irqrestore(g_lock, flags);
		} else {
			conn = kiblnd_get_conn_locked(peer2);
			kiblnd_conn_addref(conn); /* 1 ref for me... */

			write_unlock_irqrestore(g_lock, flags);

			if (tx != NULL)
				kiblnd_queue_tx(tx, conn);
			kiblnd_conn_decref(conn); /* ...to here */
		}

		kiblnd_peer_decref(peer);
		return;
	}

	/* Brand new peer */
	LASSERT(peer->ibp_connecting == 0);
	peer->ibp_connecting = 1;

	/* always called with a ref on ni, which prevents ni being shutdown */
	LASSERT(((kib_net_t *)ni->ni_data)->ibn_shutdown == 0);

	if (tx != NULL)
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
	lnet_hdr_t       *hdr = &lntmsg->msg_hdr;
	int	       type = lntmsg->msg_type;
	lnet_process_id_t target = lntmsg->msg_target;
	int	       target_is_router = lntmsg->msg_target_is_router;
	int	       routing = lntmsg->msg_routing;
	unsigned int      payload_niov = lntmsg->msg_niov;
	struct kvec      *payload_iov = lntmsg->msg_iov;
	lnet_kiov_t      *payload_kiov = lntmsg->msg_kiov;
	unsigned int      payload_offset = lntmsg->msg_offset;
	unsigned int      payload_nob = lntmsg->msg_len;
	kib_msg_t	*ibmsg;
	kib_tx_t	 *tx;
	int	       nob;
	int	       rc;

	/* NB 'private' is different depending on what we're sending.... */

	CDEBUG(D_NET, "sending %d bytes in %d frags to %s\n",
	       payload_nob, payload_niov, libcfs_id2str(target));

	LASSERT(payload_nob == 0 || payload_niov > 0);
	LASSERT(payload_niov <= LNET_MAX_IOV);

	/* Thread context */
	LASSERT(!in_interrupt());
	/* payload is either all vaddrs or all pages */
	LASSERT(!(payload_kiov != NULL && payload_iov != NULL));

	switch (type) {
	default:
		LBUG();
		return -EIO;

	case LNET_MSG_ACK:
		LASSERT(payload_nob == 0);
		break;

	case LNET_MSG_GET:
		if (routing || target_is_router)
			break;		  /* send IMMEDIATE */

		/* is the REPLY message too small for RDMA? */
		nob = offsetof(kib_msg_t, ibm_u.immediate.ibim_payload[lntmsg->msg_md->md_length]);
		if (nob <= IBLND_MSG_SIZE)
			break;		  /* send IMMEDIATE */

		tx = kiblnd_get_idle_tx(ni, target.nid);
		if (tx == NULL) {
			CERROR("Can't allocate txd for GET to %s\n",
			       libcfs_nid2str(target.nid));
			return -ENOMEM;
		}

		ibmsg = tx->tx_msg;

		if ((lntmsg->msg_md->md_options & LNET_MD_KIOV) == 0)
			rc = kiblnd_setup_rd_iov(ni, tx,
						 &ibmsg->ibm_u.get.ibgm_rd,
						 lntmsg->msg_md->md_niov,
						 lntmsg->msg_md->md_iov.iov,
						 0, lntmsg->msg_md->md_length);
		else
			rc = kiblnd_setup_rd_kiov(ni, tx,
						  &ibmsg->ibm_u.get.ibgm_rd,
						  lntmsg->msg_md->md_niov,
						  lntmsg->msg_md->md_iov.kiov,
						  0, lntmsg->msg_md->md_length);
		if (rc != 0) {
			CERROR("Can't setup GET sink for %s: %d\n",
			       libcfs_nid2str(target.nid), rc);
			kiblnd_tx_done(ni, tx);
			return -EIO;
		}

		nob = offsetof(kib_get_msg_t, ibgm_rd.rd_frags[tx->tx_nfrags]);
		ibmsg->ibm_u.get.ibgm_cookie = tx->tx_cookie;
		ibmsg->ibm_u.get.ibgm_hdr = *hdr;

		kiblnd_init_tx_msg(ni, tx, IBLND_MSG_GET_REQ, nob);

		tx->tx_lntmsg[1] = lnet_create_reply_msg(ni, lntmsg);
		if (tx->tx_lntmsg[1] == NULL) {
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
		nob = offsetof(kib_msg_t, ibm_u.immediate.ibim_payload[payload_nob]);
		if (nob <= IBLND_MSG_SIZE)
			break;		  /* send IMMEDIATE */

		tx = kiblnd_get_idle_tx(ni, target.nid);
		if (tx == NULL) {
			CERROR("Can't allocate %s txd for %s\n",
			       type == LNET_MSG_PUT ? "PUT" : "REPLY",
			       libcfs_nid2str(target.nid));
			return -ENOMEM;
		}

		if (payload_kiov == NULL)
			rc = kiblnd_setup_rd_iov(ni, tx, tx->tx_rd,
						 payload_niov, payload_iov,
						 payload_offset, payload_nob);
		else
			rc = kiblnd_setup_rd_kiov(ni, tx, tx->tx_rd,
						  payload_niov, payload_kiov,
						  payload_offset, payload_nob);
		if (rc != 0) {
			CERROR("Can't setup PUT src for %s: %d\n",
			       libcfs_nid2str(target.nid), rc);
			kiblnd_tx_done(ni, tx);
			return -EIO;
		}

		ibmsg = tx->tx_msg;
		ibmsg->ibm_u.putreq.ibprm_hdr = *hdr;
		ibmsg->ibm_u.putreq.ibprm_cookie = tx->tx_cookie;
		kiblnd_init_tx_msg(ni, tx, IBLND_MSG_PUT_REQ, sizeof(kib_putreq_msg_t));

		tx->tx_lntmsg[0] = lntmsg;      /* finalise lntmsg on completion */
		tx->tx_waiting = 1;	     /* waiting for PUT_{ACK,NAK} */
		kiblnd_launch_tx(ni, tx, target.nid);
		return 0;
	}

	/* send IMMEDIATE */

	LASSERT(offsetof(kib_msg_t, ibm_u.immediate.ibim_payload[payload_nob])
		 <= IBLND_MSG_SIZE);

	tx = kiblnd_get_idle_tx(ni, target.nid);
	if (tx == NULL) {
		CERROR("Can't send %d to %s: tx descs exhausted\n",
			type, libcfs_nid2str(target.nid));
		return -ENOMEM;
	}

	ibmsg = tx->tx_msg;
	ibmsg->ibm_u.immediate.ibim_hdr = *hdr;

	if (payload_kiov != NULL)
		lnet_copy_kiov2flat(IBLND_MSG_SIZE, ibmsg,
				    offsetof(kib_msg_t, ibm_u.immediate.ibim_payload),
				    payload_niov, payload_kiov,
				    payload_offset, payload_nob);
	else
		lnet_copy_iov2flat(IBLND_MSG_SIZE, ibmsg,
				   offsetof(kib_msg_t, ibm_u.immediate.ibim_payload),
				   payload_niov, payload_iov,
				   payload_offset, payload_nob);

	nob = offsetof(kib_immediate_msg_t, ibim_payload[payload_nob]);
	kiblnd_init_tx_msg(ni, tx, IBLND_MSG_IMMEDIATE, nob);

	tx->tx_lntmsg[0] = lntmsg;	      /* finalise lntmsg on completion */
	kiblnd_launch_tx(ni, tx, target.nid);
	return 0;
}

static void
kiblnd_reply(lnet_ni_t *ni, kib_rx_t *rx, lnet_msg_t *lntmsg)
{
	lnet_process_id_t target = lntmsg->msg_target;
	unsigned int      niov = lntmsg->msg_niov;
	struct kvec      *iov = lntmsg->msg_iov;
	lnet_kiov_t      *kiov = lntmsg->msg_kiov;
	unsigned int      offset = lntmsg->msg_offset;
	unsigned int      nob = lntmsg->msg_len;
	kib_tx_t	 *tx;
	int	       rc;

	tx = kiblnd_get_idle_tx(ni, rx->rx_conn->ibc_peer->ibp_nid);
	if (tx == NULL) {
		CERROR("Can't get tx for REPLY to %s\n",
		       libcfs_nid2str(target.nid));
		goto failed_0;
	}

	if (nob == 0)
		rc = 0;
	else if (kiov == NULL)
		rc = kiblnd_setup_rd_iov(ni, tx, tx->tx_rd,
					 niov, iov, offset, nob);
	else
		rc = kiblnd_setup_rd_kiov(ni, tx, tx->tx_rd,
					  niov, kiov, offset, nob);

	if (rc != 0) {
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

	if (nob == 0) {
		/* No RDMA: local completion may happen now! */
		lnet_finalize(ni, lntmsg, 0);
	} else {
		/* RDMA: lnet_finalize(lntmsg) when it
		 * completes */
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
	     unsigned int niov, struct kvec *iov, lnet_kiov_t *kiov,
	     unsigned int offset, unsigned int mlen, unsigned int rlen)
{
	kib_rx_t    *rx = private;
	kib_msg_t   *rxmsg = rx->rx_msg;
	kib_conn_t  *conn = rx->rx_conn;
	kib_tx_t    *tx;
	kib_msg_t   *txmsg;
	int	  nob;
	int	  post_credit = IBLND_POSTRX_PEER_CREDIT;
	int	  rc = 0;

	LASSERT(mlen <= rlen);
	LASSERT(!in_interrupt());
	/* Either all pages or all vaddrs */
	LASSERT(!(kiov != NULL && iov != NULL));

	switch (rxmsg->ibm_type) {
	default:
		LBUG();

	case IBLND_MSG_IMMEDIATE:
		nob = offsetof(kib_msg_t, ibm_u.immediate.ibim_payload[rlen]);
		if (nob > rx->rx_nob) {
			CERROR("Immediate message from %s too big: %d(%d)\n",
				libcfs_nid2str(rxmsg->ibm_u.immediate.ibim_hdr.src_nid),
				nob, rx->rx_nob);
			rc = -EPROTO;
			break;
		}

		if (kiov != NULL)
			lnet_copy_flat2kiov(niov, kiov, offset,
					    IBLND_MSG_SIZE, rxmsg,
					    offsetof(kib_msg_t, ibm_u.immediate.ibim_payload),
					    mlen);
		else
			lnet_copy_flat2iov(niov, iov, offset,
					   IBLND_MSG_SIZE, rxmsg,
					   offsetof(kib_msg_t, ibm_u.immediate.ibim_payload),
					   mlen);
		lnet_finalize(ni, lntmsg, 0);
		break;

	case IBLND_MSG_PUT_REQ:
		if (mlen == 0) {
			lnet_finalize(ni, lntmsg, 0);
			kiblnd_send_completion(rx->rx_conn, IBLND_MSG_PUT_NAK, 0,
					       rxmsg->ibm_u.putreq.ibprm_cookie);
			break;
		}

		tx = kiblnd_get_idle_tx(ni, conn->ibc_peer->ibp_nid);
		if (tx == NULL) {
			CERROR("Can't allocate tx for %s\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid));
			/* Not replying will break the connection */
			rc = -ENOMEM;
			break;
		}

		txmsg = tx->tx_msg;
		if (kiov == NULL)
			rc = kiblnd_setup_rd_iov(ni, tx,
						 &txmsg->ibm_u.putack.ibpam_rd,
						 niov, iov, offset, mlen);
		else
			rc = kiblnd_setup_rd_kiov(ni, tx,
						  &txmsg->ibm_u.putack.ibpam_rd,
						  niov, kiov, offset, mlen);
		if (rc != 0) {
			CERROR("Can't setup PUT sink for %s: %d\n",
			       libcfs_nid2str(conn->ibc_peer->ibp_nid), rc);
			kiblnd_tx_done(ni, tx);
			/* tell peer it's over */
			kiblnd_send_completion(rx->rx_conn, IBLND_MSG_PUT_NAK, rc,
					       rxmsg->ibm_u.putreq.ibprm_cookie);
			break;
		}

		nob = offsetof(kib_putack_msg_t, ibpam_rd.rd_frags[tx->tx_nfrags]);
		txmsg->ibm_u.putack.ibpam_src_cookie = rxmsg->ibm_u.putreq.ibprm_cookie;
		txmsg->ibm_u.putack.ibpam_dst_cookie = tx->tx_cookie;

		kiblnd_init_tx_msg(ni, tx, IBLND_MSG_PUT_ACK, nob);

		tx->tx_lntmsg[0] = lntmsg;      /* finalise lntmsg on completion */
		tx->tx_waiting = 1;	     /* waiting for PUT_DONE */
		kiblnd_queue_tx(tx, conn);

		/* reposted buffer reserved for PUT_DONE */
		post_credit = IBLND_POSTRX_NO_CREDIT;
		break;

	case IBLND_MSG_GET_REQ:
		if (lntmsg != NULL) {
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

void
kiblnd_peer_alive(kib_peer_t *peer)
{
	/* This is racy, but everyone's only writing cfs_time_current() */
	peer->ibp_last_alive = cfs_time_current();
	mb();
}

static void
kiblnd_peer_notify(kib_peer_t *peer)
{
	int	   error = 0;
	unsigned long    last_alive = 0;
	unsigned long flags;

	read_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	if (list_empty(&peer->ibp_conns) &&
	    peer->ibp_accepting == 0 &&
	    peer->ibp_connecting == 0 &&
	    peer->ibp_error != 0) {
		error = peer->ibp_error;
		peer->ibp_error = 0;

		last_alive = peer->ibp_last_alive;
	}

	read_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	if (error != 0)
		lnet_notify(peer->ibp_ni,
			    peer->ibp_nid, 0, last_alive);
}

void
kiblnd_close_conn_locked(kib_conn_t *conn, int error)
{
	/* This just does the immediate housekeeping.  'error' is zero for a
	 * normal shutdown which can happen only after the connection has been
	 * established.  If the connection is established, schedule the
	 * connection to be finished off by the connd.  Otherwise the connd is
	 * already dealing with it (either to set it up or tear it down).
	 * Caller holds kib_global_lock exclusively in irq context */
	kib_peer_t       *peer = conn->ibc_peer;
	kib_dev_t	*dev;
	unsigned long     flags;

	LASSERT(error != 0 || conn->ibc_state >= IBLND_CONN_ESTABLISHED);

	if (error != 0 && conn->ibc_comms_error == 0)
		conn->ibc_comms_error = error;

	if (conn->ibc_state != IBLND_CONN_ESTABLISHED)
		return; /* already being handled  */

	if (error == 0 &&
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

	dev = ((kib_net_t *)peer->ibp_ni->ni_data)->ibn_dev;
	list_del(&conn->ibc_list);
	/* connd (see below) takes over ibc_list's ref */

	if (list_empty(&peer->ibp_conns) &&    /* no more conns */
	    kiblnd_peer_active(peer)) {	 /* still in peer table */
		kiblnd_unlink_peer_locked(peer);

		/* set/clear error on last conn */
		peer->ibp_error = conn->ibc_comms_error;
	}

	kiblnd_set_conn_state(conn, IBLND_CONN_CLOSING);

	if (error != 0 &&
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
kiblnd_close_conn(kib_conn_t *conn, int error)
{
	unsigned long flags;

	write_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	kiblnd_close_conn_locked(conn, error);

	write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);
}

static void
kiblnd_handle_early_rxs(kib_conn_t *conn)
{
	unsigned long    flags;
	kib_rx_t	*rx;
	kib_rx_t *tmp;

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
kiblnd_abort_txs(kib_conn_t *conn, struct list_head *txs)
{
	LIST_HEAD(zombies);
	struct list_head	  *tmp;
	struct list_head	  *nxt;
	kib_tx_t	    *tx;

	spin_lock(&conn->ibc_lock);

	list_for_each_safe(tmp, nxt, txs) {
		tx = list_entry(tmp, kib_tx_t, tx_list);

		if (txs == &conn->ibc_active_txs) {
			LASSERT(!tx->tx_queued);
			LASSERT(tx->tx_waiting ||
				 tx->tx_sending != 0);
		} else {
			LASSERT(tx->tx_queued);
		}

		tx->tx_status = -ECONNABORTED;
		tx->tx_waiting = 0;

		if (tx->tx_sending == 0) {
			tx->tx_queued = 0;
			list_del(&tx->tx_list);
			list_add(&tx->tx_list, &zombies);
		}
	}

	spin_unlock(&conn->ibc_lock);

	kiblnd_txlist_done(conn->ibc_peer->ibp_ni, &zombies, -ECONNABORTED);
}

static void
kiblnd_finalise_conn(kib_conn_t *conn)
{
	LASSERT(!in_interrupt());
	LASSERT(conn->ibc_state > IBLND_CONN_INIT);

	kiblnd_set_conn_state(conn, IBLND_CONN_DISCONNECTED);

	/* abort_receives moves QP state to IB_QPS_ERR.  This is only required
	 * for connections that didn't get as far as being connected, because
	 * rdma_disconnect() does this for free. */
	kiblnd_abort_receives(conn);

	/* Complete all tx descs not waiting for sends to complete.
	 * NB we should be safe from RDMA now that the QP has changed state */

	kiblnd_abort_txs(conn, &conn->ibc_tx_noops);
	kiblnd_abort_txs(conn, &conn->ibc_tx_queue);
	kiblnd_abort_txs(conn, &conn->ibc_tx_queue_rsrvd);
	kiblnd_abort_txs(conn, &conn->ibc_tx_queue_nocred);
	kiblnd_abort_txs(conn, &conn->ibc_active_txs);

	kiblnd_handle_early_rxs(conn);
}

void
kiblnd_peer_connect_failed(kib_peer_t *peer, int active, int error)
{
	LIST_HEAD(zombies);
	unsigned long     flags;

	LASSERT(error != 0);
	LASSERT(!in_interrupt());

	write_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	if (active) {
		LASSERT(peer->ibp_connecting > 0);
		peer->ibp_connecting--;
	} else {
		LASSERT(peer->ibp_accepting > 0);
		peer->ibp_accepting--;
	}

	if (peer->ibp_connecting != 0 ||
	    peer->ibp_accepting != 0) {
		/* another connection attempt under way... */
		write_unlock_irqrestore(&kiblnd_data.kib_global_lock,
					    flags);
		return;
	}

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

void
kiblnd_connreq_done(kib_conn_t *conn, int status)
{
	kib_peer_t	*peer = conn->ibc_peer;
	kib_tx_t	  *tx;
	kib_tx_t *tmp;
	struct list_head	 txs;
	unsigned long      flags;
	int		active;

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

	if (status != 0) {
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

	/* Add conn to peer's list and nuke any dangling conns from a different
	 * peer instance... */
	kiblnd_conn_addref(conn);	       /* +1 ref for ibc_list */
	list_add(&conn->ibc_list, &peer->ibp_conns);
	if (active)
		peer->ibp_connecting--;
	else
		peer->ibp_accepting--;

	if (peer->ibp_version == 0) {
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
	    conn->ibc_comms_error != 0) {       /* error has happened already */
		lnet_ni_t *ni = peer->ibp_ni;

		/* start to shut down connection */
		kiblnd_close_conn_locked(conn, -ECONNABORTED);
		write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

		kiblnd_txlist_done(ni, &txs, -ECONNABORTED);

		return;
	}

	write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	/* Schedule blocked txs */
	spin_lock(&conn->ibc_lock);
	list_for_each_entry_safe(tx, tmp, &txs, tx_list) {
		list_del(&tx->tx_list);

		kiblnd_queue_tx_locked(tx, conn);
	}
	spin_unlock(&conn->ibc_lock);

	kiblnd_check_sends(conn);

	/* schedule blocked rxs */
	kiblnd_handle_early_rxs(conn);
}

static void
kiblnd_reject(struct rdma_cm_id *cmid, kib_rej_t *rej)
{
	int	  rc;

	rc = rdma_reject(cmid, rej, sizeof(*rej));

	if (rc != 0)
		CWARN("Error %d sending reject\n", rc);
}

static int
kiblnd_passive_connect(struct rdma_cm_id *cmid, void *priv, int priv_nob)
{
	rwlock_t		*g_lock = &kiblnd_data.kib_global_lock;
	kib_msg_t	     *reqmsg = priv;
	kib_msg_t	     *ackmsg;
	kib_dev_t	     *ibdev;
	kib_peer_t	    *peer;
	kib_peer_t	    *peer2;
	kib_conn_t	    *conn;
	lnet_ni_t	     *ni  = NULL;
	kib_net_t	     *net = NULL;
	lnet_nid_t	     nid;
	struct rdma_conn_param cp;
	kib_rej_t	      rej;
	int		    version = IBLND_MSG_VERSION;
	unsigned long	  flags;
	int		    rc;
	struct sockaddr_in    *peer_addr;
	LASSERT(!in_interrupt());

	/* cmid inherits 'context' from the corresponding listener id */
	ibdev = (kib_dev_t *)cmid->context;
	LASSERT(ibdev != NULL);

	memset(&rej, 0, sizeof(rej));
	rej.ibr_magic		= IBLND_MSG_MAGIC;
	rej.ibr_why		  = IBLND_REJECT_FATAL;
	rej.ibr_cp.ibcp_max_msg_size = IBLND_MSG_SIZE;

	peer_addr = (struct sockaddr_in *)&(cmid->route.addr.dst_addr);
	if (*kiblnd_tunables.kib_require_priv_port &&
	    ntohs(peer_addr->sin_port) >= PROT_SOCK) {
		__u32 ip = ntohl(peer_addr->sin_addr.s_addr);
		CERROR("Peer's port (%pI4h:%hu) is not privileged\n",
		       &ip, ntohs(peer_addr->sin_port));
		goto failed;
	}

	if (priv_nob < offsetof(kib_msg_t, ibm_type)) {
		CERROR("Short connection request\n");
		goto failed;
	}

	/* Future protocol version compatibility support!  If the
	 * o2iblnd-specific protocol changes, or when LNET unifies
	 * protocols over all LNDs, the initial connection will
	 * negotiate a protocol version.  I trap this here to avoid
	 * console errors; the reject tells the peer which protocol I
	 * speak. */
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
	if (rc != 0) {
		CERROR("Can't parse connection request: %d\n", rc);
		goto failed;
	}

	nid = reqmsg->ibm_srcnid;
	ni  = lnet_net2ni(LNET_NIDNET(reqmsg->ibm_dstnid));

	if (ni != NULL) {
		net = (kib_net_t *)ni->ni_data;
		rej.ibr_incarnation = net->ibn_incarnation;
	}

	if (ni == NULL ||			 /* no matching net */
	    ni->ni_nid != reqmsg->ibm_dstnid ||   /* right NET, wrong NID! */
	    net->ibn_dev != ibdev) {	      /* wrong device */
		CERROR("Can't accept %s on %s (%s:%d:%pI4h): bad dst nid %s\n",
		       libcfs_nid2str(nid),
		       ni == NULL ? "NA" : libcfs_nid2str(ni->ni_nid),
		       ibdev->ibd_ifname, ibdev->ibd_nnets,
		       &ibdev->ibd_ifip,
		       libcfs_nid2str(reqmsg->ibm_dstnid));

		goto failed;
	}

       /* check time stamp as soon as possible */
	if (reqmsg->ibm_dststamp != 0 &&
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

	if (reqmsg->ibm_u.connparams.ibcp_queue_depth !=
	    IBLND_MSG_QUEUE_SIZE(version)) {
		CERROR("Can't accept %s: incompatible queue depth %d (%d wanted)\n",
		       libcfs_nid2str(nid), reqmsg->ibm_u.connparams.ibcp_queue_depth,
		       IBLND_MSG_QUEUE_SIZE(version));

		if (version == IBLND_MSG_VERSION)
			rej.ibr_why = IBLND_REJECT_MSG_QUEUE_SIZE;

		goto failed;
	}

	if (reqmsg->ibm_u.connparams.ibcp_max_frags !=
	    IBLND_RDMA_FRAGS(version)) {
		CERROR("Can't accept %s(version %x): incompatible max_frags %d (%d wanted)\n",
		       libcfs_nid2str(nid), version,
		       reqmsg->ibm_u.connparams.ibcp_max_frags,
		       IBLND_RDMA_FRAGS(version));

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
	if (rc != 0) {
		CERROR("Can't create peer for %s\n", libcfs_nid2str(nid));
		rej.ibr_why = IBLND_REJECT_NO_RESOURCES;
		goto failed;
	}

	write_lock_irqsave(g_lock, flags);

	peer2 = kiblnd_find_peer_locked(nid);
	if (peer2 != NULL) {
		if (peer2->ibp_version == 0) {
			peer2->ibp_version     = version;
			peer2->ibp_incarnation = reqmsg->ibm_srcstamp;
		}

		/* not the guy I've talked with */
		if (peer2->ibp_incarnation != reqmsg->ibm_srcstamp ||
		    peer2->ibp_version     != version) {
			kiblnd_close_peer_conns_locked(peer2, -ESTALE);
			write_unlock_irqrestore(g_lock, flags);

			CWARN("Conn stale %s [old ver: %x, new ver: %x]\n",
			      libcfs_nid2str(nid), peer2->ibp_version, version);

			kiblnd_peer_decref(peer);
			rej.ibr_why = IBLND_REJECT_CONN_STALE;
			goto failed;
		}

		/* tie-break connection race in favour of the higher NID */
		if (peer2->ibp_connecting != 0 &&
		    nid < ni->ni_nid) {
			write_unlock_irqrestore(g_lock, flags);

			CWARN("Conn race %s\n", libcfs_nid2str(peer2->ibp_nid));

			kiblnd_peer_decref(peer);
			rej.ibr_why = IBLND_REJECT_CONN_RACE;
			goto failed;
		}

		peer2->ibp_accepting++;
		kiblnd_peer_addref(peer2);

		write_unlock_irqrestore(g_lock, flags);
		kiblnd_peer_decref(peer);
		peer = peer2;
	} else {
		/* Brand new peer */
		LASSERT(peer->ibp_accepting == 0);
		LASSERT(peer->ibp_version == 0 &&
			 peer->ibp_incarnation == 0);

		peer->ibp_accepting   = 1;
		peer->ibp_version     = version;
		peer->ibp_incarnation = reqmsg->ibm_srcstamp;

		/* I have a ref on ni that prevents it being shutdown */
		LASSERT(net->ibn_shutdown == 0);

		kiblnd_peer_addref(peer);
		list_add_tail(&peer->ibp_list, kiblnd_nid2peerlist(nid));

		write_unlock_irqrestore(g_lock, flags);
	}

	conn = kiblnd_create_conn(peer, cmid, IBLND_CONN_PASSIVE_WAIT, version);
	if (conn == NULL) {
		kiblnd_peer_connect_failed(peer, 0, -ENOMEM);
		kiblnd_peer_decref(peer);
		rej.ibr_why = IBLND_REJECT_NO_RESOURCES;
		goto failed;
	}

	/* conn now "owns" cmid, so I return success from here on to ensure the
	 * CM callback doesn't destroy cmid. */

	conn->ibc_incarnation      = reqmsg->ibm_srcstamp;
	conn->ibc_credits	  = IBLND_MSG_QUEUE_SIZE(version);
	conn->ibc_reserved_credits = IBLND_MSG_QUEUE_SIZE(version);
	LASSERT(conn->ibc_credits + conn->ibc_reserved_credits + IBLND_OOB_MSGS(version)
		 <= IBLND_RX_MSGS(version));

	ackmsg = &conn->ibc_connvars->cv_msg;
	memset(ackmsg, 0, sizeof(*ackmsg));

	kiblnd_init_msg(ackmsg, IBLND_MSG_CONNACK,
			sizeof(ackmsg->ibm_u.connparams));
	ackmsg->ibm_u.connparams.ibcp_queue_depth  = IBLND_MSG_QUEUE_SIZE(version);
	ackmsg->ibm_u.connparams.ibcp_max_msg_size = IBLND_MSG_SIZE;
	ackmsg->ibm_u.connparams.ibcp_max_frags    = IBLND_RDMA_FRAGS(version);

	kiblnd_pack_msg(ni, ackmsg, version, 0, nid, reqmsg->ibm_srcstamp);

	memset(&cp, 0, sizeof(cp));
	cp.private_data	= ackmsg;
	cp.private_data_len    = ackmsg->ibm_nob;
	cp.responder_resources = 0;	     /* No atomic ops or RDMA reads */
	cp.initiator_depth     = 0;
	cp.flow_control	= 1;
	cp.retry_count	 = *kiblnd_tunables.kib_retry_count;
	cp.rnr_retry_count     = *kiblnd_tunables.kib_rnr_retry_count;

	CDEBUG(D_NET, "Accept %s\n", libcfs_nid2str(nid));

	rc = rdma_accept(cmid, &cp);
	if (rc != 0) {
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
	if (ni != NULL)
		lnet_ni_decref(ni);

	rej.ibr_version = version;
	rej.ibr_cp.ibcp_queue_depth = IBLND_MSG_QUEUE_SIZE(version);
	rej.ibr_cp.ibcp_max_frags   = IBLND_RDMA_FRAGS(version);
	kiblnd_reject(cmid, &rej);

	return -ECONNREFUSED;
}

static void
kiblnd_reconnect(kib_conn_t *conn, int version,
		  __u64 incarnation, int why, kib_connparams_t *cp)
{
	kib_peer_t    *peer = conn->ibc_peer;
	char	  *reason;
	int	    retry = 0;
	unsigned long  flags;

	LASSERT(conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT);
	LASSERT(peer->ibp_connecting > 0);     /* 'conn' at least */

	write_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	/* retry connection if it's still needed and no other connection
	 * attempts (active or passive) are in progress
	 * NB: reconnect is still needed even when ibp_tx_queue is
	 * empty if ibp_version != version because reconnect may be
	 * initiated by kiblnd_query() */
	if ((!list_empty(&peer->ibp_tx_queue) ||
	     peer->ibp_version != version) &&
	    peer->ibp_connecting == 1 &&
	    peer->ibp_accepting == 0) {
		retry = 1;
		peer->ibp_connecting++;

		peer->ibp_version     = version;
		peer->ibp_incarnation = incarnation;
	}

	write_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	if (!retry)
		return;

	switch (why) {
	default:
		reason = "Unknown";
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

	CNETERR("%s: retrying (%s), %x, %x, queue_dep: %d, max_frag: %d, msg_size: %d\n",
		libcfs_nid2str(peer->ibp_nid),
		reason, IBLND_MSG_VERSION, version,
		cp != NULL ? cp->ibcp_queue_depth  : IBLND_MSG_QUEUE_SIZE(version),
		cp != NULL ? cp->ibcp_max_frags    : IBLND_RDMA_FRAGS(version),
		cp != NULL ? cp->ibcp_max_msg_size : IBLND_MSG_SIZE);

	kiblnd_connect_peer(peer);
}

static void
kiblnd_rejected(kib_conn_t *conn, int reason, void *priv, int priv_nob)
{
	kib_peer_t    *peer = conn->ibc_peer;

	LASSERT(!in_interrupt());
	LASSERT(conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT);

	switch (reason) {
	case IB_CM_REJ_STALE_CONN:
		kiblnd_reconnect(conn, IBLND_MSG_VERSION, 0,
				 IBLND_REJECT_CONN_STALE, NULL);
		break;

	case IB_CM_REJ_INVALID_SERVICE_ID:
		CNETERR("%s rejected: no listener at %d\n",
			libcfs_nid2str(peer->ibp_nid),
			*kiblnd_tunables.kib_service);
		break;

	case IB_CM_REJ_CONSUMER_DEFINED:
		if (priv_nob >= offsetof(kib_rej_t, ibr_padding)) {
			kib_rej_t	*rej	 = priv;
			kib_connparams_t *cp	  = NULL;
			int	       flip	= 0;
			__u64	     incarnation = -1;

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

			if (priv_nob >= sizeof(kib_rej_t) &&
			    rej->ibr_version > IBLND_MSG_VERSION_1) {
				/* priv_nob is always 148 in current version
				 * of OFED, so we still need to check version.
				 * (define of IB_CM_REJ_PRIVATE_DATA_SIZE) */
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
				kiblnd_reconnect(conn, rej->ibr_version,
						 incarnation, rej->ibr_why, cp);
				break;

			case IBLND_REJECT_MSG_QUEUE_SIZE:
				CERROR("%s rejected: incompatible message queue depth %d, %d\n",
				       libcfs_nid2str(peer->ibp_nid),
				       cp != NULL ? cp->ibcp_queue_depth :
				       IBLND_MSG_QUEUE_SIZE(rej->ibr_version),
				       IBLND_MSG_QUEUE_SIZE(conn->ibc_version));
				break;

			case IBLND_REJECT_RDMA_FRAGS:
				CERROR("%s rejected: incompatible # of RDMA fragments %d, %d\n",
				       libcfs_nid2str(peer->ibp_nid),
				       cp != NULL ? cp->ibcp_max_frags :
				       IBLND_RDMA_FRAGS(rej->ibr_version),
				       IBLND_RDMA_FRAGS(conn->ibc_version));
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
kiblnd_check_connreply(kib_conn_t *conn, void *priv, int priv_nob)
{
	kib_peer_t    *peer = conn->ibc_peer;
	lnet_ni_t     *ni   = peer->ibp_ni;
	kib_net_t     *net  = ni->ni_data;
	kib_msg_t     *msg  = priv;
	int	    ver  = conn->ibc_version;
	int	    rc   = kiblnd_unpack_msg(msg, priv_nob);
	unsigned long  flags;

	LASSERT(net != NULL);

	if (rc != 0) {
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

	if (msg->ibm_u.connparams.ibcp_queue_depth !=
	    IBLND_MSG_QUEUE_SIZE(ver)) {
		CERROR("%s has incompatible queue depth %d(%d wanted)\n",
		       libcfs_nid2str(peer->ibp_nid),
		       msg->ibm_u.connparams.ibcp_queue_depth,
		       IBLND_MSG_QUEUE_SIZE(ver));
		rc = -EPROTO;
		goto failed;
	}

	if (msg->ibm_u.connparams.ibcp_max_frags !=
	    IBLND_RDMA_FRAGS(ver)) {
		CERROR("%s has incompatible max_frags %d (%d wanted)\n",
		       libcfs_nid2str(peer->ibp_nid),
		       msg->ibm_u.connparams.ibcp_max_frags,
		       IBLND_RDMA_FRAGS(ver));
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

	if (rc != 0) {
		CERROR("Bad connection reply from %s, rc = %d, version: %x max_frags: %d\n",
		       libcfs_nid2str(peer->ibp_nid), rc,
		       msg->ibm_version, msg->ibm_u.connparams.ibcp_max_frags);
		goto failed;
	}

	conn->ibc_incarnation      = msg->ibm_srcstamp;
	conn->ibc_credits	  =
	conn->ibc_reserved_credits = IBLND_MSG_QUEUE_SIZE(ver);
	LASSERT(conn->ibc_credits + conn->ibc_reserved_credits + IBLND_OOB_MSGS(ver)
		 <= IBLND_RX_MSGS(ver));

	kiblnd_connreq_done(conn, 0);
	return;

 failed:
	/* NB My QP has already established itself, so I handle anything going
	 * wrong here by setting ibc_comms_error.
	 * kiblnd_connreq_done(0) moves the conn state to ESTABLISHED, but then
	 * immediately tears it down. */

	LASSERT(rc != 0);
	conn->ibc_comms_error = rc;
	kiblnd_connreq_done(conn, 0);
}

static int
kiblnd_active_connect(struct rdma_cm_id *cmid)
{
	kib_peer_t	      *peer = (kib_peer_t *)cmid->context;
	kib_conn_t	      *conn;
	kib_msg_t	       *msg;
	struct rdma_conn_param   cp;
	int		      version;
	__u64		    incarnation;
	unsigned long	    flags;
	int		      rc;

	read_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	incarnation = peer->ibp_incarnation;
	version     = (peer->ibp_version == 0) ? IBLND_MSG_VERSION :
						 peer->ibp_version;

	read_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	conn = kiblnd_create_conn(peer, cmid, IBLND_CONN_ACTIVE_CONNECT, version);
	if (conn == NULL) {
		kiblnd_peer_connect_failed(peer, 1, -ENOMEM);
		kiblnd_peer_decref(peer); /* lose cmid's ref */
		return -ENOMEM;
	}

	/* conn "owns" cmid now, so I return success from here on to ensure the
	 * CM callback doesn't destroy cmid. conn also takes over cmid's ref
	 * on peer */

	msg = &conn->ibc_connvars->cv_msg;

	memset(msg, 0, sizeof(*msg));
	kiblnd_init_msg(msg, IBLND_MSG_CONNREQ, sizeof(msg->ibm_u.connparams));
	msg->ibm_u.connparams.ibcp_queue_depth  = IBLND_MSG_QUEUE_SIZE(version);
	msg->ibm_u.connparams.ibcp_max_frags    = IBLND_RDMA_FRAGS(version);
	msg->ibm_u.connparams.ibcp_max_msg_size = IBLND_MSG_SIZE;

	kiblnd_pack_msg(peer->ibp_ni, msg, version,
			0, peer->ibp_nid, incarnation);

	memset(&cp, 0, sizeof(cp));
	cp.private_data	= msg;
	cp.private_data_len    = msg->ibm_nob;
	cp.responder_resources = 0;	     /* No atomic ops or RDMA reads */
	cp.initiator_depth     = 0;
	cp.flow_control	= 1;
	cp.retry_count	 = *kiblnd_tunables.kib_retry_count;
	cp.rnr_retry_count     = *kiblnd_tunables.kib_rnr_retry_count;

	LASSERT(cmid->context == (void *)conn);
	LASSERT(conn->ibc_cmid == cmid);

	rc = rdma_connect(cmid, &cp);
	if (rc != 0) {
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
	kib_peer_t  *peer;
	kib_conn_t  *conn;
	int	  rc;

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
		peer = (kib_peer_t *)cmid->context;
		CNETERR("%s: ADDR ERROR %d\n",
		       libcfs_nid2str(peer->ibp_nid), event->status);
		kiblnd_peer_connect_failed(peer, 1, -EHOSTUNREACH);
		kiblnd_peer_decref(peer);
		return -EHOSTUNREACH;      /* rc != 0 destroys cmid */

	case RDMA_CM_EVENT_ADDR_RESOLVED:
		peer = (kib_peer_t *)cmid->context;

		CDEBUG(D_NET, "%s Addr resolved: %d\n",
		       libcfs_nid2str(peer->ibp_nid), event->status);

		if (event->status != 0) {
			CNETERR("Can't resolve address for %s: %d\n",
				libcfs_nid2str(peer->ibp_nid), event->status);
			rc = event->status;
		} else {
			rc = rdma_resolve_route(
				cmid, *kiblnd_tunables.kib_timeout * 1000);
			if (rc == 0)
				return 0;
			/* Can't initiate route resolution */
			CERROR("Can't resolve route for %s: %d\n",
			       libcfs_nid2str(peer->ibp_nid), rc);
		}
		kiblnd_peer_connect_failed(peer, 1, rc);
		kiblnd_peer_decref(peer);
		return rc;		      /* rc != 0 destroys cmid */

	case RDMA_CM_EVENT_ROUTE_ERROR:
		peer = (kib_peer_t *)cmid->context;
		CNETERR("%s: ROUTE ERROR %d\n",
			libcfs_nid2str(peer->ibp_nid), event->status);
		kiblnd_peer_connect_failed(peer, 1, -EHOSTUNREACH);
		kiblnd_peer_decref(peer);
		return -EHOSTUNREACH;	   /* rc != 0 destroys cmid */

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		peer = (kib_peer_t *)cmid->context;
		CDEBUG(D_NET, "%s Route resolved: %d\n",
		       libcfs_nid2str(peer->ibp_nid), event->status);

		if (event->status == 0)
			return kiblnd_active_connect(cmid);

		CNETERR("Can't resolve route for %s: %d\n",
		       libcfs_nid2str(peer->ibp_nid), event->status);
		kiblnd_peer_connect_failed(peer, 1, event->status);
		kiblnd_peer_decref(peer);
		return event->status;	   /* rc != 0 destroys cmid */

	case RDMA_CM_EVENT_UNREACHABLE:
		conn = (kib_conn_t *)cmid->context;
		LASSERT(conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT ||
			conn->ibc_state == IBLND_CONN_PASSIVE_WAIT);
		CNETERR("%s: UNREACHABLE %d\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid), event->status);
		kiblnd_connreq_done(conn, -ENETDOWN);
		kiblnd_conn_decref(conn);
		return 0;

	case RDMA_CM_EVENT_CONNECT_ERROR:
		conn = (kib_conn_t *)cmid->context;
		LASSERT(conn->ibc_state == IBLND_CONN_ACTIVE_CONNECT ||
			conn->ibc_state == IBLND_CONN_PASSIVE_WAIT);
		CNETERR("%s: CONNECT ERROR %d\n",
			libcfs_nid2str(conn->ibc_peer->ibp_nid), event->status);
		kiblnd_connreq_done(conn, -ENOTCONN);
		kiblnd_conn_decref(conn);
		return 0;

	case RDMA_CM_EVENT_REJECTED:
		conn = (kib_conn_t *)cmid->context;
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
		conn = (kib_conn_t *)cmid->context;
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
		conn = (kib_conn_t *)cmid->context;
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
		/* Can't remove network from underneath LNET for now, so I have
		 * to ignore this */
		return 0;

	case RDMA_CM_EVENT_ADDR_CHANGE:
		LCONSOLE_INFO("Physical link changed (eg hca/port)\n");
		return 0;
	}
}

static int
kiblnd_check_txs_locked(kib_conn_t *conn, struct list_head *txs)
{
	kib_tx_t	  *tx;
	struct list_head	*ttmp;

	list_for_each(ttmp, txs) {
		tx = list_entry(ttmp, kib_tx_t, tx_list);

		if (txs != &conn->ibc_active_txs) {
			LASSERT(tx->tx_queued);
		} else {
			LASSERT(!tx->tx_queued);
			LASSERT(tx->tx_waiting || tx->tx_sending != 0);
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
kiblnd_conn_timed_out_locked(kib_conn_t *conn)
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
	struct list_head    *peers = &kiblnd_data.kib_peers[idx];
	struct list_head    *ptmp;
	kib_peer_t    *peer;
	kib_conn_t    *conn;
	kib_conn_t *tmp;
	struct list_head    *ctmp;
	unsigned long  flags;

	/* NB. We expect to have a look at all the peers and not find any
	 * RDMAs to time out, so we just use a shared lock while we
	 * take a look... */
	read_lock_irqsave(&kiblnd_data.kib_global_lock, flags);

	list_for_each(ptmp, peers) {
		peer = list_entry(ptmp, kib_peer_t, ibp_list);

		list_for_each(ctmp, &peer->ibp_conns) {
			int timedout;
			int sendnoop;

			conn = list_entry(ctmp, kib_conn_t, ibc_list);

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
				list_add(&conn->ibc_connd_list,
					     &checksends);
			}
			/* +ref for 'closes' or 'checksends' */
			kiblnd_conn_addref(conn);

			spin_unlock(&conn->ibc_lock);
		}
	}

	read_unlock_irqrestore(&kiblnd_data.kib_global_lock, flags);

	/* Handle timeout by closing the whole
	 * connection. We can only be sure RDMA activity
	 * has ceased once the QP has been modified. */
	list_for_each_entry_safe(conn, tmp, &closes, ibc_connd_list) {
		list_del(&conn->ibc_connd_list);
		kiblnd_close_conn(conn, -ETIMEDOUT);
		kiblnd_conn_decref(conn);
	}

	/* In case we have enough credits to return via a
	 * NOOP, but there were no non-blocking tx descs
	 * free to do it last time... */
	while (!list_empty(&checksends)) {
		conn = list_entry(checksends.next,
				      kib_conn_t, ibc_connd_list);
		list_del(&conn->ibc_connd_list);
		kiblnd_check_sends(conn);
		kiblnd_conn_decref(conn);
	}
}

static void
kiblnd_disconnect_conn(kib_conn_t *conn)
{
	LASSERT(!in_interrupt());
	LASSERT(current == kiblnd_data.kib_connd);
	LASSERT(conn->ibc_state == IBLND_CONN_CLOSING);

	rdma_disconnect(conn->ibc_cmid);
	kiblnd_finalise_conn(conn);

	kiblnd_peer_notify(conn->ibc_peer);
}

int
kiblnd_connd(void *arg)
{
	wait_queue_t     wait;
	unsigned long      flags;
	kib_conn_t	*conn;
	int		timeout;
	int		i;
	int		dropped_lock;
	int		peer_index = 0;
	unsigned long      deadline = jiffies;

	cfs_block_allsigs();

	init_waitqueue_entry(&wait, current);
	kiblnd_data.kib_connd = current;

	spin_lock_irqsave(&kiblnd_data.kib_connd_lock, flags);

	while (!kiblnd_data.kib_shutdown) {

		dropped_lock = 0;

		if (!list_empty(&kiblnd_data.kib_connd_zombies)) {
			conn = list_entry(kiblnd_data. \
					      kib_connd_zombies.next,
					      kib_conn_t, ibc_list);
			list_del(&conn->ibc_list);

			spin_unlock_irqrestore(&kiblnd_data.kib_connd_lock,
					       flags);
			dropped_lock = 1;

			kiblnd_destroy_conn(conn);

			spin_lock_irqsave(&kiblnd_data.kib_connd_lock, flags);
		}

		if (!list_empty(&kiblnd_data.kib_connd_conns)) {
			conn = list_entry(kiblnd_data.kib_connd_conns.next,
					      kib_conn_t, ibc_list);
			list_del(&conn->ibc_list);

			spin_unlock_irqrestore(&kiblnd_data.kib_connd_lock,
					       flags);
			dropped_lock = 1;

			kiblnd_disconnect_conn(conn);
			kiblnd_conn_decref(conn);

			spin_lock_irqsave(&kiblnd_data.kib_connd_lock, flags);
		}

		/* careful with the jiffy wrap... */
		timeout = (int)(deadline - jiffies);
		if (timeout <= 0) {
			const int n = 4;
			const int p = 1;
			int       chunk = kiblnd_data.kib_peer_hash_size;

			spin_unlock_irqrestore(&kiblnd_data.kib_connd_lock, flags);
			dropped_lock = 1;

			/* Time to check for RDMA timeouts on a few more
			 * peers: I do checks every 'p' seconds on a
			 * proportion of the peer table and I need to check
			 * every connection 'n' times within a timeout
			 * interval, to ensure I detect a timeout on any
			 * connection within (n+1)/n times the timeout
			 * interval. */

			if (*kiblnd_tunables.kib_timeout > n * p)
				chunk = (chunk * n * p) /
					*kiblnd_tunables.kib_timeout;
			if (chunk == 0)
				chunk = 1;

			for (i = 0; i < chunk; i++) {
				kiblnd_check_conns(peer_index);
				peer_index = (peer_index + 1) %
					     kiblnd_data.kib_peer_hash_size;
			}

			deadline += p * HZ;
			spin_lock_irqsave(&kiblnd_data.kib_connd_lock, flags);
		}

		if (dropped_lock)
			continue;

		/* Nothing to do for 'timeout'  */
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&kiblnd_data.kib_connd_waitq, &wait);
		spin_unlock_irqrestore(&kiblnd_data.kib_connd_lock, flags);

		schedule_timeout(timeout);

		remove_wait_queue(&kiblnd_data.kib_connd_waitq, &wait);
		spin_lock_irqsave(&kiblnd_data.kib_connd_lock, flags);
	}

	spin_unlock_irqrestore(&kiblnd_data.kib_connd_lock, flags);

	kiblnd_thread_fini();
	return 0;
}

void
kiblnd_qp_event(struct ib_event *event, void *arg)
{
	kib_conn_t *conn = arg;

	switch (event->event) {
	case IB_EVENT_COMM_EST:
		CDEBUG(D_NET, "%s established\n",
		       libcfs_nid2str(conn->ibc_peer->ibp_nid));
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

	case IBLND_WID_RDMA:
		/* We only get RDMA completion notification if it fails.  All
		 * subsequent work items, including the final SEND will fail
		 * too.  However we can't print out any more info about the
		 * failing RDMA because 'tx' might be back on the idle list or
		 * even reused already if we didn't manage to post all our work
		 * items */
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
	/* NB I'm not allowed to schedule this conn once its refcount has
	 * reached 0.  Since fundamentally I'm racing with scheduler threads
	 * consuming my CQ I could be called after all completions have
	 * occurred.  But in this case, ibc_nrx == 0 && ibc_nsends_posted == 0
	 * and this CQ is about to be destroyed so I NOOP. */
	kib_conn_t		*conn = (kib_conn_t *)arg;
	struct kib_sched_info	*sched = conn->ibc_sched;
	unsigned long		flags;

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
	kib_conn_t *conn = arg;

	CERROR("%s: async CQ event type %d\n",
	       libcfs_nid2str(conn->ibc_peer->ibp_nid), event->event);
}

int
kiblnd_scheduler(void *arg)
{
	long			id = (long)arg;
	struct kib_sched_info	*sched;
	kib_conn_t		*conn;
	wait_queue_t		wait;
	unsigned long		flags;
	struct ib_wc		wc;
	int			did_something;
	int			busy_loops = 0;
	int			rc;

	cfs_block_allsigs();

	init_waitqueue_entry(&wait, current);

	sched = kiblnd_data.kib_scheds[KIB_THREAD_CPT(id)];

	rc = cfs_cpt_bind(lnet_cpt_table(), sched->ibs_cpt);
	if (rc != 0) {
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
			conn = list_entry(sched->ibs_conns.next,
					      kib_conn_t, ibc_sched_list);
			/* take over kib_sched_conns' ref on conn... */
			LASSERT(conn->ibc_scheduled);
			list_del(&conn->ibc_sched_list);
			conn->ibc_ready = 0;

			spin_unlock_irqrestore(&sched->ibs_lock, flags);

			rc = ib_poll_cq(conn->ibc_cq, 1, &wc);
			if (rc == 0) {
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

			if (rc != 0 || conn->ibc_ready) {
				/* There may be another completion waiting; get
				 * another scheduler to check while I handle
				 * this one... */
				/* +1 ref for sched_conns */
				kiblnd_conn_addref(conn);
				list_add_tail(&conn->ibc_sched_list,
						  &sched->ibs_conns);
				if (waitqueue_active(&sched->ibs_waitq))
					wake_up(&sched->ibs_waitq);
			} else {
				conn->ibc_scheduled = 0;
			}

			if (rc != 0) {
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
	rwlock_t		*glock = &kiblnd_data.kib_global_lock;
	kib_dev_t	 *dev;
	wait_queue_t     wait;
	unsigned long      flags;
	int		rc;

	LASSERT(*kiblnd_tunables.kib_dev_failover != 0);

	cfs_block_allsigs();

	init_waitqueue_entry(&wait, current);
	write_lock_irqsave(glock, flags);

	while (!kiblnd_data.kib_shutdown) {
		int     do_failover = 0;
		int     long_sleep;

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

		if (!long_sleep || rc != 0)
			continue;

		/* have a long sleep, routine check all active devices,
		 * we need checking like this because if there is not active
		 * connection on the dev and no SEND from local, we may listen
		 * on wrong HCA for ever while there is a bonding failover */
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
