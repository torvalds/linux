/*
 * Based on arm/arm64/include/asm/current.h
 *
 * Copyright (C) 2016 ARM
 * Copyright (C) 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */


#ifndef __ASM_CURRENT_H
#define __ASM_CURRENT_H

#include <linux/bug.h>
#include <linux/compiler.h>

#ifndef __ASSEMBLY__

struct task_struct;

/*
 * This only works because "struct thread_info" is at offset 0 from "struct
 * task_struct".  This constraint seems to be necessary on other architectures
 * as well, but __switch_to enforces it.  We can't check TASK_TI here because
 * <asm/asm-offsets.h> includes this, and I can't get the definition of "struct
 * task_struct" here due to some header ordering problems.
 */
static __always_inline struct task_struct *get_current(void)
{
	register struct task_struct *tp __asm__("tp");
	return tp;
}

#define current get_current()

#endif /* __ASSEMBLY__ */

#endif /* __ASM_CURRENT_H */
