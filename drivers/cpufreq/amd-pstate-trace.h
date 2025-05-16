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

	TP_PROTO(u8 min_perf,
		 u8 target_perf,
		 u8 capacity,
		 u64 freq,
		 u64 mperf,
		 u64 aperf,
		 u64 tsc,
		 unsigned int cpu_id,
		 bool fast_switch
		 ),

	TP_ARGS(min_perf,
		target_perf,
		capacity,
		freq,
		mperf,
		aperf,
		tsc,
		cpu_id,
		fast_switch
		),

	TP_STRUCT__entry(
		__field(u8, min_perf)
		__field(u8, target_perf)
		__field(u8, capacity)
		__field(unsigned long long, freq)
		__field(unsigned long long, mperf)
		__field(unsigned long long, aperf)
		__field(unsigned long long, tsc)
		__field(unsigned int, cpu_id)
		__field(bool, fast_switch)
		),

	TP_fast_assign(
		__entry->min_perf = min_perf;
		__entry->target_perf = target_perf;
		__entry->capacity = capacity;
		__entry->freq = freq;
		__entry->mperf = mperf;
		__entry->aperf = aperf;
		__entry->tsc = tsc;
		__entry->cpu_id = cpu_id;
		__entry->fast_switch = fast_switch;
		),

	TP_printk("amd_min_perf=%hhu amd_des_perf=%hhu amd_max_perf=%hhu freq=%llu mperf=%llu aperf=%llu tsc=%llu cpu_id=%u fast_switch=%s",
		  (u8)__entry->min_perf,
		  (u8)__entry->target_perf,
		  (u8)__entry->capacity,
		  (unsigned long long)__entry->freq,
		  (unsigned long long)__entry->mperf,
		  (unsigned long long)__entry->aperf,
		  (unsigned long long)__entry->tsc,
		  (unsigned int)__entry->cpu_id,
		  (__entry->fast_switch) ? "true" : "false"
		 )
);

TRACE_EVENT(amd_pstate_epp_perf,

	TP_PROTO(unsigned int cpu_id,
		 u8 highest_perf,
		 u8 epp,
		 u8 min_perf,
		 u8 max_perf,
		 bool boost,
		 bool changed
		 ),

	TP_ARGS(cpu_id,
		highest_perf,
		epp,
		min_perf,
		max_perf,
		boost,
		changed),

	TP_STRUCT__entry(
		__field(unsigned int, cpu_id)
		__field(u8, highest_perf)
		__field(u8, epp)
		__field(u8, min_perf)
		__field(u8, max_perf)
		__field(bool, boost)
		__field(bool, changed)
		),

	TP_fast_assign(
		__entry->cpu_id = cpu_id;
		__entry->highest_perf = highest_perf;
		__entry->epp = epp;
		__entry->min_perf = min_perf;
		__entry->max_perf = max_perf;
		__entry->boost = boost;
		__entry->changed = changed;
		),

	TP_printk("cpu%u: [%hhu<->%hhu]/%hhu, epp=%hhu, boost=%u, changed=%u",
		  (unsigned int)__entry->cpu_id,
		  (u8)__entry->min_perf,
		  (u8)__entry->max_perf,
		  (u8)__entry->highest_perf,
		  (u8)__entry->epp,
		  (bool)__entry->boost,
		  (bool)__entry->changed
		 )
);

#endif /* _AMD_PSTATE_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#include <trace/define_trace.h>
