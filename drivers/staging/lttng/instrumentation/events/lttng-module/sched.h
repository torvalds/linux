#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched

#if !defined(_TRACE_SCHED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SCHED_H

#include <linux/sched.h>
#include <linux/tracepoint.h>
#include <linux/binfmts.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0))
#include <linux/sched/rt.h>
#endif

#ifndef _TRACE_SCHED_DEF_
#define _TRACE_SCHED_DEF_

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))

static inline long __trace_sched_switch_state(struct task_struct *p)
{
	long state = p->state;

#ifdef CONFIG_PREEMPT
	/*
	 * For all intents and purposes a preempted task is a running task.
	 */
	if (task_thread_info(p)->preempt_count & PREEMPT_ACTIVE)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
		state = TASK_RUNNING | TASK_STATE_MAX;
#else
		state = TASK_RUNNING;
#endif
#endif

	return state;
}

#endif

#endif /* _TRACE_SCHED_DEF_ */

/*
 * Tracepoint for calling kthread_stop, performed to end a kthread:
 */
TRACE_EVENT(sched_kthread_stop,

	TP_PROTO(struct task_struct *t),

	TP_ARGS(t),

	TP_STRUCT__entry(
		__array_text(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	tid			)
	),

	TP_fast_assign(
		tp_memcpy(comm, t->comm, TASK_COMM_LEN)
		tp_assign(tid, t->pid)
	),

	TP_printk("comm=%s tid=%d", __entry->comm, __entry->tid)
)

/*
 * Tracepoint for the return value of the kthread stopping:
 */
TRACE_EVENT(sched_kthread_stop_ret,

	TP_PROTO(int ret),

	TP_ARGS(ret),

	TP_STRUCT__entry(
		__field(	int,	ret	)
	),

	TP_fast_assign(
		tp_assign(ret, ret)
	),

	TP_printk("ret=%d", __entry->ret)
)

/*
 * Tracepoint for waking up a task:
 */
DECLARE_EVENT_CLASS(sched_wakeup_template,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	TP_PROTO(struct task_struct *p, int success),

	TP_ARGS(p, success),
#else
	TP_PROTO(struct rq *rq, struct task_struct *p, int success),

	TP_ARGS(rq, p, success),
#endif

	TP_STRUCT__entry(
		__array_text(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	tid			)
		__field(	int,	prio			)
		__field(	int,	success			)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
		__field(	int,	target_cpu		)
#endif
	),

	TP_fast_assign(
		tp_memcpy(comm, p->comm, TASK_COMM_LEN)
		tp_assign(tid, p->pid)
		tp_assign(prio, p->prio)
		tp_assign(success, success)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
		tp_assign(target_cpu, task_cpu(p))
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	)
	TP_perf_assign(
		__perf_task(p)
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
	TP_printk("comm=%s tid=%d prio=%d success=%d target_cpu=%03d",
		  __entry->comm, __entry->tid, __entry->prio,
		  __entry->success, __entry->target_cpu)
#else
	TP_printk("comm=%s tid=%d prio=%d success=%d",
		  __entry->comm, __entry->tid, __entry->prio,
		  __entry->success)
#endif
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))

DEFINE_EVENT(sched_wakeup_template, sched_wakeup,
	     TP_PROTO(struct task_struct *p, int success),
	     TP_ARGS(p, success))

/*
 * Tracepoint for waking up a new task:
 */
DEFINE_EVENT(sched_wakeup_template, sched_wakeup_new,
	     TP_PROTO(struct task_struct *p, int success),
	     TP_ARGS(p, success))

#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) */

DEFINE_EVENT(sched_wakeup_template, sched_wakeup,
	     TP_PROTO(struct rq *rq, struct task_struct *p, int success),
	     TP_ARGS(rq, p, success))

/*
 * Tracepoint for waking up a new task:
 */
DEFINE_EVENT(sched_wakeup_template, sched_wakeup_new,
	     TP_PROTO(struct rq *rq, struct task_struct *p, int success),
	     TP_ARGS(rq, p, success))

#endif /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) */

/*
 * Tracepoint for task switches, performed by the scheduler:
 */
TRACE_EVENT(sched_switch,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	TP_PROTO(struct task_struct *prev,
		 struct task_struct *next),

	TP_ARGS(prev, next),
#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) */
	TP_PROTO(struct rq *rq, struct task_struct *prev,
		 struct task_struct *next),

	TP_ARGS(rq, prev, next),
#endif /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) */

	TP_STRUCT__entry(
		__array_text(	char,	prev_comm,	TASK_COMM_LEN	)
		__field(	pid_t,	prev_tid			)
		__field(	int,	prev_prio			)
		__field(	long,	prev_state			)
		__array_text(	char,	next_comm,	TASK_COMM_LEN	)
		__field(	pid_t,	next_tid			)
		__field(	int,	next_prio			)
	),

	TP_fast_assign(
		tp_memcpy(next_comm, next->comm, TASK_COMM_LEN)
		tp_assign(prev_tid, prev->pid)
		tp_assign(prev_prio, prev->prio - MAX_RT_PRIO)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
		tp_assign(prev_state, __trace_sched_switch_state(prev))
#else
		tp_assign(prev_state, prev->state)
#endif
		tp_memcpy(prev_comm, prev->comm, TASK_COMM_LEN)
		tp_assign(next_tid, next->pid)
		tp_assign(next_prio, next->prio - MAX_RT_PRIO)
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	TP_printk("prev_comm=%s prev_tid=%d prev_prio=%d prev_state=%s%s ==> next_comm=%s next_tid=%d next_prio=%d",
		__entry->prev_comm, __entry->prev_tid, __entry->prev_prio,
		__entry->prev_state & (TASK_STATE_MAX-1) ?
		  __print_flags(__entry->prev_state & (TASK_STATE_MAX-1), "|",
				{ 1, "S"} , { 2, "D" }, { 4, "T" }, { 8, "t" },
				{ 16, "Z" }, { 32, "X" }, { 64, "x" },
				{ 128, "W" }) : "R",
		__entry->prev_state & TASK_STATE_MAX ? "+" : "",
		__entry->next_comm, __entry->next_tid, __entry->next_prio)
#else
	TP_printk("prev_comm=%s prev_tid=%d prev_prio=%d prev_state=%s ==> next_comm=%s next_tid=%d next_prio=%d",
		__entry->prev_comm, __entry->prev_tid, __entry->prev_prio,
		__entry->prev_state ?
		  __print_flags(__entry->prev_state, "|",
				{ 1, "S"} , { 2, "D" }, { 4, "T" }, { 8, "t" },
				{ 16, "Z" }, { 32, "X" }, { 64, "x" },
				{ 128, "W" }) : "R",
		__entry->next_comm, __entry->next_tid, __entry->next_prio)
#endif
)

/*
 * Tracepoint for a task being migrated:
 */
TRACE_EVENT(sched_migrate_task,

	TP_PROTO(struct task_struct *p, int dest_cpu),

	TP_ARGS(p, dest_cpu),

	TP_STRUCT__entry(
		__array_text(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	tid			)
		__field(	int,	prio			)
		__field(	int,	orig_cpu		)
		__field(	int,	dest_cpu		)
	),

	TP_fast_assign(
		tp_memcpy(comm, p->comm, TASK_COMM_LEN)
		tp_assign(tid, p->pid)
		tp_assign(prio, p->prio - MAX_RT_PRIO)
		tp_assign(orig_cpu, task_cpu(p))
		tp_assign(dest_cpu, dest_cpu)
	),

	TP_printk("comm=%s tid=%d prio=%d orig_cpu=%d dest_cpu=%d",
		  __entry->comm, __entry->tid, __entry->prio,
		  __entry->orig_cpu, __entry->dest_cpu)
)

DECLARE_EVENT_CLASS(sched_process_template,

	TP_PROTO(struct task_struct *p),

	TP_ARGS(p),

	TP_STRUCT__entry(
		__array_text(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	tid			)
		__field(	int,	prio			)
	),

	TP_fast_assign(
		tp_memcpy(comm, p->comm, TASK_COMM_LEN)
		tp_assign(tid, p->pid)
		tp_assign(prio, p->prio - MAX_RT_PRIO)
	),

	TP_printk("comm=%s tid=%d prio=%d",
		  __entry->comm, __entry->tid, __entry->prio)
)

/*
 * Tracepoint for freeing a task:
 */
DEFINE_EVENT(sched_process_template, sched_process_free,
	     TP_PROTO(struct task_struct *p),
	     TP_ARGS(p))


/*
 * Tracepoint for a task exiting:
 */
DEFINE_EVENT(sched_process_template, sched_process_exit,
	     TP_PROTO(struct task_struct *p),
	     TP_ARGS(p))

/*
 * Tracepoint for waiting on task to unschedule:
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
DEFINE_EVENT(sched_process_template, sched_wait_task,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p))
#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) */
DEFINE_EVENT(sched_process_template, sched_wait_task,
	TP_PROTO(struct rq *rq, struct task_struct *p),
	TP_ARGS(rq, p))
#endif /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) */

/*
 * Tracepoint for a waiting task:
 */
TRACE_EVENT(sched_process_wait,

	TP_PROTO(struct pid *pid),

	TP_ARGS(pid),

	TP_STRUCT__entry(
		__array_text(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	tid			)
		__field(	int,	prio			)
	),

	TP_fast_assign(
		tp_memcpy(comm, current->comm, TASK_COMM_LEN)
		tp_assign(tid, pid_nr(pid))
		tp_assign(prio, current->prio - MAX_RT_PRIO)
	),

	TP_printk("comm=%s tid=%d prio=%d",
		  __entry->comm, __entry->tid, __entry->prio)
)

/*
 * Tracepoint for do_fork.
 * Saving both TID and PID information, especially for the child, allows
 * trace analyzers to distinguish between creation of a new process and
 * creation of a new thread. Newly created processes will have child_tid
 * == child_pid, while creation of a thread yields to child_tid !=
 * child_pid.
 */
TRACE_EVENT(sched_process_fork,

	TP_PROTO(struct task_struct *parent, struct task_struct *child),

	TP_ARGS(parent, child),

	TP_STRUCT__entry(
		__array_text(	char,	parent_comm,	TASK_COMM_LEN	)
		__field(	pid_t,	parent_tid			)
		__field(	pid_t,	parent_pid			)
		__array_text(	char,	child_comm,	TASK_COMM_LEN	)
		__field(	pid_t,	child_tid			)
		__field(	pid_t,	child_pid			)
	),

	TP_fast_assign(
		tp_memcpy(parent_comm, parent->comm, TASK_COMM_LEN)
		tp_assign(parent_tid, parent->pid)
		tp_assign(parent_pid, parent->tgid)
		tp_memcpy(child_comm, child->comm, TASK_COMM_LEN)
		tp_assign(child_tid, child->pid)
		tp_assign(child_pid, child->tgid)
	),

	TP_printk("comm=%s tid=%d child_comm=%s child_tid=%d",
		__entry->parent_comm, __entry->parent_tid,
		__entry->child_comm, __entry->child_tid)
)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
/*
 * Tracepoint for sending a signal:
 */
TRACE_EVENT(sched_signal_send,

	TP_PROTO(int sig, struct task_struct *p),

	TP_ARGS(sig, p),

	TP_STRUCT__entry(
		__field(	int,	sig			)
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
	),

	TP_fast_assign(
		tp_memcpy(comm, p->comm, TASK_COMM_LEN)
		tp_assign(pid, p->pid)
		tp_assign(sig, sig)
	),

	TP_printk("sig=%d comm=%s pid=%d",
		__entry->sig, __entry->comm, __entry->pid)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
/*
 * Tracepoint for exec:
 */
TRACE_EVENT(sched_process_exec,

	TP_PROTO(struct task_struct *p, pid_t old_pid,
		 struct linux_binprm *bprm),

	TP_ARGS(p, old_pid, bprm),

	TP_STRUCT__entry(
		__string(	filename,	bprm->filename	)
		__field(	pid_t,		tid		)
		__field(	pid_t,		old_tid		)
	),

	TP_fast_assign(
		tp_strcpy(filename, bprm->filename)
		tp_assign(tid, p->pid)
		tp_assign(old_tid, old_pid)
	),

	TP_printk("filename=%s tid=%d old_tid=%d", __get_str(filename),
		  __entry->tid, __entry->old_tid)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
/*
 * XXX the below sched_stat tracepoints only apply to SCHED_OTHER/BATCH/IDLE
 *     adding sched_stat support to SCHED_FIFO/RR would be welcome.
 */
DECLARE_EVENT_CLASS(sched_stat_template,

	TP_PROTO(struct task_struct *tsk, u64 delay),

	TP_ARGS(tsk, delay),

	TP_STRUCT__entry(
		__array_text( char,	comm,	TASK_COMM_LEN	)
		__field( pid_t,	tid			)
		__field( u64,	delay			)
	),

	TP_fast_assign(
		tp_memcpy(comm, tsk->comm, TASK_COMM_LEN)
		tp_assign(tid,  tsk->pid)
		tp_assign(delay, delay)
	)
	TP_perf_assign(
		__perf_count(delay)
	),

	TP_printk("comm=%s tid=%d delay=%Lu [ns]",
			__entry->comm, __entry->tid,
			(unsigned long long)__entry->delay)
)


/*
 * Tracepoint for accounting wait time (time the task is runnable
 * but not actually running due to scheduler contention).
 */
DEFINE_EVENT(sched_stat_template, sched_stat_wait,
	     TP_PROTO(struct task_struct *tsk, u64 delay),
	     TP_ARGS(tsk, delay))

/*
 * Tracepoint for accounting sleep time (time the task is not runnable,
 * including iowait, see below).
 */
DEFINE_EVENT(sched_stat_template, sched_stat_sleep,
	     TP_PROTO(struct task_struct *tsk, u64 delay),
	     TP_ARGS(tsk, delay))

/*
 * Tracepoint for accounting iowait time (time the task is not runnable
 * due to waiting on IO to complete).
 */
DEFINE_EVENT(sched_stat_template, sched_stat_iowait,
	     TP_PROTO(struct task_struct *tsk, u64 delay),
	     TP_ARGS(tsk, delay))

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
/*
 * Tracepoint for accounting blocked time (time the task is in uninterruptible).
 */
DEFINE_EVENT(sched_stat_template, sched_stat_blocked,
	     TP_PROTO(struct task_struct *tsk, u64 delay),
	     TP_ARGS(tsk, delay))
#endif

/*
 * Tracepoint for accounting runtime (time the task is executing
 * on a CPU).
 */
TRACE_EVENT(sched_stat_runtime,

	TP_PROTO(struct task_struct *tsk, u64 runtime, u64 vruntime),

	TP_ARGS(tsk, runtime, vruntime),

	TP_STRUCT__entry(
		__array_text( char,	comm,	TASK_COMM_LEN	)
		__field( pid_t,	tid			)
		__field( u64,	runtime			)
		__field( u64,	vruntime			)
	),

	TP_fast_assign(
		tp_memcpy(comm, tsk->comm, TASK_COMM_LEN)
		tp_assign(tid, tsk->pid)
		tp_assign(runtime, runtime)
		tp_assign(vruntime, vruntime)
	)
	TP_perf_assign(
		__perf_count(runtime)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
		__perf_task(tsk)
#endif
	),

	TP_printk("comm=%s tid=%d runtime=%Lu [ns] vruntime=%Lu [ns]",
			__entry->comm, __entry->tid,
			(unsigned long long)__entry->runtime,
			(unsigned long long)__entry->vruntime)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
/*
 * Tracepoint for showing priority inheritance modifying a tasks
 * priority.
 */
TRACE_EVENT(sched_pi_setprio,

	TP_PROTO(struct task_struct *tsk, int newprio),

	TP_ARGS(tsk, newprio),

	TP_STRUCT__entry(
		__array_text( char,	comm,	TASK_COMM_LEN	)
		__field( pid_t,	tid			)
		__field( int,	oldprio			)
		__field( int,	newprio			)
	),

	TP_fast_assign(
		tp_memcpy(comm, tsk->comm, TASK_COMM_LEN)
		tp_assign(tid, tsk->pid)
		tp_assign(oldprio, tsk->prio - MAX_RT_PRIO)
		tp_assign(newprio, newprio - MAX_RT_PRIO)
	),

	TP_printk("comm=%s tid=%d oldprio=%d newprio=%d",
			__entry->comm, __entry->tid,
			__entry->oldprio, __entry->newprio)
)
#endif

#endif /* _TRACE_SCHED_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
