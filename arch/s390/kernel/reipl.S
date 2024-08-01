/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp 2000, 2011
 *    Author(s): Holger Smolinski <Holger.Smolinski@de.ibm.com>,
 *		 Denis Joseph Barrow,
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/nospec-insn.h>
#include <asm/sigp.h>
#include <asm/lowcore.h>

	GEN_BR_THUNK %r9

#
# Issue "store status" for the current CPU to its prefix page
# and call passed function afterwards
#
# r2 = Function to be called after store status
# r3 = Parameter for function
#
SYM_CODE_START(store_status)
	STMG_LC	%r0,%r15,__LC_GPREGS_SAVE_AREA
	/* General purpose registers */
	GET_LC	%r13
	/* Control registers */
	stctg	%c0,%c15,__LC_CREGS_SAVE_AREA(%r13)
	/* Access registers */
	stamy	%a0,%a15,__LC_AREGS_SAVE_AREA(%r13)
	/* Floating point registers */
	lay	%r1,__LC_FPREGS_SAVE_AREA(%r13)
	std	%f0, 0x00(%r1)
	std	%f1, 0x08(%r1)
	std	%f2, 0x10(%r1)
	std	%f3, 0x18(%r1)
	std	%f4, 0x20(%r1)
	std	%f5, 0x28(%r1)
	std	%f6, 0x30(%r1)
	std	%f7, 0x38(%r1)
	std	%f8, 0x40(%r1)
	std	%f9, 0x48(%r1)
	std	%f10,0x50(%r1)
	std	%f11,0x58(%r1)
	std	%f12,0x60(%r1)
	std	%f13,0x68(%r1)
	std	%f14,0x70(%r1)
	std	%f15,0x78(%r1)
	/* Floating point control register */
	lay	%r1,__LC_FP_CREG_SAVE_AREA(%r13)
	stfpc	0(%r1)
	/* CPU timer */
	lay	%r1,__LC_CPU_TIMER_SAVE_AREA(%r13)
	stpt	0(%r1)
	/* Store prefix register */
	lay	%r1,__LC_PREFIX_SAVE_AREA(%r13)
	stpx	0(%r1)
	/* Clock comparator - seven bytes */
	larl	%r4,clkcmp
	stckc	0(%r4)
	lay	%r1,__LC_CLOCK_COMP_SAVE_AREA(%r13)
	mvc	1(7,%r1),1(%r4)
	/* Program status word */
	lay	%r1,__LC_PSW_SAVE_AREA(%r13)
	epsw	%r4,%r5
	st	%r4,0(%r1)
	st	%r5,4(%r1)
	stg	%r2,8(%r1)
	lgr	%r9,%r2
	lgr	%r2,%r3
	BR_EX	%r9
SYM_CODE_END(store_status)

	.section .bss
	.balign	8
SYM_DATA_LOCAL(clkcmp,	.quad 0x0000000000000000)
	.previous
