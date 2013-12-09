#undef TRACE_SYSTEM
#define TRACE_SYSTEM raw_syscalls
#define TRACE_INCLUDE_FILE syscalls

#if !defined(_TRACE_EVENTS_SYSCALLS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EVENTS_SYSCALLS_H

#include <linux/tracepoint.h>

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS

#ifndef _TRACE_SYSCALLS_DEF_
#define _TRACE_SYSCALLS_DEF_

#include <asm/ptrace.h>
#include <asm/syscall.h>

#endif /* _TRACE_SYSCALLS_DEF_ */

TRACE_EVENT(sys_enter,

	TP_PROTO(struct pt_regs *regs, long id),

	TP_ARGS(regs, id),

	TP_STRUCT__entry(
		__field(	long,		id		)
		__array(	unsigned long,	args,	6	)
	),

	TP_fast_assign(
		tp_assign(id, id)
		{
			tp_memcpy(args,
				({
					unsigned long args_copy[6];
					syscall_get_arguments(current, regs,
							0, 6, args_copy);
					args_copy;
				}), 6 * sizeof(unsigned long));
		}
	),

	TP_printk("NR %ld (%lx, %lx, %lx, %lx, %lx, %lx)",
		  __entry->id,
		  __entry->args[0], __entry->args[1], __entry->args[2],
		  __entry->args[3], __entry->args[4], __entry->args[5])
)

TRACE_EVENT(sys_exit,

	TP_PROTO(struct pt_regs *regs, long ret),

	TP_ARGS(regs, ret),

	TP_STRUCT__entry(
		__field(	long,	id	)
		__field(	long,	ret	)
	),

	TP_fast_assign(
		tp_assign(id, syscall_get_nr(current, regs))
		tp_assign(ret, ret)
	),

	TP_printk("NR %ld = %ld",
		  __entry->id, __entry->ret)
)

#endif /* CONFIG_HAVE_SYSCALL_TRACEPOINTS */

#endif /* _TRACE_EVENTS_SYSCALLS_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"

