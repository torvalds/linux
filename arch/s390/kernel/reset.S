/*
 *  arch/s390/kernel/reset.S
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <asm/ptrace.h>
#include <asm/lowcore.h>

#ifdef CONFIG_64BIT

	.globl	reset_mcck_handler
reset_mcck_handler:
	basr	%r13,0
0:	lg	%r15,__LC_PANIC_STACK	# load panic stack
	aghi	%r15,-STACK_FRAME_OVERHEAD
	lg	%r1,s390_reset_mcck_handler-0b(%r13)
	ltgr	%r1,%r1
	jz	1f
	basr	%r14,%r1
1:	la	%r1,4095
	lmg	%r0,%r15,__LC_GPREGS_SAVE_AREA-4095(%r1)
	lpswe	__LC_MCK_OLD_PSW

	.globl	s390_reset_mcck_handler
s390_reset_mcck_handler:
	.quad	0

#else /* CONFIG_64BIT */

	.globl	reset_mcck_handler
reset_mcck_handler:
	basr	%r13,0
0:	l	%r15,__LC_PANIC_STACK	# load panic stack
	ahi	%r15,-STACK_FRAME_OVERHEAD
	l	%r1,s390_reset_mcck_handler-0b(%r13)
	ltr	%r1,%r1
	jz	1f
	basr	%r14,%r1
1:	lm	%r0,%r15,__LC_GPREGS_SAVE_AREA
	lpsw	__LC_MCK_OLD_PSW

	.globl	s390_reset_mcck_handler
s390_reset_mcck_handler:
	.long	0

#endif /* CONFIG_64BIT */
