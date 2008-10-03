#ifndef __ASM_SH_CURRENT_H
#define __ASM_SH_CURRENT_H

/*
 * Copyright (C) 1999 Niibe Yutaka
 *
 */

#include <linux/thread_info.h>

struct task_struct;

static __inline__ struct task_struct * get_current(void)
{
	return current_thread_info()->task;
}

#define current get_current()

#endif /* __ASM_SH_CURRENT_H */
