/*
 *  arch/s390/kernel/reset.S
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 *		 Michael Holzheu <holzheu@de.ibm.com>
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

	.globl	reset_pgm_handler
reset_pgm_handler:
	stmg	%r0,%r15,__LC_SAVE_AREA
	basr	%r13,0
0:	lg	%r15,__LC_PANIC_STACK	# load panic stack
	aghi	%r15,-STACK_FRAME_OVERHEAD
	lg	%r1,s390_reset_pgm_handler-0b(%r13)
	ltgr	%r1,%r1
	jz	1f
	basr	%r14,%r1
	lmg	%r0,%r15,__LC_SAVE_AREA
	lpswe	__LC_PGM_OLD_PSW
1:	lpswe	disabled_wait_psw-0b(%r13)
	.globl s390_reset_pgm_handler
s390_reset_pgm_handler:
	.quad	0
	.align	8
disabled_wait_psw:
	.quad	0x0002000180000000,0x0000000000000000 + reset_pgm_handler

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

	.globl	reset_pgm_handler
reset_pgm_handler:
	stm	%r0,%r15,__LC_SAVE_AREA
	basr	%r13,0
0:	l	%r15,__LC_PANIC_STACK	# load panic stack
	ahi	%r15,-STACK_FRAME_OVERHEAD
	l	%r1,s390_reset_pgm_handler-0b(%r13)
	ltr	%r1,%r1
	jz	1f
	basr	%r14,%r1
	lm	%r0,%r15,__LC_SAVE_AREA
	lpsw	__LC_PGM_OLD_PSW

1:	lpsw	disabled_wait_psw-0b(%r13)
	.globl	s390_reset_pgm_handler
s390_reset_pgm_handler:
	.long	0
disabled_wait_psw:
	.align 8
	.long	0x000a0000,0x00000000 + reset_pgm_handler

#endif /* CONFIG_64BIT */
