/*
 *  arch/cris/boot/compressed/head.S
 *
 *  Copyright (C) 1999, 2001 Axis Communications AB
 *
 *  Code that sets up the DRAM registers, calls the
 *  decompressor to unpack the piggybacked kernel, and jumps.
 *
 */

#include <linux/config.h>
#define ASSEMBLER_MACROS_ONLY
#include <asm/arch/sv_addr_ag.h>

#define RAM_INIT_MAGIC 0x56902387
	
	;; Exported symbols
	
	.globl	_input_data

	
	.text

	nop
	di

;; We need to initialze DRAM registers before we start using the DRAM
	
	cmp.d	RAM_INIT_MAGIC, r8	; Already initialized?
	beq	dram_init_finished
	nop
	
#include "../../lib/dram_init.S"
	
dram_init_finished:	
		
	;; Initiate the PA and PB ports

	move.b   CONFIG_ETRAX_DEF_R_PORT_PA_DATA, r0
	move.b   r0, [R_PORT_PA_DATA]

	move.b   CONFIG_ETRAX_DEF_R_PORT_PA_DIR, r0
	move.b   r0, [R_PORT_PA_DIR]

	move.b   CONFIG_ETRAX_DEF_R_PORT_PB_DATA, r0
	move.b   r0, [R_PORT_PB_DATA]

	move.b   CONFIG_ETRAX_DEF_R_PORT_PB_DIR, r0
	move.b   r0, [R_PORT_PB_DIR]

	;; Setup the stack to a suitably high address.
	;; We assume 8 MB is the minimum DRAM in an eLinux
	;; product and put the sp at the top for now.

	move.d	0x40800000, sp

	;; Figure out where the compressed piggyback image is
	;; in the flash (since we wont try to copy it to DRAM
	;; before unpacking). It is at _edata, but in flash.
	;; Use (_edata - basse) as offset to the current PC.
	
basse:	move.d	pc, r5
	and.d	0x7fffffff, r5	; strip any non-cache bit
	subq	2, r5		; compensate for the move.d pc instr
	move.d	r5, r0		; save for later - flash address of 'basse'
	add.d	_edata, r5
	sub.d	basse, r5	; r5 = flash address of '_edata'
	
	;; Copy text+data to DRAM
	
	move.d	basse, r1	; destination
	move.d	_edata, r2	; end destination
1:	move.w	[r0+], r3
	move.w	r3, [r1+]
	cmp.d	r2, r1
	bcs	1b
	nop

	move.d	r5, [_input_data] ; for the decompressor


	;; Clear the decompressors BSS (between _edata and _end)
	
	moveq	0, r0
	move.d	_edata, r1
	move.d	_end, r2
1:	move.w	r0, [r1+]
	cmp.d	r2, r1
	bcs	1b
	nop
	
	;; Do the decompression and save compressed size in _inptr

	jsr	_decompress_kernel
	
	;; Put start address of root partition in r9 so the kernel can use it
	;; when mounting from flash

	move.d	[_input_data], r9	; flash address of compressed kernel
	add.d	[_inptr], r9		; size of compressed kernel
	 
	;; Enter the decompressed kernel
	move.d	RAM_INIT_MAGIC, r8	; Tell kernel that DRAM is initialized
	jump	0x40004000	; kernel is linked to this address
	
	.data

_input_data:
	.dword	0		; used by the decompressor

#include "../../lib/hw_settings.S"
