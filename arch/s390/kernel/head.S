/*
 * Copyright IBM Corp. 1999, 2010
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
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/ptrace.h>

#define ARCH_OFFSET	4

__HEAD

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
	.long	0x02000460,0x60000050		# at 0x200 = iplstart.
	.long	0x020004b0,0x60000050
	.long	0x02000500,0x60000050
	.long	0x02000550,0x60000050
	.long	0x020005a0,0x60000050
	.long	0x020005f0,0x60000050
	.long	0x02000640,0x60000050
	.long	0x02000690,0x60000050
	.long	0x020006e0,0x20000050

	.org	0x200

#
# subroutine to wait for end I/O
#
.Lirqwait:
	mvc	0x1f0(16),.Lnewpsw	# set up IO interrupt psw
	lpsw	.Lwaitpsw
.Lioint:
	br	%r14
	.align	8
.Lnewpsw:
	.quad	0x0000000080000000,.Lioint
.Lwaitpsw:
	.long	0x020a0000,0x80000000+.Lioint

#
# subroutine for loading cards from the reader
#
.Lloader:
	la	%r4,0(%r14)
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
	bas	%r14,.Lirqwait
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

	br	%r4			# r2 contains the total size

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

	.align	8
.Lccws: .rept	19
	.long	0x02600050,0x00000000
	.endr
	.long	0x02200050,0x00000000

iplstart:
	mvi	__LC_AR_MODE_ID,1	# set esame flag
	slr	%r0,%r0			# set cpuid to zero
	lhi	%r1,2			# mode 2 = esame (dump)
	sigp	%r1,%r0,0x12		# switch to esame mode
	bras	%r13,0f
	.fill	16,4,0x0
0:	lmh	%r0,%r15,0(%r13)	# clear high-order half of gprs
	sam31				# switch to 31 bit addressing mode
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

#
# reset files in VM reader
#
	stidp	.Lcpuid			# store cpuid
	tm	.Lcpuid,0xff		# running VM ?
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
	bas	%r14,.Lirqwait		# wait for IO interrupt
	c	%r1,0xb8		# compare subchannel number
	bne	.Lwaitforirq
	la	%r5,.Lirb
	tsch	0(%r5)
.Lnoreset:
	b	.Lnoload

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
	.align	8
.Lcpuid:.fill	8,1,0

#
# startup-code at 0x10000, running in absolute addressing mode
# this is called either by the ipl loader or directly by PSW restart
# or linload or SALIPL
#
	.org	0x10000
ENTRY(startup)
	j	.Lep_startup_normal
	.org	0x10008
#
# This is a list of s390 kernel entry points. At address 0x1000f the number of
# valid entry points is stored.
#
# IMPORTANT: Do not change this table, it is s390 kernel ABI!
#
	.ascii	"S390EP"
	.byte	0x00,0x01
#
# kdump startup-code at 0x10010, running in 64 bit absolute addressing mode
#
	.org	0x10010
ENTRY(startup_kdump)
	j	.Lep_startup_kdump
.Lep_startup_normal:
	mvi	__LC_AR_MODE_ID,1	# set esame flag
	slr	%r0,%r0 		# set cpuid to zero
	lhi	%r1,2			# mode 2 = esame (dump)
	sigp	%r1,%r0,0x12		# switch to esame mode
	bras	%r13,0f
	.fill	16,4,0x0
0:	lmh	%r0,%r15,0(%r13)	# clear high-order half of gprs
	sam64				# switch to 64 bit addressing mode
	basr	%r13,0			# get base
.LPG0:
	xc	0x200(256),0x200	# partially clear lowcore
	xc	0x300(256),0x300
	xc	0xe00(256),0xe00
	xc	0xf00(256),0xf00
	lctlg	%c0,%c15,0x200(%r0)	# initialize control registers
	stck	__LC_LAST_UPDATE_CLOCK
	spt	6f-.LPG0(%r13)
	mvc	__LC_LAST_UPDATE_TIMER(8),6f-.LPG0(%r13)
	l	%r15,.Lstack-.LPG0(%r13)
	ahi	%r15,-STACK_FRAME_OVERHEAD
	brasl	%r14,verify_facilities
# For uncompressed images, continue in
# arch/s390/kernel/head64.S. For compressed images, continue in
# arch/s390/boot/compressed/head.S.
	jg	startup_continue

.Lstack:
	.long	0x8000 + (1<<(PAGE_SHIFT+THREAD_SIZE_ORDER))
	.align	8
6:	.long	0x7fffffff,0xffffffff

#include "head_kdump.S"

#
# params at 10400 (setup.h)
#
	.org	PARMAREA
	.long	0,0			# IPL_DEVICE
	.long	0,0			# INITRD_START
	.long	0,0			# INITRD_SIZE
	.long	0,0			# OLDMEM_BASE
	.long	0,0			# OLDMEM_SIZE

	.org	COMMAND_LINE
	.byte	"root=/dev/ram0 ro"
	.byte	0

	.org	0x11000
