/*
 * i386 semaphore implementation.
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Portions Copyright 1999 Red Hat, Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * rw semaphores implemented November 1999 by Benjamin LaHaise <bcrl@kvack.org>
 */
#include <linux/config.h>
#include <asm/semaphore.h>

/*
 * The semaphore operations have a special calling sequence that
 * allow us to do a simpler in-line version of them. These routines
 * need to convert that sequence back into the C sequence when
 * there is contention on the semaphore.
 *
 * %eax contains the semaphore pointer on entry. Save the C-clobbered
 * registers (%eax, %edx and %ecx) except %eax whish is either a return
 * value or just clobbered..
 */
asm(
".section .sched.text\n"
".align 4\n"
".globl __down_failed\n"
"__down_failed:\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"pushl %ebp\n\t"
	"movl  %esp,%ebp\n\t"
#endif
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __down\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"movl %ebp,%esp\n\t"
	"popl %ebp\n\t"
#endif
	"ret"
);

asm(
".section .sched.text\n"
".align 4\n"
".globl __down_failed_interruptible\n"
"__down_failed_interruptible:\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"pushl %ebp\n\t"
	"movl  %esp,%ebp\n\t"
#endif
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __down_interruptible\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"movl %ebp,%esp\n\t"
	"popl %ebp\n\t"
#endif
	"ret"
);

asm(
".section .sched.text\n"
".align 4\n"
".globl __down_failed_trylock\n"
"__down_failed_trylock:\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"pushl %ebp\n\t"
	"movl  %esp,%ebp\n\t"
#endif
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __down_trylock\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"movl %ebp,%esp\n\t"
	"popl %ebp\n\t"
#endif
	"ret"
);

asm(
".section .sched.text\n"
".align 4\n"
".globl __up_wakeup\n"
"__up_wakeup:\n\t"
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __up\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
	"ret"
);

/*
 * rw spinlock fallbacks
 */
#if defined(CONFIG_SMP)
asm(
".section .sched.text\n"
".align	4\n"
".globl	__write_lock_failed\n"
"__write_lock_failed:\n\t"
	LOCK_PREFIX "addl	$" RW_LOCK_BIAS_STR ",(%eax)\n"
"1:	rep; nop\n\t"
	"cmpl	$" RW_LOCK_BIAS_STR ",(%eax)\n\t"
	"jne	1b\n\t"
	LOCK_PREFIX "subl	$" RW_LOCK_BIAS_STR ",(%eax)\n\t"
	"jnz	__write_lock_failed\n\t"
	"ret"
);

asm(
".section .sched.text\n"
".align	4\n"
".globl	__read_lock_failed\n"
"__read_lock_failed:\n\t"
	LOCK_PREFIX "incl	(%eax)\n"
"1:	rep; nop\n\t"
	"cmpl	$1,(%eax)\n\t"
	"js	1b\n\t"
	LOCK_PREFIX "decl	(%eax)\n\t"
	"js	__read_lock_failed\n\t"
	"ret"
);
#endif
