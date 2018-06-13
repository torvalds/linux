/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-05-28
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
#APP
	.macro	it, cond
	.endm
	.macro	itt, cond
	.endm
	.macro	ite, cond
	.endm
	.macro	ittt, cond
	.endm
	.macro	itte, cond
	.endm
	.macro	itet, cond
	.endm
	.macro	itee, cond
	.endm
	.macro	itttt, cond
	.endm
	.macro	ittte, cond
	.endm
	.macro	ittet, cond
	.endm
	.macro	ittee, cond
	.endm
	.macro	itett, cond
	.endm
	.macro	itete, cond
	.endm
	.macro	iteet, cond
	.endm
	.macro	iteee, cond
	.endm

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
	.global	ftl_flash_suspend
	.thumb
	.thumb_func
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L137
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
	ldr	r3, [sp, #4]
	cmp	r10, #1
	ubfx	r1, r4, #0, #10
	uxth	r0, r0
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
	movs	r1, #11
	mov	r4, r0
	bl	__memzero
	ldr	r3, .L150
	movs	r5, #1
	ldr	r2, [r3, #1788]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	ldr	r2, [r3, #4]
	addw	r3, r3, #1620
	smulbb	r2, r1, r2
	strh	r2, [r4, #4]	@ unaligned
	ldrb	r2, [r3, #272]	@ zero_extendqisi2
	strb	r2, [r4, #7]
	ldr	r2, [r3, #276]
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
	.size	ftl_read_flash_info, .-ftl_read_flash_info
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
	ldr	r2, [r3, #1900]
	str	r0, [r3, #1900]
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
	@ link register save eliminated.
	ldr	r3, .L180
	ands	r1, r0, #6
	ldr	r2, [r3, #1828]
	ldr	r3, [r2]
	beq	.L174
	lsls	r1, r0, #29
	orr	r3, r3, #24576
	movw	r1, #16641
	str	r1, [r2, #344]
	ldr	r1, .L180+4
	bfc	r3, #15, #1
	orr	r3, r3, #196608
	it	mi
	orrmi	r3, r3, #32768
	str	r1, [r2, #304]
	movs	r1, #38
	str	r1, [r2, #308]
	movs	r1, #39
	str	r1, [r2, #308]
	b	.L176
.L174:
	bfi	r3, r1, #13, #1
.L176:
	str	r3, [r2]
	movs	r0, #0
	bx	lr
.L181:
	.align	2
.L180:
	.word	.LANCHOR0
	.word	1710595
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
	lsls	r4, r4, #8
	str	r2, [r3, #2056]
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
	ldrb	r3, [r7, #1905]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L202
	movs	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	movs	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldrb	r3, [r7, #1904]	@ zero_extendqisi2
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
	ldrb	r3, [r8, #1905]	@ zero_extendqisi2
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
	addw	r4, r6, #1906
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
	ldr	r3, [sp, #4]
	str	r2, [r8, #2048]
	b	.L215
.L217:
	add	fp, fp, r10
	movs	r3, #22
	mov	r0, r5
	add	r5, r5, r6
	str	r3, [fp, #2056]
	bl	NandcFlashDeCs
	strb	r7, [r5, #2760]
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
	ldrb	r1, [r5, #1907]	@ zero_extendqisi2
	adds	r2, r2, #118
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
	lsls	r4, r4, #8
	movs	r0, #80
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
	ldrb	r3, [r5, #2764]	@ zero_extendqisi2
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
	ldrb	r3, [r1, #1905]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L309
	ldrb	r3, [r1, #1904]	@ zero_extendqisi2
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
	ldr	r3, [sp, #12]
	ldrb	r2, [r8, #1616]	@ zero_extendqisi2
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
	ldr	r1, [sp, #8]
	mov	r0, r4
	bl	FlashEraseCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	ldr	r1, [sp, #8]
	mov	r0, r4
	bl	FlashReadStatus
	ldr	r3, [sp, #8]
	movs	r2, #0
	sbfx	r0, r0, #0, #1
	str	r0, [r6, r5]
	ldr	r1, [r8, #4]
	mov	r0, r4
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
	ldr	r4, .L340
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r3, .L340+4
	ldr	r2, [r4, #2768]
	cmp	r2, r3
	bne	.L332
	movs	r0, #0
	bl	flash_enter_slc_mode
.L332:
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	ldr	r5, .L340
	cbz	r3, .L333
	ldrb	r3, [r5, #2764]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L333
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	movs	r3, #0
	strb	r3, [r5, #2772]
.L333:
	ldr	r3, [r4, #12]
	movs	r0, #0
	str	r0, [r3, #336]
	pop	{r3, r4, r5, pc}
.L341:
	.align	2
.L340:
	.word	.LANCHOR0
	.word	1446522928
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
	ldr	r3, .L343
	add	r0, r3, r0, lsl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L344:
	.align	2
.L343:
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
	ldr	r1, .L355
	bl	__aeabi_idiv
	ldr	r3, .L355+4
	ldr	r3, [r3, #1828]
	cmp	r0, #250
	ble	.L346
	movw	r2, #8354
	b	.L353
.L346:
	cmp	r0, #220
	bgt	.L354
	cmp	r0, #185
	ble	.L349
	movw	r2, #4226
	b	.L353
.L349:
	cmp	r0, #160
	ble	.L350
	movw	r2, #4194
	b	.L353
.L350:
	cmp	r4, #35
	bhi	.L351
	movw	r2, #4193
	b	.L353
.L351:
	cmp	r4, #99
	bls	.L352
.L354:
	movw	r2, #8322
	b	.L353
.L352:
	movw	r2, #4225
.L353:
	str	r2, [r3, #4]
	pop	{r4, pc}
.L356:
	.align	2
.L355:
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
	bls	.L358
	sub	r3, r0, #4224
	subs	r3, r3, #1
	cmp	r3, #1
	bls	.L358
	movw	r3, #8322
	cmp	r0, r3
	bne	.L359
.L358:
	ldr	r3, .L360
	ldr	r3, [r3, #1828]
	str	r0, [r3, #4]
.L359:
	ldr	r3, .L360+4
	ldrb	r0, [r3, #277]	@ zero_extendqisi2
	b	NandcTimeCfg
.L361:
	.align	2
.L360:
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
	ldr	r3, .L364
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
	str	r0, [r3, #1828]
	str	r2, [r3, #40]
	ldr	r2, [r0]
	and	r2, r2, #253952
	ubfx	r4, r2, #13, #1
	bfi	r2, r1, #13, #1
	ldr	r1, [r0, #352]
	orr	r2, r2, #256
	str	r4, [r3, #2776]
	movw	r4, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r3, #2780]
	ldr	r1, [r0, #352]
	cmp	r1, r4
	mov	r4, r3
	str	r1, [r3, #2768]
	itt	eq
	moveq	r3, #8
	streq	r3, [r4, #2780]
	str	r2, [r0]
	movs	r0, #40
	ldr	r3, [r4, #1828]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #1828]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L364+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #2812]
	str	r5, [r4, #2820]
	str	r0, [r4, #2784]
	str	r0, [r4, #2788]
	add	r0, r0, #32768
	str	r0, [r4, #2792]
	pop	{r3, r4, r5, pc}
.L365:
	.align	2
.L364:
	.word	.LANCHOR0
	.word	1710593
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
	ldr	r4, .L367
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
.L368:
	.align	2
.L367:
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
	ldr	r3, .L377
	movs	r1, #1
	ldr	r2, [r3, #1828]
	str	r0, [r3, #2824]
	movs	r3, #0
	str	r1, [r2, #8]
	movs	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L370
.L373:
	bfc	r3, #4, #1
	b	.L371
.L370:
	cmp	r0, #24
	bne	.L372
	orr	r3, r3, #16
	b	.L371
.L372:
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L373
.L371:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L378:
	.align	2
.L377:
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
	ldr	r3, .L380
	strb	r0, [r3, #1892]
	b	NandcBchSel
.L381:
	.align	2
.L380:
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
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r5, #0
	ldr	r3, .L390
	add	r7, r3, #1792
	ldr	r2, [r3, #1828]
	mov	r4, r3
	ldr	r1, [r3, #1832]
	ldr	r6, [r3, #1848]
	str	r1, [r2]
	ldr	r1, [r3, #1836]
	ldr	r2, [r3, #1828]
	str	r1, [r2, #4]
	ldr	r1, [r3, #1840]
	str	r1, [r2, #8]
	ldr	r1, [r3, #1844]
	str	r1, [r2, #12]
	ldr	r1, [r3, #1852]
	str	r6, [r2, #304]
	str	r1, [r2, #308]
	ldr	r1, [r3, #1856]
	str	r1, [r2, #336]
	ldr	r1, [r3, #1860]
	str	r1, [r2, #344]
.L384:
	ldrb	r3, [r7, r5, lsl #3]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L383
	uxtb	r0, r5
	bl	FlashReset
.L383:
	adds	r5, r5, #1
	cmp	r5, #4
	bne	.L384
	ldrb	r3, [r4, #2772]	@ zero_extendqisi2
	ldr	r5, .L390
	cbz	r3, .L385
	movs	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #2764]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2764]	@ zero_extendqisi2
	bl	NandcSetMode
	ubfx	r0, r6, #8, #8
	bl	NandcSetDdrPara
.L385:
	ldr	r3, [r4, #1788]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	pop	{r3, r4, r5, r6, r7, lr}
	b	FlashBchSel
.L391:
	.align	2
.L390:
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
	bmi	.L395
	mov	r0, r4
	pop	{r4, lr}
	b	wait_for_nand_flash_ready
.L395:
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
	ldr	r4, .L430
	.pad #28
	sub	sp, sp, #28
	mov	r10, r1
	mov	r8, r2
	ldrb	r5, [r4]	@ zero_extendqisi2
	cbz	r5, .L414
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L398
.L414:
	mov	fp, r4
.L397:
	cmp	r5, r8
	bcs	.L428
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
	ldrb	r3, [fp, #2828]	@ zero_extendqisi2
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
	ldr	r1, [sp, #16]
	mov	r2, r7
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
.L400:
	adds	r5, r5, #1
	b	.L397
.L428:
	ldr	r0, [r4, #1828]
	movs	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r6, .L430
	addw	r7, r6, #1660
.L407:
	ldrb	r2, [r4, #1616]	@ zero_extendqisi2
	ldr	r3, .L430
	cmp	r5, r2
	bcs	.L429
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
.L429:
	ldr	r2, [r3, #2832]
	cbnz	r2, .L410
.L412:
	movs	r0, #0
	b	.L398
.L410:
	ldrb	r3, [r3, #1792]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L412
	movs	r3, #0
	movs	r2, #36
	mov	r1, r3
.L411:
	cmp	r3, r8
	beq	.L412
	mul	r0, r2, r3
	adds	r3, r3, #1
	str	r1, [r9, r0]
	b	.L411
.L398:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L431:
	.align	2
.L430:
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
	ldr	r3, .L434
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
.L435:
	.align	2
.L434:
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
.L437:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #11
	bpl	.L437
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
	cbnz	r2, .L454
	adds	r2, fp, #0
	it	ne
	movne	r2, #1
	b	.L441
.L454:
	movs	r2, #1
.L441:
	ldr	r5, .L459
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
	ldr	r3, [r5, #2780]
	cmp	r3, #3
	bls	.L442
	ldr	r3, [r6, #16]
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bic	r3, r3, #4
	str	r3, [sp, #12]
	cmp	r2, #0
	beq	.L443
	cbnz	r7, .L444
.L452:
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
	beq	.L445
	mov	r0, fp
	b	.L446
.L444:
	ldr	r3, [r5, #2824]
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
.L448:
	ldr	r2, [sp, #4]
	cmp	r1, r2
	bcs	.L452
	ldr	r2, [sp, #60]
	lsr	r10, r3, #2
	cbz	r2, .L449
	ldrh	r2, [r0, #2]
	ldrh	lr, [r0], #4
	orr	lr, lr, r2, lsl #16
	ldr	r2, [ip, #2792]
	str	lr, [r2, r10, lsl #2]
	b	.L450
.L449:
	ldr	r2, [ip, #2792]
	mov	lr, r2
	mov	r2, #-1
	str	r2, [lr, r10, lsl #2]
.L450:
	ldr	r2, [sp]
	adds	r1, r1, #1
	add	r3, r3, r2
	b	.L448
.L445:
	ldr	r0, [r5, #2788]
.L446:
	ldr	r3, [r5, #2792]
	ubfx	r8, r4, #22, #5
	mov	r2, r7
	str	r0, [r5, #2796]
	lsl	r1, r8, #10
	str	r3, [r5, #2800]
	bl	rknand_dma_map_single
	mov	r2, r7
	lsl	r1, r8, #7
	clz	r7, r7
	lsrs	r7, r7, #5
	str	r0, [r5, #2804]
	ldr	r0, [r5, #2800]
	bl	rknand_dma_map_single
	movs	r3, #1
	str	r3, [r5, #2812]
	tst	fp, #3
	ldr	r3, [r5, #2804]
	str	r0, [r5, #2808]
	str	r3, [r6, #20]
	ldr	r3, [r5, #2808]
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
.L443:
	ldr	r3, [sp, #12]
	str	r3, [r6, #16]
.L442:
	str	r9, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L460:
	.align	2
.L459:
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
.L462:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bhi	.L464
	lsls	r2, r2, #1
	b	.L462
.L464:
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
	ldr	r3, .L468
	cmp	r0, #23
	it	ls
	movls	r0, #24
	ldrh	r2, [r3, #2840]
	ldrh	r1, [r3, #2850]
	str	r0, [r3, #2836]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #2856]
	strh	r0, [r3, #2848]	@ movhi
	movs	r0, #0
	str	r2, [r3, #2844]
	subs	r2, r1, r2
	str	r2, [r3, #2852]
	bx	lr
.L469:
	.align	2
.L468:
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
	ldr	r4, .L498
	ldrh	lr, [r0, #8]
	str	r2, [sp]
	addw	r1, r4, #2868
	ldrh	r2, [sp]
	ldrh	r3, [r0, #10]
	ldrh	r0, [r0, #12]
	strh	r2, [r4, #2850]	@ movhi
	movs	r2, #0
	strh	lr, [r4, #2860]	@ movhi
	strh	r3, [r4, #2862]	@ movhi
	strh	r0, [r4, #2864]	@ movhi
.L471:
	strb	r2, [r2, r1]
	adds	r2, r2, #1
	cmp	r2, #32
	bne	.L471
	ldrh	r1, [r5, #14]
	ldrh	r2, [r5, #20]
	cmp	r2, r1, lsr #8
	bcs	.L472
	uxtb	r10, r0
	ldr	r9, .L498+4
	lsl	r2, r10, #1
	uxtb	r2, r2
	str	r2, [sp, #8]
	subs	r2, r3, #1
	muls	r2, r0, r2
	str	r2, [sp, #4]
	movs	r2, #0
.L473:
	cmp	r2, r0
	bcs	.L475
	ldr	r6, [sp, #4]
	rsb	ip, r0, r2
	uxtb	r1, r2
	add	ip, ip, r9
	adds	r6, r2, r6
	add	r6, r9, r6
	str	r6, [sp, #12]
	movs	r6, #0
	mov	r8, r6
.L476:
	cmp	r8, r3
	add	r6, r6, r0
	bcs	.L497
	ldr	r7, [sp, #12]
	add	fp, r1, r10
	strb	r1, [ip, r6]
	add	r8, r8, #1
	strb	fp, [r7, r6]
	ldr	r7, [sp, #8]
	add	r1, r1, r7
	uxtb	r1, r1
	b	.L476
.L497:
	adds	r2, r2, #1
	b	.L473
.L475:
	lsls	r3, r3, #1
	strh	r3, [r4, #2862]	@ movhi
	ldr	r3, [sp]
	lsrs	r7, r3, #1
	strh	r7, [r4, #2850]	@ movhi
.L472:
	cmp	lr, #1
	mov	r3, #5
	strh	r3, [r4, #2900]	@ movhi
	mov	r3, #0
	strh	r3, [r4, #2902]	@ movhi
	it	eq
	ldreq	r3, .L498
	ldrb	r8, [r4]	@ zero_extendqisi2
	it	eq
	strheq	lr, [r3, #2900]	@ movhi
	mov	r3, #4352
	strh	r3, [r4, #2904]	@ movhi
	cmp	r8, #0
	beq	.L478
	ldr	r3, .L498
	mov	r2, #384
	strh	r2, [r3, #2904]	@ movhi
.L478:
	ldrh	r6, [r4, #2850]
	ldrh	r7, [r4, #2862]
	smulbb	r7, r7, r0
	smulbb	r0, r6, r0
	uxth	r7, r7
	strh	r7, [r4, #2840]	@ movhi
	uxth	r0, r0
	strh	r0, [r4, #2906]	@ movhi
	bl	Ftl_log2
	ldrh	r9, [r5, #16]
	ldrh	fp, [r5, #20]
	ldrh	r1, [r5, #18]
	smulbb	r3, r7, r9
	strh	r9, [r4, #2910]	@ movhi
	strh	r1, [r4, #2912]	@ movhi
	str	r1, [sp]
	strh	fp, [r4, #2916]	@ movhi
	strh	r3, [r4, #2914]	@ movhi
	strh	r0, [r4, #2908]	@ movhi
	mov	r0, fp
	bl	Ftl_log2
	lsl	r3, fp, #9
	cmp	r6, #1024
	ldr	r1, [sp]
	uxth	r3, r3
	strh	r3, [r4, #2920]	@ movhi
	it	hi
	ldrhi	r2, .L498
	lsr	r3, r3, #8
	strh	r3, [r4, #2922]	@ movhi
	ldrh	r3, [r5, #26]
	mul	r1, r1, fp
	strh	r3, [r4, #2924]	@ movhi
	mul	r3, r6, r7
	str	r3, [r4, #2856]
	itt	hi
	uxtbhi	r3, r6
	strhhi	r3, [r2, #2902]	@ movhi
	mov	r10, r0
	ldrh	r5, [r4, #2902]
	strh	r0, [r4, #2918]	@ movhi
	subs	r5, r6, r5
	ldrh	r0, [r4, #2904]
	ldr	r3, .L498
	muls	r5, r7, r5
	lsls	r0, r0, #3
	str	r3, [sp]
	mul	r5, fp, r5
	mul	r5, r9, r5
	asrs	r5, r5, #11
	str	r5, [r4, #2928]
	bl	__aeabi_idiv
	ldr	r3, [sp]
	uxth	r0, r0
	cmp	r0, #4
	itee	hi
	strhhi	r0, [r3, #2932]	@ movhi
	movls	r2, #4
	strhls	r2, [r3, #2932]	@ movhi
	cmp	r8, #0
	beq	.L482
	mov	r3, #640
	strh	r3, [r4, #2904]	@ movhi
.L482:
	ldrh	r3, [r4, #2904]
	lsls	r6, r6, #6
	mov	r1, r7
	ldr	r5, .L498
	asr	r3, r3, r10
	add	r10, r10, #9
	asr	r6, r6, r10
	adds	r3, r3, #2
	strh	r3, [r4, #2934]	@ movhi
	uxth	r0, r6
	strh	r6, [r4, #2936]	@ movhi
	add	r6, r0, #8
	mul	r3, r7, r0
	ldrh	r0, [r4, #2932]
	str	r3, [r4, #2940]
	bl	__aeabi_uidiv
	cmp	r7, #1
	uxtah	r0, r6, r0
	ldr	r6, .L498
	it	eq
	addeq	r0, r0, #4
	str	r0, [r5, #2836]
	ldrh	r0, [r4, #2836]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #2836]
	ldr	r5, [r4, #2852]
	mov	r0, #2048
	ldrh	r7, [r4, #2916]
	str	r3, [r4, #2944]
	ldrh	r3, [r4, #2910]
	lsls	r5, r5, #2
	mov	r1, r7
	muls	r5, r3, r5
	ldrh	r3, [r4, #2918]
	adds	r3, r3, #9
	lsrs	r5, r5, r3
	adds	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, #2948]	@ movhi
	bl	__aeabi_idiv
	ldrh	r2, [r4, #2932]
	movs	r3, #0
	str	r3, [r4, #1896]
	adds	r3, r2, #3
	strh	r3, [r4, #2932]	@ movhi
	ldr	r3, [r4, #2940]
	adds	r1, r3, #3
	str	r1, [r4, #2940]
	strh	r0, [r4, #2950]	@ movhi
	ldrb	r0, [r4, #76]	@ zero_extendqisi2
	cbz	r0, .L485
	adds	r2, r2, #4
	adds	r3, r3, #5
	strh	r2, [r6, #2932]	@ movhi
	b	.L496
.L485:
	cmp	r1, #7
	bhi	.L486
	movs	r3, #8
.L496:
	str	r3, [r6, #2940]
.L486:
	ldrh	r2, [r4, #2848]
	movs	r3, #0
	strh	r3, [r4, #2952]	@ movhi
	movs	r0, #0
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	adds	r3, r3, #52
	add	r5, r3, r5, lsl #2
	cmp	r5, r7, lsl #9
	ittt	cc
	movcc	r2, #1
	ldrcc	r3, .L498
	strhcc	r2, [r3, #2952]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L499:
	.align	2
.L498:
	.word	.LANCHOR0
	.word	.LANCHOR0+2868
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
	ldr	r2, .L507
	ldrh	r3, [r2, #2954]
	cbz	r3, .L506
	ldr	r3, [r2, #2956]
	ldrh	r2, [r2, #2932]
	add	r2, r3, r2, lsl #1
.L502:
	cmp	r3, r2
	beq	.L506
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L502
	movs	r0, #1
	bx	lr
.L506:
	movs	r0, #0
	bx	lr
.L508:
	.align	2
.L507:
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
	ldr	r2, .L517
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r2, #2960]
	cbz	r3, .L516
	ldr	r5, [r2, #2964]
	movs	r6, #36
	movs	r2, #0
.L512:
	mla	r4, r6, r2, r5
	ldr	r4, [r4, #16]
	cmp	r4, r0
	bcc	.L511
	cmp	r4, r1
	bls	.L514
.L511:
	adds	r2, r2, #1
	cmp	r2, r3
	bne	.L512
.L516:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L514:
	movs	r0, #1
	pop	{r4, r5, r6, pc}
.L518:
	.align	2
.L517:
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
	ldr	r3, .L520
	ldr	r0, [r3, #1896]
	bx	lr
.L521:
	.align	2
.L520:
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
	ldr	r3, .L523
	ldr	r0, [r3, #1896]
	bx	lr
.L524:
	.align	2
.L523:
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
	ldr	r3, .L526
	ldr	r0, [r3, #2968]
	bx	lr
.L527:
	.align	2
.L526:
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
	ldr	r5, .L529
	ldrh	r4, [r5, #2906]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, lsl #2
	ldr	r4, [r1, #3000]
	movs	r1, #1
	subs	r3, r6, r3
	uxth	r3, r3
	and	r0, r3, #31
	lsrs	r7, r3, #5
	lsls	r1, r1, r0
	ldr	r0, [r4, r7, lsl #2]
	orrs	r1, r1, r0
	ldr	r0, .L529+4
	str	r1, [r4, r7, lsl #2]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	ldrh	r3, [r5, #2978]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r5, #2978]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L530:
	.align	2
.L529:
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
	ldr	r5, .L532
	ldrh	r6, [r5, #2906]
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
	ldr	r3, [r5, #3000]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L533:
	.align	2
.L532:
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
	ldr	r0, .L536
	movw	r3, #65535
	movs	r1, #255
	movs	r2, #16
	addw	r0, r0, #2984
	strh	r3, [r0, #-12]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	memset
.L537:
	.align	2
.L536:
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
	ldr	r4, .L546
	ldrh	r6, [r4, #2906]
	ldrh	r3, [r4, #2862]
	mov	r4, r5
	muls	r6, r3, r6
.L539:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L545
	bl	FtlBbmIsBadBlock
	cbz	r0, .L540
	adds	r4, r4, #1
	uxth	r4, r4
.L540:
	adds	r5, r5, #1
	b	.L539
.L545:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L547:
	.align	2
.L546:
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
	ldr	r6, .L549
	mov	r7, r0
	ldrh	r5, [r6, #2864]
	mov	r1, r5
	bl	__aeabi_uidivmod
	mov	r0, r7
	smlabb	r4, r4, r5, r1
	mov	r1, r5
	bl	__aeabi_uidiv
	ldrh	r3, [r6, #2906]
	smlabb	r0, r3, r0, r4
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.L550:
	.align	2
.L549:
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
	ldr	r3, .L552
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r1, [r3, #2906]
	ldrh	r5, [r3, #2864]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r1, r4
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L553:
	.align	2
.L552:
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
	ldr	r4, .L555
	ldrh	r1, [r4, #2906]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #2864]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L556:
	.align	2
.L555:
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
	bls	.L558
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L558:
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
	ldr	r3, .L561
	mov	r1, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	strh	r0, [r3, #3032]	@ movhi
	add	r0, r3, #3040
	strh	r4, [r3, #3034]	@ movhi
	strh	r4, [r3, #3036]	@ movhi
	strh	r4, [r3, #3038]	@ movhi
	bl	__memzero
	mov	r0, r4
	pop	{r4, pc}
.L562:
	.align	2
.L561:
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
	ldr	r3, .L564
	ldrh	r0, [r3, #3038]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L565:
	.align	2
.L564:
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
	ldr	r3, .L567
	ldrh	r0, [r3, #3038]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L568:
	.align	2
.L567:
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
	ldr	r4, .L578
	ldrh	r3, [r4, #3038]
	cmp	r3, #1024
	beq	.L569
	cbz	r1, .L571
	ldr	r5, .L578+4
	ldr	r3, [r5, #744]
	cbnz	r3, .L571
	bl	P2V_block_in_plane
	movs	r1, #1
	lsls	r3, r6, #10
	mov	r2, r1
	mov	r7, r0
	ldr	r0, [r5, #748]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r5, #752]
	ldrh	r3, [r2, r7, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	ldr	r3, [r5, #756]
	adds	r3, r3, #1
	str	r3, [r5, #756]
.L571:
	ldrh	r3, [r4, #3038]
	adds	r3, r3, #1
	strh	r3, [r4, #3038]	@ movhi
	ldrh	r3, [r4, #3036]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #3036]	@ movhi
	strh	r6, [r2, #3040]	@ movhi
.L569:
	pop	{r3, r4, r5, r6, r7, pc}
.L579:
	.align	2
.L578:
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
	ldr	r3, .L589
	movs	r0, #0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r4, [r3, #788]
	ldr	r3, .L589+4
	and	r4, r4, #31
	ldrh	r1, [r3, #3034]
	ldrh	r2, [r3, #3036]
.L581:
	uxth	r5, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bge	.L588
	add	r5, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r5, #3040]
	add	r5, r3, r2, lsl #1
	strh	r6, [r5, #3040]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L581
.L588:
	cbz	r6, .L580
	strh	r1, [r3, #3034]	@ movhi
	strh	r2, [r3, #3036]	@ movhi
.L580:
	pop	{r4, r5, r6, pc}
.L590:
	.align	2
.L589:
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	ldr	r3, .L598
	mov	r1, r0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r0, [r3, #3038]
	cbz	r0, .L592
	ldrh	r6, [r3, #3034]
	movs	r4, #0
.L593:
	cmp	r4, r0
	bcs	.L597
	adds	r2, r4, r6
	ldr	r5, .L598
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #3040]
	cmp	r2, r1
	bne	.L594
	ldr	r0, .L598+4
	bl	printk
	ldrh	r3, [r5, #3034]
	movs	r0, #1
	add	r4, r4, r3
	add	r2, r5, r3, lsl #1
	ubfx	r4, r4, #0, #10
	adds	r3, r3, #1
	ldrh	r2, [r2, #3040]
	add	r4, r5, r4, lsl #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r5, #3034]	@ movhi
	ldrh	r3, [r5, #3038]
	strh	r2, [r4, #3040]	@ movhi
	subs	r3, r3, #1
	strh	r3, [r5, #3038]	@ movhi
	pop	{r4, r5, r6, pc}
.L594:
	adds	r4, r4, #1
	b	.L593
.L597:
	movs	r0, #0
.L592:
	pop	{r4, r5, r6, pc}
.L599:
	.align	2
.L598:
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
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L610
	ldr	r6, .L610+4
	mov	r7, r4
.L601:
	ldrh	r1, [r4, #3038]
	cbz	r1, .L602
	ldrh	r3, [r4, #3034]
	subs	r1, r1, #1
	ldr	r10, [r6, #744]
	strh	r1, [r4, #3038]	@ movhi
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldr	r8, .L610+4
	ldrh	r5, [r2, #3040]
	strh	r3, [r4, #3034]	@ movhi
	cmp	r10, #0
	bne	.L603
	mov	r0, r5
	bl	P2V_block_in_plane
	lsls	r3, r5, #10
	mov	r9, r0
	ldr	r0, [r6, #748]
	str	r3, [r0, #4]
	ldrb	r3, [r4, #76]	@ zero_extendqisi2
	cbz	r3, .L604
	mov	r1, r10
	movs	r2, #1
	bl	FlashEraseBlocks
.L604:
	movs	r1, #1
	ldr	r0, [r8, #748]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r2, [r8, #752]
	ldrh	r3, [r2, r9, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r9, lsl #1]	@ movhi
	ldr	r3, [r8, #756]
	adds	r3, r3, #1
	str	r3, [r8, #756]
	b	.L603
.L602:
	ldr	r0, .L610+8
	bl	printk
.L605:
	b	.L605
.L603:
	cbnz	r5, .L606
	mov	r1, r5
	ldrh	r2, [r7, #3038]
	ldr	r0, .L610+12
	bl	printk
	b	.L601
.L606:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L611:
	.align	2
.L610:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC4
	.word	.LC5
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
	ldr	r5, .L628
	ldrh	r3, [r5, #2848]
	cmp	r3, r0
	bls	.L614
	movs	r4, #6
	ldr	r2, .L628+4
	movw	r3, #65535
	muls	r4, r0, r4
	ldr	lr, [r2, #808]
	mov	r7, r2
	add	r1, lr, r4
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [lr, r4]	@ movhi
	ldr	r3, [r2, #812]
	cmp	r3, #0
	beq	.L627
	ldr	r8, [r2, #816]
	lsl	fp, r0, #1
	ldrh	r2, [r1, #4]
	ldrh	r6, [r8, r0, lsl #1]
	cbz	r2, .L625
	muls	r6, r2, r6
	b	.L616
.L625:
	mov	r6, #-1
.L616:
	ldr	ip, [r7, #808]
	ldr	r2, .L628+8
	rsb	r9, ip, r3
	ldrh	r5, [r5, #2848]
	asr	r9, r9, #1
	str	r5, [sp]
	mul	r2, r2, r9
	ldr	r9, [r7, #752]
	add	r5, r9, fp
	str	r5, [sp, #4]
	movs	r5, #0
	uxth	r2, r2
.L623:
	adds	r5, r5, #1
	ldr	r7, [sp]
	uxth	r5, r5
	cmp	r5, r7
	bhi	.L614
	cmp	r0, r2
	beq	.L614
	ldrh	r7, [r3, #4]
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	cbz	r7, .L626
	mul	r7, r7, fp
	b	.L618
.L626:
	mov	r7, #-1
.L618:
	cmp	r7, r6
	bne	.L619
	ldr	r7, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L621
	b	.L620
.L619:
	bhi	.L620
.L621:
	ldrh	r7, [r3]
	movw	r10, #65535
	cmp	r7, r10
	bne	.L622
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L628+4
	str	r1, [r3, #820]
	b	.L614
.L622:
	movs	r3, #6
	mov	r2, r7
	mla	r3, r3, r7, ip
	b	.L623
.L620:
	strh	r2, [lr, r4]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L628+4
	ldr	r4, [r2, #812]
	cmp	r3, r4
	bne	.L624
	strh	r0, [r3, #2]	@ movhi
.L627:
	str	r1, [r2, #812]
	b	.L614
.L624:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #808]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L614:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L629:
	.align	2
.L628:
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
	ldr	r2, .L631
	ldrh	r3, [r2, #824]
	adds	r3, r3, #1
	strh	r3, [r2, #824]	@ movhi
	pop	{r3, pc}
.L632:
	.align	2
.L631:
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
	beq	.L634
	movs	r1, #6
	ldr	r2, .L641
	mul	lr, r1, r0
	ldr	ip, [r2, #808]
	mov	r7, r2
	add	r6, ip, lr
	strh	r5, [r6, #2]	@ movhi
	strh	r5, [ip, lr]	@ movhi
	ldr	r3, [r2, #828]
	cbz	r3, .L640
	ldr	r8, [r2, #808]
	ldr	r9, [r2, #752]
	rsb	r4, r8, r3
	ldr	r2, .L641+4
	asrs	r4, r4, #1
	ldrh	r10, [r9, r0, lsl #1]
	muls	r4, r2, r4
	uxth	r2, r4
.L638:
	ldrh	r4, [r9, r2, lsl #1]
	cmp	r4, r10
	bcs	.L636
	ldrh	r4, [r3]
	cmp	r4, r5
	bne	.L637
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L634
.L637:
	mla	r3, r1, r4, r8
	mov	r2, r4
	b	.L638
.L636:
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	strh	r2, [ip, lr]	@ movhi
	ldr	r1, [r7, #828]
	ldr	r2, .L641
	cmp	r3, r1
	bne	.L639
	strh	r0, [r3, #2]	@ movhi
.L640:
	str	r6, [r2, #828]
	b	.L634
.L639:
	ldrh	r4, [r3, #2]
	ldr	r1, [r2, #808]
	movs	r2, #6
	muls	r2, r4, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L634:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L642:
	.align	2
.L641:
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
	ldr	r2, .L644
	ldrh	r3, [r2, #832]
	adds	r3, r3, #1
	strh	r3, [r2, #832]	@ movhi
	pop	{r3, pc}
.L645:
	.align	2
.L644:
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
	ldr	r7, .L651
	movw	r6, #65535
	muls	r1, r4, r1
	ldr	r3, [r0]
	ldr	r2, [r7, #808]
	adds	r5, r2, r1
	cmp	r5, r3
	ldrh	r3, [r2, r1]
	bne	.L647
	cmp	r3, r6
	ittee	eq
	moveq	r3, #0
	streq	r3, [r0]
	mlane	r3, r4, r3, r2
	strne	r3, [r0]
	it	ne
	strhne	r6, [r3, #2]	@ movhi
	b	.L649
.L647:
	cmp	r3, r6
	ldrh	r0, [r5, #2]
	bne	.L650
	cmp	r0, r3
	beq	.L649
	muls	r0, r4, r0
	strh	r3, [r2, r0]	@ movhi
	b	.L649
.L650:
	mla	r3, r4, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r7, #808]
	muls	r4, r6, r4
	strh	r0, [r3, r4]	@ movhi
.L649:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, pc}
.L652:
	.align	2
.L651:
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
	cbz	r3, .L659
	ldr	r2, .L660
	movs	r6, #6
	movw	r5, #65535
	ldr	r4, [r2, #808]
.L655:
	cbnz	r1, .L656
.L658:
	subs	r4, r3, r4
	ldr	r3, .L660+4
	asrs	r4, r4, #1
	muls	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L656:
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L658
	subs	r1, r1, #1
	mla	r3, r6, r2, r4
	uxth	r1, r1
	b	.L655
.L659:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L661:
	.align	2
.L660:
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
	ldr	r2, .L669
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #812]
	cbz	r3, .L667
	movs	r4, #6
	ldr	r1, [r2, #808]
	movw	r2, #65535
.L664:
	cbz	r0, .L665
	ldrh	r3, [r3]
	cmp	r3, r2
	beq	.L667
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L664
.L667:
	movw	r0, #65535
	pop	{r4, pc}
.L665:
	subs	r3, r3, r1
	ldr	r0, .L669+4
	asrs	r3, r3, #1
	muls	r0, r3, r0
	uxth	r0, r0
	pop	{r4, pc}
.L670:
	.align	2
.L669:
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
	ldrh	r3, [r4, #836]
	cmp	r3, r0
	beq	.L672
	ldrh	r3, [r4, #884]
	cmp	r3, r0
	beq	.L672
	ldrh	r3, [r4, #932]
	cmp	r3, r0
	beq	.L672
	movs	r3, #6
	ldr	r1, [r4, #808]
	ldr	r2, [r4, #812]
	muls	r3, r0, r3
	adds	r0, r1, r3
	cmp	r0, r2
	beq	.L672
	ldr	r6, [r4, #816]
	ldrh	r2, [r0, #4]
	ldrh	r7, [r6, r5, lsl #1]
	cbz	r2, .L676
	muls	r2, r7, r2
	b	.L673
.L676:
	mov	r2, #-1
.L673:
	ldrh	r0, [r0, #2]
	movw	r7, #65535
	cmp	r0, r7
	bne	.L674
	ldrh	r3, [r1, r3]
	cmp	r3, r0
	beq	.L672
.L674:
	movs	r3, #6
	muls	r3, r0, r3
	ldr	r0, .L678+4
	asrs	r7, r3, #1
	add	r3, r3, r1
	muls	r0, r7, r0
	ldrh	r3, [r3, #4]
	ldrh	r0, [r6, r0, lsl #1]
	cbz	r3, .L677
	muls	r3, r0, r3
	b	.L675
.L677:
	mov	r3, #-1
.L675:
	cmp	r2, r3
	bcs	.L672
	ldr	r0, .L678+8
	mov	r1, r5
	bl	List_remove_node
	ldrh	r3, [r4, #824]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #824]	@ movhi
	bl	INSERT_DATA_LIST
.L672:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L679:
	.align	2
.L678:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+812
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
	bcs	.L684
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L681
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	subs	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L682
	ldr	r3, .L687
	ldr	r0, .L687+4
	ldrh	r2, [r3, #3038]
	bl	printk
.L683:
	b	.L683
.L682:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L684:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L688:
	.align	2
.L687:
	.word	.LANCHOR0
	.word	.LC6
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
	ldr	r3, .L703
	movs	r4, #12
	ldr	r7, .L703+4
	movw	r5, #65535
	ldrh	r2, [r3, #2950]
	ldr	r3, [r7, #980]
.L690:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L701
	adds	r1, r1, #1
	mla	r6, r4, r1, r3
	ldrh	r6, [r6, #-12]
	cmp	r6, r5
	bne	.L690
	b	.L691
.L701:
	movs	r1, #0
	mov	r6, #-2147483648
	mov	lr, #12
	mov	r0, r2
.L693:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L702
	mla	r4, lr, r1, r3
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L694
	cmp	r4, r6
	itt	cc
	movcc	r6, r4
	movcc	r0, r5
.L694:
	adds	r1, r1, #1
	b	.L693
.L702:
	cmp	r0, r2
	bcc	.L691
	ldrh	r7, [r7, #984]
	movs	r1, #0
	mov	r4, #-1
	mov	r0, r2
.L696:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L691
	ldr	r6, [r3, #4]
	cmp	r6, r4
	bcs	.L697
	ldrh	lr, [r3]
	cmp	lr, r7
	itt	ne
	movne	r4, r6
	movne	r0, r5
.L697:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L696
.L691:
	pop	{r4, r5, r6, r7, pc}
.L704:
	.align	2
.L703:
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
	ldr	r1, .L712
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r2, [r1, #986]
	mov	r3, r1
	cmp	r2, #4
	bhi	.L706
	cbnz	r0, .L706
	adds	r2, r2, #1
	strh	r2, [r1, #986]	@ movhi
	pop	{r4, r5, pc}
.L706:
	ldr	r1, .L712+4
	movs	r2, #0
	strh	r2, [r3, #986]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #988]
	ldrh	r1, [r1, #2848]
	ldr	r2, [r3, #816]
	add	r1, r2, r1, lsl #1
.L707:
	cmp	r2, r1
	beq	.L711
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #988]
	addne	r0, r0, r4
	strne	r0, [r3, #988]
	b	.L707
.L711:
	pop	{r4, r5, pc}
.L713:
	.align	2
.L712:
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
	ldr	r2, .L717
	and	r0, r0, #31
	uxth	r3, r3
	cbz	r1, .L715
	ldr	r1, [r2, #992]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	orrs	r2, r2, r0
	str	r2, [r1, r3, lsl #2]
	bx	lr
.L715:
	ldr	r1, [r2, #992]
	movs	r2, #1
	lsls	r2, r2, r0
	ldr	r0, [r1, r3, lsl #2]
	bic	r0, r0, r2
	str	r0, [r1, r3, lsl #2]
	bx	lr
.L718:
	.align	2
.L717:
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
	ldr	r3, .L720
	lsrs	r1, r0, #5
	ldr	r2, [r3, #992]
	and	r3, r0, #31
	ldr	r0, [r2, r1, lsl #2]
	lsrs	r0, r0, r3
	and	r0, r0, #1
	bx	lr
.L721:
	.align	2
.L720:
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
	ldr	r3, .L731
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	adds	r4, r2, #7
	ldrh	r5, [r3, #2840]
	movw	r7, #65535
	add	r4, r0, r4, lsl #1
.L723:
	cmp	r2, r5
	bcs	.L729
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	cmp	r6, r7
	uxth	r2, r2
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L723
.L729:
	ldrh	r6, [r3, #2910]
	add	r4, r0, #14
	movs	r2, #0
	movw	r7, #65535
.L726:
	uxth	r3, r2
	cmp	r3, r5
	bcs	.L730
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
	b	.L726
.L730:
	pop	{r4, r5, r6, r7, pc}
.L732:
	.align	2
.L731:
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
	ldr	r6, .L746
	add	r7, r0, #16
	mov	r4, r0
	mov	r5, r3
	addw	r10, r6, #2868
	movw	r9, #65535
	ldrh	r8, [r6, #2840]
	strh	r3, [r0, #4]	@ movhi
	strb	r3, [r0, #7]
.L734:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L745
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	ldrh	r1, [r4]
	bl	V2P_block
	strh	r9, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L735
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L735:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L734
.L745:
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r3, [r6, #2910]
	smulbb	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2832]
	cbz	r3, .L737
	ldr	r3, .L746+4
	ldrh	r2, [r4]
	ldr	r3, [r3, #752]
	ldrh	r3, [r3, r2, lsl #1]
	cmp	r3, #59
	itt	ls
	movls	r3, #1
	strbls	r3, [r4, #9]
.L737:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cbz	r3, .L738
	movs	r3, #1
	strb	r3, [r4, #9]
.L738:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L747:
	.align	2
.L746:
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
	ldr	r3, .L757
	mov	r7, r0
	mov	r4, r5
	addw	r6, r3, #2868
	ldrh	r8, [r3, #2840]
	ldrh	r9, [r3, #2910]
.L749:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L756
	mov	r1, r7
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L750
	add	r4, r4, r9
	uxth	r4, r4
.L750:
	adds	r5, r5, #1
	b	.L749
.L756:
	cbz	r4, .L752
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L752:
	ldr	r3, .L757+4
	movs	r2, #6
	movs	r0, #0
	ldr	r3, [r3, #808]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L758:
	.align	2
.L757:
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
	ldr	r2, .L762
	ldr	r0, [r2, #828]
	cbz	r0, .L760
	ldr	r3, [r2, #808]
	subs	r0, r0, r3
	ldr	r3, .L762+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #752]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L760:
	bx	lr
.L763:
	.align	2
.L762:
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
	ldr	r1, .L772
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #828]
	cbz	r3, .L770
	ldrh	r2, [r1, #832]
	movs	r6, #6
	ldr	r4, [r1, #808]
	movw	r7, #65535
	rsb	r2, r2, r2, lsl #3
	subs	r3, r3, r4
	asrs	r2, r2, #3
	asrs	r3, r3, #1
	cmp	r0, r2
	it	gt
	uxthgt	r0, r2
	ldr	r2, .L772+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L767:
	uxth	r5, r2
	cmp	r5, r0
	bcs	.L769
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L771
.L769:
	ldr	r2, [r1, #752]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L771:
	mov	r3, r5
	b	.L767
.L770:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L773:
	.align	2
.L772:
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
	ldr	r8, .L784+240
	add	r4, r6, #12
	.pad #32
	sub	sp, sp, #32
	ldr	r1, .L784
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L784+4
	ldr	r2, [r8, #1864]
	bl	sprintf
	ldr	r1, .L784+8
	ldr	r2, [r8, #2928]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L784+12
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r4, r0
	bne	.L780
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	mov	r0, r4
	ldr	r1, .L784+16
	str	r3, [sp]
	ldr	r3, [sp, #28]
	ldr	r7, .L784+20
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L784+24
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L784+28
	ldr	r2, [r8, #2968]
	bl	sprintf
	ldr	r1, .L784+32
	ldr	r2, [r7, #988]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+36
	ldr	r2, [r7, #996]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+40
	ldr	r2, [r7, #1000]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+44
	ldr	r2, [r7, #1004]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+48
	ldr	r2, [r7, #1008]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+52
	ldr	r2, [r7, #1012]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+56
	ldr	r2, [r7, #1016]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r7, #1020]
	ldr	r1, .L784+60
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r7, #1024]
	ldr	r1, .L784+64
	lsrs	r2, r2, #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+68
	ldr	r2, [r7, #1028]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+72
	ldr	r2, [r7, #1032]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldr	r1, .L784+76
	ldrh	r2, [r8, #2978]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+80
	ldrh	r2, [r7, #832]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+84
	ldr	r2, [r7, #1036]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+88
	ldr	r2, [r7, #1040]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+92
	ldr	r2, [r7, #1044]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+96
	ldr	r2, [r7, #756]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+100
	ldr	r2, [r7, #1048]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+104
	ldr	r2, [r7, #1052]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+108
	ldrh	r2, [r7, #790]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+112
	ldrh	r2, [r7, #788]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+116
	ldr	r2, [r8, #1896]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+120
	ldr	r2, [r8, #2944]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+124
	ldr	r2, [r8, #2836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+128
	ldrh	r2, [r8, #3038]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+132
	ldrh	r2, [r8, #2848]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+136
	ldrh	r2, [r7, #1056]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+140
	ldr	r2, [r8, #2852]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+144
	ldrh	r2, [r7, #1060]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+148
	ldrh	r2, [r8, #2972]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+152
	ldrh	r2, [r7, #838]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+156
	ldrb	r2, [r7, #842]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+160
	ldrh	r2, [r7, #836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+164
	ldrb	r2, [r7, #844]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+168
	ldrh	r2, [r7, #840]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r7, #816]
	ldrh	r2, [r7, #836]
	ldr	r1, .L784+172
	ldrh	r2, [r3, r2, lsl #1]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+176
	ldrh	r2, [r7, #886]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+180
	ldrb	r2, [r7, #890]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+184
	ldrh	r2, [r7, #884]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+188
	ldrb	r2, [r7, #892]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+192
	ldrh	r2, [r7, #888]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r7, #816]
	ldrh	r2, [r7, #884]
	ldr	r1, .L784+196
	ldrh	r2, [r3, r2, lsl #1]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+200
	ldrh	r2, [r7, #934]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+204
	ldrb	r2, [r7, #938]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+208
	ldrh	r2, [r7, #932]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+212
	ldrb	r2, [r7, #940]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+216
	ldrh	r2, [r7, #936]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+220
	ldrh	r2, [r7, #1074]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+224
	ldrb	r2, [r7, #1078]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+228
	ldrh	r2, [r7, #1072]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+232
	ldrb	r2, [r7, #1080]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L784+236
	ldrh	r2, [r7, #1076]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r7, #1208]
	ldr	r3, [r8, #2832]
	ldr	r2, [r7, #1120]
	str	r1, [sp]
	b	.L785
.L786:
	.align	2
.L784:
	.word	.LC7
	.word	.LC8
	.word	.LC9
	.word	.LANCHOR1
	.word	.LC10
	.word	.LANCHOR2
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
	.word	.LC64
	.word	.LANCHOR0
.L785:
	ldr	r1, [r7, #1200]
	orr	r2, r3, r2, lsl #8
	str	r1, [sp, #4]
	ldr	r3, [r7, #1204]
	ldr	r1, .L787
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L787+4
	ldr	r2, [r7, #1196]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L787+8
	ldr	r2, [r7, #1220]
	adds	r4, r5, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L787+12
	ldrh	r2, [r7, #1636]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L787+16
	ldrh	r2, [r7, #1638]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L787+20
	ldr	r2, [r7, #1640]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L787+24
	ldrh	r2, [r7, #1644]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L787+28
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r7, #832]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L787+32
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r7, #1072]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L777
	ldr	r2, [r7, #816]
	mov	r0, r4
	ldr	r1, .L787+36
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
	add	r4, r4, r0
.L777:
	movs	r0, #0
	movs	r5, #0
	bl	List_get_gc_head_node
	mov	r9, #6
	movw	r10, #65535
	uxth	r3, r0
.L779:
	cmp	r3, r10
	beq	.L778
	ldr	r2, [r7, #816]
	mov	r0, r4
	mul	r8, r9, r3
	ldr	r1, .L787+40
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp]
	ldr	r2, [r7, #808]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r7, #752]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r7, #808]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L779
.L778:
	ldr	r2, [r7, #808]
	movs	r5, #0
	ldr	r3, [r7, #828]
	mov	r9, #6
	movw	r10, #65535
	subs	r3, r3, r2
	ldr	r2, .L787+44
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r3, r3
.L781:
	cmp	r3, r10
	beq	.L780
	mul	r8, r9, r3
	ldr	r2, [r7, #808]
	mov	r0, r4
	ldr	r1, .L787+48
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	ldr	r2, [r7, #752]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r7, #808]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L781
.L780:
	subs	r0, r4, r6
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L788:
	.align	2
.L787:
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
	.word	.LC75
	.word	-1431655765
	.word	.LC76
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
	ldr	r1, .L790
	ldr	r2, .L790+4
	bl	sprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L791:
	.align	2
.L790:
	.word	.LC77
	.word	.LC78
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
	ldr	r4, .L820
	ldr	r2, [r4, #1044]
	ldr	r3, [r4, #1052]
	cmp	r2, r3
	bcs	.L793
	ldr	r2, .L820+4
	movs	r3, #0
	ldr	r0, [r4, #752]
	str	r3, [r4, #1036]
	ldrh	r1, [r2, #2848]
	mov	r5, r2
.L794:
	cmp	r3, r1
	bcs	.L819
	ldrh	r6, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r2, [r4, #1036]
	add	r2, r2, r6
	str	r2, [r4, #1036]
	b	.L794
.L819:
	ldr	r6, [r4, #1036]
	mov	r0, r6
	bl	__aeabi_uidiv
	ldrh	r1, [r5, #2900]
	str	r0, [r4, #1044]
	ldr	r0, [r4, #1040]
	subs	r0, r6, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #1036]
	b	.L796
.L793:
	ldr	r3, [r4, #1048]
	cmp	r2, r3
	bls	.L796
	ldr	r0, .L820+4
	adds	r3, r3, #1
	str	r3, [r4, #1048]
	movs	r3, #0
.L798:
	ldrh	r2, [r0, #2848]
	cmp	r3, r2
	bcs	.L796
	ldr	r1, [r4, #752]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L798
.L796:
	ldr	r6, [r4, #1052]
	ldr	r8, [r4, #1044]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L801
	ldr	r2, .L820
	add	r3, r6, #768
	ldr	r2, [r2, #1048]
	cmp	r3, r2
	bls	.L801
	ldr	r3, .L820+4
	ldr	r3, [r3, #2832]
	cbnz	r3, .L802
.L804:
	movw	r0, #65535
	b	.L803
.L802:
	cmp	r6, #30
	bhi	.L804
.L801:
	ldrh	r0, [r4, #832]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r9, r0
	bcs	.L805
	cmp	r6, #30
	bhi	.L804
.L805:
	ldr	r3, [r4, #812]
	ldr	r2, .L820
	cmp	r3, #0
	beq	.L804
	ldr	r1, .L820+4
	movw	r7, #65535
	ldr	r0, [r2, #808]
	mov	ip, #6
	ldr	r10, [r2, #752]
	mov	r5, r7
	ldrh	r1, [r1, #2848]
	mov	lr, r7
	str	r1, [sp, #20]
	movs	r1, #0
.L806:
	ldrh	r2, [r3]
	cmp	r2, lr
	str	r2, [sp, #16]
	beq	.L808
	adds	r1, r1, #1
	ldr	r2, [sp, #20]
	uxth	r1, r1
	cmp	r1, r2
	bhi	.L804
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L807
	subs	r3, r3, r0
	ldr	r2, .L820+8
	asrs	r3, r3, #1
	muls	r3, r2, r3
	uxth	r3, r3
	ldrh	fp, [r10, r3, lsl #1]
	cmp	fp, r6
	bls	.L812
	cmp	fp, r7
	itt	cc
	movcc	r7, fp
	movcc	r5, r3
.L807:
	ldr	r3, [sp, #16]
	mla	r3, ip, r3, r0
	b	.L806
.L812:
	mov	r5, r3
.L808:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L804
	ldrh	fp, [r10, r5, lsl #1]
	lsls	r3, r5, #1
	cmp	fp, r6
	bls	.L810
	str	r3, [sp, #16]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #16]
	cmp	r0, r6
	it	hi
	strhi	r7, [r4, #1052]
.L810:
	cmp	fp, r8
	bcs	.L804
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L804
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L811
	ldr	r2, [r4, #1048]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L804
.L811:
	ldr	r2, [r4, #816]
	mov	r1, r5
	ldr	r0, .L820+12
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	str	r9, [sp, #8]
	str	r3, [sp, #4]
	ldr	r3, [r4, #1048]
	bl	printk
	mov	r0, r5
	movs	r3, #1
	str	r3, [r4, #1648]
.L803:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L821:
	.align	2
.L820:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC79
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
	beq	.L823
	ldr	r2, .L824
	movs	r1, #0
	ldr	r2, [r2, #816]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L823:
	movs	r0, #0
	pop	{r3, pc}
.L825:
	.align	2
.L824:
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
	ldr	r2, .L837
	mov	ip, #36
	ldr	r0, .L837+4
	movs	r3, #0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r7, #12
	mov	lr, #1
	mov	r6, r0
	mov	r4, r2
	str	r3, [r2, #1652]
.L827:
	ldrh	r1, [r0, #2840]
	adds	r5, r3, #1
	uxth	r3, r3
	ldr	r8, .L837+4
	cmp	r3, r1
	bcs	.L835
	mul	r8, r7, r3
	ldr	r9, [r2, #1656]
	add	r1, r9, r8
	str	lr, [r1, #8]
	ldrh	r1, [r0, #2920]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	bic	r10, r1, #3
	ldr	r1, [r4, #1660]
	add	r1, r1, r10
	str	r1, [r9, r8]
	ldr	r10, [r2, #1656]
	ldrh	r1, [r6, #2922]
	muls	r1, r3, r1
	it	mi
	addmi	r1, r1, #3
	add	r9, r10, r8
	bic	fp, r1, #3
	ldr	r1, [r4, #1664]
	add	r1, r1, fp
	str	r1, [r9, #4]
	ldr	r1, [r2, #1668]
	mla	r3, ip, r3, r1
	ldr	r1, [r10, r8]
	str	r1, [r3, #8]
	ldr	r1, [r9, #4]
	str	r1, [r3, #12]
	mov	r3, r5
	b	.L827
.L835:
	ldr	r0, .L837
	mov	lr, #12
	movs	r5, #0
.L831:
	ldr	r3, [r2, #1672]
	cmp	r1, r3
	bcs	.L836
	mul	r7, lr, r1
	ldr	r6, [r0, #1656]
	ldr	r4, [r0, #1660]
	adds	r3, r6, r7
	str	r5, [r3, #8]
	ldrh	r3, [r8, #2920]
	muls	r3, r1, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r4
	str	r3, [r6, r7]
	ldr	r3, [r2, #1656]
	ldr	r6, [r0, #1664]
	add	r7, r7, r3
	ldrh	r3, [r8, #2922]
	muls	r3, r1, r3
	add	r1, r1, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r1, r1
	add	r3, r3, r6
	str	r3, [r7, #4]
	b	.L831
.L836:
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L838:
	.align	2
.L837:
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
	ldr	r3, .L846
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	ldr	lr, [r3, #1672]
	mov	r10, #36
	ldr	r6, [r3, #1656]
	mov	r8, #12
	mov	ip, r5
.L840:
	uxth	r4, r5
	cmp	r4, r1
	bcs	.L839
	mla	r4, r10, r4, r0
	movs	r2, #0
.L841:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L842
	mul	r3, r8, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L841
	str	ip, [r9, #8]
.L842:
	adds	r5, r5, #1
	b	.L840
.L839:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L847:
	.align	2
.L846:
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
	ldr	r3, .L856
	mov	ip, #1
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	ldr	r5, [r3, #1672]
	mov	lr, #12
	ldr	r6, [r3, #1656]
	mov	r8, #36
.L849:
	uxth	r2, r4
	cmp	r2, r1
	bcs	.L855
	mov	r9, #0
.L850:
	uxth	r3, r9
	cmp	r3, r5
	bcs	.L851
	mla	r3, lr, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L850
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	ip, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L851:
	adds	r4, r4, #1
	b	.L849
.L855:
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L857:
	.align	2
.L856:
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
	ldr	r2, .L864
	ldr	r3, [r2, #1676]
	ldrh	r2, [r2, #1680]
	add	r2, r3, r2, lsl #1
.L859:
	cmp	r3, r2
	beq	.L863
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L859
	movs	r0, #1
	bx	lr
.L863:
	movs	r0, #0
	bx	lr
.L865:
	.align	2
.L864:
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
	ldr	r3, .L873
	movs	r4, #0
	ldrh	r1, [r3, #1680]
	ldr	r2, [r3, #1676]
.L867:
	uxth	r5, r4
	cmp	r5, r1
	bcs	.L871
	adds	r4, r4, #1
	add	lr, r2, r4, lsl #1
	ldrh	lr, [lr, #-2]
	cmp	lr, r0
	bne	.L867
.L871:
	cmp	r5, r1
	bne	.L869
	strh	r0, [r2, r5, lsl #1]	@ movhi
	ldrh	r0, [r3, #1680]
	adds	r0, r0, #1
	strh	r0, [r3, #1680]	@ movhi
.L869:
	ldrh	r2, [r3, #1688]
	movs	r0, #12
	muls	r0, r2, r0
	ldr	r2, [r3, #1684]
	adds	r1, r2, r0
	str	r7, [r1, #4]
	str	r8, [r1, #8]
	str	r6, [r2, r0]
	ldrh	r2, [r3, #1688]
	adds	r2, r2, #1
	strh	r2, [r3, #1688]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L874:
	.align	2
.L873:
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
	ldr	r0, .L878
	bl	printk
	ldr	r3, .L878+4
	ldrh	r0, [r3, #1690]
	cmp	r0, r4
	beq	.L876
	ldrh	r1, [r3, #1692]
	cmp	r1, r4
	beq	.L876
	movw	r2, #65535
	cmp	r0, r2
	bne	.L877
	strh	r4, [r3, #1690]	@ movhi
	b	.L876
.L877:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #1692]	@ movhi
.L876:
	movs	r0, #0
	pop	{r4, pc}
.L879:
	.align	2
.L878:
	.word	.LC80
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
	ldr	r0, .L883
	bl	printk
	ldr	r3, .L883+4
	ldrh	r0, [r3, #1690]
	cmp	r0, r4
	beq	.L881
	ldrh	r1, [r3, #1692]
	cmp	r1, r4
	beq	.L881
	movw	r2, #65535
	cmp	r0, r2
	bne	.L882
	strh	r4, [r3, #1690]	@ movhi
	b	.L881
.L882:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #1692]	@ movhi
.L881:
	movs	r0, #0
	pop	{r4, pc}
.L884:
	.align	2
.L883:
	.word	.LC80
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
	ldr	r4, .L894
	mov	r2, r5
	ldrh	r1, [r4, #1694]
	mov	r6, r0
	ldr	r0, .L894+4
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r3, .L894+8
	ldr	r2, [r3, #2832]
	mov	r3, r4
	cbz	r2, .L886
	ldr	r1, [r4, #752]
	ldrh	r2, [r1, r6, lsl #1]
	cmp	r2, #29
	itt	hi
	subhi	r2, r2, #30
	strhhi	r2, [r1, r6, lsl #1]	@ movhi
.L886:
	ldrh	r2, [r3, #1694]
	movs	r1, #0
	ldr	r4, .L894+12
.L887:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L893
	adds	r1, r1, #1
	add	r0, r4, r1, lsl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r5
	bne	.L887
	b	.L888
.L893:
	cmp	r2, #15
	itttt	ls
	addls	r1, r2, #1
	strhls	r1, [r3, #1694]	@ movhi
	addls	r3, r3, r2, lsl #1
	strhls	r5, [r3, #1696]	@ movhi
.L888:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L895:
	.align	2
.L894:
	.word	.LANCHOR2
	.word	.LC81
	.word	.LANCHOR0
	.word	.LANCHOR2+1696
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
	ldr	r4, .L902
	ldrh	r3, [r4, #1694]
	cbz	r3, .L897
	ldrh	r1, [r4, #1690]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L897
	ldrh	r2, [r4, #1730]
	cmp	r2, r3
	itt	cs
	movcs	r3, #0
	strhcs	r3, [r4, #1730]	@ movhi
	ldrh	r3, [r4, #1730]
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #1696]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #1730]
	adds	r3, r3, #1
	strh	r3, [r4, #1730]	@ movhi
.L897:
	movs	r0, #0
	pop	{r4, pc}
.L903:
	.align	2
.L902:
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
	cbz	r2, .L905
	bl	memset
.L905:
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
	ldr	r4, .L1013
	movs	r6, #0
	ldr	r5, .L1013+4
	movw	r3, #65535
	movs	r7, #12
	mov	r8, #36
	strh	r6, [r4, #1732]	@ movhi
	ldrh	r1, [r5, #2916]
	str	r6, [r4, #1736]
	str	r6, [r4, #1120]
	str	r6, [r4, #1028]
	str	r6, [r4, #1032]
	str	r6, [r4, #1016]
	str	r6, [r4, #1004]
	str	r6, [r4, #1000]
	str	r6, [r4, #1008]
	str	r6, [r4, #1012]
	str	r6, [r4, #996]
	str	r6, [r4, #1036]
	str	r6, [r4, #1040]
	str	r6, [r4, #756]
	str	r6, [r4, #1048]
	str	r6, [r4, #1052]
	str	r6, [r4, #1740]
	str	r6, [r4, #1648]
	str	r3, [r4, #1744]
	str	r6, [r4, #1748]
	str	r6, [r4, #1640]
	str	r6, [r4, #1752]
	strh	r3, [r4, #1690]	@ movhi
	strh	r3, [r4, #1692]	@ movhi
	movs	r3, #32
	strh	r6, [r4, #1644]	@ movhi
	strh	r3, [r4, #1636]	@ movhi
	movs	r3, #128
	strh	r6, [r4, #1694]	@ movhi
	strh	r3, [r4, #1638]	@ movhi
	strh	r6, [r4, #1756]	@ movhi
	strh	r6, [r4, #1730]	@ movhi
	bl	__aeabi_idiv
	ldrh	r3, [r5, #2840]
	str	r6, [r5, #2960]
	lsls	r3, r3, #2
	cmp	r0, r3
	str	r0, [r4, #1760]
	ldrh	r0, [r5, #2914]
	it	hi
	strhi	r3, [r4, #1760]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #1676]
	ldrh	r0, [r5, #2914]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #2840]
	mul	r6, r8, r6
	lsl	r9, r6, #3
	str	r0, [r4, #1684]
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
	str	r0, [r4, #748]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1668]
	ldr	r0, [r4, #1760]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #2920]
	ldrh	r3, [r5, #2840]
	lsls	r3, r3, #1
	adds	r3, r3, #1
	str	r3, [r4, #1672]
	str	r0, [r5, #2964]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1776]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1780]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1784]
	ldr	r0, [r4, #1672]
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #1660]
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
	ldr	r0, [r4, #1672]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2840]
	ldrh	r6, [r5, #2922]
	muls	r6, r3, r6
	str	r0, [r4, #1656]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1800]
	lsls	r0, r6, #3
	bl	ftl_malloc
	ldrh	r3, [r5, #2922]
	str	r0, [r4, #1804]
	ldr	r0, [r4, #1672]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2922]
	str	r0, [r4, #1664]
	ldr	r0, [r4, #1760]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #1808]
	ldrh	r0, [r5, #2850]
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
	ldrh	r6, [r5, #2850]
	lsls	r6, r6, #1
	str	r0, [r4, #1820]
	adds	r0, r0, #32
	str	r0, [r4, #752]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1824]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r5, #2940]
	lsls	r6, r6, #1
	str	r0, [r4, #816]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1828]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1832]
	ldrh	r0, [r5, #2850]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #992]
	ldrh	r0, [r5, #2932]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r5, #2956]
	ldrh	r0, [r5, #2932]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #1836]
	ldrh	r0, [r5, #2932]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #1840]
	ldrh	r0, [r5, #2934]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5, #2934]
	movs	r1, #0
	lsls	r2, r2, #2
	str	r0, [r4, #1844]
	bl	ftl_memset
	ldrh	r6, [r5, #2948]
	lsls	r6, r6, #2
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1848]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, .L1013+8
	str	r0, [r4, #1852]
	ldr	r0, [r5, #2940]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #1856]
	ldrh	r0, [r5, #2950]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2950]
	str	r0, [r4, #980]
	ldrh	r0, [r5, #2920]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2850]
	str	r0, [r4, #1860]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2906]
	adds	r3, r3, #31
	asrs	r3, r3, #5
	strh	r3, [r4, #1864]	@ movhi
	str	r0, [r4, #808]
	ldrh	r0, [r5, #2862]
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #1864]
	ldrh	r7, [r5, #2862]
	movs	r3, #1
	lsls	r2, r2, #2
	mov	r1, r2
	str	r0, [r5, #3000]
.L911:
	cmp	r3, r7
	bcs	.L1011
	ldr	r0, [r5, #3000]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r6, #4]!
	b	.L911
.L1011:
	ldr	r2, .L1013+12
	movs	r1, #0
.L913:
	cmp	r3, #8
	beq	.L1012
	add	r0, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r0, #28]
	b	.L913
.L1012:
	ldr	r2, [r4, #1828]
	ldr	r3, .L1013
	cbnz	r2, .L915
.L917:
	ldr	r1, .L1013+16
	ldr	r0, .L1013+20
	bl	printk
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1014:
	.align	2
.L1013:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+3000
	.word	.LANCHOR0+2972
	.word	.LANCHOR3
	.word	.LC82
.L915:
	ldr	r2, [r3, #1832]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1848]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1856]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #980]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1860]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #808]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r5, #3000]
	cmp	r2, #0
	beq	.L917
	ldr	r3, [r3, #816]
	cmp	r3, #0
	beq	.L917
	ldr	r2, [r4, #1676]
	ldr	r3, .L1015
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1684]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1764]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1772]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #748]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1668]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1768]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1776]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1780]
	cmp	r2, #0
	beq	.L917
	ldr	r3, [r3, #1784]
	cmp	r3, #0
	beq	.L917
	ldr	r2, [r4, #1660]
	ldr	r3, .L1015
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1792]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1796]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1656]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1800]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1804]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1664]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #752]
	cmp	r2, #0
	beq	.L917
	ldr	r3, [r3, #1816]
	cmp	r3, #0
	beq	.L917
	ldr	r3, .L1015+4
	ldr	r3, [r3, #2956]
	cmp	r3, #0
	beq	.L917
	ldr	r3, .L1015
	ldr	r2, [r3, #1836]
	cmp	r2, #0
	beq	.L917
	ldr	r2, [r3, #1840]
	cmp	r2, #0
	beq	.L917
	ldr	r3, [r3, #1844]
	cmp	r3, #0
	beq	.L917
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1016:
	.align	2
.L1015:
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
	ldr	r3, .L1022
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
.L1019:
	ldrh	r3, [r2, #2]!
	cmp	r3, r7
	beq	.L1017
	lsrs	r1, r3, #5
	cmp	r2, r4
	and	r3, r3, #31
	ldr	r0, [r5, r1, lsl #2]
	lsl	r3, r6, r3
	orr	r3, r3, r0
	str	r3, [r5, r1, lsl #2]
	bne	.L1019
.L1017:
	pop	{r3, r4, r5, r6, r7, pc}
.L1023:
	.align	2
.L1022:
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
.L1025:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L1038
	ldr	r0, [r5, r3, lsl #2]
	movs	r1, #0
	ubfx	r0, r0, #10, #16
.L1026:
	ldrh	r7, [r4, #10]
	uxth	r3, r1
	cmp	r7, r3
	bls	.L1039
	ldrh	r7, [r9, r3, lsl #1]
	adds	r1, r1, #1
	cmp	r7, r0
	ittt	eq
	ldrheq	r7, [r6, r3, lsl #1]
	addeq	r7, r7, #1
	strheq	r7, [r6, r3, lsl #1]	@ movhi
	b	.L1026
.L1039:
	adds	r2, r2, #1
	b	.L1025
.L1038:
	movs	r7, #0
	ldrh	fp, [r6]
	ldr	r2, .L1041
	mov	r10, r7
.L1030:
	ldrh	r3, [r4, #10]
	uxth	r5, r7
	cmp	r3, r5
	bls	.L1040
	ldrh	r3, [r4]
	cmp	r3, r5
	bne	.L1031
	ldrh	r3, [r2, #2912]
	ldrh	r1, [r4, #2]
	cmp	r1, r3
	it	cc
	strhcc	r3, [r6, r5, lsl #1]	@ movhi
.L1031:
	ldrh	r8, [r6, r5, lsl #1]
	cmp	fp, r8
	itt	hi
	movhi	r10, r5
	movhi	fp, r8
	cmp	r8, #0
	bne	.L1033
	ldrh	r0, [r9, r5, lsl #1]
	cbz	r0, .L1033
	movs	r1, #1
	str	r2, [sp, #4]
	bl	FtlFreeSysBlkQueueIn
	ldr	r2, [sp, #4]
	strh	r8, [r9, r5, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1033:
	adds	r7, r7, #1
	b	.L1030
.L1040:
	mov	r0, r10
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1042:
	.align	2
.L1041:
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
	ldr	r4, .L1047
	movs	r7, #0
	ldr	r5, .L1047+4
	mov	r6, r7
	ldr	r2, [r4, #2940]
	mov	r9, r4
	ldr	r0, [r5, #1832]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #2920]
	ldrh	r2, [r4, #2950]
	movs	r1, #255
	ldr	r0, [r5, #1860]
	muls	r2, r3, r2
	bl	ftl_memset
	mov	r3, r5
	movs	r5, #12
	mov	r2, r4
	movw	lr, #65535
.L1044:
	ldrh	ip, [r2, #2950]
	adds	r0, r7, #1
	uxth	r7, r7
	ldr	r1, .L1047
	cmp	ip, r7
	bls	.L1046
	mul	r1, r5, r7
	ldr	ip, [r3, #980]
	add	r8, ip, r1
	str	r6, [r8, #4]
	strh	lr, [ip, r1]	@ movhi
	ldr	r4, [r3, #980]
	add	ip, r4, r1
	ldrh	r1, [r9, #2920]
	muls	r1, r7, r1
	ldr	r7, [r3, #1860]
	bic	r1, r1, #3
	add	r1, r1, r7
	mov	r7, r0
	str	r1, [ip, #8]
	b	.L1044
.L1046:
	ldr	r0, [r1, #2940]
	movw	r2, #65535
	ldrh	r1, [r1, #2948]
	strh	r2, [r3, #1870]	@ movhi
	strh	r0, [r3, #1878]	@ movhi
	movw	r0, #61634
	strh	r1, [r3, #1874]	@ movhi
	ldr	r1, [r3, #1828]
	strh	r0, [r3, #1872]	@ movhi
	ldrh	r0, [r3, #1912]
	str	r1, [r3, #1880]
	ldr	r1, [r3, #1856]
	strh	r2, [r3, #1868]	@ movhi
	strh	r0, [r3, #1876]	@ movhi
	str	r1, [r3, #1884]
	ldr	r1, [r3, #1832]
	strh	r2, [r3, #1908]	@ movhi
	str	r1, [r3, #1888]
	ldr	r1, [r3, #1848]
	str	r1, [r3, #1892]
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1048:
	.align	2
.L1047:
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
	ldr	r6, .L1050
	movw	r3, #65535
	ldr	r5, .L1050+4
	mov	r1, r4
	ldrh	r2, [r6, #2932]
	ldr	r0, [r6, #2956]
	strh	r3, [r5, #1920]	@ movhi
	mov	r3, #-1
	lsls	r2, r2, #1
	str	r3, [r5, #1932]
	str	r4, [r5, #1916]
	str	r4, [r5, #1924]
	str	r4, [r5, #1928]
	str	r4, [r6, #2832]
	strh	r4, [r6, #2954]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r6, #2850]
	mov	r1, r4
	ldr	r0, [r5, #752]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6, #2850]
	mov	r1, r4
	ldr	r0, [r5, #1816]
	lsls	r2, r2, #1
	bl	ftl_memset
	add	r0, r5, #760
	movs	r1, #48
	bl	__memzero
	addw	r0, r5, #1124
	mov	r1, #512
	bl	__memzero
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1051:
	.align	2
.L1050:
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
	ldr	r6, .L1063
	movs	r5, #0
	ldr	r4, .L1063+4
	.pad #20
	sub	sp, sp, #20
	movs	r1, #0
	mov	r9, r5
	ldrh	r3, [r6, #2850]
	mov	r10, r5
	ldr	r0, [r4, #808]
	mov	r8, r6
	muls	r2, r3, r2
	bl	ftl_memset
	str	r5, [r4, #828]
	str	r5, [r4, #812]
	str	r5, [r4, #820]
	strh	r5, [r4, #824]	@ movhi
	strh	r5, [r4, #832]	@ movhi
	strh	r5, [r4, #1732]	@ movhi
.L1053:
	uxth	fp, r5
	ldrh	r2, [r8, #2848]
	sxth	r7, fp
	cmp	r7, r2
	bge	.L1060
	ldr	r3, .L1063
	movs	r2, #0
	mov	r6, r2
	ldrh	ip, [r3, #2840]
	ldrh	r3, [r3, #2910]
	str	r3, [sp, #4]
.L1061:
	sxth	r1, r2
	cmp	r1, ip
	bge	.L1062
	add	r1, r1, r8
	str	r2, [sp, #12]
	str	ip, [sp, #8]
	ldrb	r0, [r1, #2868]	@ zero_extendqisi2
	mov	r1, fp
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	ip, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L1054
	ldr	r3, [sp, #4]
	add	r6, r6, r3
	uxth	r6, r6
.L1054:
	adds	r2, r2, #1
	b	.L1061
.L1062:
	cbz	r6, .L1056
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1057
.L1056:
	ldr	r1, [r4, #816]
	sxth	r2, fp
	movw	r0, #65535
	strh	r0, [r1, r2, lsl #1]	@ movhi
.L1057:
	add	r0, r7, r7, lsl #1
	ldr	r1, [r4, #808]
	ldr	r2, .L1063+4
	add	r1, r1, r0, lsl #1
	strh	r6, [r1, #4]	@ movhi
	ldrh	r1, [r4, #836]
	cmp	r7, r1
	beq	.L1058
	ldrh	r1, [r2, #884]
	cmp	r7, r1
	beq	.L1058
	ldrh	r1, [r2, #932]
	cmp	r7, r1
	beq	.L1058
	ldr	r2, [r2, #816]
	ldrh	r2, [r2, r7, lsl #1]
	cbnz	r2, .L1059
	add	r9, r9, #1
	mov	r0, fp
	uxth	r9, r9
	bl	INSERT_FREE_LIST
	b	.L1058
.L1059:
	add	r10, r10, #1
	mov	r0, fp
	uxth	r10, r10
	bl	INSERT_DATA_LIST
.L1058:
	adds	r5, r5, #1
	b	.L1053
.L1060:
	movs	r0, #0
	strh	r10, [r4, #824]	@ movhi
	strh	r9, [r4, #832]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1064:
	.align	2
.L1063:
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
	ldr	r5, .L1066
	movs	r3, #0
	ldr	r4, .L1066+4
	ldrh	r2, [r5, #2914]
	ldr	r0, [r4, #1676]
	strh	r3, [r4, #1680]	@ movhi
	lsls	r2, r2, #1
	strh	r3, [r4, #1688]	@ movhi
	bl	ftl_memset
	ldrh	r3, [r5, #2914]
	movs	r2, #12
	ldr	r0, [r4, #1684]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r3, r4, r5, lr}
	b	FtlGcBufInit
.L1067:
	.align	2
.L1066:
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
	ldr	r1, .L1070
	bl	ftl_memcpy
	movs	r0, #0
	pop	{r3, pc}
.L1071:
	.align	2
.L1070:
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r4, #0
	ldr	r6, .L1084
.L1076:
	mov	r0, r6
	ldr	r1, .L1084+4
	ldrb	r2, [r6, #-1]	@ zero_extendqisi2
	bl	FlashMemCmp8
	mov	r5, r0
	cbnz	r0, .L1073
	ldr	r3, .L1084+8
	lsls	r4, r4, #5
	add	r2, r3, #508
	adds	r6, r2, r4
	beq	.L1079
	add	r4, r4, r3
	addw	r3, r3, #2876
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	mov	r4, r0
	b	.L1078
.L1073:
	adds	r4, r4, #1
	adds	r6, r6, #32
	cmp	r4, #74
	bne	.L1076
	b	.L1079
.L1083:
	adds	r4, r4, #1
	cmp	r4, #4
	beq	.L1077
.L1078:
	lsls	r1, r4, #5
	ldrb	r1, [r1, r3]	@ zero_extendqisi2
	cmp	r1, r2
	bne	.L1083
.L1077:
	ldr	r1, .L1084+12
	movs	r2, #32
	ldr	r0, .L1084+16
	add	r1, r1, r4, lsl #5
	bl	ftl_memcpy
	mov	r1, r6
	movs	r2, #32
	ldr	r0, .L1084+20
	bl	ftl_memcpy
	b	.L1074
.L1079:
	mov	r5, #-1
.L1074:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1085:
	.align	2
.L1084:
	.word	.LANCHOR1+509
	.word	.LANCHOR0+1792
	.word	.LANCHOR1
	.word	.LANCHOR1+2876
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
	bne	.L1087
	cbz	r3, .L1088
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1088:
	cbz	r6, .L1086
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
.L1087:
	cbz	r3, .L1091
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1091:
	cbz	r6, .L1086
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
.L1086:
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
.L1103:
	cmp	r3, r2
	beq	.L1105
	ldr	r4, [r1, r3, lsl #2]
	str	r4, [r0, r3, lsl #2]
	adds	r3, r3, #1
	b	.L1103
.L1105:
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
	.global	js_hash
	.thumb
	.thumb_func
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1111
	add	r1, r1, r0
	push	{r4, lr}
	.save {r4, lr}
.L1108:
	cmp	r0, r1
	beq	.L1110
	lsls	r2, r3, #5
	ldrb	r4, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, r4
	eors	r3, r3, r2
	b	.L1108
.L1110:
	mov	r0, r3
	pop	{r4, pc}
.L1112:
	.align	2
.L1111:
	.word	1204201446
	.fnend
	.size	js_hash, .-js_hash
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
	ldr	r3, .L1114
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1115:
	.align	2
.L1114:
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
	ldr	r4, .L1119
	ldr	r4, [r4, #3984]
	add	r4, r4, #4096
	add	r1, r1, r4
	cbz	r2, .L1118
	mov	r0, r1
	mov	r1, r5
.L1118:
	mov	r2, r3
	pop	{r4, r5, lr}
	b	ftl_memcpy
.L1120:
	.align	2
.L1119:
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
	ldr	r3, .L1125
	ldr	r0, [r3, #1896]
	bx	lr
.L1126:
	.align	2
.L1125:
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
.L1131:
	cmp	r4, r8
	beq	.L1139
	cbnz	r5, .L1132
	ldr	r0, .L1140
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1132:
	cmp	r7, #4
	ldr	r0, .L1140+4
	bne	.L1133
	ldr	r1, [r6, r4, lsl #2]
	b	.L1138
.L1133:
	cmp	r7, #2
	ite	eq
	ldrsheq	r1, [r6, r4, lsl #1]
	ldrbne	r1, [r6, r4]	@ zero_extendqisi2
.L1138:
	adds	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1136
	ldr	r0, .L1140+8
	movs	r5, #0
	ldr	r1, .L1140+12
	bl	printk
.L1136:
	adds	r4, r4, #1
	b	.L1131
.L1139:
	ldr	r0, .L1140+8
	ldr	r1, .L1140+12
	pop	{r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1141:
	.align	2
.L1140:
	.word	.LC83
	.word	.LC84
	.word	.LC77
	.word	.LC85
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
	ldr	r5, .L1234
	mov	r3, #172
	mov	r1, #173
	mov	r2, #174
	.pad #44
	sub	sp, sp, #44
	mov	r4, r0
	strb	r3, [r5, #1910]
	mov	r3, #175
	strb	r0, [r5, #1906]
	strb	r1, [r5, #1911]
	strb	r2, [r5, #1912]
	strb	r3, [r5, #1913]
	bne	.L1143
	movs	r3, #167
	strb	r3, [r5, #1910]
	ldr	r3, .L1234+4
	movs	r2, #247
	strb	r2, [r3, #3021]
	b	.L1196
.L1143:
	cmp	r0, #3
	bne	.L1145
	movs	r3, #176
	strb	r3, [r5, #1910]
	movs	r3, #177
	strb	r3, [r5, #1911]
	movs	r3, #178
	strb	r3, [r5, #1912]
	movs	r3, #179
	strb	r3, [r5, #1913]
	movs	r3, #180
	strb	r3, [r5, #1914]
	movs	r3, #181
	strb	r3, [r5, #1915]
	movs	r3, #182
	strb	r3, [r5, #1916]
	movs	r3, #183
	b	.L1229
.L1145:
	cmp	r0, #4
	bne	.L1146
	movs	r0, #204
	strb	r1, [r5, #1915]
	strb	r0, [r5, #1910]
	movs	r0, #191
	strb	r2, [r5, #1916]
	strb	r0, [r5, #1911]
	movs	r0, #170
	strb	r0, [r5, #1912]
	movs	r0, #171
	strb	r0, [r5, #1913]
	movs	r0, #205
	strb	r0, [r5, #1914]
.L1229:
	mov	fp, #8
	strb	r3, [r5, #1917]
	mov	r9, fp
	b	.L1144
.L1146:
	cmp	r0, #5
	bne	.L1147
	movs	r3, #56
	mov	fp, #8
	strb	r3, [r5, #1910]
	movs	r3, #57
	strb	r3, [r5, #1911]
	movs	r3, #58
	strb	r3, [r5, #1912]
	movs	r3, #59
	strb	r3, [r5, #1913]
	b	.L1228
.L1147:
	cmp	r0, #6
	bne	.L1148
	movs	r3, #14
	mov	fp, #12
	strb	r3, [r5, #1910]
	movs	r3, #15
	strb	r3, [r5, #1911]
	movs	r3, #16
	strb	r3, [r5, #1912]
	movs	r3, #17
	strb	r3, [r5, #1913]
	b	.L1228
.L1148:
	cmp	r0, #7
	bne	.L1196
	movs	r3, #176
	mov	fp, #12
	strb	r3, [r5, #1910]
	mov	r9, #10
	movs	r3, #177
	strb	r3, [r5, #1911]
	movs	r3, #178
	strb	r3, [r5, #1912]
	movs	r3, #179
	strb	r3, [r5, #1913]
	movs	r3, #180
	strb	r3, [r5, #1914]
	movs	r3, #181
	strb	r3, [r5, #1915]
	movs	r3, #182
	strb	r3, [r5, #1916]
	movs	r3, #183
	strb	r3, [r5, #1917]
	movs	r3, #212
	strb	r3, [r5, #1918]
	movs	r3, #213
	strb	r3, [r5, #1919]
	b	.L1144
.L1196:
	mov	fp, #7
.L1228:
	mov	r9, #4
.L1144:
	subs	r3, r4, #1
	cmp	r3, #1
	bhi	.L1225
	ldr	r7, .L1234+8
	mov	r8, #0
.L1149:
	ldrb	r3, [r5, #1616]	@ zero_extendqisi2
	uxtb	r6, r8
	ldr	r2, .L1234
	cmp	r3, r6
	bls	.L1156
	add	r6, r6, r2
	ldrb	r3, [r6, #1620]	@ zero_extendqisi2
	add	r4, r2, r3, lsl #6
	add	r3, r2, r3, lsl #3
	addw	r4, r4, #1926
	addw	r2, r2, #1909
	ldrb	r6, [r3, #16]	@ zero_extendqisi2
	ldr	r1, [r3, #12]
	mov	r10, r4
	mov	r3, r4
	add	r6, r1, r6, lsl #8
	addw	ip, r6, #2056
.L1151:
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
	bcc	.L1151
	movs	r2, #0
.L1152:
	movs	r3, #1
	adds	r0, r7, r2
.L1153:
	ldrb	r6, [r0, r3, lsl #2]	@ zero_extendqisi2
	ldrb	r1, [r10]	@ zero_extendqisi2
	add	r1, r1, r6
	strb	r1, [r10, r3, lsl #3]
	adds	r3, r3, #1
	cmp	r3, #7
	bne	.L1153
	adds	r2, r2, #1
	add	r10, r10, #1
	cmp	r2, #4
	bne	.L1152
	movs	r3, #0
	add	r8, r8, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1149
.L1225:
	subs	r3, r4, #3
	cmp	r3, #4
	bhi	.L1156
	smulbb	r2, r9, fp
	lsls	r3, r2, #4
	str	r3, [sp, #32]
	and	r3, r2, #252
	str	r3, [sp]
	movs	r3, #0
.L1232:
	str	r3, [sp, #12]
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	str	r3, [sp, #8]
	ldr	r3, .L1234
	ldr	r2, [sp, #8]
	ldrb	r3, [r3, #1616]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1195
.L1156:
	ldr	r3, .L1234
	strb	r9, [r3, #1907]
	strb	fp, [r3, #1908]
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1195:
	ldr	r2, [sp, #8]
	ldr	r3, .L1234
	add	r3, r3, r2
	movs	r2, #255
	ldrb	r8, [r3, #1620]	@ zero_extendqisi2
	ldr	r3, .L1234
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
	ldreq	r3, .L1234+12
	ldrne	r3, .L1234+12
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
	bne	.L1160
	add	r6, r6, r10
	movs	r3, #255
	str	r3, [r6, #2052]
	movs	r3, #64
	str	r3, [r6, #2048]
	movs	r3, #204
	b	.L1231
.L1160:
	subs	r2, r4, #5
	cmp	r2, #1
	bhi	.L1162
	ldr	r2, .L1234
	ldrb	r2, [r2, #1910]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	movs	r2, #82
	str	r2, [r3, #2048]
	b	.L1161
.L1162:
	add	r6, r6, r10
	movs	r3, #174
	str	r3, [r6, #2052]
	movs	r3, #0
	str	r3, [r6, #2048]
	movs	r3, #176
.L1231:
	str	r3, [r6, #2052]
	movs	r3, #77
	str	r3, [r6, #2048]
.L1161:
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
	bls	.L1198
	cmp	r4, #7
	ite	eq
	moveq	r0, #32
	movne	r0, #2
	b	.L1166
.L1235:
	.align	2
.L1234:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3004
	.word	.LANCHOR0+1906
.L1198:
	movs	r0, #16
.L1166:
	ldr	r3, .L1236
	ldr	r3, [r3, #3988]
	mov	r1, r3
.L1167:
	ldr	r2, [r5, #2048]
	strb	r2, [r1], #1
	subs	r2, r1, r3
	uxtb	r2, r2
	cmp	r2, r0
	bcc	.L1167
	cmp	r4, #7
	bne	.L1168
	movs	r2, #0
.L1170:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #12
	beq	.L1169
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #10
	beq	.L1169
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1170
	b	.L1171
.L1169:
	cmp	r1, #7
	bne	.L1172
.L1171:
	ldr	r0, .L1236+4
	movs	r1, #0
	bl	printk
.L1173:
	b	.L1173
.L1168:
	cmp	r4, #6
	bne	.L1172
	subs	r2, r3, #1
	adds	r3, r3, #7
.L1174:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1172
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1172
	cmp	r2, r3
	bne	.L1174
	ldr	r0, .L1236+4
	movs	r1, #0
	bl	printk
.L1176:
	b	.L1176
.L1172:
	ldr	r1, .L1236
	ldr	r0, [sp, #32]
	ldr	r2, [r1, #3988]
	adds	r0, r2, r0
	mov	r3, r2
.L1177:
	cmp	r3, r0
	beq	.L1233
	ldr	r6, [r5, #2048]
	strb	r6, [r3], #1
	b	.L1177
.L1233:
	ldr	r3, [sp]
	ldr	r0, [r1, #3988]
	add	r0, r0, r3
	movs	r3, #8
.L1180:
	ldr	r1, [sp]
	adds	r6, r0, r1
.L1179:
	ldr	r1, [r0]
	mvns	r1, r1
	str	r1, [r0], #4
	cmp	r0, r6
	bne	.L1179
	ldr	r1, [sp]
	subs	r3, r3, #1
	add	r0, r0, r1
	bne	.L1180
.L1181:
	ldr	r1, .L1236
	movs	r0, #0
	ldr	r1, [r1, #3988]
	str	r1, [sp, #24]
	mov	r1, r0
	ldr	r6, [sp, #24]
	add	r6, r6, r3
	str	r6, [sp, #36]
.L1184:
	movs	r6, #1
	movs	r7, #16
	str	r7, [sp, #28]
	lsl	lr, r6, r1
	movs	r6, #0
	mov	ip, r6
.L1182:
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
	bne	.L1182
	cmp	ip, #8
	add	r1, r1, #1
	it	hi
	orrhi	r0, r0, lr
	cmp	r1, #32
	bne	.L1184
	ldr	r1, [sp, #24]
	str	r0, [r1, r3]
	adds	r3, r3, #4
	ldr	r1, [sp]
	cmp	r3, r1
	bne	.L1181
	ldr	r3, .L1236
	ldr	r1, [r3, #3988]
	movs	r3, #0
	subs	r0, r1, #4
	add	r6, r1, #28
.L1186:
	ldr	r7, [r0, #4]!
	cmp	r7, #0
	it	eq
	addeq	r3, r3, #1
	cmp	r0, r6
	bne	.L1186
	cmp	r3, #7
	ble	.L1187
	ldr	r0, .L1236+8
	movs	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1236+4
	movs	r1, #0
	bl	printk
.L1188:
	b	.L1188
.L1187:
	cmp	r4, #6
	beq	.L1200
	cmp	r4, #7
	ite	ne
	movne	ip, #8
	moveq	ip, #10
	b	.L1189
.L1200:
	mov	ip, #4
.L1189:
	add	r7, r9, #-1
	ldr	r1, [sp, #16]
	movs	r0, #0
	uxtb	r7, r7
	adds	r7, r7, #1
.L1190:
	mov	lr, r1
	mov	r3, r2
.L1191:
	ldrb	r6, [r3], #1	@ zero_extendqisi2
	strb	r6, [lr], #1
	subs	r6, r3, r2
	uxtb	r6, r6
	cmp	r6, r9
	bcc	.L1191
	adds	r0, r0, #1
	add	r2, r2, r7
	cmp	r0, fp
	add	r1, r1, ip
	blt	.L1190
	ldr	r3, [sp, #4]
	mov	r0, r8
	add	r6, r10, r3, lsl #8
	movs	r3, #255
	str	r3, [r6, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #20]
	cmp	r3, #1
	bhi	.L1193
	movs	r3, #54
	str	r3, [r6, #2056]
	ldr	r3, .L1236+12
	mov	r1, #-1
	ldr	r0, [sp, #8]
	ldrb	r3, [r3, #1910]	@ zero_extendqisi2
	str	r3, [r5, #2052]
	movs	r3, #0
	str	r3, [r5, #2048]
	movs	r3, #22
	str	r3, [r6, #2056]
	bl	FlashReadCmd
	b	.L1194
.L1193:
	movs	r3, #56
	str	r3, [r6, #2056]
.L1194:
	mov	r0, r8
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	b	.L1232
.L1237:
	.align	2
.L1236:
	.word	.LANCHOR2
	.word	.LC86
	.word	.LC87
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
	beq	.L1238
	subs	r2, r0, #1
	cmp	r2, #6
	bhi	.L1240
	b	HynixGetReadRetryDefault
.L1240:
	cmp	r0, #49
	bne	.L1241
	ldr	r0, .L1261
	movs	r2, #64
	ldr	r1, .L1261+4
	addw	r0, r0, #1910
	strb	r3, [r0, #-4]
	movs	r3, #4
	strb	r3, [r0, #-3]
	movs	r3, #15
	strb	r3, [r0, #-2]
	b	.L1259
.L1241:
	cmp	r0, #33
	beq	.L1242
	sub	r2, r0, #65
	cmp	r2, #1
	bhi	.L1243
.L1242:
	ldr	r0, .L1261
	strb	r3, [r0, #1906]
	movs	r3, #4
	b	.L1260
.L1243:
	cmp	r0, #34
	beq	.L1244
	cmp	r0, #67
	bne	.L1245
.L1244:
	ldr	r0, .L1261
	strb	r3, [r0, #1906]
	movs	r3, #5
.L1260:
	strb	r3, [r0, #1907]
	movs	r2, #45
	movs	r3, #7
	addw	r0, r0, #1910
	strb	r3, [r0, #-2]
	ldr	r1, .L1261+8
	b	.L1259
.L1245:
	cmp	r0, #35
	beq	.L1246
	cmp	r0, #68
	bne	.L1238
.L1246:
	ldr	r0, .L1261
	movs	r2, #95
	ldr	r1, .L1261+12
	addw	r0, r0, #1910
	strb	r3, [r0, #-4]
	movs	r3, #5
	strb	r3, [r0, #-3]
	movs	r3, #17
	strb	r3, [r0, #-2]
.L1259:
	b	ftl_memcpy
.L1238:
	bx	lr
.L1262:
	.align	2
.L1261:
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
	ldr	r5, .L1302
	add	r0, r5, r0, lsl #3
	ldr	r3, [r5, #2780]
	ldr	r4, [r0, #12]
	cmp	r3, #3
	bls	.L1294
	ldr	r3, [r4, #16]
	lsls	r6, r3, #29
	bpl	.L1294
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	str	r3, [sp]
	cmp	r6, #0
	beq	.L1276
	movs	r6, #0
.L1266:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1274
	ldr	r3, [r5, #2780]
	cmp	r3, #5
	bhi	.L1267
.L1270:
	adds	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1266
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1302+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1302+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1266
.L1267:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r0, r3, #18
	bpl	.L1270
	ldr	r3, [sp, #4]
	lsls	r1, r3, #14
	bpl	.L1270
.L1274:
	ldr	r3, [r5, #2812]
	ldr	r4, .L1302
	cmp	r3, #0
	beq	.L1275
	ldr	r1, [sp]
	movs	r2, #0
	ldr	r0, [r4, #2804]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	ldr	r0, [r4, #2808]
	movs	r2, #0
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
	b	.L1275
.L1278:
	ldr	r3, [r4, #8]
	adds	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cbnz	r3, .L1276
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1302+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1302+8
	mov	r1, r4
	movs	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1276:
	ldr	r3, [sp]
	lsls	r2, r3, #11
	bpl	.L1278
	ldr	r3, [r5, #2820]
	cbz	r3, .L1279
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1279:
	ldr	r3, [r5, #2812]
	ldr	r6, .L1302
	cbz	r3, .L1280
	ldr	r1, [sp]
	movs	r2, #1
	ldr	r0, [r6, #2804]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	ldr	r0, [r6, #2808]
	movs	r2, #1
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
.L1280:
	ldr	r3, [r5, #2820]
	cbz	r3, .L1275
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1275:
	movs	r3, #0
	str	r3, [r5, #2812]
	b	.L1263
.L1294:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	lsls	r3, r3, #11
	bpl	.L1294
.L1263:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, pc}
.L1303:
	.align	2
.L1302:
	.word	.LANCHOR0
	.word	.LC88
	.word	.LC89
	.word	.LC90
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
	ldr	r5, .L1348
	tst	r9, #63
	.pad #92
	sub	sp, sp, #92
	mov	r8, r0
	add	r3, r5, r0, lsl #3
	mov	r10, r1
	mov	r6, r2
	ldr	r7, [r3, #12]
	bne	.L1305
	ldr	r3, [sp, #128]
	cbnz	r3, .L1306
	add	r0, sp, #24
	movs	r1, #255
	movs	r2, #64
	bl	memset
	add	r3, sp, #24
	str	r3, [sp, #128]
.L1306:
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
	bne	.L1332
	ldr	r3, [r5, #2824]
	lsrs	r4, r6, #1
	mov	r2, r10
	mov	r1, r10
	cmp	r3, #25
	ldr	r3, [sp, #128]
	ite	cc
	movcc	lr, #64
	movcs	lr, #128
.L1309:
	cmp	r1, r4
	add	r3, r3, #4
	add	ip, r2, lr
	bcs	.L1346
	ldr	r0, [r5, #2792]
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
	b	.L1309
.L1346:
	movs	r2, #0
	ldr	r0, [r5, #2824]
	ldr	r1, [r5, #2780]
	lsrs	r6, r6, #2
	mov	fp, r2
.L1311:
	cmp	r2, r6
	bcs	.L1307
	cmp	r0, #0
	beq	.L1307
	add	r3, r2, #8
	ldr	r3, [r7, r3, lsl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	lsls	r4, r3, #29
	bmi	.L1335
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1335
	cmp	r1, #5
	bls	.L1313
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
	b	.L1345
.L1313:
	cmp	r1, #3
	bls	.L1315
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
.L1345:
	orr	r4, r3, r4, lsl #5
.L1315:
	cmp	fp, r4
	it	cc
	movcc	fp, r4
	b	.L1312
.L1335:
	mov	fp, #-1
.L1312:
	adds	r2, r2, #1
	b	.L1311
.L1332:
	mov	fp, #0
.L1307:
	movs	r3, #0
	str	r3, [r7, #16]
	b	.L1318
.L1305:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1344
	mov	fp, r4
.L1319:
	cmp	r4, r6
	bcs	.L1347
	and	ip, r4, #3
	cmp	r9, #0
	beq	.L1338
	add	r3, r9, r4, lsl #9
	b	.L1321
.L1338:
	mov	r3, r9
.L1321:
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
	ldr	ip, [sp, #8]
	mov	r0, r8
	movs	r1, #1
	movs	r2, #2
	str	fp, [sp]
	mov	r3, ip
	str	fp, [sp, #4]
	bl	NandcXferStart
	mov	r0, r8
	bl	NandcXferComp
	b	.L1319
.L1347:
	mov	fp, #0
	b	.L1318
.L1344:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	movs	r2, #2
	mov	r3, r4
	mov	fp, r4
	bl	NandcXferStart
	str	r9, [sp, #8]
.L1324:
	cmp	r4, r6
	bcs	.L1318
	mov	r0, r8
	bl	NandcXferComp
	ldr	r3, [r7, #32]
	add	ip, r4, #2
	cmp	ip, r6
	str	r3, [sp, #20]
	bcs	.L1325
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
.L1325:
	ldr	r3, [sp, #20]
	lsls	r3, r3, #29
	bmi	.L1340
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, lsl #5
	cmp	fp, r3
	it	cc
	movcc	fp, r3
	b	.L1326
.L1340:
	mov	fp, #-1
.L1326:
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
	b	.L1324
.L1318:
	ldr	r3, [r5, #2780]
	cmp	r3, #5
	bls	.L1330
	cmp	r10, #0
	bne	.L1330
	ldr	r3, [r7]
	and	r2, r3, #139264
	cmp	r2, #139264
	ittt	eq
	moveq	fp, #-1
	orreq	r3, r3, #131072
	streq	r3, [r7]
.L1330:
	mov	r0, fp
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1349:
	.align	2
.L1348:
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
	ldr	r3, .L1352
	mov	r6, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r5, [r3, #265]	@ zero_extendqisi2
	cbnz	r0, .L1351
	ldr	r2, .L1352+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	muls	r2, r3, r2
	cmp	r1, r2
	it	cc
	movcc	r5, #4
.L1351:
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
	str	r8, [sp]
	mov	r0, r4
	bl	NandcXferData
	mov	r5, r0
	mov	r0, r4
	bl	NandcFlashDeCs
	mov	r0, r5
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1353:
	.align	2
.L1352:
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
	ldr	r4, .L1380
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #1828]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #2780]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	ite	cc
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	beq	.L1368
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	mov	r0, r10
	bl	FlashReset
	mov	r3, r7
	ldr	r1, [sp]
	mov	r2, fp
	mov	r0, r10
	bl	FlashReadRawPage
	mov	r6, r0
	ldrb	r0, [r4, #2764]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2764]	@ zero_extendqisi2
	bl	NandcSetMode
	adds	r3, r6, #1
	bne	.L1357
.L1366:
	mov	r6, #-1
	b	.L1358
.L1357:
	mov	r2, r6
	ldr	r0, .L1380+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	itt	ls
	addls	r4, r4, r10, lsl #3
	ldrls	r3, [r4, #12]
	ldr	r4, .L1380+8
	itttt	ls
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #3992]
	adds	r3, r3, #1
	str	r3, [r4, #3992]
	cmp	r3, #2048
	bcc	.L1358
	movs	r7, #0
	str	r7, [r4, #3992]
	mov	fp, r7
	b	.L1356
.L1368:
	mov	r6, #1024
.L1356:
	movs	r4, #0
	mov	r8, #-1
	str	r4, [sp, #4]
	mov	ip, r4
	mov	r5, r4
.L1364:
	uxtb	r0, r9
	str	ip, [sp, #12]
	bl	NandcSetDdrPara
	mov	r3, r7
	mov	r0, r10
	ldr	r1, [sp]
	mov	r2, fp
	bl	FlashReadRawPage
	adds	r3, r6, #1
	ldr	ip, [sp, #12]
	cmp	r0, r3
	bhi	.L1360
	cmp	r0, #2
	bhi	.L1370
	adds	r5, r5, #1
	cmp	r5, #9
	bls	.L1370
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1362
.L1360:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1371
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1372
	str	r5, [sp, #4]
	b	.L1371
.L1370:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1361
.L1371:
	movs	r5, #0
.L1361:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1364
.L1362:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	it	cs
	movcs	r4, ip
	b	.L1363
.L1372:
	mov	r4, ip
.L1363:
	cbz	r4, .L1365
	ldr	r0, .L1380+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1365:
	cmp	r8, #0
	beq	.L1358
	ldr	r0, .L1380+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1366
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1358:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1381:
	.align	2
.L1380:
	.word	.LANCHOR0
	.word	.LC91
	.word	.LANCHOR2
	.word	.LC92
	.word	.LC93
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
	bne	.L1383
	ldr	r9, .L1402+4
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1384
.L1386:
	ldrb	r3, [r10, #2772]	@ zero_extendqisi2
	ldr	r9, .L1402+4
	cbz	r3, .L1383
	b	.L1401
.L1384:
	movs	r3, #0
	mov	r0, r5
	strb	r3, [r9, #8]
	mov	r1, r6
	mov	r3, r7
	mov	r2, r8
	bl	FlashReadRawPage
	strb	fp, [r9, #8]
	adds	r3, r0, #1
	beq	.L1386
	mov	r4, r0
	b	.L1383
.L1401:
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
	beq	.L1387
	ldrb	r3, [r9, #1892]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1383
.L1387:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1383:
	ldr	r9, .L1402+8
	ldr	ip, [r9, #3996]
	cmp	ip, #0
	beq	.L1388
	adds	r2, r4, #1
	bne	.L1388
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r3, r6
	mov	r2, r5
	mov	r4, r0
	ldr	r0, .L1402
	mov	r1, r4
	bl	printk
	adds	r3, r4, #1
	bne	.L1388
	ldr	r3, .L1402+4
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	cbz	r3, .L1388
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
.L1388:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1403:
	.align	2
.L1402:
	.word	.LC94
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
	movs	r5, #0
	ldr	r4, .L1414
	mov	r7, r0
	mov	r6, r1
	ldrb	r0, [r4, #2764]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2764]	@ zero_extendqisi2
	bl	NandcSetMode
	mov	r1, r6
	mov	r2, r5
	mov	r3, r5
	str	r5, [sp]
	mov	r0, r7
	bl	FlashDdrTunningRead
	mov	r1, r6
	mov	r2, r5
	mov	r3, r5
	mov	r8, r0
	mov	r0, r7
	bl	FlashReadRawPage
	adds	r0, r0, #1
	beq	.L1405
	cmp	r8, #-1
	bne	.L1406
.L1405:
	ldrb	r3, [r4, #2764]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L1406
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1414
	movs	r2, #0
	strb	r2, [r3, #2772]
	b	.L1407
.L1406:
	movs	r3, #1
	strb	r3, [r4, #2772]
.L1407:
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1415:
	.align	2
.L1414:
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
	ldr	r6, .L1432
	movs	r4, #0
	ldr	r3, .L1432+4
	mov	r9, #4
	ldr	r5, .L1432+8
	mov	r7, #-1
	mov	r8, r6
	ldr	r0, [r3]	@ unaligned
	ldrh	r3, [r6, #266]
	mov	r6, r5
	str	r4, [r5, #4004]
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #3988]
	str	r3, [r5, #4000]
	bl	flash_enter_slc_mode
.L1417:
	add	fp, r4, #1
	mov	r10, #0
.L1419:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #3988]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1418
	movs	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #3988]
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1418
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1420
	b	.L1419
.L1421:
	add	r0, fp, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	bne	.L1427
	add	r1, fp, #160
	movs	r2, #32
	ldr	r0, .L1432+12
	bl	ftl_memcpy
	ldr	r7, .L1432+16
	ldr	r1, [r6, #4000]
	movs	r2, #32
	add	r0, r7, #44
	adds	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #4000]
	mov	r2, #852
	addw	r0, r7, #1906
	adds	r1, r1, #224
	bl	ftl_memcpy
	ldr	fp, [r6, #4000]
	str	r4, [r6, #4004]
	mov	r0, r4
	ldrh	r1, [r8, #266]
	ldr	r3, [fp, #1076]
	strb	r3, [r7, #2772]
	bl	__aeabi_uidiv
	adds	r3, r0, #1
	cbnz	r0, .L1430
	movs	r3, #2
.L1430:
	str	r3, [r6, #4008]
	movs	r7, #0
	ldrh	r3, [fp, #14]
	strb	r3, [r5, #4012]
.L1420:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1417
	mov	r0, r9
.L1431:
	bl	flash_exit_slc_mode
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1418:
	ldr	fp, [r5, #4000]
	ldr	r2, .L1432+20
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1420
	cmp	r7, #0
	bne	.L1421
	mov	r0, r4
	ldrh	r1, [r8, #266]
	bl	__aeabi_uidiv
	ldr	r3, .L1432+8
	adds	r0, r0, #1
	str	r0, [r3, #4008]
	mov	r0, r7
	b	.L1431
.L1427:
	mov	r7, #-1
	b	.L1420
.L1433:
	.align	2
.L1432:
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
	ldr	r4, .L1462
	mov	r9, r0
	str	r2, [sp, #8]
	mov	fp, r3
	str	r1, [sp, #16]
	bl	NandcWaitFlashReady
	add	r3, r4, r9, lsl #3
	ldrb	r2, [r3, #16]	@ zero_extendqisi2
	ldr	r7, [r3, #12]
	ldrb	r3, [r4, #1904]	@ zero_extendqisi2
	add	r6, r2, #8
	subs	r3, r3, #67
	add	r6, r7, r6, lsl #8
	cmp	r3, #1
	lsl	r3, r2, #8
	str	r3, [sp, #12]
	bls	.L1451
	ldrb	r5, [r4, #2772]	@ zero_extendqisi2
	cbz	r5, .L1452
	movs	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	movs	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
	b	.L1436
.L1452:
	str	r5, [sp, #4]
.L1436:
	ldr	r3, [sp, #12]
	movs	r1, #92
	adds	r3, r7, r3
	str	r1, [r3, #2056]
	movs	r1, #197
	str	r1, [r3, #2056]
	b	.L1435
.L1451:
	movs	r3, #0
	str	r3, [sp, #4]
.L1435:
	mov	r8, #1
	mov	r10, #-1
	lsls	r3, r2, #8
	str	r3, [sp, #20]
.L1437:
	ldr	r3, .L1462+4
	ldrb	r3, [r3, #4013]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r8, r3
	bcs	.L1461
	ldrb	r3, [r4, #1904]	@ zero_extendqisi2
	mov	r0, r6
	uxtb	r1, r8
	subs	r3, r3, #67
	cmp	r3, #1
	bhi	.L1438
	bl	SandiskSetRRPara
	b	.L1439
.L1438:
	bl	ToshibaSetRRPara
.L1439:
	ldrb	r3, [r4, #1904]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1440
	ldr	r3, .L1462+4
	ldrb	r3, [r3, #4013]	@ zero_extendqisi2
	subs	r3, r3, #3
	cmp	r8, r3
	itttt	eq
	moveq	r2, #179
	ldreq	r3, [sp, #20]
	addeq	r3, r7, r3
	streq	r2, [r3, #2056]
.L1440:
	ldr	r3, [sp, #12]
	movs	r2, #38
	adds	r3, r7, r3
	str	r2, [r3, #2056]
	movs	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cbz	r3, .L1441
	movs	r0, #4
	bl	NandcSetDdrMode
	mov	r0, r9
	ldr	r1, [sp, #16]
	mov	r3, fp
	ldr	r2, [sp, #8]
	bl	FlashReadRawPage
	mov	r5, r0
	movs	r0, #0
	bl	NandcSetDdrMode
	b	.L1442
.L1441:
	mov	r0, r9
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1442:
	adds	r2, r5, #1
	beq	.L1443
	ldrb	r2, [r4, #1892]	@ zero_extendqisi2
	cmp	r10, #-1
	it	eq
	moveq	r10, r5
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1445
	mov	fp, #0
	str	fp, [sp, #8]
.L1443:
	add	r8, r8, #1
	b	.L1437
.L1461:
	mov	r5, r10
.L1445:
	ldrb	r2, [r4, #1904]	@ zero_extendqisi2
	mov	r0, r6
	movs	r1, #0
	subs	r2, r2, #67
	cmp	r2, #1
	bhi	.L1447
	bl	SandiskSetRRPara
	b	.L1448
.L1447:
	bl	ToshibaSetRRPara
.L1448:
	ldr	r3, [sp, #12]
	movs	r2, #255
	add	r7, r7, r3
	str	r2, [r7, #2056]
	ldrb	r2, [r4, #1892]	@ zero_extendqisi2
	add	r2, r2, r2, lsl #1
	cmp	r5, r2, asr #2
	bcc	.L1449
	adds	r3, r5, #1
	it	ne
	movne	r5, #256
.L1449:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cbz	r3, .L1450
	movs	r0, #4
	bl	NandcSetDdrMode
.L1450:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1463:
	.align	2
.L1462:
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
	ldr	r5, .L1477
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
.L1465:
	ldr	r3, .L1477+4
	ldrb	r3, [r3, #4013]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r10, r3
	bcs	.L1468
	mov	r0, r6
	uxtb	r1, r10
	bl	SamsungSetRRPara
	mov	r2, r9
	mov	r0, r8
	mov	r1, fp
	mov	r3, r7
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1466
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1471
	movs	r7, #0
	mov	r9, r7
.L1466:
	add	r10, r10, #1
	b	.L1465
.L1471:
	mov	r4, r0
.L1468:
	mov	r0, r6
	movs	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L1470
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
.L1470:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1478:
	.align	2
.L1477:
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
	ldr	r3, .L1495
	mov	fp, r2
	.pad #28
	sub	sp, sp, #28
	mov	r8, r0
	mov	r10, r1
	ldrb	r2, [r3, #76]	@ zero_extendqisi2
	mov	r5, r3
	ldrb	r4, [r3, #1892]	@ zero_extendqisi2
	cbnz	r2, .L1480
	add	r4, r4, r4, lsl #1
	ubfx	r4, r4, #2, #8
	b	.L1481
.L1480:
	ldr	r2, .L1495+4
	smull	r2, r3, r4, r2
	uxtb	r4, r3
.L1481:
	mov	r0, r8
	movs	r7, #0
	bl	NandcWaitFlashReady
	add	r3, r5, r8, lsl #3
	mov	r5, #-1
	ldr	r2, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	str	r2, [sp, #12]
	str	r3, [sp, #16]
	mov	r3, r2
	ldr	r2, [sp, #16]
	add	r6, r3, r2, lsl #8
.L1482:
	ldr	r3, .L1495+8
	ldrb	r3, [r3, #4013]	@ zero_extendqisi2
	cmp	r7, r3
	bcs	.L1485
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
	str	r3, [sp, #8]
	mov	r3, r9
	str	ip, [r6, #2048]
	str	ip, [r6, #2048]
	str	ip, [sp, #20]
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1483
	cmp	r5, #-1
	it	eq
	moveq	r5, r0
	cmp	r0, r4
	bcc	.L1489
	ldr	ip, [sp, #20]
	mov	r9, ip
	mov	fp, ip
.L1483:
	ldr	r7, [sp, #8]
	b	.L1482
.L1489:
	mov	r5, r0
.L1485:
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
	bcc	.L1487
	adds	r3, r5, #1
	ldr	r0, .L1495+12
	it	ne
	movne	r5, #256
	mov	r1, r7
	str	r5, [sp]
	mov	r2, r10
	mov	r3, r7
	bl	printk
.L1487:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1496:
	.align	2
.L1495:
	.word	.LANCHOR0
	.word	1431655766
	.word	.LANCHOR2
	.word	.LC95
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
	ldr	r5, .L1514
	mov	r8, r3
	mov	r7, r0
	mov	fp, r1
	adds	r3, r5, r0
	mov	r6, #-1
	ldr	r2, [r5, #1788]
	ldrb	r4, [r3, #1918]	@ zero_extendqisi2
	ldrb	r9, [r5, #1908]	@ zero_extendqisi2
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	cmp	r2, #7
	it	eq
	ldrbeq	r4, [r3, #1926]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L1499:
	cmp	ip, r9
	bcs	.L1503
	adds	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #1907]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1514+4
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
	ldr	ip, [sp, #4]
	adds	r2, r0, #1
	beq	.L1501
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	cmp	r6, #-1
	it	eq
	moveq	r6, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1508
	mov	r8, #0
	mov	r10, r8
.L1501:
	add	ip, ip, #1
	b	.L1499
.L1508:
	mov	r6, r0
.L1503:
	ldr	r3, [r5, #1788]
	add	r7, r7, r5
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #7
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	ite	eq
	strbeq	r4, [r7, #1926]
	strbne	r4, [r7, #1918]
	add	r3, r3, r3, lsl #1
	cmp	r6, r3, asr #2
	bcc	.L1507
	adds	r3, r6, #1
	it	ne
	movne	r6, #256
.L1507:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1515:
	.align	2
.L1514:
	.word	.LANCHOR0
	.word	.LANCHOR0+1910
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
	ldr	r3, .L1519
	mov	r5, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r6, [r3, #265]	@ zero_extendqisi2
	cbnz	r0, .L1517
	ldr	r2, .L1519+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	muls	r1, r3, r1
	cmp	r5, r1
	bcs	.L1517
	ldrb	r3, [r2]	@ zero_extendqisi2
	cbnz	r3, .L1518
	subs	r6, r6, #2
	b	.L1517
.L1518:
	movs	r6, #4
.L1517:
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r0, r4
	mov	r1, r5
	bl	FlashProgFirstCmd
	uxtb	r2, r6
	mov	r3, r7
	str	r8, [sp]
	mov	r0, r4
	movs	r1, #1
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
.L1520:
	.align	2
.L1519:
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
	ldr	r4, .L1530
	ldr	r5, .L1530+4
	ldr	r8, .L1530+12
	ldr	r3, [r4, #3988]
	mov	r9, r4
	ldrb	r0, [r4, #4014]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #4000]
	bl	FlashBchSel
	ldr	r0, [r4, #3988]
	mov	r1, #2048
	bl	__memzero
	ldr	r3, [r4, #4000]
	add	r1, r5, #1792
	movs	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #4000]
	ldrb	r3, [r5, #1616]	@ zero_extendqisi2
	adds	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #2772]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #4000]
	addw	r1, r5, #1620
	movs	r2, #8
	adds	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #4000]
	addw	r1, r5, #1628
	movs	r2, #32
	adds	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #4000]
	ldr	r1, .L1530+8
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
	addw	r1, r5, #1906
	adds	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #4000]
	movw	r1, #2036
	add	r0, r6, #12
	bl	JSHash
	mov	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #4016]
	str	r3, [r4, #4000]
	str	r0, [r6, #8]
	movs	r0, #0
	bl	flash_enter_slc_mode
	movs	r6, #0
	mov	r7, r6
.L1524:
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
	beq	.L1522
	ldr	fp, [r9, #4000]
	ldr	r3, [fp]
	cmp	r3, r8
	bne	.L1522
	add	r0, fp, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	JSHash
	ldr	r3, [fp, #8]
	ldr	r2, [sp, #4]
	cmp	r3, r0
	bne	.L1522
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [r9, #4008]
	mul	r3, r3, r7
	str	r3, [r9, #4004]
	beq	.L1525
	movs	r6, #1
.L1522:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1524
	b	.L1523
.L1525:
	movs	r6, #2
.L1523:
	movs	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	lsrs	r0, r0, #5
	negs	r0, r0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1531:
	.align	2
.L1530:
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
	ldr	r3, .L1545
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1545+4
	ldr	r0, [r3]	@ unaligned
	ldr	r2, [r4, #2768]
	ldr	r3, .L1545+8
	str	r0, [sp, #12]	@ unaligned
	cmp	r2, r3
	ldrb	r10, [r4, #1892]	@ zero_extendqisi2
	str	r3, [sp, #4]
	bne	.L1533
	movs	r0, #0
	bl	flash_enter_slc_mode
.L1533:
	mov	r0, r9
	mov	r1, #2048
	bl	__memzero
	mov	r8, #-1
	movs	r5, #2
.L1534:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1538
	movs	r7, #0
.L1536:
	add	r3, sp, #12
	ldr	r6, .L1545+12
	ldrb	fp, [r7, r3]	@ zero_extendqisi2
	mov	r0, fp
	bl	FlashBchSel
	ldr	r1, [r4, #4]
	movs	r0, #0
	ldr	r2, [r6, #3988]
	muls	r1, r5, r1
	mov	r3, r0
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1535
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L1536
	b	.L1537
.L1535:
	ldr	r3, [r6, #3988]
	ldr	r2, .L1545+16
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1537
	mov	r1, fp
	ldr	r0, .L1545+20
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #3988]
	bl	ftl_memcpy
	ldr	r3, [r6, #3988]
	ldr	r2, .L1545+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r6, #4008]
	cmp	r3, r5
	bls	.L1541
	str	r5, [r6, #4008]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1537:
	adds	r5, r5, #1
	b	.L1534
.L1541:
	mov	r8, #0
.L1538:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #2768]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bne	.L1543
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1543:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1546:
	.align	2
.L1545:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	1446522928
	.word	.LANCHOR2
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	.pad #28
	sub	sp, sp, #28
	mov	r0, #32768
	bl	ftl_malloc
	ldr	r5, .L1645
	ldr	r4, .L1645+4
	movs	r7, #0
	add	r9, r4, #12
	mov	r10, r4
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
	strb	r7, [r4, #2772]
	str	r3, [r4, #4]
	movs	r3, #60
	str	r7, [r5, #3992]
	strb	r7, [r4]
	strb	r3, [r5, #4014]
	str	r0, [r5, #4028]
	mov	r0, r6
	bl	NandcInit
	add	r6, r4, #1792
.L1552:
	uxtb	fp, r7
	mov	r0, fp
	bl	FlashReset
	ldr	r2, [r9, r7, lsl #3]
	add	r3, r9, r7, lsl #3
	mov	r0, fp
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	str	r2, [sp, #20]
	bl	NandcFlashCs
	ldr	r2, [sp, #20]
	movs	r3, #144
	movs	r0, #200
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
	bhi	.L1548
	ldrb	r1, [r6, #2]	@ zero_extendqisi2
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	ldr	r0, .L1645+8
	str	r1, [sp]
	ldrb	r1, [r6, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r6, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	adds	r1, r7, #1
	bl	printk
.L1548:
	cbnz	r7, .L1549
	ldrb	r3, [r4, #1792]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1595
	ldrb	r3, [r10, #1793]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1595
.L1549:
	ldrb	r3, [r6]	@ zero_extendqisi2
	adds	r7, r7, #1
	adds	r6, r6, #8
	cmp	r3, #181
	itt	eq
	moveq	r3, #44
	strbeq	r3, [r6, #-8]
	cmp	r7, #4
	bne	.L1552
	ldrb	r3, [r4, #1792]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1553
	ldr	r3, .L1645+4
	ldr	r0, [r3, #2776]
	bl	NandcSetDdrMode
.L1553:
	ldr	r6, .L1645+4
	mov	r1, #852
	addw	r0, r6, #1906
	bl	__memzero
	ldr	r3, .L1645+12
	str	r3, [r4, #1788]
	movs	r3, #0
	strb	r3, [r4, #8]
	ldrb	r3, [r4, #1793]	@ zero_extendqisi2
	cmp	r3, #161
	beq	.L1554
	cmp	r3, #218
	beq	.L1554
	and	r2, r3, #223
	cmp	r2, #209
	beq	.L1554
	cmp	r3, #220
	bne	.L1555
	ldrb	r2, [r6, #1795]	@ zero_extendqisi2
	cmp	r2, #149
	bne	.L1555
.L1554:
	ldrb	r1, [r4, #1792]	@ zero_extendqisi2
	movs	r2, #1
	strb	r2, [r4]
	movs	r2, #16
	strb	r2, [r4, #1]
	cmp	r1, #152
	strb	r2, [r5, #4014]
	ldr	r2, .L1645+16
	strb	r1, [r2, #3033]
	strb	r3, [r2, #3034]
	bne	.L1556
	ldr	r1, .L1645+4
	ldrsb	r1, [r1, #1796]
	cmp	r1, #0
	ittt	ge
	movge	r0, #24
	ldrge	r1, .L1645
	strbge	r0, [r1, #4014]
.L1556:
	ldr	r1, .L1645+20
	ldr	r0, [r4, #2768]
	cmp	r0, r1
	itt	eq
	moveq	r1, #16
	strbeq	r1, [r5, #4014]
	cmp	r3, #218
	bne	.L1558
	mov	r1, #2048
	b	.L1641
.L1558:
	cmp	r3, #220
	bne	.L1559
	mov	r1, #4096
.L1641:
	strh	r1, [r2, #3046]	@ movhi
	strb	r3, [r2, #3034]
.L1559:
	ldr	r1, .L1645+24
	movs	r2, #32
	ldr	r0, .L1645+28
	bl	ftl_memcpy
	ldr	r0, .L1645+12
	movs	r2, #32
	addw	r1, r0, #2776
	bl	ftl_memcpy
.L1555:
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r6, .L1645+4
	cmp	r3, #0
	bne	.L1560
	bl	FlashLoadPhyInfoInRam
	cbnz	r0, .L1562
	ldr	r3, [r6, #1788]
	ldrh	r3, [r3, #16]
	lsrs	r3, r3, #8
	lsls	r7, r3, #31
	and	r0, r3, #7
	strb	r0, [r6, #2764]
	bmi	.L1562
	movs	r3, #1
	strb	r3, [r6, #2772]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2764]	@ zero_extendqisi2
	bl	NandcSetMode
.L1562:
	ldr	r3, [r4, #1788]
	ldr	r6, .L1645+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #76]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1560
	ldr	r3, [r6, #2776]
	cbz	r3, .L1565
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	b	.L1642
.L1646:
	.align	2
.L1645:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC97
	.word	.LANCHOR1+256
	.word	.LANCHOR1
	.word	1446522928
	.word	.LANCHOR1+2908
	.word	.LANCHOR0+44
.L1565:
	ldrb	r0, [r6, #2764]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2764]	@ zero_extendqisi2
.L1642:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1560
	movs	r0, #1
	ldr	r6, .L1647
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #1788]
	ldr	r0, .L1647+4
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	adds	r1, r0, #1
	beq	.L1550
	bl	FlashDieInfoInit
	ldr	r3, [r6, #1788]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, [r6, #1788]
	ldrh	r2, [r6, #1824]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	addw	r2, r2, #4095
	cmp	r1, r2, asr #12
	blt	.L1567
	ldrh	r2, [r3, #14]
	adds	r2, r2, #255
	cmp	r1, r2, asr #8
	bge	.L1568
.L1567:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L1568:
	ldrb	r3, [r4, #2764]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1569
	bl	FlashSavePhyInfo
	movs	r0, #0
	ldr	r1, [r5, #4004]
	bl	FlashDdrParaScan
.L1569:
	bl	FlashSavePhyInfo
.L1560:
	ldr	r2, [r4, #1788]
	ldr	r6, .L1647
	ldr	r7, .L1647+8
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r8, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #76]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r4, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r5, #4032]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #2828]
	strb	r3, [r4, #2764]
	movs	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r5, #3996]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r8
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #1788]
	ldrh	r2, [r3, #16]
	lsls	r2, r2, #25
	bpl	.L1571
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r6, #1907]	@ zero_extendqisi2
	strb	r0, [r6, #1904]
	strb	r3, [r6, #1905]
	ldrb	r3, [r6, #1908]	@ zero_extendqisi2
	strb	r3, [r7, #4013]
	subs	r3, r0, #1
	cmp	r3, #6
	bhi	.L1572
	ldr	r3, .L1647+12
	str	r3, [r7, #3996]
	subs	r3, r0, #5
	cmp	r3, #1
	bhi	.L1573
	movs	r3, #1
	str	r3, [r6, #2820]
	b	.L1574
.L1573:
	cmp	r0, #7
	beq	.L1596
.L1574:
	ldr	r6, .L1647+16
	b	.L1575
.L1596:
	addw	r6, r6, #1934
.L1575:
	subs	r2, r6, #1
	movs	r3, #0
	adds	r6, r6, #31
.L1576:
	ldrsb	r1, [r2, #1]!
	cmp	r1, #0
	it	eq
	addeq	r3, r3, #1
	cmp	r2, r6
	bne	.L1576
	cmp	r3, #27
	bls	.L1571
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1571
.L1572:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1578
	ldr	r3, .L1647+20
	cmp	r0, #19
	str	r3, [r7, #3996]
	ite	ne
	movne	r3, #7
	moveq	r3, #15
	strb	r3, [r7, #4013]
	b	.L1571
.L1578:
	cmp	r0, #33
	beq	.L1580
	sub	r3, r0, #65
	cmp	r3, #1
	bhi	.L1581
.L1580:
	ldr	r3, .L1647+24
	str	r3, [r5, #3996]
	movs	r3, #4
	strb	r3, [r4, #1905]
	movs	r3, #7
	strb	r3, [r5, #4013]
	b	.L1571
.L1581:
	sub	r3, r0, #67
	uxtb	r3, r3
	cmp	r3, #1
	bls	.L1582
	sub	r2, r0, #34
	cmp	r2, #1
	bhi	.L1583
.L1582:
	ldr	r2, .L1647+24
	cmp	r0, #35
	str	r2, [r5, #3996]
	beq	.L1584
	cmp	r0, #68
	beq	.L1584
	ldr	r2, .L1647+8
	movs	r1, #7
	strb	r1, [r2, #4013]
	b	.L1585
.L1584:
	movs	r2, #17
	strb	r2, [r5, #4013]
.L1585:
	cmp	r3, #1
	ite	ls
	movls	r3, #4
	movhi	r3, #5
	strb	r3, [r4, #1905]
	b	.L1571
.L1583:
	cmp	r0, #49
	itt	eq
	ldreq	r3, .L1647+28
	streq	r3, [r7, #3996]
.L1571:
	ldr	r1, [r4, #2768]
	ldr	r3, .L1647+32
	ldr	r2, .L1647
	cmp	r1, r3
	bne	.L1587
	ldrb	r0, [r2, #76]	@ zero_extendqisi2
	cbz	r0, .L1587
	ldr	r2, [r2, #1788]
	movs	r0, #0
	strb	r0, [r2, #18]
.L1587:
	ldrb	r0, [r4, #1792]	@ zero_extendqisi2
	ldr	r2, .L1647
	cmp	r0, #44
	bne	.L1588
	ldrb	r0, [r2, #2772]	@ zero_extendqisi2
	cbz	r0, .L1588
	cmp	r1, r3
	bne	.L1589
	ldrb	r3, [r2, #76]	@ zero_extendqisi2
	cbnz	r3, .L1588
.L1589:
	movs	r0, #1
	movs	r3, #0
	strb	r3, [r4, #2772]
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
.L1588:
	movs	r0, #0
	bl	flash_enter_slc_mode
	ldrb	r3, [r4, #2764]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1590
	ldr	r2, .L1647
	ldrb	r2, [r2, #2772]	@ zero_extendqisi2
	cbnz	r2, .L1591
	lsls	r3, r3, #31
	bmi	.L1590
.L1591:
	movs	r0, #0
	ldr	r1, [r5, #4004]
	bl	FlashDdrParaScan
.L1590:
	movs	r0, #0
	mov	r8, #16
	bl	flash_exit_slc_mode
	ldr	r3, [r4, #1788]
	ldr	r6, .L1647
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1647+36
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1647+40
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
	bne	.L1593
	lsls	r3, r3, #1
	lsrs	r7, r7, #1
	lsls	r2, r2, #1
	strh	r3, [r6, #1886]	@ movhi
	strb	r8, [r6, #1]
	movs	r3, #8
	strh	r7, [r6, #1878]	@ movhi
	strh	r2, [r6, #1884]	@ movhi
	strh	r3, [r6, #1890]	@ movhi
.L1593:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	movs	r0, #0
	b	.L1550
.L1595:
	mvn	r0, #1
.L1550:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1648:
	.align	2
.L1647:
	.word	.LANCHOR0
	.word	.LC98
	.word	.LANCHOR2
	.word	HynixReadRetrial
	.word	.LANCHOR0+1926
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	1446522928
	.word	.LANCHOR2+1936
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
	ldr	r2, .L1664
	mov	r8, r1
	mov	r7, r0
	ldr	r3, [r2, #1788]
	mov	r5, r2
	ldrb	r1, [r2, #76]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cbz	r1, .L1650
	ldr	r1, [r2, #2768]
	ldr	r2, .L1664+4
	cmp	r1, r2
	beq	.L1649
.L1650:
	subs	r2, r3, #5
	cmp	r2, #2
	bls	.L1651
	cmp	r3, #68
	beq	.L1651
	cmp	r3, #35
	beq	.L1651
	cmp	r3, #19
	bne	.L1649
.L1651:
	ldr	r9, .L1664
	movw	r10, #65535
	ldr	r6, .L1664+8
.L1653:
	ldr	r3, [r5, #1788]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1649
	add	r3, r9, r4, lsl #1
	ldrh	r3, [r3, #592]
	cmp	r3, r10
	bne	.L1649
	movs	r1, #255
	mov	r2, #32768
	ldr	r0, [r6, #4016]
	bl	memset
	add	r1, r4, r8
	adds	r4, r4, #1
	mov	r0, r7
	ldr	r2, [r6, #4016]
	movs	r3, #0
	bl	FlashProgPage
	uxth	r4, r4
	b	.L1653
.L1649:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1665:
	.align	2
.L1664:
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
	ldr	r3, .L1696
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r6, #0
	ldr	r7, .L1696+4
	.pad #28
	sub	sp, sp, #28
	ldrb	r9, [r3, #265]	@ zero_extendqisi2
	mov	r8, r1
	mov	fp, r0
	str	r2, [sp, #8]
	mov	r10, r7
.L1667:
	cmp	r6, r8
	beq	.L1695
	rsb	r3, r6, r8
	add	r2, sp, #20
	mov	r0, fp
	ldr	r1, [sp, #8]
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #16]
	ldrb	r2, [r7, #1616]	@ zero_extendqisi2
	cmp	r3, r2
	bcc	.L1668
	mov	r3, #-1
	str	r3, [fp]
	b	.L1669
.L1668:
	add	r3, r3, r7
	ldrb	r4, [r3, #1620]	@ zero_extendqisi2
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	ldr	r1, [sp, #20]
	mov	r0, r4
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
	ldr	r3, [sp, #20]
	ldr	r1, [r7, #4]
	add	r1, r1, r3
	mov	r5, r0
	mov	r0, r4
	bl	FlashReadCmd
	ldr	r3, [fp, #8]
	cbz	r3, .L1670
	add	r3, r3, #2048
.L1670:
	ldr	r2, [fp, #12]
	cbz	r2, .L1671
	adds	r2, r2, #8
.L1671:
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
	bls	.L1672
	adds	r0, r5, #1
	it	ne
	movne	r5, #256
	str	r5, [fp]
	b	.L1674
.L1672:
	movs	r3, #0
	str	r3, [fp]
.L1674:
	ldr	r3, [fp, #12]
	cbz	r3, .L1675
	ldr	r2, [r3, #8]
	adds	r1, r2, #1
	bne	.L1675
	ldr	r3, [r3]
	adds	r3, r3, #1
	it	ne
	strne	r2, [fp]
.L1675:
	ldr	r3, [fp]
	adds	r2, r3, #1
	bne	.L1669
	ldr	r1, [fp, #4]
	ldr	r0, .L1696+8
	ldrb	r2, [r10, #1892]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [fp, #8]
	cbz	r1, .L1677
	ldr	r0, .L1696+12
	movs	r2, #4
	movs	r3, #8
	bl	rknand_print_hex
.L1677:
	ldr	r1, [fp, #12]
	cbz	r1, .L1669
	movs	r2, #4
	ldr	r0, .L1696+16
	mov	r3, r2
	bl	rknand_print_hex
.L1669:
	adds	r6, r6, #1
	add	fp, fp, #36
	b	.L1667
.L1695:
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1697:
	.align	2
.L1696:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LC99
	.word	.LC100
	.word	.LC101
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
	ldr	r3, .L1797
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r5, .L1797+4
	str	r1, [sp, #28]
	str	r3, [sp, #24]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	str	r2, [sp, #16]
	str	r3, [sp, #32]
	ldrb	r3, [r5]	@ zero_extendqisi2
	cbz	r3, .L1741
	bl	FlashReadSlc2KPages
	b	.L1792
.L1741:
	mov	r8, r3
	str	r3, [sp, #12]
.L1699:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	cmp	r3, r2
	bcs	.L1794
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
	ldr	r3, [sp, #40]
	ldrb	r2, [r5, #1616]	@ zero_extendqisi2
	cmp	r3, r2
	mov	r6, r0
	bcc	.L1701
	mov	r3, #-1
	str	r3, [r10, r9]
	b	.L1702
.L1701:
	add	r3, r3, r5
	ldrb	r4, [r3, #1620]	@ zero_extendqisi2
	ldr	r3, .L1797+8
	mov	r0, r4
	ldrb	r3, [r3, #4032]	@ zero_extendqisi2
	cmp	r3, #0
	it	eq
	moveq	r6, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1797+4
	ldr	r3, [r3, #1788]
	ldrb	r1, [r3, #19]	@ zero_extendqisi2
	subs	r3, r1, #1
	cmp	r3, #6
	bhi	.L1704
	adds	r2, r5, r4
	cmp	r1, #7
	ldrb	r3, [r2, #1918]	@ zero_extendqisi2
	it	eq
	ldrbeq	r3, [r2, #1926]	@ zero_extendqisi2
	ldr	r2, .L1797+4
	add	r2, r2, r4
	ldrb	r2, [r2, #2760]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1704
	mov	r0, r4
	ldrb	r1, [r5, #1907]	@ zero_extendqisi2
	ldr	r2, .L1797+12
	bl	HynixSetRRPara
.L1704:
	mov	r0, r4
	and	r3, fp, #-2147483648
	str	r3, [sp, #20]
	bl	NandcFlashCs
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1706
	ldr	r3, [sp, #20]
	cbz	r3, .L1707
.L1706:
	ldr	r3, .L1797+4
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	cbz	r3, .L1707
	mov	r0, r4
	bl	flash_enter_slc_mode
.L1709:
	mov	fp, #0
	b	.L1708
.L1707:
	mov	r0, r4
	bl	flash_exit_slc_mode
	b	.L1709
.L1795:
	cmp	r4, #255
	beq	.L1742
.L1710:
	cbz	r6, .L1712
	ldr	r2, [r5, #4]
	mov	r0, r4
	add	r2, r2, r1
	bl	FlashReadDpCmd
.L1713:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cbz	r6, .L1711
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1711:
	ldr	r3, [r7, #12]
	mov	r0, r4
	movs	r1, #0
	ldr	r2, [sp, #24]
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	ip, r0
	cbz	r3, .L1744
	cmp	r0, #-1
	bne	.L1744
	movs	r6, #0
	strb	fp, [r5, #8]
.L1708:
	ldr	r1, [sp, #44]
	adds	r0, r1, #1
	bne	.L1710
	b	.L1795
.L1712:
	mov	r0, r4
	bl	FlashReadCmd
	b	.L1713
.L1742:
	movs	r6, #0
	b	.L1711
.L1744:
	mov	fp, ip
	cbz	r6, .L1715
	ldr	r3, .L1797+4
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
	ldr	ip, [sp, #36]
	cmp	r0, #-1
	mov	r8, r0
	it	eq
	moveq	r6, #0
.L1715:
	mov	r0, r4
	str	ip, [sp, #36]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	ldrb	r3, [sp, #32]	@ zero_extendqisi2
	cmp	ip, #-1
	strb	r3, [r5, #8]
	bne	.L1722
	ldrb	r3, [r5, #2772]	@ zero_extendqisi2
	cbnz	r3, .L1717
.L1721:
	ldr	r3, .L1797+8
	ldr	r6, [r3, #3996]
	cbnz	r6, .L1718
	b	.L1796
.L1717:
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
	beq	.L1720
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1746
.L1720:
	ubfx	r0, r6, #8, #8
	bl	NandcSetDdrPara
	cmp	fp, #-1
	beq	.L1721
	b	.L1746
.L1718:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r6
	cmp	r0, #-1
	mov	fp, r0
	bne	.L1723
	ldr	r3, [r5, #1788]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #1
	cmp	r3, #6
	bhi	.L1724
	mov	r0, r4
	ldrb	r1, [r5, #1907]	@ zero_extendqisi2
	ldr	r2, .L1797+12
	movs	r3, #0
	bl	HynixSetRRPara
.L1724:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r5, #1892]	@ zero_extendqisi2
	mov	fp, r0
	ldr	r0, .L1797+16
	mov	r3, fp
	bl	printk
	cmp	fp, #-1
	bne	.L1723
	ldrb	r6, [r5, #76]	@ zero_extendqisi2
	cbz	r6, .L1725
	ldr	r3, [sp, #16]
	mov	r0, r4
	cmp	r3, #1
	beq	.L1726
	ldr	r3, [sp, #20]
	cbz	r3, .L1727
.L1726:
	bl	flash_enter_slc_mode
	b	.L1728
.L1727:
	bl	flash_exit_slc_mode
.L1728:
	ldr	r3, .L1797+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r6, [r3, #3996]
	ldr	r3, [r7, #12]
	blx	r6
	movs	r6, #0
	mov	fp, r0
	b	.L1729
.L1796:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	fp, r0
	b	.L1729
.L1746:
	movs	r6, #0
.L1722:
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	fp, r3, asr #2
	bls	.L1731
	ldr	r3, .L1797+8
	ldr	r3, [r3, #3996]
	cbz	r3, .L1748
.L1729:
	cmp	fp, #-1
	beq	.L1725
	b	.L1731
.L1748:
	mov	fp, #256
.L1725:
	str	fp, [r10, r9]
	b	.L1732
.L1798:
	.align	2
.L1797:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1910
	.word	.LC102
.L1723:
	movs	r6, #0
.L1731:
	cmp	fp, #256
	beq	.L1725
	movs	r3, #0
	str	r3, [r10, r9]
.L1732:
	ldr	r3, [r10, r9]
	adds	r2, r3, #1
	bne	.L1734
	ldr	r1, [r7, #4]
	ldr	r0, .L1799
	ldrb	r2, [r5, #1892]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cbz	r1, .L1734
	movs	r2, #4
	ldr	r0, .L1799+4
	mov	r3, r2
	bl	rknand_print_hex
.L1734:
	cbz	r6, .L1736
	ldrb	r3, [r5, #1892]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r8, r3, asr #2
	bls	.L1737
	ldr	r3, .L1799+8
	ldr	r3, [r3, #3996]
	cmp	r3, #0
	it	eq
	moveq	r8, #256
.L1737:
	add	r9, r9, #36
	cmp	r8, #-1
	str	r8, [r10, r9]
	beq	.L1736
	cmp	r8, #256
	itt	ne
	movne	r3, #0
	strne	r3, [r10, r9]
.L1736:
	ldr	r3, [sp, #12]
	add	r3, r3, r6
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1738
	ldr	r3, [sp, #20]
	cbz	r3, .L1702
.L1738:
	ldrb	r3, [r5, #76]	@ zero_extendqisi2
	cbz	r3, .L1702
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1702:
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	str	r3, [sp, #12]
	b	.L1699
.L1794:
	movs	r0, #0
.L1792:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1800:
	.align	2
.L1799:
	.word	.LC99
	.word	.LC101
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
	ldr	r7, .L1813
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L1813+4
	movs	r5, #0
	mov	fp, #-1
	ldrh	r3, [r7, #1878]
	addw	r0, r4, #4036
	ldrh	r6, [r7, #1876]
	mov	r10, r5
	mov	r8, r7
	mov	r9, r4
	smulbb	r6, r6, r3
	bl	__memzero
	ldr	r3, [r4, #4020]
	str	r5, [sp, #20]
	str	r3, [sp, #24]
	uxth	r6, r6
.L1802:
	ldrb	r3, [r8, #1616]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1812
	mul	ip, r6, r7
	subs	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1803:
	cmp	r4, r3
	ble	.L1805
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
	beq	.L1804
	ldr	r2, [r9, #4020]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1804
	mov	r1, r7
	ldr	r0, .L1813+8
	mov	r2, r4
	add	r10, r10, #1
	bl	printk
	ldr	r3, .L1813+4
	uxth	r10, r10
	add	r7, r3, r7, lsl #1
	strh	r4, [r7, #4036]	@ movhi
	b	.L1805
.L1804:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1803
.L1805:
	ldrb	r3, [r8, #1616]	@ zero_extendqisi2
	adds	r5, r5, #1
	cmp	r3, r10
	it	eq
	moveq	fp, #0
	b	.L1802
.L1812:
	mov	r0, fp
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1814:
	.align	2
.L1813:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r8, .L1845+20
	movs	r7, #0
	str	r3, [sp, #12]
	mov	r6, r1
	ldr	r3, .L1845
	mov	r9, r2
	mov	r4, r0
	mov	r10, r0
	mov	fp, r8
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	str	r3, [sp, #8]
.L1816:
	cmp	r7, r6
	beq	.L1844
	subs	r3, r6, r7
	add	r2, sp, #20
	mov	r0, r10
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #24]
	ldrb	r2, [r8, #1616]	@ zero_extendqisi2
	cmp	r3, r2
	bcc	.L1817
	mov	r3, #-1
	str	r3, [r10]
	b	.L1818
.L1817:
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
	ldr	r2, [sp, #8]
	movs	r1, #1
	mov	r0, r5
	str	r3, [sp]
	ldr	r3, [r10, #8]
	bl	NandcXferData
	ldr	r1, [sp, #20]
	mov	r0, r5
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	ldr	r1, [sp, #20]
	mov	r0, r5
	bl	FlashReadStatus
	ldr	r3, [sp, #20]
	sbfx	r0, r0, #0, #1
	str	r0, [r10]
	ldr	r1, [r8, #4]
	mov	r0, r5
	add	r1, r1, r3
	bl	FlashProgFirstCmd
	ldr	r3, [r10, #8]
	cbz	r3, .L1819
	add	r3, r3, #2048
.L1819:
	ldr	r2, [r10, #12]
	cbz	r2, .L1820
	adds	r2, r2, #8
.L1820:
	str	r2, [sp]
	movs	r1, #1
	ldr	r2, [sp, #8]
	mov	r0, r5
	bl	NandcXferData
	ldr	r3, [sp, #20]
	ldr	r1, [fp, #4]
	mov	r0, r5
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
.L1818:
	adds	r7, r7, #1
	add	r10, r10, #36
	b	.L1816
.L1844:
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L1843
	ldr	r8, .L1845+24
	mov	r10, #0
	mov	fp, r8
.L1823:
	cmp	r10, r6
	beq	.L1843
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L1824
	ldr	r1, [r4, #4]
	ldr	r0, .L1845+4
	bl	printk
	b	.L1825
.L1824:
	rsb	r3, r10, r6
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	mov	r7, r4
	bl	LogAddr2PhyAddr
	ldr	r2, [r8, #4024]
	movs	r3, #0
	add	r5, sp, #28
	str	r3, [r2]
	ldr	r2, [r8, #4028]
	str	r3, [r2]
	ldmia	r7!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	ldmia	r7!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	add	r0, sp, #28
	ldr	r3, [r7]
	movs	r1, #1
	mov	r2, r9
	str	r3, [r5]
	ldr	r3, [r8, #4024]
	str	r3, [sp, #36]
	ldr	r3, [r8, #4028]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r5, [sp, #28]
	adds	r3, r5, #1
	bne	.L1826
	ldr	r0, .L1845+8
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L1826:
	ldr	r3, [r4, #12]
	cbz	r3, .L1827
	ldr	r2, [r3]
	ldr	r3, [fp, #4028]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1827
	ldr	r0, .L1845+12
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1827:
	ldr	r3, [r4, #8]
	cbz	r3, .L1825
	ldr	r2, [r3]
	ldr	r3, [fp, #4024]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1825
	ldr	r0, .L1845+16
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1825:
	add	r10, r10, #1
	adds	r4, r4, #36
	b	.L1823
.L1843:
	movs	r0, #0
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1846:
	.align	2
.L1845:
	.word	.LANCHOR1
	.word	.LC104
	.word	.LC105
	.word	.LC106
	.word	.LC107
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
	ldr	r5, .L1858
	ldr	r7, .L1858+4
	ldr	r3, [r5, #1776]
	subw	r8, r7, #2982
	ldr	r9, [r5, #1800]
	mov	r10, r8
	str	r3, [r5, #4060]
	str	r9, [r5, #4064]
.L1848:
	ldrh	r3, [r8, #2862]
	cmp	r6, r3
	bcs	.L1857
	ldrh	r4, [r8, #2906]
	movw	fp, #61664
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
	subs	r4, r4, #1
	uxth	r4, r4
.L1849:
	ldrh	r3, [r10, #2906]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1851
	mla	r3, r3, r6, r4
	movs	r1, #1
	ldr	r0, .L1858+8
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [r5, #4056]
	bl	FlashReadPages
	ldr	r3, [r5, #4052]
	adds	r3, r3, #1
	beq	.L1850
	ldrh	r3, [r9]
	cmp	r3, fp
	bne	.L1850
	strh	r4, [r7]	@ movhi
	b	.L1851
.L1850:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1849
.L1851:
	adds	r6, r6, #1
	b	.L1848
.L1857:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1859:
	.align	2
.L1858:
	.word	.LANCHOR2
	.word	.LANCHOR0+2982
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
	ldr	r3, .L1871
	cmp	r1, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #104
	sub	sp, sp, #104
	it	eq
	ldrheq	r4, [r3, #2912]
	mov	r8, r1
	it	ne
	ldrhne	r4, [r3, #2910]
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
	bne	.L1863
.L1864:
	sxth	r3, r7
	sxth	r4, r5
	cmp	r3, r4
	bgt	.L1863
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
	bne	.L1865
	ldr	r3, [sp, #44]
	adds	r3, r3, #1
	bne	.L1865
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L1865
	subs	r4, r4, #1
	uxth	r5, r4
	b	.L1864
.L1865:
	adds	r3, r4, #1
	uxth	r7, r3
	b	.L1864
.L1863:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1872:
	.align	2
.L1871:
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
	ldr	r4, .L1902
	ldr	r7, .L1902+4
	ldr	r3, [r4, #1776]
	mov	r8, r4
	ldr	r6, [r4, #1800]
	str	r3, [r4, #4060]
	str	r6, [r4, #4064]
	bl	FtlBbtMemInit
	ldrh	r5, [r7, #2906]
	subs	r5, r5, #1
	uxth	r5, r5
.L1874:
	ldrh	r3, [r7, #2906]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1877
	movs	r1, #1
	ldr	r0, .L1902+8
	lsls	r3, r5, #10
	str	r3, [r4, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #4052]
	adds	r3, r3, #1
	bne	.L1875
	movs	r1, #1
	ldr	r3, [r8, #4056]
	ldr	r0, .L1902+8
	mov	r2, r1
	adds	r3, r3, #1
	str	r3, [r8, #4056]
	bl	FlashReadPages
.L1875:
	ldr	r3, [r4, #4052]
	adds	r3, r3, #1
	beq	.L1876
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1876
	ldr	r3, [r6, #4]
	strh	r5, [r7, #2972]	@ movhi
	str	r3, [r7, #2980]
	ldrh	r3, [r6, #8]
	strh	r3, [r7, #2976]	@ movhi
	b	.L1877
.L1876:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1874
.L1877:
	ldrh	r3, [r7, #2972]
	movw	r2, #65535
	ldr	r5, .L1902+4
	cmp	r3, r2
	beq	.L1891
	ldrh	r3, [r5, #2976]
	cmp	r3, r2
	beq	.L1881
	movs	r1, #1
	ldr	r0, .L1902+8
	lsls	r3, r3, #10
	str	r3, [r4, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r4, #4052]
	adds	r3, r3, #1
	beq	.L1881
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1881
	ldr	r3, [r6, #4]
	ldr	r2, [r5, #2980]
	cmp	r3, r2
	itttt	hi
	strhi	r3, [r5, #2980]
	ldrhhi	r2, [r5, #2976]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r5, #2972]	@ movhi
	it	hi
	strhhi	r3, [r5, #2976]	@ movhi
.L1881:
	ldrh	r0, [r7, #2972]
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r7, #2974]	@ movhi
.L1883:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L1888
	ldrh	r2, [r7, #2972]
	movs	r1, #1
	ldr	r0, .L1902+8
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #4056]
	ldr	r3, [r4, #1776]
	mov	r2, r1
	str	r3, [r4, #4060]
	bl	FlashReadPages
	ldr	r3, [r4, #4052]
	adds	r3, r3, #1
	beq	.L1884
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1884
.L1888:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L1902+4
	strh	r2, [r7, #2978]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1885
	b	.L1886
.L1884:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1883
.L1885:
	ldr	r2, [r3, #2836]
	cmp	r0, r2
	beq	.L1886
	ldrh	r3, [r3, #2850]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L1886
	cmp	r0, r3
	bcs	.L1886
	bl	FtlSysBlkNumInit
.L1886:
	ldr	r6, .L1902+12
	movs	r5, #0
.L1889:
	ldrh	r3, [r7, #2862]
	cmp	r5, r3
	bcs	.L1901
	ldrh	r2, [r4, #1864]
	ldr	r1, [r4, #4060]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	bl	ftl_memcpy
	adds	r5, r5, #1
	b	.L1889
.L1901:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1891:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L1903:
	.align	2
.L1902:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+4052
	.word	.LANCHOR0+2996
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
	ldr	r5, .L1989
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L1989+4
	mov	r1, r6
	ldr	r2, [r5, #2940]
	ldr	r0, [r4, #1856]
	mov	r9, r4
	strh	r6, [r5, #2954]	@ movhi
	lsls	r2, r2, #2
	strh	r6, [r4, #1912]	@ movhi
	bl	ftl_memset
	ldr	r2, [r5, #2940]
	mov	r1, r6
	ldr	r0, [r4, #1828]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2932]
	mov	r1, r6
	ldr	r0, [r4, #1840]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #2932]
	ldr	r0, [r5, #2956]
	mov	r1, r6
	lsls	r2, r2, #1
	bl	ftl_memset
	addw	r0, r4, #1060
	movs	r1, #255
	movs	r2, #12
	bl	memset
	ldrh	r3, [r5, #2848]
	str	r5, [sp, #12]
	mov	r5, r4
	str	r3, [sp, #4]
.L1905:
	ldr	r6, .L1989
	ldr	r2, [sp, #4]
	ldrh	r3, [r6, #2850]
	mov	r0, r6
	cmp	r3, r2
	bls	.L1947
	ldr	r1, [r4, #1664]
	mov	r10, #36
	ldrh	r8, [r6, #2840]
	movs	r7, #0
	ldr	r2, [r4, #1764]
	str	r1, [sp, #8]
	ldr	r3, [r4, #1660]
	ldrh	r1, [r6, #2922]
	str	r7, [sp]
	str	r1, [sp, #16]
	addw	r1, r6, #2868
	str	r1, [sp, #20]
.L1948:
	uxth	r1, r7
	cmp	r1, r8
	bcs	.L1984
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
	cbnz	r0, .L1906
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
.L1906:
	adds	r7, r7, #1
	b	.L1948
.L1984:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L1910
	ldr	r8, .L1989
	mov	r1, r3
	ldr	r0, [r4, #1764]
	movs	r2, #1
	bl	FlashReadPages
	movs	r3, #0
	str	r3, [sp, #8]
.L1911:
	ldrh	r3, [sp, #8]
	ldr	r2, [sp]
	cmp	r3, r2
	bcs	.L1910
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
	bne	.L1913
	mov	fp, #16
	movw	r3, #65535
.L1912:
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
	bne	.L1914
	ldr	r3, [r9, #1764]
	mov	r2, #-1
	str	r2, [r3, r10]
	b	.L1913
.L1914:
	ldr	r2, [r5, #1764]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L1913
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L1912
.L1913:
	ldr	r3, [r5, #1764]
	ldr	r3, [r3, r10]
	adds	r3, r3, #1
	beq	.L1916
	ldr	r2, [r5, #1028]
	ldr	r3, [r7, #4]
	adds	r1, r2, #1
	beq	.L1917
	cmp	r2, r3
	bhi	.L1918
.L1917:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r9, #1028]
.L1918:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L1920
	bhi	.L1921
	movw	r3, #61574
	cmp	r2, r3
	bne	.L1919
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #12]
	ldrh	r10, [r3, #2932]
	ldrh	r0, [r2, #2954]
	ldr	r2, [r9, #1840]
	add	lr, r10, #-1
	uxth	r3, lr
	rsb	lr, r0, lr
	str	r2, [sp, #16]
	b	.L1934
.L1921:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L1923
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1919
	mov	r0, r6
	b	.L1983
.L1923:
	ldr	r3, [sp, #12]
	ldrh	r0, [r4, #1912]
	ldr	r2, [r4, #1856]
	ldr	r1, [r3, #2940]
	str	r2, [sp, #16]
	uxth	lr, r1
	add	r3, lr, #-1
	rsb	lr, r0, lr
	add	lr, lr, #-1
	uxth	r3, r3
	sxth	lr, lr
.L1925:
	sxth	ip, r3
	cmp	ip, lr
	ble	.L1985
	ldr	r2, [r7, #4]
	lsl	fp, ip, #2
	mov	r10, r2
	ldr	r2, [sp, #16]
	ldr	r2, [r2, ip, lsl #2]
	cmp	r10, r2
	bls	.L1926
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L1927
	cmp	r0, r1
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r9, #1912]	@ movhi
.L1927:
	movs	r1, #0
	uxth	lr, r3
.L1928:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L1986
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
	b	.L1928
.L1986:
	ldr	r1, [r7, #4]
	ldr	r2, [r4, #1856]
	str	r1, [r2, fp]
	sxth	r1, r3
	ldr	r2, [r4, #1828]
	cmp	r1, #0
	strh	r6, [r2, ip, lsl #1]	@ movhi
	bge	.L1930
	b	.L1919
.L1926:
	subs	r3, r3, #1
	uxth	r3, r3
	b	.L1925
.L1985:
	cmp	ip, #0
	bge	.L1961
	b	.L1919
.L1930:
	ldrh	r0, [r5, #1912]
	ldr	r2, [r8, #2940]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r1, r2
	bgt	.L1919
.L1961:
	ldr	r2, [r4, #1856]
	sxth	r3, r3
	ldr	r1, [r7, #4]
	adds	r0, r0, #1
	strh	r0, [r4, #1912]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #1828]
	b	.L1981
.L1940:
	ldr	r1, [sp, #16]
	lsl	fp, ip, #2
	ldr	r2, [r7, #4]
	ldr	r1, [r1, ip, lsl #2]
	cmp	r2, r1
	bhi	.L1987
	subs	r3, r3, #1
	uxth	r3, r3
.L1934:
	sxth	ip, r3
	cmp	ip, lr
	bgt	.L1940
	b	.L1939
.L1990:
	.align	2
.L1989:
	.word	.LANCHOR0
	.word	.LANCHOR2
.L1987:
	ldr	r2, [sp, #16]
	ldr	r2, [r2]
	cbnz	r2, .L1936
	cmp	r0, r10
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r8, #2954]	@ movhi
.L1936:
	movs	r1, #0
	uxth	lr, r3
.L1937:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L1988
	ldr	r0, [r4, #1840]
	sxth	r2, r2
	adds	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	ldr	r0, [r8, #2956]
	add	r10, r0, r2, lsl #1
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2, lsl #1]	@ movhi
	b	.L1937
.L1988:
	ldr	r2, [r5, #1840]
	ldr	r1, [r7, #4]
	str	r1, [r2, fp]
	ldr	r2, .L1991
	ldr	r2, [r2, #2956]
	strh	r6, [r2, ip, lsl #1]	@ movhi
.L1939:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1919
	ldrh	r2, [r8, #2932]
	ldrh	r1, [r8, #2954]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L1919
	adds	r1, r1, #1
	ldr	r2, [r9, #1840]
	strh	r1, [r8, #2954]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r8, #2956]
.L1981:
	strh	r6, [r2, r3, lsl #1]	@ movhi
	b	.L1919
.L1920:
	ldrh	r1, [r9, #1060]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1941
	strh	r6, [r5, #1060]	@ movhi
	b	.L1982
.L1941:
	ldrh	r0, [r5, #1064]
	cmp	r0, r2
	beq	.L1942
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1942:
	ldr	r2, [r9, #1068]
	ldr	r3, [r7, #4]
	cmp	r2, r3
	bcs	.L1943
	ldrh	r3, [r5, #1060]
	strh	r6, [r5, #1060]	@ movhi
	strh	r3, [r5, #1064]	@ movhi
	ldr	r3, [r7, #4]
.L1982:
	str	r3, [r5, #1068]
	b	.L1919
.L1943:
	strh	r6, [r5, #1064]	@ movhi
	b	.L1919
.L1916:
	ldrb	r1, [r8, #76]	@ zero_extendqisi2
	mov	r0, r6
	cbz	r1, .L1944
.L1983:
	movs	r1, #0
.L1944:
	bl	FtlFreeSysBlkQueueIn
.L1919:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L1911
.L1910:
	ldr	r6, [sp, #4]
	adds	r6, r6, #1
	uxth	r3, r6
	str	r3, [sp, #4]
	b	.L1905
.L1947:
	ldr	r3, .L1991+4
	ldr	r5, [r3, #1828]
	ldrh	r2, [r5]
	cbz	r2, .L1949
.L1952:
	ldr	r5, [r0, #2956]
	ldr	r3, .L1991
	ldrh	r2, [r5]
	cbz	r2, .L1950
	b	.L1973
.L1949:
	ldrh	r3, [r3, #1912]
	cmp	r3, #0
	beq	.L1952
	ldr	r6, [r6, #2940]
.L1953:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bcs	.L1952
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L1953
	mov	lr, #0
.L1954:
	ldr	r5, [r0, #2940]
	sxth	r2, r3
	cmp	r2, r5
	bcs	.L1952
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
	b	.L1954
.L1950:
	ldrh	r1, [r3, #2954]
	cbz	r1, .L1973
	ldrh	r6, [r3, #2932]
.L1957:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, r6
	bge	.L1973
	ldrh	r7, [r5, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r7, #0
	beq	.L1957
	ldr	r5, .L1991
	mov	ip, #0
.L1958:
	ldrh	r6, [r0, #2932]
	sxth	r2, r3
	cmp	r2, r6
	bge	.L1973
	ldr	r6, [r5, #2956]
	rsb	lr, r1, r2
	adds	r3, r3, #1
	ldrh	r7, [r6, r2, lsl #1]
	uxth	r3, r3
	strh	r7, [r6, lr, lsl #1]	@ movhi
	ldr	r6, [r4, #1840]
	ldr	r7, [r6, r2, lsl #2]
	str	r7, [r6, lr, lsl #2]
	ldr	r6, [r5, #2956]
	strh	ip, [r6, r2, lsl #1]	@ movhi
	b	.L1958
.L1973:
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1992:
	.align	2
.L1991:
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
	ldr	r6, .L2012
	ldr	r5, .L2012+4
	ldrh	r2, [r6, #2848]
	ldr	r3, [r5, #1800]
	ldr	r0, [r5, #816]
	lsls	r2, r2, #1
	str	r1, [r5, #4060]
	str	r3, [r5, #4064]
	bl	ftl_memset
	ldrh	r0, [r5, #1060]
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1994
.L2002:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L1994:
	movs	r1, #1
	ldr	r8, .L2012+12
	bl	FtlGetLastWrittenPage
	mov	r7, r5
	uxth	r4, r0
	adds	r0, r0, #1
	strh	r0, [r5, #1062]	@ movhi
.L1996:
	sxth	r3, r4
	cmp	r3, #0
	blt	.L2001
	ldrh	r2, [r5, #1060]
	movs	r1, #1
	ldr	r0, .L2012+8
	orr	r3, r3, r2, lsl #10
	str	r3, [r5, #4056]
	ldr	r3, [r5, #1776]
	mov	r2, r1
	str	r3, [r5, #4060]
	bl	FlashReadPages
	ldr	r3, [r5, #4052]
	adds	r3, r3, #1
	beq	.L1997
	ldr	r3, [r7, #1776]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1997
	ldr	r3, [r7, #1800]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L1997
.L2001:
	ldr	r4, .L2012+4
	movs	r2, #48
	ldr	r1, [r5, #4060]
	add	r0, r4, #760
	ldr	r7, .L2012
	bl	ftl_memcpy
	ldrh	r2, [r6, #2848]
	ldr	r1, [r5, #4060]
	ldr	r0, [r5, #816]
	adds	r1, r1, #48
	lsls	r2, r2, #1
	bl	ftl_memcpy
	ldrh	r2, [r6, #2848]
	ldr	r3, [r5, #4060]
	add	r1, r2, #24
	ldr	r0, [r5, #992]
	ubfx	r1, r1, #1, #30
	lsrs	r2, r2, #3
	adds	r2, r2, #4
	add	r1, r3, r1, lsl #2
	bl	ftl_memcpy
	ldrh	r3, [r6, #2952]
	cbz	r3, .L1999
	ldrh	r2, [r7, #2848]
	ldr	r1, [r4, #4060]
	ldr	r0, [r4, #1852]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldrh	r2, [r7, #2948]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	lsls	r2, r2, #2
	add	r1, r1, r3, lsl #2
	bl	ftl_memcpy
	b	.L1999
.L1997:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1996
.L1999:
	ldr	r2, [r5, #760]
	ldr	r3, .L2012+12
	ldr	r4, .L2012+4
	cmp	r2, r3
	bne	.L2002
	ldrb	r0, [r4, #770]	@ zero_extendqisi2
	ldrh	r1, [r6, #2862]
	ldrh	r2, [r4, #768]
	cmp	r0, r1
	ldr	r3, .L2012
	strh	r2, [r4, #1066]	@ movhi
	bne	.L2002
	ldrh	r1, [r3, #2910]
	ldrh	r0, [r3, #2916]
	str	r2, [r4, #4088]
	muls	r1, r2, r1
	str	r1, [r3, #2968]
	muls	r1, r0, r1
	ldrh	r0, [r3, #2978]
	str	r1, [r3, #1896]
	ldr	r1, [r3, #2852]
	subs	r0, r1, r0
	ldrh	r1, [r3, #2840]
	subs	r0, r0, r2
	bl	__aeabi_uidiv
	ldrh	r3, [r4, #776]
	ldrh	r1, [r4, #774]
	lsrs	r2, r3, #6
	strh	r2, [r4, #838]	@ movhi
	ldrh	r2, [r4, #778]
	and	r3, r3, #63
	strb	r3, [r4, #842]
	ldrb	r3, [r4, #771]	@ zero_extendqisi2
	strh	r2, [r4, #884]	@ movhi
	ldrh	r2, [r4, #780]
	strb	r3, [r4, #844]
	movw	r3, #65535
	strh	r1, [r4, #836]	@ movhi
	strh	r3, [r4, #1072]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #1074]	@ movhi
	strb	r3, [r4, #1078]
	strb	r3, [r4, #1080]
	str	r3, [r4, #1016]
	strh	r0, [r4, #1056]	@ movhi
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #890]
	ldrb	r2, [r4, #772]	@ zero_extendqisi2
	strh	r0, [r4, #886]	@ movhi
	strb	r2, [r4, #892]
	ldrh	r2, [r4, #782]
	strh	r2, [r4, #932]	@ movhi
	ldrh	r2, [r4, #784]
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #938]
	ldrb	r2, [r4, #773]	@ zero_extendqisi2
	strh	r0, [r4, #934]	@ movhi
	strb	r2, [r4, #940]
	str	r3, [r4, #1004]
	ldr	r2, [r4, #792]
	str	r3, [r4, #996]
	str	r3, [r4, #1012]
	str	r3, [r4, #1040]
	str	r3, [r4, #1048]
	str	r3, [r4, #1008]
	ldr	r3, [r4, #800]
	str	r2, [r4, #1036]
	ldr	r2, [r4, #1028]
	cmp	r3, r2
	ldr	r2, [r5, #1032]
	it	hi
	strhi	r3, [r4, #1028]
	ldr	r3, [r5, #796]
	cmp	r3, r2
	itt	hi
	ldrhi	r2, .L2012+4
	strhi	r3, [r2, #1032]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2005
	ldr	r0, .L2012+16
	bl	make_superblock
.L2005:
	ldrh	r2, [r5, #884]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2006
	ldr	r0, .L2012+20
	bl	make_superblock
.L2006:
	ldrh	r2, [r5, #932]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2007
	ldr	r0, .L2012+24
	bl	make_superblock
.L2007:
	ldrh	r2, [r5, #1072]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2008
	ldr	r0, .L2012+28
	bl	make_superblock
.L2008:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2013:
	.align	2
.L2012:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+4052
	.word	1179929683
	.word	.LANCHOR2+836
	.word	.LANCHOR2+884
	.word	.LANCHOR2+932
	.word	.LANCHOR2+1072
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
	ldr	r3, .L2048
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldrh	r4, [r3, #3064]
	movw	r3, #65535
	mov	r5, r0
	str	r1, [sp, #12]
	cmp	r4, r3
	beq	.L2037
	cbnz	r4, .L2015
	b	.L2016
.L2037:
	movs	r4, #0
.L2015:
	ldr	r3, .L2048+4
	ldr	r2, [sp, #12]
	ldrh	r3, [r3, #2910]
	cmp	r2, r3
	bne	.L2017
.L2016:
	bl	FtlGcPageVarInit
.L2017:
	ldr	r7, .L2048+8
	mov	r10, #0
	mov	r3, #-1
	str	r3, [sp, #4]
.L2031:
	ldrh	r1, [r5]
	movw	r3, #65535
	movs	r2, #0
	strb	r2, [r5, #8]
	cmp	r1, r3
	beq	.L2018
.L2019:
	ldr	r9, .L2048+4
.L2034:
	ldr	r3, [r7, #1664]
	movs	r2, #0
	ldrh	r8, [r9, #2840]
	mov	ip, #36
	ldr	fp, [r7, #1764]
	mov	r6, r2
	str	r3, [sp, #16]
	movw	lr, #65535
	ldrh	r3, [r9, #2922]
	str	r3, [sp, #20]
	add	r3, r5, #14
	str	r3, [sp, #8]
.L2020:
	uxth	r3, r2
	cmp	r3, r8
	bcs	.L2046
	ldr	r1, [sp, #8]
	ldrh	r3, [r1, #2]!
	cmp	r3, lr
	str	r1, [sp, #8]
	beq	.L2021
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
.L2021:
	adds	r2, r2, #1
	b	.L2020
.L2046:
	ldr	r0, [r7, #1764]
	mov	r1, r6
	movs	r2, #0
	mov	fp, #0
	bl	FlashReadPages
.L2024:
	uxth	r3, fp
	cmp	r3, r6
	bcs	.L2047
	movs	r3, #36
	mul	r8, r3, fp
	ldr	r3, .L2048+8
	ldr	ip, [r3, #1764]
	add	r1, ip, r8
	str	r1, [sp, #16]
	str	ip, [sp, #20]
	ldr	r3, [r1, #4]
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #8]
	bl	P2V_plane
	ldr	ip, [sp, #20]
	ldr	r3, [sp, #8]
	mov	r2, r0
	ldr	r0, [ip, r8]
	cbnz	r0, .L2025
	ldr	r1, [sp, #16]
	movw	r0, #65535
	add	fp, fp, #1
	ldr	lr, [r1, #12]
	ldrh	r1, [lr]
	cmp	r1, r0
	bne	.L2026
	ldr	r3, .L2048+8
	movs	r1, #1
	str	r1, [r3, #1928]
	b	.L2018
.L2026:
	ldr	r0, [lr, #12]
	mov	r1, r3
	ldr	r2, [lr, #8]
	bl	FtlGcUpdatePage
	b	.L2024
.L2025:
	mov	r2, r3
	ldrh	r1, [r5]
	ldr	r0, .L2048+12
	bl	printk
	ldr	r2, .L2048+4
	ldrh	r3, [r5]
	ldr	r1, [r2, #2832]
	cbnz	r1, .L2027
	ldrb	r2, [r2, #76]	@ zero_extendqisi2
	cbz	r2, .L2028
.L2027:
	ldr	r2, [r7, #752]
	ldrh	r2, [r2, r3, lsl #1]
	cmp	r2, #119
	bls	.L2029
.L2028:
	ldr	r2, [r7, #1764]
	ldr	r2, [r2, r8]
	adds	r2, r2, #1
	bne	.L2030
.L2029:
	ldr	r2, [r7, #1764]
	add	r8, r8, r2
	ldr	r2, [r8, #4]
	str	r2, [sp, #4]
.L2030:
	ldr	r2, .L2048+8
	movs	r4, #0
	ldr	r2, [r2, #816]
	strh	r4, [r2, r3, lsl #1]	@ movhi
	ldrh	r0, [r5]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r5]	@ movhi
	bl	FtlGcPageVarInit
	b	.L2031
.L2047:
	ldr	r3, [sp, #12]
	add	r10, r10, #1
	adds	r4, r4, #1
	cmp	r10, r3
	uxth	r4, r4
	bcs	.L2033
.L2035:
	ldrh	r3, [r9, #2910]
	cmp	r3, r4
	bhi	.L2034
	movs	r2, #0
	b	.L2018
.L2033:
	ldr	r2, .L2048
	movw	r1, #65535
	ldrh	r3, [r2, #3064]
	cmp	r3, r1
	beq	.L2035
	add	r3, r3, r10
	strh	r3, [r2, #3064]	@ movhi
	ldrh	r3, [r9, #2910]
	cmp	r3, r4
	bls	.L2035
	b	.L2036
.L2018:
	ldr	r3, .L2048
	movw	r1, #65535
	strh	r4, [r5, #2]	@ movhi
	mov	r0, r5
	strb	r2, [r5, #6]
	strh	r1, [r3, #3064]	@ movhi
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L2036:
	ldr	r0, [sp, #4]
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2049:
	.align	2
.L2048:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC108
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
	ldr	r5, .L2105
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	r6, [r5, #1788]
	ldrb	r7, [r5]	@ zero_extendqisi2
	ldrb	r6, [r6, #19]	@ zero_extendqisi2
	str	r6, [sp, #16]
	ldr	r6, .L2105+4
	ldrb	r6, [r6, #265]	@ zero_extendqisi2
	str	r6, [sp, #12]
	cbz	r7, .L2051
	bl	FlashProgSlc2KPages
	b	.L2052
.L2065:
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
	ldr	r3, [sp, #32]
	ldrb	r2, [r5, #1616]	@ zero_extendqisi2
	cmp	r3, r2
	mov	r10, r0
	bcc	.L2053
	mov	r3, #-1
	str	r3, [r4, r8]
.L2054:
	adds	r7, r7, #1
.L2051:
	ldr	r3, [sp, #8]
	ldr	r6, .L2105
	cmp	r7, r3
	bcc	.L2065
	b	.L2103
.L2053:
	add	r3, r5, r3, lsl #4
	ldrb	r1, [r5, #2828]	@ zero_extendqisi2
	ldr	r3, [r3, #1668]
	cmp	r1, #0
	it	eq
	moveq	r10, #0
	cbz	r3, .L2056
	cmp	r2, #1
	bne	.L2057
	ldr	r0, [r5, #1828]
	bl	NandcIqrWaitFlashReady
.L2057:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L2056:
	ldr	r3, [sp, #32]
	movs	r0, #0
	add	r2, r5, r3, lsl #4
	addw	r1, r2, #1668
	str	r0, [r2, #1672]
	ldr	r0, [sp, #28]
	str	fp, [r2, #1668]
	str	r0, [r2, #1664]
	cmp	r10, #0
	beq	.L2058
	add	r2, r8, #36
	add	r2, r2, r4
	str	r2, [r1, #4]
.L2058:
	adds	r2, r5, r3
	add	r3, r5, r3, lsl #4
	ldrb	r6, [r2, #1620]	@ zero_extendqisi2
	strb	r6, [r3, #1660]
	mov	r0, r6
	ldrb	r3, [r5, #1616]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2059
	bl	NandcWaitFlashReady
	b	.L2060
.L2059:
	bl	NandcFlashCs
	ldr	r3, [sp, #32]
	mov	r0, r6
	ldr	r1, [sp, #28]
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #1628]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	bl	NandcFlashDeCs
.L2060:
	ldr	r3, [sp, #16]
	subs	r3, r3, #1
	cmp	r3, #6
	bhi	.L2061
	adds	r3, r5, r6
	ldrb	r3, [r3, #2760]	@ zero_extendqisi2
	cbz	r3, .L2061
	mov	r0, r6
	ldrb	r1, [r5, #1907]	@ zero_extendqisi2
	ldr	r2, .L2105+8
	movs	r3, #0
	bl	HynixSetRRPara
.L2061:
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r6
	bne	.L2062
	ldrb	r3, [r5, #76]	@ zero_extendqisi2
	cbz	r3, .L2062
	bl	flash_enter_slc_mode
	b	.L2063
.L2062:
	bl	flash_exit_slc_mode
.L2063:
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r0, r6
	movs	r1, #1
	ldr	r2, [sp, #12]
	str	r3, [sp]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L2064
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	mov	r0, r6
	add	r8, r8, #36
	add	r3, r5, r3, lsl #2
	add	r8, r8, r4
	ldr	r2, [r3, #1628]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r3, [sp, #28]
	ldr	r1, [r5, #4]
	mov	r0, r6
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r8, #12]
	mov	r0, r6
	movs	r1, #1
	ldr	r2, [sp, #12]
	str	r3, [sp]
	ldr	r3, [r8, #8]
	bl	NandcXferData
.L2064:
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2054
.L2103:
	ldr	r0, [r6, #1828]
	movs	r5, #0
	bl	NandcIqrWaitFlashReady
	addw	r8, r6, #1660
	mov	r7, r6
.L2066:
	ldrb	r3, [r6, #1616]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L2104
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2067
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbz	r3, .L2067
	lsls	r3, r5, #4
	ldrb	r0, [r8, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2067:
	adds	r5, r5, #1
	b	.L2066
.L2104:
	ldr	r3, [sp, #20]
	cbnz	r3, .L2069
.L2077:
	movs	r0, #0
	b	.L2052
.L2069:
	ldr	r7, .L2105+12
	mov	r8, #0
	mov	r10, r7
.L2070:
	ldr	r3, [sp, #8]
	cmp	r8, r3
	beq	.L2077
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L2071
	ldr	r1, [r4, #4]
	ldr	r0, .L2105+16
	bl	printk
	b	.L2072
.L2071:
	ldr	r3, [sp, #8]
	mov	r1, r9
	add	r2, sp, #28
	mov	r0, r4
	rsb	r3, r8, r3
	mov	r6, r4
	add	r5, sp, #36
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r7, #4024]
	movs	r3, #0
	str	r3, [r2]
	ldr	r2, [r7, #4028]
	str	r3, [r2]
	ldmia	r6!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	ldmia	r6!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	add	r0, sp, #36
	ldr	r3, [r6]
	movs	r1, #1
	mov	r2, r9
	str	r3, [r5]
	ldr	r3, [r7, #4024]
	str	r3, [sp, #44]
	ldr	r3, [r7, #4028]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r5, [sp, #36]
	adds	r3, r5, #1
	bne	.L2073
	ldr	r0, .L2105+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r5, [r4]
.L2073:
	ldr	r3, [r4, #12]
	cbz	r3, .L2074
	ldr	r2, [r3]
	ldr	r3, [r10, #4028]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2074
	ldr	r0, .L2105+24
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2074:
	ldr	r3, [r4, #8]
	cbz	r3, .L2072
	ldr	r2, [r3]
	ldr	r3, [r10, #4024]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2072
	ldr	r0, .L2105+28
	ldr	r1, [r4, #4]
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2072:
	add	r8, r8, #1
	adds	r4, r4, #36
	b	.L2070
.L2052:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2106:
	.align	2
.L2105:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1910
	.word	.LANCHOR2
	.word	.LC104
	.word	.LC105
	.word	.LC106
	.word	.LC107
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
	ldr	r3, .L2110
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, r0
	ldr	r2, [r3, #4008]
	.pad #108
	sub	sp, sp, #108
	cmp	r0, r2
	bcc	.L2109
	ldr	r5, [r3, #4016]
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
	add	r0, sp, #4
	str	r4, [sp, #8]
	mov	r2, r1
	bl	FlashEraseBlocks
	movs	r1, #1
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldr	r4, [sp, #4]
	add	r0, sp, #4
	movs	r1, #0
	adds	r4, r4, #0
	mov	r2, #1
	it	ne
	movne	r4, #1
	negs	r4, r4
	bl	FlashEraseBlocks
	b	.L2108
.L2109:
	movs	r4, #0
.L2108:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, pc}
.L2111:
	.align	2
.L2110:
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
	ldr	r5, .L2170
	movs	r1, #1
	ldr	r4, .L2170+4
	ldr	r0, .L2170+8
	ldr	r3, [r5, #4020]
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
	ldr	r4, .L2170+4
	cmp	r3, #1
	itttt	eq
	ldreq	r3, [sp, #16]
	lsleq	r3, r3, #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r5, #4020]
	ldr	r5, .L2170
	mov	r1, #4096
	bl	__memzero
	lsr	r3, r8, #4
	str	r3, [sp, #28]
	movs	r3, #0
	str	r3, [sp, #8]
.L2114:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #1616]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2166
	add	r3, r5, r7, lsl #1
	ldrh	r6, [r3, #4036]
	cmp	r6, #0
	bne	.L2144
	ldrh	r2, [r4, #1884]
	mov	r1, r6
	ldr	r0, [r5, #3988]
	mov	r9, r6
	lsls	r2, r2, #9
	bl	ftl_memset
	adds	r3, r4, r7
	str	r6, [sp, #4]
	ldrb	r10, [r3, #1620]	@ zero_extendqisi2
	lsls	r3, r7, #2
	add	fp, r4, r3
	str	r3, [sp, #32]
.L2116:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L2126
	movs	r3, #255
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	lsls	r1, r3, #31
	bpl	.L2118
	ldr	r3, [fp, #1628]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r3, r6
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4]	@ zero_extendqisi2
	ldr	r3, [sp, #36]
	cmp	r2, #1
	bne	.L2118
	ldr	r1, [r4, #4]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r1, r3
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	ands	r3, r3, r2
	strb	r3, [sp, #42]
.L2118:
	ldr	r3, [sp, #24]
	lsls	r2, r3, #30
	bpl	.L2120
	ldr	r3, [r4, #1788]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1628]
	subs	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2120:
	ldr	r3, [r4, #1788]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L2121
	cmp	r2, #8
	bne	.L2122
.L2121:
	cbz	r3, .L2143
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	clz	r0, r0
	lsrs	r0, r0, #5
	b	.L2123
.L2122:
	cmp	r3, #255
	bne	.L2143
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	it	ne
	movne	r0, #1
	b	.L2123
.L2143:
	movs	r0, #1
.L2123:
	ldr	r3, [sp, #24]
	lsls	r3, r3, #29
	bpl	.L2124
	ldr	r3, .L2170+4
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #1628]
	add	r1, r1, r6
	bl	SandiskProgTestBadBlock
.L2124:
	cbz	r0, .L2125
	mov	r1, r7
	ldr	r2, [sp, #4]
	ldr	r0, .L2170+12
	add	r9, r9, #1
	bl	printk
	ldr	r3, [sp, #12]
	ldr	r2, [r5, #3988]
	uxth	r9, r9
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
	bgt	.L2126
.L2125:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r6, r6, r3
	b	.L2116
.L2126:
	mov	r2, r9
	ldr	r0, .L2170+16
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #1616]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	muls	r3, r2, r3
	cmp	r9, r3
	blt	.L2128
	ldrh	r2, [r4, #1884]
	movs	r1, #0
	ldr	r0, [r5, #3988]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2128:
	cmp	r7, #0
	bne	.L2130
	ldrh	r10, [r5, #4008]
	mov	r9, #1
	mov	r6, r7
.L2131:
	ldr	r3, .L2170+4
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2167
	mov	r0, r10
	bl	FlashTestBlk
	cbz	r0, .L2132
	mov	r1, r10
	ldr	r0, .L2170+20
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
.L2132:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2131
.L2167:
	add	fp, r8, #-1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L2134:
	cmp	fp, r9
	ble	.L2168
	mov	r0, fp
	bl	FlashTestBlk
	cbz	r0, .L2135
	mov	r1, fp
	ldr	r0, .L2170+20
	bl	printk
	ldr	r2, [r5, #3988]
	lsr	r0, fp, #5
	and	r3, fp, #31
	lsl	r3, r10, r3
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2135:
	add	fp, fp, #-1
	uxth	fp, fp
	b	.L2134
.L2168:
	ldr	r3, .L2170+4
	ldr	r2, [r5, #4008]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	subs	r3, r3, r2
	cmp	r6, r3
	bcc	.L2130
	ldrh	r2, [r4, #1884]
	movs	r1, #0
	ldr	r0, [r5, #3988]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2130:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	add	r10, r8, #-1
	add	r9, r5, r7, lsl #1
	uxth	r10, r10
	mul	r6, r8, r6
.L2138:
	mov	r1, r7
	ldr	r0, .L2170+24
	mov	r2, r10
	bl	printk
	ldr	r1, [r5, #3988]
.L2139:
	lsr	r3, r10, #5
	and	r2, r10, #31
	ldr	r3, [r1, r3, lsl #2]
	lsrs	r3, r3, r2
	ands	r3, r3, #1
	beq	.L2169
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2139
.L2169:
	ldr	r1, [sp, #20]
	movw	r2, #61664
	strh	r10, [r9, #4036]	@ movhi
	add	r0, sp, #44
	strh	r2, [r1]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	movs	r1, #1
	ldr	r3, [r5, #3988]
	mov	r2, r1
	str	r3, [sp, #52]
	ldr	r3, [r5, #4020]
	str	r3, [sp, #56]
	add	r3, r10, r6
	lsls	r3, r3, #10
	str	r3, [sp, #48]
	bl	FlashEraseBlocks
	movs	r1, #1
	add	r0, sp, #44
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [sp, #44]
	cbz	r3, .L2144
	add	r10, r10, #-1
	uxth	r10, r10
	b	.L2138
.L2144:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2114
.L2166:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2171:
	.align	2
.L2170:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC109
	.word	.LC110
	.word	.LC111
	.word	.LC112
	.word	.LC113
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
	ldr	r7, .L2224
	str	r0, [sp, #4]
	str	r1, [sp]
	ldrb	r3, [r7, #2828]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r3, .L2224+4
	ldr	r2, [r3, #744]
	mov	fp, r3
	cmp	r2, #0
	bne	.L2201
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	mov	r8, #36
	mov	r9, r2
	mov	r5, r2
	mov	r4, r2
	mov	r10, fp
	str	r3, [sp, #8]
	str	r0, [fp, #1752]
.L2174:
	ldrh	r0, [r7, #2840]
	uxth	r1, r9
	ldr	r3, .L2224
	cmp	r0, r1
	bls	.L2219
	mul	r6, r8, r1
	ldr	r0, [fp, #748]
	movs	r3, #0
	str	r3, [r0, r6]
	ldr	r3, .L2224
	add	r1, r1, r3
	ldrb	r0, [r1, #2868]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	bl	V2P_block
	ldr	r3, [sp]
	mov	r6, r0
	cbz	r3, .L2175
	bl	IsBlkInVendorPart
	cbnz	r0, .L2176
.L2175:
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2177
	ldr	r3, [fp, #748]
	lsls	r6, r6, #10
	ldrh	r1, [r7, #2922]
	muls	r1, r5, r1
	it	mi
	addmi	r1, r1, #3
	mla	lr, r8, r5, r3
	adds	r5, r5, #1
	bic	r1, r1, #3
	uxth	r5, r5
	str	r0, [lr, #8]
	ldr	r0, [r10, #1804]
	str	r6, [lr, #4]
	add	r1, r1, r0
	str	r1, [lr, #12]
	b	.L2176
.L2177:
	adds	r4, r4, #1
	uxth	r4, r4
.L2176:
	add	r9, r9, #1
	b	.L2174
.L2219:
	cmp	r5, #0
	beq	.L2200
	ldr	r2, [sp, #8]
	mov	r8, #0
	ldr	r0, [fp, #748]
	mov	r9, #36
	adds	r6, r2, #0
	mov	r2, r5
	it	ne
	movne	r6, #1
	strb	r8, [r3, #2828]
	str	r3, [sp, #8]
	mov	r1, r6
	bl	FlashEraseBlocks
	ldrb	r2, [sp, #16]	@ zero_extendqisi2
	ldr	r3, [sp, #8]
	strb	r2, [r3, #2828]
.L2181:
	uxth	r2, r8
	cmp	r2, r5
	bcs	.L2220
	mul	r2, r9, r8
	ldr	r1, [fp, #748]
	adds	r0, r1, r2
	ldr	r2, [r1, r2]
	adds	r2, r2, #1
	bne	.L2182
	ldr	r0, [r0, #4]
	adds	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2182:
	add	r8, r8, #1
	b	.L2181
.L2220:
	ldr	r3, [sp]
	cbnz	r3, .L2184
	movs	r3, #6
	uxth	r6, r6
	str	r3, [sp, #12]
	movs	r3, #1
	str	r3, [sp, #8]
	b	.L2185
.L2184:
	ldrh	r3, [r7, #2912]
	str	r3, [sp, #8]
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbnz	r3, .L2202
	ldr	r3, [sp, #8]
	movs	r6, #1
	lsrs	r3, r3, #2
	str	r3, [sp, #12]
	b	.L2185
.L2202:
	movs	r6, #1
	str	r6, [sp, #12]
.L2185:
	ldr	r8, .L2224+4
	movs	r7, #0
	mov	r10, r8
.L2195:
	mov	fp, #0
	mov	r5, fp
.L2186:
	ldr	r9, .L2224
	uxth	r3, fp
	ldrh	r1, [r9, #2840]
	cmp	r1, r3
	bls	.L2221
	movs	r2, #36
	ldr	r0, [r8, #748]
	mul	r1, r2, r3
	movs	r2, #0
	str	r2, [r0, r1]
	ldr	r2, .L2224
	ldr	r1, [sp, #4]
	add	r3, r3, r2
	ldrb	r0, [r3, #2868]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp]
	mov	r9, r0
	cbz	r3, .L2187
	bl	IsBlkInVendorPart
	cbnz	r0, .L2188
.L2187:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2188
	ldr	r1, [r10, #748]
	movs	r3, #36
	ldr	r0, [r8, #1796]
	mla	r1, r3, r5, r1
	add	r3, r7, r9, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [r10, #1792]
	str	r3, [r1, #8]
	ldr	r3, .L2224
	ldrh	r3, [r3, #2922]
	muls	r3, r5, r3
	add	r5, r5, #1
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	uxth	r5, r5
	add	r3, r3, r0
	str	r3, [r1, #12]
.L2188:
	add	fp, fp, #1
	b	.L2186
.L2221:
	cmp	r5, #0
	beq	.L2200
	mov	r2, r6
	movs	r3, #1
	ldr	r0, [r8, #748]
	mov	r1, r5
	mov	fp, #0
	strb	fp, [r9, #2828]
	bl	FlashProgPages
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	movs	r2, #36
	strb	r3, [r9, #2828]
	ldr	r9, .L2224+4
.L2192:
	uxth	r3, fp
	cmp	r3, r5
	bcs	.L2222
	mul	r3, r2, fp
	ldr	r1, [r10, #748]
	adds	r0, r1, r3
	ldr	r3, [r1, r3]
	cbz	r3, .L2193
	ldr	r0, [r0, #4]
	adds	r4, r4, #1
	str	r2, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #20]
.L2193:
	add	fp, fp, #1
	b	.L2192
.L2222:
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	ldr	r3, [sp, #8]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L2195
	movs	r7, #0
	mov	r8, #36
.L2196:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L2223
	ldr	r3, [sp]
	cbz	r3, .L2197
	mul	r3, r8, r7
	ldr	r2, [r9, #748]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L2197
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2197:
	adds	r7, r7, #1
	b	.L2196
.L2223:
	ldr	r3, [sp, #4]
	cmp	r3, #63
	bls	.L2199
	ldr	r3, [sp]
	cbz	r3, .L2200
.L2199:
	ldr	r0, [r9, #748]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L2200:
	mov	r0, r4
	b	.L2173
.L2201:
	movs	r0, #0
.L2173:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2225:
	.align	2
.L2224:
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
	ldr	r4, .L2238
	ldr	r5, [r4, #744]
	cmp	r5, #0
	bne	.L2228
	ldr	r7, .L2238+4
	mov	r1, r5
	ldr	r0, [r4, #1776]
	ldr	r3, [r4, #1800]
	addw	r8, r7, #2996
	ldrh	r2, [r7, #2920]
	str	r0, [r4, #4060]
	str	r3, [r4, #4064]
	bl	ftl_memset
.L2229:
	ldrh	r3, [r7, #2862]
	ldr	r6, .L2238+4
	cmp	r5, r3
	bge	.L2237
	ldrh	r2, [r4, #1864]
	ldr	r3, [r4, #4060]
	ldr	r1, [r8, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L2229
.L2237:
	ldr	r8, [r4, #4064]
	movs	r1, #255
	movs	r2, #16
	mov	r9, #0
	ldr	r10, .L2238
	mov	r5, r6
	mov	r0, r8
	mov	fp, r9
	bl	memset
	movw	r3, #61649
	strh	r3, [r8]	@ movhi
	ldr	r3, [r6, #2980]
	str	r3, [r8, #4]
	ldrh	r3, [r6, #2972]
	strh	r3, [r8, #2]	@ movhi
	ldrh	r3, [r6, #2976]
	strh	r3, [r8, #8]	@ movhi
	ldrh	r3, [r6, #2978]
	strh	r3, [r8, #10]	@ movhi
	ldr	r3, [r6, #2836]
	strh	r3, [r8, #12]	@ movhi
.L2231:
	ldr	r3, [r4, #1776]
	movs	r6, #0
	ldrh	r1, [r7, #2972]
	ldrh	r2, [r7, #2974]
	str	r3, [r4, #4060]
	ldr	r3, [r4, #1800]
	str	r6, [r4, #4052]
	str	r3, [r4, #4064]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r8, #10]
	str	r3, [r4, #4056]
	ldrh	r3, [r7, #2976]
	str	r0, [sp]
	ldr	r0, .L2238+8
	bl	printk
	movs	r1, #1
	ldr	r0, .L2238+12
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r7, #2912]
	ldrh	r2, [r7, #2974]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L2232
	ldr	r3, [r5, #2980]
	movs	r1, #1
	ldrh	r2, [r5, #2972]
	adds	r3, r3, #1
	ldr	r0, [r10, #748]
	str	r3, [r5, #2980]
	str	r3, [r8, #4]
	ldrh	r3, [r5, #2976]
	strh	r2, [r8, #8]	@ movhi
	strh	r2, [r5, #2976]	@ movhi
	mov	r2, r1
	strh	r3, [r5, #2972]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r10, #4056]
	str	r3, [r0, #4]
	strh	r6, [r5, #2974]	@ movhi
	bl	FlashEraseBlocks
	movs	r1, #1
	ldr	r0, .L2238+12
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L2232:
	ldrh	r3, [r7, #2974]
	ldr	r6, .L2238
	adds	r3, r3, #1
	strh	r3, [r7, #2974]	@ movhi
	ldr	r3, [r4, #4052]
	adds	r3, r3, #1
	bne	.L2233
	add	r9, r9, #1
	ldr	r0, .L2238+16
	ldr	r1, [r10, #4056]
	uxth	r9, r9
	bl	printk
	cmp	r9, #3
	bls	.L2231
	ldr	r0, .L2238+20
	mov	r2, r9
	ldr	r1, [r6, #4056]
	bl	printk
	movs	r3, #1
	str	r3, [r6, #744]
	b	.L2228
.L2233:
	cmp	fp, #0
	bne	.L2228
	mov	fp, #1
	b	.L2231
.L2228:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2239:
	.align	2
.L2238:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC114
	.word	.LANCHOR2+4052
	.word	.LC115
	.word	.LC116
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
	ldr	r4, .L2296
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r4, #744]
	cmp	r3, #0
	bne	.L2241
	ldr	r10, .L2296+16
	mov	r8, r4
.L2242:
	ldr	r3, .L2296+4
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r3
	bne	.L2243
	ldrh	r7, [r8, #832]
	ldr	r6, [r8, #1648]
	lsrs	r1, r7, #1
	mul	r0, r6, r7
	adds	r3, r1, #1
	add	r3, r3, r0, lsr #2
	ldr	r0, [r10, #2832]
	uxth	r3, r3
	cbz	r0, .L2266
	ldr	r0, [r8, #1052]
	cmp	r0, #29
	bhi	.L2266
	cmp	r0, #2
	bls	.L2271
	lsls	r3, r7, #31
	bpl	.L2244
	cbz	r6, .L2268
	b	.L2244
.L2243:
	cmp	r2, #1
	bne	.L2271
	ldrh	r3, [r10, #2860]
	cmp	r3, #1
	beq	.L2271
	ldrb	r3, [r10, #76]	@ zero_extendqisi2
	cbnz	r3, .L2271
	ldrh	r0, [r8, #832]
	ldr	r3, [r10, #2832]
	lsrs	r1, r0, #3
	cbz	r3, .L2244
	ldr	r3, [r8, #1052]
	cmp	r3, #1
	bhi	.L2244
	rsb	r0, r0, r0, lsl #3
	ubfx	r1, r0, #3, #16
	b	.L2244
.L2266:
	mov	r1, r3
.L2244:
	cbz	r1, .L2245
	subs	r1, r1, #1
	uxth	r1, r1
	b	.L2245
.L2268:
	mov	r1, r6
	b	.L2245
.L2271:
	movs	r1, #0
.L2245:
	ldr	r0, .L2296+8
	bl	List_pop_index_node
	ldrh	r3, [r4, #832]
	subs	r3, r3, #1
	strh	r3, [r4, #832]	@ movhi
	uxth	r6, r0
	mov	r0, r5
	strh	r6, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbz	r3, .L2246
	ldr	r0, [r8, #748]
	movs	r2, #36
	ldrh	r7, [r10, #2840]
	add	lr, r5, #14
	ldr	r9, .L2296+16
	mov	r3, r0
	mla	r1, r2, r7, r0
	movs	r7, #0
	mov	ip, r7
	str	r1, [sp]
	b	.L2247
.L2246:
	ldr	r3, [r8, #816]
	b	.L2291
.L2247:
	ldr	r1, [sp]
	cmp	r3, r1
	beq	.L2293
	str	ip, [r3, #8]
	movw	fp, #65535
	str	ip, [r3, #12]
	adds	r3, r3, #36
	ldrh	r1, [lr, #2]!
	cmp	r1, fp
	itttt	ne
	lslne	r1, r1, #10
	mlane	fp, r2, r7, r0
	addne	r7, r7, #1
	uxthne	r7, r7
	it	ne
	strne	r1, [fp, #4]
	b	.L2247
.L2293:
	ldr	r3, [r9, #2832]
	cbz	r3, .L2251
	ldr	r3, .L2296+12
	cmp	r5, r3
	bne	.L2251
	ldr	r3, [r8, #752]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #30
	itt	hi
	movhi	r3, #0
	strbhi	r3, [r8, #844]
.L2251:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #752]
	cbnz	r3, .L2252
	ldrh	r1, [r2, r6, lsl #1]
	cbz	r1, .L2272
	ldrh	r3, [r9, #2900]
	add	r3, r3, r1
	uxth	r3, r3
	b	.L2253
.L2272:
	movs	r3, #2
.L2253:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	mov	r0, r6
	ldr	r3, [r4, #1036]
	movs	r1, #0
	adds	r3, r3, #1
	str	r3, [r4, #1036]
	bl	ftl_set_blk_mode
	b	.L2254
.L2252:
	ldrh	r3, [r2, r6, lsl #1]
	lsrs	r0, r6, #5
	and	r1, r6, #31
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #1040]
	ldr	r2, [r4, #992]
	adds	r3, r3, #1
	str	r3, [r4, #1040]
	movs	r3, #1
	lsls	r3, r3, r1
	ldr	r1, [r2, r0, lsl #2]
	orrs	r3, r3, r1
	str	r3, [r2, r0, lsl #2]
.L2254:
	ldr	r3, [r4, #752]
	ldr	r2, [r4, #1048]
	ldr	r0, [r4, #1036]
	ldrh	r3, [r3, r6, lsl #1]
	ldrh	r1, [r9, #2848]
	cmp	r3, r2
	ldrh	r2, [r9, #2900]
	it	hi
	strhi	r3, [r8, #1048]
	ldr	r3, [r4, #1040]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #1820]
	ldr	r1, [r4, #748]
	ldr	r3, [r2, #16]
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r3, #36
	adds	r2, r1, #4
	mla	r3, r3, r7, r1
	adds	r3, r3, #40
	str	r0, [r4, #1044]
.L2256:
	adds	r2, r2, #36
	cmp	r2, r3
	beq	.L2294
	ldr	r1, [r2, #-36]
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r2, #-36]
	b	.L2256
.L2294:
	ldrb	r3, [r9, #76]	@ zero_extendqisi2
	cbz	r3, .L2258
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #748]
	cmp	r3, #1
	ite	eq
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2258:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #748]
	mov	fp, #0
	bl	FlashEraseBlocks
	add	r1, r5, #16
	mov	ip, #36
	mov	r2, fp
.L2260:
	uxth	r3, fp
	cmp	r3, r7
	bcs	.L2295
	mul	r3, ip, fp
	ldr	lr, [r4, #748]
	add	r0, lr, r3
	ldr	r3, [lr, r3]
	cmp	r3, #-1
	bne	.L2261
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
.L2261:
	add	fp, fp, #1
	adds	r1, r1, #2
	b	.L2260
.L2295:
	cbz	r2, .L2263
	mov	r0, r6
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2263:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L2264
	ldr	r3, [r4, #816]
.L2291:
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L2242
.L2264:
	ldrh	r2, [r9, #2910]
	strh	r6, [r5]	@ movhi
	smulbb	r3, r2, r3
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #1028]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #1028]
	ldr	r2, [r4, #816]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
.L2241:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2297:
	.align	2
.L2296:
	.word	.LANCHOR2
	.word	.LANCHOR2+932
	.word	.LANCHOR2+828
	.word	.LANCHOR2+836
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
	ldr	r5, .L2311
	str	r0, [sp]
	ldrh	r3, [r5, #1694]
	cmp	r3, #0
	beq	.L2300
	ldr	r7, .L2311+4
	mov	r10, #0
	mov	r8, r5
.L2299:
	ldrh	r2, [r7, #2840]
	uxth	r3, r10
	cmp	r2, r3
	bls	.L2308
	add	r3, r3, r7
	ldr	r1, [sp]
	mov	fp, #0
	ldrb	r0, [r3, #2868]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r2, .L2311+8
	mov	r9, r0
.L2301:
	ldrh	r3, [r5, #1694]
	uxth	r4, fp
	cmp	r3, r4
	bls	.L2309
	lsls	r6, r4, #1
	add	r3, r8, r6
	ldrh	r3, [r3, #1696]
	cmp	r3, r9
	bne	.L2302
	mov	r1, r9
	ldr	r0, .L2311+12
	str	r2, [sp, #4]
	bl	printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r2, [sp, #4]
	ldrh	r1, [r8, #1694]
	adds	r3, r2, r6
.L2303:
	cmp	r4, r1
	bcs	.L2310
	ldrh	r0, [r3, #2]
	adds	r4, r4, #1
	uxth	r4, r4
	strh	r0, [r3], #2	@ movhi
	b	.L2303
.L2310:
	subs	r1, r1, #1
	strh	r1, [r5, #1694]	@ movhi
.L2302:
	add	fp, fp, #1
	b	.L2301
.L2309:
	add	r10, r10, #1
	b	.L2299
.L2308:
	bl	FtlGcReFreshBadBlk
.L2300:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2312:
	.align	2
.L2311:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+1696
	.word	.LC117
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
	ldr	r2, .L2322
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, [r2, #816]
	mov	r5, r2
	ldrh	r3, [r3, r0, lsl #1]
	cbnz	r3, .L2314
	ldrh	r1, [r2, #1072]
	cmp	r1, r0
	bne	.L2315
	movw	r3, #65535
	strh	r3, [r2, #1072]	@ movhi
	b	.L2316
.L2315:
	ldrh	r1, [r2, #836]
	cmp	r1, r0
	beq	.L2321
	ldrh	r1, [r2, #884]
	cmp	r1, r0
	beq	.L2321
	ldrh	r2, [r2, #932]
	cmp	r2, r0
	beq	.L2321
.L2316:
	mov	r1, r4
	ldr	r0, .L2322+4
	bl	List_remove_node
	ldrh	r3, [r5, #824]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #824]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	movs	r0, #1
	pop	{r3, r4, r5, pc}
.L2314:
	bl	List_update_data_list
.L2321:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L2323:
	.align	2
.L2322:
	.word	.LANCHOR2
	.word	.LANCHOR2+812
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
	ldr	r3, .L2330
	beq	.L2325
	ldr	r2, [r3, #816]
	ldrh	r4, [r2, r0, lsl #1]
	cbnz	r4, .L2326
	mov	r1, r5
	mov	r2, r4
	ldr	r0, .L2330+4
	bl	printk
	mov	r0, r4
	pop	{r3, r4, r5, pc}
.L2326:
	subs	r4, r4, #1
	strh	r4, [r2, r0, lsl #1]	@ movhi
.L2325:
	ldrh	r0, [r3, #1920]
	movw	r3, #65535
	ldr	r4, .L2330
	cmp	r0, r3
	bne	.L2328
	strh	r5, [r4, #1920]	@ movhi
	b	.L2329
.L2328:
	cmp	r0, r5
	beq	.L2329
	bl	update_vpc_list
	strh	r5, [r4, #1920]	@ movhi
	adds	r0, r0, #0
	it	ne
	movne	r0, #1
	pop	{r3, r4, r5, pc}
.L2329:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L2331:
	.align	2
.L2330:
	.word	.LANCHOR2
	.word	.LC118
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
	beq	.L2332
	ldrh	r3, [r0]
	movw	r5, #65535
	cmp	r3, r5
	beq	.L2332
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movs	r7, #0
	ldr	r6, .L2344
	adds	r3, r3, #8
	ldrh	r3, [r0, r3, lsl #1]
.L2336:
	cmp	r3, r5
	bne	.L2343
.L2338:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r6, #2840]
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
	b	.L2336
.L2343:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2339
	ldrb	r3, [r6, #76]	@ zero_extendqisi2
	cbnz	r3, .L2339
	ldrh	r3, [r4, #2]
	add	r3, r6, r3, lsl #1
	ldrh	r3, [r3, #592]
	cmp	r3, r5
	bne	.L2339
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2338
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L2339:
	ldr	r2, .L2344
	ldrb	r3, [r2, #76]	@ zero_extendqisi2
	cbz	r3, .L2332
	cmp	r1, #1
	bne	.L2332
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #2912]
	cmp	r1, r3
	bcc	.L2332
	ldr	r3, .L2344+4
	ldrh	r0, [r4]
	ldrh	r5, [r4, #4]
	ldr	r1, [r3, #816]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r5
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #2910]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2332:
	pop	{r3, r4, r5, r6, r7, pc}
.L2345:
	.align	2
.L2344:
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
	ldr	r5, .L2363
	ldrh	r2, [r0, r3, lsl #1]
.L2347:
	cmp	r2, r8
	ldr	r7, .L2363
	bne	.L2362
.L2348:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r5, #2840]
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
	b	.L2347
.L2362:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r1, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2350
	ldrb	r0, [r5, #76]	@ zero_extendqisi2
	cbnz	r0, .L2350
	add	r0, r5, r1, lsl #1
	ldrh	r0, [r0, #592]
	cmp	r0, r8
	bne	.L2350
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2348
.L2350:
	movw	r6, #65535
	orr	r5, r1, r2, lsl #10
	mov	r8, r6
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2351:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r1, [r7, #2840]
.L2353:
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
	beq	.L2353
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2358
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbnz	r3, .L2355
	ldrh	r2, [r4, #2]
	ldr	r3, .L2363
	add	r3, r3, r2, lsl #1
	ldrh	r3, [r3, #592]
	cmp	r3, r8
	bne	.L2355
	ldrh	r3, [r4, #4]
	cbz	r3, .L2355
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2351
.L2355:
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	ldr	r2, .L2363
	cbz	r3, .L2358
	ldrh	r1, [r4, #2]
	ldrh	r3, [r2, #2912]
	cmp	r1, r3
	bcc	.L2358
	ldr	r3, .L2363+4
	ldrh	r0, [r4]
	ldrh	r6, [r4, #4]
	ldr	r1, [r3, #816]
	ldrh	r3, [r1, r0, lsl #1]
	subs	r3, r3, r6
	strh	r3, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r2, #2910]
	movs	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2358:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L2364:
	.align	2
.L2363:
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
	ldr	r4, .L2384
	ldr	r3, [r4, #744]
	cmp	r3, #0
	bne	.L2367
	ldr	r2, [r4, #1776]
	movs	r1, #255
	ldr	r7, [r4, #1800]
	ldr	r5, .L2384+4
	str	r2, [r4, #4060]
	ldrh	r2, [r4, #1060]
	str	r7, [r4, #4064]
	str	r3, [r7, #12]
	strh	r2, [r7, #2]	@ movhi
	movw	r2, #61604
	strh	r2, [r7]	@ movhi
	ldr	r2, [r4, #1068]
	str	r3, [r7, #8]
	ldr	r3, .L2384+8
	str	r2, [r7, #4]
	ldrh	r2, [r4, #838]
	str	r3, [r4, #760]
	ldr	r3, .L2384+12
	str	r3, [r4, #764]
	ldrh	r3, [r4, #1066]
	strh	r3, [r4, #768]	@ movhi
	ldrh	r3, [r5, #2862]
	strb	r3, [r4, #770]
	ldrh	r3, [r4, #836]
	strh	r3, [r4, #774]	@ movhi
	ldrb	r3, [r4, #842]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #776]	@ movhi
	ldrb	r3, [r4, #844]	@ zero_extendqisi2
	ldrh	r2, [r4, #886]
	strb	r3, [r4, #771]
	ldrh	r3, [r4, #884]
	strh	r3, [r4, #778]	@ movhi
	ldrb	r3, [r4, #890]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #780]	@ movhi
	ldrb	r3, [r4, #892]	@ zero_extendqisi2
	strb	r3, [r4, #772]
	ldrh	r3, [r4, #932]
	strh	r3, [r4, #782]	@ movhi
	ldrh	r2, [r4, #934]
	ldrb	r3, [r4, #938]	@ zero_extendqisi2
	ldr	r0, [r4, #4060]
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #784]	@ movhi
	ldrb	r3, [r4, #940]	@ zero_extendqisi2
	ldrh	r2, [r5, #2920]
	strb	r3, [r4, #773]
	ldr	r3, [r4, #1036]
	str	r3, [r4, #792]
	ldr	r3, [r4, #1028]
	str	r3, [r4, #800]
	ldr	r3, [r4, #1032]
	str	r3, [r4, #796]
	bl	ftl_memset
	add	r1, r4, #760
	movs	r2, #48
	ldr	r0, [r4, #4060]
	bl	ftl_memcpy
	ldrh	r2, [r5, #2848]
	ldr	r0, [r4, #4060]
	ldr	r1, [r4, #816]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5, #2848]
	ldr	r0, [r4, #4060]
	add	r3, r2, #24
	ldr	r1, [r4, #992]
	ubfx	r3, r3, #1, #30
	lsrs	r2, r2, #3
	adds	r2, r2, #4
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
	ldrh	r3, [r5, #2952]
	cbz	r3, .L2368
	ldrh	r2, [r5, #2848]
	ldr	r0, [r4, #4060]
	ldr	r1, [r4, #1848]
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	ldrh	r2, [r5, #2948]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	lsls	r2, r2, #2
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
.L2368:
	movs	r0, #0
	ldr	r6, .L2384
	bl	FtlUpdateVaildLpn
	mov	r8, #0
	movw	r9, #65535
.L2369:
	ldr	r3, [r4, #1776]
	movs	r1, #1
	ldrh	r2, [r4, #1060]
	ldr	r0, .L2384+16
	str	r3, [r4, #4060]
	ldr	r3, [r4, #1800]
	str	r3, [r4, #4064]
	ldrh	r3, [r4, #1062]
	orr	r3, r3, r2, lsl #10
	mov	r2, r1
	str	r3, [r4, #4056]
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r5, #2912]
	ldrh	r2, [r4, #1062]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L2370
	ldrh	r3, [r6, #1060]
	ldrh	r9, [r6, #1064]
	strh	r3, [r6, #1064]	@ movhi
	movs	r3, #0
	strh	r3, [r6, #1062]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r6, #1028]
	movs	r1, #1
	adds	r2, r3, #1
	str	r2, [r6, #1028]
	str	r3, [r6, #1068]
	lsls	r2, r0, #10
	strh	r0, [r6, #1060]	@ movhi
	str	r2, [r6, #4056]
	mov	r2, r1
	str	r3, [r7, #4]
	mov	r3, r1
	strh	r0, [r7, #2]	@ movhi
	ldr	r0, .L2384+16
	bl	FlashProgPages
.L2370:
	ldrh	r3, [r4, #1062]
	ldr	r2, [r4, #4052]
	adds	r3, r3, #1
	adds	r1, r2, #1
	uxth	r3, r3
	strh	r3, [r4, #1062]	@ movhi
	bne	.L2371
	cmp	r3, #1
	add	r8, r8, #1
	it	eq
	ldrheq	r3, [r5, #2912]
	uxth	r8, r8
	itt	eq
	addeq	r3, r3, #-1
	strheq	r3, [r6, #1062]	@ movhi
	cmp	r8, #3
	bls	.L2369
	ldr	r0, .L2384+20
	mov	r2, r8
	ldr	r1, [r4, #4056]
	bl	printk
	movs	r3, #1
	str	r3, [r4, #744]
	b	.L2367
.L2371:
	cmp	r3, #1
	beq	.L2369
	cmp	r2, #256
	beq	.L2369
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2367
	mov	r0, r9
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2367:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L2385:
	.align	2
.L2384:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177352
	.word	.LANCHOR2+4052
	.word	.LC119
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
	ldr	r7, .L2400
	.pad #40
	sub	sp, sp, #40
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbz	r3, .L2395
	ldrb	r5, [r0, #8]	@ zero_extendqisi2
	cmp	r5, #1
	bne	.L2395
	ldrh	r6, [r0, #4]
	b	.L2387
.L2395:
	movs	r5, #0
	movs	r6, #12
.L2387:
	ldr	r10, .L2400+4
	mov	r8, #-1
	mov	r9, #0
.L2388:
	adds	r6, r6, #-1
	bcc	.L2390
	ldrh	r3, [r4, #4]
	cbnz	r3, .L2389
.L2390:
	ldr	r3, .L2400+4
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldr	r2, [r3, #816]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r7, #2910]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2389:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L2390
	ldr	r2, [r10, #1800]
	add	r0, sp, #4
	ldr	r3, [r10, #1776]
	movs	r1, #1
	str	r8, [sp, #20]
	str	r2, [sp, #16]
	str	r3, [sp, #12]
	str	r8, [r2, #12]
	str	r8, [r2, #8]
	ldrh	r3, [r4]
	strh	r9, [r2]	@ movhi
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, [r10, #1032]
	str	r3, [r2, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, r5
	it	eq
	moveq	r3, #0
	str	r3, [r10, #1032]
	movs	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2388
.L2401:
	.align	2
.L2400:
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
	ldr	r7, .L2423
	bge	.L2403
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2405
	ldrh	r2, [r4, #2]
	ldrh	r3, [r7, #2912]
	cmp	r2, r3
	bcc	.L2405
.L2403:
	movw	r3, #65535
	cmp	r1, r3
	uxth	r5, r0
	beq	.L2407
	ldrh	r3, [r4, #2]
	ldrh	r2, [r7, #2912]
	cmp	r2, r3
	bhi	.L2407
	movs	r2, #0
.L2408:
	uxth	lr, r2
	cmp	lr, ip
	bcs	.L2409
	adds	r2, r2, #1
	add	r0, r6, r2, lsl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r1
	bne	.L2408
	mov	r5, lr
.L2409:
	ldr	r0, .L2423+4
	ldrh	r2, [r6, lr, lsl #1]
	bl	printk
	movw	r3, #65535
	strh	r3, [r4, #40]	@ movhi
.L2407:
	ldrh	r9, [r6, r5, lsl #1]
	lsls	r2, r5, #1
	cmp	r9, #0
	beq	.L2405
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2405
	movs	r1, #1
	str	r1, [r4, #32]
	strh	r3, [r6, r2]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r7, #2912]
	cmp	r2, r3
	bcc	.L2411
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2411:
	ldr	r5, .L2423+8
	mov	r10, #0
.L2412:
	ldrh	r3, [r4, #6]
	uxth	r6, r10
	cmp	r3, r6
	bls	.L2422
	ldr	r3, [r8, r6, lsl #2]
	cmp	r9, r3, lsr #10
	bne	.L2413
	ldr	r3, [r5, #1780]
	movs	r1, #1
	ldr	fp, [r5, #1800]
	ldr	r0, .L2423+12
	mov	r2, r1
	str	r3, [r5, #4060]
	str	fp, [r5, #4064]
	ldr	r3, [r8, r6, lsl #2]
	str	r3, [r5, #4056]
	bl	FlashReadPages
	ldr	r3, [r5, #4052]
	adds	r3, r3, #1
	bne	.L2414
	movs	r3, #0
	ldr	r0, .L2423+16
	str	r3, [r8, r6, lsl #2]
	ldr	r1, [r5, #4056]
	ldrh	r2, [fp, #8]
	bl	printk
	movs	r3, #1
	str	r3, [r5, #744]
	b	.L2413
.L2414:
	mov	r0, r4
	mov	r1, r6
	ldr	r2, [r5, #4060]
	bl	FtlMapWritePage
.L2413:
	add	r10, r10, #1
	b	.L2412
.L2422:
	mov	r0, r9
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L2405:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r7, #2912]
	cmp	r2, r3
	bcc	.L2416
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2416:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2424:
	.align	2
.L2423:
	.word	.LANCHOR0
	.word	.LC120
	.word	.LANCHOR2
	.word	.LANCHOR2+4052
	.word	.LC121
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
	ldr	r5, .L2432
	ldr	r7, [r0, #12]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #744]
	cmp	r6, #0
	bne	.L2426
	ldrh	r3, [r0]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L2427
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
	b	.L2426
.L2427:
	ldrh	r7, [r7, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r2, [r0, #28]
	orr	r3, r3, r7, lsl #10
	str	r3, [r5, #4056]
	ldr	r3, [r5, #1776]
	str	r3, [r5, #4060]
	ldr	r3, [r5, #1800]
	str	r3, [r5, #4064]
	str	r2, [r3, #4]
	movw	r2, #64245
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r3, .L2432+4
	ldr	r0, [r5, #1776]
	ldrh	r2, [r3, #2912]
	lsls	r2, r2, #3
	bl	ftl_memset
	mov	r3, r6
.L2428:
	ldrh	r1, [r4, #6]
	uxth	r2, r6
	cmp	r1, r2
	bls	.L2431
	ldr	r1, [r8, r2, lsl #2]
	cmp	r7, r1, lsr #10
	bne	.L2429
	adds	r3, r3, #1
	ldr	r1, [r5, #1776]
	uxth	r3, r3
	str	r2, [r1, r3, lsl #3]
	ldr	r1, [r8, r2, lsl #2]
	ldr	r2, [r5, #1776]
	add	r2, r2, r3, lsl #3
	str	r1, [r2, #4]
.L2429:
	adds	r6, r6, #1
	b	.L2428
.L2431:
	movs	r1, #1
	movs	r3, #0
	ldr	r0, .L2432+8
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2426:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2433:
	.align	2
.L2432:
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	mov	r5, r0
	ldr	r4, .L2455
	mov	r7, r1
	str	r2, [sp, #4]
	ldr	r6, [r4, #744]
	cmp	r6, #0
	bne	.L2450
	ldr	r8, .L2455+16
	mov	fp, r4
	mov	r10, r8
.L2436:
	ldr	r3, [r4, #1012]
	adds	r3, r3, #1
	str	r3, [r4, #1012]
	ldrh	r3, [r8, #2912]
	ldrh	r2, [r5, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L2437
	ldrh	r2, [r5]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2438
.L2437:
	mov	r0, r5
	bl	Ftl_write_map_blk_to_last_page
.L2438:
	ldrh	r2, [r5]
	movs	r1, #16
	ldr	r3, [r5, #12]
	ldr	r0, [r4, #1800]
	ldrh	r9, [r3, r2, lsl #1]
	ldrh	r3, [r5, #2]
	str	r0, [r4, #4064]
	orr	r3, r3, r9, lsl #10
	str	r3, [r4, #4056]
	ldr	r3, [sp, #4]
	str	r3, [r4, #4060]
	bl	__memzero
	ldr	r3, [r4, #4064]
	movs	r1, #1
	ldr	r2, [r5, #28]
	ldr	r0, .L2455+4
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
	ldr	r2, [r4, #4052]
	adds	r1, r2, #1
	bne	.L2439
	ldr	r0, .L2455+8
	adds	r6, r6, #1
	ldr	r1, [fp, #4056]
	bl	printk
	ldrh	r3, [r5, #2]
	uxth	r6, r6
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r10, #2912]
	addls	r3, r3, #-1
	strhls	r3, [r5, #2]	@ movhi
	cmp	r6, #3
	bls	.L2436
	ldr	r0, .L2455+12
	mov	r2, r6
	ldr	r1, [r4, #4056]
	bl	printk
	movs	r3, #1
	str	r3, [r4, #744]
	b	.L2450
.L2439:
	cbz	r2, .L2442
	cmp	r3, #1
	strh	r9, [r5, #40]	@ movhi
	beq	.L2436
	cmp	r2, #256
	b	.L2454
.L2442:
	cmp	r3, #1
.L2454:
	beq	.L2436
	ldr	r2, [r4, #4056]
	ldr	r3, [r5, #24]
	str	r2, [r3, r7, lsl #2]
.L2450:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2456:
	.align	2
.L2455:
	.word	.LANCHOR2
	.word	.LANCHOR2+4052
	.word	.LC122
	.word	.LC123
	.word	.LANCHOR0
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
	ldr	r5, .L2458
	muls	r4, r0, r4
	addw	r0, r5, #1868
	ldr	r3, [r5, #980]
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #980]
	movs	r0, #0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L2459:
	.align	2
.L2458:
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
	ldr	r5, .L2508
	str	r3, [sp, #12]
	lsl	r2, fp, #2
	ldrh	r3, [r0, #8]
	ldr	r8, [r0, #12]
	mov	r0, r10
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, [r5, #1776]
	ldr	r7, [r5, #1800]
	str	r6, [r4, #32]
	str	r3, [r5, #4060]
	movw	r3, #65535
	str	r7, [r5, #4064]
	str	r6, [r4, #28]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #1
	str	r3, [r4, #36]
.L2461:
	uxth	ip, r6
	ldr	r3, [sp, #8]
	sxth	r9, ip
	cmp	r9, r3
	bge	.L2478
	ldr	r3, [sp, #8]
	subs	r2, r3, #1
	cmp	r9, r2
	lsl	r2, r9, #1
	bne	.L2462
	ldrh	r0, [r8, r9, lsl #1]
	movs	r1, #1
	str	ip, [sp, #8]
	add	r6, r8, r2
	bl	FtlGetLastWrittenPage
	ldr	ip, [sp, #8]
	strh	ip, [r4]	@ movhi
	mov	ip, #0
	adds	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [sp, #12]
	sxth	r0, r0
	ldr	r3, [r3, r9, lsl #2]
	add	r9, r0, #1
	str	r3, [r4, #28]
	ldr	r3, .L2508
.L2463:
	sxth	r2, ip
	cmp	r2, r9
	bge	.L2478
	ldrh	r1, [r6]
	ldr	r0, .L2508+4
	str	r3, [sp, #12]
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r5, #4056]
	mov	r2, r1
	str	ip, [sp, #8]
	bl	FlashReadPages
	ldr	r2, [r5, #4052]
	ldr	ip, [sp, #8]
	adds	r2, r2, #1
	ldr	r3, [sp, #12]
	beq	.L2464
	ldrh	r2, [r7, #8]
	cmp	r2, fp
	bcs	.L2465
	ldrh	r0, [r7]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	bne	.L2465
	ldr	r1, [r3, #4056]
	str	r1, [r10, r2, lsl #2]
	b	.L2465
.L2464:
	ldrh	r2, [r6]
	strh	r2, [r4, #40]	@ movhi
.L2465:
	add	ip, ip, #1
	b	.L2463
.L2478:
	ldr	r3, .L2508+8
	ldr	r10, .L2508+12
	cmp	r4, r3
	bne	.L2468
	b	.L2505
.L2462:
	ldr	r1, [r5, #1776]
	add	r3, r8, r2
	str	r3, [sp, #16]
	ldr	r3, .L2508+12
	str	r1, [r5, #4060]
	ldrh	r1, [r8, r9, lsl #1]
	ldrh	r2, [r3, #2912]
	ldr	r0, .L2508+4
	subs	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r5, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #4052]
	adds	r2, r2, #1
	beq	.L2493
	ldrh	r1, [r7]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2493
	ldrh	r1, [r7, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L2493
	movs	r0, #0
	mov	ip, #8
	mov	r9, #4
.L2470:
	ldr	r3, .L2508+12
	uxth	r2, r0
	sxth	lr, r2
	ldrh	r1, [r3, #2912]
	subs	r1, r1, #1
	cmp	lr, r1
	bge	.L2473
	ldr	r1, [r5, #1776]
	adds	r0, r0, #1
	ldrh	lr, [r1, lr, lsl #3]
	cmp	lr, fp
	ittt	cc
	smlabbcc	r2, r2, ip, r9
	ldrcc	r2, [r1, r2]
	strcc	r2, [r10, lr, lsl #2]
	b	.L2470
.L2493:
	ldr	r3, .L2508
	mov	r9, #0
.L2502:
	ldr	r1, .L2508+12
	sxth	r2, r9
	ldrh	r1, [r1, #2912]
	cmp	r2, r1
	bge	.L2473
	ldr	r1, [sp, #16]
	ldr	r0, .L2508+4
	str	r3, [sp, #20]
	ldrh	r1, [r1]
	orr	r2, r2, r1, lsl #10
	movs	r1, #1
	str	r2, [r3, #4056]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	ldr	r2, [r3, #4052]
	adds	r2, r2, #1
	beq	.L2474
	ldrh	r2, [r7, #8]
	cmp	r2, fp
	bcs	.L2474
	ldrh	r1, [r4, #4]
	ldrh	r0, [r7]
	cmp	r0, r1
	itt	eq
	ldreq	r1, [r3, #4056]
	streq	r1, [r10, r2, lsl #2]
.L2474:
	add	r9, r9, #1
	b	.L2502
.L2473:
	adds	r6, r6, #1
	b	.L2461
.L2490:
	ldr	r3, [r5, #1852]
	lsl	r7, r9, #2
	ldr	r2, [r3, r9, lsl #2]
	subs	r1, r2, #1
	adds	r1, r1, #3
	bls	.L2506
.L2483:
	add	r9, r9, #1
.L2481:
	ldrh	r3, [r10, #2948]
	cmp	r9, r3
	bcc	.L2490
.L2468:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r10, #2912]
	cmp	r2, r3
	bne	.L2480
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2480
.L2505:
	ldrh	r3, [r10, #2952]
	cmp	r3, #0
	beq	.L2468
	mov	r9, #0
	subw	r6, r4, #1868
	b	.L2481
.L2506:
	ldr	r1, [r6, #1848]
	ldr	r1, [r1, r9, lsl #2]
	cmp	r2, r1
	beq	.L2483
	ldr	r2, [r6, #1776]
	movs	r1, #1
	ldr	fp, [r6, #1800]
	ldr	r0, .L2508+4
	str	r2, [r6, #4060]
	mov	r2, r1
	str	fp, [r6, #4064]
	ldr	r3, [r3, r7]
	str	r3, [r6, #4056]
	bl	FlashReadPages
	ldr	r3, [r6, #4052]
	adds	r3, r3, #1
	beq	.L2483
	ldrh	r2, [fp]
	ldrh	r3, [r6, #1872]
	cmp	r2, r3
	bne	.L2483
	ldr	r2, [r6, #1848]
	ldr	r3, [fp, #4]
	ldr	r1, [r2, r7]
	str	r1, [r6, #4056]
	ldr	r2, [r2, r7]
	cbnz	r2, .L2485
	str	r2, [fp, #4]
	b	.L2486
.L2485:
	movs	r1, #1
	ldr	r0, .L2508+4
	str	r3, [sp, #8]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [sp, #8]
.L2486:
	ldr	r0, [fp, #4]
	cmp	r3, r0
	bls	.L2483
	ldr	r1, [r5, #1852]
	ldr	r2, [r5, #1848]
	str	r0, [sp]
	ldr	r1, [r1, r7]
	ldr	r2, [r2, r7]
	ldr	r0, .L2508+16
	bl	printk
	ldr	r3, [r5, #1852]
	ldr	r2, [r3, r7]
	ldr	r3, [r5, #1848]
	str	r2, [r3, r7]
	movs	r2, #0
	ldr	r3, [r5, #1852]
	ldr	r0, [r3, r7]
	ldrh	r3, [r5, #1912]
	ubfx	r0, r0, #10, #16
.L2487:
	sxth	r1, r2
	cmp	r1, r3
	bge	.L2507
	ldrh	r1, [r8, r1, lsl #1]
	adds	r2, r2, #1
	cmp	r1, r0
	bne	.L2487
	b	.L2483
.L2507:
	ldr	r2, [r10, #2940]
	cmp	r3, r2
	bcs	.L2483
	strh	r0, [r8, r3, lsl #1]	@ movhi
	ldrh	r3, [r5, #1912]
	adds	r3, r3, #1
	strh	r3, [r5, #1912]	@ movhi
	bl	remove_from_free_sys_Queue
	b	.L2483
.L2480:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2509:
	.align	2
.L2508:
	.word	.LANCHOR2
	.word	.LANCHOR2+4052
	.word	.LANCHOR2+1868
	.word	.LANCHOR0
	.word	.LC124
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
	ldr	r3, .L2511
	ldr	r0, .L2511+4
	ldrh	r1, [r3, #2932]
	strh	r1, [r0, r2]	@ movhi
	add	r2, r0, #4096
	movw	r1, #61574
	strh	r1, [r2]	@ movhi
	movw	r2, #4100
	ldrh	r1, [r3, #2954]
	strh	r1, [r0, r2]	@ movhi
	movw	r2, #4098
	ldrh	r1, [r3, #2934]
	strh	r1, [r0, r2]	@ movhi
	ldr	r2, [r3, #2956]
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
	movw	r3, #4132
	movw	r2, #65535
	strh	r2, [r0, r3]	@ movhi
	addw	r0, r0, #4092
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2512:
	.align	2
.L2511:
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
	ldr	r0, .L2514
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2515:
	.align	2
.L2514:
	.word	.LANCHOR2+1868
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
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
	mov	r8, #1
	str	r2, [sp]
	mov	r10, r1
	ldr	r2, .L2532
	movs	r3, #0
	ldr	r1, .L2532+4
	ldrh	r4, [r2, #2918]
	str	r2, [sp, #4]
	mov	fp, r1
	adds	r4, r4, #7
	lsl	r5, r8, r4
	lsr	r6, r0, r4
	subs	r4, r5, #1
	ands	r0, r0, r4
	ldr	r4, [r1, #980]
	uxth	r6, r6
	uxth	r7, r0
	ldrh	r0, [r2, #2950]
	movs	r2, #12
.L2517:
	uxth	r8, r3
	cmp	r8, r0
	bcs	.L2531
	adds	r3, r3, #1
	mla	r1, r2, r3, r4
	ldrh	r1, [r1, #-12]
	cmp	r1, r6
	bne	.L2517
.L2518:
	ldr	r2, [sp]
	movs	r3, #12
	cbnz	r2, .L2519
	ldr	r2, [fp, #980]
	mla	r3, r3, r8, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r7, lsl #2]
	str	r3, [r10]
	b	.L2520
.L2519:
	mul	r3, r3, r8
	ldr	r2, [fp, #980]
	ldr	r1, [r10]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r7, lsl #2]
	ldr	r2, [fp, #980]
	strh	r6, [fp, #984]	@ movhi
	add	r3, r3, r2
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
.L2520:
	ldr	r2, [fp, #980]
	movs	r3, #12
	mla	r8, r3, r8, r2
	ldr	r3, [r8, #4]
	adds	r2, r3, #1
	beq	.L2528
	adds	r3, r3, #1
	str	r3, [r8, #4]
	b	.L2528
.L2531:
	bl	select_l2p_ram_region
	movs	r5, #12
	movw	r2, #65535
	muls	r5, r0, r5
	mov	r8, r0
	adds	r3, r4, r5
	ldrh	r1, [r4, r5]
	cmp	r1, r2
	beq	.L2523
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2523
	bl	flush_l2p_region
.L2523:
	ldr	r3, [fp, #1848]
	ldr	r4, .L2532+4
	ldr	r9, [r3, r6, lsl #2]
	cmp	r9, #0
	bne	.L2524
	ldr	r2, [r4, #980]
	movs	r1, #255
	ldr	r3, [sp, #4]
	add	r2, r2, r5
	ldr	r0, [r2, #8]
	ldrh	r2, [r3, #2920]
	bl	ftl_memset
	ldr	r2, [r4, #980]
	strh	r6, [r2, r5]	@ movhi
	ldr	r2, [r4, #980]
	add	r5, r5, r2
	str	r9, [r5, #4]
	b	.L2518
.L2524:
	ldr	r2, [r4, #980]
	movs	r1, #1
	addw	r0, r4, #4052
	str	r9, [r4, #4056]
	add	r2, r2, r5
	ldr	r2, [r2, #8]
	str	r2, [r4, #4060]
	ldr	r2, [r4, #1800]
	str	r2, [r4, #4064]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r4, #4052]
	cmp	r2, #256
	bne	.L2525
	mov	r1, r6
	mov	r2, r9
	ldr	r0, .L2532+8
	bl	printk
	lsr	r3, r9, #10
	strh	r3, [r4, #1908]	@ movhi
	addw	r0, r4, #1868
	ldr	r3, [r4, #980]
	mov	r1, r6
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2525:
	ldr	r3, [fp, #980]
	movs	r1, #0
	adds	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L2518
.L2528:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2533:
	.align	2
.L2532:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC125
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
	ldr	r5, .L2543
	ldr	r4, [r5, #816]
	ldrh	r3, [r4, r0, lsl #1]
	cbnz	r3, .L2535
	ldr	r4, [r5, #828]
	cbz	r4, .L2536
	ldr	r1, [r5, #808]
	mov	lr, #6
	ldr	r2, .L2543+4
	movw	ip, #65535
	subs	r4, r4, r1
	ldrh	r7, [r5, #832]
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L2537:
	uxth	r2, r3
	cmp	r2, r7
	bcs	.L2536
	cmp	r4, r0
	bne	.L2538
	mov	r1, r4
	ldr	r0, .L2543+8
	bl	List_remove_node
	ldrh	r3, [r5, #832]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #832]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #816]
	ldrh	r3, [r2, r4, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r4, lsl #1]	@ movhi
	b	.L2536
.L2538:
	mul	r4, lr, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, ip
	bne	.L2537
	b	.L2536
.L2535:
	adds	r3, r3, #1
	strh	r3, [r4, r0, lsl #1]	@ movhi
.L2536:
	mov	r0, r6
	add	r1, sp, #4
	movs	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2544:
	.align	2
.L2543:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+828
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	1
	.global	FtlReadRefresh
	.thumb
	.thumb_func
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2560
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r2, [r3, #1204]
	mov	r4, r3
	ldr	r5, .L2560+4
	cmp	r2, #0
	beq	.L2546
	ldr	r1, [r3, #1208]
	ldr	r2, [r5, #2968]
	cmp	r1, r2
	bcs	.L2547
	mov	r6, #2048
	mov	r7, r3
.L2552:
	ldr	r0, [r4, #1208]
	ldr	r3, [r5, #2968]
	cmp	r0, r3
	bcs	.L2549
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r7, #1208]
	adds	r1, r2, #1
	add	r3, r3, #1
	str	r3, [r7, #1208]
	beq	.L2550
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
	bne	.L2549
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L2549
.L2550:
	subs	r6, r6, #1
	bne	.L2552
.L2549:
	mov	r0, #-1
	b	.L2554
.L2547:
	ldr	r2, [r3, #996]
	movs	r0, #0
	str	r0, [r3, #1204]
	str	r0, [r3, #1208]
	str	r2, [r3, #1200]
	b	.L2554
.L2546:
	ldrb	r2, [r5, #76]	@ zero_extendqisi2
	ldr	r1, [r3, #1048]
	ldr	r3, [r5, #2832]
	cbnz	r2, .L2557
	mov	r2, #33554432
	add	r3, r3, r1, lsr #10
	asrs	r2, r2, r3
	b	.L2555
.L2557:
	mov	r2, #4194304
.L2555:
	ldr	r1, [r4, #996]
	ldr	r3, [r4, #1200]
	add	r0, r1, #1048576
	cmp	r3, r0
	bhi	.L2556
	add	r3, r3, r2
	cmp	r3, r1
	bcc	.L2556
	ldr	r3, .L2560
	ldrb	r3, [r3, #788]	@ zero_extendqisi2
	cbnz	r3, .L2558
.L2556:
	movs	r3, #1
	movs	r0, #0
	str	r3, [r4, #1204]
	str	r0, [r4, #1208]
	str	r1, [r4, #1200]
	b	.L2554
.L2558:
	movs	r0, #0
.L2554:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2561:
	.align	2
.L2560:
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
	ldr	r3, .L2575
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	mov	r4, r1
	add	r0, r0, r1
	ldrh	r1, [r3, #2904]
	.pad #60
	sub	sp, sp, #60
	mov	r10, r2
	ldrh	r2, [r3, #2918]
	cmp	r0, r1
	str	r3, [sp, #4]
	bhi	.L2570
	ldr	r7, .L2575+4
	lsr	r6, r5, r2
	mov	fp, #0
	lsls	r3, r6, #2
	str	r3, [sp]
.L2564:
	cmp	r4, #0
	beq	.L2574
	ldr	r3, .L2575+4
	mov	r0, r5
	ldr	r2, [sp]
	ldr	r3, [r3, #1844]
	ldr	ip, [r3, r2]
	ldr	r3, [sp, #4]
	str	ip, [sp, #12]
	ldrh	r2, [r3, #2916]
	mov	r1, r2
	str	r2, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #8]
	ldr	ip, [sp, #12]
	subs	r3, r2, r1
	mov	r8, r1
	uxth	r9, r3
	cmp	r9, r4
	it	hi
	uxthhi	r9, r4
	cmp	ip, #0
	beq	.L2566
	cmp	r9, r2
	beq	.L2566
	ldr	r2, [r7, #1784]
	movs	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	movs	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2567
.L2566:
	ldr	r3, [sp, #4]
	movs	r1, #0
	ldr	r0, [r7, #1784]
	ldrh	r2, [r3, #2920]
	bl	ftl_memset
.L2567:
	lsl	ip, r9, #9
	ldr	r0, [r7, #1784]
	uxth	r8, r8
	mov	r1, r10
	mov	r2, ip
	str	ip, [sp, #8]
	add	r0, r0, r8, lsl #9
	rsb	r4, r9, r4
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r0, .L2575+8
	adds	r6, r6, #1
	ldr	r2, [r7, #1784]
	add	r5, r5, r9
	bl	FtlMapWritePage
	ldr	r3, [sp]
	ldr	ip, [sp, #8]
	adds	r3, r3, #4
	str	r3, [sp]
	add	r10, r10, ip
	adds	r0, r0, #1
	it	eq
	moveq	fp, #-1
	b	.L2564
.L2574:
	mov	r0, fp
	b	.L2563
.L2570:
	mov	r0, #-1
.L2563:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2576:
	.align	2
.L2575:
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
	ldr	r2, .L2579
	ldr	r3, .L2579+4
	ldr	r1, [r2, #1124]
	cmp	r1, r3
	bne	.L2577
	ldr	r3, .L2579+8
	movs	r0, #0
	movs	r1, #1
	str	r3, [r2, #1128]
	ldr	r3, [r2, #1020]
	str	r3, [r2, #1212]
	ldr	r3, [r2, #1024]
	str	r3, [r2, #1216]
	ldr	r3, [r2, #1016]
	str	r3, [r2, #1132]
	ldr	r3, [r2, #1004]
	str	r3, [r2, #1136]
	ldr	r3, [r2, #996]
	str	r3, [r2, #1140]
	ldr	r3, [r2, #1012]
	str	r3, [r2, #1144]
	ldr	r3, [r2, #1040]
	str	r3, [r2, #1152]
	ldr	r3, [r2, #756]
	addw	r2, r2, #1124
	str	r3, [r2, #32]
	ldr	r3, [r2, #-124]
	str	r3, [r2, #36]
	ldr	r3, [r2, #-116]
	str	r3, [r2, #40]
	ldr	r3, [r2, #-76]
	str	r3, [r2, #44]
	ldr	r3, [r2, #-72]
	str	r3, [r2, #48]
	ldr	r3, [r2, #-4]
	str	r3, [r2, #60]
	ldr	r3, [r2, #612]
	str	r3, [r2, #64]
	b	FtlVendorPartWrite
.L2577:
	bx	lr
.L2580:
	.align	2
.L2579:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177352
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
	ldr	r3, .L2588
	push	{r4, lr}
	.save {r4, lr}
	ldr	r2, [r3, #2832]
	ldr	r3, .L2588+4
	cbz	r2, .L2586
	ldr	r2, [r3, #1052]
	cmp	r2, #29
	ite	hi
	movhi	r1, #32
	movls	r1, #4
	b	.L2582
.L2586:
	movs	r1, #32
.L2582:
	ldr	r4, .L2588+8
	ldrh	r2, [r4, #-208]
	cmp	r2, #31
	ittt	ls
	addls	r2, r2, #1
	movls	r1, #1
	strhls	r2, [r4, #-208]	@ movhi
	cbnz	r0, .L2584
	ldr	r0, [r3, #1820]
	ldr	r2, [r0, #20]
	add	r2, r2, r1
	ldr	r1, [r0, #16]
	cmp	r1, r2
	bcc	.L2585
.L2584:
	ldr	r2, [r3, #1820]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2588+12
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
.L2585:
	movs	r0, #0
	pop	{r4, pc}
.L2589:
	.align	2
.L2588:
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
	ldr	r3, .L2601
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	mov	r5, r1
	add	r0, r0, r1
	ldrh	r1, [r3, #2904]
	.pad #60
	sub	sp, sp, #60
	mov	r8, r2
	ldrh	r2, [r3, #2918]
	cmp	r0, r1
	str	r3, [sp, #8]
	bhi	.L2599
	lsr	r6, r7, r2
	ldr	r10, .L2601+12
	movs	r3, #0
	str	r3, [sp]
	lsl	r9, r6, #2
	mov	fp, r10
.L2592:
	cmp	r5, #0
	beq	.L2600
	ldr	r3, [r10, #1844]
	mov	r0, r7
	ldr	ip, [r3, r9]
	ldr	r3, [sp, #8]
	str	ip, [sp, #12]
	ldrh	r4, [r3, #2916]
	mov	r1, r4
	bl	__aeabi_uidivmod
	ldr	ip, [sp, #12]
	subs	r3, r4, r1
	str	r1, [sp, #4]
	uxth	r4, r3
	cmp	r4, r5
	it	hi
	uxthhi	r4, r5
	cmp	ip, #0
	beq	.L2594
	ldr	r2, [fp, #1784]
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
	ldr	r2, [fp, #4052]
	it	eq
	moveq	r3, #-1
	ldr	ip, [sp, #12]
	cmp	r2, #256
	str	r3, [sp]
	bne	.L2596
	mov	r1, r6
	mov	r2, ip
	ldr	r0, .L2601+4
	bl	printk
	ldr	r0, .L2601+8
	mov	r1, r6
	ldr	r2, [r10, #1784]
	bl	FtlMapWritePage
.L2596:
	ldrh	r2, [sp, #4]
	mov	r0, r8
	ldr	r1, [fp, #1784]
	add	r1, r1, r2, lsl #9
	lsls	r2, r4, #9
	bl	ftl_memcpy
	b	.L2597
.L2594:
	mov	r0, r8
	mov	r1, ip
	lsls	r2, r4, #9
	bl	ftl_memset
.L2597:
	adds	r6, r6, #1
	subs	r5, r5, r4
	add	r7, r7, r4
	add	r8, r8, r4, lsl #9
	add	r9, r9, #4
	b	.L2592
.L2600:
	ldr	r0, [sp]
	b	.L2591
.L2599:
	mov	r0, #-1
.L2591:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2602:
	.align	2
.L2601:
	.word	.LANCHOR0
	.word	.LC126
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
	ldr	r4, .L2605
	ldr	r2, [r4, #1820]
	ldrh	r1, [r4, #1812]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #1820]
	ldr	r2, [r3]
	ldr	r3, .L2605+4
	cmp	r2, r3
	beq	.L2604
	ldr	r1, .L2605+8
	ldr	r0, .L2605+12
	bl	printk
	ldrh	r2, [r4, #1812]
	ldr	r0, [r4, #1820]
	movs	r1, #0
	lsls	r2, r2, #9
	bl	ftl_memset
.L2604:
	movs	r0, #0
	pop	{r4, pc}
.L2606:
	.align	2
.L2605:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC127
	.word	.LC77
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
	ldr	r4, .L2611
	movs	r1, #1
	ldr	r5, .L2611+4
	addw	r2, r4, #1124
	bl	FtlVendorPartRead
	ldr	r3, [r4, #1124]
	cmp	r3, r5
	beq	.L2608
	addw	r0, r4, #1124
	mov	r1, #512
	bl	__memzero
	str	r5, [r4, #1124]
.L2608:
	ldr	r2, [r4, #1124]
	ldr	r3, .L2611
	cmp	r2, r5
	bne	.L2609
	ldr	r2, [r3, #1212]
	str	r2, [r3, #1020]
	ldr	r2, [r3, #1216]
	str	r2, [r3, #1024]
	ldr	r2, [r3, #1132]
	str	r2, [r3, #1016]
	ldr	r2, [r3, #1136]
	str	r2, [r3, #1004]
	ldr	r2, [r3, #1140]
	str	r2, [r3, #996]
	ldr	r2, [r3, #1144]
	str	r2, [r3, #1012]
	ldr	r2, [r3, #1152]
	str	r2, [r3, #1040]
	ldr	r2, [r3, #1156]
	str	r2, [r3, #756]
	ldr	r2, [r3, #1160]
	str	r2, [r3, #1000]
	ldr	r2, [r3, #1164]
	str	r2, [r3, #1008]
	ldr	r2, [r3, #1168]
	str	r2, [r3, #1048]
	ldr	r2, [r3, #1172]
	str	r2, [r3, #1052]
	ldr	r2, [r3, #1184]
	str	r2, [r3, #1120]
.L2609:
	movs	r3, #0
	ldr	r2, [r4, #1192]
	str	r3, [r4, #1736]
	ldr	r3, .L2611+8
	ldr	r5, .L2611+12
	cmp	r2, r3
	bne	.L2610
	ldr	r0, .L2611+16
	movs	r3, #1
	ldr	r1, .L2611+20
	str	r3, [r5, #2832]
	bl	printk
.L2610:
	ldrh	r2, [r5, #2900]
	ldr	r3, [r4, #1040]
	ldr	r0, [r4, #1036]
	ldrh	r1, [r5, #2848]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #1044]
	pop	{r3, r4, r5, pc}
.L2612:
	.align	2
.L2611:
	.word	.LANCHOR2
	.word	1179929683
	.word	305432421
	.word	.LANCHOR0
	.word	.LC77
	.word	.LC128
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	1
	.global	ftl_vendor_read
	.thumb
	.thumb_func
	.type	ftl_vendor_read, %function
ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	add	r0, r0, #256
	b	FtlVendorPartRead
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
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	bl	ftl_vendor_read
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
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
	@ link register save eliminated.
	add	r0, r0, #512
	b	FtlVendorPartRead
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
	mov	r5, r0
	mov	r4, r1
	mov	r0, r1
	mov	r1, #512
	bl	__memzero
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
	cbz	r2, .L2617
	ldr	r4, .L2623
	movs	r2, #0
	str	r2, [r0, #36]
	ldr	r2, [r4, #744]
	cbnz	r2, .L2617
	subs	r5, r5, #1
	ldr	r0, [r4, #1780]
	ldr	r2, [r4, #1800]
	uxth	r5, r5
	str	r0, [r4, #4060]
	str	r2, [r4, #4064]
	ldr	r3, [r3, r5, lsl #2]
	str	r3, [r4, #4056]
	cbz	r3, .L2621
	movs	r1, #1
	addw	r0, r4, #4052
	mov	r2, r1
	bl	FlashReadPages
	b	.L2622
.L2621:
	ldr	r3, .L2623+4
	movs	r1, #255
	ldrh	r2, [r3, #2920]
	bl	ftl_memset
.L2622:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #4060]
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L2617:
	pop	{r4, r5, r6, pc}
.L2624:
	.align	2
.L2623:
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
	beq	.L2769
	ldrh	r3, [r0, #2]
	movs	r5, #0
	ldr	r6, .L2785
	str	r3, [sp, #12]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	str	r3, [sp, #28]
	ldrh	r3, [r6, #2910]
	cmp	r3, r2
	bne	.L2628
	strh	r5, [r0, #4]	@ movhi
	strb	r5, [r0, #6]
	b	.L2769
.L2628:
	ldrh	r0, [r0, #16]
.L2629:
	cmp	r0, r7
	add	r5, r5, #1
	bne	.L2780
	uxth	r3, r5
	adds	r3, r3, #8
	ldrh	r0, [r4, r3, lsl #1]
	b	.L2629
.L2780:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2631
	bl	FtlGetLastWrittenPage
	adds	r2, r0, #1
	mov	r5, r0
	beq	.L2632
	ldrb	r3, [r6, #76]	@ zero_extendqisi2
	cbnz	r3, .L2771
	ldr	r3, .L2785
	add	r3, r3, r0, lsl #1
	ldrh	r7, [r3, #80]
	b	.L2706
.L2631:
	movs	r1, #0
	bl	FtlGetLastWrittenPage
	adds	r3, r0, #1
	mov	r5, r0
	beq	.L2632
.L2771:
	mov	r7, r5
.L2706:
	ldr	r3, .L2785+4
	movw	r9, #65535
	ldrh	ip, [r6, #2840]
	mov	r10, #36
	ldrh	r0, [r6, #2922]
	ldr	r2, [r3, #1764]
	ldr	r8, [r3, #1664]
	add	r3, r4, #14
	str	r3, [sp, #20]
	str	r2, [sp, #4]
	movs	r2, #0
	mov	lr, r3
	mov	r6, r2
	mov	fp, r2
	b	.L2634
.L2632:
	movs	r3, #0
	strh	r3, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
	b	.L2769
.L2637:
	ldrh	r3, [lr, #2]!
	cmp	r3, r9
	beq	.L2635
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
.L2635:
	adds	r2, r2, #1
.L2634:
	uxth	r3, r2
	cmp	r3, ip
	bcc	.L2637
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2708
	ldr	r3, .L2785
	ldrb	r1, [r3, #76]	@ zero_extendqisi2
	adds	r3, r1, #0
	it	ne
	movne	r3, #1
	b	.L2772
.L2708:
	movs	r3, #0
.L2772:
	ldr	r8, .L2785+4
	mov	r1, r6
	str	r3, [sp, #24]
	mov	fp, #0
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #1764]
	bl	FlashReadPages
	ldr	r3, [r8, #1032]
	str	r8, [sp, #8]
	subs	r3, r3, #1
	str	r3, [sp, #4]
	movw	r3, #65535
	str	r3, [sp, #16]
.L2639:
	uxth	r9, fp
	cmp	r9, r6
	bcs	.L2646
	movs	r1, #36
	ldr	r0, [r8, #1764]
	mul	r1, r1, fp
	add	lr, r0, r1
	ldr	r1, [r0, r1]
	cbnz	r1, .L2640
	ldr	r3, [lr, #12]
	ldr	r10, [r3, #4]
	cmp	r10, #-1
	beq	.L2641
	mov	r0, r10
	ldr	r1, [r8, #1032]
	str	r3, [sp, #32]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #32]
	cbz	r0, .L2641
	add	r1, r10, #1
	str	r1, [r8, #1032]
.L2641:
	ldr	r1, [r3]
	adds	r1, r1, #1
	bne	.L2642
.L2646:
	cmp	r9, r6
	ldr	r6, .L2785+4
	bne	.L2770
	ldr	r3, [r6, #1764]
	adds	r5, r5, #1
	uxth	r10, r5
	ldr	r0, [r3, #4]
	b	.L2773
.L2640:
	ldr	r1, [lr, #4]
	ldr	r0, .L2785+8
	bl	printk
	uxth	r3, r7
	ldrh	r1, [r4]
	str	r3, [sp, #16]
	ldr	r3, .L2785+12
	strh	r1, [r3, #-206]	@ movhi
.L2642:
	add	fp, fp, #1
	b	.L2639
.L2770:
	ldr	r3, [sp, #8]
	movs	r2, #36
	uxth	r10, r5
	ldr	r3, [r3, #1764]
	mla	r9, r2, r9, r3
	ldr	r0, [r9, #4]
.L2773:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r5, [r4, #8]	@ zero_extendqisi2
	ldr	r2, .L2785
	cmp	r5, #1
	str	r0, [sp, #8]
	bne	.L2648
	ldrb	r3, [r2, #76]	@ zero_extendqisi2
	cbnz	r3, .L2648
	add	r3, r2, r10, lsl #1
	ldrh	r10, [r3, #80]
.L2648:
	ldrh	r3, [r2, #2910]
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
	bne	.L2650
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #32]
	cmp	r3, r2
	bne	.L2650
	mov	r0, r4
	mov	r1, r10
	mov	r2, r3
	b	.L2778
.L2650:
	ldr	r2, [sp, #16]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2651
	cmp	r5, #0
	bne	.L2652
.L2651:
	ldr	r3, [r6, #1932]
	uxth	fp, r7
	mov	r9, #-1
	ldr	r8, .L2785+4
	adds	r3, r3, #1
	mov	r7, r9
	ittt	eq
	ldreq	r3, .L2785+4
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
.L2655:
	cmp	r5, fp
	bhi	.L2668
	ldr	r3, .L2785
	ldr	r1, [sp, #20]
	ldrh	ip, [r3, #2840]
	ldr	r3, [r8, #1764]
	str	r3, [sp, #28]
	movs	r3, #0
	mov	r6, r3
.L2669:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2781
	ldrh	r2, [r1, #2]!
	movw	r0, #65535
	cmp	r2, r0
	beq	.L2656
	ldr	r0, [sp, #28]
	mov	lr, #36
	orr	r2, r5, r2, lsl #10
	mla	r0, lr, r6, r0
	adds	r6, r6, #1
	uxth	r6, r6
	str	r2, [r0, #4]
.L2656:
	adds	r3, r3, #1
	b	.L2669
.L2781:
	mov	r1, r6
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #1764]
	bl	FlashReadPages
	ldr	r3, .L2785
	movs	r2, #36
	movw	r1, #65535
	ldrb	r0, [r3, #76]	@ zero_extendqisi2
	ldr	r3, [r8, #1764]
	mla	r6, r2, r6, r3
	ldr	r2, .L2785
	add	lr, r2, r5, lsl #1
.L2658:
	cmp	r3, r6
	beq	.L2782
	ldr	r2, [r3]
	cbnz	r2, .L2659
	ldr	r2, [r3, #12]
	ldrh	ip, [r2]
	cmp	ip, r1
	beq	.L2660
	ldr	r2, [r2, #4]
	cmp	r2, #-1
	beq	.L2660
	cmp	r9, #-1
	ldr	r7, [r8, #1932]
	str	r2, [r8, #1932]
	bne	.L2660
	ldrh	r2, [lr, #592]
	cmp	r2, r1
	bne	.L2661
	cmp	r0, #0
	beq	.L2660
.L2661:
	ldr	r2, [sp, #4]
	cmp	r7, r2
	ite	ne
	movne	r9, r7
	moveq	r9, #-1
	b	.L2660
.L2786:
	.align	2
.L2785:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC129
	.word	.LANCHOR4
.L2659:
	ldr	r3, .L2787
	ldrh	r2, [r4]
	strh	r2, [r3, #-206]	@ movhi
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cbnz	r3, .L2652
	ldr	r3, .L2787+4
	add	r5, r3, r5, lsl #1
	movw	r3, #65535
	ldrh	r2, [r5, #592]
	cmp	r2, r3
	ldr	r3, .L2787+8
	bne	.L2663
	cmp	r9, #-1
	beq	.L2664
	str	r9, [r3, #1932]
	b	.L2652
.L2664:
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	bne	.L2774
	ldr	r2, [r3, #1932]
	b	.L2779
.L2663:
	ldr	r2, [sp, #4]
	cmp	r7, r2
	beq	.L2666
	adds	r2, r7, #1
	beq	.L2652
	str	r7, [r3, #1932]
	b	.L2652
.L2666:
	ldr	r2, [r3, #1932]
	ldr	r1, [sp, #4]
	cmp	r2, r1
	beq	.L2652
.L2779:
	subs	r2, r2, #1
	b	.L2774
.L2660:
	adds	r3, r3, #36
	b	.L2658
.L2782:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L2655
.L2668:
	ldr	r3, .L2787+8
	mov	r2, #-1
.L2774:
	str	r2, [r3, #1932]
.L2652:
	ldr	r3, .L2787
	movs	r2, #1
	ldr	r0, .L2787+12
	ldr	r5, .L2787+8
	strh	r2, [r3, #-204]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	fp, [sp, #12]
.L2670:
	ldr	r3, .L2787+4
	movw	r9, #65535
	ldr	r1, [r5, #1764]
	movs	r6, #0
	ldr	r7, [sp, #20]
	ldrh	r8, [r3, #2840]
	ldrb	r0, [r3, #76]	@ zero_extendqisi2
	str	r6, [sp, #16]
.L2671:
	uxth	r3, r6
	cmp	r3, r8
	bcs	.L2783
	ldrh	r3, [r7, #2]!
	cmp	r3, r9
	beq	.L2672
	ldr	r2, [sp, #16]
	mov	lr, #36
	orr	r3, fp, r3, lsl #10
	mla	r2, lr, r2, r1
	str	r3, [r2, #4]
	ldrb	lr, [r4, #8]	@ zero_extendqisi2
	cmp	lr, #1
	bne	.L2673
	cbz	r0, .L2673
	orr	r3, r3, #-2147483648
	str	r3, [r2, #4]
.L2673:
	ldr	r3, [sp, #16]
	add	ip, r3, #1
	uxth	r3, ip
	str	r3, [sp, #16]
.L2672:
	adds	r6, r6, #1
	b	.L2671
.L2783:
	ldr	r0, [r5, #1764]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	movs	r3, #0
.L2777:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L2784
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
	bcc	.L2676
	bne	.L2677
	ldr	r3, [sp, #32]
	cmp	r0, r3
	bcc	.L2676
.L2677:
	cmp	fp, r10
	bne	.L2678
	ldr	r3, [sp, #8]
	cmp	r0, r3
	beq	.L2679
.L2678:
	ldr	r3, [r9, r6]
	adds	r3, r3, #1
	beq	.L2680
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L2687
	ldr	r3, [r8, #4]
	adds	r7, r3, #1
	str	r3, [sp, #4]
	beq	.L2682
	mov	r0, r3
	ldr	r1, [r5, #1032]
	bl	ftl_cmp_data_ver
	cbz	r0, .L2682
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [r5, #1032]
.L2682:
	ldr	r7, [r8, #8]
	add	r1, sp, #56
	ldr	r3, [r8, #12]
	movs	r2, #0
	mov	r0, r7
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r5, #1932]
	adds	r0, r1, #1
	beq	.L2683
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2683
	ldr	r3, [sp, #52]
	adds	r2, r3, #1
	beq	.L2684
	ldr	r0, [r5, #1764]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r6
	ldr	r9, [r0, #12]
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldr	r2, [r5, #1764]
	ldr	r3, [r9, #4]
	add	ip, r2, r6
	str	r3, [sp, #36]
	ldr	r3, [r2, r6]
	adds	r3, r3, #1
	bne	.L2685
	b	.L2686
.L2684:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L2687
	mov	r0, r7
	add	r1, sp, #52
	movs	r2, #1
	bl	log2phys
.L2687:
	ldrh	r0, [r4]
	b	.L2776
.L2685:
	ldr	r8, [r9, #8]
	cmp	r8, r7
	bne	.L2686
	ldr	r0, [r5, #1932]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	cbz	r0, .L2686
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L2692
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L2686
	adds	r0, r3, #1
	beq	.L2690
	str	r3, [ip, #4]
	mov	r0, ip
	movs	r1, #1
	movs	r2, #0
	ldr	r9, [ip, #12]
	bl	FlashReadPages
	b	.L2691
.L2690:
	str	r3, [r2, r6]
.L2691:
	ldr	r3, [r5, #1764]
	ldr	r3, [r3, r6]
	adds	r3, r3, #1
	beq	.L2692
	ldr	r6, [r9, #4]
	ldr	r0, [r5, #1932]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L2692
	ldr	r0, [sp, #36]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cbz	r0, .L2686
.L2692:
	mov	r0, r8
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L2686:
	ldrh	r0, [r4]
	mov	r3, #-1
	str	r3, [sp, #52]
	bl	decrement_vpc_count
	b	.L2694
.L2788:
	.align	2
.L2787:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+1868
.L2683:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L2694
	mov	r0, r7
	add	r1, sp, #60
	movs	r2, #1
	bl	log2phys
	ldr	r8, [sp, #56]
	cmp	r8, #-1
	beq	.L2694
	ldr	r3, [sp, #52]
	cmp	r8, r3
	beq	.L2694
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #836]
	cmp	r3, r0
	beq	.L2697
	ldrh	r3, [r5, #884]
	cmp	r3, r0
	beq	.L2697
	ldrh	r3, [r5, #932]
	cmp	r3, r0
	bne	.L2694
.L2697:
	ldr	r3, .L2789
	movs	r1, #1
	movs	r2, #0
	ldr	r0, [r3, #1764]
	str	r8, [r0, #4]
	ldr	r6, [r0, #12]
	bl	FlashReadPages
	ldr	r3, .L2789
	ldr	r1, [r6, #4]
	ldr	r3, [r3, #1764]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L2694
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cbnz	r0, .L2694
	mov	r0, r7
	add	r1, sp, #56
	movs	r2, #1
	bl	log2phys
.L2694:
	ldr	r0, [sp, #52]
	adds	r1, r0, #1
	beq	.L2676
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #816]
	ldrh	r3, [r3, r0, lsl #1]
	mov	r1, r0
	cbz	r3, .L2698
.L2776:
	bl	decrement_vpc_count
	b	.L2676
.L2698:
	ldr	r0, .L2789+4
	bl	printk
	b	.L2676
.L2680:
	ldrh	r3, [r4]
	mov	r1, r7
	ldr	r2, .L2789+8
	ldr	r0, .L2789+12
	strh	r3, [r2, #-206]	@ movhi
	ldr	r2, [sp, #4]
	bl	printk
	ldr	r3, .L2789+8
	ldr	r3, [r3, #-200]
	cmp	r3, #31
	bhi	.L2699
	ldr	r2, .L2789+8
	ldr	r1, [sp, #60]
	add	r2, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r2, #-196]
	ldr	r2, .L2789+8
	str	r3, [r2, #-200]
.L2699:
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	ldr	r3, [r5, #1932]
	adds	r2, r3, #1
	bne	.L2700
	ldr	r3, [sp, #4]
	b	.L2775
.L2700:
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bls	.L2676
	mov	r3, r2
.L2775:
	str	r3, [r5, #1932]
.L2676:
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	b	.L2777
.L2784:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	fp, fp, #1
	cmp	r3, #1
	uxth	fp, fp
	ldr	r3, .L2789+16
	bne	.L2702
	ldrb	r2, [r3, #76]	@ zero_extendqisi2
	cbz	r2, .L2702
	ldrh	r2, [r3, #2912]
	cmp	r2, fp
	bne	.L2702
	cmp	r10, fp
	beq	.L2679
.L2702:
	ldrh	r3, [r3, #2910]
	cmp	fp, r3
	bne	.L2670
	ldr	r2, .L2789+16
	movw	r0, #65535
	movs	r3, #0
	strh	fp, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	ldrh	r1, [r2, #2840]
.L2703:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L2769
	ldr	r6, [sp, #20]
	adds	r3, r3, #1
	ldrh	r5, [r6, #2]!
	cmp	r5, r0
	str	r6, [sp, #20]
	beq	.L2703
	strb	r2, [r4, #6]
	b	.L2769
.L2679:
	ldrb	r3, [sp, #8]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r2, [sp, #8]
	mov	r1, r10
	strh	r10, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
.L2778:
	bl	ftl_sb_update_avl_pages
.L2769:
	movs	r0, #0
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2790:
	.align	2
.L2789:
	.word	.LANCHOR2
	.word	.LC130
	.word	.LANCHOR4
	.word	.LC131
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
	ldr	r4, .L2809
	ldr	r3, [r4, #744]
	cmp	r3, #0
	bne	.L2791
	ldrh	r2, [r4, #840]
	cmp	r2, #0
	beq	.L2793
	ldrb	r3, [r4, #844]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2793
	ldr	r7, .L2809+4
	ldrb	r1, [r4, #843]	@ zero_extendqisi2
	ldrh	r3, [r7, #2910]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L2793
	ldrb	r9, [r4, #846]	@ zero_extendqisi2
	ldr	r6, [r7, #2968]
	ldrh	r8, [r7, #2840]
	cmp	r9, #0
	bne	.L2791
	subs	r6, r6, #1
	mov	r1, sp
	mov	r2, r9
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp]
	ldr	r5, [r4, #1800]
	ldr	r0, [r4, #1776]
	str	r3, [sp, #8]
	adds	r3, r3, #1
	str	r6, [sp, #20]
	str	r0, [sp, #12]
	str	r5, [sp, #16]
	str	r9, [r5, #4]
	beq	.L2795
	add	r0, sp, #4
	movs	r1, #1
	mov	r2, r9
	bl	FlashReadPages
	b	.L2796
.L2795:
	movs	r1, #255
	ldrh	r2, [r7, #2920]
	bl	ftl_memset
.L2796:
	movs	r7, #0
	ldr	r10, .L2809
	lsl	r8, r8, #2
	movw	r3, #61589
	mov	r9, r7
	strh	r3, [r5]	@ movhi
.L2797:
	cmp	r7, r8
	beq	.L2798
	ldrh	r3, [r4, #840]
	cbz	r3, .L2798
	ldr	r3, [sp, #8]
	adds	r7, r7, #1
	str	r6, [r5, #8]
	ldr	r0, .L2809+8
	str	r3, [r5, #12]
	ldrh	r3, [r10, #836]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r10, #1032]
	movs	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, #0
	it	eq
	moveq	r3, r9
	str	r3, [r10, #1032]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r4, #836]
	bl	decrement_vpc_count
	b	.L2797
.L2798:
	movs	r3, #1
	b	.L2808
.L2793:
	movs	r3, #0
.L2808:
	strb	r3, [r4, #846]
.L2791:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2810:
	.align	2
.L2809:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+836
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
	ldr	r5, .L2816
	movs	r7, #12
	ldr	r6, .L2816+4
.L2812:
	ldrh	r3, [r5, #2950]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2815
	ldr	r3, [r6, #980]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2813
	bl	flush_l2p_region
.L2813:
	adds	r4, r4, #1
	b	.L2812
.L2815:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L2817:
	.align	2
.L2816:
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
	ldr	r6, .L2832
	ldr	r1, .L2832+4
	ldr	r0, .L2832+8
	bl	printk
	ldr	r5, .L2832+12
	ldrh	r2, [r6, #2850]
	movs	r1, #0
	ldr	r0, [r5, #1824]
	lsls	r2, r2, #1
	bl	ftl_memset
.L2819:
	ldr	r3, [r6, #2968]
	cmp	r4, r3
	bcs	.L2830
	mov	r0, r4
	add	r1, sp, #4
	movs	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L2820
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #1824]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L2820:
	adds	r4, r4, #1
	b	.L2819
.L2830:
	ldr	r7, .L2832+12
	mov	r8, #0
	movw	r9, #65535
.L2822:
	ldrh	r3, [r6, #2848]
	uxth	r4, r8
	cmp	r3, r4
	bls	.L2831
	ldr	r3, [r5, #816]
	ldrh	r2, [r3, r4, lsl #1]
	ldr	r3, [r5, #1824]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r2, r3
	beq	.L2824
	cmp	r2, r9
	beq	.L2824
	ldr	r0, .L2832+16
	mov	r1, r4
	bl	printk
	ldrh	r3, [r7, #836]
	cmp	r3, r4
	beq	.L2824
	ldrh	r3, [r7, #932]
	cmp	r3, r4
	beq	.L2824
	ldrh	r3, [r7, #884]
	cmp	r3, r4
	beq	.L2824
	ldr	r3, [r7, #1824]
	mov	r0, r4
	ldrh	r2, [r3, r4, lsl #1]
	ldr	r3, [r7, #816]
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2824:
	add	r8, r8, #1
	b	.L2822
.L2831:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2833:
	.align	2
.L2832:
	.word	.LANCHOR0
	.word	.LANCHOR3+16
	.word	.LC132
	.word	.LANCHOR2
	.word	.LC133
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
	ldr	r4, .L2860
	ldrh	r5, [r0]
	ldr	r3, [r4, #744]
	cmp	r3, #0
	bne	.L2835
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2836
	ldr	r3, [r4, #816]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L2837
	bl	INSERT_DATA_LIST
	b	.L2836
.L2837:
	bl	INSERT_FREE_LIST
.L2836:
	ldr	r2, .L2860
	movs	r3, #0
	strb	r3, [r6, #8]
	add	r3, r2, #884
	cmp	r6, r3
	beq	.L2838
	ldr	r3, .L2860+4
	ldrh	r1, [r3, #2860]
	cmp	r1, #1
	beq	.L2838
	ldrb	r0, [r3, #76]	@ zero_extendqisi2
	cbz	r0, .L2839
.L2838:
	movs	r3, #1
	strb	r3, [r6, #8]
	b	.L2840
.L2839:
	add	r0, r2, #836
	cmp	r6, r0
	bne	.L2840
	cmp	r1, #3
	beq	.L2842
	ldr	r2, [r2, #1120]
	cmp	r2, #1
	bne	.L2843
.L2842:
	movs	r2, #1
	strb	r2, [r4, #844]
.L2843:
	ldr	r3, [r3, #2832]
	cbz	r3, .L2840
	ldr	r3, [r4, #1052]
	cmp	r3, #29
	ittt	ls
	movls	r2, #1
	ldrls	r3, .L2860
	strbls	r2, [r3, #844]
.L2840:
	ldrh	r0, [r4, #1920]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2845
	cmp	r5, r0
	bne	.L2846
	ldr	r3, .L2860
	ldr	r3, [r3, #816]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L2847
.L2846:
	bl	update_vpc_list
.L2847:
	movw	r3, #65535
	strh	r3, [r4, #1920]	@ movhi
.L2845:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2835:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L2861:
	.align	2
.L2860:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	1
	.global	ftl_do_gc
	.thumb
	.thumb_func
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r4, .L3030
	.pad #44
	sub	sp, sp, #44
	mov	r6, r1
	ldr	r0, [r4, #744]
	mov	r7, r4
	cmp	r0, #0
	bne	.L2956
	ldr	r3, [r4, #1740]
	cmp	r3, #0
	bne	.L3014
	ldrh	r2, [r4, #824]
	cmp	r2, #47
	bls	.L2958
	ldr	r3, .L3030+4
	ldrh	r2, [r3, #3064]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2864
.L2867:
	ldrh	r1, [r7, #1692]
	movw	r0, #65535
	ldr	r3, .L3030
	cmp	r1, r0
	beq	.L2866
	ldrh	r2, [r3, #1690]
	cmp	r2, r0
	itt	eq
	strheq	r1, [r3, #1690]	@ movhi
	strheq	r2, [r3, #1692]	@ movhi
	b	.L2866
.L2864:
	ldrh	r2, [r4, #932]
	cmp	r2, r3
	beq	.L2867
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L2867
	movs	r0, #1
	b	.L3014
.L2866:
	ldr	r4, [r7, #1640]
	cmp	r8, #1
	add	r4, r4, #1
	add	r4, r4, r8, lsl #7
	str	r4, [r7, #1640]
	bne	.L2878
	ldr	r3, .L3030+8
	ldr	r2, [r3, #2832]
	mov	r10, r3
	cbnz	r2, .L2869
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2878
.L2869:
	ldr	r3, [r7, #1052]
	ldr	r5, .L3030
	cmp	r3, #29
	bhi	.L2878
	ldr	r9, .L3030+12
	ldrh	r3, [r9, #-68]
	add	r4, r4, r3
	str	r4, [r5, #1640]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r5, #1072]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L2878
	ldrh	r2, [r5, #1690]
	cmp	r2, r3
	bne	.L2878
	ldr	r3, [r5, #1640]
	cmp	r3, #1024
	bhi	.L2870
	ldrh	r3, [r5, #832]
	cmp	r3, #63
	bhi	.L2878
.L2870:
	ldrh	r3, [r7, #1638]
	movs	r0, #0
	ldrh	r4, [r7, #832]
	adds	r3, r3, #64
	ldr	r2, .L3030+12
	cmp	r4, r3
	strh	r0, [r9, #-68]	@ movhi
	ldr	r1, .L3030
	bgt	.L2878
	ldr	r3, [r1, #1052]
	str	r0, [r1, #1640]
	cbnz	r3, .L2871
	movs	r3, #6
	b	.L3015
.L2871:
	cmp	r3, #5
	bhi	.L2872
	movs	r3, #18
.L3015:
	strh	r3, [r2, #-68]	@ movhi
.L2872:
	movs	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r2, r0
	cmp	r2, ip
	beq	.L2877
	ldrh	r0, [r7, #1644]
	ldr	r5, .L3030
	cmp	r0, #0
	beq	.L2874
	ldrh	r4, [r10, #2912]
	ldrh	r3, [r10, #2840]
	ldr	fp, [r5, #816]
	muls	r3, r4, r3
	ldrh	r1, [fp, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r1, r3
	bgt	.L2877
	adds	r1, r0, #1
	str	ip, [sp, #20]
	str	r2, [sp, #16]
	mov	r10, #0
	uxth	r1, r1
	str	r10, [r5, #1648]
	strh	r1, [r5, #1644]	@ movhi
	str	r1, [sp, #12]
	bl	List_get_gc_head_node
	ldr	ip, [sp, #20]
	ldr	r1, [sp, #12]
	ldr	r2, [sp, #16]
	uxth	r4, r0
	cmp	r4, ip
	beq	.L2877
	ldrh	r2, [fp, r2, lsl #1]
	ldrh	r3, [fp, r4, lsl #1]
	ldr	r0, .L3030+16
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, #1644]
	cmp	r3, #40
	bls	.L2875
	ldr	r3, [r5, #816]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r3, #32
	it	hi
	strhhi	r10, [r5, #1644]	@ movhi
.L2875:
	movs	r3, #6
	strh	r3, [r9, #-68]	@ movhi
	b	.L2894
.L2874:
	movs	r3, #1
	strh	r3, [r5, #1644]	@ movhi
.L2877:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L2894
	movs	r3, #0
	strh	r3, [r9, #-68]	@ movhi
.L2878:
	ldrh	r3, [r7, #1072]
	movw	r4, #65535
	ldr	r5, .L3030
	cmp	r3, r4
	bne	.L3022
	ldrh	r4, [r5, #932]
	cmp	r4, r3
	bne	.L2879
	ldrh	r10, [r5, #1690]
	cmp	r10, r4
	bne	.L2880
	ldrh	r3, [r5, #832]
	ldr	r2, [r5, #1640]
	cmp	r3, #24
	ite	cc
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	bls	.L2882
	ldr	r2, .L3030+12
	movs	r3, #0
	str	r3, [r7, #1640]
	mov	r9, r5
	strh	r3, [r2, #-68]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r10
	mov	r4, r0
	bne	.L2883
	ldrh	r2, [r5, #832]
	ldrh	r3, [r5, #1638]
	cmp	r2, r3
	bcs	.L2884
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	bne	.L3023
.L2893:
	bl	FtlGcReFreshBadBlk
	cmp	r8, #0
	beq	.L3024
.L2894:
	ldrh	r2, [r7, #932]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2905
.L2954:
	ldrh	r3, [r7, #1690]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L2905
	cmp	r4, r3
	bne	.L2905
.L2955:
	ldrh	r2, [r7, #1072]
	movw	r3, #65535
	ldr	r4, .L3030
	cmp	r2, r3
	beq	.L2906
.L2910:
	movw	r4, #65535
	b	.L2905
.L2906:
	movs	r3, #0
	ldrh	r9, [r4, #832]
	str	r3, [r4, #1648]
	ldrh	r3, [r4, #1636]
	ldr	r5, .L3030+12
	cmp	r3, r9
	bcs	.L2907
	ldrh	r3, [r5, #-68]
	cbnz	r3, .L2908
	ldr	r3, .L3030+8
	ldr	r2, [r4, #988]
	ldr	r3, [r3, #2968]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L2966
.L2908:
	ldrh	r3, [r7, #1056]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
	b	.L2909
.L2966:
	movs	r3, #18
.L2909:
	strh	r3, [r7, #1636]	@ movhi
	b	.L3019
.L2907:
	ldrh	r0, [r5, #-68]
	cmp	r0, #0
	bne	.L2910
	ldrh	r6, [r4, #1056]
	add	r3, r6, r6, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #1636]	@ movhi
	bl	List_get_gc_head_node
	ldr	r2, .L3030+8
	ldr	r3, [r4, #816]
	ldrh	r1, [r2, #2912]
	ldrh	r4, [r2, #2840]
	muls	r4, r1, r4
	add	r4, r4, r4, lsr #31
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, r4, asr #1
	ble	.L2911
	subs	r6, r6, #1
	cmp	r9, r6
	blt	.L2911
.L3019:
	bl	FtlReadRefresh
	b	.L3020
.L2911:
	cmp	r3, #0
	bne	.L2910
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r7, #832]
	adds	r0, r0, #1
	b	.L3014
.L2905:
	ldr	r3, .L3030+8
	ldr	r3, [r3, #2832]
	cmp	r3, #0
	ite	eq
	moveq	r5, #1
	movne	r5, #2
	b	.L2904
.L3031:
	.align	2
.L3030:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC134
.L3024:
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2894
.L2953:
	ldrh	r4, [r7, #832]
	cmp	r4, #24
	bhi	.L2962
	ldr	r3, .L3032
	cmp	r4, #16
	ldrh	r5, [r3, #2910]
	bls	.L2896
	lsrs	r5, r5, #5
.L2895:
	ldrh	r3, [r7, #1636]
	ldr	r2, .L3032+4
	cmp	r3, r4
	bcc	.L3025
.L2899:
	ldr	r3, .L3032
	ldr	r3, [r3, #2832]
	cmp	r3, #0
	bne	.L3026
.L2965:
	movw	r4, #65535
.L2904:
	ldrh	r2, [r7, #1072]
	movw	r1, #65535
	ldr	r3, .L3032+4
	cmp	r2, r1
	bne	.L2913
	cmp	r4, r2
	beq	.L2914
	strh	r4, [r3, #1072]	@ movhi
	b	.L2915
.L2914:
	ldrh	r2, [r3, #1690]
	cmp	r2, r4
	beq	.L2915
	ldr	r1, [r3, #816]
	ldrh	r2, [r1, r2, lsl #1]
	cbnz	r2, .L2916
	strh	r4, [r3, #1690]	@ movhi
.L2916:
	ldrh	r3, [r7, #1690]
	strh	r3, [r7, #1072]	@ movhi
	movw	r3, #65535
	strh	r3, [r7, #1690]	@ movhi
.L2915:
	ldrh	r0, [r7, #1072]
	movw	r6, #65535
	movs	r3, #0
	strb	r3, [r7, #1080]
	cmp	r0, r6
	beq	.L2913
	bl	IsBlkInGcList
	cbz	r0, .L2918
	ldr	r3, .L3032+4
	strh	r6, [r3, #1072]	@ movhi
.L2918:
	ldr	r3, .L3032
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	cbz	r3, .L2919
	ldrh	r0, [r7, #1072]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #1080]
.L2919:
	ldrh	r2, [r7, #1072]
	movw	r3, #65535
	ldr	r6, .L3032+4
	cmp	r2, r3
	beq	.L2913
	add	r0, r6, #1072
	bl	make_superblock
	ldr	r2, .L3032+8
	movs	r3, #0
	ldrh	r1, [r6, #1072]
	strh	r3, [r6, #1074]	@ movhi
	strb	r3, [r6, #1078]
	strh	r3, [r2, #-66]	@ movhi
	ldr	r3, [r6, #816]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #-64]	@ movhi
.L2913:
	ldrh	r3, [r7, #1072]
	ldrh	r2, [r7, #836]
	cmp	r2, r3
	beq	.L2920
	ldr	r2, .L3032+4
	ldrh	r2, [r2, #884]
	cmp	r2, r3
	beq	.L2920
.L2921:
	ldr	r7, .L3032
	b	.L2922
.L2920:
	movw	r3, #65535
	strh	r3, [r7, #1072]	@ movhi
	b	.L3021
.L2943:
	ldrh	r3, [r6, #1074]
	add	r5, r5, r3
	ldr	r3, [sp, #12]
	uxth	r5, r5
	strh	r5, [r6, #1074]	@ movhi
	cmp	r5, r3
	bcs	.L3027
.L2945:
	ldrh	r3, [r6, #832]
	cmp	r3, #2
	bhi	.L2948
	ldrh	r5, [r7, #2910]
.L2922:
	ldr	r6, .L3032+4
	movw	r3, #65535
	ldrh	r2, [r6, #1072]
	mov	fp, r6
	cmp	r2, r3
	bne	.L2923
	mov	r10, r6
	movs	r3, #0
	str	r3, [r6, #1648]
.L2924:
	ldrh	r9, [fp, #1644]
	mov	r0, r9
	bl	List_get_gc_head_node
	movw	r2, #65535
	uxth	r3, r0
	strh	r3, [fp, #1072]	@ movhi
	cmp	r3, r2
	bne	.L2925
	ldr	r3, .L3032+4
	movs	r2, #0
	movs	r0, #8
	strh	r2, [r3, #1644]	@ movhi
	b	.L3014
.L2925:
	mov	r0, r3
	str	r3, [sp, #12]
	bl	IsBlkInGcList
	add	r9, r9, #1
	ldr	r3, [sp, #12]
	cbz	r0, .L2926
	strh	r9, [r10, #1644]	@ movhi
	b	.L2924
.L2926:
	ldrh	lr, [r7, #2840]
	uxth	r9, r9
	ldrh	r2, [r7, #2910]
	ldr	r1, [r10, #816]
	strh	r9, [r10, #1644]	@ movhi
	mul	r2, lr, r2
	ldrh	r0, [r1, r3, lsl #1]
	add	lr, r2, r2, lsr #31
	cmp	r0, lr, asr #1
	bgt	.L2928
	cmp	r9, #48
	bls	.L2929
	cmp	r0, #8
	bls	.L2929
	ldrh	r0, [r10, #1680]
	cmp	r0, #35
	bhi	.L2929
.L2928:
	movs	r0, #0
	strh	r0, [fp, #1644]	@ movhi
.L2929:
	ldrh	r1, [r1, r3, lsl #1]
	cmp	r1, r2
	blt	.L2930
	movw	r2, #65535
	cmp	r4, r2
	bne	.L2930
	movs	r3, #0
	strh	r4, [fp, #1072]	@ movhi
	strh	r3, [fp, #1644]	@ movhi
.L3021:
	ldr	r3, .L3032+8
	ldrh	r0, [r3, #-68]
	b	.L3014
.L2930:
	cbnz	r1, .L2931
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [fp, #1644]
	adds	r3, r3, #1
	strh	r3, [fp, #1644]	@ movhi
	b	.L2924
.L2931:
	movs	r2, #0
	strb	r2, [r6, #1080]
	ldr	r2, .L3032
	ldrb	r2, [r2, #76]	@ zero_extendqisi2
	cbz	r2, .L2932
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3032+4
	strb	r0, [r3, #1080]
.L2932:
	ldr	r0, .L3032+12
	bl	make_superblock
	ldr	r2, .L3032+8
	ldrh	r0, [r6, #1072]
	movs	r3, #0
	ldr	r1, [r6, #816]
	strh	r3, [r2, #-66]	@ movhi
	ldrh	r1, [r1, r0, lsl #1]
	strh	r3, [r6, #1074]	@ movhi
	strb	r3, [r6, #1078]
	strh	r1, [r2, #-64]	@ movhi
.L2923:
	cmp	r8, #1
	bne	.L2933
	bl	FtlReadRefresh
.L2933:
	movs	r3, #1
	str	r3, [r6, #1740]
	ldrh	r3, [r7, #2910]
	str	r3, [sp, #12]
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbz	r3, .L2934
	ldr	r3, .L3032+4
	ldrb	r3, [r3, #1080]	@ zero_extendqisi2
	cmp	r3, #1
	itt	eq
	ldrheq	r3, [r7, #2912]
	streq	r3, [sp, #12]
.L2934:
	ldrh	r3, [r6, #1074]
	ldr	r1, [sp, #12]
	adds	r2, r3, r5
	cmp	r2, r1
	ittt	gt
	movgt	r2, r1
	subgt	r3, r2, r3
	uxthgt	r5, r3
	movs	r3, #0
.L3018:
	str	r3, [sp, #16]
	ldrh	r3, [sp, #16]
	ldr	r6, .L3032+4
	cmp	r3, r5
	bcs	.L2943
	ldr	r3, [r6, #1668]
	addw	r1, r6, #1086
	ldr	r2, [sp, #16]
	movw	lr, #65535
	ldrh	r10, [r7, #2840]
	str	r3, [sp, #20]
	ldrh	r3, [r6, #1074]
	add	ip, r3, r2
	movs	r3, #0
	mov	r9, r3
.L2944:
	uxth	r2, r3
	cmp	r2, r10
	bcs	.L3028
	ldrh	r2, [r1, #2]!
	cmp	r2, lr
	beq	.L2937
	ldr	r0, [sp, #20]
	mov	fp, #36
	orr	r2, ip, r2, lsl #10
	mla	fp, fp, r9, r0
	add	r9, r9, #1
	uxth	r9, r9
	str	r2, [fp, #4]
.L2937:
	adds	r3, r3, #1
	b	.L2944
.L3033:
	.align	2
.L3032:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR2+1072
.L3028:
	ldr	r0, [r6, #1668]
	mov	r1, r9
	ldrb	r2, [r6, #1080]	@ zero_extendqisi2
	mov	fp, #0
	bl	FlashReadPages
	ldr	r6, .L3034
.L2939:
	uxth	r3, fp
	cmp	r3, r9
	bcs	.L3029
	mov	ip, #36
	ldr	r2, [r6, #1668]
	mul	r10, ip, fp
	add	r3, r2, r10
	ldr	r2, [r2, r10]
	ldr	r3, [r3, #12]
	adds	r2, r2, #1
	beq	.L2968
	ldrh	r1, [r3]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L2968
	add	r1, sp, ip
	ldr	r0, [r3, #8]
	movs	r2, #0
	str	ip, [sp, #24]
	str	r3, [sp, #20]
	bl	log2phys
	ldr	r1, [r6, #1668]
	ldr	r2, [sp, #36]
	add	r1, r1, r10
	ldr	r3, [sp, #20]
	bic	r2, r2, #-2147483648
	ldr	ip, [sp, #24]
	ldr	r0, [r1, #4]
	cmp	r2, r0
	bne	.L2968
	str	r3, [sp, #28]
	ldr	r3, .L3034+4
	ldr	r0, [r6, #1652]
	ldr	r1, [r1, #16]
	ldrh	r2, [r3, #-66]
	adds	r2, r2, #1
	strh	r2, [r3, #-66]	@ movhi
	ldr	r2, [r6, #1768]
	mla	r2, ip, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #20]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [sp, #20]
	ldr	r1, [r6, #1652]
	ldr	ip, [sp, #24]
	ldr	r3, [sp, #28]
	str	r0, [r2, #4]
	ldr	r2, [r6, #1768]
	mla	ip, ip, r1, r2
	ldr	r2, [r6, #1668]
	add	r2, r2, r10
	ldr	r1, [r2, #8]
	str	r1, [ip, #8]
	movs	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [ip, #12]
	ldr	r2, [sp, #36]
	str	r2, [r3, #12]
	ldrh	r2, [r6, #932]
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r6, #1032]
	ldr	r0, [r6, #1668]
	str	r2, [r3, #4]
	add	r0, r0, r10
	ldr	r3, [r6, #1652]
	adds	r3, r3, #1
	str	r3, [r6, #1652]
	bl	FtlGcBufAlloc
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbnz	r3, .L2941
	ldrb	r3, [r6, #939]	@ zero_extendqisi2
	ldr	r2, [r6, #1652]
	cmp	r2, r3
	beq	.L2941
	ldrh	r3, [r6, #936]
	cbnz	r3, .L2968
.L2941:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L2968
	ldr	r3, .L3034
	movs	r2, #0
	str	r2, [r3, #1740]
	b	.L3021
.L2968:
	add	fp, fp, #1
	b	.L2939
.L3029:
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	b	.L3018
.L3027:
	ldr	r3, [r6, #1652]
	ldr	r5, .L3034+4
	cbz	r3, .L2946
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L2946
	movs	r3, #0
	str	r3, [r6, #1740]
.L3020:
	ldrh	r0, [r5, #-68]
	b	.L3014
.L2946:
	ldrh	r1, [r5, #-66]
	cbnz	r1, .L2947
	ldrh	r2, [r6, #1072]
	ldr	r3, [r6, #816]
	ldrh	r0, [r3, r2, lsl #1]
	cbz	r0, .L2947
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldr	r3, .L3034
	ldrh	r0, [r3, #1072]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2947:
	movw	r3, #65535
	strh	r3, [r6, #1072]	@ movhi
	b	.L2945
.L2948:
	ldr	r2, .L3034
	movs	r1, #0
	str	r1, [r2, #1740]
	ldr	r2, .L3034+4
	ldrh	r0, [r2, #-68]
	cmp	r0, #0
	bne	.L3014
	adds	r0, r3, #1
	b	.L3014
.L2956:
	movs	r0, #0
	b	.L3014
.L2958:
	mov	r0, r3
	b	.L3014
.L3023:
	ldr	r2, [r5, #1736]
	ldr	r5, .L3034+8
	cbnz	r2, .L2886
	ldrh	r2, [r5, #2860]
	cmp	r2, #3
	beq	.L2886
	ldr	r2, [r9, #1120]
	cbnz	r2, .L2886
	ldr	r2, [r5, #2832]
	cbnz	r2, .L2886
	ldrb	r0, [r5, #76]	@ zero_extendqisi2
	cbz	r0, .L2887
.L2886:
	ldr	r2, [r7, #816]
	ldrh	r0, [r5, #2860]
	ldrh	r1, [r2, r3, lsl #1]
	cmp	r0, #3
	ldrh	r3, [r5, #2912]
	ldrh	r2, [r5, #2840]
	mul	r2, r2, r3
	ite	eq
	lsreq	r3, r3, #1
	movne	r3, #0
	add	r3, r3, r2
	cmp	r1, r3
	bgt	.L2889
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #2968]
	ldr	r2, .L3034
	ldr	r1, [r7, #988]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	ite	hi
	movhi	r3, #128
	movls	r3, #160
	strh	r3, [r2, #1638]	@ movhi
	uxth	r4, r0
	b	.L2891
.L2889:
	movs	r3, #128
	b	.L3017
.L2887:
	ldr	r2, [r9, #816]
	ldrh	r3, [r2, r3, lsl #1]
	cmp	r3, #7
	bhi	.L2892
	bl	List_get_gc_head_node
	movs	r3, #128
	strh	r3, [r7, #1638]	@ movhi
	uxth	r4, r0
	b	.L2891
.L2892:
	movs	r3, #64
.L3017:
	strh	r3, [r7, #1638]	@ movhi
	b	.L2893
.L2884:
	movs	r3, #80
	strh	r3, [r5, #1638]	@ movhi
	b	.L2893
.L2891:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L2893
.L2883:
	ldr	r1, [r7, #752]
	ldr	r3, [r7, #816]
	ldrh	r2, [r7, #832]
	ldrh	r1, [r1, r4, lsl #1]
	ldrh	r3, [r3, r4, lsl #1]
	ldr	r0, .L3034+12
	str	r1, [sp]
	ldrh	r1, [r7, #1636]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
	b	.L2893
.L2896:
	cmp	r4, #12
	bls	.L2897
	lsrs	r5, r5, #4
	b	.L2895
.L2897:
	cmp	r4, #8
	bls	.L2895
	lsrs	r5, r5, #2
	b	.L2895
.L2962:
	movs	r5, #1
	b	.L2895
.L3025:
	ldrh	r3, [r2, #932]
	movw	r1, #65535
	cmp	r3, r1
	bne	.L2900
	ldrh	r1, [r2, #1690]
	cmp	r1, r3
	bne	.L2900
	ldr	r3, .L3034+4
	ldrh	r0, [r3, #-68]
	cbnz	r0, .L2901
	ldr	r3, .L3034+8
	ldr	r2, [r2, #988]
	ldr	r3, [r3, #2968]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L2963
.L2901:
	ldrh	r3, [r7, #1056]
	add	r3, r3, r3, lsl #1
	ubfx	r3, r3, #2, #16
	b	.L2902
.L2963:
	movs	r3, #18
.L2902:
	strh	r3, [r7, #1636]	@ movhi
	movs	r3, #0
	str	r3, [r7, #1648]
	b	.L3014
.L2900:
	ldrh	r3, [r7, #1056]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r7, #1636]	@ movhi
	b	.L2899
.L3026:
	cmp	r6, #2
	bhi	.L2965
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L2965
.L3022:
	cmp	r8, #0
	beq	.L2953
	b	.L2894
.L2879:
	cmp	r8, #0
	beq	.L2953
	b	.L2910
.L2880:
	cmp	r8, #0
	beq	.L2953
	b	.L2954
.L2882:
	cmp	r8, #0
	beq	.L2953
	b	.L2955
.L3014:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3035:
	.align	2
.L3034:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LC135
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
	ldr	r3, .L3078
	ldr	r6, .L3078+4
	ldr	r5, [r3, #-60]
	ldr	r3, [r6, #744]
	cmp	r3, #0
	bne	.L3038
	ldr	r4, .L3078+8
	ldr	r1, [r4, #2960]
	cmp	r1, #0
	beq	.L3038
	ldrb	r7, [r4, #76]	@ zero_extendqisi2
	cbz	r7, .L3040
	ldrb	r7, [r5, #8]	@ zero_extendqisi2
	subs	r0, r7, #1
	rsbs	r7, r0, #0
	adcs	r7, r7, r0
.L3040:
	ldr	r0, [r4, #2964]
	mov	r2, r7
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r9, #0
	bl	FlashProgPages
	ldr	r10, .L3078+8
	mov	fp, #36
.L3041:
	ldr	r3, [r4, #2960]
	cmp	r9, r3
	bcs	.L3058
	mul	r8, fp, r9
	ldr	r3, [r10, #2964]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	beq	.L3061
	ldr	r3, [r2, #4]
	cbz	r7, .L3074
	orr	r3, r3, #-2147483648
.L3074:
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	movs	r2, #1
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2964]
	add	r8, r8, r3
	ldr	r3, [r8, #12]
	ldr	r0, [r3, #12]
	adds	r2, r0, #1
	beq	.L3045
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r6, #816]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r8, r0
	cbnz	r2, .L3046
	ldr	r0, .L3078+12
	mov	r1, r8
	bl	printk
.L3046:
	mov	r0, r8
	bl	decrement_vpc_count
.L3045:
	add	r9, r9, #1
	b	.L3041
.L3076:
	movw	r5, #16386
.L3057:
	ldrh	r3, [r6, #1694]
	cbz	r3, .L3058
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3057
.L3058:
	movs	r3, #0
	str	r3, [r4, #2960]
	b	.L3038
.L3061:
	ldr	r10, .L3078+8
	mov	r9, #0
.L3042:
	ldr	r3, [r4, #2960]
	cmp	r9, r3
	bcs	.L3076
	mov	r8, #36
	ldr	r3, [r10, #2964]
	mov	fp, #0
	mov	r2, #-1
	mul	r8, r8, r9
	str	r2, [r3, r8]
.L3048:
	ldr	r3, [r4, #2964]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	bne	.L3077
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3049
	ldr	r1, [r6, #816]
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r10, #2910]
	strb	fp, [r5, #6]
	strh	fp, [r5, #4]	@ movhi
	strh	r3, [r5, #2]	@ movhi
.L3049:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L3050
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3050:
	ldr	r3, [r6, #1220]
	adds	r3, r3, #1
	str	r3, [r6, #1220]
	ldr	r3, [r4, #2964]
	add	r3, r3, r8
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #2964]
	movs	r1, #1
	mov	r2, r7
	add	r3, r3, r8
	str	r0, [sp, #4]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r6, #744]
	cmp	r3, #0
	beq	.L3048
	b	.L3038
.L3077:
	ldr	r3, [r2, #4]
	cbz	r7, .L3075
	orr	r3, r3, #-2147483648
.L3075:
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	movs	r2, #1
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2964]
	add	r8, r8, r3
	ldr	r3, [r8, #12]
	ldr	r0, [r3, #12]
	adds	r3, r0, #1
	beq	.L3054
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r6, #816]
	ldrh	r2, [r3, r0, lsl #1]
	mov	r8, r0
	cbnz	r2, .L3055
	ldr	r0, .L3078+12
	mov	r1, r8
	bl	printk
.L3055:
	mov	r0, r8
	bl	decrement_vpc_count
.L3054:
	add	r9, r9, #1
	b	.L3042
.L3038:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3079:
	.align	2
.L3078:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC136
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
	ldr	r3, .L3082
	ldr	r3, [r3, #744]
	cbnz	r3, .L3081
	bl	FtlCacheWriteBack
	bl	l2p_flush
	movs	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3081:
	movs	r0, #0
	pop	{r3, pc}
.L3083:
	.align	2
.L3082:
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
	ldr	r3, .L3086
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3085
	bl	FtlSysFlush
.L3085:
	movs	r0, #0
	pop	{r3, pc}
.L3087:
	.align	2
.L3086:
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
	ldr	r0, .L3090
	bl	printk
	pop	{r3, lr}
	b	ftl_deinit
.L3091:
	.align	2
.L3090:
	.word	.LC137
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
	ldr	r7, .L3111
	mov	r6, r0
	mov	r5, r1
	ldr	r3, [r7, #1896]
	cmp	r2, r3
	bhi	.L3101
	cmp	r1, #31
	bls	.L3103
	ldr	r3, .L3111+4
	ldr	r2, [r3, #744]
	mov	r8, r3
	cmp	r2, #0
	bne	.L3103
	bl	FtlCacheWriteBack
	ldrh	r4, [r7, #2916]
	mov	r0, r6
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r9, r0
	subs	r6, r6, r3
	uxth	r6, r6
	cbz	r6, .L3096
	subs	r4, r4, r6
	add	r9, r0, #1
	cmp	r4, r5
	it	cs
	movcs	r4, r5
	uxth	r4, r4
	subs	r5, r5, r4
.L3096:
	ldr	r4, .L3111+8
	mov	r3, #-1
	str	r3, [sp, #4]
.L3097:
	ldrh	r3, [r7, #2916]
	cmp	r5, r3
	bcc	.L3110
	mov	r0, r9
	mov	r1, sp
	movs	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L3098
	ldr	r3, [r4, #-56]
	add	r1, sp, #4
	movs	r2, #1
	mov	r0, r9
	adds	r3, r3, #1
	str	r3, [r4, #-56]
	ldr	r3, [r8, #1000]
	adds	r3, r3, #1
	str	r3, [r8, #1000]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3098:
	ldrh	r3, [r7, #2916]
	add	r9, r9, #1
	subs	r5, r5, r3
	b	.L3097
.L3110:
	ldr	r3, .L3111+8
	ldr	r2, [r3, #-56]
	cmp	r2, #32
	bls	.L3103
	movs	r2, #0
	str	r2, [r3, #-56]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3103
.L3101:
	mov	r0, #-1
	b	.L3095
.L3103:
	movs	r0, #0
.L3095:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3112:
	.align	2
.L3111:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
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
	ldr	r4, .L3151
	ldr	r6, .L3151+4
	ldr	r2, [r4, #744]
	ldrh	r1, [r6, #2910]
	cmp	r2, #0
	bne	.L3149
	ldrh	r5, [r4, #932]
	movw	r7, #65535
	cmp	r5, r7
	bne	.L3117
.L3126:
	ldrh	r2, [r4, #932]
	movw	r3, #65535
	movs	r7, #0
	ldr	r5, .L3151
	cmp	r2, r3
	str	r7, [r4, #1928]
	beq	.L3149
	bl	FtlCacheWriteBack
	ldrb	r0, [r5, #939]	@ zero_extendqisi2
	ldrh	r3, [r6, #2910]
	mov	r10, #12
	ldr	r2, [r5, #816]
	ldrh	r1, [r5, #932]
	smulbb	r3, r0, r3
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r3, [r5, #1016]
	ldrh	r2, [r5, #1688]
	add	r3, r3, r2
	str	r3, [r5, #1016]
	b	.L3127
.L3117:
	cbz	r0, .L3120
	ldr	r3, .L3151+8
	ldrh	r0, [r3, #3064]
	cmp	r0, r7
	beq	.L3121
.L3122:
	movs	r1, #2
	b	.L3120
.L3121:
	strh	r2, [r3, #3064]	@ movhi
	ldrh	r3, [r4, #832]
	cmp	r3, #17
	bhi	.L3122
.L3120:
	ldr	r7, .L3151
	add	r0, r7, #932
	bl	FtlGcScanTempBlk
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L3123
	ldr	r2, [r7, #752]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L3124
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L3124:
	ldr	r3, [r4, #1928]
	ldr	r2, .L3151
	cbnz	r3, .L3125
	ldr	r0, [sp, #4]
	ldr	r3, [r2, #1220]
	ubfx	r0, r0, #10, #16
	adds	r3, r3, #1
	str	r3, [r2, #1220]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3125:
	movs	r3, #0
	str	r3, [r4, #1928]
	b	.L3137
.L3123:
	ldr	r3, .L3151+8
	ldrh	r2, [r3, #3064]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3137
	b	.L3126
.L3130:
	mul	r8, r10, r8
	ldr	fp, [r5, #1684]
	add	r1, sp, #4
	movs	r2, #0
	add	r9, fp, r8
	ldr	r0, [r9, #8]
	bl	log2phys
	ldr	r0, [sp, #4]
	ldr	r3, [fp, r8]
	cmp	r0, r3
	bne	.L3128
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r9, #4
	movs	r2, #1
	mov	r8, r0
	ldr	r0, [r9, #8]
	bl	log2phys
	mov	r0, r8
.L3148:
	bl	decrement_vpc_count
.L3129:
	adds	r7, r7, #1
.L3127:
	ldrh	r3, [r4, #1688]
	uxth	r8, r7
	cmp	r3, r8
	bhi	.L3130
	b	.L3150
.L3128:
	ldr	r3, [r9, #4]
	cmp	r0, r3
	beq	.L3129
	ldrh	r0, [r5, #932]
	b	.L3148
.L3150:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r6, #76]	@ zero_extendqisi2
	cbz	r3, .L3131
	ldr	r3, .L3151
	ldr	r0, .L3151+12
	ldrh	r1, [r3, #932]
	bl	printk
.L3131:
	ldrh	r0, [r4, #932]
	ldr	r3, [r4, #816]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L3132
	bl	INSERT_DATA_LIST
	b	.L3133
.L3132:
	bl	INSERT_FREE_LIST
.L3133:
	movw	r3, #65535
	strh	r3, [r4, #932]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #1688]	@ movhi
	strh	r3, [r4, #1680]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r6, #2832]
	ldr	r5, .L3151
	cbz	r3, .L3134
	ldr	r3, [r5, #1052]
	cmp	r3, #29
	bhi	.L3134
	ldrh	r3, [r5, #1056]
	ldrh	r2, [r5, #832]
	cmp	r2, r3
	itt	cc
	lslcc	r3, r3, #1
	strhcc	r3, [r5, #1636]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #1072]	@ movhi
	b	.L3149
.L3134:
	ldrh	r3, [r4, #1056]
	ldrh	r1, [r4, #832]
	ldr	r2, .L3151
	add	r0, r3, r3, lsl #1
	cmp	r1, r0, asr #2
	ble	.L3149
	ldrb	r0, [r6, #76]	@ zero_extendqisi2
	movw	r1, #65535
	strh	r1, [r2, #1072]	@ movhi
	cbz	r0, .L3136
	subs	r3, r3, #2
	strh	r3, [r2, #1636]	@ movhi
.L3149:
	movs	r0, #0
	b	.L3116
.L3136:
	movs	r3, #20
	strh	r3, [r2, #1636]	@ movhi
	b	.L3116
.L3137:
	movs	r0, #1
.L3116:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3152:
	.align	2
.L3151:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC138
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
	ldr	r3, .L3171
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldrb	r3, [r3, #76]	@ zero_extendqisi2
	ldr	r5, .L3171+4
	cbz	r3, .L3154
	ldr	r3, [r5, #1652]
	lsls	r4, r3, #31
	bpl	.L3154
	ldrh	r3, [r5, #936]
	cbz	r3, .L3154
.L3159:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3154:
	movs	r2, #0
	ldr	r0, [r5, #1768]
	ldr	r1, [r5, #1652]
	movs	r6, #0
	mov	r3, r2
	ldr	r7, .L3171+4
	bl	FlashProgPages
	mov	r8, #36
.L3156:
	ldr	r1, [r5, #1652]
	uxth	r3, r6
	ldr	r4, .L3171+4
	cmp	r3, r1
	bcs	.L3170
	mul	r3, r8, r3
	ldr	r1, [r7, #1768]
	adds	r6, r6, #1
	adds	r2, r1, r3
	ldr	r1, [r1, r3]
	adds	r0, r1, #1
	bne	.L3157
	ldrh	r0, [r4, #932]
	movs	r5, #0
	ldr	r2, [r4, #816]
	strh	r5, [r2, r0, lsl #1]	@ movhi
	ldr	r2, [r4, #1220]
	strh	r1, [r4, #932]	@ movhi
	adds	r2, r2, #1
	str	r2, [r4, #1220]
	ldr	r2, [r4, #1768]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3169
.L3157:
	ldr	r3, [r2, #12]
	ldr	r1, [r2, #4]
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L3156
.L3170:
	ldr	r0, [r4, #1768]
	bl	FtlGcBufFree
	movs	r3, #0
	str	r3, [r4, #1652]
	ldrh	r3, [r4, #936]
	cmp	r3, #0
	bne	.L3159
	movs	r0, #1
	bl	FtlGcFreeTempBlock
.L3169:
	movs	r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L3172:
	.align	2
.L3171:
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
	ldr	r5, .L3175
	ldr	r4, .L3175+4
	ldrh	r1, [r5, #2910]
	add	r0, r4, #932
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #934]
	ldrh	r3, [r5, #2910]
	cmp	r2, r3
	bcc	.L3173
	addw	r0, r4, #1868
	bl	FtlMapBlkWriteDumpData
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #1928]
.L3173:
	pop	{r3, r4, r5, pc}
.L3176:
	.align	2
.L3175:
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
	ldr	r4, .L3178
	ldr	r3, .L3178+4
	add	r6, r4, #836
	add	r4, r4, #884
	mov	r0, r6
	str	r5, [r3, #-200]
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
.L3179:
	.align	2
.L3178:
	.word	.LANCHOR2
	.word	.LANCHOR4
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
	ldr	r5, .L3196
	movw	r6, #65535
	ldr	r7, .L3196+4
	ldr	r4, .L3196+8
	ldrh	r0, [r5, #2844]
	strh	r3, [r7, #-204]	@ movhi
	strh	r6, [r7, #-206]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r3, [r4, #1060]
	cmp	r3, r6
	bne	.L3181
.L3183:
	mov	r6, #-1
	b	.L3182
.L3181:
	bl	FtlLoadSysInfo
	mov	r6, r0
	cmp	r0, #0
	bne	.L3183
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r5, #2950]
	ldr	r2, [r4, #980]
	movs	r0, #12
	mov	r3, r6
.L3184:
	cmp	r3, r1
	bge	.L3189
	mla	lr, r0, r3, r2
	ldr	lr, [lr, #4]
	cmp	lr, #0
	bge	.L3185
.L3189:
	ldrh	r2, [r4, #788]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #788]	@ movhi
	bge	.L3195
	b	.L3186
.L3185:
	adds	r3, r3, #1
	b	.L3184
.L3195:
	ldrh	r3, [r7, #-204]
	cmp	r3, #0
	beq	.L3190
.L3186:
	ldr	r0, .L3196+12
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L3196+16
	bl	FtlSuperblockPowerLostFix
	ldrh	r1, [r4, #836]
	ldr	r2, [r4, #816]
	ldrh	r0, [r4, #840]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r1, [r4, #816]
	ldrh	r3, [r5, #2910]
	ldrh	r0, [r4, #884]
	ldrh	r7, [r4, #888]
	strh	r3, [r4, #838]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #842]
	strh	r3, [r4, #840]	@ movhi
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r7
	strh	r2, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r5, #2910]
	ldr	r0, .L3196+20
	strb	r3, [r4, #890]
	strh	r2, [r4, #886]	@ movhi
	strh	r3, [r4, #888]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	r0, .L3196+24
	bl	FtlMapBlkWriteDumpData
	ldrh	r3, [r4, #790]
	adds	r3, r3, #1
	strh	r3, [r4, #790]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3190:
	ldrh	r2, [r4, #836]
	movw	r3, #65535
	ldr	r5, .L3196+8
	cmp	r2, r3
	beq	.L3191
	ldrh	r3, [r5, #840]
	cbnz	r3, .L3191
	ldrh	r3, [r5, #888]
	cbnz	r3, .L3191
	bl	FtlVpcTblFlush
	ldrh	r0, [r5, #836]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r5, #884]
	bl	FtlGcRefreshOpenBlock
	add	r0, r5, #836
	bl	allocate_new_data_superblock
	add	r0, r5, #884
	bl	allocate_new_data_superblock
	addw	r0, r5, #4092
	bl	FtlMapBlkWriteDumpData
.L3191:
	ldrh	r3, [r4, #788]
	lsls	r3, r3, #27
	bne	.L3182
	bl	FtlVpcCheckAndModify
.L3182:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L3197:
	.align	2
.L3196:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2+836
	.word	.LANCHOR2+884
	.word	.LANCHOR2+1868
	.word	.LANCHOR2+4092
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
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
	ldr	r4, .L3201
	ldrh	r2, [r4, #932]
	cmp	r2, r3
	beq	.L3199
	ldrh	r3, [r4, #936]
	cbnz	r3, .L3200
.L3199:
	movs	r5, #0
	bl	FtlCacheWriteBack
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3201+4
	strb	r5, [r4, #940]
	bl	allocate_data_superblock
	strh	r5, [r4, #1680]	@ movhi
	strh	r5, [r4, #1688]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3200:
	ldr	r0, .L3201+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L3202:
	.align	2
.L3201:
	.word	.LANCHOR2
	.word	.LANCHOR2+932
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
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
	mov	r3, #-1
	ldr	r2, .L3217
	ldr	r6, .L3217+4
	ldr	r5, .L3217+8
	ldr	r4, .L3217+12
	ldr	r1, .L3217+16
	ldr	r0, .L3217+20
	str	r3, [r6, #504]
	movs	r3, #0
	str	r3, [r2, #-52]
	str	r3, [r5, #744]
	bl	printk
	add	r0, r4, #1864
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #2844]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3204
	ldr	r0, .L3217+24
	b	.L3216
.L3204:
	bl	FtlSysBlkInit
	cbz	r0, .L3206
	ldr	r0, .L3217+28
.L3216:
	ldr	r1, .L3217+32
	bl	printk
	b	.L3205
.L3206:
	movs	r1, #1
	str	r1, [r6, #504]
	bl	ftl_do_gc
	ldrh	r3, [r5, #832]
	cmp	r3, #15
	bhi	.L3207
	mov	r4, #1024
.L3208:
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3208
	b	.L3205
.L3207:
	ldrb	r3, [r4, #76]	@ zero_extendqisi2
	cbz	r3, .L3205
	movs	r4, #128
.L3210:
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3210
.L3205:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L3218:
	.align	2
.L3217:
	.word	.LANCHOR4
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC78
	.word	.LC77
	.word	.LC139
	.word	.LC140
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
	ldr	r6, .L3223
	ldr	r4, .L3223+4
	movs	r5, #0
	mov	r1, r6
	str	r5, [r1, #-44]!
	str	r5, [r4, #3984]
	str	r0, [r6, #-48]
	add	r0, r4, #3984
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #3984]
	cbz	r3, .L3222
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r6, #-48]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #3984]
	bl	FlashInit
	mov	r4, r0
	cbnz	r0, .L3221
	bl	FtlInit
.L3221:
	mov	r1, r4
	ldr	r0, .L3223+8
	bl	printk
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3222:
	mov	r0, #-1
	pop	{r4, r5, r6, pc}
.L3224:
	.align	2
.L3223:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LC141
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
	ldr	r3, .L3240
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #48
	sub	sp, sp, #48
	ldrb	r2, [r3, #76]	@ zero_extendqisi2
	mov	r8, r3
	cmp	r2, #0
	beq	.L3225
	ldr	r4, .L3240+4
	movw	r7, #4097
	ldr	r6, .L3240+8
	ldr	r0, .L3240+12
	ldr	r3, [r4, #816]
	ldrh	r5, [r6, #-206]
	mov	r1, r5
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldrh	r0, [r4, #836]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4, #884]
	bl	FtlGcRefreshOpenBlock
	add	r0, r4, #836
	bl	allocate_new_data_superblock
	add	r0, r4, #884
	bl	allocate_new_data_superblock
	lsl	r9, r5, #1
.L3227:
	subs	r7, r7, #1
	beq	.L3231
	movs	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #816]
	ldrh	r3, [r3, r9]
	cmp	r3, #0
	bne	.L3227
.L3231:
	ldr	r3, [r4, #816]
	mov	r1, r5
	ldr	r0, .L3240+12
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	ldr	r3, [r4, #816]
	ldrh	r7, [r3, r5, lsl #1]
	cbnz	r7, .L3229
	add	r0, sp, #48
	mov	r10, #36
	movw	r9, #65535
	strh	r5, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldr	r3, .L3240+4
	ldrh	ip, [r8, #2840]
	add	r0, sp, #14
	ldr	r8, [r3, #748]
	mov	r3, r7
	mov	lr, r3
.L3232:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3239
	ldrh	r2, [r0, #2]!
	cmp	r2, r9
	beq	.L3233
	mla	r1, r10, r7, r8
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [r1, #4]
	str	lr, [r1, #8]
	str	lr, [r1, #12]
.L3233:
	adds	r3, r3, #1
	b	.L3232
.L3239:
	ldr	r3, [r4, #816]
	mov	r1, r5
	ldr	r0, .L3240+16
	ldrh	r2, [r3, r5, lsl #1]
	bl	printk
	movs	r1, #0
	mov	r2, r7
	ldr	r0, [r4, #748]
	bl	FlashEraseBlocks
	ldr	r0, [r4, #748]
	movs	r1, #1
	mov	r2, r7
	bl	FlashEraseBlocks
.L3229:
	movw	r3, #65535
	strh	r3, [r6, #-206]	@ movhi
.L3225:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3241:
	.align	2
.L3240:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC142
	.word	.LC143
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
	cmp	r0, #16
	.pad #60
	sub	sp, sp, #60
	mov	r6, r1
	mov	r7, r3
	str	r2, [sp, #16]
	bne	.L3244
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	b	.L3245
.L3244:
	ldr	r3, [sp, #16]
	adds	r3, r1, r3
	str	r3, [sp, #12]
	ldr	r3, .L3284
	ldr	r1, [sp, #12]
	ldr	r2, [r3, #1896]
	cmp	r1, r2
	bhi	.L3265
	ldrh	r4, [r3, #2916]
	mov	r0, r6
	mov	r1, r4
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	mov	r1, r4
	str	r0, [sp, #4]
	subs	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	ldr	r1, [sp, #16]
	rsb	r3, r3, #1
	add	r3, r3, r0
	str	r3, [sp]
	ldr	r3, .L3284+4
	str	r0, [sp, #8]
	ldr	r0, [sp, #4]
	ldr	r2, [r3, #1024]
	add	r2, r2, r1
	ldr	r1, [sp]
	str	r2, [r3, #1024]
	ldr	r2, [r3, #996]
	add	r2, r2, r1
	ldr	r1, [sp, #8]
	str	r2, [r3, #996]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3246
	bl	FtlCacheWriteBack
.L3246:
	mov	r9, #0
	ldr	r4, [sp, #4]
	ldr	r10, .L3284+4
	ldr	r8, .L3284
	mov	r5, r9
	str	r9, [sp, #20]
	str	r9, [sp, #24]
.L3247:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L3283
	add	r1, sp, #52
	movs	r2, #0
	mov	r0, r4
	bl	log2phys
	ldr	r2, [sp, #52]
	adds	r1, r2, #1
	bne	.L3280
	mov	fp, #0
.L3248:
	ldrh	r0, [r8, #2916]
	cmp	fp, r0
	bcs	.L3252
	mla	r0, r0, r4, fp
	cmp	r0, r6
	bcc	.L3250
	ldr	r3, [sp, #12]
	cmp	r0, r3
	bcs	.L3250
	subs	r0, r0, r6
	mov	r1, #512
	add	r0, r7, r0, lsl #9
	bl	__memzero
.L3250:
	add	fp, fp, #1
	b	.L3248
.L3280:
	ldr	r1, [r10, #1764]
	mov	fp, #36
	ldr	r3, [sp, #4]
	mla	fp, fp, r5, r1
	cmp	r4, r3
	str	r2, [fp, #4]
	bne	.L3253
	ldr	r2, [r10, #1792]
	mov	r0, r6
	str	r2, [fp, #8]
	ldrh	r2, [r8, #2916]
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
	bne	.L3254
	str	r7, [fp, #8]
	b	.L3254
.L3253:
	ldr	r3, [sp, #8]
	cmp	r4, r3
	bne	.L3255
	ldr	r2, [r10, #1796]
	ldrh	r1, [r8, #2916]
	ldr	r3, [sp, #12]
	str	r2, [fp, #8]
	mul	r2, r1, r4
	rsb	r9, r2, r3
	cmp	r9, r1
	bne	.L3254
	b	.L3281
.L3255:
	ldrh	r2, [r8, #2916]
	muls	r2, r4, r2
.L3281:
	subs	r2, r2, r6
	add	r2, r7, r2, lsl #9
	str	r2, [fp, #8]
.L3254:
	ldrh	r2, [r8, #2922]
	ldr	r1, [r10, #1804]
	str	r4, [fp, #16]
	muls	r2, r5, r2
	adds	r5, r5, #1
	bic	r2, r2, #3
	add	r2, r2, r1
	str	r2, [fp, #12]
.L3252:
	ldr	r3, [sp]
	adds	r4, r4, #1
	subs	r3, r3, #1
	str	r3, [sp]
	beq	.L3256
	ldrh	r2, [r8, #2840]
	cmp	r5, r2, lsl #3
	bne	.L3247
.L3256:
	cmp	r5, #0
	beq	.L3247
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
.L3262:
	movs	r3, #36
	ldr	r2, [r10, #1764]
	mul	ip, r3, fp
	ldr	r3, [sp, #4]
	add	r2, r2, ip
	ldr	r1, [r2, #16]
	cmp	r1, r3
	bne	.L3258
	ldr	r1, [r2, #8]
	ldr	r2, [r10, #1792]
	cmp	r1, r2
	bne	.L3259
	ldr	r3, [sp, #32]
	mov	r0, r7
	str	ip, [sp, #44]
	add	r1, r1, r3
	ldr	r2, [sp, #36]
	b	.L3282
.L3258:
	ldr	r3, [sp, #8]
	cmp	r1, r3
	bne	.L3259
	ldr	r1, [r2, #8]
	ldr	r2, [r10, #1796]
	cmp	r1, r2
	bne	.L3259
	ldrh	r0, [r8, #2916]
	ldr	r2, [sp, #40]
	str	ip, [sp, #44]
	muls	r0, r3, r0
	subs	r0, r0, r6
	add	r0, r7, r0, lsl #9
.L3282:
	bl	ftl_memcpy
	ldr	ip, [sp, #44]
.L3259:
	ldr	r2, [r10, #1764]
	add	lr, r2, ip
	ldr	r0, [r2, ip]
	adds	r3, r0, #1
	itttt	eq
	streq	r0, [sp, #24]
	ldreq	r1, [r10, #1196]
	addeq	r1, r1, #1
	streq	r1, [r10, #1196]
	ldr	r2, [r2, ip]
	cmp	r2, #256
	bne	.L3261
	ldr	r0, [lr, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3261:
	add	fp, fp, #1
	cmp	fp, r5
	bne	.L3262
	movs	r5, #0
	b	.L3247
.L3283:
	ldr	r3, .L3284+4
	ldrh	r3, [r3, #1694]
	cbz	r3, .L3264
	ldr	r0, [sp]
	movs	r1, #1
	bl	ftl_do_gc
.L3264:
	ldr	r0, [sp, #24]
	b	.L3245
.L3265:
	mov	r0, #-1
.L3245:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3285:
	.align	2
.L3284:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	ftl_read, .-ftl_read
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
	ldr	r9, .L3356+12
	mov	r8, r1
	str	r3, [sp, #8]
	str	r2, [sp, #4]
	ldr	r3, [r9, #744]
	cmp	r3, #0
	bne	.L3328
	cmp	r0, #16
	bne	.L3289
	add	r0, r1, #256
	mov	r1, r2
	ldr	r2, [sp, #8]
	bl	FtlVendorPartWrite
	b	.L3288
.L3289:
	ldr	r7, .L3356
	ldr	r3, [sp, #4]
	adds	r6, r1, r3
	ldr	r3, [r7, #1896]
	cmp	r6, r3
	bhi	.L3329
	ldrh	r4, [r7, #2916]
	mov	r3, #2048
	ldr	r5, .L3356+4
	mov	r0, r1
	mov	r1, r4
	str	r3, [r5, #-40]
	bl	__aeabi_uidiv
	mov	r1, r4
	mov	fp, r0
	subs	r0, r6, #1
	bl	__aeabi_uidiv
	rsb	r6, fp, r0
	str	r0, [sp, #20]
	adds	r3, r6, #1
	str	r3, [sp, #12]
	ldr	r2, [sp, #12]
	ldr	r3, [r9, #1004]
	add	r3, r3, r2
	ldr	r2, [sp, #4]
	str	r3, [r9, #1004]
	ldr	r3, [r9, #1020]
	cmp	r2, r4, lsl #1
	add	r3, r3, r2
	str	r3, [r9, #1020]
	mov	r3, r2
	ldr	r2, [r7, #2960]
	ite	cs
	movcs	r3, #1
	movcc	r3, #0
	str	r3, [sp, #28]
	cmp	r2, #0
	beq	.L3291
	movs	r3, #36
	ldr	r7, [r7, #2964]
	muls	r3, r2, r3
	subs	r3, r3, #36
	add	r7, r7, r3
	ldr	r3, [r7, #16]
	cmp	fp, r3
	bne	.L3292
	ldr	r3, [r9, #1008]
	mov	r1, r4
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r9, #1008]
	ldr	r3, [r5, #-36]
	adds	r3, r3, #1
	str	r3, [r5, #-36]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	ldr	r0, [r7, #8]
	subs	r4, r4, r1
	add	r0, r0, r1, lsl #9
	cmp	r4, r3
	ldr	r1, [sp, #8]
	it	cs
	movcs	r4, r3
	lsl	r9, r4, #9
	mov	r2, r9
	bl	ftl_memcpy
	cbnz	r6, .L3293
	ldr	r3, [r5, #-36]
	cmp	r3, #2
	ble	.L3328
.L3293:
	ldr	r3, [sp, #4]
	add	r8, r8, r4
	add	fp, fp, #1
	str	r6, [sp, #12]
	subs	r3, r3, r4
	str	r3, [sp, #4]
	ldr	r3, [sp, #8]
	add	r3, r3, r9
	str	r3, [sp, #8]
.L3292:
	movs	r3, #0
	str	r3, [r5, #-36]
.L3291:
	mov	r0, fp
	ldr	r1, [sp, #20]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3294
	bl	FtlCacheWriteBack
.L3294:
	ldr	r4, .L3356
	mov	r6, fp
	ldr	r7, .L3356+8
	mov	r10, r4
	str	r7, [r5, #-60]
.L3295:
	ldr	r3, [sp, #12]
	ldr	r5, .L3356+12
	cmp	r3, #0
	beq	.L3355
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3296
	add	r2, r5, #836
	ldr	r9, .L3356+24
	cmp	r7, r2
	bne	.L3297
	ldrh	r7, [r5, #888]
	cbnz	r7, .L3298
	add	r0, r5, #884
	bl	allocate_new_data_superblock
	str	r7, [r9, #3068]
.L3298:
	ldr	r0, .L3356+8
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3068]
	cbnz	r3, .L3330
.L3300:
	ldr	r7, .L3356+8
	b	.L3299
.L3297:
	str	r3, [r9, #3068]
	ldrh	r3, [r5, #840]
	cmp	r3, #0
	bne	.L3300
	mov	r0, r7
	bl	allocate_new_data_superblock
	b	.L3299
.L3330:
	ldr	r7, .L3356+16
.L3299:
	ldrh	r3, [r7, #4]
	cbnz	r3, .L3301
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3301:
	ldr	r3, .L3356+4
	str	r7, [r3, #-60]
.L3296:
	ldr	r2, [r5, #1760]
	ldr	r3, [r4, #2960]
	ldrh	ip, [r7, #4]
	subs	r3, r2, r3
	ldr	r2, [sp, #12]
	cmp	ip, r3
	ldr	r9, .L3356+12
	it	cs
	movcs	ip, r3
	cmp	ip, r2
	mov	r3, ip
	it	cs
	movcs	r3, r2
	str	r3, [sp, #44]
	movs	r3, #0
	str	r3, [sp, #24]
.L3302:
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3303
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	beq	.L3303
	ldr	r3, [sp, #28]
	cbz	r3, .L3304
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3304
	ldr	r3, [sp, #24]
	cbz	r3, .L3304
	ldr	r3, [sp, #4]
	ldrh	r2, [r4, #2916]
	add	r3, r8, r3
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3303
.L3304:
	add	r1, sp, #48
	movs	r2, #0
	mov	r0, r6
	movs	r5, #36
	bl	log2phys
	mov	r0, r7
	bl	get_new_active_ppa
	ldr	r3, [r4, #2960]
	ldr	r1, [r4, #2964]
	ldrh	r2, [r4, #2922]
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
	ldrh	lr, [r4, #2920]
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
	beq	.L3305
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L3352
	ldr	r3, [sp, #4]
	add	r5, r8, r3
	ldrh	r3, [r4, #2916]
	smulbb	r3, r3, r6
	subs	r5, r5, r3
	movs	r3, #0
	str	r3, [sp, #32]
	uxth	r5, r5
	b	.L3308
.L3305:
	ldrh	r5, [r4, #2916]
	mov	r0, r8
	mov	r1, r5
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	subs	r5, r5, r1
	str	r1, [sp, #32]
	cmp	r5, r3
	it	cs
	movcs	r5, r3
.L3308:
	ldrh	r3, [r4, #2916]
	cmp	r5, r3
	bne	.L3309
	cmp	r6, fp
	mov	lr, #36
	ittet	ne
	mulne	r1, r6, r5
	ldrne	r3, [sp, #8]
	ldreq	r1, [sp, #8]
	rsbne	r1, r8, r1
	it	ne
	addne	r1, r3, r1, lsl #9
	ldr	r3, [sp, #28]
	cbz	r3, .L3311
	ldr	r2, [r10, #2960]
	ldr	r5, [r10, #2964]
	mla	r2, lr, r2, r5
	str	r1, [r2, #8]
	b	.L3312
.L3311:
	ldr	r0, [r10, #2964]
	ldr	r3, [r10, #2960]
	ldrh	r2, [r10, #2920]
	mla	r3, lr, r3, r0
	ldr	r0, [r3, #8]
	b	.L3353
.L3309:
	ldr	r2, [sp, #48]
	movs	r3, #36
	adds	r1, r2, #1
	beq	.L3313
	ldr	r1, [r4, #2964]
	add	r0, sp, #52
	str	r2, [sp, #56]
	ldr	r2, [r4, #2960]
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
	bne	.L3314
	ldr	r3, [r9, #1196]
	adds	r3, r3, #1
	str	r3, [r9, #1196]
	b	.L3316
.L3314:
	ldr	r3, [sp, #16]
	ldr	r3, [r3, #8]
	cmp	r3, r6
	beq	.L3316
	ldr	r3, [r9, #1196]
	mov	r2, r6
	ldr	r0, .L3356+20
	adds	r3, r3, #1
	str	r3, [r9, #1196]
	ldr	r3, [sp, #16]
	ldr	r1, [r3, #8]
	bl	printk
	b	.L3316
.L3357:
	.align	2
.L3356:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2+836
	.word	.LANCHOR2
	.word	.LANCHOR2+884
	.word	.LC144
	.word	.LANCHOR1
.L3313:
	ldr	r1, [r4, #2964]
	ldr	r2, [r4, #2960]
	mla	r2, r3, r2, r1
	movs	r1, #0
	ldr	r0, [r2, #8]
	ldrh	r2, [r4, #2920]
	bl	ftl_memset
.L3316:
	cmp	r6, fp
	mov	lr, #36
	lsl	r2, r5, #9
	bne	.L3317
	ldr	r1, [r4, #2960]
	ldr	r5, [r4, #2964]
	ldr	r3, [sp, #32]
	mla	r1, lr, r1, r5
	ldr	r0, [r1, #8]
	ldr	r1, [sp, #8]
	add	r0, r0, r3, lsl #9
	b	.L3353
.L3317:
	ldr	r0, [r4, #2964]
	ldr	r3, [r4, #2960]
	ldrh	r1, [r4, #2916]
	mla	r3, lr, r3, r0
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #8]
	add	r1, r3, r1, lsl #9
	b	.L3353
.L3352:
	ldr	r3, [sp, #28]
	cbz	r3, .L3318
	ldr	r3, [r4, #2960]
	ldr	r2, [r4, #2964]
	mla	r5, r5, r3, r2
	ldrh	r3, [r4, #2916]
	ldr	r2, [sp, #8]
	muls	r3, r6, r3
	rsb	r3, r8, r3
	add	r3, r2, r3, lsl #9
	str	r3, [r5, #8]
	b	.L3312
.L3318:
	ldr	r2, [r4, #2964]
	ldr	r3, [r4, #2960]
	ldrh	r1, [r4, #2916]
	mla	r3, r5, r3, r2
	ldrh	r2, [r4, #2920]
	muls	r1, r6, r1
	ldr	r0, [r3, #8]
	rsb	r1, r8, r1
	ldr	r3, [sp, #8]
	add	r1, r3, r1, lsl #9
.L3353:
	bl	ftl_memcpy
.L3312:
	ldr	r2, [sp, #40]
	movw	r3, #61589
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1, lsl #2]	@ movhi
	ldr	r2, [sp, #16]
	ldr	r3, [r9, #1032]
	str	r3, [r2, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	ldr	r2, [sp, #16]
	it	eq
	moveq	r3, #0
	str	r3, [r9, #1032]
	ldr	r3, [sp, #16]
	str	r6, [r3, #8]
	adds	r6, r6, #1
	ldr	r3, [sp, #48]
	str	r3, [r2, #12]
	ldrh	r3, [r7]
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, [r4, #2960]
	adds	r3, r3, #1
	str	r3, [r4, #2960]
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp, #24]
	b	.L3302
.L3303:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #24]
	subs	r3, r3, r2
	str	r3, [sp, #12]
	ldr	r3, .L3358
	ldr	r2, [r10, #2960]
	ldr	r3, [r3, #1760]
	cmp	r2, r3
	bcs	.L3322
	ldr	r3, [sp, #28]
	cbnz	r3, .L3322
	ldrh	r3, [r7, #4]
	cbz	r3, .L3322
.L3324:
	movs	r3, #0
	str	r3, [sp, #28]
	b	.L3295
.L3322:
	bl	FtlCacheWriteBack
	movs	r3, #0
	str	r3, [r10, #2960]
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bhi	.L3295
	b	.L3324
.L3355:
	mov	r0, r3
	ldr	r3, [sp, #20]
	rsb	r1, fp, r3
	bl	ftl_do_gc
	ldrh	r3, [r5, #832]
	cmp	r3, #31
	bhi	.L3328
	movs	r4, #16
	movs	r3, #128
	strh	r3, [r5, #1638]	@ movhi
	strh	r3, [r5, #1636]	@ movhi
.L3327:
	movs	r0, #0
	movs	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r5, #744]
	cbnz	r3, .L3328
	subs	r4, r4, #1
	bne	.L3327
	b	.L3328
.L3329:
	mov	r0, #-1
	b	.L3288
.L3328:
	movs	r0, #0
.L3288:
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3359:
	.align	2
.L3358:
	.word	.LANCHOR2
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
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	bl	ftl_vendor_write
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
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
	.global	FtlWrite
	.thumb
	.thumb_func
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_write
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	1
	.global	FlashReadFacBbtData
	.thumb
	.thumb_func
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3379
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
	ldr	r2, .L3379+4
	ldr	r1, [r2, #3988]
	mov	r9, r2
	uxth	r3, r3
	subs	r6, r3, #1
	str	r1, [sp, #12]
	mul	r10, r3, r7
	ldr	r1, [r2, #4020]
	uxth	r6, r6
	sub	r4, r3, #16
	str	r1, [sp, #16]
.L3366:
	cmp	r6, r4
	ble	.L3378
	movs	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	lsls	r3, r3, #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L3367
	ldr	r3, [r9, #4020]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L3367
	cbz	r5, .L3373
	cbz	r7, .L3374
.L3371:
	ldr	r1, [r9, #3988]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	movs	r2, #4
	ldr	r0, .L3379+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	movs	r0, #0
	b	.L3368
.L3374:
	ldr	r6, .L3379+4
	mov	lr, #1
.L3369:
	ldr	r2, [r9, #4008]
	uxth	r3, r7
	adds	r7, r7, #1
	cmp	r3, r2
	bcs	.L3371
	ldr	r2, [r6, #3988]
	lsrs	r1, r3, #5
	and	r4, r3, #31
	lsl	r3, lr, r4
	ldr	r0, [r2, r1, lsl #2]
	orrs	r3, r3, r0
	str	r3, [r2, r1, lsl #2]
	b	.L3369
.L3367:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L3366
.L3378:
	mov	r0, #-1
	b	.L3368
.L3373:
	mov	r0, r5
.L3368:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3380:
	.align	2
.L3379:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC145
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
	ldr	r3, .L3391
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #1788]
	ldr	r6, .L3391+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #4016]
	smulbb	r4, r4, r3
	uxth	r4, r4
	adds	r2, r4, #7
	asrs	r2, r2, #3
	bl	FlashReadFacBbtData
	adds	r0, r0, #1
	bne	.L3382
.L3386:
	movs	r3, #0
	b	.L3383
.L3382:
	movs	r1, #0
	lsr	lr, r4, #4
	mov	ip, #1
	subs	r4, r4, #1
	mov	r3, r1
.L3384:
	uxth	r2, r1
	cmp	r2, r4
	bge	.L3383
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
	bcc	.L3384
	b	.L3386
.L3383:
	movw	r2, #65535
	movs	r0, #0
	strh	r2, [r5, r3, lsl #1]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L3392:
	.align	2
.L3391:
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
	ldr	r4, .L3415
	ldr	r5, [r4, #744]
	cmp	r5, #0
	bne	.L3394
	bl	FtlBbtMemInit
	ldr	r7, .L3415+4
	bl	FtlLoadFactoryBbt
	mov	fp, r4
	sub	r9, r7, #18
	subw	r8, r7, #3000
.L3395:
	ldrh	r3, [r8, #2862]
	ldr	r6, .L3415+8
	cmp	r5, r3
	bcs	.L3413
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r4, #1776]
	ldr	r10, [r4, #1800]
	cmp	r3, r2
	str	r0, [r4, #4060]
	str	r10, [r4, #4064]
	beq	.L3396
	ldrh	ip, [r8, #2906]
	movs	r1, #1
	ldr	r0, .L3415+12
	mov	r2, r1
	mla	ip, ip, r5, r3
	lsl	r3, ip, #10
	str	ip, [sp, #4]
	str	r3, [fp, #4056]
	bl	FlashReadPages
	ldrh	r2, [r8, #2906]
	ldr	r0, [r7]
	adds	r2, r2, #7
	ldr	r1, [fp, #4060]
	asrs	r2, r2, #3
	bl	ftl_memcpy
	ldr	ip, [sp, #4]
	b	.L3397
.L3396:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [fp, #4060]
	ldr	r1, [r7]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r8, #2906]
.L3399:
	subs	r6, r6, #1
	uxth	r6, r6
.L3398:
	ldr	r3, .L3415+8
	ldrh	r0, [r3, #2906]
	smlabb	r0, r0, r5, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L3399
	ldr	r0, [r4, #1800]
	movs	r1, #16
	strh	r6, [r9]	@ movhi
	bl	__memzero
	movw	r3, #61664
	strh	r3, [r10]	@ movhi
	movs	r3, #0
	str	r3, [r10, #4]
	ldrh	r3, [r9]
	ldrh	ip, [r8, #2906]
	ldrh	r2, [r4, #1864]
	strh	r3, [r10, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r7]
	lsls	r2, r2, #2
	ldr	r0, [r4, #4060]
	mla	ip, ip, r5, r3
	lsl	r3, ip, #10
	str	ip, [sp, #4]
	str	r3, [r4, #4056]
	bl	ftl_memcpy
	movs	r1, #1
	ldr	r0, .L3415+12
	mov	r2, r1
	bl	FlashEraseBlocks
	movs	r1, #1
	ldr	r0, .L3415+12
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #4052]
	ldr	ip, [sp, #4]
	adds	r3, r3, #1
	bne	.L3397
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L3398
.L3397:
	uxth	r0, ip
	adds	r5, r5, #1
	bl	FtlBbmMapBadBlock
	adds	r7, r7, #4
	b	.L3395
.L3413:
	movs	r5, #0
.L3402:
	ldrh	r3, [r6, #2924]
	uxth	r0, r5
	ldr	r7, .L3415+8
	adds	r5, r5, #1
	cmp	r3, r0
	bls	.L3414
	bl	FtlBbmMapBadBlock
	b	.L3402
.L3414:
	ldrh	r5, [r7, #2984]
	movw	r8, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L3404:
	ldrh	r3, [r6, #2984]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L3408
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L3405
	mov	r0, r5
	bl	FlashTestBlk
	cbz	r0, .L3406
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L3405
.L3406:
	ldrh	r3, [r7, #2972]
	cmp	r3, r8
	bne	.L3407
	strh	r5, [r7, #2972]	@ movhi
	b	.L3405
.L3407:
	ldr	r3, .L3415+8
	strh	r5, [r3, #2976]	@ movhi
	b	.L3408
.L3405:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L3404
.L3408:
	ldr	r0, [r4, #748]
	movs	r5, #0
	ldrh	r3, [r6, #2972]
	movs	r2, #2
	str	r5, [r6, #2980]
	movs	r1, #1
	strh	r5, [r6, #2974]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r6, #2976]
	lsls	r3, r3, #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r6, #2972]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r6, #2976]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r6, #2980]
	ldrh	r2, [r6, #2976]
	adds	r3, r3, #1
	str	r3, [r6, #2980]
	ldrh	r3, [r6, #2972]
	strh	r5, [r6, #2974]	@ movhi
	strh	r2, [r6, #2972]	@ movhi
	strh	r3, [r6, #2976]	@ movhi
	bl	FtlBbmTblFlush
.L3394:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3416:
	.align	2
.L3415:
	.word	.LANCHOR2
	.word	.LANCHOR0+3000
	.word	.LANCHOR0
	.word	.LANCHOR2+4052
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
	ldr	r4, .L3451
	ldr	r6, [r4, #744]
	cmp	r6, #0
	bne	.L3419
	ldr	r5, .L3451+4
	mov	r1, r6
	ldr	r0, [r4, #1852]
	ldrh	r2, [r5, #2948]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #2948]
	mov	r1, r6
	ldr	r0, [r4, #1848]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r5, #2844]
	str	r6, [r4, #1028]
	str	r6, [r4, #1032]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3420
	bl	FtlMakeBbt
.L3420:
	ldr	r0, .L3451+8
	movs	r2, #0
.L3421:
	ldrh	r1, [r5, #2916]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	bge	.L3448
	ldr	r6, [r4, #1792]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r6, r3, lsl #2]
	ldr	r1, [r4, #1796]
	str	r0, [r1, r3, lsl #2]
	b	.L3421
.L3448:
	ldr	r3, .L3451+4
	movs	r6, #0
	ldrh	r7, [r3, #2848]
.L3423:
	ldrh	r3, [r5, #2850]
	ldr	r8, .L3451+4
	cmp	r3, r7
	bls	.L3449
	mov	r0, r7
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r7, r7, #1
	uxth	r7, r7
	add	r0, r0, r6
	uxth	r6, r0
	b	.L3423
.L3449:
	ldrh	r1, [r8, #2840]
	subs	r3, r6, #3
	cmp	r3, r1, lsl #1
	bge	.L3425
.L3429:
	movs	r6, #0
	mov	r7, r6
	b	.L3426
.L3425:
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, [r8, #2944]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r8, #2844]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r6, [r8, #2848]
.L3427:
	ldrh	r3, [r5, #2850]
	cmp	r3, r6
	bls	.L3429
	mov	r0, r6
	adds	r6, r6, #1
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	uxth	r6, r6
	b	.L3427
.L3426:
	ldrh	r3, [r5, #2848]
	uxth	r0, r6
	ldr	r10, .L3451+4
	adds	r6, r6, #1
	cmp	r3, r0
	bls	.L3450
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r0, r7
	uxth	r7, r0
	b	.L3426
.L3450:
	ldrh	r3, [r10, #2850]
	ldrh	r6, [r10, #2840]
	ldr	fp, [r10, #2852]
	str	r3, [r4, #1752]
	mov	r1, r6
	mov	r0, fp
	bl	__aeabi_uidiv
	ubfx	r8, r0, #5, #16
	mov	r9, r0
	add	r3, r8, #36
	strh	r3, [r4, #1056]	@ movhi
	movs	r3, #24
	str	r0, [r10, #2968]
	muls	r3, r6, r3
	cmp	r7, r3
	ble	.L3431
	rsb	r0, r7, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, .L3451
	str	r0, [r10, #2968]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r3, #1056]	@ movhi
.L3431:
	ldr	r3, [r5, #2832]
	cmp	r3, #1
	bne	.L3432
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r10, [r4, #1056]
	uxtah	r0, r10, r0
	add	r10, r10, r0, asr #2
	strh	r10, [r4, #1056]	@ movhi
.L3432:
	ldrb	r3, [r5, #76]	@ zero_extendqisi2
	cbz	r3, .L3433
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r10, [r4, #1056]
	uxtah	r0, r10, r0
	add	r10, r10, r0, asr #2
	strh	r10, [r4, #1056]	@ movhi
.L3433:
	ldrh	r3, [r5, #2902]
	cbz	r3, .L3435
	ldrh	r2, [r4, #1056]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #1056]	@ movhi
	mul	r2, r6, r3
	cmp	r2, r7
	itttt	gt
	addgt	r3, r3, #32
	ldrgt	r2, .L3451+4
	addgt	r3, r3, r8
	strgt	r9, [r2, #2968]
	itt	gt
	ldrgt	r2, .L3451
	strhgt	r3, [r2, #1056]	@ movhi
.L3435:
	ldrh	r2, [r4, #1056]
	ldr	r3, [r5, #2968]
	ldr	r7, .L3451
	subs	r3, r3, r2
	muls	r6, r3, r6
	ldrh	r3, [r5, #2910]
	str	r6, [r4, #4088]
	muls	r6, r3, r6
	ldrh	r3, [r5, #2916]
	str	r6, [r5, #2968]
	muls	r6, r3, r6
	str	r6, [r5, #1896]
	bl	FtlBbmTblFlush
	ldrh	r2, [r5, #2850]
	movs	r1, #0
	ldr	r0, [r4, #816]
	movw	r6, #65535
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2848]
	ldr	r0, [r4, #992]
	movs	r1, #255
	movs	r3, #0
	strh	r6, [r4, #1072]	@ movhi
	lsrs	r2, r2, #3
	str	r3, [r4, #988]
	strh	r3, [r4, #1074]	@ movhi
	strb	r3, [r4, #1078]
	strb	r3, [r4, #1080]
	strh	r3, [r4, #838]	@ movhi
	strb	r3, [r4, #842]
	strh	r3, [r4, #836]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #844]
	bl	ftl_memset
.L3437:
	ldr	r5, .L3451
	add	r0, r5, #836
	bl	make_superblock
	ldrb	r3, [r4, #843]	@ zero_extendqisi2
	ldrh	r2, [r4, #836]
	cbnz	r3, .L3438
	ldr	r3, [r7, #816]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r7, #836]
	adds	r3, r3, #1
	strh	r3, [r7, #836]	@ movhi
	b	.L3437
.L3438:
	ldr	r3, [r5, #1028]
	movw	r7, #65535
	ldrh	r1, [r5, #840]
	str	r3, [r5, #848]
	adds	r3, r3, #1
	str	r3, [r5, #1028]
	ldr	r3, [r5, #816]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r5, #886]	@ movhi
	strb	r3, [r5, #890]
	ldrh	r3, [r5, #836]
	adds	r3, r3, #1
	strh	r3, [r5, #884]	@ movhi
	movs	r3, #1
	strb	r3, [r5, #892]
.L3439:
	ldr	r6, .L3451
	add	r0, r6, #884
	bl	make_superblock
	ldrb	r3, [r4, #891]	@ zero_extendqisi2
	ldrh	r2, [r4, #884]
	cbnz	r3, .L3440
	ldr	r3, [r5, #816]
	strh	r7, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r5, #884]
	adds	r3, r3, #1
	strh	r3, [r5, #884]	@ movhi
	b	.L3439
.L3440:
	ldr	r3, [r6, #1028]
	movw	r4, #65535
	ldrh	r1, [r6, #888]
	str	r3, [r6, #896]
	adds	r3, r3, #1
	str	r3, [r6, #1028]
	ldr	r3, [r6, #816]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r4, [r6, #932]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r3, [r6, #1062]	@ movhi
	ldr	r3, [r6, #4088]
	strh	r4, [r6, #1064]	@ movhi
	strh	r3, [r6, #1066]	@ movhi
	ldr	r3, [r6, #1028]
	str	r3, [r6, #1068]
	adds	r3, r3, #1
	str	r3, [r6, #1028]
	strh	r0, [r6, #1060]	@ movhi
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L3419
	ldr	r3, .L3451+12
	movs	r2, #1
	str	r2, [r3, #504]
.L3419:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3452:
	.align	2
.L3451:
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
	ldr	r4, .L3481
	ldrb	r3, [r4, #76]	@ zero_extendqisi2
	cbz	r3, .L3454
.L3456:
	movs	r0, #0
	b	.L3455
.L3454:
	ldr	r5, .L3481+4
	ldr	r0, [r5, #3984]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3456
	bl	FlashLoadFactorBbt
	cbz	r0, .L3457
	bl	FlashMakeFactorBbt
.L3457:
	ldr	r0, [r5, #4016]
	bl	FlashReadIdbDataRaw
	cbz	r0, .L3458
	movs	r1, #0
	movs	r2, #16
	mov	r0, sp
	movs	r6, #1
	bl	FlashReadFacBbtData
	movs	r3, #0
	ldr	r1, [sp]
	mov	r2, r3
.L3459:
	lsl	r5, r6, r2
	adds	r0, r3, #1
	tst	r5, r1
	add	r2, r2, #1
	it	ne
	movne	r3, r0
	cmp	r2, #16
	bne	.L3459
	cmp	r3, #6
	bls	.L3477
	movs	r2, #0
	movs	r6, #1
.L3462:
	lsl	r5, r6, r2
	adds	r0, r3, #1
	tst	r5, r1
	add	r2, r2, #1
	it	ne
	movne	r3, r0
	cmp	r2, #24
	bne	.L3462
	cmp	r3, #17
	bhi	.L3463
.L3477:
	strb	r2, [r4, #1]
	b	.L3461
.L3463:
	movs	r3, #36
	strb	r3, [r4, #1]
.L3461:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strh	r3, [r4, #1890]	@ movhi
.L3458:
	ldr	r1, .L3481+8
	ldr	r0, .L3481+12
	bl	printk
	ldr	r0, .L3481+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #2844]
	bl	FtlFreeSysBlkQueueInit
	movs	r4, #1
.L3464:
	bl	FtlLoadBbt
	cbz	r0, .L3465
.L3479:
	bl	FtlLowFormat
	cmp	r4, #3
	bhi	.L3480
	adds	r4, r4, #1
	b	.L3464
.L3480:
	mov	r0, #-1
	b	.L3455
.L3465:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3479
	ldr	r3, .L3481+20
	movs	r2, #1
	str	r2, [r3, #504]
.L3455:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L3482:
	.align	2
.L3481:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC78
	.word	.LC77
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
	ldr	r3, .L3485
	ldr	r2, [r3, #2768]
	ldr	r3, .L3485+4
	cmp	r2, r3
	bne	.L3483
	b	flash_enter_slc_mode
.L3483:
	bx	lr
.L3486:
	.align	2
.L3485:
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
	ldr	r3, .L3489
	ldr	r2, [r3, #2768]
	ldr	r3, .L3489+4
	cmp	r2, r3
	bne	.L3487
	b	flash_exit_slc_mode
.L3487:
	bx	lr
.L3490:
	.align	2
.L3489:
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
	ldr	r4, .L3499
	mov	r8, r1
	.pad #20
	sub	sp, sp, #20
	mov	r1, r6
	mov	fp, r2
	ldr	r0, .L3499+4
	ldr	r3, [r4, #1788]
	mov	r2, r8
	movs	r7, #0
	ldrb	r9, [r3, #9]	@ zero_extendqisi2
	ldrh	r5, [r3, #10]
	bl	printk
	mov	r0, r6
	smulbb	r5, r5, r9
	uxth	r5, r5
	mov	r1, r5
	bl	__aeabi_uidivmod
	mul	ip, r9, r1
	mov	r10, r1
	subs	r3, r6, r1
	str	r3, [sp]
	ubfx	ip, ip, #2, #2
.L3492:
	cmp	r7, r8
	bcs	.L3498
	add	r2, r7, r10
	ldrb	r1, [r4, #76]	@ zero_extendqisi2
	ubfx	r2, r2, #2, #16
	rsb	r5, ip, #4
	add	r3, r4, r2, lsl #1
	uxth	r5, r5
	ldrh	r3, [r3, #80]
	cbz	r1, .L3493
	ldr	r1, .L3499
	ldr	r0, .L3499+8
	ldr	r1, [r1, #2768]
	cmp	r1, r0
	it	eq
	moveq	r3, r2
.L3493:
	ldr	r2, [sp]
	add	r7, r7, r5
	add	r2, ip, r2
	ldrb	ip, [r4, #1892]	@ zero_extendqisi2
	uxth	r7, r7
	mla	r3, r9, r3, r2
	ldr	r2, [r4, #1788]
	str	ip, [sp, #4]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L3499+12
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
	b	.L3492
.L3498:
	mov	r1, r6
	mov	r2, r8
	movs	r3, #0
	ldr	r0, .L3499+16
	bl	printk
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3500:
	.align	2
.L3499:
	.word	.LANCHOR0
	.word	.LC146
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC147
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
	mov	r5, r2
	ldr	r7, .L3542
	.pad #124
	sub	sp, sp, #124
	mov	r4, r0
	mov	r0, #256000
	mov	r6, r1
	ldr	r2, [r7, #1788]
	ldrb	r10, [r2, #9]	@ zero_extendqisi2
	ldrh	r8, [r2, #10]
	bl	ftl_malloc
	mov	r9, r0
	cmp	r0, #0
	beq	.L3524
	addw	r4, r4, #511
	lsrs	r4, r4, #9
	cmp	r4, #8
	bls	.L3523
	cmp	r4, #500
	bhi	.L3524
	b	.L3503
.L3523:
	movs	r4, #8
.L3503:
	smulbb	r3, r10, r8
	mov	r0, r4
	uxth	r3, r3
	str	r3, [sp, #8]
	mov	r1, r3
	bl	__aeabi_uidiv
	movs	r3, #0
	movw	r2, #63999
	str	r0, [sp, #28]
	add	r0, r6, #254976
	add	r0, r0, #1020
.L3507:
	ldr	r1, [r0]
	cbnz	r1, .L3504
	ldr	r1, [r6, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #4096
	add	r2, r2, #-1
	it	hi
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0], #-4
	bne	.L3507
	b	.L3506
.L3504:
	ldr	r0, .L3542+4
	bl	printk
.L3506:
	mov	r1, r5
	movs	r3, #5
	ldr	r0, .L3542+8
	movs	r2, #4
	bl	rknand_print_hex
	ldrb	r2, [r7, #1]	@ zero_extendqisi2
	ldr	r1, [r6, #512]
	subs	r5, r5, #4
	ldr	r0, .L3542+12
	bl	printk
	ldr	r2, .L3542+16
	ldrh	r3, [r7, #1890]
	mov	r1, r4
	ldr	r0, .L3542+20
	ldr	r2, [r2, #4008]
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r7, .L3542
	ldr	r2, [r6, #512]
	cmp	r2, r3
	it	hi
	strhi	r3, [r6, #512]
	lsls	r3, r4, #7
	str	r3, [sp, #32]
	movs	r3, #0
	str	r3, [sp, #16]
	str	r3, [sp, #12]
.L3521:
	ldr	r2, [r5, #4]
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L3509
	ldr	r3, .L3542+16
	ldr	r3, [r3, #4008]
	cmp	r2, r3
	bcc	.L3509
	ldr	r3, [sp, #28]
	cmp	r3, #1
	bls	.L3510
	ldr	r3, [sp, #12]
	cbz	r3, .L3510
	ldr	r3, [r5]
	adds	r3, r3, #1
	cmp	r2, r3
	beq	.L3509
.L3510:
	mov	r0, r9
	mov	r1, #512
	bl	__memzero
	ldr	r3, [r5, #4]
	ldr	r2, [sp, #8]
	ldr	r0, .L3542+24
	mul	r8, r3, r2
	ldr	r3, .L3542
	ldr	r3, [r3, #1788]
	mov	r1, r8
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	ldrh	r10, [r3, #10]
	str	r2, [sp, #20]
	mov	r2, r4
	ldrh	r3, [sp, #20]
	smulbb	r10, r10, r3
	bl	printk
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r1, [sp, #20]
	mov	r0, r8
	bl	__aeabi_uidiv
	uxth	r10, r10
	mov	r1, r0
	movs	r0, #0
	mov	r2, r0
	bl	FlashEraseBlock
	cmp	r4, r10
	bls	.L3525
	movs	r0, #0
	add	r1, r8, r10
	mov	fp, #2
	mov	r2, r0
	bl	FlashEraseBlock
	b	.L3511
.L3525:
	mov	fp, #1
.L3511:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r7, #1788]
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
	str	r6, [sp, #24]
	mov	ip, r1
	rsb	r3, r1, r8
	str	r3, [sp, #36]
.L3512:
	ldr	r3, [sp, #44]
	cmp	r10, r3
	bcs	.L3541
	add	r2, r10, ip
	ubfx	r2, r2, #2, #16
	cbz	r2, .L3513
	adds	r3, r2, #1
	add	r1, r7, r3, lsl #1
	ldrh	fp, [r1, #80]
	ldrb	r1, [r7, #76]	@ zero_extendqisi2
	cbz	r1, .L3514
	ldr	r1, [r7, #2768]
	ldr	r0, .L3542+28
	cmp	r1, r0
	it	eq
	moveq	fp, r3
.L3514:
	add	fp, fp, #1073741824
	add	fp, fp, #-1
	lsl	r3, fp, #2
	str	r3, [sp, #56]
.L3513:
	movw	r3, #61424
	str	r3, [sp, #60]
	add	r3, r7, r2, lsl #1
	ldrh	fp, [r3, #80]
	ldrb	r3, [r7, #76]	@ zero_extendqisi2
	cbz	r3, .L3515
	ldr	r3, [r7, #2768]
	ldr	r1, .L3542+28
	cmp	r3, r1
	it	eq
	moveq	fp, r2
.L3515:
	ldr	r3, [sp, #20]
	add	r10, r10, #4
	ldr	r2, [sp, #36]
	str	ip, [sp, #52]
	uxth	r10, r10
	mla	r2, r3, fp, r2
	ldr	r3, .L3542
	ldrb	r3, [r3, #1892]	@ zero_extendqisi2
	str	r2, [sp, #48]
	str	r3, [sp, #40]
	ldr	r3, .L3542+16
	ldrb	r0, [r3, #4014]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, .L3542
	ldr	r2, [sp, #48]
	ldr	r3, [r3, #1788]
	mov	r0, r2
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	add	r3, sp, #56
	ldr	r2, [sp, #24]
	mov	r1, r0
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
	b	.L3512
.L3543:
	.align	2
.L3542:
	.word	.LANCHOR0
	.word	.LC148
	.word	.LC149
	.word	.LC150
	.word	.LANCHOR2
	.word	.LC151
	.word	.LC152
	.word	1446522928
.L3541:
	mov	r1, r8
	mov	r2, r4
	movs	r3, #0
	ldr	r0, .L3544
	bl	printk
	ldr	r0, [r5, #4]
	ldr	r3, [sp, #8]
	mov	r1, r4
	mov	r2, r9
	mov	r8, #0
	muls	r0, r3, r0
	bl	IdBlockReadData
	mov	r0, r9
	mov	r1, r6
.L3520:
	mov	fp, r0
	mov	r10, r1
	ldr	r2, [fp]
	adds	r0, r0, #4
	ldr	r3, [r10]
	adds	r1, r1, #4
	cmp	r2, r3
	beq	.L3517
	mov	r0, r9
	mov	r1, #512
	bl	__memzero
	ldr	r3, [fp]
	ldr	r1, [sp, #12]
	ldr	r0, .L3544+4
	str	r3, [sp]
	ldr	r3, [r10]
	bic	r10, r8, #255
	lsl	r10, r10, #2
	str	r3, [sp, #4]
	mov	r3, r8
	ldr	r2, [r5, #4]
	bl	printk
	ldr	r0, .L3544+8
	add	r1, r6, r10
	movs	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	add	r1, r9, r10
	movs	r2, #4
	mov	r3, #256
	ldr	r0, .L3544+12
	bl	rknand_print_hex
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [sp, #8]
	ldr	r1, [r5, #4]
	movs	r0, #0
	mov	r2, r0
	muls	r1, r3, r1
	bl	FlashEraseBlock
	ldr	r3, [sp, #28]
	cmp	r3, #1
	bls	.L3518
	ldr	r1, [r5, #4]
	movs	r0, #0
	ldr	r3, [sp, #8]
	mov	r2, r0
	mla	r1, r1, r3, r3
	bl	FlashEraseBlock
.L3518:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L3544+16
	ldr	r1, [r5, #4]
	bl	printk
	ldr	r3, [sp, #32]
	cmp	r8, r3
	bcc	.L3509
	b	.L3519
.L3517:
	ldr	r3, [sp, #32]
	add	r8, r8, #1
	cmp	r8, r3
	bne	.L3520
.L3519:
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	str	r3, [sp, #16]
.L3509:
	ldr	r3, [sp, #12]
	adds	r5, r5, #4
	adds	r3, r3, #1
	str	r3, [sp, #12]
	cmp	r3, #5
	bne	.L3521
	mov	r0, r9
	bl	ftl_free
	ldr	r3, [sp, #16]
	clz	r0, r3
	lsrs	r0, r0, #5
	negs	r0, r0
	b	.L3502
.L3524:
	mov	r0, #-1
.L3502:
	add	sp, sp, #124
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3545:
	.align	2
.L3544:
	.word	.LC153
	.word	.LC154
	.word	.LC155
	.word	.LC156
	.word	.LC157
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
	ldr	r4, .L3550
	movs	r2, #0
.L3547:
	cmp	r0, r1
	beq	.L3549
	ldrb	r3, [r0], #1	@ zero_extendqisi2
	eor	r3, r3, r2, lsr #24
	add	r3, r4, r3, lsl #2
	ldr	r3, [r3, #3072]
	eor	r2, r3, r2, lsl #8
	b	.L3547
.L3549:
	mov	r0, r2
	pop	{r4, pc}
.L3551:
	.align	2
.L3550:
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
	ldr	r3, .L3660
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	cmp	r1, r3
	.pad #532
	sub	sp, sp, #532
	mov	r4, r1
	mov	r5, r2
	beq	.L3554
	bhi	.L3555
	subw	r3, r3, #3461
	cmp	r1, r3
	beq	.L3556
	bhi	.L3557
	subs	r3, r3, #125
	cmp	r1, r3
	beq	.L3558
	bhi	.L3559
	subs	r3, r3, #237
	cmp	r1, r3
	bne	.L3615
	bl	rknand_dev_flush
	b	.L3648
.L3559:
	ldr	r3, .L3660+4
	cmp	r1, r3
	beq	.L3561
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3562
	b	.L3615
.L3557:
	ldr	r3, .L3660+8
	cmp	r1, r3
	beq	.L3563
	bhi	.L3564
	mvn	r1, #1960
	add	r3, r3, r1
	cmp	r4, r3
	beq	.L3565
	b	.L3615
.L3564:
	ldr	r3, .L3660+12
	cmp	r1, r3
	beq	.L3563
	adds	r3, r3, #10
	cmp	r1, r3
	beq	.L3563
	b	.L3615
.L3555:
	ldr	r3, .L3660+16
	cmp	r1, r3
	beq	.L3566
	bhi	.L3567
	subs	r3, r3, #78
	cmp	r1, r3
	beq	.L3568
	bcc	.L3569
	adds	r3, r3, #21
	cmp	r1, r3
	beq	.L3570
	adds	r3, r3, #56
	cmp	r1, r3
	beq	.L3571
	b	.L3615
.L3567:
	ldr	r3, .L3660+20
	cmp	r1, r3
	mov	r6, r3
	beq	.L3572
	bhi	.L3573
	mvn	r2, #956
	add	r3, r3, r2
	cmp	r1, r3
	beq	.L3574
	add	r3, r3, #956
	cmp	r1, r3
	beq	.L3575
	b	.L3615
.L3573:
	ldr	r3, .L3660+24
	cmp	r1, r3
	beq	.L3575
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3572
	b	.L3615
.L3569:
	ldr	r0, .L3660+28
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r4, r0
	cbnz	r0, .L3576
.L3581:
	mvn	r0, #11
	b	.L3553
.L3576:
	mov	r1, r5
	mov	r2, #512
	bl	rk_copy_from_user
	cbnz	r0, .L3654
	ldr	r0, .L3660+32
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r4, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L3652
	bl	rknand_device_lock
	ldr	r1, [sp, #4]
	mov	r2, r4
	ldr	r0, [r4]
	bl	IdBlockReadData
	bl	rknand_device_unlock
	ldr	r2, [sp, #4]
	mov	r0, r5
	mov	r1, r4
	lsls	r2, r2, #9
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3647
	ldr	r0, .L3660+36
.L3650:
	bl	printk
.L3652:
	mov	r0, r4
.L3653:
	bl	ftl_free
.L3651:
	mvn	r0, #13
	b	.L3553
.L3568:
	ldr	r0, .L3660+40
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r4, r0
	cmp	r0, #0
	beq	.L3581
	mov	r1, r5
	mov	r2, #4096
	bl	rk_copy_from_user
	cbz	r0, .L3582
.L3654:
	ldr	r0, .L3660+44
	b	.L3650
.L3582:
	ldr	r5, .L3660+48
	ldr	r0, .L3660+52
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r5, #-32]
	mov	r6, r5
	cbnz	r3, .L3583
	mov	r0, #260096
	bl	ftl_malloc
	str	r0, [r5, #-32]
	cmp	r0, #0
	beq	.L3652
	mov	r1, #260096
	bl	__memzero
.L3583:
	ldr	r2, [r4, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L3652
	ldr	r3, [r4]
	cmp	r3, #251904
	bhi	.L3652
	ldr	r0, [r6, #-32]
	add	r1, r4, #8
	add	r0, r0, r3
	bl	memcpy
.L3647:
	mov	r0, r4
	bl	ftl_free
.L3648:
	movs	r4, #0
	b	.L3580
.L3571:
	ldr	r0, .L3660+56
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3581
	mov	r1, r5
	movs	r2, #28
	bl	rk_copy_from_user
	cbz	r0, .L3586
	ldr	r0, .L3660+44
	bl	printk
	b	.L3587
.L3586:
	ldmia	r6, {r1, r2}
	ldr	r0, .L3660+60
	bl	printk
	ldr	r1, [r6]
	cmp	r1, #256000
	bhi	.L3587
	ldr	r5, .L3660+48
	ldr	r0, [r5, #-32]
	cbz	r0, .L3587
	bl	CRC_32
	ldr	r3, [r6, #4]
	cmp	r3, r0
	beq	.L3589
	mov	r0, r6
	bl	ftl_free
	b	.L3616
.L3589:
	bl	rknand_device_lock
	ldr	r1, [r5, #-32]
	add	r2, r6, #8
	ldr	r0, [r6]
	bl	write_idblock
	movs	r4, #0
	bl	rknand_device_unlock
	ldr	r0, [r5, #-32]
	bl	ftl_free
	str	r4, [r5, #-32]
	mov	r0, r6
	b	.L3593
.L3587:
	mov	r0, r6
	b	.L3653
.L3570:
	ldr	r0, .L3660+64
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3581
	bl	ftl_read_flash_info
	mov	r0, r5
	mov	r1, r6
	movs	r2, #11
	b	.L3658
.L3554:
	ldr	r0, .L3660+68
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3581
	bl	rknand_device_lock
	movs	r1, #0
	movs	r2, #64
	mov	r0, r6
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	ldr	r0, .L3660+72
	mov	r1, r6
	movs	r2, #4
	movs	r3, #8
	bl	rknand_print_hex
	mov	r0, r5
	mov	r1, r6
	movs	r2, #64
	b	.L3658
.L3566:
	ldr	r0, .L3660+76
	bl	printk
	ldr	r3, .L3660+48
	add	r1, sp, #4
	movs	r2, #4
	mov	r0, r5
	ldr	r3, [r3, #-28]
	ldr	r3, [r3, #20]
	str	r3, [sp, #4]
.L3642:
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3651
	b	.L3648
.L3574:
	ldr	r0, .L3660+80
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	mov	r6, r0
	cmp	r0, #0
	beq	.L3581
	bl	rknand_device_lock
	movs	r1, #2
	mov	r2, r6
	mov	r0, #520
	bl	FtlVendorPartRead
	bl	rknand_device_unlock
	mov	r2, #1024
	mov	r0, r5
	mov	r1, r6
.L3658:
	bl	rk_copy_to_user
	mov	r4, r0
	mov	r0, r6
	cmp	r4, #0
	bne	.L3653
.L3593:
	bl	ftl_free
	b	.L3580
.L3558:
	ldr	r0, .L3660+84
	bl	printk
	add	r0, sp, #8
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	mov	r4, r0
	cbz	r0, .L3594
.L3599:
	ldr	r0, .L3660+44
	bl	printk
	b	.L3651
.L3661:
	.align	2
.L3660:
	.word	1074033155
	.word	1074029570
	.word	1074031656
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC158
	.word	.LC160
	.word	.LC161
	.word	.LC162
	.word	.LC159
	.word	.LANCHOR4
	.word	.LC163
	.word	.LC164
	.word	.LC165
	.word	.LC166
	.word	.LC167
	.word	.LC168
	.word	.LC169
	.word	.LC170
	.word	.LC171
.L3594:
	ldr	r2, [sp, #8]
	ldr	r3, .L3662
	cmp	r2, r3
	beq	.L3595
.L3596:
	mov	r4, #-1
	b	.L3580
.L3595:
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3596
	ldr	r6, .L3662+4
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #-28]
	bl	memcpy
	ldr	r2, [r6, #-24]
	ldr	r3, .L3662+8
	cmp	r2, r3
	beq	.L3597
	add	r0, sp, #72
	movs	r1, #128
	str	r4, [sp, #16]
	str	r4, [sp, #20]
	bl	__memzero
.L3597:
	add	r0, sp, #264
	mov	r1, #256
	movs	r3, #0
	str	r3, [sp, #24]
	bl	__memzero
	b	.L3656
.L3561:
	ldr	r0, .L3662+12
	bl	printk
	add	r0, sp, #8
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3599
	ldr	r2, [sp, #8]
	ldr	r3, .L3662
	cmp	r2, r3
	bne	.L3596
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3596
	ldr	r2, .L3662+4
	ldr	r3, .L3662+8
	ldr	r1, [r2, #-24]
	cmp	r1, r3
	bne	.L3616
	ldr	r3, [sp, #20]
	subs	r1, r3, #1
	cmp	r1, #127
	bhi	.L3617
	ldr	r4, [r2, #-28]
	add	r1, sp, #72
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #20]
	bl	memcpy
	movs	r0, #1
	mov	r1, r4
	b	.L3649
.L3565:
	ldr	r0, .L3662+16
	bl	printk
	add	r0, sp, #8
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3599
	ldr	r2, [sp, #8]
	ldr	r3, .L3662+20
	cmp	r2, r3
	bne	.L3596
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3596
	ldr	r5, .L3662+4
	ldr	r3, [r5, #-20]
	cbnz	r3, .L3600
.L3603:
	movs	r0, #0
	b	.L3553
.L3600:
	ldr	r3, [r5, #-16]
	ldr	r2, .L3662+24
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3601
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r5, #-16]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3601:
	ldr	r1, [r5, #-16]
	movs	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #-28]
	ldr	r2, .L3662
	ldr	r1, [r3]
	cmp	r1, r2
	it	ne
	strne	r2, [r3]
	ldr	r0, [r5, #-28]
	itt	ne
	movne	r2, #504
	ldrne	r3, .L3662+4
	mov	r1, #128
	add	r0, r0, #64
	itt	ne
	ldrne	r3, [r3, #-28]
	strne	r4, [r3, #8]
	mov	r4, #0
	it	ne
	strne	r2, [r3, #4]
	str	r4, [r0, #-52]
	bl	__memzero
	movs	r0, #1
	ldr	r1, [r5, #-28]
	bl	StorageSysDataStore
	str	r4, [r5, #-20]
	str	r4, [r5, #-24]
	b	.L3580
.L3556:
	ldr	r0, .L3662+28
	bl	printk
	add	r0, sp, #8
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3599
	ldr	r2, [sp, #8]
	ldr	r3, .L3662+32
	cmp	r2, r3
	bne	.L3596
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3596
	ldr	r5, .L3662+4
	ldr	r3, [r5, #-20]
	cmp	r3, #1
	beq	.L3603
	ldr	r3, [r5, #-16]
	ldr	r2, .L3662+24
	ldr	r1, [r3]
	cmp	r1, r2
	it	ne
	strne	r2, [r3]
	ldr	r1, [r5, #-16]
	itttt	ne
	movne	r2, #504
	ldrne	r3, [r5, #-16]
	strne	r2, [r3, #4]
	strne	r0, [r3, #8]
	it	ne
	strne	r0, [r3, #12]
	movs	r3, #1
	movs	r0, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #-28]
	ldr	r2, .L3662
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3605
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L3662+4
	ldr	r3, [r3, #-28]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
.L3605:
	ldr	r0, [r5, #-28]
	movs	r1, #128
	movs	r4, #0
	adds	r0, r0, #64
	str	r4, [r0, #-52]
	bl	__memzero
	movs	r0, #1
	ldr	r1, [r5, #-28]
	bl	StorageSysDataStore
	movs	r3, #1
	str	r3, [r5, #-20]
	b	.L3580
.L3562:
	ldr	r0, .L3662+36
	bl	printk
	add	r0, sp, #8
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3599
	ldr	r2, [sp, #8]
	ldr	r3, .L3662+40
	cmp	r2, r3
	bne	.L3596
	ldr	r2, [sp, #12]
	cmp	r2, #512
	bhi	.L3596
	add	r0, sp, #16
	ldr	r1, .L3662+44
	b	.L3655
.L3563:
	ldr	r0, .L3662+48
	cmp	r4, r0
	mov	r7, r0
	bne	.L3606
	ldr	r0, .L3662+52
	b	.L3645
.L3606:
	ldr	r3, .L3662+56
	cmp	r4, r3
	ite	eq
	ldreq	r0, .L3662+60
	ldrne	r0, .L3662+64
.L3645:
	bl	printk
	add	r0, sp, #8
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3599
	ldr	r2, [sp, #8]
	ldr	r3, .L3662+68
	cmp	r2, r3
	bne	.L3651
	ldr	r3, .L3662+56
	ldr	r6, .L3662+4
	cmp	r4, r3
	bne	.L3609
	ldr	r3, [r6, #-28]
	mov	r0, r5
	add	r1, sp, #8
	movs	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #12]
	strb	r3, [sp, #16]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3553
	b	.L3651
.L3609:
	ldr	r3, [r6, #500]
	cmp	r3, #10
	bhi	.L3651
	ldr	r1, [r6, #-28]
	ldr	r3, [sp, #12]
	ldr	r2, [r1, #24]
	cmp	r2, r3
	beq	.L3610
	cbz	r2, .L3610
	mov	r1, r3
	ldr	r0, .L3662+72
	bl	printk
	ldr	r3, [r6, #500]
	adds	r3, r3, #1
	str	r3, [r6, #500]
	b	.L3651
.L3610:
	movs	r2, #0
	cmp	r4, r7
	str	r2, [r6, #500]
	mov	r0, #1
	itet	eq
	moveq	r3, r2
	movne	r2, #1
	moveq	r2, r3
	str	r3, [r1, #24]
	str	r2, [r1, #20]
	bl	StorageSysDataStore
	adds	r0, r0, #1
	bne	.L3648
	mvn	r4, #1
	b	.L3580
.L3663:
	.align	2
.L3662:
	.word	1263358532
	.word	.LANCHOR4
	.word	-1067903959
	.word	.LC172
	.word	.LC173
	.word	1112753220
	.word	1146313043
	.word	.LC174
	.word	1112755781
	.word	.LC175
	.word	1094995539
	.word	.LANCHOR4-12
	.word	1074031666
	.word	.LC176
	.word	1074031676
	.word	.LC177
	.word	.LC178
	.word	1280262987
	.word	.LC179
.L3575:
	ldr	r0, .L3664
	bl	printk
	add	r0, sp, #8
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3599
	ldr	r2, [sp, #8]
	ldr	r3, .L3664+4
	cmp	r2, r3
	bne	.L3596
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L3596
	ldr	r3, .L3664+8
	add	r0, sp, #16
	cmp	r4, r3
	ldr	r3, .L3664+12
	ite	eq
	ldreq	r1, [r3, #504]
	ldrne	r1, [r3, #508]
	adds	r1, r1, #8
.L3655:
	bl	memcpy
.L3656:
	mov	r0, r5
	add	r1, sp, #8
	mov	r2, #520
	b	.L3642
.L3572:
	ldr	r0, .L3664+16
	bl	printk
	add	r0, sp, #8
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3599
	ldr	r2, [sp, #8]
	ldr	r3, .L3664+4
	cmp	r2, r3
	bne	.L3596
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L3596
	cmp	r4, r6
	add	r2, r2, #8
	ldr	r4, .L3664+12
	bne	.L3614
	add	r1, sp, #8
	ldr	r0, [r4, #504]
	bl	memcpy
	movs	r0, #2
	ldr	r1, [r4, #504]
	b	.L3649
.L3614:
	add	r1, sp, #8
	ldr	r0, [r4, #508]
	bl	memcpy
	ldr	r1, [r4, #508]
	movs	r0, #3
.L3649:
	bl	StorageSysDataStore
	mov	r4, r0
.L3580:
	ldr	r0, .L3664+20
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L3553
.L3615:
	mvn	r0, #21
	b	.L3553
.L3616:
	mvn	r0, #1
	b	.L3553
.L3617:
	mvn	r0, #2
.L3553:
	add	sp, sp, #532
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L3665:
	.align	2
.L3664:
	.word	.LC180
	.word	1145980246
	.word	1074034192
	.word	.LANCHOR4
	.word	.LC181
	.word	.LC182
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
	ldr	r4, .L3675
	movs	r5, #0
	sub	r0, r4, #12
	ldr	r1, [r4, #-48]
	str	r5, [r4, #-32]
	add	r3, r1, #512
	str	r3, [r4, #-28]
	str	r1, [r4, #-16]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #504]
	str	r1, [r4, #508]
	bl	ftl_memcpy
	ldr	r6, [r4, #-16]
	str	r5, [r4, #-24]
	str	r5, [r4, #500]
	ldr	r3, [r6, #16]
	ldr	r7, [r6, #508]
	str	r3, [r4, #-20]
	cbz	r7, .L3667
	mov	r0, r6
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L3667
	str	r5, [r6, #16]
	ldr	r0, .L3675+4
	str	r5, [r4, #-20]
	bl	printk
.L3667:
	ldr	r3, [r4, #-20]
	cbz	r3, .L3668
	ldr	r2, .L3675+8
	ldr	r3, .L3675
	str	r2, [r3, #-24]
.L3668:
	ldr	r1, [r4, #504]
	movs	r0, #2
	bl	StorageSysDataLoad
	ldr	r1, [r4, #508]
	movs	r0, #3
	bl	StorageSysDataLoad
	pop	{r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L3676:
	.align	2
.L3675:
	.word	.LANCHOR4
	.word	.LC183
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
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r0, #65536
	bl	ftl_malloc
	ldr	r5, .L3687
	str	r0, [r5, #512]
	cmp	r0, #0
	beq	.L3684
	movs	r7, #0
	ldr	r8, .L3687+8
	mov	r9, r5
	mov	r4, r7
	mov	r6, r7
.L3682:
	lsls	r0, r6, #7
	movs	r1, #128
	ldr	r2, [r5, #512]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L3680
	ldr	r3, [r9, #512]
	ldr	r0, .L3687+4
	add	r2, r3, #65280
	adds	r2, r2, #252
	ldr	r1, [r3]
	ldr	r3, [r3, #4]
	ldr	r2, [r2]
	bl	printk
	ldr	r2, [r9, #512]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L3681
	add	r3, r2, #65280
	ldr	r1, [r2, #4]
	adds	r3, r3, #252
	ldr	r3, [r3]
	cmp	r3, r1
	bne	.L3681
	cmp	r4, r3
	itt	cc
	movcc	r7, r6
	movcc	r4, r3
.L3681:
	cbnz	r6, .L3686
	movs	r6, #1
	b	.L3682
.L3686:
	cbz	r4, .L3683
	lsls	r0, r7, #7
	movs	r1, #128
	bl	FlashBootVendorRead
	cbnz	r0, .L3680
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L3683:
	mov	r0, r2
	mov	r1, #65536
	bl	__memzero
	ldr	r3, [r5, #512]
	ldr	r2, .L3687+8
	mov	r0, r4
	str	r6, [r3, #4]
	str	r2, [r3]
	movw	r2, #65532
	str	r6, [r3, r2]
	movw	r2, #64504
	strh	r4, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L3680:
	ldr	r0, [r5, #512]
	bl	kfree
	movs	r3, #0
	mov	r0, #-1
	str	r3, [r5, #512]
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L3684:
	mvn	r0, #11
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L3688:
	.align	2
.L3687:
	.word	.LANCHOR4
	.word	.LC184
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
	ldr	r3, .L3695
	ldr	r5, [r3, #512]
	cbz	r5, .L3694
	ldrh	r7, [r5, #10]
	movs	r3, #0
.L3691:
	cmp	r3, r7
	bcs	.L3694
	add	r6, r5, r3, lsl #3
	ldrh	r4, [r6, #16]
	cmp	r4, r0
	bne	.L3692
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
.L3692:
	adds	r3, r3, #1
	b	.L3691
.L3694:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L3696:
	.align	2
.L3695:
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
	ldr	r2, .L3719
	.pad #28
	sub	sp, sp, #28
	mov	ip, r0
	mov	r3, r1
	ldr	r4, [r2, #512]
	cmp	r4, #0
	beq	.L3712
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	ldrh	r2, [r4, #10]
	bic	r7, r7, #63
	movs	r6, #0
	str	r1, [sp, #4]
.L3699:
	cmp	r6, r2
	bcs	.L3717
	add	r5, r4, r6, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L3700
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L3701
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L3712
	ldrh	r8, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #12]
.L3702:
	ldr	r2, [sp, #12]
	adds	r5, r5, #8
	cmp	r6, r2
	bcs	.L3718
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
	b	.L3702
.L3718:
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
	ldr	r3, [sp, #8]
	strh	r8, [r4, #12]	@ movhi
	add	r3, r3, r5
	subs	r7, r3, r7
	strh	r7, [r4, #14]	@ movhi
	b	.L3716
.L3701:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, r0, fp
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L3716
.L3700:
	adds	r6, r6, #1
	b	.L3699
.L3717:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L3712
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
.L3716:
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
	b	.L3698
.L3712:
	mov	r0, #-1
.L3698:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3720:
	.align	2
.L3719:
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #4096
	mov	r6, r1
	mov	r5, r2
	bl	ftl_malloc
	mov	r4, r0
	cmp	r0, #0
	beq	.L3730
	ldr	r3, .L3737
	cmp	r6, r3
	beq	.L3724
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L3725
	b	.L3736
.L3724:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3736
	ldr	r2, [r4]
	ldr	r3, .L3737+4
	cmp	r2, r3
	beq	.L3727
.L3728:
	mov	r5, #-1
	b	.L3723
.L3727:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	adds	r3, r0, #1
	beq	.L3728
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
	b	.L3723
.L3725:
	mov	r1, r5
	movs	r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L3736
	ldr	r2, [r4]
	ldr	r3, .L3737+4
	cmp	r2, r3
	bne	.L3728
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L3728
	mov	r0, r4
	mov	r1, r5
	adds	r2, r2, #8
	bl	rk_copy_from_user
	cbnz	r0, .L3736
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L3723
.L3736:
	mvn	r5, #13
.L3723:
	mov	r0, r4
	bl	kfree
	b	.L3722
.L3730:
	mov	r5, #-1
.L3722:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L3738:
	.align	2
.L3737:
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
	.type	__func__.17344, %object
	.size	__func__.17344, 11
__func__.17344:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.space	1
	.type	__func__.18293, %object
	.size	__func__.18293, 21
__func__.18293:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.17417, %object
	.size	__func__.17417, 8
__func__.17417:
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
	.ascii	"FtlFreeSysBlkQueueOut free count = %d\012\000"
.LC5:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC6:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC7:
	.ascii	"FLASH INFO:\012\000"
.LC8:
	.ascii	"FLASH ID: %x\012\000"
.LC9:
	.ascii	"Device Capacity: %d MB\012\000"
.LC10:
	.ascii	"FMWAIT: %x %x %x %x\012\000"
.LC11:
	.ascii	"FTL INFO:\012\000"
.LC12:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC13:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC14:
	.ascii	"read_page_count = 0x%x\012\000"
.LC15:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC16:
	.ascii	"write_page_count = 0x%x\012\000"
.LC17:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC18:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC19:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC20:
	.ascii	"totle_write = %d MB\012\000"
.LC21:
	.ascii	"totle_read = %d MB\012\000"
.LC22:
	.ascii	"GSV = 0x%x\012\000"
.LC23:
	.ascii	"GDV = 0x%x\012\000"
.LC24:
	.ascii	"bad blk num = %d %d\012\000"
.LC25:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC26:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC27:
	.ascii	"slc_EC = 0x%x\012\000"
.LC28:
	.ascii	"avg_EC = 0x%x\012\000"
.LC29:
	.ascii	"sys_EC = 0x%x\012\000"
.LC30:
	.ascii	"max_EC = 0x%x\012\000"
.LC31:
	.ascii	"min_EC = 0x%x\012\000"
.LC32:
	.ascii	"PLT = 0x%x\012\000"
.LC33:
	.ascii	"POT = 0x%x\012\000"
.LC34:
	.ascii	"MaxSector = 0x%x\012\000"
.LC35:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC36:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC37:
	.ascii	"free sysblock = 0x%x\012\000"
.LC38:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC39:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC40:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC41:
	.ascii	"Sys.id = 0x%x\012\000"
.LC42:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC43:
	.ascii	"ACT.page = 0x%x\012\000"
.LC44:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC45:
	.ascii	"ACT.id = 0x%x\012\000"
.LC46:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC47:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC48:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC49:
	.ascii	"BUF.page = 0x%x\012\000"
.LC50:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC51:
	.ascii	"BUF.id = 0x%x\012\000"
.LC52:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC53:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC54:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC55:
	.ascii	"TMP.page = 0x%x\012\000"
.LC56:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC57:
	.ascii	"TMP.id = 0x%x\012\000"
.LC58:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC59:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC60:
	.ascii	"GC.page = 0x%x\012\000"
.LC61:
	.ascii	"GC.plane = 0x%x\012\000"
.LC62:
	.ascii	"GC.id = 0x%x\012\000"
.LC63:
	.ascii	"GC.mode = 0x%x\012\000"
.LC64:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC65:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC66:
	.ascii	"Read Err Cnt = 0x%x\012\000"
.LC67:
	.ascii	"Prog Err Cnt = 0x%x\012\000"
.LC68:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC69:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC70:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC71:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC72:
	.ascii	"free min EC= 0x%x\012\000"
.LC73:
	.ascii	"free max EC= 0x%x\012\000"
.LC74:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC75:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC76:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC77:
	.ascii	"%s\012\000"
.LC78:
	.ascii	"FTL version: 5.0.48 20180528\000"
.LC79:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC80:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC81:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC82:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC83:
	.ascii	"%s %p:0x%x:\000"
.LC84:
	.ascii	"%x \000"
.LC85:
	.ascii	"\000"
.LC86:
	.ascii	"otp error! %d\000"
.LC87:
	.ascii	"rr\000"
.LC88:
	.ascii	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\012"
	.ascii	"\000"
.LC89:
	.ascii	"nandc:\000"
.LC90:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC91:
	.ascii	"sdr read ok %x ecc=%d\012\000"
.LC92:
	.ascii	"sync para %d\012\000"
.LC93:
	.ascii	"TOG mode Read error %x %x\012\000"
.LC94:
	.ascii	"read retry status %x %x %x\012\000"
.LC95:
	.ascii	"micron RR %d row=%x,count %d,status=%d\012\000"
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
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC107:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC108:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC109:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC110:
	.ascii	"bad block:%d %d\012\000"
.LC111:
	.ascii	"FMFB:%d %d\012\000"
.LC112:
	.ascii	"E:bad block:%d\012\000"
.LC113:
	.ascii	"FMFB:Save %d %d\012\000"
.LC114:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC115:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC116:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC117:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC118:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC119:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC120:
	.ascii	"ftl_map_blk_gc blk info: %x %x %x\012\000"
.LC121:
	.ascii	"page map lost: %x %x\012\000"
.LC122:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC123:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC124:
	.ascii	"page map lost %x %x %x %x\012\000"
.LC125:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC126:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC127:
	.ascii	"no ect\000"
.LC128:
	.ascii	"slc mode\000"
.LC129:
	.ascii	"RSB refresh addr %x\012\000"
.LC130:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC131:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC132:
	.ascii	"...%s enter...\012\000"
.LC133:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC134:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC135:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC136:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC137:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC138:
	.ascii	"GC des block %x done\012\000"
.LC139:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC140:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC141:
	.ascii	"FtlInit %x\012\000"
.LC142:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC143:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC144:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC145:
	.ascii	"BBT:\000"
.LC146:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC147:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC148:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC149:
	.ascii	"idblk:\000"
.LC150:
	.ascii	"idb reverse %x %x\012\000"
.LC151:
	.ascii	"write_idblock totle_sec %x %x %x %x\012\000"
.LC152:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC153:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC154:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC155:
	.ascii	"write\000"
.LC156:
	.ascii	"read\000"
.LC157:
	.ascii	"write_idblock error %d\012\000"
.LC158:
	.ascii	"READ_SECTOR_IO\012\000"
.LC159:
	.ascii	"rk_copy_from_user error\012\000"
.LC160:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC161:
	.ascii	"rk_copy_to_user error\012\000"
.LC162:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC163:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC164:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC165:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC166:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC167:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC168:
	.ascii	"bbt:\000"
.LC169:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC170:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC171:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC172:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC173:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC174:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC175:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC176:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC177:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC178:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC179:
	.ascii	"LockKey not match %d\012\000"
.LC180:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC181:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC182:
	.ascii	"return ret = %lx\012\000"
.LC183:
	.ascii	"secureBootEn check error\012\000"
.LC184:
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
	.size	NandFlashParaTbl, 2368
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
	.type	g_MaxLbaSector, %object
	.size	g_MaxLbaSector, 4
g_MaxLbaSector:
	.space	4
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
	.space	3
	.type	g_nandc_version_data, %object
	.size	g_nandc_version_data, 4
g_nandc_version_data:
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
