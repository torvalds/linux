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

#
# startup-code at 0x10000, running in absolute addressing mode
# this is called either by the ipl loader or directly by PSW restart
# or linload or SALIPL
#
	.org	0x10000
startup:basr	%r13,0			# get base
.LPG0:	l	%r13,0f-.LPG0(%r13)
	b	0(%r13)
0:	.long	startup_continue

#
# params at 10400 (setup.h)
#
	.org	PARMAREA
	.long	0,0			# IPL_DEVICE
	.long	0,0			# INITRD_START
	.long	0,0			# INITRD_SIZE

	.org	COMMAND_LINE
	.byte	"root=/dev/ram0 ro"
	.byte	0

	.org	0x11000

startup_continue:
	basr	%r13,0			# get base
.LPG1:	mvi	__LC_AR_MODE_ID,0	# set ESA flag (mode 0)
	lctl	%c0,%c15,.Lctl-.LPG1(%r13) # load control registers
	l	%r12,.Lparmaddr-.LPG1(%r13) # pointer to parameter area
					# move IPL device to lowcore
	mvc	__LC_IPLDEV(4),IPL_DEVICE-PARMAREA(%r12)
#
# Setup stack
#
	l	%r15,.Linittu-.LPG1(%r13)
	mvc	__LC_CURRENT(4),__TI_task(%r15)
	ahi	%r15,1<<(PAGE_SHIFT+THREAD_ORDER) # init_task_union+THREAD_SIZE
	st	%r15,__LC_KERNEL_STACK	# set end of kernel stack
	ahi	%r15,-96
	xc	__SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15) # clear backchain
#
# Save ipl parameters, clear bss memory, initialize storage key for kernel pages,
# and create a kernel NSS if the SAVESYS= parm is defined
#
	l	%r14,.Lstartup_init-.LPG1(%r13)
	basr	%r14,%r14

	l	%r12,.Lmflags-.LPG1(%r13) # get address of machine_flags
#
# find out if we have an IEEE fpu
#
	mvc	__LC_PGM_NEW_PSW(8),.Lpcfpu-.LPG1(%r13)
	efpc	%r0,0			# test IEEE extract fpc instruction
	oi	3(%r12),2		# set IEEE fpu flag
.Lchkfpu:

#
# find out if we have the CSP instruction
#
       mvc	 __LC_PGM_NEW_PSW(8),.Lpccsp-.LPG1(%r13)
       la	 %r0,0
       lr	%r1,%r0
       la	%r2,4
       csp	%r0,%r2			# Test CSP instruction
       oi	3(%r12),8		# set CSP flag
.Lchkcsp:

#
# find out if we have the MVPG instruction
#
       mvc	__LC_PGM_NEW_PSW(8),.Lpcmvpg-.LPG1(%r13)
       sr	%r0,%r0
       la	%r1,0
       la	%r2,0
       mvpg	%r1,%r2			# Test CSP instruction
       oi	3(%r12),16		# set MVPG flag
.Lchkmvpg:

#
# find out if we have the IDTE instruction
#
	mvc	__LC_PGM_NEW_PSW(8),.Lpcidte-.LPG1(%r13)
	.long	0xb2b10000		# store facility list
	tm	0xc8,0x08		# check bit for clearing-by-ASCE
	bno	.Lchkidte-.LPG1(%r13)
	lhi	%r1,2094
	lhi	%r2,0
	.long	0xb98e2001
	oi	3(%r12),0x80		# set IDTE flag
.Lchkidte:

#
# find out if the diag 0x9c is available
#
	mvc	__LC_PGM_NEW_PSW(8),.Lpcdiag9c-.LPG1(%r13)
	stap	__LC_CPUID+4		# store cpu address
	lh	%r1,__LC_CPUID+4
	diag	%r1,0,0x9c		# test diag 0x9c
	oi	2(%r12),1		# set diag9c flag
.Lchkdiag9c:

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
.Lpcfpu:.long	0x00080000,0x80000000 + .Lchkfpu
.Lpccsp:.long	0x00080000,0x80000000 + .Lchkcsp
.Lpcmvpg:.long	0x00080000,0x80000000 + .Lchkmvpg
.Lpcidte:.long	0x00080000,0x80000000 + .Lchkidte
.Lpcdiag9c:.long 0x00080000,0x80000000 + .Lchkdiag9c
.Lmchunk:.long	memory_chunk
.Lmflags:.long	machine_flags
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
