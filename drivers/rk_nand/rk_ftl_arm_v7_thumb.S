/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-02-18
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
	push	{r4, lr}
	.save {r4, lr}
	lsrs	r2, r1, #16
	ldr	r4, .L46
	add	r4, r4, r0, lsl #3
	ldr	r3, [r4, #12]
	ldrb	r4, [r4, #16]	@ zero_extendqisi2
	add	r3, r3, r4, lsl #8
	movs	r4, #0
	str	r4, [r3, #2056]
	str	r4, [r3, #2052]
	str	r4, [r3, #2052]
	uxtb	r4, r1
	str	r4, [r3, #2052]
	lsrs	r4, r1, #8
	str	r4, [r3, #2052]
	str	r2, [r3, #2052]
	movs	r2, #48
	str	r2, [r3, #2056]
	pop	{r4, lr}
	b	FlashSetRandomizer
.L47:
	.align	2
.L46:
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
	ldr	r4, .L52
	lsrs	r5, r1, #8
	add	r3, r4, r0, lsl #3
	ldrb	r4, [r4, #60]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r4, #1
	lsr	r4, r1, #16
	lsl	r3, r3, #8
	add	r3, r3, r2
	bne	.L49
	movs	r2, #6
	str	r2, [r3, #2056]
	movs	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r6, [r3, #2052]
	str	r5, [r3, #2052]
	str	r4, [r3, #2052]
	b	.L51
.L49:
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
.L51:
	movs	r2, #224
	str	r2, [r3, #2056]
	pop	{r4, r5, r6, lr}
	b	FlashSetRandomizer
.L53:
	.align	2
.L52:
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
	ldr	r4, .L55
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
.L56:
	.align	2
.L55:
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
	ldr	r5, .L62
	add	r0, r5, r0, lsl #3
	ldrb	r3, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	lsls	r3, r3, #8
	cbz	r2, .L58
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
.L58:
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
.L63:
	.align	2
.L62:
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
	ldr	r4, .L65
	add	r5, r4, r0, lsl #3
	ldrb	r4, [r4, #55]	@ zero_extendqisi2
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
.L66:
	.align	2
.L65:
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
	ldr	r3, .L68
	add	r0, r3, r0, lsl #3
	ldr	r3, .L68+4
	ldr	r4, [r0, #12]
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r3, #4]
	ldr	r0, .L68+8
	blx	r3
	add	r4, r4, r5, lsl #8
	movs	r3, #16
	str	r3, [r4, #2056]
	pop	{r3, r4, r5, pc}
.L69:
	.align	2
.L68:
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
	ldr	r2, .L71
	add	r0, r2, r0, lsl #3
	ldrb	r2, [r2, #54]	@ zero_extendqisi2
	ldrb	r1, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r1, lsl #8
	str	r2, [r3, #2056]
	bx	lr
.L72:
	.align	2
.L71:
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
	ldr	r3, .L77
	add	r1, r1, r0
	push	{r4, lr}
	.save {r4, lr}
.L74:
	cmp	r0, r1
	beq	.L76
	lsls	r2, r3, #5
	ldrb	r4, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, r4
	eors	r3, r3, r2
	b	.L74
.L76:
	mov	r0, r3
	pop	{r4, pc}
.L78:
	.align	2
.L77:
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
	ldr	r3, .L82
	ldr	r2, [r3, #76]
	ldr	r1, [r2]
	str	r1, [r3, #80]
	ldr	r1, [r2, #4]
	str	r1, [r3, #84]
	ldr	r1, [r2, #8]
	str	r1, [r3, #88]
	ldr	r1, [r2, #12]
	str	r1, [r3, #92]
	ldr	r1, [r2, #304]
	str	r1, [r3, #96]
	ldr	r1, [r2, #308]
	str	r1, [r3, #100]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #104]
	str	r2, [r3, #108]
	bx	lr
.L83:
	.align	2
.L82:
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
	ldr	r0, .L89
	mov	r8, r3
	ldr	r4, [r7, #4]
	mov	r10, r1
	mov	r9, r2
	ldrh	r3, [r0, #126]
	bic	r4, r4, #-2147483648
	ldrh	r5, [r0, #124]
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
	bne	.L86
	ldrb	r2, [r6, #140]	@ zero_extendqisi2
	cbnz	r2, .L86
	ldr	r2, .L89
	add	r1, r2, r1, lsl #1
	ldrh	r1, [r1, #144]
.L86:
	add	r6, r6, r0, lsl #2
	ldr	r2, [r6, #1168]
	mla	fp, fp, r3, r2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	cmp	r3, #1
	add	r1, r1, fp
	str	r1, [r9]
	str	r0, [r8]
	bls	.L88
	ldr	r0, [r7, #4]
	ldr	r3, [r7, #40]
	add	r0, r0, #1024
	subs	r3, r0, r3
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	b	.L87
.L88:
	movs	r0, #0
.L87:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L90:
	.align	2
.L89:
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
	ldr	r3, .L92
	ldr	r2, [r3, #1200]
	str	r0, [r3, #1200]
	mov	r0, r2
	bx	lr
.L93:
	.align	2
.L92:
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
	ldr	r3, .L95
	ldr	r3, [r3, #1204]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L96:
	.align	2
.L95:
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
	ldr	r3, .L99
	add	r0, r3, r0, lsl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	adds	r2, r2, #8
	add	r0, r0, r2, lsl #8
	bx	lr
.L100:
	.align	2
.L99:
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
	ldr	r3, .L102
	ldr	r2, [r3, #76]
	lsls	r3, r0, #8
	orr	r0, r3, r0, lsl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L103:
	.align	2
.L102:
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
	ldr	r3, .L105
	orr	r0, r0, #16640
	ldr	r3, [r3, #76]
	str	r0, [r3, #344]
	bx	lr
.L106:
	.align	2
.L105:
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
	ldr	r3, .L110
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	cbnz	r0, .L108
	bfi	r3, r0, #13, #1
	b	.L109
.L108:
	orr	r3, r3, #253952
.L109:
	str	r3, [r2]
	bx	lr
.L111:
	.align	2
.L110:
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
	ldr	r3, .L119
	ands	r1, r0, #6
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	beq	.L113
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
	b	.L115
.L113:
	bfi	r3, r1, #13, #1
.L115:
	str	r3, [r2]
	movs	r0, #0
	bx	lr
.L120:
	.align	2
.L119:
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
	ldr	r2, .L122
	add	r0, r2, r0, lsl #3
	movs	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	lsls	r2, r2, r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L123:
	.align	2
.L122:
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
	ldr	r3, .L125
	add	r0, r3, r0, lsl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L126:
	.align	2
.L125:
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
	ldr	r3, .L128
	umull	r0, r1, r0, r3
	ldr	r3, .L128+4
	ldr	r3, [r3, #8]
	lsrs	r0, r1, #6
	blx	r3
	movs	r0, #0
	pop	{r3, pc}
.L129:
	.align	2
.L128:
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
	ldr	r3, .L131
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
.L132:
	.align	2
.L131:
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
	ldr	r9, .L142+8
	movs	r4, #0
	ldr	r7, .L142
	mov	r5, r0
	add	r10, r9, #256
	mov	r6, r1
.L134:
	ldrb	r3, [r7, #1209]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L141
	movs	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	movs	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldrb	r3, [r7, #1208]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L135
	add	r3, r4, r8
	add	r3, r3, r10
	b	.L140
.L135:
	cmp	r3, #35
	bne	.L137
	ldr	r2, .L142+4
	add	r3, r4, r8
	add	r3, r3, r2
.L140:
	ldrsb	r3, [r3, #5]
	b	.L139
.L137:
	add	r3, r9, r6
	ldrsb	r3, [r3, #400]
.L139:
	str	r3, [r5]
	adds	r4, r4, #1
	b	.L134
.L141:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L143:
	.align	2
.L142:
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
	ldr	r3, .L148
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, lsl #2
	movs	r4, #0
	ldr	r8, .L148+4
	adds	r5, r1, #3
	mov	r9, #161
	mov	r6, r0
	mov	r7, r3
	mov	r10, r4
.L145:
	ldrb	r3, [r8, #1209]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L147
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	adds	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L145
.L147:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L149:
	.align	2
.L148:
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
	ldr	r5, .L158
	mov	r6, r0
	mov	r9, r1
	mov	fp, r2
	ldr	r3, [r5, #1204]
	ldrb	r4, [r3, #19]	@ zero_extendqisi2
	lsls	r3, r0, #3
	cmp	r4, #6
	bne	.L151
	add	r4, r5, r0, lsl #6
	addw	r4, r4, #1230
	add	r4, r4, r7, lsl #2
	b	.L152
.L151:
	cmp	r4, #7
	bne	.L153
	mov	r10, #160
	add	r4, r7, r7, lsl #2
	mla	r10, r10, r0, r5
	addw	r10, r10, #1238
	add	r4, r10, r4, lsl #1
	b	.L152
.L153:
	cmp	r4, #8
	iteet	ne
	addne	r4, r7, r3
	addweq	r4, r5, #1238
	addeq	r2, r7, r7, lsl #2
	addne	r4, r5, r4, lsl #3
	ite	eq
	addeq	r4, r4, r2
	addwne	r4, r4, #1230
.L152:
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
.L155:
	cmp	r1, r9
	beq	.L157
	ldrb	r3, [r1, #1]!	@ zero_extendqisi2
	movs	r0, #200
	str	r3, [fp, #2052]
	str	r1, [sp, #4]
	bl	NandcDelayns
	ldrsb	r3, [r4, #1]!
	str	r3, [fp, #2048]
	ldr	r1, [sp, #4]
	b	.L155
.L157:
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
.L159:
	.align	2
.L158:
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
	ldr	r3, .L166
	ldr	r2, [r3, #1204]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	subs	r2, r2, #1
	cmp	r2, #7
	bhi	.L160
	movs	r4, #0
	mov	r5, r3
.L161:
	ldr	r2, .L166+4
	uxtb	r0, r4
	ldrb	r3, [r2, r4, lsl #3]	@ zero_extendqisi2
	cmp	r3, #173
	bne	.L162
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	subw	r2, r2, #854
	movs	r3, #0
	bl	HynixSetRRPara
.L162:
	adds	r4, r4, #1
	cmp	r4, #4
	bne	.L161
.L160:
	pop	{r3, r4, r5, pc}
.L167:
	.align	2
.L166:
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
	ldr	r5, .L178
	add	r0, r5, r0, lsl #3
	ldr	r3, [r5, #1204]
	ldrb	r4, [r0, #16]	@ zero_extendqisi2
	ldr	r6, [r0, #12]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	lsl	r3, r4, #8
	add	r4, r4, #8
	bne	.L169
	cbnz	r2, .L170
	ldrb	r2, [r5, #57]	@ zero_extendqisi2
	b	.L177
.L170:
	ldrb	r2, [r5, #58]	@ zero_extendqisi2
.L177:
	add	r3, r3, r6
	str	r2, [r3, #2056]
	ldrb	r0, [r5, #59]	@ zero_extendqisi2
	cbz	r0, .L173
	movs	r3, #0
	add	r5, r6, r4, lsl #8
.L172:
	cmp	r3, r0
	bcs	.L173
	lsls	r2, r3, #3
	adds	r3, r3, #1
	lsr	r2, r1, r2
	uxtb	r2, r2
	str	r2, [r5, #4]
	b	.L172
.L169:
	add	r3, r3, r6
	movs	r2, #112
	str	r2, [r3, #2056]
.L173:
	movs	r0, #80
	lsls	r4, r4, #8
	bl	NandcDelayns
	ldr	r0, [r6, r4]
	uxtb	r0, r0
	pop	{r4, r5, r6, pc}
.L179:
	.align	2
.L178:
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
.L184:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	mov	r3, r0
	beq	.L184
	lsls	r3, r3, #25
	bpl	.L184
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
	ldr	r2, [r5, #1168]
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
	movs	r0, #150
	bl	NandcDelayns
.L208:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #22
	bmi	.L209
	movs	r0, #10
	bl	NandcDelayns
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
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L218
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cbz	r3, .L216
	add	r4, r4, r5, lsl #3
	bl	NandcFlashCs
	movs	r0, #100
	ldr	r3, [r4, #12]
	ldrb	r4, [r4, #16]	@ zero_extendqisi2
	add	r4, r3, r4, lsl #8
	movs	r3, #239
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
	mov	r0, r5
	bl	NandcWaitFlashReadyNoDelay
	movs	r3, #218
	mov	r0, r5
	str	r3, [r4, #2056]
	pop	{r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L216:
	pop	{r3, r4, r5, pc}
.L219:
	.align	2
.L218:
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
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L222
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cbz	r3, .L220
	add	r4, r4, r5, lsl #3
	bl	NandcFlashCs
	movs	r0, #100
	ldr	r3, [r4, #12]
	ldrb	r4, [r4, #16]	@ zero_extendqisi2
	add	r4, r3, r4, lsl #8
	movs	r3, #239
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
	mov	r0, r5
	bl	NandcWaitFlashReadyNoDelay
	movs	r3, #223
	mov	r0, r5
	str	r3, [r4, #2056]
	pop	{r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L220:
	pop	{r3, r4, r5, pc}
.L223:
	.align	2
.L222:
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
	ldr	r5, .L253
	movs	r4, #0
	movs	r7, #239
	mov	lr, #128
	mov	ip, #1
	mov	r8, #35
	ldrb	r3, [r5, #2228]	@ zero_extendqisi2
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
.L235:
	ldr	r1, .L253+4
	ldr	r3, [sp, #4]
	ldrb	r1, [r4, r1]	@ zero_extendqisi2
	ldr	r6, [r4, r3]
	add	r3, r3, r4
	cmp	r1, #152
	ldr	r3, [r3, #4]
	beq	.L226
	cmp	r1, #69
	beq	.L226
	cmp	r1, #173
	beq	.L226
	cmp	r1, #44
	bne	.L227
.L226:
	cmp	r0, #1
	uxtb	r3, r3
	bne	.L228
	ldr	r5, [sp, #12]
	cbz	r5, .L227
	lsls	r3, r3, #8
	cmp	r1, #173
	add	fp, r6, r3
	str	r7, [fp, #2056]
	bne	.L229
	str	r0, [fp, #2052]
	b	.L252
.L229:
	cmp	r1, #44
	itete	eq
	streq	r0, [fp, #2052]
	strne	lr, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L233
.L228:
	ldr	r5, [sp, #8]
	cbz	r5, .L227
	lsls	r3, r3, #8
	cmp	r1, #173
	add	fp, r6, r3
	str	r7, [fp, #2056]
	bne	.L232
	str	ip, [fp, #2052]
	str	r9, [fp, #2048]
	b	.L233
.L232:
	cmp	r1, #44
	bne	.L234
	str	ip, [fp, #2052]
	str	r8, [fp, #2048]
	b	.L233
.L234:
	str	lr, [fp, #2052]
.L252:
	str	r2, [fp, #2048]
.L233:
	add	r3, r3, r6
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L227:
	adds	r4, r4, #8
	cmp	r4, #32
	bne	.L235
	movs	r0, #0
	bl	NandcWaitFlashReady
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L254:
	.align	2
.L253:
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
	ldr	r5, .L256
	ldr	r3, .L256+4
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
.L257:
	.align	2
.L256:
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
	ldr	r2, .L259
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
.L260:
	.align	2
.L259:
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
	ldr	r0, .L268
	ldr	r1, .L268+4
	add	r4, r4, r4, lsl #2
	movs	r2, #0
	sub	r6, r0, #48
.L262:
	ldrb	r3, [r1, #1209]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L267
	ldrb	r3, [r1, #1208]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	ite	eq
	addeq	r3, r3, r6
	addne	r3, r3, r0
	adds	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L262
.L267:
	movs	r0, #0
	pop	{r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L269:
	.align	2
.L268:
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
	ldr	r3, .L271
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
.L272:
	.align	2
.L271:
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
	ldr	r8, .L284+4
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
.L274:
	cmp	r7, r9
	beq	.L283
	rsb	r3, r7, r9
	add	r2, sp, #8
	adds	r0, r6, r5
	movs	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r3, r2
	bcc	.L275
	mov	r3, #-1
	str	r3, [r6, r5]
	b	.L276
.L275:
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
	bne	.L278
	ldr	r0, .L284
	ldr	r1, [sp, #8]
	bl	printk
.L278:
	mov	r0, r4
	bl	NandcFlashDeCs
.L276:
	adds	r7, r7, #1
	adds	r5, r5, #36
	b	.L274
.L283:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L285:
	.align	2
.L284:
	.word	.LC1
	.word	.LANCHOR0
	.fnend
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	1
	.global	FlashReadDpCmd
	.thumb
	.thumb_func
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L290
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, r2
	add	r2, r3, r0, lsl #3
	ldrb	r6, [r3, #52]	@ zero_extendqisi2
	mov	r7, r1
	uxtb	lr, r1
	ldrb	r4, [r2, #16]	@ zero_extendqisi2
	uxtb	r10, r5
	ldr	ip, [r2, #12]
	lsr	r9, r5, #8
	ldrb	r2, [r3, #60]	@ zero_extendqisi2
	mov	r8, r0
	lsls	r4, r4, #8
	ldrb	r3, [r3, #53]	@ zero_extendqisi2
	cmp	r2, #1
	add	r4, r4, ip
	lsr	r2, r1, #8
	lsr	r5, r5, #16
	lsr	r1, r1, #16
	str	r6, [r4, #2056]
	bne	.L287
	movs	r6, #0
	str	r6, [r4, #2052]
	str	r6, [r4, #2052]
	str	lr, [r4, #2052]
	str	r2, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	str	r6, [r4, #2056]
	str	r6, [r4, #2052]
	str	r6, [r4, #2052]
	b	.L289
.L287:
	str	lr, [r4, #2052]
	str	r2, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
.L289:
	str	r10, [r4, #2052]
	movs	r3, #48
	str	r9, [r4, #2052]
	mov	r0, r8
	str	r5, [r4, #2052]
	mov	r1, r7
	str	r3, [r4, #2056]
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L291:
	.align	2
.L290:
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
	ldr	r4, .L302
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r0, [r4, #2240]
	cbz	r0, .L293
	movs	r0, #0
	bl	flash_enter_slc_mode
	b	.L294
.L293:
	bl	flash_exit_slc_mode
.L294:
	ldrb	r3, [r4, #2244]	@ zero_extendqisi2
	ldr	r5, .L302
	cbz	r3, .L295
	ldrb	r3, [r5, #2228]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L295
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	movs	r3, #0
	strb	r3, [r5, #2244]
.L295:
	ldr	r3, [r4, #12]
	movs	r0, #0
	str	r0, [r3, #336]
	pop	{r3, r4, r5, pc}
.L303:
	.align	2
.L302:
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
	ldr	r3, .L305
	add	r0, r3, r0, lsl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L306:
	.align	2
.L305:
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
	ldr	r1, .L317
	bl	__aeabi_idiv
	ldr	r3, .L317+4
	ldr	r3, [r3, #76]
	cmp	r0, #250
	ble	.L308
	movw	r2, #8354
	b	.L315
.L308:
	cmp	r0, #220
	bgt	.L316
	cmp	r0, #185
	ble	.L311
	movw	r2, #4226
	b	.L315
.L311:
	cmp	r0, #160
	ble	.L312
	movw	r2, #4194
	b	.L315
.L312:
	cmp	r4, #35
	bhi	.L313
	movw	r2, #4193
	b	.L315
.L313:
	cmp	r4, #99
	bls	.L314
.L316:
	movw	r2, #8322
	b	.L315
.L314:
	movw	r2, #4225
.L315:
	str	r2, [r3, #4]
	pop	{r4, pc}
.L318:
	.align	2
.L317:
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
	bls	.L320
	sub	r3, r0, #4224
	subs	r3, r3, #1
	cmp	r3, #1
	bls	.L320
	movw	r3, #8322
	cmp	r0, r3
	bne	.L321
.L320:
	ldr	r3, .L322
	ldr	r3, [r3, #76]
	str	r0, [r3, #4]
.L321:
	ldr	r3, .L322+4
	ldrb	r0, [r3, #493]	@ zero_extendqisi2
	b	NandcTimeCfg
.L323:
	.align	2
.L322:
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
	ldr	r3, .L326
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
	str	r0, [r3, #76]
	str	r2, [r3, #40]
	ldr	r2, [r0]
	and	r2, r2, #253952
	ubfx	r4, r2, #13, #1
	bfi	r2, r1, #13, #1
	ldr	r1, [r0, #352]
	orr	r2, r2, #256
	str	r4, [r3, #2248]
	movw	r4, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r3, #2252]
	ldr	r1, [r0, #352]
	cmp	r1, r4
	mov	r4, r3
	str	r1, [r3, #2256]
	itt	eq
	moveq	r3, #8
	streq	r3, [r4, #2252]
	str	r2, [r0]
	movs	r0, #40
	ldr	r3, [r4, #76]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #76]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L326+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #2288]
	str	r5, [r4, #2296]
	str	r0, [r4, #2260]
	str	r0, [r4, #2264]
	add	r0, r0, #32768
	str	r0, [r4, #2268]
	pop	{r3, r4, r5, pc}
.L327:
	.align	2
.L326:
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
	ldr	r4, .L329
	ldr	r5, [r4, #76]
	ldr	r5, [r5, #4]
	str	r5, [r0]
	ldr	r0, [r4, #76]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [r4, #76]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [r4, #76]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, lsl #16
	str	r2, [r3]
	pop	{r4, r5, pc}
.L330:
	.align	2
.L329:
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
	ldr	r3, .L339
	movs	r1, #1
	ldr	r2, [r3, #76]
	str	r0, [r3, #2300]
	movs	r3, #0
	str	r1, [r2, #8]
	movs	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L332
.L335:
	bfc	r3, #4, #1
	b	.L333
.L332:
	cmp	r0, #24
	bne	.L334
	orr	r3, r3, #16
	b	.L333
.L334:
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L335
.L333:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L340:
	.align	2
.L339:
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
	ldr	r3, .L342
	strb	r0, [r3, #2304]
	b	NandcBchSel
.L343:
	.align	2
.L342:
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
	ldr	r3, .L352
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	ldr	r2, [r3, #76]
	addw	r6, r3, #2068
	ldr	r1, [r3, #80]
	mov	r4, r3
	str	r1, [r2]
	ldr	r1, [r3, #84]
	ldr	r2, [r3, #76]
	str	r1, [r2, #4]
	ldr	r1, [r3, #88]
	str	r1, [r2, #8]
	ldr	r1, [r3, #92]
	str	r1, [r2, #12]
	ldr	r1, [r3, #96]
	str	r1, [r2, #304]
	ldr	r1, [r3, #100]
	str	r1, [r2, #308]
	ldr	r1, [r3, #104]
	str	r1, [r2, #336]
	ldr	r1, [r3, #108]
	str	r1, [r2, #344]
.L346:
	ldrb	r3, [r6, r5, lsl #3]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L345
	uxtb	r0, r5
	bl	FlashReset
.L345:
	adds	r5, r5, #1
	cmp	r5, #4
	bne	.L346
	ldrb	r3, [r4, #2244]	@ zero_extendqisi2
	ldr	r5, .L352
	cbz	r3, .L347
	movs	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #2228]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2228]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #97]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L347:
	ldr	r3, [r4, #1204]
	pop	{r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L353:
	.align	2
.L352:
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
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	bl	rk_nandc_rb_irq_flag_init
	ldr	r3, [r4, #368]
	orr	r3, r3, #2
	str	r3, [r4, #368]
	ldr	r3, [r4, #364]
	orr	r3, r3, #2
	str	r3, [r4, #364]
	ldr	r3, [r4]
	lsls	r3, r3, #22
	bmi	.L357
	mov	r0, r4
	pop	{r4, lr}
	b	wait_for_nand_flash_ready
.L357:
	ldr	r3, [r4, #368]
	orr	r3, r3, #2
	str	r3, [r4, #368]
	ldr	r3, [r4, #364]
	bic	r3, r3, #2
	str	r3, [r4, #364]
	pop	{r4, pc}
	.fnend
	.size	NandcIqrWaitFlashReady, .-NandcIqrWaitFlashReady
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
	ldr	r4, .L392
	.pad #28
	sub	sp, sp, #28
	mov	r10, r1
	mov	r8, r2
	ldrb	r5, [r4]	@ zero_extendqisi2
	cbz	r5, .L376
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L360
.L376:
	mov	fp, r4
.L359:
	cmp	r5, r8
	bcs	.L390
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
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #20]
	cmp	r0, r3
	bcc	.L361
	mov	r3, #-1
	str	r3, [r9, r6]
	b	.L362
.L361:
	ldrb	r3, [fp, #2305]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, lsl #4
	it	eq
	moveq	r7, #0
	ldr	r3, [r3, #2108]
	cbz	r3, .L364
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L364:
	ldr	r1, [sp, #20]
	ldr	r3, [sp, #12]
	lsls	r2, r1, #4
	add	lr, r4, r2
	str	r3, [lr, #2108]
	movs	r3, #0
	str	r3, [lr, #2112]
	ldr	r3, [sp, #16]
	str	r3, [lr, #2104]
	cbz	r7, .L365
	add	r3, r6, #36
	add	r3, r3, r9
	str	r3, [lr, #2112]
.L365:
	add	r1, r1, r4
	add	r2, r2, r4
	ldrb	r6, [r1, #2232]	@ zero_extendqisi2
	mov	r0, r6
	strb	r6, [r2, #2100]
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L366
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	cbz	r3, .L366
	bl	flash_enter_slc_mode
	b	.L367
.L366:
	bl	flash_exit_slc_mode
.L367:
	ldr	r3, [sp, #20]
	mov	r0, r6
	ldr	r1, [sp, #16]
	add	r5, r5, r7
	add	r3, r4, r3, lsl #2
	ldr	r2, [r3, #1168]
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
.L362:
	adds	r5, r5, #1
	b	.L359
.L390:
	ldr	r0, [r4, #76]
	movs	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r6, .L392
	addw	r7, r6, #2100
.L369:
	ldrb	r2, [r4, #2229]	@ zero_extendqisi2
	ldr	r3, .L392
	cmp	r5, r2
	bcs	.L391
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L370
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbz	r3, .L370
	lsls	r3, r5, #4
	ldrb	r0, [r7, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L370:
	adds	r5, r5, #1
	b	.L369
.L391:
	ldr	r2, [r3, #2308]
	cbnz	r2, .L372
.L374:
	movs	r0, #0
	b	.L360
.L372:
	ldrb	r3, [r3, #2068]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L374
	movs	r3, #0
	movs	r2, #36
	mov	r1, r3
.L373:
	cmp	r3, r8
	beq	.L374
	mul	r0, r2, r3
	adds	r3, r3, #1
	str	r1, [r9, r0]
	b	.L373
.L360:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L393:
	.align	2
.L392:
	.word	.LANCHOR0
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
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
	ldr	r3, .L396
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
.L397:
	.align	2
.L396:
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
.L399:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #11
	bpl	.L399
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
	cbnz	r1, .L416
	adds	r1, fp, #0
	it	ne
	movne	r1, #1
	b	.L403
.L416:
	movs	r1, #1
.L403:
	ldr	r5, .L421
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
	ldr	r3, [r5, #2252]
	cmp	r3, #3
	bls	.L404
	ldr	r3, [r7, #16]
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #4
	str	r3, [sp, #12]
	cmp	r1, #0
	beq	.L405
	cbnz	r6, .L406
.L414:
	adds	r2, r2, #1
	asrs	r2, r2, #1
	bfi	r4, r2, #22, #6
	cmp	fp, #0
	beq	.L407
	mov	r0, fp
	b	.L408
.L406:
	ldr	r3, [r5, #2300]
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
.L410:
	ldr	r1, [sp, #4]
	cmp	r0, r1
	bcs	.L414
	ldr	r1, [sp, #60]
	lsr	r10, r3, #2
	cbz	r1, .L411
	ldrh	r1, [lr, #2]
	ldrh	ip, [lr], #4
	orr	ip, ip, r1, lsl #16
	ldr	r1, [r9, #2268]
	str	ip, [r1, r10, lsl #2]
	b	.L412
.L411:
	ldr	r1, [r9, #2268]
	mov	ip, r1
	mov	r1, #-1
	str	r1, [ip, r10, lsl #2]
.L412:
	ldr	r1, [sp]
	adds	r0, r0, #1
	add	r3, r3, r1
	b	.L410
.L407:
	ldr	r0, [r5, #2264]
.L408:
	ldr	r3, [r5, #2268]
	ubfx	r9, r4, #22, #5
	mov	r2, r6
	str	r0, [r5, #2272]
	lsl	r1, r9, #10
	str	r3, [r5, #2276]
	bl	rknand_dma_map_single
	mov	r2, r6
	lsl	r1, r9, #7
	clz	r6, r6
	lsrs	r6, r6, #5
	str	r0, [r5, #2280]
	ldr	r0, [r5, #2276]
	bl	rknand_dma_map_single
	movs	r3, #1
	str	r3, [r5, #2288]
	tst	fp, #3
	ldr	r3, [r5, #2280]
	str	r0, [r5, #2284]
	str	r3, [r7, #20]
	ldr	r3, [r5, #2284]
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
.L405:
	ldr	r3, [sp, #12]
	str	r3, [r7, #16]
.L404:
	str	r8, [r7, #12]
	str	r4, [r7, #8]
	orr	r4, r4, #4
	str	r4, [r7, #8]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L422:
	.align	2
.L421:
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
.L424:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bhi	.L426
	lsls	r2, r2, #1
	b	.L424
.L426:
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
	ldr	r3, .L430
	cmp	r0, #23
	it	ls
	movls	r0, #24
	ldrh	r2, [r3, #2316]
	ldrh	r1, [r3, #2326]
	str	r0, [r3, #2312]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #2332]
	strh	r0, [r3, #2324]	@ movhi
	movs	r0, #0
	str	r2, [r3, #2320]
	subs	r2, r1, r2
	str	r2, [r3, #2328]
	bx	lr
.L431:
	.align	2
.L430:
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
	ldr	r4, .L460
	ldrh	lr, [r0, #8]
	str	r1, [sp]
	ldrh	r1, [sp]
	ldrh	r2, [r0, #10]
	ldrh	r3, [r0, #12]
	addw	r0, r4, #2344
	strh	r1, [r4, #2326]	@ movhi
	movs	r1, #0
	strh	lr, [r4, #2336]	@ movhi
	strh	r2, [r4, #2338]	@ movhi
	strh	r3, [r4, #2340]	@ movhi
.L433:
	strb	r1, [r1, r0]
	adds	r1, r1, #1
	cmp	r1, #32
	bne	.L433
	ldrh	r0, [r5, #14]
	ldrh	r1, [r5, #20]
	cmp	r1, r0, lsr #8
	bcs	.L434
	uxtb	r10, r3
	ldr	r9, .L460+4
	lsl	r1, r10, #1
	uxtb	r1, r1
	str	r1, [sp, #8]
	subs	r1, r2, #1
	muls	r1, r3, r1
	str	r1, [sp, #4]
	movs	r1, #0
.L435:
	cmp	r1, r3
	bcs	.L437
	ldr	r6, [sp, #4]
	rsb	ip, r3, r1
	uxtb	r0, r1
	add	ip, ip, r9
	adds	r6, r1, r6
	add	r6, r9, r6
	str	r6, [sp, #12]
	movs	r6, #0
	mov	r8, r6
.L438:
	cmp	r8, r2
	add	r6, r6, r3
	bcs	.L459
	ldr	r7, [sp, #12]
	add	fp, r0, r10
	strb	r0, [ip, r6]
	add	r8, r8, #1
	strb	fp, [r7, r6]
	ldr	r7, [sp, #8]
	add	r0, r0, r7
	uxtb	r0, r0
	b	.L438
.L459:
	adds	r1, r1, #1
	b	.L435
.L437:
	lsls	r2, r2, #1
	strh	r2, [r4, #2338]	@ movhi
	ldr	r2, [sp]
	lsrs	r7, r2, #1
	strh	r7, [r4, #2326]	@ movhi
.L434:
	cmp	lr, #1
	mov	r2, #5
	strh	r2, [r4, #2376]	@ movhi
	mov	r2, #0
	strh	r2, [r4, #2378]	@ movhi
	it	eq
	ldreq	r2, .L460
	ldrb	r9, [r4]	@ zero_extendqisi2
	it	eq
	strheq	lr, [r2, #2376]	@ movhi
	mov	r2, #4352
	strh	r2, [r4, #2380]	@ movhi
	cmp	r9, #0
	beq	.L440
	ldr	r2, .L460
	mov	r1, #384
	strh	r1, [r2, #2380]	@ movhi
.L440:
	ldrh	r7, [r4, #2338]
	ldrh	r6, [r4, #2326]
	ldrh	r8, [r5, #16]
	smulbb	r7, r7, r3
	ldrh	fp, [r5, #20]
	smulbb	r3, r6, r3
	ldrh	r1, [r5, #18]
	mov	r0, fp
	strh	r8, [r4, #2384]	@ movhi
	strh	fp, [r4, #2390]	@ movhi
	uxth	r7, r7
	strh	r1, [r4, #2386]	@ movhi
	str	r1, [sp]
	strh	r3, [r4, #2382]	@ movhi
	smulbb	r3, r7, r8
	strh	r7, [r4, #2316]	@ movhi
	strh	r3, [r4, #2388]	@ movhi
	bl	Ftl_log2
	lsl	r3, fp, #9
	cmp	r6, #1024
	uxth	r3, r3
	strh	r3, [r4, #2394]	@ movhi
	it	hi
	ldrhi	r2, .L460
	lsr	r3, r3, #8
	strh	r3, [r4, #2396]	@ movhi
	ldrh	r3, [r5, #26]
	strh	r3, [r4, #2398]	@ movhi
	mul	r3, r6, r7
	str	r3, [r4, #2332]
	itt	hi
	uxtbhi	r3, r6
	strhhi	r3, [r2, #2378]	@ movhi
	mov	r10, r0
	ldrh	r5, [r4, #2378]
	ldr	r1, [sp]
	subs	r5, r6, r5
	strh	r0, [r4, #2392]	@ movhi
	ldrh	r0, [r4, #2380]
	muls	r5, r7, r5
	ldr	r3, .L460
	lsls	r0, r0, #3
	mul	r1, r1, fp
	str	r3, [sp]
	mul	r5, fp, r5
	mul	r5, r8, r5
	asrs	r5, r5, #11
	str	r5, [r4, #2400]
	bl	__aeabi_idiv
	ldr	r3, [sp]
	uxth	r0, r0
	cmp	r0, #4
	itet	ls
	movls	r2, #4
	strhhi	r0, [r3, #2404]	@ movhi
	strhls	r2, [r3, #2404]	@ movhi
	cmp	r9, #0
	beq	.L444
	mov	r3, #640
	strh	r3, [r4, #2380]	@ movhi
.L444:
	ldrh	r3, [r4, #2380]
	lsls	r6, r6, #6
	mov	r1, r7
	ldr	r5, .L460
	asr	r3, r3, r10
	add	r10, r10, #9
	asr	r6, r6, r10
	adds	r3, r3, #2
	strh	r3, [r4, #2406]	@ movhi
	uxth	r0, r6
	strh	r6, [r4, #2408]	@ movhi
	add	r6, r0, #8
	mul	r3, r7, r0
	ldrh	r0, [r4, #2404]
	str	r3, [r4, #2412]
	bl	__aeabi_uidiv
	cmp	r7, #1
	uxtah	r0, r6, r0
	ldr	r6, .L460
	it	eq
	addeq	r0, r0, #4
	str	r0, [r5, #2312]
	ldrh	r0, [r4, #2312]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #2312]
	ldr	r5, [r4, #2328]
	mov	r0, #2048
	ldrh	r7, [r4, #2390]
	str	r3, [r4, #2416]
	ldrh	r3, [r4, #2384]
	lsls	r5, r5, #2
	mov	r1, r7
	muls	r5, r3, r5
	ldrh	r3, [r4, #2392]
	adds	r3, r3, #9
	lsrs	r5, r5, r3
	adds	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, #2420]	@ movhi
	bl	__aeabi_idiv
	ldrh	r2, [r4, #2404]
	movs	r3, #0
	str	r3, [r4, #2424]
	adds	r3, r2, #3
	strh	r3, [r4, #2404]	@ movhi
	ldr	r3, [r4, #2412]
	adds	r1, r3, #3
	str	r1, [r4, #2412]
	strh	r0, [r4, #2422]	@ movhi
	ldrb	r0, [r4, #140]	@ zero_extendqisi2
	cbz	r0, .L447
	adds	r2, r2, #4
	adds	r3, r3, #5
	strh	r2, [r6, #2404]	@ movhi
	b	.L458
.L447:
	cmp	r1, #7
	bhi	.L448
	movs	r3, #8
.L458:
	str	r3, [r6, #2412]
.L448:
	ldrh	r2, [r4, #2324]
	movs	r3, #0
	strh	r3, [r4, #2428]	@ movhi
	movs	r0, #0
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	adds	r3, r3, #52
	add	r5, r3, r5, lsl #2
	cmp	r5, r7, lsl #9
	ittt	cc
	movcc	r2, #1
	ldrcc	r3, .L460
	strhcc	r2, [r3, #2428]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L461:
	.align	2
.L460:
	.word	.LANCHOR0
	.word	.LANCHOR0+2344
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
	ldr	r2, .L469
	ldrh	r3, [r2, #2430]
	cbz	r3, .L468
	ldr	r3, [r2, #2432]
	ldrh	r2, [r2, #2404]
	add	r2, r3, r2, lsl #1
.L464:
	cmp	r3, r2
	beq	.L468
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L464
	movs	r0, #1
	bx	lr
.L468:
	movs	r0, #0
	bx	lr
.L470:
	.align	2
.L469:
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
	ldr	r2, .L479
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r2, #2436]
	cbz	r3, .L478
	ldr	r5, [r2, #2440]
	movs	r6, #36
	movs	r2, #0
.L474:
	mla	r4, r6, r2, r5
	ldr	r4, [r4, #16]
	cmp	r4, r0
	bcc	.L473
	cmp	r4, r1
	bls	.L476
.L473:
	adds	r2, r2, #1
	cmp	r2, r3
	bne	.L474
.L478:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L476:
	movs	r0, #1
	pop	{r4, r5, r6, pc}
.L480:
	.align	2
.L479:
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
	ldr	r3, .L482
	ldr	r0, [r3, #2424]
	bx	lr
.L483:
	.align	2
.L482:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCap, .-FtlGetCap
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
	ldr	r3, .L485
	ldr	r0, [r3, #2424]
	bx	lr
.L486:
	.align	2
.L485:
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
	ldr	r3, .L488
	ldr	r0, [r3, #2444]
	bx	lr
.L489:
	.align	2
.L488:
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
	ldr	r5, .L491
	ldrh	r4, [r5, #2382]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, lsl #2
	ldr	r4, [r1, #2476]
	movs	r1, #1
	subs	r3, r6, r3
	uxth	r3, r3
	and	r0, r3, #31
	lsrs	r7, r3, #5
	lsls	r1, r1, r0
	ldr	r0, [r4, r7, lsl #2]
	orrs	r1, r1, r0
	ldr	r0, .L491+4
	str	r1, [r4, r7, lsl #2]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	ldrh	r3, [r5, #2454]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r5, #2454]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L492:
	.align	2
.L491:
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
	ldr	r5, .L494
	ldrh	r6, [r5, #2382]
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
	ldr	r3, [r5, #2476]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L495:
	.align	2
.L494:
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
	ldr	r4, .L505
	ldrh	r6, [r4, #2382]
	ldrh	r3, [r4, #2338]
	mov	r4, r5
	muls	r6, r3, r6
.L498:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L504
	bl	FtlBbmIsBadBlock
	cbz	r0, .L499
	adds	r4, r4, #1
	uxth	r4, r4
.L499:
	adds	r5, r5, #1
	b	.L498
.L504:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L506:
	.align	2
.L505:
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
	ldr	r6, .L508
	mov	r7, r0
	ldrh	r5, [r6, #2340]
	mov	r1, r5
	bl	__aeabi_uidivmod
	mov	r0, r7
	smlabb	r4, r4, r5, r1
	mov	r1, r5
	bl	__aeabi_uidiv
	ldrh	r3, [r6, #2382]
	smlabb	r0, r3, r0, r4
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.L509:
	.align	2
.L508:
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
	ldr	r3, .L511
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r1, [r3, #2382]
	ldrh	r5, [r3, #2340]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r1, r4
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L512:
	.align	2
.L511:
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
	ldr	r4, .L514
	ldrh	r1, [r4, #2382]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #2340]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L515:
	.align	2
.L514:
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
	bls	.L517
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L517:
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
	ldr	r3, .L520
	ldrh	r0, [r3, #2514]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L521:
	.align	2
.L520:
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
	ldr	r3, .L523
	ldrh	r0, [r3, #2514]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L524:
	.align	2
.L523:
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
	bhi	.L525
	ldr	r4, .L534
	ldrh	r3, [r4, #2514]
	cmp	r3, #1024
	beq	.L525
	cbz	r1, .L527
	ldr	r5, .L534+4
	ldr	r3, [r5, #220]
	cbnz	r3, .L527
	bl	P2V_block_in_plane
	movs	r1, #1
	lsls	r3, r6, #10
	mov	r2, r1
	mov	r7, r0
	ldr	r0, [r5, #224]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r5, #228]
	ldrh	r3, [r2, r7, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	ldr	r3, [r5, #232]
	adds	r3, r3, #1
	str	r3, [r5, #232]
.L527:
	ldrh	r3, [r4, #2514]
	adds	r3, r3, #1
	strh	r3, [r4, #2514]	@ movhi
	ldrh	r3, [r4, #2512]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #2512]	@ movhi
	strh	r6, [r2, #2516]	@ movhi
.L525:
	pop	{r3, r4, r5, r6, r7, pc}
.L535:
	.align	2
.L534:
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
	ldr	r3, .L547
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r4, [r3, #264]
	ldr	r3, .L547+4
	ldrh	r2, [r3, #2514]
	cbz	r2, .L536
	movs	r0, #0
	ldrh	r1, [r3, #2510]
	ldrh	r2, [r3, #2512]
	and	r4, r4, #31
	mov	r6, r0
.L538:
	uxth	r5, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bge	.L546
	add	r5, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r5, #2516]
	add	r5, r3, r2, lsl #1
	strh	r6, [r5, #2516]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L538
.L546:
	cbz	r6, .L536
	strh	r1, [r3, #2510]	@ movhi
	strh	r2, [r3, #2512]	@ movhi
.L536:
	pop	{r4, r5, r6, pc}
.L548:
	.align	2
.L547:
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
	ldr	r4, .L559
	ldr	r6, .L559+4
	mov	r7, r4
.L550:
	ldrh	r1, [r4, #2514]
	cbz	r1, .L551
	ldrh	r3, [r4, #2510]
	subs	r1, r1, #1
	ldr	r10, [r6, #220]
	strh	r1, [r4, #2514]	@ movhi
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldr	r8, .L559+4
	ldrh	r5, [r2, #2516]
	strh	r3, [r4, #2510]	@ movhi
	cmp	r10, #0
	bne	.L552
	mov	r0, r5
	bl	P2V_block_in_plane
	lsls	r3, r5, #10
	mov	r9, r0
	ldr	r0, [r6, #224]
	str	r3, [r0, #4]
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cbz	r3, .L553
	mov	r1, r10
	movs	r2, #1
	bl	FlashEraseBlocks
.L553:
	movs	r1, #1
	ldr	r0, [r8, #224]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r2, [r8, #228]
	ldrh	r3, [r2, r9, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r9, lsl #1]	@ movhi
	ldr	r3, [r8, #232]
	adds	r3, r3, #1
	str	r3, [r8, #232]
	b	.L552
.L551:
	ldr	r0, .L559+8
	bl	printk
.L554:
	b	.L554
.L552:
	subs	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L555
	mov	r1, r5
	ldrh	r2, [r7, #2514]
	ldr	r0, .L559+12
	bl	printk
	b	.L550
.L555:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L560:
	.align	2
.L559:
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
	ldr	r3, .L566
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	movw	r5, #65535
	ldr	r4, [r3, #284]
	ldr	r2, [r0]
	ldr	r3, .L566+4
	subs	r0, r2, r4
	asrs	r0, r0, #1
	muls	r3, r0, r3
	movs	r0, #6
	uxth	r3, r3
.L563:
	cmp	r1, r3
	beq	.L564
	ldrh	r3, [r2]
	cmp	r3, r5
	beq	.L565
	mla	r2, r0, r3, r4
	b	.L563
.L564:
	movs	r0, #1
	pop	{r4, r5, pc}
.L565:
	movs	r0, #0
	pop	{r4, r5, pc}
.L567:
	.align	2
.L566:
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
	ldr	r5, .L584
	ldrh	r3, [r5, #2324]
	cmp	r3, r0
	bls	.L570
	movs	r4, #6
	ldr	r2, .L584+4
	movw	r3, #65535
	muls	r4, r0, r4
	ldr	lr, [r2, #284]
	mov	r7, r2
	add	r1, lr, r4
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [lr, r4]	@ movhi
	ldr	r3, [r2, #288]
	cmp	r3, #0
	beq	.L583
	ldr	r8, [r2, #292]
	lsl	fp, r0, #1
	ldrh	r2, [r1, #4]
	ldrh	r6, [r8, r0, lsl #1]
	cbz	r2, .L581
	muls	r6, r2, r6
	b	.L572
.L581:
	mov	r6, #-1
.L572:
	ldr	ip, [r7, #284]
	ldr	r2, .L584+8
	rsb	r9, ip, r3
	ldrh	r5, [r5, #2324]
	asr	r9, r9, #1
	str	r5, [sp]
	mul	r2, r2, r9
	ldr	r9, [r7, #228]
	add	r5, r9, fp
	str	r5, [sp, #4]
	movs	r5, #0
	uxth	r2, r2
.L579:
	adds	r5, r5, #1
	ldr	r7, [sp]
	uxth	r5, r5
	cmp	r5, r7
	bhi	.L570
	cmp	r0, r2
	beq	.L570
	ldrh	r7, [r3, #4]
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	cbz	r7, .L582
	mul	r7, r7, fp
	b	.L574
.L582:
	mov	r7, #-1
.L574:
	cmp	r7, r6
	bne	.L575
	ldr	r7, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L577
	b	.L576
.L575:
	bhi	.L576
.L577:
	ldrh	r7, [r3]
	movw	r10, #65535
	cmp	r7, r10
	bne	.L578
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L584+4
	str	r1, [r3, #296]
	b	.L570
.L578:
	movs	r3, #6
	mov	r2, r7
	mla	r3, r3, r7, ip
	b	.L579
.L576:
	strh	r2, [lr, r4]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L584+4
	ldr	r4, [r2, #288]
	cmp	r3, r4
	bne	.L580
	strh	r0, [r3, #2]	@ movhi
.L583:
	str	r1, [r2, #288]
	b	.L570
.L580:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #284]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L570:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L585:
	.align	2
.L584:
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
	ldr	r2, .L587
	ldrh	r3, [r2, #300]
	adds	r3, r3, #1
	strh	r3, [r2, #300]	@ movhi
	pop	{r3, pc}
.L588:
	.align	2
.L587:
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
	beq	.L590
	movs	r1, #6
	ldr	r2, .L597
	mul	lr, r1, r0
	ldr	ip, [r2, #284]
	mov	r7, r2
	add	r6, ip, lr
	strh	r5, [r6, #2]	@ movhi
	strh	r5, [ip, lr]	@ movhi
	ldr	r3, [r2, #304]
	cbz	r3, .L596
	ldr	r8, [r2, #284]
	ldr	r9, [r2, #228]
	rsb	r4, r8, r3
	ldr	r2, .L597+4
	asrs	r4, r4, #1
	ldrh	r10, [r9, r0, lsl #1]
	muls	r4, r2, r4
	uxth	r2, r4
.L594:
	ldrh	r4, [r9, r2, lsl #1]
	cmp	r4, r10
	bcs	.L592
	ldrh	r4, [r3]
	cmp	r4, r5
	bne	.L593
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L590
.L593:
	mla	r3, r1, r4, r8
	mov	r2, r4
	b	.L594
.L592:
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	strh	r2, [ip, lr]	@ movhi
	ldr	r1, [r7, #304]
	ldr	r2, .L597
	cmp	r3, r1
	bne	.L595
	strh	r0, [r3, #2]	@ movhi
.L596:
	str	r6, [r2, #304]
	b	.L590
.L595:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #284]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L590:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L598:
	.align	2
.L597:
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
	ldr	r2, .L600
	ldrh	r3, [r2, #308]
	adds	r3, r3, #1
	strh	r3, [r2, #308]	@ movhi
	pop	{r3, pc}
.L601:
	.align	2
.L600:
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
	ldr	r7, .L607
	movw	r6, #65535
	muls	r1, r4, r1
	ldr	r3, [r0]
	ldr	r2, [r7, #284]
	adds	r5, r2, r1
	cmp	r5, r3
	ldrh	r3, [r2, r1]
	bne	.L603
	cmp	r3, r6
	ittee	eq
	moveq	r3, #0
	streq	r3, [r0]
	mlane	r3, r4, r3, r2
	strne	r3, [r0]
	it	ne
	strhne	r6, [r3, #2]	@ movhi
	b	.L605
.L603:
	cmp	r3, r6
	ldrh	r0, [r5, #2]
	bne	.L606
	cmp	r0, r3
	beq	.L605
	muls	r0, r4, r0
	strh	r3, [r2, r0]	@ movhi
	b	.L605
.L606:
	mla	r3, r4, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r7, #284]
	muls	r4, r6, r4
	strh	r0, [r3, r4]	@ movhi
.L605:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, pc}
.L608:
	.align	2
.L607:
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
	cbz	r3, .L615
	ldr	r2, .L616
	movs	r6, #6
	movw	r5, #65535
	ldr	r4, [r2, #284]
.L611:
	cbnz	r1, .L612
.L614:
	subs	r4, r3, r4
	ldr	r3, .L616+4
	asrs	r4, r4, #1
	muls	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L612:
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L614
	subs	r1, r1, #1
	mla	r3, r6, r2, r4
	uxth	r1, r1
	b	.L611
.L615:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L617:
	.align	2
.L616:
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
	ldr	r2, .L625
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #288]
	cbz	r3, .L623
	movs	r4, #6
	ldr	r1, [r2, #284]
	movw	r2, #65535
.L620:
	cbz	r0, .L621
	ldrh	r3, [r3]
	cmp	r3, r2
	beq	.L623
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L620
.L623:
	movw	r0, #65535
	pop	{r4, pc}
.L621:
	subs	r3, r3, r1
	ldr	r0, .L625+4
	asrs	r3, r3, #1
	muls	r0, r3, r0
	uxth	r0, r0
	pop	{r4, pc}
.L626:
	.align	2
.L625:
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
	ldr	r4, .L634
	ldrh	r3, [r4, #312]
	cmp	r3, r0
	beq	.L628
	ldrh	r3, [r4, #360]
	cmp	r3, r0
	beq	.L628
	ldrh	r3, [r4, #408]
	cmp	r3, r0
	beq	.L628
	movs	r3, #6
	ldr	r1, [r4, #284]
	ldr	r2, [r4, #288]
	muls	r3, r0, r3
	adds	r0, r1, r3
	cmp	r0, r2
	beq	.L628
	ldr	r6, [r4, #292]
	ldrh	r2, [r0, #4]
	ldrh	r7, [r6, r5, lsl #1]
	cbz	r2, .L632
	muls	r2, r7, r2
	b	.L629
.L632:
	mov	r2, #-1
.L629:
	ldrh	r0, [r0, #2]
	movw	r7, #65535
	cmp	r0, r7
	bne	.L630
	ldrh	r3, [r1, r3]
	cmp	r3, r0
	beq	.L628
.L630:
	movs	r3, #6
	muls	r3, r0, r3
	ldr	r0, .L634+4
	asrs	r7, r3, #1
	add	r3, r3, r1
	muls	r0, r7, r0
	ldrh	r3, [r3, #4]
	ldrh	r0, [r6, r0, lsl #1]
	cbz	r3, .L633
	muls	r3, r0, r3
	b	.L631
.L633:
	mov	r3, #-1
.L631:
	cmp	r2, r3
	bcs	.L628
	ldr	r0, .L634+8
	mov	r1, r5
	bl	List_remove_node
	ldrh	r3, [r4, #300]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #300]	@ movhi
	bl	INSERT_DATA_LIST
.L628:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L635:
	.align	2
.L634:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+288
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
.L637:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L640
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L637
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	subs	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L638
	ldr	r3, .L643
	ldr	r0, .L643+4
	ldrh	r2, [r3, #2514]
	bl	printk
.L639:
	b	.L639
.L638:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L640:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L644:
	.align	2
.L643:
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
	ldr	r3, .L659
	movs	r4, #12
	ldr	r7, .L659+4
	movw	r5, #65535
	ldrh	r2, [r3, #2422]
	ldr	r3, [r7, #456]
.L646:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L657
	adds	r1, r1, #1
	mla	r6, r4, r1, r3
	ldrh	r6, [r6, #-12]
	cmp	r6, r5
	bne	.L646
	b	.L647
.L657:
	movs	r1, #0
	mov	r6, #-2147483648
	mov	lr, #12
	mov	r0, r2
.L649:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L658
	mla	r4, lr, r1, r3
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L650
	cmp	r4, r6
	itt	cc
	movcc	r6, r4
	movcc	r0, r5
.L650:
	adds	r1, r1, #1
	b	.L649
.L658:
	cmp	r0, r2
	bcc	.L647
	ldrh	r7, [r7, #460]
	movs	r1, #0
	mov	r4, #-1
	mov	r0, r2
.L652:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L647
	ldr	r6, [r3, #4]
	cmp	r6, r4
	bcs	.L653
	ldrh	lr, [r3]
	cmp	lr, r7
	itt	ne
	movne	r4, r6
	movne	r0, r5
.L653:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L652
.L647:
	pop	{r4, r5, r6, r7, pc}
.L660:
	.align	2
.L659:
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
	ldr	r1, .L668
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r2, [r1, #462]
	mov	r3, r1
	cmp	r2, #4
	bhi	.L662
	cbnz	r0, .L662
	adds	r2, r2, #1
	strh	r2, [r1, #462]	@ movhi
	pop	{r4, r5, pc}
.L662:
	ldr	r1, .L668+4
	movs	r2, #0
	strh	r2, [r3, #462]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #464]
	ldrh	r1, [r1, #2324]
	ldr	r2, [r3, #292]
	add	r1, r2, r1, lsl #1
.L663:
	cmp	r2, r1
	beq	.L667
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #464]
	addne	r0, r0, r4
	strne	r0, [r3, #464]
	b	.L663
.L667:
	pop	{r4, r5, pc}
.L669:
	.align	2
.L668:
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
	ldr	r2, .L673
	and	r0, r0, #31
	uxth	r3, r3
	cbz	r1, .L671
	ldr	r1, [r2, #468]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	orrs	r2, r2, r0
	str	r2, [r1, r3, lsl #2]
	bx	lr
.L671:
	ldr	r1, [r2, #468]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	bic	r0, r0, r2
	str	r0, [r1, r3, lsl #2]
	bx	lr
.L674:
	.align	2
.L673:
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
	ldr	r3, .L676
	lsrs	r1, r0, #5
	ldr	r2, [r3, #468]
	and	r3, r0, #31
	ldr	r0, [r2, r1, lsl #2]
	lsrs	r0, r0, r3
	and	r0, r0, #1
	bx	lr
.L677:
	.align	2
.L676:
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
	ldr	r3, .L687
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	adds	r4, r2, #7
	ldrh	r5, [r3, #2316]
	movw	r7, #65535
	add	r4, r0, r4, lsl #1
.L679:
	cmp	r2, r5
	bcs	.L685
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	cmp	r6, r7
	uxth	r2, r2
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L679
.L685:
	ldrh	r6, [r3, #2384]
	add	r4, r0, #14
	movs	r2, #0
	movw	r7, #65535
.L682:
	uxth	r3, r2
	cmp	r3, r5
	bcs	.L686
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
	b	.L682
.L686:
	pop	{r4, r5, r6, r7, pc}
.L688:
	.align	2
.L687:
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
	ldr	r6, .L702
	add	r7, r0, #16
	mov	r4, r0
	mov	r5, r3
	addw	r10, r6, #2344
	movw	r9, #65535
	ldrh	r8, [r6, #2316]
	strh	r3, [r0, #4]	@ movhi
	strb	r3, [r0, #7]
.L690:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L701
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	ldrh	r1, [r4]
	bl	V2P_block
	strh	r9, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L691
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L691:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L690
.L701:
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r3, [r6, #2384]
	smulbb	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2308]
	cbz	r3, .L693
	ldr	r3, .L702+4
	ldrh	r2, [r4]
	ldr	r3, [r3, #228]
	ldrh	r3, [r3, r2, lsl #1]
	cmp	r3, #79
	itt	ls
	movls	r3, #1
	strbls	r3, [r4, #9]
.L693:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cbz	r3, .L694
	movs	r3, #1
	strb	r3, [r4, #9]
.L694:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L703:
	.align	2
.L702:
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
	ldr	r3, .L713
	mov	r7, r0
	mov	r4, r5
	addw	r6, r3, #2344
	ldrh	r8, [r3, #2316]
	ldrh	r9, [r3, #2384]
.L705:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L712
	mov	r1, r7
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L706
	add	r4, r4, r9
	uxth	r4, r4
.L706:
	adds	r5, r5, #1
	b	.L705
.L712:
	cbz	r4, .L708
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L708:
	ldr	r3, .L713+4
	movs	r2, #6
	movs	r0, #0
	ldr	r3, [r3, #284]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L714:
	.align	2
.L713:
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
	ldr	r2, .L718
	ldr	r0, [r2, #304]
	cbz	r0, .L716
	ldr	r3, [r2, #284]
	subs	r0, r0, r3
	ldr	r3, .L718+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #228]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L716:
	bx	lr
.L719:
	.align	2
.L718:
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
	ldr	r1, .L728
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #304]
	cbz	r3, .L726
	ldrh	r2, [r1, #308]
	movs	r6, #6
	ldr	r4, [r1, #284]
	movw	r7, #65535
	rsb	r2, r2, r2, lsl #3
	subs	r3, r3, r4
	asrs	r2, r2, #3
	asrs	r3, r3, #1
	cmp	r0, r2
	it	gt
	uxthgt	r0, r2
	ldr	r2, .L728+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L723:
	uxth	r5, r2
	cmp	r5, r0
	bcs	.L725
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L727
.L725:
	ldr	r2, [r1, #228]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L727:
	mov	r3, r5
	b	.L723
.L726:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L729:
	.align	2
.L728:
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
	ldr	r8, .L740+240
	add	r4, r6, #12
	ldr	r1, .L740
	.pad #32
	sub	sp, sp, #32
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L740+4
	ldr	r2, [r8, #112]
	bl	sprintf
	ldr	r1, .L740+8
	ldr	r2, [r8, #2400]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L740+12
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r4, r0
	bne	.L736
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L740+16
	ldr	r7, .L740+20
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L740+24
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L740+28
	ldr	r2, [r8, #2444]
	bl	sprintf
	ldr	r1, .L740+32
	ldr	r2, [r7, #464]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+36
	ldr	r2, [r7, #472]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+40
	ldr	r2, [r7, #476]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+44
	ldr	r2, [r7, #480]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+48
	ldr	r2, [r7, #484]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+52
	ldr	r2, [r7, #488]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+56
	ldr	r2, [r7, #492]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r7, #496]
	ldr	r1, .L740+60
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r7, #500]
	ldr	r1, .L740+64
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+68
	ldr	r2, [r7, #504]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+72
	ldr	r2, [r7, #508]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldr	r1, .L740+76
	ldrh	r2, [r8, #2454]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+80
	ldrh	r2, [r7, #308]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+84
	ldr	r2, [r7, #512]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+88
	ldr	r2, [r7, #516]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+92
	ldr	r2, [r7, #520]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+96
	ldr	r2, [r7, #232]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+100
	ldr	r2, [r7, #524]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+104
	ldr	r2, [r7, #528]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+108
	ldrh	r2, [r7, #266]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+112
	ldrh	r2, [r7, #264]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+116
	ldr	r2, [r8, #2424]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+120
	ldr	r2, [r8, #2416]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+124
	ldr	r2, [r8, #2312]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+128
	ldrh	r2, [r8, #2514]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+132
	ldrh	r2, [r8, #2324]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+136
	ldrh	r2, [r7, #532]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+140
	ldr	r2, [r8, #2328]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+144
	ldrh	r2, [r7, #536]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+148
	ldrh	r2, [r8, #2448]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+152
	ldrh	r2, [r7, #314]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+156
	ldrb	r2, [r7, #318]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+160
	ldrh	r2, [r7, #312]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+164
	ldrb	r2, [r7, #320]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+168
	ldrh	r2, [r7, #316]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r7, #292]
	ldrh	r2, [r7, #312]
	ldr	r1, .L740+172
	ldrh	r2, [r3, r2, lsl #1]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+176
	ldrh	r2, [r7, #362]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+180
	ldrb	r2, [r7, #366]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+184
	ldrh	r2, [r7, #360]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+188
	ldrb	r2, [r7, #368]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+192
	ldrh	r2, [r7, #364]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r7, #292]
	ldrh	r2, [r7, #360]
	ldr	r1, .L740+196
	ldrh	r2, [r3, r2, lsl #1]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+200
	ldrh	r2, [r7, #410]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+204
	ldrb	r2, [r7, #414]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+208
	ldrh	r2, [r7, #408]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+212
	ldrb	r2, [r7, #416]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+216
	ldrh	r2, [r7, #412]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+220
	ldrh	r2, [r7, #554]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+224
	ldrb	r2, [r7, #558]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+228
	ldrh	r2, [r7, #552]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+232
	ldrb	r2, [r7, #560]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L740+236
	ldrh	r2, [r7, #556]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r7, #688]
	ldr	r3, [r8, #2308]
	ldr	r2, [r7, #600]
	b	.L741
.L742:
	.align	2
.L740:
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
.L741:
	orr	r2, r3, r2, lsl #8
	str	r1, [sp]
	add	r5, r5, r0
	ldr	r1, [r7, #680]
	mov	r0, r5
	str	r1, [sp, #4]
	ldr	r3, [r7, #684]
	ldr	r1, .L743
	bl	sprintf
	ldr	r1, .L743+4
	ldr	r2, [r7, #676]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L743+8
	ldr	r2, [r7, #700]
	adds	r4, r5, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L743+12
	ldrh	r2, [r7, #1116]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L743+16
	ldrh	r2, [r7, #1118]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L743+20
	ldr	r2, [r7, #1120]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L743+24
	ldrh	r2, [r7, #1124]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L743+28
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r7, #308]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L743+32
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r7, #552]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L733
	ldr	r2, [r7, #292]
	mov	r0, r4
	ldr	r1, .L743+36
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
	add	r4, r4, r0
.L733:
	movs	r0, #0
	movs	r5, #0
	bl	List_get_gc_head_node
	mov	r9, #6
	movw	r10, #65535
	uxth	r3, r0
.L735:
	cmp	r3, r10
	beq	.L734
	ldr	r2, [r7, #292]
	mov	r0, r4
	mul	r8, r9, r3
	ldr	r1, .L743+40
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp]
	ldr	r2, [r7, #284]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r7, #228]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r7, #284]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L735
.L734:
	ldr	r2, [r7, #284]
	movs	r5, #0
	ldr	r3, [r7, #304]
	mov	r9, #6
	movw	r10, #65535
	subs	r3, r3, r2
	ldr	r2, .L743+44
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r3, r3
.L737:
	cmp	r3, r10
	beq	.L736
	mul	r8, r9, r3
	ldr	r2, [r7, #284]
	mov	r0, r4
	ldr	r1, .L743+48
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	ldr	r2, [r7, #228]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r7, #284]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L737
.L736:
	subs	r0, r4, r6
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L744:
	.align	2
.L743:
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
	ldr	r1, .L746
	ldr	r2, .L746+4
	bl	sprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L747:
	.align	2
.L746:
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
	ldr	r4, .L776
	ldr	r2, [r4, #520]
	ldr	r3, [r4, #528]
	cmp	r2, r3
	bcs	.L749
	ldr	r2, .L776+4
	movs	r3, #0
	ldr	r0, [r4, #228]
	str	r3, [r4, #512]
	ldrh	r1, [r2, #2324]
	mov	r5, r2
.L750:
	cmp	r3, r1
	bcs	.L775
	ldrh	r6, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r2, [r4, #512]
	add	r2, r2, r6
	str	r2, [r4, #512]
	b	.L750
.L775:
	ldr	r6, [r4, #512]
	mov	r0, r6
	bl	__aeabi_uidiv
	ldrh	r1, [r5, #2376]
	str	r0, [r4, #520]
	ldr	r0, [r4, #516]
	subs	r0, r6, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #512]
	b	.L752
.L749:
	ldr	r3, [r4, #524]
	cmp	r2, r3
	bls	.L752
	ldr	r0, .L776+4
	adds	r3, r3, #1
	str	r3, [r4, #524]
	movs	r3, #0
.L754:
	ldrh	r2, [r0, #2324]
	cmp	r3, r2
	bcs	.L752
	ldr	r1, [r4, #228]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L754
.L752:
	ldr	r6, [r4, #528]
	ldr	r8, [r4, #520]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L757
	ldr	r2, .L776
	add	r3, r6, #768
	ldr	r2, [r2, #524]
	cmp	r3, r2
	bls	.L757
	ldr	r3, .L776+4
	ldr	r3, [r3, #2308]
	cbnz	r3, .L758
.L760:
	movw	r0, #65535
	b	.L759
.L758:
	cmp	r6, #40
	bhi	.L760
.L757:
	ldrh	r0, [r4, #308]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r9, r0
	bcs	.L761
	cmp	r6, #40
	bhi	.L760
.L761:
	ldr	r3, [r4, #288]
	ldr	r2, .L776
	cmp	r3, #0
	beq	.L760
	ldr	r1, .L776+4
	movw	r7, #65535
	ldr	r0, [r2, #284]
	mov	ip, #6
	ldr	r10, [r2, #228]
	mov	r5, r7
	ldrh	r1, [r1, #2324]
	mov	lr, r7
	str	r1, [sp, #20]
	movs	r1, #0
.L762:
	ldrh	r2, [r3]
	cmp	r2, lr
	str	r2, [sp, #16]
	beq	.L764
	adds	r1, r1, #1
	ldr	r2, [sp, #20]
	uxth	r1, r1
	cmp	r1, r2
	bhi	.L760
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L763
	subs	r3, r3, r0
	ldr	r2, .L776+8
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r3, r3
	ldrh	fp, [r10, r3, lsl #1]
	cmp	fp, r6
	bls	.L768
	cmp	fp, r7
	itt	cc
	movcc	r7, fp
	movcc	r5, r3
.L763:
	ldr	r3, [sp, #16]
	mla	r3, ip, r3, r0
	b	.L762
.L768:
	mov	r5, r3
.L764:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L760
	ldrh	fp, [r10, r5, lsl #1]
	lsls	r3, r5, #1
	cmp	fp, r6
	bls	.L766
	str	r3, [sp, #16]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #16]
	cmp	r0, r6
	it	hi
	strhi	r7, [r4, #528]
.L766:
	cmp	fp, r8
	bcs	.L760
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L760
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L767
	ldr	r2, [r4, #524]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L760
.L767:
	ldr	r2, [r4, #292]
	mov	r1, r5
	ldr	r0, .L776+12
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	str	r9, [sp, #8]
	str	r3, [sp, #4]
	ldr	r3, [r4, #524]
	bl	printk
	mov	r0, r5
	movs	r3, #1
	str	r3, [r4, #1128]
.L759:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L777:
	.align	2
.L776:
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
	beq	.L779
	ldr	r2, .L780
	movs	r1, #0
	ldr	r2, [r2, #292]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L779:
	movs	r0, #0
	pop	{r3, pc}
.L781:
	.align	2
.L780:
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
	ldr	r2, .L793
	mov	ip, #36
	ldr	r0, .L793+4
	movs	r3, #0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r7, #12
	mov	lr, #1
	mov	r6, r0
	mov	r4, r2
	str	r3, [r2, #1132]
.L783:
	ldrh	r1, [r0, #2316]
	adds	r5, r3, #1
	uxth	r3, r3
	ldr	r8, .L793+4
	cmp	r3, r1
	bcs	.L791
	mul	r8, r7, r3
	ldr	r9, [r2, #1136]
	add	r1, r9, r8
	str	lr, [r1, #8]
	ldrh	r1, [r0, #2394]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	bic	r10, r1, #3
	ldr	r1, [r4, #1140]
	add	r1, r1, r10
	str	r1, [r9, r8]
	ldr	r10, [r2, #1136]
	ldrh	r1, [r6, #2396]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	add	r9, r10, r8
	bic	fp, r1, #3
	ldr	r1, [r4, #1144]
	add	r1, r1, fp
	str	r1, [r9, #4]
	ldr	r1, [r2, #1148]
	mla	r3, ip, r3, r1
	ldr	r1, [r10, r8]
	str	r1, [r3, #8]
	ldr	r1, [r9, #4]
	str	r1, [r3, #12]
	mov	r3, r5
	b	.L783
.L791:
	ldr	r0, .L793
	mov	lr, #12
	movs	r5, #0
.L787:
	ldr	r3, [r2, #1152]
	cmp	r1, r3
	bcs	.L792
	mul	r7, lr, r1
	ldr	r6, [r0, #1136]
	ldr	r4, [r0, #1140]
	adds	r3, r6, r7
	str	r5, [r3, #8]
	ldrh	r3, [r8, #2394]
	muls	r3, r1, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r4
	str	r3, [r6, r7]
	ldr	r3, [r2, #1136]
	ldr	r6, [r0, #1144]
	add	r7, r7, r3
	ldrh	r3, [r8, #2396]
	muls	r3, r1, r3
	add	r1, r1, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r1, r1
	add	r3, r3, r6
	str	r3, [r7, #4]
	b	.L787
.L792:
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L794:
	.align	2
.L793:
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
	ldr	r3, .L802
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	ldr	lr, [r3, #1152]
	mov	r10, #36
	ldr	r6, [r3, #1136]
	mov	r8, #12
	mov	ip, r5
.L796:
	uxth	r4, r5
	cmp	r4, r1
	bcs	.L795
	mla	r4, r10, r4, r0
	movs	r2, #0
.L797:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L798
	mul	r3, r8, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L797
	str	ip, [r9, #8]
.L798:
	adds	r5, r5, #1
	b	.L796
.L795:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L803:
	.align	2
.L802:
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
	ldr	r3, .L812
	mov	ip, #1
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	ldr	r5, [r3, #1152]
	mov	lr, #12
	ldr	r6, [r3, #1136]
	mov	r8, #36
.L805:
	uxth	r2, r4
	cmp	r2, r1
	bcs	.L811
	mov	r9, #0
.L806:
	uxth	r3, r9
	cmp	r3, r5
	bcs	.L807
	mla	r3, lr, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L806
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	ip, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L807:
	adds	r4, r4, #1
	b	.L805
.L811:
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L813:
	.align	2
.L812:
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
	ldr	r2, .L820
	ldr	r3, [r2, #1156]
	ldrh	r2, [r2, #1160]
	add	r2, r3, r2, lsl #1
.L815:
	cmp	r3, r2
	beq	.L819
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L815
	movs	r0, #1
	bx	lr
.L819:
	movs	r0, #0
	bx	lr
.L821:
	.align	2
.L820:
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
	ldr	r3, .L829
	movs	r4, #0
	ldrh	r1, [r3, #1160]
	ldr	r2, [r3, #1156]
.L823:
	uxth	r5, r4
	cmp	r5, r1
	bcs	.L827
	adds	r4, r4, #1
	add	lr, r2, r4, lsl #1
	ldrh	lr, [lr, #-2]
	cmp	lr, r0
	bne	.L823
.L827:
	cmp	r5, r1
	bne	.L825
	strh	r0, [r2, r5, lsl #1]	@ movhi
	ldrh	r0, [r3, #1160]
	adds	r0, r0, #1
	strh	r0, [r3, #1160]	@ movhi
.L825:
	ldrh	r2, [r3, #1168]
	movs	r0, #12
	muls	r0, r2, r0
	ldr	r2, [r3, #1164]
	adds	r1, r2, r0
	str	r7, [r1, #4]
	str	r8, [r1, #8]
	str	r6, [r2, r0]
	ldrh	r2, [r3, #1168]
	adds	r2, r2, #1
	strh	r2, [r3, #1168]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L830:
	.align	2
.L829:
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
	ldr	r4, .L838
	ldrh	r3, [r4, #1170]
	cmp	r3, r0
	beq	.L833
	ldrh	r3, [r4, #1172]
	cmp	r3, r0
	beq	.L833
	ldrh	r3, [r4, #1174]
	cmp	r3, r0
	beq	.L833
	ldrh	r3, [r4, #1176]
	cmp	r3, r0
	beq	.L833
	ldr	r0, .L838+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #1170]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L835
	strh	r5, [r4, #1170]	@ movhi
	b	.L833
.L835:
	ldrh	r2, [r4, #1172]
	cmp	r2, r3
	bne	.L836
	strh	r5, [r4, #1172]	@ movhi
	b	.L833
.L836:
	ldrh	r2, [r4, #1174]
	cmp	r2, r3
	bne	.L837
	strh	r5, [r4, #1174]	@ movhi
	b	.L833
.L837:
	ldrh	r2, [r4, #1176]
	cmp	r2, r3
	it	eq
	strheq	r5, [r4, #1176]	@ movhi
.L833:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L839:
	.align	2
.L838:
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
	ldr	r4, .L850
	ldrh	r3, [r4, #1170]
	cmp	r3, r0
	beq	.L848
	ldrh	r3, [r4, #1172]
	cmp	r3, r0
	beq	.L848
	ldrh	r3, [r4, #1174]
	cmp	r3, r0
	beq	.L848
	ldrh	r3, [r4, #1176]
	cmp	r3, r0
	beq	.L848
	ldr	r0, .L850+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #1170]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L842
	strh	r5, [r4, #1170]	@ movhi
	b	.L848
.L842:
	ldrh	r2, [r4, #1172]
	cmp	r2, r3
	bne	.L843
	strh	r5, [r4, #1172]	@ movhi
	b	.L848
.L843:
	ldrh	r2, [r4, #1174]
	cmp	r2, r3
	bne	.L844
	strh	r5, [r4, #1174]	@ movhi
	b	.L848
.L844:
	ldrh	r2, [r4, #1176]
	cmp	r2, r3
	bne	.L849
	strh	r5, [r4, #1176]	@ movhi
.L848:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L849:
	mov	r0, #-1
	pop	{r3, r4, r5, pc}
.L851:
	.align	2
.L850:
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
	ldr	r4, .L861
	mov	r2, r5
	ldrh	r1, [r4, #1178]
	mov	r6, r0
	ldr	r0, .L861+4
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r3, .L861+8
	ldr	r2, [r3, #2308]
	mov	r3, r4
	cbz	r2, .L853
	ldr	r1, [r4, #228]
	ldrh	r2, [r1, r6, lsl #1]
	cmp	r2, #39
	itt	hi
	subhi	r2, r2, #40
	strhhi	r2, [r1, r6, lsl #1]	@ movhi
.L853:
	ldrh	r2, [r3, #1178]
	movs	r1, #0
	ldr	r4, .L861+12
.L854:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L860
	adds	r1, r1, #1
	add	r0, r4, r1, lsl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r5
	bne	.L854
	b	.L855
.L860:
	cmp	r2, #15
	itttt	ls
	addls	r1, r2, #1
	strhls	r1, [r3, #1178]	@ movhi
	addls	r3, r3, r2, lsl #1
	strhls	r5, [r3, #1180]	@ movhi
.L855:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L862:
	.align	2
.L861:
	.word	.LANCHOR2
	.word	.LC80
	.word	.LANCHOR0
	.word	.LANCHOR2+1180
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
	ldr	r4, .L869
	ldrh	r3, [r4, #1178]
	cbz	r3, .L864
	ldrh	r1, [r4, #1170]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L864
	ldrh	r2, [r4, #1214]
	cmp	r2, r3
	itt	cs
	movcs	r3, #0
	strhcs	r3, [r4, #1214]	@ movhi
	ldrh	r3, [r4, #1214]
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #1180]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #1214]
	adds	r3, r3, #1
	strh	r3, [r4, #1214]	@ movhi
.L864:
	movs	r0, #0
	pop	{r4, pc}
.L870:
	.align	2
.L869:
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
	cbnz	r0, .L873
	ldr	r3, .L907
.L874:
	strh	r0, [r3, r0, lsl #1]	@ movhi
	adds	r0, r0, #1
	cmp	r0, #256
	bne	.L874
.L878:
	movs	r1, #255
	ldr	r0, .L907+4
	mov	r2, #2048
	uxth	r4, r4
	bl	ftl_memset
	movs	r3, #0
	ldr	r1, .L907
	ldr	r0, .L907+8
	b	.L875
.L873:
	cmp	r0, #1
	bne	.L876
	ldr	r5, .L907
	movs	r3, #0
.L877:
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
	cmp	r3, #256
	bne	.L877
	b	.L878
.L876:
	cmp	r0, #2
	bne	.L879
	ldr	r0, .L907
	movs	r3, #0
.L880:
	uxth	r2, r3
	lsls	r1, r2, #1
	subs	r1, r1, #1
	cmp	r2, #1
	it	hi
	movhi	r2, r1
	strh	r2, [r0, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #256
	bne	.L880
	b	.L878
.L879:
	cmp	r0, #3
	bne	.L881
	ldr	r5, .L907
	movs	r3, #0
.L882:
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
	cmp	r3, #256
	bne	.L882
	b	.L878
.L881:
	cmp	r0, #4
	mov	r3, #0
	bne	.L883
	ldr	r2, .L907+12
	strh	r3, [r2, #144]	@ movhi
	movs	r3, #1
	strh	r0, [r2, #152]	@ movhi
	strh	r3, [r2, #146]	@ movhi
	movs	r3, #2
	strh	r3, [r2, #148]	@ movhi
	movs	r3, #3
	strh	r3, [r2, #150]	@ movhi
	movs	r3, #5
	strh	r3, [r2, #154]	@ movhi
	movs	r3, #7
	strh	r3, [r2, #156]	@ movhi
	movs	r3, #8
	strh	r3, [r2, #158]!	@ movhi
.L884:
	tst	r3, #1
	ite	ne
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	adds	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L884
	b	.L878
.L883:
	cmp	r0, #5
	bne	.L885
	ldr	r2, .L907
.L886:
	strh	r3, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #16
	bne	.L886
	ldr	r2, .L907+16
.L887:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L887
	b	.L878
.L885:
	cmp	r0, #6
	bne	.L888
	ldr	r5, .L907
.L889:
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
	cmp	r3, #256
	bne	.L889
	b	.L878
.L888:
	cmp	r0, #9
	bne	.L890
	ldr	r2, .L907+12
	movw	r1, #509
	strh	r3, [r2, #144]	@ movhi
	movs	r3, #1
	strh	r3, [r2, #146]	@ movhi
	movs	r3, #2
	strh	r3, [r2, #148]!	@ movhi
	movs	r3, #3
.L891:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L891
	b	.L878
.L890:
	cmp	r0, #10
	bne	.L878
	ldr	r2, .L907
.L892:
	strh	r3, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #63
	bne	.L892
	ldr	r2, .L907+20
	movw	r1, #961
.L893:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L893
	b	.L878
.L875:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L906
	ldrh	r2, [r1, r3, lsl #1]
	adds	r3, r3, #1
	add	r5, r0, r2, lsl #1
	strh	r2, [r5, #1216]	@ movhi
	b	.L875
.L906:
	pop	{r4, r5, r6, pc}
.L908:
	.align	2
.L907:
	.word	.LANCHOR0+144
	.word	.LANCHOR2+1216
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+174
	.word	.LANCHOR0+268
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
	ldr	r3, .L923
	ldr	r4, .L923+4
	ldr	r6, .L923+8
	ldrh	r0, [r3, #482]
	addw	fp, r4, #2068
	strb	r7, [r4, #2229]
	mov	r9, r4
	strb	r7, [r6, #3264]
	bl	FlashBlockAlignInit
	mov	r1, r7
	movs	r2, #8
	addw	r0, r4, #2232
	bl	ftl_memset
	mov	r1, r7
	movs	r2, #32
	add	r0, r4, #1168
	bl	ftl_memset
	addw	r0, r4, #2100
	mov	r1, r7
	movs	r2, #128
	bl	ftl_memset
	ldr	r5, [r4, #1204]
	add	r8, r5, #1
.L911:
	mov	r0, r8
	add	r1, fp, r7, lsl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r10, .L923+12
	cbnz	r0, .L910
	ldrb	r3, [r9, #2229]	@ zero_extendqisi2
	add	r2, r9, r3, lsl #2
	str	r0, [r2, #1168]
	adds	r2, r3, #1
	add	r3, r3, r9
	strb	r2, [r9, #2229]
	strb	r7, [r3, #2232]
.L910:
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L911
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	ldr	r2, .L923+4
	strb	r3, [r6, #3264]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L912
.L916:
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #2229]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #14]
	smulbb	r3, r2, r3
	strh	r3, [r6, #3266]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L912:
	ldr	r9, [r2, #4]
	movs	r7, #0
	mov	fp, r2
.L915:
	mov	r0, r8
	add	r1, r10, r7, lsl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cbnz	r0, .L913
	ldrb	r0, [r5, #13]	@ zero_extendqisi2
	ldrh	r3, [r5, #14]
	ldrb	r2, [fp, #2229]	@ zero_extendqisi2
	mul	r0, r9, r0
	and	r3, r3, #65280
	add	r1, fp, r2, lsl #2
	muls	r3, r0, r3
	str	r3, [r1, #1168]
	ldrb	r0, [r5, #23]	@ zero_extendqisi2
	cbz	r0, .L914
	lsls	r3, r3, #1
	str	r3, [r1, #1168]
.L914:
	adds	r3, r2, #1
	add	r2, r2, r4
	strb	r3, [r4, #2229]
	strb	r7, [r2, #2232]
.L913:
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L915
	b	.L916
.L924:
	.align	2
.L923:
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
	ldr	r3, .L929
	movs	r5, #1
	ldr	r2, [r3, #1204]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	ldr	r2, [r3, #4]
	smulbb	r2, r1, r2
	strh	r2, [r4, #4]	@ unaligned
	ldrb	r2, [r3, #2304]	@ zero_extendqisi2
	strb	r2, [r4, #7]
	ldr	r2, [r3, #2424]
	str	r2, [r4]	@ unaligned
	ldr	r2, [r3, #1204]
	addw	r3, r3, #2232
	ldrb	r0, [r3, #-3]	@ zero_extendqisi2
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	movs	r1, #32
	strb	r1, [r4, #8]
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	strb	r2, [r4, #9]
	movs	r2, #0
	strb	r2, [r4, #10]
.L926:
	uxtb	r1, r2
	cmp	r1, r0
	bcs	.L928
	ldrb	r1, [r2, r3]	@ zero_extendqisi2
	adds	r2, r2, #1
	lsl	r6, r5, r1
	ldrb	r1, [r4, #10]	@ zero_extendqisi2
	orrs	r1, r1, r6
	strb	r1, [r4, #10]
	b	.L926
.L928:
	pop	{r4, r5, r6, pc}
.L930:
	.align	2
.L929:
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
	ldr	r4, .L1035
	movs	r6, #0
	ldr	r5, .L1035+4
	movw	r3, #65535
	movs	r7, #12
	mov	r8, #36
	strh	r6, [r4, #3268]	@ movhi
	str	r6, [r4, #3272]
	str	r6, [r4, #600]
	str	r6, [r4, #504]
	str	r6, [r4, #508]
	str	r6, [r4, #492]
	str	r6, [r4, #480]
	str	r6, [r4, #476]
	str	r6, [r4, #484]
	str	r6, [r4, #488]
	str	r6, [r4, #472]
	str	r6, [r4, #512]
	str	r6, [r4, #516]
	str	r6, [r4, #232]
	str	r6, [r4, #524]
	str	r6, [r4, #528]
	str	r6, [r4, #3276]
	str	r6, [r4, #1128]
	str	r3, [r4, #3280]
	str	r6, [r4, #3284]
	str	r6, [r4, #1120]
	str	r6, [r4, #3288]
	strh	r3, [r4, #1170]	@ movhi
	strh	r3, [r4, #1172]	@ movhi
	strh	r3, [r4, #1174]	@ movhi
	strh	r3, [r4, #1176]	@ movhi
	movs	r3, #32
	strh	r6, [r4, #1124]	@ movhi
	strh	r3, [r4, #1116]	@ movhi
	movs	r3, #128
	strh	r6, [r4, #1178]	@ movhi
	strh	r3, [r4, #1118]	@ movhi
	strh	r6, [r4, #3292]	@ movhi
	strh	r6, [r4, #1214]	@ movhi
	ldrh	r1, [r5, #2390]
	bl	__aeabi_idiv
	ldrh	r3, [r5, #2316]
	str	r6, [r5, #2436]
	lsls	r3, r3, #2
	cmp	r0, r3
	str	r0, [r4, #3296]
	ldrh	r0, [r5, #2388]
	it	hi
	strhi	r3, [r4, #3296]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #1156]
	ldrh	r0, [r5, #2388]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #2316]
	mul	r6, r8, r6
	lsl	r9, r6, #3
	str	r0, [r4, #1164]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #3300]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3304]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #3308]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #224]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1148]
	ldr	r0, [r4, #3296]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #2394]
	ldrh	r3, [r5, #2316]
	lsls	r3, r3, #1
	adds	r3, r3, #1
	str	r3, [r4, #1152]
	str	r0, [r5, #2440]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3312]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3316]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3320]
	ldr	r0, [r4, #1152]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #1140]
	ldr	r0, [r4, #3296]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #3324]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3328]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3332]
	ldr	r0, [r4, #1152]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2316]
	ldrh	r6, [r5, #2396]
	muls	r6, r3, r6
	str	r0, [r4, #1136]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3336]
	lsls	r0, r6, #3
	bl	ftl_malloc
	ldrh	r3, [r5, #2396]
	str	r0, [r4, #3340]
	ldr	r0, [r4, #1152]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2396]
	str	r0, [r4, #1144]
	ldr	r0, [r4, #3296]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #3344]
	ldrh	r0, [r5, #2326]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3348]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3352]
	ldrh	r0, [r4, #3348]
	addw	r0, r0, #547
	lsrs	r0, r0, #9
	strh	r0, [r4, #3348]	@ movhi
	lsls	r0, r0, #9
	bl	ftl_malloc
	ldrh	r6, [r5, #2326]
	lsls	r6, r6, #1
	str	r0, [r4, #3356]
	adds	r0, r0, #32
	str	r0, [r4, #228]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3360]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r5, #2412]
	lsls	r6, r6, #1
	str	r0, [r4, #292]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3364]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3368]
	ldrh	r0, [r5, #2326]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #468]
	ldrh	r0, [r5, #2404]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r5, #2432]
	ldrh	r0, [r5, #2404]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3372]
	ldrh	r0, [r5, #2404]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3376]
	ldrh	r0, [r5, #2406]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5, #2406]
	movs	r1, #0
	lsls	r2, r2, #2
	str	r0, [r4, #3380]
	bl	ftl_memset
	ldrh	r6, [r5, #2420]
	lsls	r6, r6, #2
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3384]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, .L1035+8
	str	r0, [r4, #3388]
	ldr	r0, [r5, #2412]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3392]
	ldrh	r0, [r5, #2422]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2422]
	str	r0, [r4, #456]
	ldrh	r0, [r5, #2394]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2326]
	str	r0, [r4, #3396]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2382]
	adds	r3, r3, #31
	asrs	r3, r3, #5
	strh	r3, [r4, #3400]	@ movhi
	str	r0, [r4, #284]
	ldrh	r0, [r5, #2338]
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #3400]
	ldrh	r7, [r5, #2338]
	movs	r3, #1
	lsls	r2, r2, #2
	mov	r1, r2
	str	r0, [r5, #2476]
.L933:
	cmp	r3, r7
	bcs	.L1033
	ldr	r0, [r5, #2476]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r6, #4]!
	b	.L933
.L1033:
	ldr	r2, .L1035+12
	movs	r1, #0
.L935:
	cmp	r3, #8
	beq	.L1034
	add	r0, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r0, #28]
	b	.L935
.L1034:
	ldr	r2, [r4, #3364]
	ldr	r3, .L1035
	cbnz	r2, .L937
.L939:
	ldr	r1, .L1035+16
	ldr	r0, .L1035+20
	bl	printk
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1036:
	.align	2
.L1035:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2476
	.word	.LANCHOR0+2448
	.word	.LANCHOR3
	.word	.LC81
.L937:
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3384]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3392]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #456]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3396]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #284]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r5, #2476]
	cmp	r2, #0
	beq	.L939
	ldr	r3, [r3, #292]
	cmp	r3, #0
	beq	.L939
	ldr	r2, [r4, #1156]
	ldr	r3, .L1037
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #1164]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3300]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3308]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #224]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #1148]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3304]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3312]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3316]
	cmp	r2, #0
	beq	.L939
	ldr	r3, [r3, #3320]
	cmp	r3, #0
	beq	.L939
	ldr	r2, [r4, #1140]
	ldr	r3, .L1037
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3328]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3332]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #1136]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3336]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3340]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #1144]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #228]
	cmp	r2, #0
	beq	.L939
	ldr	r3, [r3, #3352]
	cmp	r3, #0
	beq	.L939
	ldr	r3, .L1037+4
	ldr	r3, [r3, #2432]
	cmp	r3, #0
	beq	.L939
	ldr	r3, .L1037
	ldr	r2, [r3, #3372]
	cmp	r2, #0
	beq	.L939
	ldr	r2, [r3, #3376]
	cmp	r2, #0
	beq	.L939
	ldr	r3, [r3, #3380]
	cmp	r3, #0
	beq	.L939
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1038:
	.align	2
.L1037:
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
	ldr	r3, .L1044
	mov	r0, r1
	mov	r5, r1
	movs	r1, #0
	movs	r7, #1
	ldrh	r2, [r3, #3400]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldr	r1, .L1044+4
	subs	r2, r4, #2
	addw	r4, r4, #1022
	movw	lr, #65535
.L1041:
	ldrh	r3, [r2, #2]!
	cmp	r3, lr
	beq	.L1039
	lsrs	r0, r3, #5
	and	r3, r3, #31
	lsl	r3, r7, r3
	cmp	r2, r4
	ldr	r6, [r5, r0, lsl #2]
	orr	r3, r3, r6
	str	r3, [r5, r0, lsl #2]
	ldrh	r3, [r1, #2454]
	add	r3, r3, #1
	strh	r3, [r1, #2454]	@ movhi
	bne	.L1041
.L1039:
	pop	{r3, r4, r5, r6, r7, pc}
.L1045:
	.align	2
.L1044:
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
	ldr	r0, .L1047
	movw	r3, #65535
	movs	r1, #255
	movs	r2, #16
	addw	r0, r0, #2460
	strh	r3, [r0, #-12]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	ftl_memset
.L1048:
	.align	2
.L1047:
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
	ldr	r3, .L1050
	mov	r2, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	strh	r0, [r3, #2508]	@ movhi
	addw	r0, r3, #2516
	mov	r1, r4
	strh	r4, [r3, #2510]	@ movhi
	strh	r4, [r3, #2512]	@ movhi
	strh	r4, [r3, #2514]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L1051:
	.align	2
.L1050:
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
.L1053:
	ldrh	r2, [r4, #6]
	uxth	r3, r1
	cmp	r2, r3
	bls	.L1072
	ldr	r2, [r5, r3, lsl #2]
	movs	r0, #0
	ubfx	r2, r2, #10, #16
.L1054:
	ldrh	lr, [r4, #10]
	uxth	r3, r0
	cmp	lr, r3
	bls	.L1073
	ldrh	lr, [r7, r3, lsl #1]
	cmp	lr, r2
	bne	.L1055
	cbz	r2, .L1055
	ldrh	lr, [r6, r3, lsl #1]
	add	lr, lr, #1
	strh	lr, [r6, r3, lsl #1]	@ movhi
.L1055:
	adds	r0, r0, #1
	b	.L1054
.L1073:
	adds	r1, r1, #1
	b	.L1053
.L1072:
	ldr	r3, .L1076
	mov	r8, #0
	mov	r10, r8
	ldrh	r2, [r3, #2386]
	ldrh	r3, [r4]
	strh	r2, [r6, r3, lsl #1]	@ movhi
	mov	r2, r8
	ldrh	r9, [r6]
.L1058:
	ldrh	r3, [r4, #10]
	uxth	r5, r8
	cmp	r3, r5
	bls	.L1074
	ldrh	r3, [r6, r5, lsl #1]
	lsls	r1, r5, #1
	cmp	r9, r3
	bls	.L1059
	ldrh	r0, [r7, r5, lsl #1]
	add	fp, r7, r1
	cbnz	r0, .L1060
	b	.L1061
.L1059:
	cbnz	r3, .L1061
	ldrh	r0, [r7, r1]
	add	fp, r7, r1
	cbz	r0, .L1061
	mov	r5, r2
	b	.L1063
.L1060:
	cbnz	r3, .L1075
	mov	r9, r3
.L1063:
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	r10, [fp]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r2, r5
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1061:
	add	r8, r8, #1
	b	.L1058
.L1075:
	mov	r2, r5
	mov	r9, r3
	b	.L1061
.L1074:
	mov	r0, r2
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1077:
	.align	2
.L1076:
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
	ldr	r4, .L1082
	movs	r7, #0
	ldr	r5, .L1082+4
	mov	r6, r7
	ldr	r2, [r4, #2412]
	mov	r9, r4
	ldr	r0, [r5, #3368]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #2394]
	ldrh	r2, [r4, #2422]
	movs	r1, #255
	ldr	r0, [r5, #3396]
	muls	r2, r3, r2
	bl	ftl_memset
	mov	r3, r5
	movs	r5, #12
	mov	r2, r4
	movw	lr, #65535
.L1079:
	ldrh	ip, [r2, #2422]
	adds	r0, r7, #1
	uxth	r7, r7
	ldr	r1, .L1082
	cmp	ip, r7
	bls	.L1081
	mul	r1, r5, r7
	ldr	ip, [r3, #456]
	add	r8, ip, r1
	str	r6, [r8, #4]
	strh	lr, [ip, r1]	@ movhi
	ldr	r4, [r3, #456]
	add	ip, r4, r1
	ldrh	r1, [r9, #2394]
	muls	r1, r7, r1
	ldr	r7, [r3, #3396]
	bic	r1, r1, #3
	add	r1, r1, r7
	mov	r7, r0
	str	r1, [ip, #8]
	b	.L1079
.L1081:
	movw	r2, #65535
	strh	r2, [r3, #3406]	@ movhi
	strh	r2, [r3, #3404]	@ movhi
	ldr	r2, [r1, #2412]
	strh	r2, [r3, #3414]	@ movhi
	movw	r2, #61634
	strh	r2, [r3, #3408]	@ movhi
	ldrh	r2, [r3, #3448]
	strh	r2, [r3, #3412]	@ movhi
	ldrh	r2, [r1, #2420]
	strh	r2, [r3, #3410]	@ movhi
	ldr	r2, [r3, #3364]
	str	r2, [r3, #3416]
	ldr	r2, [r3, #3392]
	str	r2, [r3, #3420]
	ldr	r2, [r3, #3368]
	str	r2, [r3, #3424]
	ldr	r2, [r3, #3384]
	str	r2, [r3, #3428]
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1083:
	.align	2
.L1082:
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
	ldr	r6, .L1085
	movw	r3, #65535
	ldr	r5, .L1085+4
	mov	r1, r4
	ldrh	r2, [r6, #2404]
	ldr	r0, [r6, #2432]
	strh	r3, [r5, #3456]	@ movhi
	mov	r3, #-1
	lsls	r2, r2, #1
	str	r3, [r5, #3468]
	str	r4, [r5, #3452]
	str	r4, [r5, #3460]
	str	r4, [r5, #3464]
	str	r4, [r6, #2308]
	strh	r4, [r6, #2430]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r6, #2326]
	mov	r1, r4
	ldr	r0, [r5, #228]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6, #2326]
	mov	r1, r4
	ldr	r0, [r5, #3352]
	lsls	r2, r2, #1
	bl	ftl_memset
	mov	r1, r4
	add	r0, r5, #236
	movs	r2, #48
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	add	r0, r5, #604
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1086:
	.align	2
.L1085:
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
	ldr	r6, .L1098
	movs	r5, #0
	ldr	r4, .L1098+4
	.pad #20
	sub	sp, sp, #20
	movs	r1, #0
	mov	r9, r5
	ldrh	r3, [r6, #2326]
	mov	r10, r5
	ldr	r0, [r4, #284]
	mov	r8, r6
	muls	r2, r3, r2
	bl	ftl_memset
	str	r5, [r4, #304]
	str	r5, [r4, #288]
	str	r5, [r4, #296]
	strh	r5, [r4, #300]	@ movhi
	strh	r5, [r4, #308]	@ movhi
	strh	r5, [r4, #3268]	@ movhi
.L1088:
	uxth	fp, r5
	ldrh	r2, [r8, #2324]
	sxth	r7, fp
	cmp	r7, r2
	bge	.L1095
	ldr	r3, .L1098
	movs	r2, #0
	mov	r6, r2
	ldrh	ip, [r3, #2316]
	ldrh	r3, [r3, #2384]
	str	r3, [sp, #4]
.L1096:
	sxth	r1, r2
	cmp	r1, ip
	bge	.L1097
	add	r1, r1, r8
	str	r2, [sp, #12]
	str	ip, [sp, #8]
	ldrb	r0, [r1, #2344]	@ zero_extendqisi2
	mov	r1, fp
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	ip, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L1089
	ldr	r3, [sp, #4]
	add	r6, r6, r3
	uxth	r6, r6
.L1089:
	adds	r2, r2, #1
	b	.L1096
.L1097:
	cbz	r6, .L1091
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1092
.L1091:
	ldr	r1, [r4, #292]
	sxth	r2, fp
	movw	r0, #65535
	strh	r0, [r1, r2, lsl #1]	@ movhi
.L1092:
	add	r0, r7, r7, lsl #1
	ldr	r1, [r4, #284]
	ldr	r2, .L1098+4
	add	r1, r1, r0, lsl #1
	strh	r6, [r1, #4]	@ movhi
	ldrh	r1, [r4, #312]
	cmp	r7, r1
	beq	.L1093
	ldrh	r1, [r2, #360]
	cmp	r7, r1
	beq	.L1093
	ldrh	r1, [r2, #408]
	cmp	r7, r1
	beq	.L1093
	ldr	r2, [r2, #292]
	ldrh	r2, [r2, r7, lsl #1]
	cbnz	r2, .L1094
	add	r9, r9, #1
	mov	r0, fp
	uxth	r9, r9
	bl	INSERT_FREE_LIST
	b	.L1093
.L1094:
	add	r10, r10, #1
	mov	r0, fp
	uxth	r10, r10
	bl	INSERT_DATA_LIST
.L1093:
	adds	r5, r5, #1
	b	.L1088
.L1095:
	movs	r0, #0
	strh	r10, [r4, #300]	@ movhi
	strh	r9, [r4, #308]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1099:
	.align	2
.L1098:
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
	ldr	r5, .L1101
	movs	r3, #0
	ldr	r4, .L1101+4
	ldrh	r2, [r5, #2388]
	ldr	r0, [r4, #1156]
	strh	r3, [r4, #1160]	@ movhi
	lsls	r2, r2, #1
	strh	r3, [r4, #1168]	@ movhi
	bl	ftl_memset
	ldrh	r3, [r5, #2388]
	movs	r2, #12
	ldr	r0, [r4, #1164]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r3, r4, r5, lr}
	b	FtlGcBufInit
.L1102:
	.align	2
.L1101:
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
	ldr	r1, .L1105
	bl	ftl_memcpy
	movs	r0, #0
	pop	{r3, pc}
.L1106:
	.align	2
.L1105:
	.word	.LANCHOR2+3472
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
	ldr	r5, .L1119
.L1111:
	mov	r0, r5
	ldr	r1, .L1119+4
	ldrb	r2, [r5, #-1]	@ zero_extendqisi2
	bl	FlashMemCmp8
	mov	r6, r0
	cbnz	r0, .L1108
	ldr	r5, .L1119+8
	lsls	r4, r4, #5
	add	r2, r5, #508
	adds	r7, r2, r4
	beq	.L1114
	add	r4, r4, r5
	mov	r3, r0
	addw	r1, r5, #3164
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	b	.L1113
.L1108:
	adds	r4, r4, #1
	adds	r5, r5, #32
	cmp	r4, #83
	bne	.L1111
	b	.L1114
.L1118:
	adds	r3, r3, #1
	cmp	r3, #4
	beq	.L1112
.L1113:
	lsls	r0, r3, #5
	ldrb	r0, [r0, r1]	@ zero_extendqisi2
	cmp	r0, r2
	bne	.L1118
.L1112:
	ldr	r1, .L1119+12
	movs	r2, #32
	ldr	r0, .L1119+16
	add	r1, r1, r3, lsl #5
	bl	ftl_memcpy
	mov	r1, r7
	movs	r2, #32
	ldr	r0, .L1119+20
	bl	ftl_memcpy
	ldrh	r0, [r5, #482]
	bl	FlashBlockAlignInit
	b	.L1109
.L1114:
	mov	r6, #-1
.L1109:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L1120:
	.align	2
.L1119:
	.word	.LANCHOR1+509
	.word	.LANCHOR0+2068
	.word	.LANCHOR1
	.word	.LANCHOR1+3164
	.word	.LANCHOR0+44
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
	bne	.L1122
	cbz	r3, .L1123
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1123:
	cbz	r6, .L1121
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
.L1122:
	cbz	r3, .L1126
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1126:
	cbz	r6, .L1121
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
.L1121:
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
.L1138:
	cmp	r3, r2
	beq	.L1140
	ldr	r4, [r1, r3, lsl #2]
	str	r4, [r0, r3, lsl #2]
	adds	r3, r3, #1
	b	.L1138
.L1140:
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
	ldr	r3, .L1143
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1144:
	.align	2
.L1143:
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
	ldr	r4, .L1148
	ldr	r4, [r4, #1176]
	add	r4, r4, #4096
	add	r1, r1, r4
	cbz	r2, .L1147
	mov	r0, r1
	mov	r1, r5
.L1147:
	mov	r2, r3
	pop	{r4, r5, lr}
	b	ftl_memcpy
.L1149:
	.align	2
.L1148:
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
	ldr	r3, .L1154
	ldr	r0, [r3, #2424]
	bx	lr
.L1155:
	.align	2
.L1154:
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
.L1160:
	cmp	r4, r8
	beq	.L1168
	cbnz	r5, .L1161
	ldr	r0, .L1169
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1161:
	cmp	r7, #4
	ldr	r0, .L1169+4
	bne	.L1162
	ldr	r1, [r6, r4, lsl #2]
	b	.L1167
.L1162:
	cmp	r7, #2
	ite	eq
	ldrsheq	r1, [r6, r4, lsl #1]
	ldrbne	r1, [r6, r4]	@ zero_extendqisi2
.L1167:
	adds	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1165
	ldr	r0, .L1169+8
	movs	r5, #0
	ldr	r1, .L1169+12
	bl	printk
.L1165:
	adds	r4, r4, #1
	b	.L1160
.L1168:
	ldr	r0, .L1169+8
	ldr	r1, .L1169+12
	pop	{r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1170:
	.align	2
.L1169:
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
	ldr	r7, .L1280
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
	bne	.L1172
	movs	r3, #167
	strb	r3, [r7, #1214]
	ldr	r3, .L1280+4
	movs	r2, #247
	movs	r5, #7
	strb	r2, [r3, #3309]
	b	.L1233
.L1172:
	cmp	r0, #3
	bne	.L1174
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
	b	.L1274
.L1174:
	cmp	r0, #4
	bne	.L1175
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
.L1274:
	movs	r5, #8
	strb	r3, [r7, #1221]
	mov	r6, r5
	b	.L1173
.L1175:
	cmp	r0, #5
	bne	.L1176
	movs	r3, #56
	movs	r5, #8
	strb	r3, [r7, #1214]
	movs	r3, #57
	strb	r3, [r7, #1215]
	movs	r3, #58
	strb	r3, [r7, #1216]
	movs	r3, #59
	strb	r3, [r7, #1217]
	b	.L1233
.L1176:
	cmp	r0, #6
	bne	.L1177
	movs	r3, #14
	movs	r5, #12
	strb	r3, [r7, #1214]
	movs	r3, #15
	strb	r3, [r7, #1215]
	movs	r3, #16
	strb	r3, [r7, #1216]
	movs	r3, #17
	strb	r3, [r7, #1217]
	b	.L1233
.L1177:
	cmp	r0, #7
	bne	.L1178
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
	b	.L1173
.L1178:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1233
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
	b	.L1173
.L1233:
	movs	r6, #4
.L1173:
	subs	r3, r4, #1
	cmp	r3, #1
	bhi	.L1271
	ldr	r1, .L1280+8
	mov	r10, #0
.L1179:
	ldrb	r3, [r7, #2229]	@ zero_extendqisi2
	uxtb	r8, r10
	ldr	r2, .L1280
	cmp	r3, r8
	bls	.L1186
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
.L1181:
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
	bcc	.L1181
	movs	r2, #0
.L1182:
	movs	r3, #1
	add	lr, r1, r2
.L1183:
	ldrb	ip, [lr, r3, lsl #2]	@ zero_extendqisi2
	ldrb	r0, [r9]	@ zero_extendqisi2
	add	r0, r0, ip
	strb	r0, [r9, r3, lsl #3]
	adds	r3, r3, #1
	cmp	r3, #7
	bne	.L1183
	adds	r2, r2, #1
	add	r9, r9, #1
	cmp	r2, #4
	bne	.L1182
	movs	r3, #0
	add	r10, r10, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1179
.L1271:
	subs	r3, r4, #3
	cmp	r3, #5
	bhi	.L1186
	smulbb	r2, r6, r5
	lsls	r3, r2, #4
	asrs	r2, r2, #1
	str	r3, [sp, #36]
	lsls	r3, r2, #1
	str	r3, [sp, #8]
	movs	r3, #0
.L1278:
	str	r3, [sp, #20]
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r3, .L1280
	ldr	r2, [sp, #16]
	ldrb	r3, [r3, #2229]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1231
.L1186:
	ldr	r3, .L1280
	strb	r6, [r3, #1211]
	strb	r5, [r3, #1212]
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1231:
	ldr	r2, [sp, #16]
	ldr	r3, .L1280
	add	r3, r3, r2
	ldrb	fp, [r3, #2232]	@ zero_extendqisi2
	ldr	r3, .L1280
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
	bne	.L1188
	ldr	r2, .L1280+12
	movs	r3, #160
	mla	ip, r3, fp, r2
	add	r3, ip, #28
	b	.L1275
.L1188:
	cmp	r4, #8
	beq	.L1190
	ldr	r2, .L1280+12
	add	ip, r2, fp, lsl #6
	add	r3, ip, #20
.L1275:
	add	r7, r7, r10
	cmp	r4, #4
	str	r3, [sp, #24]
	mov	r3, #54
	str	r3, [r7, #2056]
	bne	.L1191
	movs	r3, #255
	str	r3, [r7, #2052]
	movs	r3, #64
	str	r3, [r7, #2048]
	movs	r3, #204
	b	.L1276
.L1191:
	subs	r3, r4, #5
	cmp	r3, #1
	bhi	.L1193
	ldr	r3, .L1280
	ldrb	r2, [r3, #1214]	@ zero_extendqisi2
	ldr	r3, [sp, #4]
	add	r3, r10, r3, lsl #8
	str	r2, [r3, #2052]
	movs	r2, #82
	str	r2, [r3, #2048]
	b	.L1192
.L1193:
	cmp	r4, #7
	bne	.L1192
	movs	r3, #174
	str	r3, [r7, #2052]
	movs	r3, #0
	str	r3, [r7, #2048]
	movs	r3, #176
.L1276:
	str	r3, [r7, #2052]
	movs	r3, #77
	str	r3, [r7, #2048]
.L1192:
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
.L1232:
	ldr	r3, [sp, #4]
	movs	r2, #48
	mov	r0, fp
	add	r3, r10, r3, lsl #8
	str	r2, [r3, #2056]
	bl	NandcWaitFlashReady
	subs	r3, r4, #5
	cmp	r3, #1
	str	r3, [sp, #28]
	bls	.L1236
	cmp	r4, #8
	beq	.L1236
	cmp	r4, #7
	ite	ne
	movne	r2, #2
	moveq	r2, #32
	b	.L1196
.L1281:
	.align	2
.L1280:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3292
	.word	.LANCHOR0+1210
.L1236:
	movs	r2, #16
.L1196:
	ldr	r3, .L1282
	ldr	r1, [sp, #4]
	ldr	r3, [r3, #1180]
	add	r0, r10, r1, lsl #8
	mov	r7, r3
.L1197:
	ldr	r1, [r0, #2048]
	strb	r1, [r7], #1
	subs	r1, r7, r3
	uxtb	r1, r1
	cmp	r1, r2
	bcc	.L1197
	cmp	r4, #8
	bne	.L1198
	movs	r2, #0
.L1200:
	ldrb	r7, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r7, #50
	beq	.L1199
	add	r7, r3, r2, lsl #2
	ldrb	r7, [r7, #1]	@ zero_extendqisi2
	cmp	r7, #5
	beq	.L1199
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1200
	b	.L1201
.L1199:
	cmp	r1, #7
	bne	.L1202
.L1201:
	ldr	r0, .L1282+4
	movs	r1, #0
	bl	printk
.L1203:
	b	.L1203
.L1198:
	cmp	r4, #7
	bne	.L1204
	movs	r2, #0
.L1206:
	ldrb	r7, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r7, #12
	beq	.L1205
	add	r7, r3, r2, lsl #2
	ldrb	r7, [r7, #1]	@ zero_extendqisi2
	cmp	r7, #10
	beq	.L1205
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1206
	b	.L1207
.L1205:
	cmp	r1, #7
	bne	.L1202
.L1207:
	ldr	r0, .L1282+4
	movs	r1, #0
	bl	printk
.L1208:
	b	.L1208
.L1204:
	cmp	r4, #6
	bne	.L1202
	subs	r2, r3, #1
	adds	r3, r3, #7
.L1209:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1202
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1202
	cmp	r2, r3
	bne	.L1209
	ldr	r0, .L1282+4
	movs	r1, #0
	bl	printk
.L1211:
	b	.L1211
.L1202:
	ldr	r1, .L1282
	ldr	r7, [sp, #36]
	ldr	r2, [r1, #1180]
	adds	r7, r2, r7
	mov	r3, r2
.L1212:
	cmp	r3, r7
	beq	.L1279
	ldr	lr, [r0, #2048]
	strb	lr, [r3], #1
	b	.L1212
.L1279:
	ldr	r3, [r1, #1180]
	movs	r0, #8
	ldr	r1, [sp, #8]
	adds	r1, r3, r1
	str	r1, [sp, #32]
.L1215:
	ldr	r7, [sp, #8]
	add	lr, r1, r7
.L1214:
	ldrh	r7, [r1]
	mvns	r7, r7
	strh	r7, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L1214
	ldr	r7, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, r7
	bne	.L1215
	str	r3, [sp, #12]
.L1216:
	movs	r7, #0
	mov	r0, r7
.L1219:
	movs	r3, #1
	mov	lr, #0
	mov	r8, #16
	lsl	r1, r3, r0
	mov	ip, lr
.L1217:
	ldr	r3, [sp, #12]
	ldrh	r9, [r3, lr]
	ldr	r3, [sp, #8]
	and	r9, r9, r1
	cmp	r9, r1
	it	eq
	addeq	ip, ip, #1
	subs	r8, r8, #1
	add	lr, lr, r3
	bne	.L1217
	cmp	ip, #8
	add	r0, r0, #1
	itt	hi
	orrhi	r7, r7, r1
	uxthhi	r7, r7
	cmp	r0, #16
	bne	.L1219
	ldr	r3, [sp, #12]
	strh	r7, [r3], #2	@ movhi
	str	r3, [sp, #12]
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #32]
	cmp	r1, r3
	bne	.L1216
	ldr	r3, .L1282
	ldr	r1, [r3, #1180]
	movs	r3, #0
	subs	r0, r1, #4
	add	r7, r1, #28
.L1221:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	it	eq
	addeq	r3, r3, #1
	cmp	r0, r7
	bne	.L1221
	cmp	r3, #7
	ble	.L1222
	ldr	r0, .L1282+8
	movs	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1282+4
	movs	r1, #0
	bl	printk
.L1223:
	b	.L1223
.L1222:
	cmp	r4, #6
	beq	.L1238
	cmp	r4, #7
	beq	.L1239
	cmp	r4, #8
	ite	ne
	movne	r0, #8
	moveq	r0, #5
	b	.L1224
.L1238:
	movs	r0, #4
	b	.L1224
.L1239:
	movs	r0, #10
.L1224:
	add	r9, r6, #-1
	ldr	r1, [sp, #24]
	movs	r7, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L1225:
	mov	ip, r1
	mov	r3, r2
.L1226:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [ip], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L1226
	adds	r7, r7, #1
	add	r2, r2, r9
	cmp	r7, r5
	add	r1, r1, r0
	blt	.L1225
	ldr	r3, [sp, #4]
	mov	r0, fp
	add	r7, r10, r3, lsl #8
	movs	r3, #255
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #28]
	cmp	r3, #1
	bhi	.L1228
	movs	r3, #54
	str	r3, [r7, #2056]
	ldr	r3, .L1282+12
	mov	r1, #-1
	ldr	r0, [sp, #16]
	ldrb	r3, [r3, #1214]	@ zero_extendqisi2
	str	r3, [r7, #2052]
	movs	r3, #0
	str	r3, [r7, #2048]
	movs	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L1229
.L1228:
	cmp	r4, #8
	ite	eq
	moveq	r3, #190
	movne	r3, #56
	str	r3, [r7, #2056]
.L1229:
	mov	r0, fp
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	b	.L1278
.L1190:
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
	ldr	r3, .L1282+16
	str	r3, [sp, #24]
	b	.L1232
.L1283:
	.align	2
.L1282:
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
	beq	.L1284
	subs	r2, r0, #1
	cmp	r2, #7
	bhi	.L1286
	b	HynixGetReadRetryDefault
.L1286:
	cmp	r0, #49
	bne	.L1287
	ldr	r0, .L1307
	movs	r2, #64
	ldr	r1, .L1307+4
	addw	r0, r0, #1214
	strb	r3, [r0, #-4]
	movs	r3, #4
	strb	r3, [r0, #-3]
	movs	r3, #15
	strb	r3, [r0, #-2]
	b	.L1305
.L1287:
	cmp	r0, #33
	beq	.L1288
	sub	r2, r0, #65
	cmp	r2, #1
	bhi	.L1289
.L1288:
	ldr	r0, .L1307
	strb	r3, [r0, #1210]
	movs	r3, #4
	b	.L1306
.L1289:
	cmp	r0, #34
	beq	.L1290
	cmp	r0, #67
	bne	.L1291
.L1290:
	ldr	r0, .L1307
	strb	r3, [r0, #1210]
	movs	r3, #5
.L1306:
	strb	r3, [r0, #1211]
	movs	r2, #45
	movs	r3, #7
	addw	r0, r0, #1214
	strb	r3, [r0, #-2]
	ldr	r1, .L1307+8
	b	.L1305
.L1291:
	cmp	r0, #35
	beq	.L1292
	cmp	r0, #68
	bne	.L1284
.L1292:
	ldr	r0, .L1307
	movs	r2, #95
	ldr	r1, .L1307+12
	addw	r0, r0, #1214
	strb	r3, [r0, #-4]
	movs	r3, #5
	strb	r3, [r0, #-3]
	movs	r3, #17
	strb	r3, [r0, #-2]
.L1305:
	b	ftl_memcpy
.L1284:
	bx	lr
.L1308:
	.align	2
.L1307:
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
	ldr	r5, .L1348
	add	r0, r5, r0, lsl #3
	ldr	r3, [r5, #2252]
	ldr	r4, [r0, #12]
	cmp	r3, #3
	bls	.L1340
	ldr	r3, [r4, #16]
	lsls	r6, r3, #29
	bpl	.L1340
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	str	r3, [sp]
	cmp	r6, #0
	beq	.L1322
	movs	r6, #0
.L1312:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1320
	ldr	r3, [r5, #2252]
	cmp	r3, #5
	bhi	.L1313
.L1316:
	adds	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1315
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1348+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1348+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1315
.L1313:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r0, r3, #18
	bpl	.L1316
	ldr	r3, [sp, #4]
	lsls	r1, r3, #14
	bpl	.L1316
.L1320:
	ldr	r3, [r5, #2288]
	ldr	r4, .L1348
	cmp	r3, #0
	beq	.L1321
	ldr	r1, [sp]
	movs	r2, #0
	ldr	r0, [r4, #2280]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #2284]
	movs	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
	b	.L1321
.L1315:
	movs	r0, #5
	movs	r1, #10
	bl	usleep_range
	b	.L1312
.L1324:
	ldr	r3, [r4, #8]
	adds	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cbnz	r3, .L1323
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1348+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1348+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1323:
	movs	r0, #5
	movs	r1, #10
	bl	usleep_range
.L1322:
	ldr	r3, [sp]
	lsls	r2, r3, #11
	bpl	.L1324
	ldr	r3, [r5, #2296]
	cbz	r3, .L1325
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1325:
	ldr	r3, [r5, #2288]
	ldr	r6, .L1348
	cbz	r3, .L1326
	ldr	r1, [sp]
	movs	r2, #1
	ldr	r0, [r6, #2280]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #2284]
	movs	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
.L1326:
	ldr	r3, [r5, #2296]
	cbz	r3, .L1321
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1321:
	movs	r3, #0
	str	r3, [r5, #2288]
	b	.L1309
.L1340:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	lsls	r3, r3, #11
	bpl	.L1340
.L1309:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, pc}
.L1349:
	.align	2
.L1348:
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
	ldr	r6, .L1394
	tst	r10, #63
	.pad #92
	sub	sp, sp, #92
	mov	r9, r0
	add	r3, r6, r0, lsl #3
	mov	r5, r1
	mov	r7, r2
	ldr	r8, [r3, #12]
	bne	.L1351
	ldr	r3, [sp, #128]
	cbnz	r3, .L1352
	add	r0, sp, #24
	movs	r1, #255
	movs	r2, #64
	bl	ftl_memset
	add	r3, sp, #24
	str	r3, [sp, #128]
.L1352:
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
	bne	.L1378
	ldr	r3, [r6, #2300]
	lsrs	r4, r7, #1
	mov	r2, r5
	mov	r1, r5
	cmp	r3, #25
	ldr	r3, [sp, #128]
	ite	cc
	movcc	lr, #64
	movcs	lr, #128
.L1355:
	cmp	r1, r4
	add	r3, r3, #4
	add	ip, r2, lr
	bcs	.L1392
	ldr	r0, [r6, #2268]
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
	b	.L1355
.L1392:
	movs	r2, #0
	ldr	r0, [r6, #2300]
	ldr	r1, [r6, #2252]
	lsrs	r7, r7, #2
	mov	fp, r2
.L1357:
	cmp	r2, r7
	bcs	.L1353
	cmp	r0, #0
	beq	.L1353
	add	r3, r2, #8
	ldr	r3, [r8, r3, lsl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	lsls	r4, r3, #29
	bmi	.L1381
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1381
	cmp	r1, #5
	bls	.L1359
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
	b	.L1391
.L1359:
	cmp	r1, #3
	bls	.L1361
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
.L1391:
	orr	r4, r3, r4, lsl #5
.L1361:
	cmp	fp, r4
	it	cc
	movcc	fp, r4
	b	.L1358
.L1381:
	mov	fp, #-1
.L1358:
	adds	r2, r2, #1
	b	.L1357
.L1378:
	mov	fp, #0
.L1353:
	movs	r3, #0
	str	r3, [r8, #16]
	b	.L1364
.L1351:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1390
	mov	fp, r4
.L1365:
	cmp	r4, r7
	bcs	.L1393
	and	ip, r4, #3
	cmp	r10, #0
	beq	.L1384
	add	r3, r10, r4, lsl #9
	b	.L1367
.L1384:
	mov	r3, r10
.L1367:
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
	b	.L1365
.L1393:
	mov	fp, #0
	b	.L1364
.L1390:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	movs	r2, #2
	mov	r3, r4
	mov	fp, r4
	bl	NandcXferStart
	str	r10, [sp, #8]
.L1370:
	cmp	r4, r7
	bcs	.L1364
	mov	r0, r9
	mov	r1, r5
	bl	NandcXferComp
	ldr	r3, [r8, #32]
	add	ip, r4, #2
	cmp	ip, r7
	str	r3, [sp, #20]
	bcs	.L1371
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
.L1371:
	ldr	r3, [sp, #20]
	lsls	r3, r3, #29
	bmi	.L1386
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, lsl #5
	cmp	fp, r3
	it	cc
	movcc	fp, r3
	b	.L1372
.L1386:
	mov	fp, #-1
.L1372:
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
	b	.L1370
.L1364:
	ldr	r3, [r6, #2252]
	cmp	r3, #5
	bls	.L1376
	cbnz	r5, .L1376
	ldr	r3, [r8]
	and	r2, r3, #139264
	cmp	r2, #139264
	ittt	eq
	moveq	fp, #-1
	orreq	r3, r3, #131072
	streq	r3, [r8]
.L1376:
	mov	r0, fp
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1395:
	.align	2
.L1394:
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
	ldr	r3, .L1398
	mov	r6, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r5, [r3, #481]	@ zero_extendqisi2
	cbnz	r0, .L1397
	ldr	r2, .L1398+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	muls	r2, r3, r2
	cmp	r1, r2
	it	cc
	movcc	r5, #4
.L1397:
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
.L1399:
	.align	2
.L1398:
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
	ldr	r4, .L1426
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #76]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #2252]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	ite	cc
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	beq	.L1414
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
	ldrb	r0, [r4, #2228]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2228]	@ zero_extendqisi2
	bl	NandcSetMode
	adds	r3, r6, #1
	bne	.L1403
.L1412:
	mov	r6, #-1
	b	.L1404
.L1403:
	mov	r2, r6
	ldr	r0, .L1426+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	itt	ls
	addls	r4, r4, r10, lsl #3
	ldrls	r3, [r4, #12]
	ldr	r4, .L1426+8
	itttt	ls
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #1184]
	adds	r3, r3, #1
	str	r3, [r4, #1184]
	cmp	r3, #2048
	bcc	.L1404
	movs	r7, #0
	str	r7, [r4, #1184]
	mov	fp, r7
	b	.L1402
.L1414:
	mov	r6, #1024
.L1402:
	movs	r4, #0
	mov	r8, #-1
	str	r4, [sp, #4]
	mov	ip, r4
	mov	r5, r4
.L1410:
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
	bhi	.L1406
	cmp	r0, #2
	bhi	.L1416
	adds	r5, r5, #1
	cmp	r5, #9
	bls	.L1416
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1408
.L1406:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1417
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1418
	str	r5, [sp, #4]
	b	.L1417
.L1416:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1407
.L1417:
	movs	r5, #0
.L1407:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1410
.L1408:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	it	cs
	movcs	r4, ip
	b	.L1409
.L1418:
	mov	r4, ip
.L1409:
	cbz	r4, .L1411
	ldr	r0, .L1426+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1411:
	cmp	r8, #0
	beq	.L1404
	ldr	r0, .L1426+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1412
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1404:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1427:
	.align	2
.L1426:
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
	bne	.L1429
	ldr	r9, .L1448+4
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1430
.L1432:
	ldrb	r3, [r10, #2244]	@ zero_extendqisi2
	ldr	r9, .L1448+4
	cbz	r3, .L1429
	b	.L1447
.L1430:
	movs	r3, #0
	mov	r0, r5
	strb	r3, [r9, #8]
	mov	r1, r6
	mov	r3, r7
	mov	r2, r8
	bl	FlashReadRawPage
	strb	fp, [r9, #8]
	adds	r3, r0, #1
	beq	.L1432
	mov	r4, r0
	b	.L1429
.L1447:
	ldr	r3, [r9, #76]
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
	beq	.L1433
	ldrb	r3, [r9, #2304]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1429
.L1433:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1429:
	ldr	r9, .L1448+8
	ldr	ip, [r9, #1188]
	cmp	ip, #0
	beq	.L1434
	adds	r2, r4, #1
	bne	.L1434
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r3, r6
	mov	r2, r5
	mov	r4, r0
	ldr	r0, .L1448
	mov	r1, r4
	bl	printk
	adds	r3, r4, #1
	bne	.L1434
	ldr	r3, .L1448+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbz	r3, .L1434
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	r4, [r9, #1188]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	r4
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1434:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1449:
	.align	2
.L1448:
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
	ldr	r4, .L1460
	mov	r6, r1
	movs	r5, #0
	ldrb	r0, [r4, #2228]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2228]	@ zero_extendqisi2
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
	beq	.L1451
	cmp	r8, #-1
	bne	.L1452
.L1451:
	ldrb	r3, [r4, #2228]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L1452
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1460
	movs	r2, #0
	strb	r2, [r3, #2244]
	b	.L1453
.L1452:
	movs	r3, #1
	strb	r3, [r4, #2244]
.L1453:
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1461:
	.align	2
.L1460:
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
	ldr	r3, .L1477
	movs	r4, #0
	ldr	r7, .L1477+4
	mov	r9, #4
	ldr	r5, .L1477+8
	mov	r8, #-1
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r7, #4]
	str	r4, [r5, #1196]
	mov	r6, r5
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #1180]
	str	r3, [r5, #1192]
	bl	flash_enter_slc_mode
.L1463:
	add	fp, r4, #1
	mov	r10, #0
.L1465:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #1180]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1464
	movs	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #1180]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1464
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1466
	b	.L1465
.L1467:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	bne	.L1473
	ldr	r8, .L1477+24
	add	r1, fp, #160
	movs	r2, #32
	add	r0, r8, #472
	bl	ftl_memcpy
	ldr	r1, [r6, #1192]
	movs	r2, #32
	ldr	r0, .L1477+12
	adds	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #1192]
	mov	r2, #852
	ldr	r0, .L1477+16
	adds	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r8, #482]
	bl	FlashBlockAlignInit
	ldr	r8, [r6, #1192]
	str	r4, [r6, #1196]
	mov	r0, r4
	ldr	r1, [r7, #4]
	ldr	r3, [r8, #1076]
	strb	r3, [r7, #2244]
	bl	__aeabi_uidiv
	adds	r0, r0, #1
	cmp	r0, #1
	itee	hi
	strhi	r0, [r6, #1200]
	movls	r3, #2
	strls	r3, [r6, #1200]
	ldrh	r3, [r8, #14]
	mov	r8, #0
	strb	r3, [r5, #1204]
.L1466:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1463
	mov	r0, r9
.L1476:
	bl	flash_exit_slc_mode
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1464:
	ldr	fp, [r5, #1192]
	ldr	r2, .L1477+20
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1466
	cmp	r8, #0
	bne	.L1467
	mov	r0, r4
	ldr	r1, [r7, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L1477+8
	adds	r0, r0, #1
	str	r0, [r3, #1200]
	mov	r0, r8
	b	.L1476
.L1473:
	mov	r8, #-1
	b	.L1466
.L1478:
	.align	2
.L1477:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR0+44
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
	ldr	r4, .L1507
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
	bls	.L1496
	ldrb	r5, [r4, #2244]	@ zero_extendqisi2
	cbz	r5, .L1497
	movs	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	movs	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
	b	.L1481
.L1497:
	str	r5, [sp, #4]
.L1481:
	ldr	r3, [sp, #12]
	movs	r1, #92
	adds	r3, r7, r3
	str	r1, [r3, #2056]
	movs	r1, #197
	str	r1, [r3, #2056]
	b	.L1480
.L1496:
	movs	r3, #0
	str	r3, [sp, #4]
.L1480:
	mov	r8, #1
	mov	r10, #-1
	lsls	r3, r2, #8
	str	r3, [sp, #20]
.L1482:
	ldr	r3, .L1507+4
	ldrb	r3, [r3, #1205]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r8, r3
	bcs	.L1506
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	mov	r0, r6
	uxtb	r1, r8
	subs	r3, r3, #67
	cmp	r3, #1
	bhi	.L1483
	bl	SandiskSetRRPara
	b	.L1484
.L1483:
	bl	ToshibaSetRRPara
.L1484:
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1485
	ldr	r3, .L1507+4
	ldrb	r3, [r3, #1205]	@ zero_extendqisi2
	subs	r3, r3, #3
	cmp	r8, r3
	itttt	eq
	moveq	r2, #179
	ldreq	r3, [sp, #20]
	addeq	r3, r7, r3
	streq	r2, [r3, #2056]
.L1485:
	ldr	r3, [sp, #12]
	movs	r2, #38
	adds	r3, r7, r3
	str	r2, [r3, #2056]
	movs	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cbz	r3, .L1486
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
	b	.L1487
.L1486:
	mov	r0, r9
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1487:
	adds	r2, r5, #1
	beq	.L1488
	ldrb	r2, [r4, #2304]	@ zero_extendqisi2
	cmp	r10, #-1
	it	eq
	moveq	r10, r5
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1490
	mov	fp, #0
	str	fp, [sp, #8]
.L1488:
	add	r8, r8, #1
	b	.L1482
.L1506:
	mov	r5, r10
.L1490:
	ldrb	r2, [r4, #1208]	@ zero_extendqisi2
	mov	r0, r6
	movs	r1, #0
	subs	r2, r2, #67
	cmp	r2, #1
	bhi	.L1492
	bl	SandiskSetRRPara
	b	.L1493
.L1492:
	bl	ToshibaSetRRPara
.L1493:
	ldr	r3, [sp, #12]
	movs	r2, #255
	add	r7, r7, r3
	str	r2, [r7, #2056]
	ldrb	r2, [r4, #2304]	@ zero_extendqisi2
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1494
	adds	r3, r5, #1
	it	ne
	movne	r5, #256
.L1494:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cbz	r3, .L1495
	movs	r0, #4
	bl	NandcSetDdrMode
.L1495:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1508:
	.align	2
.L1507:
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
	ldr	r5, .L1522
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
.L1510:
	ldr	r3, .L1522+4
	ldrb	r3, [r3, #1205]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r10, r3
	bcs	.L1513
	mov	r0, r6
	uxtb	r1, r10
	bl	SamsungSetRRPara
	mov	r2, r9
	mov	r0, r8
	mov	r1, fp
	mov	r3, r7
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1511
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1516
	movs	r7, #0
	mov	r9, r7
.L1511:
	add	r10, r10, #1
	b	.L1510
.L1516:
	mov	r4, r0
.L1513:
	mov	r0, r6
	movs	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L1515
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
.L1515:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1523:
	.align	2
.L1522:
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
	ldr	r3, .L1548
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	mov	fp, r2
	str	r1, [sp, #20]
	ldrb	r5, [r3, #2304]	@ zero_extendqisi2
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbnz	r3, .L1525
	add	r5, r5, r5, lsl #1
	ubfx	r5, r5, #2, #8
	b	.L1526
.L1525:
	ldr	r2, .L1548+4
	smull	r2, r3, r5, r2
	uxtb	r5, r3
.L1526:
	ldr	r3, .L1548
	mov	r10, #0
	add	r3, r3, r6, lsl #3
	str	r3, [sp, #24]
.L1536:
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
.L1527:
	ldr	r3, .L1548+8
	ldrb	r3, [r3, #1205]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L1530
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
	beq	.L1528
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	cmp	r0, r5
	ldr	ip, [sp, #28]
	bcc	.L1538
	mov	r8, ip
	mov	fp, ip
.L1528:
	ldr	r9, [sp, #8]
	b	.L1527
.L1538:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L1530:
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
	bcc	.L1532
	adds	r1, r4, #1
	ldr	r0, .L1548+12
	it	ne
	movne	r4, #256
	mov	r1, r9
	str	r4, [sp]
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L1534
	ldr	r3, .L1548
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbz	r3, .L1543
	adds	r2, r4, #1
	bne	.L1543
	mov	r0, r6
	movs	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L1536
.L1534:
	mov	r0, r6
	mov	r1, r7
	bl	micron_auto_read_calibration_config
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
	b	.L1543
.L1532:
	cmp	r10, #0
	beq	.L1543
	mov	r0, r6
	mov	r1, r7
	bl	micron_auto_read_calibration_config
	mov	r4, #256
.L1543:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1549:
	.align	2
.L1548:
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
	ldr	r5, .L1567
	mov	r10, r2
	mov	r7, r0
	mov	fp, r1
	adds	r2, r5, r0
	mov	r6, #-1
	ldr	r3, [r5, #1204]
	ldrb	r4, [r2, #1222]	@ zero_extendqisi2
	ldrb	r9, [r5, #1212]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #7
	cmp	r3, #1
	it	ls
	ldrbls	r4, [r2, #1230]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L1552:
	cmp	ip, r9
	bcs	.L1556
	adds	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1567+4
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
	beq	.L1554
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	cmp	r6, #-1
	it	eq
	moveq	r6, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1561
	mov	r8, #0
	mov	r10, r8
.L1554:
	add	ip, ip, #1
	b	.L1552
.L1561:
	mov	r6, r0
.L1556:
	ldr	r3, [r5, #1204]
	add	r7, r7, r5
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	ite	ls
	strbls	r4, [r7, #1230]
	strbhi	r4, [r7, #1222]
	add	r3, r3, r3, lsl #1
	cmp	r6, r3, asr #2
	bcc	.L1560
	adds	r3, r6, #1
	it	ne
	movne	r6, #256
.L1560:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1568:
	.align	2
.L1567:
	.word	.LANCHOR0
	.word	.LANCHOR0+1214
	.fnend
	.size	HynixReadRetrial, .-HynixReadRetrial
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
	ldr	r3, .L1572
	mov	r5, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r6, [r3, #481]	@ zero_extendqisi2
	cbnz	r0, .L1570
	ldr	r2, .L1572+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	muls	r1, r3, r1
	cmp	r5, r1
	bcs	.L1570
	ldrb	r3, [r2]	@ zero_extendqisi2
	cmp	r3, #0
	it	ne
	movne	r6, #4
.L1570:
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
.L1573:
	.align	2
.L1572:
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
	ldr	r4, .L1583
	ldr	r5, .L1583+4
	ldr	r8, .L1583+12
	ldr	r3, [r4, #1180]
	mov	r9, r4
	ldrb	r0, [r4, #1206]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #1192]
	bl	FlashBchSel
	movs	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #1180]
	bl	ftl_memset
	ldr	r3, [r4, #1192]
	addw	r1, r5, #2068
	movs	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #1192]
	ldrb	r3, [r5, #2229]	@ zero_extendqisi2
	adds	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #2244]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #1192]
	addw	r1, r5, #2232
	movs	r2, #8
	adds	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #1192]
	add	r1, r5, #1168
	movs	r2, #32
	adds	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #1192]
	ldr	r1, .L1583+8
	movs	r2, #32
	adds	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #1192]
	add	r1, r5, #44
	movs	r2, #32
	adds	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #1192]
	mov	r2, #852
	addw	r1, r5, #1210
	adds	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #1192]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	mov	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #1208]
	str	r3, [r4, #1192]
	str	r0, [r6, #8]
	movs	r0, #0
	bl	flash_enter_slc_mode
	movs	r6, #0
	mov	r7, r6
.L1577:
	ldr	r1, [r5, #4]
	movs	r0, #0
	mov	r2, r0
	muls	r1, r7, r1
	bl	FlashEraseBlock
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #1180]
	muls	r1, r7, r1
	mov	r3, r0
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #1180]
	muls	r1, r7, r1
	mov	r3, r0
	adds	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #1208]
	muls	r1, r7, r1
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r2, r7, #1
	adds	r0, r0, #1
	beq	.L1575
	ldr	fp, [r9, #1192]
	ldr	r3, [fp]
	cmp	r3, r8
	bne	.L1575
	add	r0, fp, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1575
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [r9, #1200]
	mul	r3, r3, r7
	str	r3, [r9, #1196]
	beq	.L1578
	movs	r6, #1
.L1575:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1577
	b	.L1576
.L1578:
	movs	r6, #2
.L1576:
	movs	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	lsrs	r0, r0, #5
	negs	r0, r0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1584:
	.align	2
.L1583:
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
	ldr	r3, .L1604
	ldr	r4, .L1604+4
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r4, #2240]
	ldrb	r10, [r4, #2304]	@ zero_extendqisi2
	str	r0, [sp, #4]	@ unaligned
	cbz	r3, .L1586
	movs	r0, #0
	bl	flash_enter_slc_mode
.L1586:
	mov	r0, r9
	movs	r1, #0
	mov	r2, #2048
	mov	r8, #-1
	bl	ftl_memset
	movs	r5, #2
.L1587:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1591
	movs	r7, #0
.L1589:
	add	r3, sp, #4
	ldr	r6, .L1604+8
	ldrb	fp, [r7, r3]	@ zero_extendqisi2
	mov	r0, fp
	bl	FlashBchSel
	ldr	r1, [r4, #4]
	movs	r0, #0
	ldr	r2, [r6, #1180]
	muls	r1, r5, r1
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1588
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L1589
	b	.L1590
.L1588:
	ldr	r3, [r6, #1180]
	ldr	r2, .L1604+12
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1590
	mov	r1, fp
	ldr	r0, .L1604+16
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #1180]
	bl	ftl_memcpy
	ldr	r3, [r6, #1180]
	ldr	r2, .L1604+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r6, #1200]
	cmp	r3, r5
	bls	.L1594
	str	r5, [r6, #1200]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1590:
	adds	r5, r5, #1
	b	.L1587
.L1594:
	mov	r8, #0
.L1591:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #2240]
	cbz	r3, .L1598
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1598:
	mov	r0, r8
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1605:
	.align	2
.L1604:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	-52655045
	.word	.LC95
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
	ldr	r6, .L1725
	ldr	r4, .L1725+4
	movs	r7, #0
	add	r9, r4, #12
	mov	r8, r6
	str	r0, [r6, #1180]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r6, #1208]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r6, #1212]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r6, #1216]
	mov	r0, #4096
	bl	ftl_malloc
	movs	r3, #50
	str	r7, [r6, #1200]
	strb	r3, [r4, #1]
	strb	r3, [r6, #1204]
	movs	r3, #128
	strb	r7, [r4, #2244]
	str	r3, [r4, #4]
	movs	r3, #60
	str	r7, [r6, #1184]
	strb	r7, [r4]
	strb	r7, [r6, #1224]
	strb	r3, [r6, #1206]
	str	r0, [r6, #1220]
	mov	r0, r5
	bl	NandcInit
	addw	r5, r4, #2068
.L1612:
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
	bne	.L1607
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
.L1607:
	mov	r0, fp
	bl	NandcFlashDeCs
	ldrb	r2, [r5]	@ zero_extendqisi2
	subs	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1608
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L1725+8
	str	r1, [sp]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	adds	r1, r7, #1
	bl	printk
.L1608:
	cbnz	r7, .L1609
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1664
	ldr	r3, .L1725+4
	ldrb	r3, [r3, #2069]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1664
.L1609:
	ldrb	r3, [r5]	@ zero_extendqisi2
	adds	r7, r7, #1
	adds	r5, r5, #8
	cmp	r3, #181
	itt	eq
	moveq	r3, #44
	strbeq	r3, [r5, #-8]
	cmp	r7, #4
	bne	.L1612
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1613
	ldr	r3, .L1725+4
	ldr	r0, [r3, #2248]
	bl	NandcSetDdrMode
.L1613:
	ldr	r6, .L1725+4
	mov	r2, #852
	movs	r1, #0
	ldr	r5, .L1725+12
	addw	r0, r6, #1210
	bl	ftl_memset
	ldr	r3, .L1725+16
	ldr	r2, [r4, #2256]
	ldrb	r0, [r4, #2069]	@ zero_extendqisi2
	cmp	r2, r5
	str	r3, [r4, #1204]
	mov	r3, #0
	strb	r3, [r4, #8]
	itt	eq
	moveq	r3, #1
	streq	r3, [r6, #2240]
	cmp	r0, #161
	beq	.L1615
	cmp	r0, #241
	beq	.L1615
	cmp	r0, #218
	beq	.L1615
	and	r3, r0, #253
	cmp	r3, #209
	beq	.L1615
	cmp	r0, #220
	bne	.L1616
	ldr	r3, .L1725+4
	ldrb	r3, [r3, #2071]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1616
.L1615:
	ldrb	r7, [r4, #2068]	@ zero_extendqisi2
	movs	r3, #16
	strb	r3, [r4, #1]
	movs	r6, #1
	strb	r3, [r8, #1206]
	cmp	r7, #152
	ldr	r3, .L1725+20
	strb	r6, [r4]
	ldr	r1, .L1725
	strb	r7, [r3, #3321]
	strb	r0, [r3, #3322]
	bne	.L1618
	ldr	r7, .L1725+4
	ldrsb	r7, [r7, #2072]
	cmp	r7, #0
	blt	.L1619
	movs	r6, #24
	strb	r6, [r1, #1206]
.L1618:
	cmp	r2, r5
	beq	.L1621
	movw	r1, #2049
	cmp	r2, r1
	bne	.L1622
.L1621:
	movs	r2, #16
	strb	r2, [r8, #1206]
.L1622:
	cmp	r0, #218
	bne	.L1623
	mov	r2, #2048
	b	.L1720
.L1623:
	cmp	r0, #220
	bne	.L1625
	mov	r2, #4096
.L1720:
	strh	r2, [r3, #3334]	@ movhi
	strb	r0, [r3, #3322]
	b	.L1624
.L1625:
	cmp	r0, #211
	itttt	eq
	moveq	r2, #4096
	strheq	r2, [r3, #3334]	@ movhi
	moveq	r2, #2
	strbeq	r2, [r3, #3333]
.L1624:
	ldr	r1, .L1725+24
	movs	r2, #32
	ldr	r0, .L1725+28
	bl	ftl_memcpy
	ldr	r0, .L1725+16
	movs	r2, #32
	add	r1, r0, #2848
	bl	ftl_memcpy
.L1616:
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r6, .L1725+4
	cmp	r3, #0
	bne	.L1626
	bl	FlashLoadPhyInfoInRam
	cbnz	r0, .L1628
	ldr	r3, [r6, #1204]
	ldrh	r3, [r3, #16]
	lsrs	r3, r3, #8
	lsls	r7, r3, #31
	and	r0, r3, #7
	strb	r0, [r6, #2228]
	bmi	.L1628
	movs	r3, #1
	strb	r3, [r6, #2244]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2228]	@ zero_extendqisi2
	bl	NandcSetMode
.L1628:
	ldr	r3, [r4, #1204]
	ldr	r6, .L1725+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #140]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1626
	ldr	r3, [r6, #2248]
	cbz	r3, .L1631
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	b	.L1721
.L1726:
	.align	2
.L1725:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LC96
	.word	1446522928
	.word	.LANCHOR1+472
	.word	.LANCHOR1
	.word	.LANCHOR1+3196
	.word	.LANCHOR0+44
.L1631:
	ldrb	r0, [r6, #2228]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2228]	@ zero_extendqisi2
.L1721:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1626
	movs	r0, #1
	ldr	r6, .L1727
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #1204]
	ldr	r0, .L1727+4
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	adds	r1, r0, #1
	beq	.L1683
	bl	FlashDieInfoInit
	ldr	r3, [r6, #1204]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r2, .L1727+8
	ldr	r3, [r6, #1204]
	ldrh	r2, [r2, #3266]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	addw	r2, r2, #4095
	cmp	r1, r2, asr #12
	blt	.L1633
	ldrh	r2, [r3, #14]
	adds	r2, r2, #255
	cmp	r1, r2, asr #8
	bge	.L1634
.L1633:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L1634:
	ldrb	r3, [r4, #2228]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1635
	bl	FlashSavePhyInfo
	movs	r0, #0
	ldr	r1, [r8, #1196]
	bl	FlashDdrParaScan
.L1635:
	bl	FlashSavePhyInfo
.L1626:
	ldr	r2, [r4, #1204]
	ldr	r7, .L1727
	ldr	r6, .L1727+12
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r9, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #140]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r4, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r8, #1225]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #2305]
	strb	r3, [r4, #2228]
	movs	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r8, #1188]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r9
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #1204]
	ldrh	r2, [r3, #16]
	lsls	r2, r2, #25
	bpl	.L1637
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r7, #1211]	@ zero_extendqisi2
	strb	r0, [r7, #1208]
	strb	r3, [r7, #1209]
	ldrb	r3, [r7, #1212]	@ zero_extendqisi2
	strb	r3, [r6, #1205]
	subs	r3, r0, #1
	cmp	r3, #7
	bhi	.L1638
	ldr	r3, .L1727+16
	str	r3, [r6, #1188]
	subs	r3, r0, #5
	cmp	r3, #1
	bls	.L1639
	cmp	r0, #8
	bne	.L1640
.L1639:
	cmp	r0, #7
	mov	r3, #1
	str	r3, [r4, #2296]
	beq	.L1665
	cmp	r0, #8
	b	.L1724
.L1665:
	ldr	r3, .L1727+20
.L1642:
	subs	r1, r3, #1
	movs	r2, #0
	adds	r3, r3, #31
.L1643:
	ldrsb	r6, [r1, #1]!
	cmp	r6, #0
	it	eq
	addeq	r2, r2, #1
	cmp	r1, r3
	bne	.L1643
	cmp	r2, #27
	bls	.L1637
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1637
.L1638:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1645
	ldr	r3, .L1727+24
	cmp	r0, #19
	str	r3, [r6, #1188]
	ite	ne
	movne	r3, #7
	moveq	r3, #15
	strb	r3, [r6, #1205]
	b	.L1637
.L1645:
	cmp	r0, #33
	beq	.L1647
	sub	r3, r0, #65
	cmp	r3, #1
	bhi	.L1648
.L1647:
	ldr	r3, .L1727+28
	str	r3, [r8, #1188]
	movs	r3, #4
	strb	r3, [r4, #1209]
	movs	r3, #7
	strb	r3, [r8, #1205]
	b	.L1637
.L1648:
	sub	r3, r0, #67
	uxtb	r3, r3
	cmp	r3, #1
	bls	.L1649
	sub	r2, r0, #34
	cmp	r2, #1
	bhi	.L1650
.L1649:
	ldr	r2, .L1727+28
	cmp	r0, #35
	str	r2, [r8, #1188]
	beq	.L1651
	cmp	r0, #68
	beq	.L1651
	ldr	r2, .L1727+12
	movs	r1, #7
	strb	r1, [r2, #1205]
	b	.L1652
.L1651:
	movs	r2, #17
	strb	r2, [r8, #1205]
.L1652:
	cmp	r3, #1
	ite	ls
	movls	r3, #4
	movhi	r3, #5
	strb	r3, [r4, #1209]
	b	.L1637
.L1650:
	cmp	r0, #49
	itt	eq
	ldreq	r3, .L1727+32
	streq	r3, [r6, #1188]
.L1637:
	ldr	r2, [r4, #2256]
	ldr	r3, .L1727
	cmp	r2, r5
	bne	.L1654
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cbz	r1, .L1654
	ldr	r3, [r3, #1204]
	movs	r1, #0
	strb	r1, [r3, #18]
.L1654:
	ldrb	r1, [r4, #2068]	@ zero_extendqisi2
	ldr	r3, .L1727
	cmp	r1, #44
	bne	.L1655
	ldrb	r1, [r3, #2244]	@ zero_extendqisi2
	cbz	r1, .L1655
	cmp	r2, r5
	bne	.L1656
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbnz	r3, .L1655
.L1656:
	movs	r0, #1
	movs	r3, #0
	strb	r3, [r4, #2244]
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
.L1655:
	ldrb	r3, [r4, #2228]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1657
	ldr	r2, .L1727
	ldrb	r2, [r2, #2244]	@ zero_extendqisi2
	cbnz	r2, .L1658
	lsls	r3, r3, #31
	bmi	.L1657
.L1658:
	movs	r0, #0
	bl	flash_enter_slc_mode
	movs	r0, #0
	ldr	r1, [r8, #1196]
	bl	FlashDdrParaScan
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1657:
	ldr	r3, [r4, #1204]
	mov	r8, #16
	ldr	r6, .L1727
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1727+36
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1727+40
	strb	r8, [r4, #1]
	bl	FlashTimingCfg
	ldr	r5, [r4, #1204]
	ldrb	r3, [r5, #12]	@ zero_extendqisi2
	ldrh	r7, [r5, #14]
	strh	r3, [r4, #120]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	str	r3, [r4, #116]
	ldr	r3, [r4, #2068]
	str	r3, [r4, #112]
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	strh	r3, [r4, #122]	@ movhi
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	strh	r7, [r4, #126]	@ movhi
	strh	r3, [r4, #124]	@ movhi
	ldrh	r3, [r5, #10]
	strh	r3, [r4, #128]	@ movhi
	ldrb	r1, [r5, #12]	@ zero_extendqisi2
	ldrh	r0, [r5, #10]
	bl	__aeabi_idiv
	strh	r0, [r4, #130]	@ movhi
	ldrb	r2, [r5, #9]	@ zero_extendqisi2
	strh	r2, [r4, #132]	@ movhi
	ldrb	r1, [r5, #9]	@ zero_extendqisi2
	ldrh	r3, [r5, #10]
	smulbb	r3, r1, r3
	mov	r1, #512
	strh	r1, [r4, #136]	@ movhi
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	strh	r1, [r4, #138]	@ movhi
	uxth	r3, r3
	ldrb	r1, [r4]	@ zero_extendqisi2
	strh	r3, [r4, #134]	@ movhi
	cmp	r1, #1
	bne	.L1660
	lsls	r3, r3, #1
	lsrs	r1, r7, #1
	lsls	r2, r2, #1
	strh	r3, [r6, #134]	@ movhi
	strb	r8, [r6, #1]
	movs	r3, #8
	strh	r1, [r6, #126]	@ movhi
	strh	r2, [r6, #132]	@ movhi
	strh	r3, [r6, #138]	@ movhi
.L1660:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	movs	r0, #0
	b	.L1683
.L1664:
	mvn	r0, #1
	b	.L1683
.L1640:
	cmp	r0, #7
.L1724:
	beq	.L1665
	ldr	r3, .L1727+44
	b	.L1642
.L1619:
	strb	r6, [r1, #1224]
	b	.L1618
.L1683:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1728:
	.align	2
.L1727:
	.word	.LANCHOR0
	.word	.LC97
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	HynixReadRetrial
	.word	.LANCHOR0+1238
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	.LANCHOR2+3472
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
	ldr	r2, .L1750
	mov	r9, r1
	mov	r8, r0
	ldr	r3, [r2, #1204]
	mov	r5, r2
	ldrb	r1, [r2, #140]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cbz	r1, .L1730
	ldr	r2, [r2, #2240]
	cbnz	r2, .L1729
.L1730:
	subs	r2, r3, #5
	cmp	r2, #2
	bls	.L1731
	cmp	r3, #68
	beq	.L1731
	cmp	r3, #35
	beq	.L1731
	cmp	r3, #19
	bne	.L1729
.L1731:
	ldr	r10, .L1750+8
	movw	r7, #65535
	ldr	r6, .L1750+4
.L1735:
	ldr	r3, [r5, #1204]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1749
	add	r3, r10, r4, lsl #1
	ldrh	r3, [r3, #1216]
	cmp	r3, r7
	bne	.L1729
	movs	r1, #255
	mov	r2, #32768
	ldr	r0, [r6, #1208]
	bl	ftl_memset
	ldr	r2, [r6, #1208]
	add	r1, r4, r9
	adds	r4, r4, #1
	mov	r0, r8
	mov	r3, r2
	uxth	r4, r4
	bl	FlashProgPage
	b	.L1735
.L1749:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1729:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1751:
	.align	2
.L1750:
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
	ldr	r3, .L1802
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
.L1753:
	ldr	r3, [sp, #16]
	cmp	r7, r3
	beq	.L1801
	ldr	r3, [sp, #16]
	add	r2, sp, #28
	mov	r0, r4
	ldr	r1, [sp, #20]
	subs	r3, r3, r7
	ldr	r8, .L1802+20
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	mov	fp, r8
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	bcc	.L1754
	mov	r3, #-1
	str	r3, [r4]
	b	.L1755
.L1754:
	add	r3, r3, r8
	mov	r9, #0
	ldrb	r6, [r3, #2232]	@ zero_extendqisi2
	mov	r0, r6
	bl	NandcWaitFlashReady
	mov	r0, r6
	bl	NandcFlashCs
.L1756:
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
	ldr	r3, .L1802+4
	ldrb	r3, [r3, #1224]	@ zero_extendqisi2
	mov	r5, r0
	cbnz	r3, .L1757
.L1760:
	adds	r3, r5, #1
	beq	.L1758
	b	.L1759
.L1757:
	mov	r0, r6
	bl	flash_read_ecc
	cmp	r0, #5
	bls	.L1760
	mov	r5, #256
	b	.L1759
.L1758:
	cmp	r9, #10
	beq	.L1779
	add	r9, r9, #1
	b	.L1756
.L1759:
	cmp	r9, #0
	it	ne
	movne	r5, #256
	b	.L1761
.L1779:
	mov	r5, #256
.L1761:
	mov	r9, #0
.L1762:
	ldr	r3, [sp, #28]
	mov	r0, r6
	ldr	r1, [fp, #4]
	add	r1, r1, r3
	bl	FlashReadCmd
	mov	r0, r6
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #8]
	cbz	r3, .L1763
	add	r3, r3, #2048
.L1763:
	ldr	r2, [r4, #12]
	cbz	r2, .L1764
	adds	r2, r2, #8
.L1764:
	str	r2, [sp]
	mov	r0, r6
	ldr	r2, [sp, #12]
	movs	r1, #0
	bl	NandcXferData
	ldr	r3, .L1802+4
	ldrb	r2, [r3, #1224]	@ zero_extendqisi2
	mov	r10, r0
	cbnz	r2, .L1765
.L1768:
	cmp	r10, #-1
	beq	.L1766
	b	.L1767
.L1765:
	mov	r0, r6
	bl	flash_read_ecc
	cmp	r0, #5
	bls	.L1768
	mov	r10, #256
	b	.L1767
.L1766:
	cmp	r9, #10
	beq	.L1783
	add	r9, r9, #1
	b	.L1762
.L1767:
	cmp	r9, #0
	it	ne
	movne	r10, #256
	b	.L1769
.L1783:
	mov	r10, #256
.L1769:
	mov	r0, r6
	bl	NandcFlashDeCs
	ldrb	r3, [r8, #2304]	@ zero_extendqisi2
	cmp	r5, r10
	add	r3, r3, r3, lsl #1
	it	cc
	movcc	r5, r10
	cmp	r5, r3, asr #2
	bls	.L1770
	adds	r0, r5, #1
	it	ne
	movne	r5, #256
	str	r5, [r4]
	b	.L1772
.L1770:
	movs	r3, #0
	str	r3, [r4]
.L1772:
	ldr	r3, [r4, #12]
	cbz	r3, .L1773
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L1773
	ldr	r2, [r3, #8]
	adds	r1, r2, #1
	bne	.L1773
	ldr	r3, [r3]
	adds	r3, r3, #1
	it	ne
	strne	r2, [r4]
.L1773:
	ldr	r3, [r4]
	adds	r2, r3, #1
	bne	.L1755
	ldr	r1, [r4, #4]
	ldr	r0, .L1802+8
	ldrb	r2, [r8, #2304]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cbz	r1, .L1775
	ldr	r0, .L1802+12
	movs	r2, #4
	movs	r3, #8
	bl	rknand_print_hex
.L1775:
	ldr	r1, [r4, #12]
	cbz	r1, .L1755
	movs	r2, #4
	ldr	r0, .L1802+16
	mov	r3, r2
	bl	rknand_print_hex
.L1755:
	adds	r7, r7, #1
	adds	r4, r4, #36
	b	.L1753
.L1801:
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1803:
	.align	2
.L1802:
	.word	.LANCHOR1
	.word	.LANCHOR4
	.word	.LC98
	.word	.LC99
	.word	.LC100
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
	ldr	r3, .L1903
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r5, .L1903+4
	str	r1, [sp, #28]
	str	r3, [sp, #24]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	str	r2, [sp, #16]
	str	r3, [sp, #32]
	ldrb	r3, [r5]	@ zero_extendqisi2
	cbz	r3, .L1847
	bl	FlashReadSlc2KPages
	b	.L1898
.L1847:
	mov	r8, r3
	str	r3, [sp, #12]
.L1805:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	cmp	r3, r2
	bcs	.L1900
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
	ldrb	r2, [r5, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	mov	r6, r0
	cmp	r3, r2
	bcc	.L1807
	mov	r3, #-1
	str	r3, [r10, r9]
	b	.L1808
.L1807:
	add	r3, r3, r5
	ldrb	r4, [r3, #2232]	@ zero_extendqisi2
	ldr	r3, .L1903+8
	mov	r0, r4
	ldrb	r3, [r3, #1225]	@ zero_extendqisi2
	cmp	r3, #0
	it	eq
	moveq	r6, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1903+4
	ldr	r3, [r3, #1204]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	subs	r3, r2, #1
	cmp	r3, #7
	bhi	.L1810
	subs	r2, r2, #7
	adds	r1, r5, r4
	cmp	r2, #1
	ldr	r2, .L1903+4
	ldrb	r3, [r1, #1222]	@ zero_extendqisi2
	add	r2, r2, r4
	it	ls
	ldrbls	r3, [r1, #1230]	@ zero_extendqisi2
	ldrb	r2, [r2, #2064]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1810
	mov	r0, r4
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	ldr	r2, .L1903+12
	bl	HynixSetRRPara
.L1810:
	mov	r0, r4
	and	r3, fp, #-2147483648
	str	r3, [sp, #20]
	bl	NandcFlashCs
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1812
	ldr	r3, [sp, #20]
	cbz	r3, .L1813
.L1812:
	ldr	r3, .L1903+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbz	r3, .L1813
	mov	r0, r4
	bl	flash_enter_slc_mode
.L1815:
	mov	fp, #0
	b	.L1814
.L1813:
	mov	r0, r4
	bl	flash_exit_slc_mode
	b	.L1815
.L1901:
	cmp	r4, #255
	beq	.L1848
.L1816:
	cbz	r6, .L1818
	ldr	r2, [r5, #4]
	mov	r0, r4
	add	r2, r2, r1
	bl	FlashReadDpCmd
.L1819:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cbz	r6, .L1817
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1817:
	ldr	r3, [r7, #12]
	mov	r0, r4
	movs	r1, #0
	ldr	r2, [sp, #24]
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	ip, r0
	cbz	r3, .L1850
	cmp	r0, #-1
	bne	.L1850
	movs	r6, #0
	strb	fp, [r5, #8]
.L1814:
	ldr	r1, [sp, #44]
	adds	r0, r1, #1
	bne	.L1816
	b	.L1901
.L1818:
	mov	r0, r4
	bl	FlashReadCmd
	b	.L1819
.L1848:
	movs	r6, #0
	b	.L1817
.L1850:
	mov	fp, ip
	cbz	r6, .L1821
	ldr	r3, .L1903+4
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
.L1821:
	mov	r0, r4
	str	ip, [sp, #36]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	ldrb	r3, [sp, #32]	@ zero_extendqisi2
	cmp	ip, #-1
	strb	r3, [r5, #8]
	bne	.L1828
	ldrb	r3, [r5, #2244]	@ zero_extendqisi2
	cbnz	r3, .L1823
.L1827:
	ldr	r3, .L1903+8
	ldr	r6, [r3, #1188]
	cbnz	r6, .L1824
	b	.L1902
.L1823:
	ldr	r3, [r5, #76]
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
	beq	.L1826
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1852
.L1826:
	ubfx	r0, r6, #8, #8
	bl	NandcSetDdrPara
	cmp	fp, #-1
	beq	.L1827
	b	.L1852
.L1824:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r6
	cmp	r0, #-1
	mov	fp, r0
	bne	.L1829
	ldr	r3, [r5, #1204]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #1
	cmp	r3, #7
	bhi	.L1830
	mov	r0, r4
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	ldr	r2, .L1903+12
	movs	r3, #0
	bl	HynixSetRRPara
.L1830:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r5, #2304]	@ zero_extendqisi2
	mov	fp, r0
	ldr	r0, .L1903+16
	mov	r3, fp
	bl	printk
	cmp	fp, #-1
	bne	.L1829
	ldrb	r6, [r5, #140]	@ zero_extendqisi2
	cbz	r6, .L1831
	ldr	r3, [sp, #16]
	mov	r0, r4
	cmp	r3, #1
	beq	.L1832
	ldr	r3, [sp, #20]
	cbz	r3, .L1833
.L1832:
	bl	flash_enter_slc_mode
	b	.L1834
.L1833:
	bl	flash_exit_slc_mode
.L1834:
	ldr	r3, .L1903+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r6, [r3, #1188]
	ldr	r3, [r7, #12]
	blx	r6
	movs	r6, #0
	mov	fp, r0
	b	.L1835
.L1902:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	fp, r0
	b	.L1835
.L1852:
	movs	r6, #0
.L1828:
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	fp, r3, asr #2
	bls	.L1837
	ldr	r3, .L1903+8
	ldr	r3, [r3, #1188]
	cbz	r3, .L1854
.L1835:
	cmp	fp, #-1
	beq	.L1831
	b	.L1837
.L1854:
	mov	fp, #256
.L1831:
	str	fp, [r10, r9]
	b	.L1838
.L1904:
	.align	2
.L1903:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR0+1214
	.word	.LC101
.L1829:
	movs	r6, #0
.L1837:
	cmp	fp, #256
	beq	.L1831
	movs	r3, #0
	str	r3, [r10, r9]
.L1838:
	ldr	r3, [r10, r9]
	adds	r2, r3, #1
	bne	.L1840
	ldr	r1, [r7, #4]
	ldr	r0, .L1905
	ldrb	r2, [r5, #2304]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cbz	r1, .L1840
	movs	r2, #4
	ldr	r0, .L1905+4
	mov	r3, r2
	bl	rknand_print_hex
.L1840:
	cbz	r6, .L1842
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r8, r3, asr #2
	bls	.L1843
	ldr	r3, .L1905+8
	ldr	r3, [r3, #1188]
	cmp	r3, #0
	it	eq
	moveq	r8, #256
.L1843:
	add	r9, r9, #36
	cmp	r8, #-1
	str	r8, [r10, r9]
	beq	.L1842
	cmp	r8, #256
	itt	ne
	movne	r3, #0
	strne	r3, [r10, r9]
.L1842:
	ldr	r3, [sp, #12]
	add	r3, r3, r6
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1844
	ldr	r3, [sp, #20]
	cbz	r3, .L1808
.L1844:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cbz	r3, .L1808
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1808:
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	str	r3, [sp, #12]
	b	.L1805
.L1900:
	movs	r0, #0
.L1898:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1906:
	.align	2
.L1905:
	.word	.LC98
	.word	.LC100
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
	ldr	r7, .L1919
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L1919+4
	movs	r2, #16
	movs	r5, #0
	mov	fp, #-1
	ldrh	r3, [r7, #126]
	addw	r0, r4, #1228
	ldrh	r6, [r7, #124]
	mov	r10, r5
	mov	r8, r7
	mov	r9, r4
	smulbb	r6, r6, r3
	bl	ftl_memset
	ldr	r3, [r4, #1212]
	uxth	r6, r6
	str	r5, [sp, #20]
	str	r3, [sp, #24]
.L1908:
	ldrb	r3, [r8, #2229]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1918
	mul	ip, r6, r7
	subs	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1909:
	cmp	r4, r3
	ble	.L1911
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
	beq	.L1910
	ldr	r2, [r9, #1212]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1910
	mov	r1, r7
	ldr	r0, .L1919+8
	mov	r2, r4
	add	r10, r10, #1
	bl	printk
	ldr	r3, .L1919+4
	uxth	r10, r10
	add	r7, r3, r7, lsl #1
	strh	r4, [r7, #1228]	@ movhi
	b	.L1911
.L1910:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1909
.L1911:
	ldrb	r3, [r8, #2229]	@ zero_extendqisi2
	adds	r5, r5, #1
	cmp	r3, r10
	it	eq
	moveq	fp, #0
	b	.L1908
.L1918:
	mov	r0, fp
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1920:
	.align	2
.L1919:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC102
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r8, .L1951+20
	movs	r7, #0
	str	r3, [sp, #12]
	mov	r6, r1
	ldr	r3, .L1951
	mov	r9, r2
	mov	r4, r0
	mov	r10, r0
	mov	fp, r8
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	str	r3, [sp, #8]
.L1922:
	cmp	r7, r6
	beq	.L1950
	subs	r3, r6, r7
	add	r2, sp, #20
	mov	r0, r10
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	bcc	.L1923
	mov	r3, #-1
	str	r3, [r10]
	b	.L1924
.L1923:
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
	ldr	r2, [sp, #8]
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
	cbz	r3, .L1925
	add	r3, r3, #2048
.L1925:
	ldr	r2, [r10, #12]
	cbz	r2, .L1926
	adds	r2, r2, #8
.L1926:
	str	r2, [sp]
	movs	r1, #1
	ldr	r2, [sp, #8]
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
.L1924:
	adds	r7, r7, #1
	add	r10, r10, #36
	b	.L1922
.L1950:
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L1949
	ldr	r8, .L1951+24
	mov	r10, #0
	mov	fp, r8
.L1929:
	cmp	r10, r6
	beq	.L1949
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L1930
	ldr	r1, [r4, #4]
	ldr	r0, .L1951+4
	bl	printk
	b	.L1931
.L1930:
	rsb	r3, r10, r6
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	mov	r7, r4
	bl	LogAddr2PhyAddr
	ldr	r2, [r8, #1216]
	movs	r3, #0
	str	r3, [r2]
	ldr	r2, [r8, #1220]
	str	r3, [r2]
	ldmia	r7!, {r0, r1, r2, r3}
	add	r5, sp, #28
	stmia	r5!, {r0, r1, r2, r3}
	ldmia	r7!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	add	r0, sp, #28
	ldr	r3, [r7]
	movs	r1, #1
	mov	r2, r9
	str	r3, [r5]
	ldr	r3, [r8, #1216]
	str	r3, [sp, #36]
	ldr	r3, [r8, #1220]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r5, [sp, #28]
	adds	r3, r5, #1
	bne	.L1932
	ldr	r0, .L1951+8
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L1932:
	ldr	r3, [r4, #12]
	cbz	r3, .L1933
	ldr	r2, [r3]
	ldr	r3, [fp, #1220]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1933
	ldr	r0, .L1951+12
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1933:
	ldr	r3, [r4, #8]
	cbz	r3, .L1931
	ldr	r2, [r3]
	ldr	r3, [fp, #1216]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1931
	ldr	r0, .L1951+16
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1931:
	add	r10, r10, #1
	adds	r4, r4, #36
	b	.L1929
.L1949:
	movs	r0, #0
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1952:
	.align	2
.L1951:
	.word	.LANCHOR1
	.word	.LC103
	.word	.LC104
	.word	.LC105
	.word	.LC106
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
	ldr	r3, .L1964
	ldr	r7, .L1964+4
	ldr	r5, .L1964+8
	ldr	r2, [r3, #3312]
	subw	r8, r7, #2458
	ldr	r9, [r3, #3336]
	mov	r10, r8
	str	r2, [r5, #1252]
	str	r9, [r5, #1256]
.L1954:
	ldrh	r3, [r8, #2338]
	cmp	r6, r3
	bcs	.L1963
	ldrh	r4, [r8, #2382]
	movw	fp, #61664
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
	subs	r4, r4, #1
	uxth	r4, r4
.L1955:
	ldrh	r3, [r10, #2382]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1957
	mla	r3, r3, r6, r4
	movs	r1, #1
	ldr	r0, .L1964+12
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [r5, #1248]
	bl	FlashReadPages
	ldr	r3, [r5, #1244]
	adds	r3, r3, #1
	beq	.L1956
	ldrh	r3, [r9]
	cmp	r3, fp
	bne	.L1956
	strh	r4, [r7]	@ movhi
	b	.L1957
.L1956:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1955
.L1957:
	adds	r6, r6, #1
	b	.L1954
.L1963:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1965:
	.align	2
.L1964:
	.word	.LANCHOR2
	.word	.LANCHOR0+2458
	.word	.LANCHOR4
	.word	.LANCHOR4+1244
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
	ldr	r3, .L1977
	cmp	r1, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #104
	sub	sp, sp, #104
	it	eq
	ldrheq	r4, [r3, #2386]
	mov	r8, r1
	it	ne
	ldrhne	r4, [r3, #2384]
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
	bne	.L1969
.L1970:
	sxth	r3, r7
	sxth	r4, r5
	cmp	r3, r4
	bgt	.L1969
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
	bne	.L1971
	ldr	r3, [sp, #44]
	adds	r3, r3, #1
	bne	.L1971
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L1971
	subs	r4, r4, #1
	uxth	r5, r4
	b	.L1970
.L1971:
	adds	r3, r4, #1
	uxth	r7, r3
	b	.L1970
.L1969:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1978:
	.align	2
.L1977:
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
	ldr	r8, .L2008+16
	ldr	r4, .L2008
	ldr	r5, .L2008+4
	ldr	r3, [r8, #3312]
	ldr	r7, [r8, #3336]
	mov	r9, r4
	str	r3, [r4, #1252]
	str	r7, [r4, #1256]
	bl	FtlBbtMemInit
	ldrh	r6, [r5, #2382]
	subs	r6, r6, #1
	uxth	r6, r6
.L1980:
	ldrh	r3, [r5, #2382]
	subs	r3, r3, #48
	cmp	r6, r3
	ble	.L1983
	movs	r1, #1
	ldr	r0, .L2008+8
	lsls	r3, r6, #10
	str	r3, [r4, #1248]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #1244]
	adds	r3, r3, #1
	bne	.L1981
	movs	r1, #1
	ldr	r3, [r9, #1248]
	ldr	r0, .L2008+8
	mov	r2, r1
	adds	r3, r3, #1
	str	r3, [r9, #1248]
	bl	FlashReadPages
.L1981:
	ldr	r3, [r4, #1244]
	adds	r3, r3, #1
	beq	.L1982
	ldrh	r2, [r7]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1982
	ldr	r3, [r7, #4]
	strh	r6, [r5, #2448]	@ movhi
	str	r3, [r5, #2456]
	ldrh	r3, [r7, #8]
	strh	r3, [r5, #2452]	@ movhi
	b	.L1983
.L1982:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L1980
.L1983:
	ldrh	r3, [r5, #2448]
	movw	r2, #65535
	ldr	r6, .L2008+4
	cmp	r3, r2
	beq	.L1997
	ldrh	r3, [r6, #2452]
	cmp	r3, r2
	beq	.L1987
	movs	r1, #1
	ldr	r0, .L2008+8
	lsls	r3, r3, #10
	str	r3, [r4, #1248]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #1244]
	adds	r3, r3, #1
	beq	.L1987
	ldrh	r2, [r7]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1987
	ldr	r3, [r7, #4]
	ldr	r2, [r6, #2456]
	cmp	r3, r2
	itttt	hi
	strhi	r3, [r6, #2456]
	ldrhhi	r2, [r6, #2452]
	ldrhhi	r3, [r7, #8]
	strhhi	r2, [r6, #2448]	@ movhi
	it	hi
	strhhi	r3, [r6, #2452]	@ movhi
.L1987:
	ldrh	r0, [r5, #2448]
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r9, #61649
	uxth	r6, r0
	adds	r0, r0, #1
	strh	r0, [r5, #2450]	@ movhi
.L1989:
	sxth	r3, r6
	cmp	r3, #0
	blt	.L1994
	ldrh	r2, [r5, #2448]
	movs	r1, #1
	ldr	r0, .L2008+8
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #1248]
	ldr	r3, [r8, #3312]
	mov	r2, r1
	str	r3, [r4, #1252]
	bl	FlashReadPages
	ldr	r3, [r4, #1244]
	adds	r3, r3, #1
	beq	.L1990
	ldrh	r3, [r7]
	cmp	r3, r9
	bne	.L1990
.L1994:
	ldrh	r2, [r7, #10]
	ldrh	r0, [r7, #12]
	ldr	r3, .L2008+4
	strh	r2, [r5, #2454]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1991
	b	.L1992
.L1990:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L1989
.L1991:
	ldr	r2, [r3, #2312]
	cmp	r0, r2
	beq	.L1992
	ldrh	r3, [r3, #2326]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L1992
	cmp	r0, r3
	bcs	.L1992
	bl	FtlSysBlkNumInit
.L1992:
	ldr	r7, .L2008+12
	movs	r6, #0
.L1995:
	ldrh	r3, [r5, #2338]
	cmp	r6, r3
	bcs	.L2007
	ldrh	r2, [r8, #3400]
	ldr	r1, [r4, #1252]
	ldr	r0, [r7, #4]!
	lsls	r2, r2, #2
	mla	r1, r6, r2, r1
	bl	ftl_memcpy
	adds	r6, r6, #1
	b	.L1995
.L2007:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1997:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L2009:
	.align	2
.L2008:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR4+1244
	.word	.LANCHOR0+2472
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
	ldr	r5, .L2095
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L2095+4
	mov	r1, r6
	ldr	r2, [r5, #2412]
	ldr	r0, [r4, #3392]
	mov	r9, r4
	strh	r6, [r5, #2430]	@ movhi
	lsls	r2, r2, #2
	strh	r6, [r4, #3448]	@ movhi
	bl	ftl_memset
	ldr	r2, [r5, #2412]
	mov	r1, r6
	ldr	r0, [r4, #3364]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2404]
	mov	r1, r6
	ldr	r0, [r4, #3376]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #2404]
	ldr	r0, [r5, #2432]
	mov	r1, r6
	lsls	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #536
	movs	r1, #255
	movs	r2, #16
	bl	ftl_memset
	ldrh	r3, [r5, #2324]
	str	r5, [sp, #12]
	mov	r5, r4
	str	r3, [sp, #4]
.L2011:
	ldr	r6, .L2095
	ldr	r2, [sp, #4]
	ldrh	r3, [r6, #2326]
	mov	r0, r6
	cmp	r3, r2
	bls	.L2053
	ldr	r1, [r4, #1144]
	mov	r10, #36
	ldrh	r8, [r6, #2316]
	movs	r7, #0
	ldr	r2, [r4, #3300]
	str	r1, [sp, #8]
	ldr	r3, [r4, #1140]
	ldrh	r1, [r6, #2396]
	str	r7, [sp]
	str	r1, [sp, #16]
	addw	r1, r6, #2344
	str	r1, [sp, #20]
.L2054:
	uxth	r1, r7
	cmp	r1, r8
	bcs	.L2090
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
	cbnz	r0, .L2012
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
.L2012:
	adds	r7, r7, #1
	b	.L2054
.L2090:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L2016
	mov	r1, r3
	ldr	r0, [r4, #3300]
	movs	r2, #1
	ldr	r8, .L2095
	bl	FlashReadPages
	movs	r3, #0
	str	r3, [sp, #8]
.L2017:
	ldrh	r3, [sp, #8]
	ldr	r2, [sp]
	cmp	r3, r2
	bcs	.L2016
	ldr	r3, [sp, #8]
	mov	r10, #36
	mul	r10, r10, r3
	ldr	r3, [r4, #3300]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r6, [r2, #4]
	adds	r3, r3, #1
	ldr	r7, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L2019
	mov	fp, #16
	movw	r3, #65535
.L2018:
	ldr	r0, [r4, #3300]
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
	bne	.L2020
	ldr	r3, [r9, #3300]
	mov	r2, #-1
	str	r2, [r3, r10]
	b	.L2019
.L2020:
	ldr	r2, [r5, #3300]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L2019
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L2018
.L2019:
	ldr	r3, [r5, #3300]
	ldr	r3, [r3, r10]
	adds	r3, r3, #1
	beq	.L2022
	ldr	r2, [r5, #504]
	ldr	r3, [r7, #4]
	adds	r1, r2, #1
	beq	.L2023
	cmp	r2, r3
	bhi	.L2024
.L2023:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r9, #504]
.L2024:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2026
	bhi	.L2027
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2025
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #12]
	ldrh	r10, [r3, #2404]
	ldrh	r0, [r2, #2430]
	ldr	r2, [r9, #3376]
	add	lr, r10, #-1
	uxth	r3, lr
	rsb	lr, r0, lr
	str	r2, [sp, #16]
	b	.L2040
.L2027:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2029
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2025
	mov	r0, r6
	b	.L2089
.L2029:
	ldr	r3, [sp, #12]
	ldrh	r0, [r4, #3448]
	ldr	r2, [r4, #3392]
	ldr	r1, [r3, #2412]
	str	r2, [sp, #16]
	uxth	lr, r1
	add	r3, lr, #-1
	rsb	lr, r0, lr
	add	lr, lr, #-1
	uxth	r3, r3
	sxth	lr, lr
.L2031:
	sxth	ip, r3
	cmp	ip, lr
	ble	.L2091
	ldr	r2, [r7, #4]
	lsl	fp, ip, #2
	mov	r10, r2
	ldr	r2, [sp, #16]
	ldr	r2, [r2, ip, lsl #2]
	cmp	r10, r2
	bls	.L2032
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L2033
	cmp	r0, r1
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r9, #3448]	@ movhi
.L2033:
	movs	r1, #0
	uxth	lr, r3
.L2034:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2092
	ldr	r0, [r5, #3392]
	sxth	r2, r2
	adds	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	ldr	r0, [r5, #3364]
	add	r10, r0, r2, lsl #1
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2, lsl #1]	@ movhi
	b	.L2034
.L2092:
	ldr	r1, [r7, #4]
	ldr	r2, [r4, #3392]
	str	r1, [r2, fp]
	sxth	r1, r3
	ldr	r2, [r4, #3364]
	cmp	r1, #0
	strh	r6, [r2, ip, lsl #1]	@ movhi
	bge	.L2036
	b	.L2025
.L2032:
	subs	r3, r3, #1
	uxth	r3, r3
	b	.L2031
.L2091:
	cmp	ip, #0
	bge	.L2067
	b	.L2025
.L2036:
	ldrh	r0, [r5, #3448]
	ldr	r2, [r8, #2412]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r1, r2
	bgt	.L2025
.L2067:
	ldr	r2, [r4, #3392]
	sxth	r3, r3
	ldr	r1, [r7, #4]
	adds	r0, r0, #1
	strh	r0, [r4, #3448]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3364]
	b	.L2087
.L2046:
	ldr	r1, [sp, #16]
	lsl	fp, ip, #2
	ldr	r2, [r7, #4]
	ldr	r1, [r1, ip, lsl #2]
	cmp	r2, r1
	bhi	.L2093
	subs	r3, r3, #1
	uxth	r3, r3
.L2040:
	sxth	ip, r3
	cmp	ip, lr
	bgt	.L2046
	b	.L2045
.L2096:
	.align	2
.L2095:
	.word	.LANCHOR0
	.word	.LANCHOR2
.L2093:
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L2042
	cmp	r0, r10
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r8, #2430]	@ movhi
.L2042:
	movs	r1, #0
	uxth	lr, r3
.L2043:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2094
	ldr	r0, [r4, #3376]
	sxth	r2, r2
	adds	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	ldr	r0, [r8, #2432]
	add	r10, r0, r2, lsl #1
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2, lsl #1]	@ movhi
	b	.L2043
.L2094:
	ldr	r2, [r5, #3376]
	ldr	r1, [r7, #4]
	str	r1, [r2, fp]
	ldr	r2, .L2097
	ldr	r2, [r2, #2432]
	strh	r6, [r2, ip, lsl #1]	@ movhi
.L2045:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2025
	ldrh	r2, [r8, #2404]
	ldrh	r1, [r8, #2430]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2025
	adds	r1, r1, #1
	ldr	r2, [r9, #3376]
	strh	r1, [r8, #2430]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r8, #2432]
.L2087:
	strh	r6, [r2, r3, lsl #1]	@ movhi
	b	.L2025
.L2026:
	ldrh	r1, [r9, #536]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2047
	strh	r6, [r5, #536]	@ movhi
	b	.L2088
.L2047:
	ldrh	r0, [r5, #540]
	cmp	r0, r2
	beq	.L2048
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2048:
	ldr	r2, [r9, #544]
	ldr	r3, [r7, #4]
	cmp	r2, r3
	bcs	.L2049
	ldrh	r3, [r5, #536]
	strh	r6, [r5, #536]	@ movhi
	strh	r3, [r5, #540]	@ movhi
	ldr	r3, [r7, #4]
.L2088:
	str	r3, [r5, #544]
	b	.L2025
.L2049:
	strh	r6, [r5, #540]	@ movhi
	b	.L2025
.L2022:
	ldrb	r1, [r8, #140]	@ zero_extendqisi2
	mov	r0, r6
	cbz	r1, .L2050
.L2089:
	movs	r1, #0
.L2050:
	bl	FtlFreeSysBlkQueueIn
.L2025:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2017
.L2016:
	ldr	r6, [sp, #4]
	adds	r6, r6, #1
	uxth	r3, r6
	str	r3, [sp, #4]
	b	.L2011
.L2053:
	ldr	r3, .L2097+4
	ldr	r5, [r3, #3364]
	ldrh	r2, [r5]
	cbz	r2, .L2055
.L2058:
	ldr	r5, [r0, #2432]
	ldr	r3, .L2097
	ldrh	r2, [r5]
	cbz	r2, .L2056
	b	.L2079
.L2055:
	ldrh	r3, [r3, #3448]
	cmp	r3, #0
	beq	.L2058
	ldr	r6, [r6, #2412]
.L2059:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bcs	.L2058
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L2059
	mov	lr, #0
.L2060:
	ldr	r5, [r0, #2412]
	sxth	r2, r3
	cmp	r2, r5
	bcs	.L2058
	ldr	r5, [r4, #3364]
	subs	r6, r2, r1
	adds	r3, r3, #1
	ldrh	r7, [r5, r2, lsl #1]
	uxth	r3, r3
	strh	r7, [r5, r6, lsl #1]	@ movhi
	ldr	r5, [r4, #3392]
	ldr	r7, [r5, r2, lsl #2]
	str	r7, [r5, r6, lsl #2]
	ldr	r5, [r4, #3364]
	strh	lr, [r5, r2, lsl #1]	@ movhi
	b	.L2060
.L2056:
	ldrh	r1, [r3, #2430]
	cbz	r1, .L2079
	ldrh	r6, [r3, #2404]
.L2063:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bge	.L2079
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L2063
	ldr	r5, .L2097
	mov	ip, #0
.L2064:
	ldrh	r6, [r0, #2404]
	sxth	r2, r3
	cmp	r2, r6
	bge	.L2079
	ldr	r6, [r5, #2432]
	rsb	lr, r1, r2
	adds	r3, r3, #1
	ldrh	r7, [r6, r2, lsl #1]
	uxth	r3, r3
	strh	r7, [r6, lr, lsl #1]	@ movhi
	ldr	r6, [r4, #3376]
	ldr	r7, [r6, r2, lsl #2]
	str	r7, [r6, lr, lsl #2]
	ldr	r6, [r5, #2432]
	strh	ip, [r6, r2, lsl #1]	@ movhi
	b	.L2064
.L2079:
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2098:
	.align	2
.L2097:
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
	ldr	r4, .L2127
	.pad #44
	sub	sp, sp, #44
	ldr	r7, .L2127+4
	ldr	r5, .L2127+8
	ldr	r3, [r4, #3312]
	ldrh	r2, [r7, #2324]
	ldr	r0, [r4, #292]
	str	r3, [r5, #1252]
	ldr	r3, [r4, #3336]
	lsls	r2, r2, #1
	str	r3, [r5, #1256]
	bl	ftl_memset
	ldrh	r0, [r4, #536]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2100
.L2111:
	mov	r0, #-1
	b	.L2101
.L2100:
	movs	r1, #1
	mov	r8, r5
	bl	FtlGetLastWrittenPage
	ldrh	r10, [r4, #536]
	uxth	r6, r0
	adds	r0, r0, #1
	strh	r0, [r4, #538]	@ movhi
.L2102:
	sxth	r3, r6
	cmp	r3, #0
	blt	.L2110
	sxth	fp, r10
	movs	r1, #1
	ldr	r0, .L2127+12
	orr	r3, r3, fp, lsl #10
	str	r3, [r5, #1248]
	ldr	r3, [r4, #3312]
	mov	r2, r1
	ldr	r9, .L2127
	str	r3, [r5, #1252]
	bl	FlashReadPages
	ldrb	r3, [r7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2103
	ldr	r3, [r8, #1256]
	ldr	r2, [r3, #12]
	cmp	r2, #0
	beq	.L2103
	ldr	ip, [r8, #1252]
	str	r3, [sp, #32]
	ldr	r3, .L2127+4
	mov	r0, ip
	str	r2, [sp, #36]
	str	ip, [sp, #28]
	ldrh	r1, [r3, #2394]
	bl	js_hash
	ldr	r2, [sp, #36]
	cmp	r2, r0
	beq	.L2103
	ldr	ip, [sp, #28]
	ldr	r3, [sp, #32]
	cbnz	r6, .L2104
	ldrh	r1, [r9, #540]
	cmp	fp, r1
	beq	.L2104
	ldr	r0, [r3]
	ldrh	r1, [r9, #536]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r3, [r3, #8]
	str	r2, [sp, #12]
	ldr	r0, .L2127+16
	str	r3, [sp, #8]
	ldr	r3, [ip]
	str	r3, [sp, #16]
	ldr	r3, [r8, #1248]
	ldr	r2, [r8, #1244]
	bl	printk
	ldr	r3, .L2127+4
	ldrh	r10, [r9, #540]
	ldrh	r6, [r3, #2386]
	b	.L2106
.L2104:
	mov	r3, #-1
	str	r3, [r5, #1244]
.L2103:
	ldr	r3, [r5, #1244]
	adds	r3, r3, #1
	beq	.L2106
	ldr	r3, [r4, #3312]
	ldr	r2, .L2127+20
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2106
	ldr	r3, .L2127
	ldr	r3, [r3, #3336]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2106
.L2110:
	ldr	r6, .L2127
	movs	r2, #48
	ldr	r1, [r5, #1252]
	add	r0, r6, #236
	ldr	r8, .L2127+4
	bl	ftl_memcpy
	ldrh	r2, [r7, #2324]
	ldr	r1, [r5, #1252]
	ldr	r0, [r4, #292]
	adds	r1, r1, #48
	lsls	r2, r2, #1
	bl	ftl_memcpy
	ldrh	r2, [r7, #2324]
	ldr	r3, [r5, #1252]
	add	r1, r2, #24
	ldr	r0, [r4, #468]
	ubfx	r1, r1, #1, #30
	lsrs	r2, r2, #3
	adds	r2, r2, #4
	add	r1, r3, r1, lsl #2
	bl	ftl_memcpy
	ldrh	r3, [r7, #2428]
	cbz	r3, .L2108
	ldrh	r2, [r8, #2324]
	ldr	r0, [r6, #3388]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldr	r2, .L2127+8
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	ldr	r1, [r2, #1252]
	ldrh	r2, [r8, #2420]
	add	r1, r1, r3, lsl #2
	lsls	r2, r2, #2
	bl	ftl_memcpy
	b	.L2108
.L2106:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L2102
.L2108:
	ldr	r2, [r4, #236]
	ldr	r3, .L2127+20
	ldr	r6, .L2127
	cmp	r2, r3
	bne	.L2111
	ldrb	r0, [r6, #246]	@ zero_extendqisi2
	ldrh	r1, [r7, #2338]
	ldrh	r2, [r6, #244]
	cmp	r0, r1
	ldr	r3, .L2127+4
	strh	r2, [r6, #542]	@ movhi
	bne	.L2111
	ldrh	r1, [r3, #2384]
	ldrh	r0, [r3, #2390]
	str	r2, [r5, #1280]
	muls	r1, r2, r1
	str	r1, [r3, #2444]
	muls	r1, r0, r1
	ldrh	r0, [r3, #2454]
	str	r1, [r3, #2424]
	ldr	r1, [r3, #2328]
	subs	r0, r1, r0
	ldrh	r1, [r3, #2316]
	subs	r0, r0, r2
	bl	__aeabi_uidiv
	ldrh	r3, [r6, #252]
	ldrh	r1, [r6, #250]
	lsrs	r2, r3, #6
	strh	r2, [r6, #314]	@ movhi
	ldrh	r2, [r6, #254]
	and	r3, r3, #63
	strb	r3, [r6, #318]
	ldrb	r3, [r6, #247]	@ zero_extendqisi2
	strh	r2, [r6, #360]	@ movhi
	ldrh	r2, [r6, #256]
	strb	r3, [r6, #320]
	movw	r3, #65535
	strh	r1, [r6, #312]	@ movhi
	strh	r3, [r6, #552]	@ movhi
	movs	r3, #0
	strh	r3, [r6, #554]	@ movhi
	strb	r3, [r6, #558]
	strb	r3, [r6, #560]
	str	r3, [r6, #492]
	strh	r0, [r6, #532]	@ movhi
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r6, #366]
	ldrb	r2, [r6, #248]	@ zero_extendqisi2
	strh	r0, [r6, #362]	@ movhi
	strb	r2, [r6, #368]
	ldrh	r2, [r6, #258]
	strh	r2, [r6, #408]	@ movhi
	ldrh	r2, [r6, #260]
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r6, #414]
	ldrb	r2, [r6, #249]	@ zero_extendqisi2
	strh	r0, [r6, #410]	@ movhi
	strb	r2, [r6, #416]
	str	r3, [r6, #480]
	ldr	r2, [r6, #268]
	str	r3, [r6, #472]
	str	r3, [r6, #488]
	str	r3, [r6, #516]
	str	r3, [r6, #524]
	str	r3, [r6, #484]
	ldr	r3, [r6, #276]
	str	r2, [r6, #512]
	ldr	r2, [r6, #504]
	cmp	r3, r2
	ldr	r2, [r4, #508]
	it	hi
	strhi	r3, [r6, #504]
	ldr	r3, [r4, #272]
	cmp	r3, r2
	itt	hi
	ldrhi	r2, .L2127
	strhi	r3, [r2, #508]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2114
	ldr	r0, .L2127+24
	bl	make_superblock
.L2114:
	ldrh	r2, [r4, #360]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2115
	ldr	r0, .L2127+28
	bl	make_superblock
.L2115:
	ldrh	r2, [r4, #408]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2116
	ldr	r0, .L2127+32
	bl	make_superblock
.L2116:
	ldrh	r2, [r4, #552]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2117
	ldr	r0, .L2127+36
	bl	make_superblock
.L2117:
	movs	r0, #0
.L2101:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2128:
	.align	2
.L2127:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR4+1244
	.word	.LC107
	.word	1179929683
	.word	.LANCHOR2+312
	.word	.LANCHOR2+360
	.word	.LANCHOR2+408
	.word	.LANCHOR2+552
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
	ldr	r4, .L2145
	ldr	r7, .L2145+4
	ldr	r1, .L2145+8
	ldrh	r9, [r4, #2384]
	mov	r6, r0
	ldr	r0, .L2145+12
	bl	printk
	ldr	r3, [r7, #292]
	mov	r1, r6
	ldr	r0, .L2145+16
	ldrh	r2, [r3, r6, lsl #1]
	bl	printk
	add	r0, sp, #96
	strh	r6, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r5, [r4, #140]	@ zero_extendqisi2
	str	r7, [sp, #44]
	cbz	r5, .L2130
	cmp	r8, #0
	bne	.L2141
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r5, r0
	bne	.L2130
	ldrh	r9, [r4, #2386]
	b	.L2130
.L2141:
	movs	r5, #0
.L2130:
	ldr	r0, .L2145+20
	mov	r1, r5
	mov	r2, r9
	ldrh	r3, [r4, #2384]
	bl	printk
	ldr	r10, .L2145
	movs	r6, #0
.L2131:
	ldr	r3, .L2145+4
	movs	r2, #0
	ldrh	fp, [r10, #2316]
	add	lr, sp, #62
	ldrh	r0, [r10, #2396]
	mov	r8, #36
	ldr	r3, [r3, #3300]
	mov	r4, r2
	movw	ip, #65535
	str	r3, [sp, #28]
	ldr	r3, .L2145+4
	ldr	r3, [r3, #1140]
	str	r3, [sp, #32]
	ldrh	r3, [r10, #2394]
	str	r3, [sp, #36]
	ldr	r3, .L2145+4
	ldr	r3, [r3, #1144]
	str	r3, [sp, #40]
.L2132:
	uxth	r3, r2
	cmp	r3, fp
	bcs	.L2143
	ldrh	r3, [lr, #2]!
	cmp	r3, ip
	beq	.L2133
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
.L2133:
	adds	r2, r2, #1
	b	.L2132
.L2143:
	ldr	r3, .L2145+4
	mov	r1, r4
	mov	r2, r5
	mov	r8, #0
	mov	fp, #36
	ldr	r0, [r3, #3300]
	bl	FlashReadPages
.L2137:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2144
	ldr	r3, [sp, #44]
	mul	r2, fp, r8
	ldrh	r1, [sp, #48]
	ldr	ip, [r3, #3300]
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
	ldr	r0, .L2145+24
	str	r3, [sp, #20]
	ldr	r2, [ip, r2]
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2137
.L2144:
	adds	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2131
.L2139:
	movs	r0, #0
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2146:
	.align	2
.L2145:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+16
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.word	.LC111
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
	ldr	r0, .L2163
	mov	r8, #0
	ldr	r1, .L2163+4
	bl	printk
	ldr	r5, .L2163+8
	mov	r7, r5
.L2148:
	ldr	r3, .L2163+12
	uxth	r4, r8
	ldrh	r3, [r3, #2326]
	cmp	r3, r4
	bls	.L2159
	add	r9, sp, #88
	mov	r0, r4
	mov	fp, #36
	movw	r10, #65535
	strh	r4, [r9, #-48]!	@ movhi
	bl	ftl_get_blk_mode
	ldr	r2, [r5, #292]
	mov	r1, r4
	ldrh	r2, [r2, r4, lsl #1]
	mov	r3, r0
	ldr	r0, .L2163+16
	bl	printk
	mov	r0, r9
	bl	make_superblock
	ldr	r3, .L2163+12
	movs	r2, #0
	ldr	r9, [r5, #1144]
	ldrh	lr, [r3, #2316]
	mov	r4, r2
	ldr	r3, [r5, #3300]
	str	r3, [sp, #28]
	add	ip, sp, #54
	ldr	r3, [r5, #1140]
	str	r3, [sp, #32]
	ldr	r3, .L2163+12
	ldrh	r0, [r3, #2394]
	ldrh	r3, [r3, #2396]
	str	r3, [sp, #36]
.L2149:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L2160
	ldrh	r3, [ip, #2]!
	cmp	r3, r10
	beq	.L2150
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
.L2150:
	adds	r2, r2, #1
	b	.L2149
.L2160:
	ldr	r0, [r7, #3300]
	mov	r1, r4
	movs	r2, #0
	mov	r9, #0
	bl	FlashReadPages
	mov	r10, #36
.L2154:
	uxth	r3, r9
	cmp	r3, r4
	bcs	.L2161
	mul	r2, r10, r9
	ldr	ip, [r7, #3300]
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
	ldr	r0, .L2163+20
	str	r3, [sp, #20]
	ldr	r2, [ip, r2]
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2154
.L2161:
	ldr	r0, [r7, #3300]
	mov	r1, r4
	movs	r2, #1
	mov	r9, #0
	bl	FlashReadPages
	mov	r10, #36
.L2156:
	uxth	r3, r9
	cmp	r3, r4
	bcs	.L2162
	mul	r2, r10, r9
	ldr	ip, [r7, #3300]
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
	ldr	r0, .L2163+24
	str	r3, [sp, #20]
	ldr	r2, [ip, r2]
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2156
.L2162:
	add	r8, r8, #1
	b	.L2148
.L2159:
	movs	r0, #0
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2164:
	.align	2
.L2163:
	.word	.LC108
	.word	.LANCHOR3+36
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC112
	.word	.LC113
	.word	.LC114
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	1
	.global	ftl_scan_all_ppa
	.thumb
	.thumb_func
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r6, .L2171
	movs	r7, #0
	ldr	r0, .L2171+4
	ldr	r4, .L2171+8
	ldrh	r1, [r6, #2382]
	ldrh	r2, [r6, #2384]
	bl	printk
	ldr	r8, .L2171+28
.L2166:
	ldrh	r3, [r6, #2382]
	cmp	r7, r3
	bcs	.L2168
	lsl	r9, r7, #10
	movs	r5, #0
.L2169:
	ldrh	r3, [r6, #2384]
	cmp	r5, r3
	bcs	.L2170
	add	r3, r5, r9
	str	r3, [r4, #1248]
	ldr	r3, [r8, #3312]
	movs	r2, #0
	movs	r1, #1
	ldr	r0, .L2171+12
	str	r2, [r4, #1244]
	adds	r5, r5, #1
	str	r3, [r4, #1252]
	ldr	r3, [r8, #3336]
	str	r3, [r4, #1256]
	bl	FlashReadPages
	ldr	r3, [r4, #1256]
	ldr	r2, [r4, #1252]
	ldr	r0, .L2171+16
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	ldr	r1, [r4, #1248]
	str	r2, [sp, #16]
	ldr	r2, [r4, #1244]
	ldr	r3, [r3]
	bl	printk
	b	.L2169
.L2170:
	adds	r7, r7, #1
	b	.L2166
.L2168:
	ldr	r0, .L2171+20
	ldr	r1, .L2171+24
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L2172:
	.align	2
.L2171:
	.word	.LANCHOR0
	.word	.LC115
	.word	.LANCHOR4
	.word	.LANCHOR4+1244
	.word	.LC116
	.word	.LC117
	.word	.LANCHOR3+52
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
	ldr	r5, .L2228
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	r6, [r5, #1204]
	ldrb	r7, [r5]	@ zero_extendqisi2
	ldrb	r6, [r6, #19]	@ zero_extendqisi2
	str	r6, [sp, #16]
	ldr	r6, .L2228+4
	ldrb	r6, [r6, #481]	@ zero_extendqisi2
	str	r6, [sp, #12]
	cbz	r7, .L2174
	bl	FlashProgSlc2KPages
	b	.L2175
.L2188:
	movs	r3, #36
	add	r2, sp, #28
	mov	r1, r9
	mul	r8, r3, r7
	ldr	r3, [sp, #8]
	subs	r3, r3, r7
	add	fp, r4, r8
	uxtb	r3, r3
	str	r3, [sp]
	mov	r0, fp
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldrb	r2, [r5, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #32]
	mov	r10, r0
	cmp	r3, r2
	bcc	.L2176
	mov	r3, #-1
	str	r3, [r4, r8]
.L2177:
	adds	r7, r7, #1
.L2174:
	ldr	r3, [sp, #8]
	ldr	r6, .L2228
	cmp	r7, r3
	bcc	.L2188
	b	.L2226
.L2176:
	add	r3, r5, r3, lsl #4
	ldrb	r1, [r5, #2305]	@ zero_extendqisi2
	ldr	r3, [r3, #2108]
	cmp	r1, #0
	it	eq
	moveq	r10, #0
	cbz	r3, .L2179
	cmp	r2, #1
	bne	.L2180
	ldr	r0, [r5, #76]
	bl	NandcIqrWaitFlashReady
.L2180:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L2179:
	ldr	r3, [sp, #32]
	movs	r0, #0
	add	r2, r5, r3, lsl #4
	addw	r1, r2, #2108
	str	r0, [r2, #2112]
	ldr	r0, [sp, #28]
	str	fp, [r2, #2108]
	str	r0, [r2, #2104]
	cmp	r10, #0
	beq	.L2181
	add	r2, r8, #36
	add	r2, r2, r4
	str	r2, [r1, #4]
.L2181:
	adds	r2, r5, r3
	add	r3, r5, r3, lsl #4
	ldrb	r6, [r2, #2232]	@ zero_extendqisi2
	strb	r6, [r3, #2100]
	mov	r0, r6
	ldrb	r3, [r5, #2229]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2182
	bl	NandcWaitFlashReady
	b	.L2183
.L2182:
	bl	NandcFlashCs
	mov	r0, r6
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #1168]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	bl	NandcFlashDeCs
.L2183:
	ldr	r3, [sp, #16]
	subs	r3, r3, #1
	cmp	r3, #7
	bhi	.L2184
	adds	r3, r5, r6
	ldrb	r3, [r3, #2064]	@ zero_extendqisi2
	cbz	r3, .L2184
	mov	r0, r6
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	ldr	r2, .L2228+8
	movs	r3, #0
	bl	HynixSetRRPara
.L2184:
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r6
	bne	.L2185
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cbz	r3, .L2185
	bl	flash_enter_slc_mode
	b	.L2186
.L2185:
	bl	flash_exit_slc_mode
.L2186:
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r0, r6
	movs	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L2187
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	mov	r0, r6
	add	r8, r8, #36
	add	r8, r8, r4
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #1168]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r5, #4]
	mov	r0, r6
	ldr	r3, [sp, #28]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r8, #12]
	mov	r0, r6
	movs	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r8, #8]
	bl	NandcXferData
.L2187:
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2177
.L2226:
	ldr	r0, [r6, #76]
	movs	r5, #0
	bl	NandcIqrWaitFlashReady
	addw	r8, r6, #2100
	mov	r7, r6
.L2189:
	ldrb	r3, [r6, #2229]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L2227
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2190
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbz	r3, .L2190
	lsls	r3, r5, #4
	ldrb	r0, [r8, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2190:
	adds	r5, r5, #1
	b	.L2189
.L2227:
	ldr	r3, [sp, #20]
	cbnz	r3, .L2192
.L2200:
	movs	r0, #0
	b	.L2175
.L2192:
	ldr	r7, .L2228+12
	mov	r8, #0
	mov	r10, r7
.L2193:
	ldr	r3, [sp, #8]
	cmp	r8, r3
	beq	.L2200
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L2194
	ldr	r1, [r4, #4]
	ldr	r0, .L2228+16
	bl	printk
	b	.L2195
.L2194:
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
	ldr	r2, [r7, #1216]
	movs	r3, #0
	str	r3, [r2]
	ldr	r2, [r7, #1220]
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
	ldr	r3, [r7, #1216]
	str	r3, [sp, #44]
	ldr	r3, [r7, #1220]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r5, [sp, #36]
	adds	r3, r5, #1
	bne	.L2196
	ldr	r0, .L2228+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L2196:
	ldr	r3, [r4, #12]
	cbz	r3, .L2197
	ldr	r2, [r3]
	ldr	r3, [r10, #1220]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2197
	ldr	r0, .L2228+24
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2197:
	ldr	r3, [r4, #8]
	cbz	r3, .L2195
	ldr	r2, [r3]
	ldr	r3, [r10, #1216]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2195
	ldr	r0, .L2228+28
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2195:
	add	r8, r8, #1
	adds	r4, r4, #36
	b	.L2193
.L2175:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2229:
	.align	2
.L2228:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1214
	.word	.LANCHOR4
	.word	.LC103
	.word	.LC104
	.word	.LC105
	.word	.LC106
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
	ldr	r5, .L2233
	.pad #108
	sub	sp, sp, #108
	ldr	r3, [r5, #1200]
	cmp	r0, r3
	bcc	.L2232
	ldr	r3, [r5, #1208]
	add	r0, sp, #40
	movs	r1, #165
	movs	r2, #32
	str	r0, [sp, #16]
	lsls	r4, r4, #10
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r0, [r5, #1208]
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
	b	.L2231
.L2232:
	movs	r4, #0
.L2231:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, pc}
.L2234:
	.align	2
.L2233:
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
	ldr	r5, .L2293
	movs	r1, #1
	ldr	r4, .L2293+4
	ldr	r0, .L2293+8
	ldr	r3, [r5, #1212]
	ldrh	r8, [r4, #124]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #126]
	smulbb	r8, r8, r3
	ldr	r3, [r4, #1204]
	ldrb	r3, [r3, #24]	@ zero_extendqisi2
	uxth	r8, r8
	str	r3, [sp, #24]
	ldrh	r3, [r4, #4]
	str	r3, [sp, #16]
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r4, .L2293+4
	cmp	r3, #1
	itttt	eq
	ldreq	r3, [sp, #16]
	lsleq	r3, r3, #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r5, #1212]
	movs	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r5, .L2293
	lsr	r3, r8, #4
	str	r3, [sp, #28]
	movs	r3, #0
	str	r3, [sp, #8]
.L2237:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2289
	add	r3, r5, r7, lsl #1
	ldrh	r6, [r3, #1228]
	cmp	r6, #0
	bne	.L2267
	ldrh	r2, [r4, #132]
	mov	r1, r6
	ldr	r0, [r5, #1180]
	mov	r9, r6
	lsls	r2, r2, #9
	bl	ftl_memset
	adds	r3, r4, r7
	ldrb	r10, [r3, #2232]	@ zero_extendqisi2
	lsls	r3, r7, #2
	add	fp, r4, r3
	str	r6, [sp, #4]
	str	r3, [sp, #32]
.L2239:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L2249
	movs	r3, #255
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	lsls	r1, r3, #31
	bpl	.L2241
	ldr	r3, [fp, #1168]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r3, r6
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L2241
	ldr	r1, [r4, #4]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r1, r3
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	ands	r3, r3, r2
	strb	r3, [sp, #42]
.L2241:
	ldr	r3, [sp, #24]
	lsls	r2, r3, #30
	bpl	.L2243
	ldr	r3, [r4, #1204]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1168]
	subs	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2243:
	ldr	r3, [r4, #1204]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L2244
	cmp	r2, #8
	bne	.L2245
.L2244:
	cbz	r3, .L2266
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	clz	r0, r0
	lsrs	r0, r0, #5
	b	.L2246
.L2245:
	cmp	r3, #255
	bne	.L2266
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	it	ne
	movne	r0, #1
	b	.L2246
.L2266:
	movs	r0, #1
.L2246:
	ldr	r3, [sp, #24]
	lsls	r3, r3, #29
	bpl	.L2247
	ldr	r3, .L2293+4
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #1168]
	add	r1, r1, r6
	bl	SandiskProgTestBadBlock
.L2247:
	cbz	r0, .L2248
	mov	r1, r7
	ldr	r2, [sp, #4]
	ldr	r0, .L2293+12
	add	r9, r9, #1
	bl	printk
	ldr	r2, [r5, #1180]
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
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	muls	r3, r2, r3
	cmp	r9, r3
	bgt	.L2249
.L2248:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r6, r6, r3
	b	.L2239
.L2249:
	mov	r2, r9
	ldr	r0, .L2293+16
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	muls	r3, r2, r3
	cmp	r9, r3
	blt	.L2251
	ldrh	r2, [r4, #132]
	movs	r1, #0
	ldr	r0, [r5, #1180]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2251:
	cmp	r7, #0
	bne	.L2253
	ldrh	r10, [r5, #1200]
	mov	r9, #1
	mov	r6, r7
.L2254:
	ldr	r3, .L2293+4
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2290
	mov	r0, r10
	bl	FlashTestBlk
	cbz	r0, .L2255
	mov	r1, r10
	ldr	r0, .L2293+20
	bl	printk
	ldr	r2, [r5, #1180]
	lsr	r0, r10, #5
	adds	r6, r6, #1
	and	r3, r10, #31
	ldr	r1, [r2, r0, lsl #2]
	uxth	r6, r6
	lsl	r3, r9, r3
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2255:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2254
.L2290:
	add	fp, r8, #-1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L2257:
	cmp	fp, r9
	ble	.L2291
	mov	r0, fp
	bl	FlashTestBlk
	cbz	r0, .L2258
	mov	r1, fp
	ldr	r0, .L2293+20
	bl	printk
	ldr	r2, [r5, #1180]
	lsr	r0, fp, #5
	and	r3, fp, #31
	lsl	r3, r10, r3
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2258:
	add	fp, fp, #-1
	uxth	fp, fp
	b	.L2257
.L2291:
	ldr	r3, .L2293+4
	ldr	r2, [r5, #1200]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	subs	r3, r3, r2
	cmp	r6, r3
	bcc	.L2253
	ldrh	r2, [r4, #132]
	movs	r1, #0
	ldr	r0, [r5, #1180]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2253:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	add	r10, r8, #-1
	add	r9, r5, r7, lsl #1
	uxth	r10, r10
	mul	r6, r8, r6
.L2261:
	mov	r1, r7
	ldr	r0, .L2293+24
	mov	r2, r10
	bl	printk
	ldr	r1, [r5, #1180]
.L2262:
	lsr	r3, r10, #5
	and	r2, r10, #31
	ldr	r3, [r1, r3, lsl #2]
	lsrs	r3, r3, r2
	ands	r3, r3, #1
	beq	.L2292
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2262
.L2292:
	ldr	r1, [sp, #20]
	movw	r2, #61664
	strh	r10, [r9, #1228]	@ movhi
	add	r0, sp, #44
	strh	r2, [r1]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	movs	r1, #1
	ldr	r3, [r5, #1180]
	mov	r2, r1
	str	r3, [sp, #52]
	ldr	r3, [r5, #1212]
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
	cbz	r3, .L2267
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2261
.L2267:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2237
.L2289:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2294:
	.align	2
.L2293:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LC118
	.word	.LC119
	.word	.LC120
	.word	.LC121
	.word	.LC122
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
	ldr	r7, .L2347
	str	r0, [sp, #4]
	str	r1, [sp]
	ldrb	r3, [r7, #2305]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r3, .L2347+4
	ldr	r2, [r3, #220]
	mov	fp, r3
	cmp	r2, #0
	bne	.L2324
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	mov	r8, #36
	mov	r9, r2
	mov	r5, r2
	mov	r4, r2
	mov	r10, fp
	str	r3, [sp, #8]
	str	r0, [fp, #3288]
.L2297:
	ldrh	r0, [r7, #2316]
	uxth	r1, r9
	ldr	r3, .L2347
	cmp	r0, r1
	bls	.L2342
	mul	r6, r8, r1
	ldr	r0, [fp, #224]
	movs	r3, #0
	str	r3, [r0, r6]
	ldr	r3, .L2347
	add	r1, r1, r3
	ldrb	r0, [r1, #2344]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	bl	V2P_block
	ldr	r3, [sp]
	mov	r6, r0
	cbz	r3, .L2298
	bl	IsBlkInVendorPart
	cbnz	r0, .L2299
.L2298:
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2300
	ldr	r3, [fp, #224]
	lsls	r6, r6, #10
	ldrh	r1, [r7, #2396]
	muls	r1, r5, r1
	it	mi
	addmi	r1, r1, #3
	mla	lr, r8, r5, r3
	adds	r5, r5, #1
	bic	r1, r1, #3
	uxth	r5, r5
	str	r0, [lr, #8]
	ldr	r0, [r10, #3340]
	str	r6, [lr, #4]
	add	r1, r1, r0
	str	r1, [lr, #12]
	b	.L2299
.L2300:
	adds	r4, r4, #1
	uxth	r4, r4
.L2299:
	add	r9, r9, #1
	b	.L2297
.L2342:
	cmp	r5, #0
	beq	.L2323
	ldr	r2, [sp, #8]
	mov	r8, #0
	ldr	r0, [fp, #224]
	mov	r9, #36
	adds	r6, r2, #0
	mov	r2, r5
	it	ne
	movne	r6, #1
	strb	r8, [r3, #2305]
	str	r3, [sp, #8]
	mov	r1, r6
	bl	FlashEraseBlocks
	ldrb	r2, [sp, #16]	@ zero_extendqisi2
	ldr	r3, [sp, #8]
	strb	r2, [r3, #2305]
.L2304:
	uxth	r2, r8
	cmp	r2, r5
	bcs	.L2343
	mul	r2, r9, r8
	ldr	r1, [fp, #224]
	adds	r0, r1, r2
	ldr	r2, [r1, r2]
	adds	r2, r2, #1
	bne	.L2305
	ldr	r0, [r0, #4]
	adds	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2305:
	add	r8, r8, #1
	b	.L2304
.L2343:
	ldr	r3, [sp]
	cbnz	r3, .L2307
	movs	r3, #6
	uxth	r6, r6
	str	r3, [sp, #12]
	movs	r3, #1
	str	r3, [sp, #8]
	b	.L2308
.L2307:
	ldrh	r3, [r7, #2386]
	str	r3, [sp, #8]
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbnz	r3, .L2325
	ldr	r3, [sp, #8]
	movs	r6, #1
	lsrs	r3, r3, #2
	str	r3, [sp, #12]
	b	.L2308
.L2325:
	movs	r6, #1
	str	r6, [sp, #12]
.L2308:
	ldr	r8, .L2347+4
	movs	r7, #0
	mov	r10, r8
.L2318:
	mov	fp, #0
	mov	r5, fp
.L2309:
	ldr	r9, .L2347
	uxth	r3, fp
	ldrh	r1, [r9, #2316]
	cmp	r1, r3
	bls	.L2344
	movs	r2, #36
	ldr	r0, [r8, #224]
	mul	r1, r2, r3
	movs	r2, #0
	str	r2, [r0, r1]
	ldr	r2, .L2347
	ldr	r1, [sp, #4]
	add	r3, r3, r2
	ldrb	r0, [r3, #2344]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp]
	mov	r9, r0
	cbz	r3, .L2310
	bl	IsBlkInVendorPart
	cbnz	r0, .L2311
.L2310:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2311
	ldr	r1, [r10, #224]
	movs	r3, #36
	ldr	r0, [r8, #3332]
	mla	r1, r3, r5, r1
	add	r3, r7, r9, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [r10, #3328]
	str	r3, [r1, #8]
	ldr	r3, .L2347
	ldrh	r3, [r3, #2396]
	muls	r3, r5, r3
	add	r5, r5, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r5, r5
	add	r3, r3, r0
	str	r3, [r1, #12]
.L2311:
	add	fp, fp, #1
	b	.L2309
.L2344:
	cmp	r5, #0
	beq	.L2323
	mov	r2, r6
	movs	r3, #1
	ldr	r0, [r8, #224]
	mov	r1, r5
	mov	fp, #0
	strb	fp, [r9, #2305]
	bl	FlashProgPages
	movs	r2, #36
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r9, #2305]
	ldr	r9, .L2347+4
.L2315:
	uxth	r3, fp
	cmp	r3, r5
	bcs	.L2345
	mul	r3, r2, fp
	ldr	r1, [r10, #224]
	adds	r0, r1, r3
	ldr	r3, [r1, r3]
	cbz	r3, .L2316
	ldr	r0, [r0, #4]
	adds	r4, r4, #1
	str	r2, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #20]
.L2316:
	add	fp, fp, #1
	b	.L2315
.L2345:
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	ldr	r3, [sp, #8]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L2318
	movs	r7, #0
	mov	r8, #36
.L2319:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L2346
	ldr	r3, [sp]
	cbz	r3, .L2320
	mul	r3, r8, r7
	ldr	r2, [r9, #224]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L2320
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2320:
	adds	r7, r7, #1
	b	.L2319
.L2346:
	ldr	r3, [sp, #4]
	cmp	r3, #63
	bls	.L2322
	ldr	r3, [sp]
	cbz	r3, .L2323
.L2322:
	ldr	r0, [r9, #224]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L2323:
	mov	r0, r4
	b	.L2296
.L2324:
	movs	r0, #0
.L2296:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2348:
	.align	2
.L2347:
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
	ldr	r7, .L2366
	ldr	r6, [r7, #220]
	cmp	r6, #0
	bne	.L2351
	ldr	r4, .L2366+4
	mov	r1, r6
	ldr	r0, [r7, #3312]
	ldr	r5, .L2366+8
	addw	r9, r4, #2472
	ldr	r3, [r7, #3336]
	ldrh	r2, [r4, #2394]
	str	r0, [r5, #1252]
	str	r3, [r5, #1256]
	bl	ftl_memset
.L2352:
	ldrh	r3, [r4, #2338]
	ldr	r8, .L2366+4
	cmp	r6, r3
	bge	.L2365
	ldrh	r2, [r7, #3400]
	ldr	r3, [r5, #1252]
	ldr	r1, [r9, #4]!
	mul	r0, r2, r6
	lsls	r2, r2, #2
	adds	r6, r6, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L2352
.L2365:
	ldr	r6, [r5, #1256]
	movs	r1, #255
	movs	r2, #16
	ldr	fp, .L2366
	mov	r0, r6
	bl	ftl_memset
	movw	r3, #61649
	strh	r3, [r6]	@ movhi
	ldr	r3, [r8, #2456]
	str	r3, [r6, #4]
	ldrh	r3, [r8, #2448]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r8, #2452]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r8, #2454]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r8, #2312]
	mov	r8, #0
	mov	r10, r8
	strh	r3, [r6, #12]	@ movhi
.L2354:
	ldr	r3, [r7, #3312]
	mov	ip, #0
	ldrh	r1, [r4, #2448]
	ldrh	r2, [r4, #2450]
	str	r3, [r5, #1252]
	ldr	r3, [r7, #3336]
	str	ip, [r5, #1244]
	str	ip, [sp, #12]
	str	r3, [r5, #1256]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r5, #1248]
	ldrh	r3, [r4, #2452]
	str	r0, [sp]
	ldr	r0, .L2366+12
	bl	printk
	ldrh	r3, [r4, #2386]
	ldrh	r2, [r4, #2450]
	subs	r3, r3, #1
	ldr	r9, .L2366+4
	cmp	r2, r3
	ldr	ip, [sp, #12]
	blt	.L2355
	ldrh	r2, [r9, #2448]
	movs	r1, #1
	ldr	r3, [r9, #2456]
	ldr	r0, [fp, #224]
	adds	r3, r3, #1
	str	r3, [r9, #2456]
	str	r3, [r6, #4]
	strh	r2, [r6, #8]	@ movhi
	ldrh	r3, [r9, #2452]
	strh	r2, [r9, #2452]	@ movhi
	ldr	r2, .L2366+8
	strh	r3, [r9, #2448]	@ movhi
	lsls	r3, r3, #10
	strh	ip, [r9, #2450]	@ movhi
	str	r3, [r2, #1248]
	mov	r2, r1
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L2355:
	movs	r1, #1
	ldr	r9, .L2366+8
	mov	r2, r1
	mov	r3, r1
	addw	r0, r9, #1244
	bl	FlashProgPages
	ldrh	r3, [r4, #2450]
	adds	r3, r3, #1
	strh	r3, [r4, #2450]	@ movhi
	ldr	r3, [r5, #1244]
	adds	r2, r3, #1
	bne	.L2356
	add	r8, r8, #1
	ldr	r0, .L2366+16
	ldr	r1, [r9, #1248]
	uxth	r8, r8
	bl	printk
	cmp	r8, #3
	bls	.L2354
	ldr	r0, .L2366+20
	mov	r2, r8
	ldr	r1, [r9, #1248]
	bl	printk
	movs	r3, #1
	str	r3, [r7, #220]
	b	.L2351
.L2356:
	add	r10, r10, #1
	cmp	r10, #1
	beq	.L2354
	cmp	r3, #256
	beq	.L2354
.L2351:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2367:
	.align	2
.L2366:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC123
	.word	.LC124
	.word	.LC125
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
	ldr	r4, .L2424
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r4, #220]
	cmp	r3, #0
	bne	.L2369
	ldr	r9, .L2424+16
	mov	r8, r4
.L2370:
	ldr	r3, .L2424+4
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r3
	bne	.L2371
	ldrh	r7, [r8, #308]
	ldr	r6, [r8, #1128]
	lsrs	r1, r7, #1
	mul	r0, r6, r7
	adds	r3, r1, #1
	add	r3, r3, r0, lsr #2
	ldr	r0, [r9, #2308]
	uxth	r3, r3
	cbz	r0, .L2394
	ldr	r0, [r8, #528]
	cmp	r0, #39
	bhi	.L2394
	cmp	r0, #2
	bls	.L2399
	lsls	r0, r7, #31
	bpl	.L2372
	cbz	r6, .L2396
	b	.L2372
.L2371:
	cmp	r2, #1
	bne	.L2399
	ldrh	r3, [r9, #2336]
	cmp	r3, #1
	beq	.L2399
	ldrb	r3, [r9, #140]	@ zero_extendqisi2
	cbnz	r3, .L2399
	ldrh	r0, [r8, #308]
	ldr	r3, [r9, #2308]
	lsrs	r1, r0, #3
	cbz	r3, .L2372
	ldr	r3, [r8, #528]
	cmp	r3, #1
	bhi	.L2372
	rsb	r0, r0, r0, lsl #3
	ubfx	r1, r0, #3, #16
	b	.L2372
.L2394:
	mov	r1, r3
.L2372:
	cbz	r1, .L2373
	subs	r1, r1, #1
	uxth	r1, r1
	b	.L2373
.L2396:
	mov	r1, r6
	b	.L2373
.L2399:
	movs	r1, #0
.L2373:
	ldr	r0, .L2424+8
	bl	List_pop_index_node
	ldrh	r3, [r4, #308]
	subs	r3, r3, #1
	strh	r3, [r4, #308]	@ movhi
	ldrh	r3, [r9, #2324]
	uxth	r6, r0
	cmp	r3, r6
	bls	.L2370
	ldr	r3, [r8, #292]
	ldrh	r7, [r3, r6, lsl #1]
	cmp	r7, #0
	bne	.L2370
	strh	r6, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbz	r3, .L2375
	ldr	r0, [r8, #224]
	movs	r2, #36
	ldrh	ip, [r9, #2316]
	add	lr, r5, #14
	movw	r10, #65535
	mov	r3, r0
	mla	r1, r2, ip, r0
	mov	ip, r7
	str	r1, [sp, #4]
	b	.L2376
.L2375:
	ldr	r3, [r8, #292]
	b	.L2419
.L2376:
	ldr	r1, [sp, #4]
	cmp	r3, r1
	beq	.L2421
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
	b	.L2376
.L2421:
	ldr	r3, [r9, #2308]
	cbz	r3, .L2379
	ldr	r3, .L2424+12
	cmp	r5, r3
	bne	.L2379
	ldr	r3, [r8, #228]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #40
	itt	hi
	movhi	r3, #0
	strbhi	r3, [r8, #320]
.L2379:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #228]
	cbnz	r3, .L2380
	ldrh	r1, [r2, r6, lsl #1]
	cbz	r1, .L2400
	ldrh	r3, [r9, #2376]
	add	r3, r3, r1
	uxth	r3, r3
	b	.L2381
.L2400:
	movs	r3, #2
.L2381:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	mov	r0, r6
	ldr	r3, [r4, #512]
	movs	r1, #0
	adds	r3, r3, #1
	str	r3, [r4, #512]
	bl	ftl_set_blk_mode
	b	.L2382
.L2380:
	ldrh	r3, [r2, r6, lsl #1]
	lsrs	r0, r6, #5
	and	r1, r6, #31
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #516]
	ldr	r2, [r4, #468]
	adds	r3, r3, #1
	str	r3, [r4, #516]
	movs	r3, #1
	lsls	r3, r3, r1
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2382:
	ldr	r3, [r4, #228]
	ldr	r2, [r4, #524]
	ldr	r0, [r4, #512]
	ldrh	r3, [r3, r6, lsl #1]
	ldrh	r1, [r9, #2324]
	cmp	r3, r2
	ldrh	r2, [r9, #2376]
	it	hi
	strhi	r3, [r8, #524]
	ldr	r3, [r4, #516]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #3356]
	ldr	r1, [r4, #224]
	ldr	r3, [r2, #16]
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r3, #36
	adds	r2, r1, #4
	mla	r3, r3, r7, r1
	adds	r3, r3, #40
	str	r0, [r4, #520]
.L2384:
	adds	r2, r2, #36
	cmp	r2, r3
	beq	.L2422
	ldr	r1, [r2, #-36]
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r2, #-36]
	b	.L2384
.L2422:
	ldrb	r3, [r9, #140]	@ zero_extendqisi2
	cbz	r3, .L2386
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #224]
	cmp	r3, #1
	ite	eq
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2386:
	mov	r2, r7
	ldr	r0, [r4, #224]
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r10, #0
	bl	FlashEraseBlocks
	add	r2, r5, #16
	mov	ip, #36
	mov	fp, r10
.L2388:
	uxth	r3, r10
	cmp	r3, r7
	bcs	.L2423
	mul	r3, ip, r10
	ldr	r1, [r4, #224]
	adds	r0, r1, r3
	ldr	r3, [r1, r3]
	adds	r1, r3, #1
	bne	.L2389
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
.L2389:
	add	r10, r10, #1
	adds	r2, r2, #2
	b	.L2388
.L2423:
	cmp	fp, #0
	beq	.L2391
	mov	r0, r6
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2391:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L2392
	ldr	r3, [r4, #292]
.L2419:
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L2370
.L2392:
	ldr	r2, .L2424+16
	ldrh	r2, [r2, #2384]
	strh	r6, [r5]	@ movhi
	smulbb	r3, r2, r3
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #504]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #504]
	ldr	r2, [r4, #292]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
.L2369:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2425:
	.align	2
.L2424:
	.word	.LANCHOR2
	.word	.LANCHOR2+408
	.word	.LANCHOR2+304
	.word	.LANCHOR2+312
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
	ldr	r5, .L2439
	str	r0, [sp]
	ldrh	r3, [r5, #1178]
	cmp	r3, #0
	beq	.L2428
	ldr	r7, .L2439+4
	mov	r10, #0
	mov	r8, r5
.L2427:
	ldrh	r2, [r7, #2316]
	uxth	r3, r10
	cmp	r2, r3
	bls	.L2436
	add	r3, r3, r7
	ldr	r1, [sp]
	mov	fp, #0
	ldrb	r0, [r3, #2344]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r2, .L2439+8
	mov	r9, r0
.L2429:
	ldrh	r3, [r5, #1178]
	uxth	r4, fp
	cmp	r3, r4
	bls	.L2437
	lsls	r6, r4, #1
	add	r3, r8, r6
	ldrh	r3, [r3, #1180]
	cmp	r3, r9
	bne	.L2430
	mov	r1, r9
	ldr	r0, .L2439+12
	str	r2, [sp, #4]
	bl	printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r8, #1178]
	ldr	r2, [sp, #4]
	adds	r3, r2, r6
.L2431:
	cmp	r4, r1
	bcs	.L2438
	ldrh	r0, [r3, #2]
	adds	r4, r4, #1
	uxth	r4, r4
	strh	r0, [r3], #2	@ movhi
	b	.L2431
.L2438:
	subs	r1, r1, #1
	strh	r1, [r5, #1178]	@ movhi
.L2430:
	add	fp, fp, #1
	b	.L2429
.L2437:
	add	r10, r10, #1
	b	.L2427
.L2436:
	bl	FtlGcReFreshBadBlk
.L2428:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2440:
	.align	2
.L2439:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+1180
	.word	.LC126
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
	ldr	r2, .L2450
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, [r2, #292]
	mov	r5, r2
	ldrh	r3, [r3, r0, lsl #1]
	cbnz	r3, .L2442
	ldrh	r1, [r2, #552]
	cmp	r1, r0
	bne	.L2443
	movw	r3, #65535
	strh	r3, [r2, #552]	@ movhi
	b	.L2444
.L2443:
	ldrh	r1, [r2, #312]
	cmp	r1, r0
	beq	.L2449
	ldrh	r1, [r2, #360]
	cmp	r1, r0
	beq	.L2449
	ldrh	r2, [r2, #408]
	cmp	r2, r0
	beq	.L2449
.L2444:
	mov	r1, r4
	ldr	r0, .L2450+4
	bl	List_remove_node
	ldrh	r3, [r5, #300]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #300]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	movs	r0, #1
	pop	{r3, r4, r5, pc}
.L2442:
	bl	List_update_data_list
.L2449:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L2451:
	.align	2
.L2450:
	.word	.LANCHOR2
	.word	.LANCHOR2+288
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
	ldr	r5, .L2462
	beq	.L2453
	ldr	r3, [r5, #292]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L2454
	mov	r1, r4
	ldr	r0, .L2462+4
	bl	printk
	ldr	r3, [r5, #292]
	movs	r2, #32
	add	r0, r5, #304
	mov	r1, r4
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	test_node_in_list
	cbz	r0, .L2455
	mov	r1, r4
	add	r0, r5, #304
	bl	List_remove_node
	ldrh	r3, [r5, #308]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #308]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #292]
	ldr	r0, .L2462+8
	mov	r1, r4
	ldrh	r2, [r3, r4, lsl #1]
	bl	printk
.L2455:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2458
.L2454:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L2453:
	ldrh	r0, [r5, #3456]
	movw	r3, #65535
	ldr	r6, .L2462
	cmp	r0, r3
	bne	.L2457
	strh	r4, [r6, #3456]	@ movhi
	b	.L2458
.L2457:
	cmp	r0, r4
	beq	.L2458
	bl	update_vpc_list
	strh	r4, [r6, #3456]	@ movhi
	adds	r0, r0, #0
	it	ne
	movne	r0, #1
	pop	{r4, r5, r6, pc}
.L2458:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L2463:
	.align	2
.L2462:
	.word	.LANCHOR2
	.word	.LC127
	.word	.LC128
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
	beq	.L2464
	ldrh	r3, [r0]
	movw	r5, #65535
	cmp	r3, r5
	beq	.L2464
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r7, .L2476
	adds	r3, r3, #8
	ldr	r6, .L2476+4
	ldrh	r3, [r0, r3, lsl #1]
.L2468:
	cmp	r3, r5
	bne	.L2475
.L2470:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r7, #2316]
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
	b	.L2468
.L2475:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2471
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbnz	r3, .L2471
	ldrh	r3, [r4, #2]
	add	r3, r6, r3, lsl #1
	ldrh	r3, [r3, #1216]
	cmp	r3, r5
	bne	.L2471
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2470
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L2471:
	ldr	r2, .L2476
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	cbz	r3, .L2464
	cmp	r1, #1
	bne	.L2464
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #2386]
	cmp	r1, r3
	bcc	.L2464
	ldr	r3, .L2476+4
	ldrh	r0, [r4]
	ldrh	r5, [r4, #4]
	ldr	r1, [r3, #292]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r5
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #2384]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2464:
	pop	{r3, r4, r5, r6, r7, pc}
.L2477:
	.align	2
.L2476:
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
	ldr	r8, .L2495+4
	adds	r3, r3, #8
	ldr	r5, .L2495
	ldrh	r2, [r0, r3, lsl #1]
.L2479:
	cmp	r2, r7
	ldr	r6, .L2495+4
	bne	.L2494
.L2480:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r8, #2316]
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
	b	.L2479
.L2494:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r1, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2482
	ldrb	r0, [r8, #140]	@ zero_extendqisi2
	cbnz	r0, .L2482
	add	r0, r5, r1, lsl #1
	ldrh	r0, [r0, #1216]
	cmp	r0, r7
	bne	.L2482
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2480
.L2482:
	movw	r7, #65535
	orr	r5, r1, r2, lsl #10
	mov	r8, r7
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2483:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r1, [r6, #2316]
.L2485:
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
	beq	.L2485
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2490
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbnz	r3, .L2487
	ldrh	r2, [r4, #2]
	ldr	r3, .L2495
	add	r3, r3, r2, lsl #1
	ldrh	r3, [r3, #1216]
	cmp	r3, r8
	bne	.L2487
	ldrh	r3, [r4, #4]
	cbz	r3, .L2487
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2483
.L2487:
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	ldr	r2, .L2495+4
	cbz	r3, .L2490
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #2386]
	cmp	r1, r3
	bcc	.L2490
	ldr	r3, .L2495
	ldrh	r0, [r4]
	ldrh	r6, [r4, #4]
	ldr	r1, [r3, #292]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r6
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #2384]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2490:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L2496:
	.align	2
.L2495:
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
	ldr	r4, .L2520
	ldr	r3, [r4, #220]
	cmp	r3, #0
	bne	.L2499
	ldr	r5, .L2520+4
	movs	r1, #255
	ldr	r2, [r4, #3312]
	ldr	r7, [r4, #3336]
	ldr	r6, .L2520+8
	str	r2, [r5, #1252]
	ldrh	r2, [r4, #536]
	str	r7, [r5, #1256]
	str	r3, [r7, #12]
	strh	r2, [r7, #2]	@ movhi
	movw	r2, #61604
	strh	r2, [r7]	@ movhi
	ldr	r2, [r4, #544]
	str	r3, [r7, #8]
	ldr	r3, .L2520+12
	str	r2, [r7, #4]
	ldrh	r2, [r4, #314]
	str	r3, [r4, #236]
	ldr	r3, .L2520+16
	str	r3, [r4, #240]
	ldrh	r3, [r4, #542]
	strh	r3, [r4, #244]	@ movhi
	ldrh	r3, [r6, #2338]
	strb	r3, [r4, #246]
	ldrh	r3, [r4, #312]
	strh	r3, [r4, #250]	@ movhi
	ldrb	r3, [r4, #318]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #252]	@ movhi
	ldrb	r3, [r4, #320]	@ zero_extendqisi2
	ldrh	r2, [r4, #362]
	strb	r3, [r4, #247]
	ldrh	r3, [r4, #360]
	strh	r3, [r4, #254]	@ movhi
	ldrb	r3, [r4, #366]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #256]	@ movhi
	ldrb	r3, [r4, #368]	@ zero_extendqisi2
	strb	r3, [r4, #248]
	ldrh	r3, [r4, #408]
	strh	r3, [r4, #258]	@ movhi
	ldrh	r2, [r4, #410]
	ldrb	r3, [r4, #414]	@ zero_extendqisi2
	ldr	r0, [r5, #1252]
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #260]	@ movhi
	ldrb	r3, [r4, #416]	@ zero_extendqisi2
	ldrh	r2, [r6, #2394]
	strb	r3, [r4, #249]
	ldr	r3, [r4, #512]
	str	r3, [r4, #268]
	ldr	r3, [r4, #504]
	str	r3, [r4, #276]
	ldr	r3, [r4, #508]
	str	r3, [r4, #272]
	ldrh	r3, [r4, #1170]
	strh	r3, [r4, #280]	@ movhi
	ldrh	r3, [r4, #1172]
	strh	r3, [r4, #282]	@ movhi
	bl	ftl_memset
	add	r1, r4, #236
	movs	r2, #48
	ldr	r0, [r5, #1252]
	bl	ftl_memcpy
	ldrh	r2, [r6, #2324]
	ldr	r0, [r5, #1252]
	ldr	r1, [r4, #292]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r6, #2324]
	ldr	r0, [r5, #1252]
	add	r3, r2, #24
	ldr	r1, [r4, #468]
	ubfx	r3, r3, #1, #30
	lsrs	r2, r2, #3
	adds	r2, r2, #4
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
	ldrh	r3, [r6, #2428]
	cbz	r3, .L2500
	ldrh	r2, [r6, #2324]
	ldr	r0, [r5, #1252]
	ldr	r1, [r4, #3384]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldrh	r2, [r6, #2420]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	lsls	r2, r2, #2
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
.L2500:
	movs	r0, #0
	ldr	r10, .L2520
	bl	FtlUpdateVaildLpn
	ldr	fp, .L2520+8
	mov	r8, #0
	movw	r9, #65535
.L2501:
	ldr	r3, [r4, #3312]
	ldrh	r2, [r4, #536]
	ldrh	r1, [r4, #538]
	str	r3, [r5, #1252]
	ldr	r3, [r4, #3336]
	str	r3, [r5, #1256]
	orr	r3, r1, r2, lsl #10
	str	r3, [r5, #1248]
	ldrh	r3, [r6, #2386]
	subs	r3, r3, #1
	cmp	r1, r3
	blt	.L2502
	movs	r3, #0
	ldrh	r9, [r10, #540]
	strh	r3, [r10, #538]	@ movhi
	strh	r2, [r10, #540]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r10, #504]
	ldr	r1, .L2520+4
	adds	r2, r3, #1
	str	r2, [r10, #504]
	str	r3, [r10, #544]
	lsls	r2, r0, #10
	strh	r0, [r10, #536]	@ movhi
	str	r2, [r1, #1248]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L2502:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cbz	r3, .L2503
	ldr	r0, [r4, #3312]
	ldrh	r1, [fp, #2394]
	bl	js_hash
	str	r0, [r7, #12]
.L2503:
	movs	r1, #1
	ldr	r0, .L2520+20
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #538]
	ldr	r2, [r5, #1244]
	adds	r3, r3, #1
	adds	r1, r2, #1
	uxth	r3, r3
	strh	r3, [r4, #538]	@ movhi
	bne	.L2504
	cmp	r3, #1
	add	r8, r8, #1
	it	eq
	ldrheq	r3, [r6, #2386]
	uxth	r8, r8
	itt	eq
	addeq	r3, r3, #-1
	strheq	r3, [r10, #538]	@ movhi
	cmp	r8, #3
	bls	.L2501
	ldr	r0, .L2520+24
	mov	r2, r8
	ldr	r1, [r5, #1248]
	bl	printk
	movs	r3, #1
	str	r3, [r4, #220]
	b	.L2499
.L2504:
	cmp	r3, #1
	beq	.L2501
	cmp	r2, #256
	beq	.L2501
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2499
	mov	r0, r9
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2499:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2521:
	.align	2
.L2520:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177376
	.word	.LANCHOR4+1244
	.word	.LC129
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
	ldr	r5, .L2538
	.pad #44
	sub	sp, sp, #44
	ldr	r3, [r5, #220]
	cmp	r3, #0
	bne	.L2522
	ldr	r2, .L2538+4
	ldrb	r6, [r2, #140]	@ zero_extendqisi2
	mov	r8, r2
	cbz	r6, .L2537
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	bne	.L2533
	ldrh	fp, [r0, #4]
	b	.L2524
.L2533:
	mov	r6, r3
.L2537:
	mov	fp, #12
.L2524:
	ldr	r7, [r5, #3336]
	mov	r3, #-1
	str	r3, [sp, #20]
	mov	r9, #0
	ldr	r3, [r5, #3312]
	movw	r2, #61589
	str	r7, [sp, #16]
	ldr	r10, .L2538
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r7, #8]
	mvn	r3, #1
	str	r3, [r7, #12]
	ldrh	r3, [r4]
	strh	r9, [r7]	@ movhi
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #3312]
	str	r2, [r3]
	add	r2, r2, #304087040
	ldr	r3, [r5, #3312]
	add	r2, r2, #1269760
	addw	r2, r2, #1507
	str	r2, [r3, #4]
.L2525:
	adds	fp, fp, #-1
	bcc	.L2528
	ldrh	r3, [r4, #4]
	cbnz	r3, .L2526
.L2528:
	ldr	r2, [r5, #292]
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r8, #2384]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L2522
.L2526:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L2528
	ldr	r3, [r5, #508]
	movs	r1, #1
	add	r0, sp, #4
	str	r3, [r7, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, r6
	it	eq
	moveq	r3, r9
	str	r3, [r10, #508]
	movs	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2525
.L2522:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2539:
	.align	2
.L2538:
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
	blt	.L2541
	uxth	r0, r0
	ldrh	r10, [r5, r0, lsl #1]
	cmp	r10, #0
	beq	.L2541
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2541
	movs	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L2553
	ldrh	r3, [r3, #2386]
	cmp	r2, r3
	bcc	.L2542
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2542:
	ldr	r6, .L2553+4
	mov	fp, #0
	ldr	r9, .L2553+16
.L2543:
	ldrh	r2, [r4, #6]
	uxth	r5, fp
	cmp	r2, r5
	bls	.L2552
	ldr	r2, [r8, r5, lsl #2]
	add	ip, r8, r5, lsl #2
	cmp	r10, r2, lsr #10
	bne	.L2544
	ldr	r2, [r9, #3316]
	movs	r1, #1
	ldr	r7, [r9, #3336]
	ldr	r0, .L2553+8
	str	r2, [r6, #1252]
	str	r7, [r6, #1256]
	ldr	r2, [r8, r5, lsl #2]
	str	ip, [sp, #4]
	str	r2, [r6, #1248]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r6, #1244]
	adds	r2, r2, #1
	ldr	ip, [sp, #4]
	bne	.L2545
.L2547:
	movs	r3, #0
	str	r3, [ip]
	ldr	r3, .L2553+4
	ldrh	r2, [r7, #8]
	ldr	r0, .L2553+12
	ldr	r1, [r3, #1248]
	bl	printk
	ldr	r3, .L2553+16
	movs	r2, #1
	str	r2, [r3, #220]
	b	.L2546
.L2545:
	ldrh	r1, [r7, #8]
	cmp	r1, r5
	bne	.L2547
	ldrh	r0, [r7]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2547
	mov	r0, r4
	ldr	r2, [r6, #1252]
	bl	FtlMapWritePage
.L2544:
	add	fp, fp, #1
	b	.L2543
.L2552:
	mov	r0, r10
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L2541:
	ldr	r3, .L2553
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #2386]
	cmp	r2, r3
	bcc	.L2546
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2546:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2554:
	.align	2
.L2553:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR4+1244
	.word	.LC130
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
	ldr	r6, .L2566
	ldr	r7, [r0, #12]
	ldr	r10, [r0, #24]
	ldr	r5, [r6, #220]
	cmp	r5, #0
	bne	.L2556
	ldrh	r3, [r0]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L2557
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
	b	.L2556
.L2557:
	ldrh	r9, [r7, r3, lsl #1]
	movs	r1, #255
	ldr	fp, .L2566+12
	ldrh	r3, [r0, #2]
	ldr	r7, [r6, #3336]
	orr	r3, r3, r9, lsl #10
	str	r3, [fp, #1248]
	ldr	r3, [r6, #3312]
	str	r7, [fp, #1256]
	ldr	r8, .L2566+4
	str	r3, [fp, #1252]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	movw	r3, #64245
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r9, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	ldrh	r2, [r8, #2386]
	ldr	r0, [r6, #3312]
	lsls	r2, r2, #3
	bl	ftl_memset
	mov	r1, r5
.L2558:
	ldrh	r2, [r4, #6]
	uxth	r3, r1
	cmp	r2, r3
	bls	.L2565
	ldr	r2, [r10, r3, lsl #2]
	cmp	r9, r2, lsr #10
	bne	.L2559
	adds	r5, r5, #1
	ldr	r2, [r6, #3312]
	uxth	r5, r5
	str	r3, [r2, r5, lsl #3]
	ldr	r2, [r10, r3, lsl #2]
	ldr	r3, [r6, #3312]
	add	r3, r3, r5, lsl #3
	str	r2, [r3, #4]
.L2559:
	adds	r1, r1, #1
	b	.L2558
.L2565:
	ldrb	r3, [r8]	@ zero_extendqisi2
	cbz	r3, .L2561
	ldr	r3, .L2566+4
	ldr	r0, [fp, #1252]
	ldrh	r1, [r3, #2394]
	bl	js_hash
	str	r0, [r7, #12]
.L2561:
	movs	r1, #1
	movs	r3, #0
	ldr	r0, .L2566+8
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2556:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2567:
	.align	2
.L2566:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4+1244
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
	ldr	r7, .L2592
	mov	r4, r0
	mov	r9, r1
	str	r2, [sp]
.L2569:
	ldr	r3, [r7, #488]
	ldr	fp, .L2592+4
	adds	r3, r3, #1
	str	r3, [r7, #488]
	ldr	r3, .L2592+4
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #2386]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L2570
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2571
.L2570:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2571:
	ldr	r3, .L2592
	ldr	r1, [r3, #220]
	cmp	r1, #0
	bne	.L2587
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldr	r5, .L2592+8
	ldr	r0, [r7, #3336]
	ldrh	r8, [r3, r2, lsl #1]
	movs	r2, #16
	ldrh	r3, [r4, #2]
	mov	r10, r5
	str	r0, [r5, #1256]
	orr	r3, r3, r8, lsl #10
	str	r3, [r5, #1248]
	ldr	r3, [sp]
	str	r3, [r5, #1252]
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	r3, [r5, #1256]
	str	r2, [r3, #4]
	strh	r9, [r3, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r8, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrb	r2, [fp]	@ zero_extendqisi2
	cbz	r2, .L2573
	str	r3, [sp, #4]
	ldr	r3, .L2592+4
	ldr	r0, [r5, #1252]
	ldrh	r1, [r3, #2394]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2573:
	ldr	ip, .L2592+8
	movs	r1, #1
	addw	r0, ip, #1244
	mov	r2, r1
	mov	r3, r1
	str	ip, [sp, #4]
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #1244]
	adds	r1, r2, #1
	ldr	ip, [sp, #4]
	bne	.L2574
	ldr	r0, .L2592+12
	adds	r6, r6, #1
	ldr	r1, [ip, #1248]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [fp, #2386]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L2569
	mov	r2, r6
	ldr	r0, .L2592+16
	ldr	r1, [r10, #1248]
	bl	printk
	ldr	r3, .L2592
	movs	r2, #1
	str	r2, [r3, #220]
	b	.L2587
.L2574:
	cbz	r2, .L2577
	cmp	r3, #1
	strh	r8, [r4, #40]	@ movhi
	bne	.L2578
.L2579:
	movs	r3, #0
	str	r3, [r4, #36]
	b	.L2569
.L2578:
	cmp	r2, #256
	b	.L2591
.L2577:
	cmp	r3, #1
.L2591:
	beq	.L2579
	ldr	r3, [r4, #36]
	cmp	r3, #0
	bne	.L2579
	ldr	r2, [r10, #1248]
	ldr	r3, [r4, #24]
	str	r2, [r3, r9, lsl #2]
.L2587:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2593:
	.align	2
.L2592:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC131
	.word	.LC132
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
	ldr	r5, .L2595
	muls	r4, r0, r4
	addw	r0, r5, #3404
	ldr	r3, [r5, #456]
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #456]
	movs	r0, #0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L2596:
	.align	2
.L2595:
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
	ldr	r10, .L2639+12
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, .L2639
	movs	r0, #1
	ldr	r2, .L2639+4
	str	r0, [r4, #36]
	ldr	r1, [r3, #3312]
	ldr	r6, [r3, #3336]
	mov	r5, r2
	str	r1, [r2, #1252]
	movw	r1, #65535
	str	r6, [r2, #1256]
	strh	r1, [r4]	@ movhi
	strh	r1, [r4, #2]	@ movhi
	movs	r1, #0
	str	r1, [r4, #32]
	str	r1, [r4, #28]
	str	r1, [sp, #4]
	str	r3, [sp, #20]
.L2598:
	ldrh	r2, [sp, #4]
	ldr	r1, [sp, #8]
	ldr	fp, .L2639+12
	sxth	r3, r2
	cmp	r3, r1
	bge	.L2616
	ldr	r1, [sp, #8]
	subs	r1, r1, #1
	cmp	r3, r1
	lsl	r1, r3, #1
	bne	.L2599
	ldrh	r0, [r8, r3, lsl #1]
	str	r2, [sp, #16]
	add	r2, r8, r1
	movs	r1, #1
	str	r2, [sp, #4]
	str	r3, [sp, #8]
	bl	FtlGetLastWrittenPage
	ldr	r10, .L2639+4
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
.L2600:
	ldr	r3, [sp, #8]
	sxth	r2, r8
	cmp	r2, r3
	bge	.L2616
	ldr	r3, [sp, #4]
	ldr	r0, .L2639+8
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r5, #1248]
	mov	r2, r1
	bl	FlashReadPages
	ldrb	r2, [fp]	@ zero_extendqisi2
	cbz	r2, .L2601
	ldr	r2, [r10, #1256]
	ldr	r2, [r2, #12]
	cbz	r2, .L2601
	ldr	r1, .L2639+12
	ldr	r0, [r10, #1252]
	str	r2, [sp, #12]
	ldrh	r1, [r1, #2394]
	bl	js_hash
	ldr	r2, [sp, #12]
	cmp	r2, r0
	itt	ne
	movne	r2, #-1
	strne	r2, [r10, #1244]
.L2601:
	ldr	r2, [r10, #1244]
	adds	r2, r2, #1
	beq	.L2602
	ldrh	r2, [r6, #8]
	cmp	r2, r9
	bcs	.L2602
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	itt	eq
	ldreq	r1, [r10, #1248]
	streq	r1, [r7, r2, lsl #2]
.L2602:
	add	r8, r8, #1
	b	.L2600
.L2616:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [fp, #2386]
	cmp	r2, r3
	bne	.L2605
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2605
.L2599:
	ldr	r2, [sp, #20]
	ldr	r0, .L2639+8
	ldr	r2, [r2, #3312]
	str	r2, [r5, #1252]
	add	r2, r8, r1
	str	r2, [sp, #16]
	movs	r1, #1
	ldrh	r2, [r8, r3, lsl #1]
	ldrh	r3, [r10, #2386]
	subs	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	mov	r2, r1
	str	r3, [r5, #1248]
	bl	FlashReadPages
	ldr	r3, [r5, #1244]
	adds	r3, r3, #1
	beq	.L2618
	ldrh	r2, [r6]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2618
	ldrh	r2, [r6, #8]
	movw	r3, #64245
	cmp	r2, r3
	bne	.L2618
	movs	r1, #0
	mov	lr, #8
	mov	ip, #4
.L2607:
	uxth	r3, r1
	ldrh	r2, [r10, #2386]
	sxth	r0, r3
	subs	r2, r2, #1
	cmp	r0, r2
	bge	.L2610
	ldr	r2, .L2639
	adds	r1, r1, #1
	ldr	r2, [r2, #3312]
	ldrh	r0, [r2, r0, lsl #3]
	cmp	r0, r9
	ittt	cc
	smlabbcc	r3, r3, lr, ip
	ldrcc	r3, [r2, r3]
	strcc	r3, [r7, r0, lsl #2]
	b	.L2607
.L2618:
	ldr	r3, .L2639+4
	mov	fp, #0
.L2638:
	ldrh	r1, [r10, #2386]
	sxth	r2, fp
	cmp	r2, r1
	bge	.L2610
	ldr	r1, [sp, #16]
	ldr	r0, .L2639+8
	str	r3, [sp, #24]
	ldrh	r1, [r1]
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r3, #1248]
	mov	r2, r1
	bl	FlashReadPages
	ldrb	r2, [r10]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cbz	r2, .L2611
	ldr	r2, [r3, #1256]
	ldr	r2, [r2, #12]
	cbz	r2, .L2611
	ldr	r0, [r3, #1252]
	ldrh	r1, [r10, #2394]
	str	r2, [sp, #28]
	bl	js_hash
	ldr	r2, [sp, #28]
	ldr	r3, [sp, #24]
	cmp	r2, r0
	itt	ne
	movne	r2, #-1
	strne	r2, [r3, #1244]
.L2611:
	ldr	r2, .L2639+4
	ldr	r2, [r2, #1244]
	adds	r2, r2, #1
	beq	.L2612
	ldrh	r2, [r6, #8]
	cmp	r2, r9
	bcs	.L2612
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	itt	eq
	ldreq	r1, [r3, #1248]
	streq	r1, [r7, r2, lsl #2]
.L2612:
	add	fp, fp, #1
	b	.L2638
.L2610:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
	b	.L2598
.L2605:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2640:
	.align	2
.L2639:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR4+1244
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
	ldr	r3, .L2642
	ldr	r0, .L2642+4
	ldrh	r2, [r3, #2404]
	addw	r0, r0, #1284
	strh	r2, [r0, #10]	@ movhi
	movw	r2, #61574
	strh	r2, [r0, #4]	@ movhi
	ldrh	r2, [r3, #2430]
	strh	r2, [r0, #8]	@ movhi
	ldrh	r2, [r3, #2406]
	ldr	r3, [r3, #2432]
	strh	r2, [r0, #6]	@ movhi
	str	r3, [r0, #12]
	ldr	r3, .L2642+8
	ldr	r2, [r3, #3376]
	str	r2, [r0, #16]
	ldr	r2, [r3, #3372]
	ldr	r3, [r3, #3380]
	str	r2, [r0, #20]
	str	r3, [r0, #24]
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2643:
	.align	2
.L2642:
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
	ldr	r0, .L2645
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2646:
	.align	2
.L2645:
	.word	.LANCHOR2+3404
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
	ldr	r3, .L2660
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	mov	r4, r1
	add	r0, r0, r1
	ldrh	r1, [r3, #2380]
	.pad #60
	sub	sp, sp, #60
	mov	r10, r2
	ldrh	r2, [r3, #2392]
	cmp	r0, r1
	str	r3, [sp, #4]
	bhi	.L2655
	ldr	r7, .L2660+4
	lsr	r6, r5, r2
	mov	fp, #0
	lsls	r3, r6, #2
	str	r3, [sp]
.L2649:
	cmp	r4, #0
	beq	.L2659
	ldr	r3, .L2660+4
	mov	r0, r5
	ldr	r2, [sp]
	ldr	r3, [r3, #3380]
	ldr	ip, [r3, r2]
	ldr	r3, [sp, #4]
	str	ip, [sp, #12]
	ldrh	r2, [r3, #2390]
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
	beq	.L2651
	cmp	r9, r2
	beq	.L2651
	ldr	r2, [r7, #3320]
	movs	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	movs	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2652
.L2651:
	ldr	r3, [sp, #4]
	movs	r1, #0
	ldr	r0, [r7, #3320]
	ldrh	r2, [r3, #2394]
	bl	ftl_memset
.L2652:
	lsl	ip, r9, #9
	ldr	r0, [r7, #3320]
	uxth	r8, r8
	mov	r1, r10
	mov	r2, ip
	str	ip, [sp, #8]
	add	r0, r0, r8, lsl #9
	rsb	r4, r9, r4
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r0, .L2660+8
	adds	r6, r6, #1
	ldr	r2, [r7, #3320]
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
	b	.L2649
.L2659:
	mov	r0, fp
	b	.L2648
.L2655:
	mov	r0, #-1
.L2648:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2661:
	.align	2
.L2660:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4+1284
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
	ldr	r2, .L2664
	ldr	r3, .L2664+4
	ldr	r1, [r2, #604]
	cmp	r1, r3
	bne	.L2662
	ldr	r3, .L2664+8
	movs	r0, #0
	movs	r1, #1
	str	r3, [r2, #608]
	ldr	r3, [r2, #496]
	str	r3, [r2, #692]
	ldr	r3, [r2, #500]
	str	r3, [r2, #696]
	ldr	r3, [r2, #492]
	str	r3, [r2, #612]
	ldr	r3, [r2, #480]
	str	r3, [r2, #616]
	ldr	r3, [r2, #472]
	str	r3, [r2, #620]
	ldr	r3, [r2, #488]
	str	r3, [r2, #624]
	ldr	r3, [r2, #516]
	str	r3, [r2, #632]
	ldr	r3, [r2, #232]
	add	r2, r2, #604
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
.L2662:
	bx	lr
.L2665:
	.align	2
.L2664:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177376
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
	ldr	r3, .L2673
	push	{r4, lr}
	.save {r4, lr}
	ldr	r2, [r3, #2308]
	ldr	r3, .L2673+4
	cbz	r2, .L2671
	ldr	r2, [r3, #528]
	cmp	r2, #39
	ite	hi
	movhi	r1, #32
	movls	r1, #4
	b	.L2667
.L2671:
	movs	r1, #32
.L2667:
	ldr	r4, .L2673+8
	ldrh	r2, [r4, #1328]
	cmp	r2, #31
	ittt	ls
	addls	r2, r2, #1
	movls	r1, #1
	strhls	r2, [r4, #1328]	@ movhi
	cbnz	r0, .L2669
	ldr	r0, [r3, #3356]
	ldr	r2, [r0, #20]
	add	r2, r2, r1
	ldr	r1, [r0, #16]
	cmp	r1, r2
	bcc	.L2670
.L2669:
	ldr	r2, [r3, #3356]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2673+12
	str	r1, [r2]
	ldr	r2, [r3, #3356]
	ldrh	r1, [r3, #3348]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2670:
	movs	r0, #0
	pop	{r4, pc}
.L2674:
	.align	2
.L2673:
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
	ldr	r3, .L2686
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	mov	r4, r1
	add	r0, r0, r1
	ldrh	r1, [r3, #2380]
	.pad #60
	sub	sp, sp, #60
	mov	r7, r2
	ldrh	r2, [r3, #2392]
	cmp	r0, r1
	str	r3, [sp, #12]
	bhi	.L2684
	ldr	r10, .L2686+4
	lsr	r5, r6, r2
	mov	fp, #0
	lsls	r3, r5, #2
	str	r3, [sp, #4]
.L2677:
	cmp	r4, #0
	beq	.L2685
	ldr	r3, .L2686+4
	mov	r0, r6
	ldr	r2, [sp, #4]
	ldr	r3, [r3, #3380]
	ldr	r8, [r3, r2]
	ldr	r3, [sp, #12]
	ldrh	r9, [r3, #2390]
	mov	r1, r9
	bl	__aeabi_uidivmod
	rsb	r3, r1, r9
	str	r1, [sp, #8]
	uxth	r9, r3
	cmp	r9, r4
	it	hi
	uxthhi	r9, r4
	cmp	r8, #0
	beq	.L2679
	ldr	r2, [r10, #3320]
	movs	r1, #1
	add	r0, sp, #20
	str	r8, [sp, #24]
	str	r2, [sp, #28]
	movs	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, .L2686+8
	ldr	r2, [sp, #20]
	adds	r2, r2, #1
	ldr	r2, [r3, #1244]
	it	eq
	moveq	fp, #-1
	cmp	r2, #256
	bne	.L2681
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L2686+12
	bl	printk
	ldr	r0, .L2686+16
	mov	r1, r5
	ldr	r2, [r10, #3320]
	bl	FtlMapWritePage
.L2681:
	ldrh	r8, [sp, #8]
	mov	r0, r7
	ldr	r1, [r10, #3320]
	lsl	r2, r9, #9
	add	r1, r1, r8, lsl #9
	bl	ftl_memcpy
	b	.L2682
.L2679:
	mov	r0, r7
	mov	r1, r8
	lsl	r2, r9, #9
	bl	ftl_memset
.L2682:
	ldr	r3, [sp, #4]
	adds	r5, r5, #1
	rsb	r4, r9, r4
	add	r6, r6, r9
	adds	r3, r3, #4
	add	r7, r7, r9, lsl #9
	str	r3, [sp, #4]
	b	.L2677
.L2685:
	mov	r0, fp
	b	.L2676
.L2684:
	mov	r0, #-1
.L2676:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2687:
	.align	2
.L2686:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC133
	.word	.LANCHOR4+1284
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
	ldr	r4, .L2690
	ldr	r2, [r4, #3356]
	ldrh	r1, [r4, #3348]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3356]
	ldr	r2, [r3]
	ldr	r3, .L2690+4
	cmp	r2, r3
	beq	.L2689
	ldr	r1, .L2690+8
	ldr	r0, .L2690+12
	bl	printk
	ldrh	r2, [r4, #3348]
	ldr	r0, [r4, #3356]
	movs	r1, #0
	lsls	r2, r2, #9
	bl	ftl_memset
.L2689:
	movs	r0, #0
	pop	{r4, pc}
.L2691:
	.align	2
.L2690:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC134
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
	ldr	r4, .L2697
	movs	r1, #1
	ldr	r5, .L2697+4
	add	r2, r4, #604
	bl	FtlVendorPartRead
	ldr	r3, [r4, #604]
	cmp	r3, r5
	beq	.L2693
	add	r0, r4, #604
	movs	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #604]
.L2693:
	ldr	r2, [r4, #604]
	ldr	r3, .L2697
	cmp	r2, r5
	bne	.L2694
	ldr	r2, [r3, #692]
	str	r2, [r3, #496]
	ldr	r2, [r3, #696]
	str	r2, [r3, #500]
	ldr	r2, [r3, #612]
	str	r2, [r3, #492]
	ldr	r2, [r3, #616]
	str	r2, [r3, #480]
	ldr	r2, [r3, #620]
	str	r2, [r3, #472]
	ldr	r2, [r3, #624]
	str	r2, [r3, #488]
	ldr	r2, [r3, #632]
	str	r2, [r3, #516]
	ldr	r2, [r3, #636]
	str	r2, [r3, #232]
	ldr	r2, [r3, #640]
	str	r2, [r3, #476]
	ldr	r2, [r3, #644]
	str	r2, [r3, #484]
	ldr	r2, [r3, #648]
	str	r2, [r3, #524]
	ldr	r2, [r3, #652]
	str	r2, [r3, #528]
	ldr	r2, [r3, #664]
	str	r2, [r3, #600]
.L2694:
	ldr	r1, [r4, #672]
	movs	r3, #0
	ldr	r2, .L2697+8
	str	r3, [r4, #3272]
	cmp	r1, r2
	ldr	r5, .L2697+12
	bne	.L2695
	ldrb	r2, [r5, #140]	@ zero_extendqisi2
	cbz	r2, .L2696
	ldr	r2, .L2697
	str	r3, [r2, #672]
	bl	Ftl_save_ext_data
	b	.L2695
.L2696:
	ldr	r0, .L2697+16
	movs	r3, #1
	ldr	r1, .L2697+20
	str	r3, [r5, #2308]
	bl	printk
.L2695:
	ldrh	r2, [r5, #2376]
	ldr	r3, [r4, #516]
	ldr	r0, [r4, #512]
	ldrh	r1, [r5, #2324]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #520]
	pop	{r3, r4, r5, pc}
.L2698:
	.align	2
.L2697:
	.word	.LANCHOR2
	.word	1179929683
	.word	305432421
	.word	.LANCHOR0
	.word	.LC76
	.word	.LC135
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
	cbz	r3, .L2699
	ldr	r2, .L2705
	movs	r3, #0
	str	r3, [r0, #36]
	ldr	r3, [r2, #220]
	cbnz	r3, .L2699
	subs	r5, r5, #1
	ldr	r3, .L2705+4
	ldr	r0, [r2, #3316]
	ldr	r2, [r2, #3336]
	uxth	r5, r5
	mov	r4, r3
	str	r0, [r3, #1252]
	str	r2, [r3, #1256]
	ldr	r2, [r1, r5, lsl #2]
	str	r2, [r3, #1248]
	cbz	r2, .L2703
	movs	r1, #1
	addw	r0, r3, #1244
	mov	r2, r1
	bl	FlashReadPages
	b	.L2704
.L2703:
	ldr	r3, .L2705+8
	movs	r1, #255
	ldrh	r2, [r3, #2394]
	bl	ftl_memset
.L2704:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #1252]
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L2699:
	pop	{r4, r5, r6, pc}
.L2706:
	.align	2
.L2705:
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
	ldr	r3, .L2721
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldrh	r2, [r3, #124]
	mov	r7, r1
	ldrh	r3, [r3, #126]
	.pad #40
	sub	sp, sp, #40
	mov	r5, r0
	smulbb	r3, r2, r3
	ldr	r2, .L2721+4
	ldr	r1, [r2, #1180]
	mov	r9, r2
	uxth	r3, r3
	subs	r6, r3, #1
	str	r1, [sp, #12]
	mul	r10, r3, r7
	ldr	r1, [r2, #1212]
	uxth	r6, r6
	sub	r4, r3, #16
	str	r1, [sp, #16]
.L2708:
	cmp	r6, r4
	ble	.L2720
	movs	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L2709
	ldr	r3, [r9, #1212]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2709
	cbz	r5, .L2715
	cbz	r7, .L2716
.L2713:
	ldr	r1, [r9, #1180]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	movs	r2, #4
	ldr	r0, .L2721+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	movs	r0, #0
	b	.L2710
.L2716:
	ldr	r6, .L2721+4
	mov	lr, #1
.L2711:
	ldr	r2, [r9, #1200]
	uxth	r3, r7
	adds	r7, r7, #1
	cmp	r3, r2
	bcs	.L2713
	ldr	r2, [r6, #1180]
	lsrs	r1, r3, #5
	and	r4, r3, #31
	lsl	r3, lr, r4
	ldr	r0, [r2, r1, lsl #2]
	orrs	r3, r3, r0
	str	r3, [r2, r1, lsl #2]
	b	.L2711
.L2709:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L2708
.L2720:
	mov	r0, #-1
	b	.L2710
.L2715:
	mov	r0, r5
.L2710:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2722:
	.align	2
.L2721:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC136
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
	ldr	r3, .L2733
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #1204]
	ldr	r6, .L2733+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #1208]
	smulbb	r4, r4, r3
	uxth	r4, r4
	adds	r2, r4, #7
	asrs	r2, r2, #3
	bl	FlashReadFacBbtData
	adds	r0, r0, #1
	bne	.L2724
.L2728:
	movs	r3, #0
	b	.L2725
.L2724:
	movs	r1, #0
	lsr	lr, r4, #4
	mov	ip, #1
	subs	r4, r4, #1
	mov	r3, r1
.L2726:
	uxth	r2, r1
	cmp	r2, r4
	bge	.L2725
	ldr	r7, [r6, #1208]
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
	bcc	.L2726
	b	.L2728
.L2725:
	movw	r2, #65535
	movs	r0, #0
	strh	r2, [r5, r3, lsl #1]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L2734:
	.align	2
.L2733:
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
	ldr	r5, .L2759
	ldr	r4, [r5, #220]
	cmp	r4, #0
	bne	.L2736
	bl	FtlBbtMemInit
	ldr	r6, .L2759+4
	bl	FtlLoadFactoryBbt
	ldr	r9, .L2759+16
	sub	r8, r6, #18
	subw	r7, r6, #2476
.L2737:
	ldrh	r3, [r7, #2338]
	ldr	r10, .L2759+8
	cmp	r4, r3
	bcs	.L2756
	ldr	r3, [r5, #3336]
	movw	r2, #65535
	ldr	r0, [r5, #3312]
	ldr	fp, .L2759+16
	str	r3, [sp]
	str	r3, [r9, #1256]
	ldrh	r3, [r8, #2]!
	str	r0, [r9, #1252]
	cmp	r3, r2
	beq	.L2738
	ldrh	ip, [r7, #2382]
	movs	r1, #1
	addw	r0, fp, #1244
	mov	r2, r1
	mla	ip, ip, r4, r3
	lsl	r3, ip, #10
	str	ip, [sp]
	str	r3, [r9, #1248]
	bl	FlashReadPages
	ldrh	r2, [r7, #2382]
	ldr	r0, [r6]
	adds	r2, r2, #7
	ldr	r1, [r9, #1252]
	asrs	r2, r2, #3
	bl	ftl_memcpy
	ldr	ip, [sp]
	b	.L2739
.L2738:
	mov	r1, r4
	bl	FlashGetBadBlockList
	ldr	r0, [r9, #1252]
	ldr	r1, [r6]
	bl	FtlBbt2Bitmap
	ldrh	r3, [r7, #2382]
	subs	r3, r3, #1
.L2755:
	uxth	r10, r3
.L2740:
	ldr	r3, .L2759+8
	ldrh	r0, [r3, #2382]
	smlabb	r0, r0, r4, r10
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	bne	.L2757
	add	r3, r10, #-1
	b	.L2755
.L2757:
	movs	r1, #0
	movs	r2, #16
	strh	r10, [r8]	@ movhi
	ldr	r0, [r5, #3336]
	bl	ftl_memset
	movw	r3, #61664
	ldr	r2, [sp]
	strh	r3, [r2]	@ movhi
	movs	r3, #0
	str	r3, [r2, #4]
	ldrh	r3, [r8]
	ldrh	ip, [r7, #2382]
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r8]
	ldrh	r2, [r5, #3400]
	ldr	r1, [r6]
	mla	ip, ip, r4, r3
	lsls	r2, r2, #2
	ldr	r0, [fp, #1252]
	lsl	r3, ip, #10
	str	ip, [sp, #4]
	str	r3, [fp, #1248]
	bl	ftl_memcpy
	movs	r1, #1
	ldr	r0, .L2759+12
	mov	r2, r1
	bl	FlashEraseBlocks
	movs	r1, #1
	ldr	r0, .L2759+12
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [fp, #1244]
	adds	r3, r3, #1
	ldr	ip, [sp, #4]
	bne	.L2739
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L2740
.L2739:
	uxth	r0, ip
	adds	r4, r4, #1
	bl	FtlBbmMapBadBlock
	adds	r6, r6, #4
	b	.L2737
.L2756:
	movs	r4, #0
.L2744:
	ldrh	r3, [r10, #2398]
	uxth	r0, r4
	ldr	r6, .L2759+8
	adds	r4, r4, #1
	cmp	r3, r0
	bls	.L2758
	bl	FtlBbmMapBadBlock
	b	.L2744
.L2758:
	ldrh	r4, [r6, #2460]
	movw	r7, #65535
	subs	r4, r4, #1
	uxth	r4, r4
.L2746:
	ldrh	r3, [r10, #2460]
	subs	r3, r3, #48
	cmp	r4, r3
	ble	.L2750
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2747
	mov	r0, r4
	bl	FlashTestBlk
	cbz	r0, .L2748
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L2747
.L2748:
	ldrh	r3, [r6, #2448]
	cmp	r3, r7
	bne	.L2749
	strh	r4, [r6, #2448]	@ movhi
	b	.L2747
.L2749:
	ldr	r3, .L2759+8
	strh	r4, [r3, #2452]	@ movhi
	b	.L2750
.L2747:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L2746
.L2750:
	ldr	r0, [r5, #224]
	movs	r4, #0
	ldrh	r3, [r10, #2448]
	movs	r2, #2
	str	r4, [r10, #2456]
	movs	r1, #1
	strh	r4, [r10, #2450]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r10, #2452]
	lsls	r3, r3, #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r10, #2448]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r10, #2452]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r10, #2456]
	ldrh	r2, [r10, #2452]
	adds	r3, r3, #1
	str	r3, [r10, #2456]
	ldrh	r3, [r10, #2448]
	strh	r4, [r10, #2450]	@ movhi
	strh	r2, [r10, #2448]	@ movhi
	strh	r3, [r10, #2452]	@ movhi
	bl	FtlBbmTblFlush
.L2736:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2760:
	.align	2
.L2759:
	.word	.LANCHOR2
	.word	.LANCHOR0+2476
	.word	.LANCHOR0
	.word	.LANCHOR4+1244
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
	ldr	r6, .L2777
	mov	r9, r0
	str	r2, [sp, #8]
	mov	r10, r1
	ldr	r3, [r6, #2444]
	ldrh	r2, [r6, #2392]
	cmp	r0, r3
	bcs	.L2762
	adds	r3, r2, #7
	ldr	r2, .L2777+4
	lsr	r5, r0, r3
	str	r3, [sp, #4]
	ldrh	r1, [r6, #2422]
	movs	r4, #0
	ldr	fp, [r2, #456]
	mov	r8, r2
	uxth	r5, r5
	movs	r2, #12
	b	.L2763
.L2762:
	ldr	r3, [sp, #8]
	mov	r0, #-1
	cmp	r3, #0
	bne	.L2764
	str	r0, [r1]
	b	.L2764
.L2768:
	adds	r4, r4, #1
	mla	r0, r2, r4, fp
	ldrh	r0, [r0, #-12]
	cmp	r0, r5
	beq	.L2765
.L2763:
	uxth	r7, r4
	cmp	r7, r1
	bcc	.L2768
	bl	select_l2p_ram_region
	movs	r4, #12
	movw	r3, #65535
	muls	r4, r0, r4
	mov	r7, r0
	add	r2, fp, r4
	ldrh	r1, [fp, r4]
	cmp	r1, r3
	bne	.L2776
.L2769:
	ldr	r3, [r8, #3384]
	ldr	fp, .L2777+4
	ldr	r3, [r3, r5, lsl #2]
	cmp	r3, #0
	bne	.L2770
	ldr	r2, [fp, #456]
	movs	r1, #255
	str	r3, [sp, #12]
	add	r2, r2, r4
	ldr	r0, [r2, #8]
	ldrh	r2, [r6, #2394]
	bl	ftl_memset
	ldr	r2, [fp, #456]
	strh	r5, [r2, r4]	@ movhi
	ldr	r2, [fp, #456]
	add	r4, r4, r2
	ldr	r3, [sp, #12]
	str	r3, [r4, #4]
.L2765:
	ldr	r3, [sp, #4]
	movs	r2, #1
	ldr	r1, [sp, #8]
	lsls	r2, r2, r3
	movs	r3, #12
	subs	r2, r2, #1
	and	r9, r9, r2
	ldr	r2, .L2777+4
	uxth	r9, r9
	cbnz	r1, .L2766
	ldr	r1, [r2, #456]
	mla	r3, r3, r7, r1
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r9, lsl #2]
	str	r3, [r10]
	b	.L2767
.L2766:
	muls	r3, r7, r3
	ldr	r1, [r2, #456]
	ldr	r0, [r10]
	add	r1, r1, r3
	ldr	r1, [r1, #8]
	str	r0, [r1, r9, lsl #2]
	ldr	r1, [r2, #456]
	strh	r5, [r2, #460]	@ movhi
	add	r3, r3, r1
	ldr	r1, [r3, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r3, #4]
.L2767:
	ldr	r2, [r2, #456]
	movs	r3, #12
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	adds	r2, r3, #1
	beq	.L2774
	adds	r3, r3, #1
	str	r3, [r7, #4]
	b	.L2774
.L2776:
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L2769
	bl	flush_l2p_region
	b	.L2769
.L2770:
	ldr	r2, [fp, #456]
	movs	r1, #1
	ldr	r8, .L2777+24
	add	r2, r2, r4
	str	r3, [sp, #12]
	addw	r0, r8, #1244
	ldr	r2, [r2, #8]
	str	r3, [r8, #1248]
	str	r2, [r8, #1252]
	ldr	r2, [fp, #3336]
	str	r2, [r8, #1256]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r8, #1256]
	ldrh	r2, [r2, #8]
	cmp	r2, r5
	ldr	r3, [sp, #12]
	beq	.L2771
	mov	r2, r3
	mov	r1, r5
	ldr	r0, .L2777+8
	bl	printk
	movs	r2, #4
	ldr	r0, .L2777+12
	mov	r3, r2
	ldr	r1, [r8, #1256]
	bl	rknand_print_hex
	ldrh	r3, [r6, #2420]
	ldr	r0, .L2777+16
	movs	r2, #4
	ldr	r1, [fp, #3384]
	bl	rknand_print_hex
	movs	r3, #1
	str	r3, [fp, #220]
	b	.L2772
.L2771:
	ldr	r2, [r8, #1244]
	cmp	r2, #256
	bne	.L2772
	mov	r2, r3
	mov	r1, r5
	ldr	r0, .L2777+20
	bl	printk
	ldr	r3, [fp, #456]
	addw	r0, fp, #3404
	mov	r1, r5
	add	r3, r3, r4
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2772:
	ldr	r3, .L2777+4
	movs	r1, #0
	ldr	r3, [r3, #456]
	adds	r2, r3, r4
	str	r1, [r2, #4]
	strh	r5, [r3, r4]	@ movhi
	b	.L2765
.L2774:
	movs	r0, #0
.L2764:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2778:
	.align	2
.L2777:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC137
	.word	.LC100
	.word	.LC138
	.word	.LC139
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
	ldr	r4, .L2797
	ldr	r3, [r4, #220]
	cmp	r3, #0
	bne	.L2779
	ldrh	r2, [r4, #316]
	cmp	r2, #0
	beq	.L2781
	ldrb	r3, [r4, #320]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2781
	ldr	r7, .L2797+4
	ldrb	r1, [r4, #319]	@ zero_extendqisi2
	ldrh	r3, [r7, #2384]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L2781
	ldrb	r9, [r4, #322]	@ zero_extendqisi2
	ldr	r6, [r7, #2444]
	ldrh	r8, [r7, #2316]
	cmp	r9, #0
	bne	.L2779
	subs	r6, r6, #1
	mov	r1, sp
	mov	r2, r9
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #3336]
	ldr	r0, [r4, #3312]
	ldr	r3, [sp]
	str	r6, [sp, #20]
	str	r0, [sp, #12]
	str	r3, [sp, #8]
	adds	r3, r3, #1
	str	r5, [sp, #16]
	str	r9, [r5, #4]
	beq	.L2783
	movs	r1, #1
	mov	r2, r9
	add	r0, sp, #4
	bl	FlashReadPages
	b	.L2784
.L2783:
	movs	r1, #255
	ldrh	r2, [r7, #2394]
	bl	ftl_memset
.L2784:
	movs	r7, #0
	ldr	r10, .L2797
	lsl	r8, r8, #2
	movw	r3, #61589
	mov	r9, r7
	strh	r3, [r5]	@ movhi
.L2785:
	cmp	r7, r8
	beq	.L2786
	ldrh	r3, [r4, #316]
	cbz	r3, .L2786
	ldr	r3, [sp, #8]
	adds	r7, r7, #1
	str	r6, [r5, #8]
	ldr	r0, .L2797+8
	str	r3, [r5, #12]
	ldrh	r3, [r10, #312]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r10, #508]
	movs	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, #0
	it	eq
	moveq	r3, r9
	str	r3, [r10, #508]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r4, #312]
	bl	decrement_vpc_count
	b	.L2785
.L2786:
	movs	r3, #1
	b	.L2796
.L2781:
	movs	r3, #0
.L2796:
	strb	r3, [r4, #322]
.L2779:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2798:
	.align	2
.L2797:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+312
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
	ldr	r5, .L2804
	movs	r7, #12
	ldr	r6, .L2804+4
.L2800:
	ldrh	r3, [r5, #2422]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2803
	ldr	r3, [r6, #456]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2801
	bl	flush_l2p_region
.L2801:
	adds	r4, r4, #1
	b	.L2800
.L2803:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L2805:
	.align	2
.L2804:
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
	ldr	r4, .L2832
	ldrh	r5, [r0]
	ldr	r3, [r4, #220]
	cmp	r3, #0
	bne	.L2807
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2808
	ldr	r3, [r4, #292]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L2809
	bl	INSERT_DATA_LIST
	b	.L2808
.L2809:
	bl	INSERT_FREE_LIST
.L2808:
	ldr	r2, .L2832
	movs	r3, #0
	strb	r3, [r6, #8]
	add	r3, r2, #360
	cmp	r6, r3
	beq	.L2810
	ldr	r3, .L2832+4
	ldrh	r1, [r3, #2336]
	cmp	r1, #1
	beq	.L2810
	ldrb	r0, [r3, #140]	@ zero_extendqisi2
	cbz	r0, .L2811
.L2810:
	movs	r3, #1
	strb	r3, [r6, #8]
	b	.L2812
.L2811:
	add	r0, r2, #312
	cmp	r6, r0
	bne	.L2812
	cmp	r1, #3
	beq	.L2814
	ldr	r2, [r2, #600]
	cmp	r2, #1
	bne	.L2815
.L2814:
	movs	r2, #1
	strb	r2, [r4, #320]
.L2815:
	ldr	r3, [r3, #2308]
	cbz	r3, .L2812
	ldr	r3, [r4, #528]
	cmp	r3, #39
	ittt	ls
	movls	r2, #1
	ldrls	r3, .L2832
	strbls	r2, [r3, #320]
.L2812:
	ldrh	r0, [r4, #3456]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2817
	cmp	r5, r0
	bne	.L2818
	ldr	r3, .L2832
	ldr	r3, [r3, #292]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L2819
.L2818:
	bl	update_vpc_list
.L2819:
	movw	r3, #65535
	strh	r3, [r4, #3456]	@ movhi
.L2817:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2807:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L2833:
	.align	2
.L2832:
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
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	movs	r4, #0
	ldr	r1, .L2854
	ldr	r0, .L2854+4
	bl	printk
	ldr	r0, .L2854+8
	movs	r1, #0
	mov	r2, #8192
	bl	memset
	ldr	r5, .L2854+12
	ldr	r7, .L2854+16
.L2835:
	ldr	r3, [r5, #2444]
	ldr	r6, .L2854+12
	cmp	r4, r3
	bcs	.L2852
	mov	r0, r4
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L2836
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r0, r7, r0, lsl #1
	ldrh	r3, [r0, #1332]
	adds	r3, r3, #1
	strh	r3, [r0, #1332]	@ movhi
.L2836:
	adds	r4, r4, #1
	b	.L2835
.L2852:
	ldr	r8, .L2854+20
	movs	r4, #0
	ldr	r7, .L2854+16
.L2838:
	ldrh	r3, [r6, #2324]
	uxth	r1, r4
	ldr	r5, .L2854+20
	cmp	r3, r1
	bls	.L2853
	ldr	r3, [r8, #292]
	ldrh	r2, [r3, r1, lsl #1]
	add	r3, r7, r1, lsl #1
	ldrh	r3, [r3, #1332]
	cmp	r2, r3
	beq	.L2839
	ldr	r0, .L2854+24
	bl	printk
.L2839:
	adds	r4, r4, #1
	b	.L2838
.L2853:
	ldr	r3, [r5, #304]
	cbz	r3, .L2834
	ldr	r2, [r5, #284]
	movs	r6, #0
	ldrh	r7, [r5, #308]
	mov	r9, #6
	subs	r3, r3, r2
	ldr	r2, .L2854+28
	ldr	r8, .L2854+16
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r4, r3
.L2842:
	uxth	r3, r6
	cmp	r3, r7
	bcs	.L2834
	ldr	r3, [r5, #292]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L2843
	add	r3, r8, r4, lsl #1
	ldr	r0, .L2854+32
	mov	r1, r4
	ldrh	r3, [r3, #1332]
	bl	printk
.L2843:
	mul	r4, r9, r4
	ldr	r3, [r5, #284]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2842
.L2834:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2855:
	.align	2
.L2854:
	.word	.LANCHOR3+72
	.word	.LC108
	.word	.LANCHOR4+1332
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LC140
	.word	-1431655765
	.word	.LC141
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
	ldr	r0, .L2871
	bl	printk
	ldr	r7, .L2871+4
	ldr	r5, .L2871+8
	movs	r4, #0
.L2857:
	ldr	r3, [r7, #2444]
	cmp	r4, r3
	bcs	.L2870
	mov	r0, r4
	add	r1, sp, #28
	movs	r2, #0
	bl	log2phys
	ubfx	r3, r4, #0, #11
	cbnz	r3, .L2858
	ldr	r0, .L2871+12
	mov	r1, r4
	ldr	r2, [sp, #28]
	bl	printk
.L2858:
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L2860
	str	r3, [r5, #1248]
	movs	r1, #1
	ldr	r3, .L2871+16
	ldr	r0, .L2871+20
	str	r4, [r5, #1260]
	ldr	r2, [r3, #3312]
	ldr	r6, [r3, #3336]
	str	r2, [r5, #1252]
	movs	r2, #0
	str	r6, [r5, #1256]
	str	r2, [r5, #1244]
	bl	FlashReadPages
	ldr	r3, [r5, #1244]
	ldr	r2, .L2871+8
	cmp	r3, #256
	beq	.L2861
	adds	r3, r3, #1
	beq	.L2861
	ldr	r3, [r6, #8]
	cmp	r3, r4
	beq	.L2860
.L2861:
	ldr	r3, [r2, #1256]
	ldr	r1, [r2, #1252]
	ldr	r0, [r3, #4]
	str	r0, [sp]
	ldr	r0, [r3, #8]
	str	r0, [sp, #4]
	ldr	r0, [r3, #12]
	str	r0, [sp, #8]
	ldr	r0, [r1]
	str	r0, [sp, #12]
	ldr	r1, [r1, #4]
	ldr	r0, .L2871+24
	str	r1, [sp, #16]
	mov	r1, r4
	ldr	r2, [r2, #1248]
	ldr	r3, [r3]
	bl	printk
.L2860:
	adds	r4, r4, #1
	b	.L2857
.L2870:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2872:
	.align	2
.L2871:
	.word	.LC142
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC143
	.word	.LANCHOR2
	.word	.LANCHOR4+1244
	.word	.LC144
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
	ldr	r5, .L2882
	ldr	r4, [r5, #292]
	ldrh	r3, [r4, r0, lsl #1]
	cbnz	r3, .L2874
	ldr	r4, [r5, #304]
	cbz	r4, .L2875
	ldr	r1, [r5, #284]
	mov	lr, #6
	ldr	r2, .L2882+4
	movw	ip, #65535
	subs	r4, r4, r1
	ldrh	r7, [r5, #308]
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L2876:
	uxth	r2, r3
	cmp	r2, r7
	bcs	.L2875
	cmp	r4, r0
	bne	.L2877
	mov	r1, r4
	ldr	r0, .L2882+8
	bl	List_remove_node
	ldrh	r3, [r5, #308]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #308]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #292]
	ldrh	r3, [r2, r4, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r4, lsl #1]	@ movhi
	b	.L2875
.L2877:
	mul	r4, lr, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, ip
	bne	.L2876
	b	.L2875
.L2874:
	adds	r3, r3, #1
	strh	r3, [r4, r0, lsl #1]	@ movhi
.L2875:
	mov	r0, r6
	add	r1, sp, #4
	movs	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2883:
	.align	2
.L2882:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+304
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
	beq	.L3031
	ldrh	r3, [r0, #2]
	movs	r5, #0
	ldr	r6, .L3047
	str	r3, [sp, #12]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	str	r3, [sp, #28]
	ldrh	r3, [r6, #2384]
	cmp	r3, r2
	bne	.L2887
	strh	r5, [r0, #4]	@ movhi
	strb	r5, [r0, #6]
	b	.L3031
.L2887:
	ldrh	r0, [r0, #16]
.L2888:
	cmp	r0, r7
	add	r5, r5, #1
	bne	.L3042
	uxth	r3, r5
	adds	r3, r3, #8
	ldrh	r0, [r4, r3, lsl #1]
	b	.L2888
.L3042:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2890
	bl	FtlGetLastWrittenPage
	adds	r2, r0, #1
	mov	r5, r0
	beq	.L2891
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbnz	r3, .L3033
	ldr	r3, .L3047
	add	r3, r3, r0, lsl #1
	ldrh	r7, [r3, #144]
	b	.L2965
.L2890:
	movs	r1, #0
	bl	FtlGetLastWrittenPage
	adds	r3, r0, #1
	mov	r5, r0
	beq	.L2891
.L3033:
	mov	r7, r5
.L2965:
	ldr	r3, .L3047+4
	movw	r9, #65535
	ldrh	ip, [r6, #2316]
	mov	r10, #36
	ldrh	r0, [r6, #2396]
	ldr	r2, [r3, #3300]
	ldr	r8, [r3, #1144]
	add	r3, r4, #14
	str	r3, [sp, #20]
	str	r2, [sp, #4]
	movs	r2, #0
	mov	lr, r3
	mov	r6, r2
	mov	fp, r2
	b	.L2893
.L2891:
	movs	r3, #0
	strh	r3, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
	b	.L3031
.L2896:
	ldrh	r3, [lr, #2]!
	cmp	r3, r9
	beq	.L2894
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
.L2894:
	adds	r2, r2, #1
.L2893:
	uxth	r3, r2
	cmp	r3, ip
	bcc	.L2896
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2967
	ldr	r3, .L3047
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	adds	r3, r1, #0
	it	ne
	movne	r3, #1
	b	.L3034
.L2967:
	movs	r3, #0
.L3034:
	ldr	r8, .L3047+4
	mov	r1, r6
	str	r3, [sp, #24]
	mov	fp, #0
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #3300]
	bl	FlashReadPages
	ldr	r3, [r8, #508]
	subs	r3, r3, #1
	str	r8, [sp, #8]
	str	r3, [sp, #4]
	movw	r3, #65535
	str	r3, [sp, #16]
.L2898:
	uxth	r9, fp
	cmp	r9, r6
	bcs	.L2905
	movs	r1, #36
	ldr	r0, [r8, #3300]
	mul	r1, r1, fp
	add	lr, r0, r1
	ldr	r1, [r0, r1]
	cbnz	r1, .L2899
	ldr	r3, [lr, #12]
	ldr	r10, [r3, #4]
	cmp	r10, #-1
	beq	.L2900
	mov	r0, r10
	ldr	r1, [r8, #508]
	str	r3, [sp, #32]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #32]
	cbz	r0, .L2900
	add	r1, r10, #1
	str	r1, [r8, #508]
.L2900:
	ldr	r1, [r3]
	adds	r1, r1, #1
	bne	.L2901
.L2905:
	cmp	r9, r6
	ldr	r6, .L3047+4
	bne	.L3032
	ldr	r3, [r6, #3300]
	adds	r5, r5, #1
	uxth	r10, r5
	ldr	r0, [r3, #4]
	b	.L3035
.L2899:
	ldr	r1, [lr, #4]
	ldr	r0, .L3047+8
	bl	printk
	uxth	r3, r7
	ldrh	r1, [r4]
	str	r3, [sp, #16]
	ldr	r3, .L3047+12
	strh	r1, [r3, #836]	@ movhi
.L2901:
	add	fp, fp, #1
	b	.L2898
.L3032:
	ldr	r3, [sp, #8]
	movs	r2, #36
	uxth	r10, r5
	ldr	r3, [r3, #3300]
	mla	r9, r2, r9, r3
	ldr	r0, [r9, #4]
.L3035:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r5, [r4, #8]	@ zero_extendqisi2
	ldr	r2, .L3047
	cmp	r5, #1
	str	r0, [sp, #8]
	bne	.L2907
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	cbnz	r3, .L2907
	add	r3, r2, r10, lsl #1
	ldrh	r10, [r3, #144]
.L2907:
	ldrh	r3, [r2, #2384]
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
	bne	.L2909
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #32]
	cmp	r3, r2
	bne	.L2909
	mov	r0, r4
	mov	r1, r10
	mov	r2, r3
	b	.L3040
.L2909:
	ldr	r2, [sp, #16]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2910
	cmp	r5, #0
	bne	.L2911
.L2910:
	ldr	r3, [r6, #3468]
	uxth	fp, r7
	mov	r9, #-1
	ldr	r7, .L3047+4
	adds	r3, r3, #1
	mov	r8, r9
	ittt	eq
	ldreq	r3, .L3047+4
	ldreq	r2, [sp, #4]
	streq	r2, [r3, #3468]
	ldr	r3, [r6, #3468]
	str	r3, [sp, #16]
	ldr	r3, [sp, #12]
	adds	r3, r3, #7
	cmp	fp, r3
	itet	gt
	subgt	r5, fp, #7
	ldrle	r5, [sp, #12]
	uxthgt	r5, r5
.L2914:
	cmp	r5, fp
	bhi	.L2927
	ldr	r3, .L3047
	ldr	r1, [sp, #20]
	ldrh	ip, [r3, #2316]
	ldr	r3, [r7, #3300]
	str	r3, [sp, #28]
	movs	r3, #0
	mov	r6, r3
.L2928:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3043
	ldrh	r2, [r1, #2]!
	movw	r0, #65535
	cmp	r2, r0
	beq	.L2915
	ldr	r0, [sp, #28]
	mov	lr, #36
	orr	r2, r5, r2, lsl #10
	mla	r0, lr, r6, r0
	adds	r6, r6, #1
	uxth	r6, r6
	str	r2, [r0, #4]
.L2915:
	adds	r3, r3, #1
	b	.L2928
.L3043:
	mov	r1, r6
	ldr	r2, [sp, #24]
	ldr	r0, [r7, #3300]
	bl	FlashReadPages
	ldr	r3, .L3047
	movs	r2, #36
	add	lr, r7, r5, lsl #1
	movw	r1, #65535
	ldrb	r0, [r3, #140]	@ zero_extendqisi2
	ldr	r3, [r7, #3300]
	mla	r6, r2, r6, r3
.L2917:
	cmp	r3, r6
	beq	.L3044
	ldr	r2, [r3]
	cbnz	r2, .L2918
	ldr	r2, [r3, #12]
	ldrh	ip, [r2]
	cmp	ip, r1
	beq	.L2919
	ldr	r2, [r2, #4]
	cmp	r2, #-1
	beq	.L2919
	cmp	r9, #-1
	ldr	r8, [r7, #3468]
	str	r2, [r7, #3468]
	bne	.L2919
	ldrh	r2, [lr, #1216]
	cmp	r2, r1
	bne	.L2920
	cmp	r0, #0
	beq	.L2919
.L2920:
	ldr	r2, [sp, #4]
	cmp	r8, r2
	ite	ne
	movne	r9, r8
	moveq	r9, #-1
	b	.L2919
.L3048:
	.align	2
.L3047:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC145
	.word	.LANCHOR5
.L2918:
	ldr	r3, .L3049
	ldrh	r2, [r4]
	strh	r2, [r3, #836]	@ movhi
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cbnz	r3, .L2911
	ldr	r3, .L3049+4
	movw	r2, #65535
	add	r5, r3, r5, lsl #1
	ldrh	r1, [r5, #1216]
	cmp	r1, r2
	bne	.L2922
	cmp	r9, #-1
	beq	.L2923
	str	r9, [r3, #3468]
	b	.L2911
.L2923:
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	bne	.L3036
	ldr	r2, [r3, #3468]
	b	.L3041
.L2922:
	ldr	r2, [sp, #4]
	cmp	r8, r2
	beq	.L2925
	cmp	r8, #-1
	beq	.L2911
	str	r8, [r3, #3468]
	b	.L2911
.L2925:
	ldr	r2, [r3, #3468]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	beq	.L2911
.L3041:
	subs	r2, r2, #1
	b	.L3036
.L2919:
	adds	r3, r3, #36
	b	.L2917
.L3044:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L2914
.L2927:
	ldr	r3, .L3049+4
	mov	r2, #-1
.L3036:
	str	r2, [r3, #3468]
.L2911:
	ldr	r3, .L3049
	movs	r2, #1
	ldr	fp, [sp, #12]
	ldr	r5, .L3049+4
	strh	r2, [r3, #838]	@ movhi
.L2929:
	ldr	r3, .L3049+8
	movw	r9, #65535
	ldr	r1, [r5, #3300]
	movs	r6, #0
	ldr	r7, [sp, #20]
	ldrh	r8, [r3, #2316]
	ldrb	r0, [r3, #140]	@ zero_extendqisi2
	str	r6, [sp, #16]
.L2930:
	uxth	r3, r6
	cmp	r3, r8
	bcs	.L3045
	ldrh	r3, [r7, #2]!
	cmp	r3, r9
	beq	.L2931
	ldr	r2, [sp, #16]
	mov	lr, #36
	orr	r3, fp, r3, lsl #10
	mla	r2, lr, r2, r1
	str	r3, [r2, #4]
	ldrb	lr, [r4, #8]	@ zero_extendqisi2
	cmp	lr, #1
	bne	.L2932
	cbz	r0, .L2932
	orr	r3, r3, #-2147483648
	str	r3, [r2, #4]
.L2932:
	ldr	r3, [sp, #16]
	add	ip, r3, #1
	uxth	r3, ip
	str	r3, [sp, #16]
.L2931:
	adds	r6, r6, #1
	b	.L2930
.L3045:
	ldr	r0, [r5, #3300]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	movs	r3, #0
.L3039:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3046
	ldr	r3, [sp, #28]
	movs	r6, #36
	ldr	r9, [r5, #3300]
	muls	r6, r3, r6
	add	r8, r9, r6
	ldr	r7, [r8, #4]
	ubfx	r0, r7, #10, #16
	str	r7, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #12]
	cmp	fp, r3
	bcc	.L2935
	bne	.L2936
	ldr	r3, [sp, #32]
	cmp	r0, r3
	bcc	.L2935
.L2936:
	cmp	fp, r10
	bne	.L2937
	ldr	r3, [sp, #8]
	cmp	r0, r3
	beq	.L2938
.L2937:
	ldr	r3, [r9, r6]
	adds	r3, r3, #1
	beq	.L2939
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L2940
	ldrh	r0, [r4]
	b	.L3037
.L2940:
	ldr	r3, [r8, #4]
	adds	r7, r3, #1
	str	r3, [sp, #4]
	beq	.L2941
	mov	r0, r3
	ldr	r1, [r5, #508]
	bl	ftl_cmp_data_ver
	cbz	r0, .L2941
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [r5, #508]
.L2941:
	ldr	r7, [r8, #8]
	add	r1, sp, #56
	ldr	r3, [r8, #12]
	movs	r2, #0
	mov	r0, r7
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r5, #3468]
	adds	r0, r1, #1
	beq	.L2942
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2942
	ldr	r3, [sp, #52]
	adds	r2, r3, #1
	beq	.L2943
	ldr	r0, [r5, #3300]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r6
	str	r3, [r0, #4]
	ldr	r9, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r5, #3300]
	ldr	r3, [r9, #4]
	add	ip, r2, r6
	str	r3, [sp, #36]
	ldr	r3, [r2, r6]
	adds	r3, r3, #1
	bne	.L2944
	b	.L2945
.L2943:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L2935
	mov	r0, r7
	add	r1, sp, #52
	movs	r2, #1
	bl	log2phys
	b	.L2935
.L2944:
	ldr	r8, [r9, #8]
	cmp	r8, r7
	bne	.L2945
	ldr	r0, [r5, #3468]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	cbz	r0, .L2945
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L2950
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L2945
	adds	r7, r3, #1
	beq	.L2948
	str	r3, [ip, #4]
	mov	r0, ip
	movs	r1, #1
	movs	r2, #0
	ldr	r9, [ip, #12]
	bl	FlashReadPages
	b	.L2949
.L2948:
	str	r3, [r2, r6]
.L2949:
	ldr	r3, [r5, #3300]
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	beq	.L2950
	ldr	r6, [r9, #4]
	ldr	r0, [r5, #3468]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L2950
	ldr	r0, [sp, #36]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L2945
.L2950:
	mov	r0, r8
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L2945:
	mov	r3, #-1
	str	r3, [sp, #52]
	b	.L2952
.L3050:
	.align	2
.L3049:
	.word	.LANCHOR5
	.word	.LANCHOR2
	.word	.LANCHOR0
.L2942:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L2952
	ldr	r3, [sp, #52]
	adds	r0, r3, #1
	beq	.L2954
	ldr	r2, .L3051
	ubfx	r3, r3, #10, #21
	ldr	r2, [r2, #2332]
	cmp	r3, r2
	bcs	.L2935
.L2954:
	mov	r0, r7
	add	r1, sp, #60
	movs	r2, #1
	bl	log2phys
	ldr	r8, [sp, #56]
	cmp	r8, #-1
	beq	.L2952
	ldr	r3, [sp, #52]
	cmp	r8, r3
	beq	.L2952
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #312]
	cmp	r3, r0
	beq	.L2956
	ldrh	r3, [r5, #360]
	cmp	r3, r0
	beq	.L2956
	ldrh	r3, [r5, #408]
	cmp	r3, r0
	bne	.L2952
.L2956:
	ldr	r3, .L3051+4
	movs	r1, #1
	movs	r2, #0
	ldr	r0, [r3, #3300]
	str	r8, [r0, #4]
	ldr	r6, [r0, #12]
	bl	FlashReadPages
	ldr	r3, .L3051+4
	ldr	r1, [r6, #4]
	ldr	r3, [r3, #3300]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L2952
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cbnz	r0, .L2952
	mov	r0, r7
	add	r1, sp, #56
	movs	r2, #1
	bl	log2phys
.L2952:
	ldr	r0, [sp, #52]
	adds	r1, r0, #1
	beq	.L2935
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #292]
	ldrh	r3, [r3, r0, lsl #1]
	mov	r1, r0
	cbz	r3, .L2957
.L3037:
	bl	decrement_vpc_count
	b	.L2935
.L2957:
	ldr	r0, .L3051+8
	bl	printk
	b	.L2935
.L2939:
	ldrh	r3, [r4]
	mov	r1, r7
	ldr	r2, .L3051+12
	ldr	r0, .L3051+16
	strh	r3, [r2, #836]	@ movhi
	ldr	r2, [sp, #4]
	bl	printk
	ldr	r3, .L3051+12
	ldr	r3, [r3, #840]
	cmp	r3, #31
	bhi	.L2958
	ldr	r2, .L3051+12
	ldr	r1, [sp, #60]
	add	r2, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r2, #844]
	ldr	r2, .L3051+12
	str	r3, [r2, #840]
.L2958:
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	ldr	r3, [r5, #3468]
	adds	r2, r3, #1
	bne	.L2959
	ldr	r3, [sp, #4]
	b	.L3038
.L2959:
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bls	.L2935
	mov	r3, r2
.L3038:
	str	r3, [r5, #3468]
.L2935:
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	b	.L3039
.L3046:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	fp, fp, #1
	cmp	r3, #1
	uxth	fp, fp
	ldr	r3, .L3051
	bne	.L2961
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cbz	r2, .L2961
	ldrh	r2, [r3, #2386]
	cmp	r2, fp
	bne	.L2961
	cmp	r10, fp
	beq	.L2938
.L2961:
	ldrh	r3, [r3, #2384]
	cmp	fp, r3
	bne	.L2929
	ldr	r2, .L3051
	movw	r0, #65535
	movs	r3, #0
	strh	fp, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	ldrh	r1, [r2, #2316]
.L2962:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L3031
	ldr	r6, [sp, #20]
	adds	r3, r3, #1
	ldrh	r5, [r6, #2]!
	cmp	r5, r0
	str	r6, [sp, #20]
	beq	.L2962
	strb	r2, [r4, #6]
	b	.L3031
.L2938:
	ldrb	r3, [sp, #8]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, [sp, #8]
	mov	r1, r10
	strh	r10, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
.L3040:
	bl	ftl_sb_update_avl_pages
.L3031:
	movs	r0, #0
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3052:
	.align	2
.L3051:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC146
	.word	.LANCHOR5
	.word	.LC147
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
	ldr	r7, .L3066
	ldr	r1, .L3066+4
	ldr	r0, .L3066+8
	bl	printk
	ldr	r5, .L3066+12
	ldrh	r2, [r7, #2326]
	movs	r1, #0
	ldr	r0, [r5, #3360]
	lsls	r2, r2, #1
	bl	ftl_memset
.L3054:
	ldr	r3, [r7, #2444]
	cmp	r4, r3
	bcs	.L3064
	mov	r0, r4
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L3055
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #3360]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L3055:
	adds	r4, r4, #1
	b	.L3054
.L3064:
	ldr	r6, .L3066+12
	mov	r8, #0
	movw	r9, #65535
.L3057:
	ldrh	r3, [r7, #2324]
	uxth	r4, r8
	cmp	r3, r4
	bls	.L3065
	ldr	r3, [r5, #292]
	ldrh	r2, [r3, r4, lsl #1]
	ldr	r3, [r5, #3360]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r2, r3
	beq	.L3058
	cmp	r2, r9
	beq	.L3058
	ldrh	r1, [r6, #312]
	cmp	r1, r4
	beq	.L3058
	ldrh	r1, [r6, #408]
	cmp	r1, r4
	beq	.L3058
	ldrh	r1, [r6, #360]
	cmp	r1, r4
	beq	.L3058
	ldr	r0, .L3066+16
	mov	r1, r4
	bl	printk
	ldr	r3, [r6, #292]
	ldrh	r2, [r3, r4, lsl #1]
	cbnz	r2, .L3059
	ldr	r2, [r6, #3360]
	ldrh	r2, [r2, r4, lsl #1]
	strh	r2, [r3, r4, lsl #1]	@ movhi
	b	.L3058
.L3059:
	ldr	r2, [r6, #3360]
	mov	r0, r4
	ldrh	r2, [r2, r4, lsl #1]
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	update_vpc_list
.L3058:
	add	r8, r8, #1
	b	.L3057
.L3065:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3067:
	.align	2
.L3066:
	.word	.LANCHOR0
	.word	.LANCHOR3+84
	.word	.LC108
	.word	.LANCHOR2
	.word	.LC148
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
	ldr	r3, .L3122
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldrh	r5, [r3, #3352]
	movw	r3, #65535
	mov	r4, r0
	str	r1, [sp, #20]
	cmp	r5, r3
	beq	.L3104
	cbnz	r5, .L3069
	b	.L3070
.L3104:
	movs	r5, #0
.L3069:
	ldr	r3, .L3122+4
	ldr	r2, [sp, #20]
	ldrh	r3, [r3, #2384]
	cmp	r2, r3
	bne	.L3071
.L3070:
	bl	FtlGcPageVarInit
.L3071:
	ldr	r6, .L3122+8
	mov	r3, #-1
	str	r3, [sp, #16]
	movs	r3, #0
	str	r3, [sp, #4]
.L3072:
	ldrh	r1, [r4]
	movw	r3, #65535
	movs	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L3073
.L3074:
	ldr	r10, .L3122+4
.L3101:
	ldr	r3, [r6, #3300]
	movs	r2, #0
	ldrh	lr, [r10, #2316]
	add	ip, r4, #14
	ldr	r8, [r6, #1144]
	mov	fp, #36
	str	r3, [sp, #8]
	mov	r7, r2
	ldr	r3, [r6, #1140]
	movw	r9, #65535
	str	r3, [sp, #12]
	ldrh	r3, [r10, #2394]
	str	r3, [sp, #24]
	ldrh	r3, [r10, #2396]
	str	r3, [sp, #28]
.L3075:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L3120
	ldrh	r3, [ip, #2]!
	cmp	r3, r9
	beq	.L3076
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
.L3076:
	adds	r2, r2, #1
	b	.L3075
.L3120:
	ldr	r0, [r6, #3300]
	mov	r1, r7
	movs	r2, #0
	bl	FlashReadPages
	movs	r3, #0
.L3119:
	str	r3, [sp, #8]
	ldrh	r3, [sp, #8]
	cmp	r3, r7
	bcs	.L3121
	ldr	r3, [sp, #8]
	mov	r9, #36
	mul	r9, r9, r3
	ldr	r3, .L3122+8
	ldr	r8, [r3, #3300]
	add	fp, r8, r9
	ldr	r3, [fp, #4]
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #12]
	bl	P2V_plane
	ldr	r8, [r8, r9]
	ldr	r3, [fp, #12]
	ldr	ip, .L3122+4
	ldr	fp, .L3122+8
	mov	r2, r0
	cmp	r8, #0
	bne	.L3081
	ldrh	r0, [r3]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L3082
.L3085:
	ldrb	r3, [ip, #140]	@ zero_extendqisi2
	cbz	r3, .L3116
	movs	r3, #1
	str	r3, [fp, #3464]
	b	.L3073
.L3082:
	ldr	r0, [r3, #8]
	ldr	r1, [r10, #2444]
	cmp	r0, r1
	bls	.L3117
	b	.L3085
.L3116:
	ldrh	r1, [r4]
	ldr	r2, [fp, #292]
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
	strh	r3, [fp, #552]	@ movhi
	b	.L3118
.L3117:
	ldrb	r2, [r10]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3089
	add	r1, sp, #32
	mov	r2, r8
	str	r3, [sp, #24]
	bl	log2phys
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #32]
	ldr	r1, [r3, #12]
	cmp	r2, r1
	bne	.L3089
	adds	r1, r2, #1
	beq	.L3089
	str	r2, [sp, #40]
	movs	r1, #1
	ldr	r2, [r6, #3332]
	add	r0, sp, #36
	str	r2, [sp, #44]
	ldr	r2, [r6, #3340]
	str	r2, [sp, #48]
	mov	r2, r8
	bl	FlashReadPages
	ldrh	r2, [r10, #2390]
	ldr	r1, [r6, #3300]
	lsl	lr, r2, #7
	add	r9, r9, r1
	ldr	r0, [sp, #44]
	ldr	r3, [sp, #24]
.L3091:
	cmp	r8, lr
	beq	.L3089
	ldr	r1, [r9, #8]
	ldr	r2, [r0, r8, lsl #2]
	ldr	r1, [r1, r8, lsl #2]
	cmp	r1, r2
	beq	.L3092
	ldrh	r1, [r4]
	ldr	r2, [sp, #40]
	ldr	r0, .L3122+12
	bl	printk
	ldr	r3, [r6, #292]
	ldrh	r2, [r4]
	movs	r1, #0
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
	strh	r3, [r6, #552]	@ movhi
	b	.L3118
.L3092:
	add	r8, r8, #1
	b	.L3091
.L3089:
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	ldr	r1, [sp, #12]
	bl	FtlGcUpdatePage
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	b	.L3119
.L3081:
	ldr	r2, [sp, #12]
	ldr	r0, .L3122+16
	ldrh	r1, [r4]
	str	ip, [sp, #8]
	bl	printk
	ldrh	r3, [r4]
	ldr	ip, [sp, #8]
	ldr	r2, [ip, #2308]
	cbnz	r2, .L3095
	ldrb	r2, [ip, #140]	@ zero_extendqisi2
	cbz	r2, .L3096
.L3095:
	ldr	r2, [fp, #228]
	ldrh	r2, [r2, r3, lsl #1]
	cmp	r2, #159
	bls	.L3097
.L3096:
	ldr	r2, [fp, #3300]
	ldr	r2, [r2, r9]
	adds	r2, r2, #1
	bne	.L3098
.L3097:
	ldr	r2, [fp, #3300]
	add	r9, r9, r2
	ldr	r2, [r9, #4]
	str	r2, [sp, #16]
.L3098:
	ldr	r2, .L3122+8
	movs	r1, #0
	ldr	r2, [r2, #292]
	strh	r1, [r2, r3, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
.L3118:
	bl	FtlGcPageVarInit
	movs	r5, #0
	b	.L3072
.L3121:
	ldr	r3, [sp, #4]
	adds	r5, r5, #1
	ldr	r2, [sp, #20]
	adds	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp, #4]
	bcs	.L3100
.L3102:
	ldrh	r3, [r10, #2384]
	cmp	r3, r5
	bhi	.L3101
	movs	r2, #0
	b	.L3073
.L3100:
	ldr	r2, .L3122
	movw	r1, #65535
	ldrh	r3, [r2, #3352]
	cmp	r3, r1
	beq	.L3102
	ldr	r1, [sp, #4]
	add	r3, r3, r1
	strh	r3, [r2, #3352]	@ movhi
	ldrh	r3, [r10, #2384]
	cmp	r3, r5
	bls	.L3102
	b	.L3103
.L3073:
	ldr	r3, .L3122
	movw	r1, #65535
	strh	r5, [r4, #2]	@ movhi
	mov	r0, r4
	strb	r2, [r4, #6]
	strh	r1, [r3, #3352]	@ movhi
	mov	r1, r5
	bl	ftl_sb_update_avl_pages
.L3103:
	ldr	r0, [sp, #16]
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3123:
	.align	2
.L3122:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC149
	.word	.LC150
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
	ldr	r5, .L3140
	ldr	r4, .L3140+4
	ldr	r9, [r5, #684]
	mov	r6, r5
	cmp	r9, #0
	beq	.L3125
	ldr	r2, [r5, #688]
	ldr	r3, [r4, #2444]
	cmp	r2, r3
	bcs	.L3126
	mov	r5, #2048
	mov	r7, r6
.L3131:
	ldr	r0, [r6, #688]
	ldr	r3, [r4, #2444]
	cmp	r0, r3
	bcs	.L3130
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r3, [r7, #688]
	adds	r3, r3, #1
	str	r3, [r7, #688]
	ldr	r2, [sp]
	adds	r1, r2, #1
	beq	.L3129
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
	bne	.L3130
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3130:
	mov	r0, #-1
	b	.L3133
.L3129:
	subs	r5, r5, #1
	bne	.L3131
	b	.L3130
.L3126:
	ldr	r3, [r5, #472]
	movs	r0, #0
	str	r0, [r5, #684]
	str	r0, [r5, #688]
	str	r3, [r5, #680]
	b	.L3133
.L3125:
	ldr	r8, [r5, #472]
	ldr	r1, [r5, #524]
	ldr	r7, [r5, #680]
	add	r2, r8, #1048576
	ldr	r3, [r4, #2444]
	movw	r4, #10000
	cmp	r1, r4
	ite	hi
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r2
	bhi	.L3135
	mov	r0, #1000
	lsrs	r1, r1, #10
	adds	r1, r1, #1
	muls	r0, r3, r0
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r0, r8
	bcc	.L3135
	ldrh	r3, [r5, #264]
	ands	r0, r4, r3
	bne	.L3137
	ldr	r2, [r5, #704]
	cmp	r2, r3
	beq	.L3133
.L3135:
	ldrh	r3, [r6, #264]
	movs	r0, #0
	str	r8, [r6, #680]
	str	r0, [r6, #688]
	str	r3, [r6, #704]
	movs	r3, #1
	str	r3, [r6, #684]
	b	.L3133
.L3137:
	mov	r0, r9
.L3133:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3141:
	.align	2
.L3140:
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
	ldr	r5, .L3180
	ldr	r6, .L3180+4
	ldr	r2, [r5, #220]
	mov	r4, r5
	ldrh	r1, [r6, #2384]
	cmp	r2, #0
	bne	.L3178
	ldrh	r5, [r5, #408]
	movw	r7, #65535
	cmp	r5, r7
	bne	.L3145
.L3154:
	ldrh	r2, [r4, #408]
	movw	r3, #65535
	movs	r7, #0
	ldr	r5, .L3180
	cmp	r2, r3
	str	r7, [r4, #3464]
	beq	.L3178
	bl	FtlCacheWriteBack
	ldrb	r0, [r5, #415]	@ zero_extendqisi2
	ldrh	r3, [r6, #2384]
	mov	r10, #12
	ldr	r2, [r5, #292]
	ldrh	r1, [r5, #408]
	smulbb	r3, r0, r3
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r3, [r5, #492]
	ldrh	r2, [r5, #1168]
	add	r3, r3, r2
	str	r3, [r5, #492]
	b	.L3155
.L3145:
	cbz	r0, .L3148
	ldr	r3, .L3180+8
	ldrh	r0, [r3, #3352]
	cmp	r0, r7
	beq	.L3149
.L3150:
	movs	r1, #2
	b	.L3148
.L3149:
	strh	r2, [r3, #3352]	@ movhi
	ldrh	r3, [r4, #308]
	cmp	r3, #17
	bhi	.L3150
.L3148:
	ldr	r7, .L3180
	add	r0, r7, #408
	bl	FtlGcScanTempBlk
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L3151
	ldr	r2, [r7, #228]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L3152
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L3152:
	ldr	r3, [r4, #3464]
	ldr	r2, .L3180
	cbnz	r3, .L3153
	ldr	r0, [sp, #4]
	ldr	r3, [r2, #700]
	ubfx	r0, r0, #10, #16
	adds	r3, r3, #1
	str	r3, [r2, #700]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3153:
	movs	r3, #0
	str	r3, [r4, #3464]
	b	.L3165
.L3151:
	ldr	r3, .L3180+8
	ldrh	r2, [r3, #3352]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3165
	b	.L3154
.L3158:
	ldr	r3, [r9, #4]
	cmp	r0, r3
	bne	.L3176
.L3157:
	adds	r7, r7, #1
.L3155:
	ldrh	r3, [r4, #1168]
	uxth	r8, r7
	cmp	r3, r8
	bls	.L3179
	mul	r8, r10, r8
	ldr	fp, [r5, #1164]
	ldr	r3, [r6, #2444]
	add	r9, fp, r8
	ldr	r0, [r9, #8]
	cmp	r0, r3
	bcs	.L3176
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r3, [fp, r8]
	ldr	r0, [sp, #4]
	cmp	r0, r3
	bne	.L3158
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r9, #4
	movs	r2, #1
	mov	r8, r0
	ldr	r0, [r9, #8]
	bl	log2phys
	mov	r0, r8
	b	.L3177
.L3176:
	ldrh	r0, [r5, #408]
.L3177:
	bl	decrement_vpc_count
	b	.L3157
.L3179:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbz	r3, .L3160
	ldr	r3, .L3180
	ldr	r0, .L3180+12
	ldrh	r1, [r3, #408]
	bl	printk
.L3160:
	ldrh	r0, [r4, #408]
	ldr	r3, [r4, #292]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L3161
	bl	INSERT_DATA_LIST
	b	.L3162
.L3161:
	bl	INSERT_FREE_LIST
.L3162:
	movw	r8, #65535
	movs	r5, #0
	strh	r8, [r4, #408]	@ movhi
	strh	r5, [r4, #1168]	@ movhi
	strh	r5, [r4, #1160]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r6, #2308]
	ldr	r7, .L3180
	strh	r8, [r4, #552]	@ movhi
	cbz	r3, .L3163
	ldr	r3, [r7, #528]
	cmp	r3, #39
	bhi	.L3163
	ldrh	r3, [r7, #532]
	ldrh	r2, [r7, #308]
	cmp	r2, r3
	bcs	.L3178
	lsls	r3, r3, #1
	strh	r3, [r7, #1116]	@ movhi
	b	.L3178
.L3163:
	ldrh	r3, [r4, #532]
	ldrh	r1, [r4, #308]
	ldr	r2, .L3180
	add	r0, r3, r3, lsl #1
	cmp	r1, r0, asr #2
	ble	.L3178
	ldrb	r0, [r6, #140]	@ zero_extendqisi2
	cbz	r0, .L3164
	subs	r3, r3, #2
	strh	r3, [r2, #1116]	@ movhi
.L3178:
	movs	r0, #0
	b	.L3144
.L3164:
	movs	r3, #20
	strh	r3, [r2, #1116]	@ movhi
	b	.L3144
.L3165:
	movs	r0, #1
.L3144:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3181:
	.align	2
.L3180:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC151
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
	ldr	r5, .L3184
	ldr	r4, .L3184+4
	ldrh	r1, [r5, #2384]
	add	r0, r4, #408
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #410]
	ldrh	r3, [r5, #2384]
	cmp	r2, r3
	bcc	.L3182
	addw	r0, r4, #3404
	bl	FtlMapBlkWriteDumpData
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #3464]
.L3182:
	pop	{r3, r4, r5, pc}
.L3185:
	.align	2
.L3184:
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
	ldr	r4, .L3187
	ldr	r3, .L3187+4
	add	r6, r4, #312
	add	r4, r4, #360
	mov	r0, r6
	str	r5, [r3, #840]
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
.L3188:
	.align	2
.L3187:
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
	ldr	r5, .L3206
	movw	r7, #65535
	ldr	r6, .L3206+4
	ldr	r4, .L3206+8
	ldrh	r0, [r5, #2320]
	strh	r3, [r6, #838]	@ movhi
	strh	r7, [r6, #836]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r3, [r4, #536]
	cmp	r3, r7
	bne	.L3190
.L3192:
	mov	r7, #-1
	b	.L3191
.L3190:
	bl	FtlLoadSysInfo
	mov	r7, r0
	cmp	r0, #0
	bne	.L3192
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r5, #2422]
	ldr	r2, [r4, #456]
	movs	r0, #12
	mov	r3, r7
.L3193:
	cmp	r3, r1
	bge	.L3198
	mla	lr, r0, r3, r2
	ldr	lr, [lr, #4]
	cmp	lr, #0
	bge	.L3194
.L3198:
	ldrh	r2, [r4, #264]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #264]	@ movhi
	bge	.L3205
	b	.L3195
.L3194:
	adds	r3, r3, #1
	b	.L3193
.L3205:
	ldrh	r3, [r6, #838]
	cbz	r3, .L3199
.L3195:
	ldrh	r1, [r4, #312]
	ldr	r2, [r4, #292]
	ldrh	r0, [r4, #316]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r1, [r4, #292]
	ldrh	r3, [r5, #2384]
	ldrh	r0, [r4, #360]
	ldrh	r6, [r4, #364]
	strh	r3, [r4, #314]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #318]
	strh	r3, [r4, #316]	@ movhi
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r6
	strh	r2, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r5, #2384]
	strb	r3, [r4, #366]
	strh	r3, [r4, #364]	@ movhi
	ldrh	r3, [r4, #266]
	strh	r2, [r4, #362]	@ movhi
	adds	r3, r3, #1
	strh	r3, [r4, #266]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3199:
	ldrh	r0, [r4, #312]
	movw	r3, #65535
	ldr	r6, .L3206+8
	cmp	r0, r3
	beq	.L3200
	ldrh	r3, [r6, #316]
	cbnz	r3, .L3200
	ldrh	r3, [r6, #364]
	cbnz	r3, .L3200
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r6, #360]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	add	r0, r6, #312
	bl	allocate_new_data_superblock
	add	r0, r6, #360
	bl	allocate_new_data_superblock
.L3200:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cbnz	r3, .L3201
	ldrh	r3, [r4, #264]
	lsls	r3, r3, #27
	bne	.L3191
.L3201:
	bl	FtlVpcCheckAndModify
.L3191:
	mov	r0, r7
	pop	{r3, r4, r5, r6, r7, pc}
.L3207:
	.align	2
.L3206:
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldr	r4, .L3242
	ldr	r6, [r4, #220]
	cmp	r6, #0
	bne	.L3210
	ldr	r5, .L3242+4
	mov	r1, r6
	ldr	r0, [r4, #3388]
	ldrh	r2, [r5, #2420]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #2420]
	mov	r1, r6
	ldr	r0, [r4, #3384]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r5, #2320]
	str	r6, [r4, #504]
	str	r6, [r4, #508]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3211
	bl	FtlMakeBbt
.L3211:
	ldr	r0, .L3242+8
	movs	r2, #0
.L3212:
	ldrh	r1, [r5, #2390]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	bge	.L3239
	ldr	r6, [r4, #3328]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r6, r3, lsl #2]
	ldr	r1, [r4, #3332]
	str	r0, [r1, r3, lsl #2]
	b	.L3212
.L3239:
	ldr	r3, .L3242+4
	movs	r6, #0
	ldrh	r7, [r3, #2324]
.L3214:
	ldrh	r3, [r5, #2326]
	ldr	r8, .L3242+4
	cmp	r3, r7
	bls	.L3240
	mov	r0, r7
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r7, r7, #1
	uxth	r7, r7
	add	r0, r0, r6
	uxth	r6, r0
	b	.L3214
.L3240:
	ldrh	r1, [r8, #2316]
	subs	r3, r6, #3
	cmp	r3, r1, lsl #1
	bge	.L3216
.L3220:
	movs	r6, #0
	mov	r7, r6
	b	.L3217
.L3216:
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, [r8, #2416]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r8, #2320]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r6, [r8, #2324]
.L3218:
	ldrh	r3, [r5, #2326]
	cmp	r3, r6
	bls	.L3220
	mov	r0, r6
	adds	r6, r6, #1
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	uxth	r6, r6
	b	.L3218
.L3217:
	ldrh	r3, [r5, #2324]
	uxth	r0, r6
	ldr	r10, .L3242+4
	adds	r6, r6, #1
	cmp	r3, r0
	bls	.L3241
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r0, r7
	uxth	r7, r0
	b	.L3217
.L3241:
	ldrh	r3, [r10, #2326]
	ldrh	r6, [r10, #2316]
	ldr	fp, [r10, #2328]
	str	r3, [r4, #3288]
	mov	r1, r6
	mov	r0, fp
	bl	__aeabi_uidiv
	ubfx	r8, r0, #5, #16
	mov	r9, r0
	add	r3, r8, #36
	strh	r3, [r4, #532]	@ movhi
	movs	r3, #24
	str	r0, [r10, #2444]
	muls	r3, r6, r3
	cmp	r7, r3
	ble	.L3222
	rsb	r0, r7, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, .L3242
	str	r0, [r10, #2444]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r3, #532]	@ movhi
.L3222:
	ldr	r3, [r5, #2308]
	cmp	r3, #1
	bne	.L3223
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r10, [r4, #532]
	uxtah	r0, r10, r0
	add	r10, r10, r0, asr #2
	strh	r10, [r4, #532]	@ movhi
.L3223:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cbz	r3, .L3224
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r10, [r4, #532]
	uxtah	r0, r10, r0
	add	r10, r10, r0, asr #2
	strh	r10, [r4, #532]	@ movhi
.L3224:
	ldrh	r3, [r5, #2378]
	cbz	r3, .L3226
	ldrh	r2, [r4, #532]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #532]	@ movhi
	mul	r2, r6, r3
	cmp	r2, r7
	itttt	gt
	addgt	r3, r3, #32
	ldrgt	r2, .L3242+4
	addgt	r3, r3, r8
	strgt	r9, [r2, #2444]
	itt	gt
	ldrgt	r2, .L3242
	strhgt	r3, [r2, #532]	@ movhi
.L3226:
	ldrh	r2, [r4, #532]
	ldr	r3, [r5, #2444]
	ldr	r7, .L3242+12
	subs	r3, r3, r2
	ldr	r8, .L3242
	muls	r6, r3, r6
	ldrh	r3, [r5, #2384]
	str	r6, [r7, #1280]
	muls	r6, r3, r6
	ldrh	r3, [r5, #2390]
	str	r6, [r5, #2444]
	muls	r6, r3, r6
	str	r6, [r5, #2424]
	bl	FtlBbmTblFlush
	ldrh	r2, [r5, #2326]
	movs	r1, #0
	ldr	r0, [r4, #292]
	movw	r6, #65535
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2324]
	ldr	r0, [r4, #468]
	movs	r1, #255
	movs	r3, #0
	strh	r6, [r4, #552]	@ movhi
	lsrs	r2, r2, #3
	str	r3, [r4, #464]
	strh	r3, [r4, #554]	@ movhi
	strb	r3, [r4, #558]
	strb	r3, [r4, #560]
	strh	r3, [r4, #314]	@ movhi
	strb	r3, [r4, #318]
	strh	r3, [r4, #312]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #320]
	bl	ftl_memset
.L3228:
	ldr	r5, .L3242
	add	r0, r5, #312
	bl	make_superblock
	ldrb	r3, [r4, #319]	@ zero_extendqisi2
	ldrh	r2, [r4, #312]
	cbnz	r3, .L3229
	ldr	r3, [r8, #292]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r8, #312]
	adds	r3, r3, #1
	strh	r3, [r8, #312]	@ movhi
	b	.L3228
.L3229:
	ldr	r3, [r5, #504]
	movw	r8, #65535
	ldrh	r1, [r5, #316]
	str	r3, [r5, #324]
	adds	r3, r3, #1
	str	r3, [r5, #504]
	ldr	r3, [r5, #292]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r5, #362]	@ movhi
	strb	r3, [r5, #366]
	ldrh	r3, [r5, #312]
	adds	r3, r3, #1
	strh	r3, [r5, #360]	@ movhi
	movs	r3, #1
	strb	r3, [r5, #368]
.L3230:
	ldr	r6, .L3242
	add	r0, r6, #360
	bl	make_superblock
	ldrb	r3, [r4, #367]	@ zero_extendqisi2
	ldrh	r2, [r4, #360]
	cbnz	r3, .L3231
	ldr	r3, [r5, #292]
	strh	r8, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r5, #360]
	adds	r3, r3, #1
	strh	r3, [r5, #360]	@ movhi
	b	.L3230
.L3231:
	ldr	r3, [r6, #504]
	movw	r4, #65535
	ldrh	r1, [r6, #364]
	str	r3, [r6, #372]
	adds	r3, r3, #1
	str	r3, [r6, #504]
	ldr	r3, [r6, #292]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r4, [r6, #408]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r3, [r6, #538]	@ movhi
	ldr	r3, [r7, #1280]
	strh	r4, [r6, #540]	@ movhi
	strh	r3, [r6, #542]	@ movhi
	ldr	r3, [r6, #504]
	str	r3, [r6, #544]
	adds	r3, r3, #1
	str	r3, [r6, #504]
	strh	r0, [r6, #536]	@ movhi
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L3210
	ldr	r3, .L3242+16
	movs	r2, #1
	str	r2, [r3, #504]
.L3210:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3243:
	.align	2
.L3242:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR4
	.word	.LANCHOR1
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
	ldr	r4, .L3272
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cbz	r3, .L3245
.L3247:
	movs	r0, #0
	b	.L3246
.L3245:
	ldr	r5, .L3272+4
	ldr	r0, [r5, #1176]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3247
	bl	FlashLoadFactorBbt
	cbz	r0, .L3248
	bl	FlashMakeFactorBbt
.L3248:
	ldr	r0, [r5, #1208]
	bl	FlashReadIdbDataRaw
	cbz	r0, .L3249
	movs	r1, #0
	movs	r2, #16
	mov	r0, sp
	movs	r6, #1
	bl	FlashReadFacBbtData
	movs	r3, #0
	mov	r2, r3
	ldr	r1, [sp]
.L3250:
	lsl	r5, r6, r2
	adds	r0, r3, #1
	tst	r5, r1
	add	r2, r2, #1
	it	ne
	movne	r3, r0
	cmp	r2, #16
	bne	.L3250
	cmp	r3, #6
	bls	.L3268
	movs	r2, #0
	movs	r6, #1
.L3253:
	lsl	r5, r6, r2
	adds	r0, r3, #1
	tst	r5, r1
	add	r2, r2, #1
	it	ne
	movne	r3, r0
	cmp	r2, #24
	bne	.L3253
	cmp	r3, #17
	bhi	.L3254
.L3268:
	strb	r2, [r4, #1]
	b	.L3252
.L3254:
	movs	r3, #36
	strb	r3, [r4, #1]
.L3252:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strh	r3, [r4, #138]	@ movhi
.L3249:
	ldr	r1, .L3272+8
	ldr	r0, .L3272+12
	bl	printk
	ldr	r0, .L3272+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #2320]
	bl	FtlFreeSysBlkQueueInit
	movs	r4, #1
.L3255:
	bl	FtlLoadBbt
	cbz	r0, .L3256
.L3270:
	bl	FtlLowFormat
	cmp	r4, #3
	bhi	.L3271
	adds	r4, r4, #1
	b	.L3255
.L3271:
	mov	r0, #-1
	b	.L3246
.L3256:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3270
	ldr	r3, .L3272+20
	movs	r2, #1
	str	r2, [r3, #504]
.L3246:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L3273:
	.align	2
.L3272:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC77
	.word	.LC76
	.word	.LANCHOR0+112
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
	ldr	r5, .L3290
	ldr	r3, [r5, #220]
	cbz	r3, .L3275
.L3278:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3275:
	ldr	r3, .L3290+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbz	r3, .L3277
	ldr	r3, [r5, #1132]
	lsls	r3, r3, #31
	bpl	.L3277
	ldrh	r3, [r5, #412]
	cmp	r3, #0
	bne	.L3278
.L3277:
	movs	r2, #0
	ldr	r0, [r5, #3304]
	ldr	r1, [r5, #1132]
	movs	r6, #0
	mov	r3, r2
	ldr	r7, .L3290
	bl	FlashProgPages
	mov	r8, #36
.L3279:
	ldr	r1, [r5, #1132]
	uxth	r3, r6
	ldr	r4, .L3290
	cmp	r3, r1
	bcs	.L3289
	mul	r3, r8, r3
	ldr	r0, [r7, #3304]
	adds	r6, r6, #1
	adds	r1, r0, r3
	ldr	r0, [r0, r3]
	ldr	r2, [r1, #12]
	cmp	r0, #-1
	bne	.L3280
	ldrh	r1, [r4, #408]
	movs	r5, #0
	ldr	r2, [r4, #292]
	strh	r5, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #700]
	strh	r0, [r4, #408]	@ movhi
	adds	r2, r2, #1
	str	r2, [r4, #700]
	ldr	r2, [r4, #3304]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3288
.L3280:
	ldr	r0, [r2, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r2, #8]
	bl	FtlGcUpdatePage
	b	.L3279
.L3289:
	ldr	r0, [r4, #3304]
	bl	FtlGcBufFree
	movs	r3, #0
	str	r3, [r4, #1132]
	ldrh	r3, [r4, #412]
	cmp	r3, #0
	bne	.L3278
	movs	r0, #1
	bl	FtlGcFreeTempBlock
.L3288:
	movs	r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L3291:
	.align	2
.L3290:
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
	ldr	r4, .L3295
	ldrh	r2, [r4, #408]
	cmp	r2, r3
	beq	.L3293
	ldrh	r3, [r4, #412]
	cbnz	r3, .L3294
.L3293:
	bl	FtlCacheWriteBack
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3295+4
	strb	r5, [r4, #416]
	bl	allocate_data_superblock
	strh	r5, [r4, #1160]	@ movhi
	strh	r5, [r4, #1168]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3294:
	ldr	r0, .L3295+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L3296:
	.align	2
.L3295:
	.word	.LANCHOR2
	.word	.LANCHOR2+408
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
	ldr	r3, .L3470
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r0, [r3, #220]
	.pad #44
	sub	sp, sp, #44
	mov	r6, r1
	mov	r7, r3
	cmp	r0, #0
	bne	.L3397
	ldr	r2, .L3470+4
	ldr	r1, [r2, #504]
	adds	r1, r1, #1
	beq	.L3457
	ldr	r4, [r3, #3276]
	cmp	r4, #0
	bne	.L3457
	ldrh	r1, [r3, #300]
	cmp	r1, #47
	bls	.L3400
	ldrh	r1, [r2, #3352]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3299
.L3302:
	ldrh	r4, [r7, #1172]
	movw	r1, #65535
	ldr	r3, .L3470
	cmp	r4, r1
	bne	.L3300
	b	.L3301
.L3299:
	ldrh	r3, [r3, #408]
	cmp	r3, r2
	beq	.L3302
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3302
	movs	r0, #1
	b	.L3457
.L3300:
	ldrh	r2, [r3, #1170]
	cmp	r2, r1
	bne	.L3301
	ldrh	r0, [r3, #1174]
	cmp	r0, r2
	beq	.L3301
	ldrh	r1, [r3, #1176]
	cmp	r1, r2
	itttt	ne
	strhne	r4, [r3, #1170]	@ movhi
	strhne	r0, [r3, #1172]	@ movhi
	strhne	r1, [r3, #1174]	@ movhi
	strhne	r2, [r3, #1176]	@ movhi
.L3301:
	ldr	r4, [r7, #1120]
	cmp	r8, #1
	add	r4, r4, #1
	add	r4, r4, r8, lsl #7
	str	r4, [r7, #1120]
	bne	.L3313
	ldr	r3, .L3470+8
	ldr	r2, [r3, #2308]
	mov	r10, r3
	cbnz	r2, .L3304
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3313
.L3304:
	ldr	r3, [r7, #528]
	ldr	r5, .L3470
	cmp	r3, #39
	bhi	.L3313
	ldr	r9, .L3470+12
	ldrh	r3, [r9, #972]
	add	r4, r4, r3
	str	r4, [r5, #1120]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r5, #552]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3313
	ldrh	r2, [r5, #1170]
	cmp	r2, r3
	bne	.L3313
	ldr	r3, [r5, #1120]
	cmp	r3, #1024
	bhi	.L3305
	ldrh	r3, [r5, #308]
	cmp	r3, #63
	bhi	.L3313
.L3305:
	ldrh	r3, [r7, #1118]
	movs	r0, #0
	ldrh	r4, [r7, #308]
	adds	r3, r3, #64
	ldr	r2, .L3470+12
	cmp	r4, r3
	strh	r0, [r9, #972]	@ movhi
	ldr	r1, .L3470
	bgt	.L3313
	ldr	r3, [r1, #528]
	str	r0, [r1, #1120]
	cbnz	r3, .L3306
	movs	r3, #6
	b	.L3458
.L3306:
	cmp	r3, #5
	bhi	.L3307
	movs	r3, #18
.L3458:
	strh	r3, [r2, #972]	@ movhi
.L3307:
	movs	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r2, r0
	cmp	r2, ip
	beq	.L3312
	ldrh	r0, [r7, #1124]
	ldr	r5, .L3470
	cmp	r0, #0
	beq	.L3309
	ldrh	r4, [r10, #2386]
	ldrh	r3, [r10, #2316]
	ldr	fp, [r5, #292]
	muls	r3, r4, r3
	ldrh	r1, [fp, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r1, r3
	bgt	.L3312
	adds	r1, r0, #1
	str	ip, [sp, #20]
	str	r2, [sp, #16]
	mov	r10, #0
	uxth	r1, r1
	str	r10, [r5, #1128]
	strh	r1, [r5, #1124]	@ movhi
	str	r1, [sp, #12]
	bl	List_get_gc_head_node
	ldr	ip, [sp, #20]
	uxth	r4, r0
	ldr	r1, [sp, #12]
	cmp	r4, ip
	ldr	r2, [sp, #16]
	beq	.L3312
	ldrh	r2, [fp, r2, lsl #1]
	ldrh	r3, [fp, r4, lsl #1]
	ldr	r0, .L3470+16
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, #1124]
	cmp	r3, #40
	bls	.L3310
	ldr	r3, [r5, #292]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r3, #32
	it	hi
	strhhi	r10, [r5, #1124]	@ movhi
.L3310:
	movs	r3, #6
	strh	r3, [r9, #972]	@ movhi
	b	.L3329
.L3309:
	movs	r3, #1
	strh	r3, [r5, #1124]	@ movhi
.L3312:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L3329
	movs	r3, #0
	strh	r3, [r9, #972]	@ movhi
.L3313:
	ldrh	r3, [r7, #552]
	movw	r4, #65535
	ldr	r5, .L3470
	cmp	r3, r4
	bne	.L3463
	ldrh	r4, [r5, #408]
	cmp	r4, r3
	bne	.L3314
	ldrh	r10, [r5, #1170]
	cmp	r10, r4
	bne	.L3315
	ldrh	r3, [r5, #308]
	ldr	r2, [r5, #1120]
	cmp	r3, #24
	ite	cc
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	bls	.L3317
	ldr	r2, .L3470+12
	movs	r3, #0
	str	r3, [r7, #1120]
	mov	r9, r5
	strh	r3, [r2, #972]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r10
	mov	r4, r0
	bne	.L3318
	ldrh	r2, [r5, #308]
	ldrh	r3, [r5, #1118]
	cmp	r2, r3
	bcs	.L3319
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	bne	.L3464
.L3328:
	bl	FtlGcReFreshBadBlk
	cmp	r8, #0
	beq	.L3465
.L3329:
	ldrh	r2, [r7, #408]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3340
.L3395:
	ldrh	r3, [r7, #1170]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3340
	cmp	r4, r3
	bne	.L3340
.L3396:
	ldrh	r1, [r7, #552]
	movw	r3, #65535
	ldr	r2, .L3470
	cmp	r1, r3
	beq	.L3341
.L3345:
	movw	r4, #65535
	b	.L3340
.L3341:
	movs	r3, #0
	ldrh	r1, [r2, #308]
	str	r3, [r2, #1128]
	ldrh	r3, [r2, #1116]
	ldr	r4, .L3470+12
	cmp	r1, r3
	bls	.L3342
	ldrh	r3, [r4, #972]
	cbnz	r3, .L3343
	ldr	r3, .L3470+8
	ldr	r2, [r2, #464]
	ldr	r3, [r3, #2444]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L3408
.L3343:
	ldrh	r3, [r7, #532]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
	b	.L3344
.L3408:
	movs	r3, #18
.L3344:
	strh	r3, [r7, #1116]	@ movhi
	bl	FtlReadRefresh
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r7, #292]
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #4
	bls	.L3342
	ldrh	r0, [r4, #972]
	b	.L3457
.L3342:
	ldrh	r0, [r4, #972]
	cmp	r0, #0
	bne	.L3345
	ldrh	r4, [r7, #532]
	add	r3, r4, r4, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r7, #1116]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r7, #292]
	uxth	r0, r0
	ldrh	r2, [r3, r0, lsl #1]
	ldr	r3, .L3470+8
	ldrh	r1, [r3, #2386]
	ldrh	r3, [r3, #2316]
	muls	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L3346
	ldr	r3, .L3470
	subs	r4, r4, #1
	ldrh	r3, [r3, #308]
	cmp	r3, r4
	blt	.L3346
	bl	FtlReadRefresh
	b	.L3462
.L3346:
	cmp	r2, #0
	bne	.L3345
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r7, #308]
	adds	r0, r0, #1
	b	.L3457
.L3471:
	.align	2
.L3470:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LC152
.L3340:
	ldr	r3, .L3472
	ldr	r3, [r3, #2308]
	cmp	r3, #0
	ite	eq
	moveq	r5, #1
	movne	r5, #2
	b	.L3339
.L3465:
	movw	r3, #65535
	cmp	r4, r3
	bne	.L3329
.L3394:
	ldrh	r4, [r7, #308]
	cmp	r4, #24
	bhi	.L3404
	ldr	r3, .L3472
	cmp	r4, #16
	ldrh	r5, [r3, #2384]
	bls	.L3331
	lsrs	r5, r5, #5
.L3330:
	ldrh	r3, [r7, #1116]
	ldr	r2, .L3472+4
	cmp	r3, r4
	bcc	.L3466
.L3334:
	ldr	r3, .L3472
	ldr	r3, [r3, #2308]
	cmp	r3, #0
	bne	.L3467
.L3407:
	movw	r4, #65535
.L3339:
	ldrh	r2, [r7, #552]
	movw	r1, #65535
	ldr	r3, .L3472+4
	cmp	r2, r1
	bne	.L3348
	cmp	r4, r2
	beq	.L3349
	strh	r4, [r3, #552]	@ movhi
	b	.L3350
.L3349:
	ldrh	r2, [r3, #1170]
	cmp	r2, r4
	beq	.L3350
	ldr	r1, [r3, #292]
	ldrh	r2, [r1, r2, lsl #1]
	cbnz	r2, .L3351
	strh	r4, [r3, #1170]	@ movhi
.L3351:
	ldrh	r3, [r7, #1170]
	strh	r3, [r7, #552]	@ movhi
	movw	r3, #65535
	strh	r3, [r7, #1170]	@ movhi
.L3350:
	ldrh	r0, [r7, #552]
	movw	r6, #65535
	movs	r3, #0
	strb	r3, [r7, #560]
	cmp	r0, r6
	beq	.L3348
	bl	IsBlkInGcList
	cbz	r0, .L3353
	ldr	r3, .L3472+4
	strh	r6, [r3, #552]	@ movhi
.L3353:
	ldr	r3, .L3472
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbz	r3, .L3354
	ldrh	r0, [r7, #552]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #560]
.L3354:
	ldrh	r2, [r7, #552]
	movw	r3, #65535
	ldr	r6, .L3472+4
	cmp	r2, r3
	beq	.L3348
	add	r0, r6, #552
	bl	make_superblock
	ldr	r2, .L3472+8
	movs	r3, #0
	ldrh	r1, [r6, #552]
	strh	r3, [r6, #554]	@ movhi
	strb	r3, [r6, #558]
	strh	r3, [r2, #974]	@ movhi
	ldr	r3, [r6, #292]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #976]	@ movhi
.L3348:
	ldrh	r3, [r7, #552]
	ldrh	r1, [r7, #312]
	ldr	r2, .L3472+4
	cmp	r1, r3
	beq	.L3355
	ldrh	r1, [r2, #360]
	cmp	r1, r3
	beq	.L3355
	ldrh	r2, [r2, #408]
	cmp	r2, r3
	bne	.L3356
.L3355:
	movw	r3, #65535
	strh	r3, [r7, #552]	@ movhi
.L3356:
	ldr	r7, .L3472
.L3389:
	ldr	r6, .L3472+4
	movw	r3, #65535
	ldrh	r2, [r6, #552]
	mov	fp, r6
	cmp	r2, r3
	bne	.L3357
	mov	r10, r6
	movs	r3, #0
	str	r3, [r6, #1128]
.L3358:
	ldrh	r9, [fp, #1124]
	mov	r0, r9
	bl	List_get_gc_head_node
	movw	r2, #65535
	uxth	r3, r0
	strh	r3, [fp, #552]	@ movhi
	cmp	r3, r2
	bne	.L3359
	ldr	r3, .L3472+4
	movs	r2, #0
	movs	r0, #8
	strh	r2, [r3, #1124]	@ movhi
	b	.L3457
.L3359:
	mov	r0, r3
	str	r3, [sp, #12]
	bl	IsBlkInGcList
	add	r9, r9, #1
	ldr	r3, [sp, #12]
	cbz	r0, .L3360
	strh	r9, [r10, #1124]	@ movhi
	b	.L3358
.L3360:
	ldrh	lr, [r7, #2316]
	uxth	r9, r9
	ldrh	r2, [r7, #2384]
	ldr	r1, [r10, #292]
	strh	r9, [r10, #1124]	@ movhi
	mul	r2, lr, r2
	ldrh	r0, [r1, r3, lsl #1]
	add	lr, r2, r2, lsr #31
	cmp	r0, lr, asr #1
	bgt	.L3362
	cmp	r9, #48
	bls	.L3363
	cmp	r0, #8
	bls	.L3363
	ldrh	r0, [r10, #1160]
	cmp	r0, #35
	bhi	.L3363
.L3362:
	movs	r0, #0
	strh	r0, [fp, #1124]	@ movhi
.L3363:
	ldrh	r1, [r1, r3, lsl #1]
	cmp	r1, r2
	blt	.L3364
	movw	r2, #65535
	cmp	r4, r2
	bne	.L3364
	ldrh	r0, [fp, #1124]
	ldr	r2, .L3472+4
	cmp	r0, #3
	bhi	.L3364
	movs	r3, #0
	strh	r4, [r2, #552]	@ movhi
	strh	r3, [r2, #1124]	@ movhi
.L3462:
	ldr	r3, .L3472+8
	ldrh	r0, [r3, #972]
	b	.L3457
.L3364:
	cbnz	r1, .L3365
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [fp, #1124]
	adds	r3, r3, #1
	strh	r3, [fp, #1124]	@ movhi
	b	.L3358
.L3365:
	movs	r2, #0
	strb	r2, [r6, #560]
	ldr	r2, .L3472
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cbz	r2, .L3366
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3472+4
	strb	r0, [r3, #560]
.L3366:
	ldr	r0, .L3472+12
	bl	make_superblock
	ldr	r2, .L3472+8
	ldrh	r0, [r6, #552]
	movs	r3, #0
	ldr	r1, [r6, #292]
	strh	r3, [r2, #974]	@ movhi
	ldrh	r1, [r1, r0, lsl #1]
	strh	r3, [r6, #554]	@ movhi
	strb	r3, [r6, #558]
	strh	r1, [r2, #976]	@ movhi
.L3357:
	cmp	r8, #1
	bne	.L3367
	bl	FtlReadRefresh
.L3367:
	movs	r3, #1
	str	r3, [r6, #3276]
	ldrh	r3, [r7, #2384]
	str	r3, [sp, #12]
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbz	r3, .L3368
	ldr	r3, .L3472+4
	ldrb	r3, [r3, #560]	@ zero_extendqisi2
	cmp	r3, #1
	itt	eq
	ldrheq	r3, [r7, #2386]
	streq	r3, [sp, #12]
.L3368:
	ldrh	r3, [r6, #554]
	ldr	r1, [sp, #12]
	adds	r2, r3, r5
	cmp	r2, r1
	ittt	gt
	movgt	r2, r1
	subgt	r3, r2, r3
	uxthgt	r5, r3
	movs	r3, #0
.L3461:
	str	r3, [sp, #16]
	ldrh	r3, [sp, #16]
	ldr	r6, .L3472+4
	cmp	r3, r5
	bcs	.L3377
	ldr	r3, [r6, #1148]
	addw	r1, r6, #566
	ldr	r2, [sp, #16]
	movw	lr, #65535
	ldrh	r10, [r7, #2316]
	str	r3, [sp, #20]
	ldrh	r3, [r6, #554]
	add	ip, r3, r2
	movs	r3, #0
	mov	r9, r3
.L3378:
	uxth	r2, r3
	cmp	r2, r10
	bcs	.L3468
	ldrh	r2, [r1, #2]!
	cmp	r2, lr
	beq	.L3371
	ldr	r0, [sp, #20]
	mov	fp, #36
	orr	r2, ip, r2, lsl #10
	mla	fp, fp, r9, r0
	add	r9, r9, #1
	uxth	r9, r9
	str	r2, [fp, #4]
.L3371:
	adds	r3, r3, #1
	b	.L3378
.L3473:
	.align	2
.L3472:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LANCHOR2+552
.L3468:
	ldr	r0, [r6, #1148]
	mov	r1, r9
	ldrb	r2, [r6, #560]	@ zero_extendqisi2
	mov	fp, #0
	bl	FlashReadPages
	ldr	r6, .L3474
.L3373:
	uxth	r3, fp
	cmp	r3, r9
	bcs	.L3469
	mov	ip, #36
	ldr	r2, [r6, #1148]
	mul	r10, ip, fp
	add	r3, r2, r10
	ldr	r2, [r2, r10]
	ldr	r3, [r3, #12]
	adds	r2, r2, #1
	beq	.L3411
	ldrh	r1, [r3]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3411
	ldr	r0, [r3, #8]
	add	r1, sp, #32
	movs	r2, #0
	str	ip, [sp, #24]
	str	r3, [sp, #20]
	bl	log2phys
	ldr	r1, [r6, #1148]
	add	r1, r1, r10
	ldr	r0, [r1, #4]
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #20]
	bic	r2, r2, #-2147483648
	ldr	ip, [sp, #24]
	cmp	r2, r0
	bne	.L3411
	str	r3, [sp, #28]
	ldr	r3, .L3474+4
	ldr	r0, [r6, #1132]
	ldr	r1, [r1, #16]
	ldrh	r2, [r3, #974]
	adds	r2, r2, #1
	strh	r2, [r3, #974]	@ movhi
	ldr	r2, [r6, #3304]
	mla	r2, ip, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #20]
	bl	Ftl_get_new_temp_ppa
	ldr	r1, [r6, #1132]
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #24]
	ldr	r3, [sp, #28]
	str	r0, [r2, #4]
	ldr	r2, [r6, #3304]
	mla	ip, ip, r1, r2
	ldr	r2, [r6, #1148]
	add	r2, r2, r10
	ldr	r1, [r2, #8]
	str	r1, [ip, #8]
	movs	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [ip, #12]
	ldr	r2, [sp, #32]
	str	r2, [r3, #12]
	ldrh	r2, [r6, #408]
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r6, #508]
	ldr	r0, [r6, #1148]
	str	r2, [r3, #4]
	add	r0, r0, r10
	ldr	r3, [r6, #1132]
	adds	r3, r3, #1
	str	r3, [r6, #1132]
	bl	FtlGcBufAlloc
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbnz	r3, .L3375
	ldrb	r3, [r6, #415]	@ zero_extendqisi2
	ldr	r2, [r6, #1132]
	cmp	r2, r3
	beq	.L3375
	ldrh	r3, [r6, #412]
	cbnz	r3, .L3411
.L3375:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L3411
	ldr	r3, .L3474
	movs	r2, #0
	movw	r1, #65535
	str	r2, [r3, #3276]
	strh	r1, [r3, #552]	@ movhi
	strh	r2, [r3, #554]	@ movhi
	b	.L3462
.L3411:
	add	fp, fp, #1
	b	.L3373
.L3469:
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	b	.L3461
.L3377:
	ldrh	r3, [r6, #554]
	add	r5, r5, r3
	ldr	r3, [sp, #12]
	uxth	r5, r5
	strh	r5, [r6, #554]	@ movhi
	cmp	r5, r3
	bcc	.L3379
	ldr	r3, [r6, #1132]
	ldr	r5, .L3474+4
	cbz	r3, .L3380
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L3380
	movs	r3, #0
	ldrh	r0, [r5, #972]
	str	r3, [r6, #3276]
	b	.L3457
.L3380:
	ldrh	r5, [r5, #974]
	cbnz	r5, .L3381
	ldrh	r2, [r6, #552]
	ldr	r3, [r6, #292]
	ldrh	r3, [r3, r2, lsl #1]
	cbz	r3, .L3381
.L3382:
	ldr	r3, [r7, #2444]
	cmp	r5, r3
	bcs	.L3387
	mov	r0, r5
	add	r1, sp, #36
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #36]
	adds	r3, r0, #1
	beq	.L3383
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6, #552]
	cmp	r3, r0
	bne	.L3383
.L3387:
	ldr	r3, .L3474+8
	ldr	r3, [r3, #2444]
	cmp	r5, r3
	bcc	.L3381
	ldrh	r2, [r6, #552]
	movs	r1, #0
	ldr	r3, [r6, #292]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r6, #552]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3381
.L3383:
	adds	r5, r5, #1
	b	.L3382
.L3381:
	movw	r3, #65535
	strh	r3, [r6, #552]	@ movhi
.L3379:
	ldrh	r3, [r6, #308]
	cmp	r3, #2
	bhi	.L3388
	ldrh	r5, [r7, #2384]
	b	.L3389
.L3388:
	ldr	r2, .L3474
	movs	r1, #0
	str	r1, [r2, #3276]
	ldr	r2, .L3474+4
	ldrh	r0, [r2, #972]
	cmp	r0, #0
	bne	.L3457
	adds	r0, r3, #1
	b	.L3457
.L3397:
	movs	r0, #0
	b	.L3457
.L3400:
	mov	r0, r4
	b	.L3457
.L3464:
	ldr	r2, [r5, #3272]
	ldr	r5, .L3474+8
	cbnz	r2, .L3321
	ldrh	r2, [r5, #2336]
	cmp	r2, #3
	beq	.L3321
	ldr	r2, [r9, #600]
	cbnz	r2, .L3321
	ldr	r2, [r5, #2308]
	cbnz	r2, .L3321
	ldrb	r0, [r5, #140]	@ zero_extendqisi2
	cbz	r0, .L3322
.L3321:
	ldr	r2, [r7, #292]
	ldrh	r0, [r5, #2336]
	ldrh	r1, [r2, r3, lsl #1]
	cmp	r0, #3
	ldrh	r3, [r5, #2386]
	ldrh	r2, [r5, #2316]
	mul	r2, r2, r3
	ite	eq
	lsreq	r3, r3, #1
	movne	r3, #0
	add	r3, r3, r2
	cmp	r1, r3
	bgt	.L3324
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #2444]
	ldr	r2, .L3474
	ldr	r1, [r7, #464]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	ite	hi
	movhi	r3, #128
	movls	r3, #160
	strh	r3, [r2, #1118]	@ movhi
	uxth	r4, r0
	b	.L3326
.L3324:
	movs	r3, #128
	b	.L3460
.L3322:
	ldr	r2, [r9, #292]
	ldrh	r3, [r2, r3, lsl #1]
	cmp	r3, #7
	bhi	.L3327
	bl	List_get_gc_head_node
	movs	r3, #128
	strh	r3, [r7, #1118]	@ movhi
	uxth	r4, r0
	b	.L3326
.L3327:
	movs	r3, #64
.L3460:
	strh	r3, [r7, #1118]	@ movhi
	b	.L3328
.L3319:
	movs	r3, #80
	strh	r3, [r5, #1118]	@ movhi
	b	.L3328
.L3326:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L3328
.L3318:
	ldr	r1, [r7, #228]
	ldr	r3, [r7, #292]
	ldrh	r2, [r7, #308]
	ldrh	r1, [r1, r4, lsl #1]
	ldrh	r3, [r3, r4, lsl #1]
	ldr	r0, .L3474+12
	str	r1, [sp]
	ldrh	r1, [r7, #1116]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
	b	.L3328
.L3331:
	cmp	r4, #12
	bls	.L3332
	lsrs	r5, r5, #4
	b	.L3330
.L3332:
	cmp	r4, #8
	bls	.L3330
	lsrs	r5, r5, #2
	b	.L3330
.L3404:
	movs	r5, #1
	b	.L3330
.L3466:
	ldrh	r3, [r2, #408]
	movw	r1, #65535
	cmp	r3, r1
	bne	.L3335
	ldrh	r1, [r2, #1170]
	cmp	r1, r3
	bne	.L3335
	ldr	r3, .L3474+4
	ldrh	r0, [r3, #972]
	cbnz	r0, .L3336
	ldr	r3, .L3474+8
	ldr	r2, [r2, #464]
	ldr	r3, [r3, #2444]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L3405
.L3336:
	ldrh	r3, [r7, #532]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
	b	.L3337
.L3475:
	.align	2
.L3474:
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LANCHOR0
	.word	.LC153
.L3405:
	movs	r3, #18
.L3337:
	strh	r3, [r7, #1116]	@ movhi
	movs	r3, #0
	str	r3, [r7, #1128]
	b	.L3457
.L3335:
	ldrh	r3, [r7, #532]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r7, #1116]	@ movhi
	b	.L3334
.L3467:
	cmp	r6, #2
	bhi	.L3407
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L3407
.L3463:
	cmp	r8, #0
	beq	.L3394
	b	.L3329
.L3315:
	cmp	r8, #0
	beq	.L3394
	b	.L3395
.L3314:
	cmp	r8, #0
	beq	.L3394
	b	.L3345
.L3317:
	cmp	r8, #0
	beq	.L3394
	b	.L3396
.L3457:
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
	ldr	r3, .L3519
	ldr	r7, .L3519+4
	ldr	r5, [r3, #980]
	ldr	r3, [r7, #220]
	cmp	r3, #0
	bne	.L3478
	ldr	r4, .L3519+8
	ldr	r1, [r4, #2436]
	cmp	r1, #0
	beq	.L3478
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cbz	r3, .L3501
	ldrb	r8, [r5, #8]	@ zero_extendqisi2
	add	r0, r8, #-1
	rsbs	r8, r0, #0
	adc	r8, r8, r0
	b	.L3480
.L3501:
	mov	r8, r3
.L3480:
	ldr	r0, [r4, #2440]
	mov	r2, r8
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r9, #0
	bl	FlashProgPages
	ldr	r10, .L3519+8
	mov	fp, #36
.L3481:
	ldr	r3, [r4, #2436]
	cmp	r9, r3
	bcs	.L3499
	mul	r6, fp, r9
	ldr	r3, [r10, #2440]
	adds	r2, r3, r6
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	beq	.L3502
	ldr	r3, [r2, #4]
	cmp	r8, #0
	beq	.L3515
	orr	r3, r3, #-2147483648
.L3515:
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	movs	r2, #1
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2440]
	add	r6, r6, r3
	ldr	r3, [r6, #12]
	ldr	r0, [r3, #12]
	adds	r2, r0, #1
	beq	.L3485
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r7, #292]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r6, r0
	cbnz	r2, .L3486
	ldr	r0, .L3519+12
	mov	r1, r6
	bl	printk
.L3486:
	mov	r0, r6
	bl	decrement_vpc_count
.L3485:
	add	r9, r9, #1
	b	.L3481
.L3517:
	movw	r5, #16386
.L3498:
	ldrh	r3, [r7, #1178]
	cbz	r3, .L3499
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3498
.L3499:
	movs	r3, #0
	str	r3, [r4, #2436]
	b	.L3478
.L3502:
	ldr	r10, .L3519+8
	mov	r9, #0
.L3482:
	ldr	r3, [r4, #2436]
	cmp	r9, r3
	bcs	.L3517
	movs	r6, #36
	ldr	r3, [r10, #2440]
	mov	fp, #0
	mov	r2, #-1
	mul	r6, r6, r9
	str	r2, [r3, r6]
.L3488:
	ldr	r3, [r4, #2440]
	adds	r2, r3, r6
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	bne	.L3518
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3489
	ldr	r1, [r7, #292]
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r10, #2384]
	strb	fp, [r5, #6]
	strh	fp, [r5, #4]	@ movhi
	strh	r3, [r5, #2]	@ movhi
.L3489:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L3490
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3490:
	ldr	r3, [r7, #700]
	adds	r3, r3, #1
	str	r3, [r7, #700]
	ldr	r3, [r4, #2440]
	add	r3, r3, r6
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #2440]
	mov	r2, r8
	movs	r1, #1
	add	r3, r3, r6
	str	r0, [sp, #4]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #2440]
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	ittt	eq
	moveq	r3, #1
	ldreq	r2, .L3519+4
	streq	r3, [r2, #220]
	ldr	r3, [r7, #220]
	cmp	r3, #0
	beq	.L3488
	b	.L3478
.L3518:
	ldr	r3, [r2, #4]
	cmp	r8, #0
	beq	.L3516
	orr	r3, r3, #-2147483648
.L3516:
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	movs	r2, #1
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2440]
	add	r6, r6, r3
	ldr	r3, [r6, #12]
	ldr	r0, [r3, #12]
	adds	r3, r0, #1
	beq	.L3495
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r7, #292]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r6, r0
	cbnz	r2, .L3496
	ldr	r0, .L3519+12
	mov	r1, r6
	bl	printk
.L3496:
	mov	r0, r6
	bl	decrement_vpc_count
.L3495:
	add	r9, r9, #1
	b	.L3482
.L3478:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3520:
	.align	2
.L3519:
	.word	.LANCHOR5
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC154
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
	push	{r3, lr}
	.save {r3, lr}
	ldr	r3, .L3526
	ldr	r3, [r3, #220]
	cbnz	r3, .L3522
	ldr	r3, .L3526+4
	ldr	r3, [r3, #504]
	adds	r3, r3, #1
	beq	.L3522
	bl	FtlCacheWriteBack
	bl	l2p_flush
	movs	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3522:
	movs	r0, #0
	pop	{r3, pc}
.L3527:
	.align	2
.L3526:
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
	ldr	r3, .L3530
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3529
	bl	FtlSysFlush
.L3529:
	movs	r0, #0
	pop	{r3, pc}
.L3531:
	.align	2
.L3530:
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
	ldr	r0, .L3534
	bl	printk
	pop	{r3, lr}
	b	ftl_deinit
.L3535:
	.align	2
.L3534:
	.word	.LC155
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
	adds	r2, r0, r1
	ldr	r7, .L3556
	mov	r6, r0
	mov	r5, r1
	ldr	r3, [r7, #2424]
	cmp	r2, r3
	bhi	.L3545
	ldr	r3, .L3556+4
	ldr	r0, [r3, #504]
	adds	r3, r0, #1
	beq	.L3539
	cmp	r1, #31
	bls	.L3548
	ldr	r3, .L3556+8
	ldr	r2, [r3, #220]
	mov	r8, r3
	cmp	r2, #0
	bne	.L3548
	bl	FtlCacheWriteBack
	ldrh	r4, [r7, #2390]
	mov	r0, r6
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r9, r0
	subs	r6, r6, r3
	uxth	r6, r6
	cbz	r6, .L3540
	subs	r4, r4, r6
	add	r9, r0, #1
	cmp	r4, r5
	it	cs
	movcs	r4, r5
	uxth	r4, r4
	subs	r5, r5, r4
.L3540:
	ldr	r4, .L3556+12
	mov	r3, #-1
	str	r3, [sp, #4]
.L3541:
	ldrh	r3, [r7, #2390]
	cmp	r5, r3
	bcc	.L3555
	mov	r0, r9
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L3542
	ldr	r3, [r4, #984]
	add	r1, sp, #4
	movs	r2, #1
	mov	r0, r9
	adds	r3, r3, #1
	str	r3, [r4, #984]
	ldr	r3, [r8, #476]
	adds	r3, r3, #1
	str	r3, [r8, #476]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3542:
	ldrh	r3, [r7, #2390]
	add	r9, r9, #1
	subs	r5, r5, r3
	b	.L3541
.L3555:
	ldr	r3, .L3556+12
	ldr	r2, [r3, #984]
	cmp	r2, #32
	bls	.L3548
	movs	r2, #0
	str	r2, [r3, #984]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3548
.L3545:
	mov	r0, #-1
	b	.L3539
.L3548:
	movs	r0, #0
.L3539:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3557:
	.align	2
.L3556:
	.word	.LANCHOR0
	.word	.LANCHOR1
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
	ldr	r3, .L3602
	.pad #84
	sub	sp, sp, #84
	mov	r7, r1
	str	r2, [sp, #44]
	ldr	r3, [r3, #504]
	adds	r4, r3, #1
	beq	.L3582
	cmp	r0, #16
	bne	.L3561
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r8
	bl	FtlVendorPartRead
	b	.L3560
.L3561:
	ldr	r3, [sp, #44]
	adds	r3, r1, r3
	str	r3, [sp, #36]
	ldr	r3, .L3602+4
	ldr	r1, [sp, #36]
	ldr	r2, [r3, #2424]
	cmp	r1, r2
	bhi	.L3583
	ldrh	r4, [r3, #2390]
	mov	r0, r7
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r1, r4
	ldr	r3, [sp, #36]
	str	r0, [sp, #28]
	subs	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #44]
	rsb	r3, r3, #1
	str	r0, [sp, #32]
	add	r3, r3, r0
	str	r3, [sp, #24]
	ldr	r3, .L3602+8
	ldr	r0, [sp, #28]
	ldr	r2, [r3, #500]
	add	r2, r2, r1
	ldr	r1, [sp, #24]
	str	r2, [r3, #500]
	ldr	r2, [r3, #472]
	add	r2, r2, r1
	ldr	r1, [sp, #32]
	str	r2, [r3, #472]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3562
	bl	FtlCacheWriteBack
.L3562:
	mov	r10, #0
	ldr	r5, [sp, #28]
	ldr	r4, .L3602+8
	ldr	r9, .L3602+4
	mov	r6, r10
	str	r10, [sp, #48]
	str	r10, [sp, #52]
.L3563:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3601
	add	r1, sp, #76
	mov	r0, r5
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp, #76]
	adds	r1, r3, #1
	bne	.L3598
	mov	fp, #0
.L3564:
	ldrh	r0, [r9, #2390]
	cmp	fp, r0
	bcs	.L3568
	mla	r0, r0, r5, fp
	cmp	r0, r7
	bcc	.L3566
	ldr	r3, [sp, #36]
	cmp	r0, r3
	bcs	.L3566
	subs	r0, r0, r7
	movs	r1, #0
	mov	r2, #512
	add	r0, r8, r0, lsl #9
	bl	ftl_memset
.L3566:
	add	fp, fp, #1
	b	.L3564
.L3598:
	ldr	r2, [r4, #3300]
	mov	fp, #36
	mla	fp, fp, r6, r2
	str	r3, [fp, #4]
	ldr	r3, [sp, #28]
	cmp	r5, r3
	bne	.L3569
	ldrh	ip, [r9, #2390]
	mov	r0, r7
	ldr	r3, [r4, #3328]
	mov	r1, ip
	str	ip, [sp, #40]
	str	r3, [fp, #8]
	bl	__aeabi_uidivmod
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	rsb	r3, r1, ip
	str	r1, [sp, #56]
	cmp	r3, r2
	it	cs
	movcs	r3, r2
	cmp	r3, ip
	str	r3, [sp, #48]
	bne	.L3570
	str	r8, [fp, #8]
	b	.L3570
.L3569:
	ldr	r3, [sp, #32]
	cmp	r5, r3
	bne	.L3571
	ldr	r3, [r4, #3332]
	ldrh	r2, [r9, #2390]
	ldr	r1, [sp, #36]
	str	r3, [fp, #8]
	mul	r3, r2, r5
	rsb	r10, r3, r1
	cmp	r10, r2
	bne	.L3570
	b	.L3599
.L3571:
	ldrh	r3, [r9, #2390]
	muls	r3, r5, r3
.L3599:
	subs	r3, r3, r7
	add	r3, r8, r3, lsl #9
	str	r3, [fp, #8]
.L3570:
	ldrh	r3, [r9, #2396]
	ldr	r2, [r4, #3340]
	str	r5, [fp, #16]
	muls	r3, r6, r3
	adds	r6, r6, #1
	bic	r3, r3, #3
	add	r3, r3, r2
	str	r3, [fp, #12]
.L3568:
	ldr	r3, [sp, #24]
	adds	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #24]
	beq	.L3572
	ldrh	r3, [r9, #2316]
	cmp	r6, r3, lsl #3
	bne	.L3563
.L3572:
	cmp	r6, #0
	beq	.L3563
	ldr	r0, [r4, #3300]
	mov	r1, r6
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
	str	r3, [sp, #40]
.L3579:
	ldr	r3, [sp, #40]
	mov	ip, #36
	ldr	r1, [sp, #28]
	mul	fp, ip, r3
	ldr	r3, [r4, #3300]
	add	r3, r3, fp
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L3574
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3328]
	cmp	r1, r3
	bne	.L3575
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3600
.L3574:
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L3575
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3332]
	cmp	r1, r3
	bne	.L3575
	ldrh	r0, [r9, #2390]
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #68]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r8, r0, lsl #9
.L3600:
	bl	ftl_memcpy
.L3575:
	ldr	r2, [r4, #3300]
	add	r3, r2, fp
	ldr	r1, [r2, fp]
	adds	r2, r1, #1
	itttt	eq
	streq	r1, [sp, #52]
	ldreq	r2, [r4, #676]
	addeq	r2, r2, #1
	streq	r2, [r4, #676]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L3577
	ldr	r2, [r4, #676]
	ldr	r0, .L3602+12
	adds	r2, r2, #1
	str	r2, [r4, #676]
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
.L3577:
	ldr	r3, [r4, #3300]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmp	r3, #256
	bne	.L3578
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3578:
	ldr	r3, [sp, #40]
	adds	r3, r3, #1
	str	r3, [sp, #40]
	cmp	r3, r6
	bne	.L3579
	movs	r6, #0
	b	.L3563
.L3601:
	ldr	r3, .L3602+8
	ldrh	r3, [r3, #1178]
	cbz	r3, .L3581
	ldr	r0, [sp, #24]
	movs	r1, #1
	bl	ftl_do_gc
.L3581:
	ldr	r0, [sp, #52]
	b	.L3560
.L3582:
	mov	r0, r3
	b	.L3560
.L3583:
	mov	r0, #-1
.L3560:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3603:
	.align	2
.L3602:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC144
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
	ldr	r3, .L3608
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3607
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
	b	.L3606
.L3607:
	mov	r4, #-1
.L3606:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3609:
	.align	2
.L3608:
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
	ldr	r2, .L3629
	ldr	r7, .L3629+4
	ldr	r4, .L3629+8
	ldr	r6, .L3629+12
	ldr	r1, .L3629+16
	ldr	r0, .L3629+20
	str	r3, [r7, #504]
	movs	r3, #0
	str	r3, [r2, #988]
	str	r3, [r4, #220]
	bl	printk
	add	r0, r6, #112
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r6, #2320]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3614
	ldr	r0, .L3629+24
	b	.L3628
.L3614:
	bl	FtlSysBlkInit
	mov	r5, r0
	cbz	r0, .L3616
	ldr	r0, .L3629+28
.L3628:
	ldr	r1, .L3629+32
	bl	printk
	b	.L3615
.L3616:
	movs	r1, #1
	str	r1, [r7, #504]
	bl	ftl_do_gc
	ldrh	r7, [r4, #308]
	cmp	r7, #15
	bhi	.L3617
	mov	r8, r4
	movw	r6, #65535
.L3620:
	ldrh	r3, [r4, #552]
	cmp	r3, r6
	bne	.L3618
	ldrh	r3, [r8, #1170]
	cmp	r3, r6
	bne	.L3618
	and	r0, r5, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3618:
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	movs	r0, #0
	movs	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r4, #308]
	adds	r3, r7, #2
	cmp	r2, r3
	bhi	.L3615
	adds	r5, r5, #1
	cmp	r5, #4096
	bne	.L3620
	b	.L3615
.L3617:
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbz	r3, .L3615
	movs	r4, #128
.L3622:
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3622
.L3615:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3630:
	.align	2
.L3629:
	.word	.LANCHOR5
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC77
	.word	.LC76
	.word	.LC156
	.word	.LC157
	.word	.LANCHOR3+108
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
	ldr	r5, .L3635
	ldr	r4, .L3635+4
	movs	r6, #0
	add	r1, r5, #996
	str	r6, [r5, #996]
	str	r6, [r4, #1176]
	str	r0, [r5, #992]
	add	r0, r4, #1176
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #1176]
	cbz	r3, .L3634
	bl	rk_nandc_irq_init
	mov	r1, r6
	mov	r2, r6
	mov	r3, #2048
	ldr	r0, [r5, #992]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #1176]
	bl	FlashInit
	mov	r4, r0
	cbnz	r0, .L3633
	bl	FtlInit
.L3633:
	mov	r1, r4
	ldr	r0, .L3635+8
	bl	printk
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3634:
	mov	r0, #-1
	pop	{r4, r5, r6, pc}
.L3636:
	.align	2
.L3635:
	.word	.LANCHOR5
	.word	.LANCHOR4
	.word	.LC158
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
	ldr	r3, .L3652
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #48
	sub	sp, sp, #48
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	mov	r8, r3
	cmp	r2, #0
	beq	.L3637
	ldr	r4, .L3652+4
	movw	r7, #4097
	ldr	r6, .L3652+8
	ldr	r0, .L3652+12
	ldr	r3, [r4, #292]
	ldrh	r5, [r6, #836]
	mov	r1, r5
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldrh	r0, [r4, #312]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4, #360]
	bl	FtlGcRefreshOpenBlock
	add	r0, r4, #312
	bl	allocate_new_data_superblock
	add	r0, r4, #360
	bl	allocate_new_data_superblock
	lsl	r9, r5, #1
.L3639:
	subs	r7, r7, #1
	beq	.L3643
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #292]
	ldrh	r3, [r3, r9]
	cmp	r3, #0
	bne	.L3639
.L3643:
	ldr	r3, [r4, #292]
	mov	r1, r5
	ldr	r0, .L3652+12
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldr	r3, [r4, #292]
	ldrh	r7, [r3, r5, lsl #1]
	cbnz	r7, .L3641
	add	r0, sp, #48
	mov	r10, #36
	movw	r9, #65535
	strh	r5, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldr	r3, .L3652+4
	ldrh	ip, [r8, #2316]
	ldr	r8, [r3, #224]
	mov	r3, r7
	mov	lr, r3
	add	r0, sp, #14
.L3644:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3651
	ldrh	r2, [r0, #2]!
	cmp	r2, r9
	beq	.L3645
	mla	r1, r10, r7, r8
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [r1, #4]
	str	lr, [r1, #8]
	str	lr, [r1, #12]
.L3645:
	adds	r3, r3, #1
	b	.L3644
.L3651:
	ldr	r3, [r4, #292]
	mov	r1, r5
	ldr	r0, .L3652+16
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	movs	r1, #0
	mov	r2, r7
	ldr	r0, [r4, #224]
	bl	FlashEraseBlocks
	ldr	r0, [r4, #224]
	movs	r1, #1
	mov	r2, r7
	bl	FlashEraseBlocks
.L3641:
	movw	r3, #65535
	strh	r3, [r6, #836]	@ movhi
.L3637:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3653:
	.align	2
.L3652:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LC159
	.word	.LC160
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
	ldr	r9, .L3728+16
	mov	r8, r1
	str	r3, [sp, #12]
	str	r2, [sp, #8]
	ldr	r3, [r9, #220]
	cmp	r3, #0
	bne	.L3697
	ldr	r2, .L3728
	ldr	r2, [r2, #504]
	adds	r2, r2, #1
	beq	.L3698
	cmp	r0, #16
	bne	.L3657
	add	r0, r1, #256
	ldr	r2, [sp, #12]
	ldr	r1, [sp, #8]
	bl	FtlVendorPartWrite
	b	.L3656
.L3657:
	ldr	r7, .L3728+4
	ldr	r3, [sp, #8]
	adds	r6, r1, r3
	ldr	r3, [r7, #2424]
	cmp	r6, r3
	bhi	.L3699
	ldrh	r4, [r7, #2390]
	mov	r3, #2048
	ldr	r5, .L3728+8
	mov	r0, r1
	mov	r1, r4
	str	r3, [r5, #1000]
	bl	__aeabi_uidiv
	mov	r1, r4
	mov	fp, r0
	subs	r0, r6, #1
	bl	__aeabi_uidiv
	rsb	r6, fp, r0
	str	r0, [sp, #20]
	adds	r3, r6, #1
	str	r3, [sp, #4]
	ldr	r2, [sp, #4]
	ldr	r3, [r9, #480]
	add	r3, r3, r2
	ldr	r2, [sp, #8]
	str	r3, [r9, #480]
	ldr	r3, [r9, #496]
	cmp	r2, r4, lsl #1
	add	r3, r3, r2
	str	r3, [r9, #496]
	mov	r3, r2
	ldr	r2, [r7, #2436]
	ite	cs
	movcs	r3, #1
	movcc	r3, #0
	str	r3, [sp, #28]
	cmp	r2, #0
	beq	.L3659
	movs	r3, #36
	ldr	r7, [r7, #2440]
	muls	r3, r2, r3
	subs	r3, r3, #36
	add	r7, r7, r3
	ldr	r3, [r7, #16]
	cmp	fp, r3
	bne	.L3660
	ldr	r3, [r9, #484]
	mov	r1, r4
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r9, #484]
	ldr	r3, [r5, #1004]
	adds	r3, r3, #1
	str	r3, [r5, #1004]
	bl	__aeabi_uidivmod
	ldr	r0, [r7, #8]
	ldr	r3, [sp, #8]
	subs	r4, r4, r1
	add	r0, r0, r1, lsl #9
	ldr	r1, [sp, #12]
	cmp	r4, r3
	it	cs
	movcs	r4, r3
	lsl	r9, r4, #9
	mov	r2, r9
	bl	ftl_memcpy
	cbnz	r6, .L3661
	ldr	r3, [r5, #1004]
	cmp	r3, #2
	ble	.L3697
.L3661:
	ldr	r3, [sp, #8]
	add	r8, r8, r4
	add	fp, fp, #1
	str	r6, [sp, #4]
	subs	r3, r3, r4
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	add	r3, r3, r9
	str	r3, [sp, #12]
.L3660:
	movs	r3, #0
	str	r3, [r5, #1004]
.L3659:
	mov	r0, fp
	ldr	r1, [sp, #20]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3662
	bl	FtlCacheWriteBack
.L3662:
	ldr	r4, .L3728+4
	mov	r6, fp
	ldr	r7, .L3728+12
	mov	r10, r4
	str	r7, [r5, #980]
.L3663:
	ldr	r3, [sp, #4]
	ldr	r5, .L3728+16
	cmp	r3, #0
	beq	.L3727
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3664
	add	r2, r5, #312
	ldr	r9, .L3728
	cmp	r7, r2
	bne	.L3665
	ldrh	r7, [r5, #364]
	cbnz	r7, .L3666
	add	r0, r5, #360
	bl	allocate_new_data_superblock
	str	r7, [r9, #3356]
.L3666:
	ldr	r0, .L3728+12
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3356]
	cbnz	r3, .L3700
.L3668:
	ldr	r7, .L3728+12
	b	.L3667
.L3665:
	str	r3, [r9, #3356]
	ldrh	r3, [r5, #316]
	cmp	r3, #0
	bne	.L3668
	mov	r0, r7
	bl	allocate_new_data_superblock
	b	.L3667
.L3700:
	ldr	r7, .L3728+20
.L3667:
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3669
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3669:
	ldr	r3, .L3728+8
	str	r7, [r3, #980]
.L3664:
	ldr	r2, [r5, #3296]
	ldr	r3, [r4, #2436]
	ldrh	ip, [r7, #4]
	subs	r3, r2, r3
	ldr	r2, [sp, #4]
	cmp	ip, r3
	ldr	r9, .L3728+16
	it	cs
	movcs	ip, r3
	cmp	ip, r2
	mov	r3, ip
	it	cs
	movcs	r3, r2
	str	r3, [sp, #44]
	movs	r3, #0
	str	r3, [sp, #24]
.L3670:
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3671
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	beq	.L3671
	ldr	r3, [sp, #28]
	cbz	r3, .L3672
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3672
	ldr	r3, [sp, #24]
	cbz	r3, .L3672
	ldr	r3, [sp, #8]
	ldrh	r2, [r4, #2390]
	add	r3, r8, r3
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3671
.L3672:
	add	r1, sp, #48
	movs	r2, #0
	mov	r0, r6
	movs	r5, #36
	bl	log2phys
	mov	r0, r7
	bl	get_new_active_ppa
	ldr	r3, [r4, #2436]
	ldr	r1, [r4, #2440]
	ldrh	r2, [r4, #2396]
	mla	r1, r5, r3, r1
	mul	ip, r3, r2
	str	r6, [r1, #16]
	str	r0, [r1, #4]
	lsr	r0, ip, #2
	str	r0, [sp, #36]
	ldr	r0, [r9, #3344]
	mov	lr, r0
	str	r0, [sp, #40]
	ldr	r0, [sp, #36]
	add	r0, lr, r0, lsl #2
	ldrh	lr, [r4, #2394]
	str	r0, [sp, #16]
	str	r0, [r1, #12]
	mul	r3, r3, lr
	ldr	r0, [r9, #3324]
	bic	r3, r3, #3
	add	r3, r3, r0
	ldr	r0, [sp, #16]
	str	r3, [r1, #8]
	movs	r1, #0
	bl	ftl_memset
	cmp	r6, fp
	beq	.L3673
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3724
	ldr	r3, [sp, #8]
	add	r5, r8, r3
	ldrh	r3, [r4, #2390]
	smulbb	r3, r3, r6
	subs	r5, r5, r3
	movs	r3, #0
	str	r3, [sp, #32]
	uxth	r5, r5
	b	.L3676
.L3673:
	ldrh	r5, [r4, #2390]
	mov	r0, r8
	mov	r1, r5
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	subs	r5, r5, r1
	str	r1, [sp, #32]
	cmp	r5, r3
	it	cs
	movcs	r5, r3
.L3676:
	ldrh	r3, [r4, #2390]
	cmp	r5, r3
	bne	.L3677
	cmp	r6, fp
	mov	lr, #36
	ittet	ne
	mulne	r1, r6, r5
	ldrne	r3, [sp, #12]
	ldreq	r1, [sp, #12]
	rsbne	r1, r8, r1
	it	ne
	addne	r1, r3, r1, lsl #9
	ldr	r3, [sp, #28]
	cbz	r3, .L3679
	ldr	r2, [r10, #2436]
	ldr	r5, [r10, #2440]
	mla	r2, lr, r2, r5
	str	r1, [r2, #8]
	b	.L3680
.L3679:
	ldr	r0, [r10, #2440]
	ldr	r3, [r10, #2436]
	ldrh	r2, [r10, #2394]
	mla	r3, lr, r3, r0
	ldr	r0, [r3, #8]
	b	.L3725
.L3677:
	ldr	r2, [sp, #48]
	movs	r3, #36
	adds	r1, r2, #1
	beq	.L3681
	ldr	r1, [r4, #2440]
	add	r0, sp, #52
	str	r2, [sp, #56]
	ldr	r2, [r4, #2436]
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
	bne	.L3682
	ldr	r3, [r9, #676]
	adds	r3, r3, #1
	str	r3, [r9, #676]
	b	.L3684
.L3729:
	.align	2
.L3728:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LANCHOR2+312
	.word	.LANCHOR2
	.word	.LANCHOR2+360
.L3682:
	ldr	r3, [sp, #16]
	ldr	r3, [r3, #8]
	cmp	r3, r6
	beq	.L3684
	ldr	r3, [r9, #676]
	mov	r2, r6
	ldr	r0, .L3730
	adds	r3, r3, #1
	str	r3, [r9, #676]
	ldr	r3, [sp, #16]
	ldr	r1, [r3, #8]
	bl	printk
	b	.L3684
.L3681:
	ldr	r1, [r4, #2440]
	ldr	r2, [r4, #2436]
	mla	r2, r3, r2, r1
	movs	r1, #0
	ldr	r0, [r2, #8]
	ldrh	r2, [r4, #2394]
	bl	ftl_memset
.L3684:
	cmp	r6, fp
	mov	lr, #36
	lsl	r2, r5, #9
	bne	.L3685
	ldr	r1, [r4, #2436]
	ldr	r5, [r4, #2440]
	ldr	r3, [sp, #32]
	mla	r1, lr, r1, r5
	ldr	r0, [r1, #8]
	ldr	r1, [sp, #12]
	add	r0, r0, r3, lsl #9
	b	.L3725
.L3685:
	ldr	r0, [r4, #2440]
	ldr	r3, [r4, #2436]
	ldrh	r1, [r4, #2390]
	mla	r3, lr, r3, r0
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #12]
	add	r1, r3, r1, lsl #9
	b	.L3725
.L3724:
	ldr	r3, [sp, #28]
	cbz	r3, .L3686
	ldr	r3, [r4, #2436]
	ldr	r2, [r4, #2440]
	mla	r5, r5, r3, r2
	ldrh	r3, [r4, #2390]
	ldr	r2, [sp, #12]
	muls	r3, r6, r3
	rsb	r3, r8, r3
	add	r3, r2, r3, lsl #9
	str	r3, [r5, #8]
	b	.L3680
.L3686:
	ldr	r2, [r4, #2440]
	ldr	r3, [r4, #2436]
	ldrh	r1, [r4, #2390]
	mla	r3, r5, r3, r2
	ldrh	r2, [r4, #2394]
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #12]
	add	r1, r3, r1, lsl #9
.L3725:
	bl	ftl_memcpy
.L3680:
	ldr	r2, [sp, #40]
	movw	r3, #61589
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1, lsl #2]	@ movhi
	ldr	r2, [sp, #16]
	ldr	r3, [r9, #508]
	str	r3, [r2, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	ldr	r2, [sp, #16]
	it	eq
	moveq	r3, #0
	str	r3, [r9, #508]
	ldr	r3, [sp, #16]
	str	r6, [r3, #8]
	adds	r6, r6, #1
	ldr	r3, [sp, #48]
	str	r3, [r2, #12]
	ldrh	r3, [r7]
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, [r4, #2436]
	adds	r3, r3, #1
	str	r3, [r4, #2436]
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp, #24]
	b	.L3670
.L3671:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #24]
	subs	r3, r3, r2
	str	r3, [sp, #4]
	ldr	r3, .L3730+4
	ldr	r2, [r10, #2436]
	ldr	r3, [r3, #3296]
	cmp	r2, r3
	bcs	.L3690
	ldr	r3, [sp, #28]
	cbnz	r3, .L3690
	ldrh	r3, [r7, #4]
	cbz	r3, .L3690
.L3692:
	movs	r3, #0
	str	r3, [sp, #28]
	b	.L3663
.L3690:
	bl	FtlCacheWriteBack
	movs	r3, #0
	str	r3, [r10, #2436]
	ldr	r3, [sp, #4]
	cmp	r3, #1
	bhi	.L3663
	b	.L3692
.L3727:
	mov	r0, r3
	ldr	r3, [sp, #20]
	rsb	r1, fp, r3
	bl	ftl_do_gc
	ldrh	r3, [r5, #308]
	cmp	r3, #5
	bls	.L3703
	cmp	r3, #31
	bhi	.L3697
	ldr	r3, .L3730+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cbnz	r3, .L3697
.L3703:
	ldr	r4, [sp, #4]
	mov	r8, #128
	ldr	r6, .L3730+4
	movw	r7, #65535
.L3716:
	ldrh	r3, [r5, #552]
	cmp	r3, r7
	bne	.L3696
	ldrh	r3, [r6, #1170]
	cmp	r3, r7
	bne	.L3696
	ldrh	r3, [r6, #1172]
	cmp	r3, r7
	bne	.L3696
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3696:
	movs	r0, #1
	strh	r8, [r5, #1118]	@ movhi
	strh	r8, [r5, #1116]	@ movhi
	mov	r1, r0
	bl	ftl_do_gc
	movs	r0, #0
	movs	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r5, #220]
	cbnz	r3, .L3697
	ldrh	r3, [r6, #308]
	cmp	r3, #2
	bhi	.L3697
	adds	r4, r4, #1
	cmp	r4, #256
	bne	.L3716
	b	.L3697
.L3699:
	mov	r0, #-1
	b	.L3656
.L3697:
	movs	r0, #0
	b	.L3656
.L3698:
	mov	r0, r3
.L3656:
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3731:
	.align	2
.L3730:
	.word	.LC161
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
	ldr	r3, .L3736
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3735
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
	b	.L3734
.L3735:
	mov	r4, #-1
.L3734:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3737:
	.align	2
.L3736:
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
	ldr	r5, .L3748
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L3748+4
	mov	r9, #0
	ldr	r7, .L3748+8
	mov	r6, r0
	ldr	r3, [r5, #3312]
	str	r3, [r4, #1252]
	ldr	r3, [r5, #3336]
	str	r3, [r4, #1256]
.L3741:
	ldrh	r2, [r7, #2386]
	sxth	r3, r9
	cmp	r3, r2
	bge	.L3747
	movs	r1, #1
	ldr	r0, .L3748+12
	orr	r3, r3, r8
	str	r3, [r4, #1248]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #1256]
	ldr	r0, .L3748+16
	mov	r1, r6
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #1244]
	str	r3, [sp, #12]
	ldr	r3, [r4, #1252]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #1248]
	bl	printk
	ldr	r3, [r4, #1256]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L3742
	ldr	r0, .L3748+20
	movs	r2, #4
	ldr	r1, [r5, #3312]
	mov	r3, #768
	bl	rknand_print_hex
.L3742:
	add	r9, r9, #1
	b	.L3741
.L3747:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3749:
	.align	2
.L3748:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR4+1244
	.word	.LC162
	.word	.LC163
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
	ldr	r4, .L3768
	addw	fp, r4, #2344
	ldrh	r7, [r4, #2324]
	mov	r9, r4
.L3751:
	ldrh	r3, [r4, #2326]
	ldr	r6, .L3768+4
	cmp	r3, r7
	bls	.L3765
	ldr	r1, [r6, #1144]
	mov	r8, #0
	ldrh	ip, [r9, #2316]
	mov	r10, #36
	ldr	r2, [r6, #3300]
	mov	r5, r8
	str	r1, [sp, #24]
	ldr	r3, [r6, #1140]
	ldrh	r1, [r9, #2396]
	str	r1, [sp, #28]
.L3761:
	uxth	r1, r8
	cmp	r1, ip
	bcs	.L3766
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
	cbnz	r0, .L3752
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
.L3752:
	add	r8, r8, #1
	b	.L3761
.L3766:
	cbz	r5, .L3756
	ldr	r0, [r6, #3300]
	mov	r1, r5
	movs	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r10, #36
.L3757:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L3756
	ldr	r3, [r6, #3300]
	ldr	r0, .L3768+8
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
	b	.L3757
.L3756:
	adds	r7, r7, #1
	uxth	r7, r7
	b	.L3751
.L3765:
	ldr	r7, .L3768+12
	mov	r8, #0
.L3760:
	ldrh	r2, [r6, #3448]
	sxth	r3, r8
	ldr	r5, .L3768+4
	cmp	r3, r2
	bge	.L3763
	lsls	r5, r3, #1
	mov	r9, #0
.L3764:
	ldrh	r2, [r4, #2386]
	sxth	r3, r9
	add	r9, r9, #1
	cmp	r3, r2
	bge	.L3767
	ldr	r2, [r6, #3364]
	movs	r1, #1
	ldr	r0, .L3768+16
	ldrh	r2, [r2, r5]
	orr	r3, r3, r2, lsl #10
	mov	r2, r1
	str	r3, [r7, #1248]
	bl	FlashReadPages
	ldr	r3, [r7, #1256]
	ldr	r1, [r6, #3364]
	ldr	r2, [r7, #1252]
	ldr	r0, [r3]
	ldrh	r1, [r1, r5]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L3768+20
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r7, #1244]
	ldr	r3, [r7, #1248]
	bl	printk
	b	.L3764
.L3767:
	add	r8, r8, #1
	b	.L3760
.L3763:
	ldr	r1, [r5, #3364]
	movs	r2, #2
	ldr	r3, [r4, #2412]
	ldr	r0, .L3768+24
	bl	rknand_print_hex
	ldr	r1, [r5, #3384]
	ldrh	r3, [r4, #2420]
	movs	r2, #4
	ldr	r0, .L3768+28
	bl	rknand_print_hex
	ldr	r0, .L3768+32
	ldr	r1, [r5, #3388]
	movs	r2, #4
	ldrh	r3, [r4, #2420]
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3769:
	.align	2
.L3768:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC164
	.word	.LANCHOR4
	.word	.LANCHOR4+1244
	.word	.LC111
	.word	.LC165
	.word	.LC166
	.word	.LC167
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
	ldr	r3, .L3772
	ldr	r2, [r3, #2256]
	ldr	r3, .L3772+4
	cmp	r2, r3
	bne	.L3770
	b	flash_enter_slc_mode
.L3770:
	bx	lr
.L3773:
	.align	2
.L3772:
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
	ldr	r3, .L3776
	ldr	r2, [r3, #2256]
	ldr	r3, .L3776+4
	cmp	r2, r3
	bne	.L3774
	b	flash_exit_slc_mode
.L3774:
	bx	lr
.L3777:
	.align	2
.L3776:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	1
	.thumb
	.thumb_func
	.type	IdBlockReadData, %function
IdBlockReadData:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r4, .L3786
	mov	r8, r1
	.pad #20
	sub	sp, sp, #20
	mov	r1, r6
	mov	fp, r2
	ldr	r0, .L3786+4
	ldr	r3, [r4, #1204]
	mov	r2, r8
	movs	r7, #0
	ldrb	r9, [r3, #9]	@ zero_extendqisi2
	ldr	r3, [r4, #4]
	smulbb	r5, r3, r9
	bl	printk
	mov	r0, r6
	uxth	r5, r5
	mov	r1, r5
	bl	__aeabi_uidivmod
	mul	ip, r9, r1
	mov	r10, r1
	subs	r3, r6, r1
	str	r3, [sp]
	ubfx	ip, ip, #2, #2
.L3779:
	cmp	r7, r8
	bcs	.L3785
	add	r2, r7, r10
	ldrb	r1, [r4, #140]	@ zero_extendqisi2
	ubfx	r2, r2, #2, #16
	rsb	r5, ip, #4
	add	r3, r4, r2, lsl #1
	uxth	r5, r5
	ldrh	r3, [r3, #144]
	cbz	r1, .L3780
	ldr	r1, .L3786
	ldr	r0, .L3786+8
	ldr	r1, [r1, #2256]
	cmp	r1, r0
	it	eq
	moveq	r3, r2
.L3780:
	ldr	r2, [sp]
	add	r7, r7, r5
	add	r2, ip, r2
	ldrb	ip, [r4, #2304]	@ zero_extendqisi2
	uxth	r7, r7
	mla	r3, r9, r3, r2
	ldr	r2, [r4, #1204]
	str	ip, [sp, #4]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L3786+12
	str	r1, [sp, #8]
	ldrb	r0, [r3, #1206]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [sp, #12]
	ldr	r1, [sp, #8]
	mov	r0, r3
	bl	__aeabi_uidiv
	mov	r2, fp
	add	fp, fp, r5, lsl #9
	mov	r1, r0
	movs	r0, #0
	mov	r3, r0
	bl	FlashReadPage
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	ip, [sp, #4]
	mov	r0, ip
	bl	FlashBchSel
	mov	ip, #0
	b	.L3779
.L3785:
	mov	r1, r6
	mov	r2, r8
	movs	r3, #0
	ldr	r0, .L3786+16
	bl	printk
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3787:
	.align	2
.L3786:
	.word	.LANCHOR0
	.word	.LC168
	.word	1446522928
	.word	.LANCHOR4
	.word	.LC169
	.fnend
	.size	IdBlockReadData, .-IdBlockReadData
	.align	1
	.global	write_idblock
	.thumb
	.thumb_func
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 112
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r7, .L3830
	.pad #124
	sub	sp, sp, #124
	mov	r0, #256000
	mov	r6, r1
	mov	r5, r2
	ldr	r3, [r7, #1204]
	ldr	r8, [r7, #4]
	ldrb	r10, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	str	r0, [sp, #8]
	cmp	r0, #0
	beq	.L3812
	addw	r4, r4, #511
	lsrs	r4, r4, #9
	cmp	r4, #8
	bls	.L3810
	cmp	r4, #500
	bhi	.L3812
	b	.L3790
.L3810:
	movs	r4, #8
.L3790:
	ldr	r2, [r6]
	ldr	r3, .L3830+4
	cmp	r2, r3
	bne	.L3812
	smulbb	r3, r10, r8
	uxth	r9, r3
	add	r0, r9, #-1
	add	r0, r0, r4
	mov	r1, r9
	bl	__aeabi_uidiv
	movs	r3, #0
	movw	r2, #63871
	str	r0, [sp, #28]
	add	r0, r6, #254976
	add	r0, r0, #512
.L3794:
	ldr	r1, [r0, #-4]!
	cbnz	r1, .L3791
	ldr	r1, [r6, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #4096
	add	r2, r2, #-1
	it	hi
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3794
	b	.L3793
.L3791:
	ldr	r0, .L3830+8
	bl	printk
.L3793:
	mov	r1, r5
	movs	r3, #5
	ldr	r0, .L3830+12
	movs	r2, #4
	bl	rknand_print_hex
	ldrb	r2, [r7, #1]	@ zero_extendqisi2
	ldr	r1, [r6, #512]
	subs	r5, r5, #4
	ldr	r0, .L3830+16
	bl	printk
	ldr	r2, .L3830+20
	ldrh	r3, [r7, #138]
	mov	r1, r4
	ldr	r0, .L3830+24
	ldr	r2, [r2, #1200]
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r7, .L3830
	ldr	r2, [r6, #512]
	cmp	r2, r3
	it	hi
	strhi	r3, [r6, #512]
	lsls	r3, r4, #7
	str	r3, [sp, #32]
	movs	r3, #0
	str	r3, [sp, #16]
	str	r3, [sp, #12]
.L3808:
	ldr	r2, [r5, #4]
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r10, .L3830
	cmp	r2, r3
	bcs	.L3796
	ldr	r3, .L3830+20
	ldr	r3, [r3, #1200]
	cmp	r2, r3
	bcc	.L3796
	ldr	r3, [sp, #28]
	cmp	r3, #1
	bls	.L3797
	ldr	r3, [sp, #12]
	cbz	r3, .L3797
	ldr	r3, [r5]
	adds	r3, r3, #1
	cmp	r2, r3
	beq	.L3796
.L3797:
	movs	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r3, [r5, #4]
	ldr	r0, .L3830+28
	mul	r8, r3, r9
	ldr	r3, [r10, #1204]
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r1, r8
	str	r3, [sp, #20]
	ldr	r3, [r10, #4]
	ldrh	r2, [sp, #20]
	smulbb	r10, r3, r2
	mov	r2, r4
	bl	printk
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	mov	r0, r8
	uxth	r10, r10
	ldr	r1, [sp, #20]
	bl	__aeabi_uidiv
	mov	r1, r0
	movs	r0, #0
	mov	r2, r0
	bl	FlashEraseBlock
	cmp	r4, r10
	bls	.L3813
	movs	r0, #0
	add	r1, r8, r10
	mov	fp, #2
	mov	r2, r0
	bl	FlashEraseBlock
	b	.L3798
.L3813:
	mov	fp, #1
.L3798:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r7, #1204]
	ldrh	r0, [r3, #10]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	lsls	r0, r0, #2
	mul	r0, fp, r0
	bl	__aeabi_idiv
	mov	r1, r10
	mov	r10, #0
	str	r0, [sp, #44]
	mov	r0, r8
	bl	__aeabi_uidivmod
	mov	ip, r1
	rsb	r3, r1, r8
	str	r6, [sp, #24]
	str	r3, [sp, #36]
.L3799:
	ldr	r3, [sp, #44]
	cmp	r10, r3
	bcs	.L3829
	add	r2, r10, ip
	ubfx	r2, r2, #2, #16
	cbz	r2, .L3800
	adds	r3, r2, #1
	add	r1, r7, r3, lsl #1
	ldrh	fp, [r1, #144]
	ldrb	r1, [r7, #140]	@ zero_extendqisi2
	cbz	r1, .L3801
	ldr	r1, [r7, #2256]
	ldr	r0, .L3830+32
	cmp	r1, r0
	it	eq
	moveq	fp, r3
.L3801:
	add	fp, fp, #1073741824
	add	fp, fp, #-1
	lsl	r3, fp, #2
	str	r3, [sp, #56]
.L3800:
	movw	r3, #61424
	str	r3, [sp, #60]
	add	r3, r7, r2, lsl #1
	ldrh	fp, [r3, #144]
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbz	r3, .L3802
	ldr	r3, [r7, #2256]
	ldr	r1, .L3830+32
	cmp	r3, r1
	it	eq
	moveq	fp, r2
.L3802:
	ldr	r3, [sp, #20]
	add	r10, r10, #4
	ldr	r2, [sp, #36]
	str	ip, [sp, #52]
	uxth	r10, r10
	mla	r2, r3, fp, r2
	ldr	r3, .L3830
	ldrb	r3, [r3, #2304]	@ zero_extendqisi2
	str	r2, [sp, #48]
	str	r3, [sp, #40]
	ldr	r3, .L3830+20
	ldrb	r0, [r3, #1206]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, .L3830
	ldr	r3, [r3, #1204]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #48]
	mov	r0, r2
	bl	__aeabi_uidiv
	add	r3, sp, #56
	mov	r1, r0
	ldr	r2, [sp, #24]
	movs	r0, #0
	bl	FlashProgPage
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #40]
	bl	FlashBchSel
	ldr	r1, [sp, #20]
	ldr	r0, [sp, #36]
	bl	__aeabi_uidiv
	add	r2, fp, #1
	uxth	r2, r2
	mov	r1, r0
	movs	r0, #0
	bl	FlashPageProgMsbFFData
	ldr	r3, [sp, #24]
	ldr	ip, [sp, #52]
	add	r3, r3, #2048
	str	r3, [sp, #24]
	b	.L3799
.L3831:
	.align	2
.L3830:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC170
	.word	.LC171
	.word	.LC172
	.word	.LANCHOR4
	.word	.LC173
	.word	.LC174
	.word	1446522928
.L3829:
	mov	r1, r8
	movs	r3, #0
	mov	r2, r4
	ldr	r0, .L3832
	bl	printk
	ldr	r0, [r5, #4]
	mov	r1, r4
	mov	r8, #0
	mul	r0, r0, r9
	ldr	r2, [sp, #8]
	bl	IdBlockReadData
	mov	r3, r6
	ldr	r0, [sp, #8]
.L3807:
	mov	fp, r0
	mov	r10, r3
	ldr	r1, [fp]
	adds	r0, r0, #4
	ldr	r2, [r10]
	adds	r3, r3, #4
	cmp	r1, r2
	beq	.L3804
	movs	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r3, [fp]
	ldr	r0, .L3832+4
	str	r3, [sp]
	ldr	r3, [r10]
	bic	r10, r8, #255
	ldr	r1, [sp, #12]
	lsl	r10, r10, #2
	str	r3, [sp, #4]
	mov	r3, r8
	ldr	r2, [r5, #4]
	bl	printk
	ldr	r0, .L3832+8
	add	r1, r6, r10
	movs	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	movs	r2, #4
	ldr	r0, .L3832+12
	ldr	r3, [sp, #8]
	add	r1, r3, r10
	mov	r3, #256
	bl	rknand_print_hex
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r1, [r5, #4]
	movs	r0, #0
	mul	r1, r1, r9
	mov	r2, r0
	bl	FlashEraseBlock
	ldr	r3, [sp, #28]
	cmp	r3, #1
	bls	.L3805
	ldr	r1, [r5, #4]
	movs	r0, #0
	mov	r2, r0
	mla	r1, r1, r9, r9
	bl	FlashEraseBlock
.L3805:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L3832+16
	ldr	r1, [r5, #4]
	bl	printk
	ldr	r3, [sp, #32]
	cmp	r8, r3
	bcc	.L3796
	b	.L3806
.L3804:
	ldr	r2, [sp, #32]
	add	r8, r8, #1
	cmp	r8, r2
	bne	.L3807
.L3806:
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	str	r3, [sp, #16]
.L3796:
	ldr	r3, [sp, #12]
	adds	r5, r5, #4
	adds	r3, r3, #1
	str	r3, [sp, #12]
	cmp	r3, #5
	bne	.L3808
	ldr	r0, [sp, #8]
	bl	ftl_free
	ldr	r3, [sp, #16]
	clz	r0, r3
	lsrs	r0, r0, #5
	negs	r0, r0
	b	.L3789
.L3812:
	mov	r0, #-1
.L3789:
	add	sp, sp, #124
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3833:
	.align	2
.L3832:
	.word	.LC175
	.word	.LC176
	.word	.LC177
	.word	.LC178
	.word	.LC179
	.fnend
	.size	write_idblock, .-write_idblock
	.align	1
	.global	CRC_32
	.thumb
	.thumb_func
	.type	CRC_32, %function
CRC_32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	add	r1, r1, r0
	ldr	r4, .L3838
	movs	r2, #0
.L3835:
	cmp	r0, r1
	beq	.L3837
	ldrb	r3, [r0], #1	@ zero_extendqisi2
	eor	r3, r3, r2, lsr #24
	add	r3, r4, r3, lsl #2
	ldr	r3, [r3, #3360]
	eor	r2, r3, r2, lsl #8
	b	.L3835
.L3837:
	mov	r0, r2
	pop	{r4, pc}
.L3839:
	.align	2
.L3838:
	.word	.LANCHOR1
	.fnend
	.size	CRC_32, .-CRC_32
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
	ldr	r4, .L3862
	bne	.L3841
	ldr	r2, [r2]
	ldr	r3, .L3862+4
	cmp	r2, r3
	bne	.L3841
	mov	r0, #256000
	movs	r3, #1
	strb	r3, [r4, #1008]
	bl	ftl_malloc
	movs	r1, #0
	mov	r2, #256000
	str	r0, [r4, #1012]
	bl	ftl_memset
	str	r5, [r4, #1016]
.L3841:
	str	r6, [sp]
	mov	r3, r5
	ldr	r0, .L3862+8
	ldr	r1, [r4, #1012]
	ldr	r2, [r8]
	bl	printk
	ldrb	r3, [r4, #1008]	@ zero_extendqisi2
	ldr	r9, .L3862
	cmp	r3, #0
	beq	.L3840
	sub	r0, r5, #64
	ldr	r7, [r9, #1012]
	cmp	r0, #500
	bcs	.L3843
	rsb	r2, r5, #564
	add	r0, r7, r0, lsl #9
	cmp	r2, r6
	mov	r1, r8
	it	cs
	movcs	r2, r6
	lsls	r2, r2, #9
	bl	ftl_memcpy
	b	.L3844
.L3843:
	cmp	r5, #564
	bcs	.L3852
.L3844:
	ldr	r3, [r4, #1016]
	cmp	r3, r5
	beq	.L3850
	ldr	r2, .L3862
	movs	r3, #0
	mov	r8, r3
	strb	r3, [r2, #1008]
	cbz	r7, .L3851
	mov	r0, r7
	bl	ftl_free
.L3851:
	str	r8, [r4, #1012]
.L3850:
	add	r5, r5, r6
	str	r5, [r4, #1016]
	b	.L3840
.L3852:
	ldr	r3, .L3862+12
	ldr	r0, [r9, #1016]
	ldr	r3, [r3, #1204]
	subs	r0, r0, #64
	cmp	r0, #500
	it	cs
	movcs	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L3853
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
	b	.L3846
.L3853:
	movs	r3, #0
.L3845:
	add	r1, sp, #8
	lsls	r2, r3, #1
	cmp	r0, #256
	it	ls
	movls	r2, r3
	str	r2, [r1, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #5
	bne	.L3845
.L3846:
	movw	r3, #63872
.L3849:
	ldr	r2, [r7, r3, lsl #2]
	cbz	r2, .L3847
	adds	r3, r3, #128
	lsls	r0, r3, #2
	b	.L3848
.L3847:
	subs	r3, r3, #1
	cmp	r3, #4096
	bne	.L3849
	lsls	r0, r0, #9
.L3848:
	mov	r1, r7
	add	r2, sp, #8
	bl	write_idblock
	ldr	r0, [r4, #1012]
	movs	r7, #0
	strb	r7, [r4, #1008]
	bl	ftl_free
	str	r7, [r4, #1012]
	b	.L3850
.L3840:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3863:
	.align	2
.L3862:
	.word	.LANCHOR5
	.word	-52655045
	.word	.LC180
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
	bcs	.L3865
	cbnz	r0, .L3865
	mov	r0, r1
	mov	r1, r2
	mov	r2, r5
	bl	write_loader_lba
.L3865:
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
	@ args = 0, pretend = 0, frame = 528
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3974
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	cmp	r1, r3
	.pad #532
	sub	sp, sp, #532
	mov	r4, r1
	mov	r5, r2
	beq	.L3868
	bhi	.L3869
	subw	r3, r3, #3461
	cmp	r1, r3
	beq	.L3870
	bhi	.L3871
	subs	r3, r3, #125
	cmp	r1, r3
	beq	.L3872
	bhi	.L3873
	subs	r3, r3, #237
	cmp	r1, r3
	bne	.L3929
	bl	rknand_dev_flush
	b	.L3962
.L3873:
	ldr	r3, .L3974+4
	cmp	r1, r3
	beq	.L3875
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3876
	b	.L3929
.L3871:
	ldr	r3, .L3974+8
	cmp	r1, r3
	beq	.L3877
	bhi	.L3878
	mvn	r1, #1960
	add	r3, r3, r1
	cmp	r4, r3
	beq	.L3879
	b	.L3929
.L3878:
	ldr	r3, .L3974+12
	cmp	r1, r3
	beq	.L3877
	adds	r3, r3, #10
	cmp	r1, r3
	beq	.L3877
	b	.L3929
.L3869:
	ldr	r3, .L3974+16
	cmp	r1, r3
	beq	.L3880
	bhi	.L3881
	subs	r3, r3, #78
	cmp	r1, r3
	beq	.L3882
	bcc	.L3883
	adds	r3, r3, #21
	cmp	r1, r3
	beq	.L3884
	adds	r3, r3, #56
	cmp	r1, r3
	beq	.L3885
	b	.L3929
.L3881:
	ldr	r3, .L3974+20
	cmp	r1, r3
	mov	r6, r3
	beq	.L3886
	bhi	.L3887
	mvn	r2, #956
	add	r3, r3, r2
	cmp	r1, r3
	beq	.L3888
	add	r3, r3, #956
	cmp	r1, r3
	beq	.L3889
	b	.L3929
.L3887:
	ldr	r3, .L3974+24
	cmp	r1, r3
	beq	.L3889
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3886
	b	.L3929
.L3883:
	ldr	r0, .L3974+28
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r4, r0
	cbnz	r0, .L3890
.L3895:
	mvn	r0, #11
	b	.L3867
.L3890:
	mov	r1, r5
	mov	r2, #512
	bl	rk_copy_from_user
	cbnz	r0, .L3968
	ldr	r0, .L3974+32
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r4, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L3966
	bl	rknand_device_lock
	mov	r2, r4
	ldr	r0, [r4]
	ldr	r1, [sp, #4]
	bl	IdBlockReadData
	bl	rknand_device_unlock
	mov	r0, r5
	mov	r1, r4
	ldr	r2, [sp, #4]
	lsls	r2, r2, #9
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3961
	ldr	r0, .L3974+36
.L3964:
	bl	printk
.L3966:
	mov	r0, r4
.L3967:
	bl	ftl_free
.L3965:
	mvn	r0, #13
	b	.L3867
.L3882:
	ldr	r0, .L3974+40
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r4, r0
	cmp	r0, #0
	beq	.L3895
	mov	r1, r5
	mov	r2, #4096
	bl	rk_copy_from_user
	cbz	r0, .L3896
.L3968:
	ldr	r0, .L3974+44
	b	.L3964
.L3896:
	ldr	r5, .L3974+48
	ldr	r0, .L3974+52
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r6, [r5, #1020]
	mov	r7, r5
	cbnz	r6, .L3897
	mov	r0, #260096
	bl	ftl_malloc
	str	r0, [r5, #1020]
	cmp	r0, #0
	beq	.L3966
	mov	r1, r6
	mov	r2, #260096
	bl	memset
.L3897:
	ldr	r2, [r4, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L3966
	ldr	r3, [r4]
	cmp	r3, #251904
	bhi	.L3966
	ldr	r0, [r7, #1020]
	add	r1, r4, #8
	add	r0, r0, r3
	bl	memcpy
.L3961:
	mov	r0, r4
	bl	ftl_free
.L3962:
	movs	r4, #0
	b	.L3894
.L3885:
	ldr	r0, .L3974+56
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3895
	mov	r1, r5
	movs	r2, #28
	bl	rk_copy_from_user
	cbz	r0, .L3900
	ldr	r0, .L3974+44
	bl	printk
	b	.L3901
.L3900:
	ldmia	r6, {r1, r2}
	ldr	r0, .L3974+60
	bl	printk
	ldr	r1, [r6]
	cmp	r1, #256000
	bhi	.L3901
	ldr	r5, .L3974+48
	ldr	r0, [r5, #1020]
	cbz	r0, .L3901
	bl	CRC_32
	ldr	r3, [r6, #4]
	cmp	r3, r0
	beq	.L3903
	mov	r0, r6
	bl	ftl_free
	b	.L3930
.L3903:
	bl	rknand_device_lock
	ldr	r1, [r5, #1020]
	add	r2, r6, #8
	ldr	r0, [r6]
	bl	write_idblock
	movs	r4, #0
	bl	rknand_device_unlock
	ldr	r0, [r5, #1020]
	bl	ftl_free
	str	r4, [r5, #1020]
	mov	r0, r6
	b	.L3907
.L3901:
	mov	r0, r6
	b	.L3967
.L3884:
	ldr	r0, .L3974+64
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3895
	bl	ftl_read_flash_info
	mov	r0, r5
	mov	r1, r6
	movs	r2, #11
	b	.L3972
.L3868:
	ldr	r0, .L3974+68
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3895
	movs	r1, #0
	movs	r2, #64
	bl	ftl_memset
	mov	r0, r5
	mov	r1, r6
	movs	r2, #64
	b	.L3972
.L3880:
	ldr	r0, .L3974+72
	bl	printk
	ldr	r3, .L3974+48
	movs	r2, #4
	mov	r0, r5
	ldr	r3, [r3, #1024]
	ldr	r3, [r3, #20]
	add	r1, sp, #4
	str	r3, [sp, #4]
.L3956:
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3965
	b	.L3962
.L3888:
	ldr	r0, .L3974+76
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3895
	bl	rknand_device_lock
	mov	r1, #264
	movs	r2, #2
	mov	r3, r6
	movs	r0, #16
	bl	ftl_read
	bl	rknand_device_unlock
	mov	r2, #1024
	mov	r0, r5
	mov	r1, r6
.L3972:
	bl	rk_copy_to_user
	mov	r4, r0
	mov	r0, r6
	cmp	r4, #0
	bne	.L3967
.L3907:
	bl	ftl_free
	b	.L3894
.L3872:
	ldr	r0, .L3974+80
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	mov	r4, r0
	cbz	r0, .L3908
.L3913:
	ldr	r0, .L3974+44
	bl	printk
	b	.L3965
.L3908:
	ldr	r2, [sp, #8]
	ldr	r3, .L3974+84
	cmp	r2, r3
	beq	.L3909
.L3910:
	mov	r4, #-1
	b	.L3894
.L3975:
	.align	2
.L3974:
	.word	1074033155
	.word	1074029570
	.word	1074031656
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC181
	.word	.LC183
	.word	.LC184
	.word	.LC185
	.word	.LC182
	.word	.LANCHOR5
	.word	.LC186
	.word	.LC187
	.word	.LC188
	.word	.LC189
	.word	.LC190
	.word	.LC191
	.word	.LC192
	.word	.LC193
	.word	1263358532
.L3909:
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3910
	ldr	r6, .L3976
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #1024]
	bl	memcpy
	ldr	r2, [r6, #1028]
	ldr	r3, .L3976+4
	cmp	r2, r3
	beq	.L3911
	add	r0, sp, #72
	mov	r1, r4
	movs	r2, #128
	str	r4, [sp, #16]
	str	r4, [sp, #20]
	bl	memset
.L3911:
	movs	r1, #0
	mov	r2, #256
	add	r0, sp, #264
	str	r1, [sp, #24]
	bl	memset
	b	.L3970
.L3875:
	ldr	r0, .L3976+8
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3913
	ldr	r2, [sp, #8]
	ldr	r3, .L3976+12
	cmp	r2, r3
	bne	.L3910
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3910
	ldr	r2, .L3976
	ldr	r3, .L3976+4
	ldr	r1, [r2, #1028]
	cmp	r1, r3
	bne	.L3930
	ldr	r3, [sp, #20]
	subs	r1, r3, #1
	cmp	r1, #127
	bhi	.L3931
	ldr	r4, [r2, #1024]
	add	r1, sp, #72
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #20]
	bl	memcpy
	movs	r0, #1
	mov	r1, r4
	b	.L3963
.L3879:
	ldr	r0, .L3976+16
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3913
	ldr	r2, [sp, #8]
	ldr	r3, .L3976+20
	cmp	r2, r3
	bne	.L3910
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3910
	ldr	r5, .L3976
	ldr	r3, [r5, #1032]
	cbnz	r3, .L3914
.L3917:
	movs	r0, #0
	b	.L3867
.L3914:
	ldr	r3, [r5, #1036]
	ldr	r2, .L3976+24
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3915
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r5, #1036]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3915:
	ldr	r1, [r5, #1036]
	movs	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #1024]
	ldr	r2, .L3976+12
	ldr	r1, [r3]
	cmp	r1, r2
	it	ne
	strne	r2, [r3]
	ldr	r6, [r5, #1024]
	itt	ne
	movne	r2, #504
	ldrne	r3, .L3976
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
	b	.L3894
.L3870:
	ldr	r0, .L3976+28
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3913
	ldr	r2, [sp, #8]
	ldr	r3, .L3976+32
	cmp	r2, r3
	bne	.L3910
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3910
	ldr	r5, .L3976
	ldr	r3, [r5, #1032]
	cmp	r3, #1
	beq	.L3917
	ldr	r3, [r5, #1036]
	ldr	r2, .L3976+24
	ldr	r1, [r3]
	cmp	r1, r2
	it	ne
	strne	r2, [r3]
	ldr	r1, [r5, #1036]
	itttt	ne
	movne	r2, #504
	ldrne	r3, [r5, #1036]
	strne	r2, [r3, #4]
	strne	r0, [r3, #8]
	it	ne
	strne	r0, [r3, #12]
	movs	r3, #1
	movs	r0, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #1024]
	ldr	r2, .L3976+12
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3919
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L3976
	ldr	r3, [r3, #1024]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
.L3919:
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
	b	.L3894
.L3876:
	ldr	r0, .L3976+36
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3913
	ldr	r2, [sp, #8]
	ldr	r3, .L3976+40
	cmp	r2, r3
	bne	.L3910
	ldr	r2, [sp, #12]
	cmp	r2, #512
	bhi	.L3910
	add	r0, sp, #16
	ldr	r1, .L3976+44
	b	.L3969
.L3877:
	ldr	r0, .L3976+48
	cmp	r4, r0
	mov	r7, r0
	bne	.L3920
	ldr	r0, .L3976+52
	b	.L3959
.L3920:
	ldr	r3, .L3976+56
	cmp	r4, r3
	ite	eq
	ldreq	r0, .L3976+60
	ldrne	r0, .L3976+64
.L3959:
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3913
	ldr	r2, [sp, #8]
	ldr	r3, .L3976+68
	cmp	r2, r3
	bne	.L3965
	ldr	r3, .L3976+56
	ldr	r6, .L3976
	cmp	r4, r3
	bne	.L3923
	ldr	r3, [r6, #1024]
	mov	r0, r5
	add	r1, sp, #8
	movs	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #12]
	strb	r3, [sp, #16]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3867
	b	.L3965
.L3923:
	ldr	r3, [r6, #1552]
	cmp	r3, #10
	bhi	.L3965
	ldr	r1, [r6, #1024]
	ldr	r3, [sp, #12]
	ldr	r2, [r1, #24]
	cmp	r2, r3
	beq	.L3924
	cbz	r2, .L3924
	mov	r1, r3
	ldr	r0, .L3976+72
	bl	printk
	ldr	r3, [r6, #1552]
	adds	r3, r3, #1
	str	r3, [r6, #1552]
	b	.L3965
.L3924:
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
	bne	.L3962
	mvn	r4, #1
	b	.L3894
.L3977:
	.align	2
.L3976:
	.word	.LANCHOR5
	.word	-1067903959
	.word	.LC194
	.word	1263358532
	.word	.LC195
	.word	1112753220
	.word	1146313043
	.word	.LC196
	.word	1112755781
	.word	.LC197
	.word	1094995539
	.word	.LANCHOR5+1040
	.word	1074031666
	.word	.LC198
	.word	1074031676
	.word	.LC199
	.word	.LC200
	.word	1280262987
	.word	.LC201
.L3889:
	ldr	r0, .L3978
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3913
	ldr	r2, [sp, #8]
	ldr	r3, .L3978+4
	cmp	r2, r3
	bne	.L3910
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L3910
	ldr	r3, .L3978+8
	add	r0, sp, #16
	cmp	r4, r3
	ldr	r3, .L3978+12
	ite	eq
	ldreq	r1, [r3, #1556]
	ldrne	r1, [r3, #1560]
	adds	r1, r1, #8
.L3969:
	bl	memcpy
.L3970:
	mov	r0, r5
	add	r1, sp, #8
	mov	r2, #520
	b	.L3956
.L3886:
	ldr	r0, .L3978+16
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3913
	ldr	r2, [sp, #8]
	ldr	r3, .L3978+4
	cmp	r2, r3
	bne	.L3910
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L3910
	cmp	r4, r6
	add	r2, r2, #8
	ldr	r4, .L3978+12
	bne	.L3928
	add	r1, sp, #8
	ldr	r0, [r4, #1556]
	bl	memcpy
	movs	r0, #2
	ldr	r1, [r4, #1556]
	b	.L3963
.L3928:
	add	r1, sp, #8
	ldr	r0, [r4, #1560]
	bl	memcpy
	ldr	r1, [r4, #1560]
	movs	r0, #3
.L3963:
	bl	StorageSysDataStore
	mov	r4, r0
.L3894:
	ldr	r0, .L3978+20
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L3867
.L3929:
	mvn	r0, #21
	b	.L3867
.L3930:
	mvn	r0, #1
	b	.L3867
.L3931:
	mvn	r0, #2
.L3867:
	add	sp, sp, #532
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L3979:
	.align	2
.L3978:
	.word	.LC202
	.word	1145980246
	.word	1074034192
	.word	.LANCHOR5
	.word	.LC203
	.word	.LC204
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
	ldr	r4, .L3989
	mov	r3, #-1
	movs	r5, #0
	add	r0, r4, #1040
	ldr	r1, [r4, #992]
	str	r3, [r4, #1016]
	add	r3, r1, #512
	str	r3, [r4, #1024]
	str	r1, [r4, #1036]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #1556]
	strb	r5, [r4, #1008]
	str	r5, [r4, #1012]
	str	r5, [r4, #1020]
	str	r1, [r4, #1560]
	bl	ftl_memcpy
	ldr	r6, [r4, #1036]
	str	r5, [r4, #1028]
	str	r5, [r4, #1552]
	ldr	r3, [r6, #16]
	ldr	r7, [r6, #508]
	str	r3, [r4, #1032]
	cbz	r7, .L3981
	mov	r0, r6
	mov	r1, #508
	bl	js_hash
	cmp	r7, r0
	beq	.L3981
	str	r5, [r6, #16]
	ldr	r0, .L3989+4
	str	r5, [r4, #1032]
	bl	printk
.L3981:
	ldr	r3, [r4, #1032]
	cbz	r3, .L3982
	ldr	r2, .L3989+8
	ldr	r3, .L3989
	str	r2, [r3, #1028]
.L3982:
	ldr	r1, [r4, #1556]
	movs	r0, #2
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1560]
	movs	r0, #3
	bl	StorageSysDataLoad
	pop	{r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L3990:
	.align	2
.L3989:
	.word	.LANCHOR5
	.word	.LC205
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
	ldr	r6, .L4001
	str	r0, [r6, #1564]
	cmp	r0, #0
	beq	.L3998
	mov	r8, #0
	ldr	r9, .L4001+8
	mov	r10, r6
	mov	r4, r8
	mov	r7, r8
.L3996:
	lsls	r0, r7, #7
	movs	r1, #128
	ldr	r2, [r6, #1564]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L3994
	ldr	r3, [r10, #1564]
	ldr	r0, .L4001+4
	add	r2, r3, #65280
	adds	r2, r2, #252
	ldr	r1, [r3]
	ldr	r3, [r3, #4]
	ldr	r2, [r2]
	bl	printk
	ldr	r5, [r10, #1564]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L3995
	add	r3, r5, #65280
	ldr	r2, [r5, #4]
	adds	r3, r3, #252
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L3995
	cmp	r4, r3
	itt	cc
	movcc	r8, r7
	movcc	r4, r3
.L3995:
	cbnz	r7, .L4000
	movs	r7, #1
	b	.L3996
.L4000:
	cbz	r4, .L3997
	lsl	r0, r8, #7
	movs	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cbnz	r0, .L3994
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3997:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L4001+8
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	movw	r3, #65532
	str	r7, [r5, r3]
	movw	r3, #64504
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3994:
	ldr	r0, [r6, #1564]
	bl	kfree
	movs	r3, #0
	mov	r0, #-1
	str	r3, [r6, #1564]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3998:
	mvn	r0, #11
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4002:
	.align	2
.L4001:
	.word	.LANCHOR5
	.word	.LC206
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
	ldr	r3, .L4009
	ldr	r5, [r3, #1564]
	cbz	r5, .L4008
	ldrh	r7, [r5, #10]
	movs	r3, #0
.L4005:
	cmp	r3, r7
	bcs	.L4008
	add	r6, r5, r3, lsl #3
	ldrh	r4, [r6, #16]
	cmp	r4, r0
	bne	.L4006
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
.L4006:
	adds	r3, r3, #1
	b	.L4005
.L4008:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L4010:
	.align	2
.L4009:
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
	ldr	r2, .L4033
	.pad #28
	sub	sp, sp, #28
	mov	ip, r0
	mov	r3, r1
	ldr	r4, [r2, #1564]
	cmp	r4, #0
	beq	.L4026
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	ldrh	r2, [r4, #10]
	bic	r7, r7, #63
	movs	r6, #0
	str	r1, [sp, #4]
.L4013:
	cmp	r6, r2
	bcs	.L4031
	add	r5, r4, r6, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L4014
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L4015
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4026
	ldrh	r8, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #12]
.L4016:
	ldr	r2, [sp, #12]
	adds	r5, r5, #8
	cmp	r6, r2
	bcs	.L4032
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
	b	.L4016
.L4032:
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
	b	.L4030
.L4015:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, r0, fp
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L4030
.L4014:
	adds	r6, r6, #1
	b	.L4013
.L4031:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4026
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
.L4030:
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
	b	.L4012
.L4026:
	mov	r0, #-1
.L4012:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4034:
	.align	2
.L4033:
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
	beq	.L4044
	ldr	r3, .L4051
	cmp	r6, r3
	beq	.L4038
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L4039
	b	.L4050
.L4038:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4050
	ldr	r2, [r4]
	ldr	r3, .L4051+4
	cmp	r2, r3
	beq	.L4041
.L4042:
	mov	r5, #-1
	b	.L4037
.L4041:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	adds	r3, r0, #1
	beq	.L4042
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
	b	.L4037
.L4039:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L4050
	ldr	r2, [r4]
	ldr	r3, .L4051+4
	cmp	r2, r3
	bne	.L4042
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4042
	mov	r0, r4
	mov	r1, r5
	adds	r2, r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L4050
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4037
.L4050:
	mvn	r5, #13
.L4037:
	mov	r0, r4
	bl	kfree
	b	.L4036
.L4044:
	mov	r5, #-1
.L4036:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L4052:
	.align	2
.L4051:
	.word	1074034177
	.word	1448232273
	.fnend
	.size	rk_ftl_vendor_storage_ioctl, .-rk_ftl_vendor_storage_ioctl
	.global	gTable_Crc32
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
	.type	__func__.20269, %object
	.size	__func__.20269, 11
__func__.20269:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.space	1
	.type	__func__.21044, %object
	.size	__func__.21044, 17
__func__.21044:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21063, %object
	.size	__func__.21063, 16
__func__.21063:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21329, %object
	.size	__func__.21329, 17
__func__.21329:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.21012, %object
	.size	__func__.21012, 12
__func__.21012:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.21311, %object
	.size	__func__.21311, 21
__func__.21311:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20342, %object
	.size	__func__.20342, 8
__func__.20342:
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
	.ascii	"FTL version: 5.0.60 20200218\000"
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
	.ascii	"ECC:%d\012\000"
.LC96:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC97:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC98:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC99:
	.ascii	"data:\000"
.LC100:
	.ascii	"spare:\000"
.LC101:
	.ascii	"ReadRetry pageadd=%x ecc=%x err=%x\012\000"
.LC102:
	.ascii	"FLFB:%d %d\012\000"
.LC103:
	.ascii	"prog error: = %x\012\000"
.LC104:
	.ascii	"prog read error: = %x\012\000"
.LC105:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC106:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC107:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data= %x\012"
	.ascii	"\000"
.LC108:
	.ascii	"...%s enter...\012\000"
.LC109:
	.ascii	"superBlkID = %x vpc=%x\012\000"
.LC110:
	.ascii	"flashmode = %x pagenum = %x %x\012\000"
.LC111:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC112:
	.ascii	"blk = %x vpc=%x mode = %x\012\000"
.LC113:
	.ascii	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC114:
	.ascii	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC115:
	.ascii	"ftl_scan_all_ppa blk %x page %x\012\000"
.LC116:
	.ascii	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC117:
	.ascii	"%s finished\012\000"
.LC118:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC119:
	.ascii	"bad block:%d %d\012\000"
.LC120:
	.ascii	"FMFB:%d %d\012\000"
.LC121:
	.ascii	"E:bad block:%d\012\000"
.LC122:
	.ascii	"FMFB:Save %d %d\012\000"
.LC123:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC124:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC125:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC126:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC127:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC128:
	.ascii	"decrement_vpc_count %x = %d in free list\012\000"
.LC129:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC130:
	.ascii	"page map lost: %x %x\012\000"
.LC131:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC132:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC133:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC134:
	.ascii	"no ect\000"
.LC135:
	.ascii	"slc mode\000"
.LC136:
	.ascii	"BBT:\000"
.LC137:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC138:
	.ascii	"map_ppn:\000"
.LC139:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC140:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC141:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC142:
	.ascii	"Ftlscanalldata = %x\012\000"
.LC143:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC144:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC145:
	.ascii	"RSB refresh addr %x\012\000"
.LC146:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC147:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC148:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC149:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
.LC150:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC151:
	.ascii	"GC des block %x done\012\000"
.LC152:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC153:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC154:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC155:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC156:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC157:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC158:
	.ascii	"FtlInit %x\012\000"
.LC159:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC160:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC161:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC162:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\012"
	.ascii	"\000"
.LC163:
	.ascii	":\000"
.LC164:
	.ascii	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC165:
	.ascii	"Mblk:\000"
.LC166:
	.ascii	"L2P:\000"
.LC167:
	.ascii	"L2PC:\000"
.LC168:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC169:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC170:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC171:
	.ascii	"idblk:\000"
.LC172:
	.ascii	"idb reverse %x %x\012\000"
.LC173:
	.ascii	"write_idblock totle_sec %x %x %x %x\012\000"
.LC174:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC175:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC176:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC177:
	.ascii	"write\000"
.LC178:
	.ascii	"read\000"
.LC179:
	.ascii	"write_idblock error %d\012\000"
.LC180:
	.ascii	"wl_lba %p %x %x %x\012\000"
.LC181:
	.ascii	"READ_SECTOR_IO\012\000"
.LC182:
	.ascii	"rk_copy_from_user error\012\000"
.LC183:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC184:
	.ascii	"rk_copy_to_user error\012\000"
.LC185:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC186:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC187:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC188:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC189:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC190:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC191:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC192:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC193:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC194:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC195:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC196:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC197:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC198:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC199:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC200:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC201:
	.ascii	"LockKey not match %d\012\000"
.LC202:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC203:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC204:
	.ascii	"return ret = %lx\012\000"
.LC205:
	.ascii	"secureBootEn check error\012\000"
.LC206:
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
	.size	NandFlashParaTbl, 2656
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
	.byte	6
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
	.type	gTable_Crc32, %object
	.size	gTable_Crc32, 1024
gTable_Crc32:
	.word	0
	.word	79760823
	.word	159521646
	.word	222500569
	.word	319043292
	.word	398801771
	.word	445001138
	.word	507977733
	.word	638086584
	.word	583622671
	.word	797603542
	.word	726367073
	.word	890002276
	.word	835540691
	.word	1015955466
	.word	944721341
	.word	1276173168
	.word	1221711559
	.word	1167245342
	.word	1096011177
	.word	1595207084
	.word	1540743195
	.word	1452734146
	.word	1381497717
	.word	1780004552
	.word	1859763071
	.word	1671081382
	.word	1734058001
	.word	2031910932
	.word	2111671715
	.word	1889442682
	.word	1952421581
	.word	-1742620960
	.word	-1662993577
	.word	-1851544178
	.word	-1788698567
	.word	-1960476612
	.word	-1880846965
	.word	-2102944942
	.word	-2040097051
	.word	-1104553128
	.word	-1159145745
	.word	-1213480906
	.word	-1284846207
	.word	-1389499004
	.word	-1444093901
	.word	-1531971862
	.word	-1603339427
	.word	-734958192
	.word	-789553113
	.word	-575441154
	.word	-646808759
	.word	-952804532
	.word	-1007397125
	.word	-826851294
	.word	-898216555
	.word	-231145432
	.word	-151515745
	.word	-71623866
	.word	-8775951
	.word	-516081932
	.word	-436454589
	.word	-390124134
	.word	-327278547
	.word	880959607
	.word	809725376
	.word	1023441689
	.word	968980142
	.word	663115435
	.word	591878940
	.word	772033989
	.word	717570162
	.word	311037391
	.word	374014072
	.word	453514913
	.word	533273366
	.word	26098451
	.word	89077412
	.word	135012477
	.word	214773194
	.word	2022882055
	.word	2085861040
	.word	1896915049
	.word	1976675806
	.word	1805028827
	.word	1868005484
	.word	1645516469
	.word	1725274882
	.word	1587205823
	.word	1515969288
	.word	1461243345
	.word	1406779494
	.word	1302257763
	.word	1231023572
	.word	1142749965
	.word	1088288442
	.word	-1398548841
	.word	-1469916384
	.word	-1524511239
	.word	-1579106226
	.word	-1079517109
	.word	-1150882308
	.word	-1239024859
	.word	-1293617518
	.word	-1968456913
	.word	-1905609064
	.word	-2094423999
	.word	-2014794250
	.word	-1716548109
	.word	-1653702588
	.word	-1876060515
	.word	-1796433110
	.word	-525136409
	.word	-462290864
	.word	-382658935
	.word	-303031490
	.word	-206095557
	.word	-143247732
	.word	-97181611
	.word	-17551902
	.word	-960798625
	.word	-1032163864
	.word	-818316495
	.word	-872909178
	.word	-708880765
	.word	-780248268
	.word	-599962131
	.word	-654557094
	.word	1761919214
	.word	1841679705
	.word	1619450752
	.word	1682429495
	.word	2046883378
	.word	2126642053
	.word	1937960284
	.word	2000937195
	.word	1326230870
	.word	1271767265
	.word	1183757880
	.word	1112521615
	.word	1544067978
	.word	1489606205
	.word	1435140324
	.word	1363905875
	.word	622074782
	.word	567612969
	.word	748028144
	.word	676793671
	.word	907029826
	.word	852566261
	.word	1066546732
	.word	995310491
	.word	52196902
	.word	131955601
	.word	178154824
	.word	241131775
	.word	270024954
	.word	349785421
	.word	429546388
	.word	492525091
	.word	-249203186
	.word	-169575495
	.word	-123245216
	.word	-60399401
	.word	-501137198
	.word	-421507739
	.word	-341615684
	.word	-278768117
	.word	-684909642
	.word	-739502591
	.word	-558956328
	.word	-630321809
	.word	-1003934358
	.word	-1058529059
	.word	-844417532
	.word	-915784781
	.word	-1120555650
	.word	-1175150391
	.word	-1263028720
	.word	-1334395993
	.word	-1372480606
	.word	-1427073515
	.word	-1481408308
	.word	-1552773765
	.word	-1690451770
	.word	-1610822287
	.word	-1832920152
	.word	-1770072545
	.word	-2009467366
	.word	-1929839699
	.word	-2118390412
	.word	-2055544637
	.word	1569104025
	.word	1497869614
	.word	1409596407
	.word	1355134528
	.word	1317180997
	.word	1245944818
	.word	1191218475
	.word	1136754844
	.word	2072956193
	.word	2135933078
	.word	1913443919
	.word	1993202680
	.word	1753938941
	.word	1816917578
	.word	1627971731
	.word	1707732260
	.word	295074793
	.word	358053470
	.word	403988615
	.word	483749168
	.word	43142453
	.word	106119298
	.word	185620059
	.word	265378796
	.word	933107281
	.word	861871078
	.word	1042025791
	.word	987562120
	.word	614080653
	.word	542846266
	.word	756562915
	.word	702101076
	.word	-978905479
	.word	-1050272818
	.word	-869987049
	.word	-924581728
	.word	-693952347
	.word	-765317870
	.word	-551470133
	.word	-606062980
	.word	-475038783
	.word	-412191114
	.word	-366124881
	.word	-286495464
	.word	-257209059
	.word	-194363222
	.word	-114731405
	.word	-35103804
	.word	-1984443127
	.word	-1921597250
	.word	-2143955353
	.word	-2064327728
	.word	-1699480619
	.word	-1636632990
	.word	-1825447749
	.word	-1745818356
	.word	-1346395983
	.word	-1417761530
	.word	-1505903649
	.word	-1560496536
	.word	-1128556947
	.word	-1199924262
	.word	-1254519549
	.word	-1309114188
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
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 4
gpNandParaInfo:
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
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.space	1
	.type	gNandMaxDie, %object
	.size	gNandMaxDie, 1
gNandMaxDie:
	.space	1
	.space	2
	.type	DieCsIndex, %object
	.size	DieCsIndex, 8
DieCsIndex:
	.space	8
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
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.space	1
	.space	2
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
	.space	4
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
	.type	g_idb_buffer, %object
	.size	g_idb_buffer, 4
g_idb_buffer:
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
