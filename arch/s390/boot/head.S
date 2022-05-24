/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2010
 *
 *    Author(s): Hartmut Penner <hp@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Rob van der Heij <rvdhei@iae.nl>
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
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/sclp.h>
#include "boot.h"

#define EP_OFFSET	0x10008
#define EP_STRING	"S390EP"
#define IPL_BS		0x730

__HEAD
ipl_start:
	mvi	__LC_AR_MODE_ID,1	# set esame flag
	slr	%r0,%r0			# set cpuid to zero
	lhi	%r1,2			# mode 2 = esame (dump)
	sigp	%r1,%r0,0x12		# switch to esame mode
	sam64				# switch to 64 bit addressing mode
	lgh	%r1,__LC_SUBCHANNEL_ID	# test if subchannel number
	brctg	%r1,.Lnoload		#  is valid
	llgf	%r1,__LC_SUBCHANNEL_ID	# load ipl subchannel number
	lghi	%r2,IPL_BS		# load start address
	bras	%r14,.Lloader		# load rest of ipl image
	larl	%r12,parmarea		# pointer to parameter area
	stg	%r1,IPL_DEVICE-PARMAREA(%r12) # save ipl device number
#
# load parameter file from ipl device
#
.Lagain1:
	larl	%r2,_end		# ramdisk loc. is temp
	bras	%r14,.Lloader		# load parameter file
	ltgr	%r2,%r2			# got anything ?
	jz	.Lnopf
	lg	%r3,MAX_COMMAND_LINE_SIZE-PARMAREA(%r12)
	aghi	%r3,-1
	clgr	%r2,%r3
	jl	.Lnotrunc
	lgr	%r2,%r3
.Lnotrunc:
	larl	%r4,_end
	larl	%r13,.L_hdr
	clc	0(3,%r4),0(%r13)	# if it is HDRx
	jz	.Lagain1		# skip dataset header
	larl	%r13,.L_eof
	clc	0(3,%r4),0(%r13)	# if it is EOFx
	jz	.Lagain1		# skip dateset trailer
	lgr	%r5,%r2
	la	%r6,COMMAND_LINE-PARMAREA(%r12)
	lgr	%r7,%r2
	aghi	%r7,1
	mvcl	%r6,%r4
.Lnopf:
#
# load ramdisk from ipl device
#
.Lagain2:
	larl	%r2,_end		# addr of ramdisk
	stg	%r2,INITRD_START-PARMAREA(%r12)
	bras	%r14,.Lloader		# load ramdisk
	stg	%r2,INITRD_SIZE-PARMAREA(%r12) # store size of rd
	ltgr	%r2,%r2
	jnz	.Lrdcont
	stg	%r2,INITRD_START-PARMAREA(%r12) # no ramdisk found
.Lrdcont:
	larl	%r2,_end
	larl	%r13,.L_hdr		# skip HDRx and EOFx
	clc	0(3,%r2),0(%r13)
	jz	.Lagain2
	larl	%r13,.L_eof
	clc	0(3,%r2),0(%r13)
	jz	.Lagain2
#
# reset files in VM reader
#
	larl	%r13,.Lcpuid
	stidp	0(%r13)			# store cpuid
	tm	0(%r13),0xff		# running VM ?
	jno	.Lnoreset
	larl	%r2,.Lreset
	lghi	%r3,26
	diag	%r2,%r3,8
	larl	%r5,.Lirb
	stsch	0(%r5)			# check if irq is pending
	tm	30(%r5),0x0f		# by verifying if any of the
	jnz	.Lwaitforirq		# activity or status control
	tm	31(%r5),0xff		# bits is set in the schib
	jz	.Lnoreset
.Lwaitforirq:
	bras	%r14,.Lirqwait		# wait for IO interrupt
	c	%r1,__LC_SUBCHANNEL_ID	# compare subchannel number
	jne	.Lwaitforirq
	larl	%r5,.Lirb
	tsch	0(%r5)
.Lnoreset:
	j	.Lnoload
#
# everything loaded, go for it
#
.Lnoload:
	jg	startup
#
# subroutine to wait for end I/O
#
.Lirqwait:
	larl	%r13,.Lnewpswmask	# set up IO interrupt psw
	mvc	__LC_IO_NEW_PSW(8),0(%r13)
	stg	%r14,__LC_IO_NEW_PSW+8
	larl	%r13,.Lwaitpsw
	lpswe	0(%r13)
.Lioint:
#
# subroutine for loading cards from the reader
#
.Lloader:
	lgr	%r4,%r14
	larl	%r3,.Lorb		# r2 = address of orb into r2
	larl	%r5,.Lirb		# r4 = address of irb
	larl	%r6,.Lccws
	lghi	%r7,20
.Linit:
	st	%r2,4(%r6)		# initialize CCW data addresses
	la	%r2,0x50(%r2)
	la	%r6,8(%r6)
	brctg	%r7,.Linit
	larl	%r13,.Lcr6
	lctlg	%c6,%c6,0(%r13)
	xgr	%r2,%r2
.Lldlp:
	ssch	0(%r3)			# load chunk of 1600 bytes
	jnz	.Llderr
.Lwait4irq:
	bras	%r14,.Lirqwait
	c	%r1,__LC_SUBCHANNEL_ID	# compare subchannel number
	jne	.Lwait4irq
	tsch	0(%r5)
	xgr	%r0,%r0
	ic	%r0,8(%r5)		# get device status
	cghi	%r0,8			# channel end ?
	je	.Lcont
	cghi	%r0,12			# channel end + device end ?
	je	.Lcont
	llgf	%r0,4(%r5)
	sgf	%r0,8(%r3)		# r0/8 = number of ccws executed
	mghi	%r0,10			# *10 = number of bytes in ccws
	llgh	%r3,10(%r5)		# get residual count
	sgr	%r0,%r3			# #ccws*80-residual=#bytes read
	agr	%r2,%r0
	br	%r4			# r2 contains the total size
.Lcont:
	aghi	%r2,0x640		# add 0x640 to total size
	larl	%r6,.Lccws
	lghi	%r7,20
.Lincr:
	l	%r0,4(%r6)		# update CCW data addresses
	aghi	%r0,0x640
	st	%r0,4(%r6)
	aghi	%r6,8
	brctg	%r7,.Lincr
	j	.Lldlp
.Llderr:
	larl	%r13,.Lcrash
	lpsw	0(%r13)

	.align	8
.Lwaitpsw:
	.quad	0x0202000180000000,.Lioint
.Lnewpswmask:
	.quad	0x0000000180000000
	.align	8
.Lorb:	.long	0x00000000,0x0080ff00,.Lccws
.Lirb:	.long	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	.align	8
.Lcr6:	.quad	0x00000000ff000000
	.align	8
.Lcrash:.long	0x000a0000,0x00000000
	.align	8
.Lccws: .rept	19
	.long	0x02600050,0x00000000
	.endr
	.long	0x02200050,0x00000000
.Lreset:.byte	0xc3,0xc8,0xc1,0xd5,0xc7,0xc5,0x40,0xd9,0xc4,0xd9,0x40
	.byte	0xc1,0xd3,0xd3,0x40,0xd2,0xc5,0xc5,0xd7,0x40,0xd5,0xd6
	.byte	0xc8,0xd6,0xd3,0xc4	# "change rdr all keep nohold"
.L_eof: .long	0xc5d6c600	 /* C'EOF' */
.L_hdr: .long	0xc8c4d900	 /* C'HDR' */
	.align	8
.Lcpuid:.fill	8,1,0

#
# normal startup-code, running in absolute addressing mode
# this is called either by the ipl loader or directly by PSW restart
# or linload or SALIPL
#
	.org	STARTUP_NORMAL_OFFSET - IPL_START
SYM_CODE_START(startup)
	j	startup_normal
	.org	EP_OFFSET - IPL_START
#
# This is a list of s390 kernel entry points. At address 0x1000f the number of
# valid entry points is stored.
#
# IMPORTANT: Do not change this table, it is s390 kernel ABI!
#
	.ascii	EP_STRING
	.byte	0x00,0x01
#
# kdump startup-code, running in 64 bit absolute addressing mode
#
	.org	STARTUP_KDUMP_OFFSET - IPL_START
	j	startup_kdump
SYM_CODE_END(startup)
SYM_CODE_START_LOCAL(startup_normal)
	mvi	__LC_AR_MODE_ID,1	# set esame flag
	slr	%r0,%r0 		# set cpuid to zero
	lhi	%r1,2			# mode 2 = esame (dump)
	sigp	%r1,%r0,0x12		# switch to esame mode
	bras	%r13,0f
	.fill	16,4,0x0
0:	lmh	%r0,%r15,0(%r13)	# clear high-order half of gprs
	sam64				# switch to 64 bit addressing mode
	larl	%r13,.Lext_new_psw
	mvc	__LC_EXT_NEW_PSW(16),0(%r13)
	larl	%r13,.Lpgm_new_psw
	mvc	__LC_PGM_NEW_PSW(16),0(%r13)
	larl	%r13,.Lio_new_psw
	mvc	__LC_IO_NEW_PSW(16),0(%r13)
	xc	0x200(256),0x200	# partially clear lowcore
	xc	0x300(256),0x300
	xc	0xe00(256),0xe00
	xc	0xf00(256),0xf00
	larl	%r13,.Lctl
	lctlg	%c0,%c15,0(%r13)	# load control registers
	stcke	__LC_BOOT_CLOCK
	mvc	__LC_LAST_UPDATE_CLOCK(8),__LC_BOOT_CLOCK+1
	larl	%r13,6f
	spt	0(%r13)
	mvc	__LC_LAST_UPDATE_TIMER(8),0(%r13)
	larl	%r15,_stack_end-STACK_FRAME_OVERHEAD
	brasl	%r14,sclp_early_setup_buffer
	brasl	%r14,verify_facilities
	brasl	%r14,startup_kernel
SYM_CODE_END(startup_normal)

	.align	8
6:	.long	0x7fffffff,0xffffffff
.Lext_new_psw:
	.quad	0x0002000180000000,0x1b0	# disabled wait
.Lpgm_new_psw:
	.quad	0x0000000180000000,startup_pgm_check_handler
.Lio_new_psw:
	.quad	0x0002000180000000,0x1f0	# disabled wait
.Lctl:	.quad	0x04040000		# cr0: AFP registers & secondary space
	.quad	0			# cr1: primary space segment table
	.quad	0			# cr2: dispatchable unit control table
	.quad	0			# cr3: instruction authorization
	.quad	0xffff			# cr4: instruction authorization
	.quad	0			# cr5: primary-aste origin
	.quad	0			# cr6:	I/O interrupts
	.quad	0			# cr7:	secondary space segment table
	.quad	0x0000000000008000	# cr8:	access registers translation
	.quad	0			# cr9:	tracing off
	.quad	0			# cr10: tracing off
	.quad	0			# cr11: tracing off
	.quad	0			# cr12: tracing off
	.quad	0			# cr13: home space segment table
	.quad	0xc0000000		# cr14: machine check handling off
	.quad	0			# cr15: linkage stack operations

#include "head_kdump.S"

#
# This program check is active immediately after kernel start
# and until early_pgm_check_handler is set in kernel/early.c
# It simply saves general/control registers and psw in
# the save area and does disabled wait with a faulty address.
#
SYM_CODE_START_LOCAL(startup_pgm_check_handler)
	stmg	%r8,%r15,__LC_SAVE_AREA_SYNC
	la	%r8,4095
	stctg	%c0,%c15,__LC_CREGS_SAVE_AREA-4095(%r8)
	stmg	%r0,%r7,__LC_GPREGS_SAVE_AREA-4095(%r8)
	mvc	__LC_GPREGS_SAVE_AREA-4095+64(64,%r8),__LC_SAVE_AREA_SYNC
	mvc	__LC_PSW_SAVE_AREA-4095(16,%r8),__LC_PGM_OLD_PSW
	mvc	__LC_RETURN_PSW(16),__LC_PGM_OLD_PSW
	ni	__LC_RETURN_PSW,0xfc	# remove IO and EX bits
	ni	__LC_RETURN_PSW+1,0xfb	# remove MCHK bit
	oi	__LC_RETURN_PSW+1,0x2	# set wait state bit
	larl	%r9,.Lold_psw_disabled_wait
	stg	%r9,__LC_PGM_NEW_PSW+8
	larl	%r15,_dump_info_stack_end-STACK_FRAME_OVERHEAD
	brasl	%r14,print_pgm_check_info
.Lold_psw_disabled_wait:
	la	%r8,4095
	lmg	%r0,%r15,__LC_GPREGS_SAVE_AREA-4095(%r8)
	lpswe	__LC_RETURN_PSW		# disabled wait
SYM_CODE_END(startup_pgm_check_handler)
