#undef TRACE_SYSTEM
#define TRACE_INCLUDE_PATH ../../drivers/staging/android/trace
#define TRACE_SYSTEM lowmemorykiller

#if !defined(_TRACE_LOWMEMORYKILLER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_LOWMEMORYKILLER_H

#include <linux/tracepoint.h>

TRACE_EVENT(lowmemory_kill,
	TP_PROTO(struct task_struct *killed_task, long cache_size, \
		 long cache_limit, long free),

	TP_ARGS(killed_task, cache_size, cache_limit, free),

	TP_STRUCT__entry(
			__array(char, comm, TASK_COMM_LEN)
			__field(pid_t, pid)
			__field(long, pagecache_size)
			__field(long, pagecache_limit)
			__field(long, free)
	),

	TP_fast_assign(
			memcpy(__entry->comm, killed_task->comm, TASK_COMM_LEN);
			__entry->pid = killed_task->pid;
			__entry->pagecache_size = cache_size;
			__entry->pagecache_limit = cache_limit;
			__entry->free = free;
	),

	TP_printk("%s (%d), page cache %ldkB (limit %ldkB), free %ldKb",
		__entry->comm, __entry->pid, __entry->pagecache_size,
		__entry->pagecache_limit, __entry->free)
);


#endif /* if !defined(_TRACE_LOWMEMORYKILLER_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
