/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gh_proxy_sched
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE gh_proxy_sched_trace

#if !defined(_TRACE_GH_PROXY_SCHED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GH_PROXY_SCHED_H

#include <linux/tracepoint.h>

TRACE_EVENT(gh_hcall_vcpu_run,

	TP_PROTO(int ret, u16 vm_id, u32 vcpu_idx, u64 yield_ts,
				u64 vcpu_state, u64 vcpu_suspend_state),

	TP_ARGS(ret, vm_id, vcpu_idx, yield_ts, vcpu_state, vcpu_suspend_state),

	TP_STRUCT__entry(
		__field(int, ret)
		__field(u16, vm_id)
		__field(u32, vcpu_idx)
		__field(u64, yield_ts)
		__field(u64, vcpu_state)
		__field(u64, vcpu_suspend_state)
	),

	TP_fast_assign(
		__entry->ret			= ret;
		__entry->vm_id			= vm_id;
		__entry->vcpu_idx		= vcpu_idx;
		__entry->yield_ts		= yield_ts;
		__entry->vcpu_state		= vcpu_state;
		__entry->vcpu_suspend_state	= vcpu_suspend_state;
	),

	TP_printk("ret=%d vm=%u vcpu=%u yield_time=%llu vcpu_state=%llu vcpu_suspend_state=%llu",
			ret, __entry->vm_id, __entry->vcpu_idx, __entry->yield_ts,
			__entry->vcpu_state, __entry->vcpu_suspend_state)
);

TRACE_EVENT(gh_susp_res_irq_handler,

	TP_PROTO(u64 vpmg_state),

	TP_ARGS(vpmg_state),

	TP_STRUCT__entry(
		__field(u64, vpmg_state)
	),

	TP_fast_assign(
		__entry->vpmg_state = vpmg_state;
	),

	TP_printk("vpmg_state=%llu", __entry->vpmg_state)
);

TRACE_EVENT(gh_vcpu_irq_handler,

	TP_PROTO(u16 vm_id, u32 vcpu_idx),

	TP_ARGS(vm_id, vcpu_idx),

	TP_STRUCT__entry(
		__field(u16, vm_id)
		__field(u32, vcpu_idx)
	),

	TP_fast_assign(
		__entry->vm_id = vm_id;
		__entry->vcpu_idx = vcpu_idx;
	),

	TP_printk("vm=%u vcpu=%u", __entry->vm_id, __entry->vcpu_idx)
);
#endif /* _TRACE_GH_PROXY_SCHED_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
