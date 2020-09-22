/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-09-23
 */
	.syntax unified
	.arch armv7-a
	.fpu softvfp
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 4
	.eabi_attribute 34, 1
	.eabi_attribute 18, 2
	.thumb
	.file	"rk_ftl_arm_v7.S"
#APP
	.syntax unified
	.thumb
	.text
	.align	1
	.global	FlashMemCmp8
	.thumb
	.thumb_func
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L10
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrb	r3, [r3]	@ zero_extendqisi2
	cbz	r3, .L4
	ldrb	r4, [r0, #1]	@ zero_extendqisi2
	ldrb	r3, [r1, #1]	@ zero_extendqisi2
	cmp	r4, r3
	beq	.L8
	movs	r3, #0
.L4:
	cmp	r3, r2
	beq	.L8
	ldrb	r5, [r0, r3]	@ zero_extendqisi2
	ldrb	r4, [r1, r3]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r5, r4
	beq	.L4
	mov	r0, r3
	pop	{r4, r5, pc}
.L8:
	movs	r0, #0
	pop	{r4, r5, pc}
.L11:
	.align	2
.L10:
	.word	.LANCHOR0
	.fnend
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	1
	.global	FlashRsvdBlkChk
	.thumb
	.thumb_func
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L15
	ldrb	r2, [r3, #1]	@ zero_extendqisi2
	ldr	r3, [r3, #4]
	muls	r3, r2, r3
	cmp	r1, r3
	bcs	.L14
	adds	r0, r0, #0
	it	ne
	movne	r0, #1
	bx	lr
.L14:
	movs	r0, #1
	bx	lr
.L16:
	.align	2
.L15:
	.word	.LANCHOR0
	.fnend
	.size	FlashRsvdBlkChk, .-FlashRsvdBlkChk
	.align	1
	.global	FlashGetRandomizer
	.thumb
	.thumb_func
	.type	FlashGetRandomizer, %function
FlashGetRandomizer:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L25
	and	r2, r1, #127
	push	{r4, lr}
	.save {r4, lr}
	ldrh	r4, [r3, r2, lsl #1]
	ldr	r3, .L25+4
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cbz	r3, .L18
	bl	FlashRsvdBlkChk
	cbz	r0, .L18
	orr	r4, r4, #-1073741824
.L18:
	mov	r0, r4
	pop	{r4, pc}
.L26:
	.align	2
.L25:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashGetRandomizer, .-FlashGetRandomizer
	.align	1
	.global	FlashSetRandomizer
	.thumb
	.thumb_func
	.type	FlashSetRandomizer, %function
FlashSetRandomizer:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L35
	and	r2, r1, #127
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r3, r2, lsl #1]
	ldr	r3, .L35+4
	ldrb	r2, [r3, #8]	@ zero_extendqisi2
	mov	r4, r3
	cbz	r2, .L28
	bl	FlashRsvdBlkChk
	cbz	r0, .L28
	orr	r5, r5, #-1073741824
.L28:
	add	r4, r4, r6, lsl #3
	ldr	r3, [r4, #12]
	str	r5, [r3, #336]
	pop	{r4, r5, r6, pc}
.L36:
	.align	2
.L35:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	1
	.global	FlashBlockAlignInit
	.thumb
	.thumb_func
	.type	FlashBlockAlignInit, %function
FlashBlockAlignInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, #512
	ldr	r3, .L43
	bls	.L38
	mov	r2, #1024
	b	.L42
.L38:
	cmp	r0, #256
	bls	.L40
	mov	r2, #512
	b	.L42
.L40:
	cmp	r0, #128
	bhi	.L41
	str	r0, [r3, #4]
	bx	lr
.L41:
	mov	r2, #256
.L42:
	str	r2, [r3, #4]
	bx	lr
.L44:
	.align	2
.L43:
	.word	.LANCHOR0
	.fnend
	.size	FlashBlockAlignInit, .-FlashBlockAlignInit
	.align	1
	.global	FlashReadCmd
	.thumb
	.thumb_func
	.type	FlashReadCmd, %function
FlashReadCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L47
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	add	r3, r2, r0, lsl #3
	ldr	r2, [r2, #44]
	ldr	r4, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	lsls	r3, r3, #8
	cmp	r2, #1
	it	eq
	addeq	r2, r4, r3
	add	r3, r3, r4
	itt	eq
	moveq	r5, #38
	streq	r5, [r2, #2056]
	movs	r2, #0
	str	r2, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	uxtb	r2, r1
	str	r2, [r3, #2052]
	lsrs	r2, r1, #8
	str	r2, [r3, #2052]
	lsrs	r2, r1, #16
	str	r2, [r3, #2052]
	movs	r2, #48
	str	r2, [r3, #2056]
	pop	{r4, r5, lr}
	b	FlashSetRandomizer
.L48:
	.align	2
.L47:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadCmd, .-FlashReadCmd
	.align	1
	.global	FlashReadDpDataOutCmd
	.thumb
	.thumb_func
	.type	FlashReadDpDataOutCmd, %function
FlashReadDpDataOutCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	uxtb	r6, r1
	ldr	r4, .L53
	lsrs	r5, r1, #8
	add	r3, r4, r0, lsl #3
	ldrb	r4, [r4, #64]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r4, #1
	lsr	r4, r1, #16
	lsl	r3, r3, #8
	add	r3, r3, r2
	bne	.L50
	movs	r2, #6
	str	r2, [r3, #2056]
	movs	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r6, [r3, #2052]
	str	r5, [r3, #2052]
	str	r4, [r3, #2052]
	b	.L52
.L50:
	movs	r2, #0
	str	r2, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r6, [r3, #2052]
	str	r5, [r3, #2052]
	str	r4, [r3, #2052]
	movs	r4, #5
	str	r4, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
.L52:
	movs	r2, #224
	str	r2, [r3, #2056]
	pop	{r4, r5, r6, lr}
	b	FlashSetRandomizer
.L54:
	.align	2
.L53:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	1
	.global	FlashProgFirstCmd
	.thumb
	.thumb_func
	.type	FlashProgFirstCmd, %function
FlashProgFirstCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	lsrs	r2, r1, #16
	ldr	r4, .L56
	add	r4, r4, r0, lsl #3
	ldr	r3, [r4, #12]
	ldrb	r4, [r4, #16]	@ zero_extendqisi2
	add	r3, r3, r4, lsl #8
	movs	r4, #128
	str	r4, [r3, #2056]
	movs	r4, #0
	str	r4, [r3, #2052]
	str	r4, [r3, #2052]
	uxtb	r4, r1
	str	r4, [r3, #2052]
	lsrs	r4, r1, #8
	str	r4, [r3, #2052]
	str	r2, [r3, #2052]
	pop	{r4, lr}
	b	FlashSetRandomizer
.L57:
	.align	2
.L56:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgFirstCmd, .-FlashProgFirstCmd
	.align	1
	.global	FlashEraseCmd
	.thumb
	.thumb_func
	.type	FlashEraseCmd, %function
FlashEraseCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r5, .L63
	add	r0, r5, r0, lsl #3
	ldrb	r3, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	lsls	r3, r3, #8
	cbz	r2, .L59
	adds	r2, r4, r3
	movs	r0, #96
	str	r0, [r2, #2056]
	uxtb	r0, r1
	str	r0, [r2, #2052]
	lsrs	r0, r1, #8
	str	r0, [r2, #2052]
	lsrs	r0, r1, #16
	str	r0, [r2, #2052]
	ldr	r2, [r5, #4]
	add	r1, r1, r2
.L59:
	add	r3, r3, r4
	movs	r2, #96
	str	r2, [r3, #2056]
	uxtb	r2, r1
	str	r2, [r3, #2052]
	lsrs	r2, r1, #8
	lsrs	r1, r1, #16
	str	r2, [r3, #2052]
	str	r1, [r3, #2052]
	movs	r2, #208
	str	r2, [r3, #2056]
	pop	{r4, r5, pc}
.L64:
	.align	2
.L63:
	.word	.LANCHOR0
	.fnend
	.size	FlashEraseCmd, .-FlashEraseCmd
	.align	1
	.global	FlashProgDpSecondCmd
	.thumb
	.thumb_func
	.type	FlashProgDpSecondCmd, %function
FlashProgDpSecondCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	lsrs	r2, r1, #16
	ldr	r4, .L66
	add	r5, r4, r0, lsl #3
	ldrb	r4, [r4, #59]	@ zero_extendqisi2
	ldr	r3, [r5, #12]
	ldrb	r5, [r5, #16]	@ zero_extendqisi2
	add	r3, r3, r5, lsl #8
	str	r4, [r3, #2056]
	movs	r4, #0
	str	r4, [r3, #2052]
	str	r4, [r3, #2052]
	uxtb	r4, r1
	str	r4, [r3, #2052]
	lsrs	r4, r1, #8
	str	r4, [r3, #2052]
	str	r2, [r3, #2052]
	pop	{r4, r5, lr}
	b	FlashSetRandomizer
.L67:
	.align	2
.L66:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpSecondCmd, .-FlashProgDpSecondCmd
	.align	1
	.global	FlashProgSecondCmd
	.thumb
	.thumb_func
	.type	FlashProgSecondCmd, %function
FlashProgSecondCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L69
	add	r0, r3, r0, lsl #3
	ldr	r3, .L69+4
	ldr	r4, [r0, #12]
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r3, #4]
	ldr	r0, .L69+8
	blx	r3
	add	r4, r4, r5, lsl #8
	movs	r3, #16
	str	r3, [r4, #2056]
	pop	{r3, r4, r5, pc}
.L70:
	.align	2
.L69:
	.word	.LANCHOR0
	.word	arm_delay_ops
	.word	214748300
	.fnend
	.size	FlashProgSecondCmd, .-FlashProgSecondCmd
	.align	1
	.global	FlashProgDpFirstCmd
	.thumb
	.thumb_func
	.type	FlashProgDpFirstCmd, %function
FlashProgDpFirstCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L72
	add	r0, r2, r0, lsl #3
	ldrb	r2, [r2, #58]	@ zero_extendqisi2
	ldrb	r1, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r1, lsl #8
	str	r2, [r3, #2056]
	bx	lr
.L73:
	.align	2
.L72:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	1
	.global	js_hash
	.thumb
	.thumb_func
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L78
	add	r1, r1, r0
	push	{r4, lr}
	.save {r4, lr}
.L75:
	cmp	r0, r1
	beq	.L77
	lsls	r2, r3, #5
	ldrb	r4, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, r4
	eors	r3, r3, r2
	b	.L75
.L77:
	mov	r0, r3
	pop	{r4, pc}
.L79:
	.align	2
.L78:
	.word	1204201446
	.fnend
	.size	js_hash, .-js_hash
	.align	1
	.global	FlashLoadIdbInfo
	.thumb
	.thumb_func
	.type	FlashLoadIdbInfo, %function
FlashLoadIdbInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r0, #0
	bx	lr
	.fnend
	.size	FlashLoadIdbInfo, .-FlashLoadIdbInfo
	.align	1
	.global	FlashPrintInfo
	.thumb
	.thumb_func
	.type	FlashPrintInfo, %function
FlashPrintInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	FlashPrintInfo, .-FlashPrintInfo
	.align	1
	.global	ftl_flash_suspend
	.thumb
	.thumb_func
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L83
	ldr	r2, [r3, #80]
	ldr	r1, [r2]
	str	r1, [r3, #84]
	ldr	r1, [r2, #4]
	str	r1, [r3, #88]
	ldr	r1, [r2, #8]
	str	r1, [r3, #92]
	ldr	r1, [r2, #12]
	str	r1, [r3, #96]
	ldr	r1, [r2, #304]
	str	r1, [r3, #100]
	ldr	r1, [r2, #308]
	str	r1, [r3, #104]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #108]
	str	r2, [r3, #112]
	bx	lr
.L84:
	.align	2
.L83:
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.global	__aeabi_uidiv
	.align	1
	.global	LogAddr2PhyAddr
	.thumb
	.thumb_func
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r7, r0
	ldr	r0, .L90
	mov	r8, r3
	ldr	r4, [r7, #4]
	mov	r10, r1
	mov	r9, r2
	ldrh	r3, [r0, #130]
	bic	r4, r4, #-2147483648
	ldrh	r5, [r0, #128]
	mov	r6, r0
	ldrh	fp, [r0, #4]
	smulbb	r5, r5, r3
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r3, #1
	ubfx	r3, r4, #10, #16
	it	eq
	lsleq	fp, fp, #1
	str	r3, [sp, #4]
	mov	r0, r3
	uxth	r5, r5
	it	eq
	uxtheq	fp, fp
	mov	r1, r5
	bl	__aeabi_uidiv
	cmp	r10, #1
	ubfx	r1, r4, #0, #10
	uxth	r0, r0
	ldr	r3, [sp, #4]
	smulbb	r5, r0, r5
	sub	r3, r3, r5
	uxth	r3, r3
	bne	.L87
	ldrb	r2, [r6, #144]	@ zero_extendqisi2
	cbnz	r2, .L87
	ldr	r2, .L90
	add	r1, r2, r1, lsl #1
	ldrh	r1, [r1, #148]
.L87:
	add	r6, r6, r0, lsl #2
	ldr	r2, [r6, #1172]
	mla	fp, fp, r3, r2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	cmp	r3, #1
	add	r1, r1, fp
	str	r1, [r9]
	str	r0, [r8]
	bls	.L89
	ldr	r0, [r7, #4]
	ldr	r3, [r7, #40]
	add	r0, r0, #1024
	subs	r3, r0, r3
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	b	.L88
.L89:
	movs	r0, #0
.L88:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L91:
	.align	2
.L90:
	.word	.LANCHOR0
	.fnend
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	1
	.global	FlashScheduleEnSet
	.thumb
	.thumb_func
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L93
	ldr	r2, [r3, #1204]
	str	r0, [r3, #1204]
	mov	r0, r2
	bx	lr
.L94:
	.align	2
.L93:
	.word	.LANCHOR0
	.fnend
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	1
	.global	FlashGetPageSize
	.thumb
	.thumb_func
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L96
	ldr	r3, [r3, #44]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L97:
	.align	2
.L96:
	.word	.LANCHOR0
	.fnend
	.size	FlashGetPageSize, .-FlashGetPageSize
	.align	1
	.global	NandcReadDontCaseBusyEn
	.thumb
	.thumb_func
	.type	NandcReadDontCaseBusyEn, %function
NandcReadDontCaseBusyEn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	NandcReadDontCaseBusyEn, .-NandcReadDontCaseBusyEn
	.align	1
	.global	NandcGetChipIf
	.thumb
	.thumb_func
	.type	NandcGetChipIf, %function
NandcGetChipIf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L100
	add	r0, r3, r0, lsl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	adds	r2, r2, #8
	add	r0, r0, r2, lsl #8
	bx	lr
.L101:
	.align	2
.L100:
	.word	.LANCHOR0
	.fnend
	.size	NandcGetChipIf, .-NandcGetChipIf
	.align	1
	.global	NandcSetDdrPara
	.thumb
	.thumb_func
	.type	NandcSetDdrPara, %function
NandcSetDdrPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L103
	ldr	r2, [r3, #80]
	lsls	r3, r0, #8
	orr	r0, r3, r0, lsl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L104:
	.align	2
.L103:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	1
	.global	NandcSetDdrDiv
	.thumb
	.thumb_func
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L106
	orr	r0, r0, #16640
	ldr	r3, [r3, #80]
	str	r0, [r3, #344]
	bx	lr
.L107:
	.align	2
.L106:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	1
	.global	NandcSetDdrMode
	.thumb
	.thumb_func
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L111
	ldr	r2, [r3, #80]
	ldr	r3, [r2]
	cbnz	r0, .L109
	bfi	r3, r0, #13, #1
	b	.L110
.L109:
	orr	r3, r3, #253952
.L110:
	str	r3, [r2]
	bx	lr
.L112:
	.align	2
.L111:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetDdrMode, .-NandcSetDdrMode
	.align	1
	.global	NandcSetMode
	.thumb
	.thumb_func
	.type	NandcSetMode, %function
NandcSetMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L120
	ands	r1, r0, #6
	ldr	r2, [r3, #80]
	ldr	r3, [r2]
	beq	.L114
	orr	r3, r3, #24576
	lsls	r1, r0, #29
	bfc	r3, #15, #1
	movw	r1, #8322
	orr	r3, r3, #196608
	str	r1, [r2, #344]
	add	r1, r1, #1040384
	it	mi
	orrmi	r3, r3, #32768
	addw	r1, r1, #3969
	str	r1, [r2, #304]
	movs	r1, #38
	str	r1, [r2, #308]
	movs	r1, #39
	str	r1, [r2, #308]
	b	.L116
.L114:
	bfi	r3, r1, #13, #1
.L116:
	str	r3, [r2]
	movs	r0, #0
	bx	lr
.L121:
	.align	2
.L120:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetMode, .-NandcSetMode
	.align	1
	.global	NandcFlashCs
	.thumb
	.thumb_func
	.type	NandcFlashCs, %function
NandcFlashCs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L123
	add	r0, r2, r0, lsl #3
	movs	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	lsls	r2, r2, r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L124:
	.align	2
.L123:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashCs, .-NandcFlashCs
	.align	1
	.global	NandcFlashDeCs
	.thumb
	.thumb_func
	.type	NandcFlashDeCs, %function
NandcFlashDeCs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L126
	add	r0, r3, r0, lsl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L127:
	.align	2
.L126:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashDeCs, .-NandcFlashDeCs
	.align	1
	.global	NandcDelayns
	.thumb
	.thumb_func
	.type	NandcDelayns, %function
NandcDelayns:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	addw	r0, r0, #999
	ldr	r3, .L129
	umull	r0, r1, r0, r3
	ldr	r3, .L129+4
	ldr	r3, [r3, #8]
	lsrs	r0, r1, #6
	blx	r3
	movs	r0, #0
	pop	{r3, pc}
.L130:
	.align	2
.L129:
	.word	274877907
	.word	arm_delay_ops
	.fnend
	.size	NandcDelayns, .-NandcDelayns
	.align	1
	.global	FlashReadStatus
	.thumb
	.thumb_func
	.type	FlashReadStatus, %function
FlashReadStatus:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r2, #112
	ldr	r3, .L132
	add	r0, r3, r0, lsl #3
	ldrb	r4, [r0, #16]	@ zero_extendqisi2
	ldr	r5, [r0, #12]
	movs	r0, #80
	add	r3, r5, r4, lsl #8
	adds	r4, r4, #8
	str	r2, [r3, #2056]
	lsls	r4, r4, #8
	bl	NandcDelayns
	ldr	r0, [r5, r4]
	pop	{r3, r4, r5, pc}
.L133:
	.align	2
.L132:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatus, .-FlashReadStatus
	.align	1
	.global	ToshibaSetRRPara
	.thumb
	.thumb_func
	.type	ToshibaSetRRPara, %function
ToshibaSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r8, r1, r1, lsl #2
	ldr	r9, .L143+8
	movs	r4, #0
	ldr	r7, .L143
	mov	r5, r0
	add	r10, r9, #256
	mov	r6, r1
.L135:
	ldrb	r3, [r7, #1209]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L142
	movs	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	movs	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldrb	r3, [r7, #1208]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L136
	add	r3, r4, r8
	add	r3, r3, r10
	b	.L141
.L136:
	cmp	r3, #35
	bne	.L138
	ldr	r2, .L143+4
	add	r3, r4, r8
	add	r3, r3, r2
.L141:
	ldrsb	r3, [r3, #5]
	b	.L140
.L138:
	add	r3, r9, r6
	ldrsb	r3, [r3, #400]
.L140:
	str	r3, [r5]
	adds	r4, r4, #1
	b	.L135
.L142:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L144:
	.align	2
.L143:
	.word	.LANCHOR0
	.word	.LANCHOR1+304
	.word	.LANCHOR1
	.fnend
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	1
	.global	SamsungSetRRPara
	.thumb
	.thumb_func
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L149
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, lsl #2
	movs	r4, #0
	ldr	r8, .L149+4
	adds	r5, r1, #3
	mov	r9, #161
	mov	r6, r0
	mov	r7, r3
	mov	r10, r4
.L146:
	ldrb	r3, [r8, #1209]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L148
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	adds	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L146
.L148:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L150:
	.align	2
.L149:
	.word	.LANCHOR1+408
	.word	.LANCHOR0
	.fnend
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	1
	.global	HynixSetRRPara
	.thumb
	.thumb_func
	.type	HynixSetRRPara, %function
HynixSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r7, r3
	ldr	r5, .L159
	mov	r6, r0
	mov	r9, r1
	mov	fp, r2
	ldr	r3, [r5, #44]
	ldrb	r4, [r3, #19]	@ zero_extendqisi2
	lsls	r3, r0, #3
	cmp	r4, #6
	bne	.L152
	add	r4, r5, r0, lsl #6
	addw	r4, r4, #1230
	add	r4, r4, r7, lsl #2
	b	.L153
.L152:
	cmp	r4, #7
	bne	.L154
	mov	r10, #160
	add	r4, r7, r7, lsl #2
	mla	r10, r10, r0, r5
	addw	r10, r10, #1238
	add	r4, r10, r4, lsl #1
	b	.L153
.L154:
	cmp	r4, #8
	iteet	ne
	addne	r4, r7, r3
	addweq	r4, r5, #1238
	addeq	r2, r7, r7, lsl #2
	addne	r4, r5, r4, lsl #3
	ite	eq
	addeq	r4, r4, r2
	addwne	r4, r4, #1230
.L153:
	add	r3, r3, r5
	mov	r0, r6
	add	r9, r9, #-1
	subs	r4, r4, #1
	ldrb	r8, [r3, #16]	@ zero_extendqisi2
	add	r9, r9, fp
	ldr	r10, [r3, #12]
	bl	NandcFlashCs
	add	r1, fp, #-1
	movs	r2, #54
	lsl	r8, r8, #8
	add	r3, r10, r8
	mov	fp, r3
	str	r2, [r3, #2056]
.L156:
	cmp	r1, r9
	beq	.L158
	ldrb	r3, [r1, #1]!	@ zero_extendqisi2
	movs	r0, #200
	str	r3, [fp, #2052]
	str	r1, [sp, #4]
	bl	NandcDelayns
	ldrsb	r3, [r4, #1]!
	str	r3, [fp, #2048]
	ldr	r1, [sp, #4]
	b	.L156
.L158:
	add	r8, r8, r10
	movs	r3, #22
	mov	r0, r6
	add	r5, r5, r6
	str	r3, [r8, #2056]
	bl	NandcFlashDeCs
	strb	r7, [r5, #2064]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L160:
	.align	2
.L159:
	.word	.LANCHOR0
	.fnend
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	1
	.global	FlashSetReadRetryDefault
	.thumb
	.thumb_func
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L167
	ldr	r2, [r3, #44]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	subs	r2, r2, #1
	cmp	r2, #7
	bhi	.L161
	movs	r4, #0
	mov	r5, r3
.L162:
	ldr	r2, .L167+4
	uxtb	r0, r4
	ldrb	r3, [r2, r4, lsl #3]	@ zero_extendqisi2
	cmp	r3, #173
	bne	.L163
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	subw	r2, r2, #854
	movs	r3, #0
	bl	HynixSetRRPara
.L163:
	adds	r4, r4, #1
	cmp	r4, #4
	bne	.L162
.L161:
	pop	{r3, r4, r5, pc}
.L168:
	.align	2
.L167:
	.word	.LANCHOR0
	.word	.LANCHOR0+2068
	.fnend
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	1
	.global	FlashReadStatusEN
	.thumb
	.thumb_func
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, .L179
	add	r0, r5, r0, lsl #3
	ldr	r3, [r5, #44]
	ldrb	r4, [r0, #16]	@ zero_extendqisi2
	ldr	r6, [r0, #12]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	lsl	r3, r4, #8
	add	r4, r4, #8
	bne	.L170
	cbnz	r2, .L171
	ldrb	r2, [r5, #61]	@ zero_extendqisi2
	b	.L178
.L171:
	ldrb	r2, [r5, #62]	@ zero_extendqisi2
.L178:
	add	r3, r3, r6
	str	r2, [r3, #2056]
	ldrb	r0, [r5, #63]	@ zero_extendqisi2
	cbz	r0, .L174
	movs	r3, #0
	add	r5, r6, r4, lsl #8
.L173:
	cmp	r3, r0
	bcs	.L174
	lsls	r2, r3, #3
	adds	r3, r3, #1
	lsr	r2, r1, r2
	uxtb	r2, r2
	str	r2, [r5, #4]
	b	.L173
.L170:
	add	r3, r3, r6
	movs	r2, #112
	str	r2, [r3, #2056]
.L174:
	movs	r0, #80
	lsls	r4, r4, #8
	bl	NandcDelayns
	ldr	r0, [r6, r4]
	uxtb	r0, r0
	pop	{r4, r5, r6, pc}
.L180:
	.align	2
.L179:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatusEN, .-FlashReadStatusEN
	.align	1
	.global	FlashWaitReadyEN
	.thumb
	.thumb_func
	.type	FlashWaitReadyEN, %function
FlashWaitReadyEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
.L182:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	mov	r3, r0
	beq	.L182
	lsls	r3, r3, #25
	bmi	.L183
	movs	r0, #1
	movs	r1, #3
	bl	usleep_range
	b	.L182
.L183:
	pop	{r4, r5, r6, pc}
	.fnend
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
	.align	1
	.global	FlashWaitCmdDone
	.thumb
	.thumb_func
	.type	FlashWaitCmdDone, %function
FlashWaitCmdDone:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r8, r0
	ldr	r5, .L194
	add	r4, r5, r0, lsl #4
	ldr	r3, [r4, #2108]
	ldrb	r6, [r4, #2100]	@ zero_extendqisi2
	cbz	r3, .L189
	add	r5, r5, r8, lsl #2
	mov	r0, r6
	bl	NandcFlashCs
	ldr	r1, [r4, #2104]
	ldr	r2, [r5, #1172]
	mov	r0, r6
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r5, r0
	mov	r0, r6
	bl	NandcFlashDeCs
	ldr	r2, [r4, #2108]
	sbfx	r3, r5, #0, #1
	str	r3, [r2]
	movs	r2, #0
	ldr	r1, [r4, #2112]
	str	r2, [r4, #2108]
	cbz	r1, .L189
	str	r3, [r1]
	str	r2, [r4, #2112]
.L189:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L195:
	.align	2
.L194:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	1
	.thumb
	.thumb_func
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L197
	push	{r4, lr}
	.save {r4, lr}
	add	r0, r2, r0, lsl #3
	ldrb	r4, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	movs	r0, #80
	add	r4, r3, r4, lsl #8
	movs	r3, #122
	str	r3, [r4, #2056]
	bl	NandcDelayns
	ldr	r3, [r4, #2048]
	ldr	r0, [r4, #2048]
	and	r3, r3, #15
	and	r0, r0, #15
	cmp	r0, r3
	it	cc
	movcc	r0, r3
	ldr	r3, [r4, #2048]
	and	r3, r3, #15
	cmp	r0, r3
	it	cc
	movcc	r0, r3
	ldr	r3, [r4, #2048]
	and	r3, r3, #15
	cmp	r0, r3
	it	cc
	movcc	r0, r3
	pop	{r4, pc}
.L198:
	.align	2
.L197:
	.word	.LANCHOR0
	.fnend
	.size	flash_read_ecc, .-flash_read_ecc
	.align	1
	.global	NandcWaitFlashReadyNoDelay
	.thumb
	.thumb_func
	.type	NandcWaitFlashReadyNoDelay, %function
NandcWaitFlashReadyNoDelay:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L204
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, lsl #3
	ldr	r4, .L204+4
	ldr	r5, [r0, #12]
.L201:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #22
	bmi	.L202
	movs	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L201
	mov	r0, #-1
	b	.L200
.L202:
	movs	r0, #0
.L200:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L205:
	.align	2
.L204:
	.word	.LANCHOR0
	.word	100000
	.fnend
	.size	NandcWaitFlashReadyNoDelay, .-NandcWaitFlashReadyNoDelay
	.align	1
	.global	NandcWaitFlashReady
	.thumb
	.thumb_func
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L211
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, lsl #3
	ldr	r4, .L211+4
	ldr	r5, [r0, #12]
	movs	r0, #130
	bl	NandcDelayns
.L208:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #22
	bmi	.L209
	movs	r0, #1
	movs	r1, #2
	bl	usleep_range
	subs	r4, r4, #1
	bne	.L208
	mov	r0, #-1
	b	.L207
.L209:
	movs	r0, #0
.L207:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L212:
	.align	2
.L211:
	.word	.LANCHOR0
	.word	100000
	.fnend
	.size	NandcWaitFlashReady, .-NandcWaitFlashReady
	.align	1
	.global	FlashReset
	.thumb
	.thumb_func
	.type	FlashReset, %function
FlashReset:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L214
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r3, r3, r0, lsl #3
	mov	r4, r0
	ldrb	r6, [r3, #16]	@ zero_extendqisi2
	ldr	r5, [r3, #12]
	bl	NandcFlashCs
	movs	r3, #255
	mov	r0, r4
	add	r5, r5, r6, lsl #8
	str	r3, [r5, #2056]
	bl	NandcWaitFlashReady
	mov	r0, r4
	pop	{r4, r5, r6, lr}
	b	NandcFlashDeCs
.L215:
	.align	2
.L214:
	.word	.LANCHOR0
	.fnend
	.size	FlashReset, .-FlashReset
	.align	1
	.global	flash_enter_slc_mode
	.thumb
	.thumb_func
	.type	flash_enter_slc_mode, %function
flash_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r0
	ldr	r6, .L222
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cbz	r3, .L216
	add	r4, r6, r7, lsl #3
	bl	NandcFlashCs
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	ldrb	r5, [r4, #16]	@ zero_extendqisi2
	cmp	r3, #44
	ldr	r8, [r4, #12]
	lsl	r5, r5, #8
	bne	.L218
	add	r4, r8, r5
	movs	r3, #239
	movs	r0, #50
	str	r3, [r4, #2056]
	movs	r3, #145
	str	r3, [r4, #2052]
	bl	NandcDelayns
	movs	r3, #0
	movs	r2, #1
	str	r3, [r4, #2048]
	movs	r0, #100
	str	r2, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	NandcDelayns
.L218:
	add	r5, r5, r8
	mov	r0, r7
	bl	NandcWaitFlashReadyNoDelay
	movs	r3, #218
	mov	r0, r7
	str	r3, [r5, #2056]
	bl	NandcWaitFlashReady
	movs	r3, #2
	strb	r3, [r6, #2228]
.L216:
	pop	{r4, r5, r6, r7, r8, pc}
.L223:
	.align	2
.L222:
	.word	.LANCHOR0
	.fnend
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	1
	.global	flash_exit_slc_mode
	.thumb
	.thumb_func
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r0
	ldr	r6, .L230
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cbz	r3, .L224
	add	r4, r6, r7, lsl #3
	bl	NandcFlashCs
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	ldrb	r5, [r4, #16]	@ zero_extendqisi2
	cmp	r3, #44
	ldr	r8, [r4, #12]
	lsl	r5, r5, #8
	bne	.L226
	add	r4, r8, r5
	movs	r3, #239
	movs	r0, #50
	str	r3, [r4, #2056]
	movs	r3, #145
	str	r3, [r4, #2052]
	bl	NandcDelayns
	movs	r3, #2
	movs	r0, #100
	str	r3, [r4, #2048]
	movs	r3, #1
	str	r3, [r4, #2048]
	movs	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	NandcDelayns
.L226:
	add	r5, r5, r8
	mov	r0, r7
	bl	NandcWaitFlashReadyNoDelay
	movs	r3, #223
	mov	r0, r7
	str	r3, [r5, #2056]
	bl	NandcWaitFlashReady
	movs	r3, #0
	strb	r3, [r6, #2228]
.L224:
	pop	{r4, r5, r6, r7, r8, pc}
.L231:
	.align	2
.L230:
	.word	.LANCHOR0
	.fnend
	.size	flash_exit_slc_mode, .-flash_exit_slc_mode
	.align	1
	.global	FlashEraseBlock
	.thumb
	.thumb_func
	.type	FlashEraseBlock, %function
FlashEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	bl	FlashEraseCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r1, r5
	mov	r0, r4
	bl	FlashReadStatus
	mov	r5, r0
	mov	r0, r4
	bl	NandcFlashDeCs
	and	r0, r5, #1
	pop	{r4, r5, r6, pc}
	.fnend
	.size	FlashEraseBlock, .-FlashEraseBlock
	.align	1
	.global	FlashSetInterfaceMode
	.thumb
	.thumb_func
	.type	FlashSetInterfaceMode, %function
FlashSetInterfaceMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r5, .L261
	movs	r4, #0
	movs	r7, #239
	mov	lr, #128
	mov	ip, #1
	mov	r8, #35
	ldrb	r3, [r5, #2229]	@ zero_extendqisi2
	mov	r9, #32
	mov	r10, #5
	and	r2, r3, #1
	str	r2, [sp, #12]
	and	r2, r3, #4
	uxtb	r3, r2
	mov	r2, r4
	str	r3, [sp, #8]
	add	r3, r5, #12
	str	r3, [sp, #4]
.L243:
	ldr	r1, .L261+4
	ldr	r3, [sp, #4]
	ldrb	r1, [r4, r1]	@ zero_extendqisi2
	ldr	r6, [r4, r3]
	add	r3, r3, r4
	cmp	r1, #152
	ldr	r3, [r3, #4]
	beq	.L234
	cmp	r1, #69
	beq	.L234
	cmp	r1, #173
	beq	.L234
	cmp	r1, #44
	bne	.L235
.L234:
	cmp	r0, #1
	uxtb	r3, r3
	bne	.L236
	ldr	r5, [sp, #12]
	cbz	r5, .L235
	lsls	r3, r3, #8
	cmp	r1, #173
	add	fp, r6, r3
	str	r7, [fp, #2056]
	bne	.L237
	str	r0, [fp, #2052]
	b	.L260
.L237:
	cmp	r1, #44
	itete	eq
	streq	r0, [fp, #2052]
	strne	lr, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L241
.L236:
	ldr	r5, [sp, #8]
	cbz	r5, .L235
	lsls	r3, r3, #8
	cmp	r1, #173
	add	fp, r6, r3
	str	r7, [fp, #2056]
	bne	.L240
	str	ip, [fp, #2052]
	str	r9, [fp, #2048]
	b	.L241
.L240:
	cmp	r1, #44
	bne	.L242
	str	ip, [fp, #2052]
	str	r8, [fp, #2048]
	b	.L241
.L242:
	str	lr, [fp, #2052]
.L260:
	str	r2, [fp, #2048]
.L241:
	add	r3, r3, r6
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L235:
	adds	r4, r4, #8
	cmp	r4, #32
	bne	.L243
	movs	r0, #0
	bl	NandcWaitFlashReady
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L262:
	.align	2
.L261:
	.word	.LANCHOR0
	.word	.LANCHOR0+2068
	.fnend
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	1
	.global	FlashReadSpare
	.thumb
	.thumb_func
	.type	FlashReadSpare, %function
FlashReadSpare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r2
	ldr	r5, .L264
	ldr	r3, .L264+4
	add	r5, r5, r0, lsl #3
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	ldrb	r2, [r5, #16]	@ zero_extendqisi2
	ldr	r4, [r5, #12]
	lsls	r3, r3, #9
	add	r4, r4, r2, lsl #8
	movs	r2, #0
	str	r2, [r4, #2056]
	str	r3, [r4, #2052]
	lsrs	r3, r3, #8
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	lsrs	r3, r1, #8
	lsrs	r1, r1, #16
	str	r3, [r4, #2052]
	movs	r3, #48
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #2048]
	strb	r3, [r6]
	pop	{r4, r5, r6, pc}
.L265:
	.align	2
.L264:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.fnend
	.size	FlashReadSpare, .-FlashReadSpare
	.align	1
	.global	SandiskProgTestBadBlock
	.thumb
	.thumb_func
	.type	SandiskProgTestBadBlock, %function
SandiskProgTestBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L267
	push	{r4, lr}
	.save {r4, lr}
	add	r2, r2, r0, lsl #3
	ldrb	r4, [r2, #16]	@ zero_extendqisi2
	ldr	r3, [r2, #12]
	add	r4, r3, r4, lsl #8
	movs	r3, #162
	str	r3, [r4, #2056]
	movs	r3, #128
	str	r3, [r4, #2056]
	movs	r3, #0
	str	r3, [r4, #2052]
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	lsrs	r3, r1, #8
	lsrs	r1, r1, #16
	str	r3, [r4, #2052]
	str	r1, [r4, #2052]
	movs	r3, #16
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	movs	r3, #112
	movs	r0, #80
	str	r3, [r4, #2056]
	bl	NandcDelayns
	ldr	r0, [r4, #2048]
	and	r0, r0, #1
	pop	{r4, pc}
.L268:
	.align	2
.L267:
	.word	.LANCHOR0
	.fnend
	.size	SandiskProgTestBadBlock, .-SandiskProgTestBadBlock
	.align	1
	.global	SandiskSetRRPara
	.thumb
	.thumb_func
	.type	SandiskSetRRPara, %function
SandiskSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r3, #239
	mov	r5, r0
	str	r3, [r0, #8]
	movs	r3, #17
	str	r3, [r0, #4]
	movs	r0, #200
	mov	r4, r1
	bl	NandcDelayns
	ldr	r0, .L276
	ldr	r1, .L276+4
	add	r4, r4, r4, lsl #2
	movs	r2, #0
	sub	r6, r0, #48
.L270:
	ldrb	r3, [r1, #1209]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L275
	ldrb	r3, [r1, #1208]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	ite	eq
	addeq	r3, r3, r6
	addne	r3, r3, r0
	adds	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L270
.L275:
	movs	r0, #0
	pop	{r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L277:
	.align	2
.L276:
	.word	.LANCHOR1+304
	.word	.LANCHOR0
	.fnend
	.size	SandiskSetRRPara, .-SandiskSetRRPara
	.align	1
	.global	micron_auto_read_calibration_config
	.thumb
	.thumb_func
	.type	micron_auto_read_calibration_config, %function
micron_auto_read_calibration_config:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	mov	r5, r1
	bl	NandcWaitFlashReady
	ldr	r3, .L279
	movs	r0, #200
	add	r2, r3, r4, lsl #3
	ldrb	r4, [r2, #16]	@ zero_extendqisi2
	ldr	r3, [r2, #12]
	add	r4, r3, r4, lsl #8
	movs	r3, #239
	str	r3, [r4, #2056]
	movs	r3, #150
	str	r3, [r4, #2052]
	bl	NandcDelayns
	str	r5, [r4, #2048]
	movs	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	pop	{r3, r4, r5, pc}
.L280:
	.align	2
.L279:
	.word	.LANCHOR0
	.fnend
	.size	micron_auto_read_calibration_config, .-micron_auto_read_calibration_config
	.align	1
	.global	FlashEraseSLc2KBlocks
	.thumb
	.thumb_func
	.type	FlashEraseSLc2KBlocks, %function
FlashEraseSLc2KBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r5, #0
	ldr	r8, .L292+4
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
.L282:
	cmp	r7, r9
	beq	.L291
	rsb	r3, r7, r9
	add	r2, sp, #8
	adds	r0, r6, r5
	movs	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2230]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r3, r2
	bcc	.L283
	mov	r3, #-1
	str	r3, [r6, r5]
	b	.L284
.L283:
	add	r2, r8, r3
	add	r3, r8, r3, lsl #4
	ldrb	r4, [r2, #2232]	@ zero_extendqisi2
	strb	r4, [r3, #2100]
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	movs	r2, #0
	mov	r0, r4
	ldr	r1, [sp, #8]
	bl	FlashEraseCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldr	r1, [sp, #8]
	bl	FlashReadStatus
	movs	r2, #0
	ldr	r3, [sp, #8]
	sbfx	r0, r0, #0, #1
	str	r0, [r6, r5]
	mov	r0, r4
	ldr	r1, [r8, #4]
	add	r1, r1, r3
	bl	FlashEraseCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldr	r1, [sp, #8]
	bl	FlashReadStatus
	lsls	r3, r0, #31
	itt	mi
	movmi	r3, #-1
	strmi	r3, [r6, r5]
	ldr	r3, [r6, r5]
	adds	r3, r3, #1
	bne	.L286
	ldr	r0, .L292
	ldr	r1, [sp, #8]
	bl	printk
.L286:
	mov	r0, r4
	bl	NandcFlashDeCs
.L284:
	adds	r7, r7, #1
	adds	r5, r5, #36
	b	.L282
.L291:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L293:
	.align	2
.L292:
	.word	.LC1
	.word	.LANCHOR0
	.fnend
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	1
	.global	FlashEraseBlocks
	.thumb
	.thumb_func
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	ldr	r4, .L328
	.pad #28
	sub	sp, sp, #28
	mov	r10, r1
	mov	r8, r2
	ldrb	r5, [r4]	@ zero_extendqisi2
	cbz	r5, .L312
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L296
.L312:
	mov	fp, r4
.L295:
	cmp	r5, r8
	bcs	.L326
	movs	r3, #36
	movs	r1, #0
	add	r2, sp, #16
	mul	r6, r3, r5
	add	r3, r9, r6
	str	r3, [sp, #12]
	rsb	r3, r5, r8
	ldr	r0, [sp, #12]
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #20
	bl	LogAddr2PhyAddr
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #20]
	cmp	r0, r3
	bcc	.L297
	mov	r3, #-1
	str	r3, [r9, r6]
	b	.L298
.L297:
	ldrb	r3, [fp, #2240]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, lsl #4
	it	eq
	moveq	r7, #0
	ldr	r3, [r3, #2108]
	cbz	r3, .L300
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L300:
	ldr	r1, [sp, #20]
	ldr	r3, [sp, #12]
	lsls	r2, r1, #4
	add	lr, r4, r2
	str	r3, [lr, #2108]
	movs	r3, #0
	str	r3, [lr, #2112]
	ldr	r3, [sp, #16]
	str	r3, [lr, #2104]
	cbz	r7, .L301
	add	r3, r6, #36
	add	r3, r3, r9
	str	r3, [lr, #2112]
.L301:
	add	r1, r1, r4
	add	r2, r2, r4
	ldrb	r6, [r1, #2232]	@ zero_extendqisi2
	mov	r0, r6
	strb	r6, [r2, #2100]
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L302
	ldrb	r3, [fp, #144]	@ zero_extendqisi2
	cbz	r3, .L302
	bl	flash_enter_slc_mode
	b	.L303
.L302:
	bl	flash_exit_slc_mode
.L303:
	ldr	r3, [sp, #20]
	mov	r0, r6
	ldr	r1, [sp, #16]
	add	r5, r5, r7
	add	r3, r4, r3, lsl #2
	ldr	r2, [r3, #1172]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	mov	r2, r7
	ldr	r1, [sp, #16]
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
.L298:
	adds	r5, r5, #1
	b	.L295
.L326:
	ldr	r6, .L328
	movs	r5, #0
	addw	r7, r6, #2100
.L305:
	ldrb	r2, [r4, #2230]	@ zero_extendqisi2
	ldr	r3, .L328
	cmp	r5, r2
	bcs	.L327
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L306
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cbz	r3, .L306
	lsls	r3, r5, #4
	ldrb	r0, [r7, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L306:
	adds	r5, r5, #1
	b	.L305
.L327:
	ldr	r2, [r3, #2244]
	cbnz	r2, .L308
.L310:
	movs	r0, #0
	b	.L296
.L308:
	ldrb	r3, [r3, #2068]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L310
	movs	r3, #0
	movs	r2, #36
	mov	r1, r3
.L309:
	cmp	r3, r8
	beq	.L310
	mul	r0, r2, r3
	adds	r3, r3, #1
	str	r1, [r9, r0]
	b	.L309
.L296:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L329:
	.align	2
.L328:
	.word	.LANCHOR0
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	1
	.global	FlashReadDpCmd
	.thumb
	.thumb_func
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r7, r0
	ldr	r0, .L335
	mov	r6, r1
	uxtb	r9, r2
	lsr	r8, r2, #8
	add	r3, r0, r7, lsl #3
	lsrs	r5, r2, #16
	ldrb	r1, [r0, #64]	@ zero_extendqisi2
	uxtb	ip, r6
	ldr	r4, [r3, #12]
	lsr	lr, r6, #8
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r1, #1
	ldr	r2, [r0, #44]
	lsr	r1, r6, #16
	lsl	r3, r3, #8
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	bne	.L331
	cmp	r2, #1
	itt	eq
	addeq	r2, r4, r3
	moveq	r10, #38
	add	r4, r4, r3
	it	eq
	streq	r10, [r2, #2056]
	mov	r10, #0
	ldrb	r3, [r0, #57]	@ zero_extendqisi2
	ldrb	r2, [r0, #56]	@ zero_extendqisi2
	mov	r0, r7
	str	r2, [r4, #2056]
	str	r10, [r4, #2052]
	str	r10, [r4, #2052]
	str	ip, [r4, #2052]
	str	lr, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	str	r10, [r4, #2056]
	movs	r3, #48
	str	r10, [r4, #2052]
	str	r10, [r4, #2052]
	str	r9, [r4, #2052]
	str	r8, [r4, #2052]
	str	r5, [r4, #2052]
	str	r3, [r4, #2056]
	b	.L333
.L331:
	cmp	r2, #1
	itt	eq
	addeq	r2, r4, r3
	moveq	r10, #38
	add	r3, r3, r4
	it	eq
	streq	r10, [r2, #2056]
	ldrb	r2, [r0, #56]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r2, [r0, #57]	@ zero_extendqisi2
	str	ip, [r3, #2052]
	str	lr, [r3, #2052]
	str	r1, [r3, #2052]
	str	r2, [r3, #2056]
	movs	r2, #48
	str	r9, [r3, #2052]
	str	r8, [r3, #2052]
	str	r5, [r3, #2052]
	str	r2, [r3, #2056]
.L333:
	mov	r0, r7
	mov	r1, r6
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L336:
	.align	2
.L335:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	1
	.global	ftl_flash_de_init
	.thumb
	.thumb_func
	.type	ftl_flash_de_init, %function
ftl_flash_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r0, #0
	ldr	r4, .L347
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r0, [r4, #2248]
	cbz	r0, .L338
	movs	r0, #0
	bl	flash_enter_slc_mode
	b	.L339
.L338:
	bl	flash_exit_slc_mode
.L339:
	ldrb	r3, [r4, #2252]	@ zero_extendqisi2
	ldr	r5, .L347
	cbz	r3, .L340
	ldrb	r3, [r5, #2229]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L340
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	movs	r3, #0
	strb	r3, [r5, #2252]
.L340:
	ldr	r3, [r4, #12]
	movs	r0, #0
	str	r0, [r3, #336]
	pop	{r3, r4, r5, pc}
.L348:
	.align	2
.L347:
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_de_init, .-ftl_flash_de_init
	.align	1
	.global	NandcRandmzSel
	.thumb
	.thumb_func
	.type	NandcRandmzSel, %function
NandcRandmzSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L350
	add	r0, r3, r0, lsl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L351:
	.align	2
.L350:
	.word	.LANCHOR0
	.fnend
	.size	NandcRandmzSel, .-NandcRandmzSel
	.global	__aeabi_idiv
	.align	1
	.global	NandcTimeCfg
	.thumb
	.thumb_func
	.type	NandcTimeCfg, %function
NandcTimeCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	movs	r0, #0
	bl	rknand_get_clk_rate
	ldr	r1, .L362
	bl	__aeabi_idiv
	ldr	r3, .L362+4
	ldr	r3, [r3, #80]
	cmp	r0, #250
	ble	.L353
	movw	r2, #8354
	b	.L360
.L353:
	cmp	r0, #220
	bgt	.L361
	cmp	r0, #185
	ble	.L356
	movw	r2, #4226
	b	.L360
.L356:
	cmp	r0, #160
	ble	.L357
	movw	r2, #4194
	b	.L360
.L357:
	cmp	r4, #35
	bhi	.L358
	movw	r2, #4193
	b	.L360
.L358:
	cmp	r4, #99
	bls	.L359
.L361:
	movw	r2, #8322
	b	.L360
.L359:
	movw	r2, #4225
.L360:
	str	r2, [r3, #4]
	pop	{r4, pc}
.L363:
	.align	2
.L362:
	.word	1000000
	.word	.LANCHOR0
	.fnend
	.size	NandcTimeCfg, .-NandcTimeCfg
	.align	1
	.global	FlashTimingCfg
	.thumb
	.thumb_func
	.type	FlashTimingCfg, %function
FlashTimingCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	sub	r3, r0, #4192
	subs	r3, r3, #1
	cmp	r3, #1
	bls	.L365
	sub	r3, r0, #4224
	subs	r3, r3, #1
	cmp	r3, #1
	bls	.L365
	movw	r3, #8322
	cmp	r0, r3
	bne	.L366
.L365:
	ldr	r3, .L367
	ldr	r3, [r3, #80]
	str	r0, [r3, #4]
.L366:
	ldr	r3, .L367+4
	ldrb	r0, [r3, #493]	@ zero_extendqisi2
	b	NandcTimeCfg
.L368:
	.align	2
.L367:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.fnend
	.size	FlashTimingCfg, .-FlashTimingCfg
	.align	1
	.global	NandcInit
	.thumb
	.thumb_func
	.type	NandcInit, %function
NandcInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r1, #0
	ldr	r3, .L371
	movs	r2, #1
	movs	r5, #0
	str	r1, [r3, #16]
	str	r0, [r3, #12]
	str	r2, [r3, #24]
	movs	r2, #2
	str	r0, [r3, #20]
	str	r2, [r3, #32]
	movs	r2, #3
	str	r0, [r3, #28]
	str	r0, [r3, #36]
	str	r0, [r3, #80]
	str	r2, [r3, #40]
	ldr	r2, [r0]
	and	r2, r2, #253952
	ubfx	r4, r2, #13, #1
	bfi	r2, r1, #13, #1
	ldr	r1, [r0, #352]
	orr	r2, r2, #256
	str	r4, [r3, #2256]
	movw	r4, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r3, #2260]
	ldr	r1, [r0, #352]
	cmp	r1, r4
	mov	r4, r3
	str	r1, [r3, #2264]
	itt	eq
	moveq	r3, #8
	streq	r3, [r4, #2260]
	str	r2, [r0]
	movs	r0, #40
	ldr	r3, [r4, #80]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #80]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L371+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #2296]
	str	r5, [r4, #2304]
	str	r0, [r4, #2268]
	str	r0, [r4, #2272]
	add	r0, r0, #32768
	str	r0, [r4, #2276]
	pop	{r3, r4, r5, pc}
.L372:
	.align	2
.L371:
	.word	.LANCHOR0
	.word	1579009
	.fnend
	.size	NandcInit, .-NandcInit
	.align	1
	.global	NandcGetTimeCfg
	.thumb
	.thumb_func
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r4, .L374
	ldr	r5, [r4, #80]
	ldr	r5, [r5, #4]
	str	r5, [r0]
	ldr	r0, [r4, #80]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [r4, #80]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [r4, #80]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, lsl #16
	str	r2, [r3]
	pop	{r4, r5, pc}
.L375:
	.align	2
.L374:
	.word	.LANCHOR0
	.fnend
	.size	NandcGetTimeCfg, .-NandcGetTimeCfg
	.align	1
	.global	NandcBchSel
	.thumb
	.thumb_func
	.type	NandcBchSel, %function
NandcBchSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L384
	movs	r1, #1
	ldr	r2, [r3, #80]
	str	r0, [r3, #2308]
	movs	r3, #0
	str	r1, [r2, #8]
	movs	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L377
.L380:
	bfc	r3, #4, #1
	b	.L378
.L377:
	cmp	r0, #24
	bne	.L379
	orr	r3, r3, #16
	b	.L378
.L379:
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L380
.L378:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L385:
	.align	2
.L384:
	.word	.LANCHOR0
	.fnend
	.size	NandcBchSel, .-NandcBchSel
	.align	1
	.global	FlashBchSel
	.thumb
	.thumb_func
	.type	FlashBchSel, %function
FlashBchSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L387
	strb	r0, [r3, #2312]
	b	NandcBchSel
.L388:
	.align	2
.L387:
	.word	.LANCHOR0
	.fnend
	.size	FlashBchSel, .-FlashBchSel
	.align	1
	.global	ftl_flash_resume
	.thumb
	.thumb_func
	.type	ftl_flash_resume, %function
ftl_flash_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L397
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	ldr	r2, [r3, #80]
	addw	r6, r3, #2068
	ldr	r1, [r3, #84]
	mov	r4, r3
	str	r1, [r2]
	ldr	r1, [r3, #88]
	ldr	r2, [r3, #80]
	str	r1, [r2, #4]
	ldr	r1, [r3, #92]
	str	r1, [r2, #8]
	ldr	r1, [r3, #96]
	str	r1, [r2, #12]
	ldr	r1, [r3, #100]
	str	r1, [r2, #304]
	ldr	r1, [r3, #104]
	str	r1, [r2, #308]
	ldr	r1, [r3, #108]
	str	r1, [r2, #336]
	ldr	r1, [r3, #112]
	str	r1, [r2, #344]
.L391:
	ldrb	r3, [r6, r5, lsl #3]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L390
	uxtb	r0, r5
	bl	FlashReset
.L390:
	adds	r5, r5, #1
	cmp	r5, #4
	bne	.L391
	ldrb	r3, [r4, #2252]	@ zero_extendqisi2
	ldr	r5, .L397
	cbz	r3, .L392
	movs	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2229]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #101]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L392:
	ldr	r3, [r4, #44]
	pop	{r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L398:
	.align	2
.L397:
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_resume, .-ftl_flash_resume
	.align	1
	.global	ftl_nandc_get_irq_status
	.thumb
	.thumb_func
	.type	ftl_nandc_get_irq_status, %function
ftl_nandc_get_irq_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, [r0, #372]
	bx	lr
	.fnend
	.size	ftl_nandc_get_irq_status, .-ftl_nandc_get_irq_status
	.align	1
	.global	rk_nandc_flash_ready
	.thumb
	.thumb_func
	.type	rk_nandc_flash_ready, %function
rk_nandc_flash_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, [r0, #368]
	orr	r3, r3, #2
	str	r3, [r0, #368]
	ldr	r3, [r0, #364]
	bic	r3, r3, #2
	str	r3, [r0, #364]
	bx	lr
	.fnend
	.size	rk_nandc_flash_ready, .-rk_nandc_flash_ready
	.align	1
	.global	NandcIqrWaitFlashReady
	.thumb
	.thumb_func
	.type	NandcIqrWaitFlashReady, %function
NandcIqrWaitFlashReady:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	NandcIqrWaitFlashReady, .-NandcIqrWaitFlashReady
	.align	1
	.global	rk_nandc_flash_xfer_completed
	.thumb
	.thumb_func
	.type	rk_nandc_flash_xfer_completed, %function
rk_nandc_flash_xfer_completed:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, [r0, #368]
	orr	r3, r3, #1
	str	r3, [r0, #368]
	ldr	r3, [r0, #364]
	bic	r3, r3, #1
	str	r3, [r0, #364]
	bx	lr
	.fnend
	.size	rk_nandc_flash_xfer_completed, .-rk_nandc_flash_xfer_completed
	.align	1
	.global	NandcSendDumpDataStart
	.thumb
	.thumb_func
	.type	NandcSendDumpDataStart, %function
NandcSendDumpDataStart:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, [r0, #16]
	.pad #8
	sub	sp, sp, #8
	ldr	r3, .L404
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	bic	r2, r2, #4
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	str	r2, [r0, #16]
	str	r3, [r0, #8]
	orr	r3, r3, #4
	str	r3, [r0, #8]
	add	sp, sp, #8
	@ sp needed
	bx	lr
.L405:
	.align	2
.L404:
	.word	538969130
	.fnend
	.size	NandcSendDumpDataStart, .-NandcSendDumpDataStart
	.align	1
	.global	NandcSendDumpDataDone
	.thumb
	.thumb_func
	.type	NandcSendDumpDataDone, %function
NandcSendDumpDataDone:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.pad #8
	sub	sp, sp, #8
.L407:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #11
	bpl	.L407
	add	sp, sp, #8
	@ sp needed
	bx	lr
	.fnend
	.size	NandcSendDumpDataDone, .-NandcSendDumpDataDone
	.align	1
	.global	NandcXferStart
	.thumb
	.thumb_func
	.type	NandcXferStart, %function
NandcXferStart:
	.fnstart
	@ args = 8, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	mov	r6, r1
	ldr	r1, [sp, #60]
	ldr	fp, [sp, #56]
	cbnz	r1, .L424
	adds	r1, fp, #0
	it	ne
	movne	r1, #1
	b	.L411
.L424:
	movs	r1, #1
.L411:
	ldr	r5, .L429
	mov	lr, #16
	movs	r4, #0
	add	r0, r5, r0, lsl #3
	ldr	r7, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r8, [r7, #12]
	bfi	r8, lr, #8, #8
	bfi	r8, r4, #3, #1
	bfi	r4, r6, #1, #1
	bfi	r8, r0, #5, #3
	orr	r4, r4, #8
	movs	r0, #1
	bfi	r4, r0, #5, #2
	lsrs	r3, r3, r0
	orr	r4, r4, #536870912
	orr	r4, r4, #1024
	bfi	r4, r3, #4, #1
	ldr	r3, [r5, #2260]
	cmp	r3, #3
	bls	.L412
	ldr	r3, [r7, #16]
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #4
	str	r3, [sp, #12]
	cmp	r1, #0
	beq	.L413
	cbnz	r6, .L414
.L422:
	adds	r2, r2, #1
	asrs	r2, r2, #1
	bfi	r4, r2, #22, #6
	cmp	fp, #0
	beq	.L415
	mov	r0, fp
	b	.L416
.L414:
	ldr	r3, [r5, #2308]
	mov	r9, r5
	ldr	lr, [sp, #60]
	cmp	r3, #25
	ite	cc
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp]
	lsrs	r3, r2, #1
	str	r3, [sp, #4]
	movs	r3, #0
	mov	r0, r3
.L418:
	ldr	r1, [sp, #4]
	cmp	r0, r1
	bcs	.L422
	ldr	r1, [sp, #60]
	lsr	r10, r3, #2
	cbz	r1, .L419
	ldrh	r1, [lr, #2]
	ldrh	ip, [lr], #4
	orr	ip, ip, r1, lsl #16
	ldr	r1, [r9, #2276]
	str	ip, [r1, r10, lsl #2]
	b	.L420
.L419:
	ldr	r1, [r9, #2276]
	mov	ip, r1
	mov	r1, #-1
	str	r1, [ip, r10, lsl #2]
.L420:
	ldr	r1, [sp]
	adds	r0, r0, #1
	add	r3, r3, r1
	b	.L418
.L415:
	ldr	r0, [r5, #2272]
.L416:
	ldr	r3, [r5, #2276]
	ubfx	r9, r4, #22, #5
	mov	r2, r6
	str	r0, [r5, #2280]
	lsl	r1, r9, #10
	str	r3, [r5, #2284]
	bl	rknand_dma_map_single
	mov	r2, r6
	lsl	r1, r9, #7
	clz	r6, r6
	lsrs	r6, r6, #5
	str	r0, [r5, #2288]
	ldr	r0, [r5, #2284]
	bl	rknand_dma_map_single
	movs	r3, #1
	str	r3, [r5, #2296]
	tst	fp, #3
	ldr	r3, [r5, #2288]
	str	r0, [r5, #2292]
	str	r3, [r7, #20]
	ldr	r3, [r5, #2292]
	str	r3, [r7, #24]
	mov	r3, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #15872
	orr	r3, r3, #8192
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #448
	str	r3, [sp, #12]
	itttt	eq
	ldreq	r3, [sp, #12]
	biceq	r3, r3, #56
	orreq	r3, r3, #16
	streq	r3, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #4
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #2
	orr	r6, r3, r6, lsl #1
	str	r6, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #1
	str	r3, [sp, #12]
.L413:
	ldr	r3, [sp, #12]
	str	r3, [r7, #16]
.L412:
	str	r8, [r7, #12]
	str	r4, [r7, #8]
	orr	r4, r4, #4
	str	r4, [r7, #8]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L430:
	.align	2
.L429:
	.word	.LANCHOR0
	.fnend
	.size	NandcXferStart, .-NandcXferStart
	.align	1
	.global	Ftl_log2
	.thumb
	.thumb_func
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r1, #0
	movs	r2, #1
.L432:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bhi	.L434
	lsls	r2, r2, #1
	b	.L432
.L434:
	subs	r0, r3, #1
	uxth	r0, r0
	bx	lr
	.fnend
	.size	Ftl_log2, .-Ftl_log2
	.align	1
	.global	FtlPrintInfo
	.thumb
	.thumb_func
	.type	FtlPrintInfo, %function
FtlPrintInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	FtlPrintInfo, .-FtlPrintInfo
	.align	1
	.global	FtlSysBlkNumInit
	.thumb
	.thumb_func
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L438
	cmp	r0, #23
	it	ls
	movls	r0, #24
	ldrh	r2, [r3, #2320]
	ldrh	r1, [r3, #2330]
	str	r0, [r3, #2316]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #2336]
	strh	r0, [r3, #2328]	@ movhi
	movs	r0, #0
	str	r2, [r3, #2324]
	subs	r2, r1, r2
	str	r2, [r3, #2332]
	bx	lr
.L439:
	.align	2
.L438:
	.word	.LANCHOR0
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	1
	.global	FtlConstantsInit
	.thumb
	.thumb_func
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrh	r1, [r0, #14]
	mov	r5, r0
	ldr	r4, .L468
	ldrh	lr, [r0, #8]
	str	r1, [sp]
	ldrh	r1, [sp]
	ldrh	r2, [r0, #10]
	ldrh	r3, [r0, #12]
	addw	r0, r4, #2348
	strh	r1, [r4, #2330]	@ movhi
	movs	r1, #0
	strh	lr, [r4, #2340]	@ movhi
	strh	r2, [r4, #2342]	@ movhi
	strh	r3, [r4, #2344]	@ movhi
.L441:
	strb	r1, [r1, r0]
	adds	r1, r1, #1
	cmp	r1, #32
	bne	.L441
	ldrh	r0, [r5, #14]
	ldrh	r1, [r5, #20]
	cmp	r1, r0, lsr #8
	bcs	.L442
	uxtb	r10, r3
	ldr	r9, .L468+4
	lsl	r1, r10, #1
	uxtb	r1, r1
	str	r1, [sp, #8]
	subs	r1, r2, #1
	muls	r1, r3, r1
	str	r1, [sp, #4]
	movs	r1, #0
.L443:
	cmp	r1, r3
	bcs	.L445
	ldr	r6, [sp, #4]
	rsb	ip, r3, r1
	uxtb	r0, r1
	add	ip, ip, r9
	adds	r6, r1, r6
	add	r6, r9, r6
	str	r6, [sp, #12]
	movs	r6, #0
	mov	r8, r6
.L446:
	cmp	r8, r2
	add	r6, r6, r3
	bcs	.L467
	ldr	r7, [sp, #12]
	add	fp, r0, r10
	strb	r0, [ip, r6]
	add	r8, r8, #1
	strb	fp, [r7, r6]
	ldr	r7, [sp, #8]
	add	r0, r0, r7
	uxtb	r0, r0
	b	.L446
.L467:
	adds	r1, r1, #1
	b	.L443
.L445:
	lsls	r2, r2, #1
	strh	r2, [r4, #2342]	@ movhi
	ldr	r2, [sp]
	lsrs	r7, r2, #1
	strh	r7, [r4, #2330]	@ movhi
.L442:
	cmp	lr, #1
	mov	r2, #5
	strh	r2, [r4, #2380]	@ movhi
	mov	r2, #0
	strh	r2, [r4, #2382]	@ movhi
	it	eq
	ldreq	r2, .L468
	ldrb	r9, [r4]	@ zero_extendqisi2
	it	eq
	strheq	lr, [r2, #2380]	@ movhi
	mov	r2, #4352
	strh	r2, [r4, #2384]	@ movhi
	cmp	r9, #0
	beq	.L448
	ldr	r2, .L468
	mov	r1, #384
	strh	r1, [r2, #2384]	@ movhi
.L448:
	ldrh	r7, [r4, #2342]
	ldrh	r6, [r4, #2330]
	ldrh	r8, [r5, #16]
	smulbb	r7, r7, r3
	ldrh	fp, [r5, #20]
	smulbb	r3, r6, r3
	ldrh	r1, [r5, #18]
	mov	r0, fp
	strh	r8, [r4, #2388]	@ movhi
	strh	fp, [r4, #2394]	@ movhi
	uxth	r7, r7
	strh	r1, [r4, #2390]	@ movhi
	str	r1, [sp]
	strh	r3, [r4, #2386]	@ movhi
	smulbb	r3, r7, r8
	strh	r7, [r4, #2320]	@ movhi
	strh	r3, [r4, #2392]	@ movhi
	bl	Ftl_log2
	lsl	r3, fp, #9
	cmp	r6, #1024
	uxth	r3, r3
	strh	r3, [r4, #2398]	@ movhi
	it	hi
	ldrhi	r2, .L468
	lsr	r3, r3, #8
	strh	r3, [r4, #2400]	@ movhi
	ldrh	r3, [r5, #26]
	strh	r3, [r4, #2402]	@ movhi
	mul	r3, r6, r7
	str	r3, [r4, #2336]
	itt	hi
	uxtbhi	r3, r6
	strhhi	r3, [r2, #2382]	@ movhi
	mov	r10, r0
	ldrh	r5, [r4, #2382]
	ldr	r1, [sp]
	subs	r5, r6, r5
	strh	r0, [r4, #2396]	@ movhi
	ldrh	r0, [r4, #2384]
	muls	r5, r7, r5
	ldr	r3, .L468
	lsls	r0, r0, #3
	mul	r1, r1, fp
	str	r3, [sp]
	mul	r5, fp, r5
	mul	r5, r8, r5
	asrs	r5, r5, #11
	str	r5, [r4, #2404]
	bl	__aeabi_idiv
	ldr	r3, [sp]
	uxth	r0, r0
	cmp	r0, #4
	itet	ls
	movls	r2, #4
	strhhi	r0, [r3, #2408]	@ movhi
	strhls	r2, [r3, #2408]	@ movhi
	cmp	r9, #0
	beq	.L452
	mov	r3, #640
	strh	r3, [r4, #2384]	@ movhi
.L452:
	ldrh	r3, [r4, #2384]
	lsls	r6, r6, #6
	mov	r1, r7
	ldr	r5, .L468
	asr	r3, r3, r10
	add	r10, r10, #9
	asr	r6, r6, r10
	adds	r3, r3, #2
	strh	r3, [r4, #2410]	@ movhi
	uxth	r0, r6
	strh	r6, [r4, #2412]	@ movhi
	add	r6, r0, #8
	mul	r3, r7, r0
	ldrh	r0, [r4, #2408]
	str	r3, [r4, #2416]
	bl	__aeabi_uidiv
	cmp	r7, #1
	uxtah	r0, r6, r0
	ldr	r6, .L468
	it	eq
	addeq	r0, r0, #4
	str	r0, [r5, #2316]
	ldrh	r0, [r4, #2316]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #2316]
	ldr	r5, [r4, #2332]
	mov	r0, #2048
	ldrh	r7, [r4, #2394]
	str	r3, [r4, #2420]
	ldrh	r3, [r4, #2388]
	lsls	r5, r5, #2
	mov	r1, r7
	muls	r5, r3, r5
	ldrh	r3, [r4, #2396]
	adds	r3, r3, #9
	lsrs	r5, r5, r3
	adds	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, #2424]	@ movhi
	bl	__aeabi_idiv
	ldrh	r2, [r4, #2408]
	movs	r3, #0
	str	r3, [r4, #2428]
	adds	r3, r2, #3
	strh	r3, [r4, #2408]	@ movhi
	ldr	r3, [r4, #2416]
	adds	r1, r3, #3
	str	r1, [r4, #2416]
	strh	r0, [r4, #2426]	@ movhi
	ldrb	r0, [r4, #144]	@ zero_extendqisi2
	cbz	r0, .L455
	adds	r2, r2, #4
	adds	r3, r3, #5
	strh	r2, [r6, #2408]	@ movhi
	b	.L466
.L455:
	cmp	r1, #7
	bhi	.L456
	movs	r3, #8
.L466:
	str	r3, [r6, #2416]
.L456:
	ldrh	r2, [r4, #2328]
	movs	r3, #0
	strh	r3, [r4, #2432]	@ movhi
	movs	r0, #0
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	adds	r3, r3, #52
	add	r5, r3, r5, lsl #2
	cmp	r5, r7, lsl #9
	ittt	cc
	movcc	r2, #1
	ldrcc	r3, .L468
	strhcc	r2, [r3, #2432]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L469:
	.align	2
.L468:
	.word	.LANCHOR0
	.word	.LANCHOR0+2348
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	1
	.global	IsBlkInVendorPart
	.thumb
	.thumb_func
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L477
	ldrh	r3, [r2, #2434]
	cbz	r3, .L476
	ldr	r3, [r2, #2436]
	ldrh	r2, [r2, #2408]
	add	r2, r3, r2, lsl #1
.L472:
	cmp	r3, r2
	beq	.L476
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L472
	movs	r0, #1
	bx	lr
.L476:
	movs	r0, #0
	bx	lr
.L478:
	.align	2
.L477:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	1
	.global	FtlCacheMetchLpa
	.thumb
	.thumb_func
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L487
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r2, #2440]
	cbz	r3, .L486
	ldr	r5, [r2, #2444]
	movs	r6, #36
	movs	r2, #0
.L482:
	mla	r4, r6, r2, r5
	ldr	r4, [r4, #16]
	cmp	r4, r0
	bcc	.L481
	cmp	r4, r1
	bls	.L484
.L481:
	adds	r2, r2, #1
	cmp	r2, r3
	bne	.L482
.L486:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L484:
	movs	r0, #1
	pop	{r4, r5, r6, pc}
.L488:
	.align	2
.L487:
	.word	.LANCHOR0
	.fnend
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	1
	.global	FtlGetCap
	.thumb
	.thumb_func
	.type	FtlGetCap, %function
FtlGetCap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L490
	ldr	r0, [r3, #2428]
	bx	lr
.L491:
	.align	2
.L490:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCap, .-FtlGetCap
	.align	1
	.global	FtlGetCapacity
	.thumb
	.thumb_func
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L493
	ldr	r0, [r3, #2428]
	bx	lr
.L494:
	.align	2
.L493:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	1
	.global	ftl_get_density
	.thumb
	.thumb_func
	.type	ftl_get_density, %function
ftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L496
	ldr	r0, [r3, #2428]
	bx	lr
.L497:
	.align	2
.L496:
	.word	.LANCHOR0
	.fnend
	.size	ftl_get_density, .-ftl_get_density
	.align	1
	.global	FtlGetLpn
	.thumb
	.thumb_func
	.type	FtlGetLpn, %function
FtlGetLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L499
	ldr	r0, [r3, #2448]
	bx	lr
.L500:
	.align	2
.L499:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLpn, .-FtlGetLpn
	.align	1
	.global	FtlBbmMapBadBlock
	.thumb
	.thumb_func
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r6, r0
	ldr	r5, .L502
	ldrh	r4, [r5, #2386]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, lsl #2
	ldr	r4, [r1, #2480]
	movs	r1, #1
	subs	r3, r6, r3
	uxth	r3, r3
	and	r0, r3, #31
	lsrs	r7, r3, #5
	lsls	r1, r1, r0
	ldr	r0, [r4, r7, lsl #2]
	orrs	r1, r1, r0
	ldr	r0, .L502+4
	str	r1, [r4, r7, lsl #2]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	ldrh	r3, [r5, #2458]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r5, #2458]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L503:
	.align	2
.L502:
	.word	.LANCHOR0
	.word	.LC2
	.fnend
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.global	__aeabi_uidivmod
	.align	1
	.global	FtlBbmIsBadBlock
	.thumb
	.thumb_func
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r7, r0
	ldr	r5, .L505
	ldrh	r6, [r5, #2386]
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r7
	uxth	r4, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	lsrs	r2, r4, #5
	and	r4, r4, #31
	uxth	r0, r0
	add	r5, r5, r0, lsl #2
	ldr	r3, [r5, #2480]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L506:
	.align	2
.L505:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	1
	.global	FtlBbtInfoPrint
	.thumb
	.thumb_func
	.type	FtlBbtInfoPrint, %function
FtlBbtInfoPrint:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	FtlBbtInfoPrint, .-FtlBbtInfoPrint
	.align	1
	.global	FtlBbtCalcTotleCnt
	.thumb
	.thumb_func
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	ldr	r4, .L516
	ldrh	r6, [r4, #2386]
	ldrh	r3, [r4, #2342]
	mov	r4, r5
	muls	r6, r3, r6
.L509:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L515
	bl	FtlBbmIsBadBlock
	cbz	r0, .L510
	adds	r4, r4, #1
	uxth	r4, r4
.L510:
	adds	r5, r5, #1
	b	.L509
.L515:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L517:
	.align	2
.L516:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	1
	.global	V2P_block
	.thumb
	.thumb_func
	.type	V2P_block, %function
V2P_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r1
	ldr	r6, .L519
	mov	r7, r0
	ldrh	r5, [r6, #2344]
	mov	r1, r5
	bl	__aeabi_uidivmod
	mov	r0, r7
	smlabb	r4, r4, r5, r1
	mov	r1, r5
	bl	__aeabi_uidiv
	ldrh	r3, [r6, #2386]
	smlabb	r0, r3, r0, r4
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.L520:
	.align	2
.L519:
	.word	.LANCHOR0
	.fnend
	.size	V2P_block, .-V2P_block
	.align	1
	.global	P2V_plane
	.thumb
	.thumb_func
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L522
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r1, [r3, #2386]
	ldrh	r5, [r3, #2344]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r1, r4
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L523:
	.align	2
.L522:
	.word	.LANCHOR0
	.fnend
	.size	P2V_plane, .-P2V_plane
	.align	1
	.global	P2V_block_in_plane
	.thumb
	.thumb_func
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L525
	ldrh	r1, [r4, #2386]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #2344]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L526:
	.align	2
.L525:
	.word	.LANCHOR0
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	1
	.global	ftl_cmp_data_ver
	.thumb
	.thumb_func
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, r1
	bls	.L528
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L528:
	subs	r0, r1, r0
	cmp	r0, #-2147483648
	ite	ls
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	1
	.global	FtlFreeSysBlkQueueEmpty
	.thumb
	.thumb_func
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L531
	ldrh	r0, [r3, #2518]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L532:
	.align	2
.L531:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	1
	.global	FtlFreeSysBlkQueueFull
	.thumb
	.thumb_func
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L534
	ldrh	r0, [r3, #2518]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L535:
	.align	2
.L534:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	1
	.global	FtlFreeSysBlkQueueIn
	.thumb
	.thumb_func
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	subs	r3, r0, #1
	movw	r2, #65533
	mov	r6, r0
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L536
	ldr	r4, .L545
	ldrh	r3, [r4, #2518]
	cmp	r3, #1024
	beq	.L536
	cbz	r1, .L538
	ldr	r5, .L545+4
	ldr	r3, [r5, #224]
	cbnz	r3, .L538
	bl	P2V_block_in_plane
	movs	r1, #1
	lsls	r3, r6, #10
	mov	r2, r1
	mov	r7, r0
	ldr	r0, [r5, #228]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r5, #232]
	ldrh	r3, [r2, r7, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	ldr	r3, [r5, #236]
	adds	r3, r3, #1
	str	r3, [r5, #236]
.L538:
	ldrh	r3, [r4, #2518]
	adds	r3, r3, #1
	strh	r3, [r4, #2518]	@ movhi
	ldrh	r3, [r4, #2516]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #2516]	@ movhi
	strh	r6, [r2, #2520]	@ movhi
.L536:
	pop	{r3, r4, r5, r6, r7, pc}
.L546:
	.align	2
.L545:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	1
	.global	FtlFreeSysBLkSort
	.thumb
	.thumb_func
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L558
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r4, [r3, #268]
	ldr	r3, .L558+4
	ldrh	r2, [r3, #2518]
	cbz	r2, .L547
	movs	r0, #0
	ldrh	r1, [r3, #2514]
	ldrh	r2, [r3, #2516]
	and	r4, r4, #31
	mov	r6, r0
.L549:
	uxth	r5, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bge	.L557
	add	r5, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r5, #2520]
	add	r5, r3, r2, lsl #1
	strh	r6, [r5, #2520]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L549
.L557:
	cbz	r6, .L547
	strh	r1, [r3, #2514]	@ movhi
	strh	r2, [r3, #2516]	@ movhi
.L547:
	pop	{r4, r5, r6, pc}
.L559:
	.align	2
.L558:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	1
	.global	FtlFreeSysBlkQueueOut
	.thumb
	.thumb_func
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L570
	ldr	r6, .L570+4
	mov	r7, r4
.L561:
	ldrh	r1, [r4, #2518]
	cbz	r1, .L562
	ldrh	r3, [r4, #2514]
	subs	r1, r1, #1
	ldr	r10, [r6, #224]
	strh	r1, [r4, #2518]	@ movhi
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldr	r8, .L570+4
	ldrh	r5, [r2, #2520]
	strh	r3, [r4, #2514]	@ movhi
	cmp	r10, #0
	bne	.L563
	mov	r0, r5
	bl	P2V_block_in_plane
	lsls	r3, r5, #10
	mov	r9, r0
	ldr	r0, [r6, #228]
	str	r3, [r0, #4]
	ldrb	r3, [r4, #144]	@ zero_extendqisi2
	cbz	r3, .L564
	mov	r1, r10
	movs	r2, #1
	bl	FlashEraseBlocks
.L564:
	movs	r1, #1
	ldr	r0, [r8, #228]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r2, [r8, #232]
	ldrh	r3, [r2, r9, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r9, lsl #1]	@ movhi
	ldr	r3, [r8, #236]
	adds	r3, r3, #1
	str	r3, [r8, #236]
	b	.L563
.L562:
	ldr	r0, .L570+8
	bl	printk
.L565:
	b	.L565
.L563:
	subs	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L566
	mov	r1, r5
	ldrh	r2, [r7, #2518]
	ldr	r0, .L570+12
	bl	printk
	b	.L561
.L566:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L571:
	.align	2
.L570:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC3
	.word	.LC4
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	1
	.global	test_node_in_list
	.thumb
	.thumb_func
	.type	test_node_in_list, %function
test_node_in_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L577
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	movw	r5, #65535
	ldr	r4, [r3, #288]
	ldr	r2, [r0]
	ldr	r3, .L577+4
	subs	r0, r2, r4
	asrs	r0, r0, #1
	muls	r3, r0, r3
	movs	r0, #6
	uxth	r3, r3
.L574:
	cmp	r1, r3
	beq	.L575
	ldrh	r3, [r2]
	cmp	r3, r5
	beq	.L576
	mla	r2, r0, r3, r4
	b	.L574
.L575:
	movs	r0, #1
	pop	{r4, r5, pc}
.L576:
	movs	r0, #0
	pop	{r4, r5, pc}
.L578:
	.align	2
.L577:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	test_node_in_list, .-test_node_in_list
	.align	1
	.global	insert_data_list
	.thumb
	.thumb_func
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L595
	ldrh	r3, [r5, #2328]
	cmp	r3, r0
	bls	.L581
	movs	r4, #6
	ldr	r2, .L595+4
	movw	r3, #65535
	muls	r4, r0, r4
	ldr	lr, [r2, #288]
	mov	r7, r2
	add	r1, lr, r4
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [lr, r4]	@ movhi
	ldr	r3, [r2, #292]
	cmp	r3, #0
	beq	.L594
	ldr	r8, [r2, #296]
	lsl	fp, r0, #1
	ldrh	r2, [r1, #4]
	ldrh	r6, [r8, r0, lsl #1]
	cbz	r2, .L592
	muls	r6, r2, r6
	b	.L583
.L592:
	mov	r6, #-1
.L583:
	ldr	ip, [r7, #288]
	ldr	r2, .L595+8
	rsb	r9, ip, r3
	ldrh	r5, [r5, #2328]
	asr	r9, r9, #1
	str	r5, [sp]
	mul	r2, r2, r9
	ldr	r9, [r7, #232]
	add	r5, r9, fp
	str	r5, [sp, #4]
	movs	r5, #0
	uxth	r2, r2
.L590:
	adds	r5, r5, #1
	ldr	r7, [sp]
	uxth	r5, r5
	cmp	r5, r7
	bhi	.L581
	cmp	r0, r2
	beq	.L581
	ldrh	r7, [r3, #4]
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	cbz	r7, .L593
	mul	r7, r7, fp
	b	.L585
.L593:
	mov	r7, #-1
.L585:
	cmp	r7, r6
	bne	.L586
	ldr	r7, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L588
	b	.L587
.L586:
	bhi	.L587
.L588:
	ldrh	r7, [r3]
	movw	r10, #65535
	cmp	r7, r10
	bne	.L589
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L595+4
	str	r1, [r3, #300]
	b	.L581
.L589:
	movs	r3, #6
	mov	r2, r7
	mla	r3, r3, r7, ip
	b	.L590
.L587:
	strh	r2, [lr, r4]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L595+4
	ldr	r4, [r2, #292]
	cmp	r3, r4
	bne	.L591
	strh	r0, [r3, #2]	@ movhi
.L594:
	str	r1, [r2, #292]
	b	.L581
.L591:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #288]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L581:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L596:
	.align	2
.L595:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	insert_data_list, .-insert_data_list
	.align	1
	.global	INSERT_DATA_LIST
	.thumb
	.thumb_func
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	insert_data_list
	ldr	r2, .L598
	ldrh	r3, [r2, #304]
	adds	r3, r3, #1
	strh	r3, [r2, #304]	@ movhi
	pop	{r3, pc}
.L599:
	.align	2
.L598:
	.word	.LANCHOR2
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	1
	.global	insert_free_list
	.thumb
	.thumb_func
	.type	insert_free_list, %function
insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r5, #65535
	cmp	r0, r5
	beq	.L601
	movs	r1, #6
	ldr	r2, .L608
	mul	lr, r1, r0
	ldr	ip, [r2, #288]
	mov	r7, r2
	add	r6, ip, lr
	strh	r5, [r6, #2]	@ movhi
	strh	r5, [ip, lr]	@ movhi
	ldr	r3, [r2, #308]
	cbz	r3, .L607
	ldr	r8, [r2, #288]
	ldr	r9, [r2, #232]
	rsb	r4, r8, r3
	ldr	r2, .L608+4
	asrs	r4, r4, #1
	ldrh	r10, [r9, r0, lsl #1]
	muls	r4, r2, r4
	uxth	r2, r4
.L605:
	ldrh	r4, [r9, r2, lsl #1]
	cmp	r4, r10
	bcs	.L603
	ldrh	r4, [r3]
	cmp	r4, r5
	bne	.L604
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L601
.L604:
	mla	r3, r1, r4, r8
	mov	r2, r4
	b	.L605
.L603:
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	strh	r2, [ip, lr]	@ movhi
	ldr	r1, [r7, #308]
	ldr	r2, .L608
	cmp	r3, r1
	bne	.L606
	strh	r0, [r3, #2]	@ movhi
.L607:
	str	r6, [r2, #308]
	b	.L601
.L606:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #288]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L601:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L609:
	.align	2
.L608:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	insert_free_list, .-insert_free_list
	.align	1
	.global	INSERT_FREE_LIST
	.thumb
	.thumb_func
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	insert_free_list
	ldr	r2, .L611
	ldrh	r3, [r2, #312]
	adds	r3, r3, #1
	strh	r3, [r2, #312]	@ movhi
	pop	{r3, pc}
.L612:
	.align	2
.L611:
	.word	.LANCHOR2
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	1
	.global	List_remove_node
	.thumb
	.thumb_func
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movs	r4, #6
	ldr	r7, .L618
	movw	r6, #65535
	muls	r1, r4, r1
	ldr	r3, [r0]
	ldr	r2, [r7, #288]
	adds	r5, r2, r1
	cmp	r5, r3
	ldrh	r3, [r2, r1]
	bne	.L614
	cmp	r3, r6
	ittee	eq
	moveq	r3, #0
	streq	r3, [r0]
	mlane	r3, r4, r3, r2
	strne	r3, [r0]
	it	ne
	strhne	r6, [r3, #2]	@ movhi
	b	.L616
.L614:
	cmp	r3, r6
	ldrh	r0, [r5, #2]
	bne	.L617
	cmp	r0, r3
	beq	.L616
	muls	r0, r4, r0
	strh	r3, [r2, r0]	@ movhi
	b	.L616
.L617:
	mla	r3, r4, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r7, #288]
	muls	r4, r6, r4
	strh	r0, [r3, r4]	@ movhi
.L616:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, pc}
.L619:
	.align	2
.L618:
	.word	.LANCHOR2
	.fnend
	.size	List_remove_node, .-List_remove_node
	.align	1
	.global	List_pop_index_node
	.thumb
	.thumb_func
	.type	List_pop_index_node, %function
List_pop_index_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r0]
	cbz	r3, .L626
	ldr	r2, .L627
	movs	r6, #6
	movw	r5, #65535
	ldr	r4, [r2, #288]
.L622:
	cbnz	r1, .L623
.L625:
	subs	r4, r3, r4
	ldr	r3, .L627+4
	asrs	r4, r4, #1
	muls	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L623:
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L625
	subs	r1, r1, #1
	mla	r3, r6, r2, r4
	uxth	r1, r1
	b	.L622
.L626:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L628:
	.align	2
.L627:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	List_pop_index_node, .-List_pop_index_node
	.align	1
	.global	List_get_gc_head_node
	.thumb
	.thumb_func
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L636
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #292]
	cbz	r3, .L634
	movs	r4, #6
	ldr	r1, [r2, #288]
	movw	r2, #65535
.L631:
	cbz	r0, .L632
	ldrh	r3, [r3]
	cmp	r3, r2
	beq	.L634
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L631
.L634:
	movw	r0, #65535
	pop	{r4, pc}
.L632:
	subs	r3, r3, r1
	ldr	r0, .L636+4
	asrs	r3, r3, #1
	muls	r0, r3, r0
	uxth	r0, r0
	pop	{r4, pc}
.L637:
	.align	2
.L636:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	1
	.global	List_update_data_list
	.thumb
	.thumb_func
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r0
	ldr	r4, .L645
	ldrh	r3, [r4, #316]
	cmp	r3, r0
	beq	.L639
	ldrh	r3, [r4, #364]
	cmp	r3, r0
	beq	.L639
	ldrh	r3, [r4, #412]
	cmp	r3, r0
	beq	.L639
	movs	r3, #6
	ldr	r1, [r4, #288]
	ldr	r2, [r4, #292]
	muls	r3, r0, r3
	adds	r0, r1, r3
	cmp	r0, r2
	beq	.L639
	ldr	r6, [r4, #296]
	ldrh	r2, [r0, #4]
	ldrh	r7, [r6, r5, lsl #1]
	cbz	r2, .L643
	muls	r2, r7, r2
	b	.L640
.L643:
	mov	r2, #-1
.L640:
	ldrh	r0, [r0, #2]
	movw	r7, #65535
	cmp	r0, r7
	bne	.L641
	ldrh	r3, [r1, r3]
	cmp	r3, r0
	beq	.L639
.L641:
	movs	r3, #6
	muls	r3, r0, r3
	ldr	r0, .L645+4
	asrs	r7, r3, #1
	add	r3, r3, r1
	muls	r0, r7, r0
	ldrh	r3, [r3, #4]
	ldrh	r0, [r6, r0, lsl #1]
	cbz	r3, .L644
	muls	r3, r0, r3
	b	.L642
.L644:
	mov	r3, #-1
.L642:
	cmp	r2, r3
	bcs	.L639
	ldr	r0, .L645+8
	mov	r1, r5
	bl	List_remove_node
	ldrh	r3, [r4, #304]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #304]	@ movhi
	bl	INSERT_DATA_LIST
.L639:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L646:
	.align	2
.L645:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+292
	.fnend
	.size	List_update_data_list, .-List_update_data_list
	.align	1
	.global	ftl_map_blk_alloc_new_blk
	.thumb
	.thumb_func
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r3, #0
	ldrh	r1, [r0, #10]
	mov	r4, r0
	ldr	r2, [r0, #12]
.L648:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L651
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L648
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	subs	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L649
	ldr	r3, .L654
	ldr	r0, .L654+4
	ldrh	r2, [r3, #2518]
	bl	printk
.L650:
	b	.L650
.L649:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L651:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L655:
	.align	2
.L654:
	.word	.LANCHOR0
	.word	.LC5
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	1
	.global	select_l2p_ram_region
	.thumb
	.thumb_func
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movs	r1, #0
	ldr	r3, .L670
	movs	r4, #12
	ldr	r7, .L670+4
	movw	r5, #65535
	ldrh	r2, [r3, #2426]
	ldr	r3, [r7, #460]
.L657:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L668
	adds	r1, r1, #1
	mla	r6, r4, r1, r3
	ldrh	r6, [r6, #-12]
	cmp	r6, r5
	bne	.L657
	b	.L658
.L668:
	movs	r1, #0
	mov	r6, #-2147483648
	mov	lr, #12
	mov	r0, r2
.L660:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L669
	mla	r4, lr, r1, r3
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L661
	cmp	r4, r6
	itt	cc
	movcc	r6, r4
	movcc	r0, r5
.L661:
	adds	r1, r1, #1
	b	.L660
.L669:
	cmp	r0, r2
	bcc	.L658
	ldrh	r7, [r7, #464]
	movs	r1, #0
	mov	r4, #-1
	mov	r0, r2
.L663:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L658
	ldr	r6, [r3, #4]
	cmp	r6, r4
	bcs	.L664
	ldrh	lr, [r3]
	cmp	lr, r7
	itt	ne
	movne	r4, r6
	movne	r0, r5
.L664:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L663
.L658:
	pop	{r4, r5, r6, r7, pc}
.L671:
	.align	2
.L670:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	1
	.global	FtlUpdateVaildLpn
	.thumb
	.thumb_func
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L679
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r2, [r1, #466]
	mov	r3, r1
	cmp	r2, #4
	bhi	.L673
	cbnz	r0, .L673
	adds	r2, r2, #1
	strh	r2, [r1, #466]	@ movhi
	pop	{r4, r5, pc}
.L673:
	ldr	r1, .L679+4
	movs	r2, #0
	strh	r2, [r3, #466]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #468]
	ldrh	r1, [r1, #2328]
	ldr	r2, [r3, #296]
	add	r1, r2, r1, lsl #1
.L674:
	cmp	r2, r1
	beq	.L678
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #468]
	addne	r0, r0, r4
	strne	r0, [r3, #468]
	b	.L674
.L678:
	pop	{r4, r5, pc}
.L680:
	.align	2
.L679:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	1
	.global	ftl_set_blk_mode
	.thumb
	.thumb_func
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	lsrs	r3, r0, #5
	ldr	r2, .L684
	and	r0, r0, #31
	uxth	r3, r3
	cbz	r1, .L682
	ldr	r1, [r2, #472]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	orrs	r2, r2, r0
	str	r2, [r1, r3, lsl #2]
	bx	lr
.L682:
	ldr	r1, [r2, #472]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	bic	r0, r0, r2
	str	r0, [r1, r3, lsl #2]
	bx	lr
.L685:
	.align	2
.L684:
	.word	.LANCHOR2
	.fnend
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	1
	.global	ftl_get_blk_mode
	.thumb
	.thumb_func
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L687
	lsrs	r1, r0, #5
	ldr	r2, [r3, #472]
	and	r3, r0, #31
	ldr	r0, [r2, r1, lsl #2]
	lsrs	r0, r0, r3
	and	r0, r0, #1
	bx	lr
.L688:
	.align	2
.L687:
	.word	.LANCHOR2
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	1
	.global	ftl_sb_update_avl_pages
	.thumb
	.thumb_func
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movs	r3, #0
	strh	r3, [r0, #4]	@ movhi
	ldr	r3, .L698
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	adds	r4, r2, #7
	ldrh	r5, [r3, #2320]
	movw	r7, #65535
	add	r4, r0, r4, lsl #1
.L690:
	cmp	r2, r5
	bcs	.L696
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	cmp	r6, r7
	uxth	r2, r2
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L690
.L696:
	ldrh	r6, [r3, #2388]
	add	r4, r0, #14
	movs	r2, #0
	movw	r7, #65535
.L693:
	uxth	r3, r2
	cmp	r3, r5
	bcs	.L697
	ldrh	r3, [r4, #2]!
	adds	r2, r2, #1
	cmp	r3, r7
	itttt	ne
	ldrhne	r3, [r0, #4]
	addne	r3, r3, r6
	addne	r3, r3, #-1
	subne	r3, r3, r1
	it	ne
	strhne	r3, [r0, #4]	@ movhi
	b	.L693
.L697:
	pop	{r4, r5, r6, r7, pc}
.L699:
	.align	2
.L698:
	.word	.LANCHOR0
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	1
	.global	make_superblock
	.thumb
	.thumb_func
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r3, #0
	ldr	r6, .L713
	add	r7, r0, #16
	mov	r4, r0
	mov	r5, r3
	addw	r10, r6, #2348
	movw	r9, #65535
	ldrh	r8, [r6, #2320]
	strh	r3, [r0, #4]	@ movhi
	strb	r3, [r0, #7]
.L701:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L712
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	ldrh	r1, [r4]
	bl	V2P_block
	strh	r9, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L702
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L702:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L701
.L712:
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r3, [r6, #2388]
	smulbb	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2244]
	cbz	r3, .L704
	ldr	r3, .L713+4
	ldrh	r2, [r4]
	ldr	r3, [r3, #232]
	ldrh	r3, [r3, r2, lsl #1]
	cmp	r3, #79
	itt	ls
	movls	r3, #1
	strbls	r3, [r4, #9]
.L704:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cbz	r3, .L705
	movs	r3, #1
	strb	r3, [r4, #9]
.L705:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L714:
	.align	2
.L713:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	make_superblock, .-make_superblock
	.align	1
	.global	update_multiplier_value
	.thumb
	.thumb_func
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	movs	r5, #0
	ldr	r3, .L724
	mov	r7, r0
	mov	r4, r5
	addw	r6, r3, #2348
	ldrh	r8, [r3, #2320]
	ldrh	r9, [r3, #2388]
.L716:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L723
	mov	r1, r7
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L717
	add	r4, r4, r9
	uxth	r4, r4
.L717:
	adds	r5, r5, #1
	b	.L716
.L723:
	cbz	r4, .L719
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L719:
	ldr	r3, .L724+4
	movs	r2, #6
	movs	r0, #0
	ldr	r3, [r3, #288]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L725:
	.align	2
.L724:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	update_multiplier_value, .-update_multiplier_value
	.align	1
	.global	GetFreeBlockMinEraseCount
	.thumb
	.thumb_func
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L729
	ldr	r0, [r2, #308]
	cbz	r0, .L727
	ldr	r3, [r2, #288]
	subs	r0, r0, r3
	ldr	r3, .L729+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #232]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L727:
	bx	lr
.L730:
	.align	2
.L729:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	1
	.global	GetFreeBlockMaxEraseCount
	.thumb
	.thumb_func
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L739
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #308]
	cbz	r3, .L737
	ldrh	r2, [r1, #312]
	movs	r6, #6
	ldr	r4, [r1, #288]
	movw	r7, #65535
	rsb	r2, r2, r2, lsl #3
	subs	r3, r3, r4
	asrs	r2, r2, #3
	asrs	r3, r3, #1
	cmp	r0, r2
	it	gt
	uxthgt	r0, r2
	ldr	r2, .L739+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L734:
	uxth	r5, r2
	cmp	r5, r0
	bcs	.L736
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L738
.L736:
	ldr	r2, [r1, #232]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L738:
	mov	r3, r5
	b	.L734
.L737:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L740:
	.align	2
.L739:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	1
	.global	FtlPrintInfo2buf
	.thumb
	.thumb_func
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r6, r0
	ldr	r8, .L751+240
	add	r4, r6, #12
	ldr	r1, .L751
	.pad #32
	sub	sp, sp, #32
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L751+4
	ldr	r2, [r8, #116]
	bl	sprintf
	ldr	r1, .L751+8
	ldr	r2, [r8, #2404]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L751+12
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r4, r0
	bne	.L747
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L751+16
	ldr	r7, .L751+20
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L751+24
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L751+28
	ldr	r2, [r8, #2448]
	bl	sprintf
	ldr	r1, .L751+32
	ldr	r2, [r7, #468]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+36
	ldr	r2, [r7, #476]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+40
	ldr	r2, [r7, #480]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+44
	ldr	r2, [r7, #484]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+48
	ldr	r2, [r7, #488]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+52
	ldr	r2, [r7, #492]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+56
	ldr	r2, [r7, #496]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r7, #500]
	ldr	r1, .L751+60
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r7, #504]
	ldr	r1, .L751+64
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+68
	ldr	r2, [r7, #508]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+72
	ldr	r2, [r7, #512]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldr	r1, .L751+76
	ldrh	r2, [r8, #2458]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+80
	ldrh	r2, [r7, #312]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+84
	ldr	r2, [r7, #516]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+88
	ldr	r2, [r7, #520]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+92
	ldr	r2, [r7, #524]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+96
	ldr	r2, [r7, #236]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+100
	ldr	r2, [r7, #528]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+104
	ldr	r2, [r7, #532]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+108
	ldrh	r2, [r7, #270]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+112
	ldrh	r2, [r7, #268]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+116
	ldr	r2, [r8, #2428]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+120
	ldr	r2, [r8, #2420]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+124
	ldr	r2, [r8, #2316]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+128
	ldrh	r2, [r8, #2518]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+132
	ldrh	r2, [r8, #2328]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+136
	ldrh	r2, [r7, #536]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+140
	ldr	r2, [r8, #2332]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+144
	ldrh	r2, [r7, #540]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+148
	ldrh	r2, [r8, #2452]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+152
	ldrh	r2, [r7, #318]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+156
	ldrb	r2, [r7, #322]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+160
	ldrh	r2, [r7, #316]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+164
	ldrb	r2, [r7, #324]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+168
	ldrh	r2, [r7, #320]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r7, #296]
	ldrh	r2, [r7, #316]
	ldr	r1, .L751+172
	ldrh	r2, [r3, r2, lsl #1]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+176
	ldrh	r2, [r7, #366]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+180
	ldrb	r2, [r7, #370]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+184
	ldrh	r2, [r7, #364]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+188
	ldrb	r2, [r7, #372]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+192
	ldrh	r2, [r7, #368]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r7, #296]
	ldrh	r2, [r7, #364]
	ldr	r1, .L751+196
	ldrh	r2, [r3, r2, lsl #1]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+200
	ldrh	r2, [r7, #414]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+204
	ldrb	r2, [r7, #418]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+208
	ldrh	r2, [r7, #412]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+212
	ldrb	r2, [r7, #420]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+216
	ldrh	r2, [r7, #416]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+220
	ldrh	r2, [r7, #558]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+224
	ldrb	r2, [r7, #562]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+228
	ldrh	r2, [r7, #556]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+232
	ldrb	r2, [r7, #564]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L751+236
	ldrh	r2, [r7, #560]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r7, #692]
	ldr	r3, [r8, #2244]
	ldr	r2, [r7, #604]
	b	.L752
.L753:
	.align	2
.L751:
	.word	.LC6
	.word	.LC7
	.word	.LC8
	.word	.LANCHOR1
	.word	.LC9
	.word	.LANCHOR2
	.word	.LC10
	.word	.LC11
	.word	.LC12
	.word	.LC13
	.word	.LC14
	.word	.LC15
	.word	.LC16
	.word	.LC17
	.word	.LC18
	.word	.LC19
	.word	.LC20
	.word	.LC21
	.word	.LC22
	.word	.LC23
	.word	.LC24
	.word	.LC25
	.word	.LC26
	.word	.LC27
	.word	.LC28
	.word	.LC29
	.word	.LC30
	.word	.LC31
	.word	.LC32
	.word	.LC33
	.word	.LC34
	.word	.LC35
	.word	.LC36
	.word	.LC37
	.word	.LC38
	.word	.LC39
	.word	.LC40
	.word	.LC41
	.word	.LC42
	.word	.LC43
	.word	.LC44
	.word	.LC45
	.word	.LC46
	.word	.LC47
	.word	.LC48
	.word	.LC49
	.word	.LC50
	.word	.LC51
	.word	.LC52
	.word	.LC53
	.word	.LC54
	.word	.LC55
	.word	.LC56
	.word	.LC57
	.word	.LC58
	.word	.LC59
	.word	.LC60
	.word	.LC61
	.word	.LC62
	.word	.LC63
	.word	.LANCHOR0
.L752:
	orr	r2, r3, r2, lsl #8
	str	r1, [sp]
	add	r5, r5, r0
	ldr	r1, [r7, #684]
	mov	r0, r5
	str	r1, [sp, #4]
	ldr	r3, [r7, #688]
	ldr	r1, .L754
	bl	sprintf
	ldr	r1, .L754+4
	ldr	r2, [r7, #680]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L754+8
	ldr	r2, [r7, #704]
	adds	r4, r5, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L754+12
	ldrh	r2, [r7, #1120]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L754+16
	ldrh	r2, [r7, #1122]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L754+20
	ldr	r2, [r7, #1124]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L754+24
	ldrh	r2, [r7, #1128]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L754+28
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r7, #312]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L754+32
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r7, #556]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L744
	ldr	r2, [r7, #296]
	mov	r0, r4
	ldr	r1, .L754+36
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
	add	r4, r4, r0
.L744:
	movs	r0, #0
	movs	r5, #0
	bl	List_get_gc_head_node
	mov	r9, #6
	movw	r10, #65535
	uxth	r3, r0
.L746:
	cmp	r3, r10
	beq	.L745
	ldr	r2, [r7, #296]
	mov	r0, r4
	mul	r8, r9, r3
	ldr	r1, .L754+40
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp]
	ldr	r2, [r7, #288]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r7, #232]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r7, #288]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L746
.L745:
	ldr	r2, [r7, #288]
	movs	r5, #0
	ldr	r3, [r7, #308]
	mov	r9, #6
	movw	r10, #65535
	subs	r3, r3, r2
	ldr	r2, .L754+44
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r3, r3
.L748:
	cmp	r3, r10
	beq	.L747
	mul	r8, r9, r3
	ldr	r2, [r7, #288]
	mov	r0, r4
	ldr	r1, .L754+48
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	ldr	r2, [r7, #232]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r7, #288]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L748
.L747:
	subs	r0, r4, r6
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L755:
	.align	2
.L754:
	.word	.LC64
	.word	.LC65
	.word	.LC66
	.word	.LC67
	.word	.LC68
	.word	.LC69
	.word	.LC70
	.word	.LC71
	.word	.LC72
	.word	.LC73
	.word	.LC74
	.word	-1431655765
	.word	.LC75
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	1
	.global	ftl_proc_ftl_read
	.thumb
	.thumb_func
	.type	ftl_proc_ftl_read, %function
ftl_proc_ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r1, .L757
	ldr	r2, .L757+4
	bl	sprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L758:
	.align	2
.L757:
	.word	.LC76
	.word	.LC77
	.fnend
	.size	ftl_proc_ftl_read, .-ftl_proc_ftl_read
	.align	1
	.global	GetSwlReplaceBlock
	.thumb
	.thumb_func
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L787
	ldr	r2, [r4, #524]
	ldr	r3, [r4, #532]
	cmp	r2, r3
	bcs	.L760
	ldr	r2, .L787+4
	movs	r3, #0
	ldr	r0, [r4, #232]
	str	r3, [r4, #516]
	ldrh	r1, [r2, #2328]
	mov	r5, r2
.L761:
	cmp	r3, r1
	bcs	.L786
	ldrh	r6, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r2, [r4, #516]
	add	r2, r2, r6
	str	r2, [r4, #516]
	b	.L761
.L786:
	ldr	r6, [r4, #516]
	mov	r0, r6
	bl	__aeabi_uidiv
	ldrh	r1, [r5, #2380]
	str	r0, [r4, #524]
	ldr	r0, [r4, #520]
	subs	r0, r6, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #516]
	b	.L763
.L760:
	ldr	r3, [r4, #528]
	cmp	r2, r3
	bls	.L763
	ldr	r0, .L787+4
	adds	r3, r3, #1
	str	r3, [r4, #528]
	movs	r3, #0
.L765:
	ldrh	r2, [r0, #2328]
	cmp	r3, r2
	bcs	.L763
	ldr	r1, [r4, #232]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L765
.L763:
	ldr	r6, [r4, #532]
	ldr	r8, [r4, #524]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L768
	ldr	r2, .L787
	add	r3, r6, #768
	ldr	r2, [r2, #528]
	cmp	r3, r2
	bls	.L768
	ldr	r3, .L787+4
	ldr	r3, [r3, #2244]
	cbnz	r3, .L769
.L771:
	movw	r0, #65535
	b	.L770
.L769:
	cmp	r6, #40
	bhi	.L771
.L768:
	ldrh	r0, [r4, #312]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r9, r0
	bcs	.L772
	cmp	r6, #40
	bhi	.L771
.L772:
	ldr	r3, [r4, #292]
	ldr	r2, .L787
	cmp	r3, #0
	beq	.L771
	ldr	r1, .L787+4
	movw	r7, #65535
	ldr	r0, [r2, #288]
	mov	ip, #6
	ldr	r10, [r2, #232]
	mov	r5, r7
	ldrh	r1, [r1, #2328]
	mov	lr, r7
	str	r1, [sp, #20]
	movs	r1, #0
.L773:
	ldrh	r2, [r3]
	cmp	r2, lr
	str	r2, [sp, #16]
	beq	.L775
	adds	r1, r1, #1
	ldr	r2, [sp, #20]
	uxth	r1, r1
	cmp	r1, r2
	bhi	.L771
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L774
	subs	r3, r3, r0
	ldr	r2, .L787+8
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r3, r3
	ldrh	fp, [r10, r3, lsl #1]
	cmp	fp, r6
	bls	.L779
	cmp	fp, r7
	itt	cc
	movcc	r7, fp
	movcc	r5, r3
.L774:
	ldr	r3, [sp, #16]
	mla	r3, ip, r3, r0
	b	.L773
.L779:
	mov	r5, r3
.L775:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L771
	ldrh	fp, [r10, r5, lsl #1]
	lsls	r3, r5, #1
	cmp	fp, r6
	bls	.L777
	str	r3, [sp, #16]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #16]
	cmp	r0, r6
	it	hi
	strhi	r7, [r4, #532]
.L777:
	cmp	fp, r8
	bcs	.L771
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L771
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L778
	ldr	r2, [r4, #528]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L771
.L778:
	ldr	r2, [r4, #296]
	mov	r1, r5
	ldr	r0, .L787+12
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	str	r9, [sp, #8]
	str	r3, [sp, #4]
	ldr	r3, [r4, #528]
	bl	printk
	mov	r0, r5
	movs	r3, #1
	str	r3, [r4, #1132]
.L770:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L788:
	.align	2
.L787:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC78
	.fnend
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	1
	.global	free_data_superblock
	.thumb
	.thumb_func
	.type	free_data_superblock, %function
free_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r2, #65535
	cmp	r0, r2
	push	{r3, lr}
	.save {r3, lr}
	beq	.L790
	ldr	r2, .L791
	movs	r1, #0
	ldr	r2, [r2, #296]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L790:
	movs	r0, #0
	pop	{r3, pc}
.L792:
	.align	2
.L791:
	.word	.LANCHOR2
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	1
	.global	FtlGcBufInit
	.thumb
	.thumb_func
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L804
	mov	ip, #36
	ldr	r0, .L804+4
	movs	r3, #0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r7, #12
	mov	lr, #1
	mov	r6, r0
	mov	r4, r2
	str	r3, [r2, #1136]
.L794:
	ldrh	r1, [r0, #2320]
	adds	r5, r3, #1
	uxth	r3, r3
	ldr	r8, .L804+4
	cmp	r3, r1
	bcs	.L802
	mul	r8, r7, r3
	ldr	r9, [r2, #1140]
	add	r1, r9, r8
	str	lr, [r1, #8]
	ldrh	r1, [r0, #2398]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	bic	r10, r1, #3
	ldr	r1, [r4, #1144]
	add	r1, r1, r10
	str	r1, [r9, r8]
	ldr	r10, [r2, #1140]
	ldrh	r1, [r6, #2400]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	add	r9, r10, r8
	bic	fp, r1, #3
	ldr	r1, [r4, #1148]
	add	r1, r1, fp
	str	r1, [r9, #4]
	ldr	r1, [r2, #1152]
	mla	r3, ip, r3, r1
	ldr	r1, [r10, r8]
	str	r1, [r3, #8]
	ldr	r1, [r9, #4]
	str	r1, [r3, #12]
	mov	r3, r5
	b	.L794
.L802:
	ldr	r0, .L804
	mov	lr, #12
	movs	r5, #0
.L798:
	ldr	r3, [r2, #1156]
	cmp	r1, r3
	bcs	.L803
	mul	r7, lr, r1
	ldr	r6, [r0, #1140]
	ldr	r4, [r0, #1144]
	adds	r3, r6, r7
	str	r5, [r3, #8]
	ldrh	r3, [r8, #2398]
	muls	r3, r1, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r4
	str	r3, [r6, r7]
	ldr	r3, [r2, #1140]
	ldr	r6, [r0, #1148]
	add	r7, r7, r3
	ldrh	r3, [r8, #2400]
	muls	r3, r1, r3
	add	r1, r1, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r1, r1
	add	r3, r3, r6
	str	r3, [r7, #4]
	b	.L798
.L803:
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L805:
	.align	2
.L804:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	1
	.global	FtlGcBufFree
	.thumb
	.thumb_func
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L813
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	ldr	lr, [r3, #1156]
	mov	r10, #36
	ldr	r6, [r3, #1140]
	mov	r8, #12
	mov	ip, r5
.L807:
	uxth	r4, r5
	cmp	r4, r1
	bcs	.L806
	mla	r4, r10, r4, r0
	movs	r2, #0
.L808:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L809
	mul	r3, r8, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L808
	str	ip, [r9, #8]
.L809:
	adds	r5, r5, #1
	b	.L807
.L806:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L814:
	.align	2
.L813:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	1
	.global	FtlGcBufAlloc
	.thumb
	.thumb_func
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L823
	mov	ip, #1
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	ldr	r5, [r3, #1156]
	mov	lr, #12
	ldr	r6, [r3, #1140]
	mov	r8, #36
.L816:
	uxth	r2, r4
	cmp	r2, r1
	bcs	.L822
	mov	r9, #0
.L817:
	uxth	r3, r9
	cmp	r3, r5
	bcs	.L818
	mla	r3, lr, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L817
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	ip, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L818:
	adds	r4, r4, #1
	b	.L816
.L822:
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L824:
	.align	2
.L823:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	1
	.global	IsBlkInGcList
	.thumb
	.thumb_func
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L831
	ldr	r3, [r2, #1160]
	ldrh	r2, [r2, #1164]
	add	r2, r3, r2, lsl #1
.L826:
	cmp	r3, r2
	beq	.L830
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L826
	movs	r0, #1
	bx	lr
.L830:
	movs	r0, #0
	bx	lr
.L832:
	.align	2
.L831:
	.word	.LANCHOR2
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	1
	.global	FtlGcUpdatePage
	.thumb
	.thumb_func
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r0
	ubfx	r0, r0, #10, #16
	mov	r7, r1
	mov	r8, r2
	bl	P2V_block_in_plane
	ldr	r3, .L840
	movs	r4, #0
	ldrh	r1, [r3, #1164]
	ldr	r2, [r3, #1160]
.L834:
	uxth	r5, r4
	cmp	r5, r1
	bcs	.L838
	adds	r4, r4, #1
	add	lr, r2, r4, lsl #1
	ldrh	lr, [lr, #-2]
	cmp	lr, r0
	bne	.L834
.L838:
	cmp	r5, r1
	bne	.L836
	strh	r0, [r2, r5, lsl #1]	@ movhi
	ldrh	r0, [r3, #1164]
	adds	r0, r0, #1
	strh	r0, [r3, #1164]	@ movhi
.L836:
	ldrh	r2, [r3, #1172]
	movs	r0, #12
	muls	r0, r2, r0
	ldr	r2, [r3, #1168]
	adds	r1, r2, r0
	str	r7, [r1, #4]
	str	r8, [r1, #8]
	str	r6, [r2, r0]
	ldrh	r2, [r3, #1172]
	adds	r2, r2, #1
	strh	r2, [r3, #1172]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L841:
	.align	2
.L840:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	1
	.global	FtlGcRefreshOpenBlock
	.thumb
	.thumb_func
	.type	FtlGcRefreshOpenBlock, %function
FtlGcRefreshOpenBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L849
	ldrh	r3, [r4, #1174]
	cmp	r3, r0
	beq	.L844
	ldrh	r3, [r4, #1176]
	cmp	r3, r0
	beq	.L844
	ldrh	r3, [r4, #1178]
	cmp	r3, r0
	beq	.L844
	ldrh	r3, [r4, #1180]
	cmp	r3, r0
	beq	.L844
	ldr	r0, .L849+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #1174]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L846
	strh	r5, [r4, #1174]	@ movhi
	b	.L844
.L846:
	ldrh	r2, [r4, #1176]
	cmp	r2, r3
	bne	.L847
	strh	r5, [r4, #1176]	@ movhi
	b	.L844
.L847:
	ldrh	r2, [r4, #1178]
	cmp	r2, r3
	bne	.L848
	strh	r5, [r4, #1178]	@ movhi
	b	.L844
.L848:
	ldrh	r2, [r4, #1180]
	cmp	r2, r3
	it	eq
	strheq	r5, [r4, #1180]	@ movhi
.L844:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L850:
	.align	2
.L849:
	.word	.LANCHOR2
	.word	.LC79
	.fnend
	.size	FtlGcRefreshOpenBlock, .-FtlGcRefreshOpenBlock
	.align	1
	.global	FtlGcRefreshBlock
	.thumb
	.thumb_func
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L861
	ldrh	r3, [r4, #1174]
	cmp	r3, r0
	beq	.L859
	ldrh	r3, [r4, #1176]
	cmp	r3, r0
	beq	.L859
	ldrh	r3, [r4, #1178]
	cmp	r3, r0
	beq	.L859
	ldrh	r3, [r4, #1180]
	cmp	r3, r0
	beq	.L859
	ldr	r0, .L861+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #1174]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L853
	strh	r5, [r4, #1174]	@ movhi
	b	.L859
.L853:
	ldrh	r2, [r4, #1176]
	cmp	r2, r3
	bne	.L854
	strh	r5, [r4, #1176]	@ movhi
	b	.L859
.L854:
	ldrh	r2, [r4, #1178]
	cmp	r2, r3
	bne	.L855
	strh	r5, [r4, #1178]	@ movhi
	b	.L859
.L855:
	ldrh	r2, [r4, #1180]
	cmp	r2, r3
	bne	.L860
	strh	r5, [r4, #1180]	@ movhi
.L859:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L860:
	mov	r0, #-1
	pop	{r3, r4, r5, pc}
.L862:
	.align	2
.L861:
	.word	.LANCHOR2
	.word	.LC79
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	1
	.global	FtlGcMarkBadPhyBlk
	.thumb
	.thumb_func
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	bl	P2V_block_in_plane
	ldr	r4, .L872
	mov	r2, r5
	ldrh	r1, [r4, #1182]
	mov	r6, r0
	ldr	r0, .L872+4
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r3, .L872+8
	ldr	r2, [r3, #2244]
	mov	r3, r4
	cbz	r2, .L864
	ldr	r1, [r4, #232]
	ldrh	r2, [r1, r6, lsl #1]
	cmp	r2, #39
	itt	hi
	subhi	r2, r2, #40
	strhhi	r2, [r1, r6, lsl #1]	@ movhi
.L864:
	ldrh	r2, [r3, #1182]
	movs	r1, #0
	ldr	r4, .L872+12
.L865:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L871
	adds	r1, r1, #1
	add	r0, r4, r1, lsl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r5
	bne	.L865
	b	.L866
.L871:
	cmp	r2, #15
	itttt	ls
	addls	r1, r2, #1
	strhls	r1, [r3, #1182]	@ movhi
	addls	r3, r3, r2, lsl #1
	strhls	r5, [r3, #1184]	@ movhi
.L866:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L873:
	.align	2
.L872:
	.word	.LANCHOR2
	.word	.LC80
	.word	.LANCHOR0
	.word	.LANCHOR2+1184
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	1
	.global	FtlGcReFreshBadBlk
	.thumb
	.thumb_func
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L880
	ldrh	r3, [r4, #1182]
	cbz	r3, .L875
	ldrh	r1, [r4, #1174]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L875
	ldrh	r2, [r4, #1218]
	cmp	r2, r3
	itt	cs
	movcs	r3, #0
	strhcs	r3, [r4, #1218]	@ movhi
	ldrh	r3, [r4, #1218]
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #1184]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #1218]
	adds	r3, r3, #1
	strh	r3, [r4, #1218]	@ movhi
.L875:
	movs	r0, #0
	pop	{r4, pc}
.L881:
	.align	2
.L880:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	1
	.global	ftl_memset
	.thumb
	.thumb_func
	.type	ftl_memset, %function
ftl_memset:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	memset
	.fnend
	.size	ftl_memset, .-ftl_memset
	.align	1
	.global	BuildFlashLsbPageTable
	.thumb
	.thumb_func
	.type	BuildFlashLsbPageTable, %function
BuildFlashLsbPageTable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r1
	cbnz	r0, .L884
	ldr	r3, .L926
.L885:
	strh	r0, [r3, r0, lsl #1]	@ movhi
	adds	r0, r0, #1
	cmp	r0, #512
	bne	.L885
.L889:
	movs	r1, #255
	ldr	r0, .L926+4
	mov	r2, #2048
	uxth	r4, r4
	bl	ftl_memset
	movs	r3, #0
	ldr	r1, .L926
	ldr	r0, .L926+8
	b	.L886
.L884:
	cmp	r0, #1
	bne	.L887
	ldr	r5, .L926
	movs	r3, #0
.L888:
	uxth	r2, r3
	cmp	r2, #3
	ite	ls
	movls	r0, #0
	movhi	r0, #1
	bics	r1, r0, r3
	ite	ne
	movne	r1, #2
	moveq	r1, #3
	rsb	r1, r1, r2, lsl #1
	cmp	r0, #0
	it	ne
	movne	r2, r1
	strh	r2, [r5, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #512
	bne	.L888
	b	.L889
.L887:
	cmp	r0, #2
	bne	.L890
	ldr	r0, .L926
	movs	r3, #0
.L891:
	uxth	r2, r3
	lsls	r1, r2, #1
	subs	r1, r1, #1
	cmp	r2, #1
	it	hi
	movhi	r2, r1
	strh	r2, [r0, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #512
	bne	.L891
	b	.L889
.L890:
	cmp	r0, #3
	bne	.L892
	ldr	r5, .L926
	movs	r3, #0
.L893:
	uxth	r2, r3
	cmp	r2, #5
	ite	ls
	movls	r0, #0
	movhi	r0, #1
	bics	r1, r0, r3
	ite	ne
	movne	r1, #4
	moveq	r1, #5
	rsb	r1, r1, r2, lsl #1
	cmp	r0, #0
	it	ne
	movne	r2, r1
	strh	r2, [r5, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #512
	bne	.L893
	b	.L889
.L892:
	cmp	r0, #4
	mov	r3, #0
	bne	.L894
	ldr	r2, .L926+12
	strh	r3, [r2, #148]	@ movhi
	movs	r3, #1
	strh	r0, [r2, #156]	@ movhi
	strh	r3, [r2, #150]	@ movhi
	movs	r3, #2
	strh	r3, [r2, #152]	@ movhi
	movs	r3, #3
	strh	r3, [r2, #154]	@ movhi
	movs	r3, #5
	strh	r3, [r2, #158]	@ movhi
	movs	r3, #7
	strh	r3, [r2, #160]	@ movhi
	movs	r3, #8
	strh	r3, [r2, #162]!	@ movhi
.L895:
	tst	r3, #1
	ite	ne
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	adds	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L895
	b	.L889
.L894:
	cmp	r0, #5
	bne	.L896
	ldr	r2, .L926
.L897:
	strh	r3, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #16
	bne	.L897
	ldr	r2, .L926+16
.L898:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L898
	b	.L889
.L896:
	cmp	r0, #6
	bne	.L899
	ldr	r5, .L926
.L900:
	uxth	r2, r3
	cmp	r2, #5
	ite	ls
	movls	r0, #0
	movhi	r0, #1
	add	r1, r2, r2, lsl #1
	bics	r6, r0, r3
	ite	ne
	movne	r6, #10
	moveq	r6, #12
	subs	r1, r1, r6
	cmp	r0, #0
	it	ne
	movne	r2, r1
	strh	r2, [r5, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #512
	bne	.L900
	b	.L889
.L899:
	cmp	r0, #9
	bne	.L901
	ldr	r2, .L926+12
	movw	r1, #1021
	strh	r3, [r2, #148]	@ movhi
	movs	r3, #1
	strh	r3, [r2, #150]	@ movhi
	movs	r3, #2
	strh	r3, [r2, #152]!	@ movhi
	movs	r3, #3
.L902:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L902
	b	.L889
.L901:
	cmp	r0, #10
	bne	.L903
	ldr	r2, .L926
.L904:
	strh	r3, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #63
	bne	.L904
	ldr	r2, .L926+20
	movw	r1, #961
.L905:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L905
	b	.L889
.L903:
	cmp	r0, #11
	bne	.L906
	ldr	r2, .L926
	movs	r3, #0
.L907:
	strh	r3, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #8
	bne	.L907
	ldr	r1, .L926+24
.L908:
	tst	r3, #1
	ite	ne
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, lsl #1
	adds	r3, r3, #1
	strh	r2, [r1, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L908
	b	.L889
.L906:
	cmp	r0, #12
	bne	.L889
	ldr	r3, .L926+12
	movs	r2, #0
	strh	r2, [r3, #148]	@ movhi
	movs	r2, #1
	strh	r2, [r3, #150]	@ movhi
	movs	r2, #2
	strh	r2, [r3, #152]	@ movhi
	movs	r2, #3
	strh	r2, [r3, #154]!	@ movhi
	movs	r2, #4
.L909:
	subs	r1, r2, #1
	add	r1, r1, r2, lsr #1
	adds	r2, r2, #1
	strh	r1, [r3, #2]!	@ movhi
	uxth	r2, r2
	cmp	r2, #512
	bne	.L909
	b	.L889
.L886:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L925
	ldrh	r2, [r1, r3, lsl #1]
	adds	r3, r3, #1
	add	r5, r0, r2, lsl #1
	strh	r2, [r5, #1220]	@ movhi
	b	.L886
.L925:
	pop	{r4, r5, r6, pc}
.L927:
	.align	2
.L926:
	.word	.LANCHOR0+148
	.word	.LANCHOR2+1220
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+178
	.word	.LANCHOR0+272
	.word	.LANCHOR0+162
	.fnend
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	1
	.global	FlashDieInfoInit
	.thumb
	.thumb_func
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r7, #0
	ldr	r3, .L942
	ldr	r4, .L942+4
	ldr	r6, .L942+8
	ldrh	r0, [r3, #482]
	addw	fp, r4, #2068
	strb	r7, [r4, #2230]
	mov	r9, r4
	strb	r7, [r6, #3268]
	bl	FlashBlockAlignInit
	mov	r1, r7
	movs	r2, #8
	addw	r0, r4, #2232
	bl	ftl_memset
	mov	r1, r7
	movs	r2, #32
	addw	r0, r4, #1172
	bl	ftl_memset
	addw	r0, r4, #2100
	mov	r1, r7
	movs	r2, #128
	bl	ftl_memset
	ldr	r5, [r4, #44]
	add	r8, r5, #1
.L930:
	mov	r0, r8
	add	r1, fp, r7, lsl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r10, .L942+12
	cbnz	r0, .L929
	ldrb	r3, [r9, #2230]	@ zero_extendqisi2
	add	r2, r9, r3, lsl #2
	str	r0, [r2, #1172]
	adds	r2, r3, #1
	add	r3, r3, r9
	strb	r2, [r9, #2230]
	strb	r7, [r3, #2232]
.L929:
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L930
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	ldr	r2, .L942+4
	strb	r3, [r6, #3268]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L931
.L935:
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #2230]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #14]
	smulbb	r3, r2, r3
	strh	r3, [r6, #3270]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L931:
	ldr	r9, [r2, #4]
	movs	r7, #0
	mov	fp, r2
.L934:
	mov	r0, r8
	add	r1, r10, r7, lsl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cbnz	r0, .L932
	ldrb	r0, [r5, #13]	@ zero_extendqisi2
	ldrh	r3, [r5, #14]
	ldrb	r2, [fp, #2230]	@ zero_extendqisi2
	mul	r0, r9, r0
	and	r3, r3, #65280
	add	r1, fp, r2, lsl #2
	muls	r3, r0, r3
	str	r3, [r1, #1172]
	ldrb	r0, [r5, #23]	@ zero_extendqisi2
	cbz	r0, .L933
	lsls	r3, r3, #1
	str	r3, [r1, #1172]
.L933:
	adds	r3, r2, #1
	add	r2, r2, r4
	strb	r3, [r4, #2230]
	strb	r7, [r2, #2232]
.L932:
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L934
	b	.L935
.L943:
	.align	2
.L942:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2068
	.fnend
	.size	FlashDieInfoInit, .-FlashDieInfoInit
	.align	1
	.global	ftl_read_flash_info
	.thumb
	.thumb_func
	.type	ftl_read_flash_info, %function
ftl_read_flash_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r1, #0
	movs	r2, #11
	mov	r4, r0
	bl	ftl_memset
	ldr	r3, .L948
	movs	r5, #1
	ldr	r2, [r3, #44]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	ldr	r2, [r3, #4]
	smulbb	r2, r1, r2
	strh	r2, [r4, #4]	@ unaligned
	ldrb	r2, [r3, #2312]	@ zero_extendqisi2
	strb	r2, [r4, #7]
	ldr	r2, [r3, #2428]
	str	r2, [r4]	@ unaligned
	ldr	r2, [r3, #44]
	addw	r3, r3, #2232
	ldrb	r0, [r3, #-2]	@ zero_extendqisi2
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	movs	r1, #32
	strb	r1, [r4, #8]
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	strb	r2, [r4, #9]
	movs	r2, #0
	strb	r2, [r4, #10]
.L945:
	uxtb	r1, r2
	cmp	r1, r0
	bcs	.L947
	ldrb	r1, [r2, r3]	@ zero_extendqisi2
	adds	r2, r2, #1
	lsl	r6, r5, r1
	ldrb	r1, [r4, #10]	@ zero_extendqisi2
	orrs	r1, r1, r6
	strb	r1, [r4, #10]
	b	.L945
.L947:
	pop	{r4, r5, r6, pc}
.L949:
	.align	2
.L948:
	.word	.LANCHOR0
	.fnend
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	1
	.global	FtlMemInit
	.thumb
	.thumb_func
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r0, #1024
	ldr	r4, .L1054
	movs	r6, #0
	ldr	r5, .L1054+4
	movw	r3, #65535
	movs	r7, #12
	mov	r8, #36
	strh	r6, [r4, #3272]	@ movhi
	str	r6, [r4, #3276]
	str	r6, [r4, #604]
	str	r6, [r4, #508]
	str	r6, [r4, #512]
	str	r6, [r4, #496]
	str	r6, [r4, #484]
	str	r6, [r4, #480]
	str	r6, [r4, #488]
	str	r6, [r4, #492]
	str	r6, [r4, #476]
	str	r6, [r4, #516]
	str	r6, [r4, #520]
	str	r6, [r4, #236]
	str	r6, [r4, #528]
	str	r6, [r4, #532]
	str	r6, [r4, #3280]
	str	r6, [r4, #1132]
	str	r3, [r4, #3284]
	str	r6, [r4, #3288]
	str	r6, [r4, #1124]
	str	r6, [r4, #3292]
	strh	r3, [r4, #1174]	@ movhi
	strh	r3, [r4, #1176]	@ movhi
	strh	r3, [r4, #1178]	@ movhi
	strh	r3, [r4, #1180]	@ movhi
	movs	r3, #32
	strh	r6, [r4, #1128]	@ movhi
	strh	r3, [r4, #1120]	@ movhi
	movs	r3, #128
	strh	r6, [r4, #1182]	@ movhi
	strh	r3, [r4, #1122]	@ movhi
	strh	r6, [r4, #3296]	@ movhi
	strh	r6, [r4, #1218]	@ movhi
	ldrh	r1, [r5, #2394]
	bl	__aeabi_idiv
	ldrh	r3, [r5, #2320]
	str	r6, [r5, #2440]
	lsls	r3, r3, #2
	cmp	r0, r3
	str	r0, [r4, #3300]
	ldrh	r0, [r5, #2392]
	it	hi
	strhi	r3, [r4, #3300]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #1160]
	ldrh	r0, [r5, #2392]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #2320]
	mul	r6, r8, r6
	lsl	r9, r6, #3
	str	r0, [r4, #1168]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #3304]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3308]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #3312]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #228]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1152]
	ldr	r0, [r4, #3300]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #2398]
	ldrh	r3, [r5, #2320]
	lsls	r3, r3, #1
	adds	r3, r3, #1
	str	r3, [r4, #1156]
	str	r0, [r5, #2444]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3316]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3320]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3324]
	ldr	r0, [r4, #1156]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #1144]
	ldr	r0, [r4, #3300]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #3328]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3332]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3336]
	ldr	r0, [r4, #1156]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2320]
	ldrh	r6, [r5, #2400]
	muls	r6, r3, r6
	str	r0, [r4, #1140]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3340]
	lsls	r0, r6, #3
	bl	ftl_malloc
	ldrh	r3, [r5, #2400]
	str	r0, [r4, #3344]
	ldr	r0, [r4, #1156]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2400]
	str	r0, [r4, #1148]
	ldr	r0, [r4, #3300]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #3348]
	ldrh	r0, [r5, #2330]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3352]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3356]
	ldrh	r0, [r4, #3352]
	addw	r0, r0, #547
	lsrs	r0, r0, #9
	strh	r0, [r4, #3352]	@ movhi
	lsls	r0, r0, #9
	bl	ftl_malloc
	ldrh	r6, [r5, #2330]
	lsls	r6, r6, #1
	str	r0, [r4, #3360]
	adds	r0, r0, #32
	str	r0, [r4, #232]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3364]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r5, #2416]
	lsls	r6, r6, #1
	str	r0, [r4, #296]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3368]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3372]
	ldrh	r0, [r5, #2330]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #472]
	ldrh	r0, [r5, #2408]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r5, #2436]
	ldrh	r0, [r5, #2408]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3376]
	ldrh	r0, [r5, #2408]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3380]
	ldrh	r0, [r5, #2410]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5, #2410]
	movs	r1, #0
	lsls	r2, r2, #2
	str	r0, [r4, #3384]
	bl	ftl_memset
	ldrh	r6, [r5, #2424]
	lsls	r6, r6, #2
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3388]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, .L1054+8
	str	r0, [r4, #3392]
	ldr	r0, [r5, #2416]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3396]
	ldrh	r0, [r5, #2426]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2426]
	str	r0, [r4, #460]
	ldrh	r0, [r5, #2398]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2330]
	str	r0, [r4, #3400]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2386]
	adds	r3, r3, #31
	asrs	r3, r3, #5
	strh	r3, [r4, #3404]	@ movhi
	str	r0, [r4, #288]
	ldrh	r0, [r5, #2342]
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #3404]
	ldrh	r7, [r5, #2342]
	movs	r3, #1
	lsls	r2, r2, #2
	mov	r1, r2
	str	r0, [r5, #2480]
.L952:
	cmp	r3, r7
	bcs	.L1052
	ldr	r0, [r5, #2480]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r6, #4]!
	b	.L952
.L1052:
	ldr	r2, .L1054+12
	movs	r1, #0
.L954:
	cmp	r3, #8
	beq	.L1053
	add	r0, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r0, #28]
	b	.L954
.L1053:
	ldr	r2, [r4, #3368]
	ldr	r3, .L1054
	cbnz	r2, .L956
.L958:
	ldr	r1, .L1054+16
	ldr	r0, .L1054+20
	bl	printk
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1055:
	.align	2
.L1054:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2480
	.word	.LANCHOR0+2452
	.word	.LANCHOR3
	.word	.LC81
.L956:
	ldr	r2, [r3, #3372]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3388]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3396]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #460]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3400]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #288]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r5, #2480]
	cmp	r2, #0
	beq	.L958
	ldr	r3, [r3, #296]
	cmp	r3, #0
	beq	.L958
	ldr	r2, [r4, #1160]
	ldr	r3, .L1056
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #1168]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3304]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3312]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #228]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #1152]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3308]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3316]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3320]
	cmp	r2, #0
	beq	.L958
	ldr	r3, [r3, #3324]
	cmp	r3, #0
	beq	.L958
	ldr	r2, [r4, #1144]
	ldr	r3, .L1056
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3332]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3336]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #1140]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3340]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3344]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #1148]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #232]
	cmp	r2, #0
	beq	.L958
	ldr	r3, [r3, #3356]
	cmp	r3, #0
	beq	.L958
	ldr	r3, .L1056+4
	ldr	r3, [r3, #2436]
	cmp	r3, #0
	beq	.L958
	ldr	r3, .L1056
	ldr	r2, [r3, #3376]
	cmp	r2, #0
	beq	.L958
	ldr	r2, [r3, #3380]
	cmp	r2, #0
	beq	.L958
	ldr	r3, [r3, #3384]
	cmp	r3, #0
	beq	.L958
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1057:
	.align	2
.L1056:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	1
	.global	FtlBbt2Bitmap
	.thumb
	.thumb_func
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldr	r3, .L1063
	mov	r0, r1
	mov	r5, r1
	movs	r1, #0
	movs	r7, #1
	ldrh	r2, [r3, #3404]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldr	r1, .L1063+4
	subs	r2, r4, #2
	addw	r4, r4, #1022
	movw	lr, #65535
.L1060:
	ldrh	r3, [r2, #2]!
	cmp	r3, lr
	beq	.L1058
	lsrs	r0, r3, #5
	and	r3, r3, #31
	lsl	r3, r7, r3
	cmp	r2, r4
	ldr	r6, [r5, r0, lsl #2]
	orr	r3, r3, r6
	str	r3, [r5, r0, lsl #2]
	ldrh	r3, [r1, #2458]
	add	r3, r3, #1
	strh	r3, [r1, #2458]	@ movhi
	bne	.L1060
.L1058:
	pop	{r3, r4, r5, r6, r7, pc}
.L1064:
	.align	2
.L1063:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	1
	.global	FtlBbtMemInit
	.thumb
	.thumb_func
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L1066
	movw	r3, #65535
	movs	r1, #255
	movs	r2, #16
	add	r0, r0, #2464
	strh	r3, [r0, #-12]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	ftl_memset
.L1067:
	.align	2
.L1066:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	1
	.global	FtlFreeSysBlkQueueInit
	.thumb
	.thumb_func
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1069
	mov	r2, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	strh	r0, [r3, #2512]	@ movhi
	addw	r0, r3, #2520
	mov	r1, r4
	strh	r4, [r3, #2514]	@ movhi
	strh	r4, [r3, #2516]	@ movhi
	strh	r4, [r3, #2518]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L1070:
	.align	2
.L1069:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	1
	.global	ftl_free_no_use_map_blk
	.thumb
	.thumb_func
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r1, #0
	ldrh	r2, [r0, #10]
	mov	r4, r0
	ldr	r6, [r0, #20]
	ldr	r7, [r0, #12]
	ldr	r5, [r0, #24]
	lsls	r2, r2, #1
	mov	r0, r6
	bl	ftl_memset
	movs	r1, #0
.L1072:
	ldrh	r2, [r4, #6]
	uxth	r3, r1
	cmp	r2, r3
	bls	.L1091
	ldr	r2, [r5, r3, lsl #2]
	movs	r0, #0
	ubfx	r2, r2, #10, #16
.L1073:
	ldrh	lr, [r4, #10]
	uxth	r3, r0
	cmp	lr, r3
	bls	.L1092
	ldrh	lr, [r7, r3, lsl #1]
	cmp	lr, r2
	bne	.L1074
	cbz	r2, .L1074
	ldrh	lr, [r6, r3, lsl #1]
	add	lr, lr, #1
	strh	lr, [r6, r3, lsl #1]	@ movhi
.L1074:
	adds	r0, r0, #1
	b	.L1073
.L1092:
	adds	r1, r1, #1
	b	.L1072
.L1091:
	ldr	r3, .L1095
	mov	r8, #0
	mov	r10, r8
	ldrh	r2, [r3, #2390]
	ldrh	r3, [r4]
	strh	r2, [r6, r3, lsl #1]	@ movhi
	mov	r2, r8
	ldrh	r9, [r6]
.L1077:
	ldrh	r3, [r4, #10]
	uxth	r5, r8
	cmp	r3, r5
	bls	.L1093
	ldrh	r3, [r6, r5, lsl #1]
	lsls	r1, r5, #1
	cmp	r9, r3
	bls	.L1078
	ldrh	r0, [r7, r5, lsl #1]
	add	fp, r7, r1
	cbnz	r0, .L1079
	b	.L1080
.L1078:
	cbnz	r3, .L1080
	ldrh	r0, [r7, r1]
	add	fp, r7, r1
	cbz	r0, .L1080
	mov	r5, r2
	b	.L1082
.L1079:
	cbnz	r3, .L1094
	mov	r9, r3
.L1082:
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	r10, [fp]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r2, r5
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1080:
	add	r8, r8, #1
	b	.L1077
.L1094:
	mov	r2, r5
	mov	r9, r3
	b	.L1080
.L1093:
	mov	r0, r2
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1096:
	.align	2
.L1095:
	.word	.LANCHOR0
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	1
	.global	FtlL2PDataInit
	.thumb
	.thumb_func
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	movs	r1, #0
	ldr	r4, .L1101
	movs	r7, #0
	ldr	r5, .L1101+4
	mov	r6, r7
	ldr	r2, [r4, #2416]
	mov	r9, r4
	ldr	r0, [r5, #3372]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #2398]
	ldrh	r2, [r4, #2426]
	movs	r1, #255
	ldr	r0, [r5, #3400]
	muls	r2, r3, r2
	bl	ftl_memset
	mov	r3, r5
	movs	r5, #12
	mov	r2, r4
	movw	lr, #65535
.L1098:
	ldrh	ip, [r2, #2426]
	adds	r0, r7, #1
	uxth	r7, r7
	ldr	r1, .L1101
	cmp	ip, r7
	bls	.L1100
	mul	r1, r5, r7
	ldr	ip, [r3, #460]
	add	r8, ip, r1
	str	r6, [r8, #4]
	strh	lr, [ip, r1]	@ movhi
	ldr	r4, [r3, #460]
	add	ip, r4, r1
	ldrh	r1, [r9, #2398]
	muls	r1, r7, r1
	ldr	r7, [r3, #3400]
	bic	r1, r1, #3
	add	r1, r1, r7
	mov	r7, r0
	str	r1, [ip, #8]
	b	.L1098
.L1100:
	movw	r2, #65535
	strh	r2, [r3, #3410]	@ movhi
	strh	r2, [r3, #3408]	@ movhi
	ldr	r2, [r1, #2416]
	strh	r2, [r3, #3418]	@ movhi
	movw	r2, #61634
	strh	r2, [r3, #3412]	@ movhi
	ldrh	r2, [r3, #3452]
	strh	r2, [r3, #3416]	@ movhi
	ldrh	r2, [r1, #2424]
	strh	r2, [r3, #3414]	@ movhi
	ldr	r2, [r3, #3368]
	str	r2, [r3, #3420]
	ldr	r2, [r3, #3396]
	str	r2, [r3, #3424]
	ldr	r2, [r3, #3372]
	str	r2, [r3, #3428]
	ldr	r2, [r3, #3388]
	str	r2, [r3, #3432]
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1102:
	.align	2
.L1101:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	1
	.global	FtlVariablesInit
	.thumb
	.thumb_func
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r4, #0
	ldr	r6, .L1104
	movw	r3, #65535
	ldr	r5, .L1104+4
	mov	r1, r4
	ldrh	r2, [r6, #2408]
	ldr	r0, [r6, #2436]
	strh	r3, [r5, #3460]	@ movhi
	mov	r3, #-1
	lsls	r2, r2, #1
	str	r3, [r5, #3472]
	str	r4, [r5, #3456]
	str	r4, [r5, #3464]
	str	r4, [r5, #3468]
	str	r4, [r6, #2244]
	strh	r4, [r6, #2434]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r6, #2330]
	mov	r1, r4
	ldr	r0, [r5, #232]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6, #2330]
	mov	r1, r4
	ldr	r0, [r5, #3356]
	lsls	r2, r2, #1
	bl	ftl_memset
	mov	r1, r4
	add	r0, r5, #240
	movs	r2, #48
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	add	r0, r5, #608
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1105:
	.align	2
.L1104:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	1
	.global	SupperBlkListInit
	.thumb
	.thumb_func
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r2, #6
	ldr	r6, .L1117
	movs	r5, #0
	ldr	r4, .L1117+4
	.pad #20
	sub	sp, sp, #20
	movs	r1, #0
	mov	r9, r5
	ldrh	r3, [r6, #2330]
	mov	r10, r5
	ldr	r0, [r4, #288]
	mov	r8, r6
	muls	r2, r3, r2
	bl	ftl_memset
	str	r5, [r4, #308]
	str	r5, [r4, #292]
	str	r5, [r4, #300]
	strh	r5, [r4, #304]	@ movhi
	strh	r5, [r4, #312]	@ movhi
	strh	r5, [r4, #3272]	@ movhi
.L1107:
	uxth	fp, r5
	ldrh	r2, [r8, #2328]
	sxth	r7, fp
	cmp	r7, r2
	bge	.L1114
	ldr	r3, .L1117
	movs	r2, #0
	mov	r6, r2
	ldrh	ip, [r3, #2320]
	ldrh	r3, [r3, #2388]
	str	r3, [sp, #4]
.L1115:
	sxth	r1, r2
	cmp	r1, ip
	bge	.L1116
	add	r1, r1, r8
	str	r2, [sp, #12]
	str	ip, [sp, #8]
	ldrb	r0, [r1, #2348]	@ zero_extendqisi2
	mov	r1, fp
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	ip, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L1108
	ldr	r3, [sp, #4]
	add	r6, r6, r3
	uxth	r6, r6
.L1108:
	adds	r2, r2, #1
	b	.L1115
.L1116:
	cbz	r6, .L1110
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1111
.L1110:
	ldr	r1, [r4, #296]
	sxth	r2, fp
	movw	r0, #65535
	strh	r0, [r1, r2, lsl #1]	@ movhi
.L1111:
	add	r0, r7, r7, lsl #1
	ldr	r1, [r4, #288]
	ldr	r2, .L1117+4
	add	r1, r1, r0, lsl #1
	strh	r6, [r1, #4]	@ movhi
	ldrh	r1, [r4, #316]
	cmp	r7, r1
	beq	.L1112
	ldrh	r1, [r2, #364]
	cmp	r7, r1
	beq	.L1112
	ldrh	r1, [r2, #412]
	cmp	r7, r1
	beq	.L1112
	ldr	r2, [r2, #296]
	ldrh	r2, [r2, r7, lsl #1]
	cbnz	r2, .L1113
	add	r9, r9, #1
	mov	r0, fp
	uxth	r9, r9
	bl	INSERT_FREE_LIST
	b	.L1112
.L1113:
	add	r10, r10, #1
	mov	r0, fp
	uxth	r10, r10
	bl	INSERT_DATA_LIST
.L1112:
	adds	r5, r5, #1
	b	.L1107
.L1114:
	movs	r0, #0
	strh	r10, [r4, #304]	@ movhi
	strh	r9, [r4, #312]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1118:
	.align	2
.L1117:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	1
	.global	FtlGcPageVarInit
	.thumb
	.thumb_func
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r1, #255
	ldr	r5, .L1120
	movs	r3, #0
	ldr	r4, .L1120+4
	ldrh	r2, [r5, #2392]
	ldr	r0, [r4, #1160]
	strh	r3, [r4, #1164]	@ movhi
	lsls	r2, r2, #1
	strh	r3, [r4, #1172]	@ movhi
	bl	ftl_memset
	ldrh	r3, [r5, #2392]
	movs	r2, #12
	ldr	r0, [r4, #1168]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r3, r4, r5, lr}
	b	FtlGcBufInit
.L1121:
	.align	2
.L1120:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	1
	.global	ftl_memcpy
	.thumb
	.thumb_func
	.type	ftl_memcpy, %function
ftl_memcpy:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	memcpy
	.fnend
	.size	ftl_memcpy, .-ftl_memcpy
	.align	1
	.global	FlashReadIdbData
	.thumb
	.thumb_func
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	mov	r2, #2048
	ldr	r1, .L1124
	bl	ftl_memcpy
	movs	r0, #0
	pop	{r3, pc}
.L1125:
	.align	2
.L1124:
	.word	.LANCHOR2+3476
	.fnend
	.size	FlashReadIdbData, .-FlashReadIdbData
	.align	1
	.global	FlashLoadPhyInfoInRam
	.thumb
	.thumb_func
	.type	FlashLoadPhyInfoInRam, %function
FlashLoadPhyInfoInRam:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r4, #0
	ldr	r5, .L1138
.L1130:
	mov	r0, r5
	ldr	r1, .L1138+4
	ldrb	r2, [r5, #-1]	@ zero_extendqisi2
	bl	FlashMemCmp8
	mov	r6, r0
	cbnz	r0, .L1127
	ldr	r5, .L1138+8
	lsls	r4, r4, #5
	add	r2, r5, #508
	adds	r7, r2, r4
	beq	.L1133
	add	r4, r4, r5
	mov	r3, r0
	addw	r1, r5, #3260
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	b	.L1132
.L1127:
	adds	r4, r4, #1
	adds	r5, r5, #32
	cmp	r4, #86
	bne	.L1130
	b	.L1133
.L1137:
	adds	r3, r3, #1
	cmp	r3, #4
	beq	.L1131
.L1132:
	lsls	r0, r3, #5
	ldrb	r0, [r0, r1]	@ zero_extendqisi2
	cmp	r0, r2
	bne	.L1137
.L1131:
	ldr	r1, .L1138+12
	movs	r2, #32
	ldr	r0, .L1138+16
	add	r1, r1, r3, lsl #5
	bl	ftl_memcpy
	mov	r1, r7
	movs	r2, #32
	ldr	r0, .L1138+20
	bl	ftl_memcpy
	ldrh	r0, [r5, #482]
	bl	FlashBlockAlignInit
	b	.L1128
.L1133:
	mov	r6, #-1
.L1128:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L1139:
	.align	2
.L1138:
	.word	.LANCHOR1+509
	.word	.LANCHOR0+2068
	.word	.LANCHOR1
	.word	.LANCHOR1+3260
	.word	.LANCHOR0+48
	.word	.LANCHOR1+472
	.fnend
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
	.align	1
	.global	NandcCopy1KB
	.thumb
	.thumb_func
	.type	NandcCopy1KB, %function
NandcCopy1KB:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #1
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r2
	add	r2, r0, #4096
	ldr	r6, [sp, #16]
	add	r5, r0, #512
	add	r2, r2, r4, lsl #9
	bne	.L1141
	cbz	r3, .L1142
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1142:
	cbz	r6, .L1140
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	lsrs	r4, r4, #1
	ldrb	r2, [r6, #1]	@ zero_extendqisi2
	add	r4, r4, r4, lsl #1
	lsls	r3, r3, #16
	orr	r2, r3, r2, lsl #8
	ldrb	r3, [r6]	@ zero_extendqisi2
	lsls	r4, r4, #2
	orrs	r3, r3, r2
	ldrb	r2, [r6, #3]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #24
	str	r3, [r5, r4, lsl #2]
	pop	{r4, r5, r6, pc}
.L1141:
	cbz	r3, .L1145
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1145:
	cbz	r6, .L1140
	lsrs	r4, r4, #1
	add	r4, r4, r4, lsl #1
	lsls	r4, r4, #2
	ldr	r3, [r5, r4, lsl #2]
	lsrs	r2, r3, #8
	strb	r3, [r6]
	strb	r2, [r6, #1]
	lsrs	r2, r3, #16
	lsrs	r3, r3, #24
	strb	r2, [r6, #2]
	strb	r3, [r6, #3]
.L1140:
	pop	{r4, r5, r6, pc}
	.fnend
	.size	NandcCopy1KB, .-NandcCopy1KB
	.align	1
	.global	ftl_memcpy32
	.thumb
	.thumb_func
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movs	r3, #0
	push	{r4, lr}
	.save {r4, lr}
.L1157:
	cmp	r3, r2
	beq	.L1159
	ldr	r4, [r1, r3, lsl #2]
	str	r4, [r0, r3, lsl #2]
	adds	r3, r3, #1
	b	.L1157
.L1159:
	pop	{r4, pc}
	.fnend
	.size	ftl_memcpy32, .-ftl_memcpy32
	.align	1
	.global	ftl_memcmp
	.thumb
	.thumb_func
	.type	ftl_memcmp, %function
ftl_memcmp:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	memcmp
	.fnend
	.size	ftl_memcmp, .-ftl_memcmp
	.align	1
	.global	timer_get_time
	.thumb
	.thumb_func
	.type	timer_get_time, %function
timer_get_time:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1162
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1163:
	.align	2
.L1162:
	.word	jiffies
	.fnend
	.size	timer_get_time, .-timer_get_time
	.align	1
	.global	FlashSramLoadStore
	.thumb
	.thumb_func
	.type	FlashSramLoadStore, %function
FlashSramLoadStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L1167
	ldr	r4, [r4, #1180]
	add	r4, r4, #4096
	add	r1, r1, r4
	cbz	r2, .L1166
	mov	r0, r1
	mov	r1, r5
.L1166:
	mov	r2, r3
	pop	{r4, r5, lr}
	b	ftl_memcpy
.L1168:
	.align	2
.L1167:
	.word	.LANCHOR4
	.fnend
	.size	FlashSramLoadStore, .-FlashSramLoadStore
	.align	1
	.global	FlashCs123Init
	.thumb
	.thumb_func
	.type	FlashCs123Init, %function
FlashCs123Init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	FlashCs123Init, .-FlashCs123Init
	.align	1
	.global	rk_nand_suspend
	.thumb
	.thumb_func
	.type	rk_nand_suspend, %function
rk_nand_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_flash_suspend
	.fnend
	.size	rk_nand_suspend, .-rk_nand_suspend
	.align	1
	.global	rk_nand_resume
	.thumb
	.thumb_func
	.type	rk_nand_resume, %function
rk_nand_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_flash_resume
	.fnend
	.size	rk_nand_resume, .-rk_nand_resume
	.align	1
	.global	rk_ftl_get_capacity
	.thumb
	.thumb_func
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1173
	ldr	r0, [r3, #2428]
	bx	lr
.L1174:
	.align	2
.L1173:
	.word	.LANCHOR0
	.fnend
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	1
	.global	rk_nandc_get_irq_status
	.thumb
	.thumb_func
	.type	rk_nandc_get_irq_status, %function
rk_nandc_get_irq_status:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, [r0, #372]
	bx	lr
	.fnend
	.size	rk_nandc_get_irq_status, .-rk_nandc_get_irq_status
	.align	1
	.global	rknand_proc_ftlread
	.thumb
	.thumb_func
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_proc_ftl_read
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	1
	.global	ReadFlashInfo
	.thumb
	.thumb_func
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_read_flash_info
	.fnend
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	1
	.global	rknand_print_hex
	.thumb
	.thumb_func
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	movs	r5, #0
	mov	r9, r0
	mov	r6, r1
	mov	r7, r2
	mov	r8, r3
	mov	r4, r5
.L1179:
	cmp	r4, r8
	beq	.L1187
	cbnz	r5, .L1180
	ldr	r0, .L1188
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1180:
	cmp	r7, #4
	ldr	r0, .L1188+4
	bne	.L1181
	ldr	r1, [r6, r4, lsl #2]
	b	.L1186
.L1181:
	cmp	r7, #2
	ite	eq
	ldrsheq	r1, [r6, r4, lsl #1]
	ldrbne	r1, [r6, r4]	@ zero_extendqisi2
.L1186:
	adds	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1184
	ldr	r0, .L1188+8
	movs	r5, #0
	ldr	r1, .L1188+12
	bl	printk
.L1184:
	adds	r4, r4, #1
	b	.L1179
.L1187:
	ldr	r0, .L1188+8
	ldr	r1, .L1188+12
	pop	{r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1189:
	.align	2
.L1188:
	.word	.LC82
	.word	.LC83
	.word	.LC76
	.word	.LC84
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	1
	.global	HynixGetReadRetryDefault
	.thumb
	.thumb_func
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	cmp	r0, #2
	ldr	r7, .L1299
	mov	r3, #172
	mov	r1, #173
	mov	r2, #174
	.pad #44
	sub	sp, sp, #44
	mov	r4, r0
	strb	r3, [r7, #1214]
	mov	r3, #175
	strb	r0, [r7, #1210]
	strb	r1, [r7, #1215]
	strb	r2, [r7, #1216]
	strb	r3, [r7, #1217]
	bne	.L1191
	movs	r3, #167
	strb	r3, [r7, #1214]
	ldr	r3, .L1299+4
	movs	r2, #247
	movs	r5, #7
	strb	r2, [r3, #3405]
	b	.L1252
.L1191:
	cmp	r0, #3
	bne	.L1193
	movs	r3, #176
	strb	r3, [r7, #1214]
	movs	r3, #177
	strb	r3, [r7, #1215]
	movs	r3, #178
	strb	r3, [r7, #1216]
	movs	r3, #179
	strb	r3, [r7, #1217]
	movs	r3, #180
	strb	r3, [r7, #1218]
	movs	r3, #181
	strb	r3, [r7, #1219]
	movs	r3, #182
	strb	r3, [r7, #1220]
	movs	r3, #183
	b	.L1293
.L1193:
	cmp	r0, #4
	bne	.L1194
	movs	r0, #204
	strb	r1, [r7, #1219]
	strb	r0, [r7, #1214]
	movs	r0, #191
	strb	r2, [r7, #1220]
	strb	r0, [r7, #1215]
	movs	r0, #170
	strb	r0, [r7, #1216]
	movs	r0, #171
	strb	r0, [r7, #1217]
	movs	r0, #205
	strb	r0, [r7, #1218]
.L1293:
	movs	r5, #8
	strb	r3, [r7, #1221]
	mov	r6, r5
	b	.L1192
.L1194:
	cmp	r0, #5
	bne	.L1195
	movs	r3, #56
	movs	r5, #8
	strb	r3, [r7, #1214]
	movs	r3, #57
	strb	r3, [r7, #1215]
	movs	r3, #58
	strb	r3, [r7, #1216]
	movs	r3, #59
	strb	r3, [r7, #1217]
	b	.L1252
.L1195:
	cmp	r0, #6
	bne	.L1196
	movs	r3, #14
	movs	r5, #12
	strb	r3, [r7, #1214]
	movs	r3, #15
	strb	r3, [r7, #1215]
	movs	r3, #16
	strb	r3, [r7, #1216]
	movs	r3, #17
	strb	r3, [r7, #1217]
	b	.L1252
.L1196:
	cmp	r0, #7
	bne	.L1197
	movs	r3, #176
	movs	r5, #12
	strb	r3, [r7, #1214]
	movs	r6, #10
	movs	r3, #177
	strb	r3, [r7, #1215]
	movs	r3, #178
	strb	r3, [r7, #1216]
	movs	r3, #179
	strb	r3, [r7, #1217]
	movs	r3, #180
	strb	r3, [r7, #1218]
	movs	r3, #181
	strb	r3, [r7, #1219]
	movs	r3, #182
	strb	r3, [r7, #1220]
	movs	r3, #183
	strb	r3, [r7, #1221]
	movs	r3, #212
	strb	r3, [r7, #1222]
	movs	r3, #213
	strb	r3, [r7, #1223]
	b	.L1192
.L1197:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1252
	movs	r3, #6
	strb	r5, [r7, #1215]
	strb	r3, [r7, #1214]
	movs	r5, #50
	movs	r3, #9
	strb	r0, [r7, #1216]
	strb	r3, [r7, #1217]
	movs	r6, #5
	movs	r3, #10
	strb	r3, [r7, #1218]
	b	.L1192
.L1252:
	movs	r6, #4
.L1192:
	subs	r3, r4, #1
	cmp	r3, #1
	bhi	.L1290
	ldr	r1, .L1299+8
	mov	r10, #0
.L1198:
	ldrb	r3, [r7, #2230]	@ zero_extendqisi2
	uxtb	r8, r10
	ldr	r2, .L1299
	cmp	r3, r8
	bls	.L1205
	add	r8, r8, r2
	addw	fp, r2, #1213
	ldrb	r3, [r8, #2232]	@ zero_extendqisi2
	add	r4, r2, r3, lsl #6
	add	r3, r2, r3, lsl #3
	addw	r4, r4, #1230
	movs	r2, #55
	ldrb	r8, [r3, #16]	@ zero_extendqisi2
	ldr	r0, [r3, #12]
	mov	r9, r4
	mov	r3, r4
	add	r8, r0, r8, lsl #8
	addw	ip, r8, #2056
.L1200:
	str	r2, [ip]
	ldrb	r0, [fp, #1]!	@ zero_extendqisi2
	str	r1, [sp, #16]
	str	r3, [sp, #12]
	str	r0, [r8, #2052]
	movs	r0, #80
	str	r2, [sp, #8]
	str	ip, [sp, #4]
	bl	NandcDelayns
	ldr	r0, [r8, #2048]
	ldr	r3, [sp, #12]
	ldr	ip, [sp, #4]
	ldr	r2, [sp, #8]
	strb	r0, [r3], #1
	subs	r0, r3, r4
	ldr	r1, [sp, #16]
	uxtb	r0, r0
	cmp	r0, r6
	bcc	.L1200
	movs	r2, #0
.L1201:
	movs	r3, #1
	add	lr, r1, r2
.L1202:
	ldrb	ip, [lr, r3, lsl #2]	@ zero_extendqisi2
	ldrb	r0, [r9]	@ zero_extendqisi2
	add	r0, r0, ip
	strb	r0, [r9, r3, lsl #3]
	adds	r3, r3, #1
	cmp	r3, #7
	bne	.L1202
	adds	r2, r2, #1
	add	r9, r9, #1
	cmp	r2, #4
	bne	.L1201
	movs	r3, #0
	add	r10, r10, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1198
.L1290:
	subs	r3, r4, #3
	cmp	r3, #5
	bhi	.L1205
	smulbb	r2, r6, r5
	lsls	r3, r2, #4
	asrs	r2, r2, #1
	str	r3, [sp, #36]
	lsls	r3, r2, #1
	str	r3, [sp, #8]
	movs	r3, #0
.L1297:
	str	r3, [sp, #20]
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r3, .L1299
	ldr	r2, [sp, #16]
	ldrb	r3, [r3, #2230]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1250
.L1205:
	ldr	r3, .L1299
	strb	r6, [r3, #1211]
	strb	r5, [r3, #1212]
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1250:
	ldr	r2, [sp, #16]
	ldr	r3, .L1299
	add	r3, r3, r2
	ldrb	fp, [r3, #2232]	@ zero_extendqisi2
	ldr	r3, .L1299
	add	r3, r3, fp, lsl #3
	mov	r0, fp
	ldr	r10, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	lsls	r7, r3, #8
	str	r3, [sp, #4]
	add	r8, r10, r7
	movs	r3, #255
	str	r3, [r8, #2056]
	bl	NandcWaitFlashReady
	cmp	r4, #7
	bne	.L1207
	ldr	r2, .L1299+12
	movs	r3, #160
	mla	ip, r3, fp, r2
	add	r3, ip, #28
	b	.L1294
.L1207:
	cmp	r4, #8
	beq	.L1209
	ldr	r2, .L1299+12
	add	ip, r2, fp, lsl #6
	add	r3, ip, #20
.L1294:
	add	r7, r7, r10
	cmp	r4, #4
	str	r3, [sp, #24]
	mov	r3, #54
	str	r3, [r7, #2056]
	bne	.L1210
	movs	r3, #255
	str	r3, [r7, #2052]
	movs	r3, #64
	str	r3, [r7, #2048]
	movs	r3, #204
	b	.L1295
.L1210:
	subs	r3, r4, #5
	cmp	r3, #1
	bhi	.L1212
	ldr	r3, .L1299
	ldrb	r2, [r3, #1214]	@ zero_extendqisi2
	ldr	r3, [sp, #4]
	add	r3, r10, r3, lsl #8
	str	r2, [r3, #2052]
	movs	r2, #82
	str	r2, [r3, #2048]
	b	.L1211
.L1212:
	cmp	r4, #7
	bne	.L1211
	movs	r3, #174
	str	r3, [r7, #2052]
	movs	r3, #0
	str	r3, [r7, #2048]
	movs	r3, #176
.L1295:
	str	r3, [r7, #2052]
	movs	r3, #77
	str	r3, [r7, #2048]
.L1211:
	ldr	r3, [sp, #4]
	cmp	r4, #6
	mov	r2, #22
	add	r3, r10, r3, lsl #8
	str	r2, [r3, #2056]
	mov	r2, #23
	str	r2, [r3, #2056]
	mov	r2, #4
	str	r2, [r3, #2056]
	mov	r2, #25
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	it	eq
	moveq	r2, #31
	str	r2, [r3, #2052]
	movs	r2, #2
	str	r2, [r3, #2052]
	movs	r2, #0
	str	r2, [r3, #2052]
.L1251:
	ldr	r3, [sp, #4]
	movs	r2, #48
	mov	r0, fp
	add	r3, r10, r3, lsl #8
	str	r2, [r3, #2056]
	bl	NandcWaitFlashReady
	subs	r3, r4, #5
	cmp	r3, #1
	str	r3, [sp, #28]
	bls	.L1255
	cmp	r4, #8
	beq	.L1255
	cmp	r4, #7
	ite	ne
	movne	r2, #2
	moveq	r2, #32
	b	.L1215
.L1300:
	.align	2
.L1299:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3388
	.word	.LANCHOR0+1210
.L1255:
	movs	r2, #16
.L1215:
	ldr	r3, .L1301
	ldr	r1, [sp, #4]
	ldr	r3, [r3, #1184]
	add	r0, r10, r1, lsl #8
	mov	r7, r3
.L1216:
	ldr	r1, [r0, #2048]
	strb	r1, [r7], #1
	subs	r1, r7, r3
	uxtb	r1, r1
	cmp	r1, r2
	bcc	.L1216
	cmp	r4, #8
	bne	.L1217
	movs	r2, #0
.L1219:
	ldrb	r7, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r7, #50
	beq	.L1218
	add	r7, r3, r2, lsl #2
	ldrb	r7, [r7, #1]	@ zero_extendqisi2
	cmp	r7, #5
	beq	.L1218
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1219
	b	.L1220
.L1218:
	cmp	r1, #7
	bne	.L1221
.L1220:
	ldr	r0, .L1301+4
	movs	r1, #0
	bl	printk
.L1222:
	b	.L1222
.L1217:
	cmp	r4, #7
	bne	.L1223
	movs	r2, #0
.L1225:
	ldrb	r7, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r7, #12
	beq	.L1224
	add	r7, r3, r2, lsl #2
	ldrb	r7, [r7, #1]	@ zero_extendqisi2
	cmp	r7, #10
	beq	.L1224
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1225
	b	.L1226
.L1224:
	cmp	r1, #7
	bne	.L1221
.L1226:
	ldr	r0, .L1301+4
	movs	r1, #0
	bl	printk
.L1227:
	b	.L1227
.L1223:
	cmp	r4, #6
	bne	.L1221
	subs	r2, r3, #1
	adds	r3, r3, #7
.L1228:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1221
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1221
	cmp	r2, r3
	bne	.L1228
	ldr	r0, .L1301+4
	movs	r1, #0
	bl	printk
.L1230:
	b	.L1230
.L1221:
	ldr	r1, .L1301
	ldr	r7, [sp, #36]
	ldr	r2, [r1, #1184]
	adds	r7, r2, r7
	mov	r3, r2
.L1231:
	cmp	r3, r7
	beq	.L1298
	ldr	lr, [r0, #2048]
	strb	lr, [r3], #1
	b	.L1231
.L1298:
	ldr	r3, [r1, #1184]
	movs	r0, #8
	ldr	r1, [sp, #8]
	adds	r1, r3, r1
	str	r1, [sp, #32]
.L1234:
	ldr	r7, [sp, #8]
	add	lr, r1, r7
.L1233:
	ldrh	r7, [r1]
	mvns	r7, r7
	strh	r7, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L1233
	ldr	r7, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, r7
	bne	.L1234
	str	r3, [sp, #12]
.L1235:
	movs	r7, #0
	mov	r0, r7
.L1238:
	movs	r3, #1
	mov	lr, #0
	mov	r8, #16
	lsl	r1, r3, r0
	mov	ip, lr
.L1236:
	ldr	r3, [sp, #12]
	ldrh	r9, [r3, lr]
	ldr	r3, [sp, #8]
	and	r9, r9, r1
	cmp	r9, r1
	it	eq
	addeq	ip, ip, #1
	subs	r8, r8, #1
	add	lr, lr, r3
	bne	.L1236
	cmp	ip, #8
	add	r0, r0, #1
	itt	hi
	orrhi	r7, r7, r1
	uxthhi	r7, r7
	cmp	r0, #16
	bne	.L1238
	ldr	r3, [sp, #12]
	strh	r7, [r3], #2	@ movhi
	str	r3, [sp, #12]
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #32]
	cmp	r1, r3
	bne	.L1235
	ldr	r3, .L1301
	ldr	r1, [r3, #1184]
	movs	r3, #0
	subs	r0, r1, #4
	add	r7, r1, #28
.L1240:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	it	eq
	addeq	r3, r3, #1
	cmp	r0, r7
	bne	.L1240
	cmp	r3, #7
	ble	.L1241
	ldr	r0, .L1301+8
	movs	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1301+4
	movs	r1, #0
	bl	printk
.L1242:
	b	.L1242
.L1241:
	cmp	r4, #6
	beq	.L1257
	cmp	r4, #7
	beq	.L1258
	cmp	r4, #8
	ite	ne
	movne	r0, #8
	moveq	r0, #5
	b	.L1243
.L1257:
	movs	r0, #4
	b	.L1243
.L1258:
	movs	r0, #10
.L1243:
	add	r9, r6, #-1
	ldr	r1, [sp, #24]
	movs	r7, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L1244:
	mov	ip, r1
	mov	r3, r2
.L1245:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [ip], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L1245
	adds	r7, r7, #1
	add	r2, r2, r9
	cmp	r7, r5
	add	r1, r1, r0
	blt	.L1244
	ldr	r3, [sp, #4]
	mov	r0, fp
	add	r7, r10, r3, lsl #8
	movs	r3, #255
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #28]
	cmp	r3, #1
	bhi	.L1247
	movs	r3, #54
	str	r3, [r7, #2056]
	ldr	r3, .L1301+12
	mov	r1, #-1
	ldr	r0, [sp, #16]
	ldrb	r3, [r3, #1214]	@ zero_extendqisi2
	str	r3, [r7, #2052]
	movs	r3, #0
	str	r3, [r7, #2048]
	movs	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L1248
.L1247:
	cmp	r4, #8
	ite	eq
	moveq	r3, #190
	movne	r3, #56
	str	r3, [r7, #2056]
.L1248:
	mov	r0, fp
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	b	.L1297
.L1209:
	movs	r3, #120
	movs	r2, #23
	str	r3, [r8, #2056]
	movs	r1, #25
	movs	r3, #0
	str	r3, [r8, #2052]
	str	r3, [r8, #2052]
	str	r3, [r8, #2052]
	str	r2, [r8, #2056]
	movs	r2, #4
	str	r2, [r8, #2056]
	str	r1, [r8, #2056]
	movs	r1, #218
	str	r1, [r8, #2056]
	movs	r1, #21
	str	r3, [r8, #2056]
	str	r3, [r8, #2052]
	str	r3, [r8, #2052]
	str	r1, [r8, #2052]
	str	r2, [r8, #2052]
	str	r3, [r8, #2052]
	ldr	r3, .L1301+16
	str	r3, [sp, #24]
	b	.L1251
.L1302:
	.align	2
.L1301:
	.word	.LANCHOR4
	.word	.LC85
	.word	.LC86
	.word	.LANCHOR0
	.word	.LANCHOR0+1238
	.fnend
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	1
	.global	FlashGetReadRetryDefault
	.thumb
	.thumb_func
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r0
	cmp	r0, #0
	beq	.L1303
	subs	r2, r0, #1
	cmp	r2, #7
	bhi	.L1305
	b	HynixGetReadRetryDefault
.L1305:
	cmp	r0, #49
	bne	.L1306
	ldr	r0, .L1326
	movs	r2, #64
	ldr	r1, .L1326+4
	addw	r0, r0, #1214
	strb	r3, [r0, #-4]
	movs	r3, #4
	strb	r3, [r0, #-3]
	movs	r3, #15
	strb	r3, [r0, #-2]
	b	.L1324
.L1306:
	cmp	r0, #33
	beq	.L1307
	sub	r2, r0, #65
	cmp	r2, #1
	bhi	.L1308
.L1307:
	ldr	r0, .L1326
	strb	r3, [r0, #1210]
	movs	r3, #4
	b	.L1325
.L1308:
	cmp	r0, #34
	beq	.L1309
	cmp	r0, #67
	bne	.L1310
.L1309:
	ldr	r0, .L1326
	strb	r3, [r0, #1210]
	movs	r3, #5
.L1325:
	strb	r3, [r0, #1211]
	movs	r2, #45
	movs	r3, #7
	addw	r0, r0, #1214
	strb	r3, [r0, #-2]
	ldr	r1, .L1326+8
	b	.L1324
.L1310:
	cmp	r0, #35
	beq	.L1311
	cmp	r0, #68
	bne	.L1303
.L1311:
	ldr	r0, .L1326
	movs	r2, #95
	ldr	r1, .L1326+12
	addw	r0, r0, #1214
	strb	r3, [r0, #-4]
	movs	r3, #5
	strb	r3, [r0, #-3]
	movs	r3, #17
	strb	r3, [r0, #-2]
.L1324:
	b	ftl_memcpy
.L1303:
	bx	lr
.L1327:
	.align	2
.L1326:
	.word	.LANCHOR0
	.word	.LANCHOR1+408
	.word	.LANCHOR1+256
	.word	.LANCHOR1+304
	.fnend
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	1
	.global	NandcXferComp
	.thumb
	.thumb_func
	.type	NandcXferComp, %function
NandcXferComp:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #8
	ldr	r5, .L1367
	add	r0, r5, r0, lsl #3
	ldr	r3, [r5, #2260]
	ldr	r4, [r0, #12]
	cmp	r3, #3
	bls	.L1359
	ldr	r3, [r4, #16]
	lsls	r6, r3, #29
	bpl	.L1359
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	str	r3, [sp]
	cmp	r6, #0
	beq	.L1341
	movs	r6, #0
.L1331:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1339
	ldr	r3, [r5, #2260]
	cmp	r3, #5
	bhi	.L1332
.L1335:
	adds	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1334
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1367+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1367+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1334
.L1332:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r0, r3, #18
	bpl	.L1335
	ldr	r3, [sp, #4]
	lsls	r1, r3, #14
	bpl	.L1335
.L1339:
	ldr	r3, [r5, #2296]
	ldr	r4, .L1367
	cmp	r3, #0
	beq	.L1340
	ldr	r1, [sp]
	movs	r2, #0
	ldr	r0, [r4, #2288]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #2292]
	movs	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
	b	.L1340
.L1334:
	movs	r0, #1
	movs	r1, #5
	bl	usleep_range
	b	.L1331
.L1343:
	ldr	r3, [r4, #8]
	adds	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cbnz	r3, .L1342
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1367+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1367+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1342:
	movs	r0, #1
	movs	r1, #5
	bl	usleep_range
.L1341:
	ldr	r3, [sp]
	lsls	r2, r3, #11
	bpl	.L1343
	ldr	r3, [r5, #2304]
	cbz	r3, .L1344
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1344:
	ldr	r3, [r5, #2296]
	ldr	r6, .L1367
	cbz	r3, .L1345
	ldr	r1, [sp]
	movs	r2, #1
	ldr	r0, [r6, #2288]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #2292]
	movs	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
.L1345:
	ldr	r3, [r5, #2304]
	cbz	r3, .L1340
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1340:
	movs	r3, #0
	str	r3, [r5, #2296]
	b	.L1328
.L1359:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	lsls	r3, r3, #11
	bpl	.L1359
.L1328:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, pc}
.L1368:
	.align	2
.L1367:
	.word	.LANCHOR0
	.word	.LC87
	.word	.LC88
	.word	.LC89
	.fnend
	.size	NandcXferComp, .-NandcXferComp
	.align	1
	.global	NandcXferData
	.thumb
	.thumb_func
	.type	NandcXferData, %function
NandcXferData:
	.fnstart
	@ args = 4, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r3
	ldr	r6, .L1413
	tst	r10, #63
	.pad #92
	sub	sp, sp, #92
	mov	r9, r0
	add	r3, r6, r0, lsl #3
	mov	r5, r1
	mov	r7, r2
	ldr	r8, [r3, #12]
	bne	.L1370
	ldr	r3, [sp, #128]
	cbnz	r3, .L1371
	add	r0, sp, #24
	movs	r1, #255
	movs	r2, #64
	bl	ftl_memset
	add	r3, sp, #24
	str	r3, [sp, #128]
.L1371:
	ldr	r3, [sp, #128]
	mov	r0, r9
	mov	r1, r5
	str	r10, [sp]
	mov	r2, r7
	str	r3, [sp, #4]
	movs	r3, #0
	bl	NandcXferStart
	mov	r0, r9
	mov	r1, r5
	bl	NandcXferComp
	cmp	r5, #0
	bne	.L1397
	ldr	r3, [r6, #2308]
	lsrs	r4, r7, #1
	mov	r2, r5
	mov	r1, r5
	cmp	r3, #25
	ldr	r3, [sp, #128]
	ite	cc
	movcc	lr, #64
	movcs	lr, #128
.L1374:
	cmp	r1, r4
	add	r3, r3, #4
	add	ip, r2, lr
	bcs	.L1411
	ldr	r0, [r6, #2276]
	lsrs	r2, r2, #2
	adds	r1, r1, #1
	ldr	r2, [r0, r2, lsl #2]
	lsrs	r0, r2, #8
	strb	r2, [r3, #-4]
	strb	r0, [r3, #-3]
	lsrs	r0, r2, #16
	lsrs	r2, r2, #24
	strb	r0, [r3, #-2]
	strb	r2, [r3, #-1]
	mov	r2, ip
	b	.L1374
.L1411:
	movs	r2, #0
	ldr	r0, [r6, #2308]
	ldr	r1, [r6, #2260]
	lsrs	r7, r7, #2
	mov	fp, r2
.L1376:
	cmp	r2, r7
	bcs	.L1372
	cmp	r0, #0
	beq	.L1372
	add	r3, r2, #8
	ldr	r3, [r8, r3, lsl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	lsls	r4, r3, #29
	bmi	.L1400
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1400
	cmp	r1, #5
	bls	.L1378
	ldr	r4, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r4, r4, #3, #5
	ubfx	ip, r3, #27, #1
	ldr	r3, [sp, #20]
	ldr	lr, [sp, #20]
	ubfx	r3, r3, #16, #5
	orr	r4, r4, ip, lsl #5
	ubfx	lr, lr, #29, #1
	orr	r3, r3, lr, lsl #5
	cmp	r4, r3
	ldr	r3, [sp, #20]
	ittee	hi
	ldrhi	r4, [sp, #20]
	ubfxhi	r3, r3, #3, #5
	ldrls	r4, [sp, #20]
	ubfxls	r3, r3, #16, #5
	ite	hi
	ubfxhi	r4, r4, #27, #1
	ubfxls	r4, r4, #29, #1
	b	.L1410
.L1378:
	cmp	r1, #3
	bls	.L1380
	ldr	r4, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r4, r4, #3, #5
	ubfx	ip, r3, #28, #1
	ldr	r3, [sp, #20]
	ldr	lr, [sp, #20]
	ubfx	r3, r3, #16, #5
	orr	r4, r4, ip, lsl #5
	ubfx	lr, lr, #30, #1
	orr	r3, r3, lr, lsl #5
	cmp	r4, r3
	ldr	r3, [sp, #20]
	ittee	hi
	ldrhi	r4, [sp, #20]
	ubfxhi	r3, r3, #3, #5
	ldrls	r4, [sp, #20]
	ubfxls	r3, r3, #16, #5
	ite	hi
	ubfxhi	r4, r4, #28, #1
	ubfxls	r4, r4, #30, #1
.L1410:
	orr	r4, r3, r4, lsl #5
.L1380:
	cmp	fp, r4
	it	cc
	movcc	fp, r4
	b	.L1377
.L1400:
	mov	fp, #-1
.L1377:
	adds	r2, r2, #1
	b	.L1376
.L1397:
	mov	fp, #0
.L1372:
	movs	r3, #0
	str	r3, [r8, #16]
	b	.L1383
.L1370:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1409
	mov	fp, r4
.L1384:
	cmp	r4, r7
	bcs	.L1412
	and	ip, r4, #3
	cmp	r10, #0
	beq	.L1403
	add	r3, r10, r4, lsl #9
	b	.L1386
.L1403:
	mov	r3, r10
.L1386:
	ldr	r2, [sp, #128]
	mov	r0, r8
	ldr	r1, [sp, #128]
	cmp	r2, #0
	str	ip, [sp, #8]
	ite	ne
	movne	r2, #2
	moveq	r2, #0
	mla	r2, r4, r2, r1
	movs	r1, #1
	adds	r4, r4, #2
	str	r2, [sp]
	mov	r2, ip
	bl	NandcCopy1KB
	mov	r0, r9
	movs	r1, #1
	movs	r2, #2
	ldr	ip, [sp, #8]
	str	fp, [sp]
	str	fp, [sp, #4]
	mov	r3, ip
	bl	NandcXferStart
	mov	r0, r9
	movs	r1, #1
	bl	NandcXferComp
	b	.L1384
.L1412:
	mov	fp, #0
	b	.L1383
.L1409:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	movs	r2, #2
	mov	r3, r4
	mov	fp, r4
	bl	NandcXferStart
	str	r10, [sp, #8]
.L1389:
	cmp	r4, r7
	bcs	.L1383
	mov	r0, r9
	mov	r1, r5
	bl	NandcXferComp
	ldr	r3, [r8, #32]
	add	ip, r4, #2
	cmp	ip, r7
	str	r3, [sp, #20]
	bcs	.L1390
	movs	r3, #0
	mov	r0, r9
	str	r3, [sp]
	movs	r2, #2
	str	r3, [sp, #4]
	mov	r1, r3
	and	r3, ip, #3
	str	ip, [sp, #12]
	bl	NandcXferStart
	ldr	ip, [sp, #12]
.L1390:
	ldr	r3, [sp, #20]
	lsls	r3, r3, #29
	bmi	.L1405
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, lsl #5
	cmp	fp, r3
	it	cc
	movcc	fp, r3
	b	.L1391
.L1405:
	mov	fp, #-1
.L1391:
	ldr	r1, [sp, #128]
	cmp	r10, #0
	ldr	r3, [sp, #8]
	it	eq
	moveq	r3, #0
	ldr	r0, [sp, #128]
	cmp	r1, #0
	ite	ne
	movne	r1, #2
	moveq	r1, #0
	sub	r2, ip, #2
	str	ip, [sp, #12]
	mla	r4, r4, r1, r0
	and	r2, r2, #3
	mov	r0, r8
	movs	r1, #0
	str	r4, [sp]
	bl	NandcCopy1KB
	ldr	ip, [sp, #12]
	ldr	r3, [sp, #8]
	mov	r4, ip
	add	r3, r3, #1024
	str	r3, [sp, #8]
	b	.L1389
.L1383:
	ldr	r3, [r6, #2260]
	cmp	r3, #5
	bls	.L1395
	cbnz	r5, .L1395
	ldr	r3, [r8]
	and	r2, r3, #139264
	cmp	r2, #139264
	ittt	eq
	moveq	fp, #-1
	orreq	r3, r3, #131072
	streq	r3, [r8]
.L1395:
	mov	r0, fp
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1414:
	.align	2
.L1413:
	.word	.LANCHOR0
	.fnend
	.size	NandcXferData, .-NandcXferData
	.align	1
	.global	FlashReadRawPage
	.thumb
	.thumb_func
	.type	FlashReadRawPage, %function
FlashReadRawPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r3, .L1417
	mov	r6, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r5, [r3, #481]	@ zero_extendqisi2
	cbnz	r0, .L1416
	ldr	r2, .L1417+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	muls	r2, r3, r2
	cmp	r1, r2
	it	cc
	movcc	r5, #4
.L1416:
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r1, r6
	mov	r0, r4
	bl	FlashReadCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r2, r5
	movs	r1, #0
	mov	r3, r7
	mov	r0, r4
	str	r8, [sp]
	bl	NandcXferData
	mov	r5, r0
	mov	r0, r4
	bl	NandcFlashDeCs
	mov	r0, r5
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1418:
	.align	2
.L1417:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashReadRawPage, .-FlashReadRawPage
	.align	1
	.global	FlashDdrTunningRead
	.thumb
	.thumb_func
	.type	FlashDdrTunningRead, %function
FlashDdrTunningRead:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r3
	ldr	r4, .L1445
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #80]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #2260]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	ite	cc
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	beq	.L1433
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	mov	r0, r10
	bl	FlashReset
	mov	r3, r7
	mov	r2, fp
	mov	r0, r10
	ldr	r1, [sp]
	bl	FlashReadRawPage
	mov	r6, r0
	ldrb	r0, [r4, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2229]	@ zero_extendqisi2
	bl	NandcSetMode
	adds	r3, r6, #1
	bne	.L1422
.L1431:
	mov	r6, #-1
	b	.L1423
.L1422:
	mov	r2, r6
	ldr	r0, .L1445+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	itt	ls
	addls	r4, r4, r10, lsl #3
	ldrls	r3, [r4, #12]
	ldr	r4, .L1445+8
	itttt	ls
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #1188]
	adds	r3, r3, #1
	str	r3, [r4, #1188]
	cmp	r3, #2048
	bcc	.L1423
	movs	r7, #0
	str	r7, [r4, #1188]
	mov	fp, r7
	b	.L1421
.L1433:
	mov	r6, #1024
.L1421:
	movs	r4, #0
	mov	r8, #-1
	str	r4, [sp, #4]
	mov	ip, r4
	mov	r5, r4
.L1429:
	uxtb	r0, r9
	str	ip, [sp, #12]
	bl	NandcSetDdrPara
	mov	r3, r7
	mov	r0, r10
	mov	r2, fp
	ldr	r1, [sp]
	bl	FlashReadRawPage
	adds	r3, r6, #1
	cmp	r0, r3
	ldr	ip, [sp, #12]
	bhi	.L1425
	cmp	r0, #2
	bhi	.L1435
	adds	r5, r5, #1
	cmp	r5, #9
	bls	.L1435
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1427
.L1425:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1436
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1437
	str	r5, [sp, #4]
	b	.L1436
.L1435:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1426
.L1436:
	movs	r5, #0
.L1426:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1429
.L1427:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	it	cs
	movcs	r4, ip
	b	.L1428
.L1437:
	mov	r4, ip
.L1428:
	cbz	r4, .L1430
	ldr	r0, .L1445+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1430:
	cmp	r8, #0
	beq	.L1423
	ldr	r0, .L1445+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1431
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1423:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1446:
	.align	2
.L1445:
	.word	.LANCHOR0
	.word	.LC90
	.word	.LANCHOR4
	.word	.LC91
	.word	.LC92
	.fnend
	.size	FlashDdrTunningRead, .-FlashDdrTunningRead
	.align	1
	.global	FlashReadPage
	.thumb
	.thumb_func
	.type	FlashReadPage, %function
FlashReadPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r8, r2
	mov	r5, r0
	mov	r6, r1
	mov	r7, r3
	bl	FlashReadRawPage
	adds	r2, r0, #1
	mov	r4, r0
	bne	.L1448
	ldr	r9, .L1467+4
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1449
.L1451:
	ldrb	r3, [r10, #2252]	@ zero_extendqisi2
	ldr	r9, .L1467+4
	cbz	r3, .L1448
	b	.L1466
.L1449:
	movs	r3, #0
	mov	r0, r5
	strb	r3, [r9, #8]
	mov	r1, r6
	mov	r3, r7
	mov	r2, r8
	bl	FlashReadRawPage
	strb	fp, [r9, #8]
	adds	r3, r0, #1
	beq	.L1451
	mov	r4, r0
	b	.L1448
.L1466:
	ldr	r3, [r9, #80]
	mov	r1, r6
	mov	r0, r5
	mov	r2, r8
	ldr	r10, [r3, #304]
	movs	r3, #1
	str	r3, [sp]
	mov	r3, r7
	bl	FlashDdrTunningRead
	adds	r1, r0, #1
	mov	r4, r0
	beq	.L1452
	ldrb	r3, [r9, #2312]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1448
.L1452:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1448:
	ldr	r9, .L1467+8
	ldr	ip, [r9, #1192]
	cmp	ip, #0
	beq	.L1453
	adds	r2, r4, #1
	bne	.L1453
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r3, r6
	mov	r2, r5
	mov	r4, r0
	ldr	r0, .L1467
	mov	r1, r4
	bl	printk
	adds	r3, r4, #1
	bne	.L1453
	ldr	r3, .L1467+4
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cbz	r3, .L1453
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	r4, [r9, #1192]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	r4
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1453:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1468:
	.align	2
.L1467:
	.word	.LC93
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	FlashReadPage, .-FlashReadPage
	.align	1
	.global	FlashDdrParaScan
	.thumb
	.thumb_func
	.type	FlashDdrParaScan, %function
FlashDdrParaScan:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r7, r0
	ldr	r4, .L1479
	mov	r6, r1
	movs	r5, #0
	ldrb	r0, [r4, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2229]	@ zero_extendqisi2
	bl	NandcSetMode
	mov	r1, r6
	mov	r2, r5
	mov	r3, r5
	mov	r0, r7
	str	r5, [sp]
	bl	FlashDdrTunningRead
	mov	r1, r6
	mov	r2, r5
	mov	r3, r5
	mov	r8, r0
	mov	r0, r7
	bl	FlashReadRawPage
	adds	r0, r0, #1
	beq	.L1470
	cmp	r8, #-1
	bne	.L1471
.L1470:
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L1471
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1479
	movs	r2, #0
	strb	r2, [r3, #2252]
	b	.L1472
.L1471:
	movs	r3, #1
	strb	r3, [r4, #2252]
.L1472:
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1480:
	.align	2
.L1479:
	.word	.LANCHOR0
	.fnend
	.size	FlashDdrParaScan, .-FlashDdrParaScan
	.align	1
	.global	FlashLoadPhyInfo
	.thumb
	.thumb_func
	.type	FlashLoadPhyInfo, %function
FlashLoadPhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, .L1496
	movs	r4, #0
	ldr	r7, .L1496+4
	mov	r9, #4
	ldr	r5, .L1496+8
	mov	r8, #-1
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r7, #4]
	str	r4, [r5, #1200]
	mov	r6, r5
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #1184]
	str	r3, [r5, #1196]
	bl	flash_enter_slc_mode
.L1482:
	add	fp, r4, #1
	mov	r10, #0
.L1484:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #1184]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1483
	movs	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #1184]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1483
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1485
	b	.L1484
.L1486:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	bne	.L1492
	ldr	r8, .L1496+24
	add	r1, fp, #160
	movs	r2, #32
	add	r0, r8, #472
	bl	ftl_memcpy
	ldr	r1, [r6, #1196]
	movs	r2, #32
	ldr	r0, .L1496+12
	adds	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #1196]
	mov	r2, #852
	ldr	r0, .L1496+16
	adds	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r8, #482]
	bl	FlashBlockAlignInit
	ldr	r8, [r6, #1196]
	str	r4, [r6, #1200]
	mov	r0, r4
	ldr	r1, [r7, #4]
	ldr	r3, [r8, #1076]
	strb	r3, [r7, #2252]
	bl	__aeabi_uidiv
	adds	r0, r0, #1
	cmp	r0, #1
	itee	hi
	strhi	r0, [r6, #1204]
	movls	r3, #2
	strls	r3, [r6, #1204]
	ldrh	r3, [r8, #14]
	mov	r8, #0
	strb	r3, [r5, #1208]
.L1485:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1482
	mov	r0, r9
.L1495:
	bl	flash_exit_slc_mode
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1483:
	ldr	fp, [r5, #1196]
	ldr	r2, .L1496+20
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1485
	cmp	r8, #0
	bne	.L1486
	mov	r0, r4
	ldr	r1, [r7, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L1496+8
	adds	r0, r0, #1
	str	r0, [r3, #1204]
	mov	r0, r8
	b	.L1495
.L1492:
	mov	r8, #-1
	b	.L1485
.L1497:
	.align	2
.L1496:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR0+48
	.word	.LANCHOR0+1210
	.word	1312902724
	.word	.LANCHOR1
	.fnend
	.size	FlashLoadPhyInfo, .-FlashLoadPhyInfo
	.align	1
	.global	ToshibaReadRetrial
	.thumb
	.thumb_func
	.type	ToshibaReadRetrial, %function
ToshibaReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r9, r0
	mov	fp, r3
	str	r2, [sp, #8]
	str	r1, [sp, #16]
	bl	NandcWaitFlashReady
	ldr	r4, .L1526
	add	r3, r4, r9, lsl #3
	ldrb	r2, [r3, #16]	@ zero_extendqisi2
	ldr	r7, [r3, #12]
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	add	r6, r2, #8
	subs	r3, r3, #67
	add	r6, r7, r6, lsl #8
	cmp	r3, #1
	lsl	r3, r2, #8
	str	r3, [sp, #12]
	bls	.L1515
	ldrb	r5, [r4, #2252]	@ zero_extendqisi2
	cbz	r5, .L1516
	movs	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	movs	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
	b	.L1500
.L1516:
	str	r5, [sp, #4]
.L1500:
	ldr	r3, [sp, #12]
	movs	r1, #92
	adds	r3, r7, r3
	str	r1, [r3, #2056]
	movs	r1, #197
	str	r1, [r3, #2056]
	b	.L1499
.L1515:
	movs	r3, #0
	str	r3, [sp, #4]
.L1499:
	mov	r8, #1
	mov	r10, #-1
	lsls	r3, r2, #8
	str	r3, [sp, #20]
.L1501:
	ldr	r3, .L1526+4
	ldrb	r3, [r3, #1209]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r8, r3
	bcs	.L1525
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	mov	r0, r6
	uxtb	r1, r8
	subs	r3, r3, #67
	cmp	r3, #1
	bhi	.L1502
	bl	SandiskSetRRPara
	b	.L1503
.L1502:
	bl	ToshibaSetRRPara
.L1503:
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1504
	ldr	r3, .L1526+4
	ldrb	r3, [r3, #1209]	@ zero_extendqisi2
	subs	r3, r3, #3
	cmp	r8, r3
	itttt	eq
	moveq	r2, #179
	ldreq	r3, [sp, #20]
	addeq	r3, r7, r3
	streq	r2, [r3, #2056]
.L1504:
	ldr	r3, [sp, #12]
	movs	r2, #38
	adds	r3, r7, r3
	str	r2, [r3, #2056]
	movs	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cbz	r3, .L1505
	movs	r0, #4
	bl	NandcSetDdrMode
	mov	r0, r9
	mov	r3, fp
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	bl	FlashReadRawPage
	mov	r5, r0
	movs	r0, #0
	bl	NandcSetDdrMode
	b	.L1506
.L1505:
	mov	r0, r9
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1506:
	adds	r2, r5, #1
	beq	.L1507
	ldrb	r2, [r4, #2312]	@ zero_extendqisi2
	cmp	r10, #-1
	it	eq
	moveq	r10, r5
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1509
	mov	fp, #0
	str	fp, [sp, #8]
.L1507:
	add	r8, r8, #1
	b	.L1501
.L1525:
	mov	r5, r10
.L1509:
	ldrb	r2, [r4, #1208]	@ zero_extendqisi2
	mov	r0, r6
	movs	r1, #0
	subs	r2, r2, #67
	cmp	r2, #1
	bhi	.L1511
	bl	SandiskSetRRPara
	b	.L1512
.L1511:
	bl	ToshibaSetRRPara
.L1512:
	ldr	r3, [sp, #12]
	movs	r2, #255
	add	r7, r7, r3
	str	r2, [r7, #2056]
	ldrb	r2, [r4, #2312]	@ zero_extendqisi2
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1513
	adds	r3, r5, #1
	it	ne
	movne	r5, #256
.L1513:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cbz	r3, .L1514
	movs	r0, #4
	bl	NandcSetDdrMode
.L1514:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1527:
	.align	2
.L1526:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	ToshibaReadRetrial, .-ToshibaReadRetrial
	.align	1
	.global	SamsungReadRetrial
	.thumb
	.thumb_func
	.type	SamsungReadRetrial, %function
SamsungReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r5, .L1541
	mov	r9, r2
	mov	r7, r3
	mov	fp, r1
	bl	NandcWaitFlashReady
	add	r2, r5, r8, lsl #3
	mov	r10, #1
	mov	r4, #-1
	ldrb	r3, [r2, #16]	@ zero_extendqisi2
	ldr	r6, [r2, #12]
	adds	r3, r3, #8
	add	r6, r6, r3, lsl #8
.L1529:
	ldr	r3, .L1541+4
	ldrb	r3, [r3, #1209]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r10, r3
	bcs	.L1532
	mov	r0, r6
	uxtb	r1, r10
	bl	SamsungSetRRPara
	mov	r2, r9
	mov	r0, r8
	mov	r1, fp
	mov	r3, r7
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1530
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1535
	movs	r7, #0
	mov	r9, r7
.L1530:
	add	r10, r10, #1
	b	.L1529
.L1535:
	mov	r4, r0
.L1532:
	mov	r0, r6
	movs	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L1534
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
.L1534:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1542:
	.align	2
.L1541:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	1
	.global	MicronReadRetrial
	.thumb
	.thumb_func
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L1567
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	mov	fp, r2
	str	r1, [sp, #20]
	ldrb	r5, [r3, #2312]	@ zero_extendqisi2
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cbnz	r3, .L1544
	add	r5, r5, r5, lsl #1
	ubfx	r5, r5, #2, #8
	b	.L1545
.L1544:
	ldr	r2, .L1567+4
	smull	r2, r3, r5, r2
	uxtb	r5, r3
.L1545:
	ldr	r3, .L1567
	mov	r10, #0
	add	r3, r3, r6, lsl #3
	str	r3, [sp, #24]
.L1555:
	mov	r0, r6
	mov	r9, #0
	bl	NandcWaitFlashReady
	mov	r4, #-1
	ldr	r3, [sp, #24]
	ldr	r3, [r3, #12]
	str	r3, [sp, #12]
	ldr	r3, [sp, #24]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #12]
	add	r7, r3, r2, lsl #8
.L1546:
	ldr	r3, .L1567+8
	ldrb	r3, [r3, #1209]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L1549
	movs	r3, #239
	movs	r0, #200
	str	r3, [r7, #2056]
	movs	r3, #137
	str	r3, [r7, #2052]
	bl	NandcDelayns
	mov	ip, #0
	add	r3, r9, #1
	mov	r0, r6
	str	r3, [r7, #2048]
	mov	r2, fp
	str	ip, [r7, #2048]
	str	ip, [r7, #2048]
	str	ip, [r7, #2048]
	str	r3, [sp, #8]
	mov	r3, r8
	ldr	r1, [sp, #20]
	str	ip, [sp, #28]
	bl	FlashReadRawPage
	adds	r3, r0, #1
	beq	.L1547
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	cmp	r0, r5
	ldr	ip, [sp, #28]
	bcc	.L1557
	mov	r8, ip
	mov	fp, ip
.L1547:
	ldr	r9, [sp, #8]
	b	.L1546
.L1557:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L1549:
	ldr	r2, [sp, #16]
	movs	r0, #200
	ldr	r3, [sp, #12]
	movs	r7, #0
	add	r3, r3, r2, lsl #8
	movs	r2, #239
	str	r3, [sp, #8]
	str	r2, [r3, #2056]
	movs	r2, #137
	str	r2, [r3, #2052]
	bl	NandcDelayns
	cmp	r4, r5
	ldr	r3, [sp, #8]
	str	r7, [r3, #2048]
	str	r7, [r3, #2048]
	str	r7, [r3, #2048]
	str	r7, [r3, #2048]
	bcc	.L1551
	adds	r1, r4, #1
	ldr	r0, .L1567+12
	it	ne
	movne	r4, #256
	mov	r1, r9
	str	r4, [sp]
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L1553
	ldr	r3, .L1567
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cbz	r3, .L1562
	adds	r2, r4, #1
	bne	.L1562
	mov	r0, r6
	movs	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L1555
.L1553:
	mov	r0, r6
	mov	r1, r7
	bl	micron_auto_read_calibration_config
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
	b	.L1562
.L1551:
	cmp	r10, #0
	beq	.L1562
	mov	r0, r6
	mov	r1, r7
	bl	micron_auto_read_calibration_config
	mov	r4, #256
.L1562:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1568:
	.align	2
.L1567:
	.word	.LANCHOR0
	.word	1431655766
	.word	.LANCHOR4
	.word	.LC94
	.fnend
	.size	MicronReadRetrial, .-MicronReadRetrial
	.align	1
	.global	HynixReadRetrial
	.thumb
	.thumb_func
	.type	HynixReadRetrial, %function
HynixReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r8, r3
	ldr	r5, .L1586
	mov	r10, r2
	mov	r7, r0
	mov	fp, r1
	adds	r2, r5, r0
	mov	r6, #-1
	ldr	r3, [r5, #44]
	ldrb	r4, [r2, #1222]	@ zero_extendqisi2
	ldrb	r9, [r5, #1212]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #7
	cmp	r3, #1
	it	ls
	ldrbls	r4, [r2, #1230]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L1571:
	cmp	ip, r9
	bcs	.L1575
	adds	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1586+4
	cmp	r4, r9
	it	cs
	movcs	r4, #0
	str	ip, [sp, #4]
	mov	r3, r4
	bl	HynixSetRRPara
	mov	r2, r10
	mov	r0, r7
	mov	r1, fp
	mov	r3, r8
	bl	FlashReadRawPage
	adds	r2, r0, #1
	ldr	ip, [sp, #4]
	beq	.L1573
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	cmp	r6, #-1
	it	eq
	moveq	r6, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1580
	mov	r8, #0
	mov	r10, r8
.L1573:
	add	ip, ip, #1
	b	.L1571
.L1580:
	mov	r6, r0
.L1575:
	ldr	r3, [r5, #44]
	add	r7, r7, r5
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	ite	ls
	strbls	r4, [r7, #1230]
	strbhi	r4, [r7, #1222]
	add	r3, r3, r3, lsl #1
	cmp	r6, r3, asr #2
	bcc	.L1579
	adds	r3, r6, #1
	it	ne
	movne	r6, #256
.L1579:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1587:
	.align	2
.L1586:
	.word	.LANCHOR0
	.word	.LANCHOR0+1214
	.fnend
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	1
	.thumb
	.thumb_func
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	mov	r9, r0
	mov	r10, r2
	mov	r7, r3
	str	r1, [sp, #16]
	bl	NandcWaitFlashReady
	ldr	r3, .L1614
	add	r2, r3, r9, lsl #3
	ldr	r4, [r2, #12]
	ldrb	r2, [r2, #16]	@ zero_extendqisi2
	str	r2, [sp, #12]
	ldrb	r2, [r3, #2228]	@ zero_extendqisi2
	str	r3, [sp, #20]
	cmp	r2, #0
	bne	.L1589
	ldr	r3, [sp, #12]
	mov	r5, #-1
	movs	r6, #1
	lsl	fp, r3, #8
	add	r8, r4, fp
	addw	r3, r8, #2056
	str	r3, [sp, #24]
.L1593:
	ldr	r3, [sp, #24]
	mov	ip, #0
	ldr	r1, [sp, #16]
	mov	r0, r9
	str	ip, [sp, #28]
	mov	r2, r3
	movs	r3, #239
	str	r3, [r2]
	movs	r3, #141
	str	r3, [r8, #2052]
	mov	r2, r10
	ldr	r3, .L1614+4
	ldrsb	r3, [r6, r3]
	str	r3, [r8, #2048]
	mov	r3, r7
	str	ip, [r8, #2048]
	str	ip, [r8, #2048]
	str	ip, [r8, #2048]
	bl	FlashReadRawPage
	adds	r1, r0, #1
	beq	.L1590
	ldr	r3, [sp, #20]
	cmp	r5, #-1
	it	eq
	moveq	r5, r0
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1601
	ldr	ip, [sp, #28]
	mov	r7, ip
	mov	r10, ip
.L1590:
	adds	r6, r6, #1
	cmp	r6, #26
	bne	.L1593
	b	.L1592
.L1601:
	mov	r5, r0
.L1592:
	add	fp, fp, r4
	movs	r3, #239
	str	r3, [fp, #2056]
	ldr	r3, [sp, #12]
	add	r4, r4, r3, lsl #8
	movs	r3, #141
	b	.L1613
.L1589:
	ldr	r3, [sp, #12]
	mov	r5, #-1
	ldr	r8, .L1614+12
	movs	r6, #1
	lsl	ip, r3, #8
	add	fp, r4, ip
	addw	r3, fp, #2056
	str	r3, [sp, #24]
.L1598:
	ldr	r3, [sp, #24]
	mov	r0, r9
	ldr	r1, [sp, #16]
	str	ip, [sp, #28]
	mov	r2, r3
	movs	r3, #239
	str	r3, [r2]
	movs	r3, #137
	str	r3, [fp, #2052]
	mov	r2, r10
	ldrb	r3, [r8, #4]	@ zero_extendqisi2
	str	r3, [fp, #2048]
	ldrb	r3, [r8, #5]	@ zero_extendqisi2
	str	r3, [fp, #2048]
	ldrb	r3, [r8, #6]	@ zero_extendqisi2
	str	r3, [fp, #2048]
	ldrb	r3, [r8, #7]	@ zero_extendqisi2
	str	r3, [fp, #2048]
	mov	r3, r7
	bl	FlashReadRawPage
	adds	r2, r0, #1
	ldr	ip, [sp, #28]
	beq	.L1595
	ldr	r3, [sp, #20]
	cmp	r5, #-1
	it	eq
	moveq	r5, r0
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1602
	movs	r7, #0
	mov	r10, r7
.L1595:
	adds	r6, r6, #1
	add	r8, r8, #4
	cmp	r6, #26
	bne	.L1598
	b	.L1597
.L1602:
	mov	r5, r0
.L1597:
	add	ip, ip, r4
	movs	r3, #239
	str	r3, [ip, #2056]
	ldr	r3, [sp, #12]
	add	r4, r4, r3, lsl #8
	movs	r3, #137
.L1613:
	str	r3, [r4, #2052]
	movs	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	ldr	r3, [sp, #20]
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r5, r3, asr #2
	bcc	.L1599
	adds	r3, r5, #1
	ldr	r0, .L1614+8
	it	ne
	movne	r5, #256
	mov	r1, r6
	str	r5, [sp]
	mov	r3, r6
	ldr	r2, [sp, #16]
	bl	printk
.L1599:
	mov	r0, r9
	bl	NandcWaitFlashReady
	mov	r0, r5
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1615:
	.align	2
.L1614:
	.word	.LANCHOR0
	.word	.LANCHOR3+16
	.word	.LC95
	.word	.LANCHOR3+44
	.fnend
	.size	samsung_read_retrial, .-samsung_read_retrial
	.align	1
	.global	FlashProgPage
	.thumb
	.thumb_func
	.type	FlashProgPage, %function
FlashProgPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r3, .L1619
	mov	r5, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r6, [r3, #481]	@ zero_extendqisi2
	cbnz	r0, .L1617
	ldr	r2, .L1619+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	muls	r1, r3, r1
	cmp	r5, r1
	bcs	.L1617
	ldrb	r3, [r2]	@ zero_extendqisi2
	cmp	r3, #0
	it	ne
	movne	r6, #4
.L1617:
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r0, r4
	mov	r1, r5
	bl	FlashProgFirstCmd
	mov	r2, r6
	mov	r3, r7
	mov	r0, r4
	movs	r1, #1
	str	r8, [sp]
	bl	NandcXferData
	mov	r1, r5
	mov	r0, r4
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r1, r5
	mov	r0, r4
	bl	FlashReadStatus
	mov	r5, r0
	mov	r0, r4
	bl	NandcFlashDeCs
	and	r0, r5, #1
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1620:
	.align	2
.L1619:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashProgPage, .-FlashProgPage
	.align	1
	.global	FlashSavePhyInfo
	.thumb
	.thumb_func
	.type	FlashSavePhyInfo, %function
FlashSavePhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L1634
	ldr	r5, .L1634+4
	ldr	r8, .L1634+12
	ldr	r3, [r4, #1184]
	mov	fp, r4
	ldrb	r0, [r4, #1210]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #1196]
	bl	FlashBchSel
	movs	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #1184]
	bl	ftl_memset
	ldr	r3, [r4, #1196]
	addw	r1, r5, #2068
	movs	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #1196]
	ldrb	r3, [r5, #2230]	@ zero_extendqisi2
	adds	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #2252]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #1196]
	addw	r1, r5, #2232
	movs	r2, #8
	adds	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #1196]
	addw	r1, r5, #1172
	movs	r2, #32
	adds	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #1196]
	ldr	r1, .L1634+8
	movs	r2, #32
	adds	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #1196]
	add	r1, r5, #48
	movs	r2, #32
	adds	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #1196]
	mov	r2, #852
	addw	r1, r5, #1210
	adds	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #1196]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	mov	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #1212]
	str	r3, [r4, #1196]
	str	r0, [r6, #8]
	movs	r0, #0
	bl	flash_enter_slc_mode
	movs	r6, #0
	mov	r7, r6
.L1627:
	ldr	r1, [r5, #4]
	movs	r0, #0
	mov	r2, r0
	muls	r1, r7, r1
	bl	FlashEraseBlock
	ldrb	r9, [r5, #144]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1622
	mov	r9, #0
.L1623:
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #1184]
	mov	r3, r0
	mla	r1, r1, r7, r9
	add	r9, r9, #1
	bl	FlashProgPage
	cmp	r9, #10
	bne	.L1623
	b	.L1624
.L1622:
	ldr	r1, [r10, #4]
	mov	r3, r9
	ldr	r2, [r4, #1184]
	mov	r0, r9
	muls	r1, r7, r1
	bl	FlashProgPage
	ldr	r1, [r10, #4]
	mov	r0, r9
	ldr	r2, [r4, #1184]
	mov	r3, r9
	muls	r1, r7, r1
	adds	r1, r1, #1
	bl	FlashProgPage
.L1624:
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #1212]
	mov	r3, r0
	muls	r1, r7, r1
	bl	FlashReadRawPage
	adds	r2, r7, #1
	adds	r0, r0, #1
	beq	.L1625
	ldr	r9, [fp, #1196]
	ldr	r3, [r9]
	cmp	r3, r8
	bne	.L1625
	add	r0, r9, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [r9, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1625
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [fp, #1204]
	mul	r3, r3, r7
	str	r3, [fp, #1200]
	beq	.L1628
	movs	r6, #1
.L1625:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1627
	b	.L1626
.L1628:
	movs	r6, #2
.L1626:
	movs	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	lsrs	r0, r0, #5
	negs	r0, r0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1635:
	.align	2
.L1634:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR1+472
	.word	1312902724
	.fnend
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	1
	.global	FlashReadIdbDataRaw
	.thumb
	.thumb_func
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r9, r0
	ldr	r3, .L1655
	ldr	r4, .L1655+4
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r4, #2248]
	ldrb	r10, [r4, #2312]	@ zero_extendqisi2
	str	r0, [sp, #4]	@ unaligned
	cbz	r3, .L1637
	movs	r0, #0
	bl	flash_enter_slc_mode
.L1637:
	mov	r0, r9
	movs	r1, #0
	mov	r2, #2048
	mov	r8, #-1
	bl	ftl_memset
	movs	r5, #2
.L1638:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1642
	movs	r7, #0
.L1640:
	add	r3, sp, #4
	ldr	r6, .L1655+8
	ldrb	fp, [r7, r3]	@ zero_extendqisi2
	mov	r0, fp
	bl	FlashBchSel
	ldr	r1, [r4, #4]
	movs	r0, #0
	ldr	r2, [r6, #1184]
	muls	r1, r5, r1
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1639
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L1640
	b	.L1641
.L1639:
	ldr	r3, [r6, #1184]
	ldr	r2, .L1655+12
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1641
	mov	r1, fp
	ldr	r0, .L1655+16
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #1184]
	bl	ftl_memcpy
	ldr	r3, [r6, #1184]
	ldr	r2, .L1655+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r6, #1204]
	cmp	r3, r5
	bls	.L1645
	str	r5, [r6, #1204]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1641:
	adds	r5, r5, #1
	b	.L1638
.L1645:
	mov	r8, #0
.L1642:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #2248]
	cbz	r3, .L1649
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1649:
	mov	r0, r8
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1656:
	.align	2
.L1655:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	-52655045
	.word	.LC96
	.fnend
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	1
	.global	FlashInit
	.thumb
	.thumb_func
	.type	FlashInit, %function
FlashInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	.pad #20
	sub	sp, sp, #20
	mov	r0, #32768
	bl	ftl_malloc
	ldr	r4, .L1780
	ldr	r6, .L1780+4
	movs	r7, #0
	add	r9, r6, #12
	mov	r8, r4
	str	r0, [r4, #1184]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r4, #1212]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r4, #1216]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r4, #1220]
	mov	r0, #4096
	bl	ftl_malloc
	movs	r3, #50
	str	r7, [r4, #1204]
	strb	r3, [r4, #1208]
	strb	r3, [r6, #1]
	movs	r3, #128
	str	r7, [r4, #1188]
	str	r3, [r6, #4]
	movs	r3, #60
	strb	r7, [r4, #1228]
	strb	r3, [r4, #1210]
	strb	r7, [r6, #2252]
	strb	r7, [r6]
	str	r0, [r4, #1224]
	mov	r0, r5
	bl	NandcInit
	addw	r5, r6, #2068
	mov	r4, r6
.L1663:
	add	r3, r9, r7, lsl #3
	uxtb	fp, r7
	ldr	r6, [r9, r7, lsl #3]
	ldrb	r10, [r3, #4]	@ zero_extendqisi2
	mov	r0, fp
	bl	FlashReset
	mov	r0, fp
	bl	NandcFlashCs
	movs	r3, #144
	add	r6, r6, r10, lsl #8
	movs	r0, #200
	mov	r10, #0
	str	r3, [r6, #2056]
	str	r10, [r6, #2052]
	bl	NandcDelayns
	ldr	r2, [r6, #2048]
	uxtb	r2, r2
	strb	r2, [r5]
	ldr	r1, [r6, #2048]
	cmp	r2, #44
	strb	r1, [r5, #1]
	ldr	r1, [r6, #2048]
	strb	r1, [r5, #2]
	ldr	r1, [r6, #2048]
	strb	r1, [r5, #3]
	ldr	r1, [r6, #2048]
	strb	r1, [r5, #4]
	ldr	r1, [r6, #2048]
	strb	r1, [r5, #5]
	bne	.L1658
	movs	r2, #239
	movs	r0, #200
	str	r2, [r6, #2056]
	movs	r2, #1
	str	r2, [r6, #2052]
	bl	NandcDelayns
	movs	r2, #4
	str	r2, [r6, #2048]
	str	r10, [r6, #2048]
	str	r10, [r6, #2048]
	str	r10, [r6, #2048]
.L1658:
	mov	r0, fp
	bl	NandcFlashDeCs
	ldrb	r2, [r5]	@ zero_extendqisi2
	subs	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1659
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L1780+8
	str	r1, [sp]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	adds	r1, r7, #1
	bl	printk
.L1659:
	cbnz	r7, .L1660
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1716
	ldr	r3, .L1780+4
	ldrb	r3, [r3, #2069]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1716
.L1660:
	ldrb	r3, [r5]	@ zero_extendqisi2
	adds	r7, r7, #1
	adds	r5, r5, #8
	cmp	r3, #181
	itt	eq
	moveq	r3, #44
	strbeq	r3, [r5, #-8]
	cmp	r7, #4
	bne	.L1663
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1664
	ldr	r3, .L1780+4
	ldr	r0, [r3, #2256]
	bl	NandcSetDdrMode
.L1664:
	ldr	r6, .L1780+4
	movs	r1, #0
	mov	r2, #852
	ldr	r5, .L1780+12
	addw	r0, r6, #1210
	bl	ftl_memset
	ldr	r0, .L1780+16
	add	r3, r0, #472
	str	r3, [r4, #44]
	movs	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, [r4, #2264]
	cmp	r3, r5
	bne	.L1665
	ldrb	r2, [r0, #491]	@ zero_extendqisi2
	cmp	r2, #50
	itt	ne
	movne	r2, #1
	strne	r2, [r6, #2248]
.L1665:
	ldrb	r6, [r4, #2069]	@ zero_extendqisi2
	cmp	r6, #161
	beq	.L1666
	cmp	r6, #241
	beq	.L1666
	cmp	r6, #218
	beq	.L1666
	and	r2, r6, #253
	cmp	r2, #209
	beq	.L1666
	cmp	r6, #220
	bne	.L1667
	ldr	r2, .L1780+4
	ldrb	r2, [r2, #2071]	@ zero_extendqisi2
	cmp	r2, #149
	bne	.L1667
.L1666:
	movs	r7, #16
	strb	r7, [r4, #1]
	strb	r7, [r8, #1210]
	movs	r1, #1
	ldrb	r7, [r4, #2068]	@ zero_extendqisi2
	strb	r1, [r4]
	cmp	r7, #152
	ldr	r2, .L1780
	strb	r7, [r0, #3417]
	strb	r6, [r0, #3418]
	bne	.L1669
	ldr	r7, .L1780+4
	ldrsb	r7, [r7, #2072]
	cmp	r7, #0
	blt	.L1670
	movs	r1, #24
	strb	r1, [r2, #1210]
.L1669:
	cmp	r3, r5
	beq	.L1672
	movw	r2, #2049
	cmp	r3, r2
	bne	.L1673
.L1672:
	movs	r3, #16
	strb	r3, [r8, #1210]
.L1673:
	cmp	r6, #218
	bne	.L1674
	mov	r3, #2048
	b	.L1775
.L1674:
	cmp	r6, #220
	bne	.L1676
	mov	r3, #4096
.L1775:
	strh	r3, [r0, #3430]	@ movhi
	strb	r6, [r0, #3418]
	b	.L1675
.L1676:
	cmp	r6, #211
	itttt	eq
	moveq	r3, #4096
	strheq	r3, [r0, #3430]	@ movhi
	moveq	r3, #2
	strbeq	r3, [r0, #3429]
.L1675:
	ldr	r1, .L1780+20
	movs	r2, #32
	ldr	r0, .L1780+24
	bl	ftl_memcpy
	ldr	r0, .L1780+28
	movs	r2, #32
	add	r1, r0, #2944
	bl	ftl_memcpy
.L1667:
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r6, .L1780+4
	cmp	r3, #0
	bne	.L1677
	bl	FlashLoadPhyInfoInRam
	cbnz	r0, .L1679
	ldr	r3, [r6, #44]
	ldrh	r3, [r3, #16]
	lsrs	r3, r3, #8
	lsls	r7, r3, #31
	and	r0, r3, #7
	strb	r0, [r6, #2229]
	bmi	.L1679
	movs	r3, #1
	strb	r3, [r6, #2252]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2229]	@ zero_extendqisi2
	bl	NandcSetMode
.L1679:
	ldr	r3, [r4, #44]
	ldr	r6, .L1780+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #144]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1677
	ldr	r3, [r6, #2256]
	cbz	r3, .L1682
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	b	.L1776
.L1781:
	.align	2
.L1780:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LC97
	.word	1446522928
	.word	.LANCHOR1
	.word	.LANCHOR1+3292
	.word	.LANCHOR0+48
	.word	.LANCHOR1+472
.L1682:
	ldrb	r0, [r6, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2229]	@ zero_extendqisi2
.L1776:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1677
	movs	r0, #1
	ldr	r6, .L1782
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #44]
	ldr	r0, .L1782+4
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	adds	r1, r0, #1
	beq	.L1736
	bl	FlashDieInfoInit
	ldr	r3, [r6, #44]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r2, .L1782+8
	ldr	r3, [r6, #44]
	ldrh	r2, [r2, #3270]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	addw	r2, r2, #4095
	cmp	r1, r2, asr #12
	blt	.L1684
	ldrh	r2, [r3, #14]
	adds	r2, r2, #255
	cmp	r1, r2, asr #8
	bge	.L1685
.L1684:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L1685:
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1686
	bl	FlashSavePhyInfo
	movs	r0, #0
	bl	flash_enter_slc_mode
	movs	r0, #0
	ldr	r1, [r8, #1200]
	bl	FlashDdrParaScan
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1686:
	bl	FlashSavePhyInfo
.L1677:
	ldr	r2, [r4, #44]
	mov	r9, #0
	str	r9, [r8, #1192]
	ldr	r7, .L1782
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r10, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #144]
	ldrh	r3, [r2, #16]
	ldr	r6, .L1782+12
	ubfx	r1, r3, #7, #1
	strb	r1, [r4, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r8, #1229]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #2240]
	strb	r3, [r4, #2229]
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r10
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #44]
	ldrh	r2, [r3, #16]
	lsls	r2, r2, #25
	bpl	.L1688
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r7, #1211]	@ zero_extendqisi2
	strb	r0, [r7, #1208]
	strb	r3, [r7, #1209]
	ldrb	r3, [r7, #1212]	@ zero_extendqisi2
	strb	r3, [r6, #1209]
	subs	r3, r0, #1
	cmp	r3, #7
	bhi	.L1689
	ldr	r3, .L1782+16
	str	r3, [r6, #1192]
	subs	r3, r0, #5
	cmp	r3, #1
	bls	.L1690
	cmp	r0, #8
	bne	.L1691
.L1690:
	cmp	r0, #7
	mov	r3, #1
	str	r3, [r4, #2304]
	beq	.L1717
	cmp	r0, #8
	b	.L1779
.L1717:
	ldr	r3, .L1782+20
.L1693:
	subs	r1, r3, #1
	movs	r2, #0
	adds	r3, r3, #31
.L1694:
	ldrsb	r6, [r1, #1]!
	cmp	r6, #0
	it	eq
	addeq	r2, r2, #1
	cmp	r1, r3
	bne	.L1694
	cmp	r2, #27
	bls	.L1688
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1688
.L1689:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1696
	ldr	r3, .L1782+24
	cmp	r0, #19
	str	r3, [r6, #1192]
	ite	ne
	movne	r3, #7
	moveq	r3, #15
	strb	r3, [r6, #1209]
	b	.L1688
.L1696:
	cmp	r0, #33
	beq	.L1698
	sub	r3, r0, #65
	cmp	r3, #1
	bhi	.L1699
.L1698:
	ldr	r3, .L1782+28
	str	r3, [r8, #1192]
	movs	r3, #4
	strb	r3, [r4, #1209]
	movs	r3, #7
	strb	r3, [r8, #1209]
	b	.L1688
.L1699:
	sub	r3, r0, #67
	uxtb	r3, r3
	cmp	r3, #1
	bls	.L1700
	sub	r2, r0, #34
	cmp	r2, #1
	bhi	.L1701
.L1700:
	ldr	r2, .L1782+28
	cmp	r0, #35
	str	r2, [r8, #1192]
	beq	.L1702
	cmp	r0, #68
	beq	.L1702
	ldr	r2, .L1782+12
	movs	r1, #7
	strb	r1, [r2, #1209]
	b	.L1703
.L1702:
	movs	r2, #17
	strb	r2, [r8, #1209]
.L1703:
	cmp	r3, #1
	ite	ls
	movls	r3, #4
	movhi	r3, #5
	strb	r3, [r4, #1209]
	b	.L1688
.L1701:
	cmp	r0, #49
	bne	.L1705
	ldr	r3, .L1782+32
	str	r3, [r6, #1192]
	b	.L1688
.L1705:
	cmp	r0, #50
	ittt	eq
	streq	r9, [r7, #2248]
	ldreq	r3, .L1782+36
	streq	r3, [r6, #1192]
.L1688:
	ldr	r2, [r4, #2264]
	ldr	r3, .L1782
	cmp	r2, r5
	bne	.L1706
	ldrb	r1, [r3, #144]	@ zero_extendqisi2
	cbz	r1, .L1706
	ldr	r3, [r3, #44]
	movs	r1, #0
	strb	r1, [r3, #18]
.L1706:
	ldrb	r1, [r4, #2068]	@ zero_extendqisi2
	ldr	r3, .L1782
	cmp	r1, #44
	bne	.L1707
	ldrb	r1, [r3, #2252]	@ zero_extendqisi2
	cbz	r1, .L1707
	cmp	r2, r5
	bne	.L1708
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cbnz	r3, .L1707
.L1708:
	movs	r0, #1
	movs	r3, #0
	strb	r3, [r4, #2252]
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
.L1707:
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1709
	ldr	r2, .L1782
	ldrb	r2, [r2, #2252]	@ zero_extendqisi2
	cbnz	r2, .L1710
	lsls	r3, r3, #31
	bmi	.L1709
.L1710:
	movs	r0, #0
	bl	flash_enter_slc_mode
	movs	r0, #0
	ldr	r1, [r8, #1200]
	bl	FlashDdrParaScan
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1709:
	ldr	r3, [r4, #44]
	movs	r7, #16
	ldr	r6, .L1782
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1782+40
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1782+44
	strb	r7, [r4, #1]
	bl	FlashTimingCfg
	ldr	r5, [r4, #44]
	ldrb	r2, [r4, #2069]	@ zero_extendqisi2
	ldrb	r3, [r5, #12]	@ zero_extendqisi2
	ldrh	r8, [r5, #14]
	strh	r3, [r4, #124]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	str	r3, [r4, #120]
	lsl	r3, r2, r7
	orr	r2, r3, r2, lsl #8
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	orrs	r3, r3, r2
	ldrb	r2, [r4, #2071]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #24
	str	r3, [r4, #116]
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	strh	r3, [r4, #126]	@ movhi
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	strh	r8, [r4, #130]	@ movhi
	strh	r3, [r4, #128]	@ movhi
	ldrh	r3, [r5, #10]
	strh	r3, [r4, #132]	@ movhi
	ldrb	r1, [r5, #12]	@ zero_extendqisi2
	ldrh	r0, [r5, #10]
	bl	__aeabi_idiv
	strh	r0, [r4, #134]	@ movhi
	ldrb	r2, [r5, #9]	@ zero_extendqisi2
	strh	r2, [r4, #136]	@ movhi
	ldrb	r1, [r5, #9]	@ zero_extendqisi2
	ldrh	r3, [r5, #10]
	smulbb	r3, r1, r3
	mov	r1, #512
	strh	r1, [r4, #140]	@ movhi
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	strh	r1, [r4, #142]	@ movhi
	uxth	r3, r3
	ldrb	r1, [r4]	@ zero_extendqisi2
	strh	r3, [r4, #138]	@ movhi
	cmp	r1, #1
	bne	.L1712
	lsls	r3, r3, #1
	lsr	r1, r8, #1
	lsls	r2, r2, #1
	strh	r3, [r6, #138]	@ movhi
	strb	r7, [r6, #1]
	movs	r3, #8
	strh	r1, [r6, #130]	@ movhi
	strh	r2, [r6, #136]	@ movhi
	strh	r3, [r6, #142]	@ movhi
.L1712:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	movs	r0, #0
	b	.L1736
.L1716:
	mvn	r0, #1
	b	.L1736
.L1691:
	cmp	r0, #7
.L1779:
	beq	.L1717
	ldr	r3, .L1782+48
	b	.L1693
.L1670:
	strb	r1, [r2, #1228]
	b	.L1669
.L1736:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1783:
	.align	2
.L1782:
	.word	.LANCHOR0
	.word	.LC98
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	HynixReadRetrial
	.word	.LANCHOR0+1238
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	samsung_read_retrial
	.word	.LANCHOR2+3476
	.word	150000
	.word	.LANCHOR0+1230
	.fnend
	.size	FlashInit, .-FlashInit
	.align	1
	.global	FlashPageProgMsbFFData
	.thumb
	.thumb_func
	.type	FlashPageProgMsbFFData, %function
FlashPageProgMsbFFData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, r2
	ldr	r2, .L1805
	mov	r8, r1
	mov	r7, r0
	ldr	r3, [r2, #44]
	mov	r5, r2
	ldrb	r1, [r2, #144]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cbz	r1, .L1785
	ldr	r2, [r2, #2248]
	cbnz	r2, .L1784
.L1785:
	subs	r2, r3, #5
	cmp	r2, #2
	bls	.L1786
	cmp	r3, #50
	beq	.L1786
	cmp	r3, #68
	beq	.L1786
	cmp	r3, #35
	beq	.L1786
	cmp	r3, #19
	bne	.L1784
.L1786:
	ldr	r9, .L1805+8
	movw	r10, #65535
	ldr	r6, .L1805+4
.L1788:
	ldr	r3, [r5, #44]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1784
	add	r3, r9, r4, lsl #1
	ldrh	r3, [r3, #1220]
	cmp	r3, r10
	bne	.L1784
	movs	r1, #255
	mov	r2, #32768
	ldr	r0, [r6, #1212]
	bl	ftl_memset
	ldr	r2, [r6, #1212]
	add	r1, r4, r8
	adds	r4, r4, #1
	mov	r0, r7
	mov	r3, r2
	uxth	r4, r4
	bl	FlashProgPage
	b	.L1788
.L1784:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1806:
	.align	2
.L1805:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	1
	.global	FlashReadSlc2KPages
	.thumb
	.thumb_func
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1857
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r7, #0
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	.pad #36
	sub	sp, sp, #36
	mov	r4, r0
	str	r1, [sp, #16]
	str	r2, [sp, #20]
	str	r3, [sp, #12]
.L1808:
	ldr	r3, [sp, #16]
	cmp	r7, r3
	beq	.L1856
	ldr	r3, [sp, #16]
	add	r2, sp, #28
	mov	r0, r4
	ldr	r1, [sp, #20]
	subs	r3, r3, r7
	ldr	r8, .L1857+20
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	mov	fp, r8
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2230]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	bcc	.L1809
	mov	r3, #-1
	str	r3, [r4]
	b	.L1810
.L1809:
	add	r3, r3, r8
	mov	r9, #0
	ldrb	r6, [r3, #2232]	@ zero_extendqisi2
	mov	r0, r6
	bl	NandcWaitFlashReady
	mov	r0, r6
	bl	NandcFlashCs
.L1811:
	ldr	r1, [sp, #28]
	mov	r0, r6
	bl	FlashReadCmd
	mov	r0, r6
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #12]
	mov	r0, r6
	movs	r1, #0
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r4, #8]
	bl	NandcXferData
	ldr	r3, .L1857+4
	ldrb	r3, [r3, #1228]	@ zero_extendqisi2
	mov	r5, r0
	cbnz	r3, .L1812
.L1815:
	adds	r3, r5, #1
	beq	.L1813
	b	.L1814
.L1812:
	mov	r0, r6
	bl	flash_read_ecc
	cmp	r0, #5
	bls	.L1815
	mov	r5, #256
	b	.L1814
.L1813:
	cmp	r9, #10
	beq	.L1834
	add	r9, r9, #1
	b	.L1811
.L1814:
	cmp	r9, #0
	it	ne
	movne	r5, #256
	b	.L1816
.L1834:
	mov	r5, #256
.L1816:
	mov	r9, #0
.L1817:
	ldr	r3, [sp, #28]
	mov	r0, r6
	ldr	r1, [fp, #4]
	add	r1, r1, r3
	bl	FlashReadCmd
	mov	r0, r6
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #8]
	cbz	r3, .L1818
	add	r3, r3, #2048
.L1818:
	ldr	r2, [r4, #12]
	cbz	r2, .L1819
	adds	r2, r2, #8
.L1819:
	str	r2, [sp]
	mov	r0, r6
	ldr	r2, [sp, #12]
	movs	r1, #0
	bl	NandcXferData
	ldr	r3, .L1857+4
	ldrb	r2, [r3, #1228]	@ zero_extendqisi2
	mov	r10, r0
	cbnz	r2, .L1820
.L1823:
	cmp	r10, #-1
	beq	.L1821
	b	.L1822
.L1820:
	mov	r0, r6
	bl	flash_read_ecc
	cmp	r0, #5
	bls	.L1823
	mov	r10, #256
	b	.L1822
.L1821:
	cmp	r9, #10
	beq	.L1838
	add	r9, r9, #1
	b	.L1817
.L1822:
	cmp	r9, #0
	it	ne
	movne	r10, #256
	b	.L1824
.L1838:
	mov	r10, #256
.L1824:
	mov	r0, r6
	bl	NandcFlashDeCs
	ldrb	r3, [r8, #2312]	@ zero_extendqisi2
	cmp	r5, r10
	add	r3, r3, r3, lsl #1
	it	cc
	movcc	r5, r10
	cmp	r5, r3, asr #2
	bls	.L1825
	adds	r0, r5, #1
	it	ne
	movne	r5, #256
	str	r5, [r4]
	b	.L1827
.L1825:
	movs	r3, #0
	str	r3, [r4]
.L1827:
	ldr	r3, [r4, #12]
	cbz	r3, .L1828
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L1828
	ldr	r2, [r3, #8]
	adds	r1, r2, #1
	bne	.L1828
	ldr	r3, [r3]
	adds	r3, r3, #1
	it	ne
	strne	r2, [r4]
.L1828:
	ldr	r3, [r4]
	adds	r2, r3, #1
	bne	.L1810
	ldr	r1, [r4, #4]
	ldr	r0, .L1857+8
	ldrb	r2, [r8, #2312]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cbz	r1, .L1830
	ldr	r0, .L1857+12
	movs	r2, #4
	movs	r3, #8
	bl	rknand_print_hex
.L1830:
	ldr	r1, [r4, #12]
	cbz	r1, .L1810
	movs	r2, #4
	ldr	r0, .L1857+16
	mov	r3, r2
	bl	rknand_print_hex
.L1810:
	adds	r7, r7, #1
	adds	r4, r4, #36
	b	.L1808
.L1856:
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1858:
	.align	2
.L1857:
	.word	.LANCHOR1
	.word	.LANCHOR4
	.word	.LC99
	.word	.LC100
	.word	.LC101
	.word	.LANCHOR0
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	1
	.global	FlashReadPages
	.thumb
	.thumb_func
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1958
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r5, .L1958+4
	str	r1, [sp, #28]
	str	r3, [sp, #24]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	str	r2, [sp, #16]
	str	r3, [sp, #32]
	ldrb	r3, [r5]	@ zero_extendqisi2
	cbz	r3, .L1902
	bl	FlashReadSlc2KPages
	b	.L1953
.L1902:
	mov	r8, r3
	str	r3, [sp, #12]
.L1860:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	cmp	r3, r2
	bcs	.L1955
	ldr	r3, [sp, #12]
	mov	r9, #36
	ldr	r2, [sp, #12]
	ldr	r1, [sp, #16]
	mul	r9, r9, r3
	ldr	r3, [sp, #28]
	subs	r3, r3, r2
	add	r2, sp, #44
	add	r7, r10, r9
	uxtb	r3, r3
	ldr	fp, [r7, #4]
	mov	r0, r7
	str	r3, [sp]
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldrb	r2, [r5, #2230]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	mov	r6, r0
	cmp	r3, r2
	bcc	.L1862
	mov	r3, #-1
	str	r3, [r10, r9]
	b	.L1863
.L1862:
	add	r3, r3, r5
	ldrb	r4, [r3, #2232]	@ zero_extendqisi2
	ldr	r3, .L1958+8
	mov	r0, r4
	ldrb	r3, [r3, #1229]	@ zero_extendqisi2
	cmp	r3, #0
	it	eq
	moveq	r6, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1958+4
	ldr	r3, [r3, #44]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	subs	r3, r2, #1
	cmp	r3, #7
	bhi	.L1865
	subs	r2, r2, #7
	adds	r1, r5, r4
	cmp	r2, #1
	ldr	r2, .L1958+4
	ldrb	r3, [r1, #1222]	@ zero_extendqisi2
	add	r2, r2, r4
	it	ls
	ldrbls	r3, [r1, #1230]	@ zero_extendqisi2
	ldrb	r2, [r2, #2064]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1865
	mov	r0, r4
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	ldr	r2, .L1958+12
	bl	HynixSetRRPara
.L1865:
	mov	r0, r4
	and	r3, fp, #-2147483648
	str	r3, [sp, #20]
	bl	NandcFlashCs
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1867
	ldr	r3, [sp, #20]
	cbz	r3, .L1868
.L1867:
	ldr	r3, .L1958+4
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cbz	r3, .L1868
	mov	r0, r4
	bl	flash_enter_slc_mode
.L1870:
	mov	fp, #0
	b	.L1869
.L1868:
	mov	r0, r4
	bl	flash_exit_slc_mode
	b	.L1870
.L1956:
	cmp	r4, #255
	beq	.L1903
.L1871:
	cbz	r6, .L1873
	ldr	r2, [r5, #4]
	mov	r0, r4
	add	r2, r2, r1
	bl	FlashReadDpCmd
.L1874:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cbz	r6, .L1872
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1872:
	ldr	r3, [r7, #12]
	mov	r0, r4
	movs	r1, #0
	ldr	r2, [sp, #24]
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	ip, r0
	cbz	r3, .L1905
	cmp	r0, #-1
	bne	.L1905
	movs	r6, #0
	strb	fp, [r5, #8]
.L1869:
	ldr	r1, [sp, #44]
	adds	r0, r1, #1
	bne	.L1871
	b	.L1956
.L1873:
	mov	r0, r4
	bl	FlashReadCmd
	b	.L1874
.L1903:
	movs	r6, #0
	b	.L1872
.L1905:
	mov	fp, ip
	cbz	r6, .L1876
	ldr	r3, .L1958+4
	mov	r0, r4
	str	ip, [sp, #36]
	ldr	r1, [r3, #4]
	ldr	r3, [sp, #44]
	add	r1, r1, r3
	bl	FlashReadDpDataOutCmd
	add	r3, r9, #36
	add	r3, r3, r10
	mov	r0, r4
	movs	r1, #0
	ldr	r2, [r3, #12]
	str	r2, [sp]
	ldr	r2, [sp, #24]
	ldr	r3, [r3, #8]
	bl	NandcXferData
	cmp	r0, #-1
	ldr	ip, [sp, #36]
	it	eq
	moveq	r6, #0
	mov	r8, r0
.L1876:
	mov	r0, r4
	str	ip, [sp, #36]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	ldrb	r3, [sp, #32]	@ zero_extendqisi2
	cmp	ip, #-1
	strb	r3, [r5, #8]
	bne	.L1883
	ldrb	r3, [r5, #2252]	@ zero_extendqisi2
	cbnz	r3, .L1878
.L1882:
	ldr	r3, .L1958+8
	ldr	r6, [r3, #1192]
	cbnz	r6, .L1879
	b	.L1957
.L1878:
	ldr	r3, [r5, #80]
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r6, [r3, #304]
	movs	r3, #1
	str	r3, [sp]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashDdrTunningRead
	cmp	r0, #-1
	mov	fp, r0
	beq	.L1881
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1907
.L1881:
	ubfx	r0, r6, #8, #8
	bl	NandcSetDdrPara
	cmp	fp, #-1
	beq	.L1882
	b	.L1907
.L1879:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r6
	cmp	r0, #-1
	mov	fp, r0
	bne	.L1884
	ldr	r3, [r5, #44]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #1
	cmp	r3, #7
	bhi	.L1885
	mov	r0, r4
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	ldr	r2, .L1958+12
	movs	r3, #0
	bl	HynixSetRRPara
.L1885:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r5, #2312]	@ zero_extendqisi2
	mov	fp, r0
	ldr	r0, .L1958+16
	mov	r3, fp
	bl	printk
	cmp	fp, #-1
	bne	.L1884
	ldrb	r6, [r5, #144]	@ zero_extendqisi2
	cbz	r6, .L1886
	ldr	r3, [sp, #16]
	mov	r0, r4
	cmp	r3, #1
	beq	.L1887
	ldr	r3, [sp, #20]
	cbz	r3, .L1888
.L1887:
	bl	flash_enter_slc_mode
	b	.L1889
.L1888:
	bl	flash_exit_slc_mode
.L1889:
	ldr	r3, .L1958+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r6, [r3, #1192]
	ldr	r3, [r7, #12]
	blx	r6
	movs	r6, #0
	mov	fp, r0
	b	.L1890
.L1957:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	fp, r0
	b	.L1890
.L1907:
	movs	r6, #0
.L1883:
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	fp, r3, asr #2
	bls	.L1892
	ldr	r3, .L1958+8
	ldr	r3, [r3, #1192]
	cbz	r3, .L1909
.L1890:
	cmp	fp, #-1
	beq	.L1886
	b	.L1892
.L1909:
	mov	fp, #256
.L1886:
	str	fp, [r10, r9]
	b	.L1893
.L1959:
	.align	2
.L1958:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR0+1214
	.word	.LC102
.L1884:
	movs	r6, #0
.L1892:
	cmp	fp, #256
	beq	.L1886
	movs	r3, #0
	str	r3, [r10, r9]
.L1893:
	ldr	r3, [r10, r9]
	adds	r2, r3, #1
	bne	.L1895
	ldr	r1, [r7, #4]
	ldr	r0, .L1960
	ldrb	r2, [r5, #2312]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cbz	r1, .L1895
	movs	r2, #4
	ldr	r0, .L1960+4
	mov	r3, r2
	bl	rknand_print_hex
.L1895:
	cbz	r6, .L1897
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r8, r3, asr #2
	bls	.L1898
	ldr	r3, .L1960+8
	ldr	r3, [r3, #1192]
	cmp	r3, #0
	it	eq
	moveq	r8, #256
.L1898:
	add	r9, r9, #36
	cmp	r8, #-1
	str	r8, [r10, r9]
	beq	.L1897
	cmp	r8, #256
	itt	ne
	movne	r3, #0
	strne	r3, [r10, r9]
.L1897:
	ldr	r3, [sp, #12]
	add	r3, r3, r6
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1899
	ldr	r3, [sp, #20]
	cbz	r3, .L1863
.L1899:
	ldrb	r3, [r5, #144]	@ zero_extendqisi2
	cbz	r3, .L1863
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1863:
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	str	r3, [sp, #12]
	b	.L1860
.L1955:
	movs	r0, #0
.L1953:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1961:
	.align	2
.L1960:
	.word	.LC99
	.word	.LC101
	.word	.LANCHOR4
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	1
	.global	FlashLoadFactorBbt
	.thumb
	.thumb_func
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r1, #0
	ldr	r7, .L1974
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L1974+4
	movs	r2, #16
	movs	r5, #0
	mov	fp, #-1
	ldrh	r3, [r7, #130]
	add	r0, r4, #1232
	ldrh	r6, [r7, #128]
	mov	r10, r5
	mov	r8, r7
	mov	r9, r4
	smulbb	r6, r6, r3
	bl	ftl_memset
	ldr	r3, [r4, #1216]
	uxth	r6, r6
	str	r5, [sp, #20]
	str	r3, [sp, #24]
.L1963:
	ldrb	r3, [r8, #2230]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1973
	mul	ip, r6, r7
	subs	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1964:
	cmp	r4, r3
	ble	.L1966
	add	r2, ip, r4
	movs	r1, #1
	add	r0, sp, #12
	str	r3, [sp, #4]
	lsls	r2, r2, #10
	str	r2, [sp, #16]
	mov	r2, r1
	str	ip, [sp]
	bl	FlashReadPages
	ldr	r2, [sp, #12]
	ldr	ip, [sp]
	adds	r2, r2, #1
	ldr	r3, [sp, #4]
	beq	.L1965
	ldr	r2, [r9, #1216]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1965
	mov	r1, r7
	ldr	r0, .L1974+8
	mov	r2, r4
	add	r10, r10, #1
	bl	printk
	ldr	r3, .L1974+4
	uxth	r10, r10
	add	r7, r3, r7, lsl #1
	strh	r4, [r7, #1232]	@ movhi
	b	.L1966
.L1965:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1964
.L1966:
	ldrb	r3, [r8, #2230]	@ zero_extendqisi2
	adds	r5, r5, #1
	cmp	r3, r10
	it	eq
	moveq	fp, #0
	b	.L1963
.L1973:
	mov	r0, fp
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1975:
	.align	2
.L1974:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC103
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	1
	.global	FlashProgSlc2KPages
	.thumb
	.thumb_func
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2005
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r6, #0
	ldr	r8, .L2005+24
	.pad #68
	sub	sp, sp, #68
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	mov	r7, r1
	mov	r9, r2
	mov	r4, r0
	mov	r10, r0
	mov	fp, r8
	str	r3, [sp, #12]
.L1977:
	cmp	r6, r7
	beq	.L2003
	subs	r3, r7, r6
	add	r2, sp, #20
	mov	r0, r10
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2230]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	bcc	.L1978
	mov	r3, #-1
	str	r3, [r10]
	b	.L1979
.L1978:
	add	r3, r3, r8
	ldrb	r5, [r3, #2232]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r1, [sp, #20]
	bl	FlashProgFirstCmd
	ldr	r3, [r10, #12]
	movs	r1, #1
	mov	r0, r5
	ldr	r2, [sp, #12]
	str	r3, [sp]
	ldr	r3, [r10, #8]
	bl	NandcXferData
	mov	r0, r5
	ldr	r1, [sp, #20]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	ldr	r1, [sp, #20]
	bl	FlashReadStatus
	ldr	r3, [sp, #20]
	sbfx	r0, r0, #0, #1
	str	r0, [r10]
	mov	r0, r5
	ldr	r1, [r8, #4]
	add	r1, r1, r3
	bl	FlashProgFirstCmd
	ldr	r3, [r10, #8]
	cbz	r3, .L1980
	add	r3, r3, #2048
.L1980:
	ldr	r2, [r10, #12]
	cbz	r2, .L1981
	adds	r2, r2, #8
.L1981:
	str	r2, [sp]
	movs	r1, #1
	ldr	r2, [sp, #12]
	mov	r0, r5
	bl	NandcXferData
	ldr	r1, [fp, #4]
	mov	r0, r5
	ldr	r3, [sp, #20]
	add	r1, r1, r3
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	ldr	r1, [sp, #20]
	bl	FlashReadStatus
	lsls	r2, r0, #31
	mov	r0, r5
	itt	mi
	movmi	r3, #-1
	strmi	r3, [r10]
	bl	NandcFlashDeCs
.L1979:
	adds	r6, r6, #1
	add	r10, r10, #36
	b	.L1977
.L2003:
	ldr	r8, .L2005+28
	mov	r10, #0
	mov	fp, r8
.L1984:
	cmp	r10, r7
	beq	.L2004
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L1985
	ldr	r1, [r4, #4]
	ldr	r0, .L2005+4
	bl	printk
	b	.L1986
.L1985:
	rsb	r3, r10, r7
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	mov	r6, r4
	bl	LogAddr2PhyAddr
	ldr	r2, [r8, #1220]
	movs	r3, #0
	str	r3, [r2]
	ldr	r2, [r8, #1224]
	str	r3, [r2]
	ldmia	r6!, {r0, r1, r2, r3}
	add	r5, sp, #28
	stmia	r5!, {r0, r1, r2, r3}
	ldmia	r6!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	add	r0, sp, #28
	ldr	r3, [r6]
	movs	r1, #1
	mov	r2, r9
	str	r3, [r5]
	ldr	r3, [r8, #1220]
	str	r3, [sp, #36]
	ldr	r3, [r8, #1224]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r5, [sp, #28]
	adds	r3, r5, #1
	bne	.L1987
	ldr	r0, .L2005+8
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L1987:
	ldr	r5, [sp, #28]
	cmp	r5, #256
	bne	.L1988
	ldr	r0, .L2005+12
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L1988:
	ldr	r3, [r4, #12]
	cbz	r3, .L1989
	ldr	r2, [r3]
	ldr	r3, [fp, #1224]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1989
	ldr	r0, .L2005+16
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1989:
	ldr	r3, [r4, #8]
	cbz	r3, .L1986
	ldr	r2, [r3]
	ldr	r3, [fp, #1220]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1986
	ldr	r0, .L2005+20
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1986:
	add	r10, r10, #1
	adds	r4, r4, #36
	b	.L1984
.L2004:
	movs	r0, #0
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2006:
	.align	2
.L2005:
	.word	.LANCHOR1
	.word	.LC104
	.word	.LC105
	.word	.LC106
	.word	.LC107
	.word	.LC108
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	1
	.global	FtlLoadFactoryBbt
	.thumb
	.thumb_func
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r6, #0
	ldr	r3, .L2018
	ldr	r7, .L2018+4
	ldr	r5, .L2018+8
	ldr	r2, [r3, #3316]
	subw	r8, r7, #2462
	ldr	r9, [r3, #3340]
	mov	r10, r8
	str	r2, [r5, #1256]
	str	r9, [r5, #1260]
.L2008:
	ldrh	r3, [r8, #2342]
	cmp	r6, r3
	bcs	.L2017
	ldrh	r4, [r8, #2386]
	movw	fp, #61664
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
	subs	r4, r4, #1
	uxth	r4, r4
.L2009:
	ldrh	r3, [r10, #2386]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L2011
	mla	r3, r3, r6, r4
	movs	r1, #1
	ldr	r0, .L2018+12
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [r5, #1252]
	bl	FlashReadPages
	ldr	r3, [r5, #1248]
	adds	r3, r3, #1
	beq	.L2010
	ldrh	r3, [r9]
	cmp	r3, fp
	bne	.L2010
	strh	r4, [r7]	@ movhi
	b	.L2011
.L2010:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L2009
.L2011:
	adds	r6, r6, #1
	b	.L2008
.L2017:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2019:
	.align	2
.L2018:
	.word	.LANCHOR2
	.word	.LANCHOR0+2462
	.word	.LANCHOR4
	.word	.LANCHOR4+1248
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	1
	.global	FtlGetLastWrittenPage
	.thumb
	.thumb_func
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2031
	cmp	r1, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #104
	sub	sp, sp, #104
	it	eq
	ldrheq	r4, [r3, #2390]
	mov	r8, r1
	it	ne
	ldrhne	r4, [r3, #2388]
	lsls	r6, r0, #10
	add	r3, sp, #40
	add	r0, sp, #4
	subs	r5, r4, #1
	str	r3, [sp, #16]
	movs	r1, #1
	mov	r2, r8
	uxth	r5, r5
	movs	r7, #0
	str	r7, [sp, #12]
	sxth	r3, r5
	orrs	r3, r3, r6
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	adds	r3, r3, #1
	bne	.L2023
.L2024:
	sxth	r3, r7
	sxth	r4, r5
	cmp	r3, r4
	bgt	.L2023
	add	r4, r4, r3
	add	r0, sp, #4
	movs	r1, #1
	mov	r2, r8
	add	r4, r4, r4, lsr #31
	asrs	r4, r4, #1
	sxth	r3, r4
	orrs	r3, r3, r6
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	adds	r3, r3, #1
	bne	.L2025
	ldr	r3, [sp, #44]
	adds	r3, r3, #1
	bne	.L2025
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L2025
	subs	r4, r4, #1
	uxth	r5, r4
	b	.L2024
.L2025:
	adds	r3, r4, #1
	uxth	r7, r3
	b	.L2024
.L2023:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2032:
	.align	2
.L2031:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	1
	.global	FtlLoadBbt
	.thumb
	.thumb_func
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	ldr	r8, .L2062+16
	ldr	r4, .L2062
	ldr	r5, .L2062+4
	ldr	r3, [r8, #3316]
	ldr	r7, [r8, #3340]
	mov	r9, r4
	str	r3, [r4, #1256]
	str	r7, [r4, #1260]
	bl	FtlBbtMemInit
	ldrh	r6, [r5, #2386]
	subs	r6, r6, #1
	uxth	r6, r6
.L2034:
	ldrh	r3, [r5, #2386]
	subs	r3, r3, #48
	cmp	r6, r3
	ble	.L2037
	movs	r1, #1
	ldr	r0, .L2062+8
	lsls	r3, r6, #10
	str	r3, [r4, #1252]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #1248]
	adds	r3, r3, #1
	bne	.L2035
	movs	r1, #1
	ldr	r3, [r9, #1252]
	ldr	r0, .L2062+8
	mov	r2, r1
	adds	r3, r3, #1
	str	r3, [r9, #1252]
	bl	FlashReadPages
.L2035:
	ldr	r3, [r4, #1248]
	adds	r3, r3, #1
	beq	.L2036
	ldrh	r2, [r7]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2036
	ldr	r3, [r7, #4]
	strh	r6, [r5, #2452]	@ movhi
	str	r3, [r5, #2460]
	ldrh	r3, [r7, #8]
	strh	r3, [r5, #2456]	@ movhi
	b	.L2037
.L2036:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L2034
.L2037:
	ldrh	r3, [r5, #2452]
	movw	r2, #65535
	ldr	r6, .L2062+4
	cmp	r3, r2
	beq	.L2051
	ldrh	r3, [r6, #2456]
	cmp	r3, r2
	beq	.L2041
	movs	r1, #1
	ldr	r0, .L2062+8
	lsls	r3, r3, #10
	str	r3, [r4, #1252]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #1248]
	adds	r3, r3, #1
	beq	.L2041
	ldrh	r2, [r7]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2041
	ldr	r3, [r7, #4]
	ldr	r2, [r6, #2460]
	cmp	r3, r2
	itttt	hi
	strhi	r3, [r6, #2460]
	ldrhhi	r2, [r6, #2456]
	ldrhhi	r3, [r7, #8]
	strhhi	r2, [r6, #2452]	@ movhi
	it	hi
	strhhi	r3, [r6, #2456]	@ movhi
.L2041:
	ldrh	r0, [r5, #2452]
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r9, #61649
	uxth	r6, r0
	adds	r0, r0, #1
	strh	r0, [r5, #2454]	@ movhi
.L2043:
	sxth	r3, r6
	cmp	r3, #0
	blt	.L2048
	ldrh	r2, [r5, #2452]
	movs	r1, #1
	ldr	r0, .L2062+8
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #1252]
	ldr	r3, [r8, #3316]
	mov	r2, r1
	str	r3, [r4, #1256]
	bl	FlashReadPages
	ldr	r3, [r4, #1248]
	adds	r3, r3, #1
	beq	.L2044
	ldrh	r3, [r7]
	cmp	r3, r9
	bne	.L2044
.L2048:
	ldrh	r2, [r7, #10]
	ldrh	r0, [r7, #12]
	ldr	r3, .L2062+4
	strh	r2, [r5, #2458]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L2045
	b	.L2046
.L2044:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L2043
.L2045:
	ldr	r2, [r3, #2316]
	cmp	r0, r2
	beq	.L2046
	ldrh	r3, [r3, #2330]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L2046
	cmp	r0, r3
	bcs	.L2046
	bl	FtlSysBlkNumInit
.L2046:
	ldr	r7, .L2062+12
	movs	r6, #0
.L2049:
	ldrh	r3, [r5, #2342]
	cmp	r6, r3
	bcs	.L2061
	ldrh	r2, [r8, #3404]
	ldr	r1, [r4, #1256]
	ldr	r0, [r7, #4]!
	lsls	r2, r2, #2
	mla	r1, r6, r2, r1
	bl	ftl_memcpy
	adds	r6, r6, #1
	b	.L2049
.L2061:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L2051:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L2063:
	.align	2
.L2062:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR4+1248
	.word	.LANCHOR0+2476
	.word	.LANCHOR2
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	1
	.global	FtlScanSysBlk
	.thumb
	.thumb_func
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r6, #0
	ldr	r5, .L2149
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L2149+4
	mov	r1, r6
	ldr	r2, [r5, #2416]
	ldr	r0, [r4, #3396]
	mov	r9, r4
	strh	r6, [r5, #2434]	@ movhi
	lsls	r2, r2, #2
	strh	r6, [r4, #3452]	@ movhi
	bl	ftl_memset
	ldr	r2, [r5, #2416]
	mov	r1, r6
	ldr	r0, [r4, #3368]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2408]
	mov	r1, r6
	ldr	r0, [r4, #3380]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #2408]
	ldr	r0, [r5, #2436]
	mov	r1, r6
	lsls	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #540
	movs	r1, #255
	movs	r2, #16
	bl	ftl_memset
	ldrh	r3, [r5, #2328]
	str	r5, [sp, #12]
	mov	r5, r4
	str	r3, [sp, #4]
.L2065:
	ldr	r6, .L2149
	ldr	r2, [sp, #4]
	ldrh	r3, [r6, #2330]
	mov	r0, r6
	cmp	r3, r2
	bls	.L2107
	ldr	r1, [r4, #1148]
	mov	r10, #36
	ldrh	r8, [r6, #2320]
	movs	r7, #0
	ldr	r2, [r4, #3304]
	str	r1, [sp, #8]
	ldr	r3, [r4, #1144]
	ldrh	r1, [r6, #2400]
	str	r7, [sp]
	str	r1, [sp, #16]
	addw	r1, r6, #2348
	str	r1, [sp, #20]
.L2108:
	uxth	r1, r7
	cmp	r1, r8
	bcs	.L2144
	str	r3, [sp, #28]
	ldr	r3, [sp, #20]
	ldr	r1, [sp, #4]
	str	r2, [sp, #24]
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	V2P_block
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #28]
	cbnz	r0, .L2066
	ldr	r1, [sp]
	ldr	r6, [sp]
	mla	r0, r10, r1, r2
	lsl	r1, fp, #10
	str	r1, [r0, #4]
	ldr	r1, [sp, #16]
	muls	r1, r6, r1
	ldr	r6, [sp, #8]
	it	mi
	addmi	r1, r1, #3
	bic	r1, r1, #3
	str	r3, [r0, #8]
	add	r1, r1, r6
	str	r1, [r0, #12]
	ldr	r1, [sp]
	adds	r1, r1, #1
	uxth	r1, r1
	str	r1, [sp]
.L2066:
	adds	r7, r7, #1
	b	.L2108
.L2144:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L2070
	mov	r1, r3
	ldr	r0, [r4, #3304]
	movs	r2, #1
	ldr	r8, .L2149
	bl	FlashReadPages
	movs	r3, #0
	str	r3, [sp, #8]
.L2071:
	ldrh	r3, [sp, #8]
	ldr	r2, [sp]
	cmp	r3, r2
	bcs	.L2070
	ldr	r3, [sp, #8]
	mov	r10, #36
	mul	r10, r10, r3
	ldr	r3, [r4, #3304]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r6, [r2, #4]
	adds	r3, r3, #1
	ldr	r7, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L2073
	mov	fp, #16
	movw	r3, #65535
.L2072:
	ldr	r0, [r4, #3304]
	movs	r1, #1
	str	r3, [sp, #16]
	add	r0, r0, r10
	ldr	r2, [r0, #4]
	adds	r2, r2, #1
	str	r2, [r0, #4]
	mov	r2, r1
	bl	FlashReadPages
	ldrh	r2, [r7]
	ldr	r3, [sp, #16]
	cmp	r2, r3
	bne	.L2074
	ldr	r3, [r9, #3304]
	mov	r2, #-1
	str	r2, [r3, r10]
	b	.L2073
.L2074:
	ldr	r2, [r5, #3304]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L2073
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L2072
.L2073:
	ldr	r3, [r5, #3304]
	ldr	r3, [r3, r10]
	adds	r3, r3, #1
	beq	.L2076
	ldr	r2, [r5, #508]
	ldr	r3, [r7, #4]
	adds	r1, r2, #1
	beq	.L2077
	cmp	r2, r3
	bhi	.L2078
.L2077:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r9, #508]
.L2078:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2080
	bhi	.L2081
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2079
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #12]
	ldrh	r10, [r3, #2408]
	ldrh	r0, [r2, #2434]
	ldr	r2, [r9, #3380]
	add	lr, r10, #-1
	uxth	r3, lr
	rsb	lr, r0, lr
	str	r2, [sp, #16]
	b	.L2094
.L2081:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2083
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2079
	mov	r0, r6
	b	.L2143
.L2083:
	ldr	r3, [sp, #12]
	ldrh	r0, [r4, #3452]
	ldr	r2, [r4, #3396]
	ldr	r1, [r3, #2416]
	str	r2, [sp, #16]
	uxth	lr, r1
	add	r3, lr, #-1
	rsb	lr, r0, lr
	add	lr, lr, #-1
	uxth	r3, r3
	sxth	lr, lr
.L2085:
	sxth	ip, r3
	cmp	ip, lr
	ble	.L2145
	ldr	r2, [r7, #4]
	lsl	fp, ip, #2
	mov	r10, r2
	ldr	r2, [sp, #16]
	ldr	r2, [r2, ip, lsl #2]
	cmp	r10, r2
	bls	.L2086
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L2087
	cmp	r0, r1
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r9, #3452]	@ movhi
.L2087:
	movs	r1, #0
	uxth	lr, r3
.L2088:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2146
	ldr	r0, [r5, #3396]
	sxth	r2, r2
	adds	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	ldr	r0, [r5, #3368]
	add	r10, r0, r2, lsl #1
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2, lsl #1]	@ movhi
	b	.L2088
.L2146:
	ldr	r1, [r7, #4]
	ldr	r2, [r4, #3396]
	str	r1, [r2, fp]
	sxth	r1, r3
	ldr	r2, [r4, #3368]
	cmp	r1, #0
	strh	r6, [r2, ip, lsl #1]	@ movhi
	bge	.L2090
	b	.L2079
.L2086:
	subs	r3, r3, #1
	uxth	r3, r3
	b	.L2085
.L2145:
	cmp	ip, #0
	bge	.L2121
	b	.L2079
.L2090:
	ldrh	r0, [r5, #3452]
	ldr	r2, [r8, #2416]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r1, r2
	bgt	.L2079
.L2121:
	ldr	r2, [r4, #3396]
	sxth	r3, r3
	ldr	r1, [r7, #4]
	adds	r0, r0, #1
	strh	r0, [r4, #3452]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3368]
	b	.L2141
.L2100:
	ldr	r1, [sp, #16]
	lsl	fp, ip, #2
	ldr	r2, [r7, #4]
	ldr	r1, [r1, ip, lsl #2]
	cmp	r2, r1
	bhi	.L2147
	subs	r3, r3, #1
	uxth	r3, r3
.L2094:
	sxth	ip, r3
	cmp	ip, lr
	bgt	.L2100
	b	.L2099
.L2150:
	.align	2
.L2149:
	.word	.LANCHOR0
	.word	.LANCHOR2
.L2147:
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L2096
	cmp	r0, r10
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r8, #2434]	@ movhi
.L2096:
	movs	r1, #0
	uxth	lr, r3
.L2097:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2148
	ldr	r0, [r4, #3380]
	sxth	r2, r2
	adds	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	ldr	r0, [r8, #2436]
	add	r10, r0, r2, lsl #1
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2, lsl #1]	@ movhi
	b	.L2097
.L2148:
	ldr	r2, [r5, #3380]
	ldr	r1, [r7, #4]
	str	r1, [r2, fp]
	ldr	r2, .L2151
	ldr	r2, [r2, #2436]
	strh	r6, [r2, ip, lsl #1]	@ movhi
.L2099:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2079
	ldrh	r2, [r8, #2408]
	ldrh	r1, [r8, #2434]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2079
	adds	r1, r1, #1
	ldr	r2, [r9, #3380]
	strh	r1, [r8, #2434]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r8, #2436]
.L2141:
	strh	r6, [r2, r3, lsl #1]	@ movhi
	b	.L2079
.L2080:
	ldrh	r1, [r9, #540]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2101
	strh	r6, [r5, #540]	@ movhi
	b	.L2142
.L2101:
	ldrh	r0, [r5, #544]
	cmp	r0, r2
	beq	.L2102
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2102:
	ldr	r2, [r9, #548]
	ldr	r3, [r7, #4]
	cmp	r2, r3
	bcs	.L2103
	ldrh	r3, [r5, #540]
	strh	r6, [r5, #540]	@ movhi
	strh	r3, [r5, #544]	@ movhi
	ldr	r3, [r7, #4]
.L2142:
	str	r3, [r5, #548]
	b	.L2079
.L2103:
	strh	r6, [r5, #544]	@ movhi
	b	.L2079
.L2076:
	ldrb	r1, [r8, #144]	@ zero_extendqisi2
	mov	r0, r6
	cbz	r1, .L2104
.L2143:
	movs	r1, #0
.L2104:
	bl	FtlFreeSysBlkQueueIn
.L2079:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2071
.L2070:
	ldr	r6, [sp, #4]
	adds	r6, r6, #1
	uxth	r3, r6
	str	r3, [sp, #4]
	b	.L2065
.L2107:
	ldr	r3, .L2151+4
	ldr	r5, [r3, #3368]
	ldrh	r2, [r5]
	cbz	r2, .L2109
.L2112:
	ldr	r5, [r0, #2436]
	ldr	r3, .L2151
	ldrh	r2, [r5]
	cbz	r2, .L2110
	b	.L2133
.L2109:
	ldrh	r3, [r3, #3452]
	cmp	r3, #0
	beq	.L2112
	ldr	r6, [r6, #2416]
.L2113:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bcs	.L2112
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L2113
	mov	lr, #0
.L2114:
	ldr	r5, [r0, #2416]
	sxth	r2, r3
	cmp	r2, r5
	bcs	.L2112
	ldr	r5, [r4, #3368]
	subs	r6, r2, r1
	adds	r3, r3, #1
	ldrh	r7, [r5, r2, lsl #1]
	uxth	r3, r3
	strh	r7, [r5, r6, lsl #1]	@ movhi
	ldr	r5, [r4, #3396]
	ldr	r7, [r5, r2, lsl #2]
	str	r7, [r5, r6, lsl #2]
	ldr	r5, [r4, #3368]
	strh	lr, [r5, r2, lsl #1]	@ movhi
	b	.L2114
.L2110:
	ldrh	r1, [r3, #2434]
	cbz	r1, .L2133
	ldrh	r6, [r3, #2408]
.L2117:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bge	.L2133
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L2117
	ldr	r5, .L2151
	mov	ip, #0
.L2118:
	ldrh	r6, [r0, #2408]
	sxth	r2, r3
	cmp	r2, r6
	bge	.L2133
	ldr	r6, [r5, #2436]
	rsb	lr, r1, r2
	adds	r3, r3, #1
	ldrh	r7, [r6, r2, lsl #1]
	uxth	r3, r3
	strh	r7, [r6, lr, lsl #1]	@ movhi
	ldr	r6, [r4, #3380]
	ldr	r7, [r6, r2, lsl #2]
	str	r7, [r6, lr, lsl #2]
	ldr	r6, [r5, #2436]
	strh	ip, [r6, r2, lsl #1]	@ movhi
	b	.L2118
.L2133:
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2152:
	.align	2
.L2151:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	1
	.global	FtlLoadSysInfo
	.thumb
	.thumb_func
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r1, #0
	ldr	r4, .L2181
	.pad #44
	sub	sp, sp, #44
	ldr	r7, .L2181+4
	ldr	r5, .L2181+8
	ldr	r3, [r4, #3316]
	ldrh	r2, [r7, #2328]
	ldr	r0, [r4, #296]
	str	r3, [r5, #1256]
	ldr	r3, [r4, #3340]
	lsls	r2, r2, #1
	str	r3, [r5, #1260]
	bl	ftl_memset
	ldrh	r0, [r4, #540]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2154
.L2165:
	mov	r0, #-1
	b	.L2155
.L2154:
	movs	r1, #1
	mov	r8, r5
	bl	FtlGetLastWrittenPage
	ldrh	r10, [r4, #540]
	uxth	r6, r0
	adds	r0, r0, #1
	strh	r0, [r4, #542]	@ movhi
.L2156:
	sxth	r3, r6
	cmp	r3, #0
	blt	.L2164
	sxth	fp, r10
	movs	r1, #1
	ldr	r0, .L2181+12
	orr	r3, r3, fp, lsl #10
	str	r3, [r5, #1252]
	ldr	r3, [r4, #3316]
	mov	r2, r1
	ldr	r9, .L2181
	str	r3, [r5, #1256]
	bl	FlashReadPages
	ldrb	r3, [r7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2157
	ldr	r3, [r8, #1260]
	ldr	r2, [r3, #12]
	cmp	r2, #0
	beq	.L2157
	ldr	ip, [r8, #1256]
	str	r3, [sp, #32]
	ldr	r3, .L2181+4
	mov	r0, ip
	str	r2, [sp, #36]
	str	ip, [sp, #28]
	ldrh	r1, [r3, #2398]
	bl	js_hash
	ldr	r2, [sp, #36]
	cmp	r2, r0
	beq	.L2157
	ldr	ip, [sp, #28]
	ldr	r3, [sp, #32]
	cbnz	r6, .L2158
	ldrh	r1, [r9, #544]
	cmp	fp, r1
	beq	.L2158
	ldr	r0, [r3]
	ldrh	r1, [r9, #540]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r3, [r3, #8]
	str	r2, [sp, #12]
	ldr	r0, .L2181+16
	str	r3, [sp, #8]
	ldr	r3, [ip]
	str	r3, [sp, #16]
	ldr	r3, [r8, #1252]
	ldr	r2, [r8, #1248]
	bl	printk
	ldr	r3, .L2181+4
	ldrh	r10, [r9, #544]
	ldrh	r6, [r3, #2390]
	b	.L2160
.L2158:
	mov	r3, #-1
	str	r3, [r5, #1248]
.L2157:
	ldr	r3, [r5, #1248]
	adds	r3, r3, #1
	beq	.L2160
	ldr	r3, [r4, #3316]
	ldr	r2, .L2181+20
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2160
	ldr	r3, .L2181
	ldr	r3, [r3, #3340]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2160
.L2164:
	ldr	r6, .L2181
	movs	r2, #48
	ldr	r1, [r5, #1256]
	add	r0, r6, #240
	ldr	r8, .L2181+4
	bl	ftl_memcpy
	ldrh	r2, [r7, #2328]
	ldr	r1, [r5, #1256]
	ldr	r0, [r4, #296]
	adds	r1, r1, #48
	lsls	r2, r2, #1
	bl	ftl_memcpy
	ldrh	r2, [r7, #2328]
	ldr	r1, [r5, #1256]
	ldr	r0, [r4, #472]
	lsls	r3, r2, #1
	lsrs	r2, r2, #3
	adds	r3, r3, #51
	adds	r2, r2, #4
	bic	r3, r3, #3
	add	r1, r1, r3
	bl	ftl_memcpy
	ldrh	r3, [r7, #2432]
	cbz	r3, .L2162
	ldrh	r2, [r8, #2328]
	ldr	r0, [r6, #3392]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldr	r2, .L2181+8
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	ldr	r1, [r2, #1256]
	ldrh	r2, [r8, #2424]
	add	r1, r1, r3, lsl #2
	lsls	r2, r2, #2
	bl	ftl_memcpy
	b	.L2162
.L2160:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L2156
.L2162:
	ldr	r2, [r4, #240]
	ldr	r3, .L2181+20
	ldr	r6, .L2181
	cmp	r2, r3
	bne	.L2165
	ldrb	r0, [r6, #250]	@ zero_extendqisi2
	ldrh	r1, [r7, #2342]
	ldrh	r2, [r6, #248]
	cmp	r0, r1
	ldr	r3, .L2181+4
	strh	r2, [r6, #546]	@ movhi
	bne	.L2165
	ldrh	r1, [r3, #2388]
	ldrh	r0, [r3, #2394]
	str	r2, [r5, #1284]
	muls	r1, r2, r1
	str	r1, [r3, #2448]
	muls	r1, r0, r1
	ldrh	r0, [r3, #2458]
	str	r1, [r3, #2428]
	ldr	r1, [r3, #2332]
	subs	r0, r1, r0
	ldrh	r1, [r3, #2320]
	subs	r0, r0, r2
	bl	__aeabi_uidiv
	ldrh	r3, [r6, #256]
	ldrh	r1, [r6, #254]
	lsrs	r2, r3, #6
	strh	r2, [r6, #318]	@ movhi
	ldrh	r2, [r6, #258]
	and	r3, r3, #63
	strb	r3, [r6, #322]
	ldrb	r3, [r6, #251]	@ zero_extendqisi2
	strh	r2, [r6, #364]	@ movhi
	ldrh	r2, [r6, #260]
	strb	r3, [r6, #324]
	movw	r3, #65535
	strh	r1, [r6, #316]	@ movhi
	strh	r3, [r6, #556]	@ movhi
	movs	r3, #0
	strh	r3, [r6, #558]	@ movhi
	strb	r3, [r6, #562]
	strb	r3, [r6, #564]
	str	r3, [r6, #496]
	strh	r0, [r6, #536]	@ movhi
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r6, #370]
	ldrb	r2, [r6, #252]	@ zero_extendqisi2
	strh	r0, [r6, #366]	@ movhi
	strb	r2, [r6, #372]
	ldrh	r2, [r6, #262]
	strh	r2, [r6, #412]	@ movhi
	ldrh	r2, [r6, #264]
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r6, #418]
	ldrb	r2, [r6, #253]	@ zero_extendqisi2
	strh	r0, [r6, #414]	@ movhi
	strb	r2, [r6, #420]
	str	r3, [r6, #484]
	ldr	r2, [r6, #272]
	str	r3, [r6, #476]
	str	r3, [r6, #492]
	str	r3, [r6, #520]
	str	r3, [r6, #528]
	str	r3, [r6, #488]
	ldr	r3, [r6, #280]
	str	r2, [r6, #516]
	ldr	r2, [r6, #508]
	cmp	r3, r2
	ldr	r2, [r4, #512]
	it	hi
	strhi	r3, [r6, #508]
	ldr	r3, [r4, #276]
	cmp	r3, r2
	itt	hi
	ldrhi	r2, .L2181
	strhi	r3, [r2, #512]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2168
	ldr	r0, .L2181+24
	bl	make_superblock
.L2168:
	ldrh	r2, [r4, #364]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2169
	ldr	r0, .L2181+28
	bl	make_superblock
.L2169:
	ldrh	r2, [r4, #412]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2170
	ldr	r0, .L2181+32
	bl	make_superblock
.L2170:
	ldrh	r2, [r4, #556]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2171
	ldr	r0, .L2181+36
	bl	make_superblock
.L2171:
	movs	r0, #0
.L2155:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2182:
	.align	2
.L2181:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR4+1248
	.word	.LC109
	.word	1179929683
	.word	.LANCHOR2+316
	.word	.LANCHOR2+364
	.word	.LANCHOR2+412
	.word	.LANCHOR2+556
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	1
	.global	FtlDumpBlockInfo
	.thumb
	.thumb_func
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ubfx	r0, r0, #10, #16
	.pad #100
	sub	sp, sp, #100
	mov	r8, r1
	bl	P2V_block_in_plane
	ldr	r4, .L2199
	ldr	r7, .L2199+4
	ldr	r1, .L2199+8
	ldrh	r9, [r4, #2388]
	mov	r6, r0
	ldr	r0, .L2199+12
	bl	printk
	ldr	r3, [r7, #296]
	mov	r1, r6
	ldr	r0, .L2199+16
	ldrh	r2, [r3, r6, lsl #1]
	bl	printk
	add	r0, sp, #96
	strh	r6, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r5, [r4, #144]	@ zero_extendqisi2
	str	r7, [sp, #44]
	cbz	r5, .L2184
	cmp	r8, #0
	bne	.L2195
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r5, r0
	bne	.L2184
	ldrh	r9, [r4, #2390]
	b	.L2184
.L2195:
	movs	r5, #0
.L2184:
	ldr	r0, .L2199+20
	mov	r1, r5
	mov	r2, r9
	ldrh	r3, [r4, #2388]
	bl	printk
	ldr	r10, .L2199
	movs	r6, #0
.L2185:
	ldr	r3, .L2199+4
	movs	r2, #0
	ldrh	fp, [r10, #2320]
	add	lr, sp, #62
	ldrh	r0, [r10, #2400]
	mov	r8, #36
	ldr	r3, [r3, #3304]
	mov	r4, r2
	movw	ip, #65535
	str	r3, [sp, #28]
	ldr	r3, .L2199+4
	ldr	r3, [r3, #1144]
	str	r3, [sp, #32]
	ldrh	r3, [r10, #2398]
	str	r3, [sp, #36]
	ldr	r3, .L2199+4
	ldr	r3, [r3, #1148]
	str	r3, [sp, #40]
.L2186:
	uxth	r3, r2
	cmp	r3, fp
	bcs	.L2197
	ldrh	r3, [lr, #2]!
	cmp	r3, ip
	beq	.L2187
	ldr	r1, [sp, #28]
	orr	r3, r6, r3, lsl #10
	ldr	r7, [sp, #32]
	mla	r1, r8, r4, r1
	str	r3, [r1, #4]
	ldr	r3, [sp, #36]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r7
	str	r3, [r1, #8]
	mov	r3, r0
	muls	r3, r4, r3
	add	r4, r4, #1
	it	mi
	addmi	r3, r3, #3
	ldr	r7, [sp, #40]
	bic	r3, r3, #3
	uxth	r4, r4
	add	r3, r3, r7
	str	r3, [r1, #12]
.L2187:
	adds	r2, r2, #1
	b	.L2186
.L2197:
	ldr	r3, .L2199+4
	mov	r1, r4
	mov	r2, r5
	mov	r8, #0
	mov	fp, #36
	ldr	r0, [r3, #3304]
	bl	FlashReadPages
.L2191:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2198
	ldr	r3, [sp, #44]
	mul	r2, fp, r8
	ldrh	r1, [sp, #48]
	ldr	ip, [r3, #3304]
	add	r8, r8, #1
	add	lr, ip, r2
	ldr	r3, [lr, #12]
	ldr	r0, [lr, #8]
	ldr	r7, [r3]
	str	r7, [sp]
	ldr	r7, [r3, #4]
	str	r7, [sp, #4]
	ldr	r7, [r3, #8]
	str	r7, [sp, #8]
	ldr	r3, [r3, #12]
	str	r3, [sp, #12]
	ldr	r3, [r0]
	str	r3, [sp, #16]
	ldr	r3, [r0, #4]
	ldr	r0, .L2199+24
	str	r3, [sp, #20]
	ldr	r2, [ip, r2]
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2191
.L2198:
	adds	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2185
.L2193:
	movs	r0, #0
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2200:
	.align	2
.L2199:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+148
	.word	.LC110
	.word	.LC111
	.word	.LC112
	.word	.LC113
	.fnend
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
	.align	1
	.global	FtlScanAllBlock
	.thumb
	.thumb_func
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r0, .L2217
	mov	r8, #0
	ldr	r1, .L2217+4
	bl	printk
	ldr	r5, .L2217+8
	mov	r7, r5
.L2202:
	ldr	r3, .L2217+12
	uxth	r4, r8
	ldrh	r3, [r3, #2330]
	cmp	r3, r4
	bls	.L2213
	add	r9, sp, #88
	mov	r0, r4
	mov	fp, #36
	movw	r10, #65535
	strh	r4, [r9, #-48]!	@ movhi
	bl	ftl_get_blk_mode
	ldr	r2, [r5, #296]
	mov	r1, r4
	ldrh	r2, [r2, r4, lsl #1]
	mov	r3, r0
	ldr	r0, .L2217+16
	bl	printk
	mov	r0, r9
	bl	make_superblock
	ldr	r3, .L2217+12
	movs	r2, #0
	ldr	r9, [r5, #1148]
	ldrh	lr, [r3, #2320]
	mov	r4, r2
	ldr	r3, [r5, #3304]
	str	r3, [sp, #28]
	add	ip, sp, #54
	ldr	r3, [r5, #1144]
	str	r3, [sp, #32]
	ldr	r3, .L2217+12
	ldrh	r0, [r3, #2398]
	ldrh	r3, [r3, #2400]
	str	r3, [sp, #36]
.L2203:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L2214
	ldrh	r3, [ip, #2]!
	cmp	r3, r10
	beq	.L2204
	ldr	r1, [sp, #28]
	lsls	r3, r3, #10
	ldr	r6, [sp, #32]
	mla	r1, fp, r4, r1
	str	r3, [r1, #4]
	mov	r3, r0
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r6
	str	r3, [r1, #8]
	ldr	r3, [sp, #36]
	muls	r3, r4, r3
	add	r4, r4, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r4, r4
	add	r3, r3, r9
	str	r3, [r1, #12]
.L2204:
	adds	r2, r2, #1
	b	.L2203
.L2214:
	ldr	r0, [r7, #3304]
	mov	r1, r4
	movs	r2, #0
	mov	r9, #0
	bl	FlashReadPages
	mov	r10, #36
.L2208:
	uxth	r3, r9
	cmp	r3, r4
	bcs	.L2215
	mul	r2, r10, r9
	ldr	ip, [r7, #3304]
	ldrh	r1, [sp, #40]
	add	r9, r9, #1
	add	lr, ip, r2
	ldr	r3, [lr, #12]
	ldr	r0, [lr, #8]
	ldr	r6, [r3]
	str	r6, [sp]
	ldr	r6, [r3, #4]
	str	r6, [sp, #4]
	ldr	r6, [r3, #8]
	str	r6, [sp, #8]
	ldr	r3, [r3, #12]
	str	r3, [sp, #12]
	ldr	r3, [r0]
	str	r3, [sp, #16]
	ldr	r3, [r0, #4]
	ldr	r0, .L2217+20
	str	r3, [sp, #20]
	ldr	r2, [ip, r2]
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2208
.L2215:
	ldr	r0, [r7, #3304]
	mov	r1, r4
	movs	r2, #1
	mov	r9, #0
	bl	FlashReadPages
	mov	r10, #36
.L2210:
	uxth	r3, r9
	cmp	r3, r4
	bcs	.L2216
	mul	r2, r10, r9
	ldr	ip, [r7, #3304]
	ldrh	r1, [sp, #40]
	add	r9, r9, #1
	add	lr, ip, r2
	ldr	r3, [lr, #12]
	ldr	r0, [lr, #8]
	ldr	r6, [r3]
	str	r6, [sp]
	ldr	r6, [r3, #4]
	str	r6, [sp, #4]
	ldr	r6, [r3, #8]
	str	r6, [sp, #8]
	ldr	r3, [r3, #12]
	str	r3, [sp, #12]
	ldr	r3, [r0]
	str	r3, [sp, #16]
	ldr	r3, [r0, #4]
	ldr	r0, .L2217+24
	str	r3, [sp, #20]
	ldr	r2, [ip, r2]
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2210
.L2216:
	add	r8, r8, #1
	b	.L2202
.L2213:
	movs	r0, #0
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2218:
	.align	2
.L2217:
	.word	.LC110
	.word	.LANCHOR3+168
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC114
	.word	.LC115
	.word	.LC116
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	1
	.global	ftl_scan_all_ppa
	.thumb
	.thumb_func
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L2237
	ldr	r5, .L2237+4
	ldrh	r4, [r6, #2386]
	mov	r10, r6
	subs	r4, r4, #16
.L2220:
	ldrh	r3, [r6, #2386]
	cmp	r4, r3
	bge	.L2235
	uxth	r8, r4
	mov	r0, r8
	bl	ftl_get_blk_mode
	ldrb	r3, [r10, #144]	@ zero_extendqisi2
	cbz	r3, .L2221
	ldrh	r3, [r10, #2328]
	cmp	r4, r3
	bge	.L2222
	ldrh	r3, [r10, #2402]
	cmp	r4, r3
	blt	.L2222
.L2221:
	cmp	r0, #1
	bne	.L2223
.L2222:
	ldrh	r7, [r6, #2390]
	mov	r9, #-2147483648
	b	.L2224
.L2223:
	ldrh	r7, [r6, #2388]
	mov	r9, #0
.L2224:
	ldr	r0, .L2237+8
	mov	r1, r4
	mov	r2, r7
	mov	r3, r9
	bl	printk
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbz	r0, .L2225
	ldr	r0, .L2237+12
	mov	r1, r4
	mov	r2, r7
	mov	r3, r9
	bl	printk
.L2225:
	ldr	fp, .L2237+32
	lsl	ip, r4, #10
	mov	r8, #0
.L2226:
	cmp	r8, r7
	beq	.L2236
	add	r3, r9, ip
	movs	r2, #0
	add	r3, r3, r8
	str	r3, [r5, #1252]
	ldr	r3, [fp, #3316]
	movs	r1, #1
	ldr	r0, .L2237+16
	add	r8, r8, #1
	str	ip, [sp, #28]
	str	r3, [r5, #1256]
	ldr	r3, [fp, #3340]
	str	r2, [r5, #1248]
	str	r3, [r5, #1260]
	bl	FlashReadPages
	ldr	r3, [r5, #1260]
	ldr	r2, [r5, #1256]
	ldr	r0, .L2237+20
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	ldr	r1, [r5, #1252]
	str	r2, [sp, #16]
	ldr	r2, [r5, #1248]
	ldr	r3, [r3]
	bl	printk
	ldr	ip, [sp, #28]
	b	.L2226
.L2236:
	adds	r4, r4, #1
	b	.L2220
.L2235:
	ldr	r0, .L2237+24
	ldr	r1, .L2237+28
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L2238:
	.align	2
.L2237:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC117
	.word	.LC118
	.word	.LANCHOR4+1248
	.word	.LC119
	.word	.LC120
	.word	.LANCHOR3+184
	.word	.LANCHOR2
	.fnend
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
	.align	1
	.global	FlashProgPages
	.thumb
	.thumb_func
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	r6, .L2293
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	r5, [r6, #44]
	ldrb	r7, [r6]	@ zero_extendqisi2
	ldrb	r5, [r5, #19]	@ zero_extendqisi2
	str	r5, [sp, #16]
	ldr	r5, .L2293+4
	ldrb	r5, [r5, #481]	@ zero_extendqisi2
	str	r5, [sp, #12]
	cbz	r7, .L2240
	bl	FlashProgSlc2KPages
	b	.L2241
.L2253:
	movs	r3, #36
	mov	r1, r9
	add	r2, sp, #28
	mul	r8, r3, r7
	ldr	r3, [sp, #8]
	subs	r3, r3, r7
	add	fp, r4, r8
	uxtb	r3, r3
	str	r3, [sp]
	mov	r0, fp
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldrb	r3, [r6, #2230]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r0, [sp, #32]
	cmp	r0, r3
	bcc	.L2242
	mov	r3, #-1
	str	r3, [r4, r8]
.L2243:
	adds	r7, r7, #1
.L2240:
	ldr	r3, [sp, #8]
	ldr	r8, .L2293
	cmp	r7, r3
	bcc	.L2253
	b	.L2291
.L2242:
	ldrb	r3, [r6, #2240]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r6, r0, lsl #4
	it	eq
	moveq	r10, #0
	ldr	r3, [r3, #2108]
	cbz	r3, .L2245
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L2245:
	ldr	r3, [sp, #32]
	movs	r0, #0
	add	r2, r6, r3, lsl #4
	addw	r1, r2, #2108
	str	r0, [r2, #2112]
	ldr	r0, [sp, #28]
	str	fp, [r2, #2108]
	str	r0, [r2, #2104]
	cmp	r10, #0
	beq	.L2246
	add	r2, r8, #36
	add	r2, r2, r4
	str	r2, [r1, #4]
.L2246:
	adds	r2, r6, r3
	add	r3, r6, r3, lsl #4
	ldrb	r5, [r2, #2232]	@ zero_extendqisi2
	strb	r5, [r3, #2100]
	mov	r0, r5
	ldrb	r3, [r6, #2230]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2247
	bl	NandcWaitFlashReady
	b	.L2248
.L2247:
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r6, r3, lsl #2
	ldr	r2, [r3, #1172]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r5
	bl	NandcFlashDeCs
.L2248:
	ldr	r3, [sp, #16]
	subs	r3, r3, #1
	cmp	r3, #7
	bhi	.L2249
	adds	r3, r6, r5
	ldrb	r3, [r3, #2064]	@ zero_extendqisi2
	cbz	r3, .L2249
	mov	r0, r5
	ldrb	r1, [r6, #1211]	@ zero_extendqisi2
	ldr	r2, .L2293+8
	movs	r3, #0
	bl	HynixSetRRPara
.L2249:
	mov	r0, r5
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r5
	bne	.L2250
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cbz	r3, .L2250
	bl	flash_enter_slc_mode
	b	.L2251
.L2250:
	bl	flash_exit_slc_mode
.L2251:
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r0, r5
	movs	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L2252
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	mov	r0, r5
	add	r8, r8, #36
	add	r8, r8, r4
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r6, r3, lsl #2
	ldr	r2, [r3, #1172]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r6, #4]
	mov	r0, r5
	ldr	r3, [sp, #28]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r8, #12]
	mov	r0, r5
	movs	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r8, #8]
	bl	NandcXferData
.L2252:
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2243
.L2291:
	movs	r5, #0
	addw	r7, r8, #2100
	mov	r6, r8
.L2254:
	ldrb	r3, [r8, #2230]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L2292
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2255
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cbz	r3, .L2255
	lsls	r3, r5, #4
	ldrb	r0, [r7, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2255:
	adds	r5, r5, #1
	b	.L2254
.L2292:
	ldr	r3, [sp, #20]
	cbnz	r3, .L2257
.L2265:
	movs	r0, #0
	b	.L2241
.L2257:
	ldr	r7, .L2293+12
	mov	r8, #0
	mov	r10, r7
.L2258:
	ldr	r3, [sp, #8]
	cmp	r8, r3
	beq	.L2265
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L2259
	ldr	r1, [r4, #4]
	ldr	r0, .L2293+16
	bl	printk
	b	.L2260
.L2259:
	ldr	r3, [sp, #8]
	mov	r1, r9
	add	r2, sp, #28
	mov	r0, r4
	rsb	r3, r8, r3
	mov	r6, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r7, #1220]
	movs	r3, #0
	str	r3, [r2]
	ldr	r2, [r7, #1224]
	str	r3, [r2]
	ldmia	r6!, {r0, r1, r2, r3}
	add	r5, sp, #36
	stmia	r5!, {r0, r1, r2, r3}
	ldmia	r6!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	add	r0, sp, #36
	ldr	r3, [r6]
	movs	r1, #1
	mov	r2, r9
	str	r3, [r5]
	ldr	r3, [r7, #1220]
	str	r3, [sp, #44]
	ldr	r3, [r7, #1224]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r5, [sp, #36]
	adds	r3, r5, #1
	bne	.L2261
	ldr	r0, .L2293+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L2261:
	ldr	r3, [r4, #12]
	cbz	r3, .L2262
	ldr	r2, [r3]
	ldr	r3, [r10, #1224]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2262
	ldr	r0, .L2293+24
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2262:
	ldr	r3, [r4, #8]
	cbz	r3, .L2260
	ldr	r2, [r3]
	ldr	r3, [r10, #1220]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2260
	ldr	r0, .L2293+28
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2260:
	add	r8, r8, #1
	adds	r4, r4, #36
	b	.L2258
.L2241:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2294:
	.align	2
.L2293:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1214
	.word	.LANCHOR4
	.word	.LC104
	.word	.LC105
	.word	.LC107
	.word	.LC108
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	1
	.global	FlashTestBlk
	.thumb
	.thumb_func
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, r0
	ldr	r5, .L2298
	.pad #108
	sub	sp, sp, #108
	ldr	r3, [r5, #1204]
	cmp	r0, r3
	bcc	.L2297
	ldr	r3, [r5, #1212]
	add	r0, sp, #40
	movs	r1, #165
	movs	r2, #32
	str	r0, [sp, #16]
	lsls	r4, r4, #10
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r0, [r5, #1212]
	movs	r1, #90
	movs	r2, #8
	bl	ftl_memset
	movs	r1, #1
	mov	r2, r1
	add	r0, sp, #4
	str	r4, [sp, #8]
	bl	FlashEraseBlocks
	movs	r1, #1
	mov	r2, r1
	mov	r3, r1
	add	r0, sp, #4
	bl	FlashProgPages
	movs	r1, #0
	movs	r2, #1
	ldr	r4, [sp, #4]
	add	r0, sp, #4
	adds	r4, r4, #0
	it	ne
	movne	r4, #1
	negs	r4, r4
	bl	FlashEraseBlocks
	b	.L2296
.L2297:
	movs	r4, #0
.L2296:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, pc}
.L2299:
	.align	2
.L2298:
	.word	.LANCHOR4
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	1
	.global	FlashMakeFactorBbt
	.thumb
	.thumb_func
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r5, .L2361
	movs	r1, #1
	ldr	r4, .L2361+4
	ldr	r0, .L2361+8
	ldr	r3, [r5, #1216]
	ldrh	r8, [r4, #128]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #130]
	smulbb	r8, r8, r3
	ldr	r3, [r4, #44]
	ldrb	r3, [r3, #24]	@ zero_extendqisi2
	uxth	r8, r8
	str	r3, [sp, #24]
	ldrh	r3, [r4, #4]
	str	r3, [sp, #16]
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r4, .L2361+4
	cmp	r3, #1
	itttt	eq
	ldreq	r3, [sp, #16]
	lsleq	r3, r3, #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r5, #1216]
	movs	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r5, .L2361
	lsr	r3, r8, #4
	str	r3, [sp, #28]
	movs	r3, #0
	str	r3, [sp, #8]
.L2302:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2357
	add	r3, r5, r7, lsl #1
	ldrh	r6, [r3, #1232]
	cmp	r6, #0
	bne	.L2332
	ldrh	r2, [r4, #136]
	mov	r1, r6
	ldr	r0, [r5, #1184]
	mov	r9, r6
	lsls	r2, r2, #9
	bl	ftl_memset
	adds	r3, r4, r7
	ldrb	r10, [r3, #2232]	@ zero_extendqisi2
	lsls	r3, r7, #2
	add	fp, r4, r3
	str	r6, [sp, #4]
	str	r3, [sp, #32]
.L2304:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L2314
	movs	r3, #255
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	lsls	r1, r3, #31
	bpl	.L2306
	ldr	r3, [fp, #1172]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r3, r6
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L2306
	ldr	r1, [r4, #4]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r1, r3
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	ands	r3, r3, r2
	strb	r3, [sp, #42]
.L2306:
	ldr	r3, [sp, #24]
	lsls	r2, r3, #30
	bpl	.L2308
	ldr	r3, [r4, #44]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1172]
	subs	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2308:
	ldr	r3, [r4, #44]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L2309
	cmp	r2, #8
	beq	.L2309
	ldrb	r3, [r3, #18]	@ zero_extendqisi2
	cmp	r3, #12
	bne	.L2310
.L2309:
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	cbz	r3, .L2331
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	clz	r0, r0
	lsrs	r0, r0, #5
	b	.L2311
.L2310:
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2331
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	it	ne
	movne	r0, #1
	b	.L2311
.L2331:
	movs	r0, #1
.L2311:
	ldr	r3, [sp, #24]
	lsls	r3, r3, #29
	bpl	.L2312
	ldr	r3, .L2361+4
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #1172]
	add	r1, r1, r6
	bl	SandiskProgTestBadBlock
.L2312:
	cbz	r0, .L2313
	mov	r1, r7
	ldr	r2, [sp, #4]
	ldr	r0, .L2361+12
	add	r9, r9, #1
	bl	printk
	ldr	r2, [r5, #1184]
	uxth	r9, r9
	ldr	r3, [sp, #12]
	and	r0, r3, #31
	lsrs	r1, r3, #5
	movs	r3, #1
	lsls	r3, r3, r0
	ldr	r0, [r2, r1, lsl #2]
	orrs	r3, r3, r0
	str	r3, [r2, r1, lsl #2]
	ldr	r2, [sp, #28]
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	muls	r3, r2, r3
	cmp	r9, r3
	bgt	.L2314
.L2313:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r6, r6, r3
	b	.L2304
.L2314:
	mov	r2, r9
	ldr	r0, .L2361+16
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	muls	r3, r2, r3
	cmp	r9, r3
	blt	.L2316
	ldrh	r2, [r4, #136]
	movs	r1, #0
	ldr	r0, [r5, #1184]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2316:
	cmp	r7, #0
	bne	.L2318
	ldrh	r10, [r5, #1204]
	mov	r9, #1
	mov	r6, r7
.L2319:
	ldr	r3, .L2361+4
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2358
	mov	r0, r10
	bl	FlashTestBlk
	cbz	r0, .L2320
	mov	r1, r10
	ldr	r0, .L2361+20
	bl	printk
	ldr	r2, [r5, #1184]
	lsr	r0, r10, #5
	adds	r6, r6, #1
	and	r3, r10, #31
	ldr	r1, [r2, r0, lsl #2]
	uxth	r6, r6
	lsl	r3, r9, r3
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2320:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2319
.L2358:
	add	fp, r8, #-1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L2322:
	cmp	fp, r9
	ble	.L2359
	mov	r0, fp
	bl	FlashTestBlk
	cbz	r0, .L2323
	mov	r1, fp
	ldr	r0, .L2361+20
	bl	printk
	ldr	r2, [r5, #1184]
	lsr	r0, fp, #5
	and	r3, fp, #31
	lsl	r3, r10, r3
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2323:
	add	fp, fp, #-1
	uxth	fp, fp
	b	.L2322
.L2359:
	ldr	r3, .L2361+4
	ldr	r2, [r5, #1204]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	subs	r3, r3, r2
	cmp	r6, r3
	bcc	.L2318
	ldrh	r2, [r4, #136]
	movs	r1, #0
	ldr	r0, [r5, #1184]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2318:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	add	r10, r8, #-1
	add	r9, r5, r7, lsl #1
	uxth	r10, r10
	mul	r6, r8, r6
.L2326:
	mov	r1, r7
	ldr	r0, .L2361+24
	mov	r2, r10
	bl	printk
	ldr	r1, [r5, #1184]
.L2327:
	lsr	r3, r10, #5
	and	r2, r10, #31
	ldr	r3, [r1, r3, lsl #2]
	lsrs	r3, r3, r2
	ands	r3, r3, #1
	beq	.L2360
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2327
.L2360:
	ldr	r1, [sp, #20]
	movw	r2, #61664
	strh	r10, [r9, #1232]	@ movhi
	add	r0, sp, #44
	strh	r2, [r1]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	movs	r1, #1
	ldr	r3, [r5, #1184]
	mov	r2, r1
	str	r3, [sp, #52]
	ldr	r3, [r5, #1216]
	str	r3, [sp, #56]
	add	r3, r10, r6
	lsls	r3, r3, #10
	str	r3, [sp, #48]
	bl	FlashEraseBlocks
	movs	r1, #1
	mov	r3, r1
	mov	r2, r1
	add	r0, sp, #44
	bl	FlashProgPages
	ldr	r3, [sp, #44]
	cbz	r3, .L2332
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2326
.L2332:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2302
.L2357:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2362:
	.align	2
.L2361:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LC121
	.word	.LC122
	.word	.LC123
	.word	.LC124
	.word	.LC125
	.fnend
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	1
	.global	FtlLowFormatEraseBlock
	.thumb
	.thumb_func
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L2415
	str	r0, [sp, #4]
	str	r1, [sp]
	ldrb	r3, [r7, #2240]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r3, .L2415+4
	ldr	r2, [r3, #224]
	mov	fp, r3
	cmp	r2, #0
	bne	.L2392
	ldrb	r3, [r7, #144]	@ zero_extendqisi2
	mov	r8, #36
	mov	r9, r2
	mov	r5, r2
	mov	r4, r2
	mov	r10, fp
	str	r3, [sp, #8]
	str	r0, [fp, #3292]
.L2365:
	ldrh	r0, [r7, #2320]
	uxth	r1, r9
	ldr	r3, .L2415
	cmp	r0, r1
	bls	.L2410
	mul	r6, r8, r1
	ldr	r0, [fp, #228]
	movs	r3, #0
	str	r3, [r0, r6]
	ldr	r3, .L2415
	add	r1, r1, r3
	ldrb	r0, [r1, #2348]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	bl	V2P_block
	ldr	r3, [sp]
	mov	r6, r0
	cbz	r3, .L2366
	bl	IsBlkInVendorPart
	cbnz	r0, .L2367
.L2366:
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2368
	ldr	r3, [fp, #228]
	lsls	r6, r6, #10
	ldrh	r1, [r7, #2400]
	muls	r1, r5, r1
	it	mi
	addmi	r1, r1, #3
	mla	lr, r8, r5, r3
	adds	r5, r5, #1
	bic	r1, r1, #3
	uxth	r5, r5
	str	r0, [lr, #8]
	ldr	r0, [r10, #3344]
	str	r6, [lr, #4]
	add	r1, r1, r0
	str	r1, [lr, #12]
	b	.L2367
.L2368:
	adds	r4, r4, #1
	uxth	r4, r4
.L2367:
	add	r9, r9, #1
	b	.L2365
.L2410:
	cmp	r5, #0
	beq	.L2391
	ldr	r2, [sp, #8]
	mov	r8, #0
	ldr	r0, [fp, #228]
	mov	r9, #36
	adds	r6, r2, #0
	mov	r2, r5
	it	ne
	movne	r6, #1
	strb	r8, [r3, #2240]
	str	r3, [sp, #8]
	mov	r1, r6
	bl	FlashEraseBlocks
	ldrb	r2, [sp, #16]	@ zero_extendqisi2
	ldr	r3, [sp, #8]
	strb	r2, [r3, #2240]
.L2372:
	uxth	r2, r8
	cmp	r2, r5
	bcs	.L2411
	mul	r2, r9, r8
	ldr	r1, [fp, #228]
	adds	r0, r1, r2
	ldr	r2, [r1, r2]
	adds	r2, r2, #1
	bne	.L2373
	ldr	r0, [r0, #4]
	adds	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2373:
	add	r8, r8, #1
	b	.L2372
.L2411:
	ldr	r3, [sp]
	cbnz	r3, .L2375
	movs	r3, #6
	uxth	r6, r6
	str	r3, [sp, #12]
	movs	r3, #1
	str	r3, [sp, #8]
	b	.L2376
.L2375:
	ldrh	r3, [r7, #2390]
	str	r3, [sp, #8]
	ldrb	r3, [r7, #144]	@ zero_extendqisi2
	cbnz	r3, .L2393
	ldr	r3, [sp, #8]
	movs	r6, #1
	lsrs	r3, r3, #2
	str	r3, [sp, #12]
	b	.L2376
.L2393:
	movs	r6, #1
	str	r6, [sp, #12]
.L2376:
	ldr	r8, .L2415+4
	movs	r7, #0
	mov	r10, r8
.L2386:
	mov	fp, #0
	mov	r5, fp
.L2377:
	ldr	r9, .L2415
	uxth	r3, fp
	ldrh	r1, [r9, #2320]
	cmp	r1, r3
	bls	.L2412
	movs	r2, #36
	ldr	r0, [r8, #228]
	mul	r1, r2, r3
	movs	r2, #0
	str	r2, [r0, r1]
	ldr	r2, .L2415
	ldr	r1, [sp, #4]
	add	r3, r3, r2
	ldrb	r0, [r3, #2348]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp]
	mov	r9, r0
	cbz	r3, .L2378
	bl	IsBlkInVendorPart
	cbnz	r0, .L2379
.L2378:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2379
	ldr	r1, [r10, #228]
	movs	r3, #36
	ldr	r0, [r8, #3336]
	mla	r1, r3, r5, r1
	add	r3, r7, r9, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [r10, #3332]
	str	r3, [r1, #8]
	ldr	r3, .L2415
	ldrh	r3, [r3, #2400]
	muls	r3, r5, r3
	add	r5, r5, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r5, r5
	add	r3, r3, r0
	str	r3, [r1, #12]
.L2379:
	add	fp, fp, #1
	b	.L2377
.L2412:
	cmp	r5, #0
	beq	.L2391
	mov	r2, r6
	movs	r3, #1
	ldr	r0, [r8, #228]
	mov	r1, r5
	mov	fp, #0
	strb	fp, [r9, #2240]
	bl	FlashProgPages
	movs	r2, #36
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r9, #2240]
	ldr	r9, .L2415+4
.L2383:
	uxth	r3, fp
	cmp	r3, r5
	bcs	.L2413
	mul	r3, r2, fp
	ldr	r1, [r10, #228]
	adds	r0, r1, r3
	ldr	r3, [r1, r3]
	cbz	r3, .L2384
	ldr	r0, [r0, #4]
	adds	r4, r4, #1
	str	r2, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #20]
.L2384:
	add	fp, fp, #1
	b	.L2383
.L2413:
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	ldr	r3, [sp, #8]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L2386
	movs	r7, #0
	mov	r8, #36
.L2387:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L2414
	ldr	r3, [sp]
	cbz	r3, .L2388
	mul	r3, r8, r7
	ldr	r2, [r9, #228]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L2388
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2388:
	adds	r7, r7, #1
	b	.L2387
.L2414:
	ldr	r3, [sp, #4]
	cmp	r3, #63
	bls	.L2390
	ldr	r3, [sp]
	cbz	r3, .L2391
.L2390:
	ldr	r0, [r9, #228]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L2391:
	mov	r0, r4
	b	.L2364
.L2392:
	movs	r0, #0
.L2364:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2416:
	.align	2
.L2415:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	1
	.global	FtlBbmTblFlush
	.thumb
	.thumb_func
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r7, .L2434
	ldr	r6, [r7, #224]
	cmp	r6, #0
	bne	.L2419
	ldr	r4, .L2434+4
	mov	r1, r6
	ldr	r0, [r7, #3316]
	ldr	r5, .L2434+8
	addw	r9, r4, #2476
	ldr	r3, [r7, #3340]
	ldrh	r2, [r4, #2398]
	str	r0, [r5, #1256]
	str	r3, [r5, #1260]
	bl	ftl_memset
.L2420:
	ldrh	r3, [r4, #2342]
	ldr	r8, .L2434+4
	cmp	r6, r3
	bge	.L2433
	ldrh	r2, [r7, #3404]
	ldr	r3, [r5, #1256]
	ldr	r1, [r9, #4]!
	mul	r0, r2, r6
	lsls	r2, r2, #2
	adds	r6, r6, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L2420
.L2433:
	ldr	r6, [r5, #1260]
	movs	r1, #255
	movs	r2, #16
	ldr	fp, .L2434
	mov	r0, r6
	bl	ftl_memset
	movw	r3, #61649
	strh	r3, [r6]	@ movhi
	ldr	r3, [r8, #2460]
	str	r3, [r6, #4]
	ldrh	r3, [r8, #2452]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r8, #2456]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r8, #2458]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r8, #2316]
	mov	r8, #0
	mov	r10, r8
	strh	r3, [r6, #12]	@ movhi
.L2422:
	ldr	r3, [r7, #3316]
	mov	ip, #0
	ldrh	r1, [r4, #2452]
	ldrh	r2, [r4, #2454]
	str	r3, [r5, #1256]
	ldr	r3, [r7, #3340]
	str	ip, [r5, #1248]
	str	ip, [sp, #12]
	str	r3, [r5, #1260]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r5, #1252]
	ldrh	r3, [r4, #2456]
	str	r0, [sp]
	ldr	r0, .L2434+12
	bl	printk
	ldrh	r3, [r4, #2390]
	ldrh	r2, [r4, #2454]
	subs	r3, r3, #1
	ldr	r9, .L2434+4
	cmp	r2, r3
	ldr	ip, [sp, #12]
	blt	.L2423
	ldrh	r2, [r9, #2452]
	movs	r1, #1
	ldr	r3, [r9, #2460]
	ldr	r0, [fp, #228]
	adds	r3, r3, #1
	str	r3, [r9, #2460]
	str	r3, [r6, #4]
	strh	r2, [r6, #8]	@ movhi
	ldrh	r3, [r9, #2456]
	strh	r2, [r9, #2456]	@ movhi
	ldr	r2, .L2434+8
	strh	r3, [r9, #2452]	@ movhi
	lsls	r3, r3, #10
	strh	ip, [r9, #2454]	@ movhi
	str	r3, [r2, #1252]
	mov	r2, r1
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L2423:
	movs	r1, #1
	ldr	r9, .L2434+8
	mov	r2, r1
	mov	r3, r1
	add	r0, r9, #1248
	bl	FlashProgPages
	ldrh	r3, [r4, #2454]
	adds	r3, r3, #1
	strh	r3, [r4, #2454]	@ movhi
	ldr	r3, [r5, #1248]
	adds	r2, r3, #1
	bne	.L2424
	add	r8, r8, #1
	ldr	r0, .L2434+16
	ldr	r1, [r9, #1252]
	uxth	r8, r8
	bl	printk
	cmp	r8, #3
	bls	.L2422
	ldr	r0, .L2434+20
	mov	r2, r8
	ldr	r1, [r9, #1252]
	bl	printk
	movs	r3, #1
	str	r3, [r7, #224]
	b	.L2419
.L2424:
	add	r10, r10, #1
	cmp	r10, #1
	beq	.L2422
	cmp	r3, #256
	beq	.L2422
.L2419:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2435:
	.align	2
.L2434:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC126
	.word	.LC127
	.word	.LC128
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	1
	.global	allocate_data_superblock
	.thumb
	.thumb_func
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	ldr	r4, .L2492
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r4, #224]
	cmp	r3, #0
	bne	.L2437
	ldr	r9, .L2492+16
	mov	r8, r4
.L2438:
	ldr	r3, .L2492+4
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r3
	bne	.L2439
	ldrh	r7, [r8, #312]
	ldr	r6, [r8, #1132]
	lsrs	r1, r7, #1
	mul	r0, r6, r7
	adds	r3, r1, #1
	add	r3, r3, r0, lsr #2
	ldr	r0, [r9, #2244]
	uxth	r3, r3
	cbz	r0, .L2462
	ldr	r0, [r8, #532]
	cmp	r0, #39
	bhi	.L2462
	cmp	r0, #2
	bls	.L2467
	lsls	r0, r7, #31
	bpl	.L2440
	cbz	r6, .L2464
	b	.L2440
.L2439:
	cmp	r2, #1
	bne	.L2467
	ldrh	r3, [r9, #2340]
	cmp	r3, #1
	beq	.L2467
	ldrb	r3, [r9, #144]	@ zero_extendqisi2
	cbnz	r3, .L2467
	ldrh	r0, [r8, #312]
	ldr	r3, [r9, #2244]
	lsrs	r1, r0, #3
	cbz	r3, .L2440
	ldr	r3, [r8, #532]
	cmp	r3, #1
	bhi	.L2440
	rsb	r0, r0, r0, lsl #3
	ubfx	r1, r0, #3, #16
	b	.L2440
.L2462:
	mov	r1, r3
.L2440:
	cbz	r1, .L2441
	subs	r1, r1, #1
	uxth	r1, r1
	b	.L2441
.L2464:
	mov	r1, r6
	b	.L2441
.L2467:
	movs	r1, #0
.L2441:
	ldr	r0, .L2492+8
	bl	List_pop_index_node
	ldrh	r3, [r4, #312]
	subs	r3, r3, #1
	strh	r3, [r4, #312]	@ movhi
	ldrh	r3, [r9, #2328]
	uxth	r6, r0
	cmp	r3, r6
	bls	.L2438
	ldr	r3, [r8, #296]
	ldrh	r7, [r3, r6, lsl #1]
	cmp	r7, #0
	bne	.L2438
	strh	r6, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbz	r3, .L2443
	ldr	r0, [r8, #228]
	movs	r2, #36
	ldrh	ip, [r9, #2320]
	add	lr, r5, #14
	movw	r10, #65535
	mov	r3, r0
	mla	r1, r2, ip, r0
	mov	ip, r7
	str	r1, [sp, #4]
	b	.L2444
.L2443:
	ldr	r3, [r8, #296]
	b	.L2487
.L2444:
	ldr	r1, [sp, #4]
	cmp	r3, r1
	beq	.L2489
	str	ip, [r3, #8]
	adds	r3, r3, #36
	str	ip, [r3, #-24]
	ldrh	r1, [lr, #2]!
	cmp	r1, r10
	itttt	ne
	lslne	r1, r1, #10
	mlane	fp, r2, r7, r0
	addne	r7, r7, #1
	uxthne	r7, r7
	it	ne
	strne	r1, [fp, #4]
	b	.L2444
.L2489:
	ldr	r3, [r9, #2244]
	cbz	r3, .L2447
	ldr	r3, .L2492+12
	cmp	r5, r3
	bne	.L2447
	ldr	r3, [r8, #232]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #40
	itt	hi
	movhi	r3, #0
	strbhi	r3, [r8, #324]
.L2447:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #232]
	cbnz	r3, .L2448
	ldrh	r1, [r2, r6, lsl #1]
	cbz	r1, .L2468
	ldrh	r3, [r9, #2380]
	add	r3, r3, r1
	uxth	r3, r3
	b	.L2449
.L2468:
	movs	r3, #2
.L2449:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	mov	r0, r6
	ldr	r3, [r4, #516]
	movs	r1, #0
	adds	r3, r3, #1
	str	r3, [r4, #516]
	bl	ftl_set_blk_mode
	b	.L2450
.L2448:
	ldrh	r3, [r2, r6, lsl #1]
	lsrs	r0, r6, #5
	and	r1, r6, #31
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #520]
	ldr	r2, [r4, #472]
	adds	r3, r3, #1
	str	r3, [r4, #520]
	movs	r3, #1
	lsls	r3, r3, r1
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2450:
	ldr	r3, [r4, #232]
	ldr	r2, [r4, #528]
	ldr	r0, [r4, #516]
	ldrh	r3, [r3, r6, lsl #1]
	ldrh	r1, [r9, #2328]
	cmp	r3, r2
	ldrh	r2, [r9, #2380]
	it	hi
	strhi	r3, [r8, #528]
	ldr	r3, [r4, #520]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #3360]
	ldr	r1, [r4, #228]
	ldr	r3, [r2, #16]
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r3, #36
	adds	r2, r1, #4
	mla	r3, r3, r7, r1
	adds	r3, r3, #40
	str	r0, [r4, #524]
.L2452:
	adds	r2, r2, #36
	cmp	r2, r3
	beq	.L2490
	ldr	r1, [r2, #-36]
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r2, #-36]
	b	.L2452
.L2490:
	ldrb	r3, [r9, #144]	@ zero_extendqisi2
	cbz	r3, .L2454
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #228]
	cmp	r3, #1
	ite	eq
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2454:
	mov	r2, r7
	ldr	r0, [r4, #228]
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r10, #0
	bl	FlashEraseBlocks
	add	r2, r5, #16
	mov	ip, #36
	mov	fp, r10
.L2456:
	uxth	r3, r10
	cmp	r3, r7
	bcs	.L2491
	mul	r3, ip, r10
	ldr	r1, [r4, #228]
	adds	r0, r1, r3
	ldr	r3, [r1, r3]
	adds	r1, r3, #1
	bne	.L2457
	ldr	r0, [r0, #4]
	add	fp, fp, #1
	str	ip, [sp, #12]
	ubfx	r0, r0, #10, #16
	str	r3, [sp, #8]
	str	r2, [sp, #4]
	bl	FtlBbmMapBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #4]
	ldr	ip, [sp, #12]
	strh	r3, [r2]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	subs	r3, r3, #1
	strb	r3, [r5, #7]
.L2457:
	add	r10, r10, #1
	adds	r2, r2, #2
	b	.L2456
.L2491:
	cmp	fp, #0
	beq	.L2459
	mov	r0, r6
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2459:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L2460
	ldr	r3, [r4, #296]
.L2487:
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L2438
.L2460:
	ldr	r2, .L2492+16
	ldrh	r2, [r2, #2388]
	strh	r6, [r5]	@ movhi
	smulbb	r3, r2, r3
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #508]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #508]
	ldr	r2, [r4, #296]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
.L2437:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2493:
	.align	2
.L2492:
	.word	.LANCHOR2
	.word	.LANCHOR2+412
	.word	.LANCHOR2+308
	.word	.LANCHOR2+316
	.word	.LANCHOR0
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	1
	.global	FtlGcFreeBadSuperBlk
	.thumb
	.thumb_func
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L2507
	str	r0, [sp]
	ldrh	r3, [r5, #1182]
	cmp	r3, #0
	beq	.L2496
	ldr	r7, .L2507+4
	mov	r10, #0
	mov	r8, r5
.L2495:
	ldrh	r2, [r7, #2320]
	uxth	r3, r10
	cmp	r2, r3
	bls	.L2504
	add	r3, r3, r7
	ldr	r1, [sp]
	mov	fp, #0
	ldrb	r0, [r3, #2348]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r2, .L2507+8
	mov	r9, r0
.L2497:
	ldrh	r3, [r5, #1182]
	uxth	r4, fp
	cmp	r3, r4
	bls	.L2505
	lsls	r6, r4, #1
	add	r3, r8, r6
	ldrh	r3, [r3, #1184]
	cmp	r3, r9
	bne	.L2498
	mov	r1, r9
	ldr	r0, .L2507+12
	str	r2, [sp, #4]
	bl	printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r8, #1182]
	ldr	r2, [sp, #4]
	adds	r3, r2, r6
.L2499:
	cmp	r4, r1
	bcs	.L2506
	ldrh	r0, [r3, #2]
	adds	r4, r4, #1
	uxth	r4, r4
	strh	r0, [r3], #2	@ movhi
	b	.L2499
.L2506:
	subs	r1, r1, #1
	strh	r1, [r5, #1182]	@ movhi
.L2498:
	add	fp, fp, #1
	b	.L2497
.L2505:
	add	r10, r10, #1
	b	.L2495
.L2504:
	bl	FtlGcReFreshBadBlk
.L2496:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2508:
	.align	2
.L2507:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+1184
	.word	.LC129
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	1
	.global	update_vpc_list
	.thumb
	.thumb_func
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2518
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, [r2, #296]
	mov	r5, r2
	ldrh	r3, [r3, r0, lsl #1]
	cbnz	r3, .L2510
	ldrh	r1, [r2, #556]
	cmp	r1, r0
	bne	.L2511
	movw	r3, #65535
	strh	r3, [r2, #556]	@ movhi
	b	.L2512
.L2511:
	ldrh	r1, [r2, #316]
	cmp	r1, r0
	beq	.L2517
	ldrh	r1, [r2, #364]
	cmp	r1, r0
	beq	.L2517
	ldrh	r2, [r2, #412]
	cmp	r2, r0
	beq	.L2517
.L2512:
	mov	r1, r4
	ldr	r0, .L2518+4
	bl	List_remove_node
	ldrh	r3, [r5, #304]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #304]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	movs	r0, #1
	pop	{r3, r4, r5, pc}
.L2510:
	bl	List_update_data_list
.L2517:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L2519:
	.align	2
.L2518:
	.word	.LANCHOR2
	.word	.LANCHOR2+292
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	1
	.global	decrement_vpc_count
	.thumb
	.thumb_func
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #65535
	cmp	r0, r3
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r5, .L2530
	beq	.L2521
	ldr	r3, [r5, #296]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L2522
	mov	r1, r4
	ldr	r0, .L2530+4
	bl	printk
	ldr	r3, [r5, #296]
	movs	r2, #32
	add	r0, r5, #308
	mov	r1, r4
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	test_node_in_list
	cbz	r0, .L2523
	mov	r1, r4
	add	r0, r5, #308
	bl	List_remove_node
	ldrh	r3, [r5, #312]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #312]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #296]
	ldr	r0, .L2530+8
	mov	r1, r4
	ldrh	r2, [r3, r4, lsl #1]
	bl	printk
.L2523:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2526
.L2522:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L2521:
	ldrh	r0, [r5, #3460]
	movw	r3, #65535
	ldr	r6, .L2530
	cmp	r0, r3
	bne	.L2525
	strh	r4, [r6, #3460]	@ movhi
	b	.L2526
.L2525:
	cmp	r0, r4
	beq	.L2526
	bl	update_vpc_list
	strh	r4, [r6, #3460]	@ movhi
	adds	r0, r0, #0
	it	ne
	movne	r0, #1
	pop	{r4, r5, r6, pc}
.L2526:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L2531:
	.align	2
.L2530:
	.word	.LANCHOR2
	.word	.LC130
	.word	.LC131
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	1
	.global	FtlSlcSuperblockCheck
	.thumb
	.thumb_func
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldrh	r3, [r0, #4]
	cmp	r3, #0
	beq	.L2532
	ldrh	r3, [r0]
	movw	r5, #65535
	cmp	r3, r5
	beq	.L2532
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r7, .L2544
	adds	r3, r3, #8
	ldr	r6, .L2544+4
	ldrh	r3, [r0, r3, lsl #1]
.L2536:
	cmp	r3, r5
	bne	.L2543
.L2538:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r7, #2320]
	adds	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r2, r3
	itttt	eq
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	it	eq
	strbeq	r3, [r4, #6]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r3, [r4, r3, lsl #1]
	b	.L2536
.L2543:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2539
	ldrb	r3, [r7, #144]	@ zero_extendqisi2
	cbnz	r3, .L2539
	ldrh	r3, [r4, #2]
	add	r3, r6, r3, lsl #1
	ldrh	r3, [r3, #1220]
	cmp	r3, r5
	bne	.L2539
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2538
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L2539:
	ldr	r2, .L2544
	ldrb	r3, [r2, #144]	@ zero_extendqisi2
	cbz	r3, .L2532
	cmp	r1, #1
	bne	.L2532
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #2390]
	cmp	r1, r3
	bcc	.L2532
	ldr	r3, .L2544+4
	ldrh	r0, [r4]
	ldrh	r5, [r4, #4]
	ldr	r1, [r3, #296]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r5
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #2388]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2532:
	pop	{r3, r4, r5, r6, r7, pc}
.L2545:
	.align	2
.L2544:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	1
	.global	get_new_active_ppa
	.thumb
	.thumb_func
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movs	r3, #0
	strb	r3, [r0, #10]
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movw	r7, #65535
	ldr	r8, .L2563+4
	adds	r3, r3, #8
	ldr	r5, .L2563
	ldrh	r2, [r0, r3, lsl #1]
.L2547:
	cmp	r2, r7
	ldr	r6, .L2563+4
	bne	.L2562
.L2548:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r8, #2320]
	adds	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r2, r3
	itttt	eq
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	it	eq
	strbeq	r3, [r4, #6]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r2, [r4, r3, lsl #1]
	b	.L2547
.L2562:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r1, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2550
	ldrb	r0, [r8, #144]	@ zero_extendqisi2
	cbnz	r0, .L2550
	add	r0, r5, r1, lsl #1
	ldrh	r0, [r0, #1220]
	cmp	r0, r7
	bne	.L2550
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2548
.L2550:
	movw	r7, #65535
	orr	r5, r1, r2, lsl #10
	mov	r8, r7
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2551:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r1, [r6, #2320]
.L2553:
	adds	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r1
	itttt	eq
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	add	r2, r3, #8
	ldrh	r2, [r4, r2, lsl #1]
	cmp	r2, r7
	beq	.L2553
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2558
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cbnz	r3, .L2555
	ldrh	r2, [r4, #2]
	ldr	r3, .L2563
	add	r3, r3, r2, lsl #1
	ldrh	r3, [r3, #1220]
	cmp	r3, r8
	bne	.L2555
	ldrh	r3, [r4, #4]
	cbz	r3, .L2555
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2551
.L2555:
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	ldr	r2, .L2563+4
	cbz	r3, .L2558
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #2390]
	cmp	r1, r3
	bcc	.L2558
	ldr	r3, .L2563
	ldrh	r0, [r4]
	ldrh	r6, [r4, #4]
	ldr	r1, [r3, #296]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r6
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #2388]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2558:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L2564:
	.align	2
.L2563:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	1
	.global	FtlVpcTblFlush
	.thumb
	.thumb_func
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldr	r4, .L2588
	ldr	r3, [r4, #224]
	cmp	r3, #0
	bne	.L2567
	ldr	r5, .L2588+4
	movs	r1, #255
	ldr	r2, [r4, #3316]
	ldr	r7, [r4, #3340]
	ldr	r6, .L2588+8
	str	r2, [r5, #1256]
	ldrh	r2, [r4, #540]
	str	r7, [r5, #1260]
	str	r3, [r7, #12]
	strh	r2, [r7, #2]	@ movhi
	movw	r2, #61604
	strh	r2, [r7]	@ movhi
	ldr	r2, [r4, #548]
	str	r3, [r7, #8]
	ldr	r3, .L2588+12
	str	r2, [r7, #4]
	ldrh	r2, [r4, #318]
	str	r3, [r4, #240]
	ldr	r3, .L2588+16
	str	r3, [r4, #244]
	ldrh	r3, [r4, #546]
	strh	r3, [r4, #248]	@ movhi
	ldrh	r3, [r6, #2342]
	strb	r3, [r4, #250]
	ldrh	r3, [r4, #316]
	strh	r3, [r4, #254]	@ movhi
	ldrb	r3, [r4, #322]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #256]	@ movhi
	ldrb	r3, [r4, #324]	@ zero_extendqisi2
	ldrh	r2, [r4, #366]
	strb	r3, [r4, #251]
	ldrh	r3, [r4, #364]
	strh	r3, [r4, #258]	@ movhi
	ldrb	r3, [r4, #370]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #260]	@ movhi
	ldrb	r3, [r4, #372]	@ zero_extendqisi2
	strb	r3, [r4, #252]
	ldrh	r3, [r4, #412]
	strh	r3, [r4, #262]	@ movhi
	ldrh	r2, [r4, #414]
	ldrb	r3, [r4, #418]	@ zero_extendqisi2
	ldr	r0, [r5, #1256]
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #264]	@ movhi
	ldrb	r3, [r4, #420]	@ zero_extendqisi2
	ldrh	r2, [r6, #2398]
	strb	r3, [r4, #253]
	ldr	r3, [r4, #516]
	str	r3, [r4, #272]
	ldr	r3, [r4, #508]
	str	r3, [r4, #280]
	ldr	r3, [r4, #512]
	str	r3, [r4, #276]
	ldrh	r3, [r4, #1174]
	strh	r3, [r4, #284]	@ movhi
	ldrh	r3, [r4, #1176]
	strh	r3, [r4, #286]	@ movhi
	bl	ftl_memset
	add	r1, r4, #240
	movs	r2, #48
	ldr	r0, [r5, #1256]
	bl	ftl_memcpy
	ldrh	r2, [r6, #2328]
	ldr	r0, [r5, #1256]
	ldr	r1, [r4, #296]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r6, #2328]
	ldr	r0, [r5, #1256]
	ldr	r1, [r4, #472]
	lsls	r3, r2, #1
	lsrs	r2, r2, #3
	adds	r3, r3, #51
	adds	r2, r2, #4
	bic	r3, r3, #3
	add	r0, r0, r3
	bl	ftl_memcpy
	ldrh	r3, [r6, #2432]
	cbz	r3, .L2568
	ldrh	r2, [r6, #2328]
	ldr	r0, [r5, #1256]
	ldr	r1, [r4, #3388]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldrh	r2, [r6, #2424]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	lsls	r2, r2, #2
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
.L2568:
	movs	r0, #0
	ldr	r10, .L2588
	bl	FtlUpdateVaildLpn
	ldr	fp, .L2588+8
	mov	r8, #0
	movw	r9, #65535
.L2569:
	ldr	r3, [r4, #3316]
	ldrh	r2, [r4, #540]
	ldrh	r1, [r4, #542]
	str	r3, [r5, #1256]
	ldr	r3, [r4, #3340]
	str	r3, [r5, #1260]
	orr	r3, r1, r2, lsl #10
	str	r3, [r5, #1252]
	ldrh	r3, [r6, #2390]
	subs	r3, r3, #1
	cmp	r1, r3
	blt	.L2570
	movs	r3, #0
	ldrh	r9, [r10, #544]
	strh	r3, [r10, #542]	@ movhi
	strh	r2, [r10, #544]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r10, #508]
	ldr	r1, .L2588+4
	adds	r2, r3, #1
	str	r2, [r10, #508]
	str	r3, [r10, #548]
	lsls	r2, r0, #10
	strh	r0, [r10, #540]	@ movhi
	str	r2, [r1, #1252]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L2570:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cbz	r3, .L2571
	ldr	r0, [r4, #3316]
	ldrh	r1, [fp, #2398]
	bl	js_hash
	str	r0, [r7, #12]
.L2571:
	movs	r1, #1
	ldr	r0, .L2588+20
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #542]
	ldr	r2, [r5, #1248]
	adds	r3, r3, #1
	adds	r1, r2, #1
	uxth	r3, r3
	strh	r3, [r4, #542]	@ movhi
	bne	.L2572
	cmp	r3, #1
	add	r8, r8, #1
	it	eq
	ldrheq	r3, [r6, #2390]
	uxth	r8, r8
	itt	eq
	addeq	r3, r3, #-1
	strheq	r3, [r10, #542]	@ movhi
	cmp	r8, #3
	bls	.L2569
	ldr	r0, .L2588+24
	mov	r2, r8
	ldr	r1, [r5, #1252]
	bl	printk
	movs	r3, #1
	str	r3, [r4, #224]
	b	.L2567
.L2572:
	cmp	r3, #1
	beq	.L2569
	cmp	r2, #256
	beq	.L2569
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2567
	mov	r0, r9
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2567:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2589:
	.align	2
.L2588:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177379
	.word	.LANCHOR4+1248
	.word	.LC132
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	1
	.global	FtlSuperblockPowerLostFix
	.thumb
	.thumb_func
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r5, .L2606
	.pad #44
	sub	sp, sp, #44
	ldr	r3, [r5, #224]
	cmp	r3, #0
	bne	.L2590
	ldr	r2, .L2606+4
	ldrb	r6, [r2, #144]	@ zero_extendqisi2
	mov	r8, r2
	cbz	r6, .L2605
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	bne	.L2601
	ldrh	fp, [r0, #4]
	b	.L2592
.L2601:
	mov	r6, r3
.L2605:
	mov	fp, #12
.L2592:
	ldr	r7, [r5, #3340]
	mov	r3, #-1
	str	r3, [sp, #20]
	mov	r9, #0
	ldr	r3, [r5, #3316]
	movw	r2, #61589
	str	r7, [sp, #16]
	ldr	r10, .L2606
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r7, #8]
	mvn	r3, #1
	str	r3, [r7, #12]
	ldrh	r3, [r4]
	strh	r9, [r7]	@ movhi
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #3316]
	str	r2, [r3]
	add	r2, r2, #304087040
	ldr	r3, [r5, #3316]
	add	r2, r2, #1269760
	addw	r2, r2, #1507
	str	r2, [r3, #4]
.L2593:
	adds	fp, fp, #-1
	bcc	.L2596
	ldrh	r3, [r4, #4]
	cbnz	r3, .L2594
.L2596:
	ldr	r2, [r5, #296]
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r8, #2388]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L2590
.L2594:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L2596
	ldr	r3, [r5, #512]
	movs	r1, #1
	add	r0, sp, #4
	str	r3, [r7, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, r6
	it	eq
	moveq	r3, r9
	str	r3, [r10, #512]
	movs	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2593
.L2590:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2607:
	.align	2
.L2606:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	1
	.global	ftl_map_blk_gc
	.thumb
	.thumb_func
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldr	r5, [r0, #12]
	ldr	r8, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	subs	r3, r3, #4
	cmp	r2, r3
	blt	.L2609
	uxth	r0, r0
	ldrh	r10, [r5, r0, lsl #1]
	cmp	r10, #0
	beq	.L2609
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2609
	movs	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L2621
	ldrh	r3, [r3, #2390]
	cmp	r2, r3
	bcc	.L2610
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2610:
	ldr	r6, .L2621+4
	mov	fp, #0
	ldr	r9, .L2621+16
.L2611:
	ldrh	r2, [r4, #6]
	uxth	r5, fp
	cmp	r2, r5
	bls	.L2620
	ldr	r2, [r8, r5, lsl #2]
	add	ip, r8, r5, lsl #2
	cmp	r10, r2, lsr #10
	bne	.L2612
	ldr	r2, [r9, #3320]
	movs	r1, #1
	ldr	r7, [r9, #3340]
	ldr	r0, .L2621+8
	str	r2, [r6, #1256]
	str	r7, [r6, #1260]
	ldr	r2, [r8, r5, lsl #2]
	str	ip, [sp, #4]
	str	r2, [r6, #1252]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r6, #1248]
	adds	r2, r2, #1
	ldr	ip, [sp, #4]
	bne	.L2613
.L2615:
	movs	r3, #0
	str	r3, [ip]
	ldr	r3, .L2621+4
	ldrh	r2, [r7, #8]
	ldr	r0, .L2621+12
	ldr	r1, [r3, #1252]
	bl	printk
	ldr	r3, .L2621+16
	movs	r2, #1
	str	r2, [r3, #224]
	b	.L2614
.L2613:
	ldrh	r1, [r7, #8]
	cmp	r1, r5
	bne	.L2615
	ldrh	r0, [r7]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2615
	mov	r0, r4
	ldr	r2, [r6, #1256]
	bl	FtlMapWritePage
.L2612:
	add	fp, fp, #1
	b	.L2611
.L2620:
	mov	r0, r10
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L2609:
	ldr	r3, .L2621
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #2390]
	cmp	r2, r3
	bcc	.L2614
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2614:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2622:
	.align	2
.L2621:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR4+1248
	.word	.LC133
	.word	.LANCHOR2
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	1
	.global	Ftl_write_map_blk_to_last_page
	.thumb
	.thumb_func
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r6, .L2634
	ldr	r7, [r0, #12]
	ldr	r10, [r0, #24]
	ldr	r5, [r6, #224]
	cmp	r5, #0
	bne	.L2624
	ldrh	r3, [r0]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L2625
	ldrh	r3, [r0, #8]
	adds	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r7]	@ movhi
	ldr	r3, [r4, #28]
	strh	r5, [r4, #2]	@ movhi
	adds	r3, r3, #1
	strh	r5, [r4]	@ movhi
	str	r3, [r4, #28]
	b	.L2624
.L2625:
	ldrh	r9, [r7, r3, lsl #1]
	movs	r1, #255
	ldr	fp, .L2634+12
	ldrh	r3, [r0, #2]
	ldr	r7, [r6, #3340]
	orr	r3, r3, r9, lsl #10
	str	r3, [fp, #1252]
	ldr	r3, [r6, #3316]
	str	r7, [fp, #1260]
	ldr	r8, .L2634+4
	str	r3, [fp, #1256]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	movw	r3, #64245
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r9, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	ldrh	r2, [r8, #2390]
	ldr	r0, [r6, #3316]
	lsls	r2, r2, #3
	bl	ftl_memset
	mov	r1, r5
.L2626:
	ldrh	r2, [r4, #6]
	uxth	r3, r1
	cmp	r2, r3
	bls	.L2633
	ldr	r2, [r10, r3, lsl #2]
	cmp	r9, r2, lsr #10
	bne	.L2627
	adds	r5, r5, #1
	ldr	r2, [r6, #3316]
	uxth	r5, r5
	str	r3, [r2, r5, lsl #3]
	ldr	r2, [r10, r3, lsl #2]
	ldr	r3, [r6, #3316]
	add	r3, r3, r5, lsl #3
	str	r2, [r3, #4]
.L2627:
	adds	r1, r1, #1
	b	.L2626
.L2633:
	ldrb	r3, [r8]	@ zero_extendqisi2
	cbz	r3, .L2629
	ldr	r3, .L2634+4
	ldr	r0, [fp, #1256]
	ldrh	r1, [r3, #2398]
	bl	js_hash
	str	r0, [r7, #12]
.L2629:
	movs	r1, #1
	movs	r3, #0
	ldr	r0, .L2634+8
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2624:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2635:
	.align	2
.L2634:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4+1248
	.word	.LANCHOR4
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	1
	.global	FtlMapWritePage
	.thumb
	.thumb_func
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r6, #0
	ldr	r7, .L2660
	mov	r4, r0
	mov	r9, r1
	str	r2, [sp]
.L2637:
	ldr	r3, [r7, #492]
	ldr	fp, .L2660+4
	adds	r3, r3, #1
	str	r3, [r7, #492]
	ldr	r3, .L2660+4
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #2390]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L2638
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2639
.L2638:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2639:
	ldr	r3, .L2660
	ldr	r1, [r3, #224]
	cmp	r1, #0
	bne	.L2655
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldr	r5, .L2660+8
	ldr	r0, [r7, #3340]
	ldrh	r8, [r3, r2, lsl #1]
	movs	r2, #16
	ldrh	r3, [r4, #2]
	mov	r10, r5
	str	r0, [r5, #1260]
	orr	r3, r3, r8, lsl #10
	str	r3, [r5, #1252]
	ldr	r3, [sp]
	str	r3, [r5, #1256]
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	r3, [r5, #1260]
	str	r2, [r3, #4]
	strh	r9, [r3, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r8, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrb	r2, [fp]	@ zero_extendqisi2
	cbz	r2, .L2641
	str	r3, [sp, #4]
	ldr	r3, .L2660+4
	ldr	r0, [r5, #1256]
	ldrh	r1, [r3, #2398]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2641:
	ldr	ip, .L2660+8
	movs	r1, #1
	add	r0, ip, #1248
	mov	r2, r1
	mov	r3, r1
	str	ip, [sp, #4]
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #1248]
	adds	r1, r2, #1
	ldr	ip, [sp, #4]
	bne	.L2642
	ldr	r0, .L2660+12
	adds	r6, r6, #1
	ldr	r1, [ip, #1252]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [fp, #2390]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L2637
	mov	r2, r6
	ldr	r0, .L2660+16
	ldr	r1, [r10, #1252]
	bl	printk
	ldr	r3, .L2660
	movs	r2, #1
	str	r2, [r3, #224]
	b	.L2655
.L2642:
	cbz	r2, .L2645
	cmp	r3, #1
	strh	r8, [r4, #40]	@ movhi
	bne	.L2646
.L2647:
	movs	r3, #0
	str	r3, [r4, #36]
	b	.L2637
.L2646:
	cmp	r2, #256
	b	.L2659
.L2645:
	cmp	r3, #1
.L2659:
	beq	.L2647
	ldr	r3, [r4, #36]
	cmp	r3, #0
	bne	.L2647
	ldr	r2, [r10, #1252]
	ldr	r3, [r4, #24]
	str	r2, [r3, r9, lsl #2]
.L2655:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2661:
	.align	2
.L2660:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC134
	.word	.LC135
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	1
	.global	flush_l2p_region
	.thumb
	.thumb_func
	.type	flush_l2p_region, %function
flush_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r4, #12
	ldr	r5, .L2663
	muls	r4, r0, r4
	add	r0, r5, #3408
	ldr	r3, [r5, #460]
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #460]
	movs	r0, #0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L2664:
	.align	2
.L2663:
	.word	.LANCHOR2
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	1
	.global	FtlMapTblRecovery
	.thumb
	.thumb_func
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r3, [r0, #16]
	mov	r4, r0
	ldrh	r9, [r0, #6]
	movs	r1, #0
	ldr	r7, [r0, #24]
	str	r3, [sp, #12]
	ldrh	r3, [r0, #8]
	lsl	r2, r9, #2
	ldr	r8, [r0, #12]
	mov	r0, r7
	ldr	r10, .L2707+12
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, .L2707
	movs	r0, #1
	ldr	r2, .L2707+4
	str	r0, [r4, #36]
	ldr	r1, [r3, #3316]
	ldr	r6, [r3, #3340]
	mov	r5, r2
	str	r1, [r2, #1256]
	movw	r1, #65535
	str	r6, [r2, #1260]
	strh	r1, [r4]	@ movhi
	strh	r1, [r4, #2]	@ movhi
	movs	r1, #0
	str	r1, [r4, #32]
	str	r1, [r4, #28]
	str	r1, [sp, #4]
	str	r3, [sp, #20]
.L2666:
	ldrh	r2, [sp, #4]
	ldr	r1, [sp, #8]
	ldr	fp, .L2707+12
	sxth	r3, r2
	cmp	r3, r1
	bge	.L2684
	ldr	r1, [sp, #8]
	subs	r1, r1, #1
	cmp	r3, r1
	lsl	r1, r3, #1
	bne	.L2667
	ldrh	r0, [r8, r3, lsl #1]
	str	r2, [sp, #16]
	add	r2, r8, r1
	movs	r1, #1
	str	r2, [sp, #4]
	str	r3, [sp, #8]
	bl	FtlGetLastWrittenPage
	ldr	r10, .L2707+4
	mov	r8, #0
	ldr	r2, [sp, #16]
	adds	r1, r0, #1
	ldr	r3, [sp, #12]
	sxth	r0, r0
	strh	r1, [r4, #2]	@ movhi
	strh	r2, [r4]	@ movhi
	mov	r2, r3
	ldr	r3, [sp, #8]
	ldr	r3, [r2, r3, lsl #2]
	str	r3, [r4, #28]
	adds	r3, r0, #1
	str	r3, [sp, #8]
.L2668:
	ldr	r3, [sp, #8]
	sxth	r2, r8
	cmp	r2, r3
	bge	.L2684
	ldr	r3, [sp, #4]
	ldr	r0, .L2707+8
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r5, #1252]
	mov	r2, r1
	bl	FlashReadPages
	ldrb	r2, [fp]	@ zero_extendqisi2
	cbz	r2, .L2669
	ldr	r2, [r10, #1260]
	ldr	r2, [r2, #12]
	cbz	r2, .L2669
	ldr	r1, .L2707+12
	ldr	r0, [r10, #1256]
	str	r2, [sp, #12]
	ldrh	r1, [r1, #2398]
	bl	js_hash
	ldr	r2, [sp, #12]
	cmp	r2, r0
	itt	ne
	movne	r2, #-1
	strne	r2, [r10, #1248]
.L2669:
	ldr	r2, [r10, #1248]
	adds	r2, r2, #1
	beq	.L2670
	ldrh	r2, [r6, #8]
	cmp	r2, r9
	bcs	.L2670
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	itt	eq
	ldreq	r1, [r10, #1252]
	streq	r1, [r7, r2, lsl #2]
.L2670:
	add	r8, r8, #1
	b	.L2668
.L2684:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [fp, #2390]
	cmp	r2, r3
	bne	.L2673
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2673
.L2667:
	ldr	r2, [sp, #20]
	ldr	r0, .L2707+8
	ldr	r2, [r2, #3316]
	str	r2, [r5, #1256]
	add	r2, r8, r1
	str	r2, [sp, #16]
	movs	r1, #1
	ldrh	r2, [r8, r3, lsl #1]
	ldrh	r3, [r10, #2390]
	subs	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	mov	r2, r1
	str	r3, [r5, #1252]
	bl	FlashReadPages
	ldr	r3, [r5, #1248]
	adds	r3, r3, #1
	beq	.L2686
	ldrh	r2, [r6]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2686
	ldrh	r2, [r6, #8]
	movw	r3, #64245
	cmp	r2, r3
	bne	.L2686
	movs	r1, #0
	mov	lr, #8
	mov	ip, #4
.L2675:
	uxth	r3, r1
	ldrh	r2, [r10, #2390]
	sxth	r0, r3
	subs	r2, r2, #1
	cmp	r0, r2
	bge	.L2678
	ldr	r2, .L2707
	adds	r1, r1, #1
	ldr	r2, [r2, #3316]
	ldrh	r0, [r2, r0, lsl #3]
	cmp	r0, r9
	ittt	cc
	smlabbcc	r3, r3, lr, ip
	ldrcc	r3, [r2, r3]
	strcc	r3, [r7, r0, lsl #2]
	b	.L2675
.L2686:
	ldr	r3, .L2707+4
	mov	fp, #0
.L2706:
	ldrh	r1, [r10, #2390]
	sxth	r2, fp
	cmp	r2, r1
	bge	.L2678
	ldr	r1, [sp, #16]
	ldr	r0, .L2707+8
	str	r3, [sp, #24]
	ldrh	r1, [r1]
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r3, #1252]
	mov	r2, r1
	bl	FlashReadPages
	ldrb	r2, [r10]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cbz	r2, .L2679
	ldr	r2, [r3, #1260]
	ldr	r2, [r2, #12]
	cbz	r2, .L2679
	ldr	r0, [r3, #1256]
	ldrh	r1, [r10, #2398]
	str	r2, [sp, #28]
	bl	js_hash
	ldr	r2, [sp, #28]
	ldr	r3, [sp, #24]
	cmp	r2, r0
	itt	ne
	movne	r2, #-1
	strne	r2, [r3, #1248]
.L2679:
	ldr	r2, .L2707+4
	ldr	r2, [r2, #1248]
	adds	r2, r2, #1
	beq	.L2680
	ldrh	r2, [r6, #8]
	cmp	r2, r9
	bcs	.L2680
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	itt	eq
	ldreq	r1, [r3, #1252]
	streq	r1, [r7, r2, lsl #2]
.L2680:
	add	fp, fp, #1
	b	.L2706
.L2678:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
	b	.L2666
.L2673:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2708:
	.align	2
.L2707:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR4+1248
	.word	.LANCHOR0
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	1
	.global	FtlLoadVonderInfo
	.thumb
	.thumb_func
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	ldr	r3, .L2710
	ldr	r0, .L2710+4
	ldrh	r2, [r3, #2408]
	add	r0, r0, #1288
	strh	r2, [r0, #10]	@ movhi
	movw	r2, #61574
	strh	r2, [r0, #4]	@ movhi
	ldrh	r2, [r3, #2434]
	strh	r2, [r0, #8]	@ movhi
	ldrh	r2, [r3, #2410]
	ldr	r3, [r3, #2436]
	strh	r2, [r0, #6]	@ movhi
	str	r3, [r0, #12]
	ldr	r3, .L2710+8
	ldr	r2, [r3, #3380]
	str	r2, [r0, #16]
	ldr	r2, [r3, #3376]
	ldr	r3, [r3, #3384]
	str	r2, [r0, #20]
	str	r3, [r0, #24]
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2711:
	.align	2
.L2710:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	1
	.global	FtlLoadMapInfo
	.thumb
	.thumb_func
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	FtlL2PDataInit
	ldr	r0, .L2713
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2714:
	.align	2
.L2713:
	.word	.LANCHOR2+3408
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	1
	.global	FtlVendorPartWrite
	.thumb
	.thumb_func
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2728
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	mov	r4, r1
	add	r0, r0, r1
	ldrh	r1, [r3, #2384]
	.pad #60
	sub	sp, sp, #60
	mov	r10, r2
	ldrh	r2, [r3, #2396]
	cmp	r0, r1
	str	r3, [sp, #4]
	bhi	.L2723
	ldr	r7, .L2728+4
	lsr	r6, r5, r2
	mov	fp, #0
	lsls	r3, r6, #2
	str	r3, [sp]
.L2717:
	cmp	r4, #0
	beq	.L2727
	ldr	r3, .L2728+4
	mov	r0, r5
	ldr	r2, [sp]
	ldr	r3, [r3, #3384]
	ldr	ip, [r3, r2]
	ldr	r3, [sp, #4]
	str	ip, [sp, #12]
	ldrh	r2, [r3, #2394]
	mov	r1, r2
	str	r2, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #8]
	mov	r8, r1
	ldr	ip, [sp, #12]
	subs	r3, r2, r1
	uxth	r9, r3
	cmp	r9, r4
	it	hi
	uxthhi	r9, r4
	cmp	ip, #0
	beq	.L2719
	cmp	r9, r2
	beq	.L2719
	ldr	r2, [r7, #3324]
	movs	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	movs	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2720
.L2719:
	ldr	r3, [sp, #4]
	movs	r1, #0
	ldr	r0, [r7, #3324]
	ldrh	r2, [r3, #2398]
	bl	ftl_memset
.L2720:
	lsl	ip, r9, #9
	ldr	r0, [r7, #3324]
	uxth	r8, r8
	mov	r1, r10
	mov	r2, ip
	str	ip, [sp, #8]
	add	r0, r0, r8, lsl #9
	rsb	r4, r9, r4
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r0, .L2728+8
	adds	r6, r6, #1
	ldr	r2, [r7, #3324]
	add	r5, r5, r9
	bl	FtlMapWritePage
	ldr	r3, [sp]
	adds	r0, r0, #1
	ldr	ip, [sp, #8]
	it	eq
	moveq	fp, #-1
	adds	r3, r3, #4
	str	r3, [sp]
	add	r10, r10, ip
	b	.L2717
.L2727:
	mov	r0, fp
	b	.L2716
.L2723:
	mov	r0, #-1
.L2716:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2729:
	.align	2
.L2728:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4+1288
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	1
	.global	Ftl_save_ext_data
	.thumb
	.thumb_func
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L2732
	ldr	r3, .L2732+4
	ldr	r1, [r2, #608]
	cmp	r1, r3
	bne	.L2730
	ldr	r3, .L2732+8
	movs	r0, #0
	movs	r1, #1
	str	r3, [r2, #612]
	ldr	r3, [r2, #500]
	str	r3, [r2, #696]
	ldr	r3, [r2, #504]
	str	r3, [r2, #700]
	ldr	r3, [r2, #496]
	str	r3, [r2, #616]
	ldr	r3, [r2, #484]
	str	r3, [r2, #620]
	ldr	r3, [r2, #476]
	str	r3, [r2, #624]
	ldr	r3, [r2, #492]
	str	r3, [r2, #628]
	ldr	r3, [r2, #520]
	str	r3, [r2, #636]
	ldr	r3, [r2, #236]
	add	r2, r2, #608
	str	r3, [r2, #32]
	ldr	r3, [r2, #-128]
	str	r3, [r2, #36]
	ldr	r3, [r2, #-120]
	str	r3, [r2, #40]
	ldr	r3, [r2, #-80]
	str	r3, [r2, #44]
	ldr	r3, [r2, #-76]
	str	r3, [r2, #48]
	ldr	r3, [r2, #-4]
	str	r3, [r2, #60]
	ldr	r3, [r2, #2668]
	str	r3, [r2, #64]
	b	FtlVendorPartWrite
.L2730:
	bx	lr
.L2733:
	.align	2
.L2732:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177379
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	1
	.global	FtlEctTblFlush
	.thumb
	.thumb_func
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2741
	push	{r4, lr}
	.save {r4, lr}
	ldr	r2, [r3, #2244]
	ldr	r3, .L2741+4
	cbz	r2, .L2739
	ldr	r2, [r3, #532]
	cmp	r2, #39
	ite	hi
	movhi	r1, #32
	movls	r1, #4
	b	.L2735
.L2739:
	movs	r1, #32
.L2735:
	ldr	r4, .L2741+8
	ldrh	r2, [r4, #1332]
	cmp	r2, #31
	ittt	ls
	addls	r2, r2, #1
	movls	r1, #1
	strhls	r2, [r4, #1332]	@ movhi
	cbnz	r0, .L2737
	ldr	r0, [r3, #3360]
	ldr	r2, [r0, #20]
	add	r2, r2, r1
	ldr	r1, [r0, #16]
	cmp	r1, r2
	bcc	.L2738
.L2737:
	ldr	r2, [r3, #3360]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2741+12
	str	r1, [r2]
	ldr	r2, [r3, #3360]
	ldrh	r1, [r3, #3352]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2738:
	movs	r0, #0
	pop	{r4, pc}
.L2742:
	.align	2
.L2741:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	1112818501
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	1
	.global	FtlVendorPartRead
	.thumb
	.thumb_func
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2754
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	mov	r4, r1
	add	r0, r0, r1
	ldrh	r1, [r3, #2384]
	.pad #60
	sub	sp, sp, #60
	mov	r7, r2
	ldrh	r2, [r3, #2396]
	cmp	r0, r1
	str	r3, [sp, #12]
	bhi	.L2752
	ldr	r10, .L2754+4
	lsr	r5, r6, r2
	mov	fp, #0
	lsls	r3, r5, #2
	str	r3, [sp, #4]
.L2745:
	cmp	r4, #0
	beq	.L2753
	ldr	r3, .L2754+4
	mov	r0, r6
	ldr	r2, [sp, #4]
	ldr	r3, [r3, #3384]
	ldr	r8, [r3, r2]
	ldr	r3, [sp, #12]
	ldrh	r9, [r3, #2394]
	mov	r1, r9
	bl	__aeabi_uidivmod
	rsb	r3, r1, r9
	str	r1, [sp, #8]
	uxth	r9, r3
	cmp	r9, r4
	it	hi
	uxthhi	r9, r4
	cmp	r8, #0
	beq	.L2747
	ldr	r2, [r10, #3324]
	movs	r1, #1
	add	r0, sp, #20
	str	r8, [sp, #24]
	str	r2, [sp, #28]
	movs	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, .L2754+8
	ldr	r2, [sp, #20]
	adds	r2, r2, #1
	ldr	r2, [r3, #1248]
	it	eq
	moveq	fp, #-1
	cmp	r2, #256
	bne	.L2749
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L2754+12
	bl	printk
	ldr	r0, .L2754+16
	mov	r1, r5
	ldr	r2, [r10, #3324]
	bl	FtlMapWritePage
.L2749:
	ldrh	r8, [sp, #8]
	mov	r0, r7
	ldr	r1, [r10, #3324]
	lsl	r2, r9, #9
	add	r1, r1, r8, lsl #9
	bl	ftl_memcpy
	b	.L2750
.L2747:
	mov	r0, r7
	mov	r1, r8
	lsl	r2, r9, #9
	bl	ftl_memset
.L2750:
	ldr	r3, [sp, #4]
	adds	r5, r5, #1
	rsb	r4, r9, r4
	add	r6, r6, r9
	adds	r3, r3, #4
	add	r7, r7, r9, lsl #9
	str	r3, [sp, #4]
	b	.L2745
.L2753:
	mov	r0, fp
	b	.L2744
.L2752:
	mov	r0, #-1
.L2744:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2755:
	.align	2
.L2754:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC136
	.word	.LANCHOR4+1288
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	1
	.global	FtlLoadEctTbl
	.thumb
	.thumb_func
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movs	r0, #64
	ldr	r4, .L2758
	ldr	r2, [r4, #3360]
	ldrh	r1, [r4, #3352]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3360]
	ldr	r2, [r3]
	ldr	r3, .L2758+4
	cmp	r2, r3
	beq	.L2757
	ldr	r1, .L2758+8
	ldr	r0, .L2758+12
	bl	printk
	ldrh	r2, [r4, #3352]
	ldr	r0, [r4, #3360]
	movs	r1, #0
	lsls	r2, r2, #9
	bl	ftl_memset
.L2757:
	movs	r0, #0
	pop	{r4, pc}
.L2759:
	.align	2
.L2758:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC137
	.word	.LC76
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	1
	.global	Ftl_load_ext_data
	.thumb
	.thumb_func
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r0, #0
	ldr	r4, .L2765
	movs	r1, #1
	ldr	r5, .L2765+4
	add	r2, r4, #608
	bl	FtlVendorPartRead
	ldr	r3, [r4, #608]
	cmp	r3, r5
	beq	.L2761
	add	r0, r4, #608
	movs	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #608]
.L2761:
	ldr	r2, [r4, #608]
	ldr	r3, .L2765
	cmp	r2, r5
	bne	.L2762
	ldr	r2, [r3, #696]
	str	r2, [r3, #500]
	ldr	r2, [r3, #700]
	str	r2, [r3, #504]
	ldr	r2, [r3, #616]
	str	r2, [r3, #496]
	ldr	r2, [r3, #620]
	str	r2, [r3, #484]
	ldr	r2, [r3, #624]
	str	r2, [r3, #476]
	ldr	r2, [r3, #628]
	str	r2, [r3, #492]
	ldr	r2, [r3, #636]
	str	r2, [r3, #520]
	ldr	r2, [r3, #640]
	str	r2, [r3, #236]
	ldr	r2, [r3, #644]
	str	r2, [r3, #480]
	ldr	r2, [r3, #648]
	str	r2, [r3, #488]
	ldr	r2, [r3, #652]
	str	r2, [r3, #528]
	ldr	r2, [r3, #656]
	str	r2, [r3, #532]
	ldr	r2, [r3, #668]
	str	r2, [r3, #604]
.L2762:
	ldr	r1, [r4, #676]
	movs	r3, #0
	ldr	r2, .L2765+8
	str	r3, [r4, #3276]
	cmp	r1, r2
	ldr	r5, .L2765+12
	bne	.L2763
	ldrb	r2, [r5, #144]	@ zero_extendqisi2
	cbz	r2, .L2764
	ldr	r2, .L2765
	str	r3, [r2, #676]
	bl	Ftl_save_ext_data
	b	.L2763
.L2764:
	ldr	r0, .L2765+16
	movs	r3, #1
	ldr	r1, .L2765+20
	str	r3, [r5, #2244]
	bl	printk
.L2763:
	ldrh	r2, [r5, #2380]
	ldr	r3, [r4, #520]
	ldr	r0, [r4, #516]
	ldrh	r1, [r5, #2328]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #524]
	pop	{r3, r4, r5, pc}
.L2766:
	.align	2
.L2765:
	.word	.LANCHOR2
	.word	1179929683
	.word	305432421
	.word	.LANCHOR0
	.word	.LC76
	.word	.LC138
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	1
	.global	FtlMapBlkWriteDumpData
	.thumb
	.thumb_func
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r3, [r0, #36]
	ldrh	r5, [r0, #6]
	ldr	r1, [r0, #24]
	cbz	r3, .L2767
	ldr	r2, .L2773
	movs	r3, #0
	str	r3, [r0, #36]
	ldr	r3, [r2, #224]
	cbnz	r3, .L2767
	subs	r5, r5, #1
	ldr	r3, .L2773+4
	ldr	r0, [r2, #3320]
	ldr	r2, [r2, #3340]
	uxth	r5, r5
	mov	r4, r3
	str	r0, [r3, #1256]
	str	r2, [r3, #1260]
	ldr	r2, [r1, r5, lsl #2]
	str	r2, [r3, #1252]
	cbz	r2, .L2771
	movs	r1, #1
	add	r0, r3, #1248
	mov	r2, r1
	bl	FlashReadPages
	b	.L2772
.L2771:
	ldr	r3, .L2773+8
	movs	r1, #255
	ldrh	r2, [r3, #2398]
	bl	ftl_memset
.L2772:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #1256]
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L2767:
	pop	{r4, r5, r6, pc}
.L2774:
	.align	2
.L2773:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	1
	.global	FlashReadFacBbtData
	.thumb
	.thumb_func
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2789
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldrh	r2, [r3, #128]
	mov	r7, r1
	ldrh	r3, [r3, #130]
	.pad #40
	sub	sp, sp, #40
	mov	r5, r0
	smulbb	r3, r2, r3
	ldr	r2, .L2789+4
	ldr	r1, [r2, #1184]
	mov	r9, r2
	uxth	r3, r3
	subs	r6, r3, #1
	str	r1, [sp, #12]
	mul	r10, r3, r7
	ldr	r1, [r2, #1216]
	uxth	r6, r6
	sub	r4, r3, #16
	str	r1, [sp, #16]
.L2776:
	cmp	r6, r4
	ble	.L2788
	movs	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L2777
	ldr	r3, [r9, #1216]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2777
	cbz	r5, .L2783
	cbz	r7, .L2784
.L2781:
	ldr	r1, [r9, #1184]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	movs	r2, #4
	ldr	r0, .L2789+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	movs	r0, #0
	b	.L2778
.L2784:
	ldr	r6, .L2789+4
	mov	lr, #1
.L2779:
	ldr	r2, [r9, #1204]
	uxth	r3, r7
	adds	r7, r7, #1
	cmp	r3, r2
	bcs	.L2781
	ldr	r2, [r6, #1184]
	lsrs	r1, r3, #5
	and	r4, r3, #31
	lsl	r3, lr, r4
	ldr	r0, [r2, r1, lsl #2]
	orrs	r3, r3, r0
	str	r3, [r2, r1, lsl #2]
	b	.L2779
.L2777:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L2776
.L2788:
	mov	r0, #-1
	b	.L2778
.L2783:
	mov	r0, r5
.L2778:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2790:
	.align	2
.L2789:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC139
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	1
	.global	FlashGetBadBlockList
	.thumb
	.thumb_func
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2801
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #44]
	ldr	r6, .L2801+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #1212]
	smulbb	r4, r4, r3
	uxth	r4, r4
	adds	r2, r4, #7
	asrs	r2, r2, #3
	bl	FlashReadFacBbtData
	adds	r0, r0, #1
	bne	.L2792
.L2796:
	movs	r3, #0
	b	.L2793
.L2792:
	movs	r1, #0
	lsr	lr, r4, #4
	mov	ip, #1
	subs	r4, r4, #1
	mov	r3, r1
.L2794:
	uxth	r2, r1
	cmp	r2, r4
	bge	.L2793
	ldr	r7, [r6, #1212]
	lsrs	r0, r2, #5
	and	r8, r2, #31
	adds	r1, r1, #1
	lsl	r8, ip, r8
	ldr	r0, [r7, r0, lsl #2]
	tst	r8, r0
	ittt	ne
	strhne	r2, [r5, r3, lsl #1]	@ movhi
	addne	r0, r3, #1
	uxthne	r3, r0
	cmp	r3, lr
	bcc	.L2794
	b	.L2796
.L2793:
	movw	r2, #65535
	movs	r0, #0
	strh	r2, [r5, r3, lsl #1]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L2802:
	.align	2
.L2801:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	1
	.global	FtlMakeBbt
	.thumb
	.thumb_func
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L2827
	ldr	r4, [r5, #224]
	cmp	r4, #0
	bne	.L2804
	bl	FtlBbtMemInit
	ldr	r6, .L2827+4
	bl	FtlLoadFactoryBbt
	ldr	r9, .L2827+16
	sub	r8, r6, #18
	sub	r7, r6, #2480
.L2805:
	ldrh	r3, [r7, #2342]
	ldr	r10, .L2827+8
	cmp	r4, r3
	bcs	.L2824
	ldr	r3, [r5, #3340]
	movw	r2, #65535
	ldr	r0, [r5, #3316]
	ldr	fp, .L2827+16
	str	r3, [sp]
	str	r3, [r9, #1260]
	ldrh	r3, [r8, #2]!
	str	r0, [r9, #1256]
	cmp	r3, r2
	beq	.L2806
	ldrh	ip, [r7, #2386]
	movs	r1, #1
	add	r0, fp, #1248
	mov	r2, r1
	mla	ip, ip, r4, r3
	lsl	r3, ip, #10
	str	ip, [sp]
	str	r3, [r9, #1252]
	bl	FlashReadPages
	ldrh	r2, [r7, #2386]
	ldr	r0, [r6]
	adds	r2, r2, #7
	ldr	r1, [r9, #1256]
	asrs	r2, r2, #3
	bl	ftl_memcpy
	ldr	ip, [sp]
	b	.L2807
.L2806:
	mov	r1, r4
	bl	FlashGetBadBlockList
	ldr	r0, [r9, #1256]
	ldr	r1, [r6]
	bl	FtlBbt2Bitmap
	ldrh	r3, [r7, #2386]
	subs	r3, r3, #1
.L2823:
	uxth	r10, r3
.L2808:
	ldr	r3, .L2827+8
	ldrh	r0, [r3, #2386]
	smlabb	r0, r0, r4, r10
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	bne	.L2825
	add	r3, r10, #-1
	b	.L2823
.L2825:
	movs	r1, #0
	movs	r2, #16
	strh	r10, [r8]	@ movhi
	ldr	r0, [r5, #3340]
	bl	ftl_memset
	movw	r3, #61664
	ldr	r2, [sp]
	strh	r3, [r2]	@ movhi
	movs	r3, #0
	str	r3, [r2, #4]
	ldrh	r3, [r8]
	ldrh	ip, [r7, #2386]
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r8]
	ldrh	r2, [r5, #3404]
	ldr	r1, [r6]
	mla	ip, ip, r4, r3
	lsls	r2, r2, #2
	ldr	r0, [fp, #1256]
	lsl	r3, ip, #10
	str	ip, [sp, #4]
	str	r3, [fp, #1252]
	bl	ftl_memcpy
	movs	r1, #1
	ldr	r0, .L2827+12
	mov	r2, r1
	bl	FlashEraseBlocks
	movs	r1, #1
	ldr	r0, .L2827+12
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [fp, #1248]
	adds	r3, r3, #1
	ldr	ip, [sp, #4]
	bne	.L2807
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L2808
.L2807:
	uxth	r0, ip
	adds	r4, r4, #1
	bl	FtlBbmMapBadBlock
	adds	r6, r6, #4
	b	.L2805
.L2824:
	movs	r4, #0
.L2812:
	ldrh	r3, [r10, #2402]
	uxth	r0, r4
	ldr	r6, .L2827+8
	adds	r4, r4, #1
	cmp	r3, r0
	bls	.L2826
	bl	FtlBbmMapBadBlock
	b	.L2812
.L2826:
	ldrh	r4, [r6, #2464]
	movw	r7, #65535
	subs	r4, r4, #1
	uxth	r4, r4
.L2814:
	ldrh	r3, [r10, #2464]
	subs	r3, r3, #48
	cmp	r4, r3
	ble	.L2818
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2815
	mov	r0, r4
	bl	FlashTestBlk
	cbz	r0, .L2816
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L2815
.L2816:
	ldrh	r3, [r6, #2452]
	cmp	r3, r7
	bne	.L2817
	strh	r4, [r6, #2452]	@ movhi
	b	.L2815
.L2817:
	ldr	r3, .L2827+8
	strh	r4, [r3, #2456]	@ movhi
	b	.L2818
.L2815:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L2814
.L2818:
	ldr	r0, [r5, #228]
	movs	r4, #0
	ldrh	r3, [r10, #2452]
	movs	r2, #2
	str	r4, [r10, #2460]
	movs	r1, #1
	strh	r4, [r10, #2454]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r10, #2456]
	lsls	r3, r3, #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r10, #2452]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r10, #2456]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r10, #2460]
	ldrh	r2, [r10, #2456]
	adds	r3, r3, #1
	str	r3, [r10, #2460]
	ldrh	r3, [r10, #2452]
	strh	r4, [r10, #2454]	@ movhi
	strh	r2, [r10, #2452]	@ movhi
	strh	r3, [r10, #2456]	@ movhi
	bl	FtlBbmTblFlush
.L2804:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2828:
	.align	2
.L2827:
	.word	.LANCHOR2
	.word	.LANCHOR0+2480
	.word	.LANCHOR0
	.word	.LANCHOR4+1248
	.word	.LANCHOR4
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	1
	.global	log2phys
	.thumb
	.thumb_func
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L2845
	mov	r9, r0
	str	r2, [sp, #8]
	mov	r10, r1
	ldr	r3, [r6, #2448]
	ldrh	r2, [r6, #2396]
	cmp	r0, r3
	bcs	.L2830
	adds	r3, r2, #7
	ldr	r2, .L2845+4
	lsr	r5, r0, r3
	str	r3, [sp, #4]
	ldrh	r1, [r6, #2426]
	movs	r4, #0
	ldr	fp, [r2, #460]
	mov	r8, r2
	uxth	r5, r5
	movs	r2, #12
	b	.L2831
.L2830:
	ldr	r3, [sp, #8]
	mov	r0, #-1
	cmp	r3, #0
	bne	.L2832
	str	r0, [r1]
	b	.L2832
.L2836:
	adds	r4, r4, #1
	mla	r0, r2, r4, fp
	ldrh	r0, [r0, #-12]
	cmp	r0, r5
	beq	.L2833
.L2831:
	uxth	r7, r4
	cmp	r7, r1
	bcc	.L2836
	bl	select_l2p_ram_region
	movs	r4, #12
	movw	r3, #65535
	muls	r4, r0, r4
	mov	r7, r0
	add	r2, fp, r4
	ldrh	r1, [fp, r4]
	cmp	r1, r3
	bne	.L2844
.L2837:
	ldr	r3, [r8, #3388]
	ldr	fp, .L2845+4
	ldr	r3, [r3, r5, lsl #2]
	cmp	r3, #0
	bne	.L2838
	ldr	r2, [fp, #460]
	movs	r1, #255
	str	r3, [sp, #12]
	add	r2, r2, r4
	ldr	r0, [r2, #8]
	ldrh	r2, [r6, #2398]
	bl	ftl_memset
	ldr	r2, [fp, #460]
	strh	r5, [r2, r4]	@ movhi
	ldr	r2, [fp, #460]
	add	r4, r4, r2
	ldr	r3, [sp, #12]
	str	r3, [r4, #4]
.L2833:
	ldr	r3, [sp, #4]
	movs	r2, #1
	ldr	r1, [sp, #8]
	lsls	r2, r2, r3
	movs	r3, #12
	subs	r2, r2, #1
	and	r9, r9, r2
	ldr	r2, .L2845+4
	uxth	r9, r9
	cbnz	r1, .L2834
	ldr	r1, [r2, #460]
	mla	r3, r3, r7, r1
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r9, lsl #2]
	str	r3, [r10]
	b	.L2835
.L2834:
	muls	r3, r7, r3
	ldr	r1, [r2, #460]
	ldr	r0, [r10]
	add	r1, r1, r3
	ldr	r1, [r1, #8]
	str	r0, [r1, r9, lsl #2]
	ldr	r1, [r2, #460]
	strh	r5, [r2, #464]	@ movhi
	add	r3, r3, r1
	ldr	r1, [r3, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r3, #4]
.L2835:
	ldr	r2, [r2, #460]
	movs	r3, #12
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	adds	r2, r3, #1
	beq	.L2842
	adds	r3, r3, #1
	str	r3, [r7, #4]
	b	.L2842
.L2844:
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L2837
	bl	flush_l2p_region
	b	.L2837
.L2838:
	ldr	r2, [fp, #460]
	movs	r1, #1
	ldr	r8, .L2845+24
	add	r2, r2, r4
	str	r3, [sp, #12]
	add	r0, r8, #1248
	ldr	r2, [r2, #8]
	str	r3, [r8, #1252]
	str	r2, [r8, #1256]
	ldr	r2, [fp, #3340]
	str	r2, [r8, #1260]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r8, #1260]
	ldrh	r2, [r2, #8]
	cmp	r2, r5
	ldr	r3, [sp, #12]
	beq	.L2839
	mov	r2, r3
	mov	r1, r5
	ldr	r0, .L2845+8
	bl	printk
	movs	r2, #4
	ldr	r0, .L2845+12
	mov	r3, r2
	ldr	r1, [r8, #1260]
	bl	rknand_print_hex
	ldrh	r3, [r6, #2424]
	ldr	r0, .L2845+16
	movs	r2, #4
	ldr	r1, [fp, #3388]
	bl	rknand_print_hex
	movs	r3, #1
	str	r3, [fp, #224]
	b	.L2840
.L2839:
	ldr	r2, [r8, #1248]
	cmp	r2, #256
	bne	.L2840
	mov	r2, r3
	mov	r1, r5
	ldr	r0, .L2845+20
	bl	printk
	ldr	r3, [fp, #460]
	add	r0, fp, #3408
	mov	r1, r5
	add	r3, r3, r4
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2840:
	ldr	r3, .L2845+4
	movs	r1, #0
	ldr	r3, [r3, #460]
	adds	r2, r3, r4
	str	r1, [r2, #4]
	strh	r5, [r3, r4]	@ movhi
	b	.L2833
.L2842:
	movs	r0, #0
.L2832:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2846:
	.align	2
.L2845:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC140
	.word	.LC101
	.word	.LC141
	.word	.LC142
	.word	.LANCHOR4
	.fnend
	.size	log2phys, .-log2phys
	.align	1
	.global	FtlWriteDumpData
	.thumb
	.thumb_func
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r4, .L2865
	ldr	r3, [r4, #224]
	cmp	r3, #0
	bne	.L2847
	ldrh	r2, [r4, #320]
	cmp	r2, #0
	beq	.L2849
	ldrb	r3, [r4, #324]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2849
	ldr	r7, .L2865+4
	ldrb	r1, [r4, #323]	@ zero_extendqisi2
	ldrh	r3, [r7, #2388]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L2849
	ldrb	r9, [r4, #326]	@ zero_extendqisi2
	ldr	r6, [r7, #2448]
	ldrh	r8, [r7, #2320]
	cmp	r9, #0
	bne	.L2847
	subs	r6, r6, #1
	mov	r1, sp
	mov	r2, r9
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #3340]
	ldr	r0, [r4, #3316]
	ldr	r3, [sp]
	str	r6, [sp, #20]
	str	r0, [sp, #12]
	str	r3, [sp, #8]
	adds	r3, r3, #1
	str	r5, [sp, #16]
	str	r9, [r5, #4]
	beq	.L2851
	movs	r1, #1
	mov	r2, r9
	add	r0, sp, #4
	bl	FlashReadPages
	b	.L2852
.L2851:
	movs	r1, #255
	ldrh	r2, [r7, #2398]
	bl	ftl_memset
.L2852:
	movs	r7, #0
	ldr	r10, .L2865
	lsl	r8, r8, #2
	movw	r3, #61589
	mov	r9, r7
	strh	r3, [r5]	@ movhi
.L2853:
	cmp	r7, r8
	beq	.L2854
	ldrh	r3, [r4, #320]
	cbz	r3, .L2854
	ldr	r3, [sp, #8]
	adds	r7, r7, #1
	str	r6, [r5, #8]
	ldr	r0, .L2865+8
	str	r3, [r5, #12]
	ldrh	r3, [r10, #316]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r10, #512]
	movs	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, #0
	it	eq
	moveq	r3, r9
	str	r3, [r10, #512]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r4, #316]
	bl	decrement_vpc_count
	b	.L2853
.L2854:
	movs	r3, #1
	b	.L2864
.L2849:
	movs	r3, #0
.L2864:
	strb	r3, [r4, #326]
.L2847:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2866:
	.align	2
.L2865:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+316
	.fnend
	.size	FtlWriteDumpData, .-FtlWriteDumpData
	.align	1
	.global	l2p_flush
	.thumb
	.thumb_func
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	bl	FtlWriteDumpData
	movs	r4, #0
	ldr	r5, .L2872
	movs	r7, #12
	ldr	r6, .L2872+4
.L2868:
	ldrh	r3, [r5, #2426]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2871
	ldr	r3, [r6, #460]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2869
	bl	flush_l2p_region
.L2869:
	adds	r4, r4, #1
	b	.L2868
.L2871:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L2873:
	.align	2
.L2872:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	1
	.global	allocate_new_data_superblock
	.thumb
	.thumb_func
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r4, .L2900
	ldrh	r5, [r0]
	ldr	r3, [r4, #224]
	cmp	r3, #0
	bne	.L2875
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2876
	ldr	r3, [r4, #296]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L2877
	bl	INSERT_DATA_LIST
	b	.L2876
.L2877:
	bl	INSERT_FREE_LIST
.L2876:
	ldr	r2, .L2900
	movs	r3, #0
	strb	r3, [r6, #8]
	add	r3, r2, #364
	cmp	r6, r3
	beq	.L2878
	ldr	r3, .L2900+4
	ldrh	r1, [r3, #2340]
	cmp	r1, #1
	beq	.L2878
	ldrb	r0, [r3, #144]	@ zero_extendqisi2
	cbz	r0, .L2879
.L2878:
	movs	r3, #1
	strb	r3, [r6, #8]
	b	.L2880
.L2879:
	add	r0, r2, #316
	cmp	r6, r0
	bne	.L2880
	cmp	r1, #3
	beq	.L2882
	ldr	r2, [r2, #604]
	cmp	r2, #1
	bne	.L2883
.L2882:
	movs	r2, #1
	strb	r2, [r4, #324]
.L2883:
	ldr	r3, [r3, #2244]
	cbz	r3, .L2880
	ldr	r3, [r4, #532]
	cmp	r3, #39
	ittt	ls
	movls	r2, #1
	ldrls	r3, .L2900
	strbls	r2, [r3, #324]
.L2880:
	ldrh	r0, [r4, #3460]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2885
	cmp	r5, r0
	bne	.L2886
	ldr	r3, .L2900
	ldr	r3, [r3, #296]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L2887
.L2886:
	bl	update_vpc_list
.L2887:
	movw	r3, #65535
	strh	r3, [r4, #3460]	@ movhi
.L2885:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2875:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L2901:
	.align	2
.L2900:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	1
	.global	FtlCheckVpc
	.thumb
	.thumb_func
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	movs	r4, #0
	ldr	r1, .L2924
	ldr	r0, .L2924+4
	bl	printk
	ldr	r0, .L2924+8
	movs	r1, #0
	mov	r2, #8192
	bl	memset
	ldr	r5, .L2924+12
	ldr	r6, .L2924+16
.L2903:
	ldr	r3, [r5, #2448]
	ldr	r9, .L2924+12
	cmp	r4, r3
	bcs	.L2922
	mov	r0, r4
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L2904
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r0, r6, r0, lsl #1
	ldrh	r3, [r0, #1336]
	adds	r3, r3, #1
	strh	r3, [r0, #1336]	@ movhi
.L2904:
	adds	r4, r4, #1
	b	.L2903
.L2922:
	movs	r7, #0
	ldr	r8, .L2924+20
	ldr	r10, .L2924+16
	mov	r6, r7
.L2906:
	ldrh	r3, [r9, #2328]
	uxth	r4, r7
	ldr	r5, .L2924+20
	cmp	r3, r4
	bls	.L2923
	ldr	r3, [r8, #296]
	add	r5, r10, r4, lsl #1
	ldrh	r2, [r3, r4, lsl #1]
	ldrh	r3, [r5, #1336]
	cmp	r2, r3
	beq	.L2907
	ldr	r0, .L2924+24
	mov	r1, r4
	bl	printk
	ldr	r3, [r8, #296]
	movw	r2, #65535
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r3, r2
	beq	.L2907
	ldrh	r2, [r5, #1336]
	cmp	r2, r3
	it	hi
	movhi	r6, #1
.L2907:
	adds	r7, r7, #1
	b	.L2906
.L2923:
	ldr	r3, [r5, #308]
	cbz	r3, .L2909
	ldr	r2, [r5, #288]
	movs	r7, #0
	ldrh	r8, [r5, #312]
	subs	r3, r3, r2
	ldr	r2, .L2924+28
	ldr	r9, .L2924+16
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r4, r3
.L2910:
	uxth	r3, r7
	cmp	r3, r8
	bcs	.L2909
	ldr	r3, [r5, #296]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L2911
	add	r3, r9, r4, lsl #1
	ldr	r0, .L2924+32
	mov	r1, r4
	movs	r6, #1
	ldrh	r3, [r3, #1336]
	bl	printk
.L2911:
	movs	r3, #6
	ldr	r2, [r5, #288]
	adds	r7, r7, #1
	muls	r4, r3, r4
	movw	r3, #65535
	ldrh	r4, [r2, r4]
	cmp	r4, r3
	bne	.L2910
.L2909:
	mov	r1, r6
	ldr	r0, .L2924+36
	bl	printk
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2925:
	.align	2
.L2924:
	.word	.LANCHOR3+204
	.word	.LC110
	.word	.LANCHOR4+1336
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LC143
	.word	-1431655765
	.word	.LC144
	.word	.LC145
	.fnend
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	1
	.global	Ftlscanalldata
	.thumb
	.thumb_func
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movs	r1, #0
	.pad #36
	sub	sp, sp, #36
	ldr	r0, .L2941
	bl	printk
	ldr	r7, .L2941+4
	ldr	r5, .L2941+8
	movs	r4, #0
.L2927:
	ldr	r3, [r7, #2448]
	cmp	r4, r3
	bcs	.L2940
	mov	r0, r4
	add	r1, sp, #28
	movs	r2, #0
	bl	log2phys
	ubfx	r3, r4, #0, #11
	cbnz	r3, .L2928
	ldr	r0, .L2941+12
	mov	r1, r4
	ldr	r2, [sp, #28]
	bl	printk
.L2928:
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L2930
	str	r3, [r5, #1252]
	movs	r1, #1
	ldr	r3, .L2941+16
	ldr	r0, .L2941+20
	str	r4, [r5, #1264]
	ldr	r2, [r3, #3316]
	ldr	r6, [r3, #3340]
	str	r2, [r5, #1256]
	movs	r2, #0
	str	r6, [r5, #1260]
	str	r2, [r5, #1248]
	bl	FlashReadPages
	ldr	r3, [r5, #1248]
	ldr	r2, .L2941+8
	cmp	r3, #256
	beq	.L2931
	adds	r3, r3, #1
	beq	.L2931
	ldr	r3, [r6, #8]
	cmp	r3, r4
	beq	.L2930
.L2931:
	ldr	r3, [r2, #1260]
	ldr	r1, [r2, #1256]
	ldr	r0, [r3, #4]
	str	r0, [sp]
	ldr	r0, [r3, #8]
	str	r0, [sp, #4]
	ldr	r0, [r3, #12]
	str	r0, [sp, #8]
	ldr	r0, [r1]
	str	r0, [sp, #12]
	ldr	r1, [r1, #4]
	ldr	r0, .L2941+24
	str	r1, [sp, #16]
	mov	r1, r4
	ldr	r2, [r2, #1252]
	ldr	r3, [r3]
	bl	printk
.L2930:
	adds	r4, r4, #1
	b	.L2927
.L2940:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2942:
	.align	2
.L2941:
	.word	.LC146
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC147
	.word	.LANCHOR2
	.word	.LANCHOR4+1248
	.word	.LC148
	.fnend
	.size	Ftlscanalldata, .-Ftlscanalldata
	.align	1
	.global	FtlReUsePrevPpa
	.thumb
	.thumb_func
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r6, r0
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r5, .L2952
	ldr	r4, [r5, #296]
	ldrh	r3, [r4, r0, lsl #1]
	cbnz	r3, .L2944
	ldr	r4, [r5, #308]
	cbz	r4, .L2945
	ldr	r1, [r5, #288]
	mov	lr, #6
	ldr	r2, .L2952+4
	movw	ip, #65535
	subs	r4, r4, r1
	ldrh	r7, [r5, #312]
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L2946:
	uxth	r2, r3
	cmp	r2, r7
	bcs	.L2945
	cmp	r4, r0
	bne	.L2947
	mov	r1, r4
	ldr	r0, .L2952+8
	bl	List_remove_node
	ldrh	r3, [r5, #312]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #312]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #296]
	ldrh	r3, [r2, r4, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r4, lsl #1]	@ movhi
	b	.L2945
.L2947:
	mul	r4, lr, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, ip
	bne	.L2946
	b	.L2945
.L2944:
	adds	r3, r3, #1
	strh	r3, [r4, r0, lsl #1]	@ movhi
.L2945:
	mov	r0, r6
	add	r1, sp, #4
	movs	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2953:
	.align	2
.L2952:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+308
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	1
	.global	FtlRecoverySuperblock
	.thumb
	.thumb_func
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r7, #65535
	ldrh	r3, [r0]
	.pad #68
	sub	sp, sp, #68
	mov	r4, r0
	cmp	r3, r7
	beq	.L3101
	ldrh	r3, [r0, #2]
	movs	r5, #0
	ldr	r6, .L3117
	str	r3, [sp, #12]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	str	r3, [sp, #28]
	ldrh	r3, [r6, #2388]
	cmp	r3, r2
	bne	.L2957
	strh	r5, [r0, #4]	@ movhi
	strb	r5, [r0, #6]
	b	.L3101
.L2957:
	ldrh	r0, [r0, #16]
.L2958:
	cmp	r0, r7
	add	r5, r5, #1
	bne	.L3112
	uxth	r3, r5
	adds	r3, r3, #8
	ldrh	r0, [r4, r3, lsl #1]
	b	.L2958
.L3112:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2960
	bl	FtlGetLastWrittenPage
	adds	r2, r0, #1
	mov	r5, r0
	beq	.L2961
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cbnz	r3, .L3103
	ldr	r3, .L3117
	add	r3, r3, r0, lsl #1
	ldrh	r7, [r3, #148]
	b	.L3035
.L2960:
	movs	r1, #0
	bl	FtlGetLastWrittenPage
	adds	r3, r0, #1
	mov	r5, r0
	beq	.L2961
.L3103:
	mov	r7, r5
.L3035:
	ldr	r3, .L3117+4
	movw	r9, #65535
	ldrh	ip, [r6, #2320]
	mov	r10, #36
	ldrh	r0, [r6, #2400]
	ldr	r2, [r3, #3304]
	ldr	r8, [r3, #1148]
	add	r3, r4, #14
	str	r3, [sp, #20]
	str	r2, [sp, #4]
	movs	r2, #0
	mov	lr, r3
	mov	r6, r2
	mov	fp, r2
	b	.L2963
.L2961:
	movs	r3, #0
	strh	r3, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
	b	.L3101
.L2966:
	ldrh	r3, [lr, #2]!
	cmp	r3, r9
	beq	.L2964
	ldr	r1, [sp, #4]
	orr	r3, r7, r3, lsl #10
	mla	r1, r10, r6, r1
	str	r3, [r1, #4]
	mov	r3, r0
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	str	fp, [r1, #8]
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r8
	str	r3, [r1, #12]
.L2964:
	adds	r2, r2, #1
.L2963:
	uxth	r3, r2
	cmp	r3, ip
	bcc	.L2966
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L3037
	ldr	r3, .L3117
	ldrb	r1, [r3, #144]	@ zero_extendqisi2
	adds	r3, r1, #0
	it	ne
	movne	r3, #1
	b	.L3104
.L3037:
	movs	r3, #0
.L3104:
	ldr	r8, .L3117+4
	mov	r1, r6
	str	r3, [sp, #24]
	mov	fp, #0
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #3304]
	bl	FlashReadPages
	ldr	r3, [r8, #512]
	subs	r3, r3, #1
	str	r8, [sp, #8]
	str	r3, [sp, #4]
	movw	r3, #65535
	str	r3, [sp, #16]
.L2968:
	uxth	r9, fp
	cmp	r9, r6
	bcs	.L2975
	movs	r1, #36
	ldr	r0, [r8, #3304]
	mul	r1, r1, fp
	add	lr, r0, r1
	ldr	r1, [r0, r1]
	cbnz	r1, .L2969
	ldr	r3, [lr, #12]
	ldr	r10, [r3, #4]
	cmp	r10, #-1
	beq	.L2970
	mov	r0, r10
	ldr	r1, [r8, #512]
	str	r3, [sp, #32]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #32]
	cbz	r0, .L2970
	add	r1, r10, #1
	str	r1, [r8, #512]
.L2970:
	ldr	r1, [r3]
	adds	r1, r1, #1
	bne	.L2971
.L2975:
	cmp	r9, r6
	ldr	r6, .L3117+4
	bne	.L3102
	ldr	r3, [r6, #3304]
	adds	r5, r5, #1
	uxth	r10, r5
	ldr	r0, [r3, #4]
	b	.L3105
.L2969:
	ldr	r1, [lr, #4]
	ldr	r0, .L3117+8
	bl	printk
	uxth	r3, r7
	ldrh	r1, [r4]
	str	r3, [sp, #16]
	ldr	r3, .L3117+12
	strh	r1, [r3, #840]	@ movhi
.L2971:
	add	fp, fp, #1
	b	.L2968
.L3102:
	ldr	r3, [sp, #8]
	movs	r2, #36
	uxth	r10, r5
	ldr	r3, [r3, #3304]
	mla	r9, r2, r9, r3
	ldr	r0, [r9, #4]
.L3105:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r5, [r4, #8]	@ zero_extendqisi2
	ldr	r2, .L3117
	cmp	r5, #1
	str	r0, [sp, #8]
	bne	.L2977
	ldrb	r3, [r2, #144]	@ zero_extendqisi2
	cbnz	r3, .L2977
	add	r3, r2, r10, lsl #1
	ldrh	r10, [r3, #148]
.L2977:
	ldrh	r3, [r2, #2388]
	cmp	r3, r10
	itttt	eq
	strheq	r10, [r4, #2]	@ movhi
	moveq	r3, #0
	strbeq	r3, [r4, #6]
	strheq	r3, [r4, #4]	@ movhi
	ldrh	r3, [sp, #28]
	str	r3, [sp, #32]
	ldr	r3, [sp, #12]
	cmp	r10, r3
	bne	.L2979
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #32]
	cmp	r3, r2
	bne	.L2979
	mov	r0, r4
	mov	r1, r10
	mov	r2, r3
	b	.L3110
.L2979:
	ldr	r2, [sp, #16]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2980
	cmp	r5, #0
	bne	.L2981
.L2980:
	ldr	r3, [r6, #3472]
	uxth	fp, r7
	mov	r9, #-1
	ldr	r7, .L3117+4
	adds	r3, r3, #1
	mov	r8, r9
	ittt	eq
	ldreq	r3, .L3117+4
	ldreq	r2, [sp, #4]
	streq	r2, [r3, #3472]
	ldr	r3, [r6, #3472]
	str	r3, [sp, #16]
	ldr	r3, [sp, #12]
	adds	r3, r3, #7
	cmp	fp, r3
	itet	gt
	subgt	r5, fp, #7
	ldrle	r5, [sp, #12]
	uxthgt	r5, r5
.L2984:
	cmp	r5, fp
	bhi	.L2997
	ldr	r3, .L3117
	ldr	r1, [sp, #20]
	ldrh	ip, [r3, #2320]
	ldr	r3, [r7, #3304]
	str	r3, [sp, #28]
	movs	r3, #0
	mov	r6, r3
.L2998:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3113
	ldrh	r2, [r1, #2]!
	movw	r0, #65535
	cmp	r2, r0
	beq	.L2985
	ldr	r0, [sp, #28]
	mov	lr, #36
	orr	r2, r5, r2, lsl #10
	mla	r0, lr, r6, r0
	adds	r6, r6, #1
	uxth	r6, r6
	str	r2, [r0, #4]
.L2985:
	adds	r3, r3, #1
	b	.L2998
.L3113:
	mov	r1, r6
	ldr	r2, [sp, #24]
	ldr	r0, [r7, #3304]
	bl	FlashReadPages
	ldr	r3, .L3117
	movs	r2, #36
	add	lr, r7, r5, lsl #1
	movw	r1, #65535
	ldrb	r0, [r3, #144]	@ zero_extendqisi2
	ldr	r3, [r7, #3304]
	mla	r6, r2, r6, r3
.L2987:
	cmp	r3, r6
	beq	.L3114
	ldr	r2, [r3]
	cbnz	r2, .L2988
	ldr	r2, [r3, #12]
	ldrh	ip, [r2]
	cmp	ip, r1
	beq	.L2989
	ldr	r2, [r2, #4]
	cmp	r2, #-1
	beq	.L2989
	cmp	r9, #-1
	ldr	r8, [r7, #3472]
	str	r2, [r7, #3472]
	bne	.L2989
	ldrh	r2, [lr, #1220]
	cmp	r2, r1
	bne	.L2990
	cmp	r0, #0
	beq	.L2989
.L2990:
	ldr	r2, [sp, #4]
	cmp	r8, r2
	ite	ne
	movne	r9, r8
	moveq	r9, #-1
	b	.L2989
.L3118:
	.align	2
.L3117:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC149
	.word	.LANCHOR5
.L2988:
	ldr	r3, .L3119
	ldrh	r2, [r4]
	strh	r2, [r3, #840]	@ movhi
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cbnz	r3, .L2981
	ldr	r3, .L3119+4
	movw	r2, #65535
	add	r5, r3, r5, lsl #1
	ldrh	r1, [r5, #1220]
	cmp	r1, r2
	bne	.L2992
	cmp	r9, #-1
	beq	.L2993
	str	r9, [r3, #3472]
	b	.L2981
.L2993:
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	bne	.L3106
	ldr	r2, [r3, #3472]
	b	.L3111
.L2992:
	ldr	r2, [sp, #4]
	cmp	r8, r2
	beq	.L2995
	cmp	r8, #-1
	beq	.L2981
	str	r8, [r3, #3472]
	b	.L2981
.L2995:
	ldr	r2, [r3, #3472]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	beq	.L2981
.L3111:
	subs	r2, r2, #1
	b	.L3106
.L2989:
	adds	r3, r3, #36
	b	.L2987
.L3114:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L2984
.L2997:
	ldr	r3, .L3119+4
	mov	r2, #-1
.L3106:
	str	r2, [r3, #3472]
.L2981:
	ldr	r3, .L3119
	movs	r2, #1
	ldr	fp, [sp, #12]
	ldr	r5, .L3119+4
	strh	r2, [r3, #842]	@ movhi
.L2999:
	ldr	r3, .L3119+8
	movw	r9, #65535
	ldr	r1, [r5, #3304]
	movs	r6, #0
	ldr	r7, [sp, #20]
	ldrh	r8, [r3, #2320]
	ldrb	r0, [r3, #144]	@ zero_extendqisi2
	str	r6, [sp, #16]
.L3000:
	uxth	r3, r6
	cmp	r3, r8
	bcs	.L3115
	ldrh	r3, [r7, #2]!
	cmp	r3, r9
	beq	.L3001
	ldr	r2, [sp, #16]
	mov	lr, #36
	orr	r3, fp, r3, lsl #10
	mla	r2, lr, r2, r1
	str	r3, [r2, #4]
	ldrb	lr, [r4, #8]	@ zero_extendqisi2
	cmp	lr, #1
	bne	.L3002
	cbz	r0, .L3002
	orr	r3, r3, #-2147483648
	str	r3, [r2, #4]
.L3002:
	ldr	r3, [sp, #16]
	add	ip, r3, #1
	uxth	r3, ip
	str	r3, [sp, #16]
.L3001:
	adds	r6, r6, #1
	b	.L3000
.L3115:
	ldr	r0, [r5, #3304]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	movs	r3, #0
.L3109:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3116
	ldr	r3, [sp, #28]
	movs	r6, #36
	ldr	r9, [r5, #3304]
	muls	r6, r3, r6
	add	r8, r9, r6
	ldr	r7, [r8, #4]
	ubfx	r0, r7, #10, #16
	str	r7, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #12]
	cmp	fp, r3
	bcc	.L3005
	bne	.L3006
	ldr	r3, [sp, #32]
	cmp	r0, r3
	bcc	.L3005
.L3006:
	cmp	fp, r10
	bne	.L3007
	ldr	r3, [sp, #8]
	cmp	r0, r3
	beq	.L3008
.L3007:
	ldr	r3, [r9, r6]
	adds	r3, r3, #1
	beq	.L3009
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L3010
	ldrh	r0, [r4]
	b	.L3107
.L3010:
	ldr	r3, [r8, #4]
	adds	r7, r3, #1
	str	r3, [sp, #4]
	beq	.L3011
	mov	r0, r3
	ldr	r1, [r5, #512]
	bl	ftl_cmp_data_ver
	cbz	r0, .L3011
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [r5, #512]
.L3011:
	ldr	r7, [r8, #8]
	add	r1, sp, #56
	ldr	r3, [r8, #12]
	movs	r2, #0
	mov	r0, r7
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r5, #3472]
	adds	r0, r1, #1
	beq	.L3012
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3012
	ldr	r3, [sp, #52]
	adds	r2, r3, #1
	beq	.L3013
	ldr	r0, [r5, #3304]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r6
	str	r3, [r0, #4]
	ldr	r9, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r5, #3304]
	ldr	r3, [r9, #4]
	add	ip, r2, r6
	str	r3, [sp, #36]
	ldr	r3, [r2, r6]
	adds	r3, r3, #1
	bne	.L3014
	b	.L3015
.L3013:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L3005
	mov	r0, r7
	add	r1, sp, #52
	movs	r2, #1
	bl	log2phys
	b	.L3005
.L3014:
	ldr	r8, [r9, #8]
	cmp	r8, r7
	bne	.L3015
	ldr	r0, [r5, #3472]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	cbz	r0, .L3015
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L3020
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L3015
	adds	r7, r3, #1
	beq	.L3018
	str	r3, [ip, #4]
	mov	r0, ip
	movs	r1, #1
	movs	r2, #0
	ldr	r9, [ip, #12]
	bl	FlashReadPages
	b	.L3019
.L3018:
	str	r3, [r2, r6]
.L3019:
	ldr	r3, [r5, #3304]
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	beq	.L3020
	ldr	r6, [r9, #4]
	ldr	r0, [r5, #3472]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L3020
	ldr	r0, [sp, #36]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L3015
.L3020:
	mov	r0, r8
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L3015:
	mov	r3, #-1
	str	r3, [sp, #52]
	b	.L3022
.L3120:
	.align	2
.L3119:
	.word	.LANCHOR5
	.word	.LANCHOR2
	.word	.LANCHOR0
.L3012:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L3022
	ldr	r3, [sp, #52]
	adds	r0, r3, #1
	beq	.L3024
	ldr	r2, .L3121
	ubfx	r3, r3, #10, #21
	ldr	r2, [r2, #2336]
	cmp	r3, r2
	bcs	.L3005
.L3024:
	mov	r0, r7
	add	r1, sp, #60
	movs	r2, #1
	bl	log2phys
	ldr	r8, [sp, #56]
	cmp	r8, #-1
	beq	.L3022
	ldr	r3, [sp, #52]
	cmp	r8, r3
	beq	.L3022
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #316]
	cmp	r3, r0
	beq	.L3026
	ldrh	r3, [r5, #364]
	cmp	r3, r0
	beq	.L3026
	ldrh	r3, [r5, #412]
	cmp	r3, r0
	bne	.L3022
.L3026:
	ldr	r3, .L3121+4
	movs	r1, #1
	movs	r2, #0
	ldr	r0, [r3, #3304]
	str	r8, [r0, #4]
	ldr	r6, [r0, #12]
	bl	FlashReadPages
	ldr	r3, .L3121+4
	ldr	r1, [r6, #4]
	ldr	r3, [r3, #3304]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L3022
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cbnz	r0, .L3022
	mov	r0, r7
	add	r1, sp, #56
	movs	r2, #1
	bl	log2phys
.L3022:
	ldr	r0, [sp, #52]
	adds	r1, r0, #1
	beq	.L3005
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #296]
	ldrh	r3, [r3, r0, lsl #1]
	mov	r1, r0
	cbz	r3, .L3027
.L3107:
	bl	decrement_vpc_count
	b	.L3005
.L3027:
	ldr	r0, .L3121+8
	bl	printk
	b	.L3005
.L3009:
	ldrh	r3, [r4]
	mov	r1, r7
	ldr	r2, .L3121+12
	ldr	r0, .L3121+16
	strh	r3, [r2, #840]	@ movhi
	ldr	r2, [sp, #4]
	bl	printk
	ldr	r3, .L3121+12
	ldr	r3, [r3, #844]
	cmp	r3, #31
	bhi	.L3028
	ldr	r2, .L3121+12
	ldr	r1, [sp, #60]
	add	r2, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r2, #848]
	ldr	r2, .L3121+12
	str	r3, [r2, #844]
.L3028:
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	ldr	r3, [r5, #3472]
	adds	r2, r3, #1
	bne	.L3029
	ldr	r3, [sp, #4]
	b	.L3108
.L3029:
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bls	.L3005
	mov	r3, r2
.L3108:
	str	r3, [r5, #3472]
.L3005:
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	b	.L3109
.L3116:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	fp, fp, #1
	cmp	r3, #1
	uxth	fp, fp
	ldr	r3, .L3121
	bne	.L3031
	ldrb	r2, [r3, #144]	@ zero_extendqisi2
	cbz	r2, .L3031
	ldrh	r2, [r3, #2390]
	cmp	r2, fp
	bne	.L3031
	cmp	r10, fp
	beq	.L3008
.L3031:
	ldrh	r3, [r3, #2388]
	cmp	fp, r3
	bne	.L2999
	ldr	r2, .L3121
	movw	r0, #65535
	movs	r3, #0
	strh	fp, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	ldrh	r1, [r2, #2320]
.L3032:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L3101
	ldr	r6, [sp, #20]
	adds	r3, r3, #1
	ldrh	r5, [r6, #2]!
	cmp	r5, r0
	str	r6, [sp, #20]
	beq	.L3032
	strb	r2, [r4, #6]
	b	.L3101
.L3008:
	ldrb	r3, [sp, #8]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, [sp, #8]
	mov	r1, r10
	strh	r10, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
.L3110:
	bl	ftl_sb_update_avl_pages
.L3101:
	movs	r0, #0
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3122:
	.align	2
.L3121:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC150
	.word	.LANCHOR5
	.word	.LC151
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	1
	.global	FtlVpcCheckAndModify
	.thumb
	.thumb_func
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	movs	r4, #0
	ldr	r7, .L3136
	ldr	r1, .L3136+4
	ldr	r0, .L3136+8
	bl	printk
	ldr	r5, .L3136+12
	ldrh	r2, [r7, #2330]
	movs	r1, #0
	ldr	r0, [r5, #3364]
	lsls	r2, r2, #1
	bl	ftl_memset
.L3124:
	ldr	r3, [r7, #2448]
	cmp	r4, r3
	bcs	.L3134
	mov	r0, r4
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L3125
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #3364]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L3125:
	adds	r4, r4, #1
	b	.L3124
.L3134:
	ldr	r6, .L3136+12
	mov	r8, #0
	movw	r9, #65535
.L3127:
	ldrh	r3, [r7, #2328]
	uxth	r4, r8
	cmp	r3, r4
	bls	.L3135
	ldr	r3, [r5, #296]
	ldrh	r2, [r3, r4, lsl #1]
	ldr	r3, [r5, #3364]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r2, r3
	beq	.L3128
	cmp	r2, r9
	beq	.L3128
	ldrh	r1, [r6, #316]
	cmp	r1, r4
	beq	.L3128
	ldrh	r1, [r6, #412]
	cmp	r1, r4
	beq	.L3128
	ldrh	r1, [r6, #364]
	cmp	r1, r4
	beq	.L3128
	ldr	r0, .L3136+16
	mov	r1, r4
	bl	printk
	ldr	r3, [r6, #296]
	ldrh	r2, [r3, r4, lsl #1]
	cbnz	r2, .L3129
	ldr	r2, [r6, #3364]
	ldrh	r2, [r2, r4, lsl #1]
	strh	r2, [r3, r4, lsl #1]	@ movhi
	b	.L3128
.L3129:
	ldr	r2, [r6, #3364]
	mov	r0, r4
	ldrh	r2, [r2, r4, lsl #1]
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	update_vpc_list
.L3128:
	add	r8, r8, #1
	b	.L3127
.L3135:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3137:
	.align	2
.L3136:
	.word	.LANCHOR0
	.word	.LANCHOR3+216
	.word	.LC110
	.word	.LANCHOR2
	.word	.LC152
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	1
	.global	FtlGcScanTempBlk
	.thumb
	.thumb_func
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3192
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldrh	r5, [r3, #3448]
	movw	r3, #65535
	mov	r4, r0
	str	r1, [sp, #20]
	cmp	r5, r3
	beq	.L3174
	cbnz	r5, .L3139
	b	.L3140
.L3174:
	movs	r5, #0
.L3139:
	ldr	r3, .L3192+4
	ldr	r2, [sp, #20]
	ldrh	r3, [r3, #2388]
	cmp	r2, r3
	bne	.L3141
.L3140:
	bl	FtlGcPageVarInit
.L3141:
	ldr	r6, .L3192+8
	mov	r3, #-1
	str	r3, [sp, #16]
	movs	r3, #0
	str	r3, [sp, #4]
.L3142:
	ldrh	r1, [r4]
	movw	r3, #65535
	movs	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L3143
.L3144:
	ldr	r10, .L3192+4
.L3171:
	ldr	r3, [r6, #3304]
	movs	r2, #0
	ldrh	lr, [r10, #2320]
	add	ip, r4, #14
	ldr	r8, [r6, #1148]
	mov	fp, #36
	str	r3, [sp, #8]
	mov	r7, r2
	ldr	r3, [r6, #1144]
	movw	r9, #65535
	str	r3, [sp, #12]
	ldrh	r3, [r10, #2398]
	str	r3, [sp, #24]
	ldrh	r3, [r10, #2400]
	str	r3, [sp, #28]
.L3145:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L3190
	ldrh	r3, [ip, #2]!
	cmp	r3, r9
	beq	.L3146
	ldr	r1, [sp, #8]
	orr	r3, r5, r3, lsl #10
	mla	r1, fp, r7, r1
	str	r3, [r1, #4]
	ldr	r3, [sp, #24]
	muls	r3, r7, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r0, r3
	ldr	r3, [sp, #12]
	add	r0, r0, r3
	ldr	r3, [sp, #28]
	muls	r3, r7, r3
	add	r7, r7, #1
	it	mi
	addmi	r3, r3, #3
	str	r0, [r1, #8]
	uxth	r7, r7
	bic	r3, r3, #3
	add	r3, r3, r8
	str	r3, [r1, #12]
.L3146:
	adds	r2, r2, #1
	b	.L3145
.L3190:
	ldr	r0, [r6, #3304]
	mov	r1, r7
	movs	r2, #0
	bl	FlashReadPages
	movs	r3, #0
.L3189:
	str	r3, [sp, #8]
	ldrh	r3, [sp, #8]
	cmp	r3, r7
	bcs	.L3191
	ldr	r3, [sp, #8]
	mov	r9, #36
	mul	r9, r9, r3
	ldr	r3, .L3192+8
	ldr	r8, [r3, #3304]
	add	fp, r8, r9
	ldr	r3, [fp, #4]
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #12]
	bl	P2V_plane
	ldr	r8, [r8, r9]
	ldr	r3, [fp, #12]
	ldr	ip, .L3192+4
	ldr	fp, .L3192+8
	mov	r2, r0
	cmp	r8, #0
	bne	.L3151
	ldrh	r0, [r3]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L3152
.L3155:
	ldrb	r3, [ip, #144]	@ zero_extendqisi2
	cbz	r3, .L3186
	movs	r3, #1
	str	r3, [fp, #3468]
	b	.L3143
.L3152:
	ldr	r0, [r3, #8]
	ldr	r1, [r10, #2448]
	cmp	r0, r1
	bls	.L3187
	b	.L3155
.L3186:
	ldrh	r1, [r4]
	ldr	r2, [fp, #296]
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
	strh	r3, [fp, #556]	@ movhi
	b	.L3188
.L3187:
	ldrb	r2, [r10]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3159
	add	r1, sp, #32
	mov	r2, r8
	str	r3, [sp, #24]
	bl	log2phys
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #32]
	ldr	r1, [r3, #12]
	cmp	r2, r1
	bne	.L3159
	adds	r1, r2, #1
	beq	.L3159
	str	r2, [sp, #40]
	movs	r1, #1
	ldr	r2, [r6, #3336]
	add	r0, sp, #36
	str	r2, [sp, #44]
	ldr	r2, [r6, #3344]
	str	r2, [sp, #48]
	mov	r2, r8
	bl	FlashReadPages
	ldrh	r2, [r10, #2394]
	ldr	r1, [r6, #3304]
	lsl	lr, r2, #7
	add	r9, r9, r1
	ldr	r0, [sp, #44]
	ldr	r3, [sp, #24]
.L3161:
	cmp	r8, lr
	beq	.L3159
	ldr	r1, [r9, #8]
	ldr	r2, [r0, r8, lsl #2]
	ldr	r1, [r1, r8, lsl #2]
	cmp	r1, r2
	beq	.L3162
	ldrh	r1, [r4]
	ldr	r2, [sp, #40]
	ldr	r0, .L3192+12
	bl	printk
	ldr	r3, [r6, #296]
	ldrh	r2, [r4]
	movs	r1, #0
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
	strh	r3, [r6, #556]	@ movhi
	b	.L3188
.L3162:
	add	r8, r8, #1
	b	.L3161
.L3159:
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	ldr	r1, [sp, #12]
	bl	FtlGcUpdatePage
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	b	.L3189
.L3151:
	ldr	r2, [sp, #12]
	ldr	r0, .L3192+16
	ldrh	r1, [r4]
	str	ip, [sp, #8]
	bl	printk
	ldrh	r3, [r4]
	ldr	ip, [sp, #8]
	ldr	r2, [ip, #2244]
	cbnz	r2, .L3165
	ldrb	r2, [ip, #144]	@ zero_extendqisi2
	cbz	r2, .L3166
.L3165:
	ldr	r2, [fp, #232]
	ldrh	r2, [r2, r3, lsl #1]
	cmp	r2, #159
	bls	.L3167
.L3166:
	ldr	r2, [fp, #3304]
	ldr	r2, [r2, r9]
	adds	r2, r2, #1
	bne	.L3168
.L3167:
	ldr	r2, [fp, #3304]
	add	r9, r9, r2
	ldr	r2, [r9, #4]
	str	r2, [sp, #16]
.L3168:
	ldr	r2, .L3192+8
	movs	r1, #0
	ldr	r2, [r2, #296]
	strh	r1, [r2, r3, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
.L3188:
	bl	FtlGcPageVarInit
	movs	r5, #0
	b	.L3142
.L3191:
	ldr	r3, [sp, #4]
	adds	r5, r5, #1
	ldr	r2, [sp, #20]
	adds	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp, #4]
	bcs	.L3170
.L3172:
	ldrh	r3, [r10, #2388]
	cmp	r3, r5
	bhi	.L3171
	movs	r2, #0
	b	.L3143
.L3170:
	ldr	r2, .L3192
	movw	r1, #65535
	ldrh	r3, [r2, #3448]
	cmp	r3, r1
	beq	.L3172
	ldr	r1, [sp, #4]
	add	r3, r3, r1
	strh	r3, [r2, #3448]	@ movhi
	ldrh	r3, [r10, #2388]
	cmp	r3, r5
	bls	.L3172
	b	.L3173
.L3143:
	ldr	r3, .L3192
	movw	r1, #65535
	strh	r5, [r4, #2]	@ movhi
	mov	r0, r4
	strb	r2, [r4, #6]
	strh	r1, [r3, #3448]	@ movhi
	mov	r1, r5
	bl	ftl_sb_update_avl_pages
.L3173:
	ldr	r0, [sp, #16]
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3193:
	.align	2
.L3192:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC153
	.word	.LC154
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	1
	.global	FtlReadRefresh
	.thumb
	.thumb_func
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r5, .L3210
	ldr	r4, .L3210+4
	ldr	r9, [r5, #688]
	mov	r6, r5
	cmp	r9, #0
	beq	.L3195
	ldr	r2, [r5, #692]
	ldr	r3, [r4, #2448]
	cmp	r2, r3
	bcs	.L3196
	mov	r5, #2048
	mov	r7, r6
.L3201:
	ldr	r0, [r6, #692]
	ldr	r3, [r4, #2448]
	cmp	r0, r3
	bcs	.L3200
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r3, [r7, #692]
	adds	r3, r3, #1
	str	r3, [r7, #692]
	ldr	r2, [sp]
	adds	r1, r2, #1
	beq	.L3199
	add	r0, sp, #40
	str	r2, [sp, #8]
	movs	r1, #1
	movs	r2, #0
	str	r2, [r0, #-36]!
	str	r3, [sp, #20]
	str	r2, [sp, #12]
	str	r2, [sp, #16]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmp	r3, #256
	bne	.L3200
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3200:
	mov	r0, #-1
	b	.L3203
.L3199:
	subs	r5, r5, #1
	bne	.L3201
	b	.L3200
.L3196:
	ldr	r3, [r5, #476]
	movs	r0, #0
	str	r0, [r5, #688]
	str	r0, [r5, #692]
	str	r3, [r5, #684]
	b	.L3203
.L3195:
	ldr	r8, [r5, #476]
	ldr	r1, [r5, #528]
	ldr	r7, [r5, #684]
	add	r2, r8, #1048576
	ldr	r3, [r4, #2448]
	movw	r4, #10000
	cmp	r1, r4
	ite	hi
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r2
	bhi	.L3205
	mov	r0, #1000
	lsrs	r1, r1, #10
	adds	r1, r1, #1
	muls	r0, r3, r0
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r0, r8
	bcc	.L3205
	ldrh	r3, [r5, #268]
	ands	r0, r4, r3
	bne	.L3207
	ldr	r2, [r5, #708]
	cmp	r2, r3
	beq	.L3203
.L3205:
	ldrh	r3, [r6, #268]
	movs	r0, #0
	str	r8, [r6, #684]
	str	r0, [r6, #692]
	str	r3, [r6, #708]
	movs	r3, #1
	str	r3, [r6, #688]
	b	.L3203
.L3207:
	mov	r0, r9
.L3203:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3211:
	.align	2
.L3210:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	1
	.global	FtlGcFreeTempBlock
	.thumb
	.thumb_func
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L3250
	ldr	r6, .L3250+4
	ldr	r2, [r5, #224]
	mov	r4, r5
	ldrh	r1, [r6, #2388]
	cmp	r2, #0
	bne	.L3248
	ldrh	r5, [r5, #412]
	movw	r7, #65535
	cmp	r5, r7
	bne	.L3215
.L3224:
	ldrh	r2, [r4, #412]
	movw	r3, #65535
	movs	r7, #0
	ldr	r5, .L3250
	cmp	r2, r3
	str	r7, [r4, #3468]
	beq	.L3248
	bl	FtlCacheWriteBack
	ldrb	r0, [r5, #419]	@ zero_extendqisi2
	ldrh	r3, [r6, #2388]
	mov	r10, #12
	ldr	r2, [r5, #296]
	ldrh	r1, [r5, #412]
	smulbb	r3, r0, r3
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r3, [r5, #496]
	ldrh	r2, [r5, #1172]
	add	r3, r3, r2
	str	r3, [r5, #496]
	b	.L3225
.L3215:
	cbz	r0, .L3218
	ldr	r3, .L3250+8
	ldrh	r0, [r3, #3448]
	cmp	r0, r7
	beq	.L3219
.L3220:
	movs	r1, #2
	b	.L3218
.L3219:
	strh	r2, [r3, #3448]	@ movhi
	ldrh	r3, [r4, #312]
	cmp	r3, #17
	bhi	.L3220
.L3218:
	ldr	r7, .L3250
	add	r0, r7, #412
	bl	FtlGcScanTempBlk
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L3221
	ldr	r2, [r7, #232]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L3222
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L3222:
	ldr	r3, [r4, #3468]
	ldr	r2, .L3250
	cbnz	r3, .L3223
	ldr	r0, [sp, #4]
	ldr	r3, [r2, #704]
	ubfx	r0, r0, #10, #16
	adds	r3, r3, #1
	str	r3, [r2, #704]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3223:
	movs	r3, #0
	str	r3, [r4, #3468]
	b	.L3235
.L3221:
	ldr	r3, .L3250+8
	ldrh	r2, [r3, #3448]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3235
	b	.L3224
.L3228:
	ldr	r3, [r9, #4]
	cmp	r0, r3
	bne	.L3246
.L3227:
	adds	r7, r7, #1
.L3225:
	ldrh	r3, [r4, #1172]
	uxth	r8, r7
	cmp	r3, r8
	bls	.L3249
	mul	r8, r10, r8
	ldr	fp, [r5, #1168]
	ldr	r3, [r6, #2448]
	add	r9, fp, r8
	ldr	r0, [r9, #8]
	cmp	r0, r3
	bcs	.L3246
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r3, [fp, r8]
	ldr	r0, [sp, #4]
	cmp	r0, r3
	bne	.L3228
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r9, #4
	movs	r2, #1
	mov	r8, r0
	ldr	r0, [r9, #8]
	bl	log2phys
	mov	r0, r8
	b	.L3247
.L3246:
	ldrh	r0, [r5, #412]
.L3247:
	bl	decrement_vpc_count
	b	.L3227
.L3249:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cbz	r3, .L3230
	ldr	r3, .L3250
	ldr	r0, .L3250+12
	ldrh	r1, [r3, #412]
	bl	printk
.L3230:
	ldrh	r0, [r4, #412]
	ldr	r3, [r4, #296]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L3231
	bl	INSERT_DATA_LIST
	b	.L3232
.L3231:
	bl	INSERT_FREE_LIST
.L3232:
	movw	r8, #65535
	movs	r5, #0
	strh	r8, [r4, #412]	@ movhi
	strh	r5, [r4, #1172]	@ movhi
	strh	r5, [r4, #1164]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r6, #2244]
	ldr	r7, .L3250
	strh	r8, [r4, #556]	@ movhi
	cbz	r3, .L3233
	ldr	r3, [r7, #532]
	cmp	r3, #39
	bhi	.L3233
	ldrh	r3, [r7, #536]
	ldrh	r2, [r7, #312]
	cmp	r2, r3
	bcs	.L3248
	lsls	r3, r3, #1
	strh	r3, [r7, #1120]	@ movhi
	b	.L3248
.L3233:
	ldrh	r3, [r4, #536]
	ldrh	r1, [r4, #312]
	ldr	r2, .L3250
	add	r0, r3, r3, lsl #1
	cmp	r1, r0, asr #2
	ble	.L3248
	ldrb	r0, [r6, #144]	@ zero_extendqisi2
	cbz	r0, .L3234
	subs	r3, r3, #2
	strh	r3, [r2, #1120]	@ movhi
.L3248:
	movs	r0, #0
	b	.L3214
.L3234:
	movs	r3, #20
	strh	r3, [r2, #1120]	@ movhi
	b	.L3214
.L3235:
	movs	r0, #1
.L3214:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3251:
	.align	2
.L3250:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC155
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	1
	.global	FtlGcPageRecovery
	.thumb
	.thumb_func
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r5, .L3254
	ldr	r4, .L3254+4
	ldrh	r1, [r5, #2388]
	add	r0, r4, #412
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #414]
	ldrh	r3, [r5, #2388]
	cmp	r2, r3
	bcc	.L3252
	add	r0, r4, #3408
	bl	FtlMapBlkWriteDumpData
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #3468]
.L3252:
	pop	{r3, r4, r5, pc}
.L3255:
	.align	2
.L3254:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	1
	.global	FtlPowerLostRecovery
	.thumb
	.thumb_func
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	ldr	r4, .L3257
	ldr	r3, .L3257+4
	add	r6, r4, #316
	add	r4, r4, #364
	mov	r0, r6
	str	r5, [r3, #844]
	bl	FtlRecoverySuperblock
	mov	r0, r6
	bl	FtlSlcSuperblockCheck
	mov	r0, r4
	bl	FtlRecoverySuperblock
	mov	r0, r4
	bl	FtlSlcSuperblockCheck
	bl	FtlGcPageRecovery
	movw	r0, #65535
	bl	decrement_vpc_count
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L3258:
	.align	2
.L3257:
	.word	.LANCHOR2
	.word	.LANCHOR5
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	1
	.global	FtlSysBlkInit
	.thumb
	.thumb_func
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r3, #0
	ldr	r5, .L3276
	movw	r7, #65535
	ldr	r6, .L3276+4
	ldr	r4, .L3276+8
	ldrh	r0, [r5, #2324]
	strh	r3, [r6, #842]	@ movhi
	strh	r7, [r6, #840]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r3, [r4, #540]
	cmp	r3, r7
	bne	.L3260
.L3262:
	mov	r7, #-1
	b	.L3261
.L3260:
	bl	FtlLoadSysInfo
	mov	r7, r0
	cmp	r0, #0
	bne	.L3262
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r5, #2426]
	ldr	r2, [r4, #460]
	movs	r0, #12
	mov	r3, r7
.L3263:
	cmp	r3, r1
	bge	.L3268
	mla	lr, r0, r3, r2
	ldr	lr, [lr, #4]
	cmp	lr, #0
	bge	.L3264
.L3268:
	ldrh	r2, [r4, #268]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #268]	@ movhi
	bge	.L3275
	b	.L3265
.L3264:
	adds	r3, r3, #1
	b	.L3263
.L3275:
	ldrh	r3, [r6, #842]
	cbz	r3, .L3269
.L3265:
	ldrh	r1, [r4, #316]
	ldr	r2, [r4, #296]
	ldrh	r0, [r4, #320]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r1, [r4, #296]
	ldrh	r3, [r5, #2388]
	ldrh	r0, [r4, #364]
	ldrh	r6, [r4, #368]
	strh	r3, [r4, #318]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #322]
	strh	r3, [r4, #320]	@ movhi
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r6
	strh	r2, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r5, #2388]
	strb	r3, [r4, #370]
	strh	r3, [r4, #368]	@ movhi
	ldrh	r3, [r4, #270]
	strh	r2, [r4, #366]	@ movhi
	adds	r3, r3, #1
	strh	r3, [r4, #270]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3269:
	ldrh	r0, [r4, #316]
	movw	r3, #65535
	ldr	r6, .L3276+8
	cmp	r0, r3
	beq	.L3270
	ldrh	r3, [r6, #320]
	cbnz	r3, .L3270
	ldrh	r3, [r6, #368]
	cbnz	r3, .L3270
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r6, #364]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	add	r0, r6, #316
	bl	allocate_new_data_superblock
	add	r0, r6, #364
	bl	allocate_new_data_superblock
.L3270:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cbnz	r3, .L3271
	ldrh	r3, [r4, #268]
	lsls	r3, r3, #27
	bne	.L3261
.L3271:
	bl	FtlVpcCheckAndModify
.L3261:
	mov	r0, r7
	pop	{r3, r4, r5, r6, r7, pc}
.L3277:
	.align	2
.L3276:
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LANCHOR2
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	1
	.global	FtlLowFormat
	.thumb
	.thumb_func
	.type	FtlLowFormat, %function
FtlLowFormat:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L3312
	ldr	r6, [r4, #224]
	cmp	r6, #0
	bne	.L3280
	ldr	r5, .L3312+4
	mov	r1, r6
	ldr	r0, [r4, #3392]
	ldrh	r2, [r5, #2424]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #2424]
	mov	r1, r6
	ldr	r0, [r4, #3388]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r5, #2324]
	str	r6, [r4, #508]
	str	r6, [r4, #512]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3281
	bl	FtlMakeBbt
.L3281:
	ldr	r0, .L3312+8
	movs	r2, #0
.L3282:
	ldrh	r1, [r5, #2394]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	bge	.L3309
	ldr	r6, [r4, #3332]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r6, r3, lsl #2]
	ldr	r1, [r4, #3336]
	str	r0, [r1, r3, lsl #2]
	b	.L3282
.L3309:
	ldr	r3, .L3312+4
	movs	r6, #0
	ldrh	r7, [r3, #2328]
.L3284:
	ldrh	r3, [r5, #2330]
	ldr	r8, .L3312+4
	cmp	r3, r7
	bls	.L3310
	mov	r0, r7
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r7, r7, #1
	uxth	r7, r7
	add	r0, r0, r6
	uxth	r6, r0
	b	.L3284
.L3310:
	ldrh	r1, [r8, #2320]
	subs	r3, r6, #3
	cmp	r3, r1, lsl #1
	blt	.L3286
	mov	r0, r6
	movs	r6, #0
	bl	__aeabi_uidiv
	ldr	r3, [r8, #2420]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r8, #2324]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r7, [r8, #2328]
.L3287:
	ldrh	r3, [r5, #2330]
	cmp	r3, r7
	bls	.L3286
	mov	r0, r7
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r7, r7, #1
	uxth	r7, r7
	add	r0, r0, r6
	uxth	r6, r0
	b	.L3287
.L3286:
	mov	r8, #0
	mov	r7, r8
.L3289:
	ldrh	r3, [r5, #2328]
	uxth	r0, r8
	ldr	fp, .L3312+4
	add	r8, r8, #1
	cmp	r3, r0
	bls	.L3311
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r0, r7
	uxth	r7, r0
	b	.L3289
.L3311:
	ldr	r2, [fp, #2332]
	ldrh	r3, [fp, #2330]
	ldrh	r8, [fp, #2320]
	mov	r0, r2
	str	r2, [sp, #4]
	str	r3, [r4, #3292]
	mov	r1, r8
	bl	__aeabi_uidiv
	ubfx	r9, r0, #5, #16
	mov	r10, r0
	add	r3, r9, #36
	strh	r3, [r4, #536]	@ movhi
	movs	r3, #24
	str	r0, [fp, #2448]
	ldr	r2, [sp, #4]
	mul	r3, r3, r8
	cmp	r7, r3
	ble	.L3291
	subs	r0, r2, r7
	mov	r1, r8
	bl	__aeabi_uidiv
	ldr	r3, .L3312
	str	r0, [fp, #2448]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r3, #536]	@ movhi
.L3291:
	ldr	r3, [r5, #2244]
	cmp	r3, #1
	bne	.L3292
	mov	r0, r7
	mov	r1, r8
	bl	__aeabi_uidiv
	ldrh	fp, [r4, #536]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r4, #536]	@ movhi
.L3292:
	ldrb	r3, [r5, #144]	@ zero_extendqisi2
	cbz	r3, .L3293
	mov	r0, r7
	mov	r1, r8
	bl	__aeabi_uidiv
	ldrh	fp, [r4, #536]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r4, #536]	@ movhi
.L3293:
	ldrh	r3, [r5, #2382]
	cbz	r3, .L3295
	ldrh	r2, [r4, #536]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #536]	@ movhi
	mul	r2, r8, r3
	cmp	r2, r7
	itttt	gt
	addgt	r3, r3, #32
	ldrgt	r2, .L3312+4
	addgt	r3, r3, r9
	strgt	r10, [r2, #2448]
	itt	gt
	ldrgt	r2, .L3312
	strhgt	r3, [r2, #536]	@ movhi
.L3295:
	ldrh	r2, [r4, #536]
	ldr	r3, [r5, #2448]
	subs	r3, r3, r2
	ldrh	r2, [r5, #2388]
	mul	r3, r8, r3
	ldr	r8, .L3312+20
	str	r3, [r8, #1284]
	muls	r3, r2, r3
	ldrh	r2, [r5, #2394]
	str	r3, [r5, #2448]
	muls	r3, r2, r3
	str	r3, [r5, #2428]
	bl	FtlBbmTblFlush
	ldr	r2, [r5, #2336]
	adds	r1, r7, r6
	ldrh	r3, [r5, #2402]
	add	r3, r3, r2, lsr #3
	cmp	r1, r3
	bls	.L3297
	ldr	r0, .L3312+12
	lsrs	r2, r2, #5
	bl	printk
.L3297:
	ldrh	r2, [r5, #2330]
	movs	r1, #0
	ldr	r0, [r4, #296]
	movs	r7, #0
	movw	r6, #65535
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2328]
	ldr	r0, [r4, #472]
	movs	r1, #255
	str	r7, [r4, #468]
	movs	r3, #1
	lsrs	r2, r2, #3
	strh	r7, [r4, #558]	@ movhi
	strb	r7, [r4, #562]
	strb	r7, [r4, #564]
	strh	r7, [r4, #318]	@ movhi
	strb	r7, [r4, #322]
	strh	r7, [r4, #316]	@ movhi
	strh	r6, [r4, #556]	@ movhi
	strb	r3, [r4, #324]
	bl	ftl_memset
	ldr	r7, .L3312
.L3298:
	ldr	r5, .L3312
	add	r0, r5, #316
	bl	make_superblock
	ldrb	r3, [r4, #323]	@ zero_extendqisi2
	ldrh	r2, [r4, #316]
	cbnz	r3, .L3299
	ldr	r3, [r7, #296]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r7, #316]
	adds	r3, r3, #1
	strh	r3, [r7, #316]	@ movhi
	b	.L3298
.L3299:
	ldr	r3, [r5, #508]
	movw	r7, #65535
	ldrh	r1, [r5, #320]
	str	r3, [r5, #328]
	adds	r3, r3, #1
	str	r3, [r5, #508]
	ldr	r3, [r5, #296]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r5, #366]	@ movhi
	strb	r3, [r5, #370]
	ldrh	r3, [r5, #316]
	adds	r3, r3, #1
	strh	r3, [r5, #364]	@ movhi
	movs	r3, #1
	strb	r3, [r5, #372]
.L3300:
	ldr	r6, .L3312
	add	r0, r6, #364
	bl	make_superblock
	ldrb	r3, [r4, #371]	@ zero_extendqisi2
	ldrh	r2, [r4, #364]
	cbnz	r3, .L3301
	ldr	r3, [r5, #296]
	strh	r7, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r5, #364]
	adds	r3, r3, #1
	strh	r3, [r5, #364]	@ movhi
	b	.L3300
.L3301:
	ldr	r3, [r6, #508]
	movw	r4, #65535
	ldrh	r1, [r6, #368]
	str	r3, [r6, #376]
	adds	r3, r3, #1
	str	r3, [r6, #508]
	ldr	r3, [r6, #296]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r4, [r6, #412]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r3, [r6, #542]	@ movhi
	ldr	r3, [r8, #1284]
	strh	r4, [r6, #544]	@ movhi
	strh	r3, [r6, #546]	@ movhi
	ldr	r3, [r6, #508]
	str	r3, [r6, #548]
	adds	r3, r3, #1
	str	r3, [r6, #508]
	strh	r0, [r6, #540]	@ movhi
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L3280
	ldr	r3, .L3312+16
	movs	r2, #1
	str	r2, [r3, #504]
.L3280:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3313:
	.align	2
.L3312:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	168778952
	.word	.LC156
	.word	.LANCHOR1
	.word	.LANCHOR4
	.fnend
	.size	FtlLowFormat, .-FtlLowFormat
	.align	1
	.global	FtlReInitForSDUpdata
	.thumb
	.thumb_func
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #16
	ldr	r4, .L3342
	ldrb	r3, [r4, #144]	@ zero_extendqisi2
	cbz	r3, .L3315
.L3317:
	movs	r0, #0
	b	.L3316
.L3315:
	ldr	r5, .L3342+4
	ldr	r0, [r5, #1180]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3317
	bl	FlashLoadFactorBbt
	cbz	r0, .L3318
	bl	FlashMakeFactorBbt
.L3318:
	ldr	r0, [r5, #1212]
	bl	FlashReadIdbDataRaw
	cbz	r0, .L3319
	movs	r1, #0
	movs	r2, #16
	mov	r0, sp
	movs	r6, #1
	bl	FlashReadFacBbtData
	movs	r3, #0
	mov	r2, r3
	ldr	r1, [sp]
.L3320:
	lsl	r5, r6, r2
	adds	r0, r3, #1
	tst	r5, r1
	add	r2, r2, #1
	it	ne
	movne	r3, r0
	cmp	r2, #16
	bne	.L3320
	cmp	r3, #6
	bls	.L3338
	movs	r2, #0
	movs	r6, #1
.L3323:
	lsl	r5, r6, r2
	adds	r0, r3, #1
	tst	r5, r1
	add	r2, r2, #1
	it	ne
	movne	r3, r0
	cmp	r2, #24
	bne	.L3323
	cmp	r3, #17
	bhi	.L3324
.L3338:
	strb	r2, [r4, #1]
	b	.L3322
.L3324:
	movs	r3, #36
	strb	r3, [r4, #1]
.L3322:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strh	r3, [r4, #142]	@ movhi
.L3319:
	ldr	r1, .L3342+8
	ldr	r0, .L3342+12
	bl	printk
	ldr	r0, .L3342+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #2324]
	bl	FtlFreeSysBlkQueueInit
	movs	r4, #1
.L3325:
	bl	FtlLoadBbt
	cbz	r0, .L3326
.L3340:
	bl	FtlLowFormat
	cmp	r4, #3
	bhi	.L3341
	adds	r4, r4, #1
	b	.L3325
.L3341:
	mov	r0, #-1
	b	.L3316
.L3326:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3340
	ldr	r3, .L3342+20
	movs	r2, #1
	str	r2, [r3, #504]
.L3316:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L3343:
	.align	2
.L3342:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC77
	.word	.LC76
	.word	.LANCHOR0+116
	.word	.LANCHOR1
	.fnend
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	1
	.global	Ftl_gc_temp_data_write_back
	.thumb
	.thumb_func
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r5, .L3360
	ldr	r3, [r5, #224]
	cbz	r3, .L3345
.L3348:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3345:
	ldr	r3, .L3360+4
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cbz	r3, .L3347
	ldr	r3, [r5, #1136]
	lsls	r3, r3, #31
	bpl	.L3347
	ldrh	r3, [r5, #416]
	cmp	r3, #0
	bne	.L3348
.L3347:
	movs	r2, #0
	ldr	r0, [r5, #3308]
	ldr	r1, [r5, #1136]
	movs	r6, #0
	mov	r3, r2
	ldr	r7, .L3360
	bl	FlashProgPages
	mov	r8, #36
.L3349:
	ldr	r1, [r5, #1136]
	uxth	r3, r6
	ldr	r4, .L3360
	cmp	r3, r1
	bcs	.L3359
	mul	r3, r8, r3
	ldr	r0, [r7, #3308]
	adds	r6, r6, #1
	adds	r1, r0, r3
	ldr	r0, [r0, r3]
	ldr	r2, [r1, #12]
	cmp	r0, #-1
	bne	.L3350
	ldrh	r1, [r4, #412]
	movs	r5, #0
	ldr	r2, [r4, #296]
	strh	r5, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #704]
	strh	r0, [r4, #412]	@ movhi
	adds	r2, r2, #1
	str	r2, [r4, #704]
	ldr	r2, [r4, #3308]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3358
.L3350:
	ldr	r0, [r2, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r2, #8]
	bl	FtlGcUpdatePage
	b	.L3349
.L3359:
	ldr	r0, [r4, #3308]
	bl	FtlGcBufFree
	movs	r3, #0
	str	r3, [r4, #1136]
	ldrh	r3, [r4, #416]
	cmp	r3, #0
	bne	.L3348
	movs	r0, #1
	bl	FtlGcFreeTempBlock
.L3358:
	movs	r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L3361:
	.align	2
.L3360:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	1
	.global	Ftl_get_new_temp_ppa
	.thumb
	.thumb_func
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r3, #65535
	ldr	r4, .L3365
	ldrh	r2, [r4, #412]
	cmp	r2, r3
	beq	.L3363
	ldrh	r3, [r4, #416]
	cbnz	r3, .L3364
.L3363:
	bl	FtlCacheWriteBack
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3365+4
	strb	r5, [r4, #420]
	bl	allocate_data_superblock
	strh	r5, [r4, #1164]	@ movhi
	strh	r5, [r4, #1172]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3364:
	ldr	r0, .L3365+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L3366:
	.align	2
.L3365:
	.word	.LANCHOR2
	.word	.LANCHOR2+412
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	1
	.global	ftl_do_gc
	.thumb
	.thumb_func
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3540
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r0, [r3, #224]
	.pad #44
	sub	sp, sp, #44
	mov	r6, r1
	mov	r7, r3
	cmp	r0, #0
	bne	.L3467
	ldr	r1, .L3540+4
	ldr	r4, [r1, #504]
	cmp	r4, #1
	bne	.L3527
	ldr	r2, [r3, #3280]
	cmp	r2, #0
	bne	.L3527
	ldrh	r0, [r3, #304]
	cmp	r0, #47
	bls	.L3470
	ldrh	r1, [r1, #3448]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3369
.L3372:
	ldrh	r4, [r7, #1176]
	movw	r1, #65535
	ldr	r3, .L3540
	cmp	r4, r1
	bne	.L3370
	b	.L3371
.L3369:
	ldrh	r3, [r3, #412]
	cmp	r3, r2
	beq	.L3372
	mov	r0, r4
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3372
	mov	r0, r4
	b	.L3527
.L3370:
	ldrh	r2, [r3, #1174]
	cmp	r2, r1
	bne	.L3371
	ldrh	r0, [r3, #1178]
	cmp	r0, r2
	beq	.L3371
	ldrh	r1, [r3, #1180]
	cmp	r1, r2
	itttt	ne
	strhne	r4, [r3, #1174]	@ movhi
	strhne	r0, [r3, #1176]	@ movhi
	strhne	r1, [r3, #1178]	@ movhi
	strhne	r2, [r3, #1180]	@ movhi
.L3371:
	ldr	r4, [r7, #1124]
	cmp	r8, #1
	add	r4, r4, #1
	add	r4, r4, r8, lsl #7
	str	r4, [r7, #1124]
	bne	.L3383
	ldr	r3, .L3540+8
	ldr	r2, [r3, #2244]
	mov	r10, r3
	cbnz	r2, .L3374
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3383
.L3374:
	ldr	r3, [r7, #532]
	ldr	r5, .L3540
	cmp	r3, #39
	bhi	.L3383
	ldr	r9, .L3540+12
	ldrh	r3, [r9, #976]
	add	r4, r4, r3
	str	r4, [r5, #1124]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r5, #556]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3383
	ldrh	r2, [r5, #1174]
	cmp	r2, r3
	bne	.L3383
	ldr	r3, [r5, #1124]
	cmp	r3, #1024
	bhi	.L3375
	ldrh	r3, [r5, #312]
	cmp	r3, #63
	bhi	.L3383
.L3375:
	ldrh	r3, [r7, #1122]
	movs	r0, #0
	ldrh	r4, [r7, #312]
	adds	r3, r3, #64
	ldr	r2, .L3540+12
	cmp	r4, r3
	strh	r0, [r9, #976]	@ movhi
	ldr	r1, .L3540
	bgt	.L3383
	ldr	r3, [r1, #532]
	str	r0, [r1, #1124]
	cbnz	r3, .L3376
	movs	r3, #6
	b	.L3528
.L3376:
	cmp	r3, #5
	bhi	.L3377
	movs	r3, #18
.L3528:
	strh	r3, [r2, #976]	@ movhi
.L3377:
	movs	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r2, r0
	cmp	r2, ip
	beq	.L3382
	ldrh	r0, [r7, #1128]
	ldr	r5, .L3540
	cmp	r0, #0
	beq	.L3379
	ldrh	r4, [r10, #2390]
	ldrh	r3, [r10, #2320]
	ldr	fp, [r5, #296]
	muls	r3, r4, r3
	ldrh	r1, [fp, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r1, r3
	bgt	.L3382
	adds	r1, r0, #1
	str	ip, [sp, #20]
	str	r2, [sp, #16]
	mov	r10, #0
	uxth	r1, r1
	str	r10, [r5, #1132]
	strh	r1, [r5, #1128]	@ movhi
	str	r1, [sp, #12]
	bl	List_get_gc_head_node
	ldr	ip, [sp, #20]
	uxth	r4, r0
	ldr	r1, [sp, #12]
	cmp	r4, ip
	ldr	r2, [sp, #16]
	beq	.L3382
	ldrh	r2, [fp, r2, lsl #1]
	ldrh	r3, [fp, r4, lsl #1]
	ldr	r0, .L3540+16
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, #1128]
	cmp	r3, #40
	bls	.L3380
	ldr	r3, [r5, #296]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r3, #32
	it	hi
	strhhi	r10, [r5, #1128]	@ movhi
.L3380:
	movs	r3, #6
	strh	r3, [r9, #976]	@ movhi
	b	.L3399
.L3379:
	movs	r3, #1
	strh	r3, [r5, #1128]	@ movhi
.L3382:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L3399
	movs	r3, #0
	strh	r3, [r9, #976]	@ movhi
.L3383:
	ldrh	r3, [r7, #556]
	movw	r4, #65535
	ldr	r5, .L3540
	cmp	r3, r4
	bne	.L3533
	ldrh	r4, [r5, #412]
	cmp	r4, r3
	bne	.L3384
	ldrh	r10, [r5, #1174]
	cmp	r10, r4
	bne	.L3385
	ldrh	r3, [r5, #312]
	ldr	r2, [r5, #1124]
	cmp	r3, #24
	ite	cc
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	bls	.L3387
	ldr	r2, .L3540+12
	movs	r3, #0
	str	r3, [r7, #1124]
	mov	r9, r5
	strh	r3, [r2, #976]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r10
	mov	r4, r0
	bne	.L3388
	ldrh	r2, [r5, #312]
	ldrh	r3, [r5, #1122]
	cmp	r2, r3
	bcs	.L3389
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	bne	.L3534
.L3398:
	bl	FtlGcReFreshBadBlk
	cmp	r8, #0
	beq	.L3535
.L3399:
	ldrh	r2, [r7, #412]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3410
.L3465:
	ldrh	r3, [r7, #1174]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3410
	cmp	r4, r3
	bne	.L3410
.L3466:
	ldrh	r1, [r7, #556]
	movw	r3, #65535
	ldr	r2, .L3540
	cmp	r1, r3
	beq	.L3411
.L3415:
	movw	r4, #65535
	b	.L3410
.L3411:
	movs	r3, #0
	ldrh	r1, [r2, #312]
	str	r3, [r2, #1132]
	ldrh	r3, [r2, #1120]
	ldr	r4, .L3540+12
	cmp	r1, r3
	bls	.L3412
	ldrh	r3, [r4, #976]
	cbnz	r3, .L3413
	ldr	r3, .L3540+8
	ldr	r2, [r2, #468]
	ldr	r3, [r3, #2448]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L3478
.L3413:
	ldrh	r3, [r7, #536]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
	b	.L3414
.L3478:
	movs	r3, #18
.L3414:
	strh	r3, [r7, #1120]	@ movhi
	bl	FtlReadRefresh
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r7, #296]
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #4
	bls	.L3412
	ldrh	r0, [r4, #976]
	b	.L3527
.L3412:
	ldrh	r0, [r4, #976]
	cmp	r0, #0
	bne	.L3415
	ldrh	r4, [r7, #536]
	add	r3, r4, r4, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r7, #1120]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r7, #296]
	uxth	r0, r0
	ldrh	r2, [r3, r0, lsl #1]
	ldr	r3, .L3540+8
	ldrh	r1, [r3, #2390]
	ldrh	r3, [r3, #2320]
	muls	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L3416
	ldr	r3, .L3540
	subs	r4, r4, #1
	ldrh	r3, [r3, #312]
	cmp	r3, r4
	blt	.L3416
	bl	FtlReadRefresh
	b	.L3532
.L3416:
	cmp	r2, #0
	bne	.L3415
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r7, #312]
	adds	r0, r0, #1
	b	.L3527
.L3541:
	.align	2
.L3540:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LC157
.L3410:
	ldr	r3, .L3542
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	ite	eq
	moveq	r5, #1
	movne	r5, #2
	b	.L3409
.L3535:
	movw	r3, #65535
	cmp	r4, r3
	bne	.L3399
.L3464:
	ldrh	r4, [r7, #312]
	cmp	r4, #24
	bhi	.L3474
	ldr	r3, .L3542
	cmp	r4, #16
	ldrh	r5, [r3, #2388]
	bls	.L3401
	lsrs	r5, r5, #5
.L3400:
	ldrh	r3, [r7, #1120]
	ldr	r2, .L3542+4
	cmp	r3, r4
	bcc	.L3536
.L3404:
	ldr	r3, .L3542
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	bne	.L3537
.L3477:
	movw	r4, #65535
.L3409:
	ldrh	r2, [r7, #556]
	movw	r1, #65535
	ldr	r3, .L3542+4
	cmp	r2, r1
	bne	.L3418
	cmp	r4, r2
	beq	.L3419
	strh	r4, [r3, #556]	@ movhi
	b	.L3420
.L3419:
	ldrh	r2, [r3, #1174]
	cmp	r2, r4
	beq	.L3420
	ldr	r1, [r3, #296]
	ldrh	r2, [r1, r2, lsl #1]
	cbnz	r2, .L3421
	strh	r4, [r3, #1174]	@ movhi
.L3421:
	ldrh	r3, [r7, #1174]
	strh	r3, [r7, #556]	@ movhi
	movw	r3, #65535
	strh	r3, [r7, #1174]	@ movhi
.L3420:
	ldrh	r0, [r7, #556]
	movw	r6, #65535
	movs	r3, #0
	strb	r3, [r7, #564]
	cmp	r0, r6
	beq	.L3418
	bl	IsBlkInGcList
	cbz	r0, .L3423
	ldr	r3, .L3542+4
	strh	r6, [r3, #556]	@ movhi
.L3423:
	ldr	r3, .L3542
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cbz	r3, .L3424
	ldrh	r0, [r7, #556]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #564]
.L3424:
	ldrh	r2, [r7, #556]
	movw	r3, #65535
	ldr	r6, .L3542+4
	cmp	r2, r3
	beq	.L3418
	add	r0, r6, #556
	bl	make_superblock
	ldr	r2, .L3542+8
	movs	r3, #0
	ldrh	r1, [r6, #556]
	strh	r3, [r6, #558]	@ movhi
	strb	r3, [r6, #562]
	strh	r3, [r2, #978]	@ movhi
	ldr	r3, [r6, #296]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #980]	@ movhi
.L3418:
	ldrh	r3, [r7, #556]
	ldrh	r1, [r7, #316]
	ldr	r2, .L3542+4
	cmp	r1, r3
	beq	.L3425
	ldrh	r1, [r2, #364]
	cmp	r1, r3
	beq	.L3425
	ldrh	r2, [r2, #412]
	cmp	r2, r3
	bne	.L3426
.L3425:
	movw	r3, #65535
	strh	r3, [r7, #556]	@ movhi
.L3426:
	ldr	r7, .L3542
.L3459:
	ldr	r6, .L3542+4
	movw	r3, #65535
	ldrh	r2, [r6, #556]
	mov	fp, r6
	cmp	r2, r3
	bne	.L3427
	mov	r10, r6
	movs	r3, #0
	str	r3, [r6, #1132]
.L3428:
	ldrh	r9, [fp, #1128]
	mov	r0, r9
	bl	List_get_gc_head_node
	movw	r2, #65535
	uxth	r3, r0
	strh	r3, [fp, #556]	@ movhi
	cmp	r3, r2
	bne	.L3429
	ldr	r3, .L3542+4
	movs	r2, #0
	movs	r0, #8
	strh	r2, [r3, #1128]	@ movhi
	b	.L3527
.L3429:
	mov	r0, r3
	str	r3, [sp, #12]
	bl	IsBlkInGcList
	add	r9, r9, #1
	ldr	r3, [sp, #12]
	cbz	r0, .L3430
	strh	r9, [r10, #1128]	@ movhi
	b	.L3428
.L3430:
	ldrh	lr, [r7, #2320]
	uxth	r9, r9
	ldrh	r2, [r7, #2388]
	ldr	r1, [r10, #296]
	strh	r9, [r10, #1128]	@ movhi
	mul	r2, lr, r2
	ldrh	r0, [r1, r3, lsl #1]
	add	lr, r2, r2, lsr #31
	cmp	r0, lr, asr #1
	bgt	.L3432
	cmp	r9, #48
	bls	.L3433
	cmp	r0, #8
	bls	.L3433
	ldrh	r0, [r10, #1164]
	cmp	r0, #35
	bhi	.L3433
.L3432:
	movs	r0, #0
	strh	r0, [fp, #1128]	@ movhi
.L3433:
	ldrh	r1, [r1, r3, lsl #1]
	cmp	r1, r2
	blt	.L3434
	movw	r2, #65535
	cmp	r4, r2
	bne	.L3434
	ldrh	r0, [fp, #1128]
	ldr	r2, .L3542+4
	cmp	r0, #3
	bhi	.L3434
	movs	r3, #0
	strh	r4, [r2, #556]	@ movhi
	strh	r3, [r2, #1128]	@ movhi
.L3532:
	ldr	r3, .L3542+8
	ldrh	r0, [r3, #976]
	b	.L3527
.L3434:
	cbnz	r1, .L3435
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [fp, #1128]
	adds	r3, r3, #1
	strh	r3, [fp, #1128]	@ movhi
	b	.L3428
.L3435:
	movs	r2, #0
	strb	r2, [r6, #564]
	ldr	r2, .L3542
	ldrb	r2, [r2, #144]	@ zero_extendqisi2
	cbz	r2, .L3436
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3542+4
	strb	r0, [r3, #564]
.L3436:
	ldr	r0, .L3542+12
	bl	make_superblock
	ldr	r2, .L3542+8
	ldrh	r0, [r6, #556]
	movs	r3, #0
	ldr	r1, [r6, #296]
	strh	r3, [r2, #978]	@ movhi
	ldrh	r1, [r1, r0, lsl #1]
	strh	r3, [r6, #558]	@ movhi
	strb	r3, [r6, #562]
	strh	r1, [r2, #980]	@ movhi
.L3427:
	cmp	r8, #1
	bne	.L3437
	bl	FtlReadRefresh
.L3437:
	movs	r3, #1
	str	r3, [r6, #3280]
	ldrh	r3, [r7, #2388]
	str	r3, [sp, #12]
	ldrb	r3, [r7, #144]	@ zero_extendqisi2
	cbz	r3, .L3438
	ldr	r3, .L3542+4
	ldrb	r3, [r3, #564]	@ zero_extendqisi2
	cmp	r3, #1
	itt	eq
	ldrheq	r3, [r7, #2390]
	streq	r3, [sp, #12]
.L3438:
	ldrh	r3, [r6, #558]
	ldr	r1, [sp, #12]
	adds	r2, r3, r5
	cmp	r2, r1
	ittt	gt
	movgt	r2, r1
	subgt	r3, r2, r3
	uxthgt	r5, r3
	movs	r3, #0
.L3531:
	str	r3, [sp, #16]
	ldrh	r3, [sp, #16]
	ldr	r6, .L3542+4
	cmp	r3, r5
	bcs	.L3447
	ldr	r3, [r6, #1152]
	addw	r1, r6, #570
	ldr	r2, [sp, #16]
	movw	lr, #65535
	ldrh	r10, [r7, #2320]
	str	r3, [sp, #20]
	ldrh	r3, [r6, #558]
	add	ip, r3, r2
	movs	r3, #0
	mov	r9, r3
.L3448:
	uxth	r2, r3
	cmp	r2, r10
	bcs	.L3538
	ldrh	r2, [r1, #2]!
	cmp	r2, lr
	beq	.L3441
	ldr	r0, [sp, #20]
	mov	fp, #36
	orr	r2, ip, r2, lsl #10
	mla	fp, fp, r9, r0
	add	r9, r9, #1
	uxth	r9, r9
	str	r2, [fp, #4]
.L3441:
	adds	r3, r3, #1
	b	.L3448
.L3543:
	.align	2
.L3542:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LANCHOR2+556
.L3538:
	ldr	r0, [r6, #1152]
	mov	r1, r9
	ldrb	r2, [r6, #564]	@ zero_extendqisi2
	mov	fp, #0
	bl	FlashReadPages
	ldr	r6, .L3544
.L3443:
	uxth	r3, fp
	cmp	r3, r9
	bcs	.L3539
	mov	ip, #36
	ldr	r2, [r6, #1152]
	mul	r10, ip, fp
	add	r3, r2, r10
	ldr	r2, [r2, r10]
	ldr	r3, [r3, #12]
	adds	r2, r2, #1
	beq	.L3481
	ldrh	r1, [r3]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3481
	ldr	r0, [r3, #8]
	add	r1, sp, #32
	movs	r2, #0
	str	ip, [sp, #24]
	str	r3, [sp, #20]
	bl	log2phys
	ldr	r1, [r6, #1152]
	add	r1, r1, r10
	ldr	r0, [r1, #4]
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #20]
	bic	r2, r2, #-2147483648
	ldr	ip, [sp, #24]
	cmp	r2, r0
	bne	.L3481
	str	r3, [sp, #28]
	ldr	r3, .L3544+4
	ldr	r0, [r6, #1136]
	ldr	r1, [r1, #16]
	ldrh	r2, [r3, #978]
	adds	r2, r2, #1
	strh	r2, [r3, #978]	@ movhi
	ldr	r2, [r6, #3308]
	mla	r2, ip, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #20]
	bl	Ftl_get_new_temp_ppa
	ldr	r1, [r6, #1136]
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #24]
	ldr	r3, [sp, #28]
	str	r0, [r2, #4]
	ldr	r2, [r6, #3308]
	mla	ip, ip, r1, r2
	ldr	r2, [r6, #1152]
	add	r2, r2, r10
	ldr	r1, [r2, #8]
	str	r1, [ip, #8]
	movs	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [ip, #12]
	ldr	r2, [sp, #32]
	str	r2, [r3, #12]
	ldrh	r2, [r6, #412]
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r6, #512]
	ldr	r0, [r6, #1152]
	str	r2, [r3, #4]
	add	r0, r0, r10
	ldr	r3, [r6, #1136]
	adds	r3, r3, #1
	str	r3, [r6, #1136]
	bl	FtlGcBufAlloc
	ldrb	r3, [r7, #144]	@ zero_extendqisi2
	cbnz	r3, .L3445
	ldrb	r3, [r6, #419]	@ zero_extendqisi2
	ldr	r2, [r6, #1136]
	cmp	r2, r3
	beq	.L3445
	ldrh	r3, [r6, #416]
	cbnz	r3, .L3481
.L3445:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L3481
	ldr	r3, .L3544
	movs	r2, #0
	movw	r1, #65535
	str	r2, [r3, #3280]
	strh	r1, [r3, #556]	@ movhi
	strh	r2, [r3, #558]	@ movhi
	b	.L3532
.L3481:
	add	fp, fp, #1
	b	.L3443
.L3539:
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	b	.L3531
.L3447:
	ldrh	r3, [r6, #558]
	add	r5, r5, r3
	ldr	r3, [sp, #12]
	uxth	r5, r5
	strh	r5, [r6, #558]	@ movhi
	cmp	r5, r3
	bcc	.L3449
	ldr	r3, [r6, #1136]
	ldr	r5, .L3544+4
	cbz	r3, .L3450
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L3450
	movs	r3, #0
	ldrh	r0, [r5, #976]
	str	r3, [r6, #3280]
	b	.L3527
.L3450:
	ldrh	r5, [r5, #978]
	cbnz	r5, .L3451
	ldrh	r2, [r6, #556]
	ldr	r3, [r6, #296]
	ldrh	r3, [r3, r2, lsl #1]
	cbz	r3, .L3451
.L3452:
	ldr	r3, [r7, #2448]
	cmp	r5, r3
	bcs	.L3457
	mov	r0, r5
	add	r1, sp, #36
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #36]
	adds	r3, r0, #1
	beq	.L3453
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6, #556]
	cmp	r3, r0
	bne	.L3453
.L3457:
	ldr	r3, .L3544+8
	ldr	r3, [r3, #2448]
	cmp	r5, r3
	bcc	.L3451
	ldrh	r2, [r6, #556]
	movs	r1, #0
	ldr	r3, [r6, #296]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r6, #556]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3451
.L3453:
	adds	r5, r5, #1
	b	.L3452
.L3451:
	movw	r3, #65535
	strh	r3, [r6, #556]	@ movhi
.L3449:
	ldrh	r3, [r6, #312]
	cmp	r3, #2
	bhi	.L3458
	ldrh	r5, [r7, #2388]
	b	.L3459
.L3458:
	ldr	r2, .L3544
	movs	r1, #0
	str	r1, [r2, #3280]
	ldr	r2, .L3544+4
	ldrh	r0, [r2, #976]
	cmp	r0, #0
	bne	.L3527
	adds	r0, r3, #1
	b	.L3527
.L3467:
	movs	r0, #0
	b	.L3527
.L3470:
	mov	r0, r2
	b	.L3527
.L3534:
	ldr	r2, [r5, #3276]
	ldr	r5, .L3544+8
	cbnz	r2, .L3391
	ldrh	r2, [r5, #2340]
	cmp	r2, #3
	beq	.L3391
	ldr	r2, [r9, #604]
	cbnz	r2, .L3391
	ldr	r2, [r5, #2244]
	cbnz	r2, .L3391
	ldrb	r0, [r5, #144]	@ zero_extendqisi2
	cbz	r0, .L3392
.L3391:
	ldr	r2, [r7, #296]
	ldrh	r0, [r5, #2340]
	ldrh	r1, [r2, r3, lsl #1]
	cmp	r0, #3
	ldrh	r3, [r5, #2390]
	ldrh	r2, [r5, #2320]
	mul	r2, r2, r3
	ite	eq
	lsreq	r3, r3, #1
	movne	r3, #0
	add	r3, r3, r2
	cmp	r1, r3
	bgt	.L3394
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #2448]
	ldr	r2, .L3544
	ldr	r1, [r7, #468]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	ite	hi
	movhi	r3, #128
	movls	r3, #160
	strh	r3, [r2, #1122]	@ movhi
	uxth	r4, r0
	b	.L3396
.L3394:
	movs	r3, #128
	b	.L3530
.L3392:
	ldr	r2, [r9, #296]
	ldrh	r3, [r2, r3, lsl #1]
	cmp	r3, #7
	bhi	.L3397
	bl	List_get_gc_head_node
	movs	r3, #128
	strh	r3, [r7, #1122]	@ movhi
	uxth	r4, r0
	b	.L3396
.L3397:
	movs	r3, #64
.L3530:
	strh	r3, [r7, #1122]	@ movhi
	b	.L3398
.L3389:
	movs	r3, #80
	strh	r3, [r5, #1122]	@ movhi
	b	.L3398
.L3396:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L3398
.L3388:
	ldr	r1, [r7, #232]
	ldr	r3, [r7, #296]
	ldrh	r2, [r7, #312]
	ldrh	r1, [r1, r4, lsl #1]
	ldrh	r3, [r3, r4, lsl #1]
	ldr	r0, .L3544+12
	str	r1, [sp]
	ldrh	r1, [r7, #1120]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
	b	.L3398
.L3401:
	cmp	r4, #12
	bls	.L3402
	lsrs	r5, r5, #4
	b	.L3400
.L3402:
	cmp	r4, #8
	bls	.L3400
	lsrs	r5, r5, #2
	b	.L3400
.L3474:
	movs	r5, #1
	b	.L3400
.L3536:
	ldrh	r3, [r2, #412]
	movw	r1, #65535
	cmp	r3, r1
	bne	.L3405
	ldrh	r1, [r2, #1174]
	cmp	r1, r3
	bne	.L3405
	ldr	r3, .L3544+4
	ldrh	r0, [r3, #976]
	cbnz	r0, .L3406
	ldr	r3, .L3544+8
	ldr	r2, [r2, #468]
	ldr	r3, [r3, #2448]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L3475
.L3406:
	ldrh	r3, [r7, #536]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
	b	.L3407
.L3545:
	.align	2
.L3544:
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LANCHOR0
	.word	.LC158
.L3475:
	movs	r3, #18
.L3407:
	strh	r3, [r7, #1120]	@ movhi
	movs	r3, #0
	str	r3, [r7, #1132]
	b	.L3527
.L3405:
	ldrh	r3, [r7, #536]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r7, #1120]	@ movhi
	b	.L3404
.L3537:
	cmp	r6, #2
	bhi	.L3477
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L3477
.L3533:
	cmp	r8, #0
	beq	.L3464
	b	.L3399
.L3385:
	cmp	r8, #0
	beq	.L3464
	b	.L3465
.L3384:
	cmp	r8, #0
	beq	.L3464
	b	.L3415
.L3387:
	cmp	r8, #0
	beq	.L3464
	b	.L3466
.L3527:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
	.fnend
	.size	ftl_do_gc, .-ftl_do_gc
	.align	1
	.global	FtlCacheWriteBack
	.thumb
	.thumb_func
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r3, .L3589
	ldr	r7, .L3589+4
	ldr	r5, [r3, #984]
	ldr	r3, [r7, #224]
	cmp	r3, #0
	bne	.L3548
	ldr	r4, .L3589+8
	ldr	r1, [r4, #2440]
	cmp	r1, #0
	beq	.L3548
	ldrb	r3, [r4, #144]	@ zero_extendqisi2
	cbz	r3, .L3571
	ldrb	r8, [r5, #8]	@ zero_extendqisi2
	add	r0, r8, #-1
	rsbs	r8, r0, #0
	adc	r8, r8, r0
	b	.L3550
.L3571:
	mov	r8, r3
.L3550:
	ldr	r0, [r4, #2444]
	mov	r2, r8
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r9, #0
	bl	FlashProgPages
	ldr	r10, .L3589+8
	mov	fp, #36
.L3551:
	ldr	r3, [r4, #2440]
	cmp	r9, r3
	bcs	.L3569
	mul	r6, fp, r9
	ldr	r3, [r10, #2444]
	adds	r2, r3, r6
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	beq	.L3572
	ldr	r3, [r2, #4]
	cmp	r8, #0
	beq	.L3585
	orr	r3, r3, #-2147483648
.L3585:
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	movs	r2, #1
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2444]
	add	r6, r6, r3
	ldr	r3, [r6, #12]
	ldr	r0, [r3, #12]
	adds	r2, r0, #1
	beq	.L3555
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r7, #296]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r6, r0
	cbnz	r2, .L3556
	ldr	r0, .L3589+12
	mov	r1, r6
	bl	printk
.L3556:
	mov	r0, r6
	bl	decrement_vpc_count
.L3555:
	add	r9, r9, #1
	b	.L3551
.L3587:
	movw	r5, #16386
.L3568:
	ldrh	r3, [r7, #1182]
	cbz	r3, .L3569
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3568
.L3569:
	movs	r3, #0
	str	r3, [r4, #2440]
	b	.L3548
.L3572:
	ldr	r10, .L3589+8
	mov	r9, #0
.L3552:
	ldr	r3, [r4, #2440]
	cmp	r9, r3
	bcs	.L3587
	movs	r6, #36
	ldr	r3, [r10, #2444]
	mov	fp, #0
	mov	r2, #-1
	mul	r6, r6, r9
	str	r2, [r3, r6]
.L3558:
	ldr	r3, [r4, #2444]
	adds	r2, r3, r6
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	bne	.L3588
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3559
	ldr	r1, [r7, #296]
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r10, #2388]
	strb	fp, [r5, #6]
	strh	fp, [r5, #4]	@ movhi
	strh	r3, [r5, #2]	@ movhi
.L3559:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L3560
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3560:
	ldr	r3, [r7, #704]
	adds	r3, r3, #1
	str	r3, [r7, #704]
	ldr	r3, [r4, #2444]
	add	r3, r3, r6
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #2444]
	mov	r2, r8
	movs	r1, #1
	add	r3, r3, r6
	str	r0, [sp, #4]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #2444]
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	ittt	eq
	moveq	r3, #1
	ldreq	r2, .L3589+4
	streq	r3, [r2, #224]
	ldr	r3, [r7, #224]
	cmp	r3, #0
	beq	.L3558
	b	.L3548
.L3588:
	ldr	r3, [r2, #4]
	cmp	r8, #0
	beq	.L3586
	orr	r3, r3, #-2147483648
.L3586:
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	movs	r2, #1
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2444]
	add	r6, r6, r3
	ldr	r3, [r6, #12]
	ldr	r0, [r3, #12]
	adds	r3, r0, #1
	beq	.L3565
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r7, #296]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r6, r0
	cbnz	r2, .L3566
	ldr	r0, .L3589+12
	mov	r1, r6
	bl	printk
.L3566:
	mov	r0, r6
	bl	decrement_vpc_count
.L3565:
	add	r9, r9, #1
	b	.L3552
.L3548:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3590:
	.align	2
.L3589:
	.word	.LANCHOR5
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC159
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	1
	.global	FtlSysFlush
	.thumb
	.thumb_func
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3593
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r3, #224]
	cbnz	r3, .L3592
	ldr	r3, .L3593+4
	ldr	r4, [r3, #504]
	cmp	r4, #1
	bne	.L3592
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3592:
	movs	r0, #0
	pop	{r4, pc}
.L3594:
	.align	2
.L3593:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.fnend
	.size	FtlSysFlush, .-FtlSysFlush
	.align	1
	.global	FtlDeInit
	.thumb
	.thumb_func
	.type	FtlDeInit, %function
FtlDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	ldr	r3, .L3597
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3596
	bl	FtlSysFlush
.L3596:
	movs	r0, #0
	pop	{r3, pc}
.L3598:
	.align	2
.L3597:
	.word	.LANCHOR1
	.fnend
	.size	FtlDeInit, .-FtlDeInit
	.align	1
	.global	ftl_deinit
	.thumb
	.thumb_func
	.type	ftl_deinit, %function
ftl_deinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	ftl_flash_de_init
	bl	FtlDeInit
	pop	{r3, lr}
	b	ftl_flash_de_init
	.fnend
	.size	ftl_deinit, .-ftl_deinit
	.align	1
	.global	rk_ftl_de_init
	.thumb
	.thumb_func
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	movs	r1, #0
	ldr	r0, .L3601
	bl	printk
	pop	{r3, lr}
	b	ftl_deinit
.L3602:
	.align	2
.L3601:
	.word	.LC160
	.fnend
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	1
	.global	ftl_cache_flush
	.thumb
	.thumb_func
	.type	ftl_cache_flush, %function
ftl_cache_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	FtlCacheWriteBack
	.fnend
	.size	ftl_cache_flush, .-ftl_cache_flush
	.align	1
	.global	rk_ftl_cache_write_back
	.thumb
	.thumb_func
	.type	rk_ftl_cache_write_back, %function
rk_ftl_cache_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	FtlCacheWriteBack
	.fnend
	.size	rk_ftl_cache_write_back, .-rk_ftl_cache_write_back
	.align	1
	.global	ftl_discard
	.thumb
	.thumb_func
	.type	ftl_discard, %function
ftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r6, r0
	ldr	r7, .L3624
	mov	r4, r1
	ldr	r5, [r7, #2428]
	cmp	r0, r5
	bcs	.L3614
	cmp	r1, r5
	bhi	.L3614
	adds	r3, r0, r1
	cmp	r3, r5
	bhi	.L3614
	cmp	r1, #31
	bls	.L3616
	ldr	r3, .L3624+4
	ldr	r2, [r3, #224]
	mov	r8, r3
	cmp	r2, #0
	bne	.L3616
	bl	FtlCacheWriteBack
	ldrh	r5, [r7, #2394]
	mov	r0, r6
	mov	r1, r5
	bl	__aeabi_uidiv
	smulbb	r3, r0, r5
	mov	r9, r0
	subs	r6, r6, r3
	uxth	r6, r6
	cbz	r6, .L3607
	subs	r5, r5, r6
	add	r9, r0, #1
	cmp	r5, r4
	it	cs
	movcs	r5, r4
	uxth	r5, r5
	subs	r4, r4, r5
.L3607:
	ldr	r5, .L3624+8
	mov	r3, #-1
	str	r3, [sp, #4]
.L3608:
	ldrh	r3, [r7, #2394]
	cmp	r4, r3
	bcc	.L3623
	mov	r0, r9
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L3609
	ldr	r3, [r5, #988]
	add	r1, sp, #4
	movs	r2, #1
	mov	r0, r9
	adds	r3, r3, #1
	str	r3, [r5, #988]
	ldr	r3, [r8, #480]
	adds	r3, r3, #1
	str	r3, [r8, #480]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3609:
	ldrh	r3, [r7, #2394]
	add	r9, r9, #1
	subs	r4, r4, r3
	b	.L3608
.L3623:
	ldr	r3, .L3624+8
	ldr	r2, [r3, #988]
	cmp	r2, #32
	bls	.L3616
	movs	r2, #0
	str	r2, [r3, #988]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3616
.L3614:
	mov	r0, #-1
	b	.L3606
.L3616:
	movs	r0, #0
.L3606:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3625:
	.align	2
.L3624:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR5
	.fnend
	.size	ftl_discard, .-ftl_discard
	.align	1
	.global	FtlDiscard
	.thumb
	.thumb_func
	.type	FtlDiscard, %function
FtlDiscard:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_discard
	.fnend
	.size	FtlDiscard, .-FtlDiscard
	.align	1
	.global	ftl_read
	.thumb
	.thumb_func
	.type	ftl_read, %function
ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L3672
	.pad #84
	sub	sp, sp, #84
	mov	r5, r1
	str	r2, [sp, #36]
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3653
	cmp	r0, #16
	bne	.L3629
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r8
	bl	FtlVendorPartRead
	b	.L3628
.L3629:
	ldr	r2, .L3672+4
	ldr	r3, [r2, #2428]
	cmp	r1, r3
	bcs	.L3653
	ldr	r1, [sp, #36]
	cmp	r1, r3
	bhi	.L3653
	adds	r1, r5, r1
	str	r1, [sp, #40]
	cmp	r1, r3
	bhi	.L3653
	ldrh	r4, [r2, #2394]
	mov	r0, r5
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r1, r4
	ldr	r3, [sp, #40]
	str	r0, [sp, #28]
	subs	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #36]
	rsb	r3, r3, #1
	str	r0, [sp, #32]
	add	r3, r3, r0
	str	r3, [sp, #24]
	ldr	r3, .L3672+8
	ldr	r0, [sp, #28]
	ldr	r2, [r3, #504]
	add	r2, r2, r1
	ldr	r1, [sp, #24]
	str	r2, [r3, #504]
	ldr	r2, [r3, #476]
	add	r2, r2, r1
	ldr	r1, [sp, #32]
	str	r2, [r3, #476]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3630
	bl	FtlCacheWriteBack
.L3630:
	mov	r10, #0
	ldr	r6, [sp, #28]
	ldr	r4, .L3672+8
	ldr	r9, .L3672+4
	mov	r7, r10
	str	r10, [sp, #48]
	str	r10, [sp, #52]
.L3631:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3671
	add	r1, sp, #76
	mov	r0, r6
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp, #76]
	adds	r1, r3, #1
	bne	.L3668
	mov	fp, #0
.L3632:
	ldrh	r0, [r9, #2394]
	cmp	fp, r0
	bcs	.L3636
	mla	r0, r0, r6, fp
	cmp	r0, r5
	bcc	.L3634
	ldr	r3, [sp, #40]
	cmp	r0, r3
	bcs	.L3634
	subs	r0, r0, r5
	movs	r1, #0
	mov	r2, #512
	add	r0, r8, r0, lsl #9
	bl	ftl_memset
.L3634:
	add	fp, fp, #1
	b	.L3632
.L3668:
	ldr	r2, [r4, #3304]
	mov	fp, #36
	mla	fp, fp, r7, r2
	str	r3, [fp, #4]
	ldr	r3, [sp, #28]
	cmp	r6, r3
	bne	.L3637
	ldrh	ip, [r9, #2394]
	mov	r0, r5
	ldr	r3, [r4, #3332]
	mov	r1, ip
	str	ip, [sp, #44]
	str	r3, [fp, #8]
	bl	__aeabi_uidivmod
	ldr	ip, [sp, #44]
	ldr	r2, [sp, #36]
	rsb	r3, r1, ip
	str	r1, [sp, #56]
	cmp	r3, r2
	it	cs
	movcs	r3, r2
	cmp	r3, ip
	str	r3, [sp, #48]
	bne	.L3638
	str	r8, [fp, #8]
	b	.L3638
.L3637:
	ldr	r3, [sp, #32]
	cmp	r6, r3
	bne	.L3639
	ldr	r3, [r4, #3336]
	ldrh	r2, [r9, #2394]
	ldr	r1, [sp, #40]
	str	r3, [fp, #8]
	mul	r3, r2, r6
	rsb	r10, r3, r1
	cmp	r10, r2
	bne	.L3638
	b	.L3669
.L3639:
	ldrh	r3, [r9, #2394]
	muls	r3, r6, r3
.L3669:
	subs	r3, r3, r5
	add	r3, r8, r3, lsl #9
	str	r3, [fp, #8]
.L3638:
	ldrh	r3, [r9, #2400]
	ldr	r2, [r4, #3344]
	str	r6, [fp, #16]
	muls	r3, r7, r3
	adds	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r3, r2
	str	r3, [fp, #12]
.L3636:
	ldr	r3, [sp, #24]
	adds	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #24]
	beq	.L3640
	ldrh	r3, [r9, #2320]
	cmp	r7, r3, lsl #3
	bne	.L3631
.L3640:
	cmp	r7, #0
	beq	.L3631
	ldr	r0, [r4, #3304]
	mov	r1, r7
	movs	r2, #0
	bl	FlashReadPages
	ldr	r3, [sp, #56]
	lsls	r3, r3, #9
	str	r3, [sp, #60]
	ldr	r3, [sp, #48]
	lsls	r3, r3, #9
	str	r3, [sp, #64]
	lsl	r3, r10, #9
	str	r3, [sp, #68]
	movs	r3, #0
	str	r3, [sp, #44]
.L3647:
	ldr	r3, [sp, #44]
	mov	ip, #36
	ldr	r1, [sp, #28]
	mul	fp, ip, r3
	ldr	r3, [r4, #3304]
	add	r3, r3, fp
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L3642
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3332]
	cmp	r1, r3
	bne	.L3643
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3670
.L3642:
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L3643
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3336]
	cmp	r1, r3
	bne	.L3643
	ldrh	r0, [r9, #2394]
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #68]
	muls	r0, r3, r0
	subs	r0, r0, r5
	add	r0, r8, r0, lsl #9
.L3670:
	bl	ftl_memcpy
.L3643:
	ldr	r2, [r4, #3304]
	add	r3, r2, fp
	ldr	r1, [r2, fp]
	adds	r2, r1, #1
	itttt	eq
	streq	r1, [sp, #52]
	ldreq	r2, [r4, #680]
	addeq	r2, r2, #1
	streq	r2, [r4, #680]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L3645
	ldr	r2, [r4, #680]
	ldr	r0, .L3672+12
	adds	r2, r2, #1
	str	r2, [r4, #680]
	ldr	lr, [r3, #12]
	ldr	r2, [r3, #8]
	ldr	r1, [lr, #4]
	str	r1, [sp]
	ldr	r1, [lr, #8]
	str	r1, [sp, #4]
	ldr	r1, [lr, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	str	r2, [sp, #16]
	ldr	r1, [r3, #16]
	ldr	r2, [r3, #4]
	ldr	r3, [lr]
	bl	printk
.L3645:
	ldr	r3, [r4, #3304]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmp	r3, #256
	bne	.L3646
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3646:
	ldr	r3, [sp, #44]
	adds	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r7
	bne	.L3647
	movs	r7, #0
	b	.L3631
.L3671:
	ldr	r3, .L3672+8
	ldrh	r3, [r3, #1182]
	cbz	r3, .L3649
	ldr	r0, [sp, #24]
	movs	r1, #1
	bl	ftl_do_gc
.L3649:
	ldr	r0, [sp, #52]
	b	.L3628
.L3653:
	mov	r0, #-1
.L3628:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3673:
	.align	2
.L3672:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC148
	.fnend
	.size	ftl_read, .-ftl_read
	.align	1
	.global	ftl_vendor_read
	.thumb
	.thumb_func
	.type	ftl_vendor_read, %function
ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, r0
	mov	r4, r1
	mov	r3, r2
	mov	r1, r5
	mov	r2, r4
	movs	r0, #16
	pop	{r4, r5, lr}
	b	ftl_read
	.fnend
	.size	ftl_vendor_read, .-ftl_vendor_read
	.align	1
	.global	FlashBootVendorRead
	.thumb
	.thumb_func
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	mov	r5, r1
	mov	r4, r2
	bl	rknand_device_lock
	ldr	r3, .L3678
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3677
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
	b	.L3676
.L3677:
	mov	r4, #-1
.L3676:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3679:
	.align	2
.L3678:
	.word	.LANCHOR1
	.fnend
	.size	FlashBootVendorRead, .-FlashBootVendorRead
	.align	1
	.global	ftl_sys_read
	.thumb
	.thumb_func
	.type	ftl_sys_read, %function
ftl_sys_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r1
	mov	r3, r2
	add	r1, r0, #256
	mov	r2, r4
	movs	r0, #16
	pop	{r4, lr}
	b	ftl_read
	.fnend
	.size	ftl_sys_read, .-ftl_sys_read
	.align	1
	.global	StorageSysDataLoad
	.thumb
	.thumb_func
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r2, #512
	mov	r5, r0
	mov	r4, r1
	mov	r0, r1
	movs	r1, #0
	bl	ftl_memset
	bl	rknand_device_lock
	mov	r2, r4
	movs	r1, #1
	mov	r0, r5
	bl	ftl_sys_read
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r3, r4, r5, pc}
	.fnend
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	1
	.global	FtlRead
	.thumb
	.thumb_func
	.type	FtlRead, %function
FtlRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_read
	.fnend
	.size	FtlRead, .-FtlRead
	.align	1
	.global	FtlInit
	.thumb
	.thumb_func
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r3, #-1
	ldr	r2, .L3699
	ldr	r7, .L3699+4
	ldr	r4, .L3699+8
	ldr	r6, .L3699+12
	ldr	r1, .L3699+16
	ldr	r0, .L3699+20
	str	r3, [r7, #504]
	movs	r3, #0
	str	r3, [r2, #992]
	str	r3, [r4, #224]
	bl	printk
	add	r0, r6, #116
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r6, #2324]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3684
	ldr	r0, .L3699+24
	b	.L3698
.L3684:
	bl	FtlSysBlkInit
	mov	r5, r0
	cbz	r0, .L3686
	ldr	r0, .L3699+28
.L3698:
	ldr	r1, .L3699+32
	bl	printk
	b	.L3685
.L3686:
	movs	r1, #1
	str	r1, [r7, #504]
	bl	ftl_do_gc
	ldrh	r7, [r4, #312]
	cmp	r7, #15
	bhi	.L3687
	mov	r8, r4
	movw	r6, #65535
.L3690:
	ldrh	r3, [r4, #556]
	cmp	r3, r6
	bne	.L3688
	ldrh	r3, [r8, #1174]
	cmp	r3, r6
	bne	.L3688
	and	r0, r5, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3688:
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	movs	r0, #0
	movs	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r4, #312]
	adds	r3, r7, #2
	cmp	r2, r3
	bhi	.L3685
	adds	r5, r5, #1
	cmp	r5, #4096
	bne	.L3690
	b	.L3685
.L3687:
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cbz	r3, .L3685
	movs	r4, #128
.L3692:
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3692
.L3685:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3700:
	.align	2
.L3699:
	.word	.LANCHOR5
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC77
	.word	.LC76
	.word	.LC161
	.word	.LC162
	.word	.LANCHOR3+240
	.fnend
	.size	FtlInit, .-FtlInit
	.align	1
	.global	rk_ftl_init
	.thumb
	.thumb_func
	.type	rk_ftl_init, %function
rk_ftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #2048
	bl	ftl_malloc
	ldr	r5, .L3705
	ldr	r4, .L3705+4
	movs	r6, #0
	add	r1, r5, #1000
	str	r6, [r5, #1000]
	str	r6, [r4, #1180]
	str	r0, [r5, #996]
	addw	r0, r4, #1180
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #1180]
	cbz	r3, .L3704
	bl	rk_nandc_irq_init
	mov	r1, r6
	mov	r2, r6
	mov	r3, #2048
	ldr	r0, [r5, #996]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #1180]
	bl	FlashInit
	mov	r4, r0
	cbnz	r0, .L3703
	bl	FtlInit
.L3703:
	mov	r1, r4
	ldr	r0, .L3705+8
	bl	printk
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3704:
	mov	r0, #-1
	pop	{r4, r5, r6, pc}
.L3706:
	.align	2
.L3705:
	.word	.LANCHOR5
	.word	.LANCHOR4
	.word	.LC163
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	1
	.global	ftl_fix_nand_power_lost_error
	.thumb
	.thumb_func
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3722
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #48
	sub	sp, sp, #48
	ldrb	r2, [r3, #144]	@ zero_extendqisi2
	mov	r8, r3
	cmp	r2, #0
	beq	.L3707
	ldr	r4, .L3722+4
	movw	r7, #4097
	ldr	r6, .L3722+8
	ldr	r0, .L3722+12
	ldr	r3, [r4, #296]
	ldrh	r5, [r6, #840]
	mov	r1, r5
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldrh	r0, [r4, #316]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4, #364]
	bl	FtlGcRefreshOpenBlock
	add	r0, r4, #316
	bl	allocate_new_data_superblock
	add	r0, r4, #364
	bl	allocate_new_data_superblock
	lsl	r9, r5, #1
.L3709:
	subs	r7, r7, #1
	beq	.L3713
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #296]
	ldrh	r3, [r3, r9]
	cmp	r3, #0
	bne	.L3709
.L3713:
	ldr	r3, [r4, #296]
	mov	r1, r5
	ldr	r0, .L3722+12
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldr	r3, [r4, #296]
	ldrh	r7, [r3, r5, lsl #1]
	cbnz	r7, .L3711
	add	r0, sp, #48
	mov	r10, #36
	movw	r9, #65535
	strh	r5, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldr	r3, .L3722+4
	ldrh	ip, [r8, #2320]
	ldr	r8, [r3, #228]
	mov	r3, r7
	mov	lr, r3
	add	r0, sp, #14
.L3714:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3721
	ldrh	r2, [r0, #2]!
	cmp	r2, r9
	beq	.L3715
	mla	r1, r10, r7, r8
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [r1, #4]
	str	lr, [r1, #8]
	str	lr, [r1, #12]
.L3715:
	adds	r3, r3, #1
	b	.L3714
.L3721:
	ldr	r3, [r4, #296]
	mov	r1, r5
	ldr	r0, .L3722+16
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	movs	r1, #0
	mov	r2, r7
	ldr	r0, [r4, #228]
	bl	FlashEraseBlocks
	ldr	r0, [r4, #228]
	movs	r1, #1
	mov	r2, r7
	bl	FlashEraseBlocks
.L3711:
	movw	r3, #65535
	strh	r3, [r6, #840]	@ movhi
.L3707:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3723:
	.align	2
.L3722:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LC164
	.word	.LC165
	.fnend
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	1
	.global	rk_ftl_garbage_collect
	.thumb
	.thumb_func
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_do_gc
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	1
	.global	ftl_write
	.thumb
	.thumb_func
	.type	ftl_write, %function
ftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r9, .L3800+16
	mov	r8, r1
	str	r3, [sp, #12]
	mov	fp, r2
	ldr	r3, [r9, #224]
	cmp	r3, #0
	bne	.L3767
	ldr	r2, .L3800
	ldr	r2, [r2, #504]
	cmp	r2, #1
	bne	.L3768
	cmp	r0, #16
	bne	.L3727
	add	r0, r1, #256
	ldr	r2, [sp, #12]
	mov	r1, fp
	bl	FtlVendorPartWrite
	b	.L3726
.L3727:
	ldr	r7, .L3800+4
	ldr	r3, [r7, #2428]
	cmp	r1, r3
	bcs	.L3771
	cmp	fp, r3
	bhi	.L3771
	add	r6, r1, fp
	cmp	r6, r3
	bhi	.L3771
	ldrh	r4, [r7, #2394]
	mov	r3, #2048
	ldr	r5, .L3800+8
	mov	r0, r1
	mov	r1, r4
	str	r3, [r5, #1004]
	bl	__aeabi_uidiv
	mov	r1, r4
	str	r0, [sp, #4]
	subs	r0, r6, #1
	bl	__aeabi_uidiv
	cmp	fp, r4, lsl #1
	ldr	r2, [sp, #4]
	str	r0, [sp, #20]
	sub	r6, r0, r2
	add	r3, r6, #1
	str	r3, [sp, #8]
	ldr	r2, [sp, #8]
	ldr	r3, [r9, #484]
	add	r3, r3, r2
	ldr	r2, [r7, #2440]
	str	r3, [r9, #484]
	ldr	r3, [r9, #500]
	add	r3, r3, fp
	str	r3, [r9, #500]
	ite	cs
	movcs	r3, #1
	movcc	r3, #0
	str	r3, [sp, #28]
	cmp	r2, #0
	beq	.L3729
	movs	r3, #36
	ldr	r7, [r7, #2444]
	muls	r3, r2, r3
	ldr	r2, [sp, #4]
	subs	r3, r3, #36
	add	r7, r7, r3
	ldr	r3, [r7, #16]
	cmp	r2, r3
	bne	.L3730
	ldr	r3, [r9, #488]
	mov	r1, r4
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r9, #488]
	ldr	r3, [r5, #1008]
	adds	r3, r3, #1
	str	r3, [r5, #1008]
	bl	__aeabi_uidivmod
	ldr	r0, [r7, #8]
	subs	r4, r4, r1
	add	r0, r0, r1, lsl #9
	cmp	r4, fp
	ldr	r1, [sp, #12]
	it	cs
	movcs	r4, fp
	lsl	r9, r4, #9
	mov	r2, r9
	bl	ftl_memcpy
	cbnz	r6, .L3731
	ldr	r3, [r5, #1008]
	cmp	r3, #2
	ble	.L3767
.L3731:
	ldr	r3, [sp, #12]
	rsb	fp, r4, fp
	add	r8, r8, r4
	str	r6, [sp, #8]
	add	r3, r3, r9
	str	r3, [sp, #12]
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
.L3730:
	movs	r3, #0
	str	r3, [r5, #1008]
.L3729:
	ldr	r0, [sp, #4]
	ldr	r1, [sp, #20]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3732
	bl	FtlCacheWriteBack
.L3732:
	ldr	r4, .L3800+4
	ldr	r7, .L3800+12
	ldr	r6, [sp, #4]
	mov	r10, r4
	str	r7, [r5, #984]
.L3733:
	ldr	r3, [sp, #8]
	ldr	r5, .L3800+16
	cmp	r3, #0
	beq	.L3799
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3734
	add	r2, r5, #316
	ldr	r9, .L3800
	cmp	r7, r2
	bne	.L3735
	ldrh	r7, [r5, #368]
	cbnz	r7, .L3736
	add	r0, r5, #364
	bl	allocate_new_data_superblock
	str	r7, [r9, #3452]
.L3736:
	ldr	r0, .L3800+12
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3452]
	cbnz	r3, .L3772
.L3738:
	ldr	r7, .L3800+12
	b	.L3737
.L3735:
	str	r3, [r9, #3452]
	ldrh	r3, [r5, #320]
	cmp	r3, #0
	bne	.L3738
	mov	r0, r7
	bl	allocate_new_data_superblock
	b	.L3737
.L3772:
	ldr	r7, .L3800+20
.L3737:
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3739
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3739:
	ldr	r3, .L3800+8
	str	r7, [r3, #984]
.L3734:
	ldr	r2, [r5, #3300]
	ldr	r3, [r4, #2440]
	ldrh	ip, [r7, #4]
	subs	r3, r2, r3
	ldr	r2, [sp, #8]
	cmp	ip, r3
	ldr	r9, .L3800+16
	it	cs
	movcs	ip, r3
	cmp	ip, r2
	mov	r3, ip
	it	cs
	movcs	r3, r2
	str	r3, [sp, #44]
	movs	r3, #0
	str	r3, [sp, #24]
.L3740:
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3741
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	beq	.L3741
	ldr	r3, [sp, #28]
	cbz	r3, .L3742
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3742
	ldr	r3, [sp, #24]
	cbz	r3, .L3742
	ldrh	r2, [r4, #2394]
	add	r3, r8, fp
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3741
.L3742:
	add	r1, sp, #48
	movs	r2, #0
	mov	r0, r6
	movs	r5, #36
	bl	log2phys
	mov	r0, r7
	bl	get_new_active_ppa
	ldr	r3, [r4, #2440]
	ldr	r1, [r4, #2444]
	ldrh	r2, [r4, #2400]
	mla	r1, r5, r3, r1
	mul	ip, r3, r2
	str	r6, [r1, #16]
	str	r0, [r1, #4]
	lsr	r0, ip, #2
	str	r0, [sp, #36]
	ldr	r0, [r9, #3348]
	mov	lr, r0
	str	r0, [sp, #40]
	ldr	r0, [sp, #36]
	add	r0, lr, r0, lsl #2
	ldrh	lr, [r4, #2398]
	str	r0, [sp, #16]
	str	r0, [r1, #12]
	mul	r3, r3, lr
	ldr	r0, [r9, #3328]
	bic	r3, r3, #3
	add	r3, r3, r0
	ldr	r0, [sp, #16]
	str	r3, [r1, #8]
	movs	r1, #0
	bl	ftl_memset
	ldr	r3, [sp, #4]
	cmp	r6, r3
	beq	.L3743
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3796
	ldrh	r3, [r4, #2394]
	add	r5, r8, fp
	smulbb	r3, r3, r6
	subs	r5, r5, r3
	movs	r3, #0
	str	r3, [sp, #32]
	uxth	r5, r5
	b	.L3746
.L3743:
	ldrh	r5, [r4, #2394]
	mov	r0, r8
	mov	r1, r5
	bl	__aeabi_uidivmod
	subs	r5, r5, r1
	str	r1, [sp, #32]
	cmp	r5, fp
	it	cs
	movcs	r5, fp
.L3746:
	ldrh	r3, [r4, #2394]
	cmp	r5, r3
	bne	.L3747
	ldr	r3, [sp, #4]
	mov	lr, #36
	cmp	r6, r3
	ittet	ne
	mulne	r1, r6, r5
	ldrne	r3, [sp, #12]
	ldreq	r1, [sp, #12]
	rsbne	r1, r8, r1
	it	ne
	addne	r1, r3, r1, lsl #9
	ldr	r3, [sp, #28]
	cbz	r3, .L3749
	ldr	r2, [r10, #2440]
	ldr	r5, [r10, #2444]
	mla	r2, lr, r2, r5
	str	r1, [r2, #8]
	b	.L3750
.L3749:
	ldr	r0, [r10, #2444]
	ldr	r3, [r10, #2440]
	ldrh	r2, [r10, #2398]
	mla	r3, lr, r3, r0
	ldr	r0, [r3, #8]
	b	.L3797
.L3747:
	ldr	r2, [sp, #48]
	movs	r3, #36
	adds	r1, r2, #1
	beq	.L3751
	ldr	r1, [r4, #2444]
	add	r0, sp, #52
	str	r2, [sp, #56]
	ldr	r2, [r4, #2440]
	str	r6, [sp, #68]
	mla	r3, r3, r2, r1
	movs	r1, #1
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #60]
	movs	r2, #0
	str	r3, [sp, #64]
	bl	FlashReadPages
	ldr	r3, [sp, #52]
	adds	r3, r3, #1
	bne	.L3752
	ldr	r3, [r9, #680]
	adds	r3, r3, #1
	str	r3, [r9, #680]
	b	.L3754
.L3801:
	.align	2
.L3800:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LANCHOR2+316
	.word	.LANCHOR2
	.word	.LANCHOR2+364
.L3752:
	ldr	r3, [sp, #16]
	ldr	r3, [r3, #8]
	cmp	r3, r6
	beq	.L3754
	ldr	r3, [r9, #680]
	mov	r2, r6
	ldr	r0, .L3802
	adds	r3, r3, #1
	str	r3, [r9, #680]
	ldr	r3, [sp, #16]
	ldr	r1, [r3, #8]
	bl	printk
	b	.L3754
.L3751:
	ldr	r1, [r4, #2444]
	ldr	r2, [r4, #2440]
	mla	r2, r3, r2, r1
	movs	r1, #0
	ldr	r0, [r2, #8]
	ldrh	r2, [r4, #2398]
	bl	ftl_memset
.L3754:
	ldr	r3, [sp, #4]
	mov	lr, #36
	lsls	r2, r5, #9
	cmp	r6, r3
	bne	.L3755
	ldr	r1, [r4, #2440]
	ldr	r5, [r4, #2444]
	ldr	r3, [sp, #32]
	mla	r1, lr, r1, r5
	ldr	r0, [r1, #8]
	ldr	r1, [sp, #12]
	add	r0, r0, r3, lsl #9
	b	.L3797
.L3755:
	ldr	r0, [r4, #2444]
	ldr	r3, [r4, #2440]
	ldrh	r1, [r4, #2394]
	mla	r3, lr, r3, r0
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #12]
	add	r1, r3, r1, lsl #9
	b	.L3797
.L3796:
	ldr	r3, [sp, #28]
	cbz	r3, .L3756
	ldr	r3, [r4, #2440]
	ldr	r2, [r4, #2444]
	mla	r5, r5, r3, r2
	ldrh	r3, [r4, #2394]
	ldr	r2, [sp, #12]
	muls	r3, r6, r3
	rsb	r3, r8, r3
	add	r3, r2, r3, lsl #9
	str	r3, [r5, #8]
	b	.L3750
.L3756:
	ldr	r2, [r4, #2444]
	ldr	r3, [r4, #2440]
	ldrh	r1, [r4, #2394]
	mla	r3, r5, r3, r2
	ldrh	r2, [r4, #2398]
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #12]
	add	r1, r3, r1, lsl #9
.L3797:
	bl	ftl_memcpy
.L3750:
	ldr	r2, [sp, #40]
	movw	r3, #61589
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1, lsl #2]	@ movhi
	ldr	r2, [sp, #16]
	ldr	r3, [r9, #512]
	str	r3, [r2, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	ldr	r2, [sp, #16]
	it	eq
	moveq	r3, #0
	str	r3, [r9, #512]
	ldr	r3, [sp, #16]
	str	r6, [r3, #8]
	adds	r6, r6, #1
	ldr	r3, [sp, #48]
	str	r3, [r2, #12]
	ldrh	r3, [r7]
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, [r4, #2440]
	adds	r3, r3, #1
	str	r3, [r4, #2440]
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp, #24]
	b	.L3740
.L3741:
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #24]
	subs	r3, r3, r2
	str	r3, [sp, #8]
	ldr	r3, .L3802+4
	ldr	r2, [r10, #2440]
	ldr	r3, [r3, #3300]
	cmp	r2, r3
	bcs	.L3760
	ldr	r3, [sp, #28]
	cbnz	r3, .L3760
	ldrh	r3, [r7, #4]
	cbz	r3, .L3760
.L3762:
	movs	r3, #0
	str	r3, [sp, #28]
	b	.L3733
.L3760:
	bl	FtlCacheWriteBack
	movs	r3, #0
	str	r3, [r10, #2440]
	ldr	r3, [sp, #8]
	cmp	r3, #1
	bhi	.L3733
	b	.L3762
.L3799:
	mov	r0, r3
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #20]
	subs	r1, r3, r2
	bl	ftl_do_gc
	ldrh	r3, [r5, #312]
	cmp	r3, #5
	bls	.L3775
	cmp	r3, #31
	bhi	.L3767
	ldr	r3, .L3802+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cbnz	r3, .L3767
.L3775:
	ldr	r4, [sp, #8]
	mov	r8, #128
	ldr	r6, .L3802+4
	movw	r7, #65535
.L3788:
	ldrh	r3, [r5, #556]
	cmp	r3, r7
	bne	.L3766
	ldrh	r3, [r6, #1174]
	cmp	r3, r7
	bne	.L3766
	ldrh	r3, [r6, #1176]
	cmp	r3, r7
	bne	.L3766
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3766:
	movs	r0, #1
	strh	r8, [r5, #1122]	@ movhi
	strh	r8, [r5, #1120]	@ movhi
	mov	r1, r0
	bl	ftl_do_gc
	movs	r0, #0
	movs	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r5, #224]
	cbnz	r3, .L3767
	ldrh	r3, [r6, #312]
	cmp	r3, #2
	bhi	.L3767
	adds	r4, r4, #1
	cmp	r4, #256
	bne	.L3788
	b	.L3767
.L3771:
	mov	r0, #-1
	b	.L3726
.L3767:
	movs	r0, #0
	b	.L3726
.L3768:
	mov	r0, r3
.L3726:
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3803:
	.align	2
.L3802:
	.word	.LC166
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	ftl_write, .-ftl_write
	.align	1
	.global	ftl_vendor_write
	.thumb
	.thumb_func
	.type	ftl_vendor_write, %function
ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, r0
	mov	r4, r1
	mov	r3, r2
	mov	r1, r5
	mov	r2, r4
	movs	r0, #16
	pop	{r4, r5, lr}
	b	ftl_write
	.fnend
	.size	ftl_vendor_write, .-ftl_vendor_write
	.align	1
	.global	FlashBootVendorWrite
	.thumb
	.thumb_func
	.type	FlashBootVendorWrite, %function
FlashBootVendorWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	mov	r5, r1
	mov	r4, r2
	bl	rknand_device_lock
	ldr	r3, .L3808
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3807
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
	b	.L3806
.L3807:
	mov	r4, #-1
.L3806:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3809:
	.align	2
.L3808:
	.word	.LANCHOR1
	.fnend
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
	.align	1
	.global	ftl_sys_write
	.thumb
	.thumb_func
	.type	ftl_sys_write, %function
ftl_sys_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r1
	mov	r3, r2
	add	r1, r0, #256
	mov	r2, r4
	movs	r0, #16
	pop	{r4, lr}
	b	ftl_write
	.fnend
	.size	ftl_sys_write, .-ftl_sys_write
	.align	1
	.global	StorageSysDataStore
	.thumb
	.thumb_func
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r1
	mov	r5, r0
	bl	rknand_device_lock
	mov	r2, r4
	movs	r1, #1
	mov	r0, r5
	bl	ftl_sys_write
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r3, r4, r5, pc}
	.fnend
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	1
	.global	FtlDumpSysBlock
	.thumb
	.thumb_func
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	lsl	r8, r0, #10
	ldr	r5, .L3820
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L3820+4
	mov	r9, #0
	ldr	r7, .L3820+8
	mov	r6, r0
	ldr	r3, [r5, #3316]
	str	r3, [r4, #1256]
	ldr	r3, [r5, #3340]
	str	r3, [r4, #1260]
.L3813:
	ldrh	r2, [r7, #2390]
	sxth	r3, r9
	cmp	r3, r2
	bge	.L3819
	movs	r1, #1
	ldr	r0, .L3820+12
	orr	r3, r3, r8
	str	r3, [r4, #1252]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #1260]
	ldr	r0, .L3820+16
	mov	r1, r6
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #1248]
	str	r3, [sp, #12]
	ldr	r3, [r4, #1256]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #1252]
	bl	printk
	ldr	r3, [r4, #1260]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L3814
	ldr	r0, .L3820+20
	movs	r2, #4
	ldr	r1, [r5, #3316]
	mov	r3, #768
	bl	rknand_print_hex
.L3814:
	add	r9, r9, #1
	b	.L3813
.L3819:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3821:
	.align	2
.L3820:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR4+1248
	.word	.LC167
	.word	.LC168
	.fnend
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	1
	.global	dump_map_info
	.thumb
	.thumb_func
	.type	dump_map_info, %function
dump_map_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L3840
	addw	fp, r4, #2348
	ldrh	r7, [r4, #2328]
	mov	r9, r4
.L3823:
	ldrh	r3, [r4, #2330]
	ldr	r6, .L3840+4
	cmp	r3, r7
	bls	.L3837
	ldr	r1, [r6, #1148]
	mov	r8, #0
	ldrh	ip, [r9, #2320]
	mov	r10, #36
	ldr	r2, [r6, #3304]
	mov	r5, r8
	str	r1, [sp, #24]
	ldr	r3, [r6, #1144]
	ldrh	r1, [r9, #2400]
	str	r1, [sp, #28]
.L3833:
	uxth	r1, r8
	cmp	r1, ip
	bcs	.L3838
	mov	r1, r7
	ldrb	r0, [fp, r8]	@ zero_extendqisi2
	str	r3, [sp, #44]
	str	r2, [sp, #40]
	str	ip, [sp, #36]
	bl	V2P_block
	str	r0, [sp, #32]
	bl	FtlBbmIsBadBlock
	ldr	r1, [sp, #32]
	ldr	ip, [sp, #36]
	ldr	r2, [sp, #40]
	ldr	r3, [sp, #44]
	cbnz	r0, .L3824
	mla	r0, r10, r5, r2
	lsls	r1, r1, #10
	str	r1, [r0, #4]
	ldr	r1, [sp, #28]
	muls	r1, r5, r1
	add	r5, r5, #1
	it	mi
	addmi	r1, r1, #3
	bic	lr, r1, #3
	ldr	r1, [sp, #24]
	uxth	r5, r5
	str	r3, [r0, #8]
	add	r1, r1, lr
	str	r1, [r0, #12]
.L3824:
	add	r8, r8, #1
	b	.L3833
.L3838:
	cbz	r5, .L3828
	ldr	r0, [r6, #3304]
	mov	r1, r5
	movs	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r10, #36
.L3829:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L3828
	ldr	r3, [r6, #3304]
	ldr	r0, .L3840+8
	mla	r3, r10, r8, r3
	add	r8, r8, #1
	adds	r2, r3, #4
	ldmia	r2, {r2, r3, lr}
	ldr	r1, [lr, #4]
	str	r1, [sp]
	ldr	r1, [lr, #8]
	str	r1, [sp, #4]
	ldr	r1, [lr, #12]
	str	r1, [sp, #8]
	ldr	r1, [r3]
	str	r1, [sp, #12]
	ubfx	r1, r2, #10, #16
	ldr	r3, [r3, #4]
	str	r3, [sp, #16]
	ldr	r3, [lr]
	bl	printk
	b	.L3829
.L3828:
	adds	r7, r7, #1
	uxth	r7, r7
	b	.L3823
.L3837:
	ldr	r7, .L3840+12
	mov	r8, #0
.L3832:
	ldrh	r2, [r6, #3452]
	sxth	r3, r8
	ldr	r5, .L3840+4
	cmp	r3, r2
	bge	.L3835
	lsls	r5, r3, #1
	mov	r9, #0
.L3836:
	ldrh	r2, [r4, #2390]
	sxth	r3, r9
	add	r9, r9, #1
	cmp	r3, r2
	bge	.L3839
	ldr	r2, [r6, #3368]
	movs	r1, #1
	ldr	r0, .L3840+16
	ldrh	r2, [r2, r5]
	orr	r3, r3, r2, lsl #10
	mov	r2, r1
	str	r3, [r7, #1252]
	bl	FlashReadPages
	ldr	r3, [r7, #1260]
	ldr	r1, [r6, #3368]
	ldr	r2, [r7, #1256]
	ldr	r0, [r3]
	ldrh	r1, [r1, r5]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L3840+20
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r7, #1248]
	ldr	r3, [r7, #1252]
	bl	printk
	b	.L3836
.L3839:
	add	r8, r8, #1
	b	.L3832
.L3835:
	ldr	r1, [r5, #3368]
	movs	r2, #2
	ldr	r3, [r4, #2416]
	ldr	r0, .L3840+24
	bl	rknand_print_hex
	ldr	r1, [r5, #3388]
	ldrh	r3, [r4, #2424]
	movs	r2, #4
	ldr	r0, .L3840+28
	bl	rknand_print_hex
	ldr	r0, .L3840+32
	ldr	r1, [r5, #3392]
	movs	r2, #4
	ldrh	r3, [r4, #2424]
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3841:
	.align	2
.L3840:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC169
	.word	.LANCHOR4
	.word	.LANCHOR4+1248
	.word	.LC113
	.word	.LC170
	.word	.LC171
	.word	.LC172
	.fnend
	.size	dump_map_info, .-dump_map_info
	.align	1
	.global	flash_boot_enter_slc_mode
	.thumb
	.thumb_func
	.type	flash_boot_enter_slc_mode, %function
flash_boot_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L3844
	ldr	r2, [r3, #2264]
	ldr	r3, .L3844+4
	cmp	r2, r3
	bne	.L3842
	b	flash_enter_slc_mode
.L3842:
	bx	lr
.L3845:
	.align	2
.L3844:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	1
	.global	flash_boot_exit_slc_mode
	.thumb
	.thumb_func
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L3848
	ldr	r2, [r3, #2264]
	ldr	r3, .L3848+4
	cmp	r2, r3
	bne	.L3846
	b	flash_exit_slc_mode
.L3846:
	bx	lr
.L3849:
	.align	2
.L3848:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	1
	.global	write_idblock
	.thumb
	.thumb_func
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 120
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r7, .L3899
	.pad #132
	sub	sp, sp, #132
	mov	r0, #256000
	mov	fp, r1
	mov	r6, r2
	ldr	r3, [r7, #44]
	ldr	r4, [r7, #4]
	ldrb	r5, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	str	r0, [sp, #12]
	cmp	r0, #0
	beq	.L3877
	addw	r2, r8, #511
	lsr	r10, r2, #9
	cmp	r10, #8
	bls	.L3875
	cmp	r10, #500
	bhi	.L3877
	b	.L3852
.L3875:
	mov	r10, #8
.L3852:
	ldr	r2, [fp]
	ldr	r3, .L3899+4
	cmp	r2, r3
	bne	.L3877
	smulbb	r3, r5, r4
	uxth	r3, r3
	str	r3, [sp, #16]
	subs	r0, r3, #1
	mov	r1, r3
	add	r0, r0, r10
	bl	__aeabi_uidiv
	movs	r3, #0
	movw	r2, #63871
	str	r0, [sp, #40]
	add	r0, fp, #254976
	add	r0, r0, #512
.L3856:
	ldr	r1, [r0, #-4]!
	cbnz	r1, .L3853
	ldr	r1, [fp, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #4096
	add	r2, r2, #-1
	it	hi
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3856
	b	.L3855
.L3853:
	ldr	r0, .L3899+8
	bl	printk
.L3855:
	movs	r3, #5
	ldr	r0, .L3899+12
	mov	r1, r6
	movs	r2, #4
	bl	rknand_print_hex
	ldr	r1, [fp, #512]
	ldrb	r2, [r7, #1]	@ zero_extendqisi2
	subs	r4, r6, #4
	ldr	r0, .L3899+16
	bl	printk
	ldr	r2, .L3899+20
	ldrh	r3, [r7, #142]
	mov	r1, r10
	ldr	r0, .L3899+24
	ldr	r2, [r2, #1204]
	ldr	r5, .L3899
	str	r2, [sp]
	mov	r2, r10
	bl	printk
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r2, [fp, #512]
	cmp	r2, r3
	it	hi
	strhi	r3, [fp, #512]
	lsl	r3, r10, #7
	str	r3, [sp, #44]
	movs	r3, #0
	str	r3, [sp, #24]
	str	r3, [sp, #20]
.L3873:
	ldr	r2, [r4, #4]
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r7, .L3899
	cmp	r2, r3
	bcs	.L3858
	ldr	r3, .L3899+20
	ldr	r3, [r3, #1204]
	cmp	r2, r3
	bcc	.L3858
	ldr	r3, [sp, #40]
	cmp	r3, #1
	bls	.L3859
	ldr	r3, [sp, #20]
	cbz	r3, .L3859
	ldr	r3, [r4]
	adds	r3, r3, #1
	cmp	r2, r3
	beq	.L3858
.L3859:
	movs	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #12]
	bl	memset
	ldr	r6, [r4, #4]
	mov	r2, r10
	ldr	r0, .L3899+28
	ldr	r3, [sp, #16]
	muls	r6, r3, r6
	ldr	r3, [r7, #44]
	ldr	r7, [r7, #4]
	ldrb	r9, [r3, #9]	@ zero_extendqisi2
	mov	r1, r6
	bl	printk
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	mov	r1, r9
	mov	r0, r6
	bl	__aeabi_uidiv
	smulbb	r7, r7, r9
	uxth	r7, r7
	mov	r1, r0
	movs	r0, #0
	mov	r2, r0
	bl	FlashEraseBlock
	cmp	r10, r7
	bls	.L3878
	movs	r0, #0
	adds	r1, r6, r7
	mov	r8, #2
	mov	r2, r0
	bl	FlashEraseBlock
	b	.L3860
.L3878:
	mov	r8, #1
.L3860:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r5, #44]
	ldrh	r0, [r3, #10]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	lsls	r0, r0, #2
	mul	r0, r8, r0
	bl	__aeabi_idiv
	mov	r1, r7
	mov	r8, #0
	str	r0, [sp, #48]
	mov	r0, r6
	bl	__aeabi_uidivmod
	mov	ip, r1
	subs	r3, r6, r1
	str	fp, [sp, #28]
	str	r3, [sp, #32]
.L3861:
	ldr	r3, [sp, #48]
	ldr	r7, .L3899
	cmp	r8, r3
	bcs	.L3897
	add	r3, r8, ip
	ubfx	r3, r3, #2, #16
	cbz	r3, .L3862
	adds	r2, r3, #1
	add	r1, r5, r2, lsl #1
	ldrh	r7, [r1, #148]
	ldrb	r1, [r5, #144]	@ zero_extendqisi2
	cbz	r1, .L3863
	ldr	r1, [r5, #2264]
	ldr	r0, .L3899+32
	cmp	r1, r0
	it	eq
	moveq	r7, r2
.L3863:
	add	r7, r7, #1073741824
	subs	r7, r7, #1
	lsls	r7, r7, #2
	str	r7, [sp, #64]
.L3862:
	movw	r2, #61424
	str	r2, [sp, #68]
	add	r2, r5, r3, lsl #1
	ldrh	r7, [r2, #148]
	ldrb	r2, [r5, #144]	@ zero_extendqisi2
	cbz	r2, .L3864
	ldr	r2, [r5, #2264]
	ldr	r1, .L3899+32
	cmp	r2, r1
	it	eq
	moveq	r7, r3
.L3864:
	ldr	r3, [sp, #32]
	add	r8, r8, #4
	str	ip, [sp, #56]
	uxth	r8, r8
	mla	r2, r9, r7, r3
	ldr	r3, .L3899
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	str	r2, [sp, #52]
	str	r3, [sp, #36]
	ldr	r3, .L3899+20
	ldrb	r0, [r3, #1210]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, .L3899
	ldr	r3, [r3, #44]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #52]
	mov	r0, r2
	bl	__aeabi_uidiv
	add	r3, sp, #64
	mov	r1, r0
	ldr	r2, [sp, #28]
	movs	r0, #0
	bl	FlashProgPage
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #36]
	bl	FlashBchSel
	mov	r1, r9
	ldr	r0, [sp, #32]
	bl	__aeabi_uidiv
	adds	r2, r7, #1
	uxth	r2, r2
	mov	r1, r0
	movs	r0, #0
	bl	FlashPageProgMsbFFData
	ldr	r3, [sp, #28]
	ldr	ip, [sp, #56]
	add	r3, r3, #2048
	str	r3, [sp, #28]
	b	.L3861
.L3900:
	.align	2
.L3899:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC173
	.word	.LC174
	.word	.LC175
	.word	.LANCHOR4
	.word	.LC176
	.word	.LC177
	.word	1446522928
.L3897:
	mov	r1, r6
	mov	r2, r10
	movs	r3, #0
	ldr	r0, .L3901
	bl	printk
	ldr	r6, [r4, #4]
	ldr	r0, .L3901+4
	mov	r9, #0
	ldr	r3, [sp, #16]
	muls	r6, r3, r6
	ldr	r3, [r7, #44]
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r1, r6
	str	r3, [sp, #28]
	ldr	r3, [r7, #4]
	ldrh	r2, [sp, #28]
	smulbb	r8, r3, r2
	mov	r2, r10
	bl	printk
	mov	r0, r6
	uxth	r8, r8
	mov	r1, r8
	bl	__aeabi_uidivmod
	subs	r3, r6, r1
	str	r3, [sp, #48]
	ldr	r3, [sp, #28]
	str	r1, [sp, #32]
	mul	ip, r3, r1
	ldr	r3, [sp, #12]
	ubfx	ip, ip, #2, #2
	str	r3, [sp, #36]
.L3866:
	cmp	r9, r10
	bcs	.L3898
	ldr	r3, [sp, #32]
	rsb	r8, ip, #4
	ldrb	r1, [r5, #144]	@ zero_extendqisi2
	add	r3, r9, r3
	uxth	r8, r8
	ubfx	r3, r3, #2, #16
	add	r2, r5, r3, lsl #1
	ldrh	r2, [r2, #148]
	cbz	r1, .L3867
	ldr	r1, [r5, #2264]
	ldr	r0, .L3901+8
	cmp	r1, r0
	it	eq
	moveq	r2, r3
.L3867:
	ldr	r3, [sp, #48]
	add	r9, r9, r8
	ldr	r1, [sp, #28]
	add	r3, ip, r3
	ldrb	ip, [r7, #2312]	@ zero_extendqisi2
	uxth	r9, r9
	mla	r3, r1, r2, r3
	ldr	r2, [r7, #44]
	str	ip, [sp, #52]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	ldr	r2, .L3901+12
	str	r3, [sp, #60]
	str	r1, [sp, #56]
	ldrb	r0, [r2, #1210]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [sp, #60]
	ldr	r1, [sp, #56]
	mov	r0, r3
	bl	__aeabi_uidiv
	mov	r1, r0
	movs	r0, #0
	ldr	r2, [sp, #36]
	mov	r3, r0
	bl	FlashReadPage
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	ip, [sp, #52]
	mov	r0, ip
	bl	FlashBchSel
	mov	ip, #0
	ldr	r3, [sp, #36]
	add	r3, r3, r8, lsl #9
	str	r3, [sp, #36]
	b	.L3866
.L3898:
	mov	r1, r6
	movs	r3, #0
	ldr	r0, .L3901+16
	mov	r2, r10
	bl	printk
	movs	r6, #0
	mov	r3, fp
	ldr	r0, [sp, #12]
.L3872:
	mov	r8, r0
	mov	r7, r3
	ldr	r1, [r8]
	adds	r0, r0, #4
	ldr	r2, [r7]
	adds	r3, r3, #4
	cmp	r1, r2
	beq	.L3869
	movs	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #12]
	bl	memset
	ldr	r3, [r8]
	ldr	r0, .L3901+20
	str	r3, [sp]
	ldr	r3, [r7]
	bic	r7, r6, #255
	ldr	r1, [sp, #20]
	lsls	r7, r7, #2
	str	r3, [sp, #4]
	mov	r3, r6
	ldr	r2, [r4, #4]
	bl	printk
	ldr	r0, .L3901+24
	add	r1, fp, r7
	movs	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	movs	r2, #4
	ldr	r0, .L3901+28
	ldr	r3, [sp, #12]
	adds	r1, r3, r7
	mov	r3, #256
	bl	rknand_print_hex
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r1, [r4, #4]
	movs	r0, #0
	mov	r2, r0
	ldr	r3, [sp, #16]
	muls	r1, r3, r1
	bl	FlashEraseBlock
	ldr	r3, [sp, #40]
	cmp	r3, #1
	bls	.L3870
	ldr	r1, [r4, #4]
	movs	r0, #0
	ldr	r3, [sp, #16]
	mov	r2, r0
	mla	r1, r1, r3, r3
	bl	FlashEraseBlock
.L3870:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L3901+32
	ldr	r1, [r4, #4]
	bl	printk
	ldr	r3, [sp, #44]
	cmp	r6, r3
	bcc	.L3858
	b	.L3871
.L3869:
	ldr	r2, [sp, #44]
	adds	r6, r6, #1
	cmp	r6, r2
	bne	.L3872
.L3871:
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp, #24]
.L3858:
	ldr	r3, [sp, #20]
	adds	r4, r4, #4
	adds	r3, r3, #1
	str	r3, [sp, #20]
	cmp	r3, #5
	bne	.L3873
	ldr	r0, [sp, #12]
	bl	ftl_free
	ldr	r3, [sp, #24]
	clz	r0, r3
	lsrs	r0, r0, #5
	negs	r0, r0
	b	.L3851
.L3877:
	mov	r0, #-1
.L3851:
	add	sp, sp, #132
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3902:
	.align	2
.L3901:
	.word	.LC178
	.word	.LC179
	.word	1446522928
	.word	.LANCHOR4
	.word	.LC180
	.word	.LC181
	.word	.LC182
	.word	.LC183
	.word	.LC184
	.fnend
	.size	write_idblock, .-write_idblock
	.align	1
	.global	write_loader_lba
	.thumb
	.thumb_func
	.type	write_loader_lba, %function
write_loader_lba:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #64
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r5, r0
	.pad #52
	sub	sp, sp, #52
	mov	r6, r1
	mov	r8, r2
	ldr	r4, .L3925
	bne	.L3904
	ldr	r2, [r2]
	ldr	r3, .L3925+4
	cmp	r2, r3
	bne	.L3904
	mov	r0, #256000
	movs	r3, #1
	strb	r3, [r4, #1012]
	bl	ftl_malloc
	movs	r1, #0
	mov	r2, #256000
	str	r0, [r4, #1016]
	bl	ftl_memset
	str	r5, [r4, #1020]
.L3904:
	str	r6, [sp]
	mov	r3, r5
	ldr	r0, .L3925+8
	ldr	r1, [r4, #1016]
	ldr	r2, [r8]
	bl	printk
	ldrb	r3, [r4, #1012]	@ zero_extendqisi2
	ldr	r9, .L3925
	cmp	r3, #0
	beq	.L3903
	sub	r0, r5, #64
	ldr	r7, [r9, #1016]
	cmp	r0, #500
	bcs	.L3906
	rsb	r2, r5, #564
	add	r0, r7, r0, lsl #9
	cmp	r2, r6
	mov	r1, r8
	it	cs
	movcs	r2, r6
	lsls	r2, r2, #9
	bl	ftl_memcpy
	b	.L3907
.L3906:
	cmp	r5, #564
	bcs	.L3915
.L3907:
	ldr	r3, [r4, #1020]
	cmp	r3, r5
	beq	.L3913
	ldr	r2, .L3925
	movs	r3, #0
	mov	r8, r3
	strb	r3, [r2, #1012]
	cbz	r7, .L3914
	mov	r0, r7
	bl	ftl_free
.L3914:
	str	r8, [r4, #1016]
.L3913:
	add	r5, r5, r6
	str	r5, [r4, #1020]
	b	.L3903
.L3915:
	ldr	r3, .L3925+12
	ldr	r0, [r9, #1020]
	ldr	r3, [r3, #44]
	subs	r0, r0, #64
	cmp	r0, #500
	it	cs
	movcs	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L3916
	movs	r3, #2
	str	r3, [sp, #8]
	movs	r3, #3
	str	r3, [sp, #12]
	movs	r3, #4
	str	r3, [sp, #16]
	movs	r3, #5
	str	r3, [sp, #20]
	movs	r3, #6
	str	r3, [sp, #24]
	b	.L3909
.L3916:
	movs	r3, #0
.L3908:
	add	r1, sp, #8
	lsls	r2, r3, #1
	cmp	r0, #256
	it	ls
	movls	r2, r3
	str	r2, [r1, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #5
	bne	.L3908
.L3909:
	movw	r3, #63872
.L3912:
	ldr	r2, [r7, r3, lsl #2]
	cbz	r2, .L3910
	adds	r3, r3, #128
	lsls	r0, r3, #2
	b	.L3911
.L3910:
	subs	r3, r3, #1
	cmp	r3, #4096
	bne	.L3912
	lsls	r0, r0, #9
.L3911:
	mov	r1, r7
	add	r2, sp, #8
	bl	write_idblock
	ldr	r0, [r4, #1016]
	movs	r7, #0
	strb	r7, [r4, #1012]
	bl	ftl_free
	str	r7, [r4, #1016]
	b	.L3913
.L3903:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3926:
	.align	2
.L3925:
	.word	.LANCHOR5
	.word	-52655045
	.word	.LC185
	.word	.LANCHOR0
	.fnend
	.size	write_loader_lba, .-write_loader_lba
	.align	1
	.global	FtlWrite
	.thumb
	.thumb_func
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r3
	sub	r3, r1, #64
	mov	r7, r0
	cmp	r3, #1984
	mov	r4, r1
	mov	r6, r2
	bcs	.L3928
	cbnz	r0, .L3928
	mov	r0, r1
	mov	r1, r2
	mov	r2, r5
	bl	write_loader_lba
.L3928:
	mov	r0, r7
	mov	r1, r4
	mov	r2, r6
	mov	r3, r5
	pop	{r4, r5, r6, r7, r8, lr}
	b	ftl_write
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	1
	.global	rknand_sys_storage_ioctl
	.thumb
	.thumb_func
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 520
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3988
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	cmp	r1, r3
	.pad #524
	sub	sp, sp, #524
	mov	r4, r1
	mov	r5, r2
	beq	.L3931
	bhi	.L3932
	subw	r3, r3, #2086
	cmp	r1, r3
	beq	.L3933
	bhi	.L3934
	subs	r3, r3, #238
	cmp	r1, r3
	beq	.L3935
	adds	r3, r3, #237
	cmp	r1, r3
	beq	.L3936
	b	.L3966
.L3934:
	ldr	r3, .L3988+4
	cmp	r1, r3
	beq	.L3937
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3938
	subs	r3, r3, #124
	cmp	r1, r3
	bne	.L3966
	b	.L3986
.L3932:
	ldr	r3, .L3988+8
	cmp	r1, r3
	mov	r6, r3
	beq	.L3940
	bhi	.L3941
	subw	r3, r3, #2526
	cmp	r1, r3
	beq	.L3931
	adds	r3, r3, #10
	cmp	r1, r3
	beq	.L3931
	b	.L3966
.L3941:
	ldr	r3, .L3988+12
	cmp	r1, r3
	beq	.L3940
	bcc	.L3942
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3942
	b	.L3966
.L3936:
	ldr	r0, .L3988+16
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cbz	r0, .L3943
.L3949:
	ldr	r0, .L3988+20
	bl	printk
	b	.L3983
.L3943:
	ldr	r2, [sp]
	ldr	r3, .L3988+24
	cmp	r2, r3
	beq	.L3944
.L3946:
	mov	r4, #-1
	b	.L3945
.L3944:
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3946
	ldr	r4, .L3988+28
	mov	r2, #512
	mov	r0, sp
	ldr	r1, [r4, #1024]
	bl	memcpy
	ldr	r2, [r4, #1028]
	ldr	r3, .L3988+32
	cmp	r2, r3
	beq	.L3947
	movs	r1, #0
	add	r0, sp, #64
	movs	r2, #128
	str	r1, [sp, #8]
	str	r1, [sp, #12]
	bl	memset
.L3947:
	movs	r1, #0
	add	r0, sp, #256
	mov	r2, #256
	str	r1, [sp, #16]
	bl	memset
.L3978:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #520
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3983
	b	.L3982
.L3933:
	ldr	r0, .L3988+36
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3949
	ldr	r2, [sp]
	ldr	r3, .L3988+24
	cmp	r2, r3
	bne	.L3946
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3946
	ldr	r2, .L3988+28
	ldr	r3, .L3988+32
	ldr	r1, [r2, #1028]
	cmp	r1, r3
	bne	.L3967
	ldr	r3, [sp, #12]
	subs	r1, r3, #1
	cmp	r1, #127
	bhi	.L3968
	ldr	r4, [r2, #1024]
	add	r1, sp, #64
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #12]
	bl	memcpy
	movs	r0, #1
	mov	r1, r4
	b	.L3981
.L3938:
	ldr	r0, .L3988+40
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3949
	ldr	r2, [sp]
	ldr	r3, .L3988+44
	cmp	r2, r3
	bne	.L3946
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3946
	ldr	r5, .L3988+28
	ldr	r3, [r5, #1032]
	cbnz	r3, .L3950
.L3953:
	movs	r0, #0
	b	.L3930
.L3950:
	ldr	r3, [r5, #1036]
	ldr	r2, .L3988+48
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3951
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L3988+28
	ldr	r3, [r3, #1036]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3951:
	ldr	r1, [r5, #1036]
	movs	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #1024]
	ldr	r2, .L3988+24
	ldr	r1, [r3]
	cmp	r1, r2
	it	ne
	strne	r2, [r3]
	ldr	r6, [r5, #1024]
	itt	ne
	movne	r2, #504
	ldrne	r3, .L3988+28
	add	r0, r6, #64
	itt	ne
	ldrne	r3, [r3, #1024]
	strne	r4, [r3, #8]
	mov	r4, #0
	it	ne
	strne	r2, [r3, #4]
	movs	r2, #128
	mov	r1, r4
	str	r4, [r6, #12]
	bl	memset
	movs	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	str	r4, [r5, #1032]
	str	r4, [r5, #1028]
	b	.L3945
.L3937:
	ldr	r0, .L3988+52
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3949
	ldr	r2, [sp]
	ldr	r3, .L3988+56
	cmp	r2, r3
	bne	.L3946
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3946
	ldr	r5, .L3988+28
	ldr	r3, [r5, #1032]
	cmp	r3, #1
	beq	.L3953
	ldr	r2, [r5, #1036]
	ldr	r3, .L3988+48
	ldr	r1, [r2]
	cmp	r1, r3
	beq	.L3954
	str	r3, [r2]
	mov	r2, #504
	ldr	r3, [r5, #1036]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3954:
	ldr	r1, [r5, #1036]
	movs	r3, #1
	movs	r0, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #1024]
	ldr	r2, .L3988+24
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3955
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L3988+28
	ldr	r3, [r3, #1024]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
.L3955:
	ldr	r6, [r5, #1024]
	movs	r4, #0
	movs	r2, #128
	mov	r1, r4
	add	r0, r6, #64
	str	r4, [r6, #12]
	bl	memset
	movs	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	movs	r3, #1
	str	r3, [r5, #1032]
	b	.L3945
.L3986:
	ldr	r0, .L3988+60
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3949
	ldr	r2, [sp]
	ldr	r3, .L3988+64
	cmp	r2, r3
	bne	.L3946
	ldr	r2, [sp, #4]
	cmp	r2, #512
	bhi	.L3946
	add	r0, sp, #8
	ldr	r1, .L3988+68
	b	.L3984
.L3931:
	ldr	r0, .L3988+72
	cmp	r4, r0
	mov	r7, r0
	bne	.L3957
	ldr	r0, .L3988+76
	b	.L3980
.L3989:
	.align	2
.L3988:
	.word	1074031656
	.word	1074029694
	.word	1074034192
	.word	1074034194
	.word	.LC186
	.word	.LC187
	.word	1263358532
	.word	.LANCHOR5
	.word	-1067903959
	.word	.LC188
	.word	.LC189
	.word	1112753220
	.word	1146313043
	.word	.LC190
	.word	1112755781
	.word	.LC191
	.word	1094995539
	.word	.LANCHOR5+1040
	.word	1074031666
	.word	.LC192
.L3957:
	ldr	r3, .L3990
	cmp	r4, r3
	ite	eq
	ldreq	r0, .L3990+4
	ldrne	r0, .L3990+8
.L3980:
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3949
	ldr	r2, [sp]
	ldr	r3, .L3990+12
	cmp	r2, r3
	bne	.L3983
	ldr	r3, .L3990
	ldr	r6, .L3990+16
	cmp	r4, r3
	bne	.L3960
	ldr	r3, [r6, #1024]
	mov	r0, r5
	mov	r1, sp
	movs	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #4]
	strb	r3, [sp, #8]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3930
	b	.L3983
.L3960:
	ldr	r3, [r6, #1552]
	cmp	r3, #10
	bhi	.L3983
	ldr	r1, [r6, #1024]
	ldr	r3, [sp, #4]
	ldr	r2, [r1, #24]
	cmp	r2, r3
	beq	.L3961
	cbz	r2, .L3961
	mov	r1, r3
	ldr	r0, .L3990+20
	bl	printk
	ldr	r3, [r6, #1552]
	adds	r3, r3, #1
	str	r3, [r6, #1552]
.L3983:
	mvn	r0, #13
	b	.L3930
.L3961:
	movs	r2, #0
	cmp	r4, r7
	str	r2, [r6, #1552]
	mov	r0, #1
	itet	eq
	moveq	r3, r2
	movne	r2, #1
	moveq	r2, r3
	str	r3, [r1, #24]
	str	r2, [r1, #20]
	bl	StorageSysDataStore
	adds	r0, r0, #1
	bne	.L3982
	b	.L3987
.L3940:
	ldr	r0, .L3990+24
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3949
	ldr	r2, [sp]
	ldr	r3, .L3990+28
	cmp	r2, r3
	bne	.L3946
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L3946
	ldr	r3, .L3990+16
	cmp	r4, r6
	add	r0, sp, #8
	ite	eq
	ldreq	r1, [r3, #1556]
	ldrne	r1, [r3, #1560]
	adds	r1, r1, #8
.L3984:
	bl	memcpy
	b	.L3978
.L3942:
	ldr	r0, .L3990+32
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3949
	ldr	r2, [sp]
	ldr	r3, .L3990+28
	cmp	r2, r3
	bne	.L3946
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L3946
	ldr	r3, .L3990+36
	adds	r2, r2, #8
	cmp	r4, r3
	ldr	r4, .L3990+16
	bne	.L3965
	mov	r1, sp
	ldr	r0, [r4, #1556]
	bl	memcpy
	movs	r0, #2
	ldr	r1, [r4, #1556]
	b	.L3981
.L3965:
	mov	r1, sp
	ldr	r0, [r4, #1560]
	bl	memcpy
	ldr	r1, [r4, #1560]
	movs	r0, #3
.L3981:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L3945
.L3935:
	bl	rknand_dev_flush
.L3982:
	movs	r4, #0
	b	.L3945
.L3987:
	mvn	r4, #1
.L3945:
	ldr	r0, .L3990+40
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L3930
.L3966:
	mvn	r0, #21
	b	.L3930
.L3967:
	mvn	r0, #1
	b	.L3930
.L3968:
	mvn	r0, #2
.L3930:
	add	sp, sp, #524
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L3991:
	.align	2
.L3990:
	.word	1074031676
	.word	.LC193
	.word	.LC194
	.word	1280262987
	.word	.LANCHOR5
	.word	.LC195
	.word	.LC196
	.word	1145980246
	.word	.LC197
	.word	1074034193
	.word	.LC198
	.fnend
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	1
	.global	rk_ftl_storage_sys_init
	.thumb
	.thumb_func
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r2, #512
	ldr	r4, .L4001
	mov	r3, #-1
	movs	r5, #0
	add	r0, r4, #1040
	ldr	r1, [r4, #996]
	str	r3, [r4, #1020]
	add	r3, r1, #512
	str	r3, [r4, #1024]
	str	r1, [r4, #1036]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #1556]
	strb	r5, [r4, #1012]
	str	r5, [r4, #1016]
	str	r5, [r4, #1564]
	str	r1, [r4, #1560]
	bl	ftl_memcpy
	ldr	r6, [r4, #1036]
	str	r5, [r4, #1028]
	str	r5, [r4, #1552]
	ldr	r3, [r6, #16]
	ldr	r7, [r6, #508]
	str	r3, [r4, #1032]
	cbz	r7, .L3993
	mov	r0, r6
	mov	r1, #508
	bl	js_hash
	cmp	r7, r0
	beq	.L3993
	str	r5, [r6, #16]
	ldr	r0, .L4001+4
	str	r5, [r4, #1032]
	bl	printk
.L3993:
	ldr	r3, [r4, #1032]
	cbz	r3, .L3994
	ldr	r2, .L4001+8
	ldr	r3, .L4001
	str	r2, [r3, #1028]
.L3994:
	ldr	r1, [r4, #1556]
	movs	r0, #2
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1560]
	movs	r0, #3
	bl	StorageSysDataLoad
	pop	{r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L4002:
	.align	2
.L4001:
	.word	.LANCHOR5
	.word	.LC199
	.word	-1067903959
	.fnend
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	1
	.global	StorageSysDataDeInit
	.thumb
	.thumb_func
	.type	StorageSysDataDeInit, %function
StorageSysDataDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r0, #0
	bx	lr
	.fnend
	.size	StorageSysDataDeInit, .-StorageSysDataDeInit
	.align	1
	.global	rk_ftl_vendor_storage_init
	.thumb
	.thumb_func
	.type	rk_ftl_vendor_storage_init, %function
rk_ftl_vendor_storage_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r0, #65536
	bl	ftl_malloc
	ldr	r6, .L4013
	str	r0, [r6, #1568]
	cmp	r0, #0
	beq	.L4010
	mov	r8, #0
	ldr	r9, .L4013+8
	mov	r10, r6
	mov	r4, r8
	mov	r7, r8
.L4008:
	lsls	r0, r7, #7
	movs	r1, #128
	ldr	r2, [r6, #1568]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4006
	ldr	r3, [r10, #1568]
	ldr	r0, .L4013+4
	add	r2, r3, #65280
	adds	r2, r2, #252
	ldr	r1, [r3]
	ldr	r3, [r3, #4]
	ldr	r2, [r2]
	bl	printk
	ldr	r5, [r10, #1568]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4007
	add	r3, r5, #65280
	ldr	r2, [r5, #4]
	adds	r3, r3, #252
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L4007
	cmp	r4, r3
	itt	cc
	movcc	r8, r7
	movcc	r4, r3
.L4007:
	cbnz	r7, .L4012
	movs	r7, #1
	b	.L4008
.L4012:
	cbz	r4, .L4009
	lsl	r0, r8, #7
	movs	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cbnz	r0, .L4006
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4009:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L4013+8
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	movw	r3, #65532
	str	r7, [r5, r3]
	movw	r3, #64504
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4006:
	ldr	r0, [r6, #1568]
	bl	kfree
	movs	r3, #0
	mov	r0, #-1
	str	r3, [r6, #1568]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4010:
	mvn	r0, #11
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4014:
	.align	2
.L4013:
	.word	.LANCHOR5
	.word	.LC200
	.word	1380668996
	.fnend
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	1
	.global	rk_ftl_vendor_read
	.thumb
	.thumb_func
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldr	r3, .L4021
	ldr	r5, [r3, #1568]
	cbz	r5, .L4020
	ldrh	r7, [r5, #10]
	movs	r3, #0
.L4017:
	cmp	r3, r7
	bcs	.L4020
	add	r6, r5, r3, lsl #3
	ldrh	r4, [r6, #16]
	cmp	r4, r0
	bne	.L4018
	ldrh	r3, [r6, #20]
	mov	r0, r1
	cmp	r2, r3
	it	cs
	movcs	r2, r3
	ldrh	r3, [r6, #18]
	mov	r4, r2
	add	r3, r3, #1024
	adds	r1, r5, r3
	bl	memcpy
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L4018:
	adds	r3, r3, #1
	b	.L4017
.L4020:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L4022:
	.align	2
.L4021:
	.word	.LANCHOR5
	.fnend
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	1
	.global	rk_ftl_vendor_write
	.thumb
	.thumb_func
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r2
	ldr	r2, .L4045
	.pad #28
	sub	sp, sp, #28
	mov	ip, r0
	mov	r3, r1
	ldr	r4, [r2, #1568]
	cmp	r4, #0
	beq	.L4038
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	ldrh	r2, [r4, #10]
	bic	r7, r7, #63
	movs	r6, #0
	str	r1, [sp, #4]
.L4025:
	cmp	r6, r2
	bcs	.L4043
	add	r5, r4, r6, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L4026
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L4027
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4038
	ldrh	r8, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #12]
.L4028:
	ldr	r2, [sp, #12]
	adds	r5, r5, #8
	cmp	r6, r2
	bcs	.L4044
	ldrh	r2, [r5, #16]
	add	r0, fp, r8
	ldrh	r1, [r5, #18]
	adds	r6, r6, #1
	strh	r8, [r5, #10]	@ movhi
	strh	r2, [r5, #8]	@ movhi
	add	r1, r1, fp
	ldrh	r2, [r5, #20]
	str	r3, [sp, #20]
	add	r10, r2, #63
	str	ip, [sp, #16]
	bic	r10, r10, #63
	strh	r2, [r5, #12]	@ movhi
	add	r8, r8, r10
	mov	r2, r10
	bl	memcpy
	ldr	r3, [sp, #20]
	ldr	ip, [sp, #16]
	b	.L4028
.L4044:
	add	r6, r4, r6, lsl #3
	uxth	r8, r8
	add	r0, fp, r8
	mov	r1, r3
	strh	r8, [r6, #18]	@ movhi
	mov	r2, r9
	strh	ip, [r6, #16]	@ movhi
	uxth	r7, r7
	strh	r9, [r6, #20]	@ movhi
	add	r8, r8, r7
	bl	memcpy
	ldrh	r5, [r4, #14]
	strh	r8, [r4, #12]	@ movhi
	ldr	r3, [sp, #8]
	add	r3, r3, r5
	subs	r7, r3, r7
	strh	r7, [r4, #14]	@ movhi
	b	.L4042
.L4027:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, r0, fp
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L4042
.L4026:
	adds	r6, r6, #1
	b	.L4025
.L4043:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4038
	add	r2, r4, r2, lsl #3
	uxth	r7, r7
	subs	r1, r1, r7
	strh	ip, [r2, #16]	@ movhi
	ldrh	r0, [r4, #12]
	strh	r9, [r2, #20]	@ movhi
	strh	r0, [r2, #18]	@ movhi
	add	r0, r0, r7
	strh	r1, [r4, #14]	@ movhi
	mov	r1, r3
	strh	r0, [r4, #12]	@ movhi
	ldrh	r0, [r2, #18]
	mov	r2, r9
	add	r0, r0, #1024
	add	r0, r0, r4
	bl	memcpy
	ldrh	r3, [r4, #10]
	adds	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
.L4042:
	ldr	r3, [r4, #4]
	movw	r2, #65532
	movs	r1, #128
	adds	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r4, r2]
	mov	r2, r4
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	it	hi
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, [sp, #4]
	lsls	r0, r3, #7
	bl	FlashBootVendorWrite
	movs	r0, #0
	b	.L4024
.L4038:
	mov	r0, #-1
.L4024:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4046:
	.align	2
.L4045:
	.word	.LANCHOR5
	.fnend
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	1
	.global	rk_ftl_vendor_storage_ioctl
	.thumb
	.thumb_func
	.type	rk_ftl_vendor_storage_ioctl, %function
rk_ftl_vendor_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #4096
	mov	r6, r1
	mov	r5, r2
	bl	ftl_malloc
	mov	r4, r0
	cmp	r0, #0
	beq	.L4056
	ldr	r3, .L4063
	cmp	r6, r3
	beq	.L4050
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L4051
	b	.L4062
.L4050:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4062
	ldr	r2, [r4]
	ldr	r3, .L4063+4
	cmp	r2, r3
	beq	.L4053
.L4054:
	mov	r5, #-1
	b	.L4049
.L4053:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	adds	r3, r0, #1
	beq	.L4054
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	adds	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	ite	eq
	moveq	r5, #0
	mvnne	r5, #13
	b	.L4049
.L4051:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L4062
	ldr	r2, [r4]
	ldr	r3, .L4063+4
	cmp	r2, r3
	bne	.L4054
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4054
	mov	r0, r4
	mov	r1, r5
	adds	r2, r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L4062
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4049
.L4062:
	mvn	r5, #13
.L4049:
	mov	r0, r4
	bl	kfree
	b	.L4048
.L4056:
	mov	r5, #-1
.L4048:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L4064:
	.align	2
.L4063:
	.word	1074034177
	.word	1448232273
	.fnend
	.size	rk_ftl_vendor_storage_ioctl, .-rk_ftl_vendor_storage_ioctl
	.global	SecureBootUnlockTryCount
	.global	SecureBootCheckOK
	.global	SecureBootEn
	.global	gpVendor1Info
	.global	gpVendor0Info
	.global	g_idb_buffer
	.global	gSnSectorData
	.global	gpDrmKeyInfo
	.global	gpBootConfig
	.global	gLoaderBootInfo
	.global	RK29_NANDC1_REG_BASE
	.global	RK29_NANDC_REG_BASE
	.global	gc_ink_free_return_value
	.global	check_valid_page_count_table
	.global	FtlUpdateVaildLpnCount
	.global	g_ect_tbl_power_up_flush
	.global	last_cache_match_count
	.global	power_up_flag
	.global	g_LowFormat
	.global	gFtlInitStatus
	.global	DeviceCapacity
	.global	ToshibaRefValue
	.global	Toshiba15RefValue
	.global	ToshibaA19RefValue
	.global	SamsungRefValue
	.global	refValueDefault
	.global	FbbtBlk
	.global	random_seed
	.global	gSlcNandParaInfo
	.global	gNandParaInfo
	.global	g_page_map_check_enable
	.global	g_power_lost_ecc_error_blk
	.global	g_power_lost_recovery_flag
	.global	c_mlc_erase_count_value
	.global	g_recovery_ppa_tbl
	.global	g_recovery_page_min_ver
	.global	g_recovery_page_num
	.global	g_cur_erase_blk
	.global	g_gc_skip_write_count
	.global	g_gc_head_data_block_count
	.global	g_gc_head_data_block
	.global	g_ftl_nand_free_count
	.global	g_in_swl_replace
	.global	g_in_gc_progress
	.global	g_all_blk_used_slc_mode
	.global	g_max_erase_count
	.global	g_totle_sys_slc_erase_count
	.global	g_totle_slc_erase_count
	.global	g_min_erase_count
	.global	g_totle_avg_erase_count
	.global	g_totle_mlc_erase_count
	.global	g_totle_l2p_write_count
	.global	g_totle_cache_write_count
	.global	g_tmp_data_superblock_id
	.global	g_totle_read_page_count
	.global	g_totle_discard_page_count
	.global	g_totle_read_sector
	.global	g_totle_write_sector
	.global	g_totle_write_page_count
	.global	g_totle_gc_page_count
	.global	g_gc_blk_index
	.global	g_gc_merge_free_blk_threshold
	.global	g_gc_free_blk_threshold
	.global	g_gc_refresh_block_temp_tbl
	.global	g_free_slc_blk_num
	.global	g_gc_refresh_block_temp_num
	.global	g_gc_bad_block_temp_tbl
	.global	g_gc_bad_block_gc_index
	.global	g_gc_bad_block_temp_num
	.global	g_gc_next_blk_3
	.global	g_gc_next_blk_2
	.global	g_gc_next_blk_1
	.global	g_gc_next_blk
	.global	g_gc_cur_blk_max_valid_pages
	.global	g_gc_cur_blk_valid_pages
	.global	g_gc_page_offset
	.global	g_gc_blk_num
	.global	p_gc_blk_tbl
	.global	p_gc_page_info
	.global	g_sys_ext_data
	.global	g_sys_save_data
	.global	gp_last_act_superblock
	.global	g_gc_superblock
	.global	g_gc_temp_superblock
	.global	g_buffer_superblock
	.global	g_active_superblock
	.global	g_num_data_superblocks
	.global	g_num_free_superblocks
	.global	p_data_block_list_tail
	.global	p_data_block_list_head
	.global	p_free_data_block_list_head
	.global	p_data_block_list_table
	.global	g_l2p_last_update_region_id
	.global	p_l2p_map_buf
	.global	p_l2p_ram_map
	.global	g_totle_vendor_block
	.global	p_vendor_region_ppn_table
	.global	p_vendor_block_ver_table
	.global	p_vendor_block_valid_page_count
	.global	p_vendor_block_table
	.global	g_totle_map_block
	.global	p_map_region_ppn_check_table
	.global	p_map_region_ppn_table
	.global	p_map_block_ver_table
	.global	p_map_block_valid_page_count
	.global	p_map_block_table
	.global	p_blk_mode_table
	.global	p_valid_page_count_check_table
	.global	p_valid_page_count_table
	.global	g_totle_swl_count
	.global	p_swl_mul_table
	.global	p_erase_count_table
	.global	g_ect_tbl_info_size
	.global	gp_ect_tbl_info
	.global	g_gc_num_req
	.global	c_gc_page_buf_num
	.global	gp_gc_page_buf_info
	.global	p_gc_data_buf
	.global	p_gc_spare_buf
	.global	p_io_spare_buf
	.global	p_io_data_buf_1
	.global	p_io_data_buf_0
	.global	p_sys_spare_buf
	.global	p_vendor_data_buf
	.global	p_sys_data_buf_1
	.global	p_sys_data_buf
	.global	g_wr_page_num
	.global	req_wr_io
	.global	c_wr_page_buf_num
	.global	p_wr_io_data_buf
	.global	p_wr_io_spare_buf
	.global	p_plane_order_table
	.global	g_req_cache
	.global	req_gc_dst
	.global	req_gc
	.global	req_erase
	.global	req_prgm
	.global	req_read
	.global	req_sys
	.global	gVendorBlkInfo
	.global	gL2pMapInfo
	.global	gSysFreeQueue
	.global	gSysInfo
	.global	gBbtInfo
	.global	g_flash_read_only_en
	.global	g_inkDie_check_enable
	.global	g_SlcPartLbaEndSector
	.global	g_MaxLbn
	.global	g_VaildLpn
	.global	g_MaxLpn
	.global	g_MaxLbaSector
	.global	g_GlobalDataVersion
	.global	g_GlobalSysVersion
	.global	ftl_gc_temp_power_lost_recovery_flag
	.global	c_ftl_nand_max_data_blks
	.global	c_ftl_nand_data_op_blks_per_plane
	.global	c_ftl_nand_data_blks_per_plane
	.global	c_ftl_nand_max_sys_blks
	.global	c_ftl_nand_init_sys_blks_per_plane
	.global	c_ftl_nand_sys_blks_per_plane
	.global	c_ftl_vendor_part_size
	.global	c_ftl_nand_max_vendor_blks
	.global	c_ftl_nand_max_map_blks
	.global	c_ftl_nand_map_blks_per_plane
	.global	c_ftl_nand_vendor_region_num
	.global	c_ftl_nand_l2pmap_ram_region_num
	.global	c_ftl_nand_map_region_num
	.global	c_ftl_nand_totle_phy_blks
	.global	c_ftl_nand_reserved_blks
	.global	c_ftl_nand_byte_pre_oob
	.global	c_ftl_nand_byte_pre_page
	.global	c_ftl_nand_sec_pre_page_shift
	.global	c_ftl_nand_sec_pre_page
	.global	c_ftl_nand_page_pre_super_blk
	.global	c_ftl_nand_page_pre_slc_blk
	.global	c_ftl_nand_page_pre_blk
	.global	c_ftl_nand_bbm_buf_size
	.global	c_ftl_nand_ext_blk_pre_plane
	.global	c_ftl_nand_blk_pre_plane
	.global	c_ftl_nand_planes_num
	.global	c_ftl_nand_blks_per_die
	.global	c_ftl_nand_planes_per_die
	.global	c_ftl_nand_die_num
	.global	c_ftl_nand_type
	.global	gMasterTempBuf
	.global	gMasterInfo
	.global	gNandcDumpWriteEn
	.global	gToggleModeClkDiv
	.global	gBootDdrMode
	.global	gNandcEccBits
	.global	gpNandc1
	.global	gpNandc
	.global	g_nandc_version_data
	.global	gNandcVer
	.global	gNandChipMap
	.global	gNandIDataBuf
	.global	idb_flash_slc_mode
	.global	FlashDdrTunningReadCount
	.global	FlashWaitBusyScheduleEn
	.global	gNandPhyInfo
	.global	gFlashProgCheckSpareBuffer
	.global	gFlashProgCheckBuffer
	.global	gFlashSpareBuffer
	.global	gFlashPageBuffer1
	.global	gFlashPageBuffer0
	.global	gpFlashSaveInfo
	.global	gReadRetryInfo
	.global	gpNandParaInfo
	.global	gNandOptPara
	.global	g_nand_ecc_en
	.global	g_slc2KBNand
	.global	g_maxRetryCount
	.global	g_maxRegNum
	.global	g_retryMode
	.global	gNandIDBResBlkNumSaveInFlash
	.global	gNandIDBResBlkNum
	.global	gNandFlashResEndPageAddr
	.global	gNandFlashInfoBlockAddr
	.global	gNandFlashIdbBlockAddr
	.global	gNandFlashInfoBlockEcc
	.global	gNandFlashIDBEccBits
	.global	gNandFlashEccBits
	.global	gNandRandomizer
	.global	gBlockPageAlignSize
	.global	gTotleBlock
	.global	gNandMaxChip
	.global	gNandMaxDie
	.global	gFlashInterfaceMode
	.global	gFlashCurMode
	.global	gFlashSlcMode
	.global	gFlashOnfiModeEn
	.global	gFlashToggleModeEn
	.global	gFlashSdrModeEn
	.global	gMultiPageProgEn
	.global	gMultiPageReadEn
	.global	gpReadRetrial
	.global	mlcPageToSlcPageTbl
	.global	slcPageToMlcPageTbl
	.global	DieAddrs
	.global	gDieOp
	.global	DieCsIndex
	.global	IDByte
	.global	read_retry_cur_offset
	.section	.rodata
	.align	2
.LANCHOR3 = . + 0
	.type	__func__.20390, %object
	.size	__func__.20390, 11
__func__.20390:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.space	1
	.type	samsung_14nm_slc_rr, %object
	.size	samsung_14nm_slc_rr, 26
samsung_14nm_slc_rr:
	.byte	0
	.byte	10
	.byte	-10
	.byte	20
	.byte	-20
	.byte	30
	.byte	-30
	.byte	40
	.byte	-40
	.byte	50
	.byte	-50
	.byte	60
	.byte	-60
	.byte	-70
	.byte	-80
	.byte	-90
	.byte	-100
	.byte	-110
	.byte	-120
	.byte	-9
	.byte	70
	.byte	80
	.byte	90
	.byte	-125
	.byte	-115
	.byte	100
	.space	2
	.type	samsung_14nm_mlc_rr, %object
	.size	samsung_14nm_mlc_rr, 104
samsung_14nm_mlc_rr:
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	-4
	.byte	3
	.byte	-4
	.byte	-6
	.byte	6
	.byte	0
	.byte	6
	.byte	-10
	.byte	-10
	.byte	4
	.byte	-10
	.byte	16
	.byte	12
	.byte	-4
	.byte	12
	.byte	8
	.byte	-16
	.byte	10
	.byte	-16
	.byte	24
	.byte	18
	.byte	-14
	.byte	18
	.byte	-4
	.byte	-22
	.byte	-16
	.byte	-22
	.byte	-8
	.byte	24
	.byte	-9
	.byte	24
	.byte	8
	.byte	-28
	.byte	-4
	.byte	-28
	.byte	16
	.byte	30
	.byte	10
	.byte	30
	.byte	10
	.byte	-34
	.byte	6
	.byte	-34
	.byte	0
	.byte	36
	.byte	-8
	.byte	36
	.byte	-8
	.byte	-40
	.byte	-2
	.byte	-40
	.byte	-20
	.byte	-46
	.byte	-4
	.byte	-46
	.byte	-30
	.byte	3
	.byte	0
	.byte	3
	.byte	-3
	.byte	-2
	.byte	-4
	.byte	-2
	.byte	-6
	.byte	-4
	.byte	-4
	.byte	-4
	.byte	-10
	.byte	-6
	.byte	-8
	.byte	-6
	.byte	-14
	.byte	-9
	.byte	-8
	.byte	-9
	.byte	-18
	.byte	-52
	.byte	22
	.byte	-52
	.byte	10
	.byte	42
	.byte	4
	.byte	42
	.byte	4
	.byte	48
	.byte	-9
	.byte	48
	.byte	4
	.byte	-58
	.byte	12
	.byte	-58
	.byte	0
	.byte	-64
	.byte	-24
	.byte	-64
	.byte	-6
	.byte	9
	.byte	18
	.byte	9
	.byte	8
	.type	__func__.21169, %object
	.size	__func__.21169, 17
__func__.21169:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21188, %object
	.size	__func__.21188, 16
__func__.21188:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21456, %object
	.size	__func__.21456, 17
__func__.21456:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.21137, %object
	.size	__func__.21137, 12
__func__.21137:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.21436, %object
	.size	__func__.21436, 21
__func__.21436:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20463, %object
	.size	__func__.20463, 8
__func__.20463:
	.ascii	"FtlInit\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC1:
	.ascii	"FlashEraseBlocks pageAddr error %x\012\000"
.LC2:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC3:
	.ascii	"FtlFreeSysBlkQueueOut free count = %d\012\000"
.LC4:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC5:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC6:
	.ascii	"FLASH INFO:\012\000"
.LC7:
	.ascii	"FLASH ID: %x\012\000"
.LC8:
	.ascii	"Device Capacity: %d MB\012\000"
.LC9:
	.ascii	"FMWAIT: %x %x %x %x\012\000"
.LC10:
	.ascii	"FTL INFO:\012\000"
.LC11:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC12:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC13:
	.ascii	"read_page_count = 0x%x\012\000"
.LC14:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC15:
	.ascii	"write_page_count = 0x%x\012\000"
.LC16:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC17:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC18:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC19:
	.ascii	"totle_write = %d MB\012\000"
.LC20:
	.ascii	"totle_read = %d MB\012\000"
.LC21:
	.ascii	"GSV = 0x%x\012\000"
.LC22:
	.ascii	"GDV = 0x%x\012\000"
.LC23:
	.ascii	"bad blk num = %d %d\012\000"
.LC24:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC25:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC26:
	.ascii	"slc_EC = 0x%x\012\000"
.LC27:
	.ascii	"avg_EC = 0x%x\012\000"
.LC28:
	.ascii	"sys_EC = 0x%x\012\000"
.LC29:
	.ascii	"max_EC = 0x%x\012\000"
.LC30:
	.ascii	"min_EC = 0x%x\012\000"
.LC31:
	.ascii	"PLT = 0x%x\012\000"
.LC32:
	.ascii	"POT = 0x%x\012\000"
.LC33:
	.ascii	"MaxSector = 0x%x\012\000"
.LC34:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC35:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC36:
	.ascii	"free sysblock = 0x%x\012\000"
.LC37:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC38:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC39:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC40:
	.ascii	"Sys.id = 0x%x\012\000"
.LC41:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC42:
	.ascii	"ACT.page = 0x%x\012\000"
.LC43:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC44:
	.ascii	"ACT.id = 0x%x\012\000"
.LC45:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC46:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC47:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC48:
	.ascii	"BUF.page = 0x%x\012\000"
.LC49:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC50:
	.ascii	"BUF.id = 0x%x\012\000"
.LC51:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC52:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC53:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC54:
	.ascii	"TMP.page = 0x%x\012\000"
.LC55:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC56:
	.ascii	"TMP.id = 0x%x\012\000"
.LC57:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC58:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC59:
	.ascii	"GC.page = 0x%x\012\000"
.LC60:
	.ascii	"GC.plane = 0x%x\012\000"
.LC61:
	.ascii	"GC.id = 0x%x\012\000"
.LC62:
	.ascii	"GC.mode = 0x%x\012\000"
.LC63:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC64:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC65:
	.ascii	"Read Err = 0x%x\012\000"
.LC66:
	.ascii	"Prog Err = 0x%x\012\000"
.LC67:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC68:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC69:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC70:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC71:
	.ascii	"free min EC= 0x%x\012\000"
.LC72:
	.ascii	"free max EC= 0x%x\012\000"
.LC73:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC74:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC75:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC76:
	.ascii	"%s\012\000"
.LC77:
	.ascii	"FTL version: 5.0.63 20200923\000"
.LC78:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC79:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC80:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC81:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC82:
	.ascii	"%s %p:0x%x:\000"
.LC83:
	.ascii	"%x \000"
.LC84:
	.ascii	"\000"
.LC85:
	.ascii	"otp error! %d\000"
.LC86:
	.ascii	"rr\000"
.LC87:
	.ascii	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\012"
	.ascii	"\000"
.LC88:
	.ascii	"nandc:\000"
.LC89:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC90:
	.ascii	"sdr read ok %x ecc=%d\012\000"
.LC91:
	.ascii	"sync para %d\012\000"
.LC92:
	.ascii	"TOG mode Read error %x %x\012\000"
.LC93:
	.ascii	"read retry status %x %x %x\012\000"
.LC94:
	.ascii	"micron RR %d row=%x,count %d,status=%d\012\000"
.LC95:
	.ascii	"samsung RR %d row=%x,count %d,status=%d\012\000"
.LC96:
	.ascii	"ECC:%d\012\000"
.LC97:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC98:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC99:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC100:
	.ascii	"data:\000"
.LC101:
	.ascii	"spare:\000"
.LC102:
	.ascii	"ReadRetry pageadd=%x ecc=%x err=%x\012\000"
.LC103:
	.ascii	"FLFB:%d %d\012\000"
.LC104:
	.ascii	"prog error: = %x\012\000"
.LC105:
	.ascii	"prog read error: = %x\012\000"
.LC106:
	.ascii	"prog read REFRESH: = %x\012\000"
.LC107:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC108:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC109:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data= %x\012"
	.ascii	"\000"
.LC110:
	.ascii	"...%s enter...\012\000"
.LC111:
	.ascii	"superBlkID = %x vpc=%x\012\000"
.LC112:
	.ascii	"flashmode = %x pagenum = %x %x\012\000"
.LC113:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC114:
	.ascii	"blk = %x vpc=%x mode = %x\012\000"
.LC115:
	.ascii	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC116:
	.ascii	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC117:
	.ascii	"ftl_scan_all_ppa blk %x page %x flag: %x\012\000"
.LC118:
	.ascii	"ftl_scan_all_ppa blk %x page %x flag: %x .........."
	.ascii	"..... is bad block\012\000"
.LC119:
	.ascii	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC120:
	.ascii	"%s finished\012\000"
.LC121:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC122:
	.ascii	"bad block:%d %d\012\000"
.LC123:
	.ascii	"FMFB:%d %d\012\000"
.LC124:
	.ascii	"E:bad block:%d\012\000"
.LC125:
	.ascii	"FMFB:Save %d %d\012\000"
.LC126:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC127:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC128:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC129:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC130:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC131:
	.ascii	"decrement_vpc_count %x = %d in free list\012\000"
.LC132:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC133:
	.ascii	"page map lost: %x %x\012\000"
.LC134:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC135:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC136:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC137:
	.ascii	"no ect\000"
.LC138:
	.ascii	"slc mode\000"
.LC139:
	.ascii	"BBT:\000"
.LC140:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC141:
	.ascii	"map_ppn:\000"
.LC142:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC143:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC144:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC145:
	.ascii	"error_flag %x\012\000"
.LC146:
	.ascii	"Ftlscanalldata = %x\012\000"
.LC147:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC148:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC149:
	.ascii	"RSB refresh addr %x\012\000"
.LC150:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC151:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC152:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC153:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
.LC154:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC155:
	.ascii	"GC des block %x done\012\000"
.LC156:
	.ascii	"too many bad block  = %d %d\012\000"
.LC157:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC158:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC159:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC160:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC161:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC162:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC163:
	.ascii	"FtlInit %x\012\000"
.LC164:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC165:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC166:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC167:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\012"
	.ascii	"\000"
.LC168:
	.ascii	":\000"
.LC169:
	.ascii	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC170:
	.ascii	"Mblk:\000"
.LC171:
	.ascii	"L2P:\000"
.LC172:
	.ascii	"L2PC:\000"
.LC173:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC174:
	.ascii	"idblk:\000"
.LC175:
	.ascii	"idb reverse %x %x\012\000"
.LC176:
	.ascii	"write_idblock totle_sec %x %x %x %x\012\000"
.LC177:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC178:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC179:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC180:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC181:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC182:
	.ascii	"write\000"
.LC183:
	.ascii	"read\000"
.LC184:
	.ascii	"write_idblock error %d\012\000"
.LC185:
	.ascii	"wl_lba %p %x %x %x\012\000"
.LC186:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC187:
	.ascii	"rk_copy_from_user error\012\000"
.LC188:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC189:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC190:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC191:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC192:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC193:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC194:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC195:
	.ascii	"LockKey not match %d\012\000"
.LC196:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC197:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC198:
	.ascii	"return ret = %lx\012\000"
.LC199:
	.ascii	"secureBootEn check error\012\000"
.LC200:
	.ascii	"\0013vendor storage %x,%x,%x\012\000"
	.data
	.align	2
.LANCHOR1 = . + 0
	.type	random_seed, %object
	.size	random_seed, 256
random_seed:
	.short	22378
	.short	1512
	.short	25245
	.short	17827
	.short	25756
	.short	19440
	.short	9026
	.short	10030
	.short	29528
	.short	20467
	.short	29676
	.short	24432
	.short	31328
	.short	6872
	.short	13426
	.short	13842
	.short	8783
	.short	1108
	.short	782
	.short	28837
	.short	30729
	.short	9505
	.short	18676
	.short	23085
	.short	18730
	.short	1085
	.short	32609
	.short	14697
	.short	20858
	.short	15170
	.short	30365
	.short	1607
	.short	32298
	.short	4995
	.short	18905
	.short	1976
	.short	9592
	.short	20204
	.short	17443
	.short	13615
	.short	23330
	.short	29369
	.short	13947
	.short	9398
	.short	32398
	.short	8984
	.short	27600
	.short	21785
	.short	6019
	.short	6311
	.short	31598
	.short	30210
	.short	19327
	.short	13896
	.short	11347
	.short	27545
	.short	3107
	.short	26575
	.short	32270
	.short	19852
	.short	20601
	.short	8349
	.short	9290
	.short	29819
	.short	13579
	.short	3661
	.short	28676
	.short	27331
	.short	32574
	.short	8693
	.short	31253
	.short	9081
	.short	5399
	.short	6842
	.short	20087
	.short	5537
	.short	1274
	.short	11617
	.short	9530
	.short	4866
	.short	8035
	.short	23219
	.short	1178
	.short	23272
	.short	7383
	.short	18944
	.short	12488
	.short	12871
	.short	29340
	.short	20532
	.short	11022
	.short	22514
	.short	228
	.short	22363
	.short	24978
	.short	14584
	.short	12138
	.short	3092
	.short	17916
	.short	16863
	.short	14554
	.short	31457
	.short	29474
	.short	25311
	.short	24121
	.short	3684
	.short	28037
	.short	22865
	.short	22839
	.short	25217
	.short	13217
	.short	27186
	.short	14938
	.short	11180
	.short	29754
	.short	24180
	.short	15150
	.short	32455
	.short	20434
	.short	23848
	.short	29983
	.short	16120
	.short	14769
	.short	20041
	.short	29803
	.short	28406
	.short	17598
	.short	28087
	.type	ToshibaA19RefValue, %object
	.size	ToshibaA19RefValue, 45
ToshibaA19RefValue:
	.byte	4
	.byte	5
	.byte	6
	.byte	7
	.byte	13
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	4
	.byte	4
	.byte	124
	.byte	126
	.byte	0
	.byte	0
	.byte	124
	.byte	120
	.byte	120
	.byte	0
	.byte	124
	.byte	118
	.byte	116
	.byte	114
	.byte	0
	.byte	8
	.byte	8
	.byte	0
	.byte	0
	.byte	0
	.byte	11
	.byte	126
	.byte	118
	.byte	116
	.byte	0
	.byte	16
	.byte	118
	.byte	114
	.byte	112
	.byte	0
	.byte	2
	.byte	0
	.byte	126
	.byte	124
	.byte	0
	.space	3
	.type	Toshiba15RefValue, %object
	.size	Toshiba15RefValue, 95
Toshiba15RefValue:
	.byte	4
	.byte	5
	.byte	6
	.byte	7
	.byte	13
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	2
	.byte	4
	.byte	2
	.byte	0
	.byte	0
	.byte	8
	.byte	8
	.byte	0
	.byte	0
	.byte	0
	.byte	124
	.byte	0
	.byte	124
	.byte	124
	.byte	0
	.byte	122
	.byte	0
	.byte	122
	.byte	122
	.byte	0
	.byte	11
	.byte	126
	.byte	118
	.byte	116
	.byte	0
	.byte	120
	.byte	2
	.byte	120
	.byte	122
	.byte	0
	.byte	126
	.byte	4
	.byte	126
	.byte	122
	.byte	0
	.byte	16
	.byte	118
	.byte	114
	.byte	112
	.byte	0
	.byte	118
	.byte	4
	.byte	118
	.byte	120
	.byte	0
	.byte	4
	.byte	4
	.byte	4
	.byte	118
	.byte	0
	.byte	2
	.byte	0
	.byte	126
	.byte	124
	.byte	0
	.byte	6
	.byte	10
	.byte	6
	.byte	2
	.byte	0
	.byte	116
	.byte	124
	.byte	116
	.byte	118
	.byte	0
	.byte	4
	.byte	4
	.byte	124
	.byte	126
	.byte	0
	.byte	0
	.byte	124
	.byte	120
	.byte	120
	.byte	0
	.byte	124
	.byte	118
	.byte	116
	.byte	114
	.byte	0
	.space	1
	.type	ToshibaRefValue, %object
	.size	ToshibaRefValue, 8
ToshibaRefValue:
	.byte	0
	.byte	4
	.byte	124
	.byte	120
	.byte	116
	.byte	8
	.byte	12
	.byte	112
	.type	SamsungRefValue, %object
	.size	SamsungRefValue, 64
SamsungRefValue:
	.byte	-89
	.byte	-92
	.byte	-91
	.byte	-90
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	5
	.byte	10
	.byte	0
	.byte	0
	.byte	40
	.byte	0
	.byte	-20
	.byte	-40
	.byte	-19
	.byte	-11
	.byte	-19
	.byte	-26
	.byte	10
	.byte	15
	.byte	5
	.byte	0
	.byte	15
	.byte	10
	.byte	-5
	.byte	-20
	.byte	-24
	.byte	-17
	.byte	-24
	.byte	-36
	.byte	-15
	.byte	-5
	.byte	-2
	.byte	-16
	.byte	10
	.byte	0
	.byte	-5
	.byte	-20
	.byte	-48
	.byte	-30
	.byte	-48
	.byte	-62
	.byte	20
	.byte	15
	.byte	-5
	.byte	-20
	.byte	-24
	.byte	-5
	.byte	-24
	.byte	-36
	.byte	30
	.byte	20
	.byte	-5
	.byte	-20
	.byte	-5
	.byte	-1
	.byte	-5
	.byte	-8
	.byte	7
	.byte	12
	.byte	2
	.byte	0
	.type	gNandParaInfo, %object
	.size	gNandParaInfo, 32
gNandParaInfo:
	.byte	0
	.byte	0
	.space	5
	.byte	0
	.byte	1
	.byte	8
	.short	128
	.byte	2
	.byte	1
	.short	2048
	.short	0
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.type	gFtlInitStatus, %object
	.size	gFtlInitStatus, 4
gFtlInitStatus:
	.word	-1
	.type	NandFlashParaTbl, %object
	.size	NandFlashParaTbl, 2752
NandFlashParaTbl:
	.byte	6
	.byte	44
	.byte	100
	.byte	68
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	68
	.byte	68
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	1064
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	104
	.byte	4
	.byte	74
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	-120
	.byte	4
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	-88
	.byte	5
	.byte	-53
	.byte	-87
	.byte	0
	.byte	4
	.byte	2
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	104
	.byte	4
	.byte	70
	.byte	-119
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	72
	.byte	4
	.byte	74
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.short	256
	.byte	2
	.byte	2
	.short	1024
	.short	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	-124
	.byte	100
	.byte	60
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	2
	.short	1024
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	-124
	.byte	100
	.byte	84
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	2
	.short	1024
	.short	479
	.byte	4
	.byte	18
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	-41
	.byte	-108
	.byte	62
	.byte	-124
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.short	128
	.byte	2
	.byte	2
	.short	4096
	.short	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	72
	.byte	4
	.byte	70
	.byte	-123
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.short	256
	.byte	2
	.byte	2
	.short	1024
	.short	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	-120
	.byte	5
	.byte	-58
	.byte	-119
	.byte	0
	.byte	4
	.byte	2
	.byte	8
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	-120
	.byte	36
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	287
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	104
	.byte	0
	.byte	39
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	16
	.short	128
	.byte	1
	.byte	2
	.short	2048
	.short	287
	.byte	0
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	100
	.byte	100
	.byte	86
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	24
	.short	512
	.byte	2
	.byte	2
	.short	700
	.short	479
	.byte	4
	.byte	18
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	-124
	.byte	-59
	.byte	75
	.byte	-87
	.byte	0
	.byte	4
	.byte	2
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	-43
	.byte	-47
	.byte	-90
	.byte	104
	.byte	0
	.byte	4
	.byte	2
	.byte	8
	.short	64
	.byte	1
	.byte	2
	.short	2048
	.short	279
	.byte	0
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	-36
	.byte	-112
	.byte	-90
	.byte	84
	.byte	0
	.byte	4
	.byte	1
	.byte	8
	.short	64
	.byte	1
	.byte	2
	.short	1024
	.short	279
	.byte	0
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	100
	.byte	100
	.byte	84
	.byte	-92
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	1024
	.short	479
	.byte	4
	.byte	18
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	-124
	.byte	68
	.byte	50
	.byte	-86
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	2184
	.short	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	100
	.byte	68
	.byte	50
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	1048
	.short	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	100
	.byte	100
	.byte	60
	.byte	-91
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	1044
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	-124
	.byte	68
	.byte	50
	.byte	-86
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	2184
	.short	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.byte	1
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	-124
	.byte	68
	.byte	52
	.byte	-86
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	2184
	.short	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.byte	1
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	-124
	.byte	-60
	.byte	52
	.byte	-86
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	2184
	.short	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	-124
	.byte	68
	.byte	52
	.byte	-92
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	2184
	.short	1479
	.byte	5
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.space	4
	.byte	5
	.byte	44
	.byte	-124
	.byte	100
	.byte	60
	.byte	-87
	.byte	0
	.byte	4
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	2
	.short	1024
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	44
	.byte	-92
	.byte	100
	.byte	50
	.byte	-86
	.byte	4
	.byte	4
	.byte	1
	.byte	32
	.short	1024
	.byte	2
	.byte	1
	.short	2192
	.short	1479
	.byte	10
	.byte	19
	.byte	60
	.byte	32
	.byte	1
	.byte	0
	.byte	4
	.byte	0
	.byte	1
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-34
	.byte	-108
	.byte	-46
	.byte	4
	.byte	67
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	473
	.byte	1
	.byte	1
	.byte	24
	.byte	32
	.byte	4
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-41
	.byte	-108
	.byte	-38
	.byte	116
	.byte	-61
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	1024
	.short	473
	.byte	1
	.byte	2
	.byte	40
	.byte	32
	.byte	4
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-41
	.byte	-108
	.byte	-111
	.byte	96
	.byte	68
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	1046
	.short	473
	.byte	1
	.byte	3
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-34
	.byte	-108
	.byte	-38
	.byte	116
	.byte	-60
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2090
	.short	473
	.byte	1
	.byte	4
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-34
	.byte	-108
	.byte	-21
	.byte	116
	.byte	68
	.byte	2
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1066
	.short	473
	.byte	1
	.byte	7
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-43
	.byte	-108
	.byte	-38
	.byte	116
	.byte	-60
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	530
	.short	473
	.byte	1
	.byte	3
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-41
	.byte	-108
	.byte	-102
	.byte	116
	.byte	66
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	1024
	.short	281
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	4
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-34
	.byte	20
	.byte	-89
	.byte	66
	.byte	74
	.byte	2
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1060
	.short	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-41
	.byte	20
	.byte	-98
	.byte	52
	.byte	74
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	1056
	.short	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-34
	.byte	-108
	.byte	-89
	.byte	66
	.byte	72
	.byte	2
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1060
	.short	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-34
	.byte	20
	.byte	-85
	.byte	66
	.byte	74
	.byte	2
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1056
	.short	473
	.byte	2
	.byte	6
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	58
	.byte	20
	.byte	-85
	.byte	66
	.byte	74
	.byte	2
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	2092
	.short	473
	.byte	2
	.byte	5
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	-43
	.byte	-108
	.byte	-102
	.byte	116
	.byte	66
	.byte	2
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	1
	.short	1024
	.short	273
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	4
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-83
	.byte	58
	.byte	20
	.byte	3
	.byte	8
	.byte	80
	.byte	2
	.byte	1
	.byte	32
	.short	388
	.byte	2
	.byte	2
	.short	1362
	.short	473
	.byte	9
	.byte	8
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-119
	.byte	100
	.byte	68
	.byte	75
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-119
	.byte	-120
	.byte	36
	.byte	75
	.byte	-87
	.byte	-124
	.byte	7
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	479
	.byte	3
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-119
	.byte	-120
	.byte	36
	.byte	75
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-119
	.byte	104
	.byte	36
	.byte	74
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	8
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-119
	.byte	104
	.byte	4
	.byte	74
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	8
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-119
	.byte	-41
	.byte	-108
	.byte	62
	.byte	-124
	.byte	0
	.byte	7
	.byte	1
	.byte	8
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-119
	.byte	104
	.byte	4
	.byte	70
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	8
	.short	256
	.byte	2
	.byte	2
	.short	2048
	.short	279
	.byte	1
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	-119
	.byte	100
	.byte	100
	.byte	60
	.byte	-95
	.byte	0
	.byte	7
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	1
	.short	1024
	.short	455
	.byte	4
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	-119
	.byte	-124
	.byte	100
	.byte	60
	.byte	-91
	.byte	0
	.byte	7
	.byte	1
	.byte	32
	.short	512
	.byte	2
	.byte	2
	.short	1024
	.short	455
	.byte	4
	.byte	17
	.byte	40
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-119
	.byte	-120
	.byte	36
	.byte	59
	.byte	-87
	.byte	0
	.byte	7
	.byte	1
	.byte	16
	.short	192
	.byte	2
	.byte	2
	.short	2048
	.short	279
	.byte	12
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	87
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	1060
	.short	1473
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-124
	.byte	-109
	.byte	114
	.byte	87
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	2092
	.short	1473
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	58
	.byte	-123
	.byte	-109
	.byte	118
	.byte	87
	.byte	1
	.byte	2
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	2092
	.short	1505
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-43
	.byte	-124
	.byte	50
	.byte	114
	.byte	86
	.byte	1
	.byte	1
	.byte	16
	.short	128
	.byte	2
	.byte	1
	.short	2056
	.short	1473
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-108
	.byte	50
	.byte	118
	.byte	86
	.byte	1
	.byte	1
	.byte	16
	.short	128
	.byte	2
	.byte	2
	.short	2058
	.short	1489
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-126
	.byte	118
	.byte	86
	.byte	1
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2062
	.short	1489
	.byte	1
	.byte	33
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	80
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1066
	.short	1497
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	58
	.byte	-107
	.byte	-109
	.byte	122
	.byte	80
	.byte	1
	.byte	2
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1066
	.short	1497
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-108
	.byte	50
	.byte	118
	.byte	85
	.byte	1
	.byte	1
	.byte	16
	.short	128
	.byte	2
	.byte	2
	.short	2050
	.short	401
	.byte	2
	.byte	0
	.byte	24
	.byte	32
	.byte	1
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	87
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1058
	.short	1497
	.byte	2
	.byte	33
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	80
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	1060
	.short	1473
	.byte	2
	.byte	34
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	81
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1074
	.short	1497
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	58
	.byte	-108
	.byte	-109
	.byte	118
	.byte	81
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	2106
	.short	1497
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	81
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	1056
	.short	1497
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-104
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	-47
	.byte	1
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1074
	.short	1497
	.byte	2
	.byte	35
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	87
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1058
	.short	1497
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	87
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	1060
	.short	1473
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	2
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-92
	.byte	-126
	.byte	118
	.byte	86
	.byte	8
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2082
	.short	473
	.byte	1
	.byte	65
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	80
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1066
	.short	1497
	.byte	2
	.byte	67
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-41
	.byte	-124
	.byte	-109
	.byte	114
	.byte	80
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	1060
	.short	1473
	.byte	2
	.byte	67
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-92
	.byte	-126
	.byte	118
	.byte	-41
	.byte	8
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2090
	.short	1241
	.byte	1
	.byte	66
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-124
	.byte	-109
	.byte	114
	.byte	87
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	1
	.short	2092
	.short	1473
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	58
	.byte	-108
	.byte	-109
	.byte	118
	.byte	81
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	2106
	.short	473
	.byte	2
	.byte	68
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	81
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1074
	.short	473
	.byte	2
	.byte	68
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	4
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	58
	.byte	-92
	.byte	-109
	.byte	122
	.byte	80
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	2138
	.short	1497
	.byte	2
	.byte	0
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-126
	.byte	118
	.byte	86
	.byte	8
	.byte	1
	.byte	16
	.short	256
	.byte	2
	.byte	2
	.short	2062
	.short	473
	.byte	1
	.byte	0
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	69
	.byte	-34
	.byte	-108
	.byte	-109
	.byte	118
	.byte	-41
	.byte	8
	.byte	1
	.byte	32
	.short	256
	.byte	2
	.byte	2
	.short	1058
	.short	1497
	.byte	2
	.byte	66
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	5
	.byte	-20
	.byte	-41
	.byte	-108
	.byte	126
	.byte	100
	.byte	68
	.byte	0
	.byte	1
	.byte	16
	.short	128
	.byte	2
	.byte	2
	.short	2048
	.short	473
	.byte	2
	.byte	49
	.byte	60
	.byte	36
	.byte	3
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-20
	.byte	-34
	.byte	-43
	.byte	126
	.byte	104
	.byte	68
	.byte	0
	.byte	2
	.byte	16
	.short	128
	.byte	2
	.byte	2
	.short	2048
	.short	505
	.byte	2
	.byte	49
	.byte	60
	.byte	36
	.byte	3
	.byte	0
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-20
	.byte	-41
	.byte	-108
	.byte	122
	.byte	84
	.byte	67
	.byte	0
	.byte	1
	.byte	16
	.short	128
	.byte	2
	.byte	2
	.short	2076
	.short	409
	.byte	2
	.byte	0
	.byte	40
	.byte	36
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-20
	.byte	-34
	.byte	-43
	.byte	122
	.byte	88
	.byte	67
	.byte	0
	.byte	2
	.byte	16
	.short	128
	.byte	2
	.byte	2
	.short	2076
	.short	441
	.byte	2
	.byte	0
	.byte	40
	.byte	36
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-20
	.byte	-43
	.byte	-108
	.byte	118
	.byte	84
	.byte	67
	.byte	0
	.byte	1
	.byte	16
	.short	128
	.byte	2
	.byte	2
	.short	1038
	.short	281
	.byte	2
	.byte	0
	.byte	24
	.byte	36
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-20
	.byte	-41
	.byte	20
	.byte	118
	.byte	84
	.byte	-62
	.byte	0
	.byte	1
	.byte	16
	.short	128
	.byte	2
	.byte	2
	.short	2076
	.short	1169
	.byte	2
	.byte	0
	.byte	24
	.byte	40
	.byte	3
	.byte	1
	.byte	3
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.byte	6
	.byte	-20
	.byte	-34
	.byte	-108
	.byte	-61
	.byte	-92
	.byte	-54
	.byte	0
	.byte	1
	.byte	32
	.short	792
	.byte	2
	.byte	1
	.short	688
	.short	1217
	.byte	11
	.byte	50
	.byte	40
	.byte	32
	.byte	3
	.byte	1
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.space	4
	.type	NandOptPara, %object
	.size	NandOptPara, 128
NandOptPara:
	.byte	1
	.byte	0
	.byte	49
	.byte	63
	.byte	0
	.byte	49
	.byte	-128
	.byte	21
	.byte	0
	.byte	50
	.byte	17
	.byte	-128
	.byte	112
	.byte	120
	.byte	120
	.byte	3
	.byte	1
	.byte	0
	.space	14
	.byte	2
	.byte	0
	.byte	49
	.byte	63
	.byte	0
	.byte	49
	.byte	-128
	.byte	21
	.byte	0
	.byte	0
	.byte	17
	.byte	-127
	.byte	112
	.byte	-15
	.byte	-14
	.byte	0
	.byte	0
	.byte	0
	.space	14
	.byte	3
	.byte	0
	.byte	49
	.byte	63
	.byte	0
	.byte	49
	.byte	-128
	.byte	21
	.byte	96
	.byte	96
	.byte	17
	.byte	-127
	.byte	112
	.byte	-15
	.byte	-14
	.byte	0
	.byte	0
	.byte	0
	.space	14
	.byte	4
	.byte	0
	.byte	49
	.byte	63
	.byte	0
	.byte	49
	.byte	-128
	.byte	21
	.byte	96
	.byte	96
	.byte	17
	.byte	-127
	.byte	112
	.byte	112
	.byte	112
	.byte	0
	.byte	0
	.byte	0
	.space	14
	.type	refValueDefault, %object
	.size	refValueDefault, 28
refValueDefault:
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	6
	.byte	10
	.byte	6
	.byte	0
	.byte	-3
	.byte	-7
	.byte	-8
	.byte	0
	.byte	-6
	.byte	-13
	.byte	-15
	.byte	0
	.byte	-11
	.byte	-20
	.byte	-23
	.byte	0
	.byte	0
	.byte	-26
	.byte	-30
	.byte	0
	.byte	0
	.byte	-32
	.byte	-37
	.type	gSlcNandParaInfo, %object
	.size	gSlcNandParaInfo, 32
gSlcNandParaInfo:
	.byte	2
	.byte	-104
	.byte	-15
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	1
	.byte	1
	.byte	4
	.short	64
	.byte	1
	.byte	1
	.short	1024
	.short	256
	.byte	0
	.byte	0
	.byte	16
	.byte	40
	.byte	1
	.byte	0
	.byte	1
	.byte	0
	.byte	0
	.byte	0
	.space	4
	.type	ftl_gc_temp_block_bops_scan_page_addr, %object
	.size	ftl_gc_temp_block_bops_scan_page_addr, 2
ftl_gc_temp_block_bops_scan_page_addr:
	.short	-1
	.space	2
	.type	power_up_flag, %object
	.size	power_up_flag, 4
power_up_flag:
	.word	1
	.bss
	.align	2
.LANCHOR0 = . + 0
.LANCHOR2 = . + 4344
.LANCHOR4 = . + 8688
.LANCHOR5 = . + 17376
	.type	g_slc2KBNand, %object
	.size	g_slc2KBNand, 1
g_slc2KBNand:
	.space	1
	.type	gNandIDBResBlkNum, %object
	.size	gNandIDBResBlkNum, 1
gNandIDBResBlkNum:
	.space	1
	.space	2
	.type	gBlockPageAlignSize, %object
	.size	gBlockPageAlignSize, 4
gBlockPageAlignSize:
	.space	4
	.type	gNandRandomizer, %object
	.size	gNandRandomizer, 1
gNandRandomizer:
	.space	1
	.space	3
	.type	gNandChipMap, %object
	.size	gNandChipMap, 32
gNandChipMap:
	.space	32
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 4
gpNandParaInfo:
	.space	4
	.type	gNandOptPara, %object
	.size	gNandOptPara, 32
gNandOptPara:
	.space	32
	.type	gpNandc, %object
	.size	gpNandc, 4
gpNandc:
	.space	4
	.type	NANDC_FMCTL, %object
	.size	NANDC_FMCTL, 4
NANDC_FMCTL:
	.space	4
	.type	NANDC_FMWAIT, %object
	.size	NANDC_FMWAIT, 4
NANDC_FMWAIT:
	.space	4
	.type	NANDC_FLCTL, %object
	.size	NANDC_FLCTL, 4
NANDC_FLCTL:
	.space	4
	.type	NANDC_BCHCTL, %object
	.size	NANDC_BCHCTL, 4
NANDC_BCHCTL:
	.space	4
	.type	NANDC_DLL_CTL_REG0, %object
	.size	NANDC_DLL_CTL_REG0, 4
NANDC_DLL_CTL_REG0:
	.space	4
	.type	NANDC_DLL_CTL_REG1, %object
	.size	NANDC_DLL_CTL_REG1, 4
NANDC_DLL_CTL_REG1:
	.space	4
	.type	NANDC_RANDMZ_CFG, %object
	.size	NANDC_RANDMZ_CFG, 4
NANDC_RANDMZ_CFG:
	.space	4
	.type	NANDC_FMWAIT_SYN, %object
	.size	NANDC_FMWAIT_SYN, 4
NANDC_FMWAIT_SYN:
	.space	4
	.type	gNandPhyInfo, %object
	.size	gNandPhyInfo, 28
gNandPhyInfo:
	.space	28
	.type	gFlashSlcMode, %object
	.size	gFlashSlcMode, 1
gFlashSlcMode:
	.space	1
	.space	3
	.type	slcPageToMlcPageTbl, %object
	.size	slcPageToMlcPageTbl, 1024
slcPageToMlcPageTbl:
	.space	1024
	.type	DieAddrs, %object
	.size	DieAddrs, 32
DieAddrs:
	.space	32
	.type	FlashWaitBusyScheduleEn, %object
	.size	FlashWaitBusyScheduleEn, 4
FlashWaitBusyScheduleEn:
	.space	4
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
	.space	1
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.space	1
	.type	gReadRetryInfo, %object
	.size	gReadRetryInfo, 852
gReadRetryInfo:
	.space	852
	.space	2
	.type	read_retry_cur_offset, %object
	.size	read_retry_cur_offset, 4
read_retry_cur_offset:
	.space	4
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
	.space	32
	.type	gDieOp, %object
	.size	gDieOp, 128
gDieOp:
	.space	128
	.type	gFlashCurMode, %object
	.size	gFlashCurMode, 1
gFlashCurMode:
	.space	1
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.space	1
	.type	gNandMaxDie, %object
	.size	gNandMaxDie, 1
gNandMaxDie:
	.space	1
	.space	1
	.type	DieCsIndex, %object
	.size	DieCsIndex, 8
DieCsIndex:
	.space	8
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.space	1
	.space	3
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
	.space	4
	.type	idb_flash_slc_mode, %object
	.size	idb_flash_slc_mode, 4
idb_flash_slc_mode:
	.space	4
	.type	gFlashToggleModeEn, %object
	.size	gFlashToggleModeEn, 1
gFlashToggleModeEn:
	.space	1
	.space	3
	.type	gBootDdrMode, %object
	.size	gBootDdrMode, 4
gBootDdrMode:
	.space	4
	.type	gNandcVer, %object
	.size	gNandcVer, 4
gNandcVer:
	.space	4
	.type	g_nandc_version_data, %object
	.size	g_nandc_version_data, 4
g_nandc_version_data:
	.space	4
	.type	gMasterTempBuf, %object
	.size	gMasterTempBuf, 4
gMasterTempBuf:
	.space	4
	.type	gMasterInfo, %object
	.size	gMasterInfo, 32
gMasterInfo:
	.space	32
	.type	gNandcDumpWriteEn, %object
	.size	gNandcDumpWriteEn, 4
gNandcDumpWriteEn:
	.space	4
	.type	gNandcEccBits, %object
	.size	gNandcEccBits, 4
gNandcEccBits:
	.space	4
	.type	gNandFlashEccBits, %object
	.size	gNandFlashEccBits, 1
gNandFlashEccBits:
	.space	1
	.space	3
	.type	c_ftl_nand_sys_blks_per_plane, %object
	.size	c_ftl_nand_sys_blks_per_plane, 4
c_ftl_nand_sys_blks_per_plane:
	.space	4
	.type	c_ftl_nand_planes_num, %object
	.size	c_ftl_nand_planes_num, 2
c_ftl_nand_planes_num:
	.space	2
	.space	2
	.type	c_ftl_nand_max_sys_blks, %object
	.size	c_ftl_nand_max_sys_blks, 4
c_ftl_nand_max_sys_blks:
	.space	4
	.type	c_ftl_nand_data_blks_per_plane, %object
	.size	c_ftl_nand_data_blks_per_plane, 2
c_ftl_nand_data_blks_per_plane:
	.space	2
	.type	c_ftl_nand_blk_pre_plane, %object
	.size	c_ftl_nand_blk_pre_plane, 2
c_ftl_nand_blk_pre_plane:
	.space	2
	.type	c_ftl_nand_max_data_blks, %object
	.size	c_ftl_nand_max_data_blks, 4
c_ftl_nand_max_data_blks:
	.space	4
	.type	c_ftl_nand_totle_phy_blks, %object
	.size	c_ftl_nand_totle_phy_blks, 4
c_ftl_nand_totle_phy_blks:
	.space	4
	.type	c_ftl_nand_type, %object
	.size	c_ftl_nand_type, 2
c_ftl_nand_type:
	.space	2
	.type	c_ftl_nand_die_num, %object
	.size	c_ftl_nand_die_num, 2
c_ftl_nand_die_num:
	.space	2
	.type	c_ftl_nand_planes_per_die, %object
	.size	c_ftl_nand_planes_per_die, 2
c_ftl_nand_planes_per_die:
	.space	2
	.space	2
	.type	p_plane_order_table, %object
	.size	p_plane_order_table, 32
p_plane_order_table:
	.space	32
	.type	c_mlc_erase_count_value, %object
	.size	c_mlc_erase_count_value, 2
c_mlc_erase_count_value:
	.space	2
	.type	c_ftl_nand_ext_blk_pre_plane, %object
	.size	c_ftl_nand_ext_blk_pre_plane, 2
c_ftl_nand_ext_blk_pre_plane:
	.space	2
	.type	c_ftl_vendor_part_size, %object
	.size	c_ftl_vendor_part_size, 2
c_ftl_vendor_part_size:
	.space	2
	.type	c_ftl_nand_blks_per_die, %object
	.size	c_ftl_nand_blks_per_die, 2
c_ftl_nand_blks_per_die:
	.space	2
	.type	c_ftl_nand_page_pre_blk, %object
	.size	c_ftl_nand_page_pre_blk, 2
c_ftl_nand_page_pre_blk:
	.space	2
	.type	c_ftl_nand_page_pre_slc_blk, %object
	.size	c_ftl_nand_page_pre_slc_blk, 2
c_ftl_nand_page_pre_slc_blk:
	.space	2
	.type	c_ftl_nand_page_pre_super_blk, %object
	.size	c_ftl_nand_page_pre_super_blk, 2
c_ftl_nand_page_pre_super_blk:
	.space	2
	.type	c_ftl_nand_sec_pre_page, %object
	.size	c_ftl_nand_sec_pre_page, 2
c_ftl_nand_sec_pre_page:
	.space	2
	.type	c_ftl_nand_sec_pre_page_shift, %object
	.size	c_ftl_nand_sec_pre_page_shift, 2
c_ftl_nand_sec_pre_page_shift:
	.space	2
	.type	c_ftl_nand_byte_pre_page, %object
	.size	c_ftl_nand_byte_pre_page, 2
c_ftl_nand_byte_pre_page:
	.space	2
	.type	c_ftl_nand_byte_pre_oob, %object
	.size	c_ftl_nand_byte_pre_oob, 2
c_ftl_nand_byte_pre_oob:
	.space	2
	.type	c_ftl_nand_reserved_blks, %object
	.size	c_ftl_nand_reserved_blks, 2
c_ftl_nand_reserved_blks:
	.space	2
	.type	DeviceCapacity, %object
	.size	DeviceCapacity, 4
DeviceCapacity:
	.space	4
	.type	c_ftl_nand_max_vendor_blks, %object
	.size	c_ftl_nand_max_vendor_blks, 2
c_ftl_nand_max_vendor_blks:
	.space	2
	.type	c_ftl_nand_vendor_region_num, %object
	.size	c_ftl_nand_vendor_region_num, 2
c_ftl_nand_vendor_region_num:
	.space	2
	.type	c_ftl_nand_map_blks_per_plane, %object
	.size	c_ftl_nand_map_blks_per_plane, 2
c_ftl_nand_map_blks_per_plane:
	.space	2
	.space	2
	.type	c_ftl_nand_max_map_blks, %object
	.size	c_ftl_nand_max_map_blks, 4
c_ftl_nand_max_map_blks:
	.space	4
	.type	c_ftl_nand_init_sys_blks_per_plane, %object
	.size	c_ftl_nand_init_sys_blks_per_plane, 4
c_ftl_nand_init_sys_blks_per_plane:
	.space	4
	.type	c_ftl_nand_map_region_num, %object
	.size	c_ftl_nand_map_region_num, 2
c_ftl_nand_map_region_num:
	.space	2
	.type	c_ftl_nand_l2pmap_ram_region_num, %object
	.size	c_ftl_nand_l2pmap_ram_region_num, 2
c_ftl_nand_l2pmap_ram_region_num:
	.space	2
	.type	g_MaxLbaSector, %object
	.size	g_MaxLbaSector, 4
g_MaxLbaSector:
	.space	4
	.type	g_page_map_check_enable, %object
	.size	g_page_map_check_enable, 2
g_page_map_check_enable:
	.space	2
	.type	g_totle_vendor_block, %object
	.size	g_totle_vendor_block, 2
g_totle_vendor_block:
	.space	2
	.type	p_vendor_block_table, %object
	.size	p_vendor_block_table, 4
p_vendor_block_table:
	.space	4
	.type	g_wr_page_num, %object
	.size	g_wr_page_num, 4
g_wr_page_num:
	.space	4
	.type	req_wr_io, %object
	.size	req_wr_io, 4
req_wr_io:
	.space	4
	.type	g_MaxLpn, %object
	.size	g_MaxLpn, 4
g_MaxLpn:
	.space	4
	.type	gBbtInfo, %object
	.size	gBbtInfo, 60
gBbtInfo:
	.space	60
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.space	2056
	.type	g_flash_read_only_en, %object
	.size	g_flash_read_only_en, 4
g_flash_read_only_en:
	.space	4
	.type	req_erase, %object
	.size	req_erase, 4
req_erase:
	.space	4
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 4
p_erase_count_table:
	.space	4
	.type	g_totle_sys_slc_erase_count, %object
	.size	g_totle_sys_slc_erase_count, 4
g_totle_sys_slc_erase_count:
	.space	4
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.space	48
	.type	p_data_block_list_table, %object
	.size	p_data_block_list_table, 4
p_data_block_list_table:
	.space	4
	.type	p_data_block_list_head, %object
	.size	p_data_block_list_head, 4
p_data_block_list_head:
	.space	4
	.type	p_valid_page_count_table, %object
	.size	p_valid_page_count_table, 4
p_valid_page_count_table:
	.space	4
	.type	p_data_block_list_tail, %object
	.size	p_data_block_list_tail, 4
p_data_block_list_tail:
	.space	4
	.type	g_num_data_superblocks, %object
	.size	g_num_data_superblocks, 2
g_num_data_superblocks:
	.space	2
	.space	2
	.type	p_free_data_block_list_head, %object
	.size	p_free_data_block_list_head, 4
p_free_data_block_list_head:
	.space	4
	.type	g_num_free_superblocks, %object
	.size	g_num_free_superblocks, 2
g_num_free_superblocks:
	.space	2
	.space	2
	.type	g_active_superblock, %object
	.size	g_active_superblock, 48
g_active_superblock:
	.space	48
	.type	g_buffer_superblock, %object
	.size	g_buffer_superblock, 48
g_buffer_superblock:
	.space	48
	.type	g_gc_temp_superblock, %object
	.size	g_gc_temp_superblock, 48
g_gc_temp_superblock:
	.space	48
	.type	p_l2p_ram_map, %object
	.size	p_l2p_ram_map, 4
p_l2p_ram_map:
	.space	4
	.type	g_l2p_last_update_region_id, %object
	.size	g_l2p_last_update_region_id, 2
g_l2p_last_update_region_id:
	.space	2
	.type	FtlUpdateVaildLpnCount, %object
	.size	FtlUpdateVaildLpnCount, 2
FtlUpdateVaildLpnCount:
	.space	2
	.type	g_VaildLpn, %object
	.size	g_VaildLpn, 4
g_VaildLpn:
	.space	4
	.type	p_blk_mode_table, %object
	.size	p_blk_mode_table, 4
p_blk_mode_table:
	.space	4
	.type	g_totle_read_page_count, %object
	.size	g_totle_read_page_count, 4
g_totle_read_page_count:
	.space	4
	.type	g_totle_discard_page_count, %object
	.size	g_totle_discard_page_count, 4
g_totle_discard_page_count:
	.space	4
	.type	g_totle_write_page_count, %object
	.size	g_totle_write_page_count, 4
g_totle_write_page_count:
	.space	4
	.type	g_totle_cache_write_count, %object
	.size	g_totle_cache_write_count, 4
g_totle_cache_write_count:
	.space	4
	.type	g_totle_l2p_write_count, %object
	.size	g_totle_l2p_write_count, 4
g_totle_l2p_write_count:
	.space	4
	.type	g_totle_gc_page_count, %object
	.size	g_totle_gc_page_count, 4
g_totle_gc_page_count:
	.space	4
	.type	g_totle_write_sector, %object
	.size	g_totle_write_sector, 4
g_totle_write_sector:
	.space	4
	.type	g_totle_read_sector, %object
	.size	g_totle_read_sector, 4
g_totle_read_sector:
	.space	4
	.type	g_GlobalSysVersion, %object
	.size	g_GlobalSysVersion, 4
g_GlobalSysVersion:
	.space	4
	.type	g_GlobalDataVersion, %object
	.size	g_GlobalDataVersion, 4
g_GlobalDataVersion:
	.space	4
	.type	g_totle_mlc_erase_count, %object
	.size	g_totle_mlc_erase_count, 4
g_totle_mlc_erase_count:
	.space	4
	.type	g_totle_slc_erase_count, %object
	.size	g_totle_slc_erase_count, 4
g_totle_slc_erase_count:
	.space	4
	.type	g_totle_avg_erase_count, %object
	.size	g_totle_avg_erase_count, 4
g_totle_avg_erase_count:
	.space	4
	.type	g_max_erase_count, %object
	.size	g_max_erase_count, 4
g_max_erase_count:
	.space	4
	.type	g_min_erase_count, %object
	.size	g_min_erase_count, 4
g_min_erase_count:
	.space	4
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.space	2
	.space	2
	.type	gSysInfo, %object
	.size	gSysInfo, 16
gSysInfo:
	.space	16
	.type	g_gc_superblock, %object
	.size	g_gc_superblock, 48
g_gc_superblock:
	.space	48
	.type	g_all_blk_used_slc_mode, %object
	.size	g_all_blk_used_slc_mode, 4
g_all_blk_used_slc_mode:
	.space	4
	.type	g_sys_ext_data, %object
	.size	g_sys_ext_data, 512
g_sys_ext_data:
	.space	512
	.type	g_gc_free_blk_threshold, %object
	.size	g_gc_free_blk_threshold, 2
g_gc_free_blk_threshold:
	.space	2
	.type	g_gc_merge_free_blk_threshold, %object
	.size	g_gc_merge_free_blk_threshold, 2
g_gc_merge_free_blk_threshold:
	.space	2
	.type	g_gc_skip_write_count, %object
	.size	g_gc_skip_write_count, 4
g_gc_skip_write_count:
	.space	4
	.type	g_gc_blk_index, %object
	.size	g_gc_blk_index, 2
g_gc_blk_index:
	.space	2
	.space	2
	.type	g_in_swl_replace, %object
	.size	g_in_swl_replace, 4
g_in_swl_replace:
	.space	4
	.type	g_gc_num_req, %object
	.size	g_gc_num_req, 4
g_gc_num_req:
	.space	4
	.type	gp_gc_page_buf_info, %object
	.size	gp_gc_page_buf_info, 4
gp_gc_page_buf_info:
	.space	4
	.type	p_gc_data_buf, %object
	.size	p_gc_data_buf, 4
p_gc_data_buf:
	.space	4
	.type	p_gc_spare_buf, %object
	.size	p_gc_spare_buf, 4
p_gc_spare_buf:
	.space	4
	.type	req_gc, %object
	.size	req_gc, 4
req_gc:
	.space	4
	.type	c_gc_page_buf_num, %object
	.size	c_gc_page_buf_num, 4
c_gc_page_buf_num:
	.space	4
	.type	p_gc_blk_tbl, %object
	.size	p_gc_blk_tbl, 4
p_gc_blk_tbl:
	.space	4
	.type	g_gc_blk_num, %object
	.size	g_gc_blk_num, 2
g_gc_blk_num:
	.space	2
	.space	2
	.type	p_gc_page_info, %object
	.size	p_gc_page_info, 4
p_gc_page_info:
	.space	4
	.type	g_gc_page_offset, %object
	.size	g_gc_page_offset, 2
g_gc_page_offset:
	.space	2
	.type	g_gc_next_blk, %object
	.size	g_gc_next_blk, 2
g_gc_next_blk:
	.space	2
	.type	g_gc_next_blk_1, %object
	.size	g_gc_next_blk_1, 2
g_gc_next_blk_1:
	.space	2
	.type	g_gc_next_blk_2, %object
	.size	g_gc_next_blk_2, 2
g_gc_next_blk_2:
	.space	2
	.type	g_gc_next_blk_3, %object
	.size	g_gc_next_blk_3, 2
g_gc_next_blk_3:
	.space	2
	.type	g_gc_bad_block_temp_num, %object
	.size	g_gc_bad_block_temp_num, 2
g_gc_bad_block_temp_num:
	.space	2
	.type	g_gc_bad_block_temp_tbl, %object
	.size	g_gc_bad_block_temp_tbl, 34
g_gc_bad_block_temp_tbl:
	.space	34
	.type	g_gc_bad_block_gc_index, %object
	.size	g_gc_bad_block_gc_index, 2
g_gc_bad_block_gc_index:
	.space	2
	.type	mlcPageToSlcPageTbl, %object
	.size	mlcPageToSlcPageTbl, 2048
mlcPageToSlcPageTbl:
	.space	2048
	.type	gNandMaxChip, %object
	.size	gNandMaxChip, 1
gNandMaxChip:
	.space	1
	.space	1
	.type	gTotleBlock, %object
	.size	gTotleBlock, 2
gTotleBlock:
	.space	2
	.type	g_free_slc_blk_num, %object
	.size	g_free_slc_blk_num, 2
g_free_slc_blk_num:
	.space	2
	.space	2
	.type	g_SlcPartLbaEndSector, %object
	.size	g_SlcPartLbaEndSector, 4
g_SlcPartLbaEndSector:
	.space	4
	.type	g_in_gc_progress, %object
	.size	g_in_gc_progress, 4
g_in_gc_progress:
	.space	4
	.type	g_gc_head_data_block, %object
	.size	g_gc_head_data_block, 4
g_gc_head_data_block:
	.space	4
	.type	g_gc_head_data_block_count, %object
	.size	g_gc_head_data_block_count, 4
g_gc_head_data_block_count:
	.space	4
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
	.space	4
	.type	g_gc_refresh_block_temp_num, %object
	.size	g_gc_refresh_block_temp_num, 2
g_gc_refresh_block_temp_num:
	.space	2
	.space	2
	.type	c_wr_page_buf_num, %object
	.size	c_wr_page_buf_num, 4
c_wr_page_buf_num:
	.space	4
	.type	req_read, %object
	.size	req_read, 4
req_read:
	.space	4
	.type	req_gc_dst, %object
	.size	req_gc_dst, 4
req_gc_dst:
	.space	4
	.type	req_prgm, %object
	.size	req_prgm, 4
req_prgm:
	.space	4
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 4
p_sys_data_buf:
	.space	4
	.type	p_sys_data_buf_1, %object
	.size	p_sys_data_buf_1, 4
p_sys_data_buf_1:
	.space	4
	.type	p_vendor_data_buf, %object
	.size	p_vendor_data_buf, 4
p_vendor_data_buf:
	.space	4
	.type	p_wr_io_data_buf, %object
	.size	p_wr_io_data_buf, 4
p_wr_io_data_buf:
	.space	4
	.type	p_io_data_buf_0, %object
	.size	p_io_data_buf_0, 4
p_io_data_buf_0:
	.space	4
	.type	p_io_data_buf_1, %object
	.size	p_io_data_buf_1, 4
p_io_data_buf_1:
	.space	4
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 4
p_sys_spare_buf:
	.space	4
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 4
p_io_spare_buf:
	.space	4
	.type	p_wr_io_spare_buf, %object
	.size	p_wr_io_spare_buf, 4
p_wr_io_spare_buf:
	.space	4
	.type	g_ect_tbl_info_size, %object
	.size	g_ect_tbl_info_size, 2
g_ect_tbl_info_size:
	.space	2
	.space	2
	.type	p_swl_mul_table, %object
	.size	p_swl_mul_table, 4
p_swl_mul_table:
	.space	4
	.type	gp_ect_tbl_info, %object
	.size	gp_ect_tbl_info, 4
gp_ect_tbl_info:
	.space	4
	.type	p_valid_page_count_check_table, %object
	.size	p_valid_page_count_check_table, 4
p_valid_page_count_check_table:
	.space	4
	.type	p_map_block_table, %object
	.size	p_map_block_table, 4
p_map_block_table:
	.space	4
	.type	p_map_block_valid_page_count, %object
	.size	p_map_block_valid_page_count, 4
p_map_block_valid_page_count:
	.space	4
	.type	p_vendor_block_valid_page_count, %object
	.size	p_vendor_block_valid_page_count, 4
p_vendor_block_valid_page_count:
	.space	4
	.type	p_vendor_block_ver_table, %object
	.size	p_vendor_block_ver_table, 4
p_vendor_block_ver_table:
	.space	4
	.type	p_vendor_region_ppn_table, %object
	.size	p_vendor_region_ppn_table, 4
p_vendor_region_ppn_table:
	.space	4
	.type	p_map_region_ppn_table, %object
	.size	p_map_region_ppn_table, 4
p_map_region_ppn_table:
	.space	4
	.type	p_map_region_ppn_check_table, %object
	.size	p_map_region_ppn_check_table, 4
p_map_region_ppn_check_table:
	.space	4
	.type	p_map_block_ver_table, %object
	.size	p_map_block_ver_table, 4
p_map_block_ver_table:
	.space	4
	.type	p_l2p_map_buf, %object
	.size	p_l2p_map_buf, 4
p_l2p_map_buf:
	.space	4
	.type	c_ftl_nand_bbm_buf_size, %object
	.size	c_ftl_nand_bbm_buf_size, 2
c_ftl_nand_bbm_buf_size:
	.space	2
	.space	2
	.type	gL2pMapInfo, %object
	.size	gL2pMapInfo, 44
gL2pMapInfo:
	.space	44
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.space	2
	.space	2
	.type	g_req_cache, %object
	.size	g_req_cache, 4
g_req_cache:
	.space	4
	.type	g_tmp_data_superblock_id, %object
	.size	g_tmp_data_superblock_id, 2
g_tmp_data_superblock_id:
	.space	2
	.space	2
	.type	g_totle_swl_count, %object
	.size	g_totle_swl_count, 4
g_totle_swl_count:
	.space	4
	.type	ftl_gc_temp_power_lost_recovery_flag, %object
	.size	ftl_gc_temp_power_lost_recovery_flag, 4
ftl_gc_temp_power_lost_recovery_flag:
	.space	4
	.type	g_recovery_page_min_ver, %object
	.size	g_recovery_page_min_ver, 4
g_recovery_page_min_ver:
	.space	4
	.type	gNandIDataBuf, %object
	.size	gNandIDataBuf, 2048
gNandIDataBuf:
	.space	2048
	.type	RK29_NANDC_REG_BASE, %object
	.size	RK29_NANDC_REG_BASE, 4
RK29_NANDC_REG_BASE:
	.space	4
	.type	gFlashPageBuffer0, %object
	.size	gFlashPageBuffer0, 4
gFlashPageBuffer0:
	.space	4
	.type	FlashDdrTunningReadCount, %object
	.size	FlashDdrTunningReadCount, 4
FlashDdrTunningReadCount:
	.space	4
	.type	gpReadRetrial, %object
	.size	gpReadRetrial, 4
gpReadRetrial:
	.space	4
	.type	gpFlashSaveInfo, %object
	.size	gpFlashSaveInfo, 4
gpFlashSaveInfo:
	.space	4
	.type	gNandFlashInfoBlockAddr, %object
	.size	gNandFlashInfoBlockAddr, 4
gNandFlashInfoBlockAddr:
	.space	4
	.type	gNandFlashIdbBlockAddr, %object
	.size	gNandFlashIdbBlockAddr, 4
gNandFlashIdbBlockAddr:
	.space	4
	.type	gNandIDBResBlkNumSaveInFlash, %object
	.size	gNandIDBResBlkNumSaveInFlash, 1
gNandIDBResBlkNumSaveInFlash:
	.space	1
	.type	g_maxRetryCount, %object
	.size	g_maxRetryCount, 1
g_maxRetryCount:
	.space	1
	.type	gNandFlashIDBEccBits, %object
	.size	gNandFlashIDBEccBits, 1
gNandFlashIDBEccBits:
	.space	1
	.space	1
	.type	gFlashPageBuffer1, %object
	.size	gFlashPageBuffer1, 4
gFlashPageBuffer1:
	.space	4
	.type	gFlashSpareBuffer, %object
	.size	gFlashSpareBuffer, 4
gFlashSpareBuffer:
	.space	4
	.type	gFlashProgCheckBuffer, %object
	.size	gFlashProgCheckBuffer, 4
gFlashProgCheckBuffer:
	.space	4
	.type	gFlashProgCheckSpareBuffer, %object
	.size	gFlashProgCheckSpareBuffer, 4
gFlashProgCheckSpareBuffer:
	.space	4
	.type	g_nand_ecc_en, %object
	.size	g_nand_ecc_en, 1
g_nand_ecc_en:
	.space	1
	.type	gMultiPageReadEn, %object
	.size	gMultiPageReadEn, 1
gMultiPageReadEn:
	.space	1
	.space	2
	.type	FbbtBlk, %object
	.size	FbbtBlk, 16
FbbtBlk:
	.space	16
	.type	req_sys, %object
	.size	req_sys, 36
req_sys:
	.space	36
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.space	4
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 44
gVendorBlkInfo:
	.space	44
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.space	2
	.space	2
	.type	check_valid_page_count_table, %object
	.size	check_valid_page_count_table, 8192
check_valid_page_count_table:
	.space	8192
	.type	g_power_lost_ecc_error_blk, %object
	.size	g_power_lost_ecc_error_blk, 2
g_power_lost_ecc_error_blk:
	.space	2
	.type	g_power_lost_recovery_flag, %object
	.size	g_power_lost_recovery_flag, 2
g_power_lost_recovery_flag:
	.space	2
	.type	g_recovery_page_num, %object
	.size	g_recovery_page_num, 4
g_recovery_page_num:
	.space	4
	.type	g_recovery_ppa_tbl, %object
	.size	g_recovery_ppa_tbl, 128
g_recovery_ppa_tbl:
	.space	128
	.type	gc_ink_free_return_value, %object
	.size	gc_ink_free_return_value, 2
gc_ink_free_return_value:
	.space	2
	.type	g_gc_cur_blk_valid_pages, %object
	.size	g_gc_cur_blk_valid_pages, 2
g_gc_cur_blk_valid_pages:
	.space	2
	.type	g_gc_cur_blk_max_valid_pages, %object
	.size	g_gc_cur_blk_max_valid_pages, 2
g_gc_cur_blk_max_valid_pages:
	.space	2
	.space	2
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 4
gp_last_act_superblock:
	.space	4
	.type	gc_discard_updated, %object
	.size	gc_discard_updated, 4
gc_discard_updated:
	.space	4
	.type	g_LowFormat, %object
	.size	g_LowFormat, 4
g_LowFormat:
	.space	4
	.type	gLoaderBootInfo, %object
	.size	gLoaderBootInfo, 4
gLoaderBootInfo:
	.space	4
	.type	RK29_NANDC1_REG_BASE, %object
	.size	RK29_NANDC1_REG_BASE, 4
RK29_NANDC1_REG_BASE:
	.space	4
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.space	4
	.type	last_cache_match_count, %object
	.size	last_cache_match_count, 4
last_cache_match_count:
	.space	4
	.type	idb_write_enable, %object
	.size	idb_write_enable, 1
idb_write_enable:
	.space	1
	.space	3
	.type	idb_buf, %object
	.size	idb_buf, 4
idb_buf:
	.space	4
	.type	idb_last_lba, %object
	.size	idb_last_lba, 4
idb_last_lba:
	.space	4
	.type	gpDrmKeyInfo, %object
	.size	gpDrmKeyInfo, 4
gpDrmKeyInfo:
	.space	4
	.type	SecureBootCheckOK, %object
	.size	SecureBootCheckOK, 4
SecureBootCheckOK:
	.space	4
	.type	SecureBootEn, %object
	.size	SecureBootEn, 4
SecureBootEn:
	.space	4
	.type	gpBootConfig, %object
	.size	gpBootConfig, 4
gpBootConfig:
	.space	4
	.type	gSnSectorData, %object
	.size	gSnSectorData, 512
gSnSectorData:
	.space	512
	.type	SecureBootUnlockTryCount, %object
	.size	SecureBootUnlockTryCount, 4
SecureBootUnlockTryCount:
	.space	4
	.type	gpVendor0Info, %object
	.size	gpVendor0Info, 4
gpVendor0Info:
	.space	4
	.type	gpVendor1Info, %object
	.size	gpVendor1Info, 4
gpVendor1Info:
	.space	4
	.type	g_idb_buffer, %object
	.size	g_idb_buffer, 4
g_idb_buffer:
	.space	4
	.type	g_vendor, %object
	.size	g_vendor, 4
g_vendor:
	.space	4
	.type	g_gc_refresh_block_temp_tbl, %object
	.size	g_gc_refresh_block_temp_tbl, 34
g_gc_refresh_block_temp_tbl:
	.space	34
	.space	2
	.type	gToggleModeClkDiv, %object
	.size	gToggleModeClkDiv, 4
gToggleModeClkDiv:
	.space	4
	.type	gpNandc1, %object
	.size	gpNandc1, 4
gpNandc1:
	.space	4
	.type	gNandFlashResEndPageAddr, %object
	.size	gNandFlashResEndPageAddr, 4
gNandFlashResEndPageAddr:
	.space	4
	.type	gNandFlashInfoBlockEcc, %object
	.size	gNandFlashInfoBlockEcc, 1
gNandFlashInfoBlockEcc:
	.space	1
	.type	gFlashOnfiModeEn, %object
	.size	gFlashOnfiModeEn, 1
gFlashOnfiModeEn:
	.space	1
	.type	gFlashSdrModeEn, %object
	.size	gFlashSdrModeEn, 1
gFlashSdrModeEn:
	.space	1
