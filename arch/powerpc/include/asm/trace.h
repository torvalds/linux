#undef TRACE_SYSTEM
#define TRACE_SYSTEM powerpc

#if !defined(_TRACE_POWERPC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_POWERPC_H

#include <linux/tracepoint.h>

struct pt_regs;

TRACE_EVENT(irq_entry,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs),

	TP_STRUCT__entry(
		__field(struct pt_regs *, regs)
	),

	TP_fast_assign(
		__entry->regs = regs;
	),

	TP_printk("pt_regs=%p", __entry->regs)
);

TRACE_EVENT(irq_exit,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs),

	TP_STRUCT__entry(
		__field(struct pt_regs *, regs)
	),

	TP_fast_assign(
		__entry->regs = regs;
	),

	TP_printk("pt_regs=%p", __entry->regs)
);

TRACE_EVENT(timer_interrupt_entry,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs),

	TP_STRUCT__entry(
		__field(struct pt_regs *, regs)
	),

	TP_fast_assign(
		__entry->regs = regs;
	),

	TP_printk("pt_regs=%p", __entry->regs)
);

TRACE_EVENT(timer_interrupt_exit,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs),

	TP_STRUCT__entry(
		__field(struct pt_regs *, regs)
	),

	TP_fast_assign(
		__entry->regs = regs;
	),

	TP_printk("pt_regs=%p", __entry->regs)
);

#endif /* _TRACE_POWERPC_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH asm
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
