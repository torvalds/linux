#ifndef _ASM_S390_FTRACE_H
#define _ASM_S390_FTRACE_H

#ifndef __ASSEMBLY__

extern void _mcount(void);
extern char ftrace_graph_caller_end;

struct dyn_arch_ftrace { };

#define MCOUNT_ADDR ((long)_mcount)


static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

#endif /* __ASSEMBLY__ */

#define MCOUNT_INSN_SIZE  18

#define ARCH_SUPPORTS_FTRACE_OPS 1

#endif /* _ASM_S390_FTRACE_H */
