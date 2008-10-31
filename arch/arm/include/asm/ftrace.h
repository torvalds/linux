#ifndef _ASM_ARM_FTRACE
#define _ASM_ARM_FTRACE

#ifndef __ASSEMBLY__
static inline void ftrace_nmi_enter(void) { }
static inline void ftrace_nmi_exit(void) { }
#endif

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_ADDR		((long)(mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);
#endif

#endif

#endif /* _ASM_ARM_FTRACE */
