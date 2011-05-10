#ifndef _ASM_S390_FTRACE_H
#define _ASM_S390_FTRACE_H

#ifndef __ASSEMBLY__

extern void _mcount(void);

struct dyn_arch_ftrace { };

#define MCOUNT_ADDR ((long)_mcount)

#ifdef CONFIG_64BIT
#define MCOUNT_INSN_SIZE  12
#define MCOUNT_OFFSET	   8
#else
#define MCOUNT_INSN_SIZE  20
#define MCOUNT_OFFSET	   4
#endif

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr - MCOUNT_OFFSET;
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_FTRACE_H */
