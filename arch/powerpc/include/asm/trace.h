#undef TRACE_SYSTEM
#define TRACE_SYSTEM powerpc

#if !defined(_TRACE_POWERPC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_POWERPC_H

#include <linux/tracepoint.h>

struct pt_regs;

DECLARE_EVENT_CLASS(ppc64_interrupt_class,

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

DEFINE_EVENT(ppc64_interrupt_class, irq_entry,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs)
);

DEFINE_EVENT(ppc64_interrupt_class, irq_exit,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs)
);

DEFINE_EVENT(ppc64_interrupt_class, timer_interrupt_entry,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs)
);

DEFINE_EVENT(ppc64_interrupt_class, timer_interrupt_exit,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs)
);

#ifdef CONFIG_PPC_PSERIES
extern void hcall_tracepoint_regfunc(void);
extern void hcall_tracepoint_unregfunc(void);

TRACE_EVENT_FN(hcall_entry,

	TP_PROTO(unsigned long opcode, unsigned long *args),

	TP_ARGS(opcode, args),

	TP_STRUCT__entry(
		__field(unsigned long, opcode)
	),

	TP_fast_assign(
		__entry->opcode = opcode;
	),

	TP_printk("opcode=%lu", __entry->opcode),

	hcall_tracepoint_regfunc, hcall_tracepoint_unregfunc
);

TRACE_EVENT_FN(hcall_exit,

	TP_PROTO(unsigned long opcode, unsigned long retval,
		unsigned long *retbuf),

	TP_ARGS(opcode, retval, retbuf),

	TP_STRUCT__entry(
		__field(unsigned long, opcode)
		__field(unsigned long, retval)
	),

	TP_fast_assign(
		__entry->opcode = opcode;
		__entry->retval = retval;
	),

	TP_printk("opcode=%lu retval=%lu", __entry->opcode, __entry->retval),

	hcall_tracepoint_regfunc, hcall_tracepoint_unregfunc
);
#endif

#endif /* _TRACE_POWERPC_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH asm
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
