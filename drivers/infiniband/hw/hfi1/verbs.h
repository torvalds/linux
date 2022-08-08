/*
 * Copyright(c) 2015 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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
#include <linux/slab.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_hdrs.h>
#include <rdma/rdma_vt.h>
#include <rdma/rdmavt_qp.h>
#include <rdma/rdmavt_cq.h>

struct hfi1_ctxtdata;
struct hfi1_pportdata;
struct hfi1_devdata;
struct hfi1_packet;

#include "iowait.h"
#include "tid_rdma.h"
#include "opfn.h"

#define HFI1_MAX_RDMA_ATOMIC     16

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define HFI1_UVERBS_ABI_VERSION       2

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

#define IB_DEFAULT_GID_PREFIX	cpu_to_be64(0xfe80000000000000ULL)
#define OPA_BTH_MIG_REQ		BIT(31)

#define RC_OP(x) IB_OPCODE_RC_##x
#define UC_OP(x) IB_OPCODE_UC_##x

/* flags passed by hfi1_ib_rcv() */
enum {
	HFI1_HAS_GRH = (1 << 0),
};

#define LRH_16B_BYTES (sizeof_field(struct hfi1_16b_header, lrh))
#define LRH_16B_DWORDS (LRH_16B_BYTES / sizeof(u32))
#define LRH_9B_BYTES (sizeof_field(struct ib_header, lrh))
#define LRH_9B_DWORDS (LRH_9B_BYTES / sizeof(u32))

/* 24Bits for qpn, upper 8Bits reserved */
struct opa_16b_mgmt {
	__be32 dest_qpn;
	__be32 src_qpn;
};

struct hfi1_16b_header {
	u32 lrh[4];
	union {
		struct {
			struct ib_grh grh;
			struct ib_other_headers oth;
		} l;
		struct ib_other_headers oth;
		struct opa_16b_mgmt mgmt;
	} u;
} __packed;

struct hfi1_opa_header {
	union {
		struct ib_header ibh; /* 9B header */
		struct hfi1_16b_header opah; /* 16B header */
	};
	u8 hdr_type; /* 9B or 16B */
} __packed;

struct hfi1_ahg_info {
	u32 ahgdesc[2];
	u16 tx_flags;
	u8 ahgcount;
	u8 ahgidx;
};

struct hfi1_sdma_header {
	__le64 pbc;
	struct hfi1_opa_header hdr;
} __packed;

/*
 * hfi1 specific data structures that will be hidden from rvt after the queue
 * pair is made common
 */
struct hfi1_qp_priv {
	struct hfi1_ahg_info *s_ahg;              /* ahg info for next header */
	struct sdma_engine *s_sde;                /* current sde */
	struct send_context *s_sendcontext;       /* current sendcontext */
	struct hfi1_ctxtdata *rcd;                /* QP's receive context */
	struct page **pages;                      /* for TID page scan */
	u32 tid_enqueue;                          /* saved when tid waited */
	u8 s_sc;		                  /* SC[0..4] for next packet */
	struct iowait s_iowait;
	struct timer_list s_tid_timer;            /* for timing tid wait */
	struct timer_list s_tid_retry_timer;      /* for timing tid ack */
	struct list_head tid_wait;                /* for queueing tid space */
	struct hfi1_opfn_data opfn;
	struct tid_flow_state flow_state;
	struct tid_rdma_qp_params tid_rdma;
	struct rvt_qp *owner;
	u16 s_running_pkt_size;
	u8 hdr_type; /* 9B or 16B */
	struct rvt_sge_state tid_ss;       /* SGE state pointer for 2nd leg */
	atomic_t n_requests;               /* # of TID RDMA requests in the */
					   /* queue */
	atomic_t n_tid_requests;            /* # of sent TID RDMA requests */
	unsigned long tid_timer_timeout_jiffies;
	unsigned long tid_retry_timeout_jiffies;

	/* variables for the TID RDMA SE state machine */
	u8 s_state;
	u8 s_retry;
	u8 rnr_nak_state;       /* RNR NAK state */
	u8 s_nak_state;
	u32 s_nak_psn;
	u32 s_flags;
	u32 s_tid_cur;
	u32 s_tid_head;
	u32 s_tid_tail;
	u32 r_tid_head;     /* Most recently added TID RDMA request */
	u32 r_tid_tail;     /* the last completed TID RDMA request */
	u32 r_tid_ack;      /* the TID RDMA request to be ACK'ed */
	u32 r_tid_alloc;    /* Request for which we are allocating resources */
	u32 pending_tid_w_segs; /* Num of pending tid write segments */
	u32 pending_tid_w_resp; /* Num of pending tid write responses */
	u32 alloc_w_segs;       /* Number of segments for which write */
			       /* resources have been allocated for this QP */

	/* For TID RDMA READ */
	u32 tid_r_reqs;         /* Num of tid reads requested */
	u32 tid_r_comp;         /* Num of tid reads completed */
	u32 pending_tid_r_segs; /* Num of pending tid read segments */
	u16 pkts_ps;            /* packets per segment */
	u8 timeout_shift;       /* account for number of packets per segment */

	u32 r_next_psn_kdeth;
	u32 r_next_psn_kdeth_save;
	u32 s_resync_psn;
	u8 sync_pt;           /* Set when QP reaches sync point */
	u8 resync;
};

#define HFI1_QP_WQE_INVALID   ((u32)-1)

struct hfi1_swqe_priv {
	struct tid_rdma_request tid_req;
	struct rvt_sge_state ss;  /* Used for TID RDMA READ Request */
};

struct hfi1_ack_priv {
	struct rvt_sge_state ss;               /* used for TID WRITE RESP */
	struct tid_rdma_request tid_req;
};

/*
 * This structure is used to hold commonly lookedup and computed values during
 * the send engine progress.
 */
struct iowait_work;
struct hfi1_pkt_state {
	struct hfi1_ibdev *dev;
	struct hfi1_ibport *ibp;
	struct hfi1_pportdata *ppd;
	struct verbs_txreq *s_txreq;
	struct iowait_work *wait;
	unsigned long flags;
	unsigned long timeout;
	unsigned long timeout_int;
	int cpu;
	u8 opcode;
	bool in_thread;
	bool pkts_sent;
};

#define HFI1_PSN_CREDIT  16

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
	struct rvt_qp __rcu *qp[2];
	struct rvt_ibport rvp;

	/* the first 16 entries are sl_to_vl for !OPA */
	u8 sl_to_sc[32];
	u8 sc_to_sl[32];
};

struct hfi1_ibdev {
	struct rvt_dev_info rdi; /* Must be first */

	/* QP numbers are shared by all IB ports */
	/* protect txwait list */
	seqlock_t txwait_lock ____cacheline_aligned_in_smp;
	struct list_head txwait;        /* list for wait verbs_txreq */
	struct list_head memwait;       /* list for wait kernel memory */
	struct kmem_cache *verbs_txreq_cache;
	u64 n_txwait;
	u64 n_kmem_wait;
	u64 n_tidwait;

	/* protect iowait lists */
	seqlock_t iowait_lock ____cacheline_aligned_in_smp;
	u64 n_piowait;
	u64 n_piodrain;
	struct timer_list mem_timer;

#ifdef CONFIG_DEBUG_FS
	/* per HFI debugfs */
	struct dentry *hfi1_ibdev_dbg;
	/* per HFI symlinks to above */
	struct dentry *hfi1_ibdev_link;
#ifdef CONFIG_FAULT_INJECTION
	struct fault *fault;
#endif
#endif
};

static inline struct hfi1_ibdev *to_idev(struct ib_device *ibdev)
{
	struct rvt_dev_info *rdi;

	rdi = container_of(ibdev, struct rvt_dev_info, ibdev);
	return container_of(rdi, struct hfi1_ibdev, rdi);
}

static inline struct rvt_qp *iowait_to_qp(struct iowait *s_iowait)
{
	struct hfi1_qp_priv *priv;

	priv = container_of(s_iowait, struct hfi1_qp_priv, s_iowait);
	return priv->owner;
}

/*
 * This must be called with s_lock held.
 */
void hfi1_bad_pkey(struct hfi1_ibport *ibp, u32 key, u32 sl,
		   u32 qp1, u32 qp2, u32 lid1, u32 lid2);
void hfi1_cap_mask_chg(struct rvt_dev_info *rdi, u32 port_num);
void hfi1_sys_guid_chg(struct hfi1_ibport *ibp);
void hfi1_node_desc_chg(struct hfi1_ibport *ibp);
int hfi1_process_mad(struct ib_device *ibdev, int mad_flags, u32 port,
		     const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		     const struct ib_mad *in_mad, struct ib_mad *out_mad,
		     size_t *out_mad_size, u16 *out_mad_pkey_index);

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
#define PSN_MASK 0x7FFFFFFF
#define PSN_SHIFT 1
#define PSN_MODIFY_MASK 0xFFFFFF

/*
 * Compare two PSNs
 * Returns an integer <, ==, or > than zero.
 */
static inline int cmp_psn(u32 a, u32 b)
{
	return (((int)a) - ((int)b)) << PSN_SHIFT;
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

static inline struct tid_rdma_request *wqe_to_tid_req(struct rvt_swqe *wqe)
{
	return &((struct hfi1_swqe_priv *)wqe->priv)->tid_req;
}

static inline struct tid_rdma_request *ack_to_tid_req(struct rvt_ack_entry *e)
{
	return &((struct hfi1_ack_priv *)e->priv)->tid_req;
}

/*
 * Look through all the active flows for a TID RDMA request and find
 * the one (if it exists) that contains the specified PSN.
 */
static inline u32 __full_flow_psn(struct flow_state *state, u32 psn)
{
	return mask_psn((state->generation << HFI1_KDETH_BTH_SEQ_SHIFT) |
			(psn & HFI1_KDETH_BTH_SEQ_MASK));
}

static inline u32 full_flow_psn(struct tid_rdma_flow *flow, u32 psn)
{
	return __full_flow_psn(&flow->flow_state, psn);
}

struct verbs_txreq;
void hfi1_put_txreq(struct verbs_txreq *tx);

int hfi1_verbs_send(struct rvt_qp *qp, struct hfi1_pkt_state *ps);

void hfi1_cnp_rcv(struct hfi1_packet *packet);

void hfi1_uc_rcv(struct hfi1_packet *packet);

void hfi1_rc_rcv(struct hfi1_packet *packet);

void hfi1_rc_hdrerr(
	struct hfi1_ctxtdata *rcd,
	struct hfi1_packet *packet,
	struct rvt_qp *qp);

u8 ah_to_sc(struct ib_device *ibdev, struct rdma_ah_attr *ah_attr);

void hfi1_rc_verbs_aborted(struct rvt_qp *qp, struct hfi1_opa_header *opah);
void hfi1_rc_send_complete(struct rvt_qp *qp, struct hfi1_opa_header *opah);

void hfi1_ud_rcv(struct hfi1_packet *packet);

int hfi1_lookup_pkey_idx(struct hfi1_ibport *ibp, u16 pkey);

void hfi1_migrate_qp(struct rvt_qp *qp);

int hfi1_check_modify_qp(struct rvt_qp *qp, struct ib_qp_attr *attr,
			 int attr_mask, struct ib_udata *udata);

void hfi1_modify_qp(struct rvt_qp *qp, struct ib_qp_attr *attr,
		    int attr_mask, struct ib_udata *udata);
void hfi1_restart_rc(struct rvt_qp *qp, u32 psn, int wait);
int hfi1_setup_wqe(struct rvt_qp *qp, struct rvt_swqe *wqe,
		   bool *call_send);

extern const u32 rc_only_opcode;
extern const u32 uc_only_opcode;

int hfi1_ruc_check_hdr(struct hfi1_ibport *ibp, struct hfi1_packet *packet);

u32 hfi1_make_grh(struct hfi1_ibport *ibp, struct ib_grh *hdr,
		  const struct ib_global_route *grh, u32 hwords, u32 nwords);

void hfi1_make_ruc_header(struct rvt_qp *qp, struct ib_other_headers *ohdr,
			  u32 bth0, u32 bth1, u32 bth2, int middle,
			  struct hfi1_pkt_state *ps);

bool hfi1_schedule_send_yield(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			      bool tid);

void _hfi1_do_send(struct work_struct *work);

void hfi1_do_send_from_rvt(struct rvt_qp *qp);

void hfi1_do_send(struct rvt_qp *qp, bool in_thread);

void hfi1_send_rc_ack(struct hfi1_packet *packet, bool is_fecn);

int hfi1_make_rc_req(struct rvt_qp *qp, struct hfi1_pkt_state *ps);

int hfi1_make_uc_req(struct rvt_qp *qp, struct hfi1_pkt_state *ps);

int hfi1_make_ud_req(struct rvt_qp *qp, struct hfi1_pkt_state *ps);

int hfi1_register_ib_device(struct hfi1_devdata *);

void hfi1_unregister_ib_device(struct hfi1_devdata *);

void hfi1_kdeth_eager_rcv(struct hfi1_packet *packet);

void hfi1_kdeth_expected_rcv(struct hfi1_packet *packet);

void hfi1_ib_rcv(struct hfi1_packet *packet);

void hfi1_16B_rcv(struct hfi1_packet *packet);

unsigned hfi1_get_npkeys(struct hfi1_devdata *);

int hfi1_verbs_send_dma(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			u64 pbc);

int hfi1_verbs_send_pio(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			u64 pbc);

static inline bool opa_bth_is_migration(struct ib_other_headers *ohdr)
{
	return ohdr->bth[1] & cpu_to_be32(OPA_BTH_MIG_REQ);
}

void hfi1_wait_kmem(struct rvt_qp *qp);

static inline void hfi1_trdma_send_complete(struct rvt_qp *qp,
					    struct rvt_swqe *wqe,
					    enum ib_wc_status status)
{
	trdma_clean_swqe(qp, wqe);
	rvt_send_complete(qp, wqe, status);
}

extern const enum ib_wc_opcode ib_hfi1_wc_opcode[];

extern const u8 hdr_len_by_opcode[];

extern const int ib_rvt_state_ops[];

extern __be64 ib_hfi1_sys_image_guid;    /* in network order */

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

extern unsigned short piothreshold;

extern const u32 ib_hfi1_rnr_table[];

#endif                          /* HFI1_VERBS_H */
