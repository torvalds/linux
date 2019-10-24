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
	tm	__LC_STFLE_FAC_LIST+5,0x80	# LPP available ?
	jz	0f
	xc	__LC_LPP+1(7,0),__LC_LPP+1	# clear lpp and current_pid
	mvi	__LC_LPP,0x80			#   and set LPP_MAGIC
	.insn	s,0xb2800000,__LC_LPP		# load program parameter
0:	larl	%r1,tod_clock_base
	mvc	0(16,%r1),__LC_BOOT_CLOCK
	larl	%r13,.LPG1		# get base
	larl	%r0,boot_vdso_data
	stg	%r0,__LC_VDSO_PER_CPU
#
# Setup stack
#
	larl	%r14,init_task
	stg	%r14,__LC_CURRENT
	larl	%r15,init_thread_union+THREAD_SIZE-STACK_FRAME_OVERHEAD
#
# Early setup functions that may not rely on an initialized bss section,
# like moving the initrd. Returns with an initialized bss section.
#
	brasl	%r14,startup_init_nobss
#
# Early machine initialization and detection functions.
#
	brasl	%r14,startup_init

# check control registers
	stctg	%c0,%c15,0(%r15)
	oi	6(%r15),0x60		# enable sigp emergency & external call
	oi	4(%r15),0x10		# switch on low address proctection
	lctlg	%c0,%c15,0(%r15)

	lam	0,15,.Laregs-.LPG1(%r13)	# load acrs needed by uaccess
	brasl	%r14,start_kernel		# go to C code
#
# We returned from start_kernel ?!? PANIK
#
	basr	%r13,0
	lpswe	.Ldw-.(%r13)		# load disabled wait psw

	.align	16
.LPG1:
.Ldw:	.quad	0x0002000180000000,0x0000000000000000
.Laregs:.long	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
