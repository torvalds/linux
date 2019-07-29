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
#include <asm/sigp.h>

	GEN_BR_THUNK %r9
	GEN_BR_THUNK %r14

ENTRY(s390_base_mcck_handler)
	basr	%r13,0
0:	lg	%r15,__LC_NODAT_STACK	# load panic stack
	aghi	%r15,-STACK_FRAME_OVERHEAD
	larl	%r1,s390_base_mcck_handler_fn
	lg	%r9,0(%r1)
	ltgr	%r9,%r9
	jz	1f
	BASR_EX	%r14,%r9
1:	la	%r1,4095
	lmg	%r0,%r15,__LC_GPREGS_SAVE_AREA-4095(%r1)
	lpswe	__LC_MCK_OLD_PSW

	.section .bss
	.align 8
	.globl	s390_base_mcck_handler_fn
s390_base_mcck_handler_fn:
	.quad	0
	.previous

ENTRY(s390_base_ext_handler)
	stmg	%r0,%r15,__LC_SAVE_AREA_ASYNC
	basr	%r13,0
0:	aghi	%r15,-STACK_FRAME_OVERHEAD
	larl	%r1,s390_base_ext_handler_fn
	lg	%r9,0(%r1)
	ltgr	%r9,%r9
	jz	1f
	BASR_EX	%r14,%r9
1:	lmg	%r0,%r15,__LC_SAVE_AREA_ASYNC
	ni	__LC_EXT_OLD_PSW+1,0xfd	# clear wait state bit
	lpswe	__LC_EXT_OLD_PSW

	.section .bss
	.align 8
	.globl s390_base_ext_handler_fn
s390_base_ext_handler_fn:
	.quad	0
	.previous

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

	.align	8
disabled_wait_psw:
	.quad	0x0002000180000000,0x0000000000000000 + s390_base_pgm_handler

	.section .bss
	.align 8
	.globl s390_base_pgm_handler_fn
s390_base_pgm_handler_fn:
	.quad	0
	.previous

#
# Calls diag 308 subcode 1 and continues execution
#
ENTRY(diag308_reset)
	larl	%r4,.Lctlregs		# Save control registers
	stctg	%c0,%c15,0(%r4)
	lg	%r2,0(%r4)		# Disable lowcore protection
	nilh	%r2,0xefff
	larl	%r4,.Lctlreg0
	stg	%r2,0(%r4)
	lctlg	%c0,%c0,0(%r4)
	larl	%r4,.Lfpctl		# Floating point control register
	stfpc	0(%r4)
	larl	%r4,.Lprefix		# Save prefix register
	stpx	0(%r4)
	larl	%r4,.Lprefix_zero	# Set prefix register to 0
	spx	0(%r4)
	larl	%r4,.Lcontinue_psw	# Save PSW flags
	epsw	%r2,%r3
	stm	%r2,%r3,0(%r4)
	larl	%r4,.Lrestart_psw	# Setup restart PSW at absolute 0
	lghi	%r3,0
	lg	%r4,0(%r4)		# Save PSW
	sturg	%r4,%r3			# Use sturg, because of large pages
	lghi	%r1,1
	lghi	%r0,0
	diag	%r0,%r1,0x308
.Lrestart_part2:
	lhi	%r0,0			# Load r0 with zero
	lhi	%r1,2			# Use mode 2 = ESAME (dump)
	sigp	%r1,%r0,SIGP_SET_ARCHITECTURE	# Switch to ESAME mode
	sam64				# Switch to 64 bit addressing mode
	larl	%r4,.Lctlregs		# Restore control registers
	lctlg	%c0,%c15,0(%r4)
	larl	%r4,.Lfpctl		# Restore floating point ctl register
	lfpc	0(%r4)
	larl	%r4,.Lprefix		# Restore prefix register
	spx	0(%r4)
	larl	%r4,.Lcontinue_psw	# Restore PSW flags
	lpswe	0(%r4)
.Lcontinue:
	BR_EX	%r14
.align 16
.Lrestart_psw:
	.long	0x00080000,0x80000000 + .Lrestart_part2

	.section .data..nosave,"aw",@progbits
.align 8
.Lcontinue_psw:
	.quad	0,.Lcontinue
	.previous

	.section .bss
.align 8
.Lctlreg0:
	.quad	0
.Lctlregs:
	.rept	16
	.quad	0
	.endr
.Lfpctl:
	.long	0
.Lprefix:
	.long	0
.Lprefix_zero:
	.long	0
	.previous
