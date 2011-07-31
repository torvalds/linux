/*
 * Copyright IBM Corp. 1999,2010
 *
 *    Author(s): Hartmut Penner <hp@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Rob van der Heij <rvdhei@iae.nl>
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>
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
 *
 */

#include <linux/init.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page.h>

#ifdef CONFIG_64BIT
#define ARCH_OFFSET	4
#else
#define ARCH_OFFSET	0
#endif

__HEAD
#ifndef CONFIG_IPL
	.org   0
	.long  0x00080000,0x80000000+startup	# Just a restart PSW
#else
#ifdef CONFIG_IPL_TAPE
#define IPL_BS 1024
	.org   0
	.long  0x00080000,0x80000000+iplstart	# The first 24 bytes are loaded
	.long  0x27000000,0x60000001		# by ipl to addresses 0-23.
	.long  0x02000000,0x20000000+IPL_BS	# (a PSW and two CCWs).
	.long  0x00000000,0x00000000		# external old psw
	.long  0x00000000,0x00000000		# svc old psw
	.long  0x00000000,0x00000000		# program check old psw
	.long  0x00000000,0x00000000		# machine check old psw
	.long  0x00000000,0x00000000		# io old psw
	.long  0x00000000,0x00000000
	.long  0x00000000,0x00000000
	.long  0x00000000,0x00000000
	.long  0x000a0000,0x00000058		# external new psw
	.long  0x000a0000,0x00000060		# svc new psw
	.long  0x000a0000,0x00000068		# program check new psw
	.long  0x000a0000,0x00000070		# machine check new psw
	.long  0x00080000,0x80000000+.Lioint	# io new psw

	.org   0x100
#
# subroutine for loading from tape
# Parameters:
#  R1 = device number
#  R2 = load address
.Lloader:
	st	%r14,.Lldret
	la	%r3,.Lorbread		# r3 = address of orb
	la	%r5,.Lirb		# r5 = address of irb
	st	%r2,.Lccwread+4 	# initialize CCW data addresses
	lctl	%c6,%c6,.Lcr6
	slr	%r2,%r2
.Lldlp:
	la	%r6,3			# 3 retries
.Lssch:
	ssch	0(%r3)			# load chunk of IPL_BS bytes
	bnz	.Llderr
.Lw4end:
	bas	%r14,.Lwait4io
	tm	8(%r5),0x82		# do we have a problem ?
	bnz	.Lrecov
	slr	%r7,%r7
	icm	%r7,3,10(%r5)		# get residual count
	lcr	%r7,%r7
	la	%r7,IPL_BS(%r7) 	# IPL_BS-residual=#bytes read
	ar	%r2,%r7 		# add to total size
	tm	8(%r5),0x01		# found a tape mark ?
	bnz	.Ldone
	l	%r0,.Lccwread+4 	# update CCW data addresses
	ar	%r0,%r7
	st	%r0,.Lccwread+4
	b	.Lldlp
.Ldone:
	l	%r14,.Lldret
	br	%r14			# r2 contains the total size
.Lrecov:
	bas	%r14,.Lsense		# do the sensing
	bct	%r6,.Lssch		# dec. retry count & branch
	b	.Llderr
#
# Sense subroutine
#
.Lsense:
	st	%r14,.Lsnsret
	la	%r7,.Lorbsense
	ssch	0(%r7)			# start sense command
	bnz	.Llderr
	bas	%r14,.Lwait4io
	l	%r14,.Lsnsret
	tm	8(%r5),0x82		# do we have a problem ?
	bnz	.Llderr
	br	%r14
#
# Wait for interrupt subroutine
#
.Lwait4io:
	lpsw	.Lwaitpsw
.Lioint:
	c	%r1,0xb8		# compare subchannel number
	bne	.Lwait4io
	tsch	0(%r5)
	slr	%r0,%r0
	tm	8(%r5),0x82		# do we have a problem ?
	bnz	.Lwtexit
	tm	8(%r5),0x04		# got device end ?
	bz	.Lwait4io
.Lwtexit:
	br	%r14
.Llderr:
	lpsw	.Lcrash

	.align	8
.Lorbread:
	.long	0x00000000,0x0080ff00,.Lccwread
	.align	8
.Lorbsense:
	.long	0x00000000,0x0080ff00,.Lccwsense
	.align	8
.Lccwread:
	.long	0x02200000+IPL_BS,0x00000000
.Lccwsense:
	.long	0x04200001,0x00000000
.Lwaitpsw:
	.long	0x020a0000,0x80000000+.Lioint

.Lirb:	.long	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.Lcr6:	.long	0xff000000
	.align	8
.Lcrash:.long	0x000a0000,0x00000000
.Lldret:.long	0
.Lsnsret: .long 0
#endif	/* CONFIG_IPL_TAPE */

#ifdef CONFIG_IPL_VM
#define IPL_BS	0x730
	.org	0
	.long	0x00080000,0x80000000+iplstart	# The first 24 bytes are loaded
	.long	0x02000018,0x60000050		# by ipl to addresses 0-23.
	.long	0x02000068,0x60000050		# (a PSW and two CCWs).
	.fill	80-24,1,0x40			# bytes 24-79 are discarded !!
	.long	0x020000f0,0x60000050		# The next 160 byte are loaded
	.long	0x02000140,0x60000050		# to addresses 0x18-0xb7
	.long	0x02000190,0x60000050		# They form the continuation
	.long	0x020001e0,0x60000050		# of the CCW program started
	.long	0x02000230,0x60000050		# by ipl and load the range
	.long	0x02000280,0x60000050		# 0x0f0-0x730 from the image
	.long	0x020002d0,0x60000050		# to the range 0x0f0-0x730
	.long	0x02000320,0x60000050		# in memory. At the end of
	.long	0x02000370,0x60000050		# the channel program the PSW
	.long	0x020003c0,0x60000050		# at location 0 is loaded.
	.long	0x02000410,0x60000050		# Initial processing starts
	.long	0x02000460,0x60000050		# at 0xf0 = iplstart.
	.long	0x020004b0,0x60000050
	.long	0x02000500,0x60000050
	.long	0x02000550,0x60000050
	.long	0x020005a0,0x60000050
	.long	0x020005f0,0x60000050
	.long	0x02000640,0x60000050
	.long	0x02000690,0x60000050
	.long	0x020006e0,0x20000050

	.org	0xf0
#
# subroutine for loading cards from the reader
#
.Lloader:
	la	%r3,.Lorb		# r2 = address of orb into r2
	la	%r5,.Lirb		# r4 = address of irb
	la	%r6,.Lccws
	la	%r7,20
.Linit:
	st	%r2,4(%r6)		# initialize CCW data addresses
	la	%r2,0x50(%r2)
	la	%r6,8(%r6)
	bct	7,.Linit

	lctl	%c6,%c6,.Lcr6		# set IO subclass mask
	slr	%r2,%r2
.Lldlp:
	ssch	0(%r3)			# load chunk of 1600 bytes
	bnz	.Llderr
.Lwait4irq:
	mvc	0x78(8),.Lnewpsw	# set up IO interrupt psw
	lpsw	.Lwaitpsw
.Lioint:
	c	%r1,0xb8		# compare subchannel number
	bne	.Lwait4irq
	tsch	0(%r5)

	slr	%r0,%r0
	ic	%r0,8(%r5)		# get device status
	chi	%r0,8			# channel end ?
	be	.Lcont
	chi	%r0,12			# channel end + device end ?
	be	.Lcont

	l	%r0,4(%r5)
	s	%r0,8(%r3)		# r0/8 = number of ccws executed
	mhi	%r0,10			# *10 = number of bytes in ccws
	lh	%r3,10(%r5)		# get residual count
	sr	%r0,%r3 		# #ccws*80-residual=#bytes read
	ar	%r2,%r0

	br	%r14			# r2 contains the total size

.Lcont:
	ahi	%r2,0x640		# add 0x640 to total size
	la	%r6,.Lccws
	la	%r7,20
.Lincr:
	l	%r0,4(%r6)		# update CCW data addresses
	ahi	%r0,0x640
	st	%r0,4(%r6)
	ahi	%r6,8
	bct	7,.Lincr

	b	.Lldlp
.Llderr:
	lpsw	.Lcrash

	.align	8
.Lorb:	.long	0x00000000,0x0080ff00,.Lccws
.Lirb:	.long	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.Lcr6:	.long	0xff000000
.Lloadp:.long	0,0
	.align	8
.Lcrash:.long	0x000a0000,0x00000000
.Lnewpsw:
	.long	0x00080000,0x80000000+.Lioint
.Lwaitpsw:
	.long	0x020a0000,0x80000000+.Lioint

	.align	8
.Lccws: .rept	19
	.long	0x02600050,0x00000000
	.endr
	.long	0x02200050,0x00000000
#endif	/* CONFIG_IPL_VM */

iplstart:
	lh	%r1,0xb8		# test if subchannel number
	bct	%r1,.Lnoload		#  is valid
	l	%r1,0xb8		# load ipl subchannel number
	la	%r2,IPL_BS		# load start address
	bas	%r14,.Lloader		# load rest of ipl image
	l	%r12,.Lparm		# pointer to parameter area
	st	%r1,IPL_DEVICE+ARCH_OFFSET-PARMAREA(%r12) # save ipl device number

#
# load parameter file from ipl device
#
.Lagain1:
	l	%r2,.Linitrd		# ramdisk loc. is temp
	bas	%r14,.Lloader		# load parameter file
	ltr	%r2,%r2 		# got anything ?
	bz	.Lnopf
	chi	%r2,895
	bnh	.Lnotrunc
	la	%r2,895
.Lnotrunc:
	l	%r4,.Linitrd
	clc	0(3,%r4),.L_hdr		# if it is HDRx
	bz	.Lagain1		# skip dataset header
	clc	0(3,%r4),.L_eof		# if it is EOFx
	bz	.Lagain1		# skip dateset trailer
	la	%r5,0(%r4,%r2)
	lr	%r3,%r2
	la	%r3,COMMAND_LINE-PARMAREA(%r12) # load adr. of command line
	mvc	0(256,%r3),0(%r4)
	mvc	256(256,%r3),256(%r4)
	mvc	512(256,%r3),512(%r4)
	mvc	768(122,%r3),768(%r4)
	slr	%r0,%r0
	b	.Lcntlp
.Ldelspc:
	ic	%r0,0(%r2,%r3)
	chi	%r0,0x20		# is it a space ?
	be	.Lcntlp
	ahi	%r2,1
	b	.Leolp
.Lcntlp:
	brct	%r2,.Ldelspc
.Leolp:
	slr	%r0,%r0
	stc	%r0,0(%r2,%r3)		# terminate buffer
.Lnopf:

#
# load ramdisk from ipl device
#
.Lagain2:
	l	%r2,.Linitrd		# addr of ramdisk
	st	%r2,INITRD_START+ARCH_OFFSET-PARMAREA(%r12)
	bas	%r14,.Lloader		# load ramdisk
	st	%r2,INITRD_SIZE+ARCH_OFFSET-PARMAREA(%r12) # store size of rd
	ltr	%r2,%r2
	bnz	.Lrdcont
	st	%r2,INITRD_START+ARCH_OFFSET-PARMAREA(%r12) # no ramdisk found
.Lrdcont:
	l	%r2,.Linitrd

	clc	0(3,%r2),.L_hdr		# skip HDRx and EOFx
	bz	.Lagain2
	clc	0(3,%r2),.L_eof
	bz	.Lagain2

#ifdef CONFIG_IPL_VM
#
# reset files in VM reader
#
	stidp	__LC_SAVE_AREA		# store cpuid
	tm	__LC_SAVE_AREA,0xff	# running VM ?
	bno	.Lnoreset
	la	%r2,.Lreset
	lhi	%r3,26
	diag	%r2,%r3,8
	la	%r5,.Lirb
	stsch	0(%r5)			# check if irq is pending
	tm	30(%r5),0x0f		# by verifying if any of the
	bnz	.Lwaitforirq		# activity or status control
	tm	31(%r5),0xff		# bits is set in the schib
	bz	.Lnoreset
.Lwaitforirq:
	mvc	0x78(8),.Lrdrnewpsw	# set up IO interrupt psw
.Lwaitrdrirq:
	lpsw	.Lrdrwaitpsw
.Lrdrint:
	c	%r1,0xb8		# compare subchannel number
	bne	.Lwaitrdrirq
	la	%r5,.Lirb
	tsch	0(%r5)
.Lnoreset:
	b	.Lnoload

	.align	8
.Lrdrnewpsw:
	.long	0x00080000,0x80000000+.Lrdrint
.Lrdrwaitpsw:
	.long	0x020a0000,0x80000000+.Lrdrint
#endif

#
# everything loaded, go for it
#
.Lnoload:
	l	%r1,.Lstartup
	br	%r1

.Linitrd:.long _end			# default address of initrd
.Lparm:	.long  PARMAREA
.Lstartup: .long startup
.Lreset:.byte	0xc3,0xc8,0xc1,0xd5,0xc7,0xc5,0x40,0xd9,0xc4,0xd9,0x40
	.byte	0xc1,0xd3,0xd3,0x40,0xd2,0xc5,0xc5,0xd7,0x40,0xd5,0xd6
	.byte	0xc8,0xd6,0xd3,0xc4	# "change rdr all keep nohold"
.L_eof: .long	0xc5d6c600	 /* C'EOF' */
.L_hdr: .long	0xc8c4d900	 /* C'HDR' */

#endif	/* CONFIG_IPL */

#
# SALIPL loader support. Based on a patch by Rob van der Heij.
# This entry point is called directly from the SALIPL loader and
# doesn't need a builtin ipl record.
#
	.org	0x800
	.globl	start
start:
	stm	%r0,%r15,0x07b0		# store registers
	basr	%r12,%r0
.base:
	l	%r11,.parm
	l	%r8,.cmd		# pointer to command buffer

	ltr	%r9,%r9			# do we have SALIPL parameters?
	bp	.sk8x8

	mvc	0(64,%r8),0x00b0	# copy saved registers
	xc	64(240-64,%r8),0(%r8)	# remainder of buffer
	tr	0(64,%r8),.lowcase
	b	.gotr
.sk8x8:
	mvc	0(240,%r8),0(%r9)	# copy iplparms into buffer
.gotr:
	slr	%r0,%r0
	st	%r0,INITRD_SIZE+ARCH_OFFSET-PARMAREA(%r11)
	st	%r0,INITRD_START+ARCH_OFFSET-PARMAREA(%r11)
	j	startup 		# continue with startup
.cmd:	.long	COMMAND_LINE		# address of command line buffer
.parm:	.long	PARMAREA
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
	.byte 0xd0,0x91,0x92,0x93,0x94,0x95,0x96,0x97	# .jklmnop
	.byte 0x98,0x99,0xda,0xdb,0xdc,0xdd,0xde,0xdf	# qr
	.byte 0xe0,0xe1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7	# ..stuvwx
	.byte 0xa8,0xa9,0xea,0xeb,0xec,0xed,0xee,0xef	# yz
	.byte 0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7
	.byte 0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff

#
# startup-code at 0x10000, running in absolute addressing mode
# this is called either by the ipl loader or directly by PSW restart
# or linload or SALIPL
#
	.org	0x10000
	.globl	startup
startup:
	basr	%r13,0			# get base
.LPG0:
	xc	0x200(256),0x200	# partially clear lowcore
	xc	0x300(256),0x300
	stck	__LC_LAST_UPDATE_CLOCK
	spt	5f-.LPG0(%r13)
	mvc	__LC_LAST_UPDATE_TIMER(8),5f-.LPG0(%r13)
#ifndef CONFIG_MARCH_G5
	# check capabilities against MARCH_{G5,Z900,Z990,Z9_109,Z10}
	xc	__LC_STFL_FAC_LIST(8),__LC_STFL_FAC_LIST
	stfl	__LC_STFL_FAC_LIST	# store facility list
	tm	__LC_STFL_FAC_LIST,0x01	# stfle available ?
	jz	0f
	la	%r0,0
	.insn	s,0xb2b00000,__LC_STFL_FAC_LIST	# store facility list extended
0:	l	%r0,__LC_STFL_FAC_LIST
	n	%r0,2f+8-.LPG0(%r13)
	cl	%r0,2f+8-.LPG0(%r13)
	jne	1f
	l	%r0,__LC_STFL_FAC_LIST+4
	n	%r0,2f+12-.LPG0(%r13)
	cl	%r0,2f+12-.LPG0(%r13)
	je	3f
1:	l	%r15,.Lstack-.LPG0(%r13)
	ahi	%r15,-96
	la	%r2,.Lals_string-.LPG0(%r13)
	l	%r3,.Lsclp_print-.LPG0(%r13)
	basr	%r14,%r3
	lpsw	2f-.LPG0(%r13)		# machine type not good enough, crash
.Lals_string:
	.asciz	"The Linux kernel requires more recent processor hardware"
.Lsclp_print:
	.long	_sclp_print_early
.Lstack:
	.long	0x8000 + (1<<(PAGE_SHIFT+THREAD_ORDER))
	.align 16
2:	.long	0x000a0000,0x8badcccc
#if defined(CONFIG_64BIT)
#if defined(CONFIG_MARCH_Z10)
	.long 0xc100efe3, 0xf0680000
#elif defined(CONFIG_MARCH_Z9_109)
	.long 0xc100efc3, 0x00000000
#elif defined(CONFIG_MARCH_Z990)
	.long 0xc0002000, 0x00000000
#elif defined(CONFIG_MARCH_Z900)
	.long 0xc0000000, 0x00000000
#endif
#else
#if defined(CONFIG_MARCH_Z10)
	.long 0x8100c880, 0x00000000
#elif defined(CONFIG_MARCH_Z9_109)
	.long 0x8100c880, 0x00000000
#elif defined(CONFIG_MARCH_Z990)
	.long 0x80002000, 0x00000000
#elif defined(CONFIG_MARCH_Z900)
	.long 0x80000000, 0x00000000
#endif
#endif
3:
#endif

#ifdef CONFIG_64BIT
	mvi	__LC_AR_MODE_ID,1	# set esame flag
	slr	%r0,%r0 		# set cpuid to zero
	lhi	%r1,2			# mode 2 = esame (dump)
	sigp	%r1,%r0,0x12		# switch to esame mode
	sam64				# switch to 64 bit mode
	larl	%r13,4f
	lmh	%r0,%r15,0(%r13)	# clear high-order half
	jg	startup_continue
4:	.fill	16,4,0x0
#else
	mvi	__LC_AR_MODE_ID,0	# set ESA flag (mode 0)
	l	%r13,4f-.LPG0(%r13)
	b	0(%r13)
	.align	8
4:	.long	startup_continue
#endif
	.align	8
5:	.long	0x7fffffff,0xffffffff

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
