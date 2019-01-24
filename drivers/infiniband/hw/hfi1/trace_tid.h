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
		     "npagesets %u tnode_cnt %u tidcnt %u length %u"

#define TID_NODE_PRN "[%s] qpn 0x%x  %s idx %u grp base 0x%x map 0x%x " \
		     "used %u cnt %u"

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
		__field(unsigned long, pa);
		__field(u32, index);
		__field(u32, type);
		__field(u16, order);
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
		__field(u32, npagesets)
		__field(u32, tnode_cnt)
		__field(u32, tidcnt)
		__field(u32, length)
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
		__entry->npagesets = flow->npagesets;
		__entry->tnode_cnt = flow->tnode_cnt;
		__entry->tidcnt = flow->tidcnt;
		__entry->length = flow->length;
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
		__entry->npagesets,
		__entry->tnode_cnt,
		__entry->tidcnt,
		__entry->length
	)
);

DEFINE_EVENT(/* event */
	hfi1_tid_flow_template, hfi1_tid_flow_alloc,
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

#endif /* __HFI1_TRACE_TID_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_tid
#include <trace/define_trace.h>
