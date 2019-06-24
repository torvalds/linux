/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/suspend.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */

#ifndef __UNICORE_SUSPEND_H__
#define __UNICORE_SUSPEND_H__

#ifndef __ASSEMBLY__

#include <asm/ptrace.h>

struct swsusp_arch_regs {
	struct cpu_context_save	cpu_context;	/* cpu context */
#ifdef CONFIG_UNICORE_FPU_F64
	struct fp_state		fpstate __attribute__((aligned(8)));
#endif
};
#endif

#endif /* __UNICORE_SUSPEND_H__ */

