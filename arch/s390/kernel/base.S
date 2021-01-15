/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  arch/s390/kernel/base.S
 *
 *    Copyright IBM Corp. 2006, 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 *		 Michael Holzheu <holzheu@de.ibm.com>
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/nospec-insn.h>
#include <asm/ptrace.h>

	GEN_BR_THUNK %r9
	GEN_BR_THUNK %r14

ENTRY(s390_base_pgm_handler)
	stmg	%r0,%r15,__LC_SAVE_AREA_SYNC
	basr	%r13,0
0:	aghi	%r15,-STACK_FRAME_OVERHEAD
	larl	%r1,s390_base_pgm_handler_fn
	lg	%r9,0(%r1)
	ltgr	%r9,%r9
	jz	1f
	BASR_EX	%r14,%r9
	lmg	%r0,%r15,__LC_SAVE_AREA_SYNC
	lpswe	__LC_PGM_OLD_PSW
1:	lpswe	disabled_wait_psw-0b(%r13)
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
