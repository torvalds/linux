/*
 * linux/arch/arm/boot/compressed/head-sharpsl.S
 *
 * Copyright (C) 2004-2005 Richard Purdie <rpurdie@rpsys.net>
 *
 * Sharp's bootloader doesn't pass any kind of machine ID
 * so we have to figure out the machine for ourselves...
 *
 * Support for Poodle, Corgi (SL-C700), Shepherd (SL-C750)
 * and Husky (SL-C760).
 *
 */

#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/mach-types.h>

#ifndef CONFIG_PXA_SHARPSL
#error What am I doing here...
#endif

		.section        ".start", "ax"

__SharpSL_start:

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

	mrc p15, 0, r6, c0, c0	@ Get Processor ID
	and	r6, r6, #0xffffff00
	ldr	r7, .CORGIID
	ldr	r3, .PXA255ID
	cmp	r6, r3
	blo	.SHARPEND			@ We have a PXA250 - Corgi

	mov	r1, #0x0c000000		@ Base address of NAND chip
	ldrb	r3, [r1, #24]	@ Load FLASHCTL
	bic	r3, r3, #0x11		@ SET NCE
	orr	r3, r3, #0x0a		@ SET CLR + FLWP
	strb	r3, [r1, #24]	@ Save to FLASHCTL
	mov 	r2, #0x90		@ Command "readid"
	strb	r2, [r1, #20]	@ Save to FLASHIO
	bic	r3, r3, #2			@ CLR CLE
	orr	r3, r3, #4			@ SET ALE
	strb	r3, [r1, #24]	@ Save to FLASHCTL
	mov		r2, #0			@ Address 0x00
	strb	r2, [r1, #20]	@ Save to FLASHIO
	bic	r3, r3, #4			@ CLR ALE
	strb	r3, [r1, #24]	@ Save to FLASHCTL
.SHARP1:
	ldrb	r3, [r1, #24]	@ Load FLASHCTL
	tst	r3, #32				@ Is chip ready?
	beq	.SHARP1
	ldrb	r2, [r1, #20]	@ NAND Manufacturer ID
	ldrb	r3, [r1, #20]	@ NAND Chip ID
	ldr	r7, .SHEPHERDID
	cmp	r3, #0x76			@ 64MiB flash
	beq	.SHARPEND			@ We have Shepherd
	ldr	r7, .HUSKYID		@ Must be Husky
	b .SHARPEND

.PXA255ID:
	.word	0x69052d00		@ PXA255 Processor ID
.W100ID:
	.word	0x57411002		@ w100 Chip ID
.W100ADDR:
	.word 	0x08010000		@ w100 Chip ID Reg Address
.POODLEID:
	.word	MACH_TYPE_POODLE
.CORGIID:
	.word	MACH_TYPE_CORGI
.SHEPHERDID:
	.word	MACH_TYPE_SHEPHERD
.HUSKYID:
	.word	MACH_TYPE_HUSKY
.SHARPEND:


