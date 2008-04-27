/*
 * sie64a.S - low level sie call
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/errno.h>
#include <asm/asm-offsets.h>

SP_R5 =	5 * 8	# offset into stackframe
SP_R6 =	6 * 8

/*
 * sie64a calling convention:
 * %r2 pointer to sie control block
 * %r3 guest register save area
 */
	.globl	sie64a
sie64a:
	lgr	%r5,%r3
	stmg	%r5,%r14,SP_R5(%r15)	# save register on entry
	lgr	%r14,%r2		# pointer to sie control block
	lmg	%r0,%r13,0(%r3)		# load guest gprs 0-13
sie_inst:
	sie	0(%r14)
	lg	%r14,SP_R5(%r15)
	stmg	%r0,%r13,0(%r14)	# save guest gprs 0-13
	lghi	%r2,0
	lmg	%r6,%r14,SP_R6(%r15)
	br	%r14

sie_err:
	lg	%r14,SP_R5(%r15)
	stmg	%r0,%r13,0(%r14)	# save guest gprs 0-13
	lghi	%r2,-EFAULT
	lmg	%r6,%r14,SP_R6(%r15)
	br	%r14

	.section __ex_table,"a"
	.quad	sie_inst,sie_err
	.previous
