/*
 * Copyright 2004-2009 Analog Devices Inc.
 *               Tony Kou (tonyko@lineo.ca)
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _BLACKFIN_SWITCH_TO_H
#define _BLACKFIN_SWITCH_TO_H

#define prepare_to_switch()     do { } while(0)

/*
 * switch_to(n) should switch tasks to task ptr, first checking that
 * ptr isn't the current task, in which case it does nothing.
 */

#include <asm/l1layout.h>
#include <asm/mem_map.h>

asmlinkage struct task_struct *resume(struct task_struct *prev, struct task_struct *next);

#ifndef CONFIG_SMP
#define switch_to(prev,next,last) \
do {    \
	memcpy (&task_thread_info(prev)->l1_task_info, L1_SCRATCH_TASK_INFO, \
		sizeof *L1_SCRATCH_TASK_INFO); \
	memcpy (L1_SCRATCH_TASK_INFO, &task_thread_info(next)->l1_task_info, \
		sizeof *L1_SCRATCH_TASK_INFO); \
	(last) = resume (prev, next);   \
} while (0)
#else
#define switch_to(prev, next, last) \
do {    \
	(last) = resume(prev, next);   \
} while (0)
#endif

#endif /* _BLACKFIN_SWITCH_TO_H */
