/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-26
 */
	.syntax unified
	.arch armv7-a
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 4
	.eabi_attribute 34, 1
	.file	"rk_ftl_arm_v7.c"
	.thumb
	.text
	.align	1
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ndelay, %function
ndelay:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L2
	addw	r0, r0, #999
	umull	r0, r1, r0, r3
	ldr	r3, .L2+4
	ldr	r3, [r3, #8]
	lsrs	r0, r1, #6
	bx	r3	@ indirect register sibling call
.L3:
	.align	2
.L2:
	.word	274877907
	.word	arm_delay_ops
	.fnend
	.size	ndelay, .-ndelay
	.align	1
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L5
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	movs	r0, #80
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r4, r4, r3, lsl #8
	movs	r3, #122
	str	r3, [r4, #2056]
	bl	ndelay
	ldr	r3, [r4, #2048]
	ldr	r0, [r4, #2048]
	and	r3, r3, #15
	and	r0, r0, #15
	cmp	r0, r3
	it	cc
	movcc	r0, r3
	ldr	r3, [r4, #2048]
	and	r3, r3, #15
	cmp	r3, r0
	it	cc
	movcc	r3, r0
	ldr	r0, [r4, #2048]
	and	r0, r0, #15
	cmp	r0, r3
	it	cc
	movcc	r0, r3
	pop	{r4, pc}
.L6:
	.align	2
.L5:
	.word	.LANCHOR0
	.fnend
	.size	flash_read_ecc, .-flash_read_ecc
	.align	1
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_set_blk_mode.part.9, %function
ftl_set_blk_mode.part.9:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L8
	lsrs	r1, r0, #5
	and	r0, r0, #31
	ldr	r2, [r3, #32]
	movs	r3, #1
	lsl	r0, r3, r0
	ldr	r3, [r2, r1, lsl #2]
	orrs	r3, r3, r0
	str	r3, [r2, r1, lsl #2]
	bx	lr
.L9:
	.align	2
.L8:
	.word	.LANCHOR0
	.fnend
	.size	ftl_set_blk_mode.part.9, .-ftl_set_blk_mode.part.9
	.align	1
	.global	FlashMemCmp8
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L18
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	cbz	r3, .L13
	ldrb	r4, [r0, #1]	@ zero_extendqisi2
	ldrb	r3, [r1, #1]	@ zero_extendqisi2
	cmp	r4, r3
	beq	.L17
	movs	r3, #0
.L13:
	cmp	r3, r2
	bne	.L15
.L17:
	movs	r0, #0
	pop	{r4, r5, pc}
.L15:
	ldrb	r5, [r0, r3]	@ zero_extendqisi2
	ldrb	r4, [r1, r3]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r5, r4
	beq	.L13
	mov	r0, r3
	pop	{r4, r5, pc}
.L19:
	.align	2
.L18:
	.word	.LANCHOR0
	.fnend
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	1
	.global	FlashRsvdBlkChk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L23
	push	{r4, lr}
	.save {r4, lr}
	ldrb	r4, [r2, #37]	@ zero_extendqisi2
	ldr	r3, [r2, #40]
	muls	r3, r4, r3
	cmp	r3, r1
	bls	.L22
	adds	r0, r0, #0
	it	ne
	movne	r0, #1
	pop	{r4, pc}
.L22:
	movs	r0, #1
	pop	{r4, pc}
.L24:
	.align	2
.L23:
	.word	.LANCHOR0
	.fnend
	.size	FlashRsvdBlkChk, .-FlashRsvdBlkChk
	.align	1
	.global	FlashGetRandomizer
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashGetRandomizer, %function
FlashGetRandomizer:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L33
	and	r2, r1, #127
	push	{r4, lr}
	.save {r4, lr}
	ldrh	r4, [r3, r2, lsl #1]
	ldr	r3, .L33+4
	ldrb	r3, [r3, #44]	@ zero_extendqisi2
	cbz	r3, .L25
	bl	FlashRsvdBlkChk
	cbz	r0, .L25
	orr	r4, r4, #-1073741824
.L25:
	mov	r0, r4
	pop	{r4, pc}
.L34:
	.align	2
.L33:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashGetRandomizer, .-FlashGetRandomizer
	.align	1
	.global	FlashSetRandomizer
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashSetRandomizer, %function
FlashSetRandomizer:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	and	r2, r1, #127
	ldr	r3, .L43
	mov	r6, r0
	ldr	r4, .L43+4
	ldrh	r5, [r3, r2, lsl #1]
	ldrb	r2, [r4, #44]	@ zero_extendqisi2
	cbz	r2, .L36
	bl	FlashRsvdBlkChk
	cbz	r0, .L36
	orr	r5, r5, #-1073741824
.L36:
	ldr	r3, [r4, r6, lsl #3]
	str	r5, [r3, #336]
	pop	{r4, r5, r6, pc}
.L44:
	.align	2
.L43:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	1
	.global	FlashBlockAlignInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashBlockAlignInit, %function
FlashBlockAlignInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, #512
	ldr	r3, .L51
	bls	.L46
	mov	r2, #1024
.L50:
	str	r2, [r3, #40]
	bx	lr
.L46:
	cmp	r0, #256
	bls	.L48
	mov	r2, #512
	b	.L50
.L48:
	cmp	r0, #128
	bhi	.L49
	str	r0, [r3, #40]
	bx	lr
.L49:
	mov	r2, #256
	b	.L50
.L52:
	.align	2
.L51:
	.word	.LANCHOR0
	.fnend
	.size	FlashBlockAlignInit, .-FlashBlockAlignInit
	.align	1
	.global	FlashReadCmd
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadCmd, %function
FlashReadCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	push	{r4, r5}
	.save {r4, r5}
	ldr	r4, .L55
	ldr	r3, [r4, r0, lsl #3]
	add	r2, r4, r0, lsl #3
	ldr	r4, [r4, #48]
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	ldrb	r4, [r4, #7]	@ zero_extendqisi2
	lsls	r2, r2, #8
	cmp	r4, #1
	itt	eq
	moveq	r5, #38
	addeq	r4, r3, r2
	add	r3, r3, r2
	mov	r2, #0
	it	eq
	streq	r5, [r4, #2056]
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
	pop	{r4, r5}
	b	FlashSetRandomizer
.L56:
	.align	2
.L55:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadCmd, .-FlashReadCmd
	.align	1
	.global	FlashReadDpDataOutCmd
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadDpDataOutCmd, %function
FlashReadDpDataOutCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	push	{r4, r5, r6}
	.save {r4, r5, r6}
	uxtb	r6, r1
	ldr	r4, .L61
	lsrs	r5, r1, #8
	ldr	r3, [r4, r0, lsl #3]
	add	r2, r4, r0, lsl #3
	ldrb	r4, [r4, #68]	@ zero_extendqisi2
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	cmp	r4, #1
	lsr	r4, r1, #16
	lsl	r2, r2, #8
	add	r3, r3, r2
	bne	.L58
	movs	r2, #6
	str	r2, [r3, #2056]
	movs	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r6, [r3, #2052]
	str	r5, [r3, #2052]
	str	r4, [r3, #2052]
.L60:
	movs	r2, #224
	str	r2, [r3, #2056]
	pop	{r4, r5, r6}
	b	FlashSetRandomizer
.L58:
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
	b	.L60
.L62:
	.align	2
.L61:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	1
	.global	FlashProgFirstCmd
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgFirstCmd, %function
FlashProgFirstCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	push	{r4, r5}
	.save {r4, r5}
	lsrs	r2, r1, #16
	ldr	r4, .L64
	ldr	r3, [r4, r0, lsl #3]
	add	r4, r4, r0, lsl #3
	ldrb	r4, [r4, #4]	@ zero_extendqisi2
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
	pop	{r4, r5}
	b	FlashSetRandomizer
.L65:
	.align	2
.L64:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgFirstCmd, .-FlashProgFirstCmd
	.align	1
	.global	FlashEraseCmd
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashEraseCmd, %function
FlashEraseCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r4, .L71
	ldr	r3, [r4, r0, lsl #3]
	add	r0, r4, r0, lsl #3
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	lsls	r0, r0, #8
	cbz	r2, .L67
	adds	r2, r3, r0
	movs	r5, #96
	str	r5, [r2, #2056]
	uxtb	r5, r1
	str	r5, [r2, #2052]
	lsrs	r5, r1, #8
	str	r5, [r2, #2052]
	lsrs	r5, r1, #16
	str	r5, [r2, #2052]
	ldr	r2, [r4, #40]
	add	r1, r1, r2
.L67:
	add	r3, r3, r0
	movs	r2, #96
	str	r2, [r3, #2056]
	uxtb	r2, r1
	str	r2, [r3, #2052]
	lsrs	r2, r1, #8
	lsrs	r1, r1, #16
	str	r2, [r3, #2052]
	movs	r2, #208
	str	r1, [r3, #2052]
	str	r2, [r3, #2056]
	pop	{r4, r5, pc}
.L72:
	.align	2
.L71:
	.word	.LANCHOR0
	.fnend
	.size	FlashEraseCmd, .-FlashEraseCmd
	.align	1
	.global	FlashProgDpSecondCmd
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgDpSecondCmd, %function
FlashProgDpSecondCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	push	{r4, r5, r6}
	.save {r4, r5, r6}
	lsrs	r2, r1, #16
	ldr	r5, .L74
	ldr	r3, [r5, r0, lsl #3]
	add	r4, r5, r0, lsl #3
	ldrb	r6, [r4, #4]	@ zero_extendqisi2
	ldrb	r4, [r5, #63]	@ zero_extendqisi2
	add	r3, r3, r6, lsl #8
	str	r4, [r3, #2056]
	movs	r4, #0
	str	r4, [r3, #2052]
	str	r4, [r3, #2052]
	uxtb	r4, r1
	str	r4, [r3, #2052]
	lsrs	r4, r1, #8
	str	r4, [r3, #2052]
	str	r2, [r3, #2052]
	pop	{r4, r5, r6}
	b	FlashSetRandomizer
.L75:
	.align	2
.L74:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpSecondCmd, .-FlashProgDpSecondCmd
	.align	1
	.global	FlashProgSecondCmd
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgSecondCmd, %function
FlashProgSecondCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L77
	ldr	r4, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	ldr	r0, .L77+4
	ldrb	r5, [r3, #4]	@ zero_extendqisi2
	ldr	r3, .L77+8
	add	r4, r4, r5, lsl #8
	ldr	r3, [r3, #4]
	blx	r3
	movs	r3, #16
	str	r3, [r4, #2056]
	pop	{r3, r4, r5, pc}
.L78:
	.align	2
.L77:
	.word	.LANCHOR0
	.word	64424500
	.word	arm_delay_ops
	.fnend
	.size	FlashProgSecondCmd, .-FlashProgSecondCmd
	.align	1
	.global	FlashProgDpFirstCmd
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgDpFirstCmd, %function
FlashProgDpFirstCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L80
	ldr	r3, [r2, r0, lsl #3]
	add	r0, r2, r0, lsl #3
	ldrb	r2, [r2, #62]	@ zero_extendqisi2
	ldrb	r1, [r0, #4]	@ zero_extendqisi2
	add	r3, r3, r1, lsl #8
	str	r2, [r3, #2056]
	bx	lr
.L81:
	.align	2
.L80:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	1
	.global	FlashReadStatus
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadStatus, %function
FlashReadStatus:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r2, #112
	ldr	r3, .L83
	ldr	r5, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	movs	r0, #80
	ldrb	r4, [r3, #4]	@ zero_extendqisi2
	add	r3, r5, r4, lsl #8
	adds	r4, r4, #8
	lsls	r4, r4, #8
	str	r2, [r3, #2056]
	bl	ndelay
	ldr	r0, [r5, r4]
	pop	{r3, r4, r5, pc}
.L84:
	.align	2
.L83:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatus, .-FlashReadStatus
	.align	1
	.global	js_hash
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L88
	add	r1, r1, r0
	push	{r4, lr}
	.save {r4, lr}
.L86:
	cmp	r0, r1
	bne	.L87
	mov	r0, r3
	pop	{r4, pc}
.L87:
	lsrs	r2, r3, #2
	ldrb	r4, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	add	r2, r2, r4
	eors	r3, r3, r2
	b	.L86
.L89:
	.align	2
.L88:
	.word	1204201446
	.fnend
	.size	js_hash, .-js_hash
	.align	1
	.global	FlashLoadIdbInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.global	ToshibaSetRRPara
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ToshibaSetRRPara, %function
ToshibaSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r8, r1, r1, lsl #2
	ldr	r9, .L100+8
	mov	r5, r0
	mov	r6, r1
	movs	r4, #0
	ldr	r7, .L100
	add	r10, r9, #256
.L93:
	ldrb	r3, [r7, #85]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L97
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L97:
	movs	r3, #85
	movs	r0, #200
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	str	r3, [r5, #4]
	bl	ndelay
	ldrb	r3, [r7, #84]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L94
	add	r3, r4, r8
	add	r3, r3, r10
.L99:
	ldrsb	r3, [r3, #5]
.L98:
	str	r3, [r5]
	adds	r4, r4, #1
	b	.L93
.L94:
	cmp	r3, #35
	bne	.L96
	ldr	r2, .L100+4
	add	r3, r4, r8
	add	r3, r3, r2
	b	.L99
.L96:
	add	r3, r9, r6
	ldrsb	r3, [r3, #396]
	b	.L98
.L101:
	.align	2
.L100:
	.word	.LANCHOR0
	.word	.LANCHOR1+301
	.word	.LANCHOR1
	.fnend
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	1
	.global	SamsungSetRRPara
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r4, #0
	ldr	r7, .L105
	mov	r6, r0
	mov	r9, #161
	mov	r10, r4
	ldr	r8, .L105+4
	add	r1, r7, r1, lsl #2
	adds	r5, r1, #3
.L103:
	ldrb	r3, [r8, #85]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L104
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L104:
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	adds	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	ndelay
	b	.L103
.L106:
	.align	2
.L105:
	.word	.LANCHOR1+404
	.word	.LANCHOR0
	.fnend
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	1
	.global	ftl_flash_suspend
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L108
	ldr	r2, [r3, #88]
	ldr	r1, [r2]
	str	r1, [r3, #92]
	ldr	r1, [r2, #4]
	str	r1, [r3, #96]
	ldr	r1, [r2, #8]
	str	r1, [r3, #100]
	ldr	r1, [r2, #12]
	str	r1, [r3, #104]
	ldr	r1, [r2, #304]
	str	r1, [r3, #108]
	ldr	r1, [r2, #308]
	str	r1, [r3, #112]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #116]
	str	r2, [r3, #120]
	bx	lr
.L109:
	.align	2
.L108:
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.global	__aeabi_uidiv
	.global	__aeabi_uidivmod
	.align	1
	.global	LogAddr2PhyAddr
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r9, r2
	ldr	r4, .L115
	mov	fp, r3
	mov	r10, r1
	mov	r7, r0
	ldr	r5, [r0, #4]
	ldrh	r2, [r4, #136]
	ldrh	r3, [r4, #138]
	ldrh	r6, [r4, #40]
	smulbb	r3, r3, r2
	ldrb	r2, [r4, #36]	@ zero_extendqisi2
	uxth	r3, r3
	cmp	r2, #1
	it	eq
	lsleq	r6, r6, #1
	ubfx	r2, r5, #10, #16
	mov	r1, r3
	str	r3, [sp, #4]
	mov	r0, r2
	it	eq
	uxtheq	r6, r6
	str	r2, [sp]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	uxth	r8, r0
	ldr	r2, [sp]
	mov	r1, r3
	mov	r0, r2
	bl	__aeabi_uidivmod
	cmp	r10, #1
	uxth	r1, r1
	ubfx	r0, r5, #0, #10
	bne	.L112
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cbnz	r3, .L112
	add	r0, r4, r0, lsl #1
	ldrh	r0, [r0, #156]
.L112:
	add	r4, r4, r8, lsl #2
	ldr	r3, [r4, #1180]
	mla	r6, r6, r1, r3
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	cmp	r3, #1
	add	r0, r0, r6
	str	r0, [r9]
	str	r8, [fp]
	bls	.L114
	ldr	r0, [r7, #4]
	ldr	r3, [r7, #40]
	add	r0, r0, #1024
	subs	r3, r0, r3
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
.L113:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L114:
	movs	r0, #0
	b	.L113
.L116:
	.align	2
.L115:
	.word	.LANCHOR0
	.fnend
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	1
	.global	FlashReadStatusEN
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L129
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, [r3, r0, lsl #3]
	add	r0, r3, r0, lsl #3
	ldrb	r4, [r0, #4]	@ zero_extendqisi2
	ldr	r0, [r3, #48]
	ldrb	r0, [r0, #8]	@ zero_extendqisi2
	cmp	r0, #2
	mov	r0, r3
	lsl	r3, r4, #8
	add	r4, r4, #8
	bne	.L118
	cbnz	r2, .L119
	ldrb	r2, [r0, #65]	@ zero_extendqisi2
.L128:
	add	r3, r3, r5
	str	r2, [r3, #2056]
	ldrb	r0, [r0, #67]	@ zero_extendqisi2
	cbz	r0, .L123
	add	r6, r5, r4, lsl #8
	movs	r2, #0
.L122:
	cmp	r2, r0
	bcc	.L124
.L123:
	lsls	r4, r4, #8
	movs	r0, #80
	bl	ndelay
	ldr	r0, [r5, r4]
	uxtb	r0, r0
	pop	{r4, r5, r6, pc}
.L119:
	ldrb	r2, [r0, #66]	@ zero_extendqisi2
	b	.L128
.L124:
	lsls	r3, r2, #3
	adds	r2, r2, #1
	lsr	r3, r1, r3
	uxtb	r3, r3
	str	r3, [r6, #4]
	b	.L122
.L118:
	add	r3, r3, r5
	movs	r2, #112
	str	r2, [r3, #2056]
	b	.L123
.L130:
	.align	2
.L129:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatusEN, .-FlashReadStatusEN
	.align	1
	.global	FlashWaitReadyEN
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
.L132:
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	bl	FlashReadStatusEN
	cmp	r0, #255
	mov	r3, r0
	beq	.L132
	lsls	r3, r3, #25
	bmi	.L131
	movs	r1, #3
	movs	r0, #1
	bl	usleep_range
	b	.L132
.L131:
	pop	{r4, r5, r6, pc}
	.fnend
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
	.align	1
	.global	FlashScheduleEnSet
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L138
	ldr	r2, [r3, #1212]
	str	r0, [r3, #1212]
	mov	r0, r2
	bx	lr
.L139:
	.align	2
.L138:
	.word	.LANCHOR0
	.fnend
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	1
	.global	FlashGetPageSize
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L141
	ldr	r3, [r3, #48]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L142:
	.align	2
.L141:
	.word	.LANCHOR0
	.fnend
	.size	FlashGetPageSize, .-FlashGetPageSize
	.align	1
	.global	NandcReadDontCaseBusyEn
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcGetChipIf, %function
NandcGetChipIf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L145
	add	r3, r2, r0, lsl #3
	ldr	r0, [r2, r0, lsl #3]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	adds	r3, r3, #8
	add	r0, r0, r3, lsl #8
	bx	lr
.L146:
	.align	2
.L145:
	.word	.LANCHOR0
	.fnend
	.size	NandcGetChipIf, .-NandcGetChipIf
	.align	1
	.global	NandcSetDdrPara
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcSetDdrPara, %function
NandcSetDdrPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L148
	ldr	r2, [r3, #88]
	lsls	r3, r0, #8
	orr	r0, r3, r0, lsl #16
	orr	r0, r0, #1
	str	r0, [r2, #304]
	bx	lr
.L149:
	.align	2
.L148:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	1
	.global	NandcSetDdrDiv
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L151
	orr	r0, r0, #16640
	ldr	r3, [r3, #88]
	str	r0, [r3, #344]
	bx	lr
.L152:
	.align	2
.L151:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	1
	.global	NandcSetDdrMode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L156
	ldr	r2, [r3, #88]
	ldr	r3, [r2]
	cbnz	r0, .L154
	bfi	r3, r0, #13, #1
.L155:
	str	r3, [r2]
	bx	lr
.L154:
	orr	r3, r3, #253952
	b	.L155
.L157:
	.align	2
.L156:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetDdrMode, .-NandcSetDdrMode
	.align	1
	.global	NandcSetMode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcSetMode, %function
NandcSetMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L165
	ands	r1, r0, #6
	ldr	r2, [r3, #88]
	ldr	r3, [r2]
	beq	.L159
	lsls	r1, r0, #29
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	add	r1, r1, #1040384
	addw	r1, r1, #3969
	orr	r3, r3, #196608
	str	r1, [r2, #304]
	it	mi
	orrmi	r3, r3, #32768
	movs	r1, #38
	str	r1, [r2, #308]
	movs	r1, #39
	str	r1, [r2, #308]
.L161:
	str	r3, [r2]
	movs	r0, #0
	bx	lr
.L159:
	bfi	r3, r1, #13, #1
	b	.L161
.L166:
	.align	2
.L165:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetMode, .-NandcSetMode
	.align	1
	.global	NandcFlashCs
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcFlashCs, %function
NandcFlashCs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L168
	movs	r2, #1
	ldr	r1, [r3, r0, lsl #3]
	add	r0, r3, r0, lsl #3
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	ldr	r3, [r1]
	lsls	r2, r2, r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L169:
	.align	2
.L168:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashCs, .-NandcFlashCs
	.align	1
	.global	NandcFlashDeCs
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcFlashDeCs, %function
NandcFlashDeCs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L171
	ldr	r2, [r3, r0, lsl #3]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L172:
	.align	2
.L171:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashDeCs, .-NandcFlashDeCs
	.align	1
	.global	HynixSetRRPara
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	HynixSetRRPara, %function
HynixSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r7, r3
	ldr	r5, .L181
	mov	r6, r0
	mov	r8, r1
	mov	r9, r2
	ldr	r3, [r5, #48]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #6
	bne	.L174
	movs	r3, #20
	add	r4, r5, #1216
	add	r3, r3, r0, lsl #6
	add	r3, r3, r7, lsl #2
.L180:
	add	r4, r4, r3
	b	.L175
.L174:
	cmp	r3, #7
	bne	.L176
	movs	r3, #160
	movs	r4, #28
	smlabb	r4, r3, r0, r4
	movs	r3, #10
	add	r2, r5, #1216
	smlabb	r3, r3, r7, r4
	adds	r4, r2, r3
.L175:
	add	r3, r5, r6, lsl #3
	ldr	r10, [r5, r6, lsl #3]
	mov	r0, r6
	ldrb	fp, [r3, #4]	@ zero_extendqisi2
	add	r8, r8, #-1
	bl	NandcFlashCs
	movs	r3, #54
	add	r8, r8, r9
	subs	r4, r4, #1
	lsl	fp, fp, #8
	add	r0, r10, fp
	str	r3, [r0, #2056]
	add	r3, r9, #-1
	mov	r9, r0
.L178:
	cmp	r3, r8
	bne	.L179
	movs	r3, #22
	add	r10, r10, fp
	str	r3, [r10, #2056]
	mov	r0, r6
	add	r5, r5, r6
	bl	NandcFlashDeCs
	strb	r7, [r5, #2068]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L176:
	cmp	r3, #8
	bne	.L177
	addw	r4, r5, #1244
	add	r3, r7, r7, lsl #2
	b	.L180
.L177:
	adds	r4, r7, #2
	add	r4, r4, r0, lsl #3
	add	r4, r5, r4, lsl #3
	addw	r4, r4, #1220
	b	.L175
.L179:
	ldrb	r2, [r3, #1]!	@ zero_extendqisi2
	movs	r0, #200
	str	r2, [r9, #2052]
	str	r3, [sp, #4]
	bl	ndelay
	ldrsb	r2, [r4, #1]!
	ldr	r3, [sp, #4]
	str	r2, [r9, #2048]
	b	.L178
.L182:
	.align	2
.L181:
	.word	.LANCHOR0
	.fnend
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	1
	.global	FlashSetReadRetryDefault
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, .L189
	ldr	r3, [r5, #48]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #1
	cmp	r3, #7
	bhi	.L183
	movs	r4, #0
	addw	r6, r5, #2072
.L186:
	ldrb	r3, [r6, r4, lsl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L185
	movs	r3, #0
	ldr	r2, .L189+4
	ldrb	r1, [r5, #1217]	@ zero_extendqisi2
	bl	HynixSetRRPara
.L185:
	adds	r4, r4, #1
	cmp	r4, #4
	bne	.L186
.L183:
	pop	{r4, r5, r6, pc}
.L190:
	.align	2
.L189:
	.word	.LANCHOR0
	.word	.LANCHOR0+1220
	.fnend
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	1
	.global	FlashWaitCmdDone
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashWaitCmdDone, %function
FlashWaitCmdDone:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r7, r0
	ldr	r5, .L198
	add	r4, r5, r0, lsl #4
	ldr	r3, [r4, #2112]
	cbz	r3, .L193
	ldrb	r6, [r4, #2104]	@ zero_extendqisi2
	add	r5, r5, r7, lsl #2
	mov	r0, r6
	bl	NandcFlashCs
	ldr	r2, [r5, #1180]
	mov	r0, r6
	ldr	r1, [r4, #2108]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r1, r0
	mov	r0, r6
	bl	NandcFlashDeCs
	ldr	r3, [r4, #2112]
	sbfx	r1, r1, #0, #1
	str	r1, [r3]
	movs	r3, #0
	ldr	r2, [r4, #2116]
	str	r3, [r4, #2112]
	cbz	r2, .L193
	str	r1, [r2]
	str	r3, [r4, #2116]
.L193:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L199:
	.align	2
.L198:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	1
	.global	NandcDelayns
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcDelayns, %function
NandcDelayns:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	ndelay
	movs	r0, #0
	pop	{r3, pc}
	.fnend
	.size	NandcDelayns, .-NandcDelayns
	.align	1
	.global	NandcWaitFlashReadyNoDelay
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcWaitFlashReadyNoDelay, %function
NandcWaitFlashReadyNoDelay:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L206
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r4, .L206+4
	ldr	r5, [r3, r0, lsl #3]
.L203:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #22
	bmi	.L204
	movs	r0, #10
	bl	ndelay
	subs	r4, r4, #1
	bne	.L203
	mov	r0, #-1
.L201:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L204:
	movs	r0, #0
	b	.L201
.L207:
	.align	2
.L206:
	.word	.LANCHOR0
	.word	100000
	.fnend
	.size	NandcWaitFlashReadyNoDelay, .-NandcWaitFlashReadyNoDelay
	.align	1
	.global	NandcWaitFlashReady
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r3, .L213
	ldr	r4, .L213+4
	ldr	r5, [r3, r0, lsl #3]
	movs	r0, #130
	bl	ndelay
.L210:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #22
	bmi	.L211
	movs	r1, #2
	movs	r0, #1
	bl	usleep_range
	subs	r4, r4, #1
	bne	.L210
	mov	r0, #-1
.L208:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L211:
	movs	r0, #0
	b	.L208
.L214:
	.align	2
.L213:
	.word	.LANCHOR0
	.word	100000
	.fnend
	.size	NandcWaitFlashReady, .-NandcWaitFlashReady
	.align	1
	.global	FlashReset
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReset, %function
FlashReset:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L216
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r5, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	ldrb	r6, [r3, #4]	@ zero_extendqisi2
	bl	NandcFlashCs
	movs	r3, #255
	mov	r0, r4
	add	r5, r5, r6, lsl #8
	str	r3, [r5, #2056]
	bl	NandcWaitFlashReady
	mov	r0, r4
	pop	{r4, r5, r6, lr}
	b	NandcFlashDeCs
.L217:
	.align	2
.L216:
	.word	.LANCHOR0
	.fnend
	.size	FlashReset, .-FlashReset
	.align	1
	.global	flash_enter_slc_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	flash_enter_slc_mode, %function
flash_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r0
	ldr	r5, .L224
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cbz	r3, .L218
	bl	NandcFlashCs
	add	r3, r5, r6, lsl #3
	ldr	r7, [r5, r6, lsl #3]
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	ldrb	r3, [r3, #2072]	@ zero_extendqisi2
	cmp	r3, #44
	lsl	r8, r8, #8
	bne	.L220
	add	r4, r7, r8
	movs	r3, #239
	str	r3, [r4, #2056]
	movs	r3, #145
	str	r3, [r4, #2052]
	movs	r0, #50
	bl	ndelay
	movs	r3, #0
	movs	r2, #1
	str	r3, [r4, #2048]
	movs	r0, #100
	str	r2, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	ndelay
.L220:
	mov	r0, r6
	add	r7, r7, r8
	bl	NandcWaitFlashReadyNoDelay
	movs	r3, #218
	mov	r0, r6
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	movs	r3, #2
	strb	r3, [r5, #2232]
.L218:
	pop	{r4, r5, r6, r7, r8, pc}
.L225:
	.align	2
.L224:
	.word	.LANCHOR0
	.fnend
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	1
	.global	flash_exit_slc_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r0
	ldr	r5, .L232
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cbz	r3, .L226
	bl	NandcFlashCs
	add	r3, r5, r6, lsl #3
	ldr	r7, [r5, r6, lsl #3]
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	ldrb	r3, [r3, #2072]	@ zero_extendqisi2
	cmp	r3, #44
	lsl	r8, r8, #8
	bne	.L228
	add	r4, r7, r8
	movs	r3, #239
	str	r3, [r4, #2056]
	movs	r3, #145
	str	r3, [r4, #2052]
	movs	r0, #50
	bl	ndelay
	movs	r3, #2
	movs	r0, #100
	str	r3, [r4, #2048]
	movs	r3, #1
	str	r3, [r4, #2048]
	movs	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	ndelay
.L228:
	mov	r0, r6
	add	r7, r7, r8
	bl	NandcWaitFlashReadyNoDelay
	movs	r3, #223
	mov	r0, r6
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	movs	r3, #0
	strb	r3, [r5, #2232]
.L226:
	pop	{r4, r5, r6, r7, r8, pc}
.L233:
	.align	2
.L232:
	.word	.LANCHOR0
	.fnend
	.size	flash_exit_slc_mode, .-flash_exit_slc_mode
	.align	1
	.global	FlashEraseBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	mov	r1, r0
	mov	r0, r4
	bl	NandcFlashDeCs
	and	r0, r1, #1
	pop	{r4, r5, r6, pc}
	.fnend
	.size	FlashEraseBlock, .-FlashEraseBlock
	.align	1
	.global	FlashSetInterfaceMode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashSetInterfaceMode, %function
FlashSetInterfaceMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r5, #0
	ldr	r6, .L263
	mov	ip, #128
	mov	lr, #1
	mov	r9, #32
	mov	r10, #5
	ldrb	r3, [r6, #2233]	@ zero_extendqisi2
	addw	r8, r6, #2072
	and	r2, r3, #4
	and	r3, r3, #1
	str	r2, [sp, #4]
	mov	r2, r5
	str	r3, [sp]
.L245:
	ldrb	r4, [r5, r8]	@ zero_extendqisi2
	cmp	r4, #152
	beq	.L236
	cmp	r4, #69
	beq	.L236
	cmp	r4, #173
	beq	.L236
	cmp	r4, #44
	bne	.L237
.L236:
	cmp	r0, #1
	add	r3, r6, r5
	ldr	r1, [r6, r5]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	bne	.L238
	ldr	r7, [sp]
	cbz	r7, .L237
	lsls	r3, r3, #8
	cmp	r4, #173
	mov	r7, #239
	add	fp, r1, r3
	str	r7, [fp, #2056]
	bne	.L239
	str	r0, [fp, #2052]
.L262:
	str	r2, [fp, #2048]
	b	.L243
.L239:
	cmp	r4, #44
	itete	eq
	streq	r0, [fp, #2052]
	strne	ip, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
.L243:
	add	r3, r3, r1
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L237:
	adds	r5, r5, #8
	cmp	r5, #32
	bne	.L245
	movs	r0, #0
	bl	NandcWaitFlashReady
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L238:
	ldr	r7, [sp, #4]
	cmp	r7, #0
	beq	.L237
	lsls	r3, r3, #8
	cmp	r4, #173
	mov	r7, #239
	add	fp, r1, r3
	str	r7, [fp, #2056]
	bne	.L242
	str	lr, [fp, #2052]
	str	r9, [fp, #2048]
	b	.L243
.L242:
	cmp	r4, #44
	bne	.L244
	movs	r4, #35
	str	lr, [fp, #2052]
	str	r4, [fp, #2048]
	b	.L243
.L244:
	str	ip, [fp, #2052]
	b	.L262
.L264:
	.align	2
.L263:
	.word	.LANCHOR0
	.fnend
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	1
	.global	FlashReadSpare
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadSpare, %function
FlashReadSpare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r7, r2
	ldr	r5, .L266
	ldr	r3, .L266+4
	ldr	r4, [r5, r0, lsl #3]
	add	r5, r5, r0, lsl #3
	ldrb	r3, [r3, #477]	@ zero_extendqisi2
	ldrb	r2, [r5, #4]	@ zero_extendqisi2
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
	strb	r3, [r7]
	pop	{r3, r4, r5, r6, r7, pc}
.L267:
	.align	2
.L266:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.fnend
	.size	FlashReadSpare, .-FlashReadSpare
	.align	1
	.global	SandiskProgTestBadBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	SandiskProgTestBadBlock, %function
SandiskProgTestBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L269
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r4, r4, r3, lsl #8
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
	movs	r3, #16
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	movs	r3, #112
	movs	r0, #80
	str	r3, [r4, #2056]
	bl	ndelay
	ldr	r0, [r4, #2048]
	and	r0, r0, #1
	pop	{r4, pc}
.L270:
	.align	2
.L269:
	.word	.LANCHOR0
	.fnend
	.size	SandiskProgTestBadBlock, .-SandiskProgTestBadBlock
	.align	1
	.global	SandiskSetRRPara
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	SandiskSetRRPara, %function
SandiskSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movs	r3, #239
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	str	r3, [r0, #8]
	movs	r3, #17
	mov	r5, r0
	mov	r4, r1
	str	r3, [r0, #4]
	movs	r0, #200
	bl	ndelay
	ldr	r0, .L277
	add	r4, r4, r4, lsl #2
	ldr	r1, .L277+4
	movs	r2, #0
	sub	r6, r0, #45
.L272:
	ldrb	r3, [r1, #85]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L275
	movs	r0, #0
	pop	{r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L275:
	ldrb	r3, [r1, #84]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	ite	eq
	addeq	r3, r3, r6
	addne	r3, r3, r0
	ldrsb	r3, [r3, #5]
	adds	r2, r2, #1
	str	r3, [r5]
	b	.L272
.L278:
	.align	2
.L277:
	.word	.LANCHOR1+301
	.word	.LANCHOR0
	.fnend
	.size	SandiskSetRRPara, .-SandiskSetRRPara
	.align	1
	.global	micron_auto_read_calibration_config
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	micron_auto_read_calibration_config, %function
micron_auto_read_calibration_config:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	mov	r6, r1
	bl	NandcWaitFlashReady
	ldr	r0, .L280
	ldr	r4, [r0, r5, lsl #3]
	add	r0, r0, r5, lsl #3
	ldrb	r3, [r0, #4]	@ zero_extendqisi2
	movs	r0, #200
	add	r4, r4, r3, lsl #8
	movs	r3, #239
	str	r3, [r4, #2056]
	movs	r3, #150
	str	r3, [r4, #2052]
	bl	ndelay
	movs	r3, #0
	str	r6, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	pop	{r4, r5, r6, pc}
.L281:
	.align	2
.L280:
	.word	.LANCHOR0
	.fnend
	.size	micron_auto_read_calibration_config, .-micron_auto_read_calibration_config
	.align	1
	.global	FlashEraseSLc2KBlocks
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashEraseSLc2KBlocks, %function
FlashEraseSLc2KBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #16
	movs	r5, #0
	ldr	r8, .L292
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
	ldr	r10, .L292+4
.L283:
	cmp	r7, r9
	bne	.L288
	movs	r0, #0
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L288:
	sub	r3, r9, r7
	add	r2, sp, #8
	uxtb	r3, r3
	movs	r1, #0
	adds	r0, r6, r5
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2234]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r2, r3
	bhi	.L284
	mov	r3, #-1
	str	r3, [r6, r5]
.L285:
	adds	r7, r7, #1
	adds	r5, r5, #36
	b	.L283
.L284:
	add	r2, r8, r3
	add	r3, r8, r3, lsl #4
	ldrb	r4, [r2, #2236]	@ zero_extendqisi2
	strb	r4, [r3, #2104]
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
	mov	r0, r4
	ldr	r1, [sp, #8]
	bl	FlashReadStatus
	sbfx	r0, r0, #0, #1
	ldr	r1, [sp, #8]
	str	r0, [r6, r5]
	movs	r2, #0
	ldr	r3, [r8, #40]
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
	bne	.L287
	ldr	r1, [sp, #8]
	mov	r0, r10
	bl	printk
.L287:
	mov	r0, r4
	bl	NandcFlashDeCs
	b	.L285
.L293:
	.align	2
.L292:
	.word	.LANCHOR0
	.word	.LC1
	.fnend
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	1
	.global	FlashEraseBlocks
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	ldr	r4, .L325
	.pad #20
	sub	sp, sp, #20
	mov	r10, r1
	mov	r8, r2
	ldrb	r5, [r4, #36]	@ zero_extendqisi2
	cbz	r5, .L296
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
.L294:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L305:
	movs	r3, #36
	add	r2, sp, #8
	mul	r6, r3, r5
	sub	r3, r8, r5
	uxtb	r3, r3
	movs	r1, #0
	str	r3, [sp]
	add	r3, sp, #12
	add	fp, r9, r6
	mov	r0, fp
	bl	LogAddr2PhyAddr
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #12]
	cmp	r3, r0
	bhi	.L298
	mov	r3, #-1
	str	r3, [r9, r6]
.L299:
	adds	r5, r5, #1
.L296:
	cmp	r5, r8
	bcc	.L305
	ldr	r6, .L325+4
	movs	r5, #0
.L306:
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L308
	ldr	r3, [r4, #2248]
	cmp	r3, #0
	bne	.L309
.L310:
	movs	r0, #0
	b	.L294
.L298:
	ldrb	r3, [r4, #2244]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, lsl #4
	it	eq
	moveq	r7, #0
	ldr	r3, [r3, #2112]
	cbz	r3, .L301
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L301:
	ldr	r2, [sp, #12]
	movs	r0, #0
	lsls	r3, r2, #4
	adds	r1, r4, r3
	str	r0, [r1, #2116]
	ldr	r0, [sp, #8]
	str	fp, [r1, #2112]
	str	r0, [r1, #2108]
	cbz	r7, .L302
	adds	r6, r6, #36
	add	r6, r6, r9
	str	r6, [r1, #2116]
.L302:
	add	r2, r2, r4
	add	r3, r3, r4
	ldrb	r6, [r2, #2236]	@ zero_extendqisi2
	mov	r0, r6
	strb	r6, [r3, #2104]
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L303
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cbz	r3, .L303
	bl	flash_enter_slc_mode
.L304:
	ldr	r3, [sp, #12]
	mov	r0, r6
	ldr	r1, [sp, #8]
	add	r5, r5, r7
	add	r3, r4, r3, lsl #2
	ldr	r2, [r3, #1180]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r2, r7
	ldr	r1, [sp, #8]
	mov	r0, r6
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	b	.L299
.L303:
	bl	flash_exit_slc_mode
	b	.L304
.L308:
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L307
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cbz	r3, .L307
	lsls	r3, r5, #4
	ldrb	r0, [r6, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L307:
	adds	r5, r5, #1
	b	.L306
.L309:
	ldrb	r3, [r4, #2072]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L310
	movs	r3, #0
	movs	r2, #36
	mov	r1, r3
.L311:
	cmp	r3, r8
	beq	.L310
	mul	r0, r2, r3
	adds	r3, r3, #1
	str	r1, [r9, r0]
	b	.L311
.L326:
	.align	2
.L325:
	.word	.LANCHOR0
	.word	.LANCHOR0+2104
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	1
	.global	FlashReadDpCmd
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r6, r0
	ldr	r0, .L332
	mov	r7, r1
	uxtb	r9, r2
	lsr	r8, r2, #8
	lsrs	r5, r2, #16
	uxtb	lr, r7
	ldr	r2, [r0, #48]
	lsr	ip, r7, #8
	add	r1, r0, r6, lsl #3
	ldr	r3, [r0, r6, lsl #3]
	ldrb	r4, [r1, #4]	@ zero_extendqisi2
	ldrb	r1, [r0, #68]	@ zero_extendqisi2
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	cmp	r1, #1
	lsl	r4, r4, #8
	lsr	r1, r7, #16
	bne	.L328
	cmp	r2, #1
	itt	eq
	addeq	r2, r3, r4
	moveq	r10, #38
	add	r4, r4, r3
	it	eq
	streq	r10, [r2, #2056]
	ldrb	r3, [r0, #61]	@ zero_extendqisi2
	mov	r10, #0
	ldrb	r2, [r0, #60]	@ zero_extendqisi2
	mov	r0, r6
	str	r2, [r4, #2056]
	str	r10, [r4, #2052]
	str	r10, [r4, #2052]
	str	lr, [r4, #2052]
	str	ip, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	movs	r3, #48
	str	r10, [r4, #2056]
	str	r10, [r4, #2052]
	str	r10, [r4, #2052]
	str	r9, [r4, #2052]
	str	r8, [r4, #2052]
	str	r5, [r4, #2052]
	str	r3, [r4, #2056]
.L330:
	mov	r1, r7
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L328:
	cmp	r2, #1
	ittt	eq
	addeq	r2, r3, r4
	moveq	r10, #38
	streq	r10, [r2, #2056]
	add	r3, r3, r4
	ldrb	r2, [r0, #60]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r2, [r0, #61]	@ zero_extendqisi2
	str	lr, [r3, #2052]
	str	ip, [r3, #2052]
	str	r1, [r3, #2052]
	str	r2, [r3, #2056]
	movs	r2, #48
	str	r9, [r3, #2052]
	str	r8, [r3, #2052]
	str	r5, [r3, #2052]
	str	r2, [r3, #2056]
	b	.L330
.L333:
	.align	2
.L332:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	1
	.global	ftl_flash_de_init
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_flash_de_init, %function
ftl_flash_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movs	r0, #0
	ldr	r4, .L344
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r0, [r4, #2252]
	cbz	r0, .L335
	movs	r0, #0
	bl	flash_enter_slc_mode
.L336:
	ldrb	r3, [r4, #2256]	@ zero_extendqisi2
	cbz	r3, .L337
	ldrb	r3, [r4, #2233]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L337
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	movs	r3, #0
	strb	r3, [r4, #2256]
.L337:
	ldr	r3, [r4]
	movs	r0, #0
	str	r0, [r3, #336]
	pop	{r4, pc}
.L335:
	bl	flash_exit_slc_mode
	b	.L336
.L345:
	.align	2
.L344:
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_de_init, .-ftl_flash_de_init
	.align	1
	.global	NandcRandmzSel
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcRandmzSel, %function
NandcRandmzSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L347
	ldr	r3, [r3, r0, lsl #3]
	str	r1, [r3, #336]
	bx	lr
.L348:
	.align	2
.L347:
	.word	.LANCHOR0
	.fnend
	.size	NandcRandmzSel, .-NandcRandmzSel
	.global	__aeabi_idiv
	.align	1
	.global	NandcTimeCfg
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	ldr	r1, .L359
	bl	__aeabi_idiv
	ldr	r3, .L359+4
	cmp	r0, #250
	ldr	r3, [r3, #88]
	ble	.L350
	movw	r2, #8354
.L357:
	str	r2, [r3, #4]
	pop	{r4, pc}
.L350:
	cmp	r0, #220
	ble	.L352
.L358:
	movw	r2, #8322
	b	.L357
.L352:
	cmp	r0, #185
	ble	.L353
	movw	r2, #4226
	b	.L357
.L353:
	cmp	r0, #160
	ble	.L354
	movw	r2, #4194
	b	.L357
.L354:
	cmp	r4, #35
	bhi	.L355
	movw	r2, #4193
	b	.L357
.L355:
	cmp	r4, #99
	bhi	.L358
	movw	r2, #4225
	b	.L357
.L360:
	.align	2
.L359:
	.word	1000000
	.word	.LANCHOR0
	.fnend
	.size	NandcTimeCfg, .-NandcTimeCfg
	.align	1
	.global	FlashTimingCfg
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashTimingCfg, %function
FlashTimingCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	sub	r3, r0, #4192
	subs	r3, r3, #1
	cmp	r3, #1
	bls	.L362
	sub	r3, r0, #4224
	subs	r3, r3, #1
	cmp	r3, #1
	bls	.L362
	movw	r3, #8322
	cmp	r0, r3
	bne	.L363
.L362:
	ldr	r3, .L364
	ldr	r3, [r3, #88]
	str	r0, [r3, #4]
.L363:
	ldr	r3, .L364+4
	ldrb	r0, [r3, #489]	@ zero_extendqisi2
	b	NandcTimeCfg
.L365:
	.align	2
.L364:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.fnend
	.size	FlashTimingCfg, .-FlashTimingCfg
	.align	1
	.global	NandcInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcInit, %function
NandcInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r2, #1
	ldr	r3, .L368
	movs	r1, #0
	movs	r5, #0
	str	r2, [r3, #12]
	movs	r2, #2
	str	r2, [r3, #20]
	movs	r2, #3
	stm	r3, {r0, r1}
	str	r0, [r3, #8]
	str	r0, [r3, #16]
	str	r0, [r3, #24]
	str	r0, [r3, #88]
	str	r2, [r3, #28]
	ldr	r2, [r0]
	and	r2, r2, #253952
	ubfx	r4, r2, #13, #1
	bfi	r2, r1, #13, #1
	ldr	r1, [r0, #352]
	orr	r2, r2, #256
	str	r4, [r3, #2260]
	movw	r4, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r3, #2264]
	ldr	r1, [r0, #352]
	cmp	r1, r4
	str	r1, [r3, #2268]
	mov	r4, r3
	itt	eq
	moveq	r3, #8
	streq	r3, [r4, #2264]
	str	r2, [r0]
	movs	r0, #40
	ldr	r3, [r4, #88]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #88]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L368+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r0, [r4, #2272]
	str	r0, [r4, #2276]
	add	r0, r0, #32768
	str	r0, [r4, #2280]
	str	r5, [r4, #2300]
	str	r5, [r4, #2308]
	pop	{r3, r4, r5, pc}
.L369:
	.align	2
.L368:
	.word	.LANCHOR0
	.word	1579009
	.fnend
	.size	NandcInit, .-NandcInit
	.align	1
	.global	NandcGetTimeCfg
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r4, .L371
	ldr	r5, [r4, #88]
	ldr	r5, [r5, #4]
	str	r5, [r0]
	ldr	r0, [r4, #88]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [r4, #88]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [r4, #88]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, lsl #16
	str	r2, [r3]
	pop	{r4, r5, pc}
.L372:
	.align	2
.L371:
	.word	.LANCHOR0
	.fnend
	.size	NandcGetTimeCfg, .-NandcGetTimeCfg
	.align	1
	.global	NandcBchSel
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcBchSel, %function
NandcBchSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L381
	movs	r1, #0
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #1
	ldr	r2, [r3, #88]
	str	r0, [r3, #2312]
	mov	r3, r1
	str	r4, [r2, #8]
	movs	r4, #16
	cmp	r0, r4
	bfi	r3, r4, #8, #8
	bfi	r3, r1, #18, #1
	bne	.L374
.L377:
	bfc	r3, #4, #1
.L375:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	pop	{r4, pc}
.L374:
	cmp	r0, #24
	bne	.L376
	orr	r3, r3, #16
	b	.L375
.L376:
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L375
	b	.L377
.L382:
	.align	2
.L381:
	.word	.LANCHOR0
	.fnend
	.size	NandcBchSel, .-NandcBchSel
	.align	1
	.global	FlashBchSel
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashBchSel, %function
FlashBchSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L384
	strb	r0, [r3, #2316]
	b	NandcBchSel
.L385:
	.align	2
.L384:
	.word	.LANCHOR0
	.fnend
	.size	FlashBchSel, .-FlashBchSel
	.align	1
	.global	ftl_flash_resume
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_flash_resume, %function
ftl_flash_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	ldr	r4, .L394
	ldr	r2, [r4, #88]
	addw	r6, r4, #2072
	ldr	r1, [r4, #92]
	str	r1, [r2]
	ldr	r1, [r4, #96]
	ldr	r2, [r4, #88]
	str	r1, [r2, #4]
	ldr	r1, [r4, #100]
	str	r1, [r2, #8]
	ldr	r1, [r4, #104]
	str	r1, [r2, #12]
	ldr	r1, [r4, #108]
	str	r1, [r2, #304]
	ldr	r1, [r4, #112]
	str	r1, [r2, #308]
	ldr	r1, [r4, #116]
	str	r1, [r2, #336]
	ldr	r1, [r4, #120]
	str	r1, [r2, #344]
.L388:
	ldrb	r3, [r6, r5, lsl #3]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L387
	uxtb	r0, r5
	bl	FlashReset
.L387:
	adds	r5, r5, #1
	cmp	r5, #4
	bne	.L388
	ldrb	r3, [r4, #2256]	@ zero_extendqisi2
	cbz	r3, .L389
	movs	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r4, #109]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L389:
	ldr	r3, [r4, #48]
	pop	{r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L395:
	.align	2
.L394:
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_resume, .-ftl_flash_resume
	.align	1
	.global	ftl_nandc_get_irq_status
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcSendDumpDataStart, %function
NandcSendDumpDataStart:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, [r0, #16]
	.pad #8
	sub	sp, sp, #8
	ldr	r3, .L401
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	bfc	r2, #2, #1
	str	r2, [sp, #4]
	ldr	r2, [sp, #4]
	str	r2, [r0, #16]
	str	r3, [r0, #8]
	orr	r3, r3, #4
	str	r3, [r0, #8]
	add	sp, sp, #8
	@ sp needed
	bx	lr
.L402:
	.align	2
.L401:
	.word	538969130
	.fnend
	.size	NandcSendDumpDataStart, .-NandcSendDumpDataStart
	.align	1
	.global	NandcSendDumpDataDone
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcSendDumpDataDone, %function
NandcSendDumpDataDone:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.pad #8
	sub	sp, sp, #8
.L404:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r3, r3, #11
	bpl	.L404
	add	sp, sp, #8
	@ sp needed
	bx	lr
	.fnend
	.size	NandcSendDumpDataDone, .-NandcSendDumpDataDone
	.align	1
	.global	NandcXferStart
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	str	r2, [sp, #4]
	ldr	fp, [sp, #56]
	cmp	r1, #0
	bne	.L421
	adds	r1, fp, #0
	it	ne
	movne	r1, #1
.L408:
	ldr	r4, .L426
	mov	ip, #16
	movs	r5, #0
	ldr	r7, [r4, r0, lsl #3]
	add	r0, r4, r0, lsl #3
	ldr	r8, [r7, #12]
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	bfi	r8, ip, #8, #8
	bfi	r8, r5, #3, #1
	bfi	r5, r6, #1, #1
	bfi	r8, r0, #5, #3
	orr	r5, r5, #8
	movs	r0, #1
	bfi	r5, r0, #5, #2
	lsrs	r3, r3, r0
	orr	r5, r5, #536870912
	orr	r5, r5, #1024
	bfi	r5, r3, #4, #1
	ldr	r3, [r4, #2264]
	cmp	r3, #3
	bls	.L409
	ldr	r3, [r7, #16]
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	cmp	r1, #0
	beq	.L410
	cmp	r6, #0
	bne	.L411
.L419:
	ldr	r2, [sp, #4]
	adds	r2, r2, #1
	asrs	r2, r2, #1
	bfi	r5, r2, #22, #6
	cmp	fp, #0
	beq	.L412
	mov	r0, fp
.L413:
	ldr	r3, [r4, #2280]
	ubfx	r9, r5, #22, #5
	mov	r2, r6
	lsl	r1, r9, #10
	str	r0, [r4, #2284]
	str	r3, [r4, #2288]
	bl	rknand_dma_map_single
	mov	r2, r6
	str	r0, [r4, #2292]
	lsl	r1, r9, #7
	ldr	r0, [r4, #2288]
	clz	r6, r6
	bl	rknand_dma_map_single
	movs	r3, #1
	str	r0, [r4, #2296]
	str	r3, [r4, #2300]
	movs	r2, #16
	ldr	r3, [r4, #2292]
	tst	fp, #3
	lsr	r6, r6, #5
	str	r3, [r7, #20]
	ldr	r3, [r4, #2296]
	str	r3, [r7, #24]
	mov	r3, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfi	r3, r2, #9, #5
	it	eq
	moveq	r2, #2
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #448
	str	r3, [sp, #12]
	ittt	eq
	ldreq	r3, [sp, #12]
	bfieq	r3, r2, #3, #3
	streq	r3, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #4
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfi	r3, r6, #1, #1
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #1
	str	r3, [sp, #12]
.L410:
	ldr	r3, [sp, #12]
	str	r3, [r7, #16]
.L409:
	str	r8, [r7, #12]
	str	r5, [r7, #8]
	orr	r5, r5, #4
	str	r5, [r7, #8]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L421:
	movs	r1, #1
	b	.L408
.L411:
	ldr	r3, [r4, #2312]
	cmp	r3, #25
	ldr	r3, [sp, #4]
	ite	cc
	movcc	r10, #64
	movcs	r10, #128
	lsr	r9, r3, #1
	ldr	r3, [sp, #60]
	str	r3, [sp]
	movs	r3, #0
	mov	r0, r3
.L415:
	cmp	r0, r9
	bcs	.L419
	ldr	r1, [sp, #60]
	bic	lr, r3, #3
	cbz	r1, .L416
	ldr	r2, [sp]
	ldr	r1, [r2], #4	@ unaligned
	str	r2, [sp]
	mov	r2, r1
	ldr	r1, [r4, #2280]
	mov	ip, r1
	str	r2, [ip, lr]
.L417:
	adds	r0, r0, #1
	add	r3, r3, r10
	b	.L415
.L416:
	ldr	r1, [r4, #2280]
	mov	r2, #-1
	str	r2, [r1, lr]
	b	.L417
.L412:
	ldr	r0, [r4, #2276]
	b	.L413
.L427:
	.align	2
.L426:
	.word	.LANCHOR0
	.fnend
	.size	NandcXferStart, .-NandcXferStart
	.align	1
	.global	Ftl_log2
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r1, #0
	movs	r2, #1
.L429:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L430
	subs	r0, r3, #1
	uxth	r0, r0
	bx	lr
.L430:
	lsls	r2, r2, #1
	b	.L429
	.fnend
	.size	Ftl_log2, .-Ftl_log2
	.align	1
	.global	FtlPrintInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L433
	cmp	r0, #24
	it	cc
	movcc	r0, #24
	ldrh	r2, [r3, #2324]
	ldrh	r1, [r3, #2334]
	str	r0, [r3, #2320]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #2340]
	strh	r0, [r3, #2332]	@ movhi
	movs	r0, #0
	str	r2, [r3, #2328]
	subs	r2, r1, r2
	str	r2, [r3, #2336]
	bx	lr
.L434:
	.align	2
.L433:
	.word	.LANCHOR0
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	1
	.global	FtlConstantsInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	ldr	r4, .L462
	str	r1, [sp]
	ldrh	r1, [sp]
	ldrh	ip, [r0, #8]
	ldrh	r2, [r0, #10]
	ldrh	r3, [r0, #12]
	addw	r0, r4, #2350
	strh	r1, [r4, #2334]	@ movhi
	movs	r1, #0
	strh	ip, [r4, #2344]	@ movhi
	strh	r2, [r4, #2346]	@ movhi
	strh	r3, [r4, #2348]	@ movhi
.L436:
	strb	r1, [r1, r0]
	adds	r1, r1, #1
	cmp	r1, #32
	bne	.L436
	ldrh	r0, [r5, #14]
	ldrh	r1, [r5, #20]
	cmp	r1, r0, lsr #8
	bcs	.L437
	uxtb	r10, r3
	ldr	r9, .L462+4
	lsl	r1, r10, #1
	uxtb	r1, r1
	str	r1, [sp, #4]
	subs	r1, r2, #1
	muls	r1, r3, r1
	str	r1, [sp, #12]
	movs	r1, #0
.L438:
	cmp	r1, r3
	bcs	.L440
	ldr	r6, [sp, #12]
	sub	lr, r1, r3
	uxtb	r0, r1
	add	lr, lr, r9
	adds	r6, r1, r6
	add	r6, r9, r6
	str	r6, [sp, #8]
	movs	r6, #0
	mov	r8, r6
	b	.L441
.L439:
	ldr	r7, [sp, #8]
	add	fp, r10, r0
	strb	r0, [lr, r6]
	add	r8, r8, #1
	strb	fp, [r7, r6]
	ldr	r7, [sp, #4]
	add	r0, r0, r7
	uxtb	r0, r0
.L441:
	cmp	r8, r2
	add	r6, r6, r3
	bcc	.L439
	adds	r1, r1, #1
	b	.L438
.L440:
	lsls	r2, r2, #1
	strh	r2, [r4, #2346]	@ movhi
	ldr	r2, [sp]
	lsrs	r7, r2, #1
	strh	r7, [r4, #2334]	@ movhi
.L437:
	movs	r2, #5
	ldrb	r9, [r4, #36]	@ zero_extendqisi2
	strh	r2, [r4, #2382]	@ movhi
	movs	r2, #0
	strh	r2, [r4, #2384]	@ movhi
	cmp	ip, #1
	mov	r2, #4352
	it	eq
	strheq	ip, [r4, #2382]	@ movhi
	strh	r2, [r4, #2386]	@ movhi
	cmp	r9, #0
	beq	.L443
	mov	r2, #384
	strh	r2, [r4, #2386]	@ movhi
.L443:
	ldrh	r7, [r4, #2346]
	ldrh	r6, [r4, #2334]
	ldrh	r8, [r5, #16]
	ldrh	fp, [r5, #20]
	smulbb	r7, r7, r3
	ldrh	r1, [r5, #18]
	smulbb	r3, r3, r6
	strh	r8, [r4, #2390]	@ movhi
	mov	r0, fp
	strh	fp, [r4, #2396]	@ movhi
	uxth	r7, r7
	strh	r1, [r4, #2392]	@ movhi
	strh	r3, [r4, #2388]	@ movhi
	smulbb	r3, r7, r8
	str	r1, [sp]
	strh	r7, [r4, #2324]	@ movhi
	strh	r3, [r4, #2394]	@ movhi
	bl	Ftl_log2
	lsl	r3, fp, #9
	cmp	r6, #1024
	strh	r0, [r4, #2398]	@ movhi
	mov	r10, r0
	uxth	r3, r3
	ldr	r1, [sp]
	ldrh	r0, [r4, #2386]
	strh	r3, [r4, #2400]	@ movhi
	lsr	r3, r3, #8
	strh	r3, [r4, #2402]	@ movhi
	mul	r1, fp, r1
	ldrh	r3, [r5, #26]
	lsl	r0, r0, #3
	strh	r3, [r4, #2404]	@ movhi
	mul	r3, r6, r7
	str	r3, [r4, #2340]
	itt	hi
	uxtbhi	r3, r6
	strhhi	r3, [r4, #2384]	@ movhi
	ldrh	r3, [r4, #2384]
	subs	r3, r6, r3
	muls	r3, r7, r3
	mul	r3, fp, r3
	mul	r8, r8, r3
	asr	r3, r8, #11
	str	r3, [r4, #2408]
	bl	__aeabi_idiv
	uxth	r0, r0
	cmp	r0, #4
	itet	ls
	movls	r3, #4
	strhhi	r0, [r4, #2412]	@ movhi
	strhls	r3, [r4, #2412]	@ movhi
	cmp	r9, #0
	beq	.L447
	mov	r3, #640
	strh	r3, [r4, #2386]	@ movhi
.L447:
	ldrh	r3, [r4, #2386]
	lsls	r6, r6, #6
	mov	r1, r7
	ldrh	r0, [r4, #2412]
	asr	r3, r3, r10
	add	r10, r10, #9
	asr	r6, r6, r10
	adds	r3, r3, #2
	strh	r6, [r4, #2416]	@ movhi
	uxth	r6, r6
	strh	r3, [r4, #2414]	@ movhi
	mul	r3, r6, r7
	adds	r6, r6, #8
	str	r3, [r4, #2420]
	bl	__aeabi_uidiv
	uxtah	r0, r6, r0
	cmp	r7, #1
	it	eq
	addeq	r0, r0, #4
	str	r0, [r4, #2320]
	ldrh	r0, [r4, #2320]
	bl	FtlSysBlkNumInit
	ldr	r5, [r4, #2336]
	mov	r0, #2048
	ldr	r3, [r4, #2320]
	ldrh	r6, [r4, #2396]
	str	r3, [r4, #2424]
	lsls	r3, r5, #2
	ldrh	r5, [r4, #2390]
	mov	r1, r6
	muls	r5, r3, r5
	ldrh	r3, [r4, #2398]
	adds	r3, r3, #9
	lsrs	r5, r5, r3
	adds	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, #2428]	@ movhi
	bl	__aeabi_idiv
	ldrh	r2, [r4, #2412]
	movs	r3, #0
	str	r3, [r4, #2432]
	strh	r0, [r4, #2430]	@ movhi
	adds	r3, r2, #3
	ldrb	r0, [r4, #152]	@ zero_extendqisi2
	strh	r3, [r4, #2412]	@ movhi
	ldr	r3, [r4, #2420]
	adds	r1, r3, #3
	str	r1, [r4, #2420]
	cbz	r0, .L450
	adds	r3, r3, #5
	adds	r2, r2, #4
	strh	r2, [r4, #2412]	@ movhi
.L461:
	str	r3, [r4, #2420]
.L451:
	ldrh	r2, [r4, #2332]
	movs	r3, #0
	strh	r3, [r4, #2436]	@ movhi
	movs	r0, #0
	lsrs	r3, r2, #3
	add	r3, r3, r2, lsl #1
	adds	r3, r3, #52
	add	r5, r3, r5, lsl #2
	cmp	r5, r6, lsl #9
	itt	cc
	movcc	r3, #1
	strhcc	r3, [r4, #2436]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L450:
	cmp	r1, #7
	bhi	.L451
	movs	r3, #8
	b	.L461
.L463:
	.align	2
.L462:
	.word	.LANCHOR0
	.word	.LANCHOR0+2350
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	1
	.global	IsBlkInVendorPart
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L471
	ldrh	r3, [r2, #2438]
	cbz	r3, .L470
	ldr	r3, [r2, #2440]
	ldrh	r2, [r2, #2412]
	add	r2, r3, r2, lsl #1
.L466:
	cmp	r3, r2
	bne	.L467
.L470:
	movs	r0, #0
	bx	lr
.L467:
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L466
	movs	r0, #1
	bx	lr
.L472:
	.align	2
.L471:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	1
	.global	FtlCacheMetchLpa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L481
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r2, #2444]
	cbz	r3, .L480
	ldr	r5, [r2, #2448]
	movs	r6, #36
	movs	r2, #0
.L476:
	mla	r4, r6, r2, r5
	ldr	r4, [r4, #16]
	cmp	r4, r0
	bcc	.L475
	cmp	r4, r1
	bls	.L478
.L475:
	adds	r2, r2, #1
	cmp	r3, r2
	bne	.L476
.L480:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L478:
	movs	r0, #1
	pop	{r4, r5, r6, pc}
.L482:
	.align	2
.L481:
	.word	.LANCHOR0
	.fnend
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	1
	.global	FtlGetCap
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGetCap, %function
FtlGetCap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L484
	ldr	r0, [r3, #2432]
	bx	lr
.L485:
	.align	2
.L484:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCap, .-FtlGetCap
	.align	1
	.global	FtlGetCapacity
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L487
	ldr	r0, [r3, #2432]
	bx	lr
.L488:
	.align	2
.L487:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	1
	.global	ftl_get_density
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_get_density, %function
ftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L490
	ldr	r0, [r3, #2432]
	bx	lr
.L491:
	.align	2
.L490:
	.word	.LANCHOR0
	.fnend
	.size	ftl_get_density, .-ftl_get_density
	.align	1
	.global	FtlGetLpn
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGetLpn, %function
FtlGetLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L493
	ldr	r0, [r3, #2452]
	bx	lr
.L494:
	.align	2
.L493:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLpn, .-FtlGetLpn
	.align	1
	.global	FtlBbmMapBadBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r5, r0
	ldr	r4, .L496
	ldrh	r7, [r4, #2388]
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	ldr	r2, [r2, #2484]
	lsrs	r1, r3, #5
	and	r7, r3, #31
	movs	r0, #1
	lsls	r0, r0, r7
	ldr	r7, [r2, r1, lsl #2]
	orrs	r0, r0, r7
	str	r0, [r2, r1, lsl #2]
	mov	r2, r6
	str	r0, [sp]
	mov	r1, r5
	ldr	r0, .L496+4
	bl	printk
	ldrh	r3, [r4, #2462]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r4, #2462]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L497:
	.align	2
.L496:
	.word	.LANCHOR0
	.word	.LC2
	.fnend
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	1
	.global	FtlBbmIsBadBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r7, r0
	ldr	r5, .L499
	ldrh	r6, [r5, #2388]
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r7
	uxth	r4, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsrs	r2, r4, #5
	add	r5, r5, r0, lsl #2
	and	r4, r4, #31
	ldr	r3, [r5, #2484]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L500:
	.align	2
.L499:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	1
	.global	FtlBbtInfoPrint
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L509
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, #2388]
	ldrh	r6, [r3, #2346]
	muls	r6, r2, r6
.L503:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L505
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L505:
	bl	FtlBbmIsBadBlock
	cbz	r0, .L504
	adds	r4, r4, #1
	uxth	r4, r4
.L504:
	adds	r5, r5, #1
	b	.L503
.L510:
	.align	2
.L509:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	1
	.global	V2P_block
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	V2P_block, %function
V2P_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r1
	ldr	r4, .L512
	mov	r7, r0
	ldrh	r6, [r4, #2348]
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r4, [r4, #2388]
	smulbb	r5, r6, r5
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
	adds	r0, r5, r1
	add	r0, r0, r4
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.L513:
	.align	2
.L512:
	.word	.LANCHOR0
	.fnend
	.size	V2P_block, .-V2P_block
	.align	1
	.global	P2V_plane
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L515
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r3, #2348]
	ldrh	r1, [r3, #2388]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r1, r4
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L516:
	.align	2
.L515:
	.word	.LANCHOR0
	.fnend
	.size	P2V_plane, .-P2V_plane
	.align	1
	.global	P2V_block_in_plane
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L518
	ldrh	r1, [r4, #2388]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #2348]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L519:
	.align	2
.L518:
	.word	.LANCHOR0
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	1
	.global	ftl_cmp_data_ver
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, r1
	bls	.L521
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L521:
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L524
	ldrh	r0, [r3, #2522]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L525:
	.align	2
.L524:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	1
	.global	FtlFreeSysBlkQueueFull
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L527
	ldrh	r0, [r3, #2522]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L528:
	.align	2
.L527:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	1
	.global	FtlFreeSysBlkQueueIn
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	subs	r3, r0, #1
	uxth	r3, r3
	movw	r2, #65533
	mov	r6, r0
	cmp	r3, r2
	bhi	.L529
	ldr	r4, .L538
	ldrh	r3, [r4, #2522]
	cmp	r3, #1024
	beq	.L529
	cbz	r1, .L531
	ldr	r5, .L538+4
	ldr	r3, [r5, #228]
	cbnz	r3, .L531
	bl	P2V_block_in_plane
	mov	r7, r0
	ldr	r0, [r5, #232]
	lsls	r3, r6, #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r5, #236]
	ldrh	r3, [r2, r7, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	ldr	r3, [r5, #240]
	adds	r3, r3, #1
	str	r3, [r5, #240]
.L531:
	ldrh	r3, [r4, #2522]
	adds	r3, r3, #1
	strh	r3, [r4, #2522]	@ movhi
	ldrh	r3, [r4, #2520]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r6, [r2, #2524]	@ movhi
	strh	r3, [r4, #2520]	@ movhi
.L529:
	pop	{r3, r4, r5, r6, r7, pc}
.L539:
	.align	2
.L538:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	1
	.global	FtlFreeSysBLkSort
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L549
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r2, [r3, #2522]
	cbz	r2, .L540
	ldr	r2, .L549+4
	movs	r0, #0
	ldrh	r1, [r3, #2518]
	mov	r6, r0
	ldrh	r5, [r2, #272]
	ldrh	r2, [r3, #2520]
	and	r5, r5, #31
.L542:
	uxth	r4, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bgt	.L543
	cbz	r6, .L540
	strh	r1, [r3, #2518]	@ movhi
	strh	r2, [r3, #2520]	@ movhi
.L540:
	pop	{r4, r5, r6, pc}
.L543:
	add	r4, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r4, #2524]
	add	r4, r3, r2, lsl #1
	strh	r6, [r4, #2524]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L542
.L550:
	.align	2
.L549:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	1
	.global	FtlFreeSysBlkQueueOut
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	ldr	r4, .L561
	ldr	r5, .L561+4
	mov	r7, r4
.L552:
	ldrh	r1, [r4, #2522]
	cmp	r1, #0
	beq	.L553
	ldrh	r3, [r4, #2518]
	subs	r1, r1, #1
	ldr	r9, [r5, #228]
	strh	r1, [r4, #2522]	@ movhi
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r6, [r2, #2524]
	strh	r3, [r4, #2518]	@ movhi
	cmp	r9, #0
	bne	.L554
	mov	r0, r6
	bl	P2V_block_in_plane
	mov	r8, r0
	ldr	r0, [r5, #232]
	lsls	r3, r6, #10
	str	r3, [r0, #4]
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cbz	r3, .L555
	movs	r2, #1
	mov	r1, r9
	bl	FlashEraseBlocks
.L555:
	movs	r2, #1
	ldr	r0, [r5, #232]
	mov	r1, r2
	bl	FlashEraseBlocks
	ldr	r2, [r5, #236]
	ldrh	r3, [r2, r8, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r8, lsl #1]	@ movhi
	ldr	r3, [r5, #240]
	adds	r3, r3, #1
	str	r3, [r5, #240]
.L554:
	subs	r3, r6, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L557
	ldrh	r2, [r7, #2522]
	mov	r1, r6
	ldr	r0, .L561+8
	bl	printk
	b	.L552
.L553:
	ldr	r0, .L561+12
	bl	printk
.L556:
	b	.L556
.L557:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L562:
	.align	2
.L561:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC4
	.word	.LC3
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	1
	.global	test_node_in_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	test_node_in_list, %function
test_node_in_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L568
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	movw	r5, #65535
	ldr	r2, [r0]
	ldr	r4, [r3, #292]
	subs	r3, r2, r4
	asrs	r0, r3, #1
	ldr	r3, .L568+4
	muls	r3, r0, r3
	movs	r0, #6
	uxth	r3, r3
.L565:
	cmp	r3, r1
	beq	.L566
	ldrh	r3, [r2]
	cmp	r3, r5
	beq	.L567
	mla	r2, r0, r3, r4
	b	.L565
.L566:
	movs	r0, #1
	pop	{r4, r5, pc}
.L567:
	movs	r0, #0
	pop	{r4, r5, pc}
.L569:
	.align	2
.L568:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	test_node_in_list, .-test_node_in_list
	.align	1
	.global	insert_data_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L585
	ldrh	r3, [r5, #2332]
	cmp	r3, r0
	bls	.L572
	ldr	r2, .L585+4
	movs	r4, #6
	muls	r4, r0, r4
	movw	r3, #65535
	ldr	ip, [r2, #292]
	mov	r7, r2
	add	r1, ip, r4
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [ip, r4]	@ movhi
	ldr	r3, [r2, #296]
	cbnz	r3, .L573
	str	r1, [r2, #296]
.L572:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L573:
	ldrh	r6, [r1, #4]
	lsl	r10, r0, #1
	ldr	r8, [r2, #300]
	ldrh	r2, [r8, r0, lsl #1]
	cmp	r6, #0
	beq	.L583
	muls	r6, r2, r6
.L574:
	ldr	lr, [r7, #292]
	ldrh	r5, [r5, #2332]
	sub	r2, r3, lr
	asr	r9, r2, #1
	ldr	r2, .L585+8
	str	r5, [sp]
	mul	r2, r2, r9
	ldr	r9, [r7, #236]
	add	r5, r9, r10
	uxth	r2, r2
	str	r5, [sp, #4]
	movs	r5, #0
.L581:
	adds	r5, r5, #1
	ldr	r7, [sp]
	uxth	r5, r5
	cmp	r5, r7
	bhi	.L572
	cmp	r0, r2
	beq	.L572
	ldrh	r7, [r3, #4]
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	cbz	r7, .L584
	mul	r7, r7, fp
.L576:
	cmp	r6, r7
	bne	.L577
	ldr	r7, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L579
.L578:
	strh	r2, [ip, r4]	@ movhi
	ldr	r4, .L585+4
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, [r4, #296]
	cmp	r3, r2
	ittte	ne
	ldrhne	r5, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r4, #292]
	strheq	r0, [r3, #2]	@ movhi
	iteee	eq
	streq	r1, [r4, #296]
	mulne	r2, r2, r5
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L572
.L583:
	mov	r6, #-1
	b	.L574
.L584:
	mov	r7, #-1
	b	.L576
.L577:
	bcc	.L578
.L579:
	ldrh	r7, [r3]
	movw	r10, #65535
	cmp	r7, r10
	bne	.L580
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L585+4
	str	r1, [r3, #304]
	b	.L572
.L580:
	movs	r3, #6
	mov	r2, r7
	mla	r3, r3, r7, lr
	b	.L581
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	insert_data_list
	ldr	r2, .L588
	ldrh	r3, [r2, #308]
	adds	r3, r3, #1
	strh	r3, [r2, #308]	@ movhi
	pop	{r3, pc}
.L589:
	.align	2
.L588:
	.word	.LANCHOR2
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	1
	.global	insert_free_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	insert_free_list, %function
insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r4, #65535
	cmp	r0, r4
	beq	.L591
	ldr	r2, .L597
	movs	r1, #6
	mul	r7, r1, r0
	ldr	ip, [r2, #292]
	mov	r5, r2
	add	r6, ip, r7
	strh	r4, [r6, #2]	@ movhi
	strh	r4, [ip, r7]	@ movhi
	ldr	r3, [r2, #312]
	cbnz	r3, .L592
	str	r6, [r2, #312]
.L591:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L592:
	ldr	lr, [r2, #292]
	ldr	r8, [r2, #236]
	sub	r2, r3, lr
	asr	r10, r2, #1
	ldr	r2, .L597+4
	ldrh	r9, [r8, r0, lsl #1]
	mul	r2, r2, r10
	mov	r10, r4
	uxth	r2, r2
.L595:
	ldrh	r4, [r8, r2, lsl #1]
	cmp	r4, r9
	bcs	.L593
	ldrh	r4, [r3]
	cmp	r4, r10
	bne	.L594
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L591
.L594:
	mla	r3, r1, r4, lr
	mov	r2, r4
	b	.L595
.L593:
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	strh	r2, [ip, r7]	@ movhi
	ldr	r2, [r5, #312]
	cmp	r3, r2
	ittte	ne
	ldrhne	r4, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r5, #292]
	strheq	r0, [r3, #2]	@ movhi
	iteee	eq
	streq	r6, [r5, #312]
	mulne	r2, r2, r4
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L591
.L598:
	.align	2
.L597:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	insert_free_list, .-insert_free_list
	.align	1
	.global	INSERT_FREE_LIST
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	insert_free_list
	ldr	r2, .L600
	ldrh	r3, [r2, #316]
	adds	r3, r3, #1
	strh	r3, [r2, #316]	@ movhi
	pop	{r3, pc}
.L601:
	.align	2
.L600:
	.word	.LANCHOR2
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	1
	.global	List_remove_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movs	r4, #6
	ldr	r7, .L607
	muls	r1, r4, r1
	movw	r6, #65535
	ldr	r3, [r0]
	ldr	r2, [r7, #292]
	adds	r5, r2, r1
	cmp	r5, r3
	ldrh	r3, [r2, r1]
	bne	.L603
	cmp	r3, r6
	iteet	ne
	mlane	r3, r4, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	it	ne
	strhne	r6, [r3, #2]	@ movhi
.L605:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, pc}
.L603:
	cmp	r3, r6
	ldrh	r0, [r5, #2]
	bne	.L606
	cmp	r0, r3
	beq	.L605
	muls	r4, r0, r4
	strh	r3, [r2, r4]	@ movhi
	b	.L605
.L606:
	mla	r3, r4, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r3, [r5, #2]
	ldrh	r6, [r2, r1]
	ldr	r0, [r7, #292]
	muls	r3, r4, r3
	strh	r6, [r0, r3]	@ movhi
	b	.L605
.L608:
	.align	2
.L607:
	.word	.LANCHOR2
	.fnend
	.size	List_remove_node, .-List_remove_node
	.align	1
	.global	List_pop_index_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	movw	r5, #65535
	movs	r6, #6
	ldr	r2, [r2, #292]
.L611:
	cbnz	r1, .L612
.L614:
	ldr	r4, .L616+4
	subs	r3, r3, r2
	asrs	r3, r3, #1
	muls	r4, r3, r4
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, r5, r6, pc}
.L612:
	ldrh	r4, [r3]
	cmp	r4, r5
	beq	.L614
	subs	r1, r1, #1
	mla	r3, r6, r4, r2
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L624
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #296]
	cbz	r3, .L623
	ldr	r1, [r2, #292]
	movs	r4, #6
	movw	r2, #65535
.L620:
	cbz	r0, .L621
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L622
.L623:
	movw	r0, #65535
	pop	{r4, pc}
.L622:
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L620
.L621:
	ldr	r0, .L624+4
	subs	r3, r3, r1
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r0, r3
	pop	{r4, pc}
.L625:
	.align	2
.L624:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	1
	.global	List_update_data_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r0
	ldr	r4, .L633
	ldrh	r3, [r4, #320]
	cmp	r3, r0
	beq	.L627
	ldrh	r3, [r4, #368]
	cmp	r3, r0
	beq	.L627
	ldrh	r3, [r4, #416]
	cmp	r3, r0
	beq	.L627
	movs	r3, #6
	ldr	r1, [r4, #292]
	muls	r3, r0, r3
	ldr	r2, [r4, #296]
	adds	r0, r1, r3
	cmp	r0, r2
	beq	.L627
	ldrh	r2, [r0, #4]
	ldr	r7, [r4, #300]
	ldrh	r6, [r7, r5, lsl #1]
	cbz	r2, .L631
	muls	r2, r6, r2
.L628:
	ldrh	r6, [r0, #2]
	movw	r0, #65535
	cmp	r6, r0
	bne	.L629
	ldrh	r3, [r1, r3]
	cmp	r3, r6
	beq	.L627
.L629:
	movs	r0, #6
	ldr	r3, .L633+4
	muls	r0, r6, r0
	asrs	r6, r0, #1
	add	r1, r1, r0
	muls	r3, r6, r3
	ldrh	r6, [r7, r3, lsl #1]
	ldrh	r3, [r1, #4]
	cbz	r3, .L632
	muls	r3, r6, r3
.L630:
	cmp	r2, r3
	bcs	.L627
	mov	r1, r5
	ldr	r0, .L633+8
	bl	List_remove_node
	ldrh	r3, [r4, #308]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #308]	@ movhi
	bl	INSERT_DATA_LIST
.L627:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L631:
	mov	r2, #-1
	b	.L628
.L632:
	mov	r3, #-1
	b	.L630
.L634:
	.align	2
.L633:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+296
	.fnend
	.size	List_update_data_list, .-List_update_data_list
	.align	1
	.global	ftl_map_blk_alloc_new_blk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldrh	r1, [r0, #10]
	movs	r3, #0
	ldr	r2, [r0, #12]
.L636:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L639
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L636
	bl	FtlFreeSysBlkQueueOut
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L637
	ldr	r3, .L642
	ldr	r0, .L642+4
	ldrh	r2, [r3, #2522]
	bl	printk
.L638:
	b	.L638
.L637:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L639:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L643:
	.align	2
.L642:
	.word	.LANCHOR0
	.word	.LC5
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	1
	.global	select_l2p_ram_region
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movs	r1, #0
	ldr	r3, .L654
	movs	r4, #12
	movw	r5, #65535
	ldr	r7, .L654+4
	ldrh	r2, [r3, #2430]
	ldr	r3, [r7, #464]
.L645:
	uxth	r0, r1
	cmp	r0, r2
	bcc	.L647
	mov	r0, r2
	movs	r1, #0
	mov	r6, #-2147483648
	mov	ip, #12
.L648:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L650
	cmp	r0, r2
	bcc	.L646
	ldrh	r7, [r7, #468]
	mov	r0, r2
	movs	r1, #0
	mov	r4, #-1
.L651:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L646
	ldr	r6, [r3, #4]
	cmp	r4, r6
	bls	.L652
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r4, r6
	movne	r0, r5
.L652:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L651
.L647:
	adds	r1, r1, #1
	mla	r6, r4, r1, r3
	ldrh	r6, [r6, #-12]
	cmp	r6, r5
	bne	.L645
.L646:
	pop	{r4, r5, r6, r7, pc}
.L650:
	mla	r4, ip, r1, r3
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L649
	cmp	r6, r4
	itt	hi
	movhi	r6, r4
	movhi	r0, r5
.L649:
	adds	r1, r1, #1
	b	.L648
.L655:
	.align	2
.L654:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	1
	.global	FtlUpdateVaildLpn
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L662
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r3, r1
	ldrh	r2, [r1, #470]
	cmp	r2, #4
	bhi	.L657
	cbnz	r0, .L657
	adds	r2, r2, #1
	strh	r2, [r1, #470]	@ movhi
	pop	{r4, r5, pc}
.L657:
	ldr	r1, .L662+4
	movs	r2, #0
	strh	r2, [r3, #470]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #472]
	ldrh	r1, [r1, #2332]
	ldr	r2, [r3, #300]
	add	r1, r2, r1, lsl #1
.L658:
	cmp	r2, r1
	bne	.L660
	pop	{r4, r5, pc}
.L660:
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #472]
	addne	r0, r0, r4
	strne	r0, [r3, #472]
	b	.L658
.L663:
	.align	2
.L662:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	1
	.global	ftl_set_blk_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r0
	cbz	r1, .L665
	b	ftl_set_blk_mode.part.9
.L665:
	ldr	r2, .L666
	lsrs	r0, r0, #5
	and	r3, r3, #31
	ldr	r1, [r2, #32]
	movs	r2, #1
	lsl	r3, r2, r3
	ldr	r2, [r1, r0, lsl #2]
	bic	r2, r2, r3
	str	r2, [r1, r0, lsl #2]
	bx	lr
.L667:
	.align	2
.L666:
	.word	.LANCHOR0
	.fnend
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	1
	.global	ftl_get_blk_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L669
	lsrs	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #32]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L670:
	.align	2
.L669:
	.word	.LANCHOR0
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	1
	.global	ftl_sb_update_avl_pages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movs	r3, #0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	strh	r3, [r0, #4]	@ movhi
	add	r4, r0, r2, lsl #1
	movw	r7, #65535
	ldr	r3, .L678
	adds	r4, r4, #14
	ldrh	r5, [r3, #2324]
.L672:
	cmp	r2, r5
	bcc	.L674
	ldrh	r3, [r3, #2390]
	add	r4, r0, #16
	movw	r6, #65535
	subs	r3, r3, #1
	subs	r1, r3, r1
	movs	r3, #0
	uxth	r1, r1
.L675:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L677
	pop	{r4, r5, r6, r7, pc}
.L674:
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	uxth	r2, r2
	cmp	r6, r7
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L672
.L677:
	ldrh	r2, [r4], #2
	adds	r3, r3, #1
	cmp	r2, r6
	ittt	ne
	ldrhne	r2, [r0, #4]
	addne	r2, r2, r1
	strhne	r2, [r0, #4]	@ movhi
	b	.L675
.L679:
	.align	2
.L678:
	.word	.LANCHOR0
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	1
	.global	make_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r6, .L692
	add	r7, r0, #16
	movw	r9, #65535
	movs	r5, #0
	strh	r5, [r0, #4]	@ movhi
	ldrh	r8, [r6, #2324]
	addw	r10, r6, #2350
	strb	r5, [r0, #7]
.L681:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L683
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r6, #2390]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2248]
	cbz	r3, .L684
	ldr	r3, .L692+4
	ldrh	r2, [r4]
	ldr	r3, [r3, #236]
	ldrh	r3, [r3, r2, lsl #1]
	cmp	r3, #79
	itt	ls
	movls	r3, #1
	strbls	r3, [r4, #9]
.L684:
	ldrb	r3, [r6, #36]	@ zero_extendqisi2
	cbz	r3, .L685
	movs	r3, #1
	strb	r3, [r4, #9]
.L685:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L683:
	ldrh	r1, [r4]
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	strh	r9, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L682
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L682:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L681
.L693:
	.align	2
.L692:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	make_superblock, .-make_superblock
	.align	1
	.global	update_multiplier_value
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	movs	r5, #0
	ldr	r6, .L700
	mov	r7, r0
	mov	r4, r5
	ldrh	r8, [r6, #2324]
	addw	r6, r6, #2350
	ldrh	r9, [r6, #40]
.L695:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L697
	cbz	r4, .L699
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L698:
	ldr	r3, .L700+4
	movs	r2, #6
	ldr	r3, [r3, #292]
	mla	r7, r2, r7, r3
	strh	r0, [r7, #4]	@ movhi
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L697:
	mov	r1, r7
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L696
	add	r4, r4, r9
	uxth	r4, r4
.L696:
	adds	r5, r5, #1
	b	.L695
.L699:
	mov	r0, r4
	b	.L698
.L701:
	.align	2
.L700:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	update_multiplier_value, .-update_multiplier_value
	.align	1
	.global	GetFreeBlockMinEraseCount
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L705
	ldr	r0, [r2, #312]
	cbz	r0, .L703
	ldr	r3, [r2, #292]
	subs	r0, r0, r3
	ldr	r3, .L705+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #236]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L703:
	bx	lr
.L706:
	.align	2
.L705:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	1
	.global	GetFreeBlockMaxEraseCount
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L715
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #312]
	cbz	r3, .L713
	ldrh	r2, [r1, #316]
	movs	r6, #6
	ldr	r4, [r1, #292]
	movw	r7, #65535
	rsb	r2, r2, r2, lsl #3
	subs	r3, r3, r4
	asrs	r3, r3, #1
	asrs	r2, r2, #3
	cmp	r0, r2
	it	gt
	uxthgt	r0, r2
	ldr	r2, .L715+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L710:
	uxth	r5, r2
	cmp	r0, r5
	bls	.L712
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L714
.L712:
	ldr	r2, [r1, #236]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L714:
	mov	r3, r5
	b	.L710
.L713:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L716:
	.align	2
.L715:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	1
	.global	FtlPrintInfo2buf
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	ldr	r4, .L729
	add	r5, r7, #12
	.pad #36
	sub	sp, sp, #36
	ldr	r1, .L729+4
	bl	strcpy
	ldr	r2, [r4, #124]
	mov	r0, r5
	ldr	r1, .L729+8
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r4, #2408]
	mov	r0, r5
	ldr	r1, .L729+12
	bl	sprintf
	ldr	r3, .L729+16
	add	r5, r5, r0
	ldr	r3, [r3, #500]
	cmp	r3, #1
	beq	.L718
	subs	r0, r5, r7
.L717:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L730:
	.align	2
.L729:
	.word	.LANCHOR0
	.word	.LC6
	.word	.LC7
	.word	.LC8
	.word	.LANCHOR1
.L718:
	add	r3, sp, #28
	add	r2, sp, #24
	add	r1, sp, #20
	add	r0, sp, #16
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #28]
	mov	r0, r5
	ldr	r2, [sp, #16]
	ldr	r1, .L731
	str	r3, [sp, #4]
	ldr	r3, [sp, #24]
	str	r3, [sp]
	ldr	r3, [sp, #20]
	bl	sprintf
	adds	r6, r5, r0
	ldr	r1, .L731+4
	ldr	r5, .L731+8
	mov	r0, r6
	adds	r6, r6, #10
	bl	strcpy
	ldr	r2, [r4, #2452]
	mov	r0, r6
	ldr	r1, .L731+12
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #472]
	ldr	r1, .L731+16
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #476]
	ldr	r1, .L731+20
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #480]
	ldr	r1, .L731+24
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #484]
	ldr	r1, .L731+28
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #488]
	ldr	r1, .L731+32
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #492]
	ldr	r1, .L731+36
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #496]
	ldr	r1, .L731+40
	mov	r0, r6
	bl	sprintf
	ldr	r2, [r5, #500]
	add	r6, r6, r0
	ldr	r1, .L731+44
	mov	r0, r6
	lsrs	r2, r2, #11
	bl	sprintf
	ldr	r2, [r5, #504]
	add	r6, r6, r0
	ldr	r1, .L731+48
	mov	r0, r6
	lsrs	r2, r2, #11
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #508]
	ldr	r1, .L731+52
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #512]
	ldr	r1, .L731+56
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	bl	FtlBbtCalcTotleCnt
	ldrh	r2, [r4, #2462]
	mov	r3, r0
	ldr	r1, .L731+60
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #316]
	ldr	r1, .L731+64
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #516]
	ldr	r1, .L731+68
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #520]
	ldr	r1, .L731+72
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #524]
	ldr	r1, .L731+76
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #240]
	ldr	r1, .L731+80
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #528]
	ldr	r1, .L731+84
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r5, #532]
	ldr	r1, .L731+88
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #274]
	ldr	r1, .L731+92
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #272]
	ldr	r1, .L731+96
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r4, #2432]
	ldr	r1, .L731+100
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r4, #2424]
	ldr	r1, .L731+104
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r4, #2320]
	ldr	r1, .L731+108
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #2522]
	ldr	r1, .L731+112
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #2332]
	ldr	r1, .L731+116
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #536]
	ldr	r1, .L731+120
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r4, #2336]
	ldr	r1, .L731+124
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #540]
	ldr	r1, .L731+128
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #2456]
	ldr	r1, .L731+132
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #322]
	ldr	r1, .L731+136
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r5, #326]	@ zero_extendqisi2
	ldr	r1, .L731+140
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #320]
	ldr	r1, .L731+144
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r5, #328]	@ zero_extendqisi2
	ldr	r1, .L731+148
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #324]
	ldr	r1, .L731+152
	mov	r0, r6
	bl	sprintf
	ldr	r3, [r5, #300]
	add	r6, r6, r0
	ldrh	r2, [r5, #320]
	mov	r0, r6
	ldr	r1, .L731+156
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #370]
	ldr	r1, .L731+160
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r5, #374]	@ zero_extendqisi2
	ldr	r1, .L731+164
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #368]
	ldr	r1, .L731+168
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r5, #376]	@ zero_extendqisi2
	ldr	r1, .L731+172
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #372]
	ldr	r1, .L731+176
	mov	r0, r6
	bl	sprintf
	ldr	r3, [r5, #300]
	add	r6, r6, r0
	ldrh	r2, [r5, #368]
	mov	r0, r6
	ldr	r1, .L731+180
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #418]
	ldr	r1, .L731+184
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r5, #422]	@ zero_extendqisi2
	ldr	r1, .L731+188
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #416]
	ldr	r1, .L731+192
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r5, #424]	@ zero_extendqisi2
	ldr	r1, .L731+196
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #420]
	ldr	r1, .L731+200
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #558]
	ldr	r1, .L731+204
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r5, #562]	@ zero_extendqisi2
	ldr	r1, .L731+208
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #556]
	ldr	r1, .L731+212
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r5, #564]	@ zero_extendqisi2
	ldr	r1, .L731+216
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #560]
	ldr	r1, .L731+220
	mov	r0, r6
	bl	sprintf
	ldr	r3, [r5, #680]
	add	r6, r6, r0
	ldr	r1, [r5, #1116]
	mov	r0, r6
	ldr	r2, [r4, #2248]
	str	r3, [sp, #4]
	ldr	r3, [r5, #688]
	orr	r2, r2, r1, lsl #8
	ldr	r1, .L731+224
	str	r3, [sp]
	ldr	r3, [r5, #684]
	bl	sprintf
	adds	r4, r6, r0
	ldr	r2, [r5, #676]
	ldr	r1, .L731+228
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r5, #700]
	ldr	r1, .L731+232
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r5, #1120]
	ldr	r1, .L731+236
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r5, #1122]
	b	.L732
.L733:
	.align	2
.L731:
	.word	.LC9
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
	.word	.LC65
	.word	.LC66
	.word	.LC67
.L732:
	ldr	r1, .L734
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r5, #1124]
	ldr	r1, .L734+4
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r5, #1128]
	ldr	r1, .L734+8
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L734+12
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #316]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L734+16
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r5, #556]
	movw	r2, #65535
	add	r4, r4, r0
	cmp	r3, r2
	beq	.L720
	ldr	r2, [r5, #300]
	mov	r0, r4
	ldr	r1, .L734+20
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
	add	r4, r4, r0
.L720:
	movs	r0, #0
	ldr	r9, .L734+28
	bl	List_get_gc_head_node
	uxth	r3, r0
	movs	r6, #0
	movw	fp, #65535
	mov	r10, #6
.L722:
	cmp	r3, fp
	beq	.L721
	ldr	r2, [r5, #236]
	mul	r8, r10, r3
	mov	r0, r4
	mov	r1, r9
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	ldr	r2, [r5, #292]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r5, #300]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp]
	mov	r2, r6
	bl	sprintf
	adds	r6, r6, #1
	ldr	r3, [r5, #292]
	cmp	r6, #16
	add	r4, r4, r0
	ldrh	r3, [r3, r8]
	bne	.L722
.L721:
	ldr	r2, [r5, #292]
	movs	r6, #0
	ldr	r3, [r5, #312]
	movw	r9, #65535
	ldr	fp, .L734+32
	mov	r10, #6
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L734+24
	muls	r3, r2, r3
	uxth	r3, r3
.L724:
	cmp	r3, r9
	beq	.L723
	ldr	r2, [r5, #236]
	mul	r8, r10, r3
	mov	r0, r4
	mov	r1, fp
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #4]
	ldr	r2, [r5, #292]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r6
	adds	r6, r6, #1
	bl	sprintf
	cmp	r6, #4
	add	r4, r4, r0
	beq	.L723
	ldr	r3, [r5, #292]
	ldrh	r3, [r3, r8]
	b	.L724
.L723:
	subs	r0, r4, r7
	b	.L717
.L735:
	.align	2
.L734:
	.word	.LC68
	.word	.LC69
	.word	.LC70
	.word	.LC71
	.word	.LC72
	.word	.LC73
	.word	-1431655765
	.word	.LC74
	.word	.LC75
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	1
	.global	ftl_proc_ftl_read
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_proc_ftl_read, %function
ftl_proc_ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r2, .L737
	ldr	r1, .L737+4
	bl	sprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L738:
	.align	2
.L737:
	.word	.LC76
	.word	.LC77
	.fnend
	.size	ftl_proc_ftl_read, .-ftl_proc_ftl_read
	.align	1
	.global	GetSwlReplaceBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L765
	ldr	r2, [r4, #524]
	ldr	r3, [r4, #532]
	cmp	r2, r3
	bcs	.L740
	ldr	r5, .L765+4
	movs	r3, #0
	ldr	r0, [r4, #236]
	str	r3, [r4, #516]
	ldrh	r1, [r5, #2332]
.L741:
	cmp	r3, r1
	bcc	.L742
	ldr	r6, [r4, #516]
	mov	r0, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #524]
	ldr	r0, [r4, #520]
	ldrh	r1, [r5, #2382]
	subs	r0, r6, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #516]
.L743:
	ldr	r6, [r4, #532]
	ldr	r7, [r4, #524]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L748
	ldr	r2, [r4, #528]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L748
	ldr	r3, .L765+4
	ldr	r3, [r3, #2248]
	cbnz	r3, .L749
.L751:
	movw	r5, #65535
.L750:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L742:
	ldrh	r2, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r6, [r4, #516]
	add	r2, r2, r6
	str	r2, [r4, #516]
	b	.L741
.L740:
	ldr	r3, [r4, #528]
	cmp	r2, r3
	bls	.L743
	ldr	r0, .L765+4
	adds	r3, r3, #1
	str	r3, [r4, #528]
	movs	r3, #0
.L745:
	ldrh	r2, [r0, #2332]
	cmp	r3, r2
	bcs	.L743
	ldr	r1, [r4, #236]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L745
.L749:
	cmp	r6, #40
	bhi	.L751
.L748:
	ldrh	r0, [r4, #316]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcs	.L752
	cmp	r6, #40
	bhi	.L751
.L752:
	ldr	r3, [r4, #296]
	cmp	r3, #0
	beq	.L751
	ldr	r2, .L765+4
	movw	r1, #65535
	ldr	lr, [r4, #292]
	movs	r0, #0
	ldr	r8, [r4, #236]
	mov	r10, r1
	ldrh	r2, [r2, #2332]
	mov	fp, #6
	str	r2, [sp, #20]
	mov	r2, r1
.L753:
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L756
	mov	r5, r2
.L755:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L751
	ldrh	r10, [r8, r5, lsl #1]
	lsl	fp, r5, #1
	cmp	r6, r10
	bcs	.L757
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	it	cc
	strcc	r1, [r4, #532]
.L757:
	cmp	r7, r10
	bls	.L751
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L751
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L758
	ldr	r3, [r4, #528]
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L751
.L758:
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
	ldr	r0, .L765+8
	str	r3, [sp, #4]
	ldr	r3, [r4, #300]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #528]
	bl	printk
	movs	r3, #1
	str	r3, [r4, #1132]
	b	.L750
.L756:
	adds	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r0, r5
	bhi	.L751
	ldrh	r5, [r3, #4]
	cbz	r5, .L754
	ldr	r5, .L765+12
	sub	r3, r3, lr
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L755
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	movhi	r2, r5
.L754:
	mla	r3, fp, ip, lr
	b	.L753
.L766:
	.align	2
.L765:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC78
	.word	-1431655765
	.fnend
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	1
	.global	free_data_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	free_data_superblock, %function
free_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r2, #65535
	push	{r3, lr}
	.save {r3, lr}
	cmp	r0, r2
	beq	.L768
	ldr	r2, .L769
	movs	r1, #0
	ldr	r2, [r2, #300]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L768:
	movs	r0, #0
	pop	{r3, pc}
.L770:
	.align	2
.L769:
	.word	.LANCHOR2
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	1
	.global	FtlGcBufInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L780
	mov	ip, #1
	ldr	r0, .L780+4
	movs	r3, #0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r7, #12
	mov	r5, r2
	mov	lr, #36
	mov	r1, r0
	str	r3, [r2, #1136]
.L772:
	ldrh	r4, [r0, #2324]
	uxth	r8, r3
	adds	r6, r3, #1
	cmp	r8, r4
	bcc	.L775
	ldr	r7, .L780
	mov	lr, #12
	movs	r6, #0
.L776:
	ldr	r3, [r2, #1156]
	cmp	r4, r3
	bcc	.L779
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L775:
	uxth	r3, r3
	ldr	r9, [r2, #1140]
	mul	r8, r7, r3
	add	r4, r9, r8
	str	ip, [r4, #8]
	ldrh	r4, [r0, #2400]
	muls	r4, r3, r4
	it	mi
	addmi	r4, r4, #3
	bic	r10, r4, #3
	ldr	r4, [r5, #1144]
	add	r4, r4, r10
	str	r4, [r9, r8]
	ldrh	r4, [r1, #2402]
	muls	r4, r3, r4
	it	mi
	addmi	r4, r4, #3
	ldr	r10, [r2, #1140]
	bic	fp, r4, #3
	ldr	r4, [r5, #1148]
	add	r9, r10, r8
	add	r4, r4, fp
	str	r4, [r9, #4]
	ldr	r4, [r2, #1152]
	mla	r3, lr, r3, r4
	ldr	r4, [r10, r8]
	str	r4, [r3, #8]
	ldr	r4, [r9, #4]
	str	r4, [r3, #12]
	mov	r3, r6
	b	.L772
.L779:
	mul	ip, lr, r4
	ldr	r0, [r2, #1140]
	ldr	r5, [r2, #1144]
	add	r3, r0, ip
	str	r6, [r3, #8]
	ldrh	r3, [r1, #2400]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r5
	str	r3, [r0, ip]
	ldr	r3, [r2, #1140]
	ldr	r0, [r7, #1148]
	add	ip, ip, r3
	ldrh	r3, [r1, #2402]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	adds	r4, r4, #1
	add	r3, r3, r0
	uxth	r4, r4
	str	r3, [ip, #4]
	b	.L776
.L781:
	.align	2
.L780:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	1
	.global	FtlGcBufFree
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L789
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	mov	r10, #36
	mov	lr, #12
	mov	r8, r5
	ldr	ip, [r3, #1156]
	ldr	r6, [r3, #1140]
.L783:
	uxth	r3, r5
	cmp	r1, r3
	bls	.L782
	mla	r4, r10, r3, r0
	movs	r2, #0
.L784:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L785
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L784
	str	r8, [r9, #8]
.L785:
	adds	r5, r5, #1
	b	.L783
.L782:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L790:
	.align	2
.L789:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	1
	.global	FtlGcBufAlloc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L798
	mov	ip, #12
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	mov	lr, #1
	mov	r8, #36
	ldr	r5, [r3, #1156]
	ldr	r6, [r3, #1140]
.L792:
	uxth	r2, r4
	cmp	r1, r2
	bhi	.L796
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L796:
	mov	r9, #0
.L793:
	uxth	r3, r9
	cmp	r5, r3
	bls	.L794
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L793
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	lr, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L794:
	adds	r4, r4, #1
	b	.L792
.L799:
	.align	2
.L798:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	1
	.global	IsBlkInGcList
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L805
	ldr	r3, [r2, #1160]
	ldrh	r2, [r2, #1164]
	add	r2, r3, r2, lsl #1
.L801:
	cmp	r3, r2
	bne	.L803
	movs	r0, #0
	bx	lr
.L803:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L801
	movs	r0, #1
	bx	lr
.L806:
	.align	2
.L805:
	.word	.LANCHOR2
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	1
	.global	FtlGcUpdatePage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r0
	ubfx	r0, r0, #10, #16
	mov	r6, r1
	mov	r7, r2
	movs	r4, #0
	bl	P2V_block_in_plane
	ldr	r3, .L811
	ldrh	r1, [r3, #1164]
	ldr	r2, [r3, #1160]
.L808:
	uxth	ip, r4
	cmp	ip, r1
	bcc	.L810
	bne	.L809
	strh	r0, [r2, ip, lsl #1]	@ movhi
	ldrh	r0, [r3, #1164]
	adds	r0, r0, #1
	strh	r0, [r3, #1164]	@ movhi
	b	.L809
.L810:
	adds	r4, r4, #1
	add	ip, r2, r4, lsl #1
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L808
.L809:
	ldrh	r2, [r3, #1172]
	movs	r0, #12
	muls	r0, r2, r0
	ldr	r2, [r3, #1168]
	adds	r1, r2, r0
	str	r6, [r1, #4]
	str	r7, [r1, #8]
	str	r5, [r2, r0]
	ldrh	r2, [r3, #1172]
	adds	r2, r2, #1
	strh	r2, [r3, #1172]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L812:
	.align	2
.L811:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	1
	.global	FtlGcRefreshOpenBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcRefreshOpenBlock, %function
FtlGcRefreshOpenBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L820
	ldrh	r3, [r4, #1174]
	cmp	r3, r0
	beq	.L815
	ldrh	r3, [r4, #1176]
	cmp	r3, r0
	beq	.L815
	ldrh	r3, [r4, #1178]
	cmp	r3, r0
	beq	.L815
	ldrh	r3, [r4, #1180]
	cmp	r3, r0
	beq	.L815
	mov	r1, r0
	ldr	r0, .L820+4
	bl	printk
	ldrh	r2, [r4, #1174]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L817
	strh	r5, [r4, #1174]	@ movhi
.L815:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L817:
	ldrh	r2, [r4, #1176]
	cmp	r2, r3
	bne	.L818
	strh	r5, [r4, #1176]	@ movhi
	b	.L815
.L818:
	ldrh	r2, [r4, #1178]
	cmp	r2, r3
	bne	.L819
	strh	r5, [r4, #1178]	@ movhi
	b	.L815
.L819:
	ldrh	r2, [r4, #1180]
	cmp	r2, r3
	it	eq
	strheq	r5, [r4, #1180]	@ movhi
	b	.L815
.L821:
	.align	2
.L820:
	.word	.LANCHOR2
	.word	.LC79
	.fnend
	.size	FtlGcRefreshOpenBlock, .-FtlGcRefreshOpenBlock
	.align	1
	.global	FtlGcRefreshBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L832
	ldrh	r3, [r4, #1174]
	cmp	r3, r0
	beq	.L830
	ldrh	r3, [r4, #1176]
	cmp	r3, r0
	beq	.L830
	ldrh	r3, [r4, #1178]
	cmp	r3, r0
	beq	.L830
	ldrh	r3, [r4, #1180]
	cmp	r3, r0
	beq	.L830
	mov	r1, r0
	ldr	r0, .L832+4
	bl	printk
	ldrh	r2, [r4, #1174]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L824
	strh	r5, [r4, #1174]	@ movhi
.L830:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L824:
	ldrh	r2, [r4, #1176]
	cmp	r2, r3
	bne	.L825
	strh	r5, [r4, #1176]	@ movhi
	b	.L830
.L825:
	ldrh	r2, [r4, #1178]
	cmp	r2, r3
	bne	.L826
	strh	r5, [r4, #1178]	@ movhi
	b	.L830
.L826:
	ldrh	r2, [r4, #1180]
	cmp	r2, r3
	bne	.L831
	strh	r5, [r4, #1180]	@ movhi
	b	.L830
.L831:
	mov	r0, #-1
	pop	{r3, r4, r5, pc}
.L833:
	.align	2
.L832:
	.word	.LANCHOR2
	.word	.LC79
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	1
	.global	FtlGcMarkBadPhyBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r4, .L842
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r6, r0
	ldrh	r1, [r4, #1182]
	ldr	r0, .L842+4
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r3, .L842+8
	ldr	r2, [r3, #2248]
	mov	r3, r4
	cbz	r2, .L835
	ldr	r1, [r4, #236]
	ldrh	r2, [r1, r6, lsl #1]
	cmp	r2, #39
	itt	hi
	subhi	r2, r2, #40
	strhhi	r2, [r1, r6, lsl #1]	@ movhi
.L835:
	ldrh	r2, [r3, #1182]
	movs	r1, #0
	ldr	r4, .L842+12
.L836:
	uxth	r0, r1
	cmp	r2, r0
	bhi	.L838
	cmp	r2, #15
	itttt	ls
	addls	r1, r2, #1
	strhls	r1, [r3, #1182]	@ movhi
	addls	r3, r3, r2, lsl #1
	strhls	r5, [r3, #1184]	@ movhi
	b	.L837
.L838:
	adds	r1, r1, #1
	add	r0, r4, r1, lsl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r5
	bne	.L836
.L837:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L843:
	.align	2
.L842:
	.word	.LANCHOR2
	.word	.LC80
	.word	.LANCHOR0
	.word	.LANCHOR2+1184
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	1
	.global	FtlGcReFreshBadBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L850
	ldrh	r3, [r4, #1182]
	cbz	r3, .L845
	ldrh	r1, [r4, #1174]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L845
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
.L845:
	movs	r0, #0
	pop	{r4, pc}
.L851:
	.align	2
.L850:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	1
	.global	ftl_memset
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	BuildFlashLsbPageTable, %function
BuildFlashLsbPageTable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r1
	cbnz	r0, .L854
	ldr	r3, .L909
.L855:
	strh	r0, [r3, r0, lsl #1]	@ movhi
	adds	r0, r0, #1
	cmp	r0, #512
	bne	.L855
.L861:
	movs	r1, #255
	mov	r2, #2048
	ldr	r0, .L909+4
	uxth	r4, r4
	bl	ftl_memset
	ldr	r1, .L909
	movs	r3, #0
	ldr	r0, .L909+8
.L856:
	uxth	r2, r3
	cmp	r4, r2
	bhi	.L889
	pop	{r3, r4, r5, pc}
.L854:
	cmp	r0, #1
	bne	.L857
	ldr	r1, .L909
	movs	r3, #0
.L860:
	cmp	r3, #3
	uxth	r2, r3
	bls	.L858
	tst	r2, #1
	ite	ne
	movne	r0, #3
	moveq	r0, #2
	rsb	r2, r0, r2, lsl #1
	uxth	r2, r2
.L858:
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #512
	bne	.L860
	b	.L861
.L857:
	cmp	r0, #2
	bne	.L862
	ldr	r1, .L909
	movs	r2, #0
.L864:
	uxth	r3, r2
	cmp	r2, #1
	ittt	hi
	lslhi	r3, r3, #1
	addhi	r3, r3, #-1
	uxthhi	r3, r3
	strh	r3, [r1, r2, lsl #1]	@ movhi
	adds	r2, r2, #1
	cmp	r2, #512
	bne	.L864
	b	.L861
.L862:
	cmp	r0, #3
	bne	.L865
	ldr	r1, .L909
	movs	r3, #0
.L868:
	cmp	r3, #5
	uxth	r2, r3
	bls	.L866
	tst	r2, #1
	ite	ne
	movne	r0, #5
	moveq	r0, #4
	rsb	r2, r0, r2, lsl #1
	uxth	r2, r2
.L866:
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #512
	bne	.L868
	b	.L861
.L865:
	cmp	r0, #4
	mov	r3, #0
	bne	.L869
	ldr	r2, .L909+12
	strh	r3, [r2, #156]	@ movhi
	movs	r3, #1
	strh	r3, [r2, #158]	@ movhi
	movs	r3, #2
	strh	r3, [r2, #160]	@ movhi
	movs	r3, #3
	strh	r3, [r2, #162]	@ movhi
	movs	r3, #5
	strh	r3, [r2, #166]	@ movhi
	movs	r3, #7
	strh	r3, [r2, #168]	@ movhi
	movs	r3, #8
	strh	r0, [r2, #164]	@ movhi
	strh	r3, [r2, #170]!	@ movhi
.L871:
	tst	r3, #1
	ite	ne
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r1, [r2, #2]!	@ movhi
	cmp	r3, #512
	bne	.L871
	b	.L861
.L869:
	cmp	r0, #5
	bne	.L872
	ldr	r2, .L909+12
	add	r1, r2, #156
.L873:
	strh	r3, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #16
	bne	.L873
	adds	r2, r2, #186
.L874:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L874
	b	.L861
.L872:
	cmp	r0, #6
	bne	.L875
	ldr	r0, .L909
	mov	r1, r3
.L878:
	cmp	r1, #5
	uxth	r2, r1
	bls	.L876
	tst	r2, #1
	ite	ne
	movne	r2, #12
	moveq	r2, #10
	subs	r2, r3, r2
	uxth	r2, r2
.L876:
	strh	r2, [r0, r1, lsl #1]	@ movhi
	adds	r1, r1, #1
	cmp	r1, #512
	add	r3, r3, #3
	uxth	r3, r3
	bne	.L878
	b	.L861
.L875:
	cmp	r0, #9
	bne	.L879
	ldr	r2, .L909+12
	movw	r1, #1021
	strh	r3, [r2, #156]	@ movhi
	movs	r3, #1
	strh	r3, [r2, #158]	@ movhi
	mov	r3, r2
	movs	r2, #2
	strh	r2, [r3, #160]!	@ movhi
	movs	r2, #3
.L880:
	strh	r2, [r3, #2]!	@ movhi
	adds	r2, r2, #2
	uxth	r2, r2
	cmp	r2, r1
	bne	.L880
	b	.L861
.L879:
	cmp	r0, #10
	bne	.L881
	ldr	r2, .L909+12
	add	r1, r2, #156
.L882:
	strh	r3, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #63
	bne	.L882
	add	r2, r2, #280
	movw	r1, #961
.L883:
	strh	r3, [r2, #2]!	@ movhi
	adds	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L883
	b	.L861
.L881:
	cmp	r0, #11
	bne	.L884
	ldr	r2, .L909+12
	movs	r3, #0
	add	r1, r2, #156
.L885:
	strh	r3, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	cmp	r3, #8
	bne	.L885
	adds	r2, r2, #170
.L887:
	tst	r3, #1
	ite	ne
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r1, [r2, #2]!	@ movhi
	cmp	r3, #512
	bne	.L887
	b	.L861
.L884:
	cmp	r0, #12
	bne	.L861
	ldr	r3, .L909+12
	movs	r2, #0
	strh	r2, [r3, #156]	@ movhi
	movs	r2, #1
	strh	r2, [r3, #158]	@ movhi
	movs	r2, #2
	strh	r2, [r3, #160]	@ movhi
	movs	r2, #3
	strh	r2, [r3, #162]!	@ movhi
	movs	r2, #4
.L888:
	subs	r1, r2, #1
	add	r1, r1, r2, lsr #1
	adds	r2, r2, #1
	uxth	r2, r2
	strh	r1, [r3, #2]!	@ movhi
	cmp	r2, #512
	bne	.L888
	b	.L861
.L889:
	ldrh	r2, [r1, r3, lsl #1]
	adds	r3, r3, #1
	add	r5, r0, r2, lsl #1
	strh	r2, [r5, #1220]	@ movhi
	b	.L856
.L910:
	.align	2
.L909:
	.word	.LANCHOR0+156
	.word	.LANCHOR2+1220
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	1
	.global	FlashDieInfoInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L925
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r7, #0
	ldr	r4, .L925+4
	ldr	r6, .L925+8
	ldrh	r0, [r3, #478]
	addw	r10, r4, #2072
	strb	r7, [r4, #2234]
	mov	r9, r10
	strb	r7, [r6, #3268]
	bl	FlashBlockAlignInit
	movs	r2, #8
	mov	r1, r7
	addw	r0, r4, #2236
	bl	ftl_memset
	movs	r2, #32
	mov	r1, r7
	addw	r0, r4, #1180
	bl	ftl_memset
	movs	r2, #128
	mov	r1, r7
	addw	r0, r4, #2104
	bl	ftl_memset
	ldr	r5, [r4, #48]
	add	r8, r5, #1
.L913:
	ldrb	r2, [r5]	@ zero_extendqisi2
	add	r1, r10, r7, lsl #3
	mov	r0, r8
	bl	FlashMemCmp8
	cbnz	r0, .L912
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	add	r2, r4, r3, lsl #2
	str	r0, [r2, #1180]
	adds	r2, r3, #1
	add	r3, r3, r4
	strb	r2, [r4, #2234]
	strb	r7, [r3, #2236]
.L912:
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L913
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	strb	r3, [r6, #3268]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L914
.L918:
	ldrh	r2, [r5, #14]
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	ldrb	r2, [r5, #13]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	strh	r3, [r6, #3270]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L914:
	ldr	r10, [r4, #40]
	movs	r7, #0
.L917:
	ldrb	r2, [r5]	@ zero_extendqisi2
	add	r1, r9, r7, lsl #3
	mov	r0, r8
	bl	FlashMemCmp8
	cbnz	r0, .L915
	ldrh	r3, [r5, #14]
	ldrb	r2, [r4, #2234]	@ zero_extendqisi2
	and	r1, r3, #65280
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	mul	r3, r10, r3
	muls	r3, r1, r3
	add	r1, r4, r2, lsl #2
	str	r3, [r1, #1180]
	ldrb	r0, [r5, #23]	@ zero_extendqisi2
	cbz	r0, .L916
	lsls	r3, r3, #1
	str	r3, [r1, #1180]
.L916:
	adds	r3, r2, #1
	add	r2, r2, r4
	strb	r3, [r4, #2234]
	strb	r7, [r2, #2236]
.L915:
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L917
	b	.L918
.L926:
	.align	2
.L925:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashDieInfoInit, .-FlashDieInfoInit
	.align	1
	.global	ftl_read_flash_info
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_read_flash_info, %function
ftl_read_flash_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r2, #11
	movs	r1, #0
	mov	r4, r0
	movs	r5, #1
	bl	ftl_memset
	ldr	r3, .L930
	ldr	r2, [r3, #48]
	ldr	r1, [r3, #40]
	ldrb	r2, [r2, #9]	@ zero_extendqisi2
	smulbb	r2, r2, r1
	strh	r2, [r4, #4]	@ unaligned
	ldrb	r2, [r3, #2316]	@ zero_extendqisi2
	strb	r2, [r4, #7]
	ldr	r2, [r3, #2432]
	str	r2, [r4]	@ unaligned
	ldr	r2, [r3, #48]
	addw	r3, r3, #2236
	ldrb	r0, [r3, #-2]	@ zero_extendqisi2
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	movs	r1, #32
	strb	r1, [r4, #8]
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	strb	r2, [r4, #9]
	movs	r2, #0
	strb	r2, [r4, #10]
.L928:
	uxtb	r1, r2
	cmp	r0, r1
	bhi	.L929
	pop	{r4, r5, r6, pc}
.L929:
	ldrb	r1, [r2, r3]	@ zero_extendqisi2
	adds	r2, r2, #1
	ldrb	r6, [r4, #10]	@ zero_extendqisi2
	lsl	r1, r5, r1
	orrs	r1, r1, r6
	strb	r1, [r4, #10]
	b	.L928
.L931:
	.align	2
.L930:
	.word	.LANCHOR0
	.fnend
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	1
	.global	FtlMemInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	movw	r3, #65535
	ldr	r4, .L1034
	movs	r6, #0
	mov	r0, #1024
	movs	r7, #12
	ldr	r5, .L1034+4
	mov	r8, #36
	str	r3, [r4, #3284]
	strh	r3, [r4, #1174]	@ movhi
	strh	r3, [r4, #1176]	@ movhi
	strh	r3, [r4, #1178]	@ movhi
	strh	r3, [r4, #1180]	@ movhi
	movs	r3, #32
	strh	r3, [r4, #1120]	@ movhi
	movs	r3, #128
	strh	r3, [r4, #1122]	@ movhi
	strh	r6, [r4, #3272]	@ movhi
	str	r6, [r4, #3276]
	str	r6, [r4, #1116]
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
	str	r6, [r4, #240]
	str	r6, [r4, #528]
	str	r6, [r4, #532]
	str	r6, [r4, #3280]
	str	r6, [r4, #1132]
	str	r6, [r4, #3288]
	str	r6, [r4, #1124]
	str	r6, [r4, #3292]
	strh	r6, [r4, #1128]	@ movhi
	strh	r6, [r4, #1182]	@ movhi
	strh	r6, [r4, #3296]	@ movhi
	strh	r6, [r4, #1218]	@ movhi
	ldrh	r1, [r5, #2396]
	bl	__aeabi_idiv
	ldrh	r3, [r5, #2324]
	str	r0, [r4, #3300]
	str	r6, [r5, #2444]
	lsls	r3, r3, #2
	cmp	r0, r3
	ldrh	r0, [r5, #2394]
	it	hi
	strhi	r3, [r4, #3300]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #1160]
	ldrh	r0, [r5, #2394]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, #2324]
	str	r0, [r4, #1168]
	mul	r6, r8, r6
	lsl	r9, r6, #3
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
	str	r0, [r4, #232]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #1152]
	ldr	r0, [r4, #3300]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2324]
	ldrh	r6, [r5, #2400]
	str	r0, [r5, #2448]
	lsls	r3, r3, #1
	mov	r0, r6
	adds	r3, r3, #1
	str	r3, [r4, #1156]
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
	ldrh	r3, [r5, #2402]
	ldrh	r6, [r5, #2324]
	str	r0, [r4, #1140]
	muls	r6, r3, r6
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3340]
	lsls	r0, r6, #3
	bl	ftl_malloc
	ldrh	r3, [r5, #2402]
	str	r0, [r4, #3344]
	ldr	r0, [r4, #1156]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2402]
	str	r0, [r4, #1148]
	ldr	r0, [r4, #3300]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #3348]
	ldrh	r0, [r5, #2334]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3352]	@ movhi
	bl	ftl_malloc
	ldrh	r3, [r4, #3352]
	str	r0, [r4, #3356]
	ldr	r0, .L1034+8
	addw	r3, r3, #547
	lsrs	r3, r3, #9
	and	r0, r0, r3, lsl #9
	strh	r3, [r4, #3352]	@ movhi
	bl	ftl_malloc
	ldrh	r6, [r5, #2334]
	str	r0, [r4, #3360]
	adds	r0, r0, #32
	str	r0, [r4, #236]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3364]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r5, #2420]
	str	r0, [r4, #300]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3368]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3372]
	ldrh	r0, [r5, #2334]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r5, #32]
	ldrh	r0, [r5, #2412]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r5, #2440]
	ldrh	r0, [r5, #2412]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3376]
	ldrh	r0, [r5, #2412]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3380]
	ldrh	r0, [r5, #2414]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5, #2414]
	movs	r1, #0
	str	r0, [r4, #3384]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r6, [r5, #2428]
	lsls	r6, r6, #2
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3388]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3392]
	ldr	r0, [r5, #2420]
	ldr	r6, .L1034+12
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3396]
	ldrh	r0, [r5, #2430]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2430]
	str	r0, [r4, #464]
	ldrh	r0, [r5, #2400]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r5, #2334]
	str	r0, [r4, #3400]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #292]
	ldrh	r0, [r5, #2388]
	ldrh	r3, [r5, #2346]
	adds	r0, r0, #31
	asrs	r0, r0, #5
	strh	r0, [r4, #3404]	@ movhi
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #3404]
	movs	r3, #1
	ldrh	r7, [r5, #2346]
	str	r0, [r5, #2484]
	lsls	r2, r2, #2
	mov	r1, r2
.L934:
	cmp	r3, r7
	bcc	.L935
	ldr	r2, .L1034+16
	movs	r1, #0
	add	r3, r2, r3, lsl #2
	adds	r2, r2, #56
	adds	r3, r3, #24
.L936:
	cmp	r2, r3
	bne	.L937
	ldr	r3, [r4, #3368]
	cbnz	r3, .L938
.L940:
	ldr	r1, .L1034+20
	ldr	r0, .L1034+24
	bl	printk
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L935:
	ldr	r0, [r5, #2484]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r6, #4]!
	b	.L934
.L937:
	str	r1, [r3, #4]!
	b	.L936
.L1035:
	.align	2
.L1034:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+2484
	.word	.LANCHOR0+2456
	.word	.LANCHOR3
	.word	.LC81
.L938:
	ldr	r3, [r4, #3372]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3388]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3396]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #464]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3400]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #292]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r5, #2484]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #300]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #1160]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #1168]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #232]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #1152]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3320]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3324]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #1144]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3332]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #1140]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3340]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3344]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #1148]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #236]
	cmp	r3, #0
	beq	.L940
	ldr	r3, [r4, #3356]
	cmp	r3, #0
	beq	.L940
	ldr	r3, .L1036
	ldr	r3, [r3, #2440]
	cmp	r3, #0
	beq	.L940
	ldr	r3, .L1036+4
	ldr	r2, [r3, #3376]
	cmp	r2, #0
	beq	.L940
	ldr	r2, [r3, #3380]
	cmp	r2, #0
	beq	.L940
	ldr	r3, [r3, #3384]
	cmp	r3, #0
	beq	.L940
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1037:
	.align	2
.L1036:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	1
	.global	FtlBbt2Bitmap
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r1
	ldr	r3, .L1043
	mov	r4, r0
	movs	r1, #0
	mov	r0, r5
	movs	r7, #1
	ldrh	r2, [r3, #3404]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldr	r0, .L1043+4
	subs	r2, r4, #2
	movw	ip, #65535
	addw	r4, r4, #1022
.L1040:
	ldrh	r3, [r2, #2]!
	cmp	r3, ip
	beq	.L1038
	lsrs	r6, r3, #5
	and	r3, r3, #31
	lsl	r3, r7, r3
	cmp	r2, r4
	ldr	r1, [r5, r6, lsl #2]
	orr	r3, r3, r1
	str	r3, [r5, r6, lsl #2]
	ldrh	r3, [r0, #2462]
	add	r3, r3, #1
	strh	r3, [r0, #2462]	@ movhi
	bne	.L1040
.L1038:
	pop	{r3, r4, r5, r6, r7, pc}
.L1044:
	.align	2
.L1043:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	1
	.global	FtlBbtMemInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L1046
	movw	r3, #65535
	movs	r2, #16
	movs	r1, #255
	strh	r3, [r0, #2456]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #2462]	@ movhi
	addw	r0, r0, #2468
	b	ftl_memset
.L1047:
	.align	2
.L1046:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	1
	.global	FtlFreeSysBlkQueueInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1049
	mov	r2, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	mov	r1, r4
	strh	r0, [r3, #2516]	@ movhi
	addw	r0, r3, #2524
	strh	r4, [r3, #2518]	@ movhi
	strh	r4, [r3, #2520]	@ movhi
	strh	r4, [r3, #2522]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L1050:
	.align	2
.L1049:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	1
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	ldr	r5, [r0, #20]
	ldr	r7, [r0, #12]
	ldr	r6, [r0, #24]
	lsls	r2, r2, #1
	mov	r0, r5
	bl	ftl_memset
	movs	r3, #0
.L1052:
	ldrh	r1, [r4, #6]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L1056
	ldr	r3, .L1071
	movs	r6, #0
	mov	r9, r6
	mov	fp, r6
	ldrh	r2, [r3, #2392]
	ldrh	r3, [r4]
	strh	r2, [r5, r3, lsl #1]	@ movhi
	ldrh	r10, [r5]
.L1057:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L1061
	mov	r0, r9
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1056:
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	movs	r2, #0
	ubfx	r1, r1, #10, #16
.L1053:
	ldrh	ip, [r4, #10]
	uxth	r0, r2
	cmp	ip, r0
	bhi	.L1055
	adds	r3, r3, #1
	b	.L1052
.L1055:
	uxth	r0, r2
	ldrh	ip, [r7, r0, lsl #1]
	cmp	ip, r1
	bne	.L1054
	cbz	r1, .L1054
	ldrh	ip, [r5, r0, lsl #1]
	add	ip, ip, #1
	strh	ip, [r5, r0, lsl #1]	@ movhi
.L1054:
	adds	r2, r2, #1
	b	.L1053
.L1061:
	uxth	r2, r6
	ldrh	r3, [r5, r2, lsl #1]
	lsl	r8, r2, #1
	cmp	r10, r3
	bls	.L1058
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cbnz	r0, .L1059
.L1060:
	adds	r6, r6, #1
	b	.L1057
.L1058:
	cmp	r3, #0
	bne	.L1060
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cmp	r0, #0
	beq	.L1060
.L1062:
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r8]	@ movhi
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L1060
.L1059:
	mov	r9, r1
	mov	r10, r3
	cmp	r3, #0
	beq	.L1062
	b	.L1060
.L1072:
	.align	2
.L1071:
	.word	.LANCHOR0
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	1
	.global	FtlL2PDataInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r1, #0
	ldr	r4, .L1076
	movs	r7, #12
	ldr	r5, .L1076+4
	ldr	r2, [r4, #2420]
	ldr	r0, [r5, #3372]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #2400]
	movs	r1, #255
	ldrh	r2, [r4, #2430]
	ldr	r0, [r5, #3400]
	muls	r2, r3, r2
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r5
	movw	r1, #65535
	mov	ip, r2
.L1074:
	ldrh	r5, [r4, #2430]
	uxth	r0, r2
	adds	r6, r2, #1
	cmp	r5, r0
	bhi	.L1075
	ldr	r2, [r4, #2420]
	strh	r1, [r3, #3410]	@ movhi
	strh	r1, [r3, #3408]	@ movhi
	strh	r2, [r3, #3418]	@ movhi
	movw	r2, #61634
	strh	r2, [r3, #3412]	@ movhi
	ldrh	r2, [r3, #3452]
	strh	r2, [r3, #3416]	@ movhi
	ldrh	r2, [r4, #2428]
	strh	r2, [r3, #3414]	@ movhi
	ldr	r2, [r3, #3368]
	str	r2, [r3, #3420]
	ldr	r2, [r3, #3396]
	str	r2, [r3, #3424]
	ldr	r2, [r3, #3372]
	str	r2, [r3, #3428]
	ldr	r2, [r3, #3388]
	str	r2, [r3, #3432]
	pop	{r3, r4, r5, r6, r7, pc}
.L1075:
	uxth	r2, r2
	ldr	r0, [r3, #464]
	mul	r5, r7, r2
	add	lr, r0, r5
	str	ip, [lr, #4]
	strh	r1, [r0, r5]	@ movhi
	ldr	r0, [r3, #464]
	add	r0, r0, r5
	ldrh	r5, [r4, #2400]
	muls	r2, r5, r2
	ldr	r5, [r3, #3400]
	bic	r2, r2, #3
	add	r2, r2, r5
	str	r2, [r0, #8]
	mov	r2, r6
	b	.L1074
.L1077:
	.align	2
.L1076:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	1
	.global	FtlVariablesInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #65535
	ldr	r6, .L1079
	movs	r4, #0
	mov	r1, r4
	ldr	r5, .L1079+4
	ldrh	r2, [r6, #2412]
	ldr	r0, [r6, #2440]
	strh	r3, [r5, #3460]	@ movhi
	mov	r3, #-1
	str	r3, [r5, #3472]
	lsls	r2, r2, #1
	str	r4, [r5, #3456]
	str	r4, [r5, #3464]
	str	r4, [r5, #3468]
	str	r4, [r6, #2248]
	strh	r4, [r6, #2438]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r6, #2334]
	mov	r1, r4
	ldr	r0, [r5, #236]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6, #2334]
	mov	r1, r4
	ldr	r0, [r5, #3356]
	lsls	r2, r2, #1
	bl	ftl_memset
	mov	r1, r4
	movs	r2, #48
	add	r0, r5, #244
	bl	ftl_memset
	mov	r2, #512
	mov	r1, r4
	add	r0, r5, #604
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1080:
	.align	2
.L1079:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	1
	.global	SupperBlkListInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r5, #0
	ldr	r6, .L1091
	movs	r2, #6
	mov	r9, r5
	mov	r10, r5
	ldr	r4, .L1091+4
	movw	fp, #65535
	ldrh	r3, [r6, #2334]
	mov	r8, r6
	.pad #20
	sub	sp, sp, #20
	movs	r1, #0
	ldr	r0, [r4, #292]
	muls	r2, r3, r2
	bl	ftl_memset
	str	r5, [r4, #312]
	str	r5, [r4, #296]
	str	r5, [r4, #304]
	strh	r5, [r4, #308]	@ movhi
	strh	r5, [r4, #316]	@ movhi
	strh	r5, [r4, #3272]	@ movhi
.L1082:
	ldrh	r3, [r8, #2332]
	sxth	r7, r5
	cmp	r7, r3
	bge	.L1089
	ldrh	r3, [r8, #2390]
	uxth	r1, r5
	ldrh	r2, [r8, #2324]
	str	r1, [sp, #4]
	str	r3, [sp]
	movs	r3, #0
	mov	r6, r3
	b	.L1090
.L1084:
	add	r0, r8, r1
	ldr	r1, [sp, #4]
	ldrb	r0, [r0, #2350]	@ zero_extendqisi2
	str	r3, [sp, #12]
	str	r2, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #12]
	cbnz	r0, .L1083
	ldr	r1, [sp]
	add	r6, r6, r1
	sxth	r6, r6
.L1083:
	adds	r3, r3, #1
.L1090:
	sxth	r1, r3
	cmp	r1, r2
	blt	.L1084
	lsls	r2, r7, #1
	cbz	r6, .L1085
	mov	r1, r6
	str	r2, [sp]
	mov	r0, #32768
	bl	__aeabi_idiv
	ldr	r2, [sp]
	sxth	r6, r0
.L1086:
	ldr	r3, [r4, #292]
	add	r2, r2, r7
	add	r3, r3, r2, lsl #1
	strh	r6, [r3, #4]	@ movhi
	ldrh	r3, [r4, #320]
	cmp	r7, r3
	beq	.L1087
	ldrh	r3, [r4, #368]
	cmp	r7, r3
	beq	.L1087
	ldrh	r3, [r4, #416]
	cmp	r7, r3
	beq	.L1087
	ldr	r3, [r4, #300]
	uxth	r0, r5
	ldrh	r3, [r3, r7, lsl #1]
	cbnz	r3, .L1088
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L1087:
	adds	r5, r5, #1
	b	.L1082
.L1085:
	ldr	r3, [r4, #300]
	strh	fp, [r3, r7, lsl #1]	@ movhi
	b	.L1086
.L1088:
	add	r10, r10, #1
	uxth	r10, r10
	bl	INSERT_DATA_LIST
	b	.L1087
.L1089:
	movs	r0, #0
	strh	r10, [r4, #308]	@ movhi
	strh	r9, [r4, #316]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1092:
	.align	2
.L1091:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	1
	.global	FtlGcPageVarInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r1, #255
	ldr	r5, .L1094
	movs	r3, #0
	ldr	r4, .L1094+4
	ldrh	r2, [r5, #2394]
	ldr	r0, [r4, #1160]
	strh	r3, [r4, #1164]	@ movhi
	lsls	r2, r2, #1
	strh	r3, [r4, #1172]	@ movhi
	bl	ftl_memset
	ldrh	r3, [r5, #2394]
	movs	r2, #12
	ldr	r0, [r4, #1168]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r3, r4, r5, lr}
	b	FtlGcBufInit
.L1095:
	.align	2
.L1094:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	1
	.global	ftl_memcpy
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	mov	r2, #2048
	ldr	r1, .L1098
	bl	ftl_memcpy
	movs	r0, #0
	pop	{r3, pc}
.L1099:
	.align	2
.L1098:
	.word	.LANCHOR2+3476
	.fnend
	.size	FlashReadIdbData, .-FlashReadIdbData
	.align	1
	.global	FlashLoadPhyInfoInRam
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashLoadPhyInfoInRam, %function
FlashLoadPhyInfoInRam:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movs	r4, #0
	ldr	r6, .L1108
	ldr	r8, .L1108+16
.L1103:
	ldrb	r2, [r6, #-1]	@ zero_extendqisi2
	mov	r1, r8
	mov	r0, r6
	lsls	r7, r4, #5
	bl	FlashMemCmp8
	mov	r5, r0
	cbnz	r0, .L1101
	ldr	r4, .L1108+4
	mov	r1, r0
	add	r6, r4, #504
	addw	r3, r4, #3256
	add	r6, r6, r7
	add	r7, r7, r4
	ldrb	r2, [r7, #526]	@ zero_extendqisi2
	mov	r0, r3
.L1102:
	lsls	r7, r1, #5
	ldrb	r7, [r7, r3]	@ zero_extendqisi2
	cmp	r7, r2
	beq	.L1105
	adds	r1, r1, #1
	cmp	r1, #4
	bne	.L1102
.L1105:
	add	r1, r0, r1, lsl #5
	movs	r2, #32
	ldr	r0, .L1108+8
	bl	ftl_memcpy
	movs	r2, #32
	mov	r1, r6
	ldr	r0, .L1108+12
	bl	ftl_memcpy
	ldrh	r0, [r4, #478]
	bl	FlashBlockAlignInit
	b	.L1100
.L1101:
	adds	r4, r4, #1
	adds	r6, r6, #32
	cmp	r4, #86
	bne	.L1103
	mov	r5, #-1
.L1100:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L1109:
	.align	2
.L1108:
	.word	.LANCHOR1+505
	.word	.LANCHOR1
	.word	.LANCHOR0+52
	.word	.LANCHOR1+468
	.word	.LANCHOR0+2072
	.fnend
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
	.align	1
	.global	NandcCopy1KB
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	add	r5, r0, #512
	add	r0, r2, r4, lsl #9
	ldr	r6, [sp, #16]
	bne	.L1111
	cbz	r3, .L1112
	mov	r2, #1024
	mov	r1, r3
	bl	ftl_memcpy
.L1112:
	cbz	r6, .L1110
	lsrs	r4, r4, #1
	ldr	r3, [r6]	@ unaligned
	add	r4, r4, r4, lsl #1
	lsls	r4, r4, #2
	str	r3, [r5, r4, lsl #2]
	pop	{r4, r5, r6, pc}
.L1111:
	cbz	r3, .L1115
	mov	r1, r0
	mov	r2, #1024
	mov	r0, r3
	bl	ftl_memcpy
.L1115:
	cbz	r6, .L1110
	lsrs	r4, r4, #1
	add	r4, r4, r4, lsl #1
	lsls	r4, r4, #2
	ldr	r3, [r5, r4, lsl #2]
	strb	r3, [r6]
	lsrs	r2, r3, #8
	strb	r2, [r6, #1]
	lsrs	r2, r3, #16
	lsrs	r3, r3, #24
	strb	r2, [r6, #2]
	strb	r3, [r6, #3]
.L1110:
	pop	{r4, r5, r6, pc}
	.fnend
	.size	NandcCopy1KB, .-NandcCopy1KB
	.align	1
	.global	ftl_memcpy32
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movs	r3, #0
	push	{r4, lr}
	.save {r4, lr}
.L1127:
	cmp	r3, r2
	bne	.L1128
	pop	{r4, pc}
.L1128:
	ldr	r4, [r1, r3, lsl #2]
	str	r4, [r0, r3, lsl #2]
	adds	r3, r3, #1
	b	.L1127
	.fnend
	.size	ftl_memcpy32, .-ftl_memcpy32
	.align	1
	.global	ftl_memcmp
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	timer_get_time, %function
timer_get_time:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1131
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1132:
	.align	2
.L1131:
	.word	jiffies
	.fnend
	.size	timer_get_time, .-timer_get_time
	.align	1
	.global	FlashSramLoadStore
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashSramLoadStore, %function
FlashSramLoadStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	push	{r4, r5}
	.save {r4, r5}
	ldr	r4, .L1136
	ldr	r4, [r4, #1180]
	add	r4, r4, #4096
	add	r4, r4, r1
	cbnz	r2, .L1134
	mov	r2, r3
	mov	r1, r4
.L1135:
	pop	{r4, r5}
	b	ftl_memcpy
.L1134:
	mov	r1, r0
	mov	r2, r3
	mov	r0, r4
	b	.L1135
.L1137:
	.align	2
.L1136:
	.word	.LANCHOR4
	.fnend
	.size	FlashSramLoadStore, .-FlashSramLoadStore
	.align	1
	.global	FlashCs123Init
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.global	ftl_dma32_malloc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_dma32_malloc, %function
ftl_dma32_malloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #8192
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ble	.L1140
	pop	{r3, r4, r5, lr}
	b	ftl_malloc
.L1140:
	ldr	r5, .L1142
	adds	r4, r4, #63
	bic	r4, r4, #63
	ldr	r3, [r5, #1184]
	cmp	r4, r3
	ble	.L1141
	mov	r0, #16384
	bl	ftl_malloc
	mov	r3, #16384
	str	r0, [r5, #1188]
	str	r3, [r5, #1184]
.L1141:
	ldr	r3, [r5, #1184]
	ldr	r0, [r5, #1188]
	subs	r3, r3, r4
	add	r4, r4, r0
	str	r3, [r5, #1184]
	str	r4, [r5, #1188]
	pop	{r3, r4, r5, pc}
.L1143:
	.align	2
.L1142:
	.word	.LANCHOR4
	.fnend
	.size	ftl_dma32_malloc, .-ftl_dma32_malloc
	.align	1
	.global	rk_nand_suspend
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1147
	ldr	r0, [r3, #2432]
	bx	lr
.L1148:
	.align	2
.L1147:
	.word	.LANCHOR0
	.fnend
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	1
	.global	rk_nandc_get_irq_status
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r5, #0
	ldr	r7, .L1161
	mov	r10, r0
	mov	r6, r1
	mov	r8, r2
	ldr	fp, .L1161+4
	mov	r9, r3
	mov	r4, r5
.L1153:
	cmp	r4, r9
	bne	.L1159
	ldr	r1, .L1161+4
	ldr	r0, .L1161+8
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L1159:
	cbnz	r5, .L1154
	mov	r3, r4
	mov	r2, r6
	mov	r1, r10
	ldr	r0, .L1161+12
	bl	printk
.L1154:
	cmp	r8, #4
	bne	.L1155
	ldr	r1, [r6, r4, lsl #2]
.L1160:
	mov	r0, r7
	adds	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1158
	movs	r5, #0
	mov	r1, fp
	ldr	r0, .L1161+8
	bl	printk
.L1158:
	adds	r4, r4, #1
	b	.L1153
.L1155:
	cmp	r8, #2
	ite	eq
	ldrsheq	r1, [r6, r4, lsl #1]
	ldrbne	r1, [r6, r4]	@ zero_extendqisi2
	b	.L1160
.L1162:
	.align	2
.L1161:
	.word	.LC83
	.word	.LC84
	.word	.LC77
	.word	.LC82
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	1
	.global	HynixGetReadRetryDefault
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r3, #172
	ldr	r7, .L1279
	cmp	r0, #2
	mov	r1, #173
	mov	r2, #174
	.pad #60
	sub	sp, sp, #60
	mov	r4, r0
	strb	r3, [r7, #1220]
	mov	r3, #175
	strb	r0, [r7, #1216]
	strb	r1, [r7, #1221]
	strb	r2, [r7, #1222]
	strb	r3, [r7, #1223]
	bne	.L1164
	movs	r3, #167
	movs	r5, #7
	strb	r3, [r7, #1220]
	movs	r2, #247
	ldr	r3, .L1279+4
	strb	r2, [r3, #3401]
.L1229:
	movs	r6, #4
	b	.L1165
.L1164:
	cmp	r0, #3
	bne	.L1166
	movs	r3, #176
	strb	r3, [r7, #1220]
	movs	r3, #177
	strb	r3, [r7, #1221]
	movs	r3, #178
	strb	r3, [r7, #1222]
	movs	r3, #179
	strb	r3, [r7, #1223]
	movs	r3, #180
	strb	r3, [r7, #1224]
	movs	r3, #181
	strb	r3, [r7, #1225]
	movs	r3, #182
	strb	r3, [r7, #1226]
	movs	r3, #183
.L1274:
	movs	r5, #8
	strb	r3, [r7, #1227]
	mov	r6, r5
.L1165:
	subs	r3, r4, #1
	cmp	r3, #1
	bhi	.L1171
	ldr	fp, .L1279+12
	mov	r10, #0
	ldr	r2, .L1279+8
.L1172:
	ldrb	r1, [r7, #2234]	@ zero_extendqisi2
	uxtb	r3, r10
	cmp	r1, r3
	bhi	.L1178
.L1179:
	ldr	r3, .L1279
	strb	r6, [r3, #1217]
	strb	r5, [r3, #1218]
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1166:
	cmp	r0, #4
	bne	.L1167
	movs	r0, #204
	strb	r1, [r7, #1225]
	strb	r0, [r7, #1220]
	movs	r0, #191
	strb	r0, [r7, #1221]
	movs	r0, #170
	strb	r0, [r7, #1222]
	movs	r0, #171
	strb	r0, [r7, #1223]
	movs	r0, #205
	strb	r0, [r7, #1224]
	strb	r2, [r7, #1226]
	b	.L1274
.L1167:
	cmp	r0, #5
	bne	.L1168
	movs	r3, #56
	movs	r5, #8
	strb	r3, [r7, #1220]
	movs	r3, #57
	strb	r3, [r7, #1221]
	movs	r3, #58
	strb	r3, [r7, #1222]
	movs	r3, #59
	strb	r3, [r7, #1223]
	b	.L1229
.L1168:
	cmp	r0, #6
	bne	.L1169
	movs	r3, #14
	movs	r5, #12
	strb	r3, [r7, #1220]
	movs	r3, #15
	strb	r3, [r7, #1221]
	movs	r3, #16
	strb	r3, [r7, #1222]
	movs	r3, #17
	strb	r3, [r7, #1223]
	b	.L1229
.L1169:
	cmp	r0, #7
	bne	.L1170
	movs	r3, #176
	movs	r5, #12
	strb	r3, [r7, #1220]
	movs	r3, #177
	strb	r3, [r7, #1221]
	movs	r3, #178
	strb	r3, [r7, #1222]
	movs	r3, #179
	strb	r3, [r7, #1223]
	movs	r3, #180
	strb	r3, [r7, #1224]
	movs	r3, #181
	strb	r3, [r7, #1225]
	movs	r3, #182
	strb	r3, [r7, #1226]
	movs	r3, #183
	strb	r3, [r7, #1227]
	movs	r3, #212
	strb	r3, [r7, #1228]
	movs	r3, #213
	strb	r3, [r7, #1229]
	movs	r6, #10
	b	.L1165
.L1170:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1229
	movs	r3, #6
	strb	r5, [r7, #1221]
	strb	r3, [r7, #1220]
	movs	r3, #9
	strb	r3, [r7, #1223]
	movs	r3, #10
	strb	r0, [r7, #1222]
	movs	r5, #50
	strb	r3, [r7, #1224]
	movs	r6, #5
	b	.L1165
.L1178:
	add	r3, r3, r7
	mov	r8, #0
	ldrb	r3, [r3, #2236]	@ zero_extendqisi2
	ldr	r1, [r7, r3, lsl #3]
	add	r4, fp, r3, lsl #6
	add	r3, r7, r3, lsl #3
	ldrb	r9, [r3, #4]	@ zero_extendqisi2
	adds	r4, r4, #20
	add	r9, r1, r9, lsl #8
	movs	r1, #55
	addw	r3, r9, #2056
.L1173:
	add	r0, fp, r8
	str	r1, [r3]
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	str	r2, [sp, #8]
	str	r1, [sp, #4]
	str	r0, [r9, #2052]
	movs	r0, #80
	str	r3, [sp]
	bl	ndelay
	ldr	r0, [r9, #2048]
	ldr	r3, [sp]
	ldr	r1, [sp, #4]
	strb	r0, [r4, r8]
	add	r8, r8, #1
	uxtb	r0, r8
	ldr	r2, [sp, #8]
	cmp	r6, r0
	bhi	.L1173
	mov	r0, r4
	movs	r1, #0
.L1176:
	movs	r3, #1
	add	lr, r2, r1
.L1175:
	ldrb	ip, [lr, r3, lsl #2]	@ zero_extendqisi2
	ldrb	r8, [r0]	@ zero_extendqisi2
	add	ip, ip, r8
	strb	ip, [r0, r3, lsl #3]
	adds	r3, r3, #1
	cmp	r3, #7
	bne	.L1175
	adds	r1, r1, #1
	adds	r0, r0, #1
	cmp	r1, #4
	bne	.L1176
	movs	r3, #0
	add	r10, r10, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1172
.L1171:
	subs	r3, r4, #3
	cmp	r3, #5
	bhi	.L1179
	smulbb	r3, r6, r5
	ldr	r8, .L1279
	asrs	r2, r3, #1
	lsls	r3, r3, #4
	str	r3, [sp, #48]
	lsls	r3, r2, #2
	str	r2, [sp, #8]
	str	r3, [sp, #44]
	lsls	r3, r2, #1
	str	r3, [sp, #28]
	movs	r3, #0
	str	r3, [sp, #24]
.L1180:
	ldrb	r3, [sp, #24]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r2, [sp, #12]
	ldrb	r3, [r8, #2234]	@ zero_extendqisi2
	cmp	r3, r2
	bls	.L1179
	ldr	r3, [sp, #12]
	add	r3, r8, r3
	ldrb	r10, [r3, #2236]	@ zero_extendqisi2
	ldr	fp, [r8, r10, lsl #3]
	mov	r0, r10
	add	r3, r8, r10, lsl #3
	ldrb	r9, [r3, #4]	@ zero_extendqisi2
	movs	r3, #255
	add	r7, fp, r9, lsl #8
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	cmp	r4, #7
	bne	.L1181
	ldr	r3, .L1279+12
	movs	r0, #160
	mla	r0, r0, r10, r3
	add	r3, r0, #28
.L1275:
	str	r3, [sp, #16]
	cmp	r4, #4
	add	r3, fp, r9, lsl #8
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L1184
	movs	r2, #255
	str	r2, [r3, #2052]
	movs	r2, #64
	str	r2, [r3, #2048]
	movs	r2, #204
.L1276:
	str	r2, [r3, #2052]
	movs	r2, #77
	b	.L1277
.L1181:
	cmp	r4, #8
	beq	.L1183
	ldr	r0, .L1279+12
	add	r0, r0, r10, lsl #6
	add	r3, r0, #20
	b	.L1275
.L1280:
	.align	2
.L1279:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3384
	.word	.LANCHOR0+1216
.L1184:
	subs	r2, r4, #5
	cmp	r2, #1
	bhi	.L1186
	ldrb	r2, [r8, #1220]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	movs	r2, #82
.L1277:
	str	r2, [r3, #2048]
.L1185:
	add	r3, fp, r9, lsl #8
	movs	r2, #22
	cmp	r4, #6
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
.L1228:
	add	r3, fp, r9, lsl #8
	movs	r2, #48
	mov	r0, r10
	str	r2, [r3, #2056]
	bl	NandcWaitFlashReady
	subs	r3, r4, #5
	cmp	r3, #1
	str	r3, [sp, #32]
	bls	.L1231
	cmp	r4, #8
	beq	.L1231
	cmp	r4, #7
	ite	eq
	moveq	r2, #32
	movne	r2, #2
.L1189:
	ldr	r3, .L1281
	subs	r2, r2, #1
	add	r7, fp, r9, lsl #8
	ldr	r3, [r3, #1192]
	str	r7, [sp, #4]
	subs	r1, r3, #1
	uxtab	r2, r3, r2
	mov	r0, r1
.L1190:
	ldr	r7, [sp, #4]
	ldr	r7, [r7, #2048]
	strb	r7, [r0, #1]!
	cmp	r0, r2
	bne	.L1190
	cmp	r4, #8
	bne	.L1191
	movs	r2, #0
.L1193:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #50
	beq	.L1192
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #5
	beq	.L1192
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1193
.L1194:
	movs	r1, #0
	ldr	r0, .L1281+4
	bl	printk
.L1196:
	b	.L1196
.L1186:
	cmp	r4, #7
	bne	.L1185
	movs	r2, #174
	str	r2, [r3, #2052]
	movs	r2, #0
	str	r2, [r3, #2048]
	movs	r2, #176
	b	.L1276
.L1231:
	movs	r2, #16
	b	.L1189
.L1192:
	cmp	r1, #6
	bhi	.L1194
.L1195:
	ldr	r3, .L1281
	ldr	r3, [r3, #1192]
	str	r3, [sp]
.L1205:
	ldr	r2, [sp]
	ldr	r0, [sp, #48]
	subs	r1, r3, r2
	cmp	r1, r0
	blt	.L1206
	ldr	r3, .L1281
	ldr	r1, [r3, #1192]
	ldr	r3, [sp, #28]
	adds	r0, r1, r3
	movs	r3, #8
.L1208:
	mov	ip, r0
	movs	r7, #0
.L1207:
	ldr	r2, [sp, #8]
	adds	r7, r7, #1
	ldrh	lr, [ip]
	cmp	r2, r7
	mvn	lr, lr
	strh	lr, [ip], #2	@ movhi
	bgt	.L1207
	ldr	r2, [sp, #44]
	subs	r3, r3, #1
	add	r0, r0, r2
	bne	.L1208
	str	r3, [sp, #20]
.L1214:
	movs	r7, #0
	mov	r0, r7
.L1213:
	movs	r2, #1
	mov	ip, r1
	lsls	r2, r2, r0
	mov	lr, #0
	str	r2, [sp, #36]
	movs	r2, #16
	str	r2, [sp, #40]
.L1211:
	ldrh	r2, [ip]
	str	r2, [sp, #52]
	ldr	r2, [sp, #36]
	mov	r3, r2
	ldr	r2, [sp, #52]
	bics	r3, r3, r2
	ldr	r2, [sp, #28]
	it	eq
	addeq	lr, lr, #1
	add	ip, ip, r2
	ldr	r2, [sp, #40]
	subs	r2, r2, #1
	str	r2, [sp, #40]
	bne	.L1211
	cmp	lr, #8
	add	r0, r0, #1
	ittt	hi
	ldrhi	r2, [sp, #36]
	orrhi	r7, r7, r2
	uxthhi	r7, r7
	cmp	r0, #16
	bne	.L1213
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #8]
	strh	r7, [r1], #2	@ movhi
	adds	r3, r3, #1
	cmp	r2, r3
	str	r3, [sp, #20]
	bgt	.L1214
	ldr	r3, .L1281
	ldr	r1, [r3, #1192]
	movs	r3, #0
	subs	r0, r1, #4
	add	r7, r1, #28
.L1217:
	ldr	ip, [r0, #4]!
	cmp	ip, #0
	bne	.L1216
	adds	r3, r3, #1
.L1216:
	cmp	r7, r0
	bne	.L1217
	cmp	r3, #7
	ble	.L1218
	ldr	r0, .L1281+8
	mov	r3, #1024
	movs	r2, #1
	bl	rknand_print_hex
	movs	r1, #0
	ldr	r0, .L1281+4
	bl	printk
.L1219:
	b	.L1219
.L1191:
	cmp	r4, #7
	bne	.L1197
	movs	r2, #0
.L1199:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #12
	beq	.L1198
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #10
	beq	.L1198
	adds	r2, r2, #1
	cmp	r2, #8
	bne	.L1199
.L1200:
	movs	r1, #0
	ldr	r0, .L1281+4
	bl	printk
.L1201:
	b	.L1201
.L1198:
	cmp	r1, #6
	bls	.L1195
	b	.L1200
.L1197:
	cmp	r4, #6
	bne	.L1195
	adds	r3, r3, #7
.L1202:
	ldrb	r2, [r1, #1]!	@ zero_extendqisi2
	cmp	r2, #12
	beq	.L1195
	ldrb	r2, [r1, #8]	@ zero_extendqisi2
	cmp	r2, #4
	beq	.L1195
	cmp	r1, r3
	bne	.L1202
	movs	r1, #0
	ldr	r0, .L1281+4
	bl	printk
.L1204:
	b	.L1204
.L1206:
	ldr	r1, [sp, #4]
	ldr	r1, [r1, #2048]
	strb	r1, [r3], #1
	b	.L1205
.L1218:
	cmp	r4, #6
	beq	.L1233
	cmp	r4, #7
	beq	.L1234
	cmp	r4, #8
	ite	eq
	moveq	r7, #5
	movne	r7, #8
.L1220:
	subs	r3, r6, #1
	ldr	r0, [sp, #16]
	uxtb	r3, r3
	mov	ip, #0
	adds	r3, r3, #1
	mov	r2, r3
.L1221:
	ldr	r1, [sp]
	str	r0, [sp, #16]
.L1222:
	ldr	r3, [sp, #16]
	ldrb	lr, [r1], #1	@ zero_extendqisi2
	strb	lr, [r3], #1
	str	r3, [sp, #16]
	ldr	r3, [sp]
	sub	lr, r1, r3
	uxtb	lr, lr
	cmp	r6, lr
	bhi	.L1222
	ldr	r1, [sp]
	add	ip, ip, #1
	cmp	r5, ip
	add	r0, r0, r7
	add	r1, r1, r2
	str	r1, [sp]
	bgt	.L1221
	add	r9, fp, r9, lsl #8
	movs	r3, #255
	mov	r0, r10
	str	r3, [r9, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #32]
	cmp	r3, #1
	bhi	.L1224
	movs	r3, #54
	ldr	r2, [sp, #4]
	str	r3, [r9, #2056]
	mov	r1, #-1
	ldrb	r3, [r8, #1220]	@ zero_extendqisi2
	ldr	r0, [sp, #12]
	str	r3, [r2, #2052]
	movs	r3, #0
	str	r3, [r2, #2048]
	movs	r3, #22
	str	r3, [r9, #2056]
	bl	FlashReadCmd
.L1225:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp, #24]
	b	.L1180
.L1233:
	movs	r7, #4
	b	.L1220
.L1234:
	movs	r7, #10
	b	.L1220
.L1224:
	cmp	r4, #8
	ite	eq
	moveq	r3, #190
	movne	r3, #56
	str	r3, [r9, #2056]
	b	.L1225
.L1183:
	movs	r3, #120
	movs	r2, #23
	str	r3, [r7, #2056]
	movs	r3, #0
	str	r3, [r7, #2052]
	movs	r1, #25
	str	r3, [r7, #2052]
	str	r3, [r7, #2052]
	str	r2, [r7, #2056]
	movs	r2, #4
	str	r2, [r7, #2056]
	str	r1, [r7, #2056]
	movs	r1, #218
	str	r1, [r7, #2056]
	movs	r1, #21
	str	r3, [r7, #2056]
	str	r3, [r7, #2052]
	str	r3, [r7, #2052]
	str	r1, [r7, #2052]
	str	r2, [r7, #2052]
	str	r3, [r7, #2052]
	ldr	r3, .L1281+12
	str	r3, [sp, #16]
	b	.L1228
.L1282:
	.align	2
.L1281:
	.word	.LANCHOR4
	.word	.LC85
	.word	.LC86
	.word	.LANCHOR0+1244
	.fnend
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	1
	.global	FlashGetReadRetryDefault
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r0
	cmp	r0, #0
	beq	.L1283
	subs	r2, r0, #1
	cmp	r2, #7
	bhi	.L1285
	b	HynixGetReadRetryDefault
.L1285:
	cmp	r0, #49
	bne	.L1286
	ldr	r0, .L1306
	movs	r2, #64
	ldr	r1, .L1306+4
	strb	r3, [r0, #1216]
	movs	r3, #4
	strb	r3, [r0, #1217]
	movs	r3, #15
	strb	r3, [r0, #1218]
.L1304:
	addw	r0, r0, #1220
	b	ftl_memcpy
.L1286:
	cmp	r0, #33
	beq	.L1287
	sub	r2, r0, #65
	cmp	r2, #1
	bhi	.L1288
.L1287:
	ldr	r0, .L1306
	strb	r3, [r0, #1216]
	movs	r3, #4
.L1305:
	strb	r3, [r0, #1217]
	movs	r3, #7
	strb	r3, [r0, #1218]
	movs	r2, #45
	ldr	r1, .L1306+8
	b	.L1304
.L1288:
	cmp	r0, #34
	beq	.L1289
	cmp	r0, #67
	bne	.L1290
.L1289:
	ldr	r0, .L1306
	strb	r3, [r0, #1216]
	movs	r3, #5
	b	.L1305
.L1290:
	cmp	r0, #35
	beq	.L1291
	cmp	r0, #68
	bne	.L1283
.L1291:
	ldr	r0, .L1306
	movs	r2, #95
	ldr	r1, .L1306+12
	strb	r3, [r0, #1216]
	movs	r3, #5
	strb	r3, [r0, #1217]
	movs	r3, #17
	strb	r3, [r0, #1218]
	b	.L1304
.L1283:
	bx	lr
.L1307:
	.align	2
.L1306:
	.word	.LANCHOR0
	.word	.LANCHOR1+404
	.word	.LANCHOR1+256
	.word	.LANCHOR1+301
	.fnend
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	1
	.global	NandcXferComp
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcXferComp, %function
NandcXferComp:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	ldr	r5, .L1347
	ldr	r3, [r5, #2264]
	ldr	r4, [r5, r0, lsl #3]
	cmp	r3, #3
	bls	.L1339
	ldr	r3, [r4, #16]
	lsls	r6, r3, #29
	bpl	.L1339
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	str	r3, [sp]
	cmp	r6, #0
	beq	.L1310
	ldr	r7, .L1347+4
	movs	r6, #0
	ldr	r8, .L1347+12
.L1311:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1319
	ldr	r3, [r5, #2264]
	cmp	r3, #5
	bhi	.L1312
.L1315:
	adds	r6, r6, #1
	bics	r3, r6, #-16777216
	bne	.L1314
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	mov	r0, r7
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	bl	printk
	mov	r3, #512
	movs	r2, #4
	mov	r1, r4
	mov	r0, r8
	bl	rknand_print_hex
.L1314:
	movs	r1, #5
	movs	r0, #1
	bl	usleep_range
	b	.L1311
.L1312:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	lsls	r0, r3, #18
	bpl	.L1315
	ldr	r3, [sp, #4]
	lsls	r1, r3, #14
	bpl	.L1315
.L1319:
	ldr	r3, [r5, #2300]
	cbz	r3, .L1320
	ldr	r1, [sp]
	movs	r2, #0
	ldr	r0, [r5, #2292]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	movs	r2, #0
	ldr	r0, [r5, #2296]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
.L1320:
	movs	r3, #0
	str	r3, [r5, #2300]
.L1308:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1310:
	ldr	r7, .L1347+8
	ldr	r8, .L1347+12
.L1321:
	ldr	r3, [sp]
	lsls	r2, r3, #11
	bpl	.L1323
	ldr	r3, [r5, #2308]
	cbz	r3, .L1324
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1324:
	ldr	r3, [r5, #2300]
	cbz	r3, .L1325
	ldr	r1, [sp]
	movs	r2, #1
	ldr	r0, [r5, #2292]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	movs	r2, #1
	ldr	r0, [r5, #2296]
	ubfx	r1, r1, #22, #5
	lsls	r1, r1, #7
	bl	rknand_dma_unmap_single
.L1325:
	ldr	r3, [r5, #2308]
	cmp	r3, #0
	beq	.L1320
	mov	r0, r4
	bl	NandcSendDumpDataDone
	b	.L1320
.L1323:
	ldr	r3, [r4, #8]
	adds	r6, r6, #1
	str	r3, [sp]
	bics	r3, r6, #-16777216
	bne	.L1322
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	mov	r0, r7
	ubfx	r3, r3, #16, #5
	bl	printk
	mov	r3, #512
	movs	r2, #4
	mov	r1, r4
	mov	r0, r8
	bl	rknand_print_hex
.L1322:
	movs	r1, #5
	movs	r0, #1
	bl	usleep_range
	b	.L1321
.L1339:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	lsls	r3, r3, #11
	bpl	.L1339
	b	.L1308
.L1348:
	.align	2
.L1347:
	.word	.LANCHOR0
	.word	.LC87
	.word	.LC89
	.word	.LC88
	.fnend
	.size	NandcXferComp, .-NandcXferComp
	.align	1
	.global	NandcXferData
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	NandcXferData, %function
NandcXferData:
	.fnstart
	@ args = 4, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L1385
	tst	r8, #63
	.pad #92
	sub	sp, sp, #92
	mov	r7, r0
	mov	r5, r1
	mov	r10, r2
	ldr	fp, [sp, #128]
	mov	r9, r3
	ldr	r6, [r3, r0, lsl #3]
	bne	.L1350
	cmp	fp, #0
	bne	.L1351
	movs	r2, #64
	movs	r1, #255
	add	r0, sp, #24
	bl	ftl_memset
	add	fp, sp, #24
.L1351:
	mov	r1, r5
	mov	r0, r7
	stm	sp, {r8, fp}
	movs	r3, #0
	mov	r2, r10
	bl	NandcXferStart
	mov	r1, r5
	mov	r0, r7
	bl	NandcXferComp
	cmp	r5, #0
	bne	.L1375
	ldr	r3, [r9, #2312]
	lsr	r0, r10, #1
	mov	r2, r5
	cmp	r3, #25
	mov	r3, r5
	ite	cc
	movcc	r4, #64
	movcs	r4, #128
.L1354:
	cmp	r2, r0
	add	fp, fp, #4
	add	r7, r4, r3
	bcc	.L1355
	movs	r2, #0
	lsr	r0, r10, #2
	ldr	r1, [r9, #2312]
	ldr	r4, [r9, #2264]
	mov	r9, r2
.L1356:
	cmp	r2, r0
	bcs	.L1352
	cbnz	r1, .L1362
.L1352:
	movs	r3, #0
	str	r3, [r6, #16]
.L1363:
	ldr	r3, .L1385
	ldr	r3, [r3, #2264]
	cmp	r3, #5
	bls	.L1349
	cbnz	r5, .L1349
	ldr	r3, [r6]
	and	r2, r3, #139264
	cmp	r2, #139264
	ittt	eq
	moveq	r9, #-1
	orreq	r3, r3, #131072
	streq	r3, [r6]
.L1349:
	mov	r0, r9
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1355:
	ldr	r1, [r9, #2280]
	bic	r3, r3, #3
	adds	r2, r2, #1
	ldr	r3, [r1, r3]
	strb	r3, [fp, #-4]
	lsrs	r1, r3, #8
	strb	r1, [fp, #-3]
	lsrs	r1, r3, #16
	lsrs	r3, r3, #24
	strb	r1, [fp, #-2]
	strb	r3, [fp, #-1]
	mov	r3, r7
	b	.L1354
.L1362:
	add	r3, r2, #8
	ldr	r3, [r6, r3, lsl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	lsls	r7, r3, #29
	bmi	.L1378
	ldr	r3, [sp, #20]
	ubfx	r3, r3, #15, #1
	cmp	r3, #0
	bne	.L1378
	cmp	r4, #5
	bls	.L1358
	ldr	r7, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r7, r7, #3, #5
	ubfx	lr, r3, #27, #1
	ldr	r3, [sp, #20]
	ldr	ip, [sp, #20]
	orr	r7, r7, lr, lsl #5
	ubfx	r3, r3, #16, #5
	ubfx	ip, ip, #29, #1
	orr	r3, r3, ip, lsl #5
	cmp	r7, r3
	ldr	r3, [sp, #20]
	itete	hi
	ldrhi	r7, [sp, #20]
	ldrls	r7, [sp, #20]
	ubfxhi	r3, r3, #3, #5
	ubfxls	r3, r3, #16, #5
	ite	hi
	ubfxhi	r7, r7, #27, #1
	ubfxls	r7, r7, #29, #1
.L1384:
	orr	r3, r3, r7, lsl #5
.L1360:
	cmp	r9, r3
	it	cc
	movcc	r9, r3
.L1357:
	adds	r2, r2, #1
	b	.L1356
.L1358:
	cmp	r4, #3
	bls	.L1360
	ldr	r7, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r7, r7, #3, #5
	ubfx	lr, r3, #28, #1
	ldr	r3, [sp, #20]
	ldr	ip, [sp, #20]
	orr	r7, r7, lr, lsl #5
	ubfx	r3, r3, #16, #5
	ubfx	ip, ip, #30, #1
	orr	r3, r3, ip, lsl #5
	cmp	r7, r3
	ldr	r3, [sp, #20]
	itete	hi
	ldrhi	r7, [sp, #20]
	ldrls	r7, [sp, #20]
	ubfxhi	r3, r3, #3, #5
	ubfxls	r3, r3, #16, #5
	ite	hi
	ubfxhi	r7, r7, #28, #1
	ubfxls	r7, r7, #30, #1
	b	.L1384
.L1378:
	mov	r9, #-1
	b	.L1357
.L1375:
	mov	r9, #0
	b	.L1352
.L1350:
	cmp	r1, #1
	bne	.L1364
	cmp	fp, #0
	mov	r4, fp
	mov	r9, #0
	ite	ne
	movne	r3, #4
	moveq	r3, #0
	str	r3, [sp, #8]
.L1365:
	cmp	r9, r10
	bcc	.L1367
	mov	r9, #0
	b	.L1363
.L1367:
	and	fp, r9, #3
	cmp	r8, #0
	beq	.L1380
	add	r3, r8, r9, lsl #9
.L1366:
	str	r4, [sp]
	mov	r2, fp
	movs	r1, #1
	mov	r0, r6
	bl	NandcCopy1KB
	movs	r3, #0
	movs	r2, #2
	str	r3, [sp, #4]
	movs	r1, #1
	str	r3, [sp]
	mov	r0, r7
	mov	r3, fp
	add	r9, r9, #2
	bl	NandcXferStart
	movs	r1, #1
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [sp, #8]
	add	r4, r4, r3
	b	.L1365
.L1380:
	mov	r3, r8
	b	.L1366
.L1364:
	movs	r4, #0
	movs	r2, #2
	mov	r3, r4
	str	r4, [sp, #4]
	str	r4, [sp]
	mov	r1, r4
	bl	NandcXferStart
	mov	r9, r4
	cmp	fp, r4
	ite	ne
	movne	r3, #4
	moveq	r3, r4
	str	r3, [sp, #12]
	str	r8, [sp, #8]
.L1368:
	cmp	r4, r10
	bcs	.L1363
	mov	r1, r5
	mov	r0, r7
	bl	NandcXferComp
	adds	r4, r4, #2
	ldr	r3, [r6, #32]
	cmp	r10, r4
	str	r3, [sp, #20]
	bls	.L1369
	movs	r3, #0
	movs	r2, #2
	str	r3, [sp, #4]
	movs	r1, #0
	str	r3, [sp]
	mov	r0, r7
	and	r3, r4, #3
	bl	NandcXferStart
.L1369:
	ldr	r3, [sp, #20]
	lsls	r3, r3, #29
	bmi	.L1381
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #20]
	ubfx	r3, r3, #3, #5
	ubfx	r2, r2, #27, #1
	orr	r3, r3, r2, lsl #5
	cmp	r9, r3
	it	cc
	movcc	r9, r3
.L1370:
	cmp	r8, #0
	sub	r2, r4, #2
	ldr	r3, [sp, #8]
	and	r2, r2, #3
	str	fp, [sp]
	it	eq
	moveq	r3, #0
	movs	r1, #0
	mov	r0, r6
	bl	NandcCopy1KB
	ldr	r3, [sp, #8]
	add	r3, r3, #1024
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	add	fp, fp, r3
	b	.L1368
.L1381:
	mov	r9, #-1
	b	.L1370
.L1386:
	.align	2
.L1385:
	.word	.LANCHOR0
	.fnend
	.size	NandcXferData, .-NandcXferData
	.align	1
	.global	FlashReadRawPage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadRawPage, %function
FlashReadRawPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r3, .L1389
	mov	r6, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r5, [r3, #477]	@ zero_extendqisi2
	cbnz	r0, .L1388
	ldr	r1, .L1389+4
	ldrb	r3, [r1, #37]	@ zero_extendqisi2
	ldr	r0, [r1, #40]
	muls	r0, r3, r0
	cmp	r0, r6
	it	hi
	movhi	r5, #4
.L1388:
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r1, r6
	mov	r0, r4
	bl	FlashReadCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r3, r7
	mov	r2, r5
	str	r8, [sp]
	movs	r1, #0
	mov	r0, r4
	bl	NandcXferData
	mov	r1, r0
	mov	r0, r4
	bl	NandcFlashDeCs
	mov	r0, r1
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1390:
	.align	2
.L1389:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashReadRawPage, .-FlashReadRawPage
	.align	1
	.global	FlashDdrTunningRead
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashDdrTunningRead, %function
FlashDdrTunningRead:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r3
	ldr	r4, .L1416
	.pad #20
	sub	sp, sp, #20
	mov	fp, r2
	stm	sp, {r0, r1}
	ldr	r3, [r4, #88]
	ldr	r3, [r3, #304]
	str	r3, [sp, #12]
	ldr	r3, [r4, #2264]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	ite	cc
	movcc	r10, #6
	movcs	r10, #12
	cmp	r3, #0
	beq	.L1405
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r0, [sp]
	bl	FlashReset
	mov	r3, r7
	mov	r2, fp
	ldm	sp, {r0, r1}
	bl	FlashReadRawPage
	mov	r5, r0
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	NandcSetMode
	adds	r3, r5, #1
	bne	.L1394
.L1403:
	mov	r5, #-1
.L1391:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1394:
	mov	r2, r5
	ldr	r1, [sp, #4]
	ldr	r0, .L1416+4
	bl	printk
	cmp	r5, #9
	bhi	.L1396
	ldr	r3, [sp]
	ldr	r3, [r4, r3, lsl #3]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3]
	orr	r2, r2, #131072
	str	r2, [r3]
.L1396:
	ldr	r2, .L1416+8
	ldr	r3, [r2, #1196]
	adds	r3, r3, #1
	cmp	r3, #2048
	str	r3, [r2, #1196]
	bcc	.L1391
	movs	r7, #0
	mov	fp, r7
	str	r7, [r2, #1196]
.L1393:
	mov	r9, #0
	mov	r8, #-1
	mov	r6, r9
	mov	r4, r9
	str	r9, [sp, #8]
.L1401:
	uxtb	r0, r10
	bl	NandcSetDdrPara
	mov	r3, r7
	mov	r2, fp
	ldm	sp, {r0, r1}
	bl	FlashReadRawPage
	adds	r3, r5, #1
	cmp	r0, r3
	bhi	.L1397
	cmp	r0, #2
	bhi	.L1407
	adds	r4, r4, #1
	cmp	r4, #9
	bls	.L1407
	mov	r3, r6
	mov	r5, r0
	sub	r6, r10, r4
	mov	r8, #0
.L1399:
	ldr	r2, [sp, #8]
	cmp	r4, r2
	it	ls
	movls	r6, r3
.L1400:
	cbz	r6, .L1402
	mov	r1, r6
	ldr	r0, .L1416+12
	bl	printk
	uxtb	r0, r6
	bl	NandcSetDdrPara
.L1402:
	cmp	r8, #0
	beq	.L1391
	ldm	sp, {r1, r2}
	ldr	r0, .L1416+16
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1403
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
	b	.L1391
.L1405:
	mov	r5, #1024
	b	.L1393
.L1397:
	ldr	r3, [sp, #8]
	cmp	r4, r3
	bls	.L1408
	cmp	r4, #7
	sub	r6, r9, r4
	bhi	.L1400
	str	r4, [sp, #8]
.L1408:
	movs	r4, #0
	b	.L1398
.L1407:
	mov	r8, #0
	mov	r9, r10
	mov	r5, r0
	mov	r7, r8
	mov	fp, r8
.L1398:
	add	r10, r10, #2
	cmp	r10, #69
	bls	.L1401
	mov	r3, r6
	mov	r6, r9
	b	.L1399
.L1417:
	.align	2
.L1416:
	.word	.LANCHOR0
	.word	.LC90
	.word	.LANCHOR4
	.word	.LC91
	.word	.LC92
	.fnend
	.size	FlashDdrTunningRead, .-FlashDdrTunningRead
	.align	1
	.global	FlashReadPage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadPage, %function
FlashReadPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r7, r2
	mov	r5, r0
	mov	r6, r1
	mov	r8, r3
	bl	FlashReadRawPage
	adds	r2, r0, #1
	mov	r4, r0
	bne	.L1419
	ldr	r9, .L1437+4
	ldrb	fp, [r9, #44]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1420
.L1422:
	ldrb	r3, [r10, #2256]	@ zero_extendqisi2
	cbz	r3, .L1419
	ldr	r3, [r10, #88]
	mov	r1, r6
	mov	r2, r7
	mov	r0, r5
	ldr	r9, [r3, #304]
	movs	r3, #1
	str	r3, [sp]
	mov	r3, r8
	bl	FlashDdrTunningRead
	adds	r1, r0, #1
	mov	r4, r0
	beq	.L1423
	ldrb	r3, [r10, #2316]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1419
.L1423:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
	b	.L1419
.L1420:
	movs	r3, #0
	mov	r2, r7
	strb	r3, [r9, #44]
	mov	r1, r6
	mov	r3, r8
	mov	r0, r5
	bl	FlashReadRawPage
	adds	r3, r0, #1
	strb	fp, [r9, #44]
	beq	.L1422
	mov	r4, r0
.L1419:
	ldr	r9, .L1437+8
	ldr	r10, [r9, #1200]
	cmp	r10, #0
	beq	.L1418
	adds	r2, r4, #1
	bne	.L1418
	mov	r3, r8
	mov	r2, r7
	mov	r1, r6
	mov	r0, r5
	blx	r10
	mov	r3, r6
	mov	r4, r0
	mov	r1, r0
	mov	r2, r5
	ldr	r0, .L1437
	bl	printk
	adds	r3, r4, #1
	bne	.L1418
	ldr	r3, .L1437+4
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cbz	r3, .L1418
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	r4, [r9, #1200]
	mov	r3, r8
	mov	r2, r7
	mov	r1, r6
	mov	r0, r5
	blx	r4
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1418:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1438:
	.align	2
.L1437:
	.word	.LC93
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	FlashReadPage, .-FlashReadPage
	.align	1
	.global	FlashDdrParaScan
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashDdrParaScan, %function
FlashDdrParaScan:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r6, r0
	ldr	r4, .L1450
	movs	r5, #0
	mov	r7, r1
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	NandcSetMode
	mov	r3, r5
	mov	r2, r5
	mov	r1, r7
	str	r5, [sp]
	mov	r0, r6
	bl	FlashDdrTunningRead
	mov	r3, r5
	mov	r8, r0
	mov	r2, r5
	mov	r1, r7
	mov	r0, r6
	bl	FlashReadRawPage
	adds	r0, r0, #1
	beq	.L1440
	cmp	r8, #-1
	bne	.L1441
.L1440:
	ldrb	r3, [r4, #2233]	@ zero_extendqisi2
	lsls	r3, r3, #31
	bpl	.L1441
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	movs	r3, #0
.L1449:
	movs	r0, #0
	strb	r3, [r4, #2256]
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1441:
	movs	r3, #1
	b	.L1449
.L1451:
	.align	2
.L1450:
	.word	.LANCHOR0
	.fnend
	.size	FlashDdrParaScan, .-FlashDdrParaScan
	.align	1
	.global	FlashLoadPhyInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashLoadPhyInfo, %function
FlashLoadPhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r3, #60
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L1466
	movs	r5, #0
	mov	r8, #4
	strb	r3, [sp, #12]
	movs	r3, #40
	strb	r3, [sp, #13]
	movs	r3, #24
	strb	r3, [sp, #14]
	movs	r3, #16
	ldr	r4, .L1466+4
	mov	r7, #-1
	strb	r3, [sp, #15]
	mov	r0, r5
	ldr	r3, [r6, #40]
	ldr	r10, .L1466+24
	str	r5, [r4, #1208]
	str	r3, [sp, #4]
	ldr	r3, [r4, #1192]
	str	r3, [r4, #1204]
	bl	flash_enter_slc_mode
.L1453:
	add	fp, r5, #1
	mov	r9, #0
.L1455:
	add	r3, sp, #12
	ldrb	r0, [r3, r9]	@ zero_extendqisi2
	bl	FlashBchSel
	movs	r3, #0
	ldr	r2, [r4, #1192]
	mov	r1, r5
	mov	r0, r3
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1454
	movs	r3, #0
	ldr	r2, [r4, #1192]
	mov	r1, fp
	mov	r0, r3
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1454
	add	r9, r9, #1
	cmp	r9, #4
	bne	.L1455
.L1456:
	ldr	r3, [sp, #4]
	subs	r8, r8, #1
	add	r5, r5, r3
	bne	.L1453
	mov	r0, r8
	b	.L1465
.L1457:
	movw	r1, #2036
	add	r0, r9, #12
	bl	js_hash
	ldr	r3, [r9, #8]
	cmp	r3, r0
	bne	.L1463
	movs	r2, #32
	add	r1, r9, #160
	ldr	r0, .L1466+8
	bl	ftl_memcpy
	ldr	r1, [r4, #1204]
	movs	r2, #32
	ldr	r0, .L1466+12
	adds	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r4, #1204]
	mov	r2, #852
	ldr	r0, .L1466+16
	adds	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r10, #478]
	bl	FlashBlockAlignInit
	ldr	r7, [r4, #1204]
	mov	r0, r5
	str	r5, [r4, #1208]
	ldr	r1, [r6, #40]
	ldr	r3, [r7, #1076]
	strb	r3, [r6, #2256]
	bl	__aeabi_uidiv
	adds	r0, r0, #1
	cmp	r0, #1
	itet	ls
	movls	r3, #2
	strhi	r0, [r4, #1212]
	strls	r3, [r4, #1212]
	ldrh	r3, [r7, #14]
	movs	r7, #0
	strb	r3, [r4, #1216]
	b	.L1456
.L1463:
	mov	r7, #-1
	b	.L1456
.L1454:
	ldr	r9, [r4, #1204]
	ldr	r2, .L1466+20
	ldr	r3, [r9]
	cmp	r3, r2
	bne	.L1456
	cmp	r7, #0
	bne	.L1457
	ldr	r1, [r6, #40]
	mov	r0, r5
	bl	__aeabi_uidiv
	adds	r0, r0, #1
	str	r0, [r4, #1212]
	mov	r0, r7
.L1465:
	bl	flash_exit_slc_mode
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1467:
	.align	2
.L1466:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR1+468
	.word	.LANCHOR0+52
	.word	.LANCHOR0+1216
	.word	1312902724
	.word	.LANCHOR1
	.fnend
	.size	FlashLoadPhyInfo, .-FlashLoadPhyInfo
	.align	1
	.global	ToshibaReadRetrial
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ToshibaReadRetrial, %function
ToshibaReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r4, .L1495
	.pad #28
	sub	sp, sp, #28
	mov	fp, r3
	str	r1, [sp, #12]
	str	r2, [sp, #8]
	bl	NandcWaitFlashReady
	add	r3, r4, r8, lsl #3
	ldr	r6, [r4, r8, lsl #3]
	ldrb	r1, [r3, #4]	@ zero_extendqisi2
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	add	r7, r1, #8
	subs	r3, r3, #67
	add	r7, r6, r7, lsl #8
	cmp	r3, #1
	lsl	r3, r1, #8
	str	r3, [sp, #16]
	bls	.L1485
	ldrb	r5, [r4, #2256]	@ zero_extendqisi2
	cbz	r5, .L1470
	movs	r5, #1
	movs	r0, #0
	bl	NandcSetDdrMode
.L1470:
	lsls	r3, r1, #8
	movs	r2, #92
	adds	r3, r6, r3
	str	r2, [r3, #2056]
	movs	r2, #197
	str	r2, [r3, #2056]
.L1469:
	mov	r3, #-1
	mov	r9, #1
	str	r3, [sp, #4]
	lsls	r3, r1, #8
	str	r3, [sp, #20]
.L1471:
	ldr	r3, .L1495+4
	ldrb	r3, [r3, #1217]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r9, r3
	bcc	.L1480
	ldr	r10, [sp, #4]
.L1479:
	ldrb	r2, [r4, #84]	@ zero_extendqisi2
	movs	r1, #0
	mov	r0, r7
	subs	r2, r2, #67
	cmp	r2, #1
	bhi	.L1481
	bl	SandiskSetRRPara
.L1482:
	ldr	r3, [sp, #16]
	movs	r2, #255
	add	r6, r6, r3
	str	r2, [r6, #2056]
	ldrb	r2, [r4, #2316]	@ zero_extendqisi2
	add	r2, r2, r2, lsl #1
	cmp	r10, r2, asr #2
	bcc	.L1483
	cmp	r10, #-1
	it	ne
	movne	r10, #256
.L1483:
	mov	r0, r8
	bl	NandcWaitFlashReady
	cbz	r5, .L1468
	movs	r0, #4
	bl	NandcSetDdrMode
.L1468:
	mov	r0, r10
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1485:
	movs	r5, #0
	b	.L1469
.L1480:
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	mov	r0, r7
	uxtb	r1, r9
	subs	r3, r3, #67
	cmp	r3, #1
	bhi	.L1472
	bl	SandiskSetRRPara
.L1473:
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1474
	ldr	r3, .L1495+4
	ldrb	r3, [r3, #1217]	@ zero_extendqisi2
	subs	r3, r3, #3
	cmp	r9, r3
	itttt	eq
	ldreq	r3, [sp, #20]
	moveq	r2, #179
	addeq	r3, r6, r3
	streq	r2, [r3, #2056]
.L1474:
	ldr	r3, [sp, #16]
	movs	r2, #38
	adds	r3, r6, r3
	str	r2, [r3, #2056]
	movs	r2, #93
	str	r2, [r3, #2056]
	cbz	r5, .L1475
	movs	r0, #4
	bl	NandcSetDdrMode
	mov	r3, fp
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #12]
	mov	r0, r8
	bl	FlashReadRawPage
	mov	r10, r0
	movs	r0, #0
	bl	NandcSetDdrMode
.L1476:
	cmp	r10, #-1
	beq	.L1477
	ldrb	r2, [r4, #2316]	@ zero_extendqisi2
	ldr	r3, [sp, #4]
	add	r2, r2, r2, lsl #1
	cmp	r3, #-1
	it	eq
	moveq	r3, r10
	str	r3, [sp, #4]
	cmp	r10, r2, asr #2
	bcc	.L1479
	mov	fp, #0
	str	fp, [sp, #8]
.L1477:
	add	r9, r9, #1
	b	.L1471
.L1472:
	bl	ToshibaSetRRPara
	b	.L1473
.L1475:
	mov	r3, fp
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #12]
	mov	r0, r8
	bl	FlashReadRawPage
	mov	r10, r0
	b	.L1476
.L1481:
	bl	ToshibaSetRRPara
	b	.L1482
.L1496:
	.align	2
.L1495:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	ToshibaReadRetrial, .-ToshibaReadRetrial
	.align	1
	.global	SamsungReadRetrial
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	SamsungReadRetrial, %function
SamsungReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r5, .L1510
	mov	r9, r3
	mov	fp, r1
	mov	r10, r2
	bl	NandcWaitFlashReady
	movs	r7, #1
	mov	r4, #-1
	add	r3, r5, r8, lsl #3
	ldrb	r6, [r3, #4]	@ zero_extendqisi2
	add	r3, r6, #8
	ldr	r6, [r5, r8, lsl #3]
	add	r6, r6, r3, lsl #8
.L1498:
	ldr	r3, .L1510+4
	ldrb	r3, [r3, #1217]	@ zero_extendqisi2
	adds	r3, r3, #1
	cmp	r7, r3
	bcc	.L1502
.L1501:
	movs	r1, #0
	mov	r0, r6
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L1497
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
.L1497:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1502:
	uxtb	r1, r7
	mov	r0, r6
	bl	SamsungSetRRPara
	mov	r2, r10
	mov	r3, r9
	mov	r1, fp
	mov	r0, r8
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1499
	ldrb	r3, [r5, #2316]	@ zero_extendqisi2
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1505
	mov	r9, #0
	mov	r10, r9
.L1499:
	adds	r7, r7, #1
	b	.L1498
.L1505:
	mov	r4, r0
	b	.L1501
.L1511:
	.align	2
.L1510:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	1
	.global	MicronReadRetrial
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r3
	ldr	r3, .L1536
	mov	r10, r2
	.pad #36
	sub	sp, sp, #36
	mov	r5, r0
	str	r1, [sp, #24]
	ldrb	r2, [r3, #2316]	@ zero_extendqisi2
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1513
	add	r2, r2, r2, lsl #1
	asr	fp, r2, #2
.L1514:
	movs	r3, #0
	str	r3, [sp, #8]
	ldr	r3, .L1536
	add	r3, r3, r5, lsl #3
	str	r3, [sp, #28]
.L1524:
	mov	r0, r5
	mov	r9, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1536
	mov	r4, #-1
	ldr	r3, [r3, r5, lsl #3]
	str	r3, [sp, #16]
	ldr	r3, [sp, #28]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #16]
	add	r6, r3, r2, lsl #8
.L1515:
	ldr	r3, .L1536+4
	ldrb	r3, [r3, #1217]	@ zero_extendqisi2
	cmp	r9, r3
	bcc	.L1519
.L1518:
	ldr	r3, [sp, #16]
	movs	r0, #200
	ldr	r2, [sp, #20]
	movs	r6, #0
	add	r8, r3, r2, lsl #8
	movs	r3, #239
	str	r3, [r8, #2056]
	movs	r3, #137
	str	r3, [r8, #2052]
	bl	ndelay
	cmp	r4, fp
	str	r6, [r8, #2048]
	str	r6, [r8, #2048]
	str	r6, [r8, #2048]
	str	r6, [r8, #2048]
	bcc	.L1520
	adds	r1, r4, #1
	mov	r3, r9
	it	ne
	movne	r4, #256
	ldr	r2, [sp, #24]
	str	r4, [sp]
	mov	r1, r9
	ldr	r0, .L1536+8
	bl	printk
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L1522
	ldr	r3, .L1536
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1512
	adds	r2, r4, #1
	bne	.L1512
	movs	r1, #3
	mov	r0, r5
	bl	micron_auto_read_calibration_config
	movs	r3, #1
	str	r3, [sp, #8]
	b	.L1524
.L1513:
	ldr	r3, .L1536+12
	smull	r2, r3, r2, r3
	mov	fp, r3
	b	.L1514
.L1519:
	movs	r3, #239
	movs	r0, #200
	str	r3, [r6, #2056]
	movs	r3, #137
	str	r3, [r6, #2052]
	mov	r8, #0
	bl	ndelay
	add	r3, r9, #1
	mov	r2, r10
	str	r3, [r6, #2048]
	mov	r0, r5
	str	r8, [r6, #2048]
	str	r3, [sp, #12]
	mov	r3, r7
	str	r8, [r6, #2048]
	ldr	r1, [sp, #24]
	str	r8, [r6, #2048]
	bl	FlashReadRawPage
	adds	r3, r0, #1
	beq	.L1516
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	cmp	r0, fp
	bcc	.L1526
	mov	r7, r8
	mov	r10, r8
.L1516:
	ldr	r9, [sp, #12]
	b	.L1515
.L1526:
	mov	r4, r0
	mov	r7, r8
	mov	r10, r8
	b	.L1518
.L1522:
	mov	r1, r6
	mov	r0, r5
	bl	micron_auto_read_calibration_config
	adds	r3, r4, #1
	it	ne
	movne	r4, #256
.L1512:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1520:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	beq	.L1512
	mov	r1, r6
	mov	r0, r5
	bl	micron_auto_read_calibration_config
	mov	r4, #256
	b	.L1512
.L1537:
	.align	2
.L1536:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC94
	.word	1431655766
	.fnend
	.size	MicronReadRetrial, .-MicronReadRetrial
	.align	1
	.global	HynixReadRetrial
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	HynixReadRetrial, %function
HynixReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r9, r3
	ldr	r5, .L1555
	mov	r8, #0
	mov	r6, #-1
	mov	fp, r2
	mov	r7, r0
	str	r1, [sp, #4]
	ldr	r3, [r5, #48]
	adds	r2, r5, r0
	ldrb	r4, [r2, #1228]	@ zero_extendqisi2
	ldrb	r10, [r5, #1218]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #7
	cmp	r3, #1
	it	ls
	ldrbls	r4, [r2, #1236]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
.L1540:
	cmp	r8, r10
	bcc	.L1545
.L1544:
	ldr	r3, [r5, #48]
	add	r7, r7, r5
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #2316]	@ zero_extendqisi2
	ite	ls
	strbls	r4, [r7, #1236]
	strbhi	r4, [r7, #1228]
	add	r3, r3, r3, lsl #1
	cmp	r6, r3, asr #2
	bcc	.L1538
	adds	r3, r6, #1
	it	ne
	movne	r6, #256
.L1538:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1545:
	adds	r4, r4, #1
	ldr	r2, .L1555+4
	uxtb	r4, r4
	ldrb	r1, [r5, #1217]	@ zero_extendqisi2
	mov	r0, r7
	cmp	r10, r4
	it	ls
	movls	r4, #0
	mov	r3, r4
	bl	HynixSetRRPara
	mov	r2, fp
	mov	r3, r9
	ldr	r1, [sp, #4]
	mov	r0, r7
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1542
	ldrb	r3, [r5, #2316]	@ zero_extendqisi2
	cmp	r6, #-1
	it	eq
	moveq	r6, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1549
	mov	r9, #0
	mov	fp, r9
.L1542:
	add	r8, r8, #1
	b	.L1540
.L1549:
	mov	r6, r0
	b	.L1544
.L1556:
	.align	2
.L1555:
	.word	.LANCHOR0
	.word	.LANCHOR0+1220
	.fnend
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	1
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r10, r0
	mov	fp, r2
	mov	r9, r3
	str	r1, [sp, #12]
	bl	NandcWaitFlashReady
	ldr	r3, .L1582
	ldr	r2, [r3, r10, lsl #3]
	str	r3, [sp, #20]
	str	r2, [sp, #8]
	add	r2, r3, r10, lsl #3
	ldrb	r6, [r2, #4]	@ zero_extendqisi2
	ldrb	r2, [r3, #2232]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1558
	ldr	r3, [sp, #8]
	lsl	r8, r6, #8
	mov	r4, #-1
	movs	r7, #1
	add	r5, r3, r8
	addw	r3, r5, #2056
	str	r3, [sp, #16]
.L1562:
	ldr	r3, [sp, #16]
	movs	r6, #0
	ldr	r1, [sp, #12]
	mov	r0, r10
	mov	r2, r3
	movs	r3, #239
	str	r3, [r2]
	movs	r3, #141
	str	r3, [r5, #2052]
	mov	r2, fp
	ldr	r3, .L1582+4
	ldrsb	r3, [r7, r3]
	str	r3, [r5, #2048]
	mov	r3, r9
	str	r6, [r5, #2048]
	str	r6, [r5, #2048]
	str	r6, [r5, #2048]
	bl	FlashReadRawPage
	adds	r1, r0, #1
	beq	.L1559
	ldr	r3, [sp, #20]
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	ldrb	r3, [r3, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1570
	mov	r9, r6
	mov	fp, r6
.L1559:
	adds	r7, r7, #1
	cmp	r7, #26
	bne	.L1562
.L1561:
	ldr	r3, [sp, #8]
	add	r3, r3, r8
	mov	r8, r3
	movs	r3, #239
	str	r3, [r8, #2056]
	movs	r3, #141
.L1581:
	str	r3, [r5, #2052]
	movs	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	ldr	r3, .L1582
	ldrb	r3, [r3, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L1568
	adds	r3, r4, #1
	ldr	r2, [sp, #12]
	it	ne
	movne	r4, #256
	mov	r3, r7
	str	r4, [sp]
	mov	r1, r7
	ldr	r0, .L1582+8
	bl	printk
.L1568:
	mov	r0, r10
	bl	NandcWaitFlashReady
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1570:
	mov	r4, r0
	b	.L1561
.L1558:
	ldr	r3, [sp, #8]
	lsls	r6, r6, #8
	ldr	r8, .L1582+12
	mov	r4, #-1
	movs	r7, #1
	adds	r5, r3, r6
	addw	r3, r5, #2056
	str	r3, [sp, #16]
.L1567:
	ldr	r3, [sp, #16]
	mov	r0, r10
	ldr	r1, [sp, #12]
	mov	r2, r3
	movs	r3, #239
	str	r3, [r2]
	movs	r3, #137
	str	r3, [r5, #2052]
	mov	r2, fp
	ldrb	r3, [r8, #4]	@ zero_extendqisi2
	str	r3, [r5, #2048]
	ldrb	r3, [r8, #5]	@ zero_extendqisi2
	str	r3, [r5, #2048]
	ldrb	r3, [r8, #6]	@ zero_extendqisi2
	str	r3, [r5, #2048]
	ldrb	r3, [r8, #7]	@ zero_extendqisi2
	str	r3, [r5, #2048]
	mov	r3, r9
	bl	FlashReadRawPage
	adds	r2, r0, #1
	beq	.L1564
	ldr	r3, .L1582
	cmp	r4, #-1
	it	eq
	moveq	r4, r0
	ldrb	r3, [r3, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1571
	mov	r9, #0
	mov	fp, r9
.L1564:
	adds	r7, r7, #1
	add	r8, r8, #4
	cmp	r7, #26
	bne	.L1567
.L1566:
	ldr	r3, [sp, #8]
	add	r3, r3, r6
	mov	r6, r3
	movs	r3, #239
	str	r3, [r6, #2056]
	movs	r3, #137
	b	.L1581
.L1571:
	mov	r4, r0
	b	.L1566
.L1583:
	.align	2
.L1582:
	.word	.LANCHOR0
	.word	.LANCHOR3+11
	.word	.LC95
	.word	.LANCHOR3+37
	.fnend
	.size	samsung_read_retrial, .-samsung_read_retrial
	.align	1
	.global	FlashProgPage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgPage, %function
FlashProgPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r3, .L1587
	mov	r5, r1
	mov	r7, r2
	mov	r4, r0
	ldrb	r6, [r3, #477]	@ zero_extendqisi2
	cbnz	r0, .L1585
	ldr	r1, .L1587+4
	ldrb	r3, [r1, #37]	@ zero_extendqisi2
	ldr	r0, [r1, #40]
	muls	r0, r3, r0
	cmp	r0, r5
	bls	.L1585
	ldrb	r3, [r1, #36]	@ zero_extendqisi2
	cmp	r3, #0
	it	ne
	movne	r6, #4
.L1585:
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r1, r5
	mov	r0, r4
	bl	FlashProgFirstCmd
	mov	r3, r7
	mov	r2, r6
	str	r8, [sp]
	movs	r1, #1
	mov	r0, r4
	bl	NandcXferData
	mov	r1, r5
	mov	r0, r4
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r1, r5
	mov	r0, r4
	bl	FlashReadStatus
	mov	r1, r0
	mov	r0, r4
	bl	NandcFlashDeCs
	and	r0, r1, #1
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1588:
	.align	2
.L1587:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashProgPage, .-FlashProgPage
	.align	1
	.global	FlashSavePhyInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashSavePhyInfo, %function
FlashSavePhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L1602
	ldr	r5, .L1602+4
	ldr	r3, [r4, #1192]
	ldrb	r0, [r4, #1218]	@ zero_extendqisi2
	ldr	r8, .L1602+12
	str	r3, [r4, #1204]
	bl	FlashBchSel
	mov	r2, #2048
	movs	r1, #0
	ldr	r0, [r4, #1192]
	bl	ftl_memset
	ldr	r3, [r4, #1204]
	movs	r2, #32
	addw	r1, r5, #2072
	str	r8, [r3]
	ldr	r0, [r4, #1204]
	ldrb	r3, [r5, #2234]	@ zero_extendqisi2
	adds	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #37]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #2256]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #1204]
	movs	r2, #8
	addw	r1, r5, #2236
	adds	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #1204]
	movs	r2, #32
	addw	r1, r5, #1180
	adds	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #1204]
	movs	r2, #32
	ldr	r1, .L1602+8
	adds	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #1204]
	movs	r2, #32
	add	r1, r5, #52
	adds	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #1204]
	mov	r2, #852
	add	r1, r5, #1216
	adds	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #1204]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	mov	r3, #1592
	str	r0, [r6, #8]
	str	r3, [r6, #4]
	movs	r6, #0
	ldr	r3, [r4, #1220]
	mov	r7, r6
	movs	r0, #0
	str	r3, [r4, #1204]
	bl	flash_enter_slc_mode
.L1595:
	ldr	r1, [r5, #40]
	movs	r2, #0
	mov	r0, r2
	muls	r1, r7, r1
	bl	FlashEraseBlock
	ldrb	r9, [r5, #152]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1590
	mov	r9, #0
.L1591:
	ldr	r1, [r5, #40]
	movs	r3, #0
	ldr	r2, [r4, #1192]
	mov	r0, r3
	mla	r1, r1, r7, r9
	add	r9, r9, #1
	bl	FlashProgPage
	cmp	r9, #10
	bne	.L1591
.L1592:
	ldr	r1, [r5, #40]
	movs	r3, #0
	ldr	r2, [r4, #1220]
	mov	r0, r3
	add	r10, r7, #1
	muls	r1, r7, r1
	bl	FlashReadRawPage
	adds	r0, r0, #1
	beq	.L1593
	ldr	r9, [r4, #1204]
	ldr	r3, [r9]
	cmp	r3, r8
	bne	.L1593
	movw	r1, #2036
	add	r0, r9, #12
	bl	js_hash
	ldr	r3, [r9, #8]
	cmp	r3, r0
	bne	.L1593
	ldr	r3, [r5, #40]
	cmp	r6, #1
	str	r10, [r4, #1212]
	mul	r7, r7, r3
	str	r7, [r4, #1208]
	beq	.L1596
	movs	r6, #1
.L1593:
	mov	r7, r10
	cmp	r7, #4
	bne	.L1595
.L1594:
	movs	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	lsrs	r0, r0, #5
	negs	r0, r0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1590:
	ldr	r1, [r5, #40]
	mov	r3, r9
	ldr	r2, [r4, #1192]
	mov	r0, r9
	muls	r1, r7, r1
	bl	FlashProgPage
	ldr	r1, [r5, #40]
	mov	r3, r9
	ldr	r2, [r4, #1192]
	mov	r0, r9
	muls	r1, r7, r1
	adds	r1, r1, #1
	bl	FlashProgPage
	b	.L1592
.L1596:
	movs	r6, #2
	b	.L1594
.L1603:
	.align	2
.L1602:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR1+468
	.word	1312902724
	.fnend
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	1
	.global	FlashReadIdbDataRaw
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r3, #60
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1622
	mov	r10, r0
	strb	r3, [sp, #12]
	movs	r3, #40
	strb	r3, [sp, #13]
	movs	r3, #24
	strb	r3, [sp, #14]
	movs	r3, #16
	strb	r3, [sp, #15]
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	str	r3, [sp, #4]
	ldr	r3, [r4, #2252]
	cbz	r3, .L1605
	movs	r0, #0
	bl	flash_enter_slc_mode
.L1605:
	ldr	r6, .L1622+4
	mov	r8, #-1
	movs	r5, #2
	mov	r2, #2048
	movs	r1, #0
	mov	r0, r10
	bl	ftl_memset
.L1606:
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L1611
.L1610:
	ldr	r0, [sp, #4]
	bl	FlashBchSel
	ldr	r3, [r4, #2252]
	cbz	r3, .L1604
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1604:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1611:
	movs	r7, #0
	add	fp, sp, #12
.L1608:
	ldrb	r9, [r7, fp]	@ zero_extendqisi2
	mov	r0, r9
	bl	FlashBchSel
	ldr	r1, [r4, #40]
	movs	r3, #0
	ldr	r2, [r6, #1192]
	mov	r0, r3
	muls	r1, r5, r1
	bl	FlashReadRawPage
	adds	r0, r0, #1
	bne	.L1607
	adds	r7, r7, #1
	cmp	r7, #4
	bne	.L1608
.L1609:
	adds	r5, r5, #1
	b	.L1606
.L1614:
	mov	r8, #0
	b	.L1610
.L1607:
	ldr	r3, [r6, #1192]
	ldr	r2, .L1622+8
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1609
	mov	r1, r9
	ldr	r0, .L1622+12
	bl	printk
	mov	r2, #2048
	ldr	r1, [r6, #1192]
	mov	r0, r10
	bl	ftl_memcpy
	ldr	r3, [r6, #1192]
	ldr	r3, [r3, #512]
	strb	r3, [r4, #37]
	ldr	r3, [r6, #1212]
	cmp	r5, r3
	bcs	.L1614
	str	r5, [r6, #1212]
	mov	r8, #0
	bl	FlashSavePhyInfo
	b	.L1609
.L1623:
	.align	2
.L1622:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	-52655045
	.word	.LC96
	.fnend
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	1
	.global	FlashInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashInit, %function
FlashInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	ldr	r5, .L1740
	.pad #28
	sub	sp, sp, #28
	mov	r0, #32768
	movs	r6, #0
	bl	ftl_malloc
	str	r0, [r5, #1192]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #1220]
	mov	r0, #4096
	ldr	r4, .L1740+4
	mov	r8, r6
	bl	ftl_dma32_malloc
	str	r0, [r5, #1224]
	mov	r0, #32768
	bl	ftl_malloc
	ldr	fp, .L1740+40
	str	r0, [r5, #1228]
	mov	r0, #4096
	bl	ftl_dma32_malloc
	movs	r3, #50
	str	r0, [r5, #1232]
	strb	r3, [r4, #37]
	mov	r0, r7
	strb	r3, [r5, #1216]
	addw	r7, r4, #2072
	movs	r3, #128
	str	r6, [r5, #1212]
	str	r3, [r4, #40]
	movs	r3, #60
	strb	r6, [r4, #2256]
	str	r6, [r5, #1196]
	strb	r6, [r4, #36]
	strb	r6, [r5, #1236]
	strb	r3, [r5, #1218]
	bl	NandcInit
.L1630:
	ldr	r3, [r4, r6, lsl #3]
	uxtb	r9, r6
	add	r2, r4, r6, lsl #3
	mov	r0, r9
	ldrb	r10, [r2, #4]	@ zero_extendqisi2
	str	r3, [sp, #20]
	bl	FlashReset
	mov	r0, r9
	bl	NandcFlashCs
	ldr	r3, [sp, #20]
	movs	r0, #200
	add	r10, r3, r10, lsl #8
	movs	r3, #144
	str	r3, [r10, #2056]
	str	r8, [r10, #2052]
	bl	ndelay
	ldr	r2, [r10, #2048]
	uxtb	r2, r2
	strb	r2, [r7]
	cmp	r2, #44
	ldr	r1, [r10, #2048]
	strb	r1, [r7, #1]
	ldr	r1, [r10, #2048]
	strb	r1, [r7, #2]
	ldr	r1, [r10, #2048]
	strb	r1, [r7, #3]
	ldr	r1, [r10, #2048]
	strb	r1, [r7, #4]
	ldr	r1, [r10, #2048]
	strb	r1, [r7, #5]
	bne	.L1625
	movs	r2, #239
	movs	r0, #200
	str	r2, [r10, #2056]
	movs	r2, #1
	str	r2, [r10, #2052]
	bl	ndelay
	movs	r2, #4
	str	r2, [r10, #2048]
	str	r8, [r10, #2048]
	str	r8, [r10, #2048]
	str	r8, [r10, #2048]
.L1625:
	mov	r0, r9
	bl	NandcFlashDeCs
	ldrb	r2, [r7]	@ zero_extendqisi2
	subs	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1626
	ldrb	r1, [r7, #5]	@ zero_extendqisi2
	mov	r0, fp
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	str	r1, [sp, #12]
	ldrb	r1, [r7, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r7, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r7, #2]	@ zero_extendqisi2
	str	r1, [sp]
	adds	r1, r6, #1
	bl	printk
.L1626:
	cbnz	r6, .L1627
	ldrb	r3, [r4, #2072]	@ zero_extendqisi2
	subs	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1681
	ldrb	r3, [r4, #2073]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1681
.L1627:
	ldrb	r3, [r7]	@ zero_extendqisi2
	adds	r6, r6, #1
	adds	r7, r7, #8
	cmp	r3, #181
	itt	eq
	moveq	r3, #44
	strbeq	r3, [r7, #-8]
	cmp	r6, #4
	bne	.L1630
	ldrb	r3, [r4, #2072]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1631
	ldr	r0, [r4, #2260]
	bl	NandcSetDdrMode
.L1631:
	mov	r2, #852
	movs	r1, #0
	ldr	r0, .L1740+8
	bl	ftl_memset
	ldr	r6, .L1740+12
	ldr	r3, .L1740+16
	ldr	r1, [r4, #2268]
	add	r2, r3, #468
	cmp	r1, r6
	str	r2, [r4, #48]
	mov	r2, #0
	strb	r2, [r4, #44]
	bne	.L1632
	ldrb	r2, [r3, #487]	@ zero_extendqisi2
	cmp	r2, #50
	itt	ne
	movne	r2, #1
	strne	r2, [r4, #2252]
.L1632:
	ldrb	r2, [r4, #2073]	@ zero_extendqisi2
	cmp	r2, #161
	beq	.L1633
	cmp	r2, #241
	beq	.L1633
	cmp	r2, #218
	beq	.L1633
	and	r0, r2, #253
	cmp	r0, #209
	beq	.L1633
	cmp	r2, #220
	bne	.L1634
	ldrb	r0, [r4, #2075]	@ zero_extendqisi2
	cmp	r0, #149
	bne	.L1634
.L1633:
	movs	r7, #16
	movs	r0, #1
	strb	r7, [r4, #37]
	strb	r7, [r5, #1218]
	ldrb	r7, [r4, #2072]	@ zero_extendqisi2
	strb	r0, [r4, #36]
	strb	r2, [r3, #3414]
	cmp	r7, #152
	strb	r7, [r3, #3413]
	bne	.L1636
	ldrsb	r7, [r4, #2076]
	cmp	r7, #0
	blt	.L1637
	movs	r0, #24
	strb	r0, [r5, #1218]
.L1636:
	cmp	r1, r6
	beq	.L1639
	movw	r0, #2049
	cmp	r1, r0
	bne	.L1640
.L1639:
	movs	r1, #16
	strb	r1, [r5, #1218]
.L1640:
	cmp	r2, #218
	bne	.L1641
	mov	r1, #2048
.L1734:
	strh	r1, [r3, #3426]	@ movhi
	strb	r2, [r3, #3414]
.L1642:
	movs	r2, #32
	ldr	r1, .L1740+20
	ldr	r0, .L1740+24
	bl	ftl_memcpy
	ldr	r1, .L1740+28
	movs	r2, #32
	sub	r0, r1, #2944
	bl	ftl_memcpy
.L1634:
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1645
	bl	FlashLoadPhyInfoInRam
	cbnz	r0, .L1647
	ldr	r3, [r4, #48]
	ldrh	r3, [r3, #16]
	lsrs	r3, r3, #8
	lsls	r7, r3, #31
	and	r0, r3, #7
	strb	r0, [r4, #2233]
	bmi	.L1647
	movs	r3, #1
	strb	r3, [r4, #2256]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	NandcSetMode
.L1647:
	ldr	r3, [r4, #48]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #152]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1645
	ldr	r3, [r4, #2260]
	cmp	r3, #0
	beq	.L1650
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
.L1735:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1645
	movs	r0, #1
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #48]
	ldr	r0, .L1740+32
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	adds	r1, r0, #1
	beq	.L1624
	bl	FlashDieInfoInit
	ldr	r3, [r4, #48]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, [r4, #48]
	ldr	r2, .L1740+36
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldrh	r2, [r2, #3270]
	addw	r2, r2, #4095
	cmp	r1, r2, asr #12
	blt	.L1652
	ldrh	r2, [r3, #14]
	adds	r2, r2, #255
	cmp	r1, r2, asr #8
	bge	.L1653
	b	.L1741
.L1742:
	.align	2
.L1740:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR0+1216
	.word	1446522928
	.word	.LANCHOR1
	.word	.LANCHOR1+3288
	.word	.LANCHOR0+52
	.word	.LANCHOR1+3412
	.word	.LC98
	.word	.LANCHOR2
	.word	.LC97
.L1741:
.L1652:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L1653:
	ldrb	r3, [r4, #2233]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1654
	bl	FlashSavePhyInfo
	movs	r0, #0
	bl	flash_enter_slc_mode
	ldr	r1, [r5, #1208]
	movs	r0, #0
	bl	FlashDdrParaScan
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1654:
	bl	FlashSavePhyInfo
.L1645:
	ldr	r7, [r4, #48]
	mov	r8, #0
	str	r8, [r5, #1200]
	ldrb	r3, [r7, #26]	@ zero_extendqisi2
	ldrb	r1, [r7, #12]	@ zero_extendqisi2
	ldrh	r0, [r7, #10]
	strb	r3, [r4, #152]
	ldrh	r3, [r7, #16]
	ubfx	r2, r3, #7, #1
	strb	r2, [r4, #44]
	ubfx	r2, r3, #3, #1
	strb	r2, [r5, #1237]
	ubfx	r2, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r2, [r4, #2244]
	strb	r3, [r4, #2233]
	bl	__aeabi_idiv
	mov	r1, r0
	ldrb	r0, [r7, #18]	@ zero_extendqisi2
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #48]
	ldrh	r2, [r3, #16]
	lsls	r2, r2, #25
	bpl	.L1656
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r4, #1217]	@ zero_extendqisi2
	strb	r0, [r4, #84]
	strb	r3, [r4, #85]
	ldrb	r3, [r4, #1218]	@ zero_extendqisi2
	strb	r3, [r5, #1217]
	subs	r3, r0, #1
	cmp	r3, #7
	bhi	.L1657
	ldr	r3, .L1743
	str	r3, [r5, #1200]
	subs	r3, r0, #5
	cmp	r3, #1
	bls	.L1658
	cmp	r0, #8
	bne	.L1659
.L1658:
	movs	r3, #1
	str	r3, [r4, #2308]
.L1659:
	cmp	r0, #7
	ldr	r3, .L1743+4
	beq	.L1660
	sub	r2, r3, #8
	cmp	r0, #8
	it	ne
	movne	r3, r2
.L1660:
	subs	r1, r3, #1
	movs	r2, #0
	adds	r3, r3, #31
.L1662:
	ldrsb	r7, [r1, #1]!
	cbnz	r7, .L1661
	adds	r2, r2, #1
.L1661:
	cmp	r3, r1
	bne	.L1662
	cmp	r2, #27
	bls	.L1656
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
.L1656:
	ldr	r3, [r4, #2268]
	cmp	r3, r6
	bne	.L1674
	ldrb	r2, [r4, #152]	@ zero_extendqisi2
	cbz	r2, .L1674
	ldr	r2, [r4, #48]
	movs	r1, #0
	strb	r1, [r2, #18]
.L1674:
	ldrb	r2, [r4, #2072]	@ zero_extendqisi2
	cmp	r2, #44
	bne	.L1675
	ldrb	r2, [r4, #2256]	@ zero_extendqisi2
	cbz	r2, .L1675
	cmp	r3, r6
	bne	.L1676
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cbnz	r3, .L1675
.L1676:
	movs	r3, #0
	movs	r0, #1
	strb	r3, [r4, #2256]
	bl	FlashSetInterfaceMode
	movs	r0, #1
	bl	NandcSetMode
.L1675:
	ldrb	r3, [r4, #2233]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1677
	ldrb	r2, [r4, #2256]	@ zero_extendqisi2
	cbnz	r2, .L1678
	lsls	r3, r3, #31
	bmi	.L1677
.L1678:
	movs	r0, #0
	bl	flash_enter_slc_mode
	ldr	r1, [r5, #1208]
	movs	r0, #0
	bl	FlashDdrParaScan
	movs	r0, #0
	bl	flash_exit_slc_mode
.L1677:
	ldr	r3, [r4, #48]
	movs	r6, #16
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1743+8
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1743+12
	strb	r6, [r4, #37]
	bl	FlashTimingCfg
	ldr	r5, [r4, #48]
	ldrb	r2, [r4, #2073]	@ zero_extendqisi2
	ldrb	r3, [r5, #12]	@ zero_extendqisi2
	ldrh	r7, [r5, #14]
	strh	r3, [r4, #132]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	str	r3, [r4, #128]
	lsl	r3, r2, r6
	orr	r3, r3, r2, lsl #8
	ldrb	r2, [r4, #2072]	@ zero_extendqisi2
	orrs	r3, r3, r2
	ldrb	r2, [r4, #2075]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #24
	str	r3, [r4, #124]
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	strh	r3, [r4, #134]	@ movhi
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	strh	r7, [r4, #138]	@ movhi
	strh	r3, [r4, #136]	@ movhi
	ldrh	r3, [r5, #10]
	strh	r3, [r4, #140]	@ movhi
	ldrb	r1, [r5, #12]	@ zero_extendqisi2
	ldrh	r0, [r5, #10]
	bl	__aeabi_idiv
	strh	r0, [r4, #142]	@ movhi
	ldrb	r2, [r5, #9]	@ zero_extendqisi2
	strh	r2, [r4, #144]	@ movhi
	ldrh	r1, [r5, #10]
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	smulbb	r3, r3, r1
	mov	r1, #512
	strh	r1, [r4, #148]	@ movhi
	ldrb	r1, [r4, #37]	@ zero_extendqisi2
	uxth	r3, r3
	strh	r1, [r4, #150]	@ movhi
	ldrb	r1, [r4, #36]	@ zero_extendqisi2
	strh	r3, [r4, #146]	@ movhi
	cmp	r1, #1
	bne	.L1679
	lsls	r3, r3, #1
	lsrs	r1, r7, #1
	lsls	r2, r2, #1
	strb	r6, [r4, #37]
	strh	r3, [r4, #146]	@ movhi
	movs	r3, #8
	strh	r1, [r4, #138]	@ movhi
	strh	r2, [r4, #144]	@ movhi
	strh	r3, [r4, #150]	@ movhi
.L1679:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	movs	r0, #0
.L1624:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1641:
	cmp	r2, #220
	bne	.L1643
	mov	r1, #4096
	b	.L1734
.L1643:
	cmp	r2, #211
	itttt	eq
	moveq	r2, #4096
	strheq	r2, [r3, #3426]	@ movhi
	moveq	r2, #2
	strbeq	r2, [r3, #3425]
	b	.L1642
.L1650:
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	b	.L1735
.L1657:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1664
	ldr	r3, .L1743+16
	cmp	r0, #19
	str	r3, [r5, #1200]
	beq	.L1665
.L1739:
	movs	r3, #7
	b	.L1737
.L1665:
	movs	r3, #15
.L1737:
	strb	r3, [r5, #1217]
	b	.L1656
.L1664:
	cmp	r0, #33
	beq	.L1666
	sub	r3, r0, #65
	cmp	r3, #1
	bhi	.L1667
.L1666:
	ldr	r3, .L1743+20
	str	r3, [r5, #1200]
	movs	r3, #4
	strb	r3, [r4, #85]
	b	.L1739
.L1667:
	sub	r3, r0, #67
	uxtb	r3, r3
	cmp	r3, #1
	bls	.L1668
	sub	r2, r0, #34
	cmp	r2, #1
	bhi	.L1669
.L1668:
	ldr	r2, .L1743+20
	cmp	r0, #35
	str	r2, [r5, #1200]
	beq	.L1670
	cmp	r0, #68
	beq	.L1670
	movs	r2, #7
.L1736:
	cmp	r3, #1
	strb	r2, [r5, #1217]
	ite	ls
	movls	r3, #4
	movhi	r3, #5
	strb	r3, [r4, #85]
	b	.L1656
.L1670:
	movs	r2, #17
	b	.L1736
.L1669:
	cmp	r0, #49
	bne	.L1673
	ldr	r3, .L1743+24
	str	r3, [r5, #1200]
	b	.L1656
.L1673:
	cmp	r0, #50
	ittt	eq
	ldreq	r3, .L1743+28
	streq	r8, [r4, #2252]
	streq	r3, [r5, #1200]
	b	.L1656
.L1681:
	mvn	r0, #1
	b	.L1624
.L1637:
	strb	r0, [r5, #1236]
	b	.L1636
.L1744:
	.align	2
.L1743:
	.word	HynixReadRetrial
	.word	.LANCHOR0+1244
	.word	.LANCHOR2+3476
	.word	150000
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	samsung_read_retrial
	.fnend
	.size	FlashInit, .-FlashInit
	.align	1
	.global	FlashPageProgMsbFFData
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashPageProgMsbFFData, %function
FlashPageProgMsbFFData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r6, r0
	ldr	r5, .L1765
	mov	r7, r1
	mov	r4, r2
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cbz	r3, .L1746
	ldr	r3, [r5, #2252]
	cbnz	r3, .L1745
.L1746:
	ldr	r3, [r5, #48]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	subs	r3, r2, #5
	uxtb	r3, r3
	cmp	r3, #30
	bhi	.L1747
	ldr	r1, .L1765+4
	lsr	r3, r1, r3
	lsls	r3, r3, #31
	bmi	.L1748
.L1752:
	cmp	r2, #68
	bne	.L1745
.L1748:
	ldr	r9, .L1765+8
	movw	r10, #65535
	ldr	r8, .L1765+12
.L1750:
	ldr	r3, [r5, #48]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bhi	.L1751
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1751:
	add	r3, r9, r4, lsl #1
	ldrh	r3, [r3, #1220]
	cmp	r3, r10
	bne	.L1745
	mov	r2, #32768
	movs	r1, #255
	ldr	r0, [r8, #1220]
	bl	ftl_memset
	ldr	r3, [r8, #1220]
	adds	r1, r4, r7
	mov	r0, r6
	adds	r4, r4, #1
	uxth	r4, r4
	mov	r2, r3
	bl	FlashProgPage
	b	.L1750
.L1747:
	cmp	r2, #50
	bne	.L1752
	b	.L1748
.L1745:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1766:
	.align	2
.L1765:
	.word	.LANCHOR0
	.word	1073758215
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	1
	.global	FlashReadSlc2KPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1824
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r7, .L1824+4
	mov	r8, #0
	.pad #28
	sub	sp, sp, #28
	ldrb	fp, [r3, #477]	@ zero_extendqisi2
	str	r1, [sp, #8]
	str	r2, [sp, #12]
.L1768:
	ldr	r3, [sp, #8]
	cmp	r8, r3
	bne	.L1792
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1792:
	ldr	r3, [sp, #8]
	add	r2, sp, #20
	ldr	r1, [sp, #12]
	mov	r0, r4
	sub	r3, r3, r8
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldrb	r2, [r7, #2234]	@ zero_extendqisi2
	ldr	r3, [sp, #16]
	cmp	r2, r3
	bhi	.L1769
	mov	r3, #-1
	str	r3, [r4]
.L1770:
	add	r8, r8, #1
	adds	r4, r4, #36
	b	.L1768
.L1769:
	add	r3, r3, r7
	mov	r9, #0
	ldrb	r5, [r3, #2236]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1771:
	ldr	r1, [sp, #20]
	mov	r0, r5
	bl	FlashReadCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #12]
	mov	r2, fp
	movs	r1, #0
	mov	r0, r5
	str	r3, [sp]
	ldr	r3, [r4, #8]
	bl	NandcXferData
	ldr	r3, .L1824+8
	mov	r6, r0
	ldrb	r3, [r3, #1236]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1772
.L1775:
	adds	r0, r6, #1
	beq	.L1773
.L1774:
	cmp	r9, #0
	beq	.L1777
.L1776:
	mov	r6, #256
.L1777:
	mov	r9, #0
.L1778:
	ldr	r3, [r7, #40]
	mov	r0, r5
	ldr	r1, [sp, #20]
	add	r1, r1, r3
	bl	FlashReadCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #8]
	cbz	r3, .L1779
	add	r3, r3, #2048
.L1779:
	ldr	r2, [r4, #12]
	cbz	r2, .L1780
	adds	r2, r2, #8
.L1780:
	str	r2, [sp]
	movs	r1, #0
	mov	r2, fp
	mov	r0, r5
	bl	NandcXferData
	ldr	r3, .L1824+8
	mov	r10, r0
	ldrb	r2, [r3, #1236]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1781
.L1784:
	cmp	r10, #-1
	beq	.L1782
.L1783:
	cmp	r9, #0
	beq	.L1786
.L1785:
	mov	r10, #256
.L1786:
	mov	r0, r5
	bl	NandcFlashDeCs
	ldrb	r3, [r7, #2316]	@ zero_extendqisi2
	cmp	r6, r10
	it	cc
	movcc	r6, r10
	add	r3, r3, r3, lsl #1
	cmp	r6, r3, asr #2
	bhi	.L1787
	movs	r3, #0
.L1823:
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cbz	r3, .L1788
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L1788
	ldr	r2, [r3, #8]
	adds	r1, r2, #1
	bne	.L1788
	ldr	r3, [r3]
	adds	r3, r3, #1
	it	ne
	strne	r2, [r4]
.L1788:
	ldr	r3, [r4]
	adds	r2, r3, #1
	bne	.L1770
	ldr	r1, [r4, #4]
	ldrb	r2, [r7, #2316]	@ zero_extendqisi2
	ldr	r0, .L1824+12
	bl	printk
	ldr	r1, [r4, #8]
	cbz	r1, .L1790
	movs	r3, #8
	movs	r2, #4
	ldr	r0, .L1824+16
	bl	rknand_print_hex
.L1790:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1770
	movs	r3, #4
	ldr	r0, .L1824+20
	mov	r2, r3
	bl	rknand_print_hex
	b	.L1770
.L1772:
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	bls	.L1775
	mov	r6, #256
	b	.L1774
.L1773:
	cmp	r9, #10
	beq	.L1776
	add	r9, r9, #1
	b	.L1771
.L1781:
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	bls	.L1784
	mov	r10, #256
	b	.L1783
.L1782:
	cmp	r9, #10
	beq	.L1785
	add	r9, r9, #1
	b	.L1778
.L1787:
	mov	r3, #256
	b	.L1823
.L1825:
	.align	2
.L1824:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC99
	.word	.LC100
	.word	.LC101
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	1
	.global	FlashReadPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L1917
	mov	r9, r0
	str	r1, [sp, #32]
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	str	r2, [sp, #20]
	cbnz	r3, .L1827
	ldr	r2, .L1917+4
	mov	r8, r3
	ldr	r10, .L1917+12
	str	r3, [sp, #12]
	ldrb	r2, [r2, #477]	@ zero_extendqisi2
	str	r2, [sp, #28]
	ldrb	r2, [r4, #44]	@ zero_extendqisi2
	str	r2, [sp, #36]
.L1828:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #32]
	cmp	r3, r2
	bcc	.L1866
	movs	r0, #0
	b	.L1826
.L1827:
	bl	FlashReadSlc2KPages
.L1826:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1866:
	ldr	r2, [sp, #12]
	movs	r3, #36
	ldr	r1, [sp, #20]
	muls	r3, r2, r3
	add	r7, r9, r3
	str	r3, [sp, #16]
	ldr	r3, [r7, #4]
	mov	r0, r7
	str	r3, [sp, #24]
	ldr	r3, [sp, #32]
	subs	r3, r3, r2
	add	r2, sp, #44
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldrb	r2, [r4, #2234]	@ zero_extendqisi2
	mov	r6, r0
	ldr	r3, [sp, #40]
	cmp	r2, r3
	bhi	.L1830
	ldr	r2, [sp, #16]
	mov	r3, #-1
	str	r3, [r9, r2]
.L1831:
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	str	r3, [sp, #12]
	b	.L1828
.L1830:
	add	r3, r3, r4
	ldrb	r5, [r3, #2236]	@ zero_extendqisi2
	ldrb	r3, [r10, #1237]	@ zero_extendqisi2
	mov	r0, r5
	cmp	r3, #0
	it	eq
	moveq	r6, #0
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #48]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	subs	r3, r2, #1
	cmp	r3, #7
	bhi	.L1833
	subs	r2, r2, #7
	adds	r1, r4, r5
	cmp	r2, #1
	add	r2, r4, r5
	ldrb	r3, [r1, #1228]	@ zero_extendqisi2
	ldrb	r2, [r2, #2068]	@ zero_extendqisi2
	it	ls
	ldrbls	r3, [r1, #1236]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1833
	ldr	r2, .L1917+8
	mov	r0, r5
	ldrb	r1, [r4, #1217]	@ zero_extendqisi2
	bl	HynixSetRRPara
.L1833:
	mov	r0, r5
	bl	NandcFlashCs
	ldr	r3, [sp, #20]
	cmp	r3, #1
	beq	.L1835
	ldr	r3, [sp, #24]
	cmp	r3, #0
	bge	.L1836
.L1835:
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cbz	r3, .L1836
	mov	r0, r5
	bl	flash_enter_slc_mode
.L1843:
	ldr	r1, [sp, #44]
	adds	r0, r1, #1
	bne	.L1838
	cmp	r5, #255
	beq	.L1868
.L1838:
	cbz	r6, .L1840
	ldr	r2, [r4, #40]
	mov	r0, r5
	add	r2, r2, r1
	bl	FlashReadDpCmd
.L1841:
	mov	r0, r5
	bl	NandcWaitFlashReady
	cbz	r6, .L1839
	ldr	r1, [sp, #44]
	mov	r0, r5
	bl	FlashReadDpDataOutCmd
.L1839:
	ldr	r3, [r7, #12]
	movs	r1, #0
	ldr	r2, [sp, #28]
	mov	r0, r5
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
	ldrb	r3, [r4, #44]	@ zero_extendqisi2
	mov	fp, r0
	cbz	r3, .L1842
	cmp	r0, #-1
	bne	.L1842
	mov	r3, #0
	movs	r6, #0
	strb	r3, [r4, #44]
	b	.L1843
.L1836:
	mov	r0, r5
	bl	flash_exit_slc_mode
	b	.L1843
.L1840:
	mov	r0, r5
	bl	FlashReadCmd
	b	.L1841
.L1868:
	movs	r6, #0
	b	.L1839
.L1842:
	cbz	r6, .L1844
	ldr	r3, [r4, #40]
	mov	r0, r5
	ldr	r1, [sp, #44]
	add	r1, r1, r3
	bl	FlashReadDpDataOutCmd
	ldr	r3, [sp, #16]
	movs	r1, #0
	mov	r0, r5
	adds	r3, r3, #36
	add	r3, r3, r9
	ldr	r2, [r3, #12]
	str	r2, [sp]
	ldr	r2, [sp, #28]
	ldr	r3, [r3, #8]
	bl	NandcXferData
	cmp	r0, #-1
	mov	r8, r0
	it	eq
	moveq	r6, #0
.L1844:
	mov	r0, r5
	bl	NandcFlashDeCs
	ldrb	r3, [sp, #36]	@ zero_extendqisi2
	cmp	fp, #-1
	strb	r3, [r4, #44]
	bne	.L1845
	ldrb	r3, [r4, #2256]	@ zero_extendqisi2
	cbnz	r3, .L1846
.L1850:
	ldr	r6, [r10, #1200]
	cmp	r6, #0
	bne	.L1847
	ldr	r3, [r7, #12]
	mov	r0, r5
	ldr	r2, [r7, #8]
	ldr	r1, [sp, #44]
	bl	FlashReadRawPage
	b	.L1916
.L1846:
	ldr	r3, [r4, #88]
	mov	r0, r5
	ldr	r1, [sp, #44]
	ldr	r6, [r3, #304]
	movs	r3, #1
	str	r3, [sp]
	ldr	r3, [r7, #12]
	ldr	r2, [r7, #8]
	bl	FlashDdrTunningRead
	cmp	r0, #-1
	mov	fp, r0
	beq	.L1849
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1869
.L1849:
	ubfx	r0, r6, #8, #8
	bl	NandcSetDdrPara
	cmp	fp, #-1
	beq	.L1850
.L1869:
	movs	r6, #0
.L1845:
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	fp, r3, asr #2
	bls	.L1858
	ldr	r3, [r10, #1200]
	cmp	r3, #0
	bne	.L1858
	mov	fp, #256
	b	.L1853
.L1918:
	.align	2
.L1917:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1220
	.word	.LANCHOR4
.L1847:
	ldr	r3, [r7, #12]
	mov	r0, r5
	ldr	r2, [r7, #8]
	ldr	r1, [sp, #44]
	blx	r6
	cmp	r0, #-1
	mov	fp, r0
	bne	.L1851
	ldr	r3, [r4, #48]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	subs	r3, r3, #1
	cmp	r3, #7
	bhi	.L1852
	movs	r3, #0
	ldr	r2, .L1919
	ldrb	r1, [r4, #1217]	@ zero_extendqisi2
	mov	r0, r5
	bl	HynixSetRRPara
.L1852:
	ldr	r3, [r7, #12]
	mov	r0, r5
	ldr	r2, [r7, #8]
	ldr	r1, [sp, #44]
	bl	FlashReadRawPage
	ldrb	r2, [r4, #2316]	@ zero_extendqisi2
	mov	fp, r0
	mov	r3, r0
	ldr	r1, [r7, #4]
	ldr	r0, .L1919+4
	bl	printk
	cmp	fp, #-1
	bne	.L1851
	ldrb	r6, [r4, #152]	@ zero_extendqisi2
	cbz	r6, .L1853
	ldr	r3, [sp, #20]
	mov	r0, r5
	cmp	r3, #1
	beq	.L1854
	ldr	r3, [sp, #24]
	cmp	r3, #0
	bge	.L1855
.L1854:
	bl	flash_enter_slc_mode
.L1856:
	ldr	r6, [r10, #1200]
	mov	r0, r5
	ldr	r3, [r7, #12]
	ldr	r2, [r7, #8]
	ldr	r1, [sp, #44]
	blx	r6
.L1916:
	cmp	r0, #-1
	mov	fp, r0
	mov	r6, #0
	bne	.L1858
.L1853:
	ldr	r3, [sp, #16]
	str	fp, [r9, r3]
.L1859:
	ldr	r3, [sp, #16]
	ldr	r3, [r9, r3]
	adds	r2, r3, #1
	bne	.L1861
	ldr	r1, [r7, #4]
	ldrb	r2, [r4, #2316]	@ zero_extendqisi2
	ldr	r0, .L1919+8
	bl	printk
	ldr	r1, [r7, #12]
	cbz	r1, .L1861
	movs	r3, #4
	ldr	r0, .L1919+12
	mov	r2, r3
	bl	rknand_print_hex
.L1861:
	cbz	r6, .L1863
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r8, r3, asr #2
	bls	.L1864
	ldr	r3, [r10, #1200]
	cmp	r3, #0
	it	eq
	moveq	r8, #256
.L1864:
	ldr	r3, [sp, #16]
	cmp	r8, #-1
	add	r3, r3, #36
	str	r8, [r9, r3]
	beq	.L1863
	cmp	r8, #256
	itt	ne
	movne	r2, #0
	strne	r2, [r9, r3]
.L1863:
	ldr	r3, [sp, #12]
	add	r3, r3, r6
	str	r3, [sp, #12]
	ldr	r3, [sp, #20]
	cmp	r3, #1
	beq	.L1865
	ldr	r3, [sp, #24]
	cmp	r3, #0
	bge	.L1831
.L1865:
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1831
	mov	r0, r5
	bl	flash_exit_slc_mode
	b	.L1831
.L1855:
	bl	flash_exit_slc_mode
	b	.L1856
.L1851:
	movs	r6, #0
.L1858:
	cmp	fp, #256
	beq	.L1853
	ldr	r2, [sp, #16]
	movs	r3, #0
	str	r3, [r9, r2]
	b	.L1859
.L1920:
	.align	2
.L1919:
	.word	.LANCHOR0+1220
	.word	.LC102
	.word	.LC99
	.word	.LC101
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	1
	.global	FlashLoadFactorBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r2, #16
	ldr	r7, .L1932
	.pad #60
	sub	sp, sp, #60
	movs	r1, #0
	mov	fp, #-1
	ldr	r4, .L1932+4
	movs	r5, #0
	ldrh	r3, [r7, #136]
	mov	r10, r5
	ldrh	r6, [r7, #138]
	mov	r8, r7
	addw	r0, r4, #1238
	mov	r9, r4
	smulbb	r6, r6, r3
	bl	ftl_memset
	ldr	r3, [r4, #1224]
	uxth	r6, r6
	str	r5, [sp, #28]
	str	r3, [sp, #32]
	add	r3, r6, fp
	uxth	r3, r3
	str	r3, [sp, #8]
.L1922:
	ldrb	r3, [r8, #2234]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bhi	.L1928
	mov	r0, fp
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1928:
	ldr	r4, [sp, #8]
	mul	r3, r7, r6
	sub	r2, r6, #12
	str	r2, [sp, #4]
.L1923:
	ldr	r2, [sp, #4]
	cmp	r4, r2
	ble	.L1925
	adds	r2, r4, r3
	add	r0, sp, #20
	lsls	r2, r2, #10
	str	r3, [sp, #12]
	str	r2, [sp, #24]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #12]
	adds	r2, r2, #1
	beq	.L1924
	ldr	r2, [r9, #1224]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1924
	add	r10, r10, #1
	mov	r1, r7
	uxth	r10, r10
	add	r7, r9, r7, lsl #1
	ldr	r0, .L1932+8
	mov	r2, r4
	bl	printk
	strh	r4, [r7, #1238]	@ movhi
.L1925:
	ldrb	r3, [r8, #2234]	@ zero_extendqisi2
	adds	r5, r5, #1
	cmp	r3, r10
	it	eq
	moveq	fp, #0
	b	.L1922
.L1924:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1923
.L1933:
	.align	2
.L1932:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC103
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	1
	.global	FlashProgSlc2KPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1961
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r1
	ldr	r8, .L1961+20
	.pad #60
	sub	sp, sp, #60
	mov	r9, r2
	mov	r4, r0
	ldrb	fp, [r3, #477]	@ zero_extendqisi2
	mov	r6, r0
	movs	r7, #0
.L1935:
	cmp	r7, r10
	bne	.L1941
	ldr	r8, .L1961+24
	mov	r10, #0
	ldr	fp, .L1961+28
.L1942:
	cmp	r7, r10
	bne	.L1949
	movs	r0, #0
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1941:
	sub	r3, r10, r7
	add	r2, sp, #12
	uxtb	r3, r3
	mov	r1, r9
	mov	r0, r6
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2234]	@ zero_extendqisi2
	ldr	r3, [sp, #16]
	cmp	r2, r3
	bhi	.L1936
	mov	r3, #-1
	str	r3, [r6]
.L1937:
	adds	r7, r7, #1
	adds	r6, r6, #36
	b	.L1935
.L1936:
	add	r3, r3, r8
	ldrb	r5, [r3, #2236]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r1, [sp, #12]
	bl	FlashProgFirstCmd
	ldr	r3, [r6, #12]
	mov	r2, fp
	movs	r1, #1
	mov	r0, r5
	str	r3, [sp]
	ldr	r3, [r6, #8]
	bl	NandcXferData
	mov	r0, r5
	ldr	r1, [sp, #12]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	ldr	r1, [sp, #12]
	bl	FlashReadStatus
	sbfx	r0, r0, #0, #1
	ldr	r1, [sp, #12]
	str	r0, [r6]
	mov	r0, r5
	ldr	r3, [r8, #40]
	add	r1, r1, r3
	bl	FlashProgFirstCmd
	ldr	r3, [r6, #8]
	cbz	r3, .L1938
	add	r3, r3, #2048
.L1938:
	ldr	r2, [r6, #12]
	cbz	r2, .L1939
	adds	r2, r2, #8
.L1939:
	str	r2, [sp]
	movs	r1, #1
	mov	r2, fp
	mov	r0, r5
	bl	NandcXferData
	ldr	r3, [r8, #40]
	mov	r0, r5
	ldr	r1, [sp, #12]
	add	r1, r1, r3
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	ldr	r1, [sp, #12]
	bl	FlashReadStatus
	lsls	r2, r0, #31
	mov	r0, r5
	itt	mi
	movmi	r3, #-1
	strmi	r3, [r6]
	bl	NandcFlashDeCs
	b	.L1937
.L1949:
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L1943
	ldr	r1, [r4, #4]
	ldr	r0, .L1961+4
	bl	printk
.L1944:
	add	r10, r10, #1
	adds	r4, r4, #36
	b	.L1942
.L1943:
	sub	r3, r7, r10
	add	r2, sp, #12
	uxtb	r3, r3
	mov	r1, r9
	mov	r0, r4
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldr	r2, [r8, #1228]
	movs	r3, #0
	mov	r6, r4
	add	r5, sp, #20
	str	r3, [r2]
	ldr	r2, [r8, #1232]
	str	r3, [r2]
	ldmia	r6!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	ldmia	r6!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	mov	r2, r9
	ldr	r3, [r6]
	movs	r1, #1
	add	r0, sp, #20
	str	r3, [r5]
	ldr	r3, [r8, #1228]
	str	r3, [sp, #28]
	ldr	r3, [r8, #1232]
	str	r3, [sp, #32]
	bl	FlashReadPages
	ldr	r5, [sp, #20]
	adds	r3, r5, #1
	bne	.L1945
	ldr	r1, [r4, #4]
	ldr	r0, .L1961+8
	bl	printk
	str	r5, [r4]
.L1945:
	ldr	r5, [sp, #20]
	cmp	r5, #256
	bne	.L1946
	ldr	r1, [r4, #4]
	ldr	r0, .L1961+12
	bl	printk
	str	r5, [r4]
.L1946:
	ldr	r3, [r4, #12]
	cbz	r3, .L1947
	ldr	r2, [r3]
	ldr	r3, [r8, #1232]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1947
	ldr	r1, [r4, #4]
	ldr	r0, .L1961+16
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L1947:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1944
	ldr	r2, [r3]
	ldr	r3, [r8, #1228]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1944
	ldr	r1, [r4, #4]
	mov	r0, fp
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
	b	.L1944
.L1962:
	.align	2
.L1961:
	.word	.LANCHOR1
	.word	.LC104
	.word	.LC105
	.word	.LC106
	.word	.LC107
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC108
	.fnend
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	1
	.global	FtlLoadFactoryBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r6, #0
	ldr	r3, .L1973
	ldr	r7, .L1973+4
	ldr	r5, .L1973+8
	ldr	r2, [r3, #3316]
	ldr	r9, [r3, #3340]
	subw	r8, r7, #2466
	mov	r10, r8
	add	fp, r5, #1256
	str	r2, [r5, #1264]
	str	r9, [r5, #1268]
.L1964:
	ldrh	r3, [r8, #2346]
	cmp	r6, r3
	bcc	.L1969
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1969:
	ldrh	r4, [r8, #2388]
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
	movw	r3, #61664
	subs	r4, r4, #1
	uxth	r4, r4
.L1965:
	ldrh	r2, [r10, #2388]
	sub	r1, r2, #16
	cmp	r4, r1
	ble	.L1967
	mla	r2, r6, r2, r4
	str	r3, [sp, #4]
	mov	r0, fp
	lsls	r2, r2, #10
	str	r2, [r5, #1260]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #1256]
	ldr	r3, [sp, #4]
	adds	r2, r2, #1
	beq	.L1966
	ldrh	r2, [r9]
	cmp	r2, r3
	bne	.L1966
	strh	r4, [r7]	@ movhi
.L1967:
	adds	r6, r6, #1
	b	.L1964
.L1966:
	subs	r4, r4, #1
	uxth	r4, r4
	b	.L1965
.L1974:
	.align	2
.L1973:
	.word	.LANCHOR2
	.word	.LANCHOR0+2466
	.word	.LANCHOR4
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	1
	.global	FtlGetLastWrittenPage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1986
	cmp	r1, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	lsl	r8, r0, #10
	.pad #104
	sub	sp, sp, #104
	mov	r2, r1
	mov	r7, r1
	it	eq
	ldrheq	r5, [r3, #2392]
	mov	r6, #0
	it	ne
	ldrhne	r5, [r3, #2390]
	add	r3, sp, #40
	str	r3, [sp, #16]
	movs	r1, #1
	add	r0, sp, #4
	str	r6, [sp, #12]
	subs	r5, r5, #1
	sxth	r5, r5
	orr	r3, r5, r8
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	adds	r3, r3, #1
	bne	.L1978
.L1979:
	cmp	r6, r5
	ble	.L1982
.L1978:
	mov	r0, r5
	add	sp, sp, #104
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1982:
	adds	r3, r6, r5
	mov	r2, r7
	add	r3, r3, r3, lsr #31
	movs	r1, #1
	add	r0, sp, #4
	asrs	r4, r3, #1
	sxth	r3, r4
	orr	r3, r3, r8
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	adds	r3, r3, #1
	bne	.L1980
	ldr	r3, [sp, #44]
	adds	r3, r3, #1
	bne	.L1980
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L1980
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L1979
.L1980:
	adds	r4, r4, #1
	sxth	r6, r4
	b	.L1979
.L1987:
	.align	2
.L1986:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	1
	.global	FtlLoadBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r10, #61649
	ldr	r8, .L2016+16
	ldr	r5, .L2016
	ldr	r3, [r8, #3316]
	ldr	r7, [r8, #3340]
	ldr	r4, .L2016+4
	add	r9, r5, #1256
	str	r3, [r5, #1264]
	str	r7, [r5, #1268]
	bl	FtlBbtMemInit
	ldrh	r6, [r4, #2388]
	subs	r6, r6, #1
	uxth	r6, r6
.L1989:
	ldrh	r3, [r4, #2388]
	subs	r3, r3, #48
	cmp	r6, r3
	ble	.L1992
	lsls	r3, r6, #10
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	str	r3, [r5, #1260]
	bl	FlashReadPages
	ldr	r3, [r5, #1256]
	adds	r3, r3, #1
	bne	.L1990
	ldr	r3, [r5, #1260]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	adds	r3, r3, #1
	str	r3, [r5, #1260]
	bl	FlashReadPages
.L1990:
	ldr	r3, [r5, #1256]
	adds	r3, r3, #1
	beq	.L1991
	ldrh	r3, [r7]
	cmp	r3, r10
	bne	.L1991
	ldr	r3, [r7, #4]
	strh	r6, [r4, #2456]	@ movhi
	str	r3, [r4, #2464]
	ldrh	r3, [r7, #8]
	strh	r3, [r4, #2460]	@ movhi
.L1992:
	ldrh	r3, [r4, #2456]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L2006
	ldrh	r3, [r4, #2460]
	cmp	r3, r2
	beq	.L1996
	lsls	r3, r3, #10
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L2016+8
	str	r3, [r5, #1260]
	bl	FlashReadPages
	ldr	r3, [r5, #1256]
	adds	r3, r3, #1
	beq	.L1996
	ldrh	r2, [r7]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1996
	ldr	r3, [r7, #4]
	ldr	r2, [r4, #2464]
	cmp	r3, r2
	bls	.L1996
	ldrh	r2, [r4, #2460]
	str	r3, [r4, #2464]
	ldrh	r3, [r7, #8]
	strh	r2, [r4, #2456]	@ movhi
	strh	r3, [r4, #2460]	@ movhi
.L1996:
	ldr	r9, .L2016+8
	movs	r1, #1
	ldrh	r0, [r4, #2456]
	movw	r10, #61649
	bl	FtlGetLastWrittenPage
	sxth	r6, r0
	adds	r0, r0, #1
	strh	r0, [r4, #2458]	@ movhi
.L1998:
	cmp	r6, #0
	blt	.L2003
	ldrh	r3, [r4, #2456]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	orr	r3, r6, r3, lsl #10
	str	r3, [r5, #1260]
	ldr	r3, [r8, #3316]
	str	r3, [r5, #1264]
	bl	FlashReadPages
	ldr	r3, [r5, #1256]
	adds	r3, r3, #1
	beq	.L1999
	ldrh	r3, [r7]
	cmp	r3, r10
	bne	.L1999
.L2003:
	ldrh	r3, [r7, #10]
	ldrh	r0, [r7, #12]
	strh	r3, [r4, #2462]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2000
.L2001:
	ldr	r7, .L2016+12
	movs	r6, #0
.L2004:
	ldrh	r3, [r4, #2346]
	cmp	r6, r3
	bcc	.L2005
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1991:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L1989
.L1999:
	subs	r6, r6, #1
	sxth	r6, r6
	b	.L1998
.L2000:
	ldr	r2, [r4, #2320]
	cmp	r0, r2
	beq	.L2001
	ldrh	r3, [r4, #2334]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L2001
	cmp	r0, r3
	bcs	.L2001
	bl	FtlSysBlkNumInit
	b	.L2001
.L2005:
	ldrh	r2, [r8, #3404]
	ldr	r1, [r5, #1264]
	ldr	r0, [r7, #4]!
	lsls	r2, r2, #2
	mla	r1, r6, r2, r1
	adds	r6, r6, #1
	bl	ftl_memcpy
	b	.L2004
.L2006:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2017:
	.align	2
.L2016:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR4+1256
	.word	.LANCHOR0+2480
	.word	.LANCHOR2
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	1
	.global	FtlScanSysBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r6, #0
	ldr	r5, .L2098
	mov	r1, r6
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L2098+4
	ldr	r2, [r5, #2420]
	mov	fp, r5
	strh	r6, [r5, #2438]	@ movhi
	ldr	r0, [r4, #3396]
	strh	r6, [r4, #3452]	@ movhi
	lsls	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r5, #2420]
	mov	r1, r6
	ldr	r0, [r4, #3368]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2412]
	mov	r1, r6
	ldr	r0, [r4, #3380]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #2412]
	mov	r1, r6
	ldr	r0, [r5, #2440]
	mov	r6, r5
	lsls	r2, r2, #1
	bl	ftl_memset
	movs	r2, #16
	movs	r1, #255
	add	r0, r4, #540
	bl	ftl_memset
	ldrh	r3, [r5, #2332]
	str	r3, [sp, #4]
.L2019:
	ldrh	r3, [r6, #2334]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bls	.L2060
	ldr	r2, [r4, #1148]
	movs	r7, #0
	ldrh	r8, [r6, #2324]
	mov	r10, #36
	ldr	r9, [r4, #3304]
	str	r2, [sp, #8]
	ldr	r3, [r4, #1144]
	ldrh	r2, [r6, #2402]
	str	r7, [sp]
	b	.L2061
.L2022:
	str	r3, [sp, #16]
	ldr	r3, .L2098+8
	ldr	r1, [sp, #4]
	str	r2, [sp, #20]
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #20]
	cbnz	r0, .L2020
	ldr	r1, [sp]
	mla	r0, r10, r1, r9
	ldr	r1, [sp, #12]
	lsls	r1, r1, #10
	str	r3, [r0, #8]
	str	r1, [r0, #4]
	ldr	r1, [sp]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	ip, r1, #3
	ldr	r1, [sp, #8]
	add	r1, r1, ip
	str	r1, [r0, #12]
	ldr	r1, [sp]
	adds	r1, r1, #1
	uxth	r1, r1
	str	r1, [sp]
.L2020:
	adds	r7, r7, #1
.L2061:
	uxth	r1, r7
	cmp	r8, r1
	bhi	.L2022
	ldr	r3, [sp]
	cbnz	r3, .L2023
.L2059:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #4]
	b	.L2019
.L2023:
	movs	r2, #1
	ldr	r1, [sp]
	mov	r0, r9
	bl	FlashReadPages
	movs	r3, #0
.L2096:
	str	r3, [sp, #8]
	ldr	r2, [sp]
	ldrh	r3, [sp, #8]
	cmp	r2, r3
	bls	.L2059
	ldr	r3, [sp, #8]
	movs	r2, #36
	mul	r9, r2, r3
	ldr	r3, [r4, #3304]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r7, [r2, #4]
	ldr	r8, [r2, #12]
	adds	r3, r3, #1
	ubfx	r7, r7, #10, #16
	bne	.L2027
	mov	r10, #16
.L2029:
	ldr	r0, [r4, #3304]
	movs	r2, #1
	mov	r1, r2
	add	r0, r0, r9
	ldr	r3, [r0, #4]
	adds	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r8]
	movw	r2, #65535
	cmp	r3, r2
	ldr	r3, [r4, #3304]
	bne	.L2026
	mov	r2, #-1
	str	r2, [r3, r9]
	ldr	r3, [r4, #3304]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	beq	.L2028
.L2027:
	ldr	r2, [r4, #508]
	ldr	r3, [r8, #4]
	adds	r1, r2, #1
	beq	.L2030
	cmp	r2, r3
	bhi	.L2031
.L2030:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #508]
.L2031:
	ldrh	r2, [r8]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2033
	bhi	.L2034
	movw	r3, #61574
	cmp	r2, r3
	beq	.L2035
.L2032:
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	b	.L2096
.L2026:
	ldr	r3, [r3, r9]
	adds	r3, r3, #1
	bne	.L2027
	add	r10, r10, #-1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L2029
.L2028:
	ldrb	r1, [r6, #152]	@ zero_extendqisi2
	cbnz	r1, .L2097
.L2057:
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
	b	.L2032
.L2034:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2036
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2032
.L2097:
	movs	r1, #0
	b	.L2057
.L2036:
	ldr	r1, [r5, #2420]
	ldrh	r0, [r4, #3452]
	ldr	r9, [r4, #3396]
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L2038:
	cmp	r3, ip
	bgt	.L2044
	cmp	r3, #0
	bge	.L2074
	b	.L2032
.L2044:
	ldr	r2, [r8, #4]
	lsl	r10, r3, #2
	mov	lr, r2
	ldr	r2, [r9, r3, lsl #2]
	cmp	lr, r2
	bls	.L2039
	ldr	r2, [r9]
	cbnz	r2, .L2040
	cmp	r1, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3452]	@ movhi
.L2040:
	uxth	ip, r3
	movs	r2, #0
.L2041:
	uxth	r0, r2
	sxth	r1, r2
	cmp	r0, ip
	bcc	.L2042
	ldr	r2, [r4, #3396]
	cmp	r3, #0
	ldr	r1, [r8, #4]
	str	r1, [r2, r10]
	ldr	r2, [r4, #3368]
	strh	r7, [r2, r3, lsl #1]	@ movhi
	blt	.L2032
	ldrh	r0, [r4, #3452]
	ldr	r2, [r5, #2420]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2032
.L2074:
	ldr	r2, [r4, #3396]
	adds	r0, r0, #1
	ldr	r1, [r8, #4]
	strh	r0, [r4, #3452]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3368]
.L2094:
	strh	r7, [r2, r3, lsl #1]	@ movhi
	b	.L2032
.L2042:
	ldr	r0, [r4, #3396]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #3368]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L2041
.L2039:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L2038
.L2099:
	.align	2
.L2098:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2350
.L2035:
	ldrh	lr, [r5, #2412]
	ldrh	r0, [r5, #2438]
	ldr	r9, [r4, #3380]
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L2047:
	cmp	r3, ip
	ble	.L2052
	ldr	r2, [r8, #4]
	lsl	r10, r3, #2
	ldr	r1, [r9, r3, lsl #2]
	cmp	r2, r1
	bls	.L2048
	ldr	r2, [r9]
	cbnz	r2, .L2049
	cmp	lr, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r6, #2438]	@ movhi
.L2049:
	uxth	ip, r3
	movs	r2, #0
.L2050:
	uxth	r0, r2
	sxth	r1, r2
	cmp	r0, ip
	bcc	.L2051
	ldr	r2, [r4, #3380]
	ldr	r1, [r8, #4]
	str	r1, [r2, r10]
	ldr	r2, [r6, #2440]
	strh	r7, [r2, r3, lsl #1]	@ movhi
.L2052:
	cmp	r3, #0
	blt	.L2032
	ldrh	r2, [r6, #2412]
	ldrh	r1, [r6, #2438]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2032
	adds	r1, r1, #1
	ldr	r2, [r4, #3380]
	strh	r1, [r6, #2438]	@ movhi
	ldr	r1, [r8, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r6, #2440]
	b	.L2094
.L2051:
	ldr	r0, [r4, #3380]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r6, #2440]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L2050
.L2048:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L2047
.L2033:
	ldrh	r1, [r4, #540]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2054
	strh	r7, [r4, #540]	@ movhi
.L2095:
	str	r3, [r4, #548]
	b	.L2032
.L2054:
	ldrh	r0, [r4, #544]
	cmp	r0, r2
	beq	.L2055
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2055:
	ldr	r2, [r4, #548]
	ldr	r3, [r8, #4]
	cmp	r2, r3
	bcs	.L2056
	ldrh	r3, [r4, #540]
	strh	r7, [r4, #540]	@ movhi
	strh	r3, [r4, #544]	@ movhi
	ldr	r3, [r8, #4]
	b	.L2095
.L2056:
	strh	r7, [r4, #544]	@ movhi
	b	.L2032
.L2060:
	ldr	r1, [r4, #3368]
	ldrh	r3, [r1]
	cbz	r3, .L2062
.L2065:
	ldr	r1, [r5, #2440]
	ldrh	r2, [r1]
	cbz	r2, .L2063
.L2085:
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2062:
	ldrh	r2, [r4, #3452]
	cmp	r2, #0
	beq	.L2065
	ldr	r0, [r6, #2420]
.L2066:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L2065
	ldrh	r6, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r6, #0
	beq	.L2066
	mov	r3, r2
	movs	r7, #0
.L2067:
	ldr	r1, [fp, #2420]
	cmp	r3, r1
	bcs	.L2065
	ldr	r1, [r4, #3368]
	subs	r0, r3, r2
	ldrh	r6, [r1, r3, lsl #1]
	strh	r6, [r1, r0, lsl #1]	@ movhi
	ldr	r1, [r4, #3396]
	ldr	r6, [r1, r3, lsl #2]
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r4, #3368]
	strh	r7, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L2067
.L2063:
	ldrh	r3, [r5, #2438]
	cmp	r3, #0
	beq	.L2085
	ldrh	r0, [r5, #2412]
.L2070:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L2085
	ldrh	r6, [r1, r3, lsl #1]
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L2070
	movs	r0, #0
.L2071:
	ldrh	r2, [fp, #2412]
	cmp	r3, r2
	bge	.L2085
	ldr	r2, [fp, #2440]
	subs	r1, r3, r5
	ldrh	r6, [r2, r3, lsl #1]
	strh	r6, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #3380]
	ldr	r6, [r2, r3, lsl #2]
	str	r6, [r2, r1, lsl #2]
	ldr	r2, [fp, #2440]
	strh	r0, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L2071
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	1
	.global	FtlLoadSysInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r1, #0
	ldr	r4, .L2128
	.pad #36
	sub	sp, sp, #36
	ldr	r5, .L2128+4
	ldr	r3, [r4, #3316]
	ldr	r6, .L2128+8
	ldrh	r2, [r5, #2332]
	ldr	r0, [r4, #300]
	str	r3, [r6, #1264]
	ldr	r3, [r4, #3340]
	lsls	r2, r2, #1
	str	r3, [r6, #1268]
	bl	ftl_memset
	ldrh	r0, [r4, #540]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2101
.L2112:
	mov	r0, #-1
.L2100:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2101:
	movs	r1, #1
	ldr	fp, .L2128+36
	bl	FtlGetLastWrittenPage
	ldrsh	r9, [r4, #540]
	add	r10, r6, #1256
	sxth	r7, r0
	adds	r0, r0, #1
	strh	r0, [r4, #542]	@ movhi
.L2103:
	cmp	r7, #0
	blt	.L2111
	orr	r3, r7, r9, lsl #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r6, #1260]
	mov	r0, r10
	ldr	r3, [r4, #3316]
	str	r3, [r6, #1264]
	bl	FlashReadPages
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2104
	ldr	r8, [r6, #1268]
	ldr	r3, [r8, #12]
	str	r3, [sp, #28]
	cbz	r3, .L2104
	ldr	r2, [r6, #1264]
	ldrh	r1, [r5, #2400]
	mov	r0, r2
	str	r2, [sp, #24]
	bl	js_hash
	ldr	r3, [sp, #28]
	cmp	r3, r0
	beq	.L2104
	cbnz	r7, .L2105
	ldrh	r1, [r4, #544]
	ldr	r2, [sp, #24]
	cmp	r9, r1
	beq	.L2105
	ldr	r2, [r2]
	mov	r0, fp
	str	r3, [sp, #12]
	ldrh	r1, [r4, #540]
	str	r2, [sp, #16]
	ldr	r3, [r8, #8]
	ldr	r2, [r6, #1256]
	str	r3, [sp, #8]
	ldr	r3, [r8, #4]
	str	r3, [sp, #4]
	ldr	r3, [r8]
	str	r3, [sp]
	ldr	r3, [r6, #1260]
	bl	printk
	ldrsh	r9, [r4, #544]
	ldrh	r7, [r5, #2392]
.L2107:
	subs	r7, r7, #1
	sxth	r7, r7
	b	.L2103
.L2105:
	mov	r3, #-1
	str	r3, [r6, #1256]
.L2104:
	ldr	r3, [r6, #1256]
	adds	r3, r3, #1
	beq	.L2107
	ldr	r3, [r4, #3316]
	ldr	r2, .L2128+12
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2107
	ldr	r3, [r4, #3340]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2107
.L2111:
	movs	r2, #48
	ldr	r1, [r6, #1264]
	ldr	r0, .L2128+16
	bl	ftl_memcpy
	ldrh	r2, [r5, #2332]
	ldr	r1, [r6, #1264]
	ldr	r0, [r4, #300]
	lsls	r2, r2, #1
	adds	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r5, #2332]
	ldr	r3, [r6, #1264]
	ldr	r0, [r5, #32]
	lsrs	r2, r1, #3
	lsls	r1, r1, #1
	adds	r1, r1, #51
	adds	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r1, r3
	bl	ftl_memcpy
	ldrh	r3, [r5, #2436]
	cbz	r3, .L2109
	ldrh	r1, [r5, #2332]
	ldrh	r2, [r5, #2428]
	ldr	r0, [r4, #3392]
	lsrs	r3, r1, #3
	lsls	r2, r2, #2
	add	r3, r3, r1, lsl #1
	ldr	r1, [r6, #1264]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	add	r1, r1, r3, lsl #2
	bl	ftl_memcpy
.L2109:
	ldr	r2, [r4, #244]
	ldr	r3, .L2128+12
	cmp	r2, r3
	bne	.L2112
	ldrb	r1, [r4, #254]	@ zero_extendqisi2
	ldrh	r3, [r5, #2346]
	ldrh	r2, [r4, #252]
	cmp	r1, r3
	strh	r2, [r4, #546]	@ movhi
	bne	.L2112
	ldrh	r3, [r5, #2390]
	ldrh	r1, [r5, #2396]
	ldr	r0, [r5, #2336]
	str	r2, [r6, #1292]
	muls	r3, r2, r3
	str	r3, [r5, #2452]
	muls	r3, r1, r3
	ldrh	r1, [r5, #2324]
	str	r3, [r5, #2432]
	ldrh	r3, [r5, #2462]
	subs	r0, r0, r3
	subs	r0, r0, r2
	bl	__aeabi_uidiv
	ldrh	r3, [r4, #260]
	strh	r0, [r4, #536]	@ movhi
	ldrh	r1, [r4, #258]
	lsrs	r2, r3, #6
	and	r3, r3, #63
	strb	r3, [r4, #326]
	strh	r2, [r4, #322]	@ movhi
	ldrh	r2, [r4, #262]
	ldrb	r3, [r4, #255]	@ zero_extendqisi2
	strh	r1, [r4, #320]	@ movhi
	strh	r2, [r4, #368]	@ movhi
	ldrh	r2, [r4, #264]
	strb	r3, [r4, #328]
	movw	r3, #65535
	strh	r3, [r4, #556]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #558]	@ movhi
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #374]
	ldrb	r2, [r4, #256]	@ zero_extendqisi2
	strh	r0, [r4, #370]	@ movhi
	strb	r3, [r4, #562]
	strb	r2, [r4, #376]
	ldrh	r2, [r4, #266]
	strb	r3, [r4, #564]
	strh	r2, [r4, #416]	@ movhi
	ldrh	r2, [r4, #268]
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #422]
	ldrb	r2, [r4, #257]	@ zero_extendqisi2
	strh	r0, [r4, #418]	@ movhi
	strb	r2, [r4, #424]
	str	r3, [r4, #496]
	ldr	r2, [r4, #276]
	str	r3, [r4, #484]
	str	r3, [r4, #476]
	str	r3, [r4, #492]
	str	r2, [r4, #516]
	str	r3, [r4, #520]
	ldr	r2, [r4, #508]
	str	r3, [r4, #528]
	str	r3, [r4, #488]
	ldr	r3, [r4, #284]
	cmp	r3, r2
	ldr	r2, [r4, #512]
	it	hi
	strhi	r3, [r4, #508]
	ldr	r3, [r4, #280]
	cmp	r3, r2
	it	hi
	strhi	r3, [r4, #512]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2115
	ldr	r0, .L2128+20
	bl	make_superblock
.L2115:
	ldrh	r2, [r4, #368]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2116
	ldr	r0, .L2128+24
	bl	make_superblock
.L2116:
	ldrh	r2, [r4, #416]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2117
	ldr	r0, .L2128+28
	bl	make_superblock
.L2117:
	ldrh	r2, [r4, #556]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2118
	ldr	r0, .L2128+32
	bl	make_superblock
.L2118:
	movs	r0, #0
	b	.L2100
.L2129:
	.align	2
.L2128:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	1179929683
	.word	.LANCHOR2+244
	.word	.LANCHOR2+320
	.word	.LANCHOR2+368
	.word	.LANCHOR2+416
	.word	.LANCHOR2+556
	.word	.LC109
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	1
	.global	FtlDumpBlockInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ubfx	r0, r0, #10, #16
	ldr	r4, .L2144
	.pad #100
	sub	sp, sp, #100
	mov	r9, r1
	ldr	r7, .L2144+4
	bl	P2V_block_in_plane
	ldr	r1, .L2144+8
	mov	r6, r0
	ldr	r0, .L2144+12
	ldrh	r8, [r4, #2390]
	bl	printk
	ldr	r3, [r7, #300]
	mov	r1, r6
	ldr	r0, .L2144+16
	ldrh	r2, [r3, r6, lsl #1]
	bl	printk
	add	r0, sp, #96
	strh	r6, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r5, [r4, #152]	@ zero_extendqisi2
	str	r7, [sp, #44]
	cbz	r5, .L2131
	cmp	r9, #0
	bne	.L2142
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r5, r0
	bne	.L2131
	ldrh	r8, [r4, #2392]
.L2131:
	ldr	r9, .L2144
	mov	r2, r8
	ldrh	r3, [r4, #2390]
	mov	r1, r5
	ldr	r0, .L2144+20
	mov	r10, #36
	bl	printk
	movs	r3, #0
	str	r3, [sp, #28]
.L2132:
	ldr	r3, .L2144+4
	add	ip, sp, #62
	ldrh	fp, [r9, #2324]
	movw	lr, #65535
	ldrh	r7, [r9, #2402]
	ldr	r2, [r3, #1144]
	ldr	r0, [r3, #3304]
	ldr	r3, [r3, #1148]
	str	r2, [sp, #32]
	ldrh	r2, [r9, #2400]
	str	r3, [sp, #40]
	str	r2, [sp, #36]
	movs	r2, #0
	mov	r4, r2
.L2133:
	uxth	r3, r2
	cmp	fp, r3
	bhi	.L2137
	ldr	fp, .L2144+24
	movs	r7, #0
	mov	r2, r5
	mov	r1, r4
	bl	FlashReadPages
.L2138:
	uxth	r3, r7
	cmp	r4, r3
	bhi	.L2139
	ldr	r6, [sp, #28]
	adds	r6, r6, #1
	uxth	r3, r6
	cmp	r8, r3
	str	r3, [sp, #28]
	bne	.L2132
.L2140:
	movs	r0, #0
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2142:
	movs	r5, #0
	b	.L2131
.L2137:
	ldrh	r3, [ip, #2]!
	cmp	r3, lr
	beq	.L2134
	ldr	r6, [sp, #28]
	mla	r1, r10, r4, r0
	orr	r3, r6, r3, lsl #10
	ldr	r6, [sp, #32]
	str	r3, [r1, #4]
	ldr	r3, [sp, #36]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r6
	ldr	r6, [sp, #40]
	str	r3, [r1, #8]
	mov	r3, r7
	muls	r3, r4, r3
	add	r4, r4, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r4, r4
	bic	r3, r3, #3
	add	r3, r3, r6
	str	r3, [r1, #12]
.L2134:
	adds	r2, r2, #1
	b	.L2133
.L2139:
	ldr	r3, [sp, #44]
	mul	r0, r10, r7
	ldrh	r1, [sp, #48]
	adds	r7, r7, #1
	ldr	ip, [r3, #3304]
	add	r2, ip, r0
	ldr	lr, [r2, #8]
	ldr	r3, [r2, #12]
	ldr	r6, [lr, #4]
	str	r6, [sp, #20]
	ldr	r6, [lr]
	str	r6, [sp, #16]
	ldr	r6, [r3, #12]
	str	r6, [sp, #12]
	ldr	r6, [r3, #8]
	str	r6, [sp, #8]
	ldr	r6, [r3, #4]
	str	r6, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp]
	ldr	r3, [r2, #4]
	ldr	r2, [ip, r0]
	mov	r0, fp
	bl	printk
	b	.L2138
.L2145:
	.align	2
.L2144:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+141
	.word	.LC110
	.word	.LC111
	.word	.LC112
	.word	.LC113
	.fnend
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
	.align	1
	.global	FtlScanAllBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r7, #0
	ldr	r5, .L2158
	.pad #84
	sub	sp, sp, #84
	ldr	r1, .L2158+4
	ldr	r0, .L2158+8
	bl	printk
.L2147:
	ldr	r3, .L2158+12
	uxth	r0, r7
	ldrh	r3, [r3, #2334]
	cmp	r3, r0
	bhi	.L2157
	movs	r0, #0
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2157:
	add	r4, sp, #80
	movw	r10, #65535
	strh	r0, [r4, #-48]!	@ movhi
	mov	fp, #36
	bl	ftl_get_blk_mode
	ldr	r2, [r5, #300]
	uxth	r1, r7
	mov	r3, r0
	ldr	r0, .L2158+16
	ldrh	r2, [r2, r1, lsl #1]
	bl	printk
	mov	r0, r4
	bl	make_superblock
	ldr	r3, .L2158+12
	movs	r2, #0
	ldr	r0, [r5, #3304]
	add	r8, sp, #46
	ldr	r9, [r5, #1148]
	mov	r4, r2
	ldrh	ip, [r3, #2324]
	ldr	r3, [r5, #1144]
	str	r3, [sp, #24]
	ldr	r3, .L2158+12
	ldrh	r3, [r3, #2400]
	str	r3, [sp, #28]
	ldr	r3, .L2158+12
	ldrh	lr, [r3, #2402]
.L2148:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L2152
	ldr	r10, .L2158+20
	mov	r8, #0
	mov	r9, #36
	movs	r2, #0
	mov	r1, r4
	bl	FlashReadPages
.L2153:
	uxth	r3, r8
	cmp	r4, r3
	bhi	.L2154
	ldr	r10, .L2158+24
	mov	r8, #0
	mov	r9, #36
	movs	r2, #1
	mov	r1, r4
	ldr	r0, [r5, #3304]
	bl	FlashReadPages
.L2155:
	uxth	r3, r8
	cmp	r4, r3
	bhi	.L2156
	adds	r7, r7, #1
	b	.L2147
.L2152:
	ldrh	r3, [r8, #2]!
	cmp	r3, r10
	beq	.L2149
	mla	r1, fp, r4, r0
	ldr	r6, [sp, #24]
	lsls	r3, r3, #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #28]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r6
	str	r3, [r1, #8]
	mov	r3, lr
	muls	r3, r4, r3
	add	r4, r4, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r4, r4
	bic	r3, r3, #3
	add	r3, r3, r9
	str	r3, [r1, #12]
.L2149:
	adds	r2, r2, #1
	b	.L2148
.L2154:
	mul	r2, r9, r8
	ldr	r0, [r5, #3304]
	ldrh	r1, [sp, #32]
	add	r8, r8, #1
	add	lr, r0, r2
	ldr	fp, [lr, #8]
	ldr	r3, [lr, #12]
	ldr	r6, [fp, #4]
	str	r6, [sp, #20]
	ldr	r6, [fp]
	str	r6, [sp, #16]
	ldr	r6, [r3, #12]
	str	r6, [sp, #12]
	ldr	r6, [r3, #8]
	str	r6, [sp, #8]
	ldr	r6, [r3, #4]
	str	r6, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp]
	ldr	r2, [r0, r2]
	mov	r0, r10
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2153
.L2156:
	mul	r2, r9, r8
	ldr	r0, [r5, #3304]
	ldrh	r1, [sp, #32]
	add	r8, r8, #1
	add	lr, r0, r2
	ldr	fp, [lr, #8]
	ldr	r3, [lr, #12]
	ldr	r6, [fp, #4]
	str	r6, [sp, #20]
	ldr	r6, [fp]
	str	r6, [sp, #16]
	ldr	r6, [r3, #12]
	str	r6, [sp, #12]
	ldr	r6, [r3, #8]
	str	r6, [sp, #8]
	ldr	r6, [r3, #4]
	str	r6, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp]
	ldr	r2, [r0, r2]
	mov	r0, r10
	ldr	r3, [lr, #4]
	bl	printk
	b	.L2155
.L2159:
	.align	2
.L2158:
	.word	.LANCHOR2
	.word	.LANCHOR3+158
	.word	.LC110
	.word	.LANCHOR0
	.word	.LC114
	.word	.LC115
	.word	.LC116
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	1
	.global	ftl_scan_all_ppa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L2176
	ldr	r6, .L2176+4
	ldrh	r4, [r5, #2388]
	subs	r4, r4, #16
	lsl	r10, r4, #10
.L2161:
	ldrh	r3, [r5, #2388]
	cmp	r4, r3
	blt	.L2169
	ldr	r1, .L2176+8
	ldr	r0, .L2176+12
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L2169:
	uxth	r8, r4
	mov	r0, r8
	bl	ftl_get_blk_mode
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cbz	r3, .L2162
	ldrh	r3, [r5, #2332]
	cmp	r4, r3
	bge	.L2163
	ldrh	r3, [r5, #2404]
	cmp	r4, r3
	blt	.L2163
.L2162:
	cmp	r0, #1
	bne	.L2164
.L2163:
	ldrh	r7, [r5, #2392]
	mov	r9, #-2147483648
.L2165:
	mov	r3, r9
	mov	r2, r7
	mov	r1, r4
	ldr	r0, .L2176+16
	bl	printk
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbz	r0, .L2166
	mov	r3, r9
	mov	r2, r7
	mov	r1, r4
	ldr	r0, .L2176+20
	bl	printk
.L2166:
	ldr	fp, .L2176+32
	mov	r8, #0
.L2167:
	cmp	r8, r7
	bne	.L2168
	adds	r4, r4, #1
	add	r10, r10, #1024
	b	.L2161
.L2164:
	ldrh	r7, [r5, #2390]
	mov	r9, #0
	b	.L2165
.L2168:
	add	r3, r9, r10
	movs	r2, #0
	add	r3, r3, r8
	movs	r1, #1
	str	r3, [r6, #1260]
	add	r8, r8, #1
	ldr	r3, [fp, #3316]
	ldr	r0, .L2176+24
	str	r2, [r6, #1256]
	str	r3, [r6, #1264]
	ldr	r3, [fp, #3340]
	str	r3, [r6, #1268]
	bl	FlashReadPages
	ldr	r2, [r6, #1264]
	ldr	r3, [r6, #1268]
	ldr	r0, .L2176+28
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	ldr	r2, [r2]
	ldr	r1, [r6, #1260]
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r2, [r6, #1256]
	ldr	r3, [r3]
	bl	printk
	b	.L2167
.L2177:
	.align	2
.L2176:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR3+174
	.word	.LC120
	.word	.LC117
	.word	.LC118
	.word	.LANCHOR4+1256
	.word	.LC119
	.word	.LANCHOR2
	.fnend
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
	.align	1
	.global	FlashProgPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	r5, .L2230
	mov	r4, r0
	mov	r9, r2
	str	r1, [sp, #8]
	ldr	r6, [r5, #48]
	ldrb	r7, [r5, #36]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldrb	r6, [r6, #19]	@ zero_extendqisi2
	str	r6, [sp, #16]
	cbnz	r7, .L2179
	ldr	r3, .L2230+4
	ldrb	r3, [r3, #477]	@ zero_extendqisi2
	str	r3, [sp, #12]
.L2180:
	ldr	r3, [sp, #8]
	cmp	r7, r3
	bcc	.L2193
	ldr	r7, .L2230+8
	movs	r6, #0
.L2194:
	ldrb	r3, [r5, #2234]	@ zero_extendqisi2
	cmp	r6, r3
	bcc	.L2196
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L2197
.L2205:
	movs	r0, #0
	b	.L2178
.L2179:
	bl	FlashProgSlc2KPages
.L2178:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2193:
	movs	r3, #36
	add	r2, sp, #28
	mul	r8, r3, r7
	ldr	r3, [sp, #8]
	mov	r1, r9
	subs	r3, r3, r7
	uxtb	r3, r3
	add	fp, r4, r8
	mov	r0, fp
	str	r3, [sp]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldrb	r3, [r5, #2234]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r0, [sp, #32]
	cmp	r3, r0
	bhi	.L2182
	mov	r3, #-1
	str	r3, [r4, r8]
.L2183:
	adds	r7, r7, #1
	b	.L2180
.L2182:
	ldrb	r3, [r5, #2244]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r5, r0, lsl #4
	it	eq
	moveq	r10, #0
	ldr	r3, [r3, #2112]
	cbz	r3, .L2185
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L2185:
	ldr	r3, [sp, #32]
	movs	r1, #0
	add	r2, r5, r3, lsl #4
	str	r1, [r2, #2116]
	ldr	r1, [sp, #28]
	str	fp, [r2, #2112]
	str	r1, [r2, #2108]
	cmp	r10, #0
	beq	.L2186
	add	r1, r8, #36
	add	r1, r1, r4
	str	r1, [r2, #2116]
.L2186:
	adds	r2, r5, r3
	add	r3, r5, r3, lsl #4
	ldrb	r6, [r2, #2236]	@ zero_extendqisi2
	strb	r6, [r3, #2104]
	mov	r0, r6
	ldrb	r3, [r5, #2234]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2187
	bl	NandcWaitFlashReady
.L2188:
	ldr	r3, [sp, #16]
	subs	r3, r3, #1
	cmp	r3, #7
	bhi	.L2189
	adds	r3, r5, r6
	ldrb	r3, [r3, #2068]	@ zero_extendqisi2
	cbz	r3, .L2189
	movs	r3, #0
	ldr	r2, .L2230+12
	ldrb	r1, [r5, #1217]	@ zero_extendqisi2
	mov	r0, r6
	bl	HynixSetRRPara
.L2189:
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r6
	bne	.L2190
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2190
	bl	flash_enter_slc_mode
.L2191:
	ldr	r1, [sp, #28]
	mov	r0, r6
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	movs	r1, #1
	ldr	r2, [sp, #12]
	mov	r0, r6
	str	r3, [sp]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L2192
	ldr	r1, [sp, #28]
	mov	r0, r6
	bl	FlashProgDpFirstCmd
	ldr	r3, [sp, #32]
	mov	r0, r6
	ldr	r1, [sp, #28]
	add	r8, r8, #36
	add	r8, r8, r4
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #1180]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r3, [r5, #40]
	mov	r0, r6
	ldr	r1, [sp, #28]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r8, #12]
	movs	r1, #1
	ldr	r2, [sp, #12]
	mov	r0, r6
	str	r3, [sp]
	ldr	r3, [r8, #8]
	bl	NandcXferData
.L2192:
	ldr	r1, [sp, #28]
	mov	r0, r6
	bl	FlashProgSecondCmd
	mov	r0, r6
	add	r7, r7, r10
	bl	NandcFlashDeCs
	b	.L2183
.L2187:
	bl	NandcFlashCs
	ldr	r3, [sp, #32]
	mov	r0, r6
	ldr	r1, [sp, #28]
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #1180]
	adds	r2, r2, #0
	it	ne
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	bl	NandcFlashDeCs
	b	.L2188
.L2190:
	bl	flash_exit_slc_mode
	b	.L2191
.L2196:
	uxtb	r0, r6
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2195
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cbz	r3, .L2195
	lsls	r3, r6, #4
	ldrb	r0, [r7, r3]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2195:
	adds	r6, r6, #1
	b	.L2194
.L2197:
	ldr	r7, .L2230+16
	mov	r8, #0
	ldr	r10, .L2230+32
.L2198:
	ldr	r3, [sp, #8]
	cmp	r8, r3
	beq	.L2205
	ldr	r3, [r4]
	adds	r3, r3, #1
	bne	.L2199
	ldr	r1, [r4, #4]
	ldr	r0, .L2230+20
	bl	printk
.L2200:
	add	r8, r8, #1
	adds	r4, r4, #36
	b	.L2198
.L2199:
	ldr	r3, [sp, #8]
	add	r2, sp, #28
	mov	r1, r9
	mov	r0, r4
	mov	r6, r4
	sub	r3, r3, r8
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r7, #1228]
	movs	r3, #0
	add	r5, sp, #36
	str	r3, [r2]
	ldr	r2, [r7, #1232]
	str	r3, [r2]
	ldmia	r6!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	ldmia	r6!, {r0, r1, r2, r3}
	stmia	r5!, {r0, r1, r2, r3}
	mov	r2, r9
	ldr	r3, [r6]
	movs	r1, #1
	add	r0, sp, #36
	str	r3, [r5]
	ldr	r3, [r7, #1228]
	str	r3, [sp, #44]
	ldr	r3, [r7, #1232]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r5, [sp, #36]
	adds	r3, r5, #1
	bne	.L2201
	ldr	r1, [r4, #4]
	ldr	r0, .L2230+24
	bl	printk
	str	r5, [r4]
.L2201:
	ldr	r3, [r4, #12]
	cbz	r3, .L2202
	ldr	r2, [r3]
	ldr	r3, [r7, #1232]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2202
	ldr	r1, [r4, #4]
	ldr	r0, .L2230+28
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L2202:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2200
	ldr	r2, [r3]
	ldr	r3, [r7, #1228]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2200
	ldr	r1, [r4, #4]
	mov	r0, r10
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
	b	.L2200
.L2231:
	.align	2
.L2230:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+2104
	.word	.LANCHOR0+1220
	.word	.LANCHOR4
	.word	.LC104
	.word	.LC105
	.word	.LC107
	.word	.LC108
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	1
	.global	FlashTestBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, r0
	ldr	r5, .L2235
	.pad #108
	sub	sp, sp, #108
	ldr	r3, [r5, #1212]
	cmp	r0, r3
	bcc	.L2234
	ldr	r3, [r5, #1220]
	add	r0, sp, #40
	movs	r2, #32
	movs	r1, #165
	str	r0, [sp, #16]
	lsls	r4, r4, #10
	str	r3, [sp, #12]
	bl	ftl_memset
	movs	r2, #8
	movs	r1, #90
	ldr	r0, [r5, #1220]
	bl	ftl_memset
	movs	r2, #1
	add	r0, sp, #4
	mov	r1, r2
	str	r4, [sp, #8]
	bl	FlashEraseBlocks
	movs	r3, #1
	add	r0, sp, #4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r4, [sp, #4]
	movs	r2, #1
	movs	r1, #0
	add	r0, sp, #4
	adds	r4, r4, #0
	it	ne
	movne	r4, #1
	negs	r4, r4
	bl	FlashEraseBlocks
.L2232:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, pc}
.L2234:
	movs	r4, #0
	b	.L2232
.L2236:
	.align	2
.L2235:
	.word	.LANCHOR4
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	1
	.global	FlashMakeFactorBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r4, .L2293
	ldr	r0, .L2293+4
	ldr	r3, [r4, #1224]
	ldr	r5, .L2293
	str	r3, [sp, #24]
	ldr	r3, .L2293+8
	ldrh	r1, [r3, #136]
	ldrh	r6, [r3, #138]
	smulbb	r6, r6, r1
	ldr	r1, [r3, #48]
	ldrb	r2, [r1, #24]	@ zero_extendqisi2
	uxth	r6, r6
	movs	r1, #1
	str	r2, [sp, #28]
	ldrh	r2, [r3, #40]
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	str	r2, [sp, #20]
	cmp	r3, #1
	itttt	eq
	moveq	r3, r2
	lsleq	r3, r3, #1
	uxtheq	r3, r3
	streq	r3, [sp, #20]
	bl	printk
	ldr	r0, [r4, #1224]
	mov	r2, #4096
	movs	r1, #0
	ldr	r4, .L2293+8
	bl	ftl_memset
	lsrs	r3, r6, #4
	str	r3, [sp, #32]
	movs	r3, #0
	str	r3, [sp, #12]
.L2239:
	ldrb	r8, [sp, #12]	@ zero_extendqisi2
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	cmp	r3, r8
	bhi	.L2266
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2266:
	add	r3, r5, r8, lsl #1
	ldrh	r7, [r3, #1238]
	cmp	r7, #0
	bne	.L2240
	ldrh	r2, [r4, #144]
	mov	r1, r7
	ldr	r0, [r5, #1192]
	add	fp, r4, r8, lsl #2
	mov	r9, r7
	mov	r10, r7
	lsls	r2, r2, #9
	bl	ftl_memset
	add	r3, r4, r8
	str	r7, [sp, #4]
	ldrb	r3, [r3, #2236]	@ zero_extendqisi2
	str	r3, [sp, #8]
.L2241:
	ldrh	r3, [sp, #4]
	cmp	r3, r6
	str	r3, [sp, #16]
	bcc	.L2252
.L2251:
	mov	r2, r9
	mov	r1, r8
	ldr	r0, .L2293+12
	bl	printk
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	ldr	r2, [sp, #32]
	muls	r3, r2, r3
	cmp	r9, r3
	blt	.L2253
	ldrh	r2, [r4, #144]
	movs	r1, #0
	ldr	r0, [r5, #1192]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2253:
	cmp	r8, #0
	bne	.L2255
	ldrh	fp, [r5, #1212]
	mov	r10, #1
	ldr	r9, .L2293+20
.L2256:
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	cmp	r3, fp
	bhi	.L2258
	subs	r3, r6, #1
	sub	r9, r6, #50
	uxth	r10, r3
	mov	fp, #1
.L2259:
	cmp	r10, r9
	bgt	.L2261
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	ldr	r2, [r5, #1212]
	subs	r3, r3, r2
	cmp	r7, r3
	bcc	.L2255
	ldrh	r2, [r4, #144]
	movs	r1, #0
	ldr	r0, [r5, #1192]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2255:
	ldrb	r9, [sp, #12]	@ zero_extendqisi2
	subs	r7, r6, #1
	ldr	fp, .L2293+24
	uxth	r7, r7
	add	r10, r5, r8, lsl #1
	mul	r9, r6, r9
.L2263:
	mov	r1, r8
	mov	r2, r7
	mov	r0, fp
	bl	printk
	ldr	r1, [r5, #1192]
.L2264:
	lsrs	r2, r7, #5
	and	r3, r7, #31
	ldr	r2, [r1, r2, lsl #2]
	lsr	r3, r2, r3
	ands	r3, r3, #1
	bne	.L2265
	ldr	r1, [sp, #24]
	movw	r2, #61664
	strh	r7, [r10, #1238]	@ movhi
	add	r0, sp, #44
	strh	r2, [r1]	@ movhi
	movs	r2, #1
	strh	r7, [r1, #2]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	mov	r1, r2
	ldr	r3, [r5, #1192]
	str	r3, [sp, #52]
	ldr	r3, [r5, #1224]
	str	r3, [sp, #56]
	add	r3, r7, r9
	lsls	r3, r3, #10
	str	r3, [sp, #48]
	bl	FlashEraseBlocks
	movs	r3, #1
	add	r0, sp, #44
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [sp, #44]
	cmp	r3, #0
	beq	.L2240
	subs	r7, r7, #1
	uxth	r7, r7
	b	.L2263
.L2252:
	movs	r3, #255
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #28]
	lsls	r1, r3, #31
	bpl	.L2243
	ldr	r3, [fp, #1180]
	add	r2, sp, #42
	ldr	r0, [sp, #8]
	add	r3, r3, r10
	mov	r1, r3
	str	r3, [sp, #36]
	bl	FlashReadSpare
	ldrb	r2, [r4, #36]	@ zero_extendqisi2
	ldr	r3, [sp, #36]
	cmp	r2, #1
	bne	.L2243
	ldr	r1, [r4, #40]
	add	r2, sp, #43
	ldr	r0, [sp, #8]
	add	r1, r1, r3
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	ands	r3, r3, r2
	strb	r3, [sp, #42]
.L2243:
	ldr	r3, [sp, #28]
	lsls	r2, r3, #30
	bpl	.L2245
	ldr	r3, [r4, #48]
	add	r2, sp, #43
	ldr	r0, [sp, #8]
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1180]
	subs	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r10
	bl	FlashReadSpare
.L2245:
	ldr	r3, [r4, #48]
	ldrb	r2, [r3, #7]	@ zero_extendqisi2
	cmp	r2, #1
	beq	.L2246
	cmp	r2, #8
	beq	.L2246
	ldrb	r3, [r3, #18]	@ zero_extendqisi2
	cmp	r3, #12
	bne	.L2247
.L2246:
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2268
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	clz	r0, r0
	lsrs	r0, r0, #5
.L2248:
	ldr	r3, [sp, #28]
	lsls	r3, r3, #29
	bpl	.L2249
	ldr	r1, [fp, #1180]
	ldr	r0, [sp, #8]
	add	r1, r1, r10
	bl	SandiskProgTestBadBlock
.L2249:
	cbz	r0, .L2250
	ldr	r2, [sp, #4]
	mov	r1, r8
	ldr	r0, .L2293+16
	add	r9, r9, #1
	bl	printk
	ldr	r3, [sp, #16]
	uxth	r9, r9
	ldr	r1, [r5, #1192]
	and	r2, r3, #31
	lsrs	r0, r3, #5
	movs	r3, #1
	lsl	r2, r3, r2
	ldr	r3, [r1, r0, lsl #2]
	orrs	r3, r3, r2
	ldr	r2, [sp, #32]
	str	r3, [r1, r0, lsl #2]
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	muls	r3, r2, r3
	cmp	r9, r3
	bgt	.L2251
.L2250:
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	add	r10, r10, r3
	b	.L2241
.L2247:
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	cmp	r3, #255
	bne	.L2268
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	it	ne
	movne	r0, #1
	b	.L2248
.L2268:
	movs	r0, #1
	b	.L2248
.L2258:
	mov	r0, fp
	bl	FlashTestBlk
	cbz	r0, .L2257
	mov	r1, fp
	mov	r0, r9
	bl	printk
	ldr	r1, [r5, #1192]
	lsr	r0, fp, #5
	and	r3, fp, #31
	lsl	r2, r10, r3
	adds	r7, r7, #1
	ldr	r3, [r1, r0, lsl #2]
	uxth	r7, r7
	orrs	r3, r3, r2
	str	r3, [r1, r0, lsl #2]
.L2257:
	add	fp, fp, #1
	uxth	fp, fp
	b	.L2256
.L2261:
	mov	r0, r10
	bl	FlashTestBlk
	cbz	r0, .L2260
	mov	r1, r10
	ldr	r0, .L2293+20
	bl	printk
	ldr	r0, [r5, #1192]
	lsr	ip, r10, #5
	and	r2, r10, #31
	lsl	r1, fp, r2
	ldr	r2, [r0, ip, lsl #2]
	orrs	r2, r2, r1
	str	r2, [r0, ip, lsl #2]
.L2260:
	add	r3, r10, #-1
	uxth	r10, r3
	b	.L2259
.L2265:
	subs	r7, r7, #1
	uxth	r7, r7
	b	.L2264
.L2240:
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	str	r3, [sp, #12]
	b	.L2239
.L2294:
	.align	2
.L2293:
	.word	.LANCHOR4
	.word	.LC121
	.word	.LANCHOR0
	.word	.LC123
	.word	.LC122
	.word	.LC124
	.word	.LC125
	.fnend
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	1
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2342
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	str	r0, [sp, #8]
	mov	r10, r3
	ldr	r6, [r3, #228]
	str	r1, [sp, #4]
	cmp	r6, #0
	bne	.L2324
	ldr	r5, .L2342+4
	mov	r7, r6
	mov	r4, r6
	mov	r9, #36
	mov	fp, r10
	str	r0, [r10, #3292]
	ldrb	r3, [r5, #2244]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	str	r3, [sp, #12]
.L2297:
	ldrh	r0, [r5, #2324]
	uxth	r1, r6
	cmp	r0, r1
	bhi	.L2302
	cmp	r7, #0
	beq	.L2295
	ldr	r3, [sp, #12]
	mov	r8, #0
	mov	r2, r7
	ldr	r0, [r10, #232]
	strb	r8, [r5, #2244]
	mov	r9, #36
	adds	r6, r3, #0
	it	ne
	movne	r6, #1
	mov	r1, r6
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r5, #2244]
.L2304:
	uxth	r2, r8
	cmp	r7, r2
	bhi	.L2306
	ldr	r3, [sp, #4]
	cmp	r3, #0
	bne	.L2307
	uxth	r6, r6
	mov	fp, #6
	movs	r3, #1
	str	r3, [sp, #12]
.L2308:
	ldr	r7, .L2342
	mov	r9, #0
.L2318:
	ldr	r10, .L2342+4
	mov	r8, #0
	mov	r5, r8
.L2309:
	ldrh	r1, [r10, #2324]
	uxth	r3, r8
	cmp	r1, r3
	bhi	.L2313
	cbz	r5, .L2295
	movs	r3, #1
	mov	r8, #0
	mov	r2, r6
	mov	r1, r5
	ldr	r0, [r7, #232]
	strb	r8, [r10, #2244]
	bl	FlashProgPages
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r10, #2244]
	mov	r10, #36
.L2315:
	uxth	r3, r8
	cmp	r5, r3
	bhi	.L2317
	add	r9, r9, fp
	ldr	r3, [sp, #12]
	uxth	r9, r9
	cmp	r3, r9
	bhi	.L2318
	mov	r8, #0
	mov	r9, #36
.L2319:
	uxth	r3, r8
	cmp	r5, r3
	bhi	.L2321
	ldr	r3, [sp, #8]
	cmp	r3, #63
	bls	.L2322
	ldr	r3, [sp, #4]
	cbz	r3, .L2295
.L2322:
	mov	r2, r5
	mov	r1, r6
	ldr	r0, [r7, #232]
	bl	FlashEraseBlocks
.L2295:
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2302:
	uxth	r1, r6
	ldr	r0, [r10, #232]
	movs	r3, #0
	mul	ip, r9, r1
	str	r3, [r0, ip]
	adds	r0, r5, r1
	ldrb	r0, [r0, #2350]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	bl	V2P_block
	ldr	r3, [sp, #4]
	mov	r8, r0
	cbz	r3, .L2298
	bl	IsBlkInVendorPart
	cbnz	r0, .L2299
.L2298:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2300
	ldr	r1, [r10, #232]
	mla	ip, r9, r7, r1
	lsl	r1, r8, #10
	str	r0, [ip, #8]
	str	r1, [ip, #4]
	ldr	r0, [fp, #3344]
	ldrh	r1, [r5, #2402]
	muls	r1, r7, r1
	add	r7, r7, #1
	it	mi
	addmi	r1, r1, #3
	uxth	r7, r7
	bic	r1, r1, #3
	add	r1, r1, r0
	str	r1, [ip, #12]
.L2299:
	adds	r6, r6, #1
	b	.L2297
.L2300:
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L2299
.L2306:
	mul	r2, r9, r8
	ldr	r1, [r10, #232]
	add	ip, r1, r2
	ldr	r2, [r1, r2]
	adds	r2, r2, #1
	bne	.L2305
	ldr	r0, [ip, #4]
	adds	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L2305:
	add	r8, r8, #1
	b	.L2304
.L2307:
	ldrh	r3, [r5, #2392]
	str	r3, [sp, #12]
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cbnz	r3, .L2325
	ldr	r3, [sp, #12]
	movs	r6, #1
	lsr	fp, r3, #2
	b	.L2308
.L2325:
	movs	r6, #1
	mov	fp, r6
	b	.L2308
.L2313:
	uxth	r3, r8
	movs	r2, #36
	ldr	r0, [r7, #232]
	mul	r1, r2, r3
	add	r3, r3, r10
	movs	r2, #0
	str	r2, [r0, r1]
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #2350]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp, #4]
	str	r0, [sp, #20]
	cbz	r3, .L2310
	bl	IsBlkInVendorPart
	cbnz	r0, .L2311
.L2310:
	ldr	r0, [sp, #20]
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L2311
	ldr	r1, [r7, #232]
	movs	r3, #36
	ldr	r0, [r7, #3336]
	mla	r1, r3, r5, r1
	ldr	r3, [sp, #20]
	add	r3, r9, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [r7, #3332]
	str	r3, [r1, #8]
	ldrh	r3, [r10, #2402]
	muls	r3, r5, r3
	add	r5, r5, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r5, r5
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L2311:
	add	r8, r8, #1
	b	.L2309
.L2317:
	mul	r3, r10, r8
	ldr	r2, [r7, #232]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L2316
	ldr	r0, [r1, #4]
	adds	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L2316:
	add	r8, r8, #1
	b	.L2315
.L2321:
	ldr	r3, [sp, #4]
	cbz	r3, .L2320
	mul	r3, r9, r8
	ldr	r2, [r7, #232]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L2320
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2320:
	add	r8, r8, #1
	b	.L2319
.L2324:
	movs	r4, #0
	b	.L2295
.L2343:
	.align	2
.L2342:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	1
	.global	FtlBbmTblFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r7, .L2358
	ldr	r6, [r7, #228]
	cmp	r6, #0
	bne	.L2346
	ldr	r4, .L2358+4
	mov	r1, r6
	ldr	r0, [r7, #3316]
	ldr	r5, .L2358+8
	ldr	r3, [r7, #3340]
	add	r8, r4, #2480
	ldrh	r2, [r4, #2400]
	str	r0, [r5, #1264]
	str	r3, [r5, #1268]
	bl	ftl_memset
.L2347:
	ldrh	r3, [r4, #2346]
	cmp	r6, r3
	blt	.L2348
	ldr	r6, [r5, #1268]
	movs	r2, #16
	movs	r1, #255
	ldr	fp, .L2358+24
	mov	r8, #0
	mov	r0, r6
	mov	r9, r8
	bl	ftl_memset
	movw	r3, #61649
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #2464]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #2456]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r4, #2460]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r4, #2462]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r4, #2320]
	strh	r3, [r6, #12]	@ movhi
.L2349:
	ldr	r3, [r7, #3316]
	mov	r10, #0
	ldrh	r2, [r4, #2458]
	ldrh	r1, [r4, #2456]
	str	r3, [r5, #1264]
	ldr	r3, [r7, #3340]
	str	r10, [r5, #1256]
	str	r3, [r5, #1268]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r5, #1260]
	ldrh	r3, [r4, #2460]
	str	r0, [sp]
	ldr	r0, .L2358+12
	bl	printk
	ldrh	r3, [r4, #2392]
	ldrh	r2, [r4, #2458]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L2350
	ldr	r3, [r4, #2464]
	ldrh	r2, [r4, #2456]
	ldr	r0, [r7, #232]
	adds	r3, r3, #1
	strh	r10, [r4, #2458]	@ movhi
	str	r3, [r4, #2464]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #2460]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r4, #2460]	@ movhi
	movs	r2, #1
	strh	r3, [r4, #2456]	@ movhi
	mov	r1, r2
	lsls	r3, r3, #10
	str	r3, [r5, #1260]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L2350:
	movs	r3, #1
	mov	r0, fp
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #2458]
	adds	r3, r3, #1
	strh	r3, [r4, #2458]	@ movhi
	ldr	r3, [r5, #1256]
	adds	r2, r3, #1
	bne	.L2351
	add	r8, r8, #1
	ldr	r1, [r5, #1260]
	uxth	r8, r8
	ldr	r0, .L2358+16
	bl	printk
	cmp	r8, #3
	bls	.L2349
	mov	r2, r8
	ldr	r1, [r5, #1260]
	ldr	r0, .L2358+20
	bl	printk
	movs	r3, #1
	str	r3, [r7, #228]
.L2346:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2348:
	ldrh	r2, [r7, #3404]
	ldr	r3, [r5, #1264]
	ldr	r1, [r8, #4]!
	mul	r0, r6, r2
	lsls	r2, r2, #2
	adds	r6, r6, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L2347
.L2354:
	mov	r9, #1
	b	.L2349
.L2351:
	add	r9, r9, #1
	cmp	r9, #1
	ble	.L2354
	cmp	r3, #256
	bne	.L2346
	b	.L2349
.L2359:
	.align	2
.L2358:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC126
	.word	.LC127
	.word	.LC128
	.word	.LANCHOR4+1256
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	1
	.global	allocate_data_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	ldr	r4, .L2409
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r4, #228]
	cmp	r3, #0
	bne	.L2361
.L2362:
	ldr	r3, .L2409+4
	ldr	r7, .L2409+8
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r3
	bne	.L2363
	ldrh	ip, [r4, #316]
	ldr	r3, [r4, #1132]
	lsr	r6, ip, #1
	mul	r0, r3, ip
	adds	r1, r6, #1
	add	r1, r1, r0, lsr #2
	ldr	r0, [r7, #2248]
	uxth	r1, r1
	cbz	r0, .L2364
	ldr	r0, [r4, #532]
	cmp	r0, #39
	bhi	.L2364
	cmp	r0, #2
	bls	.L2391
	tst	ip, #1
	beq	.L2387
	cmp	r3, #0
	beq	.L2391
.L2387:
	mov	r1, r6
	b	.L2364
.L2363:
	cmp	r2, #1
	bne	.L2391
	ldrh	r3, [r7, #2344]
	cmp	r3, #1
	beq	.L2391
	ldrb	r3, [r7, #152]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2391
	ldrh	r3, [r4, #316]
	ldr	r0, [r7, #2248]
	lsrs	r1, r3, #3
	cbz	r0, .L2364
	ldr	r0, [r4, #532]
	cmp	r0, #1
	bhi	.L2364
	rsb	r3, r3, r3, lsl #3
	ubfx	r1, r3, #3, #16
.L2364:
	cbz	r1, .L2365
	subs	r1, r1, #1
	uxth	r1, r1
.L2365:
	ldr	r0, .L2409+12
	bl	List_pop_index_node
	ldrh	r3, [r4, #316]
	uxth	r9, r0
	subs	r3, r3, #1
	strh	r3, [r4, #316]	@ movhi
	ldrh	r3, [r7, #2332]
	cmp	r3, r9
	bls	.L2362
	ldr	r3, [r4, #300]
	movw	fp, #65535
	mov	r6, r9
	ldrh	r8, [r3, r9, lsl #1]
	cmp	r8, #0
	bne	.L2362
	strh	r9, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2367
	ldr	r0, [r4, #232]
	movs	r2, #36
	ldrh	r1, [r7, #2324]
	add	r10, r5, #16
	mov	ip, r10
	mov	lr, r8
	mov	r3, r0
	str	r2, [sp, #8]
	mla	r1, r2, r1, r0
	str	r1, [sp, #4]
.L2368:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L2370
	ldr	r3, [r7, #2248]
	cbz	r3, .L2371
	ldr	r3, .L2409+16
	cmp	r5, r3
	bne	.L2371
	ldr	r3, [r4, #236]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #40
	itt	hi
	movhi	r3, #0
	strbhi	r3, [r4, #328]
.L2371:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #236]
	cmp	r3, #0
	bne	.L2372
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L2373
	ldrh	r1, [r7, #2382]
	add	r3, r3, r1
.L2407:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	movs	r1, #0
	ldr	r3, [r4, #516]
	mov	r0, r9
	adds	r3, r3, #1
	str	r3, [r4, #516]
	bl	ftl_set_blk_mode
.L2375:
	ldr	r3, [r4, #236]
	ldr	r2, [r4, #528]
	ldrh	r0, [r7, #2382]
	ldrh	r3, [r3, r6, lsl #1]
	ldrh	r1, [r7, #2332]
	cmp	r3, r2
	ldr	r2, [r4, #516]
	it	hi
	strhi	r3, [r4, #528]
	ldr	r3, [r4, #520]
	mla	r0, r2, r0, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #3360]
	ldr	r1, [r4, #232]
	str	r0, [r4, #524]
	ldr	r3, [r2, #16]
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #36
	mla	r2, r2, r8, r1
	adds	r3, r1, #4
	adds	r2, r2, #40
.L2377:
	adds	r3, r3, #36
	cmp	r2, r3
	bne	.L2378
	ldrb	r3, [r7, #152]	@ zero_extendqisi2
	cbz	r3, .L2379
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r8
	ldr	r0, [r4, #232]
	cmp	r3, #1
	ite	eq
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2379:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r8
	ldr	r0, [r4, #232]
	mov	fp, #0
	bl	FlashEraseBlocks
	mov	r3, fp
	movs	r1, #36
.L2381:
	uxth	r2, fp
	cmp	r8, r2
	bhi	.L2383
	cmp	r3, #0
	ble	.L2384
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2384:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2385
	ldr	r3, [r4, #300]
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L2362
.L2391:
	movs	r1, #0
	b	.L2365
.L2367:
	ldr	r3, [r4, #300]
	strh	fp, [r3, r9, lsl #1]	@ movhi
	b	.L2362
.L2370:
	str	lr, [r3, #8]
	str	lr, [r3, #12]
	ldrh	r1, [ip], #2
	cmp	r1, fp
	beq	.L2369
	ldr	r2, [sp, #8]
	lsls	r1, r1, #10
	mla	r2, r2, r8, r0
	add	r8, r8, #1
	uxth	r8, r8
	str	r1, [r2, #4]
.L2369:
	adds	r3, r3, #36
	b	.L2368
.L2373:
	movs	r3, #2
	b	.L2407
.L2372:
	ldrh	r3, [r2, r6, lsl #1]
	mov	r0, r9
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #520]
	adds	r3, r3, #1
	str	r3, [r4, #520]
	bl	ftl_set_blk_mode.part.9
	b	.L2375
.L2378:
	ldr	r1, [r3, #-36]
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r3, #-36]
	b	.L2377
.L2383:
	mul	r2, r1, fp
	ldr	r0, [r4, #232]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L2382
	ldr	r0, [ip, #4]
	adds	r3, r3, #1
	str	r1, [sp, #12]
	str	r2, [sp, #8]
	ubfx	r0, r0, #10, #16
	str	r3, [sp, #4]
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #4]
	strh	r2, [r10]	@ movhi
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	subs	r2, r2, #1
	strb	r2, [r5, #7]
.L2382:
	add	fp, fp, #1
	add	r10, r10, #2
	b	.L2381
.L2385:
	ldrh	r3, [r7, #2390]
	strh	r9, [r5]	@ movhi
	smulbb	r3, r3, r2
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #508]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #508]
	ldr	r2, [r4, #300]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
.L2361:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2410:
	.align	2
.L2409:
	.word	.LANCHOR2
	.word	.LANCHOR2+416
	.word	.LANCHOR0
	.word	.LANCHOR2+312
	.word	.LANCHOR2+320
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	1
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r9, r0
	ldr	r4, .L2423
	ldrh	r3, [r4, #1182]
	cbz	r3, .L2412
	ldr	r8, .L2423+8
	movs	r6, #0
	add	r10, r4, #1184
.L2413:
	ldrh	r2, [r8, #2324]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L2419
	bl	FtlGcReFreshBadBlk
.L2412:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2419:
	uxtah	r3, r8, r6
	mov	r1, r9
	movs	r7, #0
	ldrb	r0, [r3, #2350]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r2, .L2423+4
	mov	fp, r0
.L2414:
	ldrh	r3, [r4, #1182]
	uxth	r5, r7
	cmp	r3, r5
	bhi	.L2418
	adds	r6, r6, #1
	b	.L2413
.L2418:
	uxth	r3, r7
	add	r1, r4, r3, lsl #1
	ldrh	r1, [r1, #1184]
	cmp	r1, fp
	bne	.L2415
	mov	r1, fp
	mov	r0, r2
	str	r3, [sp, #4]
	str	r2, [sp]
	bl	printk
	mov	r0, fp
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [sp, #4]
	ldrh	r1, [r4, #1182]
	ldr	r2, [sp]
	add	r3, r10, r3, lsl #1
.L2416:
	cmp	r5, r1
	bcc	.L2417
	subs	r1, r1, #1
	strh	r1, [r4, #1182]	@ movhi
.L2415:
	adds	r7, r7, #1
	b	.L2414
.L2417:
	ldrh	r0, [r3, #2]!
	adds	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r3, #-2]	@ movhi
	b	.L2416
.L2424:
	.align	2
.L2423:
	.word	.LANCHOR2
	.word	.LC129
	.word	.LANCHOR0
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	1
	.global	update_vpc_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2433
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	mov	r5, r2
	ldr	r3, [r2, #300]
	ldrh	r3, [r3, r0, lsl #1]
	cbnz	r3, .L2426
	ldrh	r1, [r2, #556]
	cmp	r1, r0
	bne	.L2427
	movw	r3, #65535
	strh	r3, [r2, #556]	@ movhi
.L2428:
	mov	r1, r4
	ldr	r0, .L2433+4
	bl	List_remove_node
	ldrh	r3, [r5, #308]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #308]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	movs	r3, #1
	b	.L2425
.L2427:
	ldrh	r1, [r2, #320]
	cmp	r1, r0
	beq	.L2425
	ldrh	r1, [r2, #368]
	cmp	r1, r0
	beq	.L2425
	ldrh	r2, [r2, #416]
	cmp	r2, r0
	bne	.L2428
.L2425:
	mov	r0, r3
	pop	{r3, r4, r5, pc}
.L2426:
	bl	List_update_data_list
	movs	r3, #0
	b	.L2425
.L2434:
	.align	2
.L2433:
	.word	.LANCHOR2
	.word	.LANCHOR2+296
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	1
	.global	decrement_vpc_count
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	ldr	r5, .L2445
	beq	.L2436
	ldr	r3, [r5, #300]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L2437
	mov	r1, r0
	ldr	r0, .L2445+4
	bl	printk
	ldr	r3, [r5, #300]
	movs	r2, #32
	mov	r1, r4
	add	r0, r5, #312
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	test_node_in_list
	cbz	r0, .L2438
	mov	r1, r4
	add	r0, r5, #312
	bl	List_remove_node
	ldrh	r3, [r5, #316]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #316]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #300]
	mov	r1, r4
	ldr	r0, .L2445+8
	ldrh	r2, [r3, r4, lsl #1]
	bl	printk
.L2438:
	mov	r0, r4
	bl	FtlGcRefreshBlock
.L2441:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L2437:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L2436:
	ldrh	r0, [r5, #3460]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2440
	strh	r4, [r5, #3460]	@ movhi
	b	.L2441
.L2440:
	cmp	r4, r0
	beq	.L2441
	bl	update_vpc_list
	adds	r0, r0, #0
	strh	r4, [r5, #3460]	@ movhi
	it	ne
	movne	r0, #1
	pop	{r3, r4, r5, pc}
.L2446:
	.align	2
.L2445:
	.word	.LANCHOR2
	.word	.LC130
	.word	.LC131
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	1
	.global	FtlSlcSuperblockCheck
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	beq	.L2447
	ldrh	r3, [r0]
	movw	r6, #65535
	cmp	r3, r6
	beq	.L2447
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r5, .L2458
	ldr	r7, .L2458+4
	adds	r3, r3, #8
	ldrh	r3, [r0, r3, lsl #1]
.L2451:
	cmp	r3, r6
	beq	.L2453
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2454
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cbnz	r3, .L2454
	ldrh	r3, [r4, #2]
	add	r3, r7, r3, lsl #1
	ldrh	r3, [r3, #1220]
	cmp	r3, r6
	bne	.L2454
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cbnz	r2, .L2453
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L2453:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r5, #2324]
	adds	r3, r3, #1
	uxtb	r3, r3
	cmp	r2, r3
	strb	r3, [r4, #6]
	bne	.L2452
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
.L2452:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r3, [r4, r3, lsl #1]
	b	.L2451
.L2454:
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cbz	r3, .L2447
	cmp	r2, #1
	bne	.L2447
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #2392]
	cmp	r2, r3
	bcc	.L2447
	ldr	r3, .L2458+4
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldr	r2, [r3, #300]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	movs	r3, #0
	ldrh	r2, [r5, #2390]
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2447:
	pop	{r3, r4, r5, r6, r7, pc}
.L2459:
	.align	2
.L2458:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	1
	.global	get_new_active_ppa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movs	r3, #0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	strb	r3, [r0, #10]
	mov	r4, r0
	movw	r7, #65535
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r5, .L2475
	ldr	r6, .L2475+4
	adds	r3, r3, #8
	ldrh	r2, [r0, r3, lsl #1]
.L2461:
	cmp	r2, r7
	beq	.L2462
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r1, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2464
	ldrb	r0, [r5, #152]	@ zero_extendqisi2
	cbnz	r0, .L2464
	add	r0, r6, r1, lsl #1
	ldrh	r0, [r0, #1220]
	cmp	r0, r7
	bne	.L2464
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
.L2462:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r5, #2324]
	adds	r3, r3, #1
	uxtb	r3, r3
	cmp	r2, r3
	strb	r3, [r4, #6]
	bne	.L2463
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
.L2463:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r2, [r4, r3, lsl #1]
	b	.L2461
.L2464:
	ldr	r8, .L2475+4
	orr	r6, r1, r2, lsl #10
	movw	r7, #65535
	subs	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2465:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r1, [r5, #2324]
.L2467:
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
	beq	.L2467
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2460
	ldrb	r2, [r5, #152]	@ zero_extendqisi2
	ldrh	r3, [r4, #2]
	cbnz	r2, .L2469
	add	r3, r8, r3, lsl #1
	ldrh	r3, [r3, #1220]
	cmp	r3, r7
	bne	.L2460
	ldrh	r3, [r4, #4]
	cbz	r3, .L2460
	subs	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2465
.L2469:
	ldrh	r2, [r5, #2392]
	cmp	r3, r2
	bcc	.L2460
	ldr	r3, .L2475+4
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldr	r2, [r3, #300]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	movs	r3, #0
	ldrh	r2, [r5, #2390]
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2460:
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, pc}
.L2476:
	.align	2
.L2475:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	1
	.global	FtlVpcTblFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldr	r4, .L2500
	ldr	r3, [r4, #228]
	cmp	r3, #0
	bne	.L2479
	ldr	r5, .L2500+4
	movs	r1, #255
	ldr	r2, [r4, #3316]
	ldr	r7, [r4, #3340]
	ldr	r6, .L2500+8
	str	r2, [r5, #1264]
	ldrh	r2, [r4, #540]
	str	r7, [r5, #1268]
	str	r3, [r7, #12]
	strh	r2, [r7, #2]	@ movhi
	movw	r2, #61604
	strh	r2, [r7]	@ movhi
	ldr	r2, [r4, #548]
	str	r3, [r7, #8]
	ldr	r3, .L2500+12
	str	r2, [r7, #4]
	ldrh	r2, [r4, #322]
	str	r3, [r4, #244]
	ldr	r3, .L2500+16
	str	r3, [r4, #248]
	ldrh	r3, [r4, #546]
	strh	r3, [r4, #252]	@ movhi
	ldrh	r3, [r6, #2346]
	strb	r3, [r4, #254]
	ldrh	r3, [r4, #320]
	strh	r3, [r4, #258]	@ movhi
	ldrb	r3, [r4, #326]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #370]
	strh	r3, [r4, #260]	@ movhi
	ldrb	r3, [r4, #328]	@ zero_extendqisi2
	strb	r3, [r4, #255]
	ldrh	r3, [r4, #368]
	strh	r3, [r4, #262]	@ movhi
	ldrb	r3, [r4, #374]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #264]	@ movhi
	ldrb	r3, [r4, #376]	@ zero_extendqisi2
	strb	r3, [r4, #256]
	ldrh	r3, [r4, #416]
	strh	r3, [r4, #266]	@ movhi
	ldrh	r2, [r4, #418]
	ldrb	r3, [r4, #422]	@ zero_extendqisi2
	ldr	r0, [r5, #1264]
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r6, #2400]
	strh	r3, [r4, #268]	@ movhi
	ldrb	r3, [r4, #424]	@ zero_extendqisi2
	strb	r3, [r4, #257]
	ldr	r3, [r4, #516]
	str	r3, [r4, #276]
	ldr	r3, [r4, #508]
	str	r3, [r4, #284]
	ldr	r3, [r4, #512]
	str	r3, [r4, #280]
	ldrh	r3, [r4, #1174]
	strh	r3, [r4, #288]	@ movhi
	ldrh	r3, [r4, #1176]
	strh	r3, [r4, #290]	@ movhi
	bl	ftl_memset
	movs	r2, #48
	add	r1, r4, #244
	ldr	r0, [r5, #1264]
	bl	ftl_memcpy
	ldrh	r2, [r6, #2332]
	ldr	r0, [r5, #1264]
	ldr	r1, [r4, #300]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r6, #2332]
	ldr	r3, [r5, #1264]
	ldr	r1, [r6, #32]
	lsrs	r2, r0, #3
	lsls	r0, r0, #1
	adds	r0, r0, #51
	adds	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r0, r3
	bl	ftl_memcpy
	ldrh	r3, [r6, #2436]
	cbz	r3, .L2480
	ldrh	r0, [r6, #2332]
	ldrh	r2, [r6, #2428]
	ldr	r1, [r4, #3388]
	lsrs	r3, r0, #3
	lsls	r2, r2, #2
	add	r3, r3, r0, lsl #1
	ldr	r0, [r5, #1264]
	adds	r3, r3, #52
	ubfx	r3, r3, #2, #14
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
.L2480:
	ldr	fp, .L2500+24
	mov	r8, #0
	movw	r9, #65535
	mov	r10, r8
	movs	r0, #0
	bl	FtlUpdateVaildLpn
.L2481:
	ldr	r3, [r4, #3316]
	ldrh	r1, [r4, #542]
	ldrh	r2, [r4, #540]
	str	r3, [r5, #1264]
	ldr	r3, [r4, #3340]
	str	r3, [r5, #1268]
	orr	r3, r1, r2, lsl #10
	str	r3, [r5, #1260]
	ldrh	r3, [r6, #2392]
	subs	r3, r3, #1
	cmp	r1, r3
	blt	.L2482
	ldrh	r9, [r4, #544]
	strh	r10, [r4, #542]	@ movhi
	strh	r2, [r4, #544]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #508]
	strh	r0, [r4, #540]	@ movhi
	adds	r2, r3, #1
	str	r3, [r4, #548]
	str	r2, [r4, #508]
	lsls	r2, r0, #10
	str	r2, [r5, #1260]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L2482:
	ldrb	r3, [r6, #36]	@ zero_extendqisi2
	cbz	r3, .L2483
	ldrh	r1, [r6, #2400]
	ldr	r0, [r4, #3316]
	bl	js_hash
	str	r0, [r7, #12]
.L2483:
	movs	r3, #1
	mov	r0, fp
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #542]
	ldr	r2, [r5, #1256]
	adds	r3, r3, #1
	uxth	r3, r3
	adds	r1, r2, #1
	strh	r3, [r4, #542]	@ movhi
	bne	.L2484
	cmp	r3, #1
	add	r8, r8, #1
	it	eq
	ldrheq	r3, [r6, #2392]
	uxth	r8, r8
	itt	eq
	addeq	r3, r3, #-1
	strheq	r3, [r4, #542]	@ movhi
	cmp	r8, #3
	bls	.L2481
	mov	r2, r8
	ldr	r1, [r5, #1260]
	ldr	r0, .L2500+20
	bl	printk
	movs	r3, #1
	str	r3, [r4, #228]
.L2479:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2484:
	cmp	r3, #1
	beq	.L2481
	cmp	r2, #256
	beq	.L2481
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2479
	movs	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	b	.L2479
.L2501:
	.align	2
.L2500:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177379
	.word	.LC132
	.word	.LANCHOR4+1256
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	1
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, r0
	ldr	r5, .L2517
	.pad #40
	sub	sp, sp, #40
	ldr	r10, [r5, #228]
	cmp	r10, #0
	bne	.L2502
	ldr	r3, .L2517+4
	ldrb	r2, [r3, #152]	@ zero_extendqisi2
	mov	r8, r3
	cmp	r2, #0
	beq	.L2513
	ldrb	r3, [r0, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2513
	ldrh	r7, [r0, #4]
	mov	r10, r3
.L2504:
	mov	r3, #-1
	ldr	r6, [r5, #3340]
	str	r3, [sp, #20]
	mov	r9, #0
	ldr	r3, [r5, #3316]
	movw	r2, #61589
	str	r6, [sp, #16]
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r6, #8]
	mvn	r3, #1
	str	r3, [r6, #12]
	ldrh	r3, [r4]
	strh	r9, [r6]	@ movhi
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r5, #3316]
	str	r2, [r3]
	add	r2, r2, #304087040
	ldr	r3, [r5, #3316]
	add	r2, r2, #1269760
	addw	r2, r2, #1507
	str	r2, [r3, #4]
.L2505:
	adds	r7, r7, #-1
	bcc	.L2508
	ldrh	r3, [r4, #4]
	cbnz	r3, .L2506
.L2508:
	ldr	r2, [r5, #300]
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r8, #2390]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
.L2502:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2513:
	movs	r7, #12
	b	.L2504
.L2506:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L2508
	ldr	r3, [r5, #512]
	movs	r1, #1
	add	r0, sp, #4
	str	r3, [r6, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	mov	r2, r10
	it	eq
	moveq	r3, r9
	str	r3, [r5, #512]
	movs	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2505
.L2518:
	.align	2
.L2517:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	1
	.global	ftl_map_blk_gc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	ldr	r10, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	subs	r3, r3, #4
	cmp	r2, r3
	blt	.L2520
	uxth	r0, r0
	ldrh	r9, [r5, r0, lsl #1]
	cmp	r9, #0
	beq	.L2520
	ldr	r3, [r4, #32]
	cbnz	r3, .L2520
	movs	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L2531
	ldrh	r3, [r3, #2392]
	cmp	r2, r3
	bcc	.L2521
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2521:
	ldr	r5, .L2531+4
	movs	r6, #0
	ldr	fp, .L2531+16
.L2522:
	ldrh	r2, [r4, #6]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L2527
	movs	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L2520:
	ldr	r3, .L2531
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #2392]
	cmp	r2, r3
	bcc	.L2525
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2525
.L2527:
	uxth	r7, r6
	add	r2, r10, r7, lsl #2
	str	r2, [sp]
	ldr	r2, [r10, r7, lsl #2]
	cmp	r9, r2, lsr #10
	bne	.L2523
	ldr	r2, [fp, #3320]
	ldr	r8, [fp, #3340]
	ldr	r0, .L2531+8
	str	r2, [r5, #1264]
	str	r8, [r5, #1268]
	ldr	r2, [r10, r7, lsl #2]
	str	r3, [sp, #4]
	str	r2, [r5, #1260]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #1256]
	ldr	r3, [sp, #4]
	adds	r2, r2, #1
	bne	.L2524
.L2526:
	ldr	r2, [sp]
	movs	r3, #0
	ldr	r0, .L2531+12
	str	r3, [r2]
	ldrh	r2, [r8, #8]
	ldr	r1, [r5, #1260]
	bl	printk
	movs	r3, #1
	str	r3, [fp, #228]
.L2525:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2524:
	ldrh	r2, [r8, #8]
	cmp	r2, r3
	bne	.L2526
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2526
	ldr	r2, [r5, #1264]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L2523:
	adds	r6, r6, #1
	b	.L2522
.L2532:
	.align	2
.L2531:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR4+1256
	.word	.LC133
	.word	.LANCHOR2
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	1
	.global	Ftl_write_map_blk_to_last_page
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r6, .L2543
	ldr	r5, [r6, #228]
	cbnz	r5, .L2534
	ldrh	r3, [r0]
	movw	r2, #65535
	ldr	r7, [r0, #12]
	cmp	r3, r2
	bne	.L2535
	ldrh	r3, [r0, #8]
	adds	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r7]	@ movhi
	ldr	r3, [r4, #28]
	strh	r5, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
.L2534:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2535:
	ldrh	r10, [r7, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r8, .L2543+8
	ldr	r7, [r6, #3340]
	ldr	fp, [r0, #24]
	orr	r3, r3, r10, lsl #10
	ldr	r9, .L2543+12
	str	r7, [r8, #1268]
	str	r3, [r8, #1260]
	ldr	r3, [r6, #3316]
	str	r3, [r8, #1264]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	movw	r3, #64245
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r10, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	ldrh	r2, [r9, #2392]
	ldr	r0, [r6, #3316]
	lsls	r2, r2, #3
	bl	ftl_memset
	mov	r3, r5
	mov	r2, r5
.L2536:
	ldrh	r0, [r4, #6]
	uxth	r1, r3
	cmp	r0, r1
	bhi	.L2538
	ldrb	r3, [r9, #36]	@ zero_extendqisi2
	cbz	r3, .L2539
	ldrh	r1, [r9, #2400]
	ldr	r0, [r8, #1264]
	bl	js_hash
	str	r0, [r7, #12]
.L2539:
	movs	r2, #1
	movs	r3, #0
	mov	r1, r2
	ldr	r0, .L2543+4
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L2534
.L2538:
	uxth	r0, r3
	ldr	r1, [fp, r0, lsl #2]
	cmp	r10, r1, lsr #10
	bne	.L2537
	ldr	r1, [r6, #3316]
	adds	r2, r2, #1
	uxth	r2, r2
	str	r0, [r1, r2, lsl #3]
	ldr	r1, [r6, #3316]
	ldr	r0, [fp, r0, lsl #2]
	add	r1, r1, r2, lsl #3
	str	r0, [r1, #4]
.L2537:
	adds	r3, r3, #1
	b	.L2536
.L2544:
	.align	2
.L2543:
	.word	.LANCHOR2
	.word	.LANCHOR4+1256
	.word	.LANCHOR4
	.word	.LANCHOR0
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	1
	.global	FtlMapWritePage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldr	r8, .L2569+20
	mov	fp, r1
	movs	r6, #0
	ldr	r7, .L2569
	mov	r9, r8
	str	r2, [sp]
.L2546:
	ldr	r3, [r8, #492]
	adds	r3, r3, #1
	str	r3, [r8, #492]
	ldrh	r3, [r7, #2392]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L2547
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2548
.L2547:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2548:
	ldr	r1, [r9, #228]
	cmp	r1, #0
	bne	.L2564
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldr	r5, .L2569+4
	ldr	r0, [r9, #3340]
	ldrh	r10, [r3, r2, lsl #1]
	movs	r2, #16
	ldrh	r3, [r4, #2]
	str	r0, [r5, #1268]
	orr	r3, r3, r10, lsl #10
	str	r3, [r5, #1260]
	ldr	r3, [sp]
	str	r3, [r5, #1264]
	bl	ftl_memset
	ldr	r3, [r5, #1268]
	ldr	r2, [r4, #28]
	strh	fp, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	str	r3, [sp, #4]
	strh	r10, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrb	r2, [r7, #36]	@ zero_extendqisi2
	cbz	r2, .L2550
	ldrh	r1, [r7, #2400]
	ldr	r0, [r5, #1264]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2550:
	movs	r3, #1
	ldr	r0, .L2569+8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #1256]
	adds	r1, r2, #1
	bne	.L2551
	ldr	r1, [r5, #1260]
	adds	r6, r6, #1
	ldr	r0, .L2569+12
	uxth	r6, r6
	bl	printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r7, #2392]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L2546
	ldr	r3, .L2569+4
	mov	r2, r6
	ldr	r0, .L2569+16
	ldr	r1, [r3, #1260]
	bl	printk
	movs	r3, #1
	str	r3, [r9, #228]
.L2564:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2551:
	cbz	r2, .L2554
	cmp	r3, #1
	strh	r10, [r4, #40]	@ movhi
	bne	.L2555
.L2556:
	movs	r3, #0
	str	r3, [r4, #36]
	b	.L2546
.L2555:
	cmp	r2, #256
.L2568:
	beq	.L2556
	ldr	r3, [r4, #36]
	cmp	r3, #0
	bne	.L2556
	ldr	r3, .L2569+4
	ldr	r2, [r3, #1260]
	ldr	r3, [r4, #24]
	str	r2, [r3, fp, lsl #2]
	b	.L2564
.L2554:
	cmp	r3, #1
	b	.L2568
.L2570:
	.align	2
.L2569:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR4+1256
	.word	.LC134
	.word	.LC135
	.word	.LANCHOR2
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	1
	.global	flush_l2p_region
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	flush_l2p_region, %function
flush_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r4, #12
	ldr	r5, .L2572
	muls	r4, r0, r4
	ldr	r3, [r5, #464]
	add	r0, r5, #3408
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #464]
	movs	r0, #0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L2573:
	.align	2
.L2572:
	.word	.LANCHOR2
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	1
	.global	FtlMapTblRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r3, [r0, #24]
	mov	r4, r0
	movs	r1, #0
	movs	r7, #0
	ldr	fp, .L2615+8
	str	r3, [sp, #4]
	ldr	r3, [r0, #12]
	ldr	r5, .L2615
	ldr	r6, .L2615+4
	str	r3, [sp, #8]
	ldr	r3, [r0, #16]
	str	r3, [sp, #20]
	ldrh	r3, [r0, #6]
	str	r3, [sp, #12]
	ldrh	r3, [r0, #8]
	ldr	r0, [sp, #4]
	str	r3, [sp, #16]
	ldr	r3, [sp, #12]
	lsls	r2, r3, #2
	bl	ftl_memset
	ldr	r2, [fp, #3316]
	ldr	r8, [fp, #3340]
	str	r7, [r4, #32]
	str	r2, [r5, #1264]
	movw	r2, #65535
	str	r8, [r5, #1268]
	strh	r2, [r4]	@ movhi
	strh	r2, [r4, #2]	@ movhi
	movs	r2, #1
	str	r7, [r4, #28]
	str	r2, [r4, #36]
.L2575:
	ldr	r3, [sp, #16]
	sxth	r10, r7
	cmp	r10, r3
	bge	.L2594
	ldr	r3, [sp, #16]
	lsl	r9, r10, #1
	subs	r2, r3, #1
	cmp	r10, r2
	bne	.L2576
	ldr	r3, [sp, #8]
	movs	r1, #1
	ldr	fp, .L2615+12
	ldrh	r0, [r3, r10, lsl #1]
	add	r9, r9, r3
	bl	FtlGetLastWrittenPage
	ldr	r2, [sp, #20]
	sxth	r3, r0
	adds	r0, r0, #1
	strh	r7, [r4]	@ movhi
	strh	r0, [r4, #2]	@ movhi
	sub	r7, fp, #1256
	ldr	r2, [r2, r10, lsl #2]
	mov	r10, #0
	adds	r3, r3, #1
	str	r3, [sp, #8]
	str	r2, [r4, #28]
.L2577:
	ldr	r3, [sp, #8]
	sxth	r2, r10
	cmp	r2, r3
	blt	.L2580
.L2594:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, #2392]
	cmp	r2, r3
	bne	.L2582
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2582:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2580:
	ldrh	r1, [r9]
	mov	r0, fp
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #1260]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrb	r2, [r6, #36]	@ zero_extendqisi2
	cbz	r2, .L2578
	ldr	r2, [r5, #1268]
	ldr	r2, [r2, #12]
	str	r2, [sp, #16]
	cbz	r2, .L2578
	ldrh	r1, [r6, #2400]
	ldr	r0, [r5, #1264]
	bl	js_hash
	ldr	r2, [sp, #16]
	cmp	r2, r0
	itt	ne
	movne	r2, #-1
	strne	r2, [r5, #1256]
.L2578:
	ldr	r2, [r7, #1256]
	adds	r2, r2, #1
	beq	.L2579
	ldrh	r2, [r8, #8]
	ldr	r3, [sp, #12]
	cmp	r3, r2
	bls	.L2579
	ldrh	r1, [r4, #4]
	ldrh	r0, [r8]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r7, #1260]
	ldreq	r3, [sp, #4]
	streq	r1, [r3, r2, lsl #2]
.L2579:
	add	r10, r10, #1
	b	.L2577
.L2576:
	ldr	r3, .L2615+8
	ldr	r0, .L2615+12
	ldr	r2, [r3, #3316]
	ldr	r3, [sp, #8]
	str	r2, [r5, #1264]
	add	r3, r3, r9
	ldrh	r2, [r6, #2392]
	str	r3, [sp, #24]
	ldr	r3, [sp, #8]
	subs	r2, r2, #1
	ldrh	r1, [r3, r10, lsl #1]
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #1260]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #1256]
	adds	r2, r2, #1
	beq	.L2596
	ldrh	r1, [r8]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2596
	ldrh	r1, [r8, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L2584
.L2596:
	ldr	r9, .L2615
	mov	r10, #0
.L2585:
	ldrh	r1, [r6, #2392]
	sxth	r2, r10
	cmp	r2, r1
	bge	.L2592
	ldr	r3, [sp, #24]
	ldr	r0, .L2615+12
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	str	r2, [r9, #1260]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrb	r2, [r6, #36]	@ zero_extendqisi2
	cbz	r2, .L2589
	ldr	r2, [r9, #1268]
	ldr	r2, [r2, #12]
	str	r2, [sp, #28]
	cbz	r2, .L2589
	ldrh	r1, [r6, #2400]
	ldr	r0, [r9, #1264]
	bl	js_hash
	ldr	r2, [sp, #28]
	cmp	r2, r0
	itt	ne
	movne	r2, #-1
	strne	r2, [r9, #1256]
.L2589:
	ldr	r2, [r9, #1256]
	adds	r2, r2, #1
	beq	.L2590
	ldrh	r2, [r8, #8]
	ldr	r3, [sp, #12]
	cmp	r3, r2
	bls	.L2590
	ldrh	r1, [r4, #4]
	ldrh	r0, [r8]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r9, #1260]
	ldreq	r3, [sp, #4]
	streq	r1, [r3, r2, lsl #2]
.L2590:
	add	r10, r10, #1
	b	.L2585
.L2584:
	movs	r0, #0
	mov	lr, #4
.L2586:
	ldrh	r1, [r6, #2392]
	sxth	r2, r0
	subs	r1, r1, #1
	cmp	r2, r1
	blt	.L2588
.L2592:
	adds	r7, r7, #1
	b	.L2575
.L2588:
	ldr	ip, [fp, #3316]
	ldr	r3, [sp, #12]
	ldr	r1, [ip, r2, lsl #3]
	uxth	r9, r1
	cmp	r3, r9
	bls	.L2587
	add	r2, lr, r2, lsl #3
	ldr	r3, [sp, #4]
	ldr	r2, [ip, r2]
	str	r2, [r3, r9, lsl #2]
.L2587:
	adds	r0, r0, #1
	b	.L2586
.L2616:
	.align	2
.L2615:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4+1256
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	1
	.global	FtlLoadVonderInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	ldr	r3, .L2618
	ldr	r0, .L2618+4
	ldrh	r2, [r3, #2412]
	add	r0, r0, #1296
	strh	r2, [r0, #10]	@ movhi
	movw	r2, #61574
	strh	r2, [r0, #4]	@ movhi
	ldrh	r2, [r3, #2438]
	strh	r2, [r0, #8]	@ movhi
	ldrh	r2, [r3, #2414]
	ldr	r3, [r3, #2440]
	strh	r2, [r0, #6]	@ movhi
	str	r3, [r0, #12]
	ldr	r3, .L2618+8
	ldr	r2, [r3, #3380]
	str	r2, [r0, #16]
	ldr	r2, [r3, #3376]
	ldr	r3, [r3, #3384]
	str	r2, [r0, #20]
	str	r3, [r0, #24]
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2619:
	.align	2
.L2618:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	1
	.global	FtlLoadMapInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	FtlL2PDataInit
	ldr	r0, .L2621
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L2622:
	.align	2
.L2621:
	.word	.LANCHOR2+3408
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	1
	.global	FtlVendorPartWrite
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2635
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	mov	r5, r1
	adds	r1, r0, r1
	.pad #60
	sub	sp, sp, #60
	mov	r7, r0
	ldrh	r2, [r3, #2386]
	str	r3, [sp]
	cmp	r1, r2
	bhi	.L2631
	ldrh	r6, [r3, #2398]
	mov	r9, #0
	ldr	r8, .L2635+8
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L2625:
	cbnz	r5, .L2630
.L2623:
	mov	r0, r9
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2630:
	ldr	r3, [r8, #3384]
	mov	r0, r7
	ldr	r2, [r3, fp]
	ldr	r3, [sp]
	str	r2, [sp, #12]
	ldrh	r3, [r3, #2396]
	mov	r1, r3
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	str	r1, [sp, #4]
	subs	r4, r3, r1
	uxth	r4, r4
	cmp	r5, r4
	it	cc
	uxthcc	r4, r5
	cbz	r2, .L2627
	cmp	r4, r3
	beq	.L2627
	ldr	r3, [r8, #3324]
	add	r0, sp, #20
	str	r2, [sp, #24]
	movs	r2, #1
	mov	r1, r2
	str	r3, [sp, #28]
	movs	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
.L2628:
	lsls	r3, r4, #9
	ldr	r0, [r8, #3324]
	mov	r1, r10
	mov	r2, r3
	str	r3, [sp, #8]
	ldr	r3, [sp, #4]
	subs	r5, r5, r4
	add	r7, r7, r4
	add	fp, fp, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r2, [r8, #3324]
	ldr	r0, .L2635+4
	adds	r6, r6, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	add	r10, r10, r3
	b	.L2625
.L2627:
	ldr	r3, [sp]
	movs	r1, #0
	ldr	r0, [r8, #3324]
	ldrh	r2, [r3, #2400]
	bl	ftl_memset
	b	.L2628
.L2631:
	mov	r9, #-1
	b	.L2623
.L2636:
	.align	2
.L2635:
	.word	.LANCHOR0
	.word	.LANCHOR4+1296
	.word	.LANCHOR2
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	1
	.global	Ftl_save_ext_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L2639
	ldr	r3, .L2639+4
	ldr	r1, [r2, #604]
	cmp	r1, r3
	bne	.L2637
	ldr	r3, .L2639+8
	movs	r1, #1
	movs	r0, #0
	str	r3, [r2, #608]
	ldr	r3, [r2, #500]
	str	r3, [r2, #692]
	ldr	r3, [r2, #504]
	str	r3, [r2, #696]
	ldr	r3, [r2, #496]
	str	r3, [r2, #612]
	ldr	r3, [r2, #484]
	str	r3, [r2, #616]
	ldr	r3, [r2, #476]
	str	r3, [r2, #620]
	ldr	r3, [r2, #492]
	str	r3, [r2, #624]
	ldr	r3, [r2, #520]
	str	r3, [r2, #632]
	ldr	r3, [r2, #240]
	add	r2, r2, #604
	str	r3, [r2, #32]
	ldr	r3, [r2, #-124]
	str	r3, [r2, #36]
	ldr	r3, [r2, #-116]
	str	r3, [r2, #40]
	ldr	r3, [r2, #-76]
	str	r3, [r2, #44]
	ldr	r3, [r2, #-72]
	str	r3, [r2, #48]
	ldr	r3, [r2, #512]
	str	r3, [r2, #60]
	ldr	r3, [r2, #2672]
	str	r3, [r2, #64]
	b	FtlVendorPartWrite
.L2637:
	bx	lr
.L2640:
	.align	2
.L2639:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177379
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	1
	.global	FtlEctTblFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2648
	push	{r4, lr}
	.save {r4, lr}
	ldr	r2, [r3, #2248]
	ldr	r3, .L2648+4
	cmp	r2, #0
	beq	.L2646
	ldr	r2, [r3, #532]
	cmp	r2, #39
	ite	hi
	movhi	r2, #32
	movls	r2, #4
.L2642:
	ldr	r4, .L2648+8
	ldrh	r1, [r4, #1340]
	cmp	r1, #31
	ittt	ls
	addls	r1, r1, #1
	movls	r2, #1
	strhls	r1, [r4, #1340]	@ movhi
	cbnz	r0, .L2644
	ldr	r1, [r3, #3360]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2645
.L2644:
	ldr	r2, [r3, #3360]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2648+12
	str	r1, [r2]
	ldrh	r1, [r3, #3352]
	ldr	r2, [r3, #3360]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2645:
	movs	r0, #0
	pop	{r4, pc}
.L2646:
	movs	r2, #32
	b	.L2642
.L2649:
	.align	2
.L2648:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	1112818501
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	1
	.global	FtlVendorPartRead
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2660
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r2
	mov	r6, r1
	adds	r1, r0, r1
	.pad #60
	sub	sp, sp, #60
	mov	r7, r0
	ldrh	r2, [r3, #2386]
	str	r3, [sp, #8]
	cmp	r1, r2
	bhi	.L2659
	ldrh	r5, [r3, #2398]
	mov	r8, #0
	ldr	r10, .L2660+16
	lsr	r5, r0, r5
	lsl	fp, r5, #2
.L2652:
	cbnz	r6, .L2658
.L2650:
	mov	r0, r8
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2658:
	ldr	r3, [r10, #3384]
	mov	r0, r7
	ldr	r3, [r3, fp]
	str	r3, [sp, #12]
	ldr	r3, [sp, #8]
	ldrh	r4, [r3, #2396]
	mov	r1, r4
	bl	__aeabi_uidivmod
	subs	r4, r4, r1
	ldr	r3, [sp, #12]
	uxth	r4, r4
	str	r1, [sp, #4]
	cmp	r6, r4
	it	cc
	uxthcc	r4, r6
	lsls	r2, r4, #9
	str	r2, [sp, #12]
	cbz	r3, .L2654
	ldr	r2, [r10, #3324]
	add	r0, sp, #20
	str	r3, [sp, #24]
	str	r3, [sp, #12]
	str	r2, [sp, #28]
	movs	r2, #0
	str	r2, [sp, #32]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r3, .L2660+4
	ldr	r2, [sp, #20]
	adds	r2, r2, #1
	ldr	r2, [r3, #1256]
	it	eq
	moveq	r8, #-1
	ldr	r3, [sp, #12]
	cmp	r2, #256
	bne	.L2656
	mov	r2, r3
	mov	r1, r5
	ldr	r0, .L2660+8
	bl	printk
	ldr	r2, [r10, #3324]
	mov	r1, r5
	ldr	r0, .L2660+12
	bl	FtlMapWritePage
.L2656:
	ldr	r1, [r10, #3324]
	lsls	r2, r4, #9
	ldr	r3, [sp, #4]
	mov	r0, r9
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L2657:
	adds	r5, r5, #1
	subs	r6, r6, r4
	add	r7, r7, r4
	add	r9, r9, r4, lsl #9
	add	fp, fp, #4
	b	.L2652
.L2654:
	lsls	r2, r4, #9
	mov	r1, r3
	mov	r0, r9
	bl	ftl_memset
	b	.L2657
.L2659:
	mov	r8, #-1
	b	.L2650
.L2661:
	.align	2
.L2660:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC136
	.word	.LANCHOR4+1296
	.word	.LANCHOR2
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	1
	.global	FtlLoadEctTbl
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movs	r0, #64
	ldr	r4, .L2664
	ldr	r2, [r4, #3360]
	ldrh	r1, [r4, #3352]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3360]
	ldr	r2, [r3]
	ldr	r3, .L2664+4
	cmp	r2, r3
	beq	.L2663
	ldr	r1, .L2664+8
	ldr	r0, .L2664+12
	bl	printk
	ldrh	r2, [r4, #3352]
	movs	r1, #0
	ldr	r0, [r4, #3360]
	lsls	r2, r2, #9
	bl	ftl_memset
.L2663:
	movs	r0, #0
	pop	{r4, pc}
.L2665:
	.align	2
.L2664:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC137
	.word	.LC77
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	1
	.global	Ftl_load_ext_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r1, #1
	ldr	r4, .L2671
	movs	r0, #0
	ldr	r5, .L2671+4
	add	r2, r4, #604
	bl	FtlVendorPartRead
	ldr	r3, [r4, #604]
	cmp	r3, r5
	beq	.L2667
	mov	r2, #512
	movs	r1, #0
	add	r0, r4, #604
	bl	ftl_memset
	str	r5, [r4, #604]
.L2667:
	ldr	r3, [r4, #604]
	cmp	r3, r5
	bne	.L2668
	ldr	r3, [r4, #692]
	str	r3, [r4, #500]
	ldr	r3, [r4, #696]
	str	r3, [r4, #504]
	ldr	r3, [r4, #612]
	str	r3, [r4, #496]
	ldr	r3, [r4, #616]
	str	r3, [r4, #484]
	ldr	r3, [r4, #620]
	str	r3, [r4, #476]
	ldr	r3, [r4, #624]
	str	r3, [r4, #492]
	ldr	r3, [r4, #632]
	str	r3, [r4, #520]
	ldr	r3, [r4, #636]
	str	r3, [r4, #240]
	ldr	r3, [r4, #640]
	str	r3, [r4, #480]
	ldr	r3, [r4, #644]
	str	r3, [r4, #488]
	ldr	r3, [r4, #648]
	str	r3, [r4, #528]
	ldr	r3, [r4, #652]
	str	r3, [r4, #532]
	ldr	r3, [r4, #664]
	str	r3, [r4, #1116]
.L2668:
	ldr	r1, [r4, #672]
	movs	r3, #0
	ldr	r2, .L2671+8
	str	r3, [r4, #3276]
	ldr	r5, .L2671+12
	cmp	r1, r2
	bne	.L2669
	ldrb	r2, [r5, #152]	@ zero_extendqisi2
	cbz	r2, .L2670
	str	r3, [r4, #672]
	bl	Ftl_save_ext_data
.L2669:
	ldrh	r2, [r5, #2382]
	ldr	r3, [r4, #520]
	ldr	r0, [r4, #516]
	ldrh	r1, [r5, #2332]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #524]
	pop	{r3, r4, r5, pc}
.L2670:
	movs	r3, #1
	ldr	r1, .L2671+16
	ldr	r0, .L2671+20
	str	r3, [r5, #2248]
	bl	printk
	b	.L2669
.L2672:
	.align	2
.L2671:
	.word	.LANCHOR2
	.word	1179929683
	.word	305432421
	.word	.LANCHOR0
	.word	.LC138
	.word	.LC77
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	1
	.global	FtlMapBlkWriteDumpData
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r3, [r0, #36]
	cbz	r3, .L2673
	ldr	r2, .L2679
	movs	r3, #0
	str	r3, [r0, #36]
	ldrh	r5, [r0, #6]
	ldr	r3, [r2, #228]
	ldr	r1, [r0, #24]
	cbnz	r3, .L2673
	ldr	r4, .L2679+4
	subs	r5, r5, #1
	ldr	r0, [r2, #3320]
	uxth	r5, r5
	ldr	r2, [r2, #3340]
	str	r0, [r4, #1264]
	str	r2, [r4, #1268]
	ldr	r2, [r1, r5, lsl #2]
	str	r2, [r4, #1260]
	cbz	r2, .L2677
	movs	r2, #1
	add	r0, r4, #1256
	mov	r1, r2
	bl	FlashReadPages
.L2678:
	ldr	r2, [r4, #1264]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L2677:
	ldr	r3, .L2679+8
	movs	r1, #255
	ldrh	r2, [r3, #2400]
	bl	ftl_memset
	b	.L2678
.L2673:
	pop	{r4, r5, r6, pc}
.L2680:
	.align	2
.L2679:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	1
	.global	FlashReadFacBbtData
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldr	r2, .L2693
	.pad #40
	sub	sp, sp, #40
	mov	r5, r0
	mov	r9, r1
	ldr	r7, .L2693+4
	ldrh	r3, [r2, #138]
	ldrh	r2, [r2, #136]
	smulbb	r3, r3, r2
	ldr	r2, [r7, #1192]
	uxth	r3, r3
	str	r2, [sp, #12]
	ldr	r2, [r7, #1224]
	subs	r6, r3, #1
	mul	r10, r1, r3
	uxth	r6, r6
	sub	r4, r3, #16
	str	r2, [sp, #16]
.L2682:
	cmp	r6, r4
	bgt	.L2688
	mov	r0, #-1
	b	.L2681
.L2688:
	add	r3, r6, r10
	movs	r2, #1
	lsls	r3, r3, #10
	mov	r1, r2
	add	r0, sp, #4
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L2683
	ldr	r3, [r7, #1224]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2683
	cbz	r5, .L2689
	cmp	r9, #0
	bne	.L2685
	mov	r1, r9
	movs	r4, #1
.L2686:
	ldr	r0, [r7, #1212]
	uxth	r3, r1
	adds	r1, r1, #1
	cmp	r3, r0
	bcc	.L2687
.L2685:
	mov	r2, r8
	ldr	r1, [r7, #1192]
	mov	r0, r5
	bl	ftl_memcpy
	movs	r3, #4
	ldr	r0, .L2693+8
	mov	r2, r3
	mov	r1, r5
	bl	rknand_print_hex
	movs	r0, #0
.L2681:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2687:
	ldr	r0, [r7, #1192]
	lsrs	r6, r3, #5
	and	r3, r3, #31
	lsl	r3, r4, r3
	ldr	r2, [r0, r6, lsl #2]
	orrs	r3, r3, r2
	str	r3, [r0, r6, lsl #2]
	b	.L2686
.L2683:
	subs	r6, r6, #1
	uxth	r6, r6
	b	.L2682
.L2689:
	mov	r0, r5
	b	.L2681
.L2694:
	.align	2
.L2693:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC139
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	1
	.global	FlashGetBadBlockList
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2705
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r6, .L2705+4
	ldr	r3, [r3, #48]
	ldr	r0, [r6, #1220]
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	smulbb	r4, r4, r3
	uxth	r4, r4
	adds	r2, r4, #7
	asrs	r2, r2, #3
	bl	FlashReadFacBbtData
	adds	r0, r0, #1
	mov	ip, r6
	bne	.L2696
.L2700:
	movs	r3, #0
.L2697:
	movw	r2, #65535
	movs	r0, #0
	strh	r2, [r5, r3, lsl #1]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L2696:
	movs	r2, #0
	lsr	lr, r4, #4
	mov	r3, r2
	subs	r4, r4, #1
	mov	r8, #1
.L2698:
	uxth	r1, r2
	cmp	r1, r4
	bge	.L2697
	ldr	r6, [ip, #1220]
	lsrs	r7, r1, #5
	and	r0, r1, #31
	lsl	r0, r8, r0
	adds	r2, r2, #1
	ldr	r6, [r6, r7, lsl #2]
	tst	r0, r6
	ittt	ne
	addne	r0, r3, #1
	strhne	r1, [r5, r3, lsl #1]	@ movhi
	uxthne	r3, r0
	cmp	r3, lr
	bcc	.L2698
	b	.L2700
.L2706:
	.align	2
.L2705:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	1
	.global	FtlMakeBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r6, .L2728
	ldr	r7, [r6, #228]
	cmp	r7, #0
	bne	.L2708
	ldr	r9, .L2728+8
	ldr	r8, .L2728+12
	bl	FtlBbtMemInit
	subw	r10, r9, #2484
	sub	fp, r9, #18
	mov	r4, r10
	bl	FtlLoadFactoryBbt
.L2709:
	ldrh	r3, [r10, #2346]
	cmp	r7, r3
	bcc	.L2715
	movs	r5, #0
.L2716:
	ldrh	r3, [r4, #2404]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L2717
	ldrh	r5, [r4, #2468]
	movw	r7, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L2718:
	ldrh	r3, [r4, #2468]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L2722
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2719
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2720
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L2719:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L2718
.L2715:
	ldr	r3, [r6, #3340]
	movw	r2, #65535
	ldr	r0, [r6, #3316]
	str	r3, [sp]
	str	r3, [r8, #1268]
	ldrh	r3, [fp, #2]!
	str	r0, [r8, #1264]
	cmp	r3, r2
	beq	.L2710
	ldrh	r5, [r10, #2388]
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L2728+4
	mla	r5, r7, r5, r3
	lsls	r3, r5, #10
	str	r3, [r8, #1260]
	bl	FlashReadPages
	ldrh	r2, [r10, #2388]
	ldr	r1, [r8, #1264]
	ldr	r0, [r9]
	adds	r2, r2, #7
	asrs	r2, r2, #3
	bl	ftl_memcpy
.L2711:
	uxth	r0, r5
	adds	r7, r7, #1
	add	r9, r9, #4
	bl	FtlBbmMapBadBlock
	b	.L2709
.L2710:
	mov	r1, r7
	bl	FlashGetBadBlockList
	ldr	r1, [r9]
	ldr	r0, [r8, #1264]
	bl	FtlBbt2Bitmap
	ldrh	r3, [r10, #2388]
.L2727:
	subs	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #4]
.L2712:
	ldr	r3, [sp, #4]
	ldrh	r0, [r4, #2388]
	smlabb	r0, r0, r7, r3
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2713
	ldrh	r3, [sp, #4]
	movs	r2, #16
	movs	r1, #0
	ldr	r0, [r6, #3340]
	strh	r3, [fp]	@ movhi
	bl	ftl_memset
	ldr	r2, [sp]
	movw	r3, #61664
	strh	r3, [r2]	@ movhi
	movs	r3, #0
	str	r3, [r2, #4]
	ldrh	r3, [fp]
	ldrh	r5, [r4, #2388]
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [fp]
	ldrh	r2, [r6, #3404]
	ldr	r1, [r9]
	ldr	r0, [r8, #1264]
	mla	r5, r7, r5, r3
	lsls	r2, r2, #2
	lsls	r3, r5, #10
	str	r3, [r8, #1260]
	bl	ftl_memcpy
	movs	r2, #1
	ldr	r0, .L2728+4
	mov	r1, r2
	bl	FlashEraseBlocks
	movs	r3, #1
	ldr	r0, .L2728+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r8, #1256]
	adds	r3, r3, #1
	bne	.L2711
	uxth	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L2712
.L2713:
	ldr	r3, [sp, #4]
	b	.L2727
.L2717:
	bl	FtlBbmMapBadBlock
	b	.L2716
.L2720:
	ldrh	r3, [r4, #2456]
	cmp	r3, r7
	bne	.L2721
	strh	r5, [r4, #2456]	@ movhi
	b	.L2719
.L2721:
	strh	r5, [r4, #2460]	@ movhi
.L2722:
	ldrh	r3, [r4, #2456]
	movs	r5, #0
	ldr	r0, [r6, #232]
	movs	r2, #2
	str	r5, [r4, #2464]
	movs	r1, #1
	strh	r5, [r4, #2458]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #2460]
	lsls	r3, r3, #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r4, #2456]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, #2460]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #2464]
	ldrh	r2, [r4, #2460]
	strh	r5, [r4, #2458]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #2464]
	ldrh	r3, [r4, #2456]
	strh	r2, [r4, #2456]	@ movhi
	strh	r3, [r4, #2460]	@ movhi
	bl	FtlBbmTblFlush
.L2708:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2729:
	.align	2
.L2728:
	.word	.LANCHOR2
	.word	.LANCHOR4+1256
	.word	.LANCHOR0+2484
	.word	.LANCHOR4
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	1
	.global	log2phys
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2745
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	str	r2, [sp, #8]
	mov	fp, r1
	mov	r8, r3
	ldr	r2, [r3, #2452]
	str	r0, [sp]
	cmp	r0, r2
	bcs	.L2731
	ldrh	r3, [r3, #2398]
	movs	r5, #12
	ldr	r4, .L2745+4
	adds	r3, r3, #7
	str	r3, [sp, #4]
	movs	r3, #0
	ldr	r2, [sp, #4]
	ldr	r7, [r4, #464]
	lsr	r6, r0, r2
	ldrh	r2, [r8, #2430]
	uxth	r10, r6
.L2732:
	uxth	r9, r3
	cmp	r9, r2
	bcc	.L2737
	bl	select_l2p_ram_region
	muls	r5, r0, r5
	movw	r2, #65535
	mov	r9, r0
	ldrh	r1, [r7, r5]
	adds	r3, r7, r5
	cmp	r1, r2
	beq	.L2738
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2738
	bl	flush_l2p_region
.L2738:
	ldr	r3, [r4, #3388]
	uxth	r6, r6
	ldr	r3, [r3, r6, lsl #2]
	cmp	r3, #0
	bne	.L2739
	ldr	r2, [r4, #464]
	movs	r1, #255
	str	r3, [sp, #12]
	adds	r0, r2, r5
	ldrh	r2, [r8, #2400]
	ldr	r0, [r0, #8]
	bl	ftl_memset
	ldr	r2, [r4, #464]
	ldr	r3, [sp, #12]
	strh	r10, [r2, r5]	@ movhi
	ldr	r2, [r4, #464]
	add	r5, r5, r2
	str	r3, [r5, #4]
	b	.L2734
.L2731:
	ldr	r3, [sp, #8]
	mov	r0, #-1
	cbnz	r3, .L2730
	str	r0, [fp]
.L2730:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2737:
	adds	r3, r3, #1
	mla	r1, r5, r3, r7
	ldrh	r1, [r1, #-12]
	cmp	r1, r10
	bne	.L2732
.L2734:
	ldr	r2, [sp, #4]
	movs	r3, #1
	ldr	r0, [sp, #8]
	ldr	r1, .L2745+4
	lsls	r3, r3, r2
	ldr	r2, [sp]
	subs	r3, r3, #1
	ands	r3, r3, r2
	movs	r2, #12
	uxth	r3, r3
	cbnz	r0, .L2735
	ldr	r0, [r1, #464]
	mla	r2, r2, r9, r0
	ldr	r2, [r2, #8]
	ldr	r3, [r2, r3, lsl #2]
	str	r3, [fp]
.L2736:
	ldr	r2, [r1, #464]
	movs	r3, #12
	mla	r9, r3, r9, r2
	ldr	r3, [r9, #4]
	adds	r2, r3, #1
	beq	.L2743
	adds	r3, r3, #1
	str	r3, [r9, #4]
.L2743:
	movs	r0, #0
	b	.L2730
.L2735:
	mul	r2, r2, r9
	ldr	r0, [r4, #464]
	ldr	r5, [fp]
	add	r0, r0, r2
	ldr	r0, [r0, #8]
	str	r5, [r0, r3, lsl #2]
	ldr	r3, [r4, #464]
	strh	r10, [r4, #468]	@ movhi
	add	r2, r2, r3
	ldr	r3, [r2, #4]
	orr	r3, r3, #-2147483648
	str	r3, [r2, #4]
	b	.L2736
.L2739:
	ldr	r2, [r4, #464]
	ldr	r7, .L2745+8
	str	r3, [sp, #12]
	add	r2, r2, r5
	ldr	r2, [r2, #8]
	add	r0, r7, #1256
	str	r3, [r7, #1260]
	str	r2, [r7, #1264]
	ldr	r2, [r4, #3340]
	str	r2, [r7, #1268]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r7, #1268]
	ldr	r3, [sp, #12]
	ldrh	r2, [r2, #8]
	cmp	r2, r10
	beq	.L2740
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L2745+12
	bl	printk
	movs	r3, #4
	ldr	r1, [r7, #1268]
	mov	r2, r3
	ldr	r0, .L2745+16
	bl	rknand_print_hex
	ldrh	r3, [r8, #2428]
	movs	r2, #4
	ldr	r1, [r4, #3388]
	ldr	r0, .L2745+20
	bl	rknand_print_hex
	movs	r3, #1
	str	r3, [r4, #228]
.L2741:
	ldr	r3, .L2745+4
	movs	r1, #0
	ldr	r3, [r3, #464]
	adds	r2, r3, r5
	str	r1, [r2, #4]
	strh	r10, [r3, r5]	@ movhi
	b	.L2734
.L2740:
	ldr	r2, [r7, #1256]
	cmp	r2, #256
	bne	.L2741
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L2745+24
	bl	printk
	ldr	r3, [r4, #464]
	mov	r1, r6
	ldr	r0, .L2745+28
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L2741
.L2746:
	.align	2
.L2745:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC140
	.word	.LC101
	.word	.LC141
	.word	.LC142
	.word	.LANCHOR2+3408
	.fnend
	.size	log2phys, .-log2phys
	.align	1
	.global	FtlWriteDumpData
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r4, .L2765
	ldr	r3, [r4, #228]
	cmp	r3, #0
	bne	.L2747
	ldrh	r2, [r4, #324]
	cmp	r2, #0
	beq	.L2749
	ldrb	r3, [r4, #328]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2749
	ldr	r7, .L2765+4
	ldrb	r1, [r4, #327]	@ zero_extendqisi2
	ldrh	r3, [r7, #2390]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L2749
	ldrb	r9, [r4, #330]	@ zero_extendqisi2
	cmp	r9, #0
	bne	.L2747
	ldr	r6, [r7, #2452]
	mov	r2, r9
	mov	r1, sp
	ldrh	r8, [r7, #2324]
	subs	r6, r6, #1
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp]
	ldr	r5, [r4, #3340]
	ldr	r0, [r4, #3316]
	str	r3, [sp, #8]
	adds	r3, r3, #1
	str	r6, [sp, #20]
	str	r0, [sp, #12]
	str	r5, [sp, #16]
	str	r9, [r5, #4]
	beq	.L2751
	mov	r2, r9
	movs	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.L2752:
	ldr	r9, .L2765+8
	movs	r7, #0
	lsl	r8, r8, #2
	mov	r10, r7
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L2753:
	cmp	r8, r7
	bne	.L2757
.L2754:
	movs	r3, #1
.L2764:
	strb	r3, [r4, #330]
.L2747:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2751:
	ldrh	r2, [r7, #2400]
	movs	r1, #255
	bl	ftl_memset
	b	.L2752
.L2757:
	ldrh	r3, [r4, #324]
	cmp	r3, #0
	beq	.L2754
	ldr	r3, [sp, #8]
	mov	r0, r9
	str	r6, [r5, #8]
	adds	r7, r7, #1
	str	r3, [r5, #12]
	ldrh	r3, [r4, #320]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r4, #512]
	movs	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r10
	str	r3, [r4, #512]
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4, #320]
	bl	decrement_vpc_count
	b	.L2753
.L2749:
	movs	r3, #0
	b	.L2764
.L2766:
	.align	2
.L2765:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+320
	.fnend
	.size	FtlWriteDumpData, .-FtlWriteDumpData
	.align	1
	.global	l2p_flush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r4, #0
	ldr	r5, .L2771
	movs	r7, #12
	ldr	r6, .L2771+4
	bl	FtlWriteDumpData
.L2768:
	ldrh	r3, [r5, #2430]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L2770
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L2770:
	ldr	r2, [r6, #464]
	uxth	r3, r4
	mla	r3, r7, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2769
	bl	flush_l2p_region
.L2769:
	adds	r4, r4, #1
	b	.L2768
.L2772:
	.align	2
.L2771:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	1
	.global	allocate_new_data_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r4, .L2799
	ldr	r3, [r4, #228]
	cbnz	r3, .L2774
	ldrh	r5, [r0]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2775
	ldr	r3, [r4, #300]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L2776
	bl	INSERT_DATA_LIST
.L2775:
	ldr	r2, .L2799+4
	movs	r3, #0
	strb	r3, [r6, #8]
	cmp	r6, r2
	beq	.L2777
	ldr	r3, .L2799+8
	ldrh	r1, [r3, #2344]
	cmp	r1, #1
	beq	.L2777
	ldrb	r0, [r3, #152]	@ zero_extendqisi2
	cbz	r0, .L2778
.L2777:
	movs	r3, #1
	strb	r3, [r6, #8]
.L2779:
	ldrh	r0, [r4, #3460]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2784
	cmp	r5, r0
	bne	.L2785
	ldr	r3, [r4, #300]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L2786
.L2785:
	bl	update_vpc_list
.L2786:
	movw	r3, #65535
	strh	r3, [r4, #3460]	@ movhi
.L2784:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2774:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L2776:
	bl	INSERT_FREE_LIST
	b	.L2775
.L2778:
	subs	r2, r2, #48
	cmp	r6, r2
	bne	.L2779
	cmp	r1, #3
	beq	.L2781
	ldr	r2, [r4, #1116]
	cmp	r2, #1
	bne	.L2782
.L2781:
	movs	r2, #1
	strb	r2, [r4, #328]
.L2782:
	ldr	r3, [r3, #2248]
	cmp	r3, #0
	beq	.L2779
	ldr	r3, [r4, #532]
	cmp	r3, #39
	itt	ls
	movls	r3, #1
	strbls	r3, [r4, #328]
	b	.L2779
.L2800:
	.align	2
.L2799:
	.word	.LANCHOR2
	.word	.LANCHOR2+368
	.word	.LANCHOR0
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	1
	.global	FtlCheckVpc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r4, #0
	ldr	r5, .L2821
	ldr	r6, .L2821+4
	ldr	r1, .L2821+8
	mov	r8, r5
	ldr	r0, .L2821+12
	bl	printk
	mov	r2, #8192
	movs	r1, #0
	ldr	r0, .L2821+4
	bl	memset
.L2802:
	ldr	r3, [r5, #2452]
	cmp	r4, r3
	bcc	.L2804
	ldr	r5, .L2821+16
	movs	r4, #0
	ldr	r9, .L2821+4
	mov	r7, r4
	ldr	r10, .L2821+28
.L2805:
	ldrh	r2, [r8, #2332]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L2807
	ldr	r4, [r5, #312]
	cbz	r4, .L2808
	ldr	r3, [r5, #292]
	movs	r6, #0
	ldrh	r8, [r5, #316]
	mov	fp, #6
	ldr	r9, .L2821+4
	subs	r4, r4, r3
	ldr	r3, .L2821+20
	asrs	r4, r4, #1
	ldr	r10, .L2821+32
	muls	r4, r3, r4
	uxth	r4, r4
.L2809:
	uxth	r3, r6
	cmp	r8, r3
	bls	.L2808
	ldr	r3, [r5, #300]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L2810
	movs	r7, #1
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	printk
.L2810:
	mul	r4, fp, r4
	ldr	r3, [r5, #292]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2809
.L2808:
	mov	r1, r7
	ldr	r0, .L2821+24
	bl	printk
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2804:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L2803
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r6, r0, lsl #1]	@ movhi
.L2803:
	adds	r4, r4, #1
	b	.L2802
.L2807:
	ldr	r3, [r5, #300]
	uxth	r6, r4
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r9, r6, lsl #1]
	cmp	r2, r3
	beq	.L2806
	mov	r1, r6
	mov	r0, r10
	bl	printk
	ldr	r3, [r5, #300]
	movw	r2, #65535
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, r2
	beq	.L2806
	ldrh	r2, [r9, r6, lsl #1]
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L2806:
	adds	r4, r4, #1
	b	.L2805
.L2822:
	.align	2
.L2821:
	.word	.LANCHOR0
	.word	check_valid_page_count_table
	.word	.LANCHOR3+191
	.word	.LC110
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LC145
	.word	.LC143
	.word	.LC144
	.fnend
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	1
	.global	Ftlscanalldata
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movs	r5, #0
	ldr	r4, .L2837
	.pad #32
	sub	sp, sp, #32
	movs	r1, #0
	ldr	r7, .L2837+4
	add	r8, r4, #1256
	ldr	r0, .L2837+8
	bl	printk
.L2824:
	ldr	r3, [r7, #2452]
	cmp	r5, r3
	bcc	.L2830
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2830:
	movs	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L2825
	ldr	r2, [sp, #28]
	mov	r1, r5
	ldr	r0, .L2837+12
	bl	printk
.L2825:
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L2827
	str	r3, [r4, #1260]
	movs	r1, #1
	ldr	r3, .L2837+16
	mov	r0, r8
	str	r5, [r4, #1272]
	ldr	r2, [r3, #3316]
	ldr	r6, [r3, #3340]
	str	r2, [r4, #1264]
	movs	r2, #0
	str	r6, [r4, #1268]
	str	r2, [r4, #1256]
	bl	FlashReadPages
	ldr	r3, [r4, #1256]
	cmp	r3, #256
	beq	.L2828
	adds	r3, r3, #1
	beq	.L2828
	ldr	r3, [r6, #8]
	cmp	r5, r3
	beq	.L2827
.L2828:
	ldr	r2, [r4, #1264]
	ldr	r3, [r4, #1268]
	ldr	r0, .L2837+20
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	mov	r1, r5
	ldr	r2, [r2]
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r2, [r4, #1260]
	ldr	r3, [r3]
	bl	printk
.L2827:
	adds	r5, r5, #1
	b	.L2824
.L2838:
	.align	2
.L2837:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LC146
	.word	.LC147
	.word	.LANCHOR2
	.word	.LC148
	.fnend
	.size	Ftlscanalldata, .-Ftlscanalldata
	.align	1
	.global	FtlReUsePrevPpa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r6, r0
	ldr	r5, .L2848
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r5, #300]
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L2840
	ldr	r4, [r5, #312]
	cbz	r4, .L2841
	ldr	r1, [r5, #292]
	mov	ip, #6
	ldr	r2, .L2848+4
	movw	lr, #65535
	ldrh	r7, [r5, #316]
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L2842:
	uxth	r2, r3
	cmp	r7, r2
	bls	.L2841
	cmp	r4, r0
	bne	.L2843
	mov	r1, r4
	ldr	r0, .L2848+8
	bl	List_remove_node
	ldrh	r3, [r5, #316]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #316]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #300]
	ldrh	r3, [r2, r4, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r4, lsl #1]	@ movhi
.L2841:
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L2843:
	mul	r4, ip, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, lr
	bne	.L2842
	b	.L2841
.L2840:
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
	b	.L2841
.L2849:
	.align	2
.L2848:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+312
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	1
	.global	FtlRecoverySuperblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r2, #65535
	ldrh	r3, [r0]
	.pad #60
	sub	sp, sp, #60
	mov	r4, r0
	cmp	r3, r2
	beq	.L2999
	ldrh	r3, [r0, #2]
	ldr	r6, .L3009
	str	r3, [sp, #8]
	ldr	r1, [sp, #8]
	ldrh	r3, [r6, #2390]
	cmp	r3, r1
	mov	r3, #0
	bne	.L2853
	strh	r3, [r0, #4]	@ movhi
.L3007:
	strb	r3, [r4, #6]
.L2999:
	movs	r0, #0
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2853:
	ldrh	r0, [r0, #16]
.L2854:
	cmp	r0, r2
	add	r3, r3, #1
	beq	.L2855
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	cmp	r1, #1
	str	r3, [sp, #20]
	bne	.L2856
	bl	FtlGetLastWrittenPage
	adds	r7, r0, #1
	mov	r5, r0
	beq	.L2857
	ldrb	r3, [r6, #152]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2931
	add	r3, r6, r0, lsl #1
	ldrh	r7, [r3, #156]
.L2858:
	ldr	r3, .L3009+4
	movs	r2, #0
	ldrh	ip, [r6, #2324]
	movw	r9, #65535
	mov	r10, #36
	mov	fp, r2
	ldr	r0, [r3, #3304]
	ldr	r8, [r3, #1148]
	ldrh	r3, [r6, #2402]
	mov	r6, r2
	str	r3, [sp, #4]
	add	r3, r4, #16
	mov	lr, r3
	str	r3, [sp, #16]
.L2859:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L2862
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2932
	ldr	r3, .L3009
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	adds	r3, r3, #0
	it	ne
	movne	r3, #1
.L3000:
	str	r3, [sp, #24]
	mov	r1, r6
	ldr	r2, [sp, #24]
	mov	r9, #0
	bl	FlashReadPages
	ldr	r3, .L3009+4
	ldr	r2, [r3, #512]
	mov	r8, r3
	mov	fp, r3
	add	r10, r2, #-1
	movw	r2, #65535
	str	r2, [sp, #12]
.L2864:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L2869
	bne	.L2867
	ldr	r3, [r8, #3304]
	adds	r5, r5, #1
	uxth	fp, r5
	ldr	r0, [r3, #4]
.L3001:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	str	r0, [sp, #4]
	ldr	r3, .L3009
	cmp	r2, #1
	bne	.L2871
	ldrb	r1, [r3, #152]	@ zero_extendqisi2
	cbnz	r1, .L2871
	add	r5, r3, fp, lsl #1
	ldrh	fp, [r5, #156]
.L2871:
	ldrh	r3, [r3, #2390]
	cmp	r3, fp
	itttt	eq
	moveq	r3, #0
	strheq	fp, [r4, #2]	@ movhi
	strbeq	r3, [r4, #6]
	strheq	r3, [r4, #4]	@ movhi
	ldrh	r3, [sp, #20]
	str	r3, [sp, #28]
	ldr	r3, [sp, #8]
	cmp	fp, r3
	bne	.L2873
	ldr	r3, [sp, #4]
	ldr	r1, [sp, #28]
	cmp	r3, r1
	bne	.L2873
	mov	r2, r3
.L3008:
	mov	r1, fp
	mov	r0, r4
	bl	ftl_sb_update_avl_pages
	b	.L2999
.L2855:
	uxth	r1, r3
	adds	r1, r1, #8
	ldrh	r0, [r4, r1, lsl #1]
	b	.L2854
.L2856:
	movs	r1, #0
	bl	FtlGetLastWrittenPage
	mov	r5, r0
	adds	r0, r0, #1
	beq	.L2857
.L2931:
	mov	r7, r5
	b	.L2858
.L2857:
	movs	r3, #0
	strh	r3, [r4, #2]	@ movhi
	b	.L3007
.L2862:
	ldrh	r3, [lr], #2
	cmp	r3, r9
	beq	.L2860
	mla	r1, r10, r6, r0
	orr	r3, r7, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #4]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r8
	str	fp, [r1, #8]
	str	r3, [r1, #12]
.L2860:
	adds	r2, r2, #1
	b	.L2859
.L2932:
	movs	r3, #0
	b	.L3000
.L2869:
	movs	r3, #36
	ldr	r1, [fp, #3304]
	mul	r3, r3, r9
	adds	r2, r1, r3
	ldr	r3, [r1, r3]
	cbnz	r3, .L2865
	ldr	r2, [r2, #12]
	ldr	r3, [r2, #4]
	adds	r1, r3, #1
	beq	.L2866
	ldr	r1, [fp, #512]
	mov	r0, r3
	bl	ftl_cmp_data_ver
	cbz	r0, .L2866
	adds	r3, r3, #1
	str	r3, [fp, #512]
.L2866:
	ldr	r3, [r2]
	adds	r3, r3, #1
	bne	.L2868
.L2867:
	ldr	r3, [r8, #3304]
	uxth	r9, r9
	movs	r2, #36
	uxth	fp, r5
	mla	r9, r2, r9, r3
	ldr	r0, [r9, #4]
	b	.L3001
.L2865:
	ldr	r1, [r2, #4]
	ldr	r0, .L3009+8
	bl	printk
	uxth	r3, r7
	ldr	r2, .L3009+12
	str	r3, [sp, #12]
	ldrh	r3, [r4]
	strh	r3, [r2, #1342]	@ movhi
.L2868:
	add	r9, r9, #1
	b	.L2864
.L2873:
	ldr	r1, [sp, #12]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L2874
	cmp	r2, #0
	bne	.L2875
.L2874:
	uxth	r3, r7
	uxth	r7, r7
	str	r3, [sp, #20]
	mov	r9, #-1
	ldr	r3, [r8, #3472]
	adds	r3, r3, #1
	it	eq
	streq	r10, [r8, #3472]
	ldr	r3, [r8, #3472]
	mov	r8, r9
	str	r3, [sp, #32]
	ldr	r3, [sp, #8]
	adds	r3, r3, #7
	cmp	r7, r3
	ldr	r7, .L3009+4
	itett	gt
	ldrgt	r3, [sp, #20]
	ldrle	r5, [sp, #8]
	subgt	r5, r3, #7
	uxthgt	r5, r5
.L2878:
	ldr	r3, [sp, #20]
	cmp	r5, r3
	bhi	.L2891
	ldr	r3, .L3009
	movw	lr, #65535
	ldr	r0, [r7, #3304]
	mov	ip, #36
	ldrh	r3, [r3, #2324]
	str	r3, [sp, #36]
	ldr	r3, [sp, #16]
	str	r3, [sp, #12]
	movs	r3, #0
	mov	r6, r3
	b	.L2892
.L2880:
	ldr	r1, [sp, #12]
	ldrh	r2, [r1], #2
	cmp	r2, lr
	str	r1, [sp, #12]
	beq	.L2879
	mla	r1, ip, r6, r0
	adds	r6, r6, #1
	orr	r2, r5, r2, lsl #10
	uxth	r6, r6
	str	r2, [r1, #4]
.L2879:
	adds	r3, r3, #1
.L2892:
	ldr	r1, [sp, #36]
	uxth	r2, r3
	cmp	r2, r1
	bcc	.L2880
	mov	r1, r6
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	ldr	r3, .L3009
	movs	r2, #36
	add	ip, r7, r5, lsl #1
	movw	r1, #65535
	ldrb	r0, [r3, #152]	@ zero_extendqisi2
	ldr	r3, [r7, #3304]
	mla	r6, r2, r6, r3
.L2881:
	cmp	r6, r3
	bne	.L2890
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L2878
.L2890:
	ldr	r2, [r3]
	cbnz	r2, .L2882
	ldr	r2, [r3, #12]
	ldrh	lr, [r2]
	cmp	lr, r1
	beq	.L2883
	ldr	r2, [r2, #4]
	cmp	r2, #-1
	beq	.L2883
	cmp	r9, #-1
	ldr	r8, [r7, #3472]
	str	r2, [r7, #3472]
	bne	.L2883
	ldrh	r2, [ip, #1220]
	cmp	r2, r1
	bne	.L2884
	cbz	r0, .L2883
.L2884:
	cmp	r10, r8
	it	ne
	movne	r9, r8
.L2883:
	adds	r3, r3, #36
	b	.L2881
.L3010:
	.align	2
.L3009:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC149
	.word	.LANCHOR4
.L2882:
	ldr	r3, .L3011
	ldrh	r2, [r4]
	strh	r2, [r3, #1342]	@ movhi
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cbnz	r3, .L2875
	ldr	r3, .L3011+4
	movw	r2, #65535
	add	r5, r3, r5, lsl #1
	ldrh	r1, [r5, #1220]
	cmp	r1, r2
	bne	.L2886
	cmp	r9, #-1
	beq	.L2887
	str	r9, [r3, #3472]
.L2875:
	ldr	r6, [sp, #8]
	movs	r2, #1
	ldr	r5, .L3011+4
	ldr	r3, .L3011
	strh	r2, [r3, #1344]	@ movhi
.L2893:
	ldr	r3, .L3011+8
	movw	r8, #65535
	ldr	r0, [r5, #3304]
	mov	r9, #36
	movs	r2, #0
	ldrh	ip, [r3, #2324]
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	str	r2, [sp, #12]
	mov	r1, r3
	ldr	r3, [sp, #16]
	str	r3, [sp, #20]
.L2894:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L2897
	ldr	r2, [sp, #24]
	ldr	r1, [sp, #12]
	bl	FlashReadPages
	movs	r3, #0
.L3006:
	str	r3, [sp, #20]
	ldr	r2, [sp, #12]
	ldrh	r3, [sp, #20]
	cmp	r2, r3
	bhi	.L2925
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	adds	r6, r6, #1
	uxth	r6, r6
	ldr	r2, .L3011+8
	cmp	r3, #1
	bne	.L2926
	ldrb	r3, [r2, #152]	@ zero_extendqisi2
	cbz	r3, .L2926
	ldrh	r3, [r2, #2392]
	cmp	r3, r6
	bne	.L2926
	cmp	fp, r6
	beq	.L2902
.L2926:
	ldrh	r3, [r2, #2390]
	cmp	r3, r6
	bne	.L2893
	ldrh	r2, [r2, #2324]
	movw	r0, #65535
	movs	r3, #0
	strh	r6, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
.L2927:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L2999
	ldr	r1, [sp, #16]
	ldrh	r5, [r1], #2
	cmp	r5, r0
	str	r1, [sp, #16]
	add	r1, r3, #1
	bne	.L3007
	mov	r3, r1
	b	.L2927
.L2887:
	ldr	r2, [sp, #32]
	cmp	r10, r2
	beq	.L2888
.L3003:
	str	r2, [r3, #3472]
	b	.L2875
.L2888:
	ldr	r2, [r3, #3472]
.L3002:
	subs	r2, r2, #1
	b	.L3003
.L2886:
	cmp	r8, r10
	beq	.L2889
	cmp	r8, #-1
	beq	.L2875
	str	r8, [r3, #3472]
	b	.L2875
.L2889:
	ldr	r2, [r3, #3472]
	cmp	r10, r2
	bne	.L3002
	b	.L2875
.L2891:
	mov	r3, #-1
	str	r3, [r7, #3472]
	b	.L2875
.L2897:
	ldr	r7, [sp, #20]
	ldrh	r3, [r7], #2
	cmp	r3, r8
	str	r7, [sp, #20]
	beq	.L2895
	ldr	r7, [sp, #12]
	orr	r3, r6, r3, lsl #10
	mla	r7, r9, r7, r0
	str	r3, [r7, #4]
	ldrb	lr, [r4, #8]	@ zero_extendqisi2
	cmp	lr, #1
	bne	.L2896
	cbz	r1, .L2896
	orr	r3, r3, #-2147483648
	str	r3, [r7, #4]
.L2896:
	ldr	r3, [sp, #12]
	adds	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #12]
.L2895:
	adds	r2, r2, #1
	b	.L2894
.L2925:
	ldr	r3, [sp, #20]
	mov	r8, #36
	mul	r8, r8, r3
	ldr	r3, [r5, #3304]
	str	r3, [sp, #32]
	add	r9, r3, r8
	ldr	r7, [r9, #4]
	ubfx	r0, r7, #10, #16
	str	r7, [sp, #52]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcc	.L2899
	ldr	r3, [sp, #32]
	bne	.L2900
	ldr	r2, [sp, #28]
	cmp	r2, r0
	bhi	.L2899
.L2900:
	cmp	r6, fp
	bne	.L2901
	ldr	r2, [sp, #4]
	cmp	r2, r0
	beq	.L2902
.L2901:
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	beq	.L2903
	ldr	r3, [r9, #12]
	movw	r2, #61589
	ldrh	r1, [r3]
	cmp	r1, r2
	beq	.L2904
	ldrh	r0, [r4]
.L3004:
	bl	decrement_vpc_count
	b	.L2899
.L2904:
	ldr	r10, [r3, #4]
	cmp	r10, #-1
	beq	.L2905
	ldr	r1, [r5, #512]
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cbz	r0, .L2905
	add	r2, r10, #1
	str	r2, [r5, #512]
.L2905:
	ldr	r7, [r3, #8]
	add	r1, sp, #48
	ldr	r3, [r3, #12]
	movs	r2, #0
	mov	r0, r7
	str	r3, [sp, #44]
	bl	log2phys
	ldr	r1, [r5, #3472]
	adds	r0, r1, #1
	beq	.L2906
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2906
	ldr	r3, [sp, #44]
	adds	r2, r3, #1
	beq	.L2907
	ldr	r0, [r5, #3304]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r8
	str	r3, [r0, #4]
	ldr	r9, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r5, #3304]
	ldr	r1, [r2, r8]
	add	r3, r2, r8
	adds	r1, r1, #1
	bne	.L2908
.L2909:
	mov	r3, #-1
	str	r3, [sp, #44]
.L2916:
	ldr	r0, [sp, #44]
	adds	r1, r0, #1
	beq	.L2899
.L2930:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #300]
	mov	r1, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L3004
	ldr	r0, .L3011+12
	bl	printk
	b	.L2899
.L2907:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	bne	.L2899
	movs	r2, #1
	add	r1, sp, #44
	mov	r0, r7
	bl	log2phys
.L2899:
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	b	.L3006
.L2908:
	ldr	r1, [r9, #8]
	cmp	r7, r1
	bne	.L2909
	ldr	r1, [r9, #4]
	ldr	r0, [r5, #3472]
	str	r1, [sp, #32]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2909
	ldr	r1, [sp, #48]
	ldr	r0, [sp, #52]
	cmp	r1, r0
	bne	.L2911
.L2914:
	ldr	r1, [sp, #44]
	mov	r0, r7
	bl	FtlReUsePrevPpa
	b	.L2909
.L2911:
	ldr	r0, [sp, #44]
	cmp	r1, r0
	beq	.L2909
	adds	r0, r1, #1
	beq	.L2912
	str	r1, [r3, #4]
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	ldr	r9, [r3, #12]
	bl	FlashReadPages
.L2913:
	ldr	r3, [r5, #3304]
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	beq	.L2914
	ldr	r3, [r9, #4]
	ldr	r0, [r5, #3472]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2914
	mov	r1, r3
	ldr	r0, [sp, #32]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2909
	b	.L2914
.L2912:
	str	r1, [r2, r8]
	b	.L2913
.L3012:
	.align	2
.L3011:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC150
.L2906:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	beq	.L2916
	ldr	r3, [sp, #44]
	adds	r0, r3, #1
	beq	.L2918
	ldr	r2, .L3013
	ubfx	r3, r3, #10, #21
	ldr	r2, [r2, #2340]
	cmp	r3, r2
	bcs	.L2899
.L2918:
	movs	r2, #1
	add	r1, sp, #52
	mov	r0, r7
	bl	log2phys
	ldr	r9, [sp, #48]
	cmp	r9, #-1
	beq	.L2916
	ldr	r3, [sp, #44]
	cmp	r9, r3
	beq	.L2920
	ubfx	r0, r9, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #320]
	cmp	r3, r0
	beq	.L2921
	ldrh	r3, [r5, #368]
	cmp	r3, r0
	beq	.L2921
	ldrh	r3, [r5, #416]
	cmp	r3, r0
	bne	.L2916
.L2921:
	ldr	r0, [r5, #3304]
	movs	r2, #0
	movs	r1, #1
	str	r9, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r5, #3304]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L2916
	ldr	r1, [r8, #4]
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L2916
	movs	r2, #1
	add	r1, sp, #48
	mov	r0, r7
	bl	log2phys
	b	.L2916
.L2903:
	ldrh	r3, [r4]
	mov	r1, r7
	ldr	r2, .L3013+4
	ldr	r0, .L3013+8
	strh	r3, [r2, #1342]	@ movhi
	mov	r2, r10
	bl	printk
	ldr	r3, .L3013+4
	ldr	r3, [r3, #1348]
	cmp	r3, #31
	bhi	.L2923
	ldr	r2, .L3013+4
	ldr	r1, [sp, #52]
	add	r2, r2, r3, lsl #2
	adds	r3, r3, #1
	str	r1, [r2, #1352]
	ldr	r2, .L3013+4
	str	r3, [r2, #1348]
.L2923:
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	ldr	r3, [r5, #3472]
	adds	r2, r3, #1
	bne	.L2924
.L3005:
	str	r10, [r5, #3472]
	b	.L2899
.L2924:
	cmp	r10, r3
	bcs	.L2899
	b	.L3005
.L2902:
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	strh	fp, [r4, #2]	@ movhi
	ldr	r2, [sp, #4]
	strb	r3, [r4, #6]
	b	.L3008
.L2920:
	mov	r0, r9
	b	.L2930
.L3014:
	.align	2
.L3013:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC151
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	1
	.global	FtlVpcCheckAndModify
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	movs	r5, #0
	ldr	r7, .L3026
	ldr	r1, .L3026+4
	ldr	r0, .L3026+8
	bl	printk
	ldrh	r2, [r7, #2334]
	movs	r1, #0
	ldr	r4, .L3026+12
	lsls	r2, r2, #1
	ldr	r0, [r4, #3364]
	bl	ftl_memset
.L3016:
	ldr	r3, [r7, #2452]
	cmp	r5, r3
	bcc	.L3018
	ldr	r10, .L3026+16
	mov	r8, #0
	movw	r9, #65535
.L3019:
	ldrh	r3, [r7, #2332]
	uxth	r6, r8
	cmp	r3, r6
	bhi	.L3022
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3018:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L3017
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3364]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L3017:
	adds	r5, r5, #1
	b	.L3016
.L3022:
	ldr	r3, [r4, #300]
	uxth	r5, r8
	ldrh	r2, [r3, r5, lsl #1]
	ldr	r3, [r4, #3364]
	ldrh	r3, [r3, r5, lsl #1]
	cmp	r2, r3
	beq	.L3020
	cmp	r2, r9
	beq	.L3020
	ldrh	r1, [r4, #320]
	cmp	r1, r6
	beq	.L3020
	ldrh	r1, [r4, #416]
	cmp	r1, r6
	beq	.L3020
	ldrh	r1, [r4, #368]
	cmp	r1, r6
	beq	.L3020
	mov	r1, r5
	mov	r0, r10
	bl	printk
	ldr	r3, [r4, #300]
	ldrh	r2, [r3, r5, lsl #1]
	cbnz	r2, .L3021
	ldr	r2, [r4, #3364]
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
.L3020:
	add	r8, r8, #1
	b	.L3019
.L3021:
	ldr	r2, [r4, #3364]
	mov	r0, r6
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
	bl	update_vpc_list
	b	.L3020
.L3027:
	.align	2
.L3026:
	.word	.LANCHOR0
	.word	.LANCHOR3+203
	.word	.LC110
	.word	.LANCHOR2
	.word	.LC152
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	1
	.global	FtlGcScanTempBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3078
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	mov	r4, r0
	str	r1, [sp, #16]
	ldrh	r6, [r3, #3444]
	movw	r3, #65535
	cmp	r6, r3
	beq	.L3062
	cbnz	r6, .L3029
.L3030:
	bl	FtlGcPageVarInit
	b	.L3031
.L3062:
	movs	r6, #0
.L3029:
	ldr	r3, .L3078+4
	ldr	r2, [sp, #16]
	ldrh	r3, [r3, #2390]
	cmp	r3, r2
	beq	.L3030
.L3031:
	ldr	r5, .L3078+8
	mov	r3, #-1
	str	r3, [sp, #12]
	movs	r3, #0
	str	r3, [sp, #4]
.L3032:
	ldrh	r1, [r4]
	movw	r3, #65535
	movs	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L3033
	ldr	r7, .L3078+4
.L3059:
	ldr	r3, [r5, #3304]
	movs	r2, #0
	ldrh	ip, [r7, #2324]
	add	lr, r4, #16
	ldr	r10, [r5, #1148]
	mov	fp, r2
	str	r3, [sp, #8]
	movw	r8, #65535
	ldr	r3, [r5, #1144]
	mov	r9, #36
	str	r3, [sp, #20]
	ldrh	r3, [r7, #2400]
	str	r3, [sp, #24]
	ldrh	r3, [r7, #2402]
	str	r3, [sp, #28]
.L3034:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L3038
	mov	r10, #0
	movs	r2, #0
	mov	r1, fp
	ldr	r0, [sp, #8]
	bl	FlashReadPages
.L3039:
	uxth	r3, r10
	cmp	fp, r3
	bhi	.L3057
	ldr	r3, [sp, #4]
	adds	r6, r6, #1
	uxth	r6, r6
	adds	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #16]
	cmp	r3, r2
	bls	.L3058
.L3060:
	ldrh	r3, [r7, #2390]
	cmp	r3, r6
	bhi	.L3059
	movs	r2, #0
	b	.L3033
.L3038:
	ldrh	r3, [lr], #2
	cmp	r3, r8
	beq	.L3035
	ldr	r1, [sp, #8]
	orr	r3, r6, r3, lsl #10
	mov	r0, fp
	mla	r1, r9, fp, r1
	str	r3, [r1, #4]
	ldr	r3, [sp, #24]
	muls	r3, r0, r3
	ldr	r0, [sp, #20]
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r0
	mov	r0, fp
	str	r3, [r1, #8]
	ldr	r3, [sp, #28]
	muls	r3, r0, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	add	r3, r3, r10
	str	r3, [r1, #12]
	add	r3, fp, #1
	uxth	fp, r3
.L3035:
	adds	r2, r2, #1
	b	.L3034
.L3057:
	mov	r9, #36
	ldr	r8, [r5, #3304]
	mul	r9, r9, r10
	add	r3, r8, r9
	ldr	r2, [r3, #4]
	str	r3, [sp, #20]
	ubfx	r0, r2, #10, #16
	str	r2, [sp, #8]
	bl	P2V_plane
	ldr	r8, [r8, r9]
	mov	r2, r0
	ldr	r3, [sp, #20]
	ldr	r3, [r3, #12]
	cmp	r8, #0
	bne	.L3040
	ldrh	r0, [r3]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L3041
.L3044:
	ldrb	r3, [r7, #152]	@ zero_extendqisi2
	cbz	r3, .L3074
	movs	r3, #1
	str	r3, [r5, #3468]
.L3033:
	ldr	r3, .L3078
	movw	r1, #65535
	strh	r6, [r4, #2]	@ movhi
	mov	r0, r4
	strb	r2, [r4, #6]
	strh	r1, [r3, #3444]	@ movhi
	mov	r1, r6
	bl	ftl_sb_update_avl_pages
	b	.L3028
.L3041:
	ldr	r0, [r3, #8]
	ldr	r1, [r7, #2452]
	cmp	r0, r1
	bhi	.L3044
	ldrb	r2, [r7, #36]	@ zero_extendqisi2
	cbnz	r2, .L3047
.L3048:
	ldr	r2, [r3, #8]
	add	r10, r10, #1
	ldr	r1, [sp, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L3039
.L3074:
	ldrh	r1, [r4]
	ldr	r2, [r5, #300]
	strh	r3, [r2, r1, lsl #1]	@ movhi
.L3077:
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
	strh	r3, [r5, #556]	@ movhi
.L3076:
	bl	FtlGcPageVarInit
	movs	r6, #0
	b	.L3032
.L3047:
	mov	r2, r8
	add	r1, sp, #32
	str	r3, [sp, #20]
	bl	log2phys
	ldr	r3, [sp, #20]
	ldr	r1, [sp, #32]
	ldr	r2, [r3, #12]
	cmp	r2, r1
	bne	.L3048
	adds	r1, r2, #1
	beq	.L3048
	str	r2, [sp, #40]
	movs	r1, #1
	ldr	r2, [r5, #3336]
	add	r0, sp, #36
	str	r2, [sp, #44]
	ldr	r2, [r5, #3344]
	str	r2, [sp, #48]
	mov	r2, r8
	bl	FlashReadPages
	ldrh	r2, [r7, #2396]
	ldr	r1, [r5, #3304]
	ldr	r0, [sp, #44]
	ldr	r3, [sp, #20]
	lsl	ip, r2, #7
	add	r9, r9, r1
.L3049:
	cmp	r8, ip
	beq	.L3048
	ldr	r1, [r9, #8]
	ldr	r2, [r0, r8, lsl #2]
	ldr	r1, [r1, r8, lsl #2]
	cmp	r1, r2
	beq	.L3050
	ldr	r2, [sp, #40]
	ldrh	r1, [r4]
	ldr	r0, .L3078+12
	bl	printk
	ldrh	r2, [r4]
	movs	r1, #0
	ldr	r3, [r5, #300]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	b	.L3077
.L3050:
	add	r8, r8, #1
	b	.L3049
.L3040:
	ldr	r2, [sp, #8]
	ldrh	r1, [r4]
	ldr	r0, .L3078+16
	bl	printk
	ldr	r3, [r7, #2248]
	ldrh	r2, [r4]
	cbnz	r3, .L3053
	ldrb	r3, [r7, #152]	@ zero_extendqisi2
	cbz	r3, .L3054
.L3053:
	ldr	r3, [r5, #236]
	ldrh	r3, [r3, r2, lsl #1]
	cmp	r3, #159
	bls	.L3055
.L3054:
	ldr	r3, [r5, #3304]
	ldr	r3, [r3, r9]
	adds	r3, r3, #1
	bne	.L3056
.L3055:
	ldr	r3, [r5, #3304]
	add	r3, r3, r9
	ldr	r3, [r3, #4]
	str	r3, [sp, #12]
.L3056:
	ldr	r3, [r5, #300]
	movs	r1, #0
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r4]	@ movhi
	b	.L3076
.L3058:
	ldr	r2, .L3078
	movw	r1, #65535
	ldrh	r3, [r2, #3444]
	cmp	r3, r1
	beq	.L3060
	ldr	r1, [sp, #4]
	add	r3, r3, r1
	strh	r3, [r2, #3444]	@ movhi
	ldrh	r3, [r7, #2390]
	cmp	r3, r6
	bls	.L3060
.L3028:
	ldr	r0, [sp, #12]
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3079:
	.align	2
.L3078:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC153
	.word	.LC154
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	1
	.global	FtlReadRefresh
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r5, .L3096
	ldr	r9, [r5, #684]
	mov	r6, r5
	cmp	r9, #0
	beq	.L3081
	ldr	r4, .L3096+4
	ldr	r1, [r5, #688]
	ldr	r2, [r4, #2452]
	cmp	r1, r2
	bcs	.L3082
	mov	r5, #2048
.L3087:
	ldr	r0, [r6, #688]
	ldr	r3, [r4, #2452]
	cmp	r0, r3
	bcc	.L3083
.L3086:
	mov	r0, #-1
.L3080:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3083:
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r6, #688]
	adds	r1, r2, #1
	add	r3, r3, #1
	str	r3, [r6, #688]
	beq	.L3085
	str	r2, [sp, #8]
	add	r0, sp, #40
	movs	r2, #0
	movs	r1, #1
	str	r2, [r0, #-36]!
	str	r3, [sp, #20]
	str	r2, [sp, #12]
	str	r2, [sp, #16]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmp	r3, #256
	bne	.L3086
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L3086
.L3085:
	subs	r5, r5, #1
	bne	.L3087
	b	.L3086
.L3082:
	ldr	r3, [r5, #476]
	movs	r0, #0
	str	r0, [r5, #684]
	str	r0, [r5, #688]
	str	r3, [r5, #680]
	b	.L3080
.L3081:
	ldr	r8, [r5, #476]
	movw	r4, #10000
	ldr	r1, [r5, #528]
	ldr	r7, [r5, #680]
	add	r3, r8, #1048576
	cmp	r1, r4
	ite	hi
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r3
	bhi	.L3091
	ldr	r3, .L3096+4
	lsrs	r1, r1, #10
	mov	r0, #1000
	adds	r1, r1, #1
	ldr	r3, [r3, #2452]
	muls	r0, r3, r0
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r8, r0
	bhi	.L3091
	ldrh	r3, [r5, #272]
	ands	r0, r4, r3
	bne	.L3093
	ldr	r2, [r5, #704]
	cmp	r3, r2
	beq	.L3080
.L3091:
	ldrh	r3, [r6, #272]
	movs	r0, #0
	str	r0, [r6, #688]
	str	r8, [r6, #680]
	str	r3, [r6, #704]
	movs	r3, #1
	str	r3, [r6, #684]
	b	.L3080
.L3093:
	mov	r0, r9
	b	.L3080
.L3097:
	.align	2
.L3096:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	1
	.global	FtlGcFreeTempBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	ldr	r4, .L3136
	ldr	r6, .L3136+4
	ldr	r2, [r4, #228]
	ldrh	r1, [r6, #2390]
	cbz	r2, .L3099
.L3134:
	movs	r0, #0
.L3098:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3099:
	ldrh	r5, [r4, #416]
	movw	r7, #65535
	cmp	r5, r7
	bne	.L3101
.L3110:
	ldrh	r2, [r4, #416]
	movw	r3, #65535
	movs	r5, #0
	str	r5, [r4, #3468]
	cmp	r2, r3
	beq	.L3134
	bl	FtlCacheWriteBack
	ldrb	r3, [r4, #423]	@ zero_extendqisi2
	mov	r9, #12
	ldrh	r0, [r6, #2390]
	ldr	r2, [r4, #300]
	ldrh	r1, [r4, #416]
	smulbb	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #496]
	ldrh	r3, [r4, #1172]
	add	r3, r3, r2
	str	r3, [r4, #496]
.L3111:
	ldrh	r2, [r4, #1172]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L3115
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r6, #152]	@ zero_extendqisi2
	cbz	r3, .L3116
	ldrh	r1, [r4, #416]
	ldr	r0, .L3136+8
	bl	printk
.L3116:
	ldrh	r0, [r4, #416]
	ldr	r3, [r4, #300]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L3117
	bl	INSERT_DATA_LIST
.L3118:
	movw	r7, #65535
	movs	r5, #0
	strh	r7, [r4, #416]	@ movhi
	strh	r5, [r4, #1172]	@ movhi
	strh	r5, [r4, #1164]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r6, #2248]
	strh	r7, [r4, #556]	@ movhi
	cmp	r3, #0
	beq	.L3119
	ldr	r3, [r4, #532]
	cmp	r3, #39
	bhi	.L3119
	ldrh	r3, [r4, #536]
	ldrh	r2, [r4, #316]
	cmp	r2, r3
	bcs	.L3134
	lsls	r3, r3, #1
.L3135:
	strh	r3, [r4, #1120]	@ movhi
	b	.L3134
.L3101:
	cbz	r0, .L3104
	ldr	r3, .L3136+12
	ldrh	r0, [r3, #3444]
	cmp	r0, r7
	beq	.L3105
.L3106:
	movs	r1, #2
.L3104:
	ldr	r0, .L3136+16
	bl	FtlGcScanTempBlk
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L3107
	ldr	r2, [r4, #236]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L3108
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L3108:
	ldr	r3, [r4, #3468]
	cbnz	r3, .L3109
	ldr	r3, [r4, #700]
	ldr	r0, [sp, #4]
	adds	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #700]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3109:
	movs	r3, #0
	str	r3, [r4, #3468]
.L3121:
	movs	r0, #1
	b	.L3098
.L3105:
	strh	r2, [r3, #3444]	@ movhi
	ldrh	r3, [r4, #316]
	cmp	r3, #17
	bhi	.L3106
	b	.L3104
.L3107:
	ldr	r3, .L3136+12
	ldrh	r2, [r3, #3444]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3121
	b	.L3110
.L3115:
	uxth	r8, r5
	ldr	r10, [r4, #1168]
	ldr	r3, [r6, #2452]
	mul	r8, r9, r8
	add	r7, r10, r8
	ldr	r0, [r7, #8]
	cmp	r0, r3
	bcc	.L3112
.L3132:
	ldrh	r0, [r4, #416]
	b	.L3133
.L3112:
	movs	r2, #0
	add	r1, sp, #4
	bl	log2phys
	ldr	r0, [r10, r8]
	ldr	r3, [sp, #4]
	cmp	r0, r3
	bne	.L3114
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	movs	r2, #1
	mov	r8, r0
	adds	r1, r7, #4
	ldr	r0, [r7, #8]
	bl	log2phys
	mov	r0, r8
.L3133:
	bl	decrement_vpc_count
	b	.L3113
.L3114:
	ldr	r2, [r7, #4]
	cmp	r3, r2
	bne	.L3132
.L3113:
	adds	r5, r5, #1
	b	.L3111
.L3117:
	bl	INSERT_FREE_LIST
	b	.L3118
.L3119:
	ldrh	r3, [r4, #536]
	ldrh	r2, [r4, #316]
	add	r1, r3, r3, lsl #1
	cmp	r2, r1, asr #2
	ble	.L3134
	ldrb	r0, [r6, #152]	@ zero_extendqisi2
	cbz	r0, .L3120
	subs	r3, r3, #2
	b	.L3135
.L3120:
	movs	r3, #20
	strh	r3, [r4, #1120]	@ movhi
	b	.L3098
.L3137:
	.align	2
.L3136:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC155
	.word	.LANCHOR1
	.word	.LANCHOR2+416
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	1
	.global	FtlGcPageRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r5, .L3140
	ldr	r4, .L3140+4
	ldrh	r1, [r5, #2390]
	add	r0, r4, #416
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #418]
	ldrh	r3, [r5, #2390]
	cmp	r2, r3
	bcc	.L3138
	add	r0, r4, #3408
	bl	FtlMapBlkWriteDumpData
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #3468]
.L3138:
	pop	{r3, r4, r5, pc}
.L3141:
	.align	2
.L3140:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	1
	.global	FtlPowerLostRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	ldr	r4, .L3143
	ldr	r3, .L3143+4
	add	r6, r4, #320
	add	r4, r4, #368
	mov	r0, r6
	str	r5, [r3, #1348]
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
.L3144:
	.align	2
.L3143:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	1
	.global	FtlSysBlkInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r3, #0
	ldr	r5, .L3162
	movw	r7, #65535
	ldr	r6, .L3162+4
	ldr	r4, .L3162+8
	ldrh	r0, [r5, #2328]
	strh	r3, [r6, #1344]	@ movhi
	strh	r7, [r6, #1342]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r3, [r4, #540]
	cmp	r3, r7
	mov	r7, r6
	bne	.L3146
.L3148:
	mov	r6, #-1
.L3145:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L3146:
	bl	FtlLoadSysInfo
	mov	r6, r0
	cmp	r0, #0
	bne	.L3148
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r5, #2430]
	mov	r3, r6
	ldr	r2, [r4, #464]
	movs	r0, #12
.L3149:
	cmp	r3, r1
	bge	.L3154
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3150
.L3154:
	ldrh	r2, [r4, #272]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #272]	@ movhi
	bge	.L3161
.L3151:
	ldrh	r1, [r4, #320]
	ldr	r2, [r4, #300]
	ldrh	r0, [r4, #324]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r5, #2390]
	ldr	r1, [r4, #300]
	ldrh	r0, [r4, #368]
	strh	r3, [r4, #322]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #326]
	strh	r3, [r4, #324]	@ movhi
	ldrh	r7, [r4, #372]
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r7
	strh	r2, [r1, r0, lsl #1]	@ movhi
	strb	r3, [r4, #374]
	strh	r3, [r4, #372]	@ movhi
	ldrh	r3, [r4, #274]
	ldrh	r2, [r5, #2390]
	adds	r3, r3, #1
	strh	r2, [r4, #370]	@ movhi
	strh	r3, [r4, #274]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L3155
.L3150:
	adds	r3, r3, #1
	b	.L3149
.L3161:
	ldrh	r3, [r7, #1344]
	cmp	r3, #0
	bne	.L3151
.L3155:
	ldrh	r0, [r4, #320]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3156
	ldrh	r3, [r4, #324]
	cbnz	r3, .L3156
	ldrh	r3, [r4, #372]
	cbnz	r3, .L3156
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4, #368]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L3162+12
	bl	allocate_new_data_superblock
	ldr	r0, .L3162+16
	bl	allocate_new_data_superblock
.L3156:
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	cbnz	r3, .L3157
	ldrh	r3, [r4, #272]
	lsls	r3, r3, #27
	bne	.L3145
.L3157:
	bl	FtlVpcCheckAndModify
	b	.L3145
.L3163:
	.align	2
.L3162:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2+320
	.word	.LANCHOR2+368
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	1
	.global	FtlLowFormat
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLowFormat, %function
FtlLowFormat:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldr	r4, .L3195
	ldr	r6, [r4, #228]
	cmp	r6, #0
	bne	.L3166
	ldr	r5, .L3195+4
	mov	r1, r6
	ldr	r0, [r4, #3392]
	ldrh	r2, [r5, #2428]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, #2428]
	mov	r1, r6
	ldr	r0, [r4, #3388]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r5, #2328]
	str	r6, [r4, #508]
	str	r6, [r4, #512]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3167
	bl	FtlMakeBbt
.L3167:
	ldr	r0, .L3195+8
	movs	r2, #0
.L3168:
	ldrh	r1, [r5, #2396]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L3169
	ldrh	r7, [r5, #2332]
	movs	r6, #0
.L3170:
	ldrh	r3, [r5, #2334]
	cmp	r3, r7
	bhi	.L3171
	ldrh	r1, [r5, #2324]
	subs	r3, r6, #3
	cmp	r3, r1, lsl #1
	blt	.L3172
	mov	r0, r6
	movs	r6, #0
	bl	__aeabi_uidiv
	ldr	r3, [r5, #2424]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r5, #2328]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r7, [r5, #2332]
.L3173:
	ldrh	r3, [r5, #2334]
	cmp	r3, r7
	bhi	.L3174
.L3172:
	movs	r7, #0
	mov	r8, r7
.L3175:
	ldrh	r3, [r5, #2332]
	uxth	r0, r7
	adds	r7, r7, #1
	cmp	r3, r0
	bhi	.L3176
	ldrh	r3, [r5, #2334]
	ldrh	r7, [r5, #2324]
	ldr	fp, [r5, #2336]
	str	r3, [r4, #3292]
	mov	r1, r7
	mov	r0, fp
	bl	__aeabi_uidiv
	ubfx	r9, r0, #5, #16
	mov	r10, r0
	add	r3, r9, #36
	str	r0, [r5, #2452]
	strh	r3, [r4, #536]	@ movhi
	movs	r3, #24
	muls	r3, r7, r3
	cmp	r8, r3
	ble	.L3177
	mov	r1, r7
	sub	r0, fp, r8
	bl	__aeabi_uidiv
	str	r0, [r5, #2452]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #536]	@ movhi
.L3177:
	ldr	r3, [r5, #2248]
	cmp	r3, #1
	bne	.L3178
	ldrh	fp, [r4, #536]
	mov	r1, r7
	mov	r0, r8
	bl	__aeabi_uidiv
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r4, #536]	@ movhi
.L3178:
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cbz	r3, .L3179
	ldrh	fp, [r4, #536]
	mov	r1, r7
	mov	r0, r8
	bl	__aeabi_uidiv
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r4, #536]	@ movhi
.L3179:
	ldrh	r3, [r5, #2384]
	cbz	r3, .L3181
	ldrh	r2, [r4, #536]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #536]	@ movhi
	mul	r2, r7, r3
	cmp	r8, r2
	itttt	lt
	addlt	r3, r3, #32
	strlt	r10, [r5, #2452]
	addlt	r3, r3, r9
	strhlt	r3, [r4, #536]	@ movhi
.L3181:
	ldrh	r2, [r4, #536]
	ldr	r3, [r5, #2452]
	ldr	r9, .L3195+28
	subs	r3, r3, r2
	muls	r7, r3, r7
	ldrh	r3, [r5, #2390]
	str	r7, [r9, #1292]
	muls	r7, r3, r7
	ldrh	r3, [r5, #2396]
	str	r7, [r5, #2452]
	muls	r7, r3, r7
	str	r7, [r5, #2432]
	bl	FtlBbmTblFlush
	ldr	r2, [r5, #2340]
	add	r1, r6, r8
	ldrh	r3, [r5, #2404]
	add	r3, r3, r2, lsr #3
	cmp	r1, r3
	bls	.L3183
	lsrs	r2, r2, #5
	ldr	r0, .L3195+12
	bl	printk
.L3183:
	ldrh	r2, [r5, #2334]
	movs	r1, #0
	ldr	r0, [r4, #300]
	movw	r7, #65535
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, #2332]
	movs	r3, #0
	ldr	r0, [r5, #32]
	movs	r1, #255
	ldr	r5, .L3195+16
	str	r3, [r4, #472]
	strh	r3, [r4, #558]	@ movhi
	lsrs	r2, r2, #3
	strb	r3, [r4, #562]
	strb	r3, [r4, #564]
	strh	r3, [r4, #322]	@ movhi
	strb	r3, [r4, #326]
	strh	r3, [r4, #320]	@ movhi
	movs	r3, #1
	strh	r7, [r4, #556]	@ movhi
	strb	r3, [r4, #328]
	bl	ftl_memset
.L3184:
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r4, #327]	@ zero_extendqisi2
	ldrh	r2, [r4, #320]
	cbnz	r3, .L3185
	ldr	r3, [r4, #300]
	strh	r7, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #320]
	adds	r3, r3, #1
	strh	r3, [r4, #320]	@ movhi
	b	.L3184
.L3169:
	ldr	r6, [r4, #3332]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r6, r3, lsl #2]
	ldr	r1, [r4, #3336]
	str	r0, [r1, r3, lsl #2]
	b	.L3168
.L3171:
	mov	r0, r7
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r7, r7, #1
	add	r6, r6, r0
	uxth	r6, r6
	uxth	r7, r7
	b	.L3170
.L3174:
	mov	r0, r7
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r7, r7, #1
	add	r6, r6, r0
	uxth	r6, r6
	uxth	r7, r7
	b	.L3173
.L3176:
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r8, r8, r0
	uxth	r8, r8
	b	.L3175
.L3185:
	ldr	r3, [r4, #508]
	movw	r5, #65535
	ldrh	r1, [r4, #324]
	ldr	r7, .L3195+20
	str	r3, [r4, #332]
	adds	r3, r3, #1
	str	r3, [r4, #508]
	ldr	r3, [r4, #300]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #370]	@ movhi
	strb	r3, [r4, #374]
	ldrh	r3, [r4, #320]
	adds	r3, r3, #1
	strh	r3, [r4, #368]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #376]
.L3186:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #375]	@ zero_extendqisi2
	ldrh	r2, [r4, #368]
	cbnz	r3, .L3187
	ldr	r3, [r4, #300]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #368]
	adds	r3, r3, #1
	strh	r3, [r4, #368]	@ movhi
	b	.L3186
.L3187:
	ldr	r3, [r4, #508]
	movw	r5, #65535
	ldrh	r1, [r4, #372]
	str	r3, [r4, #380]
	adds	r3, r3, #1
	str	r3, [r4, #508]
	ldr	r3, [r4, #300]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r5, [r4, #416]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r0, [r4, #540]	@ movhi
	strh	r3, [r4, #542]	@ movhi
	ldr	r3, [r9, #1292]
	strh	r5, [r4, #544]	@ movhi
	strh	r3, [r4, #546]	@ movhi
	ldr	r3, [r4, #508]
	str	r3, [r4, #548]
	adds	r3, r3, #1
	str	r3, [r4, #508]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L3166
	ldr	r3, .L3195+24
	movs	r2, #1
	str	r2, [r3, #500]
.L3166:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3196:
	.align	2
.L3195:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	168778952
	.word	.LC156
	.word	.LANCHOR2+320
	.word	.LANCHOR2+368
	.word	.LANCHOR1
	.word	.LANCHOR4
	.fnend
	.size	FtlLowFormat, .-FtlLowFormat
	.align	1
	.global	FtlReInitForSDUpdata
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #16
	ldr	r4, .L3232
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cbz	r3, .L3198
.L3200:
	movs	r5, #0
.L3197:
	mov	r0, r5
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L3198:
	ldr	r6, .L3232+4
	ldr	r0, [r6, #1180]
	bl	FlashInit
	mov	r5, r0
	cmp	r0, #0
	bne	.L3200
	bl	FlashLoadFactorBbt
	cbz	r0, .L3201
	bl	FlashMakeFactorBbt
.L3201:
	ldr	r0, [r6, #1220]
	bl	FlashReadIdbDataRaw
	cbz	r0, .L3202
	movs	r2, #16
	movs	r1, #0
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp]
	movs	r3, #0
	mov	r2, r3
	movs	r0, #1
.L3204:
	lsl	r6, r0, r2
	adds	r2, r2, #1
	tst	r6, r1
	it	ne
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L3204
	cmp	r3, #6
	bhi	.L3205
.L3229:
	strb	r2, [r4, #37]
.L3206:
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	strh	r3, [r4, #150]	@ movhi
.L3202:
	ldr	r1, .L3232+8
	ldr	r0, .L3232+12
	bl	printk
	ldr	r0, .L3232+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #2328]
	movs	r4, #1
	bl	FtlFreeSysBlkQueueInit
.L3210:
	bl	FtlLoadBbt
	cbz	r0, .L3211
.L3231:
	bl	FtlLowFormat
	cmp	r4, #3
	bls	.L3212
	mov	r5, #-1
	b	.L3197
.L3205:
	movs	r2, #0
	movs	r0, #1
.L3208:
	lsl	r6, r0, r2
	adds	r2, r2, #1
	tst	r6, r1
	it	ne
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L3208
	cmp	r3, #17
	bls	.L3229
	movs	r3, #36
	strb	r3, [r4, #37]
	b	.L3206
.L3212:
	adds	r4, r4, #1
	b	.L3210
.L3211:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3231
	ldr	r3, .L3232+20
	movs	r2, #1
	str	r2, [r3, #500]
	b	.L3197
.L3233:
	.align	2
.L3232:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC76
	.word	.LC77
	.word	.LANCHOR0+124
	.word	.LANCHOR1
	.fnend
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	1
	.global	Ftl_gc_temp_data_write_back
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, .L3249
	ldr	r3, [r4, #228]
	cbz	r3, .L3235
.L3238:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L3235:
	ldr	r3, .L3249+4
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cbz	r3, .L3237
	ldr	r3, [r4, #1136]
	lsls	r3, r3, #31
	bpl	.L3237
	ldrh	r3, [r4, #420]
	cmp	r3, #0
	bne	.L3238
.L3237:
	movs	r3, #0
	movs	r5, #0
	movs	r6, #36
	mov	r2, r3
	ldr	r1, [r4, #1136]
	ldr	r0, [r4, #3308]
	bl	FlashProgPages
.L3239:
	ldr	r1, [r4, #1136]
	uxth	r3, r5
	cmp	r3, r1
	bcc	.L3241
	ldr	r0, [r4, #3308]
	bl	FtlGcBufFree
	movs	r3, #0
	str	r3, [r4, #1136]
	ldrh	r3, [r4, #420]
	cmp	r3, #0
	bne	.L3238
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L3248
.L3241:
	muls	r3, r6, r3
	ldr	r2, [r4, #3308]
	adds	r5, r5, #1
	adds	r1, r2, r3
	ldr	r2, [r2, r3]
	ldr	r0, [r1, #12]
	cmp	r2, #-1
	bne	.L3240
	ldrh	r0, [r4, #416]
	movs	r5, #0
	ldr	r1, [r4, #300]
	strh	r5, [r1, r0, lsl #1]	@ movhi
	strh	r2, [r4, #416]	@ movhi
	ldr	r2, [r4, #700]
	adds	r2, r2, #1
	str	r2, [r4, #700]
	ldr	r2, [r4, #3308]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L3248:
	movs	r0, #1
	pop	{r4, r5, r6, pc}
.L3240:
	ldr	r2, [r0, #8]
	ldr	r1, [r1, #4]
	ldr	r0, [r0, #12]
	bl	FtlGcUpdatePage
	b	.L3239
.L3250:
	.align	2
.L3249:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	1
	.global	Ftl_get_new_temp_ppa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r3, #65535
	ldr	r4, .L3254
	ldrh	r2, [r4, #416]
	cmp	r2, r3
	beq	.L3252
	ldrh	r3, [r4, #420]
	cbnz	r3, .L3253
.L3252:
	bl	FtlCacheWriteBack
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3254+4
	strb	r5, [r4, #424]
	bl	allocate_data_superblock
	strh	r5, [r4, #1164]	@ movhi
	strh	r5, [r4, #1172]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3253:
	ldr	r0, .L3254+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L3255:
	.align	2
.L3254:
	.word	.LANCHOR2
	.word	.LANCHOR2+416
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	1
	.global	ftl_do_gc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3422
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	.pad #44
	sub	sp, sp, #44
	mov	r7, r1
	mov	r6, r3
	ldr	r0, [r3, #228]
	cmp	r0, #0
	bne	.L3355
	ldr	r1, .L3422+4
	ldr	r4, [r1, #500]
	cmp	r4, #1
	bne	.L3256
	ldr	r2, [r3, #3280]
	cmp	r2, #0
	bne	.L3256
	ldrh	r0, [r3, #308]
	cmp	r0, #47
	bls	.L3355
	ldrh	r1, [r1, #3444]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3258
.L3261:
	ldrh	r0, [r6, #1176]
	movw	r2, #65535
	cmp	r0, r2
	bne	.L3259
.L3260:
	ldr	r3, [r6, #1124]
	cmp	r8, #1
	add	r3, r3, #1
	add	r3, r3, r8, lsl #7
	str	r3, [r6, #1124]
	bne	.L3262
	ldr	r2, .L3422+8
	ldr	r1, [r2, #2248]
	mov	r9, r2
	cbnz	r1, .L3263
	ldrb	r2, [r2, #152]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3262
.L3263:
	ldr	r2, [r6, #532]
	cmp	r2, #39
	bhi	.L3262
	ldr	r5, .L3422+12
	movw	r4, #65535
	ldrh	r2, [r5, #1480]
	add	r3, r3, r2
	str	r3, [r6, #1124]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r6, #556]
	cmp	r3, r4
	bne	.L3264
	ldrh	r2, [r6, #1174]
	cmp	r2, r3
	bne	.L3352
	ldr	r3, [r6, #1124]
	cmp	r3, #1024
	bhi	.L3266
	ldrh	r3, [r6, #316]
	cmp	r3, #63
	bhi	.L3352
.L3266:
	ldrh	r3, [r6, #1122]
	movs	r2, #0
	ldrh	r1, [r6, #316]
	strh	r2, [r5, #1480]	@ movhi
	adds	r3, r3, #64
	cmp	r1, r3
	bgt	.L3352
	ldr	r3, [r6, #532]
	str	r2, [r6, #1124]
	cmp	r3, #0
	bne	.L3267
	movs	r3, #6
.L3413:
	strh	r3, [r5, #1480]	@ movhi
.L3268:
	movs	r0, #32
	bl	List_get_gc_head_node
	uxth	r2, r0
	movw	r3, #65535
	str	r3, [sp, #16]
	cmp	r2, r3
	beq	.L3272
	ldrh	r4, [r6, #1128]
	cmp	r4, #0
	beq	.L3270
	ldrh	ip, [r9, #2392]
	uxth	r10, r0
	ldrh	r1, [r9, #2324]
	ldr	r2, [r6, #300]
	mul	r1, r1, ip
	ldrh	r0, [r2, r10, lsl #1]
	str	r2, [sp, #12]
	adds	r1, r1, #1
	cmp	r0, r1
	bgt	.L3272
	add	fp, r4, #1
	mov	r0, r4
	uxth	fp, fp
	mov	r9, #0
	str	r9, [r6, #1132]
	strh	fp, [r6, #1128]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [sp, #16]
	uxth	r4, r0
	ldr	r2, [sp, #12]
	cmp	r4, r3
	beq	.L3272
	ldrh	r3, [r2, r4, lsl #1]
	mov	r1, fp
	ldrh	r2, [r2, r10, lsl #1]
	ldr	r0, .L3422+16
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r6, #1128]
	cmp	r3, #40
	bls	.L3271
	ldr	r3, [r6, #300]
	ldrh	r3, [r3, r4, lsl #1]
	cmp	r3, #32
	it	hi
	strhhi	r9, [r6, #1128]	@ movhi
.L3271:
	movs	r3, #6
	strh	r3, [r5, #1480]	@ movhi
.L3264:
	ldrh	r2, [r6, #416]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3299
.L3354:
	ldrh	r3, [r6, #1174]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3299
	cmp	r4, r3
	bne	.L3299
	ldrh	r3, [r6, #556]
	cmp	r3, r4
	beq	.L3300
.L3305:
	movw	r4, #65535
.L3299:
	ldr	r3, .L3422+8
	ldr	r3, [r3, #2248]
	cmp	r3, #0
	ite	eq
	moveq	r5, #1
	movne	r5, #2
.L3298:
	ldrh	r3, [r6, #556]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3308
	cmp	r4, r3
	beq	.L3309
	strh	r4, [r6, #556]	@ movhi
.L3310:
	ldrh	r0, [r6, #556]
	movw	r7, #65535
	movs	r3, #0
	strb	r3, [r6, #564]
	cmp	r0, r7
	beq	.L3308
	bl	IsBlkInGcList
	cbz	r0, .L3313
	strh	r7, [r6, #556]	@ movhi
.L3313:
	ldr	r3, .L3422+8
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cbz	r3, .L3314
	ldrh	r0, [r6, #556]
	bl	ftl_get_blk_mode
	strb	r0, [r6, #564]
.L3314:
	ldrh	r2, [r6, #556]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L3308
	ldr	r0, .L3422+20
	bl	make_superblock
	ldr	r2, .L3422+12
	movs	r3, #0
	strh	r3, [r6, #558]	@ movhi
	strb	r3, [r6, #562]
	strh	r3, [r2, #1482]	@ movhi
	ldrh	r1, [r6, #556]
	ldr	r3, [r6, #300]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #1484]	@ movhi
.L3308:
	ldrh	r3, [r6, #556]
	ldrh	r2, [r6, #320]
	cmp	r2, r3
	beq	.L3315
	ldrh	r2, [r6, #368]
	cmp	r2, r3
	beq	.L3315
	ldrh	r2, [r6, #416]
	cmp	r2, r3
	bne	.L3316
.L3315:
	movw	r3, #65535
	strh	r3, [r6, #556]	@ movhi
.L3316:
	ldr	r7, .L3422+8
.L3349:
	ldr	r9, .L3422
	movw	r3, #65535
	ldrh	fp, [r9, #556]
	mov	r6, r9
	cmp	fp, r3
	bne	.L3317
	movs	r3, #0
	str	r3, [r9, #1132]
.L3318:
	ldrh	r10, [r6, #1128]
	mov	r0, r10
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, fp
	strh	r2, [r6, #556]	@ movhi
	bne	.L3319
	movs	r3, #0
	movs	r0, #8
	strh	r3, [r6, #1128]	@ movhi
	b	.L3256
.L3258:
	ldrh	r3, [r3, #416]
	cmp	r3, r2
	beq	.L3261
	mov	r0, r4
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3261
	mov	r0, r4
	b	.L3256
.L3259:
	ldrh	r3, [r6, #1174]
	cmp	r3, r2
	bne	.L3260
	ldrh	r1, [r6, #1178]
	cmp	r1, r3
	beq	.L3260
	ldrh	r2, [r6, #1180]
	cmp	r2, r3
	itttt	ne
	strhne	r0, [r6, #1174]	@ movhi
	strhne	r1, [r6, #1176]	@ movhi
	strhne	r2, [r6, #1178]	@ movhi
	strhne	r3, [r6, #1180]	@ movhi
	b	.L3260
.L3267:
	cmp	r3, #5
	bhi	.L3268
	movs	r3, #18
	b	.L3413
.L3270:
	movs	r3, #1
	strh	r3, [r6, #1128]	@ movhi
.L3272:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	mov	r4, r0
	cmp	r0, r3
	bne	.L3264
	movs	r3, #0
	strh	r3, [r5, #1480]	@ movhi
.L3262:
	ldrh	r3, [r6, #556]
	movw	r4, #65535
	cmp	r3, r4
	beq	.L3352
	cmp	r8, #0
	beq	.L3353
	b	.L3264
.L3423:
	.align	2
.L3422:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC157
	.word	.LANCHOR2+556
.L3352:
	ldrh	r4, [r6, #416]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L3273
	ldrh	r5, [r6, #1174]
	cmp	r5, r4
	bne	.L3274
	ldrh	r3, [r6, #316]
	ldr	r2, [r6, #1124]
	cmp	r3, #24
	ite	cc
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	bls	.L3274
	ldr	r2, .L3424
	movs	r3, #0
	str	r3, [r6, #1124]
	strh	r3, [r2, #1480]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r5
	mov	r4, r0
	bne	.L3362
	ldrh	r2, [r6, #316]
	ldrh	r3, [r6, #1122]
	cmp	r2, r3
	bcs	.L3277
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L3279
	ldr	r2, [r6, #3276]
	ldr	r9, .L3424+8
	cbnz	r2, .L3280
	ldrh	r2, [r9, #2344]
	cmp	r2, #3
	beq	.L3280
	ldr	r2, [r6, #1116]
	cbnz	r2, .L3280
	ldr	r2, [r9, #2248]
	cbnz	r2, .L3280
	ldrb	r0, [r9, #152]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3281
.L3280:
	ldr	r2, [r6, #300]
	ldrh	r0, [r9, #2344]
	ldrh	r1, [r2, r3, lsl #1]
	ldrh	r3, [r9, #2392]
	cmp	r0, #3
	ldrh	r2, [r9, #2324]
	mul	r2, r3, r2
	ite	eq
	lsreq	r3, r3, #1
	movne	r3, #0
	add	r3, r3, r2
	cmp	r1, r3
	bgt	.L3283
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r9, #2452]
	uxth	r5, r0
	ldr	r2, [r6, #472]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bls	.L3284
.L3415:
	movs	r3, #128
	b	.L3414
.L3284:
	movs	r3, #160
.L3414:
	strh	r3, [r6, #1122]	@ movhi
	movw	r3, #65535
	cmp	r5, r3
	beq	.L3279
.L3276:
	ldr	r3, [r6, #300]
	mov	r4, r5
	ldrh	r1, [r6, #1120]
	ldrh	r2, [r6, #316]
	ldrh	r3, [r3, r5, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r6, #236]
	ldr	r0, .L3424+4
	ldrh	r1, [r1, r5, lsl #1]
	str	r1, [sp]
	mov	r1, r5
	bl	printk
	b	.L3279
.L3283:
	movs	r3, #128
.L3416:
	strh	r3, [r6, #1122]	@ movhi
.L3279:
	bl	FtlGcReFreshBadBlk
	cmp	r8, #0
	bne	.L3264
	movw	r3, #65535
	cmp	r4, r3
	bne	.L3264
.L3353:
	ldrh	r3, [r6, #316]
	cmp	r3, #24
	bhi	.L3364
	ldr	r2, .L3424+8
	cmp	r3, #16
	ldrh	r5, [r2, #2390]
	bls	.L3289
	lsrs	r5, r5, #5
.L3288:
	ldrh	r2, [r6, #1120]
	cmp	r2, r3
	bcs	.L3292
	ldrh	r3, [r6, #416]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3293
	ldrh	r2, [r6, #1174]
	cmp	r2, r3
	bne	.L3293
	ldr	r3, .L3424
	ldrh	r0, [r3, #1480]
	cbnz	r0, .L3294
	ldr	r3, .L3424+8
	ldr	r2, [r6, #472]
	ldr	r3, [r3, #2452]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L3295
.L3294:
	ldrh	r3, [r6, #536]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L3417:
	strh	r3, [r6, #1120]	@ movhi
	movs	r3, #0
	str	r3, [r6, #1132]
.L3256:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3281:
	ldr	r2, [r6, #300]
	ldrh	r3, [r2, r3, lsl #1]
	cmp	r3, #7
	bhi	.L3286
	bl	List_get_gc_head_node
	uxth	r5, r0
	b	.L3415
.L3286:
	movs	r3, #64
	b	.L3416
.L3277:
	movs	r3, #80
	b	.L3416
.L3362:
	mov	r5, r0
	b	.L3276
.L3289:
	cmp	r3, #12
	bls	.L3290
	lsrs	r5, r5, #4
	b	.L3288
.L3290:
	cmp	r3, #8
	bls	.L3288
	lsrs	r5, r5, #2
	b	.L3288
.L3364:
	movs	r5, #1
	b	.L3288
.L3295:
	movs	r3, #18
	b	.L3417
.L3293:
	ldrh	r3, [r6, #536]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r6, #1120]	@ movhi
.L3292:
	ldr	r3, .L3424+8
	ldr	r3, [r3, #2248]
	cbz	r3, .L3366
	cmp	r7, #2
	bhi	.L3366
	adds	r5, r5, #1
	uxth	r5, r5
.L3366:
	movw	r4, #65535
	b	.L3298
.L3300:
	movs	r3, #0
	ldrh	r2, [r6, #316]
	str	r3, [r6, #1132]
	ldrh	r3, [r6, #1120]
	ldr	r4, .L3424
	cmp	r2, r3
	bls	.L3301
	ldrh	r3, [r4, #1480]
	cbnz	r3, .L3302
	ldr	r3, .L3424+8
	ldr	r2, [r6, #472]
	ldr	r3, [r3, #2452]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L3303
.L3302:
	ldrh	r3, [r6, #536]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L3418:
	strh	r3, [r6, #1120]	@ movhi
	bl	FtlReadRefresh
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r6, #300]
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #4
	bls	.L3301
.L3420:
	ldrh	r0, [r4, #1480]
	b	.L3256
.L3303:
	movs	r3, #18
	b	.L3418
.L3301:
	ldrh	r0, [r4, #1480]
	cmp	r0, #0
	bne	.L3305
	ldrh	r5, [r6, #536]
	add	r3, r5, r5, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r6, #1120]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r6, #300]
	uxth	r0, r0
	ldr	r1, .L3424+8
	ldrh	r2, [r3, r0, lsl #1]
	ldrh	r0, [r1, #2392]
	ldrh	r3, [r1, #2324]
	muls	r3, r0, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L3306
	ldrh	r3, [r6, #316]
	subs	r5, r5, #1
	cmp	r3, r5
	blt	.L3306
	bl	FtlReadRefresh
	b	.L3420
.L3306:
	cmp	r2, #0
	bne	.L3305
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r6, #316]
	adds	r0, r0, #1
	b	.L3256
.L3309:
	ldrh	r3, [r6, #1174]
	cmp	r3, r4
	beq	.L3310
	ldr	r2, [r6, #300]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L3311
	strh	r4, [r6, #1174]	@ movhi
.L3311:
	ldrh	r3, [r6, #1174]
	strh	r3, [r6, #556]	@ movhi
	movw	r3, #65535
	strh	r3, [r6, #1174]	@ movhi
	b	.L3310
.L3319:
	str	r0, [sp, #16]
	mov	r0, r2
	str	r2, [sp, #12]
	add	r10, r10, #1
	bl	IsBlkInGcList
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #16]
	cbz	r0, .L3320
	strh	r10, [r6, #1128]	@ movhi
	b	.L3318
.L3320:
	ldrh	lr, [r7, #2324]
	uxth	r10, r10
	ldrh	r1, [r7, #2390]
	uxth	r3, r3
	ldr	r0, [r6, #300]
	strh	r10, [r6, #1128]	@ movhi
	mul	r1, lr, r1
	ldrh	ip, [r0, r3, lsl #1]
	add	lr, r1, r1, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L3322
	cmp	r10, #48
	bls	.L3323
	cmp	ip, #8
	bls	.L3323
	ldrh	ip, [r6, #1164]
	cmp	ip, #35
	bhi	.L3323
.L3322:
	mov	ip, #0
	strh	ip, [r6, #1128]	@ movhi
.L3323:
	ldrh	r3, [r0, r3, lsl #1]
	cmp	r1, r3
	bgt	.L3324
	cmp	r4, fp
	bne	.L3324
	ldrh	r1, [r6, #1128]
	cmp	r1, #3
	bhi	.L3324
	movs	r3, #0
	strh	r4, [r6, #556]	@ movhi
	strh	r3, [r6, #1128]	@ movhi
.L3421:
	ldr	r3, .L3424
	ldrh	r0, [r3, #1480]
	b	.L3256
.L3425:
	.align	2
.L3424:
	.word	.LANCHOR4
	.word	.LC158
	.word	.LANCHOR0
.L3324:
	cbnz	r3, .L3325
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r6, #1128]
	adds	r3, r3, #1
	strh	r3, [r6, #1128]	@ movhi
	b	.L3318
.L3325:
	movs	r3, #0
	strb	r3, [r9, #564]
	ldrb	r3, [r7, #152]	@ zero_extendqisi2
	cbz	r3, .L3326
	mov	r0, r2
	bl	ftl_get_blk_mode
	strb	r0, [r9, #564]
.L3326:
	ldr	r0, .L3426
	bl	make_superblock
	ldr	r2, .L3426+4
	movs	r3, #0
	ldrh	r0, [r9, #556]
	ldr	r1, [r9, #300]
	strh	r3, [r2, #1482]	@ movhi
	ldrh	r1, [r1, r0, lsl #1]
	strh	r3, [r9, #558]	@ movhi
	strb	r3, [r9, #562]
	strh	r1, [r2, #1484]	@ movhi
.L3317:
	cmp	r8, #1
	bne	.L3327
	bl	FtlReadRefresh
.L3327:
	movs	r3, #1
	str	r3, [r9, #3280]
	ldrh	r3, [r7, #2390]
	str	r3, [sp, #16]
	ldrb	r3, [r7, #152]	@ zero_extendqisi2
	cbz	r3, .L3328
	ldrb	r3, [r9, #564]	@ zero_extendqisi2
	cmp	r3, #1
	itt	eq
	ldrheq	r3, [r7, #2392]
	streq	r3, [sp, #16]
.L3328:
	ldrh	r3, [r9, #558]
	ldr	r1, [sp, #16]
	adds	r2, r3, r5
	cmp	r2, r1
	itt	gt
	movgt	r2, r1
	subgt	r5, r2, r3
	mov	r3, #0
	it	gt
	uxthgt	r5, r5
.L3419:
	str	r3, [sp, #20]
	ldrh	r3, [sp, #20]
	ldr	r6, .L3426+8
	cmp	r5, r3
	bls	.L3337
	ldr	r3, [sp, #20]
	movw	fp, #65535
	ldrh	r10, [r6, #558]
	mov	ip, #36
	ldrh	r9, [r7, #2324]
	ldr	r0, [r6, #1152]
	add	r10, r10, r3
	addw	r3, r6, #570
	str	r3, [sp, #24]
	movs	r3, #0
	str	r3, [sp, #12]
	b	.L3338
.L3332:
	ldr	r1, [sp, #24]
	ldrh	r2, [r1, #2]!
	cmp	r2, fp
	str	r1, [sp, #24]
	beq	.L3331
	ldr	r1, [sp, #12]
	orr	r2, r10, r2, lsl #10
	mla	lr, ip, r1, r0
	str	r2, [lr, #4]
	mov	r2, r1
	adds	r2, r2, #1
	uxth	r2, r2
	str	r2, [sp, #12]
.L3331:
	adds	r3, r3, #1
.L3338:
	uxth	r2, r3
	cmp	r9, r2
	bhi	.L3332
	ldrb	r2, [r6, #564]	@ zero_extendqisi2
	mov	fp, #0
	ldr	r6, .L3426+8
	ldr	r1, [sp, #12]
	bl	FlashReadPages
.L3333:
	ldr	r2, [sp, #12]
	uxth	r3, fp
	cmp	r2, r3
	bhi	.L3336
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	b	.L3419
.L3336:
	movs	r3, #36
	ldr	r2, [r6, #1152]
	mul	r9, r3, fp
	add	r1, r2, r9
	ldr	r2, [r2, r9]
	adds	r2, r2, #1
	beq	.L3334
	ldr	r10, [r1, #12]
	movw	r2, #61589
	ldrh	r1, [r10]
	cmp	r1, r2
	bne	.L3334
	movs	r2, #0
	add	r1, sp, #32
	ldr	r0, [r10, #8]
	str	r3, [sp, #24]
	bl	log2phys
	ldr	r1, [r6, #1152]
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #24]
	add	r1, r1, r9
	ldr	r0, [r1, #4]
	bic	r2, r2, #-2147483648
	cmp	r2, r0
	bne	.L3334
	ldr	r2, .L3426+4
	ldr	r0, .L3426+4
	ldr	r1, [r1, #16]
	ldrh	r2, [r2, #1482]
	str	r3, [sp, #28]
	adds	r2, r2, #1
	strh	r2, [r0, #1482]	@ movhi
	ldr	r0, [r6, #1136]
	ldr	r2, [r6, #3308]
	mla	r2, r3, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #24]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [sp, #24]
	ldr	r1, [r6, #3308]
	ldr	r3, [sp, #28]
	str	r0, [r2, #4]
	ldr	r2, [r6, #1136]
	mla	r3, r3, r2, r1
	ldr	r2, [r6, #1152]
	add	r2, r2, r9
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	movs	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #32]
	str	r3, [r10, #12]
	ldrh	r3, [r6, #416]
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r6, #512]
	ldr	r0, [r6, #1152]
	str	r3, [r10, #4]
	ldr	r3, [r6, #1136]
	add	r0, r0, r9
	adds	r3, r3, #1
	str	r3, [r6, #1136]
	bl	FtlGcBufAlloc
	ldrb	r3, [r7, #152]	@ zero_extendqisi2
	cbnz	r3, .L3335
	ldrb	r2, [r6, #423]	@ zero_extendqisi2
	ldr	r3, [r6, #1136]
	cmp	r2, r3
	beq	.L3335
	ldrh	r3, [r6, #420]
	cbnz	r3, .L3334
.L3335:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L3334
	ldr	r3, .L3426+8
	movs	r2, #0
	movw	r1, #65535
	str	r2, [r3, #3280]
	strh	r1, [r3, #556]	@ movhi
	strh	r2, [r3, #558]	@ movhi
	b	.L3421
.L3334:
	add	fp, fp, #1
	b	.L3333
.L3337:
	ldrh	r3, [r6, #558]
	add	r5, r5, r3
	ldr	r3, [sp, #16]
	uxth	r5, r5
	cmp	r3, r5
	strh	r5, [r6, #558]	@ movhi
	bhi	.L3339
	ldr	r3, [r6, #1136]
	ldr	r5, .L3426+4
	cbz	r3, .L3340
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L3340
	movs	r3, #0
	ldrh	r0, [r5, #1480]
	str	r3, [r6, #3280]
	b	.L3256
.L3340:
	ldrh	r5, [r5, #1482]
	cbnz	r5, .L3341
	ldrh	r2, [r6, #556]
	ldr	r3, [r6, #300]
	ldrh	r3, [r3, r2, lsl #1]
	cbz	r3, .L3341
.L3342:
	ldr	r3, [r7, #2452]
	cmp	r5, r3
	bcs	.L3347
	movs	r2, #0
	add	r1, sp, #36
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #36]
	adds	r3, r0, #1
	beq	.L3343
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6, #556]
	cmp	r3, r0
	bne	.L3343
.L3347:
	ldr	r3, [r7, #2452]
	cmp	r5, r3
	bcc	.L3341
	ldrh	r2, [r6, #556]
	movs	r1, #0
	ldr	r3, [r6, #300]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r6, #556]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3341:
	movw	r3, #65535
	strh	r3, [r6, #556]	@ movhi
.L3339:
	ldrh	r3, [r6, #316]
	cmp	r3, #2
	bhi	.L3348
	ldrh	r5, [r7, #2390]
	b	.L3349
.L3343:
	adds	r5, r5, #1
	b	.L3342
.L3348:
	movs	r2, #0
	str	r2, [r6, #3280]
	ldr	r2, .L3426+4
	ldrh	r0, [r2, #1480]
	cmp	r0, #0
	bne	.L3256
	adds	r0, r3, #1
	b	.L3256
.L3355:
	movs	r0, #0
	b	.L3256
.L3274:
	cmp	r8, #0
	bne	.L3354
	b	.L3353
.L3273:
	cmp	r8, #0
	bne	.L3305
	b	.L3353
.L3427:
	.align	2
.L3426:
	.word	.LANCHOR2+556
	.word	.LANCHOR4
	.word	.LANCHOR2
	.fnend
	.size	ftl_do_gc, .-ftl_do_gc
	.align	1
	.global	FtlCacheWriteBack
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L3470
	ldr	r9, [r5, #228]
	cmp	r9, #0
	bne	.L3430
	ldr	r4, .L3470+4
	ldr	r1, [r4, #2444]
	cbz	r1, .L3430
	ldr	r3, .L3470+8
	ldr	r6, [r3, #1488]
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cbz	r3, .L3455
	ldrb	r8, [r6, #8]	@ zero_extendqisi2
	add	r0, r8, #-1
	rsbs	r8, r0, #0
	adc	r8, r8, r0
.L3432:
	movs	r7, #0
	mov	r10, #36
	ldrb	r3, [r6, #9]	@ zero_extendqisi2
	mov	r2, r8
	ldr	r0, [r4, #2448]
	bl	FlashProgPages
.L3433:
	ldr	r3, [r4, #2444]
	cmp	r7, r3
	bcc	.L3440
.L3452:
	movs	r3, #0
	str	r3, [r4, #2444]
.L3430:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3455:
	mov	r8, r9
	b	.L3432
.L3440:
	mul	fp, r10, r7
	ldr	r2, [r4, #2448]
	add	r0, r2, fp
	ldr	r2, [r2, fp]
	adds	r2, r2, #1
	bne	.L3434
	mov	r10, #0
.L3435:
	ldr	r3, [r4, #2444]
	cmp	r9, r3
	bcc	.L3450
	movw	r6, #16386
.L3453:
	ldrh	r3, [r5, #1182]
	cmp	r3, #0
	beq	.L3452
	movs	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	subs	r6, r6, #1
	bne	.L3453
	b	.L3452
.L3434:
	ldr	r2, [r0, #4]
	cmp	r8, #0
	bne	.L3436
.L3468:
	str	r2, [sp, #4]
	add	r1, sp, #4
	movs	r2, #1
	ldr	r0, [r0, #16]
	bl	log2phys
	ldr	r2, [r4, #2448]
	add	r3, r2, fp
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	adds	r2, r0, #1
	beq	.L3438
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #300]
	mov	fp, r0
	ldrh	r2, [r2, r0, lsl #1]
	cbnz	r2, .L3439
	mov	r1, r0
	ldr	r0, .L3470+12
	bl	printk
.L3439:
	mov	r0, fp
	bl	decrement_vpc_count
.L3438:
	adds	r7, r7, #1
	b	.L3433
.L3436:
	orr	r2, r2, #-2147483648
	b	.L3468
.L3450:
	movs	r7, #36
	ldr	r3, [r4, #2448]
	mul	r7, r7, r9
	mov	fp, #1
	mov	r2, #-1
	str	r2, [r3, r7]
.L3441:
	ldr	r2, [r4, #2448]
	adds	r3, r2, r7
	ldr	r2, [r2, r7]
	ldr	r0, [r3, #4]
	adds	r2, r2, #1
	beq	.L3445
	cmp	r8, #0
	bne	.L3446
.L3469:
	str	r0, [sp, #4]
	movs	r2, #1
	ldr	r0, [r3, #16]
	add	r1, sp, #4
	bl	log2phys
	ldr	r3, [r4, #2448]
	add	r7, r7, r3
	ldr	r3, [r7, #12]
	ldr	r0, [r3, #12]
	adds	r3, r0, #1
	beq	.L3448
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #300]
	mov	r7, r0
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L3449
	mov	r1, r0
	ldr	r0, .L3470+12
	bl	printk
.L3449:
	mov	r0, r7
	bl	decrement_vpc_count
.L3448:
	add	r9, r9, #1
	b	.L3435
.L3445:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6]
	cmp	r3, r0
	bne	.L3442
	ldr	r1, [r5, #300]
	ldrh	r0, [r6, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r4, #2390]
	strb	r10, [r6, #6]
	strh	r10, [r6, #4]	@ movhi
	strh	r3, [r6, #2]	@ movhi
.L3442:
	ldrh	r3, [r6, #4]
	cbnz	r3, .L3443
	mov	r0, r6
	bl	allocate_new_data_superblock
.L3443:
	ldr	r3, [r5, #700]
	adds	r3, r3, #1
	str	r3, [r5, #700]
	ldr	r3, [r4, #2448]
	add	r3, r3, r7
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r6
	bl	get_new_active_ppa
	ldr	r3, [r4, #2448]
	mov	r2, r0
	str	r0, [sp, #4]
	movs	r1, #1
	adds	r0, r3, r7
	str	r2, [r0, #4]
	mov	r2, r8
	ldrb	r3, [r6, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #2448]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	it	eq
	streq	fp, [r5, #228]
	ldr	r3, [r5, #228]
	cmp	r3, #0
	beq	.L3441
	b	.L3430
.L3446:
	orr	r0, r0, #-2147483648
	b	.L3469
.L3471:
	.align	2
.L3470:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC159
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	1
	.global	FtlSysFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3474
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r3, #228]
	cbnz	r3, .L3473
	ldr	r3, .L3474+4
	ldr	r4, [r3, #500]
	cmp	r4, #1
	bne	.L3473
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3473:
	movs	r0, #0
	pop	{r4, pc}
.L3475:
	.align	2
.L3474:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.fnend
	.size	FtlSysFlush, .-FtlSysFlush
	.align	1
	.global	FtlDeInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlDeInit, %function
FtlDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	ldr	r3, .L3478
	ldr	r3, [r3, #500]
	cmp	r3, #1
	bne	.L3477
	bl	FtlSysFlush
.L3477:
	movs	r0, #0
	pop	{r3, pc}
.L3479:
	.align	2
.L3478:
	.word	.LANCHOR1
	.fnend
	.size	FtlDeInit, .-FtlDeInit
	.align	1
	.global	ftl_deinit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	movs	r1, #0
	ldr	r0, .L3482
	bl	printk
	pop	{r3, lr}
	b	ftl_deinit
.L3483:
	.align	2
.L3482:
	.word	.LC160
	.fnend
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	1
	.global	ftl_cache_flush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_discard, %function
ftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r6, r0
	ldr	r7, .L3504
	mov	r4, r1
	ldr	r3, [r7, #2432]
	cmp	r3, r0
	bls	.L3495
	cmp	r3, r1
	bcc	.L3495
	adds	r2, r0, r1
	cmp	r3, r2
	bcc	.L3495
	cmp	r1, #31
	bls	.L3497
	ldr	r3, .L3504+4
	ldr	r2, [r3, #228]
	mov	r8, r3
	cbnz	r2, .L3497
	bl	FtlCacheWriteBack
	ldrh	r5, [r7, #2396]
	mov	r0, r6
	mov	r1, r5
	bl	__aeabi_uidiv
	smulbb	r3, r0, r5
	mov	r9, r0
	subs	r6, r6, r3
	uxth	r6, r6
	cbz	r6, .L3488
	subs	r5, r5, r6
	add	r9, r0, #1
	cmp	r5, r4
	it	cs
	movcs	r5, r4
	uxth	r5, r5
	subs	r4, r4, r5
.L3488:
	ldr	r5, .L3504+8
	mov	r3, #-1
	str	r3, [sp, #4]
.L3489:
	ldrh	r3, [r7, #2396]
	cmp	r4, r3
	bcs	.L3491
	ldr	r3, .L3504+8
	ldr	r2, [r3, #1492]
	cmp	r2, #32
	bls	.L3497
	movs	r2, #0
	str	r2, [r3, #1492]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3497:
	movs	r0, #0
	b	.L3486
.L3491:
	movs	r2, #0
	mov	r1, sp
	mov	r0, r9
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L3490
	ldr	r3, [r5, #1492]
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r9
	adds	r3, r3, #1
	str	r3, [r5, #1492]
	ldr	r3, [r8, #480]
	adds	r3, r3, #1
	str	r3, [r8, #480]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3490:
	ldrh	r3, [r7, #2396]
	add	r9, r9, #1
	subs	r4, r4, r3
	b	.L3489
.L3495:
	mov	r0, #-1
.L3486:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3505:
	.align	2
.L3504:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	ftl_discard, .-ftl_discard
	.align	1
	.global	FtlDiscard
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_read, %function
ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r3
	ldr	r3, .L3548
	.pad #84
	sub	sp, sp, #84
	mov	r5, r1
	str	r2, [sp, #44]
	ldr	r3, [r3, #500]
	cmp	r3, #1
	bne	.L3532
	cmp	r0, #16
	bne	.L3509
	mov	r2, r9
	ldr	r1, [sp, #44]
	add	r0, r5, #256
	bl	FtlVendorPartRead
	mov	r8, r0
.L3507:
	mov	r0, r8
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3509:
	ldr	r2, .L3548+4
	ldr	r3, [r2, #2432]
	cmp	r1, r3
	bcs	.L3532
	ldr	r1, [sp, #44]
	cmp	r1, r3
	bhi	.L3532
	adds	r1, r5, r1
	cmp	r3, r1
	str	r1, [sp, #48]
	bcc	.L3532
	ldrh	r4, [r2, #2396]
	mov	r0, r5
	mov	r1, r4
	bl	__aeabi_uidiv
	ldr	r3, [sp, #48]
	mov	r1, r4
	str	r0, [sp, #36]
	subs	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #36]
	ldr	r1, [sp, #44]
	str	r0, [sp, #40]
	rsb	r3, r3, #1
	add	r3, r3, r0
	str	r3, [sp, #32]
	ldr	r3, .L3548+8
	ldr	r2, [r3, #504]
	add	r2, r2, r1
	ldr	r1, [sp, #32]
	str	r2, [r3, #504]
	ldr	r2, [r3, #476]
	add	r2, r2, r1
	mov	r1, r0
	ldr	r0, [sp, #36]
	str	r2, [r3, #476]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3510
	bl	FtlCacheWriteBack
.L3510:
	ldr	r6, [sp, #36]
	movs	r3, #0
	ldr	r4, .L3548+8
	mov	r7, r3
	mov	r8, r3
	str	r3, [sp, #28]
	str	r3, [sp, #52]
.L3511:
	ldr	r3, [sp, #32]
	cbnz	r3, .L3528
	ldr	r3, .L3548+8
	ldrh	r3, [r3, #1182]
	cmp	r3, #0
	beq	.L3507
	movs	r1, #1
	ldr	r0, [sp, #32]
	bl	ftl_do_gc
	b	.L3507
.L3528:
	movs	r2, #0
	add	r1, sp, #76
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp, #76]
	adds	r2, r3, #1
	bne	.L3512
	mov	r10, #0
.L3513:
	ldr	r3, .L3548+4
	ldrh	r0, [r3, #2396]
	cmp	r10, r0
	bcc	.L3515
.L3516:
	ldr	r3, [sp, #32]
	adds	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #32]
	beq	.L3520
	ldr	r3, .L3548+4
	ldrh	r3, [r3, #2324]
	cmp	r7, r3, lsl #3
	bne	.L3511
.L3520:
	cmp	r7, #0
	beq	.L3511
	movs	r2, #0
	mov	r1, r7
	ldr	r0, [r4, #3304]
	mov	fp, #0
	bl	FlashReadPages
	ldr	r3, [sp, #28]
	lsls	r3, r3, #9
	str	r3, [sp, #68]
	ldr	r3, [sp, #56]
	lsls	r3, r3, #9
	str	r3, [sp, #60]
	ldr	r3, [sp, #52]
	lsls	r3, r3, #9
	str	r3, [sp, #64]
.L3527:
	mov	r10, #36
	ldr	r3, [r4, #3304]
	mul	r10, r10, fp
	ldr	r1, [sp, #36]
	add	r3, r3, r10
	ldr	r2, [r3, #16]
	cmp	r1, r2
	bne	.L3522
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3332]
	cmp	r1, r3
	bne	.L3523
	ldr	r3, [sp, #60]
	mov	r0, r9
	ldr	r2, [sp, #64]
	add	r1, r1, r3
.L3547:
	bl	ftl_memcpy
.L3523:
	ldr	r3, [r4, #3304]
	ldr	r2, [r3, r10]
	add	r1, r3, r10
	adds	r3, r2, #1
	itttt	eq
	ldreq	r3, [r4, #676]
	moveq	r8, r2
	addeq	r3, r3, #1
	streq	r3, [r4, #676]
	ldr	r3, [r1, #12]
	ldr	r2, [r1, #16]
	ldr	r3, [r3, #8]
	cmp	r2, r3
	beq	.L3525
	ldr	r3, [r4, #676]
	adds	r3, r3, #1
	str	r3, [r4, #676]
	ldr	r2, [r1, #8]
	ldr	r3, [r1, #12]
	ldr	r0, [r2, #4]
	str	r0, [sp, #16]
	ldr	r2, [r2]
	ldr	r0, .L3548+12
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r2, [r1, #4]
	ldr	r3, [r3]
	ldr	r1, [r1, #16]
	bl	printk
.L3525:
	ldr	r3, [r4, #3304]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	cmp	r3, #256
	bne	.L3526
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3526:
	add	fp, fp, #1
	cmp	r7, fp
	bne	.L3527
	movs	r7, #0
	b	.L3511
.L3515:
	mla	r0, r0, r6, r10
	cmp	r5, r0
	bhi	.L3514
	ldr	r3, [sp, #48]
	cmp	r3, r0
	bls	.L3514
	subs	r0, r0, r5
	mov	r2, #512
	movs	r1, #0
	add	r0, r9, r0, lsl #9
	bl	ftl_memset
.L3514:
	add	r10, r10, #1
	b	.L3513
.L3512:
	ldr	r2, [r4, #3304]
	mov	r10, #36
	mla	r10, r10, r7, r2
	str	r3, [r10, #4]
	ldr	r3, [sp, #36]
	cmp	r6, r3
	bne	.L3517
	ldr	r3, [r4, #3332]
	mov	r0, r5
	str	r3, [r10, #8]
	ldr	r3, .L3548+4
	ldrh	fp, [r3, #2396]
	mov	r1, fp
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #44]
	sub	r3, fp, r1
	str	r1, [sp, #56]
	cmp	r3, r2
	it	cs
	movcs	r3, r2
	cmp	r3, fp
	str	r3, [sp, #52]
	bne	.L3518
	str	r9, [r10, #8]
.L3518:
	ldr	r3, .L3548+4
	ldr	r2, [r4, #3344]
	str	r6, [r10, #16]
	ldrh	r3, [r3, #2402]
	muls	r3, r7, r3
	adds	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r3, r2
	str	r3, [r10, #12]
	b	.L3516
.L3517:
	ldr	r3, [sp, #40]
	cmp	r6, r3
	bne	.L3519
	ldr	r3, [r4, #3336]
	ldr	r1, [sp, #48]
	str	r3, [r10, #8]
	ldr	r3, .L3548+4
	ldrh	r2, [r3, #2396]
	mul	r3, r2, r6
	subs	r1, r1, r3
	cmp	r2, r1
	str	r1, [sp, #28]
	bne	.L3518
.L3546:
	subs	r3, r3, r5
	add	r3, r9, r3, lsl #9
	str	r3, [r10, #8]
	b	.L3518
.L3519:
	ldr	r3, .L3548+4
	ldrh	r3, [r3, #2396]
	muls	r3, r6, r3
	b	.L3546
.L3522:
	ldr	r1, [sp, #40]
	cmp	r1, r2
	bne	.L3523
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3336]
	cmp	r1, r3
	bne	.L3523
	ldr	r3, .L3548+4
	ldr	r2, [sp, #68]
	ldrh	r0, [r3, #2396]
	ldr	r3, [sp, #40]
	muls	r0, r3, r0
	subs	r0, r0, r5
	add	r0, r9, r0, lsl #9
	b	.L3547
.L3532:
	mov	r8, #-1
	b	.L3507
.L3549:
	.align	2
.L3548:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC148
	.fnend
	.size	ftl_read, .-ftl_read
	.align	1
	.global	ftl_vendor_read
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_vendor_read, %function
ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	movs	r0, #16
	b	ftl_read
	.fnend
	.size	ftl_vendor_read, .-ftl_vendor_read
	.align	1
	.global	FlashBootVendorRead
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	bl	rknand_device_lock
	ldr	r3, .L3554
	ldr	r3, [r3, #500]
	cmp	r3, #1
	bne	.L3553
	mov	r0, r4
	mov	r2, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
.L3552:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3553:
	mov	r4, #-1
	b	.L3552
.L3555:
	.align	2
.L3554:
	.word	.LANCHOR1
	.fnend
	.size	FlashBootVendorRead, .-FlashBootVendorRead
	.align	1
	.global	ftl_sys_read
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_sys_read, %function
ftl_sys_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	add	r1, r0, #256
	movs	r0, #16
	b	ftl_read
	.fnend
	.size	ftl_sys_read, .-ftl_sys_read
	.align	1
	.global	StorageSysDataLoad
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r1
	mov	r5, r0
	mov	r2, #512
	movs	r1, #0
	mov	r0, r4
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r3, #-1
	ldr	r7, .L3575
	ldr	r2, .L3575+4
	ldr	r6, .L3575+8
	ldr	r4, .L3575+12
	ldr	r1, .L3575+16
	str	r3, [r7, #500]
	movs	r3, #0
	ldr	r0, .L3575+20
	str	r3, [r2, #1496]
	str	r3, [r4, #228]
	bl	printk
	add	r0, r6, #124
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r6, #2328]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L3560
	ldr	r1, .L3575+24
	ldr	r0, .L3575+28
.L3574:
	bl	printk
.L3561:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L3560:
	bl	FtlSysBlkInit
	mov	r5, r0
	cbz	r0, .L3562
	ldr	r1, .L3575+24
	ldr	r0, .L3575+32
	b	.L3574
.L3562:
	movs	r1, #1
	str	r1, [r7, #500]
	bl	ftl_do_gc
	ldrh	r7, [r4, #316]
	cmp	r7, #15
	bhi	.L3563
	movw	r6, #65535
.L3566:
	ldrh	r3, [r4, #556]
	cmp	r3, r6
	bne	.L3564
	ldrh	r3, [r4, #1174]
	cmp	r3, r6
	bne	.L3564
	and	r0, r5, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3564:
	movs	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	movs	r1, #1
	movs	r0, #0
	bl	ftl_do_gc
	ldrh	r2, [r4, #316]
	adds	r3, r7, #2
	cmp	r2, r3
	bhi	.L3561
	adds	r5, r5, #1
	cmp	r5, #4096
	bne	.L3566
	b	.L3561
.L3563:
	ldrb	r3, [r6, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3561
	movs	r4, #128
.L3568:
	movs	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3568
	b	.L3561
.L3576:
	.align	2
.L3575:
	.word	.LANCHOR1
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC76
	.word	.LC77
	.word	.LANCHOR3+224
	.word	.LC161
	.word	.LC162
	.fnend
	.size	FtlInit, .-FtlInit
	.align	1
	.global	rk_ftl_init
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_init, %function
rk_ftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r0, #2048
	ldr	r4, .L3581
	movs	r5, #0
	bl	ftl_dma32_malloc
	add	r1, r4, #1504
	str	r0, [r4, #1500]
	addw	r0, r4, #1180
	str	r5, [r4, #1184]
	str	r5, [r4, #1180]
	str	r5, [r4, #1504]
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #1180]
	cbz	r3, .L3580
	bl	rk_nandc_irq_init
	mov	r3, #2048
	mov	r2, r5
	mov	r1, r5
	ldr	r0, [r4, #1500]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #1180]
	bl	FlashInit
	mov	r4, r0
	cbnz	r0, .L3579
	bl	FtlInit
.L3579:
	mov	r1, r4
	ldr	r0, .L3581+4
	bl	printk
.L3577:
	mov	r0, r4
	pop	{r3, r4, r5, pc}
.L3580:
	mov	r4, #-1
	b	.L3577
.L3582:
	.align	2
.L3581:
	.word	.LANCHOR4
	.word	.LC163
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	1
	.global	ftl_fix_nand_power_lost_error
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3597
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #48
	sub	sp, sp, #48
	mov	r8, r3
	ldrb	r2, [r3, #152]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3583
	ldr	r4, .L3597+4
	movw	r5, #4097
	ldr	r7, .L3597+8
	ldr	r0, .L3597+12
	ldr	r3, [r4, #300]
	ldrh	r6, [r7, #1342]
	ldrh	r2, [r3, r6, lsl #1]
	mov	r1, r6
	bl	printk
	ldrh	r0, [r4, #320]
	lsl	r9, r6, #1
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4, #368]
	bl	FtlGcRefreshOpenBlock
	add	r0, r4, #320
	bl	allocate_new_data_superblock
	add	r0, r4, #368
	bl	allocate_new_data_superblock
.L3585:
	subs	r5, r5, #1
	beq	.L3589
	movs	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	ldr	r3, [r4, #300]
	ldrh	r3, [r3, r9]
	cmp	r3, #0
	bne	.L3585
.L3589:
	ldr	r3, [r4, #300]
	mov	r1, r6
	ldr	r0, .L3597+12
	ldrh	r2, [r3, r6, lsl #1]
	bl	printk
	ldr	r3, [r4, #300]
	ldrh	r5, [r3, r6, lsl #1]
	cbnz	r5, .L3587
	add	r0, sp, #48
	movw	r9, #65535
	strh	r6, [r0, #-48]!	@ movhi
	mov	r10, #36
	bl	make_superblock
	ldrh	lr, [r8, #2324]
	add	r0, sp, #14
	ldr	r8, [r4, #232]
	mov	r2, r5
	mov	ip, r5
.L3590:
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L3592
	ldr	r3, [r4, #300]
	mov	r1, r6
	ldr	r0, .L3597+16
	ldrh	r2, [r3, r6, lsl #1]
	bl	printk
	mov	r2, r5
	movs	r1, #0
	ldr	r0, [r4, #232]
	bl	FlashEraseBlocks
	mov	r2, r5
	movs	r1, #1
	ldr	r0, [r4, #232]
	bl	FlashEraseBlocks
.L3587:
	movw	r3, #65535
	strh	r3, [r7, #1342]	@ movhi
.L3583:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3592:
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L3591
	mla	r1, r10, r5, r8
	adds	r5, r5, #1
	lsls	r3, r3, #10
	uxth	r5, r5
	str	r3, [r1, #4]
	str	ip, [r1, #8]
	str	ip, [r1, #12]
.L3591:
	adds	r2, r2, #1
	b	.L3590
.L3598:
	.align	2
.L3597:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC164
	.word	.LC165
	.fnend
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	1
	.global	rk_ftl_garbage_collect
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_write, %function
ftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r3
	ldr	r3, .L3672
	mov	r9, r2
	.pad #84
	sub	sp, sp, #84
	mov	r7, r1
	ldr	r2, [r3, #228]
	cmp	r2, #0
	bne	.L3642
	ldr	r1, .L3672+4
	ldr	r1, [r1, #500]
	cmp	r1, #1
	bne	.L3643
	cmp	r0, #16
	bne	.L3602
	mov	r2, r10
	mov	r1, r9
	add	r0, r7, #256
	bl	FtlVendorPartWrite
.L3600:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3602:
	ldr	fp, .L3672+16
	str	r3, [sp, #4]
	ldr	r2, [fp, #2432]
	cmp	r7, r2
	bcs	.L3646
	cmp	r9, r2
	bhi	.L3646
	add	r5, r7, r9
	cmp	r2, r5
	bcc	.L3646
	ldrh	r4, [fp, #2396]
	mov	r2, #2048
	ldr	r6, .L3672+8
	mov	r0, r7
	mov	r1, r4
	str	r2, [r6, #1508]
	bl	__aeabi_uidiv
	mov	r1, r4
	str	r0, [sp]
	subs	r0, r5, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	cmp	r9, r4, lsl #1
	ldr	r2, [sp]
	ldr	r1, [fp, #2444]
	str	r0, [sp, #12]
	sub	r5, r0, r2
	ldr	r2, [r3, #484]
	add	r8, r5, #1
	add	r2, r2, r8
	str	r2, [r3, #484]
	ldr	r2, [r3, #500]
	add	r2, r2, r9
	str	r2, [r3, #500]
	ite	cs
	movcs	r2, #1
	movcc	r2, #0
	str	r2, [sp, #16]
	cmp	r1, #0
	beq	.L3647
	movs	r2, #36
	muls	r2, r1, r2
	ldr	r1, [fp, #2448]
	subs	r2, r2, #36
	add	fp, r1, r2
	ldr	r1, [sp]
	ldr	r2, [fp, #16]
	cmp	r1, r2
	bne	.L3648
	ldr	r2, [r3, #488]
	mov	r1, r4
	mov	r0, r7
	adds	r2, r2, #1
	str	r2, [r3, #488]
	ldr	r3, [r6, #1512]
	adds	r3, r3, #1
	str	r3, [r6, #1512]
	bl	__aeabi_uidivmod
	subs	r4, r4, r1
	ldr	r0, [fp, #8]
	cmp	r4, r9
	mov	r3, r1
	it	cs
	movcs	r4, r9
	mov	r1, r10
	lsl	r8, r4, #9
	add	r0, r0, r3, lsl #9
	mov	r2, r8
	bl	ftl_memcpy
	cbnz	r5, .L3606
	ldr	r3, [r6, #1512]
	cmp	r3, #2
	bgt	.L3606
.L3642:
	movs	r0, #0
	b	.L3600
.L3606:
	add	r3, r10, r8
	sub	r9, r9, r4
	str	r3, [sp, #8]
	add	r7, r7, r4
	ldr	r3, [sp]
	mov	r8, r5
	adds	r3, r3, #1
	str	r3, [sp]
.L3605:
	movs	r3, #0
	str	r3, [r6, #1512]
.L3604:
	ldr	r1, [sp, #12]
	ldr	r0, [sp]
	bl	FtlCacheMetchLpa
	cbz	r0, .L3607
	bl	FtlCacheWriteBack
.L3607:
	ldr	r5, .L3672+12
	ldr	fp, .L3672+16
	str	r5, [r6, #1488]
	ldr	r6, [sp]
.L3608:
	ldr	r4, .L3672
	cmp	r8, #0
	bne	.L3637
	ldr	r3, [sp, #12]
	mov	r0, r8
	ldr	r2, [sp]
	subs	r1, r3, r2
	bl	ftl_do_gc
	ldrh	r3, [r4, #316]
	cmp	r3, #5
	bls	.L3638
	cmp	r3, #31
	bhi	.L3642
	ldr	r3, .L3672+16
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3642
.L3638:
	movw	r5, #65535
	movs	r6, #128
.L3641:
	ldrh	r3, [r4, #556]
	cmp	r3, r5
	bne	.L3640
	ldrh	r3, [r4, #1174]
	cmp	r3, r5
	bne	.L3640
	ldrh	r3, [r4, #1176]
	cmp	r3, r5
	bne	.L3640
	and	r0, r8, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3640:
	movs	r1, #1
	strh	r6, [r4, #1122]	@ movhi
	mov	r0, r1
	strh	r6, [r4, #1120]	@ movhi
	bl	ftl_do_gc
	movs	r1, #1
	movs	r0, #0
	bl	ftl_do_gc
	ldr	r3, [r4, #228]
	cmp	r3, #0
	bne	.L3642
	ldrh	r3, [r4, #316]
	cmp	r3, #2
	bhi	.L3642
	add	r8, r8, #1
	cmp	r8, #256
	bne	.L3641
	b	.L3642
.L3648:
	str	r10, [sp, #8]
	b	.L3605
.L3647:
	str	r10, [sp, #8]
	b	.L3604
.L3637:
	ldrh	r1, [r5, #4]
	cbnz	r1, .L3609
	add	r2, r4, #320
	ldr	r10, .L3672+4
	cmp	r5, r2
	bne	.L3610
	ldrh	r5, [r4, #372]
	cbnz	r5, .L3611
	add	r0, r4, #368
	bl	allocate_new_data_superblock
	str	r5, [r10, #3448]
.L3611:
	ldr	r5, .L3672+12
	ldr	r0, .L3672+12
	bl	allocate_new_data_superblock
	ldr	r1, [r10, #3448]
	add	r2, r5, #48
	cmp	r1, #0
	it	ne
	movne	r5, r2
.L3612:
	ldrh	r2, [r5, #4]
	cbnz	r2, .L3613
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3613:
	ldr	r2, .L3672+8
	str	r5, [r2, #1488]
.L3609:
	ldr	r1, [fp, #2444]
	ldr	r2, [r4, #3300]
	ldr	r10, .L3672
	subs	r2, r2, r1
	ldrh	r1, [r5, #4]
	cmp	r2, r8
	it	cs
	movcs	r2, r8
	cmp	r1, r2
	mov	r3, r1
	it	cs
	movcs	r3, r2
	str	r3, [sp, #36]
	movs	r3, #0
	str	r3, [sp, #20]
.L3614:
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #36]
	cmp	r3, r2
	bne	.L3633
.L3615:
	ldr	r2, .L3672
	ldr	r1, [fp, #2444]
	ldr	r3, [sp, #20]
	ldr	r2, [r2, #3300]
	sub	r8, r8, r3
	cmp	r1, r2
	bcs	.L3634
	ldr	r3, [sp, #16]
	cmp	r3, #0
	bne	.L3634
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	beq	.L3634
.L3636:
	movs	r3, #0
	str	r3, [sp, #16]
	b	.L3608
.L3610:
	str	r1, [r10, #3448]
	ldrh	r1, [r4, #324]
	cbnz	r1, .L3650
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L3612
.L3650:
	mov	r5, r2
	b	.L3613
.L3673:
	.align	2
.L3672:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR4
	.word	.LANCHOR2+320
	.word	.LANCHOR0
.L3633:
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	beq	.L3615
	ldr	r3, [sp, #16]
	cbz	r3, .L3616
	ldr	r3, [sp, #12]
	cmp	r3, r6
	bne	.L3616
	ldr	r3, [sp, #20]
	cbz	r3, .L3616
	ldrh	r1, [fp, #2396]
	add	r2, r7, r9
	ldr	r3, [sp, #12]
	mls	r2, r1, r3, r2
	cmp	r1, r2
	bne	.L3615
.L3616:
	movs	r2, #0
	add	r1, sp, #40
	mov	r0, r6
	movs	r4, #36
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r1, [fp, #2444]
	ldr	r2, [fp, #2448]
	mla	ip, r4, r1, r2
	ldrh	r2, [fp, #2402]
	str	r0, [ip, #4]
	mul	r0, r2, r1
	str	r6, [ip, #16]
	bic	r3, r0, #3
	str	r3, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r3, [r10, #3348]
	str	r3, [sp, #32]
	add	r3, r3, r0
	ldrh	r0, [fp, #2400]
	str	r3, [ip, #12]
	str	r3, [sp, #4]
	muls	r1, r0, r1
	ldr	r0, [r10, #3328]
	bic	r1, r1, #3
	add	r1, r1, r0
	mov	r0, r3
	str	r1, [ip, #8]
	movs	r1, #0
	bl	ftl_memset
	ldr	r3, [sp]
	cmp	r3, r6
	beq	.L3617
	ldr	r3, [sp, #12]
	cmp	r3, r6
	bne	.L3668
	ldrh	r3, [sp, #12]
	add	r4, r7, r9
	ldrh	r2, [fp, #2396]
	smulbb	r2, r2, r3
	movs	r3, #0
	str	r3, [sp, #24]
	subs	r4, r4, r2
	uxth	r4, r4
	b	.L3620
.L3617:
	ldrh	r4, [fp, #2396]
	mov	r0, r7
	mov	r1, r4
	bl	__aeabi_uidivmod
	subs	r4, r4, r1
	str	r1, [sp, #24]
	cmp	r4, r9
	it	cs
	movcs	r4, r9
.L3620:
	ldrh	r2, [fp, #2396]
	cmp	r2, r4
	bne	.L3621
	ldr	r3, [sp]
	ldr	r2, [fp, #2444]
	ldr	r0, [fp, #2448]
	cmp	r3, r6
	itte	ne
	mulne	r1, r4, r6
	ldrne	r3, [sp, #8]
	ldreq	r1, [sp, #8]
	mov	r4, #36
	itt	ne
	subne	r1, r1, r7
	addne	r1, r3, r1, lsl #9
	ldr	r3, [sp, #16]
	cbz	r3, .L3623
	mla	r2, r4, r2, r0
	str	r1, [r2, #8]
.L3624:
	ldr	r3, [sp, #32]
	movw	r2, #61589
	ldr	r1, [sp, #28]
	strh	r2, [r3, r1]	@ movhi
	ldr	r3, [sp, #4]
	ldr	r2, [r10, #512]
	str	r2, [r3, #4]
	adds	r2, r2, #1
	adds	r3, r2, #1
	ldr	r3, [sp, #4]
	it	eq
	moveq	r2, #0
	str	r2, [r10, #512]
	ldr	r2, [sp, #40]
	str	r6, [r3, #8]
	adds	r6, r6, #1
	str	r2, [r3, #12]
	ldrh	r2, [r5]
	strh	r2, [r3, #2]	@ movhi
	ldr	r3, [sp, #20]
	ldr	r2, [fp, #2444]
	adds	r3, r3, #1
	adds	r2, r2, #1
	str	r3, [sp, #20]
	str	r2, [fp, #2444]
	b	.L3614
.L3623:
	mla	r0, r4, r2, r0
	ldrh	r2, [fp, #2400]
.L3671:
	ldr	r0, [r0, #8]
	b	.L3669
.L3621:
	ldr	r2, [sp, #40]
	movs	r0, #36
	adds	r1, r2, #1
	beq	.L3625
	ldr	r1, [fp, #2448]
	str	r2, [sp, #48]
	ldr	r2, [fp, #2444]
	str	r6, [sp, #60]
	mla	r2, r0, r2, r1
	add	r0, sp, #44
	ldr	r1, [r2, #8]
	ldr	r2, [r2, #12]
	str	r1, [sp, #52]
	movs	r1, #1
	str	r2, [sp, #56]
	movs	r2, #0
	bl	FlashReadPages
	ldr	r2, [sp, #44]
	adds	r2, r2, #1
	bne	.L3626
	ldr	r2, [r10, #676]
	adds	r2, r2, #1
	str	r2, [r10, #676]
.L3628:
	ldr	r3, [sp]
	lsls	r2, r4, #9
	cmp	r3, r6
	bne	.L3629
	ldr	r0, [fp, #2448]
	movs	r4, #36
	ldr	r1, [fp, #2444]
	ldr	r3, [sp, #24]
	mla	r1, r4, r1, r0
	ldr	r0, [r1, #8]
	ldr	r1, [sp, #8]
	add	r0, r0, r3, lsl #9
.L3669:
	bl	ftl_memcpy
	b	.L3624
.L3626:
	ldr	r3, [sp, #4]
	ldr	r2, [r3, #8]
	cmp	r6, r2
	beq	.L3628
	ldr	r2, [r10, #676]
	ldr	r0, .L3674
	adds	r2, r2, #1
	str	r2, [r10, #676]
	mov	r2, r6
	ldr	r1, [r3, #8]
	bl	printk
	b	.L3628
.L3625:
	ldr	r1, [fp, #2444]
	ldr	r2, [fp, #2448]
	mla	r0, r0, r1, r2
	ldrh	r2, [fp, #2400]
	movs	r1, #0
	ldr	r0, [r0, #8]
	bl	ftl_memset
	b	.L3628
.L3629:
	ldrh	r1, [fp, #2396]
	movs	r4, #36
	ldr	r3, [fp, #2448]
	ldr	r0, [fp, #2444]
	muls	r1, r6, r1
	mla	r0, r4, r0, r3
	ldr	r3, [sp, #8]
	subs	r1, r1, r7
	add	r1, r3, r1, lsl #9
	b	.L3671
.L3668:
	ldr	r3, [sp, #16]
	cbz	r3, .L3630
	ldr	r2, [fp, #2444]
	ldr	r1, [fp, #2448]
	ldr	r3, [sp, #8]
	mla	r4, r4, r2, r1
	ldrh	r2, [fp, #2396]
	muls	r2, r6, r2
	subs	r2, r2, r7
	add	r2, r3, r2, lsl #9
	str	r2, [r4, #8]
	b	.L3624
.L3630:
	ldrh	r1, [fp, #2396]
	ldr	r2, [fp, #2444]
	ldr	r0, [fp, #2448]
	ldr	r3, [sp, #8]
	muls	r1, r6, r1
	mla	r4, r4, r2, r0
	ldrh	r2, [fp, #2400]
	subs	r1, r1, r7
	add	r1, r3, r1, lsl #9
	ldr	r0, [r4, #8]
	b	.L3669
.L3634:
	bl	FtlCacheWriteBack
	cmp	r8, #1
	mov	r2, #0
	str	r2, [fp, #2444]
	bhi	.L3608
	b	.L3636
.L3646:
	mov	r0, #-1
	b	.L3600
.L3643:
	mov	r0, r2
	b	.L3600
.L3675:
	.align	2
.L3674:
	.word	.LC166
	.fnend
	.size	ftl_write, .-ftl_write
	.align	1
	.global	ftl_vendor_write
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_vendor_write, %function
ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	movs	r0, #16
	b	ftl_write
	.fnend
	.size	ftl_vendor_write, .-ftl_vendor_write
	.align	1
	.global	FlashBootVendorWrite
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashBootVendorWrite, %function
FlashBootVendorWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	bl	rknand_device_lock
	ldr	r3, .L3680
	ldr	r3, [r3, #500]
	cmp	r3, #1
	bne	.L3679
	mov	r0, r4
	mov	r2, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
.L3678:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3679:
	mov	r4, #-1
	b	.L3678
.L3681:
	.align	2
.L3680:
	.word	.LANCHOR1
	.fnend
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
	.align	1
	.global	ftl_sys_write
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_sys_write, %function
ftl_sys_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	add	r1, r0, #256
	movs	r0, #16
	b	ftl_write
	.fnend
	.size	ftl_sys_write, .-ftl_sys_write
	.align	1
	.global	StorageSysDataStore
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r1
	mov	r4, r0
	bl	rknand_device_lock
	mov	r2, r5
	movs	r1, #1
	mov	r0, r4
	bl	ftl_sys_write
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r3, r4, r5, pc}
	.fnend
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	1
	.global	FtlDumpSysBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	lsl	r9, r0, #10
	ldr	r5, .L3691
	.pad #28
	sub	sp, sp, #28
	mov	r7, r0
	movs	r6, #0
	ldr	r4, .L3691+4
	ldr	r3, [r5, #3316]
	ldr	r8, .L3691+12
	ldr	fp, .L3691+16
	add	r10, r4, #1256
	str	r3, [r4, #1264]
	ldr	r3, [r5, #3340]
	str	r3, [r4, #1268]
.L3685:
	ldrh	r2, [r8, #2392]
	sxth	r3, r6
	cmp	r3, r2
	blt	.L3687
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3687:
	movs	r2, #1
	orr	r3, r3, r9
	mov	r1, r2
	mov	r0, r10
	str	r3, [r4, #1260]
	bl	FlashReadPages
	ldr	r2, [r4, #1264]
	mov	r1, r7
	ldr	r3, [r4, #1268]
	mov	r0, fp
	ldr	r2, [r2]
	str	r2, [sp, #16]
	ldr	r2, [r3, #12]
	str	r2, [sp, #12]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r3, [r3]
	ldr	r2, [r4, #1256]
	str	r3, [sp]
	ldr	r3, [r4, #1260]
	bl	printk
	ldr	r3, [r4, #1268]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L3686
	mov	r3, #768
	movs	r2, #4
	ldr	r1, [r5, #3316]
	ldr	r0, .L3691+8
	bl	rknand_print_hex
.L3686:
	adds	r6, r6, #1
	b	.L3685
.L3692:
	.align	2
.L3691:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC168
	.word	.LANCHOR0
	.word	.LC167
	.fnend
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	1
	.global	dump_map_info
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	dump_map_info, %function
dump_map_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L3708
	ldr	r5, .L3708+4
	ldrh	r7, [r4, #2332]
	addw	fp, r4, #2350
.L3694:
	ldrh	r3, [r4, #2334]
	cmp	r3, r7
	bhi	.L3702
	ldr	r6, .L3708+8
	mov	r9, #0
	ldr	fp, .L3708+24
	add	r10, r6, #1256
.L3703:
	ldrh	r3, [r5, #3452]
	sxth	r7, r9
	cmp	r7, r3
	bge	.L3706
	lsls	r7, r7, #1
	mov	r8, #0
	b	.L3707
.L3697:
	mov	r1, r7
	ldrb	r0, [fp, r8]	@ zero_extendqisi2
	str	r3, [sp, #44]
	str	r2, [sp, #40]
	bl	V2P_block
	str	r0, [sp, #36]
	bl	FtlBbmIsBadBlock
	ldr	r2, [sp, #40]
	ldr	r3, [sp, #44]
	cbnz	r0, .L3695
	ldr	r1, [sp, #36]
	mla	r0, r10, r6, r9
	lsls	r1, r1, #10
	str	r3, [r0, #8]
	str	r1, [r0, #4]
	ldr	r1, [sp, #32]
	muls	r1, r6, r1
	it	mi
	addmi	r1, r1, #3
	adds	r6, r6, #1
	bic	ip, r1, #3
	ldr	r1, [sp, #28]
	uxth	r6, r6
	add	r1, r1, ip
	str	r1, [r0, #12]
.L3695:
	add	r8, r8, #1
.L3704:
	uxth	r1, r8
	cmp	r2, r1
	bhi	.L3697
	cbnz	r6, .L3698
.L3701:
	adds	r7, r7, #1
	uxth	r7, r7
	b	.L3694
.L3698:
	ldr	r10, .L3708+28
	mov	r0, r9
	mov	r8, #0
	mov	r9, #36
	movs	r2, #1
	mov	r1, r6
	bl	FlashReadPages
.L3699:
	uxth	r3, r8
	cmp	r6, r3
	bls	.L3701
	ldr	r3, [r5, #3304]
	mla	r3, r9, r8, r3
	add	r8, r8, #1
	ldr	r1, [r3, #12]
	ldr	r2, [r3, #4]
	ldr	r3, [r3, #8]
	ldr	r0, [r3, #4]
	str	r0, [sp, #16]
	mov	r0, r10
	ldr	r3, [r3]
	str	r3, [sp, #12]
	ldr	r3, [r1, #12]
	str	r3, [sp, #8]
	ldr	r3, [r1, #8]
	str	r3, [sp, #4]
	ldr	r3, [r1, #4]
	str	r3, [sp]
	ldr	r3, [r1]
	ubfx	r1, r2, #10, #16
	bl	printk
	b	.L3699
.L3702:
	ldr	r1, [r5, #1148]
	mov	r8, #0
	ldrh	r2, [r4, #2324]
	mov	r6, r8
	ldr	r9, [r5, #3304]
	mov	r10, #36
	str	r1, [sp, #28]
	ldrh	r1, [r4, #2402]
	ldr	r3, [r5, #1144]
	str	r1, [sp, #32]
	b	.L3704
.L3705:
	ldr	r2, [r5, #3368]
	mov	r0, r10
	ldrh	r2, [r2, r7]
	orr	r3, r3, r2, lsl #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r6, #1260]
	bl	FlashReadPages
	ldr	r2, [r6, #1264]
	ldr	r1, [r5, #3368]
	ldr	r3, [r6, #1268]
	ldr	r0, [r2, #4]
	ldrh	r1, [r1, r7]
	str	r0, [sp, #20]
	mov	r0, fp
	ldr	r2, [r2]
	str	r2, [sp, #16]
	ldr	r2, [r3, #12]
	str	r2, [sp, #12]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp]
	ldr	r3, [r6, #1260]
	ldr	r2, [r6, #1256]
	bl	printk
.L3707:
	ldrh	r2, [r4, #2392]
	sxth	r3, r8
	add	r8, r8, #1
	cmp	r3, r2
	blt	.L3705
	add	r9, r9, #1
	b	.L3703
.L3706:
	ldr	r3, [r4, #2420]
	movs	r2, #2
	ldr	r1, [r5, #3368]
	ldr	r0, .L3708+12
	bl	rknand_print_hex
	ldrh	r3, [r4, #2428]
	movs	r2, #4
	ldr	r1, [r5, #3388]
	ldr	r0, .L3708+16
	bl	rknand_print_hex
	ldrh	r3, [r4, #2428]
	movs	r2, #4
	ldr	r1, [r5, #3392]
	ldr	r0, .L3708+20
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3709:
	.align	2
.L3708:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC170
	.word	.LC171
	.word	.LC172
	.word	.LC113
	.word	.LC169
	.fnend
	.size	dump_map_info, .-dump_map_info
	.align	1
	.global	flash_boot_enter_slc_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	flash_boot_enter_slc_mode, %function
flash_boot_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L3712
	ldr	r2, [r3, #2268]
	ldr	r3, .L3712+4
	cmp	r2, r3
	bne	.L3710
	b	flash_enter_slc_mode
.L3710:
	bx	lr
.L3713:
	.align	2
.L3712:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	1
	.global	flash_boot_exit_slc_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L3716
	ldr	r2, [r3, #2268]
	ldr	r3, .L3716+4
	cmp	r2, r3
	bne	.L3714
	b	flash_exit_slc_mode
.L3714:
	bx	lr
.L3717:
	.align	2
.L3716:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	1
	.global	write_idblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 112
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	ldr	r4, .L3766
	.pad #124
	sub	sp, sp, #124
	mov	r0, #256000
	mov	r9, r1
	mov	r5, r2
	ldr	r3, [r4, #48]
	ldr	r8, [r4, #40]
	ldrb	r6, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	str	r0, [sp, #8]
	cmp	r0, #0
	beq	.L3745
	addw	r7, r7, #511
	lsr	fp, r7, #9
	cmp	fp, #8
	bls	.L3743
	cmp	fp, #500
	bhi	.L3745
.L3720:
	ldr	r2, [r9]
	ldr	r3, .L3766+4
	cmp	r2, r3
	bne	.L3745
	smulbb	r6, r6, r8
	uxth	r3, r6
	subs	r0, r3, #1
	mov	r1, r3
	add	r0, r0, fp
	str	r3, [sp, #12]
	bl	__aeabi_uidiv
	str	r0, [sp, #32]
	add	r0, r9, #254976
	add	r0, r0, #512
	movs	r3, #0
	movw	r2, #63871
.L3724:
	ldr	r1, [r0, #-4]!
	cmp	r1, #0
	bne	.L3721
	ldr	r1, [r9, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #4096
	add	r2, r2, #-1
	it	hi
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3724
.L3723:
	movs	r3, #5
	mov	r1, r5
	movs	r2, #4
	ldr	r0, .L3766+8
	bl	rknand_print_hex
	ldrb	r2, [r4, #37]	@ zero_extendqisi2
	subs	r5, r5, #4
	ldr	r1, [r9, #512]
	ldr	r0, .L3766+12
	bl	printk
	ldr	r2, .L3766+16
	mov	r1, fp
	ldrh	r3, [r4, #150]
	ldr	r0, .L3766+20
	ldr	r2, [r2, #1212]
	str	r2, [sp]
	mov	r2, fp
	bl	printk
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	ldr	r2, [r9, #512]
	ldr	r4, .L3766
	cmp	r2, r3
	it	hi
	strhi	r3, [r9, #512]
	lsl	r3, fp, #7
	str	r3, [sp, #40]
	movs	r3, #0
	str	r3, [sp, #20]
	str	r3, [sp, #16]
.L3741:
	ldr	r2, [r5, #4]
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L3726
	ldr	r3, .L3766+16
	ldr	r3, [r3, #1212]
	cmp	r2, r3
	bcc	.L3726
	ldr	r3, [sp, #32]
	cmp	r3, #1
	bls	.L3727
	ldr	r3, [sp, #16]
	cbz	r3, .L3727
	ldr	r3, [r5]
	adds	r3, r3, #1
	cmp	r2, r3
	beq	.L3726
.L3727:
	mov	r2, #512
	movs	r1, #0
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r6, [r5, #4]
	mov	r2, fp
	ldr	r3, [sp, #12]
	ldr	r7, [r4, #40]
	ldr	r0, .L3766+24
	muls	r6, r3, r6
	ldr	r3, [r4, #48]
	ldrb	r10, [r3, #9]	@ zero_extendqisi2
	mov	r1, r6
	bl	printk
	movs	r0, #0
	smulbb	r7, r7, r10
	bl	flash_boot_enter_slc_mode
	mov	r1, r10
	mov	r0, r6
	bl	__aeabi_uidiv
	uxth	r7, r7
	movs	r2, #0
	mov	r1, r0
	mov	r0, r2
	bl	FlashEraseBlock
	cmp	r7, fp
	bcs	.L3746
	movs	r2, #0
	mov	r8, #2
	adds	r1, r6, r7
	mov	r0, r2
	bl	FlashEraseBlock
.L3728:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r4, #48]
	ldrh	r0, [r3, #10]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	lsls	r0, r0, #2
	mul	r0, r8, r0
	mov	r8, #0
	bl	__aeabi_idiv
	mov	r1, r7
	str	r0, [sp, #44]
	mov	r0, r6
	bl	__aeabi_uidivmod
	subs	r3, r6, r1
	str	r1, [sp, #28]
	str	r3, [sp, #36]
	str	r9, [sp, #24]
.L3729:
	ldr	r3, [sp, #44]
	cmp	r3, r8
	bhi	.L3733
	mov	r1, r6
	movs	r3, #0
	mov	r2, fp
	ldr	r0, .L3766+28
	bl	printk
	ldr	r6, [r5, #4]
	mov	r2, fp
	ldr	r3, [sp, #12]
	mov	r8, #0
	ldr	r1, [r4, #40]
	ldr	r0, .L3766+32
	muls	r6, r3, r6
	ldr	r3, [r4, #48]
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	str	r3, [sp, #24]
	ldrh	r3, [sp, #24]
	smulbb	r1, r1, r3
	uxth	r7, r1
	mov	r1, r6
	bl	printk
	mov	r1, r7
	mov	r0, r6
	bl	__aeabi_uidivmod
	subs	r3, r6, r1
	ldr	r10, [sp, #8]
	str	r3, [sp, #44]
	ldr	r3, [sp, #24]
	str	r1, [sp, #28]
	muls	r3, r1, r3
	ubfx	r3, r3, #2, #2
.L3734:
	cmp	r8, fp
	bcc	.L3736
	movs	r3, #0
	mov	r1, r6
	mov	r2, fp
	ldr	r0, .L3766+36
	bl	printk
	ldr	r0, [sp, #8]
	mov	r3, r9
	movs	r6, #0
.L3739:
	mov	r7, r0
	mov	r8, r3
	ldr	r1, [r7]
	adds	r0, r0, #4
	ldr	r2, [r8]
	adds	r3, r3, #4
	cmp	r1, r2
	beq	.L3737
	mov	r2, #512
	movs	r1, #0
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r3, [r8]
	ldr	r1, [sp, #16]
	ldr	r0, .L3766+40
	str	r3, [sp, #4]
	ldr	r3, [r7]
	str	r3, [sp]
	mov	r3, r6
	bic	r6, r6, #255
	ldr	r2, [r5, #4]
	lsls	r6, r6, #2
	bl	printk
	mov	r3, #256
	movs	r2, #4
	add	r1, r9, r6
	ldr	r0, .L3766+44
	bl	rknand_print_hex
	ldr	r1, [sp, #8]
	mov	r3, #256
	movs	r2, #4
	ldr	r0, .L3766+48
	add	r1, r1, r6
	bl	rknand_print_hex
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r1, [r5, #4]
	movs	r2, #0
	ldr	r3, [sp, #12]
	mov	r0, r2
	muls	r1, r3, r1
	bl	FlashEraseBlock
	ldr	r3, [sp, #32]
	cmp	r3, #1
	bls	.L3738
	ldr	r1, [r5, #4]
	movs	r2, #0
	ldr	r3, [sp, #12]
	mov	r0, r2
	mla	r1, r1, r3, r3
	bl	FlashEraseBlock
.L3738:
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r1, [r5, #4]
	ldr	r0, .L3766+52
	bl	printk
.L3726:
	ldr	r3, [sp, #16]
	adds	r5, r5, #4
	adds	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #16]
	bne	.L3741
	ldr	r0, [sp, #8]
	bl	ftl_free
	ldr	r3, [sp, #20]
	clz	r0, r3
	lsrs	r0, r0, #5
	negs	r0, r0
.L3718:
	add	sp, sp, #124
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3743:
	mov	fp, #8
	b	.L3720
.L3721:
	ldr	r0, .L3766+56
	bl	printk
	b	.L3723
.L3746:
	mov	r8, #1
	b	.L3728
.L3767:
	.align	2
.L3766:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC174
	.word	.LC175
	.word	.LANCHOR4
	.word	.LC176
	.word	.LC177
	.word	.LC178
	.word	.LC179
	.word	.LC180
	.word	.LC181
	.word	.LC182
	.word	.LC183
	.word	.LC184
	.word	.LC173
.L3733:
	ldr	r3, [sp, #28]
	add	r2, r3, r8
	lsrs	r2, r2, #2
	beq	.L3730
	ldrb	r0, [r4, #152]	@ zero_extendqisi2
	adds	r1, r2, #1
	add	r3, r4, r1, lsl #1
	ldrh	r3, [r3, #156]
	cbz	r0, .L3731
	ldr	r0, [r4, #2268]
	ldr	r7, .L3768
	cmp	r0, r7
	it	eq
	moveq	r3, r1
.L3731:
	add	r3, r3, #1073741824
	subs	r3, r3, #1
	lsls	r3, r3, #2
	str	r3, [sp, #56]
.L3730:
	movw	r3, #61424
	str	r3, [sp, #60]
	add	r3, r4, r2, lsl #1
	ldrh	r7, [r3, #156]
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cbz	r3, .L3732
	ldr	r3, [r4, #2268]
	ldr	r1, .L3768
	cmp	r3, r1
	it	eq
	moveq	r7, r2
.L3732:
	ldr	r3, [sp, #36]
	add	r8, r8, #4
	ldr	r2, .L3768+4
	uxth	r8, r8
	mla	r3, r7, r10, r3
	ldrb	r0, [r2, #1218]	@ zero_extendqisi2
	adds	r7, r7, #1
	uxth	r7, r7
	str	r3, [sp, #52]
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	str	r3, [sp, #48]
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r2, [r4, #48]
	ldr	r3, [sp, #52]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	mov	r0, r3
	bl	__aeabi_uidiv
	add	r3, sp, #56
	ldr	r2, [sp, #24]
	mov	r1, r0
	movs	r0, #0
	bl	FlashProgPage
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #48]
	bl	FlashBchSel
	mov	r1, r10
	ldr	r0, [sp, #36]
	bl	__aeabi_uidiv
	mov	r2, r7
	mov	r1, r0
	movs	r0, #0
	bl	FlashPageProgMsbFFData
	ldr	r3, [sp, #24]
	add	r3, r3, #2048
	str	r3, [sp, #24]
	b	.L3729
.L3736:
	rsb	r7, r3, #4
	ldrb	r0, [r4, #152]	@ zero_extendqisi2
	uxth	r2, r7
	str	r2, [sp, #36]
	ldr	r2, [sp, #28]
	add	r2, r2, r8
	lsrs	r2, r2, #2
	add	r1, r4, r2, lsl #1
	ldrh	r1, [r1, #156]
	cbz	r0, .L3735
	ldr	r0, [r4, #2268]
	ldr	r7, .L3768
	cmp	r0, r7
	it	eq
	moveq	r1, r2
.L3735:
	ldr	r2, [sp, #44]
	add	r3, r3, r2
	ldr	r2, [sp, #24]
	mla	r3, r1, r2, r3
	ldr	r2, [r4, #48]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	ldr	r2, .L3768+4
	str	r3, [sp, #52]
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	ldrb	r0, [r2, #1218]	@ zero_extendqisi2
	str	r1, [sp, #48]
	mov	r7, r3
	bl	FlashBchSel
	movs	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [sp, #52]
	ldr	r1, [sp, #48]
	mov	r0, r3
	bl	__aeabi_uidiv
	movs	r3, #0
	mov	r2, r10
	mov	r1, r0
	mov	r0, r3
	bl	FlashReadPage
	movs	r0, #0
	bl	flash_boot_exit_slc_mode
	mov	r0, r7
	bl	FlashBchSel
	ldr	r3, [sp, #36]
	add	r8, r8, r3
	add	r10, r10, r3, lsl #9
	uxth	r8, r8
	movs	r3, #0
	b	.L3734
.L3737:
	ldr	r2, [sp, #40]
	adds	r6, r6, #1
	cmp	r2, r6
	bne	.L3739
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	str	r3, [sp, #20]
	b	.L3726
.L3745:
	mov	r0, #-1
	b	.L3718
.L3769:
	.align	2
.L3768:
	.word	1446522928
	.word	.LANCHOR4
	.fnend
	.size	write_idblock, .-write_idblock
	.align	1
	.global	write_loader_lba
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	write_loader_lba, %function
write_loader_lba:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #64
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	.pad #48
	sub	sp, sp, #48
	mov	r6, r1
	mov	r8, r2
	ldr	r4, .L3793
	bne	.L3771
	ldr	r2, [r2]
	ldr	r3, .L3793+4
	cmp	r2, r3
	bne	.L3771
	movs	r3, #1
	mov	r0, #256000
	strb	r3, [r4, #1516]
	bl	ftl_malloc
	mov	r2, #256000
	movs	r1, #0
	str	r0, [r4, #1520]
	bl	ftl_memset
	str	r5, [r4, #1524]
.L3771:
	str	r6, [sp]
	mov	r3, r5
	ldr	r2, [r8]
	ldr	r1, [r4, #1520]
	ldr	r0, .L3793+8
	bl	printk
	ldrb	r3, [r4, #1516]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3770
	sub	r0, r5, #64
	ldr	r7, [r4, #1520]
	cmp	r0, #500
	bcs	.L3773
	rsb	r2, r5, #564
	mov	r1, r8
	cmp	r2, r6
	add	r0, r7, r0, lsl #9
	it	cs
	movcs	r2, r6
	lsls	r2, r2, #9
	bl	ftl_memcpy
.L3774:
	ldr	r3, [r4, #1524]
	cmp	r5, r3
	beq	.L3783
	movs	r3, #0
	strb	r3, [r4, #1516]
	mov	r8, r3
	cbz	r7, .L3784
	mov	r0, r7
	bl	ftl_free
.L3784:
	str	r8, [r4, #1520]
	b	.L3783
.L3773:
	cmp	r5, #564
	bcc	.L3774
	ldr	r3, .L3793+12
	ldr	r0, [r4, #1524]
	ldr	r3, [r3, #48]
	subs	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	it	cs
	movcs	r0, #500
	cmp	r3, #4
	beq	.L3775
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
.L3776:
	movw	r3, #63872
.L3782:
	ldr	r2, [r7, r3, lsl #2]
	cbz	r2, .L3780
	adds	r3, r3, #128
	lsls	r0, r3, #2
.L3781:
	mov	r1, r7
	add	r2, sp, #8
	movs	r7, #0
	bl	write_idblock
	ldr	r0, [r4, #1520]
	strb	r7, [r4, #1516]
	bl	ftl_free
	str	r7, [r4, #1520]
.L3783:
	add	r5, r5, r6
	str	r5, [r4, #1524]
.L3770:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L3775:
	movs	r2, #0
	add	r3, sp, #8
.L3779:
	cmp	r0, #256
	itet	hi
	lslhi	r1, r2, #1
	strls	r2, [r3, r2, lsl #2]
	strhi	r1, [r3, r2, lsl #2]
	adds	r2, r2, #1
	cmp	r2, #5
	bne	.L3779
	b	.L3776
.L3780:
	subs	r3, r3, #1
	cmp	r3, #4096
	bne	.L3782
	lsls	r0, r0, #9
	b	.L3781
.L3794:
	.align	2
.L3793:
	.word	.LANCHOR4
	.word	-52655045
	.word	.LC185
	.word	.LANCHOR0
	.fnend
	.size	write_loader_lba, .-write_loader_lba
	.align	1
	.global	FtlWrite
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r3
	sub	r3, r1, #64
	mov	r5, r0
	cmp	r3, #1984
	mov	r4, r1
	mov	r6, r2
	bcs	.L3796
	cbnz	r0, .L3796
	mov	r2, r7
	mov	r1, r6
	mov	r0, r4
	bl	write_loader_lba
.L3796:
	mov	r3, r7
	mov	r2, r6
	mov	r1, r4
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, lr}
	b	ftl_write
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	1
	.global	rknand_sys_storage_ioctl
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 520
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3856
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	mov	r4, r1
	.pad #524
	sub	sp, sp, #524
	mov	r5, r2
	cmp	r1, r3
	beq	.L3799
	bhi	.L3800
	subw	r3, r3, #2086
	cmp	r1, r3
	beq	.L3801
	bhi	.L3802
	subs	r3, r3, #238
	cmp	r1, r3
	beq	.L3803
	adds	r3, r3, #237
	cmp	r1, r3
	beq	.L3804
.L3834:
	mvn	r4, #21
	b	.L3797
.L3802:
	ldr	r3, .L3856+4
	cmp	r1, r3
	beq	.L3805
	adds	r3, r3, #1
	cmp	r1, r3
	beq	.L3806
	subs	r3, r3, #124
	cmp	r1, r3
	bne	.L3834
	ldr	r0, .L3856+8
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3817
	ldr	r2, [sp]
	ldr	r3, .L3856+12
	cmp	r2, r3
	bne	.L3814
	ldr	r2, [sp, #4]
	cmp	r2, #512
	bhi	.L3814
	ldr	r1, .L3856+16
.L3854:
	add	r0, sp, #8
	bl	memcpy
	b	.L3848
.L3800:
	ldr	r6, .L3856+20
	cmp	r1, r6
	beq	.L3808
	bhi	.L3809
	ldr	r3, .L3856+24
	cmp	r1, r3
	beq	.L3799
	adds	r3, r3, #10
	cmp	r1, r3
	bne	.L3834
.L3799:
	ldr	r7, .L3856+24
	cmp	r4, r7
	bne	.L3824
	ldr	r0, .L3856+28
.L3850:
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3817
	ldr	r2, [sp]
	ldr	r3, .L3856+32
	cmp	r2, r3
	bne	.L3853
	ldr	r3, .L3856+36
	ldr	r6, .L3856+40
	cmp	r4, r3
	bne	.L3827
	ldr	r3, [r6, #1528]
	movs	r2, #16
	mov	r1, sp
	mov	r0, r5
	ldr	r3, [r3, #20]
	str	r3, [sp, #4]
	strb	r3, [sp, #8]
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3853
.L3818:
	movs	r4, #0
.L3797:
	mov	r0, r4
	add	sp, sp, #524
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L3809:
	ldr	r3, .L3856+44
	cmp	r1, r3
	beq	.L3808
	bcc	.L3810
	adds	r3, r3, #1
	cmp	r1, r3
	bne	.L3834
.L3810:
	ldr	r0, .L3856+48
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cbnz	r0, .L3817
	ldr	r2, [sp]
	ldr	r3, .L3856+52
	cmp	r2, r3
	bne	.L3814
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L3814
	ldr	r3, .L3856+56
	mov	r1, sp
	adds	r2, r2, #8
	cmp	r4, r3
	ldr	r4, .L3856+40
	bne	.L3833
	ldr	r0, [r4, #2060]
	bl	memcpy
	ldr	r1, [r4, #2060]
	movs	r0, #2
	b	.L3851
.L3804:
	ldr	r0, .L3856+60
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cbz	r0, .L3811
.L3817:
	ldr	r0, .L3856+64
	bl	printk
.L3853:
	mvn	r4, #13
	b	.L3797
.L3811:
	ldr	r2, [sp]
	ldr	r3, .L3856+68
	cmp	r2, r3
	beq	.L3812
.L3814:
	mov	r4, #-1
.L3813:
	mov	r1, r4
	ldr	r0, .L3856+72
	bl	printk
	b	.L3797
.L3812:
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3814
	ldr	r4, .L3856+40
	mov	r2, #512
	mov	r0, sp
	ldr	r1, [r4, #1528]
	bl	memcpy
	ldr	r2, [r4, #1532]
	ldr	r3, .L3856+76
	cmp	r2, r3
	beq	.L3815
	movs	r1, #0
	movs	r2, #128
	add	r0, sp, #64
	str	r1, [sp, #8]
	str	r1, [sp, #12]
	bl	memset
.L3815:
	mov	r2, #256
	movs	r1, #0
	add	r0, sp, r2
	str	r1, [sp, #16]
	bl	memset
.L3848:
	mov	r2, #520
	mov	r1, sp
	mov	r0, r5
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3853
.L3852:
	movs	r4, #0
	b	.L3813
.L3801:
	ldr	r0, .L3856+80
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3817
	ldr	r2, [sp]
	ldr	r3, .L3856+68
	cmp	r2, r3
	bne	.L3814
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3814
	ldr	r2, .L3856+40
	ldr	r3, .L3856+76
	ldr	r1, [r2, #1532]
	cmp	r1, r3
	bne	.L3835
	ldr	r3, [sp, #12]
	subs	r1, r3, #1
	cmp	r1, #127
	bhi	.L3836
	ldr	r4, [r2, #1528]
	add	r1, sp, #64
	str	r3, [r4, #12]
	add	r0, r4, #64
	ldr	r2, [sp, #12]
	bl	memcpy
	mov	r1, r4
	movs	r0, #1
.L3851:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L3813
.L3806:
	ldr	r0, .L3856+84
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3817
	ldr	r2, [sp]
	ldr	r3, .L3856+88
	cmp	r2, r3
	bne	.L3814
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3814
	ldr	r5, .L3856+40
	ldr	r3, [r5, #1536]
	cmp	r3, #0
	beq	.L3818
	ldr	r3, [r5, #1540]
	ldr	r2, .L3856+92
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3819
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r5, #1540]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3819:
	ldr	r1, [r5, #1540]
	movs	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #1528]
	ldr	r2, .L3856+68
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3820
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r5, #1528]
	str	r2, [r3, #4]
	str	r4, [r3, #8]
.L3820:
	ldr	r6, [r5, #1528]
	movs	r4, #0
	movs	r2, #128
	mov	r1, r4
	str	r4, [r6, #12]
	add	r0, r6, #64
	bl	memset
	mov	r1, r6
	movs	r0, #1
	bl	StorageSysDataStore
	str	r4, [r5, #1536]
	str	r4, [r5, #1532]
	b	.L3813
.L3857:
	.align	2
.L3856:
	.word	1074031656
	.word	1074029694
	.word	.LC191
	.word	1094995539
	.word	.LANCHOR4+1544
	.word	1074034192
	.word	1074031666
	.word	.LC192
	.word	1280262987
	.word	1074031676
	.word	.LANCHOR4
	.word	1074034194
	.word	.LC197
	.word	1145980246
	.word	1074034193
	.word	.LC186
	.word	.LC187
	.word	1263358532
	.word	.LC198
	.word	-1067903959
	.word	.LC188
	.word	.LC189
	.word	1112753220
	.word	1146313043
.L3805:
	ldr	r0, .L3858
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3817
	ldr	r2, [sp]
	ldr	r3, .L3858+4
	cmp	r2, r3
	bne	.L3814
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3814
	ldr	r5, .L3858+8
	ldr	r3, [r5, #1536]
	cmp	r3, #1
	beq	.L3818
	ldr	r2, [r5, #1540]
	ldr	r3, .L3858+12
	ldr	r1, [r2]
	cmp	r1, r3
	beq	.L3821
	str	r3, [r2]
	mov	r2, #504
	ldr	r3, [r5, #1540]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3821:
	ldr	r1, [r5, #1540]
	movs	r3, #1
	movs	r0, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #1528]
	ldr	r2, .L3858+16
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3822
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r5, #1528]
	str	r2, [r3, #4]
	movs	r2, #0
	str	r2, [r3, #8]
.L3822:
	ldr	r6, [r5, #1528]
	movs	r4, #0
	movs	r2, #128
	mov	r1, r4
	str	r4, [r6, #12]
	add	r0, r6, #64
	bl	memset
	mov	r1, r6
	movs	r0, #1
	bl	StorageSysDataStore
	movs	r3, #1
	str	r3, [r5, #1536]
	b	.L3813
.L3824:
	ldr	r3, .L3858+20
	cmp	r4, r3
	ite	eq
	ldreq	r0, .L3858+24
	ldrne	r0, .L3858+28
	b	.L3850
.L3827:
	ldr	r3, [r6, #2056]
	cmp	r3, #10
	bhi	.L3853
	ldr	r3, [r6, #1528]
	ldr	r1, [sp, #4]
	ldr	r2, [r3, #24]
	cmp	r2, r1
	beq	.L3828
	cbz	r2, .L3828
	ldr	r0, .L3858+32
	bl	printk
	ldr	r3, [r6, #2056]
	adds	r3, r3, #1
	str	r3, [r6, #2056]
	b	.L3853
.L3828:
	cmp	r4, r7
	mov	r2, #0
	str	r2, [r6, #2056]
	itt	ne
	movne	r2, #1
	strne	r1, [r3, #24]
	mov	r0, #1
	it	eq
	streq	r2, [r3, #20]
	mov	r1, r3
	ite	eq
	streq	r2, [r3, #24]
	strne	r2, [r3, #20]
	bl	StorageSysDataStore
	adds	r0, r0, #1
	bne	.L3852
	mvn	r4, #1
	b	.L3813
.L3808:
	ldr	r0, .L3858+36
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3817
	ldr	r2, [sp]
	ldr	r3, .L3858+40
	cmp	r2, r3
	bne	.L3814
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L3814
	ldr	r3, .L3858+8
	cmp	r4, r6
	ite	eq
	ldreq	r1, [r3, #2060]
	ldrne	r1, [r3, #2064]
	adds	r1, r1, #8
	b	.L3854
.L3833:
	ldr	r0, [r4, #2064]
	bl	memcpy
	ldr	r1, [r4, #2064]
	movs	r0, #3
	b	.L3851
.L3803:
	bl	rknand_dev_flush
	b	.L3852
.L3835:
	mvn	r4, #1
	b	.L3797
.L3836:
	mvn	r4, #2
	b	.L3797
.L3859:
	.align	2
.L3858:
	.word	.LC190
	.word	1112755781
	.word	.LANCHOR4
	.word	1146313043
	.word	1263358532
	.word	1074031676
	.word	.LC193
	.word	.LC194
	.word	.LC195
	.word	.LC196
	.word	1145980246
	.fnend
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	1
	.global	rk_ftl_storage_sys_init
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r3, #-1
	ldr	r4, .L3869
	movs	r5, #0
	mov	r2, #512
	ldr	r1, [r4, #1500]
	add	r0, r4, #1544
	str	r3, [r4, #1524]
	strb	r5, [r4, #1516]
	add	r3, r1, #512
	str	r1, [r4, #1540]
	str	r3, [r4, #1528]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #2060]
	str	r5, [r4, #1520]
	str	r5, [r4, #2068]
	str	r1, [r4, #2064]
	bl	ftl_memcpy
	ldr	r6, [r4, #1540]
	str	r5, [r4, #1532]
	str	r5, [r4, #2056]
	ldr	r7, [r6, #508]
	ldr	r3, [r6, #16]
	str	r3, [r4, #1536]
	cbz	r7, .L3861
	mov	r1, #508
	mov	r0, r6
	bl	js_hash
	cmp	r7, r0
	beq	.L3861
	str	r5, [r6, #16]
	ldr	r0, .L3869+4
	str	r5, [r4, #1536]
	bl	printk
.L3861:
	ldr	r3, [r4, #1536]
	cbz	r3, .L3862
	ldr	r3, .L3869+8
	str	r3, [r4, #1532]
.L3862:
	ldr	r1, [r4, #2060]
	movs	r0, #2
	bl	StorageSysDataLoad
	ldr	r1, [r4, #2064]
	movs	r0, #3
	bl	StorageSysDataLoad
	pop	{r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L3870:
	.align	2
.L3869:
	.word	.LANCHOR4
	.word	.LC199
	.word	-1067903959
	.fnend
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	1
	.global	StorageSysDataDeInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_vendor_storage_init, %function
rk_ftl_vendor_storage_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r0, #65536
	ldr	r6, .L3880
	bl	ftl_malloc
	str	r0, [r6, #2072]
	cmp	r0, #0
	beq	.L3878
	ldr	r10, .L3880+4
	mov	r8, #0
	ldr	r9, .L3880+8
	mov	r4, r8
	mov	r7, r8
.L3876:
	ldr	r2, [r6, #2072]
	movs	r1, #128
	lsls	r0, r7, #7
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L3874
	ldr	r1, [r6, #2072]
	movw	fp, #65532
	mov	r0, r10
	ldr	r3, [r1, #4]
	ldr	r2, [r1, fp]
	ldr	r1, [r1]
	bl	printk
	ldr	r5, [r6, #2072]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L3875
	ldr	r3, [r5, fp]
	ldr	r2, [r5, #4]
	cmp	r3, r2
	bne	.L3875
	cmp	r3, r4
	itt	hi
	movhi	r8, r7
	movhi	r4, r3
.L3875:
	adds	r7, r7, #1
	cmp	r7, #2
	bne	.L3876
	cbz	r4, .L3877
	mov	r2, r5
	movs	r1, #128
	lsl	r0, r8, #7
	bl	FlashBootVendorRead
	cbnz	r0, .L3874
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3877:
	mov	r2, #65536
	mov	r1, r4
	mov	r0, r5
	bl	memset
	movs	r3, #1
	movw	r2, #65532
	str	r3, [r5, #4]
	mov	r0, r4
	str	r9, [r5]
	str	r3, [r5, r2]
	movw	r3, #64504
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3874:
	ldr	r0, [r6, #2072]
	bl	kfree
	movs	r3, #0
	mov	r0, #-1
	str	r3, [r6, #2072]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3878:
	mvn	r0, #11
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3881:
	.align	2
.L3880:
	.word	.LANCHOR4
	.word	.LC200
	.word	1380668996
	.fnend
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	1
	.global	rk_ftl_vendor_read
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r7, r0
	ldr	r3, .L3888
	mov	r0, r1
	ldr	r5, [r3, #2072]
	cbz	r5, .L3887
	ldrh	r6, [r5, #10]
	movs	r3, #0
.L3884:
	cmp	r3, r6
	bcc	.L3886
.L3887:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L3886:
	add	r1, r5, r3, lsl #3
	ldrh	r4, [r1, #16]
	cmp	r4, r7
	bne	.L3885
	ldrh	r4, [r1, #20]
	ldrh	r1, [r1, #18]
	cmp	r4, r2
	it	cs
	movcs	r4, r2
	add	r1, r1, #1024
	mov	r2, r4
	add	r1, r1, r5
	bl	memcpy
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L3885:
	adds	r3, r3, #1
	b	.L3884
.L3889:
	.align	2
.L3888:
	.word	.LANCHOR4
	.fnend
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	1
	.global	rk_ftl_vendor_write
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r2, .L3910
	.pad #28
	sub	sp, sp, #28
	mov	r3, r1
	ldr	r4, [r2, #2072]
	cmp	r4, #0
	beq	.L3905
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r1, [r4, #8]
	bic	r6, r6, #63
	movs	r7, #0
	str	r1, [sp, #4]
.L3892:
	cmp	r7, r2
	bcc	.L3900
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L3905
	add	r2, r4, r2, lsl #3
	uxth	r6, r6
	strh	r0, [r2, #16]	@ movhi
	ldrh	r0, [r4, #12]
	strh	r8, [r2, #20]	@ movhi
	strh	r0, [r2, #18]	@ movhi
	add	r0, r0, r6
	subs	r6, r1, r6
	strh	r0, [r4, #12]	@ movhi
	strh	r6, [r4, #14]	@ movhi
	mov	r1, r3
	ldrh	r0, [r2, #18]
	mov	r2, r8
	add	r0, r0, #1024
	add	r0, r0, r4
	bl	memcpy
	ldrh	r3, [r4, #10]
	adds	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
	b	.L3909
.L3900:
	add	r5, r4, r7, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, r0
	str	r1, [sp, #8]
	bne	.L3893
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L3894
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L3905
	ldrh	r10, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #16]
.L3895:
	ldr	r2, [sp, #16]
	adds	r5, r5, #8
	cmp	r7, r2
	bcc	.L3896
	ldrh	r2, [sp, #8]
	add	r7, r4, r7, lsl #3
	uxth	r5, r10
	mov	r1, r3
	strh	r8, [r7, #20]	@ movhi
	uxtah	r0, fp, r10
	strh	r2, [r7, #16]	@ movhi
	mov	r2, r8
	strh	r5, [r7, #18]	@ movhi
	bl	memcpy
	uxth	r3, r6
	ldrh	r6, [r4, #14]
	add	r5, r5, r3
	subs	r6, r6, r3
	ldr	r3, [sp, #12]
	strh	r5, [r4, #12]	@ movhi
	add	r6, r6, r3
	strh	r6, [r4, #14]	@ movhi
.L3909:
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
.L3890:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3896:
	ldrh	r9, [r5, #20]
	add	r0, fp, r10
	ldrh	r2, [r5, #16]
	adds	r7, r7, #1
	ldrh	r1, [r5, #18]
	strh	r9, [r5, #12]	@ movhi
	add	r9, r9, #63
	bic	r9, r9, #63
	strh	r2, [r5, #8]	@ movhi
	strh	r10, [r5, #10]	@ movhi
	mov	r2, r9
	add	r1, r1, fp
	str	r3, [sp, #20]
	add	r10, r10, r9
	bl	memcpy
	ldr	r3, [sp, #20]
	b	.L3895
.L3894:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, r3
	add	r0, r0, fp
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L3909
.L3893:
	adds	r7, r7, #1
	b	.L3892
.L3905:
	mov	r0, #-1
	b	.L3890
.L3911:
	.align	2
.L3910:
	.word	.LANCHOR4
	.fnend
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	1
	.global	rk_ftl_vendor_storage_ioctl
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
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
	beq	.L3920
	ldr	r3, .L3927
	cmp	r6, r3
	beq	.L3915
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L3916
.L3926:
	mvn	r5, #13
	b	.L3914
.L3915:
	movs	r2, #8
	mov	r1, r5
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3926
	ldr	r2, [r4]
	ldr	r3, .L3927+4
	cmp	r2, r3
	beq	.L3918
.L3919:
	mov	r5, #-1
.L3914:
	mov	r0, r4
	bl	kfree
.L3912:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L3918:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_ftl_vendor_read
	adds	r3, r0, #1
	beq	.L3919
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	adds	r2, r2, #8
	mov	r0, r5
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3926
.L3922:
	mov	r5, r0
	b	.L3914
.L3916:
	movs	r2, #8
	mov	r1, r5
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3926
	ldr	r2, [r4]
	ldr	r3, .L3927+4
	cmp	r2, r3
	bne	.L3919
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L3919
	adds	r2, r2, #8
	mov	r1, r5
	mov	r0, r4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3926
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_ftl_vendor_write
	b	.L3922
.L3920:
	mov	r5, #-1
	b	.L3912
.L3928:
	.align	2
.L3927:
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
	.global	ftl_dma32_buffer_size
	.global	ftl_dma32_buffer
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
	.set	.LANCHOR3,. + 0
	.type	__func__.23812, %object
	.size	__func__.23812, 11
__func__.23812:
	.ascii	"FtlMemInit\000"
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
	.type	__func__.24591, %object
	.size	__func__.24591, 17
__func__.24591:
	.ascii	"FtlDumpBlockInfo\000"
	.type	__func__.24610, %object
	.size	__func__.24610, 16
__func__.24610:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.24878, %object
	.size	__func__.24878, 17
__func__.24878:
	.ascii	"ftl_scan_all_ppa\000"
	.type	__func__.24559, %object
	.size	__func__.24559, 12
__func__.24559:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.24858, %object
	.size	__func__.24858, 21
__func__.24858:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.23885, %object
	.size	__func__.23885, 8
__func__.23885:
	.ascii	"FtlInit\000"
	.data
	.align	2
	.set	.LANCHOR1,. + 0
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
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR2,. + 4344
	.set	.LANCHOR4,. + 8688
	.type	gNandChipMap, %object
	.size	gNandChipMap, 32
gNandChipMap:
	.space	32
	.type	p_blk_mode_table, %object
	.size	p_blk_mode_table, 4
p_blk_mode_table:
	.space	4
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
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 4
gpNandParaInfo:
	.space	4
	.type	gNandOptPara, %object
	.size	gNandOptPara, 32
gNandOptPara:
	.space	32
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
	.space	1
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.space	1
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
	.type	gReadRetryInfo, %object
	.size	gReadRetryInfo, 852
gReadRetryInfo:
	.space	852
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
	.type	g_sys_ext_data, %object
	.size	g_sys_ext_data, 512
g_sys_ext_data:
	.space	512
	.type	g_all_blk_used_slc_mode, %object
	.size	g_all_blk_used_slc_mode, 4
g_all_blk_used_slc_mode:
	.space	4
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
	.type	ftl_dma32_buffer_size, %object
	.size	ftl_dma32_buffer_size, 4
ftl_dma32_buffer_size:
	.space	4
	.type	ftl_dma32_buffer, %object
	.size	ftl_dma32_buffer, 4
ftl_dma32_buffer:
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
	.type	FbbtBlk, %object
	.size	FbbtBlk, 16
FbbtBlk:
	.space	16
	.space	2
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
	.type	check_valid_page_count_table, %object
	.size	check_valid_page_count_table, 8192
check_valid_page_count_table:
	.space	8192
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
	.ascii	"FTL version: 5.0.63 20210616\000"
.LC77:
	.ascii	"%s\012\000"
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
