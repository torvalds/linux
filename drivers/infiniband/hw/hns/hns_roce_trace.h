/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 Hisilicon Limited.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hns_roce

#if !defined(__HNS_ROCE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HNS_ROCE_TRACE_H

#include <linux/tracepoint.h>
#include "hns_roce_device.h"

DECLARE_EVENT_CLASS(flush_head_template,
		    TP_PROTO(unsigned long qpn, u32 pi,
			     enum hns_roce_trace_type type),
		    TP_ARGS(qpn, pi, type),

		    TP_STRUCT__entry(__field(unsigned long, qpn)
				     __field(u32, pi)
				     __field(enum hns_roce_trace_type, type)
		    ),

		    TP_fast_assign(__entry->qpn = qpn;
				   __entry->pi = pi;
				   __entry->type = type;
		    ),

		    TP_printk("%s 0x%lx flush head 0x%x.",
			      trace_type_to_str(__entry->type),
			      __entry->qpn, __entry->pi)
);

DEFINE_EVENT(flush_head_template, hns_sq_flush_cqe,
	     TP_PROTO(unsigned long qpn, u32 pi,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, pi, type));
DEFINE_EVENT(flush_head_template, hns_rq_flush_cqe,
	     TP_PROTO(unsigned long qpn, u32 pi,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, pi, type));

#define MAX_SGE_PER_WQE 64
#define MAX_WQE_SIZE (MAX_SGE_PER_WQE * HNS_ROCE_SGE_SIZE)
DECLARE_EVENT_CLASS(wqe_template,
		    TP_PROTO(unsigned long qpn, u32 idx, void *wqe, u32 len,
			     u64 id, enum hns_roce_trace_type type),
		    TP_ARGS(qpn, idx, wqe, len, id, type),

		    TP_STRUCT__entry(__field(unsigned long, qpn)
				     __field(u32, idx)
				     __array(__le32, wqe,
					     MAX_WQE_SIZE / sizeof(__le32))
				     __field(u32, len)
				     __field(u64, id)
				     __field(enum hns_roce_trace_type, type)
				     ),

		    TP_fast_assign(__entry->qpn = qpn;
				   __entry->idx = idx;
				   __entry->id = id;
				   memcpy(__entry->wqe, wqe, len);
				   __entry->len = len / sizeof(__le32);
				   __entry->type = type;
				   ),

		    TP_printk("%s 0x%lx wqe(0x%x/0x%llx): %s",
			      trace_type_to_str(__entry->type),
			      __entry->qpn, __entry->idx, __entry->id,
			      __print_array(__entry->wqe, __entry->len,
					    sizeof(__le32)))
);

DEFINE_EVENT(wqe_template, hns_sq_wqe,
	     TP_PROTO(unsigned long qpn, u32 idx, void *wqe, u32 len, u64 id,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, idx, wqe, len, id, type));
DEFINE_EVENT(wqe_template, hns_rq_wqe,
	     TP_PROTO(unsigned long qpn, u32 idx, void *wqe, u32 len, u64 id,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, idx, wqe, len, id, type));
DEFINE_EVENT(wqe_template, hns_srq_wqe,
	     TP_PROTO(unsigned long qpn, u32 idx, void *wqe, u32 len, u64 id,
		      enum hns_roce_trace_type type),
	     TP_ARGS(qpn, idx, wqe, len, id, type));

#endif /* __HNS_ROCE_TRACE_H */

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hns_roce_trace
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
