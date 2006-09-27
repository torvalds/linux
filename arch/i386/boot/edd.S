/*
 * BIOS Enhanced Disk Drive support
 * Copyright (C) 2002, 2003, 2004 Dell, Inc.
 * by Matt Domsch <Matt_Domsch@dell.com> October 2002
 * conformant to T13 Committee www.t13.org
 *   projects 1572D, 1484D, 1386D, 1226DT
 * disk signature read by Matt Domsch <Matt_Domsch@dell.com>
 *	and Andrew Wilks <Andrew_Wilks@dell.com> September 2003, June 2004
 * legacy CHS retrieval by Patrick J. LoPresti <patl@users.sourceforge.net>
 *      March 2004
 * Command line option parsing, Matt Domsch, November 2004
 */

#include <linux/edd.h>
#include <asm/setup.h>

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)

# It is assumed that %ds == INITSEG here

	movb	$0, (EDD_MBR_SIG_NR_BUF)
	movb	$0, (EDDNR)

# Check the command line for options:
# edd=of  disables EDD completely  (edd=off)
# edd=sk  skips the MBR test    (edd=skipmbr)
# edd=on  re-enables EDD (edd=on)

	pushl	%esi
	movw	$edd_mbr_sig_start, %di	# Default to edd=on

	movl	%cs:(cmd_line_ptr), %esi
	andl	%esi, %esi
	jz	old_cl			# Old boot protocol?

# Convert to a real-mode pointer in fs:si
	movl	%esi, %eax
	shrl	$4, %eax
	movw	%ax, %fs
	andw	$0xf, %si
	jmp	have_cl_pointer

# Old-style boot protocol?
old_cl:
	push	%ds			# aka INITSEG
	pop	%fs

	cmpw	$0xa33f, (0x20)
	jne	done_cl			# No command line at all?
	movw	(0x22), %si		# Pointer relative to INITSEG

# fs:si has the pointer to the command line now
have_cl_pointer:

# Loop through kernel command line one byte at a time.  Just in
# case the loader is buggy and failed to null-terminate the command line
# terminate if we get close enough to the end of the segment that we
# cannot fit "edd=XX"...
cl_atspace:
	cmpw	$-5, %si		# Watch for segment wraparound
	jae	done_cl
	movl	%fs:(%si), %eax
	andb	%al, %al		# End of line?
	jz	done_cl
	cmpl	$EDD_CL_EQUALS, %eax
	jz	found_edd_equals
	cmpb	$0x20, %al		# <= space consider whitespace
	ja	cl_skipword
	incw	%si
	jmp	cl_atspace

cl_skipword:
	cmpw	$-5, %si		# Watch for segment wraparound
	jae	done_cl
	movb	%fs:(%si), %al		# End of string?
	andb	%al, %al
	jz	done_cl
	cmpb	$0x20, %al
	jbe	cl_atspace
	incw	%si
	jmp	cl_skipword

found_edd_equals:
# only looking at first two characters after equals
# late overrides early on the command line, so keep going after finding something
	movw	%fs:4(%si), %ax
	cmpw	$EDD_CL_OFF, %ax	# edd=of
	je	do_edd_off
	cmpw	$EDD_CL_SKIP, %ax	# edd=sk
	je	do_edd_skipmbr
	cmpw	$EDD_CL_ON, %ax		# edd=on
	je	do_edd_on
	jmp	cl_skipword
do_edd_skipmbr:
	movw	$edd_start, %di
	jmp	cl_skipword
do_edd_off:
	movw	$edd_done, %di
	jmp	cl_skipword
do_edd_on:
	movw	$edd_mbr_sig_start, %di
	jmp	cl_skipword

done_cl:
	popl	%esi
	jmpw	*%di

# Read the first sector of each BIOS disk device and store the 4-byte signature
edd_mbr_sig_start:
	movb	$0x80, %dl			# from device 80
	movw	$EDD_MBR_SIG_BUF, %bx		# store buffer ptr in bx
edd_mbr_sig_read:
	movl	$0xFFFFFFFF, %eax
	movl	%eax, (%bx)			# assume failure
	pushw	%bx
	movb	$READ_SECTORS, %ah
	movb	$1, %al				# read 1 sector
	movb	$0, %dh				# at head 0
	movw	$1, %cx				# cylinder 0, sector 0
	pushw	%es
	pushw	%ds
	popw	%es
    	movw	$EDDBUF, %bx			# disk's data goes into EDDBUF
	pushw	%dx             # work around buggy BIOSes
	stc                     # work around buggy BIOSes
	int	$0x13
	sti                     # work around buggy BIOSes
	popw	%dx
	popw	%es
	popw	%bx
	jc	edd_mbr_sig_done		# on failure, we're done.
	cmpb	$0, %ah		# some BIOSes do not set CF
	jne	edd_mbr_sig_done		# on failure, we're done.
	movl	(EDDBUF+EDD_MBR_SIG_OFFSET), %eax # read sig out of the MBR
	movl	%eax, (%bx)			# store success
	incb	(EDD_MBR_SIG_NR_BUF)		# note that we stored something
	incb	%dl				# increment to next device
	addw	$4, %bx				# increment sig buffer ptr
	cmpb	$EDD_MBR_SIG_MAX, (EDD_MBR_SIG_NR_BUF)	# Out of space?
	jb	edd_mbr_sig_read		# keep looping
edd_mbr_sig_done:

# Do the BIOS Enhanced Disk Drive calls
# This consists of two calls:
#    int 13h ah=41h "Check Extensions Present"
#    int 13h ah=48h "Get Device Parameters"
#    int 13h ah=08h "Legacy Get Device Parameters"
#
# A buffer of size EDDMAXNR*(EDDEXTSIZE+EDDPARMSIZE) is reserved for our use
# in the boot_params at EDDBUF.  The first four bytes of which are
# used to store the device number, interface support map and version
# results from fn41.  The next four bytes are used to store the legacy
# cylinders, heads, and sectors from fn08. The following 74 bytes are used to
# store the results from fn48.  Starting from device 80h, fn41, then fn48
# are called and their results stored in EDDBUF+n*(EDDEXTSIZE+EDDPARMIZE).
# Then the pointer is incremented to store the data for the next call.
# This repeats until either a device doesn't exist, or until EDDMAXNR
# devices have been stored.
# The one tricky part is that ds:si always points EDDEXTSIZE bytes into
# the structure, and the fn41 and fn08 results are stored at offsets
# from there.  This removes the need to increment the pointer for
# every store, and leaves it ready for the fn48 call.
# A second one-byte buffer, EDDNR, in the boot_params stores
# the number of BIOS devices which exist, up to EDDMAXNR.
# In setup.c, copy_edd() stores both boot_params buffers away
# for later use, as they would get overwritten otherwise.
# This code is sensitive to the size of the structs in edd.h
edd_start:
						# %ds points to the bootsector
       						# result buffer for fn48
	movw	$EDDBUF+EDDEXTSIZE, %si		# in ds:si, fn41 results
						# kept just before that
	movb	$0x80, %dl			# BIOS device 0x80

edd_check_ext:
	movb	$CHECKEXTENSIONSPRESENT, %ah    # Function 41
	movw	$EDDMAGIC1, %bx			# magic
	int	$0x13				# make the call
	jc	edd_done			# no more BIOS devices

	cmpw	$EDDMAGIC2, %bx			# is magic right?
	jne	edd_next			# nope, next...

	movb	%dl, %ds:-8(%si)		# store device number
	movb	%ah, %ds:-7(%si)		# store version
	movw	%cx, %ds:-6(%si)		# store extensions
	incb	(EDDNR)				# note that we stored something

edd_get_device_params:
	movw	$EDDPARMSIZE, %ds:(%si)		# put size
	movw	$0x0, %ds:2(%si)		# work around buggy BIOSes
	movb	$GETDEVICEPARAMETERS, %ah	# Function 48
	int	$0x13				# make the call
						# Don't check for fail return
						# it doesn't matter.
edd_get_legacy_chs:
	xorw    %ax, %ax
	movw    %ax, %ds:-4(%si)
	movw    %ax, %ds:-2(%si)
        # Ralf Brown's Interrupt List says to set ES:DI to
	# 0000h:0000h "to guard against BIOS bugs"
	pushw   %es
	movw    %ax, %es
	movw    %ax, %di
	pushw   %dx                             # legacy call clobbers %dl
	movb    $LEGACYGETDEVICEPARAMETERS, %ah # Function 08
	int     $0x13                           # make the call
	jc      edd_legacy_done                 # failed
	movb    %cl, %al                        # Low 6 bits are max
	andb    $0x3F, %al                      #   sector number
	movb	%al, %ds:-1(%si)                # Record max sect
	movb    %dh, %ds:-2(%si)                # Record max head number
	movb    %ch, %al                        # Low 8 bits of max cyl
	shr     $6, %cl
	movb    %cl, %ah                        # High 2 bits of max cyl
	movw    %ax, %ds:-4(%si)

edd_legacy_done:
	popw    %dx
	popw    %es
	movw	%si, %ax			# increment si
	addw	$EDDPARMSIZE+EDDEXTSIZE, %ax
	movw	%ax, %si

edd_next:
	incb	%dl				# increment to next device
	cmpb	$EDDMAXNR, (EDDNR) 		# Out of space?
	jb	edd_check_ext			# keep looping

edd_done:
#endif
