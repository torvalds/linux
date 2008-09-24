#ifndef __ASM_SH_SYSTEM_64_H
#define __ASM_SH_SYSTEM_64_H

/*
 * include/asm-sh/system_64.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <asm/processor.h>

/*
 *	switch_to() should switch tasks to task nr n, first
 */
struct task_struct *sh64_switch_to(struct task_struct *prev,
				   struct thread_struct *prev_thread,
				   struct task_struct *next,
				   struct thread_struct *next_thread);

#define switch_to(prev,next,last)				\
do {								\
	if (last_task_used_math != next) {			\
		struct pt_regs *regs = next->thread.uregs;	\
		if (regs) regs->sr |= SR_FD;			\
	}							\
	last = sh64_switch_to(prev, &prev->thread, next,	\
			      &next->thread);			\
} while (0)

#define __uses_jump_to_uncached

#define jump_to_uncached()	do { } while (0)
#define back_to_cached()	do { } while (0)

#endif /* __ASM_SH_SYSTEM_64_H */
