/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ASM_ARC_SWITCH_TO_H
#define _ASM_ARC_SWITCH_TO_H

#ifndef __ASSEMBLY__

#include <linux/sched.h>
#include <asm/dsp-impl.h>
#include <asm/fpu.h>

struct task_struct *__switch_to(struct task_struct *p, struct task_struct *n);

#define switch_to(prev, next, last)	\
do {					\
	dsp_save_restore(prev, next);	\
	fpu_save_restore(prev, next);	\
	last = __switch_to(prev, next);\
	mb();				\
} while (0)

#endif

#endif
