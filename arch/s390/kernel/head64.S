/*
 *  arch/s390/kernel/head.S
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Rob van der Heij (rvdhei@iae.nl)
 *
 * There are 5 different IPL methods
 *  1) load the image directly into ram at address 0 and do an PSW restart
 *  2) linload will load the image from address 0x10000 to memory 0x10000
 *     and start the code thru LPSW 0x0008000080010000 (VM only, deprecated)
 *  3) generate the tape ipl header, store the generated image on a tape
 *     and ipl from it
 *     In case of SL tape you need to IPL 5 times to get past VOL1 etc
 *  4) generate the vm reader ipl header, move the generated image to the
 *     VM reader (use option NOH!) and do a ipl from reader (VM only)
 *  5) direct call of start by the SALIPL loader
 *  We use the cpuid to distinguish between VM and native ipl
 *  params for kernel are pushed to 0x10400 (see setup.h)

    Changes: 
    Okt 25 2000 <rvdheij@iae.nl>
	added code to skip HDR and EOF to allow SL tape IPL (5 retries)
	changed first CCW from rewind to backspace block

 */

#include <linux/config.h>
#include <asm/setup.h>
#include <asm/lowcore.h>
#include <asm/offsets.h>
#include <asm/thread_info.h>
#include <asm/page.h>

#ifndef CONFIG_IPL
        .org   0
        .long  0x00080000,0x80000000+startup   # Just a restart PSW
#else
#ifdef CONFIG_IPL_TAPE
#define IPL_BS 1024
        .org   0
        .long  0x00080000,0x80000000+iplstart  # The first 24 bytes are loaded
        .long  0x27000000,0x60000001           # by ipl to addresses 0-23.
        .long  0x02000000,0x20000000+IPL_BS    # (a PSW and two CCWs).
        .long  0x00000000,0x00000000           # external old psw
        .long  0x00000000,0x00000000           # svc old psw
        .long  0x00000000,0x00000000           # program check old psw
        .long  0x00000000,0x00000000           # machine check old psw
        .long  0x00000000,0x00000000           # io old psw
        .long  0x00000000,0x00000000
        .long  0x00000000,0x00000000
        .long  0x00000000,0x00000000
        .long  0x000a0000,0x00000058           # external new psw
        .long  0x000a0000,0x00000060           # svc new psw
        .long  0x000a0000,0x00000068           # program check new psw
        .long  0x000a0000,0x00000070           # machine check new psw
        .long  0x00080000,0x80000000+.Lioint   # io new psw

        .org   0x100
#
# subroutine for loading from tape
# Paramters:	
#  R1 = device number
#  R2 = load address
.Lloader:	
        st    %r14,.Lldret
        la    %r3,.Lorbread                    # r3 = address of orb 
	la    %r5,.Lirb                        # r5 = address of irb
        st    %r2,.Lccwread+4                  # initialize CCW data addresses
        lctl  %c6,%c6,.Lcr6               
        slr   %r2,%r2
.Lldlp:
        la    %r6,3                            # 3 retries
.Lssch:
        ssch  0(%r3)                           # load chunk of IPL_BS bytes
        bnz   .Llderr
.Lw4end:
        bas   %r14,.Lwait4io
        tm    8(%r5),0x82                      # do we have a problem ?
        bnz   .Lrecov
        slr   %r7,%r7
        icm   %r7,3,10(%r5)                    # get residual count
        lcr   %r7,%r7
        la    %r7,IPL_BS(%r7)                  # IPL_BS-residual=#bytes read
        ar    %r2,%r7                          # add to total size
        tm    8(%r5),0x01                      # found a tape mark ?
        bnz   .Ldone
        l     %r0,.Lccwread+4                  # update CCW data addresses
        ar    %r0,%r7
        st    %r0,.Lccwread+4                
        b     .Lldlp
.Ldone:
        l     %r14,.Lldret
        br    %r14                             # r2 contains the total size
.Lrecov:
        bas   %r14,.Lsense                     # do the sensing
        bct   %r6,.Lssch                       # dec. retry count & branch
        b     .Llderr
#
# Sense subroutine
#
.Lsense:
        st    %r14,.Lsnsret
        la    %r7,.Lorbsense              
        ssch  0(%r7)                           # start sense command
        bnz   .Llderr
        bas   %r14,.Lwait4io
        l     %r14,.Lsnsret
        tm    8(%r5),0x82                      # do we have a problem ?
        bnz   .Llderr
        br    %r14
#
# Wait for interrupt subroutine
#
.Lwait4io:
        lpsw  .Lwaitpsw                 
.Lioint:
        c     %r1,0xb8                         # compare subchannel number
        bne   .Lwait4io
        tsch  0(%r5)
        slr   %r0,%r0
        tm    8(%r5),0x82                      # do we have a problem ?
        bnz   .Lwtexit
        tm    8(%r5),0x04                      # got device end ?
        bz    .Lwait4io
.Lwtexit:
        br    %r14
.Llderr:
        lpsw  .Lcrash              

        .align 8
.Lorbread:
	.long  0x00000000,0x0080ff00,.Lccwread
        .align 8
.Lorbsense:
        .long  0x00000000,0x0080ff00,.Lccwsense
        .align 8
.Lccwread:
        .long  0x02200000+IPL_BS,0x00000000
.Lccwsense:
        .long  0x04200001,0x00000000
.Lwaitpsw:
	.long  0x020a0000,0x80000000+.Lioint

.Lirb:	.long  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.Lcr6:  .long  0xff000000
        .align 8
.Lcrash:.long  0x000a0000,0x00000000
.Lldret:.long  0
.Lsnsret: .long 0
#endif  /* CONFIG_IPL_TAPE */

#ifdef CONFIG_IPL_VM
#define IPL_BS 0x730
        .org   0
        .long  0x00080000,0x80000000+iplstart  # The first 24 bytes are loaded
        .long  0x02000018,0x60000050           # by ipl to addresses 0-23.
        .long  0x02000068,0x60000050           # (a PSW and two CCWs).
        .fill  80-24,1,0x40                    # bytes 24-79 are discarded !!
        .long  0x020000f0,0x60000050           # The next 160 byte are loaded
        .long  0x02000140,0x60000050           # to addresses 0x18-0xb7
        .long  0x02000190,0x60000050           # They form the continuation
        .long  0x020001e0,0x60000050           # of the CCW program started
        .long  0x02000230,0x60000050           # by ipl and load the range
        .long  0x02000280,0x60000050           # 0x0f0-0x730 from the image
        .long  0x020002d0,0x60000050           # to the range 0x0f0-0x730
        .long  0x02000320,0x60000050           # in memory. At the end of
        .long  0x02000370,0x60000050           # the channel program the PSW
        .long  0x020003c0,0x60000050           # at location 0 is loaded.
        .long  0x02000410,0x60000050           # Initial processing starts
        .long  0x02000460,0x60000050           # at 0xf0 = iplstart.
        .long  0x020004b0,0x60000050
        .long  0x02000500,0x60000050
        .long  0x02000550,0x60000050
        .long  0x020005a0,0x60000050
        .long  0x020005f0,0x60000050
        .long  0x02000640,0x60000050
        .long  0x02000690,0x60000050
        .long  0x020006e0,0x20000050

        .org   0xf0
#
# subroutine for loading cards from the reader
#
.Lloader:	
	la    %r3,.Lorb                        # r2 = address of orb into r2
	la    %r5,.Lirb                        # r4 = address of irb
        la    %r6,.Lccws              
        la    %r7,20
.Linit:
        st    %r2,4(%r6)                       # initialize CCW data addresses
        la    %r2,0x50(%r2)
        la    %r6,8(%r6)
        bct   7,.Linit

        lctl  %c6,%c6,.Lcr6                    # set IO subclass mask
	slr   %r2,%r2
.Lldlp:
        ssch  0(%r3)                           # load chunk of 1600 bytes
        bnz   .Llderr
.Lwait4irq:
        mvc   0x78(8),.Lnewpsw                 # set up IO interrupt psw
        lpsw  .Lwaitpsw              
.Lioint:
        c     %r1,0xb8                         # compare subchannel number
	bne   .Lwait4irq
	tsch  0(%r5)

	slr   %r0,%r0
	ic    %r0,8(%r5)                       # get device status
	chi   %r0,8                            # channel end ?
	be    .Lcont
	chi   %r0,12                           # channel end + device end ?
	be    .Lcont

        l     %r0,4(%r5)
        s     %r0,8(%r3)                       # r0/8 = number of ccws executed
        mhi   %r0,10                           # *10 = number of bytes in ccws
        lh    %r3,10(%r5)                      # get residual count
        sr    %r0,%r3                          # #ccws*80-residual=#bytes read
	ar    %r2,%r0
	
        br    %r14                             # r2 contains the total size

.Lcont:
	ahi   %r2,0x640                        # add 0x640 to total size
        la    %r6,.Lccws             
        la    %r7,20
.Lincr:
        l     %r0,4(%r6)                       # update CCW data addresses
        ahi   %r0,0x640
        st    %r0,4(%r6)
        ahi   %r6,8
        bct   7,.Lincr

        b     .Lldlp
.Llderr:
        lpsw  .Lcrash              

        .align 8
.Lorb:	.long  0x00000000,0x0080ff00,.Lccws
.Lirb:	.long  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.Lcr6:  .long  0xff000000
.Lloadp:.long  0,0
        .align 8
.Lcrash:.long  0x000a0000,0x00000000
.Lnewpsw:
        .long  0x00080000,0x80000000+.Lioint
.Lwaitpsw:
        .long  0x020a0000,0x80000000+.Lioint

        .align 8
.Lccws: .rept  19
        .long  0x02600050,0x00000000
        .endr
        .long  0x02200050,0x00000000
#endif  /* CONFIG_IPL_VM */

iplstart:
        lh    %r1,0xb8                         # test if subchannel number
        bct   %r1,.Lnoload                     #  is valid
	l     %r1,0xb8                         # load ipl subchannel number
        la    %r2,IPL_BS                       # load start address
        bas   %r14,.Lloader                    # load rest of ipl image
        larl  %r12,_pstart                     # pointer to parameter area
        st    %r1,IPL_DEVICE+4-PARMAREA(%r12)  # store ipl device number

#
# load parameter file from ipl device
#
.Lagain1:
 	l     %r2,INITRD_START+4-PARMAREA(%r12)# use ramdisk location as temp
        bas   %r14,.Lloader                    # load parameter file
        ltr   %r2,%r2                          # got anything ?
        bz    .Lnopf
	chi   %r2,895
	bnh   .Lnotrunc
	la    %r2,895
.Lnotrunc:
	l     %r4,INITRD_START+4-PARMAREA(%r12)
 	clc   0(3,%r4),.L_hdr		       # if it is HDRx
 	bz    .Lagain1			       # skip dataset header
 	clc   0(3,%r4),.L_eof		       # if it is EOFx
 	bz    .Lagain1			       # skip dateset trailer
        la    %r5,0(%r4,%r2)
        lr    %r3,%r2
.Lidebc:
        tm    0(%r5),0x80                      # high order bit set ?
        bo    .Ldocv                           #  yes -> convert from EBCDIC
        ahi   %r5,-1
        bct   %r3,.Lidebc
        b     .Lnocv
.Ldocv:
        l     %r3,.Lcvtab
        tr    0(256,%r4),0(%r3)                # convert parameters to ascii
        tr    256(256,%r4),0(%r3)
        tr    512(256,%r4),0(%r3)
        tr    768(122,%r4),0(%r3)
.Lnocv: la    %r3,COMMAND_LINE-PARMAREA(%r12)  # load adr. of command line
	mvc   0(256,%r3),0(%r4)
	mvc   256(256,%r3),256(%r4)
	mvc   512(256,%r3),512(%r4)
	mvc   768(122,%r3),768(%r4)
        slr   %r0,%r0
        b     .Lcntlp
.Ldelspc:
        ic    %r0,0(%r2,%r3)
        chi   %r0,0x20                         # is it a space ?
        be    .Lcntlp
        ahi   %r2,1
        b     .Leolp
.Lcntlp:
        brct  %r2,.Ldelspc
.Leolp:
        slr   %r0,%r0
        stc   %r0,0(%r2,%r3)                   # terminate buffer
.Lnopf:

#
# load ramdisk from ipl device
#
.Lagain2:
 	l     %r2,INITRD_START+4-PARMAREA(%r12)# load adr. of ramdisk
        bas   %r14,.Lloader                    # load ramdisk
 	st    %r2,INITRD_SIZE+4-PARMAREA(%r12) # store size of ramdisk
        ltr   %r2,%r2
        bnz   .Lrdcont
        st    %r2,INITRD_START+4-PARMAREA(%r12)# no ramdisk found, null it
.Lrdcont:
	l     %r2,INITRD_START+4-PARMAREA(%r12)
	clc   0(3,%r2),.L_hdr		       # skip HDRx and EOFx 
	bz    .Lagain2
	clc   0(3,%r2),.L_eof
	bz    .Lagain2

#ifdef CONFIG_IPL_VM
#
# reset files in VM reader
#
        stidp __LC_CPUID                       # store cpuid
	tm    __LC_CPUID,0xff                  # running VM ?
	bno   .Lnoreset
        la    %r2,.Lreset              
        lhi   %r3,26
        .long 0x83230008
.Lnoreset:
#endif
	
#
# everything loaded, go for it
#
.Lnoload:
        l     %r1,.Lstartup
        br    %r1

.Lstartup: .long startup
.Lcvtab:.long  _ebcasc                         # ebcdic to ascii table
.Lreset:.byte  0xc3,0xc8,0xc1,0xd5,0xc7,0xc5,0x40,0xd9,0xc4,0xd9,0x40
        .byte  0xc1,0xd3,0xd3,0x40,0xd2,0xc5,0xc5,0xd7,0x40,0xd5,0xd6
        .byte  0xc8,0xd6,0xd3,0xc4             # "change rdr all keep nohold"
.L_eof: .long  0xc5d6c600       /* C'EOF' */
.L_hdr: .long  0xc8c4d900       /* C'HDR' */
#endif  /* CONFIG_IPL */

#
# SALIPL loader support. Based on a patch by Rob van der Heij.
# This entry point is called directly from the SALIPL loader and
# doesn't need a builtin ipl record.
#
        .org  0x800
	.globl start
start:
	stm   %r0,%r15,0x07b0		# store registers
	basr  %r12,%r0
.base:
	l     %r11,.parm
	l     %r8,.cmd			# pointer to command buffer

	ltr   %r9,%r9			# do we have SALIPL parameters?
	bp    .sk8x8

	mvc   0(64,%r8),0x00b0		# copy saved registers
	xc    64(240-64,%r8),0(%r8)	# remainder of buffer
	tr    0(64,%r8),.lowcase	
	b     .gotr
.sk8x8:
	mvc   0(240,%r8),0(%r9)		# copy iplparms into buffer
.gotr:
	l     %r10,.tbl			# EBCDIC to ASCII table
	tr    0(240,%r8),0(%r10)
	stidp __LC_CPUID		# Are we running on VM maybe
	cli   __LC_CPUID,0xff
	bnz   .test
	.long 0x83300060		# diag 3,0,x'0060' - storage size
	b     .done
.test:
	mvc   0x68(8),.pgmnw		# set up pgm check handler
	l     %r2,.fourmeg
	lr    %r3,%r2
	bctr  %r3,%r0			# 4M-1
.loop:  iske  %r0,%r3
	ar    %r3,%r2
.pgmx:
	sr    %r3,%r2
	la    %r3,1(%r3)
.done:
	l     %r1,.memsize
	st    %r3,4(%r1)
	slr   %r0,%r0
	st    %r0,INITRD_SIZE+4-PARMAREA(%r11)
	st    %r0,INITRD_START+4-PARMAREA(%r11)
	j     startup                   # continue with startup
.tbl:	.long _ebcasc			# translate table
.cmd:	.long COMMAND_LINE		# address of command line buffer
.parm:	.long PARMAREA
.fourmeg: .long 0x00400000      	# 4M
.pgmnw:	.long 0x00080000,.pgmx
.memsize: .long memory_size
.lowcase:
	.byte 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07 
	.byte 0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
	.byte 0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17 
	.byte 0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
	.byte 0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27 
	.byte 0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f
	.byte 0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37 
	.byte 0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f
	.byte 0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47 
	.byte 0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f
	.byte 0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57 
	.byte 0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f
	.byte 0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67 
	.byte 0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f
	.byte 0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77 
	.byte 0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f

	.byte 0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87 
	.byte 0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f
	.byte 0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97 
	.byte 0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f
	.byte 0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7 
	.byte 0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf
	.byte 0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7 
	.byte 0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf
	.byte 0xc0,0x81,0x82,0x83,0x84,0x85,0x86,0x87	# .abcdefg 
	.byte 0x88,0x89,0xca,0xcb,0xcc,0xcd,0xce,0xcf	# hi
	.byte 0xd0,0x91,0x92,0x93,0x94,0x95,0x96,0x97 	# .jklmnop
	.byte 0x98,0x99,0xda,0xdb,0xdc,0xdd,0xde,0xdf	# qr
	.byte 0xe0,0xe1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7	# ..stuvwx
	.byte 0xa8,0xa9,0xea,0xeb,0xec,0xed,0xee,0xef	# yz
	.byte 0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7 
	.byte 0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff

#
# startup-code at 0x10000, running in real mode
# this is called either by the ipl loader or directly by PSW restart
# or linload or SALIPL
#
        .org  0x10000
startup:basr  %r13,0                     # get base
.LPG1:  sll   %r13,1                     # remove high order bit
        srl   %r13,1
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
	lpsw  .Lwaitsclp-.LPG1(%r13)
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
	lh    %r1,.Lscpincr1-PARMAREA(%r4) # use this one if != 0
	chi   %r1,0x00
	jne   .Lscnd
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

.Lpmask:
	.byte 0
	.align 8
.Lcr:
	.quad 0x00  # place holder for cr0
.Lwaitsclp:
	.long 0x020A0000
	.quad .Lsclph
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
	mvc    __LC_DIAG44_OPCODE(8),.Lnop-.LPG1(%r13)
	diag   0,0,0x44			# test diag 0x44
	oi     7(%r12),32		# set diag44 flag
	mvc    __LC_DIAG44_OPCODE(8),.Ldiag44-.LPG1(%r13)
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
.Ldiag44:.long 0x83000044

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

#ifdef CONFIG_SHARED_KERNEL
	.org   0x100000
#endif
	
#
# startup-code, running in virtual mode
#
        .globl _stext
_stext:	basr  %r13,0                    # get base
.LPG2:
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
	oi     6(%r15),0x20             # enable sigp external interrupts
	oi     4(%r15),0x10             # switch on low address proctection
        lctlg  %c0,%c15,0(%r15)

#
        lam    0,15,.Laregs-.LPG2(%r13) # load access regs needed by uaccess
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

