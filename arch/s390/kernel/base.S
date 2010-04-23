/*
 *  arch/s390/kernel/base.S
 *
 *    Copyright IBM Corp. 2006,2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 *		 Michael Holzheu <holzheu@de.ibm.com>
 */

#include <asm/asm-offsets.h>
#include <asm/ptrace.h>

#ifdef CONFIG_64BIT

	.globl	s390_base_mcck_handler
s390_base_mcck_handler:
	basr	%r13,0
0:	lg	%r15,__LC_PANIC_STACK	# load panic stack
	aghi	%r15,-STACK_FRAME_OVERHEAD
	larl	%r1,s390_base_mcck_handler_fn
	lg	%r1,0(%r1)
	ltgr	%r1,%r1
	jz	1f
	basr	%r14,%r1
1:	la	%r1,4095
	lmg	%r0,%r15,__LC_GPREGS_SAVE_AREA-4095(%r1)
	lpswe	__LC_MCK_OLD_PSW

	.section .bss
	.globl	s390_base_mcck_handler_fn
s390_base_mcck_handler_fn:
	.quad	0
	.previous

	.globl	s390_base_ext_handler
s390_base_ext_handler:
	stmg	%r0,%r15,__LC_SAVE_AREA
	basr	%r13,0
0:	aghi	%r15,-STACK_FRAME_OVERHEAD
	larl	%r1,s390_base_ext_handler_fn
	lg	%r1,0(%r1)
	ltgr	%r1,%r1
	jz	1f
	basr	%r14,%r1
1:	lmg	%r0,%r15,__LC_SAVE_AREA
	ni	__LC_EXT_OLD_PSW+1,0xfd	# clear wait state bit
	lpswe	__LC_EXT_OLD_PSW

	.section .bss
	.globl s390_base_ext_handler_fn
s390_base_ext_handler_fn:
	.quad	0
	.previous

	.globl	s390_base_pgm_handler
s390_base_pgm_handler:
	stmg	%r0,%r15,__LC_SAVE_AREA
	basr	%r13,0
0:	aghi	%r15,-STACK_FRAME_OVERHEAD
	larl	%r1,s390_base_pgm_handler_fn
	lg	%r1,0(%r1)
	ltgr	%r1,%r1
	jz	1f
	basr	%r14,%r1
	lmg	%r0,%r15,__LC_SAVE_AREA
	lpswe	__LC_PGM_OLD_PSW
1:	lpswe	disabled_wait_psw-0b(%r13)

	.align	8
disabled_wait_psw:
	.quad	0x0002000180000000,0x0000000000000000 + s390_base_pgm_handler

	.section .bss
	.globl s390_base_pgm_handler_fn
s390_base_pgm_handler_fn:
	.quad	0
	.previous

#else /* CONFIG_64BIT */

	.globl	s390_base_mcck_handler
s390_base_mcck_handler:
	basr	%r13,0
0:	l	%r15,__LC_PANIC_STACK	# load panic stack
	ahi	%r15,-STACK_FRAME_OVERHEAD
	l	%r1,2f-0b(%r13)
	l	%r1,0(%r1)
	ltr	%r1,%r1
	jz	1f
	basr	%r14,%r1
1:	lm	%r0,%r15,__LC_GPREGS_SAVE_AREA
	lpsw	__LC_MCK_OLD_PSW

2:	.long	s390_base_mcck_handler_fn

	.section .bss
	.globl	s390_base_mcck_handler_fn
s390_base_mcck_handler_fn:
	.long	0
	.previous

	.globl	s390_base_ext_handler
s390_base_ext_handler:
	stm	%r0,%r15,__LC_SAVE_AREA
	basr	%r13,0
0:	ahi	%r15,-STACK_FRAME_OVERHEAD
	l	%r1,2f-0b(%r13)
	l	%r1,0(%r1)
	ltr	%r1,%r1
	jz	1f
	basr	%r14,%r1
1:	lm	%r0,%r15,__LC_SAVE_AREA
	ni	__LC_EXT_OLD_PSW+1,0xfd	# clear wait state bit
	lpsw	__LC_EXT_OLD_PSW

2:	.long	s390_base_ext_handler_fn

	.section .bss
	.globl	s390_base_ext_handler_fn
s390_base_ext_handler_fn:
	.long	0
	.previous

	.globl	s390_base_pgm_handler
s390_base_pgm_handler:
	stm	%r0,%r15,__LC_SAVE_AREA
	basr	%r13,0
0:	ahi	%r15,-STACK_FRAME_OVERHEAD
	l	%r1,2f-0b(%r13)
	l	%r1,0(%r1)
	ltr	%r1,%r1
	jz	1f
	basr	%r14,%r1
	lm	%r0,%r15,__LC_SAVE_AREA
	lpsw	__LC_PGM_OLD_PSW

1:	lpsw	disabled_wait_psw-0b(%r13)

2:	.long	s390_base_pgm_handler_fn

disabled_wait_psw:
	.align	8
	.long	0x000a0000,0x00000000 + s390_base_pgm_handler

	.section .bss
	.globl	s390_base_pgm_handler_fn
s390_base_pgm_handler_fn:
	.long	0
	.previous

#endif /* CONFIG_64BIT */
