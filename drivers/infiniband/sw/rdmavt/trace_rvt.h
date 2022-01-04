/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
 */
#if !defined(__RVT_TRACE_RVT_H) || defined(TRACE_HEADER_MULTI_READ)
#define __RVT_TRACE_RVT_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdma_vt.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rvt

TRACE_EVENT(rvt_dbg,
	TP_PROTO(struct rvt_dev_info *rdi,
		 const char *msg),
	TP_ARGS(rdi, msg),
	TP_STRUCT__entry(
		RDI_DEV_ENTRY(rdi)
		__string(msg, msg)
	),
	TP_fast_assign(
		RDI_DEV_ASSIGN(rdi);
		__assign_str(msg, msg);
	),
	TP_printk("[%s]: %s", __get_str(dev), __get_str(msg))
);

#endif /* __RVT_TRACE_MISC_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_rvt
#include <trace/define_trace.h>

