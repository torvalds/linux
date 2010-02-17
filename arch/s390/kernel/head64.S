/*
 * arch/s390/kernel/head64.S
 *
 * Copyright (C) IBM Corp. 1999,2006
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
.LPG1:	sll	%r13,1			# remove high order bit
	srl	%r13,1

#ifdef CONFIG_ZFCPDUMP

	# check if we have been ipled using zfcp dump:

	tm	0xb9,0x01		# test if subchannel is enabled
	jno	.nodump			# subchannel disabled
	l	%r1,0xb8
	la	%r5,.Lipl_schib-.LPG1(%r13)
	stsch	0(%r5)			# get schib of subchannel
	jne	.nodump			# schib not available
	tm	5(%r5),0x01		# devno valid?
	jno	.nodump
	tm	4(%r5),0x80		# qdio capable device?
	jno	.nodump
	l	%r2,20(%r0)		# address of ipl parameter block
	lhi	%r3,0
	ic	%r3,0x148(%r2)		# get opt field
	chi	%r3,0x20		# load with dump?
	jne	.nodump

	# store all prefix registers in case of load with dump:

	la	%r7,0			# base register for 0 page
	la	%r8,0			# first cpu
	l	%r11,.Lpref_arr_ptr-.LPG1(%r13)	# address of prefix array
	ahi	%r11,4			# skip boot cpu
	lr	%r12,%r11
	ahi	%r12,(CONFIG_NR_CPUS*4)	# end of prefix array
	stap	.Lcurrent_cpu+2-.LPG1(%r13)	# store current cpu addr
1:
	cl	%r8,.Lcurrent_cpu-.LPG1(%r13)	# is ipl cpu ?
	je	4f				# if yes get next cpu
2:
	lr	%r9,%r7
	sigp	%r9,%r8,0x9		# stop & store status of cpu
	brc	8,3f			# accepted
	brc	4,4f			# status stored: next cpu
	brc	2,2b			# busy: 	 try again
	brc	1,4f			# not op:	 next cpu
3:
	mvc	0(4,%r11),264(%r7)	# copy prefix register to prefix array
	ahi	%r11,4			# next element in prefix array
	clr	%r11,%r12
	je	5f			# no more space in prefix array
4:
	ahi	%r8,1			# next cpu (r8 += 1)
	chi	%r8,MAX_CPU_ADDRESS	# is last possible cpu ?
	jle	1b			# jump if not last cpu
5:
	lhi	%r1,2			# mode 2 = esame (dump)
	j	6f
	.align 4
.Lipl_schib:
	.rept 13
	.long 0
	.endr
.nodump:
	lhi	%r1,1			# mode 1 = esame (normal ipl)
6:
#else
	lhi	%r1,1			# mode 1 = esame (normal ipl)
#endif /* CONFIG_ZFCPDUMP */
	mvi	__LC_AR_MODE_ID,1	# set esame flag
	slr	%r0,%r0 		# set cpuid to zero
	sigp	%r1,%r0,0x12		# switch to esame mode
	sam64				# switch to 64 bit mode
	llgfr	%r13,%r13		# clear high-order half of base reg
	lmh	%r0,%r15,.Lzero64-.LPG1(%r13)	# clear high-order half
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
.Lentry:.quad	0x0000000180000000,_stext
.Lctl:	.quad	0x04350002		# cr0: various things
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
	.quad	0			# cr15: linkage stack operations
.Lpcmsk:.quad	0x0000000180000000
.L4malign:.quad 0xffffffffffc00000
.Lscan2g:.quad	0x80000000 + 0x20000 - 8	# 2GB + 128K - 8
.Lnop:	.long	0x07000700
.Lzero64:.fill	16,4,0x0
#ifdef CONFIG_ZFCPDUMP
.Lcurrent_cpu:
	.long 0x0
.Lpref_arr_ptr:
	.long zfcpdump_prefix_array
#endif /* CONFIG_ZFCPDUMP */
.Lparmaddr:
	.quad	PARMAREA
	.align	64
.Lduct: .long	0,0,0,0,.Lduald,0,0,0
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
	stctg	%c0,%c15,0(%r15)
	oi	6(%r15),0x40		# enable sigp emergency signal
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
