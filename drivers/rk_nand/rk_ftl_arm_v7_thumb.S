/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2017-12-13
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
	.eabi_attribute 18, 4
	.thumb
	.file	"rk_ftl_arm_v7.S"
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
	ldr	r4, .L38
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
.L39:
	.align	2
.L38:
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
	ldr	r4, .L44
	lsrs	r5, r1, #8
	add	r3, r4, r0, lsl #3
	ldrb	r4, [r4, #60]	@ zero_extendqisi2
	ldr	r2, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r4, #1
	lsr	r4, r1, #16
	lsl	r3, r3, #8
	add	r3, r3, r2
	bne	.L41
	movs	r2, #6
	str	r2, [r3, #2056]
	movs	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r6, [r3, #2052]
	str	r5, [r3, #2052]
	str	r4, [r3, #2052]
	b	.L43
.L41:
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
.L43:
	movs	r2, #224
	str	r2, [r3, #2056]
	pop	{r4, r5, r6, lr}
	b	FlashSetRandomizer
.L45:
	.align	2
.L44:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	1
	.global	flash_enter_slc_mode
	.thumb
	.thumb_func
	.type	flash_enter_slc_mode, %function
flash_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L51
	ldrb	r2, [r3, #76]	@ zero_extendqisi2
	cbz	r2, .L46
	add	r0, r3, r0, lsl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r2, lsl #8
	movs	r2, #218
	str	r2, [r3, #2056]
.L46:
	bx	lr
.L52:
	.align	2
.L51:
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
	@ link register save eliminated.
	ldr	r3, .L58
	ldrb	r2, [r3, #76]	@ zero_extendqisi2
	cbz	r2, .L53
	add	r0, r3, r0, lsl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r2, lsl #8
	movs	r2, #223
	str	r2, [r3, #2056]
.L53:
	bx	lr
.L59:
	.align	2
.L58:
	.word	.LANCHOR0
	.fnend
	.size	flash_exit_slc_mode, .-flash_exit_slc_mode
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
	ldr	r4, .L61
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
.L62:
	.align	2
.L61:
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
	ldr	r5, .L68
	add	r0, r5, r0, lsl #3
	ldrb	r3, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	lsls	r3, r3, #8
	cbz	r2, .L64
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
.L64:
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
.L69:
	.align	2
.L68:
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
	ldr	r4, .L71
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
.L72:
	.align	2
.L71:
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
	ldr	r3, .L74
	add	r0, r3, r0, lsl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r2, lsl #8
	movs	r2, #16
	str	r2, [r3, #2056]
	bx	lr
.L75:
	.align	2
.L74:
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
	ldr	r2, .L77
	add	r0, r2, r0, lsl #3
	ldrb	r2, [r2, #54]	@ zero_extendqisi2
	ldrb	r1, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r1, lsl #8
	str	r2, [r3, #2056]
	bx	lr
.L78:
	.align	2
.L77:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	1
	.global	JSHash
	.thumb
	.thumb_func
	.type	JSHash, %function
JSHash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L83
	add	r1, r1, r0
	push	{r4, lr}
	.save {r4, lr}
.L80:
	cmp	r0, r1
	beq	.L82
	lsls	r2, r3, #5
	ldrb	r4, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, r4
	eors	r3, r3, r2
	b	.L80
.L82:
	mov	r0, r3
	pop	{r4, pc}
.L84:
	.align	2
.L83:
	.word	1204201446
	.fnend
	.size	JSHash, .-JSHash
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
	cbnz	r0, .L87
	ldr	r3, .L113
.L88:
	strh	r0, [r3, r0, lsl #1]	@ movhi
	adds	r0, r0, #1
	cmp	r0, #256
	bne	.L88
.L92:
	movs	r1, #255
	ldr	r0, .L113+4
	mov	r2, #1024
	uxth	r4, r4
	bl	memset
	movs	r3, #0
	ldr	r1, .L113+8
	b	.L89
.L87:
	cmp	r0, #1
	bne	.L90
	ldr	r5, .L113
	movs	r3, #0
.L91:
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
	bne	.L91
	b	.L92
.L90:
	cmp	r0, #2
	bne	.L93
	ldr	r0, .L113
	movs	r3, #0
.L94:
	uxth	r2, r3
	lsls	r1, r2, #1
	subs	r1, r1, #1
	cmp	r2, #1
	it	hi
	movhi	r2, r1
	strh	r2, [r0, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #256
	bne	.L94
	b	.L92
.L93:
	cmp	r0, #3
	bne	.L95
	ldr	r5, .L113
	movs	r3, #0
.L96:
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
	bne	.L96
	b	.L92
.L95:
	cmp	r0, #4
	mov	r3, #0
	bne	.L97
	ldr	r2, .L113+8
	strh	r3, [r2, #80]	@ movhi
	movs	r3, #1
	strh	r0, [r2, #88]	@ movhi
	strh	r3, [r2, #82]	@ movhi
	movs	r3, #2
	strh	r3, [r2, #84]	@ movhi
	movs	r3, #3
	strh	r3, [r2, #86]	@ movhi
	movs	r3, #5
	strh	r3, [r2, #90]	@ movhi
	movs	r3, #7
	strh	r3, [r2, #92]	@ movhi
	movs	r3, #8
	strh	r3, [r2, #94]!	@ movhi
.L98:
	tst	r3, #1
	ite	ne
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	adds	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L98
	b	.L92
.L97:
	cmp	r0, #5
	bne	.L99
	ldr	r2, .L113
.L100:
	strh	r3, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #16
	bne	.L100
	ldr	r2, .L113+12
.L101:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L101
	b	.L92
.L99:
	cmp	r0, #6
	bne	.L92
	ldr	r5, .L113
.L102:
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
	bne	.L102
	b	.L92
.L89:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L112
	ldr	r2, .L113
	ldrh	r2, [r2, r3, lsl #1]
	adds	r3, r3, #1
	add	r0, r1, r2, lsl #1
	strh	r2, [r0, #592]	@ movhi
	b	.L89
.L112:
	pop	{r4, r5, r6, pc}
.L114:
	.align	2
.L113:
	.word	.LANCHOR0+80
	.word	.LANCHOR0+592
	.word	.LANCHOR0
	.word	.LANCHOR0+110
	.fnend
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
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
	.global	FlashDieInfoInit
	.thumb
	.thumb_func
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r3, #0
	ldr	r4, .L134
	strb	r3, [r4, #1616]
	strb	r3, [r4, #1617]
	ldr	r3, .L134+4
	ldrh	r3, [r3, #266]
	cmp	r3, #256
	bls	.L117
	mov	r3, #512
	b	.L133
.L117:
	cmp	r3, #128
	it	hi
	movhi	r3, #256
.L133:
	ldr	r0, .L134+8
	movs	r1, #8
	str	r3, [r4, #4]
	bl	__memzero
	ldr	r0, .L134+12
	movs	r1, #32
	bl	__memzero
	ldr	r0, .L134+16
	movs	r1, #128
	ldr	r8, .L134
	bl	__memzero
	ldr	r5, [r4, #1788]
	movs	r6, #0
	add	r10, r8, #1792
	adds	r7, r5, #1
.L121:
	mov	r0, r7
	add	r1, r10, r6, lsl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r9, .L134+20
	cbnz	r0, .L120
	ldrb	r3, [r8, #1616]	@ zero_extendqisi2
	add	r2, r8, r3, lsl #2
	str	r0, [r2, #1628]
	adds	r2, r3, #1
	add	r3, r3, r8
	strb	r2, [r8, #1616]
	strb	r6, [r3, #1620]
.L120:
	adds	r6, r6, #1
	cmp	r6, #4
	bne	.L121
	ldrb	r3, [r4, #1616]	@ zero_extendqisi2
	ldr	r2, .L134
	strb	r3, [r4, #1617]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L122
.L126:
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #1616]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #14]
	smulbb	r3, r2, r3
	strh	r3, [r4, #1824]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L122:
	ldr	r8, [r2, #4]
	movs	r6, #0
	mov	r10, r2
.L125:
	mov	r0, r7
	add	r1, r9, r6, lsl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cbnz	r0, .L123
	ldrb	r1, [r5, #13]	@ zero_extendqisi2
	ldrh	r3, [r5, #14]
	ldrb	r2, [r10, #1616]	@ zero_extendqisi2
	mul	r1, r8, r1
	and	r3, r3, #65280
	add	r0, r10, r2, lsl #2
	muls	r3, r1, r3
	str	r3, [r0, #1628]
	ldrb	r1, [r5, #23]	@ zero_extendqisi2
	cbz	r1, .L124
	lsls	r3, r3, #1
	str	r3, [r0, #1628]
.L124:
	adds	r3, r2, #1
	add	r2, r2, r4
	strb	r3, [r4, #1616]
	strb	r6, [r2, #1620]
.L123:
	adds	r6, r6, #1
	cmp	r6, #4
	bne	.L125
	b	.L126
.L135:
	.align	2
.L134:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1620
	.word	.LANCHOR0+1628
	.word	.LANCHOR0+1660
	.word	.LANCHOR0+1792
	.fnend
	.size	FlashDieInfoInit, .-FlashDieInfoInit
	.align	1
	.global	FlashSuspend
	.thumb
	.thumb_func
	.type	FlashSuspend, %function
FlashSuspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L137
	movs	r0, #0
	ldr	r2, [r3, #1828]
	ldr	r1, [r2]
	str	r1, [r3, #1832]
	ldr	r1, [r2, #4]
	str	r1, [r3, #1836]
	ldr	r1, [r2, #8]
	str	r1, [r3, #1840]
	ldr	r1, [r2, #12]
	str	r1, [r3, #1844]
	ldr	r1, [r2, #304]
	str	r1, [r3, #1848]
	ldr	r1, [r2, #308]
	str	r1, [r3, #1852]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #1856]
	str	r2, [r3, #1860]
	bx	lr
.L138:
	.align	2
.L137:
	.word	.LANCHOR0
	.fnend
	.size	FlashSuspend, .-FlashSuspend
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
	ldr	r0, .L144
	mov	r8, r3
	ldr	r4, [r7, #4]
	mov	r10, r1
	mov	r9, r2
	ldrh	r3, [r0, #1878]
	bic	r4, r4, #-2147483648
	ldrh	r5, [r0, #1876]
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
	bne	.L141
	ldrb	r2, [r6, #76]	@ zero_extendqisi2
	cbnz	r2, .L141
	ldr	r2, .L144
	add	r1, r2, r1, lsl #1
	ldrh	r1, [r1, #80]
.L141:
	add	r6, r6, r0, lsl #2
	ldr	r2, [r6, #1628]
	mla	fp, fp, r3, r2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	cmp	r3, #1
	add	r1, r1, fp
	str	r1, [r9]
	str	r0, [r8]
	bls	.L143
	ldr	r0, [r7, #4]
	ldr	r3, [r7, #40]
	add	r0, r0, #1024
	subs	r3, r0, r3
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	b	.L142
.L143:
	movs	r0, #0
.L142:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L145:
	.align	2
.L144:
	.word	.LANCHOR0
	.fnend
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	1
	.global	ReadFlashInfo
	.thumb
	.thumb_func
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r1, #11
	mov	r4, r0
	bl	__memzero
	ldr	r3, .L150
	movs	r5, #1
	ldr	r1, [r3, #1788]
	ldr	r2, [r3, #4]
	addw	r3, r3, #1620
	ldrb	r0, [r1, #9]	@ zero_extendqisi2
	smulbb	r2, r0, r2
	ldrb	r0, [r3, #272]	@ zero_extendqisi2
	strb	r0, [r4, #7]
	uxth	r2, r2
	strh	r2, [r4, #4]	@ unaligned
	ldrb	r0, [r1, #13]	@ zero_extendqisi2
	muls	r2, r0, r2
	ldrh	r0, [r1, #14]
	ldrb	r1, [r1, #8]	@ zero_extendqisi2
	muls	r2, r0, r2
	muls	r2, r1, r2
	str	r2, [r4]	@ unaligned
	ldr	r2, [r3, #168]
	ldrb	r0, [r3, #-4]	@ zero_extendqisi2
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	movs	r1, #32
	strb	r1, [r4, #8]
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	strb	r2, [r4, #9]
	movs	r2, #0
	strb	r2, [r4, #10]
.L147:
	uxtb	r1, r2
	cmp	r1, r0
	bcs	.L149
	ldrb	r1, [r2, r3]	@ zero_extendqisi2
	adds	r2, r2, #1
	lsl	r6, r5, r1
	ldrb	r1, [r4, #10]	@ zero_extendqisi2
	orrs	r1, r1, r6
	strb	r1, [r4, #10]
	b	.L147
.L149:
	pop	{r4, r5, r6, pc}
.L151:
	.align	2
.L150:
	.word	.LANCHOR0
	.fnend
	.size	ReadFlashInfo, .-ReadFlashInfo
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
	ldr	r3, .L153
	ldr	r2, [r3, #1896]
	str	r0, [r3, #1896]
	mov	r0, r2
	bx	lr
.L154:
	.align	2
.L153:
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
	ldr	r3, .L156
	ldr	r3, [r3, #1788]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L157:
	.align	2
.L156:
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
	ldr	r3, .L160
	add	r0, r3, r0, lsl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	adds	r2, r2, #8
	add	r0, r0, r2, lsl #8
	bx	lr
.L161:
	.align	2
.L160:
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
	ldr	r3, .L163
	ldr	r2, [r3, #1828]
	lsls	r3, r0, #8
	orr	r0, r3, r0, lsl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L164:
	.align	2
.L163:
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
	ldr	r3, .L166
	orr	r0, r0, #16640
	ldr	r3, [r3, #1828]
	str	r0, [r3, #344]
	bx	lr
.L167:
	.align	2
.L166:
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
	ldr	r3, .L171
	ldr	r2, [r3, #1828]
	ldr	r3, [r2]
	cbnz	r0, .L169
	bfi	r3, r0, #13, #1
	b	.L170
.L169:
	orr	r3, r3, #253952
.L170:
	str	r3, [r2]
	bx	lr
.L172:
	.align	2
.L171:
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
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r5, .L180
	ldr	r3, [r5, #1828]
	ldr	r4, [r3]
	ands	r3, r0, #6
	beq	.L174
	orr	r4, r4, #24576
	lsls	r3, r0, #29
	bfc	r4, #15, #1
	mov	r0, #0
	orr	r4, r4, #196608
	it	mi
	orrmi	r4, r4, #32768
	bl	rknand_get_clk_rate
	ldr	r3, [r5, #1828]
	movw	r2, #8322
	str	r2, [r3, #344]
	add	r2, r2, #1040384
	addw	r2, r2, #3969
	str	r2, [r3, #304]
	movs	r2, #38
	str	r2, [r3, #308]
	movs	r2, #39
	str	r2, [r3, #308]
	b	.L176
.L174:
	bfi	r4, r3, #13, #1
.L176:
	ldr	r3, [r5, #1828]
	movs	r0, #0
	str	r4, [r3]
	pop	{r3, r4, r5, pc}
.L181:
	.align	2
.L180:
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
	ldr	r2, .L183
	add	r0, r2, r0, lsl #3
	movs	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	lsls	r2, r2, r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L184:
	.align	2
.L183:
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
	ldr	r3, .L186
	add	r0, r3, r0, lsl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L187:
	.align	2
.L186:
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.pad #8
	sub	sp, sp, #8
	lsrs	r0, r0, #4
	str	r0, [sp, #4]
.L189:
	ldr	r0, [sp, #4]
	subs	r3, r0, #1
	str	r3, [sp, #4]
	cmp	r0, #0
	bne	.L189
	add	sp, sp, #8
	@ sp needed
	bx	lr
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
	ldr	r3, .L192
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
.L193:
	.align	2
.L192:
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
	ldr	r9, .L203+8
	movs	r4, #0
	ldr	r7, .L203
	mov	r5, r0
	add	r10, r9, #288
	mov	r6, r1
.L195:
	ldrb	r3, [r7, #1901]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L202
	movs	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	movs	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldrb	r3, [r7, #1900]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L196
	add	r3, r4, r8
	add	r3, r3, r10
	b	.L201
.L196:
	cmp	r3, #35
	bne	.L198
	ldr	r2, .L203+4
	add	r3, r4, r8
	add	r3, r3, r2
.L201:
	ldrsb	r3, [r3, #5]
	b	.L200
.L198:
	add	r3, r9, r6
	ldrsb	r3, [r3, #432]
.L200:
	str	r3, [r5]
	adds	r4, r4, #1
	b	.L195
.L202:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L204:
	.align	2
.L203:
	.word	.LANCHOR0
	.word	.LANCHOR1+336
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
	ldr	r3, .L209
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, lsl #2
	movs	r4, #0
	ldr	r8, .L209+4
	adds	r5, r1, #3
	mov	r9, #161
	mov	r6, r0
	mov	r7, r3
	mov	r10, r4
.L206:
	ldrb	r3, [r8, #1901]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L208
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	adds	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L206
.L208:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L210:
	.align	2
.L209:
	.word	.LANCHOR1+440
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
	ldr	r6, .L218
	mov	r9, r2
	mov	r5, r0
	mov	r8, r1
	addw	r4, r6, #1902
	ldr	r3, [r6, #1788]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	lsls	r3, r0, #3
	cmp	r2, #6
	bne	.L212
	add	r4, r4, r0, lsl #6
	adds	r4, r4, #20
	add	r4, r4, r7, lsl #2
	b	.L213
.L212:
	cmp	r2, #7
	itett	eq
	moveq	r2, #160
	addne	r2, r7, r3
	mlaeq	r4, r2, r0, r4
	addeq	r2, r7, r7, lsl #2
	ittee	ne
	addne	r4, r4, r2, lsl #3
	addne	r4, r4, #20
	addeq	r4, r4, #28
	addeq	r4, r4, r2, lsl #1
.L213:
	add	r3, r3, r6
	mov	r0, r5
	add	r8, r8, #-1
	subs	r4, r4, #1
	ldrb	fp, [r3, #16]	@ zero_extendqisi2
	ldr	r10, [r3, #12]
	bl	NandcFlashCs
	movs	r3, #54
	lsl	fp, fp, #8
	add	r2, r10, fp
	str	r3, [r2, #2056]
	add	r3, r9, #-1
	add	r9, r9, r8
	mov	r8, r2
.L215:
	cmp	r3, r9
	beq	.L217
	ldrb	r2, [r3, #1]!	@ zero_extendqisi2
	movs	r0, #200
	str	r2, [r8, #2052]
	str	r3, [sp, #4]
	bl	NandcDelayns
	ldrsb	r2, [r4, #1]!
	str	r2, [r8, #2048]
	ldr	r3, [sp, #4]
	b	.L215
.L217:
	add	fp, fp, r10
	movs	r3, #22
	mov	r0, r5
	add	r5, r5, r6
	str	r3, [fp, #2056]
	bl	NandcFlashDeCs
	strb	r7, [r5, #2756]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L219:
	.align	2
.L218:
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
	ldr	r3, .L226
	ldr	r2, [r3, #1788]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	subs	r2, r2, #1
	cmp	r2, #6
	bhi	.L220
	movs	r4, #0
	mov	r5, r3
.L221:
	ldr	r2, .L226+4
	uxtb	r0, r4
	ldrb	r3, [r2, r4, lsl #3]	@ zero_extendqisi2
	cmp	r3, #173
	bne	.L222
	ldrb	r1, [r5, #1903]	@ zero_extendqisi2
	adds	r2, r2, #114
	movs	r3, #0
	bl	HynixSetRRPara
.L222:
	adds	r4, r4, #1
	cmp	r4, #4
	bne	.L221
.L220:
	pop	{r3, r4, r5, pc}
.L227:
	.align	2
.L226:
	.word	.LANCHOR0
	.word	.LANCHOR0+1792
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
	ldr	r5, .L238
	add	r0, r5, r0, lsl #3
	ldr	r3, [r5, #1788]
	ldrb	r4, [r0, #16]	@ zero_extendqisi2
	ldr	r6, [r0, #12]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	lsl	r3, r4, #8
	add	r4, r4, #8
	bne	.L229
	cbnz	r2, .L230
	ldrb	r2, [r5, #57]	@ zero_extendqisi2
	b	.L237
.L230:
	ldrb	r2, [r5, #58]	@ zero_extendqisi2
.L237:
	add	r3, r3, r6
	str	r2, [r3, #2056]
	ldrb	r0, [r5, #59]	@ zero_extendqisi2
	cbz	r0, .L233
	movs	r3, #0
	add	r5, r6, r4, lsl #8
.L232:
	cmp	r3, r0
	bcs	.L233
	lsls	r2, r3, #3
	adds	r3, r3, #1
	lsr	r2, r1, r2
	uxtb	r2, r2
	str	r2, [r5, #4]
	b	.L232
.L229:
	add	r3, r3, r6
	movs	r2, #112
	str	r2, [r3, #2056]
.L233:
	movs	r0, #80
	lsls	r4, r4, #8
	bl	NandcDelayns
	ldr	r0, [r6, r4]
	uxtb	r0, r0
	pop	{r4, r5, r6, pc}
.L239:
	.align	2
.L238:
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
.L244:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	mov	r3, r0
	beq	.L244
	lsls	r3, r3, #25
	bpl	.L244
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
	ldr	r5, .L254
	add	r4, r5, r0, lsl #4
	ldr	r3, [r4, #1668]
	ldrb	r6, [r4, #1660]	@ zero_extendqisi2
	cbz	r3, .L249
	add	r5, r5, r8, lsl #2
	mov	r0, r6
	bl	NandcFlashCs
	ldr	r1, [r4, #1664]
	ldr	r2, [r5, #1628]
	mov	r0, r6
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r5, r0
	mov	r0, r6
	bl	NandcFlashDeCs
	ldr	r2, [r4, #1668]
	sbfx	r3, r5, #0, #1
	str	r3, [r2]
	movs	r2, #0
	ldr	r1, [r4, #1672]
	str	r2, [r4, #1668]
	cbz	r1, .L249
	str	r3, [r1]
	str	r2, [r4, #1672]
.L249:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L255:
	.align	2
.L254:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	1
	.global	NandcWaitFlashReady
	.thumb
	.thumb_func
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L261
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, lsl #3
	ldr	r4, .L261+4
	ldr	r5, [r0, #12]
.L258:
	movs	r0, #100
	bl	NandcDelayns
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #22
	bmi	.L259
	subs	r4, r4, #1
	bne	.L258
	mov	r0, #-1
	b	.L257
.L259:
	movs	r0, #0
.L257:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L262:
	.align	2
.L261:
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
	ldr	r3, .L264
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
.L265:
	.align	2
.L264:
	.word	.LANCHOR0
	.fnend
	.size	FlashReset, .-FlashReset
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
	ldr	r5, .L295
	movs	r4, #0
	movs	r7, #239
	mov	lr, #128
	mov	ip, #1
	mov	r8, #35
	ldrb	r3, [r5, #2760]	@ zero_extendqisi2
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
.L277:
	ldr	r1, .L295+4
	ldr	r3, [sp, #4]
	ldrb	r1, [r4, r1]	@ zero_extendqisi2
	ldr	r6, [r4, r3]
	add	r3, r3, r4
	cmp	r1, #152
	ldr	r3, [r3, #4]
	beq	.L268
	cmp	r1, #69
	beq	.L268
	cmp	r1, #173
	beq	.L268
	cmp	r1, #44
	bne	.L269
.L268:
	cmp	r0, #1
	uxtb	r3, r3
	bne	.L270
	ldr	r5, [sp, #12]
	cbz	r5, .L269
	lsls	r3, r3, #8
	cmp	r1, #173
	add	fp, r6, r3
	str	r7, [fp, #2056]
	bne	.L271
	str	r0, [fp, #2052]
	b	.L294
.L271:
	cmp	r1, #44
	itete	eq
	streq	r0, [fp, #2052]
	strne	lr, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L275
.L270:
	ldr	r5, [sp, #8]
	cbz	r5, .L269
	lsls	r3, r3, #8
	cmp	r1, #173
	add	fp, r6, r3
	str	r7, [fp, #2056]
	bne	.L274
	str	ip, [fp, #2052]
	str	r9, [fp, #2048]
	b	.L275
.L274:
	cmp	r1, #44
	bne	.L276
	str	ip, [fp, #2052]
	str	r8, [fp, #2048]
	b	.L275
.L276:
	str	lr, [fp, #2052]
.L294:
	str	r2, [fp, #2048]
.L275:
	add	r3, r3, r6
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L269:
	adds	r4, r4, #8
	cmp	r4, #32
	bne	.L277
	movs	r0, #0
	bl	NandcWaitFlashReady
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L296:
	.align	2
.L295:
	.word	.LANCHOR0
	.word	.LANCHOR0+1792
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
	ldr	r5, .L298
	ldr	r3, .L298+4
	add	r5, r5, r0, lsl #3
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
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
.L299:
	.align	2
.L298:
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
	ldr	r2, .L301
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
.L302:
	.align	2
.L301:
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
	ldr	r0, .L310
	ldr	r1, .L310+4
	add	r4, r4, r4, lsl #2
	movs	r2, #0
	sub	r6, r0, #48
.L304:
	ldrb	r3, [r1, #1901]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L309
	ldrb	r3, [r1, #1900]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	ite	eq
	addeq	r3, r3, r6
	addne	r3, r3, r0
	adds	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L304
.L309:
	movs	r0, #0
	pop	{r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L311:
	.align	2
.L310:
	.word	.LANCHOR1+336
	.word	.LANCHOR0
	.fnend
	.size	SandiskSetRRPara, .-SandiskSetRRPara
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
	ldr	r8, .L323+4
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
.L313:
	cmp	r7, r9
	beq	.L322
	rsb	r3, r7, r9
	add	r2, sp, #8
	adds	r0, r6, r5
	movs	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #1616]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r3, r2
	bcc	.L314
	mov	r3, #-1
	str	r3, [r6, r5]
	b	.L315
.L314:
	add	r2, r8, r3
	add	r3, r8, r3, lsl #4
	ldrb	r4, [r2, #1620]	@ zero_extendqisi2
	strb	r4, [r3, #1660]
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
	bne	.L317
	ldr	r0, .L323
	ldr	r1, [sp, #8]
	bl	printk
.L317:
	mov	r0, r4
	bl	NandcFlashDeCs
.L315:
	adds	r7, r7, #1
	adds	r5, r5, #36
	b	.L313
.L322:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L324:
	.align	2
.L323:
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
	ldr	r3, .L329
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
	bne	.L326
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
	b	.L328
.L326:
	str	lr, [r4, #2052]
	str	r2, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
.L328:
	str	r10, [r4, #2052]
	movs	r3, #48
	str	r9, [r4, #2052]
	mov	r0, r8
	str	r5, [r4, #2052]
	mov	r1, r7
	str	r3, [r4, #2056]
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L330:
	.align	2
.L329:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	1
	.global	FlashDeInit
	.thumb
	.thumb_func
	.type	FlashDeInit, %function
FlashDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movs	r0, #0
	ldr	r4, .L339
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldrb	r3, [r4, #2761]	@ zero_extendqisi2
	cbz	r3, .L332
	ldrb	r3, [r4, #2760]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L332
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	movs	r3, #0
	strb	r3, [r4, #2761]
.L332:
	ldr	r3, [r4, #12]
	movs	r0, #0
	str	r0, [r3, #336]
	pop	{r4, pc}
.L340:
	.align	2
.L339:
	.word	.LANCHOR0
	.fnend
	.size	FlashDeInit, .-FlashDeInit
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
	ldr	r3, .L342
	add	r0, r3, r0, lsl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L343:
	.align	2
.L342:
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
	ldr	r1, .L354
	bl	__aeabi_idiv
	ldr	r3, .L354+4
	ldr	r3, [r3, #1828]
	cmp	r0, #250
	ble	.L345
	movw	r2, #8354
	b	.L352
.L345:
	cmp	r0, #220
	bgt	.L353
	cmp	r0, #185
	ble	.L348
	movw	r2, #4226
	b	.L352
.L348:
	cmp	r0, #160
	ble	.L349
	movw	r2, #4194
	b	.L352
.L349:
	cmp	r4, #35
	bhi	.L350
	movw	r2, #4193
	b	.L352
.L350:
	cmp	r4, #99
	bls	.L351
.L353:
	movw	r2, #8322
	b	.L352
.L351:
	movw	r2, #4225
.L352:
	str	r2, [r3, #4]
	pop	{r4, pc}
.L355:
	.align	2
.L354:
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
	bls	.L357
	sub	r3, r0, #4224
	subs	r3, r3, #1
	cmp	r3, #1
	bls	.L357
	movw	r3, #8322
	cmp	r0, r3
	bne	.L358
.L357:
	ldr	r3, .L359
	ldr	r3, [r3, #1828]
	str	r0, [r3, #4]
.L358:
	ldr	r3, .L359+4
	ldrb	r0, [r3, #277]	@ zero_extendqisi2
	b	NandcTimeCfg
.L360:
	.align	2
.L359:
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
	movs	r5, #0
	ldr	r4, .L362
	movs	r3, #1
	str	r0, [r4, #1828]
	str	r0, [r4, #12]
	str	r3, [r4, #24]
	movs	r3, #2
	str	r0, [r4, #20]
	str	r3, [r4, #32]
	movs	r3, #3
	str	r0, [r4, #28]
	str	r0, [r4, #36]
	str	r5, [r4, #16]
	str	r3, [r4, #40]
	ldr	r3, [r0]
	and	r3, r3, #253952
	ubfx	r2, r3, #13, #1
	str	r2, [r4, #2764]
	ldr	r2, [r0, #352]
	and	r3, r3, #245760
	orr	r3, r3, #256
	ubfx	r2, r2, #16, #4
	str	r2, [r4, #2768]
	ldr	r2, [r0, #352]
	str	r2, [r4, #2772]
	str	r3, [r0]
	movs	r0, #40
	ldr	r3, [r4, #1828]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #1828]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L362+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #2804]
	str	r5, [r4, #2812]
	str	r0, [r4, #2776]
	str	r0, [r4, #2780]
	add	r0, r0, #32768
	str	r0, [r4, #2784]
	pop	{r3, r4, r5, pc}
.L363:
	.align	2
.L362:
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
	ldr	r4, .L365
	ldr	r5, [r4, #1828]
	ldr	r5, [r5, #4]
	str	r5, [r0]
	ldr	r0, [r4, #1828]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [r4, #1828]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [r4, #1828]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, lsl #16
	str	r2, [r3]
	pop	{r4, r5, pc}
.L366:
	.align	2
.L365:
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
	ldr	r3, .L375
	movs	r1, #1
	ldr	r2, [r3, #1828]
	str	r0, [r3, #2816]
	movs	r3, #0
	str	r1, [r2, #8]
	movs	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L368
.L371:
	bfc	r3, #4, #1
	b	.L369
.L368:
	cmp	r0, #24
	bne	.L370
	orr	r3, r3, #16
	b	.L369
.L370:
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L371
.L369:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L376:
	.align	2
.L375:
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
	ldr	r3, .L378
	strb	r0, [r3, #1892]
	b	NandcBchSel
.L379:
	.align	2
.L378:
	.word	.LANCHOR0
	.fnend
	.size	FlashBchSel, .-FlashBchSel
	.align	1
	.global	FlashResume
	.thumb
	.thumb_func
	.type	FlashResume, %function
FlashResume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L388
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	ldr	r2, [r3, #1828]
	add	r6, r3, #1792
	ldr	r1, [r3, #1832]
	mov	r4, r3
	str	r1, [r2]
	ldr	r1, [r3, #1836]
	ldr	r2, [r3, #1828]
	str	r1, [r2, #4]
	ldr	r1, [r3, #1840]
	str	r1, [r2, #8]
	ldr	r1, [r3, #1844]
	str	r1, [r2, #12]
	ldr	r1, [r3, #1848]
	str	r1, [r2, #304]
	ldr	r1, [r3, #1852]
	str	r1, [r2, #308]
	ldr	r1, [r3, #1856]
	str	r1, [r2, #336]
	ldr	r1, [r3, #1860]
	str	r1, [r2, #344]
.L382:
	ldrb	r3, [r6, r5, lsl #3]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L381
	uxtb	r0, r5
	bl	FlashReset
.L381:
	adds	r5, r5, #1
	cmp	r5, #4
	bne	.L382
	ldrb	r3, [r4, #2761]	@ zero_extendqisi2
	ldr	r5, .L388
	cbz	r3, .L383
	movs	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #2760]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2760]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #1849]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L383:
	ldr	r3, [r4, #1788]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L389:
	.align	2
.L388:
	.word	.LANCHOR0
	.fnend
	.size	FlashResume, .-FlashResume
	.align	1
	.global	NandCIrqEnable
	.thumb
	.thumb_func
	.type	NandCIrqEnable, %function
NandCIrqEnable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r3, #1
	ldr	r2, [r0, #368]
	lsls	r3, r3, r1
	orrs	r2, r2, r3
	str	r2, [r0, #368]
	ldr	r2, [r0, #364]
	orrs	r3, r3, r2
	str	r3, [r0, #364]
	bx	lr
	.fnend
	.size	NandCIrqEnable, .-NandCIrqEnable
	.align	1
	.global	NandCIrqDisable
	.thumb
	.thumb_func
	.type	NandCIrqDisable, %function
NandCIrqDisable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r3, #1
	ldr	r2, [r0, #368]
	lsls	r3, r3, r1
	orrs	r2, r2, r3
	str	r2, [r0, #368]
	ldr	r2, [r0, #364]
	bic	r3, r2, r3
	str	r3, [r0, #364]
	bx	lr
	.fnend
	.size	NandCIrqDisable, .-NandCIrqDisable
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
	.global	rk_nandc_flash_ready
	.thumb
	.thumb_func
	.type	rk_nandc_flash_ready, %function
rk_nandc_flash_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r1, #1
	b	NandCIrqDisable
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
	mov	r0, r4
	movs	r1, #1
	bl	NandCIrqEnable
	ldr	r3, [r4]
	mov	r0, r4
	lsls	r3, r3, #22
	bmi	.L395
	pop	{r4, lr}
	b	wait_for_nand_flash_ready
.L395:
	movs	r1, #1
	pop	{r4, lr}
	b	NandCIrqDisable
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
	ldr	r4, .L425
	.pad #28
	sub	sp, sp, #28
	mov	r10, r1
	mov	r8, r2
	ldrb	r5, [r4]	@ zero_extendqisi2
	cbz	r5, .L410
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L398
.L410:
	mov	fp, r4
.L397:
	cmp	r5, r8
	bcs	.L423
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
	ldrb	r3, [r4, #1616]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #20]
	cmp	r0, r3
	bcc	.L399
	mov	r3, #-1
	str	r3, [r9, r6]
	b	.L400
.L399:
	ldrb	r3, [fp, #2820]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, lsl #4
	it	eq
	moveq	r7, #0
	ldr	r3, [r3, #1668]
	cbz	r3, .L402
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L402:
	ldr	r1, [sp, #20]
	ldr	r3, [sp, #12]
	lsls	r2, r1, #4
	add	lr, r4, r2
	str	r3, [lr, #1668]
	movs	r3, #0
	str	r3, [lr, #1672]
	ldr	r3, [sp, #16]
	str	r3, [lr, #1664]
	cbz	r7, .L403
	add	r3, r6, #36
	add	r3, r3, r9
	str	r3, [lr, #1672]
.L403:
	add	r1, r1, r4
	add	r2, r2, r4
	ldrb	r6, [r1, #1620]	@ zero_extendqisi2
	mov	r0, r6
	strb	r6, [r2, #1660]
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L404
	ldrb	r3, [fp, #76]	@ zero_extendqisi2
	cbz	r3, .L404
	bl	flash_enter_slc_mode
	b	.L405
.L404:
	bl	flash_exit_slc_mode
.L405:
	ldr	r3, [sp, #20]
	mov	r0, r6
	ldr	r1, [sp, #16]
	add	r5, r5, r7
	add	r3, r4, r3, lsl #2
	ldr	r2, [r3, #1628]
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
.L400:
	adds	r5, r5, #1
	b	.L397
.L423:
	ldr	r0, [r4, #1828]
	movs	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r6, .L425
	addw	r7, r6, #1660
.L407:
	ldrb	r3, [r4, #1616]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L424
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L408
	ldrb	r3, [r6, #76]	@ zero_extendqisi2
	cbz	r3, .L408
	lsls	r3, r5, #4
	ldrb	r0, [r7, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L408:
	adds	r5, r5, #1
	b	.L407
.L424:
	movs	r0, #0
.L398:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L426:
	.align	2
.L425:
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
	movs	r1, #0
	b	NandCIrqDisable
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
	ldr	r3, .L429
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
.L430:
	.align	2
.L429:
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
.L432:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #11
	bpl	.L432
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
	cbnz	r2, .L449
	adds	r2, fp, #0
	it	ne
	movne	r2, #1
	b	.L436
.L449:
	movs	r2, #1
.L436:
	ldr	r5, .L454
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
	ldr	r3, [r5, #2768]
	cmp	r3, #3
	bls	.L437
	ldr	r3, [r6, #16]
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #4
	str	r3, [sp, #12]
	cmp	r2, #0
	beq	.L438
	cbnz	r7, .L439
.L447:
	add	r8, r8, #1
	mov	r0, r6
	bl	rk_nandc_xfer_irq_flag_init
	mov	r0, r6
	movs	r1, #0
	asr	r8, r8, #1
	bl	NandCIrqEnable
	bfi	r4, r8, #22, #6
	cmp	fp, #0
	beq	.L440
	mov	r0, fp
	b	.L441
.L439:
	ldr	r3, [r5, #2816]
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
.L443:
	ldr	r2, [sp, #4]
	cmp	r1, r2
	bcs	.L447
	ldr	r2, [sp, #60]
	lsr	r10, r3, #2
	cbz	r2, .L444
	ldrh	r2, [r0, #2]
	ldrh	lr, [r0], #4
	orr	lr, lr, r2, lsl #16
	ldr	r2, [ip, #2784]
	str	lr, [r2, r10, lsl #2]
	b	.L445
.L444:
	ldr	r2, [ip, #2784]
	mov	lr, r2
	mov	r2, #-1
	str	r2, [lr, r10, lsl #2]
.L445:
	ldr	r2, [sp]
	adds	r1, r1, #1
	add	r3, r3, r2
	b	.L443
.L440:
	ldr	r0, [r5, #2780]
.L441:
	ldr	r3, [r5, #2784]
	ubfx	r8, r4, #22, #5
	mov	r2, r7
	str	r0, [r5, #2788]
	lsl	r1, r8, #10
	str	r3, [r5, #2792]
	bl	rknand_dma_map_single
	mov	r2, r7
	lsl	r1, r8, #7
	clz	r7, r7
	lsrs	r7, r7, #5
	str	r0, [r5, #2796]
	ldr	r0, [r5, #2792]
	bl	rknand_dma_map_single
	movs	r3, #1
	str	r3, [r5, #2804]
	tst	fp, #3
	ldr	r3, [r5, #2796]
	str	r0, [r5, #2800]
	str	r3, [r6, #20]
	ldr	r3, [r5, #2800]
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
.L438:
	ldr	r3, [sp, #12]
	str	r3, [r6, #16]
.L437:
	str	r9, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L455:
	.align	2
.L454:
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
.L457:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bhi	.L459
	lsls	r2, r2, #1
	b	.L457
.L459:
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
	ldr	r3, .L463
	cmp	r0, #23
	it	ls
	movls	r0, #24
	ldrh	r2, [r3, #2828]
	ldrh	r1, [r3, #2838]
	str	r0, [r3, #2824]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #2844]
	strh	r0, [r3, #2836]	@ movhi
	movs	r0, #0
	str	r2, [r3, #2832]
	subs	r2, r1, r2
	str	r2, [r3, #2840]
	bx	lr
.L464:
	.align	2
.L463:
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
	mov	r5, r0
	ldrh	r2, [r5, #14]
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L494
	ldrh	lr, [r0, #8]
	str	r2, [sp]
	addw	r1, r4, #2856
	ldrh	r2, [sp]
	ldrh	r3, [r0, #10]
	ldrh	r0, [r0, #12]
	strh	r2, [r4, #2838]	@ movhi
	movs	r2, #0
	strh	lr, [r4, #2848]	@ movhi
	strh	r3, [r4, #2850]	@ movhi
	strh	r0, [r4, #2852]	@ movhi
.L466:
	strb	r2, [r2, r1]
	adds	r2, r2, #1
	cmp	r2, #32
	bne	.L466
	ldrh	r1, [r5, #14]
	ldrh	r2, [r5, #20]
	cmp	r2, r1, lsr #8
	bcs	.L467
	uxtb	r10, r0
	ldr	r9, .L494+4
	lsl	r2, r10, #1
	uxtb	r2, r2
	str	r2, [sp, #8]
	subs	r2, r3, #1
	muls	r2, r0, r2
	str	r2, [sp, #4]
	movs	r2, #0
.L468:
	cmp	r2, r0
	bcs	.L470
	ldr	r6, [sp, #4]
	rsb	ip, r0, r2
	uxtb	r1, r2
	add	ip, ip, r9
	adds	r6, r2, r6
	add	r6, r9, r6
	str	r6, [sp, #12]
	movs	r6, #0
	mov	r8, r6
.L471:
	cmp	r8, r3
	add	r6, r6, r0
	bcs	.L493
	ldr	r7, [sp, #12]
	add	fp, r1, r10
	strb	r1, [ip, r6]
	add	r8, r8, #1
	strb	fp, [r7, r6]
	ldr	r7, [sp, #8]
	add	r1, r1, r7
	uxtb	r1, r1
	b	.L471
.L493:
	adds	r2, r2, #1
	b	.L468
.L470:
	lsls	r3, r3, #1
	strh	r3, [r4, #2850]	@ movhi
	ldr	r3, [sp]
	lsrs	r7, r3, #1
	strh	r7, [r4, #2838]	@ movhi
.L467:
	cmp	lr, #1
	mov	r3, #5
	strh	r3, [r4, #2888]	@ movhi
	mov	r3, #0
	strh	r3, [r4, #2890]	@ movhi
	it	eq
	ldreq	r3, .L494
	ldrb	r8, [r4]	@ zero_extendqisi2
	it	eq
	strheq	lr, [r3, #2888]	@ movhi
	mov	r3, #4352
	strh	r3, [r4, #2892]	@ movhi
	cmp	r8, #0
	beq	.L473
	ldr	r3, .L494
	mov	r2, #384
	strh	r2, [r3, #2892]	@ movhi
.L473:
	ldrh	r6, [r4, #2838]
	ldrh	r7, [r4, #2850]
	smulbb	r7, r7, r0
	smulbb	r0, r6, r0
	uxth	r7, r7
	strh	r7, [r4, #2828]	@ movhi
	uxth	r0, r0
	strh	r0, [r4, #2894]	@ movhi
	bl	Ftl_log2
	ldrh	r9, [r5, #16]
	ldrh	fp, [r5, #20]
	ldrh	r1, [r5, #18]
	smulbb	r3, r7, r9
	strh	r9, [r4, #2898]	@ movhi
	strh	r1, [r4, #2900]	@ movhi
	strh	fp, [r4, #2904]	@ movhi
	strh	r3, [r4, #2902]	@ movhi
	strh	r0, [r4, #2896]	@ movhi
	mov	r0, fp
	str	r1, [sp]
	bl	Ftl_log2
	lsl	r3, fp, #9
	cmp	r6, #1024
	uxth	r3, r3
	strh	r3, [r4, #2908]	@ movhi
	it	hi
	ldrhi	r2, .L494
	lsr	r3, r3, #8
	strh	r3, [r4, #2910]	@ movhi
	ldrh	r3, [r5, #26]
	strh	r3, [r4, #2912]	@ movhi
	mul	r3, r6, r7
	str	r3, [r4, #2844]
	itt	hi
	uxtbhi	r3, r6
	strhhi	r3, [r2, #2890]	@ movhi
	mov	r10, r0
	ldrh	r5, [r4, #2890]
	ldr	r1, [sp]
	subs	r5, r6, r5
	strh	r0, [r4, #2906]	@ movhi
	ldrh	r0, [r4, #2892]
	muls	r5, r7, r5
	ldr	r3, .L494
	lsls	r0, r0, #3
	mul	r1, r1, fp
	str	r3, [sp]
	mul	r5, fp, r5
	mul	r5, r9, r5
	asrs	r5, r5, #11
	str	r5, [r4, #2916]
	bl	__aeabi_idiv
	ldr	r3, [sp]
	uxth	r0, r0
	cmp	r0, #4
	itet	ls
	movls	r2, #4
	strhhi	r0, [r3, #2920]	@ movhi
	strhls	r2, [r3, #2920]	@ movhi
	cmp	r8, #0
	beq	.L477
	mov	r3, #640
	strh	r3, [r4, #2892]	@ movhi
.L477:
	ldrh	r3, [r4, #2892]
	lsls	r6, r6, #6
	mov	r1, r7
	ldr	r5, .L494
	asr	r3, r3, r10
	add	r10, r10, #9
	asr	r6, r6, r10
	adds	r3, r3, #2
	strh	r3, [r4, #2922]	@ movhi
	uxth	r0, r6
	strh	r6, [r4, #2924]	@ movhi
	add	r6, r0, #8
	mul	r3, r7, r0
	ldrh	r0, [r4, #2920]
	str	r3, [r4, #2928]
	bl	__aeabi_uidiv
	cmp	r7, #1
	ldr	r7, .L494
	uxtah	r0, r6, r0
	it	eq
	addeq	r0, r0, #4
	str	r0, [r5, #2824]
	ldrh	r0, [r4, #2824]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #2824]
	ldr	r5, [r4, #2840]
	mov	r0, #2048
	ldrh	r6, [r4, #2904]
	str	r3, [r4, #2932]
	ldrh	r3, [r4, #2898]
	lsls	r5, r5, #2
	mov	r1, r6
	muls	r5, r3, r5
	ldrh	r3, [r4, #2906]
	adds	r3, r3, #9
	lsrs	r5, r5, r3
	adds	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, #2936]	@ movhi
	bl	__aeabi_idiv
	ldrh	r2, [r4, #2920]
	movs	r3, #0
	str	r3, [r4, #2940]
	adds	r3, r2, #3
	strh	r3, [r4, #2920]	@ movhi
	ldr	r3, [r4, #2928]
	adds	r1, r3, #3
	str	r1, [r4, #2928]
	ldrb	r1, [r4, #76]	@ zero_extendqisi2
	strh	r0, [r4, #2938]	@ movhi
	cbz	r1, .L480
	adds	r2, r2, #4
	adds	r3, r3, #5
	strh	r2, [r7, #2920]	@ movhi
	str	r3, [r7, #2928]
.L480:
	ldrh	r2, [r4, #2836]
	movs	r3, #0
	strh	r3, [r4, #2944]	@ movhi
	movs	r0, #0
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	adds	r3, r3, #52
	add	r5, r3, r5, lsl #2
	cmp	r5, r6, lsl #9
	ittt	cc
	movcc	r2, #1
	ldrcc	r3, .L494
	strhcc	r2, [r3, #2944]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L495:
	.align	2
.L494:
	.word	.LANCHOR0
	.word	.LANCHOR0+2856
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
	ldr	r2, .L503
	ldrh	r3, [r2, #2946]
	cbz	r3, .L502
	ldr	r3, [r2, #2948]
	ldrh	r2, [r2, #2920]
	add	r2, r3, r2, lsl #1
.L498:
	cmp	r3, r2
	beq	.L502
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L498
	movs	r0, #1
	bx	lr
.L502:
	movs	r0, #0
	bx	lr
.L504:
	.align	2
.L503:
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
	ldr	r2, .L513
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r2, #2952]
	cbz	r3, .L512
	ldr	r5, [r2, #2956]
	movs	r6, #36
	movs	r2, #0
.L508:
	mla	r4, r6, r2, r5
	ldr	r4, [r4, #16]
	cmp	r4, r0
	bcc	.L507
	cmp	r4, r1
	bls	.L510
.L507:
	adds	r2, r2, #1
	cmp	r2, r3
	bne	.L508
.L512:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L510:
	movs	r0, #1
	pop	{r4, r5, r6, pc}
.L514:
	.align	2
.L513:
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
	ldr	r3, .L516
	ldr	r0, [r3, #2940]
	bx	lr
.L517:
	.align	2
.L516:
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
	ldr	r3, .L519
	ldr	r0, [r3, #2940]
	bx	lr
.L520:
	.align	2
.L519:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCapacity, .-FtlGetCapacity
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
	ldr	r3, .L522
	ldr	r0, [r3, #2960]
	bx	lr
.L523:
	.align	2
.L522:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLpn, .-FtlGetLpn
	.align	1
	.global	FtlGetCurEraseBlock
	.thumb
	.thumb_func
	.type	FtlGetCurEraseBlock, %function
FtlGetCurEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L525
	ldrh	r2, [r3, #2828]
	ldr	r0, [r3, #2964]
	muls	r0, r2, r0
	bx	lr
.L526:
	.align	2
.L525:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCurEraseBlock, .-FtlGetCurEraseBlock
	.align	1
	.global	FtlGetAllBlockNum
	.thumb
	.thumb_func
	.type	FtlGetAllBlockNum, %function
FtlGetAllBlockNum:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L528
	ldrh	r2, [r3, #2838]
	ldrh	r0, [r3, #2828]
	muls	r0, r2, r0
	bx	lr
.L529:
	.align	2
.L528:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetAllBlockNum, .-FtlGetAllBlockNum
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
	ldr	r5, .L531
	ldrh	r4, [r5, #2894]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, lsl #2
	ldr	r4, [r1, #2996]
	movs	r1, #1
	subs	r3, r6, r3
	uxth	r3, r3
	and	r0, r3, #31
	lsrs	r7, r3, #5
	lsls	r1, r1, r0
	ldr	r0, [r4, r7, lsl #2]
	orrs	r1, r1, r0
	ldr	r0, .L531+4
	str	r1, [r4, r7, lsl #2]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	ldrh	r3, [r5, #2974]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r5, #2974]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L532:
	.align	2
.L531:
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
	ldr	r5, .L534
	ldrh	r6, [r5, #2894]
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
	ldr	r3, [r5, #2996]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L535:
	.align	2
.L534:
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
	.global	FtlBbtMemInit
	.thumb
	.thumb_func
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L538
	movw	r3, #65535
	movs	r1, #255
	movs	r2, #16
	addw	r0, r0, #2980
	strh	r3, [r0, #-12]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	memset
.L539:
	.align	2
.L538:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
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
	ldr	r4, .L548
	ldrh	r6, [r4, #2894]
	ldrh	r3, [r4, #2850]
	mov	r4, r5
	muls	r6, r3, r6
.L541:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L547
	bl	FtlBbmIsBadBlock
	cbz	r0, .L542
	adds	r4, r4, #1
	uxth	r4, r4
.L542:
	adds	r5, r5, #1
	b	.L541
.L547:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L549:
	.align	2
.L548:
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
	ldr	r6, .L551
	mov	r7, r0
	ldrh	r5, [r6, #2852]
	mov	r1, r5
	bl	__aeabi_uidivmod
	mov	r0, r7
	smlabb	r4, r4, r5, r1
	mov	r1, r5
	bl	__aeabi_uidiv
	ldrh	r3, [r6, #2894]
	smlabb	r0, r3, r0, r4
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.L552:
	.align	2
.L551:
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
	ldr	r3, .L554
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r1, [r3, #2894]
	ldrh	r5, [r3, #2852]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r1, r4
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L555:
	.align	2
.L554:
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
	ldr	r4, .L557
	ldrh	r1, [r4, #2894]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #2852]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L558:
	.align	2
.L557:
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
	bls	.L560
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L560:
	subs	r0, r1, r0
	cmp	r0, #-2147483648
	ite	ls
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	1
	.global	FtlFreeSysBlkQueueInit
	.thumb
	.thumb_func
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L563
	mov	r1, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	strh	r0, [r3, #3028]	@ movhi
	addw	r0, r3, #3036
	strh	r4, [r3, #3030]	@ movhi
	strh	r4, [r3, #3032]	@ movhi
	strh	r4, [r3, #3034]	@ movhi
	bl	__memzero
	mov	r0, r4
	pop	{r4, pc}
.L564:
	.align	2
.L563:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
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
	ldr	r3, .L566
	ldrh	r0, [r3, #3034]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L567:
	.align	2
.L566:
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
	ldr	r3, .L569
	ldrh	r0, [r3, #3034]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L570:
	.align	2
.L569:
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
	mov	r6, r0
	ldr	r4, .L580
	ldrh	r3, [r4, #3034]
	cmp	r3, #1024
	beq	.L571
	cbz	r1, .L573
	ldr	r5, .L580+4
	ldr	r3, [r5, #740]
	cbnz	r3, .L573
	bl	P2V_block_in_plane
	movs	r1, #1
	lsls	r3, r6, #10
	mov	r2, r1
	mov	r7, r0
	ldr	r0, [r5, #744]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r5, #748]
	ldrh	r3, [r2, r7, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	ldr	r3, [r5, #752]
	adds	r3, r3, #1
	str	r3, [r5, #752]
.L573:
	ldrh	r3, [r4, #3034]
	adds	r3, r3, #1
	strh	r3, [r4, #3034]	@ movhi
	ldrh	r3, [r4, #3032]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #3032]	@ movhi
	strh	r6, [r2, #3036]	@ movhi
.L571:
	pop	{r3, r4, r5, r6, r7, pc}
.L581:
	.align	2
.L580:
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movs	r4, #0
	ldr	r5, .L594
	ldr	r6, .L594+4
.L583:
	ldrh	r3, [r5, #3034]
	adds	r7, r4, #1
	uxth	r4, r4
	ldr	r0, .L594
	cmp	r3, r4
	bls	.L591
	ldrh	r3, [r5, #3030]
	add	r3, r3, r4
	add	r3, r5, r3, lsl #1
	ldrh	r0, [r3, #3036]
	bl	P2V_block_in_plane
	ldr	r2, [r6, #748]
	ldr	r3, [r6, #756]
	ldrh	r2, [r2, r0, lsl #1]
	str	r2, [r3, r4, lsl #2]
	mov	r4, r7
	b	.L583
.L591:
	ldr	r5, .L594+4
	movs	r1, #0
	mov	ip, r5
.L585:
	ldrh	r8, [r0, #3034]
	add	r3, r8, #-1
	cmp	r1, r3
	bge	.L592
	adds	r4, r1, #1
	ldr	r2, [r5, #756]
	mov	r3, r1
	uxth	r4, r4
	mov	lr, r4
.L586:
	cmp	lr, r8
	bcs	.L593
	ldr	r6, [r2, lr, lsl #2]
	ldr	r7, [r2, r3, lsl #2]
	cmp	r7, r6
	it	hi
	movhi	r3, lr
	add	lr, lr, #1
	uxth	lr, lr
	b	.L586
.L593:
	cmp	r1, r3
	beq	.L589
	ldr	r6, [r2, r1, lsl #2]
	ldr	r7, [r2, r3, lsl #2]
	str	r6, [r2, r3, lsl #2]
	ldr	r2, [ip, #756]
	str	r7, [r2, r1, lsl #2]
	ldrh	r2, [r0, #3030]
	add	r3, r3, r2
	add	r2, r2, r1
	adds	r3, r3, #4
	adds	r2, r2, #4
	add	r3, r0, r3, lsl #1
	add	r2, r0, r2, lsl #1
	ldrh	r7, [r3, #3028]
	ldrh	r1, [r2, #3028]
	strh	r1, [r3, #3028]	@ movhi
	strh	r7, [r2, #3028]	@ movhi
.L589:
	mov	r1, r4
	b	.L585
.L592:
	pop	{r4, r5, r6, r7, r8, pc}
.L595:
	.align	2
.L594:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	1
	.global	remove_from_free_sys_Queue
	.thumb
	.thumb_func
	.type	remove_from_free_sys_Queue, %function
remove_from_free_sys_Queue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L603
	mov	r1, r0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r0, [r3, #3034]
	cbz	r0, .L597
	ldrh	r6, [r3, #3030]
	movs	r4, #0
.L598:
	cmp	r4, r0
	bcs	.L602
	adds	r2, r4, r6
	ldr	r5, .L603
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #3036]
	cmp	r2, r1
	bne	.L599
	ldr	r0, .L603+4
	bl	printk
	ldrh	r3, [r5, #3030]
	movs	r0, #1
	add	r4, r4, r3
	add	r2, r5, r3, lsl #1
	ubfx	r4, r4, #0, #10
	adds	r3, r3, #1
	ldrh	r2, [r2, #3036]
	add	r4, r5, r4, lsl #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r5, #3030]	@ movhi
	ldrh	r3, [r5, #3034]
	strh	r2, [r4, #3036]	@ movhi
	subs	r3, r3, #1
	strh	r3, [r5, #3034]	@ movhi
	pop	{r4, r5, r6, pc}
.L599:
	adds	r4, r4, #1
	b	.L598
.L602:
	movs	r0, #0
.L597:
	pop	{r4, r5, r6, pc}
.L604:
	.align	2
.L603:
	.word	.LANCHOR0
	.word	.LC3
	.fnend
	.size	remove_from_free_sys_Queue, .-remove_from_free_sys_Queue
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
	ldr	r5, .L612
	ldrh	r2, [r5, #3034]
	cbz	r2, .L608
	ldrh	r3, [r5, #3030]
	subs	r2, r2, #1
	ldr	r4, .L612+4
	strh	r2, [r5, #3034]	@ movhi
	add	r1, r5, r3, lsl #1
	adds	r3, r3, #1
	ldr	r8, [r4, #740]
	ubfx	r3, r3, #0, #10
	ldrh	r6, [r1, #3036]
	strh	r3, [r5, #3030]	@ movhi
	cmp	r8, #0
	bne	.L606
	mov	r0, r6
	bl	P2V_block_in_plane
	lsls	r3, r6, #10
	mov	r7, r0
	ldr	r0, [r4, #744]
	str	r3, [r0, #4]
	ldrb	r3, [r5, #76]	@ zero_extendqisi2
	cbz	r3, .L607
	mov	r1, r8
	movs	r2, #1
	bl	FlashEraseBlocks
.L607:
	movs	r1, #1
	ldr	r0, [r4, #744]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r2, [r4, #748]
	ldrh	r3, [r2, r7, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	ldr	r3, [r4, #752]
	adds	r3, r3, #1
	str	r3, [r4, #752]
	b	.L606
.L608:
	movw	r6, #65535
.L606:
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, pc}
.L613:
	.align	2
.L612:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
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
	ldr	r5, .L630
	ldrh	r3, [r5, #2836]
	cmp	r3, r0
	bls	.L616
	movs	r4, #6
	ldr	r2, .L630+4
	movw	r3, #65535
	muls	r4, r0, r4
	ldr	lr, [r2, #760]
	mov	r7, r2
	add	r1, lr, r4
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [lr, r4]	@ movhi
	ldr	r3, [r2, #764]
	cmp	r3, #0
	beq	.L629
	ldr	r8, [r2, #768]
	lsl	fp, r0, #1
	ldrh	r2, [r1, #4]
	ldrh	r6, [r8, r0, lsl #1]
	cbz	r2, .L627
	muls	r6, r2, r6
	b	.L618
.L627:
	mov	r6, #-1
.L618:
	ldr	ip, [r7, #760]
	ldr	r2, .L630+8
	rsb	r9, ip, r3
	ldrh	r5, [r5, #2836]
	asr	r9, r9, #1
	str	r5, [sp]
	mul	r2, r2, r9
	ldr	r9, [r7, #748]
	add	r5, r9, fp
	str	r5, [sp, #4]
	movs	r5, #0
	uxth	r2, r2
.L625:
	adds	r5, r5, #1
	ldr	r7, [sp]
	uxth	r5, r5
	cmp	r5, r7
	bhi	.L616
	cmp	r0, r2
	beq	.L616
	ldrh	r7, [r3, #4]
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	cbz	r7, .L628
	mul	r7, r7, fp
	b	.L620
.L628:
	mov	r7, #-1
.L620:
	cmp	r7, r6
	bne	.L621
	ldr	r7, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L623
	b	.L622
.L621:
	bhi	.L622
.L623:
	ldrh	r7, [r3]
	movw	r10, #65535
	cmp	r7, r10
	bne	.L624
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L630+4
	str	r1, [r3, #772]
	b	.L616
.L624:
	movs	r3, #6
	mov	r2, r7
	mla	r3, r3, r7, ip
	b	.L625
.L622:
	strh	r2, [lr, r4]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L630+4
	ldr	r4, [r2, #764]
	cmp	r3, r4
	bne	.L626
	strh	r0, [r3, #2]	@ movhi
.L629:
	str	r1, [r2, #764]
	b	.L616
.L626:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #760]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L616:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L631:
	.align	2
.L630:
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
	ldr	r2, .L633
	ldrh	r3, [r2, #776]
	adds	r3, r3, #1
	strh	r3, [r2, #776]	@ movhi
	pop	{r3, pc}
.L634:
	.align	2
.L633:
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
	beq	.L636
	movs	r1, #6
	ldr	r2, .L643
	mul	lr, r1, r0
	ldr	ip, [r2, #760]
	mov	r7, r2
	add	r6, ip, lr
	strh	r5, [r6, #2]	@ movhi
	strh	r5, [ip, lr]	@ movhi
	ldr	r3, [r2, #780]
	cbz	r3, .L642
	ldr	r8, [r2, #760]
	ldr	r9, [r2, #748]
	rsb	r4, r8, r3
	ldr	r2, .L643+4
	asrs	r4, r4, #1
	ldrh	r10, [r9, r0, lsl #1]
	muls	r4, r2, r4
	uxth	r2, r4
.L640:
	ldrh	r4, [r9, r2, lsl #1]
	cmp	r4, r10
	bcs	.L638
	ldrh	r4, [r3]
	cmp	r4, r5
	bne	.L639
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L636
.L639:
	mla	r3, r1, r4, r8
	mov	r2, r4
	b	.L640
.L638:
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	strh	r2, [ip, lr]	@ movhi
	ldr	r1, [r7, #780]
	ldr	r2, .L643
	cmp	r3, r1
	bne	.L641
	strh	r0, [r3, #2]	@ movhi
.L642:
	str	r6, [r2, #780]
	b	.L636
.L641:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #760]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L636:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L644:
	.align	2
.L643:
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
	ldr	r2, .L646
	ldrh	r3, [r2, #784]
	adds	r3, r3, #1
	strh	r3, [r2, #784]	@ movhi
	pop	{r3, pc}
.L647:
	.align	2
.L646:
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
	ldr	r7, .L653
	movw	r6, #65535
	muls	r1, r4, r1
	ldr	r3, [r0]
	ldr	r2, [r7, #760]
	adds	r5, r2, r1
	cmp	r5, r3
	ldrh	r3, [r2, r1]
	bne	.L649
	cmp	r3, r6
	ittee	eq
	moveq	r3, #0
	streq	r3, [r0]
	mlane	r3, r4, r3, r2
	strne	r3, [r0]
	it	ne
	strhne	r6, [r3, #2]	@ movhi
	b	.L651
.L649:
	cmp	r3, r6
	ldrh	r0, [r5, #2]
	bne	.L652
	cmp	r0, r3
	beq	.L651
	muls	r0, r4, r0
	strh	r3, [r2, r0]	@ movhi
	b	.L651
.L652:
	mla	r3, r4, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r7, #760]
	muls	r4, r6, r4
	strh	r0, [r3, r4]	@ movhi
.L651:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, pc}
.L654:
	.align	2
.L653:
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
	cbz	r3, .L661
	ldr	r2, .L662
	movs	r6, #6
	movw	r5, #65535
	ldr	r4, [r2, #760]
.L657:
	cbnz	r1, .L658
.L660:
	subs	r4, r3, r4
	ldr	r3, .L662+4
	asrs	r4, r4, #1
	muls	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L658:
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L660
	subs	r1, r1, #1
	mla	r3, r6, r2, r4
	uxth	r1, r1
	b	.L657
.L661:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L663:
	.align	2
.L662:
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
	ldr	r2, .L671
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #764]
	cbz	r3, .L669
	movs	r4, #6
	ldr	r1, [r2, #760]
	movw	r2, #65535
.L666:
	cbz	r0, .L667
	ldrh	r3, [r3]
	cmp	r3, r2
	beq	.L669
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L666
.L669:
	movw	r0, #65535
	pop	{r4, pc}
.L667:
	subs	r3, r3, r1
	ldr	r0, .L671+4
	asrs	r3, r3, #1
	muls	r0, r3, r0
	uxth	r0, r0
	pop	{r4, pc}
.L672:
	.align	2
.L671:
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
	ldr	r4, .L678
	ldrh	r3, [r4, #788]
	cmp	r3, r0
	beq	.L674
	ldrh	r3, [r4, #836]
	cmp	r3, r0
	beq	.L674
	ldrh	r3, [r4, #884]
	cmp	r3, r0
	beq	.L674
	movs	r3, #6
	ldr	r0, [r4, #760]
	ldr	r2, [r4, #764]
	muls	r3, r5, r3
	adds	r1, r0, r3
	cmp	r1, r2
	beq	.L674
	ldr	r6, [r4, #768]
	ldrh	r7, [r1, #4]
	ldrh	r1, [r1, #2]
	ldrh	r2, [r6, r5, lsl #1]
	muls	r2, r7, r2
	movw	r7, #65535
	it	eq
	moveq	r2, #-1
	cmp	r1, r7
	bne	.L676
	ldrh	r3, [r0, r3]
	cmp	r3, r1
	beq	.L674
.L676:
	movs	r3, #6
	muls	r1, r3, r1
	ldr	r3, .L678+4
	asrs	r7, r1, #1
	add	r1, r1, r0
	muls	r3, r7, r3
	ldrh	r1, [r1, #4]
	ldrh	r3, [r6, r3, lsl #1]
	muls	r3, r1, r3
	it	eq
	moveq	r3, #-1
	cmp	r2, r3
	bcs	.L674
	ldr	r0, .L678+8
	mov	r1, r5
	bl	List_remove_node
	ldrh	r3, [r4, #776]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #776]	@ movhi
	bl	INSERT_DATA_LIST
.L674:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L679:
	.align	2
.L678:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+764
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
.L681:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L683
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L681
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r7]	@ movhi
	cbz	r0, .L683
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L683:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
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
	ldr	r3, .L701
	movs	r4, #12
	ldr	r7, .L701+4
	movw	r5, #65535
	ldrh	r2, [r3, #2938]
	ldr	r3, [r7, #932]
.L688:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L699
	adds	r1, r1, #1
	mla	r6, r4, r1, r3
	ldrh	r6, [r6, #-12]
	cmp	r6, r5
	bne	.L688
	b	.L689
.L699:
	movs	r1, #0
	mov	r6, #-2147483648
	mov	lr, #12
	mov	r0, r2
.L691:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L700
	mla	r4, lr, r1, r3
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L692
	cmp	r4, r6
	itt	cc
	movcc	r6, r4
	movcc	r0, r5
.L692:
	adds	r1, r1, #1
	b	.L691
.L700:
	cmp	r0, r2
	bcc	.L689
	ldrh	r7, [r7, #936]
	movs	r1, #0
	mov	r4, #-1
	mov	r0, r2
.L694:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L689
	ldr	r6, [r3, #4]
	cmp	r6, r4
	bcs	.L695
	ldrh	lr, [r3]
	cmp	lr, r7
	itt	ne
	movne	r4, r6
	movne	r0, r5
.L695:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L694
.L689:
	pop	{r4, r5, r6, r7, pc}
.L702:
	.align	2
.L701:
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
	ldr	r1, .L710
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r2, [r1, #938]
	mov	r3, r1
	cmp	r2, #4
	bhi	.L704
	cbnz	r0, .L704
	adds	r2, r2, #1
	strh	r2, [r1, #938]	@ movhi
	pop	{r4, r5, pc}
.L704:
	ldr	r1, .L710+4
	movs	r2, #0
	strh	r2, [r3, #938]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #940]
	ldrh	r1, [r1, #2836]
	ldr	r2, [r3, #768]
	add	r1, r2, r1, lsl #1
.L705:
	cmp	r2, r1
	beq	.L709
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #940]
	addne	r0, r0, r4
	strne	r0, [r3, #940]
	b	.L705
.L709:
	pop	{r4, r5, pc}
.L711:
	.align	2
.L710:
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
	ldr	r2, .L715
	and	r0, r0, #31
	uxth	r3, r3
	cbz	r1, .L713
	ldr	r1, [r2, #944]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	orrs	r2, r2, r0
	str	r2, [r1, r3, lsl #2]
	bx	lr
.L713:
	ldr	r1, [r2, #944]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	bic	r0, r0, r2
	str	r0, [r1, r3, lsl #2]
	bx	lr
.L716:
	.align	2
.L715:
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
	ldr	r3, .L718
	lsrs	r1, r0, #5
	ldr	r2, [r3, #944]
	and	r3, r0, #31
	ldr	r0, [r2, r1, lsl #2]
	lsrs	r0, r0, r3
	and	r0, r0, #1
	bx	lr
.L719:
	.align	2
.L718:
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
	ldr	r3, .L729
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	adds	r4, r2, #7
	ldrh	r5, [r3, #2828]
	movw	r7, #65535
	add	r4, r0, r4, lsl #1
.L721:
	cmp	r2, r5
	bcs	.L727
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	cmp	r6, r7
	uxth	r2, r2
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L721
.L727:
	ldrh	r6, [r3, #2898]
	add	r4, r0, #14
	movs	r2, #0
	movw	r7, #65535
.L724:
	uxth	r3, r2
	cmp	r3, r5
	bcs	.L728
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
	b	.L724
.L728:
	pop	{r4, r5, r6, r7, pc}
.L730:
	.align	2
.L729:
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
	ldr	r6, .L744
	add	r7, r0, #16
	mov	r4, r0
	mov	r5, r3
	addw	r10, r6, #2856
	movw	r9, #65535
	ldrh	r8, [r6, #2828]
	strh	r3, [r0, #4]	@ movhi
	strb	r3, [r0, #7]
.L732:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L743
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	ldrh	r1, [r4]
	bl	V2P_block
	strh	r9, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L733
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L733:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L732
.L743:
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r3, [r6, #2898]
	smulbb	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, .L744+4
	ldr	r2, [r3, #948]
	cbz	r2, .L735
	ldrh	r2, [r4]
	ldr	r3, [r3, #748]
	ldrh	r3, [r3, r2, lsl #1]
	cmp	r3, #59
	itt	ls
	movls	r3, #1
	strbls	r3, [r4, #9]
.L735:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cbz	r3, .L736
	movs	r3, #1
	strb	r3, [r4, #9]
.L736:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L745:
	.align	2
.L744:
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
	ldr	r3, .L755
	mov	r7, r0
	mov	r4, r5
	addw	r6, r3, #2856
	ldrh	r8, [r3, #2828]
	ldrh	r9, [r3, #2898]
.L747:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L754
	mov	r1, r7
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L748
	add	r4, r4, r9
	uxth	r4, r4
.L748:
	adds	r5, r5, #1
	b	.L747
.L754:
	cbz	r4, .L750
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L750:
	ldr	r3, .L755+4
	movs	r2, #6
	movs	r0, #0
	ldr	r3, [r3, #760]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L756:
	.align	2
.L755:
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
	ldr	r2, .L760
	ldr	r0, [r2, #780]
	cbz	r0, .L758
	ldr	r3, [r2, #760]
	subs	r0, r0, r3
	ldr	r3, .L760+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #748]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L758:
	bx	lr
.L761:
	.align	2
.L760:
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
	ldr	r1, .L770
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #780]
	cbz	r3, .L768
	ldrh	r2, [r1, #784]
	movs	r6, #6
	ldr	r4, [r1, #760]
	movw	r7, #65535
	rsb	r2, r2, r2, lsl #3
	subs	r3, r3, r4
	asrs	r2, r2, #3
	asrs	r3, r3, #1
	cmp	r0, r2
	it	gt
	uxthgt	r0, r2
	ldr	r2, .L770+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L765:
	uxth	r5, r2
	cmp	r5, r0
	bcs	.L767
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L769
.L767:
	ldr	r2, [r1, #748]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L769:
	mov	r3, r5
	b	.L765
.L768:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L771:
	.align	2
.L770:
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
	ldr	r8, .L782+240
	add	r4, r6, #12
	ldr	r1, .L782
	.pad #32
	sub	sp, sp, #32
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L782+4
	ldr	r2, [r8, #1864]
	bl	sprintf
	ldr	r1, .L782+8
	ldr	r2, [r8, #2916]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L782+12
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r4, r0
	bne	.L778
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L782+16
	ldr	r7, .L782+20
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L782+24
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L782+28
	ldr	r2, [r8, #2960]
	bl	sprintf
	ldr	r1, .L782+32
	ldr	r2, [r7, #940]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+36
	ldr	r2, [r7, #952]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+40
	ldr	r2, [r7, #956]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+44
	ldr	r2, [r7, #960]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+48
	ldr	r2, [r7, #964]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+52
	ldr	r2, [r7, #968]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+56
	ldr	r2, [r7, #972]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r7, #976]
	ldr	r1, .L782+60
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r7, #980]
	ldr	r1, .L782+64
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+68
	ldr	r2, [r7, #984]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+72
	ldr	r2, [r7, #988]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldr	r1, .L782+76
	ldrh	r2, [r8, #2974]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+80
	ldrh	r2, [r7, #784]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+84
	ldr	r2, [r7, #992]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+88
	ldr	r2, [r7, #996]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+92
	ldr	r2, [r7, #1000]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+96
	ldr	r2, [r7, #752]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+100
	ldr	r2, [r7, #1004]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+104
	ldr	r2, [r7, #1008]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+108
	ldrh	r2, [r7, #1042]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+112
	ldrh	r2, [r7, #1040]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+116
	ldr	r2, [r8, #2940]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+120
	ldr	r2, [r8, #2932]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+124
	ldr	r2, [r8, #2824]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+128
	ldrh	r2, [r8, #3034]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+132
	ldrh	r2, [r8, #2836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+136
	ldrh	r2, [r7, #1060]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+140
	ldr	r2, [r8, #2840]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+144
	ldrh	r2, [r7, #1064]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+148
	ldrh	r2, [r8, #2968]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+152
	ldrh	r2, [r7, #790]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+156
	ldrb	r2, [r7, #794]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+160
	ldrh	r2, [r7, #788]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+164
	ldrb	r2, [r7, #796]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+168
	ldrh	r2, [r7, #792]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r7, #768]
	ldrh	r2, [r7, #788]
	ldr	r1, .L782+172
	ldrh	r2, [r3, r2, lsl #1]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+176
	ldrh	r2, [r7, #838]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+180
	ldrb	r2, [r7, #842]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+184
	ldrh	r2, [r7, #836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+188
	ldrb	r2, [r7, #844]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+192
	ldrh	r2, [r7, #840]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r7, #768]
	ldrh	r2, [r7, #836]
	ldr	r1, .L782+196
	ldrh	r2, [r3, r2, lsl #1]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+200
	ldrh	r2, [r7, #886]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+204
	ldrb	r2, [r7, #890]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+208
	ldrh	r2, [r7, #884]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+212
	ldrb	r2, [r7, #892]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+216
	ldrh	r2, [r7, #888]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+220
	ldrh	r2, [r7, #1078]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+224
	ldrb	r2, [r7, #1082]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+228
	ldrh	r2, [r7, #1076]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+232
	ldrb	r2, [r7, #1084]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L782+236
	ldrh	r2, [r7, #1080]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r7, #1212]
	ldr	r3, [r7, #948]
	ldr	r2, [r7, #1124]
	b	.L783
.L784:
	.align	2
.L782:
	.word	.LC4
	.word	.LC5
	.word	.LC6
	.word	.LANCHOR1
	.word	.LC7
	.word	.LANCHOR2
	.word	.LC8
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
	.word	.LANCHOR0
.L783:
	orr	r2, r3, r2, lsl #8
	str	r1, [sp]
	add	r5, r5, r0
	ldr	r1, [r7, #1204]
	mov	r0, r5
	str	r1, [sp, #4]
	ldr	r3, [r7, #1208]
	ldr	r1, .L785
	bl	sprintf
	ldr	r1, .L785+4
	ldr	r2, [r7, #1200]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L785+8
	ldr	r2, [r7, #1224]
	adds	r4, r5, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L785+12
	ldrh	r2, [r7, #1640]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L785+16
	ldrh	r2, [r7, #1642]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L785+20
	ldr	r2, [r7, #1644]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L785+24
	ldrh	r2, [r7, #1648]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L785+28
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r7, #784]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L785+32
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r7, #1076]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L775
	ldr	r2, [r7, #768]
	mov	r0, r4
	ldr	r1, .L785+36
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
	add	r4, r4, r0
.L775:
	movs	r0, #0
	movs	r5, #0
	bl	List_get_gc_head_node
	mov	r9, #6
	movw	r10, #65535
	uxth	r3, r0
.L777:
	cmp	r3, r10
	beq	.L776
	ldr	r2, [r7, #768]
	mov	r0, r4
	mul	r8, r9, r3
	ldr	r1, .L785+40
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp]
	ldr	r2, [r7, #760]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r7, #748]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r7, #760]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L777
.L776:
	ldr	r2, [r7, #760]
	movs	r5, #0
	ldr	r3, [r7, #780]
	mov	r9, #6
	movw	r10, #65535
	subs	r3, r3, r2
	ldr	r2, .L785+44
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r3, r3
.L779:
	cmp	r3, r10
	beq	.L778
	mul	r8, r9, r3
	ldr	r2, [r7, #760]
	mov	r0, r4
	ldr	r1, .L785+48
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	ldr	r2, [r7, #748]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r7, #760]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L779
.L778:
	subs	r0, r4, r6
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L786:
	.align	2
.L785:
	.word	.LC62
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
	.word	-1431655765
	.word	.LC73
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	1
	.global	rknand_proc_ftlread
	.thumb
	.thumb_func
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r1, .L788
	ldr	r2, .L788+4
	bl	sprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L789:
	.align	2
.L788:
	.word	.LC74
	.word	.LC75
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
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
	ldr	r4, .L818
	ldr	r2, [r4, #1000]
	ldr	r3, [r4, #1008]
	cmp	r2, r3
	bcs	.L791
	ldr	r2, .L818+4
	movs	r3, #0
	ldr	r0, [r4, #748]
	str	r3, [r4, #992]
	ldrh	r1, [r2, #2836]
	mov	r5, r2
.L792:
	cmp	r3, r1
	bcs	.L817
	ldrh	r6, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r2, [r4, #992]
	add	r2, r2, r6
	str	r2, [r4, #992]
	b	.L792
.L817:
	ldr	r6, [r4, #992]
	mov	r0, r6
	bl	__aeabi_uidiv
	ldrh	r1, [r5, #2888]
	str	r0, [r4, #1000]
	ldr	r0, [r4, #996]
	subs	r0, r6, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #992]
	b	.L794
.L791:
	ldr	r3, [r4, #1004]
	cmp	r2, r3
	bls	.L794
	ldr	r0, .L818+4
	adds	r3, r3, #1
	str	r3, [r4, #1004]
	movs	r3, #0
.L796:
	ldrh	r2, [r0, #2836]
	cmp	r3, r2
	bcs	.L794
	ldr	r1, [r4, #748]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L796
.L794:
	ldr	r6, [r4, #1008]
	ldr	r8, [r4, #1000]
	add	r3, r6, #256
	ldr	r2, .L818
	cmp	r3, r8
	bls	.L799
	ldr	r1, [r2, #1004]
	add	r3, r6, #768
	cmp	r3, r1
	bls	.L799
	ldr	r3, [r2, #948]
	cbnz	r3, .L800
.L802:
	movw	r0, #65535
	b	.L801
.L800:
	cmp	r6, #30
	bhi	.L802
.L799:
	ldrh	r0, [r4, #784]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r9, r0
	bcs	.L803
	cmp	r6, #30
	bhi	.L802
.L803:
	ldr	r3, [r4, #764]
	ldr	r2, .L818
	cmp	r3, #0
	beq	.L802
	ldr	r1, .L818+4
	movw	r7, #65535
	ldr	r0, [r2, #760]
	mov	ip, #6
	ldr	r10, [r2, #748]
	mov	r5, r7
	ldrh	r1, [r1, #2836]
	mov	lr, r7
	str	r1, [sp, #20]
	movs	r1, #0
.L804:
	ldrh	r2, [r3]
	cmp	r2, lr
	str	r2, [sp, #16]
	beq	.L806
	adds	r1, r1, #1
	ldr	r2, [sp, #20]
	uxth	r1, r1
	cmp	r1, r2
	bhi	.L802
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L805
	subs	r3, r3, r0
	ldr	r2, .L818+8
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r3, r3
	ldrh	fp, [r10, r3, lsl #1]
	cmp	fp, r6
	bls	.L810
	cmp	fp, r7
	itt	cc
	movcc	r7, fp
	movcc	r5, r3
.L805:
	ldr	r3, [sp, #16]
	mla	r3, ip, r3, r0
	b	.L804
.L810:
	mov	r5, r3
.L806:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L802
	ldrh	fp, [r10, r5, lsl #1]
	lsls	r3, r5, #1
	cmp	fp, r6
	bls	.L808
	str	r3, [sp, #16]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #16]
	cmp	r0, r6
	it	hi
	strhi	r7, [r4, #1008]
.L808:
	cmp	fp, r8
	bcs	.L802
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L802
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L809
	ldr	r2, [r4, #1004]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L802
.L809:
	ldr	r2, [r4, #768]
	mov	r1, r5
	ldr	r0, .L818+12
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	str	r9, [sp, #8]
	str	r3, [sp, #4]
	ldr	r3, [r4, #1004]
	bl	printk
	mov	r0, r5
	movs	r3, #1
	str	r3, [r4, #1652]
.L801:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L819:
	.align	2
.L818:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC76
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
	beq	.L821
	ldr	r2, .L822
	movs	r1, #0
	ldr	r2, [r2, #768]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L821:
	movs	r0, #0
	pop	{r3, pc}
.L823:
	.align	2
.L822:
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
	ldr	r2, .L835
	mov	ip, #36
	ldr	r0, .L835+4
	movs	r3, #0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r7, #12
	mov	lr, #1
	mov	r6, r0
	mov	r4, r2
	str	r3, [r2, #1656]
.L825:
	ldrh	r1, [r0, #2828]
	adds	r5, r3, #1
	uxth	r3, r3
	ldr	r8, .L835+4
	cmp	r3, r1
	bcs	.L833
	mul	r8, r7, r3
	ldr	r9, [r2, #1660]
	add	r1, r9, r8
	str	lr, [r1, #8]
	ldrh	r1, [r0, #2908]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	bic	r10, r1, #3
	ldr	r1, [r4, #1664]
	add	r1, r1, r10
	str	r1, [r9, r8]
	ldr	r10, [r2, #1660]
	ldrh	r1, [r6, #2910]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	add	r9, r10, r8
	bic	fp, r1, #3
	ldr	r1, [r4, #1668]
	add	r1, r1, fp
	str	r1, [r9, #4]
	ldr	r1, [r2, #1672]
	mla	r3, ip, r3, r1
	ldr	r1, [r10, r8]
	str	r1, [r3, #8]
	ldr	r1, [r9, #4]
	str	r1, [r3, #12]
	mov	r3, r5
	b	.L825
.L833:
	ldr	r0, .L835
	mov	lr, #12
	movs	r5, #0
.L829:
	ldr	r3, [r2, #1676]
	cmp	r1, r3
	bcs	.L834
	mul	r7, lr, r1
	ldr	r6, [r0, #1660]
	ldr	r4, [r0, #1664]
	adds	r3, r6, r7
	str	r5, [r3, #8]
	ldrh	r3, [r8, #2908]
	muls	r3, r1, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r4
	str	r3, [r6, r7]
	ldr	r3, [r2, #1660]
	ldr	r6, [r0, #1668]
	add	r7, r7, r3
	ldrh	r3, [r8, #2910]
	muls	r3, r1, r3
	add	r1, r1, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r1, r1
	add	r3, r3, r6
	str	r3, [r7, #4]
	b	.L829
.L834:
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L836:
	.align	2
.L835:
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
	ldr	r3, .L844
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	ldr	lr, [r3, #1676]
	mov	r10, #36
	ldr	r6, [r3, #1660]
	mov	r8, #12
	mov	ip, r5
.L838:
	uxth	r4, r5
	cmp	r4, r1
	bcs	.L837
	mla	r4, r10, r4, r0
	movs	r2, #0
.L839:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L840
	mul	r3, r8, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L839
	str	ip, [r9, #8]
.L840:
	adds	r5, r5, #1
	b	.L838
.L837:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L845:
	.align	2
.L844:
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
	ldr	r3, .L854
	mov	ip, #1
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	ldr	r5, [r3, #1676]
	mov	lr, #12
	ldr	r6, [r3, #1660]
	mov	r8, #36
.L847:
	uxth	r2, r4
	cmp	r2, r1
	bcs	.L853
	mov	r9, #0
.L848:
	uxth	r3, r9
	cmp	r3, r5
	bcs	.L849
	mla	r3, lr, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L848
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	ip, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L849:
	adds	r4, r4, #1
	b	.L847
.L853:
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L855:
	.align	2
.L854:
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
	ldr	r2, .L862
	ldr	r3, [r2, #1680]
	ldrh	r2, [r2, #1684]
	add	r2, r3, r2, lsl #1
.L857:
	cmp	r3, r2
	beq	.L861
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L857
	movs	r0, #1
	bx	lr
.L861:
	movs	r0, #0
	bx	lr
.L863:
	.align	2
.L862:
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
	ldr	r3, .L871
	movs	r4, #0
	ldrh	r1, [r3, #1684]
	ldr	r2, [r3, #1680]
.L865:
	uxth	r5, r4
	cmp	r5, r1
	bcs	.L869
	adds	r4, r4, #1
	add	lr, r2, r4, lsl #1
	ldrh	lr, [lr, #-2]
	cmp	lr, r0
	bne	.L865
.L869:
	cmp	r5, r1
	bne	.L867
	strh	r0, [r2, r5, lsl #1]	@ movhi
	ldrh	r0, [r3, #1684]
	adds	r0, r0, #1
	strh	r0, [r3, #1684]	@ movhi
.L867:
	ldrh	r2, [r3, #1692]
	movs	r0, #12
	muls	r0, r2, r0
	ldr	r2, [r3, #1688]
	adds	r1, r2, r0
	str	r7, [r1, #4]
	str	r8, [r1, #8]
	str	r6, [r2, r0]
	ldrh	r2, [r3, #1692]
	adds	r2, r2, #1
	strh	r2, [r3, #1692]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L872:
	.align	2
.L871:
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
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	mov	r1, r4
	ldr	r0, .L876
	bl	printk
	ldr	r3, .L876+4
	ldrh	r0, [r3, #1694]
	cmp	r0, r4
	beq	.L874
	ldrh	r1, [r3, #1696]
	cmp	r1, r4
	beq	.L874
	movw	r2, #65535
	cmp	r0, r2
	bne	.L875
	strh	r4, [r3, #1694]	@ movhi
	b	.L874
.L875:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #1696]	@ movhi
.L874:
	movs	r0, #0
	pop	{r4, pc}
.L877:
	.align	2
.L876:
	.word	.LC77
	.word	.LANCHOR2
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
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	mov	r1, r4
	ldr	r0, .L881
	bl	printk
	ldr	r3, .L881+4
	ldrh	r0, [r3, #1694]
	cmp	r0, r4
	beq	.L879
	ldrh	r1, [r3, #1696]
	cmp	r1, r4
	beq	.L879
	movw	r2, #65535
	cmp	r0, r2
	bne	.L880
	strh	r4, [r3, #1694]	@ movhi
	b	.L879
.L880:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #1696]	@ movhi
.L879:
	movs	r0, #0
	pop	{r4, pc}
.L882:
	.align	2
.L881:
	.word	.LC77
	.word	.LANCHOR2
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
	ldr	r4, .L892
	mov	r2, r5
	ldrh	r1, [r4, #1698]
	mov	r6, r0
	ldr	r0, .L892+4
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r2, [r4, #948]
	mov	r3, r4
	cbz	r2, .L884
	ldr	r1, [r4, #748]
	ldrh	r2, [r1, r6, lsl #1]
	cmp	r2, #29
	itt	hi
	subhi	r2, r2, #30
	strhhi	r2, [r1, r6, lsl #1]	@ movhi
.L884:
	ldrh	r2, [r3, #1698]
	movs	r1, #0
	ldr	r4, .L892+8
.L885:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L891
	adds	r1, r1, #1
	add	r0, r4, r1, lsl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r5
	bne	.L885
	b	.L886
.L891:
	cmp	r2, #15
	itttt	ls
	addls	r1, r2, #1
	strhls	r1, [r3, #1698]	@ movhi
	addls	r3, r3, r2, lsl #1
	strhls	r5, [r3, #1700]	@ movhi
.L886:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L893:
	.align	2
.L892:
	.word	.LANCHOR2
	.word	.LC78
	.word	.LANCHOR2+1700
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
	ldr	r4, .L900
	ldrh	r3, [r4, #1698]
	cbz	r3, .L895
	ldrh	r1, [r4, #1694]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L895
	ldrh	r2, [r4, #1734]
	cmp	r2, r3
	itt	cs
	movcs	r3, #0
	strhcs	r3, [r4, #1734]	@ movhi
	ldrh	r3, [r4, #1734]
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #1700]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #1734]
	adds	r3, r3, #1
	strh	r3, [r4, #1734]	@ movhi
.L895:
	movs	r0, #0
	pop	{r4, pc}
.L901:
	.align	2
.L900:
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
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	cbz	r2, .L903
	bl	memset
.L903:
	mov	r0, r4
	pop	{r4, pc}
	.fnend
	.size	ftl_memset, .-ftl_memset
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
	ldr	r4, .L1011
	movs	r6, #0
	ldr	r5, .L1011+4
	movw	r3, #65535
	movs	r7, #12
	mov	r8, #36
	strh	r6, [r4, #1736]	@ movhi
	ldrh	r1, [r5, #2904]
	str	r6, [r4, #1740]
	str	r6, [r4, #1124]
	str	r6, [r4, #984]
	str	r6, [r4, #988]
	str	r6, [r4, #972]
	str	r6, [r4, #960]
	str	r6, [r4, #956]
	str	r6, [r4, #964]
	str	r6, [r4, #968]
	str	r6, [r4, #952]
	str	r6, [r4, #992]
	str	r6, [r4, #996]
	str	r6, [r4, #752]
	str	r6, [r4, #1004]
	str	r6, [r4, #1008]
	str	r6, [r4, #1744]
	str	r6, [r4, #1652]
	str	r3, [r4, #1748]
	str	r6, [r4, #1752]
	str	r6, [r4, #1644]
	str	r6, [r5, #2964]
	strh	r3, [r4, #1694]	@ movhi
	strh	r3, [r4, #1696]	@ movhi
	movs	r3, #32
	strh	r6, [r4, #1648]	@ movhi
	strh	r3, [r4, #1640]	@ movhi
	movs	r3, #128
	strh	r6, [r4, #1698]	@ movhi
	strh	r3, [r4, #1642]	@ movhi
	strh	r6, [r4, #1756]	@ movhi
	strh	r6, [r4, #1734]	@ movhi
	bl	__aeabi_idiv
	ldrh	r3, [r5, #2828]
	str	r6, [r5, #2952]
	lsls	r3, r3, #2
	cmp	r0, r3
	str	r0, [r4, #1760]
	ldrh	r0, [r5, #2902]
	it	hi
	strhi	r3, [r4, #1760]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #1680]
	ldrh	r0, [r5, #2902]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #2828]
	mul	r6, r8, r6
	lsl	r9, r6, #3
	str	r0, [r4, #1688]
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
	str	r0, [r4, #744]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1672]
	ldr	r0, [r4, #1760]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #2908]
	ldrh	r3, [r5, #2828]
	lsls	r3, r3, #1
	adds	r3, r3, #1
	str	r3, [r4, #1676]
	str	r0, [r5, #2956]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #756]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1776]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1780]
	ldr	r0, [r4, #1676]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #1664]
	ldr	r0, [r4, #1760]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #1784]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1788]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1792]
	ldr	r0, [r4, #1676]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2828]
	ldrh	r6, [r5, #2910]
	muls	r6, r3, r6
	str	r0, [r4, #1660]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1796]
	lsls	r0, r6, #3
	bl	ftl_malloc
	ldrh	r3, [r5, #2910]
	str	r0, [r4, #1800]
	ldr	r0, [r4, #1676]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2910]
	str	r0, [r4, #1668]
	ldr	r0, [r4, #1760]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #1804]
	ldrh	r0, [r5, #2838]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #1808]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #1812]
	ldrh	r0, [r4, #1808]
	addw	r0, r0, #547
	lsrs	r0, r0, #9
	strh	r0, [r4, #1808]	@ movhi
	lsls	r0, r0, #9
	bl	ftl_malloc
	ldrh	r6, [r5, #2838]
	lsls	r6, r6, #1
	str	r0, [r4, #1816]
	adds	r0, r0, #32
	str	r0, [r4, #748]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1820]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r5, #2928]
	lsls	r6, r6, #1
	str	r0, [r4, #768]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1824]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1828]
	ldrh	r0, [r5, #2838]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #944]
	ldrh	r0, [r5, #2920]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r5, #2948]
	ldrh	r0, [r5, #2920]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #1832]
	ldrh	r0, [r5, #2920]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #1836]
	ldrh	r0, [r5, #2922]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5, #2922]
	movs	r1, #0
	lsls	r2, r2, #2
	str	r0, [r4, #1840]
	bl	ftl_memset
	ldrh	r6, [r5, #2936]
	lsls	r6, r6, #2
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1844]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, .L1011+8
	str	r0, [r4, #1848]
	ldr	r0, [r5, #2928]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #1852]
	ldrh	r0, [r5, #2938]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2938]
	str	r0, [r4, #932]
	ldrh	r0, [r5, #2908]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2838]
	str	r0, [r4, #1856]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2894]
	adds	r3, r3, #31
	asrs	r3, r3, #5
	strh	r3, [r4, #1860]	@ movhi
	str	r0, [r4, #760]
	ldrh	r0, [r5, #2850]
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #1860]
	ldrh	r7, [r5, #2850]
	movs	r3, #1
	lsls	r2, r2, #2
	mov	r1, r2
	str	r0, [r5, #2996]
.L909:
	cmp	r3, r7
	bcs	.L1009
	ldr	r0, [r5, #2996]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r6, #4]!
	b	.L909
.L1009:
	ldr	r2, .L1011+12
	movs	r1, #0
.L911:
	cmp	r3, #8
	beq	.L1010
	add	r0, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r0, #28]
	b	.L911
.L1010:
	ldr	r2, [r4, #1824]
	ldr	r3, .L1011
	cbnz	r2, .L913
.L915:
	ldr	r1, .L1011+16
	ldr	r0, .L1011+20
	bl	printk
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1012:
	.align	2
.L1011:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2996
	.word	.LANCHOR0+2968
	.word	.LANCHOR3
	.word	.LC79
.L913:
	ldr	r2, [r3, #1828]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1844]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1852]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #932]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1856]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #760]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r5, #2996]
	cmp	r2, #0
	beq	.L915
	ldr	r3, [r3, #768]
	cmp	r3, #0
	beq	.L915
	ldr	r2, [r4, #1680]
	ldr	r3, .L1013
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1688]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1764]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1772]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #744]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1672]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1768]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #756]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1776]
	cmp	r2, #0
	beq	.L915
	ldr	r3, [r3, #1780]
	cmp	r3, #0
	beq	.L915
	ldr	r2, [r4, #1664]
	ldr	r3, .L1013
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1788]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1792]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1660]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1796]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1800]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1668]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #748]
	cmp	r2, #0
	beq	.L915
	ldr	r3, [r3, #1812]
	cmp	r3, #0
	beq	.L915
	ldr	r3, .L1013+4
	ldr	r3, [r3, #2948]
	cmp	r3, #0
	beq	.L915
	ldr	r3, .L1013
	ldr	r2, [r3, #1832]
	cmp	r2, #0
	beq	.L915
	ldr	r2, [r3, #1836]
	cmp	r2, #0
	beq	.L915
	ldr	r3, [r3, #1840]
	cmp	r3, #0
	beq	.L915
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1014:
	.align	2
.L1013:
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
	ldr	r3, .L1020
	mov	r0, r1
	mov	r5, r1
	movs	r1, #0
	movs	r6, #1
	movw	r7, #65535
	ldrh	r2, [r3, #1860]
	lsls	r2, r2, #2
	bl	ftl_memset
	subs	r2, r4, #2
	addw	r4, r4, #1022
.L1017:
	ldrh	r3, [r2, #2]!
	cmp	r3, r7
	beq	.L1015
	lsrs	r1, r3, #5
	cmp	r2, r4
	and	r3, r3, #31
	ldr	r0, [r5, r1, lsl #2]
	lsl	r3, r6, r3
	orr	r3, r3, r0
	str	r3, [r5, r1, lsl #2]
	bne	.L1017
.L1015:
	pop	{r3, r4, r5, r6, r7, pc}
.L1021:
	.align	2
.L1020:
	.word	.LANCHOR2
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	1
	.global	ftl_free_no_use_map_blk
	.thumb
	.thumb_func
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldrh	r2, [r0, #10]
	movs	r1, #0
	ldr	r6, [r0, #20]
	ldr	r9, [r0, #12]
	ldr	r5, [r0, #24]
	lsls	r2, r2, #1
	mov	r0, r6
	bl	ftl_memset
	movs	r2, #0
.L1023:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L1036
	ldr	r0, [r5, r3, lsl #2]
	movs	r1, #0
	ubfx	r0, r0, #10, #16
.L1024:
	ldrh	r7, [r4, #10]
	uxth	r3, r1
	cmp	r7, r3
	bls	.L1037
	ldrh	r7, [r9, r3, lsl #1]
	adds	r1, r1, #1
	cmp	r7, r0
	ittt	eq
	ldrheq	r7, [r6, r3, lsl #1]
	addeq	r7, r7, #1
	strheq	r7, [r6, r3, lsl #1]	@ movhi
	b	.L1024
.L1037:
	adds	r2, r2, #1
	b	.L1023
.L1036:
	movs	r7, #0
	ldrh	fp, [r6]
	ldr	r2, .L1039
	mov	r10, r7
.L1028:
	ldrh	r3, [r4, #10]
	uxth	r5, r7
	cmp	r3, r5
	bls	.L1038
	ldrh	r3, [r4]
	cmp	r3, r5
	bne	.L1029
	ldrh	r3, [r2, #2900]
	ldrh	r1, [r4, #2]
	cmp	r1, r3
	it	cc
	strhcc	r3, [r6, r5, lsl #1]	@ movhi
.L1029:
	ldrh	r8, [r6, r5, lsl #1]
	cmp	fp, r8
	itt	hi
	movhi	r10, r5
	movhi	fp, r8
	cmp	r8, #0
	bne	.L1031
	ldrh	r0, [r9, r5, lsl #1]
	cbz	r0, .L1031
	movs	r1, #1
	str	r2, [sp, #4]
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r9, r5, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r2, [sp, #4]
.L1031:
	adds	r7, r7, #1
	b	.L1028
.L1038:
	mov	r0, r10
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1040:
	.align	2
.L1039:
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
	ldr	r4, .L1045
	movs	r7, #0
	ldr	r5, .L1045+4
	mov	r6, r7
	ldr	r2, [r4, #2928]
	mov	r9, r4
	ldr	r0, [r5, #1828]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #2908]
	ldrh	r2, [r4, #2938]
	movs	r1, #255
	ldr	r0, [r5, #1856]
	muls	r2, r3, r2
	bl	ftl_memset
	mov	r3, r5
	movs	r5, #12
	mov	r2, r4
	movw	lr, #65535
.L1042:
	ldrh	ip, [r2, #2938]
	adds	r0, r7, #1
	uxth	r7, r7
	ldr	r1, .L1045
	cmp	ip, r7
	bls	.L1044
	mul	r1, r5, r7
	ldr	ip, [r3, #932]
	add	r8, ip, r1
	str	r6, [r8, #4]
	strh	lr, [ip, r1]	@ movhi
	ldr	r4, [r3, #932]
	add	ip, r4, r1
	ldrh	r1, [r9, #2908]
	muls	r1, r7, r1
	ldr	r7, [r3, #1856]
	bic	r1, r1, #3
	add	r1, r1, r7
	mov	r7, r0
	str	r1, [ip, #8]
	b	.L1042
.L1044:
	ldr	r0, [r1, #2928]
	movw	r2, #65535
	ldrh	r1, [r1, #2936]
	strh	r2, [r3, #1866]	@ movhi
	strh	r0, [r3, #1874]	@ movhi
	movw	r0, #61634
	strh	r1, [r3, #1870]	@ movhi
	ldr	r1, [r3, #1824]
	strh	r0, [r3, #1868]	@ movhi
	ldrh	r0, [r3, #1908]
	str	r1, [r3, #1876]
	ldr	r1, [r3, #1852]
	strh	r2, [r3, #1864]	@ movhi
	strh	r0, [r3, #1872]	@ movhi
	str	r1, [r3, #1880]
	ldr	r1, [r3, #1828]
	strh	r2, [r3, #1904]	@ movhi
	str	r1, [r3, #1884]
	ldr	r1, [r3, #1844]
	str	r1, [r3, #1888]
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1046:
	.align	2
.L1045:
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
	movs	r5, #0
	ldr	r6, .L1048
	movw	r3, #65535
	ldr	r4, .L1048+4
	mov	r1, r5
	ldrh	r2, [r6, #2920]
	ldr	r0, [r6, #2948]
	strh	r3, [r4, #1916]	@ movhi
	mov	r3, #-1
	lsls	r2, r2, #1
	str	r3, [r4, #1928]
	str	r5, [r4, #1912]
	str	r5, [r4, #1920]
	str	r5, [r4, #1924]
	str	r5, [r4, #948]
	strh	r5, [r6, #2946]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r6, #2838]
	mov	r1, r5
	ldr	r0, [r4, #748]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6, #2838]
	mov	r1, r5
	ldr	r0, [r4, #1812]
	lsls	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #1012
	movs	r1, #48
	bl	__memzero
	add	r0, r4, #1128
	mov	r1, #512
	bl	__memzero
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1049:
	.align	2
.L1048:
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
	ldr	r6, .L1061
	movs	r5, #0
	ldr	r4, .L1061+4
	.pad #20
	sub	sp, sp, #20
	movs	r1, #0
	mov	r9, r5
	ldrh	r3, [r6, #2838]
	mov	r10, r5
	ldr	r0, [r4, #760]
	mov	r8, r6
	muls	r2, r3, r2
	bl	ftl_memset
	str	r5, [r4, #780]
	str	r5, [r4, #764]
	str	r5, [r4, #772]
	strh	r5, [r4, #776]	@ movhi
	strh	r5, [r4, #784]	@ movhi
	strh	r5, [r4, #1736]	@ movhi
.L1051:
	uxth	fp, r5
	ldrh	r2, [r8, #2836]
	sxth	r7, fp
	cmp	r7, r2
	bge	.L1058
	ldr	r3, .L1061
	movs	r2, #0
	mov	r6, r2
	ldrh	ip, [r3, #2828]
	ldrh	r3, [r3, #2898]
	str	r3, [sp, #4]
.L1059:
	sxth	r1, r2
	cmp	r1, ip
	bge	.L1060
	add	r1, r1, r8
	str	r2, [sp, #12]
	str	ip, [sp, #8]
	ldrb	r0, [r1, #2856]	@ zero_extendqisi2
	mov	r1, fp
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	ip, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L1052
	ldr	r3, [sp, #4]
	add	r6, r6, r3
	uxth	r6, r6
.L1052:
	adds	r2, r2, #1
	b	.L1059
.L1060:
	cbz	r6, .L1054
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1055
.L1054:
	ldr	r1, [r4, #768]
	sxth	r2, fp
	movw	r0, #65535
	strh	r0, [r1, r2, lsl #1]	@ movhi
.L1055:
	add	r0, r7, r7, lsl #1
	ldr	r1, [r4, #760]
	ldr	r2, .L1061+4
	add	r1, r1, r0, lsl #1
	strh	r6, [r1, #4]	@ movhi
	ldrh	r1, [r4, #788]
	cmp	r7, r1
	beq	.L1056
	ldrh	r1, [r2, #836]
	cmp	r7, r1
	beq	.L1056
	ldrh	r1, [r2, #884]
	cmp	r7, r1
	beq	.L1056
	ldr	r2, [r2, #768]
	ldrh	r2, [r2, r7, lsl #1]
	cbnz	r2, .L1057
	add	r9, r9, #1
	mov	r0, fp
	uxth	r9, r9
	bl	INSERT_FREE_LIST
	b	.L1056
.L1057:
	add	r10, r10, #1
	mov	r0, fp
	uxth	r10, r10
	bl	INSERT_DATA_LIST
.L1056:
	adds	r5, r5, #1
	b	.L1051
.L1058:
	movs	r0, #0
	strh	r10, [r4, #776]	@ movhi
	strh	r9, [r4, #784]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1062:
	.align	2
.L1061:
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
	ldr	r5, .L1064
	movs	r3, #0
	ldr	r4, .L1064+4
	ldrh	r2, [r5, #2902]
	ldr	r0, [r4, #1680]
	strh	r3, [r4, #1684]	@ movhi
	lsls	r2, r2, #1
	strh	r3, [r4, #1692]	@ movhi
	bl	ftl_memset
	ldrh	r3, [r5, #2902]
	movs	r2, #12
	ldr	r0, [r4, #1688]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r3, r4, r5, lr}
	b	FtlGcBufInit
.L1065:
	.align	2
.L1064:
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
	ldr	r1, .L1068
	bl	ftl_memcpy
	movs	r0, #0
	pop	{r3, pc}
.L1069:
	.align	2
.L1068:
	.word	.LANCHOR2+1932
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r4, #0
	ldr	r6, .L1082
.L1074:
	mov	r0, r6
	ldr	r1, .L1082+4
	ldrb	r2, [r6, #-1]	@ zero_extendqisi2
	bl	FlashMemCmp8
	mov	r5, r0
	cbnz	r0, .L1071
	ldr	r3, .L1082+8
	lsls	r4, r4, #5
	add	r2, r3, #508
	adds	r6, r2, r4
	beq	.L1077
	add	r4, r4, r3
	addw	r3, r3, #2812
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	mov	r4, r0
	b	.L1076
.L1071:
	adds	r4, r4, #1
	adds	r6, r6, #32
	cmp	r4, #72
	bne	.L1074
	b	.L1077
.L1081:
	adds	r4, r4, #1
	cmp	r4, #4
	beq	.L1075
.L1076:
	lsls	r1, r4, #5
	ldrb	r1, [r1, r3]	@ zero_extendqisi2
	cmp	r1, r2
	bne	.L1081
.L1075:
	ldr	r1, .L1082+12
	movs	r2, #32
	ldr	r0, .L1082+16
	add	r1, r1, r4, lsl #5
	bl	ftl_memcpy
	mov	r1, r6
	movs	r2, #32
	ldr	r0, .L1082+20
	bl	ftl_memcpy
	b	.L1072
.L1077:
	mov	r5, #-1
.L1072:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1083:
	.align	2
.L1082:
	.word	.LANCHOR1+509
	.word	.LANCHOR0+1792
	.word	.LANCHOR1
	.word	.LANCHOR1+2812
	.word	.LANCHOR0+44
	.word	.LANCHOR1+256
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
	bne	.L1085
	cbz	r3, .L1086
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1086:
	cbz	r6, .L1084
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
.L1085:
	cbz	r3, .L1089
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1089:
	cbz	r6, .L1084
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
.L1084:
	pop	{r4, r5, r6, pc}
	.fnend
	.size	NandcCopy1KB, .-NandcCopy1KB
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
	ldr	r4, .L1104
	ldr	r4, [r4, #3980]
	add	r4, r4, #4096
	add	r1, r1, r4
	cbz	r2, .L1103
	mov	r0, r1
	mov	r1, r5
.L1103:
	mov	r2, r3
	pop	{r4, r5, lr}
	b	ftl_memcpy
.L1105:
	.align	2
.L1104:
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
	.global	rk_nand_de_init
	.thumb
	.thumb_func
	.type	rk_nand_de_init, %function
rk_nand_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	FlashDeInit
	.fnend
	.size	rk_nand_de_init, .-rk_nand_de_init
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
	b	FlashSuspend
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
	b	FlashResume
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
	ldr	r3, .L1111
	ldr	r0, [r3, #2940]
	bx	lr
.L1112:
	.align	2
.L1111:
	.word	.LANCHOR0
	.fnend
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
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
.L1114:
	cmp	r4, r8
	beq	.L1122
	cbnz	r5, .L1115
	ldr	r0, .L1123
	mov	r1, r9
	mov	r2, r4
	bl	printk
.L1115:
	cmp	r7, #4
	ldr	r0, .L1123+4
	bne	.L1116
	ldr	r1, [r6, r4, lsl #2]
	b	.L1121
.L1116:
	cmp	r7, #2
	ite	eq
	ldrsheq	r1, [r6, r4, lsl #1]
	ldrbne	r1, [r6, r4]	@ zero_extendqisi2
.L1121:
	adds	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1119
	ldr	r0, .L1123+8
	movs	r5, #0
	ldr	r1, .L1123+12
	bl	printk
.L1119:
	adds	r4, r4, #1
	b	.L1114
.L1122:
	ldr	r0, .L1123+8
	ldr	r1, .L1123+12
	pop	{r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1124:
	.align	2
.L1123:
	.word	.LC80
	.word	.LC81
	.word	.LC74
	.word	.LC82
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
	ldr	r5, .L1217
	mov	r3, #172
	mov	r1, #173
	mov	r2, #174
	.pad #44
	sub	sp, sp, #44
	mov	r4, r0
	strb	r3, [r5, #1906]
	mov	r3, #175
	strb	r0, [r5, #1902]
	strb	r1, [r5, #1907]
	strb	r2, [r5, #1908]
	strb	r3, [r5, #1909]
	bne	.L1126
	movs	r3, #167
	strb	r3, [r5, #1906]
	ldr	r3, .L1217+4
	movs	r2, #247
	strb	r2, [r3, #2957]
	b	.L1179
.L1126:
	cmp	r0, #3
	bne	.L1128
	movs	r3, #176
	strb	r3, [r5, #1906]
	movs	r3, #177
	strb	r3, [r5, #1907]
	movs	r3, #178
	strb	r3, [r5, #1908]
	movs	r3, #179
	strb	r3, [r5, #1909]
	movs	r3, #180
	strb	r3, [r5, #1910]
	movs	r3, #181
	strb	r3, [r5, #1911]
	movs	r3, #182
	strb	r3, [r5, #1912]
	movs	r3, #183
	b	.L1212
.L1128:
	cmp	r0, #4
	bne	.L1129
	movs	r0, #204
	strb	r1, [r5, #1911]
	strb	r0, [r5, #1906]
	movs	r0, #191
	strb	r2, [r5, #1912]
	strb	r0, [r5, #1907]
	movs	r0, #170
	strb	r0, [r5, #1908]
	movs	r0, #171
	strb	r0, [r5, #1909]
	movs	r0, #205
	strb	r0, [r5, #1910]
.L1212:
	mov	fp, #8
	strb	r3, [r5, #1913]
	mov	r9, fp
	b	.L1127
.L1129:
	cmp	r0, #5
	bne	.L1130
	movs	r3, #56
	mov	fp, #8
	strb	r3, [r5, #1906]
	movs	r3, #57
	strb	r3, [r5, #1907]
	movs	r3, #58
	strb	r3, [r5, #1908]
	movs	r3, #59
	strb	r3, [r5, #1909]
	b	.L1211
.L1130:
	cmp	r0, #6
	bne	.L1131
	movs	r3, #14
	mov	fp, #12
	strb	r3, [r5, #1906]
	movs	r3, #15
	strb	r3, [r5, #1907]
	movs	r3, #16
	strb	r3, [r5, #1908]
	movs	r3, #17
	strb	r3, [r5, #1909]
	b	.L1211
.L1131:
	cmp	r0, #7
	bne	.L1179
	movs	r3, #176
	mov	fp, #12
	strb	r3, [r5, #1906]
	mov	r9, #10
	movs	r3, #177
	strb	r3, [r5, #1907]
	movs	r3, #178
	strb	r3, [r5, #1908]
	movs	r3, #179
	strb	r3, [r5, #1909]
	movs	r3, #180
	strb	r3, [r5, #1910]
	movs	r3, #181
	strb	r3, [r5, #1911]
	movs	r3, #182
	strb	r3, [r5, #1912]
	movs	r3, #183
	strb	r3, [r5, #1913]
	movs	r3, #212
	strb	r3, [r5, #1914]
	movs	r3, #213
	strb	r3, [r5, #1915]
	b	.L1127
.L1179:
	mov	fp, #7
.L1211:
	mov	r9, #4
.L1127:
	subs	r3, r4, #1
	cmp	r3, #1
	bhi	.L1208
	ldr	r7, .L1217+8
	mov	r8, #0
.L1132:
	ldrb	r3, [r5, #1616]	@ zero_extendqisi2
	uxtb	r6, r8
	ldr	r2, .L1217
	cmp	r3, r6
	bls	.L1139
	add	r6, r6, r2
	ldrb	r3, [r6, #1620]	@ zero_extendqisi2
	add	r4, r2, r3, lsl #6
	add	r3, r2, r3, lsl #3
	addw	r4, r4, #1922
	addw	r2, r2, #1905
	ldrb	r6, [r3, #16]	@ zero_extendqisi2
	ldr	r1, [r3, #12]
	mov	r10, r4
	mov	r3, r4
	add	r6, r1, r6, lsl #8
	addw	ip, r6, #2056
.L1134:
	str	r3, [sp, #8]
	movs	r3, #55
	str	r3, [ip]
	movs	r0, #80
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	str	ip, [sp, #4]
	str	r1, [r6, #2052]
	str	r2, [sp]
	str	r2, [sp, #12]
	bl	NandcDelayns
	ldr	r1, [r6, #2048]
	ldr	r3, [sp, #8]
	ldmia	sp, {r2, ip}
	strb	r1, [r3], #1
	subs	r1, r3, r4
	uxtb	r1, r1
	cmp	r1, r9
	bcc	.L1134
	movs	r2, #0
.L1135:
	movs	r3, #1
	adds	r0, r7, r2
.L1136:
	ldrb	r6, [r0, r3, lsl #2]	@ zero_extendqisi2
	ldrb	r1, [r10]	@ zero_extendqisi2
	add	r1, r1, r6
	strb	r1, [r10, r3, lsl #3]
	adds	r3, r3, #1
	cmp	r3, #7
	bne	.L1136
	adds	r2, r2, #1
	add	r10, r10, #1
	cmp	r2, #4
	bne	.L1135
	movs	r3, #0
	add	r8, r8, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1132
.L1208:
	subs	r3, r4, #3
	cmp	r3, #4
	bhi	.L1139
	smulbb	r2, r9, fp
	lsls	r3, r2, #4
	str	r3, [sp, #32]
	and	r3, r2, #252
	str	r3, [sp]
	movs	r3, #0
.L1215:
	str	r3, [sp, #12]
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	str	r3, [sp, #8]
	ldr	r3, .L1217
	ldr	r2, [sp, #8]
	ldrb	r3, [r3, #1616]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1178
.L1139:
	ldr	r3, .L1217
	strb	r9, [r3, #1903]
	strb	fp, [r3, #1904]
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1178:
	ldr	r2, [sp, #8]
	ldr	r3, .L1217
	add	r3, r3, r2
	movs	r2, #255
	ldrb	r8, [r3, #1620]	@ zero_extendqisi2
	ldr	r3, .L1217
	add	r3, r3, r8, lsl #3
	mov	r0, r8
	ldr	r10, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	lsls	r6, r3, #8
	str	r3, [sp, #4]
	add	r3, r10, r6
	str	r2, [r3, #2056]
	bl	NandcWaitFlashReady
	cmp	r4, #7
	it	eq
	moveq	lr, #160
	mov	r2, #54
	itete	eq
	ldreq	r3, .L1217+12
	ldrne	r3, .L1217+12
	mlaeq	lr, lr, r8, r3
	addne	lr, r3, r8, lsl #6
	ite	ne
	addne	r3, lr, #20
	addeq	r3, lr, #28
	str	r3, [sp, #16]
	ldr	r3, [sp, #4]
	cmp	r4, #4
	add	r3, r10, r3, lsl #8
	str	r2, [r3, #2056]
	bne	.L1143
	add	r6, r6, r10
	movs	r3, #255
	str	r3, [r6, #2052]
	movs	r3, #64
	str	r3, [r6, #2048]
	movs	r3, #204
	b	.L1214
.L1143:
	subs	r2, r4, #5
	cmp	r2, #1
	bhi	.L1145
	ldr	r2, .L1217
	ldrb	r2, [r2, #1906]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	movs	r2, #82
	str	r2, [r3, #2048]
	b	.L1144
.L1145:
	add	r6, r6, r10
	movs	r3, #174
	str	r3, [r6, #2052]
	movs	r3, #0
	str	r3, [r6, #2048]
	movs	r3, #176
.L1214:
	str	r3, [r6, #2052]
	movs	r3, #77
	str	r3, [r6, #2048]
.L1144:
	ldr	r3, [sp, #4]
	cmp	r4, #6
	mov	r2, #48
	mov	r0, r8
	add	r5, r10, r3, lsl #8
	mov	r3, #22
	str	r3, [r5, #2056]
	mov	r3, #23
	str	r3, [r5, #2056]
	mov	r3, #4
	str	r3, [r5, #2056]
	mov	r3, #25
	str	r3, [r5, #2056]
	mov	r3, #0
	str	r3, [r5, #2056]
	str	r3, [r5, #2052]
	str	r3, [r5, #2052]
	it	eq
	moveq	r3, #31
	str	r3, [r5, #2052]
	movs	r3, #2
	str	r3, [r5, #2052]
	movs	r3, #0
	str	r3, [r5, #2052]
	ldr	r3, [sp, #4]
	add	r3, r10, r3, lsl #8
	str	r2, [r3, #2056]
	bl	NandcWaitFlashReady
	subs	r3, r4, #5
	cmp	r3, #1
	str	r3, [sp, #20]
	bls	.L1181
	cmp	r4, #7
	ite	eq
	moveq	r0, #32
	movne	r0, #2
	b	.L1149
.L1218:
	.align	2
.L1217:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+2940
	.word	.LANCHOR0+1902
.L1181:
	movs	r0, #16
.L1149:
	ldr	r3, .L1219
	ldr	r3, [r3, #3984]
	mov	r1, r3
.L1150:
	ldr	r2, [r5, #2048]
	strb	r2, [r1], #1
	subs	r2, r1, r3
	uxtb	r2, r2
	cmp	r2, r0
	bcc	.L1150
	cmp	r4, #7
	bne	.L1151
	movs	r2, #0
.L1153:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #12
	beq	.L1152
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #10
	beq	.L1152
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1153
	b	.L1154
.L1152:
	cmp	r1, #7
	bne	.L1155
.L1154:
	ldr	r0, .L1219+4
	movs	r1, #0
	bl	printk
.L1156:
	b	.L1156
.L1151:
	cmp	r4, #6
	bne	.L1155
	subs	r2, r3, #1
	adds	r3, r3, #7
.L1157:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1155
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1155
	cmp	r2, r3
	bne	.L1157
	ldr	r0, .L1219+4
	movs	r1, #0
	bl	printk
.L1159:
	b	.L1159
.L1155:
	ldr	r1, .L1219
	ldr	r0, [sp, #32]
	ldr	r2, [r1, #3984]
	adds	r0, r2, r0
	mov	r3, r2
.L1160:
	cmp	r3, r0
	beq	.L1216
	ldr	r6, [r5, #2048]
	strb	r6, [r3], #1
	b	.L1160
.L1216:
	ldr	r3, [sp]
	ldr	r0, [r1, #3984]
	add	r0, r0, r3
	movs	r3, #8
.L1163:
	ldr	r1, [sp]
	adds	r6, r0, r1
.L1162:
	ldr	r1, [r0]
	mvns	r1, r1
	str	r1, [r0], #4
	cmp	r0, r6
	bne	.L1162
	ldr	r1, [sp]
	subs	r3, r3, #1
	add	r0, r0, r1
	bne	.L1163
.L1164:
	ldr	r1, .L1219
	movs	r0, #0
	ldr	r1, [r1, #3984]
	str	r1, [sp, #24]
	mov	r1, r0
	ldr	r6, [sp, #24]
	add	r6, r6, r3
	str	r6, [sp, #36]
.L1167:
	movs	r6, #1
	movs	r7, #16
	str	r7, [sp, #28]
	lsl	lr, r6, r1
	movs	r6, #0
	mov	ip, r6
.L1165:
	ldr	r7, [sp, #36]
	ldr	r7, [r7, r6]
	and	r7, lr, r7
	cmp	r7, lr
	it	eq
	addeq	ip, ip, #1
	ldr	r7, [sp]
	add	r6, r6, r7
	ldr	r7, [sp, #28]
	subs	r7, r7, #1
	str	r7, [sp, #28]
	bne	.L1165
	cmp	ip, #8
	add	r1, r1, #1
	it	hi
	orrhi	r0, r0, lr
	cmp	r1, #32
	bne	.L1167
	ldr	r1, [sp, #24]
	str	r0, [r1, r3]
	adds	r3, r3, #4
	ldr	r1, [sp]
	cmp	r3, r1
	bne	.L1164
	ldr	r3, .L1219
	ldr	r1, [r3, #3984]
	movs	r3, #0
	subs	r0, r1, #4
	add	r6, r1, #28
.L1169:
	ldr	r7, [r0, #4]!
	cmp	r7, #0
	it	eq
	addeq	r3, r3, #1
	cmp	r0, r6
	bne	.L1169
	cmp	r3, #7
	ble	.L1170
	ldr	r0, .L1219+8
	movs	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1219+4
	movs	r1, #0
	bl	printk
.L1171:
	b	.L1171
.L1170:
	cmp	r4, #6
	beq	.L1183
	cmp	r4, #7
	ite	ne
	movne	ip, #8
	moveq	ip, #10
	b	.L1172
.L1183:
	mov	ip, #4
.L1172:
	add	r7, r9, #-1
	ldr	r1, [sp, #16]
	movs	r0, #0
	uxtb	r7, r7
	adds	r7, r7, #1
.L1173:
	mov	lr, r1
	mov	r3, r2
.L1174:
	ldrb	r6, [r3], #1	@ zero_extendqisi2
	strb	r6, [lr], #1
	subs	r6, r3, r2
	uxtb	r6, r6
	cmp	r6, r9
	bcc	.L1174
	adds	r0, r0, #1
	add	r2, r2, r7
	cmp	r0, fp
	add	r1, r1, ip
	blt	.L1173
	ldr	r3, [sp, #4]
	mov	r0, r8
	add	r6, r10, r3, lsl #8
	movs	r3, #255
	str	r3, [r6, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #20]
	cmp	r3, #1
	bhi	.L1176
	movs	r3, #54
	str	r3, [r6, #2056]
	ldr	r3, .L1219+12
	mov	r1, #-1
	ldr	r0, [sp, #8]
	ldrb	r3, [r3, #1906]	@ zero_extendqisi2
	str	r3, [r5, #2052]
	movs	r3, #0
	str	r3, [r5, #2048]
	movs	r3, #22
	str	r3, [r6, #2056]
	bl	FlashReadCmd
	b	.L1177
.L1176:
	movs	r3, #56
	str	r3, [r6, #2056]
.L1177:
	mov	r0, r8
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	b	.L1215
.L1220:
	.align	2
.L1219:
	.word	.LANCHOR2
	.word	.LC83
	.word	.LC84
	.word	.LANCHOR0
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
	beq	.L1221
	subs	r2, r0, #1
	cmp	r2, #6
	bhi	.L1223
	b	HynixGetReadRetryDefault
.L1223:
	cmp	r0, #49
	bne	.L1224
	ldr	r0, .L1244
	movs	r2, #64
	ldr	r1, .L1244+4
	addw	r0, r0, #1906
	strb	r3, [r0, #-4]
	movs	r3, #4
	strb	r3, [r0, #-3]
	movs	r3, #15
	strb	r3, [r0, #-2]
	b	.L1242
.L1224:
	cmp	r0, #33
	beq	.L1225
	sub	r2, r0, #65
	cmp	r2, #1
	bhi	.L1226
.L1225:
	ldr	r0, .L1244
	strb	r3, [r0, #1902]
	movs	r3, #4
	b	.L1243
.L1226:
	cmp	r0, #34
	beq	.L1227
	cmp	r0, #67
	bne	.L1228
.L1227:
	ldr	r0, .L1244
	strb	r3, [r0, #1902]
	movs	r3, #5
.L1243:
	strb	r3, [r0, #1903]
	movs	r2, #45
	movs	r3, #7
	addw	r0, r0, #1906
	strb	r3, [r0, #-2]
	ldr	r1, .L1244+8
	b	.L1242
.L1228:
	cmp	r0, #35
	beq	.L1229
	cmp	r0, #68
	bne	.L1221
.L1229:
	ldr	r0, .L1244
	movs	r2, #95
	ldr	r1, .L1244+12
	addw	r0, r0, #1906
	strb	r3, [r0, #-4]
	movs	r3, #5
	strb	r3, [r0, #-3]
	movs	r3, #17
	strb	r3, [r0, #-2]
.L1242:
	b	ftl_memcpy
.L1221:
	bx	lr
.L1245:
	.align	2
.L1244:
	.word	.LANCHOR0
	.word	.LANCHOR1+440
	.word	.LANCHOR1+288
	.word	.LANCHOR1+336
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
	ldr	r5, .L1285
	add	r0, r5, r0, lsl #3
	ldr	r3, [r5, #2768]
	ldr	r4, [r0, #12]
	cmp	r3, #3
	bls	.L1277
	ldr	r3, [r4, #16]
	lsls	r6, r3, #29
	bpl	.L1277
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	str	r3, [sp]
	cmp	r6, #0
	beq	.L1259
	movs	r6, #0
.L1249:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1257
	ldr	r3, [r5, #2768]
	cmp	r3, #5
	bhi	.L1250
.L1253:
	adds	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1249
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1285+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1285+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1249
.L1250:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r0, r3, #18
	bpl	.L1253
	ldr	r3, [sp, #4]
	lsls	r1, r3, #14
	bpl	.L1253
.L1257:
	ldr	r3, [r5, #2804]
	ldr	r4, .L1285
	cmp	r3, #0
	beq	.L1258
	ldr	r1, [sp]
	movs	r2, #0
	ldr	r0, [r4, #2796]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #2800]
	movs	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
	b	.L1258
.L1261:
	ldr	r3, [r4, #8]
	adds	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cbnz	r3, .L1259
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1285+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1285+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1259:
	ldr	r3, [sp]
	lsls	r2, r3, #11
	bpl	.L1261
	ldr	r3, [r5, #2812]
	cbz	r3, .L1262
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1262:
	ldr	r3, [r5, #2804]
	ldr	r6, .L1285
	cbz	r3, .L1263
	ldr	r1, [sp]
	movs	r2, #1
	ldr	r0, [r6, #2796]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #2800]
	movs	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
.L1263:
	ldr	r3, [r5, #2812]
	cbz	r3, .L1258
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1258:
	movs	r3, #0
	str	r3, [r5, #2804]
	b	.L1246
.L1277:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	lsls	r3, r3, #11
	bpl	.L1277
.L1246:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, pc}
.L1286:
	.align	2
.L1285:
	.word	.LANCHOR0
	.word	.LC85
	.word	.LC86
	.word	.LC87
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
	mov	r9, r3
	ldr	r5, .L1331
	tst	r9, #63
	.pad #92
	sub	sp, sp, #92
	mov	r8, r0
	add	r3, r5, r0, lsl #3
	mov	r10, r1
	mov	r6, r2
	ldr	r7, [r3, #12]
	bne	.L1288
	ldr	r3, [sp, #128]
	cbnz	r3, .L1289
	add	r0, sp, #24
	movs	r1, #255
	movs	r2, #64
	bl	memset
	add	r3, sp, #24
	str	r3, [sp, #128]
.L1289:
	ldr	r3, [sp, #128]
	mov	r0, r8
	str	r9, [sp]
	mov	r1, r10
	mov	r2, r6
	str	r3, [sp, #4]
	movs	r3, #0
	bl	NandcXferStart
	mov	r0, r8
	bl	NandcXferComp
	cmp	r10, #0
	bne	.L1315
	ldr	r3, [r5, #2816]
	lsrs	r4, r6, #1
	mov	r2, r10
	mov	r1, r10
	cmp	r3, #25
	ldr	r3, [sp, #128]
	ite	cc
	movcc	lr, #64
	movcs	lr, #128
.L1292:
	cmp	r1, r4
	add	r3, r3, #4
	add	ip, r2, lr
	bcs	.L1329
	ldr	r0, [r5, #2784]
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
	b	.L1292
.L1329:
	movs	r2, #0
	ldr	r0, [r5, #2816]
	ldr	r1, [r5, #2768]
	lsrs	r6, r6, #2
	mov	fp, r2
.L1294:
	cmp	r2, r6
	bcs	.L1290
	cmp	r0, #0
	beq	.L1290
	add	r3, r2, #8
	ldr	r3, [r7, r3, lsl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	lsls	r4, r3, #29
	bmi	.L1318
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1318
	cmp	r1, #5
	bls	.L1296
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
	b	.L1328
.L1296:
	cmp	r1, #3
	bls	.L1298
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
.L1328:
	orr	r4, r3, r4, lsl #5
.L1298:
	cmp	fp, r4
	it	cc
	movcc	fp, r4
	b	.L1295
.L1318:
	mov	fp, #-1
.L1295:
	adds	r2, r2, #1
	b	.L1294
.L1315:
	mov	fp, #0
.L1290:
	movs	r3, #0
	str	r3, [r7, #16]
	b	.L1301
.L1288:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1327
	mov	fp, r4
.L1302:
	cmp	r4, r6
	bcs	.L1330
	and	ip, r4, #3
	cmp	r9, #0
	beq	.L1321
	add	r3, r9, r4, lsl #9
	b	.L1304
.L1321:
	mov	r3, r9
.L1304:
	ldr	r2, [sp, #128]
	mov	r0, r7
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
	mov	r0, r8
	movs	r1, #1
	movs	r2, #2
	ldr	ip, [sp, #8]
	str	fp, [sp]
	str	fp, [sp, #4]
	mov	r3, ip
	bl	NandcXferStart
	mov	r0, r8
	bl	NandcXferComp
	b	.L1302
.L1330:
	mov	fp, #0
	b	.L1301
.L1327:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	movs	r2, #2
	mov	r3, r4
	mov	fp, r4
	bl	NandcXferStart
	str	r9, [sp, #8]
.L1307:
	cmp	r4, r6
	bcs	.L1301
	mov	r0, r8
	bl	NandcXferComp
	ldr	r3, [r7, #32]
	add	ip, r4, #2
	cmp	ip, r6
	str	r3, [sp, #20]
	bcs	.L1308
	movs	r3, #0
	mov	r0, r8
	str	r3, [sp]
	movs	r2, #2
	str	r3, [sp, #4]
	mov	r1, r3
	and	r3, ip, #3
	str	ip, [sp, #12]
	bl	NandcXferStart
	ldr	ip, [sp, #12]
.L1308:
	ldr	r3, [sp, #20]
	lsls	r3, r3, #29
	bmi	.L1323
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, lsl #5
	cmp	fp, r3
	it	cc
	movcc	fp, r3
	b	.L1309
.L1323:
	mov	fp, #-1
.L1309:
	ldr	r1, [sp, #128]
	cmp	r9, #0
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
	mov	r0, r7
	movs	r1, #0
	str	r4, [sp]
	bl	NandcCopy1KB
	ldr	ip, [sp, #12]
	ldr	r3, [sp, #8]
	mov	r4, ip
	add	r3, r3, #1024
	str	r3, [sp, #8]
	b	.L1307
.L1301:
	ldr	r3, [r5, #2768]
	cmp	r3, #5
	bls	.L1313
	cmp	r10, #0
	bne	.L1313
	ldr	r3, [r7]
	and	r2, r3, #139264
	cmp	r2, #139264
	ittt	eq
	moveq	fp, #-1
	orreq	r3, r3, #131072
	streq	r3, [r7]
.L1313:
	mov	r0, fp
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1332:
	.align	2
.L1331:
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
	ldr	r3, .L1335
	mov	r6, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r5, [r3, #265]	@ zero_extendqisi2
	cbnz	r0, .L1334
	ldr	r2, .L1335+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	muls	r2, r3, r2
	cmp	r1, r2
	it	cc
	movcc	r5, #4
.L1334:
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
.L1336:
	.align	2
.L1335:
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
	ldr	r4, .L1363
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #1828]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #2768]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	ite	cc
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	beq	.L1351
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
	ldrb	r0, [r4, #2760]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2760]	@ zero_extendqisi2
	bl	NandcSetMode
	adds	r3, r6, #1
	bne	.L1340
.L1349:
	mov	r6, #-1
	b	.L1341
.L1340:
	mov	r2, r6
	ldr	r0, .L1363+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	itt	ls
	addls	r4, r4, r10, lsl #3
	ldrls	r3, [r4, #12]
	ldr	r4, .L1363+8
	itttt	ls
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #3988]
	adds	r3, r3, #1
	str	r3, [r4, #3988]
	cmp	r3, #2048
	bcc	.L1341
	movs	r7, #0
	str	r7, [r4, #3988]
	mov	fp, r7
	b	.L1339
.L1351:
	mov	r6, #1024
.L1339:
	movs	r4, #0
	mov	r8, #-1
	str	r4, [sp, #4]
	mov	ip, r4
	mov	r5, r4
.L1347:
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
	bhi	.L1343
	cmp	r0, #2
	bhi	.L1353
	adds	r5, r5, #1
	cmp	r5, #9
	bls	.L1353
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1345
.L1343:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1354
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1355
	str	r5, [sp, #4]
	b	.L1354
.L1353:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1344
.L1354:
	movs	r5, #0
.L1344:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1347
.L1345:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	it	cs
	movcs	r4, ip
	b	.L1346
.L1355:
	mov	r4, ip
.L1346:
	cbz	r4, .L1348
	ldr	r0, .L1363+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1348:
	cmp	r8, #0
	beq	.L1341
	ldr	r0, .L1363+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1349
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1341:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1364:
	.align	2
.L1363:
	.word	.LANCHOR0
	.word	.LC88
	.word	.LANCHOR2
	.word	.LC89
	.word	.LC90
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
	bne	.L1366
	ldr	r9, .L1385+4
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1367
.L1369:
	ldrb	r3, [r10, #2761]	@ zero_extendqisi2
	ldr	r9, .L1385+4
	cbz	r3, .L1366
	b	.L1384
.L1367:
	movs	r3, #0
	mov	r0, r5
	strb	r3, [r9, #8]
	mov	r1, r6
	mov	r3, r7
	mov	r2, r8
	bl	FlashReadRawPage
	strb	fp, [r9, #8]
	adds	r3, r0, #1
	beq	.L1369
	mov	r4, r0
	b	.L1366
.L1384:
	ldr	r3, [r9, #1828]
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
	beq	.L1370
	ldrb	r3, [r9, #1892]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1366
.L1370:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1366:
	ldr	r9, .L1385+8
	ldr	ip, [r9, #3992]
	cmp	ip, #0
	beq	.L1371
	adds	r2, r4, #1
	bne	.L1371
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r3, r6
	mov	r2, r5
	mov	r4, r0
	ldr	r0, .L1385
	mov	r1, r4
	bl	printk
	adds	r3, r4, #1
	bne	.L1371
	ldr	r3, .L1385+4
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	cbz	r3, .L1371
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	r4, [r9, #3992]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	r4
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1371:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1386:
	.align	2
.L1385:
	.word	.LC91
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
	ldr	r4, .L1397
	mov	r6, r1
	movs	r5, #0
	ldrb	r0, [r4, #2760]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2760]	@ zero_extendqisi2
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
	beq	.L1388
	cmp	r8, #-1
	bne	.L1389
.L1388:
	ldrb	r3, [r4, #2760]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L1389
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1397
	movs	r2, #0
	strb	r2, [r3, #2761]
	b	.L1390
.L1389:
	movs	r3, #1
	strb	r3, [r4, #2761]
.L1390:
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1398:
	.align	2
.L1397:
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
	ldr	r6, .L1415
	movs	r4, #0
	ldr	r3, .L1415+4
	mov	r9, #4
	ldr	r5, .L1415+8
	mov	r7, #-1
	mov	r8, r6
	ldr	r0, [r3]	@ unaligned
	ldrh	r3, [r6, #266]
	mov	r6, r5
	str	r4, [r5, #4000]
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #3984]
	str	r3, [r5, #3996]
	bl	flash_enter_slc_mode
.L1400:
	add	fp, r4, #1
	mov	r10, #0
.L1402:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #3984]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1401
	movs	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #3984]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1401
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1403
	b	.L1402
.L1404:
	add	r0, fp, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	bne	.L1410
	add	r1, fp, #160
	movs	r2, #32
	ldr	r0, .L1415+12
	bl	ftl_memcpy
	ldr	r7, .L1415+16
	ldr	r1, [r6, #3996]
	movs	r2, #32
	add	r0, r7, #44
	adds	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #3996]
	mov	r2, #852
	addw	r0, r7, #1902
	adds	r1, r1, #224
	bl	ftl_memcpy
	ldr	fp, [r6, #3996]
	str	r4, [r6, #4000]
	mov	r0, r4
	ldrh	r1, [r8, #266]
	ldr	r3, [fp, #1076]
	strb	r3, [r7, #2761]
	bl	__aeabi_uidiv
	adds	r3, r0, #1
	cbnz	r0, .L1413
	movs	r3, #2
.L1413:
	str	r3, [r6, #4004]
	movs	r7, #0
	ldrh	r3, [fp, #14]
	strb	r3, [r5, #4008]
.L1403:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1400
	mov	r0, r9
.L1414:
	bl	flash_exit_slc_mode
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1401:
	ldr	fp, [r5, #3996]
	ldr	r2, .L1415+20
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1403
	cmp	r7, #0
	bne	.L1404
	mov	r0, r4
	ldrh	r1, [r8, #266]
	bl	__aeabi_uidiv
	ldr	r3, .L1415+8
	adds	r0, r0, #1
	str	r0, [r3, #4004]
	mov	r0, r7
	b	.L1414
.L1410:
	mov	r7, #-1
	b	.L1403
.L1416:
	.align	2
.L1415:
	.word	.LANCHOR1
	.word	.LANCHOR3+11
	.word	.LANCHOR2
	.word	.LANCHOR1+256
	.word	.LANCHOR0
	.word	1312902724
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
	ldr	r4, .L1445
	add	r3, r4, r9, lsl #3
	ldrb	r2, [r3, #16]	@ zero_extendqisi2
	ldr	r7, [r3, #12]
	ldrb	r3, [r4, #1900]	@ zero_extendqisi2
	add	r6, r2, #8
	subs	r3, r3, #67
	add	r6, r7, r6, lsl #8
	cmp	r3, #1
	lsl	r3, r2, #8
	str	r3, [sp, #12]
	bls	.L1434
	ldrb	r5, [r4, #2761]	@ zero_extendqisi2
	cbz	r5, .L1435
	movs	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	movs	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
	b	.L1419
.L1435:
	str	r5, [sp, #4]
.L1419:
	ldr	r3, [sp, #12]
	movs	r1, #92
	adds	r3, r7, r3
	str	r1, [r3, #2056]
	movs	r1, #197
	str	r1, [r3, #2056]
	b	.L1418
.L1434:
	movs	r3, #0
	str	r3, [sp, #4]
.L1418:
	mov	r8, #1
	mov	r10, #-1
	lsls	r3, r2, #8
	str	r3, [sp, #20]
.L1420:
	ldr	r3, .L1445+4
	ldrb	r3, [r3, #4009]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r8, r3
	bcs	.L1444
	ldrb	r3, [r4, #1900]	@ zero_extendqisi2
	mov	r0, r6
	uxtb	r1, r8
	subs	r3, r3, #67
	cmp	r3, #1
	bhi	.L1421
	bl	SandiskSetRRPara
	b	.L1422
.L1421:
	bl	ToshibaSetRRPara
.L1422:
	ldrb	r3, [r4, #1900]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1423
	ldr	r3, .L1445+4
	ldrb	r3, [r3, #4009]	@ zero_extendqisi2
	subs	r3, r3, #3
	cmp	r8, r3
	itttt	eq
	moveq	r2, #179
	ldreq	r3, [sp, #20]
	addeq	r3, r7, r3
	streq	r2, [r3, #2056]
.L1423:
	ldr	r3, [sp, #12]
	movs	r2, #38
	adds	r3, r7, r3
	str	r2, [r3, #2056]
	movs	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cbz	r3, .L1424
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
	b	.L1425
.L1424:
	mov	r0, r9
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1425:
	adds	r2, r5, #1
	beq	.L1426
	ldrb	r2, [r4, #1892]	@ zero_extendqisi2
	cmp	r10, #-1
	it	eq
	moveq	r10, r5
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1428
	mov	fp, #0
	str	fp, [sp, #8]
.L1426:
	add	r8, r8, #1
	b	.L1420
.L1444:
	mov	r5, r10
.L1428:
	ldrb	r2, [r4, #1900]	@ zero_extendqisi2
	mov	r0, r6
	movs	r1, #0
	subs	r2, r2, #67
	cmp	r2, #1
	bhi	.L1430
	bl	SandiskSetRRPara
	b	.L1431
.L1430:
	bl	ToshibaSetRRPara
.L1431:
	ldr	r3, [sp, #12]
	movs	r2, #255
	add	r7, r7, r3
	str	r2, [r7, #2056]
	ldrb	r2, [r4, #1892]	@ zero_extendqisi2
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1432
	adds	r3, r5, #1
	it	ne
	movne	r5, #256
.L1432:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cbz	r3, .L1433
	movs	r0, #4
	bl	NandcSetDdrMode
.L1433:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1446:
	.align	2
.L1445:
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
	ldr	r5, .L1460
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
.L1448:
	ldr	r3, .L1460+4
	ldrb	r3, [r3, #4009]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r10, r3
	bcs	.L1451
	mov	r0, r6
	uxtb	r1, r10
	bl	SamsungSetRRPara
	mov	r2, r9
	mov	r0, r8
	mov	r1, fp
	mov	r3, r7
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1449
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1454
	movs	r7, #0
	mov	r9, r7
.L1449:
	add	r10, r10, #1
	b	.L1448
.L1454:
	mov	r4, r0
.L1451:
	mov	r0, r6
	movs	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L1453
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
.L1453:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1461:
	.align	2
.L1460:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r3
	ldr	r3, .L1478
	mov	fp, r2
	.pad #28
	sub	sp, sp, #28
	mov	r8, r0
	mov	r10, r1
	ldrb	r2, [r3, #76]	@ zero_extendqisi2
	mov	r5, r3
	ldrb	r4, [r3, #1892]	@ zero_extendqisi2
	cbnz	r2, .L1463
	add	r4, r4, r4, lsl #1
	ubfx	r4, r4, #2, #8
	b	.L1464
.L1463:
	ldr	r2, .L1478+4
	smull	r2, r3, r4, r2
	uxtb	r4, r3
.L1464:
	mov	r0, r8
	movs	r7, #0
	bl	NandcWaitFlashReady
	add	r3, r5, r8, lsl #3
	mov	r5, #-1
	ldr	r2, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	str	r3, [sp, #16]
	mov	r3, r2
	str	r2, [sp, #12]
	ldr	r2, [sp, #16]
	add	r6, r3, r2, lsl #8
.L1465:
	ldr	r3, .L1478+8
	ldrb	r3, [r3, #4009]	@ zero_extendqisi2
	cmp	r7, r3
	bcs	.L1468
	movs	r3, #239
	movs	r0, #200
	str	r3, [r6, #2056]
	movs	r3, #137
	str	r3, [r6, #2052]
	bl	NandcDelayns
	mov	ip, #0
	adds	r3, r7, #1
	mov	r2, fp
	str	r3, [r6, #2048]
	mov	r0, r8
	str	ip, [r6, #2048]
	mov	r1, r10
	str	ip, [r6, #2048]
	str	ip, [r6, #2048]
	str	r3, [sp, #8]
	mov	r3, r9
	str	ip, [sp, #20]
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1466
	cmp	r5, #-1
	it	eq
	moveq	r5, r0
	cmp	r0, r4
	bcc	.L1472
	ldr	ip, [sp, #20]
	mov	r9, ip
	mov	fp, ip
.L1466:
	ldr	r7, [sp, #8]
	b	.L1465
.L1472:
	mov	r5, r0
.L1468:
	ldr	r2, [sp, #16]
	movs	r0, #200
	ldr	r3, [sp, #12]
	add	r6, r3, r2, lsl #8
	movs	r3, #239
	str	r3, [r6, #2056]
	movs	r3, #137
	str	r3, [r6, #2052]
	bl	NandcDelayns
	cmp	r5, r4
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	bcc	.L1470
	adds	r3, r5, #1
	ldr	r0, .L1478+12
	it	ne
	movne	r5, #256
	mov	r1, r7
	str	r5, [sp]
	mov	r2, r10
	mov	r3, r7
	bl	printk
.L1470:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1479:
	.align	2
.L1478:
	.word	.LANCHOR0
	.word	1431655766
	.word	.LANCHOR2
	.word	.LC92
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
	mov	r10, r2
	ldr	r5, .L1497
	mov	r8, r3
	mov	r7, r0
	mov	fp, r1
	adds	r3, r5, r0
	mov	r6, #-1
	ldr	r2, [r5, #1788]
	ldrb	r4, [r3, #1914]	@ zero_extendqisi2
	ldrb	r9, [r5, #1904]	@ zero_extendqisi2
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	cmp	r2, #7
	it	eq
	ldrbeq	r4, [r3, #1922]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L1482:
	cmp	ip, r9
	bcs	.L1486
	adds	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #1903]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1497+4
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
	beq	.L1484
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	cmp	r6, #-1
	it	eq
	moveq	r6, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1491
	mov	r8, #0
	mov	r10, r8
.L1484:
	add	ip, ip, #1
	b	.L1482
.L1491:
	mov	r6, r0
.L1486:
	ldr	r3, [r5, #1788]
	add	r7, r7, r5
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #7
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	ite	eq
	strbeq	r4, [r7, #1922]
	strbne	r4, [r7, #1914]
	add	r3, r3, r3, lsl #1
	cmp	r6, r3, asr #2
	bcc	.L1490
	adds	r3, r6, #1
	it	ne
	movne	r6, #256
.L1490:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1498:
	.align	2
.L1497:
	.word	.LANCHOR0
	.word	.LANCHOR0+1906
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
	ldr	r3, .L1502
	mov	r5, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r6, [r3, #265]	@ zero_extendqisi2
	cbnz	r0, .L1500
	ldr	r2, .L1502+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	muls	r1, r3, r1
	cmp	r5, r1
	bcs	.L1500
	ldrb	r3, [r2]	@ zero_extendqisi2
	cbnz	r3, .L1501
	subs	r6, r6, #2
	b	.L1500
.L1501:
	movs	r6, #4
.L1500:
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
.L1503:
	.align	2
.L1502:
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
	ldr	r4, .L1513
	ldr	r5, .L1513+4
	ldr	r8, .L1513+12
	ldr	r3, [r4, #3984]
	mov	r9, r4
	ldrb	r0, [r4, #4010]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #3996]
	bl	FlashBchSel
	ldr	r0, [r4, #3984]
	mov	r1, #2048
	bl	__memzero
	ldr	r3, [r4, #3996]
	add	r1, r5, #1792
	movs	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #3996]
	ldrb	r3, [r5, #1616]	@ zero_extendqisi2
	adds	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #2761]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #3996]
	addw	r1, r5, #1620
	movs	r2, #8
	adds	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #3996]
	addw	r1, r5, #1628
	movs	r2, #32
	adds	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #3996]
	ldr	r1, .L1513+8
	movs	r2, #32
	adds	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #3996]
	add	r1, r5, #44
	movs	r2, #32
	adds	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #3996]
	mov	r2, #852
	addw	r1, r5, #1902
	adds	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #3996]
	movw	r1, #2036
	add	r0, r6, #12
	bl	JSHash
	mov	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #4012]
	str	r3, [r4, #3996]
	str	r0, [r6, #8]
	movs	r0, #0
	bl	flash_enter_slc_mode
	movs	r6, #0
	mov	r7, r6
.L1507:
	ldr	r1, [r5, #4]
	movs	r0, #0
	mov	r2, r0
	muls	r1, r7, r1
	bl	FlashEraseBlock
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #3984]
	muls	r1, r7, r1
	mov	r3, r0
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #3984]
	muls	r1, r7, r1
	mov	r3, r0
	adds	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r2, [r4, #4012]
	muls	r1, r7, r1
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r2, r7, #1
	adds	r0, r0, #1
	beq	.L1505
	ldr	fp, [r9, #3996]
	ldr	r3, [fp]
	cmp	r3, r8
	bne	.L1505
	add	r0, fp, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	JSHash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1505
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [r9, #4004]
	mul	r3, r3, r7
	str	r3, [r9, #4000]
	beq	.L1508
	movs	r6, #1
.L1505:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1507
	b	.L1506
.L1508:
	movs	r6, #2
.L1506:
	movs	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	lsrs	r0, r0, #5
	negs	r0, r0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1514:
	.align	2
.L1513:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1+256
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	ldr	r3, .L1528
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1528+4
	ldr	r0, [r3]	@ unaligned
	ldr	r2, [r4, #2772]
	ldr	r3, .L1528+8
	str	r0, [sp, #12]	@ unaligned
	cmp	r2, r3
	ldrb	r10, [r4, #1892]	@ zero_extendqisi2
	str	r3, [sp, #4]
	bne	.L1516
	movs	r0, #0
	bl	flash_enter_slc_mode
.L1516:
	mov	r0, r9
	mov	r1, #2048
	bl	__memzero
	mov	r8, #-1
	movs	r5, #2
.L1517:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1521
	movs	r7, #0
.L1519:
	add	r3, sp, #12
	ldr	r6, .L1528+12
	ldrb	fp, [r7, r3]	@ zero_extendqisi2
	mov	r0, fp
	bl	FlashBchSel
	ldr	r1, [r4, #4]
	movs	r0, #0
	ldr	r2, [r6, #3984]
	muls	r1, r5, r1
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1518
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L1519
	b	.L1520
.L1518:
	ldr	r3, [r6, #3984]
	ldr	r2, .L1528+16
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1520
	mov	r1, fp
	ldr	r0, .L1528+20
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #3984]
	bl	ftl_memcpy
	ldr	r3, [r6, #3984]
	ldr	r2, .L1528+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r6, #4004]
	cmp	r3, r5
	bls	.L1524
	str	r5, [r6, #4004]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1520:
	adds	r5, r5, #1
	b	.L1517
.L1524:
	mov	r8, #0
.L1521:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #2772]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bne	.L1526
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1526:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1529:
	.align	2
.L1528:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	1446522928
	.word	.LANCHOR2
	.word	-52655045
	.word	.LC93
	.fnend
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	1
	.global	FlashInit
	.thumb
	.thumb_func
	.type	FlashInit, %function
FlashInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	.pad #28
	sub	sp, sp, #28
	mov	r0, #32768
	bl	ftl_malloc
	ldr	r5, .L1626
	ldr	r4, .L1626+4
	movs	r7, #0
	add	r9, r4, #12
	mov	r10, r4
	str	r0, [r5, #3984]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #4012]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r5, #4016]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #4020]
	mov	r0, #4096
	bl	ftl_malloc
	movs	r3, #50
	str	r7, [r5, #4004]
	strb	r3, [r4, #1]
	strb	r3, [r5, #4008]
	movs	r3, #128
	strb	r7, [r4, #2761]
	str	r3, [r4, #4]
	movs	r3, #60
	str	r7, [r5, #3988]
	strb	r7, [r4]
	strb	r3, [r5, #4010]
	str	r0, [r5, #4024]
	mov	r0, r6
	bl	NandcInit
	add	r6, r4, #1792
.L1535:
	uxtb	fp, r7
	mov	r0, fp
	bl	FlashReset
	ldr	r2, [r9, r7, lsl #3]
	add	r3, r9, r7, lsl #3
	mov	r0, fp
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	str	r2, [sp, #20]
	bl	NandcFlashCs
	movs	r3, #144
	movs	r0, #200
	ldr	r2, [sp, #20]
	add	r8, r2, r8, lsl #8
	str	r3, [r8, #2056]
	movs	r3, #0
	str	r3, [r8, #2052]
	bl	NandcDelayns
	ldr	r3, [r8, #2048]
	mov	r0, fp
	strb	r3, [r6]
	ldr	r3, [r8, #2048]
	strb	r3, [r6, #1]
	ldr	r3, [r8, #2048]
	strb	r3, [r6, #2]
	ldr	r3, [r8, #2048]
	strb	r3, [r6, #3]
	ldr	r3, [r8, #2048]
	strb	r3, [r6, #4]
	ldr	r3, [r8, #2048]
	strb	r3, [r6, #5]
	bl	NandcFlashDeCs
	ldrb	r2, [r6]	@ zero_extendqisi2
	subs	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1531
	ldrb	r1, [r6, #2]	@ zero_extendqisi2
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	ldr	r0, .L1626+8
	str	r1, [sp]
	ldrb	r1, [r6, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r6, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	adds	r1, r7, #1
	bl	printk
.L1531:
	cbnz	r7, .L1532
	ldrb	r3, [r4, #1792]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1576
	ldrb	r3, [r10, #1793]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1576
.L1532:
	ldrb	r3, [r6]	@ zero_extendqisi2
	adds	r7, r7, #1
	adds	r6, r6, #8
	cmp	r3, #181
	itt	eq
	moveq	r3, #44
	strbeq	r3, [r6, #-8]
	cmp	r7, #4
	bne	.L1535
	ldrb	r3, [r4, #1792]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1536
	ldr	r3, .L1626+4
	ldr	r0, [r3, #2764]
	bl	NandcSetDdrMode
.L1536:
	ldr	r0, .L1626+12
	mov	r1, #852
	bl	__memzero
	ldr	r3, .L1626+16
	str	r3, [r4, #1788]
	movs	r3, #0
	strb	r3, [r4, #8]
	ldrb	r3, [r4, #1793]	@ zero_extendqisi2
	cmp	r3, #161
	beq	.L1537
	cmp	r3, #241
	beq	.L1537
	cmp	r3, #218
	beq	.L1537
	cmp	r3, #220
	bne	.L1538
.L1537:
	ldrb	r1, [r4, #1792]	@ zero_extendqisi2
	movs	r2, #1
	strb	r2, [r4]
	movs	r2, #16
	strb	r2, [r4, #1]
	cmp	r1, #152
	strb	r2, [r5, #4010]
	ldr	r2, .L1626+20
	strb	r1, [r2, #2969]
	strb	r3, [r2, #2970]
	bne	.L1539
	ldr	r1, .L1626+4
	ldrsb	r1, [r1, #1796]
	cmp	r1, #0
	ittt	ge
	movge	r0, #24
	ldrge	r1, .L1626
	strbge	r0, [r1, #4010]
.L1539:
	cmp	r3, #218
	bne	.L1540
	mov	r1, #2048
	b	.L1622
.L1540:
	cmp	r3, #220
	bne	.L1541
	mov	r1, #4096
.L1622:
	strh	r1, [r2, #2982]	@ movhi
	strb	r3, [r2, #2970]
.L1541:
	ldr	r1, .L1626+24
	movs	r2, #32
	ldr	r0, .L1626+28
	bl	ftl_memcpy
	ldr	r0, .L1626+16
	movs	r2, #32
	addw	r1, r0, #2712
	bl	ftl_memcpy
.L1538:
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r6, .L1626+4
	cmp	r3, #0
	bne	.L1542
	bl	FlashLoadPhyInfoInRam
	cbnz	r0, .L1544
	ldr	r3, [r6, #1788]
	ldrh	r3, [r3, #16]
	lsrs	r3, r3, #8
	lsls	r7, r3, #31
	and	r0, r3, #7
	strb	r0, [r6, #2760]
	bmi	.L1544
	movs	r3, #1
	strb	r3, [r6, #2761]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2760]	@ zero_extendqisi2
	bl	NandcSetMode
.L1544:
	ldr	r3, [r4, #1788]
	ldr	r6, .L1626+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #76]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1542
	ldr	r3, [r6, #2764]
	cbz	r3, .L1547
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	b	.L1623
.L1547:
	ldrb	r0, [r6, #2760]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2760]	@ zero_extendqisi2
.L1623:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1542
	movs	r0, #1
	ldr	r6, .L1626+4
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #1788]
	ldr	r0, .L1626+32
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	adds	r1, r0, #1
	beq	.L1533
	bl	FlashDieInfoInit
	ldr	r3, [r6, #1788]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, [r6, #1788]
	ldrh	r2, [r6, #1824]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	addw	r2, r2, #4095
	cmp	r1, r2, asr #12
	blt	.L1549
	ldrh	r2, [r3, #14]
	adds	r2, r2, #255
	cmp	r1, r2, asr #8
	bge	.L1550
.L1549:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L1550:
	ldrb	r3, [r4, #2760]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1551
	bl	FlashSavePhyInfo
	movs	r0, #0
	ldr	r1, [r5, #4000]
	bl	FlashDdrParaScan
.L1551:
	bl	FlashSavePhyInfo
.L1542:
	ldr	r2, [r4, #1788]
	ldr	r6, .L1626+4
	ldr	r7, .L1626
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r8, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #76]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r4, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r5, #4028]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #2820]
	strb	r3, [r4, #2760]
	movs	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r5, #3992]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r8
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #1788]
	ldrh	r2, [r3, #16]
	lsls	r2, r2, #25
	bpl	.L1553
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r6, #1903]	@ zero_extendqisi2
	strb	r0, [r6, #1900]
	strb	r3, [r6, #1901]
	ldrb	r3, [r6, #1904]	@ zero_extendqisi2
	strb	r3, [r7, #4009]
	subs	r3, r0, #1
	cmp	r3, #6
	bhi	.L1554
	ldr	r3, .L1626+36
	str	r3, [r7, #3992]
	subs	r3, r0, #5
	cmp	r3, #1
	bhi	.L1555
	movs	r3, #1
	str	r3, [r6, #2812]
	b	.L1556
.L1555:
	cmp	r0, #7
	beq	.L1577
.L1556:
	ldr	r6, .L1626+40
	b	.L1557
.L1627:
	.align	2
.L1626:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC94
	.word	.LANCHOR0+1902
	.word	.LANCHOR1+256
	.word	.LANCHOR1
	.word	.LANCHOR1+2844
	.word	.LANCHOR0+44
	.word	.LC95
	.word	HynixReadRetrial
	.word	.LANCHOR0+1922
.L1577:
	addw	r6, r6, #1930
.L1557:
	subs	r2, r6, #1
	movs	r3, #0
	adds	r6, r6, #31
.L1558:
	ldrsb	r1, [r2, #1]!
	cmp	r1, #0
	it	eq
	addeq	r3, r3, #1
	cmp	r2, r6
	bne	.L1558
	cmp	r3, #27
	bls	.L1553
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1553
.L1554:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1560
	ldr	r3, .L1628
	cmp	r0, #19
	str	r3, [r7, #3992]
	ite	ne
	movne	r3, #7
	moveq	r3, #15
	strb	r3, [r7, #4009]
	b	.L1553
.L1560:
	cmp	r0, #33
	beq	.L1562
	sub	r3, r0, #65
	cmp	r3, #1
	bhi	.L1563
.L1562:
	ldr	r3, .L1628+4
	str	r3, [r5, #3992]
	movs	r3, #4
	strb	r3, [r4, #1901]
	movs	r3, #7
	strb	r3, [r5, #4009]
	b	.L1553
.L1563:
	sub	r3, r0, #67
	uxtb	r3, r3
	cmp	r3, #1
	bls	.L1564
	sub	r2, r0, #34
	cmp	r2, #1
	bhi	.L1565
.L1564:
	ldr	r2, .L1628+4
	cmp	r0, #35
	str	r2, [r5, #3992]
	beq	.L1566
	cmp	r0, #68
	beq	.L1566
	ldr	r2, .L1628+8
	movs	r1, #7
	strb	r1, [r2, #4009]
	b	.L1567
.L1566:
	movs	r2, #17
	strb	r2, [r5, #4009]
.L1567:
	cmp	r3, #1
	ite	ls
	movls	r3, #4
	movhi	r3, #5
	strb	r3, [r4, #1901]
	b	.L1553
.L1565:
	cmp	r0, #49
	itt	eq
	ldreq	r3, .L1628+12
	streq	r3, [r7, #3992]
.L1553:
	ldr	r1, [r4, #2772]
	ldr	r2, .L1628+16
	ldr	r3, .L1628+20
	cmp	r1, r2
	bne	.L1569
	ldrb	r2, [r3, #76]	@ zero_extendqisi2
	cbz	r2, .L1569
	ldr	r3, [r3, #1788]
	movs	r2, #0
	strb	r2, [r3, #18]
.L1569:
	ldrb	r2, [r4, #1792]	@ zero_extendqisi2
	ldr	r3, .L1628+20
	cmp	r2, #44
	bne	.L1570
	ldrb	r2, [r3, #2761]	@ zero_extendqisi2
	cbz	r2, .L1570
	movs	r0, #1
	movs	r2, #0
	strb	r2, [r3, #2761]
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
.L1570:
	movs	r0, #0
	bl	flash_enter_slc_mode
	ldrb	r3, [r4, #2760]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1571
	ldr	r2, .L1628+20
	ldrb	r2, [r2, #2761]	@ zero_extendqisi2
	cbnz	r2, .L1572
	lsls	r3, r3, #31
	bmi	.L1571
.L1572:
	movs	r0, #0
	ldr	r1, [r5, #4000]
	bl	FlashDdrParaScan
.L1571:
	movs	r0, #0
	mov	r8, #16
	bl	flash_exit_slc_mode
	ldr	r3, [r4, #1788]
	ldr	r6, .L1628+20
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1628+24
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1628+28
	strb	r8, [r4, #1]
	bl	FlashTimingCfg
	ldr	r5, [r4, #1788]
	ldrb	r3, [r5, #12]	@ zero_extendqisi2
	ldrh	r7, [r5, #14]
	strh	r3, [r4, #1872]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	str	r3, [r4, #1868]
	ldr	r3, [r4, #1792]
	str	r3, [r4, #1864]
	ldrb	r3, [r4, #1616]	@ zero_extendqisi2
	strh	r3, [r4, #1874]	@ movhi
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	strh	r7, [r4, #1878]	@ movhi
	strh	r3, [r4, #1876]	@ movhi
	ldrh	r3, [r5, #10]
	strh	r3, [r4, #1880]	@ movhi
	ldrb	r1, [r5, #12]	@ zero_extendqisi2
	ldrh	r0, [r5, #10]
	bl	__aeabi_idiv
	strh	r0, [r4, #1882]	@ movhi
	ldrb	r2, [r5, #9]	@ zero_extendqisi2
	strh	r2, [r4, #1884]	@ movhi
	ldrb	r1, [r5, #9]	@ zero_extendqisi2
	ldrh	r3, [r5, #10]
	smulbb	r3, r1, r3
	mov	r1, #512
	strh	r1, [r4, #1888]	@ movhi
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	strh	r1, [r4, #1890]	@ movhi
	uxth	r3, r3
	ldrb	r1, [r4]	@ zero_extendqisi2
	strh	r3, [r4, #1886]	@ movhi
	cmp	r1, #1
	bne	.L1574
	lsls	r3, r3, #1
	lsrs	r7, r7, #1
	lsls	r2, r2, #1
	strh	r3, [r6, #1886]	@ movhi
	strb	r8, [r6, #1]
	movs	r3, #8
	strh	r7, [r6, #1878]	@ movhi
	strh	r2, [r6, #1884]	@ movhi
	strh	r3, [r6, #1890]	@ movhi
.L1574:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	FlashSuspend
	movs	r0, #0
	b	.L1533
.L1576:
	mvn	r0, #1
.L1533:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1629:
	.align	2
.L1628:
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	.LANCHOR2
	.word	SamsungReadRetrial
	.word	1446522928
	.word	.LANCHOR0
	.word	.LANCHOR2+1932
	.word	150000
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
	ldr	r2, .L1645
	mov	r8, r1
	mov	r7, r0
	ldr	r3, [r2, #1788]
	mov	r5, r2
	ldrb	r1, [r2, #76]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cbz	r1, .L1631
	ldr	r1, [r2, #2772]
	ldr	r2, .L1645+4
	cmp	r1, r2
	beq	.L1630
.L1631:
	subs	r2, r3, #5
	cmp	r2, #2
	bls	.L1632
	cmp	r3, #68
	beq	.L1632
	cmp	r3, #35
	beq	.L1632
	cmp	r3, #19
	bne	.L1630
.L1632:
	ldr	r9, .L1645
	movw	r10, #65535
	ldr	r6, .L1645+8
.L1634:
	ldr	r3, [r5, #1788]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1630
	add	r3, r9, r4, lsl #1
	ldrh	r3, [r3, #592]
	cmp	r3, r10
	bne	.L1630
	movs	r1, #255
	mov	r2, #32768
	ldr	r0, [r6, #4012]
	bl	memset
	add	r1, r4, r8
	adds	r4, r4, #1
	mov	r0, r7
	ldr	r2, [r6, #4012]
	movs	r3, #0
	bl	FlashProgPage
	uxth	r4, r4
	b	.L1634
.L1630:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1646:
	.align	2
.L1645:
	.word	.LANCHOR0
	.word	1446522928
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1677
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r6, #0
	ldr	r7, .L1677+4
	.pad #28
	sub	sp, sp, #28
	ldrb	r9, [r3, #265]	@ zero_extendqisi2
	mov	r8, r1
	mov	fp, r0
	str	r2, [sp, #8]
	mov	r10, r7
.L1648:
	cmp	r6, r8
	beq	.L1676
	rsb	r3, r6, r8
	add	r2, sp, #20
	mov	r0, fp
	ldr	r1, [sp, #8]
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldrb	r2, [r7, #1616]	@ zero_extendqisi2
	ldr	r3, [sp, #16]
	cmp	r3, r2
	bcc	.L1649
	mov	r3, #-1
	str	r3, [fp]
	b	.L1650
.L1649:
	add	r3, r3, r7
	ldrb	r4, [r3, #1620]	@ zero_extendqisi2
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r0, r4
	ldr	r1, [sp, #20]
	bl	FlashReadCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	ldr	r3, [fp, #12]
	movs	r1, #0
	mov	r2, r9
	mov	r0, r4
	str	r3, [sp]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	ldr	r1, [r7, #4]
	ldr	r3, [sp, #20]
	mov	r5, r0
	mov	r0, r4
	add	r1, r1, r3
	bl	FlashReadCmd
	ldr	r3, [fp, #8]
	cbz	r3, .L1651
	add	r3, r3, #2048
.L1651:
	ldr	r2, [fp, #12]
	cbz	r2, .L1652
	adds	r2, r2, #8
.L1652:
	str	r2, [sp]
	movs	r1, #0
	mov	r2, r9
	mov	r0, r4
	bl	NandcXferData
	str	r0, [sp, #12]
	mov	r0, r4
	bl	NandcFlashDeCs
	ldr	r3, [sp, #12]
	cmp	r5, r3
	it	cc
	movcc	r5, r3
	ldrb	r3, [r10, #1892]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r5, r3, asr #2
	bls	.L1653
	adds	r0, r5, #1
	it	ne
	movne	r5, #256
	str	r5, [fp]
	b	.L1655
.L1653:
	movs	r3, #0
	str	r3, [fp]
.L1655:
	ldr	r3, [fp, #12]
	cbz	r3, .L1656
	ldr	r2, [r3, #8]
	adds	r1, r2, #1
	bne	.L1656
	ldr	r3, [r3]
	adds	r3, r3, #1
	it	ne
	strne	r2, [fp]
.L1656:
	ldr	r3, [fp]
	adds	r2, r3, #1
	bne	.L1650
	ldr	r1, [fp, #4]
	ldr	r0, .L1677+8
	ldrb	r2, [r10, #1892]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [fp, #8]
	cbz	r1, .L1658
	ldr	r0, .L1677+12
	movs	r2, #4
	movs	r3, #8
	bl	rknand_print_hex
.L1658:
	ldr	r1, [fp, #12]
	cbz	r1, .L1650
	movs	r2, #4
	ldr	r0, .L1677+16
	mov	r3, r2
	bl	rknand_print_hex
.L1650:
	adds	r6, r6, #1
	add	fp, fp, #36
	b	.L1648
.L1676:
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1678:
	.align	2
.L1677:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LC96
	.word	.LC97
	.word	.LC98
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
	ldr	r3, .L1778
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r5, .L1778+4
	str	r1, [sp, #28]
	str	r3, [sp, #24]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	str	r2, [sp, #16]
	str	r3, [sp, #32]
	ldrb	r3, [r5]	@ zero_extendqisi2
	cbz	r3, .L1722
	bl	FlashReadSlc2KPages
	b	.L1773
.L1722:
	mov	r8, r3
	str	r3, [sp, #12]
.L1680:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	cmp	r3, r2
	bcs	.L1775
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
	ldrb	r2, [r5, #1616]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	mov	r6, r0
	cmp	r3, r2
	bcc	.L1682
	mov	r3, #-1
	str	r3, [r10, r9]
	b	.L1683
.L1682:
	add	r3, r3, r5
	ldrb	r4, [r3, #1620]	@ zero_extendqisi2
	ldr	r3, .L1778+8
	mov	r0, r4
	ldrb	r3, [r3, #4028]	@ zero_extendqisi2
	cmp	r3, #0
	it	eq
	moveq	r6, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1778+4
	ldr	r3, [r3, #1788]
	ldrb	r1, [r3, #19]	@ zero_extendqisi2
	subs	r3, r1, #1
	cmp	r3, #6
	bhi	.L1685
	adds	r2, r5, r4
	cmp	r1, #7
	ldrb	r3, [r2, #1914]	@ zero_extendqisi2
	it	eq
	ldrbeq	r3, [r2, #1922]	@ zero_extendqisi2
	ldr	r2, .L1778+4
	add	r2, r2, r4
	ldrb	r2, [r2, #2756]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1685
	mov	r0, r4
	ldrb	r1, [r5, #1903]	@ zero_extendqisi2
	ldr	r2, .L1778+12
	bl	HynixSetRRPara
.L1685:
	mov	r0, r4
	and	r3, fp, #-2147483648
	str	r3, [sp, #20]
	bl	NandcFlashCs
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1687
	ldr	r3, [sp, #20]
	cbz	r3, .L1688
.L1687:
	ldr	r3, .L1778+4
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	cbz	r3, .L1688
	mov	r0, r4
	bl	flash_enter_slc_mode
.L1690:
	mov	fp, #0
	b	.L1689
.L1688:
	mov	r0, r4
	bl	flash_exit_slc_mode
	b	.L1690
.L1776:
	cmp	r4, #255
	beq	.L1723
.L1691:
	cbz	r6, .L1693
	ldr	r2, [r5, #4]
	mov	r0, r4
	add	r2, r2, r1
	bl	FlashReadDpCmd
.L1694:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cbz	r6, .L1692
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1692:
	ldr	r3, [r7, #12]
	mov	r0, r4
	movs	r1, #0
	ldr	r2, [sp, #24]
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	ip, r0
	cbz	r3, .L1725
	cmp	r0, #-1
	bne	.L1725
	movs	r6, #0
	strb	fp, [r5, #8]
.L1689:
	ldr	r1, [sp, #44]
	adds	r0, r1, #1
	bne	.L1691
	b	.L1776
.L1693:
	mov	r0, r4
	bl	FlashReadCmd
	b	.L1694
.L1723:
	movs	r6, #0
	b	.L1692
.L1725:
	mov	fp, ip
	cbz	r6, .L1696
	ldr	r3, .L1778+4
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
.L1696:
	mov	r0, r4
	str	ip, [sp, #36]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	ldrb	r3, [sp, #32]	@ zero_extendqisi2
	cmp	ip, #-1
	strb	r3, [r5, #8]
	bne	.L1703
	ldrb	r3, [r5, #2761]	@ zero_extendqisi2
	cbnz	r3, .L1698
.L1702:
	ldr	r3, .L1778+8
	ldr	r6, [r3, #3992]
	cbnz	r6, .L1699
	b	.L1777
.L1698:
	ldr	r3, [r5, #1828]
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
	beq	.L1701
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1727
.L1701:
	ubfx	r0, r6, #8, #8
	bl	NandcSetDdrPara
	cmp	fp, #-1
	beq	.L1702
	b	.L1727
.L1699:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r6
	cmp	r0, #-1
	mov	fp, r0
	bne	.L1704
	ldr	r3, [r5, #1788]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #1
	cmp	r3, #6
	bhi	.L1705
	mov	r0, r4
	ldrb	r1, [r5, #1903]	@ zero_extendqisi2
	ldr	r2, .L1778+12
	movs	r3, #0
	bl	HynixSetRRPara
.L1705:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r5, #1892]	@ zero_extendqisi2
	mov	fp, r0
	ldr	r0, .L1778+16
	mov	r3, fp
	bl	printk
	cmp	fp, #-1
	bne	.L1704
	ldrb	r6, [r5, #76]	@ zero_extendqisi2
	cbz	r6, .L1706
	ldr	r3, [sp, #16]
	mov	r0, r4
	cmp	r3, #1
	beq	.L1707
	ldr	r3, [sp, #20]
	cbz	r3, .L1708
.L1707:
	bl	flash_enter_slc_mode
	b	.L1709
.L1708:
	bl	flash_exit_slc_mode
.L1709:
	ldr	r3, .L1778+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r6, [r3, #3992]
	ldr	r3, [r7, #12]
	blx	r6
	movs	r6, #0
	mov	fp, r0
	b	.L1710
.L1777:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	fp, r0
	b	.L1710
.L1727:
	movs	r6, #0
.L1703:
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	fp, r3, asr #2
	bls	.L1712
	ldr	r3, .L1778+8
	ldr	r3, [r3, #3992]
	cbz	r3, .L1729
.L1710:
	cmp	fp, #-1
	beq	.L1706
	b	.L1712
.L1729:
	mov	fp, #256
.L1706:
	str	fp, [r10, r9]
	b	.L1713
.L1779:
	.align	2
.L1778:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1906
	.word	.LC99
.L1704:
	movs	r6, #0
.L1712:
	cmp	fp, #256
	beq	.L1706
	movs	r3, #0
	str	r3, [r10, r9]
.L1713:
	ldr	r3, [r10, r9]
	adds	r2, r3, #1
	bne	.L1715
	ldr	r1, [r7, #4]
	ldr	r0, .L1780
	ldrb	r2, [r5, #1892]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cbz	r1, .L1715
	movs	r2, #4
	ldr	r0, .L1780+4
	mov	r3, r2
	bl	rknand_print_hex
.L1715:
	cbz	r6, .L1717
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r8, r3, asr #2
	bls	.L1718
	ldr	r3, .L1780+8
	ldr	r3, [r3, #3992]
	cmp	r3, #0
	it	eq
	moveq	r8, #256
.L1718:
	add	r9, r9, #36
	cmp	r8, #-1
	str	r8, [r10, r9]
	beq	.L1717
	cmp	r8, #256
	itt	ne
	movne	r3, #0
	strne	r3, [r10, r9]
.L1717:
	ldr	r3, [sp, #12]
	add	r3, r3, r6
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1719
	ldr	r3, [sp, #20]
	cbz	r3, .L1683
.L1719:
	ldrb	r3, [r5, #76]	@ zero_extendqisi2
	cbz	r3, .L1683
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1683:
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	str	r3, [sp, #12]
	b	.L1680
.L1775:
	movs	r0, #0
.L1773:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1781:
	.align	2
.L1780:
	.word	.LC96
	.word	.LC98
	.word	.LANCHOR2
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
	movs	r1, #16
	ldr	r7, .L1794
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L1794+4
	movs	r5, #0
	mov	fp, #-1
	ldrh	r3, [r7, #1878]
	add	r0, r4, #4032
	ldrh	r6, [r7, #1876]
	mov	r10, r5
	mov	r8, r7
	mov	r9, r4
	smulbb	r6, r6, r3
	bl	__memzero
	ldr	r3, [r4, #4016]
	uxth	r6, r6
	str	r5, [sp, #20]
	str	r3, [sp, #24]
.L1783:
	ldrb	r3, [r8, #1616]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1793
	mul	ip, r6, r7
	subs	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1784:
	cmp	r4, r3
	ble	.L1786
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
	beq	.L1785
	ldr	r2, [r9, #4016]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1785
	mov	r1, r7
	ldr	r0, .L1794+8
	mov	r2, r4
	add	r10, r10, #1
	bl	printk
	ldr	r3, .L1794+4
	uxth	r10, r10
	add	r7, r3, r7, lsl #1
	strh	r4, [r7, #4032]	@ movhi
	b	.L1786
.L1785:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1784
.L1786:
	ldrb	r3, [r8, #1616]	@ zero_extendqisi2
	adds	r5, r5, #1
	cmp	r3, r10
	it	eq
	moveq	fp, #0
	b	.L1783
.L1793:
	mov	r0, fp
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1795:
	.align	2
.L1794:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC100
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
	ldr	r8, .L1826+20
	movs	r7, #0
	str	r3, [sp, #12]
	mov	r6, r1
	ldr	r3, .L1826
	mov	r9, r2
	mov	r4, r0
	mov	r10, r0
	mov	fp, r8
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	str	r3, [sp, #8]
.L1797:
	cmp	r7, r6
	beq	.L1825
	subs	r3, r6, r7
	add	r2, sp, #20
	mov	r0, r10
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #1616]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	bcc	.L1798
	mov	r3, #-1
	str	r3, [r10]
	b	.L1799
.L1798:
	add	r3, r3, r8
	ldrb	r5, [r3, #1620]	@ zero_extendqisi2
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
	cbz	r3, .L1800
	add	r3, r3, #2048
.L1800:
	ldr	r2, [r10, #12]
	cbz	r2, .L1801
	adds	r2, r2, #8
.L1801:
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
.L1799:
	adds	r7, r7, #1
	add	r10, r10, #36
	b	.L1797
.L1825:
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L1824
	ldr	r8, .L1826+24
	mov	r10, #0
	mov	fp, r8
.L1804:
	cmp	r10, r6
	beq	.L1824
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L1805
	ldr	r1, [r4, #4]
	ldr	r0, .L1826+4
	bl	printk
	b	.L1806
.L1805:
	rsb	r3, r10, r6
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	mov	r7, r4
	bl	LogAddr2PhyAddr
	ldr	r2, [r8, #4020]
	movs	r3, #0
	str	r3, [r2]
	ldr	r2, [r8, #4024]
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
	ldr	r3, [r8, #4020]
	str	r3, [sp, #36]
	ldr	r3, [r8, #4024]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r5, [sp, #28]
	adds	r3, r5, #1
	bne	.L1807
	ldr	r0, .L1826+8
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L1807:
	ldr	r3, [r4, #12]
	cbz	r3, .L1808
	ldr	r2, [r3]
	ldr	r3, [fp, #4024]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1808
	ldr	r0, .L1826+12
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1808:
	ldr	r3, [r4, #8]
	cbz	r3, .L1806
	ldr	r2, [r3]
	ldr	r3, [fp, #4020]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1806
	ldr	r0, .L1826+16
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1806:
	add	r10, r10, #1
	adds	r4, r4, #36
	b	.L1804
.L1824:
	movs	r0, #0
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1827:
	.align	2
.L1826:
	.word	.LANCHOR1
	.word	.LC101
	.word	.LC102
	.word	.LC103
	.word	.LC104
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r5, .L1839
	ldr	r7, .L1839+4
	ldr	r3, [r5, #756]
	subw	r8, r7, #2978
	ldr	r9, [r5, #1796]
	mov	r10, r8
	str	r3, [r5, #4056]
	str	r9, [r5, #4060]
.L1829:
	ldrh	r3, [r8, #2850]
	cmp	r6, r3
	bcs	.L1838
	ldrh	r4, [r8, #2894]
	movw	fp, #61664
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
	subs	r4, r4, #1
	uxth	r4, r4
.L1830:
	ldrh	r3, [r10, #2894]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1832
	mla	r3, r3, r6, r4
	movs	r1, #1
	ldr	r0, .L1839+8
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [r5, #4052]
	bl	FlashReadPages
	ldr	r3, [r5, #4048]
	adds	r3, r3, #1
	beq	.L1831
	ldrh	r3, [r9]
	cmp	r3, fp
	bne	.L1831
	strh	r4, [r7]	@ movhi
	b	.L1832
.L1831:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1830
.L1832:
	adds	r6, r6, #1
	b	.L1829
.L1838:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1840:
	.align	2
.L1839:
	.word	.LANCHOR2
	.word	.LANCHOR0+2978
	.word	.LANCHOR2+4048
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
	ldr	r3, .L1852
	cmp	r1, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #104
	sub	sp, sp, #104
	it	eq
	ldrheq	r4, [r3, #2900]
	mov	r8, r1
	it	ne
	ldrhne	r4, [r3, #2898]
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
	bne	.L1844
.L1845:
	sxth	r3, r7
	sxth	r4, r5
	cmp	r3, r4
	bgt	.L1844
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
	bne	.L1846
	ldr	r3, [sp, #44]
	adds	r3, r3, #1
	bne	.L1846
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L1846
	subs	r4, r4, #1
	uxth	r5, r4
	b	.L1845
.L1846:
	adds	r3, r4, #1
	uxth	r7, r3
	b	.L1845
.L1844:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1853:
	.align	2
.L1852:
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
	movs	r3, #0
	ldr	r4, .L1883
	ldr	r5, .L1883+4
	ldr	r7, [r4, #1796]
	mov	r8, r4
	str	r3, [r4, #4056]
	str	r7, [r4, #4060]
	bl	FtlBbtMemInit
	ldrh	r6, [r5, #2894]
	subs	r6, r6, #1
	uxth	r6, r6
.L1855:
	ldrh	r3, [r5, #2894]
	subs	r3, r3, #48
	cmp	r6, r3
	ble	.L1858
	movs	r1, #1
	ldr	r0, .L1883+8
	lsls	r3, r6, #10
	str	r3, [r4, #4052]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #4048]
	adds	r3, r3, #1
	bne	.L1856
	movs	r1, #1
	ldr	r3, [r8, #4052]
	ldr	r0, .L1883+8
	mov	r2, r1
	adds	r3, r3, #1
	str	r3, [r8, #4052]
	bl	FlashReadPages
.L1856:
	ldr	r3, [r4, #4048]
	adds	r3, r3, #1
	beq	.L1857
	ldrh	r2, [r7]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1857
	ldr	r3, [r7, #4]
	strh	r6, [r5, #2968]	@ movhi
	str	r3, [r5, #2976]
	ldrh	r3, [r7, #8]
	strh	r3, [r5, #2972]	@ movhi
	b	.L1858
.L1857:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L1855
.L1858:
	ldrh	r3, [r5, #2968]
	movw	r2, #65535
	ldr	r6, .L1883+4
	cmp	r3, r2
	beq	.L1872
	ldrh	r3, [r6, #2972]
	cmp	r3, r2
	beq	.L1862
	movs	r1, #1
	ldr	r0, .L1883+8
	lsls	r3, r3, #10
	str	r3, [r4, #4052]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #4048]
	adds	r3, r3, #1
	beq	.L1862
	ldrh	r2, [r7]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1862
	ldr	r3, [r7, #4]
	ldr	r2, [r6, #2976]
	cmp	r3, r2
	itttt	hi
	strhi	r3, [r6, #2976]
	ldrhhi	r2, [r6, #2972]
	ldrhhi	r3, [r7, #8]
	strhhi	r2, [r6, #2968]	@ movhi
	it	hi
	strhhi	r3, [r6, #2972]	@ movhi
.L1862:
	ldrh	r0, [r5, #2968]
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r6, r0
	adds	r0, r0, #1
	strh	r0, [r5, #2970]	@ movhi
.L1864:
	sxth	r3, r6
	cmp	r3, #0
	blt	.L1869
	ldrh	r2, [r5, #2968]
	movs	r1, #1
	ldr	r0, .L1883+8
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #4052]
	ldr	r3, [r4, #756]
	mov	r2, r1
	str	r3, [r4, #4056]
	bl	FlashReadPages
	ldr	r3, [r4, #4048]
	adds	r3, r3, #1
	beq	.L1865
	ldrh	r3, [r7]
	cmp	r3, r8
	bne	.L1865
.L1869:
	ldrh	r2, [r7, #10]
	ldrh	r0, [r7, #12]
	ldr	r3, .L1883+4
	strh	r2, [r5, #2974]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1866
	b	.L1867
.L1865:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L1864
.L1866:
	ldr	r2, [r3, #2824]
	cmp	r0, r2
	beq	.L1867
	ldrh	r3, [r3, #2838]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L1867
	cmp	r0, r3
	bcs	.L1867
	bl	FtlSysBlkNumInit
.L1867:
	ldr	r7, .L1883+12
	movs	r6, #0
.L1870:
	ldrh	r3, [r5, #2850]
	cmp	r6, r3
	bcs	.L1882
	ldrh	r2, [r4, #1860]
	ldr	r1, [r4, #4056]
	ldr	r0, [r7, #4]!
	lsls	r2, r2, #2
	mla	r1, r6, r2, r1
	bl	ftl_memcpy
	adds	r6, r6, #1
	b	.L1870
.L1882:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1872:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L1884:
	.align	2
.L1883:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+4048
	.word	.LANCHOR0+2992
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
	ldr	r5, .L1970
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L1970+4
	mov	r1, r6
	ldr	r2, [r5, #2928]
	ldr	r0, [r4, #1852]
	mov	r9, r4
	strh	r6, [r5, #2946]	@ movhi
	lsls	r2, r2, #2
	strh	r6, [r4, #1908]	@ movhi
	bl	ftl_memset
	ldr	r2, [r5, #2928]
	mov	r1, r6
	ldr	r0, [r4, #1824]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2920]
	mov	r1, r6
	ldr	r0, [r4, #1836]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #2920]
	ldr	r0, [r5, #2948]
	mov	r1, r6
	lsls	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #1064
	movs	r1, #255
	movs	r2, #12
	bl	memset
	ldrh	r3, [r5, #2836]
	str	r5, [sp, #12]
	mov	r5, r4
	str	r3, [sp, #4]
.L1886:
	ldr	r6, .L1970
	ldr	r2, [sp, #4]
	ldrh	r3, [r6, #2838]
	mov	r0, r6
	cmp	r3, r2
	bls	.L1928
	ldr	r1, [r4, #1668]
	mov	r10, #36
	ldrh	r8, [r6, #2828]
	movs	r7, #0
	ldr	r2, [r4, #1764]
	str	r1, [sp, #8]
	ldr	r3, [r4, #1664]
	ldrh	r1, [r6, #2910]
	str	r7, [sp]
	str	r1, [sp, #16]
	addw	r1, r6, #2856
	str	r1, [sp, #20]
.L1929:
	uxth	r1, r7
	cmp	r1, r8
	bcs	.L1965
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
	cbnz	r0, .L1887
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
.L1887:
	adds	r7, r7, #1
	b	.L1929
.L1965:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L1891
	mov	r1, r3
	ldr	r0, [r4, #1764]
	movs	r2, #1
	ldr	r8, .L1970
	bl	FlashReadPages
	movs	r3, #0
	str	r3, [sp, #8]
.L1892:
	ldrh	r3, [sp, #8]
	ldr	r2, [sp]
	cmp	r3, r2
	bcs	.L1891
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
	bne	.L1894
	mov	fp, #16
	movw	r3, #65535
.L1893:
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
	bne	.L1895
	ldr	r3, [r9, #1764]
	mov	r2, #-1
	str	r2, [r3, r10]
	b	.L1894
.L1895:
	ldr	r2, [r5, #1764]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L1894
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L1893
.L1894:
	ldr	r3, [r5, #1764]
	ldr	r3, [r3, r10]
	adds	r3, r3, #1
	beq	.L1897
	ldr	r2, [r5, #984]
	ldr	r3, [r7, #4]
	adds	r1, r2, #1
	beq	.L1898
	cmp	r2, r3
	bhi	.L1899
.L1898:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r9, #984]
.L1899:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L1901
	bhi	.L1902
	movw	r3, #61574
	cmp	r2, r3
	bne	.L1900
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #12]
	ldrh	r10, [r3, #2920]
	ldrh	r0, [r2, #2946]
	ldr	r2, [r9, #1836]
	add	lr, r10, #-1
	uxth	r3, lr
	rsb	lr, r0, lr
	str	r2, [sp, #16]
	b	.L1915
.L1902:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L1904
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1900
	mov	r0, r6
	b	.L1964
.L1904:
	ldr	r3, [sp, #12]
	ldrh	r0, [r4, #1908]
	ldr	r2, [r4, #1852]
	ldr	r1, [r3, #2928]
	str	r2, [sp, #16]
	uxth	lr, r1
	add	r3, lr, #-1
	rsb	lr, r0, lr
	add	lr, lr, #-1
	uxth	r3, r3
	sxth	lr, lr
.L1906:
	sxth	ip, r3
	cmp	ip, lr
	ble	.L1966
	ldr	r2, [r7, #4]
	lsl	fp, ip, #2
	mov	r10, r2
	ldr	r2, [sp, #16]
	ldr	r2, [r2, ip, lsl #2]
	cmp	r10, r2
	bls	.L1907
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L1908
	cmp	r0, r1
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r9, #1908]	@ movhi
.L1908:
	movs	r1, #0
	uxth	lr, r3
.L1909:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L1967
	ldr	r0, [r5, #1852]
	sxth	r2, r2
	adds	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	ldr	r0, [r5, #1824]
	add	r10, r0, r2, lsl #1
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2, lsl #1]	@ movhi
	b	.L1909
.L1967:
	ldr	r1, [r7, #4]
	ldr	r2, [r4, #1852]
	str	r1, [r2, fp]
	sxth	r1, r3
	ldr	r2, [r4, #1824]
	cmp	r1, #0
	strh	r6, [r2, ip, lsl #1]	@ movhi
	bge	.L1911
	b	.L1900
.L1907:
	subs	r3, r3, #1
	uxth	r3, r3
	b	.L1906
.L1966:
	cmp	ip, #0
	bge	.L1942
	b	.L1900
.L1911:
	ldrh	r0, [r5, #1908]
	ldr	r2, [r8, #2928]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r1, r2
	bgt	.L1900
.L1942:
	ldr	r2, [r4, #1852]
	sxth	r3, r3
	ldr	r1, [r7, #4]
	adds	r0, r0, #1
	strh	r0, [r4, #1908]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #1824]
	b	.L1962
.L1921:
	ldr	r1, [sp, #16]
	lsl	fp, ip, #2
	ldr	r2, [r7, #4]
	ldr	r1, [r1, ip, lsl #2]
	cmp	r2, r1
	bhi	.L1968
	subs	r3, r3, #1
	uxth	r3, r3
.L1915:
	sxth	ip, r3
	cmp	ip, lr
	bgt	.L1921
	b	.L1920
.L1971:
	.align	2
.L1970:
	.word	.LANCHOR0
	.word	.LANCHOR2
.L1968:
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L1917
	cmp	r0, r10
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r8, #2946]	@ movhi
.L1917:
	movs	r1, #0
	uxth	lr, r3
.L1918:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L1969
	ldr	r0, [r4, #1836]
	sxth	r2, r2
	adds	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	ldr	r0, [r8, #2948]
	add	r10, r0, r2, lsl #1
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2, lsl #1]	@ movhi
	b	.L1918
.L1969:
	ldr	r2, [r5, #1836]
	ldr	r1, [r7, #4]
	str	r1, [r2, fp]
	ldr	r2, .L1972
	ldr	r2, [r2, #2948]
	strh	r6, [r2, ip, lsl #1]	@ movhi
.L1920:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1900
	ldrh	r2, [r8, #2920]
	ldrh	r1, [r8, #2946]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L1900
	adds	r1, r1, #1
	ldr	r2, [r9, #1836]
	strh	r1, [r8, #2946]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r8, #2948]
.L1962:
	strh	r6, [r2, r3, lsl #1]	@ movhi
	b	.L1900
.L1901:
	ldrh	r1, [r9, #1064]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1922
	strh	r6, [r5, #1064]	@ movhi
	b	.L1963
.L1922:
	ldrh	r0, [r5, #1068]
	cmp	r0, r2
	beq	.L1923
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1923:
	ldr	r2, [r9, #1072]
	ldr	r3, [r7, #4]
	cmp	r2, r3
	bcs	.L1924
	ldrh	r3, [r5, #1064]
	strh	r6, [r5, #1064]	@ movhi
	strh	r3, [r5, #1068]	@ movhi
	ldr	r3, [r7, #4]
.L1963:
	str	r3, [r5, #1072]
	b	.L1900
.L1924:
	strh	r6, [r5, #1068]	@ movhi
	b	.L1900
.L1897:
	ldrb	r1, [r8, #76]	@ zero_extendqisi2
	mov	r0, r6
	cbz	r1, .L1925
.L1964:
	movs	r1, #0
.L1925:
	bl	FtlFreeSysBlkQueueIn
.L1900:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L1892
.L1891:
	ldr	r6, [sp, #4]
	adds	r6, r6, #1
	uxth	r3, r6
	str	r3, [sp, #4]
	b	.L1886
.L1928:
	ldr	r3, .L1972+4
	ldr	r5, [r3, #1824]
	ldrh	r2, [r5]
	cbz	r2, .L1930
.L1933:
	ldr	r5, [r0, #2948]
	ldr	r3, .L1972
	ldrh	r2, [r5]
	cbz	r2, .L1931
	b	.L1954
.L1930:
	ldrh	r3, [r3, #1908]
	cmp	r3, #0
	beq	.L1933
	ldr	r6, [r6, #2928]
.L1934:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bcs	.L1933
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L1934
	mov	lr, #0
.L1935:
	ldr	r5, [r0, #2928]
	sxth	r2, r3
	cmp	r2, r5
	bcs	.L1933
	ldr	r5, [r4, #1824]
	subs	r6, r2, r1
	adds	r3, r3, #1
	ldrh	r7, [r5, r2, lsl #1]
	uxth	r3, r3
	strh	r7, [r5, r6, lsl #1]	@ movhi
	ldr	r5, [r4, #1852]
	ldr	r7, [r5, r2, lsl #2]
	str	r7, [r5, r6, lsl #2]
	ldr	r5, [r4, #1824]
	strh	lr, [r5, r2, lsl #1]	@ movhi
	b	.L1935
.L1931:
	ldrh	r1, [r3, #2946]
	cbz	r1, .L1954
	ldrh	r6, [r3, #2920]
.L1938:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bge	.L1954
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L1938
	ldr	r5, .L1972
	mov	ip, #0
.L1939:
	ldrh	r6, [r0, #2920]
	sxth	r2, r3
	cmp	r2, r6
	bge	.L1954
	ldr	r6, [r5, #2948]
	rsb	lr, r1, r2
	adds	r3, r3, #1
	ldrh	r7, [r6, r2, lsl #1]
	uxth	r3, r3
	strh	r7, [r6, lr, lsl #1]	@ movhi
	ldr	r6, [r4, #1836]
	ldr	r7, [r6, r2, lsl #2]
	str	r7, [r6, lr, lsl #2]
	ldr	r6, [r5, #2948]
	strh	ip, [r6, r2, lsl #1]	@ movhi
	b	.L1939
.L1954:
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1973:
	.align	2
.L1972:
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movs	r1, #0
	ldr	r6, .L1993
	ldr	r5, .L1993+4
	ldrh	r2, [r6, #2836]
	ldr	r3, [r5, #1796]
	ldr	r0, [r5, #768]
	lsls	r2, r2, #1
	str	r1, [r5, #4056]
	str	r3, [r5, #4060]
	bl	ftl_memset
	ldrh	r0, [r5, #1064]
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1975
.L1983:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L1975:
	movs	r1, #1
	ldr	r8, .L1993+12
	bl	FtlGetLastWrittenPage
	mov	r7, r5
	uxth	r4, r0
	adds	r0, r0, #1
	strh	r0, [r5, #1066]	@ movhi
.L1977:
	sxth	r3, r4
	cmp	r3, #0
	blt	.L1982
	ldrh	r2, [r5, #1064]
	movs	r1, #1
	ldr	r0, .L1993+8
	orr	r3, r3, r2, lsl #10
	str	r3, [r5, #4052]
	ldr	r3, [r5, #756]
	mov	r2, r1
	str	r3, [r5, #4056]
	bl	FlashReadPages
	ldr	r3, [r5, #4048]
	adds	r3, r3, #1
	beq	.L1978
	ldr	r3, [r7, #756]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1978
	ldr	r3, [r7, #1796]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L1978
.L1982:
	ldr	r4, .L1993+4
	movs	r2, #48
	ldr	r1, [r5, #4056]
	add	r0, r4, #1012
	ldr	r7, .L1993
	bl	ftl_memcpy
	ldrh	r2, [r6, #2836]
	ldr	r1, [r5, #4056]
	ldr	r0, [r5, #768]
	adds	r1, r1, #48
	lsls	r2, r2, #1
	bl	ftl_memcpy
	ldrh	r2, [r6, #2836]
	ldr	r3, [r5, #4056]
	add	r1, r2, #24
	ldr	r0, [r5, #944]
	ubfx	r1, r1, #1, #30
	lsrs	r2, r2, #3
	adds	r2, r2, #4
	add	r1, r3, r1, lsl #2
	bl	ftl_memcpy
	ldrh	r3, [r6, #2944]
	cbz	r3, .L1980
	ldrh	r2, [r7, #2836]
	ldr	r1, [r4, #4056]
	ldr	r0, [r4, #1848]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldrh	r2, [r7, #2936]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	lsls	r2, r2, #2
	add	r1, r1, r3, lsl #2
	bl	ftl_memcpy
	b	.L1980
.L1978:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1977
.L1980:
	ldr	r2, [r5, #1012]
	ldr	r3, .L1993+12
	ldr	r4, .L1993+4
	cmp	r2, r3
	bne	.L1983
	ldrb	r0, [r4, #1022]	@ zero_extendqisi2
	ldrh	r1, [r6, #2850]
	ldrh	r2, [r4, #1020]
	cmp	r0, r1
	ldr	r3, .L1993
	strh	r2, [r4, #1070]	@ movhi
	bne	.L1983
	ldrh	r1, [r3, #2898]
	ldrh	r0, [r3, #2904]
	str	r2, [r4, #4084]
	muls	r1, r2, r1
	str	r1, [r3, #2960]
	muls	r1, r0, r1
	ldrh	r0, [r3, #2974]
	str	r1, [r3, #2940]
	ldr	r1, [r3, #2840]
	subs	r0, r1, r0
	ldrh	r1, [r3, #2828]
	subs	r0, r0, r2
	bl	__aeabi_uidiv
	ldrh	r3, [r4, #1028]
	ldrh	r1, [r4, #1026]
	lsrs	r2, r3, #6
	strh	r2, [r4, #790]	@ movhi
	ldrh	r2, [r4, #1030]
	and	r3, r3, #63
	strb	r3, [r4, #794]
	ldrb	r3, [r4, #1023]	@ zero_extendqisi2
	strh	r2, [r4, #836]	@ movhi
	ldrh	r2, [r4, #1032]
	strb	r3, [r4, #796]
	movw	r3, #65535
	strh	r1, [r4, #788]	@ movhi
	strh	r3, [r4, #1076]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #1078]	@ movhi
	strb	r3, [r4, #1082]
	strb	r3, [r4, #1084]
	str	r3, [r4, #972]
	strh	r0, [r4, #1060]	@ movhi
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #842]
	ldrb	r2, [r4, #1024]	@ zero_extendqisi2
	strh	r0, [r4, #838]	@ movhi
	strb	r2, [r4, #844]
	ldrh	r2, [r4, #1034]
	strh	r2, [r4, #884]	@ movhi
	ldrh	r2, [r4, #1036]
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #890]
	ldrb	r2, [r4, #1025]	@ zero_extendqisi2
	strh	r0, [r4, #886]	@ movhi
	strb	r2, [r4, #892]
	str	r3, [r4, #960]
	ldr	r2, [r4, #1044]
	str	r3, [r4, #952]
	str	r3, [r4, #968]
	str	r3, [r4, #996]
	str	r3, [r4, #1004]
	str	r3, [r4, #964]
	ldr	r3, [r4, #1052]
	str	r2, [r4, #992]
	ldr	r2, [r4, #984]
	cmp	r3, r2
	ldr	r2, [r5, #988]
	it	hi
	strhi	r3, [r4, #984]
	ldr	r3, [r5, #1048]
	cmp	r3, r2
	itt	hi
	ldrhi	r2, .L1993+4
	strhi	r3, [r2, #988]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1986
	ldr	r0, .L1993+16
	bl	make_superblock
.L1986:
	ldrh	r2, [r5, #836]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1987
	ldr	r0, .L1993+20
	bl	make_superblock
.L1987:
	ldrh	r2, [r5, #884]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1988
	ldr	r0, .L1993+24
	bl	make_superblock
.L1988:
	ldrh	r2, [r5, #1076]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1989
	ldr	r0, .L1993+28
	bl	make_superblock
.L1989:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1994:
	.align	2
.L1993:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+4048
	.word	1179929683
	.word	.LANCHOR2+788
	.word	.LANCHOR2+836
	.word	.LANCHOR2+884
	.word	.LANCHOR2+1076
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	1
	.global	FtlGcScanTempBlk
	.thumb
	.thumb_func
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2029
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldrh	r4, [r3, #3000]
	movw	r3, #65535
	mov	r5, r0
	str	r1, [sp, #12]
	cmp	r4, r3
	beq	.L2018
	cbnz	r4, .L1996
	b	.L1997
.L2018:
	movs	r4, #0
.L1996:
	ldr	r3, .L2029+4
	ldr	r2, [sp, #12]
	ldrh	r3, [r3, #2898]
	cmp	r2, r3
	bne	.L1998
.L1997:
	bl	FtlGcPageVarInit
.L1998:
	ldr	r7, .L2029+8
	mov	r10, #0
	mov	r3, #-1
	str	r3, [sp, #4]
	mov	r9, r7
.L2012:
	ldrh	r1, [r5]
	movw	r3, #65535
	movs	r2, #0
	strb	r2, [r5, #8]
	cmp	r1, r3
	beq	.L1999
.L2000:
.L2015:
	ldr	r3, .L2029+4
	movs	r2, #0
	ldr	fp, [r7, #1764]
	mov	ip, #36
	mov	r6, r2
	movw	lr, #65535
	ldrh	r8, [r3, #2828]
	ldr	r3, [r7, #1668]
	str	r3, [sp, #16]
	ldr	r3, .L2029+4
	ldrh	r3, [r3, #2910]
	str	r3, [sp, #20]
	add	r3, r5, #14
	str	r3, [sp, #8]
.L2001:
	uxth	r3, r2
	cmp	r3, r8
	bcs	.L2027
	ldr	r1, [sp, #8]
	ldrh	r3, [r1, #2]!
	cmp	r3, lr
	str	r1, [sp, #8]
	beq	.L2002
	mla	r1, ip, r6, fp
	orr	r3, r4, r3, lsl #10
	str	r3, [r1, #4]
	movs	r3, #0
	str	r3, [r1, #8]
	ldr	r3, [sp, #20]
	muls	r3, r6, r3
	it	mi
	addmi	r3, r3, #3
	adds	r6, r6, #1
	bic	r3, r3, #3
	uxth	r6, r6
	mov	r0, r3
	ldr	r3, [sp, #16]
	add	r0, r0, r3
	str	r0, [r1, #12]
.L2002:
	adds	r2, r2, #1
	b	.L2001
.L2027:
	ldr	r0, [r7, #1764]
	mov	r1, r6
	movs	r2, #0
	mov	fp, #0
	bl	FlashReadPages
.L2005:
	uxth	r3, fp
	cmp	r3, r6
	bcs	.L2028
	movs	r3, #36
	mul	r8, r3, fp
	ldr	r3, .L2029+8
	ldr	ip, [r3, #1764]
	add	r1, ip, r8
	str	r1, [sp, #16]
	str	ip, [sp, #20]
	ldr	r3, [r1, #4]
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #8]
	bl	P2V_plane
	ldr	ip, [sp, #20]
	mov	r2, r0
	ldr	r3, [sp, #8]
	ldr	r0, [ip, r8]
	cbnz	r0, .L2006
	ldr	r1, [sp, #16]
	movw	r0, #65535
	add	fp, fp, #1
	ldr	lr, [r1, #12]
	ldrh	r1, [lr]
	cmp	r1, r0
	bne	.L2007
	ldr	r3, .L2029+8
	movs	r1, #1
	str	r1, [r3, #1924]
	b	.L1999
.L2007:
	ldr	r0, [lr, #12]
	mov	r1, r3
	ldr	r2, [lr, #8]
	bl	FtlGcUpdatePage
	b	.L2005
.L2006:
	mov	r2, r3
	ldr	r0, .L2029+12
	ldrh	r1, [r5]
	bl	printk
	ldr	r2, [r9, #948]
	ldrh	r3, [r5]
	cbnz	r2, .L2008
	ldr	r2, .L2029+4
	ldrb	r2, [r2, #76]	@ zero_extendqisi2
	cbz	r2, .L2009
.L2008:
	ldr	r2, [r7, #748]
	ldrh	r2, [r2, r3, lsl #1]
	cmp	r2, #119
	bls	.L2010
.L2009:
	ldr	r2, [r7, #1764]
	ldr	r2, [r2, r8]
	adds	r2, r2, #1
	bne	.L2011
.L2010:
	ldr	r2, [r7, #1764]
	add	r8, r8, r2
	ldr	r2, [r8, #4]
	str	r2, [sp, #4]
.L2011:
	ldr	r2, [r9, #768]
	movs	r4, #0
	strh	r4, [r2, r3, lsl #1]	@ movhi
	ldrh	r0, [r5]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r5]	@ movhi
	bl	FtlGcPageVarInit
	b	.L2012
.L2028:
	ldr	r3, [sp, #12]
	add	r10, r10, #1
	adds	r4, r4, #1
	cmp	r10, r3
	uxth	r4, r4
	bcs	.L2014
.L2016:
	ldr	r3, .L2029+4
	ldrh	r3, [r3, #2898]
	cmp	r3, r4
	bhi	.L2015
	movs	r2, #0
	b	.L1999
.L2014:
	ldr	r2, .L2029
	movw	r1, #65535
	ldrh	r3, [r2, #3000]
	cmp	r3, r1
	beq	.L2016
	add	r3, r3, r10
	strh	r3, [r2, #3000]	@ movhi
	ldr	r3, .L2029+4
	ldrh	r3, [r3, #2898]
	cmp	r3, r4
	bls	.L2016
	b	.L2017
.L1999:
	ldr	r3, .L2029
	movw	r1, #65535
	strh	r4, [r5, #2]	@ movhi
	mov	r0, r5
	strb	r2, [r5, #6]
	strh	r1, [r3, #3000]	@ movhi
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L2017:
	ldr	r0, [sp, #4]
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2030:
	.align	2
.L2029:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC105
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
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
	ldr	r5, .L2086
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	r6, [r5, #1788]
	ldrb	r7, [r5]	@ zero_extendqisi2
	ldrb	r6, [r6, #19]	@ zero_extendqisi2
	str	r6, [sp, #16]
	ldr	r6, .L2086+4
	ldrb	r6, [r6, #265]	@ zero_extendqisi2
	str	r6, [sp, #12]
	cbz	r7, .L2032
	bl	FlashProgSlc2KPages
	b	.L2033
.L2046:
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
	ldrb	r2, [r5, #1616]	@ zero_extendqisi2
	ldr	r3, [sp, #32]
	mov	r10, r0
	cmp	r3, r2
	bcc	.L2034
	mov	r3, #-1
	str	r3, [r4, r8]
.L2035:
	adds	r7, r7, #1
.L2032:
	ldr	r3, [sp, #8]
	ldr	r6, .L2086
	cmp	r7, r3
	bcc	.L2046
	b	.L2084
.L2034:
	add	r3, r5, r3, lsl #4
	ldrb	r1, [r5, #2820]	@ zero_extendqisi2
	ldr	r3, [r3, #1668]
	cmp	r1, #0
	it	eq
	moveq	r10, #0
	cbz	r3, .L2037
	cmp	r2, #1
	bne	.L2038
	ldr	r0, [r5, #1828]
	bl	NandcIqrWaitFlashReady
.L2038:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L2037:
	ldr	r3, [sp, #32]
	movs	r0, #0
	add	r2, r5, r3, lsl #4
	addw	r1, r2, #1668
	str	r0, [r2, #1672]
	ldr	r0, [sp, #28]
	str	fp, [r2, #1668]
	str	r0, [r2, #1664]
	cmp	r10, #0
	beq	.L2039
	add	r2, r8, #36
	add	r2, r2, r4
	str	r2, [r1, #4]
.L2039:
	adds	r2, r5, r3
	add	r3, r5, r3, lsl #4
	ldrb	r6, [r2, #1620]	@ zero_extendqisi2
	strb	r6, [r3, #1660]
	mov	r0, r6
	ldrb	r3, [r5, #1616]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2040
	bl	NandcWaitFlashReady
	b	.L2041
.L2040:
	bl	NandcFlashCs
	mov	r0, r6
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #1628]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	bl	NandcFlashDeCs
.L2041:
	ldr	r3, [sp, #16]
	subs	r3, r3, #1
	cmp	r3, #6
	bhi	.L2042
	adds	r3, r5, r6
	ldrb	r3, [r3, #2756]	@ zero_extendqisi2
	cbz	r3, .L2042
	mov	r0, r6
	ldrb	r1, [r5, #1903]	@ zero_extendqisi2
	ldr	r2, .L2086+8
	movs	r3, #0
	bl	HynixSetRRPara
.L2042:
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r6
	bne	.L2043
	ldrb	r3, [r5, #76]	@ zero_extendqisi2
	cbz	r3, .L2043
	bl	flash_enter_slc_mode
	b	.L2044
.L2043:
	bl	flash_exit_slc_mode
.L2044:
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
	beq	.L2045
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	mov	r0, r6
	add	r8, r8, #36
	add	r8, r8, r4
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #1628]
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
.L2045:
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2035
.L2084:
	ldr	r0, [r6, #1828]
	movs	r5, #0
	bl	NandcIqrWaitFlashReady
	addw	r8, r6, #1660
	mov	r7, r6
.L2047:
	ldrb	r3, [r6, #1616]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L2085
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2048
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbz	r3, .L2048
	lsls	r3, r5, #4
	ldrb	r0, [r8, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2048:
	adds	r5, r5, #1
	b	.L2047
.L2085:
	ldr	r3, [sp, #20]
	cbnz	r3, .L2050
.L2058:
	movs	r0, #0
	b	.L2033
.L2050:
	ldr	r7, .L2086+12
	mov	r8, #0
	mov	r10, r7
.L2051:
	ldr	r3, [sp, #8]
	cmp	r8, r3
	beq	.L2058
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L2052
	ldr	r1, [r4, #4]
	ldr	r0, .L2086+16
	bl	printk
	b	.L2053
.L2052:
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
	ldr	r2, [r7, #4020]
	movs	r3, #0
	str	r3, [r2]
	ldr	r2, [r7, #4024]
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
	ldr	r3, [r7, #4020]
	str	r3, [sp, #44]
	ldr	r3, [r7, #4024]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r5, [sp, #36]
	adds	r3, r5, #1
	bne	.L2054
	ldr	r0, .L2086+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L2054:
	ldr	r3, [r4, #12]
	cbz	r3, .L2055
	ldr	r2, [r3]
	ldr	r3, [r10, #4024]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2055
	ldr	r0, .L2086+24
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2055:
	ldr	r3, [r4, #8]
	cbz	r3, .L2053
	ldr	r2, [r3]
	ldr	r3, [r10, #4020]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2053
	ldr	r0, .L2086+28
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2053:
	add	r8, r8, #1
	adds	r4, r4, #36
	b	.L2051
.L2033:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2087:
	.align	2
.L2086:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1906
	.word	.LANCHOR2
	.word	.LC101
	.word	.LC102
	.word	.LC103
	.word	.LC104
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
	ldr	r3, .L2091
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, r0
	ldr	r2, [r3, #4004]
	.pad #108
	sub	sp, sp, #108
	cmp	r0, r2
	bcc	.L2090
	ldr	r5, [r3, #4012]
	add	r0, sp, #40
	movs	r1, #165
	movs	r2, #32
	str	r0, [sp, #16]
	lsls	r4, r4, #10
	str	r5, [sp, #12]
	bl	memset
	mov	r0, r5
	movs	r1, #90
	movs	r2, #8
	bl	memset
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
	b	.L2089
.L2090:
	movs	r4, #0
.L2089:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, pc}
.L2092:
	.align	2
.L2091:
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
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r5, .L2151
	movs	r1, #1
	ldr	r4, .L2151+4
	ldr	r0, .L2151+8
	ldr	r3, [r5, #4016]
	ldrh	r8, [r4, #1876]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #1878]
	smulbb	r8, r8, r3
	ldr	r3, [r4, #1788]
	ldrb	r3, [r3, #24]	@ zero_extendqisi2
	uxth	r8, r8
	str	r3, [sp, #24]
	ldrh	r3, [r4, #4]
	str	r3, [sp, #16]
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r4, .L2151+4
	cmp	r3, #1
	itttt	eq
	ldreq	r3, [sp, #16]
	lsleq	r3, r3, #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r5, #4016]
	mov	r1, #4096
	ldr	r5, .L2151
	bl	__memzero
	lsr	r3, r8, #4
	str	r3, [sp, #28]
	movs	r3, #0
	str	r3, [sp, #8]
.L2095:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #1616]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2147
	add	r3, r5, r7, lsl #1
	ldrh	r6, [r3, #4032]
	cmp	r6, #0
	bne	.L2125
	ldrh	r2, [r4, #1884]
	mov	r1, r6
	ldr	r0, [r5, #3984]
	mov	r9, r6
	lsls	r2, r2, #9
	bl	ftl_memset
	adds	r3, r4, r7
	ldrb	r10, [r3, #1620]	@ zero_extendqisi2
	lsls	r3, r7, #2
	add	fp, r4, r3
	str	r6, [sp, #4]
	str	r3, [sp, #32]
.L2097:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L2107
	movs	r3, #255
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	lsls	r1, r3, #31
	bpl	.L2099
	ldr	r3, [fp, #1628]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r3, r6
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L2099
	ldr	r1, [r4, #4]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r1, r3
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	ands	r3, r3, r2
	strb	r3, [sp, #42]
.L2099:
	ldr	r3, [sp, #24]
	lsls	r2, r3, #30
	bpl	.L2101
	ldr	r3, [r4, #1788]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1628]
	subs	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2101:
	ldr	r3, [r4, #1788]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L2102
	cmp	r2, #8
	bne	.L2103
.L2102:
	cbz	r3, .L2124
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	clz	r0, r0
	lsrs	r0, r0, #5
	b	.L2104
.L2103:
	cmp	r3, #255
	bne	.L2124
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	it	ne
	movne	r0, #1
	b	.L2104
.L2124:
	movs	r0, #1
.L2104:
	ldr	r3, [sp, #24]
	lsls	r3, r3, #29
	bpl	.L2105
	ldr	r3, .L2151+4
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #1628]
	add	r1, r1, r6
	bl	SandiskProgTestBadBlock
.L2105:
	cbz	r0, .L2106
	mov	r1, r7
	ldr	r2, [sp, #4]
	ldr	r0, .L2151+12
	add	r9, r9, #1
	bl	printk
	ldr	r2, [r5, #3984]
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
	ldrb	r3, [r4, #1616]	@ zero_extendqisi2
	muls	r3, r2, r3
	cmp	r9, r3
	bgt	.L2107
.L2106:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r6, r6, r3
	b	.L2097
.L2107:
	mov	r2, r9
	ldr	r0, .L2151+16
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #1616]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	muls	r3, r2, r3
	cmp	r9, r3
	blt	.L2109
	ldrh	r2, [r4, #1884]
	movs	r1, #0
	ldr	r0, [r5, #3984]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2109:
	cmp	r7, #0
	bne	.L2111
	ldrh	r10, [r5, #4004]
	mov	r9, #1
	mov	r6, r7
.L2112:
	ldr	r3, .L2151+4
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2148
	mov	r0, r10
	bl	FlashTestBlk
	cbz	r0, .L2113
	mov	r1, r10
	ldr	r0, .L2151+20
	bl	printk
	ldr	r2, [r5, #3984]
	lsr	r0, r10, #5
	adds	r6, r6, #1
	and	r3, r10, #31
	ldr	r1, [r2, r0, lsl #2]
	uxth	r6, r6
	lsl	r3, r9, r3
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2113:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2112
.L2148:
	add	fp, r8, #-1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L2115:
	cmp	fp, r9
	ble	.L2149
	mov	r0, fp
	bl	FlashTestBlk
	cbz	r0, .L2116
	mov	r1, fp
	ldr	r0, .L2151+20
	bl	printk
	ldr	r2, [r5, #3984]
	lsr	r0, fp, #5
	and	r3, fp, #31
	lsl	r3, r10, r3
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2116:
	add	fp, fp, #-1
	uxth	fp, fp
	b	.L2115
.L2149:
	ldr	r3, .L2151+4
	ldr	r2, [r5, #4004]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	subs	r3, r3, r2
	cmp	r6, r3
	bcc	.L2111
	ldrh	r2, [r4, #1884]
	movs	r1, #0
	ldr	r0, [r5, #3984]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2111:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	add	r10, r8, #-1
	add	r9, r5, r7, lsl #1
	uxth	r10, r10
	mul	r6, r8, r6
.L2119:
	mov	r1, r7
	ldr	r0, .L2151+24
	mov	r2, r10
	bl	printk
	ldr	r1, [r5, #3984]
.L2120:
	lsr	r3, r10, #5
	and	r2, r10, #31
	ldr	r3, [r1, r3, lsl #2]
	lsrs	r3, r3, r2
	ands	r3, r3, #1
	beq	.L2150
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2120
.L2150:
	ldr	r1, [sp, #20]
	movw	r2, #61664
	strh	r10, [r9, #4032]	@ movhi
	add	r0, sp, #44
	strh	r2, [r1]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	movs	r1, #1
	ldr	r3, [r5, #3984]
	mov	r2, r1
	str	r3, [sp, #52]
	ldr	r3, [r5, #4016]
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
	cbz	r3, .L2125
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2119
.L2125:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2095
.L2147:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2152:
	.align	2
.L2151:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC106
	.word	.LC107
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.fnend
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	1
	.global	FtlLowFormatEraseBlock
	.thumb
	.thumb_func
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L2205
	str	r0, [sp, #8]
	str	r1, [sp, #4]
	ldr	r2, [r4, #740]
	cmp	r2, #0
	bne	.L2182
	ldr	r3, .L2205+4
	mov	r9, #36
	mov	r10, r2
	mov	r5, r2
	mov	r7, r2
	mov	fp, r4
	ldrb	r1, [r3, #76]	@ zero_extendqisi2
	mov	r6, r3
	str	r0, [r3, #2964]
	str	r1, [sp, #12]
.L2155:
	ldrh	r1, [r6, #2828]
	uxth	r3, r10
	cmp	r1, r3
	bls	.L2200
	mul	r1, r9, r3
	ldr	r0, [r4, #744]
	movs	r2, #0
	str	r2, [r0, r1]
	ldr	r2, .L2205+4
	ldr	r1, [sp, #8]
	add	r3, r3, r2
	ldrb	r0, [r3, #2856]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp, #4]
	mov	r8, r0
	cbz	r3, .L2156
	bl	IsBlkInVendorPart
	cbnz	r0, .L2157
.L2156:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2158
	ldr	r1, [r4, #744]
	lsl	r3, r8, #10
	mla	r1, r9, r5, r1
	str	r3, [r1, #4]
	str	r0, [r1, #8]
	ldrh	r3, [r6, #2910]
	muls	r3, r5, r3
	ldr	r0, [fp, #1800]
	it	mi
	addmi	r3, r3, #3
	adds	r5, r5, #1
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
	uxth	r5, r5
	b	.L2157
.L2158:
	adds	r7, r7, #1
	uxth	r7, r7
.L2157:
	add	r10, r10, #1
	b	.L2155
.L2200:
	mov	r8, r7
	cbnz	r5, .L2161
	mov	r0, r7
	b	.L2154
.L2161:
	ldr	r3, [sp, #12]
	mov	r2, r5
	ldr	r0, [r4, #744]
	movs	r7, #0
	adds	r9, r3, #0
	mov	r10, #36
	it	ne
	movne	r9, #1
	mov	r1, r9
	bl	FlashEraseBlocks
.L2162:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L2201
	mul	r3, r10, r7
	ldr	r2, [r4, #744]
	adds	r0, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L2163
	ldr	r0, [r0, #4]
	add	r8, r8, #1
	ubfx	r0, r0, #10, #16
	uxth	r8, r8
	bl	FtlBbmMapBadBlock
.L2163:
	adds	r7, r7, #1
	b	.L2162
.L2201:
	ldr	r3, [sp, #4]
	cbnz	r3, .L2165
	movs	r3, #6
	uxth	r5, r9
	str	r3, [sp, #12]
	mov	fp, #1
	b	.L2166
.L2165:
	ldrb	r3, [r6, #76]	@ zero_extendqisi2
	ldrh	fp, [r6, #2900]
	cbnz	r3, .L2183
	lsr	r3, fp, #2
	movs	r5, #1
	str	r3, [sp, #12]
	b	.L2166
.L2183:
	movs	r5, #1
	str	r5, [sp, #12]
.L2166:
	movs	r7, #0
.L2176:
	mov	r9, #0
	mov	r6, r9
.L2167:
	ldr	r3, .L2205+4
	ldrh	r2, [r3, #2828]
	uxth	r3, r9
	cmp	r2, r3
	bls	.L2202
	movs	r2, #36
	ldr	r1, [r4, #744]
	movs	r0, #0
	muls	r2, r3, r2
	str	r0, [r1, r2]
	ldr	r2, .L2205+4
	ldr	r1, [sp, #8]
	add	r3, r3, r2
	ldrb	r0, [r3, #2856]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp, #4]
	mov	r10, r0
	cbz	r3, .L2168
	bl	IsBlkInVendorPart
	cbnz	r0, .L2169
.L2168:
	mov	r0, r10
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2169
	ldr	r2, [r4, #744]
	movs	r3, #36
	ldr	r1, .L2205
	mla	r2, r3, r6, r2
	add	r3, r7, r10, lsl #10
	ldr	r1, [r1, #1792]
	str	r3, [r2, #4]
	ldr	r3, [r4, #1788]
	str	r3, [r2, #8]
	ldr	r3, .L2205+4
	ldrh	r3, [r3, #2910]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r6, r6
	add	r3, r3, r1
	str	r3, [r2, #12]
.L2169:
	add	r9, r9, #1
	b	.L2167
.L2202:
	cmp	r6, #0
	beq	.L2181
	ldr	r0, [r4, #744]
	mov	r1, r6
	mov	r2, r5
	movs	r3, #1
	bl	FlashProgPages
	mov	r9, #0
	mov	r10, #36
.L2173:
	uxth	r3, r9
	cmp	r3, r6
	bcs	.L2203
	mul	r3, r10, r9
	ldr	r2, [r4, #744]
	adds	r0, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L2174
	ldr	r0, [r0, #4]
	add	r8, r8, #1
	ubfx	r0, r0, #10, #16
	uxth	r8, r8
	bl	FtlBbmMapBadBlock
.L2174:
	add	r9, r9, #1
	b	.L2173
.L2203:
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	uxth	r7, r7
	cmp	r7, fp
	bcc	.L2176
	movs	r7, #0
	mov	r9, #36
.L2177:
	uxth	r3, r7
	cmp	r3, r6
	bcs	.L2204
	ldr	r3, [sp, #4]
	cbz	r3, .L2178
	mul	r3, r9, r7
	ldr	r2, [r4, #744]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L2178
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2178:
	adds	r7, r7, #1
	b	.L2177
.L2204:
	ldr	r3, [sp, #8]
	cmp	r3, #63
	bls	.L2180
	ldr	r3, [sp, #4]
	cbz	r3, .L2181
.L2180:
	ldr	r0, [r4, #744]
	mov	r1, r5
	mov	r2, r6
	bl	FlashEraseBlocks
.L2181:
	mov	r0, r8
	b	.L2154
.L2182:
	movs	r0, #0
.L2154:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2206:
	.align	2
.L2205:
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	ldr	r4, .L2219
	ldr	r5, [r4, #740]
	cmp	r5, #0
	bne	.L2209
	ldr	r7, .L2219+4
	mov	r1, r5
	ldr	r0, [r4, #756]
	ldr	r3, [r4, #1796]
	add	r8, r7, #2992
	ldrh	r2, [r7, #2908]
	str	r0, [r4, #4056]
	str	r3, [r4, #4060]
	bl	ftl_memset
.L2210:
	ldrh	r3, [r7, #2850]
	ldr	r6, .L2219+4
	cmp	r5, r3
	bge	.L2218
	ldrh	r2, [r4, #1860]
	ldr	r3, [r4, #4056]
	ldr	r1, [r8, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L2210
.L2218:
	ldr	r8, [r4, #4060]
	movs	r1, #255
	movs	r2, #16
	mov	r9, #0
	ldr	r10, .L2219
	mov	r5, r6
	mov	r0, r8
	mov	fp, r9
	bl	memset
	movw	r3, #61649
	strh	r3, [r8]	@ movhi
	ldr	r3, [r6, #2976]
	str	r3, [r8, #4]
	ldrh	r3, [r6, #2968]
	strh	r3, [r8, #2]	@ movhi
	ldrh	r3, [r6, #2972]
	strh	r3, [r8, #8]	@ movhi
	ldrh	r3, [r6, #2974]
	strh	r3, [r8, #10]	@ movhi
	ldr	r3, [r6, #2824]
	strh	r3, [r8, #12]	@ movhi
.L2212:
	ldr	r3, [r4, #756]
	movs	r6, #0
	ldrh	r1, [r7, #2968]
	ldrh	r2, [r7, #2970]
	str	r3, [r4, #4056]
	ldr	r3, [r4, #1796]
	str	r6, [r4, #4048]
	str	r3, [r4, #4060]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r8, #10]
	str	r3, [r4, #4052]
	ldrh	r3, [r7, #2972]
	str	r0, [sp]
	ldr	r0, .L2219+8
	bl	printk
	movs	r1, #1
	ldr	r0, .L2219+12
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r7, #2900]
	ldrh	r2, [r7, #2970]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L2213
	ldr	r3, [r5, #2976]
	movs	r1, #1
	ldrh	r2, [r5, #2968]
	adds	r3, r3, #1
	ldr	r0, [r10, #744]
	str	r3, [r5, #2976]
	str	r3, [r8, #4]
	ldrh	r3, [r5, #2972]
	strh	r2, [r8, #8]	@ movhi
	strh	r2, [r5, #2972]	@ movhi
	mov	r2, r1
	strh	r3, [r5, #2968]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r10, #4052]
	str	r3, [r0, #4]
	strh	r6, [r5, #2970]	@ movhi
	bl	FlashEraseBlocks
	movs	r1, #1
	ldr	r0, .L2219+12
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L2213:
	ldrh	r3, [r7, #2970]
	ldr	r6, .L2219
	adds	r3, r3, #1
	strh	r3, [r7, #2970]	@ movhi
	ldr	r3, [r4, #4048]
	adds	r3, r3, #1
	bne	.L2214
	add	r9, r9, #1
	ldr	r0, .L2219+16
	ldr	r1, [r10, #4052]
	uxth	r9, r9
	bl	printk
	cmp	r9, #3
	bls	.L2212
	ldr	r0, .L2219+20
	mov	r2, r9
	ldr	r1, [r6, #4052]
	bl	printk
	movs	r3, #1
	str	r3, [r6, #740]
	b	.L2209
.L2214:
	cmp	fp, #0
	bne	.L2209
	mov	fp, #1
	b	.L2212
.L2209:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2220:
	.align	2
.L2219:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC111
	.word	.LANCHOR2+4048
	.word	.LC112
	.word	.LC113
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
	ldr	r4, .L2277
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r4, #740]
	cmp	r3, #0
	bne	.L2222
	ldr	r10, .L2277+16
	mov	r7, r4
.L2223:
	ldr	r3, .L2277+4
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r3
	bne	.L2224
	ldrh	lr, [r7, #784]
	ldr	r6, [r7, #1652]
	lsr	r1, lr, #1
	mul	r0, r6, lr
	adds	r3, r1, #1
	add	r3, r3, r0, lsr #2
	ldr	r0, [r7, #948]
	uxth	r3, r3
	cbz	r0, .L2247
	ldr	r0, [r7, #1008]
	cmp	r0, #29
	bhi	.L2247
	cmp	r0, #2
	bls	.L2252
	tst	lr, #1
	beq	.L2225
	cbz	r6, .L2249
	b	.L2225
.L2224:
	cmp	r2, #1
	bne	.L2252
	ldrh	r3, [r10, #2848]
	cmp	r3, #1
	beq	.L2252
	ldrb	r3, [r10, #76]	@ zero_extendqisi2
	cbnz	r3, .L2252
	ldrh	r0, [r7, #784]
	ldr	r3, [r7, #948]
	lsrs	r1, r0, #3
	cbz	r3, .L2225
	ldr	r3, [r7, #1008]
	cmp	r3, #1
	bhi	.L2225
	rsb	r0, r0, r0, lsl #3
	ubfx	r1, r0, #3, #16
	b	.L2225
.L2247:
	mov	r1, r3
.L2225:
	cbz	r1, .L2226
	subs	r1, r1, #1
	uxth	r1, r1
	b	.L2226
.L2249:
	mov	r1, r6
	b	.L2226
.L2252:
	movs	r1, #0
.L2226:
	ldr	r0, .L2277+8
	bl	List_pop_index_node
	ldrh	r3, [r4, #784]
	subs	r3, r3, #1
	strh	r3, [r4, #784]	@ movhi
	uxth	r6, r0
	mov	r0, r5
	strh	r6, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbz	r3, .L2227
	ldr	r0, [r7, #744]
	movs	r2, #36
	ldrh	ip, [r10, #2828]
	mov	r8, #0
	add	lr, r5, #14
	ldr	r9, .L2277+16
	mov	r3, r0
	mla	r1, r2, ip, r0
	mov	ip, r8
	str	r1, [sp]
	b	.L2228
.L2227:
	ldr	r3, [r7, #768]
	b	.L2272
.L2228:
	ldr	r1, [sp]
	cmp	r3, r1
	beq	.L2274
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
	b	.L2228
.L2274:
	ldr	r3, [r4, #948]
	cbz	r3, .L2232
	ldr	r3, .L2277+12
	cmp	r5, r3
	bne	.L2232
	ldr	r3, [r7, #748]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #30
	itt	hi
	movhi	r3, #0
	strbhi	r3, [r7, #796]
.L2232:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #748]
	cbnz	r3, .L2233
	ldrh	r1, [r2, r6, lsl #1]
	cbz	r1, .L2253
	ldrh	r3, [r9, #2888]
	add	r3, r3, r1
	uxth	r3, r3
	b	.L2234
.L2253:
	movs	r3, #2
.L2234:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	mov	r0, r6
	ldr	r3, [r4, #992]
	movs	r1, #0
	adds	r3, r3, #1
	str	r3, [r4, #992]
	bl	ftl_set_blk_mode
	b	.L2235
.L2233:
	ldrh	r3, [r2, r6, lsl #1]
	lsrs	r0, r6, #5
	and	r1, r6, #31
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #996]
	ldr	r2, [r4, #944]
	adds	r3, r3, #1
	str	r3, [r4, #996]
	movs	r3, #1
	lsls	r3, r3, r1
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2235:
	ldr	r3, [r4, #748]
	ldr	r2, [r4, #1004]
	ldr	r0, [r4, #992]
	ldrh	r3, [r3, r6, lsl #1]
	ldrh	r1, [r9, #2836]
	cmp	r3, r2
	ldrh	r2, [r9, #2888]
	it	hi
	strhi	r3, [r7, #1004]
	ldr	r3, [r4, #996]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #1816]
	ldr	r1, [r4, #744]
	ldr	r3, [r2, #16]
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r3, #36
	adds	r2, r1, #4
	mla	r3, r3, r8, r1
	adds	r3, r3, #40
	str	r0, [r4, #1000]
.L2237:
	adds	r2, r2, #36
	cmp	r2, r3
	beq	.L2275
	ldr	r1, [r2, #-36]
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r2, #-36]
	b	.L2237
.L2275:
	ldrb	r3, [r9, #76]	@ zero_extendqisi2
	cbz	r3, .L2239
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r8
	ldr	r0, [r4, #744]
	cmp	r3, #1
	ite	eq
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2239:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r8
	ldr	r0, [r4, #744]
	mov	fp, #0
	bl	FlashEraseBlocks
	add	r1, r5, #16
	mov	ip, #36
	mov	r2, fp
.L2241:
	uxth	r3, fp
	cmp	r3, r8
	bcs	.L2276
	mul	r3, ip, fp
	ldr	lr, [r4, #744]
	add	r0, lr, r3
	ldr	r3, [lr, r3]
	cmp	r3, #-1
	bne	.L2242
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
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	subs	r3, r3, #1
	strb	r3, [r5, #7]
.L2242:
	add	fp, fp, #1
	adds	r1, r1, #2
	b	.L2241
.L2276:
	cbz	r2, .L2244
	mov	r0, r6
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2244:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L2245
	ldr	r3, [r4, #768]
.L2272:
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L2223
.L2245:
	ldrh	r2, [r9, #2898]
	strh	r6, [r5]	@ movhi
	smulbb	r3, r2, r3
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #984]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #984]
	ldr	r2, [r4, #768]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
.L2222:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2278:
	.align	2
.L2277:
	.word	.LANCHOR2
	.word	.LANCHOR2+884
	.word	.LANCHOR2+780
	.word	.LANCHOR2+788
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
	ldr	r5, .L2292
	str	r0, [sp]
	ldrh	r3, [r5, #1698]
	cmp	r3, #0
	beq	.L2281
	ldr	r7, .L2292+4
	mov	r10, #0
	mov	r8, r5
.L2280:
	ldrh	r2, [r7, #2828]
	uxth	r3, r10
	cmp	r2, r3
	bls	.L2289
	add	r3, r3, r7
	ldr	r1, [sp]
	mov	fp, #0
	ldrb	r0, [r3, #2856]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r2, .L2292+8
	mov	r9, r0
.L2282:
	ldrh	r3, [r5, #1698]
	uxth	r4, fp
	cmp	r3, r4
	bls	.L2290
	lsls	r6, r4, #1
	add	r3, r8, r6
	ldrh	r3, [r3, #1700]
	cmp	r3, r9
	bne	.L2283
	mov	r1, r9
	ldr	r0, .L2292+12
	str	r2, [sp, #4]
	bl	printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r8, #1698]
	ldr	r2, [sp, #4]
	adds	r3, r2, r6
.L2284:
	cmp	r4, r1
	bcs	.L2291
	ldrh	r0, [r3, #2]
	adds	r4, r4, #1
	uxth	r4, r4
	strh	r0, [r3], #2	@ movhi
	b	.L2284
.L2291:
	subs	r1, r1, #1
	strh	r1, [r5, #1698]	@ movhi
.L2283:
	add	fp, fp, #1
	b	.L2282
.L2290:
	add	r10, r10, #1
	b	.L2280
.L2289:
	bl	FtlGcReFreshBadBlk
.L2281:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2293:
	.align	2
.L2292:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+1700
	.word	.LC114
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
	ldr	r2, .L2303
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, [r2, #768]
	mov	r5, r2
	ldrh	r3, [r3, r0, lsl #1]
	cbnz	r3, .L2295
	ldrh	r1, [r2, #1076]
	cmp	r1, r0
	bne	.L2296
	movw	r3, #65535
	strh	r3, [r2, #1076]	@ movhi
	b	.L2297
.L2296:
	ldrh	r1, [r2, #788]
	cmp	r1, r0
	beq	.L2302
	ldrh	r1, [r2, #836]
	cmp	r1, r0
	beq	.L2302
	ldrh	r2, [r2, #884]
	cmp	r2, r0
	beq	.L2302
.L2297:
	mov	r1, r4
	ldr	r0, .L2303+4
	bl	List_remove_node
	ldrh	r3, [r5, #776]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #776]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	movs	r0, #1
	pop	{r3, r4, r5, pc}
.L2295:
	bl	List_update_data_list
.L2302:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L2304:
	.align	2
.L2303:
	.word	.LANCHOR2
	.word	.LANCHOR2+764
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
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r3, #65535
	cmp	r0, r3
	mov	r5, r0
	ldr	r3, .L2311
	beq	.L2306
	ldr	r2, [r3, #768]
	ldrh	r4, [r2, r0, lsl #1]
	cbnz	r4, .L2307
	mov	r1, r5
	mov	r2, r4
	ldr	r0, .L2311+4
	bl	printk
	mov	r0, r4
	pop	{r3, r4, r5, pc}
.L2307:
	subs	r4, r4, #1
	strh	r4, [r2, r0, lsl #1]	@ movhi
.L2306:
	ldrh	r0, [r3, #1916]
	movw	r3, #65535
	ldr	r4, .L2311
	cmp	r0, r3
	bne	.L2309
	strh	r5, [r4, #1916]	@ movhi
	b	.L2310
.L2309:
	cmp	r0, r5
	beq	.L2310
	bl	update_vpc_list
	strh	r5, [r4, #1916]	@ movhi
	adds	r0, r0, #0
	it	ne
	movne	r0, #1
	pop	{r3, r4, r5, pc}
.L2310:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L2312:
	.align	2
.L2311:
	.word	.LANCHOR2
	.word	.LC115
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
	beq	.L2313
	ldrh	r3, [r0]
	movw	r5, #65535
	cmp	r3, r5
	beq	.L2313
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movs	r7, #0
	ldr	r6, .L2325
	adds	r3, r3, #8
	ldrh	r3, [r0, r3, lsl #1]
.L2317:
	cmp	r3, r5
	bne	.L2324
.L2319:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r6, #2828]
	adds	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r2, r3
	itttt	eq
	strbeq	r7, [r4, #6]
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r3, [r4, r3, lsl #1]
	b	.L2317
.L2324:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2320
	ldrb	r3, [r6, #76]	@ zero_extendqisi2
	cbnz	r3, .L2320
	ldrh	r3, [r4, #2]
	add	r3, r6, r3, lsl #1
	ldrh	r3, [r3, #592]
	cmp	r3, r5
	bne	.L2320
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2319
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L2320:
	ldr	r2, .L2325
	ldrb	r3, [r2, #76]	@ zero_extendqisi2
	cbz	r3, .L2313
	cmp	r1, #1
	bne	.L2313
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #2900]
	cmp	r1, r3
	bcc	.L2313
	ldr	r3, .L2325+4
	ldrh	r0, [r4]
	ldrh	r5, [r4, #4]
	ldr	r1, [r3, #768]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r5
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #2898]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2313:
	pop	{r3, r4, r5, r6, r7, pc}
.L2326:
	.align	2
.L2325:
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
	movs	r6, #0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	mov	r4, r0
	strb	r6, [r0, #10]
	movw	r8, #65535
	adds	r3, r3, #8
	ldr	r5, .L2344
	ldrh	r2, [r0, r3, lsl #1]
.L2328:
	cmp	r2, r8
	ldr	r7, .L2344
	bne	.L2343
.L2329:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r5, #2828]
	adds	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r2, r3
	itttt	eq
	strbeq	r6, [r4, #6]
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r2, [r4, r3, lsl #1]
	b	.L2328
.L2343:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r1, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2331
	ldrb	r0, [r5, #76]	@ zero_extendqisi2
	cbnz	r0, .L2331
	add	r0, r5, r1, lsl #1
	ldrh	r0, [r0, #592]
	cmp	r0, r8
	bne	.L2331
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2329
.L2331:
	movw	r6, #65535
	orr	r5, r1, r2, lsl #10
	mov	r8, r6
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2332:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r1, [r7, #2828]
.L2334:
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
	cmp	r2, r6
	beq	.L2334
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2339
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbnz	r3, .L2336
	ldrh	r2, [r4, #2]
	ldr	r3, .L2344
	add	r3, r3, r2, lsl #1
	ldrh	r3, [r3, #592]
	cmp	r3, r8
	bne	.L2336
	ldrh	r3, [r4, #4]
	cbz	r3, .L2336
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2332
.L2336:
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	ldr	r2, .L2344
	cbz	r3, .L2339
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #2900]
	cmp	r1, r3
	bcc	.L2339
	ldr	r3, .L2344+4
	ldrh	r0, [r4]
	ldrh	r6, [r4, #4]
	ldr	r1, [r3, #768]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r6
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #2898]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2339:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L2345:
	.align	2
.L2344:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	ldr	r4, .L2365
	ldr	r3, [r4, #740]
	cmp	r3, #0
	bne	.L2348
	ldr	r2, [r4, #756]
	movs	r1, #255
	ldr	r7, [r4, #1796]
	ldr	r5, .L2365+4
	str	r2, [r4, #4056]
	ldrh	r2, [r4, #1064]
	str	r7, [r4, #4060]
	str	r3, [r7, #12]
	strh	r2, [r7, #2]	@ movhi
	movw	r2, #61604
	strh	r2, [r7]	@ movhi
	ldr	r2, [r4, #1072]
	str	r3, [r7, #8]
	ldr	r3, .L2365+8
	str	r2, [r7, #4]
	ldrh	r2, [r4, #790]
	str	r3, [r4, #1012]
	ldr	r3, .L2365+12
	str	r3, [r4, #1016]
	ldrh	r3, [r4, #1070]
	strh	r3, [r4, #1020]	@ movhi
	ldrh	r3, [r5, #2850]
	strb	r3, [r4, #1022]
	ldrh	r3, [r4, #788]
	strh	r3, [r4, #1026]	@ movhi
	ldrb	r3, [r4, #794]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #1028]	@ movhi
	ldrb	r3, [r4, #796]	@ zero_extendqisi2
	ldrh	r2, [r4, #838]
	strb	r3, [r4, #1023]
	ldrh	r3, [r4, #836]
	strh	r3, [r4, #1030]	@ movhi
	ldrb	r3, [r4, #842]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #1032]	@ movhi
	ldrb	r3, [r4, #844]	@ zero_extendqisi2
	strb	r3, [r4, #1024]
	ldrh	r3, [r4, #884]
	strh	r3, [r4, #1034]	@ movhi
	ldrh	r2, [r4, #886]
	ldrb	r3, [r4, #890]	@ zero_extendqisi2
	ldr	r0, [r4, #4056]
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #1036]	@ movhi
	ldrb	r3, [r4, #892]	@ zero_extendqisi2
	ldrh	r2, [r5, #2908]
	strb	r3, [r4, #1025]
	ldr	r3, [r4, #992]
	str	r3, [r4, #1044]
	ldr	r3, [r4, #984]
	str	r3, [r4, #1052]
	ldr	r3, [r4, #988]
	str	r3, [r4, #1048]
	bl	ftl_memset
	add	r1, r4, #1012
	movs	r2, #48
	ldr	r0, [r4, #4056]
	bl	ftl_memcpy
	ldrh	r2, [r5, #2836]
	ldr	r0, [r4, #4056]
	ldr	r1, [r4, #768]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5, #2836]
	ldr	r0, [r4, #4056]
	add	r3, r2, #24
	ldr	r1, [r4, #944]
	ubfx	r3, r3, #1, #30
	lsrs	r2, r2, #3
	adds	r2, r2, #4
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
	ldrh	r3, [r5, #2944]
	cbz	r3, .L2349
	ldrh	r2, [r5, #2836]
	ldr	r0, [r4, #4056]
	ldr	r1, [r4, #1844]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldrh	r2, [r5, #2936]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	lsls	r2, r2, #2
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
.L2349:
	movs	r0, #0
	ldr	r6, .L2365
	bl	FtlUpdateVaildLpn
	mov	r8, #0
	movw	r9, #65535
.L2350:
	ldr	r3, [r4, #756]
	movs	r1, #1
	ldrh	r2, [r4, #1064]
	ldr	r0, .L2365+16
	str	r3, [r4, #4056]
	ldr	r3, [r4, #1796]
	str	r3, [r4, #4060]
	ldrh	r3, [r4, #1066]
	orr	r3, r3, r2, lsl #10
	mov	r2, r1
	str	r3, [r4, #4052]
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r5, #2900]
	ldrh	r2, [r4, #1066]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L2351
	ldrh	r3, [r6, #1064]
	ldrh	r9, [r6, #1068]
	strh	r3, [r6, #1068]	@ movhi
	movs	r3, #0
	strh	r3, [r6, #1066]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r6, #984]
	movs	r1, #1
	adds	r2, r3, #1
	str	r2, [r6, #984]
	str	r3, [r6, #1072]
	lsls	r2, r0, #10
	strh	r0, [r6, #1064]	@ movhi
	str	r2, [r6, #4052]
	mov	r2, r1
	str	r3, [r7, #4]
	mov	r3, r1
	strh	r0, [r7, #2]	@ movhi
	ldr	r0, .L2365+16
	bl	FlashProgPages
.L2351:
	ldrh	r3, [r4, #1066]
	ldr	r2, [r4, #4048]
	adds	r3, r3, #1
	adds	r1, r2, #1
	uxth	r3, r3
	strh	r3, [r4, #1066]	@ movhi
	bne	.L2352
	cmp	r3, #1
	add	r8, r8, #1
	it	eq
	ldrheq	r3, [r5, #2900]
	uxth	r8, r8
	itt	eq
	addeq	r3, r3, #-1
	strheq	r3, [r6, #1066]	@ movhi
	cmp	r8, #3
	bls	.L2350
	ldr	r0, .L2365+20
	mov	r2, r8
	ldr	r1, [r4, #4052]
	bl	printk
	movs	r3, #1
	str	r3, [r4, #740]
	b	.L2348
.L2352:
	cmp	r3, #1
	beq	.L2350
	cmp	r2, #256
	beq	.L2350
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2348
	mov	r0, r9
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2348:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L2366:
	.align	2
.L2365:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177351
	.word	.LANCHOR2+4048
	.word	.LC116
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
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, r0
	ldr	r7, .L2381
	.pad #40
	sub	sp, sp, #40
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbz	r3, .L2376
	ldrb	r5, [r0, #8]	@ zero_extendqisi2
	cmp	r5, #1
	bne	.L2376
	ldrh	r6, [r0, #4]
	b	.L2368
.L2376:
	movs	r5, #0
	movs	r6, #12
.L2368:
	ldr	r10, .L2381+4
	mov	r8, #-1
	mov	r9, #0
.L2369:
	adds	r6, r6, #-1
	bcc	.L2371
	ldrh	r3, [r4, #4]
	cbnz	r3, .L2370
.L2371:
	ldr	r3, .L2381+4
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldr	r2, [r3, #768]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r7, #2898]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2370:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L2371
	ldr	r2, [r10, #1796]
	movs	r1, #1
	ldr	r3, [r10, #756]
	add	r0, sp, #4
	str	r8, [sp, #20]
	str	r2, [sp, #16]
	str	r3, [sp, #12]
	str	r8, [r2, #12]
	str	r8, [r2, #8]
	ldrh	r3, [r4]
	strh	r9, [r2]	@ movhi
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, [r10, #988]
	str	r3, [r2, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, r5
	it	eq
	moveq	r3, #0
	str	r3, [r10, #988]
	movs	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2369
.L2382:
	.align	2
.L2381:
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r6, [r0, #12]
	ldr	r8, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	ip, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, ip, #4
	ldrh	r1, [r4, #40]
	cmp	r2, r3
	ldr	r7, .L2404
	bge	.L2384
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2386
	ldrh	r2, [r4, #2]
	ldrh	r3, [r7, #2900]
	cmp	r2, r3
	bcc	.L2386
.L2384:
	movw	r3, #65535
	cmp	r1, r3
	uxth	r5, r0
	beq	.L2388
	ldrh	r3, [r4, #2]
	ldrh	r2, [r7, #2900]
	cmp	r2, r3
	bhi	.L2388
	movs	r2, #0
.L2389:
	uxth	lr, r2
	cmp	lr, ip
	bcs	.L2390
	adds	r2, r2, #1
	add	r0, r6, r2, lsl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r1
	bne	.L2389
	mov	r5, lr
.L2390:
	ldr	r0, .L2404+4
	ldrh	r2, [r6, lr, lsl #1]
	bl	printk
	movw	r3, #65535
	strh	r3, [r4, #40]	@ movhi
.L2388:
	ldrh	r9, [r6, r5, lsl #1]
	lsls	r2, r5, #1
	cmp	r9, #0
	beq	.L2386
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2386
	movs	r1, #1
	str	r1, [r4, #32]
	strh	r3, [r6, r2]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r7, #2900]
	cmp	r2, r3
	bcc	.L2392
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2392:
	ldr	r5, .L2404+8
	mov	r10, #0
.L2393:
	ldrh	r3, [r4, #6]
	uxth	r6, r10
	cmp	r3, r6
	bls	.L2403
	ldr	r3, [r8, r6, lsl #2]
	cmp	r9, r3, lsr #10
	bne	.L2394
	ldr	r3, [r5, #1776]
	movs	r1, #1
	ldr	fp, [r5, #1796]
	ldr	r0, .L2404+12
	mov	r2, r1
	str	r3, [r5, #4056]
	str	fp, [r5, #4060]
	ldr	r3, [r8, r6, lsl #2]
	str	r3, [r5, #4052]
	bl	FlashReadPages
	ldr	r3, [r5, #4048]
	adds	r3, r3, #1
	bne	.L2395
	movs	r3, #0
	ldr	r0, .L2404+16
	str	r3, [r8, r6, lsl #2]
	ldr	r1, [r5, #4052]
	ldrh	r2, [fp, #8]
	bl	printk
	movs	r3, #1
	str	r3, [r5, #740]
	b	.L2394
.L2395:
	mov	r0, r4
	mov	r1, r6
	ldr	r2, [r5, #4056]
	bl	FtlMapWritePage
.L2394:
	add	r10, r10, #1
	b	.L2393
.L2403:
	mov	r0, r9
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L2386:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r7, #2900]
	cmp	r2, r3
	bcc	.L2397
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2397:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2405:
	.align	2
.L2404:
	.word	.LANCHOR0
	.word	.LC117
	.word	.LANCHOR2
	.word	.LANCHOR2+4048
	.word	.LC118
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r5, .L2413
	ldr	r7, [r0, #12]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #740]
	cmp	r6, #0
	bne	.L2407
	ldrh	r3, [r0]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L2408
	ldrh	r3, [r0, #8]
	adds	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r7]	@ movhi
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	adds	r3, r3, #1
	strh	r6, [r4]	@ movhi
	str	r3, [r4, #28]
	b	.L2407
.L2408:
	ldrh	r7, [r7, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r2, [r0, #28]
	orr	r3, r3, r7, lsl #10
	str	r3, [r5, #4052]
	ldr	r3, [r5, #756]
	str	r3, [r5, #4056]
	ldr	r3, [r5, #1796]
	str	r3, [r5, #4060]
	str	r2, [r3, #4]
	movw	r2, #64245
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r3, .L2413+4
	ldr	r0, [r5, #756]
	ldrh	r2, [r3, #2900]
	lsls	r2, r2, #3
	bl	ftl_memset
	mov	r3, r6
.L2409:
	ldrh	r1, [r4, #6]
	uxth	r2, r6
	cmp	r1, r2
	bls	.L2412
	ldr	r1, [r8, r2, lsl #2]
	cmp	r7, r1, lsr #10
	bne	.L2410
	adds	r3, r3, #1
	ldr	r1, [r5, #756]
	uxth	r3, r3
	str	r2, [r1, r3, lsl #3]
	ldr	r1, [r8, r2, lsl #2]
	ldr	r2, [r5, #756]
	add	r2, r2, r3, lsl #3
	str	r1, [r2, #4]
.L2410:
	adds	r6, r6, #1
	b	.L2409
.L2412:
	movs	r1, #1
	movs	r3, #0
	ldr	r0, .L2413+8
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2407:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2414:
	.align	2
.L2413:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+4048
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
	mov	r5, r0
	ldr	r4, .L2436
	mov	r7, r1
	str	r2, [sp, #4]
	ldr	r6, [r4, #740]
	cmp	r6, #0
	bne	.L2431
	ldr	r8, .L2436+16
	mov	fp, r4
	mov	r10, r8
.L2417:
	ldr	r3, [r4, #968]
	adds	r3, r3, #1
	str	r3, [r4, #968]
	ldrh	r3, [r8, #2900]
	ldrh	r2, [r5, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L2418
	ldrh	r2, [r5]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2419
.L2418:
	mov	r0, r5
	bl	Ftl_write_map_blk_to_last_page
.L2419:
	ldrh	r2, [r5]
	movs	r1, #16
	ldr	r3, [r5, #12]
	ldr	r0, [r4, #1796]
	ldrh	r9, [r3, r2, lsl #1]
	ldrh	r3, [r5, #2]
	str	r0, [r4, #4060]
	orr	r3, r3, r9, lsl #10
	str	r3, [r4, #4052]
	ldr	r3, [sp, #4]
	str	r3, [r4, #4056]
	bl	__memzero
	ldr	r3, [r4, #4060]
	movs	r1, #1
	ldr	r2, [r5, #28]
	ldr	r0, .L2436+4
	strh	r7, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r5, #4]
	strh	r9, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r5, #2]	@ movhi
	ldr	r2, [r4, #4048]
	adds	r1, r2, #1
	bne	.L2420
	ldr	r0, .L2436+8
	adds	r6, r6, #1
	ldr	r1, [fp, #4052]
	bl	printk
	ldrh	r3, [r5, #2]
	uxth	r6, r6
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r10, #2900]
	addls	r3, r3, #-1
	strhls	r3, [r5, #2]	@ movhi
	cmp	r6, #3
	bls	.L2417
	ldr	r0, .L2436+12
	mov	r2, r6
	ldr	r1, [r4, #4052]
	bl	printk
	movs	r3, #1
	str	r3, [r4, #740]
	b	.L2431
.L2420:
	cbz	r2, .L2423
	cmp	r3, #1
	strh	r9, [r5, #40]	@ movhi
	beq	.L2417
	cmp	r2, #256
	b	.L2435
.L2423:
	cmp	r3, #1
.L2435:
	beq	.L2417
	ldr	r2, [r4, #4052]
	ldr	r3, [r5, #24]
	str	r2, [r3, r7, lsl #2]
.L2431:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2437:
	.align	2
.L2436:
	.word	.LANCHOR2
	.word	.LANCHOR2+4048
	.word	.LC119
	.word	.LC120
	.word	.LANCHOR0
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	1
	.global	load_l2p_region
	.thumb
	.thumb_func
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r0
	ldr	r8, .L2442+8
	ldr	r3, [r8, #1844]
	mov	r5, r8
	ldr	r6, [r3, r0, lsl #2]
	movs	r3, #12
	cbnz	r6, .L2439
	mul	r4, r3, r1
	ldr	r3, [r8, #932]
	movs	r1, #255
	add	r3, r3, r4
	ldr	r0, [r3, #8]
	ldr	r3, .L2442
	ldrh	r2, [r3, #2908]
	bl	ftl_memset
	ldr	r3, [r8, #932]
	strh	r7, [r3, r4]	@ movhi
	ldr	r3, [r8, #932]
	add	r4, r4, r3
	str	r6, [r4, #4]
	b	.L2440
.L2439:
	mul	r4, r3, r1
	ldr	r3, [r8, #932]
	movs	r1, #1
	add	r0, r8, #4048
	str	r6, [r8, #4052]
	add	r3, r3, r4
	mov	r2, r1
	ldr	r3, [r3, #8]
	str	r3, [r8, #4056]
	ldr	r3, [r8, #1796]
	str	r3, [r8, #4060]
	bl	FlashReadPages
	ldr	r3, [r8, #4048]
	cmp	r3, #256
	bne	.L2441
	mov	r2, r6
	mov	r1, r7
	ldr	r0, .L2442+4
	lsrs	r6, r6, #10
	bl	printk
	ldr	r3, [r8, #932]
	strh	r6, [r8, #1904]	@ movhi
	add	r0, r8, #1864
	add	r3, r3, r4
	mov	r1, r7
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2441:
	ldr	r3, [r5, #932]
	movs	r1, #0
	adds	r2, r3, r4
	str	r1, [r2, #4]
	strh	r7, [r3, r4]	@ movhi
.L2440:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2443:
	.align	2
.L2442:
	.word	.LANCHOR0
	.word	.LC121
	.word	.LANCHOR2
	.fnend
	.size	load_l2p_region, .-load_l2p_region
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
	ldr	r5, .L2445
	muls	r4, r0, r4
	add	r0, r5, #1864
	ldr	r3, [r5, #932]
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #932]
	movs	r0, #0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L2446:
	.align	2
.L2445:
	.word	.LANCHOR2
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	1
	.global	log2phys
	.thumb
	.thumb_func
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r3, #1
	ldr	r5, .L2461
	mov	r9, r2
	mov	r8, r1
	movs	r1, #12
	ldrh	r4, [r5, #2906]
	ldrh	r2, [r5, #2938]
	adds	r4, r4, #7
	lsr	r7, r0, r4
	lsls	r3, r3, r4
	ldr	r4, .L2461+4
	subs	r3, r3, #1
	ands	r0, r0, r3
	uxth	r7, r7
	movs	r3, #0
	ldr	r10, [r4, #932]
	uxth	r6, r0
.L2448:
	uxth	r5, r3
	cmp	r5, r2
	bcs	.L2460
	adds	r3, r3, #1
	mla	r0, r1, r3, r10
	ldrh	r0, [r0, #-12]
	cmp	r0, r7
	bne	.L2448
.L2449:
	movs	r3, #12
	cmp	r9, #0
	bne	.L2450
	ldr	r2, [r4, #932]
	mla	r3, r3, r5, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r8]
	b	.L2451
.L2450:
	muls	r3, r5, r3
	ldr	r2, [r4, #932]
	ldr	r1, [r8]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r6, lsl #2]
	ldr	r2, [r4, #932]
	strh	r7, [r4, #936]	@ movhi
	add	r3, r3, r2
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
.L2451:
	ldr	r2, [r4, #932]
	movs	r3, #12
	mla	r5, r3, r5, r2
	ldr	r3, [r5, #4]
	adds	r2, r3, #1
	beq	.L2457
	adds	r3, r3, #1
	str	r3, [r5, #4]
	b	.L2457
.L2460:
	bl	select_l2p_ram_region
	movs	r3, #12
	muls	r3, r0, r3
	mov	r5, r0
	add	r2, r10, r3
	ldrh	r1, [r10, r3]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2454
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L2454
	bl	flush_l2p_region
.L2454:
	mov	r0, r7
	mov	r1, r5
	bl	load_l2p_region
	b	.L2449
.L2457:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2462:
	.align	2
.L2461:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	log2phys, .-log2phys
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
	ldr	r5, .L2472
	ldr	r4, [r5, #768]
	ldrh	r3, [r4, r0, lsl #1]
	cbnz	r3, .L2464
	ldr	r4, [r5, #780]
	cbz	r4, .L2465
	ldr	r1, [r5, #760]
	mov	lr, #6
	ldr	r2, .L2472+4
	movw	ip, #65535
	subs	r4, r4, r1
	ldrh	r7, [r5, #784]
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L2466:
	uxth	r2, r3
	cmp	r2, r7
	bcs	.L2465
	cmp	r4, r0
	bne	.L2467
	mov	r1, r4
	ldr	r0, .L2472+8
	bl	List_remove_node
	ldrh	r3, [r5, #784]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #784]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #768]
	ldrh	r3, [r2, r4, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r4, lsl #1]	@ movhi
	b	.L2465
.L2467:
	mul	r4, lr, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, ip
	bne	.L2466
	b	.L2465
.L2464:
	adds	r3, r3, #1
	strh	r3, [r4, r0, lsl #1]	@ movhi
.L2465:
	mov	r0, r6
	add	r1, sp, #4
	movs	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2473:
	.align	2
.L2472:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+780
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	1
	.global	FtlMapTblRecovery
	.thumb
	.thumb_func
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r3, [r0, #16]
	mov	r4, r0
	ldrh	fp, [r0, #6]
	movs	r1, #0
	ldr	r10, [r0, #24]
	movs	r6, #0
	ldr	r5, .L2520
	str	r3, [sp, #12]
	lsl	r2, fp, #2
	ldrh	r3, [r0, #8]
	ldr	r8, [r0, #12]
	mov	r0, r10
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, [r5, #756]
	ldr	r7, [r5, #1796]
	str	r6, [r4, #32]
	str	r3, [r5, #4056]
	movw	r3, #65535
	str	r7, [r5, #4060]
	str	r6, [r4, #28]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #1
	str	r3, [r4, #36]
.L2475:
	uxth	ip, r6
	ldr	r3, [sp, #8]
	sxth	r9, ip
	cmp	r9, r3
	bge	.L2492
	ldr	r3, [sp, #8]
	subs	r2, r3, #1
	cmp	r9, r2
	lsl	r2, r9, #1
	bne	.L2476
	ldrh	r0, [r8, r9, lsl #1]
	movs	r1, #1
	str	ip, [sp, #8]
	add	r6, r8, r2
	bl	FtlGetLastWrittenPage
	adds	r3, r0, #1
	ldr	ip, [sp, #8]
	strh	r3, [r4, #2]	@ movhi
	sxth	r0, r0
	ldr	r3, [sp, #12]
	strh	ip, [r4]	@ movhi
	mov	ip, #0
	ldr	r3, [r3, r9, lsl #2]
	add	r9, r0, #1
	str	r3, [r4, #28]
	ldr	r3, .L2520
.L2477:
	sxth	r2, ip
	cmp	r2, r9
	bge	.L2492
	ldrh	r1, [r6]
	ldr	r0, .L2520+4
	str	r3, [sp, #12]
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r5, #4052]
	mov	r2, r1
	str	ip, [sp, #8]
	bl	FlashReadPages
	ldr	r2, [r5, #4048]
	adds	r2, r2, #1
	ldr	ip, [sp, #8]
	ldr	r3, [sp, #12]
	beq	.L2478
	ldrh	r2, [r7, #8]
	cmp	r2, fp
	bcs	.L2479
	ldrh	r0, [r7]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	bne	.L2479
	ldr	r1, [r3, #4052]
	str	r1, [r10, r2, lsl #2]
	b	.L2479
.L2478:
	ldrh	r2, [r6]
	strh	r2, [r4, #40]	@ movhi
.L2479:
	add	ip, ip, #1
	b	.L2477
.L2492:
	ldr	r3, .L2520+8
	ldr	r10, .L2520+12
	cmp	r4, r3
	bne	.L2482
	b	.L2517
.L2476:
	ldr	r1, [r5, #756]
	add	r3, r8, r2
	str	r3, [sp, #16]
	ldr	r3, .L2520+12
	str	r1, [r5, #4056]
	ldrh	r1, [r8, r9, lsl #1]
	ldrh	r2, [r3, #2900]
	ldr	r0, .L2520+4
	subs	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r5, #4052]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #4048]
	adds	r2, r2, #1
	beq	.L2506
	ldrh	r1, [r7]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2506
	ldrh	r1, [r7, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L2506
	movs	r0, #0
	mov	ip, #8
	mov	r9, #4
.L2484:
	ldr	r3, .L2520+12
	uxth	r2, r0
	sxth	lr, r2
	ldrh	r1, [r3, #2900]
	subs	r1, r1, #1
	cmp	lr, r1
	bge	.L2487
	ldr	r1, [r5, #756]
	adds	r0, r0, #1
	ldrh	lr, [r1, lr, lsl #3]
	cmp	lr, fp
	ittt	cc
	smlabbcc	r2, r2, ip, r9
	ldrcc	r2, [r1, r2]
	strcc	r2, [r10, lr, lsl #2]
	b	.L2484
.L2506:
	ldr	r3, .L2520
	mov	r9, #0
.L2514:
	ldr	r1, .L2520+12
	sxth	r2, r9
	ldrh	r1, [r1, #2900]
	cmp	r2, r1
	bge	.L2487
	ldr	r1, [sp, #16]
	ldr	r0, .L2520+4
	str	r3, [sp, #20]
	ldrh	r1, [r1]
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r3, #4052]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	ldr	r2, [r3, #4048]
	adds	r2, r2, #1
	beq	.L2488
	ldrh	r2, [r7, #8]
	cmp	r2, fp
	bcs	.L2488
	ldrh	r1, [r4, #4]
	ldrh	r0, [r7]
	cmp	r0, r1
	itt	eq
	ldreq	r1, [r3, #4052]
	streq	r1, [r10, r2, lsl #2]
.L2488:
	add	r9, r9, #1
	b	.L2514
.L2487:
	adds	r6, r6, #1
	b	.L2475
.L2503:
	ldr	r2, [r5, #1848]
	lsl	r7, r9, #2
	ldr	r3, [r2, r9, lsl #2]
	subs	r1, r3, #1
	adds	r1, r1, #3
	bls	.L2518
.L2497:
	add	r9, r9, #1
.L2495:
	ldrh	r3, [r10, #2936]
	cmp	r9, r3
	bcc	.L2503
.L2482:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r10, #2900]
	cmp	r2, r3
	bne	.L2494
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2494
.L2517:
	ldrh	r3, [r10, #2944]
	cmp	r3, #0
	beq	.L2482
	mov	r9, #0
	sub	r6, r4, #1864
	b	.L2495
.L2518:
	ldr	r1, [r6, #1844]
	ldr	r1, [r1, r9, lsl #2]
	cmp	r3, r1
	beq	.L2497
	ldr	r3, [r6, #756]
	movs	r1, #1
	ldr	r0, .L2520+4
	str	r3, [r6, #4056]
	ldr	r3, [r6, #1796]
	str	r3, [r6, #4060]
	ldr	r2, [r2, r7]
	str	r3, [sp, #8]
	str	r2, [r6, #4052]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r6, #4048]
	adds	r2, r2, #1
	ldr	r3, [sp, #8]
	beq	.L2510
	ldrh	r1, [r3]
	ldrh	r2, [r6, #1868]
	cmp	r1, r2
	bne	.L2510
	ldr	fp, [r3, #4]
	b	.L2499
.L2510:
	mov	fp, #0
.L2499:
	ldr	r2, [r5, #1844]
	movs	r1, #1
	ldr	r0, .L2520+4
	str	r3, [sp, #8]
	ldr	r2, [r2, r7]
	str	r2, [r5, #4052]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [sp, #8]
	ldr	r1, [r3, #4]
	cmp	fp, r1
	bls	.L2497
	ldr	r2, [r6, #1848]
	ldr	r3, [r6, #1844]
	str	r1, [sp]
	ldr	r1, [r2, r7]
	ldr	r0, .L2520+16
	ldr	r2, [r3, r7]
	mov	r3, fp
	bl	printk
	ldr	r3, [r6, #1848]
	ldr	r2, [r3, r7]
	ldr	r3, [r6, #1844]
	str	r2, [r3, r7]
	movs	r2, #0
	ldr	r3, [r6, #1848]
	ldr	r0, [r3, r7]
	ldrh	r3, [r6, #1908]
	ubfx	r0, r0, #10, #16
.L2500:
	sxth	r1, r2
	cmp	r1, r3
	bge	.L2519
	ldrh	r1, [r8, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r1, r0
	bne	.L2500
	b	.L2497
.L2519:
	ldr	r2, [r10, #2928]
	cmp	r3, r2
	bcs	.L2497
	strh	r0, [r8, r3, lsl #1]	@ movhi
	ldrh	r3, [r5, #1908]
	adds	r3, r3, #1
	strh	r3, [r5, #1908]	@ movhi
	bl	remove_from_free_sys_Queue
	b	.L2497
.L2494:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2521:
	.align	2
.L2520:
	.word	.LANCHOR2
	.word	.LANCHOR2+4048
	.word	.LANCHOR2+1864
	.word	.LANCHOR0
	.word	.LC122
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
	movw	r2, #4098
	ldr	r3, .L2523
	ldr	r0, .L2523+4
	ldrh	r1, [r3, #2920]
	strh	r1, [r0, r2]	@ movhi
	movw	r2, #61574
	ldrh	r1, [r3, #2946]
	strh	r2, [r0, #4092]	@ movhi
	add	r2, r0, #4096
	strh	r1, [r2]	@ movhi
	ldrh	r2, [r3, #2922]
	strh	r2, [r0, #4094]	@ movhi
	ldr	r2, [r3, #2948]
	movw	r3, #4100
	str	r2, [r0, r3]
	movw	r3, #4104
	ldr	r2, [r0, #1836]
	str	r2, [r0, r3]
	movw	r3, #4108
	ldr	r2, [r0, #1832]
	str	r2, [r0, r3]
	movw	r3, #4112
	ldr	r2, [r0, #1840]
	str	r2, [r0, r3]
	add	r3, r0, #4128
	addw	r0, r0, #4088
	movw	r2, #65535
	strh	r2, [r3]	@ movhi
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2524:
	.align	2
.L2523:
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
	ldr	r0, .L2526
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2527:
	.align	2
.L2526:
	.word	.LANCHOR2+1864
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	1
	.global	FtlReadRefresh
	.thumb
	.thumb_func
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2543
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r2, [r3, #1208]
	mov	r4, r3
	ldr	r5, .L2543+4
	cmp	r2, #0
	beq	.L2529
	ldr	r1, [r3, #1212]
	ldr	r2, [r5, #2960]
	cmp	r1, r2
	bcs	.L2530
	mov	r6, #2048
	mov	r7, r3
.L2535:
	ldr	r0, [r4, #1212]
	ldr	r3, [r5, #2960]
	cmp	r0, r3
	bcs	.L2532
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r3, [r7, #1212]
	adds	r3, r3, #1
	str	r3, [r7, #1212]
	ldr	r2, [sp]
	adds	r1, r2, #1
	beq	.L2533
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
	bne	.L2532
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2532
.L2533:
	subs	r6, r6, #1
	bne	.L2535
.L2532:
	mov	r0, #-1
	b	.L2537
.L2530:
	ldr	r2, [r3, #952]
	movs	r0, #0
	str	r0, [r3, #1208]
	str	r0, [r3, #1212]
	str	r2, [r3, #1204]
	b	.L2537
.L2529:
	ldrb	r2, [r5, #76]	@ zero_extendqisi2
	ldr	r1, [r3, #1004]
	ldr	r3, [r3, #948]
	cbnz	r2, .L2540
	mov	r2, #33554432
	add	r3, r3, r1, lsr #10
	asrs	r2, r2, r3
	b	.L2538
.L2540:
	mov	r2, #4194304
.L2538:
	ldr	r1, [r4, #952]
	ldr	r3, [r4, #1204]
	add	r0, r1, #1048576
	cmp	r3, r0
	bhi	.L2539
	add	r3, r3, r2
	cmp	r3, r1
	bcc	.L2539
	ldr	r3, .L2543
	ldrb	r3, [r3, #1040]	@ zero_extendqisi2
	cbnz	r3, .L2541
.L2539:
	movs	r3, #1
	movs	r0, #0
	str	r3, [r4, #1208]
	str	r0, [r4, #1212]
	str	r1, [r4, #1204]
	b	.L2537
.L2541:
	movs	r0, #0
.L2537:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2544:
	.align	2
.L2543:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	1
	.global	FtlVendorPartWrite
	.thumb
	.thumb_func
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2558
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	mov	r4, r1
	add	r0, r0, r1
	ldrh	r1, [r3, #2892]
	.pad #60
	sub	sp, sp, #60
	mov	r10, r2
	ldrh	r2, [r3, #2906]
	cmp	r0, r1
	str	r3, [sp, #4]
	bhi	.L2553
	ldr	r7, .L2558+4
	lsr	r6, r5, r2
	mov	fp, #0
	lsls	r3, r6, #2
	str	r3, [sp]
.L2547:
	cmp	r4, #0
	beq	.L2557
	ldr	r3, .L2558+4
	mov	r0, r5
	ldr	r2, [sp]
	ldr	r3, [r3, #1840]
	ldr	ip, [r3, r2]
	ldr	r3, [sp, #4]
	str	ip, [sp, #12]
	ldrh	r2, [r3, #2904]
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
	beq	.L2549
	cmp	r9, r2
	beq	.L2549
	ldr	r2, [r7, #1780]
	movs	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	movs	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2550
.L2549:
	ldr	r3, [sp, #4]
	movs	r1, #0
	ldr	r0, [r7, #1780]
	ldrh	r2, [r3, #2908]
	bl	ftl_memset
.L2550:
	lsl	ip, r9, #9
	ldr	r0, [r7, #1780]
	uxth	r8, r8
	mov	r1, r10
	mov	r2, ip
	str	ip, [sp, #8]
	add	r0, r0, r8, lsl #9
	rsb	r4, r9, r4
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r0, .L2558+8
	adds	r6, r6, #1
	ldr	r2, [r7, #1780]
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
	b	.L2547
.L2557:
	mov	r0, fp
	b	.L2546
.L2553:
	mov	r0, #-1
.L2546:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2559:
	.align	2
.L2558:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+4088
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
	ldr	r2, .L2562
	ldr	r3, .L2562+4
	ldr	r1, [r2, #1128]
	cmp	r1, r3
	bne	.L2560
	ldr	r3, .L2562+8
	movs	r0, #0
	movs	r1, #1
	str	r3, [r2, #1132]
	ldr	r3, [r2, #976]
	str	r3, [r2, #1216]
	ldr	r3, [r2, #980]
	str	r3, [r2, #1220]
	ldr	r3, [r2, #972]
	str	r3, [r2, #1136]
	ldr	r3, [r2, #960]
	str	r3, [r2, #1140]
	ldr	r3, [r2, #952]
	str	r3, [r2, #1144]
	ldr	r3, [r2, #968]
	str	r3, [r2, #1148]
	ldr	r3, [r2, #996]
	str	r3, [r2, #1156]
	ldr	r3, [r2, #752]
	add	r2, r2, #1128
	str	r3, [r2, #32]
	ldr	r3, [r2, #-172]
	str	r3, [r2, #36]
	ldr	r3, [r2, #-164]
	str	r3, [r2, #40]
	ldr	r3, [r2, #-124]
	str	r3, [r2, #44]
	ldr	r3, [r2, #-120]
	str	r3, [r2, #48]
	ldr	r3, [r2, #-4]
	str	r3, [r2, #60]
	ldr	r3, [r2, #612]
	str	r3, [r2, #64]
	b	FtlVendorPartWrite
.L2560:
	bx	lr
.L2563:
	.align	2
.L2562:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177351
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
	ldr	r3, .L2571
	push	{r4, lr}
	.save {r4, lr}
	ldr	r2, [r3, #948]
	cbz	r2, .L2569
	ldr	r2, [r3, #1008]
	cmp	r2, #29
	ite	hi
	movhi	r1, #32
	movls	r1, #4
	b	.L2565
.L2569:
	movs	r1, #32
.L2565:
	ldr	r4, .L2571+4
	ldrh	r2, [r4, #-212]
	cmp	r2, #31
	ittt	ls
	addls	r2, r2, #1
	movls	r1, #1
	strhls	r2, [r4, #-212]	@ movhi
	cbnz	r0, .L2567
	ldr	r0, [r3, #1816]
	ldr	r2, [r0, #20]
	add	r2, r2, r1
	ldr	r1, [r0, #16]
	cmp	r1, r2
	bcc	.L2568
.L2567:
	ldr	r2, [r3, #1816]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2571+8
	str	r1, [r2]
	ldr	r2, [r3, #1816]
	ldrh	r1, [r3, #1808]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2568:
	movs	r0, #0
	pop	{r4, pc}
.L2572:
	.align	2
.L2571:
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
	ldr	r3, .L2584
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	mov	r5, r1
	add	r0, r0, r1
	ldrh	r1, [r3, #2892]
	.pad #60
	sub	sp, sp, #60
	mov	r8, r2
	ldrh	r2, [r3, #2906]
	cmp	r0, r1
	str	r3, [sp, #8]
	bhi	.L2582
	lsr	r6, r7, r2
	ldr	r10, .L2584+12
	movs	r3, #0
	str	r3, [sp]
	lsl	r9, r6, #2
	mov	fp, r10
.L2575:
	cmp	r5, #0
	beq	.L2583
	ldr	r3, [r10, #1840]
	mov	r0, r7
	ldr	ip, [r3, r9]
	ldr	r3, [sp, #8]
	str	ip, [sp, #12]
	ldrh	r4, [r3, #2904]
	mov	r1, r4
	bl	__aeabi_uidivmod
	subs	r3, r4, r1
	ldr	ip, [sp, #12]
	str	r1, [sp, #4]
	uxth	r4, r3
	cmp	r4, r5
	it	hi
	uxthhi	r4, r5
	cmp	ip, #0
	beq	.L2577
	ldr	r2, [fp, #1780]
	movs	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	ip, [sp, #12]
	str	r2, [sp, #28]
	movs	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [sp, #20]
	ldr	r3, [sp]
	adds	r2, r2, #1
	ldr	r2, [fp, #4048]
	it	eq
	moveq	r3, #-1
	ldr	ip, [sp, #12]
	cmp	r2, #256
	str	r3, [sp]
	bne	.L2579
	mov	r1, r6
	mov	r2, ip
	ldr	r0, .L2584+4
	bl	printk
	ldr	r0, .L2584+8
	mov	r1, r6
	ldr	r2, [r10, #1780]
	bl	FtlMapWritePage
.L2579:
	ldrh	r2, [sp, #4]
	mov	r0, r8
	ldr	r1, [fp, #1780]
	add	r1, r1, r2, lsl #9
	lsls	r2, r4, #9
	bl	ftl_memcpy
	b	.L2580
.L2577:
	mov	r0, r8
	mov	r1, ip
	lsls	r2, r4, #9
	bl	ftl_memset
.L2580:
	adds	r6, r6, #1
	subs	r5, r5, r4
	add	r7, r7, r4
	add	r8, r8, r4, lsl #9
	add	r9, r9, #4
	b	.L2575
.L2583:
	ldr	r0, [sp]
	b	.L2574
.L2582:
	mov	r0, #-1
.L2574:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2585:
	.align	2
.L2584:
	.word	.LANCHOR0
	.word	.LC123
	.word	.LANCHOR2+4088
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
	ldr	r4, .L2588
	ldr	r2, [r4, #1816]
	ldrh	r1, [r4, #1808]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #1816]
	ldr	r2, [r3]
	ldr	r3, .L2588+4
	cmp	r2, r3
	beq	.L2587
	ldr	r1, .L2588+8
	ldr	r0, .L2588+12
	bl	printk
	ldrh	r2, [r4, #1808]
	ldr	r0, [r4, #1816]
	movs	r1, #0
	lsls	r2, r2, #9
	bl	ftl_memset
.L2587:
	movs	r0, #0
	pop	{r4, pc}
.L2589:
	.align	2
.L2588:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC124
	.word	.LC74
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
	ldr	r4, .L2594
	movs	r1, #1
	ldr	r5, .L2594+4
	add	r2, r4, #1128
	bl	FtlVendorPartRead
	ldr	r3, [r4, #1128]
	cmp	r3, r5
	beq	.L2591
	add	r0, r4, #1128
	mov	r1, #512
	bl	__memzero
	str	r5, [r4, #1128]
.L2591:
	ldr	r2, [r4, #1128]
	ldr	r3, .L2594
	cmp	r2, r5
	bne	.L2592
	ldr	r2, [r3, #1216]
	str	r2, [r3, #976]
	ldr	r2, [r3, #1220]
	str	r2, [r3, #980]
	ldr	r2, [r3, #1136]
	str	r2, [r3, #972]
	ldr	r2, [r3, #1140]
	str	r2, [r3, #960]
	ldr	r2, [r3, #1144]
	str	r2, [r3, #952]
	ldr	r2, [r3, #1148]
	str	r2, [r3, #968]
	ldr	r2, [r3, #1156]
	str	r2, [r3, #996]
	ldr	r2, [r3, #1160]
	str	r2, [r3, #752]
	ldr	r2, [r3, #1164]
	str	r2, [r3, #956]
	ldr	r2, [r3, #1168]
	str	r2, [r3, #964]
	ldr	r2, [r3, #1172]
	str	r2, [r3, #1004]
	ldr	r2, [r3, #1176]
	str	r2, [r3, #1008]
	ldr	r2, [r3, #1188]
	str	r2, [r3, #1124]
.L2592:
	movs	r3, #0
	ldr	r2, [r4, #1196]
	str	r3, [r4, #1740]
	ldr	r3, .L2594+8
	cmp	r2, r3
	bne	.L2593
	ldr	r3, .L2594
	movs	r2, #1
	ldr	r0, .L2594+12
	ldr	r1, .L2594+16
	str	r2, [r3, #948]
	bl	printk
.L2593:
	ldr	r3, .L2594+20
	ldr	r2, [r4, #996]
	ldr	r0, [r4, #992]
	ldrh	r1, [r3, #2888]
	mla	r0, r0, r1, r2
	ldrh	r1, [r3, #2836]
	bl	__aeabi_uidiv
	str	r0, [r4, #1000]
	pop	{r3, r4, r5, pc}
.L2595:
	.align	2
.L2594:
	.word	.LANCHOR2
	.word	1179929683
	.word	305432421
	.word	.LC74
	.word	.LC125
	.word	.LANCHOR0
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
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
	mov	r5, r0
	mov	r4, r1
	mov	r0, r1
	mov	r1, #512
	bl	__memzero
	bl	rknand_device_lock
	mov	r2, r4
	movs	r1, #1
	add	r0, r5, #512
	bl	FtlVendorPartRead
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r3, r4, r5, pc}
	.fnend
	.size	StorageSysDataLoad, .-StorageSysDataLoad
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
	ldr	r2, [r0, #36]
	ldrh	r5, [r0, #6]
	ldr	r3, [r0, #24]
	cbz	r2, .L2597
	ldr	r4, .L2603
	movs	r2, #0
	str	r2, [r0, #36]
	ldr	r2, [r4, #740]
	cbnz	r2, .L2597
	subs	r5, r5, #1
	ldr	r0, [r4, #1776]
	ldr	r2, [r4, #1796]
	uxth	r5, r5
	str	r0, [r4, #4056]
	str	r2, [r4, #4060]
	ldr	r3, [r3, r5, lsl #2]
	str	r3, [r4, #4052]
	cbz	r3, .L2601
	movs	r1, #1
	add	r0, r4, #4048
	mov	r2, r1
	bl	FlashReadPages
	b	.L2602
.L2601:
	ldr	r3, .L2603+4
	movs	r1, #255
	ldrh	r2, [r3, #2908]
	bl	ftl_memset
.L2602:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #4056]
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L2597:
	pop	{r4, r5, r6, pc}
.L2604:
	.align	2
.L2603:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
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
	beq	.L2749
	ldrh	r3, [r0, #2]
	movs	r5, #0
	ldr	r6, .L2765
	str	r3, [sp, #12]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	str	r3, [sp, #28]
	ldrh	r3, [r6, #2898]
	cmp	r3, r2
	bne	.L2608
	strh	r5, [r0, #4]	@ movhi
	strb	r5, [r0, #6]
	b	.L2749
.L2608:
	ldrh	r0, [r0, #16]
.L2609:
	cmp	r0, r7
	add	r5, r5, #1
	bne	.L2760
	uxth	r3, r5
	adds	r3, r3, #8
	ldrh	r0, [r4, r3, lsl #1]
	b	.L2609
.L2760:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2611
	bl	FtlGetLastWrittenPage
	adds	r2, r0, #1
	mov	r5, r0
	beq	.L2612
	ldrb	r3, [r6, #76]	@ zero_extendqisi2
	cbnz	r3, .L2751
	ldr	r3, .L2765
	add	r3, r3, r0, lsl #1
	ldrh	r7, [r3, #80]
	b	.L2686
.L2611:
	movs	r1, #0
	bl	FtlGetLastWrittenPage
	adds	r3, r0, #1
	mov	r5, r0
	beq	.L2612
.L2751:
	mov	r7, r5
.L2686:
	ldr	r3, .L2765+4
	movw	r9, #65535
	ldrh	ip, [r6, #2828]
	mov	r10, #36
	ldrh	r0, [r6, #2910]
	ldr	r2, [r3, #1764]
	ldr	r8, [r3, #1668]
	add	r3, r4, #14
	str	r3, [sp, #20]
	str	r2, [sp, #4]
	movs	r2, #0
	mov	lr, r3
	mov	r6, r2
	mov	fp, r2
	b	.L2614
.L2612:
	movs	r3, #0
	strh	r3, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
	b	.L2749
.L2617:
	ldrh	r3, [lr, #2]!
	cmp	r3, r9
	beq	.L2615
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
.L2615:
	adds	r2, r2, #1
.L2614:
	uxth	r3, r2
	cmp	r3, ip
	bcc	.L2617
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2688
	ldr	r3, .L2765
	ldrb	r1, [r3, #76]	@ zero_extendqisi2
	adds	r3, r1, #0
	it	ne
	movne	r3, #1
	b	.L2752
.L2688:
	movs	r3, #0
.L2752:
	ldr	r8, .L2765+4
	mov	r1, r6
	str	r3, [sp, #24]
	mov	fp, #0
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #1764]
	bl	FlashReadPages
	ldr	r3, [r8, #988]
	subs	r3, r3, #1
	str	r8, [sp, #8]
	str	r3, [sp, #4]
	movw	r3, #65535
	str	r3, [sp, #16]
.L2619:
	uxth	r9, fp
	cmp	r9, r6
	bcs	.L2626
	movs	r1, #36
	ldr	r0, [r8, #1764]
	mul	r1, r1, fp
	add	lr, r0, r1
	ldr	r1, [r0, r1]
	cbnz	r1, .L2620
	ldr	r3, [lr, #12]
	ldr	r10, [r3, #4]
	cmp	r10, #-1
	beq	.L2621
	mov	r0, r10
	ldr	r1, [r8, #988]
	str	r3, [sp, #32]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #32]
	cbz	r0, .L2621
	add	r1, r10, #1
	str	r1, [r8, #988]
.L2621:
	ldr	r1, [r3]
	adds	r1, r1, #1
	bne	.L2622
.L2626:
	cmp	r9, r6
	ldr	r6, .L2765+4
	bne	.L2750
	ldr	r3, [r6, #1764]
	adds	r5, r5, #1
	uxth	r10, r5
	ldr	r0, [r3, #4]
	b	.L2753
.L2620:
	ldr	r1, [lr, #4]
	ldr	r0, .L2765+8
	bl	printk
	uxth	r3, r7
	ldrh	r1, [r4]
	str	r3, [sp, #16]
	ldr	r3, .L2765+12
	strh	r1, [r3, #-210]	@ movhi
.L2622:
	add	fp, fp, #1
	b	.L2619
.L2750:
	ldr	r3, [sp, #8]
	movs	r2, #36
	uxth	r10, r5
	ldr	r3, [r3, #1764]
	mla	r9, r2, r9, r3
	ldr	r0, [r9, #4]
.L2753:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r5, [r4, #8]	@ zero_extendqisi2
	ldr	r2, .L2765
	cmp	r5, #1
	str	r0, [sp, #8]
	bne	.L2628
	ldrb	r3, [r2, #76]	@ zero_extendqisi2
	cbnz	r3, .L2628
	add	r3, r2, r10, lsl #1
	ldrh	r10, [r3, #80]
.L2628:
	ldrh	r3, [r2, #2898]
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
	bne	.L2630
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #32]
	cmp	r3, r2
	bne	.L2630
	mov	r0, r4
	mov	r1, r10
	mov	r2, r3
	b	.L2758
.L2630:
	ldr	r2, [sp, #16]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2631
	cmp	r5, #0
	bne	.L2632
.L2631:
	ldr	r3, [r6, #1928]
	uxth	fp, r7
	mov	r9, #-1
	ldr	r8, .L2765+4
	adds	r3, r3, #1
	mov	r7, r9
	ittt	eq
	ldreq	r3, .L2765+4
	ldreq	r2, [sp, #4]
	streq	r2, [r3, #1928]
	ldr	r3, [r6, #1928]
	str	r3, [sp, #16]
	ldr	r3, [sp, #12]
	adds	r3, r3, #7
	cmp	fp, r3
	itet	gt
	subgt	r5, fp, #7
	ldrle	r5, [sp, #12]
	uxthgt	r5, r5
.L2635:
	cmp	r5, fp
	bhi	.L2648
	ldr	r3, .L2765
	ldr	r1, [sp, #20]
	ldrh	ip, [r3, #2828]
	ldr	r3, [r8, #1764]
	str	r3, [sp, #28]
	movs	r3, #0
	mov	r6, r3
.L2649:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2761
	ldrh	r2, [r1, #2]!
	movw	r0, #65535
	cmp	r2, r0
	beq	.L2636
	ldr	r0, [sp, #28]
	mov	lr, #36
	orr	r2, r5, r2, lsl #10
	mla	r0, lr, r6, r0
	adds	r6, r6, #1
	uxth	r6, r6
	str	r2, [r0, #4]
.L2636:
	adds	r3, r3, #1
	b	.L2649
.L2761:
	mov	r1, r6
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #1764]
	bl	FlashReadPages
	ldr	r3, .L2765
	movs	r2, #36
	movw	r1, #65535
	ldrb	r0, [r3, #76]	@ zero_extendqisi2
	ldr	r3, [r8, #1764]
	mla	r6, r2, r6, r3
	ldr	r2, .L2765
	add	lr, r2, r5, lsl #1
.L2638:
	cmp	r3, r6
	beq	.L2762
	ldr	r2, [r3]
	cbnz	r2, .L2639
	ldr	r2, [r3, #12]
	ldrh	ip, [r2]
	cmp	ip, r1
	beq	.L2640
	ldr	r2, [r2, #4]
	cmp	r2, #-1
	beq	.L2640
	cmp	r9, #-1
	ldr	r7, [r8, #1928]
	str	r2, [r8, #1928]
	bne	.L2640
	ldrh	r2, [lr, #592]
	cmp	r2, r1
	bne	.L2641
	cmp	r0, #0
	beq	.L2640
.L2641:
	ldr	r2, [sp, #4]
	cmp	r7, r2
	ite	ne
	movne	r9, r7
	moveq	r9, #-1
	b	.L2640
.L2766:
	.align	2
.L2765:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC126
	.word	.LANCHOR4
.L2639:
	ldr	r3, .L2767
	ldrh	r2, [r4]
	strh	r2, [r3, #-210]	@ movhi
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cbnz	r3, .L2632
	ldr	r3, .L2767+4
	add	r5, r3, r5, lsl #1
	movw	r3, #65535
	ldrh	r2, [r5, #592]
	cmp	r2, r3
	ldr	r3, .L2767+8
	bne	.L2643
	cmp	r9, #-1
	beq	.L2644
	str	r9, [r3, #1928]
	b	.L2632
.L2644:
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	bne	.L2754
	ldr	r2, [r3, #1928]
	b	.L2759
.L2643:
	ldr	r2, [sp, #4]
	cmp	r7, r2
	beq	.L2646
	adds	r2, r7, #1
	beq	.L2632
	str	r7, [r3, #1928]
	b	.L2632
.L2646:
	ldr	r2, [r3, #1928]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	beq	.L2632
.L2759:
	subs	r2, r2, #1
	b	.L2754
.L2640:
	adds	r3, r3, #36
	b	.L2638
.L2762:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L2635
.L2648:
	ldr	r3, .L2767+8
	mov	r2, #-1
.L2754:
	str	r2, [r3, #1928]
.L2632:
	ldr	r3, .L2767
	movs	r2, #1
	ldr	r0, .L2767+12
	ldr	r5, .L2767+8
	strh	r2, [r3, #-208]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	fp, [sp, #12]
.L2650:
	ldr	r3, .L2767+4
	movw	r9, #65535
	ldr	r1, [r5, #1764]
	movs	r6, #0
	ldr	r7, [sp, #20]
	ldrh	r8, [r3, #2828]
	ldrb	r0, [r3, #76]	@ zero_extendqisi2
	str	r6, [sp, #16]
.L2651:
	uxth	r3, r6
	cmp	r3, r8
	bcs	.L2763
	ldrh	r3, [r7, #2]!
	cmp	r3, r9
	beq	.L2652
	ldr	r2, [sp, #16]
	mov	lr, #36
	orr	r3, fp, r3, lsl #10
	mla	r2, lr, r2, r1
	str	r3, [r2, #4]
	ldrb	lr, [r4, #8]	@ zero_extendqisi2
	cmp	lr, #1
	bne	.L2653
	cbz	r0, .L2653
	orr	r3, r3, #-2147483648
	str	r3, [r2, #4]
.L2653:
	ldr	r3, [sp, #16]
	add	ip, r3, #1
	uxth	r3, ip
	str	r3, [sp, #16]
.L2652:
	adds	r6, r6, #1
	b	.L2651
.L2763:
	ldr	r0, [r5, #1764]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	movs	r3, #0
.L2757:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L2764
	ldr	r3, [sp, #28]
	movs	r6, #36
	ldr	r9, [r5, #1764]
	muls	r6, r3, r6
	add	r8, r9, r6
	ldr	r7, [r8, #4]
	ubfx	r0, r7, #10, #16
	str	r7, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #12]
	cmp	fp, r3
	bcc	.L2656
	bne	.L2657
	ldr	r3, [sp, #32]
	cmp	r0, r3
	bcc	.L2656
.L2657:
	cmp	fp, r10
	bne	.L2658
	ldr	r3, [sp, #8]
	cmp	r0, r3
	beq	.L2659
.L2658:
	ldr	r3, [r9, r6]
	adds	r3, r3, #1
	beq	.L2660
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L2667
	ldr	r3, [r8, #4]
	adds	r7, r3, #1
	str	r3, [sp, #4]
	beq	.L2662
	mov	r0, r3
	ldr	r1, [r5, #988]
	bl	ftl_cmp_data_ver
	cbz	r0, .L2662
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [r5, #988]
.L2662:
	ldr	r7, [r8, #8]
	add	r1, sp, #56
	ldr	r3, [r8, #12]
	movs	r2, #0
	mov	r0, r7
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r5, #1928]
	adds	r0, r1, #1
	beq	.L2663
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2663
	ldr	r3, [sp, #52]
	adds	r2, r3, #1
	beq	.L2664
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
	bne	.L2665
	b	.L2666
.L2664:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L2667
	mov	r0, r7
	add	r1, sp, #52
	movs	r2, #1
	bl	log2phys
.L2667:
	ldrh	r0, [r4]
	b	.L2756
.L2665:
	ldr	r8, [r9, #8]
	cmp	r8, r7
	bne	.L2666
	ldr	r0, [r5, #1928]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	cbz	r0, .L2666
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L2672
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L2666
	adds	r0, r3, #1
	beq	.L2670
	str	r3, [ip, #4]
	mov	r0, ip
	movs	r1, #1
	movs	r2, #0
	ldr	r9, [ip, #12]
	bl	FlashReadPages
	b	.L2671
.L2670:
	str	r3, [r2, r6]
.L2671:
	ldr	r3, [r5, #1764]
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	beq	.L2672
	ldr	r6, [r9, #4]
	ldr	r0, [r5, #1928]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L2672
	ldr	r0, [sp, #36]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L2666
.L2672:
	mov	r0, r8
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L2666:
	ldrh	r0, [r4]
	mov	r3, #-1
	str	r3, [sp, #52]
	bl	decrement_vpc_count
	b	.L2674
.L2768:
	.align	2
.L2767:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+1864
.L2663:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L2674
	mov	r0, r7
	add	r1, sp, #60
	movs	r2, #1
	bl	log2phys
	ldr	r8, [sp, #56]
	cmp	r8, #-1
	beq	.L2674
	ldr	r3, [sp, #52]
	cmp	r8, r3
	beq	.L2674
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #788]
	cmp	r3, r0
	beq	.L2677
	ldrh	r3, [r5, #836]
	cmp	r3, r0
	beq	.L2677
	ldrh	r3, [r5, #884]
	cmp	r3, r0
	bne	.L2674
.L2677:
	ldr	r3, .L2769
	movs	r1, #1
	movs	r2, #0
	ldr	r0, [r3, #1764]
	str	r8, [r0, #4]
	ldr	r6, [r0, #12]
	bl	FlashReadPages
	ldr	r3, .L2769
	ldr	r1, [r6, #4]
	ldr	r3, [r3, #1764]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L2674
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cbnz	r0, .L2674
	mov	r0, r7
	add	r1, sp, #56
	movs	r2, #1
	bl	log2phys
.L2674:
	ldr	r0, [sp, #52]
	adds	r1, r0, #1
	beq	.L2656
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #768]
	ldrh	r3, [r3, r0, lsl #1]
	mov	r1, r0
	cbz	r3, .L2678
.L2756:
	bl	decrement_vpc_count
	b	.L2656
.L2678:
	ldr	r0, .L2769+4
	bl	printk
	b	.L2656
.L2660:
	ldrh	r3, [r4]
	mov	r1, r7
	ldr	r2, .L2769+8
	ldr	r0, .L2769+12
	strh	r3, [r2, #-210]	@ movhi
	ldr	r2, [sp, #4]
	bl	printk
	ldr	r3, .L2769+8
	ldr	r3, [r3, #-204]
	cmp	r3, #31
	bhi	.L2679
	ldr	r2, .L2769+8
	ldr	r1, [sp, #60]
	add	r2, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r2, #-200]
	ldr	r2, .L2769+8
	str	r3, [r2, #-204]
.L2679:
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	ldr	r3, [r5, #1928]
	adds	r2, r3, #1
	bne	.L2680
	ldr	r3, [sp, #4]
	b	.L2755
.L2680:
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bls	.L2656
	mov	r3, r2
.L2755:
	str	r3, [r5, #1928]
.L2656:
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	b	.L2757
.L2764:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	fp, fp, #1
	cmp	r3, #1
	uxth	fp, fp
	ldr	r3, .L2769+16
	bne	.L2682
	ldrb	r2, [r3, #76]	@ zero_extendqisi2
	cbz	r2, .L2682
	ldrh	r2, [r3, #2900]
	cmp	r2, fp
	bne	.L2682
	cmp	r10, fp
	beq	.L2659
.L2682:
	ldrh	r3, [r3, #2898]
	cmp	fp, r3
	bne	.L2650
	ldr	r2, .L2769+16
	movw	r0, #65535
	movs	r3, #0
	strh	fp, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	ldrh	r1, [r2, #2828]
.L2683:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L2749
	ldr	r6, [sp, #20]
	adds	r3, r3, #1
	ldrh	r5, [r6, #2]!
	cmp	r5, r0
	str	r6, [sp, #20]
	beq	.L2683
	strb	r2, [r4, #6]
	b	.L2749
.L2659:
	ldrb	r3, [sp, #8]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, [sp, #8]
	mov	r1, r10
	strh	r10, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
.L2758:
	bl	ftl_sb_update_avl_pages
.L2749:
	movs	r0, #0
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2770:
	.align	2
.L2769:
	.word	.LANCHOR2
	.word	.LC127
	.word	.LANCHOR4
	.word	.LC128
	.word	.LANCHOR0
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
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
	ldr	r4, .L2789
	ldr	r3, [r4, #740]
	cmp	r3, #0
	bne	.L2771
	ldrh	r2, [r4, #792]
	cmp	r2, #0
	beq	.L2773
	ldrb	r3, [r4, #796]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2773
	ldr	r7, .L2789+4
	ldrb	r1, [r4, #795]	@ zero_extendqisi2
	ldrh	r3, [r7, #2898]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L2773
	ldrb	r9, [r4, #798]	@ zero_extendqisi2
	ldr	r6, [r7, #2960]
	ldrh	r8, [r7, #2828]
	cmp	r9, #0
	bne	.L2771
	subs	r6, r6, #1
	mov	r1, sp
	mov	r2, r9
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #1796]
	ldr	r0, [r4, #756]
	ldr	r3, [sp]
	str	r6, [sp, #20]
	str	r0, [sp, #12]
	str	r3, [sp, #8]
	adds	r3, r3, #1
	str	r5, [sp, #16]
	str	r9, [r5, #4]
	beq	.L2775
	movs	r1, #1
	mov	r2, r9
	add	r0, sp, #4
	bl	FlashReadPages
	b	.L2776
.L2775:
	movs	r1, #255
	ldrh	r2, [r7, #2908]
	bl	ftl_memset
.L2776:
	movs	r7, #0
	ldr	r10, .L2789
	lsl	r8, r8, #2
	movw	r3, #61589
	mov	r9, r7
	strh	r3, [r5]	@ movhi
.L2777:
	cmp	r7, r8
	beq	.L2778
	ldrh	r3, [r4, #792]
	cbz	r3, .L2778
	ldr	r3, [sp, #8]
	adds	r7, r7, #1
	str	r6, [r5, #8]
	ldr	r0, .L2789+8
	str	r3, [r5, #12]
	ldrh	r3, [r10, #788]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r10, #988]
	movs	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, #0
	it	eq
	moveq	r3, r9
	str	r3, [r10, #988]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r4, #788]
	bl	decrement_vpc_count
	b	.L2777
.L2778:
	movs	r3, #1
	b	.L2788
.L2773:
	movs	r3, #0
.L2788:
	strb	r3, [r4, #798]
.L2771:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2790:
	.align	2
.L2789:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+788
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
	ldr	r5, .L2796
	movs	r7, #12
	ldr	r6, .L2796+4
.L2792:
	ldrh	r3, [r5, #2938]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2795
	ldr	r3, [r6, #932]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2793
	bl	flush_l2p_region
.L2793:
	adds	r4, r4, #1
	b	.L2792
.L2795:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L2797:
	.align	2
.L2796:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	l2p_flush, .-l2p_flush
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
	ldr	r6, .L2812
	ldr	r1, .L2812+4
	ldr	r0, .L2812+8
	bl	printk
	ldr	r5, .L2812+12
	ldrh	r2, [r6, #2838]
	movs	r1, #0
	ldr	r0, [r5, #1820]
	lsls	r2, r2, #1
	bl	ftl_memset
.L2799:
	ldr	r3, [r6, #2960]
	cmp	r4, r3
	bcs	.L2810
	mov	r0, r4
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L2800
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #1820]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L2800:
	adds	r4, r4, #1
	b	.L2799
.L2810:
	ldr	r7, .L2812+12
	mov	r8, #0
	movw	r9, #65535
.L2802:
	ldrh	r3, [r6, #2836]
	uxth	r4, r8
	cmp	r3, r4
	bls	.L2811
	ldr	r3, [r5, #768]
	ldrh	r2, [r3, r4, lsl #1]
	ldr	r3, [r5, #1820]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r2, r3
	beq	.L2804
	cmp	r2, r9
	beq	.L2804
	ldr	r0, .L2812+16
	mov	r1, r4
	bl	printk
	ldrh	r3, [r7, #788]
	cmp	r3, r4
	beq	.L2804
	ldrh	r3, [r7, #884]
	cmp	r3, r4
	beq	.L2804
	ldrh	r3, [r7, #836]
	cmp	r3, r4
	beq	.L2804
	ldr	r3, [r7, #1820]
	mov	r0, r4
	ldrh	r2, [r3, r4, lsl #1]
	ldr	r3, [r7, #768]
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2804:
	add	r8, r8, #1
	b	.L2802
.L2811:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2813:
	.align	2
.L2812:
	.word	.LANCHOR0
	.word	.LANCHOR3+16
	.word	.LC129
	.word	.LANCHOR2
	.word	.LC130
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
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
	ldr	r4, .L2840
	ldrh	r5, [r0]
	ldr	r3, [r4, #740]
	cmp	r3, #0
	bne	.L2815
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2816
	ldr	r3, [r4, #768]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L2817
	bl	INSERT_DATA_LIST
	b	.L2816
.L2817:
	bl	INSERT_FREE_LIST
.L2816:
	movs	r3, #0
	strb	r3, [r6, #8]
	ldr	r3, .L2840
	add	r2, r3, #836
	cmp	r6, r2
	beq	.L2818
	ldr	r1, .L2840+4
	ldrh	r2, [r1, #2848]
	cmp	r2, #1
	beq	.L2818
	ldrb	r1, [r1, #76]	@ zero_extendqisi2
	cbz	r1, .L2819
.L2818:
	movs	r3, #1
	strb	r3, [r6, #8]
	b	.L2820
.L2819:
	add	r1, r3, #788
	cmp	r6, r1
	bne	.L2820
	cmp	r2, #3
	beq	.L2822
	ldr	r3, [r3, #1124]
	cmp	r3, #1
	bne	.L2823
.L2822:
	movs	r3, #1
	strb	r3, [r4, #796]
.L2823:
	ldr	r2, [r4, #948]
	ldr	r3, .L2840
	cbz	r2, .L2820
	ldr	r2, [r3, #1008]
	cmp	r2, #29
	itt	ls
	movls	r2, #1
	strbls	r2, [r3, #796]
.L2820:
	ldrh	r0, [r4, #1916]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2825
	cmp	r5, r0
	bne	.L2826
	ldr	r3, .L2840
	ldr	r3, [r3, #768]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L2827
.L2826:
	bl	update_vpc_list
.L2827:
	movw	r3, #65535
	strh	r3, [r4, #1916]	@ movhi
.L2825:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2815:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L2841:
	.align	2
.L2840:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	1
	.global	rk_ftl_garbage_collect
	.thumb
	.thumb_func
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r4, .L3010
	.pad #44
	sub	sp, sp, #44
	mov	r6, r1
	ldr	r0, [r4, #740]
	mov	r7, r4
	cmp	r0, #0
	bne	.L2936
	ldr	r3, [r4, #1744]
	cmp	r3, #0
	bne	.L2994
	ldrh	r2, [r4, #776]
	cmp	r2, #47
	bls	.L2938
	ldr	r3, .L3010+4
	ldrh	r2, [r3, #3000]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2844
.L2847:
	ldrh	r1, [r7, #1696]
	movw	r0, #65535
	ldr	r3, .L3010
	cmp	r1, r0
	beq	.L2846
	ldrh	r2, [r3, #1694]
	cmp	r2, r0
	itt	eq
	strheq	r1, [r3, #1694]	@ movhi
	strheq	r2, [r3, #1696]	@ movhi
	b	.L2846
.L2844:
	ldrh	r2, [r4, #884]
	cmp	r2, r3
	beq	.L2847
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L2847
	movs	r0, #1
	b	.L2994
.L2846:
	ldr	r4, [r7, #1644]
	cmp	r8, #1
	add	r4, r4, #1
	add	r4, r4, r8, lsl #7
	str	r4, [r7, #1644]
	bne	.L2858
	ldr	r3, .L3010
	ldr	r3, [r3, #948]
	cbnz	r3, .L2849
	ldr	r3, .L3010+8
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2858
.L2849:
	ldr	r3, [r7, #1008]
	ldr	r5, .L3010
	cmp	r3, #29
	bhi	.L2858
	ldr	r9, .L3010+12
	ldrh	r3, [r9, #-72]
	add	r4, r4, r3
	str	r4, [r5, #1644]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r5, #1076]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L2858
	ldrh	r2, [r5, #1694]
	cmp	r2, r3
	bne	.L2858
	ldr	r3, [r5, #1644]
	cmp	r3, #1024
	bhi	.L2850
	ldrh	r3, [r5, #784]
	cmp	r3, #63
	bhi	.L2858
.L2850:
	ldrh	r3, [r7, #1642]
	movs	r0, #0
	ldrh	r4, [r7, #784]
	adds	r3, r3, #64
	ldr	r2, .L3010+12
	cmp	r4, r3
	strh	r0, [r9, #-72]	@ movhi
	ldr	r1, .L3010
	bgt	.L2858
	ldr	r3, [r1, #1008]
	str	r0, [r1, #1644]
	cbnz	r3, .L2851
	movs	r3, #6
	b	.L2995
.L2851:
	cmp	r3, #5
	bhi	.L2852
	movs	r3, #18
.L2995:
	strh	r3, [r2, #-72]	@ movhi
.L2852:
	movs	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r2, r0
	cmp	r2, ip
	beq	.L2857
	ldrh	r0, [r7, #1648]
	ldr	r5, .L3010
	cmp	r0, #0
	beq	.L2854
	ldr	r3, .L3010+8
	ldr	r10, [r5, #768]
	ldrh	r4, [r3, #2900]
	ldrh	r3, [r3, #2828]
	ldrh	r1, [r10, r2, lsl #1]
	muls	r3, r4, r3
	adds	r3, r3, #1
	cmp	r1, r3
	bgt	.L2857
	adds	r1, r0, #1
	str	ip, [sp, #20]
	str	r2, [sp, #16]
	mov	fp, #0
	uxth	r1, r1
	str	fp, [r5, #1652]
	strh	r1, [r5, #1648]	@ movhi
	str	r1, [sp, #12]
	bl	List_get_gc_head_node
	ldr	ip, [sp, #20]
	uxth	r4, r0
	ldr	r1, [sp, #12]
	cmp	r4, ip
	ldr	r2, [sp, #16]
	beq	.L2857
	ldrh	r2, [r10, r2, lsl #1]
	ldrh	r3, [r10, r4, lsl #1]
	ldr	r0, .L3010+16
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, #1648]
	cmp	r3, #40
	bls	.L2855
	ldr	r3, [r5, #768]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r3, #32
	it	hi
	strhhi	fp, [r5, #1648]	@ movhi
.L2855:
	movs	r3, #6
	strh	r3, [r9, #-72]	@ movhi
	b	.L2874
.L2854:
	movs	r3, #1
	strh	r3, [r5, #1648]	@ movhi
.L2857:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L2874
	movs	r3, #0
	strh	r3, [r9, #-72]	@ movhi
.L2858:
	ldrh	r3, [r7, #1076]
	movw	r4, #65535
	ldr	r5, .L3010
	cmp	r3, r4
	bne	.L3002
	ldrh	r4, [r5, #884]
	cmp	r4, r3
	bne	.L2859
	ldrh	r10, [r5, #1694]
	cmp	r10, r4
	bne	.L2860
	ldrh	r3, [r5, #784]
	ldr	r2, [r5, #1644]
	cmp	r3, #24
	ite	cc
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	bls	.L2862
	ldr	r2, .L3010+12
	movs	r3, #0
	str	r3, [r7, #1644]
	mov	r9, r5
	strh	r3, [r2, #-72]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r10
	mov	r4, r0
	bne	.L2863
	ldrh	r2, [r5, #784]
	ldrh	r3, [r5, #1642]
	cmp	r2, r3
	bcs	.L2864
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	bne	.L3003
.L2873:
	bl	FtlGcReFreshBadBlk
	cmp	r8, #0
	beq	.L3004
.L2874:
	ldrh	r2, [r7, #884]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2885
.L2934:
	ldrh	r3, [r7, #1694]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L2885
	cmp	r4, r3
	bne	.L2885
.L2935:
	ldrh	r2, [r7, #1076]
	movw	r3, #65535
	ldr	r4, .L3010
	cmp	r2, r3
	beq	.L2886
.L2890:
	movw	r4, #65535
	b	.L2885
.L2886:
	movs	r3, #0
	ldrh	r9, [r4, #784]
	str	r3, [r4, #1652]
	ldrh	r3, [r4, #1640]
	ldr	r5, .L3010+12
	cmp	r3, r9
	bcs	.L2887
	ldrh	r3, [r5, #-72]
	cbnz	r3, .L2888
	ldr	r3, .L3010+8
	ldr	r2, [r4, #940]
	ldr	r3, [r3, #2960]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L2946
.L2888:
	ldrh	r3, [r7, #1060]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
	b	.L2889
.L2946:
	movs	r3, #18
.L2889:
	strh	r3, [r7, #1640]	@ movhi
	b	.L2999
.L2887:
	ldrh	r0, [r5, #-72]
	cmp	r0, #0
	bne	.L2890
	ldrh	r6, [r4, #1060]
	add	r3, r6, r6, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #1640]	@ movhi
	bl	List_get_gc_head_node
	ldr	r2, .L3010+8
	ldr	r3, [r4, #768]
	ldrh	r1, [r2, #2900]
	ldrh	r4, [r2, #2828]
	muls	r4, r1, r4
	add	r4, r4, r4, lsr #31
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, r4, asr #1
	ble	.L2891
	subs	r6, r6, #1
	cmp	r9, r6
	blt	.L2891
.L2999:
	bl	FtlReadRefresh
	b	.L3000
.L2891:
	cmp	r3, #0
	bne	.L2890
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r7, #784]
	adds	r0, r0, #1
	b	.L2994
.L2885:
	ldr	r3, [r7, #948]
	cmp	r3, #0
	ite	eq
	moveq	r5, #1
	movne	r5, #2
	b	.L2884
.L3011:
	.align	2
.L3010:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC131
.L3004:
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2874
.L2933:
	ldrh	r4, [r7, #784]
	cmp	r4, #24
	bhi	.L2942
	ldr	r3, .L3012
	cmp	r4, #16
	ldrh	r5, [r3, #2898]
	bls	.L2876
	lsrs	r5, r5, #5
.L2875:
	ldrh	r3, [r7, #1640]
	ldr	r2, .L3012+4
	cmp	r3, r4
	bcc	.L3005
.L2879:
	ldr	r3, [r7, #948]
	cmp	r3, #0
	bne	.L3006
.L2945:
	movw	r4, #65535
.L2884:
	ldrh	r2, [r7, #1076]
	movw	r1, #65535
	ldr	r3, .L3012+4
	cmp	r2, r1
	bne	.L2893
	cmp	r4, r2
	beq	.L2894
	strh	r4, [r3, #1076]	@ movhi
	b	.L2895
.L2894:
	ldrh	r2, [r3, #1694]
	cmp	r2, r4
	beq	.L2895
	ldr	r1, [r3, #768]
	ldrh	r2, [r1, r2, lsl #1]
	cbnz	r2, .L2896
	strh	r4, [r3, #1694]	@ movhi
.L2896:
	ldrh	r3, [r7, #1694]
	strh	r3, [r7, #1076]	@ movhi
	movw	r3, #65535
	strh	r3, [r7, #1694]	@ movhi
.L2895:
	ldrh	r0, [r7, #1076]
	movw	r6, #65535
	movs	r3, #0
	strb	r3, [r7, #1084]
	cmp	r0, r6
	beq	.L2893
	bl	IsBlkInGcList
	cbz	r0, .L2898
	ldr	r3, .L3012+4
	strh	r6, [r3, #1076]	@ movhi
.L2898:
	ldr	r3, .L3012
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	cbz	r3, .L2899
	ldrh	r0, [r7, #1076]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #1084]
.L2899:
	ldrh	r2, [r7, #1076]
	movw	r3, #65535
	ldr	r6, .L3012+4
	cmp	r2, r3
	beq	.L2893
	addw	r0, r6, #1076
	bl	make_superblock
	ldr	r2, .L3012+8
	movs	r3, #0
	ldrh	r1, [r6, #1076]
	strh	r3, [r6, #1078]	@ movhi
	strb	r3, [r6, #1082]
	strh	r3, [r2, #-70]	@ movhi
	ldr	r3, [r6, #768]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #-68]	@ movhi
.L2893:
	ldrh	r3, [r7, #1076]
	ldrh	r2, [r7, #788]
	cmp	r2, r3
	beq	.L2900
	ldr	r2, .L3012+4
	ldrh	r2, [r2, #836]
	cmp	r2, r3
	beq	.L2900
.L2901:
	ldr	r7, .L3012
	b	.L2902
.L2900:
	movw	r3, #65535
	strh	r3, [r7, #1076]	@ movhi
	b	.L3001
.L2923:
	ldrh	r3, [r6, #1078]
	add	r5, r5, r3
	ldr	r3, [sp, #12]
	uxth	r5, r5
	strh	r5, [r6, #1078]	@ movhi
	cmp	r5, r3
	bcs	.L3007
.L2925:
	ldrh	r3, [r6, #784]
	cmp	r3, #2
	bhi	.L2928
	ldrh	r5, [r7, #2898]
.L2902:
	ldr	r6, .L3012+4
	movw	r3, #65535
	ldrh	r2, [r6, #1076]
	mov	fp, r6
	cmp	r2, r3
	bne	.L2903
	mov	r10, r6
	movs	r3, #0
	str	r3, [r6, #1652]
.L2904:
	ldrh	r9, [fp, #1648]
	mov	r0, r9
	bl	List_get_gc_head_node
	movw	r2, #65535
	uxth	r3, r0
	strh	r3, [fp, #1076]	@ movhi
	cmp	r3, r2
	bne	.L2905
	ldr	r3, .L3012+4
	movs	r2, #0
	movs	r0, #8
	strh	r2, [r3, #1648]	@ movhi
	b	.L2994
.L2905:
	mov	r0, r3
	str	r3, [sp, #12]
	bl	IsBlkInGcList
	add	r9, r9, #1
	ldr	r3, [sp, #12]
	cbz	r0, .L2906
	strh	r9, [r10, #1648]	@ movhi
	b	.L2904
.L2906:
	ldrh	lr, [r7, #2828]
	uxth	r9, r9
	ldrh	r2, [r7, #2898]
	ldr	r1, [r10, #768]
	strh	r9, [r10, #1648]	@ movhi
	mul	r2, lr, r2
	ldrh	r0, [r1, r3, lsl #1]
	add	lr, r2, r2, lsr #31
	cmp	r0, lr, asr #1
	bgt	.L2908
	cmp	r9, #48
	bls	.L2909
	cmp	r0, #8
	bls	.L2909
	ldrh	r0, [r10, #1684]
	cmp	r0, #35
	bhi	.L2909
.L2908:
	movs	r0, #0
	strh	r0, [fp, #1648]	@ movhi
.L2909:
	ldrh	r1, [r1, r3, lsl #1]
	cmp	r1, r2
	blt	.L2910
	movw	r2, #65535
	cmp	r4, r2
	bne	.L2910
	movs	r3, #0
	strh	r4, [fp, #1076]	@ movhi
	strh	r3, [fp, #1648]	@ movhi
.L3001:
	ldr	r3, .L3012+8
	ldrh	r0, [r3, #-72]
	b	.L2994
.L2910:
	cbnz	r1, .L2911
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [fp, #1648]
	adds	r3, r3, #1
	strh	r3, [fp, #1648]	@ movhi
	b	.L2904
.L2911:
	movs	r2, #0
	strb	r2, [r6, #1084]
	ldr	r2, .L3012
	ldrb	r2, [r2, #76]	@ zero_extendqisi2
	cbz	r2, .L2912
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3012+4
	strb	r0, [r3, #1084]
.L2912:
	ldr	r0, .L3012+12
	bl	make_superblock
	ldr	r2, .L3012+8
	ldrh	r0, [r6, #1076]
	movs	r3, #0
	ldr	r1, [r6, #768]
	strh	r3, [r2, #-70]	@ movhi
	ldrh	r1, [r1, r0, lsl #1]
	strh	r3, [r6, #1078]	@ movhi
	strb	r3, [r6, #1082]
	strh	r1, [r2, #-68]	@ movhi
.L2903:
	cmp	r8, #1
	bne	.L2913
	bl	FtlReadRefresh
.L2913:
	movs	r3, #1
	str	r3, [r6, #1744]
	ldrh	r3, [r7, #2898]
	str	r3, [sp, #12]
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbz	r3, .L2914
	ldr	r3, .L3012+4
	ldrb	r3, [r3, #1084]	@ zero_extendqisi2
	cmp	r3, #1
	itt	eq
	ldrheq	r3, [r7, #2900]
	streq	r3, [sp, #12]
.L2914:
	ldrh	r3, [r6, #1078]
	ldr	r1, [sp, #12]
	adds	r2, r3, r5
	cmp	r2, r1
	ittt	gt
	movgt	r2, r1
	subgt	r3, r2, r3
	uxthgt	r5, r3
	movs	r3, #0
.L2998:
	str	r3, [sp, #16]
	ldrh	r3, [sp, #16]
	ldr	r6, .L3012+4
	cmp	r3, r5
	bcs	.L2923
	ldr	r3, [r6, #1672]
	addw	r1, r6, #1090
	ldr	r2, [sp, #16]
	movw	lr, #65535
	ldrh	r10, [r7, #2828]
	str	r3, [sp, #20]
	ldrh	r3, [r6, #1078]
	add	ip, r3, r2
	movs	r3, #0
	mov	r9, r3
.L2924:
	uxth	r2, r3
	cmp	r2, r10
	bcs	.L3008
	ldrh	r2, [r1, #2]!
	cmp	r2, lr
	beq	.L2917
	ldr	r0, [sp, #20]
	mov	fp, #36
	orr	r2, ip, r2, lsl #10
	mla	fp, fp, r9, r0
	add	r9, r9, #1
	uxth	r9, r9
	str	r2, [fp, #4]
.L2917:
	adds	r3, r3, #1
	b	.L2924
.L3013:
	.align	2
.L3012:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR2+1076
.L3008:
	ldr	r0, [r6, #1672]
	mov	r1, r9
	ldrb	r2, [r6, #1084]	@ zero_extendqisi2
	mov	fp, #0
	bl	FlashReadPages
	ldr	r6, .L3014
.L2919:
	uxth	r3, fp
	cmp	r3, r9
	bcs	.L3009
	mov	ip, #36
	ldr	r2, [r6, #1672]
	mul	r10, ip, fp
	add	r3, r2, r10
	ldr	r2, [r2, r10]
	ldr	r3, [r3, #12]
	adds	r2, r2, #1
	beq	.L2948
	ldrh	r1, [r3]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L2948
	add	r1, sp, ip
	ldr	r0, [r3, #8]
	movs	r2, #0
	str	ip, [sp, #24]
	str	r3, [sp, #20]
	bl	log2phys
	ldr	r1, [r6, #1672]
	add	r1, r1, r10
	ldr	r0, [r1, #4]
	ldr	r2, [sp, #36]
	ldr	r3, [sp, #20]
	bic	r2, r2, #-2147483648
	ldr	ip, [sp, #24]
	cmp	r2, r0
	bne	.L2948
	str	r3, [sp, #28]
	ldr	r3, .L3014+4
	ldr	r0, [r6, #1656]
	ldr	r1, [r1, #16]
	ldrh	r2, [r3, #-70]
	adds	r2, r2, #1
	strh	r2, [r3, #-70]	@ movhi
	ldr	r2, [r6, #1768]
	mla	r2, ip, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #20]
	bl	Ftl_get_new_temp_ppa
	ldr	r1, [r6, #1656]
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #24]
	ldr	r3, [sp, #28]
	str	r0, [r2, #4]
	ldr	r2, [r6, #1768]
	mla	ip, ip, r1, r2
	ldr	r2, [r6, #1672]
	add	r2, r2, r10
	ldr	r1, [r2, #8]
	str	r1, [ip, #8]
	movs	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [ip, #12]
	ldr	r2, [sp, #36]
	str	r2, [r3, #12]
	ldrh	r2, [r6, #884]
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r6, #988]
	ldr	r0, [r6, #1672]
	str	r2, [r3, #4]
	add	r0, r0, r10
	ldr	r3, [r6, #1656]
	adds	r3, r3, #1
	str	r3, [r6, #1656]
	bl	FtlGcBufAlloc
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbnz	r3, .L2921
	ldrb	r3, [r6, #891]	@ zero_extendqisi2
	ldr	r2, [r6, #1656]
	cmp	r2, r3
	beq	.L2921
	ldrh	r3, [r6, #888]
	cbnz	r3, .L2948
.L2921:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L2948
	ldr	r3, .L3014
	movs	r2, #0
	str	r2, [r3, #1744]
	b	.L3001
.L2948:
	add	fp, fp, #1
	b	.L2919
.L3009:
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	b	.L2998
.L3007:
	ldr	r3, [r6, #1656]
	ldr	r5, .L3014+4
	cbz	r3, .L2926
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L2926
	movs	r3, #0
	str	r3, [r6, #1744]
.L3000:
	ldrh	r0, [r5, #-72]
	b	.L2994
.L2926:
	ldrh	r1, [r5, #-70]
	cbnz	r1, .L2927
	ldrh	r2, [r6, #1076]
	ldr	r3, [r6, #768]
	ldrh	r0, [r3, r2, lsl #1]
	cbz	r0, .L2927
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldr	r3, .L3014
	ldrh	r0, [r3, #1076]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2927:
	movw	r3, #65535
	strh	r3, [r6, #1076]	@ movhi
	b	.L2925
.L2928:
	ldr	r2, .L3014
	movs	r1, #0
	str	r1, [r2, #1744]
	ldr	r2, .L3014+4
	ldrh	r0, [r2, #-72]
	cmp	r0, #0
	bne	.L2994
	adds	r0, r3, #1
	b	.L2994
.L2936:
	movs	r0, #0
	b	.L2994
.L2938:
	mov	r0, r3
	b	.L2994
.L3003:
	ldr	r2, [r5, #1740]
	ldr	r5, .L3014+8
	cbnz	r2, .L2866
	ldrh	r2, [r5, #2848]
	cmp	r2, #3
	beq	.L2866
	ldr	r2, [r9, #1124]
	cbnz	r2, .L2866
	ldr	r2, [r9, #948]
	cbnz	r2, .L2866
	ldrb	r0, [r5, #76]	@ zero_extendqisi2
	cbz	r0, .L2867
.L2866:
	ldr	r2, [r7, #768]
	ldrh	r0, [r5, #2848]
	ldrh	r1, [r2, r3, lsl #1]
	cmp	r0, #3
	ldrh	r3, [r5, #2900]
	ldrh	r2, [r5, #2828]
	mul	r2, r2, r3
	ite	eq
	lsreq	r3, r3, #1
	movne	r3, #0
	add	r3, r3, r2
	cmp	r1, r3
	bgt	.L2869
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #2960]
	ldr	r2, .L3014
	ldr	r1, [r7, #940]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	ite	hi
	movhi	r3, #128
	movls	r3, #160
	strh	r3, [r2, #1642]	@ movhi
	uxth	r4, r0
	b	.L2871
.L2869:
	movs	r3, #128
	b	.L2997
.L2867:
	ldr	r2, [r9, #768]
	ldrh	r3, [r2, r3, lsl #1]
	cmp	r3, #7
	bhi	.L2872
	bl	List_get_gc_head_node
	movs	r3, #128
	strh	r3, [r7, #1642]	@ movhi
	uxth	r4, r0
	b	.L2871
.L2872:
	movs	r3, #64
.L2997:
	strh	r3, [r7, #1642]	@ movhi
	b	.L2873
.L2864:
	movs	r3, #80
	strh	r3, [r5, #1642]	@ movhi
	b	.L2873
.L2871:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L2873
.L2863:
	ldr	r1, [r7, #748]
	ldr	r3, [r7, #768]
	ldrh	r2, [r7, #784]
	ldrh	r1, [r1, r4, lsl #1]
	ldrh	r3, [r3, r4, lsl #1]
	ldr	r0, .L3014+12
	str	r1, [sp]
	ldrh	r1, [r7, #1640]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
	b	.L2873
.L2876:
	cmp	r4, #12
	bls	.L2877
	lsrs	r5, r5, #4
	b	.L2875
.L2877:
	cmp	r4, #8
	bls	.L2875
	lsrs	r5, r5, #2
	b	.L2875
.L2942:
	movs	r5, #1
	b	.L2875
.L3005:
	ldrh	r3, [r2, #884]
	movw	r1, #65535
	cmp	r3, r1
	bne	.L2880
	ldrh	r1, [r2, #1694]
	cmp	r1, r3
	bne	.L2880
	ldr	r3, .L3014+4
	ldrh	r0, [r3, #-72]
	cbnz	r0, .L2881
	ldr	r3, .L3014+8
	ldr	r2, [r2, #940]
	ldr	r3, [r3, #2960]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L2943
.L2881:
	ldrh	r3, [r7, #1060]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
	b	.L2882
.L2943:
	movs	r3, #18
.L2882:
	strh	r3, [r7, #1640]	@ movhi
	movs	r3, #0
	str	r3, [r7, #1652]
	b	.L2994
.L2880:
	ldrh	r3, [r7, #1060]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r7, #1640]	@ movhi
	b	.L2879
.L3006:
	cmp	r6, #2
	bhi	.L2945
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L2945
.L3002:
	cmp	r8, #0
	beq	.L2933
	b	.L2874
.L2859:
	cmp	r8, #0
	beq	.L2933
	b	.L2890
.L2860:
	cmp	r8, #0
	beq	.L2933
	b	.L2934
.L2862:
	cmp	r8, #0
	beq	.L2933
	b	.L2935
.L2994:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3015:
	.align	2
.L3014:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LC132
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
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
	ldr	r3, .L3058
	ldr	r6, .L3058+4
	ldr	r5, [r3, #-64]
	ldr	r3, [r6, #740]
	cmp	r3, #0
	bne	.L3018
	ldr	r4, .L3058+8
	ldr	r1, [r4, #2952]
	cmp	r1, #0
	beq	.L3018
	ldrb	r7, [r4, #76]	@ zero_extendqisi2
	cbz	r7, .L3020
	ldrb	r7, [r5, #8]	@ zero_extendqisi2
	subs	r0, r7, #1
	rsbs	r7, r0, #0
	adcs	r7, r7, r0
.L3020:
	ldr	r0, [r4, #2956]
	mov	r2, r7
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r9, #0
	bl	FlashProgPages
	ldr	r10, .L3058+8
	mov	fp, #36
.L3021:
	ldr	r3, [r4, #2952]
	cmp	r9, r3
	bcs	.L3038
	mul	r8, fp, r9
	ldr	r3, [r10, #2956]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	beq	.L3041
	ldr	r3, [r2, #4]
	cbz	r7, .L3054
	orr	r3, r3, #-2147483648
.L3054:
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	movs	r2, #1
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2956]
	add	r8, r8, r3
	ldr	r3, [r8, #12]
	ldr	r0, [r3, #12]
	adds	r2, r0, #1
	beq	.L3025
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r6, #768]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r8, r0
	cbnz	r2, .L3026
	ldr	r0, .L3058+12
	mov	r1, r8
	bl	printk
.L3026:
	mov	r0, r8
	bl	decrement_vpc_count
.L3025:
	add	r9, r9, #1
	b	.L3021
.L3056:
	movw	r5, #16386
.L3037:
	ldrh	r3, [r6, #1698]
	cbz	r3, .L3038
	movs	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r5, r5, #1
	bne	.L3037
.L3038:
	movs	r3, #0
	str	r3, [r4, #2952]
	b	.L3018
.L3041:
	ldr	r10, .L3058+8
	mov	r9, #0
.L3022:
	ldr	r3, [r4, #2952]
	cmp	r9, r3
	bcs	.L3056
	mov	r8, #36
	ldr	r3, [r10, #2956]
	mov	fp, #0
	mov	r2, #-1
	mul	r8, r8, r9
	str	r2, [r3, r8]
.L3028:
	ldr	r3, [r4, #2956]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	bne	.L3057
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3029
	ldr	r1, [r6, #768]
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r10, #2898]
	strb	fp, [r5, #6]
	strh	fp, [r5, #4]	@ movhi
	strh	r3, [r5, #2]	@ movhi
.L3029:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L3030
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3030:
	ldr	r3, [r6, #1224]
	adds	r3, r3, #1
	str	r3, [r6, #1224]
	ldr	r3, [r4, #2956]
	add	r3, r3, r8
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #2956]
	movs	r1, #1
	mov	r2, r7
	add	r3, r3, r8
	str	r0, [sp, #4]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r6, #740]
	cmp	r3, #0
	beq	.L3028
	b	.L3018
.L3057:
	ldr	r3, [r2, #4]
	cbz	r7, .L3055
	orr	r3, r3, #-2147483648
.L3055:
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	movs	r2, #1
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2956]
	add	r8, r8, r3
	ldr	r3, [r8, #12]
	ldr	r0, [r3, #12]
	adds	r3, r0, #1
	beq	.L3034
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r6, #768]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r8, r0
	cbnz	r2, .L3035
	ldr	r0, .L3058+12
	mov	r1, r8
	bl	printk
.L3035:
	mov	r0, r8
	bl	decrement_vpc_count
.L3034:
	add	r9, r9, #1
	b	.L3022
.L3018:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3059:
	.align	2
.L3058:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC133
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
	ldr	r3, .L3062
	ldr	r3, [r3, #740]
	cbnz	r3, .L3061
	bl	FtlCacheWriteBack
	bl	l2p_flush
	movs	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3061:
	movs	r0, #0
	pop	{r3, pc}
.L3063:
	.align	2
.L3062:
	.word	.LANCHOR2
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
	ldr	r3, .L3066
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3065
	bl	FtlSysFlush
.L3065:
	movs	r0, #0
	pop	{r3, pc}
.L3067:
	.align	2
.L3066:
	.word	.LANCHOR1
	.fnend
	.size	FtlDeInit, .-FtlDeInit
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
	bl	FlashDeInit
	bl	FtlDeInit
	pop	{r3, lr}
	b	FlashDeInit
	.fnend
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	1
	.global	FtlDiscard
	.thumb
	.thumb_func
	.type	FtlDiscard, %function
FtlDiscard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	adds	r2, r0, r1
	ldr	r7, .L3085
	mov	r6, r0
	mov	r5, r1
	ldr	r3, [r7, #2940]
	cmp	r2, r3
	bhi	.L3077
	cmp	r1, #31
	bhi	.L3071
.L3072:
	movs	r0, #0
	b	.L3070
.L3071:
	ldr	r3, .L3085+4
	ldr	r2, [r3, #740]
	mov	r8, r3
	cmp	r2, #0
	bne	.L3072
	bl	FtlCacheWriteBack
	ldrh	r4, [r7, #2904]
	mov	r0, r6
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r9, r0
	subs	r6, r6, r3
	uxth	r6, r6
	cbz	r6, .L3073
	subs	r4, r4, r6
	add	r9, r0, #1
	cmp	r4, r5
	it	cs
	movcs	r4, r5
	uxth	r4, r4
	subs	r5, r5, r4
.L3073:
	ldr	r4, .L3085+8
	mov	r3, #-1
	str	r3, [sp, #4]
.L3074:
	ldrh	r3, [r7, #2904]
	cmp	r5, r3
	bcc	.L3084
	mov	r0, r9
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L3075
	ldr	r3, [r4, #-60]
	add	r1, sp, #4
	movs	r2, #1
	mov	r0, r9
	adds	r3, r3, #1
	str	r3, [r4, #-60]
	ldr	r3, [r8, #956]
	adds	r3, r3, #1
	str	r3, [r8, #956]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3075:
	ldrh	r3, [r7, #2904]
	add	r9, r9, #1
	subs	r5, r5, r3
	b	.L3074
.L3084:
	ldr	r3, .L3085+8
	ldr	r2, [r3, #-60]
	cmp	r2, #32
	bls	.L3072
	movs	r4, #0
	str	r4, [r3, #-60]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3072
.L3077:
	mov	r0, #-1
.L3070:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3086:
	.align	2
.L3085:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	FtlDiscard, .-FtlDiscard
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
	ldr	r4, .L3124
	ldr	r6, .L3124+4
	ldr	r2, [r4, #740]
	ldrh	r1, [r6, #2898]
	cmp	r2, #0
	bne	.L3122
	ldrh	r5, [r4, #884]
	movw	r7, #65535
	cmp	r5, r7
	bne	.L3090
.L3099:
	ldrh	r2, [r4, #884]
	movw	r3, #65535
	movs	r7, #0
	ldr	r5, .L3124
	cmp	r2, r3
	str	r7, [r4, #1924]
	beq	.L3122
	bl	FtlCacheWriteBack
	ldrb	r0, [r5, #891]	@ zero_extendqisi2
	ldrh	r3, [r6, #2898]
	mov	r10, #12
	ldr	r2, [r5, #768]
	ldrh	r1, [r5, #884]
	smulbb	r3, r0, r3
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r3, [r5, #972]
	ldrh	r2, [r5, #1692]
	add	r3, r3, r2
	str	r3, [r5, #972]
	b	.L3100
.L3090:
	cbz	r0, .L3093
	ldr	r3, .L3124+8
	ldrh	r0, [r3, #3000]
	cmp	r0, r7
	beq	.L3094
.L3095:
	movs	r1, #2
	b	.L3093
.L3094:
	strh	r2, [r3, #3000]	@ movhi
	ldrh	r3, [r4, #784]
	cmp	r3, #17
	bhi	.L3095
.L3093:
	ldr	r7, .L3124
	add	r0, r7, #884
	bl	FtlGcScanTempBlk
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L3096
	ldr	r2, [r7, #748]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L3097
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L3097:
	ldr	r3, [r4, #1924]
	ldr	r2, .L3124
	cbnz	r3, .L3098
	ldr	r0, [sp, #4]
	ldr	r3, [r2, #1224]
	ubfx	r0, r0, #10, #16
	adds	r3, r3, #1
	str	r3, [r2, #1224]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3098:
	movs	r3, #0
	str	r3, [r4, #1924]
	b	.L3110
.L3096:
	ldr	r3, .L3124+8
	ldrh	r2, [r3, #3000]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3110
	b	.L3099
.L3103:
	mul	r8, r10, r8
	ldr	fp, [r5, #1688]
	add	r1, sp, #4
	movs	r2, #0
	add	r9, fp, r8
	ldr	r0, [r9, #8]
	bl	log2phys
	ldr	r3, [fp, r8]
	ldr	r0, [sp, #4]
	cmp	r0, r3
	bne	.L3101
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r9, #4
	movs	r2, #1
	mov	r8, r0
	ldr	r0, [r9, #8]
	bl	log2phys
	mov	r0, r8
.L3121:
	bl	decrement_vpc_count
.L3102:
	adds	r7, r7, #1
.L3100:
	ldrh	r3, [r4, #1692]
	uxth	r8, r7
	cmp	r3, r8
	bhi	.L3103
	b	.L3123
.L3101:
	ldr	r3, [r9, #4]
	cmp	r0, r3
	beq	.L3102
	ldrh	r0, [r5, #884]
	b	.L3121
.L3123:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r6, #76]	@ zero_extendqisi2
	cbz	r3, .L3104
	ldr	r3, .L3124
	ldr	r0, .L3124+12
	ldrh	r1, [r3, #884]
	bl	printk
.L3104:
	ldrh	r0, [r4, #884]
	ldr	r3, [r4, #768]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L3105
	bl	INSERT_DATA_LIST
	b	.L3106
.L3105:
	bl	INSERT_FREE_LIST
.L3106:
	movw	r3, #65535
	strh	r3, [r4, #884]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #1692]	@ movhi
	strh	r3, [r4, #1684]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r4, #948]
	ldr	r5, .L3124
	cbz	r3, .L3107
	ldr	r3, [r5, #1008]
	cmp	r3, #29
	bhi	.L3107
	ldrh	r3, [r5, #1060]
	ldrh	r2, [r5, #784]
	cmp	r2, r3
	itt	cc
	lslcc	r3, r3, #1
	strhcc	r3, [r5, #1640]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #1076]	@ movhi
	b	.L3122
.L3107:
	ldrh	r3, [r4, #1060]
	ldrh	r1, [r4, #784]
	ldr	r2, .L3124
	add	r0, r3, r3, lsl #1
	cmp	r1, r0, asr #2
	ble	.L3122
	ldrb	r0, [r6, #76]	@ zero_extendqisi2
	movw	r1, #65535
	strh	r1, [r2, #1076]	@ movhi
	cbz	r0, .L3109
	subs	r3, r3, #2
	strh	r3, [r2, #1640]	@ movhi
.L3122:
	movs	r0, #0
	b	.L3089
.L3109:
	movs	r3, #20
	strh	r3, [r2, #1640]	@ movhi
	b	.L3089
.L3110:
	movs	r0, #1
.L3089:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3125:
	.align	2
.L3124:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC134
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	1
	.global	Ftl_gc_temp_data_write_back
	.thumb
	.thumb_func
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3144
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	ldr	r5, .L3144+4
	cbz	r3, .L3127
	ldr	r3, [r5, #1656]
	lsls	r4, r3, #31
	bpl	.L3127
	ldrh	r3, [r5, #888]
	cbz	r3, .L3127
.L3132:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3127:
	movs	r2, #0
	ldr	r0, [r5, #1768]
	ldr	r1, [r5, #1656]
	movs	r6, #0
	mov	r3, r2
	ldr	r7, .L3144+4
	bl	FlashProgPages
	mov	r8, #36
.L3129:
	ldr	r1, [r5, #1656]
	uxth	r3, r6
	ldr	r4, .L3144+4
	cmp	r3, r1
	bcs	.L3143
	mul	r3, r8, r3
	ldr	r1, [r7, #1768]
	adds	r6, r6, #1
	adds	r2, r1, r3
	ldr	r1, [r1, r3]
	adds	r0, r1, #1
	bne	.L3130
	ldrh	r0, [r4, #884]
	movs	r5, #0
	ldr	r2, [r4, #768]
	strh	r5, [r2, r0, lsl #1]	@ movhi
	ldr	r2, [r4, #1224]
	strh	r1, [r4, #884]	@ movhi
	adds	r2, r2, #1
	str	r2, [r4, #1224]
	ldr	r2, [r4, #1768]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3142
.L3130:
	ldr	r3, [r2, #12]
	ldr	r1, [r2, #4]
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L3129
.L3143:
	ldr	r0, [r4, #1768]
	bl	FtlGcBufFree
	movs	r3, #0
	str	r3, [r4, #1656]
	ldrh	r3, [r4, #888]
	cmp	r3, #0
	bne	.L3132
	movs	r0, #1
	bl	FtlGcFreeTempBlock
.L3142:
	movs	r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L3145:
	.align	2
.L3144:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
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
	ldr	r5, .L3148
	ldr	r4, .L3148+4
	ldrh	r1, [r5, #2898]
	add	r0, r4, #884
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #886]
	ldrh	r3, [r5, #2898]
	cmp	r2, r3
	bcc	.L3146
	add	r0, r4, #1864
	bl	FtlMapBlkWriteDumpData
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #1924]
.L3146:
	pop	{r3, r4, r5, pc}
.L3149:
	.align	2
.L3148:
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
	ldr	r4, .L3151
	ldr	r3, .L3151+4
	add	r6, r4, #788
	add	r4, r4, #836
	mov	r0, r6
	str	r5, [r3, #-204]
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
.L3152:
	.align	2
.L3151:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
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
	ldr	r4, .L3156
	ldrh	r2, [r4, #884]
	cmp	r2, r3
	beq	.L3154
	ldrh	r3, [r4, #888]
	cbnz	r3, .L3155
.L3154:
	bl	FtlCacheWriteBack
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3156+4
	strb	r5, [r4, #892]
	bl	allocate_data_superblock
	strh	r5, [r4, #1684]	@ movhi
	strh	r5, [r4, #1692]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3155:
	ldr	r0, .L3156+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L3157:
	.align	2
.L3156:
	.word	.LANCHOR2
	.word	.LANCHOR2+884
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
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
	.global	ftl_fix_nand_power_lost_error
	.thumb
	.thumb_func
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3174
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #48
	sub	sp, sp, #48
	ldrb	r2, [r3, #76]	@ zero_extendqisi2
	mov	r8, r3
	cmp	r2, #0
	beq	.L3159
	ldr	r4, .L3174+4
	movw	r7, #4097
	ldr	r6, .L3174+8
	ldr	r0, .L3174+12
	ldr	r3, [r4, #768]
	ldrh	r5, [r6, #-210]
	mov	r1, r5
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldrh	r0, [r4, #788]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4, #836]
	bl	FtlGcRefreshOpenBlock
	add	r0, r4, #788
	bl	allocate_new_data_superblock
	add	r0, r4, #836
	bl	allocate_new_data_superblock
	lsl	r9, r5, #1
.L3161:
	subs	r7, r7, #1
	beq	.L3165
	movs	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	ldr	r3, [r4, #768]
	ldrh	r3, [r3, r9]
	cmp	r3, #0
	bne	.L3161
.L3165:
	ldr	r3, [r4, #768]
	mov	r1, r5
	ldr	r0, .L3174+12
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldr	r3, [r4, #768]
	ldrh	r7, [r3, r5, lsl #1]
	cbnz	r7, .L3163
	add	r0, sp, #48
	mov	r10, #36
	movw	r9, #65535
	strh	r5, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldr	r3, .L3174+4
	ldrh	ip, [r8, #2828]
	ldr	r8, [r3, #744]
	mov	r3, r7
	mov	lr, r3
	add	r0, sp, #14
.L3166:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3173
	ldrh	r2, [r0, #2]!
	cmp	r2, r9
	beq	.L3167
	mla	r1, r10, r7, r8
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [r1, #4]
	str	lr, [r1, #8]
	str	lr, [r1, #12]
.L3167:
	adds	r3, r3, #1
	b	.L3166
.L3173:
	ldr	r3, [r4, #768]
	mov	r1, r5
	ldr	r0, .L3174+16
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	movs	r1, #0
	mov	r2, r7
	ldr	r0, [r4, #744]
	bl	FlashEraseBlocks
	ldr	r0, [r4, #744]
	movs	r1, #1
	mov	r2, r7
	bl	FlashEraseBlocks
.L3163:
	movw	r3, #65535
	strh	r3, [r6, #-210]	@ movhi
.L3159:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3175:
	.align	2
.L3174:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC135
	.word	.LC136
	.fnend
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
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
	ldr	r5, .L3196
	movw	r6, #65535
	ldr	r7, .L3196+4
	ldr	r4, .L3196+8
	ldrh	r0, [r5, #2832]
	strh	r3, [r7, #-208]	@ movhi
	strh	r6, [r7, #-210]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r3, [r4, #1064]
	cmp	r3, r6
	bne	.L3177
.L3179:
	mov	r6, #-1
	b	.L3178
.L3177:
	bl	FtlLoadSysInfo
	mov	r6, r0
	cmp	r0, #0
	bne	.L3179
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r5, #2938]
	ldr	r2, [r4, #932]
	movs	r0, #12
	mov	r3, r6
.L3180:
	cmp	r3, r1
	bge	.L3185
	mla	lr, r0, r3, r2
	ldr	lr, [lr, #4]
	cmp	lr, #0
	bge	.L3181
.L3185:
	ldrh	r2, [r4, #1040]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #1040]	@ movhi
	bge	.L3195
	b	.L3182
.L3181:
	adds	r3, r3, #1
	b	.L3180
.L3195:
	ldrh	r3, [r7, #-208]
	cmp	r3, #0
	beq	.L3186
.L3182:
	ldr	r0, .L3196+12
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L3196+16
	bl	FtlSuperblockPowerLostFix
	ldrh	r1, [r4, #788]
	ldr	r2, [r4, #768]
	ldrh	r0, [r4, #792]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r1, [r4, #768]
	ldrh	r3, [r5, #2898]
	ldrh	r0, [r4, #836]
	ldrh	r7, [r4, #840]
	strh	r3, [r4, #790]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #794]
	strh	r3, [r4, #792]	@ movhi
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r7
	strh	r2, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r5, #2898]
	ldr	r0, .L3196+20
	strb	r3, [r4, #842]
	strh	r2, [r4, #838]	@ movhi
	strh	r3, [r4, #840]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	r0, .L3196+24
	bl	FtlMapBlkWriteDumpData
	ldrh	r3, [r4, #1042]
	adds	r3, r3, #1
	strh	r3, [r4, #1042]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3186:
	ldrh	r2, [r4, #788]
	movw	r3, #65535
	ldr	r5, .L3196+8
	cmp	r2, r3
	bne	.L3187
.L3189:
	ldrh	r3, [r4, #1040]
	lsls	r3, r3, #27
	bne	.L3178
	bl	FtlVpcCheckAndModify
	b	.L3178
.L3187:
	ldrh	r3, [r5, #792]
	cmp	r3, #0
	bne	.L3189
	ldrh	r3, [r5, #840]
	cmp	r3, #0
	bne	.L3189
	bl	FtlVpcTblFlush
	ldrh	r0, [r5, #788]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r5, #836]
	bl	FtlGcRefreshOpenBlock
	add	r0, r5, #788
	bl	allocate_new_data_superblock
	add	r0, r5, #836
	bl	allocate_new_data_superblock
	addw	r0, r5, #4088
	bl	FtlMapBlkWriteDumpData
	mov	r5, #4096
.L3190:
	movs	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r5, r5, #1
	bne	.L3190
	b	.L3189
.L3178:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L3197:
	.align	2
.L3196:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2+788
	.word	.LANCHOR2+836
	.word	.LANCHOR2+1864
	.word	.LANCHOR2+4088
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	1
	.global	FtlInit
	.thumb
	.thumb_func
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r2, .L3212
	mov	r3, #-1
	ldr	r6, .L3212+4
	ldr	r5, .L3212+8
	ldr	r1, .L3212+12
	ldr	r0, .L3212+16
	str	r3, [r6, #504]
	movs	r3, #0
	str	r3, [r2, #-56]
	str	r3, [r5, #740]
	bl	printk
	mov	r0, r4
	ldr	r4, .L3212+20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #2832]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3199
	ldr	r0, .L3212+24
	b	.L3211
.L3199:
	bl	FtlSysBlkInit
	cbz	r0, .L3201
	ldr	r0, .L3212+28
.L3211:
	ldr	r1, .L3212+32
	bl	printk
	b	.L3200
.L3201:
	movs	r1, #1
	str	r1, [r6, #504]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r5, #784]
	cmp	r3, #15
	bhi	.L3202
	mov	r4, #1024
.L3203:
	movs	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L3203
	b	.L3200
.L3202:
	ldrb	r3, [r4, #76]	@ zero_extendqisi2
	cbz	r3, .L3200
	movs	r4, #128
.L3205:
	movs	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L3205
.L3200:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L3213:
	.align	2
.L3212:
	.word	.LANCHOR4
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LC75
	.word	.LC74
	.word	.LANCHOR0
	.word	.LC137
	.word	.LC138
	.word	.LANCHOR3+40
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
	ldr	r6, .L3218
	ldr	r4, .L3218+4
	movs	r5, #0
	mov	r1, r6
	str	r5, [r1, #-48]!
	str	r5, [r4, #3980]
	str	r0, [r6, #-52]
	addw	r0, r4, #3980
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #3980]
	cbz	r3, .L3217
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r6, #-52]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #3980]
	bl	FlashInit
	mov	r4, r0
	cbnz	r0, .L3216
	ldr	r0, .L3218+8
	bl	FtlInit
.L3216:
	mov	r1, r4
	ldr	r0, .L3218+12
	bl	printk
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3217:
	mov	r0, #-1
	pop	{r4, r5, r6, pc}
.L3219:
	.align	2
.L3218:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR0+1864
	.word	.LC139
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	1
	.global	FtlRead
	.thumb
	.thumb_func
	.type	FtlRead, %function
FtlRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	cmp	r0, #16
	.pad #60
	sub	sp, sp, #60
	mov	r6, r1
	mov	r7, r3
	str	r2, [sp, #16]
	bne	.L3221
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	b	.L3222
.L3221:
	ldr	r3, [sp, #16]
	adds	r3, r1, r3
	str	r3, [sp, #12]
	ldr	r3, .L3261
	ldr	r1, [sp, #12]
	ldr	r2, [r3, #2940]
	cmp	r1, r2
	bhi	.L3242
	ldrh	r4, [r3, #2904]
	mov	r0, r6
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r1, r4
	ldr	r3, [sp, #12]
	str	r0, [sp, #4]
	subs	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	ldr	r1, [sp, #16]
	rsb	r3, r3, #1
	str	r0, [sp, #8]
	add	r3, r3, r0
	str	r3, [sp]
	ldr	r3, .L3261+4
	ldr	r0, [sp, #4]
	ldr	r2, [r3, #980]
	add	r2, r2, r1
	ldr	r1, [sp]
	str	r2, [r3, #980]
	ldr	r2, [r3, #952]
	add	r2, r2, r1
	ldr	r1, [sp, #8]
	str	r2, [r3, #952]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3223
	bl	FtlCacheWriteBack
.L3223:
	mov	r9, #0
	ldr	r4, [sp, #4]
	ldr	r10, .L3261+4
	ldr	r8, .L3261
	mov	r5, r9
	str	r9, [sp, #20]
	str	r9, [sp, #24]
.L3224:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L3260
	add	r1, sp, #52
	movs	r2, #0
	mov	r0, r4
	bl	log2phys
	ldr	r2, [sp, #52]
	adds	r1, r2, #1
	bne	.L3257
	mov	fp, #0
.L3225:
	ldrh	r0, [r8, #2904]
	cmp	fp, r0
	bcs	.L3229
	mla	r0, r0, r4, fp
	cmp	r0, r6
	bcc	.L3227
	ldr	r3, [sp, #12]
	cmp	r0, r3
	bcs	.L3227
	subs	r0, r0, r6
	mov	r1, #512
	add	r0, r7, r0, lsl #9
	bl	__memzero
.L3227:
	add	fp, fp, #1
	b	.L3225
.L3257:
	ldr	r1, [r10, #1764]
	mov	fp, #36
	ldr	r3, [sp, #4]
	mla	fp, fp, r5, r1
	cmp	r4, r3
	str	r2, [fp, #4]
	bne	.L3230
	ldr	r2, [r10, #1788]
	mov	r0, r6
	str	r2, [fp, #8]
	ldrh	r2, [r8, #2904]
	mov	r1, r2
	str	r2, [sp, #20]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #20]
	ldr	r0, [sp, #16]
	str	r1, [sp, #28]
	subs	r1, r2, r1
	cmp	r1, r0
	mov	r3, r1
	it	cs
	movcs	r3, r0
	cmp	r3, r2
	str	r3, [sp, #20]
	bne	.L3231
	str	r7, [fp, #8]
	b	.L3231
.L3230:
	ldr	r3, [sp, #8]
	cmp	r4, r3
	bne	.L3232
	ldr	r2, [r10, #1792]
	ldrh	r1, [r8, #2904]
	ldr	r3, [sp, #12]
	str	r2, [fp, #8]
	mul	r2, r1, r4
	rsb	r9, r2, r3
	cmp	r9, r1
	bne	.L3231
	b	.L3258
.L3232:
	ldrh	r2, [r8, #2904]
	muls	r2, r4, r2
.L3258:
	subs	r2, r2, r6
	add	r2, r7, r2, lsl #9
	str	r2, [fp, #8]
.L3231:
	ldrh	r2, [r8, #2910]
	ldr	r1, [r10, #1800]
	str	r4, [fp, #16]
	muls	r2, r5, r2
	adds	r5, r5, #1
	bic	r2, r2, #3
	add	r2, r2, r1
	str	r2, [fp, #12]
.L3229:
	ldr	r3, [sp]
	adds	r4, r4, #1
	subs	r3, r3, #1
	str	r3, [sp]
	beq	.L3233
	ldrh	r2, [r8, #2828]
	cmp	r5, r2, lsl #3
	bne	.L3224
.L3233:
	cmp	r5, #0
	beq	.L3224
	ldr	r0, [r10, #1764]
	mov	r1, r5
	movs	r2, #0
	mov	fp, #0
	bl	FlashReadPages
	ldr	r3, [sp, #28]
	lsls	r3, r3, #9
	str	r3, [sp, #32]
	ldr	r3, [sp, #20]
	lsls	r3, r3, #9
	str	r3, [sp, #36]
	lsl	r3, r9, #9
	str	r3, [sp, #40]
.L3239:
	movs	r3, #36
	ldr	r2, [r10, #1764]
	mul	ip, r3, fp
	ldr	r3, [sp, #4]
	add	r2, r2, ip
	ldr	r1, [r2, #16]
	cmp	r1, r3
	bne	.L3235
	ldr	r1, [r2, #8]
	ldr	r2, [r10, #1788]
	cmp	r1, r2
	bne	.L3236
	ldr	r3, [sp, #32]
	mov	r0, r7
	str	ip, [sp, #44]
	add	r1, r1, r3
	ldr	r2, [sp, #36]
	b	.L3259
.L3235:
	ldr	r3, [sp, #8]
	cmp	r1, r3
	bne	.L3236
	ldr	r1, [r2, #8]
	ldr	r2, [r10, #1792]
	cmp	r1, r2
	bne	.L3236
	ldrh	r0, [r8, #2904]
	ldr	r2, [sp, #40]
	str	ip, [sp, #44]
	muls	r0, r3, r0
	subs	r0, r0, r6
	add	r0, r7, r0, lsl #9
.L3259:
	bl	ftl_memcpy
	ldr	ip, [sp, #44]
.L3236:
	ldr	r2, [r10, #1764]
	add	lr, r2, ip
	ldr	r0, [r2, ip]
	adds	r3, r0, #1
	itttt	eq
	streq	r0, [sp, #24]
	ldreq	r1, [r10, #1200]
	addeq	r1, r1, #1
	streq	r1, [r10, #1200]
	ldr	r2, [r2, ip]
	cmp	r2, #256
	bne	.L3238
	ldr	r0, [lr, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3238:
	add	fp, fp, #1
	cmp	fp, r5
	bne	.L3239
	movs	r5, #0
	b	.L3224
.L3260:
	ldr	r3, .L3261+4
	ldrh	r3, [r3, #1698]
	cbz	r3, .L3241
	ldr	r0, [sp]
	movs	r1, #1
	bl	rk_ftl_garbage_collect
.L3241:
	ldr	r0, [sp, #24]
	b	.L3222
.L3242:
	mov	r0, #-1
.L3222:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3262:
	.align	2
.L3261:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlRead, .-FtlRead
	.align	1
	.global	FtlWrite
	.thumb
	.thumb_func
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r5, .L3334
	mov	r8, r1
	str	r3, [sp, #8]
	mov	fp, r2
	ldr	r3, [r5, #740]
	cmp	r3, #0
	bne	.L3305
	cmp	r0, #16
	bne	.L3265
	add	r0, r1, #256
	mov	r1, r2
	ldr	r2, [sp, #8]
	bl	FtlVendorPartWrite
	b	.L3264
.L3265:
	ldr	r10, .L3334+8
	adds	r7, r1, r2
	ldr	r3, [r10, #2940]
	cmp	r7, r3
	bhi	.L3306
	ldrh	r4, [r10, #2904]
	mov	r3, #2048
	ldr	r6, .L3334+4
	mov	r0, r1
	mov	r1, r4
	str	r3, [r6, #-44]
	bl	__aeabi_uidiv
	mov	r1, r4
	str	r0, [sp, #4]
	subs	r0, r7, #1
	bl	__aeabi_uidiv
	cmp	fp, r4, lsl #1
	add	r7, r5, #788
	ldr	r2, [sp, #4]
	str	r0, [sp, #20]
	rsb	r9, r2, r0
	add	r3, r9, #1
	str	r3, [sp, #12]
	ldr	r2, [sp, #12]
	ldr	r3, [r5, #960]
	add	r3, r3, r2
	ldr	r2, [r10, #2952]
	str	r3, [r5, #960]
	ldr	r3, [r5, #976]
	add	r3, r3, fp
	str	r3, [r5, #976]
	ite	cs
	movcs	r3, #1
	movcc	r3, #0
	str	r3, [sp, #28]
	add	r3, r5, #836
	cmp	fp, #8
	it	ls
	movls	r7, r3
	cmp	r2, #0
	beq	.L3268
	movs	r3, #36
	ldr	r7, [r10, #2956]
	muls	r3, r2, r3
	ldr	r2, [sp, #4]
	subs	r3, r3, #36
	add	r7, r7, r3
	ldr	r3, [r7, #16]
	cmp	r2, r3
	bne	.L3269
	ldr	r3, [r5, #964]
	mov	r1, r4
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r5, #964]
	ldr	r3, [r6, #-40]
	adds	r3, r3, #1
	str	r3, [r6, #-40]
	bl	__aeabi_uidivmod
	ldr	r0, [r7, #8]
	subs	r4, r4, r1
	add	r0, r0, r1, lsl #9
	cmp	r4, fp
	ldr	r1, [sp, #8]
	it	cs
	movcs	r4, fp
	lsls	r5, r4, #9
	mov	r2, r5
	bl	ftl_memcpy
	cmp	r9, #0
	bne	.L3270
	ldr	r3, [r6, #-40]
	cmp	r3, #2
	ble	.L3305
.L3270:
	ldr	r3, [sp, #8]
	rsb	fp, r4, fp
	add	r8, r8, r4
	str	r9, [sp, #12]
	add	r3, r3, r5
	str	r3, [sp, #8]
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
.L3269:
	ldr	r3, .L3334+4
	movs	r2, #0
	ldr	r7, [r3, #-64]
	str	r2, [r3, #-40]
.L3268:
	ldr	r0, [sp, #4]
	ldr	r1, [sp, #20]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3271
	bl	FtlCacheWriteBack
.L3271:
	ldr	r4, .L3334+8
	ldr	r3, .L3334+4
	ldr	r6, [sp, #4]
	mov	r10, r4
	str	r7, [r3, #-64]
.L3272:
	ldr	r3, [sp, #12]
	ldr	r5, .L3334
	cmp	r3, #0
	beq	.L3333
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3273
	add	r2, r5, #788
	ldr	r9, .L3334+20
	cmp	r7, r2
	bne	.L3274
	ldrh	r7, [r5, #840]
	cbnz	r7, .L3275
	add	r0, r5, #836
	bl	allocate_new_data_superblock
	str	r7, [r9, #3004]
.L3275:
	ldr	r0, .L3334+12
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3004]
	cbnz	r3, .L3308
.L3277:
	ldr	r7, .L3334+12
	b	.L3276
.L3274:
	str	r3, [r9, #3004]
	ldrh	r3, [r5, #792]
	cmp	r3, #0
	bne	.L3277
	mov	r0, r7
	bl	allocate_new_data_superblock
	b	.L3276
.L3308:
	ldr	r7, .L3334+16
.L3276:
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3278
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3278:
	ldr	r3, .L3334+4
	str	r7, [r3, #-64]
.L3273:
	ldr	r2, [r5, #1760]
	ldr	r3, [r4, #2952]
	ldrh	ip, [r7, #4]
	subs	r3, r2, r3
	ldr	r2, [sp, #12]
	cmp	ip, r3
	ldr	r9, .L3334
	it	cs
	movcs	ip, r3
	cmp	ip, r2
	mov	r3, ip
	it	cs
	movcs	r3, r2
	str	r3, [sp, #44]
	movs	r3, #0
	str	r3, [sp, #24]
.L3279:
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3280
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	beq	.L3280
	ldr	r3, [sp, #28]
	cbz	r3, .L3281
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3281
	ldr	r3, [sp, #24]
	cbz	r3, .L3281
	ldrh	r2, [r4, #2904]
	add	r3, r8, fp
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3280
.L3281:
	add	r1, sp, #48
	movs	r2, #0
	mov	r0, r6
	movs	r5, #36
	bl	log2phys
	mov	r0, r7
	bl	get_new_active_ppa
	ldr	r3, [r4, #2952]
	ldr	r1, [r4, #2956]
	ldrh	r2, [r4, #2910]
	mla	r1, r5, r3, r1
	mul	ip, r3, r2
	str	r6, [r1, #16]
	str	r0, [r1, #4]
	lsr	r0, ip, #2
	str	r0, [sp, #36]
	ldr	r0, [r9, #1804]
	mov	lr, r0
	str	r0, [sp, #40]
	ldr	r0, [sp, #36]
	add	r0, lr, r0, lsl #2
	ldrh	lr, [r4, #2908]
	str	r0, [sp, #16]
	str	r0, [r1, #12]
	mul	r3, r3, lr
	ldr	r0, [r9, #1784]
	bic	r3, r3, #3
	add	r3, r3, r0
	ldr	r0, [sp, #16]
	str	r3, [r1, #8]
	movs	r1, #0
	bl	ftl_memset
	ldr	r3, [sp, #4]
	cmp	r6, r3
	beq	.L3282
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3330
	ldrh	r3, [r4, #2904]
	add	r5, r8, fp
	smulbb	r3, r3, r6
	subs	r5, r5, r3
	movs	r3, #0
	str	r3, [sp, #32]
	uxth	r5, r5
	b	.L3285
.L3282:
	ldrh	r5, [r4, #2904]
	mov	r0, r8
	mov	r1, r5
	bl	__aeabi_uidivmod
	subs	r5, r5, r1
	str	r1, [sp, #32]
	cmp	r5, fp
	it	cs
	movcs	r5, fp
.L3285:
	ldrh	r3, [r4, #2904]
	cmp	r5, r3
	bne	.L3286
	ldr	r3, [sp, #4]
	mov	lr, #36
	cmp	r6, r3
	ittet	ne
	mulne	r1, r6, r5
	ldrne	r3, [sp, #8]
	ldreq	r1, [sp, #8]
	rsbne	r1, r8, r1
	it	ne
	addne	r1, r3, r1, lsl #9
	ldr	r3, [sp, #28]
	cbz	r3, .L3288
	ldr	r2, [r10, #2952]
	ldr	r5, [r10, #2956]
	mla	r2, lr, r2, r5
	str	r1, [r2, #8]
	b	.L3289
.L3288:
	ldr	r0, [r10, #2956]
	ldr	r3, [r10, #2952]
	ldrh	r2, [r10, #2908]
	mla	r3, lr, r3, r0
	ldr	r0, [r3, #8]
	b	.L3331
.L3335:
	.align	2
.L3334:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2+788
	.word	.LANCHOR2+836
	.word	.LANCHOR1
.L3286:
	ldr	r2, [sp, #48]
	movs	r3, #36
	adds	r1, r2, #1
	beq	.L3290
	ldr	r1, [r4, #2956]
	add	r0, sp, #52
	str	r2, [sp, #56]
	ldr	r2, [r4, #2952]
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
	bne	.L3291
	ldr	r3, [r9, #1200]
	adds	r3, r3, #1
	str	r3, [r9, #1200]
	b	.L3293
.L3291:
	ldr	r3, [sp, #16]
	ldr	r3, [r3, #8]
	cmp	r3, r6
	beq	.L3293
	ldr	r3, [r9, #1200]
	mov	r2, r6
	ldr	r0, .L3336
	adds	r3, r3, #1
	str	r3, [r9, #1200]
	ldr	r3, [sp, #16]
	ldr	r1, [r3, #8]
	bl	printk
	b	.L3293
.L3290:
	ldr	r1, [r4, #2956]
	ldr	r2, [r4, #2952]
	mla	r2, r3, r2, r1
	movs	r1, #0
	ldr	r0, [r2, #8]
	ldrh	r2, [r4, #2908]
	bl	ftl_memset
.L3293:
	ldr	r3, [sp, #4]
	mov	lr, #36
	lsls	r2, r5, #9
	cmp	r6, r3
	bne	.L3294
	ldr	r1, [r4, #2952]
	ldr	r5, [r4, #2956]
	ldr	r3, [sp, #32]
	mla	r1, lr, r1, r5
	ldr	r0, [r1, #8]
	ldr	r1, [sp, #8]
	add	r0, r0, r3, lsl #9
	b	.L3331
.L3294:
	ldr	r0, [r4, #2956]
	ldr	r3, [r4, #2952]
	ldrh	r1, [r4, #2904]
	mla	r3, lr, r3, r0
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #8]
	add	r1, r3, r1, lsl #9
	b	.L3331
.L3330:
	ldr	r3, [sp, #28]
	cbz	r3, .L3295
	ldr	r3, [r4, #2952]
	ldr	r2, [r4, #2956]
	mla	r5, r5, r3, r2
	ldrh	r3, [r4, #2904]
	ldr	r2, [sp, #8]
	muls	r3, r6, r3
	rsb	r3, r8, r3
	add	r3, r2, r3, lsl #9
	str	r3, [r5, #8]
	b	.L3289
.L3295:
	ldr	r2, [r4, #2956]
	ldr	r3, [r4, #2952]
	ldrh	r1, [r4, #2904]
	mla	r3, r5, r3, r2
	ldrh	r2, [r4, #2908]
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #8]
	add	r1, r3, r1, lsl #9
.L3331:
	bl	ftl_memcpy
.L3289:
	ldr	r2, [sp, #40]
	movw	r3, #61589
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1, lsl #2]	@ movhi
	ldr	r2, [sp, #16]
	ldr	r3, [r9, #988]
	str	r3, [r2, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	ldr	r2, [sp, #16]
	it	eq
	moveq	r3, #0
	str	r3, [r9, #988]
	ldr	r3, [sp, #16]
	str	r6, [r3, #8]
	adds	r6, r6, #1
	ldr	r3, [sp, #48]
	str	r3, [r2, #12]
	ldrh	r3, [r7]
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, [r4, #2952]
	adds	r3, r3, #1
	str	r3, [r4, #2952]
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp, #24]
	b	.L3279
.L3280:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #24]
	subs	r3, r3, r2
	str	r3, [sp, #12]
	ldr	r3, .L3336+4
	ldr	r2, [r10, #2952]
	ldr	r3, [r3, #1760]
	cmp	r2, r3
	bcs	.L3299
	ldr	r3, [sp, #28]
	cbnz	r3, .L3299
	ldrh	r3, [r7, #4]
	cbz	r3, .L3299
.L3301:
	movs	r3, #0
	str	r3, [sp, #28]
	b	.L3272
.L3299:
	bl	FtlCacheWriteBack
	movs	r3, #0
	str	r3, [r10, #2952]
	ldr	r3, [sp, #12]
	cmp	r3, #3
	bls	.L3301
	b	.L3272
.L3333:
	mov	r0, r3
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #20]
	subs	r1, r3, r2
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r5, #784]
	cmp	r3, #31
	bhi	.L3305
	movs	r4, #16
	movs	r3, #128
	strh	r3, [r5, #1642]	@ movhi
	strh	r3, [r5, #1640]	@ movhi
.L3304:
	movs	r0, #0
	movs	r1, #1
	bl	rk_ftl_garbage_collect
	ldr	r3, [r5, #740]
	cbnz	r3, .L3305
	subs	r4, r4, #1
	bne	.L3304
	b	.L3305
.L3306:
	mov	r0, #-1
	b	.L3264
.L3305:
	movs	r0, #0
.L3264:
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3337:
	.align	2
.L3336:
	.word	.LC140
	.word	.LANCHOR2
	.fnend
	.size	FtlWrite, .-FtlWrite
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
	mov	r3, r4
	add	r1, r5, #256
	movs	r2, #1
	movs	r0, #16
	bl	FtlWrite
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r3, r4, r5, pc}
	.fnend
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	1
	.global	FlashReadFacBbtData
	.thumb
	.thumb_func
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3353
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldrh	r2, [r3, #1876]
	mov	r7, r1
	ldrh	r3, [r3, #1878]
	.pad #40
	sub	sp, sp, #40
	mov	r5, r0
	smulbb	r3, r2, r3
	ldr	r2, .L3353+4
	ldr	r1, [r2, #3984]
	mov	r9, r2
	uxth	r3, r3
	subs	r6, r3, #1
	str	r1, [sp, #12]
	mul	r10, r3, r7
	ldr	r1, [r2, #4016]
	uxth	r6, r6
	sub	r4, r3, #16
	str	r1, [sp, #16]
.L3340:
	cmp	r6, r4
	ble	.L3352
	movs	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L3341
	ldr	r3, [r9, #4016]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L3341
	cbz	r5, .L3347
	cbz	r7, .L3348
.L3345:
	ldr	r1, [r9, #3984]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	movs	r2, #4
	ldr	r0, .L3353+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	movs	r0, #0
	b	.L3342
.L3348:
	ldr	r6, .L3353+4
	mov	lr, #1
.L3343:
	ldr	r2, [r9, #4004]
	uxth	r3, r7
	adds	r7, r7, #1
	cmp	r3, r2
	bcs	.L3345
	ldr	r2, [r6, #3984]
	lsrs	r1, r3, #5
	and	r4, r3, #31
	lsl	r3, lr, r4
	ldr	r0, [r2, r1, lsl #2]
	orrs	r3, r3, r0
	str	r3, [r2, r1, lsl #2]
	b	.L3343
.L3341:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L3340
.L3352:
	mov	r0, #-1
	b	.L3342
.L3347:
	mov	r0, r5
.L3342:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3354:
	.align	2
.L3353:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC141
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
	ldr	r3, .L3365
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #1788]
	ldr	r6, .L3365+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #4012]
	smulbb	r4, r4, r3
	uxth	r4, r4
	adds	r2, r4, #7
	asrs	r2, r2, #3
	bl	FlashReadFacBbtData
	adds	r0, r0, #1
	bne	.L3356
.L3360:
	movs	r3, #0
	b	.L3357
.L3356:
	movs	r1, #0
	lsr	lr, r4, #4
	mov	ip, #1
	subs	r4, r4, #1
	mov	r3, r1
.L3358:
	uxth	r2, r1
	cmp	r2, r4
	bge	.L3357
	ldr	r7, [r6, #4012]
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
	bcc	.L3358
	b	.L3360
.L3357:
	movw	r2, #65535
	movs	r0, #0
	strh	r2, [r5, r3, lsl #1]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L3366:
	.align	2
.L3365:
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
	ldr	r4, .L3389
	ldr	r5, [r4, #740]
	cmp	r5, #0
	bne	.L3368
	bl	FtlBbtMemInit
	ldr	r7, .L3389+4
	bl	FtlLoadFactoryBbt
	mov	fp, r4
	sub	r9, r7, #18
	subw	r8, r7, #2996
.L3369:
	ldrh	r3, [r8, #2850]
	ldr	r6, .L3389+8
	cmp	r5, r3
	bcs	.L3387
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r4, #756]
	ldr	r10, [r4, #1796]
	cmp	r3, r2
	str	r0, [r4, #4056]
	str	r10, [r4, #4060]
	beq	.L3370
	ldrh	ip, [r8, #2894]
	movs	r1, #1
	ldr	r0, .L3389+12
	mov	r2, r1
	mla	ip, ip, r5, r3
	lsl	r3, ip, #10
	str	ip, [sp, #4]
	str	r3, [fp, #4052]
	bl	FlashReadPages
	ldrh	r2, [r8, #2894]
	ldr	r0, [r7]
	adds	r2, r2, #7
	ldr	r1, [fp, #4056]
	asrs	r2, r2, #3
	bl	ftl_memcpy
	ldr	ip, [sp, #4]
	b	.L3371
.L3370:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [fp, #4056]
	ldr	r1, [r7]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r8, #2894]
.L3373:
	subs	r6, r6, #1
	uxth	r6, r6
.L3372:
	ldr	r3, .L3389+8
	ldrh	r0, [r3, #2894]
	smlabb	r0, r0, r5, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L3373
	ldr	r0, [r4, #1796]
	movs	r1, #16
	strh	r6, [r9]	@ movhi
	bl	__memzero
	movw	r3, #61664
	strh	r3, [r10]	@ movhi
	movs	r3, #0
	str	r3, [r10, #4]
	ldrh	r3, [r9]
	ldrh	ip, [r8, #2894]
	ldrh	r2, [r4, #1860]
	strh	r3, [r10, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r7]
	lsls	r2, r2, #2
	ldr	r0, [r4, #4056]
	mla	ip, ip, r5, r3
	lsl	r3, ip, #10
	str	r3, [r4, #4052]
	str	ip, [sp, #4]
	bl	ftl_memcpy
	movs	r1, #1
	ldr	r0, .L3389+12
	mov	r2, r1
	bl	FlashEraseBlocks
	movs	r1, #1
	ldr	r0, .L3389+12
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #4048]
	adds	r3, r3, #1
	ldr	ip, [sp, #4]
	bne	.L3371
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L3372
.L3371:
	uxth	r0, ip
	adds	r5, r5, #1
	bl	FtlBbmMapBadBlock
	adds	r7, r7, #4
	b	.L3369
.L3387:
	movs	r5, #0
.L3376:
	ldrh	r3, [r6, #2912]
	uxth	r0, r5
	ldr	r7, .L3389+8
	adds	r5, r5, #1
	cmp	r3, r0
	bls	.L3388
	bl	FtlBbmMapBadBlock
	b	.L3376
.L3388:
	ldrh	r5, [r7, #2980]
	movw	r8, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L3378:
	ldrh	r3, [r6, #2980]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L3382
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L3379
	mov	r0, r5
	bl	FlashTestBlk
	cbz	r0, .L3380
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L3379
.L3380:
	ldrh	r3, [r7, #2968]
	cmp	r3, r8
	bne	.L3381
	strh	r5, [r7, #2968]	@ movhi
	b	.L3379
.L3381:
	ldr	r3, .L3389+8
	strh	r5, [r3, #2972]	@ movhi
	b	.L3382
.L3379:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L3378
.L3382:
	ldr	r0, [r4, #744]
	movs	r5, #0
	ldrh	r3, [r6, #2968]
	movs	r2, #2
	str	r5, [r6, #2976]
	movs	r1, #1
	strh	r5, [r6, #2970]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r6, #2972]
	lsls	r3, r3, #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r6, #2968]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r6, #2972]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r6, #2976]
	ldrh	r2, [r6, #2972]
	adds	r3, r3, #1
	str	r3, [r6, #2976]
	ldrh	r3, [r6, #2968]
	strh	r5, [r6, #2970]	@ movhi
	strh	r2, [r6, #2968]	@ movhi
	strh	r3, [r6, #2972]	@ movhi
	bl	FtlBbmTblFlush
.L3368:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3390:
	.align	2
.L3389:
	.word	.LANCHOR2
	.word	.LANCHOR0+2996
	.word	.LANCHOR0
	.word	.LANCHOR2+4048
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
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
	ldr	r4, .L3425
	ldr	r3, [r4, #740]
	cmp	r3, #0
	bne	.L3393
	ldr	r5, .L3425+4
	str	r3, [r4, #984]
	str	r3, [r4, #988]
	ldrh	r0, [r5, #2832]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3394
	bl	FtlMakeBbt
.L3394:
	ldr	r0, .L3425+8
	movs	r2, #0
.L3395:
	ldrh	r1, [r5, #2904]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	bge	.L3422
	ldr	r6, [r4, #1788]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r6, r3, lsl #2]
	ldr	r1, [r4, #1792]
	str	r0, [r1, r3, lsl #2]
	b	.L3395
.L3422:
	ldr	r3, .L3425+4
	movs	r6, #0
	ldrh	r7, [r3, #2836]
.L3397:
	ldrh	r3, [r5, #2838]
	ldr	r8, .L3425+4
	cmp	r3, r7
	bls	.L3423
	mov	r0, r7
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r7, r7, #1
	uxth	r7, r7
	add	r0, r0, r6
	uxth	r6, r0
	b	.L3397
.L3423:
	ldrh	r1, [r8, #2828]
	subs	r3, r6, #3
	cmp	r3, r1, lsl #1
	bge	.L3399
.L3403:
	movs	r6, #0
	mov	r7, r6
	b	.L3400
.L3399:
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, [r8, #2932]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r8, #2832]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r6, [r8, #2836]
.L3401:
	ldrh	r3, [r5, #2838]
	cmp	r3, r6
	bls	.L3403
	mov	r0, r6
	adds	r6, r6, #1
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	uxth	r6, r6
	b	.L3401
.L3400:
	ldrh	r3, [r5, #2836]
	uxth	r0, r6
	ldr	r10, .L3425+4
	adds	r6, r6, #1
	cmp	r3, r0
	bls	.L3424
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r0, r7
	uxth	r7, r0
	b	.L3400
.L3424:
	ldrh	r3, [r10, #2838]
	ldrh	r6, [r10, #2828]
	ldr	fp, [r10, #2840]
	str	r3, [r10, #2964]
	mov	r1, r6
	mov	r0, fp
	bl	__aeabi_uidiv
	ubfx	r8, r0, #5, #16
	mov	r9, r0
	add	r3, r8, #36
	strh	r3, [r4, #1060]	@ movhi
	movs	r3, #24
	str	r0, [r10, #2960]
	muls	r3, r6, r3
	cmp	r7, r3
	ble	.L3405
	rsb	r0, r7, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, .L3425
	str	r0, [r10, #2960]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r3, #1060]	@ movhi
.L3405:
	ldr	r3, [r4, #948]
	ldr	fp, .L3425
	cmp	r3, #1
	bne	.L3406
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r10, [fp, #1060]
	uxtah	r0, r10, r0
	add	r10, r10, r0, asr #2
	strh	r10, [fp, #1060]	@ movhi
.L3406:
	ldrb	r3, [r5, #76]	@ zero_extendqisi2
	cbz	r3, .L3407
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r10, [r4, #1060]
	uxtah	r0, r10, r0
	add	r10, r10, r0, asr #2
	strh	r10, [r4, #1060]	@ movhi
.L3407:
	ldrh	r3, [r5, #2890]
	cbz	r3, .L3409
	ldrh	r2, [r4, #1060]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #1060]	@ movhi
	mul	r2, r6, r3
	cmp	r2, r7
	itttt	gt
	addgt	r3, r3, #32
	ldrgt	r2, .L3425+4
	addgt	r3, r3, r8
	strgt	r9, [r2, #2960]
	itt	gt
	ldrgt	r2, .L3425
	strhgt	r3, [r2, #1060]	@ movhi
.L3409:
	ldrh	r2, [r4, #1060]
	ldr	r3, [r5, #2960]
	ldr	r7, .L3425
	subs	r3, r3, r2
	muls	r6, r3, r6
	ldrh	r3, [r5, #2898]
	str	r6, [r4, #4084]
	muls	r6, r3, r6
	ldrh	r3, [r5, #2904]
	str	r6, [r5, #2960]
	muls	r6, r3, r6
	str	r6, [r5, #2940]
	bl	FtlBbmTblFlush
	ldrh	r2, [r5, #2838]
	movs	r1, #0
	ldr	r0, [r4, #768]
	movw	r6, #65535
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2836]
	ldr	r0, [r4, #944]
	movs	r1, #255
	movs	r3, #0
	strh	r6, [r4, #1076]	@ movhi
	lsrs	r2, r2, #3
	str	r3, [r4, #940]
	strh	r3, [r4, #1078]	@ movhi
	strb	r3, [r4, #1082]
	strb	r3, [r4, #1084]
	strh	r3, [r4, #790]	@ movhi
	strb	r3, [r4, #794]
	strh	r3, [r4, #788]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #796]
	bl	ftl_memset
.L3411:
	ldr	r5, .L3425
	add	r0, r5, #788
	bl	make_superblock
	ldrb	r3, [r4, #795]	@ zero_extendqisi2
	ldrh	r2, [r4, #788]
	cbnz	r3, .L3412
	ldr	r3, [r7, #768]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r7, #788]
	adds	r3, r3, #1
	strh	r3, [r7, #788]	@ movhi
	b	.L3411
.L3412:
	ldr	r3, [r5, #984]
	movw	r7, #65535
	ldrh	r1, [r5, #792]
	str	r3, [r5, #800]
	adds	r3, r3, #1
	str	r3, [r5, #984]
	ldr	r3, [r5, #768]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r5, #838]	@ movhi
	strb	r3, [r5, #842]
	ldrh	r3, [r5, #788]
	adds	r3, r3, #1
	strh	r3, [r5, #836]	@ movhi
	movs	r3, #1
	strb	r3, [r5, #844]
.L3413:
	ldr	r6, .L3425
	add	r0, r6, #836
	bl	make_superblock
	ldrb	r3, [r4, #843]	@ zero_extendqisi2
	ldrh	r2, [r4, #836]
	cbnz	r3, .L3414
	ldr	r3, [r5, #768]
	strh	r7, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r5, #836]
	adds	r3, r3, #1
	strh	r3, [r5, #836]	@ movhi
	b	.L3413
.L3414:
	ldr	r3, [r6, #984]
	movw	r4, #65535
	ldrh	r1, [r6, #840]
	str	r3, [r6, #848]
	adds	r3, r3, #1
	str	r3, [r6, #984]
	ldr	r3, [r6, #768]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r4, [r6, #884]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r3, [r6, #1066]	@ movhi
	ldr	r3, [r6, #4084]
	strh	r4, [r6, #1068]	@ movhi
	strh	r3, [r6, #1070]	@ movhi
	ldr	r3, [r6, #984]
	str	r3, [r6, #1072]
	adds	r3, r3, #1
	str	r3, [r6, #984]
	strh	r0, [r6, #1064]	@ movhi
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L3393
	ldr	r3, .L3425+12
	movs	r2, #1
	str	r2, [r3, #504]
.L3393:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3426:
	.align	2
.L3425:
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #16
	ldr	r4, .L3456
	ldrb	r3, [r4, #76]	@ zero_extendqisi2
	cbz	r3, .L3428
.L3430:
	movs	r0, #0
	b	.L3429
.L3428:
	ldr	r5, .L3456+4
	ldr	r0, [r5, #3980]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3430
	bl	FlashLoadFactorBbt
	cbz	r0, .L3431
	bl	FlashMakeFactorBbt
.L3431:
	ldr	r0, [r5, #4012]
	bl	FlashReadIdbDataRaw
	cbz	r0, .L3432
	movs	r1, #0
	movs	r2, #16
	mov	r0, sp
	movs	r6, #1
	bl	FlashReadFacBbtData
	movs	r3, #0
	mov	r2, r3
	ldr	r1, [sp]
.L3433:
	lsl	r5, r6, r2
	adds	r0, r3, #1
	tst	r5, r1
	add	r2, r2, #1
	it	ne
	movne	r3, r0
	cmp	r2, #16
	bne	.L3433
	cmp	r3, #6
	bls	.L3452
	movs	r2, #0
	movs	r6, #1
.L3436:
	lsl	r5, r6, r2
	adds	r0, r3, #1
	tst	r5, r1
	add	r2, r2, #1
	it	ne
	movne	r3, r0
	cmp	r2, #24
	bne	.L3436
	cmp	r3, #17
	bhi	.L3437
.L3452:
	strb	r2, [r4, #1]
	b	.L3435
.L3437:
	movs	r3, #36
	strb	r3, [r4, #1]
.L3435:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strh	r3, [r4, #1890]	@ movhi
.L3432:
	ldr	r1, .L3456+8
	ldr	r0, .L3456+12
	bl	printk
	ldr	r0, .L3456+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #2832]
	bl	FtlFreeSysBlkQueueInit
	movs	r4, #1
.L3438:
	bl	FtlLoadBbt
	cbz	r0, .L3439
.L3454:
	bl	FtlLowFormat
	cmp	r4, #3
	bhi	.L3455
	adds	r4, r4, #1
	b	.L3438
.L3455:
	mov	r0, #-1
	b	.L3429
.L3439:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3454
	ldr	r3, .L3456+20
	movs	r2, #1
	str	r2, [r3, #504]
.L3429:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L3457:
	.align	2
.L3456:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC75
	.word	.LC74
	.word	.LANCHOR0+1864
	.word	.LANCHOR1
	.fnend
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
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
	ldr	r3, .L3460
	ldr	r2, [r3, #2772]
	ldr	r3, .L3460+4
	cmp	r2, r3
	bne	.L3458
	b	flash_enter_slc_mode
.L3458:
	bx	lr
.L3461:
	.align	2
.L3460:
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
	ldr	r3, .L3464
	ldr	r2, [r3, #2772]
	ldr	r3, .L3464+4
	cmp	r2, r3
	bne	.L3462
	b	flash_exit_slc_mode
.L3462:
	bx	lr
.L3465:
	.align	2
.L3464:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	1
	.global	IdBlockReadData
	.thumb
	.thumb_func
	.type	IdBlockReadData, %function
IdBlockReadData:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L3474
	mov	r6, r0
	str	r2, [sp, #12]
	mov	r7, r1
	ldr	r0, .L3474+4
	mov	r1, r6
	ldr	r2, [r4, #1788]
	mov	fp, r4
	ldr	r3, [r4, #4]
	ldrb	r5, [r2, #9]	@ zero_extendqisi2
	mov	r2, r7
	smulbb	r5, r5, r3
	bl	printk
	ldr	r3, [r4, #1788]
	mov	r0, r6
	ldrb	r10, [r3, #9]	@ zero_extendqisi2
	uxth	r5, r5
	mov	r1, r5
	movs	r5, #0
	bl	__aeabi_uidivmod
	mul	ip, r10, r1
	mov	r8, r1
	subs	r3, r6, r1
	str	r3, [sp, #4]
	ubfx	ip, ip, #2, #2
.L3467:
	cmp	r5, r7
	bcs	.L3473
	add	r9, r5, r8
	rsb	r3, ip, #4
	ubfx	r9, r9, #2, #16
	ldrb	r2, [r4, #76]	@ zero_extendqisi2
	uxth	r3, r3
	str	r3, [sp, #8]
	add	r3, r4, r9, lsl #1
	ldrh	r3, [r3, #80]
	cbz	r2, .L3468
	ldr	r2, [r4, #2772]
	ldr	r1, .L3474+8
	cmp	r2, r1
	it	eq
	moveq	r3, r9
.L3468:
	ldr	r2, [sp, #4]
	ldr	r9, .L3474+16
	add	r2, ip, r2
	ldr	r1, [fp, #1788]
	ldrb	ip, [fp, #1892]	@ zero_extendqisi2
	mla	r3, r10, r3, r2
	ldrb	r1, [r1, #9]	@ zero_extendqisi2
	ldr	r2, [r9, #4012]
	ldrb	r0, [r9, #4010]	@ zero_extendqisi2
	str	ip, [sp, #16]
	str	r2, [sp, #24]
	str	r3, [sp, #28]
	str	r1, [sp, #20]
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #20]
	mov	r0, r3
	bl	__aeabi_uidiv
	mov	r1, r0
	movs	r0, #0
	ldr	r2, [sp, #24]
	mov	r3, r0
	bl	FlashReadPage
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	ip, [sp, #16]
	mov	r0, ip
	bl	FlashBchSel
	ldr	r1, [r9, #4012]
	mov	r2, #2048
	ldr	r3, [sp, #12]
	add	r0, r3, r5, lsl #9
	bl	ftl_memcpy
	mov	ip, #0
	ldr	r3, [sp, #8]
	add	r3, r3, r5
	uxth	r5, r3
	b	.L3467
.L3473:
	mov	r1, r6
	mov	r2, r7
	movs	r3, #0
	ldr	r0, .L3474+12
	bl	printk
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3475:
	.align	2
.L3474:
	.word	.LANCHOR0
	.word	.LC142
	.word	1446522928
	.word	.LC143
	.word	.LANCHOR2
	.fnend
	.size	IdBlockReadData, .-IdBlockReadData
	.align	1
	.global	IDBlockWriteData
	.thumb
	.thumb_func
	.type	IDBlockWriteData, %function
IDBlockWriteData:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r4, .L3492
	mov	r7, r0
	str	r2, [sp, #8]
	mov	r8, r1
	ldr	r0, .L3492+4
	mov	r1, r7
	ldr	r2, [r4, #1788]
	mov	fp, r4
	ldr	r3, [r4, #4]
	ldrb	r5, [r2, #9]	@ zero_extendqisi2
	mov	r2, r8
	smulbb	r5, r5, r3
	bl	printk
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldrh	r1, [r4, #1884]
	mov	r0, r7
	bl	__aeabi_uidiv
	uxth	r5, r5
	mov	r1, r0
	movs	r0, #0
	mov	r2, r0
	bl	FlashEraseBlock
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r4, #1788]
	mov	r1, r5
	mov	r0, r7
	movs	r5, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	str	r3, [sp]
	bl	__aeabi_uidivmod
	ldr	ip, .L3492+16
	rsb	r10, r1, r7
	mov	r9, r1
.L3477:
	cmp	r5, r8
	bcs	.L3491
	add	r3, r5, r9
	ubfx	r3, r3, #2, #16
	cbz	r3, .L3478
	adds	r1, r3, #1
	ldrb	r0, [r4, #76]	@ zero_extendqisi2
	add	r2, r4, r1, lsl #1
	ldrh	r2, [r2, #80]
	cbz	r0, .L3479
	ldr	r0, [fp, #2772]
	cmp	r0, ip
	it	eq
	uxtheq	r2, r1
.L3479:
	subs	r2, r2, #1
	lsls	r2, r2, #2
	str	r2, [sp, #24]
	movs	r2, #0
	str	r2, [sp, #28]
.L3478:
	add	r2, r4, r3, lsl #1
	ldrh	r6, [r2, #80]
	ldrb	r2, [r4, #76]	@ zero_extendqisi2
	cbz	r2, .L3480
	ldr	r2, [fp, #2772]
	cmp	r2, ip
	it	eq
	moveq	r6, r3
.L3480:
	ldr	r3, [sp]
	str	ip, [sp, #20]
	mla	r1, r3, r6, r10
	ldr	r3, [sp, #8]
	add	r2, r3, r5, lsl #9
	ldrb	r3, [r4, #1892]	@ zero_extendqisi2
	str	r2, [sp, #12]
	adds	r5, r5, #4
	str	r3, [sp, #4]
	uxth	r5, r5
	ldr	r3, .L3492+8
	str	r1, [sp, #16]
	ldrb	r0, [r3, #4010]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [r4, #1788]
	ldr	r1, [sp, #16]
	mov	r0, r1
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	mov	r1, r0
	ldr	r2, [sp, #12]
	add	r3, sp, #24
	movs	r0, #0
	bl	FlashProgPage
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #4]
	bl	FlashBchSel
	mov	r0, r10
	ldr	r1, [sp]
	bl	__aeabi_uidiv
	adds	r2, r6, #1
	uxth	r2, r2
	mov	r1, r0
	movs	r0, #0
	bl	FlashPageProgMsbFFData
	ldr	ip, [sp, #20]
	b	.L3477
.L3491:
	mov	r1, r7
	mov	r2, r8
	movs	r3, #0
	ldr	r0, .L3492+12
	bl	printk
	movs	r0, #0
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3493:
	.align	2
.L3492:
	.word	.LANCHOR0
	.word	.LC144
	.word	.LANCHOR2
	.word	.LC145
	.word	1446522928
	.fnend
	.size	IDBlockWriteData, .-IDBlockWriteData
	.align	1
	.global	write_idblock
	.thumb
	.thumb_func
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r0
	ldr	r5, .L3506
	.pad #20
	sub	sp, sp, #20
	mov	r0, #256000
	mov	r8, r1
	mov	r9, r2
	ldr	r3, [r5, #1788]
	ldr	r6, [r5, #4]
	ldrb	r10, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	mov	r7, r0
	cmp	r0, #0
	beq	.L3503
	addw	r4, fp, #511
	lsrs	r4, r4, #9
	cmp	r4, #255
	bhi	.L3496
	add	r0, r8, r4, lsl #9
	mov	r1, r8
	rsb	r2, r4, #256
	bl	memcpy
.L3496:
	adds	r4, r4, #128
	movs	r3, #5
	cmp	r4, #256
	ldr	r0, .L3506+4
	mov	r1, r9
	mov	r2, #4
	it	cs
	movcs	r4, #256
	bl	rknand_print_hex
	ldrb	r2, [r5, #1]	@ zero_extendqisi2
	ldr	r1, [r8, #512]
	ldr	r0, .L3506+8
	bl	printk
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r2, [r8, #512]
	mov	r1, r4
	ldr	r0, .L3506+12
	movs	r5, #0
	cmp	r2, r3
	mov	r2, fp
	it	hi
	strhi	r3, [r8, #512]
	mov	fp, r5
	bl	printk
	smulbb	r10, r10, r6
	mov	r6, r9
	lsls	r3, r4, #7
	uxth	r10, r10
	str	r3, [sp, #8]
.L3502:
	ldr	r2, .L3506
	ldr	r3, [r6]
	ldrh	r2, [r2, #1890]
	cmp	r3, r2
	bcs	.L3498
	ldr	r2, .L3506+16
	ldr	r2, [r2, #4004]
	cmp	r3, r2
	bcc	.L3498
	mov	r0, r7
	mov	r1, #512
	bl	__memzero
	ldr	r0, [r6]
	mov	r1, r4
	mov	r2, r8
	mov	r9, #0
	mul	r0, r0, r10
	bl	IDBlockWriteData
	ldr	r0, [r6]
	mov	r1, r4
	mov	r2, r7
	mul	r0, r0, r10
	bl	IdBlockReadData
.L3501:
	ldr	r2, [r7, r9, lsl #2]
	ldr	r3, [r8, r9, lsl #2]
	cmp	r2, r3
	beq	.L3499
	stmia	sp, {r2, r3}
	mov	r1, fp
	ldr	r2, [r6]
	mov	r3, r9
	ldr	r0, .L3506+20
	bl	printk
	bic	ip, r9, #255
	ldr	r0, .L3506+24
	movs	r2, #4
	lsl	ip, ip, #2
	mov	r3, #256
	add	r1, r8, ip
	str	ip, [sp, #12]
	bl	rknand_print_hex
	mov	r3, #256
	movs	r2, #4
	ldr	r0, .L3506+28
	ldr	ip, [sp, #12]
	add	r1, r7, ip
	bl	rknand_print_hex
	mov	r0, r7
	mov	r1, #512
	bl	__memzero
	ldr	r0, [r6]
	movs	r1, #4
	mov	r2, r7
	mul	r0, r0, r10
	bl	IDBlockWriteData
	ldr	r0, .L3506+32
	bl	printk
	ldr	r3, [sp, #8]
	cmp	r9, r3
	bcc	.L3498
	b	.L3500
.L3499:
	ldr	r3, [sp, #8]
	add	r9, r9, #1
	cmp	r9, r3
	bne	.L3501
.L3500:
	adds	r5, r5, #1
.L3498:
	add	fp, fp, #1
	adds	r6, r6, #4
	cmp	fp, #5
	bne	.L3502
	clz	r5, r5
	mov	r0, r7
	bl	ftl_free
	lsrs	r5, r5, #5
	negs	r0, r5
	b	.L3495
.L3503:
	mov	r0, #-1
.L3495:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3507:
	.align	2
.L3506:
	.word	.LANCHOR0
	.word	.LC146
	.word	.LC147
	.word	.LC148
	.word	.LANCHOR2
	.word	.LC149
	.word	.LC150
	.word	.LC151
	.word	.LC152
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
	ldr	r4, .L3512
	movs	r2, #0
.L3509:
	cmp	r0, r1
	beq	.L3511
	ldrb	r3, [r0], #1	@ zero_extendqisi2
	eor	r3, r3, r2, lsr #24
	add	r3, r4, r3, lsl #2
	ldr	r3, [r3, #3008]
	eor	r2, r3, r2, lsl #8
	b	.L3509
.L3511:
	mov	r0, r2
	pop	{r4, pc}
.L3513:
	.align	2
.L3512:
	.word	.LANCHOR1
	.fnend
	.size	CRC_32, .-CRC_32
	.align	1
	.global	rknand_sys_storage_ioctl
	.thumb
	.thumb_func
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 528
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3619
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	cmp	r1, r3
	.pad #532
	sub	sp, sp, #532
	mov	r4, r1
	mov	r5, r2
	beq	.L3516
	bhi	.L3517
	subw	r3, r3, #3461
	cmp	r1, r3
	beq	.L3518
	bhi	.L3519
	subs	r3, r3, #125
	cmp	r1, r3
	beq	.L3520
	bhi	.L3521
	subs	r3, r3, #237
	cmp	r1, r3
	bne	.L3577
	bl	rknand_dev_flush
	b	.L3608
.L3521:
	ldr	r3, .L3619+4
	cmp	r1, r3
	beq	.L3523
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3524
	b	.L3577
.L3519:
	ldr	r3, .L3619+8
	cmp	r1, r3
	beq	.L3525
	bhi	.L3526
	mvn	r1, #1960
	add	r3, r3, r1
	cmp	r4, r3
	beq	.L3527
	b	.L3577
.L3526:
	ldr	r3, .L3619+12
	cmp	r1, r3
	beq	.L3525
	adds	r3, r3, #10
	cmp	r1, r3
	beq	.L3525
	b	.L3577
.L3517:
	ldr	r3, .L3619+16
	cmp	r1, r3
	beq	.L3528
	bhi	.L3529
	subs	r3, r3, #78
	cmp	r1, r3
	beq	.L3530
	bcc	.L3531
	adds	r3, r3, #21
	cmp	r1, r3
	beq	.L3532
	adds	r3, r3, #56
	cmp	r1, r3
	beq	.L3533
	b	.L3577
.L3529:
	ldr	r3, .L3619+20
	cmp	r1, r3
	mov	r6, r3
	beq	.L3534
	bhi	.L3535
	mvn	r2, #956
	add	r3, r3, r2
	cmp	r1, r3
	beq	.L3536
	add	r3, r3, #956
	cmp	r1, r3
	beq	.L3537
	b	.L3577
.L3535:
	ldr	r3, .L3619+24
	cmp	r1, r3
	beq	.L3537
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3534
	b	.L3577
.L3531:
	ldr	r0, .L3619+28
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r4, r0
	cbnz	r0, .L3538
.L3543:
	mvn	r0, #11
	b	.L3515
.L3538:
	mov	r1, r5
	mov	r2, #512
	bl	rk_copy_from_user
	cbnz	r0, .L3614
	ldr	r0, .L3619+32
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r4, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L3611
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
	beq	.L3607
	ldr	r0, .L3619+36
.L3610:
	bl	printk
.L3611:
	mov	r0, r4
.L3612:
	bl	ftl_free
.L3613:
	mvn	r0, #13
	b	.L3515
.L3530:
	ldr	r0, .L3619+40
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r4, r0
	cmp	r0, #0
	beq	.L3543
	mov	r1, r5
	mov	r2, #4096
	bl	rk_copy_from_user
	cbz	r0, .L3544
.L3614:
	ldr	r0, .L3619+44
	b	.L3610
.L3544:
	ldr	r5, .L3619+48
	ldr	r0, .L3619+52
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r5, #-36]
	mov	r6, r5
	cbnz	r3, .L3545
	mov	r0, #260096
	bl	ftl_malloc
	str	r0, [r5, #-36]
	cmp	r0, #0
	beq	.L3611
.L3545:
	ldr	r2, [r4, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L3611
	ldr	r3, [r4]
	cmp	r3, #251904
	bhi	.L3611
	ldr	r0, [r6, #-36]
	add	r1, r4, #8
	add	r0, r0, r3
	bl	memcpy
.L3607:
	mov	r0, r4
	bl	ftl_free
.L3608:
	movs	r4, #0
	b	.L3542
.L3533:
	ldr	r0, .L3619+56
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3543
	mov	r1, r5
	movs	r2, #28
	bl	rk_copy_from_user
	cbz	r0, .L3547
	ldr	r0, .L3619+44
	bl	printk
	b	.L3548
.L3547:
	ldmia	r6, {r1, r2}
	ldr	r0, .L3619+60
	bl	printk
	ldr	r1, [r6]
	cmp	r1, #256000
	bhi	.L3548
	ldr	r5, .L3619+48
	ldr	r0, [r5, #-36]
	cbz	r0, .L3548
	bl	CRC_32
	ldr	r3, [r6, #4]
	cmp	r3, r0
	beq	.L3550
	mov	r0, r6
	bl	ftl_free
	b	.L3578
.L3550:
	bl	rknand_device_lock
	ldr	r1, [r5, #-36]
	add	r2, r6, #8
	ldr	r0, [r6]
	bl	write_idblock
	movs	r4, #0
	bl	rknand_device_unlock
	ldr	r0, [r5, #-36]
	bl	ftl_free
	str	r4, [r5, #-36]
	mov	r0, r6
	b	.L3555
.L3548:
	mov	r0, r6
	b	.L3612
.L3532:
	ldr	r0, .L3619+64
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3543
	bl	ReadFlashInfo
	mov	r0, r5
	mov	r1, r6
	movs	r2, #11
	b	.L3617
.L3516:
	ldr	r0, .L3619+68
	bl	printk
	bl	rknand_device_lock
	bl	FtlReInitForSDUpdata
	mov	r4, r0
	bl	rknand_device_unlock
	cmp	r4, #0
	bne	.L3613
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3543
	bl	rknand_device_lock
	mov	r1, r4
	movs	r2, #64
	mov	r0, r6
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	ldr	r0, .L3619+72
	mov	r1, r6
	movs	r2, #4
	movs	r3, #8
	bl	rknand_print_hex
	mov	r0, r5
	mov	r1, r6
	movs	r2, #64
	b	.L3617
.L3528:
	ldr	r0, .L3619+76
	bl	printk
	ldr	r3, .L3619+48
	mov	r0, r5
	movs	r2, #4
	ldr	r3, [r3, #-32]
	ldr	r3, [r3, #20]
	add	r1, sp, #4
	str	r3, [sp, #4]
	b	.L3603
.L3536:
	ldr	r0, .L3619+80
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3543
	bl	rknand_device_lock
	movs	r1, #2
	mov	r2, r6
	mov	r0, #520
	bl	FtlVendorPartRead
	bl	rknand_device_unlock
	mov	r2, #1024
	mov	r0, r5
	mov	r1, r6
.L3617:
	bl	rk_copy_to_user
	mov	r4, r0
	mov	r0, r6
	cmp	r4, #0
	bne	.L3612
.L3555:
	bl	ftl_free
	b	.L3542
.L3520:
	ldr	r0, .L3619+84
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	mov	r4, r0
	cbz	r0, .L3556
.L3561:
	ldr	r0, .L3619+44
	bl	printk
	b	.L3613
.L3620:
	.align	2
.L3619:
	.word	1074033155
	.word	1074029570
	.word	1074031656
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC153
	.word	.LC155
	.word	.LC156
	.word	.LC157
	.word	.LC154
	.word	.LANCHOR4
	.word	.LC158
	.word	.LC159
	.word	.LC160
	.word	.LC161
	.word	.LC162
	.word	.LC163
	.word	.LC164
	.word	.LC165
	.word	.LC166
.L3556:
	ldr	r2, [sp, #8]
	ldr	r3, .L3621
	cmp	r2, r3
	beq	.L3557
.L3558:
	mov	r4, #-1
	b	.L3542
.L3557:
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3558
	ldr	r6, .L3621+4
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #-32]
	bl	memcpy
	ldr	r2, [r6, #-28]
	ldr	r3, .L3621+8
	cmp	r2, r3
	beq	.L3559
	add	r0, sp, #72
	movs	r1, #128
	str	r4, [sp, #16]
	str	r4, [sp, #20]
	bl	__memzero
.L3559:
	add	r0, sp, #264
	mov	r1, #256
	movs	r3, #0
	str	r3, [sp, #24]
	bl	__memzero
.L3604:
	add	r1, sp, #8
	mov	r2, #520
	mov	r0, r5
.L3603:
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3613
	b	.L3608
.L3523:
	ldr	r0, .L3621+12
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3561
	ldr	r2, [sp, #8]
	ldr	r3, .L3621
	cmp	r2, r3
	bne	.L3558
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3558
	ldr	r2, .L3621+4
	ldr	r3, .L3621+8
	ldr	r1, [r2, #-28]
	cmp	r1, r3
	bne	.L3578
	ldr	r3, [sp, #20]
	subs	r1, r3, #1
	cmp	r1, #127
	bhi	.L3579
	ldr	r4, [r2, #-32]
	add	r1, sp, #72
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #20]
	bl	memcpy
	movs	r0, #1
	mov	r1, r4
	b	.L3609
.L3527:
	ldr	r0, .L3621+16
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3561
	ldr	r2, [sp, #8]
	ldr	r3, .L3621+20
	cmp	r2, r3
	bne	.L3558
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3558
	ldr	r5, .L3621+4
	ldr	r3, [r5, #-24]
	cbnz	r3, .L3562
.L3565:
	movs	r0, #0
	b	.L3515
.L3562:
	ldr	r3, [r5, #-20]
	ldr	r2, .L3621+24
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3563
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r5, #-20]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3563:
	ldr	r1, [r5, #-20]
	movs	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #-32]
	ldr	r2, .L3621
	ldr	r1, [r3]
	cmp	r1, r2
	it	ne
	strne	r2, [r3]
	ldr	r0, [r5, #-32]
	itt	ne
	movne	r2, #504
	ldrne	r3, .L3621+4
	mov	r1, #128
	add	r0, r0, #64
	itt	ne
	ldrne	r3, [r3, #-32]
	strne	r4, [r3, #8]
	mov	r4, #0
	it	ne
	strne	r2, [r3, #4]
	str	r4, [r0, #-52]
	bl	__memzero
	movs	r0, #1
	ldr	r1, [r5, #-32]
	bl	StorageSysDataStore
	str	r4, [r5, #-24]
	str	r4, [r5, #-28]
	b	.L3542
.L3518:
	ldr	r0, .L3621+28
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3561
	ldr	r2, [sp, #8]
	ldr	r3, .L3621+32
	cmp	r2, r3
	bne	.L3558
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3558
	ldr	r5, .L3621+4
	ldr	r3, [r5, #-24]
	cmp	r3, #1
	beq	.L3565
	ldr	r3, [r5, #-20]
	ldr	r2, .L3621+24
	ldr	r1, [r3]
	cmp	r1, r2
	it	ne
	strne	r2, [r3]
	ldr	r1, [r5, #-20]
	itttt	ne
	movne	r2, #504
	ldrne	r3, [r5, #-20]
	strne	r2, [r3, #4]
	strne	r0, [r3, #8]
	it	ne
	strne	r0, [r3, #12]
	movs	r3, #1
	movs	r0, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #-32]
	ldr	r2, .L3621
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3567
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L3621+4
	ldr	r3, [r3, #-32]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
.L3567:
	ldr	r0, [r5, #-32]
	movs	r1, #128
	movs	r4, #0
	adds	r0, r0, #64
	str	r4, [r0, #-52]
	bl	__memzero
	movs	r0, #1
	ldr	r1, [r5, #-32]
	bl	StorageSysDataStore
	movs	r3, #1
	str	r3, [r5, #-24]
	b	.L3542
.L3524:
	ldr	r0, .L3621+36
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3561
	ldr	r2, [sp, #8]
	ldr	r3, .L3621+40
	cmp	r2, r3
	bne	.L3558
	ldr	r2, [sp, #12]
	cmp	r2, #512
	bhi	.L3558
	add	r0, sp, #16
	ldr	r1, .L3621+44
	b	.L3605
.L3525:
	ldr	r0, .L3621+48
	cmp	r4, r0
	mov	r7, r0
	bne	.L3568
	ldr	r0, .L3621+52
	b	.L3606
.L3568:
	ldr	r3, .L3621+56
	cmp	r4, r3
	ite	eq
	ldreq	r0, .L3621+60
	ldrne	r0, .L3621+64
.L3606:
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3561
	ldr	r2, [sp, #8]
	ldr	r3, .L3621+68
	cmp	r2, r3
	bne	.L3613
	ldr	r3, .L3621+56
	ldr	r6, .L3621+4
	cmp	r4, r3
	bne	.L3571
	ldr	r3, [r6, #-32]
	mov	r0, r5
	add	r1, sp, #8
	movs	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #12]
	strb	r3, [sp, #16]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3515
	b	.L3613
.L3571:
	ldr	r3, [r6, #496]
	cmp	r3, #10
	bhi	.L3613
	ldr	r1, [r6, #-32]
	ldr	r3, [sp, #12]
	ldr	r2, [r1, #24]
	cmp	r2, r3
	beq	.L3572
	cbz	r2, .L3572
	mov	r1, r3
	ldr	r0, .L3621+72
	bl	printk
	ldr	r3, [r6, #496]
	adds	r3, r3, #1
	str	r3, [r6, #496]
	b	.L3613
.L3572:
	movs	r2, #0
	cmp	r4, r7
	str	r2, [r6, #496]
	mov	r0, #1
	itet	eq
	moveq	r3, r2
	movne	r2, #1
	moveq	r2, r3
	str	r3, [r1, #24]
	str	r2, [r1, #20]
	bl	StorageSysDataStore
	adds	r0, r0, #1
	bne	.L3608
	mvn	r4, #1
	b	.L3542
.L3622:
	.align	2
.L3621:
	.word	1263358532
	.word	.LANCHOR4
	.word	-1067903959
	.word	.LC167
	.word	.LC168
	.word	1112753220
	.word	1146313043
	.word	.LC169
	.word	1112755781
	.word	.LC170
	.word	1094995539
	.word	.LANCHOR4-16
	.word	1074031666
	.word	.LC171
	.word	1074031676
	.word	.LC172
	.word	.LC173
	.word	1280262987
	.word	.LC174
.L3537:
	ldr	r0, .L3623
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3561
	ldr	r2, [sp, #8]
	ldr	r3, .L3623+4
	cmp	r2, r3
	bne	.L3558
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L3558
	ldr	r3, .L3623+8
	add	r0, sp, #16
	cmp	r4, r3
	ldr	r3, .L3623+12
	ite	eq
	ldreq	r1, [r3, #500]
	ldrne	r1, [r3, #504]
	adds	r1, r1, #8
.L3605:
	bl	memcpy
	b	.L3604
.L3534:
	ldr	r0, .L3623+16
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3561
	ldr	r2, [sp, #8]
	ldr	r3, .L3623+4
	cmp	r2, r3
	bne	.L3558
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L3558
	cmp	r4, r6
	add	r2, r2, #8
	ldr	r4, .L3623+12
	bne	.L3576
	add	r1, sp, #8
	ldr	r0, [r4, #500]
	bl	memcpy
	movs	r0, #2
	ldr	r1, [r4, #500]
	b	.L3609
.L3576:
	add	r1, sp, #8
	ldr	r0, [r4, #504]
	bl	memcpy
	ldr	r1, [r4, #504]
	movs	r0, #3
.L3609:
	bl	StorageSysDataStore
	mov	r4, r0
.L3542:
	ldr	r0, .L3623+20
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L3515
.L3577:
	mvn	r0, #21
	b	.L3515
.L3578:
	mvn	r0, #1
	b	.L3515
.L3579:
	mvn	r0, #2
.L3515:
	add	sp, sp, #532
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L3624:
	.align	2
.L3623:
	.word	.LC175
	.word	1145980246
	.word	1074034192
	.word	.LANCHOR4
	.word	.LC176
	.word	.LC177
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
	ldr	r4, .L3634
	movs	r5, #0
	sub	r0, r4, #16
	ldr	r1, [r4, #-52]
	str	r5, [r4, #-36]
	add	r3, r1, #512
	str	r3, [r4, #-32]
	str	r1, [r4, #-20]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #500]
	str	r1, [r4, #504]
	bl	ftl_memcpy
	ldr	r6, [r4, #-20]
	str	r5, [r4, #-28]
	str	r5, [r4, #496]
	ldr	r3, [r6, #16]
	ldr	r7, [r6, #508]
	str	r3, [r4, #-24]
	cbz	r7, .L3626
	mov	r0, r6
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L3626
	str	r5, [r6, #16]
	ldr	r0, .L3634+4
	str	r5, [r4, #-24]
	bl	printk
.L3626:
	ldr	r3, [r4, #-24]
	cbz	r3, .L3627
	ldr	r2, .L3634+8
	ldr	r3, .L3634
	str	r2, [r3, #-28]
.L3627:
	ldr	r1, [r4, #500]
	movs	r0, #2
	bl	StorageSysDataLoad
	ldr	r1, [r4, #504]
	movs	r0, #3
	bl	StorageSysDataLoad
	pop	{r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L3635:
	.align	2
.L3634:
	.word	.LANCHOR4
	.word	.LC178
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
	ldr	r1, .L3647
	movs	r2, #4
	bl	kmalloc_order
	ldr	r5, .L3647+4
	str	r0, [r5, #508]
	cmp	r0, #0
	beq	.L3643
	movs	r7, #0
	ldr	r9, .L3647+12
	mov	r10, r5
	mov	r4, r7
	mov	r6, r7
.L3641:
	ldr	r8, [r5, #508]
	bl	rknand_device_lock
	adds	r0, r6, #2
	movs	r1, #128
	mov	r2, r8
	lsls	r0, r0, #7
	bl	FtlVendorPartRead
	mov	r8, r0
	bl	rknand_device_unlock
	cmp	r8, #0
	bne	.L3639
	ldr	r3, [r10, #508]
	ldr	r0, .L3647+8
	add	r2, r3, #65280
	adds	r2, r2, #252
	ldr	r1, [r3]
	ldr	r3, [r3, #4]
	ldr	r2, [r2]
	bl	printk
	ldr	r8, [r10, #508]
	ldr	r3, [r8]
	cmp	r3, r9
	bne	.L3640
	add	r3, r8, #65280
	ldr	r2, [r8, #4]
	adds	r3, r3, #252
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L3640
	cmp	r4, r3
	itt	cc
	movcc	r7, r6
	movcc	r4, r3
.L3640:
	cbnz	r6, .L3646
	movs	r6, #1
	b	.L3641
.L3646:
	cbz	r4, .L3642
	bl	rknand_device_lock
	lsls	r0, r7, #7
	add	r0, r0, #256
	movs	r1, #128
	mov	r2, r8
	bl	FtlVendorPartRead
	mov	r4, r0
	bl	rknand_device_unlock
	cbz	r4, .L3645
	b	.L3639
.L3642:
	mov	r0, r8
	mov	r1, #65536
	bl	__memzero
	ldr	r3, [r5, #508]
	ldr	r2, .L3647+12
	str	r6, [r3, #4]
	str	r2, [r3]
	movw	r2, #65532
	str	r6, [r3, r2]
	movw	r2, #64504
	strh	r4, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L3645:
	mov	r0, r4
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3639:
	ldr	r0, [r5, #508]
	bl	kfree
	movs	r3, #0
	mov	r0, #-1
	str	r3, [r5, #508]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3643:
	mvn	r0, #11
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3648:
	.align	2
.L3647:
	.word	37748929
	.word	.LANCHOR4
	.word	.LC179
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
	ldr	r3, .L3655
	ldr	r5, [r3, #508]
	cbz	r5, .L3654
	ldrh	r7, [r5, #10]
	movs	r3, #0
.L3651:
	cmp	r3, r7
	bcs	.L3654
	add	r6, r5, r3, lsl #3
	ldrh	r4, [r6, #16]
	cmp	r4, r0
	bne	.L3652
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
.L3652:
	adds	r3, r3, #1
	b	.L3651
.L3654:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L3656:
	.align	2
.L3655:
	.word	.LANCHOR4
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
	ldr	r2, .L3679
	.pad #28
	sub	sp, sp, #28
	mov	ip, r0
	mov	r3, r1
	ldr	r4, [r2, #508]
	cmp	r4, #0
	beq	.L3672
	add	r6, r9, #63
	ldrh	r1, [r4, #8]
	ldrh	r2, [r4, #10]
	bic	r6, r6, #63
	movs	r7, #0
	str	r1, [sp, #4]
.L3659:
	cmp	r7, r2
	bcs	.L3677
	add	r5, r4, r7, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L3660
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L3661
	ldrh	r1, [r4, #14]
	cmp	r1, r6
	bcc	.L3672
	ldrh	r8, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #12]
.L3662:
	ldr	r2, [sp, #12]
	adds	r5, r5, #8
	cmp	r7, r2
	bcs	.L3678
	ldrh	r2, [r5, #16]
	add	r0, fp, r8
	ldrh	r1, [r5, #18]
	adds	r7, r7, #1
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
	b	.L3662
.L3678:
	add	r7, r4, r7, lsl #3
	uxth	r5, r8
	add	r0, fp, r5
	mov	r1, r3
	strh	r5, [r7, #18]	@ movhi
	mov	r2, r9
	strh	ip, [r7, #16]	@ movhi
	uxth	r6, r6
	strh	r9, [r7, #20]	@ movhi
	add	r5, r5, r6
	bl	memcpy
	ldrh	r2, [r4, #14]
	strh	r5, [r4, #12]	@ movhi
	ldr	r3, [sp, #8]
	add	r3, r3, r2
	subs	r6, r3, r6
	strh	r6, [r4, #14]	@ movhi
	b	.L3676
.L3661:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, r0, fp
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L3676
.L3660:
	adds	r7, r7, #1
	b	.L3659
.L3677:
	ldrh	r1, [r4, #14]
	cmp	r1, r6
	bcc	.L3672
	add	r2, r4, r2, lsl #3
	uxth	r6, r6
	subs	r1, r1, r6
	strh	ip, [r2, #16]	@ movhi
	ldrh	r0, [r4, #12]
	strh	r9, [r2, #20]	@ movhi
	strh	r0, [r2, #18]	@ movhi
	add	r0, r0, r6
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
.L3676:
	ldr	r3, [r4, #4]
	movw	r2, #65532
	adds	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r4, r2]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	it	hi
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	bl	rknand_device_lock
	movs	r2, #128
	movs	r0, #16
	ldr	r3, [sp, #4]
	lsls	r1, r3, #7
	mov	r3, r4
	bl	FtlWrite
	bl	rknand_device_unlock
	movs	r0, #0
	b	.L3658
.L3672:
	mov	r0, #-1
.L3658:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3680:
	.align	2
.L3679:
	.word	.LANCHOR4
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
	ldr	r3, .L3697
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r1
	ldr	r0, [r3, #48]
	mov	r5, r2
	ldr	r1, .L3697+4
	bl	kmem_cache_alloc
	mov	r4, r0
	cmp	r0, #0
	beq	.L3690
	ldr	r3, .L3697+8
	cmp	r6, r3
	beq	.L3684
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L3685
	b	.L3696
.L3684:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3696
	ldr	r2, [r4]
	ldr	r3, .L3697+12
	cmp	r2, r3
	beq	.L3687
.L3688:
	mov	r5, #-1
	b	.L3683
.L3687:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	adds	r3, r0, #1
	beq	.L3688
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
	b	.L3683
.L3685:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L3696
	ldr	r2, [r4]
	ldr	r3, .L3697+12
	cmp	r2, r3
	bne	.L3688
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L3688
	mov	r0, r4
	mov	r1, r5
	adds	r2, r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L3696
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L3683
.L3696:
	mvn	r5, #13
.L3683:
	mov	r0, r4
	bl	kfree
	b	.L3682
.L3690:
	mov	r5, #-1
.L3682:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L3698:
	.align	2
.L3697:
	.word	kmalloc_caches
	.word	37748928
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
	.global	NandFlashParaTbl
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
	.global	c_ftl_nand_blks_per_die_shift
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
	.type	__func__.21081, %object
	.size	__func__.21081, 11
__func__.21081:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.space	1
	.type	__func__.22011, %object
	.size	__func__.22011, 21
__func__.22011:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.21152, %object
	.size	__func__.21152, 8
__func__.21152:
	.ascii	"FtlInit\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC1:
	.ascii	"FlashEraseBlocks pageAddr error %x\012\000"
.LC2:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC3:
	.ascii	"remove_from_free_sys_Queue %x\012\000"
.LC4:
	.ascii	"FLASH INFO:\012\000"
.LC5:
	.ascii	"FLASH ID: %x\012\000"
.LC6:
	.ascii	"Device Capacity: %d MB\012\000"
.LC7:
	.ascii	"FMWAIT: %x %x %x %x\012\000"
.LC8:
	.ascii	"FTL INFO:\012\000"
.LC9:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC10:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC11:
	.ascii	"read_page_count = 0x%x\012\000"
.LC12:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC13:
	.ascii	"write_page_count = 0x%x\012\000"
.LC14:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC15:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC16:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC17:
	.ascii	"totle_write = %d MB\012\000"
.LC18:
	.ascii	"totle_read = %d MB\012\000"
.LC19:
	.ascii	"GSV = 0x%x\012\000"
.LC20:
	.ascii	"GDV = 0x%x\012\000"
.LC21:
	.ascii	"bad blk num = %d %d\012\000"
.LC22:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC23:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC24:
	.ascii	"slc_EC = 0x%x\012\000"
.LC25:
	.ascii	"avg_EC = 0x%x\012\000"
.LC26:
	.ascii	"sys_EC = 0x%x\012\000"
.LC27:
	.ascii	"max_EC = 0x%x\012\000"
.LC28:
	.ascii	"min_EC = 0x%x\012\000"
.LC29:
	.ascii	"PLT = 0x%x\012\000"
.LC30:
	.ascii	"POT = 0x%x\012\000"
.LC31:
	.ascii	"MaxSector = 0x%x\012\000"
.LC32:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC33:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC34:
	.ascii	"free sysblock = 0x%x\012\000"
.LC35:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC36:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC37:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC38:
	.ascii	"Sys.id = 0x%x\012\000"
.LC39:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC40:
	.ascii	"ACT.page = 0x%x\012\000"
.LC41:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC42:
	.ascii	"ACT.id = 0x%x\012\000"
.LC43:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC44:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC45:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC46:
	.ascii	"BUF.page = 0x%x\012\000"
.LC47:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC48:
	.ascii	"BUF.id = 0x%x\012\000"
.LC49:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC50:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC51:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC52:
	.ascii	"TMP.page = 0x%x\012\000"
.LC53:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC54:
	.ascii	"TMP.id = 0x%x\012\000"
.LC55:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC56:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC57:
	.ascii	"GC.page = 0x%x\012\000"
.LC58:
	.ascii	"GC.plane = 0x%x\012\000"
.LC59:
	.ascii	"GC.id = 0x%x\012\000"
.LC60:
	.ascii	"GC.mode = 0x%x\012\000"
.LC61:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC62:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC63:
	.ascii	"Read Err Cnt = 0x%x\012\000"
.LC64:
	.ascii	"Prog Err Cnt = 0x%x\012\000"
.LC65:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC66:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC67:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC68:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC69:
	.ascii	"free min EC= 0x%x\012\000"
.LC70:
	.ascii	"free max EC= 0x%x\012\000"
.LC71:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC72:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC73:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC74:
	.ascii	"%s\012\000"
.LC75:
	.ascii	"FTL version: 5.0.47 20171213\000"
.LC76:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC77:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC78:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC79:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC80:
	.ascii	"%s 0x%x:\000"
.LC81:
	.ascii	"%x \000"
.LC82:
	.ascii	"\000"
.LC83:
	.ascii	"otp error! %d\000"
.LC84:
	.ascii	"rr\000"
.LC85:
	.ascii	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\012"
	.ascii	"\000"
.LC86:
	.ascii	"nandc:\000"
.LC87:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC88:
	.ascii	"sdr read ok %x ecc=%d\012\000"
.LC89:
	.ascii	"sync para %d\012\000"
.LC90:
	.ascii	"TOG mode Read error %x %x\012\000"
.LC91:
	.ascii	"read retry status %x %x %x\012\000"
.LC92:
	.ascii	"micron RR %d row=%x,count %d,status=%d\012\000"
.LC93:
	.ascii	"ECC:%d\012\000"
.LC94:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC95:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC96:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC97:
	.ascii	"data:\000"
.LC98:
	.ascii	"spare:\000"
.LC99:
	.ascii	"ReadRetry pageadd=%x ecc=%x err=%x\012\000"
.LC100:
	.ascii	"FLFB:%d %d\012\000"
.LC101:
	.ascii	"prog error: = %x\012\000"
.LC102:
	.ascii	"prog read error: = %x\012\000"
.LC103:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC104:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC105:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC106:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC107:
	.ascii	"bad block:%d %d\012\000"
.LC108:
	.ascii	"FMFB:%d %d\012\000"
.LC109:
	.ascii	"E:bad block:%d\012\000"
.LC110:
	.ascii	"FMFB:Save %d %d\012\000"
.LC111:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC112:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC113:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC114:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC115:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC116:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC117:
	.ascii	"ftl_map_blk_gc blk info: %x %x %x\012\000"
.LC118:
	.ascii	"page map lost: %x %x\012\000"
.LC119:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC120:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC121:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC122:
	.ascii	"page map lost %x %x %x %x\012\000"
.LC123:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC124:
	.ascii	"no ect\000"
.LC125:
	.ascii	"slc mode\000"
.LC126:
	.ascii	"RSB refresh addr %x\012\000"
.LC127:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC128:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC129:
	.ascii	"...%s enter...\012\000"
.LC130:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC131:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC132:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC133:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC134:
	.ascii	"GC des block %x done\012\000"
.LC135:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC136:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC137:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC138:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC139:
	.ascii	"FtlInit %x\012\000"
.LC140:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC141:
	.ascii	"BBT:\000"
.LC142:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC143:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC144:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC145:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC146:
	.ascii	"idblk:\000"
.LC147:
	.ascii	"idb reverse %x %x\012\000"
.LC148:
	.ascii	"write_idblock totle_sec %x %x\012\000"
.LC149:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC150:
	.ascii	"write\000"
.LC151:
	.ascii	"read\000"
.LC152:
	.ascii	"write_idblock error\012\000"
.LC153:
	.ascii	"READ_SECTOR_IO\012\000"
.LC154:
	.ascii	"rk_copy_from_user error\012\000"
.LC155:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC156:
	.ascii	"rk_copy_to_user error\012\000"
.LC157:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC158:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC159:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC160:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC161:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC162:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC163:
	.ascii	"bbt:\000"
.LC164:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC165:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC166:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC167:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC168:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC169:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC170:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC171:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC172:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC173:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC174:
	.ascii	"LockKey not match %d\012\000"
.LC175:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC176:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC177:
	.ascii	"return ret = %lx\012\000"
.LC178:
	.ascii	"secureBootEn check error\012\000"
.LC179:
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
	.type	gFtlInitStatus, %object
	.size	gFtlInitStatus, 4
gFtlInitStatus:
	.word	-1
	.type	NandFlashParaTbl, %object
	.size	NandFlashParaTbl, 2304
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
	.short	1503
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
	.short	1497
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
	.short	1497
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
	.type	gFlashSlcMode, %object
	.size	gFlashSlcMode, 1
gFlashSlcMode:
	.space	1
	.space	3
	.type	slcPageToMlcPageTbl, %object
	.size	slcPageToMlcPageTbl, 512
slcPageToMlcPageTbl:
	.space	512
	.type	mlcPageToSlcPageTbl, %object
	.size	mlcPageToSlcPageTbl, 1024
mlcPageToSlcPageTbl:
	.space	1024
	.type	gNandMaxDie, %object
	.size	gNandMaxDie, 1
gNandMaxDie:
	.space	1
	.type	gNandMaxChip, %object
	.size	gNandMaxChip, 1
gNandMaxChip:
	.space	1
	.space	2
	.type	DieCsIndex, %object
	.size	DieCsIndex, 8
DieCsIndex:
	.space	8
	.type	DieAddrs, %object
	.size	DieAddrs, 32
DieAddrs:
	.space	32
	.type	gDieOp, %object
	.size	gDieOp, 128
gDieOp:
	.space	128
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 4
gpNandParaInfo:
	.space	4
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
	.space	32
	.type	gTotleBlock, %object
	.size	gTotleBlock, 2
gTotleBlock:
	.space	2
	.space	2
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
	.type	gNandFlashEccBits, %object
	.size	gNandFlashEccBits, 1
gNandFlashEccBits:
	.space	1
	.space	3
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
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.space	1
	.type	gFlashToggleModeEn, %object
	.size	gFlashToggleModeEn, 1
gFlashToggleModeEn:
	.space	1
	.space	2
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
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
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
	.type	c_ftl_nand_blks_per_die_shift, %object
	.size	c_ftl_nand_blks_per_die_shift, 2
c_ftl_nand_blks_per_die_shift:
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
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
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
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 4
p_sys_data_buf:
	.space	4
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
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
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
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.space	48
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.space	2
	.space	2
	.type	gSysInfo, %object
	.size	gSysInfo, 12
gSysInfo:
	.space	12
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
	.type	gMultiPageReadEn, %object
	.size	gMultiPageReadEn, 1
gMultiPageReadEn:
	.space	1
	.space	3
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
	.type	g_power_lost_ecc_error_blk, %object
	.size	g_power_lost_ecc_error_blk, 2
g_power_lost_ecc_error_blk:
	.space	2
	.type	g_power_lost_recovery_flag, %object
	.size	g_power_lost_recovery_flag, 2
g_power_lost_recovery_flag:
	.space	2
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
