/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#if !defined(_TRACE_CPUCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CPUCP_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpucp

#include <linux/tracepoint.h>

TRACE_EVENT(cpucp_log,

	TP_PROTO(char *str),

	TP_ARGS(str),

	TP_STRUCT__entry(
		__string(str, str)
	),

	TP_fast_assign(
		__assign_str(str, str);
	),

	TP_printk("%s\n", __get_str(str))
);

#endif /* _TRACE_CPUCP_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_cpucp

#include <trace/define_trace.h>
