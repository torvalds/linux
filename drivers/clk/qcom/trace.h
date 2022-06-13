/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM clk_qcom

#if !defined(_TRACE_CLOCK_QCOM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CLOCK_QCOM

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(clk_state_dump,

	TP_PROTO(const char *name, unsigned int prepare_count,
	unsigned int enable_count, unsigned long rate, unsigned int vdd_level),

	TP_ARGS(name, prepare_count, enable_count, rate, vdd_level),

	TP_STRUCT__entry(
		__string(name,			name)
		__field(unsigned int,		prepare_count)
		__field(unsigned int,		enable_count)
		__field(unsigned long,		rate)
		__field(unsigned int,		vdd_level)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->prepare_count = prepare_count;
		__entry->enable_count = enable_count;
		__entry->rate = rate;
		__entry->vdd_level = vdd_level;
	),

	TP_printk("%s\tprepare:enable cnt [%u:%u]\trate: vdd_level [%lu:%u]",
		__get_str(name), __entry->prepare_count, __entry->enable_count,
		__entry->rate, __entry->vdd_level)
);

DEFINE_EVENT(clk_state_dump, clk_state,

	TP_PROTO(const char *name, unsigned int prepare_count,
	unsigned int enable_count, unsigned long rate, unsigned int vdd_level),

	TP_ARGS(name, prepare_count, enable_count, rate, vdd_level)
);

DECLARE_EVENT_CLASS(clk_measure_support,

	TP_PROTO(const char *name, unsigned long rate),

	TP_ARGS(name, rate),

	TP_STRUCT__entry(
		__string(name, name)
		__field(unsigned long, rate)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->rate = rate;
	),

	TP_printk("%s rate: %lu",
		__get_str(name), (unsigned long)__entry->rate)
);

DEFINE_EVENT(clk_measure_support, clk_measure,

	TP_PROTO(const char *name, unsigned long rate),

	TP_ARGS(name, rate)
);

#endif /* _TRACE_CLOCK_QCOM */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
