/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */

#ifndef HFI1_RC_H
#define HFI1_RC_H

/* cut down ridiculously long IB macro names */
#define OP(x) IB_OPCODE_RC_##x

static inline void update_ack_queue(struct rvt_qp *qp, unsigned int n)
{
	unsigned int next;

	next = n + 1;
	if (next > rvt_size_atomic(ib_to_rvt(qp->ibqp.device)))
		next = 0;
	qp->s_tail_ack_queue = next;
	qp->s_acked_ack_queue = next;
	qp->s_ack_state = OP(ACKNOWLEDGE);
}

static inline void rc_defered_ack(struct hfi1_ctxtdata *rcd,
				  struct rvt_qp *qp)
{
	if (list_empty(&qp->rspwait)) {
		qp->r_flags |= RVT_R_RSP_NAK;
		rvt_get_qp(qp);
		list_add_tail(&qp->rspwait, &rcd->qp_wait_list);
	}
}

static inline u32 restart_sge(struct rvt_sge_state *ss, struct rvt_swqe *wqe,
			      u32 psn, u32 pmtu)
{
	u32 len;

	len = delta_psn(psn, wqe->psn) * pmtu;
	return rvt_restart_sge(ss, wqe, len);
}

static inline void release_rdma_sge_mr(struct rvt_ack_entry *e)
{
	if (e->rdma_sge.mr) {
		rvt_put_mr(e->rdma_sge.mr);
		e->rdma_sge.mr = NULL;
	}
}

struct rvt_ack_entry *find_prev_entry(struct rvt_qp *qp, u32 psn, u8 *prev,
				      u8 *prev_ack, bool *scheduled);
int do_rc_ack(struct rvt_qp *qp, u32 aeth, u32 psn, int opcode, u64 val,
	      struct hfi1_ctxtdata *rcd);
struct rvt_swqe *do_rc_completion(struct rvt_qp *qp, struct rvt_swqe *wqe,
				  struct hfi1_ibport *ibp);

#endif /* HFI1_RC_H */
