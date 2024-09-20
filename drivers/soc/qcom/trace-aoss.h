/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qcom_aoss

#if !defined(_TRACE_QCOM_AOSS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QCOM_AOSS_H

#include <linux/tracepoint.h>

TRACE_EVENT(aoss_send,
	TP_PROTO(const char *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
		__string(msg, msg)
	),
	TP_fast_assign(
		__assign_str(msg);
	),
	TP_printk("%s", __get_str(msg))
);

TRACE_EVENT(aoss_send_done,
	TP_PROTO(const char *msg, int ret),
	TP_ARGS(msg, ret),
	TP_STRUCT__entry(
		__string(msg, msg)
		__field(int, ret)
	),
	TP_fast_assign(
		__assign_str(msg);
		__entry->ret = ret;
	),
	TP_printk("%s: %d", __get_str(msg), __entry->ret)
);

#endif /* _TRACE_QCOM_AOSS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-aoss

#include <trace/define_trace.h>
