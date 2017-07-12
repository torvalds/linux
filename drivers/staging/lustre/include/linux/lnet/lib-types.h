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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Seagate, Inc.
 *
 * lnet/include/lnet/lib-types.h
 */

#ifndef __LNET_LIB_TYPES_H__
#define __LNET_LIB_TYPES_H__

#include <linux/kthread.h>
#include <linux/uio.h>
#include <linux/types.h>
#include <linux/completion.h>

#include "types.h"
#include "lnetctl.h"

/* Max payload size */
#define LNET_MAX_PAYLOAD      CONFIG_LNET_MAX_PAYLOAD
#if (LNET_MAX_PAYLOAD < LNET_MTU)
# error "LNET_MAX_PAYLOAD too small - error in configure --with-max-payload-mb"
#elif (LNET_MAX_PAYLOAD > (PAGE_SIZE * LNET_MAX_IOV))
# error "LNET_MAX_PAYLOAD too large - error in configure --with-max-payload-mb"
#endif

/* forward refs */
struct lnet_libmd;

struct lnet_msg {
	struct list_head	msg_activelist;
	struct list_head	msg_list;	   /* Q for credits/MD */

	struct lnet_process_id	msg_target;
	/* where is it from, it's only for building event */
	lnet_nid_t		msg_from;
	__u32			msg_type;

	/* committed for sending */
	unsigned int		msg_tx_committed:1;
	/* CPT # this message committed for sending */
	unsigned int		msg_tx_cpt:15;
	/* committed for receiving */
	unsigned int		msg_rx_committed:1;
	/* CPT # this message committed for receiving */
	unsigned int		msg_rx_cpt:15;
	/* queued for tx credit */
	unsigned int		msg_tx_delayed:1;
	/* queued for RX buffer */
	unsigned int		msg_rx_delayed:1;
	/* ready for pending on RX delay list */
	unsigned int		msg_rx_ready_delay:1;

	unsigned int	msg_vmflush:1;		/* VM trying to free memory */
	unsigned int	msg_target_is_router:1; /* sending to a router */
	unsigned int	msg_routing:1;		/* being forwarded */
	unsigned int	msg_ack:1;		/* ack on finalize (PUT) */
	unsigned int	msg_sending:1;		/* outgoing message */
	unsigned int	msg_receiving:1;	/* being received */
	unsigned int	msg_txcredit:1;		/* taken an NI send credit */
	unsigned int	msg_peertxcredit:1;	/* taken a peer send credit */
	unsigned int	msg_rtrcredit:1;	/* taken a global router credit */
	unsigned int	msg_peerrtrcredit:1;	/* taken a peer router credit */
	unsigned int	msg_onactivelist:1;	/* on the activelist */
	unsigned int	msg_rdma_get:1;

	struct lnet_peer	*msg_txpeer;	 /* peer I'm sending to */
	struct lnet_peer	*msg_rxpeer;	 /* peer I received from */

	void			*msg_private;
	struct lnet_libmd	*msg_md;

	unsigned int		 msg_len;
	unsigned int		 msg_wanted;
	unsigned int		 msg_offset;
	unsigned int		 msg_niov;
	struct kvec		*msg_iov;
	struct bio_vec		*msg_kiov;

	struct lnet_event	 msg_ev;
	struct lnet_hdr		 msg_hdr;
};

struct lnet_libhandle {
	struct list_head	lh_hash_chain;
	__u64			lh_cookie;
};

#define lh_entry(ptr, type, member) \
	((type *)((char *)(ptr) - (char *)(&((type *)0)->member)))

struct lnet_eq {
	struct list_head	  eq_list;
	struct lnet_libhandle	  eq_lh;
	unsigned long		  eq_enq_seq;
	unsigned long		  eq_deq_seq;
	unsigned int		  eq_size;
	lnet_eq_handler_t	  eq_callback;
	struct lnet_event	 *eq_events;
	int			**eq_refs;	/* percpt refcount for EQ */
};

struct lnet_me {
	struct list_head	 me_list;
	struct lnet_libhandle	 me_lh;
	struct lnet_process_id	 me_match_id;
	unsigned int		 me_portal;
	unsigned int		 me_pos;	/* hash offset in mt_hash */
	__u64			 me_match_bits;
	__u64			 me_ignore_bits;
	enum lnet_unlink	 me_unlink;
	struct lnet_libmd	*me_md;
};

struct lnet_libmd {
	struct list_head	 md_list;
	struct lnet_libhandle	 md_lh;
	struct lnet_me		*md_me;
	char			*md_start;
	unsigned int		 md_offset;
	unsigned int		 md_length;
	unsigned int		 md_max_size;
	int			 md_threshold;
	int			 md_refcount;
	unsigned int		 md_options;
	unsigned int		 md_flags;
	void			*md_user_ptr;
	struct lnet_eq		*md_eq;
	unsigned int		 md_niov;	/* # frags */
	union {
		struct kvec	iov[LNET_MAX_IOV];
		struct bio_vec	kiov[LNET_MAX_IOV];
	} md_iov;
};

#define LNET_MD_FLAG_ZOMBIE		(1 << 0)
#define LNET_MD_FLAG_AUTO_UNLINK	(1 << 1)
#define LNET_MD_FLAG_ABORTED		(1 << 2)

struct lnet_test_peer {
	/* info about peers we are trying to fail */
	struct list_head	tp_list;	/* ln_test_peers */
	lnet_nid_t		tp_nid;		/* matching nid */
	unsigned int		tp_threshold;	/* # failures to simulate */
};

#define LNET_COOKIE_TYPE_MD	1
#define LNET_COOKIE_TYPE_ME	2
#define LNET_COOKIE_TYPE_EQ	3
#define LNET_COOKIE_TYPE_BITS	2
#define LNET_COOKIE_MASK	((1ULL << LNET_COOKIE_TYPE_BITS) - 1ULL)

struct lnet_ni;			/* forward ref */

struct lnet_lnd {
	/* fields managed by portals */
	struct list_head	lnd_list;	/* stash in the LND table */
	int			lnd_refcount;	/* # active instances */

	/* fields initialised by the LND */
	__u32			lnd_type;

	int  (*lnd_startup)(struct lnet_ni *ni);
	void (*lnd_shutdown)(struct lnet_ni *ni);
	int  (*lnd_ctl)(struct lnet_ni *ni, unsigned int cmd, void *arg);

	/*
	 * In data movement APIs below, payload buffers are described as a set
	 * of 'niov' fragments which are...
	 * EITHER
	 *    in virtual memory (struct iovec *iov != NULL)
	 * OR
	 *    in pages (kernel only: plt_kiov_t *kiov != NULL).
	 * The LND may NOT overwrite these fragment descriptors.
	 * An 'offset' and may specify a byte offset within the set of
	 * fragments to start from
	 */

	/*
	 * Start sending a preformatted message.  'private' is NULL for PUT and
	 * GET messages; otherwise this is a response to an incoming message
	 * and 'private' is the 'private' passed to lnet_parse().  Return
	 * non-zero for immediate failure, otherwise complete later with
	 * lnet_finalize()
	 */
	int (*lnd_send)(struct lnet_ni *ni, void *private,
			struct lnet_msg *msg);

	/*
	 * Start receiving 'mlen' bytes of payload data, skipping the following
	 * 'rlen' - 'mlen' bytes. 'private' is the 'private' passed to
	 * lnet_parse().  Return non-zero for immediate failure, otherwise
	 * complete later with lnet_finalize().  This also gives back a receive
	 * credit if the LND does flow control.
	 */
	int (*lnd_recv)(struct lnet_ni *ni, void *private, struct lnet_msg *msg,
			int delayed, struct iov_iter *to, unsigned int rlen);

	/*
	 * lnet_parse() has had to delay processing of this message
	 * (e.g. waiting for a forwarding buffer or send credits).  Give the
	 * LND a chance to free urgently needed resources.  If called, return 0
	 * for success and do NOT give back a receive credit; that has to wait
	 * until lnd_recv() gets called.  On failure return < 0 and
	 * release resources; lnd_recv() will not be called.
	 */
	int (*lnd_eager_recv)(struct lnet_ni *ni, void *private,
			      struct lnet_msg *msg, void **new_privatep);

	/* notification of peer health */
	void (*lnd_notify)(struct lnet_ni *ni, lnet_nid_t peer, int alive);

	/* query of peer aliveness */
	void (*lnd_query)(struct lnet_ni *ni, lnet_nid_t peer,
			  unsigned long *when);

	/* accept a new connection */
	int (*lnd_accept)(struct lnet_ni *ni, struct socket *sock);
};

struct lnet_tx_queue {
	int			tq_credits;	/* # tx credits free */
	int			tq_credits_min;	/* lowest it's been */
	int			tq_credits_max;	/* total # tx credits */
	struct list_head	tq_delayed;	/* delayed TXs */
};

struct lnet_ni {
	spinlock_t		  ni_lock;
	struct list_head	  ni_list;	/* chain on ln_nis */
	struct list_head	  ni_cptlist;	/* chain on ln_nis_cpt */
	int			  ni_maxtxcredits; /* # tx credits  */
	/* # per-peer send credits */
	int			  ni_peertxcredits;
	/* # per-peer router buffer credits */
	int			  ni_peerrtrcredits;
	/* seconds to consider peer dead */
	int			  ni_peertimeout;
	int			  ni_ncpts;	/* number of CPTs */
	__u32			 *ni_cpts;	/* bond NI on some CPTs */
	lnet_nid_t		  ni_nid;	/* interface's NID */
	void			 *ni_data;	/* instance-specific data */
	struct lnet_lnd		 *ni_lnd;	/* procedural interface */
	struct lnet_tx_queue	**ni_tx_queues;	/* percpt TX queues */
	int			**ni_refs;	/* percpt reference count */
	time64_t		  ni_last_alive;/* when I was last alive */
	struct lnet_ni_status	 *ni_status;	/* my health status */
	/* per NI LND tunables */
	struct lnet_ioctl_config_lnd_tunables *ni_lnd_tunables;
	/* equivalent interfaces to use */
	char			 *ni_interfaces[LNET_MAX_INTERFACES];
	/* original net namespace */
	struct net		 *ni_net_ns;
};

#define LNET_PROTO_PING_MATCHBITS	0x8000000000000000LL

/*
 * NB: value of these features equal to LNET_PROTO_PING_VERSION_x
 * of old LNet, so there shouldn't be any compatibility issue
 */
#define LNET_PING_FEAT_INVAL		(0)		/* no feature */
#define LNET_PING_FEAT_BASE		(1 << 0)	/* just a ping */
#define LNET_PING_FEAT_NI_STATUS	(1 << 1)	/* return NI status */
#define LNET_PING_FEAT_RTE_DISABLED	(1 << 2)	/* Routing enabled */

#define LNET_PING_FEAT_MASK		(LNET_PING_FEAT_BASE | \
					 LNET_PING_FEAT_NI_STATUS)

/* router checker data, per router */
#define LNET_MAX_RTR_NIS   16
#define LNET_PINGINFO_SIZE offsetof(struct lnet_ping_info, pi_ni[LNET_MAX_RTR_NIS])
struct lnet_rc_data {
	/* chain on the_lnet.ln_zombie_rcd or ln_deathrow_rcd */
	struct list_head	 rcd_list;
	struct lnet_handle_md	 rcd_mdh;	/* ping buffer MD */
	struct lnet_peer	*rcd_gateway;	/* reference to gateway */
	struct lnet_ping_info	*rcd_pinginfo;	/* ping buffer */
};

struct lnet_peer {
	struct list_head	 lp_hashlist;	/* chain on peer hash */
	struct list_head	 lp_txq;	/* messages blocking for
						 * tx credits
						 */
	struct list_head	 lp_rtrq;	/* messages blocking for
						 * router credits
						 */
	struct list_head	 lp_rtr_list;	/* chain on router list */
	int			 lp_txcredits;	/* # tx credits available */
	int			 lp_mintxcredits;  /* low water mark */
	int			 lp_rtrcredits;	   /* # router credits */
	int			 lp_minrtrcredits; /* low water mark */
	unsigned int		 lp_alive:1;	   /* alive/dead? */
	unsigned int		 lp_notify:1;	/* notification outstanding? */
	unsigned int		 lp_notifylnd:1;/* outstanding notification
						 * for LND?
						 */
	unsigned int		 lp_notifying:1; /* some thread is handling
						  * notification
						  */
	unsigned int		 lp_ping_notsent;/* SEND event outstanding
						  * from ping
						  */
	int			 lp_alive_count; /* # times router went
						  * dead<->alive
						  */
	long			 lp_txqnob;	 /* ytes queued for sending */
	unsigned long		 lp_timestamp;	 /* time of last aliveness
						  * news
						  */
	unsigned long		 lp_ping_timestamp;/* time of last ping
						    * attempt
						    */
	unsigned long		 lp_ping_deadline; /* != 0 if ping reply
						    * expected
						    */
	unsigned long		 lp_last_alive;	/* when I was last alive */
	unsigned long		 lp_last_query;	/* when lp_ni was queried
						 * last time
						 */
	struct lnet_ni		*lp_ni;		/* interface peer is on */
	lnet_nid_t		 lp_nid;	/* peer's NID */
	int			 lp_refcount;	/* # refs */
	int			 lp_cpt;	/* CPT this peer attached on */
	/* # refs from lnet_route::lr_gateway */
	int			 lp_rtr_refcount;
	/* returned RC ping features */
	unsigned int		 lp_ping_feats;
	struct list_head	 lp_routes;	/* routers on this peer */
	struct lnet_rc_data	*lp_rcd;	/* router checker state */
};

/* peer hash size */
#define LNET_PEER_HASH_BITS	9
#define LNET_PEER_HASH_SIZE	(1 << LNET_PEER_HASH_BITS)

/* peer hash table */
struct lnet_peer_table {
	int			 pt_version;	/* /proc validity stamp */
	int			 pt_number;	/* # peers extant */
	/* # zombies to go to deathrow (and not there yet) */
	int			 pt_zombies;
	struct list_head	 pt_deathrow;	/* zombie peers */
	struct list_head	*pt_hash;	/* NID->peer hash */
};

/*
 * peer aliveness is enabled only on routers for peers in a network where the
 * lnet_ni::ni_peertimeout has been set to a positive value
 */
#define lnet_peer_aliveness_enabled(lp) (the_lnet.ln_routing && \
					 (lp)->lp_ni->ni_peertimeout > 0)

struct lnet_route {
	struct list_head	 lr_list;	/* chain on net */
	struct list_head	 lr_gwlist;	/* chain on gateway */
	struct lnet_peer	*lr_gateway;	/* router node */
	__u32			 lr_net;	/* remote network number */
	int			 lr_seq;	/* sequence for round-robin */
	unsigned int		 lr_downis;	/* number of down NIs */
	__u32			 lr_hops;	/* how far I am */
	unsigned int             lr_priority;	/* route priority */
};

#define LNET_REMOTE_NETS_HASH_DEFAULT	(1U << 7)
#define LNET_REMOTE_NETS_HASH_MAX	(1U << 16)
#define LNET_REMOTE_NETS_HASH_SIZE	(1 << the_lnet.ln_remote_nets_hbits)

struct lnet_remotenet {
	struct list_head	lrn_list;	/* chain on
						 * ln_remote_nets_hash
						 */
	struct list_head	lrn_routes;	/* routes to me */
	__u32			lrn_net;	/* my net number */
};

/** lnet message has credit and can be submitted to lnd for send/receive */
#define LNET_CREDIT_OK		0
/** lnet message is waiting for credit */
#define LNET_CREDIT_WAIT	1

struct lnet_rtrbufpool {
	struct list_head	rbp_bufs;	/* my free buffer pool */
	struct list_head	rbp_msgs;	/* messages blocking
						 * for a buffer
						 */
	int			rbp_npages;	/* # pages in each buffer */
	/* requested number of buffers */
	int			rbp_req_nbuffers;
	/* # buffers actually allocated */
	int			rbp_nbuffers;
	int			rbp_credits;	/* # free buffers
						 * blocked messages
						 */
	int			rbp_mincredits;	/* low water mark */
};

struct lnet_rtrbuf {
	struct list_head	 rb_list;	/* chain on rbp_bufs */
	struct lnet_rtrbufpool	*rb_pool;	/* owning pool */
	struct bio_vec		 rb_kiov[0];	/* the buffer space */
};

#define LNET_PEER_HASHSIZE	503	/* prime! */

#define LNET_TINY_BUF_IDX	0
#define LNET_SMALL_BUF_IDX	1
#define LNET_LARGE_BUF_IDX	2

/* # different router buffer pools */
#define LNET_NRBPOOLS		(LNET_LARGE_BUF_IDX + 1)

enum lnet_match_flags {
	/* Didn't match anything */
	LNET_MATCHMD_NONE	= (1 << 0),
	/* Matched OK */
	LNET_MATCHMD_OK		= (1 << 1),
	/* Must be discarded */
	LNET_MATCHMD_DROP	= (1 << 2),
	/* match and buffer is exhausted */
	LNET_MATCHMD_EXHAUSTED	= (1 << 3),
	/* match or drop */
	LNET_MATCHMD_FINISH	= (LNET_MATCHMD_OK | LNET_MATCHMD_DROP),
};

/* Options for lnet_portal::ptl_options */
#define LNET_PTL_LAZY		(1 << 0)
#define LNET_PTL_MATCH_UNIQUE	(1 << 1)	/* unique match, for RDMA */
#define LNET_PTL_MATCH_WILDCARD	(1 << 2)	/* wildcard match,
						 * request portal
						 */

/* parameter for matching operations (GET, PUT) */
struct lnet_match_info {
	__u64			mi_mbits;
	struct lnet_process_id	mi_id;
	unsigned int		mi_opc;
	unsigned int		mi_portal;
	unsigned int		mi_rlength;
	unsigned int		mi_roffset;
};

/* ME hash of RDMA portal */
#define LNET_MT_HASH_BITS		8
#define LNET_MT_HASH_SIZE		(1 << LNET_MT_HASH_BITS)
#define LNET_MT_HASH_MASK		(LNET_MT_HASH_SIZE - 1)
/*
 * we allocate (LNET_MT_HASH_SIZE + 1) entries for lnet_match_table::mt_hash,
 * the last entry is reserved for MEs with ignore-bits
 */
#define LNET_MT_HASH_IGNORE		LNET_MT_HASH_SIZE
/*
 * __u64 has 2^6 bits, so need 2^(LNET_MT_HASH_BITS - LNET_MT_BITS_U64) which
 * is 4 __u64s as bit-map, and add an extra __u64 (only use one bit) for the
 * ME-list with ignore-bits, which is mtable::mt_hash[LNET_MT_HASH_IGNORE]
 */
#define LNET_MT_BITS_U64		6	/* 2^6 bits */
#define LNET_MT_EXHAUSTED_BITS		(LNET_MT_HASH_BITS - LNET_MT_BITS_U64)
#define LNET_MT_EXHAUSTED_BMAP		((1 << LNET_MT_EXHAUSTED_BITS) + 1)

/* portal match table */
struct lnet_match_table {
	/* reserved for upcoming patches, CPU partition ID */
	unsigned int		 mt_cpt;
	unsigned int		 mt_portal;	/* portal index */
	/*
	 * match table is set as "enabled" if there's non-exhausted MD
	 * attached on mt_mhash, it's only valid for wildcard portal
	 */
	unsigned int		 mt_enabled;
	/* bitmap to flag whether MEs on mt_hash are exhausted or not */
	__u64			 mt_exhausted[LNET_MT_EXHAUSTED_BMAP];
	struct list_head	*mt_mhash;	/* matching hash */
};

/* these are only useful for wildcard portal */
/* Turn off message rotor for wildcard portals */
#define	LNET_PTL_ROTOR_OFF	0
/* round-robin dispatch all PUT messages for wildcard portals */
#define	LNET_PTL_ROTOR_ON	1
/* round-robin dispatch routed PUT message for wildcard portals */
#define	LNET_PTL_ROTOR_RR_RT	2
/* dispatch routed PUT message by hashing source NID for wildcard portals */
#define	LNET_PTL_ROTOR_HASH_RT	3

struct lnet_portal {
	spinlock_t		  ptl_lock;
	unsigned int		  ptl_index;	/* portal ID, reserved */
	/* flags on this portal: lazy, unique... */
	unsigned int		  ptl_options;
	/* list of messages which are stealing buffer */
	struct list_head	  ptl_msg_stealing;
	/* messages blocking for MD */
	struct list_head	  ptl_msg_delayed;
	/* Match table for each CPT */
	struct lnet_match_table	**ptl_mtables;
	/* spread rotor of incoming "PUT" */
	unsigned int		  ptl_rotor;
	/* # active entries for this portal */
	int			  ptl_mt_nmaps;
	/* array of active entries' cpu-partition-id */
	int			  ptl_mt_maps[0];
};

#define LNET_LH_HASH_BITS	12
#define LNET_LH_HASH_SIZE	(1ULL << LNET_LH_HASH_BITS)
#define LNET_LH_HASH_MASK	(LNET_LH_HASH_SIZE - 1)

/* resource container (ME, MD, EQ) */
struct lnet_res_container {
	unsigned int		 rec_type;	/* container type */
	__u64			 rec_lh_cookie;	/* cookie generator */
	struct list_head	 rec_active;	/* active resource list */
	struct list_head	*rec_lh_hash;	/* handle hash */
};

/* message container */
struct lnet_msg_container {
	int			  msc_init;	/* initialized or not */
	/* max # threads finalizing */
	int			  msc_nfinalizers;
	/* msgs waiting to complete finalizing */
	struct list_head	  msc_finalizing;
	struct list_head	  msc_active;	/* active message list */
	/* threads doing finalization */
	void			**msc_finalizers;
};

/* Router Checker states */
#define LNET_RC_STATE_SHUTDOWN		0	/* not started */
#define LNET_RC_STATE_RUNNING		1	/* started up OK */
#define LNET_RC_STATE_STOPPING		2	/* telling thread to stop */

struct lnet {
	/* CPU partition table of LNet */
	struct cfs_cpt_table		 *ln_cpt_table;
	/* number of CPTs in ln_cpt_table */
	unsigned int			  ln_cpt_number;
	unsigned int			  ln_cpt_bits;

	/* protect LNet resources (ME/MD/EQ) */
	struct cfs_percpt_lock		 *ln_res_lock;
	/* # portals */
	int				  ln_nportals;
	/* the vector of portals */
	struct lnet_portal		**ln_portals;
	/* percpt ME containers */
	struct lnet_res_container	**ln_me_containers;
	/* percpt MD container */
	struct lnet_res_container	**ln_md_containers;

	/* Event Queue container */
	struct lnet_res_container	  ln_eq_container;
	wait_queue_head_t		  ln_eq_waitq;
	spinlock_t			  ln_eq_wait_lock;
	unsigned int			  ln_remote_nets_hbits;

	/* protect NI, peer table, credits, routers, rtrbuf... */
	struct cfs_percpt_lock		 *ln_net_lock;
	/* percpt message containers for active/finalizing/freed message */
	struct lnet_msg_container	**ln_msg_containers;
	struct lnet_counters		**ln_counters;
	struct lnet_peer_table		**ln_peer_tables;
	/* failure simulation */
	struct list_head		  ln_test_peers;
	struct list_head		  ln_drop_rules;
	struct list_head		  ln_delay_rules;

	struct list_head		  ln_nis;	/* LND instances */
	/* NIs bond on specific CPT(s) */
	struct list_head		  ln_nis_cpt;
	/* dying LND instances */
	struct list_head		  ln_nis_zombie;
	struct lnet_ni			 *ln_loni;	/* the loopback NI */

	/* remote networks with routes to them */
	struct list_head		 *ln_remote_nets_hash;
	/* validity stamp */
	__u64				  ln_remote_nets_version;
	/* list of all known routers */
	struct list_head		  ln_routers;
	/* validity stamp */
	__u64				  ln_routers_version;
	/* percpt router buffer pools */
	struct lnet_rtrbufpool		**ln_rtrpools;

	struct lnet_handle_md		  ln_ping_target_md;
	struct lnet_handle_eq		  ln_ping_target_eq;
	struct lnet_ping_info		 *ln_ping_info;

	/* router checker startup/shutdown state */
	int				  ln_rc_state;
	/* router checker's event queue */
	struct lnet_handle_eq		  ln_rc_eqh;
	/* rcd still pending on net */
	struct list_head		  ln_rcd_deathrow;
	/* rcd ready for free */
	struct list_head		  ln_rcd_zombie;
	/* serialise startup/shutdown */
	struct completion		  ln_rc_signal;

	struct mutex			  ln_api_mutex;
	struct mutex			  ln_lnd_mutex;
	struct mutex			  ln_delay_mutex;
	/* Have I called LNetNIInit myself? */
	int				  ln_niinit_self;
	/* LNetNIInit/LNetNIFini counter */
	int				  ln_refcount;
	/* shutdown in progress */
	int				  ln_shutdown;

	int				  ln_routing;	/* am I a router? */
	lnet_pid_t			  ln_pid;	/* requested pid */
	/* uniquely identifies this ni in this epoch */
	__u64				  ln_interface_cookie;
	/* registered LNDs */
	struct list_head		  ln_lnds;

	/* test protocol compatibility flags */
	int				  ln_testprotocompat;

	/*
	 * 0 - load the NIs from the mod params
	 * 1 - do not load the NIs from the mod params
	 * Reverse logic to ensure that other calls to LNetNIInit
	 * need no change
	 */
	bool				  ln_nis_from_mod_params;

	/*
	 * waitq for router checker.  As long as there are no routes in
	 * the list, the router checker will sleep on this queue.  when
	 * routes are added the thread will wake up
	 */
	wait_queue_head_t		  ln_rc_waitq;

};

#endif
