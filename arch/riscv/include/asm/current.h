/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arm/arm64/include/asm/current.h
 *
 * Copyright (C) 2016 ARM
 * Copyright (C) 2017 SiFive
 */


#ifndef _ASM_RISCV_CURRENT_H
#define _ASM_RISCV_CURRENT_H

#include <linux/bug.h>
#include <linux/compiler.h>

#ifndef __ASSEMBLER__

struct task_struct;

register struct task_struct *riscv_current_is_tp __asm__("tp");

/*
 * This only works because "struct thread_info" is at offset 0 from "struct
 * task_struct".  This constraint seems to be necessary on other architectures
 * as well, but __switch_to enforces it.  We can't check TASK_TI here because
 * <asm/asm-offsets.h> includes this, and I can't get the definition of "struct
 * task_struct" here due to some header ordering problems.
 */
static __always_inline struct task_struct *get_current(void)
{
	return riscv_current_is_tp;
}

#define current get_current()

register unsigned long current_stack_pointer __asm__("sp");

#endif /* __ASSEMBLER__ */

#endif /* _ASM_RISCV_CURRENT_H */
