/*
 * Copyright (c) 2012 - 2018 Intel Corporation.  All rights reserved.
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
#include <rdma/ib_hdrs.h>
#include <rdma/rdmavt_qp.h>
#include <rdma/rdmavt_cq.h>

struct qib_ctxtdata;
struct qib_pportdata;
struct qib_devdata;
struct qib_verbs_txreq;

#define QIB_MAX_RDMA_ATOMIC     16
#define QIB_GUIDS_PER_PORT	5
#define QIB_PSN_SHIFT		8

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define QIB_UVERBS_ABI_VERSION       2

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

struct qib_pio_header {
	__le32 pbc[2];
	struct ib_header hdr;
} __packed;

/*
 * qib specific data structure that will be hidden from rvt after the queue pair
 * is made common.
 */
struct qib_qp_priv {
	struct ib_header *s_hdr;        /* next packet header to send */
	struct list_head iowait;        /* link for wait PIO buf */
	atomic_t s_dma_busy;
	struct qib_verbs_txreq *s_tx;
	struct work_struct s_work;
	wait_queue_head_t wait_dma;
	struct rvt_qp *owner;
};

#define QIB_PSN_CREDIT  16

struct qib_opcode_stats {
	u64 n_packets;          /* number of packets */
	u64 n_bytes;            /* total number of bytes */
};

struct qib_opcode_stats_perctx {
	struct qib_opcode_stats stats[128];
};

struct qib_pma_counters {
	u64 n_unicast_xmit;     /* total unicast packets sent */
	u64 n_unicast_rcv;      /* total unicast packets received */
	u64 n_multicast_xmit;   /* total multicast packets sent */
	u64 n_multicast_rcv;    /* total multicast packets received */
};

struct qib_ibport {
	struct rvt_ibport rvp;
	struct rvt_ah *smi_ah;
	__be64 guids[QIB_GUIDS_PER_PORT	- 1];	/* writable GUIDs */
	struct qib_pma_counters __percpu *pmastats;
	u64 z_unicast_xmit;     /* starting count for PMA */
	u64 z_unicast_rcv;      /* starting count for PMA */
	u64 z_multicast_xmit;   /* starting count for PMA */
	u64 z_multicast_rcv;    /* starting count for PMA */
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
	u8 sl_to_vl[16];
};

struct qib_ibdev {
	struct rvt_dev_info rdi;

	struct list_head piowait;       /* list for wait PIO buf */
	struct list_head dmawait;	/* list for wait DMA */
	struct list_head txwait;        /* list for wait qib_verbs_txreq */
	struct list_head memwait;       /* list for wait kernel memory */
	struct list_head txreq_free;
	struct timer_list mem_timer;
	struct qib_pio_header *pio_hdrs;
	dma_addr_t pio_hdrs_phys;

	u32 n_piowait;
	u32 n_txwait;

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

static inline struct qib_ibdev *to_idev(struct ib_device *ibdev)
{
	struct rvt_dev_info *rdi;

	rdi = container_of(ibdev, struct rvt_dev_info, ibdev);
	return container_of(rdi, struct qib_ibdev, rdi);
}

/*
 * Send if not busy or waiting for I/O and either
 * a RC response is pending or we can process send work requests.
 */
static inline int qib_send_ok(struct rvt_qp *qp)
{
	return !(qp->s_flags & (RVT_S_BUSY | RVT_S_ANY_WAIT_IO)) &&
		(qp->s_hdrwords || (qp->s_flags & RVT_S_RESP_PENDING) ||
		 !(qp->s_flags & RVT_S_ANY_WAIT_SEND));
}

bool _qib_schedule_send(struct rvt_qp *qp);
bool qib_schedule_send(struct rvt_qp *qp);

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

void qib_bad_pkey(struct qib_ibport *ibp, u32 key, u32 sl,
		  u32 qp1, u32 qp2, __be16 lid1, __be16 lid2);
void qib_cap_mask_chg(struct rvt_dev_info *rdi, u32 port_num);
void qib_sys_guid_chg(struct qib_ibport *ibp);
void qib_node_desc_chg(struct qib_ibport *ibp);
int qib_process_mad(struct ib_device *ibdev, int mad_flags, u32 port_num,
		    const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		    const struct ib_mad *in, struct ib_mad *out,
		    size_t *out_mad_size, u16 *out_mad_pkey_index);
void qib_notify_create_mad_agent(struct rvt_dev_info *rdi, int port_idx);
void qib_notify_free_mad_agent(struct rvt_dev_info *rdi, int port_idx);

/*
 * Compare the lower 24 bits of the two values.
 * Returns an integer <, ==, or > than zero.
 */
static inline int qib_cmp24(u32 a, u32 b)
{
	return (((int) a) - ((int) b)) << 8;
}

int qib_snapshot_counters(struct qib_pportdata *ppd, u64 *swords,
			  u64 *rwords, u64 *spkts, u64 *rpkts,
			  u64 *xmit_wait);

int qib_get_counters(struct qib_pportdata *ppd,
		     struct qib_verbs_counters *cntrs);

/*
 * Functions provided by qib driver for rdmavt to use
 */
unsigned qib_free_all_qps(struct rvt_dev_info *rdi);
void *qib_qp_priv_alloc(struct rvt_dev_info *rdi, struct rvt_qp *qp);
void qib_qp_priv_free(struct rvt_dev_info *rdi, struct rvt_qp *qp);
void qib_notify_qp_reset(struct rvt_qp *qp);
int qib_alloc_qpn(struct rvt_dev_info *rdi, struct rvt_qpn_table *qpt,
		  enum ib_qp_type type, u32 port);
void qib_restart_rc(struct rvt_qp *qp, u32 psn, int wait);
#ifdef CONFIG_DEBUG_FS

void qib_qp_iter_print(struct seq_file *s, struct rvt_qp_iter *iter);

#endif

unsigned qib_pkt_delay(u32 plen, u8 snd_mult, u8 rcv_mult);

void qib_verbs_sdma_desc_avail(struct qib_pportdata *ppd, unsigned avail);

void qib_put_txreq(struct qib_verbs_txreq *tx);

int qib_verbs_send(struct rvt_qp *qp, struct ib_header *hdr,
		   u32 hdrwords, struct rvt_sge_state *ss, u32 len);

void qib_uc_rcv(struct qib_ibport *ibp, struct ib_header *hdr,
		int has_grh, void *data, u32 tlen, struct rvt_qp *qp);

void qib_rc_rcv(struct qib_ctxtdata *rcd, struct ib_header *hdr,
		int has_grh, void *data, u32 tlen, struct rvt_qp *qp);

int qib_check_ah(struct ib_device *ibdev, struct rdma_ah_attr *ah_attr);

int qib_check_send_wqe(struct rvt_qp *qp, struct rvt_swqe *wqe,
		       bool *call_send);

struct ib_ah *qib_create_qp0_ah(struct qib_ibport *ibp, u16 dlid);

void qib_rc_send_complete(struct rvt_qp *qp, struct ib_header *hdr);

int qib_post_ud_send(struct rvt_qp *qp, const struct ib_send_wr *wr);

void qib_ud_rcv(struct qib_ibport *ibp, struct ib_header *hdr,
		int has_grh, void *data, u32 tlen, struct rvt_qp *qp);

void qib_migrate_qp(struct rvt_qp *qp);

int qib_ruc_check_hdr(struct qib_ibport *ibp, struct ib_header *hdr,
		      int has_grh, struct rvt_qp *qp, u32 bth0);

u32 qib_make_grh(struct qib_ibport *ibp, struct ib_grh *hdr,
		 const struct ib_global_route *grh, u32 hwords, u32 nwords);

void qib_make_ruc_header(struct rvt_qp *qp, struct ib_other_headers *ohdr,
			 u32 bth0, u32 bth2);

void _qib_do_send(struct work_struct *work);

void qib_do_send(struct rvt_qp *qp);

void qib_send_rc_ack(struct rvt_qp *qp);

int qib_make_rc_req(struct rvt_qp *qp, unsigned long *flags);

int qib_make_uc_req(struct rvt_qp *qp, unsigned long *flags);

int qib_make_ud_req(struct rvt_qp *qp, unsigned long *flags);

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

extern const int ib_rvt_state_ops[];

extern __be64 ib_qib_sys_image_guid;    /* in network order */

extern unsigned int ib_rvt_lkey_table_size;

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

extern const struct rvt_operation_params qib_post_parms[];

#endif                          /* QIB_VERBS_H */
