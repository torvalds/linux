/*
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_NIOS2_SWITCH_TO_H
#define _ASM_NIOS2_SWITCH_TO_H

/*
 * switch_to(n) should switch tasks to task ptr, first checking that
 * ptr isn't the current task, in which case it does nothing.  This
 * also clears the TS-flag if the task we switched to has used the
 * math co-processor latest.
 */
#define switch_to(prev, next, last)			\
{							\
	void *_last;					\
	__asm__ __volatile__ (				\
		"mov	r4, %1\n"			\
		"mov	r5, %2\n"			\
		"call	resume\n"			\
		"mov	%0,r4\n"			\
		: "=r" (_last)				\
		: "r" (prev), "r" (next)		\
		: "r4", "r5", "r7", "r8", "ra");	\
	(last) = _last;					\
}

#endif /* _ASM_NIOS2_SWITCH_TO_H */
