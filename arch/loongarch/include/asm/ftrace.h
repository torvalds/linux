/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_LOONGARCH_FTRACE_H
#define _ASM_LOONGARCH_FTRACE_H

#define FTRACE_PLT_IDX		0
#define FTRACE_REGS_PLT_IDX	1
#define NR_FTRACE_PLTS		2

#ifdef CONFIG_FUNCTION_TRACER

#define MCOUNT_INSN_SIZE 4		/* sizeof mcount call */

#ifndef __ASSEMBLER__

#ifndef CONFIG_DYNAMIC_FTRACE

#define mcount _mcount
extern void _mcount(void);
extern void prepare_ftrace_return(unsigned long self_addr, unsigned long callsite_sp, unsigned long old);

#else

struct dyn_ftrace;
struct dyn_arch_ftrace { };

#define ARCH_SUPPORTS_FTRACE_OPS 1

#define ftrace_init_nop ftrace_init_nop
int ftrace_init_nop(struct module *mod, struct dyn_ftrace *rec);

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

void prepare_ftrace_return(unsigned long self_addr, unsigned long *parent);

#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_HAVE_DYNAMIC_FTRACE_WITH_ARGS
struct ftrace_ops;

#include <linux/ftrace_regs.h>

static __always_inline struct pt_regs *arch_ftrace_get_regs(struct ftrace_regs *fregs)
{
	return &arch_ftrace_regs(fregs)->regs;
}

static __always_inline void
ftrace_regs_set_instruction_pointer(struct ftrace_regs *fregs, unsigned long ip)
{
	instruction_pointer_set(&arch_ftrace_regs(fregs)->regs, ip);
}

#undef ftrace_regs_get_frame_pointer
#define ftrace_regs_get_frame_pointer(fregs) \
	(arch_ftrace_regs(fregs)->regs.regs[22])

static __always_inline unsigned long
ftrace_regs_get_return_address(struct ftrace_regs *fregs)
{
	return *(unsigned long *)(arch_ftrace_regs(fregs)->regs.regs[1]);
}

#define ftrace_graph_func ftrace_graph_func
void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
static inline void
__arch_ftrace_set_direct_caller(struct pt_regs *regs, unsigned long addr)
{
	regs->regs[13] = addr;	/* t1 */
}

#define arch_ftrace_set_direct_caller(fregs, addr) \
	__arch_ftrace_set_direct_caller(&arch_ftrace_regs(fregs)->regs, addr)
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */

#endif

#endif /* __ASSEMBLER__ */

#endif /* CONFIG_FUNCTION_TRACER */

#endif /* _ASM_LOONGARCH_FTRACE_H */
