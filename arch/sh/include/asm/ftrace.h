#ifndef __ASM_SH_FTRACE_H
#define __ASM_SH_FTRACE_H

#ifndef __ASSEMBLY__
static inline void ftrace_nmi_enter(void) { }
static inline void ftrace_nmi_exit(void) { }
#endif

#ifndef __ASSEMBLY__
extern void mcount(void);
#endif

#endif /* __ASM_SH_FTRACE_H */
