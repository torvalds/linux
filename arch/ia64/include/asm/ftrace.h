#ifndef _ASM_IA64_FTRACE_H
#define _ASM_IA64_FTRACE_H

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_INSN_SIZE        32 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void _mcount(unsigned long pfs, unsigned long r1, unsigned long b0, unsigned long r0);
#define mcount _mcount

#endif

#endif /* CONFIG_FUNCTION_TRACER */

#endif /* _ASM_IA64_FTRACE_H */
