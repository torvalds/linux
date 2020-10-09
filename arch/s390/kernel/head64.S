/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2010
 *
 *   Author(s):	Hartmut Penner <hp@de.ibm.com>
 *		Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		Rob van der Heij <rvdhei@iae.nl>
 *		Heiko Carstens <heiko.carstens@de.ibm.com>
 *
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/ptrace.h>

__HEAD
ENTRY(startup_continue)
	larl	%r1,tod_clock_base
	mvc	0(16,%r1),__LC_BOOT_CLOCK
	larl	%r13,.LPG1		# get base
#
# Setup stack
#
	larl	%r14,init_task
	stg	%r14,__LC_CURRENT
	larl	%r15,init_thread_union+THREAD_SIZE-STACK_FRAME_OVERHEAD-__PT_SIZE
#ifdef CONFIG_KASAN
	brasl	%r14,kasan_early_init
#endif
	brasl	%r14,startup_init		# s390 specific early init
	brasl	%r14,start_kernel		# common init code
#
# We returned from start_kernel ?!? PANIK
#
	basr	%r13,0
	lpswe	.Ldw-.(%r13)		# load disabled wait psw

	.align	16
.LPG1:
.Ldw:	.quad	0x0002000180000000,0x0000000000000000
