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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#include "socklnd.h"

int
ksocknal_lib_get_conn_addrs(ksock_conn_t *conn)
{
	int rc = lnet_sock_getaddr(conn->ksnc_sock, 1, &conn->ksnc_ipaddr,
				   &conn->ksnc_port);

	/* Didn't need the {get,put}connsock dance to deref ksnc_sock... */
	LASSERT(!conn->ksnc_closing);

	if (rc) {
		CERROR("Error %d getting sock peer IP\n", rc);
		return rc;
	}

	rc = lnet_sock_getaddr(conn->ksnc_sock, 0, &conn->ksnc_myipaddr, NULL);
	if (rc) {
		CERROR("Error %d getting sock local IP\n", rc);
		return rc;
	}

	return 0;
}

int
ksocknal_lib_zc_capable(ksock_conn_t *conn)
{
	int caps = conn->ksnc_sock->sk->sk_route_caps;

	if (conn->ksnc_proto == &ksocknal_protocol_v1x)
		return 0;

	/*
	 * ZC if the socket supports scatter/gather and doesn't need software
	 * checksums
	 */
	return ((caps & NETIF_F_SG) && (caps & NETIF_F_CSUM_MASK));
}

int
ksocknal_lib_send_iov(ksock_conn_t *conn, ksock_tx_t *tx)
{
	struct socket *sock = conn->ksnc_sock;
	int nob;
	int rc;

	if (*ksocknal_tunables.ksnd_enable_csum	&& /* checksum enabled */
	    conn->ksnc_proto == &ksocknal_protocol_v2x && /* V2.x connection  */
	    tx->tx_nob == tx->tx_resid		 && /* frist sending    */
	    !tx->tx_msg.ksm_csum)		     /* not checksummed  */
		ksocknal_lib_csum_tx(tx);

	/*
	 * NB we can't trust socket ops to either consume our iovs
	 * or leave them alone.
	 */
	{
#if SOCKNAL_SINGLE_FRAG_TX
		struct kvec scratch;
		struct kvec *scratchiov = &scratch;
		unsigned int niov = 1;
#else
		struct kvec *scratchiov = conn->ksnc_scheduler->kss_scratch_iov;
		unsigned int niov = tx->tx_niov;
#endif
		struct msghdr msg = {.msg_flags = MSG_DONTWAIT};
		int i;

		for (nob = i = 0; i < niov; i++) {
			scratchiov[i] = tx->tx_iov[i];
			nob += scratchiov[i].iov_len;
		}

		if (!list_empty(&conn->ksnc_tx_queue) ||
		    nob < tx->tx_resid)
			msg.msg_flags |= MSG_MORE;

		rc = kernel_sendmsg(sock, &msg, scratchiov, niov, nob);
	}
	return rc;
}

int
ksocknal_lib_send_kiov(ksock_conn_t *conn, ksock_tx_t *tx)
{
	struct socket *sock = conn->ksnc_sock;
	lnet_kiov_t *kiov = tx->tx_kiov;
	int rc;
	int nob;

	/* Not NOOP message */
	LASSERT(tx->tx_lnetmsg);

	/*
	 * NB we can't trust socket ops to either consume our iovs
	 * or leave them alone.
	 */
	if (tx->tx_msg.ksm_zc_cookies[0]) {
		/* Zero copy is enabled */
		struct sock *sk = sock->sk;
		struct page *page = kiov->kiov_page;
		int offset = kiov->kiov_offset;
		int fragsize = kiov->kiov_len;
		int msgflg = MSG_DONTWAIT;

		CDEBUG(D_NET, "page %p + offset %x for %d\n",
		       page, offset, kiov->kiov_len);

		if (!list_empty(&conn->ksnc_tx_queue) ||
		    fragsize < tx->tx_resid)
			msgflg |= MSG_MORE;

		if (sk->sk_prot->sendpage) {
			rc = sk->sk_prot->sendpage(sk, page,
						   offset, fragsize, msgflg);
		} else {
			rc = tcp_sendpage(sk, page, offset, fragsize, msgflg);
		}
	} else {
#if SOCKNAL_SINGLE_FRAG_TX || !SOCKNAL_RISK_KMAP_DEADLOCK
		struct kvec scratch;
		struct kvec *scratchiov = &scratch;
		unsigned int niov = 1;
#else
#ifdef CONFIG_HIGHMEM
#warning "XXX risk of kmap deadlock on multiple frags..."
#endif
		struct kvec *scratchiov = conn->ksnc_scheduler->kss_scratch_iov;
		unsigned int niov = tx->tx_nkiov;
#endif
		struct msghdr msg = {.msg_flags = MSG_DONTWAIT};
		int i;

		for (nob = i = 0; i < niov; i++) {
			scratchiov[i].iov_base = kmap(kiov[i].kiov_page) +
						 kiov[i].kiov_offset;
			nob += scratchiov[i].iov_len = kiov[i].kiov_len;
		}

		if (!list_empty(&conn->ksnc_tx_queue) ||
		    nob < tx->tx_resid)
			msg.msg_flags |= MSG_MORE;

		rc = kernel_sendmsg(sock, &msg, (struct kvec *)scratchiov, niov, nob);

		for (i = 0; i < niov; i++)
			kunmap(kiov[i].kiov_page);
	}
	return rc;
}

void
ksocknal_lib_eager_ack(ksock_conn_t *conn)
{
	int opt = 1;
	struct socket *sock = conn->ksnc_sock;

	/*
	 * Remind the socket to ACK eagerly.  If I don't, the socket might
	 * think I'm about to send something it could piggy-back the ACK
	 * on, introducing delay in completing zero-copy sends in my
	 * peer.
	 */
	kernel_setsockopt(sock, SOL_TCP, TCP_QUICKACK, (char *)&opt,
			  sizeof(opt));
}

int
ksocknal_lib_recv_iov(ksock_conn_t *conn)
{
#if SOCKNAL_SINGLE_FRAG_RX
	struct kvec scratch;
	struct kvec *scratchiov = &scratch;
	unsigned int niov = 1;
#else
	struct kvec *scratchiov = conn->ksnc_scheduler->kss_scratch_iov;
	unsigned int niov = conn->ksnc_rx_niov;
#endif
	struct kvec *iov = conn->ksnc_rx_iov;
	struct msghdr msg = {
		.msg_flags = 0
	};
	int nob;
	int i;
	int rc;
	int fragnob;
	int sum;
	__u32 saved_csum;

	/*
	 * NB we can't trust socket ops to either consume our iovs
	 * or leave them alone.
	 */
	LASSERT(niov > 0);

	for (nob = i = 0; i < niov; i++) {
		scratchiov[i] = iov[i];
		nob += scratchiov[i].iov_len;
	}
	LASSERT(nob <= conn->ksnc_rx_nob_wanted);

	rc = kernel_recvmsg(conn->ksnc_sock, &msg, scratchiov, niov, nob,
			    MSG_DONTWAIT);

	saved_csum = 0;
	if (conn->ksnc_proto == &ksocknal_protocol_v2x) {
		saved_csum = conn->ksnc_msg.ksm_csum;
		conn->ksnc_msg.ksm_csum = 0;
	}

	if (saved_csum) {
		/* accumulate checksum */
		for (i = 0, sum = rc; sum > 0; i++, sum -= fragnob) {
			LASSERT(i < niov);

			fragnob = iov[i].iov_len;
			if (fragnob > sum)
				fragnob = sum;

			conn->ksnc_rx_csum = ksocknal_csum(conn->ksnc_rx_csum,
							   iov[i].iov_base, fragnob);
		}
		conn->ksnc_msg.ksm_csum = saved_csum;
	}

	return rc;
}

static void
ksocknal_lib_kiov_vunmap(void *addr)
{
	if (!addr)
		return;

	vunmap(addr);
}

static void *
ksocknal_lib_kiov_vmap(lnet_kiov_t *kiov, int niov,
		       struct kvec *iov, struct page **pages)
{
	void *addr;
	int nob;
	int i;

	if (!*ksocknal_tunables.ksnd_zc_recv || !pages)
		return NULL;

	LASSERT(niov <= LNET_MAX_IOV);

	if (niov < 2 ||
	    niov < *ksocknal_tunables.ksnd_zc_recv_min_nfrags)
		return NULL;

	for (nob = i = 0; i < niov; i++) {
		if ((kiov[i].kiov_offset && i > 0) ||
		    (kiov[i].kiov_offset + kiov[i].kiov_len != PAGE_SIZE && i < niov - 1))
			return NULL;

		pages[i] = kiov[i].kiov_page;
		nob += kiov[i].kiov_len;
	}

	addr = vmap(pages, niov, VM_MAP, PAGE_KERNEL);
	if (!addr)
		return NULL;

	iov->iov_base = addr + kiov[0].kiov_offset;
	iov->iov_len = nob;

	return addr;
}

int
ksocknal_lib_recv_kiov(ksock_conn_t *conn)
{
#if SOCKNAL_SINGLE_FRAG_RX || !SOCKNAL_RISK_KMAP_DEADLOCK
	struct kvec scratch;
	struct kvec *scratchiov = &scratch;
	struct page **pages = NULL;
	unsigned int niov = 1;
#else
#ifdef CONFIG_HIGHMEM
#warning "XXX risk of kmap deadlock on multiple frags..."
#endif
	struct kvec *scratchiov = conn->ksnc_scheduler->kss_scratch_iov;
	struct page **pages = conn->ksnc_scheduler->kss_rx_scratch_pgs;
	unsigned int niov = conn->ksnc_rx_nkiov;
#endif
	lnet_kiov_t   *kiov = conn->ksnc_rx_kiov;
	struct msghdr msg = {
		.msg_flags = 0
	};
	int nob;
	int i;
	int rc;
	void *base;
	void *addr;
	int sum;
	int fragnob;
	int n;

	/*
	 * NB we can't trust socket ops to either consume our iovs
	 * or leave them alone.
	 */
	addr = ksocknal_lib_kiov_vmap(kiov, niov, scratchiov, pages);
	if (addr) {
		nob = scratchiov[0].iov_len;
		n = 1;

	} else {
		for (nob = i = 0; i < niov; i++) {
			nob += scratchiov[i].iov_len = kiov[i].kiov_len;
			scratchiov[i].iov_base = kmap(kiov[i].kiov_page) +
						 kiov[i].kiov_offset;
		}
		n = niov;
	}

	LASSERT(nob <= conn->ksnc_rx_nob_wanted);

	rc = kernel_recvmsg(conn->ksnc_sock, &msg, (struct kvec *)scratchiov,
			    n, nob, MSG_DONTWAIT);

	if (conn->ksnc_msg.ksm_csum) {
		for (i = 0, sum = rc; sum > 0; i++, sum -= fragnob) {
			LASSERT(i < niov);

			/*
			 * Dang! have to kmap again because I have nowhere to
			 * stash the mapped address.  But by doing it while the
			 * page is still mapped, the kernel just bumps the map
			 * count and returns me the address it stashed.
			 */
			base = kmap(kiov[i].kiov_page) + kiov[i].kiov_offset;
			fragnob = kiov[i].kiov_len;
			if (fragnob > sum)
				fragnob = sum;

			conn->ksnc_rx_csum = ksocknal_csum(conn->ksnc_rx_csum,
							   base, fragnob);

			kunmap(kiov[i].kiov_page);
		}
	}

	if (addr) {
		ksocknal_lib_kiov_vunmap(addr);
	} else {
		for (i = 0; i < niov; i++)
			kunmap(kiov[i].kiov_page);
	}

	return rc;
}

void
ksocknal_lib_csum_tx(ksock_tx_t *tx)
{
	int i;
	__u32 csum;
	void *base;

	LASSERT(tx->tx_iov[0].iov_base == &tx->tx_msg);
	LASSERT(tx->tx_conn);
	LASSERT(tx->tx_conn->ksnc_proto == &ksocknal_protocol_v2x);

	tx->tx_msg.ksm_csum = 0;

	csum = ksocknal_csum(~0, tx->tx_iov[0].iov_base,
			     tx->tx_iov[0].iov_len);

	if (tx->tx_kiov) {
		for (i = 0; i < tx->tx_nkiov; i++) {
			base = kmap(tx->tx_kiov[i].kiov_page) +
			       tx->tx_kiov[i].kiov_offset;

			csum = ksocknal_csum(csum, base, tx->tx_kiov[i].kiov_len);

			kunmap(tx->tx_kiov[i].kiov_page);
		}
	} else {
		for (i = 1; i < tx->tx_niov; i++)
			csum = ksocknal_csum(csum, tx->tx_iov[i].iov_base,
					     tx->tx_iov[i].iov_len);
	}

	if (*ksocknal_tunables.ksnd_inject_csum_error) {
		csum++;
		*ksocknal_tunables.ksnd_inject_csum_error = 0;
	}

	tx->tx_msg.ksm_csum = csum;
}

int
ksocknal_lib_get_conn_tunables(ksock_conn_t *conn, int *txmem, int *rxmem, int *nagle)
{
	struct socket *sock = conn->ksnc_sock;
	int len;
	int rc;

	rc = ksocknal_connsock_addref(conn);
	if (rc) {
		LASSERT(conn->ksnc_closing);
		*txmem = *rxmem = *nagle = 0;
		return -ESHUTDOWN;
	}

	rc = lnet_sock_getbuf(sock, txmem, rxmem);
	if (!rc) {
		len = sizeof(*nagle);
		rc = kernel_getsockopt(sock, SOL_TCP, TCP_NODELAY,
				       (char *)nagle, &len);
	}

	ksocknal_connsock_decref(conn);

	if (!rc)
		*nagle = !*nagle;
	else
		*txmem = *rxmem = *nagle = 0;

	return rc;
}

int
ksocknal_lib_setup_sock(struct socket *sock)
{
	int rc;
	int option;
	int keep_idle;
	int keep_intvl;
	int keep_count;
	int do_keepalive;
	struct linger linger;

	sock->sk->sk_allocation = GFP_NOFS;

	/*
	 * Ensure this socket aborts active sends immediately when we close
	 * it.
	 */
	linger.l_onoff = 0;
	linger.l_linger = 0;

	rc = kernel_setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&linger,
			       sizeof(linger));
	if (rc) {
		CERROR("Can't set SO_LINGER: %d\n", rc);
		return rc;
	}

	option = -1;
	rc = kernel_setsockopt(sock, SOL_TCP, TCP_LINGER2, (char *)&option,
			       sizeof(option));
	if (rc) {
		CERROR("Can't set SO_LINGER2: %d\n", rc);
		return rc;
	}

	if (!*ksocknal_tunables.ksnd_nagle) {
		option = 1;

		rc = kernel_setsockopt(sock, SOL_TCP, TCP_NODELAY,
				       (char *)&option, sizeof(option));
		if (rc) {
			CERROR("Can't disable nagle: %d\n", rc);
			return rc;
		}
	}

	rc = lnet_sock_setbuf(sock, *ksocknal_tunables.ksnd_tx_buffer_size,
			      *ksocknal_tunables.ksnd_rx_buffer_size);
	if (rc) {
		CERROR("Can't set buffer tx %d, rx %d buffers: %d\n",
		       *ksocknal_tunables.ksnd_tx_buffer_size,
		       *ksocknal_tunables.ksnd_rx_buffer_size, rc);
		return rc;
	}

/* TCP_BACKOFF_* sockopt tunables unsupported in stock kernels */

	/* snapshot tunables */
	keep_idle  = *ksocknal_tunables.ksnd_keepalive_idle;
	keep_count = *ksocknal_tunables.ksnd_keepalive_count;
	keep_intvl = *ksocknal_tunables.ksnd_keepalive_intvl;

	do_keepalive = (keep_idle > 0 && keep_count > 0 && keep_intvl > 0);

	option = (do_keepalive ? 1 : 0);
	rc = kernel_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&option,
			       sizeof(option));
	if (rc) {
		CERROR("Can't set SO_KEEPALIVE: %d\n", rc);
		return rc;
	}

	if (!do_keepalive)
		return 0;

	rc = kernel_setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, (char *)&keep_idle,
			       sizeof(keep_idle));
	if (rc) {
		CERROR("Can't set TCP_KEEPIDLE: %d\n", rc);
		return rc;
	}

	rc = kernel_setsockopt(sock, SOL_TCP, TCP_KEEPINTVL,
			       (char *)&keep_intvl, sizeof(keep_intvl));
	if (rc) {
		CERROR("Can't set TCP_KEEPINTVL: %d\n", rc);
		return rc;
	}

	rc = kernel_setsockopt(sock, SOL_TCP, TCP_KEEPCNT, (char *)&keep_count,
			       sizeof(keep_count));
	if (rc) {
		CERROR("Can't set TCP_KEEPCNT: %d\n", rc);
		return rc;
	}

	return 0;
}

void
ksocknal_lib_push_conn(ksock_conn_t *conn)
{
	struct sock *sk;
	struct tcp_sock *tp;
	int nonagle;
	int val = 1;
	int rc;

	rc = ksocknal_connsock_addref(conn);
	if (rc)			    /* being shut down */
		return;

	sk = conn->ksnc_sock->sk;
	tp = tcp_sk(sk);

	lock_sock(sk);
	nonagle = tp->nonagle;
	tp->nonagle = 1;
	release_sock(sk);

	rc = kernel_setsockopt(conn->ksnc_sock, SOL_TCP, TCP_NODELAY,
			       (char *)&val, sizeof(val));
	LASSERT(!rc);

	lock_sock(sk);
	tp->nonagle = nonagle;
	release_sock(sk);

	ksocknal_connsock_decref(conn);
}

/*
 * socket call back in Linux
 */
static void
ksocknal_data_ready(struct sock *sk)
{
	ksock_conn_t *conn;

	/* interleave correctly with closing sockets... */
	LASSERT(!in_irq());
	read_lock(&ksocknal_data.ksnd_global_lock);

	conn = sk->sk_user_data;
	if (!conn) {	     /* raced with ksocknal_terminate_conn */
		LASSERT(sk->sk_data_ready != &ksocknal_data_ready);
		sk->sk_data_ready(sk);
	} else {
		ksocknal_read_callback(conn);
	}

	read_unlock(&ksocknal_data.ksnd_global_lock);
}

static void
ksocknal_write_space(struct sock *sk)
{
	ksock_conn_t *conn;
	int wspace;
	int min_wpace;

	/* interleave correctly with closing sockets... */
	LASSERT(!in_irq());
	read_lock(&ksocknal_data.ksnd_global_lock);

	conn = sk->sk_user_data;
	wspace = sk_stream_wspace(sk);
	min_wpace = sk_stream_min_wspace(sk);

	CDEBUG(D_NET, "sk %p wspace %d low water %d conn %p%s%s%s\n",
	       sk, wspace, min_wpace, conn,
	       !conn ? "" : (conn->ksnc_tx_ready ?
				      " ready" : " blocked"),
	       !conn ? "" : (conn->ksnc_tx_scheduled ?
				      " scheduled" : " idle"),
	       !conn ? "" : (list_empty(&conn->ksnc_tx_queue) ?
				      " empty" : " queued"));

	if (!conn) {	     /* raced with ksocknal_terminate_conn */
		LASSERT(sk->sk_write_space != &ksocknal_write_space);
		sk->sk_write_space(sk);

		read_unlock(&ksocknal_data.ksnd_global_lock);
		return;
	}

	if (wspace >= min_wpace) {	      /* got enough space */
		ksocknal_write_callback(conn);

		/*
		 * Clear SOCK_NOSPACE _after_ ksocknal_write_callback so the
		 * ENOMEM check in ksocknal_transmit is race-free (think about
		 * it).
		 */
		clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
	}

	read_unlock(&ksocknal_data.ksnd_global_lock);
}

void
ksocknal_lib_save_callback(struct socket *sock, ksock_conn_t *conn)
{
	conn->ksnc_saved_data_ready = sock->sk->sk_data_ready;
	conn->ksnc_saved_write_space = sock->sk->sk_write_space;
}

void
ksocknal_lib_set_callback(struct socket *sock,  ksock_conn_t *conn)
{
	sock->sk->sk_user_data = conn;
	sock->sk->sk_data_ready = ksocknal_data_ready;
	sock->sk->sk_write_space = ksocknal_write_space;
}

void
ksocknal_lib_reset_callback(struct socket *sock, ksock_conn_t *conn)
{
	/*
	 * Remove conn's network callbacks.
	 * NB I _have_ to restore the callback, rather than storing a noop,
	 * since the socket could survive past this module being unloaded!!
	 */
	sock->sk->sk_data_ready = conn->ksnc_saved_data_ready;
	sock->sk->sk_write_space = conn->ksnc_saved_write_space;

	/*
	 * A callback could be in progress already; they hold a read lock
	 * on ksnd_global_lock (to serialise with me) and NOOP if
	 * sk_user_data is NULL.
	 */
	sock->sk->sk_user_data = NULL;
}

int
ksocknal_lib_memory_pressure(ksock_conn_t *conn)
{
	int rc = 0;
	ksock_sched_t *sched;

	sched = conn->ksnc_scheduler;
	spin_lock_bh(&sched->kss_lock);

	if (!test_bit(SOCK_NOSPACE, &conn->ksnc_sock->flags) &&
	    !conn->ksnc_tx_ready) {
		/*
		 * SOCK_NOSPACE is set when the socket fills
		 * and cleared in the write_space callback
		 * (which also sets ksnc_tx_ready).  If
		 * SOCK_NOSPACE and ksnc_tx_ready are BOTH
		 * zero, I didn't fill the socket and
		 * write_space won't reschedule me, so I
		 * return -ENOMEM to get my caller to retry
		 * after a timeout
		 */
		rc = -ENOMEM;
	}

	spin_unlock_bh(&sched->kss_lock);

	return rc;
}
