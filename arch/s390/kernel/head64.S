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
	lctlg	%c0,%c15,.Lctl-.LPG1(%r13)	# load control registers
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
.Lctl:	.quad	0x04040000		# cr0: AFP registers & secondary space
	.quad	0			# cr1: primary space segment table
	.quad	0			# cr2: dispatchable unit control table
	.quad	0			# cr3: instruction authorization
	.quad	0xffff			# cr4: instruction authorization
	.quad	0			# cr5: primary-aste origin
	.quad	0			# cr6: I/O interrupts
	.quad	0			# cr7: secondary space segment table
	.quad	0x0000000000008000	# cr8: access registers translation
	.quad	0			# cr9: tracing off
	.quad	0			# cr10: tracing off
	.quad	0			# cr11: tracing off
	.quad	0			# cr12: tracing off
	.quad	0			# cr13: home space segment table
	.quad	0xc0000000		# cr14: machine check handling off
	.quad	0			# cr15: linkage stack operations
