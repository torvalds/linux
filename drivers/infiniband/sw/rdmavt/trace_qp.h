/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
 */
#if !defined(__RVT_TRACE_QP_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RVT_TRACE_QP_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdmavt_qp.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rvt_qp

DECLARE_EVENT_CLASS(rvt_qphash_template,
	TP_PROTO(struct rvt_qp *qp, u32 bucket),
	TP_ARGS(qp, bucket),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(ib_to_rvt(qp->ibqp.device))
		__field(u32, qpn)
		__field(u32, bucket)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(ib_to_rvt(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->bucket = bucket;
	),
	TP_printk(
		"[%s] qpn 0x%x bucket %u",
		__get_str(dev),
		__entry->qpn,
		__entry->bucket
	)
);

DEFINE_EVENT(rvt_qphash_template, rvt_qpinsert,
	TP_PROTO(struct rvt_qp *qp, u32 bucket),
	TP_ARGS(qp, bucket));

DEFINE_EVENT(rvt_qphash_template, rvt_qpremove,
	TP_PROTO(struct rvt_qp *qp, u32 bucket),
	TP_ARGS(qp, bucket));

DECLARE_EVENT_CLASS(
	rvt_rnrnak_template,
	TP_PROTO(struct rvt_qp *qp, u32 to),
	TP_ARGS(qp, to),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(ib_to_rvt(qp->ibqp.device))
		__field(u32, qpn)
		__field(void *, hrtimer)
		__field(u32, s_flags)
		__field(u32, to)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(ib_to_rvt(qp->ibqp.device));
		__entry->qpn = qp->ibqp.qp_num;
		__entry->hrtimer = &qp->s_rnr_timer;
		__entry->s_flags = qp->s_flags;
		__entry->to = to;
	),
	TP_printk(
		"[%s] qpn 0x%x hrtimer 0x%p s_flags 0x%x timeout %u us",
		__get_str(dev),
		__entry->qpn,
		__entry->hrtimer,
		__entry->s_flags,
		__entry->to
	)
);

DEFINE_EVENT(
	rvt_rnrnak_template, rvt_rnrnak_add,
	TP_PROTO(struct rvt_qp *qp, u32 to),
	TP_ARGS(qp, to));

DEFINE_EVENT(
	rvt_rnrnak_template, rvt_rnrnak_timeout,
	TP_PROTO(struct rvt_qp *qp, u32 to),
	TP_ARGS(qp, to));

DEFINE_EVENT(
	rvt_rnrnak_template, rvt_rnrnak_stop,
	TP_PROTO(struct rvt_qp *qp, u32 to),
	TP_ARGS(qp, to));

#endif /* __RVT_TRACE_QP_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_qp
#include <trace/define_trace.h>

