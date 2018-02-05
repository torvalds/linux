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
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/klnds/o2iblnd/o2iblnd.h
 *
 * Author: Eric Barton <eric@bartonsoftware.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/uio.h>
#include <linux/uaccess.h>

#include <linux/io.h>

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/kmod.h>
#include <linux/sysctl.h>
#include <linux/pci.h>

#include <net/sock.h>
#include <linux/in.h>

#include <rdma/rdma_cm.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_fmr_pool.h>

#define DEBUG_SUBSYSTEM S_LND

#include <linux/libcfs/libcfs.h>
#include <linux/lnet/lib-lnet.h>

#define IBLND_PEER_HASH_SIZE		101	/* # peer lists */
/* # scheduler loops before reschedule */
#define IBLND_RESCHED			100

#define IBLND_N_SCHED			2
#define IBLND_N_SCHED_HIGH		4

struct kib_tunables {
	int *kib_dev_failover;           /* HCA failover */
	unsigned int *kib_service;       /* IB service number */
	int *kib_min_reconnect_interval; /* first failed connection retry... */
	int *kib_max_reconnect_interval; /* exponentially increasing to this */
	int *kib_cksum;                  /* checksum struct kib_msg? */
	int *kib_timeout;                /* comms timeout (seconds) */
	int *kib_keepalive;              /* keepalive timeout (seconds) */
	int *kib_ntx;                    /* # tx descs */
	char **kib_default_ipif;         /* default IPoIB interface */
	int *kib_retry_count;
	int *kib_rnr_retry_count;
	int *kib_ib_mtu;                 /* IB MTU */
	int *kib_require_priv_port;      /* accept only privileged ports */
	int *kib_use_priv_port; /* use privileged port for active connect */
	int *kib_nscheds;                /* # threads on each CPT */
};

extern struct kib_tunables  kiblnd_tunables;

#define IBLND_MSG_QUEUE_SIZE_V1   8 /* V1 only : # messages/RDMAs in-flight */
#define IBLND_CREDIT_HIGHWATER_V1 7 /* V1 only : when eagerly to return credits */

#define IBLND_CREDITS_DEFAULT     8 /* default # of peer credits */
#define IBLND_CREDITS_MAX	  ((typeof(((struct kib_msg *)0)->ibm_credits)) - 1)  /* Max # of peer credits */

/* when eagerly to return credits */
#define IBLND_CREDITS_HIGHWATER(t, v)	((v) == IBLND_MSG_VERSION_1 ? \
					IBLND_CREDIT_HIGHWATER_V1 : \
					t->lnd_peercredits_hiw)

#define kiblnd_rdma_create_id(cb, dev, ps, qpt) rdma_create_id(current->nsproxy->net_ns, \
							       cb, dev, \
							       ps, qpt)

/* 2 OOB shall suffice for 1 keepalive and 1 returning credits */
#define IBLND_OOB_CAPABLE(v)       ((v) != IBLND_MSG_VERSION_1)
#define IBLND_OOB_MSGS(v)	   (IBLND_OOB_CAPABLE(v) ? 2 : 0)

#define IBLND_FRAG_SHIFT	(PAGE_SHIFT - 12)	/* frag size on wire is in 4K units */
#define IBLND_MSG_SIZE		(4 << 10)		/* max size of queued messages (inc hdr) */
#define IBLND_MAX_RDMA_FRAGS	(LNET_MAX_PAYLOAD >> 12)/* max # of fragments supported in 4K size */

/************************/
/* derived constants... */
/* Pools (shared by connections on each CPT) */
/* These pools can grow at runtime, so don't need give a very large value */
#define IBLND_TX_POOL			256
#define IBLND_FMR_POOL			256
#define IBLND_FMR_POOL_FLUSH		192

#define IBLND_RX_MSGS(c)	\
	((c->ibc_queue_depth) * 2 + IBLND_OOB_MSGS(c->ibc_version))
#define IBLND_RX_MSG_BYTES(c)	(IBLND_RX_MSGS(c) * IBLND_MSG_SIZE)
#define IBLND_RX_MSG_PAGES(c)	\
	((IBLND_RX_MSG_BYTES(c) + PAGE_SIZE - 1) / PAGE_SIZE)

/* WRs and CQEs (per connection) */
#define IBLND_RECV_WRS(c)	IBLND_RX_MSGS(c)
#define IBLND_SEND_WRS(c)	\
	(((c->ibc_max_frags + 1) << IBLND_FRAG_SHIFT) * \
	  kiblnd_concurrent_sends(c->ibc_version, c->ibc_peer->ibp_ni))
#define IBLND_CQ_ENTRIES(c)	(IBLND_RECV_WRS(c) + IBLND_SEND_WRS(c))

struct kib_hca_dev;

/* o2iblnd can run over aliased interface */
#ifdef IFALIASZ
#define KIB_IFNAME_SIZE	      IFALIASZ
#else
#define KIB_IFNAME_SIZE	      256
#endif

struct kib_dev {
	struct list_head   ibd_list;            /* chain on kib_devs */
	struct list_head   ibd_fail_list;       /* chain on kib_failed_devs */
	__u32              ibd_ifip;            /* IPoIB interface IP */

	/* IPoIB interface name */
	char               ibd_ifname[KIB_IFNAME_SIZE];
	int                ibd_nnets;           /* # nets extant */

	unsigned long      ibd_next_failover;
	int                ibd_failed_failover; /* # failover failures */
	unsigned int       ibd_failover;        /* failover in progress */
	unsigned int ibd_can_failover; /* IPoIB interface is a bonding master */
	struct list_head   ibd_nets;
	struct kib_hca_dev *ibd_hdev;
};

struct kib_hca_dev {
	struct rdma_cm_id  *ibh_cmid;           /* listener cmid */
	struct ib_device   *ibh_ibdev;          /* IB device */
	int                ibh_page_shift;      /* page shift of current HCA */
	int                ibh_page_size;       /* page size of current HCA */
	__u64              ibh_page_mask;       /* page mask of current HCA */
	int                ibh_mr_shift;        /* bits shift of max MR size */
	__u64              ibh_mr_size;         /* size of MR */
	struct ib_pd       *ibh_pd;             /* PD */
	struct kib_dev	   *ibh_dev;		/* owner */
	atomic_t           ibh_ref;             /* refcount */
};

/** # of seconds to keep pool alive */
#define IBLND_POOL_DEADLINE     300
/** # of seconds to retry if allocation failed */
#define IBLND_POOL_RETRY	1

struct kib_pages {
	int                ibp_npages;          /* # pages */
	struct page        *ibp_pages[0];       /* page array */
};

struct kib_pool;
struct kib_poolset;

typedef int  (*kib_ps_pool_create_t)(struct kib_poolset *ps,
				     int inc, struct kib_pool **pp_po);
typedef void (*kib_ps_pool_destroy_t)(struct kib_pool *po);
typedef void (*kib_ps_node_init_t)(struct kib_pool *po, struct list_head *node);
typedef void (*kib_ps_node_fini_t)(struct kib_pool *po, struct list_head *node);

struct kib_net;

#define IBLND_POOL_NAME_LEN     32

struct kib_poolset {
	spinlock_t            ps_lock;            /* serialize */
	struct kib_net        *ps_net;            /* network it belongs to */
	char                  ps_name[IBLND_POOL_NAME_LEN]; /* pool set name */
	struct list_head      ps_pool_list;       /* list of pools */
	struct list_head      ps_failed_pool_list;/* failed pool list */
	unsigned long         ps_next_retry;      /* time stamp for retry if */
						  /* failed to allocate */
	int                   ps_increasing;      /* is allocating new pool */
	int                   ps_pool_size;       /* new pool size */
	int                   ps_cpt;             /* CPT id */

	kib_ps_pool_create_t  ps_pool_create;     /* create a new pool */
	kib_ps_pool_destroy_t ps_pool_destroy;    /* destroy a pool */
	kib_ps_node_init_t    ps_node_init; /* initialize new allocated node */
	kib_ps_node_fini_t    ps_node_fini;       /* finalize node */
};

struct kib_pool {
	struct list_head      po_list;       /* chain on pool list */
	struct list_head      po_free_list;  /* pre-allocated node */
	struct kib_poolset	*po_owner;	/* pool_set of this pool */
	unsigned long         po_deadline;   /* deadline of this pool */
	int                   po_allocated;  /* # of elements in use */
	int                   po_failed;     /* pool is created on failed HCA */
	int                   po_size;       /* # of pre-allocated elements */
};

struct kib_tx_poolset {
	struct kib_poolset	tps_poolset;		/* pool-set */
	__u64                 tps_next_tx_cookie; /* cookie of TX */
};

struct kib_tx_pool {
	struct kib_pool		 tpo_pool;	/* pool */
	struct kib_hca_dev	*tpo_hdev;	/* device for this pool */
	struct kib_tx		*tpo_tx_descs;	/* all the tx descriptors */
	struct kib_pages	*tpo_tx_pages;	/* premapped tx msg pages */
};

struct kib_fmr_poolset {
	spinlock_t            fps_lock;            /* serialize */
	struct kib_net        *fps_net;            /* IB network */
	struct list_head      fps_pool_list;       /* FMR pool list */
	struct list_head      fps_failed_pool_list;/* FMR pool list */
	__u64                 fps_version;         /* validity stamp */
	int                   fps_cpt;             /* CPT id */
	int                   fps_pool_size;
	int                   fps_flush_trigger;
	int		      fps_cache;
	int                   fps_increasing;      /* is allocating new pool */
	unsigned long         fps_next_retry;      /* time stamp for retry if*/
						   /* failed to allocate */
};

struct kib_fast_reg_descriptor { /* For fast registration */
	struct list_head		 frd_list;
	struct ib_send_wr		 frd_inv_wr;
	struct ib_reg_wr		 frd_fastreg_wr;
	struct ib_mr			*frd_mr;
	bool				 frd_valid;
};

struct kib_fmr_pool {
	struct list_head	 fpo_list;	/* chain on pool list */
	struct kib_hca_dev	*fpo_hdev;	/* device for this pool */
	struct kib_fmr_poolset	*fpo_owner;	/* owner of this pool */
	union {
		struct {
			struct ib_fmr_pool *fpo_fmr_pool; /* IB FMR pool */
		} fmr;
		struct { /* For fast registration */
			struct list_head    fpo_pool_list;
			int		    fpo_pool_size;
		} fast_reg;
	};
	unsigned long         fpo_deadline;        /* deadline of this pool */
	int                   fpo_failed;          /* fmr pool is failed */
	int                   fpo_map_count;       /* # of mapped FMR */
	int		      fpo_is_fmr;
};

struct kib_fmr {
	struct kib_fmr_pool		*fmr_pool;	/* pool of FMR */
	struct ib_pool_fmr		*fmr_pfmr;	/* IB pool fmr */
	struct kib_fast_reg_descriptor	*fmr_frd;
	u32				 fmr_key;
};

struct kib_net {
	struct list_head      ibn_list;       /* chain on struct kib_dev::ibd_nets */
	__u64                 ibn_incarnation;/* my epoch */
	int                   ibn_init;       /* initialisation state */
	int                   ibn_shutdown;   /* shutting down? */

	atomic_t              ibn_npeers;     /* # peers extant */
	atomic_t              ibn_nconns;     /* # connections extant */

	struct kib_tx_poolset	**ibn_tx_ps;	/* tx pool-set */
	struct kib_fmr_poolset	**ibn_fmr_ps;	/* fmr pool-set */

	struct kib_dev		*ibn_dev;	/* underlying IB device */
};

#define KIB_THREAD_SHIFT		16
#define KIB_THREAD_ID(cpt, tid)		((cpt) << KIB_THREAD_SHIFT | (tid))
#define KIB_THREAD_CPT(id)		((id) >> KIB_THREAD_SHIFT)
#define KIB_THREAD_TID(id)		((id) & ((1UL << KIB_THREAD_SHIFT) - 1))

struct kib_sched_info {
	spinlock_t         ibs_lock;     /* serialise */
	wait_queue_head_t  ibs_waitq;    /* schedulers sleep here */
	struct list_head   ibs_conns;    /* conns to check for rx completions */
	int                ibs_nthreads; /* number of scheduler threads */
	int                ibs_nthreads_max; /* max allowed scheduler threads */
	int                ibs_cpt;      /* CPT id */
};

struct kib_data {
	int               kib_init;           /* initialisation state */
	int               kib_shutdown;       /* shut down? */
	struct list_head  kib_devs;           /* IB devices extant */
	struct list_head  kib_failed_devs;    /* list head of failed devices */
	wait_queue_head_t kib_failover_waitq; /* schedulers sleep here */
	atomic_t kib_nthreads;                /* # live threads */
	rwlock_t kib_global_lock;    /* stabilize net/dev/peer/conn ops */
	struct list_head *kib_peers; /* hash table of all my known peers */
	int  kib_peer_hash_size;     /* size of kib_peers */
	void *kib_connd; /* the connd task (serialisation assertions) */
	struct list_head kib_connd_conns;   /* connections to setup/teardown */
	struct list_head kib_connd_zombies; /* connections with zero refcount */
	/* connections to reconnect */
	struct list_head	kib_reconn_list;
	/* peers wait for reconnection */
	struct list_head	kib_reconn_wait;
	/**
	 * The second that peers are pulled out from \a kib_reconn_wait
	 * for reconnection.
	 */
	time64_t		kib_reconn_sec;

	wait_queue_head_t kib_connd_waitq;  /* connection daemon sleeps here */
	spinlock_t kib_connd_lock;          /* serialise */
	struct ib_qp_attr kib_error_qpa;    /* QP->ERROR */
	struct kib_sched_info **kib_scheds; /* percpt data for schedulers */
};

#define IBLND_INIT_NOTHING 0
#define IBLND_INIT_DATA    1
#define IBLND_INIT_ALL     2

/************************************************************************
 * IB Wire message format.
 * These are sent in sender's byte order (i.e. receiver flips).
 */

struct kib_connparams {
	__u16        ibcp_queue_depth;
	__u16        ibcp_max_frags;
	__u32        ibcp_max_msg_size;
} WIRE_ATTR;

struct kib_immediate_msg {
	struct lnet_hdr	ibim_hdr;        /* portals header */
	char         ibim_payload[0]; /* piggy-backed payload */
} WIRE_ATTR;

struct kib_rdma_frag {
	__u32        rf_nob;          /* # bytes this frag */
	__u64        rf_addr;         /* CAVEAT EMPTOR: misaligned!! */
} WIRE_ATTR;

struct kib_rdma_desc {
	__u32           rd_key;       /* local/remote key */
	__u32           rd_nfrags;    /* # fragments */
	struct kib_rdma_frag	rd_frags[0];	/* buffer frags */
} WIRE_ATTR;

struct kib_putreq_msg {
	struct lnet_hdr	ibprm_hdr;    /* portals header */
	__u64           ibprm_cookie; /* opaque completion cookie */
} WIRE_ATTR;

struct kib_putack_msg {
	__u64           ibpam_src_cookie; /* reflected completion cookie */
	__u64           ibpam_dst_cookie; /* opaque completion cookie */
	struct kib_rdma_desc ibpam_rd;         /* sender's sink buffer */
} WIRE_ATTR;

struct kib_get_msg {
	struct lnet_hdr ibgm_hdr;     /* portals header */
	__u64           ibgm_cookie;  /* opaque completion cookie */
	struct kib_rdma_desc ibgm_rd;      /* rdma descriptor */
} WIRE_ATTR;

struct kib_completion_msg {
	__u64           ibcm_cookie;  /* opaque completion cookie */
	__s32           ibcm_status;  /* < 0 failure: >= 0 length */
} WIRE_ATTR;

struct kib_msg {
	/* First 2 fields fixed FOR ALL TIME */
	__u32           ibm_magic;    /* I'm an ibnal message */
	__u16           ibm_version;  /* this is my version number */

	__u8            ibm_type;     /* msg type */
	__u8            ibm_credits;  /* returned credits */
	__u32           ibm_nob;      /* # bytes in whole message */
	__u32           ibm_cksum;    /* checksum (0 == no checksum) */
	__u64           ibm_srcnid;   /* sender's NID */
	__u64           ibm_srcstamp; /* sender's incarnation */
	__u64           ibm_dstnid;   /* destination's NID */
	__u64           ibm_dststamp; /* destination's incarnation */

	union {
		struct kib_connparams		connparams;
		struct kib_immediate_msg	immediate;
		struct kib_putreq_msg		putreq;
		struct kib_putack_msg		putack;
		struct kib_get_msg		get;
		struct kib_completion_msg	completion;
	} WIRE_ATTR ibm_u;
} WIRE_ATTR;

#define IBLND_MSG_MAGIC     LNET_PROTO_IB_MAGIC /* unique magic */

#define IBLND_MSG_VERSION_1 0x11
#define IBLND_MSG_VERSION_2 0x12
#define IBLND_MSG_VERSION   IBLND_MSG_VERSION_2

#define IBLND_MSG_CONNREQ   0xc0	/* connection request */
#define IBLND_MSG_CONNACK   0xc1	/* connection acknowledge */
#define IBLND_MSG_NOOP      0xd0	/* nothing (just credits) */
#define IBLND_MSG_IMMEDIATE 0xd1	/* immediate */
#define IBLND_MSG_PUT_REQ   0xd2	/* putreq (src->sink) */
#define IBLND_MSG_PUT_NAK   0xd3	/* completion (sink->src) */
#define IBLND_MSG_PUT_ACK   0xd4	/* putack (sink->src) */
#define IBLND_MSG_PUT_DONE  0xd5	/* completion (src->sink) */
#define IBLND_MSG_GET_REQ   0xd6	/* getreq (sink->src) */
#define IBLND_MSG_GET_DONE  0xd7	/* completion (src->sink: all OK) */

struct kib_rej {
	__u32            ibr_magic;       /* sender's magic */
	__u16            ibr_version;     /* sender's version */
	__u8             ibr_why;         /* reject reason */
	__u8             ibr_padding;     /* padding */
	__u64            ibr_incarnation; /* incarnation of peer */
	struct kib_connparams ibr_cp;          /* connection parameters */
} WIRE_ATTR;

/* connection rejection reasons */
#define IBLND_REJECT_CONN_RACE      1 /* You lost connection race */
#define IBLND_REJECT_NO_RESOURCES   2 /* Out of memory/conns etc */
#define IBLND_REJECT_FATAL          3 /* Anything else */
#define IBLND_REJECT_CONN_UNCOMPAT  4 /* incompatible version peer */
#define IBLND_REJECT_CONN_STALE     5 /* stale peer */
/* peer's rdma frags doesn't match mine */
#define IBLND_REJECT_RDMA_FRAGS	    6
/* peer's msg queue size doesn't match mine */
#define IBLND_REJECT_MSG_QUEUE_SIZE 7

/***********************************************************************/

struct kib_rx {					/* receive message */
	struct list_head       rx_list;       /* queue for attention */
	struct kib_conn        *rx_conn;      /* owning conn */
	int                    rx_nob; /* # bytes received (-1 while posted) */
	enum ib_wc_status      rx_status;     /* completion status */
	struct kib_msg		*rx_msg;	/* message buffer (host vaddr) */
	__u64                  rx_msgaddr;    /* message buffer (I/O addr) */
	DECLARE_PCI_UNMAP_ADDR(rx_msgunmap);  /* for dma_unmap_single() */
	struct ib_recv_wr      rx_wrq;        /* receive work item... */
	struct ib_sge          rx_sge;        /* ...and its memory */
};

#define IBLND_POSTRX_DONT_POST    0 /* don't post */
#define IBLND_POSTRX_NO_CREDIT    1 /* post: no credits */
#define IBLND_POSTRX_PEER_CREDIT  2 /* post: give peer back 1 credit */
#define IBLND_POSTRX_RSRVD_CREDIT 3 /* post: give self back 1 reserved credit */

struct kib_tx {					/* transmit message */
	struct list_head      tx_list; /* queue on idle_txs ibc_tx_queue etc. */
	struct kib_tx_pool	*tx_pool;	/* pool I'm from */
	struct kib_conn       *tx_conn;       /* owning conn */
	short                 tx_sending;     /* # tx callbacks outstanding */
	short                 tx_queued;      /* queued for sending */
	short                 tx_waiting;     /* waiting for peer */
	int                   tx_status;      /* LNET completion status */
	unsigned long         tx_deadline;    /* completion deadline */
	__u64                 tx_cookie;      /* completion cookie */
	struct lnet_msg		*tx_lntmsg[2];	/* lnet msgs to finalize on completion */
	struct kib_msg	      *tx_msg;        /* message buffer (host vaddr) */
	__u64                 tx_msgaddr;     /* message buffer (I/O addr) */
	DECLARE_PCI_UNMAP_ADDR(tx_msgunmap);  /* for dma_unmap_single() */
	int                   tx_nwrq;        /* # send work items */
	struct ib_rdma_wr     *tx_wrq;        /* send work items... */
	struct ib_sge         *tx_sge;        /* ...and their memory */
	struct kib_rdma_desc  *tx_rd;         /* rdma descriptor */
	int                   tx_nfrags;      /* # entries in... */
	struct scatterlist    *tx_frags;      /* dma_map_sg descriptor */
	__u64                 *tx_pages;      /* rdma phys page addrs */
	struct kib_fmr        fmr;	      /* FMR */
	int                   tx_dmadir;      /* dma direction */
};

struct kib_connvars {
	struct kib_msg cv_msg; /* connection-in-progress variables */
};

struct kib_conn {
	struct kib_sched_info *ibc_sched;      /* scheduler information */
	struct kib_peer       *ibc_peer;       /* owning peer */
	struct kib_hca_dev         *ibc_hdev;       /* HCA bound on */
	struct list_head ibc_list;             /* stash on peer's conn list */
	struct list_head      ibc_sched_list;  /* schedule for attention */
	__u16                 ibc_version;     /* version of connection */
	/* reconnect later */
	__u16			ibc_reconnect:1;
	__u64                 ibc_incarnation; /* which instance of the peer */
	atomic_t              ibc_refcount;    /* # users */
	int                   ibc_state;       /* what's happening */
	int                   ibc_nsends_posted; /* # uncompleted sends */
	int                   ibc_noops_posted;  /* # uncompleted NOOPs */
	int                   ibc_credits;     /* # credits I have */
	int                   ibc_outstanding_credits; /* # credits to return */
	int                   ibc_reserved_credits; /* # ACK/DONE msg credits */
	int                   ibc_comms_error; /* set on comms error */
	/* connections queue depth */
	__u16		      ibc_queue_depth;
	/* connections max frags */
	__u16		      ibc_max_frags;
	unsigned int          ibc_nrx:16;      /* receive buffers owned */
	unsigned int          ibc_scheduled:1; /* scheduled for attention */
	unsigned int          ibc_ready:1;     /* CQ callback fired */
	unsigned long         ibc_last_send;   /* time of last send */
	struct list_head      ibc_connd_list;  /* link chain for */
					       /* kiblnd_check_conns only */
	struct list_head ibc_early_rxs; /* rxs completed before ESTABLISHED */
	struct list_head ibc_tx_noops;         /* IBLND_MSG_NOOPs for */
					       /* IBLND_MSG_VERSION_1 */
	struct list_head ibc_tx_queue;         /* sends that need a credit */
	struct list_head ibc_tx_queue_nocred;  /* sends that don't need a */
					       /* credit */
	struct list_head ibc_tx_queue_rsrvd;   /* sends that need to */
					       /* reserve an ACK/DONE msg */
	struct list_head ibc_active_txs; /* active tx awaiting completion */
	spinlock_t            ibc_lock;        /* serialise */
	struct kib_rx              *ibc_rxs;        /* the rx descs */
	struct kib_pages           *ibc_rx_pages;   /* premapped rx msg pages */

	struct rdma_cm_id     *ibc_cmid;       /* CM id */
	struct ib_cq          *ibc_cq;         /* completion queue */

	struct kib_connvars	*ibc_connvars;	/* in-progress connection state */
};

#define IBLND_CONN_INIT           0	 /* being initialised */
#define IBLND_CONN_ACTIVE_CONNECT 1	 /* active sending req */
#define IBLND_CONN_PASSIVE_WAIT   2	 /* passive waiting for rtu */
#define IBLND_CONN_ESTABLISHED    3	 /* connection established */
#define IBLND_CONN_CLOSING        4	 /* being closed */
#define IBLND_CONN_DISCONNECTED   5	 /* disconnected */

struct kib_peer {
	struct list_head ibp_list;        /* stash on global peer list */
	lnet_nid_t       ibp_nid;         /* who's on the other end(s) */
	struct lnet_ni	*ibp_ni;         /* LNet interface */
	struct list_head ibp_conns;       /* all active connections */
	struct list_head ibp_tx_queue;    /* msgs waiting for a conn */
	__u64            ibp_incarnation; /* incarnation of peer */
	/* when (in jiffies) I was last alive */
	unsigned long		ibp_last_alive;
	/* # users */
	atomic_t		ibp_refcount;
	/* version of peer */
	__u16			ibp_version;
	/* current passive connection attempts */
	unsigned short		ibp_accepting;
	/* current active connection attempts */
	unsigned short		ibp_connecting;
	/* reconnect this peer later */
	unsigned short		ibp_reconnecting:1;
	/* counter of how many times we triggered a conn race */
	unsigned char		ibp_races;
	/* # consecutive reconnection attempts to this peer */
	unsigned int		ibp_reconnected;
	/* errno on closing this peer */
	int              ibp_error;
	/* max map_on_demand */
	__u16		 ibp_max_frags;
	/* max_peer_credits */
	__u16		 ibp_queue_depth;
};

extern struct kib_data kiblnd_data;

void kiblnd_hdev_destroy(struct kib_hca_dev *hdev);

int kiblnd_msg_queue_size(int version, struct lnet_ni *ni);

/* max # of fragments configured by user */
static inline int
kiblnd_cfg_rdma_frags(struct lnet_ni *ni)
{
	struct lnet_ioctl_config_o2iblnd_tunables *tunables;
	int mod;

	tunables = &ni->ni_lnd_tunables->lt_tun_u.lt_o2ib;
	mod = tunables->lnd_map_on_demand;
	return mod ? mod : IBLND_MAX_RDMA_FRAGS >> IBLND_FRAG_SHIFT;
}

static inline int
kiblnd_rdma_frags(int version, struct lnet_ni *ni)
{
	return version == IBLND_MSG_VERSION_1 ?
			  (IBLND_MAX_RDMA_FRAGS >> IBLND_FRAG_SHIFT) :
			  kiblnd_cfg_rdma_frags(ni);
}

static inline int
kiblnd_concurrent_sends(int version, struct lnet_ni *ni)
{
	struct lnet_ioctl_config_o2iblnd_tunables *tunables;
	int concurrent_sends;

	tunables = &ni->ni_lnd_tunables->lt_tun_u.lt_o2ib;
	concurrent_sends = tunables->lnd_concurrent_sends;

	if (version == IBLND_MSG_VERSION_1) {
		if (concurrent_sends > IBLND_MSG_QUEUE_SIZE_V1 * 2)
			return IBLND_MSG_QUEUE_SIZE_V1 * 2;

		if (concurrent_sends < IBLND_MSG_QUEUE_SIZE_V1 / 2)
			return IBLND_MSG_QUEUE_SIZE_V1 / 2;
	}

	return concurrent_sends;
}

static inline void
kiblnd_hdev_addref_locked(struct kib_hca_dev *hdev)
{
	LASSERT(atomic_read(&hdev->ibh_ref) > 0);
	atomic_inc(&hdev->ibh_ref);
}

static inline void
kiblnd_hdev_decref(struct kib_hca_dev *hdev)
{
	LASSERT(atomic_read(&hdev->ibh_ref) > 0);
	if (atomic_dec_and_test(&hdev->ibh_ref))
		kiblnd_hdev_destroy(hdev);
}

static inline int
kiblnd_dev_can_failover(struct kib_dev *dev)
{
	if (!list_empty(&dev->ibd_fail_list)) /* already scheduled */
		return 0;

	if (!*kiblnd_tunables.kib_dev_failover) /* disabled */
		return 0;

	if (*kiblnd_tunables.kib_dev_failover > 1) /* force failover */
		return 1;

	return dev->ibd_can_failover;
}

#define kiblnd_conn_addref(conn)				\
do {							    \
	CDEBUG(D_NET, "conn[%p] (%d)++\n",		      \
	       (conn), atomic_read(&(conn)->ibc_refcount)); \
	atomic_inc(&(conn)->ibc_refcount);		  \
} while (0)

#define kiblnd_conn_decref(conn)					\
do {									\
	unsigned long flags;						\
									\
	CDEBUG(D_NET, "conn[%p] (%d)--\n",				\
	       (conn), atomic_read(&(conn)->ibc_refcount));		\
	LASSERT_ATOMIC_POS(&(conn)->ibc_refcount);			\
	if (atomic_dec_and_test(&(conn)->ibc_refcount)) {		\
		spin_lock_irqsave(&kiblnd_data.kib_connd_lock, flags);	\
		list_add_tail(&(conn)->ibc_list,			\
				  &kiblnd_data.kib_connd_zombies);	\
		wake_up(&kiblnd_data.kib_connd_waitq);		\
		spin_unlock_irqrestore(&kiblnd_data.kib_connd_lock, flags);\
	}								\
} while (0)

#define kiblnd_peer_addref(peer)				\
do {							    \
	CDEBUG(D_NET, "peer[%p] -> %s (%d)++\n",		\
	       (peer), libcfs_nid2str((peer)->ibp_nid),	 \
	       atomic_read(&(peer)->ibp_refcount));	\
	atomic_inc(&(peer)->ibp_refcount);		  \
} while (0)

#define kiblnd_peer_decref(peer)				\
do {							    \
	CDEBUG(D_NET, "peer[%p] -> %s (%d)--\n",		\
	       (peer), libcfs_nid2str((peer)->ibp_nid),	 \
	       atomic_read(&(peer)->ibp_refcount));	\
	LASSERT_ATOMIC_POS(&(peer)->ibp_refcount);	      \
	if (atomic_dec_and_test(&(peer)->ibp_refcount))     \
		kiblnd_destroy_peer(peer);		      \
} while (0)

static inline bool
kiblnd_peer_connecting(struct kib_peer *peer)
{
	return peer->ibp_connecting ||
	       peer->ibp_reconnecting ||
	       peer->ibp_accepting;
}

static inline bool
kiblnd_peer_idle(struct kib_peer *peer)
{
	return !kiblnd_peer_connecting(peer) && list_empty(&peer->ibp_conns);
}

static inline struct list_head *
kiblnd_nid2peerlist(lnet_nid_t nid)
{
	unsigned int hash =
		((unsigned int)nid) % kiblnd_data.kib_peer_hash_size;

	return &kiblnd_data.kib_peers[hash];
}

static inline int
kiblnd_peer_active(struct kib_peer *peer)
{
	/* Am I in the peer hash table? */
	return !list_empty(&peer->ibp_list);
}

static inline struct kib_conn *
kiblnd_get_conn_locked(struct kib_peer *peer)
{
	LASSERT(!list_empty(&peer->ibp_conns));

	/* just return the first connection */
	return list_entry(peer->ibp_conns.next, struct kib_conn, ibc_list);
}

static inline int
kiblnd_send_keepalive(struct kib_conn *conn)
{
	return (*kiblnd_tunables.kib_keepalive > 0) &&
		cfs_time_after(jiffies, conn->ibc_last_send +
			       msecs_to_jiffies(*kiblnd_tunables.kib_keepalive *
						MSEC_PER_SEC));
}

static inline int
kiblnd_need_noop(struct kib_conn *conn)
{
	struct lnet_ioctl_config_o2iblnd_tunables *tunables;
	struct lnet_ni *ni = conn->ibc_peer->ibp_ni;

	LASSERT(conn->ibc_state >= IBLND_CONN_ESTABLISHED);
	tunables = &ni->ni_lnd_tunables->lt_tun_u.lt_o2ib;

	if (conn->ibc_outstanding_credits <
	    IBLND_CREDITS_HIGHWATER(tunables, conn->ibc_version) &&
	    !kiblnd_send_keepalive(conn))
		return 0; /* No need to send NOOP */

	if (IBLND_OOB_CAPABLE(conn->ibc_version)) {
		if (!list_empty(&conn->ibc_tx_queue_nocred))
			return 0; /* NOOP can be piggybacked */

		/* No tx to piggyback NOOP onto or no credit to send a tx */
		return (list_empty(&conn->ibc_tx_queue) ||
			!conn->ibc_credits);
	}

	if (!list_empty(&conn->ibc_tx_noops) || /* NOOP already queued */
	    !list_empty(&conn->ibc_tx_queue_nocred) || /* piggyback NOOP */
	    !conn->ibc_credits)		    /* no credit */
		return 0;

	if (conn->ibc_credits == 1 &&      /* last credit reserved for */
	    !conn->ibc_outstanding_credits) /* giving back credits */
		return 0;

	/* No tx to piggyback NOOP onto or no credit to send a tx */
	return (list_empty(&conn->ibc_tx_queue) || conn->ibc_credits == 1);
}

static inline void
kiblnd_abort_receives(struct kib_conn *conn)
{
	ib_modify_qp(conn->ibc_cmid->qp,
		     &kiblnd_data.kib_error_qpa, IB_QP_STATE);
}

static inline const char *
kiblnd_queue2str(struct kib_conn *conn, struct list_head *q)
{
	if (q == &conn->ibc_tx_queue)
		return "tx_queue";

	if (q == &conn->ibc_tx_queue_rsrvd)
		return "tx_queue_rsrvd";

	if (q == &conn->ibc_tx_queue_nocred)
		return "tx_queue_nocred";

	if (q == &conn->ibc_active_txs)
		return "active_txs";

	LBUG();
	return NULL;
}

/* CAVEAT EMPTOR: We rely on descriptor alignment to allow us to use the */
/* lowest bits of the work request id to stash the work item type. */

#define IBLND_WID_INVAL	0
#define IBLND_WID_TX	1
#define IBLND_WID_RX	2
#define IBLND_WID_RDMA	3
#define IBLND_WID_MR	4
#define IBLND_WID_MASK	7UL

static inline __u64
kiblnd_ptr2wreqid(void *ptr, int type)
{
	unsigned long lptr = (unsigned long)ptr;

	LASSERT(!(lptr & IBLND_WID_MASK));
	LASSERT(!(type & ~IBLND_WID_MASK));
	return (__u64)(lptr | type);
}

static inline void *
kiblnd_wreqid2ptr(__u64 wreqid)
{
	return (void *)(((unsigned long)wreqid) & ~IBLND_WID_MASK);
}

static inline int
kiblnd_wreqid2type(__u64 wreqid)
{
	return wreqid & IBLND_WID_MASK;
}

static inline void
kiblnd_set_conn_state(struct kib_conn *conn, int state)
{
	conn->ibc_state = state;
	mb();
}

static inline void
kiblnd_init_msg(struct kib_msg *msg, int type, int body_nob)
{
	msg->ibm_type = type;
	msg->ibm_nob  = offsetof(struct kib_msg, ibm_u) + body_nob;
}

static inline int
kiblnd_rd_size(struct kib_rdma_desc *rd)
{
	int   i;
	int   size;

	for (i = size = 0; i < rd->rd_nfrags; i++)
		size += rd->rd_frags[i].rf_nob;

	return size;
}

static inline __u64
kiblnd_rd_frag_addr(struct kib_rdma_desc *rd, int index)
{
	return rd->rd_frags[index].rf_addr;
}

static inline __u32
kiblnd_rd_frag_size(struct kib_rdma_desc *rd, int index)
{
	return rd->rd_frags[index].rf_nob;
}

static inline __u32
kiblnd_rd_frag_key(struct kib_rdma_desc *rd, int index)
{
	return rd->rd_key;
}

static inline int
kiblnd_rd_consume_frag(struct kib_rdma_desc *rd, int index, __u32 nob)
{
	if (nob < rd->rd_frags[index].rf_nob) {
		rd->rd_frags[index].rf_addr += nob;
		rd->rd_frags[index].rf_nob  -= nob;
	} else {
		index++;
	}

	return index;
}

static inline int
kiblnd_rd_msg_size(struct kib_rdma_desc *rd, int msgtype, int n)
{
	LASSERT(msgtype == IBLND_MSG_GET_REQ ||
		msgtype == IBLND_MSG_PUT_ACK);

	return msgtype == IBLND_MSG_GET_REQ ?
	       offsetof(struct kib_get_msg, ibgm_rd.rd_frags[n]) :
	       offsetof(struct kib_putack_msg, ibpam_rd.rd_frags[n]);
}

static inline __u64
kiblnd_dma_mapping_error(struct ib_device *dev, u64 dma_addr)
{
	return ib_dma_mapping_error(dev, dma_addr);
}

static inline __u64 kiblnd_dma_map_single(struct ib_device *dev,
					  void *msg, size_t size,
					  enum dma_data_direction direction)
{
	return ib_dma_map_single(dev, msg, size, direction);
}

static inline void kiblnd_dma_unmap_single(struct ib_device *dev,
					   __u64 addr, size_t size,
					  enum dma_data_direction direction)
{
	ib_dma_unmap_single(dev, addr, size, direction);
}

#define KIBLND_UNMAP_ADDR_SET(p, m, a)  do {} while (0)
#define KIBLND_UNMAP_ADDR(p, m, a)      (a)

static inline int kiblnd_dma_map_sg(struct ib_device *dev,
				    struct scatterlist *sg, int nents,
				    enum dma_data_direction direction)
{
	return ib_dma_map_sg(dev, sg, nents, direction);
}

static inline void kiblnd_dma_unmap_sg(struct ib_device *dev,
				       struct scatterlist *sg, int nents,
				       enum dma_data_direction direction)
{
	ib_dma_unmap_sg(dev, sg, nents, direction);
}

static inline __u64 kiblnd_sg_dma_address(struct ib_device *dev,
					  struct scatterlist *sg)
{
	return ib_sg_dma_address(dev, sg);
}

static inline unsigned int kiblnd_sg_dma_len(struct ib_device *dev,
					     struct scatterlist *sg)
{
	return ib_sg_dma_len(dev, sg);
}

/* XXX We use KIBLND_CONN_PARAM(e) as writable buffer, it's not strictly */
/* right because OFED1.2 defines it as const, to use it we have to add */
/* (void *) cast to overcome "const" */

#define KIBLND_CONN_PARAM(e)     ((e)->param.conn.private_data)
#define KIBLND_CONN_PARAM_LEN(e) ((e)->param.conn.private_data_len)

void kiblnd_map_rx_descs(struct kib_conn *conn);
void kiblnd_unmap_rx_descs(struct kib_conn *conn);
void kiblnd_pool_free_node(struct kib_pool *pool, struct list_head *node);
struct list_head *kiblnd_pool_alloc_node(struct kib_poolset *ps);

int  kiblnd_fmr_pool_map(struct kib_fmr_poolset *fps, struct kib_tx *tx,
			 struct kib_rdma_desc *rd, __u32 nob, __u64 iov,
			 struct kib_fmr *fmr);
void kiblnd_fmr_pool_unmap(struct kib_fmr *fmr, int status);

int kiblnd_tunables_setup(struct lnet_ni *ni);
void kiblnd_tunables_init(void);

int  kiblnd_connd(void *arg);
int  kiblnd_scheduler(void *arg);
int  kiblnd_thread_start(int (*fn)(void *arg), void *arg, char *name);
int  kiblnd_failover_thread(void *arg);

int  kiblnd_alloc_pages(struct kib_pages **pp, int cpt, int npages);

int  kiblnd_cm_callback(struct rdma_cm_id *cmid,
			struct rdma_cm_event *event);
int  kiblnd_translate_mtu(int value);

int  kiblnd_dev_failover(struct kib_dev *dev);
int kiblnd_create_peer(struct lnet_ni *ni, struct kib_peer **peerp,
		       lnet_nid_t nid);
void kiblnd_destroy_peer(struct kib_peer *peer);
bool kiblnd_reconnect_peer(struct kib_peer *peer);
void kiblnd_destroy_dev(struct kib_dev *dev);
void kiblnd_unlink_peer_locked(struct kib_peer *peer);
struct kib_peer *kiblnd_find_peer_locked(lnet_nid_t nid);
int  kiblnd_close_stale_conns_locked(struct kib_peer *peer,
				     int version, __u64 incarnation);
int  kiblnd_close_peer_conns_locked(struct kib_peer *peer, int why);

struct kib_conn *kiblnd_create_conn(struct kib_peer *peer,
				    struct rdma_cm_id *cmid,
				    int state, int version);
void kiblnd_destroy_conn(struct kib_conn *conn);
void kiblnd_close_conn(struct kib_conn *conn, int error);
void kiblnd_close_conn_locked(struct kib_conn *conn, int error);

void kiblnd_launch_tx(struct lnet_ni *ni, struct kib_tx *tx, lnet_nid_t nid);
void kiblnd_txlist_done(struct lnet_ni *ni, struct list_head *txlist,
			int status);

void kiblnd_qp_event(struct ib_event *event, void *arg);
void kiblnd_cq_event(struct ib_event *event, void *arg);
void kiblnd_cq_completion(struct ib_cq *cq, void *arg);

void kiblnd_pack_msg(struct lnet_ni *ni, struct kib_msg *msg, int version,
		     int credits, lnet_nid_t dstnid, __u64 dststamp);
int  kiblnd_unpack_msg(struct kib_msg *msg, int nob);
int  kiblnd_post_rx(struct kib_rx *rx, int credit);

int kiblnd_send(struct lnet_ni *ni, void *private, struct lnet_msg *lntmsg);
int kiblnd_recv(struct lnet_ni *ni, void *private, struct lnet_msg *lntmsg,
		int delayed, struct iov_iter *to, unsigned int rlen);
