#undef TRACE_SYSTEM
#define TRACE_SYSTEM workqueue

#if !defined(_TRACE_WORKQUEUE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WORKQUEUE_H

#include <linux/tracepoint.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))

#ifndef _TRACE_WORKQUEUE_DEF_
#define _TRACE_WORKQUEUE_DEF_

struct worker;
struct global_cwq;

#endif

DECLARE_EVENT_CLASS(workqueue_work,

	TP_PROTO(struct work_struct *work),

	TP_ARGS(work),

	TP_STRUCT__entry(
		__field( void *,	work	)
	),

	TP_fast_assign(
		tp_assign(work, work)
	),

	TP_printk("work struct %p", __entry->work)
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
/**
 * workqueue_queue_work - called when a work gets queued
 * @req_cpu:	the requested cpu
 * @cwq:	pointer to struct cpu_workqueue_struct
 * @work:	pointer to struct work_struct
 *
 * This event occurs when a work is queued immediately or once a
 * delayed work is actually queued on a workqueue (ie: once the delay
 * has been reached).
 */
TRACE_EVENT(workqueue_queue_work,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0))
	TP_PROTO(unsigned int req_cpu, struct pool_workqueue *pwq,
		 struct work_struct *work),

	TP_ARGS(req_cpu, pwq, work),
#else
	TP_PROTO(unsigned int req_cpu, struct cpu_workqueue_struct *cwq,
		 struct work_struct *work),

	TP_ARGS(req_cpu, cwq, work),
#endif

	TP_STRUCT__entry(
		__field( void *,	work	)
		__field( void *,	function)
		__field( unsigned int,	req_cpu	)
	),

	TP_fast_assign(
		tp_assign(work, work)
		tp_assign(function, work->func)
		tp_assign(req_cpu, req_cpu)
	),

	TP_printk("work struct=%p function=%pf req_cpu=%u",
		  __entry->work, __entry->function,
		  __entry->req_cpu)
)

/**
 * workqueue_activate_work - called when a work gets activated
 * @work:	pointer to struct work_struct
 *
 * This event occurs when a queued work is put on the active queue,
 * which happens immediately after queueing unless @max_active limit
 * is reached.
 */
DEFINE_EVENT(workqueue_work, workqueue_activate_work,

	TP_PROTO(struct work_struct *work),

	TP_ARGS(work)
)
#endif

/**
 * workqueue_execute_start - called immediately before the workqueue callback
 * @work:	pointer to struct work_struct
 *
 * Allows to track workqueue execution.
 */
TRACE_EVENT(workqueue_execute_start,

	TP_PROTO(struct work_struct *work),

	TP_ARGS(work),

	TP_STRUCT__entry(
		__field( void *,	work	)
		__field( void *,	function)
	),

	TP_fast_assign(
		tp_assign(work, work)
		tp_assign(function, work->func)
	),

	TP_printk("work struct %p: function %pf", __entry->work, __entry->function)
)

/**
 * workqueue_execute_end - called immediately after the workqueue callback
 * @work:	pointer to struct work_struct
 *
 * Allows to track workqueue execution.
 */
DEFINE_EVENT(workqueue_work, workqueue_execute_end,

	TP_PROTO(struct work_struct *work),

	TP_ARGS(work)
)

#else

DECLARE_EVENT_CLASS(workqueue,

	TP_PROTO(struct task_struct *wq_thread, struct work_struct *work),

	TP_ARGS(wq_thread, work),

	TP_STRUCT__entry(
		__array(char,		thread_comm,	TASK_COMM_LEN)
		__field(pid_t,		thread_pid)
		__field(work_func_t,	func)
	),

	TP_fast_assign(
		tp_memcpy(thread_comm, wq_thread->comm, TASK_COMM_LEN)
		tp_assign(thread_pid, wq_thread->pid)
		tp_assign(func, work->func)
	),

	TP_printk("thread=%s:%d func=%pf", __entry->thread_comm,
		__entry->thread_pid, __entry->func)
)

DEFINE_EVENT(workqueue, workqueue_insertion,

	TP_PROTO(struct task_struct *wq_thread, struct work_struct *work),

	TP_ARGS(wq_thread, work)
)

DEFINE_EVENT(workqueue, workqueue_execution,

	TP_PROTO(struct task_struct *wq_thread, struct work_struct *work),

	TP_ARGS(wq_thread, work)
)

/* Trace the creation of one workqueue thread on a cpu */
TRACE_EVENT(workqueue_creation,

	TP_PROTO(struct task_struct *wq_thread, int cpu),

	TP_ARGS(wq_thread, cpu),

	TP_STRUCT__entry(
		__array(char,	thread_comm,	TASK_COMM_LEN)
		__field(pid_t,	thread_pid)
		__field(int,	cpu)
	),

	TP_fast_assign(
		tp_memcpy(thread_comm, wq_thread->comm, TASK_COMM_LEN)
		tp_assign(thread_pid, wq_thread->pid)
		tp_assign(cpu, cpu)
	),

	TP_printk("thread=%s:%d cpu=%d", __entry->thread_comm,
		__entry->thread_pid, __entry->cpu)
)

TRACE_EVENT(workqueue_destruction,

	TP_PROTO(struct task_struct *wq_thread),

	TP_ARGS(wq_thread),

	TP_STRUCT__entry(
		__array(char,	thread_comm,	TASK_COMM_LEN)
		__field(pid_t,	thread_pid)
	),

	TP_fast_assign(
		tp_memcpy(thread_comm, wq_thread->comm, TASK_COMM_LEN)
		tp_assign(thread_pid, wq_thread->pid)
	),

	TP_printk("thread=%s:%d", __entry->thread_comm, __entry->thread_pid)
)

#endif

#endif /*  _TRACE_WORKQUEUE_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
