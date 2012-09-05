/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 */

#ifndef R2CLUSTER_TCP_INTERNAL_H
#define R2CLUSTER_TCP_INTERNAL_H

#define R2NET_MSG_MAGIC           ((u16)0xfa55)
#define R2NET_MSG_STATUS_MAGIC    ((u16)0xfa56)
#define R2NET_MSG_KEEP_REQ_MAGIC  ((u16)0xfa57)
#define R2NET_MSG_KEEP_RESP_MAGIC ((u16)0xfa58)
/*
 * "data magic" is a long version of "status magic" where the message
 * payload actually contains data to be passed in reply to certain messages
 */
#define R2NET_MSG_DATA_MAGIC      ((u16)0xfa59)

/* we're delaying our quorum decision so that heartbeat will have timed
 * out truly dead nodes by the time we come around to making decisions
 * on their number */
#define R2NET_QUORUM_DELAY_MS	\
		((r2hb_dead_threshold + 2) * R2HB_REGION_TIMEOUT_MS)

/*
 * This version number represents quite a lot, unfortunately.  It not
 * only represents the raw network message protocol on the wire but also
 * locking semantics of the file system using the protocol.  It should
 * be somewhere else, I'm sure, but right now it isn't.
 *
 * With version 11, we separate out the filesystem locking portion.  The
 * filesystem now has a major.minor version it negotiates.  Version 11
 * introduces this negotiation to the r2dlm protocol, and as such the
 * version here in tcp_internal.h should not need to be bumped for
 * filesystem locking changes.
 *
 * New in version 11
 *	- Negotiation of filesystem locking in the dlm join.
 *
 * New in version 10:
 *	- Meta/data locks combined
 *
 * New in version 9:
 *	- All votes removed
 *
 * New in version 8:
 *	- Replace delete inode votes with a cluster lock
 *
 * New in version 7:
 *	- DLM join domain includes the live nodemap
 *
 * New in version 6:
 *	- DLM lockres remote refcount fixes.
 *
 * New in version 5:
 *	- Network timeout checking protocol
 *
 * New in version 4:
 *	- Remove i_generation from lock names for better stat performance.
 *
 * New in version 3:
 *	- Replace dentry votes with a cluster lock
 *
 * New in version 2:
 *	- full 64 bit i_size in the metadata lock lvbs
 *	- introduction of "rw" lock and pushing meta/data locking down
 */
#define R2NET_PROTOCOL_VERSION 11ULL
struct r2net_handshake {
	__be64	protocol_version;
	__be64	connector_id;
	__be32  r2hb_heartbeat_timeout_ms;
	__be32  r2net_idle_timeout_ms;
	__be32  r2net_keepalive_delay_ms;
	__be32  r2net_reconnect_delay_ms;
};

struct r2net_node {
	/* this is never called from int/bh */
	spinlock_t			nn_lock;

	/* set the moment an sc is allocated and a connect is started */
	struct r2net_sock_container	*nn_sc;
	/* _valid is only set after the handshake passes and tx can happen */
	unsigned			nn_sc_valid:1;
	/* if this is set tx just returns it */
	int				nn_persistent_error;
	/* It is only set to 1 after the idle time out. */
	atomic_t			nn_timeout;

	/* threads waiting for an sc to arrive wait on the wq for generation
	 * to increase.  it is increased when a connecting socket succeeds
	 * or fails or when an accepted socket is attached. */
	wait_queue_head_t		nn_sc_wq;

	struct idr			nn_status_idr;
	struct list_head		nn_status_list;

	/* connects are attempted from when heartbeat comes up until either hb
	 * goes down, the node is unconfigured, no connect attempts succeed
	 * before R2NET_CONN_IDLE_DELAY, or a connect succeeds.  connect_work
	 * is queued from set_nn_state both from hb up and from itself if a
	 * connect attempt fails and so can be self-arming.  shutdown is
	 * careful to first mark the nn such that no connects will be attempted
	 * before canceling delayed connect work and flushing the queue. */
	struct delayed_work		nn_connect_work;
	unsigned long			nn_last_connect_attempt;

	/* this is queued as nodes come up and is canceled when a connection is
	 * established.  this expiring gives up on the node and errors out
	 * transmits */
	struct delayed_work		nn_connect_expired;

	/* after we give up on a socket we wait a while before deciding
	 * that it is still heartbeating and that we should do some
	 * quorum work */
	struct delayed_work		nn_still_up;
};

struct r2net_sock_container {
	struct kref		sc_kref;
	/* the next two are valid for the life time of the sc */
	struct socket		*sc_sock;
	struct r2nm_node	*sc_node;

	/* all of these sc work structs hold refs on the sc while they are
	 * queued.  they should not be able to ref a freed sc.  the teardown
	 * race is with r2net_wq destruction in r2net_stop_listening() */

	/* rx and connect work are generated from socket callbacks.  sc
	 * shutdown removes the callbacks and then flushes the work queue */
	struct work_struct	sc_rx_work;
	struct work_struct	sc_connect_work;
	/* shutdown work is triggered in two ways.  the simple way is
	 * for a code path calls ensure_shutdown which gets a lock, removes
	 * the sc from the nn, and queues the work.  in this case the
	 * work is single-shot.  the work is also queued from a sock
	 * callback, though, and in this case the work will find the sc
	 * still on the nn and will call ensure_shutdown itself.. this
	 * ends up triggering the shutdown work again, though nothing
	 * will be done in that second iteration.  so work queue teardown
	 * has to be careful to remove the sc from the nn before waiting
	 * on the work queue so that the shutdown work doesn't remove the
	 * sc and rearm itself.
	 */
	struct work_struct	sc_shutdown_work;

	struct timer_list	sc_idle_timeout;
	struct delayed_work	sc_keepalive_work;

	unsigned		sc_handshake_ok:1;

	struct page		*sc_page;
	size_t			sc_page_off;

	/* original handlers for the sockets */
	void			(*sc_state_change)(struct sock *sk);
	void			(*sc_data_ready)(struct sock *sk, int bytes);

	u32			sc_msg_key;
	u16			sc_msg_type;

#ifdef CONFIG_DEBUG_FS
	struct list_head        sc_net_debug_item;
	ktime_t			sc_tv_timer;
	ktime_t			sc_tv_data_ready;
	ktime_t			sc_tv_advance_start;
	ktime_t			sc_tv_advance_stop;
	ktime_t			sc_tv_func_start;
	ktime_t			sc_tv_func_stop;
#endif
#ifdef CONFIG_RAMSTER_FS_STATS
	ktime_t			sc_tv_acquiry_total;
	ktime_t			sc_tv_send_total;
	ktime_t			sc_tv_status_total;
	u32			sc_send_count;
	u32			sc_recv_count;
	ktime_t			sc_tv_process_total;
#endif
	struct mutex		sc_send_lock;
};

struct r2net_msg_handler {
	struct rb_node		nh_node;
	u32			nh_max_len;
	u32			nh_msg_type;
	u32			nh_key;
	r2net_msg_handler_func	*nh_func;
	r2net_msg_handler_func	*nh_func_data;
	r2net_post_msg_handler_func
				*nh_post_func;
	struct kref		nh_kref;
	struct list_head	nh_unregister_item;
};

enum r2net_system_error {
	R2NET_ERR_NONE = 0,
	R2NET_ERR_NO_HNDLR,
	R2NET_ERR_OVERFLOW,
	R2NET_ERR_DIED,
	R2NET_ERR_MAX
};

struct r2net_status_wait {
	enum r2net_system_error	ns_sys_status;
	s32			ns_status;
	int			ns_id;
	wait_queue_head_t	ns_wq;
	struct list_head	ns_node_item;
};

#ifdef CONFIG_DEBUG_FS
/* just for state dumps */
struct r2net_send_tracking {
	struct list_head		st_net_debug_item;
	struct task_struct		*st_task;
	struct r2net_sock_container	*st_sc;
	u32				st_id;
	u32				st_msg_type;
	u32				st_msg_key;
	u8				st_node;
	ktime_t				st_sock_time;
	ktime_t				st_send_time;
	ktime_t				st_status_time;
};
#else
struct r2net_send_tracking {
	u32	dummy;
};
#endif	/* CONFIG_DEBUG_FS */

#endif /* R2CLUSTER_TCP_INTERNAL_H */
