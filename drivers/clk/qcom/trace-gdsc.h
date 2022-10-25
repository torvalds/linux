/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM clk_gdsc

#if !defined(_TRACE_CLOCK_GDSC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CLOCK_GDSC

#include <linux/tracepoint.h>

TRACE_EVENT(gdsc_time,

	TP_PROTO(const char *name, u32 enabling, u32 time_us, u32 timed_out),

	TP_ARGS(name, enabling, time_us, timed_out),

	TP_STRUCT__entry(
		__string(name,		name)
		__field(unsigned int,	enabling)
		__field(unsigned int,	time_us)
		__field(unsigned int,	timed_out)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->enabling = enabling;
		__entry->time_us = time_us;
		__entry->timed_out = timed_out;
	),

	TP_printk("%s enabling:%d time_us:%d timed_out:%d",
		__get_str(name), __entry->enabling,
		__entry->time_us, __entry->timed_out)
);

#endif /* _TRACE_CLOCK_GDSC */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-gdsc

#include <trace/define_trace.h>
