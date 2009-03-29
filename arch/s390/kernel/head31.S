/*
 * arch/s390/kernel/head31.S
 *
 * Copyright (C) IBM Corp. 2005,2006
 *
 *   Author(s):	Hartmut Penner <hp@de.ibm.com>
 *		Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		Rob van der Heij <rvdhei@iae.nl>
 *		Heiko Carstens <heiko.carstens@de.ibm.com>
 *
 */

	.org	0x11000

startup_continue:
	basr	%r13,0			# get base
.LPG1:

	mvi	__LC_AR_MODE_ID,0	# set ESA flag (mode 0)
	lctl	%c0,%c15,.Lctl-.LPG1(%r13) # load control registers
	l	%r12,.Lparmaddr-.LPG1(%r13) # pointer to parameter area
					# move IPL device to lowcore
#
# Setup stack
#
	l	%r15,.Linittu-.LPG1(%r13)
	mvc	__LC_CURRENT(4),__TI_task(%r15)
	ahi	%r15,1<<(PAGE_SHIFT+THREAD_ORDER) # init_task_union+THREAD_SIZE
	st	%r15,__LC_KERNEL_STACK	# set end of kernel stack
	ahi	%r15,-96
#
# Save ipl parameters, clear bss memory, initialize storage key for kernel pages,
# and create a kernel NSS if the SAVESYS= parm is defined
#
	l	%r14,.Lstartup_init-.LPG1(%r13)
	basr	%r14,%r14
	lpsw  .Lentry-.LPG1(13)		# jump to _stext in primary-space,
					# virtual and never return ...
	.align	8
.Lentry:.long	0x00080000,0x80000000 + _stext
.Lctl:	.long	0x04b50002		# cr0: various things
	.long	0			# cr1: primary space segment table
	.long	.Lduct			# cr2: dispatchable unit control table
	.long	0			# cr3: instruction authorization
	.long	0			# cr4: instruction authorization
	.long	.Lduct			# cr5: primary-aste origin
	.long	0			# cr6:	I/O interrupts
	.long	0			# cr7:	secondary space segment table
	.long	0			# cr8:	access registers translation
	.long	0			# cr9:	tracing off
	.long	0			# cr10: tracing off
	.long	0			# cr11: tracing off
	.long	0			# cr12: tracing off
	.long	0			# cr13: home space segment table
	.long	0xc0000000		# cr14: machine check handling off
	.long	0			# cr15: linkage stack operations
.Lmchunk:.long	memory_chunk
.Lbss_bgn:  .long __bss_start
.Lbss_end:  .long _end
.Lparmaddr: .long PARMAREA
.Linittu:   .long init_thread_union
.Lstartup_init:
	    .long startup_init
	.align	64
.Lduct:	.long	0,0,0,0,.Lduald,0,0,0
	.long	0,0,0,0,0,0,0,0
	.align	128
.Lduald:.rept	8
	.long	0x80000000,0,0,0	# invalid access-list entries
	.endr

	.org	0x12000
	.globl	_ehead
_ehead:
#ifdef CONFIG_SHARED_KERNEL
	.org	0x100000
#endif

#
# startup-code, running in absolute addressing mode
#
	.globl	_stext
_stext:	basr	%r13,0			# get base
.LPG3:
# check control registers
	stctl	%c0,%c15,0(%r15)
	oi	2(%r15),0x40		# enable sigp emergency signal
	oi	0(%r15),0x10		# switch on low address protection
	lctl	%c0,%c15,0(%r15)

#
	lam	0,15,.Laregs-.LPG3(%r13) # load access regs needed by uaccess
	l	%r14,.Lstart-.LPG3(%r13)
	basr	%r14,%r14		# call start_kernel
#
# We returned from start_kernel ?!? PANIK
#
	basr	%r13,0
	lpsw	.Ldw-.(%r13)		# load disabled wait psw
#
	.align	8
.Ldw:	.long	0x000a0000,0x00000000
.Lstart:.long	start_kernel
.Laregs:.long	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
