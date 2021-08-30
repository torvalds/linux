/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Code that needs to run below 2 GB.
 *
 * Copyright IBM Corp. 2019
 */

#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/sigp.h>

	.section .dma.text,"ax"
/*
 * Simplified version of expoline thunk. The normal thunks can not be used here,
 * because they might be more than 2 GB away, and not reachable by the relative
 * branch. No comdat, exrl, etc. optimizations used here, because it only
 * affects a few functions that are not performance-relevant.
 */
	.macro BR_EX_DMA_r14
	larl	%r1,0f
	ex	0,0(%r1)
	j	.
0:	br	%r14
	.endm

/*
 * int _diag14_dma(unsigned long rx, unsigned long ry1, unsigned long subcode)
 */
ENTRY(_diag14_dma)
	lgr	%r1,%r2
	lgr	%r2,%r3
	lgr	%r3,%r4
	lhi	%r5,-EIO
	sam31
	diag	%r1,%r2,0x14
.Ldiag14_ex:
	ipm	%r5
	srl	%r5,28
.Ldiag14_fault:
	sam64
	lgfr	%r2,%r5
	BR_EX_DMA_r14
	EX_TABLE_DMA(.Ldiag14_ex, .Ldiag14_fault)
ENDPROC(_diag14_dma)

/*
 * int _diag210_dma(struct diag210 *addr)
 */
ENTRY(_diag210_dma)
	lgr	%r1,%r2
	lhi	%r2,-1
	sam31
	diag	%r1,%r0,0x210
.Ldiag210_ex:
	ipm	%r2
	srl	%r2,28
.Ldiag210_fault:
	sam64
	lgfr	%r2,%r2
	BR_EX_DMA_r14
	EX_TABLE_DMA(.Ldiag210_ex, .Ldiag210_fault)
ENDPROC(_diag210_dma)

/*
 * int _diag26c_dma(void *req, void *resp, enum diag26c_sc subcode)
 */
ENTRY(_diag26c_dma)
	lghi	%r5,-EOPNOTSUPP
	sam31
	diag	%r2,%r4,0x26c
.Ldiag26c_ex:
	sam64
	lgfr	%r2,%r5
	BR_EX_DMA_r14
	EX_TABLE_DMA(.Ldiag26c_ex, .Ldiag26c_ex)
ENDPROC(_diag26c_dma)

/*
 * void _diag0c_dma(struct hypfs_diag0c_entry *entry)
 */
ENTRY(_diag0c_dma)
	sam31
	diag	%r2,%r2,0x0c
	sam64
	BR_EX_DMA_r14
ENDPROC(_diag0c_dma)

/*
 * void _diag308_reset_dma(void)
 *
 * Calls diag 308 subcode 1 and continues execution
 */
ENTRY(_diag308_reset_dma)
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
	larl	%r4,restart_part2	# Setup restart PSW at absolute 0
	larl	%r3,.Lrestart_diag308_psw
	og	%r4,0(%r3)		# Save PSW
	lghi	%r3,0
	sturg	%r4,%r3			# Use sturg, because of large pages
	lghi	%r1,1
	lghi	%r0,0
	diag	%r0,%r1,0x308
restart_part2:
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
	BR_EX_DMA_r14
ENDPROC(_diag308_reset_dma)

	.section .dma.data,"aw",@progbits
.align	8
.Lrestart_diag308_psw:
	.long	0x00080000,0x80000000

.align 8
.Lcontinue_psw:
	.quad	0,.Lcontinue

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
