/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_SWITCH_TO_H
#define _ASM_C6X_SWITCH_TO_H

#include <linux/linkage.h>

#define prepare_to_switch()    do { } while (0)

struct task_struct;
struct thread_struct;
asmlinkage void *__switch_to(struct thread_struct *prev,
			     struct thread_struct *next,
			     struct task_struct *tsk);

#define switch_to(prev, next, last)				\
	do {							\
		current->thread.wchan = (u_long) __builtin_return_address(0); \
		(last) = __switch_to(&(prev)->thread,		\
				     &(next)->thread, (prev));	\
		mb();						\
		current->thread.wchan = 0;			\
	} while (0)

#endif /* _ASM_C6X_SWITCH_TO_H */
