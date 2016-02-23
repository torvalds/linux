/*
 * linux/arch/arm/boot/compressed/head-sharpsl.S
 *
 * Copyright (C) 2004-2005 Richard Purdie <rpurdie@rpsys.net>
 *
 * Sharp's bootloader doesn't pass any kind of machine ID
 * so we have to figure out the machine for ourselves...
 *
 * Support for Poodle, Corgi (SL-C700), Shepherd (SL-C750)
 * Husky (SL-C760), Tosa (SL-C6000), Spitz (SL-C3000),
 * Akita (SL-C1000) and Borzoi (SL-C3100).
 *
 */

#include <linux/linkage.h>
#include <asm/mach-types.h>

#ifndef CONFIG_PXA_SHARPSL
#error What am I doing here...
#endif

		.section        ".start", "ax"

__SharpSL_start:

/* Check for TC6393 - if found we have a Tosa */
	ldr	r7, .TOSAID
	mov	r1, #0x10000000		@ Base address of TC6393 chip
	mov 	r6, #0x03
	ldrh	r3, [r1, #8]		@ Load TC6393XB Revison: This is 0x0003
	cmp	r6, r3
	beq	.SHARPEND		@ Success -> tosa

/* Check for pxa270 - if found, branch */
	mrc p15, 0, r4, c0, c0		@ Get Processor ID
	and	r4, r4, #0xffffff00
	ldr	r3, .PXA270ID
	cmp	r4, r3
	beq	.PXA270

/* Check for w100 - if not found we have a Poodle */
	ldr	r1, .W100ADDR		@ Base address of w100 chip + regs offset

	mov r6, #0x31			@ Load Magic Init value
	str	r6, [r1, #0x280]	@ to SCRATCH_UMSK
	mov r5, #0x3000
.W100LOOP:
	subs r5, r5, #1
	bne .W100LOOP
	mov r6, #0x30			@ Load 2nd Magic Init value
	str	r6, [r1, #0x280]	@ to SCRATCH_UMSK

	ldr	r6, [r1, #0]		@ Load Chip ID
	ldr	r3, .W100ID
	ldr	r7, .POODLEID
	cmp	r6, r3
	bne	.SHARPEND			@ We have no w100 - Poodle

/* Check for pxa250 - if found we have a Corgi */
	ldr	r7, .CORGIID
	ldr	r3, .PXA255ID
	cmp	r4, r3
	blo	.SHARPEND			@ We have a PXA250 - Corgi

/* Check for 64MiB flash - if found we have a Shepherd */
	bl	get_flash_ids
	ldr	r7, .SHEPHERDID
	cmp	r3, #0x76			@ 64MiB flash
	beq	.SHARPEND			@ We have Shepherd

/* Must be a Husky */
	ldr	r7, .HUSKYID		@ Must be Husky
	b .SHARPEND

.PXA270:
/* Check for 16MiB flash - if found we have Spitz */
	bl	get_flash_ids
	ldr	r7, .SPITZID
	cmp	r3, #0x73			@ 16MiB flash
	beq	.SHARPEND			@ We have Spitz

/* Check for a second SCOOP chip - if found we have Borzoi */
	ldr	r1, .SCOOP2ADDR
	ldr	r7, .BORZOIID
	mov 	r6, #0x0140
	strh	r6, [r1]
	ldrh	r6, [r1]
	cmp	r6, #0x0140
	beq	.SHARPEND			@ We have Borzoi

/* Must be Akita */
	ldr	r7, .AKITAID
	b	.SHARPEND			@ We have Borzoi

.PXA255ID:
	.word	0x69052d00		@ PXA255 Processor ID
.PXA270ID:
	.word	0x69054100		@ PXA270 Processor ID
.W100ID:
	.word	0x57411002		@ w100 Chip ID
.W100ADDR:
	.word 	0x08010000		@ w100 Chip ID Reg Address
.SCOOP2ADDR:
	.word	0x08800040
.POODLEID:
	.word	MACH_TYPE_POODLE
.CORGIID:
	.word	MACH_TYPE_CORGI
.SHEPHERDID:
	.word	MACH_TYPE_SHEPHERD
.HUSKYID:
	.word	MACH_TYPE_HUSKY
.TOSAID:
	.word	MACH_TYPE_TOSA
.SPITZID:
	.word	MACH_TYPE_SPITZ
.AKITAID:
	.word	MACH_TYPE_AKITA
.BORZOIID:
	.word	MACH_TYPE_BORZOI

/*
 * Return: r2 - NAND Manufacturer ID
 *         r3 - NAND Chip ID
 * Corrupts: r1
 */
get_flash_ids:
	mov	r1, #0x0c000000		@ Base address of NAND chip
	ldrb	r3, [r1, #24]		@ Load FLASHCTL
	bic	r3, r3, #0x11		@ SET NCE
	orr	r3, r3, #0x0a		@ SET CLR + FLWP
	strb	r3, [r1, #24]		@ Save to FLASHCTL
	mov 	r2, #0x90		@ Command "readid"
	strb	r2, [r1, #20]		@ Save to FLASHIO
	bic	r3, r3, #2		@ CLR CLE
	orr	r3, r3, #4		@ SET ALE
	strb	r3, [r1, #24]		@ Save to FLASHCTL
	mov	r2, #0			@ Address 0x00
	strb	r2, [r1, #20]		@ Save to FLASHIO
	bic	r3, r3, #4		@ CLR ALE
	strb	r3, [r1, #24]		@ Save to FLASHCTL
.fids1:
	ldrb	r3, [r1, #24]		@ Load FLASHCTL
	tst	r3, #32			@ Is chip ready?
	beq	.fids1
	ldrb	r2, [r1, #20]		@ NAND Manufacturer ID
	ldrb	r3, [r1, #20]		@ NAND Chip ID
	mov	pc, lr

.SHARPEND:
