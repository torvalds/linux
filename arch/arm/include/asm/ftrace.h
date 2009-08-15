#ifndef _ASM_ARM_FTRACE
#define _ASM_ARM_FTRACE

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_ADDR		((long)(mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);
extern void __gnu_mcount_nc(void);
#endif

#endif

#endif /* _ASM_ARM_FTRACE */
