/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001  Hiroyuki Kondo, Hirokazu Takata, and Hitoshi Yamamoto
 * Copyright (C) 2004, 2006  Hirokazu Takata <takata at linux-m32r.org>
 */
#ifndef _ASM_M32R_SWITCH_TO_H
#define _ASM_M32R_SWITCH_TO_H

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.
 *
 * `next' and `prev' should be struct task_struct, but it isn't always defined
 */

#if defined(CONFIG_FRAME_POINTER) || \
	!defined(CONFIG_SCHED_OMIT_FRAME_POINTER)
#define M32R_PUSH_FP "	push fp\n"
#define M32R_POP_FP  "	pop  fp\n"
#else
#define M32R_PUSH_FP ""
#define M32R_POP_FP  ""
#endif

#define switch_to(prev, next, last)  do { \
	__asm__ __volatile__ ( \
		"	seth	lr, #high(1f)				\n" \
		"	or3	lr, lr, #low(1f)			\n" \
		"	st	lr, @%4  ; store old LR			\n" \
		"	ld	lr, @%5  ; load new LR			\n" \
			M32R_PUSH_FP \
		"	st	sp, @%2  ; store old SP			\n" \
		"	ld	sp, @%3  ; load new SP			\n" \
		"	push	%1  ; store `prev' on new stack		\n" \
		"	jmp	lr					\n" \
		"	.fillinsn					\n" \
		"1:							\n" \
		"	pop	%0  ; restore `__last' from new stack	\n" \
			M32R_POP_FP \
		: "=r" (last) \
		: "0" (prev), \
		  "r" (&(prev->thread.sp)), "r" (&(next->thread.sp)), \
		  "r" (&(prev->thread.lr)), "r" (&(next->thread.lr)) \
		: "memory", "lr" \
	); \
} while(0)

#endif /* _ASM_M32R_SWITCH_TO_H */
