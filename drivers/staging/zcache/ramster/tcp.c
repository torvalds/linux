/* -*- mode: c; c-basic-offset: 8; -*-
 *
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * ----
 *
 * Callers for this were originally written against a very simple synchronus
 * API.  This implementation reflects those simple callers.  Some day I'm sure
 * we'll need to move to a more robust posting/callback mechanism.
 *
 * Transmit calls pass in kernel virtual addresses and block copying this into
 * the socket's tx buffers via a usual blocking sendmsg.  They'll block waiting
 * for a failed socket to timeout.  TX callers can also pass in a poniter to an
 * 'int' which gets filled with an errno off the wire in response to the
 * message they send.
 *
 * Handlers for unsolicited messages are registered.  Each socket has a page
 * that incoming data is copied into.  First the header, then the data.
 * Handlers are called from only one thread with a reference to this per-socket
 * page.  This page is destroyed after the handler call, so it can't be
 * referenced beyond the call.  Handlers may block but are discouraged from
 * doing so.
 *
 * Any framing errors (bad magic, large payload lengths) close a connection.
 *
 * Our sock_container holds the state we associate with a socket.  It's current
 * framing state is held there as well as the refcounting we do around when it
 * is safe to tear down the socket.  The socket is only finally torn down from
 * the container when the container loses all of its references -- so as long
 * as you hold a ref on the container you can trust that the socket is valid
 * for use with kernel socket APIs.
 *
 * Connections are initiated between a pair of nodes when the node with the
 * higher node number gets a heartbeat callback which indicates that the lower
 * numbered node has started heartbeating.  The lower numbered node is passive
 * and only accepts the connection if the higher numbered node is heartbeating.
 */

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/net.h>
#include <linux/export.h>
#include <linux/uaccess.h>
#include <net/tcp.h>


#include "heartbeat.h"
#include "tcp.h"
#include "nodemanager.h"
#define MLOG_MASK_PREFIX ML_TCP
#include "masklog.h"

#include "tcp_internal.h"

#define SC_NODEF_FMT "node %s (num %u) at %pI4:%u"

/*
 * In the following two log macros, the whitespace after the ',' just
 * before ##args is intentional. Otherwise, gcc 2.95 will eat the
 * previous token if args expands to nothing.
 */
#define msglog(hdr, fmt, args...) do {					\
	typeof(hdr) __hdr = (hdr);					\
	mlog(ML_MSG, "[mag %u len %u typ %u stat %d sys_stat %d "	\
	     "key %08x num %u] " fmt,					\
	be16_to_cpu(__hdr->magic), be16_to_cpu(__hdr->data_len),	\
	     be16_to_cpu(__hdr->msg_type), be32_to_cpu(__hdr->status),	\
	     be32_to_cpu(__hdr->sys_status), be32_to_cpu(__hdr->key),	\
	     be32_to_cpu(__hdr->msg_num) ,  ##args);			\
} while (0)

#define sclog(sc, fmt, args...) do {					\
	typeof(sc) __sc = (sc);						\
	mlog(ML_SOCKET, "[sc %p refs %d sock %p node %u page %p "	\
	     "pg_off %zu] " fmt, __sc,					\
	     atomic_read(&__sc->sc_kref.refcount), __sc->sc_sock,	\
	    __sc->sc_node->nd_num, __sc->sc_page, __sc->sc_page_off ,	\
	    ##args);							\
} while (0)

static DEFINE_RWLOCK(r2net_handler_lock);
static struct rb_root r2net_handler_tree = RB_ROOT;

static struct r2net_node r2net_nodes[R2NM_MAX_NODES];

/* XXX someday we'll need better accounting */
static struct socket *r2net_listen_sock;

/*
 * listen work is only queued by the listening socket callbacks on the
 * r2net_wq.  teardown detaches the callbacks before destroying the workqueue.
 * quorum work is queued as sock containers are shutdown.. stop_listening
 * tears down all the node's sock containers, preventing future shutdowns
 * and queued quorum work, before canceling delayed quorum work and
 * destroying the work queue.
 */
static struct workqueue_struct *r2net_wq;
static struct work_struct r2net_listen_work;

static struct r2hb_callback_func r2net_hb_up, r2net_hb_down;
#define R2NET_HB_PRI 0x1

static struct r2net_handshake *r2net_hand;
static struct r2net_msg *r2net_keep_req, *r2net_keep_resp;

static int r2net_sys_err_translations[R2NET_ERR_MAX] = {
		[R2NET_ERR_NONE]	= 0,
		[R2NET_ERR_NO_HNDLR]	= -ENOPROTOOPT,
		[R2NET_ERR_OVERFLOW]	= -EOVERFLOW,
		[R2NET_ERR_DIED]	= -EHOSTDOWN,};

/* can't quite avoid *all* internal declarations :/ */
static void r2net_sc_connect_completed(struct work_struct *work);
static void r2net_rx_until_empty(struct work_struct *work);
static void r2net_shutdown_sc(struct work_struct *work);
static void r2net_listen_data_ready(struct sock *sk, int bytes);
static void r2net_sc_send_keep_req(struct work_struct *work);
static void r2net_idle_timer(unsigned long data);
static void r2net_sc_postpone_idle(struct r2net_sock_container *sc);
static void r2net_sc_reset_idle_timer(struct r2net_sock_container *sc);

#ifdef CONFIG_DEBUG_FS
static void r2net_init_nst(struct r2net_send_tracking *nst, u32 msgtype,
			   u32 msgkey, struct task_struct *task, u8 node)
{
	INIT_LIST_HEAD(&nst->st_net_debug_item);
	nst->st_task = task;
	nst->st_msg_type = msgtype;
	nst->st_msg_key = msgkey;
	nst->st_node = node;
}

static inline void r2net_set_nst_sock_time(struct r2net_send_tracking *nst)
{
	nst->st_sock_time = ktime_get();
}

static inline void r2net_set_nst_send_time(struct r2net_send_tracking *nst)
{
	nst->st_send_time = ktime_get();
}

static inline void r2net_set_nst_status_time(struct r2net_send_tracking *nst)
{
	nst->st_status_time = ktime_get();
}

static inline void r2net_set_nst_sock_container(struct r2net_send_tracking *nst,
						struct r2net_sock_container *sc)
{
	nst->st_sc = sc;
}

static inline void r2net_set_nst_msg_id(struct r2net_send_tracking *nst,
					u32 msg_id)
{
	nst->st_id = msg_id;
}

static inline void r2net_set_sock_timer(struct r2net_sock_container *sc)
{
	sc->sc_tv_timer = ktime_get();
}

static inline void r2net_set_data_ready_time(struct r2net_sock_container *sc)
{
	sc->sc_tv_data_ready = ktime_get();
}

static inline void r2net_set_advance_start_time(struct r2net_sock_container *sc)
{
	sc->sc_tv_advance_start = ktime_get();
}

static inline void r2net_set_advance_stop_time(struct r2net_sock_container *sc)
{
	sc->sc_tv_advance_stop = ktime_get();
}

static inline void r2net_set_func_start_time(struct r2net_sock_container *sc)
{
	sc->sc_tv_func_start = ktime_get();
}

static inline void r2net_set_func_stop_time(struct r2net_sock_container *sc)
{
	sc->sc_tv_func_stop = ktime_get();
}

#else  /* CONFIG_DEBUG_FS */
# define r2net_init_nst(a, b, c, d, e)
# define r2net_set_nst_sock_time(a)
# define r2net_set_nst_send_time(a)
# define r2net_set_nst_status_time(a)
# define r2net_set_nst_sock_container(a, b)
# define r2net_set_nst_msg_id(a, b)
# define r2net_set_sock_timer(a)
# define r2net_set_data_ready_time(a)
# define r2net_set_advance_start_time(a)
# define r2net_set_advance_stop_time(a)
# define r2net_set_func_start_time(a)
# define r2net_set_func_stop_time(a)
#endif /* CONFIG_DEBUG_FS */

#ifdef CONFIG_RAMSTER_FS_STATS
static ktime_t r2net_get_func_run_time(struct r2net_sock_container *sc)
{
	return ktime_sub(sc->sc_tv_func_stop, sc->sc_tv_func_start);
}

static void r2net_update_send_stats(struct r2net_send_tracking *nst,
				    struct r2net_sock_container *sc)
{
	sc->sc_tv_status_total = ktime_add(sc->sc_tv_status_total,
					   ktime_sub(ktime_get(),
						     nst->st_status_time));
	sc->sc_tv_send_total = ktime_add(sc->sc_tv_send_total,
					 ktime_sub(nst->st_status_time,
						   nst->st_send_time));
	sc->sc_tv_acquiry_total = ktime_add(sc->sc_tv_acquiry_total,
					    ktime_sub(nst->st_send_time,
						      nst->st_sock_time));
	sc->sc_send_count++;
}

static void r2net_update_recv_stats(struct r2net_sock_container *sc)
{
	sc->sc_tv_process_total = ktime_add(sc->sc_tv_process_total,
					    r2net_get_func_run_time(sc));
	sc->sc_recv_count++;
}

#else

# define r2net_update_send_stats(a, b)

# define r2net_update_recv_stats(sc)

#endif /* CONFIG_RAMSTER_FS_STATS */

static inline int r2net_reconnect_delay(void)
{
	return r2nm_single_cluster->cl_reconnect_delay_ms;
}

static inline int r2net_keepalive_delay(void)
{
	return r2nm_single_cluster->cl_keepalive_delay_ms;
}

static inline int r2net_idle_timeout(void)
{
	return r2nm_single_cluster->cl_idle_timeout_ms;
}

static inline int r2net_sys_err_to_errno(enum r2net_system_error err)
{
	int trans;
	BUG_ON(err >= R2NET_ERR_MAX);
	trans = r2net_sys_err_translations[err];

	/* Just in case we mess up the translation table above */
	BUG_ON(err != R2NET_ERR_NONE && trans == 0);
	return trans;
}

struct r2net_node *r2net_nn_from_num(u8 node_num)
{
	BUG_ON(node_num >= ARRAY_SIZE(r2net_nodes));
	return &r2net_nodes[node_num];
}

static u8 r2net_num_from_nn(struct r2net_node *nn)
{
	BUG_ON(nn == NULL);
	return nn - r2net_nodes;
}

/* ------------------------------------------------------------ */

static int r2net_prep_nsw(struct r2net_node *nn, struct r2net_status_wait *nsw)
{
	int ret;

	spin_lock(&nn->nn_lock);
	ret = idr_alloc(&nn->nn_status_idr, nsw, 0, 0, GFP_ATOMIC);
	if (ret >= 0) {
		nsw->ns_id = ret;
		list_add_tail(&nsw->ns_node_item, &nn->nn_status_list);
	}
	spin_unlock(&nn->nn_lock);

	if (ret >= 0) {
		init_waitqueue_head(&nsw->ns_wq);
		nsw->ns_sys_status = R2NET_ERR_NONE;
		nsw->ns_status = 0;
		return 0;
	}
	return ret;
}

static void r2net_complete_nsw_locked(struct r2net_node *nn,
				      struct r2net_status_wait *nsw,
				      enum r2net_system_error sys_status,
				      s32 status)
{
	assert_spin_locked(&nn->nn_lock);

	if (!list_empty(&nsw->ns_node_item)) {
		list_del_init(&nsw->ns_node_item);
		nsw->ns_sys_status = sys_status;
		nsw->ns_status = status;
		idr_remove(&nn->nn_status_idr, nsw->ns_id);
		wake_up(&nsw->ns_wq);
	}
}

static void r2net_complete_nsw(struct r2net_node *nn,
			       struct r2net_status_wait *nsw,
			       u64 id, enum r2net_system_error sys_status,
			       s32 status)
{
	spin_lock(&nn->nn_lock);
	if (nsw == NULL) {
		if (id > INT_MAX)
			goto out;

		nsw = idr_find(&nn->nn_status_idr, id);
		if (nsw == NULL)
			goto out;
	}

	r2net_complete_nsw_locked(nn, nsw, sys_status, status);

out:
	spin_unlock(&nn->nn_lock);
	return;
}

static void r2net_complete_nodes_nsw(struct r2net_node *nn)
{
	struct r2net_status_wait *nsw, *tmp;
	unsigned int num_kills = 0;

	assert_spin_locked(&nn->nn_lock);

	list_for_each_entry_safe(nsw, tmp, &nn->nn_status_list, ns_node_item) {
		r2net_complete_nsw_locked(nn, nsw, R2NET_ERR_DIED, 0);
		num_kills++;
	}

	mlog(0, "completed %d messages for node %u\n", num_kills,
	     r2net_num_from_nn(nn));
}

static int r2net_nsw_completed(struct r2net_node *nn,
			       struct r2net_status_wait *nsw)
{
	int completed;
	spin_lock(&nn->nn_lock);
	completed = list_empty(&nsw->ns_node_item);
	spin_unlock(&nn->nn_lock);
	return completed;
}

/* ------------------------------------------------------------ */

static void sc_kref_release(struct kref *kref)
{
	struct r2net_sock_container *sc = container_of(kref,
					struct r2net_sock_container, sc_kref);
	BUG_ON(timer_pending(&sc->sc_idle_timeout));

	sclog(sc, "releasing\n");

	if (sc->sc_sock) {
		sock_release(sc->sc_sock);
		sc->sc_sock = NULL;
	}

	r2nm_undepend_item(&sc->sc_node->nd_item);
	r2nm_node_put(sc->sc_node);
	sc->sc_node = NULL;

	r2net_debug_del_sc(sc);
	kfree(sc);
}

static void sc_put(struct r2net_sock_container *sc)
{
	sclog(sc, "put\n");
	kref_put(&sc->sc_kref, sc_kref_release);
}
static void sc_get(struct r2net_sock_container *sc)
{
	sclog(sc, "get\n");
	kref_get(&sc->sc_kref);
}
static struct r2net_sock_container *sc_alloc(struct r2nm_node *node)
{
	struct r2net_sock_container *sc, *ret = NULL;
	struct page *page = NULL;
	int status = 0;

	page = alloc_page(GFP_NOFS);
	sc = kzalloc(sizeof(*sc), GFP_NOFS);
	if (sc == NULL || page == NULL)
		goto out;

	kref_init(&sc->sc_kref);
	r2nm_node_get(node);
	sc->sc_node = node;

	/* pin the node item of the remote node */
	status = r2nm_depend_item(&node->nd_item);
	if (status) {
		mlog_errno(status);
		r2nm_node_put(node);
		goto out;
	}
	INIT_WORK(&sc->sc_connect_work, r2net_sc_connect_completed);
	INIT_WORK(&sc->sc_rx_work, r2net_rx_until_empty);
	INIT_WORK(&sc->sc_shutdown_work, r2net_shutdown_sc);
	INIT_DELAYED_WORK(&sc->sc_keepalive_work, r2net_sc_send_keep_req);

	init_timer(&sc->sc_idle_timeout);
	sc->sc_idle_timeout.function = r2net_idle_timer;
	sc->sc_idle_timeout.data = (unsigned long)sc;

	sclog(sc, "alloced\n");

	ret = sc;
	sc->sc_page = page;
	r2net_debug_add_sc(sc);
	sc = NULL;
	page = NULL;

out:
	if (page)
		__free_page(page);
	kfree(sc);

	return ret;
}

/* ------------------------------------------------------------ */

static void r2net_sc_queue_work(struct r2net_sock_container *sc,
				struct work_struct *work)
{
	sc_get(sc);
	if (!queue_work(r2net_wq, work))
		sc_put(sc);
}
static void r2net_sc_queue_delayed_work(struct r2net_sock_container *sc,
					struct delayed_work *work,
					int delay)
{
	sc_get(sc);
	if (!queue_delayed_work(r2net_wq, work, delay))
		sc_put(sc);
}
static void r2net_sc_cancel_delayed_work(struct r2net_sock_container *sc,
					 struct delayed_work *work)
{
	if (cancel_delayed_work(work))
		sc_put(sc);
}

static atomic_t r2net_connected_peers = ATOMIC_INIT(0);

int r2net_num_connected_peers(void)
{
	return atomic_read(&r2net_connected_peers);
}

static void r2net_set_nn_state(struct r2net_node *nn,
			       struct r2net_sock_container *sc,
			       unsigned valid, int err)
{
	int was_valid = nn->nn_sc_valid;
	int was_err = nn->nn_persistent_error;
	struct r2net_sock_container *old_sc = nn->nn_sc;

	assert_spin_locked(&nn->nn_lock);

	if (old_sc && !sc)
		atomic_dec(&r2net_connected_peers);
	else if (!old_sc && sc)
		atomic_inc(&r2net_connected_peers);

	/* the node num comparison and single connect/accept path should stop
	 * an non-null sc from being overwritten with another */
	BUG_ON(sc && nn->nn_sc && nn->nn_sc != sc);
	mlog_bug_on_msg(err && valid, "err %d valid %u\n", err, valid);
	mlog_bug_on_msg(valid && !sc, "valid %u sc %p\n", valid, sc);

	if (was_valid && !valid && err == 0)
		err = -ENOTCONN;

	mlog(ML_CONN, "node %u sc: %p -> %p, valid %u -> %u, err %d -> %d\n",
	     r2net_num_from_nn(nn), nn->nn_sc, sc, nn->nn_sc_valid, valid,
	     nn->nn_persistent_error, err);

	nn->nn_sc = sc;
	nn->nn_sc_valid = valid ? 1 : 0;
	nn->nn_persistent_error = err;

	/* mirrors r2net_tx_can_proceed() */
	if (nn->nn_persistent_error || nn->nn_sc_valid)
		wake_up(&nn->nn_sc_wq);

	if (!was_err && nn->nn_persistent_error) {
		queue_delayed_work(r2net_wq, &nn->nn_still_up,
				   msecs_to_jiffies(R2NET_QUORUM_DELAY_MS));
	}

	if (was_valid && !valid) {
		pr_notice("ramster: No longer connected to " SC_NODEF_FMT "\n",
			old_sc->sc_node->nd_name, old_sc->sc_node->nd_num,
			&old_sc->sc_node->nd_ipv4_address,
			ntohs(old_sc->sc_node->nd_ipv4_port));
		r2net_complete_nodes_nsw(nn);
	}

	if (!was_valid && valid) {
		cancel_delayed_work(&nn->nn_connect_expired);
		pr_notice("ramster: %s " SC_NODEF_FMT "\n",
		       r2nm_this_node() > sc->sc_node->nd_num ?
		       "Connected to" : "Accepted connection from",
		       sc->sc_node->nd_name, sc->sc_node->nd_num,
			&sc->sc_node->nd_ipv4_address,
			ntohs(sc->sc_node->nd_ipv4_port));
	}

	/* trigger the connecting worker func as long as we're not valid,
	 * it will back off if it shouldn't connect.  This can be called
	 * from node config teardown and so needs to be careful about
	 * the work queue actually being up. */
	if (!valid && r2net_wq) {
		unsigned long delay;
		/* delay if we're within a RECONNECT_DELAY of the
		 * last attempt */
		delay = (nn->nn_last_connect_attempt +
			 msecs_to_jiffies(r2net_reconnect_delay()))
			- jiffies;
		if (delay > msecs_to_jiffies(r2net_reconnect_delay()))
			delay = 0;
		mlog(ML_CONN, "queueing conn attempt in %lu jiffies\n", delay);
		queue_delayed_work(r2net_wq, &nn->nn_connect_work, delay);

		/*
		 * Delay the expired work after idle timeout.
		 *
		 * We might have lots of failed connection attempts that run
		 * through here but we only cancel the connect_expired work when
		 * a connection attempt succeeds.  So only the first enqueue of
		 * the connect_expired work will do anything.  The rest will see
		 * that it's already queued and do nothing.
		 */
		delay += msecs_to_jiffies(r2net_idle_timeout());
		queue_delayed_work(r2net_wq, &nn->nn_connect_expired, delay);
	}

	/* keep track of the nn's sc ref for the caller */
	if ((old_sc == NULL) && sc)
		sc_get(sc);
	if (old_sc && (old_sc != sc)) {
		r2net_sc_queue_work(old_sc, &old_sc->sc_shutdown_work);
		sc_put(old_sc);
	}
}

/* see r2net_register_callbacks() */
static void r2net_data_ready(struct sock *sk, int bytes)
{
	void (*ready)(struct sock *sk, int bytes);

	read_lock(&sk->sk_callback_lock);
	if (sk->sk_user_data) {
		struct r2net_sock_container *sc = sk->sk_user_data;
		sclog(sc, "data_ready hit\n");
		r2net_set_data_ready_time(sc);
		r2net_sc_queue_work(sc, &sc->sc_rx_work);
		ready = sc->sc_data_ready;
	} else {
		ready = sk->sk_data_ready;
	}
	read_unlock(&sk->sk_callback_lock);

	ready(sk, bytes);
}

/* see r2net_register_callbacks() */
static void r2net_state_change(struct sock *sk)
{
	void (*state_change)(struct sock *sk);
	struct r2net_sock_container *sc;

	read_lock(&sk->sk_callback_lock);
	sc = sk->sk_user_data;
	if (sc == NULL) {
		state_change = sk->sk_state_change;
		goto out;
	}

	sclog(sc, "state_change to %d\n", sk->sk_state);

	state_change = sc->sc_state_change;

	switch (sk->sk_state) {

	/* ignore connecting sockets as they make progress */
	case TCP_SYN_SENT:
	case TCP_SYN_RECV:
		break;
	case TCP_ESTABLISHED:
		r2net_sc_queue_work(sc, &sc->sc_connect_work);
		break;
	default:
		pr_info("ramster: Connection to "
			SC_NODEF_FMT " shutdown, state %d\n",
			sc->sc_node->nd_name, sc->sc_node->nd_num,
			&sc->sc_node->nd_ipv4_address,
			ntohs(sc->sc_node->nd_ipv4_port), sk->sk_state);
		r2net_sc_queue_work(sc, &sc->sc_shutdown_work);
		break;

	}
out:
	read_unlock(&sk->sk_callback_lock);
	state_change(sk);
}

/*
 * we register callbacks so we can queue work on events before calling
 * the original callbacks.  our callbacks are careful to test user_data
 * to discover when they've reaced with r2net_unregister_callbacks().
 */
static void r2net_register_callbacks(struct sock *sk,
				     struct r2net_sock_container *sc)
{
	write_lock_bh(&sk->sk_callback_lock);

	/* accepted sockets inherit the old listen socket data ready */
	if (sk->sk_data_ready == r2net_listen_data_ready) {
		sk->sk_data_ready = sk->sk_user_data;
		sk->sk_user_data = NULL;
	}

	BUG_ON(sk->sk_user_data != NULL);
	sk->sk_user_data = sc;
	sc_get(sc);

	sc->sc_data_ready = sk->sk_data_ready;
	sc->sc_state_change = sk->sk_state_change;
	sk->sk_data_ready = r2net_data_ready;
	sk->sk_state_change = r2net_state_change;

	mutex_init(&sc->sc_send_lock);

	write_unlock_bh(&sk->sk_callback_lock);
}

static int r2net_unregister_callbacks(struct sock *sk,
					struct r2net_sock_container *sc)
{
	int ret = 0;

	write_lock_bh(&sk->sk_callback_lock);
	if (sk->sk_user_data == sc) {
		ret = 1;
		sk->sk_user_data = NULL;
		sk->sk_data_ready = sc->sc_data_ready;
		sk->sk_state_change = sc->sc_state_change;
	}
	write_unlock_bh(&sk->sk_callback_lock);

	return ret;
}

/*
 * this is a little helper that is called by callers who have seen a problem
 * with an sc and want to detach it from the nn if someone already hasn't beat
 * them to it.  if an error is given then the shutdown will be persistent
 * and pending transmits will be canceled.
 */
static void r2net_ensure_shutdown(struct r2net_node *nn,
					struct r2net_sock_container *sc,
				   int err)
{
	spin_lock(&nn->nn_lock);
	if (nn->nn_sc == sc)
		r2net_set_nn_state(nn, NULL, 0, err);
	spin_unlock(&nn->nn_lock);
}

/*
 * This work queue function performs the blocking parts of socket shutdown.  A
 * few paths lead here.  set_nn_state will trigger this callback if it sees an
 * sc detached from the nn.  state_change will also trigger this callback
 * directly when it sees errors.  In that case we need to call set_nn_state
 * ourselves as state_change couldn't get the nn_lock and call set_nn_state
 * itself.
 */
static void r2net_shutdown_sc(struct work_struct *work)
{
	struct r2net_sock_container *sc =
		container_of(work, struct r2net_sock_container,
			     sc_shutdown_work);
	struct r2net_node *nn = r2net_nn_from_num(sc->sc_node->nd_num);

	sclog(sc, "shutting down\n");

	/* drop the callbacks ref and call shutdown only once */
	if (r2net_unregister_callbacks(sc->sc_sock->sk, sc)) {
		/* we shouldn't flush as we're in the thread, the
		 * races with pending sc work structs are harmless */
		del_timer_sync(&sc->sc_idle_timeout);
		r2net_sc_cancel_delayed_work(sc, &sc->sc_keepalive_work);
		sc_put(sc);
		kernel_sock_shutdown(sc->sc_sock, SHUT_RDWR);
	}

	/* not fatal so failed connects before the other guy has our
	 * heartbeat can be retried */
	r2net_ensure_shutdown(nn, sc, 0);
	sc_put(sc);
}

/* ------------------------------------------------------------ */

static int r2net_handler_cmp(struct r2net_msg_handler *nmh, u32 msg_type,
			     u32 key)
{
	int ret = memcmp(&nmh->nh_key, &key, sizeof(key));

	if (ret == 0)
		ret = memcmp(&nmh->nh_msg_type, &msg_type, sizeof(msg_type));

	return ret;
}

static struct r2net_msg_handler *
r2net_handler_tree_lookup(u32 msg_type, u32 key, struct rb_node ***ret_p,
				struct rb_node **ret_parent)
{
	struct rb_node **p = &r2net_handler_tree.rb_node;
	struct rb_node *parent = NULL;
	struct r2net_msg_handler *nmh, *ret = NULL;
	int cmp;

	while (*p) {
		parent = *p;
		nmh = rb_entry(parent, struct r2net_msg_handler, nh_node);
		cmp = r2net_handler_cmp(nmh, msg_type, key);

		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else {
			ret = nmh;
			break;
		}
	}

	if (ret_p != NULL)
		*ret_p = p;
	if (ret_parent != NULL)
		*ret_parent = parent;

	return ret;
}

static void r2net_handler_kref_release(struct kref *kref)
{
	struct r2net_msg_handler *nmh;
	nmh = container_of(kref, struct r2net_msg_handler, nh_kref);

	kfree(nmh);
}

static void r2net_handler_put(struct r2net_msg_handler *nmh)
{
	kref_put(&nmh->nh_kref, r2net_handler_kref_release);
}

/* max_len is protection for the handler func.  incoming messages won't
 * be given to the handler if their payload is longer than the max. */
int r2net_register_handler(u32 msg_type, u32 key, u32 max_len,
			   r2net_msg_handler_func *func, void *data,
			   r2net_post_msg_handler_func *post_func,
			   struct list_head *unreg_list)
{
	struct r2net_msg_handler *nmh = NULL;
	struct rb_node **p, *parent;
	int ret = 0;

	if (max_len > R2NET_MAX_PAYLOAD_BYTES) {
		mlog(0, "max_len for message handler out of range: %u\n",
			max_len);
		ret = -EINVAL;
		goto out;
	}

	if (!msg_type) {
		mlog(0, "no message type provided: %u, %p\n", msg_type, func);
		ret = -EINVAL;
		goto out;

	}
	if (!func) {
		mlog(0, "no message handler provided: %u, %p\n",
		       msg_type, func);
		ret = -EINVAL;
		goto out;
	}

	nmh = kzalloc(sizeof(struct r2net_msg_handler), GFP_NOFS);
	if (nmh == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	nmh->nh_func = func;
	nmh->nh_func_data = data;
	nmh->nh_post_func = post_func;
	nmh->nh_msg_type = msg_type;
	nmh->nh_max_len = max_len;
	nmh->nh_key = key;
	/* the tree and list get this ref.. they're both removed in
	 * unregister when this ref is dropped */
	kref_init(&nmh->nh_kref);
	INIT_LIST_HEAD(&nmh->nh_unregister_item);

	write_lock(&r2net_handler_lock);
	if (r2net_handler_tree_lookup(msg_type, key, &p, &parent))
		ret = -EEXIST;
	else {
		rb_link_node(&nmh->nh_node, parent, p);
		rb_insert_color(&nmh->nh_node, &r2net_handler_tree);
		list_add_tail(&nmh->nh_unregister_item, unreg_list);

		mlog(ML_TCP, "registered handler func %p type %u key %08x\n",
		     func, msg_type, key);
		/* we've had some trouble with handlers seemingly vanishing. */
		mlog_bug_on_msg(r2net_handler_tree_lookup(msg_type, key, &p,
							  &parent) == NULL,
				"couldn't find handler we *just* registered "
				"for type %u key %08x\n", msg_type, key);
	}
	write_unlock(&r2net_handler_lock);
	if (ret)
		goto out;

out:
	if (ret)
		kfree(nmh);

	return ret;
}
EXPORT_SYMBOL_GPL(r2net_register_handler);

void r2net_unregister_handler_list(struct list_head *list)
{
	struct r2net_msg_handler *nmh, *n;

	write_lock(&r2net_handler_lock);
	list_for_each_entry_safe(nmh, n, list, nh_unregister_item) {
		mlog(ML_TCP, "unregistering handler func %p type %u key %08x\n",
		     nmh->nh_func, nmh->nh_msg_type, nmh->nh_key);
		rb_erase(&nmh->nh_node, &r2net_handler_tree);
		list_del_init(&nmh->nh_unregister_item);
		kref_put(&nmh->nh_kref, r2net_handler_kref_release);
	}
	write_unlock(&r2net_handler_lock);
}
EXPORT_SYMBOL_GPL(r2net_unregister_handler_list);

static struct r2net_msg_handler *r2net_handler_get(u32 msg_type, u32 key)
{
	struct r2net_msg_handler *nmh;

	read_lock(&r2net_handler_lock);
	nmh = r2net_handler_tree_lookup(msg_type, key, NULL, NULL);
	if (nmh)
		kref_get(&nmh->nh_kref);
	read_unlock(&r2net_handler_lock);

	return nmh;
}

/* ------------------------------------------------------------ */

static int r2net_recv_tcp_msg(struct socket *sock, void *data, size_t len)
{
	int ret;
	mm_segment_t oldfs;
	struct kvec vec = {
		.iov_len = len,
		.iov_base = data,
	};
	struct msghdr msg = {
		.msg_iovlen = 1,
		.msg_iov = (struct iovec *)&vec,
		.msg_flags = MSG_DONTWAIT,
	};

	oldfs = get_fs();
	set_fs(get_ds());
	ret = sock_recvmsg(sock, &msg, len, msg.msg_flags);
	set_fs(oldfs);

	return ret;
}

static int r2net_send_tcp_msg(struct socket *sock, struct kvec *vec,
			      size_t veclen, size_t total)
{
	int ret;
	mm_segment_t oldfs;
	struct msghdr msg = {
		.msg_iov = (struct iovec *)vec,
		.msg_iovlen = veclen,
	};

	if (sock == NULL) {
		ret = -EINVAL;
		goto out;
	}

	oldfs = get_fs();
	set_fs(get_ds());
	ret = sock_sendmsg(sock, &msg, total);
	set_fs(oldfs);
	if (ret != total) {
		mlog(ML_ERROR, "sendmsg returned %d instead of %zu\n", ret,
		     total);
		if (ret >= 0)
			ret = -EPIPE; /* should be smarter, I bet */
		goto out;
	}

	ret = 0;
out:
	if (ret < 0)
		mlog(0, "returning error: %d\n", ret);
	return ret;
}

static void r2net_sendpage(struct r2net_sock_container *sc,
			   void *kmalloced_virt,
			   size_t size)
{
	struct r2net_node *nn = r2net_nn_from_num(sc->sc_node->nd_num);
	ssize_t ret;

	while (1) {
		mutex_lock(&sc->sc_send_lock);
		ret = sc->sc_sock->ops->sendpage(sc->sc_sock,
					virt_to_page(kmalloced_virt),
					(long)kmalloced_virt & ~PAGE_MASK,
					size, MSG_DONTWAIT);
		mutex_unlock(&sc->sc_send_lock);
		if (ret == size)
			break;
		if (ret == (ssize_t)-EAGAIN) {
			mlog(0, "sendpage of size %zu to " SC_NODEF_FMT
			     " returned EAGAIN\n", size, sc->sc_node->nd_name,
				sc->sc_node->nd_num,
				&sc->sc_node->nd_ipv4_address,
				ntohs(sc->sc_node->nd_ipv4_port));
			cond_resched();
			continue;
		}
		mlog(ML_ERROR, "sendpage of size %zu to " SC_NODEF_FMT
		     " failed with %zd\n", size, sc->sc_node->nd_name,
			sc->sc_node->nd_num, &sc->sc_node->nd_ipv4_address,
			ntohs(sc->sc_node->nd_ipv4_port), ret);
		r2net_ensure_shutdown(nn, sc, 0);
		break;
	}
}

static void r2net_init_msg(struct r2net_msg *msg, u16 data_len,
				u16 msg_type, u32 key)
{
	memset(msg, 0, sizeof(struct r2net_msg));
	msg->magic = cpu_to_be16(R2NET_MSG_MAGIC);
	msg->data_len = cpu_to_be16(data_len);
	msg->msg_type = cpu_to_be16(msg_type);
	msg->sys_status = cpu_to_be32(R2NET_ERR_NONE);
	msg->status = 0;
	msg->key = cpu_to_be32(key);
}

static int r2net_tx_can_proceed(struct r2net_node *nn,
				struct r2net_sock_container **sc_ret,
				int *error)
{
	int ret = 0;

	spin_lock(&nn->nn_lock);
	if (nn->nn_persistent_error) {
		ret = 1;
		*sc_ret = NULL;
		*error = nn->nn_persistent_error;
	} else if (nn->nn_sc_valid) {
		kref_get(&nn->nn_sc->sc_kref);

		ret = 1;
		*sc_ret = nn->nn_sc;
		*error = 0;
	}
	spin_unlock(&nn->nn_lock);

	return ret;
}

/* Get a map of all nodes to which this node is currently connected to */
void r2net_fill_node_map(unsigned long *map, unsigned bytes)
{
	struct r2net_sock_container *sc;
	int node, ret;

	BUG_ON(bytes < (BITS_TO_LONGS(R2NM_MAX_NODES) * sizeof(unsigned long)));

	memset(map, 0, bytes);
	for (node = 0; node < R2NM_MAX_NODES; ++node) {
		r2net_tx_can_proceed(r2net_nn_from_num(node), &sc, &ret);
		if (!ret) {
			set_bit(node, map);
			sc_put(sc);
		}
	}
}
EXPORT_SYMBOL_GPL(r2net_fill_node_map);

int r2net_send_message_vec(u32 msg_type, u32 key, struct kvec *caller_vec,
			   size_t caller_veclen, u8 target_node, int *status)
{
	int ret = 0;
	struct r2net_msg *msg = NULL;
	size_t veclen, caller_bytes = 0;
	struct kvec *vec = NULL;
	struct r2net_sock_container *sc = NULL;
	struct r2net_node *nn = r2net_nn_from_num(target_node);
	struct r2net_status_wait nsw = {
		.ns_node_item = LIST_HEAD_INIT(nsw.ns_node_item),
	};
	struct r2net_send_tracking nst;

	/* this may be a general bug fix */
	init_waitqueue_head(&nsw.ns_wq);

	r2net_init_nst(&nst, msg_type, key, current, target_node);

	if (r2net_wq == NULL) {
		mlog(0, "attempt to tx without r2netd running\n");
		ret = -ESRCH;
		goto out;
	}

	if (caller_veclen == 0) {
		mlog(0, "bad kvec array length\n");
		ret = -EINVAL;
		goto out;
	}

	caller_bytes = iov_length((struct iovec *)caller_vec, caller_veclen);
	if (caller_bytes > R2NET_MAX_PAYLOAD_BYTES) {
		mlog(0, "total payload len %zu too large\n", caller_bytes);
		ret = -EINVAL;
		goto out;
	}

	if (target_node == r2nm_this_node()) {
		ret = -ELOOP;
		goto out;
	}

	r2net_debug_add_nst(&nst);

	r2net_set_nst_sock_time(&nst);

	wait_event(nn->nn_sc_wq, r2net_tx_can_proceed(nn, &sc, &ret));
	if (ret)
		goto out;

	r2net_set_nst_sock_container(&nst, sc);

	veclen = caller_veclen + 1;
	vec = kmalloc(sizeof(struct kvec) * veclen, GFP_ATOMIC);
	if (vec == NULL) {
		mlog(0, "failed to %zu element kvec!\n", veclen);
		ret = -ENOMEM;
		goto out;
	}

	msg = kmalloc(sizeof(struct r2net_msg), GFP_ATOMIC);
	if (!msg) {
		mlog(0, "failed to allocate a r2net_msg!\n");
		ret = -ENOMEM;
		goto out;
	}

	r2net_init_msg(msg, caller_bytes, msg_type, key);

	vec[0].iov_len = sizeof(struct r2net_msg);
	vec[0].iov_base = msg;
	memcpy(&vec[1], caller_vec, caller_veclen * sizeof(struct kvec));

	ret = r2net_prep_nsw(nn, &nsw);
	if (ret)
		goto out;

	msg->msg_num = cpu_to_be32(nsw.ns_id);
	r2net_set_nst_msg_id(&nst, nsw.ns_id);

	r2net_set_nst_send_time(&nst);

	/* finally, convert the message header to network byte-order
	 * and send */
	mutex_lock(&sc->sc_send_lock);
	ret = r2net_send_tcp_msg(sc->sc_sock, vec, veclen,
				 sizeof(struct r2net_msg) + caller_bytes);
	mutex_unlock(&sc->sc_send_lock);
	msglog(msg, "sending returned %d\n", ret);
	if (ret < 0) {
		mlog(0, "error returned from r2net_send_tcp_msg=%d\n", ret);
		goto out;
	}

	/* wait on other node's handler */
	r2net_set_nst_status_time(&nst);
	wait_event(nsw.ns_wq, r2net_nsw_completed(nn, &nsw) ||
			nn->nn_persistent_error || !nn->nn_sc_valid);

	r2net_update_send_stats(&nst, sc);

	/* Note that we avoid overwriting the callers status return
	 * variable if a system error was reported on the other
	 * side. Callers beware. */
	ret = r2net_sys_err_to_errno(nsw.ns_sys_status);
	if (status && !ret)
		*status = nsw.ns_status;

	mlog(0, "woken, returning system status %d, user status %d\n",
	     ret, nsw.ns_status);
out:
	r2net_debug_del_nst(&nst); /* must be before dropping sc and node */
	if (sc)
		sc_put(sc);
	kfree(vec);
	kfree(msg);
	r2net_complete_nsw(nn, &nsw, 0, 0, 0);
	return ret;
}
EXPORT_SYMBOL_GPL(r2net_send_message_vec);

int r2net_send_message(u32 msg_type, u32 key, void *data, u32 len,
		       u8 target_node, int *status)
{
	struct kvec vec = {
		.iov_base = data,
		.iov_len = len,
	};
	return r2net_send_message_vec(msg_type, key, &vec, 1,
				      target_node, status);
}
EXPORT_SYMBOL_GPL(r2net_send_message);

static int r2net_send_status_magic(struct socket *sock, struct r2net_msg *hdr,
				   enum r2net_system_error syserr, int err)
{
	struct kvec vec = {
		.iov_base = hdr,
		.iov_len = sizeof(struct r2net_msg),
	};

	BUG_ON(syserr >= R2NET_ERR_MAX);

	/* leave other fields intact from the incoming message, msg_num
	 * in particular */
	hdr->sys_status = cpu_to_be32(syserr);
	hdr->status = cpu_to_be32(err);
	/* twiddle the magic */
	hdr->magic = cpu_to_be16(R2NET_MSG_STATUS_MAGIC);
	hdr->data_len = 0;

	msglog(hdr, "about to send status magic %d\n", err);
	/* hdr has been in host byteorder this whole time */
	return r2net_send_tcp_msg(sock, &vec, 1, sizeof(struct r2net_msg));
}

/*
 * "data magic" is a long version of "status magic" where the message
 * payload actually contains data to be passed in reply to certain messages
 */
static int r2net_send_data_magic(struct r2net_sock_container *sc,
			  struct r2net_msg *hdr,
			  void *data, size_t data_len,
			  enum r2net_system_error syserr, int err)
{
	struct kvec vec[2];
	int ret;

	vec[0].iov_base = hdr;
	vec[0].iov_len = sizeof(struct r2net_msg);
	vec[1].iov_base = data;
	vec[1].iov_len = data_len;

	BUG_ON(syserr >= R2NET_ERR_MAX);

	/* leave other fields intact from the incoming message, msg_num
	 * in particular */
	hdr->sys_status = cpu_to_be32(syserr);
	hdr->status = cpu_to_be32(err);
	hdr->magic = cpu_to_be16(R2NET_MSG_DATA_MAGIC);  /* twiddle magic */
	hdr->data_len = cpu_to_be16(data_len);

	msglog(hdr, "about to send data magic %d\n", err);
	/* hdr has been in host byteorder this whole time */
	ret = r2net_send_tcp_msg(sc->sc_sock, vec, 2,
			sizeof(struct r2net_msg) + data_len);
	return ret;
}

/*
 * called by a message handler to convert an otherwise normal reply
 * message into a "data magic" message
 */
void r2net_force_data_magic(struct r2net_msg *hdr, u16 msgtype, u32 msgkey)
{
	hdr->magic = cpu_to_be16(R2NET_MSG_DATA_MAGIC);
	hdr->msg_type = cpu_to_be16(msgtype);
	hdr->key = cpu_to_be32(msgkey);
}

/* this returns -errno if the header was unknown or too large, etc.
 * after this is called the buffer us reused for the next message */
static int r2net_process_message(struct r2net_sock_container *sc,
				 struct r2net_msg *hdr)
{
	struct r2net_node *nn = r2net_nn_from_num(sc->sc_node->nd_num);
	int ret = 0, handler_status;
	enum  r2net_system_error syserr;
	struct r2net_msg_handler *nmh = NULL;
	void *ret_data = NULL;
	int data_magic = 0;

	msglog(hdr, "processing message\n");

	r2net_sc_postpone_idle(sc);

	switch (be16_to_cpu(hdr->magic)) {

	case R2NET_MSG_STATUS_MAGIC:
		/* special type for returning message status */
		r2net_complete_nsw(nn, NULL, be32_to_cpu(hdr->msg_num),
						be32_to_cpu(hdr->sys_status),
						be32_to_cpu(hdr->status));
		goto out;
	case R2NET_MSG_KEEP_REQ_MAGIC:
		r2net_sendpage(sc, r2net_keep_resp, sizeof(*r2net_keep_resp));
		goto out;
	case R2NET_MSG_KEEP_RESP_MAGIC:
		goto out;
	case R2NET_MSG_MAGIC:
		break;
	case R2NET_MSG_DATA_MAGIC:
		/*
		 * unlike a normal status magic, a data magic DOES
		 * (MUST) have a handler, so the control flow is
		 * a little funky here as a result
		 */
		data_magic = 1;
		break;
	default:
		msglog(hdr, "bad magic\n");
		ret = -EINVAL;
		goto out;
		break;
	}

	/* find a handler for it */
	handler_status = 0;
	nmh = r2net_handler_get(be16_to_cpu(hdr->msg_type),
				be32_to_cpu(hdr->key));
	if (!nmh) {
		mlog(ML_TCP, "couldn't find handler for type %u key %08x\n",
		     be16_to_cpu(hdr->msg_type), be32_to_cpu(hdr->key));
		syserr = R2NET_ERR_NO_HNDLR;
		goto out_respond;
	}

	syserr = R2NET_ERR_NONE;

	if (be16_to_cpu(hdr->data_len) > nmh->nh_max_len)
		syserr = R2NET_ERR_OVERFLOW;

	if (syserr != R2NET_ERR_NONE) {
		pr_err("ramster_r2net, message length problem\n");
		goto out_respond;
	}

	r2net_set_func_start_time(sc);
	sc->sc_msg_key = be32_to_cpu(hdr->key);
	sc->sc_msg_type = be16_to_cpu(hdr->msg_type);
	handler_status = (nmh->nh_func)(hdr, sizeof(struct r2net_msg) +
					     be16_to_cpu(hdr->data_len),
					nmh->nh_func_data, &ret_data);
	if (data_magic) {
		/*
		 * handler handled data sent in reply to request
		 * so complete the transaction
		 */
		r2net_complete_nsw(nn, NULL, be32_to_cpu(hdr->msg_num),
			be32_to_cpu(hdr->sys_status), handler_status);
		goto out;
	}
	/*
	 * handler changed magic to DATA_MAGIC to reply to request for data,
	 * implies ret_data points to data to return and handler_status
	 * is the number of bytes of data
	 */
	if (be16_to_cpu(hdr->magic) == R2NET_MSG_DATA_MAGIC) {
		ret = r2net_send_data_magic(sc, hdr,
						ret_data, handler_status,
						syserr, 0);
		hdr = NULL;
		mlog(0, "sending data reply %d, syserr %d returned %d\n",
			handler_status, syserr, ret);
		r2net_set_func_stop_time(sc);

		r2net_update_recv_stats(sc);
		goto out;
	}
	r2net_set_func_stop_time(sc);

	r2net_update_recv_stats(sc);

out_respond:
	/* this destroys the hdr, so don't use it after this */
	mutex_lock(&sc->sc_send_lock);
	ret = r2net_send_status_magic(sc->sc_sock, hdr, syserr,
				      handler_status);
	mutex_unlock(&sc->sc_send_lock);
	hdr = NULL;
	mlog(0, "sending handler status %d, syserr %d returned %d\n",
	     handler_status, syserr, ret);

	if (nmh) {
		BUG_ON(ret_data != NULL && nmh->nh_post_func == NULL);
		if (nmh->nh_post_func)
			(nmh->nh_post_func)(handler_status, nmh->nh_func_data,
					    ret_data);
	}

out:
	if (nmh)
		r2net_handler_put(nmh);
	return ret;
}

static int r2net_check_handshake(struct r2net_sock_container *sc)
{
	struct r2net_handshake *hand = page_address(sc->sc_page);
	struct r2net_node *nn = r2net_nn_from_num(sc->sc_node->nd_num);

	if (hand->protocol_version != cpu_to_be64(R2NET_PROTOCOL_VERSION)) {
		pr_notice("ramster: " SC_NODEF_FMT " Advertised net "
		       "protocol version %llu but %llu is required. "
		       "Disconnecting.\n", sc->sc_node->nd_name,
			sc->sc_node->nd_num, &sc->sc_node->nd_ipv4_address,
			ntohs(sc->sc_node->nd_ipv4_port),
		       (unsigned long long)be64_to_cpu(hand->protocol_version),
		       R2NET_PROTOCOL_VERSION);

		/* don't bother reconnecting if its the wrong version. */
		r2net_ensure_shutdown(nn, sc, -ENOTCONN);
		return -1;
	}

	/*
	 * Ensure timeouts are consistent with other nodes, otherwise
	 * we can end up with one node thinking that the other must be down,
	 * but isn't. This can ultimately cause corruption.
	 */
	if (be32_to_cpu(hand->r2net_idle_timeout_ms) !=
				r2net_idle_timeout()) {
		pr_notice("ramster: " SC_NODEF_FMT " uses a network "
		       "idle timeout of %u ms, but we use %u ms locally. "
		       "Disconnecting.\n", sc->sc_node->nd_name,
			sc->sc_node->nd_num, &sc->sc_node->nd_ipv4_address,
			ntohs(sc->sc_node->nd_ipv4_port),
		       be32_to_cpu(hand->r2net_idle_timeout_ms),
		       r2net_idle_timeout());
		r2net_ensure_shutdown(nn, sc, -ENOTCONN);
		return -1;
	}

	if (be32_to_cpu(hand->r2net_keepalive_delay_ms) !=
			r2net_keepalive_delay()) {
		pr_notice("ramster: " SC_NODEF_FMT " uses a keepalive "
		       "delay of %u ms, but we use %u ms locally. "
		       "Disconnecting.\n", sc->sc_node->nd_name,
			sc->sc_node->nd_num, &sc->sc_node->nd_ipv4_address,
			ntohs(sc->sc_node->nd_ipv4_port),
		       be32_to_cpu(hand->r2net_keepalive_delay_ms),
		       r2net_keepalive_delay());
		r2net_ensure_shutdown(nn, sc, -ENOTCONN);
		return -1;
	}

	if (be32_to_cpu(hand->r2hb_heartbeat_timeout_ms) !=
			R2HB_MAX_WRITE_TIMEOUT_MS) {
		pr_notice("ramster: " SC_NODEF_FMT " uses a heartbeat "
		       "timeout of %u ms, but we use %u ms locally. "
		       "Disconnecting.\n", sc->sc_node->nd_name,
			sc->sc_node->nd_num, &sc->sc_node->nd_ipv4_address,
			ntohs(sc->sc_node->nd_ipv4_port),
		       be32_to_cpu(hand->r2hb_heartbeat_timeout_ms),
		       R2HB_MAX_WRITE_TIMEOUT_MS);
		r2net_ensure_shutdown(nn, sc, -ENOTCONN);
		return -1;
	}

	sc->sc_handshake_ok = 1;

	spin_lock(&nn->nn_lock);
	/* set valid and queue the idle timers only if it hasn't been
	 * shut down already */
	if (nn->nn_sc == sc) {
		r2net_sc_reset_idle_timer(sc);
		atomic_set(&nn->nn_timeout, 0);
		r2net_set_nn_state(nn, sc, 1, 0);
	}
	spin_unlock(&nn->nn_lock);

	/* shift everything up as though it wasn't there */
	sc->sc_page_off -= sizeof(struct r2net_handshake);
	if (sc->sc_page_off)
		memmove(hand, hand + 1, sc->sc_page_off);

	return 0;
}

/* this demuxes the queued rx bytes into header or payload bits and calls
 * handlers as each full message is read off the socket.  it returns -error,
 * == 0 eof, or > 0 for progress made.*/
static int r2net_advance_rx(struct r2net_sock_container *sc)
{
	struct r2net_msg *hdr;
	int ret = 0;
	void *data;
	size_t datalen;

	sclog(sc, "receiving\n");
	r2net_set_advance_start_time(sc);

	if (unlikely(sc->sc_handshake_ok == 0)) {
		if (sc->sc_page_off < sizeof(struct r2net_handshake)) {
			data = page_address(sc->sc_page) + sc->sc_page_off;
			datalen = sizeof(struct r2net_handshake) -
							sc->sc_page_off;
			ret = r2net_recv_tcp_msg(sc->sc_sock, data, datalen);
			if (ret > 0)
				sc->sc_page_off += ret;
		}

		if (sc->sc_page_off == sizeof(struct r2net_handshake)) {
			r2net_check_handshake(sc);
			if (unlikely(sc->sc_handshake_ok == 0))
				ret = -EPROTO;
		}
		goto out;
	}

	/* do we need more header? */
	if (sc->sc_page_off < sizeof(struct r2net_msg)) {
		data = page_address(sc->sc_page) + sc->sc_page_off;
		datalen = sizeof(struct r2net_msg) - sc->sc_page_off;
		ret = r2net_recv_tcp_msg(sc->sc_sock, data, datalen);
		if (ret > 0) {
			sc->sc_page_off += ret;
			/* only swab incoming here.. we can
			 * only get here once as we cross from
			 * being under to over */
			if (sc->sc_page_off == sizeof(struct r2net_msg)) {
				hdr = page_address(sc->sc_page);
				if (be16_to_cpu(hdr->data_len) >
				    R2NET_MAX_PAYLOAD_BYTES)
					ret = -EOVERFLOW;
				WARN_ON_ONCE(ret == -EOVERFLOW);
			}
		}
		if (ret <= 0)
			goto out;
	}

	if (sc->sc_page_off < sizeof(struct r2net_msg)) {
		/* oof, still don't have a header */
		goto out;
	}

	/* this was swabbed above when we first read it */
	hdr = page_address(sc->sc_page);

	msglog(hdr, "at page_off %zu\n", sc->sc_page_off);

	/* do we need more payload? */
	if (sc->sc_page_off - sizeof(struct r2net_msg) <
					be16_to_cpu(hdr->data_len)) {
		/* need more payload */
		data = page_address(sc->sc_page) + sc->sc_page_off;
		datalen = (sizeof(struct r2net_msg) +
				be16_to_cpu(hdr->data_len)) -
				sc->sc_page_off;
		ret = r2net_recv_tcp_msg(sc->sc_sock, data, datalen);
		if (ret > 0)
			sc->sc_page_off += ret;
		if (ret <= 0)
			goto out;
	}

	if (sc->sc_page_off - sizeof(struct r2net_msg) ==
						be16_to_cpu(hdr->data_len)) {
		/* we can only get here once, the first time we read
		 * the payload.. so set ret to progress if the handler
		 * works out. after calling this the message is toast */
		ret = r2net_process_message(sc, hdr);
		if (ret == 0)
			ret = 1;
		sc->sc_page_off = 0;
	}

out:
	sclog(sc, "ret = %d\n", ret);
	r2net_set_advance_stop_time(sc);
	return ret;
}

/* this work func is triggerd by data ready.  it reads until it can read no
 * more.  it interprets 0, eof, as fatal.  if data_ready hits while we're doing
 * our work the work struct will be marked and we'll be called again. */
static void r2net_rx_until_empty(struct work_struct *work)
{
	struct r2net_sock_container *sc =
		container_of(work, struct r2net_sock_container, sc_rx_work);
	int ret;

	do {
		ret = r2net_advance_rx(sc);
	} while (ret > 0);

	if (ret <= 0 && ret != -EAGAIN) {
		struct r2net_node *nn = r2net_nn_from_num(sc->sc_node->nd_num);
		sclog(sc, "saw error %d, closing\n", ret);
		/* not permanent so read failed handshake can retry */
		r2net_ensure_shutdown(nn, sc, 0);
	}
	sc_put(sc);
}

static int r2net_set_nodelay(struct socket *sock)
{
	int ret, val = 1;
	mm_segment_t oldfs;

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	/*
	 * Dear unsuspecting programmer,
	 *
	 * Don't use sock_setsockopt() for SOL_TCP.  It doesn't check its level
	 * argument and assumes SOL_SOCKET so, say, your TCP_NODELAY will
	 * silently turn into SO_DEBUG.
	 *
	 * Yours,
	 * Keeper of hilariously fragile interfaces.
	 */
	ret = sock->ops->setsockopt(sock, SOL_TCP, TCP_NODELAY,
				    (char __user *)&val, sizeof(val));

	set_fs(oldfs);
	return ret;
}

static void r2net_initialize_handshake(void)
{
	r2net_hand->r2hb_heartbeat_timeout_ms = cpu_to_be32(
		R2HB_MAX_WRITE_TIMEOUT_MS);
	r2net_hand->r2net_idle_timeout_ms = cpu_to_be32(r2net_idle_timeout());
	r2net_hand->r2net_keepalive_delay_ms = cpu_to_be32(
		r2net_keepalive_delay());
	r2net_hand->r2net_reconnect_delay_ms = cpu_to_be32(
		r2net_reconnect_delay());
}

/* ------------------------------------------------------------ */

/* called when a connect completes and after a sock is accepted.  the
 * rx path will see the response and mark the sc valid */
static void r2net_sc_connect_completed(struct work_struct *work)
{
	struct r2net_sock_container *sc =
			container_of(work, struct r2net_sock_container,
			     sc_connect_work);

	mlog(ML_MSG, "sc sending handshake with ver %llu id %llx\n",
		(unsigned long long)R2NET_PROTOCOL_VERSION,
		(unsigned long long)be64_to_cpu(r2net_hand->connector_id));

	r2net_initialize_handshake();
	r2net_sendpage(sc, r2net_hand, sizeof(*r2net_hand));
	sc_put(sc);
}

/* this is called as a work_struct func. */
static void r2net_sc_send_keep_req(struct work_struct *work)
{
	struct r2net_sock_container *sc =
		container_of(work, struct r2net_sock_container,
			     sc_keepalive_work.work);

	r2net_sendpage(sc, r2net_keep_req, sizeof(*r2net_keep_req));
	sc_put(sc);
}

/* socket shutdown does a del_timer_sync against this as it tears down.
 * we can't start this timer until we've got to the point in sc buildup
 * where shutdown is going to be involved */
static void r2net_idle_timer(unsigned long data)
{
	struct r2net_sock_container *sc = (struct r2net_sock_container *)data;
	struct r2net_node *nn = r2net_nn_from_num(sc->sc_node->nd_num);
#ifdef CONFIG_DEBUG_FS
	unsigned long msecs = ktime_to_ms(ktime_get()) -
		ktime_to_ms(sc->sc_tv_timer);
#else
	unsigned long msecs = r2net_idle_timeout();
#endif

	pr_notice("ramster: Connection to " SC_NODEF_FMT " has been "
	       "idle for %lu.%lu secs, shutting it down.\n",
		sc->sc_node->nd_name, sc->sc_node->nd_num,
		&sc->sc_node->nd_ipv4_address, ntohs(sc->sc_node->nd_ipv4_port),
	       msecs / 1000, msecs % 1000);

	/*
	 * Initialize the nn_timeout so that the next connection attempt
	 * will continue in r2net_start_connect.
	 */
	atomic_set(&nn->nn_timeout, 1);
	r2net_sc_queue_work(sc, &sc->sc_shutdown_work);
}

static void r2net_sc_reset_idle_timer(struct r2net_sock_container *sc)
{
	r2net_sc_cancel_delayed_work(sc, &sc->sc_keepalive_work);
	r2net_sc_queue_delayed_work(sc, &sc->sc_keepalive_work,
		      msecs_to_jiffies(r2net_keepalive_delay()));
	r2net_set_sock_timer(sc);
	mod_timer(&sc->sc_idle_timeout,
	       jiffies + msecs_to_jiffies(r2net_idle_timeout()));
}

static void r2net_sc_postpone_idle(struct r2net_sock_container *sc)
{
	/* Only push out an existing timer */
	if (timer_pending(&sc->sc_idle_timeout))
		r2net_sc_reset_idle_timer(sc);
}

/* this work func is kicked whenever a path sets the nn state which doesn't
 * have valid set.  This includes seeing hb come up, losing a connection,
 * having a connect attempt fail, etc. This centralizes the logic which decides
 * if a connect attempt should be made or if we should give up and all future
 * transmit attempts should fail */
static void r2net_start_connect(struct work_struct *work)
{
	struct r2net_node *nn =
		container_of(work, struct r2net_node, nn_connect_work.work);
	struct r2net_sock_container *sc = NULL;
	struct r2nm_node *node = NULL, *mynode = NULL;
	struct socket *sock = NULL;
	struct sockaddr_in myaddr = {0, }, remoteaddr = {0, };
	int ret = 0, stop;
	unsigned int timeout;

	/* if we're greater we initiate tx, otherwise we accept */
	if (r2nm_this_node() <= r2net_num_from_nn(nn))
		goto out;

	/* watch for racing with tearing a node down */
	node = r2nm_get_node_by_num(r2net_num_from_nn(nn));
	if (node == NULL) {
		ret = 0;
		goto out;
	}

	mynode = r2nm_get_node_by_num(r2nm_this_node());
	if (mynode == NULL) {
		ret = 0;
		goto out;
	}

	spin_lock(&nn->nn_lock);
	/*
	 * see if we already have one pending or have given up.
	 * For nn_timeout, it is set when we close the connection
	 * because of the idle time out. So it means that we have
	 * at least connected to that node successfully once,
	 * now try to connect to it again.
	 */
	timeout = atomic_read(&nn->nn_timeout);
	stop = (nn->nn_sc ||
		(nn->nn_persistent_error &&
		(nn->nn_persistent_error != -ENOTCONN || timeout == 0)));
	spin_unlock(&nn->nn_lock);
	if (stop)
		goto out;

	nn->nn_last_connect_attempt = jiffies;

	sc = sc_alloc(node);
	if (sc == NULL) {
		mlog(0, "couldn't allocate sc\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0) {
		mlog(0, "can't create socket: %d\n", ret);
		goto out;
	}
	sc->sc_sock = sock; /* freed by sc_kref_release */

	sock->sk->sk_allocation = GFP_ATOMIC;

	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = mynode->nd_ipv4_address;
	myaddr.sin_port = htons(0); /* any port */

	ret = sock->ops->bind(sock, (struct sockaddr *)&myaddr,
			      sizeof(myaddr));
	if (ret) {
		mlog(ML_ERROR, "bind failed with %d at address %pI4\n",
		     ret, &mynode->nd_ipv4_address);
		goto out;
	}

	ret = r2net_set_nodelay(sc->sc_sock);
	if (ret) {
		mlog(ML_ERROR, "setting TCP_NODELAY failed with %d\n", ret);
		goto out;
	}

	r2net_register_callbacks(sc->sc_sock->sk, sc);

	spin_lock(&nn->nn_lock);
	/* handshake completion will set nn->nn_sc_valid */
	r2net_set_nn_state(nn, sc, 0, 0);
	spin_unlock(&nn->nn_lock);

	remoteaddr.sin_family = AF_INET;
	remoteaddr.sin_addr.s_addr = node->nd_ipv4_address;
	remoteaddr.sin_port = node->nd_ipv4_port;

	ret = sc->sc_sock->ops->connect(sc->sc_sock,
					(struct sockaddr *)&remoteaddr,
					sizeof(remoteaddr),
					O_NONBLOCK);
	if (ret == -EINPROGRESS)
		ret = 0;

out:
	if (ret) {
		pr_notice("ramster: Connect attempt to " SC_NODEF_FMT
		       " failed with errno %d\n", sc->sc_node->nd_name,
			sc->sc_node->nd_num, &sc->sc_node->nd_ipv4_address,
			ntohs(sc->sc_node->nd_ipv4_port), ret);
		/* 0 err so that another will be queued and attempted
		 * from set_nn_state */
		if (sc)
			r2net_ensure_shutdown(nn, sc, 0);
	}
	if (sc)
		sc_put(sc);
	if (node)
		r2nm_node_put(node);
	if (mynode)
		r2nm_node_put(mynode);

	return;
}

static void r2net_connect_expired(struct work_struct *work)
{
	struct r2net_node *nn =
		container_of(work, struct r2net_node, nn_connect_expired.work);

	spin_lock(&nn->nn_lock);
	if (!nn->nn_sc_valid) {
		pr_notice("ramster: No connection established with "
		       "node %u after %u.%u seconds, giving up.\n",
		     r2net_num_from_nn(nn),
		     r2net_idle_timeout() / 1000,
		     r2net_idle_timeout() % 1000);

		r2net_set_nn_state(nn, NULL, 0, -ENOTCONN);
	}
	spin_unlock(&nn->nn_lock);
}

static void r2net_still_up(struct work_struct *work)
{
}

/* ------------------------------------------------------------ */

void r2net_disconnect_node(struct r2nm_node *node)
{
	struct r2net_node *nn = r2net_nn_from_num(node->nd_num);

	/* don't reconnect until it's heartbeating again */
	spin_lock(&nn->nn_lock);
	atomic_set(&nn->nn_timeout, 0);
	r2net_set_nn_state(nn, NULL, 0, -ENOTCONN);
	spin_unlock(&nn->nn_lock);

	if (r2net_wq) {
		cancel_delayed_work(&nn->nn_connect_expired);
		cancel_delayed_work(&nn->nn_connect_work);
		cancel_delayed_work(&nn->nn_still_up);
		flush_workqueue(r2net_wq);
	}
}

static void r2net_hb_node_down_cb(struct r2nm_node *node, int node_num,
				  void *data)
{
	if (!node)
		return;

	if (node_num != r2nm_this_node())
		r2net_disconnect_node(node);

	BUG_ON(atomic_read(&r2net_connected_peers) < 0);
}

static void r2net_hb_node_up_cb(struct r2nm_node *node, int node_num,
				void *data)
{
	struct r2net_node *nn = r2net_nn_from_num(node_num);

	BUG_ON(!node);

	/* ensure an immediate connect attempt */
	nn->nn_last_connect_attempt = jiffies -
		(msecs_to_jiffies(r2net_reconnect_delay()) + 1);

	if (node_num != r2nm_this_node()) {
		/* believe it or not, accept and node hearbeating testing
		 * can succeed for this node before we got here.. so
		 * only use set_nn_state to clear the persistent error
		 * if that hasn't already happened */
		spin_lock(&nn->nn_lock);
		atomic_set(&nn->nn_timeout, 0);
		if (nn->nn_persistent_error)
			r2net_set_nn_state(nn, NULL, 0, 0);
		spin_unlock(&nn->nn_lock);
	}
}

void r2net_unregister_hb_callbacks(void)
{
	r2hb_unregister_callback(NULL, &r2net_hb_up);
	r2hb_unregister_callback(NULL, &r2net_hb_down);
}

int r2net_register_hb_callbacks(void)
{
	int ret;

	r2hb_setup_callback(&r2net_hb_down, R2HB_NODE_DOWN_CB,
			    r2net_hb_node_down_cb, NULL, R2NET_HB_PRI);
	r2hb_setup_callback(&r2net_hb_up, R2HB_NODE_UP_CB,
			    r2net_hb_node_up_cb, NULL, R2NET_HB_PRI);

	ret = r2hb_register_callback(NULL, &r2net_hb_up);
	if (ret == 0)
		ret = r2hb_register_callback(NULL, &r2net_hb_down);

	if (ret)
		r2net_unregister_hb_callbacks();

	return ret;
}

/* ------------------------------------------------------------ */

static int r2net_accept_one(struct socket *sock)
{
	int ret, slen;
	struct sockaddr_in sin;
	struct socket *new_sock = NULL;
	struct r2nm_node *node = NULL;
	struct r2nm_node *local_node = NULL;
	struct r2net_sock_container *sc = NULL;
	struct r2net_node *nn;

	BUG_ON(sock == NULL);
	ret = sock_create_lite(sock->sk->sk_family, sock->sk->sk_type,
			       sock->sk->sk_protocol, &new_sock);
	if (ret)
		goto out;

	new_sock->type = sock->type;
	new_sock->ops = sock->ops;
	ret = sock->ops->accept(sock, new_sock, O_NONBLOCK);
	if (ret < 0)
		goto out;

	new_sock->sk->sk_allocation = GFP_ATOMIC;

	ret = r2net_set_nodelay(new_sock);
	if (ret) {
		mlog(ML_ERROR, "setting TCP_NODELAY failed with %d\n", ret);
		goto out;
	}

	slen = sizeof(sin);
	ret = new_sock->ops->getname(new_sock, (struct sockaddr *) &sin,
				       &slen, 1);
	if (ret < 0)
		goto out;

	node = r2nm_get_node_by_ip(sin.sin_addr.s_addr);
	if (node == NULL) {
		pr_notice("ramster: Attempt to connect from unknown "
		       "node at %pI4:%d\n", &sin.sin_addr.s_addr,
		       ntohs(sin.sin_port));
		ret = -EINVAL;
		goto out;
	}

	if (r2nm_this_node() >= node->nd_num) {
		local_node = r2nm_get_node_by_num(r2nm_this_node());
		pr_notice("ramster: Unexpected connect attempt seen "
		       "at node '%s' (%u, %pI4:%d) from node '%s' (%u, "
		       "%pI4:%d)\n", local_node->nd_name, local_node->nd_num,
		       &(local_node->nd_ipv4_address),
		       ntohs(local_node->nd_ipv4_port), node->nd_name,
		       node->nd_num, &sin.sin_addr.s_addr, ntohs(sin.sin_port));
		ret = -EINVAL;
		goto out;
	}

	/* this happens all the time when the other node sees our heartbeat
	 * and tries to connect before we see their heartbeat */
	if (!r2hb_check_node_heartbeating_from_callback(node->nd_num)) {
		mlog(ML_CONN, "attempt to connect from node '%s' at "
		     "%pI4:%d but it isn't heartbeating\n",
		     node->nd_name, &sin.sin_addr.s_addr,
		     ntohs(sin.sin_port));
		ret = -EINVAL;
		goto out;
	}

	nn = r2net_nn_from_num(node->nd_num);

	spin_lock(&nn->nn_lock);
	if (nn->nn_sc)
		ret = -EBUSY;
	else
		ret = 0;
	spin_unlock(&nn->nn_lock);
	if (ret) {
		pr_notice("ramster: Attempt to connect from node '%s' "
		       "at %pI4:%d but it already has an open connection\n",
		       node->nd_name, &sin.sin_addr.s_addr,
		       ntohs(sin.sin_port));
		goto out;
	}

	sc = sc_alloc(node);
	if (sc == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	sc->sc_sock = new_sock;
	new_sock = NULL;

	spin_lock(&nn->nn_lock);
	atomic_set(&nn->nn_timeout, 0);
	r2net_set_nn_state(nn, sc, 0, 0);
	spin_unlock(&nn->nn_lock);

	r2net_register_callbacks(sc->sc_sock->sk, sc);
	r2net_sc_queue_work(sc, &sc->sc_rx_work);

	r2net_initialize_handshake();
	r2net_sendpage(sc, r2net_hand, sizeof(*r2net_hand));

out:
	if (new_sock)
		sock_release(new_sock);
	if (node)
		r2nm_node_put(node);
	if (local_node)
		r2nm_node_put(local_node);
	if (sc)
		sc_put(sc);
	return ret;
}

static void r2net_accept_many(struct work_struct *work)
{
	struct socket *sock = r2net_listen_sock;
	while (r2net_accept_one(sock) == 0)
		cond_resched();
}

static void r2net_listen_data_ready(struct sock *sk, int bytes)
{
	void (*ready)(struct sock *sk, int bytes);

	read_lock(&sk->sk_callback_lock);
	ready = sk->sk_user_data;
	if (ready == NULL) { /* check for teardown race */
		ready = sk->sk_data_ready;
		goto out;
	}

	/* ->sk_data_ready is also called for a newly established child socket
	 * before it has been accepted and the acceptor has set up their
	 * data_ready.. we only want to queue listen work for our listening
	 * socket */
	if (sk->sk_state == TCP_LISTEN) {
		mlog(ML_TCP, "bytes: %d\n", bytes);
		queue_work(r2net_wq, &r2net_listen_work);
	}

out:
	read_unlock(&sk->sk_callback_lock);
	ready(sk, bytes);
}

static int r2net_open_listening_sock(__be32 addr, __be16 port)
{
	struct socket *sock = NULL;
	int ret;
	struct sockaddr_in sin = {
		.sin_family = PF_INET,
		.sin_addr = { .s_addr = addr },
		.sin_port = port,
	};

	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0) {
		pr_err("ramster: Error %d while creating socket\n", ret);
		goto out;
	}

	sock->sk->sk_allocation = GFP_ATOMIC;

	write_lock_bh(&sock->sk->sk_callback_lock);
	sock->sk->sk_user_data = sock->sk->sk_data_ready;
	sock->sk->sk_data_ready = r2net_listen_data_ready;
	write_unlock_bh(&sock->sk->sk_callback_lock);

	r2net_listen_sock = sock;
	INIT_WORK(&r2net_listen_work, r2net_accept_many);

	sock->sk->sk_reuse = /* SK_CAN_REUSE FIXME FOR 3.4 */ 1;
	ret = sock->ops->bind(sock, (struct sockaddr *)&sin, sizeof(sin));
	if (ret < 0) {
		pr_err("ramster: Error %d while binding socket at %pI4:%u\n",
			ret, &addr, ntohs(port));
		goto out;
	}

	ret = sock->ops->listen(sock, 64);
	if (ret < 0)
		pr_err("ramster: Error %d while listening on %pI4:%u\n",
		       ret, &addr, ntohs(port));

out:
	if (ret) {
		r2net_listen_sock = NULL;
		if (sock)
			sock_release(sock);
	}
	return ret;
}

/*
 * called from node manager when we should bring up our network listening
 * socket.  node manager handles all the serialization to only call this
 * once and to match it with r2net_stop_listening().  note,
 * r2nm_this_node() doesn't work yet as we're being called while it
 * is being set up.
 */
int r2net_start_listening(struct r2nm_node *node)
{
	int ret = 0;

	BUG_ON(r2net_wq != NULL);
	BUG_ON(r2net_listen_sock != NULL);

	mlog(ML_KTHREAD, "starting r2net thread...\n");
	r2net_wq = create_singlethread_workqueue("r2net");
	if (r2net_wq == NULL) {
		mlog(ML_ERROR, "unable to launch r2net thread\n");
		return -ENOMEM; /* ? */
	}

	ret = r2net_open_listening_sock(node->nd_ipv4_address,
					node->nd_ipv4_port);
	if (ret) {
		destroy_workqueue(r2net_wq);
		r2net_wq = NULL;
	}

	return ret;
}

/* again, r2nm_this_node() doesn't work here as we're involved in
 * tearing it down */
void r2net_stop_listening(struct r2nm_node *node)
{
	struct socket *sock = r2net_listen_sock;
	size_t i;

	BUG_ON(r2net_wq == NULL);
	BUG_ON(r2net_listen_sock == NULL);

	/* stop the listening socket from generating work */
	write_lock_bh(&sock->sk->sk_callback_lock);
	sock->sk->sk_data_ready = sock->sk->sk_user_data;
	sock->sk->sk_user_data = NULL;
	write_unlock_bh(&sock->sk->sk_callback_lock);

	for (i = 0; i < ARRAY_SIZE(r2net_nodes); i++) {
		struct r2nm_node *node = r2nm_get_node_by_num(i);
		if (node) {
			r2net_disconnect_node(node);
			r2nm_node_put(node);
		}
	}

	/* finish all work and tear down the work queue */
	mlog(ML_KTHREAD, "waiting for r2net thread to exit....\n");
	destroy_workqueue(r2net_wq);
	r2net_wq = NULL;

	sock_release(r2net_listen_sock);
	r2net_listen_sock = NULL;
}

void r2net_hb_node_up_manual(int node_num)
{
	struct r2nm_node dummy;
	if (r2nm_single_cluster == NULL)
		pr_err("ramster: cluster not alive, node_up_manual ignored\n");
	else {
		r2hb_manual_set_node_heartbeating(node_num);
		r2net_hb_node_up_cb(&dummy, node_num, NULL);
	}
}

/* ------------------------------------------------------------ */

int r2net_init(void)
{
	unsigned long i;

	if (r2net_debugfs_init())
		return -ENOMEM;

	r2net_hand = kzalloc(sizeof(struct r2net_handshake), GFP_KERNEL);
	r2net_keep_req = kzalloc(sizeof(struct r2net_msg), GFP_KERNEL);
	r2net_keep_resp = kzalloc(sizeof(struct r2net_msg), GFP_KERNEL);
	if (!r2net_hand || !r2net_keep_req || !r2net_keep_resp) {
		kfree(r2net_hand);
		kfree(r2net_keep_req);
		kfree(r2net_keep_resp);
		return -ENOMEM;
	}

	r2net_hand->protocol_version = cpu_to_be64(R2NET_PROTOCOL_VERSION);
	r2net_hand->connector_id = cpu_to_be64(1);

	r2net_keep_req->magic = cpu_to_be16(R2NET_MSG_KEEP_REQ_MAGIC);
	r2net_keep_resp->magic = cpu_to_be16(R2NET_MSG_KEEP_RESP_MAGIC);

	for (i = 0; i < ARRAY_SIZE(r2net_nodes); i++) {
		struct r2net_node *nn = r2net_nn_from_num(i);

		atomic_set(&nn->nn_timeout, 0);
		spin_lock_init(&nn->nn_lock);
		INIT_DELAYED_WORK(&nn->nn_connect_work, r2net_start_connect);
		INIT_DELAYED_WORK(&nn->nn_connect_expired,
				  r2net_connect_expired);
		INIT_DELAYED_WORK(&nn->nn_still_up, r2net_still_up);
		/* until we see hb from a node we'll return einval */
		nn->nn_persistent_error = -ENOTCONN;
		init_waitqueue_head(&nn->nn_sc_wq);
		idr_init(&nn->nn_status_idr);
		INIT_LIST_HEAD(&nn->nn_status_list);
	}

	return 0;
}

void r2net_exit(void)
{
	kfree(r2net_hand);
	kfree(r2net_keep_req);
	kfree(r2net_keep_resp);
	r2net_debugfs_exit();
}
