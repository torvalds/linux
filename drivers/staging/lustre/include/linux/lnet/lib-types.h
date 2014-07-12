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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/include/lnet/lib-types.h
 *
 * Types used by the library side routines that do not need to be
 * exposed to the user application
 */

#ifndef __LNET_LIB_TYPES_H__
#define __LNET_LIB_TYPES_H__

#include "linux/lib-types.h"

#include "../libcfs/libcfs.h"
#include <linux/list.h>
#include "types.h"

#define WIRE_ATTR       __attribute__((packed))

/* Packed version of lnet_process_id_t to transfer via network */
typedef struct {
	lnet_nid_t nid;
	lnet_pid_t pid;   /* node id / process id */
} WIRE_ATTR lnet_process_id_packed_t;

/* The wire handle's interface cookie only matches one network interface in
 * one epoch (i.e. new cookie when the interface restarts or the node
 * reboots).  The object cookie only matches one object on that interface
 * during that object's lifetime (i.e. no cookie re-use). */
typedef struct {
	__u64 wh_interface_cookie;
	__u64 wh_object_cookie;
} WIRE_ATTR lnet_handle_wire_t;

typedef enum {
	LNET_MSG_ACK = 0,
	LNET_MSG_PUT,
	LNET_MSG_GET,
	LNET_MSG_REPLY,
	LNET_MSG_HELLO,
} lnet_msg_type_t;

/* The variant fields of the portals message header are aligned on an 8
 * byte boundary in the message header.  Note that all types used in these
 * wire structs MUST be fixed size and the smaller types are placed at the
 * end. */
typedef struct lnet_ack {
	lnet_handle_wire_t  dst_wmd;
	__u64	       match_bits;
	__u32	       mlength;
} WIRE_ATTR lnet_ack_t;

typedef struct lnet_put {
	lnet_handle_wire_t  ack_wmd;
	__u64	       match_bits;
	__u64	       hdr_data;
	__u32	       ptl_index;
	__u32	       offset;
} WIRE_ATTR lnet_put_t;

typedef struct lnet_get {
	lnet_handle_wire_t  return_wmd;
	__u64	       match_bits;
	__u32	       ptl_index;
	__u32	       src_offset;
	__u32	       sink_length;
} WIRE_ATTR lnet_get_t;

typedef struct lnet_reply {
	lnet_handle_wire_t  dst_wmd;
} WIRE_ATTR lnet_reply_t;

typedef struct lnet_hello {
	__u64	      incarnation;
	__u32	      type;
} WIRE_ATTR lnet_hello_t;

typedef struct {
	lnet_nid_t	  dest_nid;
	lnet_nid_t	  src_nid;
	lnet_pid_t	  dest_pid;
	lnet_pid_t	  src_pid;
	__u32	       type;	       /* lnet_msg_type_t */
	__u32	       payload_length;     /* payload data to follow */
	/*<------__u64 aligned------->*/
	union {
		lnet_ack_t   ack;
		lnet_put_t   put;
		lnet_get_t   get;
		lnet_reply_t reply;
		lnet_hello_t hello;
	} msg;
} WIRE_ATTR lnet_hdr_t;

/* A HELLO message contains a magic number and protocol version
 * code in the header's dest_nid, the peer's NID in the src_nid, and
 * LNET_MSG_HELLO in the type field.  All other common fields are zero
 * (including payload_size; i.e. no payload).
 * This is for use by byte-stream LNDs (e.g. TCP/IP) to check the peer is
 * running the same protocol and to find out its NID. These LNDs should
 * exchange HELLO messages when a connection is first established.  Individual
 * LNDs can put whatever else they fancy in lnet_hdr_t::msg.
 */
typedef struct {
	__u32   magic;			  /* LNET_PROTO_TCP_MAGIC */
	__u16   version_major;		  /* increment on incompatible change */
	__u16   version_minor;		  /* increment on compatible change */
} WIRE_ATTR lnet_magicversion_t;

/* PROTO MAGIC for LNDs */
#define LNET_PROTO_IB_MAGIC		 0x0be91b91
#define LNET_PROTO_RA_MAGIC		 0x0be91b92
#define LNET_PROTO_QSW_MAGIC		0x0be91b93
#define LNET_PROTO_GNI_MAGIC		0xb00fbabe /* ask Kim */
#define LNET_PROTO_TCP_MAGIC		0xeebc0ded
#define LNET_PROTO_PTL_MAGIC		0x50746C4E /* 'PtlN' unique magic */
#define LNET_PROTO_MX_MAGIC		 0x4d583130 /* 'MX10'! */
#define LNET_PROTO_ACCEPTOR_MAGIC	   0xacce7100
#define LNET_PROTO_PING_MAGIC	       0x70696E67 /* 'ping' */

/* Placeholder for a future "unified" protocol across all LNDs */
/* Current LNDs that receive a request with this magic will respond with a
 * "stub" reply using their current protocol */
#define LNET_PROTO_MAGIC		    0x45726963 /* ! */

#define LNET_PROTO_TCP_VERSION_MAJOR	1
#define LNET_PROTO_TCP_VERSION_MINOR	0

/* Acceptor connection request */
typedef struct {
	__u32       acr_magic;		  /* PTL_ACCEPTOR_PROTO_MAGIC */
	__u32       acr_version;		/* protocol version */
	__u64       acr_nid;		    /* target NID */
} WIRE_ATTR lnet_acceptor_connreq_t;

#define LNET_PROTO_ACCEPTOR_VERSION       1

/* forward refs */
struct lnet_libmd;

typedef struct lnet_msg {
	struct list_head	    msg_activelist;
	struct list_head	    msg_list;	   /* Q for credits/MD */

	lnet_process_id_t     msg_target;
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

	unsigned int	  msg_vmflush:1;      /* VM trying to free memory */
	unsigned int	  msg_target_is_router:1; /* sending to a router */
	unsigned int	  msg_routing:1;      /* being forwarded */
	unsigned int	  msg_ack:1;	  /* ack on finalize (PUT) */
	unsigned int	  msg_sending:1;      /* outgoing message */
	unsigned int	  msg_receiving:1;    /* being received */
	unsigned int	  msg_txcredit:1;     /* taken an NI send credit */
	unsigned int	  msg_peertxcredit:1; /* taken a peer send credit */
	unsigned int	  msg_rtrcredit:1;    /* taken a global router credit */
	unsigned int	  msg_peerrtrcredit:1; /* taken a peer router credit */
	unsigned int	  msg_onactivelist:1; /* on the activelist */

	struct lnet_peer     *msg_txpeer;	 /* peer I'm sending to */
	struct lnet_peer     *msg_rxpeer;	 /* peer I received from */

	void		 *msg_private;
	struct lnet_libmd    *msg_md;

	unsigned int	  msg_len;
	unsigned int	  msg_wanted;
	unsigned int	  msg_offset;
	unsigned int	  msg_niov;
	struct iovec	 *msg_iov;
	lnet_kiov_t	  *msg_kiov;

	lnet_event_t	  msg_ev;
	lnet_hdr_t	    msg_hdr;
} lnet_msg_t;

typedef struct lnet_libhandle {
	struct list_head	    lh_hash_chain;
	__u64		 lh_cookie;
} lnet_libhandle_t;

#define lh_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))

typedef struct lnet_eq {
	struct list_head		eq_list;
	lnet_libhandle_t	eq_lh;
	lnet_seq_t		eq_enq_seq;
	lnet_seq_t		eq_deq_seq;
	unsigned int		eq_size;
	lnet_eq_handler_t	eq_callback;
	lnet_event_t		*eq_events;
	int			**eq_refs;	/* percpt refcount for EQ */
} lnet_eq_t;

typedef struct lnet_me {
	struct list_head	     me_list;
	lnet_libhandle_t       me_lh;
	lnet_process_id_t      me_match_id;
	unsigned int	   me_portal;
	unsigned int	   me_pos;		/* hash offset in mt_hash */
	__u64		  me_match_bits;
	__u64		  me_ignore_bits;
	lnet_unlink_t	  me_unlink;
	struct lnet_libmd     *me_md;
} lnet_me_t;

typedef struct lnet_libmd {
	struct list_head	    md_list;
	lnet_libhandle_t      md_lh;
	lnet_me_t	    *md_me;
	char		 *md_start;
	unsigned int	  md_offset;
	unsigned int	  md_length;
	unsigned int	  md_max_size;
	int		   md_threshold;
	int		   md_refcount;
	unsigned int	  md_options;
	unsigned int	  md_flags;
	void		 *md_user_ptr;
	lnet_eq_t	    *md_eq;
	unsigned int	  md_niov;		/* # frags */
	union {
		struct iovec  iov[LNET_MAX_IOV];
		lnet_kiov_t   kiov[LNET_MAX_IOV];
	} md_iov;
} lnet_libmd_t;

#define LNET_MD_FLAG_ZOMBIE	   (1 << 0)
#define LNET_MD_FLAG_AUTO_UNLINK      (1 << 1)
#define LNET_MD_FLAG_ABORTED	 (1 << 2)

#ifdef LNET_USE_LIB_FREELIST
typedef struct {
	void		  *fl_objs;	  /* single contiguous array of objects */
	int		    fl_nobjs;	 /* the number of them */
	int		    fl_objsize;       /* the size (including overhead) of each of them */
	struct list_head	     fl_list;	  /* where they are enqueued */
} lnet_freelist_t;

typedef struct {
	struct list_head	     fo_list;	     /* enqueue on fl_list */
	void		  *fo_contents;	 /* aligned contents */
} lnet_freeobj_t;
#endif

typedef struct {
	/* info about peers we are trying to fail */
	struct list_head	     tp_list;	     /* ln_test_peers */
	lnet_nid_t	     tp_nid;	      /* matching nid */
	unsigned int	   tp_threshold;	/* # failures to simulate */
} lnet_test_peer_t;

#define LNET_COOKIE_TYPE_MD    1
#define LNET_COOKIE_TYPE_ME    2
#define LNET_COOKIE_TYPE_EQ    3
#define LNET_COOKIE_TYPE_BITS  2
#define LNET_COOKIE_MASK	((1ULL << LNET_COOKIE_TYPE_BITS) - 1ULL)

struct lnet_ni;				  /* forward ref */

typedef struct lnet_lnd {
	/* fields managed by portals */
	struct list_head	    lnd_list;	     /* stash in the LND table */
	int		   lnd_refcount;	 /* # active instances */

	/* fields initialised by the LND */
	unsigned int	  lnd_type;

	int  (*lnd_startup)(struct lnet_ni *ni);
	void (*lnd_shutdown)(struct lnet_ni *ni);
	int  (*lnd_ctl)(struct lnet_ni *ni, unsigned int cmd, void *arg);

	/* In data movement APIs below, payload buffers are described as a set
	 * of 'niov' fragments which are...
	 * EITHER
	 *    in virtual memory (struct iovec *iov != NULL)
	 * OR
	 *    in pages (kernel only: plt_kiov_t *kiov != NULL).
	 * The LND may NOT overwrite these fragment descriptors.
	 * An 'offset' and may specify a byte offset within the set of
	 * fragments to start from
	 */

	/* Start sending a preformatted message.  'private' is NULL for PUT and
	 * GET messages; otherwise this is a response to an incoming message
	 * and 'private' is the 'private' passed to lnet_parse().  Return
	 * non-zero for immediate failure, otherwise complete later with
	 * lnet_finalize() */
	int (*lnd_send)(struct lnet_ni *ni, void *private, lnet_msg_t *msg);

	/* Start receiving 'mlen' bytes of payload data, skipping the following
	 * 'rlen' - 'mlen' bytes. 'private' is the 'private' passed to
	 * lnet_parse().  Return non-zero for immediate failure, otherwise
	 * complete later with lnet_finalize().  This also gives back a receive
	 * credit if the LND does flow control. */
	int (*lnd_recv)(struct lnet_ni *ni, void *private, lnet_msg_t *msg,
			int delayed, unsigned int niov,
			struct iovec *iov, lnet_kiov_t *kiov,
			unsigned int offset, unsigned int mlen, unsigned int rlen);

	/* lnet_parse() has had to delay processing of this message
	 * (e.g. waiting for a forwarding buffer or send credits).  Give the
	 * LND a chance to free urgently needed resources.  If called, return 0
	 * for success and do NOT give back a receive credit; that has to wait
	 * until lnd_recv() gets called.  On failure return < 0 and
	 * release resources; lnd_recv() will not be called. */
	int (*lnd_eager_recv)(struct lnet_ni *ni, void *private, lnet_msg_t *msg,
			      void **new_privatep);

	/* notification of peer health */
	void (*lnd_notify)(struct lnet_ni *ni, lnet_nid_t peer, int alive);

	/* query of peer aliveness */
	void (*lnd_query)(struct lnet_ni *ni, lnet_nid_t peer, cfs_time_t *when);

	/* accept a new connection */
	int (*lnd_accept)(struct lnet_ni *ni, socket_t *sock);

} lnd_t;

#define LNET_NI_STATUS_UP      0x15aac0de
#define LNET_NI_STATUS_DOWN    0xdeadface
#define LNET_NI_STATUS_INVALID 0x00000000
typedef struct {
	lnet_nid_t ns_nid;
	__u32      ns_status;
	__u32      ns_unused;
} WIRE_ATTR lnet_ni_status_t;

struct lnet_tx_queue {
	int			tq_credits;	/* # tx credits free */
	int			tq_credits_min;	/* lowest it's been */
	int			tq_credits_max;	/* total # tx credits */
	struct list_head		tq_delayed;	/* delayed TXs */
};

#define LNET_MAX_INTERFACES   16

typedef struct lnet_ni {
	spinlock_t		ni_lock;
	struct list_head		ni_list;	/* chain on ln_nis */
	struct list_head		ni_cptlist;	/* chain on ln_nis_cpt */
	int			ni_maxtxcredits; /* # tx credits  */
	/* # per-peer send credits */
	int			ni_peertxcredits;
	/* # per-peer router buffer credits */
	int			ni_peerrtrcredits;
	/* seconds to consider peer dead */
	int			ni_peertimeout;
	int			ni_ncpts;	/* number of CPTs */
	__u32			*ni_cpts;	/* bond NI on some CPTs */
	lnet_nid_t		ni_nid;		/* interface's NID */
	void			*ni_data;	/* instance-specific data */
	lnd_t			*ni_lnd;	/* procedural interface */
	struct lnet_tx_queue	**ni_tx_queues;	/* percpt TX queues */
	int			**ni_refs;	/* percpt reference count */
	long			ni_last_alive;	/* when I was last alive */
	lnet_ni_status_t	*ni_status;	/* my health status */
	/* equivalent interfaces to use */
	char			*ni_interfaces[LNET_MAX_INTERFACES];
} lnet_ni_t;

#define LNET_PROTO_PING_MATCHBITS	0x8000000000000000LL

/* NB: value of these features equal to LNET_PROTO_PING_VERSION_x
 * of old LNet, so there shouldn't be any compatibility issue */
#define LNET_PING_FEAT_INVAL		(0)		/* no feature */
#define LNET_PING_FEAT_BASE		(1 << 0)	/* just a ping */
#define LNET_PING_FEAT_NI_STATUS	(1 << 1)	/* return NI status */

#define LNET_PING_FEAT_MASK		(LNET_PING_FEAT_BASE | \
					 LNET_PING_FEAT_NI_STATUS)

typedef struct {
	__u32			pi_magic;
	__u32			pi_features;
	lnet_pid_t		pi_pid;
	__u32			pi_nnis;
	lnet_ni_status_t	pi_ni[0];
} WIRE_ATTR lnet_ping_info_t;

/* router checker data, per router */
#define LNET_MAX_RTR_NIS   16
#define LNET_PINGINFO_SIZE offsetof(lnet_ping_info_t, pi_ni[LNET_MAX_RTR_NIS])
typedef struct {
	/* chain on the_lnet.ln_zombie_rcd or ln_deathrow_rcd */
	struct list_head		rcd_list;
	lnet_handle_md_t	rcd_mdh;	/* ping buffer MD */
	struct lnet_peer	*rcd_gateway;	/* reference to gateway */
	lnet_ping_info_t	*rcd_pinginfo;	/* ping buffer */
} lnet_rc_data_t;

typedef struct lnet_peer {
	struct list_head	lp_hashlist;	  /* chain on peer hash */
	struct list_head	lp_txq;	       /* messages blocking for tx credits */
	struct list_head	lp_rtrq;	      /* messages blocking for router credits */
	struct list_head	lp_rtr_list;	  /* chain on router list */
	int	       lp_txcredits;	 /* # tx credits available */
	int	       lp_mintxcredits;      /* low water mark */
	int	       lp_rtrcredits;	/* # router credits */
	int	       lp_minrtrcredits;     /* low water mark */
	unsigned int      lp_alive:1;	   /* alive/dead? */
	unsigned int      lp_notify:1;	  /* notification outstanding? */
	unsigned int      lp_notifylnd:1;       /* outstanding notification for LND? */
	unsigned int      lp_notifying:1;       /* some thread is handling notification */
	unsigned int      lp_ping_notsent;      /* SEND event outstanding from ping */
	int	       lp_alive_count;       /* # times router went dead<->alive */
	long	      lp_txqnob;	    /* bytes queued for sending */
	cfs_time_t	lp_timestamp;	 /* time of last aliveness news */
	cfs_time_t	lp_ping_timestamp;    /* time of last ping attempt */
	cfs_time_t	lp_ping_deadline;     /* != 0 if ping reply expected */
	cfs_time_t	lp_last_alive;	/* when I was last alive */
	cfs_time_t	lp_last_query;	/* when lp_ni was queried last time */
	lnet_ni_t	*lp_ni;		/* interface peer is on */
	lnet_nid_t	lp_nid;	       /* peer's NID */
	int	       lp_refcount;	  /* # refs */
	int			lp_cpt;		/* CPT this peer attached on */
	/* # refs from lnet_route_t::lr_gateway */
	int			lp_rtr_refcount;
	/* returned RC ping features */
	unsigned int		lp_ping_feats;
	struct list_head		lp_routes;	/* routers on this peer */
	lnet_rc_data_t		*lp_rcd;	/* router checker state */
} lnet_peer_t;

/* peer hash size */
#define LNET_PEER_HASH_BITS     9
#define LNET_PEER_HASH_SIZE     (1 << LNET_PEER_HASH_BITS)

/* peer hash table */
struct lnet_peer_table {
	int			pt_version;	/* /proc validity stamp */
	int			pt_number;	/* # peers extant */
	struct list_head		pt_deathrow;	/* zombie peers */
	struct list_head		*pt_hash;	/* NID->peer hash */
};

/* peer aliveness is enabled only on routers for peers in a network where the
 * lnet_ni_t::ni_peertimeout has been set to a positive value */
#define lnet_peer_aliveness_enabled(lp) (the_lnet.ln_routing != 0 && \
					 (lp)->lp_ni->ni_peertimeout > 0)

typedef struct {
	struct list_head		lr_list;	/* chain on net */
	struct list_head		lr_gwlist;	/* chain on gateway */
	lnet_peer_t		*lr_gateway;	/* router node */
	__u32			lr_net;		/* remote network number */
	int			lr_seq;		/* sequence for round-robin */
	unsigned int		lr_downis;	/* number of down NIs */
	unsigned int		lr_hops;	/* how far I am */
	unsigned int            lr_priority;    /* route priority */
} lnet_route_t;

#define LNET_REMOTE_NETS_HASH_DEFAULT	(1U << 7)
#define LNET_REMOTE_NETS_HASH_MAX	(1U << 16)
#define LNET_REMOTE_NETS_HASH_SIZE	(1 << the_lnet.ln_remote_nets_hbits)

typedef struct {
	struct list_head	      lrn_list;       /* chain on ln_remote_nets_hash */
	struct list_head	      lrn_routes;     /* routes to me */
	__u32		   lrn_net;	/* my net number */
} lnet_remotenet_t;

typedef struct {
	struct list_head rbp_bufs;	     /* my free buffer pool */
	struct list_head rbp_msgs;	     /* messages blocking for a buffer */
	int	rbp_npages;	   /* # pages in each buffer */
	int	rbp_nbuffers;	 /* # buffers */
	int	rbp_credits;	  /* # free buffers / blocked messages */
	int	rbp_mincredits;       /* low water mark */
} lnet_rtrbufpool_t;

typedef struct {
	struct list_head	     rb_list;	     /* chain on rbp_bufs */
	lnet_rtrbufpool_t     *rb_pool;	     /* owning pool */
	lnet_kiov_t	    rb_kiov[0];	  /* the buffer space */
} lnet_rtrbuf_t;

typedef struct {
	__u32	msgs_alloc;
	__u32	msgs_max;
	__u32	errors;
	__u32	send_count;
	__u32	recv_count;
	__u32	route_count;
	__u32	drop_count;
	__u64	send_length;
	__u64	recv_length;
	__u64	route_length;
	__u64	drop_length;
} WIRE_ATTR lnet_counters_t;

#define LNET_PEER_HASHSIZE   503		/* prime! */

#define LNET_NRBPOOLS	 3		 /* # different router buffer pools */

enum {
	/* Didn't match anything */
	LNET_MATCHMD_NONE	= (1 << 0),
	/* Matched OK */
	LNET_MATCHMD_OK		= (1 << 1),
	/* Must be discarded */
	LNET_MATCHMD_DROP	= (1 << 2),
	/* match and buffer is exhausted */
	LNET_MATCHMD_EXHAUSTED  = (1 << 3),
	/* match or drop */
	LNET_MATCHMD_FINISH     = (LNET_MATCHMD_OK | LNET_MATCHMD_DROP),
};

/* Options for lnet_portal_t::ptl_options */
#define LNET_PTL_LAZY	       (1 << 0)
#define LNET_PTL_MATCH_UNIQUE       (1 << 1)    /* unique match, for RDMA */
#define LNET_PTL_MATCH_WILDCARD     (1 << 2)    /* wildcard match, request portal */

/* parameter for matching operations (GET, PUT) */
struct lnet_match_info {
	__u64			mi_mbits;
	lnet_process_id_t	mi_id;
	unsigned int		mi_opc;
	unsigned int		mi_portal;
	unsigned int		mi_rlength;
	unsigned int		mi_roffset;
};

/* ME hash of RDMA portal */
#define LNET_MT_HASH_BITS		8
#define LNET_MT_HASH_SIZE		(1 << LNET_MT_HASH_BITS)
#define LNET_MT_HASH_MASK		(LNET_MT_HASH_SIZE - 1)
/* we allocate (LNET_MT_HASH_SIZE + 1) entries for lnet_match_table::mt_hash,
 * the last entry is reserved for MEs with ignore-bits */
#define LNET_MT_HASH_IGNORE		LNET_MT_HASH_SIZE
/* __u64 has 2^6 bits, so need 2^(LNET_MT_HASH_BITS - LNET_MT_BITS_U64) which
 * is 4 __u64s as bit-map, and add an extra __u64 (only use one bit) for the
 * ME-list with ignore-bits, which is mtable::mt_hash[LNET_MT_HASH_IGNORE] */
#define LNET_MT_BITS_U64		6	/* 2^6 bits */
#define LNET_MT_EXHAUSTED_BITS		(LNET_MT_HASH_BITS - LNET_MT_BITS_U64)
#define LNET_MT_EXHAUSTED_BMAP		((1 << LNET_MT_EXHAUSTED_BITS) + 1)

/* portal match table */
struct lnet_match_table {
	/* reserved for upcoming patches, CPU partition ID */
	unsigned int		mt_cpt;
	unsigned int		mt_portal;      /* portal index */
	/* match table is set as "enabled" if there's non-exhausted MD
	 * attached on mt_mhash, it's only valid for wildcard portal */
	unsigned int		mt_enabled;
	/* bitmap to flag whether MEs on mt_hash are exhausted or not */
	__u64			mt_exhausted[LNET_MT_EXHAUSTED_BMAP];
	struct list_head		*mt_mhash;      /* matching hash */
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

typedef struct lnet_portal {
	spinlock_t		ptl_lock;
	unsigned int		ptl_index;	/* portal ID, reserved */
	/* flags on this portal: lazy, unique... */
	unsigned int		ptl_options;
	/* list of messages which are stealing buffer */
	struct list_head		ptl_msg_stealing;
	/* messages blocking for MD */
	struct list_head		ptl_msg_delayed;
	/* Match table for each CPT */
	struct lnet_match_table	**ptl_mtables;
	/* spread rotor of incoming "PUT" */
	int			ptl_rotor;
	/* # active entries for this portal */
	int		     ptl_mt_nmaps;
	/* array of active entries' cpu-partition-id */
	int		     ptl_mt_maps[0];
} lnet_portal_t;

#define LNET_LH_HASH_BITS	12
#define LNET_LH_HASH_SIZE	(1ULL << LNET_LH_HASH_BITS)
#define LNET_LH_HASH_MASK	(LNET_LH_HASH_SIZE - 1)

/* resource container (ME, MD, EQ) */
struct lnet_res_container {
	unsigned int		rec_type;	/* container type */
	__u64			rec_lh_cookie;	/* cookie generator */
	struct list_head		rec_active;	/* active resource list */
	struct list_head		*rec_lh_hash;	/* handle hash */
#ifdef LNET_USE_LIB_FREELIST
	lnet_freelist_t		rec_freelist;	/* freelist for resources */
#endif
};

/* message container */
struct lnet_msg_container {
	int			msc_init;	/* initialized or not */
	/* max # threads finalizing */
	int			msc_nfinalizers;
	/* msgs waiting to complete finalizing */
	struct list_head		msc_finalizing;
	struct list_head		msc_active;	/* active message list */
	/* threads doing finalization */
	void			**msc_finalizers;
#ifdef LNET_USE_LIB_FREELIST
	lnet_freelist_t		msc_freelist;	/* freelist for messages */
#endif
};

/* Router Checker states */
#define LNET_RC_STATE_SHUTDOWN		0	/* not started */
#define LNET_RC_STATE_RUNNING		1	/* started up OK */
#define LNET_RC_STATE_STOPPING		2	/* telling thread to stop */

typedef struct {
	/* CPU partition table of LNet */
	struct cfs_cpt_table		*ln_cpt_table;
	/* number of CPTs in ln_cpt_table */
	unsigned int			ln_cpt_number;
	unsigned int			ln_cpt_bits;

	/* protect LNet resources (ME/MD/EQ) */
	struct cfs_percpt_lock		*ln_res_lock;
	/* # portals */
	int				ln_nportals;
	/* the vector of portals */
	lnet_portal_t			**ln_portals;
	/* percpt ME containers */
	struct lnet_res_container	**ln_me_containers;
	/* percpt MD container */
	struct lnet_res_container	**ln_md_containers;

	/* Event Queue container */
	struct lnet_res_container	ln_eq_container;
	wait_queue_head_t			ln_eq_waitq;
	spinlock_t			ln_eq_wait_lock;
	unsigned int			ln_remote_nets_hbits;

	/* protect NI, peer table, credits, routers, rtrbuf... */
	struct cfs_percpt_lock		*ln_net_lock;
	/* percpt message containers for active/finalizing/freed message */
	struct lnet_msg_container	**ln_msg_containers;
	lnet_counters_t			**ln_counters;
	struct lnet_peer_table		**ln_peer_tables;
	/* failure simulation */
	struct list_head			ln_test_peers;

	struct list_head			ln_nis;		/* LND instances */
	/* NIs bond on specific CPT(s) */
	struct list_head			ln_nis_cpt;
	/* dying LND instances */
	struct list_head			ln_nis_zombie;
	lnet_ni_t			*ln_loni;	/* the loopback NI */
	/* NI to wait for events in */
	lnet_ni_t			*ln_eq_waitni;

	/* remote networks with routes to them */
	struct list_head			*ln_remote_nets_hash;
	/* validity stamp */
	__u64				ln_remote_nets_version;
	/* list of all known routers */
	struct list_head			ln_routers;
	/* validity stamp */
	__u64				ln_routers_version;
	/* percpt router buffer pools */
	lnet_rtrbufpool_t		**ln_rtrpools;

	lnet_handle_md_t		ln_ping_target_md;
	lnet_handle_eq_t		ln_ping_target_eq;
	lnet_ping_info_t		*ln_ping_info;

	/* router checker startup/shutdown state */
	int				ln_rc_state;
	/* router checker's event queue */
	lnet_handle_eq_t		ln_rc_eqh;
	/* rcd still pending on net */
	struct list_head			ln_rcd_deathrow;
	/* rcd ready for free */
	struct list_head			ln_rcd_zombie;
	/* serialise startup/shutdown */
	struct semaphore		ln_rc_signal;

	struct mutex			ln_api_mutex;
	struct mutex			ln_lnd_mutex;
	int				ln_init;	/* LNetInit() called? */
	/* Have I called LNetNIInit myself? */
	int				ln_niinit_self;
	/* LNetNIInit/LNetNIFini counter */
	int				ln_refcount;
	/* shutdown in progress */
	int				ln_shutdown;

	int				ln_routing;	/* am I a router? */
	lnet_pid_t			ln_pid;		/* requested pid */
	/* uniquely identifies this ni in this epoch */
	__u64				ln_interface_cookie;
	/* registered LNDs */
	struct list_head			ln_lnds;

	/* space for network names */
	char				*ln_network_tokens;
	int				ln_network_tokens_nob;
	/* test protocol compatibility flags */
	int				ln_testprotocompat;

} lnet_t;

#endif
