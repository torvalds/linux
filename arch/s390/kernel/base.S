/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  arch/s390/kernel/base.S
 *
 *    Copyright IBM Corp. 2006, 2007
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/nospec-insn.h>
#include <asm/ptrace.h>

	GEN_BR_THUNK %r9
	GEN_BR_THUNK %r14

__PT_R0 = __PT_GPRS
__PT_R8 = __PT_GPRS + 64

ENTRY(s390_base_pgm_handler)
	stmg	%r8,%r15,__LC_SAVE_AREA_SYNC
	aghi	%r15,-(STACK_FRAME_OVERHEAD+__PT_SIZE)
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_PSW(16,%r11),__LC_PGM_OLD_PSW
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_SYNC
	lgr	%r2,%r11
	larl	%r1,s390_base_pgm_handler_fn
	lg	%r9,0(%r1)
	ltgr	%r9,%r9
	jz	1f
	BASR_EX	%r14,%r9
	mvc	__LC_RETURN_PSW(16),STACK_FRAME_OVERHEAD+__PT_PSW(%r15)
	lmg	%r0,%r15,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	lpswe	__LC_RETURN_PSW
1:	larl	%r13,disabled_wait_psw
	lpswe	0(%r13)
ENDPROC(s390_base_pgm_handler)

	.align	8
disabled_wait_psw:
	.quad	0x0002000180000000,0x0000000000000000 + s390_base_pgm_handler

	.section .bss
	.align 8
	.globl s390_base_pgm_handler_fn
s390_base_pgm_handler_fn:
	.quad	0
	.previous
