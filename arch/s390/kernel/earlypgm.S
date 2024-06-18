/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 2006, 2007
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>

SYM_CODE_START(early_pgm_check_handler)
	stmg	%r8,%r15,__LC_SAVE_AREA_SYNC
	aghi	%r15,-(STACK_FRAME_OVERHEAD+__PT_SIZE)
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_PSW(16,%r11),__LC_PGM_OLD_PSW
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_SYNC
	lgr	%r2,%r11
	brasl	%r14,__do_early_pgm_check
	mvc	__LC_RETURN_PSW(16),STACK_FRAME_OVERHEAD+__PT_PSW(%r15)
	lmg	%r0,%r15,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	lpswe	__LC_RETURN_PSW
SYM_CODE_END(early_pgm_check_handler)
