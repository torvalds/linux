/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 SiFive
 */

#ifndef _ASM_POWERPC_FPU_H
#define _ASM_POWERPC_FPU_H

#include <linux/preempt.h>

#include <asm/cpu_has_feature.h>
#include <asm/switch_to.h>

#define kernel_fpu_available()	(!cpu_has_feature(CPU_FTR_FPU_UNAVAILABLE))

static inline void kernel_fpu_begin(void)
{
	preempt_disable();
	enable_kernel_fp();
}

static inline void kernel_fpu_end(void)
{
	disable_kernel_fp();
	preempt_enable();
}

#endif /* ! _ASM_POWERPC_FPU_H */
