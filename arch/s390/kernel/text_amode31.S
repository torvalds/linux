/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Code that needs to run below 2 GB.
 *
 * Copyright IBM Corp. 2019
 */

#include <linux/linkage.h>
#include <asm/asm-extable.h>
#include <asm/errno.h>
#include <asm/sigp.h>

	.section .amode31.text,"ax"
/*
 * Simplified version of expoline thunk. The normal thunks can not be used here,
 * because they might be more than 2 GB away, and not reachable by the relative
 * branch. No comdat, exrl, etc. optimizations used here, because it only
 * affects a few functions that are not performance-relevant.
 */
	.macro BR_EX_AMODE31_r14
	larl	%r1,0f
	ex	0,0(%r1)
	j	.
0:	br	%r14
	.endm

/*
 * int _diag14_amode31(unsigned long rx, unsigned long ry1, unsigned long subcode)
 */
SYM_FUNC_START(_diag14_amode31)
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
	BR_EX_AMODE31_r14
	EX_TABLE_AMODE31(.Ldiag14_ex, .Ldiag14_fault)
SYM_FUNC_END(_diag14_amode31)

/*
 * int _diag210_amode31(struct diag210 *addr)
 */
SYM_FUNC_START(_diag210_amode31)
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
	BR_EX_AMODE31_r14
	EX_TABLE_AMODE31(.Ldiag210_ex, .Ldiag210_fault)
SYM_FUNC_END(_diag210_amode31)

/*
 * int diag8c(struct diag8c *addr, struct ccw_dev_id *devno, size_t len)
*/
SYM_FUNC_START(_diag8c_amode31)
	llgf	%r3,0(%r3)
	sam31
	diag	%r2,%r4,0x8c
.Ldiag8c_ex:
	sam64
	lgfr	%r2,%r3
	BR_EX_AMODE31_r14
	EX_TABLE_AMODE31(.Ldiag8c_ex, .Ldiag8c_ex)
SYM_FUNC_END(_diag8c_amode31)
/*
 * int _diag26c_amode31(void *req, void *resp, enum diag26c_sc subcode)
 */
SYM_FUNC_START(_diag26c_amode31)
	lghi	%r5,-EOPNOTSUPP
	sam31
	diag	%r2,%r4,0x26c
.Ldiag26c_ex:
	sam64
	lgfr	%r2,%r5
	BR_EX_AMODE31_r14
	EX_TABLE_AMODE31(.Ldiag26c_ex, .Ldiag26c_ex)
SYM_FUNC_END(_diag26c_amode31)

/*
 * void _diag0c_amode31(unsigned long rx)
 */
SYM_FUNC_START(_diag0c_amode31)
	sam31
	diag	%r2,%r2,0x0c
	sam64
	BR_EX_AMODE31_r14
SYM_FUNC_END(_diag0c_amode31)

/*
 * void _diag308_reset_amode31(void)
 *
 * Calls diag 308 subcode 1 and continues execution
 */
SYM_FUNC_START(_diag308_reset_amode31)
	larl	%r4,ctlregs		# Save control registers
	stctg	%c0,%c15,0(%r4)
	lg	%r2,0(%r4)		# Disable lowcore protection
	nilh	%r2,0xefff
	larl	%r4,ctlreg0
	stg	%r2,0(%r4)
	lctlg	%c0,%c0,0(%r4)
	larl	%r4,fpctl		# Floating point control register
	stfpc	0(%r4)
	larl	%r4,prefix		# Save prefix register
	stpx	0(%r4)
	larl	%r4,prefix_zero	# Set prefix register to 0
	spx	0(%r4)
	larl	%r4,continue_psw	# Save PSW flags
	epsw	%r2,%r3
	stm	%r2,%r3,0(%r4)
	larl	%r4,.Lrestart_part2	# Setup restart PSW at absolute 0
	larl	%r3,restart_diag308_psw
	og	%r4,0(%r3)		# Save PSW
	lghi	%r3,0
	sturg	%r4,%r3			# Use sturg, because of large pages
	lghi	%r1,1
	lghi	%r0,0
	diag	%r0,%r1,0x308
.Lrestart_part2:
	lhi	%r0,0			# Load r0 with zero
	lhi	%r1,2			# Use mode 2 = ESAME (dump)
	sigp	%r1,%r0,SIGP_SET_ARCHITECTURE	# Switch to ESAME mode
	sam64				# Switch to 64 bit addressing mode
	larl	%r4,ctlregs		# Restore control registers
	lctlg	%c0,%c15,0(%r4)
	larl	%r4,fpctl		# Restore floating point ctl register
	lfpc	0(%r4)
	larl	%r4,prefix		# Restore prefix register
	spx	0(%r4)
	larl	%r4,continue_psw	# Restore PSW flags
	larl	%r2,.Lcontinue
	stg	%r2,8(%r4)
	lpswe	0(%r4)
.Lcontinue:
	BR_EX_AMODE31_r14
SYM_FUNC_END(_diag308_reset_amode31)

	.section .amode31.data,"aw",@progbits
	.balign	8
SYM_DATA_LOCAL(restart_diag308_psw,	.long 0x00080000,0x80000000)
SYM_DATA_LOCAL(continue_psw,		.quad 0,0)
SYM_DATA_LOCAL(ctlreg0,			.quad 0)
SYM_DATA_LOCAL(ctlregs,			.fill 16,8,0)
SYM_DATA_LOCAL(fpctl,			.long 0)
SYM_DATA_LOCAL(prefix,			.long 0)
SYM_DATA_LOCAL(prefix_zero,		.long 0)
