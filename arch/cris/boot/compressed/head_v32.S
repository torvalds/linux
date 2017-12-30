/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Code that sets up the DRAM registers, calls the
 *  decompressor to unpack the piggybacked kernel, and jumps.
 *
 *  Copyright (C) 1999 - 2006, Axis Communications AB
 */

#define ASSEMBLER_MACROS_ONLY
#include <hwregs/asm/reg_map_asm.h>
#include <mach/startup.inc>

#define RAM_INIT_MAGIC 0x56902387
#define COMMAND_LINE_MAGIC 0x87109563

	;; Exported symbols

	.globl	input_data

	.text
start:
	di

	;; Start clocks for used blocks.
	START_CLOCKS

	;; Initialize the DRAM registers.
	cmp.d	RAM_INIT_MAGIC, $r8	; Already initialized?
	beq	dram_init_finished
	nop

#if defined CONFIG_ETRAXFS
#include "../../arch-v32/mach-fs/dram_init.S"
#elif defined CONFIG_CRIS_MACH_ARTPEC3
#include "../../arch-v32/mach-a3/dram_init.S"
#else
#error Only ETRAXFS and ARTPEC-3 supported!
#endif

dram_init_finished:

	GIO_INIT
	;; Setup the stack to a suitably high address.
	;; We assume 8 MB is the minimum DRAM and put
	;; the SP at the top for now.

	move.d	0x40800000, $sp

	;; Figure out where the compressed piggyback image is.
	;; It is either in [NOR] flash (we don't want to copy it
	;; to DRAM before unpacking), or copied to DRAM
	;; by the [NAND] flash boot loader.
	;; The piggyback image is at _edata, but relative to where the
	;; image is actually located in memory, not where it is linked
	;; (the decompressor is linked at 0x40700000+ and runs there).
	;; Use (_edata - herami) as offset to the current PC.

hereami:
	lapcq	., $r5		; get PC
	and.d	0x7fffffff, $r5	; strip any non-cache bit
	move.d	$r5, $r0	; source address of 'herami'
	add.d	_edata, $r5
	sub.d	hereami, $r5	; r5 = flash address of '_edata'
	move.d	hereami, $r1	; destination

	;; Copy text+data to DRAM

	move.d	_edata, $r2	; end destination
1:	move.w	[$r0+], $r3	; from herami+ source
	move.w	$r3, [$r1+]	; to hereami+ destination (linked address)
	cmp.d	$r2, $r1	; finish when destination == _edata
	bcs	1b
	nop
	move.d	input_data, $r0 ; for the decompressor
	move.d	$r5, [$r0]	; for the decompressor

	;; Clear the decompressors BSS (between _edata and _end)

	moveq	0, $r0
	move.d	_edata, $r1
	move.d	_end, $r2
1:	move.w	$r0, [$r1+]
	cmp.d	$r2, $r1
	bcs	1b
	nop

	;;  Save command line magic and address.
	move.d	_cmd_line_magic, $r0
	move.d  $r10, [$r0]
	move.d	_cmd_line_addr, $r0
	move.d  $r11, [$r0]

	;;  Save boot source indicator
	move.d	_boot_source, $r0
	move.d	$r12, [$r0]

	;; Do the decompression and save compressed size in _inptr

	jsr	decompress_kernel
	nop

	;; Restore boot source indicator
	move.d	_boot_source, $r12
	move.d	[$r12], $r12

	;; Restore command line magic and address.
	move.d  _cmd_line_magic, $r10
	move.d  [$r10], $r10
	move.d  _cmd_line_addr, $r11
	move.d  [$r11], $r11

	;; Put start address of root partition in r9 so the kernel can use it
	;; when mounting from flash
	move.d  input_data, $r0
	move.d	[$r0], $r9		; flash address of compressed kernel
	move.d  inptr, $r0
	add.d	[$r0], $r9		; size of compressed kernel
	cmp.d   0x40000000, $r9		; image in DRAM ?
	blo	enter_kernel		; no, must be [NOR] flash, jump
	nop				; delay slot
	and.d   0x001fffff, $r9		; assume compressed kernel was < 2M

enter_kernel:
	;; Enter the decompressed kernel
	move.d	RAM_INIT_MAGIC, $r8	; Tell kernel that DRAM is initialized
	jump	0x40004000	; kernel is linked to this address
	nop

	.data

input_data:
	.dword	0		; used by the decompressor
_cmd_line_magic:
	.dword 0
_cmd_line_addr:
	.dword 0
_boot_source:
	.dword 0

#if defined CONFIG_ETRAXFS
#include "../../arch-v32/mach-fs/hw_settings.S"
#elif defined CONFIG_CRIS_MACH_ARTPEC3
#include "../../arch-v32/mach-a3/hw_settings.S"
#else
#error Only ETRAXFS and ARTPEC-3 supported!
#endif
