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

	.section .bss
	.align	8
.Lclkcmp:	.quad	0x0000000000000000
	.previous

#
# do_reipl_asm
# Parameter: r2 = schid of reipl device
#

ENTRY(do_reipl_asm)
		basr	%r13,0
.Lpg0:		lpswe	.Lnewpsw-.Lpg0(%r13)
.Lpg1:		lgr	%r3,%r2
		larl	%r2,.Lstatus
		brasl	%r14,store_status

.Lstatus:	lctlg	%c6,%c6,.Lall-.Lpg0(%r13)
		lgr	%r1,%r2
		mvc	__LC_PGM_NEW_PSW(16),.Lpcnew-.Lpg0(%r13)
		stsch	.Lschib-.Lpg0(%r13)
		oi	.Lschib+5-.Lpg0(%r13),0x84
.Lecs:		xi	.Lschib+27-.Lpg0(%r13),0x01
		msch	.Lschib-.Lpg0(%r13)
		lghi	%r0,5
.Lssch:		ssch	.Liplorb-.Lpg0(%r13)
		jz	.L001
		brct	%r0,.Lssch
		bas	%r14,.Ldisab-.Lpg0(%r13)
.L001:		mvc	__LC_IO_NEW_PSW(16),.Lionew-.Lpg0(%r13)
.Ltpi:		lpswe	.Lwaitpsw-.Lpg0(%r13)
.Lcont:		c	%r1,__LC_SUBCHANNEL_ID
		jnz	.Ltpi
		clc	__LC_IO_INT_PARM(4),.Liplorb-.Lpg0(%r13)
		jnz	.Ltpi
		tsch	.Liplirb-.Lpg0(%r13)
		tm	.Liplirb+9-.Lpg0(%r13),0xbf
		jz	.L002
		bas	%r14,.Ldisab-.Lpg0(%r13)
.L002:		tm	.Liplirb+8-.Lpg0(%r13),0xf3
		jz	.L003
		bas	%r14,.Ldisab-.Lpg0(%r13)
.L003:		st	%r1,__LC_SUBCHANNEL_ID
		lhi	%r1,0		 # mode 0 = esa
		slr	%r0,%r0		 # set cpuid to zero
		sigp	%r1,%r0,SIGP_SET_ARCHITECTURE # switch to esa mode
		lpsw	0
.Ldisab:	sll	%r14,1
		srl	%r14,1		 # need to kill hi bit to avoid specification exceptions.
		st	%r14,.Ldispsw+12-.Lpg0(%r13)
		lpswe	.Ldispsw-.Lpg0(%r13)
		.align	8
.Lall:		.quad	0x00000000ff000000
		.align	16
/*
 * These addresses have to be 31 bit otherwise
 * the sigp will throw a specifcation exception
 * when switching to ESA mode as bit 31 be set
 * in the ESA psw.
 * Bit 31 of the addresses has to be 0 for the
 * 31bit lpswe instruction a fact they appear to have
 * omitted from the pop.
 */
.Lnewpsw:	.quad	0x0000000080000000
		.quad	.Lpg1
.Lpcnew:	.quad	0x0000000080000000
		.quad	.Lecs
.Lionew:	.quad	0x0000000080000000
		.quad	.Lcont
.Lwaitpsw:	.quad	0x0202000080000000
		.quad	.Ltpi
.Ldispsw:	.quad	0x0002000080000000
		.quad	0x0000000000000000
.Liplccws:	.long	0x02000000,0x60000018
		.long	0x08000008,0x20000001
.Liplorb:	.long	0x0049504c,0x0040ff80
		.long	0x00000000+.Liplccws
.Lschib:	.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
.Liplirb:	.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
		.long	0x00000000,0x00000000
