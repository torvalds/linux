/*
 * Copyright (c) 2012 Intel Corporation.  All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef QIB_VERBS_H
#define QIB_VERBS_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_user_verbs.h>

struct qib_ctxtdata;
struct qib_pportdata;
struct qib_devdata;
struct qib_verbs_txreq;

#define QIB_MAX_RDMA_ATOMIC     16
#define QIB_GUIDS_PER_PORT	5

#define QPN_MAX                 (1 << 24)
#define QPNMAP_ENTRIES          (QPN_MAX / PAGE_SIZE / BITS_PER_BYTE)

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define QIB_UVERBS_ABI_VERSION       2

/*
 * Define an ib_cq_notify value that is not valid so we know when CQ
 * notifications are armed.
 */
#define IB_CQ_NONE      (IB_CQ_NEXT_COMP + 1)

#define IB_SEQ_NAK	(3 << 29)

/* AETH NAK opcode values */
#define IB_RNR_NAK                      0x20
#define IB_NAK_PSN_ERROR                0x60
#define IB_NAK_INVALID_REQUEST          0x61
#define IB_NAK_REMOTE_ACCESS_ERROR      0x62
#define IB_NAK_REMOTE_OPERATIONAL_ERROR 0x63
#define IB_NAK_INVALID_RD_REQUEST       0x64

/* Flags for checking QP state (see ib_qib_state_ops[]) */
#define QIB_POST_SEND_OK                0x01
#define QIB_POST_RECV_OK                0x02
#define QIB_PROCESS_RECV_OK             0x04
#define QIB_PROCESS_SEND_OK             0x08
#define QIB_PROCESS_NEXT_SEND_OK        0x10
#define QIB_FLUSH_SEND			0x20
#define QIB_FLUSH_RECV			0x40
#define QIB_PROCESS_OR_FLUSH_SEND \
	(QIB_PROCESS_SEND_OK | QIB_FLUSH_SEND)

/* IB Performance Manager status values */
#define IB_PMA_SAMPLE_STATUS_DONE       0x00
#define IB_PMA_SAMPLE_STATUS_STARTED    0x01
#define IB_PMA_SAMPLE_STATUS_RUNNING    0x02

/* Mandatory IB performance counter select values. */
#define IB_PMA_PORT_XMIT_DATA   cpu_to_be16(0x0001)
#define IB_PMA_PORT_RCV_DATA    cpu_to_be16(0x0002)
#define IB_PMA_PORT_XMIT_PKTS   cpu_to_be16(0x0003)
#define IB_PMA_PORT_RCV_PKTS    cpu_to_be16(0x0004)
#define IB_PMA_PORT_XMIT_WAIT   cpu_to_be16(0x0005)

#define QIB_VENDOR_IPG		cpu_to_be16(0xFFA0)

#define IB_BTH_REQ_ACK		(1 << 31)
#define IB_BTH_SOLICITED	(1 << 23)
#define IB_BTH_MIG_REQ		(1 << 22)

/* XXX Should be defined in ib_verbs.h enum ib_port_cap_flags */
#define IB_PORT_OTHER_LOCAL_CHANGES_SUP (1 << 26)

#define IB_GRH_VERSION		6
#define IB_GRH_VERSION_MASK	0xF
#define IB_GRH_VERSION_SHIFT	28
#define IB_GRH_TCLASS_MASK	0xFF
#define IB_GRH_TCLASS_SHIFT	20
#define IB_GRH_FLOW_MASK	0xFFFFF
#define IB_GRH_FLOW_SHIFT	0
#define IB_GRH_NEXT_HDR		0x1B

#define IB_DEFAULT_GID_PREFIX	cpu_to_be64(0xfe80000000000000ULL)

/* Values for set/get portinfo VLCap OperationalVLs */
#define IB_VL_VL0       1
#define IB_VL_VL0_1     2
#define IB_VL_VL0_3     3
#define IB_VL_VL0_7     4
#define IB_VL_VL0_14    5

static inline int qib_num_vls(int vls)
{
	switch (vls) {
	default:
	case IB_VL_VL0:
		return 1;
	case IB_VL_VL0_1:
		return 2;
	case IB_VL_VL0_3:
		return 4;
	case IB_VL_VL0_7:
		return 8;
	case IB_VL_VL0_14:
		return 15;
	}
}

struct ib_reth {
	__be64 vaddr;
	__be32 rkey;
	__be32 length;
} __attribute__ ((packed));

struct ib_atomic_eth {
	__be32 vaddr[2];        /* unaligned so access as 2 32-bit words */
	__be32 rkey;
	__be64 swap_data;
	__be64 compare_data;
} __attribute__ ((packed));

struct qib_other_headers {
	__be32 bth[3];
	union {
		struct {
			__be32 deth[2];
			__be32 imm_data;
		} ud;
		struct {
			struct ib_reth reth;
			__be32 imm_data;
		} rc;
		struct {
			__be32 aeth;
			__be32 atomic_ack_eth[2];
		} at;
		__be32 imm_data;
		__be32 aeth;
		struct ib_atomic_eth atomic_eth;
	} u;
} __attribute__ ((packed));

/*
 * Note that UD packets with a GRH header are 8+40+12+8 = 68 bytes
 * long (72 w/ imm_data).  Only the first 56 bytes of the IB header
 * will be in the eager header buffer.  The remaining 12 or 16 bytes
 * are in the data buffer.
 */
struct qib_ib_header {
	__be16 lrh[4];
	union {
		struct {
			struct ib_grh grh;
			struct qib_other_headers oth;
		} l;
		struct qib_other_headers oth;
	} u;
} __attribute__ ((packed));

struct qib_pio_header {
	__le32 pbc[2];
	struct qib_ib_header hdr;
} __attribute__ ((packed));

/*
 * There is one struct qib_mcast for each multicast GID.
 * All attached QPs are then stored as a list of
 * struct qib_mcast_qp.
 */
struct qib_mcast_qp {
	struct list_head list;
	struct qib_qp *qp;
};

struct qib_mcast {
	struct rb_node rb_node;
	union ib_gid mgid;
	struct list_head qp_list;
	wait_queue_head_t wait;
	atomic_t refcount;
	int n_attached;
};

/* Protection domain */
struct qib_pd {
	struct ib_pd ibpd;
	int user;               /* non-zero if created from user space */
};

/* Address Handle */
struct qib_ah {
	struct ib_ah ibah;
	struct ib_ah_attr attr;
	atomic_t refcount;
};

/*
 * This structure is used by qib_mmap() to validate an offset
 * when an mmap() request is made.  The vm_area_struct then uses
 * this as its vm_private_data.
 */
struct qib_mmap_info {
	struct list_head pending_mmaps;
	struct ib_ucontext *context;
	void *obj;
	__u64 offset;
	struct kref ref;
	unsigned size;
};

/*
 * This structure is used to contain the head pointer, tail pointer,
 * and completion queue entries as a single memory allocation so
 * it can be mmap'ed into user space.
 */
struct qib_cq_wc {
	u32 head;               /* index of next entry to fill */
	u32 tail;               /* index of next ib_poll_cq() entry */
	union {
		/* these are actually size ibcq.cqe + 1 */
		struct ib_uverbs_wc uqueue[0];
		struct ib_wc kqueue[0];
	};
};

/*
 * The completion queue structure.
 */
struct qib_cq {
	struct ib_cq ibcq;
	struct kthread_work comptask;
	struct qib_devdata *dd;
	spinlock_t lock; /* protect changes in this struct */
	u8 notify;
	u8 triggered;
	struct qib_cq_wc *queue;
	struct qib_mmap_info *ip;
};

/*
 * A segment is a linear region of low physical memory.
 * XXX Maybe we should use phys addr here and kmap()/kunmap().
 * Used by the verbs layer.
 */
struct qib_seg {
	void *vaddr;
	size_t length;
};

/* The number of qib_segs that fit in a page. */
#define QIB_SEGSZ     (PAGE_SIZE / sizeof(struct qib_seg))

struct qib_segarray {
	struct qib_seg segs[QIB_SEGSZ];
};

struct qib_mregion {
	struct ib_pd *pd;       /* shares refcnt of ibmr.pd */
	u64 user_base;          /* User's address for this region */
	u64 iova;               /* IB start address of this region */
	size_t length;
	u32 lkey;
	u32 offset;             /* offset (bytes) to start of region */
	int access_flags;
	u32 max_segs;           /* number of qib_segs in all the arrays */
	u32 mapsz;              /* size of the map array */
	u8  page_shift;         /* 0 - non unform/non powerof2 sizes */
	u8  lkey_published;     /* in global table */
	struct completion comp; /* complete when refcount goes to zero */
	struct rcu_head list;
	atomic_t refcount;
	struct qib_segarray *map[0];    /* the segments */
};

/*
 * These keep track of the copy progress within a memory region.
 * Used by the verbs layer.
 */
struct qib_sge {
	struct qib_mregion *mr;
	void *vaddr;            /* kernel virtual address of segment */
	u32 sge_length;         /* length of the SGE */
	u32 length;             /* remaining length of the segment */
	u16 m;                  /* current index: mr->map[m] */
	u16 n;                  /* current index: mr->map[m]->segs[n] */
};

/* Memory region */
struct qib_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
	struct qib_mregion mr;  /* must be last */
};

/*
 * Send work request queue entry.
 * The size of the sg_list is determined when the QP is created and stored
 * in qp->s_max_sge.
 */
struct qib_swqe {
	struct ib_send_wr wr;   /* don't use wr.sg_list */
	u32 psn;                /* first packet sequence number */
	u32 lpsn;               /* last packet sequence number */
	u32 ssn;                /* send sequence number */
	u32 length;             /* total length of data in sg_list */
	struct qib_sge sg_list[0];
};

/*
 * Receive work request queue entry.
 * The size of the sg_list is determined when the QP (or SRQ) is created
 * and stored in qp->r_rq.max_sge (or srq->rq.max_sge).
 */
struct qib_rwqe {
	u64 wr_id;
	u8 num_sge;
	struct ib_sge sg_list[0];
};

/*
 * This structure is used to contain the head pointer, tail pointer,
 * and receive work queue entries as a single memory allocation so
 * it can be mmap'ed into user space.
 * Note that the wq array elements are variable size so you can't
 * just index into the array to get the N'th element;
 * use get_rwqe_ptr() instead.
 */
struct qib_rwq {
	u32 head;               /* new work requests posted to the head */
	u32 tail;               /* receives pull requests from here. */
	struct qib_rwqe wq[0];
};

struct qib_rq {
	struct qib_rwq *wq;
	u32 size;               /* size of RWQE array */
	u8 max_sge;
	spinlock_t lock /* protect changes in this struct */
		____cacheline_aligned_in_smp;
};

struct qib_srq {
	struct ib_srq ibsrq;
	struct qib_rq rq;
	struct qib_mmap_info *ip;
	/* send signal when number of RWQEs < limit */
	u32 limit;
};

struct qib_sge_state {
	struct qib_sge *sg_list;      /* next SGE to be used if any */
	struct qib_sge sge;   /* progress state for the current SGE */
	u32 total_len;
	u8 num_sge;
};

/*
 * This structure holds the information that the send tasklet needs
 * to send a RDMA read response or atomic operation.
 */
struct qib_ack_entry {
	u8 opcode;
	u8 sent;
	u32 psn;
	u32 lpsn;
	union {
		struct qib_sge rdma_sge;
		u64 atomic_data;
	};
};

/*
 * Variables prefixed with s_ are for the requester (sender).
 * Variables prefixed with r_ are for the responder (receiver).
 * Variables prefixed with ack_ are for responder replies.
 *
 * Common variables are protected by both r_rq.lock and s_lock in that order
 * which only happens in modify_qp() or changing the QP 'state'.
 */
struct qib_qp {
	struct ib_qp ibqp;
	/* read mostly fields above and below */
	struct ib_ah_attr remote_ah_attr;
	struct ib_ah_attr alt_ah_attr;
	struct qib_qp __rcu *next;            /* link list for QPN hash table */
	struct qib_swqe *s_wq;  /* send work queue */
	struct qib_mmap_info *ip;
	struct qib_ib_header *s_hdr;     /* next packet header to send */
	unsigned long timeout_jiffies;  /* computed from timeout */

	enum ib_mtu path_mtu;
	u32 remote_qpn;
	u32 pmtu;		/* decoded from path_mtu */
	u32 qkey;               /* QKEY for this QP (for UD or RD) */
	u32 s_size;             /* send work queue size */
	u32 s_rnr_timeout;      /* number of milliseconds for RNR timeout */

	u8 state;               /* QP state */
	u8 qp_access_flags;
	u8 alt_timeout;         /* Alternate path timeout for this QP */
	u8 timeout;             /* Timeout for this QP */
	u8 s_srate;
	u8 s_mig_state;
	u8 port_num;
	u8 s_pkey_index;        /* PKEY index to use */
	u8 s_alt_pkey_index;    /* Alternate path PKEY index to use */
	u8 r_max_rd_atomic;     /* max number of RDMA read/atomic to receive */
	u8 s_max_rd_atomic;     /* max number of RDMA read/atomic to send */
	u8 s_retry_cnt;         /* number of times to retry */
	u8 s_rnr_retry_cnt;
	u8 r_min_rnr_timer;     /* retry timeout value for RNR NAKs */
	u8 s_max_sge;           /* size of s_wq->sg_list */
	u8 s_draining;

	/* start of read/write fields */

	atomic_t refcount ____cacheline_aligned_in_smp;
	wait_queue_head_t wait;


	struct qib_ack_entry s_ack_queue[QIB_MAX_RDMA_ATOMIC + 1]
		____cacheline_aligned_in_smp;
	struct qib_sge_state s_rdma_read_sge;

	spinlock_t r_lock ____cacheline_aligned_in_smp;      /* used for APM */
	unsigned long r_aflags;
	u64 r_wr_id;            /* ID for current receive WQE */
	u32 r_ack_psn;          /* PSN for next ACK or atomic ACK */
	u32 r_len;              /* total length of r_sge */
	u32 r_rcv_len;          /* receive data len processed */
	u32 r_psn;              /* expected rcv packet sequence number */
	u32 r_msn;              /* message sequence number */

	u8 r_state;             /* opcode of last packet received */
	u8 r_flags;
	u8 r_head_ack_queue;    /* index into s_ack_queue[] */

	struct list_head rspwait;       /* link for waititing to respond */

	struct qib_sge_state r_sge;     /* current receive data */
	struct qib_rq r_rq;             /* receive work queue */

	spinlock_t s_lock ____cacheline_aligned_in_smp;
	struct qib_sge_state *s_cur_sge;
	u32 s_flags;
	struct qib_verbs_txreq *s_tx;
	struct qib_swqe *s_wqe;
	struct qib_sge_state s_sge;     /* current send request data */
	struct qib_mregion *s_rdma_mr;
	atomic_t s_dma_busy;
	u32 s_cur_size;         /* size of send packet in bytes */
	u32 s_len;              /* total length of s_sge */
	u32 s_rdma_read_len;    /* total length of s_rdma_read_sge */
	u32 s_next_psn;         /* PSN for next request */
	u32 s_last_psn;         /* last response PSN processed */
	u32 s_sending_psn;      /* lowest PSN that is being sent */
	u32 s_sending_hpsn;     /* highest PSN that is being sent */
	u32 s_psn;              /* current packet sequence number */
	u32 s_ack_rdma_psn;     /* PSN for sending RDMA read responses */
	u32 s_ack_psn;          /* PSN for acking sends and RDMA writes */
	u32 s_head;             /* new entries added here */
	u32 s_tail;             /* next entry to process */
	u32 s_cur;              /* current work queue entry */
	u32 s_acked;            /* last un-ACK'ed entry */
	u32 s_last;             /* last completed entry */
	u32 s_ssn;              /* SSN of tail entry */
	u32 s_lsn;              /* limit sequence number (credit) */
	u16 s_hdrwords;         /* size of s_hdr in 32 bit words */
	u16 s_rdma_ack_cnt;
	u8 s_state;             /* opcode of last packet sent */
	u8 s_ack_state;         /* opcode of packet to ACK */
	u8 s_nak_state;         /* non-zero if NAK is pending */
	u8 r_nak_state;         /* non-zero if NAK is pending */
	u8 s_retry;             /* requester retry counter */
	u8 s_rnr_retry;         /* requester RNR retry counter */
	u8 s_num_rd_atomic;     /* number of RDMA read/atomic pending */
	u8 s_tail_ack_queue;    /* index into s_ack_queue[] */

	struct qib_sge_state s_ack_rdma_sge;
	struct timer_list s_timer;
	struct list_head iowait;        /* link for wait PIO buf */

	struct work_struct s_work;

	wait_queue_head_t wait_dma;

	struct qib_sge r_sg_list[0] /* verified SGEs */
		____cacheline_aligned_in_smp;
};

/*
 * Atomic bit definitions for r_aflags.
 */
#define QIB_R_WRID_VALID        0
#define QIB_R_REWIND_SGE        1

/*
 * Bit definitions for r_flags.
 */
#define QIB_R_REUSE_SGE 0x01
#define QIB_R_RDMAR_SEQ 0x02
#define QIB_R_RSP_NAK   0x04
#define QIB_R_RSP_SEND  0x08
#define QIB_R_COMM_EST  0x10

/*
 * Bit definitions for s_flags.
 *
 * QIB_S_SIGNAL_REQ_WR - set if QP send WRs contain completion signaled
 * QIB_S_BUSY - send tasklet is processing the QP
 * QIB_S_TIMER - the RC retry timer is active
 * QIB_S_ACK_PENDING - an ACK is waiting to be sent after RDMA read/atomics
 * QIB_S_WAIT_FENCE - waiting for all prior RDMA read or atomic SWQEs
 *                         before processing the next SWQE
 * QIB_S_WAIT_RDMAR - waiting for a RDMA read or atomic SWQE to complete
 *                         before processing the next SWQE
 * QIB_S_WAIT_RNR - waiting for RNR timeout
 * QIB_S_WAIT_SSN_CREDIT - waiting for RC credits to process next SWQE
 * QIB_S_WAIT_DMA - waiting for send DMA queue to drain before generating
 *                  next send completion entry not via send DMA
 * QIB_S_WAIT_PIO - waiting for a send buffer to be available
 * QIB_S_WAIT_TX - waiting for a struct qib_verbs_txreq to be available
 * QIB_S_WAIT_DMA_DESC - waiting for DMA descriptors to be available
 * QIB_S_WAIT_KMEM - waiting for kernel memory to be available
 * QIB_S_WAIT_PSN - waiting for a packet to exit the send DMA queue
 * QIB_S_WAIT_ACK - waiting for an ACK packet before sending more requests
 * QIB_S_SEND_ONE - send one packet, request ACK, then wait for ACK
 */
#define QIB_S_SIGNAL_REQ_WR	0x0001
#define QIB_S_BUSY		0x0002
#define QIB_S_TIMER		0x0004
#define QIB_S_RESP_PENDING	0x0008
#define QIB_S_ACK_PENDING	0x0010
#define QIB_S_WAIT_FENCE	0x0020
#define QIB_S_WAIT_RDMAR	0x0040
#define QIB_S_WAIT_RNR		0x0080
#define QIB_S_WAIT_SSN_CREDIT	0x0100
#define QIB_S_WAIT_DMA		0x0200
#define QIB_S_WAIT_PIO		0x0400
#define QIB_S_WAIT_TX		0x0800
#define QIB_S_WAIT_DMA_DESC	0x1000
#define QIB_S_WAIT_KMEM		0x2000
#define QIB_S_WAIT_PSN		0x4000
#define QIB_S_WAIT_ACK		0x8000
#define QIB_S_SEND_ONE		0x10000
#define QIB_S_UNLIMITED_CREDIT	0x20000

/*
 * Wait flags that would prevent any packet type from being sent.
 */
#define QIB_S_ANY_WAIT_IO (QIB_S_WAIT_PIO | QIB_S_WAIT_TX | \
	QIB_S_WAIT_DMA_DESC | QIB_S_WAIT_KMEM)

/*
 * Wait flags that would prevent send work requests from making progress.
 */
#define QIB_S_ANY_WAIT_SEND (QIB_S_WAIT_FENCE | QIB_S_WAIT_RDMAR | \
	QIB_S_WAIT_RNR | QIB_S_WAIT_SSN_CREDIT | QIB_S_WAIT_DMA | \
	QIB_S_WAIT_PSN | QIB_S_WAIT_ACK)

#define QIB_S_ANY_WAIT (QIB_S_ANY_WAIT_IO | QIB_S_ANY_WAIT_SEND)

#define QIB_PSN_CREDIT  16

/*
 * Since struct qib_swqe is not a fixed size, we can't simply index into
 * struct qib_qp.s_wq.  This function does the array index computation.
 */
static inline struct qib_swqe *get_swqe_ptr(struct qib_qp *qp,
					      unsigned n)
{
	return (struct qib_swqe *)((char *)qp->s_wq +
				     (sizeof(struct qib_swqe) +
				      qp->s_max_sge *
				      sizeof(struct qib_sge)) * n);
}

/*
 * Since struct qib_rwqe is not a fixed size, we can't simply index into
 * struct qib_rwq.wq.  This function does the array index computation.
 */
static inline struct qib_rwqe *get_rwqe_ptr(struct qib_rq *rq, unsigned n)
{
	return (struct qib_rwqe *)
		((char *) rq->wq->wq +
		 (sizeof(struct qib_rwqe) +
		  rq->max_sge * sizeof(struct ib_sge)) * n);
}

/*
 * QPN-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way,
 * large bitmaps are not allocated unless large numbers of QPs are used.
 */
struct qpn_map {
	void *page;
};

struct qib_qpn_table {
	spinlock_t lock; /* protect changes in this struct */
	unsigned flags;         /* flags for QP0/1 allocated for each port */
	u32 last;               /* last QP number allocated */
	u32 nmaps;              /* size of the map table */
	u16 limit;
	u16 mask;
	/* bit map of free QP numbers other than 0/1 */
	struct qpn_map map[QPNMAP_ENTRIES];
};

struct qib_lkey_table {
	spinlock_t lock; /* protect changes in this struct */
	u32 next;               /* next unused index (speeds search) */
	u32 gen;                /* generation count */
	u32 max;                /* size of the table */
	struct qib_mregion __rcu **table;
};

struct qib_opcode_stats {
	u64 n_packets;          /* number of packets */
	u64 n_bytes;            /* total number of bytes */
};

struct qib_opcode_stats_perctx {
	struct qib_opcode_stats stats[128];
};

struct qib_ibport {
	struct qib_qp __rcu *qp0;
	struct qib_qp __rcu *qp1;
	struct ib_mad_agent *send_agent;	/* agent for SMI (traps) */
	struct qib_ah *sm_ah;
	struct qib_ah *smi_ah;
	struct rb_root mcast_tree;
	spinlock_t lock;		/* protect changes in this struct */

	/* non-zero when timer is set */
	unsigned long mkey_lease_timeout;
	unsigned long trap_timeout;
	__be64 gid_prefix;      /* in network order */
	__be64 mkey;
	__be64 guids[QIB_GUIDS_PER_PORT	- 1];	/* writable GUIDs */
	u64 tid;		/* TID for traps */
	u64 n_unicast_xmit;     /* total unicast packets sent */
	u64 n_unicast_rcv;      /* total unicast packets received */
	u64 n_multicast_xmit;   /* total multicast packets sent */
	u64 n_multicast_rcv;    /* total multicast packets received */
	u64 z_symbol_error_counter;             /* starting count for PMA */
	u64 z_link_error_recovery_counter;      /* starting count for PMA */
	u64 z_link_downed_counter;              /* starting count for PMA */
	u64 z_port_rcv_errors;                  /* starting count for PMA */
	u64 z_port_rcv_remphys_errors;          /* starting count for PMA */
	u64 z_port_xmit_discards;               /* starting count for PMA */
	u64 z_port_xmit_data;                   /* starting count for PMA */
	u64 z_port_rcv_data;                    /* starting count for PMA */
	u64 z_port_xmit_packets;                /* starting count for PMA */
	u64 z_port_rcv_packets;                 /* starting count for PMA */
	u32 z_local_link_integrity_errors;      /* starting count for PMA */
	u32 z_excessive_buffer_overrun_errors;  /* starting count for PMA */
	u32 z_vl15_dropped;                     /* starting count for PMA */
	u32 n_rc_resends;
	u32 n_rc_acks;
	u32 n_rc_qacks;
	u32 n_rc_delayed_comp;
	u32 n_seq_naks;
	u32 n_rdma_seq;
	u32 n_rnr_naks;
	u32 n_other_naks;
	u32 n_loop_pkts;
	u32 n_pkt_drops;
	u32 n_vl15_dropped;
	u32 n_rc_timeouts;
	u32 n_dmawait;
	u32 n_unaligned;
	u32 n_rc_dupreq;
	u32 n_rc_seqnak;
	u32 port_cap_flags;
	u32 pma_sample_start;
	u32 pma_sample_interval;
	__be16 pma_counter_select[5];
	u16 pma_tag;
	u16 pkey_violations;
	u16 qkey_violations;
	u16 mkey_violations;
	u16 mkey_lease_period;
	u16 sm_lid;
	u16 repress_traps;
	u8 sm_sl;
	u8 mkeyprot;
	u8 subnet_timeout;
	u8 vl_high_limit;
	u8 sl_to_vl[16];

};


struct qib_ibdev {
	struct ib_device ibdev;
	struct list_head pending_mmaps;
	spinlock_t mmap_offset_lock; /* protect mmap_offset */
	u32 mmap_offset;
	struct qib_mregion __rcu *dma_mr;

	/* QP numbers are shared by all IB ports */
	struct qib_qpn_table qpn_table;
	struct qib_lkey_table lk_table;
	struct list_head piowait;       /* list for wait PIO buf */
	struct list_head dmawait;	/* list for wait DMA */
	struct list_head txwait;        /* list for wait qib_verbs_txreq */
	struct list_head memwait;       /* list for wait kernel memory */
	struct list_head txreq_free;
	struct timer_list mem_timer;
	struct qib_qp __rcu **qp_table;
	struct qib_pio_header *pio_hdrs;
	dma_addr_t pio_hdrs_phys;
	/* list of QPs waiting for RNR timer */
	spinlock_t pending_lock; /* protect wait lists, PMA counters, etc. */
	u32 qp_table_size; /* size of the hash table */
	u32 qp_rnd; /* random bytes for hash */
	spinlock_t qpt_lock;

	u32 n_piowait;
	u32 n_txwait;

	u32 n_pds_allocated;    /* number of PDs allocated for device */
	spinlock_t n_pds_lock;
	u32 n_ahs_allocated;    /* number of AHs allocated for device */
	spinlock_t n_ahs_lock;
	u32 n_cqs_allocated;    /* number of CQs allocated for device */
	spinlock_t n_cqs_lock;
	u32 n_qps_allocated;    /* number of QPs allocated for device */
	spinlock_t n_qps_lock;
	u32 n_srqs_allocated;   /* number of SRQs allocated for device */
	spinlock_t n_srqs_lock;
	u32 n_mcast_grps_allocated; /* number of mcast groups allocated */
	spinlock_t n_mcast_grps_lock;
#ifdef CONFIG_DEBUG_FS
	/* per HCA debugfs */
	struct dentry *qib_ibdev_dbg;
#endif
};

struct qib_verbs_counters {
	u64 symbol_error_counter;
	u64 link_error_recovery_counter;
	u64 link_downed_counter;
	u64 port_rcv_errors;
	u64 port_rcv_remphys_errors;
	u64 port_xmit_discards;
	u64 port_xmit_data;
	u64 port_rcv_data;
	u64 port_xmit_packets;
	u64 port_rcv_packets;
	u32 local_link_integrity_errors;
	u32 excessive_buffer_overrun_errors;
	u32 vl15_dropped;
};

static inline struct qib_mr *to_imr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct qib_mr, ibmr);
}

static inline struct qib_pd *to_ipd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct qib_pd, ibpd);
}

static inline struct qib_ah *to_iah(struct ib_ah *ibah)
{
	return container_of(ibah, struct qib_ah, ibah);
}

static inline struct qib_cq *to_icq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct qib_cq, ibcq);
}

static inline struct qib_srq *to_isrq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct qib_srq, ibsrq);
}

static inline struct qib_qp *to_iqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct qib_qp, ibqp);
}

static inline struct qib_ibdev *to_idev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct qib_ibdev, ibdev);
}

/*
 * Send if not busy or waiting for I/O and either
 * a RC response is pending or we can process send work requests.
 */
static inline int qib_send_ok(struct qib_qp *qp)
{
	return !(qp->s_flags & (QIB_S_BUSY | QIB_S_ANY_WAIT_IO)) &&
		(qp->s_hdrwords || (qp->s_flags & QIB_S_RESP_PENDING) ||
		 !(qp->s_flags & QIB_S_ANY_WAIT_SEND));
}

/*
 * This must be called with s_lock held.
 */
void qib_schedule_send(struct qib_qp *qp);

static inline int qib_pkey_ok(u16 pkey1, u16 pkey2)
{
	u16 p1 = pkey1 & 0x7FFF;
	u16 p2 = pkey2 & 0x7FFF;

	/*
	 * Low 15 bits must be non-zero and match, and
	 * one of the two must be a full member.
	 */
	return p1 && p1 == p2 && ((__s16)pkey1 < 0 || (__s16)pkey2 < 0);
}

void qib_bad_pqkey(struct qib_ibport *ibp, __be16 trap_num, u32 key, u32 sl,
		   u32 qp1, u32 qp2, __be16 lid1, __be16 lid2);
void qib_cap_mask_chg(struct qib_ibport *ibp);
void qib_sys_guid_chg(struct qib_ibport *ibp);
void qib_node_desc_chg(struct qib_ibport *ibp);
int qib_process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
		    struct ib_wc *in_wc, struct ib_grh *in_grh,
		    struct ib_mad *in_mad, struct ib_mad *out_mad);
int qib_create_agents(struct qib_ibdev *dev);
void qib_free_agents(struct qib_ibdev *dev);

/*
 * Compare the lower 24 bits of the two values.
 * Returns an integer <, ==, or > than zero.
 */
static inline int qib_cmp24(u32 a, u32 b)
{
	return (((int) a) - ((int) b)) << 8;
}

struct qib_mcast *qib_mcast_find(struct qib_ibport *ibp, union ib_gid *mgid);

int qib_snapshot_counters(struct qib_pportdata *ppd, u64 *swords,
			  u64 *rwords, u64 *spkts, u64 *rpkts,
			  u64 *xmit_wait);

int qib_get_counters(struct qib_pportdata *ppd,
		     struct qib_verbs_counters *cntrs);

int qib_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);

int qib_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);

int qib_mcast_tree_empty(struct qib_ibport *ibp);

__be32 qib_compute_aeth(struct qib_qp *qp);

struct qib_qp *qib_lookup_qpn(struct qib_ibport *ibp, u32 qpn);

struct ib_qp *qib_create_qp(struct ib_pd *ibpd,
			    struct ib_qp_init_attr *init_attr,
			    struct ib_udata *udata);

int qib_destroy_qp(struct ib_qp *ibqp);

int qib_error_qp(struct qib_qp *qp, enum ib_wc_status err);

int qib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		  int attr_mask, struct ib_udata *udata);

int qib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		 int attr_mask, struct ib_qp_init_attr *init_attr);

unsigned qib_free_all_qps(struct qib_devdata *dd);

void qib_init_qpn_table(struct qib_devdata *dd, struct qib_qpn_table *qpt);

void qib_free_qpn_table(struct qib_qpn_table *qpt);

void qib_get_credit(struct qib_qp *qp, u32 aeth);

unsigned qib_pkt_delay(u32 plen, u8 snd_mult, u8 rcv_mult);

void qib_verbs_sdma_desc_avail(struct qib_pportdata *ppd, unsigned avail);

void qib_put_txreq(struct qib_verbs_txreq *tx);

int qib_verbs_send(struct qib_qp *qp, struct qib_ib_header *hdr,
		   u32 hdrwords, struct qib_sge_state *ss, u32 len);

void qib_copy_sge(struct qib_sge_state *ss, void *data, u32 length,
		  int release);

void qib_skip_sge(struct qib_sge_state *ss, u32 length, int release);

void qib_uc_rcv(struct qib_ibport *ibp, struct qib_ib_header *hdr,
		int has_grh, void *data, u32 tlen, struct qib_qp *qp);

void qib_rc_rcv(struct qib_ctxtdata *rcd, struct qib_ib_header *hdr,
		int has_grh, void *data, u32 tlen, struct qib_qp *qp);

int qib_check_ah(struct ib_device *ibdev, struct ib_ah_attr *ah_attr);

struct ib_ah *qib_create_qp0_ah(struct qib_ibport *ibp, u16 dlid);

void qib_rc_rnr_retry(unsigned long arg);

void qib_rc_send_complete(struct qib_qp *qp, struct qib_ib_header *hdr);

void qib_rc_error(struct qib_qp *qp, enum ib_wc_status err);

int qib_post_ud_send(struct qib_qp *qp, struct ib_send_wr *wr);

void qib_ud_rcv(struct qib_ibport *ibp, struct qib_ib_header *hdr,
		int has_grh, void *data, u32 tlen, struct qib_qp *qp);

int qib_alloc_lkey(struct qib_mregion *mr, int dma_region);

void qib_free_lkey(struct qib_mregion *mr);

int qib_lkey_ok(struct qib_lkey_table *rkt, struct qib_pd *pd,
		struct qib_sge *isge, struct ib_sge *sge, int acc);

int qib_rkey_ok(struct qib_qp *qp, struct qib_sge *sge,
		u32 len, u64 vaddr, u32 rkey, int acc);

int qib_post_srq_receive(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			 struct ib_recv_wr **bad_wr);

struct ib_srq *qib_create_srq(struct ib_pd *ibpd,
			      struct ib_srq_init_attr *srq_init_attr,
			      struct ib_udata *udata);

int qib_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		   enum ib_srq_attr_mask attr_mask,
		   struct ib_udata *udata);

int qib_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr);

int qib_destroy_srq(struct ib_srq *ibsrq);

int qib_cq_init(struct qib_devdata *dd);

void qib_cq_exit(struct qib_devdata *dd);

void qib_cq_enter(struct qib_cq *cq, struct ib_wc *entry, int sig);

int qib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *entry);

struct ib_cq *qib_create_cq(struct ib_device *ibdev, int entries,
			    int comp_vector, struct ib_ucontext *context,
			    struct ib_udata *udata);

int qib_destroy_cq(struct ib_cq *ibcq);

int qib_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags notify_flags);

int qib_resize_cq(struct ib_cq *ibcq, int cqe, struct ib_udata *udata);

struct ib_mr *qib_get_dma_mr(struct ib_pd *pd, int acc);

struct ib_mr *qib_reg_phys_mr(struct ib_pd *pd,
			      struct ib_phys_buf *buffer_list,
			      int num_phys_buf, int acc, u64 *iova_start);

struct ib_mr *qib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			      u64 virt_addr, int mr_access_flags,
			      struct ib_udata *udata);

int qib_dereg_mr(struct ib_mr *ibmr);

struct ib_mr *qib_alloc_fast_reg_mr(struct ib_pd *pd, int max_page_list_len);

struct ib_fast_reg_page_list *qib_alloc_fast_reg_page_list(
				struct ib_device *ibdev, int page_list_len);

void qib_free_fast_reg_page_list(struct ib_fast_reg_page_list *pl);

int qib_fast_reg_mr(struct qib_qp *qp, struct ib_send_wr *wr);

struct ib_fmr *qib_alloc_fmr(struct ib_pd *pd, int mr_access_flags,
			     struct ib_fmr_attr *fmr_attr);

int qib_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
		     int list_len, u64 iova);

int qib_unmap_fmr(struct list_head *fmr_list);

int qib_dealloc_fmr(struct ib_fmr *ibfmr);

static inline void qib_get_mr(struct qib_mregion *mr)
{
	atomic_inc(&mr->refcount);
}

void mr_rcu_callback(struct rcu_head *list);

static inline void qib_put_mr(struct qib_mregion *mr)
{
	if (unlikely(atomic_dec_and_test(&mr->refcount)))
		call_rcu(&mr->list, mr_rcu_callback);
}

static inline void qib_put_ss(struct qib_sge_state *ss)
{
	while (ss->num_sge) {
		qib_put_mr(ss->sge.mr);
		if (--ss->num_sge)
			ss->sge = *ss->sg_list++;
	}
}


void qib_release_mmap_info(struct kref *ref);

struct qib_mmap_info *qib_create_mmap_info(struct qib_ibdev *dev, u32 size,
					   struct ib_ucontext *context,
					   void *obj);

void qib_update_mmap_info(struct qib_ibdev *dev, struct qib_mmap_info *ip,
			  u32 size, void *obj);

int qib_mmap(struct ib_ucontext *context, struct vm_area_struct *vma);

int qib_get_rwqe(struct qib_qp *qp, int wr_id_only);

void qib_migrate_qp(struct qib_qp *qp);

int qib_ruc_check_hdr(struct qib_ibport *ibp, struct qib_ib_header *hdr,
		      int has_grh, struct qib_qp *qp, u32 bth0);

u32 qib_make_grh(struct qib_ibport *ibp, struct ib_grh *hdr,
		 struct ib_global_route *grh, u32 hwords, u32 nwords);

void qib_make_ruc_header(struct qib_qp *qp, struct qib_other_headers *ohdr,
			 u32 bth0, u32 bth2);

void qib_do_send(struct work_struct *work);

void qib_send_complete(struct qib_qp *qp, struct qib_swqe *wqe,
		       enum ib_wc_status status);

void qib_send_rc_ack(struct qib_qp *qp);

int qib_make_rc_req(struct qib_qp *qp);

int qib_make_uc_req(struct qib_qp *qp);

int qib_make_ud_req(struct qib_qp *qp);

int qib_register_ib_device(struct qib_devdata *);

void qib_unregister_ib_device(struct qib_devdata *);

void qib_ib_rcv(struct qib_ctxtdata *, void *, void *, u32);

void qib_ib_piobufavail(struct qib_devdata *);

unsigned qib_get_npkeys(struct qib_devdata *);

unsigned qib_get_pkey(struct qib_ibport *, unsigned);

extern const enum ib_wc_opcode ib_qib_wc_opcode[];

/*
 * Below  HCA-independent IB PhysPortState values, returned
 * by the f_ibphys_portstate() routine.
 */
#define IB_PHYSPORTSTATE_SLEEP 1
#define IB_PHYSPORTSTATE_POLL 2
#define IB_PHYSPORTSTATE_DISABLED 3
#define IB_PHYSPORTSTATE_CFG_TRAIN 4
#define IB_PHYSPORTSTATE_LINKUP 5
#define IB_PHYSPORTSTATE_LINK_ERR_RECOVER 6
#define IB_PHYSPORTSTATE_CFG_DEBOUNCE 8
#define IB_PHYSPORTSTATE_CFG_IDLE 0xB
#define IB_PHYSPORTSTATE_RECOVERY_RETRAIN 0xC
#define IB_PHYSPORTSTATE_RECOVERY_WAITRMT 0xE
#define IB_PHYSPORTSTATE_RECOVERY_IDLE 0xF
#define IB_PHYSPORTSTATE_CFG_ENH 0x10
#define IB_PHYSPORTSTATE_CFG_WAIT_ENH 0x13

extern const int ib_qib_state_ops[];

extern __be64 ib_qib_sys_image_guid;    /* in network order */

extern unsigned int ib_qib_lkey_table_size;

extern unsigned int ib_qib_max_cqes;

extern unsigned int ib_qib_max_cqs;

extern unsigned int ib_qib_max_qp_wrs;

extern unsigned int ib_qib_max_qps;

extern unsigned int ib_qib_max_sges;

extern unsigned int ib_qib_max_mcast_grps;

extern unsigned int ib_qib_max_mcast_qp_attached;

extern unsigned int ib_qib_max_srqs;

extern unsigned int ib_qib_max_srq_sges;

extern unsigned int ib_qib_max_srq_wrs;

extern const u32 ib_qib_rnr_table[];

extern struct ib_dma_mapping_ops qib_dma_mapping_ops;

#endif                          /* QIB_VERBS_H */
