/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_LOONGARCH_FTRACE_H
#define _ASM_LOONGARCH_FTRACE_H

#ifdef CONFIG_FUNCTION_TRACER

#define MCOUNT_INSN_SIZE 4		/* sizeof mcount call */

#ifndef __ASSEMBLY__

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

#endif /* __ASSEMBLY__ */

#endif /* CONFIG_FUNCTION_TRACER */

#endif /* _ASM_LOONGARCH_FTRACE_H */
