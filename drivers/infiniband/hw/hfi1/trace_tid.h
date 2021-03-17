/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */
#if !defined(__HFI1_TRACE_TID_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HFI1_TRACE_TID_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "hfi.h"

#define tidtype_name(type) { PT_##type, #type }
#define show_tidtype(type)                   \
__print_symbolic(type,                       \
	tidtype_name(EXPECTED),              \
	tidtype_name(EAGER),                 \
	tidtype_name(INVALID))               \

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_tid

u8 hfi1_trace_get_tid_ctrl(u32 ent);
u16 hfi1_trace_get_tid_len(u32 ent);
u16 hfi1_trace_get_tid_idx(u32 ent);

#define OPFN_PARAM_PRN "[%s] qpn 0x%x %s OPFN: qp 0x%x, max read %u, " \
		       "max write %u, max length %u, jkey 0x%x timeout %u " \
		       "urg %u"

#define TID_FLOW_PRN "[%s] qpn 0x%x flow %d: idx %d resp_ib_psn 0x%x " \
		     "generation 0x%x fpsn 0x%x-%x r_next_psn 0x%x " \
		     "ib_psn 0x%x-%x npagesets %u tnode_cnt %u " \
		     "tidcnt %u tid_idx %u tid_offset %u length %u sent %u"

#define TID_NODE_PRN "[%s] qpn 0x%x  %s idx %u grp base 0x%x map 0x%x " \
		     "used %u cnt %u"

#define RSP_INFO_PRN "[%s] qpn 0x%x state 0x%x s_state 0x%x psn 0x%x " \
		     "r_psn 0x%x r_state 0x%x r_flags 0x%x " \
		     "r_head_ack_queue %u s_tail_ack_queue %u " \
		     "s_acked_ack_queue %u s_ack_state 0x%x " \
		     "s_nak_state 0x%x s_flags 0x%x ps_flags 0x%x " \
		     "iow_flags 0x%lx"

#define SENDER_INFO_PRN "[%s] qpn 0x%x state 0x%x s_cur %u s_tail %u " \
			"s_head %u s_acked %u s_last %u s_psn 0x%x " \
			"s_last_psn 0x%x s_flags 0x%x ps_flags 0x%x " \
			"iow_flags 0x%lx s_state 0x%x s_num_rd %u s_retry %u"

#define TID_READ_SENDER_PRN "[%s] qpn 0x%x newreq %u tid_r_reqs %u " \
			    "tid_r_comp %u pending_tid_r_segs %u " \
			    "s_flags 0x%x ps_flags 0x%x iow_flags 0x%lx " \
			    "s_state 0x%x hw_flow_index %u generation 0x%x " \
			    "fpsn 0x%x"

#define TID_REQ_PRN "[%s] qpn 0x%x newreq %u opcode 0x%x psn 0x%x lpsn 0x%x " \
		    "cur_seg %u comp_seg %u ack_seg %u alloc_seg %u " \
		    "total_segs %u setup_head %u clear_tail %u flow_idx %u " \
		    "acked_tail %u state %u r_ack_psn 0x%x r_flow_psn 0x%x " \
		    "r_last_ackd 0x%x s_next_psn 0x%x"

#define RCV_ERR_PRN "[%s] qpn 0x%x s_flags 0x%x state 0x%x " \
		    "s_acked_ack_queue %u s_tail_ack_queue %u " \
		    "r_head_ack_queue %u opcode 0x%x psn 0x%x r_psn 0x%x " \
		    " diff %d"

#define TID_WRITE_RSPDR_PRN "[%s] qpn 0x%x r_tid_head %u r_tid_tail %u " \
			    "r_tid_ack %u r_tid_alloc %u alloc_w_segs %u " \
			    "pending_tid_w_segs %u sync_pt %s " \
			    "ps_nak_psn 0x%x ps_nak_state 0x%x " \
			    "prnr_nak_state 0x%x hw_flow_index %u generation "\
			    "0x%x fpsn 0x%x resync %s" \
			    "r_next_psn_kdeth 0x%x"

#define TID_WRITE_SENDER_PRN "[%s] qpn 0x%x newreq %u s_tid_cur %u " \
			     "s_tid_tail %u s_tid_head %u " \
			     "pending_tid_w_resp %u n_requests %u " \
			     "n_tid_requests %u s_flags 0x%x ps_flags 0x%x "\
			     "iow_flags 0x%lx s_state 0x%x s_retry %u"

#define KDETH_EFLAGS_ERR_PRN "[%s] qpn 0x%x  TID ERR: RcvType 0x%x " \
			     "RcvTypeError 0x%x PSN 0x%x"

DECLARE_EVENT_CLASS(/* class */
	hfi1_exp_tid_reg_unreg,
	TP_PROTO(unsigned int ctxt, u16 subctxt, u32 rarr, u32 npages,
		 unsigned long va, unsigned long pa, dma_addr_t dma),
	TP_ARGS(ctxt, subctxt, rarr, npages, va, pa, dma),
	TP_STRUCT__entry(/* entry */
		__field(unsigned int, ctxt)
		__field(u16, subctxt)
		__field(u32, rarr)
		__field(u32, npages)
		__field(unsigned long, va)
		__field(unsigned long, pa)
		__field(dma_addr_t, dma)
	),
	TP_fast_assign(/* assign */
		__entry->ctxt = ctxt;
		__entry->subctxt = subctxt;
		__entry->rarr = rarr;
		__entry->npages = npages;
		__entry->va = va;
		__entry->pa = pa;
		__entry->dma = dma;
	),
	TP_printk("[%u:%u] entry:%u, %u pages @ 0x%lx, va:0x%lx dma:0x%llx",
		  __entry->ctxt,
		  __entry->subctxt,
		  __entry->rarr,
		  __entry->npages,
		  __entry->pa,
		  __entry->va,
		  __entry->dma
	)
);

DEFINE_EVENT(/* exp_tid_unreg */
	hfi1_exp_tid_reg_unreg, hfi1_exp_tid_unreg,
	TP_PROTO(unsigned int ctxt, u16 subctxt, u32 rarr, u32 npages,
		 unsigned long va, unsigned long pa, dma_addr_t dma),
	TP_ARGS(ctxt, subctxt, rarr, npages, va, pa, dma)
);

DEFINE_EVENT(/* exp_tid_reg */
	hfi1_exp_tid_reg_unreg, hfi1_exp_tid_reg,
	TP_PROTO(unsigned int ctxt, u16 subctxt, u32 rarr, u32 npages,
		 unsigned long va, unsigned long pa, dma_addr_t dma),
	TP_ARGS(ctxt, subctxt, rarr, npages, va, pa, dma)
);

TRACE_EVENT(/* put_tid */
	hfi1_put_tid,
	TP_PROTO(struct hfi1_devdata *dd,
		 u32 index, u32 type, unsigned long pa, u16 order),
	TP_ARGS(dd, index, type, pa, order),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd)
		__field(unsigned long, pa)
		__field(u32, index)
		__field(u32, type)
		__field(u16, order)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd);
		__entry->pa = pa;
		__entry->index = index;
		__entry->type = type;
		__entry->order = order;
	),
	TP_printk("[%s] type %s pa %lx index %u order %u",
		  __get_str(dev),
		  show_tidtype(__entry->type),
		  __entry->pa,
		  __entry->index,
		  __entry->order
	)
);

TRACE_EVENT(/* exp_tid_inval */
	hfi1_exp_tid_inval,
	TP_PROTO(unsigned int ctxt, u16 subctxt, unsigned long va, u32 rarr,
		 u32 npages, dma_addr_t dma),
	TP_ARGS(ctxt, subctxt, va, rarr, npages, dma),
	TP_STRUCT__entry(/* entry */
		__field(unsigned int, ctxt)
		__field(u16, subctxt)
		__field(unsigned long, va)
		__field(u32, rarr)
		__field(u32, npages)
		__field(dma_addr_t, dma)
	),
	TP_fast_assign(/* assign */
		__entry->ctxt = ctxt;
		__entry->subctxt = subctxt;
		__entry->va = va;
		__entry->rarr = rarr;
		__entry->npages = npages;
		__entry->dma = dma;
	),
	TP_printk("[%u:%u] entry:%u, %u pages @ 0x%lx dma: 0x%llx",
		  __entry->ctxt,
		  __entry->subctxt,
		  __entry->rarr,
		  __entry->npages,
		  __entry->va,
		  __entry->dma
	)
);

DECLARE_EVENT_CLASS(/* opfn_state */
	hfi1_opfn_state_template,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u16, requested)
		__field(u16, completed)
		__field(u8, curr)
	),
	TP_fast_assign(/* assign */
		struct hfi1_qp_priv *priv = qp->priv;

		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->requested = priv->opfn.requested;
		__entry->completed = priv->opfn.completed;
		__entry->curr = priv->opfn.curr;
	),
	TP_printk(/* print */
		"[%s] qpn 0x%x requested 0x%x completed 0x%x curr 0x%x",
		__get_str(dev),
		__entry->qpn,
		__entry->requested,
		__entry->completed,
		__entry->curr
	)
);

DEFINE_EVENT(/* event */
	hfi1_opfn_state_template, hfi1_opfn_state_conn_request,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_opfn_state_template, hfi1_opfn_state_sched_conn_request,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_opfn_state_template, hfi1_opfn_state_conn_response,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_opfn_state_template, hfi1_opfn_state_conn_reply,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_opfn_state_template, hfi1_opfn_state_conn_error,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DECLARE_EVENT_CLASS(/* opfn_data */
	hfi1_opfn_data_template,
	TP_PROTO(struct rvt_qp *qp, u8 capcode, u64 data),
	TP_ARGS(qp, capcode, data),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, state)
		__field(u8, capcode)
		__field(u64, data)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->state = qp->state;
		__entry->capcode = capcode;
		__entry->data = data;
	),
	TP_printk(/* printk */
		"[%s] qpn 0x%x (state 0x%x) Capcode %u data 0x%llx",
		__get_str(dev),
		__entry->qpn,
		__entry->state,
		__entry->capcode,
		__entry->data
	)
);

DEFINE_EVENT(/* event */
	hfi1_opfn_data_template, hfi1_opfn_data_conn_request,
	TP_PROTO(struct rvt_qp *qp, u8 capcode, u64 data),
	TP_ARGS(qp, capcode, data)
);

DEFINE_EVENT(/* event */
	hfi1_opfn_data_template, hfi1_opfn_data_conn_response,
	TP_PROTO(struct rvt_qp *qp, u8 capcode, u64 data),
	TP_ARGS(qp, capcode, data)
);

DEFINE_EVENT(/* event */
	hfi1_opfn_data_template, hfi1_opfn_data_conn_reply,
	TP_PROTO(struct rvt_qp *qp, u8 capcode, u64 data),
	TP_ARGS(qp, capcode, data)
);

DECLARE_EVENT_CLASS(/* opfn_param */
	hfi1_opfn_param_template,
	TP_PROTO(struct rvt_qp *qp, char remote,
		 struct tid_rdma_params *param),
	TP_ARGS(qp, remote, param),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(char, remote)
		__field(u32, param_qp)
		__field(u32, max_len)
		__field(u16, jkey)
		__field(u8, max_read)
		__field(u8, max_write)
		__field(u8, timeout)
		__field(u8, urg)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->remote = remote;
		__entry->param_qp = param->qp;
		__entry->max_len = param->max_len;
		__entry->jkey = param->jkey;
		__entry->max_read = param->max_read;
		__entry->max_write = param->max_write;
		__entry->timeout = param->timeout;
		__entry->urg = param->urg;
	),
	TP_printk(/* print */
		OPFN_PARAM_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->remote ? "remote" : "local",
		__entry->param_qp,
		__entry->max_read,
		__entry->max_write,
		__entry->max_len,
		__entry->jkey,
		__entry->timeout,
		__entry->urg
	)
);

DEFINE_EVENT(/* event */
	hfi1_opfn_param_template, hfi1_opfn_param,
	TP_PROTO(struct rvt_qp *qp, char remote,
		 struct tid_rdma_params *param),
	TP_ARGS(qp, remote, param)
);

DECLARE_EVENT_CLASS(/* msg */
	hfi1_msg_template,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u64 more),
	TP_ARGS(qp, msg, more),
	TP_STRUCT__entry(/* entry */
		__field(u32, qpn)
		__string(msg, msg)
		__field(u64, more)
	),
	TP_fast_assign(/* assign */
		__entry->qpn = qp ? qp->ibqp.qp_num : 0;
		__assign_str(msg, msg);
		__entry->more = more;
	),
	TP_printk(/* print */
		"qpn 0x%x %s 0x%llx",
		__entry->qpn,
		__get_str(msg),
		__entry->more
	)
);

DEFINE_EVENT(/* event */
	hfi1_msg_template, hfi1_msg_opfn_conn_request,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u64 more),
	TP_ARGS(qp, msg, more)
);

DEFINE_EVENT(/* event */
	hfi1_msg_template, hfi1_msg_opfn_conn_error,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u64 more),
	TP_ARGS(qp, msg, more)
);

DEFINE_EVENT(/* event */
	hfi1_msg_template, hfi1_msg_alloc_tids,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u64 more),
	TP_ARGS(qp, msg, more)
);

DEFINE_EVENT(/* event */
	hfi1_msg_template, hfi1_msg_tid_restart_req,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u64 more),
	TP_ARGS(qp, msg, more)
);

DEFINE_EVENT(/* event */
	hfi1_msg_template, hfi1_msg_handle_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u64 more),
	TP_ARGS(qp, msg, more)
);

DEFINE_EVENT(/* event */
	hfi1_msg_template, hfi1_msg_tid_timeout,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u64 more),
	TP_ARGS(qp, msg, more)
);

DEFINE_EVENT(/* event */
	hfi1_msg_template, hfi1_msg_tid_retry_timeout,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u64 more),
	TP_ARGS(qp, msg, more)
);

DECLARE_EVENT_CLASS(/* tid_flow_page */
	hfi1_tid_flow_page_template,
	TP_PROTO(struct rvt_qp *qp, struct tid_rdma_flow *flow, u32 index,
		 char mtu8k, char v1, void *vaddr),
	TP_ARGS(qp, flow, index, mtu8k, v1, vaddr),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(char, mtu8k)
		__field(char, v1)
		__field(u32, index)
		__field(u64, page)
		__field(u64, vaddr)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->mtu8k = mtu8k;
		__entry->v1 = v1;
		__entry->index = index;
		__entry->page = vaddr ? (u64)virt_to_page(vaddr) : 0ULL;
		__entry->vaddr = (u64)vaddr;
	),
	TP_printk(/* print */
		"[%s] qpn 0x%x page[%u]: page 0x%llx %s 0x%llx",
		__get_str(dev),
		__entry->qpn,
		__entry->index,
		__entry->page,
		__entry->mtu8k ? (__entry->v1 ? "v1" : "v0") : "vaddr",
		__entry->vaddr
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_page_template, hfi1_tid_flow_page,
	TP_PROTO(struct rvt_qp *qp, struct tid_rdma_flow *flow, u32 index,
		 char mtu8k, char v1, void *vaddr),
	TP_ARGS(qp, flow, index, mtu8k, v1, vaddr)
);

DECLARE_EVENT_CLASS(/* tid_pageset */
	hfi1_tid_pageset_template,
	TP_PROTO(struct rvt_qp *qp, u32 index, u16 idx, u16 count),
	TP_ARGS(qp, index, idx, count),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, index)
		__field(u16, idx)
		__field(u16, count)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->index = index;
		__entry->idx = idx;
		__entry->count = count;
	),
	TP_printk(/* print */
		"[%s] qpn 0x%x list[%u]: idx %u count %u",
		__get_str(dev),
		__entry->qpn,
		__entry->index,
		__entry->idx,
		__entry->count
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_pageset_template, hfi1_tid_pageset,
	TP_PROTO(struct rvt_qp *qp, u32 index, u16 idx, u16 count),
	TP_ARGS(qp, index, idx, count)
);

DECLARE_EVENT_CLASS(/* tid_fow */
	hfi1_tid_flow_template,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(int, index)
		__field(int, idx)
		__field(u32, resp_ib_psn)
		__field(u32, generation)
		__field(u32, fspsn)
		__field(u32, flpsn)
		__field(u32, r_next_psn)
		__field(u32, ib_spsn)
		__field(u32, ib_lpsn)
		__field(u32, npagesets)
		__field(u32, tnode_cnt)
		__field(u32, tidcnt)
		__field(u32, tid_idx)
		__field(u32, tid_offset)
		__field(u32, length)
		__field(u32, sent)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->index = index;
		__entry->idx = flow->idx;
		__entry->resp_ib_psn = flow->flow_state.resp_ib_psn;
		__entry->generation = flow->flow_state.generation;
		__entry->fspsn = full_flow_psn(flow,
					       flow->flow_state.spsn);
		__entry->flpsn = full_flow_psn(flow,
					       flow->flow_state.lpsn);
		__entry->r_next_psn = flow->flow_state.r_next_psn;
		__entry->ib_spsn = flow->flow_state.ib_spsn;
		__entry->ib_lpsn = flow->flow_state.ib_lpsn;
		__entry->npagesets = flow->npagesets;
		__entry->tnode_cnt = flow->tnode_cnt;
		__entry->tidcnt = flow->tidcnt;
		__entry->tid_idx = flow->tid_idx;
		__entry->tid_offset =  flow->tid_offset;
		__entry->length = flow->length;
		__entry->sent = flow->sent;
	),
	TP_printk(/* print */
		TID_FLOW_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->index,
		__entry->idx,
		__entry->resp_ib_psn,
		__entry->generation,
		__entry->fspsn,
		__entry->flpsn,
		__entry->r_next_psn,
		__entry->ib_spsn,
		__entry->ib_lpsn,
		__entry->npagesets,
		__entry->tnode_cnt,
		__entry->tidcnt,
		__entry->tid_idx,
		__entry->tid_offset,
		__entry->length,
		__entry->sent
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_alloc,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_build_read_pkt,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_build_read_resp,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_rcv_read_req,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_rcv_read_resp,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_restart_req,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_build_write_resp,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_rcv_write_resp,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_build_write_data,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_rcv_tid_ack,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_rcv_resync,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_handle_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_read_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp, int index, struct tid_rdma_flow *flow),
	TP_ARGS(qp, index, flow)
);

DECLARE_EVENT_CLASS(/* tid_node */
	hfi1_tid_node_template,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u32 index, u32 base,
		 u8 map, u8 used, u8 cnt),
	TP_ARGS(qp, msg, index, base, map, used, cnt),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__string(msg, msg)
		__field(u32, index)
		__field(u32, base)
		__field(u8, map)
		__field(u8, used)
		__field(u8, cnt)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__assign_str(msg, msg);
		__entry->index = index;
		__entry->base = base;
		__entry->map = map;
		__entry->used = used;
		__entry->cnt = cnt;
	),
	TP_printk(/* print */
		TID_NODE_PRN,
		__get_str(dev),
		__entry->qpn,
		__get_str(msg),
		__entry->index,
		__entry->base,
		__entry->map,
		__entry->used,
		__entry->cnt
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_node_template, hfi1_tid_node_add,
	TP_PROTO(struct rvt_qp *qp, const char *msg, u32 index, u32 base,
		 u8 map, u8 used, u8 cnt),
	TP_ARGS(qp, msg, index, base, map, used, cnt)
);

DECLARE_EVENT_CLASS(/* tid_entry */
	hfi1_tid_entry_template,
	TP_PROTO(struct rvt_qp *qp, int index, u32 ent),
	TP_ARGS(qp, index, ent),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(int, index)
		__field(u8, ctrl)
		__field(u16, idx)
		__field(u16, len)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->index = index;
		__entry->ctrl = hfi1_trace_get_tid_ctrl(ent);
		__entry->idx = hfi1_trace_get_tid_idx(ent);
		__entry->len = hfi1_trace_get_tid_len(ent);
	),
	TP_printk(/* print */
		"[%s] qpn 0x%x TID entry %d: idx %u len %u ctrl 0x%x",
		__get_str(dev),
		__entry->qpn,
		__entry->index,
		__entry->idx,
		__entry->len,
		__entry->ctrl
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_entry_template, hfi1_tid_entry_alloc,
	TP_PROTO(struct rvt_qp *qp, int index, u32 entry),
	TP_ARGS(qp, index, entry)
);

DEFINE_EVENT(/* event */
	hfi1_tid_entry_template, hfi1_tid_entry_build_read_resp,
	TP_PROTO(struct rvt_qp *qp, int index, u32 ent),
	TP_ARGS(qp, index, ent)
);

DEFINE_EVENT(/* event */
	hfi1_tid_entry_template, hfi1_tid_entry_rcv_read_req,
	TP_PROTO(struct rvt_qp *qp, int index, u32 ent),
	TP_ARGS(qp, index, ent)
);

DEFINE_EVENT(/* event */
	hfi1_tid_entry_template, hfi1_tid_entry_rcv_write_resp,
	TP_PROTO(struct rvt_qp *qp, int index, u32 entry),
	TP_ARGS(qp, index, entry)
);

DEFINE_EVENT(/* event */
	hfi1_tid_entry_template, hfi1_tid_entry_build_write_data,
	TP_PROTO(struct rvt_qp *qp, int index, u32 entry),
	TP_ARGS(qp, index, entry)
);

DECLARE_EVENT_CLASS(/* rsp_info */
	hfi1_responder_info_template,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u8, state)
		__field(u8, s_state)
		__field(u32, psn)
		__field(u32, r_psn)
		__field(u8, r_state)
		__field(u8, r_flags)
		__field(u8, r_head_ack_queue)
		__field(u8, s_tail_ack_queue)
		__field(u8, s_acked_ack_queue)
		__field(u8, s_ack_state)
		__field(u8, s_nak_state)
		__field(u8, r_nak_state)
		__field(u32, s_flags)
		__field(u32, ps_flags)
		__field(unsigned long, iow_flags)
	),
	TP_fast_assign(/* assign */
		struct hfi1_qp_priv *priv = qp->priv;

		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->state = qp->state;
		__entry->s_state = qp->s_state;
		__entry->psn = psn;
		__entry->r_psn = qp->r_psn;
		__entry->r_state = qp->r_state;
		__entry->r_flags = qp->r_flags;
		__entry->r_head_ack_queue = qp->r_head_ack_queue;
		__entry->s_tail_ack_queue = qp->s_tail_ack_queue;
		__entry->s_acked_ack_queue = qp->s_acked_ack_queue;
		__entry->s_ack_state = qp->s_ack_state;
		__entry->s_nak_state = qp->s_nak_state;
		__entry->s_flags = qp->s_flags;
		__entry->ps_flags = priv->s_flags;
		__entry->iow_flags = priv->s_iowait.flags;
	),
	TP_printk(/* print */
		RSP_INFO_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->state,
		__entry->s_state,
		__entry->psn,
		__entry->r_psn,
		__entry->r_state,
		__entry->r_flags,
		__entry->r_head_ack_queue,
		__entry->s_tail_ack_queue,
		__entry->s_acked_ack_queue,
		__entry->s_ack_state,
		__entry->s_nak_state,
		__entry->s_flags,
		__entry->ps_flags,
		__entry->iow_flags
	)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_make_rc_ack,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_rcv_tid_read_req,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_tid_rcv_error,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_tid_write_alloc_res,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_rcv_tid_write_req,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_build_tid_write_resp,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_rcv_tid_write_data,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_make_tid_ack,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_handle_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DEFINE_EVENT(/* event */
	hfi1_responder_info_template, hfi1_rsp_read_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp, u32 psn),
	TP_ARGS(qp, psn)
);

DECLARE_EVENT_CLASS(/* sender_info */
	hfi1_sender_info_template,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u8, state)
		__field(u32, s_cur)
		__field(u32, s_tail)
		__field(u32, s_head)
		__field(u32, s_acked)
		__field(u32, s_last)
		__field(u32, s_psn)
		__field(u32, s_last_psn)
		__field(u32, s_flags)
		__field(u32, ps_flags)
		__field(unsigned long, iow_flags)
		__field(u8, s_state)
		__field(u8, s_num_rd)
		__field(u8, s_retry)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device))
		__entry->qpn = qp->ibqp.qp_num;
		__entry->state = qp->state;
		__entry->s_cur = qp->s_cur;
		__entry->s_tail = qp->s_tail;
		__entry->s_head = qp->s_head;
		__entry->s_acked = qp->s_acked;
		__entry->s_last = qp->s_last;
		__entry->s_psn = qp->s_psn;
		__entry->s_last_psn = qp->s_last_psn;
		__entry->s_flags = qp->s_flags;
		__entry->ps_flags = ((struct hfi1_qp_priv *)qp->priv)->s_flags;
		__entry->iow_flags =
			((struct hfi1_qp_priv *)qp->priv)->s_iowait.flags;
		__entry->s_state = qp->s_state;
		__entry->s_num_rd = qp->s_num_rd_atomic;
		__entry->s_retry = qp->s_retry;
	),
	TP_printk(/* print */
		SENDER_INFO_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->state,
		__entry->s_cur,
		__entry->s_tail,
		__entry->s_head,
		__entry->s_acked,
		__entry->s_last,
		__entry->s_psn,
		__entry->s_last_psn,
		__entry->s_flags,
		__entry->ps_flags,
		__entry->iow_flags,
		__entry->s_state,
		__entry->s_num_rd,
		__entry->s_retry
	)
);

DEFINE_EVENT(/* event */
	hfi1_sender_info_template, hfi1_sender_make_rc_req,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_sender_info_template, hfi1_sender_reset_psn,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_sender_info_template, hfi1_sender_restart_rc,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_sender_info_template, hfi1_sender_do_rc_ack,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_sender_info_template, hfi1_sender_rcv_tid_read_resp,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_sender_info_template, hfi1_sender_rcv_tid_ack,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_sender_info_template, hfi1_sender_make_tid_pkt,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_sender_info_template, hfi1_sender_read_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DECLARE_EVENT_CLASS(/* tid_read_sender */
	hfi1_tid_read_sender_template,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(char, newreq)
		__field(u32, tid_r_reqs)
		__field(u32, tid_r_comp)
		__field(u32, pending_tid_r_segs)
		__field(u32, s_flags)
		__field(u32, ps_flags)
		__field(unsigned long, iow_flags)
		__field(u8, s_state)
		__field(u32, hw_flow_index)
		__field(u32, generation)
		__field(u32, fpsn)
	),
	TP_fast_assign(/* assign */
		struct hfi1_qp_priv *priv = qp->priv;

		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->newreq = newreq;
		__entry->tid_r_reqs = priv->tid_r_reqs;
		__entry->tid_r_comp = priv->tid_r_comp;
		__entry->pending_tid_r_segs = priv->pending_tid_r_segs;
		__entry->s_flags = qp->s_flags;
		__entry->ps_flags = priv->s_flags;
		__entry->iow_flags = priv->s_iowait.flags;
		__entry->s_state = priv->s_state;
		__entry->hw_flow_index = priv->flow_state.index;
		__entry->generation = priv->flow_state.generation;
		__entry->fpsn = priv->flow_state.psn;
	),
	TP_printk(/* print */
		TID_READ_SENDER_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->newreq,
		__entry->tid_r_reqs,
		__entry->tid_r_comp,
		__entry->pending_tid_r_segs,
		__entry->s_flags,
		__entry->ps_flags,
		__entry->iow_flags,
		__entry->s_state,
		__entry->hw_flow_index,
		__entry->generation,
		__entry->fpsn
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_read_sender_template, hfi1_tid_read_sender_make_req,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq)
);

DEFINE_EVENT(/* event */
	hfi1_tid_read_sender_template, hfi1_tid_read_sender_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq)
);

DECLARE_EVENT_CLASS(/* tid_rdma_request */
	hfi1_tid_rdma_request_template,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(char, newreq)
		__field(u8, opcode)
		__field(u32, psn)
		__field(u32, lpsn)
		__field(u32, cur_seg)
		__field(u32, comp_seg)
		__field(u32, ack_seg)
		__field(u32, alloc_seg)
		__field(u32, total_segs)
		__field(u16, setup_head)
		__field(u16, clear_tail)
		__field(u16, flow_idx)
		__field(u16, acked_tail)
		__field(u32, state)
		__field(u32, r_ack_psn)
		__field(u32, r_flow_psn)
		__field(u32, r_last_acked)
		__field(u32, s_next_psn)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->newreq = newreq;
		__entry->opcode = opcode;
		__entry->psn = psn;
		__entry->lpsn = lpsn;
		__entry->cur_seg = req->cur_seg;
		__entry->comp_seg = req->comp_seg;
		__entry->ack_seg = req->ack_seg;
		__entry->alloc_seg = req->alloc_seg;
		__entry->total_segs = req->total_segs;
		__entry->setup_head = req->setup_head;
		__entry->clear_tail = req->clear_tail;
		__entry->flow_idx = req->flow_idx;
		__entry->acked_tail = req->acked_tail;
		__entry->state = req->state;
		__entry->r_ack_psn = req->r_ack_psn;
		__entry->r_flow_psn = req->r_flow_psn;
		__entry->r_last_acked = req->r_last_acked;
		__entry->s_next_psn = req->s_next_psn;
	),
	TP_printk(/* print */
		TID_REQ_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->newreq,
		__entry->opcode,
		__entry->psn,
		__entry->lpsn,
		__entry->cur_seg,
		__entry->comp_seg,
		__entry->ack_seg,
		__entry->alloc_seg,
		__entry->total_segs,
		__entry->setup_head,
		__entry->clear_tail,
		__entry->flow_idx,
		__entry->acked_tail,
		__entry->state,
		__entry->r_ack_psn,
		__entry->r_flow_psn,
		__entry->r_last_acked,
		__entry->s_next_psn
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_make_req_read,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_build_read_req,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_rcv_read_req,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_rcv_read_resp,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_rcv_err,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_restart_req,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_setup_tid_wqe,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_write_alloc_res,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_rcv_write_req,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_build_write_resp,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_rcv_write_resp,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_rcv_write_data,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_rcv_tid_ack,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_tid_retry_timeout,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_rcv_resync,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_make_tid_pkt,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_make_tid_ack,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_handle_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_read_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_make_rc_ack_write,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_make_req_write,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DEFINE_EVENT(/* event */
	hfi1_tid_rdma_request_template, hfi1_tid_req_update_num_rd_atomic,
	TP_PROTO(struct rvt_qp *qp, char newreq, u8 opcode, u32 psn, u32 lpsn,
		 struct tid_rdma_request *req),
	TP_ARGS(qp, newreq, opcode, psn, lpsn, req)
);

DECLARE_EVENT_CLASS(/* rc_rcv_err */
	hfi1_rc_rcv_err_template,
	TP_PROTO(struct rvt_qp *qp, u32 opcode, u32 psn, int diff),
	TP_ARGS(qp, opcode, psn, diff),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, s_flags)
		__field(u8, state)
		__field(u8, s_acked_ack_queue)
		__field(u8, s_tail_ack_queue)
		__field(u8, r_head_ack_queue)
		__field(u32, opcode)
		__field(u32, psn)
		__field(u32, r_psn)
		__field(int, diff)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device))
		__entry->qpn = qp->ibqp.qp_num;
		__entry->s_flags = qp->s_flags;
		__entry->state = qp->state;
		__entry->s_acked_ack_queue = qp->s_acked_ack_queue;
		__entry->s_tail_ack_queue = qp->s_tail_ack_queue;
		__entry->r_head_ack_queue = qp->r_head_ack_queue;
		__entry->opcode = opcode;
		__entry->psn = psn;
		__entry->r_psn = qp->r_psn;
		__entry->diff = diff;
	),
	TP_printk(/* print */
		RCV_ERR_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->s_flags,
		__entry->state,
		__entry->s_acked_ack_queue,
		__entry->s_tail_ack_queue,
		__entry->r_head_ack_queue,
		__entry->opcode,
		__entry->psn,
		__entry->r_psn,
		__entry->diff
	)
);

DEFINE_EVENT(/* event */
	hfi1_rc_rcv_err_template, hfi1_tid_rdma_rcv_err,
	TP_PROTO(struct rvt_qp *qp, u32 opcode, u32 psn, int diff),
	TP_ARGS(qp, opcode, psn, diff)
);

DECLARE_EVENT_CLASS(/* sge  */
	hfi1_sge_template,
	TP_PROTO(struct rvt_qp *qp, int index, struct rvt_sge *sge),
	TP_ARGS(qp, index, sge),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(int, index)
		__field(u64, vaddr)
		__field(u32, sge_length)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->index = index;
		__entry->vaddr = (u64)sge->vaddr;
		__entry->sge_length = sge->sge_length;
	),
	TP_printk(/* print */
		"[%s] qpn 0x%x sge %d: vaddr 0x%llx sge_length %u",
		__get_str(dev),
		__entry->qpn,
		__entry->index,
		__entry->vaddr,
		__entry->sge_length
	)
);

DEFINE_EVENT(/* event */
	hfi1_sge_template, hfi1_sge_check_align,
	TP_PROTO(struct rvt_qp *qp, int index, struct rvt_sge *sge),
	TP_ARGS(qp, index, sge)
);

DECLARE_EVENT_CLASS(/* tid_write_sp */
	hfi1_tid_write_rsp_template,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, r_tid_head)
		__field(u32, r_tid_tail)
		__field(u32, r_tid_ack)
		__field(u32, r_tid_alloc)
		__field(u32, alloc_w_segs)
		__field(u32, pending_tid_w_segs)
		__field(bool, sync_pt)
		__field(u32, ps_nak_psn)
		__field(u8, ps_nak_state)
		__field(u8, prnr_nak_state)
		__field(u32, hw_flow_index)
		__field(u32, generation)
		__field(u32, fpsn)
		__field(bool, resync)
		__field(u32, r_next_psn_kdeth)
	),
	TP_fast_assign(/* assign */
		struct hfi1_qp_priv *priv = qp->priv;

		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->r_tid_head = priv->r_tid_head;
		__entry->r_tid_tail = priv->r_tid_tail;
		__entry->r_tid_ack = priv->r_tid_ack;
		__entry->r_tid_alloc = priv->r_tid_alloc;
		__entry->alloc_w_segs = priv->alloc_w_segs;
		__entry->pending_tid_w_segs = priv->pending_tid_w_segs;
		__entry->sync_pt = priv->sync_pt;
		__entry->ps_nak_psn = priv->s_nak_psn;
		__entry->ps_nak_state = priv->s_nak_state;
		__entry->prnr_nak_state = priv->rnr_nak_state;
		__entry->hw_flow_index = priv->flow_state.index;
		__entry->generation = priv->flow_state.generation;
		__entry->fpsn = priv->flow_state.psn;
		__entry->resync = priv->resync;
		__entry->r_next_psn_kdeth = priv->r_next_psn_kdeth;
	),
	TP_printk(/* print */
		TID_WRITE_RSPDR_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->r_tid_head,
		__entry->r_tid_tail,
		__entry->r_tid_ack,
		__entry->r_tid_alloc,
		__entry->alloc_w_segs,
		__entry->pending_tid_w_segs,
		__entry->sync_pt ? "yes" : "no",
		__entry->ps_nak_psn,
		__entry->ps_nak_state,
		__entry->prnr_nak_state,
		__entry->hw_flow_index,
		__entry->generation,
		__entry->fpsn,
		__entry->resync ? "yes" : "no",
		__entry->r_next_psn_kdeth
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_rsp_template, hfi1_tid_write_rsp_alloc_res,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_rsp_template, hfi1_tid_write_rsp_rcv_req,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_rsp_template, hfi1_tid_write_rsp_build_resp,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_rsp_template, hfi1_tid_write_rsp_rcv_data,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_rsp_template, hfi1_tid_write_rsp_rcv_resync,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_rsp_template, hfi1_tid_write_rsp_make_tid_ack,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_rsp_template, hfi1_tid_write_rsp_handle_kdeth_eflags,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_rsp_template, hfi1_tid_write_rsp_make_rc_ack,
	TP_PROTO(struct rvt_qp *qp),
	TP_ARGS(qp)
);

DECLARE_EVENT_CLASS(/* tid_write_sender */
	hfi1_tid_write_sender_template,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(char, newreq)
		__field(u32, s_tid_cur)
		__field(u32, s_tid_tail)
		__field(u32, s_tid_head)
		__field(u32, pending_tid_w_resp)
		__field(u32, n_requests)
		__field(u32, n_tid_requests)
		__field(u32, s_flags)
		__field(u32, ps_flags)
		__field(unsigned long, iow_flags)
		__field(u8, s_state)
		__field(u8, s_retry)
	),
	TP_fast_assign(/* assign */
		struct hfi1_qp_priv *priv = qp->priv;

		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->newreq = newreq;
		__entry->s_tid_cur = priv->s_tid_cur;
		__entry->s_tid_tail = priv->s_tid_tail;
		__entry->s_tid_head = priv->s_tid_head;
		__entry->pending_tid_w_resp = priv->pending_tid_w_resp;
		__entry->n_requests = atomic_read(&priv->n_requests);
		__entry->n_tid_requests = atomic_read(&priv->n_tid_requests);
		__entry->s_flags = qp->s_flags;
		__entry->ps_flags = priv->s_flags;
		__entry->iow_flags = priv->s_iowait.flags;
		__entry->s_state = priv->s_state;
		__entry->s_retry = priv->s_retry;
	),
	TP_printk(/* print */
		TID_WRITE_SENDER_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->newreq,
		__entry->s_tid_cur,
		__entry->s_tid_tail,
		__entry->s_tid_head,
		__entry->pending_tid_w_resp,
		__entry->n_requests,
		__entry->n_tid_requests,
		__entry->s_flags,
		__entry->ps_flags,
		__entry->iow_flags,
		__entry->s_state,
		__entry->s_retry
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_sender_template, hfi1_tid_write_sender_rcv_resp,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_sender_template, hfi1_tid_write_sender_rcv_tid_ack,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_sender_template, hfi1_tid_write_sender_retry_timeout,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_sender_template, hfi1_tid_write_sender_make_tid_pkt,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_sender_template, hfi1_tid_write_sender_make_req,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq)
);

DEFINE_EVENT(/* event */
	hfi1_tid_write_sender_template, hfi1_tid_write_sender_restart_rc,
	TP_PROTO(struct rvt_qp *qp, char newreq),
	TP_ARGS(qp, newreq)
);

DECLARE_EVENT_CLASS(/* tid_ack */
	hfi1_tid_ack_template,
	TP_PROTO(struct rvt_qp *qp, u32 aeth, u32 psn,
		 u32 req_psn, u32 resync_psn),
	TP_ARGS(qp, aeth, psn, req_psn, resync_psn),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, aeth)
		__field(u32, psn)
		__field(u32, req_psn)
		__field(u32, resync_psn)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device))
		__entry->qpn = qp->ibqp.qp_num;
		__entry->aeth = aeth;
		__entry->psn = psn;
		__entry->req_psn = req_psn;
		__entry->resync_psn = resync_psn;
		),
	TP_printk(/* print */
		"[%s] qpn 0x%x aeth 0x%x psn 0x%x req_psn 0x%x resync_psn 0x%x",
		__get_str(dev),
		__entry->qpn,
		__entry->aeth,
		__entry->psn,
		__entry->req_psn,
		__entry->resync_psn
	)
);

DEFINE_EVENT(/* rcv_tid_ack */
	hfi1_tid_ack_template, hfi1_rcv_tid_ack,
	TP_PROTO(struct rvt_qp *qp, u32 aeth, u32 psn,
		 u32 req_psn, u32 resync_psn),
	TP_ARGS(qp, aeth, psn, req_psn, resync_psn)
);

DECLARE_EVENT_CLASS(/* kdeth_eflags_error */
	hfi1_kdeth_eflags_error_template,
	TP_PROTO(struct rvt_qp *qp, u8 rcv_type, u8 rte, u32 psn),
	TP_ARGS(qp, rcv_type, rte, psn),
	TP_STRUCT__entry(/* entry */
		DD_DEV_ENTRY(dd_from_ibdev(qp->ibqp.device))
		__field(u32, qpn)
		__field(u8, rcv_type)
		__field(u8, rte)
		__field(u32, psn)
	),
	TP_fast_assign(/* assign */
		DD_DEV_ASSIGN(dd_from_ibdev(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->rcv_type = rcv_type;
		__entry->rte = rte;
		__entry->psn = psn;
	),
	TP_printk(/* print */
		KDETH_EFLAGS_ERR_PRN,
		__get_str(dev),
		__entry->qpn,
		__entry->rcv_type,
		__entry->rte,
		__entry->psn
	)
);

DEFINE_EVENT(/* event */
	hfi1_kdeth_eflags_error_template, hfi1_eflags_err_write,
	TP_PROTO(struct rvt_qp *qp, u8 rcv_type, u8 rte, u32 psn),
	TP_ARGS(qp, rcv_type, rte, psn)
);

#endif /* __HFI1_TRACE_TID_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_tid
#include <trace/define_trace.h>
