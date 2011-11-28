#if !defined(_TRACE_SYSCALLS_UNKNOWN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYSCALLS_UNKNOWN_H

#include <linux/tracepoint.h>
#include <linux/syscalls.h>

#define UNKNOWN_SYSCALL_NRARGS	6

TRACE_EVENT(sys_unknown,
	TP_PROTO(unsigned int id, unsigned long *args),
	TP_ARGS(id, args),
	TP_STRUCT__entry(
		__field(unsigned int, id)
		__array(unsigned long, args, UNKNOWN_SYSCALL_NRARGS)
	),
	TP_fast_assign(
		tp_assign(id, id)
		tp_memcpy(args, args, UNKNOWN_SYSCALL_NRARGS * sizeof(*args))
	),
	TP_printk()
)
TRACE_EVENT(compat_sys_unknown,
	TP_PROTO(unsigned int id, unsigned long *args),
	TP_ARGS(id, args),
	TP_STRUCT__entry(
		__field(unsigned int, id)
		__array(unsigned long, args, UNKNOWN_SYSCALL_NRARGS)
	),
	TP_fast_assign(
		tp_assign(id, id)
		tp_memcpy(args, args, UNKNOWN_SYSCALL_NRARGS * sizeof(*args))
	),
	TP_printk()
)
/* 
 * This is going to hook on sys_exit in the kernel.
 * We change the name so we don't clash with the sys_exit syscall entry
 * event.
 */
TRACE_EVENT(exit_syscall,
	TP_PROTO(struct pt_regs *regs, long ret),
	TP_ARGS(regs, ret),
	TP_STRUCT__entry(
		__field(long, ret)
	),
	TP_fast_assign(
		tp_assign(ret, ret)
	),
	TP_printk()
)

#endif /*  _TRACE_SYSCALLS_UNKNOWN_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
