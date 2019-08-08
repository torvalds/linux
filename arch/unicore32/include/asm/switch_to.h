/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Task switching for PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2012 GUAN Xue-tao
 */
#ifndef __UNICORE_SWITCH_TO_H__
#define __UNICORE_SWITCH_TO_H__

struct task_struct;
struct thread_info;

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.  schedule() itself
 * contains the memory barrier to tell GCC not to cache `current'.
 */
extern struct task_struct *__switch_to(struct task_struct *,
		struct thread_info *, struct thread_info *);

#define switch_to(prev, next, last)					\
	do {								\
		last = __switch_to(prev, task_thread_info(prev),	\
					task_thread_info(next));	\
	} while (0)

#endif /* __UNICORE_SWITCH_TO_H__ */
