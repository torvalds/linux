#ifndef _ASM_IA64_FTRACE_H
#define _ASM_IA64_FTRACE_H

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_INSN_SIZE        32 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void _mcount(unsigned long pfs, unsigned long r1, unsigned long b0, unsigned long r0);
#define mcount _mcount

/* In IA64, MCOUNT_ADDR is set in link time, so it's not a constant at compile time */
#define MCOUNT_ADDR (((struct fnptr *)mcount)->ip)
#define FTRACE_ADDR (((struct fnptr *)ftrace_caller)->ip)

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	/* second bundle, insn 2 */
	return addr - 0x12;
}

struct dyn_arch_ftrace {
};
#endif

#endif /* CONFIG_FUNCTION_TRACER */

#endif /* _ASM_IA64_FTRACE_H */
