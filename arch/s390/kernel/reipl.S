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

	GEN_BR_THUNK %r9

#
# Issue "store status" for the current CPU to its prefix page
# and call passed function afterwards
#
# r2 = Function to be called after store status
# r3 = Parameter for function
#
ENTRY(store_status)
	/* Save register one and load save area base */
	stg	%r1,__LC_SAVE_AREA_RESTART
	/* General purpose registers */
	lghi	%r1,__LC_GPREGS_SAVE_AREA
	stmg	%r0,%r15,0(%r1)
	mvc	8(8,%r1),__LC_SAVE_AREA_RESTART
	/* Control registers */
	lghi	%r1,__LC_CREGS_SAVE_AREA
	stctg	%c0,%c15,0(%r1)
	/* Access registers */
	lghi	%r1,__LC_AREGS_SAVE_AREA
	stam	%a0,%a15,0(%r1)
	/* Floating point registers */
	lghi	%r1,__LC_FPREGS_SAVE_AREA
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
	lghi	%r1,__LC_FP_CREG_SAVE_AREA
	stfpc	0(%r1)
	/* CPU timer */
	lghi	%r1,__LC_CPU_TIMER_SAVE_AREA
	stpt	0(%r1)
	/* Store prefix register */
	lghi	%r1,__LC_PREFIX_SAVE_AREA
	stpx	0(%r1)
	/* Clock comparator - seven bytes */
	lghi	%r1,__LC_CLOCK_COMP_SAVE_AREA
	larl	%r4,.Lclkcmp
	stckc	0(%r4)
	mvc	1(7,%r1),1(%r4)
	/* Program status word */
	lghi	%r1,__LC_PSW_SAVE_AREA
	epsw	%r4,%r5
	st	%r4,0(%r1)
	st	%r5,4(%r1)
	stg	%r2,8(%r1)
	lgr	%r9,%r2
	lgr	%r2,%r3
	BR_EX	%r9
ENDPROC(store_status)

	.section .bss
	.align	8
.Lclkcmp:	.quad	0x0000000000000000
	.previous
