#ifndef _ASM_PARISC_FTRACE_H
#define _ASM_PARISC_FTRACE_H

#ifndef __ASSEMBLY__
extern void mcount(void);

#define MCOUNT_INSN_SIZE 4

extern unsigned long return_address(unsigned int);

#define ftrace_return_address(n) return_address(n)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_PARISC_FTRACE_H */
