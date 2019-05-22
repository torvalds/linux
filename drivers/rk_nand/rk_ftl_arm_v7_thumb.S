/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2019-05-08
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
	cmp	r0, #256
	ldr	r3, .L42
	bls	.L38
	mov	r2, #512
	b	.L41
.L38:
	cmp	r0, #128
	bhi	.L40
	str	r0, [r3, #4]
	bx	lr
.L40:
	mov	r2, #256
.L41:
	str	r2, [r3, #4]
	bx	lr
.L43:
	.align	2
.L42:
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
	ldr	r4, .L45
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
.L46:
	.align	2
.L45:
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
	ldr	r4, .L51
	lsrs	r5, r1, #8
	add	r3, r4, r0, lsl #3
	ldrb	r4, [r4, #60]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r4, #1
	lsr	r4, r1, #16
	lsl	r3, r3, #8
	add	r3, r3, r2
	bne	.L48
	movs	r2, #6
	str	r2, [r3, #2056]
	movs	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r6, [r3, #2052]
	str	r5, [r3, #2052]
	str	r4, [r3, #2052]
	b	.L50
.L48:
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
.L50:
	movs	r2, #224
	str	r2, [r3, #2056]
	pop	{r4, r5, r6, lr}
	b	FlashSetRandomizer
.L52:
	.align	2
.L51:
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
	ldr	r4, .L54
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
.L55:
	.align	2
.L54:
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
	ldr	r5, .L61
	add	r0, r5, r0, lsl #3
	ldrb	r3, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	lsls	r3, r3, #8
	cbz	r2, .L57
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
.L57:
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
.L62:
	.align	2
.L61:
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
	ldr	r4, .L64
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
.L65:
	.align	2
.L64:
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
	@ link register save eliminated.
	ldr	r3, .L67
	add	r0, r3, r0, lsl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r2, lsl #8
	movs	r2, #16
	str	r2, [r3, #2056]
	bx	lr
.L68:
	.align	2
.L67:
	.word	.LANCHOR0
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
	ldr	r2, .L70
	add	r0, r2, r0, lsl #3
	ldrb	r2, [r2, #54]	@ zero_extendqisi2
	ldrb	r1, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r1, lsl #8
	str	r2, [r3, #2056]
	bx	lr
.L71:
	.align	2
.L70:
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
	ldr	r3, .L76
	add	r1, r1, r0
	push	{r4, lr}
	.save {r4, lr}
.L73:
	cmp	r0, r1
	beq	.L75
	lsls	r2, r3, #5
	ldrb	r4, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, r4
	eors	r3, r3, r2
	b	.L73
.L75:
	mov	r0, r3
	pop	{r4, pc}
.L77:
	.align	2
.L76:
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
	ldr	r3, .L81
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
.L82:
	.align	2
.L81:
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
	ldr	r0, .L88
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
	bne	.L85
	ldrb	r2, [r6, #140]	@ zero_extendqisi2
	cbnz	r2, .L85
	ldr	r2, .L88
	add	r1, r2, r1, lsl #1
	ldrh	r1, [r1, #144]
.L85:
	add	r6, r6, r0, lsl #2
	ldr	r2, [r6, #656]
	mla	fp, fp, r3, r2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	cmp	r3, #1
	add	r1, r1, fp
	str	r1, [r9]
	str	r0, [r8]
	bls	.L87
	ldr	r0, [r7, #4]
	ldr	r3, [r7, #40]
	add	r0, r0, #1024
	subs	r3, r0, r3
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	b	.L86
.L87:
	movs	r0, #0
.L86:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L89:
	.align	2
.L88:
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
	ldr	r3, .L91
	ldr	r2, [r3, #688]
	str	r0, [r3, #688]
	mov	r0, r2
	bx	lr
.L92:
	.align	2
.L91:
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
	ldr	r3, .L94
	ldr	r3, [r3, #692]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L95:
	.align	2
.L94:
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
	ldr	r3, .L98
	add	r0, r3, r0, lsl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	adds	r2, r2, #8
	add	r0, r0, r2, lsl #8
	bx	lr
.L99:
	.align	2
.L98:
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
	ldr	r3, .L101
	ldr	r2, [r3, #76]
	lsls	r3, r0, #8
	orr	r0, r3, r0, lsl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L102:
	.align	2
.L101:
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
	ldr	r3, .L104
	orr	r0, r0, #16640
	ldr	r3, [r3, #76]
	str	r0, [r3, #344]
	bx	lr
.L105:
	.align	2
.L104:
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
	ldr	r3, .L109
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	cbnz	r0, .L107
	bfi	r3, r0, #13, #1
	b	.L108
.L107:
	orr	r3, r3, #253952
.L108:
	str	r3, [r2]
	bx	lr
.L110:
	.align	2
.L109:
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
	ldr	r3, .L118
	ands	r1, r0, #6
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	beq	.L112
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
	b	.L114
.L112:
	bfi	r3, r1, #13, #1
.L114:
	str	r3, [r2]
	movs	r0, #0
	bx	lr
.L119:
	.align	2
.L118:
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
	ldr	r2, .L121
	add	r0, r2, r0, lsl #3
	movs	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	lsls	r2, r2, r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L122:
	.align	2
.L121:
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
	ldr	r3, .L124
	add	r0, r3, r0, lsl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L125:
	.align	2
.L124:
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
	ldr	r3, .L127
	umull	r0, r1, r0, r3
	ldr	r3, .L127+4
	ldr	r3, [r3, #8]
	lsrs	r0, r1, #6
	blx	r3
	movs	r0, #0
	pop	{r3, pc}
.L128:
	.align	2
.L127:
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
	ldr	r3, .L130
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
.L131:
	.align	2
.L130:
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
	ldr	r9, .L141+8
	movs	r4, #0
	ldr	r7, .L141
	mov	r5, r0
	add	r10, r9, #256
	mov	r6, r1
.L133:
	ldrb	r3, [r7, #697]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L140
	movs	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	movs	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldrb	r3, [r7, #696]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L134
	add	r3, r4, r8
	add	r3, r3, r10
	b	.L139
.L134:
	cmp	r3, #35
	bne	.L136
	ldr	r2, .L141+4
	add	r3, r4, r8
	add	r3, r3, r2
.L139:
	ldrsb	r3, [r3, #5]
	b	.L138
.L136:
	add	r3, r9, r6
	ldrsb	r3, [r3, #400]
.L138:
	str	r3, [r5]
	adds	r4, r4, #1
	b	.L133
.L140:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L142:
	.align	2
.L141:
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
	ldr	r3, .L147
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, lsl #2
	movs	r4, #0
	ldr	r8, .L147+4
	adds	r5, r1, #3
	mov	r9, #161
	mov	r6, r0
	mov	r7, r3
	mov	r10, r4
.L144:
	ldrb	r3, [r8, #697]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L146
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	adds	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L144
.L146:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L148:
	.align	2
.L147:
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
	ldr	r5, .L157
	mov	r6, r0
	mov	r9, r1
	mov	fp, r2
	ldr	r3, [r5, #692]
	ldrb	r4, [r3, #19]	@ zero_extendqisi2
	lsls	r3, r0, #3
	cmp	r4, #6
	bne	.L150
	add	r4, r5, r0, lsl #6
	addw	r4, r4, #718
	add	r4, r4, r7, lsl #2
	b	.L151
.L150:
	cmp	r4, #7
	bne	.L152
	mov	r10, #160
	add	r4, r7, r7, lsl #2
	mla	r10, r10, r0, r5
	addw	r10, r10, #726
	add	r4, r10, r4, lsl #1
	b	.L151
.L152:
	cmp	r4, #8
	iteet	ne
	addne	r4, r7, r3
	addweq	r4, r5, #726
	addeq	r2, r7, r7, lsl #2
	addne	r4, r5, r4, lsl #3
	ite	eq
	addeq	r4, r4, r2
	addwne	r4, r4, #718
.L151:
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
.L154:
	cmp	r1, r9
	beq	.L156
	ldrb	r3, [r1, #1]!	@ zero_extendqisi2
	movs	r0, #200
	str	r3, [fp, #2052]
	str	r1, [sp, #4]
	bl	NandcDelayns
	ldrsb	r3, [r4, #1]!
	str	r3, [fp, #2048]
	ldr	r1, [sp, #4]
	b	.L154
.L156:
	add	r8, r8, r10
	movs	r3, #22
	mov	r0, r6
	add	r5, r5, r6
	str	r3, [r8, #2056]
	bl	NandcFlashDeCs
	strb	r7, [r5, #1552]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L158:
	.align	2
.L157:
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
	ldr	r3, .L165
	ldr	r2, [r3, #692]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	subs	r2, r2, #1
	cmp	r2, #7
	bhi	.L159
	movs	r4, #0
	mov	r5, r3
.L160:
	ldr	r2, .L165+4
	uxtb	r0, r4
	ldrb	r3, [r2, r4, lsl #3]	@ zero_extendqisi2
	cmp	r3, #173
	bne	.L161
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	subw	r2, r2, #854
	movs	r3, #0
	bl	HynixSetRRPara
.L161:
	adds	r4, r4, #1
	cmp	r4, #4
	bne	.L160
.L159:
	pop	{r3, r4, r5, pc}
.L166:
	.align	2
.L165:
	.word	.LANCHOR0
	.word	.LANCHOR0+1556
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
	ldr	r5, .L177
	add	r0, r5, r0, lsl #3
	ldr	r3, [r5, #692]
	ldrb	r4, [r0, #16]	@ zero_extendqisi2
	ldr	r6, [r0, #12]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	lsl	r3, r4, #8
	add	r4, r4, #8
	bne	.L168
	cbnz	r2, .L169
	ldrb	r2, [r5, #57]	@ zero_extendqisi2
	b	.L176
.L169:
	ldrb	r2, [r5, #58]	@ zero_extendqisi2
.L176:
	add	r3, r3, r6
	str	r2, [r3, #2056]
	ldrb	r0, [r5, #59]	@ zero_extendqisi2
	cbz	r0, .L172
	movs	r3, #0
	add	r5, r6, r4, lsl #8
.L171:
	cmp	r3, r0
	bcs	.L172
	lsls	r2, r3, #3
	adds	r3, r3, #1
	lsr	r2, r1, r2
	uxtb	r2, r2
	str	r2, [r5, #4]
	b	.L171
.L168:
	add	r3, r3, r6
	movs	r2, #112
	str	r2, [r3, #2056]
.L172:
	movs	r0, #80
	lsls	r4, r4, #8
	bl	NandcDelayns
	ldr	r0, [r6, r4]
	uxtb	r0, r0
	pop	{r4, r5, r6, pc}
.L178:
	.align	2
.L177:
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
.L183:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	mov	r3, r0
	beq	.L183
	lsls	r3, r3, #25
	bpl	.L183
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
	ldr	r5, .L193
	add	r4, r5, r0, lsl #4
	ldr	r3, [r4, #1596]
	ldrb	r6, [r4, #1588]	@ zero_extendqisi2
	cbz	r3, .L188
	add	r5, r5, r8, lsl #2
	mov	r0, r6
	bl	NandcFlashCs
	ldr	r1, [r4, #1592]
	ldr	r2, [r5, #656]
	mov	r0, r6
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r5, r0
	mov	r0, r6
	bl	NandcFlashDeCs
	ldr	r2, [r4, #1596]
	sbfx	r3, r5, #0, #1
	str	r3, [r2]
	movs	r2, #0
	ldr	r1, [r4, #1600]
	str	r2, [r4, #1596]
	cbz	r1, .L188
	str	r3, [r1]
	str	r2, [r4, #1600]
.L188:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L194:
	.align	2
.L193:
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
	ldr	r2, .L196
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
.L197:
	.align	2
.L196:
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
	ldr	r3, .L203
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, lsl #3
	ldr	r4, .L203+4
	ldr	r5, [r0, #12]
.L200:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #22
	bmi	.L201
	movs	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L200
	mov	r0, #-1
	b	.L199
.L201:
	movs	r0, #0
.L199:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L204:
	.align	2
.L203:
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
	ldr	r3, .L210
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, lsl #3
	ldr	r4, .L210+4
	ldr	r5, [r0, #12]
	movs	r0, #150
	bl	NandcDelayns
.L207:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #22
	bmi	.L208
	movs	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L207
	mov	r0, #-1
	b	.L206
.L208:
	movs	r0, #0
.L206:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L211:
	.align	2
.L210:
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
	ldr	r3, .L213
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
.L214:
	.align	2
.L213:
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
	ldr	r4, .L217
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cbz	r3, .L215
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
.L215:
	pop	{r3, r4, r5, pc}
.L218:
	.align	2
.L217:
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
	ldr	r4, .L221
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cbz	r3, .L219
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
.L219:
	pop	{r3, r4, r5, pc}
.L222:
	.align	2
.L221:
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
	ldr	r5, .L252
	movs	r4, #0
	movs	r7, #239
	mov	lr, #128
	mov	ip, #1
	mov	r8, #35
	ldrb	r3, [r5, #1716]	@ zero_extendqisi2
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
.L234:
	ldr	r1, .L252+4
	ldr	r3, [sp, #4]
	ldrb	r1, [r4, r1]	@ zero_extendqisi2
	ldr	r6, [r4, r3]
	add	r3, r3, r4
	cmp	r1, #152
	ldr	r3, [r3, #4]
	beq	.L225
	cmp	r1, #69
	beq	.L225
	cmp	r1, #173
	beq	.L225
	cmp	r1, #44
	bne	.L226
.L225:
	cmp	r0, #1
	uxtb	r3, r3
	bne	.L227
	ldr	r5, [sp, #12]
	cbz	r5, .L226
	lsls	r3, r3, #8
	cmp	r1, #173
	add	fp, r6, r3
	str	r7, [fp, #2056]
	bne	.L228
	str	r0, [fp, #2052]
	b	.L251
.L228:
	cmp	r1, #44
	itete	eq
	streq	r0, [fp, #2052]
	strne	lr, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L232
.L227:
	ldr	r5, [sp, #8]
	cbz	r5, .L226
	lsls	r3, r3, #8
	cmp	r1, #173
	add	fp, r6, r3
	str	r7, [fp, #2056]
	bne	.L231
	str	ip, [fp, #2052]
	str	r9, [fp, #2048]
	b	.L232
.L231:
	cmp	r1, #44
	bne	.L233
	str	ip, [fp, #2052]
	str	r8, [fp, #2048]
	b	.L232
.L233:
	str	lr, [fp, #2052]
.L251:
	str	r2, [fp, #2048]
.L232:
	add	r3, r3, r6
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L226:
	adds	r4, r4, #8
	cmp	r4, #32
	bne	.L234
	movs	r0, #0
	bl	NandcWaitFlashReady
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L253:
	.align	2
.L252:
	.word	.LANCHOR0
	.word	.LANCHOR0+1556
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
	ldr	r5, .L255
	ldr	r3, .L255+4
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
.L256:
	.align	2
.L255:
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
	ldr	r2, .L258
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
.L259:
	.align	2
.L258:
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
	ldr	r0, .L267
	ldr	r1, .L267+4
	add	r4, r4, r4, lsl #2
	movs	r2, #0
	sub	r6, r0, #48
.L261:
	ldrb	r3, [r1, #697]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L266
	ldrb	r3, [r1, #696]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	ite	eq
	addeq	r3, r3, r6
	addne	r3, r3, r0
	adds	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L261
.L266:
	movs	r0, #0
	pop	{r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L268:
	.align	2
.L267:
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
	ldr	r3, .L270
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
.L271:
	.align	2
.L270:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	ldr	r7, .L284
	.pad #24
	sub	sp, sp, #24
	ldr	r9, .L284+8
	mov	r6, r0
	mov	r10, r1
	mov	r8, r5
	ldr	r3, [r7]
	str	r3, [sp, #20]
.L273:
	cmp	r8, r10
	beq	.L283
	rsb	r3, r8, r10
	add	r2, sp, #12
	adds	r0, r6, r5
	movs	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #16]
	cmp	r3, r2
	bcc	.L274
	mov	r3, #-1
	str	r3, [r6, r5]
	b	.L275
.L274:
	add	r2, r9, r3
	add	r3, r9, r3, lsl #4
	ldrb	r4, [r2, #1720]	@ zero_extendqisi2
	strb	r4, [r3, #1588]
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	movs	r2, #0
	mov	r0, r4
	ldr	r1, [sp, #12]
	bl	FlashEraseCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldr	r1, [sp, #12]
	bl	FlashReadStatus
	movs	r2, #0
	ldr	r3, [sp, #12]
	sbfx	r0, r0, #0, #1
	str	r0, [r6, r5]
	mov	r0, r4
	ldr	r1, [r9, #4]
	add	r1, r1, r3
	bl	FlashEraseCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldr	r1, [sp, #12]
	bl	FlashReadStatus
	lsls	r3, r0, #31
	itt	mi
	movmi	r3, #-1
	strmi	r3, [r6, r5]
	ldr	r3, [r6, r5]
	adds	r3, r3, #1
	bne	.L277
	ldr	r0, .L284+4
	ldr	r1, [sp, #12]
	bl	printk
.L277:
	mov	r0, r4
	bl	NandcFlashDeCs
.L275:
	add	r8, r8, #1
	adds	r5, r5, #36
	b	.L273
.L283:
	ldr	r2, [sp, #20]
	movs	r0, #0
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L279
	bl	__stack_chk_fail
.L279:
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L285:
	.align	2
.L284:
	.word	__stack_chk_guard
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
	ldr	r0, [r4, #1728]
	cbz	r0, .L293
	movs	r0, #0
	bl	flash_enter_slc_mode
	b	.L294
.L293:
	bl	flash_exit_slc_mode
.L294:
	ldrb	r3, [r4, #1732]	@ zero_extendqisi2
	ldr	r5, .L302
	cbz	r3, .L295
	ldrb	r3, [r5, #1716]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L295
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	movs	r3, #0
	strb	r3, [r5, #1732]
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
	str	r4, [r3, #1736]
	movw	r4, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r3, #1740]
	ldr	r1, [r0, #352]
	cmp	r1, r4
	mov	r4, r3
	str	r1, [r3, #1744]
	itt	eq
	moveq	r3, #8
	streq	r3, [r4, #1740]
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
	str	r5, [r4, #1776]
	str	r5, [r4, #1784]
	str	r0, [r4, #1748]
	str	r0, [r4, #1752]
	add	r0, r0, #32768
	str	r0, [r4, #1756]
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
	str	r0, [r3, #1788]
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
	strb	r0, [r3, #1792]
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
	addw	r6, r3, #1556
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
	ldrb	r3, [r4, #1732]	@ zero_extendqisi2
	ldr	r5, .L352
	cbz	r3, .L347
	movs	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #1716]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #97]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L347:
	ldr	r3, [r4, #692]
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r3, .L393
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L393+4
	mov	r9, r0
	mov	r10, r1
	ldr	r2, [r3]
	ldrb	r5, [r4]	@ zero_extendqisi2
	str	r3, [sp, #12]
	str	r2, [sp, #28]
	cbz	r5, .L377
	mov	r1, r8
	bl	FlashEraseSLc2KBlocks
	b	.L360
.L377:
	mov	fp, r4
.L359:
	cmp	r5, r8
	bcs	.L391
	movs	r3, #36
	movs	r1, #0
	add	r2, sp, #20
	mul	r6, r3, r5
	add	r3, r9, r6
	str	r3, [sp, #8]
	rsb	r3, r5, r8
	ldr	r0, [sp, #8]
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #24]
	cmp	r0, r3
	bcc	.L361
	mov	r3, #-1
	str	r3, [r9, r6]
	b	.L362
.L361:
	ldrb	r3, [fp, #1793]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, lsl #4
	it	eq
	moveq	r7, #0
	ldr	r3, [r3, #1596]
	cbz	r3, .L364
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L364:
	ldr	r1, [sp, #24]
	ldr	r3, [sp, #8]
	lsls	r2, r1, #4
	add	lr, r4, r2
	str	r3, [lr, #1596]
	movs	r3, #0
	str	r3, [lr, #1600]
	ldr	r3, [sp, #20]
	str	r3, [lr, #1592]
	cbz	r7, .L365
	add	r3, r6, #36
	add	r3, r3, r9
	str	r3, [lr, #1600]
.L365:
	add	r1, r1, r4
	add	r2, r2, r4
	ldrb	r6, [r1, #1720]	@ zero_extendqisi2
	mov	r0, r6
	strb	r6, [r2, #1588]
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
	ldr	r3, [sp, #24]
	mov	r0, r6
	ldr	r1, [sp, #20]
	add	r5, r5, r7
	add	r3, r4, r3, lsl #2
	ldr	r2, [r3, #656]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	mov	r2, r7
	ldr	r1, [sp, #20]
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
.L362:
	adds	r5, r5, #1
	b	.L359
.L391:
	ldr	r0, [r4, #76]
	movs	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r6, .L393+4
	addw	r7, r6, #1588
.L369:
	ldrb	r2, [r4, #1717]	@ zero_extendqisi2
	ldr	r3, .L393+4
	cmp	r5, r2
	bcs	.L392
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
.L392:
	ldr	r2, [r3, #1796]
	cbnz	r2, .L372
.L374:
	movs	r0, #0
	b	.L360
.L372:
	ldrb	r3, [r3, #1556]	@ zero_extendqisi2
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
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L376
	bl	__stack_chk_fail
.L376:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L394:
	.align	2
.L393:
	.word	__stack_chk_guard
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
	ldr	r3, .L397
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
.L398:
	.align	2
.L397:
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
.L400:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #11
	bpl	.L400
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
	mov	r8, r2
	mov	r7, r1
	ldr	r2, [sp, #60]
	ldr	fp, [sp, #56]
	cbnz	r2, .L417
	adds	r2, fp, #0
	it	ne
	movne	r2, #1
	b	.L404
.L417:
	movs	r2, #1
.L404:
	ldr	r5, .L422
	movs	r4, #0
	add	r0, r5, r0, lsl #3
	ldr	r6, [r0, #12]
	ldrb	r1, [r0, #16]	@ zero_extendqisi2
	movs	r0, #16
	ldr	r9, [r6, #12]
	bfi	r9, r0, #8, #8
	bfi	r9, r4, #3, #1
	bfi	r4, r7, #1, #1
	bfi	r9, r1, #5, #3
	orr	r4, r4, #8
	movs	r1, #1
	bfi	r4, r1, #5, #2
	lsrs	r3, r3, r1
	orr	r4, r4, #536870912
	orr	r4, r4, #1024
	bfi	r4, r3, #4, #1
	ldr	r3, [r5, #1740]
	cmp	r3, #3
	bls	.L405
	ldr	r3, [r6, #16]
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #4
	str	r3, [sp, #12]
	cmp	r2, #0
	beq	.L406
	cbnz	r7, .L407
.L415:
	mov	r0, r6
	add	r8, r8, #1
	bl	rk_nandc_xfer_irq_flag_init
	ldr	r3, [r6, #368]
	asr	r8, r8, #1
	orr	r3, r3, #1
	str	r3, [r6, #368]
	ldr	r3, [r6, #364]
	bfi	r4, r8, #22, #6
	orr	r3, r3, #1
	str	r3, [r6, #364]
	cmp	fp, #0
	beq	.L408
	mov	r0, fp
	b	.L409
.L407:
	ldr	r3, [r5, #1788]
	mov	ip, r5
	ldr	r0, [sp, #60]
	cmp	r3, #25
	ite	cc
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp]
	lsr	r3, r8, #1
	str	r3, [sp, #4]
	movs	r3, #0
	mov	r1, r3
.L411:
	ldr	r2, [sp, #4]
	cmp	r1, r2
	bcs	.L415
	ldr	r2, [sp, #60]
	lsr	r10, r3, #2
	cbz	r2, .L412
	ldrh	r2, [r0, #2]
	ldrh	lr, [r0], #4
	orr	lr, lr, r2, lsl #16
	ldr	r2, [ip, #1756]
	str	lr, [r2, r10, lsl #2]
	b	.L413
.L412:
	ldr	r2, [ip, #1756]
	mov	lr, r2
	mov	r2, #-1
	str	r2, [lr, r10, lsl #2]
.L413:
	ldr	r2, [sp]
	adds	r1, r1, #1
	add	r3, r3, r2
	b	.L411
.L408:
	ldr	r0, [r5, #1752]
.L409:
	ldr	r3, [r5, #1756]
	ubfx	r8, r4, #22, #5
	mov	r2, r7
	str	r0, [r5, #1760]
	lsl	r1, r8, #10
	str	r3, [r5, #1764]
	bl	rknand_dma_map_single
	mov	r2, r7
	lsl	r1, r8, #7
	clz	r7, r7
	lsrs	r7, r7, #5
	str	r0, [r5, #1768]
	ldr	r0, [r5, #1764]
	bl	rknand_dma_map_single
	movs	r3, #1
	str	r3, [r5, #1776]
	tst	fp, #3
	ldr	r3, [r5, #1768]
	str	r0, [r5, #1772]
	str	r3, [r6, #20]
	ldr	r3, [r5, #1772]
	str	r3, [r6, #24]
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
	orr	r7, r3, r7, lsl #1
	str	r7, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #1
	str	r3, [sp, #12]
.L406:
	ldr	r3, [sp, #12]
	str	r3, [r6, #16]
.L405:
	str	r9, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L423:
	.align	2
.L422:
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
.L425:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bhi	.L427
	lsls	r2, r2, #1
	b	.L425
.L427:
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
	ldr	r3, .L431
	cmp	r0, #23
	it	ls
	movls	r0, #24
	ldrh	r2, [r3, #1804]
	ldrh	r1, [r3, #1814]
	str	r0, [r3, #1800]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #1820]
	strh	r0, [r3, #1812]	@ movhi
	movs	r0, #0
	str	r2, [r3, #1808]
	subs	r2, r1, r2
	str	r2, [r3, #1816]
	bx	lr
.L432:
	.align	2
.L431:
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
	ldr	r4, .L461
	ldrh	lr, [r0, #8]
	str	r1, [sp]
	ldrh	r1, [sp]
	ldrh	r2, [r0, #10]
	ldrh	r3, [r0, #12]
	add	r0, r4, #1832
	strh	r1, [r4, #1814]	@ movhi
	movs	r1, #0
	strh	lr, [r4, #1824]	@ movhi
	strh	r2, [r4, #1826]	@ movhi
	strh	r3, [r4, #1828]	@ movhi
.L434:
	strb	r1, [r1, r0]
	adds	r1, r1, #1
	cmp	r1, #32
	bne	.L434
	ldrh	r0, [r5, #14]
	ldrh	r1, [r5, #20]
	cmp	r1, r0, lsr #8
	bcs	.L435
	uxtb	r10, r3
	ldr	r9, .L461+4
	lsl	r1, r10, #1
	uxtb	r1, r1
	str	r1, [sp, #8]
	subs	r1, r2, #1
	muls	r1, r3, r1
	str	r1, [sp, #4]
	movs	r1, #0
.L436:
	cmp	r1, r3
	bcs	.L438
	ldr	r6, [sp, #4]
	rsb	ip, r3, r1
	uxtb	r0, r1
	add	ip, ip, r9
	adds	r6, r1, r6
	add	r6, r9, r6
	str	r6, [sp, #12]
	movs	r6, #0
	mov	r8, r6
.L439:
	cmp	r8, r2
	add	r6, r6, r3
	bcs	.L460
	ldr	r7, [sp, #12]
	add	fp, r0, r10
	strb	r0, [ip, r6]
	add	r8, r8, #1
	strb	fp, [r7, r6]
	ldr	r7, [sp, #8]
	add	r0, r0, r7
	uxtb	r0, r0
	b	.L439
.L460:
	adds	r1, r1, #1
	b	.L436
.L438:
	lsls	r2, r2, #1
	strh	r2, [r4, #1826]	@ movhi
	ldr	r2, [sp]
	lsrs	r7, r2, #1
	strh	r7, [r4, #1814]	@ movhi
.L435:
	cmp	lr, #1
	mov	r2, #5
	strh	r2, [r4, #1864]	@ movhi
	mov	r2, #0
	strh	r2, [r4, #1866]	@ movhi
	it	eq
	ldreq	r2, .L461
	ldrb	r9, [r4]	@ zero_extendqisi2
	it	eq
	strheq	lr, [r2, #1864]	@ movhi
	mov	r2, #4352
	strh	r2, [r4, #1868]	@ movhi
	cmp	r9, #0
	beq	.L441
	ldr	r2, .L461
	mov	r1, #384
	strh	r1, [r2, #1868]	@ movhi
.L441:
	ldrh	r7, [r4, #1826]
	ldrh	r6, [r4, #1814]
	ldrh	r8, [r5, #16]
	smulbb	r7, r7, r3
	ldrh	fp, [r5, #20]
	smulbb	r3, r6, r3
	ldrh	r1, [r5, #18]
	mov	r0, fp
	strh	r8, [r4, #1872]	@ movhi
	strh	fp, [r4, #1878]	@ movhi
	uxth	r7, r7
	strh	r1, [r4, #1874]	@ movhi
	str	r1, [sp]
	strh	r3, [r4, #1870]	@ movhi
	smulbb	r3, r7, r8
	strh	r7, [r4, #1804]	@ movhi
	strh	r3, [r4, #1876]	@ movhi
	bl	Ftl_log2
	lsl	r3, fp, #9
	cmp	r6, #1024
	uxth	r3, r3
	strh	r3, [r4, #1882]	@ movhi
	it	hi
	ldrhi	r2, .L461
	lsr	r3, r3, #8
	strh	r3, [r4, #1884]	@ movhi
	ldrh	r3, [r5, #26]
	strh	r3, [r4, #1886]	@ movhi
	mul	r3, r6, r7
	str	r3, [r4, #1820]
	itt	hi
	uxtbhi	r3, r6
	strhhi	r3, [r2, #1866]	@ movhi
	mov	r10, r0
	ldrh	r5, [r4, #1866]
	ldr	r1, [sp]
	subs	r5, r6, r5
	strh	r0, [r4, #1880]	@ movhi
	ldrh	r0, [r4, #1868]
	muls	r5, r7, r5
	ldr	r3, .L461
	lsls	r0, r0, #3
	mul	r1, r1, fp
	str	r3, [sp]
	mul	r5, fp, r5
	mul	r5, r8, r5
	asrs	r5, r5, #11
	str	r5, [r4, #1888]
	bl	__aeabi_idiv
	ldr	r3, [sp]
	uxth	r0, r0
	cmp	r0, #4
	itet	ls
	movls	r2, #4
	strhhi	r0, [r3, #1892]	@ movhi
	strhls	r2, [r3, #1892]	@ movhi
	cmp	r9, #0
	beq	.L445
	mov	r3, #640
	strh	r3, [r4, #1868]	@ movhi
.L445:
	ldrh	r3, [r4, #1868]
	lsls	r6, r6, #6
	mov	r1, r7
	ldr	r5, .L461
	asr	r3, r3, r10
	add	r10, r10, #9
	asr	r6, r6, r10
	adds	r3, r3, #2
	strh	r3, [r4, #1894]	@ movhi
	uxth	r0, r6
	strh	r6, [r4, #1896]	@ movhi
	add	r6, r0, #8
	mul	r3, r7, r0
	ldrh	r0, [r4, #1892]
	str	r3, [r4, #1900]
	bl	__aeabi_uidiv
	cmp	r7, #1
	uxtah	r0, r6, r0
	ldr	r6, .L461
	it	eq
	addeq	r0, r0, #4
	str	r0, [r5, #1800]
	ldrh	r0, [r4, #1800]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #1800]
	ldr	r5, [r4, #1816]
	mov	r0, #2048
	ldrh	r7, [r4, #1878]
	str	r3, [r4, #1904]
	ldrh	r3, [r4, #1872]
	lsls	r5, r5, #2
	mov	r1, r7
	muls	r5, r3, r5
	ldrh	r3, [r4, #1880]
	adds	r3, r3, #9
	lsrs	r5, r5, r3
	adds	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, #1908]	@ movhi
	bl	__aeabi_idiv
	ldrh	r2, [r4, #1892]
	movs	r3, #0
	str	r3, [r4, #1912]
	adds	r3, r2, #3
	strh	r3, [r4, #1892]	@ movhi
	ldr	r3, [r4, #1900]
	adds	r1, r3, #3
	str	r1, [r4, #1900]
	strh	r0, [r4, #1910]	@ movhi
	ldrb	r0, [r4, #140]	@ zero_extendqisi2
	cbz	r0, .L448
	adds	r2, r2, #4
	adds	r3, r3, #5
	strh	r2, [r6, #1892]	@ movhi
	b	.L459
.L448:
	cmp	r1, #7
	bhi	.L449
	movs	r3, #8
.L459:
	str	r3, [r6, #1900]
.L449:
	ldrh	r2, [r4, #1812]
	movs	r3, #0
	strh	r3, [r4, #1916]	@ movhi
	movs	r0, #0
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	adds	r3, r3, #52
	add	r5, r3, r5, lsl #2
	cmp	r5, r7, lsl #9
	ittt	cc
	movcc	r2, #1
	ldrcc	r3, .L461
	strhcc	r2, [r3, #1916]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L462:
	.align	2
.L461:
	.word	.LANCHOR0
	.word	.LANCHOR0+1832
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
	ldr	r2, .L470
	ldrh	r3, [r2, #1918]
	cbz	r3, .L469
	ldr	r3, [r2, #1920]
	ldrh	r2, [r2, #1892]
	add	r2, r3, r2, lsl #1
.L465:
	cmp	r3, r2
	beq	.L469
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L465
	movs	r0, #1
	bx	lr
.L469:
	movs	r0, #0
	bx	lr
.L471:
	.align	2
.L470:
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
	ldr	r2, .L480
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r2, #1924]
	cbz	r3, .L479
	ldr	r5, [r2, #1928]
	movs	r6, #36
	movs	r2, #0
.L475:
	mla	r4, r6, r2, r5
	ldr	r4, [r4, #16]
	cmp	r4, r0
	bcc	.L474
	cmp	r4, r1
	bls	.L477
.L474:
	adds	r2, r2, #1
	cmp	r2, r3
	bne	.L475
.L479:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L477:
	movs	r0, #1
	pop	{r4, r5, r6, pc}
.L481:
	.align	2
.L480:
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
	ldr	r3, .L483
	ldr	r0, [r3, #1912]
	bx	lr
.L484:
	.align	2
.L483:
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
	ldr	r3, .L486
	ldr	r0, [r3, #1912]
	bx	lr
.L487:
	.align	2
.L486:
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
	ldr	r3, .L489
	ldr	r0, [r3, #1932]
	bx	lr
.L490:
	.align	2
.L489:
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
	ldr	r5, .L492
	ldrh	r4, [r5, #1870]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, lsl #2
	ldr	r4, [r1, #1964]
	movs	r1, #1
	subs	r3, r6, r3
	uxth	r3, r3
	and	r0, r3, #31
	lsrs	r7, r3, #5
	lsls	r1, r1, r0
	ldr	r0, [r4, r7, lsl #2]
	orrs	r1, r1, r0
	ldr	r0, .L492+4
	str	r1, [r4, r7, lsl #2]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	ldrh	r3, [r5, #1942]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r5, #1942]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L493:
	.align	2
.L492:
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
	ldr	r5, .L495
	ldrh	r6, [r5, #1870]
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
	ldr	r3, [r5, #1964]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L496:
	.align	2
.L495:
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
	ldr	r4, .L506
	ldrh	r6, [r4, #1870]
	ldrh	r3, [r4, #1826]
	mov	r4, r5
	muls	r6, r3, r6
.L499:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L505
	bl	FtlBbmIsBadBlock
	cbz	r0, .L500
	adds	r4, r4, #1
	uxth	r4, r4
.L500:
	adds	r5, r5, #1
	b	.L499
.L505:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L507:
	.align	2
.L506:
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
	ldr	r6, .L509
	mov	r7, r0
	ldrh	r5, [r6, #1828]
	mov	r1, r5
	bl	__aeabi_uidivmod
	mov	r0, r7
	smlabb	r4, r4, r5, r1
	mov	r1, r5
	bl	__aeabi_uidiv
	ldrh	r3, [r6, #1870]
	smlabb	r0, r3, r0, r4
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.L510:
	.align	2
.L509:
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
	ldr	r3, .L512
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r1, [r3, #1870]
	ldrh	r5, [r3, #1828]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r1, r4
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L513:
	.align	2
.L512:
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
	ldr	r4, .L515
	ldrh	r1, [r4, #1870]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #1828]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L516:
	.align	2
.L515:
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
	bls	.L518
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L518:
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
	ldr	r3, .L521
	ldrh	r0, [r3, #2002]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L522:
	.align	2
.L521:
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
	ldr	r3, .L524
	ldrh	r0, [r3, #2002]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L525:
	.align	2
.L524:
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
	subs	r3, r0, #1
	movw	r2, #65533
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	uxth	r3, r3
	cmp	r3, r2
	mov	r5, r0
	bhi	.L526
	ldr	r4, .L535
	ldrh	r3, [r4, #2002]
	cmp	r3, #1024
	beq	.L526
	cbz	r1, .L528
	ldr	r3, [r4, #4052]
	cbnz	r3, .L528
	bl	P2V_block_in_plane
	movs	r1, #1
	lsls	r3, r5, #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r4, #4056]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r4, #4060]
	ldrh	r3, [r2, r6, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #4064]
	adds	r3, r3, #1
	str	r3, [r4, #4064]
.L528:
	ldrh	r3, [r4, #2002]
	adds	r3, r3, #1
	strh	r3, [r4, #2002]	@ movhi
	ldrh	r3, [r4, #2000]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #2000]	@ movhi
	strh	r5, [r2, #2004]	@ movhi
.L526:
	pop	{r4, r5, r6, pc}
.L536:
	.align	2
.L535:
	.word	.LANCHOR0
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
	ldr	r3, .L548
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r2, r3, #4096
	ldrh	r4, [r2]
	ldrh	r2, [r3, #2002]
	cbz	r2, .L537
	movs	r0, #0
	ldrh	r1, [r3, #1998]
	ldrh	r2, [r3, #2000]
	and	r4, r4, #31
	mov	r6, r0
.L539:
	uxth	r5, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bge	.L547
	add	r5, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r5, #2004]
	add	r5, r3, r2, lsl #1
	strh	r6, [r5, #2004]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L539
.L547:
	cbz	r6, .L537
	strh	r1, [r3, #1998]	@ movhi
	strh	r2, [r3, #2000]	@ movhi
.L537:
	pop	{r4, r5, r6, pc}
.L549:
	.align	2
.L548:
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r4, .L560
	mov	r6, r4
.L551:
	ldrh	r1, [r4, #2002]
	cbz	r1, .L552
	ldrh	r3, [r4, #1998]
	subs	r1, r1, #1
	ldr	r8, [r4, #4052]
	strh	r1, [r4, #2002]	@ movhi
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #1998]	@ movhi
	ldrh	r5, [r2, #2004]
	cmp	r8, #0
	bne	.L553
	mov	r0, r5
	bl	P2V_block_in_plane
	lsls	r3, r5, #10
	mov	r7, r0
	ldr	r0, [r4, #4056]
	str	r3, [r0, #4]
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cbz	r3, .L554
	mov	r1, r8
	movs	r2, #1
	bl	FlashEraseBlocks
.L554:
	movs	r1, #1
	ldr	r0, [r6, #4056]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r2, [r6, #4060]
	ldrh	r3, [r2, r7, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	ldr	r3, [r6, #4064]
	adds	r3, r3, #1
	str	r3, [r6, #4064]
	b	.L553
.L552:
	ldr	r0, .L560+4
	bl	printk
.L555:
	b	.L555
.L553:
	subs	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L556
	mov	r1, r5
	ldrh	r2, [r6, #2002]
	ldr	r0, .L560+8
	bl	printk
	b	.L551
.L556:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L561:
	.align	2
.L560:
	.word	.LANCHOR0
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
	ldr	r3, .L567
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	movw	r5, #65535
	ldr	r4, [r3, #-228]
	ldr	r2, [r0]
	ldr	r3, .L567+4
	subs	r0, r2, r4
	asrs	r0, r0, #1
	muls	r3, r0, r3
	movs	r0, #6
	uxth	r3, r3
.L564:
	cmp	r1, r3
	beq	.L565
	ldrh	r3, [r2]
	cmp	r3, r5
	beq	.L566
	mla	r2, r0, r3, r4
	b	.L564
.L565:
	movs	r0, #1
	pop	{r4, r5, pc}
.L566:
	movs	r0, #0
	pop	{r4, r5, pc}
.L568:
	.align	2
.L567:
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
	ldr	r5, .L585
	ldrh	r3, [r5, #1812]
	cmp	r3, r0
	bls	.L571
	movs	r4, #6
	ldr	r2, .L585+4
	movw	r3, #65535
	muls	r4, r0, r4
	ldr	lr, [r2, #-228]
	mov	r7, r2
	add	r1, lr, r4
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [lr, r4]	@ movhi
	ldr	r3, [r2, #-224]
	cmp	r3, #0
	beq	.L584
	ldr	r8, [r2, #-220]
	lsl	fp, r0, #1
	ldrh	r2, [r1, #4]
	ldrh	r6, [r8, r0, lsl #1]
	cbz	r2, .L582
	muls	r6, r2, r6
	b	.L573
.L582:
	mov	r6, #-1
.L573:
	ldr	ip, [r7, #-228]
	ldr	r2, .L585+8
	rsb	r7, ip, r3
	ldr	r9, [r5, #4060]
	asrs	r7, r7, #1
	muls	r7, r2, r7
	uxth	r2, r7
	ldrh	r7, [r5, #1812]
	add	r5, r9, fp
	str	r5, [sp, #4]
	movs	r5, #0
	str	r7, [sp]
.L580:
	adds	r5, r5, #1
	ldr	r7, [sp]
	uxth	r5, r5
	cmp	r5, r7
	bhi	.L571
	cmp	r0, r2
	beq	.L571
	ldrh	r7, [r3, #4]
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	cbz	r7, .L583
	mul	r7, r7, fp
	b	.L575
.L583:
	mov	r7, #-1
.L575:
	cmp	r7, r6
	bne	.L576
	ldr	r7, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L578
	b	.L577
.L576:
	bhi	.L577
.L578:
	ldrh	r7, [r3]
	movw	r10, #65535
	cmp	r7, r10
	bne	.L579
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L585+4
	str	r1, [r3, #-216]
	b	.L571
.L579:
	movs	r3, #6
	mov	r2, r7
	mla	r3, r3, r7, ip
	b	.L580
.L577:
	strh	r2, [lr, r4]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L585+4
	ldr	r4, [r2, #-224]
	cmp	r3, r4
	bne	.L581
	strh	r0, [r3, #2]	@ movhi
.L584:
	str	r1, [r2, #-224]
	b	.L571
.L581:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #-228]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L571:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L586:
	.align	2
.L585:
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
	ldr	r2, .L588
	ldrh	r3, [r2, #-212]
	adds	r3, r3, #1
	strh	r3, [r2, #-212]	@ movhi
	pop	{r3, pc}
.L589:
	.align	2
.L588:
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
	beq	.L591
	movs	r1, #6
	ldr	r2, .L598
	mul	r7, r1, r0
	ldr	lr, [r2, #-228]
	mov	ip, r2
	add	r6, lr, r7
	strh	r5, [r6, #2]	@ movhi
	strh	r5, [lr, r7]	@ movhi
	ldr	r3, [r2, #-208]
	cbz	r3, .L597
	ldr	r2, .L598+4
	ldr	r8, [ip, #-228]
	rsb	r4, r8, r3
	ldr	r9, [r2, #4060]
	ldr	r2, .L598+8
	asrs	r4, r4, #1
	ldrh	r10, [r9, r0, lsl #1]
	muls	r4, r2, r4
	uxth	r2, r4
.L595:
	ldrh	r4, [r9, r2, lsl #1]
	cmp	r4, r10
	bcs	.L593
	ldrh	r4, [r3]
	cmp	r4, r5
	bne	.L594
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L591
.L594:
	mla	r3, r1, r4, r8
	mov	r2, r4
	b	.L595
.L593:
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	strh	r2, [lr, r7]	@ movhi
	ldr	r1, [ip, #-208]
	ldr	r2, .L598
	cmp	r3, r1
	bne	.L596
	strh	r0, [r3, #2]	@ movhi
.L597:
	str	r6, [r2, #-208]
	b	.L591
.L596:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #-228]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L591:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L599:
	.align	2
.L598:
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	ldr	r2, .L601
	ldrh	r3, [r2, #-204]
	adds	r3, r3, #1
	strh	r3, [r2, #-204]	@ movhi
	pop	{r3, pc}
.L602:
	.align	2
.L601:
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
	ldr	r7, .L608
	movw	r6, #65535
	muls	r1, r4, r1
	ldr	r3, [r0]
	ldr	r2, [r7, #-228]
	adds	r5, r2, r1
	cmp	r5, r3
	ldrh	r3, [r2, r1]
	bne	.L604
	cmp	r3, r6
	ittee	eq
	moveq	r3, #0
	streq	r3, [r0]
	mlane	r3, r4, r3, r2
	strne	r3, [r0]
	it	ne
	strhne	r6, [r3, #2]	@ movhi
	b	.L606
.L604:
	cmp	r3, r6
	ldrh	r0, [r5, #2]
	bne	.L607
	cmp	r0, r3
	beq	.L606
	muls	r0, r4, r0
	strh	r3, [r2, r0]	@ movhi
	b	.L606
.L607:
	mla	r3, r4, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r7, #-228]
	muls	r4, r6, r4
	strh	r0, [r3, r4]	@ movhi
.L606:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, pc}
.L609:
	.align	2
.L608:
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
	cbz	r3, .L616
	ldr	r2, .L617
	movs	r6, #6
	movw	r5, #65535
	ldr	r4, [r2, #-228]
.L612:
	cbnz	r1, .L613
.L615:
	subs	r4, r3, r4
	ldr	r3, .L617+4
	asrs	r4, r4, #1
	muls	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L613:
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L615
	subs	r1, r1, #1
	mla	r3, r6, r2, r4
	uxth	r1, r1
	b	.L612
.L616:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L618:
	.align	2
.L617:
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
	ldr	r2, .L626
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #-224]
	cbz	r3, .L624
	movs	r4, #6
	ldr	r1, [r2, #-228]
	movw	r2, #65535
.L621:
	cbz	r0, .L622
	ldrh	r3, [r3]
	cmp	r3, r2
	beq	.L624
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L621
.L624:
	movw	r0, #65535
	pop	{r4, pc}
.L622:
	subs	r3, r3, r1
	ldr	r0, .L626+4
	asrs	r3, r3, #1
	muls	r0, r3, r0
	uxth	r0, r0
	pop	{r4, pc}
.L627:
	.align	2
.L626:
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
	ldr	r4, .L635
	ldrh	r3, [r4, #-200]
	cmp	r3, r0
	beq	.L629
	ldrh	r3, [r4, #-152]
	cmp	r3, r0
	beq	.L629
	ldrh	r3, [r4, #-104]
	cmp	r3, r0
	beq	.L629
	movs	r3, #6
	ldr	r1, [r4, #-228]
	ldr	r2, [r4, #-224]
	muls	r3, r0, r3
	adds	r0, r1, r3
	cmp	r0, r2
	beq	.L629
	ldr	r6, [r4, #-220]
	ldrh	r2, [r0, #4]
	ldrh	r7, [r6, r5, lsl #1]
	cbz	r2, .L633
	muls	r2, r7, r2
	b	.L630
.L633:
	mov	r2, #-1
.L630:
	ldrh	r0, [r0, #2]
	movw	r7, #65535
	cmp	r0, r7
	bne	.L631
	ldrh	r3, [r1, r3]
	cmp	r3, r0
	beq	.L629
.L631:
	movs	r3, #6
	muls	r3, r0, r3
	ldr	r0, .L635+4
	asrs	r7, r3, #1
	add	r3, r3, r1
	muls	r0, r7, r0
	ldrh	r3, [r3, #4]
	ldrh	r0, [r6, r0, lsl #1]
	cbz	r3, .L634
	muls	r3, r0, r3
	b	.L632
.L634:
	mov	r3, #-1
.L632:
	cmp	r2, r3
	bcs	.L629
	ldr	r0, .L635+8
	mov	r1, r5
	bl	List_remove_node
	ldrh	r3, [r4, #-212]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #-212]	@ movhi
	bl	INSERT_DATA_LIST
.L629:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L636:
	.align	2
.L635:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-224
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
.L638:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L641
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L638
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	subs	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L639
	ldr	r3, .L644
	ldr	r0, .L644+4
	ldrh	r2, [r3, #2002]
	bl	printk
.L640:
	b	.L640
.L639:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L641:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L645:
	.align	2
.L644:
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
	ldr	r3, .L660
	movs	r4, #12
	ldr	r7, .L660+4
	movw	r5, #65535
	ldrh	r2, [r3, #1910]
	ldr	r3, [r7, #-56]
.L647:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L658
	adds	r1, r1, #1
	mla	r6, r4, r1, r3
	ldrh	r6, [r6, #-12]
	cmp	r6, r5
	bne	.L647
	b	.L648
.L658:
	movs	r1, #0
	mov	r6, #-2147483648
	mov	lr, #12
	mov	r0, r2
.L650:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L659
	mla	r4, lr, r1, r3
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L651
	cmp	r4, r6
	itt	cc
	movcc	r6, r4
	movcc	r0, r5
.L651:
	adds	r1, r1, #1
	b	.L650
.L659:
	cmp	r0, r2
	bcc	.L648
	ldrh	r7, [r7, #-52]
	movs	r1, #0
	mov	r4, #-1
	mov	r0, r2
.L653:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L648
	ldr	r6, [r3, #4]
	cmp	r6, r4
	bcs	.L654
	ldrh	lr, [r3]
	cmp	lr, r7
	itt	ne
	movne	r4, r6
	movne	r0, r5
.L654:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L653
.L648:
	pop	{r4, r5, r6, r7, pc}
.L661:
	.align	2
.L660:
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
	ldr	r1, .L669
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r2, [r1, #-50]
	mov	r3, r1
	cmp	r2, #4
	bhi	.L663
	cbnz	r0, .L663
	adds	r2, r2, #1
	strh	r2, [r1, #-50]	@ movhi
	pop	{r4, r5, pc}
.L663:
	ldr	r1, .L669+4
	movs	r2, #0
	strh	r2, [r3, #-50]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #-48]
	ldrh	r1, [r1, #1812]
	ldr	r2, [r3, #-220]
	add	r1, r2, r1, lsl #1
.L664:
	cmp	r2, r1
	beq	.L668
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #-48]
	addne	r0, r0, r4
	strne	r0, [r3, #-48]
	b	.L664
.L668:
	pop	{r4, r5, pc}
.L670:
	.align	2
.L669:
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
	ldr	r2, .L674
	and	r0, r0, #31
	uxth	r3, r3
	cbz	r1, .L672
	ldr	r1, [r2, #-44]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	orrs	r2, r2, r0
	str	r2, [r1, r3, lsl #2]
	bx	lr
.L672:
	ldr	r1, [r2, #-44]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	bic	r0, r0, r2
	str	r0, [r1, r3, lsl #2]
	bx	lr
.L675:
	.align	2
.L674:
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
	ldr	r3, .L677
	lsrs	r1, r0, #5
	ldr	r2, [r3, #-44]
	and	r3, r0, #31
	ldr	r0, [r2, r1, lsl #2]
	lsrs	r0, r0, r3
	and	r0, r0, #1
	bx	lr
.L678:
	.align	2
.L677:
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
	ldr	r3, .L688
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	adds	r4, r2, #7
	ldrh	r5, [r3, #1804]
	movw	r7, #65535
	add	r4, r0, r4, lsl #1
.L680:
	cmp	r2, r5
	bcs	.L686
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	cmp	r6, r7
	uxth	r2, r2
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L680
.L686:
	ldrh	r6, [r3, #1872]
	add	r4, r0, #14
	movs	r2, #0
	movw	r7, #65535
.L683:
	uxth	r3, r2
	cmp	r3, r5
	bcs	.L687
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
	b	.L683
.L687:
	pop	{r4, r5, r6, r7, pc}
.L689:
	.align	2
.L688:
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
	ldr	r6, .L703
	add	r7, r0, #16
	mov	r4, r0
	mov	r5, r3
	add	r10, r6, #1832
	movw	r9, #65535
	ldrh	r8, [r6, #1804]
	strh	r3, [r0, #4]	@ movhi
	strb	r3, [r0, #7]
.L691:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L702
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	ldrh	r1, [r4]
	bl	V2P_block
	strh	r9, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L692
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L692:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L691
.L702:
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r3, [r6, #1872]
	smulbb	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #1796]
	cbz	r3, .L694
	ldr	r3, .L703
	ldrh	r2, [r4]
	ldr	r3, [r3, #4060]
	ldrh	r3, [r3, r2, lsl #1]
	cmp	r3, #79
	itt	ls
	movls	r3, #1
	strbls	r3, [r4, #9]
.L694:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cbz	r3, .L695
	movs	r3, #1
	strb	r3, [r4, #9]
.L695:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L704:
	.align	2
.L703:
	.word	.LANCHOR0
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
	ldr	r3, .L714
	mov	r7, r0
	mov	r4, r5
	add	r6, r3, #1832
	ldrh	r8, [r3, #1804]
	ldrh	r9, [r3, #1872]
.L706:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L713
	mov	r1, r7
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L707
	add	r4, r4, r9
	uxth	r4, r4
.L707:
	adds	r5, r5, #1
	b	.L706
.L713:
	cbz	r4, .L709
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L709:
	ldr	r3, .L714+4
	movs	r2, #6
	movs	r0, #0
	ldr	r3, [r3, #-228]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L715:
	.align	2
.L714:
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
	ldr	r3, .L719
	ldr	r0, [r3, #-208]
	cbz	r0, .L717
	ldr	r3, [r3, #-228]
	subs	r0, r0, r3
	ldr	r3, .L719+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, .L719+8
	ldr	r3, [r3, #4060]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L717:
	bx	lr
.L720:
	.align	2
.L719:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR0
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
	ldr	r1, .L729
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r1, #-208]
	cbz	r3, .L727
	ldrh	r2, [r1, #-204]
	movs	r5, #6
	ldr	r1, [r1, #-228]
	movw	r6, #65535
	rsb	r2, r2, r2, lsl #3
	subs	r3, r3, r1
	asrs	r2, r2, #3
	asrs	r3, r3, #1
	cmp	r0, r2
	it	gt
	uxthgt	r0, r2
	ldr	r2, .L729+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L724:
	uxth	r4, r2
	cmp	r4, r0
	bcs	.L726
	mul	r4, r5, r3
	adds	r2, r2, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r6
	bne	.L728
.L726:
	ldr	r2, .L729+8
	ldr	r2, [r2, #4060]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, pc}
.L728:
	mov	r3, r4
	b	.L724
.L727:
	mov	r0, r3
	pop	{r4, r5, r6, pc}
.L730:
	.align	2
.L729:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR0
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	1
	.global	FtlPrintInfo2buf
	.thumb
	.thumb_func
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r7, .L742
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L742+4
	add	r5, r8, #12
	ldr	r1, .L742+8
	ldr	r3, [r7]
	str	r3, [sp, #44]
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L742+12
	ldr	r2, [r6, #112]
	bl	sprintf
	ldr	r1, .L742+16
	ldr	r2, [r6, #1888]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L742+20
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r5, r0
	bne	.L737
	add	r0, sp, #28
	add	r1, sp, #32
	add	r2, sp, #36
	add	r3, sp, #40
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L742+24
	ldr	r9, .L742+240
	ldr	r3, [sp, #36]
	ldr	r2, [sp, #28]
	str	r3, [sp]
	ldr	r3, [sp, #40]
	str	r3, [sp, #4]
	ldr	r3, [sp, #32]
	bl	sprintf
	ldr	r1, .L742+28
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L742+32
	ldr	r2, [r6, #1932]
	bl	sprintf
	ldr	r1, .L742+36
	ldr	r2, [r9, #-48]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+40
	ldr	r2, [r9, #-40]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+44
	ldr	r2, [r9, #-36]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+48
	ldr	r2, [r9, #-32]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+52
	ldr	r2, [r9, #-28]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+56
	ldr	r2, [r9, #-24]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+60
	ldr	r2, [r9, #-20]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r9, #-16]
	ldr	r1, .L742+64
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r9, #-12]
	ldr	r1, .L742+68
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+72
	ldr	r2, [r9, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+76
	ldr	r2, [r9, #-4]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldr	r1, .L742+80
	ldrh	r2, [r6, #1942]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+84
	ldrh	r2, [r9, #-204]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L742+88
	ldr	r2, [r9]
	adds	r4, r5, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+92
	ldr	r2, [r9, #4]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+96
	ldr	r2, [r9, #8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+100
	ldr	r2, [r6, #4064]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+104
	ldr	r2, [r9, #12]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+108
	ldr	r2, [r9, #16]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #4098
	ldrh	r2, [r6, r3]
	ldr	r1, .L742+112
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r3, r6, #4096
	ldr	r1, .L742+116
	ldrh	r2, [r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+120
	ldr	r2, [r6, #1912]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+124
	ldr	r2, [r6, #1904]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+128
	ldr	r2, [r6, #1800]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+132
	ldrh	r2, [r6, #2002]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+136
	ldrh	r2, [r6, #1812]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+140
	ldrh	r2, [r9, #20]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+144
	ldr	r2, [r6, #1816]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+148
	ldrh	r2, [r9, #24]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+152
	ldrh	r2, [r6, #1936]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+156
	ldrh	r2, [r9, #-198]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+160
	ldrb	r2, [r9, #-194]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+164
	ldrh	r2, [r9, #-200]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+168
	ldrb	r2, [r9, #-192]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+172
	ldrh	r2, [r9, #-196]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r9, #-220]
	ldrh	r2, [r9, #-200]
	ldr	r1, .L742+176
	ldrh	r2, [r3, r2, lsl #1]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+180
	ldrh	r2, [r9, #-150]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+184
	ldrb	r2, [r9, #-146]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+188
	ldrh	r2, [r9, #-152]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+192
	ldrb	r2, [r9, #-144]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+196
	ldrh	r2, [r9, #-148]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, [r9, #-220]
	ldrh	r2, [r9, #-152]
	ldr	r1, .L742+200
	ldrh	r2, [r3, r2, lsl #1]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+204
	ldrh	r2, [r9, #-102]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+208
	ldrb	r2, [r9, #-98]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+212
	ldrh	r2, [r9, #-104]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+216
	ldrb	r2, [r9, #-96]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+220
	ldrh	r2, [r9, #-100]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+224
	ldrh	r2, [r9, #42]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+228
	ldrb	r2, [r9, #46]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+232
	ldrh	r2, [r9, #40]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L742+236
	ldrb	r2, [r9, #48]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	b	.L743
.L744:
	.align	2
.L742:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC6
	.word	.LC7
	.word	.LC8
	.word	.LANCHOR1
	.word	.LC9
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
	.word	.LANCHOR2
.L743:
	bl	sprintf
	ldr	r1, .L745
	ldrh	r2, [r9, #44]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r9, #176]
	ldr	r2, [r9, #88]
	ldr	r3, [r6, #1796]
	orr	r2, r3, r2, lsl #8
	ldr	r3, [r9, #172]
	str	r1, [sp]
	add	r4, r4, r0
	ldr	r1, [r9, #168]
	mov	r0, r4
	str	r1, [sp, #4]
	ldr	r1, .L745+4
	bl	sprintf
	ldr	r1, .L745+8
	ldr	r2, [r9, #164]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L745+12
	ldr	r2, [r9, #188]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L745+16
	ldrh	r2, [r9, #604]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L745+20
	ldrh	r2, [r9, #606]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L745+24
	ldr	r2, [r9, #608]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L745+28
	ldrh	r2, [r9, #612]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L745+32
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r9, #-204]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L745+36
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r9, #40]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L734
	ldr	r2, [r9, #-220]
	mov	r0, r4
	ldr	r1, .L745+40
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
	add	r4, r4, r0
.L734:
	movs	r0, #0
	movs	r5, #0
	bl	List_get_gc_head_node
	mov	fp, #6
	movw	ip, #65535
	uxth	r3, r0
.L736:
	cmp	r3, ip
	str	ip, [sp, #20]
	beq	.L735
	ldr	r2, [r9, #-220]
	mov	r0, r4
	mul	r10, fp, r3
	ldr	r1, .L745+44
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp]
	ldr	r2, [r9, #-228]
	add	r2, r2, r10
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #4060]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r9, #-228]
	cmp	r5, #16
	ldrh	r3, [r3, r10]
	add	r4, r4, r0
	ldr	ip, [sp, #20]
	bne	.L736
.L735:
	ldr	r3, [r9, #-228]
	mov	r10, #0
	ldr	r5, [r9, #-208]
	movw	ip, #65535
	subs	r5, r5, r3
	ldr	r3, .L745+48
	asrs	r5, r5, #1
	muls	r3, r5, r3
	movs	r5, #6
	uxth	r3, r3
.L738:
	cmp	r3, ip
	str	ip, [sp, #20]
	beq	.L737
	mul	fp, r5, r3
	ldr	r2, [r9, #-228]
	mov	r0, r4
	ldr	r1, .L745+52
	add	r2, r2, fp
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	ldr	r2, [r6, #4060]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #4]
	mov	r2, r10
	bl	sprintf
	add	r10, r10, #1
	ldr	r3, [r9, #-228]
	cmp	r10, #4
	ldrh	r3, [r3, fp]
	add	r4, r4, r0
	ldr	ip, [sp, #20]
	bne	.L738
.L737:
	ldr	r2, [sp, #44]
	rsb	r0, r8, r4
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L739
	bl	__stack_chk_fail
.L739:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L746:
	.align	2
.L745:
	.word	.LC63
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
	ldr	r1, .L748
	ldr	r2, .L748+4
	bl	sprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L749:
	.align	2
.L748:
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
	ldr	r4, .L778
	ldr	r2, [r4, #8]
	ldr	r3, [r4, #16]
	cmp	r2, r3
	bcs	.L751
	ldr	r2, .L778+4
	movs	r3, #0
	str	r3, [r4]
	ldrh	r1, [r2, #1812]
	mov	r5, r2
	ldr	r0, [r2, #4060]
.L752:
	cmp	r3, r1
	bcs	.L777
	ldrh	r6, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r2, [r4]
	add	r2, r2, r6
	str	r2, [r4]
	b	.L752
.L777:
	ldr	r6, [r4]
	mov	r0, r6
	bl	__aeabi_uidiv
	ldrh	r1, [r5, #1864]
	str	r0, [r4, #8]
	ldr	r0, [r4, #4]
	subs	r0, r6, r0
	bl	__aeabi_uidiv
	str	r0, [r4]
	b	.L754
.L751:
	ldr	r3, [r4, #12]
	cmp	r2, r3
	bls	.L754
	ldr	r1, .L778+4
	adds	r3, r3, #1
	str	r3, [r4, #12]
	movs	r3, #0
.L756:
	ldrh	r2, [r1, #1812]
	cmp	r3, r2
	bcs	.L754
	ldr	r0, [r1, #4060]
	ldrh	r2, [r0, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r0, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L756
.L754:
	ldr	r6, [r4, #16]
	ldr	r8, [r4, #8]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L759
	ldr	r2, .L778
	add	r3, r6, #768
	ldr	r2, [r2, #12]
	cmp	r3, r2
	bls	.L759
	ldr	r3, .L778+4
	ldr	r3, [r3, #1796]
	cbnz	r3, .L760
.L762:
	movw	r0, #65535
	b	.L761
.L760:
	cmp	r6, #40
	bhi	.L762
.L759:
	ldrh	r0, [r4, #-204]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r9, r0
	bcs	.L763
	cmp	r6, #40
	bhi	.L762
.L763:
	ldr	r3, [r4, #-224]
	cmp	r3, #0
	beq	.L762
	ldr	r2, .L778+4
	movw	r7, #65535
	mov	ip, #6
	mov	r5, r7
	mov	lr, r7
	ldrh	r1, [r2, #1812]
	ldr	r10, [r2, #4060]
	str	r1, [sp, #20]
	ldr	r1, .L778
	ldr	r0, [r1, #-228]
	movs	r1, #0
.L764:
	ldrh	r2, [r3]
	cmp	r2, lr
	str	r2, [sp, #16]
	beq	.L766
	adds	r1, r1, #1
	ldr	r2, [sp, #20]
	uxth	r1, r1
	cmp	r1, r2
	bhi	.L762
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L765
	subs	r3, r3, r0
	ldr	r2, .L778+8
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r3, r3
	ldrh	fp, [r10, r3, lsl #1]
	cmp	fp, r6
	bls	.L770
	cmp	fp, r7
	itt	cc
	movcc	r7, fp
	movcc	r5, r3
.L765:
	ldr	r3, [sp, #16]
	mla	r3, ip, r3, r0
	b	.L764
.L770:
	mov	r5, r3
.L766:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L762
	ldrh	fp, [r10, r5, lsl #1]
	lsls	r3, r5, #1
	cmp	fp, r6
	bls	.L768
	str	r3, [sp, #16]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #16]
	cmp	r0, r6
	it	hi
	strhi	r7, [r4, #16]
.L768:
	cmp	fp, r8
	bcs	.L762
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L762
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L769
	ldr	r2, [r4, #12]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L762
.L769:
	ldr	r2, [r4, #-220]
	mov	r1, r5
	ldr	r0, .L778+12
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	str	r9, [sp, #8]
	str	r3, [sp, #4]
	ldr	r3, [r4, #12]
	bl	printk
	mov	r0, r5
	movs	r3, #1
	str	r3, [r4, #616]
.L761:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L779:
	.align	2
.L778:
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
	beq	.L781
	ldr	r2, .L782
	movs	r1, #0
	ldr	r2, [r2, #-220]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L781:
	movs	r0, #0
	pop	{r3, pc}
.L783:
	.align	2
.L782:
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
	ldr	r2, .L795
	mov	ip, #36
	ldr	r0, .L795+4
	movs	r3, #0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r7, #12
	mov	lr, #1
	mov	r6, r0
	mov	r4, r2
	str	r3, [r2, #620]
.L785:
	ldrh	r1, [r0, #1804]
	adds	r5, r3, #1
	uxth	r3, r3
	ldr	r8, .L795+4
	cmp	r3, r1
	bcs	.L793
	mul	r8, r7, r3
	ldr	r9, [r2, #624]
	add	r1, r9, r8
	str	lr, [r1, #8]
	ldrh	r1, [r0, #1882]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	bic	r10, r1, #3
	ldr	r1, [r4, #628]
	add	r1, r1, r10
	str	r1, [r9, r8]
	ldr	r10, [r2, #624]
	ldrh	r1, [r6, #1884]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	add	r9, r10, r8
	bic	fp, r1, #3
	ldr	r1, [r4, #632]
	add	r1, r1, fp
	str	r1, [r9, #4]
	ldr	r1, [r2, #636]
	mla	r3, ip, r3, r1
	ldr	r1, [r10, r8]
	str	r1, [r3, #8]
	ldr	r1, [r9, #4]
	str	r1, [r3, #12]
	mov	r3, r5
	b	.L785
.L793:
	ldr	r0, .L795
	mov	lr, #12
	movs	r5, #0
.L789:
	ldr	r3, [r2, #640]
	cmp	r1, r3
	bcs	.L794
	mul	r7, lr, r1
	ldr	r6, [r0, #624]
	ldr	r4, [r0, #628]
	adds	r3, r6, r7
	str	r5, [r3, #8]
	ldrh	r3, [r8, #1882]
	muls	r3, r1, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r4
	str	r3, [r6, r7]
	ldr	r3, [r2, #624]
	ldr	r6, [r0, #632]
	add	r7, r7, r3
	ldrh	r3, [r8, #1884]
	muls	r3, r1, r3
	add	r1, r1, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r1, r1
	add	r3, r3, r6
	str	r3, [r7, #4]
	b	.L789
.L794:
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L796:
	.align	2
.L795:
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
	ldr	r3, .L804
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	ldr	lr, [r3, #640]
	mov	r10, #36
	ldr	r6, [r3, #624]
	mov	r8, #12
	mov	ip, r5
.L798:
	uxth	r4, r5
	cmp	r4, r1
	bcs	.L797
	mla	r4, r10, r4, r0
	movs	r2, #0
.L799:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L800
	mul	r3, r8, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L799
	str	ip, [r9, #8]
.L800:
	adds	r5, r5, #1
	b	.L798
.L797:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L805:
	.align	2
.L804:
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
	ldr	r3, .L814
	mov	ip, #1
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	ldr	r5, [r3, #640]
	mov	lr, #12
	ldr	r6, [r3, #624]
	mov	r8, #36
.L807:
	uxth	r2, r4
	cmp	r2, r1
	bcs	.L813
	mov	r9, #0
.L808:
	uxth	r3, r9
	cmp	r3, r5
	bcs	.L809
	mla	r3, lr, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L808
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	ip, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L809:
	adds	r4, r4, #1
	b	.L807
.L813:
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L815:
	.align	2
.L814:
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
	ldr	r2, .L822
	ldr	r3, [r2, #644]
	ldrh	r2, [r2, #648]
	add	r2, r3, r2, lsl #1
.L817:
	cmp	r3, r2
	beq	.L821
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L817
	movs	r0, #1
	bx	lr
.L821:
	movs	r0, #0
	bx	lr
.L823:
	.align	2
.L822:
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
	ldr	r3, .L831
	movs	r4, #0
	ldrh	r1, [r3, #648]
	ldr	r2, [r3, #644]
.L825:
	uxth	r5, r4
	cmp	r5, r1
	bcs	.L829
	adds	r4, r4, #1
	add	lr, r2, r4, lsl #1
	ldrh	lr, [lr, #-2]
	cmp	lr, r0
	bne	.L825
.L829:
	cmp	r5, r1
	bne	.L827
	strh	r0, [r2, r5, lsl #1]	@ movhi
	ldrh	r0, [r3, #648]
	adds	r0, r0, #1
	strh	r0, [r3, #648]	@ movhi
.L827:
	ldrh	r2, [r3, #656]
	movs	r0, #12
	muls	r0, r2, r0
	ldr	r2, [r3, #652]
	adds	r1, r2, r0
	str	r7, [r1, #4]
	str	r8, [r1, #8]
	str	r6, [r2, r0]
	ldrh	r2, [r3, #656]
	adds	r2, r2, #1
	strh	r2, [r3, #656]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L832:
	.align	2
.L831:
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
	ldr	r4, .L840
	ldrh	r3, [r4, #658]
	cmp	r3, r0
	beq	.L835
	ldrh	r3, [r4, #660]
	cmp	r3, r0
	beq	.L835
	ldrh	r3, [r4, #662]
	cmp	r3, r0
	beq	.L835
	ldrh	r3, [r4, #664]
	cmp	r3, r0
	beq	.L835
	ldr	r0, .L840+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #658]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L837
	strh	r5, [r4, #658]	@ movhi
	b	.L835
.L837:
	ldrh	r2, [r4, #660]
	cmp	r2, r3
	bne	.L838
	strh	r5, [r4, #660]	@ movhi
	b	.L835
.L838:
	ldrh	r2, [r4, #662]
	cmp	r2, r3
	bne	.L839
	strh	r5, [r4, #662]	@ movhi
	b	.L835
.L839:
	ldrh	r2, [r4, #664]
	cmp	r2, r3
	it	eq
	strheq	r5, [r4, #664]	@ movhi
.L835:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L841:
	.align	2
.L840:
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
	ldr	r4, .L852
	ldrh	r3, [r4, #658]
	cmp	r3, r0
	beq	.L850
	ldrh	r3, [r4, #660]
	cmp	r3, r0
	beq	.L850
	ldrh	r3, [r4, #662]
	cmp	r3, r0
	beq	.L850
	ldrh	r3, [r4, #664]
	cmp	r3, r0
	beq	.L850
	ldr	r0, .L852+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #658]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L844
	strh	r5, [r4, #658]	@ movhi
	b	.L850
.L844:
	ldrh	r2, [r4, #660]
	cmp	r2, r3
	bne	.L845
	strh	r5, [r4, #660]	@ movhi
	b	.L850
.L845:
	ldrh	r2, [r4, #662]
	cmp	r2, r3
	bne	.L846
	strh	r5, [r4, #662]	@ movhi
	b	.L850
.L846:
	ldrh	r2, [r4, #664]
	cmp	r2, r3
	bne	.L851
	strh	r5, [r4, #664]	@ movhi
.L850:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L851:
	mov	r0, #-1
	pop	{r3, r4, r5, pc}
.L853:
	.align	2
.L852:
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
	mov	r4, r0
	bl	P2V_block_in_plane
	ldr	r6, .L863
	mov	r2, r4
	ldrh	r1, [r6, #666]
	mov	r5, r0
	ldr	r0, .L863+4
	bl	printk
	mov	r0, r5
	bl	FtlGcRefreshBlock
	ldr	r2, .L863+8
	mov	r3, r6
	ldr	r1, [r2, #1796]
	cbz	r1, .L855
	ldr	r1, [r2, #4060]
	ldrh	r2, [r1, r5, lsl #1]
	cmp	r2, #39
	itt	hi
	subhi	r2, r2, #40
	strhhi	r2, [r1, r5, lsl #1]	@ movhi
.L855:
	ldrh	r2, [r3, #666]
	movs	r1, #0
	ldr	r5, .L863+12
.L856:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L862
	adds	r1, r1, #1
	add	r0, r5, r1, lsl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r4
	bne	.L856
	b	.L857
.L862:
	cmp	r2, #15
	itttt	ls
	addls	r1, r2, #1
	strhls	r1, [r3, #666]	@ movhi
	addls	r3, r3, r2, lsl #1
	strhls	r4, [r3, #668]	@ movhi
.L857:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L864:
	.align	2
.L863:
	.word	.LANCHOR2
	.word	.LC80
	.word	.LANCHOR0
	.word	.LANCHOR2+668
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
	ldr	r4, .L871
	ldrh	r3, [r4, #666]
	cbz	r3, .L866
	ldrh	r1, [r4, #658]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L866
	ldrh	r2, [r4, #702]
	cmp	r2, r3
	itt	cs
	movcs	r3, #0
	strhcs	r3, [r4, #702]	@ movhi
	ldrh	r3, [r4, #702]
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #668]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #702]
	adds	r3, r3, #1
	strh	r3, [r4, #702]	@ movhi
.L866:
	movs	r0, #0
	pop	{r4, pc}
.L872:
	.align	2
.L871:
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
	cbnz	r0, .L875
	ldr	r3, .L904
.L876:
	strh	r0, [r3, r0, lsl #1]	@ movhi
	adds	r0, r0, #1
	cmp	r0, #256
	bne	.L876
.L880:
	movs	r1, #255
	ldr	r0, .L904+4
	mov	r2, #1024
	uxth	r4, r4
	bl	ftl_memset
	movs	r3, #0
	ldr	r1, .L904
	ldr	r0, .L904+8
	b	.L877
.L875:
	cmp	r0, #1
	bne	.L878
	ldr	r5, .L904
	movs	r3, #0
.L879:
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
	bne	.L879
	b	.L880
.L878:
	cmp	r0, #2
	bne	.L881
	ldr	r0, .L904
	movs	r3, #0
.L882:
	uxth	r2, r3
	lsls	r1, r2, #1
	subs	r1, r1, #1
	cmp	r2, #1
	it	hi
	movhi	r2, r1
	strh	r2, [r0, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #256
	bne	.L882
	b	.L880
.L881:
	cmp	r0, #3
	bne	.L883
	ldr	r5, .L904
	movs	r3, #0
.L884:
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
	bne	.L884
	b	.L880
.L883:
	cmp	r0, #4
	mov	r3, #0
	bne	.L885
	ldr	r2, .L904+12
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
.L886:
	tst	r3, #1
	ite	ne
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	adds	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L886
	b	.L880
.L885:
	cmp	r0, #5
	bne	.L887
	ldr	r2, .L904
.L888:
	strh	r3, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #16
	bne	.L888
	ldr	r2, .L904+16
.L889:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L889
	b	.L880
.L887:
	cmp	r0, #6
	bne	.L890
	ldr	r5, .L904
.L891:
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
	bne	.L891
	b	.L880
.L890:
	cmp	r0, #9
	bne	.L880
	ldr	r2, .L904+12
	movw	r1, #509
	strh	r3, [r2, #144]	@ movhi
	movs	r3, #1
	strh	r3, [r2, #146]	@ movhi
	movs	r3, #2
	strh	r3, [r2, #148]!	@ movhi
	movs	r3, #3
.L892:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L892
	b	.L880
.L877:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L903
	ldrh	r2, [r1, r3, lsl #1]
	adds	r3, r3, #1
	add	r5, r0, r2, lsl #1
	strh	r2, [r5, #704]	@ movhi
	b	.L877
.L903:
	pop	{r4, r5, r6, pc}
.L905:
	.align	2
.L904:
	.word	.LANCHOR0+144
	.word	.LANCHOR2+704
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+174
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
	ldr	r3, .L920
	ldr	r4, .L920+4
	ldr	r6, .L920+8
	ldrh	r0, [r3, #482]
	addw	fp, r4, #1556
	strb	r7, [r4, #1717]
	mov	r9, r4
	strb	r7, [r6, #1728]
	bl	FlashBlockAlignInit
	mov	r1, r7
	movs	r2, #8
	add	r0, r4, #1720
	bl	ftl_memset
	mov	r1, r7
	movs	r2, #32
	add	r0, r4, #656
	bl	ftl_memset
	addw	r0, r4, #1588
	mov	r1, r7
	movs	r2, #128
	bl	ftl_memset
	ldr	r5, [r4, #692]
	add	r8, r5, #1
.L908:
	mov	r0, r8
	add	r1, fp, r7, lsl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r10, .L920+12
	cbnz	r0, .L907
	ldrb	r3, [r9, #1717]	@ zero_extendqisi2
	add	r2, r9, r3, lsl #2
	str	r0, [r2, #656]
	adds	r2, r3, #1
	add	r3, r3, r9
	strb	r2, [r9, #1717]
	strb	r7, [r3, #1720]
.L907:
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L908
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	ldr	r2, .L920+4
	strb	r3, [r6, #1728]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L909
.L913:
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #1717]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #14]
	smulbb	r3, r2, r3
	strh	r3, [r6, #1730]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L909:
	ldr	r9, [r2, #4]
	movs	r7, #0
	mov	fp, r2
.L912:
	mov	r0, r8
	add	r1, r10, r7, lsl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cbnz	r0, .L910
	ldrb	r0, [r5, #13]	@ zero_extendqisi2
	ldrh	r3, [r5, #14]
	ldrb	r2, [fp, #1717]	@ zero_extendqisi2
	mul	r0, r9, r0
	and	r3, r3, #65280
	add	r1, fp, r2, lsl #2
	muls	r3, r0, r3
	str	r3, [r1, #656]
	ldrb	r0, [r5, #23]	@ zero_extendqisi2
	cbz	r0, .L911
	lsls	r3, r3, #1
	str	r3, [r1, #656]
.L911:
	adds	r3, r2, #1
	add	r2, r2, r4
	strb	r3, [r4, #1717]
	strb	r7, [r2, #1720]
.L910:
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L912
	b	.L913
.L921:
	.align	2
.L920:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1556
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
	ldr	r3, .L926
	movs	r5, #1
	ldr	r2, [r3, #692]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	ldr	r2, [r3, #4]
	smulbb	r2, r1, r2
	strh	r2, [r4, #4]	@ unaligned
	ldrb	r2, [r3, #1792]	@ zero_extendqisi2
	strb	r2, [r4, #7]
	ldr	r2, [r3, #1912]
	str	r2, [r4]	@ unaligned
	ldr	r2, [r3, #692]
	add	r3, r3, #1720
	ldrb	r0, [r3, #-3]	@ zero_extendqisi2
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	movs	r1, #32
	strb	r1, [r4, #8]
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	strb	r2, [r4, #9]
	movs	r2, #0
	strb	r2, [r4, #10]
.L923:
	uxtb	r1, r2
	cmp	r1, r0
	bcs	.L925
	ldrb	r1, [r2, r3]	@ zero_extendqisi2
	adds	r2, r2, #1
	lsl	r6, r5, r1
	ldrb	r1, [r4, #10]	@ zero_extendqisi2
	orrs	r1, r1, r6
	strb	r1, [r4, #10]
	b	.L923
.L925:
	pop	{r4, r5, r6, pc}
.L927:
	.align	2
.L926:
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
	ldr	r4, .L1032
	movs	r6, #0
	ldr	r5, .L1032+4
	movw	r3, #65535
	movs	r7, #12
	mov	r8, #36
	strh	r6, [r4, #1732]	@ movhi
	str	r6, [r4, #1736]
	str	r6, [r4, #88]
	str	r6, [r4, #-8]
	str	r6, [r4, #-4]
	str	r6, [r4, #-20]
	str	r6, [r4, #-32]
	str	r6, [r4, #-36]
	str	r6, [r4, #-28]
	str	r6, [r4, #-24]
	str	r6, [r4, #-40]
	str	r6, [r4]
	str	r6, [r4, #4]
	str	r6, [r5, #4064]
	str	r6, [r4, #12]
	str	r6, [r4, #16]
	str	r6, [r4, #1740]
	str	r6, [r4, #616]
	str	r3, [r4, #1744]
	str	r6, [r4, #1748]
	str	r6, [r4, #608]
	str	r6, [r4, #1752]
	strh	r3, [r4, #658]	@ movhi
	strh	r3, [r4, #660]	@ movhi
	strh	r3, [r4, #662]	@ movhi
	strh	r3, [r4, #664]	@ movhi
	movs	r3, #32
	strh	r6, [r4, #612]	@ movhi
	strh	r3, [r4, #604]	@ movhi
	movs	r3, #128
	strh	r6, [r4, #666]	@ movhi
	strh	r3, [r4, #606]	@ movhi
	strh	r6, [r4, #1756]	@ movhi
	strh	r6, [r4, #702]	@ movhi
	ldrh	r1, [r5, #1878]
	bl	__aeabi_idiv
	ldrh	r3, [r5, #1804]
	str	r6, [r5, #1924]
	lsls	r3, r3, #2
	cmp	r0, r3
	str	r0, [r4, #1760]
	ldrh	r0, [r5, #1876]
	it	hi
	strhi	r3, [r4, #1760]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #644]
	ldrh	r0, [r5, #1876]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #1804]
	mul	r6, r8, r6
	lsl	r9, r6, #3
	str	r0, [r4, #652]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #1764]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1768]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #1772]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r5, #4056]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #636]
	ldr	r0, [r4, #1760]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #1882]
	ldrh	r3, [r5, #1804]
	lsls	r3, r3, #1
	adds	r3, r3, #1
	str	r3, [r4, #640]
	str	r0, [r5, #1928]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1776]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1780]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1784]
	ldr	r0, [r4, #640]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #628]
	ldr	r0, [r4, #1760]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #1788]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1792]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1796]
	ldr	r0, [r4, #640]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #1804]
	ldrh	r6, [r5, #1884]
	muls	r6, r3, r6
	str	r0, [r4, #624]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1800]
	lsls	r0, r6, #3
	bl	ftl_malloc
	ldrh	r3, [r5, #1884]
	str	r0, [r4, #1804]
	ldr	r0, [r4, #640]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #1884]
	str	r0, [r4, #632]
	ldr	r0, [r4, #1760]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #1808]
	ldrh	r0, [r5, #1814]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #1812]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #1816]
	ldrh	r0, [r4, #1812]
	addw	r0, r0, #547
	lsrs	r0, r0, #9
	strh	r0, [r4, #1812]	@ movhi
	lsls	r0, r0, #9
	bl	ftl_malloc
	ldrh	r6, [r5, #1814]
	lsls	r6, r6, #1
	str	r0, [r4, #1820]
	adds	r0, r0, #32
	str	r0, [r5, #4060]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1824]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r5, #1900]
	lsls	r6, r6, #1
	str	r0, [r4, #-220]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1828]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1832]
	ldrh	r0, [r5, #1814]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-44]
	ldrh	r0, [r5, #1892]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r5, #1920]
	ldrh	r0, [r5, #1892]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #1836]
	ldrh	r0, [r5, #1892]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #1840]
	ldrh	r0, [r5, #1894]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5, #1894]
	movs	r1, #0
	lsls	r2, r2, #2
	str	r0, [r4, #1844]
	bl	ftl_memset
	ldrh	r6, [r5, #1908]
	lsls	r6, r6, #2
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1848]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, .L1032+8
	str	r0, [r4, #1852]
	ldr	r0, [r5, #1900]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #1856]
	ldrh	r0, [r5, #1910]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #1910]
	str	r0, [r4, #-56]
	ldrh	r0, [r5, #1882]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #1814]
	str	r0, [r4, #1860]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #1870]
	adds	r3, r3, #31
	asrs	r3, r3, #5
	strh	r3, [r4, #1864]	@ movhi
	str	r0, [r4, #-228]
	ldrh	r0, [r5, #1826]
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #1864]
	ldrh	r7, [r5, #1826]
	movs	r3, #1
	lsls	r2, r2, #2
	mov	r1, r2
	str	r0, [r5, #1964]
.L930:
	cmp	r3, r7
	bcs	.L1030
	ldr	r0, [r5, #1964]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r6, #4]!
	b	.L930
.L1030:
	ldr	r2, .L1032+12
	movs	r1, #0
.L932:
	cmp	r3, #8
	beq	.L1031
	add	r0, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r0, #28]
	b	.L932
.L1031:
	ldr	r2, [r4, #1828]
	ldr	r3, .L1032
	cbnz	r2, .L934
.L936:
	ldr	r1, .L1032+16
	ldr	r0, .L1032+20
	bl	printk
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1033:
	.align	2
.L1032:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+1964
	.word	.LANCHOR0+1936
	.word	.LANCHOR3
	.word	.LC81
.L934:
	ldr	r2, [r3, #1832]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1848]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1856]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #-56]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1860]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #-228]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r5, #1964]
	cmp	r2, #0
	beq	.L936
	ldr	r3, [r3, #-220]
	cmp	r3, #0
	beq	.L936
	ldr	r2, [r4, #644]
	ldr	r3, .L1034
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #652]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1764]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1772]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r5, #4056]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #636]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1768]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1776]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1780]
	cmp	r2, #0
	beq	.L936
	ldr	r3, [r3, #1784]
	cmp	r3, #0
	beq	.L936
	ldr	r2, [r4, #628]
	ldr	r3, .L1034
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1792]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1796]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #624]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1800]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1804]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #632]
	cmp	r2, #0
	beq	.L936
	ldr	r2, .L1034+4
	ldr	r1, [r2, #4060]
	cmp	r1, #0
	beq	.L936
	ldr	r3, [r3, #1816]
	cmp	r3, #0
	beq	.L936
	ldr	r3, [r2, #1920]
	cmp	r3, #0
	beq	.L936
	ldr	r3, .L1034
	ldr	r2, [r3, #1836]
	cmp	r2, #0
	beq	.L936
	ldr	r2, [r3, #1840]
	cmp	r2, #0
	beq	.L936
	ldr	r3, [r3, #1844]
	cmp	r3, #0
	beq	.L936
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1035:
	.align	2
.L1034:
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
	ldr	r3, .L1041
	mov	r0, r1
	mov	r5, r1
	movs	r1, #0
	movs	r6, #1
	movw	r7, #65535
	ldrh	r2, [r3, #1864]
	lsls	r2, r2, #2
	bl	ftl_memset
	subs	r2, r4, #2
	addw	r4, r4, #1022
.L1038:
	ldrh	r3, [r2, #2]!
	cmp	r3, r7
	beq	.L1036
	lsrs	r1, r3, #5
	cmp	r2, r4
	and	r3, r3, #31
	ldr	r0, [r5, r1, lsl #2]
	lsl	r3, r6, r3
	orr	r3, r3, r0
	str	r3, [r5, r1, lsl #2]
	bne	.L1038
.L1036:
	pop	{r3, r4, r5, r6, r7, pc}
.L1042:
	.align	2
.L1041:
	.word	.LANCHOR2
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
	ldr	r0, .L1044
	movw	r3, #65535
	movs	r1, #255
	movs	r2, #16
	addw	r0, r0, #1948
	strh	r3, [r0, #-12]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	ftl_memset
.L1045:
	.align	2
.L1044:
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
	ldr	r3, .L1047
	mov	r2, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	strh	r0, [r3, #1996]	@ movhi
	addw	r0, r3, #2004
	mov	r1, r4
	strh	r4, [r3, #1998]	@ movhi
	strh	r4, [r3, #2000]	@ movhi
	strh	r4, [r3, #2002]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L1048:
	.align	2
.L1047:
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
.L1050:
	ldrh	r2, [r4, #6]
	uxth	r3, r1
	cmp	r2, r3
	bls	.L1069
	ldr	r2, [r5, r3, lsl #2]
	movs	r0, #0
	ubfx	r2, r2, #10, #16
.L1051:
	ldrh	lr, [r4, #10]
	uxth	r3, r0
	cmp	lr, r3
	bls	.L1070
	ldrh	lr, [r7, r3, lsl #1]
	cmp	lr, r2
	bne	.L1052
	cbz	r2, .L1052
	ldrh	lr, [r6, r3, lsl #1]
	add	lr, lr, #1
	strh	lr, [r6, r3, lsl #1]	@ movhi
.L1052:
	adds	r0, r0, #1
	b	.L1051
.L1070:
	adds	r1, r1, #1
	b	.L1050
.L1069:
	ldr	r3, .L1073
	mov	r8, #0
	mov	r10, r8
	ldrh	r2, [r3, #1874]
	ldrh	r3, [r4]
	strh	r2, [r6, r3, lsl #1]	@ movhi
	mov	r2, r8
	ldrh	r9, [r6]
.L1055:
	ldrh	r3, [r4, #10]
	uxth	r5, r8
	cmp	r3, r5
	bls	.L1071
	ldrh	r3, [r6, r5, lsl #1]
	lsls	r1, r5, #1
	cmp	r9, r3
	bls	.L1056
	ldrh	r0, [r7, r5, lsl #1]
	add	fp, r7, r1
	cbnz	r0, .L1057
	b	.L1058
.L1056:
	cbnz	r3, .L1058
	ldrh	r0, [r7, r1]
	add	fp, r7, r1
	cbz	r0, .L1058
	mov	r5, r2
	b	.L1060
.L1057:
	cbnz	r3, .L1072
	mov	r9, r3
.L1060:
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	r10, [fp]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r2, r5
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1058:
	add	r8, r8, #1
	b	.L1055
.L1072:
	mov	r2, r5
	mov	r9, r3
	b	.L1058
.L1071:
	mov	r0, r2
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1074:
	.align	2
.L1073:
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
	ldr	r4, .L1079
	movs	r7, #0
	ldr	r5, .L1079+4
	mov	r6, r7
	ldr	r2, [r4, #1900]
	mov	r9, r4
	ldr	r0, [r5, #1832]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #1882]
	ldrh	r2, [r4, #1910]
	movs	r1, #255
	ldr	r0, [r5, #1860]
	muls	r2, r3, r2
	bl	ftl_memset
	mov	r3, r5
	movs	r5, #12
	mov	r2, r4
	movw	lr, #65535
.L1076:
	ldrh	ip, [r2, #1910]
	adds	r0, r7, #1
	uxth	r7, r7
	ldr	r1, .L1079
	cmp	ip, r7
	bls	.L1078
	mul	r1, r5, r7
	ldr	ip, [r3, #-56]
	add	r8, ip, r1
	str	r6, [r8, #4]
	strh	lr, [ip, r1]	@ movhi
	ldr	r4, [r3, #-56]
	add	ip, r4, r1
	ldrh	r1, [r9, #1882]
	muls	r1, r7, r1
	ldr	r7, [r3, #1860]
	bic	r1, r1, #3
	add	r1, r1, r7
	mov	r7, r0
	str	r1, [ip, #8]
	b	.L1076
.L1078:
	movw	r2, #65535
	strh	r2, [r3, #1870]	@ movhi
	strh	r2, [r3, #1868]	@ movhi
	ldr	r2, [r1, #1900]
	strh	r2, [r3, #1878]	@ movhi
	movw	r2, #61634
	strh	r2, [r3, #1872]	@ movhi
	ldrh	r2, [r3, #1912]
	strh	r2, [r3, #1876]	@ movhi
	ldrh	r2, [r1, #1908]
	strh	r2, [r3, #1874]	@ movhi
	ldr	r2, [r3, #1828]
	str	r2, [r3, #1880]
	ldr	r2, [r3, #1856]
	str	r2, [r3, #1884]
	ldr	r2, [r3, #1832]
	str	r2, [r3, #1888]
	ldr	r2, [r3, #1848]
	str	r2, [r3, #1892]
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1080:
	.align	2
.L1079:
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
	ldr	r5, .L1082
	movw	r3, #65535
	ldr	r6, .L1082+4
	mov	r1, r4
	ldrh	r2, [r5, #1892]
	ldr	r0, [r5, #1920]
	strh	r3, [r6, #1920]	@ movhi
	mov	r3, #-1
	lsls	r2, r2, #1
	str	r3, [r6, #1932]
	str	r4, [r6, #1916]
	str	r4, [r6, #1924]
	str	r4, [r6, #1928]
	str	r4, [r5, #1796]
	strh	r4, [r5, #1918]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r5, #1814]
	mov	r1, r4
	ldr	r0, [r5, #4060]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #1814]
	mov	r1, r4
	ldr	r0, [r6, #1816]
	lsls	r2, r2, #1
	bl	ftl_memset
	mov	r1, r4
	addw	r0, r5, #4068
	movs	r2, #48
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	add	r0, r6, #92
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1083:
	.align	2
.L1082:
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
	ldr	r6, .L1095
	movs	r5, #0
	ldr	r4, .L1095+4
	.pad #20
	sub	sp, sp, #20
	movs	r1, #0
	mov	r9, r5
	ldrh	r3, [r6, #1814]
	mov	r10, r5
	ldr	r0, [r4, #-228]
	mov	r8, r6
	muls	r2, r3, r2
	bl	ftl_memset
	str	r5, [r4, #-208]
	str	r5, [r4, #-224]
	str	r5, [r4, #-216]
	strh	r5, [r4, #-212]	@ movhi
	strh	r5, [r4, #-204]	@ movhi
	strh	r5, [r4, #1732]	@ movhi
.L1085:
	uxth	fp, r5
	ldrh	r2, [r8, #1812]
	sxth	r7, fp
	cmp	r7, r2
	bge	.L1092
	ldr	r3, .L1095
	movs	r2, #0
	mov	r6, r2
	ldrh	ip, [r3, #1804]
	ldrh	r3, [r3, #1872]
	str	r3, [sp, #4]
.L1093:
	sxth	r1, r2
	cmp	r1, ip
	bge	.L1094
	add	r1, r1, r8
	str	r2, [sp, #12]
	str	ip, [sp, #8]
	ldrb	r0, [r1, #1832]	@ zero_extendqisi2
	mov	r1, fp
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	ip, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L1086
	ldr	r3, [sp, #4]
	add	r6, r6, r3
	uxth	r6, r6
.L1086:
	adds	r2, r2, #1
	b	.L1093
.L1094:
	cbz	r6, .L1088
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1089
.L1088:
	ldr	r1, [r4, #-220]
	sxth	r2, fp
	movw	r0, #65535
	strh	r0, [r1, r2, lsl #1]	@ movhi
.L1089:
	add	r0, r7, r7, lsl #1
	ldr	r1, [r4, #-228]
	ldr	r2, .L1095+4
	add	r1, r1, r0, lsl #1
	strh	r6, [r1, #4]	@ movhi
	ldrh	r1, [r4, #-200]
	cmp	r7, r1
	beq	.L1090
	ldrh	r1, [r2, #-152]
	cmp	r7, r1
	beq	.L1090
	ldrh	r1, [r2, #-104]
	cmp	r7, r1
	beq	.L1090
	ldr	r2, [r2, #-220]
	ldrh	r2, [r2, r7, lsl #1]
	cbnz	r2, .L1091
	add	r9, r9, #1
	mov	r0, fp
	uxth	r9, r9
	bl	INSERT_FREE_LIST
	b	.L1090
.L1091:
	add	r10, r10, #1
	mov	r0, fp
	uxth	r10, r10
	bl	INSERT_DATA_LIST
.L1090:
	adds	r5, r5, #1
	b	.L1085
.L1092:
	movs	r0, #0
	strh	r10, [r4, #-212]	@ movhi
	strh	r9, [r4, #-204]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1096:
	.align	2
.L1095:
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
	ldr	r5, .L1098
	movs	r3, #0
	ldr	r4, .L1098+4
	ldrh	r2, [r5, #1876]
	ldr	r0, [r4, #644]
	strh	r3, [r4, #648]	@ movhi
	lsls	r2, r2, #1
	strh	r3, [r4, #656]	@ movhi
	bl	ftl_memset
	ldrh	r3, [r5, #1876]
	movs	r2, #12
	ldr	r0, [r4, #652]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r3, r4, r5, lr}
	b	FtlGcBufInit
.L1099:
	.align	2
.L1098:
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
	ldr	r1, .L1102
	bl	ftl_memcpy
	movs	r0, #0
	pop	{r3, pc}
.L1103:
	.align	2
.L1102:
	.word	.LANCHOR2+1936
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
	ldr	r5, .L1116
.L1108:
	mov	r0, r5
	ldr	r1, .L1116+4
	ldrb	r2, [r5, #-1]	@ zero_extendqisi2
	bl	FlashMemCmp8
	mov	r6, r0
	cbnz	r0, .L1105
	ldr	r5, .L1116+8
	lsls	r4, r4, #5
	add	r2, r5, #508
	adds	r7, r2, r4
	beq	.L1111
	add	r4, r4, r5
	mov	r3, r0
	addw	r1, r5, #3100
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	b	.L1110
.L1105:
	adds	r4, r4, #1
	adds	r5, r5, #32
	cmp	r4, #81
	bne	.L1108
	b	.L1111
.L1115:
	adds	r3, r3, #1
	cmp	r3, #4
	beq	.L1109
.L1110:
	lsls	r0, r3, #5
	ldrb	r0, [r0, r1]	@ zero_extendqisi2
	cmp	r0, r2
	bne	.L1115
.L1109:
	ldr	r1, .L1116+12
	movs	r2, #32
	ldr	r0, .L1116+16
	add	r1, r1, r3, lsl #5
	bl	ftl_memcpy
	mov	r1, r7
	movs	r2, #32
	ldr	r0, .L1116+20
	bl	ftl_memcpy
	ldrh	r0, [r5, #482]
	bl	FlashBlockAlignInit
	b	.L1106
.L1111:
	mov	r6, #-1
.L1106:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L1117:
	.align	2
.L1116:
	.word	.LANCHOR1+509
	.word	.LANCHOR0+1556
	.word	.LANCHOR1
	.word	.LANCHOR1+3100
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
	bne	.L1119
	cbz	r3, .L1120
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1120:
	cbz	r6, .L1118
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
.L1119:
	cbz	r3, .L1123
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1123:
	cbz	r6, .L1118
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
.L1118:
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
.L1135:
	cmp	r3, r2
	beq	.L1137
	ldr	r4, [r1, r3, lsl #2]
	str	r4, [r0, r3, lsl #2]
	adds	r3, r3, #1
	b	.L1135
.L1137:
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
	ldr	r3, .L1140
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1141:
	.align	2
.L1140:
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
	ldr	r4, .L1145
	ldr	r4, [r4, #3984]
	add	r4, r4, #4096
	add	r1, r1, r4
	cbz	r2, .L1144
	mov	r0, r1
	mov	r1, r5
.L1144:
	mov	r2, r3
	pop	{r4, r5, lr}
	b	ftl_memcpy
.L1146:
	.align	2
.L1145:
	.word	.LANCHOR2
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
	ldr	r3, .L1151
	ldr	r0, [r3, #1912]
	bx	lr
.L1152:
	.align	2
.L1151:
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
.L1157:
	cmp	r4, r8
	beq	.L1165
	cbnz	r5, .L1158
	ldr	r0, .L1166
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1158:
	cmp	r7, #4
	ldr	r0, .L1166+4
	bne	.L1159
	ldr	r1, [r6, r4, lsl #2]
	b	.L1164
.L1159:
	cmp	r7, #2
	ite	eq
	ldrsheq	r1, [r6, r4, lsl #1]
	ldrbne	r1, [r6, r4]	@ zero_extendqisi2
.L1164:
	adds	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1162
	ldr	r0, .L1166+8
	movs	r5, #0
	ldr	r1, .L1166+12
	bl	printk
.L1162:
	adds	r4, r4, #1
	b	.L1157
.L1165:
	ldr	r0, .L1166+8
	ldr	r1, .L1166+12
	pop	{r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1167:
	.align	2
.L1166:
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
	ldr	r7, .L1277
	mov	r3, #172
	mov	r1, #173
	mov	r2, #174
	.pad #44
	sub	sp, sp, #44
	mov	r4, r0
	strb	r3, [r7, #702]
	mov	r3, #175
	strb	r0, [r7, #698]
	strb	r1, [r7, #703]
	strb	r2, [r7, #704]
	strb	r3, [r7, #705]
	bne	.L1169
	movs	r3, #167
	strb	r3, [r7, #702]
	ldr	r3, .L1277+4
	movs	r2, #247
	movs	r5, #7
	strb	r2, [r3, #3245]
	b	.L1230
.L1169:
	cmp	r0, #3
	bne	.L1171
	movs	r3, #176
	strb	r3, [r7, #702]
	movs	r3, #177
	strb	r3, [r7, #703]
	movs	r3, #178
	strb	r3, [r7, #704]
	movs	r3, #179
	strb	r3, [r7, #705]
	movs	r3, #180
	strb	r3, [r7, #706]
	movs	r3, #181
	strb	r3, [r7, #707]
	movs	r3, #182
	strb	r3, [r7, #708]
	movs	r3, #183
	b	.L1271
.L1171:
	cmp	r0, #4
	bne	.L1172
	movs	r0, #204
	strb	r1, [r7, #707]
	strb	r0, [r7, #702]
	movs	r0, #191
	strb	r2, [r7, #708]
	strb	r0, [r7, #703]
	movs	r0, #170
	strb	r0, [r7, #704]
	movs	r0, #171
	strb	r0, [r7, #705]
	movs	r0, #205
	strb	r0, [r7, #706]
.L1271:
	movs	r5, #8
	strb	r3, [r7, #709]
	mov	r6, r5
	b	.L1170
.L1172:
	cmp	r0, #5
	bne	.L1173
	movs	r3, #56
	movs	r5, #8
	strb	r3, [r7, #702]
	movs	r3, #57
	strb	r3, [r7, #703]
	movs	r3, #58
	strb	r3, [r7, #704]
	movs	r3, #59
	strb	r3, [r7, #705]
	b	.L1230
.L1173:
	cmp	r0, #6
	bne	.L1174
	movs	r3, #14
	movs	r5, #12
	strb	r3, [r7, #702]
	movs	r3, #15
	strb	r3, [r7, #703]
	movs	r3, #16
	strb	r3, [r7, #704]
	movs	r3, #17
	strb	r3, [r7, #705]
	b	.L1230
.L1174:
	cmp	r0, #7
	bne	.L1175
	movs	r3, #176
	movs	r5, #12
	strb	r3, [r7, #702]
	movs	r6, #10
	movs	r3, #177
	strb	r3, [r7, #703]
	movs	r3, #178
	strb	r3, [r7, #704]
	movs	r3, #179
	strb	r3, [r7, #705]
	movs	r3, #180
	strb	r3, [r7, #706]
	movs	r3, #181
	strb	r3, [r7, #707]
	movs	r3, #182
	strb	r3, [r7, #708]
	movs	r3, #183
	strb	r3, [r7, #709]
	movs	r3, #212
	strb	r3, [r7, #710]
	movs	r3, #213
	strb	r3, [r7, #711]
	b	.L1170
.L1175:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1230
	movs	r3, #6
	strb	r5, [r7, #703]
	strb	r3, [r7, #702]
	movs	r5, #50
	movs	r3, #9
	strb	r0, [r7, #704]
	strb	r3, [r7, #705]
	movs	r6, #5
	movs	r3, #10
	strb	r3, [r7, #706]
	b	.L1170
.L1230:
	movs	r6, #4
.L1170:
	subs	r3, r4, #1
	cmp	r3, #1
	bhi	.L1268
	ldr	r1, .L1277+8
	mov	r10, #0
.L1176:
	ldrb	r3, [r7, #1717]	@ zero_extendqisi2
	uxtb	r8, r10
	ldr	r2, .L1277
	cmp	r3, r8
	bls	.L1183
	add	r8, r8, r2
	addw	fp, r2, #701
	ldrb	r3, [r8, #1720]	@ zero_extendqisi2
	add	r4, r2, r3, lsl #6
	add	r3, r2, r3, lsl #3
	addw	r4, r4, #718
	movs	r2, #55
	ldrb	r8, [r3, #16]	@ zero_extendqisi2
	ldr	r0, [r3, #12]
	mov	r9, r4
	mov	r3, r4
	add	r8, r0, r8, lsl #8
	addw	ip, r8, #2056
.L1178:
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
	bcc	.L1178
	movs	r2, #0
.L1179:
	movs	r3, #1
	add	lr, r1, r2
.L1180:
	ldrb	ip, [lr, r3, lsl #2]	@ zero_extendqisi2
	ldrb	r0, [r9]	@ zero_extendqisi2
	add	r0, r0, ip
	strb	r0, [r9, r3, lsl #3]
	adds	r3, r3, #1
	cmp	r3, #7
	bne	.L1180
	adds	r2, r2, #1
	add	r9, r9, #1
	cmp	r2, #4
	bne	.L1179
	movs	r3, #0
	add	r10, r10, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1176
.L1268:
	subs	r3, r4, #3
	cmp	r3, #5
	bhi	.L1183
	smulbb	r2, r6, r5
	lsls	r3, r2, #4
	asrs	r2, r2, #1
	str	r3, [sp, #36]
	lsls	r3, r2, #1
	str	r3, [sp, #8]
	movs	r3, #0
.L1275:
	str	r3, [sp, #20]
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r3, .L1277
	ldr	r2, [sp, #16]
	ldrb	r3, [r3, #1717]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1228
.L1183:
	ldr	r3, .L1277
	strb	r6, [r3, #699]
	strb	r5, [r3, #700]
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1228:
	ldr	r2, [sp, #16]
	ldr	r3, .L1277
	add	r3, r3, r2
	ldrb	fp, [r3, #1720]	@ zero_extendqisi2
	ldr	r3, .L1277
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
	bne	.L1185
	ldr	r2, .L1277+12
	movs	r3, #160
	mla	ip, r3, fp, r2
	add	r3, ip, #28
	b	.L1272
.L1185:
	cmp	r4, #8
	beq	.L1187
	ldr	r2, .L1277+12
	add	ip, r2, fp, lsl #6
	add	r3, ip, #20
.L1272:
	add	r7, r7, r10
	cmp	r4, #4
	str	r3, [sp, #24]
	mov	r3, #54
	str	r3, [r7, #2056]
	bne	.L1188
	movs	r3, #255
	str	r3, [r7, #2052]
	movs	r3, #64
	str	r3, [r7, #2048]
	movs	r3, #204
	b	.L1273
.L1188:
	subs	r3, r4, #5
	cmp	r3, #1
	bhi	.L1190
	ldr	r3, .L1277
	ldrb	r2, [r3, #702]	@ zero_extendqisi2
	ldr	r3, [sp, #4]
	add	r3, r10, r3, lsl #8
	str	r2, [r3, #2052]
	movs	r2, #82
	str	r2, [r3, #2048]
	b	.L1189
.L1190:
	cmp	r4, #7
	bne	.L1189
	movs	r3, #174
	str	r3, [r7, #2052]
	movs	r3, #0
	str	r3, [r7, #2048]
	movs	r3, #176
.L1273:
	str	r3, [r7, #2052]
	movs	r3, #77
	str	r3, [r7, #2048]
.L1189:
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
.L1229:
	ldr	r3, [sp, #4]
	movs	r2, #48
	mov	r0, fp
	add	r3, r10, r3, lsl #8
	str	r2, [r3, #2056]
	bl	NandcWaitFlashReady
	subs	r3, r4, #5
	cmp	r3, #1
	str	r3, [sp, #28]
	bls	.L1233
	cmp	r4, #8
	beq	.L1233
	cmp	r4, #7
	ite	ne
	movne	r2, #2
	moveq	r2, #32
	b	.L1193
.L1278:
	.align	2
.L1277:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3228
	.word	.LANCHOR0+698
.L1233:
	movs	r2, #16
.L1193:
	ldr	r3, .L1279
	ldr	r1, [sp, #4]
	ldr	r3, [r3, #3988]
	add	r0, r10, r1, lsl #8
	mov	r7, r3
.L1194:
	ldr	r1, [r0, #2048]
	strb	r1, [r7], #1
	subs	r1, r7, r3
	uxtb	r1, r1
	cmp	r1, r2
	bcc	.L1194
	cmp	r4, #8
	bne	.L1195
	movs	r2, #0
.L1197:
	ldrb	r7, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r7, #50
	beq	.L1196
	add	r7, r3, r2, lsl #2
	ldrb	r7, [r7, #1]	@ zero_extendqisi2
	cmp	r7, #5
	beq	.L1196
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1197
	b	.L1198
.L1196:
	cmp	r1, #7
	bne	.L1199
.L1198:
	ldr	r0, .L1279+4
	movs	r1, #0
	bl	printk
.L1200:
	b	.L1200
.L1195:
	cmp	r4, #7
	bne	.L1201
	movs	r2, #0
.L1203:
	ldrb	r7, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r7, #12
	beq	.L1202
	add	r7, r3, r2, lsl #2
	ldrb	r7, [r7, #1]	@ zero_extendqisi2
	cmp	r7, #10
	beq	.L1202
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1203
	b	.L1204
.L1202:
	cmp	r1, #7
	bne	.L1199
.L1204:
	ldr	r0, .L1279+4
	movs	r1, #0
	bl	printk
.L1205:
	b	.L1205
.L1201:
	cmp	r4, #6
	bne	.L1199
	subs	r2, r3, #1
	adds	r3, r3, #7
.L1206:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1199
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1199
	cmp	r2, r3
	bne	.L1206
	ldr	r0, .L1279+4
	movs	r1, #0
	bl	printk
.L1208:
	b	.L1208
.L1199:
	ldr	r1, .L1279
	ldr	r7, [sp, #36]
	ldr	r2, [r1, #3988]
	adds	r7, r2, r7
	mov	r3, r2
.L1209:
	cmp	r3, r7
	beq	.L1276
	ldr	lr, [r0, #2048]
	strb	lr, [r3], #1
	b	.L1209
.L1276:
	ldr	r3, [r1, #3988]
	movs	r0, #8
	ldr	r1, [sp, #8]
	adds	r1, r3, r1
	str	r1, [sp, #32]
.L1212:
	ldr	r7, [sp, #8]
	add	lr, r1, r7
.L1211:
	ldrh	r7, [r1]
	mvns	r7, r7
	strh	r7, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L1211
	ldr	r7, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, r7
	bne	.L1212
	str	r3, [sp, #12]
.L1213:
	movs	r7, #0
	mov	r0, r7
.L1216:
	movs	r3, #1
	mov	lr, #0
	mov	r8, #16
	lsl	r1, r3, r0
	mov	ip, lr
.L1214:
	ldr	r3, [sp, #12]
	ldrh	r9, [r3, lr]
	ldr	r3, [sp, #8]
	and	r9, r9, r1
	cmp	r9, r1
	it	eq
	addeq	ip, ip, #1
	subs	r8, r8, #1
	add	lr, lr, r3
	bne	.L1214
	cmp	ip, #8
	add	r0, r0, #1
	itt	hi
	orrhi	r7, r7, r1
	uxthhi	r7, r7
	cmp	r0, #16
	bne	.L1216
	ldr	r3, [sp, #12]
	strh	r7, [r3], #2	@ movhi
	str	r3, [sp, #12]
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #32]
	cmp	r1, r3
	bne	.L1213
	ldr	r3, .L1279
	ldr	r1, [r3, #3988]
	movs	r3, #0
	subs	r0, r1, #4
	add	r7, r1, #28
.L1218:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	it	eq
	addeq	r3, r3, #1
	cmp	r0, r7
	bne	.L1218
	cmp	r3, #7
	ble	.L1219
	ldr	r0, .L1279+8
	movs	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1279+4
	movs	r1, #0
	bl	printk
.L1220:
	b	.L1220
.L1219:
	cmp	r4, #6
	beq	.L1235
	cmp	r4, #7
	beq	.L1236
	cmp	r4, #8
	ite	ne
	movne	r0, #8
	moveq	r0, #5
	b	.L1221
.L1235:
	movs	r0, #4
	b	.L1221
.L1236:
	movs	r0, #10
.L1221:
	add	r9, r6, #-1
	ldr	r1, [sp, #24]
	movs	r7, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L1222:
	mov	ip, r1
	mov	r3, r2
.L1223:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [ip], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L1223
	adds	r7, r7, #1
	add	r2, r2, r9
	cmp	r7, r5
	add	r1, r1, r0
	blt	.L1222
	ldr	r3, [sp, #4]
	mov	r0, fp
	add	r7, r10, r3, lsl #8
	movs	r3, #255
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #28]
	cmp	r3, #1
	bhi	.L1225
	movs	r3, #54
	str	r3, [r7, #2056]
	ldr	r3, .L1279+12
	mov	r1, #-1
	ldr	r0, [sp, #16]
	ldrb	r3, [r3, #702]	@ zero_extendqisi2
	str	r3, [r7, #2052]
	movs	r3, #0
	str	r3, [r7, #2048]
	movs	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L1226
.L1225:
	cmp	r4, #8
	ite	eq
	moveq	r3, #190
	movne	r3, #56
	str	r3, [r7, #2056]
.L1226:
	mov	r0, fp
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	b	.L1275
.L1187:
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
	ldr	r3, .L1279+16
	str	r3, [sp, #24]
	b	.L1229
.L1280:
	.align	2
.L1279:
	.word	.LANCHOR2
	.word	.LC85
	.word	.LC86
	.word	.LANCHOR0
	.word	.LANCHOR0+726
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
	beq	.L1281
	subs	r2, r0, #1
	cmp	r2, #7
	bhi	.L1283
	b	HynixGetReadRetryDefault
.L1283:
	cmp	r0, #49
	bne	.L1284
	ldr	r0, .L1304
	movs	r2, #64
	ldr	r1, .L1304+4
	addw	r0, r0, #702
	strb	r3, [r0, #-4]
	movs	r3, #4
	strb	r3, [r0, #-3]
	movs	r3, #15
	strb	r3, [r0, #-2]
	b	.L1302
.L1284:
	cmp	r0, #33
	beq	.L1285
	sub	r2, r0, #65
	cmp	r2, #1
	bhi	.L1286
.L1285:
	ldr	r0, .L1304
	strb	r3, [r0, #698]
	movs	r3, #4
	b	.L1303
.L1286:
	cmp	r0, #34
	beq	.L1287
	cmp	r0, #67
	bne	.L1288
.L1287:
	ldr	r0, .L1304
	strb	r3, [r0, #698]
	movs	r3, #5
.L1303:
	strb	r3, [r0, #699]
	movs	r2, #45
	movs	r3, #7
	addw	r0, r0, #702
	strb	r3, [r0, #-2]
	ldr	r1, .L1304+8
	b	.L1302
.L1288:
	cmp	r0, #35
	beq	.L1289
	cmp	r0, #68
	bne	.L1281
.L1289:
	ldr	r0, .L1304
	movs	r2, #95
	ldr	r1, .L1304+12
	addw	r0, r0, #702
	strb	r3, [r0, #-4]
	movs	r3, #5
	strb	r3, [r0, #-3]
	movs	r3, #17
	strb	r3, [r0, #-2]
.L1302:
	b	ftl_memcpy
.L1281:
	bx	lr
.L1305:
	.align	2
.L1304:
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
	ldr	r5, .L1345
	add	r0, r5, r0, lsl #3
	ldr	r3, [r5, #1740]
	ldr	r4, [r0, #12]
	cmp	r3, #3
	bls	.L1337
	ldr	r3, [r4, #16]
	lsls	r6, r3, #29
	bpl	.L1337
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	str	r3, [sp]
	cmp	r6, #0
	beq	.L1319
	movs	r6, #0
.L1309:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1317
	ldr	r3, [r5, #1740]
	cmp	r3, #5
	bhi	.L1310
.L1313:
	adds	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1309
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1345+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1345+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1309
.L1310:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r0, r3, #18
	bpl	.L1313
	ldr	r3, [sp, #4]
	lsls	r1, r3, #14
	bpl	.L1313
.L1317:
	ldr	r3, [r5, #1776]
	ldr	r4, .L1345
	cmp	r3, #0
	beq	.L1318
	ldr	r1, [sp]
	movs	r2, #0
	ldr	r0, [r4, #1768]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #1772]
	movs	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
	b	.L1318
.L1321:
	ldr	r3, [r4, #8]
	adds	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cbnz	r3, .L1319
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1345+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1345+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1319:
	ldr	r3, [sp]
	lsls	r2, r3, #11
	bpl	.L1321
	ldr	r3, [r5, #1784]
	cbz	r3, .L1322
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1322:
	ldr	r3, [r5, #1776]
	ldr	r6, .L1345
	cbz	r3, .L1323
	ldr	r1, [sp]
	movs	r2, #1
	ldr	r0, [r6, #1768]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #1772]
	movs	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
.L1323:
	ldr	r3, [r5, #1784]
	cbz	r3, .L1318
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1318:
	movs	r3, #0
	str	r3, [r5, #1776]
	b	.L1306
.L1337:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	lsls	r3, r3, #11
	bpl	.L1337
.L1306:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, pc}
.L1346:
	.align	2
.L1345:
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
	@ args = 4, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r3
	ldr	r3, .L1392
	mov	r7, r2
	.pad #100
	sub	sp, sp, #100
	ldr	r4, .L1392+4
	tst	r10, #63
	mov	r9, r0
	ldr	r2, [r3]
	mov	fp, r1
	ldr	r5, [sp, #136]
	str	r3, [sp, #16]
	str	r2, [sp, #92]
	add	r2, r4, r0, lsl #3
	ldr	r8, [r2, #12]
	bne	.L1348
	cbnz	r5, .L1349
	add	r0, sp, #28
	movs	r1, #255
	movs	r2, #64
	bl	ftl_memset
	add	r5, sp, #28
.L1349:
	mov	r0, r9
	str	r10, [sp]
	str	r5, [sp, #4]
	mov	r1, fp
	mov	r2, r7
	movs	r3, #0
	bl	NandcXferStart
	mov	r0, r9
	bl	NandcXferComp
	cmp	fp, #0
	bne	.L1376
	ldr	r3, [r4, #1788]
	lsrs	r1, r7, #1
	mov	r2, fp
	cmp	r3, #25
	mov	r3, fp
	ite	cc
	movcc	r6, #64
	movcs	r6, #128
.L1352:
	cmp	r2, r1
	add	r5, r5, #4
	add	lr, r3, r6
	bcs	.L1390
	ldr	r0, [r4, #1756]
	lsrs	r3, r3, #2
	adds	r2, r2, #1
	ldr	r3, [r0, r3, lsl #2]
	lsrs	r0, r3, #8
	strb	r3, [r5, #-4]
	strb	r0, [r5, #-3]
	lsrs	r0, r3, #16
	lsrs	r3, r3, #24
	strb	r0, [r5, #-2]
	strb	r3, [r5, #-1]
	mov	r3, lr
	b	.L1352
.L1390:
	movs	r2, #0
	ldr	r1, [r4, #1788]
	ldr	r0, [r4, #1740]
	lsrs	r7, r7, #2
	mov	r6, r2
.L1354:
	cmp	r2, r7
	bcs	.L1350
	cmp	r1, #0
	beq	.L1350
	add	r3, r2, #8
	ldr	r3, [r8, r3, lsl #2]
	str	r3, [sp, #24]
	ldr	r3, [sp, #24]
	lsls	r4, r3, #29
	bmi	.L1379
	ldr	r4, [sp, #24]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1379
	cmp	r0, #5
	bls	.L1356
	ldr	r4, [sp, #24]
	ldr	r5, [sp, #24]
	ldr	r3, [sp, #24]
	ubfx	r4, r4, #3, #5
	ubfx	lr, r5, #27, #1
	ldr	r5, [sp, #24]
	ubfx	r3, r3, #16, #5
	ubfx	r5, r5, #29, #1
	orr	r4, r4, lr, lsl #5
	orr	r3, r3, r5, lsl #5
	cmp	r4, r3
	ldr	r3, [sp, #24]
	ittee	hi
	ldrhi	r4, [sp, #24]
	ubfxhi	r3, r3, #3, #5
	ldrls	r4, [sp, #24]
	ubfxls	r3, r3, #16, #5
	ite	hi
	ubfxhi	r4, r4, #27, #1
	ubfxls	r4, r4, #29, #1
	b	.L1389
.L1356:
	cmp	r0, #3
	bls	.L1358
	ldr	r4, [sp, #24]
	ldr	r5, [sp, #24]
	ldr	r3, [sp, #24]
	ubfx	r4, r4, #3, #5
	ubfx	lr, r5, #28, #1
	ldr	r5, [sp, #24]
	ubfx	r3, r3, #16, #5
	ubfx	r5, r5, #30, #1
	orr	r4, r4, lr, lsl #5
	orr	r3, r3, r5, lsl #5
	cmp	r4, r3
	bls	.L1359
	ldr	r3, [sp, #24]
	ldr	r4, [sp, #24]
	ubfx	r3, r3, #3, #5
	ubfx	r4, r4, #28, #1
.L1389:
	orr	r4, r3, r4, lsl #5
	b	.L1358
.L1359:
	ldr	r5, [sp, #24]
	ldr	r4, [sp, #24]
	ubfx	r5, r5, #16, #5
	ubfx	r4, r4, #30, #1
	orr	r4, r5, r4, lsl #5
.L1358:
	cmp	r6, r4
	it	cc
	movcc	r6, r4
	b	.L1355
.L1379:
	mov	r6, #-1
.L1355:
	adds	r2, r2, #1
	b	.L1354
.L1376:
	movs	r6, #0
.L1350:
	movs	r3, #0
	str	r3, [r8, #16]
	b	.L1361
.L1348:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1388
	mov	r6, r4
.L1362:
	cmp	r4, r7
	bcs	.L1391
	and	ip, r4, #3
	cmp	r10, #0
	beq	.L1382
	add	r3, r10, r4, lsl #9
	b	.L1364
.L1382:
	mov	r3, r10
.L1364:
	cmp	r5, #0
	mov	r0, r8
	ite	ne
	movne	r2, #2
	moveq	r2, #0
	movs	r1, #1
	str	ip, [sp, #12]
	mla	r2, r4, r2, r5
	adds	r4, r4, #2
	str	r2, [sp]
	mov	r2, ip
	bl	NandcCopy1KB
	mov	r0, r9
	movs	r1, #1
	movs	r2, #2
	ldr	ip, [sp, #12]
	str	r6, [sp]
	str	r6, [sp, #4]
	mov	r3, ip
	bl	NandcXferStart
	mov	r0, r9
	bl	NandcXferComp
	b	.L1362
.L1391:
	movs	r6, #0
	b	.L1361
.L1388:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	movs	r2, #2
	mov	r3, r4
	mov	r6, r4
	bl	NandcXferStart
	str	r10, [sp, #12]
.L1367:
	cmp	r4, r7
	bcs	.L1361
	mov	r0, r9
	bl	NandcXferComp
	ldr	r3, [r8, #32]
	add	ip, r4, #2
	cmp	ip, r7
	str	r3, [sp, #24]
	bcs	.L1368
	movs	r3, #0
	mov	r0, r9
	str	r3, [sp]
	movs	r2, #2
	str	r3, [sp, #4]
	mov	r1, r3
	and	r3, ip, #3
	str	ip, [sp, #20]
	bl	NandcXferStart
	ldr	ip, [sp, #20]
.L1368:
	ldr	r3, [sp, #24]
	lsls	r3, r3, #29
	bmi	.L1384
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #24]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, lsl #5
	cmp	r6, r3
	it	cc
	movcc	r6, r3
	b	.L1369
.L1384:
	mov	r6, #-1
.L1369:
	cmp	r10, #0
	ldr	r3, [sp, #12]
	it	eq
	moveq	r3, #0
	cmp	r5, #0
	ite	ne
	movne	r1, #2
	moveq	r1, #0
	sub	r2, ip, #2
	mov	r0, r8
	mla	r4, r4, r1, r5
	and	r2, r2, #3
	movs	r1, #0
	str	ip, [sp, #20]
	str	r4, [sp]
	bl	NandcCopy1KB
	ldr	ip, [sp, #20]
	ldr	r3, [sp, #12]
	mov	r4, ip
	add	r3, r3, #1024
	str	r3, [sp, #12]
	b	.L1367
.L1361:
	ldr	r3, .L1392+4
	ldr	r3, [r3, #1740]
	cmp	r3, #5
	bls	.L1373
	cmp	fp, #0
	bne	.L1373
	ldr	r3, [r8]
	and	r2, r3, #139264
	cmp	r2, #139264
	ittt	eq
	moveq	r6, #-1
	orreq	r3, r3, #131072
	streq	r3, [r8]
.L1373:
	ldr	r3, [sp, #16]
	mov	r0, r6
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1375
	bl	__stack_chk_fail
.L1375:
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1393:
	.align	2
.L1392:
	.word	__stack_chk_guard
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
	ldr	r3, .L1396
	mov	r6, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r5, [r3, #481]	@ zero_extendqisi2
	cbnz	r0, .L1395
	ldr	r2, .L1396+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	muls	r2, r3, r2
	cmp	r1, r2
	it	cc
	movcc	r5, #4
.L1395:
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
.L1397:
	.align	2
.L1396:
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
	ldr	r4, .L1424
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #76]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #1740]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	ite	cc
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	beq	.L1412
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
	ldrb	r0, [r4, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #1716]	@ zero_extendqisi2
	bl	NandcSetMode
	adds	r3, r6, #1
	bne	.L1401
.L1410:
	mov	r6, #-1
	b	.L1402
.L1401:
	mov	r2, r6
	ldr	r0, .L1424+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	itt	ls
	addls	r4, r4, r10, lsl #3
	ldrls	r3, [r4, #12]
	ldr	r4, .L1424+8
	itttt	ls
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #3992]
	adds	r3, r3, #1
	str	r3, [r4, #3992]
	cmp	r3, #2048
	bcc	.L1402
	movs	r7, #0
	str	r7, [r4, #3992]
	mov	fp, r7
	b	.L1400
.L1412:
	mov	r6, #1024
.L1400:
	movs	r4, #0
	mov	r8, #-1
	str	r4, [sp, #4]
	mov	ip, r4
	mov	r5, r4
.L1408:
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
	bhi	.L1404
	cmp	r0, #2
	bhi	.L1414
	adds	r5, r5, #1
	cmp	r5, #9
	bls	.L1414
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1406
.L1404:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1415
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1416
	str	r5, [sp, #4]
	b	.L1415
.L1414:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1405
.L1415:
	movs	r5, #0
.L1405:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1408
.L1406:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	it	cs
	movcs	r4, ip
	b	.L1407
.L1416:
	mov	r4, ip
.L1407:
	cbz	r4, .L1409
	ldr	r0, .L1424+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1409:
	cmp	r8, #0
	beq	.L1402
	ldr	r0, .L1424+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1410
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1402:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1425:
	.align	2
.L1424:
	.word	.LANCHOR0
	.word	.LC90
	.word	.LANCHOR2
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
	bne	.L1427
	ldr	r9, .L1446+4
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1428
.L1430:
	ldrb	r3, [r10, #1732]	@ zero_extendqisi2
	ldr	r9, .L1446+4
	cbz	r3, .L1427
	b	.L1445
.L1428:
	movs	r3, #0
	mov	r0, r5
	strb	r3, [r9, #8]
	mov	r1, r6
	mov	r3, r7
	mov	r2, r8
	bl	FlashReadRawPage
	strb	fp, [r9, #8]
	adds	r3, r0, #1
	beq	.L1430
	mov	r4, r0
	b	.L1427
.L1445:
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
	beq	.L1431
	ldrb	r3, [r9, #1792]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1427
.L1431:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1427:
	ldr	r9, .L1446+8
	ldr	ip, [r9, #3996]
	cmp	ip, #0
	beq	.L1432
	adds	r2, r4, #1
	bne	.L1432
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r3, r6
	mov	r2, r5
	mov	r4, r0
	ldr	r0, .L1446
	mov	r1, r4
	bl	printk
	adds	r3, r4, #1
	bne	.L1432
	ldr	r3, .L1446+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbz	r3, .L1432
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	r4, [r9, #3996]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	r4
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1432:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1447:
	.align	2
.L1446:
	.word	.LC93
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r4, .L1458
	mov	r6, r1
	movs	r5, #0
	ldrb	r0, [r4, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #1716]	@ zero_extendqisi2
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
	beq	.L1449
	cmp	r8, #-1
	bne	.L1450
.L1449:
	ldrb	r3, [r4, #1716]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L1450
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1458
	movs	r2, #0
	strb	r2, [r3, #1732]
	b	.L1451
.L1450:
	movs	r3, #1
	strb	r3, [r4, #1732]
.L1451:
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1459:
	.align	2
.L1458:
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
	ldr	r10, .L1476+24
	movs	r6, #0
	ldr	r7, .L1476
	mov	r9, #4
	ldr	r5, .L1476+4
	mov	r8, #-1
	ldr	r3, [r10]
	mov	r4, r6
	str	r6, [r5, #4004]
	str	r3, [sp, #12]
	ldr	r3, .L1476+8
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r7, #4]
	str	r0, [sp, #8]	@ unaligned
	mov	r0, r6
	str	r3, [sp]
	mov	r6, r5
	ldr	r3, [r5, #3988]
	str	r3, [r5, #4000]
	bl	flash_enter_slc_mode
	str	r10, [sp, #4]
.L1461:
	add	fp, r4, #1
	mov	r10, #0
.L1463:
	add	r3, sp, #8
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #3988]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1462
	movs	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #3988]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1462
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1464
	b	.L1463
.L1465:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	bne	.L1472
	ldr	r8, .L1476+28
	add	r1, fp, #160
	movs	r2, #32
	add	r0, r8, #472
	bl	ftl_memcpy
	ldr	r1, [r6, #4000]
	movs	r2, #32
	ldr	r0, .L1476+12
	adds	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #4000]
	mov	r2, #852
	ldr	r0, .L1476+16
	adds	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r8, #482]
	bl	FlashBlockAlignInit
	ldr	r8, [r6, #4000]
	str	r4, [r6, #4004]
	mov	r0, r4
	ldr	r1, [r7, #4]
	ldr	r3, [r8, #1076]
	strb	r3, [r7, #1732]
	bl	__aeabi_uidiv
	adds	r0, r0, #1
	cmp	r0, #1
	itee	hi
	strhi	r0, [r6, #4008]
	movls	r3, #2
	strls	r3, [r6, #4008]
	ldrh	r3, [r8, #14]
	mov	r8, #0
	strb	r3, [r5, #4012]
.L1464:
	ldr	r3, [sp]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1461
	mov	r0, r9
.L1475:
	bl	flash_exit_slc_mode
	mov	r0, r8
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1471
	bl	__stack_chk_fail
.L1462:
	ldr	fp, [r5, #4000]
	ldr	r2, .L1476+20
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1464
	cmp	r8, #0
	bne	.L1465
	mov	r0, r4
	ldr	r1, [r7, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L1476+4
	adds	r0, r0, #1
	str	r0, [r3, #4008]
	mov	r0, r8
	b	.L1475
.L1472:
	mov	r8, #-1
	b	.L1464
.L1471:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1477:
	.align	2
.L1476:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+11
	.word	.LANCHOR0+44
	.word	.LANCHOR0+698
	.word	1312902724
	.word	__stack_chk_guard
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
	ldr	r4, .L1506
	add	r3, r4, r9, lsl #3
	ldrb	r2, [r3, #16]	@ zero_extendqisi2
	ldr	r7, [r3, #12]
	ldrb	r3, [r4, #696]	@ zero_extendqisi2
	add	r6, r2, #8
	subs	r3, r3, #67
	add	r6, r7, r6, lsl #8
	cmp	r3, #1
	lsl	r3, r2, #8
	str	r3, [sp, #12]
	bls	.L1495
	ldrb	r5, [r4, #1732]	@ zero_extendqisi2
	cbz	r5, .L1496
	movs	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	movs	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
	b	.L1480
.L1496:
	str	r5, [sp, #4]
.L1480:
	ldr	r3, [sp, #12]
	movs	r1, #92
	adds	r3, r7, r3
	str	r1, [r3, #2056]
	movs	r1, #197
	str	r1, [r3, #2056]
	b	.L1479
.L1495:
	movs	r3, #0
	str	r3, [sp, #4]
.L1479:
	mov	r8, #1
	mov	r10, #-1
	lsls	r3, r2, #8
	str	r3, [sp, #20]
.L1481:
	ldr	r3, .L1506+4
	ldrb	r3, [r3, #4013]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r8, r3
	bcs	.L1505
	ldrb	r3, [r4, #696]	@ zero_extendqisi2
	mov	r0, r6
	uxtb	r1, r8
	subs	r3, r3, #67
	cmp	r3, #1
	bhi	.L1482
	bl	SandiskSetRRPara
	b	.L1483
.L1482:
	bl	ToshibaSetRRPara
.L1483:
	ldrb	r3, [r4, #696]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1484
	ldr	r3, .L1506+4
	ldrb	r3, [r3, #4013]	@ zero_extendqisi2
	subs	r3, r3, #3
	cmp	r8, r3
	itttt	eq
	moveq	r2, #179
	ldreq	r3, [sp, #20]
	addeq	r3, r7, r3
	streq	r2, [r3, #2056]
.L1484:
	ldr	r3, [sp, #12]
	movs	r2, #38
	adds	r3, r7, r3
	str	r2, [r3, #2056]
	movs	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cbz	r3, .L1485
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
	b	.L1486
.L1485:
	mov	r0, r9
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1486:
	adds	r2, r5, #1
	beq	.L1487
	ldrb	r2, [r4, #1792]	@ zero_extendqisi2
	cmp	r10, #-1
	it	eq
	moveq	r10, r5
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1489
	mov	fp, #0
	str	fp, [sp, #8]
.L1487:
	add	r8, r8, #1
	b	.L1481
.L1505:
	mov	r5, r10
.L1489:
	ldrb	r2, [r4, #696]	@ zero_extendqisi2
	mov	r0, r6
	movs	r1, #0
	subs	r2, r2, #67
	cmp	r2, #1
	bhi	.L1491
	bl	SandiskSetRRPara
	b	.L1492
.L1491:
	bl	ToshibaSetRRPara
.L1492:
	ldr	r3, [sp, #12]
	movs	r2, #255
	add	r7, r7, r3
	str	r2, [r7, #2056]
	ldrb	r2, [r4, #1792]	@ zero_extendqisi2
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1493
	adds	r3, r5, #1
	it	ne
	movne	r5, #256
.L1493:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cbz	r3, .L1494
	movs	r0, #4
	bl	NandcSetDdrMode
.L1494:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1507:
	.align	2
.L1506:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r5, .L1521
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
.L1509:
	ldr	r3, .L1521+4
	ldrb	r3, [r3, #4013]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r10, r3
	bcs	.L1512
	mov	r0, r6
	uxtb	r1, r10
	bl	SamsungSetRRPara
	mov	r2, r9
	mov	r0, r8
	mov	r1, fp
	mov	r3, r7
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1510
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1515
	movs	r7, #0
	mov	r9, r7
.L1510:
	add	r10, r10, #1
	b	.L1509
.L1515:
	mov	r4, r0
.L1512:
	mov	r0, r6
	movs	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L1514
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
.L1514:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1522:
	.align	2
.L1521:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r3, .L1547
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	mov	fp, r2
	str	r1, [sp, #20]
	ldrb	r5, [r3, #1792]	@ zero_extendqisi2
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbnz	r3, .L1524
	add	r5, r5, r5, lsl #1
	ubfx	r5, r5, #2, #8
	b	.L1525
.L1524:
	ldr	r2, .L1547+4
	smull	r2, r3, r5, r2
	uxtb	r5, r3
.L1525:
	ldr	r3, .L1547
	mov	r10, #0
	add	r3, r3, r6, lsl #3
	str	r3, [sp, #24]
.L1535:
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
.L1526:
	ldr	r3, .L1547+8
	ldrb	r3, [r3, #4013]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L1529
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
	beq	.L1527
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	cmp	r0, r5
	ldr	ip, [sp, #28]
	bcc	.L1537
	mov	r8, ip
	mov	fp, ip
.L1527:
	ldr	r9, [sp, #8]
	b	.L1526
.L1537:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L1529:
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
	bcc	.L1531
	adds	r1, r4, #1
	ldr	r0, .L1547+12
	it	ne
	movne	r4, #256
	mov	r1, r9
	str	r4, [sp]
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L1533
	ldr	r3, .L1547
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbz	r3, .L1542
	adds	r2, r4, #1
	bne	.L1542
	mov	r0, r6
	movs	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L1535
.L1533:
	mov	r0, r6
	mov	r1, r7
	bl	micron_auto_read_calibration_config
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
	b	.L1542
.L1531:
	cmp	r10, #0
	beq	.L1542
	mov	r0, r6
	mov	r1, r7
	bl	micron_auto_read_calibration_config
	mov	r4, #256
.L1542:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1548:
	.align	2
.L1547:
	.word	.LANCHOR0
	.word	1431655766
	.word	.LANCHOR2
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
	ldr	r5, .L1566
	mov	r10, r2
	mov	r7, r0
	mov	fp, r1
	adds	r2, r5, r0
	mov	r6, #-1
	ldr	r3, [r5, #692]
	ldrb	r4, [r2, #710]	@ zero_extendqisi2
	ldrb	r9, [r5, #700]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #7
	cmp	r3, #1
	it	ls
	ldrbls	r4, [r2, #718]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L1551:
	cmp	ip, r9
	bcs	.L1555
	adds	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1566+4
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
	beq	.L1553
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	cmp	r6, #-1
	it	eq
	moveq	r6, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1560
	mov	r8, #0
	mov	r10, r8
.L1553:
	add	ip, ip, #1
	b	.L1551
.L1560:
	mov	r6, r0
.L1555:
	ldr	r3, [r5, #692]
	add	r7, r7, r5
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	ite	ls
	strbls	r4, [r7, #718]
	strbhi	r4, [r7, #710]
	add	r3, r3, r3, lsl #1
	cmp	r6, r3, asr #2
	bcc	.L1559
	adds	r3, r6, #1
	it	ne
	movne	r6, #256
.L1559:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1567:
	.align	2
.L1566:
	.word	.LANCHOR0
	.word	.LANCHOR0+702
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
	ldr	r3, .L1571
	mov	r5, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r6, [r3, #481]	@ zero_extendqisi2
	cbnz	r0, .L1569
	ldr	r2, .L1571+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	muls	r1, r3, r1
	cmp	r5, r1
	bcs	.L1569
	ldrb	r3, [r2]	@ zero_extendqisi2
	cbnz	r3, .L1570
	subs	r6, r6, #2
	b	.L1569
.L1570:
	movs	r6, #4
.L1569:
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r0, r4
	mov	r1, r5
	bl	FlashProgFirstCmd
	uxtb	r2, r6
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
.L1572:
	.align	2
.L1571:
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
	ldr	r4, .L1582
	ldr	r5, .L1582+4
	ldr	r8, .L1582+12
	ldr	r3, [r4, #3988]
	mov	r9, r4
	ldrb	r0, [r4, #4014]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #4000]
	bl	FlashBchSel
	movs	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #3988]
	bl	ftl_memset
	ldr	r3, [r4, #4000]
	addw	r1, r5, #1556
	movs	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #4000]
	ldrb	r3, [r5, #1717]	@ zero_extendqisi2
	adds	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #1732]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #4000]
	add	r1, r5, #1720
	movs	r2, #8
	adds	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #4000]
	add	r1, r5, #656
	movs	r2, #32
	adds	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #4000]
	ldr	r1, .L1582+8
	movs	r2, #32
	adds	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #4000]
	add	r1, r5, #44
	movs	r2, #32
	adds	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #4000]
	mov	r2, #852
	addw	r1, r5, #698
	adds	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #4000]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	mov	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #4016]
	str	r3, [r4, #4000]
	str	r0, [r6, #8]
	movs	r0, #0
	bl	flash_enter_slc_mode
	movs	r6, #0
	mov	r7, r6
.L1576:
	ldr	r1, [r5, #4]
	movs	r0, #0
	mov	r2, r0
	muls	r1, r7, r1
	bl	FlashEraseBlock
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #3988]
	muls	r1, r7, r1
	mov	r3, r0
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #3988]
	muls	r1, r7, r1
	mov	r3, r0
	adds	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #4016]
	muls	r1, r7, r1
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r2, r7, #1
	adds	r0, r0, #1
	beq	.L1574
	ldr	fp, [r9, #4000]
	ldr	r3, [fp]
	cmp	r3, r8
	bne	.L1574
	add	r0, fp, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1574
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [r9, #4008]
	mul	r3, r3, r7
	str	r3, [r9, #4004]
	beq	.L1577
	movs	r6, #1
.L1574:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1576
	b	.L1575
.L1577:
	movs	r6, #2
.L1575:
	movs	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	lsrs	r0, r0, #5
	negs	r0, r0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1583:
	.align	2
.L1582:
	.word	.LANCHOR2
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1604
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r3]
	mov	r9, r0
	ldr	r4, .L1604+4
	str	r3, [sp, #4]
	str	r2, [sp, #12]
	ldr	r2, .L1604+8
	ldrb	r10, [r4, #1792]	@ zero_extendqisi2
	ldr	r0, [r2]	@ unaligned
	ldr	r2, [r4, #1728]
	str	r0, [sp, #8]	@ unaligned
	cbz	r2, .L1585
	movs	r0, #0
	bl	flash_enter_slc_mode
.L1585:
	mov	r0, r9
	movs	r1, #0
	mov	r2, #2048
	mov	r8, #-1
	bl	ftl_memset
	movs	r5, #2
.L1586:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1590
	movs	r7, #0
.L1588:
	add	r3, sp, #8
	ldr	r6, .L1604+12
	ldrb	fp, [r3, r7]	@ zero_extendqisi2
	mov	r0, fp
	bl	FlashBchSel
	ldr	r1, [r4, #4]
	movs	r0, #0
	ldr	r2, [r6, #3988]
	muls	r1, r5, r1
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1587
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L1588
	b	.L1589
.L1587:
	ldr	r3, [r6, #3988]
	ldr	r2, .L1604+16
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1589
	mov	r1, fp
	ldr	r0, .L1604+20
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #3988]
	bl	ftl_memcpy
	ldr	r3, [r6, #3988]
	ldr	r2, .L1604+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r6, #4008]
	cmp	r3, r5
	bls	.L1594
	str	r5, [r6, #4008]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1589:
	adds	r5, r5, #1
	b	.L1586
.L1594:
	mov	r8, #0
.L1590:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #1728]
	cbz	r3, .L1591
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1591:
	ldr	r3, [sp, #4]
	mov	r0, r8
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1593
	bl	__stack_chk_fail
.L1593:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1605:
	.align	2
.L1604:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR3+11
	.word	.LANCHOR2
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
	mov	r6, r0
	.pad #20
	sub	sp, sp, #20
	mov	r0, #32768
	bl	ftl_malloc
	ldr	r5, .L1725
	ldr	r4, .L1725+4
	movs	r7, #0
	add	r9, r4, #12
	mov	r8, r7
	str	r0, [r5, #3988]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #4016]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r5, #4020]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #4024]
	mov	r0, #4096
	bl	ftl_malloc
	movs	r3, #50
	str	r7, [r5, #4008]
	strb	r3, [r4, #1]
	strb	r3, [r5, #4012]
	movs	r3, #128
	strb	r7, [r4, #1732]
	str	r3, [r4, #4]
	movs	r3, #60
	str	r7, [r5, #3992]
	strb	r7, [r4]
	strb	r7, [r5, #4032]
	strb	r3, [r5, #4014]
	str	r0, [r5, #4028]
	mov	r0, r6
	bl	NandcInit
	addw	r6, r4, #1556
.L1612:
	add	r3, r9, r8, lsl #3
	uxtb	fp, r8
	ldr	r7, [r9, r8, lsl #3]
	ldrb	r10, [r3, #4]	@ zero_extendqisi2
	mov	r0, fp
	bl	FlashReset
	mov	r0, fp
	bl	NandcFlashCs
	movs	r3, #144
	add	r7, r7, r10, lsl #8
	movs	r0, #200
	mov	r10, #0
	str	r3, [r7, #2056]
	str	r10, [r7, #2052]
	bl	NandcDelayns
	ldr	r2, [r7, #2048]
	uxtb	r2, r2
	strb	r2, [r6]
	ldr	r1, [r7, #2048]
	cmp	r2, #44
	strb	r1, [r6, #1]
	ldr	r1, [r7, #2048]
	strb	r1, [r6, #2]
	ldr	r1, [r7, #2048]
	strb	r1, [r6, #3]
	ldr	r1, [r7, #2048]
	strb	r1, [r6, #4]
	ldr	r1, [r7, #2048]
	strb	r1, [r6, #5]
	bne	.L1607
	movs	r2, #239
	movs	r0, #200
	str	r2, [r7, #2056]
	movs	r2, #1
	str	r2, [r7, #2052]
	bl	NandcDelayns
	movs	r2, #4
	str	r2, [r7, #2048]
	str	r10, [r7, #2048]
	str	r10, [r7, #2048]
	str	r10, [r7, #2048]
.L1607:
	mov	r0, fp
	bl	NandcFlashDeCs
	ldrb	r2, [r6]	@ zero_extendqisi2
	subs	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1608
	ldrb	r1, [r6, #2]	@ zero_extendqisi2
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	ldr	r0, .L1725+8
	str	r1, [sp]
	ldrb	r1, [r6, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r6, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r8, #1
	bl	printk
.L1608:
	cmp	r8, #0
	bne	.L1609
	ldrb	r3, [r4, #1556]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1664
	ldr	r3, .L1725+4
	ldrb	r3, [r3, #1557]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1664
.L1609:
	ldrb	r3, [r6]	@ zero_extendqisi2
	add	r8, r8, #1
	adds	r6, r6, #8
	cmp	r3, #181
	itt	eq
	moveq	r3, #44
	strbeq	r3, [r6, #-8]
	cmp	r8, #4
	bne	.L1612
	ldrb	r3, [r4, #1556]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1613
	ldr	r3, .L1725+4
	ldr	r0, [r3, #1736]
	bl	NandcSetDdrMode
.L1613:
	ldr	r7, .L1725+4
	mov	r2, #852
	movs	r1, #0
	ldr	r6, .L1725+12
	addw	r0, r7, #698
	bl	ftl_memset
	ldr	r3, .L1725+16
	ldr	r2, [r4, #1744]
	ldrb	r0, [r4, #1557]	@ zero_extendqisi2
	cmp	r2, r6
	str	r3, [r4, #692]
	mov	r3, #0
	strb	r3, [r4, #8]
	itt	eq
	moveq	r3, #1
	streq	r3, [r7, #1728]
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
	ldrb	r3, [r3, #1559]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1616
.L1615:
	ldrb	lr, [r4, #1556]	@ zero_extendqisi2
	movs	r3, #16
	strb	r3, [r4, #1]
	movs	r7, #1
	strb	r3, [r5, #4014]
	cmp	lr, #152
	ldr	r3, .L1725+20
	strb	r7, [r4]
	ldr	r1, .L1725
	strb	lr, [r3, #3257]
	strb	r0, [r3, #3258]
	bne	.L1618
	ldr	lr, .L1725+4
	ldrsb	lr, [lr, #1560]
	cmp	lr, #0
	blt	.L1619
	movs	r7, #24
	strb	r7, [r1, #4014]
.L1618:
	cmp	r2, r6
	beq	.L1621
	movw	r1, #2049
	cmp	r2, r1
	bne	.L1622
.L1621:
	movs	r2, #16
	strb	r2, [r5, #4014]
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
	strh	r2, [r3, #3270]	@ movhi
	strb	r0, [r3, #3258]
	b	.L1624
.L1625:
	cmp	r0, #211
	itttt	eq
	moveq	r2, #4096
	strheq	r2, [r3, #3270]	@ movhi
	moveq	r2, #2
	strbeq	r2, [r3, #3269]
.L1624:
	ldr	r1, .L1725+24
	movs	r2, #32
	ldr	r0, .L1725+28
	bl	ftl_memcpy
	ldr	r0, .L1725+16
	movs	r2, #32
	add	r1, r0, #2784
	bl	ftl_memcpy
.L1616:
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r7, .L1725+4
	cmp	r3, #0
	bne	.L1626
	bl	FlashLoadPhyInfoInRam
	cbnz	r0, .L1628
	ldr	r3, [r7, #692]
	ldrh	r3, [r3, #16]
	lsrs	r3, r3, #8
	and	r0, r3, #7
	lsls	r3, r3, #31
	strb	r0, [r7, #1716]
	bmi	.L1628
	movs	r3, #1
	strb	r3, [r7, #1732]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r7, #1716]	@ zero_extendqisi2
	bl	NandcSetMode
.L1628:
	ldr	r3, [r4, #692]
	ldr	r7, .L1725+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #140]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1626
	ldr	r3, [r7, #1736]
	cbz	r3, .L1631
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	b	.L1721
.L1726:
	.align	2
.L1725:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC96
	.word	1446522928
	.word	.LANCHOR1+472
	.word	.LANCHOR1
	.word	.LANCHOR1+3132
	.word	.LANCHOR0+44
.L1631:
	ldrb	r0, [r7, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r7, #1716]	@ zero_extendqisi2
.L1721:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1626
	movs	r0, #1
	ldr	r7, .L1727
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #692]
	ldr	r0, .L1727+4
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	adds	r1, r0, #1
	beq	.L1683
	bl	FlashDieInfoInit
	ldr	r3, [r7, #692]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, [r7, #692]
	ldrh	r2, [r5, #1730]
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
	ldrb	r3, [r4, #1716]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1635
	bl	FlashSavePhyInfo
	movs	r0, #0
	ldr	r1, [r5, #4004]
	bl	FlashDdrParaScan
.L1635:
	bl	FlashSavePhyInfo
.L1626:
	ldr	r2, [r4, #692]
	ldr	r8, .L1727
	ldr	r7, .L1727+8
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r9, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #140]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r4, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r5, #4033]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #1793]
	strb	r3, [r4, #1716]
	movs	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r5, #3996]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r9
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #692]
	ldrh	r2, [r3, #16]
	lsls	r2, r2, #25
	bpl	.L1637
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r8, #699]	@ zero_extendqisi2
	strb	r0, [r8, #696]
	strb	r3, [r8, #697]
	ldrb	r3, [r8, #700]	@ zero_extendqisi2
	strb	r3, [r7, #4013]
	subs	r3, r0, #1
	cmp	r3, #7
	bhi	.L1638
	ldr	r3, .L1727+12
	str	r3, [r7, #3996]
	subs	r3, r0, #5
	cmp	r3, #1
	bls	.L1639
	cmp	r0, #8
	bne	.L1640
.L1639:
	cmp	r0, #7
	mov	r3, #1
	str	r3, [r4, #1784]
	beq	.L1665
	cmp	r0, #8
	b	.L1724
.L1665:
	ldr	r3, .L1727+16
.L1642:
	subs	r1, r3, #1
	movs	r2, #0
	adds	r3, r3, #31
.L1643:
	ldrsb	r7, [r1, #1]!
	cmp	r7, #0
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
	ldr	r3, .L1727+20
	cmp	r0, #19
	str	r3, [r7, #3996]
	ite	ne
	movne	r3, #7
	moveq	r3, #15
	strb	r3, [r7, #4013]
	b	.L1637
.L1645:
	cmp	r0, #33
	beq	.L1647
	sub	r3, r0, #65
	cmp	r3, #1
	bhi	.L1648
.L1647:
	ldr	r3, .L1727+24
	str	r3, [r5, #3996]
	movs	r3, #4
	strb	r3, [r4, #697]
	movs	r3, #7
	strb	r3, [r5, #4013]
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
	ldr	r2, .L1727+24
	cmp	r0, #35
	str	r2, [r5, #3996]
	beq	.L1651
	cmp	r0, #68
	beq	.L1651
	ldr	r2, .L1727+8
	movs	r1, #7
	strb	r1, [r2, #4013]
	b	.L1652
.L1651:
	movs	r2, #17
	strb	r2, [r5, #4013]
.L1652:
	cmp	r3, #1
	ite	ls
	movls	r3, #4
	movhi	r3, #5
	strb	r3, [r4, #697]
	b	.L1637
.L1650:
	cmp	r0, #49
	itt	eq
	ldreq	r3, .L1727+28
	streq	r3, [r7, #3996]
.L1637:
	ldr	r2, [r4, #1744]
	ldr	r3, .L1727
	cmp	r2, r6
	bne	.L1654
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cbz	r1, .L1654
	ldr	r3, [r3, #692]
	movs	r1, #0
	strb	r1, [r3, #18]
.L1654:
	ldrb	r1, [r4, #1556]	@ zero_extendqisi2
	ldr	r3, .L1727
	cmp	r1, #44
	bne	.L1655
	ldrb	r1, [r3, #1732]	@ zero_extendqisi2
	cbz	r1, .L1655
	cmp	r2, r6
	bne	.L1656
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbnz	r3, .L1655
.L1656:
	movs	r0, #1
	movs	r3, #0
	strb	r3, [r4, #1732]
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
.L1655:
	ldrb	r3, [r4, #1716]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1657
	ldr	r2, .L1727
	ldrb	r2, [r2, #1732]	@ zero_extendqisi2
	cbnz	r2, .L1658
	lsls	r3, r3, #31
	bmi	.L1657
.L1658:
	movs	r0, #0
	bl	flash_enter_slc_mode
	movs	r0, #0
	ldr	r1, [r5, #4004]
	bl	FlashDdrParaScan
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1657:
	ldr	r3, [r4, #692]
	mov	r8, #16
	ldr	r6, .L1727
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1727+32
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1727+36
	strb	r8, [r4, #1]
	bl	FlashTimingCfg
	ldr	r5, [r4, #692]
	ldrb	r3, [r5, #12]	@ zero_extendqisi2
	ldrh	r7, [r5, #14]
	strh	r3, [r4, #120]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	str	r3, [r4, #116]
	ldr	r3, [r4, #1556]
	str	r3, [r4, #112]
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
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
	ldr	r3, .L1727+40
	b	.L1642
.L1619:
	strb	r7, [r1, #4032]
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
	.word	HynixReadRetrial
	.word	.LANCHOR0+726
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	.LANCHOR2+1936
	.word	150000
	.word	.LANCHOR0+718
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r2
	ldr	r3, .L1750
	mov	r8, r0
	mov	r9, r1
	ldr	r2, [r3, #692]
	mov	r6, r3
	ldrb	r7, [r2, #19]	@ zero_extendqisi2
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cbz	r2, .L1730
	ldr	r3, [r3, #1728]
	cbnz	r3, .L1729
.L1730:
	subs	r3, r7, #5
	cmp	r3, #3
	bls	.L1731
	cmp	r7, #68
	beq	.L1731
	cmp	r7, #35
	beq	.L1731
	cmp	r7, #19
	bne	.L1729
.L1731:
	ldr	r5, .L1750+4
	movw	r10, #65535
	mov	fp, r5
.L1733:
	ldr	r3, [r6, #692]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1729
	add	r3, r5, r4, lsl #1
	ldrh	r3, [r3, #704]
	cmp	r3, r10
	bne	.L1729
	cmp	r7, #8
	mov	r2, #32768
	ite	eq
	moveq	r1, #0
	movne	r1, #255
	ldr	r0, [r5, #4016]
	bl	ftl_memset
	ldr	r2, [fp, #4016]
	add	r1, r4, r9
	adds	r4, r4, #1
	mov	r0, r8
	mov	r3, r2
	uxth	r4, r4
	bl	FlashProgPage
	b	.L1733
.L1729:
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1751:
	.align	2
.L1750:
	.word	.LANCHOR0
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
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1803
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r9, .L1803+24
	movs	r7, #0
	str	r2, [sp, #16]
	mov	r4, r0
	ldr	r2, [r3]
	mov	r10, r9
	str	r1, [sp, #12]
	str	r3, [sp, #20]
	str	r2, [sp, #36]
	ldr	r2, .L1803+4
	ldrb	r2, [r2, #481]	@ zero_extendqisi2
	str	r2, [sp, #8]
.L1753:
	ldr	r3, [sp, #12]
	cmp	r7, r3
	beq	.L1802
	ldr	r3, [sp, #12]
	add	r2, sp, #32
	mov	r0, r4
	ldr	r1, [sp, #16]
	subs	r3, r3, r7
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #28
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #28]
	cmp	r3, r2
	bcc	.L1754
	mov	r3, #-1
	str	r3, [r4]
	b	.L1755
.L1754:
	add	r3, r3, r9
	mov	r8, #0
	ldrb	r6, [r3, #1720]	@ zero_extendqisi2
	mov	r0, r6
	bl	NandcWaitFlashReady
	mov	r0, r6
	bl	NandcFlashCs
.L1756:
	ldr	r1, [sp, #32]
	mov	r0, r6
	bl	FlashReadCmd
	mov	r0, r6
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #12]
	mov	r0, r6
	movs	r1, #0
	str	r3, [sp]
	ldr	r2, [sp, #8]
	ldr	r3, [r4, #8]
	bl	NandcXferData
	ldr	r3, .L1803+8
	ldrb	r3, [r3, #4032]	@ zero_extendqisi2
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
	cmp	r8, #10
	beq	.L1780
	add	r8, r8, #1
	b	.L1756
.L1759:
	cmp	r8, #0
	it	ne
	movne	r5, #256
	b	.L1761
.L1780:
	mov	r5, #256
.L1761:
	mov	r8, #0
.L1762:
	ldr	r3, [sp, #32]
	mov	r0, r6
	ldr	r1, [r10, #4]
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
	ldr	r2, [sp, #8]
	movs	r1, #0
	bl	NandcXferData
	ldr	r3, .L1803+8
	ldrb	r2, [r3, #4032]	@ zero_extendqisi2
	mov	fp, r0
	cbnz	r2, .L1765
.L1768:
	cmp	fp, #-1
	beq	.L1766
	b	.L1767
.L1765:
	mov	r0, r6
	bl	flash_read_ecc
	cmp	r0, #5
	bls	.L1768
	mov	fp, #256
	b	.L1767
.L1766:
	cmp	r8, #10
	beq	.L1784
	add	r8, r8, #1
	b	.L1762
.L1767:
	cmp	r8, #0
	it	ne
	movne	fp, #256
	b	.L1769
.L1784:
	mov	fp, #256
.L1769:
	mov	r0, r6
	bl	NandcFlashDeCs
	ldrb	r3, [r10, #1792]	@ zero_extendqisi2
	cmp	r5, fp
	add	r3, r3, r3, lsl #1
	it	cc
	movcc	r5, fp
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
	ldr	r0, .L1803+12
	ldrb	r2, [r10, #1792]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cbz	r1, .L1775
	ldr	r0, .L1803+16
	movs	r2, #4
	movs	r3, #8
	bl	rknand_print_hex
.L1775:
	ldr	r1, [r4, #12]
	cbz	r1, .L1755
	movs	r2, #4
	ldr	r0, .L1803+20
	mov	r3, r2
	bl	rknand_print_hex
.L1755:
	adds	r7, r7, #1
	adds	r4, r4, #36
	b	.L1753
.L1802:
	ldr	r3, [sp, #20]
	movs	r0, #0
	ldr	r2, [sp, #36]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1778
	bl	__stack_chk_fail
.L1778:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1804:
	.align	2
.L1803:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR2
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
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1905
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r3, [r3]
	mov	r10, r0
	ldr	r5, .L1905+4
	str	r1, [sp, #28]
	str	r3, [sp, #52]
	ldr	r3, .L1905+8
	str	r2, [sp, #16]
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	str	r3, [sp, #24]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	str	r3, [sp, #32]
	ldrb	r3, [r5]	@ zero_extendqisi2
	cbz	r3, .L1849
	bl	FlashReadSlc2KPages
	b	.L1807
.L1849:
	mov	r8, r3
	str	r3, [sp, #12]
.L1806:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	cmp	r3, r2
	bcs	.L1902
	ldr	r3, [sp, #12]
	mov	r9, #36
	ldr	r2, [sp, #12]
	ldr	r1, [sp, #16]
	mul	r9, r9, r3
	ldr	r3, [sp, #28]
	subs	r3, r3, r2
	add	r2, sp, #48
	add	r7, r10, r9
	uxtb	r3, r3
	ldr	fp, [r7, #4]
	mov	r0, r7
	str	r3, [sp]
	add	r3, sp, #44
	bl	LogAddr2PhyAddr
	ldrb	r2, [r5, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #44]
	mov	r6, r0
	cmp	r3, r2
	bcc	.L1808
	mov	r3, #-1
	str	r3, [r10, r9]
	b	.L1809
.L1808:
	add	r3, r3, r5
	ldrb	r4, [r3, #1720]	@ zero_extendqisi2
	ldr	r3, .L1905+12
	mov	r0, r4
	ldrb	r3, [r3, #4033]	@ zero_extendqisi2
	cmp	r3, #0
	it	eq
	moveq	r6, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1905+4
	ldr	r3, [r3, #692]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	subs	r3, r2, #1
	cmp	r3, #7
	bhi	.L1811
	subs	r2, r2, #7
	adds	r1, r5, r4
	cmp	r2, #1
	ldr	r2, .L1905+4
	ldrb	r3, [r1, #710]	@ zero_extendqisi2
	add	r2, r2, r4
	it	ls
	ldrbls	r3, [r1, #718]	@ zero_extendqisi2
	ldrb	r2, [r2, #1552]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1811
	mov	r0, r4
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	ldr	r2, .L1905+16
	bl	HynixSetRRPara
.L1811:
	mov	r0, r4
	and	r3, fp, #-2147483648
	str	r3, [sp, #20]
	bl	NandcFlashCs
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1813
	ldr	r3, [sp, #20]
	cbz	r3, .L1814
.L1813:
	ldr	r3, .L1905+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cbz	r3, .L1814
	mov	r0, r4
	bl	flash_enter_slc_mode
.L1816:
	mov	fp, #0
	b	.L1815
.L1814:
	mov	r0, r4
	bl	flash_exit_slc_mode
	b	.L1816
.L1903:
	cmp	r4, #255
	beq	.L1850
.L1817:
	cbz	r6, .L1819
	ldr	r2, [r5, #4]
	mov	r0, r4
	add	r2, r2, r1
	bl	FlashReadDpCmd
.L1820:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cbz	r6, .L1818
	mov	r0, r4
	ldr	r1, [sp, #48]
	bl	FlashReadDpDataOutCmd
.L1818:
	ldr	r3, [r7, #12]
	mov	r0, r4
	movs	r1, #0
	ldr	r2, [sp, #24]
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	ip, r0
	cbz	r3, .L1852
	cmp	r0, #-1
	bne	.L1852
	movs	r6, #0
	strb	fp, [r5, #8]
.L1815:
	ldr	r1, [sp, #48]
	adds	r0, r1, #1
	bne	.L1817
	b	.L1903
.L1819:
	mov	r0, r4
	bl	FlashReadCmd
	b	.L1820
.L1850:
	movs	r6, #0
	b	.L1818
.L1852:
	mov	fp, ip
	cbz	r6, .L1822
	ldr	r3, .L1905+4
	mov	r0, r4
	str	ip, [sp, #36]
	ldr	r1, [r3, #4]
	ldr	r3, [sp, #48]
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
.L1822:
	mov	r0, r4
	str	ip, [sp, #36]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	ldrb	r3, [sp, #32]	@ zero_extendqisi2
	cmp	ip, #-1
	strb	r3, [r5, #8]
	bne	.L1829
	ldrb	r3, [r5, #1732]	@ zero_extendqisi2
	cbnz	r3, .L1824
.L1828:
	ldr	r3, .L1905+12
	ldr	r6, [r3, #3996]
	cbnz	r6, .L1825
	b	.L1904
.L1824:
	ldr	r3, [r5, #76]
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r6, [r3, #304]
	movs	r3, #1
	str	r3, [sp]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashDdrTunningRead
	cmp	r0, #-1
	mov	fp, r0
	beq	.L1827
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1854
.L1827:
	ubfx	r0, r6, #8, #8
	bl	NandcSetDdrPara
	cmp	fp, #-1
	beq	.L1828
	b	.L1854
.L1825:
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r6
	cmp	r0, #-1
	mov	fp, r0
	bne	.L1830
	ldr	r3, [r5, #692]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #1
	cmp	r3, #7
	bhi	.L1831
	mov	r0, r4
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	ldr	r2, .L1905+16
	movs	r3, #0
	bl	HynixSetRRPara
.L1831:
	ldr	r1, [sp, #48]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r5, #1792]	@ zero_extendqisi2
	mov	fp, r0
	ldr	r0, .L1905+20
	mov	r3, fp
	bl	printk
	cmp	fp, #-1
	bne	.L1830
	ldrb	r6, [r5, #140]	@ zero_extendqisi2
	cbz	r6, .L1832
	ldr	r3, [sp, #16]
	mov	r0, r4
	cmp	r3, #1
	beq	.L1833
	ldr	r3, [sp, #20]
	cbz	r3, .L1834
.L1833:
	bl	flash_enter_slc_mode
	b	.L1835
.L1834:
	bl	flash_exit_slc_mode
.L1835:
	ldr	r3, .L1905+12
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r6, [r3, #3996]
	ldr	r3, [r7, #12]
	blx	r6
	movs	r6, #0
	mov	fp, r0
	b	.L1836
.L1904:
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	fp, r0
	b	.L1836
.L1854:
	movs	r6, #0
.L1829:
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	fp, r3, asr #2
	bls	.L1838
	ldr	r3, .L1905+12
	ldr	r3, [r3, #3996]
	cbz	r3, .L1856
.L1836:
	cmp	fp, #-1
	beq	.L1832
	b	.L1838
.L1856:
	mov	fp, #256
.L1832:
	str	fp, [r10, r9]
	b	.L1839
.L1906:
	.align	2
.L1905:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0+702
	.word	.LC101
.L1830:
	movs	r6, #0
.L1838:
	cmp	fp, #256
	beq	.L1832
	movs	r3, #0
	str	r3, [r10, r9]
.L1839:
	ldr	r3, [r10, r9]
	adds	r2, r3, #1
	bne	.L1841
	ldr	r1, [r7, #4]
	ldr	r0, .L1907
	ldrb	r2, [r5, #1792]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cbz	r1, .L1841
	movs	r2, #4
	ldr	r0, .L1907+4
	mov	r3, r2
	bl	rknand_print_hex
.L1841:
	cbz	r6, .L1843
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r8, r3, asr #2
	bls	.L1844
	ldr	r3, .L1907+8
	ldr	r3, [r3, #3996]
	cmp	r3, #0
	it	eq
	moveq	r8, #256
.L1844:
	add	r9, r9, #36
	cmp	r8, #-1
	str	r8, [r10, r9]
	beq	.L1843
	cmp	r8, #256
	itt	ne
	movne	r3, #0
	strne	r3, [r10, r9]
.L1843:
	ldr	r3, [sp, #12]
	add	r3, r3, r6
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1845
	ldr	r3, [sp, #20]
	cbz	r3, .L1809
.L1845:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cbz	r3, .L1809
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1809:
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	str	r3, [sp, #12]
	b	.L1806
.L1902:
	movs	r0, #0
.L1807:
	ldr	r3, .L1907+12
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1848
	bl	__stack_chk_fail
.L1848:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1908:
	.align	2
.L1907:
	.word	.LC98
	.word	.LC100
	.word	.LANCHOR2
	.word	__stack_chk_guard
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	1
	.global	FlashLoadFactorBbt
	.thumb
	.thumb_func
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r9, .L1922+12
	movs	r1, #0
	ldr	r7, .L1922
	movs	r5, #0
	ldr	r4, .L1922+4
	mov	fp, #-1
	ldr	r3, [r9]
	mov	r10, r5
	ldrh	r2, [r7, #126]
	addw	r0, r4, #4036
	mov	r8, r7
	str	r3, [sp, #52]
	ldrh	r3, [r7, #124]
	smulbb	r3, r3, r2
	movs	r2, #16
	uxth	r6, r3
	bl	ftl_memset
	ldr	r3, [r4, #4020]
	str	r9, [sp, #4]
	mov	r9, r4
	str	r5, [sp, #24]
	str	r3, [sp, #28]
.L1910:
	ldrb	r3, [r8, #1717]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1921
	mul	ip, r6, r7
	subs	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1911:
	cmp	r4, r3
	ble	.L1913
	add	r2, ip, r4
	movs	r1, #1
	add	r0, sp, #16
	str	r3, [sp, #12]
	lsls	r2, r2, #10
	str	r2, [sp, #20]
	mov	r2, r1
	str	ip, [sp, #8]
	bl	FlashReadPages
	ldr	r2, [sp, #16]
	ldr	ip, [sp, #8]
	adds	r2, r2, #1
	ldr	r3, [sp, #12]
	beq	.L1912
	ldr	r2, [r9, #4020]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1912
	mov	r1, r7
	ldr	r0, .L1922+8
	mov	r2, r4
	add	r10, r10, #1
	bl	printk
	ldr	r3, .L1922+4
	uxth	r10, r10
	add	r7, r3, r7, lsl #1
	strh	r4, [r7, #4036]	@ movhi
	b	.L1913
.L1912:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1911
.L1913:
	ldrb	r3, [r8, #1717]	@ zero_extendqisi2
	adds	r5, r5, #1
	cmp	r3, r10
	it	eq
	moveq	fp, #0
	b	.L1910
.L1921:
	ldr	r3, [sp, #4]
	mov	r0, fp
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1917
	bl	__stack_chk_fail
.L1917:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1923:
	.align	2
.L1922:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC102
	.word	__stack_chk_guard
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
	mov	r9, r2
	ldr	r8, .L1955+28
	str	r3, [sp, #8]
	movs	r7, #0
	ldr	r3, .L1955
	mov	r6, r1
	mov	r4, r0
	mov	fp, r0
	ldr	r2, [r3]
	str	r3, [sp, #12]
	str	r2, [sp, #60]
	ldr	r2, .L1955+4
	ldrb	r10, [r2, #481]	@ zero_extendqisi2
.L1925:
	cmp	r7, r6
	beq	.L1954
	subs	r3, r6, r7
	add	r2, sp, #16
	mov	r0, fp
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #20
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #20]
	cmp	r3, r2
	bcc	.L1926
	mov	r3, #-1
	str	r3, [fp]
	b	.L1927
.L1926:
	add	r3, r3, r8
	ldrb	r5, [r3, #1720]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r1, [sp, #16]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r2, r10
	movs	r1, #1
	mov	r0, r5
	str	r3, [sp]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	mov	r0, r5
	ldr	r1, [sp, #16]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	ldr	r1, [sp, #16]
	bl	FlashReadStatus
	ldr	r3, [sp, #16]
	sbfx	r0, r0, #0, #1
	str	r0, [fp]
	mov	r0, r5
	ldr	r1, [r8, #4]
	add	r1, r1, r3
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #8]
	cbz	r3, .L1928
	add	r3, r3, #2048
.L1928:
	ldr	r2, [fp, #12]
	cbz	r2, .L1929
	adds	r2, r2, #8
.L1929:
	str	r2, [sp]
	movs	r1, #1
	mov	r2, r10
	mov	r0, r5
	bl	NandcXferData
	ldr	r1, [r8, #4]
	mov	r0, r5
	ldr	r3, [sp, #16]
	add	r1, r1, r3
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	ldr	r1, [sp, #16]
	bl	FlashReadStatus
	lsls	r2, r0, #31
	mov	r0, r5
	itt	mi
	movmi	r3, #-1
	strmi	r3, [fp]
	bl	NandcFlashDeCs
.L1927:
	adds	r7, r7, #1
	add	fp, fp, #36
	b	.L1925
.L1954:
	ldr	r3, [sp, #8]
	cbnz	r3, .L1943
.L1939:
	ldr	r3, [sp, #12]
	movs	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1940
	bl	__stack_chk_fail
.L1943:
	ldr	r7, .L1955+8
	mov	r8, #0
	mov	r10, r7
.L1932:
	cmp	r8, r6
	beq	.L1939
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L1933
	ldr	r1, [r4, #4]
	ldr	r0, .L1955+12
	bl	printk
	b	.L1934
.L1933:
	rsb	r3, r8, r6
	mov	r1, r9
	add	r2, sp, #16
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #20
	bl	LogAddr2PhyAddr
	ldr	r2, [r7, #4024]
	movs	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r7, #4028]
	str	r3, [r2]
	ldmia	lr!, {r0, r1, r2, r3}
	add	r5, sp, #24
	stmia	r5!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	add	r0, sp, #24
	ldr	r3, [lr]
	movs	r1, #1
	mov	r2, r9
	str	r3, [r5]
	ldr	r3, [r7, #4024]
	str	r3, [sp, #32]
	ldr	r3, [r7, #4028]
	str	r3, [sp, #36]
	bl	FlashReadPages
	ldr	r5, [sp, #24]
	adds	r3, r5, #1
	bne	.L1935
	ldr	r0, .L1955+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L1935:
	ldr	r3, [r4, #12]
	cbz	r3, .L1936
	ldr	r2, [r3]
	ldr	r3, [r10, #4028]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1936
	ldr	r0, .L1955+20
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1936:
	ldr	r3, [r4, #8]
	cbz	r3, .L1934
	ldr	r2, [r3]
	ldr	r3, [r10, #4024]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1934
	ldr	r0, .L1955+24
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1934:
	add	r8, r8, #1
	adds	r4, r4, #36
	b	.L1932
.L1940:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1956:
	.align	2
.L1955:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LC103
	.word	.LC104
	.word	.LC105
	.word	.LC106
	.word	.LANCHOR0
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
	ldr	r5, .L1968
	ldr	r7, .L1968+4
	ldr	r3, [r5, #1776]
	subw	r8, r7, #1946
	ldr	r9, [r5, #1800]
	mov	r10, r8
	str	r3, [r5, #4060]
	str	r9, [r5, #4064]
.L1958:
	ldrh	r3, [r8, #1826]
	cmp	r6, r3
	bcs	.L1967
	ldrh	r4, [r8, #1870]
	movw	fp, #61664
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
	subs	r4, r4, #1
	uxth	r4, r4
.L1959:
	ldrh	r3, [r10, #1870]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1961
	mla	r3, r3, r6, r4
	movs	r1, #1
	ldr	r0, .L1968+8
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [r5, #4056]
	bl	FlashReadPages
	ldr	r3, [r5, #4052]
	adds	r3, r3, #1
	beq	.L1960
	ldrh	r3, [r9]
	cmp	r3, fp
	bne	.L1960
	strh	r4, [r7]	@ movhi
	b	.L1961
.L1960:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1959
.L1961:
	adds	r6, r6, #1
	b	.L1958
.L1967:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1969:
	.align	2
.L1968:
	.word	.LANCHOR2
	.word	.LANCHOR0+1946
	.word	.LANCHOR2+4052
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
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r6, .L1982
	cmp	r1, #1
	mov	r9, r1
	lsl	r7, r0, #10
	mov	r1, #1
	mov	r0, sp
	ldr	r3, [r6]
	mov	r2, r9
	mov	r8, #0
	str	r8, [sp, #8]
	str	r3, [sp, #100]
	ldr	r3, .L1982+4
	ite	eq
	ldrheq	r4, [r3, #1874]
	ldrhne	r4, [r3, #1872]
	add	r3, sp, #36
	str	r3, [sp, #12]
	subs	r5, r4, #1
	uxth	r5, r5
	sxth	r3, r5
	orrs	r3, r3, r7
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp, #36]
	adds	r3, r3, #1
	bne	.L1973
.L1974:
	sxth	r3, r8
	sxth	r4, r5
	cmp	r3, r4
	bgt	.L1973
	add	r4, r4, r3
	mov	r0, sp
	movs	r1, #1
	mov	r2, r9
	add	r4, r4, r4, lsr #31
	asrs	r4, r4, #1
	sxth	r3, r4
	orrs	r3, r3, r7
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp, #36]
	adds	r3, r3, #1
	bne	.L1975
	ldr	r3, [sp, #40]
	adds	r3, r3, #1
	bne	.L1975
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L1975
	subs	r5, r4, #1
	uxth	r5, r5
	b	.L1974
.L1975:
	adds	r3, r4, #1
	uxth	r8, r3
	b	.L1974
.L1973:
	ldr	r2, [sp, #100]
	sxth	r0, r5
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L1978
	bl	__stack_chk_fail
.L1978:
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1983:
	.align	2
.L1982:
	.word	__stack_chk_guard
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r4, .L2013
	ldr	r7, .L2013+4
	ldr	r3, [r4, #1776]
	mov	r8, r4
	ldr	r6, [r4, #1800]
	str	r3, [r4, #4060]
	str	r6, [r4, #4064]
	bl	FtlBbtMemInit
	ldrh	r5, [r7, #1870]
	subs	r5, r5, #1
	uxth	r5, r5
.L1985:
	ldrh	r3, [r7, #1870]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1988
	movs	r1, #1
	ldr	r0, .L2013+8
	lsls	r3, r5, #10
	str	r3, [r4, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #4052]
	adds	r3, r3, #1
	bne	.L1986
	movs	r1, #1
	ldr	r3, [r8, #4056]
	ldr	r0, .L2013+8
	mov	r2, r1
	adds	r3, r3, #1
	str	r3, [r8, #4056]
	bl	FlashReadPages
.L1986:
	ldr	r3, [r4, #4052]
	adds	r3, r3, #1
	beq	.L1987
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1987
	ldr	r3, [r6, #4]
	strh	r5, [r7, #1936]	@ movhi
	str	r3, [r7, #1944]
	ldrh	r3, [r6, #8]
	strh	r3, [r7, #1940]	@ movhi
	b	.L1988
.L1987:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1985
.L1988:
	ldrh	r3, [r7, #1936]
	movw	r2, #65535
	ldr	r5, .L2013+4
	cmp	r3, r2
	beq	.L2002
	ldrh	r3, [r5, #1940]
	cmp	r3, r2
	beq	.L1992
	movs	r1, #1
	ldr	r0, .L2013+8
	lsls	r3, r3, #10
	str	r3, [r4, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #4052]
	adds	r3, r3, #1
	beq	.L1992
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1992
	ldr	r3, [r6, #4]
	ldr	r2, [r5, #1944]
	cmp	r3, r2
	itttt	hi
	strhi	r3, [r5, #1944]
	ldrhhi	r2, [r5, #1940]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r5, #1936]	@ movhi
	it	hi
	strhhi	r3, [r5, #1940]	@ movhi
.L1992:
	ldrh	r0, [r7, #1936]
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r7, #1938]	@ movhi
.L1994:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L1999
	ldrh	r2, [r7, #1936]
	movs	r1, #1
	ldr	r0, .L2013+8
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #4056]
	ldr	r3, [r4, #1776]
	mov	r2, r1
	str	r3, [r4, #4060]
	bl	FlashReadPages
	ldr	r3, [r4, #4052]
	adds	r3, r3, #1
	beq	.L1995
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1995
.L1999:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L2013+4
	strh	r2, [r7, #1942]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1996
	b	.L1997
.L1995:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1994
.L1996:
	ldr	r2, [r3, #1800]
	cmp	r0, r2
	beq	.L1997
	ldrh	r3, [r3, #1814]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L1997
	cmp	r0, r3
	bcs	.L1997
	bl	FtlSysBlkNumInit
.L1997:
	ldr	r6, .L2013+12
	movs	r5, #0
.L2000:
	ldrh	r3, [r7, #1826]
	cmp	r5, r3
	bcs	.L2012
	ldrh	r2, [r4, #1864]
	ldr	r1, [r4, #4060]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	bl	ftl_memcpy
	adds	r5, r5, #1
	b	.L2000
.L2012:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2002:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L2014:
	.align	2
.L2013:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+4052
	.word	.LANCHOR0+1960
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
	ldr	r5, .L2100
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L2100+4
	mov	r1, r6
	ldr	r2, [r5, #1900]
	ldr	r0, [r4, #1856]
	mov	r9, r4
	strh	r6, [r5, #1918]	@ movhi
	lsls	r2, r2, #2
	strh	r6, [r4, #1912]	@ movhi
	bl	ftl_memset
	ldr	r2, [r5, #1900]
	mov	r1, r6
	ldr	r0, [r4, #1828]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #1892]
	mov	r1, r6
	ldr	r0, [r4, #1840]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #1892]
	ldr	r0, [r5, #1920]
	mov	r1, r6
	lsls	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #24
	movs	r1, #255
	movs	r2, #16
	bl	ftl_memset
	ldrh	r3, [r5, #1812]
	str	r5, [sp, #12]
	mov	r5, r4
	str	r3, [sp, #4]
.L2016:
	ldr	r6, .L2100
	ldr	r2, [sp, #4]
	ldrh	r3, [r6, #1814]
	mov	r0, r6
	cmp	r3, r2
	bls	.L2058
	ldr	r1, [r4, #632]
	mov	r10, #36
	ldrh	r8, [r6, #1804]
	movs	r7, #0
	ldr	r2, [r4, #1764]
	str	r1, [sp, #8]
	ldr	r3, [r4, #628]
	ldrh	r1, [r6, #1884]
	str	r7, [sp]
	str	r1, [sp, #16]
	add	r1, r6, #1832
	str	r1, [sp, #20]
.L2059:
	uxth	r1, r7
	cmp	r1, r8
	bcs	.L2095
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
	cbnz	r0, .L2017
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
.L2017:
	adds	r7, r7, #1
	b	.L2059
.L2095:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L2021
	mov	r1, r3
	ldr	r0, [r4, #1764]
	movs	r2, #1
	ldr	r8, .L2100
	bl	FlashReadPages
	movs	r3, #0
	str	r3, [sp, #8]
.L2022:
	ldrh	r3, [sp, #8]
	ldr	r2, [sp]
	cmp	r3, r2
	bcs	.L2021
	ldr	r3, [sp, #8]
	mov	r10, #36
	mul	r10, r10, r3
	ldr	r3, [r4, #1764]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r6, [r2, #4]
	adds	r3, r3, #1
	ldr	r7, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L2024
	mov	fp, #16
	movw	r3, #65535
.L2023:
	ldr	r0, [r4, #1764]
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
	bne	.L2025
	ldr	r3, [r9, #1764]
	mov	r2, #-1
	str	r2, [r3, r10]
	b	.L2024
.L2025:
	ldr	r2, [r5, #1764]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L2024
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L2023
.L2024:
	ldr	r3, [r5, #1764]
	ldr	r3, [r3, r10]
	adds	r3, r3, #1
	beq	.L2027
	ldr	r2, [r5, #-8]
	ldr	r3, [r7, #4]
	adds	r1, r2, #1
	beq	.L2028
	cmp	r2, r3
	bhi	.L2029
.L2028:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r9, #-8]
.L2029:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2031
	bhi	.L2032
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2030
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #12]
	ldrh	r10, [r3, #1892]
	ldrh	r0, [r2, #1918]
	ldr	r2, [r9, #1840]
	add	lr, r10, #-1
	uxth	r3, lr
	rsb	lr, r0, lr
	str	r2, [sp, #16]
	b	.L2045
.L2032:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2034
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2030
	mov	r0, r6
	b	.L2094
.L2034:
	ldr	r3, [sp, #12]
	ldrh	r0, [r4, #1912]
	ldr	r2, [r4, #1856]
	ldr	r1, [r3, #1900]
	str	r2, [sp, #16]
	uxth	lr, r1
	add	r3, lr, #-1
	rsb	lr, r0, lr
	add	lr, lr, #-1
	uxth	r3, r3
	sxth	lr, lr
.L2036:
	sxth	ip, r3
	cmp	ip, lr
	ble	.L2096
	ldr	r2, [r7, #4]
	lsl	fp, ip, #2
	mov	r10, r2
	ldr	r2, [sp, #16]
	ldr	r2, [r2, ip, lsl #2]
	cmp	r10, r2
	bls	.L2037
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L2038
	cmp	r0, r1
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r9, #1912]	@ movhi
.L2038:
	movs	r1, #0
	uxth	lr, r3
.L2039:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2097
	ldr	r0, [r5, #1856]
	sxth	r2, r2
	adds	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	ldr	r0, [r5, #1828]
	add	r10, r0, r2, lsl #1
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2, lsl #1]	@ movhi
	b	.L2039
.L2097:
	ldr	r1, [r7, #4]
	ldr	r2, [r4, #1856]
	str	r1, [r2, fp]
	sxth	r1, r3
	ldr	r2, [r4, #1828]
	cmp	r1, #0
	strh	r6, [r2, ip, lsl #1]	@ movhi
	bge	.L2041
	b	.L2030
.L2037:
	subs	r3, r3, #1
	uxth	r3, r3
	b	.L2036
.L2096:
	cmp	ip, #0
	bge	.L2072
	b	.L2030
.L2041:
	ldrh	r0, [r5, #1912]
	ldr	r2, [r8, #1900]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r1, r2
	bgt	.L2030
.L2072:
	ldr	r2, [r4, #1856]
	sxth	r3, r3
	ldr	r1, [r7, #4]
	adds	r0, r0, #1
	strh	r0, [r4, #1912]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #1828]
	b	.L2092
.L2051:
	ldr	r1, [sp, #16]
	lsl	fp, ip, #2
	ldr	r2, [r7, #4]
	ldr	r1, [r1, ip, lsl #2]
	cmp	r2, r1
	bhi	.L2098
	subs	r3, r3, #1
	uxth	r3, r3
.L2045:
	sxth	ip, r3
	cmp	ip, lr
	bgt	.L2051
	b	.L2050
.L2101:
	.align	2
.L2100:
	.word	.LANCHOR0
	.word	.LANCHOR2
.L2098:
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L2047
	cmp	r0, r10
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r8, #1918]	@ movhi
.L2047:
	movs	r1, #0
	uxth	lr, r3
.L2048:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2099
	ldr	r0, [r4, #1840]
	sxth	r2, r2
	adds	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	ldr	r0, [r8, #1920]
	add	r10, r0, r2, lsl #1
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2, lsl #1]	@ movhi
	b	.L2048
.L2099:
	ldr	r2, [r5, #1840]
	ldr	r1, [r7, #4]
	str	r1, [r2, fp]
	ldr	r2, .L2102
	ldr	r2, [r2, #1920]
	strh	r6, [r2, ip, lsl #1]	@ movhi
.L2050:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2030
	ldrh	r2, [r8, #1892]
	ldrh	r1, [r8, #1918]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2030
	adds	r1, r1, #1
	ldr	r2, [r9, #1840]
	strh	r1, [r8, #1918]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r8, #1920]
.L2092:
	strh	r6, [r2, r3, lsl #1]	@ movhi
	b	.L2030
.L2031:
	ldrh	r1, [r9, #24]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2052
	strh	r6, [r5, #24]	@ movhi
	b	.L2093
.L2052:
	ldrh	r0, [r5, #28]
	cmp	r0, r2
	beq	.L2053
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2053:
	ldr	r2, [r9, #32]
	ldr	r3, [r7, #4]
	cmp	r2, r3
	bcs	.L2054
	ldrh	r3, [r5, #24]
	strh	r6, [r5, #24]	@ movhi
	strh	r3, [r5, #28]	@ movhi
	ldr	r3, [r7, #4]
.L2093:
	str	r3, [r5, #32]
	b	.L2030
.L2054:
	strh	r6, [r5, #28]	@ movhi
	b	.L2030
.L2027:
	ldrb	r1, [r8, #140]	@ zero_extendqisi2
	mov	r0, r6
	cbz	r1, .L2055
.L2094:
	movs	r1, #0
.L2055:
	bl	FtlFreeSysBlkQueueIn
.L2030:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2022
.L2021:
	ldr	r6, [sp, #4]
	adds	r6, r6, #1
	uxth	r3, r6
	str	r3, [sp, #4]
	b	.L2016
.L2058:
	ldr	r3, .L2102+4
	ldr	r5, [r3, #1828]
	ldrh	r2, [r5]
	cbz	r2, .L2060
.L2063:
	ldr	r5, [r0, #1920]
	ldr	r3, .L2102
	ldrh	r2, [r5]
	cbz	r2, .L2061
	b	.L2084
.L2060:
	ldrh	r3, [r3, #1912]
	cmp	r3, #0
	beq	.L2063
	ldr	r6, [r6, #1900]
.L2064:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bcs	.L2063
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L2064
	mov	lr, #0
.L2065:
	ldr	r5, [r0, #1900]
	sxth	r2, r3
	cmp	r2, r5
	bcs	.L2063
	ldr	r5, [r4, #1828]
	subs	r6, r2, r1
	adds	r3, r3, #1
	ldrh	r7, [r5, r2, lsl #1]
	uxth	r3, r3
	strh	r7, [r5, r6, lsl #1]	@ movhi
	ldr	r5, [r4, #1856]
	ldr	r7, [r5, r2, lsl #2]
	str	r7, [r5, r6, lsl #2]
	ldr	r5, [r4, #1828]
	strh	lr, [r5, r2, lsl #1]	@ movhi
	b	.L2065
.L2061:
	ldrh	r1, [r3, #1918]
	cbz	r1, .L2084
	ldrh	r6, [r3, #1892]
.L2068:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bge	.L2084
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L2068
	ldr	r5, .L2102
	mov	ip, #0
.L2069:
	ldrh	r6, [r0, #1892]
	sxth	r2, r3
	cmp	r2, r6
	bge	.L2084
	ldr	r6, [r5, #1920]
	rsb	lr, r1, r2
	adds	r3, r3, #1
	ldrh	r7, [r6, r2, lsl #1]
	uxth	r3, r3
	strh	r7, [r6, lr, lsl #1]	@ movhi
	ldr	r6, [r4, #1840]
	ldr	r7, [r6, r2, lsl #2]
	str	r7, [r6, lr, lsl #2]
	ldr	r6, [r5, #1920]
	strh	ip, [r6, r2, lsl #1]	@ movhi
	b	.L2069
.L2084:
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2103:
	.align	2
.L2102:
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r1, #0
	ldr	r4, .L2132
	ldr	r7, .L2132+4
	ldr	r3, [r4, #1776]
	mov	r5, r4
	ldrh	r2, [r7, #1812]
	ldr	r0, [r4, #-220]
	str	r3, [r4, #4060]
	ldr	r3, [r4, #1800]
	lsls	r2, r2, #1
	str	r3, [r4, #4064]
	bl	ftl_memset
	ldrh	r0, [r4, #24]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2105
.L2116:
	mov	r0, #-1
	b	.L2106
.L2105:
	movs	r1, #1
	ldr	r10, .L2132+12
	bl	FtlGetLastWrittenPage
	ldrh	r8, [r5, #24]
	mov	r6, r5
	uxth	r4, r0
	adds	r0, r0, #1
	strh	r0, [r5, #26]	@ movhi
.L2107:
	sxth	r3, r4
	cmp	r3, #0
	blt	.L2115
	sxth	r9, r8
	movs	r1, #1
	ldr	r0, .L2132+8
	orr	r3, r3, r9, lsl #10
	str	r3, [r5, #4056]
	ldr	r3, [r5, #1776]
	mov	r2, r1
	ldr	fp, .L2132+4
	str	r3, [r5, #4060]
	bl	FlashReadPages
	ldrb	r3, [r7]	@ zero_extendqisi2
	cbz	r3, .L2108
	ldr	r3, [r6, #4064]
	ldr	r3, [r3, #12]
	cbz	r3, .L2108
	ldr	r0, [r6, #4060]
	ldrh	r1, [fp, #1882]
	str	r3, [sp, #4]
	bl	js_hash
	ldr	r3, [sp, #4]
	cmp	r3, r0
	beq	.L2108
	cbnz	r4, .L2109
	ldrh	r3, [r6, #28]
	cmp	r9, r3
	beq	.L2109
	uxth	r8, r3
	ldrh	r4, [fp, #1874]
	b	.L2111
.L2109:
	mov	r3, #-1
	str	r3, [r5, #4052]
.L2108:
	ldr	r3, [r5, #4052]
	adds	r3, r3, #1
	beq	.L2111
	ldr	r3, [r6, #1776]
	ldr	r3, [r3]
	cmp	r3, r10
	bne	.L2111
	ldr	r3, [r6, #1800]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2111
.L2115:
	ldr	r4, .L2132+4
	movs	r2, #48
	ldr	r1, [r5, #4060]
	addw	r0, r4, #4068
	ldr	r6, .L2132
	bl	ftl_memcpy
	ldrh	r2, [r7, #1812]
	ldr	r1, [r5, #4060]
	ldr	r0, [r5, #-220]
	adds	r1, r1, #48
	lsls	r2, r2, #1
	bl	ftl_memcpy
	ldrh	r2, [r7, #1812]
	ldr	r3, [r5, #4060]
	add	r1, r2, #24
	ldr	r0, [r5, #-44]
	ubfx	r1, r1, #1, #30
	lsrs	r2, r2, #3
	adds	r2, r2, #4
	add	r1, r3, r1, lsl #2
	bl	ftl_memcpy
	ldrh	r3, [r7, #1916]
	cbz	r3, .L2113
	ldrh	r2, [r4, #1812]
	ldr	r1, [r6, #4060]
	ldr	r0, [r6, #1852]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldrh	r2, [r4, #1908]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	lsls	r2, r2, #2
	add	r1, r1, r3, lsl #2
	bl	ftl_memcpy
	b	.L2113
.L2111:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L2107
.L2113:
	ldr	r2, [r7, #4068]
	ldr	r3, .L2132+12
	ldr	r6, .L2132+4
	cmp	r2, r3
	bne	.L2116
	ldrb	r1, [r6, #4078]	@ zero_extendqisi2
	ldrh	r2, [r6, #1826]
	ldrh	r3, [r6, #4076]
	cmp	r1, r2
	ldr	r4, .L2132
	strh	r3, [r5, #30]	@ movhi
	bne	.L2116
	ldrh	r2, [r6, #1872]
	ldrh	r1, [r6, #1878]
	ldrh	r0, [r6, #1942]
	muls	r2, r3, r2
	str	r3, [r4, #4088]
	str	r2, [r6, #1932]
	muls	r2, r1, r2
	ldrh	r1, [r6, #1804]
	str	r2, [r6, #1912]
	ldr	r2, [r6, #1816]
	subs	r0, r2, r0
	subs	r0, r0, r3
	bl	__aeabi_uidiv
	ldrh	r3, [r6, #4084]
	ldrh	r1, [r6, #4082]
	lsrs	r2, r3, #6
	strh	r2, [r4, #-198]	@ movhi
	ldrh	r2, [r6, #4086]
	and	r3, r3, #63
	strb	r3, [r4, #-194]
	ldrb	r3, [r6, #4079]	@ zero_extendqisi2
	strh	r2, [r4, #-152]	@ movhi
	ldrh	r2, [r6, #4088]
	strb	r3, [r4, #-192]
	movw	r3, #65535
	strh	r1, [r4, #-200]	@ movhi
	strh	r3, [r4, #40]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #42]	@ movhi
	strb	r3, [r4, #46]
	strb	r3, [r4, #48]
	str	r3, [r4, #-20]
	strh	r0, [r4, #20]	@ movhi
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #-146]
	ldrb	r2, [r6, #4080]	@ zero_extendqisi2
	strh	r0, [r4, #-150]	@ movhi
	strb	r2, [r4, #-144]
	ldrh	r2, [r6, #4090]
	strh	r2, [r4, #-104]	@ movhi
	ldrh	r2, [r6, #4092]
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #-98]
	ldrb	r2, [r6, #4081]	@ zero_extendqisi2
	strh	r0, [r4, #-102]	@ movhi
	strb	r2, [r4, #-96]
	movw	r2, #4100
	str	r3, [r4, #-32]
	ldr	r2, [r6, r2]
	str	r3, [r4, #-40]
	str	r3, [r4, #-24]
	stmia	r4, {r2, r3}
	str	r3, [r4, #12]
	str	r3, [r4, #-28]
	movw	r3, #4108
	ldr	r3, [r6, r3]
	ldr	r2, [r4, #-8]
	cmp	r3, r2
	it	hi
	strhi	r3, [r4, #-8]
	movw	r3, #4104
	ldr	r2, [r5, #-4]
	ldr	r3, [r7, r3]
	cmp	r3, r2
	itt	hi
	ldrhi	r2, .L2132
	strhi	r3, [r2, #-4]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2119
	ldr	r0, .L2132+16
	bl	make_superblock
.L2119:
	ldrh	r2, [r5, #-152]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2120
	ldr	r0, .L2132+20
	bl	make_superblock
.L2120:
	ldrh	r2, [r5, #-104]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2121
	ldr	r0, .L2132+24
	bl	make_superblock
.L2121:
	ldrh	r2, [r5, #40]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2122
	ldr	r0, .L2132+28
	bl	make_superblock
.L2122:
	movs	r0, #0
.L2106:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2133:
	.align	2
.L2132:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+4052
	.word	1179929683
	.word	.LANCHOR2-200
	.word	.LANCHOR2-152
	.word	.LANCHOR2-104
	.word	.LANCHOR2+40
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	1
	.global	FtlDumpBlockInfo
	.thumb
	.thumb_func
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2151
	ubfx	r0, r0, #10, #16
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r3, [r3]
	mov	r8, r1
	ldr	r4, .L2151+4
	ldr	r7, .L2151+8
	str	r3, [sp, #100]
	bl	P2V_block_in_plane
	ldr	r1, .L2151+12
	ldrh	r9, [r4, #1872]
	mov	r6, r0
	ldr	r0, .L2151+16
	bl	printk
	ldr	r3, [r7, #-220]
	mov	r1, r6
	ldr	r0, .L2151+20
	ldrh	r2, [r3, r6, lsl #1]
	bl	printk
	add	r0, sp, #104
	strh	r6, [r0, #-52]!	@ movhi
	bl	make_superblock
	ldrb	r5, [r4, #140]	@ zero_extendqisi2
	str	r7, [sp, #44]
	cbz	r5, .L2135
	cmp	r8, #0
	bne	.L2147
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r5, r0
	bne	.L2135
	ldrh	r9, [r4, #1874]
	b	.L2135
.L2147:
	movs	r5, #0
.L2135:
	ldr	r0, .L2151+24
	mov	r1, r5
	mov	r2, r9
	ldrh	r3, [r4, #1872]
	bl	printk
	ldr	r10, .L2151+4
	movs	r6, #0
.L2136:
	ldr	r3, .L2151+8
	movs	r2, #0
	ldrh	fp, [r10, #1804]
	add	lr, sp, #66
	ldrh	r0, [r10, #1884]
	mov	r8, #36
	ldr	r3, [r3, #1764]
	mov	r4, r2
	movw	ip, #65535
	str	r3, [sp, #28]
	ldr	r3, .L2151+8
	ldr	r3, [r3, #628]
	str	r3, [sp, #32]
	ldrh	r3, [r10, #1882]
	str	r3, [sp, #36]
	ldr	r3, .L2151+8
	ldr	r3, [r3, #632]
	str	r3, [sp, #40]
.L2137:
	uxth	r3, r2
	cmp	r3, fp
	bcs	.L2149
	ldrh	r3, [lr, #2]!
	cmp	r3, ip
	beq	.L2138
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
.L2138:
	adds	r2, r2, #1
	b	.L2137
.L2149:
	ldr	r3, .L2151+8
	mov	r1, r4
	mov	r2, r5
	mov	r8, #0
	mov	fp, #36
	ldr	r0, [r3, #1764]
	bl	FlashReadPages
.L2142:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2150
	ldr	r3, [sp, #44]
	mul	r2, fp, r8
	ldrh	r1, [sp, #52]
	ldr	ip, [r3, #1764]
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
	ldr	r0, .L2151+28
	str	r3, [sp, #20]
	ldr	r2, [ip, r2]
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2142
.L2150:
	adds	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2136
.L2144:
	ldr	r3, .L2151
	movs	r0, #0
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2145
	bl	__stack_chk_fail
.L2145:
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2152:
	.align	2
.L2151:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+16
	.word	.LC107
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.fnend
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
	.align	1
	.global	FtlScanAllBlock
	.thumb
	.thumb_func
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #100
	sub	sp, sp, #100
	ldr	r5, .L2170
	mov	r8, #0
	ldr	r0, .L2170+4
	ldr	r1, .L2170+8
	ldr	r3, [r5]
	str	r3, [sp, #92]
	bl	printk
	str	r5, [sp, #36]
	ldr	r5, .L2170+12
	mov	r7, r5
.L2154:
	ldr	r3, .L2170+16
	uxth	r4, r8
	ldrh	r3, [r3, #1814]
	cmp	r3, r4
	bls	.L2166
	add	r9, sp, #96
	mov	r0, r4
	mov	fp, #36
	movw	r10, #65535
	strh	r4, [r9, #-52]!	@ movhi
	bl	ftl_get_blk_mode
	ldr	r2, [r5, #-220]
	mov	r1, r4
	ldrh	r2, [r2, r4, lsl #1]
	mov	r3, r0
	ldr	r0, .L2170+20
	bl	printk
	mov	r0, r9
	bl	make_superblock
	ldr	r3, .L2170+16
	movs	r2, #0
	ldr	r9, [r5, #632]
	ldrh	lr, [r3, #1804]
	mov	r4, r2
	ldr	r3, [r5, #1764]
	str	r3, [sp, #24]
	add	ip, sp, #58
	ldr	r3, [r5, #628]
	str	r3, [sp, #28]
	ldr	r3, .L2170+16
	ldrh	r0, [r3, #1882]
	ldrh	r3, [r3, #1884]
	str	r3, [sp, #32]
.L2155:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L2167
	ldrh	r3, [ip, #2]!
	cmp	r3, r10
	beq	.L2156
	ldr	r1, [sp, #24]
	lsls	r3, r3, #10
	ldr	r6, [sp, #28]
	mla	r1, fp, r4, r1
	str	r3, [r1, #4]
	mov	r3, r0
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r6
	str	r3, [r1, #8]
	ldr	r3, [sp, #32]
	muls	r3, r4, r3
	add	r4, r4, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r4, r4
	add	r3, r3, r9
	str	r3, [r1, #12]
.L2156:
	adds	r2, r2, #1
	b	.L2155
.L2167:
	ldr	r0, [r7, #1764]
	mov	r1, r4
	movs	r2, #0
	mov	r9, #0
	bl	FlashReadPages
	mov	r10, #36
.L2160:
	uxth	r3, r9
	cmp	r3, r4
	bcs	.L2168
	mul	r2, r10, r9
	ldr	ip, [r7, #1764]
	ldrh	r1, [sp, #44]
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
	ldr	r0, .L2170+24
	str	r3, [sp, #20]
	ldr	r2, [ip, r2]
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2160
.L2168:
	ldr	r0, [r7, #1764]
	mov	r1, r4
	movs	r2, #1
	mov	r9, #0
	bl	FlashReadPages
	mov	r10, #36
.L2162:
	uxth	r3, r9
	cmp	r3, r4
	bcs	.L2169
	mul	r2, r10, r9
	ldr	ip, [r7, #1764]
	ldrh	r1, [sp, #44]
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
	ldr	r0, .L2170+28
	str	r3, [sp, #20]
	ldr	r2, [ip, r2]
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2162
.L2169:
	add	r8, r8, #1
	b	.L2154
.L2166:
	ldr	r3, [sp, #36]
	movs	r0, #0
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2165
	bl	__stack_chk_fail
.L2165:
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2171:
	.align	2
.L2170:
	.word	__stack_chk_guard
	.word	.LC107
	.word	.LANCHOR3+36
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC111
	.word	.LC112
	.word	.LC113
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #24
	sub	sp, sp, #24
	ldr	r6, .L2178
	movs	r7, #0
	ldr	r0, .L2178+4
	ldr	r4, .L2178+8
	ldrh	r1, [r6, #1870]
	ldrh	r2, [r6, #1872]
	bl	printk
.L2173:
	ldrh	r3, [r6, #1870]
	cmp	r7, r3
	bcs	.L2175
	lsl	r8, r7, #10
	movs	r5, #0
.L2176:
	ldrh	r3, [r6, #1872]
	cmp	r5, r3
	bcs	.L2177
	add	r3, r5, r8
	str	r3, [r4, #4056]
	ldr	r3, [r4, #1776]
	movs	r2, #0
	movs	r1, #1
	ldr	r0, .L2178+12
	str	r2, [r4, #4052]
	adds	r5, r5, #1
	str	r3, [r4, #4060]
	ldr	r3, [r4, #1800]
	str	r3, [r4, #4064]
	bl	FlashReadPages
	ldr	r3, [r4, #4064]
	ldr	r2, [r4, #4060]
	ldr	r0, .L2178+16
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	ldr	r1, [r4, #4056]
	str	r2, [sp, #16]
	ldr	r2, [r4, #4052]
	ldr	r3, [r3]
	bl	printk
	b	.L2176
.L2177:
	adds	r7, r7, #1
	b	.L2173
.L2175:
	ldr	r0, .L2178+20
	ldr	r1, .L2178+24
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, lr}
	b	printk
.L2179:
	.align	2
.L2178:
	.word	.LANCHOR0
	.word	.LC114
	.word	.LANCHOR2
	.word	.LANCHOR2+4052
	.word	.LC115
	.word	.LC116
	.word	.LANCHOR3+52
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
	ldr	r5, .L2236
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	r5, [r5]
	str	r5, [sp, #68]
	ldr	r5, .L2236+4
	ldr	r6, [r5, #692]
	ldrb	r7, [r5]	@ zero_extendqisi2
	ldrb	r6, [r6, #19]	@ zero_extendqisi2
	str	r6, [sp, #16]
	ldr	r6, .L2236+8
	ldrb	r6, [r6, #481]	@ zero_extendqisi2
	str	r6, [sp, #12]
	cbz	r7, .L2181
	bl	FlashProgSlc2KPages
	b	.L2182
.L2195:
	movs	r3, #36
	add	r2, sp, #24
	mov	r1, r9
	mul	r8, r3, r7
	ldr	r3, [sp, #8]
	subs	r3, r3, r7
	add	fp, r4, r8
	uxtb	r3, r3
	str	r3, [sp]
	mov	r0, fp
	add	r3, sp, #28
	bl	LogAddr2PhyAddr
	ldrb	r2, [r5, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #28]
	mov	r10, r0
	cmp	r3, r2
	bcc	.L2183
	mov	r3, #-1
	str	r3, [r4, r8]
.L2184:
	adds	r7, r7, #1
.L2181:
	ldr	r3, [sp, #8]
	ldr	r6, .L2236+4
	cmp	r7, r3
	bcc	.L2195
	b	.L2234
.L2183:
	add	r3, r5, r3, lsl #4
	ldrb	r1, [r5, #1793]	@ zero_extendqisi2
	ldr	r3, [r3, #1596]
	cmp	r1, #0
	it	eq
	moveq	r10, #0
	cbz	r3, .L2186
	cmp	r2, #1
	bne	.L2187
	ldr	r0, [r5, #76]
	bl	NandcIqrWaitFlashReady
.L2187:
	ldrb	r0, [sp, #28]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L2186:
	ldr	r3, [sp, #28]
	movs	r0, #0
	add	r2, r5, r3, lsl #4
	addw	r1, r2, #1596
	str	r0, [r2, #1600]
	ldr	r0, [sp, #24]
	str	fp, [r2, #1596]
	str	r0, [r2, #1592]
	cmp	r10, #0
	beq	.L2188
	add	r2, r8, #36
	add	r2, r2, r4
	str	r2, [r1, #4]
.L2188:
	adds	r2, r5, r3
	add	r3, r5, r3, lsl #4
	ldrb	r6, [r2, #1720]	@ zero_extendqisi2
	strb	r6, [r3, #1588]
	mov	r0, r6
	ldrb	r3, [r5, #1717]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2189
	bl	NandcWaitFlashReady
	b	.L2190
.L2189:
	bl	NandcFlashCs
	mov	r0, r6
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #24]
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #656]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	bl	NandcFlashDeCs
.L2190:
	ldr	r3, [sp, #16]
	subs	r3, r3, #1
	cmp	r3, #7
	bhi	.L2191
	adds	r3, r5, r6
	ldrb	r3, [r3, #1552]	@ zero_extendqisi2
	cbz	r3, .L2191
	mov	r0, r6
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	ldr	r2, .L2236+12
	movs	r3, #0
	bl	HynixSetRRPara
.L2191:
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r6
	bne	.L2192
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cbz	r3, .L2192
	bl	flash_enter_slc_mode
	b	.L2193
.L2192:
	bl	flash_exit_slc_mode
.L2193:
	mov	r0, r6
	ldr	r1, [sp, #24]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r0, r6
	movs	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L2194
	mov	r0, r6
	ldr	r1, [sp, #24]
	bl	FlashProgDpFirstCmd
	mov	r0, r6
	add	r8, r8, #36
	add	r8, r8, r4
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #24]
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #656]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r5, #4]
	mov	r0, r6
	ldr	r3, [sp, #24]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r8, #12]
	mov	r0, r6
	movs	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r8, #8]
	bl	NandcXferData
.L2194:
	mov	r0, r6
	ldr	r1, [sp, #24]
	bl	FlashProgSecondCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2184
.L2234:
	ldr	r0, [r6, #76]
	movs	r5, #0
	bl	NandcIqrWaitFlashReady
	addw	r8, r6, #1588
	mov	r7, r6
.L2196:
	ldrb	r3, [r6, #1717]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L2235
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2197
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbz	r3, .L2197
	lsls	r3, r5, #4
	ldrb	r0, [r8, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2197:
	adds	r5, r5, #1
	b	.L2196
.L2235:
	ldr	r3, [sp, #20]
	cbnz	r3, .L2199
.L2207:
	movs	r0, #0
	b	.L2182
.L2199:
	ldr	r7, .L2236+16
	mov	r8, #0
	mov	r10, r7
.L2200:
	ldr	r3, [sp, #8]
	cmp	r8, r3
	beq	.L2207
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L2201
	ldr	r1, [r4, #4]
	ldr	r0, .L2236+20
	bl	printk
	b	.L2202
.L2201:
	ldr	r3, [sp, #8]
	mov	r1, r9
	add	r2, sp, #24
	mov	r0, r4
	rsb	r3, r8, r3
	mov	r6, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #28
	bl	LogAddr2PhyAddr
	ldr	r2, [r7, #4024]
	movs	r3, #0
	str	r3, [r2]
	ldr	r2, [r7, #4028]
	str	r3, [r2]
	ldmia	r6!, {r0, r1, r2, r3}
	add	r5, sp, #32
	stmia	r5!, {r0, r1, r2, r3}
	ldmia	r6!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	add	r0, sp, #32
	ldr	r3, [r6]
	movs	r1, #1
	mov	r2, r9
	str	r3, [r5]
	ldr	r3, [r7, #4024]
	str	r3, [sp, #40]
	ldr	r3, [r7, #4028]
	str	r3, [sp, #44]
	bl	FlashReadPages
	ldr	r5, [sp, #32]
	adds	r3, r5, #1
	bne	.L2203
	ldr	r0, .L2236+24
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L2203:
	ldr	r3, [r4, #12]
	cbz	r3, .L2204
	ldr	r2, [r3]
	ldr	r3, [r10, #4028]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2204
	ldr	r0, .L2236+28
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2204:
	ldr	r3, [r4, #8]
	cbz	r3, .L2202
	ldr	r2, [r3]
	ldr	r3, [r10, #4024]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2202
	ldr	r0, .L2236+32
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2202:
	add	r8, r8, #1
	adds	r4, r4, #36
	b	.L2200
.L2182:
	ldr	r3, .L2236
	ldr	r2, [sp, #68]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2208
	bl	__stack_chk_fail
.L2208:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2237:
	.align	2
.L2236:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+702
	.word	.LANCHOR2
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
	ldr	r3, .L2242
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #104
	sub	sp, sp, #104
	ldr	r2, [r3]
	mov	r4, r0
	ldr	r5, .L2242+4
	mov	r6, r3
	str	r2, [sp, #100]
	ldr	r2, [r5, #4008]
	cmp	r0, r2
	bcc	.L2241
	ldr	r3, [r5, #4016]
	add	r0, sp, #36
	movs	r1, #165
	movs	r2, #32
	str	r0, [sp, #12]
	lsls	r4, r4, #10
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r0, [r5, #4016]
	movs	r1, #90
	movs	r2, #8
	bl	ftl_memset
	movs	r1, #1
	mov	r2, r1
	mov	r0, sp
	str	r4, [sp, #4]
	bl	FlashEraseBlocks
	movs	r1, #1
	mov	r2, r1
	mov	r3, r1
	mov	r0, sp
	bl	FlashProgPages
	movs	r1, #0
	movs	r2, #1
	ldr	r4, [sp]
	mov	r0, sp
	adds	r4, r4, #0
	it	ne
	movne	r4, #1
	negs	r4, r4
	bl	FlashEraseBlocks
	b	.L2239
.L2241:
	movs	r4, #0
.L2239:
	ldr	r2, [sp, #100]
	mov	r0, r4
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2240
	bl	__stack_chk_fail
.L2240:
	add	sp, sp, #104
	@ sp needed
	pop	{r4, r5, r6, pc}
.L2243:
	.align	2
.L2242:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	1
	.global	FlashMakeFactorBbt
	.thumb
	.thumb_func
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2303
	movs	r1, #1
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r3, [r3]
	ldr	r5, .L2303+4
	ldr	r4, .L2303+8
	str	r3, [sp, #84]
	ldr	r3, [r5, #4020]
	ldrh	r8, [r4, #124]
	ldr	r0, .L2303+12
	str	r3, [sp, #20]
	ldrh	r3, [r4, #126]
	smulbb	r8, r8, r3
	ldr	r3, [r4, #692]
	ldrb	r3, [r3, #24]	@ zero_extendqisi2
	uxth	r8, r8
	str	r3, [sp, #24]
	ldrh	r3, [r4, #4]
	str	r3, [sp, #16]
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r4, .L2303+8
	cmp	r3, #1
	itttt	eq
	ldreq	r3, [sp, #16]
	lsleq	r3, r3, #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r5, #4020]
	movs	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r5, .L2303+4
	lsr	r3, r8, #4
	str	r3, [sp, #28]
	movs	r3, #0
	str	r3, [sp, #8]
.L2246:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2299
	add	r3, r5, r7, lsl #1
	ldrh	r6, [r3, #4036]
	cmp	r6, #0
	bne	.L2277
	ldrh	r2, [r4, #132]
	mov	r1, r6
	ldr	r0, [r5, #3988]
	mov	r9, r6
	lsls	r2, r2, #9
	bl	ftl_memset
	adds	r3, r4, r7
	ldrb	r10, [r3, #1720]	@ zero_extendqisi2
	lsls	r3, r7, #2
	add	fp, r4, r3
	str	r6, [sp, #4]
	str	r3, [sp, #32]
.L2248:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L2258
	movs	r3, #255
	strb	r3, [sp, #46]
	strb	r3, [sp, #47]
	ldr	r3, [sp, #24]
	lsls	r1, r3, #31
	bpl	.L2250
	ldr	r3, [fp, #656]
	add	r2, sp, #46
	mov	r0, r10
	add	r3, r3, r6
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L2250
	ldr	r1, [r4, #4]
	add	r2, sp, #47
	mov	r0, r10
	add	r1, r1, r3
	bl	FlashReadSpare
	ldrb	r3, [sp, #46]	@ zero_extendqisi2
	ldrb	r2, [sp, #47]	@ zero_extendqisi2
	ands	r3, r3, r2
	strb	r3, [sp, #46]
.L2250:
	ldr	r3, [sp, #24]
	lsls	r2, r3, #30
	bpl	.L2252
	ldr	r3, [r4, #692]
	mov	r0, r10
	add	r2, sp, #47
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #656]
	subs	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2252:
	ldr	r3, [r4, #692]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	ldrb	r3, [sp, #46]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L2253
	cmp	r2, #8
	bne	.L2254
.L2253:
	cbz	r3, .L2276
	ldrb	r0, [sp, #47]	@ zero_extendqisi2
	clz	r0, r0
	lsrs	r0, r0, #5
	b	.L2255
.L2254:
	cmp	r3, #255
	bne	.L2276
	ldrb	r0, [sp, #47]	@ zero_extendqisi2
	subs	r0, r0, #255
	it	ne
	movne	r0, #1
	b	.L2255
.L2276:
	movs	r0, #1
.L2255:
	ldr	r3, [sp, #24]
	lsls	r3, r3, #29
	bpl	.L2256
	ldr	r3, .L2303+8
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #656]
	add	r1, r1, r6
	bl	SandiskProgTestBadBlock
.L2256:
	cbz	r0, .L2257
	mov	r1, r7
	ldr	r2, [sp, #4]
	ldr	r0, .L2303+16
	add	r9, r9, #1
	bl	printk
	ldr	r2, [r5, #3988]
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
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	muls	r3, r2, r3
	cmp	r9, r3
	bgt	.L2258
.L2257:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r6, r6, r3
	b	.L2248
.L2258:
	mov	r2, r9
	ldr	r0, .L2303+20
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	muls	r3, r2, r3
	cmp	r9, r3
	blt	.L2260
	ldrh	r2, [r4, #132]
	movs	r1, #0
	ldr	r0, [r5, #3988]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2260:
	cmp	r7, #0
	bne	.L2262
	ldrh	r10, [r5, #4008]
	mov	r9, #1
	mov	r6, r7
.L2263:
	ldr	r3, .L2303+8
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2300
	mov	r0, r10
	bl	FlashTestBlk
	cbz	r0, .L2264
	mov	r1, r10
	ldr	r0, .L2303+24
	bl	printk
	ldr	r2, [r5, #3988]
	lsr	r0, r10, #5
	adds	r6, r6, #1
	and	r3, r10, #31
	ldr	r1, [r2, r0, lsl #2]
	uxth	r6, r6
	lsl	r3, r9, r3
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2264:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2263
.L2300:
	add	fp, r8, #-1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L2266:
	cmp	fp, r9
	ble	.L2301
	mov	r0, fp
	bl	FlashTestBlk
	cbz	r0, .L2267
	mov	r1, fp
	ldr	r0, .L2303+24
	bl	printk
	ldr	r2, [r5, #3988]
	lsr	r0, fp, #5
	and	r3, fp, #31
	lsl	r3, r10, r3
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2267:
	add	fp, fp, #-1
	uxth	fp, fp
	b	.L2266
.L2301:
	ldr	r3, .L2303+8
	ldr	r2, [r5, #4008]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	subs	r3, r3, r2
	cmp	r6, r3
	bcc	.L2262
	ldrh	r2, [r4, #132]
	movs	r1, #0
	ldr	r0, [r5, #3988]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2262:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	add	r10, r8, #-1
	add	r9, r5, r7, lsl #1
	uxth	r10, r10
	mul	r6, r8, r6
.L2270:
	mov	r1, r7
	ldr	r0, .L2303+28
	mov	r2, r10
	bl	printk
	ldr	r1, [r5, #3988]
.L2271:
	lsr	r3, r10, #5
	and	r2, r10, #31
	ldr	r3, [r1, r3, lsl #2]
	lsrs	r3, r3, r2
	ands	r3, r3, #1
	beq	.L2302
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2271
.L2302:
	ldr	r1, [sp, #20]
	movw	r2, #61664
	strh	r10, [r9, #4036]	@ movhi
	add	r0, sp, #48
	strh	r2, [r1]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	movs	r1, #1
	ldr	r3, [r5, #3988]
	mov	r2, r1
	str	r3, [sp, #56]
	ldr	r3, [r5, #4020]
	str	r3, [sp, #60]
	add	r3, r10, r6
	lsls	r3, r3, #10
	str	r3, [sp, #52]
	bl	FlashEraseBlocks
	movs	r1, #1
	mov	r3, r1
	mov	r2, r1
	add	r0, sp, #48
	bl	FlashProgPages
	ldr	r3, [sp, #48]
	cbz	r3, .L2277
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2270
.L2277:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2246
.L2304:
	.align	2
.L2303:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC117
	.word	.LC118
	.word	.LC119
	.word	.LC120
	.word	.LC121
.L2299:
	ldr	r3, .L2305
	ldr	r2, [sp, #84]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2274
	bl	__stack_chk_fail
.L2274:
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2306:
	.align	2
.L2305:
	.word	__stack_chk_guard
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
	ldr	r3, .L2359
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldrb	r2, [r3, #1793]	@ zero_extendqisi2
	mov	fp, r3
	str	r0, [sp, #8]
	str	r1, [sp, #4]
	str	r2, [sp, #20]
	ldr	r2, [r3, #4052]
	cmp	r2, #0
	bne	.L2336
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	mov	r10, #36
	ldr	r7, .L2359+4
	mov	r6, r2
	mov	r5, r2
	mov	r4, r2
	mov	r9, fp
	str	r3, [sp, #12]
	str	r0, [r7, #1752]
.L2309:
	ldrh	r0, [fp, #1804]
	uxth	r1, r6
	ldr	r8, .L2359
	cmp	r0, r1
	bls	.L2354
	mul	lr, r10, r1
	ldr	r0, [r9, #4056]
	add	r1, r1, r9
	movs	r3, #0
	str	r3, [r0, lr]
	ldrb	r0, [r1, #1832]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	bl	V2P_block
	ldr	r3, [sp, #4]
	mov	r8, r0
	cbz	r3, .L2310
	bl	IsBlkInVendorPart
	cbnz	r0, .L2311
.L2310:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2312
	ldr	r3, [fp, #4056]
	lsl	r1, r8, #10
	mla	lr, r10, r5, r3
	str	r1, [lr, #4]
	str	r0, [lr, #8]
	ldrh	r1, [fp, #1884]
	muls	r1, r5, r1
	ldr	r0, [r7, #1804]
	it	mi
	addmi	r1, r1, #3
	adds	r5, r5, #1
	bic	r1, r1, #3
	add	r1, r1, r0
	str	r1, [lr, #12]
	uxth	r5, r5
	b	.L2311
.L2312:
	adds	r4, r4, #1
	uxth	r4, r4
.L2311:
	adds	r6, r6, #1
	b	.L2309
.L2354:
	cmp	r5, #0
	beq	.L2335
	ldr	r3, [sp, #12]
	mov	r2, r5
	ldr	r0, [r8, #4056]
	movs	r7, #0
	adds	r6, r3, #0
	strb	r7, [r8, #1793]
	it	ne
	movne	r6, #1
	mov	r1, r6
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	strb	r3, [r8, #1793]
	mov	r8, #36
.L2316:
	uxth	r2, r7
	cmp	r2, r5
	bcs	.L2355
	mul	r2, r8, r7
	ldr	r1, [fp, #4056]
	adds	r0, r1, r2
	ldr	r2, [r1, r2]
	adds	r2, r2, #1
	bne	.L2317
	ldr	r0, [r0, #4]
	adds	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2317:
	adds	r7, r7, #1
	b	.L2316
.L2355:
	ldr	r3, [sp, #4]
	cbnz	r3, .L2319
	movs	r3, #6
	uxth	r6, r6
	str	r3, [sp, #16]
	movs	r3, #1
	str	r3, [sp, #12]
	b	.L2320
.L2319:
	ldrh	r3, [fp, #1874]
	str	r3, [sp, #12]
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	cbnz	r3, .L2337
	ldr	r3, [sp, #12]
	movs	r6, #1
	lsrs	r3, r3, #2
	str	r3, [sp, #16]
	b	.L2320
.L2337:
	movs	r6, #1
	str	r6, [sp, #16]
.L2320:
	ldr	r9, .L2359
	mov	r8, #0
.L2330:
	mov	r10, #0
	ldr	fp, .L2359+4
	mov	r5, r10
.L2321:
	ldrh	r2, [r9, #1804]
	uxth	r3, r10
	ldr	r7, .L2359
	cmp	r2, r3
	bls	.L2356
	movs	r2, #36
	ldr	r1, [r9, #4056]
	movs	r0, #0
	muls	r2, r3, r2
	add	r3, r3, r9
	str	r0, [r1, r2]
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #1832]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp, #4]
	mov	r7, r0
	cbz	r3, .L2322
	bl	IsBlkInVendorPart
	cbnz	r0, .L2323
.L2322:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2323
	ldr	r2, [r9, #4056]
	movs	r3, #36
	ldr	r1, .L2359+4
	add	r7, r8, r7, lsl #10
	mla	r2, r3, r5, r2
	ldr	r3, [fp, #1792]
	ldr	r1, [r1, #1796]
	str	r3, [r2, #8]
	ldrh	r3, [r9, #1884]
	muls	r3, r5, r3
	add	r5, r5, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	str	r7, [r2, #4]
	uxth	r5, r5
	add	r3, r3, r1
	str	r3, [r2, #12]
.L2323:
	add	r10, r10, #1
	b	.L2321
.L2356:
	cmp	r5, #0
	beq	.L2335
	ldr	r3, .L2359
	movs	r2, #0
	mov	r1, r5
	mov	r10, #0
	mov	fp, #36
	strb	r2, [r3, #1793]
	mov	r2, r6
	ldr	r0, [r3, #4056]
	movs	r3, #1
	bl	FlashProgPages
	ldr	r3, .L2359
	ldrb	r2, [sp, #20]	@ zero_extendqisi2
	strb	r2, [r3, #1793]
.L2327:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2357
	mul	r3, fp, r10
	ldr	r2, .L2359
	ldr	r2, [r2, #4056]
	adds	r0, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L2328
	ldr	r0, [r0, #4]
	adds	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2328:
	add	r10, r10, #1
	b	.L2327
.L2357:
	ldr	r3, [sp, #16]
	add	r8, r8, r3
	ldr	r3, [sp, #12]
	uxth	r8, r8
	cmp	r8, r3
	bcc	.L2330
	mov	r8, #0
	mov	r9, #36
.L2331:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L2358
	ldr	r3, [sp, #4]
	cbz	r3, .L2332
	mul	r3, r9, r8
	ldr	r2, [r7, #4056]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L2332
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2332:
	add	r8, r8, #1
	b	.L2331
.L2358:
	ldr	r3, [sp, #8]
	cmp	r3, #63
	bls	.L2334
	ldr	r3, [sp, #4]
	cbz	r3, .L2335
.L2334:
	ldr	r0, [r7, #4056]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L2335:
	mov	r0, r4
	b	.L2308
.L2336:
	movs	r0, #0
.L2308:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2360:
	.align	2
.L2359:
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r7, .L2378
	ldr	r6, [r7, #4052]
	cmp	r6, #0
	bne	.L2363
	ldr	r4, .L2378+4
	mov	r1, r6
	ldrh	r2, [r7, #1882]
	add	r8, r7, #1960
	ldr	r0, [r4, #1776]
	ldr	r3, [r4, #1800]
	str	r0, [r4, #4060]
	str	r3, [r4, #4064]
	bl	ftl_memset
.L2364:
	ldrh	r3, [r7, #1826]
	ldr	r5, .L2378
	cmp	r6, r3
	bge	.L2377
	ldrh	r2, [r4, #1864]
	ldr	r3, [r4, #4060]
	ldr	r1, [r8, #4]!
	mul	r0, r2, r6
	lsls	r2, r2, #2
	adds	r6, r6, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L2364
.L2377:
	ldr	r6, [r4, #4064]
	movs	r1, #255
	movs	r2, #16
	mov	r8, #0
	ldr	r10, .L2378+4
	mov	r0, r6
	mov	r9, r8
	bl	ftl_memset
	movw	r3, #61649
	strh	r3, [r6]	@ movhi
	ldr	r3, [r5, #1944]
	str	r3, [r6, #4]
	ldrh	r3, [r5, #1936]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r5, #1940]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r5, #1942]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r5, #1800]
	strh	r3, [r6, #12]	@ movhi
.L2366:
	ldr	r3, [r4, #1776]
	mov	fp, #0
	ldrh	r1, [r7, #1936]
	ldrh	r2, [r7, #1938]
	str	r3, [r4, #4060]
	ldr	r3, [r4, #1800]
	str	fp, [r4, #4052]
	str	r3, [r4, #4064]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #4056]
	ldrh	r3, [r7, #1940]
	str	r0, [sp]
	ldr	r0, .L2378+8
	bl	printk
	ldrh	r3, [r7, #1874]
	ldrh	r2, [r7, #1938]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L2367
	ldr	r3, [r5, #1944]
	movs	r1, #1
	ldrh	r2, [r5, #1936]
	adds	r3, r3, #1
	ldr	r0, [r5, #4056]
	str	r3, [r5, #1944]
	str	r3, [r6, #4]
	ldrh	r3, [r5, #1940]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r5, #1940]	@ movhi
	mov	r2, r1
	strh	r3, [r5, #1936]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r10, #4056]
	str	r3, [r0, #4]
	strh	fp, [r5, #1938]	@ movhi
	bl	FlashEraseBlocks
.L2367:
	movs	r1, #1
	ldr	fp, .L2378+4
	mov	r2, r1
	mov	r3, r1
	addw	r0, fp, #4052
	bl	FlashProgPages
	ldrh	r3, [r7, #1938]
	adds	r3, r3, #1
	strh	r3, [r7, #1938]	@ movhi
	ldr	r3, [r4, #4052]
	adds	r2, r3, #1
	bne	.L2368
	add	r8, r8, #1
	ldr	r0, .L2378+12
	ldr	r1, [r10, #4056]
	uxth	r8, r8
	bl	printk
	cmp	r8, #3
	bls	.L2366
	mov	r2, r8
	ldr	r0, .L2378+16
	ldr	r1, [fp, #4056]
	bl	printk
	ldr	r3, .L2378
	movs	r2, #1
	str	r2, [r3, #4052]
	b	.L2363
.L2368:
	add	r9, r9, #1
	cmp	r9, #1
	beq	.L2366
	cmp	r3, #256
	beq	.L2366
.L2363:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2379:
	.align	2
.L2378:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC122
	.word	.LC123
	.word	.LC124
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
	mov	r4, r0
	ldr	r5, .L2436
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r5, #4052]
	cmp	r3, #0
	bne	.L2381
	ldr	r9, .L2436+16
	mov	r10, r5
.L2382:
	ldr	r3, .L2436+4
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r4, r3
	add	r6, r3, #104
	bne	.L2383
	ldrh	lr, [r9, #-204]
	ldr	r7, [r9, #616]
	lsr	r1, lr, #1
	mul	r0, r7, lr
	adds	r3, r1, #1
	add	r3, r3, r0, lsr #2
	ldr	r0, [r5, #1796]
	uxth	r3, r3
	cbz	r0, .L2406
	ldr	r0, [r9, #16]
	cmp	r0, #39
	bhi	.L2406
	cmp	r0, #2
	bls	.L2411
	tst	lr, #1
	beq	.L2384
	cbz	r7, .L2408
	b	.L2384
.L2383:
	cmp	r2, #1
	bne	.L2411
	ldrh	r3, [r5, #1824]
	cmp	r3, #1
	beq	.L2411
	ldrb	r3, [r10, #140]	@ zero_extendqisi2
	cbnz	r3, .L2411
	ldrh	r0, [r9, #-204]
	ldr	r3, [r10, #1796]
	lsrs	r1, r0, #3
	cbz	r3, .L2384
	ldr	r3, [r9, #16]
	cmp	r3, #1
	bhi	.L2384
	rsb	r0, r0, r0, lsl #3
	ubfx	r1, r0, #3, #16
	b	.L2384
.L2406:
	mov	r1, r3
.L2384:
	cbz	r1, .L2385
	subs	r1, r1, #1
	uxth	r1, r1
	b	.L2385
.L2408:
	mov	r1, r7
	b	.L2385
.L2411:
	movs	r1, #0
.L2385:
	ldr	r0, .L2436+8
	bl	List_pop_index_node
	ldrh	r3, [r6, #-204]
	subs	r3, r3, #1
	strh	r3, [r6, #-204]	@ movhi
	ldrh	r3, [r5, #1812]
	uxth	r7, r0
	cmp	r3, r7
	bls	.L2382
	ldr	r3, [r9, #-220]
	ldrh	r8, [r3, r7, lsl #1]
	cmp	r8, #0
	bne	.L2382
	strh	r7, [r4]	@ movhi
	mov	r0, r4
	bl	make_superblock
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cbz	r3, .L2387
	ldr	r0, [r10, #4056]
	movs	r2, #36
	ldrh	ip, [r10, #1804]
	add	lr, r4, #14
	mov	r3, r0
	mla	r1, r2, ip, r0
	mov	ip, r8
	str	r1, [sp]
	b	.L2388
.L2387:
	ldr	r3, [r9, #-220]
	b	.L2431
.L2388:
	ldr	r1, [sp]
	cmp	r3, r1
	beq	.L2433
	str	ip, [r3, #8]
	movw	fp, #65535
	str	ip, [r3, #12]
	adds	r3, r3, #36
	ldrh	r1, [lr, #2]!
	cmp	r1, fp
	itttt	ne
	lslne	r1, r1, #10
	mlane	fp, r2, r8, r0
	addne	r8, r8, #1
	uxthne	r8, r8
	it	ne
	strne	r1, [fp, #4]
	b	.L2388
.L2433:
	ldr	r3, [r5, #1796]
	cbz	r3, .L2391
	ldr	r3, .L2436+12
	cmp	r4, r3
	bne	.L2391
	ldr	r3, [r10, #4060]
	ldrh	r3, [r3, r7, lsl #1]
	cmp	r3, #40
	itt	hi
	movhi	r3, #0
	strbhi	r3, [r9, #-192]
.L2391:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldr	r2, [r5, #4060]
	cbnz	r3, .L2392
	ldrh	r1, [r2, r7, lsl #1]
	cbz	r1, .L2412
	ldrh	r3, [r10, #1864]
	add	r3, r3, r1
	uxth	r3, r3
	b	.L2393
.L2412:
	movs	r3, #2
.L2393:
	strh	r3, [r2, r7, lsl #1]	@ movhi
	mov	r0, r7
	ldr	r3, [r6]
	movs	r1, #0
	adds	r3, r3, #1
	str	r3, [r6]
	bl	ftl_set_blk_mode
	b	.L2394
.L2392:
	ldrh	r3, [r2, r7, lsl #1]
	lsrs	r0, r7, #5
	and	r1, r7, #31
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	ldr	r3, [r6, #4]
	ldr	r2, [r6, #-44]
	adds	r3, r3, #1
	str	r3, [r6, #4]
	movs	r3, #1
	lsls	r3, r3, r1
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2394:
	ldr	r3, [r5, #4060]
	ldr	r2, [r6, #12]
	ldrh	r1, [r5, #1812]
	ldrh	r3, [r3, r7, lsl #1]
	cmp	r3, r2
	ldrh	r2, [r5, #1864]
	it	hi
	strhi	r3, [r9, #12]
	ldmia	r6, {r0, r3}
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r6, #1820]
	ldr	r1, [r5, #4056]
	ldr	r3, [r2, #16]
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r3, #36
	adds	r2, r1, #4
	mla	r3, r3, r8, r1
	adds	r3, r3, #40
	str	r0, [r6, #8]
.L2396:
	adds	r2, r2, #36
	cmp	r2, r3
	beq	.L2434
	ldr	r1, [r2, #-36]
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r2, #-36]
	b	.L2396
.L2434:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cbz	r3, .L2398
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	mov	r2, r8
	ldr	r0, [r10, #4056]
	cmp	r3, #1
	ite	eq
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2398:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	mov	r2, r8
	ldr	r0, [r5, #4056]
	mov	fp, #0
	bl	FlashEraseBlocks
	add	r1, r4, #16
	mov	ip, #36
	mov	r2, fp
.L2400:
	uxth	r3, fp
	cmp	r3, r8
	bcs	.L2435
	mul	r3, ip, fp
	ldr	lr, [r5, #4056]
	add	r0, lr, r3
	ldr	r3, [lr, r3]
	cmp	r3, #-1
	bne	.L2401
	ldr	r0, [r0, #4]
	adds	r2, r2, #1
	str	ip, [sp, #12]
	ubfx	r0, r0, #10, #16
	str	r3, [sp, #8]
	str	r1, [sp, #4]
	str	r2, [sp]
	bl	FtlBbmMapBadBlock
	ldr	r3, [sp, #8]
	ldr	r1, [sp, #4]
	ldr	ip, [sp, #12]
	ldr	r2, [sp]
	strh	r3, [r1]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	subs	r3, r3, #1
	strb	r3, [r4, #7]
.L2401:
	add	fp, fp, #1
	adds	r1, r1, #2
	b	.L2400
.L2435:
	cbz	r2, .L2403
	mov	r0, r7
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2403:
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cbnz	r3, .L2404
	ldr	r3, [r6, #-220]
.L2431:
	movw	r2, #65535
	strh	r2, [r3, r7, lsl #1]	@ movhi
	b	.L2382
.L2404:
	ldrh	r2, [r5, #1872]
	strh	r7, [r4]	@ movhi
	smulbb	r3, r2, r3
	movs	r2, #0
	strh	r2, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	ldr	r2, [r6, #-8]
	uxth	r3, r3
	strh	r3, [r4, #4]	@ movhi
	str	r2, [r4, #12]
	adds	r2, r2, #1
	str	r2, [r6, #-8]
	ldr	r2, [r6, #-220]
	ldrh	r1, [r4]
	strh	r3, [r2, r1, lsl #1]	@ movhi
.L2381:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2437:
	.align	2
.L2436:
	.word	.LANCHOR0
	.word	.LANCHOR2-104
	.word	.LANCHOR2-208
	.word	.LANCHOR2-200
	.word	.LANCHOR2
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
	ldr	r5, .L2451
	str	r0, [sp]
	ldrh	r3, [r5, #666]
	cmp	r3, #0
	beq	.L2440
	ldr	r7, .L2451+4
	mov	r10, #0
	mov	r8, r5
.L2439:
	ldrh	r2, [r7, #1804]
	uxth	r3, r10
	cmp	r2, r3
	bls	.L2448
	add	r3, r3, r7
	ldr	r1, [sp]
	mov	fp, #0
	ldrb	r0, [r3, #1832]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r2, .L2451+8
	mov	r9, r0
.L2441:
	ldrh	r3, [r5, #666]
	uxth	r4, fp
	cmp	r3, r4
	bls	.L2449
	lsls	r6, r4, #1
	add	r3, r8, r6
	ldrh	r3, [r3, #668]
	cmp	r3, r9
	bne	.L2442
	mov	r1, r9
	ldr	r0, .L2451+12
	str	r2, [sp, #4]
	bl	printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r8, #666]
	ldr	r2, [sp, #4]
	adds	r3, r2, r6
.L2443:
	cmp	r4, r1
	bcs	.L2450
	ldrh	r0, [r3, #2]
	adds	r4, r4, #1
	uxth	r4, r4
	strh	r0, [r3], #2	@ movhi
	b	.L2443
.L2450:
	subs	r1, r1, #1
	strh	r1, [r5, #666]	@ movhi
.L2442:
	add	fp, fp, #1
	b	.L2441
.L2449:
	add	r10, r10, #1
	b	.L2439
.L2448:
	bl	FtlGcReFreshBadBlk
.L2440:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2452:
	.align	2
.L2451:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+668
	.word	.LC125
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
	ldr	r2, .L2462
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, [r2, #-220]
	mov	r5, r2
	ldrh	r3, [r3, r0, lsl #1]
	cbnz	r3, .L2454
	ldrh	r1, [r2, #40]
	cmp	r1, r0
	bne	.L2455
	movw	r3, #65535
	strh	r3, [r2, #40]	@ movhi
	b	.L2456
.L2455:
	ldrh	r1, [r2, #-200]
	cmp	r1, r0
	beq	.L2461
	ldrh	r1, [r2, #-152]
	cmp	r1, r0
	beq	.L2461
	ldrh	r2, [r2, #-104]
	cmp	r2, r0
	beq	.L2461
.L2456:
	mov	r1, r4
	ldr	r0, .L2462+4
	bl	List_remove_node
	ldrh	r3, [r5, #-212]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #-212]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	movs	r0, #1
	pop	{r3, r4, r5, pc}
.L2454:
	bl	List_update_data_list
.L2461:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L2463:
	.align	2
.L2462:
	.word	.LANCHOR2
	.word	.LANCHOR2-224
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
	ldr	r5, .L2474
	beq	.L2465
	ldr	r3, [r5, #-220]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L2466
	mov	r1, r4
	ldr	r0, .L2474+4
	bl	printk
	ldr	r3, [r5, #-220]
	movs	r2, #32
	sub	r0, r5, #208
	mov	r1, r4
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	test_node_in_list
	cbz	r0, .L2467
	mov	r1, r4
	sub	r0, r5, #208
	bl	List_remove_node
	ldrh	r3, [r5, #-204]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #-204]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #-220]
	ldr	r0, .L2474+8
	mov	r1, r4
	ldrh	r2, [r3, r4, lsl #1]
	bl	printk
.L2467:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2470
.L2466:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L2465:
	ldrh	r0, [r5, #1920]
	movw	r3, #65535
	ldr	r6, .L2474
	cmp	r0, r3
	bne	.L2469
	strh	r4, [r6, #1920]	@ movhi
	b	.L2470
.L2469:
	cmp	r0, r4
	beq	.L2470
	bl	update_vpc_list
	strh	r4, [r6, #1920]	@ movhi
	adds	r0, r0, #0
	it	ne
	movne	r0, #1
	pop	{r4, r5, r6, pc}
.L2470:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L2475:
	.align	2
.L2474:
	.word	.LANCHOR2
	.word	.LC126
	.word	.LC127
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
	beq	.L2476
	ldrh	r3, [r0]
	movw	r5, #65535
	cmp	r3, r5
	beq	.L2476
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r7, .L2488
	adds	r3, r3, #8
	ldr	r6, .L2488+4
	ldrh	r3, [r0, r3, lsl #1]
.L2480:
	cmp	r3, r5
	bne	.L2487
.L2482:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r7, #1804]
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
	b	.L2480
.L2487:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2483
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbnz	r3, .L2483
	ldrh	r3, [r4, #2]
	add	r3, r6, r3, lsl #1
	ldrh	r3, [r3, #704]
	cmp	r3, r5
	bne	.L2483
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2482
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L2483:
	ldr	r2, .L2488
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	cbz	r3, .L2476
	cmp	r1, #1
	bne	.L2476
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #1874]
	cmp	r1, r3
	bcc	.L2476
	ldr	r3, .L2488+4
	ldrh	r0, [r4]
	ldrh	r5, [r4, #4]
	ldr	r1, [r3, #-220]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r5
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #1872]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2476:
	pop	{r3, r4, r5, r6, r7, pc}
.L2489:
	.align	2
.L2488:
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
	ldr	r8, .L2507+4
	adds	r3, r3, #8
	ldr	r5, .L2507
	ldrh	r2, [r0, r3, lsl #1]
.L2491:
	cmp	r2, r7
	ldr	r6, .L2507+4
	bne	.L2506
.L2492:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r8, #1804]
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
	b	.L2491
.L2506:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r1, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2494
	ldrb	r0, [r8, #140]	@ zero_extendqisi2
	cbnz	r0, .L2494
	add	r0, r5, r1, lsl #1
	ldrh	r0, [r0, #704]
	cmp	r0, r7
	bne	.L2494
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2492
.L2494:
	movw	r7, #65535
	orr	r5, r1, r2, lsl #10
	mov	r8, r7
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2495:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r1, [r6, #1804]
.L2497:
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
	beq	.L2497
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2502
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbnz	r3, .L2499
	ldrh	r2, [r4, #2]
	ldr	r3, .L2507
	add	r3, r3, r2, lsl #1
	ldrh	r3, [r3, #704]
	cmp	r3, r8
	bne	.L2499
	ldrh	r3, [r4, #4]
	cbz	r3, .L2499
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2495
.L2499:
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	ldr	r2, .L2507+4
	cbz	r3, .L2502
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #1874]
	cmp	r1, r3
	bcc	.L2502
	ldr	r3, .L2507
	ldrh	r0, [r4]
	ldrh	r6, [r4, #4]
	ldr	r1, [r3, #-220]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r6
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #1872]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2502:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L2508:
	.align	2
.L2507:
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
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r5, .L2532
	ldr	r3, [r5, #4052]
	cmp	r3, #0
	bne	.L2511
	ldr	r4, .L2532+4
	movs	r1, #255
	ldr	r2, [r4, #1776]
	ldr	r6, [r4, #1800]
	str	r2, [r4, #4060]
	ldrh	r2, [r4, #24]
	str	r6, [r4, #4064]
	str	r3, [r6, #12]
	strh	r2, [r6, #2]	@ movhi
	movw	r2, #61604
	strh	r2, [r6]	@ movhi
	ldr	r2, [r4, #32]
	str	r3, [r6, #8]
	ldr	r3, .L2532+8
	str	r2, [r6, #4]
	ldrh	r2, [r4, #-198]
	str	r3, [r5, #4068]
	ldr	r3, .L2532+12
	str	r3, [r5, #4072]
	ldrh	r3, [r4, #30]
	strh	r3, [r5, #4076]	@ movhi
	ldrh	r3, [r5, #1826]
	strb	r3, [r5, #4078]
	ldrh	r3, [r4, #-200]
	strh	r3, [r5, #4082]	@ movhi
	ldrb	r3, [r4, #-194]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r5, #4084]	@ movhi
	ldrb	r3, [r4, #-192]	@ zero_extendqisi2
	ldrh	r2, [r4, #-150]
	strb	r3, [r5, #4079]
	ldrh	r3, [r4, #-152]
	strh	r3, [r5, #4086]	@ movhi
	ldrb	r3, [r4, #-146]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r5, #4088]	@ movhi
	ldrb	r3, [r4, #-144]	@ zero_extendqisi2
	strb	r3, [r5, #4080]
	ldrh	r3, [r4, #-104]
	strh	r3, [r5, #4090]	@ movhi
	ldrh	r2, [r4, #-102]
	ldrb	r3, [r4, #-98]	@ zero_extendqisi2
	ldr	r0, [r4, #4060]
	orr	r3, r3, r2, lsl #6
	strh	r3, [r5, #4092]	@ movhi
	ldrb	r3, [r4, #-96]	@ zero_extendqisi2
	ldr	r2, [r4]
	strb	r3, [r5, #4081]
	movw	r3, #4100
	str	r2, [r5, r3]
	movw	r3, #4108
	ldr	r2, [r4, #-8]
	str	r2, [r5, r3]
	movw	r3, #4104
	ldr	r2, [r4, #-4]
	str	r2, [r5, r3]
	movw	r3, #4112
	ldrh	r2, [r4, #658]
	strh	r2, [r5, r3]	@ movhi
	movw	r3, #4114
	ldrh	r2, [r4, #660]
	strh	r2, [r5, r3]	@ movhi
	ldrh	r2, [r5, #1882]
	bl	ftl_memset
	addw	r1, r5, #4068
	movs	r2, #48
	ldr	r0, [r4, #4060]
	bl	ftl_memcpy
	ldrh	r2, [r5, #1812]
	ldr	r0, [r4, #4060]
	ldr	r1, [r4, #-220]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5, #1812]
	ldr	r0, [r4, #4060]
	add	r3, r2, #24
	ldr	r1, [r4, #-44]
	ubfx	r3, r3, #1, #30
	lsrs	r2, r2, #3
	adds	r2, r2, #4
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
	ldrh	r3, [r5, #1916]
	cbz	r3, .L2512
	ldrh	r2, [r5, #1812]
	ldr	r0, [r4, #4060]
	ldr	r1, [r4, #1848]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldrh	r2, [r5, #1908]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	lsls	r2, r2, #2
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
.L2512:
	movs	r0, #0
	ldr	r7, .L2532+4
	bl	FtlUpdateVaildLpn
	ldr	r10, .L2532
	mov	r8, #0
	movw	r9, #65535
.L2513:
	ldr	r3, [r4, #1776]
	ldrh	r2, [r4, #24]
	ldrh	r1, [r4, #26]
	str	r3, [r4, #4060]
	ldr	r3, [r4, #1800]
	str	r3, [r4, #4064]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #4056]
	ldrh	r3, [r5, #1874]
	subs	r3, r3, #1
	cmp	r1, r3
	blt	.L2514
	movs	r3, #0
	ldrh	r9, [r7, #28]
	strh	r3, [r7, #26]	@ movhi
	strh	r2, [r7, #28]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r7, #-8]
	adds	r2, r3, #1
	str	r2, [r7, #-8]
	str	r3, [r7, #32]
	lsls	r2, r0, #10
	strh	r0, [r7, #24]	@ movhi
	str	r2, [r7, #4056]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
.L2514:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cbz	r3, .L2515
	ldr	r0, [r4, #1776]
	ldrh	r1, [r10, #1882]
	bl	js_hash
	str	r0, [r6, #12]
.L2515:
	movs	r1, #1
	ldr	r0, .L2532+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #26]
	ldr	r2, [r4, #4052]
	adds	r3, r3, #1
	adds	r1, r2, #1
	uxth	r3, r3
	strh	r3, [r4, #26]	@ movhi
	bne	.L2516
	cmp	r3, #1
	add	r8, r8, #1
	it	eq
	ldrheq	r3, [r5, #1874]
	uxth	r8, r8
	itt	eq
	addeq	r3, r3, #-1
	strheq	r3, [r7, #26]	@ movhi
	cmp	r8, #3
	bls	.L2513
	ldr	r0, .L2532+20
	mov	r2, r8
	ldr	r1, [r4, #4056]
	bl	printk
	movs	r3, #1
	str	r3, [r5, #4052]
	b	.L2511
.L2516:
	cmp	r3, #1
	beq	.L2513
	cmp	r2, #256
	beq	.L2513
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2511
	mov	r0, r9
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2511:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2533:
	.align	2
.L2532:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177366
	.word	.LANCHOR2+4052
	.word	.LC128
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	1
	.global	FtlSuperblockPowerLostFix
	.thumb
	.thumb_func
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2551
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r2, [r3]
	mov	r4, r0
	str	r3, [sp, #4]
	str	r2, [sp, #44]
	ldr	r2, .L2551+4
	ldr	r5, [r2, #4052]
	mov	r8, r2
	cmp	r5, #0
	bne	.L2534
	ldrb	r6, [r2, #140]	@ zero_extendqisi2
	cbz	r6, .L2550
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	bne	.L2546
	ldrh	r9, [r0, #4]
	b	.L2536
.L2546:
	mov	r6, r5
.L2550:
	mov	r9, #12
.L2536:
	ldr	r5, .L2551+8
	mov	r3, #-1
	str	r3, [sp, #24]
	mov	r10, #0
	movw	r2, #61589
	ldr	r7, [r5, #1800]
	mov	fp, r5
	ldr	r3, [r5, #1776]
	str	r7, [sp, #20]
	str	r3, [sp, #16]
	mvn	r3, #2
	str	r3, [r7, #8]
	mvn	r3, #1
	str	r3, [r7, #12]
	ldrh	r3, [r4]
	strh	r10, [r7]	@ movhi
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #1776]
	str	r2, [r3]
	add	r2, r2, #304087040
	ldr	r3, [r5, #1776]
	add	r2, r2, #1269760
	addw	r2, r2, #1507
	str	r2, [r3, #4]
.L2537:
	adds	r9, r9, #-1
	bcc	.L2540
	ldrh	r3, [r4, #4]
	cbnz	r3, .L2538
.L2540:
	ldr	r2, [r5, #-220]
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r8, #1872]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L2534
.L2538:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #12]
	adds	r0, r0, #1
	beq	.L2540
	ldr	r3, [r5, #-4]
	movs	r1, #1
	add	r0, sp, #8
	str	r3, [r7, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, r6
	it	eq
	moveq	r3, r10
	str	r3, [fp, #-4]
	movs	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2537
.L2534:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2544
	bl	__stack_chk_fail
.L2544:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2552:
	.align	2
.L2551:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r9, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	subs	r3, r3, #4
	ldr	r8, .L2566+12
	cmp	r2, r3
	blt	.L2554
	uxth	r0, r0
	ldrh	r10, [r5, r0, lsl #1]
	cmp	r10, #0
	beq	.L2554
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2554
	movs	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r8, #1874]
	cmp	r2, r3
	bcc	.L2555
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2555:
	ldr	r5, .L2566
	mov	fp, #0
.L2556:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2565
	ldr	r2, [r9, r6, lsl #2]
	add	r3, r9, r6, lsl #2
	cmp	r10, r2, lsr #10
	bne	.L2557
	ldr	r2, [r5, #1780]
	movs	r1, #1
	ldr	r7, [r5, #1800]
	ldr	r0, .L2566+4
	str	r2, [r5, #4060]
	str	r7, [r5, #4064]
	ldr	r2, [r9, r6, lsl #2]
	str	r3, [sp, #4]
	str	r2, [r5, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #4052]
	adds	r2, r2, #1
	ldr	r3, [sp, #4]
	bne	.L2558
.L2560:
	movs	r2, #0
	str	r2, [r3]
	ldr	r3, .L2566
	ldr	r0, .L2566+8
	ldrh	r2, [r7, #8]
	ldr	r1, [r3, #4056]
	bl	printk
	movs	r3, #1
	str	r3, [r8, #4052]
	b	.L2559
.L2558:
	ldrh	r1, [r7, #8]
	cmp	r1, r6
	bne	.L2560
	ldrh	r0, [r7]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2560
	mov	r0, r4
	ldr	r2, [r5, #4060]
	bl	FtlMapWritePage
.L2557:
	add	fp, fp, #1
	b	.L2556
.L2565:
	mov	r0, r10
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L2554:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, #1874]
	cmp	r2, r3
	bcc	.L2559
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2559:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2567:
	.align	2
.L2566:
	.word	.LANCHOR2
	.word	.LANCHOR2+4052
	.word	.LC129
	.word	.LANCHOR0
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
	ldr	r2, .L2579
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, r0
	ldr	r6, [r2, #4052]
	mov	r8, r2
	ldr	r5, [r0, #12]
	ldr	r10, [r0, #24]
	cmp	r6, #0
	bne	.L2569
	ldrh	r3, [r0]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L2570
	ldrh	r3, [r0, #8]
	adds	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r5]	@ movhi
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	adds	r3, r3, #1
	strh	r6, [r4]	@ movhi
	str	r3, [r4, #28]
	b	.L2569
.L2570:
	ldrh	r9, [r5, r3, lsl #1]
	movs	r1, #255
	ldr	r5, .L2579+4
	ldrh	r3, [r0, #2]
	orr	r3, r3, r9, lsl #10
	str	r3, [r5, #4056]
	ldr	r3, [r5, #1776]
	ldr	r7, [r5, #1800]
	str	r3, [r5, #4060]
	ldr	r3, [r0, #28]
	str	r7, [r5, #4064]
	str	r3, [r7, #4]
	movw	r3, #64245
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r9, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	ldrh	r2, [r8, #1874]
	ldr	r0, [r5, #1776]
	lsls	r2, r2, #3
	bl	ftl_memset
	mov	r3, r6
.L2571:
	ldrh	r1, [r4, #6]
	uxth	r2, r6
	cmp	r1, r2
	bls	.L2578
	ldr	r1, [r10, r2, lsl #2]
	cmp	r9, r1, lsr #10
	bne	.L2572
	adds	r3, r3, #1
	ldr	r1, [r5, #1776]
	uxth	r3, r3
	str	r2, [r1, r3, lsl #3]
	ldr	r1, [r10, r2, lsl #2]
	ldr	r2, [r5, #1776]
	add	r2, r2, r3, lsl #3
	str	r1, [r2, #4]
.L2572:
	adds	r6, r6, #1
	b	.L2571
.L2578:
	ldrb	r3, [r8]	@ zero_extendqisi2
	cbz	r3, .L2574
	ldr	r3, .L2579
	ldr	r0, [r5, #4060]
	ldrh	r1, [r3, #1882]
	bl	js_hash
	str	r0, [r7, #12]
.L2574:
	movs	r1, #1
	movs	r3, #0
	ldr	r0, .L2579+8
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2569:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2580:
	.align	2
.L2579:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+4052
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
	movs	r4, #0
	ldr	r6, .L2605
	mov	fp, r0
	ldr	r7, .L2605+4
	mov	r10, r1
	str	r2, [sp]
	mov	r5, r6
.L2582:
	ldr	r3, [r6, #-24]
	ldr	r9, .L2605+4
	adds	r3, r3, #1
	str	r3, [r6, #-24]
	ldrh	r3, [r7, #1874]
	ldrh	r2, [fp, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L2583
	ldrh	r2, [fp]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2584
.L2583:
	mov	r0, fp
	bl	Ftl_write_map_blk_to_last_page
.L2584:
	ldr	r1, [r9, #4052]
	cmp	r1, #0
	bne	.L2600
	ldrh	r2, [fp]
	ldr	r3, [fp, #12]
	ldr	r0, [r5, #1800]
	ldrh	r8, [r3, r2, lsl #1]
	movs	r2, #16
	ldrh	r3, [fp, #2]
	str	r0, [r5, #4064]
	orr	r3, r3, r8, lsl #10
	str	r3, [r5, #4056]
	ldr	r3, [sp]
	str	r3, [r5, #4060]
	bl	ftl_memset
	ldr	r2, [fp, #28]
	ldr	r3, [r5, #4064]
	str	r2, [r3, #4]
	strh	r10, [r3, #8]	@ movhi
	ldrh	r2, [fp, #4]
	strh	r8, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrb	r2, [r7]	@ zero_extendqisi2
	cbz	r2, .L2586
	ldr	r0, [r6, #4060]
	ldrh	r1, [r7, #1882]
	str	r3, [sp, #4]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2586:
	movs	r1, #1
	ldr	r0, .L2605+8
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [fp, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [fp, #2]	@ movhi
	ldr	r2, [r5, #4052]
	adds	r1, r2, #1
	bne	.L2587
	ldr	r0, .L2605+12
	adds	r4, r4, #1
	ldr	r1, [r6, #4056]
	bl	printk
	ldrh	r3, [fp, #2]
	uxth	r4, r4
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r9, #1874]
	addls	r3, r3, #-1
	strhls	r3, [fp, #2]	@ movhi
	cmp	r4, #3
	bls	.L2582
	ldr	r3, .L2605
	mov	r2, r4
	ldr	r0, .L2605+16
	ldr	r1, [r3, #4056]
	bl	printk
	movs	r3, #1
	str	r3, [r9, #4052]
	b	.L2600
.L2587:
	cbz	r2, .L2590
	cmp	r3, #1
	strh	r8, [fp, #40]	@ movhi
	bne	.L2591
.L2592:
	movs	r3, #0
	str	r3, [fp, #36]
	b	.L2582
.L2591:
	cmp	r2, #256
	b	.L2604
.L2590:
	cmp	r3, #1
.L2604:
	beq	.L2592
	ldr	r3, [fp, #36]
	cmp	r3, #0
	bne	.L2592
	ldr	r3, .L2605
	ldr	r2, [r3, #4056]
	ldr	r3, [fp, #24]
	str	r2, [r3, r10, lsl #2]
.L2600:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2606:
	.align	2
.L2605:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+4052
	.word	.LC130
	.word	.LC131
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
	ldr	r5, .L2608
	muls	r4, r0, r4
	addw	r0, r5, #1868
	ldr	r3, [r5, #-56]
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-56]
	movs	r0, #0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L2609:
	.align	2
.L2608:
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r3, [r0, #24]
	mov	r4, r0
	ldrh	r10, [r0, #6]
	movs	r1, #0
	ldr	r7, .L2652
	str	r3, [sp]
	ldr	r3, [r0, #12]
	lsl	r2, r10, #2
	ldr	r8, .L2652+8
	mov	r5, r7
	mov	r9, r7
	str	r3, [sp, #4]
	ldr	r3, [r0, #16]
	str	r3, [sp, #16]
	ldrh	r3, [r0, #8]
	ldr	r0, [sp]
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r3, [r7, #1776]
	movs	r2, #1
	ldr	r6, [r7, #1800]
	str	r2, [r4, #36]
	str	r3, [r7, #4060]
	movw	r3, #65535
	str	r6, [r7, #4064]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	str	r3, [r4, #32]
	str	r3, [r4, #28]
	str	r3, [sp, #8]
.L2611:
	ldrh	r2, [sp, #8]
	ldr	r3, [sp, #12]
	ldr	fp, .L2652+8
	sxth	r7, r2
	cmp	r7, r3
	bge	.L2629
	ldr	r3, [sp, #12]
	subs	r1, r3, #1
	cmp	r7, r1
	lsl	r1, r7, #1
	bne	.L2612
	ldr	r3, [sp, #4]
	mov	r8, #0
	str	r2, [sp, #12]
	add	r3, r3, r1
	str	r3, [sp, #8]
	ldr	r3, [sp, #4]
	movs	r1, #1
	ldrh	r0, [r3, r7, lsl #1]
	bl	FtlGetLastWrittenPage
	ldr	r2, [sp, #12]
	adds	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	sxth	r0, r0
	ldr	r3, [sp, #16]
	add	r9, r0, #1
	strh	r2, [r4]	@ movhi
	ldr	r3, [r3, r7, lsl #2]
	ldr	r7, .L2652
	str	r3, [r4, #28]
.L2613:
	sxth	r2, r8
	cmp	r2, r9
	bge	.L2629
	ldr	r3, [sp, #8]
	ldr	r0, .L2652+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r5, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldrb	r2, [fp]	@ zero_extendqisi2
	cbz	r2, .L2614
	ldr	r2, [r7, #4064]
	ldr	r2, [r2, #12]
	cbz	r2, .L2614
	ldr	r1, .L2652+8
	ldr	r0, [r7, #4060]
	str	r2, [sp, #4]
	ldrh	r1, [r1, #1882]
	bl	js_hash
	ldr	r2, [sp, #4]
	cmp	r2, r0
	itt	ne
	movne	r2, #-1
	strne	r2, [r7, #4052]
.L2614:
	ldr	r2, [r7, #4052]
	adds	r2, r2, #1
	beq	.L2615
	ldrh	r2, [r6, #8]
	cmp	r2, r10
	bcs	.L2615
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r7, #4056]
	ldreq	r3, [sp]
	streq	r1, [r3, r2, lsl #2]
.L2615:
	add	r8, r8, #1
	b	.L2613
.L2629:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [fp, #1874]
	cmp	r2, r3
	bne	.L2618
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2618
.L2612:
	ldr	r2, [r5, #1776]
	ldr	r3, [sp, #4]
	ldr	r0, .L2652+4
	str	r2, [r5, #4060]
	add	fp, r3, r1
	ldrh	r2, [r8, #1874]
	ldrh	r1, [r3, r7, lsl #1]
	subs	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r5, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #4052]
	adds	r2, r2, #1
	beq	.L2631
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2631
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L2631
	movs	r0, #0
	mov	lr, #8
	mov	ip, #4
.L2620:
	uxth	r2, r0
	ldrh	r1, [r8, #1874]
	sxth	r7, r2
	subs	r1, r1, #1
	cmp	r7, r1
	bge	.L2623
	ldr	r1, [r5, #1776]
	adds	r0, r0, #1
	ldrh	r7, [r1, r7, lsl #3]
	cmp	r7, r10
	itttt	cc
	smlabbcc	r2, r2, lr, ip
	ldrcc	r3, [sp]
	ldrcc	r2, [r1, r2]
	strcc	r2, [r3, r7, lsl #2]
	b	.L2620
.L2631:
	movs	r7, #0
.L2651:
	ldrh	r1, [r8, #1874]
	sxth	r2, r7
	cmp	r2, r1
	bge	.L2623
	ldrh	r1, [fp]
	ldr	r0, .L2652+4
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r9, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldrb	r2, [r8]	@ zero_extendqisi2
	cbz	r2, .L2624
	ldr	r2, [r9, #4064]
	ldr	r2, [r2, #12]
	cbz	r2, .L2624
	ldr	r0, [r9, #4060]
	ldrh	r1, [r8, #1882]
	str	r2, [sp, #20]
	bl	js_hash
	ldr	r2, [sp, #20]
	cmp	r2, r0
	itt	ne
	movne	r2, #-1
	strne	r2, [r9, #4052]
.L2624:
	ldr	r2, .L2652
	ldr	r2, [r2, #4052]
	adds	r2, r2, #1
	beq	.L2625
	ldrh	r2, [r6, #8]
	cmp	r2, r10
	bcs	.L2625
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r9, #4056]
	ldreq	r3, [sp]
	streq	r1, [r3, r2, lsl #2]
.L2625:
	adds	r7, r7, #1
	b	.L2651
.L2623:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2611
.L2618:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2653:
	.align	2
.L2652:
	.word	.LANCHOR2
	.word	.LANCHOR2+4052
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
	movw	r2, #4102
	ldr	r3, .L2655
	ldr	r0, .L2655+4
	ldrh	r1, [r3, #1892]
	strh	r1, [r0, r2]	@ movhi
	add	r2, r0, #4096
	movw	r1, #61574
	strh	r1, [r2]	@ movhi
	movw	r2, #4100
	ldrh	r1, [r3, #1918]
	strh	r1, [r0, r2]	@ movhi
	movw	r2, #4098
	ldrh	r1, [r3, #1894]
	strh	r1, [r0, r2]	@ movhi
	ldr	r2, [r3, #1920]
	movw	r3, #4104
	str	r2, [r0, r3]
	movw	r3, #4108
	ldr	r2, [r0, #1840]
	str	r2, [r0, r3]
	movw	r3, #4112
	ldr	r2, [r0, #1836]
	str	r2, [r0, r3]
	movw	r3, #4116
	ldr	r2, [r0, #1844]
	str	r2, [r0, r3]
	addw	r0, r0, #4092
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2656:
	.align	2
.L2655:
	.word	.LANCHOR0
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
	ldr	r0, .L2658
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2659:
	.align	2
.L2658:
	.word	.LANCHOR2+1868
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	1
	.global	FtlVendorPartWrite
	.thumb
	.thumb_func
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r2, .L2674
	.pad #68
	sub	sp, sp, #68
	mov	r4, r1
	mov	r5, r0
	adds	r6, r0, r4
	ldr	r3, [r2]
	str	r2, [sp, #12]
	str	r3, [sp, #60]
	ldr	r3, .L2674+4
	ldrh	r0, [r3, #1868]
	ldrh	r1, [r3, #1880]
	cmp	r6, r0
	str	r3, [sp, #8]
	bhi	.L2669
	ldr	r7, .L2674+8
	lsr	r6, r5, r1
	mov	fp, #0
	lsls	r3, r6, #2
	str	r3, [sp, #4]
.L2662:
	cmp	r4, #0
	beq	.L2673
	ldr	r3, .L2674+8
	mov	r0, r5
	ldr	r2, [sp, #4]
	ldr	r3, [r3, #1844]
	ldr	ip, [r3, r2]
	ldr	r3, [sp, #8]
	str	ip, [sp, #20]
	ldrh	r2, [r3, #1878]
	mov	r1, r2
	str	r2, [sp, #16]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #16]
	mov	r8, r1
	ldr	ip, [sp, #20]
	subs	r3, r2, r1
	uxth	r9, r3
	cmp	r9, r4
	it	hi
	uxthhi	r9, r4
	cmp	ip, #0
	beq	.L2664
	cmp	r9, r2
	beq	.L2664
	ldr	r2, [r7, #1784]
	movs	r1, #1
	add	r0, sp, #24
	str	ip, [sp, #28]
	str	r2, [sp, #32]
	movs	r2, #0
	str	r2, [sp, #36]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2665
.L2664:
	ldr	r3, [sp, #8]
	movs	r1, #0
	ldr	r0, [r7, #1784]
	ldrh	r2, [r3, #1882]
	bl	ftl_memset
.L2665:
	lsl	ip, r9, #9
	ldr	r0, [r7, #1784]
	uxth	r8, r8
	mov	r1, r10
	mov	r2, ip
	str	ip, [sp, #16]
	add	r0, r0, r8, lsl #9
	rsb	r4, r9, r4
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r0, .L2674+12
	adds	r6, r6, #1
	ldr	r2, [r7, #1784]
	add	r5, r5, r9
	bl	FtlMapWritePage
	ldr	r3, [sp, #4]
	adds	r0, r0, #1
	ldr	ip, [sp, #16]
	it	eq
	moveq	fp, #-1
	adds	r3, r3, #4
	str	r3, [sp, #4]
	add	r10, r10, ip
	b	.L2662
.L2673:
	mov	r0, fp
	b	.L2661
.L2669:
	mov	r0, #-1
.L2661:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2668
	bl	__stack_chk_fail
.L2668:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2675:
	.align	2
.L2674:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+4092
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
	ldr	r2, .L2678
	ldr	r3, .L2678+4
	ldr	r1, [r2, #92]
	cmp	r1, r3
	bne	.L2676
	ldr	r3, .L2678+8
	movs	r0, #0
	movs	r1, #1
	adds	r2, r2, #92
	str	r3, [r2, #4]
	ldr	r3, [r2, #-108]
	str	r3, [r2, #88]
	ldr	r3, [r2, #-104]
	str	r3, [r2, #92]
	ldr	r3, [r2, #-112]
	str	r3, [r2, #8]
	ldr	r3, [r2, #-124]
	str	r3, [r2, #12]
	ldr	r3, [r2, #-132]
	str	r3, [r2, #16]
	ldr	r3, [r2, #-116]
	str	r3, [r2, #20]
	ldr	r3, [r2, #-88]
	str	r3, [r2, #28]
	ldr	r3, .L2678+12
	ldr	r3, [r3, #4064]
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
	ldr	r3, [r2, #1644]
	str	r3, [r2, #64]
	b	FtlVendorPartWrite
.L2676:
	bx	lr
.L2679:
	.align	2
.L2678:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177366
	.word	.LANCHOR0
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
	ldr	r3, .L2687
	push	{r4, lr}
	.save {r4, lr}
	ldr	r2, [r3, #1796]
	ldr	r3, .L2687+4
	cbz	r2, .L2685
	ldr	r2, [r3, #16]
	cmp	r2, #39
	ite	hi
	movhi	r1, #32
	movls	r1, #4
	b	.L2681
.L2685:
	movs	r1, #32
.L2681:
	ldr	r4, .L2687+8
	ldrh	r2, [r4, #-208]
	cmp	r2, #31
	ittt	ls
	addls	r2, r2, #1
	movls	r1, #1
	strhls	r2, [r4, #-208]	@ movhi
	cbnz	r0, .L2683
	ldr	r0, [r3, #1820]
	ldr	r2, [r0, #20]
	add	r2, r2, r1
	ldr	r1, [r0, #16]
	cmp	r1, r2
	bcc	.L2684
.L2683:
	ldr	r2, [r3, #1820]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2687+12
	str	r1, [r2]
	ldr	r2, [r3, #1820]
	ldrh	r1, [r3, #1812]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2684:
	movs	r0, #0
	pop	{r4, pc}
.L2688:
	.align	2
.L2687:
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
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r2
	ldr	r2, .L2701
	.pad #68
	sub	sp, sp, #68
	mov	r4, r1
	mov	r6, r0
	adds	r5, r0, r4
	ldr	r3, [r2]
	str	r2, [sp, #16]
	str	r3, [sp, #60]
	ldr	r3, .L2701+4
	ldrh	r0, [r3, #1868]
	ldrh	r1, [r3, #1880]
	cmp	r5, r0
	str	r3, [sp, #20]
	bhi	.L2699
	ldr	r8, .L2701+16
	lsr	r5, r6, r1
	lsls	r3, r5, #2
	str	r3, [sp, #8]
	mov	fp, r8
	movs	r3, #0
	str	r3, [sp, #4]
.L2691:
	cmp	r4, #0
	beq	.L2700
	ldr	r3, [r8, #1844]
	mov	r0, r6
	ldr	r2, [sp, #8]
	ldr	r9, [r3, r2]
	ldr	r3, [sp, #20]
	ldrh	r10, [r3, #1878]
	mov	r1, r10
	bl	__aeabi_uidivmod
	rsb	r3, r1, r10
	str	r1, [sp, #12]
	uxth	r10, r3
	cmp	r10, r4
	it	hi
	uxthhi	r10, r4
	cmp	r9, #0
	beq	.L2693
	ldr	r2, [fp, #1784]
	movs	r1, #1
	add	r0, sp, #24
	str	r9, [sp, #28]
	str	r2, [sp, #32]
	movs	r2, #0
	str	r2, [sp, #36]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #4]
	adds	r2, r2, #1
	ldr	r2, [r8, #4052]
	it	eq
	moveq	r3, #-1
	cmp	r2, #256
	str	r3, [sp, #4]
	bne	.L2695
	mov	r1, r5
	mov	r2, r9
	ldr	r0, .L2701+8
	bl	printk
	ldr	r0, .L2701+12
	mov	r1, r5
	ldr	r2, [r8, #1784]
	bl	FtlMapWritePage
.L2695:
	ldrh	r9, [sp, #12]
	mov	r0, r7
	ldr	r1, [fp, #1784]
	lsl	r2, r10, #9
	add	r1, r1, r9, lsl #9
	bl	ftl_memcpy
	b	.L2696
.L2693:
	mov	r0, r7
	mov	r1, r9
	lsl	r2, r10, #9
	bl	ftl_memset
.L2696:
	ldr	r3, [sp, #8]
	adds	r5, r5, #1
	rsb	r4, r10, r4
	add	r6, r6, r10
	adds	r3, r3, #4
	add	r7, r7, r10, lsl #9
	str	r3, [sp, #8]
	b	.L2691
.L2700:
	ldr	r0, [sp, #4]
	b	.L2690
.L2699:
	mov	r0, #-1
.L2690:
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2698
	bl	__stack_chk_fail
.L2698:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2702:
	.align	2
.L2701:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC132
	.word	.LANCHOR2+4092
	.word	.LANCHOR2
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
	ldr	r4, .L2705
	ldr	r2, [r4, #1820]
	ldrh	r1, [r4, #1812]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #1820]
	ldr	r2, [r3]
	ldr	r3, .L2705+4
	cmp	r2, r3
	beq	.L2704
	ldr	r1, .L2705+8
	ldr	r0, .L2705+12
	bl	printk
	ldrh	r2, [r4, #1812]
	ldr	r0, [r4, #1820]
	movs	r1, #0
	lsls	r2, r2, #9
	bl	ftl_memset
.L2704:
	movs	r0, #0
	pop	{r4, pc}
.L2706:
	.align	2
.L2705:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC133
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
	ldr	r4, .L2711
	movs	r1, #1
	ldr	r5, .L2711+4
	add	r2, r4, #92
	bl	FtlVendorPartRead
	ldr	r3, [r4, #92]
	cmp	r3, r5
	beq	.L2708
	add	r0, r4, #92
	movs	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #92]
.L2708:
	ldr	r2, [r4, #92]
	ldr	r3, .L2711
	cmp	r2, r5
	ldr	r5, .L2711+8
	bne	.L2709
	ldr	r2, [r3, #180]
	str	r2, [r3, #-16]
	ldr	r2, [r3, #184]
	str	r2, [r3, #-12]
	ldr	r2, [r3, #100]
	str	r2, [r3, #-20]
	ldr	r2, [r3, #104]
	str	r2, [r3, #-32]
	ldr	r2, [r3, #108]
	str	r2, [r3, #-40]
	ldr	r2, [r3, #112]
	str	r2, [r3, #-24]
	ldr	r2, [r3, #120]
	str	r2, [r3, #4]
	ldr	r2, [r3, #124]
	str	r2, [r5, #4064]
	ldr	r2, [r3, #128]
	str	r2, [r3, #-36]
	ldr	r2, [r3, #132]
	str	r2, [r3, #-28]
	ldr	r2, [r3, #136]
	str	r2, [r3, #12]
	ldr	r2, [r3, #140]
	str	r2, [r3, #16]
	ldr	r2, [r3, #152]
	str	r2, [r3, #88]
.L2709:
	movs	r3, #0
	ldr	r2, [r4, #160]
	str	r3, [r4, #1736]
	ldr	r3, .L2711+12
	cmp	r2, r3
	bne	.L2710
	ldr	r0, .L2711+16
	movs	r3, #1
	ldr	r1, .L2711+20
	str	r3, [r5, #1796]
	bl	printk
.L2710:
	ldrh	r2, [r5, #1864]
	ldmia	r4, {r0, r3}
	ldrh	r1, [r5, #1812]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #8]
	pop	{r3, r4, r5, pc}
.L2712:
	.align	2
.L2711:
	.word	.LANCHOR2
	.word	1179929683
	.word	.LANCHOR0
	.word	305432421
	.word	.LC76
	.word	.LC134
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
	cbz	r3, .L2713
	ldr	r2, .L2719
	movs	r3, #0
	str	r3, [r0, #36]
	ldr	r3, [r2, #4052]
	cbnz	r3, .L2713
	ldr	r3, .L2719+4
	subs	r5, r5, #1
	uxth	r5, r5
	ldr	r4, [r3, #1800]
	ldr	r0, [r3, #1780]
	str	r4, [r3, #4064]
	mov	r4, r3
	str	r0, [r3, #4060]
	ldr	r1, [r1, r5, lsl #2]
	str	r1, [r3, #4056]
	cbz	r1, .L2717
	movs	r1, #1
	addw	r0, r3, #4052
	mov	r2, r1
	bl	FlashReadPages
	b	.L2718
.L2717:
	movs	r1, #255
	ldrh	r2, [r2, #1882]
	bl	ftl_memset
.L2718:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #4060]
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L2713:
	pop	{r4, r5, r6, pc}
.L2720:
	.align	2
.L2719:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r3, .L2736
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r2, [r3]
	.pad #44
	sub	sp, sp, #44
	mov	r7, r1
	mov	r5, r0
	mov	r9, r3
	str	r2, [sp, #36]
	ldr	r2, .L2736+4
	ldrh	r1, [r2, #124]
	ldrh	r4, [r2, #126]
	ldr	r2, .L2736+8
	smulbb	r4, r1, r4
	ldr	r1, [r2, #3988]
	mov	r8, r2
	str	r1, [sp, #8]
	ldr	r1, [r2, #4020]
	uxth	r0, r4
	subs	r6, r0, #1
	sub	r4, r0, #16
	mul	fp, r0, r7
	uxth	r6, r6
	str	r1, [sp, #12]
.L2722:
	cmp	r6, r4
	ble	.L2735
	movs	r1, #1
	add	r3, r6, fp
	mov	r0, sp
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L2723
	ldr	r3, [r8, #4020]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2723
	cbz	r5, .L2730
	cbz	r7, .L2731
.L2727:
	ldr	r1, [r8, #3988]
	mov	r2, r10
	mov	r0, r5
	bl	ftl_memcpy
	movs	r2, #4
	ldr	r0, .L2736+12
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	movs	r0, #0
	b	.L2724
.L2731:
	ldr	r6, .L2736+8
	mov	lr, #1
.L2725:
	ldr	r3, [r8, #4008]
	uxth	r4, r7
	adds	r7, r7, #1
	cmp	r4, r3
	bcs	.L2727
	ldr	r2, [r6, #3988]
	lsr	ip, r4, #5
	and	r0, r4, #31
	lsl	r3, lr, r0
	ldr	r1, [r2, ip, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, ip, lsl #2]
	b	.L2725
.L2723:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L2722
.L2735:
	mov	r0, #-1
	b	.L2724
.L2730:
	mov	r0, r5
.L2724:
	ldr	r2, [sp, #36]
	ldr	r3, [r9]
	cmp	r2, r3
	beq	.L2729
	bl	__stack_chk_fail
.L2729:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2737:
	.align	2
.L2736:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC135
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
	ldr	r3, .L2748
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #692]
	ldr	r6, .L2748+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #4016]
	smulbb	r4, r4, r3
	uxth	r4, r4
	adds	r2, r4, #7
	asrs	r2, r2, #3
	bl	FlashReadFacBbtData
	adds	r0, r0, #1
	bne	.L2739
.L2743:
	movs	r3, #0
	b	.L2740
.L2739:
	movs	r1, #0
	lsr	lr, r4, #4
	mov	ip, #1
	subs	r4, r4, #1
	mov	r3, r1
.L2741:
	uxth	r2, r1
	cmp	r2, r4
	bge	.L2740
	ldr	r7, [r6, #4016]
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
	bcc	.L2741
	b	.L2743
.L2740:
	movw	r2, #65535
	movs	r0, #0
	strh	r2, [r5, r3, lsl #1]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L2749:
	.align	2
.L2748:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r4, .L2772
	ldr	r5, [r4, #4052]
	cmp	r5, #0
	bne	.L2751
	bl	FtlBbtMemInit
	ldr	r7, .L2772+4
	bl	FtlLoadFactoryBbt
	addw	r8, r4, #1964
	addw	r9, r4, #1946
	mov	fp, r4
	mov	r10, r7
.L2752:
	ldrh	r3, [r4, #1826]
	cmp	r5, r3
	bcs	.L2770
	ldr	r3, [r7, #1800]
	movw	r2, #65535
	ldr	r0, [r7, #1776]
	str	r3, [sp]
	str	r3, [r7, #4064]
	ldrh	r3, [r9, #2]!
	str	r0, [r7, #4060]
	cmp	r3, r2
	beq	.L2753
	ldrh	ip, [fp, #1870]
	movs	r1, #1
	ldr	r0, .L2772+8
	mov	r2, r1
	mla	ip, ip, r5, r3
	lsl	r3, ip, #10
	str	ip, [sp]
	str	r3, [r7, #4056]
	bl	FlashReadPages
	ldrh	r2, [fp, #1870]
	ldr	r0, [r8]
	adds	r2, r2, #7
	ldr	r1, [r7, #4060]
	asrs	r2, r2, #3
	bl	ftl_memcpy
	ldr	ip, [sp]
	b	.L2754
.L2753:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r7, #4060]
	ldr	r1, [r8]
	bl	FtlBbt2Bitmap
	ldrh	r6, [fp, #1870]
.L2756:
	subs	r6, r6, #1
	uxth	r6, r6
.L2755:
	ldrh	r0, [r4, #1870]
	smlabb	r0, r0, r5, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2756
	movs	r1, #0
	movs	r2, #16
	strh	r6, [r9]	@ movhi
	ldr	r0, [r10, #1800]
	bl	ftl_memset
	movw	r3, #61664
	ldr	r2, [sp]
	strh	r3, [r2]	@ movhi
	movs	r3, #0
	str	r3, [r2, #4]
	ldrh	r3, [r9]
	ldrh	ip, [fp, #1870]
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r9]
	ldrh	r2, [r10, #1864]
	ldr	r1, [r8]
	mla	ip, ip, r5, r3
	lsls	r2, r2, #2
	ldr	r0, [r10, #4060]
	lsl	r3, ip, #10
	str	ip, [sp, #4]
	str	r3, [r10, #4056]
	bl	ftl_memcpy
	movs	r1, #1
	ldr	r0, .L2772+8
	mov	r2, r1
	bl	FlashEraseBlocks
	movs	r1, #1
	ldr	r0, .L2772+8
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r10, #4052]
	adds	r3, r3, #1
	ldr	ip, [sp, #4]
	bne	.L2754
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L2755
.L2754:
	uxth	r0, ip
	adds	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r8, r8, #4
	b	.L2752
.L2770:
	movs	r5, #0
.L2759:
	ldrh	r3, [r4, #1886]
	uxth	r0, r5
	ldr	r6, .L2772
	adds	r5, r5, #1
	cmp	r3, r0
	bls	.L2771
	bl	FtlBbmMapBadBlock
	b	.L2759
.L2771:
	ldrh	r5, [r6, #1948]
	movw	r7, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L2761:
	ldrh	r3, [r4, #1948]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L2765
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2762
	mov	r0, r5
	bl	FlashTestBlk
	cbz	r0, .L2763
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L2762
.L2763:
	ldrh	r3, [r6, #1936]
	cmp	r3, r7
	bne	.L2764
	strh	r5, [r6, #1936]	@ movhi
	b	.L2762
.L2764:
	ldr	r3, .L2772
	strh	r5, [r3, #1940]	@ movhi
	b	.L2765
.L2762:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L2761
.L2765:
	ldr	r0, [r4, #4056]
	movs	r5, #0
	ldrh	r3, [r4, #1936]
	movs	r2, #2
	str	r5, [r4, #1944]
	movs	r1, #1
	strh	r5, [r4, #1938]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #1940]
	lsls	r3, r3, #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r4, #1936]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, #1940]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #1944]
	ldrh	r2, [r4, #1940]
	adds	r3, r3, #1
	str	r3, [r4, #1944]
	ldrh	r3, [r4, #1936]
	strh	r5, [r4, #1938]	@ movhi
	strh	r2, [r4, #1936]	@ movhi
	strh	r3, [r4, #1940]	@ movhi
	bl	FtlBbmTblFlush
.L2751:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2773:
	.align	2
.L2772:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+4052
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	1
	.global	log2phys
	.thumb
	.thumb_func
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r9, r0
	ldr	r6, .L2790
	mov	r10, r1
	str	r2, [sp, #4]
	ldr	r3, [r6, #1932]
	ldrh	r8, [r6, #1880]
	cmp	r0, r3
	bcs	.L2775
	add	r3, r8, #7
	ldr	r2, .L2790+4
	lsr	r5, r0, r3
	str	r3, [sp]
	ldrh	r1, [r6, #1910]
	movs	r4, #0
	ldr	fp, [r2, #-56]
	mov	r8, r2
	uxth	r5, r5
	movs	r2, #12
	b	.L2776
.L2775:
	ldr	r3, [sp, #4]
	mov	r0, #-1
	cmp	r3, #0
	bne	.L2777
	str	r0, [r1]
	b	.L2777
.L2781:
	adds	r4, r4, #1
	mla	r0, r2, r4, fp
	ldrh	r0, [r0, #-12]
	cmp	r0, r5
	beq	.L2778
.L2776:
	uxth	r7, r4
	cmp	r7, r1
	bcc	.L2781
	bl	select_l2p_ram_region
	movs	r4, #12
	movw	r3, #65535
	muls	r4, r0, r4
	mov	r7, r0
	add	r2, fp, r4
	ldrh	r1, [fp, r4]
	cmp	r1, r3
	bne	.L2789
.L2782:
	ldr	r3, [r8, #1848]
	ldr	fp, .L2790+4
	ldr	r8, [r3, r5, lsl #2]
	cmp	r8, #0
	bne	.L2783
	ldr	r2, [fp, #-56]
	movs	r1, #255
	add	r2, r2, r4
	ldr	r0, [r2, #8]
	ldrh	r2, [r6, #1882]
	bl	ftl_memset
	ldr	r2, [fp, #-56]
	strh	r5, [r2, r4]	@ movhi
	ldr	r2, [fp, #-56]
	add	r4, r4, r2
	str	r8, [r4, #4]
.L2778:
	ldr	r3, [sp]
	movs	r2, #1
	ldr	r1, [sp, #4]
	lsls	r2, r2, r3
	movs	r3, #12
	subs	r2, r2, #1
	and	r9, r9, r2
	ldr	r2, .L2790+4
	uxth	r9, r9
	cbnz	r1, .L2779
	ldr	r1, [r2, #-56]
	mla	r3, r3, r7, r1
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r9, lsl #2]
	str	r3, [r10]
	b	.L2780
.L2779:
	muls	r3, r7, r3
	ldr	r1, [r2, #-56]
	ldr	r0, [r10]
	add	r1, r1, r3
	ldr	r1, [r1, #8]
	str	r0, [r1, r9, lsl #2]
	ldr	r1, [r2, #-56]
	strh	r5, [r2, #-52]	@ movhi
	add	r3, r3, r1
	ldr	r1, [r3, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r3, #4]
.L2780:
	ldr	r2, [r2, #-56]
	movs	r3, #12
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	adds	r2, r3, #1
	beq	.L2787
	adds	r3, r3, #1
	str	r3, [r7, #4]
	b	.L2787
.L2789:
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L2782
	bl	flush_l2p_region
	b	.L2782
.L2783:
	ldr	r2, [fp, #-56]
	movs	r1, #1
	addw	r0, fp, #4052
	str	r8, [fp, #4056]
	add	r2, r2, r4
	ldr	r2, [r2, #8]
	str	r2, [fp, #4060]
	ldr	r2, [fp, #1800]
	str	r2, [fp, #4064]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [fp, #4064]
	ldrh	r2, [r2, #8]
	cmp	r2, r5
	beq	.L2784
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L2790+8
	bl	printk
	movs	r2, #4
	ldr	r0, .L2790+12
	mov	r3, r2
	ldr	r1, [fp, #4064]
	bl	rknand_print_hex
	ldrh	r3, [r6, #1908]
	ldr	r0, .L2790+16
	movs	r2, #4
	ldr	r1, [fp, #1848]
	bl	rknand_print_hex
	movs	r3, #1
	str	r3, [r6, #4052]
	b	.L2785
.L2784:
	ldr	r2, [fp, #4052]
	cmp	r2, #256
	bne	.L2785
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L2790+20
	bl	printk
	ldr	r3, [fp, #-56]
	addw	r0, fp, #1868
	mov	r1, r5
	add	r3, r3, r4
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2785:
	ldr	r3, .L2790+4
	movs	r1, #0
	ldr	r3, [r3, #-56]
	adds	r2, r3, r4
	str	r1, [r2, #4]
	strh	r5, [r3, r4]	@ movhi
	b	.L2778
.L2787:
	movs	r0, #0
.L2777:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2791:
	.align	2
.L2790:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC136
	.word	.LC100
	.word	.LC137
	.word	.LC138
	.fnend
	.size	log2phys, .-log2phys
	.align	1
	.global	FtlWriteDumpData
	.thumb
	.thumb_func
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r7, .L2811
	ldr	r8, .L2811+12
	ldr	r3, [r7]
	str	r3, [sp, #44]
	ldr	r3, [r8, #4052]
	cmp	r3, #0
	bne	.L2792
	ldr	r4, .L2811+4
	ldrh	r2, [r4, #-196]
	cmp	r2, #0
	beq	.L2794
	ldrb	r3, [r4, #-192]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2794
	ldrb	r1, [r4, #-193]	@ zero_extendqisi2
	ldrh	r3, [r8, #1872]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L2794
	ldrb	r10, [r4, #-190]	@ zero_extendqisi2
	ldr	r6, [r8, #1932]
	ldrh	r9, [r8, #1804]
	cmp	r10, #0
	bne	.L2792
	subs	r6, r6, #1
	add	r1, sp, #4
	mov	r2, r10
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #1800]
	ldr	r0, [r4, #1776]
	ldr	r3, [sp, #4]
	str	r6, [sp, #24]
	str	r0, [sp, #16]
	str	r3, [sp, #12]
	adds	r3, r3, #1
	str	r5, [sp, #20]
	str	r10, [r5, #4]
	beq	.L2796
	movs	r1, #1
	mov	r2, r10
	add	r0, sp, #8
	bl	FlashReadPages
	b	.L2797
.L2796:
	movs	r1, #255
	ldrh	r2, [r8, #1882]
	bl	ftl_memset
.L2797:
	mov	r8, #0
	ldr	fp, .L2811+4
	lsl	r9, r9, #2
	movw	r3, #61589
	mov	r10, r8
	strh	r3, [r5]	@ movhi
.L2798:
	cmp	r8, r9
	beq	.L2799
	ldrh	r3, [r4, #-196]
	cbz	r3, .L2799
	ldr	r3, [sp, #12]
	add	r8, r8, #1
	str	r6, [r5, #8]
	ldr	r0, .L2811+8
	str	r3, [r5, #12]
	ldrh	r3, [fp, #-200]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [fp, #-4]
	movs	r1, #1
	str	r0, [sp, #12]
	add	r0, sp, #8
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, #0
	it	eq
	moveq	r3, r10
	str	r3, [fp, #-4]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r4, #-200]
	bl	decrement_vpc_count
	b	.L2798
.L2799:
	movs	r3, #1
	b	.L2810
.L2794:
	movs	r3, #0
.L2810:
	strb	r3, [r4, #-190]
.L2792:
	ldr	r2, [sp, #44]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L2803
	bl	__stack_chk_fail
.L2803:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2812:
	.align	2
.L2811:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR2-200
	.word	.LANCHOR0
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
	ldr	r5, .L2818
	movs	r7, #12
	ldr	r6, .L2818+4
.L2814:
	ldrh	r3, [r5, #1910]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2817
	ldr	r3, [r6, #-56]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2815
	bl	flush_l2p_region
.L2815:
	adds	r4, r4, #1
	b	.L2814
.L2817:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L2819:
	.align	2
.L2818:
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
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldr	r7, .L2846
	ldrh	r6, [r0]
	ldr	r3, [r7, #4052]
	cmp	r3, #0
	bne	.L2821
	movw	r3, #65535
	cmp	r6, r3
	ldr	r5, .L2846+4
	beq	.L2822
	ldr	r3, [r5, #-220]
	mov	r0, r6
	ldrh	r3, [r3, r6, lsl #1]
	cbz	r3, .L2823
	bl	INSERT_DATA_LIST
	b	.L2822
.L2823:
	bl	INSERT_FREE_LIST
.L2822:
	movs	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L2846+4
	sub	r2, r3, #152
	cmp	r4, r2
	beq	.L2824
	ldrh	r2, [r7, #1824]
	cmp	r2, #1
	beq	.L2824
	ldr	r1, .L2846
	ldrb	r1, [r1, #140]	@ zero_extendqisi2
	cbz	r1, .L2825
.L2824:
	movs	r3, #1
	strb	r3, [r4, #8]
	b	.L2826
.L2825:
	sub	r1, r3, #200
	cmp	r4, r1
	bne	.L2826
	cmp	r2, #3
	beq	.L2828
	ldr	r3, [r3, #88]
	cmp	r3, #1
	bne	.L2829
.L2828:
	movs	r3, #1
	strb	r3, [r5, #-192]
.L2829:
	ldr	r3, [r7, #1796]
	cbz	r3, .L2826
	ldr	r3, [r5, #16]
	cmp	r3, #39
	ittt	ls
	movls	r2, #1
	ldrls	r3, .L2846+4
	strbls	r2, [r3, #-192]
.L2826:
	ldrh	r0, [r5, #1920]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2831
	cmp	r6, r0
	bne	.L2832
	ldr	r3, .L2846+4
	ldr	r3, [r3, #-220]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L2833
.L2832:
	bl	update_vpc_list
.L2833:
	movw	r3, #65535
	strh	r3, [r5, #1920]	@ movhi
.L2831:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2821:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L2847:
	.align	2
.L2846:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r6, .L2869
	ldr	r1, .L2869+4
	ldr	r0, .L2869+8
	ldr	r3, [r6]
	ldr	r5, .L2869+12
	ldr	r8, .L2869+36
	str	r3, [sp, #4]
	bl	printk
	ldr	r0, .L2869+16
	movs	r1, #0
	mov	r2, #8192
	bl	memset
.L2849:
	ldr	r3, [r5, #1932]
	ldr	r7, .L2869+12
	cmp	r4, r3
	bcs	.L2867
	mov	r0, r4
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	adds	r3, r0, #1
	beq	.L2850
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r0, r8, r0, lsl #1
	ldrh	r3, [r0, #-204]
	adds	r3, r3, #1
	strh	r3, [r0, #-204]	@ movhi
.L2850:
	adds	r4, r4, #1
	b	.L2849
.L2867:
	ldr	r9, .L2869+20
	movs	r4, #0
	ldr	r8, .L2869+36
.L2852:
	ldrh	r3, [r7, #1812]
	uxth	r1, r4
	ldr	r5, .L2869+20
	cmp	r3, r1
	bls	.L2868
	ldr	r3, [r9, #-220]
	ldrh	r2, [r3, r1, lsl #1]
	add	r3, r8, r1, lsl #1
	ldrh	r3, [r3, #-204]
	cmp	r2, r3
	beq	.L2853
	ldr	r0, .L2869+24
	bl	printk
.L2853:
	adds	r4, r4, #1
	b	.L2852
.L2868:
	ldr	r4, [r5, #-208]
	cbz	r4, .L2848
	ldr	r3, [r5, #-228]
	movs	r7, #0
	ldrh	r8, [r5, #-204]
	mov	r10, #6
	subs	r4, r4, r3
	ldr	r3, .L2869+28
	ldr	r9, .L2869+36
	asrs	r4, r4, #1
	muls	r4, r3, r4
	uxth	r4, r4
.L2856:
	uxth	r3, r7
	cmp	r3, r8
	bcs	.L2848
	ldr	r3, [r5, #-220]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L2857
	add	r3, r9, r4, lsl #1
	ldr	r0, .L2869+32
	mov	r1, r4
	ldrh	r3, [r3, #-204]
	bl	printk
.L2857:
	mul	r4, r10, r4
	ldr	r3, [r5, #-228]
	adds	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2856
.L2848:
	ldr	r2, [sp, #4]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2860
	bl	__stack_chk_fail
.L2860:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2870:
	.align	2
.L2869:
	.word	__stack_chk_guard
	.word	.LANCHOR3+72
	.word	.LC107
	.word	.LANCHOR0
	.word	.LANCHOR4-204
	.word	.LANCHOR2
	.word	.LC139
	.word	-1431655765
	.word	.LC140
	.word	.LANCHOR4
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #32
	sub	sp, sp, #32
	ldr	r6, .L2887
	movs	r1, #0
	ldr	r0, .L2887+4
	movs	r4, #0
	ldr	r8, .L2887+24
	ldr	r3, [r6]
	ldr	r5, .L2887+8
	str	r3, [sp, #28]
	bl	printk
.L2872:
	ldr	r3, [r8, #1932]
	cmp	r4, r3
	bcs	.L2886
	mov	r0, r4
	add	r1, sp, #24
	movs	r2, #0
	bl	log2phys
	ubfx	r3, r4, #0, #11
	cbnz	r3, .L2873
	ldr	r0, .L2887+12
	mov	r1, r4
	ldr	r2, [sp, #24]
	bl	printk
.L2873:
	ldr	r3, [sp, #24]
	adds	r2, r3, #1
	beq	.L2875
	str	r3, [r5, #4056]
	movs	r2, #0
	ldr	r3, [r5, #1776]
	movs	r1, #1
	ldr	r7, [r5, #1800]
	ldr	r0, .L2887+16
	str	r3, [r5, #4060]
	str	r2, [r5, #4052]
	str	r4, [r5, #4068]
	str	r7, [r5, #4064]
	bl	FlashReadPages
	ldr	r3, [r5, #4052]
	ldr	r2, .L2887+8
	cmp	r3, #256
	beq	.L2876
	adds	r3, r3, #1
	beq	.L2876
	ldr	r3, [r7, #8]
	cmp	r3, r4
	beq	.L2875
.L2876:
	ldr	r3, [r2, #4064]
	ldr	r1, [r2, #4060]
	ldr	r0, [r3, #4]
	str	r0, [sp]
	ldr	r0, [r3, #8]
	str	r0, [sp, #4]
	ldr	r0, [r3, #12]
	str	r0, [sp, #8]
	ldr	r0, [r1]
	str	r0, [sp, #12]
	ldr	r1, [r1, #4]
	ldr	r0, .L2887+20
	str	r1, [sp, #16]
	mov	r1, r4
	ldr	r2, [r2, #4056]
	ldr	r3, [r3]
	bl	printk
.L2875:
	adds	r4, r4, #1
	b	.L2872
.L2886:
	ldr	r2, [sp, #28]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2879
	bl	__stack_chk_fail
.L2879:
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2888:
	.align	2
.L2887:
	.word	__stack_chk_guard
	.word	.LC141
	.word	.LANCHOR2
	.word	.LC142
	.word	.LANCHOR2+4052
	.word	.LC143
	.word	.LANCHOR0
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
	ldr	r5, .L2898
	ldr	r4, [r5, #-220]
	ldrh	r3, [r4, r0, lsl #1]
	cbnz	r3, .L2890
	ldr	r4, [r5, #-208]
	cbz	r4, .L2891
	ldr	r1, [r5, #-228]
	mov	lr, #6
	ldr	r2, .L2898+4
	movw	ip, #65535
	subs	r4, r4, r1
	ldrh	r7, [r5, #-204]
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L2892:
	uxth	r2, r3
	cmp	r2, r7
	bcs	.L2891
	cmp	r4, r0
	bne	.L2893
	mov	r1, r4
	ldr	r0, .L2898+8
	bl	List_remove_node
	ldrh	r3, [r5, #-204]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #-204]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #-220]
	ldrh	r3, [r2, r4, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r4, lsl #1]	@ movhi
	b	.L2891
.L2893:
	mul	r4, lr, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, ip
	bne	.L2892
	b	.L2891
.L2890:
	adds	r3, r3, #1
	strh	r3, [r4, r0, lsl #1]	@ movhi
.L2891:
	mov	r0, r6
	add	r1, sp, #4
	movs	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2899:
	.align	2
.L2898:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-208
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
	ldr	r3, .L3061
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r3, [r3]
	movw	r7, #65535
	mov	r4, r0
	str	r3, [sp, #60]
	ldrh	r3, [r0]
	cmp	r3, r7
	beq	.L2902
	ldrh	r3, [r0, #2]
	movs	r5, #0
	ldr	r6, .L3061+4
	str	r3, [sp, #12]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	str	r3, [sp, #28]
	ldrh	r3, [r6, #1872]
	cmp	r3, r2
	bne	.L2903
	strh	r5, [r0, #4]	@ movhi
	strb	r5, [r0, #6]
	b	.L2902
.L2903:
	ldrh	r0, [r0, #16]
.L2904:
	cmp	r0, r7
	add	r5, r5, #1
	bne	.L3056
	uxth	r3, r5
	adds	r3, r3, #8
	ldrh	r0, [r4, r3, lsl #1]
	b	.L2904
.L3056:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2906
	bl	FtlGetLastWrittenPage
	adds	r2, r0, #1
	mov	r5, r0
	beq	.L2907
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbnz	r3, .L3047
	ldr	r3, .L3061+4
	add	r3, r3, r0, lsl #1
	ldrh	r7, [r3, #144]
	b	.L2981
.L2906:
	movs	r1, #0
	bl	FtlGetLastWrittenPage
	adds	r3, r0, #1
	mov	r5, r0
	beq	.L2907
.L3047:
	mov	r7, r5
.L2981:
	ldr	r3, .L3061+8
	movw	r9, #65535
	ldrh	ip, [r6, #1804]
	mov	r10, #36
	ldrh	r0, [r6, #1884]
	ldr	r2, [r3, #1764]
	ldr	r8, [r3, #632]
	add	r3, r4, #14
	str	r3, [sp, #20]
	str	r2, [sp, #4]
	movs	r2, #0
	mov	lr, r3
	mov	r6, r2
	mov	fp, r2
	b	.L2909
.L2907:
	movs	r3, #0
	strh	r3, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
	b	.L2902
.L2912:
	ldrh	r3, [lr, #2]!
	cmp	r3, r9
	beq	.L2910
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
.L2910:
	adds	r2, r2, #1
.L2909:
	uxth	r3, r2
	cmp	r3, ip
	bcc	.L2912
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2984
	ldr	r3, .L3061+4
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	adds	r3, r1, #0
	it	ne
	movne	r3, #1
	b	.L3048
.L2984:
	movs	r3, #0
.L3048:
	ldr	r8, .L3061+8
	mov	r1, r6
	str	r3, [sp, #24]
	mov	fp, #0
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #1764]
	bl	FlashReadPages
	ldr	r3, [r8, #-4]
	subs	r3, r3, #1
	str	r8, [sp, #8]
	str	r3, [sp, #4]
	movw	r3, #65535
	str	r3, [sp, #16]
.L2914:
	uxth	r9, fp
	cmp	r9, r6
	bcs	.L2921
	movs	r1, #36
	ldr	r0, [r8, #1764]
	mul	r1, r1, fp
	add	lr, r0, r1
	ldr	r1, [r0, r1]
	cbnz	r1, .L2915
	ldr	r3, [lr, #12]
	ldr	r10, [r3, #4]
	cmp	r10, #-1
	beq	.L2916
	mov	r0, r10
	ldr	r1, [r8, #-4]
	str	r3, [sp, #32]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #32]
	cbz	r0, .L2916
	add	r1, r10, #1
	str	r1, [r8, #-4]
.L2916:
	ldr	r1, [r3]
	adds	r1, r1, #1
	bne	.L2917
.L2921:
	cmp	r9, r6
	ldr	r6, .L3061+8
	bne	.L3045
	ldr	r3, [r6, #1764]
	adds	r5, r5, #1
	uxth	r10, r5
	ldr	r0, [r3, #4]
	b	.L3049
.L2915:
	ldr	r1, [lr, #4]
	ldr	r0, .L3061+12
	bl	printk
	uxth	r3, r7
	ldrh	r1, [r4]
	str	r3, [sp, #16]
	ldr	r3, .L3061+16
	strh	r1, [r3, #3644]	@ movhi
.L2917:
	add	fp, fp, #1
	b	.L2914
.L3045:
	ldr	r3, [sp, #8]
	movs	r2, #36
	uxth	r10, r5
	ldr	r3, [r3, #1764]
	mla	r9, r2, r9, r3
	ldr	r0, [r9, #4]
.L3049:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r5, [r4, #8]	@ zero_extendqisi2
	ldr	r2, .L3061+4
	cmp	r5, #1
	str	r0, [sp, #8]
	bne	.L2923
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	cbnz	r3, .L2923
	add	r3, r2, r10, lsl #1
	ldrh	r10, [r3, #144]
.L2923:
	ldrh	r3, [r2, #1872]
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
	bne	.L2925
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #32]
	cmp	r3, r2
	bne	.L2925
	mov	r0, r4
	mov	r1, r10
	mov	r2, r3
	b	.L3054
.L2925:
	ldr	r2, [sp, #16]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2926
	cmp	r5, #0
	bne	.L2927
.L2926:
	ldr	r3, [r6, #1932]
	uxth	fp, r7
	mov	r9, #-1
	ldr	r7, .L3061+8
	adds	r3, r3, #1
	mov	r8, r9
	ittt	eq
	ldreq	r3, .L3061+8
	ldreq	r2, [sp, #4]
	streq	r2, [r3, #1932]
	ldr	r3, [r6, #1932]
	str	r3, [sp, #16]
	ldr	r3, [sp, #12]
	adds	r3, r3, #7
	cmp	fp, r3
	itet	gt
	subgt	r5, fp, #7
	ldrle	r5, [sp, #12]
	uxthgt	r5, r5
.L2930:
	cmp	r5, fp
	bhi	.L2943
	ldr	r3, .L3061+4
	ldr	r1, [sp, #20]
	ldrh	ip, [r3, #1804]
	ldr	r3, [r7, #1764]
	str	r3, [sp, #28]
	movs	r3, #0
	mov	r6, r3
.L2944:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3057
	ldrh	r2, [r1, #2]!
	movw	r0, #65535
	cmp	r2, r0
	beq	.L2931
	ldr	r0, [sp, #28]
	mov	lr, #36
	orr	r2, r5, r2, lsl #10
	mla	r0, lr, r6, r0
	adds	r6, r6, #1
	uxth	r6, r6
	str	r2, [r0, #4]
.L2931:
	adds	r3, r3, #1
	b	.L2944
.L3062:
	.align	2
.L3061:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC144
	.word	.LANCHOR5
.L3057:
	mov	r1, r6
	ldr	r2, [sp, #24]
	ldr	r0, [r7, #1764]
	bl	FlashReadPages
	ldr	r3, .L3063
	movs	r2, #36
	add	lr, r7, r5, lsl #1
	movw	r1, #65535
	ldrb	r0, [r3, #140]	@ zero_extendqisi2
	ldr	r3, [r7, #1764]
	mla	r6, r2, r6, r3
.L2933:
	cmp	r3, r6
	beq	.L3058
	ldr	r2, [r3]
	cbnz	r2, .L2934
	ldr	r2, [r3, #12]
	ldrh	ip, [r2]
	cmp	ip, r1
	beq	.L2935
	ldr	r2, [r2, #4]
	cmp	r2, #-1
	beq	.L2935
	cmp	r9, #-1
	ldr	r8, [r7, #1932]
	str	r2, [r7, #1932]
	bne	.L2935
	ldrh	r2, [lr, #704]
	cmp	r2, r1
	bne	.L2936
	cbz	r0, .L2935
.L2936:
	ldr	r2, [sp, #4]
	cmp	r8, r2
	ite	ne
	movne	r9, r8
	moveq	r9, #-1
	b	.L2935
.L2934:
	ldr	r3, .L3063+4
	ldrh	r2, [r4]
	strh	r2, [r3, #3644]	@ movhi
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cbnz	r3, .L2927
	ldr	r3, .L3063+8
	movw	r2, #65535
	add	r5, r3, r5, lsl #1
	ldrh	r1, [r5, #704]
	cmp	r1, r2
	bne	.L2938
	cmp	r9, #-1
	beq	.L2939
	str	r9, [r3, #1932]
	b	.L2927
.L2939:
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	bne	.L3050
	ldr	r2, [r3, #1932]
	b	.L3055
.L2938:
	ldr	r2, [sp, #4]
	cmp	r8, r2
	beq	.L2941
	cmp	r8, #-1
	beq	.L2927
	str	r8, [r3, #1932]
	b	.L2927
.L2941:
	ldr	r2, [r3, #1932]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	beq	.L2927
.L3055:
	subs	r2, r2, #1
	b	.L3050
.L2935:
	adds	r3, r3, #36
	b	.L2933
.L3058:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L2930
.L2943:
	ldr	r3, .L3063+8
	mov	r2, #-1
.L3050:
	str	r2, [r3, #1932]
.L2927:
	ldr	r3, .L3063+4
	movs	r2, #1
	ldr	fp, [sp, #12]
	ldr	r5, .L3063+8
	strh	r2, [r3, #3646]	@ movhi
.L2945:
	ldr	r3, .L3063
	movw	r9, #65535
	ldr	r1, [r5, #1764]
	movs	r6, #0
	ldr	r7, [sp, #20]
	ldrh	r8, [r3, #1804]
	ldrb	r0, [r3, #140]	@ zero_extendqisi2
	str	r6, [sp, #16]
.L2946:
	uxth	r3, r6
	cmp	r3, r8
	bcs	.L3059
	ldrh	r3, [r7, #2]!
	cmp	r3, r9
	beq	.L2947
	ldr	r2, [sp, #16]
	mov	lr, #36
	orr	r3, fp, r3, lsl #10
	mla	r2, lr, r2, r1
	str	r3, [r2, #4]
	ldrb	lr, [r4, #8]	@ zero_extendqisi2
	cmp	lr, #1
	bne	.L2948
	cbz	r0, .L2948
	orr	r3, r3, #-2147483648
	str	r3, [r2, #4]
.L2948:
	ldr	r3, [sp, #16]
	add	ip, r3, #1
	uxth	r3, ip
	str	r3, [sp, #16]
.L2947:
	adds	r6, r6, #1
	b	.L2946
.L3059:
	ldr	r0, [r5, #1764]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	movs	r3, #0
.L3053:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3060
	ldr	r3, [sp, #28]
	movs	r6, #36
	ldr	r9, [r5, #1764]
	muls	r6, r3, r6
	add	r8, r9, r6
	ldr	r7, [r8, #4]
	ubfx	r0, r7, #10, #16
	str	r7, [sp, #56]
	bl	P2V_plane
	ldr	r3, [sp, #12]
	cmp	fp, r3
	bcc	.L2951
	bne	.L2952
	ldr	r3, [sp, #32]
	cmp	r0, r3
	bcc	.L2951
.L2952:
	cmp	fp, r10
	bne	.L2953
	ldr	r3, [sp, #8]
	cmp	r0, r3
	beq	.L2954
.L2953:
	ldr	r3, [r9, r6]
	adds	r3, r3, #1
	beq	.L2955
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L2956
	ldrh	r0, [r4]
	b	.L3051
.L2956:
	ldr	r3, [r8, #4]
	adds	r7, r3, #1
	str	r3, [sp, #4]
	beq	.L2957
	mov	r0, r3
	ldr	r1, [r5, #-4]
	bl	ftl_cmp_data_ver
	cbz	r0, .L2957
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [r5, #-4]
.L2957:
	ldr	r7, [r8, #8]
	add	r1, sp, #52
	ldr	r3, [r8, #12]
	movs	r2, #0
	mov	r0, r7
	str	r3, [sp, #48]
	bl	log2phys
	ldr	r1, [r5, #1932]
	adds	r0, r1, #1
	beq	.L2958
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2958
	ldr	r3, [sp, #48]
	adds	r2, r3, #1
	beq	.L2959
	ldr	r0, [r5, #1764]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r6
	str	r3, [r0, #4]
	ldr	r9, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r5, #1764]
	ldr	r3, [r9, #4]
	add	ip, r2, r6
	str	r3, [sp, #36]
	ldr	r3, [r2, r6]
	adds	r3, r3, #1
	bne	.L2960
	b	.L2961
.L2959:
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #52]
	cmp	r2, r3
	bne	.L2951
	mov	r0, r7
	add	r1, sp, #48
	movs	r2, #1
	bl	log2phys
	b	.L2951
.L2960:
	ldr	r8, [r9, #8]
	cmp	r8, r7
	bne	.L2961
	ldr	r0, [r5, #1932]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	cbz	r0, .L2961
	ldr	r3, [sp, #52]
	ldr	r1, [sp, #56]
	cmp	r3, r1
	beq	.L2966
	ldr	r1, [sp, #48]
	cmp	r3, r1
	beq	.L2961
	adds	r0, r3, #1
	beq	.L2964
	str	r3, [ip, #4]
	mov	r0, ip
	movs	r1, #1
	movs	r2, #0
	ldr	r9, [ip, #12]
	bl	FlashReadPages
	b	.L2965
.L2964:
	str	r3, [r2, r6]
.L2965:
	ldr	r3, [r5, #1764]
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	beq	.L2966
	ldr	r6, [r9, #4]
	ldr	r0, [r5, #1932]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L2966
	ldr	r0, [sp, #36]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L2961
.L2966:
	mov	r0, r8
	ldr	r1, [sp, #48]
	bl	FtlReUsePrevPpa
.L2961:
	mov	r3, #-1
	str	r3, [sp, #48]
	b	.L2968
.L3064:
	.align	2
.L3063:
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LANCHOR2
.L2958:
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #52]
	cmp	r2, r3
	beq	.L2968
	ldr	r2, .L3065
	ldr	r3, [sp, #48]
	ldr	r2, [r2, #1820]
	cmp	r2, r3, lsr #10
	bhi	.L2970
	adds	r3, r3, #1
	bne	.L2951
.L2970:
	mov	r0, r7
	add	r1, sp, #56
	movs	r2, #1
	bl	log2phys
	ldr	r8, [sp, #52]
	cmp	r8, #-1
	beq	.L2968
	ldr	r3, [sp, #48]
	cmp	r8, r3
	beq	.L2968
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #-200]
	cmp	r3, r0
	beq	.L2972
	ldrh	r3, [r5, #-152]
	cmp	r3, r0
	beq	.L2972
	ldrh	r3, [r5, #-104]
	cmp	r3, r0
	bne	.L2968
.L2972:
	ldr	r3, .L3065+4
	movs	r1, #1
	movs	r2, #0
	ldr	r0, [r3, #1764]
	str	r8, [r0, #4]
	ldr	r6, [r0, #12]
	bl	FlashReadPages
	ldr	r3, .L3065+4
	ldr	r1, [r6, #4]
	ldr	r3, [r3, #1764]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L2968
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cbnz	r0, .L2968
	mov	r0, r7
	add	r1, sp, #52
	movs	r2, #1
	bl	log2phys
.L2968:
	ldr	r0, [sp, #48]
	adds	r1, r0, #1
	beq	.L2951
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #-220]
	ldrh	r3, [r3, r0, lsl #1]
	mov	r1, r0
	cbz	r3, .L2973
.L3051:
	bl	decrement_vpc_count
	b	.L2951
.L2973:
	ldr	r0, .L3065+8
	bl	printk
	b	.L2951
.L2955:
	ldrh	r3, [r4]
	mov	r1, r7
	ldr	r2, .L3065+12
	ldr	r0, .L3065+16
	strh	r3, [r2, #3644]	@ movhi
	ldr	r2, [sp, #4]
	bl	printk
	ldr	r3, .L3065+12
	ldr	r3, [r3, #3648]
	cmp	r3, #31
	bhi	.L2974
	ldr	r2, .L3065+12
	ldr	r1, [sp, #56]
	add	r2, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r2, #3652]
	ldr	r2, .L3065+12
	str	r3, [r2, #3648]
.L2974:
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	ldr	r3, [r5, #1932]
	adds	r2, r3, #1
	bne	.L2975
	ldr	r3, [sp, #4]
	b	.L3052
.L2975:
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bls	.L2951
	mov	r3, r2
.L3052:
	str	r3, [r5, #1932]
.L2951:
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	b	.L3053
.L3060:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	fp, fp, #1
	cmp	r3, #1
	uxth	fp, fp
	ldr	r3, .L3065
	bne	.L2977
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cbz	r2, .L2977
	ldrh	r2, [r3, #1874]
	cmp	r2, fp
	bne	.L2977
	cmp	r10, fp
	beq	.L2954
.L2977:
	ldrh	r3, [r3, #1872]
	cmp	fp, r3
	bne	.L2945
	ldr	r2, .L3065
	movw	r0, #65535
	movs	r3, #0
	strh	fp, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	ldrh	r1, [r2, #1804]
.L2978:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L2902
	ldr	r6, [sp, #20]
	adds	r3, r3, #1
	ldrh	r5, [r6, #2]!
	cmp	r5, r0
	str	r6, [sp, #20]
	beq	.L2978
	strb	r2, [r4, #6]
	b	.L2902
.L2954:
	ldrb	r3, [sp, #8]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, [sp, #8]
	mov	r1, r10
	strh	r10, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
.L3054:
	bl	ftl_sb_update_avl_pages
.L2902:
	ldr	r3, .L3065+20
	movs	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2982
	bl	__stack_chk_fail
.L2982:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3066:
	.align	2
.L3065:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC145
	.word	.LANCHOR5
	.word	.LC146
	.word	__stack_chk_guard
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
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	movs	r4, #0
	ldr	r6, .L3083
	ldr	r7, .L3083+4
	ldr	r1, .L3083+8
	ldr	r3, [r6]
	mov	r8, r6
	ldr	r0, .L3083+12
	ldr	r5, .L3083+16
	str	r3, [sp, #4]
	bl	printk
	ldrh	r2, [r7, #1814]
	movs	r1, #0
	ldr	r0, [r5, #1824]
	lsls	r2, r2, #1
	bl	ftl_memset
.L3068:
	ldr	r3, [r7, #1932]
	cmp	r4, r3
	bcs	.L3081
	mov	r0, r4
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	adds	r3, r0, #1
	beq	.L3069
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #1824]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L3069:
	adds	r4, r4, #1
	b	.L3068
.L3081:
	ldr	r6, .L3083+16
	mov	r9, #0
	movw	r10, #65535
.L3071:
	ldrh	r3, [r7, #1812]
	uxth	r4, r9
	cmp	r3, r4
	bls	.L3082
	ldr	r3, [r5, #-220]
	ldrh	r2, [r3, r4, lsl #1]
	ldr	r3, [r5, #1824]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r2, r3
	beq	.L3073
	cmp	r2, r10
	beq	.L3073
	ldr	r0, .L3083+20
	mov	r1, r4
	bl	printk
	ldrh	r3, [r6, #-200]
	cmp	r3, r4
	beq	.L3073
	ldrh	r3, [r6, #-104]
	cmp	r3, r4
	beq	.L3073
	ldrh	r3, [r6, #-152]
	cmp	r3, r4
	beq	.L3073
	ldr	r3, [r6, #-220]
	ldrh	r2, [r3, r4, lsl #1]
	cbnz	r2, .L3075
	ldr	r2, [r6, #1824]
	ldrh	r2, [r2, r4, lsl #1]
	strh	r2, [r3, r4, lsl #1]	@ movhi
	b	.L3073
.L3075:
	ldr	r2, [r6, #1824]
	mov	r0, r4
	ldrh	r2, [r2, r4, lsl #1]
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	update_vpc_list
.L3073:
	add	r9, r9, #1
	b	.L3071
.L3082:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r2, [sp, #4]
	ldr	r3, [r8]
	cmp	r2, r3
	beq	.L3077
	bl	__stack_chk_fail
.L3077:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3084:
	.align	2
.L3083:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR3+84
	.word	.LC107
	.word	.LANCHOR2
	.word	.LC147
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	1
	.global	FtlGcScanTempBlk
	.thumb
	.thumb_func
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3133
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r3, [r3]
	mov	r4, r0
	str	r1, [sp, #16]
	str	r3, [sp, #76]
	ldr	r3, .L3133+4
	ldrh	r5, [r3, #3288]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L3119
	cbnz	r5, .L3086
	b	.L3087
.L3119:
	movs	r5, #0
.L3086:
	ldr	r3, .L3133+8
	ldr	r2, [sp, #16]
	ldrh	r3, [r3, #1872]
	cmp	r2, r3
	bne	.L3088
.L3087:
	bl	FtlGcPageVarInit
.L3088:
	ldr	r6, .L3133+12
	mov	r3, #-1
	str	r3, [sp, #12]
	movs	r3, #0
	str	r3, [sp, #4]
.L3089:
	ldrh	r2, [r4]
	movs	r3, #0
	strb	r3, [r4, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L3090
.L3091:
	ldr	r10, .L3133+8
.L3115:
	ldr	r3, [r6, #1764]
	movs	r2, #0
	ldrh	r9, [r10, #1804]
	add	lr, r4, #14
	ldrh	fp, [r10, #1882]
	mov	r8, #36
	str	r3, [sp, #8]
	mov	r7, r2
	ldr	r3, [r6, #628]
	movw	ip, #65535
	str	r3, [sp, #20]
	ldr	r3, [r6, #632]
	str	r3, [sp, #24]
	ldrh	r3, [r10, #1884]
	str	r3, [sp, #28]
.L3092:
	uxth	r3, r2
	cmp	r3, r9
	bcs	.L3131
	ldrh	r3, [lr, #2]!
	cmp	r3, ip
	beq	.L3093
	ldr	r1, [sp, #8]
	orr	r3, r5, r3, lsl #10
	ldr	r0, [sp, #20]
	mla	r1, r8, r7, r1
	str	r3, [r1, #4]
	mov	r3, fp
	muls	r3, r7, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #8]
	ldr	r3, [sp, #28]
	muls	r3, r7, r3
	add	r7, r7, #1
	it	mi
	addmi	r3, r3, #3
	ldr	r0, [sp, #24]
	bic	r3, r3, #3
	uxth	r7, r7
	add	r3, r3, r0
	str	r3, [r1, #12]
.L3093:
	adds	r2, r2, #1
	b	.L3092
.L3131:
	ldr	r0, [r6, #1764]
	mov	r1, r7
	movs	r2, #0
	mov	fp, #0
	bl	FlashReadPages
.L3097:
	uxth	r3, fp
	cmp	r3, r7
	bcs	.L3132
	mov	r9, #36
	ldr	r2, [r6, #1764]
	ldr	ip, .L3133+12
	mul	r9, r9, fp
	add	r3, r2, r9
	ldr	r1, [r3, #4]
	ldr	r8, [r2, r9]
	ldr	r3, [r3, #12]
	str	r1, [sp, #8]
	cmp	r8, #0
	bne	.L3098
	ldrh	r1, [r3]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3099
.L3101:
	ldr	r3, .L3133+12
	movs	r1, #0
	ldrh	r2, [r4]
	ldr	r3, [r3, #-220]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L3133+12
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
	strh	r3, [r2, #40]	@ movhi
	b	.L3130
.L3099:
	ldr	r0, [r3, #8]
	ldr	r2, [r10, #1932]
	cmp	r0, r2
	bhi	.L3101
	ldrb	r2, [r10]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3103
	add	r1, sp, #36
	mov	r2, r8
	str	r3, [sp, #20]
	bl	log2phys
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #36]
	ldr	r1, [r3, #12]
	cmp	r2, r1
	bne	.L3103
	adds	r1, r2, #1
	beq	.L3103
	str	r2, [sp, #44]
	movs	r1, #1
	ldr	r2, [r6, #1796]
	add	r0, sp, #40
	str	r2, [sp, #48]
	ldr	r2, [r6, #1804]
	str	r2, [sp, #52]
	mov	r2, r8
	bl	FlashReadPages
	ldrh	r2, [r10, #1878]
	ldr	r1, [r6, #1764]
	lsl	lr, r2, #7
	add	r9, r9, r1
	ldr	r0, [sp, #48]
	ldr	r3, [sp, #20]
.L3105:
	cmp	r8, lr
	beq	.L3103
	ldr	r1, [r9, #8]
	ldr	r2, [r0, r8, lsl #2]
	ldr	r1, [r1, r8, lsl #2]
	cmp	r1, r2
	beq	.L3106
	ldrh	r1, [r4]
	ldr	r2, [sp, #44]
	ldr	r0, .L3133+16
	bl	printk
	ldr	r3, [r6, #-220]
	ldrh	r2, [r4]
	movs	r1, #0
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
	strh	r3, [r6, #40]	@ movhi
	b	.L3130
.L3106:
	add	r8, r8, #1
	b	.L3105
.L3103:
	ldr	r0, [r3, #12]
	add	fp, fp, #1
	ldr	r1, [sp, #8]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L3097
.L3098:
	ldrh	r1, [r4]
	ldr	r2, [sp, #8]
	ldr	r0, .L3133+20
	str	ip, [sp, #20]
	bl	printk
	ldr	r3, .L3133+8
	ldrh	r2, [r4]
	ldr	r1, [r3, #1796]
	ldr	ip, [sp, #20]
	cbnz	r1, .L3109
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cbz	r1, .L3110
.L3109:
	ldr	r3, [r3, #4060]
	ldrh	r3, [r3, r2, lsl #1]
	cmp	r3, #159
	bls	.L3111
.L3110:
	ldr	r3, [ip, #1764]
	ldr	r3, [r3, r9]
	adds	r3, r3, #1
	bne	.L3112
.L3111:
	ldr	r3, [ip, #1764]
	add	r9, r9, r3
	ldr	r3, [r9, #4]
	str	r3, [sp, #12]
.L3112:
	ldr	r3, .L3133+12
	movs	r1, #0
	ldr	r3, [r3, #-220]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
.L3130:
	bl	FtlGcPageVarInit
	movs	r5, #0
	b	.L3089
.L3132:
	ldr	r3, [sp, #4]
	adds	r5, r5, #1
	ldr	r2, [sp, #16]
	adds	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp, #4]
	bcs	.L3114
.L3116:
	ldrh	r3, [r10, #1872]
	cmp	r3, r5
	bhi	.L3115
	b	.L3090
.L3114:
	ldr	r2, .L3133+4
	movw	r1, #65535
	ldrh	r3, [r2, #3288]
	cmp	r3, r1
	beq	.L3116
	ldr	r1, [sp, #4]
	add	r3, r3, r1
	strh	r3, [r2, #3288]	@ movhi
	ldrh	r3, [r10, #1872]
	cmp	r3, r5
	bls	.L3116
	b	.L3117
.L3090:
	ldr	r3, .L3133+4
	movw	r2, #65535
	strh	r5, [r4, #2]	@ movhi
	mov	r0, r4
	mov	r1, r5
	strh	r2, [r3, #3288]	@ movhi
	movs	r2, #0
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
.L3117:
	ldr	r3, .L3133
	ldr	r2, [sp, #76]
	ldr	r0, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3118
	bl	__stack_chk_fail
.L3118:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3134:
	.align	2
.L3133:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC148
	.word	.LC149
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	1
	.global	FtlReadRefresh
	.thumb
	.thumb_func
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r7, .L3152
	ldr	r5, .L3152+4
	ldr	r8, .L3152+8
	ldr	r3, [r7]
	ldr	fp, [r5, #172]
	mov	r6, r5
	str	r3, [sp, #44]
	cmp	fp, #0
	beq	.L3136
	ldr	r2, [r5, #176]
	ldr	r3, [r8, #1932]
	cmp	r2, r3
	bcs	.L3137
	mov	r4, #2048
.L3142:
	ldr	r0, [r6, #176]
	ldr	r3, [r8, #1932]
	cmp	r0, r3
	bcs	.L3141
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r3, [r5, #176]
	adds	r3, r3, #1
	str	r3, [r5, #176]
	ldr	r2, [sp, #4]
	adds	r1, r2, #1
	beq	.L3140
	add	r0, sp, #48
	str	r2, [sp, #12]
	movs	r1, #1
	movs	r2, #0
	str	r2, [r0, #-40]!
	str	r3, [sp, #24]
	str	r2, [sp, #16]
	str	r2, [sp, #20]
	bl	FlashReadPages
	ldr	r3, [sp, #8]
	cmp	r3, #256
	bne	.L3141
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3141:
	mov	r0, #-1
	b	.L3144
.L3140:
	subs	r4, r4, #1
	bne	.L3142
	b	.L3141
.L3137:
	ldr	r3, [r5, #-40]
	movs	r0, #0
	str	r0, [r5, #172]
	str	r0, [r5, #176]
	str	r3, [r5, #168]
	b	.L3144
.L3136:
	ldr	r10, [r5, #-40]
	movw	r4, #10000
	ldr	r1, [r5, #12]
	ldr	r9, [r5, #168]
	add	r2, r10, #1048576
	cmp	r1, r4
	ite	hi
	movhi	r4, #31
	movls	r4, #63
	ldr	r3, [r8, #1932]
	cmp	r9, r2
	add	r8, r8, #4096
	bhi	.L3146
	mov	r0, #1000
	lsrs	r1, r1, #10
	adds	r1, r1, #1
	muls	r0, r3, r0
	bl	__aeabi_uidiv
	add	r0, r0, r9
	cmp	r0, r10
	bcc	.L3146
	ldrh	r3, [r8]
	ands	r0, r4, r3
	bne	.L3149
	ldr	r2, [r5, #192]
	cmp	r2, r3
	beq	.L3144
.L3146:
	ldrh	r3, [r8]
	movs	r0, #0
	str	r10, [r6, #168]
	str	r0, [r6, #176]
	str	r3, [r6, #192]
	movs	r3, #1
	str	r3, [r6, #172]
	b	.L3144
.L3149:
	mov	r0, fp
.L3144:
	ldr	r2, [sp, #44]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L3147
	bl	__stack_chk_fail
.L3147:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3153:
	.align	2
.L3152:
	.word	__stack_chk_guard
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L3193
	ldr	r7, .L3193+4
	ldr	r2, [r6, #4052]
	ldr	r3, [r7]
	ldrh	r1, [r6, #1872]
	str	r3, [sp, #12]
	cmp	r2, #0
	bne	.L3191
	ldr	r4, .L3193+8
	movw	lr, #65535
	ldrh	r5, [r4, #-104]
	cmp	r5, lr
	bne	.L3157
.L3166:
	ldrh	r2, [r4, #-104]
	movw	r3, #65535
	mov	r8, #0
	ldr	r5, .L3193+8
	cmp	r2, r3
	str	r8, [r4, #1928]
	beq	.L3191
	bl	FtlCacheWriteBack
	ldrb	r0, [r5, #-97]	@ zero_extendqisi2
	ldrh	r3, [r6, #1872]
	mov	r10, #12
	ldr	r2, [r5, #-220]
	ldrh	r1, [r5, #-104]
	smulbb	r3, r0, r3
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r3, [r5, #-20]
	ldrh	r2, [r5, #656]
	add	r3, r3, r2
	str	r3, [r5, #-20]
	b	.L3167
.L3157:
	cbz	r0, .L3160
	ldr	r3, .L3193+12
	ldrh	r0, [r3, #3288]
	cmp	r0, lr
	beq	.L3161
.L3162:
	movs	r1, #2
	b	.L3160
.L3161:
	strh	r2, [r3, #3288]	@ movhi
	ldrh	r3, [r4, #-204]
	cmp	r3, #17
	bhi	.L3162
.L3160:
	ldr	r0, .L3193+16
	bl	FtlGcScanTempBlk
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L3163
	ldr	r2, [r6, #4060]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L3164
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L3164:
	ldr	r3, [r4, #1928]
	ldr	r2, .L3193+8
	cbnz	r3, .L3165
	ldr	r0, [sp, #8]
	ldr	r3, [r2, #188]
	ubfx	r0, r0, #10, #16
	adds	r3, r3, #1
	str	r3, [r2, #188]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3165:
	movs	r3, #0
	str	r3, [r4, #1928]
	b	.L3178
.L3163:
	ldr	r3, .L3193+12
	ldrh	r2, [r3, #3288]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3178
	b	.L3166
.L3170:
	ldr	r3, [fp, #4]
	cmp	r0, r3
	bne	.L3189
.L3169:
	add	r8, r8, #1
.L3167:
	ldrh	r3, [r4, #656]
	uxth	r9, r8
	cmp	r3, r9
	bls	.L3192
	mul	r9, r10, r9
	ldr	r3, [r5, #652]
	ldr	r2, [r6, #1932]
	add	fp, r3, r9
	ldr	r0, [fp, #8]
	cmp	r0, r2
	bcs	.L3189
	add	r1, sp, #8
	movs	r2, #0
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [sp, #4]
	ldr	r0, [sp, #8]
	ldr	r3, [r3, r9]
	cmp	r0, r3
	bne	.L3170
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, fp, #4
	movs	r2, #1
	mov	r9, r0
	ldr	r0, [fp, #8]
	bl	log2phys
	mov	r0, r9
	b	.L3190
.L3189:
	ldrh	r0, [r5, #-104]
.L3190:
	bl	decrement_vpc_count
	b	.L3169
.L3192:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbz	r3, .L3172
	ldr	r3, .L3193+8
	ldr	r0, .L3193+20
	ldrh	r1, [r3, #-104]
	bl	printk
.L3172:
	ldrh	r0, [r4, #-104]
	ldr	r3, [r4, #-220]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L3173
	bl	INSERT_DATA_LIST
	b	.L3174
.L3173:
	bl	INSERT_FREE_LIST
.L3174:
	movw	r9, #65535
	movs	r5, #0
	strh	r9, [r4, #-104]	@ movhi
	strh	r5, [r4, #656]	@ movhi
	strh	r5, [r4, #648]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r6, #1796]
	ldr	r8, .L3193+8
	strh	r9, [r4, #40]	@ movhi
	cbz	r3, .L3175
	ldr	r3, [r8, #16]
	cmp	r3, #39
	bhi	.L3175
	ldrh	r3, [r8, #20]
	ldrh	r2, [r8, #-204]
	cmp	r2, r3
	bcs	.L3191
	lsls	r3, r3, #1
	strh	r3, [r8, #604]	@ movhi
	b	.L3191
.L3175:
	ldrh	r3, [r4, #20]
	ldrh	r1, [r4, #-204]
	ldr	r2, .L3193+8
	add	r0, r3, r3, lsl #1
	cmp	r1, r0, asr #2
	ble	.L3191
	ldrb	r0, [r6, #140]	@ zero_extendqisi2
	cbz	r0, .L3176
	subs	r3, r3, #2
	strh	r3, [r2, #604]	@ movhi
.L3191:
	movs	r0, #0
	b	.L3156
.L3176:
	movs	r3, #20
	strh	r3, [r2, #604]	@ movhi
	b	.L3156
.L3178:
	movs	r0, #1
.L3156:
	ldr	r2, [sp, #12]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L3177
	bl	__stack_chk_fail
.L3177:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3194:
	.align	2
.L3193:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-104
	.word	.LC150
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
	ldr	r5, .L3197
	ldr	r4, .L3197+4
	ldrh	r1, [r5, #1872]
	sub	r0, r4, #104
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #-102]
	ldrh	r3, [r5, #1872]
	cmp	r2, r3
	bcc	.L3195
	addw	r0, r4, #1868
	bl	FtlMapBlkWriteDumpData
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #1928]
.L3195:
	pop	{r3, r4, r5, pc}
.L3198:
	.align	2
.L3197:
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
	ldr	r4, .L3200
	ldr	r3, .L3200+4
	sub	r6, r4, #200
	subs	r4, r4, #152
	mov	r0, r6
	str	r5, [r3, #3648]
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
.L3201:
	.align	2
.L3200:
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movs	r3, #0
	ldr	r5, .L3219
	movw	r6, #65535
	ldr	r7, .L3219+4
	ldr	r4, .L3219+8
	ldrh	r0, [r5, #1808]
	strh	r3, [r7, #3646]	@ movhi
	strh	r6, [r7, #3644]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r3, [r4, #24]
	cmp	r3, r6
	bne	.L3203
.L3205:
	mov	r8, #-1
	b	.L3204
.L3203:
	bl	FtlLoadSysInfo
	mov	r8, r0
	cmp	r0, #0
	bne	.L3205
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r5, #1910]
	ldr	r2, [r4, #-56]
	movs	r0, #12
	mov	r3, r8
.L3206:
	cmp	r3, r1
	bge	.L3211
	mla	r6, r0, r3, r2
	ldr	r6, [r6, #4]
	cmp	r6, #0
	bge	.L3207
.L3211:
	ldr	r6, .L3219+12
	cmp	r3, r1
	ldrh	r2, [r6]
	add	r2, r2, #1
	strh	r2, [r6]	@ movhi
	bge	.L3218
	b	.L3208
.L3207:
	adds	r3, r3, #1
	b	.L3206
.L3218:
	ldrh	r3, [r7, #3646]
	cbz	r3, .L3212
.L3208:
	ldrh	r1, [r4, #-200]
	ldr	r2, [r4, #-220]
	ldrh	r0, [r4, #-196]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r1, [r4, #-220]
	ldrh	r3, [r5, #1872]
	ldrh	r0, [r4, #-152]
	ldrh	r7, [r4, #-148]
	strh	r3, [r4, #-198]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #-194]
	strh	r3, [r4, #-196]	@ movhi
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r7
	strh	r2, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r5, #1872]
	strb	r3, [r4, #-146]
	strh	r3, [r4, #-148]	@ movhi
	strh	r2, [r4, #-150]	@ movhi
	movw	r2, #4098
	ldrh	r3, [r5, r2]
	adds	r3, r3, #1
	strh	r3, [r5, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3212:
	ldrh	r0, [r4, #-200]
	movw	r3, #65535
	ldr	r7, .L3219+8
	cmp	r0, r3
	beq	.L3213
	ldrh	r3, [r7, #-196]
	cbnz	r3, .L3213
	ldrh	r3, [r7, #-148]
	cbnz	r3, .L3213
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r7, #-152]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	sub	r0, r7, #200
	bl	allocate_new_data_superblock
	sub	r0, r7, #152
	bl	allocate_new_data_superblock
.L3213:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cbnz	r3, .L3214
	ldrh	r3, [r6]
	lsls	r3, r3, #27
	bne	.L3204
.L3214:
	bl	FtlVpcCheckAndModify
.L3204:
	mov	r0, r8
	pop	{r4, r5, r6, r7, r8, pc}
.L3220:
	.align	2
.L3219:
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LANCHOR2
	.word	.LANCHOR0+4096
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
	ldr	r5, .L3255
	ldr	r6, [r5, #4052]
	cmp	r6, #0
	bne	.L3223
	ldr	r4, .L3255+4
	mov	r1, r6
	ldrh	r2, [r5, #1908]
	ldr	r0, [r4, #1852]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #1908]
	mov	r1, r6
	ldr	r0, [r4, #1848]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r5, #1808]
	str	r6, [r4, #-8]
	str	r6, [r4, #-4]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3224
	bl	FtlMakeBbt
.L3224:
	ldr	r0, .L3255+8
	movs	r2, #0
.L3225:
	ldrh	r1, [r5, #1878]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	bge	.L3252
	ldr	r6, [r4, #1792]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r6, r3, lsl #2]
	ldr	r1, [r4, #1796]
	str	r0, [r1, r3, lsl #2]
	b	.L3225
.L3252:
	ldr	r3, .L3255
	movs	r6, #0
	ldrh	r7, [r3, #1812]
.L3227:
	ldrh	r3, [r5, #1814]
	ldr	r8, .L3255
	cmp	r3, r7
	bls	.L3253
	mov	r0, r7
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r7, r7, #1
	uxth	r7, r7
	add	r0, r0, r6
	uxth	r6, r0
	b	.L3227
.L3253:
	ldrh	r1, [r8, #1804]
	subs	r3, r6, #3
	cmp	r3, r1, lsl #1
	bge	.L3229
.L3233:
	movs	r6, #0
	mov	r7, r6
	b	.L3230
.L3229:
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, [r8, #1904]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r8, #1808]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r6, [r8, #1812]
.L3231:
	ldrh	r3, [r5, #1814]
	cmp	r3, r6
	bls	.L3233
	mov	r0, r6
	adds	r6, r6, #1
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	uxth	r6, r6
	b	.L3231
.L3230:
	ldrh	r3, [r5, #1812]
	uxth	r0, r6
	ldr	r10, .L3255
	adds	r6, r6, #1
	cmp	r3, r0
	bls	.L3254
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r0, r7
	uxth	r7, r0
	b	.L3230
.L3254:
	ldrh	r3, [r10, #1814]
	ldrh	r6, [r10, #1804]
	ldr	fp, [r10, #1816]
	str	r3, [r4, #1752]
	mov	r1, r6
	mov	r0, fp
	bl	__aeabi_uidiv
	ubfx	r8, r0, #5, #16
	mov	r9, r0
	add	r3, r8, #36
	strh	r3, [r4, #20]	@ movhi
	movs	r3, #24
	str	r0, [r10, #1932]
	muls	r3, r6, r3
	cmp	r7, r3
	ble	.L3235
	rsb	r0, r7, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, .L3255+4
	str	r0, [r10, #1932]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r3, #20]	@ movhi
.L3235:
	ldr	r3, [r5, #1796]
	cmp	r3, #1
	bne	.L3236
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r10, [r4, #20]
	uxtah	r0, r10, r0
	add	r10, r10, r0, asr #2
	strh	r10, [r4, #20]	@ movhi
.L3236:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cbz	r3, .L3237
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r10, [r4, #20]
	uxtah	r0, r10, r0
	add	r10, r10, r0, asr #2
	strh	r10, [r4, #20]	@ movhi
.L3237:
	ldrh	r3, [r5, #1866]
	cbz	r3, .L3239
	ldrh	r2, [r4, #20]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #20]	@ movhi
	mul	r2, r6, r3
	cmp	r2, r7
	itttt	gt
	addgt	r3, r3, #32
	ldrgt	r2, .L3255
	addgt	r3, r3, r8
	strgt	r9, [r2, #1932]
	itt	gt
	ldrgt	r2, .L3255+4
	strhgt	r3, [r2, #20]	@ movhi
.L3239:
	ldrh	r2, [r4, #20]
	ldr	r3, [r5, #1932]
	ldr	r7, .L3255+4
	subs	r3, r3, r2
	muls	r6, r3, r6
	ldrh	r3, [r5, #1872]
	str	r6, [r4, #4088]
	muls	r6, r3, r6
	ldrh	r3, [r5, #1878]
	str	r6, [r5, #1932]
	muls	r6, r3, r6
	str	r6, [r5, #1912]
	bl	FtlBbmTblFlush
	ldrh	r2, [r5, #1814]
	movs	r1, #0
	ldr	r0, [r4, #-220]
	movw	r6, #65535
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #1812]
	ldr	r0, [r4, #-44]
	movs	r1, #255
	movs	r3, #0
	strh	r6, [r4, #40]	@ movhi
	lsrs	r2, r2, #3
	str	r3, [r4, #-48]
	strh	r3, [r4, #42]	@ movhi
	strb	r3, [r4, #46]
	strb	r3, [r4, #48]
	strh	r3, [r4, #-198]	@ movhi
	strb	r3, [r4, #-194]
	strh	r3, [r4, #-200]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #-192]
	bl	ftl_memset
.L3241:
	ldr	r5, .L3255+4
	sub	r0, r5, #200
	bl	make_superblock
	ldrb	r3, [r4, #-193]	@ zero_extendqisi2
	ldrh	r2, [r4, #-200]
	cbnz	r3, .L3242
	ldr	r3, [r7, #-220]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r7, #-200]
	adds	r3, r3, #1
	strh	r3, [r7, #-200]	@ movhi
	b	.L3241
.L3242:
	ldr	r3, [r5, #-8]
	movw	r7, #65535
	ldrh	r1, [r5, #-196]
	str	r3, [r5, #-188]
	adds	r3, r3, #1
	str	r3, [r5, #-8]
	ldr	r3, [r5, #-220]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r5, #-150]	@ movhi
	strb	r3, [r5, #-146]
	ldrh	r3, [r5, #-200]
	adds	r3, r3, #1
	strh	r3, [r5, #-152]	@ movhi
	movs	r3, #1
	strb	r3, [r5, #-144]
.L3243:
	ldr	r6, .L3255+4
	sub	r0, r6, #152
	bl	make_superblock
	ldrb	r3, [r4, #-145]	@ zero_extendqisi2
	ldrh	r2, [r4, #-152]
	cbnz	r3, .L3244
	ldr	r3, [r5, #-220]
	strh	r7, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r5, #-152]
	adds	r3, r3, #1
	strh	r3, [r5, #-152]	@ movhi
	b	.L3243
.L3244:
	ldr	r3, [r6, #-8]
	movw	r4, #65535
	ldrh	r1, [r6, #-148]
	str	r3, [r6, #-140]
	adds	r3, r3, #1
	str	r3, [r6, #-8]
	ldr	r3, [r6, #-220]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r4, [r6, #-104]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r3, [r6, #26]	@ movhi
	ldr	r3, [r6, #4088]
	strh	r4, [r6, #28]	@ movhi
	strh	r3, [r6, #30]	@ movhi
	ldr	r3, [r6, #-8]
	str	r3, [r6, #32]
	adds	r3, r3, #1
	str	r3, [r6, #-8]
	strh	r0, [r6, #24]	@ movhi
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L3223
	ldr	r3, .L3255+12
	movs	r2, #1
	str	r2, [r3, #504]
.L3223:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3256:
	.align	2
.L3255:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	168778952
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L3286
	ldr	r0, .L3286+4
	ldr	r3, [r5]
	mov	r4, r0
	str	r3, [sp, #20]
	ldrb	r3, [r0, #140]	@ zero_extendqisi2
	cbz	r3, .L3258
.L3260:
	movs	r0, #0
	b	.L3259
.L3258:
	ldr	r6, .L3286+8
	ldr	r0, [r6, #3984]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3260
	bl	FlashLoadFactorBbt
	cbz	r0, .L3261
	bl	FlashMakeFactorBbt
.L3261:
	ldr	r0, [r6, #4016]
	bl	FlashReadIdbDataRaw
	cbz	r0, .L3262
	movs	r2, #16
	add	r0, sp, #4
	movs	r1, #0
	movs	r7, #1
	bl	FlashReadFacBbtData
	movs	r3, #0
	mov	r0, r3
	ldr	r2, [sp, #4]
.L3263:
	lsl	r6, r7, r0
	adds	r1, r3, #1
	tst	r6, r2
	add	r0, r0, #1
	it	ne
	movne	r3, r1
	cmp	r0, #16
	bne	.L3263
	cmp	r3, #6
	bls	.L3282
	movs	r0, #0
	movs	r7, #1
.L3266:
	lsl	r6, r7, r0
	adds	r1, r3, #1
	tst	r6, r2
	add	r0, r0, #1
	it	ne
	movne	r3, r1
	cmp	r0, #24
	bne	.L3266
	cmp	r3, #17
	bhi	.L3267
.L3282:
	strb	r0, [r4, #1]
	b	.L3265
.L3267:
	movs	r3, #36
	strb	r3, [r4, #1]
.L3265:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strh	r3, [r4, #138]	@ movhi
.L3262:
	ldr	r1, .L3286+12
	ldr	r0, .L3286+16
	bl	printk
	ldr	r0, .L3286+20
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #1808]
	bl	FtlFreeSysBlkQueueInit
	movs	r4, #1
.L3268:
	bl	FtlLoadBbt
	cbz	r0, .L3269
.L3284:
	bl	FtlLowFormat
	cmp	r4, #3
	bhi	.L3285
	adds	r4, r4, #1
	b	.L3268
.L3285:
	mov	r0, #-1
	b	.L3259
.L3269:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3284
	ldr	r3, .L3286+24
	movs	r2, #1
	str	r2, [r3, #504]
.L3259:
	ldr	r2, [sp, #20]
	ldr	r3, [r5]
	cmp	r2, r3
	beq	.L3273
	bl	__stack_chk_fail
.L3273:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L3287:
	.align	2
.L3286:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r3, .L3304
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r2, [r3, #4052]
	cbz	r2, .L3289
.L3292:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3289:
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	ldr	r5, .L3304+4
	cbz	r3, .L3291
	ldr	r3, [r5, #620]
	lsls	r3, r3, #31
	bpl	.L3291
	ldrh	r3, [r5, #-100]
	cmp	r3, #0
	bne	.L3292
.L3291:
	movs	r2, #0
	ldr	r0, [r5, #1768]
	ldr	r1, [r5, #620]
	movs	r6, #0
	mov	r3, r2
	ldr	r7, .L3304+4
	bl	FlashProgPages
	mov	r8, #36
.L3293:
	ldr	r1, [r5, #620]
	uxth	r3, r6
	ldr	r4, .L3304+4
	cmp	r3, r1
	bcs	.L3303
	mul	r3, r8, r3
	ldr	r0, [r7, #1768]
	adds	r6, r6, #1
	adds	r1, r0, r3
	ldr	r0, [r0, r3]
	ldr	r2, [r1, #12]
	cmp	r0, #-1
	bne	.L3294
	ldrh	r1, [r4, #-104]
	movs	r5, #0
	ldr	r2, [r4, #-220]
	strh	r5, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #188]
	strh	r0, [r4, #-104]	@ movhi
	adds	r2, r2, #1
	str	r2, [r4, #188]
	ldr	r2, [r4, #1768]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3302
.L3294:
	ldr	r0, [r2, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r2, #8]
	bl	FtlGcUpdatePage
	b	.L3293
.L3303:
	ldr	r0, [r4, #1768]
	bl	FtlGcBufFree
	movs	r3, #0
	str	r3, [r4, #620]
	ldrh	r3, [r4, #-100]
	cmp	r3, #0
	bne	.L3292
	movs	r0, #1
	bl	FtlGcFreeTempBlock
.L3302:
	movs	r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L3305:
	.align	2
.L3304:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r4, .L3309
	ldrh	r2, [r4, #-104]
	cmp	r2, r3
	beq	.L3307
	ldrh	r3, [r4, #-100]
	cbnz	r3, .L3308
.L3307:
	bl	FtlCacheWriteBack
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3309+4
	strb	r5, [r4, #-96]
	bl	allocate_data_superblock
	strh	r5, [r4, #648]	@ movhi
	strh	r5, [r4, #656]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3308:
	ldr	r0, .L3309+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L3310:
	.align	2
.L3309:
	.word	.LANCHOR2
	.word	.LANCHOR2-104
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	1
	.global	ftl_do_gc
	.thumb
	.thumb_func
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r3, .L3486
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L3486+4
	mov	r9, r1
	ldr	r3, [r3]
	ldr	r0, [r6, #4052]
	str	r3, [sp, #44]
	cmp	r0, #0
	bne	.L3412
	ldr	r2, .L3486+8
	ldr	r3, [r2, #504]
	adds	r3, r3, #1
	beq	.L3312
	ldr	r3, .L3486+12
	ldr	r4, [r3, #1740]
	mov	r7, r3
	cmp	r4, #0
	bne	.L3312
	ldrh	r1, [r3, #-212]
	cmp	r1, #47
	bls	.L3415
	ldrh	r1, [r2, #3288]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3313
.L3316:
	ldrh	r4, [r7, #660]
	movw	r1, #65535
	ldr	r3, .L3486+12
	cmp	r4, r1
	bne	.L3314
	b	.L3315
.L3313:
	ldrh	r3, [r3, #-104]
	cmp	r3, r2
	beq	.L3316
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3316
	movs	r0, #1
	b	.L3312
.L3314:
	ldrh	r2, [r3, #658]
	cmp	r2, r1
	bne	.L3315
	ldrh	r0, [r3, #662]
	cmp	r0, r2
	beq	.L3315
	ldrh	r1, [r3, #664]
	cmp	r1, r2
	itttt	ne
	strhne	r4, [r3, #658]	@ movhi
	strhne	r0, [r3, #660]	@ movhi
	strhne	r1, [r3, #662]	@ movhi
	strhne	r2, [r3, #664]	@ movhi
.L3315:
	ldr	r4, [r7, #608]
	cmp	r8, #1
	add	r4, r4, #1
	add	r4, r4, r8, lsl #7
	str	r4, [r7, #608]
	bne	.L3327
	ldr	r3, [r6, #1796]
	cbnz	r3, .L3318
	ldr	r3, .L3486+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3327
.L3318:
	ldr	r3, [r7, #16]
	ldr	r5, .L3486+12
	cmp	r3, #39
	bhi	.L3327
	ldr	r10, .L3486+16
	ldrh	r3, [r10, #3780]
	add	r4, r4, r3
	str	r4, [r5, #608]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r5, #40]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3327
	ldrh	r2, [r5, #658]
	cmp	r2, r3
	bne	.L3327
	ldr	r3, [r5, #608]
	cmp	r3, #1024
	bhi	.L3319
	ldrh	r3, [r5, #-204]
	cmp	r3, #63
	bhi	.L3327
.L3319:
	ldrh	r3, [r7, #606]
	movs	r0, #0
	ldrh	r4, [r7, #-204]
	adds	r3, r3, #64
	ldr	r2, .L3486+16
	cmp	r4, r3
	strh	r0, [r10, #3780]	@ movhi
	ldr	r1, .L3486+12
	bgt	.L3327
	ldr	r3, [r1, #16]
	str	r0, [r1, #608]
	cbnz	r3, .L3320
	movs	r3, #6
	b	.L3473
.L3320:
	cmp	r3, #5
	bhi	.L3321
	movs	r3, #18
.L3473:
	strh	r3, [r2, #3780]	@ movhi
.L3321:
	movs	r0, #32
	bl	List_get_gc_head_node
	movw	r1, #65535
	uxth	ip, r0
	cmp	ip, r1
	beq	.L3326
	ldrh	r0, [r7, #612]
	ldr	r5, .L3486+12
	cmp	r0, #0
	beq	.L3323
	ldrh	lr, [r6, #1874]
	ldrh	r3, [r6, #1804]
	ldr	r2, [r5, #-220]
	mul	r3, r3, lr
	ldrh	r4, [r2, ip, lsl #1]
	adds	r3, r3, #1
	cmp	r4, r3
	bgt	.L3326
	adds	r3, r0, #1
	str	r1, [sp, #24]
	str	ip, [sp, #20]
	mov	fp, #0
	uxth	r3, r3
	str	r2, [sp, #16]
	strh	r3, [r5, #612]	@ movhi
	str	r3, [sp, #12]
	str	fp, [r5, #616]
	bl	List_get_gc_head_node
	ldr	r1, [sp, #24]
	uxth	r4, r0
	ldr	r3, [sp, #12]
	cmp	r4, r1
	ldr	r2, [sp, #16]
	ldr	ip, [sp, #20]
	beq	.L3326
	ldrh	lr, [r2, r4, lsl #1]
	mov	r1, r3
	ldrh	r2, [r2, ip, lsl #1]
	ldr	r0, .L3486+20
	mov	r3, lr
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, #612]
	cmp	r3, #40
	bls	.L3324
	ldr	r3, [r5, #-220]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r3, #32
	it	hi
	strhhi	fp, [r5, #612]	@ movhi
.L3324:
	movs	r3, #6
	strh	r3, [r10, #3780]	@ movhi
	b	.L3343
.L3323:
	movs	r3, #1
	strh	r3, [r5, #612]	@ movhi
.L3326:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L3343
	movs	r3, #0
	strh	r3, [r10, #3780]	@ movhi
.L3327:
	ldrh	r3, [r7, #40]
	movw	r4, #65535
	ldr	r5, .L3486+12
	cmp	r3, r4
	bne	.L3478
	ldrh	r4, [r5, #-104]
	cmp	r4, r3
	bne	.L3328
	ldrh	fp, [r5, #658]
	cmp	fp, r4
	bne	.L3329
	ldrh	r3, [r5, #-204]
	ldr	r2, [r5, #608]
	cmp	r3, #24
	ite	cc
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	bls	.L3331
	ldr	r2, .L3486+16
	movs	r3, #0
	str	r3, [r7, #608]
	strh	r3, [r2, #3780]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, fp
	mov	r4, r0
	bne	.L3332
	ldrh	r2, [r5, #-204]
	ldrh	r3, [r5, #606]
	cmp	r2, r3
	bcs	.L3333
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	bne	.L3479
.L3342:
	bl	FtlGcReFreshBadBlk
	cmp	r8, #0
	beq	.L3480
.L3343:
	ldrh	r2, [r7, #-104]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3354
.L3409:
	ldrh	r3, [r7, #658]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3354
	cmp	r4, r3
	bne	.L3354
.L3410:
	ldrh	r1, [r7, #40]
	movw	r3, #65535
	ldr	r2, .L3486+12
	cmp	r1, r3
	beq	.L3355
.L3359:
	movw	r4, #65535
	b	.L3354
.L3355:
	movs	r3, #0
	ldrh	r1, [r2, #-204]
	str	r3, [r2, #616]
	ldrh	r3, [r2, #604]
	ldr	r4, .L3486+16
	cmp	r1, r3
	bls	.L3356
	ldrh	r3, [r4, #3780]
	cbnz	r3, .L3357
	ldr	r3, [r6, #1932]
	ldr	r2, [r2, #-48]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L3423
.L3357:
	ldrh	r3, [r7, #20]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
	b	.L3358
.L3423:
	movs	r3, #18
.L3358:
	strh	r3, [r7, #604]	@ movhi
	bl	FtlReadRefresh
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-220]
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #4
	bls	.L3356
	ldrh	r0, [r4, #3780]
	b	.L3312
.L3356:
	ldrh	r0, [r4, #3780]
	cmp	r0, #0
	bne	.L3359
	ldrh	r4, [r7, #20]
	add	r3, r4, r4, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r7, #604]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-220]
	ldrh	r1, [r6, #1874]
	uxth	r0, r0
	ldrh	r2, [r3, r0, lsl #1]
	ldrh	r3, [r6, #1804]
	muls	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L3360
	ldr	r3, .L3486+12
	subs	r4, r4, #1
	ldrh	r3, [r3, #-204]
	cmp	r3, r4
	blt	.L3360
	bl	FtlReadRefresh
	b	.L3477
.L3487:
	.align	2
.L3486:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LC151
.L3360:
	cmp	r2, #0
	bne	.L3359
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r7, #-204]
	adds	r0, r0, #1
	b	.L3312
.L3354:
	ldr	r3, [r6, #1796]
	cmp	r3, #0
	ite	eq
	moveq	r5, #1
	movne	r5, #2
	b	.L3353
.L3480:
	movw	r3, #65535
	cmp	r4, r3
	bne	.L3343
.L3408:
	ldrh	r4, [r7, #-204]
	cmp	r4, #24
	bhi	.L3419
	cmp	r4, #16
	ldrh	r5, [r6, #1872]
	bls	.L3345
	lsrs	r5, r5, #5
.L3344:
	ldrh	r3, [r7, #604]
	ldr	r2, .L3488
	cmp	r3, r4
	bcc	.L3481
.L3348:
	ldr	r3, [r6, #1796]
	cmp	r3, #0
	bne	.L3482
.L3422:
	movw	r4, #65535
.L3353:
	ldrh	r2, [r7, #40]
	movw	r1, #65535
	ldr	r3, .L3488
	cmp	r2, r1
	bne	.L3362
	cmp	r4, r2
	beq	.L3363
	strh	r4, [r3, #40]	@ movhi
	b	.L3364
.L3363:
	ldrh	r2, [r3, #658]
	cmp	r2, r4
	beq	.L3364
	ldr	r1, [r3, #-220]
	ldrh	r2, [r1, r2, lsl #1]
	cbnz	r2, .L3365
	strh	r4, [r3, #658]	@ movhi
.L3365:
	ldrh	r3, [r7, #658]
	strh	r3, [r7, #40]	@ movhi
	movw	r3, #65535
	strh	r3, [r7, #658]	@ movhi
.L3364:
	ldrh	r0, [r7, #40]
	movw	r9, #65535
	movs	r3, #0
	strb	r3, [r7, #48]
	cmp	r0, r9
	beq	.L3362
	bl	IsBlkInGcList
	cbz	r0, .L3367
	ldr	r3, .L3488
	strh	r9, [r3, #40]	@ movhi
.L3367:
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbz	r3, .L3368
	ldrh	r0, [r7, #40]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #48]
.L3368:
	ldrh	r2, [r7, #40]
	movw	r3, #65535
	ldr	r6, .L3488
	cmp	r2, r3
	beq	.L3362
	add	r0, r6, #40
	bl	make_superblock
	ldr	r2, .L3488+4
	movs	r3, #0
	ldrh	r1, [r6, #40]
	strh	r3, [r6, #42]	@ movhi
	strb	r3, [r6, #46]
	strh	r3, [r2, #3782]	@ movhi
	ldr	r3, [r6, #-220]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #3784]	@ movhi
.L3362:
	ldrh	r3, [r7, #40]
	ldrh	r1, [r7, #-200]
	ldr	r2, .L3488
	cmp	r1, r3
	beq	.L3369
	ldrh	r1, [r2, #-152]
	cmp	r1, r3
	beq	.L3369
	ldrh	r2, [r2, #-104]
	cmp	r2, r3
	bne	.L3370
.L3369:
	movw	r3, #65535
	strh	r3, [r7, #40]	@ movhi
.L3370:
	ldr	r7, .L3488+8
.L3403:
	ldr	r6, .L3488
	movw	r3, #65535
	ldrh	r2, [r6, #40]
	mov	fp, r6
	cmp	r2, r3
	bne	.L3371
	mov	r10, r6
	movs	r3, #0
	str	r3, [r6, #616]
.L3372:
	ldrh	r9, [fp, #612]
	mov	r0, r9
	bl	List_get_gc_head_node
	movw	r2, #65535
	uxth	r3, r0
	strh	r3, [fp, #40]	@ movhi
	cmp	r3, r2
	bne	.L3373
	ldr	r3, .L3488
	movs	r2, #0
	movs	r0, #8
	strh	r2, [r3, #612]	@ movhi
	b	.L3312
.L3373:
	mov	r0, r3
	str	r3, [sp, #12]
	bl	IsBlkInGcList
	add	r9, r9, #1
	ldr	r3, [sp, #12]
	cbz	r0, .L3374
	strh	r9, [r10, #612]	@ movhi
	b	.L3372
.L3374:
	ldrh	lr, [r7, #1804]
	uxth	r9, r9
	ldrh	r2, [r7, #1872]
	ldr	r1, [r10, #-220]
	strh	r9, [r10, #612]	@ movhi
	mul	r2, lr, r2
	ldrh	r0, [r1, r3, lsl #1]
	add	lr, r2, r2, lsr #31
	cmp	r0, lr, asr #1
	bgt	.L3376
	cmp	r9, #48
	bls	.L3377
	cmp	r0, #8
	bls	.L3377
	ldrh	r0, [r10, #648]
	cmp	r0, #35
	bhi	.L3377
.L3376:
	movs	r0, #0
	strh	r0, [fp, #612]	@ movhi
.L3377:
	ldrh	r1, [r1, r3, lsl #1]
	cmp	r1, r2
	blt	.L3378
	movw	r2, #65535
	cmp	r4, r2
	bne	.L3378
	ldrh	r0, [fp, #612]
	ldr	r2, .L3488
	cmp	r0, #3
	bhi	.L3378
	movs	r3, #0
	strh	r4, [r2, #40]	@ movhi
	strh	r3, [r2, #612]	@ movhi
.L3477:
	ldr	r3, .L3488+4
	ldrh	r0, [r3, #3780]
	b	.L3312
.L3378:
	cbnz	r1, .L3379
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [fp, #612]
	adds	r3, r3, #1
	strh	r3, [fp, #612]	@ movhi
	b	.L3372
.L3379:
	movs	r2, #0
	strb	r2, [r6, #48]
	ldr	r2, .L3488+8
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cbz	r2, .L3380
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3488
	strb	r0, [r3, #48]
.L3380:
	ldr	r0, .L3488+12
	bl	make_superblock
	ldr	r2, .L3488+4
	ldrh	r0, [r6, #40]
	movs	r3, #0
	ldr	r1, [r6, #-220]
	strh	r3, [r2, #3782]	@ movhi
	ldrh	r1, [r1, r0, lsl #1]
	strh	r3, [r6, #42]	@ movhi
	strb	r3, [r6, #46]
	strh	r1, [r2, #3784]	@ movhi
.L3371:
	cmp	r8, #1
	bne	.L3381
	bl	FtlReadRefresh
.L3381:
	movs	r3, #1
	str	r3, [r6, #1740]
	ldrh	r3, [r7, #1872]
	str	r3, [sp, #12]
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbz	r3, .L3382
	ldr	r3, .L3488
	ldrb	r3, [r3, #48]	@ zero_extendqisi2
	cmp	r3, #1
	itt	eq
	ldrheq	r3, [r7, #1874]
	streq	r3, [sp, #12]
.L3382:
	ldrh	r3, [r6, #42]
	ldr	r1, [sp, #12]
	adds	r2, r3, r5
	cmp	r2, r1
	ittt	gt
	movgt	r2, r1
	subgt	r3, r2, r3
	uxthgt	r5, r3
	movs	r3, #0
.L3476:
	str	r3, [sp, #16]
	ldrh	r3, [sp, #16]
	ldr	r6, .L3488
	cmp	r3, r5
	bcs	.L3391
	ldr	r3, [r6, #636]
	add	r1, r6, #54
	ldr	r2, [sp, #16]
	movw	lr, #65535
	ldrh	r10, [r7, #1804]
	str	r3, [sp, #20]
	ldrh	r3, [r6, #42]
	add	ip, r3, r2
	movs	r3, #0
	mov	r9, r3
.L3392:
	uxth	r2, r3
	cmp	r2, r10
	bcs	.L3483
	ldrh	r2, [r1, #2]!
	cmp	r2, lr
	beq	.L3385
	ldr	r0, [sp, #20]
	mov	fp, #36
	orr	r2, ip, r2, lsl #10
	mla	fp, fp, r9, r0
	add	r9, r9, #1
	uxth	r9, r9
	str	r2, [fp, #4]
.L3385:
	adds	r3, r3, #1
	b	.L3392
.L3489:
	.align	2
.L3488:
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LANCHOR0
	.word	.LANCHOR2+40
.L3483:
	ldr	r0, [r6, #636]
	mov	r1, r9
	ldrb	r2, [r6, #48]	@ zero_extendqisi2
	mov	fp, #0
	bl	FlashReadPages
	ldr	r6, .L3490
.L3387:
	uxth	r3, fp
	cmp	r3, r9
	bcs	.L3484
	mov	ip, #36
	ldr	r2, [r6, #636]
	mul	r10, ip, fp
	add	r3, r2, r10
	ldr	r2, [r2, r10]
	ldr	r3, [r3, #12]
	adds	r2, r2, #1
	beq	.L3426
	ldrh	r1, [r3]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3426
	add	r1, sp, ip
	ldr	r0, [r3, #8]
	movs	r2, #0
	str	ip, [sp, #24]
	str	r3, [sp, #20]
	bl	log2phys
	ldr	r1, [r6, #636]
	add	r1, r1, r10
	ldr	r0, [r1, #4]
	ldr	r2, [sp, #36]
	ldr	r3, [sp, #20]
	bic	r2, r2, #-2147483648
	ldr	ip, [sp, #24]
	cmp	r2, r0
	bne	.L3426
	str	r3, [sp, #28]
	ldr	r3, .L3490+4
	ldr	r0, [r6, #620]
	ldr	r1, [r1, #16]
	ldrh	r2, [r3, #3782]
	adds	r2, r2, #1
	strh	r2, [r3, #3782]	@ movhi
	ldr	r2, [r6, #1768]
	mla	r2, ip, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #20]
	bl	Ftl_get_new_temp_ppa
	ldr	r1, [r6, #620]
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #24]
	ldr	r3, [sp, #28]
	str	r0, [r2, #4]
	ldr	r2, [r6, #1768]
	mla	ip, ip, r1, r2
	ldr	r2, [r6, #636]
	add	r2, r2, r10
	ldr	r1, [r2, #8]
	str	r1, [ip, #8]
	movs	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [ip, #12]
	ldr	r2, [sp, #36]
	str	r2, [r3, #12]
	ldrh	r2, [r6, #-104]
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r6, #-4]
	ldr	r0, [r6, #636]
	str	r2, [r3, #4]
	add	r0, r0, r10
	ldr	r3, [r6, #620]
	adds	r3, r3, #1
	str	r3, [r6, #620]
	bl	FtlGcBufAlloc
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbnz	r3, .L3389
	ldrb	r3, [r6, #-97]	@ zero_extendqisi2
	ldr	r2, [r6, #620]
	cmp	r2, r3
	beq	.L3389
	ldrh	r3, [r6, #-100]
	cbnz	r3, .L3426
.L3389:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L3426
	ldr	r3, .L3490
	movs	r2, #0
	movw	r1, #65535
	str	r2, [r3, #1740]
	strh	r1, [r3, #40]	@ movhi
	strh	r2, [r3, #42]	@ movhi
	b	.L3477
.L3426:
	add	fp, fp, #1
	b	.L3387
.L3484:
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	b	.L3476
.L3391:
	ldrh	r3, [r6, #42]
	add	r5, r5, r3
	ldr	r3, [sp, #12]
	uxth	r5, r5
	strh	r5, [r6, #42]	@ movhi
	cmp	r5, r3
	bcc	.L3393
	ldr	r3, [r6, #620]
	ldr	r5, .L3490+4
	cbz	r3, .L3394
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L3394
	movs	r3, #0
	ldrh	r0, [r5, #3780]
	str	r3, [r6, #1740]
	b	.L3312
.L3394:
	ldrh	r5, [r5, #3782]
	cbnz	r5, .L3395
	ldrh	r2, [r6, #40]
	ldr	r3, [r6, #-220]
	ldrh	r3, [r3, r2, lsl #1]
	cbz	r3, .L3395
.L3396:
	ldr	r3, [r7, #1932]
	cmp	r5, r3
	bcs	.L3401
	mov	r0, r5
	add	r1, sp, #40
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #40]
	adds	r3, r0, #1
	beq	.L3397
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6, #40]
	cmp	r3, r0
	bne	.L3397
.L3401:
	ldr	r3, .L3490+8
	ldr	r3, [r3, #1932]
	cmp	r5, r3
	bcc	.L3395
	ldrh	r2, [r6, #40]
	movs	r1, #0
	ldr	r3, [r6, #-220]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r6, #40]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3395
.L3397:
	adds	r5, r5, #1
	b	.L3396
.L3395:
	movw	r3, #65535
	strh	r3, [r6, #40]	@ movhi
.L3393:
	ldrh	r3, [r6, #-204]
	cmp	r3, #2
	bhi	.L3402
	ldrh	r5, [r7, #1872]
	b	.L3403
.L3402:
	ldr	r2, .L3490
	movs	r1, #0
	str	r1, [r2, #1740]
	ldr	r2, .L3490+4
	ldrh	r0, [r2, #3780]
	cbnz	r0, .L3312
	adds	r0, r3, #1
	b	.L3312
.L3412:
	movs	r0, #0
	b	.L3312
.L3415:
	mov	r0, r4
	b	.L3312
.L3481:
	ldrh	r3, [r2, #-104]
	movw	r1, #65535
	cmp	r3, r1
	bne	.L3349
	ldrh	r1, [r2, #658]
	cmp	r1, r3
	bne	.L3349
	ldr	r3, .L3490+4
	ldrh	r0, [r3, #3780]
	cmp	r0, #0
	beq	.L3485
.L3350:
	ldrh	r3, [r7, #20]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
.L3351:
	strh	r3, [r7, #604]	@ movhi
	movs	r3, #0
	str	r3, [r7, #616]
.L3312:
	ldr	r3, .L3490+12
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3411
	bl	__stack_chk_fail
.L3479:
	ldr	r2, [r5, #1736]
	cbnz	r2, .L3335
	ldrh	r1, [r6, #1824]
	ldr	r2, .L3490+8
	cmp	r1, #3
	beq	.L3335
	ldr	r1, [r5, #88]
	cbnz	r1, .L3335
	ldr	r1, [r2, #1796]
	cbnz	r1, .L3335
	ldrb	r0, [r2, #140]	@ zero_extendqisi2
	cbz	r0, .L3336
.L3335:
	ldr	r2, [r7, #-220]
	ldrh	r0, [r6, #1824]
	ldrh	r1, [r2, r3, lsl #1]
	cmp	r0, #3
	ldrh	r3, [r6, #1874]
	ldrh	r2, [r6, #1804]
	mul	r2, r2, r3
	ite	eq
	lsreq	r3, r3, #1
	movne	r3, #0
	add	r3, r3, r2
	cmp	r1, r3
	bgt	.L3338
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r6, #1932]
	ldr	r2, .L3490
	ldr	r1, [r7, #-48]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	ite	hi
	movhi	r3, #128
	movls	r3, #160
	strh	r3, [r2, #606]	@ movhi
	uxth	r4, r0
	b	.L3340
.L3338:
	movs	r3, #128
	b	.L3475
.L3336:
	ldr	r2, [r5, #-220]
	ldrh	r3, [r2, r3, lsl #1]
	cmp	r3, #7
	bhi	.L3341
	bl	List_get_gc_head_node
	movs	r3, #128
	strh	r3, [r7, #606]	@ movhi
	uxth	r4, r0
	b	.L3340
.L3341:
	movs	r3, #64
.L3475:
	strh	r3, [r7, #606]	@ movhi
	b	.L3342
.L3333:
	movs	r3, #80
	strh	r3, [r5, #606]	@ movhi
	b	.L3342
.L3340:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L3342
.L3332:
	ldr	r1, [r6, #4060]
	ldr	r3, [r7, #-220]
	ldrh	r2, [r7, #-204]
	ldrh	r1, [r1, r4, lsl #1]
	ldrh	r3, [r3, r4, lsl #1]
	ldr	r0, .L3490+16
	str	r1, [sp]
	ldrh	r1, [r7, #604]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
	b	.L3342
.L3345:
	cmp	r4, #12
	bls	.L3346
	lsrs	r5, r5, #4
	b	.L3344
.L3346:
	cmp	r4, #8
	bls	.L3344
	lsrs	r5, r5, #2
	b	.L3344
.L3419:
	movs	r5, #1
	b	.L3344
.L3491:
	.align	2
.L3490:
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LC152
.L3485:
	ldr	r3, [r6, #1932]
	ldr	r2, [r2, #-48]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcc	.L3350
	movs	r3, #18
	b	.L3351
.L3349:
	ldrh	r3, [r7, #20]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r7, #604]	@ movhi
	b	.L3348
.L3482:
	cmp	r9, #2
	bhi	.L3422
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L3422
.L3478:
	cmp	r8, #0
	beq	.L3408
	b	.L3343
.L3329:
	cmp	r8, #0
	beq	.L3408
	b	.L3409
.L3328:
	cmp	r8, #0
	beq	.L3408
	b	.L3359
.L3331:
	cmp	r8, #0
	beq	.L3408
	b	.L3410
.L3411:
	add	sp, sp, #52
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3536
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r3]
	ldr	r4, .L3536+4
	str	r3, [sp, #4]
	str	r2, [sp, #12]
	ldr	r2, .L3536+8
	ldr	r5, [r2, #3788]
	ldr	r2, [r4, #4052]
	cmp	r2, #0
	bne	.L3494
	ldr	r1, [r4, #1924]
	cmp	r1, #0
	beq	.L3494
	ldrb	r7, [r4, #140]	@ zero_extendqisi2
	cbz	r7, .L3496
	ldrb	r7, [r5, #8]	@ zero_extendqisi2
	subs	r0, r7, #1
	rsbs	r7, r0, #0
	adcs	r7, r7, r0
.L3496:
	ldr	r0, [r4, #1928]
	mov	r2, r7
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r8, #0
	bl	FlashProgPages
	ldr	r9, .L3536+4
	ldr	r10, .L3536+16
.L3497:
	ldr	r3, [r4, #1924]
	cmp	r8, r3
	bcs	.L3515
	movs	r6, #36
	ldr	r3, [r9, #1928]
	mul	r6, r6, r8
	adds	r2, r3, r6
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	beq	.L3519
	ldr	r3, [r2, #4]
	cbz	r7, .L3532
	orr	r3, r3, #-2147483648
.L3532:
	ldr	r0, [r2, #16]
	add	r1, sp, #8
	movs	r2, #1
	str	r3, [sp, #8]
	bl	log2phys
	ldr	r3, [r4, #1928]
	add	r6, r6, r3
	ldr	r3, [r6, #12]
	ldr	r0, [r3, #12]
	adds	r2, r0, #1
	beq	.L3501
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r10, #-220]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r6, r0
	cbnz	r2, .L3502
	ldr	r0, .L3536+12
	mov	r1, r6
	bl	printk
.L3502:
	mov	r0, r6
	bl	decrement_vpc_count
.L3501:
	add	r8, r8, #1
	b	.L3497
.L3534:
	ldr	r6, .L3536+16
	movw	r5, #16386
.L3514:
	ldrh	r3, [r6, #666]
	cbz	r3, .L3515
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3514
.L3515:
	movs	r3, #0
	str	r3, [r4, #1924]
	b	.L3494
.L3519:
	ldr	r10, .L3536+4
	mov	r8, #0
	ldr	r9, .L3536+16
.L3498:
	ldr	r3, [r4, #1924]
	cmp	r8, r3
	bcs	.L3534
	movs	r6, #36
	ldr	r3, [r10, #1928]
	mov	fp, #0
	mov	r2, #-1
	mul	r6, r6, r8
	str	r2, [r3, r6]
.L3504:
	ldr	r3, [r4, #1928]
	adds	r2, r3, r6
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	bne	.L3535
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3505
	ldr	r1, [r9, #-220]
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r10, #1872]
	strb	fp, [r5, #6]
	strh	fp, [r5, #4]	@ movhi
	strh	r3, [r5, #2]	@ movhi
.L3505:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L3506
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3506:
	ldr	r3, [r9, #188]
	adds	r3, r3, #1
	str	r3, [r9, #188]
	ldr	r3, [r4, #1928]
	add	r3, r3, r6
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #1928]
	movs	r1, #1
	mov	r2, r7
	add	r3, r3, r6
	str	r0, [sp, #8]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #1928]
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	itt	eq
	moveq	r3, #1
	streq	r3, [r10, #4052]
	ldr	r3, [r4, #4052]
	cmp	r3, #0
	beq	.L3504
	b	.L3494
.L3535:
	ldr	r3, [r2, #4]
	cbz	r7, .L3533
	orr	r3, r3, #-2147483648
.L3533:
	ldr	r0, [r2, #16]
	add	r1, sp, #8
	movs	r2, #1
	str	r3, [sp, #8]
	bl	log2phys
	ldr	r3, [r4, #1928]
	add	r6, r6, r3
	ldr	r3, [r6, #12]
	ldr	r0, [r3, #12]
	adds	r3, r0, #1
	beq	.L3511
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r9, #-220]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r6, r0
	cbnz	r2, .L3512
	ldr	r0, .L3536+12
	mov	r1, r6
	bl	printk
.L3512:
	mov	r0, r6
	bl	decrement_vpc_count
.L3511:
	add	r8, r8, #1
	b	.L3498
.L3494:
	ldr	r3, [sp, #4]
	movs	r0, #0
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3517
	bl	__stack_chk_fail
.L3517:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3537:
	.align	2
.L3536:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR5
	.word	.LC153
	.word	.LANCHOR2
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
	ldr	r3, .L3543
	ldr	r3, [r3, #4052]
	cbnz	r3, .L3539
	ldr	r3, .L3543+4
	ldr	r3, [r3, #504]
	adds	r3, r3, #1
	beq	.L3539
	bl	FtlCacheWriteBack
	bl	l2p_flush
	movs	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3539:
	movs	r0, #0
	pop	{r3, pc}
.L3544:
	.align	2
.L3543:
	.word	.LANCHOR0
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
	ldr	r3, .L3547
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3546
	bl	FtlSysFlush
.L3546:
	movs	r0, #0
	pop	{r3, pc}
.L3548:
	.align	2
.L3547:
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
	ldr	r0, .L3551
	bl	printk
	pop	{r3, lr}
	b	ftl_deinit
.L3552:
	.align	2
.L3551:
	.word	.LC154
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	adds	r2, r0, r1
	ldr	r6, .L3574
	mov	r8, r0
	ldr	r0, .L3574+4
	.pad #20
	sub	sp, sp, #20
	mov	r5, r1
	ldr	r3, [r6]
	mov	r7, r0
	str	r3, [sp, #12]
	ldr	r3, [r0, #1912]
	cmp	r2, r3
	bhi	.L3563
	ldr	r3, .L3574+8
	ldr	r0, [r3, #504]
	adds	r3, r0, #1
	beq	.L3556
	cmp	r1, #31
	bls	.L3566
	ldr	r3, [r7, #4052]
	cmp	r3, #0
	bne	.L3566
	bl	FtlCacheWriteBack
	ldrh	r4, [r7, #1878]
	mov	r0, r8
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r9, r0
	rsb	r3, r3, r8
	uxth	r3, r3
	cbz	r3, .L3557
	subs	r4, r4, r3
	add	r9, r0, #1
	cmp	r4, r5
	it	cs
	movcs	r4, r5
	uxth	r4, r4
	subs	r5, r5, r4
.L3557:
	ldr	r4, .L3574+12
	mov	r3, #-1
	ldr	r8, .L3574+16
	str	r3, [sp, #8]
.L3558:
	ldrh	r3, [r7, #1878]
	cmp	r5, r3
	bcc	.L3573
	mov	r0, r9
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L3559
	ldr	r3, [r4, #3792]
	add	r1, sp, #8
	movs	r2, #1
	mov	r0, r9
	adds	r3, r3, #1
	str	r3, [r4, #3792]
	ldr	r3, [r8, #-36]
	adds	r3, r3, #1
	str	r3, [r8, #-36]
	bl	log2phys
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3559:
	ldrh	r3, [r7, #1878]
	add	r9, r9, #1
	subs	r5, r5, r3
	b	.L3558
.L3573:
	ldr	r3, .L3574+12
	ldr	r2, [r3, #3792]
	cmp	r2, #32
	bls	.L3566
	movs	r2, #0
	str	r2, [r3, #3792]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3566
.L3563:
	mov	r0, #-1
	b	.L3556
.L3566:
	movs	r0, #0
.L3556:
	ldr	r2, [sp, #12]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L3562
	bl	__stack_chk_fail
.L3562:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3575:
	.align	2
.L3574:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR5
	.word	.LANCHOR2
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
	ldr	r3, .L3621
	.pad #84
	sub	sp, sp, #84
	mov	r7, r1
	str	r2, [sp, #44]
	ldr	r3, [r3]
	str	r3, [sp, #76]
	ldr	r3, .L3621+4
	ldr	r3, [r3, #504]
	adds	r4, r3, #1
	beq	.L3601
	cmp	r0, #16
	bne	.L3579
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r8
	bl	FtlVendorPartRead
	b	.L3578
.L3579:
	ldr	r3, [sp, #44]
	adds	r3, r1, r3
	str	r3, [sp, #36]
	ldr	r3, .L3621+8
	ldr	r1, [sp, #36]
	ldr	r2, [r3, #1912]
	cmp	r1, r2
	bhi	.L3602
	ldrh	r4, [r3, #1878]
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
	ldr	r3, .L3621+12
	ldr	r0, [sp, #28]
	ldr	r2, [r3, #-12]
	add	r2, r2, r1
	ldr	r1, [sp, #24]
	str	r2, [r3, #-12]
	ldr	r2, [r3, #-40]
	add	r2, r2, r1
	ldr	r1, [sp, #32]
	str	r2, [r3, #-40]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3580
	bl	FtlCacheWriteBack
.L3580:
	mov	r10, #0
	ldr	r5, [sp, #28]
	ldr	r4, .L3621+12
	ldr	r9, .L3621+8
	mov	r6, r10
	str	r10, [sp, #48]
	str	r10, [sp, #52]
.L3581:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3620
	add	r1, sp, #72
	mov	r0, r5
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp, #72]
	adds	r1, r3, #1
	bne	.L3617
	mov	fp, #0
.L3582:
	ldrh	r0, [r9, #1878]
	cmp	fp, r0
	bcs	.L3586
	mla	r0, r0, r5, fp
	cmp	r0, r7
	bcc	.L3584
	ldr	r3, [sp, #36]
	cmp	r0, r3
	bcs	.L3584
	subs	r0, r0, r7
	movs	r1, #0
	mov	r2, #512
	add	r0, r8, r0, lsl #9
	bl	ftl_memset
.L3584:
	add	fp, fp, #1
	b	.L3582
.L3617:
	ldr	r2, [r4, #1764]
	mov	fp, #36
	mla	fp, fp, r6, r2
	str	r3, [fp, #4]
	ldr	r3, [sp, #28]
	cmp	r5, r3
	bne	.L3587
	ldrh	ip, [r9, #1878]
	mov	r0, r7
	ldr	r3, [r4, #1792]
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
	bne	.L3588
	str	r8, [fp, #8]
	b	.L3588
.L3587:
	ldr	r3, [sp, #32]
	cmp	r5, r3
	bne	.L3589
	ldr	r3, [r4, #1796]
	ldrh	r2, [r9, #1878]
	ldr	r1, [sp, #36]
	str	r3, [fp, #8]
	mul	r3, r2, r5
	rsb	r10, r3, r1
	cmp	r10, r2
	bne	.L3588
	b	.L3618
.L3589:
	ldrh	r3, [r9, #1878]
	muls	r3, r5, r3
.L3618:
	subs	r3, r3, r7
	add	r3, r8, r3, lsl #9
	str	r3, [fp, #8]
.L3588:
	ldrh	r3, [r9, #1884]
	ldr	r2, [r4, #1804]
	str	r5, [fp, #16]
	muls	r3, r6, r3
	adds	r6, r6, #1
	bic	r3, r3, #3
	add	r3, r3, r2
	str	r3, [fp, #12]
.L3586:
	ldr	r3, [sp, #24]
	adds	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #24]
	beq	.L3590
	ldrh	r3, [r9, #1804]
	cmp	r6, r3, lsl #3
	bne	.L3581
.L3590:
	cmp	r6, #0
	beq	.L3581
	ldr	r0, [r4, #1764]
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
.L3597:
	ldr	r3, [sp, #40]
	mov	ip, #36
	ldr	r1, [sp, #28]
	mul	fp, ip, r3
	ldr	r3, [r4, #1764]
	add	r3, r3, fp
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L3592
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #1792]
	cmp	r1, r3
	bne	.L3593
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3619
.L3592:
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L3593
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #1796]
	cmp	r1, r3
	bne	.L3593
	ldrh	r0, [r9, #1878]
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #68]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r8, r0, lsl #9
.L3619:
	bl	ftl_memcpy
.L3593:
	ldr	r2, [r4, #1764]
	add	r3, r2, fp
	ldr	r1, [r2, fp]
	adds	r2, r1, #1
	itttt	eq
	streq	r1, [sp, #52]
	ldreq	r2, [r4, #164]
	addeq	r2, r2, #1
	streq	r2, [r4, #164]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L3595
	ldr	r2, [r4, #164]
	ldr	r0, .L3621+16
	adds	r2, r2, #1
	str	r2, [r4, #164]
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
.L3595:
	ldr	r3, [r4, #1764]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmp	r3, #256
	bne	.L3596
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3596:
	ldr	r3, [sp, #40]
	adds	r3, r3, #1
	str	r3, [sp, #40]
	cmp	r3, r6
	bne	.L3597
	movs	r6, #0
	b	.L3581
.L3620:
	ldr	r3, .L3621+12
	ldrh	r3, [r3, #666]
	cbz	r3, .L3599
	ldr	r0, [sp, #24]
	movs	r1, #1
	bl	ftl_do_gc
.L3599:
	ldr	r0, [sp, #52]
	b	.L3578
.L3601:
	mov	r0, r3
	b	.L3578
.L3602:
	mov	r0, #-1
.L3578:
	ldr	r3, .L3621
	ldr	r2, [sp, #76]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3600
	bl	__stack_chk_fail
.L3600:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3622:
	.align	2
.L3621:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC143
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
	ldr	r3, .L3627
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3626
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
	b	.L3625
.L3626:
	mov	r4, #-1
.L3625:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3628:
	.align	2
.L3627:
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
	ldr	r2, .L3648
	ldr	r6, .L3648+4
	ldr	r5, .L3648+8
	ldr	r1, .L3648+12
	ldr	r0, .L3648+16
	str	r3, [r5, #504]
	movs	r3, #0
	str	r3, [r2, #3796]
	str	r3, [r6, #4052]
	bl	printk
	add	r0, r6, #112
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r6, #1808]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3633
	ldr	r0, .L3648+20
	b	.L3647
.L3633:
	bl	FtlSysBlkInit
	mov	r4, r0
	cbz	r0, .L3635
	ldr	r0, .L3648+24
.L3647:
	ldr	r1, .L3648+28
	bl	printk
	b	.L3634
.L3635:
	movs	r1, #1
	str	r1, [r5, #504]
	ldr	r5, .L3648+32
	bl	ftl_do_gc
	ldrh	r7, [r5, #-204]
	cmp	r7, #15
	bhi	.L3636
	mov	r8, r5
	movw	r6, #65535
.L3639:
	ldrh	r3, [r5, #40]
	cmp	r3, r6
	bne	.L3637
	ldrh	r3, [r8, #658]
	cmp	r3, r6
	bne	.L3637
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3637:
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	movs	r0, #0
	movs	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r5, #-204]
	adds	r3, r7, #2
	cmp	r2, r3
	bhi	.L3634
	adds	r4, r4, #1
	cmp	r4, #1024
	bne	.L3639
	b	.L3634
.L3636:
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cbz	r3, .L3634
	movs	r4, #128
.L3641:
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3641
.L3634:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3649:
	.align	2
.L3648:
	.word	.LANCHOR5
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC77
	.word	.LC76
	.word	.LC155
	.word	.LC156
	.word	.LANCHOR3+108
	.word	.LANCHOR2
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
	ldr	r5, .L3654
	ldr	r4, .L3654+4
	movs	r6, #0
	addw	r1, r5, #3804
	str	r6, [r5, #3804]
	str	r6, [r4, #3984]
	str	r0, [r5, #3800]
	add	r0, r4, #3984
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #3984]
	cbz	r3, .L3653
	bl	rk_nandc_irq_init
	mov	r1, r6
	mov	r2, r6
	mov	r3, #2048
	ldr	r0, [r5, #3800]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #3984]
	bl	FlashInit
	mov	r4, r0
	cbnz	r0, .L3652
	bl	FtlInit
.L3652:
	mov	r1, r4
	ldr	r0, .L3654+8
	bl	printk
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3653:
	mov	r0, #-1
	pop	{r4, r5, r6, pc}
.L3655:
	.align	2
.L3654:
	.word	.LANCHOR5
	.word	.LANCHOR2
	.word	.LC157
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	1
	.global	ftl_fix_nand_power_lost_error
	.thumb
	.thumb_func
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3672
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r2, [r3]
	str	r3, [sp, #4]
	str	r2, [sp, #60]
	ldr	r2, .L3672+4
	ldrb	r1, [r2, #140]	@ zero_extendqisi2
	mov	r7, r2
	cmp	r1, #0
	beq	.L3656
	ldr	r4, .L3672+8
	movw	r6, #4097
	ldr	r8, .L3672+20
	ldr	r0, .L3672+12
	ldr	r3, [r4, #-220]
	ldrh	r5, [r8, #3644]
	mov	r1, r5
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldrh	r0, [r4, #-200]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4, #-152]
	bl	FtlGcRefreshOpenBlock
	sub	r0, r4, #200
	bl	allocate_new_data_superblock
	sub	r0, r4, #152
	bl	allocate_new_data_superblock
	lsl	r9, r5, #1
.L3658:
	subs	r6, r6, #1
	beq	.L3662
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-220]
	ldrh	r3, [r3, r9]
	cmp	r3, #0
	bne	.L3658
.L3662:
	ldr	r3, [r4, #-220]
	mov	r1, r5
	ldr	r0, .L3672+12
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldr	r3, [r4, #-220]
	ldrh	r6, [r3, r5, lsl #1]
	cbnz	r6, .L3660
	add	r0, sp, #64
	mov	fp, #36
	movw	r10, #65535
	strh	r5, [r0, #-52]!	@ movhi
	bl	make_superblock
	ldrh	ip, [r7, #1804]
	ldr	r9, [r7, #4056]
	mov	r3, r6
	mov	lr, r3
	add	r0, sp, #26
.L3663:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3671
	ldrh	r2, [r0, #2]!
	cmp	r2, r10
	beq	.L3664
	mla	r1, fp, r6, r9
	adds	r6, r6, #1
	lsls	r2, r2, #10
	uxth	r6, r6
	str	r2, [r1, #4]
	str	lr, [r1, #8]
	str	lr, [r1, #12]
.L3664:
	adds	r3, r3, #1
	b	.L3663
.L3671:
	ldr	r3, [r4, #-220]
	mov	r1, r5
	ldr	r0, .L3672+16
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	movs	r1, #0
	mov	r2, r6
	ldr	r0, [r7, #4056]
	bl	FlashEraseBlocks
	ldr	r0, [r7, #4056]
	movs	r1, #1
	mov	r2, r6
	bl	FlashEraseBlocks
.L3660:
	movw	r3, #65535
	strh	r3, [r8, #3644]	@ movhi
.L3656:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3666
	bl	__stack_chk_fail
.L3666:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3673:
	.align	2
.L3672:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC158
	.word	.LC159
	.word	.LANCHOR5
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
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #100
	sub	sp, sp, #100
	ldr	r7, .L3749
	mov	r8, r1
	str	r3, [sp, #12]
	ldr	r3, .L3749+4
	str	r2, [sp, #8]
	ldr	r3, [r3]
	str	r3, [sp, #92]
	ldr	r3, [r7, #4052]
	cmp	r3, #0
	bne	.L3718
	ldr	r2, .L3749+8
	ldr	r2, [r2, #504]
	adds	r2, r2, #1
	beq	.L3719
	cmp	r0, #16
	bne	.L3677
	add	r0, r1, #256
	ldr	r2, [sp, #12]
	ldr	r1, [sp, #8]
	bl	FtlVendorPartWrite
	b	.L3676
.L3677:
	ldr	r3, [sp, #8]
	adds	r6, r1, r3
	ldr	r3, [r7, #1912]
	cmp	r6, r3
	bhi	.L3720
	ldrh	r4, [r7, #1878]
	mov	r3, #2048
	ldr	r5, .L3749+12
	mov	r0, r1
	mov	r1, r4
	str	r3, [r5, #3808]
	bl	__aeabi_uidiv
	mov	r1, r4
	mov	fp, r0
	subs	r0, r6, #1
	bl	__aeabi_uidiv
	rsb	r6, fp, r0
	str	r0, [sp, #20]
	adds	r3, r6, #1
	str	r3, [sp, #4]
	ldr	r3, .L3749+16
	ldr	r1, [sp, #4]
	ldr	r2, [r3, #-32]
	add	r2, r2, r1
	ldr	r1, [sp, #8]
	str	r2, [r3, #-32]
	ldr	r2, [r3, #-16]
	cmp	r1, r4, lsl #1
	add	r2, r2, r1
	str	r2, [r3, #-16]
	mov	r2, r1
	ldr	r1, [r7, #1924]
	ite	cs
	movcs	r2, #1
	movcc	r2, #0
	str	r2, [sp, #28]
	cmp	r1, #0
	beq	.L3679
	movs	r2, #36
	muls	r2, r1, r2
	ldr	r1, [r7, #1928]
	subs	r2, r2, #36
	add	r9, r1, r2
	ldr	r2, [r9, #16]
	cmp	fp, r2
	bne	.L3680
	ldr	r2, [r3, #-28]
	mov	r1, r4
	mov	r0, r8
	adds	r2, r2, #1
	str	r2, [r3, #-28]
	ldr	r3, [r5, #3812]
	adds	r3, r3, #1
	str	r3, [r5, #3812]
	bl	__aeabi_uidivmod
	ldr	r0, [r9, #8]
	ldr	r3, [sp, #8]
	subs	r4, r4, r1
	add	r0, r0, r1, lsl #9
	ldr	r1, [sp, #12]
	cmp	r4, r3
	it	cs
	movcs	r4, r3
	lsls	r7, r4, #9
	mov	r2, r7
	bl	ftl_memcpy
	cbnz	r6, .L3681
	ldr	r3, [r5, #3812]
	cmp	r3, #2
	ble	.L3718
.L3681:
	ldr	r3, [sp, #8]
	add	r8, r8, r4
	add	fp, fp, #1
	str	r6, [sp, #4]
	subs	r3, r3, r4
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	add	r3, r3, r7
	str	r3, [sp, #12]
.L3680:
	movs	r3, #0
	str	r3, [r5, #3812]
.L3679:
	mov	r0, fp
	ldr	r1, [sp, #20]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3682
	bl	FtlCacheWriteBack
.L3682:
	ldr	r4, .L3749
	mov	r6, fp
	ldr	r7, .L3749+20
	mov	r10, r4
	str	r7, [r5, #3788]
.L3683:
	ldr	r3, [sp, #4]
	ldr	r5, .L3749+16
	cmp	r3, #0
	beq	.L3748
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3684
	sub	r2, r5, #200
	ldr	r9, .L3749+8
	cmp	r7, r2
	bne	.L3685
	ldrh	r7, [r5, #-148]
	cbnz	r7, .L3686
	sub	r0, r5, #152
	bl	allocate_new_data_superblock
	str	r7, [r9, #3292]
.L3686:
	ldr	r0, .L3749+20
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3292]
	cbnz	r3, .L3721
.L3688:
	ldr	r7, .L3749+20
	b	.L3687
.L3685:
	str	r3, [r9, #3292]
	ldrh	r3, [r5, #-196]
	cmp	r3, #0
	bne	.L3688
	mov	r0, r7
	bl	allocate_new_data_superblock
	b	.L3687
.L3721:
	ldr	r7, .L3749+24
.L3687:
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3689
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3689:
	ldr	r3, .L3749+12
	str	r7, [r3, #3788]
.L3684:
	ldr	r2, [r5, #1760]
	ldr	r3, [r4, #1924]
	ldrh	ip, [r7, #4]
	subs	r3, r2, r3
	ldr	r2, [sp, #4]
	cmp	ip, r3
	ldr	r9, .L3749+16
	it	cs
	movcs	ip, r3
	cmp	ip, r2
	mov	r3, ip
	it	cs
	movcs	r3, r2
	str	r3, [sp, #44]
	movs	r3, #0
	str	r3, [sp, #24]
.L3690:
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3691
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	beq	.L3691
	ldr	r3, [sp, #28]
	cbz	r3, .L3692
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3692
	ldr	r3, [sp, #24]
	cbz	r3, .L3692
	ldr	r3, [sp, #8]
	ldrh	r2, [r4, #1878]
	add	r3, r8, r3
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3691
.L3692:
	add	r1, sp, #52
	movs	r2, #0
	mov	r0, r6
	movs	r5, #36
	bl	log2phys
	mov	r0, r7
	bl	get_new_active_ppa
	ldr	r3, [r4, #1924]
	ldr	r1, [r4, #1928]
	ldrh	r2, [r4, #1884]
	mla	r1, r5, r3, r1
	mul	ip, r3, r2
	str	r6, [r1, #16]
	str	r0, [r1, #4]
	lsr	r0, ip, #2
	str	r0, [sp, #36]
	ldr	r0, [r9, #1808]
	mov	lr, r0
	str	r0, [sp, #40]
	ldr	r0, [sp, #36]
	add	r0, lr, r0, lsl #2
	ldrh	lr, [r4, #1882]
	str	r0, [sp, #16]
	str	r0, [r1, #12]
	mul	r3, r3, lr
	ldr	r0, [r9, #1788]
	bic	r3, r3, #3
	add	r3, r3, r0
	ldr	r0, [sp, #16]
	str	r3, [r1, #8]
	movs	r1, #0
	bl	ftl_memset
	cmp	r6, fp
	beq	.L3693
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3745
	ldr	r3, [sp, #8]
	add	r5, r8, r3
	ldrh	r3, [r4, #1878]
	smulbb	r3, r3, r6
	subs	r5, r5, r3
	movs	r3, #0
	str	r3, [sp, #32]
	uxth	r5, r5
	b	.L3696
.L3693:
	ldrh	r5, [r4, #1878]
	mov	r0, r8
	mov	r1, r5
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	subs	r5, r5, r1
	str	r1, [sp, #32]
	cmp	r5, r3
	it	cs
	movcs	r5, r3
.L3696:
	ldrh	r3, [r4, #1878]
	cmp	r5, r3
	bne	.L3697
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
	cbz	r3, .L3699
	ldr	r2, [r10, #1924]
	ldr	r5, [r10, #1928]
	mla	r2, lr, r2, r5
	str	r1, [r2, #8]
	b	.L3700
.L3699:
	ldr	r0, [r10, #1928]
	ldr	r3, [r10, #1924]
	ldrh	r2, [r10, #1882]
	mla	r3, lr, r3, r0
	ldr	r0, [r3, #8]
	b	.L3746
.L3750:
	.align	2
.L3749:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR5
	.word	.LANCHOR2
	.word	.LANCHOR2-200
	.word	.LANCHOR2-152
.L3697:
	ldr	r2, [sp, #52]
	movs	r3, #36
	adds	r1, r2, #1
	beq	.L3701
	ldr	r1, [r4, #1928]
	add	r0, sp, #56
	str	r2, [sp, #60]
	ldr	r2, [r4, #1924]
	str	r6, [sp, #72]
	mla	r3, r3, r2, r1
	movs	r1, #1
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #64]
	movs	r2, #0
	str	r3, [sp, #68]
	bl	FlashReadPages
	ldr	r3, [sp, #56]
	adds	r3, r3, #1
	bne	.L3702
	ldr	r3, [r9, #164]
	adds	r3, r3, #1
	str	r3, [r9, #164]
	b	.L3704
.L3702:
	ldr	r3, [sp, #16]
	ldr	r3, [r3, #8]
	cmp	r3, r6
	beq	.L3704
	ldr	r3, [r9, #164]
	mov	r2, r6
	ldr	r0, .L3751
	adds	r3, r3, #1
	str	r3, [r9, #164]
	ldr	r3, [sp, #16]
	ldr	r1, [r3, #8]
	bl	printk
	b	.L3704
.L3701:
	ldr	r1, [r4, #1928]
	ldr	r2, [r4, #1924]
	mla	r2, r3, r2, r1
	movs	r1, #0
	ldr	r0, [r2, #8]
	ldrh	r2, [r4, #1882]
	bl	ftl_memset
.L3704:
	cmp	r6, fp
	mov	lr, #36
	lsl	r2, r5, #9
	bne	.L3705
	ldr	r1, [r4, #1924]
	ldr	r5, [r4, #1928]
	ldr	r3, [sp, #32]
	mla	r1, lr, r1, r5
	ldr	r0, [r1, #8]
	ldr	r1, [sp, #12]
	add	r0, r0, r3, lsl #9
	b	.L3746
.L3705:
	ldr	r0, [r4, #1928]
	ldr	r3, [r4, #1924]
	ldrh	r1, [r4, #1878]
	mla	r3, lr, r3, r0
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #12]
	add	r1, r3, r1, lsl #9
	b	.L3746
.L3745:
	ldr	r3, [sp, #28]
	cbz	r3, .L3706
	ldr	r3, [r4, #1924]
	ldr	r2, [r4, #1928]
	mla	r5, r5, r3, r2
	ldrh	r3, [r4, #1878]
	ldr	r2, [sp, #12]
	muls	r3, r6, r3
	rsb	r3, r8, r3
	add	r3, r2, r3, lsl #9
	str	r3, [r5, #8]
	b	.L3700
.L3706:
	ldr	r2, [r4, #1928]
	ldr	r3, [r4, #1924]
	ldrh	r1, [r4, #1878]
	mla	r3, r5, r3, r2
	ldrh	r2, [r4, #1882]
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #12]
	add	r1, r3, r1, lsl #9
.L3746:
	bl	ftl_memcpy
.L3700:
	ldr	r2, [sp, #40]
	movw	r3, #61589
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1, lsl #2]	@ movhi
	ldr	r2, [sp, #16]
	ldr	r3, [r9, #-4]
	str	r3, [r2, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	ldr	r2, [sp, #16]
	it	eq
	moveq	r3, #0
	str	r3, [r9, #-4]
	ldr	r3, [sp, #16]
	str	r6, [r3, #8]
	adds	r6, r6, #1
	ldr	r3, [sp, #52]
	str	r3, [r2, #12]
	ldrh	r3, [r7]
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, [r4, #1924]
	adds	r3, r3, #1
	str	r3, [r4, #1924]
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp, #24]
	b	.L3690
.L3691:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #24]
	subs	r3, r3, r2
	str	r3, [sp, #4]
	ldr	r3, .L3751+4
	ldr	r2, [r10, #1924]
	ldr	r3, [r3, #1760]
	cmp	r2, r3
	bcs	.L3710
	ldr	r3, [sp, #28]
	cbnz	r3, .L3710
	ldrh	r3, [r7, #4]
	cbz	r3, .L3710
.L3712:
	movs	r3, #0
	str	r3, [sp, #28]
	b	.L3683
.L3710:
	bl	FtlCacheWriteBack
	movs	r3, #0
	str	r3, [r10, #1924]
	ldr	r3, [sp, #4]
	cmp	r3, #1
	bhi	.L3683
	b	.L3712
.L3748:
	mov	r0, r3
	ldr	r3, [sp, #20]
	rsb	r1, fp, r3
	bl	ftl_do_gc
	ldrh	r3, [r5, #-204]
	cmp	r3, #5
	bls	.L3724
	cmp	r3, #31
	bhi	.L3718
	ldr	r3, .L3751+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cbnz	r3, .L3718
.L3724:
	ldr	r4, [sp, #4]
	movw	r7, #65535
	ldr	r6, .L3751+4
	ldr	r8, .L3751+8
.L3737:
	ldrh	r3, [r5, #40]
	cmp	r3, r7
	bne	.L3716
	ldrh	r3, [r6, #658]
	cmp	r3, r7
	bne	.L3716
	ldrh	r3, [r6, #660]
	cmp	r3, r7
	bne	.L3716
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3716:
	movs	r0, #1
	movs	r3, #128
	strh	r3, [r5, #606]	@ movhi
	mov	r1, r0
	strh	r3, [r5, #604]	@ movhi
	bl	ftl_do_gc
	movs	r0, #0
	movs	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r8, #4052]
	cbnz	r3, .L3718
	ldrh	r3, [r6, #-204]
	cmp	r3, #2
	bhi	.L3718
	adds	r4, r4, #1
	cmp	r4, #256
	bne	.L3737
	b	.L3718
.L3720:
	mov	r0, #-1
	b	.L3676
.L3718:
	movs	r0, #0
	b	.L3676
.L3719:
	mov	r0, r3
.L3676:
	ldr	r3, .L3751+12
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3717
	bl	__stack_chk_fail
.L3717:
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3752:
	.align	2
.L3751:
	.word	.LC160
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	__stack_chk_guard
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
	ldr	r3, .L3757
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3756
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
	b	.L3755
.L3756:
	mov	r4, #-1
.L3755:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3758:
	.align	2
.L3757:
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
	ldr	r4, .L3769
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L3769+4
	movs	r5, #0
	mov	r6, r0
	ldr	r3, [r4, #1776]
	mov	r9, r4
	str	r3, [r4, #4060]
	ldr	r3, [r4, #1800]
	str	r3, [r4, #4064]
.L3762:
	ldrh	r2, [r7, #1874]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L3768
	movs	r1, #1
	ldr	r0, .L3769+8
	orr	r3, r3, r8
	str	r3, [r4, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #4064]
	ldr	r0, .L3769+12
	mov	r1, r6
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #4052]
	str	r3, [sp, #12]
	ldr	r3, [r4, #4060]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #4056]
	bl	printk
	ldr	r3, [r4, #4064]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L3763
	ldr	r0, .L3769+16
	movs	r2, #4
	ldr	r1, [r9, #1776]
	mov	r3, #768
	bl	rknand_print_hex
.L3763:
	adds	r5, r5, #1
	b	.L3762
.L3768:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3770:
	.align	2
.L3769:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+4052
	.word	.LC161
	.word	.LC162
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
	ldr	r5, .L3789
	add	fp, r5, #1832
	ldrh	r7, [r5, #1812]
	mov	r9, r5
.L3772:
	ldrh	r3, [r5, #1814]
	ldr	r4, .L3789+4
	cmp	r3, r7
	bls	.L3786
	ldr	r1, [r4, #632]
	mov	r8, #0
	ldrh	ip, [r9, #1804]
	mov	r10, #36
	ldr	r2, [r4, #1764]
	mov	r6, r8
	str	r1, [sp, #24]
	ldr	r3, [r4, #628]
	ldrh	r1, [r9, #1884]
	str	r1, [sp, #28]
.L3782:
	uxth	r1, r8
	cmp	r1, ip
	bcs	.L3787
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
	cbnz	r0, .L3773
	mla	r0, r10, r6, r2
	lsls	r1, r1, #10
	str	r1, [r0, #4]
	ldr	r1, [sp, #28]
	muls	r1, r6, r1
	add	r6, r6, #1
	it	mi
	addmi	r1, r1, #3
	bic	lr, r1, #3
	ldr	r1, [sp, #24]
	uxth	r6, r6
	str	r3, [r0, #8]
	add	r1, r1, lr
	str	r1, [r0, #12]
.L3773:
	add	r8, r8, #1
	b	.L3782
.L3787:
	cbz	r6, .L3777
	ldr	r0, [r4, #1764]
	mov	r1, r6
	movs	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r10, #36
.L3778:
	uxth	r3, r8
	cmp	r3, r6
	bcs	.L3777
	ldr	r3, [r4, #1764]
	ldr	r0, .L3789+8
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
	b	.L3778
.L3777:
	adds	r7, r7, #1
	uxth	r7, r7
	b	.L3772
.L3786:
	mov	r8, #0
.L3781:
	ldrh	r3, [r4, #1912]
	sxth	r6, r8
	ldr	r7, .L3789+4
	cmp	r6, r3
	bge	.L3784
	lsls	r6, r6, #1
	movs	r7, #0
.L3785:
	ldrh	r2, [r5, #1874]
	sxth	r3, r7
	adds	r7, r7, #1
	cmp	r3, r2
	bge	.L3788
	ldr	r2, [r4, #1828]
	movs	r1, #1
	ldr	r0, .L3789+12
	ldrh	r2, [r2, r6]
	orr	r3, r3, r2, lsl #10
	mov	r2, r1
	str	r3, [r4, #4056]
	bl	FlashReadPages
	ldr	r3, [r4, #4064]
	ldr	r1, [r4, #1828]
	ldr	r2, [r4, #4060]
	ldr	r0, [r3]
	ldrh	r1, [r1, r6]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L3789+16
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	ldr	r2, [r4, #4052]
	str	r3, [sp, #20]
	ldr	r3, [r4, #4056]
	bl	printk
	b	.L3785
.L3788:
	add	r8, r8, #1
	b	.L3781
.L3784:
	ldr	r1, [r7, #1828]
	movs	r2, #2
	ldr	r3, [r5, #1900]
	ldr	r0, .L3789+20
	bl	rknand_print_hex
	ldr	r1, [r7, #1848]
	ldrh	r3, [r5, #1908]
	movs	r2, #4
	ldr	r0, .L3789+24
	bl	rknand_print_hex
	ldr	r0, .L3789+28
	ldr	r1, [r7, #1852]
	movs	r2, #4
	ldrh	r3, [r5, #1908]
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3790:
	.align	2
.L3789:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC163
	.word	.LANCHOR2+4052
	.word	.LC110
	.word	.LC164
	.word	.LC165
	.word	.LC166
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
	ldr	r3, .L3793
	ldr	r2, [r3, #1744]
	ldr	r3, .L3793+4
	cmp	r2, r3
	bne	.L3791
	b	flash_enter_slc_mode
.L3791:
	bx	lr
.L3794:
	.align	2
.L3793:
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
	ldr	r3, .L3797
	ldr	r2, [r3, #1744]
	ldr	r3, .L3797+4
	cmp	r2, r3
	bne	.L3795
	b	flash_exit_slc_mode
.L3795:
	bx	lr
.L3798:
	.align	2
.L3797:
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
	ldr	r4, .L3807
	mov	r8, r1
	.pad #20
	sub	sp, sp, #20
	mov	r1, r6
	mov	fp, r2
	ldr	r0, .L3807+4
	ldr	r3, [r4, #692]
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
.L3800:
	cmp	r7, r8
	bcs	.L3806
	add	r2, r7, r10
	ldrb	r1, [r4, #140]	@ zero_extendqisi2
	ubfx	r2, r2, #2, #16
	rsb	r5, ip, #4
	add	r3, r4, r2, lsl #1
	uxth	r5, r5
	ldrh	r3, [r3, #144]
	cbz	r1, .L3801
	ldr	r1, .L3807
	ldr	r0, .L3807+8
	ldr	r1, [r1, #1744]
	cmp	r1, r0
	it	eq
	moveq	r3, r2
.L3801:
	ldr	r2, [sp]
	add	r7, r7, r5
	add	r2, ip, r2
	ldrb	ip, [r4, #1792]	@ zero_extendqisi2
	uxth	r7, r7
	mla	r3, r9, r3, r2
	ldr	r2, [r4, #692]
	str	ip, [sp, #4]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L3807+12
	str	r1, [sp, #8]
	ldrb	r0, [r3, #4014]	@ zero_extendqisi2
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
	b	.L3800
.L3806:
	mov	r1, r6
	mov	r2, r8
	movs	r3, #0
	ldr	r0, .L3807+16
	bl	printk
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3808:
	.align	2
.L3807:
	.word	.LANCHOR0
	.word	.LC167
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC168
	.fnend
	.size	IdBlockReadData, .-IdBlockReadData
	.align	1
	.global	write_idblock
	.thumb
	.thumb_func
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 120
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3852
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #132
	sub	sp, sp, #132
	ldr	r3, [r3]
	mov	r4, r0
	ldr	r7, .L3852+4
	mov	r0, #256000
	mov	r6, r1
	mov	r5, r2
	str	r3, [sp, #124]
	ldr	r3, [r7, #692]
	ldr	r8, [r7, #4]
	ldrb	r10, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	str	r0, [sp, #8]
	cmp	r0, #0
	beq	.L3834
	addw	r4, r4, #511
	lsrs	r4, r4, #9
	cmp	r4, #8
	bls	.L3832
	cmp	r4, #500
	bhi	.L3834
	b	.L3811
.L3832:
	movs	r4, #8
.L3811:
	ldr	r2, [r6]
	ldr	r3, .L3852+8
	cmp	r2, r3
	bne	.L3834
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
.L3815:
	ldr	r1, [r0, #-4]!
	cbnz	r1, .L3812
	ldr	r1, [r6, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #4096
	add	r2, r2, #-1
	it	hi
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3815
	b	.L3814
.L3812:
	ldr	r0, .L3852+12
	bl	printk
.L3814:
	mov	r1, r5
	movs	r3, #5
	ldr	r0, .L3852+16
	movs	r2, #4
	bl	rknand_print_hex
	ldrb	r2, [r7, #1]	@ zero_extendqisi2
	ldr	r1, [r6, #512]
	subs	r5, r5, #4
	ldr	r0, .L3852+20
	bl	printk
	ldr	r2, .L3852+24
	ldrh	r3, [r7, #138]
	mov	r1, r4
	ldr	r0, .L3852+28
	ldr	r2, [r2, #4008]
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r7, .L3852+4
	ldr	r2, [r6, #512]
	cmp	r2, r3
	it	hi
	strhi	r3, [r6, #512]
	lsls	r3, r4, #7
	str	r3, [sp, #32]
	movs	r3, #0
	str	r3, [sp, #16]
	str	r3, [sp, #12]
.L3829:
	ldr	r2, [r5, #4]
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r10, .L3852+4
	cmp	r2, r3
	bcs	.L3817
	ldr	r3, .L3852+24
	ldr	r3, [r3, #4008]
	cmp	r2, r3
	bcc	.L3817
	ldr	r3, [sp, #28]
	cmp	r3, #1
	bls	.L3818
	ldr	r3, [sp, #12]
	cbz	r3, .L3818
	ldr	r3, [r5]
	adds	r3, r3, #1
	cmp	r2, r3
	beq	.L3817
.L3818:
	movs	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r3, [r5, #4]
	ldr	r0, .L3852+32
	mul	r8, r3, r9
	ldr	r3, [r10, #692]
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
	bls	.L3835
	movs	r0, #0
	add	r1, r8, r10
	mov	fp, #2
	mov	r2, r0
	bl	FlashEraseBlock
	b	.L3819
.L3835:
	mov	fp, #1
.L3819:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r7, #692]
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
.L3820:
	ldr	r3, [sp, #44]
	cmp	r10, r3
	bcs	.L3851
	add	r2, r10, ip
	ubfx	r2, r2, #2, #16
	cbz	r2, .L3821
	adds	r3, r2, #1
	add	r1, r7, r3, lsl #1
	ldrh	fp, [r1, #144]
	ldrb	r1, [r7, #140]	@ zero_extendqisi2
	cbz	r1, .L3822
	ldr	r1, [r7, #1744]
	ldr	r0, .L3852+36
	cmp	r1, r0
	it	eq
	moveq	fp, r3
.L3822:
	add	fp, fp, #1073741824
	add	fp, fp, #-1
	lsl	r3, fp, #2
	str	r3, [sp, #60]
.L3821:
	movw	r3, #61424
	str	r3, [sp, #64]
	add	r3, r7, r2, lsl #1
	ldrh	fp, [r3, #144]
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cbz	r3, .L3823
	ldr	r3, [r7, #1744]
	ldr	r1, .L3852+36
	cmp	r3, r1
	it	eq
	moveq	fp, r2
.L3823:
	ldr	r3, [sp, #20]
	add	r10, r10, #4
	ldr	r2, [sp, #36]
	str	ip, [sp, #52]
	uxth	r10, r10
	mla	r2, r3, fp, r2
	ldr	r3, .L3852+4
	ldrb	r3, [r3, #1792]	@ zero_extendqisi2
	str	r2, [sp, #48]
	str	r3, [sp, #40]
	ldr	r3, .L3852+24
	ldrb	r0, [r3, #4014]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, .L3852+4
	ldr	r3, [r3, #692]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #48]
	mov	r0, r2
	bl	__aeabi_uidiv
	add	r3, sp, #60
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
	b	.L3820
.L3853:
	.align	2
.L3852:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC169
	.word	.LC170
	.word	.LC171
	.word	.LANCHOR2
	.word	.LC172
	.word	.LC173
	.word	1446522928
.L3851:
	mov	r1, r8
	movs	r3, #0
	mov	r2, r4
	ldr	r0, .L3854
	bl	printk
	ldr	r0, [r5, #4]
	mov	r1, r4
	mov	r8, #0
	mul	r0, r0, r9
	ldr	r2, [sp, #8]
	bl	IdBlockReadData
	mov	r3, r6
	ldr	r0, [sp, #8]
.L3828:
	mov	fp, r0
	mov	r10, r3
	ldr	r1, [fp]
	adds	r0, r0, #4
	ldr	r2, [r10]
	adds	r3, r3, #4
	cmp	r1, r2
	beq	.L3825
	movs	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r3, [fp]
	ldr	r0, .L3854+4
	str	r3, [sp]
	ldr	r3, [r10]
	bic	r10, r8, #255
	ldr	r1, [sp, #12]
	lsl	r10, r10, #2
	str	r3, [sp, #4]
	mov	r3, r8
	ldr	r2, [r5, #4]
	bl	printk
	ldr	r0, .L3854+8
	add	r1, r6, r10
	movs	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	movs	r2, #4
	ldr	r0, .L3854+12
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
	bls	.L3826
	ldr	r1, [r5, #4]
	movs	r0, #0
	mov	r2, r0
	mla	r1, r1, r9, r9
	bl	FlashEraseBlock
.L3826:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L3854+16
	ldr	r1, [r5, #4]
	bl	printk
	ldr	r3, [sp, #32]
	cmp	r8, r3
	bcc	.L3817
	b	.L3827
.L3825:
	ldr	r2, [sp, #32]
	add	r8, r8, #1
	cmp	r8, r2
	bne	.L3828
.L3827:
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	str	r3, [sp, #16]
.L3817:
	ldr	r3, [sp, #12]
	adds	r5, r5, #4
	adds	r3, r3, #1
	str	r3, [sp, #12]
	cmp	r3, #5
	bne	.L3829
	ldr	r0, [sp, #8]
	bl	ftl_free
	ldr	r3, [sp, #16]
	clz	r0, r3
	lsrs	r0, r0, #5
	negs	r0, r0
	b	.L3810
.L3834:
	mov	r0, #-1
.L3810:
	ldr	r3, .L3854+20
	ldr	r2, [sp, #124]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3830
	bl	__stack_chk_fail
.L3830:
	add	sp, sp, #132
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3855:
	.align	2
.L3854:
	.word	.LC174
	.word	.LC175
	.word	.LC176
	.word	.LC177
	.word	.LC178
	.word	__stack_chk_guard
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
	ldr	r4, .L3860
	movs	r2, #0
.L3857:
	cmp	r0, r1
	beq	.L3859
	ldrb	r3, [r0], #1	@ zero_extendqisi2
	eor	r3, r3, r2, lsr #24
	add	r3, r4, r3, lsl #2
	ldr	r3, [r3, #3296]
	eor	r2, r3, r2, lsl #8
	b	.L3857
.L3859:
	mov	r0, r2
	pop	{r4, pc}
.L3861:
	.align	2
.L3860:
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
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	cmp	r0, #64
	ldr	r6, .L3885
	.pad #56
	sub	sp, sp, #56
	mov	r5, r0
	mov	r7, r1
	mov	r9, r2
	ldr	r4, .L3885+4
	ldr	r3, [r6]
	str	r3, [sp, #52]
	bne	.L3863
	ldr	r2, [r2]
	ldr	r3, .L3885+8
	cmp	r2, r3
	bne	.L3863
	mov	r0, #256000
	movs	r3, #1
	strb	r3, [r4, #3816]
	bl	ftl_malloc
	movs	r1, #0
	mov	r2, #256000
	str	r0, [r4, #3820]
	bl	ftl_memset
	str	r5, [r4, #3824]
.L3863:
	str	r7, [sp]
	mov	r3, r5
	ldr	r0, .L3885+12
	ldr	r1, [r4, #3820]
	ldr	r2, [r9]
	bl	printk
	ldrb	r3, [r4, #3816]	@ zero_extendqisi2
	ldr	r10, .L3885+4
	cmp	r3, #0
	beq	.L3862
	sub	r0, r5, #64
	ldr	r8, [r10, #3820]
	cmp	r0, #500
	bcs	.L3865
	rsb	r2, r5, #564
	add	r0, r8, r0, lsl #9
	cmp	r2, r7
	mov	r1, r9
	it	cs
	movcs	r2, r7
	lsls	r2, r2, #9
	bl	ftl_memcpy
	b	.L3866
.L3865:
	cmp	r5, #564
	bcs	.L3874
.L3866:
	ldr	r3, [r4, #3824]
	cmp	r3, r5
	beq	.L3872
	ldr	r2, .L3885+4
	movs	r3, #0
	mov	r9, r3
	strb	r3, [r2, #3816]
	cmp	r8, #0
	beq	.L3873
	mov	r0, r8
	bl	ftl_free
.L3873:
	str	r9, [r4, #3820]
.L3872:
	add	r5, r5, r7
	str	r5, [r4, #3824]
	b	.L3862
.L3874:
	ldr	r3, .L3885+16
	ldr	r0, [r10, #3824]
	ldr	r3, [r3, #692]
	subs	r0, r0, #64
	cmp	r0, #500
	it	cs
	movcs	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L3876
	movs	r3, #2
	str	r3, [sp, #12]
	movs	r3, #3
	str	r3, [sp, #16]
	movs	r3, #4
	str	r3, [sp, #20]
	movs	r3, #5
	str	r3, [sp, #24]
	movs	r3, #6
	str	r3, [sp, #28]
	b	.L3868
.L3876:
	movs	r3, #0
.L3867:
	add	r1, sp, #12
	lsls	r2, r3, #1
	cmp	r0, #256
	it	ls
	movls	r2, r3
	str	r2, [r1, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #5
	bne	.L3867
.L3868:
	movw	r3, #63872
.L3871:
	ldr	r2, [r8, r3, lsl #2]
	cbz	r2, .L3869
	adds	r3, r3, #128
	lsls	r0, r3, #2
	b	.L3870
.L3869:
	subs	r3, r3, #1
	cmp	r3, #4096
	bne	.L3871
	lsls	r0, r0, #9
.L3870:
	mov	r1, r8
	add	r2, sp, #12
	bl	write_idblock
	ldr	r0, [r4, #3820]
	mov	r8, #0
	strb	r8, [r4, #3816]
	bl	ftl_free
	str	r8, [r4, #3820]
	b	.L3872
.L3862:
	ldr	r2, [sp, #52]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L3875
	bl	__stack_chk_fail
.L3875:
	add	sp, sp, #56
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3886:
	.align	2
.L3885:
	.word	__stack_chk_guard
	.word	.LANCHOR5
	.word	-52655045
	.word	.LC179
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
	bcs	.L3888
	cbnz	r0, .L3888
	mov	r0, r1
	mov	r1, r2
	mov	r2, r5
	bl	write_loader_lba
.L3888:
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #528
	sub	sp, sp, #528
	ldr	r5, .L3998
	mov	r4, r1
	mov	r6, r2
	ldr	r3, [r5]
	str	r3, [sp, #524]
	ldr	r3, .L3998+4
	cmp	r1, r3
	beq	.L3891
	bhi	.L3892
	subw	r3, r3, #3461
	cmp	r1, r3
	beq	.L3893
	bhi	.L3894
	subs	r3, r3, #125
	cmp	r1, r3
	beq	.L3895
	bhi	.L3896
	subs	r3, r3, #237
	cmp	r1, r3
	bne	.L3953
	bl	rknand_dev_flush
	b	.L3986
.L3896:
	ldr	r3, .L3998+8
	cmp	r1, r3
	beq	.L3898
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3899
	b	.L3953
.L3894:
	ldr	r3, .L3998+12
	cmp	r1, r3
	beq	.L3900
	bhi	.L3901
	mvn	r1, #1960
	add	r3, r3, r1
	cmp	r4, r3
	beq	.L3902
	b	.L3953
.L3901:
	ldr	r3, .L3998+16
	cmp	r1, r3
	beq	.L3900
	adds	r3, r3, #10
	cmp	r1, r3
	beq	.L3900
	b	.L3953
.L3892:
	ldr	r3, .L3998+20
	cmp	r1, r3
	beq	.L3903
	bhi	.L3904
	subs	r3, r3, #78
	cmp	r1, r3
	beq	.L3905
	bcc	.L3906
	adds	r3, r3, #21
	cmp	r1, r3
	beq	.L3907
	adds	r3, r3, #56
	cmp	r1, r3
	beq	.L3908
	b	.L3953
.L3904:
	ldr	r3, .L3998+24
	cmp	r1, r3
	mov	r7, r3
	beq	.L3909
	bhi	.L3910
	mvn	r2, #956
	add	r3, r3, r2
	cmp	r1, r3
	beq	.L3911
	add	r3, r3, #956
	cmp	r1, r3
	beq	.L3912
	b	.L3953
.L3910:
	ldr	r3, .L3998+28
	cmp	r1, r3
	beq	.L3912
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3909
	b	.L3953
.L3906:
	ldr	r0, .L3998+32
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r4, r0
	cbnz	r0, .L3913
.L3918:
	mvn	r0, #11
	b	.L3890
.L3913:
	mov	r1, r6
	mov	r2, #512
	bl	rk_copy_from_user
	cbnz	r0, .L3992
	ldr	r0, .L3998+36
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r4, #4]
	cmp	r3, #8
	str	r3, [sp]
	bhi	.L3990
	bl	rknand_device_lock
	mov	r2, r4
	ldr	r0, [r4]
	ldr	r1, [sp]
	bl	IdBlockReadData
	bl	rknand_device_unlock
	mov	r0, r6
	mov	r1, r4
	ldr	r2, [sp]
	lsls	r2, r2, #9
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3985
	ldr	r0, .L3998+40
.L3988:
	bl	printk
.L3990:
	mov	r0, r4
.L3991:
	bl	ftl_free
.L3989:
	mvn	r0, #13
	b	.L3890
.L3905:
	ldr	r0, .L3998+44
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r4, r0
	cmp	r0, #0
	beq	.L3918
	mov	r1, r6
	mov	r2, #4096
	bl	rk_copy_from_user
	cbz	r0, .L3919
.L3992:
	ldr	r0, .L3998+48
	b	.L3988
.L3919:
	ldr	r6, .L3998+52
	ldr	r0, .L3998+56
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r7, [r6, #3828]
	mov	r8, r6
	cbnz	r7, .L3920
	mov	r0, #260096
	bl	ftl_malloc
	str	r0, [r6, #3828]
	cmp	r0, #0
	beq	.L3990
	mov	r1, r7
	mov	r2, #260096
	bl	memset
.L3920:
	ldr	r2, [r4, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L3990
	ldr	r3, [r4]
	cmp	r3, #251904
	bhi	.L3990
	ldr	r0, [r8, #3828]
	add	r1, r4, #8
	add	r0, r0, r3
	bl	memcpy
.L3985:
	mov	r0, r4
	bl	ftl_free
.L3986:
	movs	r4, #0
	b	.L3917
.L3908:
	ldr	r0, .L3998+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r7, r0
	cmp	r0, #0
	beq	.L3918
	mov	r1, r6
	movs	r2, #28
	bl	rk_copy_from_user
	cbz	r0, .L3923
	ldr	r0, .L3998+48
	bl	printk
	b	.L3924
.L3923:
	ldmia	r7, {r1, r2}
	ldr	r0, .L3998+64
	bl	printk
	ldr	r1, [r7]
	cmp	r1, #256000
	bhi	.L3924
	ldr	r6, .L3998+52
	ldr	r0, [r6, #3828]
	cbz	r0, .L3924
	bl	CRC_32
	ldr	r3, [r7, #4]
	cmp	r3, r0
	beq	.L3926
	mov	r0, r7
	bl	ftl_free
	b	.L3954
.L3926:
	bl	rknand_device_lock
	ldr	r1, [r6, #3828]
	add	r2, r7, #8
	ldr	r0, [r7]
	bl	write_idblock
	movs	r4, #0
	bl	rknand_device_unlock
	ldr	r0, [r6, #3828]
	bl	ftl_free
	str	r4, [r6, #3828]
	mov	r0, r7
	b	.L3930
.L3924:
	mov	r0, r7
	b	.L3991
.L3907:
	ldr	r0, .L3998+68
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r7, r0
	cmp	r0, #0
	beq	.L3918
	bl	ftl_read_flash_info
	mov	r0, r6
	mov	r1, r7
	movs	r2, #11
	b	.L3996
.L3891:
	ldr	r0, .L3998+72
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r7, r0
	cmp	r0, #0
	beq	.L3918
	movs	r1, #0
	movs	r2, #64
	bl	ftl_memset
	mov	r0, r6
	mov	r1, r7
	movs	r2, #64
	b	.L3996
.L3903:
	ldr	r0, .L3998+76
	bl	printk
	ldr	r3, .L3998+52
	movs	r2, #4
	mov	r0, r6
	ldr	r3, [r3, #3832]
	ldr	r3, [r3, #20]
	mov	r1, sp
	str	r3, [sp]
.L3980:
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3989
	b	.L3986
.L3911:
	ldr	r0, .L3998+80
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r7, r0
	cmp	r0, #0
	beq	.L3918
	bl	rknand_device_lock
	mov	r1, #264
	movs	r2, #2
	mov	r3, r7
	movs	r0, #16
	bl	ftl_read
	bl	rknand_device_unlock
	mov	r2, #1024
	mov	r0, r6
	mov	r1, r7
.L3996:
	bl	rk_copy_to_user
	mov	r4, r0
	mov	r0, r7
	cmp	r4, #0
	bne	.L3991
.L3930:
	bl	ftl_free
	b	.L3917
.L3895:
	ldr	r0, .L3998+84
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	mov	r4, r0
	cbz	r0, .L3931
.L3936:
	ldr	r0, .L3998+48
	bl	printk
	b	.L3989
.L3931:
	ldr	r2, [sp, #4]
	ldr	r3, .L3998+88
	cmp	r2, r3
	beq	.L3932
.L3933:
	mov	r4, #-1
	b	.L3917
.L3999:
	.align	2
.L3998:
	.word	__stack_chk_guard
	.word	1074033155
	.word	1074029570
	.word	1074031656
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC180
	.word	.LC182
	.word	.LC183
	.word	.LC184
	.word	.LC181
	.word	.LANCHOR5
	.word	.LC185
	.word	.LC186
	.word	.LC187
	.word	.LC188
	.word	.LC189
	.word	.LC190
	.word	.LC191
	.word	.LC192
	.word	1263358532
.L3932:
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3933
	ldr	r7, .L4000
	mov	r2, #512
	add	r0, sp, #4
	ldr	r1, [r7, #3832]
	bl	memcpy
	ldr	r2, [r7, #3836]
	ldr	r3, .L4000+4
	cmp	r2, r3
	beq	.L3934
	add	r0, sp, #68
	mov	r1, r4
	movs	r2, #128
	str	r4, [sp, #12]
	str	r4, [sp, #16]
	bl	memset
.L3934:
	movs	r1, #0
	mov	r2, #256
	add	r0, sp, #260
	str	r1, [sp, #20]
	bl	memset
	b	.L3994
.L3898:
	ldr	r0, .L4000+8
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3936
	ldr	r2, [sp, #4]
	ldr	r3, .L4000+12
	cmp	r2, r3
	bne	.L3933
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3933
	ldr	r2, .L4000
	ldr	r3, .L4000+4
	ldr	r1, [r2, #3836]
	cmp	r1, r3
	bne	.L3954
	ldr	r3, [sp, #16]
	subs	r1, r3, #1
	cmp	r1, #127
	bhi	.L3955
	ldr	r4, [r2, #3832]
	add	r1, sp, #68
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #16]
	bl	memcpy
	movs	r0, #1
	mov	r1, r4
	b	.L3987
.L3902:
	ldr	r0, .L4000+16
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3936
	ldr	r2, [sp, #4]
	ldr	r3, .L4000+20
	cmp	r2, r3
	bne	.L3933
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3933
	ldr	r6, .L4000
	ldr	r3, [r6, #3840]
	cbnz	r3, .L3937
.L3940:
	movs	r0, #0
	b	.L3890
.L3937:
	ldr	r3, [r6, #3844]
	ldr	r2, .L4000+24
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3938
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r6, #3844]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3938:
	ldr	r1, [r6, #3844]
	movs	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r6, #3832]
	ldr	r2, .L4000+12
	ldr	r1, [r3]
	cmp	r1, r2
	it	ne
	strne	r2, [r3]
	ldr	r7, [r6, #3832]
	itt	ne
	movne	r2, #504
	ldrne	r3, .L4000
	add	r0, r7, #64
	itt	ne
	ldrne	r3, [r3, #3832]
	strne	r4, [r3, #8]
	mov	r4, #0
	it	ne
	strne	r2, [r3, #4]
	movs	r2, #128
	mov	r1, r4
	str	r4, [r7, #12]
	bl	memset
	movs	r0, #1
	mov	r1, r7
	bl	StorageSysDataStore
	str	r4, [r6, #3840]
	str	r4, [r6, #3836]
	b	.L3917
.L3893:
	ldr	r0, .L4000+28
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3936
	ldr	r2, [sp, #4]
	ldr	r3, .L4000+32
	cmp	r2, r3
	bne	.L3933
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3933
	ldr	r6, .L4000
	ldr	r3, [r6, #3840]
	cmp	r3, #1
	beq	.L3940
	ldr	r3, [r6, #3844]
	ldr	r2, .L4000+24
	ldr	r1, [r3]
	cmp	r1, r2
	it	ne
	strne	r2, [r3]
	ldr	r1, [r6, #3844]
	itttt	ne
	movne	r2, #504
	ldrne	r3, [r6, #3844]
	strne	r2, [r3, #4]
	strne	r0, [r3, #8]
	it	ne
	strne	r0, [r3, #12]
	movs	r3, #1
	movs	r0, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r6, #3832]
	ldr	r2, .L4000+12
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3942
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L4000
	ldr	r3, [r3, #3832]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
.L3942:
	ldr	r7, [r6, #3832]
	movs	r4, #0
	movs	r2, #128
	mov	r1, r4
	add	r0, r7, #64
	str	r4, [r7, #12]
	bl	memset
	movs	r0, #1
	mov	r1, r7
	bl	StorageSysDataStore
	movs	r3, #1
	str	r3, [r6, #3840]
	b	.L3917
.L3899:
	ldr	r0, .L4000+36
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3936
	ldr	r2, [sp, #4]
	ldr	r3, .L4000+40
	cmp	r2, r3
	bne	.L3933
	ldr	r2, [sp, #8]
	cmp	r2, #512
	bhi	.L3933
	add	r0, sp, #12
	ldr	r1, .L4000+44
	b	.L3993
.L3900:
	ldr	r7, .L4000+48
	cmp	r4, r7
	bne	.L3943
	ldr	r0, .L4000+52
	b	.L3983
.L3943:
	ldr	r3, .L4000+56
	cmp	r4, r3
	ite	eq
	ldreq	r0, .L4000+60
	ldrne	r0, .L4000+64
.L3983:
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3936
	ldr	r2, [sp, #4]
	ldr	r3, .L4000+68
	cmp	r2, r3
	bne	.L3989
	ldr	r3, .L4000+56
	cmp	r4, r3
	bne	.L3946
	ldr	r3, .L4000
	mov	r0, r6
	add	r1, sp, #4
	movs	r2, #16
	ldr	r3, [r3, #3832]
	ldr	r3, [r3, #20]
	str	r3, [sp, #8]
	strb	r3, [sp, #12]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3890
	b	.L3989
.L3946:
	ldr	r6, .L4000+72
	ldr	r3, [r6, #16]
	mov	r0, r6
	cmp	r3, #10
	bhi	.L3989
	ldr	r3, .L4000
	ldr	r1, [r3, #3832]
	ldr	r3, [sp, #8]
	ldr	r2, [r1, #24]
	cmp	r2, r3
	beq	.L3947
	cbz	r2, .L3947
	mov	r1, r3
	ldr	r0, .L4000+76
	bl	printk
	ldr	r3, [r6, #16]
	adds	r3, r3, #1
	str	r3, [r6, #16]
	b	.L3989
.L3947:
	movs	r2, #0
	cmp	r4, r7
	str	r2, [r0, #16]
	ite	ne
	movne	r2, #1
	moveq	r3, r2
	mov	r0, #1
	it	eq
	moveq	r2, r3
	str	r3, [r1, #24]
	str	r2, [r1, #20]
	bl	StorageSysDataStore
	adds	r0, r0, #1
	bne	.L3986
	mvn	r4, #1
	b	.L3917
.L4001:
	.align	2
.L4000:
	.word	.LANCHOR5
	.word	-1067903959
	.word	.LC193
	.word	1263358532
	.word	.LC194
	.word	1112753220
	.word	1146313043
	.word	.LC195
	.word	1112755781
	.word	.LC196
	.word	1094995539
	.word	.LANCHOR5+3848
	.word	1074031666
	.word	.LC197
	.word	1074031676
	.word	.LC198
	.word	.LC199
	.word	1280262987
	.word	.LANCHOR6
	.word	.LC200
.L3912:
	ldr	r0, .L4002
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3936
	ldr	r2, [sp, #4]
	ldr	r3, .L4002+4
	cmp	r2, r3
	bne	.L3933
	ldr	r2, [sp, #8]
	cmp	r2, #504
	bhi	.L3933
	ldr	r3, .L4002+8
	add	r0, sp, #12
	cmp	r4, r3
	ldr	r3, .L4002+12
	ite	eq
	ldreq	r1, [r3, #20]
	ldrne	r1, [r3, #24]
	adds	r1, r1, #8
.L3993:
	bl	memcpy
.L3994:
	mov	r0, r6
	add	r1, sp, #4
	mov	r2, #520
	b	.L3980
.L3909:
	ldr	r0, .L4002+16
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3936
	ldr	r2, [sp, #4]
	ldr	r3, .L4002+4
	cmp	r2, r3
	bne	.L3933
	ldr	r2, [sp, #8]
	cmp	r2, #504
	bhi	.L3933
	cmp	r4, r7
	add	r2, r2, #8
	ldr	r4, .L4002+12
	bne	.L3951
	add	r1, sp, #4
	ldr	r0, [r4, #20]
	bl	memcpy
	movs	r0, #2
	ldr	r1, [r4, #20]
	b	.L3987
.L3951:
	add	r1, sp, #4
	ldr	r0, [r4, #24]
	bl	memcpy
	ldr	r1, [r4, #24]
	movs	r0, #3
.L3987:
	bl	StorageSysDataStore
	mov	r4, r0
.L3917:
	ldr	r0, .L4002+20
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L3890
.L3953:
	mvn	r0, #21
	b	.L3890
.L3954:
	mvn	r0, #1
	b	.L3890
.L3955:
	mvn	r0, #2
.L3890:
	ldr	r2, [sp, #524]
	ldr	r3, [r5]
	cmp	r2, r3
	beq	.L3952
	bl	__stack_chk_fail
.L3952:
	add	sp, sp, #528
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L4003:
	.align	2
.L4002:
	.word	.LC201
	.word	1145980246
	.word	1074034192
	.word	.LANCHOR6
	.word	.LC202
	.word	.LC203
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r3, #-1
	ldr	r4, .L4013
	mov	r2, #512
	ldr	r5, .L4013+4
	movs	r6, #0
	addw	r0, r4, #3848
	ldr	r1, [r4, #3800]
	str	r3, [r4, #3824]
	add	r3, r1, #512
	str	r3, [r4, #3832]
	str	r1, [r4, #3844]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r5, #20]
	strb	r6, [r4, #3816]
	str	r6, [r4, #3820]
	str	r6, [r4, #3828]
	str	r1, [r5, #24]
	bl	ftl_memcpy
	ldr	r7, [r4, #3844]
	str	r6, [r4, #3836]
	str	r6, [r5, #16]
	ldr	r3, [r7, #16]
	ldr	r8, [r7, #508]
	str	r3, [r4, #3840]
	cmp	r8, #0
	beq	.L4005
	mov	r0, r7
	mov	r1, #508
	bl	js_hash
	cmp	r8, r0
	beq	.L4005
	str	r6, [r7, #16]
	ldr	r0, .L4013+8
	str	r6, [r4, #3840]
	bl	printk
.L4005:
	ldr	r3, [r4, #3840]
	cbz	r3, .L4006
	ldr	r2, .L4013+12
	ldr	r3, .L4013
	str	r2, [r3, #3836]
.L4006:
	ldr	r1, [r5, #20]
	movs	r0, #2
	bl	StorageSysDataLoad
	ldr	r1, [r5, #24]
	movs	r0, #3
	bl	StorageSysDataLoad
	pop	{r4, r5, r6, r7, r8, lr}
	b	rknand_sys_storage_init
.L4014:
	.align	2
.L4013:
	.word	.LANCHOR5
	.word	.LANCHOR6
	.word	.LC204
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
	ldr	r6, .L4025
	str	r0, [r6, #28]
	cmp	r0, #0
	beq	.L4022
	mov	r8, #0
	ldr	r9, .L4025+8
	mov	r10, r6
	mov	r4, r8
	mov	r7, r8
.L4020:
	lsls	r0, r7, #7
	movs	r1, #128
	ldr	r2, [r6, #28]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4018
	ldr	r3, [r10, #28]
	ldr	r0, .L4025+4
	add	r2, r3, #65280
	adds	r2, r2, #252
	ldr	r1, [r3]
	ldr	r3, [r3, #4]
	ldr	r2, [r2]
	bl	printk
	ldr	r5, [r10, #28]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4019
	add	r3, r5, #65280
	ldr	r2, [r5, #4]
	adds	r3, r3, #252
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L4019
	cmp	r4, r3
	itt	cc
	movcc	r8, r7
	movcc	r4, r3
.L4019:
	cbnz	r7, .L4024
	movs	r7, #1
	b	.L4020
.L4024:
	cbz	r4, .L4021
	lsl	r0, r8, #7
	movs	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cbnz	r0, .L4018
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4021:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L4025+8
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	movw	r3, #65532
	str	r7, [r5, r3]
	movw	r3, #64504
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4018:
	ldr	r0, [r6, #28]
	bl	kfree
	movs	r3, #0
	mov	r0, #-1
	str	r3, [r6, #28]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4022:
	mvn	r0, #11
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L4026:
	.align	2
.L4025:
	.word	.LANCHOR6
	.word	.LC205
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
	ldr	r3, .L4033
	ldr	r5, [r3, #28]
	cbz	r5, .L4032
	ldrh	r7, [r5, #10]
	movs	r3, #0
.L4029:
	cmp	r3, r7
	bcs	.L4032
	add	r6, r5, r3, lsl #3
	ldrh	r4, [r6, #16]
	cmp	r4, r0
	bne	.L4030
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
.L4030:
	adds	r3, r3, #1
	b	.L4029
.L4032:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L4034:
	.align	2
.L4033:
	.word	.LANCHOR6
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
	ldr	r2, .L4057
	.pad #28
	sub	sp, sp, #28
	mov	ip, r0
	mov	r3, r1
	ldr	r4, [r2, #28]
	cmp	r4, #0
	beq	.L4050
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	ldrh	r2, [r4, #10]
	bic	r7, r7, #63
	movs	r6, #0
	str	r1, [sp, #4]
.L4037:
	cmp	r6, r2
	bcs	.L4055
	add	r5, r4, r6, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L4038
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L4039
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4050
	ldrh	r8, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #12]
.L4040:
	ldr	r2, [sp, #12]
	adds	r5, r5, #8
	cmp	r6, r2
	bcs	.L4056
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
	b	.L4040
.L4056:
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
	b	.L4054
.L4039:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, r0, fp
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L4054
.L4038:
	adds	r6, r6, #1
	b	.L4037
.L4055:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4050
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
.L4054:
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
	b	.L4036
.L4050:
	mov	r0, #-1
.L4036:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4058:
	.align	2
.L4057:
	.word	.LANCHOR6
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
	beq	.L4068
	ldr	r3, .L4075
	cmp	r6, r3
	beq	.L4062
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L4063
	b	.L4074
.L4062:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4074
	ldr	r2, [r4]
	ldr	r3, .L4075+4
	cmp	r2, r3
	beq	.L4065
.L4066:
	mov	r5, #-1
	b	.L4061
.L4065:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	adds	r3, r0, #1
	beq	.L4066
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
	b	.L4061
.L4063:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L4074
	ldr	r2, [r4]
	ldr	r3, .L4075+4
	cmp	r2, r3
	bne	.L4066
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4066
	mov	r0, r4
	mov	r1, r5
	adds	r2, r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L4074
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4061
.L4074:
	mvn	r5, #13
.L4061:
	mov	r0, r4
	bl	kfree
	b	.L4060
.L4068:
	mov	r5, #-1
.L4060:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L4076:
	.align	2
.L4075:
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
	.type	__func__.20216, %object
	.size	__func__.20216, 11
__func__.20216:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.space	1
	.type	__func__.20991, %object
	.size	__func__.20991, 17
__func__.20991:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21010, %object
	.size	__func__.21010, 16
__func__.21010:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21276, %object
	.size	__func__.21276, 17
__func__.21276:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.20959, %object
	.size	__func__.20959, 12
__func__.20959:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.21258, %object
	.size	__func__.21258, 21
__func__.21258:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20289, %object
	.size	__func__.20289, 8
__func__.20289:
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
	.ascii	"FTL version: 5.0.56 20190508\000"
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
	.ascii	"...%s enter...\012\000"
.LC108:
	.ascii	"superBlkID = %x vpc=%x\012\000"
.LC109:
	.ascii	"flashmode = %x pagenum = %x %x\012\000"
.LC110:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC111:
	.ascii	"blk = %x vpc=%x mode = %x\012\000"
.LC112:
	.ascii	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC113:
	.ascii	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC114:
	.ascii	"ftl_scan_all_ppa blk %x page %x\012\000"
.LC115:
	.ascii	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC116:
	.ascii	"%s finished\012\000"
.LC117:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC118:
	.ascii	"bad block:%d %d\012\000"
.LC119:
	.ascii	"FMFB:%d %d\012\000"
.LC120:
	.ascii	"E:bad block:%d\012\000"
.LC121:
	.ascii	"FMFB:Save %d %d\012\000"
.LC122:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC123:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC124:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC125:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC126:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC127:
	.ascii	"decrement_vpc_count %x = %d in free list\012\000"
.LC128:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC129:
	.ascii	"page map lost: %x %x\012\000"
.LC130:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC131:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC132:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC133:
	.ascii	"no ect\000"
.LC134:
	.ascii	"slc mode\000"
.LC135:
	.ascii	"BBT:\000"
.LC136:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC137:
	.ascii	"map_ppn:\000"
.LC138:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC139:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC140:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC141:
	.ascii	"Ftlscanalldata = %x\012\000"
.LC142:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC143:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC144:
	.ascii	"RSB refresh addr %x\012\000"
.LC145:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC146:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC147:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC148:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
.LC149:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC150:
	.ascii	"GC des block %x done\012\000"
.LC151:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC152:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC153:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC154:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC155:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC156:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC157:
	.ascii	"FtlInit %x\012\000"
.LC158:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC159:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC160:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC161:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\012"
	.ascii	"\000"
.LC162:
	.ascii	":\000"
.LC163:
	.ascii	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC164:
	.ascii	"Mblk:\000"
.LC165:
	.ascii	"L2P:\000"
.LC166:
	.ascii	"L2PC:\000"
.LC167:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC168:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC169:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC170:
	.ascii	"idblk:\000"
.LC171:
	.ascii	"idb reverse %x %x\012\000"
.LC172:
	.ascii	"write_idblock totle_sec %x %x %x %x\012\000"
.LC173:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC174:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC175:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC176:
	.ascii	"write\000"
.LC177:
	.ascii	"read\000"
.LC178:
	.ascii	"write_idblock error %d\012\000"
.LC179:
	.ascii	"wl_lba %p %x %x %x\012\000"
.LC180:
	.ascii	"READ_SECTOR_IO\012\000"
.LC181:
	.ascii	"rk_copy_from_user error\012\000"
.LC182:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC183:
	.ascii	"rk_copy_to_user error\012\000"
.LC184:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC185:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC186:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC187:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC188:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC189:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC190:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC191:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC192:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC193:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC194:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC195:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC196:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC197:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC198:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC199:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC200:
	.ascii	"LockKey not match %d\012\000"
.LC201:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC202:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC203:
	.ascii	"return ret = %lx\012\000"
.LC204:
	.ascii	"secureBootEn check error\012\000"
.LC205:
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
	.size	NandFlashParaTbl, 2592
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
.LANCHOR5 = . + 13032
.LANCHOR6 = . + 17376
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
	.size	slcPageToMlcPageTbl, 512
slcPageToMlcPageTbl:
	.space	512
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
	.size	mlcPageToSlcPageTbl, 1024
mlcPageToSlcPageTbl:
	.space	1024
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
