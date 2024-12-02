/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2020 Qiang Yu <yuq825@gmail.com> */

#if !defined(_LIMA_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _LIMA_TRACE_H_

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM lima
#define TRACE_INCLUDE_FILE lima_trace

DECLARE_EVENT_CLASS(lima_task,
	TP_PROTO(struct lima_sched_task *task),
	TP_ARGS(task),
	TP_STRUCT__entry(
		__field(uint64_t, task_id)
		__field(unsigned int, context)
		__field(unsigned int, seqno)
		__string(pipe, task->base.sched->name)
		),

	TP_fast_assign(
		__entry->task_id = task->base.id;
		__entry->context = task->base.s_fence->finished.context;
		__entry->seqno = task->base.s_fence->finished.seqno;
		__assign_str(pipe, task->base.sched->name);
		),

	TP_printk("task=%llu, context=%u seqno=%u pipe=%s",
		  __entry->task_id, __entry->context, __entry->seqno,
		  __get_str(pipe))
);

DEFINE_EVENT(lima_task, lima_task_submit,
	     TP_PROTO(struct lima_sched_task *task),
	     TP_ARGS(task)
);

DEFINE_EVENT(lima_task, lima_task_run,
	     TP_PROTO(struct lima_sched_task *task),
	     TP_ARGS(task)
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/lima
#include <trace/define_trace.h>
