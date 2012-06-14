/* FR-V CPU basic task switching
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_SWITCH_TO_H
#define _ASM_SWITCH_TO_H

#include <linux/thread_info.h>

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.
 * The `mb' is to tell GCC not to cache `current' across this call.
 */
extern asmlinkage
struct task_struct *__switch_to(struct thread_struct *prev_thread,
				struct thread_struct *next_thread,
				struct task_struct *prev);

#define switch_to(prev, next, last)					\
do {									\
	(prev)->thread.sched_lr =					\
		(unsigned long) __builtin_return_address(0);		\
	(last) = __switch_to(&(prev)->thread, &(next)->thread, (prev));	\
	mb();								\
} while(0)

#endif /* _ASM_SWITCH_TO_H */
