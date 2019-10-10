/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */
#ifndef HFI1_TID_RDMA_H
#define HFI1_TID_RDMA_H

#include <linux/circ_buf.h>
#include "common.h"

/* Add a convenience helper */
#define CIRC_ADD(val, add, size) (((val) + (add)) & ((size) - 1))
#define CIRC_NEXT(val, size) CIRC_ADD(val, 1, size)
#define CIRC_PREV(val, size) CIRC_ADD(val, -1, size)

#define TID_RDMA_MIN_SEGMENT_SIZE       BIT(18)   /* 256 KiB (for now) */
#define TID_RDMA_MAX_SEGMENT_SIZE       BIT(18)   /* 256 KiB (for now) */
#define TID_RDMA_MAX_PAGES              (BIT(18) >> PAGE_SHIFT)

/*
 * Bit definitions for priv->s_flags.
 * These bit flags overload the bit flags defined for the QP's s_flags.
 * Due to the fact that these bit fields are used only for the QP priv
 * s_flags, there are no collisions.
 *
 * HFI1_S_TID_WAIT_INTERLCK - QP is waiting for requester interlock
 * HFI1_R_TID_WAIT_INTERLCK - QP is waiting for responder interlock
 */
#define HFI1_S_TID_BUSY_SET       BIT(0)
/* BIT(1) reserved for RVT_S_BUSY. */
#define HFI1_R_TID_RSC_TIMER      BIT(2)
/* BIT(3) reserved for RVT_S_RESP_PENDING. */
/* BIT(4) reserved for RVT_S_ACK_PENDING. */
#define HFI1_S_TID_WAIT_INTERLCK  BIT(5)
#define HFI1_R_TID_WAIT_INTERLCK  BIT(6)
/* BIT(7) - BIT(15) reserved for RVT_S_WAIT_*. */
/* BIT(16) reserved for RVT_S_SEND_ONE */
#define HFI1_S_TID_RETRY_TIMER    BIT(17)
/* BIT(18) reserved for RVT_S_ECN. */
#define HFI1_R_TID_SW_PSN         BIT(19)
/* BIT(26) reserved for HFI1_S_WAIT_HALT */
/* BIT(27) reserved for HFI1_S_WAIT_TID_RESP */
/* BIT(28) reserved for HFI1_S_WAIT_TID_SPACE */

/*
 * Unlike regular IB RDMA VERBS, which do not require an entry
 * in the s_ack_queue, TID RDMA WRITE requests do because they
 * generate responses.
 * Therefore, the s_ack_queue needs to be extended by a certain
 * amount. The key point is that the queue needs to be extended
 * without letting the "user" know so they user doesn't end up
 * using these extra entries.
 */
#define HFI1_TID_RDMA_WRITE_CNT 8

struct tid_rdma_params {
	struct rcu_head rcu_head;
	u32 qp;
	u32 max_len;
	u16 jkey;
	u8 max_read;
	u8 max_write;
	u8 timeout;
	u8 urg;
	u8 version;
};

struct tid_rdma_qp_params {
	struct work_struct trigger_work;
	struct tid_rdma_params local;
	struct tid_rdma_params __rcu *remote;
};

/* Track state for each hardware flow */
struct tid_flow_state {
	u32 generation;
	u32 psn;
	u8 index;
	u8 last_index;
};

enum tid_rdma_req_state {
	TID_REQUEST_INACTIVE = 0,
	TID_REQUEST_INIT,
	TID_REQUEST_INIT_RESEND,
	TID_REQUEST_ACTIVE,
	TID_REQUEST_RESEND,
	TID_REQUEST_RESEND_ACTIVE,
	TID_REQUEST_QUEUED,
	TID_REQUEST_SYNC,
	TID_REQUEST_RNR_NAK,
	TID_REQUEST_COMPLETE,
};

struct tid_rdma_request {
	struct rvt_qp *qp;
	struct hfi1_ctxtdata *rcd;
	union {
		struct rvt_swqe *swqe;
		struct rvt_ack_entry *ack;
	} e;

	struct tid_rdma_flow *flows;	/* array of tid flows */
	struct rvt_sge_state ss; /* SGE state for TID RDMA requests */
	u16 n_flows;		/* size of the flow buffer window */
	u16 setup_head;		/* flow index we are setting up */
	u16 clear_tail;		/* flow index we are clearing */
	u16 flow_idx;		/* flow index most recently set up */
	u16 acked_tail;

	u32 seg_len;
	u32 total_len;
	u32 r_ack_psn;          /* next expected ack PSN */
	u32 r_flow_psn;         /* IB PSN of next segment start */
	u32 r_last_acked;       /* IB PSN of last ACK'ed packet */
	u32 s_next_psn;		/* IB PSN of next segment start for read */

	u32 total_segs;		/* segments required to complete a request */
	u32 cur_seg;		/* index of current segment */
	u32 comp_seg;           /* index of last completed segment */
	u32 ack_seg;            /* index of last ack'ed segment */
	u32 alloc_seg;          /* index of next segment to be allocated */
	u32 isge;		/* index of "current" sge */
	u32 ack_pending;        /* num acks pending for this request */

	enum tid_rdma_req_state state;
};

/*
 * When header suppression is used, PSNs associated with a "flow" are
 * relevant (and not the PSNs maintained by verbs). Track per-flow
 * PSNs here for a TID RDMA segment.
 *
 */
struct flow_state {
	u32 flags;
	u32 resp_ib_psn;     /* The IB PSN of the response for this flow */
	u32 generation;      /* generation of flow */
	u32 spsn;            /* starting PSN in TID space */
	u32 lpsn;            /* last PSN in TID space */
	u32 r_next_psn;      /* next PSN to be received (in TID space) */

	/* For tid rdma read */
	u32 ib_spsn;         /* starting PSN in Verbs space */
	u32 ib_lpsn;         /* last PSn in Verbs space */
};

struct tid_rdma_pageset {
	dma_addr_t addr : 48; /* Only needed for the first page */
	u8 idx: 8;
	u8 count : 7;
	u8 mapped: 1;
};

/**
 * kern_tid_node - used for managing TID's in TID groups
 *
 * @grp_idx: rcd relative index to tid_group
 * @map: grp->map captured prior to programming this TID group in HW
 * @cnt: Only @cnt of available group entries are actually programmed
 */
struct kern_tid_node {
	struct tid_group *grp;
	u8 map;
	u8 cnt;
};

/* Overall info for a TID RDMA segment */
struct tid_rdma_flow {
	/*
	 * While a TID RDMA segment is being transferred, it uses a QP number
	 * from the "KDETH section of QP numbers" (which is different from the
	 * QP number that originated the request). Bits 11-15 of these QP
	 * numbers identify the "TID flow" for the segment.
	 */
	struct flow_state flow_state;
	struct tid_rdma_request *req;
	u32 tid_qpn;
	u32 tid_offset;
	u32 length;
	u32 sent;
	u8 tnode_cnt;
	u8 tidcnt;
	u8 tid_idx;
	u8 idx;
	u8 npagesets;
	u8 npkts;
	u8 pkt;
	u8 resync_npkts;
	struct kern_tid_node tnode[TID_RDMA_MAX_PAGES];
	struct tid_rdma_pageset pagesets[TID_RDMA_MAX_PAGES];
	u32 tid_entry[TID_RDMA_MAX_PAGES];
};

enum tid_rnr_nak_state {
	TID_RNR_NAK_INIT = 0,
	TID_RNR_NAK_SEND,
	TID_RNR_NAK_SENT,
};

bool tid_rdma_conn_req(struct rvt_qp *qp, u64 *data);
bool tid_rdma_conn_reply(struct rvt_qp *qp, u64 data);
bool tid_rdma_conn_resp(struct rvt_qp *qp, u64 *data);
void tid_rdma_conn_error(struct rvt_qp *qp);
void tid_rdma_opfn_init(struct rvt_qp *qp, struct tid_rdma_params *p);

int hfi1_kern_exp_rcv_init(struct hfi1_ctxtdata *rcd, int reinit);
int hfi1_kern_exp_rcv_setup(struct tid_rdma_request *req,
			    struct rvt_sge_state *ss, bool *last);
int hfi1_kern_exp_rcv_clear(struct tid_rdma_request *req);
void hfi1_kern_exp_rcv_clear_all(struct tid_rdma_request *req);
void __trdma_clean_swqe(struct rvt_qp *qp, struct rvt_swqe *wqe);

/**
 * trdma_clean_swqe - clean flows for swqe if large send queue
 * @qp: the qp
 * @wqe: the send wqe
 */
static inline void trdma_clean_swqe(struct rvt_qp *qp, struct rvt_swqe *wqe)
{
	if (!wqe->priv)
		return;
	__trdma_clean_swqe(qp, wqe);
}

void hfi1_kern_read_tid_flow_free(struct rvt_qp *qp);

int hfi1_qp_priv_init(struct rvt_dev_info *rdi, struct rvt_qp *qp,
		      struct ib_qp_init_attr *init_attr);
void hfi1_qp_priv_tid_free(struct rvt_dev_info *rdi, struct rvt_qp *qp);

void hfi1_tid_rdma_flush_wait(struct rvt_qp *qp);

int hfi1_kern_setup_hw_flow(struct hfi1_ctxtdata *rcd, struct rvt_qp *qp);
void hfi1_kern_clear_hw_flow(struct hfi1_ctxtdata *rcd, struct rvt_qp *qp);
void hfi1_kern_init_ctxt_generations(struct hfi1_ctxtdata *rcd);

struct cntr_entry;
u64 hfi1_access_sw_tid_wait(const struct cntr_entry *entry,
			    void *context, int vl, int mode, u64 data);

u32 hfi1_build_tid_rdma_read_packet(struct rvt_swqe *wqe,
				    struct ib_other_headers *ohdr,
				    u32 *bth1, u32 *bth2, u32 *len);
u32 hfi1_build_tid_rdma_read_req(struct rvt_qp *qp, struct rvt_swqe *wqe,
				 struct ib_other_headers *ohdr, u32 *bth1,
				 u32 *bth2, u32 *len);
void hfi1_rc_rcv_tid_rdma_read_req(struct hfi1_packet *packet);
u32 hfi1_build_tid_rdma_read_resp(struct rvt_qp *qp, struct rvt_ack_entry *e,
				  struct ib_other_headers *ohdr, u32 *bth0,
				  u32 *bth1, u32 *bth2, u32 *len, bool *last);
void hfi1_rc_rcv_tid_rdma_read_resp(struct hfi1_packet *packet);
bool hfi1_handle_kdeth_eflags(struct hfi1_ctxtdata *rcd,
			      struct hfi1_pportdata *ppd,
			      struct hfi1_packet *packet);
void hfi1_tid_rdma_restart_req(struct rvt_qp *qp, struct rvt_swqe *wqe,
			       u32 *bth2);
void hfi1_qp_kern_exp_rcv_clear_all(struct rvt_qp *qp);
bool hfi1_tid_rdma_wqe_interlock(struct rvt_qp *qp, struct rvt_swqe *wqe);

void setup_tid_rdma_wqe(struct rvt_qp *qp, struct rvt_swqe *wqe);
static inline void hfi1_setup_tid_rdma_wqe(struct rvt_qp *qp,
					   struct rvt_swqe *wqe)
{
	if (wqe->priv &&
	    (wqe->wr.opcode == IB_WR_RDMA_READ ||
	     wqe->wr.opcode == IB_WR_RDMA_WRITE) &&
	    wqe->length >= TID_RDMA_MIN_SEGMENT_SIZE)
		setup_tid_rdma_wqe(qp, wqe);
}

u32 hfi1_build_tid_rdma_write_req(struct rvt_qp *qp, struct rvt_swqe *wqe,
				  struct ib_other_headers *ohdr,
				  u32 *bth1, u32 *bth2, u32 *len);

void hfi1_compute_tid_rdma_flow_wt(void);

void hfi1_rc_rcv_tid_rdma_write_req(struct hfi1_packet *packet);

u32 hfi1_build_tid_rdma_write_resp(struct rvt_qp *qp, struct rvt_ack_entry *e,
				   struct ib_other_headers *ohdr, u32 *bth1,
				   u32 bth2, u32 *len,
				   struct rvt_sge_state **ss);

void hfi1_del_tid_reap_timer(struct rvt_qp *qp);

void hfi1_rc_rcv_tid_rdma_write_resp(struct hfi1_packet *packet);

bool hfi1_build_tid_rdma_packet(struct rvt_swqe *wqe,
				struct ib_other_headers *ohdr,
				u32 *bth1, u32 *bth2, u32 *len);

void hfi1_rc_rcv_tid_rdma_write_data(struct hfi1_packet *packet);

u32 hfi1_build_tid_rdma_write_ack(struct rvt_qp *qp, struct rvt_ack_entry *e,
				  struct ib_other_headers *ohdr, u16 iflow,
				  u32 *bth1, u32 *bth2);

void hfi1_rc_rcv_tid_rdma_ack(struct hfi1_packet *packet);

void hfi1_add_tid_retry_timer(struct rvt_qp *qp);
void hfi1_del_tid_retry_timer(struct rvt_qp *qp);

u32 hfi1_build_tid_rdma_resync(struct rvt_qp *qp, struct rvt_swqe *wqe,
			       struct ib_other_headers *ohdr, u32 *bth1,
			       u32 *bth2, u16 fidx);

void hfi1_rc_rcv_tid_rdma_resync(struct hfi1_packet *packet);

struct hfi1_pkt_state;
int hfi1_make_tid_rdma_pkt(struct rvt_qp *qp, struct hfi1_pkt_state *ps);

void _hfi1_do_tid_send(struct work_struct *work);

bool hfi1_schedule_tid_send(struct rvt_qp *qp);

bool hfi1_tid_rdma_ack_interlock(struct rvt_qp *qp, struct rvt_ack_entry *e);

#endif /* HFI1_TID_RDMA_H */
