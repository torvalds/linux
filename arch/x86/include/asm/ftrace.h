#ifndef _ASM_X86_FTRACE_H
#define _ASM_X86_FTRACE_H

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_ADDR		((long)(mcount))
#define MCOUNT_INSN_SIZE	5 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	/*
	 * call mcount is "e8 <4 byte offset>"
	 * The addr points to the 4 byte offset and the caller of this
	 * function wants the pointer to e8. Simply subtract one.
	 */
	return addr - 1;
}

#ifdef CONFIG_DYNAMIC_FTRACE
extern void ftrace_nmi_enter(void);
extern void ftrace_nmi_exit(void);
#else
#define ftrace_nmi_enter()	do { } while (0)
#define ftrace_nmi_exit()	do { } while (0)
#endif
#endif

#else /* CONFIG_FUNCTION_TRACER */

#ifndef __ASSEMBLY__
#define ftrace_nmi_enter()	do { } while (0)
#define ftrace_nmi_exit()	do { } while (0)
#endif

#endif /* CONFIG_FUNCTION_TRACER */

#endif /* _ASM_X86_FTRACE_H */
