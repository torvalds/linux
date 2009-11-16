#ifndef _ASM_MICROBLAZE_FTRACE
#define _ASM_MICROBLAZE_FTRACE

#ifdef CONFIG_FUNCTION_TRACER

#define MCOUNT_ADDR		((long)(_mcount))
#define MCOUNT_INSN_SIZE	8 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void _mcount(void);
extern void ftrace_call_graph(void);
#endif

#endif /* CONFIG_FUNCTION_TRACER */
#endif /* _ASM_MICROBLAZE_FTRACE */
