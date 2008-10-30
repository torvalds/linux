#ifndef __ASM_SH_FTRACE_H
#define __ASM_SH_FTRACE_H

#ifndef __ASSEMBLY__
#define ftrace_nmi_enter()	do { } while (0)
#define ftrace_nmi_exit()	do { } while (0)
#endif

#ifndef __ASSEMBLY__
extern void mcount(void);
#endif

#endif /* __ASM_SH_FTRACE_H */
