/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SPARC64_FTRACE
#define _ASM_SPARC64_FTRACE

#ifdef CONFIG_MCOUNT
#define MCOUNT_ADDR		((unsigned long)(_mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
void _mcount(void);
#endif

#endif /* CONFIG_MCOUNT */

#if defined(CONFIG_SPARC64) && !defined(CC_USE_FENTRY)
#define HAVE_FUNCTION_GRAPH_FP_TEST
#endif

#ifdef CONFIG_DYNAMIC_FTRACE
/* reloction of mcount call site is the same as the address */
static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

struct dyn_arch_ftrace {
};
#endif /*  CONFIG_DYNAMIC_FTRACE */

unsigned long prepare_ftrace_return(unsigned long parent,
				    unsigned long self_addr,
				    unsigned long frame_pointer);

#endif /* _ASM_SPARC64_FTRACE */
