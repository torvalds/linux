/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_LOONGARCH_FTRACE_H
#define _ASM_LOONGARCH_FTRACE_H

#ifdef CONFIG_FUNCTION_TRACER

#define MCOUNT_INSN_SIZE 4		/* sizeof mcount call */

#ifndef __ASSEMBLY__
#define mcount _mcount
extern void _mcount(void);
extern void prepare_ftrace_return(unsigned long self_addr, unsigned long callsite_sp, unsigned long old);
#endif /* __ASSEMBLY__ */

#endif /* CONFIG_FUNCTION_TRACER */

#endif /* _ASM_LOONGARCH_FTRACE_H */
