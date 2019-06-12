/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_CURRENT_H
#define _ASM_POWERPC_CURRENT_H
#ifdef __KERNEL__

/*
 */

struct task_struct;

#ifdef __powerpc64__
#include <linux/stddef.h>
#include <asm/paca.h>

static inline struct task_struct *get_current(void)
{
	struct task_struct *task;

	/* get_current can be cached by the compiler, so no volatile */
	asm ("ld %0,%1(13)"
	: "=r" (task)
	: "i" (offsetof(struct paca_struct, __current)));

	return task;
}
#define current	get_current()

#else

/*
 * We keep `current' in r2 for speed.
 */
register struct task_struct *current asm ("r2");

#endif

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_CURRENT_H */
