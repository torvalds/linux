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
#include <rdma/rdma_vt.h>
#include <rdma/rdmavt_qp.h>

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

#define IB_BTH_REQ_ACK		BIT(31)
#define IB_BTH_SOLICITED	BIT(23)
#define IB_BTH_MIG_REQ		BIT(22)

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
	struct rvt_qp *qp;
};

struct hfi1_mcast {
	struct rb_node rb_node;
	union ib_gid mgid;
	struct list_head qp_list;
	wait_queue_head_t wait;
	atomic_t refcount;
	int n_attached;
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
	struct rvt_mmap_info *ip;
};

/*
 * hfi1 specific data structures that will be hidden from rvt after the queue
 * pair is made common
 */
struct hfi1_qp_priv {
	struct ahg_ib_header *s_hdr; /* next packet header to send */
	struct sdma_engine *s_sde;   /* current sde */
	u8 s_sc;		     /* SC[0..4] for next packet */
	u8 r_adefered;               /* number of acks defered */
	struct iowait s_iowait;
	struct rvt_qp *owner;
};

/*
 * This structure is used to hold commonly lookedup and computed values during
 * the send engine progress.
 */
struct hfi1_pkt_state {
	struct hfi1_ibdev *dev;
	struct hfi1_ibport *ibp;
	struct hfi1_pportdata *ppd;
};

#define HFI1_PSN_CREDIT  16

/*
 * Since struct rvt_swqe is not a fixed size, we can't simply index into
 * struct hfi1_qp.s_wq.  This function does the array index computation.
 */
static inline struct rvt_swqe *get_swqe_ptr(struct rvt_qp *qp,
					    unsigned n)
{
	return (struct rvt_swqe *)((char *)qp->s_wq +
				     (sizeof(struct rvt_swqe) +
				      qp->s_max_sge *
				      sizeof(struct rvt_sge)) * n);
}

/*
 * Since struct rvt_rwqe is not a fixed size, we can't simply index into
 * struct rvt_rwq.wq.  This function does the array index computation.
 */
static inline struct rvt_rwqe *get_rwqe_ptr(struct rvt_rq *rq, unsigned n)
{
	return (struct rvt_rwqe *)
		((char *) rq->wq->wq +
		 (sizeof(struct rvt_rwqe) +
		  rq->max_sge * sizeof(struct ib_sge)) * n);
}

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
	struct rvt_ah *sm_ah;
	struct rvt_ah *smi_ah;

	__be64 guids[HFI1_GUIDS_PER_PORT	- 1];	/* writable GUIDs */

	/* the first 16 entries are sl_to_vl for !OPA */
	u8 sl_to_sc[32];
	u8 sc_to_sl[32];
};

struct hfi1_qp_ibdev;
struct hfi1_ibdev {
	struct rvt_dev_info rdi; /* Must be first */

	struct hfi1_qp_ibdev *qp_dev;

	/* QP numbers are shared by all IB ports */
	/* protect wait lists */
	seqlock_t iowait_lock;
	struct list_head txwait;        /* list for wait verbs_txreq */
	struct list_head memwait;       /* list for wait kernel memory */
	struct list_head txreq_free;
	struct kmem_cache *verbs_txreq_cache;
	struct timer_list mem_timer;

	u64 n_piowait;
	u64 n_txwait;
	u64 n_kmem_wait;
	u64 n_send_schedule;

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

static inline struct hfi1_cq *to_icq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct hfi1_cq, ibcq);
}

static inline struct rvt_qp *to_iqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct rvt_qp, ibqp);
}

static inline struct hfi1_ibdev *to_idev(struct ib_device *ibdev)
{
	struct rvt_dev_info *rdi;

	rdi = container_of(ibdev, struct rvt_dev_info, ibdev);
	return container_of(rdi, struct hfi1_ibdev, rdi);
}

static inline struct rvt_qp *iowait_to_qp(struct  iowait *s_iowait)
{
	struct hfi1_qp_priv *priv;

	priv = container_of(s_iowait, struct hfi1_qp_priv, s_iowait);
	return priv->owner;
}

/*
 * Send if not busy or waiting for I/O and either
 * a RC response is pending or we can process send work requests.
 */
static inline int hfi1_send_ok(struct rvt_qp *qp)
{
	return !(qp->s_flags & (RVT_S_BUSY | RVT_S_ANY_WAIT_IO)) &&
		(qp->s_hdrwords || (qp->s_flags & RVT_S_RESP_PENDING) ||
		 !(qp->s_flags & RVT_S_ANY_WAIT_SEND));
}

/*
 * This must be called with s_lock held.
 */
void hfi1_bad_pqkey(struct hfi1_ibport *ibp, __be16 trap_num, u32 key, u32 sl,
		    u32 qp1, u32 qp2, u16 lid1, u16 lid2);
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

int hfi1_verbs_send(struct rvt_qp *qp, struct hfi1_pkt_state *ps);

void hfi1_copy_sge(struct rvt_sge_state *ss, void *data, u32 length,
		   int release);

void hfi1_skip_sge(struct rvt_sge_state *ss, u32 length, int release);

void hfi1_cnp_rcv(struct hfi1_packet *packet);

void hfi1_uc_rcv(struct hfi1_packet *packet);

void hfi1_rc_rcv(struct hfi1_packet *packet);

void hfi1_rc_hdrerr(
	struct hfi1_ctxtdata *rcd,
	struct hfi1_ib_header *hdr,
	u32 rcv_flags,
	struct rvt_qp *qp);

u8 ah_to_sc(struct ib_device *ibdev, struct ib_ah_attr *ah_attr);

struct ib_ah *hfi1_create_qp0_ah(struct hfi1_ibport *ibp, u16 dlid);

void hfi1_rc_rnr_retry(unsigned long arg);

void hfi1_rc_send_complete(struct rvt_qp *qp, struct hfi1_ib_header *hdr);

void hfi1_rc_error(struct rvt_qp *qp, enum ib_wc_status err);

void hfi1_ud_rcv(struct hfi1_packet *packet);

int hfi1_lookup_pkey_idx(struct hfi1_ibport *ibp, u16 pkey);

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

static inline void hfi1_put_ss(struct rvt_sge_state *ss)
{
	while (ss->num_sge) {
		rvt_put_mr(ss->sge.mr);
		if (--ss->num_sge)
			ss->sge = *ss->sg_list++;
	}
}

int hfi1_get_rwqe(struct rvt_qp *qp, int wr_id_only);

void hfi1_migrate_qp(struct rvt_qp *qp);

int hfi1_ruc_check_hdr(struct hfi1_ibport *ibp, struct hfi1_ib_header *hdr,
		       int has_grh, struct rvt_qp *qp, u32 bth0);

u32 hfi1_make_grh(struct hfi1_ibport *ibp, struct ib_grh *hdr,
		  struct ib_global_route *grh, u32 hwords, u32 nwords);

void hfi1_make_ruc_header(struct rvt_qp *qp, struct hfi1_other_headers *ohdr,
			  u32 bth0, u32 bth2, int middle);

void hfi1_do_send(struct work_struct *work);

void hfi1_send_complete(struct rvt_qp *qp, struct rvt_swqe *wqe,
			enum ib_wc_status status);

void hfi1_send_rc_ack(struct hfi1_ctxtdata *, struct rvt_qp *qp, int is_fecn);

int hfi1_make_rc_req(struct rvt_qp *qp);

int hfi1_make_uc_req(struct rvt_qp *qp);

int hfi1_make_ud_req(struct rvt_qp *qp);

int hfi1_register_ib_device(struct hfi1_devdata *);

void hfi1_unregister_ib_device(struct hfi1_devdata *);

void hfi1_ib_rcv(struct hfi1_packet *packet);

unsigned hfi1_get_npkeys(struct hfi1_devdata *);

int hfi1_verbs_send_dma(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			u64 pbc);

int hfi1_verbs_send_pio(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			u64 pbc);

struct send_context *qp_to_send_context(struct rvt_qp *qp, u8 sc5);

extern const enum ib_wc_opcode ib_hfi1_wc_opcode[];

extern const u8 hdr_len_by_opcode[];

extern const int ib_hfi1_state_ops[];

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

extern const u32 ib_hfi1_rnr_table[];

#endif                          /* HFI1_VERBS_H */
