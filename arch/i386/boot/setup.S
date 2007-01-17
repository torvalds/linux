/*
 *	setup.S		Copyright (C) 1991, 1992 Linus Torvalds
 *
 * setup.s is responsible for getting the system data from the BIOS,
 * and putting them into the appropriate places in system memory.
 * both setup.s and system has been loaded by the bootblock.
 *
 * This code asks the bios for memory/disk/other parameters, and
 * puts them in a "safe" place: 0x90000-0x901FF, ie where the
 * boot-block used to be. It is then up to the protected mode
 * system to read them from there before the area is overwritten
 * for buffer-blocks.
 *
 * Move PS/2 aux init code to psaux.c
 * (troyer@saifr00.cfsat.Honeywell.COM) 03Oct92
 *
 * some changes and additional features by Christoph Niemann,
 * March 1993/June 1994 (Christoph.Niemann@linux.org)
 *
 * add APM BIOS checking by Stephen Rothwell, May 1994
 * (sfr@canb.auug.org.au)
 *
 * High load stuff, initrd support and position independency
 * by Hans Lermen & Werner Almesberger, February 1996
 * <lermen@elserv.ffm.fgan.de>, <almesber@lrc.epfl.ch>
 *
 * Video handling moved to video.S by Martin Mares, March 1996
 * <mj@k332.feld.cvut.cz>
 *
 * Extended memory detection scheme retwiddled by orc@pell.chi.il.us (david
 * parsons) to avoid loadlin confusion, July 1997
 *
 * Transcribed from Intel (as86) -> AT&T (gas) by Chris Noe, May 1999.
 * <stiker@northlink.com>
 *
 * Fix to work around buggy BIOSes which don't use carry bit correctly
 * and/or report extended memory in CX/DX for e801h memory size detection 
 * call.  As a result the kernel got wrong figures.  The int15/e801h docs
 * from Ralf Brown interrupt list seem to indicate AX/BX should be used
 * anyway.  So to avoid breaking many machines (presumably there was a reason
 * to orginally use CX/DX instead of AX/BX), we do a kludge to see
 * if CX/DX have been changed in the e801 call and if so use AX/BX .
 * Michael Miller, April 2001 <michaelm@mjmm.org>
 *
 * New A20 code ported from SYSLINUX by H. Peter Anvin. AMD Elan bugfixes
 * by Robert Schwebel, December 2001 <robert@schwebel.de>
 */

#include <asm/segment.h>
#include <linux/utsrelease.h>
#include <linux/compile.h>
#include <asm/boot.h>
#include <asm/e820.h>
#include <asm/page.h>
	
/* Signature words to ensure LILO loaded us right */
#define SIG1	0xAA55
#define SIG2	0x5A5A

INITSEG  = DEF_INITSEG		# 0x9000, we move boot here, out of the way
SYSSEG   = DEF_SYSSEG		# 0x1000, system loaded at 0x10000 (65536).
SETUPSEG = DEF_SETUPSEG		# 0x9020, this is the current segment
				# ... and the former contents of CS

DELTA_INITSEG = SETUPSEG - INITSEG	# 0x0020

.code16
.globl begtext, begdata, begbss, endtext, enddata, endbss

.text
begtext:
.data
begdata:
.bss
begbss:
.text

start:
	jmp	trampoline

# This is the setup header, and it must start at %cs:2 (old 0x9020:2)

		.ascii	"HdrS"		# header signature
		.word	0x0205		# header version number (>= 0x0105)
					# or else old loadlin-1.5 will fail)
realmode_swtch:	.word	0, 0		# default_switch, SETUPSEG
start_sys_seg:	.word	SYSSEG
		.word	kernel_version	# pointing to kernel version string
					# above section of header is compatible
					# with loadlin-1.5 (header v1.5). Don't
					# change it.

type_of_loader:	.byte	0		# = 0, old one (LILO, Loadlin,
					#      Bootlin, SYSLX, bootsect...)
					# See Documentation/i386/boot.txt for
					# assigned ids
	
# flags, unused bits must be zero (RFU) bit within loadflags
loadflags:
LOADED_HIGH	= 1			# If set, the kernel is loaded high
CAN_USE_HEAP	= 0x80			# If set, the loader also has set
					# heap_end_ptr to tell how much
					# space behind setup.S can be used for
					# heap purposes.
					# Only the loader knows what is free
#ifndef __BIG_KERNEL__
		.byte	0
#else
		.byte	LOADED_HIGH
#endif

setup_move_size: .word  0x8000		# size to move, when setup is not
					# loaded at 0x90000. We will move setup 
					# to 0x90000 then just before jumping
					# into the kernel. However, only the
					# loader knows how much data behind
					# us also needs to be loaded.

code32_start:				# here loaders can put a different
					# start address for 32-bit code.
#ifndef __BIG_KERNEL__
		.long	0x1000		#   0x1000 = default for zImage
#else
		.long	0x100000	# 0x100000 = default for big kernel
#endif

ramdisk_image:	.long	0		# address of loaded ramdisk image
					# Here the loader puts the 32-bit
					# address where it loaded the image.
					# This only will be read by the kernel.

ramdisk_size:	.long	0		# its size in bytes

bootsect_kludge:
		.long	0		# obsolete

heap_end_ptr:	.word	modelist+1024	# (Header version 0x0201 or later)
					# space from here (exclusive) down to
					# end of setup code can be used by setup
					# for local heap purposes.

pad1:		.word	0
cmd_line_ptr:	.long 0			# (Header version 0x0202 or later)
					# If nonzero, a 32-bit pointer
					# to the kernel command line.
					# The command line should be
					# located between the start of
					# setup and the end of low
					# memory (0xa0000), or it may
					# get overwritten before it
					# gets read.  If this field is
					# used, there is no longer
					# anything magical about the
					# 0x90000 segment; the setup
					# can be located anywhere in
					# low memory 0x10000 or higher.

ramdisk_max:	.long (-__PAGE_OFFSET-(512 << 20)-1) & 0x7fffffff
					# (Header version 0x0203 or later)
					# The highest safe address for
					# the contents of an initrd

kernel_alignment:  .long CONFIG_PHYSICAL_ALIGN 	#physical addr alignment
						#required for protected mode
						#kernel
#ifdef CONFIG_RELOCATABLE
relocatable_kernel:    .byte 1
#else
relocatable_kernel:    .byte 0
#endif
pad2:			.byte 0
pad3:			.word 0

trampoline:	call	start_of_setup
		.align 16
					# The offset at this point is 0x240
		.space	(0xeff-0x240+1) # E820 & EDD space (ending at 0xeff)
# End of setup header #####################################################

start_of_setup:
# Bootlin depends on this being done early
	movw	$0x01500, %ax
	movb	$0x81, %dl
	int	$0x13

#ifdef SAFE_RESET_DISK_CONTROLLER
# Reset the disk controller.
	movw	$0x0000, %ax
	movb	$0x80, %dl
	int	$0x13
#endif

# Set %ds = %cs, we know that SETUPSEG = %cs at this point
	movw	%cs, %ax		# aka SETUPSEG
	movw	%ax, %ds
# Check signature at end of setup
	cmpw	$SIG1, setup_sig1
	jne	bad_sig

	cmpw	$SIG2, setup_sig2
	jne	bad_sig

	jmp	good_sig1

# Routine to print asciiz string at ds:si
prtstr:
	lodsb
	andb	%al, %al
	jz	fin

	call	prtchr
	jmp	prtstr

fin:	ret

# Space printing
prtsp2:	call	prtspc		# Print double space
prtspc:	movb	$0x20, %al	# Print single space (note: fall-thru)

# Part of above routine, this one just prints ascii al
prtchr:	pushw	%ax
	pushw	%cx
	movw	$7,%bx
	movw	$0x01, %cx
	movb	$0x0e, %ah
	int	$0x10
	popw	%cx
	popw	%ax
	ret

beep:	movb	$0x07, %al
	jmp	prtchr
	
no_sig_mess: .string	"No setup signature found ..."

good_sig1:
	jmp	good_sig

# We now have to find the rest of the setup code/data
bad_sig:
	movw	%cs, %ax			# SETUPSEG
	subw	$DELTA_INITSEG, %ax		# INITSEG
	movw	%ax, %ds
	xorb	%bh, %bh
	movb	(497), %bl			# get setup sect from bootsect
	subw	$4, %bx				# LILO loads 4 sectors of setup
	shlw	$8, %bx				# convert to words (1sect=2^8 words)
	movw	%bx, %cx
	shrw	$3, %bx				# convert to segment
	addw	$SYSSEG, %bx
	movw	%bx, %cs:start_sys_seg
# Move rest of setup code/data to here
	movw	$2048, %di			# four sectors loaded by LILO
	subw	%si, %si
	pushw	%cs
	popw	%es
	movw	$SYSSEG, %ax
	movw	%ax, %ds
	rep
	movsw
	movw	%cs, %ax			# aka SETUPSEG
	movw	%ax, %ds
	cmpw	$SIG1, setup_sig1
	jne	no_sig

	cmpw	$SIG2, setup_sig2
	jne	no_sig

	jmp	good_sig

no_sig:
	lea	no_sig_mess, %si
	call	prtstr

no_sig_loop:
	hlt
	jmp	no_sig_loop

good_sig:
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax 		# aka INITSEG
	movw	%ax, %ds
# Check if an old loader tries to load a big-kernel
	testb	$LOADED_HIGH, %cs:loadflags	# Do we have a big kernel?
	jz	loader_ok			# No, no danger for old loaders.

	cmpb	$0, %cs:type_of_loader 		# Do we have a loader that
						# can deal with us?
	jnz	loader_ok			# Yes, continue.

	pushw	%cs				# No, we have an old loader,
	popw	%ds				# die. 
	lea	loader_panic_mess, %si
	call	prtstr

	jmp	no_sig_loop

loader_panic_mess: .string "Wrong loader, giving up..."

loader_ok:
# Get memory size (extended mem, kB)

	xorl	%eax, %eax
	movl	%eax, (0x1e0)
#ifndef STANDARD_MEMORY_BIOS_CALL
	movb	%al, (E820NR)
# Try three different memory detection schemes.  First, try
# e820h, which lets us assemble a memory map, then try e801h,
# which returns a 32-bit memory size, and finally 88h, which
# returns 0-64m

# method E820H:
# the memory map from hell.  e820h returns memory classified into
# a whole bunch of different types, and allows memory holes and
# everything.  We scan through this memory map and build a list
# of the first 32 memory areas, which we return at [E820MAP].
# This is documented at http://www.acpi.info/, in the ACPI 2.0 specification.

#define SMAP  0x534d4150

meme820:
	xorl	%ebx, %ebx			# continuation counter
	movw	$E820MAP, %di			# point into the whitelist
						# so we can have the bios
						# directly write into it.

jmpe820:
	movl	$0x0000e820, %eax		# e820, upper word zeroed
	movl	$SMAP, %edx			# ascii 'SMAP'
	movl	$20, %ecx			# size of the e820rec
	pushw	%ds				# data record.
	popw	%es
	int	$0x15				# make the call
	jc	bail820				# fall to e801 if it fails

	cmpl	$SMAP, %eax			# check the return is `SMAP'
	jne	bail820				# fall to e801 if it fails

#	cmpl	$1, 16(%di)			# is this usable memory?
#	jne	again820

	# If this is usable memory, we save it by simply advancing %di by
	# sizeof(e820rec).
	#
good820:
	movb	(E820NR), %al			# up to 128 entries
	cmpb	$E820MAX, %al
	jae	bail820

	incb	(E820NR)
	movw	%di, %ax
	addw	$20, %ax
	movw	%ax, %di
again820:
	cmpl	$0, %ebx			# check to see if
	jne	jmpe820				# %ebx is set to EOF
bail820:


# method E801H:
# memory size is in 1k chunksizes, to avoid confusing loadlin.
# we store the 0xe801 memory size in a completely different place,
# because it will most likely be longer than 16 bits.
# (use 1e0 because that's what Larry Augustine uses in his
# alternative new memory detection scheme, and it's sensible
# to write everything into the same place.)

meme801:
	stc					# fix to work around buggy
	xorw	%cx,%cx				# BIOSes which don't clear/set
	xorw	%dx,%dx				# carry on pass/error of
						# e801h memory size call
						# or merely pass cx,dx though
						# without changing them.
	movw	$0xe801, %ax
	int	$0x15
	jc	mem88

	cmpw	$0x0, %cx			# Kludge to handle BIOSes
	jne	e801usecxdx			# which report their extended
	cmpw	$0x0, %dx			# memory in AX/BX rather than
	jne	e801usecxdx			# CX/DX.  The spec I have read
	movw	%ax, %cx			# seems to indicate AX/BX 
	movw	%bx, %dx			# are more reasonable anyway...

e801usecxdx:
	andl	$0xffff, %edx			# clear sign extend
	shll	$6, %edx			# and go from 64k to 1k chunks
	movl	%edx, (0x1e0)			# store extended memory size
	andl	$0xffff, %ecx			# clear sign extend
 	addl	%ecx, (0x1e0)			# and add lower memory into
						# total size.

# Ye Olde Traditional Methode.  Returns the memory size (up to 16mb or
# 64mb, depending on the bios) in ax.
mem88:

#endif
	movb	$0x88, %ah
	int	$0x15
	movw	%ax, (2)

# Set the keyboard repeat rate to the max
	movw	$0x0305, %ax
	xorw	%bx, %bx
	int	$0x16

# Check for video adapter and its parameters and allow the
# user to browse video modes.
	call	video				# NOTE: we need %ds pointing
						# to bootsector

# Get hd0 data...
	xorw	%ax, %ax
	movw	%ax, %ds
	ldsw	(4 * 0x41), %si
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax		# aka INITSEG
	pushw	%ax
	movw	%ax, %es
	movw	$0x0080, %di
	movw	$0x10, %cx
	pushw	%cx
	cld
	rep
 	movsb
# Get hd1 data...
	xorw	%ax, %ax
	movw	%ax, %ds
	ldsw	(4 * 0x46), %si
	popw	%cx
	popw	%es
	movw	$0x0090, %di
	rep
	movsb
# Check that there IS a hd1 :-)
	movw	$0x01500, %ax
	movb	$0x81, %dl
	int	$0x13
	jc	no_disk1
	
	cmpb	$3, %ah
	je	is_disk1

no_disk1:
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax 		# aka INITSEG
	movw	%ax, %es
	movw	$0x0090, %di
	movw	$0x10, %cx
	xorw	%ax, %ax
	cld
	rep
	stosb
is_disk1:
# check for Micro Channel (MCA) bus
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax		# aka INITSEG
	movw	%ax, %ds
	xorw	%ax, %ax
	movw	%ax, (0xa0)			# set table length to 0
	movb	$0xc0, %ah
	stc
	int	$0x15				# moves feature table to es:bx
	jc	no_mca

	pushw	%ds
	movw	%es, %ax
	movw	%ax, %ds
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax		# aka INITSEG
	movw	%ax, %es
	movw	%bx, %si
	movw	$0xa0, %di
	movw	(%si), %cx
	addw	$2, %cx				# table length is a short
	cmpw	$0x10, %cx
	jc	sysdesc_ok

	movw	$0x10, %cx			# we keep only first 16 bytes
sysdesc_ok:
	rep
	movsb
	popw	%ds
no_mca:
#ifdef CONFIG_X86_VOYAGER
	movb	$0xff, 0x40	# flag on config found
	movb	$0xc0, %al
	mov	$0xff, %ah
	int	$0x15		# put voyager config info at es:di
	jc	no_voyager
	movw	$0x40, %si	# place voyager info in apm table
	cld
	movw	$7, %cx
voyager_rep:
	movb	%es:(%di), %al
	movb	%al,(%si)
	incw	%di
	incw	%si
	decw	%cx
	jnz	voyager_rep
no_voyager:	
#endif
# Check for PS/2 pointing device
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax		# aka INITSEG
	movw	%ax, %ds
	movb	$0, (0x1ff)			# default is no pointing device
	int	$0x11				# int 0x11: equipment list
	testb	$0x04, %al			# check if mouse installed
	jz	no_psmouse

	movb	$0xAA, (0x1ff)			# device present
no_psmouse:

#if defined(CONFIG_X86_SPEEDSTEP_SMI) || defined(CONFIG_X86_SPEEDSTEP_SMI_MODULE)
	movl	$0x0000E980, %eax		# IST Support 
	movl	$0x47534943, %edx		# Request value
	int	$0x15

	movl	%eax, (96)
	movl	%ebx, (100)
	movl	%ecx, (104)
	movl	%edx, (108)
#endif

#if defined(CONFIG_APM) || defined(CONFIG_APM_MODULE)
# Then check for an APM BIOS...
						# %ds points to the bootsector
	movw	$0, 0x40			# version = 0 means no APM BIOS
	movw	$0x05300, %ax			# APM BIOS installation check
	xorw	%bx, %bx
	int	$0x15
	jc	done_apm_bios			# Nope, no APM BIOS
	
	cmpw	$0x0504d, %bx			# Check for "PM" signature
	jne	done_apm_bios			# No signature, no APM BIOS

	andw	$0x02, %cx			# Is 32 bit supported?
	je	done_apm_bios			# No 32-bit, no (good) APM BIOS

	movw	$0x05304, %ax			# Disconnect first just in case
	xorw	%bx, %bx
	int	$0x15				# ignore return code
	movw	$0x05303, %ax			# 32 bit connect
	xorl	%ebx, %ebx
	xorw	%cx, %cx			# paranoia :-)
	xorw	%dx, %dx			#   ...
	xorl	%esi, %esi			#   ...
	xorw	%di, %di			#   ...
	int	$0x15
	jc	no_32_apm_bios			# Ack, error. 

	movw	%ax,  (66)			# BIOS code segment
	movl	%ebx, (68)			# BIOS entry point offset
	movw	%cx,  (72)			# BIOS 16 bit code segment
	movw	%dx,  (74)			# BIOS data segment
	movl	%esi, (78)			# BIOS code segment lengths
	movw	%di,  (82)			# BIOS data segment length
# Redo the installation check as the 32 bit connect
# modifies the flags returned on some BIOSs
	movw	$0x05300, %ax			# APM BIOS installation check
	xorw	%bx, %bx
	xorw	%cx, %cx			# paranoia
	int	$0x15
	jc	apm_disconnect			# error -> shouldn't happen

	cmpw	$0x0504d, %bx			# check for "PM" signature
	jne	apm_disconnect			# no sig -> shouldn't happen

	movw	%ax, (64)			# record the APM BIOS version
	movw	%cx, (76)			# and flags
	jmp	done_apm_bios

apm_disconnect:					# Tidy up
	movw	$0x05304, %ax			# Disconnect
	xorw	%bx, %bx
	int	$0x15				# ignore return code

	jmp	done_apm_bios

no_32_apm_bios:
	andw	$0xfffd, (76)			# remove 32 bit support bit
done_apm_bios:
#endif

#include "edd.S"

# Now we want to move to protected mode ...
	cmpw	$0, %cs:realmode_swtch
	jz	rmodeswtch_normal

	lcall	*%cs:realmode_swtch

	jmp	rmodeswtch_end

rmodeswtch_normal:
        pushw	%cs
	call	default_switch

rmodeswtch_end:
# Now we move the system to its rightful place ... but we check if we have a
# big-kernel. In that case we *must* not move it ...
	testb	$LOADED_HIGH, %cs:loadflags
	jz	do_move0			# .. then we have a normal low
						# loaded zImage
						# .. or else we have a high
						# loaded bzImage
	jmp	end_move			# ... and we skip moving

do_move0:
	movw	$0x100, %ax			# start of destination segment
	movw	%cs, %bp			# aka SETUPSEG
	subw	$DELTA_INITSEG, %bp		# aka INITSEG
	movw	%cs:start_sys_seg, %bx		# start of source segment
	cld
do_move:
	movw	%ax, %es			# destination segment
	incb	%ah				# instead of add ax,#0x100
	movw	%bx, %ds			# source segment
	addw	$0x100, %bx
	subw	%di, %di
	subw	%si, %si
	movw 	$0x800, %cx
	rep
	movsw
	cmpw	%bp, %bx			# assume start_sys_seg > 0x200,
						# so we will perhaps read one
						# page more than needed, but
						# never overwrite INITSEG
						# because destination is a
						# minimum one page below source
	jb	do_move

end_move:
# then we load the segment descriptors
	movw	%cs, %ax			# aka SETUPSEG
	movw	%ax, %ds
		
# Check whether we need to be downward compatible with version <=201
	cmpl	$0, cmd_line_ptr
	jne	end_move_self		# loader uses version >=202 features
	cmpb	$0x20, type_of_loader
	je	end_move_self		# bootsect loader, we know of it

# Boot loader doesnt support boot protocol version 2.02.
# If we have our code not at 0x90000, we need to move it there now.
# We also then need to move the params behind it (commandline)
# Because we would overwrite the code on the current IP, we move
# it in two steps, jumping high after the first one.
	movw	%cs, %ax
	cmpw	$SETUPSEG, %ax
	je	end_move_self

	cli					# make sure we really have
						# interrupts disabled !
						# because after this the stack
						# should not be used
	subw	$DELTA_INITSEG, %ax		# aka INITSEG
	movw	%ss, %dx
	cmpw	%ax, %dx
	jb	move_self_1

	addw	$INITSEG, %dx
	subw	%ax, %dx			# this will go into %ss after
						# the move
move_self_1:
	movw	%ax, %ds
	movw	$INITSEG, %ax			# real INITSEG
	movw	%ax, %es
	movw	%cs:setup_move_size, %cx
	std					# we have to move up, so we use
						# direction down because the
						# areas may overlap
	movw	%cx, %di
	decw	%di
	movw	%di, %si
	subw	$move_self_here+0x200, %cx
	rep
	movsb
	ljmp	$SETUPSEG, $move_self_here

move_self_here:
	movw	$move_self_here+0x200, %cx
	rep
	movsb
	movw	$SETUPSEG, %ax
	movw	%ax, %ds
	movw	%dx, %ss
end_move_self:					# now we are at the right place

#
# Enable A20.  This is at the very best an annoying procedure.
# A20 code ported from SYSLINUX 1.52-1.63 by H. Peter Anvin.
# AMD Elan bug fix by Robert Schwebel.
#

#if defined(CONFIG_X86_ELAN)
	movb $0x02, %al			# alternate A20 gate
	outb %al, $0x92			# this works on SC410/SC520
a20_elan_wait:
	call a20_test
	jz a20_elan_wait
	jmp a20_done
#endif


A20_TEST_LOOPS		=  32		# Iterations per wait
A20_ENABLE_LOOPS	= 255		# Total loops to try		


#ifndef CONFIG_X86_VOYAGER
a20_try_loop:

	# First, see if we are on a system with no A20 gate.
a20_none:
	call	a20_test
	jnz	a20_done

	# Next, try the BIOS (INT 0x15, AX=0x2401)
a20_bios:
	movw	$0x2401, %ax
	pushfl					# Be paranoid about flags
	int	$0x15
	popfl

	call	a20_test
	jnz	a20_done

	# Try enabling A20 through the keyboard controller
#endif /* CONFIG_X86_VOYAGER */
a20_kbc:
	call	empty_8042

#ifndef CONFIG_X86_VOYAGER
	call	a20_test			# Just in case the BIOS worked
	jnz	a20_done			# but had a delayed reaction.
#endif

	movb	$0xD1, %al			# command write
	outb	%al, $0x64
	call	empty_8042

	movb	$0xDF, %al			# A20 on
	outb	%al, $0x60
	call	empty_8042

#ifndef CONFIG_X86_VOYAGER
	# Wait until a20 really *is* enabled; it can take a fair amount of
	# time on certain systems; Toshiba Tecras are known to have this
	# problem.
a20_kbc_wait:
	xorw	%cx, %cx
a20_kbc_wait_loop:
	call	a20_test
	jnz	a20_done
	loop	a20_kbc_wait_loop

	# Final attempt: use "configuration port A"
a20_fast:
	inb	$0x92, %al			# Configuration Port A
	orb	$0x02, %al			# "fast A20" version
	andb	$0xFE, %al			# don't accidentally reset
	outb	%al, $0x92

	# Wait for configuration port A to take effect
a20_fast_wait:
	xorw	%cx, %cx
a20_fast_wait_loop:
	call	a20_test
	jnz	a20_done
	loop	a20_fast_wait_loop

	# A20 is still not responding.  Try frobbing it again.
	# 
	decb	(a20_tries)
	jnz	a20_try_loop
	
	movw	$a20_err_msg, %si
	call	prtstr

a20_die:
	hlt
	jmp	a20_die

a20_tries:
	.byte	A20_ENABLE_LOOPS

a20_err_msg:
	.ascii	"linux: fatal error: A20 gate not responding!"
	.byte	13, 10, 0

	# If we get here, all is good
a20_done:

#endif /* CONFIG_X86_VOYAGER */
# set up gdt and idt and 32bit start address
	lidt	idt_48				# load idt with 0,0
	xorl	%eax, %eax			# Compute gdt_base
	movw	%ds, %ax			# (Convert %ds:gdt to a linear ptr)
	shll	$4, %eax
	addl	%eax, code32
	addl	$gdt, %eax
	movl	%eax, (gdt_48+2)
	lgdt	gdt_48				# load gdt with whatever is
						# appropriate

# make sure any possible coprocessor is properly reset..
	xorw	%ax, %ax
	outb	%al, $0xf0
	call	delay

	outb	%al, $0xf1
	call	delay

# well, that went ok, I hope. Now we mask all interrupts - the rest
# is done in init_IRQ().
	movb	$0xFF, %al			# mask all interrupts for now
	outb	%al, $0xA1
	call	delay
	
	movb	$0xFB, %al			# mask all irq's but irq2 which
	outb	%al, $0x21			# is cascaded

# Well, that certainly wasn't fun :-(. Hopefully it works, and we don't
# need no steenking BIOS anyway (except for the initial loading :-).
# The BIOS-routine wants lots of unnecessary data, and it's less
# "interesting" anyway. This is how REAL programmers do it.
#
# Well, now's the time to actually move into protected mode. To make
# things as simple as possible, we do no register set-up or anything,
# we let the gnu-compiled 32-bit programs do that. We just jump to
# absolute address 0x1000 (or the loader supplied one),
# in 32-bit protected mode.
#
# Note that the short jump isn't strictly needed, although there are
# reasons why it might be a good idea. It won't hurt in any case.
	movw	$1, %ax				# protected mode (PE) bit
	lmsw	%ax				# This is it!
	jmp	flush_instr

flush_instr:
	xorw	%bx, %bx			# Flag to indicate a boot
	xorl	%esi, %esi			# Pointer to real-mode code
	movw	%cs, %si
	subw	$DELTA_INITSEG, %si
	shll	$4, %esi			# Convert to 32-bit pointer

# jump to startup_32 in arch/i386/boot/compressed/head.S
#	
# NOTE: For high loaded big kernels we need a
#	jmpi    0x100000,__BOOT_CS
#
#	but we yet haven't reloaded the CS register, so the default size 
#	of the target offset still is 16 bit.
#	However, using an operand prefix (0x66), the CPU will properly
#	take our 48 bit far pointer. (INTeL 80386 Programmer's Reference
#	Manual, Mixing 16-bit and 32-bit code, page 16-6)

	.byte 0x66, 0xea			# prefix + jmpi-opcode
code32:	.long	startup_32			# will be set to %cs+startup_32
	.word	__BOOT_CS
.code32
startup_32:
	movl $(__BOOT_DS), %eax
	movl %eax, %ds
	movl %eax, %es
	movl %eax, %fs
	movl %eax, %gs
	movl %eax, %ss

	xorl %eax, %eax
1:	incl %eax				# check that A20 really IS enabled
	movl %eax, 0x00000000			# loop forever if it isn't
	cmpl %eax, 0x00100000
	je 1b

	# Jump to the 32bit entry point
	jmpl *(code32_start - start + (DELTA_INITSEG << 4))(%esi)
.code16

# Here's a bunch of information about your current kernel..
kernel_version:	.ascii	UTS_RELEASE
		.ascii	" ("
		.ascii	LINUX_COMPILE_BY
		.ascii	"@"
		.ascii	LINUX_COMPILE_HOST
		.ascii	") "
		.ascii	UTS_VERSION
		.byte	0

# This is the default real mode switch routine.
# to be called just before protected mode transition
default_switch:
	cli					# no interrupts allowed !
	movb	$0x80, %al			# disable NMI for bootup
						# sequence
	outb	%al, $0x70
	lret


#ifndef CONFIG_X86_VOYAGER
# This routine tests whether or not A20 is enabled.  If so, it
# exits with zf = 0.
#
# The memory address used, 0x200, is the int $0x80 vector, which
# should be safe.

A20_TEST_ADDR = 4*0x80

a20_test:
	pushw	%cx
	pushw	%ax
	xorw	%cx, %cx
	movw	%cx, %fs			# Low memory
	decw	%cx
	movw	%cx, %gs			# High memory area
	movw	$A20_TEST_LOOPS, %cx
	movw	%fs:(A20_TEST_ADDR), %ax
	pushw	%ax
a20_test_wait:
	incw	%ax
	movw	%ax, %fs:(A20_TEST_ADDR)
	call	delay				# Serialize and make delay constant
	cmpw	%gs:(A20_TEST_ADDR+0x10), %ax
	loope	a20_test_wait

	popw	%fs:(A20_TEST_ADDR)
	popw	%ax
	popw	%cx
	ret	

#endif /* CONFIG_X86_VOYAGER */

# This routine checks that the keyboard command queue is empty
# (after emptying the output buffers)
#
# Some machines have delusions that the keyboard buffer is always full
# with no keyboard attached...
#
# If there is no keyboard controller, we will usually get 0xff
# to all the reads.  With each IO taking a microsecond and
# a timeout of 100,000 iterations, this can take about half a
# second ("delay" == outb to port 0x80). That should be ok,
# and should also be plenty of time for a real keyboard controller
# to empty.
#

empty_8042:
	pushl	%ecx
	movl	$100000, %ecx

empty_8042_loop:
	decl	%ecx
	jz	empty_8042_end_loop

	call	delay

	inb	$0x64, %al			# 8042 status port
	testb	$1, %al				# output buffer?
	jz	no_output

	call	delay
	inb	$0x60, %al			# read it
	jmp	empty_8042_loop

no_output:
	testb	$2, %al				# is input buffer full?
	jnz	empty_8042_loop			# yes - loop
empty_8042_end_loop:
	popl	%ecx
	ret

# Read the cmos clock. Return the seconds in al
gettime:
	pushw	%cx
	movb	$0x02, %ah
	int	$0x1a
	movb	%dh, %al			# %dh contains the seconds
	andb	$0x0f, %al
	movb	%dh, %ah
	movb	$0x04, %cl
	shrb	%cl, %ah
	aad
	popw	%cx
	ret

# Delay is needed after doing I/O
delay:
	outb	%al,$0x80
	ret

# Descriptor tables
#
# NOTE: The intel manual says gdt should be sixteen bytes aligned for
# efficiency reasons.  However, there are machines which are known not
# to boot with misaligned GDTs, so alter this at your peril!  If you alter
# GDT_ENTRY_BOOT_CS (in asm/segment.h) remember to leave at least two
# empty GDT entries (one for NULL and one reserved).
#
# NOTE:	On some CPUs, the GDT must be 8 byte aligned.  This is
# true for the Voyager Quad CPU card which will not boot without
# This directive.  16 byte aligment is recommended by intel.
#
	.align 16
gdt:
	.fill GDT_ENTRY_BOOT_CS,8,0

	.word	0xFFFF				# 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0				# base address = 0
	.word	0x9A00				# code read/exec
	.word	0x00CF				# granularity = 4096, 386
						#  (+5th nibble of limit)

	.word	0xFFFF				# 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0				# base address = 0
	.word	0x9200				# data read/write
	.word	0x00CF				# granularity = 4096, 386
						#  (+5th nibble of limit)
gdt_end:
	.align	4
	
	.word	0				# alignment byte
idt_48:
	.word	0				# idt limit = 0
	.word	0, 0				# idt base = 0L

	.word	0				# alignment byte
gdt_48:
	.word	gdt_end - gdt - 1		# gdt limit
	.word	0, 0				# gdt base (filled in later)

# Include video setup & detection code

#include "video.S"

# Setup signature -- must be last
setup_sig1:	.word	SIG1
setup_sig2:	.word	SIG2

# After this point, there is some free space which is used by the video mode
# handling code to store the temporary mode table (not used by the kernel).

modelist:

.text
endtext:
.data
enddata:
.bss
endbss:
