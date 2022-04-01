/* SPDX-License-Identifier: GPL-2.0 */
/*
 * amd-pstate-trace.h - AMD Processor P-state Frequency Driver Tracer
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * Author: Huang Rui <ray.huang@amd.com>
 */

#if !defined(_AMD_PSTATE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _AMD_PSTATE_TRACE_H

#include <linux/cpufreq.h>
#include <linux/tracepoint.h>
#include <linux/trace_events.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM amd_cpu

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE amd-pstate-trace

#define TPS(x)  tracepoint_string(x)

TRACE_EVENT(amd_pstate_perf,

	TP_PROTO(unsigned long min_perf,
		 unsigned long target_perf,
		 unsigned long capacity,
		 unsigned int cpu_id,
		 bool changed,
		 bool fast_switch
		 ),

	TP_ARGS(min_perf,
		target_perf,
		capacity,
		cpu_id,
		changed,
		fast_switch
		),

	TP_STRUCT__entry(
		__field(unsigned long, min_perf)
		__field(unsigned long, target_perf)
		__field(unsigned long, capacity)
		__field(unsigned int, cpu_id)
		__field(bool, changed)
		__field(bool, fast_switch)
		),

	TP_fast_assign(
		__entry->min_perf = min_perf;
		__entry->target_perf = target_perf;
		__entry->capacity = capacity;
		__entry->cpu_id = cpu_id;
		__entry->changed = changed;
		__entry->fast_switch = fast_switch;
		),

	TP_printk("amd_min_perf=%lu amd_des_perf=%lu amd_max_perf=%lu cpu_id=%u changed=%s fast_switch=%s",
		  (unsigned long)__entry->min_perf,
		  (unsigned long)__entry->target_perf,
		  (unsigned long)__entry->capacity,
		  (unsigned int)__entry->cpu_id,
		  (__entry->changed) ? "true" : "false",
		  (__entry->fast_switch) ? "true" : "false"
		 )
);

#endif /* _AMD_PSTATE_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#include <trace/define_trace.h>
