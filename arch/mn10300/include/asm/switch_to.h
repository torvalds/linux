/* MN10300 task switching definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_SWITCH_TO_H
#define _ASM_SWITCH_TO_H

#include <asm/barrier.h>

struct task_struct;
struct thread_struct;

#if !defined(CONFIG_LAZY_SAVE_FPU)
struct fpu_state_struct;
extern asmlinkage void fpu_save(struct fpu_state_struct *);
#define switch_fpu(prev, next)						\
	do {								\
		if ((prev)->thread.fpu_flags & THREAD_HAS_FPU) {	\
			(prev)->thread.fpu_flags &= ~THREAD_HAS_FPU;	\
			(prev)->thread.uregs->epsw &= ~EPSW_FE;		\
			fpu_save(&(prev)->thread.fpu_state);		\
		}							\
	} while (0)
#else
#define switch_fpu(prev, next) do {} while (0)
#endif

/* context switching is now performed out-of-line in switch_to.S */
extern asmlinkage
struct task_struct *__switch_to(struct thread_struct *prev,
				struct thread_struct *next,
				struct task_struct *prev_task);

#define switch_to(prev, next, last)					\
do {									\
	switch_fpu(prev, next);						\
	current->thread.wchan = (u_long) __builtin_return_address(0);	\
	(last) = __switch_to(&(prev)->thread, &(next)->thread, (prev));	\
	mb();								\
	current->thread.wchan = 0;					\
} while (0)

#endif /* _ASM_SWITCH_TO_H */
