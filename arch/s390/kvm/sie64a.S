/*
 * sie64a.S - low level sie call
 *
 * Copyright IBM Corp. 2008,2010
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 *		 Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 */

#include <linux/errno.h>
#include <asm/asm-offsets.h>
#include <asm/setup.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>

_TIF_EXIT_SIE = (_TIF_SIGPENDING | _TIF_NEED_RESCHED | _TIF_MCCK_PENDING)

/*
 * offsets into stackframe
 * SP_	= offsets into stack sie64 is called with
 * SPI_ = offsets into irq stack
 */
SP_GREGS = __SF_EMPTY
SP_HOOK  = __SF_EMPTY+8
SP_GPP	 = __SF_EMPTY+16
SPI_PSW  = STACK_FRAME_OVERHEAD + __PT_PSW


	.macro SPP newpp
	tm	__LC_MACHINE_FLAGS+6,0x20	# MACHINE_FLAG_SPP
	jz	0f
	.insn	s,0xb2800000,\newpp
0:
	.endm

sie_irq_handler:
	SPP	__LC_CMF_HPP			# set host id
	larl	%r2,sie_inst
	clg	%r2,SPI_PSW+8(0,%r15)		# intercepted sie
	jne	1f
	xc	__LC_SIE_HOOK(8),__LC_SIE_HOOK
	lg	%r2,__LC_THREAD_INFO		# pointer thread_info struct
	tm	__TI_flags+7(%r2),_TIF_EXIT_SIE
	jz	0f
	larl	%r2,sie_exit			# work pending, leave sie
	stg	%r2,SPI_PSW+8(0,%r15)
	br	%r14
0:	larl	%r2,sie_reenter			# re-enter with guest id
	stg	%r2,SPI_PSW+8(0,%r15)
1:	br	%r14

/*
 * sie64a calling convention:
 * %r2 pointer to sie control block
 * %r3 guest register save area
 */
	.globl	sie64a
sie64a:
	stg	%r3,SP_GREGS(%r15)		# save guest register save area
	stmg	%r6,%r14,__SF_GPRS(%r15)	# save registers on entry
	lgr	%r14,%r2			# pointer to sie control block
	larl	%r5,sie_irq_handler
	stg	%r2,SP_GPP(%r15)
	stg	%r5,SP_HOOK(%r15)		# save hook target
	lmg	%r0,%r13,0(%r3)			# load guest gprs 0-13
sie_reenter:
	mvc	__LC_SIE_HOOK(8),SP_HOOK(%r15)
	SPP	SP_GPP(%r15)			# set guest id
sie_inst:
	sie	0(%r14)
	xc	__LC_SIE_HOOK(8),__LC_SIE_HOOK
	SPP	__LC_CMF_HPP			# set host id
sie_exit:
	lg	%r14,SP_GREGS(%r15)
	stmg	%r0,%r13,0(%r14)		# save guest gprs 0-13
	lghi	%r2,0
	lmg	%r6,%r14,__SF_GPRS(%r15)
	br	%r14

sie_err:
	xc	__LC_SIE_HOOK(8),__LC_SIE_HOOK
	SPP	__LC_CMF_HPP			# set host id
	lg	%r14,SP_GREGS(%r15)
	stmg	%r0,%r13,0(%r14)		# save guest gprs 0-13
	lghi	%r2,-EFAULT
	lmg	%r6,%r14,__SF_GPRS(%r15)
	br	%r14

	.section __ex_table,"a"
	.quad	sie_inst,sie_err
	.previous
