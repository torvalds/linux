#undef TRACE_SYSTEM
#define TRACE_SYSTEM sunrpc

#if !defined(_TRACE_SUNRPC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SUNRPC_H

#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(rpc_task_status,

	TP_PROTO(struct rpc_task *task),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(const struct rpc_task *, task)
		__field(const struct rpc_clnt *, clnt)
		__field(int, status)
	),

	TP_fast_assign(
		tp_assign(task, task)
		tp_assign(clnt, task->tk_client)
		tp_assign(status, task->tk_status)
	),

	TP_printk("task:%p@%p, status %d",__entry->task, __entry->clnt, __entry->status)
)

DEFINE_EVENT(rpc_task_status, rpc_call_status,
	TP_PROTO(struct rpc_task *task),

	TP_ARGS(task)
)

DEFINE_EVENT(rpc_task_status, rpc_bind_status,
	TP_PROTO(struct rpc_task *task),

	TP_ARGS(task)
)

TRACE_EVENT(rpc_connect_status,
	TP_PROTO(struct rpc_task *task, int status),

	TP_ARGS(task, status),

	TP_STRUCT__entry(
		__field(const struct rpc_task *, task)
		__field(const struct rpc_clnt *, clnt)
		__field(int, status)
	),

	TP_fast_assign(
		tp_assign(task, task)
		tp_assign(clnt, task->tk_client)
		tp_assign(status, status)
	),

	TP_printk("task:%p@%p, status %d",__entry->task, __entry->clnt, __entry->status)
)

DECLARE_EVENT_CLASS(rpc_task_running,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const void *action),

	TP_ARGS(clnt, task, action),

	TP_STRUCT__entry(
		__field(const struct rpc_clnt *, clnt)
		__field(const struct rpc_task *, task)
		__field(const void *, action)
		__field(unsigned long, runstate)
		__field(int, status)
		__field(unsigned short, flags)
		),

	TP_fast_assign(
		tp_assign(clnt, clnt)
		tp_assign(task, task)
		tp_assign(action, action)
		tp_assign(runstate, task->tk_runstate)
		tp_assign(status, task->tk_status)
		tp_assign(flags, task->tk_flags)
		),

	TP_printk("task:%p@%p flags=%4.4x state=%4.4lx status=%d action=%pf",
		__entry->task,
		__entry->clnt,
		__entry->flags,
		__entry->runstate,
		__entry->status,
		__entry->action
		)
)

DEFINE_EVENT(rpc_task_running, rpc_task_begin,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const void *action),

	TP_ARGS(clnt, task, action)

)

DEFINE_EVENT(rpc_task_running, rpc_task_run_action,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const void *action),

	TP_ARGS(clnt, task, action)

)

DEFINE_EVENT(rpc_task_running, rpc_task_complete,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const void *action),

	TP_ARGS(clnt, task, action)

)

DECLARE_EVENT_CLASS(rpc_task_queued,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const struct rpc_wait_queue *q),

	TP_ARGS(clnt, task, q),

	TP_STRUCT__entry(
		__field(const struct rpc_clnt *, clnt)
		__field(const struct rpc_task *, task)
		__field(unsigned long, timeout)
		__field(unsigned long, runstate)
		__field(int, status)
		__field(unsigned short, flags)
		__string(q_name, rpc_qname(q))
		),

	TP_fast_assign(
		tp_assign(clnt, clnt)
		tp_assign(task, task)
		tp_assign(timeout, task->tk_timeout)
		tp_assign(runstate, task->tk_runstate)
		tp_assign(status, task->tk_status)
		tp_assign(flags, task->tk_flags)
		tp_strcpy(q_name, rpc_qname(q))
		),

	TP_printk("task:%p@%p flags=%4.4x state=%4.4lx status=%d timeout=%lu queue=%s",
		__entry->task,
		__entry->clnt,
		__entry->flags,
		__entry->runstate,
		__entry->status,
		__entry->timeout,
		__get_str(q_name)
		)
)

DEFINE_EVENT(rpc_task_queued, rpc_task_sleep,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const struct rpc_wait_queue *q),

	TP_ARGS(clnt, task, q)

)

DEFINE_EVENT(rpc_task_queued, rpc_task_wakeup,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const struct rpc_wait_queue *q),

	TP_ARGS(clnt, task, q)

)

#endif /* _TRACE_SUNRPC_H */

#include "../../../probes/define_trace.h"
