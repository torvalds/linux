/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_SWITCH_TO_H
#define _ASM_ARC_SWITCH_TO_H

#ifndef __ASSEMBLY__

#include <linux/sched.h>

#ifdef CONFIG_ARC_FPU_SAVE_RESTORE

extern void fpu_save_restore(struct task_struct *p, struct task_struct *n);
#define ARC_FPU_PREV(p, n)	fpu_save_restore(p, n)
#define ARC_FPU_NEXT(t)

#else

#define ARC_FPU_PREV(p, n)
#define ARC_FPU_NEXT(n)

#endif /* !CONFIG_ARC_FPU_SAVE_RESTORE */

#ifdef CONFIG_ARC_PLAT_EZNPS
extern void dp_save_restore(struct task_struct *p, struct task_struct *n);
#define ARC_EZNPS_DP_PREV(p, n)      dp_save_restore(p, n)
#else
#define ARC_EZNPS_DP_PREV(p, n)

#endif /* !CONFIG_ARC_PLAT_EZNPS */

struct task_struct *__switch_to(struct task_struct *p, struct task_struct *n);

#define switch_to(prev, next, last)	\
do {					\
	ARC_EZNPS_DP_PREV(prev, next);	\
	ARC_FPU_PREV(prev, next);	\
	last = __switch_to(prev, next);\
	ARC_FPU_NEXT(next);		\
	mb();				\
} while (0)

#endif

#endif
