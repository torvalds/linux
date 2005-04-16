/*
 *	bootsect.S		Copyright (C) 1991, 1992 Linus Torvalds
 *
 *	modified by Drew Eckhardt
 *	modified by Bruce Evans (bde)
 *	modified by Chris Noe (May 1999) (as86 -> gas)
 *	gutted by H. Peter Anvin (Jan 2003)
 *
 * BIG FAT NOTE: We're in real mode using 64k segments.  Therefore segment
 * addresses must be multiplied by 16 to obtain their respective linear
 * addresses. To avoid confusion, linear addresses are written using leading
 * hex while segment addresses are written as segment:offset.
 *
 */

#include <asm/boot.h>

SETUPSECTS	= 4			/* default nr of setup-sectors */
BOOTSEG		= 0x07C0		/* original address of boot-sector */
INITSEG		= DEF_INITSEG		/* we move boot here - out of the way */
SETUPSEG	= DEF_SETUPSEG		/* setup starts here */
SYSSEG		= DEF_SYSSEG		/* system loaded at 0x10000 (65536) */
SYSSIZE		= DEF_SYSSIZE		/* system size: # of 16-byte clicks */
					/* to be loaded */
ROOT_DEV	= 0 			/* ROOT_DEV is now written by "build" */
SWAP_DEV	= 0			/* SWAP_DEV is now written by "build" */

#ifndef SVGA_MODE
#define SVGA_MODE ASK_VGA
#endif

#ifndef RAMDISK
#define RAMDISK 0
#endif

#ifndef ROOT_RDONLY
#define ROOT_RDONLY 1
#endif

.code16
.text

.global _start
_start:

	# Normalize the start address
	jmpl	$BOOTSEG, $start2

start2:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	$0x7c00, %sp
	sti
	cld

	movw	$bugger_off_msg, %si

msg_loop:
	lodsb
	andb	%al, %al
	jz	die
	movb	$0xe, %ah
	movw	$7, %bx
 	int	$0x10
	jmp	msg_loop

die:
	# Allow the user to press a key, then reboot
	xorw	%ax, %ax
	int	$0x16
	int	$0x19
	
	# int 0x19 should never return.  In case it does anyway,
	# invoke the BIOS reset code...
	ljmp	$0xf000,$0xfff0


bugger_off_msg:
	.ascii	"Direct booting from floppy is no longer supported.\r\n"
	.ascii	"Please use a boot loader program instead.\r\n"
	.ascii	"\n"
	.ascii	"Remove disk and press any key to reboot . . .\r\n"
	.byte	0


	# Kernel attributes; used by setup

	.org 497
setup_sects:	.byte SETUPSECTS
root_flags:	.word ROOT_RDONLY
syssize:	.word SYSSIZE
swap_dev:	.word SWAP_DEV
ram_size:	.word RAMDISK
vid_mode:	.word SVGA_MODE
root_dev:	.word ROOT_DEV
boot_flag:	.word 0xAA55
