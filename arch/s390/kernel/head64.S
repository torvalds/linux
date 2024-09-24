/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2010
 *
 *   Author(s):	Hartmut Penner <hp@de.ibm.com>
 *		Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		Rob van der Heij <rvdhei@iae.nl>
 *
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/lowcore.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/ptrace.h>

__HEAD
SYM_CODE_START(startup_continue)
	larl	%r1,tod_clock_base
	GET_LC	%r2
	mvc	0(16,%r1),__LC_BOOT_CLOCK(%r2)
#
# Setup stack
#
	larl	%r14,init_task
	stg	%r14,__LC_CURRENT(%r2)
	larl	%r15,init_thread_union+STACK_INIT_OFFSET
	stg	%r15,__LC_KERNEL_STACK(%r2)
	brasl	%r14,sclp_early_adjust_va	# allow sclp_early_printk
	brasl	%r14,startup_init		# s390 specific early init
	brasl	%r14,start_kernel		# common init code
#
# We returned from start_kernel ?!? PANIK
#
	basr	%r13,0
	lpswe	dw_psw-.(%r13)		# load disabled wait psw
SYM_CODE_END(startup_continue)

	.balign	16
SYM_DATA_LOCAL(dw_psw,	.quad 0x0002000180000000,0x0000000000000000)
