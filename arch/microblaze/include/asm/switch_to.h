/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_SWITCH_TO_H
#define _ASM_MICROBLAZE_SWITCH_TO_H

struct task_struct;
struct thread_info;

extern struct task_struct *_switch_to(struct thread_info *prev,
					struct thread_info *next);

#define switch_to(prev, next, last)					\
	do {								\
		(last) = _switch_to(task_thread_info(prev),		\
					task_thread_info(next));	\
	} while (0)

#endif /* _ASM_MICROBLAZE_SWITCH_TO_H */
