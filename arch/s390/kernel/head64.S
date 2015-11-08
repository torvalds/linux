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

__HEAD
ENTRY(startup_continue)
	tm	__LC_STFL_FAC_LIST+6,0x80	# LPP available ?
	jz	0f
	xc	__LC_LPP+1(7,0),__LC_LPP+1	# clear lpp and current_pid
	mvi	__LC_LPP,0x80			#   and set LPP_MAGIC
	.insn	s,0xb2800000,__LC_LPP		# load program parameter
0:	larl	%r1,sched_clock_base_cc
	mvc	0(8,%r1),__LC_LAST_UPDATE_CLOCK
	larl	%r13,.LPG1		# get base
	lctlg	%c0,%c15,.Lctl-.LPG1(%r13)	# load control registers
	lg	%r12,.Lparmaddr-.LPG1(%r13)	# pointer to parameter area
					# move IPL device to lowcore
	lghi	%r0,__LC_PASTE
	stg	%r0,__LC_VDSO_PER_CPU
#
# Setup stack
#
	larl	%r15,init_thread_union
	stg	%r15,__LC_THREAD_INFO	# cache thread info in lowcore
	lg	%r14,__TI_task(%r15)	# cache current in lowcore
	stg	%r14,__LC_CURRENT
	aghi	%r15,1<<(PAGE_SHIFT+THREAD_ORDER) # init_task_union + THREAD_SIZE
	stg	%r15,__LC_KERNEL_STACK	# set end of kernel stack
	aghi	%r15,-160
#
# Save ipl parameters, clear bss memory, initialize storage key for kernel pages,
# and create a kernel NSS if the SAVESYS= parm is defined
#
	brasl	%r14,startup_init
	lpswe	.Lentry-.LPG1(13)	# jump to _stext in primary-space,
					# virtual and never return ...
	.align	16
.LPG1:
.Lentry:.quad	0x0000000180000000,_stext
.Lctl:	.quad	0x04040000		# cr0: AFP registers & secondary space
	.quad	0			# cr1: primary space segment table
	.quad	.Lduct			# cr2: dispatchable unit control table
	.quad	0			# cr3: instruction authorization
	.quad	0			# cr4: instruction authorization
	.quad	.Lduct			# cr5: primary-aste origin
	.quad	0			# cr6:	I/O interrupts
	.quad	0			# cr7:	secondary space segment table
	.quad	0			# cr8:	access registers translation
	.quad	0			# cr9:	tracing off
	.quad	0			# cr10: tracing off
	.quad	0			# cr11: tracing off
	.quad	0			# cr12: tracing off
	.quad	0			# cr13: home space segment table
	.quad	0xc0000000		# cr14: machine check handling off
	.quad	.Llinkage_stack		# cr15: linkage stack operations
.Lpcmsk:.quad	0x0000000180000000
.L4malign:.quad 0xffffffffffc00000
.Lscan2g:.quad	0x80000000 + 0x20000 - 8	# 2GB + 128K - 8
.Lnop:	.long	0x07000700
.Lparmaddr:
	.quad	PARMAREA
	.align	64
.Lduct: .long	0,.Laste,.Laste,0,.Lduald,0,0,0
	.long	0,0,0,0,0,0,0,0
.Laste:	.quad	0,0xffffffffffffffff,0,0,0,0,0,0
	.align	128
.Lduald:.rept	8
	.long	0x80000000,0,0,0	# invalid access-list entries
	.endr
.Llinkage_stack:
	.long	0,0,0x89000000,0,0,0,0x8a000000,0

ENTRY(_ehead)

	.org	0x100000 - 0x11000	# head.o ends at 0x11000
#
# startup-code, running in absolute addressing mode
#
ENTRY(_stext)
	basr	%r13,0			# get base
.LPG3:
# check control registers
	stctg	%c0,%c15,0(%r15)
	oi	6(%r15),0x60		# enable sigp emergency & external call
	oi	4(%r15),0x10		# switch on low address proctection
	lctlg	%c0,%c15,0(%r15)

	lam	0,15,.Laregs-.LPG3(%r13)	# load acrs needed by uaccess
	brasl	%r14,start_kernel	# go to C code
#
# We returned from start_kernel ?!? PANIK
#
	basr	%r13,0
	lpswe	.Ldw-.(%r13)		# load disabled wait psw

	.align	8
.Ldw:	.quad	0x0002000180000000,0x0000000000000000
.Laregs:.long	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
