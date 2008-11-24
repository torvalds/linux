#ifndef _ASM_POWERPC_FTRACE
#define _ASM_POWERPC_FTRACE

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_ADDR		((long)(_mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void _mcount(void);

#ifdef CONFIG_DYNAMIC_FTRACE
static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
       /* reloction of mcount call site is the same as the address */
       return addr;
}

struct dyn_arch_ftrace {
	struct module *mod;
};
#endif /*  CONFIG_DYNAMIC_FTRACE */
#endif /* __ASSEMBLY__ */

#endif

#endif /* _ASM_POWERPC_FTRACE */
