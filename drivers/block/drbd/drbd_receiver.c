/*
   drbd_receiver.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2001-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 1999-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2002-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>

#include <asm/uaccess.h>
#include <net/sock.h>

#include <linux/drbd.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/in.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/mm_inline.h>
#include <linux/slab.h>
#include <linux/pkt_sched.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/scatterlist.h>
#include "drbd_int.h"
#include "drbd_req.h"

#include "drbd_vli.h"

enum finish_epoch {
	FE_STILL_LIVE,
	FE_DESTROYED,
	FE_RECYCLED,
};

static int drbd_do_handshake(struct drbd_conf *mdev);
static int drbd_do_auth(struct drbd_conf *mdev);

static enum finish_epoch drbd_may_finish_epoch(struct drbd_conf *, struct drbd_epoch *, enum epoch_event);
static int e_end_block(struct drbd_conf *, struct drbd_work *, int);


#define GFP_TRY	(__GFP_HIGHMEM | __GFP_NOWARN)

/*
 * some helper functions to deal with single linked page lists,
 * page->private being our "next" pointer.
 */

/* If at least n pages are linked at head, get n pages off.
 * Otherwise, don't modify head, and return NULL.
 * Locking is the responsibility of the caller.
 */
static struct page *page_chain_del(struct page **head, int n)
{
	struct page *page;
	struct page *tmp;

	BUG_ON(!n);
	BUG_ON(!head);

	page = *head;

	if (!page)
		return NULL;

	while (page) {
		tmp = page_chain_next(page);
		if (--n == 0)
			break; /* found sufficient pages */
		if (tmp == NULL)
			/* insufficient pages, don't use any of them. */
			return NULL;
		page = tmp;
	}

	/* add end of list marker for the returned list */
	set_page_private(page, 0);
	/* actual return value, and adjustment of head */
	page = *head;
	*head = tmp;
	return page;
}

/* may be used outside of locks to find the tail of a (usually short)
 * "private" page chain, before adding it back to a global chain head
 * with page_chain_add() under a spinlock. */
static struct page *page_chain_tail(struct page *page, int *len)
{
	struct page *tmp;
	int i = 1;
	while ((tmp = page_chain_next(page)))
		++i, page = tmp;
	if (len)
		*len = i;
	return page;
}

static int page_chain_free(struct page *page)
{
	struct page *tmp;
	int i = 0;
	page_chain_for_each_safe(page, tmp) {
		put_page(page);
		++i;
	}
	return i;
}

static void page_chain_add(struct page **head,
		struct page *chain_first, struct page *chain_last)
{
#if 1
	struct page *tmp;
	tmp = page_chain_tail(chain_first, NULL);
	BUG_ON(tmp != chain_last);
#endif

	/* add chain to head */
	set_page_private(chain_last, (unsigned long)*head);
	*head = chain_first;
}

static struct page *drbd_pp_first_pages_or_try_alloc(struct drbd_conf *mdev, int number)
{
	struct page *page = NULL;
	struct page *tmp = NULL;
	int i = 0;

	/* Yes, testing drbd_pp_vacant outside the lock is racy.
	 * So what. It saves a spin_lock. */
	if (drbd_pp_vacant >= number) {
		spin_lock(&drbd_pp_lock);
		page = page_chain_del(&drbd_pp_pool, number);
		if (page)
			drbd_pp_vacant -= number;
		spin_unlock(&drbd_pp_lock);
		if (page)
			return page;
	}

	/* GFP_TRY, because we must not cause arbitrary write-out: in a DRBD
	 * "criss-cross" setup, that might cause write-out on some other DRBD,
	 * which in turn might block on the other node at this very place.  */
	for (i = 0; i < number; i++) {
		tmp = alloc_page(GFP_TRY);
		if (!tmp)
			break;
		set_page_private(tmp, (unsigned long)page);
		page = tmp;
	}

	if (i == number)
		return page;

	/* Not enough pages immediately available this time.
	 * No need to jump around here, drbd_pp_alloc will retry this
	 * function "soon". */
	if (page) {
		tmp = page_chain_tail(page, NULL);
		spin_lock(&drbd_pp_lock);
		page_chain_add(&drbd_pp_pool, page, tmp);
		drbd_pp_vacant += i;
		spin_unlock(&drbd_pp_lock);
	}
	return NULL;
}

static void reclaim_net_ee(struct drbd_conf *mdev, struct list_head *to_be_freed)
{
	struct drbd_epoch_entry *e;
	struct list_head *le, *tle;

	/* The EEs are always appended to the end of the list. Since
	   they are sent in order over the wire, they have to finish
	   in order. As soon as we see the first not finished we can
	   stop to examine the list... */

	list_for_each_safe(le, tle, &mdev->net_ee) {
		e = list_entry(le, struct drbd_epoch_entry, w.list);
		if (drbd_ee_has_active_page(e))
			break;
		list_move(le, to_be_freed);
	}
}

static void drbd_kick_lo_and_reclaim_net(struct drbd_conf *mdev)
{
	LIST_HEAD(reclaimed);
	struct drbd_epoch_entry *e, *t;

	spin_lock_irq(&mdev->req_lock);
	reclaim_net_ee(mdev, &reclaimed);
	spin_unlock_irq(&mdev->req_lock);

	list_for_each_entry_safe(e, t, &reclaimed, w.list)
		drbd_free_net_ee(mdev, e);
}

/**
 * drbd_pp_alloc() - Returns @number pages, retries forever (or until signalled)
 * @mdev:	DRBD device.
 * @number:	number of pages requested
 * @retry:	whether to retry, if not enough pages are available right now
 *
 * Tries to allocate number pages, first from our own page pool, then from
 * the kernel, unless this allocation would exceed the max_buffers setting.
 * Possibly retry until DRBD frees sufficient pages somewhere else.
 *
 * Returns a page chain linked via page->private.
 */
static struct page *drbd_pp_alloc(struct drbd_conf *mdev, unsigned number, bool retry)
{
	struct page *page = NULL;
	DEFINE_WAIT(wait);

	/* Yes, we may run up to @number over max_buffers. If we
	 * follow it strictly, the admin will get it wrong anyways. */
	if (atomic_read(&mdev->pp_in_use) < mdev->net_conf->max_buffers)
		page = drbd_pp_first_pages_or_try_alloc(mdev, number);

	while (page == NULL) {
		prepare_to_wait(&drbd_pp_wait, &wait, TASK_INTERRUPTIBLE);

		drbd_kick_lo_and_reclaim_net(mdev);

		if (atomic_read(&mdev->pp_in_use) < mdev->net_conf->max_buffers) {
			page = drbd_pp_first_pages_or_try_alloc(mdev, number);
			if (page)
				break;
		}

		if (!retry)
			break;

		if (signal_pending(current)) {
			dev_warn(DEV, "drbd_pp_alloc interrupted!\n");
			break;
		}

		schedule();
	}
	finish_wait(&drbd_pp_wait, &wait);

	if (page)
		atomic_add(number, &mdev->pp_in_use);
	return page;
}

/* Must not be used from irq, as that may deadlock: see drbd_pp_alloc.
 * Is also used from inside an other spin_lock_irq(&mdev->req_lock);
 * Either links the page chain back to the global pool,
 * or returns all pages to the system. */
static void drbd_pp_free(struct drbd_conf *mdev, struct page *page, int is_net)
{
	atomic_t *a = is_net ? &mdev->pp_in_use_by_net : &mdev->pp_in_use;
	int i;

	if (drbd_pp_vacant > (DRBD_MAX_BIO_SIZE/PAGE_SIZE)*minor_count)
		i = page_chain_free(page);
	else {
		struct page *tmp;
		tmp = page_chain_tail(page, &i);
		spin_lock(&drbd_pp_lock);
		page_chain_add(&drbd_pp_pool, page, tmp);
		drbd_pp_vacant += i;
		spin_unlock(&drbd_pp_lock);
	}
	i = atomic_sub_return(i, a);
	if (i < 0)
		dev_warn(DEV, "ASSERTION FAILED: %s: %d < 0\n",
			is_net ? "pp_in_use_by_net" : "pp_in_use", i);
	wake_up(&drbd_pp_wait);
}

/*
You need to hold the req_lock:
 _drbd_wait_ee_list_empty()

You must not have the req_lock:
 drbd_free_ee()
 drbd_alloc_ee()
 drbd_init_ee()
 drbd_release_ee()
 drbd_ee_fix_bhs()
 drbd_process_done_ee()
 drbd_clear_done_ee()
 drbd_wait_ee_list_empty()
*/

struct drbd_epoch_entry *drbd_alloc_ee(struct drbd_conf *mdev,
				     u64 id,
				     sector_t sector,
				     unsigned int data_size,
				     gfp_t gfp_mask) __must_hold(local)
{
	struct drbd_epoch_entry *e;
	struct page *page;
	unsigned nr_pages = (data_size + PAGE_SIZE -1) >> PAGE_SHIFT;

	if (drbd_insert_fault(mdev, DRBD_FAULT_AL_EE))
		return NULL;

	e = mempool_alloc(drbd_ee_mempool, gfp_mask & ~__GFP_HIGHMEM);
	if (!e) {
		if (!(gfp_mask & __GFP_NOWARN))
			dev_err(DEV, "alloc_ee: Allocation of an EE failed\n");
		return NULL;
	}

	page = drbd_pp_alloc(mdev, nr_pages, (gfp_mask & __GFP_WAIT));
	if (!page)
		goto fail;

	INIT_HLIST_NODE(&e->collision);
	e->epoch = NULL;
	e->mdev = mdev;
	e->pages = page;
	atomic_set(&e->pending_bios, 0);
	e->size = data_size;
	e->flags = 0;
	e->sector = sector;
	e->block_id = id;

	return e;

 fail:
	mempool_free(e, drbd_ee_mempool);
	return NULL;
}

void drbd_free_some_ee(struct drbd_conf *mdev, struct drbd_epoch_entry *e, int is_net)
{
	if (e->flags & EE_HAS_DIGEST)
		kfree(e->digest);
	drbd_pp_free(mdev, e->pages, is_net);
	D_ASSERT(atomic_read(&e->pending_bios) == 0);
	D_ASSERT(hlist_unhashed(&e->collision));
	mempool_free(e, drbd_ee_mempool);
}

int drbd_release_ee(struct drbd_conf *mdev, struct list_head *list)
{
	LIST_HEAD(work_list);
	struct drbd_epoch_entry *e, *t;
	int count = 0;
	int is_net = list == &mdev->net_ee;

	spin_lock_irq(&mdev->req_lock);
	list_splice_init(list, &work_list);
	spin_unlock_irq(&mdev->req_lock);

	list_for_each_entry_safe(e, t, &work_list, w.list) {
		drbd_free_some_ee(mdev, e, is_net);
		count++;
	}
	return count;
}


/*
 * This function is called from _asender only_
 * but see also comments in _req_mod(,barrier_acked)
 * and receive_Barrier.
 *
 * Move entries from net_ee to done_ee, if ready.
 * Grab done_ee, call all callbacks, free the entries.
 * The callbacks typically send out ACKs.
 */
static int drbd_process_done_ee(struct drbd_conf *mdev)
{
	LIST_HEAD(work_list);
	LIST_HEAD(reclaimed);
	struct drbd_epoch_entry *e, *t;
	int ok = (mdev->state.conn >= C_WF_REPORT_PARAMS);

	spin_lock_irq(&mdev->req_lock);
	reclaim_net_ee(mdev, &reclaimed);
	list_splice_init(&mdev->done_ee, &work_list);
	spin_unlock_irq(&mdev->req_lock);

	list_for_each_entry_safe(e, t, &reclaimed, w.list)
		drbd_free_net_ee(mdev, e);

	/* possible callbacks here:
	 * e_end_block, and e_end_resync_block, e_send_discard_ack.
	 * all ignore the last argument.
	 */
	list_for_each_entry_safe(e, t, &work_list, w.list) {
		/* list_del not necessary, next/prev members not touched */
		ok = e->w.cb(mdev, &e->w, !ok) && ok;
		drbd_free_ee(mdev, e);
	}
	wake_up(&mdev->ee_wait);

	return ok;
}

void _drbd_wait_ee_list_empty(struct drbd_conf *mdev, struct list_head *head)
{
	DEFINE_WAIT(wait);

	/* avoids spin_lock/unlock
	 * and calling prepare_to_wait in the fast path */
	while (!list_empty(head)) {
		prepare_to_wait(&mdev->ee_wait, &wait, TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&mdev->req_lock);
		io_schedule();
		finish_wait(&mdev->ee_wait, &wait);
		spin_lock_irq(&mdev->req_lock);
	}
}

void drbd_wait_ee_list_empty(struct drbd_conf *mdev, struct list_head *head)
{
	spin_lock_irq(&mdev->req_lock);
	_drbd_wait_ee_list_empty(mdev, head);
	spin_unlock_irq(&mdev->req_lock);
}

/* see also kernel_accept; which is only present since 2.6.18.
 * also we want to log which part of it failed, exactly */
static int drbd_accept(struct drbd_conf *mdev, const char **what,
		struct socket *sock, struct socket **newsock)
{
	struct sock *sk = sock->sk;
	int err = 0;

	*what = "listen";
	err = sock->ops->listen(sock, 5);
	if (err < 0)
		goto out;

	*what = "sock_create_lite";
	err = sock_create_lite(sk->sk_family, sk->sk_type, sk->sk_protocol,
			       newsock);
	if (err < 0)
		goto out;

	*what = "accept";
	err = sock->ops->accept(sock, *newsock, 0);
	if (err < 0) {
		sock_release(*newsock);
		*newsock = NULL;
		goto out;
	}
	(*newsock)->ops  = sock->ops;

out:
	return err;
}

static int drbd_recv_short(struct drbd_conf *mdev, struct socket *sock,
		    void *buf, size_t size, int flags)
{
	mm_segment_t oldfs;
	struct kvec iov = {
		.iov_base = buf,
		.iov_len = size,
	};
	struct msghdr msg = {
		.msg_iovlen = 1,
		.msg_iov = (struct iovec *)&iov,
		.msg_flags = (flags ? flags : MSG_WAITALL | MSG_NOSIGNAL)
	};
	int rv;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	rv = sock_recvmsg(sock, &msg, size, msg.msg_flags);
	set_fs(oldfs);

	return rv;
}

static int drbd_recv(struct drbd_conf *mdev, void *buf, size_t size)
{
	mm_segment_t oldfs;
	struct kvec iov = {
		.iov_base = buf,
		.iov_len = size,
	};
	struct msghdr msg = {
		.msg_iovlen = 1,
		.msg_iov = (struct iovec *)&iov,
		.msg_flags = MSG_WAITALL | MSG_NOSIGNAL
	};
	int rv;

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	for (;;) {
		rv = sock_recvmsg(mdev->data.socket, &msg, size, msg.msg_flags);
		if (rv == size)
			break;

		/* Note:
		 * ECONNRESET	other side closed the connection
		 * ERESTARTSYS	(on  sock) we got a signal
		 */

		if (rv < 0) {
			if (rv == -ECONNRESET)
				dev_info(DEV, "sock was reset by peer\n");
			else if (rv != -ERESTARTSYS)
				dev_err(DEV, "sock_recvmsg returned %d\n", rv);
			break;
		} else if (rv == 0) {
			dev_info(DEV, "sock was shut down by peer\n");
			break;
		} else	{
			/* signal came in, or peer/link went down,
			 * after we read a partial message
			 */
			/* D_ASSERT(signal_pending(current)); */
			break;
		}
	};

	set_fs(oldfs);

	if (rv != size)
		drbd_force_state(mdev, NS(conn, C_BROKEN_PIPE));

	return rv;
}

/* quoting tcp(7):
 *   On individual connections, the socket buffer size must be set prior to the
 *   listen(2) or connect(2) calls in order to have it take effect.
 * This is our wrapper to do so.
 */
static void drbd_setbufsize(struct socket *sock, unsigned int snd,
		unsigned int rcv)
{
	/* open coded SO_SNDBUF, SO_RCVBUF */
	if (snd) {
		sock->sk->sk_sndbuf = snd;
		sock->sk->sk_userlocks |= SOCK_SNDBUF_LOCK;
	}
	if (rcv) {
		sock->sk->sk_rcvbuf = rcv;
		sock->sk->sk_userlocks |= SOCK_RCVBUF_LOCK;
	}
}

static struct socket *drbd_try_connect(struct drbd_conf *mdev)
{
	const char *what;
	struct socket *sock;
	struct sockaddr_in6 src_in6;
	int err;
	int disconnect_on_error = 1;

	if (!get_net_conf(mdev))
		return NULL;

	what = "sock_create_kern";
	err = sock_create_kern(((struct sockaddr *)mdev->net_conf->my_addr)->sa_family,
		SOCK_STREAM, IPPROTO_TCP, &sock);
	if (err < 0) {
		sock = NULL;
		goto out;
	}

	sock->sk->sk_rcvtimeo =
	sock->sk->sk_sndtimeo =  mdev->net_conf->try_connect_int*HZ;
	drbd_setbufsize(sock, mdev->net_conf->sndbuf_size,
			mdev->net_conf->rcvbuf_size);

       /* explicitly bind to the configured IP as source IP
	*  for the outgoing connections.
	*  This is needed for multihomed hosts and to be
	*  able to use lo: interfaces for drbd.
	* Make sure to use 0 as port number, so linux selects
	*  a free one dynamically.
	*/
	memcpy(&src_in6, mdev->net_conf->my_addr,
	       min_t(int, mdev->net_conf->my_addr_len, sizeof(src_in6)));
	if (((struct sockaddr *)mdev->net_conf->my_addr)->sa_family == AF_INET6)
		src_in6.sin6_port = 0;
	else
		((struct sockaddr_in *)&src_in6)->sin_port = 0; /* AF_INET & AF_SCI */

	what = "bind before connect";
	err = sock->ops->bind(sock,
			      (struct sockaddr *) &src_in6,
			      mdev->net_conf->my_addr_len);
	if (err < 0)
		goto out;

	/* connect may fail, peer not yet available.
	 * stay C_WF_CONNECTION, don't go Disconnecting! */
	disconnect_on_error = 0;
	what = "connect";
	err = sock->ops->connect(sock,
				 (struct sockaddr *)mdev->net_conf->peer_addr,
				 mdev->net_conf->peer_addr_len, 0);

out:
	if (err < 0) {
		if (sock) {
			sock_release(sock);
			sock = NULL;
		}
		switch (-err) {
			/* timeout, busy, signal pending */
		case ETIMEDOUT: case EAGAIN: case EINPROGRESS:
		case EINTR: case ERESTARTSYS:
			/* peer not (yet) available, network problem */
		case ECONNREFUSED: case ENETUNREACH:
		case EHOSTDOWN:    case EHOSTUNREACH:
			disconnect_on_error = 0;
			break;
		default:
			dev_err(DEV, "%s failed, err = %d\n", what, err);
		}
		if (disconnect_on_error)
			drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
	}
	put_net_conf(mdev);
	return sock;
}

static struct socket *drbd_wait_for_connect(struct drbd_conf *mdev)
{
	int timeo, err;
	struct socket *s_estab = NULL, *s_listen;
	const char *what;

	if (!get_net_conf(mdev))
		return NULL;

	what = "sock_create_kern";
	err = sock_create_kern(((struct sockaddr *)mdev->net_conf->my_addr)->sa_family,
		SOCK_STREAM, IPPROTO_TCP, &s_listen);
	if (err) {
		s_listen = NULL;
		goto out;
	}

	timeo = mdev->net_conf->try_connect_int * HZ;
	timeo += (random32() & 1) ? timeo / 7 : -timeo / 7; /* 28.5% random jitter */

	s_listen->sk->sk_reuse    = 1; /* SO_REUSEADDR */
	s_listen->sk->sk_rcvtimeo = timeo;
	s_listen->sk->sk_sndtimeo = timeo;
	drbd_setbufsize(s_listen, mdev->net_conf->sndbuf_size,
			mdev->net_conf->rcvbuf_size);

	what = "bind before listen";
	err = s_listen->ops->bind(s_listen,
			      (struct sockaddr *) mdev->net_conf->my_addr,
			      mdev->net_conf->my_addr_len);
	if (err < 0)
		goto out;

	err = drbd_accept(mdev, &what, s_listen, &s_estab);

out:
	if (s_listen)
		sock_release(s_listen);
	if (err < 0) {
		if (err != -EAGAIN && err != -EINTR && err != -ERESTARTSYS) {
			dev_err(DEV, "%s failed, err = %d\n", what, err);
			drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
		}
	}
	put_net_conf(mdev);

	return s_estab;
}

static int drbd_send_fp(struct drbd_conf *mdev,
	struct socket *sock, enum drbd_packets cmd)
{
	struct p_header80 *h = &mdev->data.sbuf.header.h80;

	return _drbd_send_cmd(mdev, sock, cmd, h, sizeof(*h), 0);
}

static enum drbd_packets drbd_recv_fp(struct drbd_conf *mdev, struct socket *sock)
{
	struct p_header80 *h = &mdev->data.rbuf.header.h80;
	int rr;

	rr = drbd_recv_short(mdev, sock, h, sizeof(*h), 0);

	if (rr == sizeof(*h) && h->magic == BE_DRBD_MAGIC)
		return be16_to_cpu(h->command);

	return 0xffff;
}

/**
 * drbd_socket_okay() - Free the socket if its connection is not okay
 * @mdev:	DRBD device.
 * @sock:	pointer to the pointer to the socket.
 */
static int drbd_socket_okay(struct drbd_conf *mdev, struct socket **sock)
{
	int rr;
	char tb[4];

	if (!*sock)
		return false;

	rr = drbd_recv_short(mdev, *sock, tb, 4, MSG_DONTWAIT | MSG_PEEK);

	if (rr > 0 || rr == -EAGAIN) {
		return true;
	} else {
		sock_release(*sock);
		*sock = NULL;
		return false;
	}
}

/*
 * return values:
 *   1 yes, we have a valid connection
 *   0 oops, did not work out, please try again
 *  -1 peer talks different language,
 *     no point in trying again, please go standalone.
 *  -2 We do not have a network config...
 */
static int drbd_connect(struct drbd_conf *mdev)
{
	struct socket *s, *sock, *msock;
	int try, h, ok;

	D_ASSERT(!mdev->data.socket);

	if (drbd_request_state(mdev, NS(conn, C_WF_CONNECTION)) < SS_SUCCESS)
		return -2;

	clear_bit(DISCARD_CONCURRENT, &mdev->flags);

	sock  = NULL;
	msock = NULL;

	do {
		for (try = 0;;) {
			/* 3 tries, this should take less than a second! */
			s = drbd_try_connect(mdev);
			if (s || ++try >= 3)
				break;
			/* give the other side time to call bind() & listen() */
			schedule_timeout_interruptible(HZ / 10);
		}

		if (s) {
			if (!sock) {
				drbd_send_fp(mdev, s, P_HAND_SHAKE_S);
				sock = s;
				s = NULL;
			} else if (!msock) {
				drbd_send_fp(mdev, s, P_HAND_SHAKE_M);
				msock = s;
				s = NULL;
			} else {
				dev_err(DEV, "Logic error in drbd_connect()\n");
				goto out_release_sockets;
			}
		}

		if (sock && msock) {
			schedule_timeout_interruptible(mdev->net_conf->ping_timeo*HZ/10);
			ok = drbd_socket_okay(mdev, &sock);
			ok = drbd_socket_okay(mdev, &msock) && ok;
			if (ok)
				break;
		}

retry:
		s = drbd_wait_for_connect(mdev);
		if (s) {
			try = drbd_recv_fp(mdev, s);
			drbd_socket_okay(mdev, &sock);
			drbd_socket_okay(mdev, &msock);
			switch (try) {
			case P_HAND_SHAKE_S:
				if (sock) {
					dev_warn(DEV, "initial packet S crossed\n");
					sock_release(sock);
				}
				sock = s;
				break;
			case P_HAND_SHAKE_M:
				if (msock) {
					dev_warn(DEV, "initial packet M crossed\n");
					sock_release(msock);
				}
				msock = s;
				set_bit(DISCARD_CONCURRENT, &mdev->flags);
				break;
			default:
				dev_warn(DEV, "Error receiving initial packet\n");
				sock_release(s);
				if (random32() & 1)
					goto retry;
			}
		}

		if (mdev->state.conn <= C_DISCONNECTING)
			goto out_release_sockets;
		if (signal_pending(current)) {
			flush_signals(current);
			smp_rmb();
			if (get_t_state(&mdev->receiver) == Exiting)
				goto out_release_sockets;
		}

		if (sock && msock) {
			ok = drbd_socket_okay(mdev, &sock);
			ok = drbd_socket_okay(mdev, &msock) && ok;
			if (ok)
				break;
		}
	} while (1);

	msock->sk->sk_reuse = 1; /* SO_REUSEADDR */
	sock->sk->sk_reuse = 1; /* SO_REUSEADDR */

	sock->sk->sk_allocation = GFP_NOIO;
	msock->sk->sk_allocation = GFP_NOIO;

	sock->sk->sk_priority = TC_PRIO_INTERACTIVE_BULK;
	msock->sk->sk_priority = TC_PRIO_INTERACTIVE;

	/* NOT YET ...
	 * sock->sk->sk_sndtimeo = mdev->net_conf->timeout*HZ/10;
	 * sock->sk->sk_rcvtimeo = MAX_SCHEDULE_TIMEOUT;
	 * first set it to the P_HAND_SHAKE timeout,
	 * which we set to 4x the configured ping_timeout. */
	sock->sk->sk_sndtimeo =
	sock->sk->sk_rcvtimeo = mdev->net_conf->ping_timeo*4*HZ/10;

	msock->sk->sk_sndtimeo = mdev->net_conf->timeout*HZ/10;
	msock->sk->sk_rcvtimeo = mdev->net_conf->ping_int*HZ;

	/* we don't want delays.
	 * we use TCP_CORK where appropriate, though */
	drbd_tcp_nodelay(sock);
	drbd_tcp_nodelay(msock);

	mdev->data.socket = sock;
	mdev->meta.socket = msock;
	mdev->last_received = jiffies;

	D_ASSERT(mdev->asender.task == NULL);

	h = drbd_do_handshake(mdev);
	if (h <= 0)
		return h;

	if (mdev->cram_hmac_tfm) {
		/* drbd_request_state(mdev, NS(conn, WFAuth)); */
		switch (drbd_do_auth(mdev)) {
		case -1:
			dev_err(DEV, "Authentication of peer failed\n");
			return -1;
		case 0:
			dev_err(DEV, "Authentication of peer failed, trying again.\n");
			return 0;
		}
	}

	if (drbd_request_state(mdev, NS(conn, C_WF_REPORT_PARAMS)) < SS_SUCCESS)
		return 0;

	sock->sk->sk_sndtimeo = mdev->net_conf->timeout*HZ/10;
	sock->sk->sk_rcvtimeo = MAX_SCHEDULE_TIMEOUT;

	atomic_set(&mdev->packet_seq, 0);
	mdev->peer_seq = 0;

	drbd_thread_start(&mdev->asender);

	if (drbd_send_protocol(mdev) == -1)
		return -1;
	drbd_send_sync_param(mdev, &mdev->sync_conf);
	drbd_send_sizes(mdev, 0, 0);
	drbd_send_uuids(mdev);
	drbd_send_state(mdev);
	clear_bit(USE_DEGR_WFC_T, &mdev->flags);
	clear_bit(RESIZE_PENDING, &mdev->flags);
	mod_timer(&mdev->request_timer, jiffies + HZ); /* just start it here. */

	return 1;

out_release_sockets:
	if (sock)
		sock_release(sock);
	if (msock)
		sock_release(msock);
	return -1;
}

static int drbd_recv_header(struct drbd_conf *mdev, enum drbd_packets *cmd, unsigned int *packet_size)
{
	union p_header *h = &mdev->data.rbuf.header;
	int r;

	r = drbd_recv(mdev, h, sizeof(*h));
	if (unlikely(r != sizeof(*h))) {
		if (!signal_pending(current))
			dev_warn(DEV, "short read expecting header on sock: r=%d\n", r);
		return false;
	}

	if (likely(h->h80.magic == BE_DRBD_MAGIC)) {
		*cmd = be16_to_cpu(h->h80.command);
		*packet_size = be16_to_cpu(h->h80.length);
	} else if (h->h95.magic == BE_DRBD_MAGIC_BIG) {
		*cmd = be16_to_cpu(h->h95.command);
		*packet_size = be32_to_cpu(h->h95.length);
	} else {
		dev_err(DEV, "magic?? on data m: 0x%08x c: %d l: %d\n",
		    be32_to_cpu(h->h80.magic),
		    be16_to_cpu(h->h80.command),
		    be16_to_cpu(h->h80.length));
		return false;
	}
	mdev->last_received = jiffies;

	return true;
}

static void drbd_flush(struct drbd_conf *mdev)
{
	int rv;

	if (mdev->write_ordering >= WO_bdev_flush && get_ldev(mdev)) {
		rv = blkdev_issue_flush(mdev->ldev->backing_bdev, GFP_KERNEL,
					NULL);
		if (rv) {
			dev_info(DEV, "local disk flush failed with status %d\n", rv);
			/* would rather check on EOPNOTSUPP, but that is not reliable.
			 * don't try again for ANY return value != 0
			 * if (rv == -EOPNOTSUPP) */
			drbd_bump_write_ordering(mdev, WO_drain_io);
		}
		put_ldev(mdev);
	}
}

/**
 * drbd_may_finish_epoch() - Applies an epoch_event to the epoch's state, eventually finishes it.
 * @mdev:	DRBD device.
 * @epoch:	Epoch object.
 * @ev:		Epoch event.
 */
static enum finish_epoch drbd_may_finish_epoch(struct drbd_conf *mdev,
					       struct drbd_epoch *epoch,
					       enum epoch_event ev)
{
	int epoch_size;
	struct drbd_epoch *next_epoch;
	enum finish_epoch rv = FE_STILL_LIVE;

	spin_lock(&mdev->epoch_lock);
	do {
		next_epoch = NULL;

		epoch_size = atomic_read(&epoch->epoch_size);

		switch (ev & ~EV_CLEANUP) {
		case EV_PUT:
			atomic_dec(&epoch->active);
			break;
		case EV_GOT_BARRIER_NR:
			set_bit(DE_HAVE_BARRIER_NUMBER, &epoch->flags);
			break;
		case EV_BECAME_LAST:
			/* nothing to do*/
			break;
		}

		if (epoch_size != 0 &&
		    atomic_read(&epoch->active) == 0 &&
		    (test_bit(DE_HAVE_BARRIER_NUMBER, &epoch->flags) || ev & EV_CLEANUP)) {
			if (!(ev & EV_CLEANUP)) {
				spin_unlock(&mdev->epoch_lock);
				drbd_send_b_ack(mdev, epoch->barrier_nr, epoch_size);
				spin_lock(&mdev->epoch_lock);
			}
			if (test_bit(DE_HAVE_BARRIER_NUMBER, &epoch->flags))
				dec_unacked(mdev);

			if (mdev->current_epoch != epoch) {
				next_epoch = list_entry(epoch->list.next, struct drbd_epoch, list);
				list_del(&epoch->list);
				ev = EV_BECAME_LAST | (ev & EV_CLEANUP);
				mdev->epochs--;
				kfree(epoch);

				if (rv == FE_STILL_LIVE)
					rv = FE_DESTROYED;
			} else {
				epoch->flags = 0;
				atomic_set(&epoch->epoch_size, 0);
				/* atomic_set(&epoch->active, 0); is already zero */
				if (rv == FE_STILL_LIVE)
					rv = FE_RECYCLED;
				wake_up(&mdev->ee_wait);
			}
		}

		if (!next_epoch)
			break;

		epoch = next_epoch;
	} while (1);

	spin_unlock(&mdev->epoch_lock);

	return rv;
}

/**
 * drbd_bump_write_ordering() - Fall back to an other write ordering method
 * @mdev:	DRBD device.
 * @wo:		Write ordering method to try.
 */
void drbd_bump_write_ordering(struct drbd_conf *mdev, enum write_ordering_e wo) __must_hold(local)
{
	enum write_ordering_e pwo;
	static char *write_ordering_str[] = {
		[WO_none] = "none",
		[WO_drain_io] = "drain",
		[WO_bdev_flush] = "flush",
	};

	pwo = mdev->write_ordering;
	wo = min(pwo, wo);
	if (wo == WO_bdev_flush && mdev->ldev->dc.no_disk_flush)
		wo = WO_drain_io;
	if (wo == WO_drain_io && mdev->ldev->dc.no_disk_drain)
		wo = WO_none;
	mdev->write_ordering = wo;
	if (pwo != mdev->write_ordering || wo == WO_bdev_flush)
		dev_info(DEV, "Method to ensure write ordering: %s\n", write_ordering_str[mdev->write_ordering]);
}

/**
 * drbd_submit_ee()
 * @mdev:	DRBD device.
 * @e:		epoch entry
 * @rw:		flag field, see bio->bi_rw
 *
 * May spread the pages to multiple bios,
 * depending on bio_add_page restrictions.
 *
 * Returns 0 if all bios have been submitted,
 * -ENOMEM if we could not allocate enough bios,
 * -ENOSPC (any better suggestion?) if we have not been able to bio_add_page a
 *  single page to an empty bio (which should never happen and likely indicates
 *  that the lower level IO stack is in some way broken). This has been observed
 *  on certain Xen deployments.
 */
/* TODO allocate from our own bio_set. */
int drbd_submit_ee(struct drbd_conf *mdev, struct drbd_epoch_entry *e,
		const unsigned rw, const int fault_type)
{
	struct bio *bios = NULL;
	struct bio *bio;
	struct page *page = e->pages;
	sector_t sector = e->sector;
	unsigned ds = e->size;
	unsigned n_bios = 0;
	unsigned nr_pages = (ds + PAGE_SIZE -1) >> PAGE_SHIFT;
	int err = -ENOMEM;

	/* In most cases, we will only need one bio.  But in case the lower
	 * level restrictions happen to be different at this offset on this
	 * side than those of the sending peer, we may need to submit the
	 * request in more than one bio. */
next_bio:
	bio = bio_alloc(GFP_NOIO, nr_pages);
	if (!bio) {
		dev_err(DEV, "submit_ee: Allocation of a bio failed\n");
		goto fail;
	}
	/* > e->sector, unless this is the first bio */
	bio->bi_sector = sector;
	bio->bi_bdev = mdev->ldev->backing_bdev;
	bio->bi_rw = rw;
	bio->bi_private = e;
	bio->bi_end_io = drbd_endio_sec;

	bio->bi_next = bios;
	bios = bio;
	++n_bios;

	page_chain_for_each(page) {
		unsigned len = min_t(unsigned, ds, PAGE_SIZE);
		if (!bio_add_page(bio, page, len, 0)) {
			/* A single page must always be possible!
			 * But in case it fails anyways,
			 * we deal with it, and complain (below). */
			if (bio->bi_vcnt == 0) {
				dev_err(DEV,
					"bio_add_page failed for len=%u, "
					"bi_vcnt=0 (bi_sector=%llu)\n",
					len, (unsigned long long)bio->bi_sector);
				err = -ENOSPC;
				goto fail;
			}
			goto next_bio;
		}
		ds -= len;
		sector += len >> 9;
		--nr_pages;
	}
	D_ASSERT(page == NULL);
	D_ASSERT(ds == 0);

	atomic_set(&e->pending_bios, n_bios);
	do {
		bio = bios;
		bios = bios->bi_next;
		bio->bi_next = NULL;

		drbd_generic_make_request(mdev, fault_type, bio);
	} while (bios);
	return 0;

fail:
	while (bios) {
		bio = bios;
		bios = bios->bi_next;
		bio_put(bio);
	}
	return err;
}

static int receive_Barrier(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	int rv;
	struct p_barrier *p = &mdev->data.rbuf.barrier;
	struct drbd_epoch *epoch;

	inc_unacked(mdev);

	mdev->current_epoch->barrier_nr = p->barrier;
	rv = drbd_may_finish_epoch(mdev, mdev->current_epoch, EV_GOT_BARRIER_NR);

	/* P_BARRIER_ACK may imply that the corresponding extent is dropped from
	 * the activity log, which means it would not be resynced in case the
	 * R_PRIMARY crashes now.
	 * Therefore we must send the barrier_ack after the barrier request was
	 * completed. */
	switch (mdev->write_ordering) {
	case WO_none:
		if (rv == FE_RECYCLED)
			return true;

		/* receiver context, in the writeout path of the other node.
		 * avoid potential distributed deadlock */
		epoch = kmalloc(sizeof(struct drbd_epoch), GFP_NOIO);
		if (epoch)
			break;
		else
			dev_warn(DEV, "Allocation of an epoch failed, slowing down\n");
			/* Fall through */

	case WO_bdev_flush:
	case WO_drain_io:
		drbd_wait_ee_list_empty(mdev, &mdev->active_ee);
		drbd_flush(mdev);

		if (atomic_read(&mdev->current_epoch->epoch_size)) {
			epoch = kmalloc(sizeof(struct drbd_epoch), GFP_NOIO);
			if (epoch)
				break;
		}

		epoch = mdev->current_epoch;
		wait_event(mdev->ee_wait, atomic_read(&epoch->epoch_size) == 0);

		D_ASSERT(atomic_read(&epoch->active) == 0);
		D_ASSERT(epoch->flags == 0);

		return true;
	default:
		dev_err(DEV, "Strangeness in mdev->write_ordering %d\n", mdev->write_ordering);
		return false;
	}

	epoch->flags = 0;
	atomic_set(&epoch->epoch_size, 0);
	atomic_set(&epoch->active, 0);

	spin_lock(&mdev->epoch_lock);
	if (atomic_read(&mdev->current_epoch->epoch_size)) {
		list_add(&epoch->list, &mdev->current_epoch->list);
		mdev->current_epoch = epoch;
		mdev->epochs++;
	} else {
		/* The current_epoch got recycled while we allocated this one... */
		kfree(epoch);
	}
	spin_unlock(&mdev->epoch_lock);

	return true;
}

/* used from receive_RSDataReply (recv_resync_read)
 * and from receive_Data */
static struct drbd_epoch_entry *
read_in_block(struct drbd_conf *mdev, u64 id, sector_t sector, int data_size) __must_hold(local)
{
	const sector_t capacity = drbd_get_capacity(mdev->this_bdev);
	struct drbd_epoch_entry *e;
	struct page *page;
	int dgs, ds, rr;
	void *dig_in = mdev->int_dig_in;
	void *dig_vv = mdev->int_dig_vv;
	unsigned long *data;

	dgs = (mdev->agreed_pro_version >= 87 && mdev->integrity_r_tfm) ?
		crypto_hash_digestsize(mdev->integrity_r_tfm) : 0;

	if (dgs) {
		rr = drbd_recv(mdev, dig_in, dgs);
		if (rr != dgs) {
			if (!signal_pending(current))
				dev_warn(DEV,
					"short read receiving data digest: read %d expected %d\n",
					rr, dgs);
			return NULL;
		}
	}

	data_size -= dgs;

	ERR_IF(data_size == 0) return NULL;
	ERR_IF(data_size &  0x1ff) return NULL;
	ERR_IF(data_size >  DRBD_MAX_BIO_SIZE) return NULL;

	/* even though we trust out peer,
	 * we sometimes have to double check. */
	if (sector + (data_size>>9) > capacity) {
		dev_err(DEV, "request from peer beyond end of local disk: "
			"capacity: %llus < sector: %llus + size: %u\n",
			(unsigned long long)capacity,
			(unsigned long long)sector, data_size);
		return NULL;
	}

	/* GFP_NOIO, because we must not cause arbitrary write-out: in a DRBD
	 * "criss-cross" setup, that might cause write-out on some other DRBD,
	 * which in turn might block on the other node at this very place.  */
	e = drbd_alloc_ee(mdev, id, sector, data_size, GFP_NOIO);
	if (!e)
		return NULL;

	ds = data_size;
	page = e->pages;
	page_chain_for_each(page) {
		unsigned len = min_t(int, ds, PAGE_SIZE);
		data = kmap(page);
		rr = drbd_recv(mdev, data, len);
		if (drbd_insert_fault(mdev, DRBD_FAULT_RECEIVE)) {
			dev_err(DEV, "Fault injection: Corrupting data on receive\n");
			data[0] = data[0] ^ (unsigned long)-1;
		}
		kunmap(page);
		if (rr != len) {
			drbd_free_ee(mdev, e);
			if (!signal_pending(current))
				dev_warn(DEV, "short read receiving data: read %d expected %d\n",
				rr, len);
			return NULL;
		}
		ds -= rr;
	}

	if (dgs) {
		drbd_csum_ee(mdev, mdev->integrity_r_tfm, e, dig_vv);
		if (memcmp(dig_in, dig_vv, dgs)) {
			dev_err(DEV, "Digest integrity check FAILED: %llus +%u\n",
				(unsigned long long)sector, data_size);
			drbd_bcast_ee(mdev, "digest failed",
					dgs, dig_in, dig_vv, e);
			drbd_free_ee(mdev, e);
			return NULL;
		}
	}
	mdev->recv_cnt += data_size>>9;
	return e;
}

/* drbd_drain_block() just takes a data block
 * out of the socket input buffer, and discards it.
 */
static int drbd_drain_block(struct drbd_conf *mdev, int data_size)
{
	struct page *page;
	int rr, rv = 1;
	void *data;

	if (!data_size)
		return true;

	page = drbd_pp_alloc(mdev, 1, 1);

	data = kmap(page);
	while (data_size) {
		rr = drbd_recv(mdev, data, min_t(int, data_size, PAGE_SIZE));
		if (rr != min_t(int, data_size, PAGE_SIZE)) {
			rv = 0;
			if (!signal_pending(current))
				dev_warn(DEV,
					"short read receiving data: read %d expected %d\n",
					rr, min_t(int, data_size, PAGE_SIZE));
			break;
		}
		data_size -= rr;
	}
	kunmap(page);
	drbd_pp_free(mdev, page, 0);
	return rv;
}

static int recv_dless_read(struct drbd_conf *mdev, struct drbd_request *req,
			   sector_t sector, int data_size)
{
	struct bio_vec *bvec;
	struct bio *bio;
	int dgs, rr, i, expect;
	void *dig_in = mdev->int_dig_in;
	void *dig_vv = mdev->int_dig_vv;

	dgs = (mdev->agreed_pro_version >= 87 && mdev->integrity_r_tfm) ?
		crypto_hash_digestsize(mdev->integrity_r_tfm) : 0;

	if (dgs) {
		rr = drbd_recv(mdev, dig_in, dgs);
		if (rr != dgs) {
			if (!signal_pending(current))
				dev_warn(DEV,
					"short read receiving data reply digest: read %d expected %d\n",
					rr, dgs);
			return 0;
		}
	}

	data_size -= dgs;

	/* optimistically update recv_cnt.  if receiving fails below,
	 * we disconnect anyways, and counters will be reset. */
	mdev->recv_cnt += data_size>>9;

	bio = req->master_bio;
	D_ASSERT(sector == bio->bi_sector);

	bio_for_each_segment(bvec, bio, i) {
		expect = min_t(int, data_size, bvec->bv_len);
		rr = drbd_recv(mdev,
			     kmap(bvec->bv_page)+bvec->bv_offset,
			     expect);
		kunmap(bvec->bv_page);
		if (rr != expect) {
			if (!signal_pending(current))
				dev_warn(DEV, "short read receiving data reply: "
					"read %d expected %d\n",
					rr, expect);
			return 0;
		}
		data_size -= rr;
	}

	if (dgs) {
		drbd_csum_bio(mdev, mdev->integrity_r_tfm, bio, dig_vv);
		if (memcmp(dig_in, dig_vv, dgs)) {
			dev_err(DEV, "Digest integrity check FAILED. Broken NICs?\n");
			return 0;
		}
	}

	D_ASSERT(data_size == 0);
	return 1;
}

/* e_end_resync_block() is called via
 * drbd_process_done_ee() by asender only */
static int e_end_resync_block(struct drbd_conf *mdev, struct drbd_work *w, int unused)
{
	struct drbd_epoch_entry *e = (struct drbd_epoch_entry *)w;
	sector_t sector = e->sector;
	int ok;

	D_ASSERT(hlist_unhashed(&e->collision));

	if (likely((e->flags & EE_WAS_ERROR) == 0)) {
		drbd_set_in_sync(mdev, sector, e->size);
		ok = drbd_send_ack(mdev, P_RS_WRITE_ACK, e);
	} else {
		/* Record failure to sync */
		drbd_rs_failed_io(mdev, sector, e->size);

		ok  = drbd_send_ack(mdev, P_NEG_ACK, e);
	}
	dec_unacked(mdev);

	return ok;
}

static int recv_resync_read(struct drbd_conf *mdev, sector_t sector, int data_size) __releases(local)
{
	struct drbd_epoch_entry *e;

	e = read_in_block(mdev, ID_SYNCER, sector, data_size);
	if (!e)
		goto fail;

	dec_rs_pending(mdev);

	inc_unacked(mdev);
	/* corresponding dec_unacked() in e_end_resync_block()
	 * respective _drbd_clear_done_ee */

	e->w.cb = e_end_resync_block;

	spin_lock_irq(&mdev->req_lock);
	list_add(&e->w.list, &mdev->sync_ee);
	spin_unlock_irq(&mdev->req_lock);

	atomic_add(data_size >> 9, &mdev->rs_sect_ev);
	if (drbd_submit_ee(mdev, e, WRITE, DRBD_FAULT_RS_WR) == 0)
		return true;

	/* don't care for the reason here */
	dev_err(DEV, "submit failed, triggering re-connect\n");
	spin_lock_irq(&mdev->req_lock);
	list_del(&e->w.list);
	spin_unlock_irq(&mdev->req_lock);

	drbd_free_ee(mdev, e);
fail:
	put_ldev(mdev);
	return false;
}

static int receive_DataReply(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	struct drbd_request *req;
	sector_t sector;
	int ok;
	struct p_data *p = &mdev->data.rbuf.data;

	sector = be64_to_cpu(p->sector);

	spin_lock_irq(&mdev->req_lock);
	req = _ar_id_to_req(mdev, p->block_id, sector);
	spin_unlock_irq(&mdev->req_lock);
	if (unlikely(!req)) {
		dev_err(DEV, "Got a corrupt block_id/sector pair(1).\n");
		return false;
	}

	/* hlist_del(&req->collision) is done in _req_may_be_done, to avoid
	 * special casing it there for the various failure cases.
	 * still no race with drbd_fail_pending_reads */
	ok = recv_dless_read(mdev, req, sector, data_size);

	if (ok)
		req_mod(req, data_received);
	/* else: nothing. handled from drbd_disconnect...
	 * I don't think we may complete this just yet
	 * in case we are "on-disconnect: freeze" */

	return ok;
}

static int receive_RSDataReply(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	sector_t sector;
	int ok;
	struct p_data *p = &mdev->data.rbuf.data;

	sector = be64_to_cpu(p->sector);
	D_ASSERT(p->block_id == ID_SYNCER);

	if (get_ldev(mdev)) {
		/* data is submitted to disk within recv_resync_read.
		 * corresponding put_ldev done below on error,
		 * or in drbd_endio_write_sec. */
		ok = recv_resync_read(mdev, sector, data_size);
	} else {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "Can not write resync data to local disk.\n");

		ok = drbd_drain_block(mdev, data_size);

		drbd_send_ack_dp(mdev, P_NEG_ACK, p, data_size);
	}

	atomic_add(data_size >> 9, &mdev->rs_sect_in);

	return ok;
}

/* e_end_block() is called via drbd_process_done_ee().
 * this means this function only runs in the asender thread
 */
static int e_end_block(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_epoch_entry *e = (struct drbd_epoch_entry *)w;
	sector_t sector = e->sector;
	int ok = 1, pcmd;

	if (mdev->net_conf->wire_protocol == DRBD_PROT_C) {
		if (likely((e->flags & EE_WAS_ERROR) == 0)) {
			pcmd = (mdev->state.conn >= C_SYNC_SOURCE &&
				mdev->state.conn <= C_PAUSED_SYNC_T &&
				e->flags & EE_MAY_SET_IN_SYNC) ?
				P_RS_WRITE_ACK : P_WRITE_ACK;
			ok &= drbd_send_ack(mdev, pcmd, e);
			if (pcmd == P_RS_WRITE_ACK)
				drbd_set_in_sync(mdev, sector, e->size);
		} else {
			ok  = drbd_send_ack(mdev, P_NEG_ACK, e);
			/* we expect it to be marked out of sync anyways...
			 * maybe assert this?  */
		}
		dec_unacked(mdev);
	}
	/* we delete from the conflict detection hash _after_ we sent out the
	 * P_WRITE_ACK / P_NEG_ACK, to get the sequence number right.  */
	if (mdev->net_conf->two_primaries) {
		spin_lock_irq(&mdev->req_lock);
		D_ASSERT(!hlist_unhashed(&e->collision));
		hlist_del_init(&e->collision);
		spin_unlock_irq(&mdev->req_lock);
	} else {
		D_ASSERT(hlist_unhashed(&e->collision));
	}

	drbd_may_finish_epoch(mdev, e->epoch, EV_PUT + (cancel ? EV_CLEANUP : 0));

	return ok;
}

static int e_send_discard_ack(struct drbd_conf *mdev, struct drbd_work *w, int unused)
{
	struct drbd_epoch_entry *e = (struct drbd_epoch_entry *)w;
	int ok = 1;

	D_ASSERT(mdev->net_conf->wire_protocol == DRBD_PROT_C);
	ok = drbd_send_ack(mdev, P_DISCARD_ACK, e);

	spin_lock_irq(&mdev->req_lock);
	D_ASSERT(!hlist_unhashed(&e->collision));
	hlist_del_init(&e->collision);
	spin_unlock_irq(&mdev->req_lock);

	dec_unacked(mdev);

	return ok;
}

/* Called from receive_Data.
 * Synchronize packets on sock with packets on msock.
 *
 * This is here so even when a P_DATA packet traveling via sock overtook an Ack
 * packet traveling on msock, they are still processed in the order they have
 * been sent.
 *
 * Note: we don't care for Ack packets overtaking P_DATA packets.
 *
 * In case packet_seq is larger than mdev->peer_seq number, there are
 * outstanding packets on the msock. We wait for them to arrive.
 * In case we are the logically next packet, we update mdev->peer_seq
 * ourselves. Correctly handles 32bit wrap around.
 *
 * Assume we have a 10 GBit connection, that is about 1<<30 byte per second,
 * about 1<<21 sectors per second. So "worst" case, we have 1<<3 == 8 seconds
 * for the 24bit wrap (historical atomic_t guarantee on some archs), and we have
 * 1<<9 == 512 seconds aka ages for the 32bit wrap around...
 *
 * returns 0 if we may process the packet,
 * -ERESTARTSYS if we were interrupted (by disconnect signal). */
static int drbd_wait_peer_seq(struct drbd_conf *mdev, const u32 packet_seq)
{
	DEFINE_WAIT(wait);
	unsigned int p_seq;
	long timeout;
	int ret = 0;
	spin_lock(&mdev->peer_seq_lock);
	for (;;) {
		prepare_to_wait(&mdev->seq_wait, &wait, TASK_INTERRUPTIBLE);
		if (seq_le(packet_seq, mdev->peer_seq+1))
			break;
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		p_seq = mdev->peer_seq;
		spin_unlock(&mdev->peer_seq_lock);
		timeout = schedule_timeout(30*HZ);
		spin_lock(&mdev->peer_seq_lock);
		if (timeout == 0 && p_seq == mdev->peer_seq) {
			ret = -ETIMEDOUT;
			dev_err(DEV, "ASSERT FAILED waited 30 seconds for sequence update, forcing reconnect\n");
			break;
		}
	}
	finish_wait(&mdev->seq_wait, &wait);
	if (mdev->peer_seq+1 == packet_seq)
		mdev->peer_seq++;
	spin_unlock(&mdev->peer_seq_lock);
	return ret;
}

/* see also bio_flags_to_wire()
 * DRBD_REQ_*, because we need to semantically map the flags to data packet
 * flags and back. We may replicate to other kernel versions. */
static unsigned long wire_flags_to_bio(struct drbd_conf *mdev, u32 dpf)
{
	return  (dpf & DP_RW_SYNC ? REQ_SYNC : 0) |
		(dpf & DP_FUA ? REQ_FUA : 0) |
		(dpf & DP_FLUSH ? REQ_FLUSH : 0) |
		(dpf & DP_DISCARD ? REQ_DISCARD : 0);
}

/* mirrored write */
static int receive_Data(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	sector_t sector;
	struct drbd_epoch_entry *e;
	struct p_data *p = &mdev->data.rbuf.data;
	int rw = WRITE;
	u32 dp_flags;

	if (!get_ldev(mdev)) {
		spin_lock(&mdev->peer_seq_lock);
		if (mdev->peer_seq+1 == be32_to_cpu(p->seq_num))
			mdev->peer_seq++;
		spin_unlock(&mdev->peer_seq_lock);

		drbd_send_ack_dp(mdev, P_NEG_ACK, p, data_size);
		atomic_inc(&mdev->current_epoch->epoch_size);
		return drbd_drain_block(mdev, data_size);
	}

	/* get_ldev(mdev) successful.
	 * Corresponding put_ldev done either below (on various errors),
	 * or in drbd_endio_write_sec, if we successfully submit the data at
	 * the end of this function. */

	sector = be64_to_cpu(p->sector);
	e = read_in_block(mdev, p->block_id, sector, data_size);
	if (!e) {
		put_ldev(mdev);
		return false;
	}

	e->w.cb = e_end_block;

	dp_flags = be32_to_cpu(p->dp_flags);
	rw |= wire_flags_to_bio(mdev, dp_flags);

	if (dp_flags & DP_MAY_SET_IN_SYNC)
		e->flags |= EE_MAY_SET_IN_SYNC;

	spin_lock(&mdev->epoch_lock);
	e->epoch = mdev->current_epoch;
	atomic_inc(&e->epoch->epoch_size);
	atomic_inc(&e->epoch->active);
	spin_unlock(&mdev->epoch_lock);

	/* I'm the receiver, I do hold a net_cnt reference. */
	if (!mdev->net_conf->two_primaries) {
		spin_lock_irq(&mdev->req_lock);
	} else {
		/* don't get the req_lock yet,
		 * we may sleep in drbd_wait_peer_seq */
		const int size = e->size;
		const int discard = test_bit(DISCARD_CONCURRENT, &mdev->flags);
		DEFINE_WAIT(wait);
		struct drbd_request *i;
		struct hlist_node *n;
		struct hlist_head *slot;
		int first;

		D_ASSERT(mdev->net_conf->wire_protocol == DRBD_PROT_C);
		BUG_ON(mdev->ee_hash == NULL);
		BUG_ON(mdev->tl_hash == NULL);

		/* conflict detection and handling:
		 * 1. wait on the sequence number,
		 *    in case this data packet overtook ACK packets.
		 * 2. check our hash tables for conflicting requests.
		 *    we only need to walk the tl_hash, since an ee can not
		 *    have a conflict with an other ee: on the submitting
		 *    node, the corresponding req had already been conflicting,
		 *    and a conflicting req is never sent.
		 *
		 * Note: for two_primaries, we are protocol C,
		 * so there cannot be any request that is DONE
		 * but still on the transfer log.
		 *
		 * unconditionally add to the ee_hash.
		 *
		 * if no conflicting request is found:
		 *    submit.
		 *
		 * if any conflicting request is found
		 * that has not yet been acked,
		 * AND I have the "discard concurrent writes" flag:
		 *	 queue (via done_ee) the P_DISCARD_ACK; OUT.
		 *
		 * if any conflicting request is found:
		 *	 block the receiver, waiting on misc_wait
		 *	 until no more conflicting requests are there,
		 *	 or we get interrupted (disconnect).
		 *
		 *	 we do not just write after local io completion of those
		 *	 requests, but only after req is done completely, i.e.
		 *	 we wait for the P_DISCARD_ACK to arrive!
		 *
		 *	 then proceed normally, i.e. submit.
		 */
		if (drbd_wait_peer_seq(mdev, be32_to_cpu(p->seq_num)))
			goto out_interrupted;

		spin_lock_irq(&mdev->req_lock);

		hlist_add_head(&e->collision, ee_hash_slot(mdev, sector));

#define OVERLAPS overlaps(i->sector, i->size, sector, size)
		slot = tl_hash_slot(mdev, sector);
		first = 1;
		for (;;) {
			int have_unacked = 0;
			int have_conflict = 0;
			prepare_to_wait(&mdev->misc_wait, &wait,
				TASK_INTERRUPTIBLE);
			hlist_for_each_entry(i, n, slot, collision) {
				if (OVERLAPS) {
					/* only ALERT on first iteration,
					 * we may be woken up early... */
					if (first)
						dev_alert(DEV, "%s[%u] Concurrent local write detected!"
						      "	new: %llus +%u; pending: %llus +%u\n",
						      current->comm, current->pid,
						      (unsigned long long)sector, size,
						      (unsigned long long)i->sector, i->size);
					if (i->rq_state & RQ_NET_PENDING)
						++have_unacked;
					++have_conflict;
				}
			}
#undef OVERLAPS
			if (!have_conflict)
				break;

			/* Discard Ack only for the _first_ iteration */
			if (first && discard && have_unacked) {
				dev_alert(DEV, "Concurrent write! [DISCARD BY FLAG] sec=%llus\n",
				     (unsigned long long)sector);
				inc_unacked(mdev);
				e->w.cb = e_send_discard_ack;
				list_add_tail(&e->w.list, &mdev->done_ee);

				spin_unlock_irq(&mdev->req_lock);

				/* we could probably send that P_DISCARD_ACK ourselves,
				 * but I don't like the receiver using the msock */

				put_ldev(mdev);
				wake_asender(mdev);
				finish_wait(&mdev->misc_wait, &wait);
				return true;
			}

			if (signal_pending(current)) {
				hlist_del_init(&e->collision);

				spin_unlock_irq(&mdev->req_lock);

				finish_wait(&mdev->misc_wait, &wait);
				goto out_interrupted;
			}

			spin_unlock_irq(&mdev->req_lock);
			if (first) {
				first = 0;
				dev_alert(DEV, "Concurrent write! [W AFTERWARDS] "
				     "sec=%llus\n", (unsigned long long)sector);
			} else if (discard) {
				/* we had none on the first iteration.
				 * there must be none now. */
				D_ASSERT(have_unacked == 0);
			}
			schedule();
			spin_lock_irq(&mdev->req_lock);
		}
		finish_wait(&mdev->misc_wait, &wait);
	}

	list_add(&e->w.list, &mdev->active_ee);
	spin_unlock_irq(&mdev->req_lock);

	switch (mdev->net_conf->wire_protocol) {
	case DRBD_PROT_C:
		inc_unacked(mdev);
		/* corresponding dec_unacked() in e_end_block()
		 * respective _drbd_clear_done_ee */
		break;
	case DRBD_PROT_B:
		/* I really don't like it that the receiver thread
		 * sends on the msock, but anyways */
		drbd_send_ack(mdev, P_RECV_ACK, e);
		break;
	case DRBD_PROT_A:
		/* nothing to do */
		break;
	}

	if (mdev->state.pdsk < D_INCONSISTENT) {
		/* In case we have the only disk of the cluster, */
		drbd_set_out_of_sync(mdev, e->sector, e->size);
		e->flags |= EE_CALL_AL_COMPLETE_IO;
		e->flags &= ~EE_MAY_SET_IN_SYNC;
		drbd_al_begin_io(mdev, e->sector);
	}

	if (drbd_submit_ee(mdev, e, rw, DRBD_FAULT_DT_WR) == 0)
		return true;

	/* don't care for the reason here */
	dev_err(DEV, "submit failed, triggering re-connect\n");
	spin_lock_irq(&mdev->req_lock);
	list_del(&e->w.list);
	hlist_del_init(&e->collision);
	spin_unlock_irq(&mdev->req_lock);
	if (e->flags & EE_CALL_AL_COMPLETE_IO)
		drbd_al_complete_io(mdev, e->sector);

out_interrupted:
	drbd_may_finish_epoch(mdev, e->epoch, EV_PUT + EV_CLEANUP);
	put_ldev(mdev);
	drbd_free_ee(mdev, e);
	return false;
}

/* We may throttle resync, if the lower device seems to be busy,
 * and current sync rate is above c_min_rate.
 *
 * To decide whether or not the lower device is busy, we use a scheme similar
 * to MD RAID is_mddev_idle(): if the partition stats reveal "significant"
 * (more than 64 sectors) of activity we cannot account for with our own resync
 * activity, it obviously is "busy".
 *
 * The current sync rate used here uses only the most recent two step marks,
 * to have a short time average so we can react faster.
 */
int drbd_rs_should_slow_down(struct drbd_conf *mdev, sector_t sector)
{
	struct gendisk *disk = mdev->ldev->backing_bdev->bd_contains->bd_disk;
	unsigned long db, dt, dbdt;
	struct lc_element *tmp;
	int curr_events;
	int throttle = 0;

	/* feature disabled? */
	if (mdev->sync_conf.c_min_rate == 0)
		return 0;

	spin_lock_irq(&mdev->al_lock);
	tmp = lc_find(mdev->resync, BM_SECT_TO_EXT(sector));
	if (tmp) {
		struct bm_extent *bm_ext = lc_entry(tmp, struct bm_extent, lce);
		if (test_bit(BME_PRIORITY, &bm_ext->flags)) {
			spin_unlock_irq(&mdev->al_lock);
			return 0;
		}
		/* Do not slow down if app IO is already waiting for this extent */
	}
	spin_unlock_irq(&mdev->al_lock);

	curr_events = (int)part_stat_read(&disk->part0, sectors[0]) +
		      (int)part_stat_read(&disk->part0, sectors[1]) -
			atomic_read(&mdev->rs_sect_ev);

	if (!mdev->rs_last_events || curr_events - mdev->rs_last_events > 64) {
		unsigned long rs_left;
		int i;

		mdev->rs_last_events = curr_events;

		/* sync speed average over the last 2*DRBD_SYNC_MARK_STEP,
		 * approx. */
		i = (mdev->rs_last_mark + DRBD_SYNC_MARKS-1) % DRBD_SYNC_MARKS;

		if (mdev->state.conn == C_VERIFY_S || mdev->state.conn == C_VERIFY_T)
			rs_left = mdev->ov_left;
		else
			rs_left = drbd_bm_total_weight(mdev) - mdev->rs_failed;

		dt = ((long)jiffies - (long)mdev->rs_mark_time[i]) / HZ;
		if (!dt)
			dt++;
		db = mdev->rs_mark_left[i] - rs_left;
		dbdt = Bit2KB(db/dt);

		if (dbdt > mdev->sync_conf.c_min_rate)
			throttle = 1;
	}
	return throttle;
}


static int receive_DataRequest(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int digest_size)
{
	sector_t sector;
	const sector_t capacity = drbd_get_capacity(mdev->this_bdev);
	struct drbd_epoch_entry *e;
	struct digest_info *di = NULL;
	int size, verb;
	unsigned int fault_type;
	struct p_block_req *p =	&mdev->data.rbuf.block_req;

	sector = be64_to_cpu(p->sector);
	size   = be32_to_cpu(p->blksize);

	if (size <= 0 || (size & 0x1ff) != 0 || size > DRBD_MAX_BIO_SIZE) {
		dev_err(DEV, "%s:%d: sector: %llus, size: %u\n", __FILE__, __LINE__,
				(unsigned long long)sector, size);
		return false;
	}
	if (sector + (size>>9) > capacity) {
		dev_err(DEV, "%s:%d: sector: %llus, size: %u\n", __FILE__, __LINE__,
				(unsigned long long)sector, size);
		return false;
	}

	if (!get_ldev_if_state(mdev, D_UP_TO_DATE)) {
		verb = 1;
		switch (cmd) {
		case P_DATA_REQUEST:
			drbd_send_ack_rp(mdev, P_NEG_DREPLY, p);
			break;
		case P_RS_DATA_REQUEST:
		case P_CSUM_RS_REQUEST:
		case P_OV_REQUEST:
			drbd_send_ack_rp(mdev, P_NEG_RS_DREPLY , p);
			break;
		case P_OV_REPLY:
			verb = 0;
			dec_rs_pending(mdev);
			drbd_send_ack_ex(mdev, P_OV_RESULT, sector, size, ID_IN_SYNC);
			break;
		default:
			dev_err(DEV, "unexpected command (%s) in receive_DataRequest\n",
				cmdname(cmd));
		}
		if (verb && __ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "Can not satisfy peer's read request, "
			    "no local data.\n");

		/* drain possibly payload */
		return drbd_drain_block(mdev, digest_size);
	}

	/* GFP_NOIO, because we must not cause arbitrary write-out: in a DRBD
	 * "criss-cross" setup, that might cause write-out on some other DRBD,
	 * which in turn might block on the other node at this very place.  */
	e = drbd_alloc_ee(mdev, p->block_id, sector, size, GFP_NOIO);
	if (!e) {
		put_ldev(mdev);
		return false;
	}

	switch (cmd) {
	case P_DATA_REQUEST:
		e->w.cb = w_e_end_data_req;
		fault_type = DRBD_FAULT_DT_RD;
		/* application IO, don't drbd_rs_begin_io */
		goto submit;

	case P_RS_DATA_REQUEST:
		e->w.cb = w_e_end_rsdata_req;
		fault_type = DRBD_FAULT_RS_RD;
		/* used in the sector offset progress display */
		mdev->bm_resync_fo = BM_SECT_TO_BIT(sector);
		break;

	case P_OV_REPLY:
	case P_CSUM_RS_REQUEST:
		fault_type = DRBD_FAULT_RS_RD;
		di = kmalloc(sizeof(*di) + digest_size, GFP_NOIO);
		if (!di)
			goto out_free_e;

		di->digest_size = digest_size;
		di->digest = (((char *)di)+sizeof(struct digest_info));

		e->digest = di;
		e->flags |= EE_HAS_DIGEST;

		if (drbd_recv(mdev, di->digest, digest_size) != digest_size)
			goto out_free_e;

		if (cmd == P_CSUM_RS_REQUEST) {
			D_ASSERT(mdev->agreed_pro_version >= 89);
			e->w.cb = w_e_end_csum_rs_req;
			/* used in the sector offset progress display */
			mdev->bm_resync_fo = BM_SECT_TO_BIT(sector);
		} else if (cmd == P_OV_REPLY) {
			/* track progress, we may need to throttle */
			atomic_add(size >> 9, &mdev->rs_sect_in);
			e->w.cb = w_e_end_ov_reply;
			dec_rs_pending(mdev);
			/* drbd_rs_begin_io done when we sent this request,
			 * but accounting still needs to be done. */
			goto submit_for_resync;
		}
		break;

	case P_OV_REQUEST:
		if (mdev->ov_start_sector == ~(sector_t)0 &&
		    mdev->agreed_pro_version >= 90) {
			unsigned long now = jiffies;
			int i;
			mdev->ov_start_sector = sector;
			mdev->ov_position = sector;
			mdev->ov_left = drbd_bm_bits(mdev) - BM_SECT_TO_BIT(sector);
			mdev->rs_total = mdev->ov_left;
			for (i = 0; i < DRBD_SYNC_MARKS; i++) {
				mdev->rs_mark_left[i] = mdev->ov_left;
				mdev->rs_mark_time[i] = now;
			}
			dev_info(DEV, "Online Verify start sector: %llu\n",
					(unsigned long long)sector);
		}
		e->w.cb = w_e_end_ov_req;
		fault_type = DRBD_FAULT_RS_RD;
		break;

	default:
		dev_err(DEV, "unexpected command (%s) in receive_DataRequest\n",
		    cmdname(cmd));
		fault_type = DRBD_FAULT_MAX;
		goto out_free_e;
	}

	/* Throttle, drbd_rs_begin_io and submit should become asynchronous
	 * wrt the receiver, but it is not as straightforward as it may seem.
	 * Various places in the resync start and stop logic assume resync
	 * requests are processed in order, requeuing this on the worker thread
	 * introduces a bunch of new code for synchronization between threads.
	 *
	 * Unlimited throttling before drbd_rs_begin_io may stall the resync
	 * "forever", throttling after drbd_rs_begin_io will lock that extent
	 * for application writes for the same time.  For now, just throttle
	 * here, where the rest of the code expects the receiver to sleep for
	 * a while, anyways.
	 */

	/* Throttle before drbd_rs_begin_io, as that locks out application IO;
	 * this defers syncer requests for some time, before letting at least
	 * on request through.  The resync controller on the receiving side
	 * will adapt to the incoming rate accordingly.
	 *
	 * We cannot throttle here if remote is Primary/SyncTarget:
	 * we would also throttle its application reads.
	 * In that case, throttling is done on the SyncTarget only.
	 */
	if (mdev->state.peer != R_PRIMARY && drbd_rs_should_slow_down(mdev, sector))
		schedule_timeout_uninterruptible(HZ/10);
	if (drbd_rs_begin_io(mdev, sector))
		goto out_free_e;

submit_for_resync:
	atomic_add(size >> 9, &mdev->rs_sect_ev);

submit:
	inc_unacked(mdev);
	spin_lock_irq(&mdev->req_lock);
	list_add_tail(&e->w.list, &mdev->read_ee);
	spin_unlock_irq(&mdev->req_lock);

	if (drbd_submit_ee(mdev, e, READ, fault_type) == 0)
		return true;

	/* don't care for the reason here */
	dev_err(DEV, "submit failed, triggering re-connect\n");
	spin_lock_irq(&mdev->req_lock);
	list_del(&e->w.list);
	spin_unlock_irq(&mdev->req_lock);
	/* no drbd_rs_complete_io(), we are dropping the connection anyways */

out_free_e:
	put_ldev(mdev);
	drbd_free_ee(mdev, e);
	return false;
}

static int drbd_asb_recover_0p(struct drbd_conf *mdev) __must_hold(local)
{
	int self, peer, rv = -100;
	unsigned long ch_self, ch_peer;

	self = mdev->ldev->md.uuid[UI_BITMAP] & 1;
	peer = mdev->p_uuid[UI_BITMAP] & 1;

	ch_peer = mdev->p_uuid[UI_SIZE];
	ch_self = mdev->comm_bm_set;

	switch (mdev->net_conf->after_sb_0p) {
	case ASB_CONSENSUS:
	case ASB_DISCARD_SECONDARY:
	case ASB_CALL_HELPER:
		dev_err(DEV, "Configuration error.\n");
		break;
	case ASB_DISCONNECT:
		break;
	case ASB_DISCARD_YOUNGER_PRI:
		if (self == 0 && peer == 1) {
			rv = -1;
			break;
		}
		if (self == 1 && peer == 0) {
			rv =  1;
			break;
		}
		/* Else fall through to one of the other strategies... */
	case ASB_DISCARD_OLDER_PRI:
		if (self == 0 && peer == 1) {
			rv = 1;
			break;
		}
		if (self == 1 && peer == 0) {
			rv = -1;
			break;
		}
		/* Else fall through to one of the other strategies... */
		dev_warn(DEV, "Discard younger/older primary did not find a decision\n"
		     "Using discard-least-changes instead\n");
	case ASB_DISCARD_ZERO_CHG:
		if (ch_peer == 0 && ch_self == 0) {
			rv = test_bit(DISCARD_CONCURRENT, &mdev->flags)
				? -1 : 1;
			break;
		} else {
			if (ch_peer == 0) { rv =  1; break; }
			if (ch_self == 0) { rv = -1; break; }
		}
		if (mdev->net_conf->after_sb_0p == ASB_DISCARD_ZERO_CHG)
			break;
	case ASB_DISCARD_LEAST_CHG:
		if	(ch_self < ch_peer)
			rv = -1;
		else if (ch_self > ch_peer)
			rv =  1;
		else /* ( ch_self == ch_peer ) */
		     /* Well, then use something else. */
			rv = test_bit(DISCARD_CONCURRENT, &mdev->flags)
				? -1 : 1;
		break;
	case ASB_DISCARD_LOCAL:
		rv = -1;
		break;
	case ASB_DISCARD_REMOTE:
		rv =  1;
	}

	return rv;
}

static int drbd_asb_recover_1p(struct drbd_conf *mdev) __must_hold(local)
{
	int hg, rv = -100;

	switch (mdev->net_conf->after_sb_1p) {
	case ASB_DISCARD_YOUNGER_PRI:
	case ASB_DISCARD_OLDER_PRI:
	case ASB_DISCARD_LEAST_CHG:
	case ASB_DISCARD_LOCAL:
	case ASB_DISCARD_REMOTE:
		dev_err(DEV, "Configuration error.\n");
		break;
	case ASB_DISCONNECT:
		break;
	case ASB_CONSENSUS:
		hg = drbd_asb_recover_0p(mdev);
		if (hg == -1 && mdev->state.role == R_SECONDARY)
			rv = hg;
		if (hg == 1  && mdev->state.role == R_PRIMARY)
			rv = hg;
		break;
	case ASB_VIOLENTLY:
		rv = drbd_asb_recover_0p(mdev);
		break;
	case ASB_DISCARD_SECONDARY:
		return mdev->state.role == R_PRIMARY ? 1 : -1;
	case ASB_CALL_HELPER:
		hg = drbd_asb_recover_0p(mdev);
		if (hg == -1 && mdev->state.role == R_PRIMARY) {
			enum drbd_state_rv rv2;

			drbd_set_role(mdev, R_SECONDARY, 0);
			 /* drbd_change_state() does not sleep while in SS_IN_TRANSIENT_STATE,
			  * we might be here in C_WF_REPORT_PARAMS which is transient.
			  * we do not need to wait for the after state change work either. */
			rv2 = drbd_change_state(mdev, CS_VERBOSE, NS(role, R_SECONDARY));
			if (rv2 != SS_SUCCESS) {
				drbd_khelper(mdev, "pri-lost-after-sb");
			} else {
				dev_warn(DEV, "Successfully gave up primary role.\n");
				rv = hg;
			}
		} else
			rv = hg;
	}

	return rv;
}

static int drbd_asb_recover_2p(struct drbd_conf *mdev) __must_hold(local)
{
	int hg, rv = -100;

	switch (mdev->net_conf->after_sb_2p) {
	case ASB_DISCARD_YOUNGER_PRI:
	case ASB_DISCARD_OLDER_PRI:
	case ASB_DISCARD_LEAST_CHG:
	case ASB_DISCARD_LOCAL:
	case ASB_DISCARD_REMOTE:
	case ASB_CONSENSUS:
	case ASB_DISCARD_SECONDARY:
		dev_err(DEV, "Configuration error.\n");
		break;
	case ASB_VIOLENTLY:
		rv = drbd_asb_recover_0p(mdev);
		break;
	case ASB_DISCONNECT:
		break;
	case ASB_CALL_HELPER:
		hg = drbd_asb_recover_0p(mdev);
		if (hg == -1) {
			enum drbd_state_rv rv2;

			 /* drbd_change_state() does not sleep while in SS_IN_TRANSIENT_STATE,
			  * we might be here in C_WF_REPORT_PARAMS which is transient.
			  * we do not need to wait for the after state change work either. */
			rv2 = drbd_change_state(mdev, CS_VERBOSE, NS(role, R_SECONDARY));
			if (rv2 != SS_SUCCESS) {
				drbd_khelper(mdev, "pri-lost-after-sb");
			} else {
				dev_warn(DEV, "Successfully gave up primary role.\n");
				rv = hg;
			}
		} else
			rv = hg;
	}

	return rv;
}

static void drbd_uuid_dump(struct drbd_conf *mdev, char *text, u64 *uuid,
			   u64 bits, u64 flags)
{
	if (!uuid) {
		dev_info(DEV, "%s uuid info vanished while I was looking!\n", text);
		return;
	}
	dev_info(DEV, "%s %016llX:%016llX:%016llX:%016llX bits:%llu flags:%llX\n",
	     text,
	     (unsigned long long)uuid[UI_CURRENT],
	     (unsigned long long)uuid[UI_BITMAP],
	     (unsigned long long)uuid[UI_HISTORY_START],
	     (unsigned long long)uuid[UI_HISTORY_END],
	     (unsigned long long)bits,
	     (unsigned long long)flags);
}

/*
  100	after split brain try auto recover
    2	C_SYNC_SOURCE set BitMap
    1	C_SYNC_SOURCE use BitMap
    0	no Sync
   -1	C_SYNC_TARGET use BitMap
   -2	C_SYNC_TARGET set BitMap
 -100	after split brain, disconnect
-1000	unrelated data
-1091   requires proto 91
-1096   requires proto 96
 */
static int drbd_uuid_compare(struct drbd_conf *mdev, int *rule_nr) __must_hold(local)
{
	u64 self, peer;
	int i, j;

	self = mdev->ldev->md.uuid[UI_CURRENT] & ~((u64)1);
	peer = mdev->p_uuid[UI_CURRENT] & ~((u64)1);

	*rule_nr = 10;
	if (self == UUID_JUST_CREATED && peer == UUID_JUST_CREATED)
		return 0;

	*rule_nr = 20;
	if ((self == UUID_JUST_CREATED || self == (u64)0) &&
	     peer != UUID_JUST_CREATED)
		return -2;

	*rule_nr = 30;
	if (self != UUID_JUST_CREATED &&
	    (peer == UUID_JUST_CREATED || peer == (u64)0))
		return 2;

	if (self == peer) {
		int rct, dc; /* roles at crash time */

		if (mdev->p_uuid[UI_BITMAP] == (u64)0 && mdev->ldev->md.uuid[UI_BITMAP] != (u64)0) {

			if (mdev->agreed_pro_version < 91)
				return -1091;

			if ((mdev->ldev->md.uuid[UI_BITMAP] & ~((u64)1)) == (mdev->p_uuid[UI_HISTORY_START] & ~((u64)1)) &&
			    (mdev->ldev->md.uuid[UI_HISTORY_START] & ~((u64)1)) == (mdev->p_uuid[UI_HISTORY_START + 1] & ~((u64)1))) {
				dev_info(DEV, "was SyncSource, missed the resync finished event, corrected myself:\n");
				drbd_uuid_set_bm(mdev, 0UL);

				drbd_uuid_dump(mdev, "self", mdev->ldev->md.uuid,
					       mdev->state.disk >= D_NEGOTIATING ? drbd_bm_total_weight(mdev) : 0, 0);
				*rule_nr = 34;
			} else {
				dev_info(DEV, "was SyncSource (peer failed to write sync_uuid)\n");
				*rule_nr = 36;
			}

			return 1;
		}

		if (mdev->ldev->md.uuid[UI_BITMAP] == (u64)0 && mdev->p_uuid[UI_BITMAP] != (u64)0) {

			if (mdev->agreed_pro_version < 91)
				return -1091;

			if ((mdev->ldev->md.uuid[UI_HISTORY_START] & ~((u64)1)) == (mdev->p_uuid[UI_BITMAP] & ~((u64)1)) &&
			    (mdev->ldev->md.uuid[UI_HISTORY_START + 1] & ~((u64)1)) == (mdev->p_uuid[UI_HISTORY_START] & ~((u64)1))) {
				dev_info(DEV, "was SyncTarget, peer missed the resync finished event, corrected peer:\n");

				mdev->p_uuid[UI_HISTORY_START + 1] = mdev->p_uuid[UI_HISTORY_START];
				mdev->p_uuid[UI_HISTORY_START] = mdev->p_uuid[UI_BITMAP];
				mdev->p_uuid[UI_BITMAP] = 0UL;

				drbd_uuid_dump(mdev, "peer", mdev->p_uuid, mdev->p_uuid[UI_SIZE], mdev->p_uuid[UI_FLAGS]);
				*rule_nr = 35;
			} else {
				dev_info(DEV, "was SyncTarget (failed to write sync_uuid)\n");
				*rule_nr = 37;
			}

			return -1;
		}

		/* Common power [off|failure] */
		rct = (test_bit(CRASHED_PRIMARY, &mdev->flags) ? 1 : 0) +
			(mdev->p_uuid[UI_FLAGS] & 2);
		/* lowest bit is set when we were primary,
		 * next bit (weight 2) is set when peer was primary */
		*rule_nr = 40;

		switch (rct) {
		case 0: /* !self_pri && !peer_pri */ return 0;
		case 1: /*  self_pri && !peer_pri */ return 1;
		case 2: /* !self_pri &&  peer_pri */ return -1;
		case 3: /*  self_pri &&  peer_pri */
			dc = test_bit(DISCARD_CONCURRENT, &mdev->flags);
			return dc ? -1 : 1;
		}
	}

	*rule_nr = 50;
	peer = mdev->p_uuid[UI_BITMAP] & ~((u64)1);
	if (self == peer)
		return -1;

	*rule_nr = 51;
	peer = mdev->p_uuid[UI_HISTORY_START] & ~((u64)1);
	if (self == peer) {
		if (mdev->agreed_pro_version < 96 ?
		    (mdev->ldev->md.uuid[UI_HISTORY_START] & ~((u64)1)) ==
		    (mdev->p_uuid[UI_HISTORY_START + 1] & ~((u64)1)) :
		    peer + UUID_NEW_BM_OFFSET == (mdev->p_uuid[UI_BITMAP] & ~((u64)1))) {
			/* The last P_SYNC_UUID did not get though. Undo the last start of
			   resync as sync source modifications of the peer's UUIDs. */

			if (mdev->agreed_pro_version < 91)
				return -1091;

			mdev->p_uuid[UI_BITMAP] = mdev->p_uuid[UI_HISTORY_START];
			mdev->p_uuid[UI_HISTORY_START] = mdev->p_uuid[UI_HISTORY_START + 1];

			dev_info(DEV, "Did not got last syncUUID packet, corrected:\n");
			drbd_uuid_dump(mdev, "peer", mdev->p_uuid, mdev->p_uuid[UI_SIZE], mdev->p_uuid[UI_FLAGS]);

			return -1;
		}
	}

	*rule_nr = 60;
	self = mdev->ldev->md.uuid[UI_CURRENT] & ~((u64)1);
	for (i = UI_HISTORY_START; i <= UI_HISTORY_END; i++) {
		peer = mdev->p_uuid[i] & ~((u64)1);
		if (self == peer)
			return -2;
	}

	*rule_nr = 70;
	self = mdev->ldev->md.uuid[UI_BITMAP] & ~((u64)1);
	peer = mdev->p_uuid[UI_CURRENT] & ~((u64)1);
	if (self == peer)
		return 1;

	*rule_nr = 71;
	self = mdev->ldev->md.uuid[UI_HISTORY_START] & ~((u64)1);
	if (self == peer) {
		if (mdev->agreed_pro_version < 96 ?
		    (mdev->ldev->md.uuid[UI_HISTORY_START + 1] & ~((u64)1)) ==
		    (mdev->p_uuid[UI_HISTORY_START] & ~((u64)1)) :
		    self + UUID_NEW_BM_OFFSET == (mdev->ldev->md.uuid[UI_BITMAP] & ~((u64)1))) {
			/* The last P_SYNC_UUID did not get though. Undo the last start of
			   resync as sync source modifications of our UUIDs. */

			if (mdev->agreed_pro_version < 91)
				return -1091;

			_drbd_uuid_set(mdev, UI_BITMAP, mdev->ldev->md.uuid[UI_HISTORY_START]);
			_drbd_uuid_set(mdev, UI_HISTORY_START, mdev->ldev->md.uuid[UI_HISTORY_START + 1]);

			dev_info(DEV, "Last syncUUID did not get through, corrected:\n");
			drbd_uuid_dump(mdev, "self", mdev->ldev->md.uuid,
				       mdev->state.disk >= D_NEGOTIATING ? drbd_bm_total_weight(mdev) : 0, 0);

			return 1;
		}
	}


	*rule_nr = 80;
	peer = mdev->p_uuid[UI_CURRENT] & ~((u64)1);
	for (i = UI_HISTORY_START; i <= UI_HISTORY_END; i++) {
		self = mdev->ldev->md.uuid[i] & ~((u64)1);
		if (self == peer)
			return 2;
	}

	*rule_nr = 90;
	self = mdev->ldev->md.uuid[UI_BITMAP] & ~((u64)1);
	peer = mdev->p_uuid[UI_BITMAP] & ~((u64)1);
	if (self == peer && self != ((u64)0))
		return 100;

	*rule_nr = 100;
	for (i = UI_HISTORY_START; i <= UI_HISTORY_END; i++) {
		self = mdev->ldev->md.uuid[i] & ~((u64)1);
		for (j = UI_HISTORY_START; j <= UI_HISTORY_END; j++) {
			peer = mdev->p_uuid[j] & ~((u64)1);
			if (self == peer)
				return -100;
		}
	}

	return -1000;
}

/* drbd_sync_handshake() returns the new conn state on success, or
   CONN_MASK (-1) on failure.
 */
static enum drbd_conns drbd_sync_handshake(struct drbd_conf *mdev, enum drbd_role peer_role,
					   enum drbd_disk_state peer_disk) __must_hold(local)
{
	int hg, rule_nr;
	enum drbd_conns rv = C_MASK;
	enum drbd_disk_state mydisk;

	mydisk = mdev->state.disk;
	if (mydisk == D_NEGOTIATING)
		mydisk = mdev->new_state_tmp.disk;

	dev_info(DEV, "drbd_sync_handshake:\n");
	drbd_uuid_dump(mdev, "self", mdev->ldev->md.uuid, mdev->comm_bm_set, 0);
	drbd_uuid_dump(mdev, "peer", mdev->p_uuid,
		       mdev->p_uuid[UI_SIZE], mdev->p_uuid[UI_FLAGS]);

	hg = drbd_uuid_compare(mdev, &rule_nr);

	dev_info(DEV, "uuid_compare()=%d by rule %d\n", hg, rule_nr);

	if (hg == -1000) {
		dev_alert(DEV, "Unrelated data, aborting!\n");
		return C_MASK;
	}
	if (hg < -1000) {
		dev_alert(DEV, "To resolve this both sides have to support at least protocol %d\n", -hg - 1000);
		return C_MASK;
	}

	if    ((mydisk == D_INCONSISTENT && peer_disk > D_INCONSISTENT) ||
	    (peer_disk == D_INCONSISTENT && mydisk    > D_INCONSISTENT)) {
		int f = (hg == -100) || abs(hg) == 2;
		hg = mydisk > D_INCONSISTENT ? 1 : -1;
		if (f)
			hg = hg*2;
		dev_info(DEV, "Becoming sync %s due to disk states.\n",
		     hg > 0 ? "source" : "target");
	}

	if (abs(hg) == 100)
		drbd_khelper(mdev, "initial-split-brain");

	if (hg == 100 || (hg == -100 && mdev->net_conf->always_asbp)) {
		int pcount = (mdev->state.role == R_PRIMARY)
			   + (peer_role == R_PRIMARY);
		int forced = (hg == -100);

		switch (pcount) {
		case 0:
			hg = drbd_asb_recover_0p(mdev);
			break;
		case 1:
			hg = drbd_asb_recover_1p(mdev);
			break;
		case 2:
			hg = drbd_asb_recover_2p(mdev);
			break;
		}
		if (abs(hg) < 100) {
			dev_warn(DEV, "Split-Brain detected, %d primaries, "
			     "automatically solved. Sync from %s node\n",
			     pcount, (hg < 0) ? "peer" : "this");
			if (forced) {
				dev_warn(DEV, "Doing a full sync, since"
				     " UUIDs where ambiguous.\n");
				hg = hg*2;
			}
		}
	}

	if (hg == -100) {
		if (mdev->net_conf->want_lose && !(mdev->p_uuid[UI_FLAGS]&1))
			hg = -1;
		if (!mdev->net_conf->want_lose && (mdev->p_uuid[UI_FLAGS]&1))
			hg = 1;

		if (abs(hg) < 100)
			dev_warn(DEV, "Split-Brain detected, manually solved. "
			     "Sync from %s node\n",
			     (hg < 0) ? "peer" : "this");
	}

	if (hg == -100) {
		/* FIXME this log message is not correct if we end up here
		 * after an attempted attach on a diskless node.
		 * We just refuse to attach -- well, we drop the "connection"
		 * to that disk, in a way... */
		dev_alert(DEV, "Split-Brain detected but unresolved, dropping connection!\n");
		drbd_khelper(mdev, "split-brain");
		return C_MASK;
	}

	if (hg > 0 && mydisk <= D_INCONSISTENT) {
		dev_err(DEV, "I shall become SyncSource, but I am inconsistent!\n");
		return C_MASK;
	}

	if (hg < 0 && /* by intention we do not use mydisk here. */
	    mdev->state.role == R_PRIMARY && mdev->state.disk >= D_CONSISTENT) {
		switch (mdev->net_conf->rr_conflict) {
		case ASB_CALL_HELPER:
			drbd_khelper(mdev, "pri-lost");
			/* fall through */
		case ASB_DISCONNECT:
			dev_err(DEV, "I shall become SyncTarget, but I am primary!\n");
			return C_MASK;
		case ASB_VIOLENTLY:
			dev_warn(DEV, "Becoming SyncTarget, violating the stable-data"
			     "assumption\n");
		}
	}

	if (mdev->net_conf->dry_run || test_bit(CONN_DRY_RUN, &mdev->flags)) {
		if (hg == 0)
			dev_info(DEV, "dry-run connect: No resync, would become Connected immediately.\n");
		else
			dev_info(DEV, "dry-run connect: Would become %s, doing a %s resync.",
				 drbd_conn_str(hg > 0 ? C_SYNC_SOURCE : C_SYNC_TARGET),
				 abs(hg) >= 2 ? "full" : "bit-map based");
		return C_MASK;
	}

	if (abs(hg) >= 2) {
		dev_info(DEV, "Writing the whole bitmap, full sync required after drbd_sync_handshake.\n");
		if (drbd_bitmap_io(mdev, &drbd_bmio_set_n_write, "set_n_write from sync_handshake",
					BM_LOCKED_SET_ALLOWED))
			return C_MASK;
	}

	if (hg > 0) { /* become sync source. */
		rv = C_WF_BITMAP_S;
	} else if (hg < 0) { /* become sync target */
		rv = C_WF_BITMAP_T;
	} else {
		rv = C_CONNECTED;
		if (drbd_bm_total_weight(mdev)) {
			dev_info(DEV, "No resync, but %lu bits in bitmap!\n",
			     drbd_bm_total_weight(mdev));
		}
	}

	return rv;
}

/* returns 1 if invalid */
static int cmp_after_sb(enum drbd_after_sb_p peer, enum drbd_after_sb_p self)
{
	/* ASB_DISCARD_REMOTE - ASB_DISCARD_LOCAL is valid */
	if ((peer == ASB_DISCARD_REMOTE && self == ASB_DISCARD_LOCAL) ||
	    (self == ASB_DISCARD_REMOTE && peer == ASB_DISCARD_LOCAL))
		return 0;

	/* any other things with ASB_DISCARD_REMOTE or ASB_DISCARD_LOCAL are invalid */
	if (peer == ASB_DISCARD_REMOTE || peer == ASB_DISCARD_LOCAL ||
	    self == ASB_DISCARD_REMOTE || self == ASB_DISCARD_LOCAL)
		return 1;

	/* everything else is valid if they are equal on both sides. */
	if (peer == self)
		return 0;

	/* everything es is invalid. */
	return 1;
}

static int receive_protocol(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	struct p_protocol *p = &mdev->data.rbuf.protocol;
	int p_proto, p_after_sb_0p, p_after_sb_1p, p_after_sb_2p;
	int p_want_lose, p_two_primaries, cf;
	char p_integrity_alg[SHARED_SECRET_MAX] = "";

	p_proto		= be32_to_cpu(p->protocol);
	p_after_sb_0p	= be32_to_cpu(p->after_sb_0p);
	p_after_sb_1p	= be32_to_cpu(p->after_sb_1p);
	p_after_sb_2p	= be32_to_cpu(p->after_sb_2p);
	p_two_primaries = be32_to_cpu(p->two_primaries);
	cf		= be32_to_cpu(p->conn_flags);
	p_want_lose = cf & CF_WANT_LOSE;

	clear_bit(CONN_DRY_RUN, &mdev->flags);

	if (cf & CF_DRY_RUN)
		set_bit(CONN_DRY_RUN, &mdev->flags);

	if (p_proto != mdev->net_conf->wire_protocol) {
		dev_err(DEV, "incompatible communication protocols\n");
		goto disconnect;
	}

	if (cmp_after_sb(p_after_sb_0p, mdev->net_conf->after_sb_0p)) {
		dev_err(DEV, "incompatible after-sb-0pri settings\n");
		goto disconnect;
	}

	if (cmp_after_sb(p_after_sb_1p, mdev->net_conf->after_sb_1p)) {
		dev_err(DEV, "incompatible after-sb-1pri settings\n");
		goto disconnect;
	}

	if (cmp_after_sb(p_after_sb_2p, mdev->net_conf->after_sb_2p)) {
		dev_err(DEV, "incompatible after-sb-2pri settings\n");
		goto disconnect;
	}

	if (p_want_lose && mdev->net_conf->want_lose) {
		dev_err(DEV, "both sides have the 'want_lose' flag set\n");
		goto disconnect;
	}

	if (p_two_primaries != mdev->net_conf->two_primaries) {
		dev_err(DEV, "incompatible setting of the two-primaries options\n");
		goto disconnect;
	}

	if (mdev->agreed_pro_version >= 87) {
		unsigned char *my_alg = mdev->net_conf->integrity_alg;

		if (drbd_recv(mdev, p_integrity_alg, data_size) != data_size)
			return false;

		p_integrity_alg[SHARED_SECRET_MAX-1] = 0;
		if (strcmp(p_integrity_alg, my_alg)) {
			dev_err(DEV, "incompatible setting of the data-integrity-alg\n");
			goto disconnect;
		}
		dev_info(DEV, "data-integrity-alg: %s\n",
		     my_alg[0] ? my_alg : (unsigned char *)"<not-used>");
	}

	return true;

disconnect:
	drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
	return false;
}

/* helper function
 * input: alg name, feature name
 * return: NULL (alg name was "")
 *         ERR_PTR(error) if something goes wrong
 *         or the crypto hash ptr, if it worked out ok. */
struct crypto_hash *drbd_crypto_alloc_digest_safe(const struct drbd_conf *mdev,
		const char *alg, const char *name)
{
	struct crypto_hash *tfm;

	if (!alg[0])
		return NULL;

	tfm = crypto_alloc_hash(alg, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		dev_err(DEV, "Can not allocate \"%s\" as %s (reason: %ld)\n",
			alg, name, PTR_ERR(tfm));
		return tfm;
	}
	if (!drbd_crypto_is_hash(crypto_hash_tfm(tfm))) {
		crypto_free_hash(tfm);
		dev_err(DEV, "\"%s\" is not a digest (%s)\n", alg, name);
		return ERR_PTR(-EINVAL);
	}
	return tfm;
}

static int receive_SyncParam(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int packet_size)
{
	int ok = true;
	struct p_rs_param_95 *p = &mdev->data.rbuf.rs_param_95;
	unsigned int header_size, data_size, exp_max_sz;
	struct crypto_hash *verify_tfm = NULL;
	struct crypto_hash *csums_tfm = NULL;
	const int apv = mdev->agreed_pro_version;
	int *rs_plan_s = NULL;
	int fifo_size = 0;

	exp_max_sz  = apv <= 87 ? sizeof(struct p_rs_param)
		    : apv == 88 ? sizeof(struct p_rs_param)
					+ SHARED_SECRET_MAX
		    : apv <= 94 ? sizeof(struct p_rs_param_89)
		    : /* apv >= 95 */ sizeof(struct p_rs_param_95);

	if (packet_size > exp_max_sz) {
		dev_err(DEV, "SyncParam packet too long: received %u, expected <= %u bytes\n",
		    packet_size, exp_max_sz);
		return false;
	}

	if (apv <= 88) {
		header_size = sizeof(struct p_rs_param) - sizeof(struct p_header80);
		data_size   = packet_size  - header_size;
	} else if (apv <= 94) {
		header_size = sizeof(struct p_rs_param_89) - sizeof(struct p_header80);
		data_size   = packet_size  - header_size;
		D_ASSERT(data_size == 0);
	} else {
		header_size = sizeof(struct p_rs_param_95) - sizeof(struct p_header80);
		data_size   = packet_size  - header_size;
		D_ASSERT(data_size == 0);
	}

	/* initialize verify_alg and csums_alg */
	memset(p->verify_alg, 0, 2 * SHARED_SECRET_MAX);

	if (drbd_recv(mdev, &p->head.payload, header_size) != header_size)
		return false;

	mdev->sync_conf.rate	  = be32_to_cpu(p->rate);

	if (apv >= 88) {
		if (apv == 88) {
			if (data_size > SHARED_SECRET_MAX) {
				dev_err(DEV, "verify-alg too long, "
				    "peer wants %u, accepting only %u byte\n",
						data_size, SHARED_SECRET_MAX);
				return false;
			}

			if (drbd_recv(mdev, p->verify_alg, data_size) != data_size)
				return false;

			/* we expect NUL terminated string */
			/* but just in case someone tries to be evil */
			D_ASSERT(p->verify_alg[data_size-1] == 0);
			p->verify_alg[data_size-1] = 0;

		} else /* apv >= 89 */ {
			/* we still expect NUL terminated strings */
			/* but just in case someone tries to be evil */
			D_ASSERT(p->verify_alg[SHARED_SECRET_MAX-1] == 0);
			D_ASSERT(p->csums_alg[SHARED_SECRET_MAX-1] == 0);
			p->verify_alg[SHARED_SECRET_MAX-1] = 0;
			p->csums_alg[SHARED_SECRET_MAX-1] = 0;
		}

		if (strcmp(mdev->sync_conf.verify_alg, p->verify_alg)) {
			if (mdev->state.conn == C_WF_REPORT_PARAMS) {
				dev_err(DEV, "Different verify-alg settings. me=\"%s\" peer=\"%s\"\n",
				    mdev->sync_conf.verify_alg, p->verify_alg);
				goto disconnect;
			}
			verify_tfm = drbd_crypto_alloc_digest_safe(mdev,
					p->verify_alg, "verify-alg");
			if (IS_ERR(verify_tfm)) {
				verify_tfm = NULL;
				goto disconnect;
			}
		}

		if (apv >= 89 && strcmp(mdev->sync_conf.csums_alg, p->csums_alg)) {
			if (mdev->state.conn == C_WF_REPORT_PARAMS) {
				dev_err(DEV, "Different csums-alg settings. me=\"%s\" peer=\"%s\"\n",
				    mdev->sync_conf.csums_alg, p->csums_alg);
				goto disconnect;
			}
			csums_tfm = drbd_crypto_alloc_digest_safe(mdev,
					p->csums_alg, "csums-alg");
			if (IS_ERR(csums_tfm)) {
				csums_tfm = NULL;
				goto disconnect;
			}
		}

		if (apv > 94) {
			mdev->sync_conf.rate	  = be32_to_cpu(p->rate);
			mdev->sync_conf.c_plan_ahead = be32_to_cpu(p->c_plan_ahead);
			mdev->sync_conf.c_delay_target = be32_to_cpu(p->c_delay_target);
			mdev->sync_conf.c_fill_target = be32_to_cpu(p->c_fill_target);
			mdev->sync_conf.c_max_rate = be32_to_cpu(p->c_max_rate);

			fifo_size = (mdev->sync_conf.c_plan_ahead * 10 * SLEEP_TIME) / HZ;
			if (fifo_size != mdev->rs_plan_s.size && fifo_size > 0) {
				rs_plan_s   = kzalloc(sizeof(int) * fifo_size, GFP_KERNEL);
				if (!rs_plan_s) {
					dev_err(DEV, "kmalloc of fifo_buffer failed");
					goto disconnect;
				}
			}
		}

		spin_lock(&mdev->peer_seq_lock);
		/* lock against drbd_nl_syncer_conf() */
		if (verify_tfm) {
			strcpy(mdev->sync_conf.verify_alg, p->verify_alg);
			mdev->sync_conf.verify_alg_len = strlen(p->verify_alg) + 1;
			crypto_free_hash(mdev->verify_tfm);
			mdev->verify_tfm = verify_tfm;
			dev_info(DEV, "using verify-alg: \"%s\"\n", p->verify_alg);
		}
		if (csums_tfm) {
			strcpy(mdev->sync_conf.csums_alg, p->csums_alg);
			mdev->sync_conf.csums_alg_len = strlen(p->csums_alg) + 1;
			crypto_free_hash(mdev->csums_tfm);
			mdev->csums_tfm = csums_tfm;
			dev_info(DEV, "using csums-alg: \"%s\"\n", p->csums_alg);
		}
		if (fifo_size != mdev->rs_plan_s.size) {
			kfree(mdev->rs_plan_s.values);
			mdev->rs_plan_s.values = rs_plan_s;
			mdev->rs_plan_s.size   = fifo_size;
			mdev->rs_planed = 0;
		}
		spin_unlock(&mdev->peer_seq_lock);
	}

	return ok;
disconnect:
	/* just for completeness: actually not needed,
	 * as this is not reached if csums_tfm was ok. */
	crypto_free_hash(csums_tfm);
	/* but free the verify_tfm again, if csums_tfm did not work out */
	crypto_free_hash(verify_tfm);
	drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
	return false;
}

/* warn if the arguments differ by more than 12.5% */
static void warn_if_differ_considerably(struct drbd_conf *mdev,
	const char *s, sector_t a, sector_t b)
{
	sector_t d;
	if (a == 0 || b == 0)
		return;
	d = (a > b) ? (a - b) : (b - a);
	if (d > (a>>3) || d > (b>>3))
		dev_warn(DEV, "Considerable difference in %s: %llus vs. %llus\n", s,
		     (unsigned long long)a, (unsigned long long)b);
}

static int receive_sizes(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	struct p_sizes *p = &mdev->data.rbuf.sizes;
	enum determine_dev_size dd = unchanged;
	sector_t p_size, p_usize, my_usize;
	int ldsc = 0; /* local disk size changed */
	enum dds_flags ddsf;

	p_size = be64_to_cpu(p->d_size);
	p_usize = be64_to_cpu(p->u_size);

	if (p_size == 0 && mdev->state.disk == D_DISKLESS) {
		dev_err(DEV, "some backing storage is needed\n");
		drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
		return false;
	}

	/* just store the peer's disk size for now.
	 * we still need to figure out whether we accept that. */
	mdev->p_size = p_size;

	if (get_ldev(mdev)) {
		warn_if_differ_considerably(mdev, "lower level device sizes",
			   p_size, drbd_get_max_capacity(mdev->ldev));
		warn_if_differ_considerably(mdev, "user requested size",
					    p_usize, mdev->ldev->dc.disk_size);

		/* if this is the first connect, or an otherwise expected
		 * param exchange, choose the minimum */
		if (mdev->state.conn == C_WF_REPORT_PARAMS)
			p_usize = min_not_zero((sector_t)mdev->ldev->dc.disk_size,
					     p_usize);

		my_usize = mdev->ldev->dc.disk_size;

		if (mdev->ldev->dc.disk_size != p_usize) {
			mdev->ldev->dc.disk_size = p_usize;
			dev_info(DEV, "Peer sets u_size to %lu sectors\n",
			     (unsigned long)mdev->ldev->dc.disk_size);
		}

		/* Never shrink a device with usable data during connect.
		   But allow online shrinking if we are connected. */
		if (drbd_new_dev_size(mdev, mdev->ldev, 0) <
		   drbd_get_capacity(mdev->this_bdev) &&
		   mdev->state.disk >= D_OUTDATED &&
		   mdev->state.conn < C_CONNECTED) {
			dev_err(DEV, "The peer's disk size is too small!\n");
			drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
			mdev->ldev->dc.disk_size = my_usize;
			put_ldev(mdev);
			return false;
		}
		put_ldev(mdev);
	}

	ddsf = be16_to_cpu(p->dds_flags);
	if (get_ldev(mdev)) {
		dd = drbd_determine_dev_size(mdev, ddsf);
		put_ldev(mdev);
		if (dd == dev_size_error)
			return false;
		drbd_md_sync(mdev);
	} else {
		/* I am diskless, need to accept the peer's size. */
		drbd_set_my_capacity(mdev, p_size);
	}

	mdev->peer_max_bio_size = be32_to_cpu(p->max_bio_size);
	drbd_reconsider_max_bio_size(mdev);

	if (get_ldev(mdev)) {
		if (mdev->ldev->known_size != drbd_get_capacity(mdev->ldev->backing_bdev)) {
			mdev->ldev->known_size = drbd_get_capacity(mdev->ldev->backing_bdev);
			ldsc = 1;
		}

		put_ldev(mdev);
	}

	if (mdev->state.conn > C_WF_REPORT_PARAMS) {
		if (be64_to_cpu(p->c_size) !=
		    drbd_get_capacity(mdev->this_bdev) || ldsc) {
			/* we have different sizes, probably peer
			 * needs to know my new size... */
			drbd_send_sizes(mdev, 0, ddsf);
		}
		if (test_and_clear_bit(RESIZE_PENDING, &mdev->flags) ||
		    (dd == grew && mdev->state.conn == C_CONNECTED)) {
			if (mdev->state.pdsk >= D_INCONSISTENT &&
			    mdev->state.disk >= D_INCONSISTENT) {
				if (ddsf & DDSF_NO_RESYNC)
					dev_info(DEV, "Resync of new storage suppressed with --assume-clean\n");
				else
					resync_after_online_grow(mdev);
			} else
				set_bit(RESYNC_AFTER_NEG, &mdev->flags);
		}
	}

	return true;
}

static int receive_uuids(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	struct p_uuids *p = &mdev->data.rbuf.uuids;
	u64 *p_uuid;
	int i, updated_uuids = 0;

	p_uuid = kmalloc(sizeof(u64)*UI_EXTENDED_SIZE, GFP_NOIO);

	for (i = UI_CURRENT; i < UI_EXTENDED_SIZE; i++)
		p_uuid[i] = be64_to_cpu(p->uuid[i]);

	kfree(mdev->p_uuid);
	mdev->p_uuid = p_uuid;

	if (mdev->state.conn < C_CONNECTED &&
	    mdev->state.disk < D_INCONSISTENT &&
	    mdev->state.role == R_PRIMARY &&
	    (mdev->ed_uuid & ~((u64)1)) != (p_uuid[UI_CURRENT] & ~((u64)1))) {
		dev_err(DEV, "Can only connect to data with current UUID=%016llX\n",
		    (unsigned long long)mdev->ed_uuid);
		drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
		return false;
	}

	if (get_ldev(mdev)) {
		int skip_initial_sync =
			mdev->state.conn == C_CONNECTED &&
			mdev->agreed_pro_version >= 90 &&
			mdev->ldev->md.uuid[UI_CURRENT] == UUID_JUST_CREATED &&
			(p_uuid[UI_FLAGS] & 8);
		if (skip_initial_sync) {
			dev_info(DEV, "Accepted new current UUID, preparing to skip initial sync\n");
			drbd_bitmap_io(mdev, &drbd_bmio_clear_n_write,
					"clear_n_write from receive_uuids",
					BM_LOCKED_TEST_ALLOWED);
			_drbd_uuid_set(mdev, UI_CURRENT, p_uuid[UI_CURRENT]);
			_drbd_uuid_set(mdev, UI_BITMAP, 0);
			_drbd_set_state(_NS2(mdev, disk, D_UP_TO_DATE, pdsk, D_UP_TO_DATE),
					CS_VERBOSE, NULL);
			drbd_md_sync(mdev);
			updated_uuids = 1;
		}
		put_ldev(mdev);
	} else if (mdev->state.disk < D_INCONSISTENT &&
		   mdev->state.role == R_PRIMARY) {
		/* I am a diskless primary, the peer just created a new current UUID
		   for me. */
		updated_uuids = drbd_set_ed_uuid(mdev, p_uuid[UI_CURRENT]);
	}

	/* Before we test for the disk state, we should wait until an eventually
	   ongoing cluster wide state change is finished. That is important if
	   we are primary and are detaching from our disk. We need to see the
	   new disk state... */
	wait_event(mdev->misc_wait, !test_bit(CLUSTER_ST_CHANGE, &mdev->flags));
	if (mdev->state.conn >= C_CONNECTED && mdev->state.disk < D_INCONSISTENT)
		updated_uuids |= drbd_set_ed_uuid(mdev, p_uuid[UI_CURRENT]);

	if (updated_uuids)
		drbd_print_uuids(mdev, "receiver updated UUIDs to");

	return true;
}

/**
 * convert_state() - Converts the peer's view of the cluster state to our point of view
 * @ps:		The state as seen by the peer.
 */
static union drbd_state convert_state(union drbd_state ps)
{
	union drbd_state ms;

	static enum drbd_conns c_tab[] = {
		[C_CONNECTED] = C_CONNECTED,

		[C_STARTING_SYNC_S] = C_STARTING_SYNC_T,
		[C_STARTING_SYNC_T] = C_STARTING_SYNC_S,
		[C_DISCONNECTING] = C_TEAR_DOWN, /* C_NETWORK_FAILURE, */
		[C_VERIFY_S]       = C_VERIFY_T,
		[C_MASK]   = C_MASK,
	};

	ms.i = ps.i;

	ms.conn = c_tab[ps.conn];
	ms.peer = ps.role;
	ms.role = ps.peer;
	ms.pdsk = ps.disk;
	ms.disk = ps.pdsk;
	ms.peer_isp = (ps.aftr_isp | ps.user_isp);

	return ms;
}

static int receive_req_state(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	struct p_req_state *p = &mdev->data.rbuf.req_state;
	union drbd_state mask, val;
	enum drbd_state_rv rv;

	mask.i = be32_to_cpu(p->mask);
	val.i = be32_to_cpu(p->val);

	if (test_bit(DISCARD_CONCURRENT, &mdev->flags) &&
	    test_bit(CLUSTER_ST_CHANGE, &mdev->flags)) {
		drbd_send_sr_reply(mdev, SS_CONCURRENT_ST_CHG);
		return true;
	}

	mask = convert_state(mask);
	val = convert_state(val);

	rv = drbd_change_state(mdev, CS_VERBOSE, mask, val);

	drbd_send_sr_reply(mdev, rv);
	drbd_md_sync(mdev);

	return true;
}

static int receive_state(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	struct p_state *p = &mdev->data.rbuf.state;
	union drbd_state os, ns, peer_state;
	enum drbd_disk_state real_peer_disk;
	enum chg_state_flags cs_flags;
	int rv;

	peer_state.i = be32_to_cpu(p->state);

	real_peer_disk = peer_state.disk;
	if (peer_state.disk == D_NEGOTIATING) {
		real_peer_disk = mdev->p_uuid[UI_FLAGS] & 4 ? D_INCONSISTENT : D_CONSISTENT;
		dev_info(DEV, "real peer disk state = %s\n", drbd_disk_str(real_peer_disk));
	}

	spin_lock_irq(&mdev->req_lock);
 retry:
	os = ns = mdev->state;
	spin_unlock_irq(&mdev->req_lock);

	/* peer says his disk is uptodate, while we think it is inconsistent,
	 * and this happens while we think we have a sync going on. */
	if (os.pdsk == D_INCONSISTENT && real_peer_disk == D_UP_TO_DATE &&
	    os.conn > C_CONNECTED && os.disk == D_UP_TO_DATE) {
		/* If we are (becoming) SyncSource, but peer is still in sync
		 * preparation, ignore its uptodate-ness to avoid flapping, it
		 * will change to inconsistent once the peer reaches active
		 * syncing states.
		 * It may have changed syncer-paused flags, however, so we
		 * cannot ignore this completely. */
		if (peer_state.conn > C_CONNECTED &&
		    peer_state.conn < C_SYNC_SOURCE)
			real_peer_disk = D_INCONSISTENT;

		/* if peer_state changes to connected at the same time,
		 * it explicitly notifies us that it finished resync.
		 * Maybe we should finish it up, too? */
		else if (os.conn >= C_SYNC_SOURCE &&
			 peer_state.conn == C_CONNECTED) {
			if (drbd_bm_total_weight(mdev) <= mdev->rs_failed)
				drbd_resync_finished(mdev);
			return true;
		}
	}

	/* peer says his disk is inconsistent, while we think it is uptodate,
	 * and this happens while the peer still thinks we have a sync going on,
	 * but we think we are already done with the sync.
	 * We ignore this to avoid flapping pdsk.
	 * This should not happen, if the peer is a recent version of drbd. */
	if (os.pdsk == D_UP_TO_DATE && real_peer_disk == D_INCONSISTENT &&
	    os.conn == C_CONNECTED && peer_state.conn > C_SYNC_SOURCE)
		real_peer_disk = D_UP_TO_DATE;

	if (ns.conn == C_WF_REPORT_PARAMS)
		ns.conn = C_CONNECTED;

	if (peer_state.conn == C_AHEAD)
		ns.conn = C_BEHIND;

	if (mdev->p_uuid && peer_state.disk >= D_NEGOTIATING &&
	    get_ldev_if_state(mdev, D_NEGOTIATING)) {
		int cr; /* consider resync */

		/* if we established a new connection */
		cr  = (os.conn < C_CONNECTED);
		/* if we had an established connection
		 * and one of the nodes newly attaches a disk */
		cr |= (os.conn == C_CONNECTED &&
		       (peer_state.disk == D_NEGOTIATING ||
			os.disk == D_NEGOTIATING));
		/* if we have both been inconsistent, and the peer has been
		 * forced to be UpToDate with --overwrite-data */
		cr |= test_bit(CONSIDER_RESYNC, &mdev->flags);
		/* if we had been plain connected, and the admin requested to
		 * start a sync by "invalidate" or "invalidate-remote" */
		cr |= (os.conn == C_CONNECTED &&
				(peer_state.conn >= C_STARTING_SYNC_S &&
				 peer_state.conn <= C_WF_BITMAP_T));

		if (cr)
			ns.conn = drbd_sync_handshake(mdev, peer_state.role, real_peer_disk);

		put_ldev(mdev);
		if (ns.conn == C_MASK) {
			ns.conn = C_CONNECTED;
			if (mdev->state.disk == D_NEGOTIATING) {
				drbd_force_state(mdev, NS(disk, D_FAILED));
			} else if (peer_state.disk == D_NEGOTIATING) {
				dev_err(DEV, "Disk attach process on the peer node was aborted.\n");
				peer_state.disk = D_DISKLESS;
				real_peer_disk = D_DISKLESS;
			} else {
				if (test_and_clear_bit(CONN_DRY_RUN, &mdev->flags))
					return false;
				D_ASSERT(os.conn == C_WF_REPORT_PARAMS);
				drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
				return false;
			}
		}
	}

	spin_lock_irq(&mdev->req_lock);
	if (mdev->state.i != os.i)
		goto retry;
	clear_bit(CONSIDER_RESYNC, &mdev->flags);
	ns.peer = peer_state.role;
	ns.pdsk = real_peer_disk;
	ns.peer_isp = (peer_state.aftr_isp | peer_state.user_isp);
	if ((ns.conn == C_CONNECTED || ns.conn == C_WF_BITMAP_S) && ns.disk == D_NEGOTIATING)
		ns.disk = mdev->new_state_tmp.disk;
	cs_flags = CS_VERBOSE + (os.conn < C_CONNECTED && ns.conn >= C_CONNECTED ? 0 : CS_HARD);
	if (ns.pdsk == D_CONSISTENT && is_susp(ns) && ns.conn == C_CONNECTED && os.conn < C_CONNECTED &&
	    test_bit(NEW_CUR_UUID, &mdev->flags)) {
		/* Do not allow tl_restart(resend) for a rebooted peer. We can only allow this
		   for temporal network outages! */
		spin_unlock_irq(&mdev->req_lock);
		dev_err(DEV, "Aborting Connect, can not thaw IO with an only Consistent peer\n");
		tl_clear(mdev);
		drbd_uuid_new_current(mdev);
		clear_bit(NEW_CUR_UUID, &mdev->flags);
		drbd_force_state(mdev, NS2(conn, C_PROTOCOL_ERROR, susp, 0));
		return false;
	}
	rv = _drbd_set_state(mdev, ns, cs_flags, NULL);
	ns = mdev->state;
	spin_unlock_irq(&mdev->req_lock);

	if (rv < SS_SUCCESS) {
		drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
		return false;
	}

	if (os.conn > C_WF_REPORT_PARAMS) {
		if (ns.conn > C_CONNECTED && peer_state.conn <= C_CONNECTED &&
		    peer_state.disk != D_NEGOTIATING ) {
			/* we want resync, peer has not yet decided to sync... */
			/* Nowadays only used when forcing a node into primary role and
			   setting its disk to UpToDate with that */
			drbd_send_uuids(mdev);
			drbd_send_state(mdev);
		}
	}

	mdev->net_conf->want_lose = 0;

	drbd_md_sync(mdev); /* update connected indicator, la_size, ... */

	return true;
}

static int receive_sync_uuid(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	struct p_rs_uuid *p = &mdev->data.rbuf.rs_uuid;

	wait_event(mdev->misc_wait,
		   mdev->state.conn == C_WF_SYNC_UUID ||
		   mdev->state.conn == C_BEHIND ||
		   mdev->state.conn < C_CONNECTED ||
		   mdev->state.disk < D_NEGOTIATING);

	/* D_ASSERT( mdev->state.conn == C_WF_SYNC_UUID ); */

	/* Here the _drbd_uuid_ functions are right, current should
	   _not_ be rotated into the history */
	if (get_ldev_if_state(mdev, D_NEGOTIATING)) {
		_drbd_uuid_set(mdev, UI_CURRENT, be64_to_cpu(p->uuid));
		_drbd_uuid_set(mdev, UI_BITMAP, 0UL);

		drbd_print_uuids(mdev, "updated sync uuid");
		drbd_start_resync(mdev, C_SYNC_TARGET);

		put_ldev(mdev);
	} else
		dev_err(DEV, "Ignoring SyncUUID packet!\n");

	return true;
}

/**
 * receive_bitmap_plain
 *
 * Return 0 when done, 1 when another iteration is needed, and a negative error
 * code upon failure.
 */
static int
receive_bitmap_plain(struct drbd_conf *mdev, unsigned int data_size,
		     unsigned long *buffer, struct bm_xfer_ctx *c)
{
	unsigned num_words = min_t(size_t, BM_PACKET_WORDS, c->bm_words - c->word_offset);
	unsigned want = num_words * sizeof(long);
	int err;

	if (want != data_size) {
		dev_err(DEV, "%s:want (%u) != data_size (%u)\n", __func__, want, data_size);
		return -EIO;
	}
	if (want == 0)
		return 0;
	err = drbd_recv(mdev, buffer, want);
	if (err != want) {
		if (err >= 0)
			err = -EIO;
		return err;
	}

	drbd_bm_merge_lel(mdev, c->word_offset, num_words, buffer);

	c->word_offset += num_words;
	c->bit_offset = c->word_offset * BITS_PER_LONG;
	if (c->bit_offset > c->bm_bits)
		c->bit_offset = c->bm_bits;

	return 1;
}

/**
 * recv_bm_rle_bits
 *
 * Return 0 when done, 1 when another iteration is needed, and a negative error
 * code upon failure.
 */
static int
recv_bm_rle_bits(struct drbd_conf *mdev,
		struct p_compressed_bm *p,
		struct bm_xfer_ctx *c)
{
	struct bitstream bs;
	u64 look_ahead;
	u64 rl;
	u64 tmp;
	unsigned long s = c->bit_offset;
	unsigned long e;
	int len = be16_to_cpu(p->head.length) - (sizeof(*p) - sizeof(p->head));
	int toggle = DCBP_get_start(p);
	int have;
	int bits;

	bitstream_init(&bs, p->code, len, DCBP_get_pad_bits(p));

	bits = bitstream_get_bits(&bs, &look_ahead, 64);
	if (bits < 0)
		return -EIO;

	for (have = bits; have > 0; s += rl, toggle = !toggle) {
		bits = vli_decode_bits(&rl, look_ahead);
		if (bits <= 0)
			return -EIO;

		if (toggle) {
			e = s + rl -1;
			if (e >= c->bm_bits) {
				dev_err(DEV, "bitmap overflow (e:%lu) while decoding bm RLE packet\n", e);
				return -EIO;
			}
			_drbd_bm_set_bits(mdev, s, e);
		}

		if (have < bits) {
			dev_err(DEV, "bitmap decoding error: h:%d b:%d la:0x%08llx l:%u/%u\n",
				have, bits, look_ahead,
				(unsigned int)(bs.cur.b - p->code),
				(unsigned int)bs.buf_len);
			return -EIO;
		}
		look_ahead >>= bits;
		have -= bits;

		bits = bitstream_get_bits(&bs, &tmp, 64 - have);
		if (bits < 0)
			return -EIO;
		look_ahead |= tmp << have;
		have += bits;
	}

	c->bit_offset = s;
	bm_xfer_ctx_bit_to_word_offset(c);

	return (s != c->bm_bits);
}

/**
 * decode_bitmap_c
 *
 * Return 0 when done, 1 when another iteration is needed, and a negative error
 * code upon failure.
 */
static int
decode_bitmap_c(struct drbd_conf *mdev,
		struct p_compressed_bm *p,
		struct bm_xfer_ctx *c)
{
	if (DCBP_get_code(p) == RLE_VLI_Bits)
		return recv_bm_rle_bits(mdev, p, c);

	/* other variants had been implemented for evaluation,
	 * but have been dropped as this one turned out to be "best"
	 * during all our tests. */

	dev_err(DEV, "receive_bitmap_c: unknown encoding %u\n", p->encoding);
	drbd_force_state(mdev, NS(conn, C_PROTOCOL_ERROR));
	return -EIO;
}

void INFO_bm_xfer_stats(struct drbd_conf *mdev,
		const char *direction, struct bm_xfer_ctx *c)
{
	/* what would it take to transfer it "plaintext" */
	unsigned plain = sizeof(struct p_header80) *
		((c->bm_words+BM_PACKET_WORDS-1)/BM_PACKET_WORDS+1)
		+ c->bm_words * sizeof(long);
	unsigned total = c->bytes[0] + c->bytes[1];
	unsigned r;

	/* total can not be zero. but just in case: */
	if (total == 0)
		return;

	/* don't report if not compressed */
	if (total >= plain)
		return;

	/* total < plain. check for overflow, still */
	r = (total > UINT_MAX/1000) ? (total / (plain/1000))
		                    : (1000 * total / plain);

	if (r > 1000)
		r = 1000;

	r = 1000 - r;
	dev_info(DEV, "%s bitmap stats [Bytes(packets)]: plain %u(%u), RLE %u(%u), "
	     "total %u; compression: %u.%u%%\n",
			direction,
			c->bytes[1], c->packets[1],
			c->bytes[0], c->packets[0],
			total, r/10, r % 10);
}

/* Since we are processing the bitfield from lower addresses to higher,
   it does not matter if the process it in 32 bit chunks or 64 bit
   chunks as long as it is little endian. (Understand it as byte stream,
   beginning with the lowest byte...) If we would use big endian
   we would need to process it from the highest address to the lowest,
   in order to be agnostic to the 32 vs 64 bits issue.

   returns 0 on failure, 1 if we successfully received it. */
static int receive_bitmap(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	struct bm_xfer_ctx c;
	void *buffer;
	int err;
	int ok = false;
	struct p_header80 *h = &mdev->data.rbuf.header.h80;

	drbd_bm_lock(mdev, "receive bitmap", BM_LOCKED_SET_ALLOWED);
	/* you are supposed to send additional out-of-sync information
	 * if you actually set bits during this phase */

	/* maybe we should use some per thread scratch page,
	 * and allocate that during initial device creation? */
	buffer	 = (unsigned long *) __get_free_page(GFP_NOIO);
	if (!buffer) {
		dev_err(DEV, "failed to allocate one page buffer in %s\n", __func__);
		goto out;
	}

	c = (struct bm_xfer_ctx) {
		.bm_bits = drbd_bm_bits(mdev),
		.bm_words = drbd_bm_words(mdev),
	};

	for(;;) {
		if (cmd == P_BITMAP) {
			err = receive_bitmap_plain(mdev, data_size, buffer, &c);
		} else if (cmd == P_COMPRESSED_BITMAP) {
			/* MAYBE: sanity check that we speak proto >= 90,
			 * and the feature is enabled! */
			struct p_compressed_bm *p;

			if (data_size > BM_PACKET_PAYLOAD_BYTES) {
				dev_err(DEV, "ReportCBitmap packet too large\n");
				goto out;
			}
			/* use the page buff */
			p = buffer;
			memcpy(p, h, sizeof(*h));
			if (drbd_recv(mdev, p->head.payload, data_size) != data_size)
				goto out;
			if (data_size <= (sizeof(*p) - sizeof(p->head))) {
				dev_err(DEV, "ReportCBitmap packet too small (l:%u)\n", data_size);
				goto out;
			}
			err = decode_bitmap_c(mdev, p, &c);
		} else {
			dev_warn(DEV, "receive_bitmap: cmd neither ReportBitMap nor ReportCBitMap (is 0x%x)", cmd);
			goto out;
		}

		c.packets[cmd == P_BITMAP]++;
		c.bytes[cmd == P_BITMAP] += sizeof(struct p_header80) + data_size;

		if (err <= 0) {
			if (err < 0)
				goto out;
			break;
		}
		if (!drbd_recv_header(mdev, &cmd, &data_size))
			goto out;
	}

	INFO_bm_xfer_stats(mdev, "receive", &c);

	if (mdev->state.conn == C_WF_BITMAP_T) {
		enum drbd_state_rv rv;

		ok = !drbd_send_bitmap(mdev);
		if (!ok)
			goto out;
		/* Omit CS_ORDERED with this state transition to avoid deadlocks. */
		rv = _drbd_request_state(mdev, NS(conn, C_WF_SYNC_UUID), CS_VERBOSE);
		D_ASSERT(rv == SS_SUCCESS);
	} else if (mdev->state.conn != C_WF_BITMAP_S) {
		/* admin may have requested C_DISCONNECTING,
		 * other threads may have noticed network errors */
		dev_info(DEV, "unexpected cstate (%s) in receive_bitmap\n",
		    drbd_conn_str(mdev->state.conn));
	}

	ok = true;
 out:
	drbd_bm_unlock(mdev);
	if (ok && mdev->state.conn == C_WF_BITMAP_S)
		drbd_start_resync(mdev, C_SYNC_SOURCE);
	free_page((unsigned long) buffer);
	return ok;
}

static int receive_skip(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	/* TODO zero copy sink :) */
	static char sink[128];
	int size, want, r;

	dev_warn(DEV, "skipping unknown optional packet type %d, l: %d!\n",
		 cmd, data_size);

	size = data_size;
	while (size > 0) {
		want = min_t(int, size, sizeof(sink));
		r = drbd_recv(mdev, sink, want);
		ERR_IF(r <= 0) break;
		size -= r;
	}
	return size == 0;
}

static int receive_UnplugRemote(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	/* Make sure we've acked all the TCP data associated
	 * with the data requests being unplugged */
	drbd_tcp_quickack(mdev->data.socket);

	return true;
}

static int receive_out_of_sync(struct drbd_conf *mdev, enum drbd_packets cmd, unsigned int data_size)
{
	struct p_block_desc *p = &mdev->data.rbuf.block_desc;

	switch (mdev->state.conn) {
	case C_WF_SYNC_UUID:
	case C_WF_BITMAP_T:
	case C_BEHIND:
			break;
	default:
		dev_err(DEV, "ASSERT FAILED cstate = %s, expected: WFSyncUUID|WFBitMapT|Behind\n",
				drbd_conn_str(mdev->state.conn));
	}

	drbd_set_out_of_sync(mdev, be64_to_cpu(p->sector), be32_to_cpu(p->blksize));

	return true;
}

typedef int (*drbd_cmd_handler_f)(struct drbd_conf *, enum drbd_packets cmd, unsigned int to_receive);

struct data_cmd {
	int expect_payload;
	size_t pkt_size;
	drbd_cmd_handler_f function;
};

static struct data_cmd drbd_cmd_handler[] = {
	[P_DATA]	    = { 1, sizeof(struct p_data), receive_Data },
	[P_DATA_REPLY]	    = { 1, sizeof(struct p_data), receive_DataReply },
	[P_RS_DATA_REPLY]   = { 1, sizeof(struct p_data), receive_RSDataReply } ,
	[P_BARRIER]	    = { 0, sizeof(struct p_barrier), receive_Barrier } ,
	[P_BITMAP]	    = { 1, sizeof(struct p_header80), receive_bitmap } ,
	[P_COMPRESSED_BITMAP] = { 1, sizeof(struct p_header80), receive_bitmap } ,
	[P_UNPLUG_REMOTE]   = { 0, sizeof(struct p_header80), receive_UnplugRemote },
	[P_DATA_REQUEST]    = { 0, sizeof(struct p_block_req), receive_DataRequest },
	[P_RS_DATA_REQUEST] = { 0, sizeof(struct p_block_req), receive_DataRequest },
	[P_SYNC_PARAM]	    = { 1, sizeof(struct p_header80), receive_SyncParam },
	[P_SYNC_PARAM89]    = { 1, sizeof(struct p_header80), receive_SyncParam },
	[P_PROTOCOL]        = { 1, sizeof(struct p_protocol), receive_protocol },
	[P_UUIDS]	    = { 0, sizeof(struct p_uuids), receive_uuids },
	[P_SIZES]	    = { 0, sizeof(struct p_sizes), receive_sizes },
	[P_STATE]	    = { 0, sizeof(struct p_state), receive_state },
	[P_STATE_CHG_REQ]   = { 0, sizeof(struct p_req_state), receive_req_state },
	[P_SYNC_UUID]       = { 0, sizeof(struct p_rs_uuid), receive_sync_uuid },
	[P_OV_REQUEST]      = { 0, sizeof(struct p_block_req), receive_DataRequest },
	[P_OV_REPLY]        = { 1, sizeof(struct p_block_req), receive_DataRequest },
	[P_CSUM_RS_REQUEST] = { 1, sizeof(struct p_block_req), receive_DataRequest },
	[P_DELAY_PROBE]     = { 0, sizeof(struct p_delay_probe93), receive_skip },
	[P_OUT_OF_SYNC]     = { 0, sizeof(struct p_block_desc), receive_out_of_sync },
	/* anything missing from this table is in
	 * the asender_tbl, see get_asender_cmd */
	[P_MAX_CMD]	    = { 0, 0, NULL },
};

/* All handler functions that expect a sub-header get that sub-heder in
   mdev->data.rbuf.header.head.payload.

   Usually in mdev->data.rbuf.header.head the callback can find the usual
   p_header, but they may not rely on that. Since there is also p_header95 !
 */

static void drbdd(struct drbd_conf *mdev)
{
	union p_header *header = &mdev->data.rbuf.header;
	unsigned int packet_size;
	enum drbd_packets cmd;
	size_t shs; /* sub header size */
	int rv;

	while (get_t_state(&mdev->receiver) == Running) {
		drbd_thread_current_set_cpu(mdev);
		if (!drbd_recv_header(mdev, &cmd, &packet_size))
			goto err_out;

		if (unlikely(cmd >= P_MAX_CMD || !drbd_cmd_handler[cmd].function)) {
			dev_err(DEV, "unknown packet type %d, l: %d!\n", cmd, packet_size);
			goto err_out;
		}

		shs = drbd_cmd_handler[cmd].pkt_size - sizeof(union p_header);
		if (packet_size - shs > 0 && !drbd_cmd_handler[cmd].expect_payload) {
			dev_err(DEV, "No payload expected %s l:%d\n", cmdname(cmd), packet_size);
			goto err_out;
		}

		if (shs) {
			rv = drbd_recv(mdev, &header->h80.payload, shs);
			if (unlikely(rv != shs)) {
				if (!signal_pending(current))
					dev_warn(DEV, "short read while reading sub header: rv=%d\n", rv);
				goto err_out;
			}
		}

		rv = drbd_cmd_handler[cmd].function(mdev, cmd, packet_size - shs);

		if (unlikely(!rv)) {
			dev_err(DEV, "error receiving %s, l: %d!\n",
			    cmdname(cmd), packet_size);
			goto err_out;
		}
	}

	if (0) {
	err_out:
		drbd_force_state(mdev, NS(conn, C_PROTOCOL_ERROR));
	}
	/* If we leave here, we probably want to update at least the
	 * "Connected" indicator on stable storage. Do so explicitly here. */
	drbd_md_sync(mdev);
}

void drbd_flush_workqueue(struct drbd_conf *mdev)
{
	struct drbd_wq_barrier barr;

	barr.w.cb = w_prev_work_done;
	init_completion(&barr.done);
	drbd_queue_work(&mdev->data.work, &barr.w);
	wait_for_completion(&barr.done);
}

void drbd_free_tl_hash(struct drbd_conf *mdev)
{
	struct hlist_head *h;

	spin_lock_irq(&mdev->req_lock);

	if (!mdev->tl_hash || mdev->state.conn != C_STANDALONE) {
		spin_unlock_irq(&mdev->req_lock);
		return;
	}
	/* paranoia code */
	for (h = mdev->ee_hash; h < mdev->ee_hash + mdev->ee_hash_s; h++)
		if (h->first)
			dev_err(DEV, "ASSERT FAILED ee_hash[%u].first == %p, expected NULL\n",
				(int)(h - mdev->ee_hash), h->first);
	kfree(mdev->ee_hash);
	mdev->ee_hash = NULL;
	mdev->ee_hash_s = 0;

	/* paranoia code */
	for (h = mdev->tl_hash; h < mdev->tl_hash + mdev->tl_hash_s; h++)
		if (h->first)
			dev_err(DEV, "ASSERT FAILED tl_hash[%u] == %p, expected NULL\n",
				(int)(h - mdev->tl_hash), h->first);
	kfree(mdev->tl_hash);
	mdev->tl_hash = NULL;
	mdev->tl_hash_s = 0;
	spin_unlock_irq(&mdev->req_lock);
}

static void drbd_disconnect(struct drbd_conf *mdev)
{
	enum drbd_fencing_p fp;
	union drbd_state os, ns;
	int rv = SS_UNKNOWN_ERROR;
	unsigned int i;

	if (mdev->state.conn == C_STANDALONE)
		return;

	/* asender does not clean up anything. it must not interfere, either */
	drbd_thread_stop(&mdev->asender);
	drbd_free_sock(mdev);

	/* wait for current activity to cease. */
	spin_lock_irq(&mdev->req_lock);
	_drbd_wait_ee_list_empty(mdev, &mdev->active_ee);
	_drbd_wait_ee_list_empty(mdev, &mdev->sync_ee);
	_drbd_wait_ee_list_empty(mdev, &mdev->read_ee);
	spin_unlock_irq(&mdev->req_lock);

	/* We do not have data structures that would allow us to
	 * get the rs_pending_cnt down to 0 again.
	 *  * On C_SYNC_TARGET we do not have any data structures describing
	 *    the pending RSDataRequest's we have sent.
	 *  * On C_SYNC_SOURCE there is no data structure that tracks
	 *    the P_RS_DATA_REPLY blocks that we sent to the SyncTarget.
	 *  And no, it is not the sum of the reference counts in the
	 *  resync_LRU. The resync_LRU tracks the whole operation including
	 *  the disk-IO, while the rs_pending_cnt only tracks the blocks
	 *  on the fly. */
	drbd_rs_cancel_all(mdev);
	mdev->rs_total = 0;
	mdev->rs_failed = 0;
	atomic_set(&mdev->rs_pending_cnt, 0);
	wake_up(&mdev->misc_wait);

	/* make sure syncer is stopped and w_resume_next_sg queued */
	del_timer_sync(&mdev->resync_timer);
	resync_timer_fn((unsigned long)mdev);

	/* wait for all w_e_end_data_req, w_e_end_rsdata_req, w_send_barrier,
	 * w_make_resync_request etc. which may still be on the worker queue
	 * to be "canceled" */
	drbd_flush_workqueue(mdev);

	/* This also does reclaim_net_ee().  If we do this too early, we might
	 * miss some resync ee and pages.*/
	drbd_process_done_ee(mdev);

	kfree(mdev->p_uuid);
	mdev->p_uuid = NULL;

	if (!is_susp(mdev->state))
		tl_clear(mdev);

	dev_info(DEV, "Connection closed\n");

	drbd_md_sync(mdev);

	fp = FP_DONT_CARE;
	if (get_ldev(mdev)) {
		fp = mdev->ldev->dc.fencing;
		put_ldev(mdev);
	}

	if (mdev->state.role == R_PRIMARY && fp >= FP_RESOURCE && mdev->state.pdsk >= D_UNKNOWN)
		drbd_try_outdate_peer_async(mdev);

	spin_lock_irq(&mdev->req_lock);
	os = mdev->state;
	if (os.conn >= C_UNCONNECTED) {
		/* Do not restart in case we are C_DISCONNECTING */
		ns = os;
		ns.conn = C_UNCONNECTED;
		rv = _drbd_set_state(mdev, ns, CS_VERBOSE, NULL);
	}
	spin_unlock_irq(&mdev->req_lock);

	if (os.conn == C_DISCONNECTING) {
		wait_event(mdev->net_cnt_wait, atomic_read(&mdev->net_cnt) == 0);

		crypto_free_hash(mdev->cram_hmac_tfm);
		mdev->cram_hmac_tfm = NULL;

		kfree(mdev->net_conf);
		mdev->net_conf = NULL;
		drbd_request_state(mdev, NS(conn, C_STANDALONE));
	}

	/* serialize with bitmap writeout triggered by the state change,
	 * if any. */
	wait_event(mdev->misc_wait, !test_bit(BITMAP_IO, &mdev->flags));

	/* tcp_close and release of sendpage pages can be deferred.  I don't
	 * want to use SO_LINGER, because apparently it can be deferred for
	 * more than 20 seconds (longest time I checked).
	 *
	 * Actually we don't care for exactly when the network stack does its
	 * put_page(), but release our reference on these pages right here.
	 */
	i = drbd_release_ee(mdev, &mdev->net_ee);
	if (i)
		dev_info(DEV, "net_ee not empty, killed %u entries\n", i);
	i = atomic_read(&mdev->pp_in_use_by_net);
	if (i)
		dev_info(DEV, "pp_in_use_by_net = %d, expected 0\n", i);
	i = atomic_read(&mdev->pp_in_use);
	if (i)
		dev_info(DEV, "pp_in_use = %d, expected 0\n", i);

	D_ASSERT(list_empty(&mdev->read_ee));
	D_ASSERT(list_empty(&mdev->active_ee));
	D_ASSERT(list_empty(&mdev->sync_ee));
	D_ASSERT(list_empty(&mdev->done_ee));

	/* ok, no more ee's on the fly, it is safe to reset the epoch_size */
	atomic_set(&mdev->current_epoch->epoch_size, 0);
	D_ASSERT(list_empty(&mdev->current_epoch->list));
}

/*
 * We support PRO_VERSION_MIN to PRO_VERSION_MAX. The protocol version
 * we can agree on is stored in agreed_pro_version.
 *
 * feature flags and the reserved array should be enough room for future
 * enhancements of the handshake protocol, and possible plugins...
 *
 * for now, they are expected to be zero, but ignored.
 */
static int drbd_send_handshake(struct drbd_conf *mdev)
{
	/* ASSERT current == mdev->receiver ... */
	struct p_handshake *p = &mdev->data.sbuf.handshake;
	int ok;

	if (mutex_lock_interruptible(&mdev->data.mutex)) {
		dev_err(DEV, "interrupted during initial handshake\n");
		return 0; /* interrupted. not ok. */
	}

	if (mdev->data.socket == NULL) {
		mutex_unlock(&mdev->data.mutex);
		return 0;
	}

	memset(p, 0, sizeof(*p));
	p->protocol_min = cpu_to_be32(PRO_VERSION_MIN);
	p->protocol_max = cpu_to_be32(PRO_VERSION_MAX);
	ok = _drbd_send_cmd( mdev, mdev->data.socket, P_HAND_SHAKE,
			     (struct p_header80 *)p, sizeof(*p), 0 );
	mutex_unlock(&mdev->data.mutex);
	return ok;
}

/*
 * return values:
 *   1 yes, we have a valid connection
 *   0 oops, did not work out, please try again
 *  -1 peer talks different language,
 *     no point in trying again, please go standalone.
 */
static int drbd_do_handshake(struct drbd_conf *mdev)
{
	/* ASSERT current == mdev->receiver ... */
	struct p_handshake *p = &mdev->data.rbuf.handshake;
	const int expect = sizeof(struct p_handshake) - sizeof(struct p_header80);
	unsigned int length;
	enum drbd_packets cmd;
	int rv;

	rv = drbd_send_handshake(mdev);
	if (!rv)
		return 0;

	rv = drbd_recv_header(mdev, &cmd, &length);
	if (!rv)
		return 0;

	if (cmd != P_HAND_SHAKE) {
		dev_err(DEV, "expected HandShake packet, received: %s (0x%04x)\n",
		     cmdname(cmd), cmd);
		return -1;
	}

	if (length != expect) {
		dev_err(DEV, "expected HandShake length: %u, received: %u\n",
		     expect, length);
		return -1;
	}

	rv = drbd_recv(mdev, &p->head.payload, expect);

	if (rv != expect) {
		if (!signal_pending(current))
			dev_warn(DEV, "short read receiving handshake packet: l=%u\n", rv);
		return 0;
	}

	p->protocol_min = be32_to_cpu(p->protocol_min);
	p->protocol_max = be32_to_cpu(p->protocol_max);
	if (p->protocol_max == 0)
		p->protocol_max = p->protocol_min;

	if (PRO_VERSION_MAX < p->protocol_min ||
	    PRO_VERSION_MIN > p->protocol_max)
		goto incompat;

	mdev->agreed_pro_version = min_t(int, PRO_VERSION_MAX, p->protocol_max);

	dev_info(DEV, "Handshake successful: "
	     "Agreed network protocol version %d\n", mdev->agreed_pro_version);

	return 1;

 incompat:
	dev_err(DEV, "incompatible DRBD dialects: "
	    "I support %d-%d, peer supports %d-%d\n",
	    PRO_VERSION_MIN, PRO_VERSION_MAX,
	    p->protocol_min, p->protocol_max);
	return -1;
}

#if !defined(CONFIG_CRYPTO_HMAC) && !defined(CONFIG_CRYPTO_HMAC_MODULE)
static int drbd_do_auth(struct drbd_conf *mdev)
{
	dev_err(DEV, "This kernel was build without CONFIG_CRYPTO_HMAC.\n");
	dev_err(DEV, "You need to disable 'cram-hmac-alg' in drbd.conf.\n");
	return -1;
}
#else
#define CHALLENGE_LEN 64

/* Return value:
	1 - auth succeeded,
	0 - failed, try again (network error),
	-1 - auth failed, don't try again.
*/

static int drbd_do_auth(struct drbd_conf *mdev)
{
	char my_challenge[CHALLENGE_LEN];  /* 64 Bytes... */
	struct scatterlist sg;
	char *response = NULL;
	char *right_response = NULL;
	char *peers_ch = NULL;
	unsigned int key_len = strlen(mdev->net_conf->shared_secret);
	unsigned int resp_size;
	struct hash_desc desc;
	enum drbd_packets cmd;
	unsigned int length;
	int rv;

	desc.tfm = mdev->cram_hmac_tfm;
	desc.flags = 0;

	rv = crypto_hash_setkey(mdev->cram_hmac_tfm,
				(u8 *)mdev->net_conf->shared_secret, key_len);
	if (rv) {
		dev_err(DEV, "crypto_hash_setkey() failed with %d\n", rv);
		rv = -1;
		goto fail;
	}

	get_random_bytes(my_challenge, CHALLENGE_LEN);

	rv = drbd_send_cmd2(mdev, P_AUTH_CHALLENGE, my_challenge, CHALLENGE_LEN);
	if (!rv)
		goto fail;

	rv = drbd_recv_header(mdev, &cmd, &length);
	if (!rv)
		goto fail;

	if (cmd != P_AUTH_CHALLENGE) {
		dev_err(DEV, "expected AuthChallenge packet, received: %s (0x%04x)\n",
		    cmdname(cmd), cmd);
		rv = 0;
		goto fail;
	}

	if (length > CHALLENGE_LEN * 2) {
		dev_err(DEV, "expected AuthChallenge payload too big.\n");
		rv = -1;
		goto fail;
	}

	peers_ch = kmalloc(length, GFP_NOIO);
	if (peers_ch == NULL) {
		dev_err(DEV, "kmalloc of peers_ch failed\n");
		rv = -1;
		goto fail;
	}

	rv = drbd_recv(mdev, peers_ch, length);

	if (rv != length) {
		if (!signal_pending(current))
			dev_warn(DEV, "short read AuthChallenge: l=%u\n", rv);
		rv = 0;
		goto fail;
	}

	resp_size = crypto_hash_digestsize(mdev->cram_hmac_tfm);
	response = kmalloc(resp_size, GFP_NOIO);
	if (response == NULL) {
		dev_err(DEV, "kmalloc of response failed\n");
		rv = -1;
		goto fail;
	}

	sg_init_table(&sg, 1);
	sg_set_buf(&sg, peers_ch, length);

	rv = crypto_hash_digest(&desc, &sg, sg.length, response);
	if (rv) {
		dev_err(DEV, "crypto_hash_digest() failed with %d\n", rv);
		rv = -1;
		goto fail;
	}

	rv = drbd_send_cmd2(mdev, P_AUTH_RESPONSE, response, resp_size);
	if (!rv)
		goto fail;

	rv = drbd_recv_header(mdev, &cmd, &length);
	if (!rv)
		goto fail;

	if (cmd != P_AUTH_RESPONSE) {
		dev_err(DEV, "expected AuthResponse packet, received: %s (0x%04x)\n",
			cmdname(cmd), cmd);
		rv = 0;
		goto fail;
	}

	if (length != resp_size) {
		dev_err(DEV, "expected AuthResponse payload of wrong size\n");
		rv = 0;
		goto fail;
	}

	rv = drbd_recv(mdev, response , resp_size);

	if (rv != resp_size) {
		if (!signal_pending(current))
			dev_warn(DEV, "short read receiving AuthResponse: l=%u\n", rv);
		rv = 0;
		goto fail;
	}

	right_response = kmalloc(resp_size, GFP_NOIO);
	if (right_response == NULL) {
		dev_err(DEV, "kmalloc of right_response failed\n");
		rv = -1;
		goto fail;
	}

	sg_set_buf(&sg, my_challenge, CHALLENGE_LEN);

	rv = crypto_hash_digest(&desc, &sg, sg.length, right_response);
	if (rv) {
		dev_err(DEV, "crypto_hash_digest() failed with %d\n", rv);
		rv = -1;
		goto fail;
	}

	rv = !memcmp(response, right_response, resp_size);

	if (rv)
		dev_info(DEV, "Peer authenticated using %d bytes of '%s' HMAC\n",
		     resp_size, mdev->net_conf->cram_hmac_alg);
	else
		rv = -1;

 fail:
	kfree(peers_ch);
	kfree(response);
	kfree(right_response);

	return rv;
}
#endif

int drbdd_init(struct drbd_thread *thi)
{
	struct drbd_conf *mdev = thi->mdev;
	unsigned int minor = mdev_to_minor(mdev);
	int h;

	sprintf(current->comm, "drbd%d_receiver", minor);

	dev_info(DEV, "receiver (re)started\n");

	do {
		h = drbd_connect(mdev);
		if (h == 0) {
			drbd_disconnect(mdev);
			schedule_timeout_interruptible(HZ);
		}
		if (h == -1) {
			dev_warn(DEV, "Discarding network configuration.\n");
			drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
		}
	} while (h == 0);

	if (h > 0) {
		if (get_net_conf(mdev)) {
			drbdd(mdev);
			put_net_conf(mdev);
		}
	}

	drbd_disconnect(mdev);

	dev_info(DEV, "receiver terminated\n");
	return 0;
}

/* ********* acknowledge sender ******** */

static int got_RqSReply(struct drbd_conf *mdev, struct p_header80 *h)
{
	struct p_req_state_reply *p = (struct p_req_state_reply *)h;

	int retcode = be32_to_cpu(p->retcode);

	if (retcode >= SS_SUCCESS) {
		set_bit(CL_ST_CHG_SUCCESS, &mdev->flags);
	} else {
		set_bit(CL_ST_CHG_FAIL, &mdev->flags);
		dev_err(DEV, "Requested state change failed by peer: %s (%d)\n",
		    drbd_set_st_err_str(retcode), retcode);
	}
	wake_up(&mdev->state_wait);

	return true;
}

static int got_Ping(struct drbd_conf *mdev, struct p_header80 *h)
{
	return drbd_send_ping_ack(mdev);

}

static int got_PingAck(struct drbd_conf *mdev, struct p_header80 *h)
{
	/* restore idle timeout */
	mdev->meta.socket->sk->sk_rcvtimeo = mdev->net_conf->ping_int*HZ;
	if (!test_and_set_bit(GOT_PING_ACK, &mdev->flags))
		wake_up(&mdev->misc_wait);

	return true;
}

static int got_IsInSync(struct drbd_conf *mdev, struct p_header80 *h)
{
	struct p_block_ack *p = (struct p_block_ack *)h;
	sector_t sector = be64_to_cpu(p->sector);
	int blksize = be32_to_cpu(p->blksize);

	D_ASSERT(mdev->agreed_pro_version >= 89);

	update_peer_seq(mdev, be32_to_cpu(p->seq_num));

	if (get_ldev(mdev)) {
		drbd_rs_complete_io(mdev, sector);
		drbd_set_in_sync(mdev, sector, blksize);
		/* rs_same_csums is supposed to count in units of BM_BLOCK_SIZE */
		mdev->rs_same_csum += (blksize >> BM_BLOCK_SHIFT);
		put_ldev(mdev);
	}
	dec_rs_pending(mdev);
	atomic_add(blksize >> 9, &mdev->rs_sect_in);

	return true;
}

/* when we receive the ACK for a write request,
 * verify that we actually know about it */
static struct drbd_request *_ack_id_to_req(struct drbd_conf *mdev,
	u64 id, sector_t sector)
{
	struct hlist_head *slot = tl_hash_slot(mdev, sector);
	struct hlist_node *n;
	struct drbd_request *req;

	hlist_for_each_entry(req, n, slot, collision) {
		if ((unsigned long)req == (unsigned long)id) {
			if (req->sector != sector) {
				dev_err(DEV, "_ack_id_to_req: found req %p but it has "
				    "wrong sector (%llus versus %llus)\n", req,
				    (unsigned long long)req->sector,
				    (unsigned long long)sector);
				break;
			}
			return req;
		}
	}
	return NULL;
}

typedef struct drbd_request *(req_validator_fn)
	(struct drbd_conf *mdev, u64 id, sector_t sector);

static int validate_req_change_req_state(struct drbd_conf *mdev,
	u64 id, sector_t sector, req_validator_fn validator,
	const char *func, enum drbd_req_event what)
{
	struct drbd_request *req;
	struct bio_and_error m;

	spin_lock_irq(&mdev->req_lock);
	req = validator(mdev, id, sector);
	if (unlikely(!req)) {
		spin_unlock_irq(&mdev->req_lock);

		dev_err(DEV, "%s: failed to find req %p, sector %llus\n", func,
			(void *)(unsigned long)id, (unsigned long long)sector);
		return false;
	}
	__req_mod(req, what, &m);
	spin_unlock_irq(&mdev->req_lock);

	if (m.bio)
		complete_master_bio(mdev, &m);
	return true;
}

static int got_BlockAck(struct drbd_conf *mdev, struct p_header80 *h)
{
	struct p_block_ack *p = (struct p_block_ack *)h;
	sector_t sector = be64_to_cpu(p->sector);
	int blksize = be32_to_cpu(p->blksize);
	enum drbd_req_event what;

	update_peer_seq(mdev, be32_to_cpu(p->seq_num));

	if (is_syncer_block_id(p->block_id)) {
		drbd_set_in_sync(mdev, sector, blksize);
		dec_rs_pending(mdev);
		return true;
	}
	switch (be16_to_cpu(h->command)) {
	case P_RS_WRITE_ACK:
		D_ASSERT(mdev->net_conf->wire_protocol == DRBD_PROT_C);
		what = write_acked_by_peer_and_sis;
		break;
	case P_WRITE_ACK:
		D_ASSERT(mdev->net_conf->wire_protocol == DRBD_PROT_C);
		what = write_acked_by_peer;
		break;
	case P_RECV_ACK:
		D_ASSERT(mdev->net_conf->wire_protocol == DRBD_PROT_B);
		what = recv_acked_by_peer;
		break;
	case P_DISCARD_ACK:
		D_ASSERT(mdev->net_conf->wire_protocol == DRBD_PROT_C);
		what = conflict_discarded_by_peer;
		break;
	default:
		D_ASSERT(0);
		return false;
	}

	return validate_req_change_req_state(mdev, p->block_id, sector,
		_ack_id_to_req, __func__ , what);
}

static int got_NegAck(struct drbd_conf *mdev, struct p_header80 *h)
{
	struct p_block_ack *p = (struct p_block_ack *)h;
	sector_t sector = be64_to_cpu(p->sector);
	int size = be32_to_cpu(p->blksize);
	struct drbd_request *req;
	struct bio_and_error m;

	update_peer_seq(mdev, be32_to_cpu(p->seq_num));

	if (is_syncer_block_id(p->block_id)) {
		dec_rs_pending(mdev);
		drbd_rs_failed_io(mdev, sector, size);
		return true;
	}

	spin_lock_irq(&mdev->req_lock);
	req = _ack_id_to_req(mdev, p->block_id, sector);
	if (!req) {
		spin_unlock_irq(&mdev->req_lock);
		if (mdev->net_conf->wire_protocol == DRBD_PROT_A ||
		    mdev->net_conf->wire_protocol == DRBD_PROT_B) {
			/* Protocol A has no P_WRITE_ACKs, but has P_NEG_ACKs.
			   The master bio might already be completed, therefore the
			   request is no longer in the collision hash.
			   => Do not try to validate block_id as request. */
			/* In Protocol B we might already have got a P_RECV_ACK
			   but then get a P_NEG_ACK after wards. */
			drbd_set_out_of_sync(mdev, sector, size);
			return true;
		} else {
			dev_err(DEV, "%s: failed to find req %p, sector %llus\n", __func__,
				(void *)(unsigned long)p->block_id, (unsigned long long)sector);
			return false;
		}
	}
	__req_mod(req, neg_acked, &m);
	spin_unlock_irq(&mdev->req_lock);

	if (m.bio)
		complete_master_bio(mdev, &m);
	return true;
}

static int got_NegDReply(struct drbd_conf *mdev, struct p_header80 *h)
{
	struct p_block_ack *p = (struct p_block_ack *)h;
	sector_t sector = be64_to_cpu(p->sector);

	update_peer_seq(mdev, be32_to_cpu(p->seq_num));
	dev_err(DEV, "Got NegDReply; Sector %llus, len %u; Fail original request.\n",
	    (unsigned long long)sector, be32_to_cpu(p->blksize));

	return validate_req_change_req_state(mdev, p->block_id, sector,
		_ar_id_to_req, __func__ , neg_acked);
}

static int got_NegRSDReply(struct drbd_conf *mdev, struct p_header80 *h)
{
	sector_t sector;
	int size;
	struct p_block_ack *p = (struct p_block_ack *)h;

	sector = be64_to_cpu(p->sector);
	size = be32_to_cpu(p->blksize);

	update_peer_seq(mdev, be32_to_cpu(p->seq_num));

	dec_rs_pending(mdev);

	if (get_ldev_if_state(mdev, D_FAILED)) {
		drbd_rs_complete_io(mdev, sector);
		switch (be16_to_cpu(h->command)) {
		case P_NEG_RS_DREPLY:
			drbd_rs_failed_io(mdev, sector, size);
		case P_RS_CANCEL:
			break;
		default:
			D_ASSERT(0);
			put_ldev(mdev);
			return false;
		}
		put_ldev(mdev);
	}

	return true;
}

static int got_BarrierAck(struct drbd_conf *mdev, struct p_header80 *h)
{
	struct p_barrier_ack *p = (struct p_barrier_ack *)h;

	tl_release(mdev, p->barrier, be32_to_cpu(p->set_size));

	if (mdev->state.conn == C_AHEAD &&
	    atomic_read(&mdev->ap_in_flight) == 0 &&
	    !test_and_set_bit(AHEAD_TO_SYNC_SOURCE, &mdev->current_epoch->flags)) {
		mdev->start_resync_timer.expires = jiffies + HZ;
		add_timer(&mdev->start_resync_timer);
	}

	return true;
}

static int got_OVResult(struct drbd_conf *mdev, struct p_header80 *h)
{
	struct p_block_ack *p = (struct p_block_ack *)h;
	struct drbd_work *w;
	sector_t sector;
	int size;

	sector = be64_to_cpu(p->sector);
	size = be32_to_cpu(p->blksize);

	update_peer_seq(mdev, be32_to_cpu(p->seq_num));

	if (be64_to_cpu(p->block_id) == ID_OUT_OF_SYNC)
		drbd_ov_oos_found(mdev, sector, size);
	else
		ov_oos_print(mdev);

	if (!get_ldev(mdev))
		return true;

	drbd_rs_complete_io(mdev, sector);
	dec_rs_pending(mdev);

	--mdev->ov_left;

	/* let's advance progress step marks only for every other megabyte */
	if ((mdev->ov_left & 0x200) == 0x200)
		drbd_advance_rs_marks(mdev, mdev->ov_left);

	if (mdev->ov_left == 0) {
		w = kmalloc(sizeof(*w), GFP_NOIO);
		if (w) {
			w->cb = w_ov_finished;
			drbd_queue_work_front(&mdev->data.work, w);
		} else {
			dev_err(DEV, "kmalloc(w) failed.");
			ov_oos_print(mdev);
			drbd_resync_finished(mdev);
		}
	}
	put_ldev(mdev);
	return true;
}

static int got_skip(struct drbd_conf *mdev, struct p_header80 *h)
{
	return true;
}

struct asender_cmd {
	size_t pkt_size;
	int (*process)(struct drbd_conf *mdev, struct p_header80 *h);
};

static struct asender_cmd *get_asender_cmd(int cmd)
{
	static struct asender_cmd asender_tbl[] = {
		/* anything missing from this table is in
		 * the drbd_cmd_handler (drbd_default_handler) table,
		 * see the beginning of drbdd() */
	[P_PING]	    = { sizeof(struct p_header80), got_Ping },
	[P_PING_ACK]	    = { sizeof(struct p_header80), got_PingAck },
	[P_RECV_ACK]	    = { sizeof(struct p_block_ack), got_BlockAck },
	[P_WRITE_ACK]	    = { sizeof(struct p_block_ack), got_BlockAck },
	[P_RS_WRITE_ACK]    = { sizeof(struct p_block_ack), got_BlockAck },
	[P_DISCARD_ACK]	    = { sizeof(struct p_block_ack), got_BlockAck },
	[P_NEG_ACK]	    = { sizeof(struct p_block_ack), got_NegAck },
	[P_NEG_DREPLY]	    = { sizeof(struct p_block_ack), got_NegDReply },
	[P_NEG_RS_DREPLY]   = { sizeof(struct p_block_ack), got_NegRSDReply},
	[P_OV_RESULT]	    = { sizeof(struct p_block_ack), got_OVResult },
	[P_BARRIER_ACK]	    = { sizeof(struct p_barrier_ack), got_BarrierAck },
	[P_STATE_CHG_REPLY] = { sizeof(struct p_req_state_reply), got_RqSReply },
	[P_RS_IS_IN_SYNC]   = { sizeof(struct p_block_ack), got_IsInSync },
	[P_DELAY_PROBE]     = { sizeof(struct p_delay_probe93), got_skip },
	[P_RS_CANCEL]       = { sizeof(struct p_block_ack), got_NegRSDReply},
	[P_MAX_CMD]	    = { 0, NULL },
	};
	if (cmd > P_MAX_CMD || asender_tbl[cmd].process == NULL)
		return NULL;
	return &asender_tbl[cmd];
}

int drbd_asender(struct drbd_thread *thi)
{
	struct drbd_conf *mdev = thi->mdev;
	struct p_header80 *h = &mdev->meta.rbuf.header.h80;
	struct asender_cmd *cmd = NULL;

	int rv, len;
	void *buf    = h;
	int received = 0;
	int expect   = sizeof(struct p_header80);
	int empty;
	int ping_timeout_active = 0;

	sprintf(current->comm, "drbd%d_asender", mdev_to_minor(mdev));

	current->policy = SCHED_RR;  /* Make this a realtime task! */
	current->rt_priority = 2;    /* more important than all other tasks */

	while (get_t_state(thi) == Running) {
		drbd_thread_current_set_cpu(mdev);
		if (test_and_clear_bit(SEND_PING, &mdev->flags)) {
			ERR_IF(!drbd_send_ping(mdev)) goto reconnect;
			mdev->meta.socket->sk->sk_rcvtimeo =
				mdev->net_conf->ping_timeo*HZ/10;
			ping_timeout_active = 1;
		}

		/* conditionally cork;
		 * it may hurt latency if we cork without much to send */
		if (!mdev->net_conf->no_cork &&
			3 < atomic_read(&mdev->unacked_cnt))
			drbd_tcp_cork(mdev->meta.socket);
		while (1) {
			clear_bit(SIGNAL_ASENDER, &mdev->flags);
			flush_signals(current);
			if (!drbd_process_done_ee(mdev))
				goto reconnect;
			/* to avoid race with newly queued ACKs */
			set_bit(SIGNAL_ASENDER, &mdev->flags);
			spin_lock_irq(&mdev->req_lock);
			empty = list_empty(&mdev->done_ee);
			spin_unlock_irq(&mdev->req_lock);
			/* new ack may have been queued right here,
			 * but then there is also a signal pending,
			 * and we start over... */
			if (empty)
				break;
		}
		/* but unconditionally uncork unless disabled */
		if (!mdev->net_conf->no_cork)
			drbd_tcp_uncork(mdev->meta.socket);

		/* short circuit, recv_msg would return EINTR anyways. */
		if (signal_pending(current))
			continue;

		rv = drbd_recv_short(mdev, mdev->meta.socket,
				     buf, expect-received, 0);
		clear_bit(SIGNAL_ASENDER, &mdev->flags);

		flush_signals(current);

		/* Note:
		 * -EINTR	 (on meta) we got a signal
		 * -EAGAIN	 (on meta) rcvtimeo expired
		 * -ECONNRESET	 other side closed the connection
		 * -ERESTARTSYS  (on data) we got a signal
		 * rv <  0	 other than above: unexpected error!
		 * rv == expected: full header or command
		 * rv <  expected: "woken" by signal during receive
		 * rv == 0	 : "connection shut down by peer"
		 */
		if (likely(rv > 0)) {
			received += rv;
			buf	 += rv;
		} else if (rv == 0) {
			dev_err(DEV, "meta connection shut down by peer.\n");
			goto reconnect;
		} else if (rv == -EAGAIN) {
			/* If the data socket received something meanwhile,
			 * that is good enough: peer is still alive. */
			if (time_after(mdev->last_received,
				jiffies - mdev->meta.socket->sk->sk_rcvtimeo))
				continue;
			if (ping_timeout_active) {
				dev_err(DEV, "PingAck did not arrive in time.\n");
				goto reconnect;
			}
			set_bit(SEND_PING, &mdev->flags);
			continue;
		} else if (rv == -EINTR) {
			continue;
		} else {
			dev_err(DEV, "sock_recvmsg returned %d\n", rv);
			goto reconnect;
		}

		if (received == expect && cmd == NULL) {
			if (unlikely(h->magic != BE_DRBD_MAGIC)) {
				dev_err(DEV, "magic?? on meta m: 0x%08x c: %d l: %d\n",
				    be32_to_cpu(h->magic),
				    be16_to_cpu(h->command),
				    be16_to_cpu(h->length));
				goto reconnect;
			}
			cmd = get_asender_cmd(be16_to_cpu(h->command));
			len = be16_to_cpu(h->length);
			if (unlikely(cmd == NULL)) {
				dev_err(DEV, "unknown command?? on meta m: 0x%08x c: %d l: %d\n",
				    be32_to_cpu(h->magic),
				    be16_to_cpu(h->command),
				    be16_to_cpu(h->length));
				goto disconnect;
			}
			expect = cmd->pkt_size;
			ERR_IF(len != expect-sizeof(struct p_header80))
				goto reconnect;
		}
		if (received == expect) {
			mdev->last_received = jiffies;
			D_ASSERT(cmd != NULL);
			if (!cmd->process(mdev, h))
				goto reconnect;

			/* the idle_timeout (ping-int)
			 * has been restored in got_PingAck() */
			if (cmd == get_asender_cmd(P_PING_ACK))
				ping_timeout_active = 0;

			buf	 = h;
			received = 0;
			expect	 = sizeof(struct p_header80);
			cmd	 = NULL;
		}
	}

	if (0) {
reconnect:
		drbd_force_state(mdev, NS(conn, C_NETWORK_FAILURE));
		drbd_md_sync(mdev);
	}
	if (0) {
disconnect:
		drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
		drbd_md_sync(mdev);
	}
	clear_bit(SIGNAL_ASENDER, &mdev->flags);

	D_ASSERT(mdev->state.conn < C_CONNECTED);
	dev_info(DEV, "asender terminated\n");

	return 0;
}
