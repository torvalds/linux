#ifndef __ASM_SH_FTRACE_H
#define __ASM_SH_FTRACE_H

#ifdef CONFIG_FUNCTION_TRACER

#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);

#define MCOUNT_ADDR		((long)(mcount))

#ifdef CONFIG_DYNAMIC_FTRACE
#define CALLER_ADDR		((long)(ftrace_caller))
#define STUB_ADDR		((long)(ftrace_stub))

#define MCOUNT_INSN_OFFSET	((STUB_ADDR - CALLER_ADDR) >> 1)

struct dyn_arch_ftrace {
	/* No extra data needed on sh */
};

#endif /* CONFIG_DYNAMIC_FTRACE */

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	/* 'addr' is the memory table address. */
	return addr;
}

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_FUNCTION_TRACER */

#endif /* __ASM_SH_FTRACE_H */
