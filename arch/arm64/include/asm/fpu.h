/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 SiFive
 */

#ifndef __ASM_FPU_H
#define __ASM_FPU_H

#include <linux/preempt.h>
#include <asm/neon.h>

#define kernel_fpu_available()	cpu_has_neon()

static inline void kernel_fpu_begin(void)
{
	BUG_ON(!in_task());
	preempt_disable();
	kernel_neon_begin(NULL);
}

static inline void kernel_fpu_end(void)
{
	kernel_neon_end(NULL);
	preempt_enable();
}

#endif /* ! __ASM_FPU_H */
