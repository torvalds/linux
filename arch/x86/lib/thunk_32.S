/*
 * Trampoline to trace irqs off. (otherwise CALLER_ADDR1 might crash)
 * Copyright 2008 by Steven Rostedt, Red Hat, Inc
 *  (inspired by Andi Kleen's thunk_64.S)
 * Subject to the GNU public license, v.2. No warranty of any kind.
 */

	#include <linux/linkage.h>

#define ARCH_TRACE_IRQS_ON			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_on;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_TRACE_IRQS_OFF			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_off;		\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#ifdef CONFIG_TRACE_IRQFLAGS
	/* put return address in eax (arg1) */
	.macro thunk_ra name,func
	.globl \name
\name:
	pushl %eax
	pushl %ecx
	pushl %edx
	/* Place EIP in the arg1 */
	movl 3*4(%esp), %eax
	call \func
	popl %edx
	popl %ecx
	popl %eax
	ret
	.endm

	thunk_ra trace_hardirqs_on_thunk,trace_hardirqs_on_caller
	thunk_ra trace_hardirqs_off_thunk,trace_hardirqs_off_caller
#endif
