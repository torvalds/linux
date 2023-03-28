/* SPDX-License-Identifier: GPL-2.0 */
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

#ifdef CONFIG_PPC_DOORBELL
DEFINE_EVENT(ppc64_interrupt_class, doorbell_entry,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs)
);

DEFINE_EVENT(ppc64_interrupt_class, doorbell_exit,

	TP_PROTO(struct pt_regs *regs),

	TP_ARGS(regs)
);
#endif

#ifdef CONFIG_PPC_PSERIES
extern int hcall_tracepoint_regfunc(void);
extern void hcall_tracepoint_unregfunc(void);

TRACE_EVENT_FN_COND(hcall_entry,

	TP_PROTO(unsigned long opcode, unsigned long *args),

	TP_ARGS(opcode, args),

	TP_CONDITION(cpu_online(raw_smp_processor_id())),

	TP_STRUCT__entry(
		__field(unsigned long, opcode)
	),

	TP_fast_assign(
		__entry->opcode = opcode;
	),

	TP_printk("opcode=%lu", __entry->opcode),

	hcall_tracepoint_regfunc, hcall_tracepoint_unregfunc
);

TRACE_EVENT_FN_COND(hcall_exit,

	TP_PROTO(unsigned long opcode, long retval, unsigned long *retbuf),

	TP_ARGS(opcode, retval, retbuf),

	TP_CONDITION(cpu_online(raw_smp_processor_id())),

	TP_STRUCT__entry(
		__field(unsigned long, opcode)
		__field(long, retval)
	),

	TP_fast_assign(
		__entry->opcode = opcode;
		__entry->retval = retval;
	),

	TP_printk("opcode=%lu retval=%ld", __entry->opcode, __entry->retval),

	hcall_tracepoint_regfunc, hcall_tracepoint_unregfunc
);
#endif

#ifdef CONFIG_PPC_RTAS

#include <asm/rtas-types.h>

TRACE_EVENT(rtas_input,

	TP_PROTO(struct rtas_args *rtas_args, const char *name),

	TP_ARGS(rtas_args, name),

	TP_STRUCT__entry(
		__field(__u32, nargs)
		__string(name, name)
		__dynamic_array(__u32, inputs, be32_to_cpu(rtas_args->nargs))
	),

	TP_fast_assign(
		__entry->nargs = be32_to_cpu(rtas_args->nargs);
		__assign_str(name, name);
		be32_to_cpu_array(__get_dynamic_array(inputs), rtas_args->args, __entry->nargs);
	),

	TP_printk("%s arguments: %s", __get_str(name),
		  __print_array(__get_dynamic_array(inputs), __entry->nargs, 4)
	)
);

TRACE_EVENT(rtas_output,

	TP_PROTO(struct rtas_args *rtas_args, const char *name),

	TP_ARGS(rtas_args, name),

	TP_STRUCT__entry(
		__field(__u32, nr_other)
		__field(__s32, status)
		__string(name, name)
		__dynamic_array(__u32, other_outputs, be32_to_cpu(rtas_args->nret) - 1)
	),

	TP_fast_assign(
		__entry->nr_other = be32_to_cpu(rtas_args->nret) - 1;
		__entry->status = be32_to_cpu(rtas_args->rets[0]);
		__assign_str(name, name);
		be32_to_cpu_array(__get_dynamic_array(other_outputs),
				  &rtas_args->rets[1], __entry->nr_other);
	),

	TP_printk("%s status: %d, other outputs: %s", __get_str(name), __entry->status,
		  __print_array(__get_dynamic_array(other_outputs),
				__entry->nr_other, 4)
	)
);

DECLARE_EVENT_CLASS(rtas_parameter_block,

	TP_PROTO(struct rtas_args *rtas_args),

	TP_ARGS(rtas_args),

	TP_STRUCT__entry(
		__field(u32, token)
		__field(u32, nargs)
		__field(u32, nret)
		__array(__u32, params, 16)
	),

	TP_fast_assign(
		__entry->token = be32_to_cpu(rtas_args->token);
		__entry->nargs = be32_to_cpu(rtas_args->nargs);
		__entry->nret = be32_to_cpu(rtas_args->nret);
		be32_to_cpu_array(__entry->params, rtas_args->args, ARRAY_SIZE(rtas_args->args));
	),

	TP_printk("token=%u nargs=%u nret=%u params:"
		  " [0]=0x%08x [1]=0x%08x [2]=0x%08x [3]=0x%08x"
		  " [4]=0x%08x [5]=0x%08x [6]=0x%08x [7]=0x%08x"
		  " [8]=0x%08x [9]=0x%08x [10]=0x%08x [11]=0x%08x"
		  " [12]=0x%08x [13]=0x%08x [14]=0x%08x [15]=0x%08x",
		  __entry->token, __entry->nargs, __entry->nret,
		  __entry->params[0], __entry->params[1], __entry->params[2], __entry->params[3],
		  __entry->params[4], __entry->params[5], __entry->params[6], __entry->params[7],
		  __entry->params[8], __entry->params[9], __entry->params[10], __entry->params[11],
		  __entry->params[12], __entry->params[13], __entry->params[14], __entry->params[15]
	)
);

DEFINE_EVENT(rtas_parameter_block, rtas_ll_entry,

	TP_PROTO(struct rtas_args *rtas_args),

	TP_ARGS(rtas_args)
);

DEFINE_EVENT(rtas_parameter_block, rtas_ll_exit,

	TP_PROTO(struct rtas_args *rtas_args),

	TP_ARGS(rtas_args)
);

#endif /* CONFIG_PPC_RTAS */

#ifdef CONFIG_PPC_POWERNV
extern int opal_tracepoint_regfunc(void);
extern void opal_tracepoint_unregfunc(void);

TRACE_EVENT_FN(opal_entry,

	TP_PROTO(unsigned long opcode, unsigned long *args),

	TP_ARGS(opcode, args),

	TP_STRUCT__entry(
		__field(unsigned long, opcode)
	),

	TP_fast_assign(
		__entry->opcode = opcode;
	),

	TP_printk("opcode=%lu", __entry->opcode),

	opal_tracepoint_regfunc, opal_tracepoint_unregfunc
);

TRACE_EVENT_FN(opal_exit,

	TP_PROTO(unsigned long opcode, unsigned long retval),

	TP_ARGS(opcode, retval),

	TP_STRUCT__entry(
		__field(unsigned long, opcode)
		__field(unsigned long, retval)
	),

	TP_fast_assign(
		__entry->opcode = opcode;
		__entry->retval = retval;
	),

	TP_printk("opcode=%lu retval=%lu", __entry->opcode, __entry->retval),

	opal_tracepoint_regfunc, opal_tracepoint_unregfunc
);
#endif

TRACE_EVENT(hash_fault,

	    TP_PROTO(unsigned long addr, unsigned long access, unsigned long trap),
	    TP_ARGS(addr, access, trap),
	    TP_STRUCT__entry(
		    __field(unsigned long, addr)
		    __field(unsigned long, access)
		    __field(unsigned long, trap)
		    ),

	    TP_fast_assign(
		    __entry->addr = addr;
		    __entry->access = access;
		    __entry->trap = trap;
		    ),

	    TP_printk("hash fault with addr 0x%lx and access = 0x%lx trap = 0x%lx",
		      __entry->addr, __entry->access, __entry->trap)
);


TRACE_EVENT(tlbie,

	TP_PROTO(unsigned long lpid, unsigned long local, unsigned long rb,
		unsigned long rs, unsigned long ric, unsigned long prs,
		unsigned long r),
	TP_ARGS(lpid, local, rb, rs, ric, prs, r),
	TP_STRUCT__entry(
		__field(unsigned long, lpid)
		__field(unsigned long, local)
		__field(unsigned long, rb)
		__field(unsigned long, rs)
		__field(unsigned long, ric)
		__field(unsigned long, prs)
		__field(unsigned long, r)
		),

	TP_fast_assign(
		__entry->lpid = lpid;
		__entry->local = local;
		__entry->rb = rb;
		__entry->rs = rs;
		__entry->ric = ric;
		__entry->prs = prs;
		__entry->r = r;
		),

	TP_printk("lpid=%ld, local=%ld, rb=0x%lx, rs=0x%lx, ric=0x%lx, "
		"prs=0x%lx, r=0x%lx", __entry->lpid, __entry->local,
		__entry->rb, __entry->rs, __entry->ric, __entry->prs,
		__entry->r)
);

TRACE_EVENT(tlbia,

	TP_PROTO(unsigned long id),
	TP_ARGS(id),
	TP_STRUCT__entry(
		__field(unsigned long, id)
		),

	TP_fast_assign(
		__entry->id = id;
		),

	TP_printk("ctx.id=0x%lx", __entry->id)
);

#endif /* _TRACE_POWERPC_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH asm
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
