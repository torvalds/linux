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
	movb	$0, (EDD_MBR_SIG_NR_BUF)
	movb	$0, (EDDNR)

# Check the command line for two options:
# edd=of  disables EDD completely  (edd=off)
# edd=sk  skips the MBR test    (edd=skipmbr)
	pushl	%esi
    	cmpl	$0, %cs:cmd_line_ptr
	jz	done_cl
	movl	%cs:(cmd_line_ptr), %esi
# ds:esi has the pointer to the command line now
	movl	$(COMMAND_LINE_SIZE-7), %ecx
# loop through kernel command line one byte at a time
cl_loop:
	cmpl	$EDD_CL_EQUALS, (%si)
	jz	found_edd_equals
	incl	%esi
	loop	cl_loop
	jmp	done_cl
found_edd_equals:
# only looking at first two characters after equals
    	addl	$4, %esi
	cmpw	$EDD_CL_OFF, (%si)	# edd=of
	jz	do_edd_off
	cmpw	$EDD_CL_SKIP, (%si)	# edd=sk
	jz	do_edd_skipmbr
	jmp	done_cl
do_edd_skipmbr:
    	popl	%esi
	jmp	edd_start
do_edd_off:
	popl	%esi
	jmp	edd_done
done_cl:
	popl	%esi


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
