/*
 * arch/s390/kernel/head64.S
 *
 * (C) Copyright IBM Corp. 1999,2005
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
        .org  0x10000
startup:basr  %r13,0                     # get base
.LPG1:  sll   %r13,1                     # remove high order bit
        srl   %r13,1
	l     %r1,.Lget_ipl_device_addr-.LPG1(%r13)
	basr  %r14,%r1
        lhi   %r1,1                      # mode 1 = esame
        slr   %r0,%r0                    # set cpuid to zero
        sigp  %r1,%r0,0x12               # switch to esame mode
	sam64				 # switch to 64 bit mode
	lctlg %c0,%c15,.Lctl-.LPG1(%r13) # load control registers
	larl  %r12,_pstart               # pointer to parameter area
					 # move IPL device to lowcore
        mvc   __LC_IPLDEV(4),IPL_DEVICE+4-PARMAREA(%r12)

#
# clear bss memory
#
	larl  %r2,__bss_start           # start of bss segment
        larl  %r3,_end                  # end of bss segment
        sgr   %r3,%r2                   # length of bss
        sgr   %r4,%r4                   #
        sgr   %r5,%r5                   # set src,length and pad to zero
        mvcle %r2,%r4,0                 # clear mem
        jo    .-4                       # branch back, if not finish

	l     %r2,.Lrcp-.LPG1(%r13)	# Read SCP forced command word
.Lservicecall:
	stosm .Lpmask-.LPG1(%r13),0x01	# authorize ext interrupts

	stctg %r0,%r0,.Lcr-.LPG1(%r13)	# get cr0
	la    %r1,0x200			# set bit 22
	og    %r1,.Lcr-.LPG1(%r13)	# or old cr0 with r1
	stg   %r1,.Lcr-.LPG1(%r13)
	lctlg %r0,%r0,.Lcr-.LPG1(%r13)	# load modified cr0

	mvc   __LC_EXT_NEW_PSW(8),.Lpcmsk-.LPG1(%r13) # set postcall psw
	larl  %r1,.Lsclph
	stg   %r1,__LC_EXT_NEW_PSW+8	# set handler

	larl  %r4,_pstart		# %r4 is our index for sccb stuff
	la    %r1,.Lsccb-PARMAREA(%r4)	# our sccb
	.insn rre,0xb2200000,%r2,%r1	# service call
	ipm   %r1
	srl   %r1,28			# get cc code
	xr    %r3,%r3
	chi   %r1,3
	be    .Lfchunk-.LPG1(%r13)	# leave
	chi   %r1,2
	be    .Lservicecall-.LPG1(%r13)
	lpswe .Lwaitsclp-.LPG1(%r13)
.Lsclph:
	lh    %r1,.Lsccbr-PARMAREA(%r4)
	chi   %r1,0x10			# 0x0010 is the sucess code
	je    .Lprocsccb		# let's process the sccb
	chi   %r1,0x1f0
	bne   .Lfchunk-.LPG1(%r13)	# unhandled error code
	c     %r2,.Lrcp-.LPG1(%r13)	# Did we try Read SCP forced
	bne   .Lfchunk-.LPG1(%r13)	# if no, give up
	l     %r2,.Lrcp2-.LPG1(%r13)	# try with Read SCP
	b     .Lservicecall-.LPG1(%r13)
.Lprocsccb:
	lghi  %r1,0
	icm   %r1,3,.Lscpincr1-PARMAREA(%r4) # use this one if != 0
	jnz   .Lscnd
	lg    %r1,.Lscpincr2-PARMAREA(%r4) # otherwise use this one
.Lscnd:
	xr    %r3,%r3			# same logic
	ic    %r3,.Lscpa1-PARMAREA(%r4)
	chi   %r3,0x00
	jne   .Lcompmem
	l     %r3,.Lscpa2-PARMAREA(%r13)
.Lcompmem:
	mlgr  %r2,%r1			# mem in MB on 128-bit
	l     %r1,.Lonemb-.LPG1(%r13)
	mlgr  %r2,%r1			# mem size in bytes in %r3
	b     .Lfchunk-.LPG1(%r13)

	.align 4
.Lget_ipl_device_addr:
	.long .Lget_ipl_device
.Lpmask:
	.byte 0
	.align 8
.Lcr:
	.quad 0x00  # place holder for cr0
.Lwaitsclp:
	.quad  0x0102000180000000,.Lsclph
.Lrcp:
	.int 0x00120001 # Read SCP forced code
.Lrcp2:
	.int 0x00020001 # Read SCP code
.Lonemb:
	.int 0x100000

.Lfchunk:
					 # set program check new psw mask
	mvc   __LC_PGM_NEW_PSW(8),.Lpcmsk-.LPG1(%r13)

#
# find memory chunks.
#
	lgr   %r9,%r3			 # end of mem
	larl  %r1,.Lchkmem               # set program check address
	stg   %r1,__LC_PGM_NEW_PSW+8
	la    %r1,1                      # test in increments of 128KB
	sllg  %r1,%r1,17
	larl  %r3,memory_chunk
	slgr  %r4,%r4                    # set start of chunk to zero
	slgr  %r5,%r5                    # set end of chunk to zero
	slr  %r6,%r6			 # set access code to zero
	la    %r10,MEMORY_CHUNKS	 # number of chunks
.Lloop:
	tprot 0(%r5),0			 # test protection of first byte
	ipm   %r7
	srl   %r7,28
	clr   %r6,%r7			 # compare cc with last access code
	je    .Lsame
	j     .Lchkmem
.Lsame:
	algr  %r5,%r1			 # add 128KB to end of chunk
					 # no need to check here,
	brc   12,.Lloop			 # this is the same chunk
.Lchkmem:				 # > 16EB or tprot got a program check
	clgr  %r4,%r5			 # chunk size > 0?
	je    .Lchkloop
	stg   %r4,0(%r3)		 # store start address of chunk
	lgr   %r0,%r5
	slgr  %r0,%r4
	stg   %r0,8(%r3)		 # store size of chunk
	st    %r6,20(%r3)		 # store type of chunk
	la    %r3,24(%r3)
	larl  %r8,memory_size
	stg   %r5,0(%r8)                 # store memory size
	ahi   %r10,-1			 # update chunk number
.Lchkloop:
	lr    %r6,%r7			 # set access code to last cc
	# we got an exception or we're starting a new
	# chunk , we must check if we should
	# still try to find valid memory (if we detected
	# the amount of available storage), and if we
	# have chunks left
	lghi  %r4,1
	sllg  %r4,%r4,31
	clgr  %r5,%r4
	je    .Lhsaskip
	xr    %r0, %r0
	clgr  %r0, %r9			 # did we detect memory?
	je    .Ldonemem			 # if not, leave
	chi   %r10, 0			 # do we have chunks left?
	je    .Ldonemem
.Lhsaskip:
	algr  %r5,%r1			 # add 128KB to end of chunk
	lgr   %r4,%r5			 # potential new chunk
	clgr  %r5,%r9			 # should we go on?
	jl    .Lloop
.Ldonemem:		

	larl  %r12,machine_flags
#
# find out if we are running under VM
#
        stidp  __LC_CPUID               # store cpuid
	tm     __LC_CPUID,0xff          # running under VM ?
	bno    0f-.LPG1(%r13)
        oi     7(%r12),1                # set VM flag
0:      lh     %r0,__LC_CPUID+4         # get cpu version
        chi    %r0,0x7490               # running on a P/390 ?
        bne    1f-.LPG1(%r13)
        oi     7(%r12),4                # set P/390 flag
1:

#
# find out if we have the MVPG instruction
#
	la     %r1,0f-.LPG1(%r13)       # set program check address
	stg    %r1,__LC_PGM_NEW_PSW+8
	sgr    %r0,%r0
	lghi   %r1,0
	lghi   %r2,0
	mvpg   %r1,%r2                  # test MVPG instruction
	oi     7(%r12),16               # set MVPG flag
0:

#
# find out if the diag 0x44 works in 64 bit mode
#
	la     %r1,0f-.LPG1(%r13)	# set program check address
	stg    %r1,__LC_PGM_NEW_PSW+8
	diag   0,0,0x44			# test diag 0x44
	oi     7(%r12),32		# set diag44 flag
0:	

#
# find out if we have the IDTE instruction
#
	la     %r1,0f-.LPG1(%r13)	# set program check address
	stg    %r1,__LC_PGM_NEW_PSW+8
	.long	0xb2b10000		# store facility list
	tm	0xc8,0x08		# check bit for clearing-by-ASCE
	bno	0f-.LPG1(%r13)
	lhi	%r1,2094
	lhi	%r2,0
	.long	0xb98e2001
	oi	7(%r12),0x80		# set IDTE flag
0:

        lpswe .Lentry-.LPG1(13)         # jump to _stext in primary-space,
                                        # virtual and never return ...
        .align 16
.Lentry:.quad  0x0000000180000000,_stext
.Lctl:  .quad  0x04b50002               # cr0: various things
        .quad  0                        # cr1: primary space segment table
        .quad  .Lduct                   # cr2: dispatchable unit control table
        .quad  0                        # cr3: instruction authorization
        .quad  0                        # cr4: instruction authorization
        .quad  0xffffffffffffffff       # cr5: primary-aste origin
        .quad  0                        # cr6:  I/O interrupts
        .quad  0                        # cr7:  secondary space segment table
        .quad  0                        # cr8:  access registers translation
        .quad  0                        # cr9:  tracing off
        .quad  0                        # cr10: tracing off
        .quad  0                        # cr11: tracing off
        .quad  0                        # cr12: tracing off
        .quad  0                        # cr13: home space segment table
        .quad  0xc0000000               # cr14: machine check handling off
        .quad  0                        # cr15: linkage stack operations
.Lpcmsk:.quad  0x0000000180000000
.L4malign:.quad 0xffffffffffc00000
.Lscan2g:.quad 0x80000000 + 0x20000 - 8 # 2GB + 128K - 8
.Lnop:	.long  0x07000700

	.org PARMAREA-64
.Lduct:	.long 0,0,0,0,0,0,0,0
	.long 0,0,0,0,0,0,0,0

#
# params at 10400 (setup.h)
#
	.org   PARMAREA
	.global _pstart
_pstart:
	.quad  0                        # IPL_DEVICE
        .quad  RAMDISK_ORIGIN           # INITRD_START
        .quad  RAMDISK_SIZE             # INITRD_SIZE

        .org   COMMAND_LINE
    	.byte  "root=/dev/ram0 ro"
        .byte  0
	.org   0x11000
.Lsccb:
	.hword 0x1000			# length, one page
	.byte 0x00,0x00,0x00
	.byte 0x80			# variable response bit set
.Lsccbr:
	.hword 0x00			# response code
.Lscpincr1:
	.hword 0x00
.Lscpa1:
	.byte 0x00
	.fill 89,1,0
.Lscpa2:
	.int 0x00
.Lscpincr2:
	.quad 0x00
	.fill 3984,1,0
	.org 0x12000
	.global _pend
_pend:	

	GET_IPL_DEVICE

#ifdef CONFIG_SHARED_KERNEL
	.org   0x100000
#endif
	
#
# startup-code, running in virtual mode
#
        .globl _stext
_stext:	basr  %r13,0                    # get base
.LPG3:
#
# Setup stack
#
	larl  %r15,init_thread_union
	lg    %r14,__TI_task(%r15)      # cache current in lowcore
	stg   %r14,__LC_CURRENT
        aghi  %r15,1<<(PAGE_SHIFT+THREAD_ORDER) # init_task_union + THREAD_SIZE
        stg   %r15,__LC_KERNEL_STACK    # set end of kernel stack
        aghi  %r15,-160
        xc    __SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15) # clear backchain

# check control registers
        stctg  %c0,%c15,0(%r15)
	oi     6(%r15),0x40             # enable sigp emergency signal
	oi     4(%r15),0x10             # switch on low address proctection
        lctlg  %c0,%c15,0(%r15)

#
        lam    0,15,.Laregs-.LPG3(%r13) # load access regs needed by uaccess
        brasl  %r14,start_kernel        # go to C code
#
# We returned from start_kernel ?!? PANIK
#
        basr  %r13,0
	lpswe .Ldw-.(%r13)           # load disabled wait psw
#
            .align 8
.Ldw:       .quad  0x0002000180000000,0x0000000000000000
.Laregs:    .long  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

