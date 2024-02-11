/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_FTRACE_H
#define __ASM_CSKY_FTRACE_H

#define MCOUNT_INSN_SIZE	14

#define HAVE_FUNCTION_GRAPH_FP_TEST

#define HAVE_FUNCTION_GRAPH_RET_ADDR_PTR

#define ARCH_SUPPORTS_FTRACE_OPS 1

#define MCOUNT_ADDR	((unsigned long)_mcount)

#ifndef __ASSEMBLY__

extern void _mcount(unsigned long);

extern void ftrace_graph_call(void);

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

struct dyn_arch_ftrace {
};

void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer);

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_CSKY_FTRACE_H */
