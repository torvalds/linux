// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 *
 *   Author: Zach Brown <zab@zabbo.net>
 *   Author: Peter J. Braam <braam@clusterfs.com>
 *   Author: Phil Schwan <phil@clusterfs.com>
 *   Author: Eric Barton <eric@bartonsoftware.com>
 *
 *   This file is part of Lustre, http://www.lustre.org
 *
 *   Portals is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Portals is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 */

#ifndef _SOCKLND_SOCKLND_H_
#define _SOCKLND_SOCKLND_H_

#define DEBUG_PORTAL_ALLOC
#define DEBUG_SUBSYSTEM S_LND

#include <linux/crc32.h>
#include <linux/errno.h>
#include <linux/if.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/sysctl.h>
#include <linux/uio.h>
#include <linux/unistd.h>
#include <asm/irq.h>
#include <net/sock.h>
#include <net/tcp.h>

#include <linux/libcfs/libcfs.h>
#include <linux/lnet/lib-lnet.h>
#include <linux/lnet/socklnd.h>

/* assume one thread for each connection type */
#define SOCKNAL_NSCHEDS		3
#define SOCKNAL_NSCHEDS_HIGH	(SOCKNAL_NSCHEDS << 1)

#define SOCKNAL_PEER_HASH_SIZE  101   /* # peer lists */
#define SOCKNAL_RESCHED         100   /* # scheduler loops before reschedule */
#define SOCKNAL_INSANITY_RECONN 5000  /* connd is trying on reconn infinitely */
#define SOCKNAL_ENOMEM_RETRY    CFS_TICK /* jiffies between retries */

#define SOCKNAL_SINGLE_FRAG_TX  0     /* disable multi-fragment sends */
#define SOCKNAL_SINGLE_FRAG_RX  0     /* disable multi-fragment receives */

#define SOCKNAL_VERSION_DEBUG   0     /* enable protocol version debugging */

/*
 * risk kmap deadlock on multi-frag I/O (backs off to single-frag if disabled).
 * no risk if we're not running on a CONFIG_HIGHMEM platform.
 */
#ifdef CONFIG_HIGHMEM
# define SOCKNAL_RISK_KMAP_DEADLOCK  0
#else
# define SOCKNAL_RISK_KMAP_DEADLOCK  1
#endif

struct ksock_sched_info;

struct ksock_sched {				/* per scheduler state */
	spinlock_t              kss_lock;       /* serialise */
	struct list_head        kss_rx_conns;   /* conn waiting to be read */
	struct list_head        kss_tx_conns;   /* conn waiting to be written */
	struct list_head        kss_zombie_noop_txs; /* zombie noop tx list */
	wait_queue_head_t       kss_waitq;	/* where scheduler sleeps */
	int                     kss_nconns;     /* # connections assigned to
						 * this scheduler
						 */
	struct ksock_sched_info *kss_info;	/* owner of it */
};

struct ksock_sched_info {
	int                     ksi_nthreads_max; /* max allowed threads */
	int                     ksi_nthreads;     /* number of threads */
	int                     ksi_cpt;          /* CPT id */
	struct ksock_sched	*ksi_scheds;	  /* array of schedulers */
};

#define KSOCK_CPT_SHIFT           16
#define KSOCK_THREAD_ID(cpt, sid) (((cpt) << KSOCK_CPT_SHIFT) | (sid))
#define KSOCK_THREAD_CPT(id)      ((id) >> KSOCK_CPT_SHIFT)
#define KSOCK_THREAD_SID(id)      ((id) & ((1UL << KSOCK_CPT_SHIFT) - 1))

struct ksock_interface {			/* in-use interface */
	__u32		ksni_ipaddr;		/* interface's IP address */
	__u32		ksni_netmask;		/* interface's network mask */
	int		ksni_nroutes;		/* # routes using (active) */
	int		ksni_npeers;		/* # peers using (passive) */
	char		ksni_name[IFNAMSIZ];	/* interface name */
};

struct ksock_tunables {
	int          *ksnd_timeout;            /* "stuck" socket timeout
						* (seconds)
						*/
	int          *ksnd_nscheds;            /* # scheduler threads in each
						* pool while starting
						*/
	int          *ksnd_nconnds;            /* # connection daemons */
	int          *ksnd_nconnds_max;        /* max # connection daemons */
	int          *ksnd_min_reconnectms;    /* first connection retry after
						* (ms)...
						*/
	int          *ksnd_max_reconnectms;    /* ...exponentially increasing to
						* this
						*/
	int          *ksnd_eager_ack;          /* make TCP ack eagerly? */
	int          *ksnd_typed_conns;        /* drive sockets by type? */
	int          *ksnd_min_bulk;           /* smallest "large" message */
	int          *ksnd_tx_buffer_size;     /* socket tx buffer size */
	int          *ksnd_rx_buffer_size;     /* socket rx buffer size */
	int          *ksnd_nagle;              /* enable NAGLE? */
	int          *ksnd_round_robin;        /* round robin for multiple
						* interfaces
						*/
	int          *ksnd_keepalive;          /* # secs for sending keepalive
						* NOOP
						*/
	int          *ksnd_keepalive_idle;     /* # idle secs before 1st probe
						*/
	int          *ksnd_keepalive_count;    /* # probes */
	int          *ksnd_keepalive_intvl;    /* time between probes */
	int          *ksnd_credits;            /* # concurrent sends */
	int          *ksnd_peertxcredits;      /* # concurrent sends to 1 peer
						*/
	int          *ksnd_peerrtrcredits;     /* # per-peer router buffer
						* credits
						*/
	int          *ksnd_peertimeout;        /* seconds to consider peer dead
						*/
	int          *ksnd_enable_csum;        /* enable check sum */
	int          *ksnd_inject_csum_error;  /* set non-zero to inject
						* checksum error
						*/
	int          *ksnd_nonblk_zcack;       /* always send zc-ack on
						* non-blocking connection
						*/
	unsigned int *ksnd_zc_min_payload;     /* minimum zero copy payload
						* size
						*/
	int          *ksnd_zc_recv;            /* enable ZC receive (for
						* Chelsio TOE)
						*/
	int          *ksnd_zc_recv_min_nfrags; /* minimum # of fragments to
						* enable ZC receive
						*/
};

struct ksock_net {
	__u64		  ksnn_incarnation;	/* my epoch */
	spinlock_t	  ksnn_lock;		/* serialise */
	struct list_head	  ksnn_list;		/* chain on global list */
	int		  ksnn_npeers;		/* # peers */
	int		  ksnn_shutdown;	/* shutting down? */
	int		  ksnn_ninterfaces;	/* IP interfaces */
	struct ksock_interface ksnn_interfaces[LNET_MAX_INTERFACES];
};

/** connd timeout */
#define SOCKNAL_CONND_TIMEOUT  120
/** reserved thread for accepting & creating new connd */
#define SOCKNAL_CONND_RESV     1

struct ksock_nal_data {
	int                     ksnd_init;              /* initialisation state
							 */
	int                     ksnd_nnets;             /* # networks set up */
	struct list_head        ksnd_nets;              /* list of nets */
	rwlock_t                ksnd_global_lock;       /* stabilize peer/conn
							 * ops
							 */
	struct list_head        *ksnd_peers;            /* hash table of all my
							 * known peers
							 */
	int                     ksnd_peer_hash_size;    /* size of ksnd_peers */

	int                     ksnd_nthreads;          /* # live threads */
	int                     ksnd_shuttingdown;      /* tell threads to exit
							 */
	struct ksock_sched_info **ksnd_sched_info;      /* schedulers info */

	atomic_t                ksnd_nactive_txs;       /* #active txs */

	struct list_head        ksnd_deathrow_conns;    /* conns to close:
							 * reaper_lock
							 */
	struct list_head        ksnd_zombie_conns;      /* conns to free:
							 * reaper_lock
							 */
	struct list_head        ksnd_enomem_conns;      /* conns to retry:
							 * reaper_lock
							 */
	wait_queue_head_t       ksnd_reaper_waitq;      /* reaper sleeps here */
	unsigned long	        ksnd_reaper_waketime;   /* when reaper will wake
							 */
	spinlock_t              ksnd_reaper_lock;       /* serialise */

	int                     ksnd_enomem_tx;         /* test ENOMEM sender */
	int                     ksnd_stall_tx;          /* test sluggish sender
							 */
	int                     ksnd_stall_rx;          /* test sluggish
							 * receiver
							 */
	struct list_head        ksnd_connd_connreqs;    /* incoming connection
							 * requests
							 */
	struct list_head        ksnd_connd_routes;      /* routes waiting to be
							 * connected
							 */
	wait_queue_head_t       ksnd_connd_waitq;       /* connds sleep here */
	int                     ksnd_connd_connecting;  /* # connds connecting
							 */
	time64_t                ksnd_connd_failed_stamp;/* time stamp of the
							 * last failed
							 * connecting attempt
							 */
	time64_t                ksnd_connd_starting_stamp;/* time stamp of the
							   * last starting connd
							   */
	unsigned int		ksnd_connd_starting;	/* # starting connd */
	unsigned int		ksnd_connd_running;	/* # running connd */
	spinlock_t              ksnd_connd_lock;        /* serialise */

	struct list_head        ksnd_idle_noop_txs;     /* list head for freed
							 * noop tx
							 */
	spinlock_t              ksnd_tx_lock;           /* serialise, g_lock
							 * unsafe
							 */
};

#define SOCKNAL_INIT_NOTHING 0
#define SOCKNAL_INIT_DATA    1
#define SOCKNAL_INIT_ALL     2

/*
 * A packet just assembled for transmission is represented by 1 or more
 * struct iovec fragments (the first frag contains the portals header),
 * followed by 0 or more struct bio_vec fragments.
 *
 * On the receive side, initially 1 struct iovec fragment is posted for
 * receive (the header).  Once the header has been received, the payload is
 * received into either struct iovec or struct bio_vec fragments, depending on
 * what the header matched or whether the message needs forwarding.
 */
struct ksock_conn;  /* forward ref */
struct ksock_peer;  /* forward ref */
struct ksock_route; /* forward ref */
struct ksock_proto; /* forward ref */

struct ksock_tx {			   /* transmit packet */
	struct list_head  tx_list;         /* queue on conn for transmission etc
					    */
	struct list_head  tx_zc_list;      /* queue on peer for ZC request */
	atomic_t          tx_refcount;     /* tx reference count */
	int               tx_nob;          /* # packet bytes */
	int               tx_resid;        /* residual bytes */
	int               tx_niov;         /* # packet iovec frags */
	struct kvec       *tx_iov;         /* packet iovec frags */
	int               tx_nkiov;        /* # packet page frags */
	unsigned short    tx_zc_aborted;   /* aborted ZC request */
	unsigned short    tx_zc_capable:1; /* payload is large enough for ZC */
	unsigned short    tx_zc_checked:1; /* Have I checked if I should ZC? */
	unsigned short    tx_nonblk:1;     /* it's a non-blocking ACK */
	struct bio_vec	  *tx_kiov;	   /* packet page frags */
	struct ksock_conn *tx_conn;        /* owning conn */
	struct lnet_msg        *tx_lnetmsg;     /* lnet message for lnet_finalize()
					    */
	unsigned long     tx_deadline;     /* when (in jiffies) tx times out */
	struct ksock_msg       tx_msg;          /* socklnd message buffer */
	int               tx_desc_size;    /* size of this descriptor */
	union {
		struct {
			struct kvec iov;     /* virt hdr */
			struct bio_vec kiov[0]; /* paged payload */
		} paged;
		struct {
			struct kvec iov[1];  /* virt hdr + payload */
		} virt;
	} tx_frags;
};

#define KSOCK_NOOP_TX_SIZE (offsetof(struct ksock_tx, tx_frags.paged.kiov[0]))

/* network zero copy callback descriptor embedded in struct ksock_tx */

/*
 * space for the rx frag descriptors; we either read a single contiguous
 * header, or up to LNET_MAX_IOV frags of payload of either type.
 */
union ksock_rxiovspace {
	struct kvec      iov[LNET_MAX_IOV];
	struct bio_vec	kiov[LNET_MAX_IOV];
};

#define SOCKNAL_RX_KSM_HEADER   1 /* reading ksock message header */
#define SOCKNAL_RX_LNET_HEADER  2 /* reading lnet message header */
#define SOCKNAL_RX_PARSE        3 /* Calling lnet_parse() */
#define SOCKNAL_RX_PARSE_WAIT   4 /* waiting to be told to read the body */
#define SOCKNAL_RX_LNET_PAYLOAD 5 /* reading lnet payload (to deliver here) */
#define SOCKNAL_RX_SLOP         6 /* skipping body */

struct ksock_conn {
	struct ksock_peer  *ksnc_peer;        /* owning peer */
	struct ksock_route *ksnc_route;       /* owning route */
	struct list_head   ksnc_list;         /* stash on peer's conn list */
	struct socket      *ksnc_sock;        /* actual socket */
	void               *ksnc_saved_data_ready;  /* socket's original
						     * data_ready() callback
						     */
	void               *ksnc_saved_write_space; /* socket's original
						     * write_space() callback
						     */
	atomic_t           ksnc_conn_refcount;/* conn refcount */
	atomic_t           ksnc_sock_refcount;/* sock refcount */
	struct ksock_sched *ksnc_scheduler;	/* who schedules this connection
						 */
	__u32              ksnc_myipaddr;     /* my IP */
	__u32              ksnc_ipaddr;       /* peer's IP */
	int                ksnc_port;         /* peer's port */
	signed int         ksnc_type:3;       /* type of connection, should be
					       * signed value
					       */
	unsigned int       ksnc_closing:1;    /* being shut down */
	unsigned int       ksnc_flip:1;       /* flip or not, only for V2.x */
	unsigned int       ksnc_zc_capable:1; /* enable to ZC */
	struct ksock_proto *ksnc_proto;       /* protocol for the connection */

	/* reader */
	struct list_head   ksnc_rx_list;      /* where I enq waiting input or a
					       * forwarding descriptor
					       */
	unsigned long      ksnc_rx_deadline;  /* when (in jiffies) receive times
					       * out
					       */
	__u8               ksnc_rx_started;   /* started receiving a message */
	__u8               ksnc_rx_ready;     /* data ready to read */
	__u8               ksnc_rx_scheduled; /* being progressed */
	__u8               ksnc_rx_state;     /* what is being read */
	int                ksnc_rx_nob_left;  /* # bytes to next hdr/body */
	int                ksnc_rx_nob_wanted;/* bytes actually wanted */
	int                ksnc_rx_niov;      /* # iovec frags */
	struct kvec        *ksnc_rx_iov;      /* the iovec frags */
	int                ksnc_rx_nkiov;     /* # page frags */
	struct bio_vec		*ksnc_rx_kiov;	/* the page frags */
	union ksock_rxiovspace ksnc_rx_iov_space; /* space for frag descriptors */
	__u32              ksnc_rx_csum;      /* partial checksum for incoming
					       * data
					       */
	void               *ksnc_cookie;      /* rx lnet_finalize passthru arg
					       */
	struct ksock_msg        ksnc_msg;          /* incoming message buffer:
					       * V2.x message takes the
					       * whole struct
					       * V1.x message is a bare
					       * struct lnet_hdr, it's stored in
					       * ksnc_msg.ksm_u.lnetmsg
					       */
	/* WRITER */
	struct list_head   ksnc_tx_list;      /* where I enq waiting for output
					       * space
					       */
	struct list_head   ksnc_tx_queue;     /* packets waiting to be sent */
	struct ksock_tx	  *ksnc_tx_carrier;   /* next TX that can carry a LNet
					       * message or ZC-ACK
					       */
	unsigned long      ksnc_tx_deadline;  /* when (in jiffies) tx times out
					       */
	int                ksnc_tx_bufnob;    /* send buffer marker */
	atomic_t           ksnc_tx_nob;       /* # bytes queued */
	int		   ksnc_tx_ready;     /* write space */
	int		   ksnc_tx_scheduled; /* being progressed */
	unsigned long      ksnc_tx_last_post; /* time stamp of the last posted
					       * TX
					       */
};

struct ksock_route {
	struct list_head  ksnr_list;           /* chain on peer route list */
	struct list_head  ksnr_connd_list;     /* chain on ksnr_connd_routes */
	struct ksock_peer *ksnr_peer;          /* owning peer */
	atomic_t          ksnr_refcount;       /* # users */
	unsigned long     ksnr_timeout;        /* when (in jiffies) reconnection
						* can happen next
						*/
	long              ksnr_retry_interval; /* how long between retries */
	__u32             ksnr_myipaddr;       /* my IP */
	__u32             ksnr_ipaddr;         /* IP address to connect to */
	int               ksnr_port;           /* port to connect to */
	unsigned int      ksnr_scheduled:1;    /* scheduled for attention */
	unsigned int      ksnr_connecting:1;   /* connection establishment in
						* progress
						*/
	unsigned int      ksnr_connected:4;    /* connections established by
						* type
						*/
	unsigned int      ksnr_deleted:1;      /* been removed from peer? */
	unsigned int      ksnr_share_count;    /* created explicitly? */
	int               ksnr_conn_count;     /* # conns established by this
						* route
						*/
};

#define SOCKNAL_KEEPALIVE_PING 1 /* cookie for keepalive ping */

struct ksock_peer {
	struct list_head   ksnp_list;           /* stash on global peer list */
	unsigned long      ksnp_last_alive;     /* when (in jiffies) I was last
						 * alive
						 */
	struct lnet_process_id  ksnp_id;	/* who's on the other end(s) */
	atomic_t           ksnp_refcount;       /* # users */
	int                ksnp_sharecount;     /* lconf usage counter */
	int                ksnp_closing;        /* being closed */
	int                ksnp_accepting;      /* # passive connections pending
						 */
	int                ksnp_error;          /* errno on closing last conn */
	__u64              ksnp_zc_next_cookie; /* ZC completion cookie */
	__u64              ksnp_incarnation;    /* latest known peer incarnation
						 */
	struct ksock_proto *ksnp_proto;         /* latest known peer protocol */
	struct list_head   ksnp_conns;          /* all active connections */
	struct list_head   ksnp_routes;         /* routes */
	struct list_head   ksnp_tx_queue;       /* waiting packets */
	spinlock_t         ksnp_lock;           /* serialize, g_lock unsafe */
	struct list_head   ksnp_zc_req_list;    /* zero copy requests wait for
						 * ACK
						 */
	unsigned long      ksnp_send_keepalive; /* time to send keepalive */
	struct lnet_ni	   *ksnp_ni;		/* which network */
	int                ksnp_n_passive_ips;  /* # of... */

	/* preferred local interfaces */
	__u32              ksnp_passive_ips[LNET_MAX_INTERFACES];
};

struct ksock_connreq {
	struct list_head ksncr_list;  /* stash on ksnd_connd_connreqs */
	struct lnet_ni	 *ksncr_ni;	/* chosen NI */
	struct socket    *ksncr_sock; /* accepted socket */
};

extern struct ksock_nal_data ksocknal_data;
extern struct ksock_tunables ksocknal_tunables;

#define SOCKNAL_MATCH_NO  0 /* TX can't match type of connection */
#define SOCKNAL_MATCH_YES 1 /* TX matches type of connection */
#define SOCKNAL_MATCH_MAY 2 /* TX can be sent on the connection, but not
			     * preferred
			     */

struct ksock_proto {
	/* version number of protocol */
	int        pro_version;

	/* handshake function */
	int        (*pro_send_hello)(struct ksock_conn *, struct ksock_hello_msg *);

	/* handshake function */
	int        (*pro_recv_hello)(struct ksock_conn *, struct ksock_hello_msg *, int);

	/* message pack */
	void       (*pro_pack)(struct ksock_tx *);

	/* message unpack */
	void       (*pro_unpack)(struct ksock_msg *);

	/* queue tx on the connection */
	struct ksock_tx *(*pro_queue_tx_msg)(struct ksock_conn *, struct ksock_tx *);

	/* queue ZC ack on the connection */
	int        (*pro_queue_tx_zcack)(struct ksock_conn *, struct ksock_tx *, __u64);

	/* handle ZC request */
	int        (*pro_handle_zcreq)(struct ksock_conn *, __u64, int);

	/* handle ZC ACK */
	int        (*pro_handle_zcack)(struct ksock_conn *, __u64, __u64);

	/*
	 * msg type matches the connection type:
	 * return value:
	 *   return MATCH_NO  : no
	 *   return MATCH_YES : matching type
	 *   return MATCH_MAY : can be backup
	 */
	int        (*pro_match_tx)(struct ksock_conn *, struct ksock_tx *, int);
};

extern struct ksock_proto ksocknal_protocol_v1x;
extern struct ksock_proto ksocknal_protocol_v2x;
extern struct ksock_proto ksocknal_protocol_v3x;

#define KSOCK_PROTO_V1_MAJOR LNET_PROTO_TCP_VERSION_MAJOR
#define KSOCK_PROTO_V1_MINOR LNET_PROTO_TCP_VERSION_MINOR
#define KSOCK_PROTO_V1       KSOCK_PROTO_V1_MAJOR

#ifndef CPU_MASK_NONE
#define CPU_MASK_NONE   0UL
#endif

static inline int
ksocknal_route_mask(void)
{
	if (!*ksocknal_tunables.ksnd_typed_conns)
		return (1 << SOCKLND_CONN_ANY);

	return ((1 << SOCKLND_CONN_CONTROL) |
		(1 << SOCKLND_CONN_BULK_IN) |
		(1 << SOCKLND_CONN_BULK_OUT));
}

static inline struct list_head *
ksocknal_nid2peerlist(lnet_nid_t nid)
{
	unsigned int hash = ((unsigned int)nid) % ksocknal_data.ksnd_peer_hash_size;

	return &ksocknal_data.ksnd_peers[hash];
}

static inline void
ksocknal_conn_addref(struct ksock_conn *conn)
{
	LASSERT(atomic_read(&conn->ksnc_conn_refcount) > 0);
	atomic_inc(&conn->ksnc_conn_refcount);
}

void ksocknal_queue_zombie_conn(struct ksock_conn *conn);
void ksocknal_finalize_zcreq(struct ksock_conn *conn);

static inline void
ksocknal_conn_decref(struct ksock_conn *conn)
{
	LASSERT(atomic_read(&conn->ksnc_conn_refcount) > 0);
	if (atomic_dec_and_test(&conn->ksnc_conn_refcount))
		ksocknal_queue_zombie_conn(conn);
}

static inline int
ksocknal_connsock_addref(struct ksock_conn *conn)
{
	int rc = -ESHUTDOWN;

	read_lock(&ksocknal_data.ksnd_global_lock);
	if (!conn->ksnc_closing) {
		LASSERT(atomic_read(&conn->ksnc_sock_refcount) > 0);
		atomic_inc(&conn->ksnc_sock_refcount);
		rc = 0;
	}
	read_unlock(&ksocknal_data.ksnd_global_lock);

	return rc;
}

static inline void
ksocknal_connsock_decref(struct ksock_conn *conn)
{
	LASSERT(atomic_read(&conn->ksnc_sock_refcount) > 0);
	if (atomic_dec_and_test(&conn->ksnc_sock_refcount)) {
		LASSERT(conn->ksnc_closing);
		sock_release(conn->ksnc_sock);
		conn->ksnc_sock = NULL;
		ksocknal_finalize_zcreq(conn);
	}
}

static inline void
ksocknal_tx_addref(struct ksock_tx *tx)
{
	LASSERT(atomic_read(&tx->tx_refcount) > 0);
	atomic_inc(&tx->tx_refcount);
}

void ksocknal_tx_prep(struct ksock_conn *, struct ksock_tx *tx);
void ksocknal_tx_done(struct lnet_ni *ni, struct ksock_tx *tx);

static inline void
ksocknal_tx_decref(struct ksock_tx *tx)
{
	LASSERT(atomic_read(&tx->tx_refcount) > 0);
	if (atomic_dec_and_test(&tx->tx_refcount))
		ksocknal_tx_done(NULL, tx);
}

static inline void
ksocknal_route_addref(struct ksock_route *route)
{
	LASSERT(atomic_read(&route->ksnr_refcount) > 0);
	atomic_inc(&route->ksnr_refcount);
}

void ksocknal_destroy_route(struct ksock_route *route);

static inline void
ksocknal_route_decref(struct ksock_route *route)
{
	LASSERT(atomic_read(&route->ksnr_refcount) > 0);
	if (atomic_dec_and_test(&route->ksnr_refcount))
		ksocknal_destroy_route(route);
}

static inline void
ksocknal_peer_addref(struct ksock_peer *peer)
{
	LASSERT(atomic_read(&peer->ksnp_refcount) > 0);
	atomic_inc(&peer->ksnp_refcount);
}

void ksocknal_destroy_peer(struct ksock_peer *peer);

static inline void
ksocknal_peer_decref(struct ksock_peer *peer)
{
	LASSERT(atomic_read(&peer->ksnp_refcount) > 0);
	if (atomic_dec_and_test(&peer->ksnp_refcount))
		ksocknal_destroy_peer(peer);
}

int ksocknal_startup(struct lnet_ni *ni);
void ksocknal_shutdown(struct lnet_ni *ni);
int ksocknal_ctl(struct lnet_ni *ni, unsigned int cmd, void *arg);
int ksocknal_send(struct lnet_ni *ni, void *private, struct lnet_msg *lntmsg);
int ksocknal_recv(struct lnet_ni *ni, void *private, struct lnet_msg *lntmsg,
		  int delayed, struct iov_iter *to, unsigned int rlen);
int ksocknal_accept(struct lnet_ni *ni, struct socket *sock);

int ksocknal_add_peer(struct lnet_ni *ni, struct lnet_process_id id, __u32 ip,
		      int port);
struct ksock_peer *ksocknal_find_peer_locked(struct lnet_ni *ni,
					     struct lnet_process_id id);
struct ksock_peer *ksocknal_find_peer(struct lnet_ni *ni,
				      struct lnet_process_id id);
void ksocknal_peer_failed(struct ksock_peer *peer);
int ksocknal_create_conn(struct lnet_ni *ni, struct ksock_route *route,
			 struct socket *sock, int type);
void ksocknal_close_conn_locked(struct ksock_conn *conn, int why);
void ksocknal_terminate_conn(struct ksock_conn *conn);
void ksocknal_destroy_conn(struct ksock_conn *conn);
int  ksocknal_close_peer_conns_locked(struct ksock_peer *peer,
				      __u32 ipaddr, int why);
int ksocknal_close_conn_and_siblings(struct ksock_conn *conn, int why);
int ksocknal_close_matching_conns(struct lnet_process_id id, __u32 ipaddr);
struct ksock_conn *ksocknal_find_conn_locked(struct ksock_peer *peer,
					     struct ksock_tx *tx, int nonblk);

int  ksocknal_launch_packet(struct lnet_ni *ni, struct ksock_tx *tx,
			    struct lnet_process_id id);
struct ksock_tx *ksocknal_alloc_tx(int type, int size);
void ksocknal_free_tx(struct ksock_tx *tx);
struct ksock_tx *ksocknal_alloc_tx_noop(__u64 cookie, int nonblk);
void ksocknal_next_tx_carrier(struct ksock_conn *conn);
void ksocknal_queue_tx_locked(struct ksock_tx *tx, struct ksock_conn *conn);
void ksocknal_txlist_done(struct lnet_ni *ni, struct list_head *txlist, int error);
void ksocknal_notify(struct lnet_ni *ni, lnet_nid_t gw_nid, int alive);
void ksocknal_query(struct lnet_ni *ni, lnet_nid_t nid, unsigned long *when);
int ksocknal_thread_start(int (*fn)(void *arg), void *arg, char *name);
void ksocknal_thread_fini(void);
void ksocknal_launch_all_connections_locked(struct ksock_peer *peer);
struct ksock_route *ksocknal_find_connectable_route_locked(struct ksock_peer *peer);
struct ksock_route *ksocknal_find_connecting_route_locked(struct ksock_peer *peer);
int ksocknal_new_packet(struct ksock_conn *conn, int skip);
int ksocknal_scheduler(void *arg);
int ksocknal_connd(void *arg);
int ksocknal_reaper(void *arg);
int ksocknal_send_hello(struct lnet_ni *ni, struct ksock_conn *conn,
			lnet_nid_t peer_nid, struct ksock_hello_msg *hello);
int ksocknal_recv_hello(struct lnet_ni *ni, struct ksock_conn *conn,
			struct ksock_hello_msg *hello,
			struct lnet_process_id *id,
			__u64 *incarnation);
void ksocknal_read_callback(struct ksock_conn *conn);
void ksocknal_write_callback(struct ksock_conn *conn);

int ksocknal_lib_zc_capable(struct ksock_conn *conn);
void ksocknal_lib_save_callback(struct socket *sock, struct ksock_conn *conn);
void ksocknal_lib_set_callback(struct socket *sock,  struct ksock_conn *conn);
void ksocknal_lib_reset_callback(struct socket *sock, struct ksock_conn *conn);
void ksocknal_lib_push_conn(struct ksock_conn *conn);
int ksocknal_lib_get_conn_addrs(struct ksock_conn *conn);
int ksocknal_lib_setup_sock(struct socket *so);
int ksocknal_lib_send_iov(struct ksock_conn *conn, struct ksock_tx *tx);
int ksocknal_lib_send_kiov(struct ksock_conn *conn, struct ksock_tx *tx);
void ksocknal_lib_eager_ack(struct ksock_conn *conn);
int ksocknal_lib_recv_iov(struct ksock_conn *conn);
int ksocknal_lib_recv_kiov(struct ksock_conn *conn);
int ksocknal_lib_get_conn_tunables(struct ksock_conn *conn, int *txmem,
				   int *rxmem, int *nagle);

void ksocknal_read_callback(struct ksock_conn *conn);
void ksocknal_write_callback(struct ksock_conn *conn);

int ksocknal_tunables_init(void);

void ksocknal_lib_csum_tx(struct ksock_tx *tx);

int ksocknal_lib_memory_pressure(struct ksock_conn *conn);
int ksocknal_lib_bind_thread_to_cpu(int id);

#endif /* _SOCKLND_SOCKLND_H_ */
