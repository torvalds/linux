#ifndef _ASM_S390_FTRACE_H
#define _ASM_S390_FTRACE_H

#ifndef __ASSEMBLY__

extern void _mcount(void);
extern unsigned long ftrace_dyn_func;

struct dyn_arch_ftrace { };

#define MCOUNT_ADDR ((long)_mcount)

#ifdef CONFIG_64BIT
#define MCOUNT_OFFSET_RET 18
#define MCOUNT_INSN_SIZE  24
#define MCOUNT_OFFSET	  14
#else
#define MCOUNT_OFFSET_RET 26
#define MCOUNT_INSN_SIZE  30
#define MCOUNT_OFFSET	   8
#endif

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr - MCOUNT_OFFSET;
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_FTRACE_H */
