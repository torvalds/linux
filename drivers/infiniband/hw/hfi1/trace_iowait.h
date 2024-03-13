/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */
#if !defined(__HFI1_TRACE_IOWAIT_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HFI1_TRACE_IOWAIT_H

#include <linux/tracepoint.h>
#include "iowait.h"
#include "verbs.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_iowait

DECLARE_EVENT_CLASS(hfi1_iowait_template,
		    TP_PROTO(struct iowait *wait, u32 flag),
		    TP_ARGS(wait, flag),
		    TP_STRUCT__entry(/* entry */
			    __field(unsigned long, addr)
			    __field(unsigned long, flags)
			    __field(u32, flag)
			    __field(u32, qpn)
			    ),
		    TP_fast_assign(/* assign */
			    __entry->addr = (unsigned long)wait;
			    __entry->flags = wait->flags;
			    __entry->flag = (1 << flag);
			    __entry->qpn = iowait_to_qp(wait)->ibqp.qp_num;
			    ),
		    TP_printk(/* print */
			    "iowait 0x%lx qp %u flags 0x%lx flag 0x%x",
			    __entry->addr,
			    __entry->qpn,
			    __entry->flags,
			    __entry->flag
			    )
	);

DEFINE_EVENT(hfi1_iowait_template, hfi1_iowait_set,
	     TP_PROTO(struct iowait *wait, u32 flag),
	     TP_ARGS(wait, flag));

DEFINE_EVENT(hfi1_iowait_template, hfi1_iowait_clear,
	     TP_PROTO(struct iowait *wait, u32 flag),
	     TP_ARGS(wait, flag));

#endif /* __HFI1_TRACE_IOWAIT_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_iowait
#include <trace/define_trace.h>
