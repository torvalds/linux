#ifndef _ASM_SPARC64_FTRACE
#define _ASM_SPARC64_FTRACE

#ifndef __ASSEMBLY__
#define ftrace_nmi_enter()	do { } while (0)
#define ftrace_nmi_exit()	do { } while (0)
#endif

#ifdef CONFIG_MCOUNT
#define MCOUNT_ADDR		((long)(_mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void _mcount(void);
#endif

#endif

#endif /* _ASM_SPARC64_FTRACE */
