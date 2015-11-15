/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef HFI1_VERBS_H
#define HFI1_VERBS_H

#include <linux/types.h>
#include <linux/seqlock.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_mad.h>

struct hfi1_ctxtdata;
struct hfi1_pportdata;
struct hfi1_devdata;
struct hfi1_packet;

#include "iowait.h"

#define HFI1_MAX_RDMA_ATOMIC     16
#define HFI1_GUIDS_PER_PORT	5

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define HFI1_UVERBS_ABI_VERSION       2

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

/* Flags for checking QP state (see ib_hfi1_state_ops[]) */
#define HFI1_POST_SEND_OK                0x01
#define HFI1_POST_RECV_OK                0x02
#define HFI1_PROCESS_RECV_OK             0x04
#define HFI1_PROCESS_SEND_OK             0x08
#define HFI1_PROCESS_NEXT_SEND_OK        0x10
#define HFI1_FLUSH_SEND			0x20
#define HFI1_FLUSH_RECV			0x40
#define HFI1_PROCESS_OR_FLUSH_SEND \
	(HFI1_PROCESS_SEND_OK | HFI1_FLUSH_SEND)

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

#define HFI1_VENDOR_IPG		cpu_to_be16(0xFFA0)

#define IB_BTH_REQ_ACK		(1 << 31)
#define IB_BTH_SOLICITED	(1 << 23)
#define IB_BTH_MIG_REQ		(1 << 22)

#define IB_GRH_VERSION		6
#define IB_GRH_VERSION_MASK	0xF
#define IB_GRH_VERSION_SHIFT	28
#define IB_GRH_TCLASS_MASK	0xFF
#define IB_GRH_TCLASS_SHIFT	20
#define IB_GRH_FLOW_MASK	0xFFFFF
#define IB_GRH_FLOW_SHIFT	0
#define IB_GRH_NEXT_HDR		0x1B

#define IB_DEFAULT_GID_PREFIX	cpu_to_be64(0xfe80000000000000ULL)

/* flags passed by hfi1_ib_rcv() */
enum {
	HFI1_HAS_GRH = (1 << 0),
};

struct ib_reth {
	__be64 vaddr;
	__be32 rkey;
	__be32 length;
} __packed;

struct ib_atomic_eth {
	__be32 vaddr[2];        /* unaligned so access as 2 32-bit words */
	__be32 rkey;
	__be64 swap_data;
	__be64 compare_data;
} __packed;

union ib_ehdrs {
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
}  __packed;

struct hfi1_other_headers {
	__be32 bth[3];
	union ib_ehdrs u;
} __packed;

/*
 * Note that UD packets with a GRH header are 8+40+12+8 = 68 bytes
 * long (72 w/ imm_data).  Only the first 56 bytes of the IB header
 * will be in the eager header buffer.  The remaining 12 or 16 bytes
 * are in the data buffer.
 */
struct hfi1_ib_header {
	__be16 lrh[4];
	union {
		struct {
			struct ib_grh grh;
			struct hfi1_other_headers oth;
		} l;
		struct hfi1_other_headers oth;
	} u;
} __packed;

struct ahg_ib_header {
	struct sdma_engine *sde;
	u32 ahgdesc[2];
	u16 tx_flags;
	u8 ahgcount;
	u8 ahgidx;
	struct hfi1_ib_header ibh;
};

struct hfi1_pio_header {
	__le64 pbc;
	struct hfi1_ib_header hdr;
} __packed;

/*
 * used for force cacheline alignment for AHG
 */
struct tx_pio_header {
	struct hfi1_pio_header phdr;
} ____cacheline_aligned;

/*
 * There is one struct hfi1_mcast for each multicast GID.
 * All attached QPs are then stored as a list of
 * struct hfi1_mcast_qp.
 */
struct hfi1_mcast_qp {
	struct list_head list;
	struct hfi1_qp *qp;
};

struct hfi1_mcast {
	struct rb_node rb_node;
	union ib_gid mgid;
	struct list_head qp_list;
	wait_queue_head_t wait;
	atomic_t refcount;
	int n_attached;
};

/* Protection domain */
struct hfi1_pd {
	struct ib_pd ibpd;
	int user;               /* non-zero if created from user space */
};

/* Address Handle */
struct hfi1_ah {
	struct ib_ah ibah;
	struct ib_ah_attr attr;
	atomic_t refcount;
};

/*
 * This structure is used by hfi1_mmap() to validate an offset
 * when an mmap() request is made.  The vm_area_struct then uses
 * this as its vm_private_data.
 */
struct hfi1_mmap_info {
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
struct hfi1_cq_wc {
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
struct hfi1_cq {
	struct ib_cq ibcq;
	struct kthread_work comptask;
	struct hfi1_devdata *dd;
	spinlock_t lock; /* protect changes in this struct */
	u8 notify;
	u8 triggered;
	struct hfi1_cq_wc *queue;
	struct hfi1_mmap_info *ip;
};

/*
 * A segment is a linear region of low physical memory.
 * Used by the verbs layer.
 */
struct hfi1_seg {
	void *vaddr;
	size_t length;
};

/* The number of hfi1_segs that fit in a page. */
#define HFI1_SEGSZ     (PAGE_SIZE / sizeof(struct hfi1_seg))

struct hfi1_segarray {
	struct hfi1_seg segs[HFI1_SEGSZ];
};

struct hfi1_mregion {
	struct ib_pd *pd;       /* shares refcnt of ibmr.pd */
	u64 user_base;          /* User's address for this region */
	u64 iova;               /* IB start address of this region */
	size_t length;
	u32 lkey;
	u32 offset;             /* offset (bytes) to start of region */
	int access_flags;
	u32 max_segs;           /* number of hfi1_segs in all the arrays */
	u32 mapsz;              /* size of the map array */
	u8  page_shift;         /* 0 - non unform/non powerof2 sizes */
	u8  lkey_published;     /* in global table */
	struct completion comp; /* complete when refcount goes to zero */
	atomic_t refcount;
	struct hfi1_segarray *map[0];    /* the segments */
};

/*
 * These keep track of the copy progress within a memory region.
 * Used by the verbs layer.
 */
struct hfi1_sge {
	struct hfi1_mregion *mr;
	void *vaddr;            /* kernel virtual address of segment */
	u32 sge_length;         /* length of the SGE */
	u32 length;             /* remaining length of the segment */
	u16 m;                  /* current index: mr->map[m] */
	u16 n;                  /* current index: mr->map[m]->segs[n] */
};

/* Memory region */
struct hfi1_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
	struct hfi1_mregion mr;  /* must be last */
};

/*
 * Send work request queue entry.
 * The size of the sg_list is determined when the QP is created and stored
 * in qp->s_max_sge.
 */
struct hfi1_swqe {
	union {
		struct ib_send_wr wr;   /* don't use wr.sg_list */
		struct ib_rdma_wr rdma_wr;
		struct ib_atomic_wr atomic_wr;
		struct ib_ud_wr ud_wr;
	};
	u32 psn;                /* first packet sequence number */
	u32 lpsn;               /* last packet sequence number */
	u32 ssn;                /* send sequence number */
	u32 length;             /* total length of data in sg_list */
	struct hfi1_sge sg_list[0];
};

/*
 * Receive work request queue entry.
 * The size of the sg_list is determined when the QP (or SRQ) is created
 * and stored in qp->r_rq.max_sge (or srq->rq.max_sge).
 */
struct hfi1_rwqe {
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
struct hfi1_rwq {
	u32 head;               /* new work requests posted to the head */
	u32 tail;               /* receives pull requests from here. */
	struct hfi1_rwqe wq[0];
};

struct hfi1_rq {
	struct hfi1_rwq *wq;
	u32 size;               /* size of RWQE array */
	u8 max_sge;
	/* protect changes in this struct */
	spinlock_t lock ____cacheline_aligned_in_smp;
};

struct hfi1_srq {
	struct ib_srq ibsrq;
	struct hfi1_rq rq;
	struct hfi1_mmap_info *ip;
	/* send signal when number of RWQEs < limit */
	u32 limit;
};

struct hfi1_sge_state {
	struct hfi1_sge *sg_list;      /* next SGE to be used if any */
	struct hfi1_sge sge;   /* progress state for the current SGE */
	u32 total_len;
	u8 num_sge;
};

/*
 * This structure holds the information that the send tasklet needs
 * to send a RDMA read response or atomic operation.
 */
struct hfi1_ack_entry {
	u8 opcode;
	u8 sent;
	u32 psn;
	u32 lpsn;
	union {
		struct hfi1_sge rdma_sge;
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
struct hfi1_qp {
	struct ib_qp ibqp;
	/* read mostly fields above and below */
	struct ib_ah_attr remote_ah_attr;
	struct ib_ah_attr alt_ah_attr;
	struct hfi1_qp __rcu *next;           /* link list for QPN hash table */
	struct hfi1_swqe *s_wq;  /* send work queue */
	struct hfi1_mmap_info *ip;
	struct ahg_ib_header *s_hdr;     /* next packet header to send */
	u8 s_sc;			/* SC[0..4] for next packet */
	unsigned long timeout_jiffies;  /* computed from timeout */

	enum ib_mtu path_mtu;
	int srate_mbps;		/* s_srate (below) converted to Mbit/s */
	u32 remote_qpn;
	u32 pmtu;		/* decoded from path_mtu */
	u32 qkey;               /* QKEY for this QP (for UD or RD) */
	u32 s_size;             /* send work queue size */
	u32 s_rnr_timeout;      /* number of milliseconds for RNR timeout */
	u32 s_ahgpsn;           /* set to the psn in the copy of the header */

	u8 state;               /* QP state */
	u8 allowed_ops;		/* high order bits of allowed opcodes */
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


	struct hfi1_ack_entry s_ack_queue[HFI1_MAX_RDMA_ATOMIC + 1]
		____cacheline_aligned_in_smp;
	struct hfi1_sge_state s_rdma_read_sge;

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

	struct list_head rspwait;       /* link for waiting to respond */

	struct hfi1_sge_state r_sge;     /* current receive data */
	struct hfi1_rq r_rq;             /* receive work queue */

	spinlock_t s_lock ____cacheline_aligned_in_smp;
	struct hfi1_sge_state *s_cur_sge;
	u32 s_flags;
	struct hfi1_swqe *s_wqe;
	struct hfi1_sge_state s_sge;     /* current send request data */
	struct hfi1_mregion *s_rdma_mr;
	struct sdma_engine *s_sde; /* current sde */
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
	s8 s_ahgidx;
	u8 s_state;             /* opcode of last packet sent */
	u8 s_ack_state;         /* opcode of packet to ACK */
	u8 s_nak_state;         /* non-zero if NAK is pending */
	u8 r_nak_state;         /* non-zero if NAK is pending */
	u8 s_retry;             /* requester retry counter */
	u8 s_rnr_retry;         /* requester RNR retry counter */
	u8 s_num_rd_atomic;     /* number of RDMA read/atomic pending */
	u8 s_tail_ack_queue;    /* index into s_ack_queue[] */

	struct hfi1_sge_state s_ack_rdma_sge;
	struct timer_list s_timer;

	struct iowait s_iowait;

	struct hfi1_sge r_sg_list[0] /* verified SGEs */
		____cacheline_aligned_in_smp;
};

/*
 * Atomic bit definitions for r_aflags.
 */
#define HFI1_R_WRID_VALID        0
#define HFI1_R_REWIND_SGE        1

/*
 * Bit definitions for r_flags.
 */
#define HFI1_R_REUSE_SGE 0x01
#define HFI1_R_RDMAR_SEQ 0x02
#define HFI1_R_RSP_NAK   0x04
#define HFI1_R_RSP_SEND  0x08
#define HFI1_R_COMM_EST  0x10

/*
 * Bit definitions for s_flags.
 *
 * HFI1_S_SIGNAL_REQ_WR - set if QP send WRs contain completion signaled
 * HFI1_S_BUSY - send tasklet is processing the QP
 * HFI1_S_TIMER - the RC retry timer is active
 * HFI1_S_ACK_PENDING - an ACK is waiting to be sent after RDMA read/atomics
 * HFI1_S_WAIT_FENCE - waiting for all prior RDMA read or atomic SWQEs
 *                         before processing the next SWQE
 * HFI1_S_WAIT_RDMAR - waiting for a RDMA read or atomic SWQE to complete
 *                         before processing the next SWQE
 * HFI1_S_WAIT_RNR - waiting for RNR timeout
 * HFI1_S_WAIT_SSN_CREDIT - waiting for RC credits to process next SWQE
 * HFI1_S_WAIT_DMA - waiting for send DMA queue to drain before generating
 *                  next send completion entry not via send DMA
 * HFI1_S_WAIT_PIO - waiting for a send buffer to be available
 * HFI1_S_WAIT_TX - waiting for a struct verbs_txreq to be available
 * HFI1_S_WAIT_DMA_DESC - waiting for DMA descriptors to be available
 * HFI1_S_WAIT_KMEM - waiting for kernel memory to be available
 * HFI1_S_WAIT_PSN - waiting for a packet to exit the send DMA queue
 * HFI1_S_WAIT_ACK - waiting for an ACK packet before sending more requests
 * HFI1_S_SEND_ONE - send one packet, request ACK, then wait for ACK
 * HFI1_S_ECN - a BECN was queued to the send engine
 */
#define HFI1_S_SIGNAL_REQ_WR	0x0001
#define HFI1_S_BUSY		0x0002
#define HFI1_S_TIMER		0x0004
#define HFI1_S_RESP_PENDING	0x0008
#define HFI1_S_ACK_PENDING	0x0010
#define HFI1_S_WAIT_FENCE	0x0020
#define HFI1_S_WAIT_RDMAR	0x0040
#define HFI1_S_WAIT_RNR		0x0080
#define HFI1_S_WAIT_SSN_CREDIT	0x0100
#define HFI1_S_WAIT_DMA		0x0200
#define HFI1_S_WAIT_PIO		0x0400
#define HFI1_S_WAIT_TX		0x0800
#define HFI1_S_WAIT_DMA_DESC	0x1000
#define HFI1_S_WAIT_KMEM		0x2000
#define HFI1_S_WAIT_PSN		0x4000
#define HFI1_S_WAIT_ACK		0x8000
#define HFI1_S_SEND_ONE		0x10000
#define HFI1_S_UNLIMITED_CREDIT	0x20000
#define HFI1_S_AHG_VALID		0x40000
#define HFI1_S_AHG_CLEAR		0x80000
#define HFI1_S_ECN		0x100000

/*
 * Wait flags that would prevent any packet type from being sent.
 */
#define HFI1_S_ANY_WAIT_IO (HFI1_S_WAIT_PIO | HFI1_S_WAIT_TX | \
	HFI1_S_WAIT_DMA_DESC | HFI1_S_WAIT_KMEM)

/*
 * Wait flags that would prevent send work requests from making progress.
 */
#define HFI1_S_ANY_WAIT_SEND (HFI1_S_WAIT_FENCE | HFI1_S_WAIT_RDMAR | \
	HFI1_S_WAIT_RNR | HFI1_S_WAIT_SSN_CREDIT | HFI1_S_WAIT_DMA | \
	HFI1_S_WAIT_PSN | HFI1_S_WAIT_ACK)

#define HFI1_S_ANY_WAIT (HFI1_S_ANY_WAIT_IO | HFI1_S_ANY_WAIT_SEND)

#define HFI1_PSN_CREDIT  16

/*
 * Since struct hfi1_swqe is not a fixed size, we can't simply index into
 * struct hfi1_qp.s_wq.  This function does the array index computation.
 */
static inline struct hfi1_swqe *get_swqe_ptr(struct hfi1_qp *qp,
					     unsigned n)
{
	return (struct hfi1_swqe *)((char *)qp->s_wq +
				     (sizeof(struct hfi1_swqe) +
				      qp->s_max_sge *
				      sizeof(struct hfi1_sge)) * n);
}

/*
 * Since struct hfi1_rwqe is not a fixed size, we can't simply index into
 * struct hfi1_rwq.wq.  This function does the array index computation.
 */
static inline struct hfi1_rwqe *get_rwqe_ptr(struct hfi1_rq *rq, unsigned n)
{
	return (struct hfi1_rwqe *)
		((char *) rq->wq->wq +
		 (sizeof(struct hfi1_rwqe) +
		  rq->max_sge * sizeof(struct ib_sge)) * n);
}

#define MAX_LKEY_TABLE_BITS 23

struct hfi1_lkey_table {
	spinlock_t lock; /* protect changes in this struct */
	u32 next;               /* next unused index (speeds search) */
	u32 gen;                /* generation count */
	u32 max;                /* size of the table */
	struct hfi1_mregion __rcu **table;
};

struct hfi1_opcode_stats {
	u64 n_packets;          /* number of packets */
	u64 n_bytes;            /* total number of bytes */
};

struct hfi1_opcode_stats_perctx {
	struct hfi1_opcode_stats stats[256];
};

static inline void inc_opstats(
	u32 tlen,
	struct hfi1_opcode_stats *stats)
{
#ifdef CONFIG_DEBUG_FS
	stats->n_bytes += tlen;
	stats->n_packets++;
#endif
}

struct hfi1_ibport {
	struct hfi1_qp __rcu *qp[2];
	struct ib_mad_agent *send_agent;	/* agent for SMI (traps) */
	struct hfi1_ah *sm_ah;
	struct hfi1_ah *smi_ah;
	struct rb_root mcast_tree;
	spinlock_t lock;		/* protect changes in this struct */

	/* non-zero when timer is set */
	unsigned long mkey_lease_timeout;
	unsigned long trap_timeout;
	__be64 gid_prefix;      /* in network order */
	__be64 mkey;
	__be64 guids[HFI1_GUIDS_PER_PORT	- 1];	/* writable GUIDs */
	u64 tid;		/* TID for traps */
	u64 n_rc_resends;
	u64 n_seq_naks;
	u64 n_rdma_seq;
	u64 n_rnr_naks;
	u64 n_other_naks;
	u64 n_loop_pkts;
	u64 n_pkt_drops;
	u64 n_vl15_dropped;
	u64 n_rc_timeouts;
	u64 n_dmawait;
	u64 n_unaligned;
	u64 n_rc_dupreq;
	u64 n_rc_seqnak;

	/* Hot-path per CPU counters to avoid cacheline trading to update */
	u64 z_rc_acks;
	u64 z_rc_qacks;
	u64 z_rc_delayed_comp;
	u64 __percpu *rc_acks;
	u64 __percpu *rc_qacks;
	u64 __percpu *rc_delayed_comp;

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
	/* the first 16 entries are sl_to_vl for !OPA */
	u8 sl_to_sc[32];
	u8 sc_to_sl[32];
};


struct hfi1_qp_ibdev;
struct hfi1_ibdev {
	struct ib_device ibdev;
	struct list_head pending_mmaps;
	spinlock_t mmap_offset_lock; /* protect mmap_offset */
	u32 mmap_offset;
	struct hfi1_mregion __rcu *dma_mr;

	struct hfi1_qp_ibdev *qp_dev;

	/* QP numbers are shared by all IB ports */
	struct hfi1_lkey_table lk_table;
	/* protect wait lists */
	seqlock_t iowait_lock;
	struct list_head txwait;        /* list for wait verbs_txreq */
	struct list_head memwait;       /* list for wait kernel memory */
	struct list_head txreq_free;
	struct kmem_cache *verbs_txreq_cache;
	struct timer_list mem_timer;

	/* other waiters */
	spinlock_t pending_lock;

	u64 n_piowait;
	u64 n_txwait;
	u64 n_kmem_wait;
	u64 n_send_schedule;

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
	/* per HFI debugfs */
	struct dentry *hfi1_ibdev_dbg;
	/* per HFI symlinks to above */
	struct dentry *hfi1_ibdev_link;
#endif
};

struct hfi1_verbs_counters {
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

static inline struct hfi1_mr *to_imr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct hfi1_mr, ibmr);
}

static inline struct hfi1_pd *to_ipd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct hfi1_pd, ibpd);
}

static inline struct hfi1_ah *to_iah(struct ib_ah *ibah)
{
	return container_of(ibah, struct hfi1_ah, ibah);
}

static inline struct hfi1_cq *to_icq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct hfi1_cq, ibcq);
}

static inline struct hfi1_srq *to_isrq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct hfi1_srq, ibsrq);
}

static inline struct hfi1_qp *to_iqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct hfi1_qp, ibqp);
}

static inline struct hfi1_ibdev *to_idev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct hfi1_ibdev, ibdev);
}

/*
 * Send if not busy or waiting for I/O and either
 * a RC response is pending or we can process send work requests.
 */
static inline int hfi1_send_ok(struct hfi1_qp *qp)
{
	return !(qp->s_flags & (HFI1_S_BUSY | HFI1_S_ANY_WAIT_IO)) &&
		(qp->s_hdrwords || (qp->s_flags & HFI1_S_RESP_PENDING) ||
		 !(qp->s_flags & HFI1_S_ANY_WAIT_SEND));
}

/*
 * This must be called with s_lock held.
 */
void hfi1_schedule_send(struct hfi1_qp *qp);
void hfi1_bad_pqkey(struct hfi1_ibport *ibp, __be16 trap_num, u32 key, u32 sl,
		    u32 qp1, u32 qp2, __be16 lid1, __be16 lid2);
void hfi1_cap_mask_chg(struct hfi1_ibport *ibp);
void hfi1_sys_guid_chg(struct hfi1_ibport *ibp);
void hfi1_node_desc_chg(struct hfi1_ibport *ibp);
int hfi1_process_mad(struct ib_device *ibdev, int mad_flags, u8 port,
		     const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		     const struct ib_mad_hdr *in_mad, size_t in_mad_size,
		     struct ib_mad_hdr *out_mad, size_t *out_mad_size,
		     u16 *out_mad_pkey_index);
int hfi1_create_agents(struct hfi1_ibdev *dev);
void hfi1_free_agents(struct hfi1_ibdev *dev);

/*
 * The PSN_MASK and PSN_SHIFT allow for
 * 1) comparing two PSNs
 * 2) returning the PSN with any upper bits masked
 * 3) returning the difference between to PSNs
 *
 * The number of significant bits in the PSN must
 * necessarily be at least one bit less than
 * the container holding the PSN.
 */
#ifndef CONFIG_HFI1_VERBS_31BIT_PSN
#define PSN_MASK 0xFFFFFF
#define PSN_SHIFT 8
#else
#define PSN_MASK 0x7FFFFFFF
#define PSN_SHIFT 1
#endif
#define PSN_MODIFY_MASK 0xFFFFFF

/* Number of bits to pay attention to in the opcode for checking qp type */
#define OPCODE_QP_MASK 0xE0

/*
 * Compare the lower 24 bits of the msn values.
 * Returns an integer <, ==, or > than zero.
 */
static inline int cmp_msn(u32 a, u32 b)
{
	return (((int) a) - ((int) b)) << 8;
}

/*
 * Compare two PSNs
 * Returns an integer <, ==, or > than zero.
 */
static inline int cmp_psn(u32 a, u32 b)
{
	return (((int) a) - ((int) b)) << PSN_SHIFT;
}

/*
 * Return masked PSN
 */
static inline u32 mask_psn(u32 a)
{
	return a & PSN_MASK;
}

/*
 * Return delta between two PSNs
 */
static inline u32 delta_psn(u32 a, u32 b)
{
	return (((int)a - (int)b) << PSN_SHIFT) >> PSN_SHIFT;
}

struct hfi1_mcast *hfi1_mcast_find(struct hfi1_ibport *ibp, union ib_gid *mgid);

int hfi1_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);

int hfi1_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);

int hfi1_mcast_tree_empty(struct hfi1_ibport *ibp);

struct verbs_txreq;
void hfi1_put_txreq(struct verbs_txreq *tx);

int hfi1_verbs_send(struct hfi1_qp *qp, struct ahg_ib_header *ahdr,
		    u32 hdrwords, struct hfi1_sge_state *ss, u32 len);

void hfi1_copy_sge(struct hfi1_sge_state *ss, void *data, u32 length,
		   int release);

void hfi1_skip_sge(struct hfi1_sge_state *ss, u32 length, int release);

void hfi1_cnp_rcv(struct hfi1_packet *packet);

void hfi1_uc_rcv(struct hfi1_packet *packet);

void hfi1_rc_rcv(struct hfi1_packet *packet);

void hfi1_rc_hdrerr(
	struct hfi1_ctxtdata *rcd,
	struct hfi1_ib_header *hdr,
	u32 rcv_flags,
	struct hfi1_qp *qp);

u8 ah_to_sc(struct ib_device *ibdev, struct ib_ah_attr *ah_attr);

int hfi1_check_ah(struct ib_device *ibdev, struct ib_ah_attr *ah_attr);

struct ib_ah *hfi1_create_qp0_ah(struct hfi1_ibport *ibp, u16 dlid);

void hfi1_rc_rnr_retry(unsigned long arg);

void hfi1_rc_send_complete(struct hfi1_qp *qp, struct hfi1_ib_header *hdr);

void hfi1_rc_error(struct hfi1_qp *qp, enum ib_wc_status err);

void hfi1_ud_rcv(struct hfi1_packet *packet);

int hfi1_lookup_pkey_idx(struct hfi1_ibport *ibp, u16 pkey);

int hfi1_alloc_lkey(struct hfi1_mregion *mr, int dma_region);

void hfi1_free_lkey(struct hfi1_mregion *mr);

int hfi1_lkey_ok(struct hfi1_lkey_table *rkt, struct hfi1_pd *pd,
		 struct hfi1_sge *isge, struct ib_sge *sge, int acc);

int hfi1_rkey_ok(struct hfi1_qp *qp, struct hfi1_sge *sge,
		 u32 len, u64 vaddr, u32 rkey, int acc);

int hfi1_post_srq_receive(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			  struct ib_recv_wr **bad_wr);

struct ib_srq *hfi1_create_srq(struct ib_pd *ibpd,
			       struct ib_srq_init_attr *srq_init_attr,
			       struct ib_udata *udata);

int hfi1_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		    enum ib_srq_attr_mask attr_mask,
		    struct ib_udata *udata);

int hfi1_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr);

int hfi1_destroy_srq(struct ib_srq *ibsrq);

int hfi1_cq_init(struct hfi1_devdata *dd);

void hfi1_cq_exit(struct hfi1_devdata *dd);

void hfi1_cq_enter(struct hfi1_cq *cq, struct ib_wc *entry, int sig);

int hfi1_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *entry);

struct ib_cq *hfi1_create_cq(
	struct ib_device *ibdev,
	const struct ib_cq_init_attr *attr,
	struct ib_ucontext *context,
	struct ib_udata *udata);

int hfi1_destroy_cq(struct ib_cq *ibcq);

int hfi1_req_notify_cq(
	struct ib_cq *ibcq,
	enum ib_cq_notify_flags notify_flags);

int hfi1_resize_cq(struct ib_cq *ibcq, int cqe, struct ib_udata *udata);

struct ib_mr *hfi1_get_dma_mr(struct ib_pd *pd, int acc);

struct ib_mr *hfi1_reg_phys_mr(struct ib_pd *pd,
			       struct ib_phys_buf *buffer_list,
			       int num_phys_buf, int acc, u64 *iova_start);

struct ib_mr *hfi1_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			       u64 virt_addr, int mr_access_flags,
			       struct ib_udata *udata);

int hfi1_dereg_mr(struct ib_mr *ibmr);

struct ib_mr *hfi1_alloc_mr(struct ib_pd *pd,
			    enum ib_mr_type mr_type,
			    u32 max_entries);

struct ib_fmr *hfi1_alloc_fmr(struct ib_pd *pd, int mr_access_flags,
			      struct ib_fmr_attr *fmr_attr);

int hfi1_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
		      int list_len, u64 iova);

int hfi1_unmap_fmr(struct list_head *fmr_list);

int hfi1_dealloc_fmr(struct ib_fmr *ibfmr);

static inline void hfi1_get_mr(struct hfi1_mregion *mr)
{
	atomic_inc(&mr->refcount);
}

static inline void hfi1_put_mr(struct hfi1_mregion *mr)
{
	if (unlikely(atomic_dec_and_test(&mr->refcount)))
		complete(&mr->comp);
}

static inline void hfi1_put_ss(struct hfi1_sge_state *ss)
{
	while (ss->num_sge) {
		hfi1_put_mr(ss->sge.mr);
		if (--ss->num_sge)
			ss->sge = *ss->sg_list++;
	}
}

void hfi1_release_mmap_info(struct kref *ref);

struct hfi1_mmap_info *hfi1_create_mmap_info(struct hfi1_ibdev *dev, u32 size,
					     struct ib_ucontext *context,
					     void *obj);

void hfi1_update_mmap_info(struct hfi1_ibdev *dev, struct hfi1_mmap_info *ip,
			   u32 size, void *obj);

int hfi1_mmap(struct ib_ucontext *context, struct vm_area_struct *vma);

int hfi1_get_rwqe(struct hfi1_qp *qp, int wr_id_only);

void hfi1_migrate_qp(struct hfi1_qp *qp);

int hfi1_ruc_check_hdr(struct hfi1_ibport *ibp, struct hfi1_ib_header *hdr,
		       int has_grh, struct hfi1_qp *qp, u32 bth0);

u32 hfi1_make_grh(struct hfi1_ibport *ibp, struct ib_grh *hdr,
		  struct ib_global_route *grh, u32 hwords, u32 nwords);

void hfi1_make_ruc_header(struct hfi1_qp *qp, struct hfi1_other_headers *ohdr,
			  u32 bth0, u32 bth2, int middle);

void hfi1_do_send(struct work_struct *work);

void hfi1_send_complete(struct hfi1_qp *qp, struct hfi1_swqe *wqe,
			enum ib_wc_status status);

void hfi1_send_rc_ack(struct hfi1_ctxtdata *, struct hfi1_qp *qp, int is_fecn);

int hfi1_make_rc_req(struct hfi1_qp *qp);

int hfi1_make_uc_req(struct hfi1_qp *qp);

int hfi1_make_ud_req(struct hfi1_qp *qp);

int hfi1_register_ib_device(struct hfi1_devdata *);

void hfi1_unregister_ib_device(struct hfi1_devdata *);

void hfi1_ib_rcv(struct hfi1_packet *packet);

unsigned hfi1_get_npkeys(struct hfi1_devdata *);

int hfi1_verbs_send_dma(struct hfi1_qp *qp, struct ahg_ib_header *hdr,
			u32 hdrwords, struct hfi1_sge_state *ss, u32 len,
			u32 plen, u32 dwords, u64 pbc);

int hfi1_verbs_send_pio(struct hfi1_qp *qp, struct ahg_ib_header *hdr,
			u32 hdrwords, struct hfi1_sge_state *ss, u32 len,
			u32 plen, u32 dwords, u64 pbc);

struct send_context *qp_to_send_context(struct hfi1_qp *qp, u8 sc5);

extern const enum ib_wc_opcode ib_hfi1_wc_opcode[];

extern const u8 hdr_len_by_opcode[];

extern const int ib_hfi1_state_ops[];

extern __be64 ib_hfi1_sys_image_guid;    /* in network order */

extern unsigned int hfi1_lkey_table_size;

extern unsigned int hfi1_max_cqes;

extern unsigned int hfi1_max_cqs;

extern unsigned int hfi1_max_qp_wrs;

extern unsigned int hfi1_max_qps;

extern unsigned int hfi1_max_sges;

extern unsigned int hfi1_max_mcast_grps;

extern unsigned int hfi1_max_mcast_qp_attached;

extern unsigned int hfi1_max_srqs;

extern unsigned int hfi1_max_srq_sges;

extern unsigned int hfi1_max_srq_wrs;

extern const u32 ib_hfi1_rnr_table[];

extern struct ib_dma_mapping_ops hfi1_dma_mapping_ops;

#endif                          /* HFI1_VERBS_H */
