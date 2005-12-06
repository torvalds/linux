/*
 * arch/s390/kernel/head31.S
 *
 * (C) Copyright IBM Corp. 2005
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
startup:basr	%r13,0			 # get base
.LPG1:	l	%r1, .Lget_ipl_device_addr-.LPG1(%r13)
	basr	%r14, %r1
	lctl	%c0,%c15,.Lctl-.LPG1(%r13) # load control registers
	la	%r12,_pstart-.LPG1(%r13) # pointer to parameter area
					 # move IPL device to lowcore
	mvc	__LC_IPLDEV(4),IPL_DEVICE-PARMAREA(%r12)

#
# clear bss memory
#
	l	%r2,.Lbss_bgn-.LPG1(%r13) # start of bss
	l	%r3,.Lbss_end-.LPG1(%r13) # end of bss
	sr	%r3,%r2			# length of bss
	sr	%r4,%r4
	sr	%r5,%r5			# set src,length and pad to zero
	sr	%r0,%r0
	mvcle	%r2,%r4,0		# clear mem
	jo	.-4			# branch back, if not finish

	l	%r2,.Lrcp-.LPG1(%r13)	# Read SCP forced command word
.Lservicecall:
	stosm	.Lpmask-.LPG1(%r13),0x01	# authorize ext interrupts

	stctl	%r0, %r0,.Lcr-.LPG1(%r13)	# get cr0
	la	%r1,0x200		# set bit 22
	o	%r1,.Lcr-.LPG1(%r13)	# or old cr0 with r1
	st	%r1,.Lcr-.LPG1(%r13)
	lctl	%r0, %r0,.Lcr-.LPG1(%r13)	# load modified cr0

	mvc	__LC_EXT_NEW_PSW(8),.Lpcext-.LPG1(%r13) # set postcall psw
	la	%r1, .Lsclph-.LPG1(%r13)
	a	%r1,__LC_EXT_NEW_PSW+4	# set handler
	st	%r1,__LC_EXT_NEW_PSW+4

	la	%r4,_pstart-.LPG1(%r13)	# %r4 is our index for sccb stuff
	la	%r1, .Lsccb-PARMAREA(%r4)	# our sccb
	.insn	rre,0xb2200000,%r2,%r1	# service call
	ipm	%r1
	srl	%r1,28			# get cc code
	xr	%r3, %r3
	chi	%r1,3
	be	.Lfchunk-.LPG1(%r13)	# leave
	chi	%r1,2
	be	.Lservicecall-.LPG1(%r13)
	lpsw	.Lwaitsclp-.LPG1(%r13)
.Lsclph:
	lh	%r1,.Lsccbr-PARMAREA(%r4)
	chi	%r1,0x10		# 0x0010 is the sucess code
	je	.Lprocsccb		# let's process the sccb
	chi	%r1,0x1f0
	bne	.Lfchunk-.LPG1(%r13)	# unhandled error code
	c	%r2, .Lrcp-.LPG1(%r13)	# Did we try Read SCP forced
	bne	.Lfchunk-.LPG1(%r13)	# if no, give up
	l	%r2, .Lrcp2-.LPG1(%r13)	# try with Read SCP
	b	.Lservicecall-.LPG1(%r13)
.Lprocsccb:
	lhi	%r1,0
	icm	%r1,3,.Lscpincr1-PARMAREA(%r4) # use this one if != 0
	jnz	.Lscnd
	lhi	%r1,0x800		# otherwise report 2GB
.Lscnd:
	lhi	%r3,0x800		# limit reported memory size to 2GB
	cr	%r1,%r3
	jl	.Lno2gb
	lr	%r1,%r3
.Lno2gb:
	xr	%r3,%r3			# same logic
	ic	%r3,.Lscpa1-PARMAREA(%r4)
	chi	%r3,0x00
	jne	.Lcompmem
	l	%r3,.Lscpa2-PARMAREA(%r13)
.Lcompmem:
	mr	%r2,%r1			# mem in MB on 128-bit
	l	%r1,.Lonemb-.LPG1(%r13)
	mr	%r2,%r1			# mem size in bytes in %r3
	b	.Lfchunk-.LPG1(%r13)

	.align 4
.Lget_ipl_device_addr:
	.long	.Lget_ipl_device
.Lpmask:
	.byte	0
.align 8
.Lpcext:.long	0x00080000,0x80000000
.Lcr:
	.long	0x00			# place holder for cr0
.Lwaitsclp:
	.long 0x010a0000,0x80000000 + .Lsclph
.Lrcp:
	.int	0x00120001		# Read SCP forced code
.Lrcp2:
	.int	0x00020001		# Read SCP code
.Lonemb:
	.int	0x100000
.Lfchunk:

#
# find memory chunks.
#
	lr	%r9,%r3			# end of mem
	mvc	__LC_PGM_NEW_PSW(8),.Lpcmem-.LPG1(%r13)
	la	%r1,1			# test in increments of 128KB
	sll	%r1,17
	l	%r3,.Lmchunk-.LPG1(%r13) # get pointer to memory_chunk array
	slr	%r4,%r4			# set start of chunk to zero
	slr	%r5,%r5			# set end of chunk to zero
	slr	%r6,%r6			# set access code to zero
	la	%r10, MEMORY_CHUNKS	# number of chunks
.Lloop:
	tprot	0(%r5),0		# test protection of first byte
	ipm	%r7
	srl	%r7,28
	clr	%r6,%r7			# compare cc with last access code
	be	.Lsame-.LPG1(%r13)
	b	.Lchkmem-.LPG1(%r13)
.Lsame:
	ar	%r5,%r1			# add 128KB to end of chunk
	bno	.Lloop-.LPG1(%r13)	# r1 < 0x80000000 -> loop
.Lchkmem:				# > 2GB or tprot got a program check
	clr	%r4,%r5			# chunk size > 0?
	be	.Lchkloop-.LPG1(%r13)
	st	%r4,0(%r3)		# store start address of chunk
	lr	%r0,%r5
	slr	%r0,%r4
	st	%r0,4(%r3)		# store size of chunk
	st	%r6,8(%r3)		# store type of chunk
	la	%r3,12(%r3)
	l	%r4,.Lmemsize-.LPG1(%r13)	 # address of variable memory_size
	st	%r5,0(%r4)		# store last end to memory size
	ahi	%r10,-1			# update chunk number
.Lchkloop:
	lr	%r6,%r7			# set access code to last cc
	# we got an exception or we're starting a new
	# chunk , we must check if we should
	# still try to find valid memory (if we detected
	# the amount of available storage), and if we
	# have chunks left
	xr	%r0,%r0
	clr	%r0,%r9			# did we detect memory?
	je	.Ldonemem		# if not, leave
	chi	%r10,0			# do we have chunks left?
	je	.Ldonemem
	alr	%r5,%r1			# add 128KB to end of chunk
	lr	%r4,%r5			# potential new chunk
	clr	%r5,%r9			# should we go on?
	jl	.Lloop
.Ldonemem:
	l	%r12,.Lmflags-.LPG1(%r13) # get address of machine_flags
#
# find out if we are running under VM
#
	stidp	__LC_CPUID		# store cpuid
	tm	__LC_CPUID,0xff		# running under VM ?
	bno	.Lnovm-.LPG1(%r13)
	oi	3(%r12),1		# set VM flag
.Lnovm:
	lh	%r0,__LC_CPUID+4	# get cpu version
	chi	%r0,0x7490		# running on a P/390 ?
	bne	.Lnop390-.LPG1(%r13)
	oi	3(%r12),4		# set P/390 flag
.Lnop390:

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

	lpsw  .Lentry-.LPG1(13)		# jump to _stext in primary-space,
					# virtual and never return ...
	.align	8
.Lentry:.long	0x00080000,0x80000000 + _stext
.Lctl:	.long	0x04b50002		# cr0: various things
	.long	0			# cr1: primary space segment table
	.long	.Lduct			# cr2: dispatchable unit control table
	.long	0			# cr3: instruction authorization
	.long	0			# cr4: instruction authorization
	.long	0xffffffff		# cr5: primary-aste origin
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
.Lpcmem:.long	0x00080000,0x80000000 + .Lchkmem
.Lpcfpu:.long	0x00080000,0x80000000 + .Lchkfpu
.Lpccsp:.long	0x00080000,0x80000000 + .Lchkcsp
.Lpcmvpg:.long	0x00080000,0x80000000 + .Lchkmvpg
.Lpcidte:.long	0x00080000,0x80000000 + .Lchkidte
.Lmemsize:.long memory_size
.Lmchunk:.long	memory_chunk
.Lmflags:.long	machine_flags
.Lbss_bgn:  .long __bss_start
.Lbss_end:  .long _end

	.org	PARMAREA-64
.Lduct:	.long	0,0,0,0,0,0,0,0
	.long	0,0,0,0,0,0,0,0

#
# params at 10400 (setup.h)
#
	.org	PARMAREA
	.global _pstart
_pstart:
	.long	0,0			# IPL_DEVICE
	.long	0,RAMDISK_ORIGIN	# INITRD_START
	.long	0,RAMDISK_SIZE		# INITRD_SIZE

	.org	COMMAND_LINE
	.byte	"root=/dev/ram0 ro"
	.byte	0
	.org	0x11000
.Lsccb:
	.hword	0x1000			# length, one page
	.byte	0x00,0x00,0x00
	.byte	0x80			# variable response bit set
.Lsccbr:
	.hword	0x00			# response code
.Lscpincr1:
	.hword	0x00
.Lscpa1:
	.byte	0x00
	.fill	89,1,0
.Lscpa2:
	.int	0x00
.Lscpincr2:
	.quad	0x00
	.fill	3984,1,0
	.org	0x12000
	.global	_pend
_pend:

	GET_IPL_DEVICE

#ifdef CONFIG_SHARED_KERNEL
	.org	0x100000
#endif

#
# startup-code, running in virtual mode
#
	.globl	_stext
_stext:	basr	%r13,0			# get base
.LPG3:
#
# Setup stack
#
	l	%r15,.Linittu-.LPG3(%r13)
	mvc	__LC_CURRENT(4),__TI_task(%r15)
	ahi	%r15,1<<(PAGE_SHIFT+THREAD_ORDER) # init_task_union+THREAD_SIZE
	st	%r15,__LC_KERNEL_STACK	# set end of kernel stack
	ahi	%r15,-96
	xc	__SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15) # clear backchain

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
.Linittu:.long	init_thread_union
.Lstart:.long	start_kernel
.Laregs:.long	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
