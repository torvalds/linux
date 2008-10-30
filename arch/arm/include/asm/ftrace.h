#ifndef _ASM_ARM_FTRACE
#define _ASM_ARM_FTRACE

#ifndef __ASSEMBLY__
#define ftrace_nmi_enter()	do { } while (0)
#define ftrace_nmi_exit()	do { } while (0)
#endif

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_ADDR		((long)(mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);
#endif

#endif

#endif /* _ASM_ARM_FTRACE */
