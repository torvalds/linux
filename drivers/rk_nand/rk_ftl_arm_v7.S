/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-26
 */
	.arch armv7-a
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 4
	.eabi_attribute 34, 1
	.eabi_attribute 18, 4
	.file	"rk_ftl_arm_v7.c"
	.syntax unified
	.text
	.align	2
	.fpu softvfp
	.type	ndelay, %function
ndelay:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L2
	add	r0, r0, #996
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L2+4
	ldr	r3, [r3, #8]
	lsr	r0, r1, #6
	bx	r3	@ indirect register sibling call
.L3:
	.align	2
.L2:
	.word	274877907
	.word	arm_delay_ops
	.fnend
	.size	ndelay, .-ndelay
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L6
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	mov	r0, #80
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r4, r4, r3, lsl #8
	mov	r3, #122
	str	r3, [r4, #2056]
	bl	ndelay
	ldr	r3, [r4, #2048]
	ldr	r0, [r4, #2048]
	and	r3, r3, #15
	and	r0, r0, #15
	cmp	r0, r3
	movcc	r0, r3
	ldr	r3, [r4, #2048]
	and	r3, r3, #15
	cmp	r3, r0
	movcc	r3, r0
	ldr	r0, [r4, #2048]
	and	r0, r0, #15
	cmp	r0, r3
	movcc	r0, r3
	pop	{r4, pc}
.L7:
	.align	2
.L6:
	.word	.LANCHOR0
	.fnend
	.size	flash_read_ecc, .-flash_read_ecc
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode.part.9, %function
ftl_set_blk_mode.part.9:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L9
	lsr	r1, r0, #5
	mov	ip, #1
	and	r0, r0, #31
	ldr	r2, [r3, #32]
	ldr	r3, [r2, r1, lsl #2]
	orr	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	bx	lr
.L10:
	.align	2
.L9:
	.word	.LANCHOR0
	.fnend
	.size	ftl_set_blk_mode.part.9, .-ftl_set_blk_mode.part.9
	.align	2
	.global	FlashMemCmp8
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L25
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L20
	ldrb	r3, [r1, #1]	@ zero_extendqisi2
	ldrb	ip, [r0, #1]	@ zero_extendqisi2
	cmp	ip, r3
	movne	r3, #0
	bne	.L20
.L24:
	mov	r0, #0
	bx	lr
.L14:
	cmp	r3, r2
	bne	.L16
	mov	r0, #0
	ldr	pc, [sp], #4
.L20:
	cmp	r3, r2
	beq	.L24
	str	lr, [sp, #-4]!
	.save {lr}
.L16:
	ldrb	lr, [r0, r3]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	lr, ip
	beq	.L14
	mov	r0, r3
	ldr	pc, [sp], #4
.L26:
	.align	2
.L25:
	.word	.LANCHOR0
	.fnend
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	2
	.global	FlashRsvdBlkChk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L28
	ldrb	ip, [r2, #37]	@ zero_extendqisi2
	ldr	r3, [r2, #40]
	mul	r3, r3, ip
	cmp	r3, r1
	movls	r2, #0
	movhi	r2, #1
	cmp	r0, #0
	movne	r2, #0
	eor	r0, r2, #1
	bx	lr
.L29:
	.align	2
.L28:
	.word	.LANCHOR0
	.fnend
	.size	FlashRsvdBlkChk, .-FlashRsvdBlkChk
	.align	2
	.global	FlashGetRandomizer
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashGetRandomizer, %function
FlashGetRandomizer:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	and	r3, r1, #127
	ldr	r2, .L39
	lsl	r3, r3, #1
	push	{r4, lr}
	.save {r4, lr}
	ldrh	r4, [r2, r3]
	ldr	r3, .L39+4
	ldrb	r3, [r3, #44]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L30
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r4, r4, #-1073741824
.L30:
	mov	r0, r4
	pop	{r4, pc}
.L40:
	.align	2
.L39:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashGetRandomizer, .-FlashGetRandomizer
	.align	2
	.global	FlashSetRandomizer
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashSetRandomizer, %function
FlashSetRandomizer:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L50
	and	r3, r1, #127
	lsl	r3, r3, #1
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r2, r3]
	ldr	r3, .L50+4
	ldrb	r2, [r3, #44]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r2, #0
	beq	.L42
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r5, r5, #-1073741824
.L42:
	ldr	r3, [r4, r6, lsl #3]
	str	r5, [r3, #336]
	pop	{r4, r5, r6, pc}
.L51:
	.align	2
.L50:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	2
	.global	FlashBlockAlignInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashBlockAlignInit, %function
FlashBlockAlignInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, #512
	ldr	r3, .L58
	movhi	r2, #1024
	bhi	.L57
	cmp	r0, #256
	movhi	r2, #512
	bhi	.L57
	cmp	r0, #128
	movhi	r2, #256
	bhi	.L57
	str	r0, [r3, #40]
	bx	lr
.L57:
	str	r2, [r3, #40]
	bx	lr
.L59:
	.align	2
.L58:
	.word	.LANCHOR0
	.fnend
	.size	FlashBlockAlignInit, .-FlashBlockAlignInit
	.align	2
	.global	FlashReadCmd
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadCmd, %function
FlashReadCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L63
	str	lr, [sp, #-4]!
	.save {lr}
	add	r2, ip, r0, lsl #3
	ldr	r3, [ip, r0, lsl #3]
	ldr	ip, [ip, #48]
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	ldrb	ip, [ip, #7]	@ zero_extendqisi2
	lsl	r2, r2, #8
	cmp	ip, #1
	addeq	ip, r3, r2
	moveq	lr, #38
	add	r3, r3, r2
	mov	r2, #0
	streq	lr, [ip, #2056]
	str	r2, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	uxtb	r2, r1
	str	r2, [r3, #2052]
	lsr	r2, r1, #8
	str	r2, [r3, #2052]
	lsr	r2, r1, #16
	str	r2, [r3, #2052]
	mov	r2, #48
	str	r2, [r3, #2056]
	ldr	lr, [sp], #4
	b	FlashSetRandomizer
.L64:
	.align	2
.L63:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadCmd, .-FlashReadCmd
	.align	2
	.global	FlashReadDpDataOutCmd
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadDpDataOutCmd, %function
FlashReadDpDataOutCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L70
	push	{r4, lr}
	.save {r4, lr}
	uxtb	r4, r1
	lsr	lr, r1, #8
	add	r2, ip, r0, lsl #3
	ldr	r3, [ip, r0, lsl #3]
	ldrb	ip, [ip, #68]	@ zero_extendqisi2
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	cmp	ip, #1
	lsr	ip, r1, #16
	lsl	r2, r2, #8
	add	r3, r3, r2
	bne	.L66
	mov	r2, #6
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r4, [r3, #2052]
	str	lr, [r3, #2052]
	str	ip, [r3, #2052]
.L69:
	mov	r2, #224
	str	r2, [r3, #2056]
	pop	{r4, lr}
	b	FlashSetRandomizer
.L66:
	mov	r2, #0
	str	r2, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r4, [r3, #2052]
	str	lr, [r3, #2052]
	str	ip, [r3, #2052]
	mov	ip, #5
	str	ip, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	b	.L69
.L71:
	.align	2
.L70:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	2
	.global	FlashProgFirstCmd
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgFirstCmd, %function
FlashProgFirstCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L74
	lsr	r2, r1, #16
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	r3, [ip, r0, lsl #3]
	add	ip, ip, r0, lsl #3
	ldrb	ip, [ip, #4]	@ zero_extendqisi2
	add	r3, r3, ip, lsl #8
	mov	ip, #128
	str	ip, [r3, #2056]
	mov	ip, #0
	str	ip, [r3, #2052]
	str	ip, [r3, #2052]
	uxtb	ip, r1
	str	ip, [r3, #2052]
	lsr	ip, r1, #8
	str	ip, [r3, #2052]
	str	r2, [r3, #2052]
	ldr	lr, [sp], #4
	b	FlashSetRandomizer
.L75:
	.align	2
.L74:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgFirstCmd, .-FlashProgFirstCmd
	.align	2
	.global	FlashEraseCmd
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseCmd, %function
FlashEraseCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L82
	cmp	r2, #0
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	r3, [ip, r0, lsl #3]
	add	r0, ip, r0, lsl #3
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	lsl	r0, r0, #8
	beq	.L77
	add	r2, r3, r0
	mov	lr, #96
	str	lr, [r2, #2056]
	uxtb	lr, r1
	str	lr, [r2, #2052]
	lsr	lr, r1, #8
	str	lr, [r2, #2052]
	lsr	lr, r1, #16
	str	lr, [r2, #2052]
	ldr	r2, [ip, #40]
	add	r1, r1, r2
.L77:
	add	r3, r3, r0
	mov	r2, #96
	str	r2, [r3, #2056]
	uxtb	r2, r1
	str	r2, [r3, #2052]
	lsr	r2, r1, #8
	lsr	r1, r1, #16
	str	r2, [r3, #2052]
	mov	r2, #208
	str	r1, [r3, #2052]
	str	r2, [r3, #2056]
	ldr	pc, [sp], #4
.L83:
	.align	2
.L82:
	.word	.LANCHOR0
	.fnend
	.size	FlashEraseCmd, .-FlashEraseCmd
	.align	2
	.global	FlashProgDpSecondCmd
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgDpSecondCmd, %function
FlashProgDpSecondCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	lsr	r2, r1, #16
	ldr	lr, .L86
	ldr	r3, [lr, r0, lsl #3]
	add	ip, lr, r0, lsl #3
	ldrb	r4, [ip, #4]	@ zero_extendqisi2
	ldrb	ip, [lr, #63]	@ zero_extendqisi2
	add	r3, r3, r4, lsl #8
	str	ip, [r3, #2056]
	mov	ip, #0
	str	ip, [r3, #2052]
	str	ip, [r3, #2052]
	uxtb	ip, r1
	str	ip, [r3, #2052]
	lsr	ip, r1, #8
	str	ip, [r3, #2052]
	str	r2, [r3, #2052]
	pop	{r4, lr}
	b	FlashSetRandomizer
.L87:
	.align	2
.L86:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpSecondCmd, .-FlashProgDpSecondCmd
	.align	2
	.global	FlashProgSecondCmd
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgSecondCmd, %function
FlashProgSecondCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L90
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	ldr	r0, .L90+4
	ldrb	r5, [r3, #4]	@ zero_extendqisi2
	ldr	r3, .L90+8
	add	r4, r4, r5, lsl #8
	ldr	r3, [r3, #4]
	blx	r3
	mov	r3, #16
	str	r3, [r4, #2056]
	pop	{r4, r5, r6, pc}
.L91:
	.align	2
.L90:
	.word	.LANCHOR0
	.word	64424500
	.word	arm_delay_ops
	.fnend
	.size	FlashProgSecondCmd, .-FlashProgSecondCmd
	.align	2
	.global	FlashProgDpFirstCmd
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgDpFirstCmd, %function
FlashProgDpFirstCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L93
	ldr	r3, [r2, r0, lsl #3]
	add	r0, r2, r0, lsl #3
	ldrb	r2, [r2, #62]	@ zero_extendqisi2
	ldrb	r1, [r0, #4]	@ zero_extendqisi2
	add	r3, r3, r1, lsl #8
	str	r2, [r3, #2056]
	bx	lr
.L94:
	.align	2
.L93:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	2
	.global	FlashReadStatus
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadStatus, %function
FlashReadStatus:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L97
	mov	r2, #112
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	mov	r0, #80
	ldrb	r4, [r3, #4]	@ zero_extendqisi2
	add	r3, r5, r4, lsl #8
	add	r4, r4, #8
	str	r2, [r3, #2056]
	bl	ndelay
	ldr	r0, [r5, r4, lsl #8]
	pop	{r4, r5, r6, pc}
.L98:
	.align	2
.L97:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatus, .-FlashReadStatus
	.align	2
	.global	js_hash
	.syntax unified
	.arm
	.fpu softvfp
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L102
	add	r1, r0, r1
.L100:
	cmp	r0, r1
	bne	.L101
	mov	r0, r3
	bx	lr
.L101:
	lsr	r2, r3, #2
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L100
.L103:
	.align	2
.L102:
	.word	1204201446
	.fnend
	.size	js_hash, .-js_hash
	.align	2
	.global	FlashLoadIdbInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashLoadIdbInfo, %function
FlashLoadIdbInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r0, #0
	bx	lr
	.fnend
	.size	FlashLoadIdbInfo, .-FlashLoadIdbInfo
	.align	2
	.global	FlashPrintInfo
	.syntax unified
	.arm
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
	.align	2
	.global	ToshibaSetRRPara
	.syntax unified
	.arm
	.fpu softvfp
	.type	ToshibaSetRRPara, %function
ToshibaSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r9, r1, r1, lsl #2
	ldr	r7, .L114
	mov	r6, r0
	mov	r5, #0
	add	r7, r1, r7
.L107:
	ldr	r8, .L114+4
	ldrb	r3, [r8, #85]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L111
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L111:
	ldr	r4, .L114+8
	mov	r3, #85
	str	r3, [r6, #8]
	mov	r0, #200
	ldrsb	r3, [r5, r4]
	str	r3, [r6, #4]
	bl	ndelay
	ldrb	r3, [r8, #84]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r5, r9
	addeq	r4, r4, r3
	ldrsbeq	r3, [r4, #5]
	beq	.L113
	cmp	r3, #35
	addeq	r3, r5, r9
	ldrsbne	r3, [r7]
	addeq	r4, r4, r3
	ldrsbeq	r3, [r4, #50]
.L113:
	str	r3, [r6]
	add	r5, r5, #1
	b	.L107
.L115:
	.align	2
.L114:
	.word	.LANCHOR1+396
	.word	.LANCHOR0
	.word	.LANCHOR1+256
	.fnend
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	2
	.global	SamsungSetRRPara
	.syntax unified
	.arm
	.fpu softvfp
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L120
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, #0
	ldr	r8, .L120+4
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	add	r1, r3, r1, lsl #2
	mov	r10, r4
	add	r5, r1, #3
.L117:
	ldrb	r3, [r8, #85]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L118
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L118:
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	ndelay
	b	.L117
.L121:
	.align	2
.L120:
	.word	.LANCHOR1+404
	.word	.LANCHOR0
	.fnend
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	2
	.global	ftl_flash_suspend
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L123
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
.L124:
	.align	2
.L123:
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.global	__aeabi_uidiv
	.global	__aeabi_uidivmod
	.align	2
	.global	LogAddr2PhyAddr
	.syntax unified
	.arm
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
	ldr	r4, .L131
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
	lsleq	r6, r6, #1
	ubfx	r2, r5, #10, #16
	mov	r1, r3
	str	r3, [sp, #4]
	mov	r0, r2
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
	bne	.L127
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r0, r4, r0, lsl #1
	ldrheq	r0, [r0, #156]
.L127:
	add	r4, r4, r8, lsl #2
	ldr	r3, [r4, #1180]
	mla	r6, r6, r1, r3
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	cmp	r3, #1
	add	r0, r6, r0
	str	r0, [r9]
	movls	r0, #0
	str	r8, [fp]
	ldrhi	r0, [r7, #4]
	ldrhi	r3, [r7, #40]
	addhi	r0, r0, #1024
	subhi	r0, r0, r3
	clzhi	r0, r0
	lsrhi	r0, r0, #5
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L132:
	.align	2
.L131:
	.word	.LANCHOR0
	.fnend
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	FlashReadStatusEN
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L146
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, [r3, r0, lsl #3]
	add	r0, r3, r0, lsl #3
	ldrb	r5, [r0, #4]	@ zero_extendqisi2
	ldr	r0, [r3, #48]
	ldrb	r0, [r0, #8]	@ zero_extendqisi2
	cmp	r0, #2
	mov	r0, r3
	lsl	r3, r5, #8
	movne	r2, #112
	add	r5, r5, #8
	addne	r3, r4, r3
	strne	r2, [r3, #2056]
	bne	.L139
	cmp	r2, #0
	add	r3, r4, r3
	ldrbne	r2, [r0, #66]	@ zero_extendqisi2
	ldrbeq	r2, [r0, #65]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [r0, #67]	@ zero_extendqisi2
	cmp	r0, #0
	movne	r2, #0
	addne	ip, r4, r5, lsl #8
	bne	.L138
.L139:
	mov	r0, #80
	bl	ndelay
	ldr	r0, [r4, r5, lsl #8]
	uxtb	r0, r0
	pop	{r4, r5, r6, pc}
.L140:
	lsl	r3, r2, #3
	add	r2, r2, #1
	lsr	r3, r1, r3
	uxtb	r3, r3
	str	r3, [ip, #4]
.L138:
	cmp	r2, r0
	bcc	.L140
	b	.L139
.L147:
	.align	2
.L146:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatusEN, .-FlashReadStatusEN
	.align	2
	.global	FlashWaitReadyEN
	.syntax unified
	.arm
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
.L149:
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L149
	tst	r0, #64
	popne	{r4, r5, r6, pc}
	mov	r1, #3
	mov	r0, #1
	bl	usleep_range
	b	.L149
	.fnend
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
	.align	2
	.global	FlashScheduleEnSet
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L156
	ldr	r2, [r3, #1212]
	str	r0, [r3, #1212]
	mov	r0, r2
	bx	lr
.L157:
	.align	2
.L156:
	.word	.LANCHOR0
	.fnend
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	2
	.global	FlashGetPageSize
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L159
	ldr	r3, [r3, #48]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L160:
	.align	2
.L159:
	.word	.LANCHOR0
	.fnend
	.size	FlashGetPageSize, .-FlashGetPageSize
	.align	2
	.global	NandcReadDontCaseBusyEn
	.syntax unified
	.arm
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
	.align	2
	.global	NandcGetChipIf
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcGetChipIf, %function
NandcGetChipIf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L163
	add	r3, r2, r0, lsl #3
	ldr	r0, [r2, r0, lsl #3]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r3, r3, #8
	add	r0, r0, r3, lsl #8
	bx	lr
.L164:
	.align	2
.L163:
	.word	.LANCHOR0
	.fnend
	.size	NandcGetChipIf, .-NandcGetChipIf
	.align	2
	.global	NandcSetDdrPara
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcSetDdrPara, %function
NandcSetDdrPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L166
	ldr	r2, [r3, #88]
	lsl	r3, r0, #8
	orr	r0, r3, r0, lsl #16
	orr	r0, r0, #1
	str	r0, [r2, #304]
	bx	lr
.L167:
	.align	2
.L166:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrDiv
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L169
	orr	r0, r0, #16640
	ldr	r3, [r3, #88]
	str	r0, [r3, #344]
	bx	lr
.L170:
	.align	2
.L169:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	2
	.global	NandcSetDdrMode
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L174
	cmp	r0, #0
	ldr	r2, [r3, #88]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L175:
	.align	2
.L174:
	.word	.LANCHOR0
	.fnend
	.size	NandcSetDdrMode, .-NandcSetDdrMode
	.align	2
	.global	NandcSetMode
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcSetMode, %function
NandcSetMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L183
	ands	r1, r0, #6
	ldr	r2, [r3, #88]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L179
	movw	r1, #8322
	orr	r3, r3, #24576
	str	r1, [r2, #344]
	bfc	r3, #15, #1
	ldr	r1, .L183+4
	orr	r3, r3, #196608
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L179:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L184:
	.align	2
.L183:
	.word	.LANCHOR0
	.word	1052675
	.fnend
	.size	NandcSetMode, .-NandcSetMode
	.align	2
	.global	NandcFlashCs
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcFlashCs, %function
NandcFlashCs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L186
	mov	r2, #1
	ldr	r1, [r3, r0, lsl #3]
	add	r0, r3, r0, lsl #3
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	ldr	r3, [r1]
	lsl	r2, r2, r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L187:
	.align	2
.L186:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashCs, .-NandcFlashCs
	.align	2
	.global	NandcFlashDeCs
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcFlashDeCs, %function
NandcFlashDeCs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L189
	ldr	r2, [r3, r0, lsl #3]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L190:
	.align	2
.L189:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashDeCs, .-NandcFlashDeCs
	.align	2
	.global	HynixSetRRPara
	.syntax unified
	.arm
	.fpu softvfp
	.type	HynixSetRRPara, %function
HynixSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r6, r0
	ldr	r0, .L200
	mov	r7, r3
	mov	r8, r1
	mov	r10, r2
	ldr	r3, [r0, #48]
	mov	r5, r0
	add	r4, r0, #1216
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #6
	moveq	r3, #20
	addeq	r3, r3, r6, lsl #6
	addeq	r3, r3, r7, lsl #2
	beq	.L199
	cmp	r3, #7
	bne	.L194
	mov	r3, #160
	mov	r2, #28
	smlabb	r2, r3, r6, r2
	mov	r3, #10
	smlabb	r3, r3, r7, r2
.L199:
	add	r4, r4, r3
.L193:
	add	r3, r5, r6, lsl #3
	ldr	r9, [r5, r6, lsl #3]
	mov	r0, r6
	ldrb	fp, [r3, #4]	@ zero_extendqisi2
	sub	r8, r8, #1
	bl	NandcFlashCs
	mov	r3, #54
	add	r8, r10, r8
	sub	r4, r4, #1
	lsl	fp, fp, #8
	add	r0, r9, fp
	str	r3, [r0, #2056]
	sub	r3, r10, #1
	mov	r10, r0
.L196:
	cmp	r3, r8
	bne	.L197
	mov	r3, #22
	add	r9, r9, fp
	mov	r0, r6
	str	r3, [r9, #2056]
	bl	NandcFlashDeCs
	add	r0, r5, r6
	strb	r7, [r0, #2068]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L194:
	cmp	r3, #8
	addeq	r4, r4, #28
	addeq	r3, r7, r7, lsl #2
	beq	.L199
	add	r3, r7, #2
	add	r3, r3, r6, lsl #3
	add	r4, r4, r3, lsl #3
	add	r4, r4, #4
	b	.L193
.L197:
	ldrb	r2, [r3, #1]!	@ zero_extendqisi2
	mov	r0, #200
	str	r2, [r10, #2052]
	str	r3, [sp, #4]
	bl	ndelay
	ldrsb	r2, [r4, #1]!
	ldr	r3, [sp, #4]
	str	r2, [r10, #2048]
	b	.L196
.L201:
	.align	2
.L200:
	.word	.LANCHOR0
	.fnend
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r5, .L209
	ldr	r3, [r5, #48]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	pophi	{r4, r5, r6, r7, r8, pc}
	ldr	r7, .L209+4
	add	r6, r5, #1216
	mov	r4, #0
	add	r6, r6, #4
.L205:
	ldrb	r3, [r7, r4, lsl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L204
	mov	r3, #0
	mov	r2, r6
	ldrb	r1, [r5, #1217]	@ zero_extendqisi2
	bl	HynixSetRRPara
.L204:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L205
	pop	{r4, r5, r6, r7, r8, pc}
.L210:
	.align	2
.L209:
	.word	.LANCHOR0
	.word	.LANCHOR0+2072
	.fnend
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	FlashWaitCmdDone
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashWaitCmdDone, %function
FlashWaitCmdDone:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r5, .L219
	add	r4, r5, r0, lsl #4
	ldr	r3, [r4, #2112]
	cmp	r3, #0
	beq	.L213
	ldrb	r7, [r4, #2104]	@ zero_extendqisi2
	mov	r6, r0
	add	r5, r5, r6, lsl #2
	mov	r0, r7
	bl	NandcFlashCs
	ldr	r2, [r5, #1180]
	mov	r0, r7
	ldr	r1, [r4, #2108]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r1, r0
	mov	r0, r7
	bl	NandcFlashDeCs
	ldr	r3, [r4, #2112]
	sbfx	r0, r1, #0, #1
	str	r0, [r3]
	mov	r3, #0
	ldr	r2, [r4, #2116]
	str	r3, [r4, #2112]
	cmp	r2, r3
	strne	r0, [r2]
	strne	r3, [r4, #2116]
.L213:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L220:
	.align	2
.L219:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.global	NandcDelayns
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcDelayns, %function
NandcDelayns:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	ndelay
	mov	r0, #0
	pop	{r4, pc}
	.fnend
	.size	NandcDelayns, .-NandcDelayns
	.align	2
	.global	NandcWaitFlashReadyNoDelay
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcWaitFlashReadyNoDelay, %function
NandcWaitFlashReadyNoDelay:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L229
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r4, .L229+4
	ldr	r5, [r3, r0, lsl #3]
.L225:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L226
	mov	r0, #10
	bl	ndelay
	subs	r4, r4, #1
	bne	.L225
	mvn	r0, #0
.L223:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L226:
	mov	r0, #0
	b	.L223
.L230:
	.align	2
.L229:
	.word	.LANCHOR0
	.word	100000
	.fnend
	.size	NandcWaitFlashReadyNoDelay, .-NandcWaitFlashReadyNoDelay
	.align	2
	.global	NandcWaitFlashReady
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r3, .L237
	ldr	r4, .L237+4
	ldr	r5, [r3, r0, lsl #3]
	mov	r0, #130
	bl	ndelay
.L233:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L234
	mov	r1, #2
	mov	r0, #1
	bl	usleep_range
	subs	r4, r4, #1
	bne	.L233
	mvn	r0, #0
.L231:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L234:
	mov	r0, #0
	b	.L231
.L238:
	.align	2
.L237:
	.word	.LANCHOR0
	.word	100000
	.fnend
	.size	NandcWaitFlashReady, .-NandcWaitFlashReady
	.align	2
	.global	FlashReset
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReset, %function
FlashReset:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L241
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r5, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	ldrb	r6, [r3, #4]	@ zero_extendqisi2
	bl	NandcFlashCs
	mov	r3, #255
	mov	r0, r4
	add	r5, r5, r6, lsl #8
	str	r3, [r5, #2056]
	bl	NandcWaitFlashReady
	mov	r0, r4
	pop	{r4, r5, r6, lr}
	b	NandcFlashDeCs
.L242:
	.align	2
.L241:
	.word	.LANCHOR0
	.fnend
	.size	FlashReset, .-FlashReset
	.align	2
	.global	flash_enter_slc_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_enter_slc_mode, %function
flash_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r5, .L250
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cmp	r3, #0
	popeq	{r4, r5, r6, r7, r8, pc}
	mov	r6, r0
	bl	NandcFlashCs
	add	r3, r5, r6, lsl #3
	ldr	r7, [r5, r6, lsl #3]
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	ldrb	r3, [r3, #2072]	@ zero_extendqisi2
	cmp	r3, #44
	lsl	r8, r8, #8
	bne	.L245
	add	r4, r7, r8
	mov	r3, #239
	str	r3, [r4, #2056]
	mov	r3, #145
	str	r3, [r4, #2052]
	mov	r0, #50
	bl	ndelay
	mov	r3, #0
	mov	r2, #1
	str	r3, [r4, #2048]
	mov	r0, #100
	str	r2, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	ndelay
.L245:
	mov	r0, r6
	add	r7, r7, r8
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #218
	mov	r0, r6
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #2
	strb	r3, [r5, #2232]
	pop	{r4, r5, r6, r7, r8, pc}
.L251:
	.align	2
.L250:
	.word	.LANCHOR0
	.fnend
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	2
	.global	flash_exit_slc_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r5, .L259
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cmp	r3, #0
	popeq	{r4, r5, r6, r7, r8, pc}
	mov	r6, r0
	bl	NandcFlashCs
	add	r3, r5, r6, lsl #3
	ldr	r7, [r5, r6, lsl #3]
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	ldrb	r3, [r3, #2072]	@ zero_extendqisi2
	cmp	r3, #44
	lsl	r8, r8, #8
	bne	.L254
	add	r4, r7, r8
	mov	r3, #239
	str	r3, [r4, #2056]
	mov	r3, #145
	str	r3, [r4, #2052]
	mov	r0, #50
	bl	ndelay
	mov	r3, #2
	mov	r0, #100
	str	r3, [r4, #2048]
	mov	r3, #1
	str	r3, [r4, #2048]
	mov	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	ndelay
.L254:
	mov	r0, r6
	add	r7, r7, r8
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #223
	mov	r0, r6
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #0
	strb	r3, [r5, #2232]
	pop	{r4, r5, r6, r7, r8, pc}
.L260:
	.align	2
.L259:
	.word	.LANCHOR0
	.fnend
	.size	flash_exit_slc_mode, .-flash_exit_slc_mode
	.align	2
	.global	FlashEraseBlock
	.syntax unified
	.arm
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
	.align	2
	.global	FlashSetInterfaceMode
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashSetInterfaceMode, %function
FlashSetInterfaceMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	lr, #0
	ldr	r4, .L286
	mov	r5, #239
	mov	r6, #128
	mov	r7, #1
	mov	r8, #35
	mov	r9, #32
	ldrb	r3, [r4, #2233]	@ zero_extendqisi2
	mov	r10, #5
	and	r2, r3, #4
	and	r3, r3, #1
	str	r2, [sp, #4]
	mov	r2, lr
	str	r3, [sp]
.L273:
	ldr	r1, .L286+4
	add	r3, r4, lr
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	ldrb	ip, [lr, r1]	@ zero_extendqisi2
	cmp	ip, #69
	cmpne	ip, #152
	beq	.L264
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L265
.L264:
	cmp	r0, #1
	ldr	r1, [r4, lr]
	bne	.L266
	ldr	fp, [sp]
	cmp	fp, #0
	beq	.L265
	lsl	r3, r3, #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r5, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L285
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r6, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
.L271:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L265:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L273
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L266:
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L265
	lsl	r3, r3, #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r5, [fp, #2056]
	streq	r7, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L271
	cmp	ip, #44
	streq	r7, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L271
	str	r6, [fp, #2052]
.L285:
	str	r2, [fp, #2048]
	b	.L271
.L287:
	.align	2
.L286:
	.word	.LANCHOR0
	.word	.LANCHOR0+2072
	.fnend
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	2
	.global	FlashReadSpare
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadSpare, %function
FlashReadSpare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L290
	ldr	r3, .L290+4
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r2
	ldr	r4, [ip, r0, lsl #3]
	add	ip, ip, r0, lsl #3
	ldrb	r3, [r3, #477]	@ zero_extendqisi2
	ldrb	r2, [ip, #4]	@ zero_extendqisi2
	lsl	r3, r3, #9
	add	r4, r4, r2, lsl #8
	mov	r2, #0
	str	r2, [r4, #2056]
	str	r3, [r4, #2052]
	lsr	r3, r3, #8
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	lsr	r3, r1, #8
	lsr	r1, r1, #16
	str	r3, [r4, #2052]
	mov	r3, #48
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #2048]
	strb	r3, [r5]
	pop	{r4, r5, r6, pc}
.L291:
	.align	2
.L290:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.fnend
	.size	FlashReadSpare, .-FlashReadSpare
	.align	2
	.global	SandiskProgTestBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	SandiskProgTestBadBlock, %function
SandiskProgTestBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L294
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, r0, lsl #3]
	add	r3, r3, r0, lsl #3
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r4, r4, r3, lsl #8
	mov	r3, #162
	str	r3, [r4, #2056]
	mov	r3, #128
	str	r3, [r4, #2056]
	mov	r3, #0
	str	r3, [r4, #2052]
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	lsr	r3, r1, #8
	lsr	r1, r1, #16
	str	r3, [r4, #2052]
	mov	r3, #16
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #112
	mov	r0, #80
	str	r3, [r4, #2056]
	bl	ndelay
	ldr	r0, [r4, #2048]
	and	r0, r0, #1
	pop	{r4, pc}
.L295:
	.align	2
.L294:
	.word	.LANCHOR0
	.fnend
	.size	SandiskProgTestBadBlock, .-SandiskProgTestBadBlock
	.align	2
	.global	SandiskSetRRPara
	.syntax unified
	.arm
	.fpu softvfp
	.type	SandiskSetRRPara, %function
SandiskSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #239
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	str	r3, [r0, #8]
	mov	r3, #17
	mov	r5, r0
	mov	r4, r1
	str	r3, [r0, #4]
	mov	r0, #200
	bl	ndelay
	ldr	r0, .L303
	add	r4, r4, r4, lsl #2
	ldr	r1, .L303+4
	mov	r2, #0
	sub	ip, r0, #45
.L297:
	ldrb	r3, [r1, #85]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L300
	mov	r0, #0
	pop	{r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L300:
	ldrb	r3, [r1, #84]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	ldrsb	r3, [r3, #5]
	add	r2, r2, #1
	str	r3, [r5]
	b	.L297
.L304:
	.align	2
.L303:
	.word	.LANCHOR1+301
	.word	.LANCHOR0
	.fnend
	.size	SandiskSetRRPara, .-SandiskSetRRPara
	.align	2
	.global	micron_auto_read_calibration_config
	.syntax unified
	.arm
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
	ldr	r0, .L307
	ldr	r4, [r0, r5, lsl #3]
	add	r0, r0, r5, lsl #3
	ldrb	r3, [r0, #4]	@ zero_extendqisi2
	mov	r0, #200
	add	r4, r4, r3, lsl #8
	mov	r3, #239
	str	r3, [r4, #2056]
	mov	r3, #150
	str	r3, [r4, #2052]
	bl	ndelay
	mov	r3, #0
	str	r6, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	pop	{r4, r5, r6, pc}
.L308:
	.align	2
.L307:
	.word	.LANCHOR0
	.fnend
	.size	micron_auto_read_calibration_config, .-micron_auto_read_calibration_config
	.align	2
	.global	FlashEraseSLc2KBlocks
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseSLc2KBlocks, %function
FlashEraseSLc2KBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #16
	mov	r5, #0
	ldr	r8, .L320
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
	ldr	r10, .L320+4
.L310:
	cmp	r7, r9
	bne	.L315
	mov	r0, #0
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L315:
	sub	r3, r9, r7
	add	r2, sp, #8
	uxtb	r3, r3
	mov	r1, #0
	add	r0, r6, r5
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2234]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r2, r3
	mvnls	r3, #0
	strls	r3, [r6, r5]
	bls	.L312
	add	r2, r8, r3
	add	r3, r8, r3, lsl #4
	ldrb	r4, [r2, #2236]	@ zero_extendqisi2
	strb	r4, [r3, #2104]
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r2, #0
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
	mov	r2, #0
	ldr	r3, [r8, #40]
	mov	r0, r4
	add	r1, r1, r3
	bl	FlashEraseCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldr	r1, [sp, #8]
	bl	FlashReadStatus
	tst	r0, #1
	mvnne	r3, #0
	strne	r3, [r6, r5]
	ldr	r3, [r6, r5]
	cmn	r3, #1
	bne	.L314
	ldr	r1, [sp, #8]
	mov	r0, r10
	bl	printk
.L314:
	mov	r0, r4
	bl	NandcFlashDeCs
.L312:
	add	r7, r7, #1
	add	r5, r5, #36
	b	.L310
.L321:
	.align	2
.L320:
	.word	.LANCHOR0
	.word	.LC1
	.fnend
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	2
	.global	FlashEraseBlocks
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r4, .L355
	.pad #20
	sub	sp, sp, #20
	ldrb	r5, [r4, #36]	@ zero_extendqisi2
	cmp	r5, #0
	moveq	r9, r0
	moveq	r10, r1
	beq	.L324
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
.L322:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L333:
	mov	r3, #36
	add	r2, sp, #8
	mul	r6, r3, r5
	sub	r3, r8, r5
	uxtb	r3, r3
	mov	r1, #0
	str	r3, [sp]
	add	r3, sp, #12
	add	fp, r9, r6
	mov	r0, fp
	bl	LogAddr2PhyAddr
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #12]
	cmp	r3, r0
	mvnls	r3, #0
	strls	r3, [r9, r6]
	bls	.L327
	ldrb	r3, [r4, #2244]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, lsl #4
	moveq	r7, #0
	ldr	r3, [r3, #2112]
	cmp	r3, #0
	beq	.L329
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L329:
	ldr	r2, [sp, #12]
	cmp	r7, #0
	addne	r6, r6, #36
	mov	r0, #0
	addne	r6, r9, r6
	lsl	r3, r2, #4
	add	r2, r4, r2
	add	r1, r4, r3
	add	r3, r4, r3
	str	r0, [r1, #2116]
	ldr	r0, [sp, #8]
	strne	r6, [r1, #2116]
	ldrb	r6, [r2, #2236]	@ zero_extendqisi2
	str	r0, [r1, #2108]
	str	fp, [r1, #2112]
	mov	r0, r6
	strb	r6, [r3, #2104]
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L331
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L331
	bl	flash_enter_slc_mode
.L332:
	ldr	r3, [sp, #12]
	mov	r0, r6
	ldr	r1, [sp, #8]
	add	r5, r5, r7
	add	r3, r4, r3, lsl #2
	ldr	r2, [r3, #1180]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r2, r7
	ldr	r1, [sp, #8]
	mov	r0, r6
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
.L327:
	add	r5, r5, #1
.L324:
	cmp	r5, r8
	bcc	.L333
	ldr	r6, .L355+4
	mov	r5, #0
.L334:
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L336
	ldr	r3, [r4, #2248]
	cmp	r3, #0
	bne	.L337
.L338:
	mov	r0, #0
	b	.L322
.L331:
	bl	flash_exit_slc_mode
	b	.L332
.L336:
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L335
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L335
	ldrb	r0, [r6, r5, lsl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L335:
	add	r5, r5, #1
	b	.L334
.L337:
	ldrb	r3, [r4, #2072]	@ zero_extendqisi2
	cmp	r3, #69
	moveq	r3, #0
	moveq	r2, #36
	moveq	r1, r3
	bne	.L338
.L339:
	cmp	r3, r8
	beq	.L338
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L339
.L356:
	.align	2
.L355:
	.word	.LANCHOR0
	.word	.LANCHOR0+2104
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	FlashReadDpCmd
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r7, r0
	ldr	r0, .L363
	mov	r8, r1
	uxtb	r10, r2
	lsr	r9, r2, #8
	lsr	r6, r2, #16
	uxtb	lr, r8
	ldr	r2, [r0, #48]
	lsr	ip, r8, #8
	add	r1, r0, r7, lsl #3
	ldr	r3, [r0, r7, lsl #3]
	ldrb	r4, [r1, #4]	@ zero_extendqisi2
	ldrb	r1, [r0, #68]	@ zero_extendqisi2
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	cmp	r1, #1
	lsl	r4, r4, #8
	lsr	r1, r8, #16
	bne	.L358
	cmp	r2, #1
	addeq	r2, r3, r4
	moveq	r5, #38
	add	r4, r3, r4
	streq	r5, [r2, #2056]
	ldrb	r3, [r0, #61]	@ zero_extendqisi2
	mov	r5, #0
	ldrb	r2, [r0, #60]	@ zero_extendqisi2
	mov	r0, r7
	str	r2, [r4, #2056]
	str	r5, [r4, #2052]
	str	r5, [r4, #2052]
	str	lr, [r4, #2052]
	str	ip, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #48
	str	r5, [r4, #2056]
	str	r5, [r4, #2052]
	str	r5, [r4, #2052]
	str	r10, [r4, #2052]
	str	r9, [r4, #2052]
	str	r6, [r4, #2052]
	str	r3, [r4, #2056]
.L360:
	mov	r1, r8
	mov	r0, r7
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L358:
	cmp	r2, #1
	addeq	r2, r3, r4
	moveq	r5, #38
	streq	r5, [r2, #2056]
	add	r3, r3, r4
	ldrb	r2, [r0, #60]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r2, [r0, #61]	@ zero_extendqisi2
	str	lr, [r3, #2052]
	str	ip, [r3, #2052]
	str	r1, [r3, #2052]
	str	r2, [r3, #2056]
	mov	r2, #48
	str	r10, [r3, #2052]
	str	r9, [r3, #2052]
	str	r6, [r3, #2052]
	str	r2, [r3, #2056]
	b	.L360
.L364:
	.align	2
.L363:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	2
	.global	ftl_flash_de_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_flash_de_init, %function
ftl_flash_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r0, #0
	ldr	r4, .L376
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r0, [r4, #2252]
	cmp	r0, #0
	beq	.L366
	mov	r0, #0
	bl	flash_enter_slc_mode
.L367:
	ldrb	r3, [r4, #2256]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L368
	ldrb	r3, [r4, #2233]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L368
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r4, #2256]
.L368:
	ldr	r3, [r4]
	mov	r0, #0
	str	r0, [r3, #336]
	pop	{r4, pc}
.L366:
	bl	flash_exit_slc_mode
	b	.L367
.L377:
	.align	2
.L376:
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_de_init, .-ftl_flash_de_init
	.align	2
	.global	NandcRandmzSel
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcRandmzSel, %function
NandcRandmzSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L379
	ldr	r3, [r3, r0, lsl #3]
	str	r1, [r3, #336]
	bx	lr
.L380:
	.align	2
.L379:
	.word	.LANCHOR0
	.fnend
	.size	NandcRandmzSel, .-NandcRandmzSel
	.global	__aeabi_idiv
	.align	2
	.global	NandcTimeCfg
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcTimeCfg, %function
NandcTimeCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	mov	r0, #0
	bl	rknand_get_clk_rate
	ldr	r1, .L392
	bl	__aeabi_idiv
	ldr	r3, .L392+4
	cmp	r0, #250
	movwgt	r2, #8354
	ldr	r3, [r3, #88]
	bgt	.L390
	cmp	r0, #220
	ble	.L384
.L391:
	movw	r2, #8322
	b	.L390
.L384:
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L390
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L390
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L390
	cmp	r4, #99
	movwls	r2, #4225
	bhi	.L391
.L390:
	str	r2, [r3, #4]
	pop	{r4, pc}
.L393:
	.align	2
.L392:
	.word	1000000
	.word	.LANCHOR0
	.fnend
	.size	NandcTimeCfg, .-NandcTimeCfg
	.align	2
	.global	FlashTimingCfg
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashTimingCfg, %function
FlashTimingCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	sub	r3, r0, #4160
	sub	r3, r3, #33
	bic	r3, r3, #32
	cmp	r3, #1
	bls	.L395
	movw	r3, #8322
	cmp	r0, r3
	bne	.L396
.L395:
	ldr	r3, .L397
	ldr	r3, [r3, #88]
	str	r0, [r3, #4]
.L396:
	ldr	r3, .L397+4
	ldrb	r0, [r3, #489]	@ zero_extendqisi2
	b	NandcTimeCfg
.L398:
	.align	2
.L397:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.fnend
	.size	FlashTimingCfg, .-FlashTimingCfg
	.align	2
	.global	NandcInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcInit, %function
NandcInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L402
	mov	r2, #1
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #0
	mov	r5, #0
	mov	r4, r3
	str	r2, [r3, #12]
	mov	r2, #2
	str	r2, [r3, #20]
	mov	r2, #3
	stm	r3, {r0, r1}
	str	r0, [r3, #8]
	str	r0, [r3, #16]
	str	r0, [r3, #24]
	str	r0, [r3, #88]
	str	r2, [r3, #28]
	ldr	r2, [r0]
	and	r2, r2, #253952
	ubfx	ip, r2, #13, #1
	bfi	r2, r1, #13, #1
	ldr	r1, [r0, #352]
	orr	r2, r2, #256
	str	ip, [r3, #2260]
	movw	ip, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r3, #2264]
	ldr	r1, [r0, #352]
	cmp	r1, ip
	str	r1, [r3, #2268]
	moveq	r3, #8
	streq	r3, [r4, #2264]
	str	r2, [r0]
	mov	r0, #40
	ldr	r3, [r4, #88]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #88]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L402+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r0, [r4, #2272]
	str	r0, [r4, #2276]
	add	r0, r0, #32768
	str	r0, [r4, #2280]
	str	r5, [r4, #2300]
	str	r5, [r4, #2308]
	pop	{r4, r5, r6, pc}
.L403:
	.align	2
.L402:
	.word	.LANCHOR0
	.word	1579009
	.fnend
	.size	NandcInit, .-NandcInit
	.align	2
	.global	NandcGetTimeCfg
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L406
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	lr, [ip, #88]
	ldr	lr, [lr, #4]
	str	lr, [r0]
	ldr	r0, [ip, #88]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [ip, #88]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [ip, #88]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, lsl #16
	str	r2, [r3]
	ldr	pc, [sp], #4
.L407:
	.align	2
.L406:
	.word	.LANCHOR0
	.fnend
	.size	NandcGetTimeCfg, .-NandcGetTimeCfg
	.align	2
	.global	NandcBchSel
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcBchSel, %function
NandcBchSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L416
	mov	ip, #1
	mov	r1, #0
	ldr	r2, [r3, #88]
	str	r0, [r3, #2312]
	mov	r3, r1
	str	ip, [r2, #8]
	mov	ip, #16
	cmp	r0, ip
	bfi	r3, ip, #8, #8
	bfi	r3, r1, #18, #1
	bne	.L409
.L412:
	bfc	r3, #4, #1
.L410:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L409:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L410
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L410
	b	.L412
.L417:
	.align	2
.L416:
	.word	.LANCHOR0
	.fnend
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashBchSel, %function
FlashBchSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L419
	strb	r0, [r3, #2316]
	b	NandcBchSel
.L420:
	.align	2
.L419:
	.word	.LANCHOR0
	.fnend
	.size	FlashBchSel, .-FlashBchSel
	.align	2
	.global	ftl_flash_resume
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_flash_resume, %function
ftl_flash_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L430
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r6, .L430+4
	mov	r4, r3
	ldr	r2, [r3, #88]
	ldr	r1, [r3, #92]
	str	r1, [r2]
	ldr	r1, [r3, #96]
	ldr	r2, [r3, #88]
	str	r1, [r2, #4]
	ldr	r1, [r3, #100]
	str	r1, [r2, #8]
	ldr	r1, [r3, #104]
	str	r1, [r2, #12]
	ldr	r1, [r3, #108]
	str	r1, [r2, #304]
	ldr	r1, [r3, #112]
	str	r1, [r2, #308]
	ldr	r1, [r3, #116]
	str	r1, [r2, #336]
	ldr	r1, [r3, #120]
	str	r1, [r2, #344]
.L423:
	ldrb	r3, [r6, r5, lsl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L422
	uxtb	r0, r5
	bl	FlashReset
.L422:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L423
	ldrb	r3, [r4, #2256]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L424
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r4, #109]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L424:
	ldr	r3, [r4, #48]
	pop	{r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L431:
	.align	2
.L430:
	.word	.LANCHOR0
	.word	.LANCHOR0+2072
	.fnend
	.size	ftl_flash_resume, .-ftl_flash_resume
	.align	2
	.global	ftl_nandc_get_irq_status
	.syntax unified
	.arm
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
	.align	2
	.global	rk_nandc_flash_ready
	.syntax unified
	.arm
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
	.align	2
	.global	NandcIqrWaitFlashReady
	.syntax unified
	.arm
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
	.align	2
	.global	rk_nandc_flash_xfer_completed
	.syntax unified
	.arm
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
	.align	2
	.global	NandcSendDumpDataStart
	.syntax unified
	.arm
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
	ldr	r3, .L438
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
.L439:
	.align	2
.L438:
	.word	538969130
	.fnend
	.size	NandcSendDumpDataStart, .-NandcSendDumpDataStart
	.align	2
	.global	NandcSendDumpDataDone
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcSendDumpDataDone, %function
NandcSendDumpDataDone:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.pad #8
	sub	sp, sp, #8
.L441:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L441
	add	sp, sp, #8
	@ sp needed
	bx	lr
	.fnend
	.size	NandcSendDumpDataDone, .-NandcSendDumpDataDone
	.align	2
	.global	NandcXferStart
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcXferStart, %function
NandcXferStart:
	.fnstart
	@ args = 8, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	ip, #16
	ldr	r4, .L463
	mov	r5, #0
	.pad #20
	sub	sp, sp, #20
	ldr	r8, [sp, #56]
	ldr	r6, [r4, r0, lsl #3]
	add	r0, r4, r0, lsl #3
	ldr	r7, [r6, #12]
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	bfi	r7, ip, #8, #8
	bfi	r7, r5, #3, #1
	bfi	r5, r1, #1, #1
	bfi	r7, r0, #5, #3
	orr	r5, r5, #8
	mov	r0, #1
	bfi	r5, r0, #5, #2
	lsr	r3, r3, r0
	orr	r5, r5, #536870912
	orr	r5, r5, #1024
	bfi	r5, r3, #4, #1
	ldr	r3, [r4, #2264]
	cmp	r3, #3
	bls	.L446
	ldr	r3, [r6, #16]
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	ldr	r3, [sp, #60]
	cmp	r8, #0
	cmpeq	r3, #0
	beq	.L447
	cmp	r1, #0
	bne	.L448
.L456:
	add	r2, r2, #1
	cmp	r8, #0
	asr	r2, r2, #1
	movne	r0, r8
	bfi	r5, r2, #22, #6
	ldreq	r0, [r4, #2276]
.L450:
	ldr	r3, [r4, #2280]
	ubfx	r10, r5, #22, #5
	mov	r9, r1
	mov	r2, r1
	lsl	r1, r10, #10
	str	r0, [r4, #2284]
	str	r3, [r4, #2288]
	bl	rknand_dma_map_single
	mov	r2, r9
	str	r0, [r4, #2292]
	lsl	r1, r10, #7
	ldr	r0, [r4, #2288]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r0, [r4, #2296]
	str	r3, [r4, #2300]
	mov	r2, #16
	ldr	r3, [r4, #2292]
	tst	r8, #3
	clz	r1, r9
	lsr	r1, r1, #5
	str	r3, [r6, #20]
	ldr	r3, [r4, #2296]
	str	r3, [r6, #24]
	mov	r3, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfi	r3, r2, #9, #5
	moveq	r2, #2
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #448
	str	r3, [sp, #12]
	ldreq	r3, [sp, #12]
	bfieq	r3, r2, #3, #3
	streq	r3, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #4
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfi	r3, r1, #1, #1
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #1
	str	r3, [sp, #12]
.L447:
	ldr	r3, [sp, #12]
	str	r3, [r6, #16]
.L446:
	str	r7, [r6, #12]
	str	r5, [r6, #8]
	orr	r5, r5, #4
	str	r5, [r6, #8]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L448:
	ldr	r3, [r4, #2312]
	lsr	r10, r2, #1
	ldr	ip, [sp, #60]
	cmp	r3, #25
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp, #4]
	mov	r3, #0
	mov	r0, r3
.L452:
	cmp	r0, r10
	bcs	.L456
	ldr	lr, [sp, #60]
	add	r0, r0, #1
	cmp	lr, #0
	bic	lr, r3, #3
	ldrne	fp, [ip], #4	@ unaligned
	mvneq	r9, #0
	ldrne	r9, [r4, #2280]
	ldreq	fp, [r4, #2280]
	strne	fp, [r9, lr]
	streq	r9, [fp, lr]
	ldr	lr, [sp, #4]
	add	r3, r3, lr
	b	.L452
.L464:
	.align	2
.L463:
	.word	.LANCHOR0
	.fnend
	.size	NandcXferStart, .-NandcXferStart
	.align	2
	.global	Ftl_log2
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #0
	mov	r2, #1
.L466:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L467
	sub	r0, r3, #1
	uxth	r0, r0
	bx	lr
.L467:
	lsl	r2, r2, #1
	b	.L466
	.fnend
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
	.syntax unified
	.arm
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
	.align	2
	.global	FtlSysBlkNumInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L470
	movw	r2, #2324
	movw	r1, #2334
	cmp	r0, #24
	movcc	r0, #24
	ldrh	r2, [r3, r2]
	ldrh	r1, [r3, r1]
	str	r0, [r3, #2320]
	mul	r2, r0, r2
	sub	r0, r1, r0
	movw	r1, #2332
	strh	r0, [r3, r1]	@ movhi
	mov	r0, #0
	ldr	r1, [r3, #2340]
	str	r2, [r3, #2328]
	sub	r2, r1, r2
	str	r2, [r3, #2336]
	bx	lr
.L471:
	.align	2
.L470:
	.word	.LANCHOR0
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2344
	ldr	r4, .L500
	movw	r1, #2348
	mov	r5, r0
	.pad #20
	sub	sp, sp, #20
	ldrh	r6, [r0, #8]
	ldrh	r2, [r0, #10]
	ldrh	lr, [r0, #14]
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #2346
	strh	r2, [r4, r3]	@ movhi
	ldrh	r3, [r0, #12]
	ldr	r0, .L500+4
	strh	r3, [r4, r1]	@ movhi
	movw	r1, #2334
	strh	lr, [r4, r1]	@ movhi
	mov	r1, #0
.L473:
	strb	r1, [r1, r0]
	add	r1, r1, #1
	cmp	r1, #32
	bne	.L473
	ldrh	r0, [r5, #14]
	ldrh	r1, [r5, #20]
	cmp	r1, r0, lsr #8
	bcs	.L474
	uxtb	r10, r3
	ldr	r9, .L500+4
	lsl	r1, r10, #1
	uxtb	r1, r1
	str	r1, [sp]
	sub	r1, r2, #1
	mul	r1, r3, r1
	str	r1, [sp, #8]
	mov	r1, #0
.L475:
	cmp	r1, r3
	bcs	.L477
	ldr	ip, [sp, #8]
	sub	r7, r1, r3
	add	r7, r9, r7
	uxtb	r0, r1
	str	r7, [sp, #12]
	add	ip, r1, ip
	add	ip, r9, ip
	str	ip, [sp, #4]
	mov	ip, #0
	mov	r8, ip
	b	.L478
.L476:
	ldr	r7, [sp, #12]
	add	fp, r10, r0
	add	r8, r8, #1
	strb	r0, [r7, ip]
	ldr	r7, [sp, #4]
	strb	fp, [r7, ip]
	ldr	r7, [sp]
	add	r0, r7, r0
	uxtb	r0, r0
.L478:
	cmp	r8, r2
	add	ip, ip, r3
	bcc	.L476
	add	r1, r1, #1
	b	.L475
.L477:
	lsl	r2, r2, #1
	movw	r1, #2346
	lsr	lr, lr, #1
	strh	r2, [r4, r1]	@ movhi
	movw	r2, #2334
	strh	lr, [r4, r2]	@ movhi
.L474:
	ldr	fp, .L500+8
	movw	r2, #2382
	ldrb	r9, [r4, #36]	@ zero_extendqisi2
	mov	r1, #5
	strh	r1, [r4, r2]	@ movhi
	cmp	r6, #1
	mov	r1, #0
	strheq	r6, [r4, r2]	@ movhi
	strh	r1, [fp]	@ movhi
	cmp	r9, #0
	mov	r1, #4352
	movw	r2, #2386
	strh	r1, [r4, r2]	@ movhi
	movne	r1, #384
	strhne	r1, [r4, r2]	@ movhi
	movw	r2, #2346
	ldrh	r7, [r4, r2]
	movw	r2, #2324
	ldrh	r8, [r5, #16]
	ldrh	r1, [r5, #18]
	smulbb	r7, r7, r3
	str	r1, [sp, #4]
	uxth	r7, r7
	strh	r7, [r4, r2]	@ movhi
	movw	r2, #2334
	ldrh	r6, [r4, r2]
	movw	r2, #2388
	smulbb	r3, r3, r6
	strh	r3, [r4, r2]	@ movhi
	smulbb	r2, r7, r8
	movw	r3, #2390
	strh	r8, [r4, r3]	@ movhi
	movw	r3, #2392
	strh	r1, [r4, r3]	@ movhi
	movw	r3, #2394
	strh	r2, [r4, r3]	@ movhi
	movw	r2, #2396
	ldrh	r3, [r5, #20]
	mov	r0, r3
	strh	r3, [r4, r2]	@ movhi
	str	r3, [sp]
	bl	Ftl_log2
	ldr	r3, [sp]
	movw	r2, #2398
	strh	r0, [r4, r2]	@ movhi
	mov	r10, r0
	ldr	r0, .L500+12
	cmp	r6, #1024
	ldr	r1, [sp, #4]
	lsl	r2, r3, #9
	uxth	r2, r2
	mul	r1, r3, r1
	strh	r2, [r0]	@ movhi
	lsr	r2, r2, #8
	movw	r0, #2402
	strh	r2, [r4, r0]	@ movhi
	movw	r2, #2404
	ldrh	r0, [r5, #26]
	strh	r0, [r4, r2]	@ movhi
	mul	r2, r6, r7
	str	r2, [r4, #2340]
	uxtbhi	r2, r6
	strhhi	r2, [fp]	@ movhi
	ldrh	r2, [fp]
	sub	r2, r6, r2
	lsl	r6, r6, #6
	mul	r2, r7, r2
	mul	r2, r3, r2
	mul	r8, r8, r2
	movw	r2, #2386
	ldrh	r0, [r4, r2]
	asr	r8, r8, #11
	lsl	r0, r0, #3
	str	r8, [r4, #2408]
	bl	__aeabi_idiv
	uxth	r0, r0
	movw	r3, #2412
	mov	r1, r7
	cmp	r0, #4
	movls	r2, #4
	strhhi	r0, [r4, r3]	@ movhi
	strhls	r2, [r4, r3]	@ movhi
	cmp	r9, #0
	movne	r2, #640
	movwne	r3, #2386
	strhne	r2, [r4, r3]	@ movhi
	movw	r3, #2386
	ldrh	r3, [r4, r3]
	movw	r2, #2414
	asr	r3, r3, r10
	add	r10, r10, #9
	asr	r6, r6, r10
	add	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
	ldr	r3, .L500+16
	strh	r6, [r3]	@ movhi
	uxth	r6, r6
	mul	r3, r6, r7
	add	r6, r6, #8
	str	r3, [r4, #2420]
	movw	r3, #2412
	ldrh	r0, [r4, r3]
	bl	__aeabi_uidiv
	uxtah	r0, r6, r0
	cmp	r7, #1
	add	r3, r4, #2320
	addeq	r0, r0, #4
	str	r0, [r4, #2320]
	ldrh	r0, [r3]
	bl	FtlSysBlkNumInit
	ldr	r5, [r4, #2336]
	movw	r2, #2390
	ldr	r3, [r4, #2320]
	mov	r0, #2048
	str	r3, [r4, #2424]
	lsl	r3, r5, #2
	ldrh	r5, [r4, r2]
	mul	r5, r5, r3
	movw	r3, #2398
	ldrh	r3, [r4, r3]
	add	r3, r3, #9
	lsr	r5, r5, r3
	movw	r3, #2428
	add	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #2396
	ldrh	r6, [r4, r3]
	mov	r1, r6
	bl	__aeabi_idiv
	movw	r1, #2412
	movw	r3, #2430
	ldrh	r2, [r4, r1]
	strh	r0, [r4, r3]	@ movhi
	mov	r3, #0
	str	r3, [r4, #2432]
	ldrb	ip, [r4, #152]	@ zero_extendqisi2
	add	r3, r2, #3
	strh	r3, [r4, r1]	@ movhi
	ldr	r3, [r4, #2420]
	cmp	ip, #0
	addne	r2, r2, #4
	add	r0, r3, #3
	strhne	r2, [r4, r1]	@ movhi
	str	r0, [r4, #2420]
	addne	r3, r3, #5
	bne	.L499
	cmp	r0, #7
	bhi	.L488
	mov	r3, #8
.L499:
	str	r3, [r4, #2420]
.L488:
	movw	r2, #2436
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	movw	r3, #2332
	ldrh	r1, [r4, r3]
	mov	r0, #0
	lsr	r3, r1, #3
	add	r3, r3, r1, lsl #1
	add	r3, r3, #52
	add	r5, r3, r5, lsl #2
	cmp	r5, r6, lsl #9
	movcc	r3, #1
	strhcc	r3, [r4, r2]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L501:
	.align	2
.L500:
	.word	.LANCHOR0
	.word	.LANCHOR0+2350
	.word	.LANCHOR0+2384
	.word	.LANCHOR0+2400
	.word	.LANCHOR0+2416
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L509
	movw	r3, #2438
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L508
	movw	r1, #2412
	ldr	r3, [r2, #2440]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, lsl #1
.L504:
	cmp	r3, r2
	bne	.L505
.L508:
	mov	r0, #0
	bx	lr
.L505:
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L504
	mov	r0, #1
	bx	lr
.L510:
	.align	2
.L509:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheMetchLpa
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L521
	ldr	r3, [r2, #2444]
	cmp	r3, #0
	beq	.L514
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #2448]
	mov	r2, #0
.L513:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r1
	movhi	ip, #0
	movls	ip, #1
	cmp	lr, r0
	movcc	ip, #0
	cmp	ip, #0
	bne	.L515
	add	r2, r2, #1
	cmp	r3, r2
	bne	.L513
	mov	r0, ip
	pop	{r4, r5, pc}
.L514:
	mov	r0, r3
	bx	lr
.L515:
	mov	r0, #1
	pop	{r4, r5, pc}
.L522:
	.align	2
.L521:
	.word	.LANCHOR0
	.fnend
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGetCap, %function
FtlGetCap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L524
	ldr	r0, [r3, #2432]
	bx	lr
.L525:
	.align	2
.L524:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	FtlGetCapacity
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L527
	ldr	r0, [r3, #2432]
	bx	lr
.L528:
	.align	2
.L527:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	ftl_get_density
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_density, %function
ftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L530
	ldr	r0, [r3, #2432]
	bx	lr
.L531:
	.align	2
.L530:
	.word	.LANCHOR0
	.fnend
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGetLpn, %function
FtlGetLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L533
	ldr	r0, [r3, #2452]
	bx	lr
.L534:
	.align	2
.L533:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLpn, .-FtlGetLpn
	.align	2
	.global	FtlBbmMapBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	movw	r3, #2388
	ldr	r4, .L537
	mov	r5, r0
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	ldr	r2, [r2, #2484]
	lsr	r1, r3, #5
	and	ip, r3, #31
	mov	lr, #1
	ldr	r0, [r2, r1, lsl #2]
	orr	r0, r0, lr, lsl ip
	str	r0, [r2, r1, lsl #2]
	mov	r2, r6
	str	r0, [sp]
	mov	r1, r5
	ldr	r0, .L537+4
	bl	printk
	add	r3, r4, #2448
	mov	r0, #0
	ldrh	r2, [r3, #14]
	add	r2, r2, #1
	strh	r2, [r3, #14]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L538:
	.align	2
.L537:
	.word	.LANCHOR0
	.word	.LC2
	.fnend
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #2388
	ldr	r5, .L541
	mov	r7, r0
	ldrh	r6, [r5, r3]
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r7
	uxth	r4, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	r2, r4, #5
	add	r5, r5, r0, lsl #2
	and	r4, r4, #31
	ldr	r3, [r5, #2484]
	ldr	r0, [r3, r2, lsl #2]
	lsr	r0, r0, r4
	and	r0, r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L542:
	.align	2
.L541:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
	.syntax unified
	.arm
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
	.align	2
	.global	FtlBbtCalcTotleCnt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L552
	movw	r2, #2388
	movw	r1, #2346
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, r2]
	ldrh	r6, [r3, r1]
	mul	r6, r6, r2
.L545:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L547
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L547:
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L545
.L553:
	.align	2
.L552:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	V2P_block, %function
V2P_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #2348
	ldr	r4, .L556
	mov	r5, r1
	mov	r7, r0
	ldrh	r6, [r4, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	movw	r3, #2388
	smulbb	r5, r6, r5
	ldrh	r4, [r4, r3]
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
	add	r0, r5, r1
	add	r0, r4, r0
	uxth	r0, r0
	pop	{r4, r5, r6, r7, r8, pc}
.L557:
	.align	2
.L556:
	.word	.LANCHOR0
	.fnend
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L560
	movw	r2, #2348
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r3, r2]
	movw	r2, #2388
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r4, r1
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L561:
	.align	2
.L560:
	.word	.LANCHOR0
	.fnend
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movw	r3, #2388
	ldr	r4, .L564
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #2348
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L565:
	.align	2
.L564:
	.word	.LANCHOR0
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, r1
	bls	.L567
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L567:
	sub	r0, r1, r0
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L570
	ldrh	r0, [r3, #6]
	clz	r0, r0
	lsr	r0, r0, #5
	bx	lr
.L571:
	.align	2
.L570:
	.word	.LANCHOR0+2516
	.fnend
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L573
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	bx	lr
.L574:
	.align	2
.L573:
	.word	.LANCHOR0+2516
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bxhi	lr
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r4, .L588
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	popeq	{r4, r5, r6, r7, r8, pc}
	cmp	r1, #0
	mov	r5, r0
	beq	.L577
	ldr	r6, .L588+4
	ldr	r3, [r6, #-3612]
	cmp	r3, #0
	bne	.L577
	bl	P2V_block_in_plane
	mov	r7, r0
	ldr	r0, [r6, #-3608]
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r6, #-3604]
	lsl	r0, r7, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	ldr	r3, [r6, #-3600]
	add	r3, r3, #1
	str	r3, [r6, #-3600]
.L577:
	ldrh	r3, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	ldrh	r3, [r4, #4]
	add	r2, r4, r3, lsl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #8]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L589:
	.align	2
.L588:
	.word	.LANCHOR0+2516
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBLkSort
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L603
	ldrh	r2, [r3, #6]
	cmp	r2, #0
	bxeq	lr
	ldr	r2, .L603+4
	mov	r0, #0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldrh	r1, [r3, #2]
	ldrh	lr, [r2, #28]
	ldrh	r2, [r3, #4]
	and	lr, lr, #31
.L592:
	uxth	ip, r0
	add	r0, r0, #1
	cmp	lr, ip
	bgt	.L593
	cmp	r4, #0
	strhne	r1, [r3, #2]	@ movhi
	strhne	r2, [r3, #4]	@ movhi
	pop	{r4, pc}
.L593:
	add	ip, r3, r1, lsl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [ip, #8]
	add	ip, r3, r2, lsl #1
	strh	r4, [ip, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L592
.L604:
	.align	2
.L603:
	.word	.LANCHOR0+2516
	.word	.LANCHOR2-3596
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L616
	ldr	r8, .L616+4
	mov	r7, r4
.L606:
	ldrh	r1, [r4, #6]
	cmp	r1, #0
	beq	.L607
	ldr	r5, .L616+8
	sub	r1, r1, #1
	ldrh	r3, [r4, #2]
	strh	r1, [r4, #6]	@ movhi
	ldr	r10, [r5, #-3612]
	add	r2, r4, r3, lsl #1
	add	r3, r3, #1
	cmp	r10, #0
	ubfx	r3, r3, #0, #10
	ldrh	r6, [r2, #8]
	strh	r3, [r4, #2]	@ movhi
	bne	.L608
	mov	r0, r6
	bl	P2V_block_in_plane
	mov	r9, r0
	ldr	r0, [r5, #-3608]
	lsl	r3, r6, #10
	str	r3, [r0, #4]
	ldrb	r3, [r8, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L609
	mov	r2, #1
	mov	r1, r10
	bl	FlashEraseBlocks
.L609:
	mov	r2, #1
	ldr	r0, [r5, #-3608]
	mov	r1, r2
	bl	FlashEraseBlocks
	ldr	r2, [r5, #-3604]
	lsl	r0, r9, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	ldr	r3, [r5, #-3600]
	add	r3, r3, #1
	str	r3, [r5, #-3600]
.L608:
	sub	r3, r6, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L611
	ldrh	r2, [r7, #6]
	mov	r1, r6
	ldr	r0, .L616+12
	bl	printk
	b	.L606
.L607:
	ldr	r0, .L616+16
	bl	printk
.L610:
	b	.L610
.L611:
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L617:
	.align	2
.L616:
	.word	.LANCHOR0+2516
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC4
	.word	.LC3
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	test_node_in_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	test_node_in_list, %function
test_node_in_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L624
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	r2, [r0]
	ldr	ip, [r3, #-3548]
	sub	r3, r2, ip
	asr	r0, r3, #1
	ldr	r3, .L624+4
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L620:
	cmp	r3, r1
	beq	.L621
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L622
	mla	r2, r0, r3, ip
	b	.L620
.L621:
	mov	r0, #1
	ldr	pc, [sp], #4
.L622:
	mov	r0, #0
	ldr	pc, [sp], #4
.L625:
	.align	2
.L624:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movw	r3, #2332
	ldr	r4, .L642
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	bls	.L628
	ldr	r2, .L642+4
	mov	lr, #6
	mul	lr, lr, r0
	mvn	ip, #0
	ldr	r6, [r2, #-3548]
	mov	r5, r2
	add	r1, r6, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r6, lr]	@ movhi
	ldr	r3, [r2, #-3544]
	cmp	r3, #0
	streq	r1, [r2, #-3544]
	beq	.L628
	ldr	r8, [r2, #-3540]
	lsl	r10, r0, #1
	ldrh	r2, [r1, #4]
	ldrh	r7, [r8, r10]
	cmp	r2, #0
	mulne	ip, r2, r7
	ldr	r7, [r5, #-3548]
	sub	r2, r3, r7
	asr	r9, r2, #1
	ldr	r2, .L642+8
	mul	r2, r2, r9
	movw	r9, #2332
	ldrh	r4, [r4, r9]
	ldr	r9, [r5, #-3604]
	str	r4, [sp]
	uxth	r2, r2
	add	r4, r9, r10
	str	r4, [sp, #4]
	mov	r4, #0
.L637:
	ldr	r5, [sp]
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L628
	lsl	r10, r2, #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	ip, r5
	bne	.L633
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L635
.L634:
	strh	r2, [r6, lr]	@ movhi
	ldr	ip, .L642+4
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, [ip, #-3544]
	cmp	r3, r2
	ldrhne	lr, [r3, #2]
	movne	r2, #6
	ldrne	r1, [ip, #-3548]
	strheq	r0, [r3, #2]	@ movhi
	streq	r1, [ip, #-3544]
	mulne	r2, r2, lr
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L628
.L633:
	bcc	.L634
.L635:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	bne	.L636
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L642+4
	str	r1, [r3, #-3536]
.L628:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L636:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L637
.L643:
	.align	2
.L642:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	insert_data_list
	ldr	r2, .L646
	ldrh	r3, [r2, #-12]
	add	r3, r3, #1
	strh	r3, [r2, #-12]	@ movhi
	pop	{r4, pc}
.L647:
	.align	2
.L646:
	.word	.LANCHOR2-3520
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.syntax unified
	.arm
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
	beq	.L649
	ldr	r2, .L656
	mov	r1, #6
	mul	r5, r1, r0
	mvn	r3, #0
	ldr	r6, [r2, #-3548]
	mov	ip, r2
	add	lr, r6, r5
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r6, r5]	@ movhi
	ldr	r3, [r2, #-3528]
	cmp	r3, #0
	streq	lr, [r2, #-3528]
	beq	.L649
	ldr	r8, [r2, #-3604]
	lsl	r2, r0, #1
	ldr	r7, [ip, #-3548]
	ldrh	r9, [r8, r2]
	sub	r2, r3, r7
	asr	r10, r2, #1
	ldr	r2, .L656+4
	mul	r2, r2, r10
	mov	r10, r1
	uxth	r2, r2
.L653:
	lsl	r1, r2, #1
	ldrh	r1, [r8, r1]
	cmp	r1, r9
	bcs	.L651
	ldrh	r1, [r3]
	cmp	r1, r4
	bne	.L652
	strh	r2, [lr, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L649:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L652:
	mla	r3, r10, r1, r7
	mov	r2, r1
	b	.L653
.L651:
	ldrh	r1, [r3, #2]
	strh	r1, [lr, #2]	@ movhi
	strh	r2, [r6, r5]	@ movhi
	ldr	r2, [ip, #-3528]
	cmp	r3, r2
	ldrhne	lr, [r3, #2]
	movne	r2, #6
	ldrne	r1, [ip, #-3548]
	strheq	r0, [r3, #2]	@ movhi
	streq	lr, [ip, #-3528]
	mulne	r2, r2, lr
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L649
.L657:
	.align	2
.L656:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	insert_free_list
	ldr	r2, .L660
	ldrh	r3, [r2, #-4]
	add	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	pop	{r4, pc}
.L661:
	.align	2
.L660:
	.word	.LANCHOR2-3520
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	ip, #6
	ldr	r4, .L668
	mul	r1, ip, r1
	movw	r5, #65535
	ldr	r3, [r0]
	ldr	r2, [r4, #-3548]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L663
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strhne	r0, [r3, #2]	@ movhi
.L665:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	pop	{r4, r5, pc}
.L663:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L666
	cmp	r0, r3
	mulne	r3, ip, r0
	mvnne	r0, #0
	strhne	r0, [r2, r3]	@ movhi
	b	.L665
.L666:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r3, [lr, #2]
	ldrh	r5, [r2, r1]
	ldr	r0, [r4, #-3548]
	mul	r3, ip, r3
	strh	r5, [r0, r3]	@ movhi
	b	.L665
.L669:
	.align	2
.L668:
	.word	.LANCHOR2
	.fnend
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_index_node, %function
List_pop_index_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, [r0]
	cmp	r3, #0
	beq	.L676
	ldr	r2, .L681
	push	{r4, lr}
	.save {r4, lr}
	movw	lr, #65535
	mov	r4, #6
	ldr	r2, [r2, #-3548]
.L672:
	cmp	r1, #0
	bne	.L673
.L675:
	ldr	r4, .L681+4
	sub	r3, r3, r2
	asr	r3, r3, #1
	mul	r4, r4, r3
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, pc}
.L673:
	ldrh	ip, [r3]
	cmp	ip, lr
	beq	.L675
	sub	r1, r1, #1
	mla	r3, r4, ip, r2
	uxth	r1, r1
	b	.L672
.L676:
	movw	r0, #65535
	bx	lr
.L682:
	.align	2
.L681:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_get_gc_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L689
	ldr	r3, [r2, #-3544]
	cmp	r3, #0
	ldrne	r1, [r2, #-3548]
	movne	ip, #6
	movwne	r2, #65535
	bne	.L685
.L688:
	movw	r0, #65535
	bx	lr
.L687:
	sub	r0, r0, #1
	mla	r3, ip, r3, r1
	uxth	r0, r0
.L685:
	cmp	r0, #0
	beq	.L686
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L687
	b	.L688
.L686:
	ldr	r0, .L689+4
	sub	r3, r3, r1
	asr	r3, r3, #1
	mul	r3, r0, r3
	uxth	r0, r3
	bx	lr
.L690:
	.align	2
.L689:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L699
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	sub	r5, r3, #3520
	ldrh	r2, [r5]
	cmp	r2, r0
	beq	.L692
	sub	r2, r3, #3472
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L692
	sub	r2, r3, #3424
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L692
	mov	lr, #6
	ldr	r1, [r3, #-3548]
	mul	lr, lr, r0
	ldr	r2, [r3, #-3544]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L692
	ldr	r4, [r3, #-3540]
	lsl	r3, r0, #1
	ldrh	r2, [ip, #4]
	ldrh	r3, [r4, r3]
	cmp	r2, #0
	mvneq	r2, #0
	mulne	r2, r2, r3
	ldrh	r3, [ip, #2]
	movw	ip, #65535
	cmp	r3, ip
	bne	.L694
	ldrh	ip, [r1, lr]
	cmp	ip, r3
	beq	.L692
.L694:
	mov	ip, #6
	mul	ip, ip, r3
	ldr	r3, .L699+4
	asr	lr, ip, #1
	add	r1, r1, ip
	mul	r3, r3, lr
	lsl	r3, r3, #1
	ldrh	lr, [r4, r3]
	ldrh	r3, [r1, #4]
	cmp	r3, #0
	mulne	r3, r3, lr
	mvneq	r3, #0
	cmp	r2, r3
	bcs	.L692
	mov	r4, r0
	mov	r1, r0
	ldr	r0, .L699+8
	bl	List_remove_node
	ldrh	r3, [r5, #-12]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r5, #-12]	@ movhi
	bl	INSERT_DATA_LIST
.L692:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L700:
	.align	2
.L699:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-3544
	.fnend
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r3, #0
	ldrh	r1, [r0, #10]
	ldr	r2, [r0, #12]
.L702:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L705
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L702
	mov	r4, r0
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L703
	ldr	r3, .L709
	ldr	r0, .L709+4
	ldrh	r2, [r3, #6]
	bl	printk
.L704:
	b	.L704
.L703:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L705:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L710:
	.align	2
.L709:
	.word	.LANCHOR0+2516
	.word	.LC5
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	select_l2p_ram_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L722
	movw	r3, #2430
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #0
	mov	ip, #12
	movw	lr, #65535
	ldrh	r2, [r2, r3]
	ldr	r3, .L722+4
	ldr	r3, [r3, #-3376]
.L712:
	uxth	r0, r1
	cmp	r0, r2
	bcc	.L714
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r5, #12
.L715:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L717
	cmp	r0, r2
	popcc	{r4, r5, r6, pc}
	ldr	r1, .L722+8
	mov	r0, r2
	mvn	ip, #0
	ldrh	r5, [r1, #-12]
	mov	r1, #0
.L718:
	uxth	lr, r1
	cmp	lr, r2
	bcc	.L720
	pop	{r4, r5, r6, pc}
.L714:
	add	r1, r1, #1
	mla	r4, ip, r1, r3
	ldrh	r4, [r4, #-12]
	cmp	r4, lr
	bne	.L712
	pop	{r4, r5, r6, pc}
.L717:
	mla	lr, r5, r1, r3
	add	r1, r1, #1
	ldr	lr, [lr, #4]
	cmp	ip, lr
	movls	r6, #0
	movhi	r6, #1
	cmp	lr, #0
	movlt	r6, #0
	cmp	r6, #0
	movne	ip, lr
	movne	r0, r4
	b	.L715
.L720:
	ldr	r4, [r3, #4]
	cmp	ip, r4
	bls	.L719
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L719:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L718
.L723:
	.align	2
.L722:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3360
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L733
	sub	r1, r3, #3360
	ldrh	r2, [r1, #-10]
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L725
	add	r2, r2, #1
	strh	r2, [r1, #-10]	@ movhi
	bx	lr
.L725:
	ldr	r0, .L733+4
	mov	r2, #0
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	strh	r2, [r1, #-10]	@ movhi
	movw	r1, #2332
	str	r2, [r3, #-3368]
	ldrh	r1, [r0, r1]
	ldr	r2, [r3, #-3540]
	add	r1, r2, r1, lsl #1
.L726:
	cmp	r2, r1
	bne	.L728
	ldr	pc, [sp], #4
.L728:
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #-3368]
	addne	r0, r0, ip
	strne	r0, [r3, #-3368]
	b	.L726
.L734:
	.align	2
.L733:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r1, #0
	mov	r3, r0
	beq	.L736
	b	ftl_set_blk_mode.part.9
.L736:
	ldr	r2, .L737
	lsr	r0, r0, #5
	and	r3, r3, #31
	mov	ip, #1
	ldr	r1, [r2, #32]
	ldr	r2, [r1, r0, lsl #2]
	bic	r3, r2, ip, lsl r3
	str	r3, [r1, r0, lsl #2]
	bx	lr
.L738:
	.align	2
.L737:
	.word	.LANCHOR0
	.fnend
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L740
	lsr	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #32]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L741:
	.align	2
.L740:
	.word	.LANCHOR0
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #0
	movw	ip, #2324
	strh	r3, [r0, #4]	@ movhi
	ldr	r3, .L750
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	movw	r5, #65535
	ldrh	lr, [r3, ip]
	add	ip, r0, r2, lsl #1
	add	ip, ip, #14
.L743:
	cmp	r2, lr
	bcc	.L745
	movw	r2, #2390
	add	ip, r0, #16
	ldrh	r3, [r3, r2]
	movw	r4, #65535
	sub	r3, r3, #1
	sub	r1, r3, r1
	mov	r3, #0
	uxth	r1, r1
.L746:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L748
	pop	{r4, r5, pc}
.L745:
	ldrh	r4, [ip, #2]!
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r4, r5
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L743
.L748:
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L746
.L751:
	.align	2
.L750:
	.word	.LANCHOR0
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	make_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2324
	ldr	r6, .L765
	mov	r4, r0
	add	r7, r0, #16
	mvn	r9, #0
	ldr	r10, .L765+4
	mov	r5, #0
	ldrh	r8, [r6, r3]
	strh	r5, [r0, #4]	@ movhi
	strb	r5, [r0, #7]
.L753:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L755
	movw	r2, #2390
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r6, r2]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2248]
	cmp	r3, #0
	beq	.L756
	ldrh	r3, [r4]
	ldr	r2, .L765+8
	ldr	r2, [r2, #-3604]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #79
	movls	r3, #1
	strbls	r3, [r4, #9]
.L756:
	ldrb	r3, [r6, #36]	@ zero_extendqisi2
	mov	r0, #0
	cmp	r3, #0
	movne	r3, #1
	strbne	r3, [r4, #9]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L755:
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	add	r7, r7, #2
	ldrh	r1, [r4]
	add	r5, r5, #1
	bl	V2P_block
	strh	r9, [r7, #-2]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	strheq	fp, [r7, #-2]	@ movhi
	ldrbeq	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	strbeq	r3, [r4, #7]
	b	.L753
.L766:
	.align	2
.L765:
	.word	.LANCHOR0
	.word	.LANCHOR0+2350
	.word	.LANCHOR2
	.fnend
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L774
	movw	r2, #2324
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	ldr	r9, .L774+4
	mov	r6, r0
	mov	r4, r5
	ldrh	r7, [r3, r2]
	movw	r2, #2390
	ldrh	r8, [r3, r2]
.L768:
	uxth	r3, r5
	cmp	r7, r3
	bhi	.L770
	cmp	r4, #0
	moveq	r0, r4
	beq	.L771
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L771:
	ldr	r3, .L774+8
	mov	r2, #6
	ldr	r3, [r3, #-3548]
	mla	r6, r2, r6, r3
	strh	r0, [r6, #4]	@ movhi
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L770:
	mov	r1, r6
	ldrb	r0, [r9, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addeq	r4, r4, r8
	uxtheq	r4, r4
	b	.L768
.L775:
	.align	2
.L774:
	.word	.LANCHOR0
	.word	.LANCHOR0+2350
	.word	.LANCHOR2
	.fnend
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L779
	ldr	r0, [r2, #-3528]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #-3548]
	sub	r0, r0, r3
	ldr	r3, .L779+4
	asr	r0, r0, #1
	mul	r0, r3, r0
	ldr	r3, [r2, #-3604]
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r0, [r3, r0]
	bx	lr
.L780:
	.align	2
.L779:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L793
	ldr	r3, [r1, #-3528]
	cmp	r3, #0
	beq	.L787
	sub	r2, r1, #3520
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r2, [r2, #-4]
	mov	r4, #6
	movw	r5, #65535
	ldr	ip, [r1, #-3548]
	rsb	r2, r2, r2, lsl #3
	sub	r3, r3, ip
	asr	r2, r2, #3
	asr	r3, r3, #1
	cmp	r0, r2
	uxthgt	r0, r2
	ldr	r2, .L793+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L784:
	uxth	lr, r2
	cmp	r0, lr
	bls	.L786
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L788
.L786:
	ldr	r2, [r1, #-3604]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	pop	{r4, r5, pc}
.L788:
	mov	r3, lr
	b	.L784
.L787:
	mov	r0, r3
	bx	lr
.L794:
	.align	2
.L793:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r9, .L808
	add	r5, r8, #12
	.pad #36
	sub	sp, sp, #36
	ldr	r1, .L808+4
	bl	strcpy
	ldr	r2, [r9, #124]
	mov	r0, r5
	ldr	r1, .L808+8
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r9, #2408]
	mov	r0, r5
	ldr	r1, .L808+12
	bl	sprintf
	ldr	r3, .L808+16
	add	r5, r5, r0
	ldr	r3, [r3, #500]
	cmp	r3, #1
	subne	r0, r5, r8
	bne	.L795
	add	r3, sp, #28
	add	r2, sp, #24
	add	r1, sp, #20
	add	r0, sp, #16
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #28]
	mov	r0, r5
	ldr	r2, [sp, #16]
	ldr	r1, .L808+20
	str	r3, [sp, #4]
	ldr	r3, [sp, #24]
	ldr	r7, .L808+24
	str	r3, [sp]
	ldr	r3, [sp, #20]
	sub	r10, r7, #3520
	bl	sprintf
	add	r6, r5, r0
	ldr	r1, .L808+28
	mov	r0, r6
	add	r6, r6, #10
	bl	strcpy
	ldr	r2, [r9, #2452]
	mov	r0, r6
	ldr	r1, .L808+32
	sub	r4, r7, #3584
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3368]
	ldr	r1, .L808+36
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3364]
	ldr	r1, .L808+40
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3360]
	ldr	r1, .L808+44
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3356]
	ldr	r1, .L808+48
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3352]
	ldr	r1, .L808+52
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3348]
	ldr	r1, .L808+56
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3344]
	ldr	r1, .L808+60
	mov	r0, r6
	bl	sprintf
	ldr	r2, [r7, #-3340]
	add	r6, r6, r0
	ldr	r1, .L808+64
	mov	r0, r6
	sub	r5, r7, #3280
	lsr	r2, r2, #11
	bl	sprintf
	ldr	r2, [r7, #-3336]
	add	r6, r6, r0
	ldr	r1, .L808+68
	mov	r0, r6
	lsr	r2, r2, #11
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3332]
	ldr	r1, .L808+72
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3328]
	ldr	r1, .L808+76
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	bl	FtlBbtCalcTotleCnt
	ldr	r2, .L808+80
	mov	r3, r0
	ldr	r1, .L808+84
	mov	r0, r6
	ldrh	r2, [r2, #6]
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r10, #-4]
	ldr	r1, .L808+88
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3324]
	ldr	r1, .L808+92
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3320]
	ldr	r1, .L808+96
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3316]
	ldr	r1, .L808+100
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3600]
	ldr	r1, .L808+104
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3312]
	ldr	r1, .L808+108
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-3308]
	ldr	r1, .L808+112
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #18]
	ldr	r1, .L808+116
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #16]
	ldr	r1, .L808+120
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r9, #2432]
	ldr	r1, .L808+124
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r9, #2424]
	ldr	r1, .L808+128
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r9, #2320]
	ldr	r1, .L808+132
	mov	r0, r6
	bl	sprintf
	ldr	r3, .L808+136
	add	r6, r6, r0
	ldr	r1, .L808+140
	mov	r0, r6
	sub	r4, r7, #3296
	ldrh	r2, [r3, #6]
	bl	sprintf
	movw	r3, #2332
	add	r6, r6, r0
	ldrh	r2, [r9, r3]
	mov	r0, r6
	ldr	r1, .L808+144
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #-8]
	ldr	r1, .L808+148
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r9, #2336]
	ldr	r1, .L808+152
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #-4]
	ldr	r1, .L808+156
	mov	r0, r6
	bl	sprintf
	movw	r3, #2456
	add	r6, r6, r0
	ldrh	r2, [r9, r3]
	mov	r0, r6
	ldr	r1, .L808+160
	sub	r4, r7, #3472
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r10, #2]
	ldr	r1, .L808+164
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #-3514]	@ zero_extendqisi2
	ldr	r1, .L808+168
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r10]
	ldr	r1, .L808+172
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #-3512]	@ zero_extendqisi2
	ldr	r1, .L808+176
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r10, #4]
	ldr	r1, .L808+180
	mov	r0, r6
	bl	sprintf
	ldrh	r3, [r10]
	add	r6, r6, r0
	ldr	r2, [r7, #-3540]
	mov	r0, r6
	ldr	r1, .L808+184
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #2]
	ldr	r1, .L808+188
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #-3466]	@ zero_extendqisi2
	ldr	r1, .L808+192
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4]
	ldr	r1, .L808+196
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #-3464]	@ zero_extendqisi2
	ldr	r1, .L808+200
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #4]
	ldr	r1, .L808+204
	mov	r0, r6
	bl	sprintf
	ldrh	r3, [r4]
	add	r6, r6, r0
	ldr	r2, [r7, #-3540]
	mov	r0, r6
	ldr	r1, .L808+208
	sub	r4, r7, #3424
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #2]
	ldr	r1, .L808+212
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #-3418]	@ zero_extendqisi2
	ldr	r1, .L808+216
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4]
	ldr	r1, .L808+220
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #-3416]	@ zero_extendqisi2
	ldr	r1, .L808+224
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #4]
	ldr	r1, .L808+228
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #-2]
	ldr	r1, .L808+232
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #-3278]	@ zero_extendqisi2
	ldr	r1, .L808+236
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #-4]
	ldr	r1, .L808+240
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #-3276]	@ zero_extendqisi2
	ldr	r1, .L808+244
	mov	r0, r6
	bl	sprintf
	sub	r4, r5, #4
	add	r6, r6, r0
	ldrh	r2, [r4, #4]
	mov	r0, r6
	ldr	r1, .L808+248
	bl	sprintf
	ldr	r3, [r7, #-3160]
	add	r6, r6, r0
	ldr	r2, [r9, #2248]
	mov	r0, r6
	ldr	r1, [r7, #-2724]
	str	r3, [sp, #4]
	ldr	r3, [r7, #-3152]
	orr	r2, r2, r1, lsl #8
	ldr	r1, .L808+252
	str	r3, [sp]
	ldr	r3, [r7, #-3156]
	bl	sprintf
	add	r4, r6, r0
	ldr	r2, [r7, #-3164]
	ldr	r1, .L808+256
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r7, #-3140]
	ldr	r1, .L808+260
	mov	r0, r4
	bl	sprintf
	sub	r3, r7, #2720
	add	r4, r4, r0
	ldrh	r2, [r3]
	mov	r0, r4
	ldr	r1, .L808+264
	sub	r6, r7, #2704
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #-14]
	ldr	r1, .L808+268
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r7, #-2716]
	ldr	r1, .L808+272
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #-8]
	ldr	r1, .L808+276
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L808+280
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r10, #-4]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L808+284
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r5, #-4]
	movw	r2, #65535
	add	r4, r4, r0
	cmp	r3, r2
	beq	.L798
	ldr	r2, [r7, #-3540]
	lsl	r3, r3, #1
	mov	r0, r4
	ldr	r1, .L808+288
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L798:
	mov	r0, #0
	ldr	r9, .L808+292
	bl	List_get_gc_head_node
	uxth	r3, r0
	mov	r5, #0
	movw	fp, #65535
	mov	r10, #6
.L800:
	cmp	r3, fp
	beq	.L799
	ldr	r2, [r7, #-3604]
	lsl	r1, r3, #1
	mul	r6, r10, r3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp, #8]
	ldr	r2, [r7, #-3548]
	add	r2, r2, r6
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r7, #-3540]
	ldrh	r2, [r2, r1]
	mov	r1, r9
	str	r2, [sp]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r7, #-3548]
	cmp	r5, #16
	add	r4, r4, r0
	ldrh	r3, [r3, r6]
	bne	.L800
.L799:
	ldr	r2, [r7, #-3548]
	mov	r5, #0
	ldr	r3, [r7, #-3528]
	movw	r9, #65535
	ldr	fp, .L808+296
	mov	r10, #6
	sub	r3, r3, r2
	ldr	r2, .L808+300
	asr	r3, r3, #1
	mul	r3, r2, r3
	uxth	r3, r3
.L802:
	cmp	r3, r9
	beq	.L801
	ldr	r1, [r7, #-3604]
	lsl	r2, r3, #1
	mul	r6, r10, r3
	mov	r0, r4
	ldrh	r2, [r1, r2]
	mov	r1, fp
	str	r2, [sp, #4]
	ldr	r2, [r7, #-3548]
	add	r2, r2, r6
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r5
	add	r5, r5, #1
	bl	sprintf
	cmp	r5, #4
	add	r4, r4, r0
	ldrne	r3, [r7, #-3548]
	ldrhne	r3, [r3, r6]
	bne	.L802
.L801:
	sub	r0, r4, r8
.L795:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L809:
	.align	2
.L808:
	.word	.LANCHOR0
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
	.word	.LANCHOR0+2456
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
	.word	.LANCHOR0+2516
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
	.word	.LC68
	.word	.LC69
	.word	.LC70
	.word	.LC71
	.word	.LC72
	.word	.LC73
	.word	.LC74
	.word	.LC75
	.word	-1431655765
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	ftl_proc_ftl_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_proc_ftl_read, %function
ftl_proc_ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r2, .L812
	ldr	r1, .L812+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	pop	{r4, r5, r6, pc}
.L813:
	.align	2
.L812:
	.word	.LC76
	.word	.LC77
	.fnend
	.size	ftl_proc_ftl_read, .-ftl_proc_ftl_read
	.align	2
	.global	GetSwlReplaceBlock
	.syntax unified
	.arm
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
	ldr	r4, .L842
	ldr	r2, [r4, #-3316]
	ldr	r3, [r4, #-3308]
	cmp	r2, r3
	bcs	.L815
	ldr	r0, .L842+4
	movw	r2, #2332
	mov	r3, #0
	str	r3, [r4, #-3324]
	ldrh	r1, [r0, r2]
	mov	r6, r0
	ldr	r2, [r4, #-3604]
	sub	r2, r2, #2
.L816:
	cmp	r3, r1
	bcc	.L817
	ldr	r5, [r4, #-3324]
	mov	r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #-3316]
	movw	r3, #2382
	ldr	r0, [r4, #-3320]
	ldrh	r1, [r6, r3]
	sub	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #-3324]
.L818:
	ldr	r5, [r4, #-3308]
	ldr	r8, [r4, #-3316]
	add	r3, r5, #256
	cmp	r3, r8
	bls	.L823
	ldr	r2, [r4, #-3312]
	add	r3, r5, #768
	cmp	r3, r2
	bls	.L823
	ldr	r3, .L842+4
	cmp	r5, #40
	ldr	r2, [r3, #2248]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L823
.L825:
	movw	r6, #65535
.L824:
	mov	r0, r6
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L817:
	ldrh	r0, [r2, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #-3324]
	add	r0, r0, ip
	str	r0, [r4, #-3324]
	b	.L816
.L815:
	ldr	r3, [r4, #-3312]
	cmp	r2, r3
	bls	.L818
	ldr	ip, .L842+8
	add	r3, r3, #1
	str	r3, [r4, #-3312]
	mov	r3, #0
.L820:
	ldrh	r2, [ip]
	cmp	r3, r2
	bcs	.L818
	ldr	r0, [r4, #-3604]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L820
.L823:
	ldr	r3, .L842+12
	ldrh	r0, [r3, #-4]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r1, r5, #64
	mov	r10, r0
	cmp	r0, r1
	movcs	r1, #0
	movcc	r1, #1
	cmp	r5, #40
	movls	r1, #0
	cmp	r1, #0
	bne	.L825
	ldr	r3, [r4, #-3544]
	cmp	r3, #0
	beq	.L825
	ldr	r0, .L842+4
	movw	r2, #2332
	ldr	ip, [r4, #-3548]
	movw	r7, #65535
	ldr	r9, [r4, #-3604]
	mov	fp, #6
	ldrh	r2, [r0, r2]
	ldr	lr, .L842+16
	str	r2, [sp, #20]
	mov	r2, r7
.L826:
	ldrh	r0, [r3]
	movw	r6, #65535
	cmp	r0, r6
	bne	.L829
	mov	r6, r2
.L828:
	movw	r3, #65535
	cmp	r6, r3
	beq	.L825
	lsl	fp, r6, #1
	ldrh	r1, [r9, fp]
	cmp	r5, r1
	bcs	.L830
	bl	GetFreeBlockMinEraseCount
	cmp	r5, r0
	strcc	r7, [r4, #-3308]
.L830:
	cmp	r8, r1
	bls	.L825
	add	r3, r1, #128
	cmp	r10, r3
	ble	.L825
	add	r3, r1, #256
	cmp	r8, r3
	bhi	.L831
	ldr	r3, [r4, #-3312]
	add	r1, r1, #768
	cmp	r1, r3
	bcs	.L825
.L831:
	str	r10, [sp, #8]
	mov	r2, r8
	ldrh	r3, [r9, fp]
	mov	r1, r6
	ldr	r0, .L842+20
	str	r3, [sp, #4]
	ldr	r3, [r4, #-3540]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #-3312]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2708]
	b	.L824
.L829:
	add	r1, r1, #1
	ldr	r6, [sp, #20]
	uxth	r1, r1
	cmp	r1, r6
	bhi	.L825
	ldrh	r6, [r3, #4]
	cmp	r6, #0
	beq	.L827
	sub	r3, r3, ip
	asr	r3, r3, #1
	mul	r3, lr, r3
	uxth	r6, r3
	lsl	r3, r6, #1
	ldrh	r3, [r9, r3]
	cmp	r5, r3
	bcs	.L828
	cmp	r7, r3
	movhi	r7, r3
	movhi	r2, r6
.L827:
	mla	r3, fp, r0, ip
	b	.L826
.L843:
	.align	2
.L842:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2332
	.word	.LANCHOR2-3520
	.word	-1431655765
	.word	.LC78
	.fnend
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	free_data_superblock, %function
free_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r2, #65535
	cmp	r0, r2
	beq	.L847
	ldr	r2, .L850
	lsl	r3, r0, #1
	push	{r4, lr}
	.save {r4, lr}
	mov	r1, #0
	ldr	r2, [r2, #-3540]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
	mov	r0, #0
	pop	{r4, pc}
.L847:
	mov	r0, #0
	bx	lr
.L851:
	.align	2
.L850:
	.word	.LANCHOR2
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	FtlGcBufInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L858
	mov	r3, #0
	ldr	r2, .L858+4
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, #12
	mov	r5, #1
	add	r6, ip, #76
	mov	r7, #36
	str	r3, [r2, #-2704]
.L853:
	ldrh	r1, [ip]
	uxth	r0, r3
	add	lr, r3, #1
	cmp	r0, r1
	bcc	.L854
	ldr	r4, .L858+8
	mov	ip, #12
	mov	lr, #0
.L855:
	ldr	r3, [r2, #-2684]
	cmp	r1, r3
	bcc	.L856
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L854:
	uxth	r3, r3
	ldr	r8, [r2, #-2700]
	mul	r0, r4, r3
	add	r1, r8, r0
	str	r5, [r1, #8]
	ldrh	r1, [r6]
	mul	r1, r3, r1
	add	r9, r1, #3
	cmp	r1, #0
	movlt	r1, r9
	ldr	r9, [r2, #-2696]
	bic	r1, r1, #3
	add	r1, r9, r1
	str	r1, [r8, r0]
	ldr	r1, .L858+12
	ldr	r9, [r2, #-2700]
	ldrh	r1, [r1]
	add	r8, r9, r0
	mul	r1, r3, r1
	add	r10, r1, #3
	cmp	r1, #0
	movlt	r1, r10
	ldr	r10, [r2, #-2692]
	bic	r1, r1, #3
	add	r1, r10, r1
	str	r1, [r8, #4]
	ldr	r1, [r2, #-2688]
	mla	r3, r7, r3, r1
	ldr	r1, [r9, r0]
	str	r1, [r3, #8]
	ldr	r1, [r8, #4]
	str	r1, [r3, #12]
	mov	r3, lr
	b	.L853
.L856:
	mul	r5, ip, r1
	ldr	r6, [r2, #-2700]
	add	r3, r6, r5
	str	lr, [r3, #8]
	ldrh	r3, [r4]
	mul	r3, r1, r3
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r2, #-2696]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r6, r5]
	ldr	r3, .L858+12
	ldr	r0, [r2, #-2700]
	ldrh	r3, [r3]
	add	r0, r0, r5
	mul	r3, r1, r3
	add	r1, r1, #1
	uxth	r1, r1
	add	r5, r3, #3
	cmp	r3, #0
	movlt	r3, r5
	ldr	r5, [r2, #-2692]
	bic	r3, r3, #3
	add	r3, r5, r3
	str	r3, [r0, #4]
	b	.L855
.L859:
	.align	2
.L858:
	.word	.LANCHOR0+2324
	.word	.LANCHOR2
	.word	.LANCHOR0+2400
	.word	.LANCHOR0+2402
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L868
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	mov	r5, #36
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #-2684]
	ldr	r4, [r3, #-2700]
.L861:
	uxth	r3, lr
	cmp	r1, r3
	popls	{r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L862:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L863
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L862
	str	r8, [r9, #8]
.L863:
	add	lr, lr, #1
	b	.L861
.L869:
	.align	2
.L868:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L878
	mov	ip, #0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	mov	r7, #1
	mov	r8, #36
	ldr	r4, [r3, #-2684]
	ldr	r5, [r3, #-2700]
.L871:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L875
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L875:
	mov	lr, #0
.L872:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L873
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L872
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L873:
	add	ip, ip, #1
	b	.L871
.L879:
	.align	2
.L878:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L885
	ldr	r3, [r2, #-2680]
	sub	r2, r2, #2672
	ldrh	r2, [r2, #-4]
	add	r2, r3, r2, lsl #1
.L881:
	cmp	r3, r2
	bne	.L883
	mov	r0, #0
	bx	lr
.L883:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L881
	mov	r0, #1
	bx	lr
.L886:
	.align	2
.L885:
	.word	.LANCHOR2
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ubfx	r0, r0, #10, #16
	mov	r5, r1
	mov	r6, r2
	bl	P2V_block_in_plane
	ldr	r2, .L892
	mov	r3, #0
	sub	ip, r2, #2672
	ldr	lr, [r2, #-2680]
	ldrh	r7, [ip, #-4]
	sub	r1, lr, #2
.L888:
	uxth	r8, r3
	cmp	r8, r7
	bcc	.L890
	moveq	r3, r8
	lsleq	r3, r3, #1
	strheq	r0, [lr, r3]	@ movhi
	ldrheq	r3, [ip, #-4]
	addeq	r3, r3, #1
	strheq	r3, [ip, #-4]	@ movhi
	b	.L889
.L890:
	ldrh	r8, [r1, #2]!
	add	r3, r3, #1
	cmp	r8, r0
	bne	.L888
.L889:
	ldr	r0, .L892+4
	mov	r3, #12
	ldr	r2, [r2, #-2672]
	ldrh	r1, [r0, #-12]
	mul	r3, r3, r1
	add	r1, r2, r3
	stmib	r1, {r5, r6}
	str	r4, [r2, r3]
	ldrh	r3, [r0, #-12]
	add	r3, r3, #1
	strh	r3, [r0, #-12]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L893:
	.align	2
.L892:
	.word	.LANCHOR2
	.word	.LANCHOR2-2656
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshOpenBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcRefreshOpenBlock, %function
FtlGcRefreshOpenBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, .L902
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L896
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L896
	ldrh	r3, [r4, #-6]
	cmp	r3, r0
	beq	.L896
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L896
	mov	r5, r0
	mov	r1, r0
	ldr	r0, .L902+4
	bl	printk
	ldrh	r2, [r4, #-10]
	movw	r3, #65535
	cmp	r2, r3
	strheq	r5, [r4, #-10]	@ movhi
	beq	.L896
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	strheq	r5, [r4, #-8]	@ movhi
	beq	.L896
	ldrh	r2, [r4, #-6]
	cmp	r2, r3
	strheq	r5, [r4, #-6]	@ movhi
	beq	.L896
	ldrh	r2, [r4, #-4]
	cmp	r2, r3
	strheq	r5, [r4, #-4]	@ movhi
.L896:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L903:
	.align	2
.L902:
	.word	.LANCHOR2-2656
	.word	.LC79
	.fnend
	.size	FtlGcRefreshOpenBlock, .-FtlGcRefreshOpenBlock
	.align	2
	.global	FtlGcRefreshBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, .L915
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L912
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L912
	ldrh	r3, [r4, #-6]
	cmp	r3, r0
	beq	.L912
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L912
	mov	r5, r0
	mov	r1, r0
	ldr	r0, .L915+4
	bl	printk
	ldrh	r2, [r4, #-10]
	movw	r3, #65535
	cmp	r2, r3
	strheq	r5, [r4, #-10]	@ movhi
	beq	.L912
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	strheq	r5, [r4, #-8]	@ movhi
	beq	.L912
	ldrh	r2, [r4, #-6]
	cmp	r2, r3
	strheq	r5, [r4, #-6]	@ movhi
	beq	.L912
	ldrh	r2, [r4, #-4]
	cmp	r2, r3
	bne	.L913
	strh	r5, [r4, #-4]	@ movhi
.L912:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L913:
	mvn	r0, #0
	pop	{r4, r5, r6, pc}
.L916:
	.align	2
.L915:
	.word	.LANCHOR2-2656
	.word	.LC79
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r6, .L926
	bl	P2V_block_in_plane
	sub	r7, r6, #2656
	mov	r4, r0
	mov	r2, r5
	ldrh	r1, [r7, #-2]
	ldr	r0, .L926+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, .L926+8
	ldr	r3, [r3, #2248]
	cmp	r3, #0
	mov	r3, r7
	beq	.L918
	ldr	r1, [r6, #-3604]
	lsl	r4, r4, #1
	ldrh	r2, [r1, r4]
	cmp	r2, #39
	subhi	r2, r2, #40
	strhhi	r2, [r1, r4]	@ movhi
.L918:
	ldrh	r2, [r3, #-2]
	mov	r1, #0
.L919:
	uxth	r0, r1
	cmp	r2, r0
	bhi	.L921
	cmp	r2, #15
	addls	r1, r2, #1
	lslls	r2, r2, #1
	strhls	r1, [r3, #-2]	@ movhi
	strhls	r5, [r3, r2]	@ movhi
	b	.L920
.L921:
	add	r1, r1, #1
	add	r0, r3, r1, lsl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r5
	bne	.L919
.L920:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L927:
	.align	2
.L926:
	.word	.LANCHOR2
	.word	.LC80
	.word	.LANCHOR0
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L938
	ldrh	r2, [r3, #-2]
	cmp	r2, #0
	beq	.L935
	ldrh	r0, [r3, #-10]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L935
	push	{r4, lr}
	.save {r4, lr}
	add	r4, r3, #48
	ldrh	r1, [r4, #-14]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, #-14]	@ movhi
	ldrh	r2, [r4, #-14]
	lsl	r2, r2, #1
	ldrh	r0, [r3, r2]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #-14]
	mov	r0, #0
	add	r3, r3, #1
	strh	r3, [r4, #-14]	@ movhi
	pop	{r4, pc}
.L935:
	mov	r0, #0
	bx	lr
.L939:
	.align	2
.L938:
	.word	.LANCHOR2-2656
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	ftl_memset
	.syntax unified
	.arm
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
	.align	2
	.global	BuildFlashLsbPageTable
	.syntax unified
	.arm
	.fpu softvfp
	.type	BuildFlashLsbPageTable, %function
BuildFlashLsbPageTable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r1
	bne	.L942
	ldr	r3, .L998
.L943:
	lsl	r2, r0, #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #512
	bne	.L943
.L949:
	ldr	r5, .L998+4
	mov	r1, #255
	mov	r2, #2048
	uxth	r4, r4
	sub	r0, r5, #12
	sub	r5, r5, #12
	bl	ftl_memset
	ldr	r1, .L998
	mov	r3, #0
.L944:
	uxth	r2, r3
	cmp	r4, r2
	bhi	.L977
	pop	{r4, r5, r6, pc}
.L942:
	cmp	r0, #1
	bne	.L945
	ldr	r1, .L998
	mov	r3, #0
.L948:
	cmp	r3, #3
	uxth	r2, r3
	bls	.L946
	tst	r2, #1
	movne	r0, #3
	moveq	r0, #2
	rsb	r2, r0, r2, lsl #1
	uxth	r2, r2
.L946:
	lsl	r0, r3, #1
	add	r3, r3, #1
	cmp	r3, #512
	strh	r2, [r0, r1]	@ movhi
	bne	.L948
	b	.L949
.L945:
	cmp	r0, #2
	bne	.L950
	ldr	r1, .L998
	mov	r2, #0
.L952:
	uxth	r3, r2
	cmp	r2, #1
	lsl	r0, r2, #1
	add	r2, r2, #1
	lslhi	r3, r3, #1
	subhi	r3, r3, #1
	uxthhi	r3, r3
	cmp	r2, #512
	strh	r3, [r0, r1]	@ movhi
	bne	.L952
	b	.L949
.L950:
	cmp	r0, #3
	bne	.L953
	ldr	r1, .L998
	mov	r3, #0
.L956:
	cmp	r3, #5
	uxth	r2, r3
	bls	.L954
	tst	r2, #1
	movne	r0, #5
	moveq	r0, #4
	rsb	r2, r0, r2, lsl #1
	uxth	r2, r2
.L954:
	lsl	r0, r3, #1
	add	r3, r3, #1
	cmp	r3, #512
	strh	r2, [r0, r1]	@ movhi
	bne	.L956
	b	.L949
.L953:
	cmp	r0, #4
	mov	r3, #0
	bne	.L957
	ldr	r2, .L998+8
	strh	r3, [r2, #156]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #158]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #160]	@ movhi
	mov	r3, #3
	strh	r3, [r2, #162]	@ movhi
	mov	r3, #5
	strh	r3, [r2, #166]	@ movhi
	mov	r3, #7
	strh	r3, [r2, #168]	@ movhi
	mov	r3, #8
	strh	r0, [r2, #164]	@ movhi
	strh	r3, [r2, #170]!	@ movhi
.L959:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r1, [r2, #2]!	@ movhi
	cmp	r3, #512
	bne	.L959
	b	.L949
.L957:
	cmp	r0, #5
	bne	.L960
	ldr	r2, .L998+8
	add	r1, r2, #156
.L961:
	lsl	r0, r3, #1
	strh	r3, [r0, r1]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L961
	add	r2, r2, #186
.L962:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L962
	b	.L949
.L960:
	cmp	r0, #6
	bne	.L963
	ldr	r0, .L998
	mov	r1, r3
.L966:
	cmp	r1, #5
	uxth	r2, r1
	bls	.L964
	tst	r2, #1
	movne	r2, #12
	moveq	r2, #10
	sub	r2, r3, r2
	uxth	r2, r2
.L964:
	lsl	ip, r1, #1
	add	r1, r1, #1
	cmp	r1, #512
	add	r3, r3, #3
	strh	r2, [ip, r0]	@ movhi
	uxth	r3, r3
	bne	.L966
	b	.L949
.L963:
	cmp	r0, #9
	bne	.L967
	ldr	r2, .L998+8
	movw	r1, #1021
	strh	r3, [r2, #156]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #158]	@ movhi
	mov	r3, r2
	mov	r2, #2
	strh	r2, [r3, #160]!	@ movhi
	mov	r2, #3
.L968:
	strh	r2, [r3, #2]!	@ movhi
	add	r2, r2, #2
	uxth	r2, r2
	cmp	r2, r1
	bne	.L968
	b	.L949
.L967:
	cmp	r0, #10
	bne	.L969
	ldr	r2, .L998+8
	add	r1, r2, #156
.L970:
	lsl	r0, r3, #1
	strh	r3, [r0, r1]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L970
	add	r2, r2, #280
	movw	r1, #961
.L971:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L971
	b	.L949
.L969:
	cmp	r0, #11
	bne	.L972
	ldr	r2, .L998+8
	mov	r3, #0
	add	r1, r2, #156
.L973:
	lsl	r0, r3, #1
	strh	r3, [r0, r1]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L973
	add	r2, r2, #170
.L975:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r1, [r2, #2]!	@ movhi
	cmp	r3, #512
	bne	.L975
	b	.L949
.L972:
	cmp	r0, #12
	bne	.L949
	ldr	r3, .L998+8
	mov	r2, #0
	strh	r2, [r3, #156]	@ movhi
	mov	r2, #1
	strh	r2, [r3, #158]	@ movhi
	mov	r2, #2
	strh	r2, [r3, #160]	@ movhi
	mov	r2, #3
	strh	r2, [r3, #162]!	@ movhi
	mov	r2, #4
.L976:
	sub	r1, r2, #1
	add	r1, r1, r2, lsr #1
	add	r2, r2, #1
	uxth	r2, r2
	strh	r1, [r3, #2]!	@ movhi
	cmp	r2, #512
	bne	.L976
	b	.L949
.L977:
	lsl	r2, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r2, r1]
	lsl	r0, r2, #1
	strh	r2, [r5, r0]	@ movhi
	b	.L944
.L999:
	.align	2
.L998:
	.word	.LANCHOR0+156
	.word	.LANCHOR2-2608
	.word	.LANCHOR0
	.fnend
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	2
	.global	FlashDieInfoInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1015
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r6, #0
	ldr	r4, .L1015+4
	ldr	r10, .L1015+8
	ldrh	r0, [r3, #10]
	strb	r6, [r4, #2234]
	strb	r6, [r10, #-572]
	bl	FlashBlockAlignInit
	mov	r2, #8
	mov	r1, r6
	ldr	r0, .L1015+12
	bl	ftl_memset
	mov	r2, #32
	mov	r1, r6
	ldr	r0, .L1015+16
	bl	ftl_memset
	mov	r2, #128
	mov	r1, r6
	ldr	r0, .L1015+20
	bl	ftl_memset
	ldr	r9, .L1015+24
	ldr	r5, [r4, #48]
	mov	r8, r9
	add	r7, r5, #1
.L1002:
	ldrb	r2, [r5]	@ zero_extendqisi2
	add	r1, r9, r6, lsl #3
	mov	r0, r7
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L1001
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	add	r2, r4, r3, lsl #2
	str	r0, [r2, #1180]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #2234]
	strb	r6, [r3, #2236]
.L1001:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1002
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	strb	r3, [r10, #-572]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L1003
.L1007:
	ldrh	r2, [r5, #14]
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	ldrb	r2, [r5, #13]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	ldr	r2, .L1015+28
	strh	r3, [r2, #-2]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1003:
	ldr	r9, [r4, #40]
	mov	r6, #0
.L1006:
	ldrb	r2, [r5]	@ zero_extendqisi2
	add	r1, r8, r6, lsl #3
	mov	r0, r7
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L1004
	ldrh	r3, [r5, #14]
	ldrb	r2, [r4, #2234]	@ zero_extendqisi2
	and	r1, r3, #65280
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	mul	r3, r9, r3
	mul	r3, r3, r1
	add	r1, r4, r2, lsl #2
	str	r3, [r1, #1180]
	ldrb	r0, [r5, #23]	@ zero_extendqisi2
	cmp	r0, #0
	lslne	r3, r3, #1
	strne	r3, [r1, #1180]
	add	r3, r2, #1
	add	r2, r4, r2
	strb	r3, [r4, #2234]
	strb	r6, [r2, #2236]
.L1004:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1006
	b	.L1007
.L1016:
	.align	2
.L1015:
	.word	.LANCHOR1+468
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2236
	.word	.LANCHOR0+1180
	.word	.LANCHOR0+2104
	.word	.LANCHOR0+2072
	.word	.LANCHOR2-568
	.fnend
	.size	FlashDieInfoInit, .-FlashDieInfoInit
	.align	2
	.global	ftl_read_flash_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_read_flash_info, %function
ftl_read_flash_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r2, #11
	mov	r1, #0
	mov	r4, r0
	bl	ftl_memset
	ldr	r2, .L1021
	mov	ip, #1
	ldr	r0, .L1021+4
	ldr	r3, [r2, #48]
	ldr	r1, [r2, #40]
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	smulbb	r3, r3, r1
	strh	r3, [r4, #4]	@ unaligned
	ldrb	r3, [r2, #2316]	@ zero_extendqisi2
	strb	r3, [r4, #7]
	ldr	r3, [r2, #2432]
	str	r3, [r4]	@ unaligned
	ldr	r3, [r2, #48]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r1, [r2, #2234]	@ zero_extendqisi2
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L1018:
	uxtb	r2, r3
	cmp	r1, r2
	bhi	.L1019
	pop	{r4, pc}
.L1019:
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, lsl lr
	strb	r2, [r4, #10]
	b	.L1018
.L1022:
	.align	2
.L1021:
	.word	.LANCHOR0
	.word	.LANCHOR0+2236
	.fnend
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	2
	.global	FtlMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r6, #0
	ldr	r4, .L1126
	mvn	r2, #0
	mov	r1, #32
	mov	r0, #1024
	ldr	r5, .L1126+4
	mov	r8, #12
	sub	r3, r4, #568
	str	r6, [r4, #-564]
	strh	r6, [r3]	@ movhi
	movw	r3, #65535
	str	r3, [r4, #-556]
	sub	r3, r4, #2656
	strh	r2, [r3, #-10]	@ movhi
	movw	r9, #2324
	strh	r2, [r3, #-8]	@ movhi
	mov	r7, #36
	strh	r2, [r3, #-6]	@ movhi
	strh	r2, [r3, #-4]	@ movhi
	sub	r2, r4, #2720
	strh	r6, [r3, #-2]	@ movhi
	sub	r3, r4, #544
	strh	r1, [r2]	@ movhi
	sub	r2, r4, #2704
	strh	r6, [r3]	@ movhi
	mov	r1, #128
	sub	r3, r4, #2608
	strh	r1, [r2, #-14]	@ movhi
	strh	r6, [r2, #-8]	@ movhi
	str	r6, [r4, #-2724]
	str	r6, [r4, #-3332]
	str	r6, [r4, #-3328]
	str	r6, [r4, #-3344]
	str	r6, [r4, #-3356]
	str	r6, [r4, #-3360]
	str	r6, [r4, #-3352]
	str	r6, [r4, #-3348]
	str	r6, [r4, #-3364]
	str	r6, [r4, #-3324]
	str	r6, [r4, #-3320]
	str	r6, [r4, #-3600]
	str	r6, [r4, #-3312]
	str	r6, [r4, #-3308]
	str	r6, [r4, #-560]
	str	r6, [r4, #-2708]
	str	r6, [r4, #-552]
	str	r6, [r4, #-2716]
	str	r6, [r4, #-548]
	strh	r6, [r3, #-14]	@ movhi
	movw	r3, #2396
	ldrh	r1, [r5, r3]
	bl	__aeabi_idiv
	movw	r3, #2324
	str	r6, [r5, #2444]
	ldrh	r3, [r5, r3]
	movw	r6, #2394
	str	r0, [r4, #-540]
	lsl	r3, r3, #2
	cmp	r0, r3
	ldrh	r0, [r5, r6]
	strhi	r3, [r4, #-540]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #-2680]
	ldrh	r0, [r5, r6]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r6, [r5, r9]
	str	r0, [r4, #-2672]
	mul	r6, r7, r6
	lsl	r10, r6, #3
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-536]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-532]
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-528]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-3608]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2688]
	movw	r10, #2402
	ldr	r0, [r4, #-540]
	ldr	r6, .L1126+8
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r9]
	ldrh	r7, [r6]
	str	r0, [r5, #2448]
	lsl	r3, r3, #1
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #-2684]
	bl	ftl_malloc
	str	r0, [r4, #-524]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #-520]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #-516]
	ldr	r0, [r4, #-2684]
	mul	r0, r0, r7
	bl	ftl_malloc
	str	r0, [r4, #-2696]
	ldr	r0, [r4, #-540]
	mul	r0, r0, r7
	bl	ftl_malloc
	str	r0, [r4, #-512]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #-508]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #-504]
	ldr	r0, [r4, #-2684]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r10]
	ldrh	r7, [r5, r9]
	movw	r9, #2334
	str	r0, [r4, #-2700]
	mul	r7, r7, r3
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #-500]
	lsl	r0, r7, #3
	ldr	r7, .L1126+12
	bl	ftl_malloc
	ldrh	r3, [r5, r10]
	str	r0, [r4, #-496]
	ldr	r0, [r4, #-2684]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r10]
	str	r0, [r4, #-2692]
	ldr	r0, [r4, #-540]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-492]
	ldrh	r0, [r5, r9]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r7]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-484]
	ldrh	r0, [r7]
	ldr	r3, .L1126+16
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r0, r0, #9
	strh	r0, [r7]	@ movhi
	and	r0, r3, r0, lsl #9
	bl	ftl_malloc
	ldrh	r10, [r5, r9]
	str	r0, [r4, #-480]
	add	r0, r0, #32
	str	r0, [r4, #-3604]
	lsl	r10, r10, #1
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-476]
	mov	r0, r10
	bl	ftl_malloc
	ldr	r10, [r5, #2420]
	str	r0, [r4, #-3540]
	lsl	r10, r10, #1
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-472]
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-468]
	movw	r10, #2412
	ldrh	r0, [r5, r9]
	lsr	r0, r0, #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r5, #32]
	ldrh	r0, [r5, r10]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r5, #2440]
	ldrh	r0, [r5, r10]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #-464]
	ldrh	r0, [r5, r10]
	movw	r10, #2414
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #-460]
	ldrh	r0, [r5, r10]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5, r10]
	mov	r1, #0
	str	r0, [r4, #-456]
	lsl	r2, r2, #2
	bl	ftl_memset
	movw	r3, #2428
	ldrh	r10, [r5, r3]
	lsl	r10, r10, #2
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-452]
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-448]
	movw	r10, #2430
	ldr	r0, [r5, #2420]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #-444]
	ldrh	r0, [r5, r10]
	mul	r0, r8, r0
	mov	r8, r6
	add	r6, r6, #56
	bl	ftl_malloc
	ldrh	r3, [r5, r10]
	str	r0, [r4, #-3376]
	ldrh	r0, [r8], #84
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r9]
	movw	r9, #2346
	str	r0, [r4, #-440]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	movw	r3, #2388
	str	r0, [r4, #-3548]
	ldrh	r0, [r5, r3]
	ldrh	r3, [r5, r9]
	add	r0, r0, #31
	asr	r0, r0, #5
	strh	r0, [r7, #52]!	@ movhi
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r7]
	mov	r3, #1
	ldrh	ip, [r5, r9]
	str	r0, [r5, #2484]
	lsl	r2, r2, #2
	mov	r1, r2
.L1025:
	cmp	r3, ip
	bcc	.L1026
	add	r6, r6, r3, lsl #2
	ldr	r3, .L1126+20
	mov	r2, #0
	add	r6, r6, #24
.L1027:
	cmp	r3, r6
	bne	.L1028
	ldr	r3, [r4, #-472]
	cmp	r3, #0
	bne	.L1029
.L1031:
	ldr	r1, .L1126+24
	ldr	r0, .L1126+28
	bl	printk
	mvn	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1026:
	ldr	r0, [r5, #2484]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r8, #4]!
	b	.L1025
.L1028:
	str	r2, [r6, #4]!
	b	.L1027
.L1029:
	ldr	r3, [r4, #-468]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-452]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-444]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-3376]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-440]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-3548]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r5, #2484]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-3540]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-2680]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-2672]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-536]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-528]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-3608]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-2688]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-532]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-524]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-520]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-516]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-2696]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-508]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-504]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-2700]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-500]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-496]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-2692]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-3604]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, [r4, #-484]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, .L1126+4
	ldr	r3, [r3, #2440]
	cmp	r3, #0
	beq	.L1031
	ldr	r3, .L1126
	ldr	r2, [r3, #-464]
	cmp	r2, #0
	beq	.L1031
	ldr	r2, [r3, #-460]
	cmp	r2, #0
	beq	.L1031
	ldr	r3, [r3, #-456]
	cmp	r3, #0
	beq	.L1031
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1127:
	.align	2
.L1126:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2400
	.word	.LANCHOR2-488
	.word	33553920
	.word	.LANCHOR0+2512
	.word	.LANCHOR3
	.word	.LC81
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1134
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	mov	r4, r0
	mov	r1, #0
	mov	r0, r5
	movw	r6, #65535
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldr	ip, .L1134+4
	add	r0, r4, #1020
	sub	r2, r4, #2
	add	r0, r0, #2
	mov	r4, #1
.L1130:
	ldrh	r3, [r2, #2]!
	cmp	r3, r6
	popeq	{r4, r5, r6, pc}
	lsr	lr, r3, #5
	and	r3, r3, #31
	cmp	r2, r0
	ldr	r1, [r5, lr, lsl #2]
	orr	r3, r1, r4, lsl r3
	str	r3, [r5, lr, lsl #2]
	ldrh	r3, [ip, #6]
	add	r3, r3, #1
	strh	r3, [ip, #6]	@ movhi
	bne	.L1130
	pop	{r4, r5, r6, pc}
.L1135:
	.align	2
.L1134:
	.word	.LANCHOR2-436
	.word	.LANCHOR0+2456
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbtMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L1137
	movw	r3, #2456
	mvn	r1, #0
	add	r0, r2, r3
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	mov	r2, #16
	strh	r3, [r0, #6]	@ movhi
	mov	r1, #255
	add	r0, r0, #12
	b	ftl_memset
.L1138:
	.align	2
.L1137:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L1141
	movw	r2, #2516
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, #0
	add	r3, r1, r2
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #2048
	mov	r1, r4
	add	r0, r3, #8
	strh	r4, [r3, #2]	@ movhi
	strh	r4, [r3, #4]	@ movhi
	strh	r4, [r3, #6]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L1142:
	.align	2
.L1141:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldrh	r2, [r0, #10]
	mov	r4, r0
	ldr	r5, [r0, #20]
	ldr	r7, [r0, #12]
	ldr	r6, [r0, #24]
	lsl	r2, r2, #1
	mov	r0, r5
	bl	ftl_memset
	mov	r2, #0
.L1144:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1148
	ldr	r2, .L1164
	movw	r3, #2392
	mov	r6, #0
	mov	r8, r6
	mov	r10, r6
	ldrh	r2, [r2, r3]
	ldrh	r3, [r4]
	lsl	r3, r3, #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L1149:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L1153
	mov	r0, r8
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1148:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L1145:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L1144
.L1147:
	uxth	r3, r1
	add	r1, r1, #1
	lsl	r3, r3, #1
	ldrh	ip, [r7, r3]
	adds	lr, ip, #0
	movne	lr, #1
	cmp	r0, ip
	movne	lr, #0
	cmp	lr, #0
	ldrhne	ip, [r5, r3]
	addne	ip, ip, #1
	strhne	ip, [r5, r3]	@ movhi
	b	.L1145
.L1153:
	uxth	r3, r6
	lsl	r3, r3, #1
	ldrh	r2, [r5, r3]
	cmp	r9, r2
	bls	.L1150
	ldrh	r0, [r7, r3]
	add	fp, r7, r3
	cmp	r0, #0
	bne	.L1151
.L1152:
	add	r6, r6, #1
	b	.L1149
.L1150:
	cmp	r2, #0
	bne	.L1152
	ldrh	r0, [r7, r3]
	add	fp, r7, r3
	cmp	r0, #0
	beq	.L1152
.L1154:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	r10, [fp]	@ movhi
	ldrh	r3, [r4, #8]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L1152
.L1151:
	subs	r9, r2, #0
	mov	r8, r1
	beq	.L1154
	b	.L1152
.L1165:
	.align	2
.L1164:
	.word	.LANCHOR0
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	FtlL2PDataInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r1, #0
	ldr	r4, .L1170
	mvn	r7, #0
	ldr	r5, .L1170+4
	ldr	r2, [r4, #2420]
	add	r6, r4, #2400
	ldr	r0, [r5, #-468]
	lsl	r2, r2, #1
	bl	ftl_memset
	movw	r2, #2430
	ldrh	r3, [r6]
	ldrh	r2, [r4, r2]
	mov	r1, #255
	ldr	r0, [r5, #-440]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r5
	mov	r1, r6
	add	ip, r6, #30
	mov	r5, #12
	mov	r6, r2
.L1167:
	ldrh	r8, [ip]
	uxth	r0, r2
	add	lr, r2, #1
	cmp	r8, r0
	bhi	.L1168
	ldr	r2, .L1170+8
	mvn	r1, #0
	strh	r1, [r2, #2]	@ movhi
	strh	r1, [r2]	@ movhi
	ldr	r1, [r4, #2420]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L1170+12
	strh	r1, [r2, #4]	@ movhi
	ldrh	r1, [r2, #44]
	strh	r1, [r2, #8]	@ movhi
	movw	r1, #2428
	ldrh	r1, [r4, r1]
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #-472]
	str	r2, [r3, #-420]
	ldr	r2, [r3, #-444]
	str	r2, [r3, #-416]
	ldr	r2, [r3, #-468]
	str	r2, [r3, #-412]
	ldr	r2, [r3, #-452]
	str	r2, [r3, #-408]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1168:
	uxth	r2, r2
	ldr	r8, [r3, #-3376]
	mul	r0, r5, r2
	add	r9, r8, r0
	str	r6, [r9, #4]
	strh	r7, [r8, r0]	@ movhi
	ldr	r8, [r3, #-3376]
	add	r0, r8, r0
	ldrh	r8, [r1]
	mul	r2, r2, r8
	ldr	r8, [r3, #-440]
	bic	r2, r2, #3
	add	r2, r8, r2
	str	r2, [r0, #8]
	mov	r2, lr
	b	.L1167
.L1171:
	.align	2
.L1170:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-432
	.word	-3902
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlVariablesInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mvn	r3, #0
	ldr	r5, .L1174
	mov	r4, #0
	mov	r1, r4
	movw	r7, #2334
	ldr	r6, .L1174+4
	sub	r2, r5, #380
	str	r3, [r5, #-368]
	strh	r3, [r2]	@ movhi
	movw	r3, #2438
	strh	r4, [r6, r3]	@ movhi
	movw	r3, #2412
	ldrh	r2, [r6, r3]
	ldr	r0, [r6, #2440]
	str	r4, [r5, #-384]
	str	r4, [r5, #-376]
	lsl	r2, r2, #1
	str	r4, [r5, #-372]
	str	r4, [r6, #2248]
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-3604]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-484]
	lsl	r2, r2, #1
	bl	ftl_memset
	sub	r0, r5, #3584
	mov	r1, r4
	mov	r2, #48
	sub	r0, r0, #12
	bl	ftl_memset
	sub	r0, r5, #3232
	mov	r2, #512
	mov	r1, r4
	sub	r0, r0, #4
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	pop	{r4, r5, r6, r7, r8, pc}
.L1175:
	.align	2
.L1174:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2334
	ldr	r6, .L1187
	mov	r4, #0
	mov	r2, #6
	.pad #20
	sub	sp, sp, #20
	ldr	r5, .L1187+4
	mov	r1, #0
	ldrh	r3, [r6, r3]
	mov	r9, r4
	mov	r10, r4
	ldr	r0, [r5, #-3548]
	sub	r7, r5, #3520
	mul	r2, r2, r3
	bl	ftl_memset
	sub	r3, r5, #568
	str	r4, [r5, #-3528]
	str	r4, [r5, #-3544]
	str	r4, [r5, #-3536]
	strh	r4, [r7, #-12]	@ movhi
	strh	r4, [r7, #-4]	@ movhi
	strh	r4, [r3]	@ movhi
	str	r6, [sp]
.L1177:
	ldr	r3, .L1187+8
	sxth	r8, r4
	ldrh	r3, [r3]
	cmp	r8, r3
	bge	.L1184
	ldr	r3, .L1187+12
	uxth	r1, r4
	mov	fp, #0
	mov	r6, fp
	str	r1, [sp, #4]
	ldrh	r2, [r3]
	add	r3, r3, #66
	ldrh	r3, [r3]
	b	.L1185
.L1179:
	str	r3, [sp, #12]
	add	fp, fp, #1
	ldr	r3, [sp]
	str	r2, [sp, #8]
	add	r0, r3, r1
	ldr	r1, [sp, #4]
	ldrb	r0, [r0, #2350]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #12]
	cmp	r0, #0
	ldr	r2, [sp, #8]
	addeq	r6, r3, r6
	sxtheq	r6, r6
.L1185:
	sxth	r1, fp
	cmp	r1, r2
	blt	.L1179
	cmp	r6, #0
	lsl	fp, r8, #1
	ldreq	r3, [r5, #-3540]
	mvneq	r2, #0
	strheq	r2, [r3, fp]	@ movhi
	beq	.L1181
	mov	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	sxth	r6, r0
.L1181:
	ldr	r2, [r5, #-3548]
	add	r3, fp, r8
	add	r3, r2, r3, lsl #1
	strh	r6, [r3, #4]	@ movhi
	ldrh	r3, [r7]
	cmp	r8, r3
	beq	.L1182
	ldr	r3, .L1187+16
	ldrh	r3, [r3]
	cmp	r8, r3
	beq	.L1182
	ldr	r3, .L1187+20
	ldrh	r3, [r3]
	cmp	r8, r3
	beq	.L1182
	ldr	r3, [r5, #-3540]
	uxth	r0, r4
	ldrh	r3, [r3, fp]
	cmp	r3, #0
	bne	.L1183
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L1182:
	add	r4, r4, #1
	b	.L1177
.L1183:
	add	r10, r10, #1
	uxth	r10, r10
	bl	INSERT_DATA_LIST
	b	.L1182
.L1184:
	mov	r0, #0
	strh	r10, [r7, #-12]	@ movhi
	strh	r9, [r7, #-4]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1188:
	.align	2
.L1187:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2332
	.word	.LANCHOR0+2324
	.word	.LANCHOR2-3472
	.word	.LANCHOR2-3424
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, #0
	ldr	r4, .L1191
	movw	r5, #2394
	mov	r1, #255
	ldr	r6, .L1191+4
	sub	r2, r4, #2672
	ldr	r0, [r4, #-2680]
	strh	r3, [r2, #-4]	@ movhi
	sub	r2, r4, #2656
	strh	r3, [r2, #-12]	@ movhi
	ldrh	r2, [r6, r5]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r6, r5]
	mov	r2, #12
	ldr	r0, [r4, #-2672]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	pop	{r4, r5, r6, lr}
	b	FtlGcBufInit
.L1192:
	.align	2
.L1191:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	ftl_memcpy
	.syntax unified
	.arm
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
	.align	2
	.global	FlashReadIdbData
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r2, #2048
	ldr	r1, .L1196
	bl	ftl_memcpy
	mov	r0, #0
	pop	{r4, pc}
.L1197:
	.align	2
.L1196:
	.word	.LANCHOR2-364
	.fnend
	.size	FlashReadIdbData, .-FlashReadIdbData
	.align	2
	.global	FlashLoadPhyInfoInRam
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashLoadPhyInfoInRam, %function
FlashLoadPhyInfoInRam:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, #0
	ldr	r5, .L1207
	ldr	r9, .L1207+4
	add	r6, r5, #504
.L1201:
	lsl	r8, r4, #5
	ldrb	r2, [r6, r4, lsl #5]	@ zero_extendqisi2
	mov	r1, r9
	add	r0, r8, #1
	add	r0, r6, r0
	bl	FlashMemCmp8
	subs	r7, r0, #0
	bne	.L1199
	add	r5, r5, r8
	ldr	r2, .L1207+8
	ldrb	r0, [r5, #526]	@ zero_extendqisi2
	add	r6, r6, r8
	mov	r3, r7
	mov	r1, r2
.L1200:
	ldrb	ip, [r2, r3, lsl #5]	@ zero_extendqisi2
	cmp	ip, r0
	beq	.L1203
	add	r3, r3, #1
	cmp	r3, #4
	bne	.L1200
.L1203:
	ldr	r4, .L1207+12
	add	r1, r1, r3, lsl #5
	mov	r2, #32
	ldr	r0, .L1207+16
	bl	ftl_memcpy
	mov	r2, #32
	mov	r1, r6
	mov	r0, r4
	bl	ftl_memcpy
	ldrh	r0, [r4, #10]
	bl	FlashBlockAlignInit
	b	.L1198
.L1199:
	add	r4, r4, #1
	cmp	r4, #86
	bne	.L1201
	mvn	r7, #0
.L1198:
	mov	r0, r7
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1208:
	.align	2
.L1207:
	.word	.LANCHOR1
	.word	.LANCHOR0+2072
	.word	.LANCHOR1+3256
	.word	.LANCHOR1+468
	.word	.LANCHOR0+52
	.fnend
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
	.align	2
	.global	NandcCopy1KB
	.syntax unified
	.arm
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
	add	r6, r0, #512
	add	r0, r2, r4, lsl #9
	ldr	r5, [sp, #16]
	bne	.L1210
	cmp	r3, #0
	beq	.L1211
	mov	r2, #1024
	mov	r1, r3
	bl	ftl_memcpy
.L1211:
	cmp	r5, #0
	lsrne	r4, r4, #1
	ldrne	r3, [r5]	@ unaligned
	addne	r4, r4, r4, lsl #1
	strne	r3, [r6, r4, lsl #4]
	pop	{r4, r5, r6, pc}
.L1210:
	cmp	r3, #0
	beq	.L1214
	mov	r1, r0
	mov	r2, #1024
	mov	r0, r3
	bl	ftl_memcpy
.L1214:
	cmp	r5, #0
	popeq	{r4, r5, r6, pc}
	lsr	r4, r4, #1
	add	r4, r4, r4, lsl #1
	ldr	r3, [r6, r4, lsl #4]
	strb	r3, [r5]
	lsr	r2, r3, #8
	strb	r2, [r5, #1]
	lsr	r2, r3, #16
	lsr	r3, r3, #24
	strb	r2, [r5, #2]
	strb	r3, [r5, #3]
	pop	{r4, r5, r6, pc}
	.fnend
	.size	NandcCopy1KB, .-NandcCopy1KB
	.align	2
	.global	ftl_memcpy32
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, #0
.L1227:
	cmp	r3, r2
	bne	.L1228
	bx	lr
.L1228:
	ldr	ip, [r1, r3, lsl #2]
	str	ip, [r0, r3, lsl #2]
	add	r3, r3, #1
	b	.L1227
	.fnend
	.size	ftl_memcpy32, .-ftl_memcpy32
	.align	2
	.global	ftl_memcmp
	.syntax unified
	.arm
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
	.align	2
	.global	timer_get_time
	.syntax unified
	.arm
	.fpu softvfp
	.type	timer_get_time, %function
timer_get_time:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1231
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1232:
	.align	2
.L1231:
	.word	jiffies
	.fnend
	.size	timer_get_time, .-timer_get_time
	.align	2
	.global	FlashSramLoadStore
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashSramLoadStore, %function
FlashSramLoadStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L1238
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #1684]
	add	ip, ip, #4096
	add	ip, ip, r1
	moveq	r1, ip
	strne	lr, [sp, #-4]!
	.save {lr}
	movne	r1, r0
	ldrne	lr, [sp], #4
	movne	r2, r3
	movne	r0, ip
.L1237:
	b	ftl_memcpy
.L1239:
	.align	2
.L1238:
	.word	.LANCHOR2
	.fnend
	.size	FlashSramLoadStore, .-FlashSramLoadStore
	.align	2
	.global	FlashCs123Init
	.syntax unified
	.arm
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
	.align	2
	.global	ftl_dma32_malloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_dma32_malloc, %function
ftl_dma32_malloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #8192
	ble	.L1242
	b	ftl_malloc
.L1242:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r4, r0, #63
	ldr	r5, .L1246
	bic	r4, r4, #63
	ldr	r3, [r5, #1688]
	cmp	r4, r3
	ble	.L1243
	mov	r0, #16384
	bl	ftl_malloc
	mov	r3, #16384
	str	r0, [r5, #1692]
	str	r3, [r5, #1688]
.L1243:
	ldr	r3, [r5, #1688]
	ldr	r0, [r5, #1692]
	sub	r3, r3, r4
	add	r4, r0, r4
	str	r3, [r5, #1688]
	str	r4, [r5, #1692]
	pop	{r4, r5, r6, pc}
.L1247:
	.align	2
.L1246:
	.word	.LANCHOR2
	.fnend
	.size	ftl_dma32_malloc, .-ftl_dma32_malloc
	.align	2
	.global	rk_nand_suspend
	.syntax unified
	.arm
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
	.align	2
	.global	rk_nand_resume
	.syntax unified
	.arm
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
	.align	2
	.global	rk_ftl_get_capacity
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1251
	ldr	r0, [r3, #2432]
	bx	lr
.L1252:
	.align	2
.L1251:
	.word	.LANCHOR0
	.fnend
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	2
	.global	rk_nandc_get_irq_status
	.syntax unified
	.arm
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
	.align	2
	.global	rknand_proc_ftlread
	.syntax unified
	.arm
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
	.align	2
	.global	ReadFlashInfo
	.syntax unified
	.arm
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
	.align	2
	.global	rknand_print_hex
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, #0
	ldr	r7, .L1266
	mov	r10, r0
	mov	r6, r1
	mov	r8, r2
	ldr	fp, .L1266+4
	mov	r9, r3
	mov	r4, r5
.L1257:
	cmp	r4, r9
	bne	.L1263
	ldr	r1, .L1266+4
	ldr	r0, .L1266+8
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L1263:
	cmp	r5, #0
	bne	.L1258
	mov	r3, r4
	mov	r2, r6
	mov	r1, r10
	ldr	r0, .L1266+12
	bl	printk
.L1258:
	cmp	r8, #4
	ldreq	r1, [r6, r4, lsl #2]
	beq	.L1265
	cmp	r8, #2
	lsleq	r3, r4, #1
	ldrbne	r1, [r6, r4]	@ zero_extendqisi2
	ldrsheq	r1, [r6, r3]
.L1265:
	mov	r0, r7
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1262
	mov	r5, #0
	mov	r1, fp
	ldr	r0, .L1266+8
	bl	printk
.L1262:
	add	r4, r4, #1
	b	.L1257
.L1267:
	.align	2
.L1266:
	.word	.LC83
	.word	.LC84
	.word	.LC77
	.word	.LC82
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	HynixGetReadRetryDefault
	.syntax unified
	.arm
	.fpu softvfp
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mvn	r3, #83
	ldr	r7, .L1384
	cmp	r0, #2
	mvn	r1, #82
	mvn	r2, #81
	.pad #60
	sub	sp, sp, #60
	mov	r4, r0
	strb	r3, [r7, #1220]
	mvn	r3, #80
	strb	r0, [r7, #1216]
	strb	r1, [r7, #1221]
	strb	r2, [r7, #1222]
	strb	r3, [r7, #1223]
	bne	.L1269
	mvn	r3, #88
	mov	r5, #7
	strb	r3, [r7, #1220]
	mvn	r2, #8
	ldr	r3, .L1384+4
	strb	r2, [r3, #3401]
.L1334:
	mov	r6, #4
	b	.L1270
.L1269:
	cmp	r0, #3
	bne	.L1271
	mvn	r3, #79
	strb	r3, [r7, #1220]
	mvn	r3, #78
	strb	r3, [r7, #1221]
	mvn	r3, #77
	strb	r3, [r7, #1222]
	mvn	r3, #76
	strb	r3, [r7, #1223]
	mvn	r3, #75
	strb	r3, [r7, #1224]
	mvn	r3, #74
	strb	r3, [r7, #1225]
	mvn	r3, #73
	strb	r3, [r7, #1226]
	mvn	r3, #72
.L1379:
	mov	r5, #8
	strb	r3, [r7, #1227]
	mov	r6, r5
.L1270:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L1276
	ldr	fp, .L1384+8
	mov	r10, #0
	ldr	r2, .L1384+12
.L1277:
	ldrb	r1, [r7, #2234]	@ zero_extendqisi2
	uxtb	r3, r10
	cmp	r1, r3
	bhi	.L1283
.L1284:
	ldr	r3, .L1384
	strb	r6, [r3, #1217]
	strb	r5, [r3, #1218]
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1271:
	cmp	r0, #4
	bne	.L1272
	mvn	r0, #51
	strb	r1, [r7, #1225]
	strb	r0, [r7, #1220]
	mvn	r0, #64
	strb	r0, [r7, #1221]
	mvn	r0, #85
	strb	r0, [r7, #1222]
	mvn	r0, #84
	strb	r0, [r7, #1223]
	mvn	r0, #50
	strb	r0, [r7, #1224]
	strb	r2, [r7, #1226]
	b	.L1379
.L1272:
	cmp	r0, #5
	bne	.L1273
	mov	r3, #56
	mov	r5, #8
	strb	r3, [r7, #1220]
	mov	r3, #57
	strb	r3, [r7, #1221]
	mov	r3, #58
	strb	r3, [r7, #1222]
	mov	r3, #59
	strb	r3, [r7, #1223]
	b	.L1334
.L1273:
	cmp	r0, #6
	bne	.L1274
	mov	r3, #14
	mov	r5, #12
	strb	r3, [r7, #1220]
	mov	r3, #15
	strb	r3, [r7, #1221]
	mov	r3, #16
	strb	r3, [r7, #1222]
	mov	r3, #17
	strb	r3, [r7, #1223]
	b	.L1334
.L1274:
	cmp	r0, #7
	bne	.L1275
	mvn	r3, #79
	mov	r5, #12
	strb	r3, [r7, #1220]
	mvn	r3, #78
	strb	r3, [r7, #1221]
	mvn	r3, #77
	strb	r3, [r7, #1222]
	mvn	r3, #76
	strb	r3, [r7, #1223]
	mvn	r3, #75
	strb	r3, [r7, #1224]
	mvn	r3, #74
	strb	r3, [r7, #1225]
	mvn	r3, #73
	strb	r3, [r7, #1226]
	mvn	r3, #72
	strb	r3, [r7, #1227]
	mvn	r3, #43
	strb	r3, [r7, #1228]
	mvn	r3, #42
	strb	r3, [r7, #1229]
	mov	r6, #10
	b	.L1270
.L1275:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1334
	mov	r3, #6
	strb	r5, [r7, #1221]
	strb	r3, [r7, #1220]
	mov	r3, #9
	strb	r3, [r7, #1223]
	mov	r3, #10
	strb	r0, [r7, #1222]
	mov	r5, #50
	strb	r3, [r7, #1224]
	mov	r6, #5
	b	.L1270
.L1283:
	add	r3, r7, r3
	mov	r8, #0
	ldrb	r3, [r3, #2236]	@ zero_extendqisi2
	mov	r1, #55
	ldr	r9, [r7, r3, lsl #3]
	add	r4, fp, r3, lsl #6
	add	r3, r7, r3, lsl #3
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r4, r4, #20
	add	r9, r9, r3, lsl #8
.L1278:
	add	r3, fp, r8
	str	r1, [r9, #2056]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	mov	r0, #80
	str	r2, [sp, #4]
	str	r1, [sp]
	str	r3, [r9, #2052]
	bl	ndelay
	ldr	r3, [r9, #2048]
	ldm	sp, {r1, r2}
	strb	r3, [r4, r8]
	add	r8, r8, #1
	uxtb	r3, r8
	cmp	r6, r3
	bhi	.L1278
	mov	r0, r4
	mov	r1, #0
.L1281:
	mov	r3, #1
	add	lr, r2, r1
.L1280:
	ldrb	ip, [lr, r3, lsl #2]	@ zero_extendqisi2
	ldrb	r8, [r0]	@ zero_extendqisi2
	add	ip, ip, r8
	strb	ip, [r0, r3, lsl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1280
	add	r1, r1, #1
	add	r0, r0, #1
	cmp	r1, #4
	bne	.L1281
	mov	r3, #0
	add	r10, r10, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1277
.L1276:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L1284
	smulbb	r3, r6, r5
	ldr	r8, .L1384
	asr	r2, r3, #1
	lsl	r3, r3, #4
	str	r3, [sp, #48]
	lsl	r3, r2, #2
	str	r2, [sp, #4]
	str	r3, [sp, #40]
	lsl	r3, r2, #1
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #24]
	add	r3, r8, #1216
	add	r3, r3, #28
	str	r3, [sp, #52]
.L1285:
	ldrb	r3, [sp, #24]	@ zero_extendqisi2
	str	r3, [sp, #8]
	ldr	r2, [sp, #8]
	ldrb	r3, [r8, #2234]	@ zero_extendqisi2
	cmp	r3, r2
	bls	.L1284
	ldr	r3, [sp, #8]
	add	r3, r8, r3
	ldrb	r9, [r3, #2236]	@ zero_extendqisi2
	ldr	fp, [r8, r9, lsl #3]
	mov	r0, r9
	add	r3, r8, r9, lsl #3
	ldrb	r10, [r3, #4]	@ zero_extendqisi2
	mov	r3, #255
	add	r7, fp, r10, lsl #8
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	cmp	r4, #7
	bne	.L1286
	ldr	r3, .L1384+8
	mov	r0, #160
	mla	r0, r0, r9, r3
	add	r3, r0, #28
.L1380:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, fp, r10, lsl #8
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L1289
	mov	r2, #255
	str	r2, [r3, #2052]
	mov	r2, #64
	str	r2, [r3, #2048]
	mov	r2, #204
.L1381:
	str	r2, [r3, #2052]
	mov	r2, #77
.L1382:
	str	r2, [r3, #2048]
.L1290:
	add	r3, fp, r10, lsl #8
	mov	r2, #22
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
	moveq	r2, #31
	str	r2, [r3, #2052]
	mov	r2, #2
	str	r2, [r3, #2052]
	mov	r2, #0
	str	r2, [r3, #2052]
.L1333:
	add	r3, fp, r10, lsl #8
	mov	r2, #48
	mov	r0, r9
	str	r2, [r3, #2056]
	bl	NandcWaitFlashReady
	sub	r3, r4, #5
	cmp	r4, #8
	cmpne	r3, #1
	str	r3, [sp, #44]
	movls	r2, #16
	bls	.L1294
	cmp	r4, #7
	moveq	r2, #32
	movne	r2, #2
.L1294:
	ldr	r3, .L1384+16
	sub	r2, r2, #1
	add	ip, fp, r10, lsl #8
	ldr	r3, [r3, #1696]
	str	ip, [sp]
	sub	r1, r3, #1
	uxtab	r2, r3, r2
	mov	r0, r1
.L1295:
	ldr	ip, [sp]
	ldr	ip, [ip, #2048]
	strb	ip, [r0, #1]!
	cmp	r2, r0
	bne	.L1295
	cmp	r4, #8
	bne	.L1296
	mov	r2, #0
.L1298:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #50
	beq	.L1297
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #5
	beq	.L1297
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1298
.L1299:
	mov	r1, #0
	ldr	r0, .L1384+20
	bl	printk
.L1301:
	b	.L1301
.L1286:
	cmp	r4, #8
	beq	.L1288
	ldr	r3, .L1384+8
	add	r0, r3, r9, lsl #6
	add	r3, r0, #20
	b	.L1380
.L1289:
	sub	r2, r4, #5
	cmp	r2, #1
	ldrbls	r2, [r8, #1220]	@ zero_extendqisi2
	strls	r2, [r3, #2052]
	movls	r2, #82
	bls	.L1382
	cmp	r4, #7
	bne	.L1290
	mov	r2, #174
	str	r2, [r3, #2052]
	mov	r2, #0
	str	r2, [r3, #2048]
	mov	r2, #176
	b	.L1381
.L1297:
	cmp	r1, #6
	bhi	.L1299
.L1300:
	ldr	r1, .L1384+16
	ldr	r2, [r1, #1696]
	mov	r3, r2
.L1310:
	ldr	ip, [sp, #48]
	sub	r0, r3, r2
	cmp	r0, ip
	blt	.L1311
	ldr	r3, [sp, #28]
	ldr	r1, [r1, #1696]
	add	r0, r1, r3
	mov	r3, #8
.L1313:
	mov	lr, r0
	mov	ip, #0
.L1312:
	ldrh	r7, [lr]
	add	ip, ip, #1
	mvn	r7, r7
	strh	r7, [lr], #2	@ movhi
	ldr	r7, [sp, #4]
	cmp	r7, ip
	bgt	.L1312
	ldr	ip, [sp, #40]
	subs	r3, r3, #1
	add	r0, r0, ip
	bne	.L1313
	str	r1, [sp, #12]
	str	r3, [sp, #16]
.L1319:
	mov	ip, #0
	mov	r0, ip
.L1318:
	mov	lr, #1
	mov	r7, #16
	lsl	lr, lr, r0
	str	r7, [sp, #36]
	mov	r7, #0
	str	lr, [sp, #32]
	ldr	lr, [sp, #12]
.L1316:
	ldrh	r3, [lr]
	mov	r1, r3
	ldr	r3, [sp, #32]
	bics	r3, r3, r1
	ldr	r3, [sp, #28]
	addeq	r7, r7, #1
	add	lr, lr, r3
	ldr	r3, [sp, #36]
	subs	r3, r3, #1
	str	r3, [sp, #36]
	bne	.L1316
	cmp	r7, #8
	add	r0, r0, #1
	ldrhi	r3, [sp, #32]
	orrhi	ip, ip, r3
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L1318
	ldr	r3, [sp, #12]
	strh	ip, [r3], #2	@ movhi
	str	r3, [sp, #12]
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	str	r3, [sp, #16]
	ldr	r1, [sp, #16]
	ldr	r3, [sp, #4]
	cmp	r3, r1
	bgt	.L1319
	ldr	r3, .L1384+16
	ldr	r1, [r3, #1696]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L1322:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	ip, r0
	bne	.L1322
	cmp	r3, #7
	ble	.L1323
	ldr	r0, .L1384+24
	mov	r3, #1024
	mov	r2, #1
	bl	rknand_print_hex
	mov	r1, #0
	ldr	r0, .L1384+20
	bl	printk
.L1324:
	b	.L1324
.L1296:
	cmp	r4, #7
	bne	.L1302
	mov	r2, #0
.L1304:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #12
	beq	.L1303
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #10
	beq	.L1303
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1304
.L1305:
	mov	r1, #0
	ldr	r0, .L1384+20
	bl	printk
.L1306:
	b	.L1306
.L1303:
	cmp	r1, #6
	bls	.L1300
	b	.L1305
.L1302:
	cmp	r4, #6
	bne	.L1300
	add	r3, r3, #7
.L1307:
	ldrb	r2, [r1, #1]!	@ zero_extendqisi2
	cmp	r2, #12
	beq	.L1300
	ldrb	r2, [r1, #8]	@ zero_extendqisi2
	cmp	r2, #4
	beq	.L1300
	cmp	r3, r1
	bne	.L1307
	mov	r1, #0
	ldr	r0, .L1384+20
	bl	printk
.L1309:
	b	.L1309
.L1311:
	ldr	r0, [sp]
	ldr	r0, [r0, #2048]
	strb	r0, [r3], #1
	b	.L1310
.L1323:
	cmp	r4, #6
	moveq	ip, #4
	beq	.L1325
	cmp	r4, #7
	moveq	ip, #10
	beq	.L1325
	cmp	r4, #8
	moveq	ip, #5
	movne	ip, #8
.L1325:
	sub	r3, r6, #1
	ldr	r0, [sp, #20]
	uxtb	r3, r3
	mov	lr, #0
	add	r3, r3, #1
	str	r3, [sp, #12]
.L1326:
	mov	r3, r0
	mov	r1, r2
.L1327:
	ldrb	r7, [r1], #1	@ zero_extendqisi2
	strb	r7, [r3], #1
	sub	r7, r1, r2
	uxtb	r7, r7
	cmp	r6, r7
	bhi	.L1327
	ldr	r3, [sp, #12]
	add	lr, lr, #1
	cmp	r5, lr
	add	r0, r0, ip
	add	r2, r2, r3
	bgt	.L1326
	add	r10, fp, r10, lsl #8
	mov	r3, #255
	mov	r0, r9
	str	r3, [r10, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #44]
	cmp	r3, #1
	bhi	.L1329
	mov	r3, #54
	ldr	r2, [sp]
	str	r3, [r10, #2056]
	mvn	r1, #0
	ldrb	r3, [r8, #1220]	@ zero_extendqisi2
	ldr	r0, [sp, #8]
	str	r3, [r2, #2052]
	mov	r3, #0
	str	r3, [r2, #2048]
	mov	r3, #22
	str	r3, [r10, #2056]
	bl	FlashReadCmd
.L1330:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	b	.L1285
.L1329:
	cmp	r4, #8
	moveq	r3, #190
	movne	r3, #56
	str	r3, [r10, #2056]
	b	.L1330
.L1288:
	mov	r3, #120
	mov	r2, #23
	str	r3, [r7, #2056]
	mov	r3, #0
	str	r3, [r7, #2052]
	mov	r1, #25
	str	r3, [r7, #2052]
	str	r3, [r7, #2052]
	str	r2, [r7, #2056]
	mov	r2, #4
	str	r2, [r7, #2056]
	str	r1, [r7, #2056]
	mov	r1, #218
	str	r1, [r7, #2056]
	mov	r1, #21
	str	r3, [r7, #2056]
	str	r3, [r7, #2052]
	str	r3, [r7, #2052]
	str	r1, [r7, #2052]
	str	r2, [r7, #2052]
	str	r3, [r7, #2052]
	ldr	r3, [sp, #52]
	str	r3, [sp, #20]
	b	.L1333
.L1385:
	.align	2
.L1384:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1216
	.word	.LANCHOR1+3384
	.word	.LANCHOR2
	.word	.LC85
	.word	.LC86
	.fnend
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	subs	r3, r0, #0
	bxeq	lr
	sub	r2, r3, #1
	cmp	r2, #7
	bhi	.L1388
	b	HynixGetReadRetryDefault
.L1388:
	cmp	r3, #49
	bne	.L1389
	ldr	r2, .L1400
	ldr	r1, .L1400+4
	strb	r3, [r2, #1216]
	mov	r3, #4
	strb	r3, [r2, #1217]
	mov	r3, #15
	strb	r3, [r2, #1218]
	mov	r2, #64
.L1398:
	ldr	r0, .L1400+8
	b	ftl_memcpy
.L1389:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	bhi	.L1390
	ldr	r2, .L1400
	strb	r3, [r2, #1216]
	mov	r3, #4
.L1399:
	strb	r3, [r2, #1217]
	mov	r3, #7
	strb	r3, [r2, #1218]
	mov	r2, #45
	ldr	r1, .L1400+12
	b	.L1398
.L1390:
	cmp	r3, #67
	cmpne	r3, #34
	ldreq	r2, .L1400
	strbeq	r3, [r2, #1216]
	moveq	r3, #5
	beq	.L1399
.L1391:
	cmp	r3, #68
	cmpne	r3, #35
	bxne	lr
	ldr	r2, .L1400
	ldr	r1, .L1400+16
	strb	r3, [r2, #1216]
	mov	r3, #5
	strb	r3, [r2, #1217]
	mov	r3, #17
	strb	r3, [r2, #1218]
	mov	r2, #95
	b	.L1398
.L1401:
	.align	2
.L1400:
	.word	.LANCHOR0
	.word	.LANCHOR1+404
	.word	.LANCHOR0+1220
	.word	.LANCHOR1+256
	.word	.LANCHOR1+301
	.fnend
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	NandcXferComp
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcXferComp, %function
NandcXferComp:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	ldr	r5, .L1442
	ldr	r3, [r5, #2264]
	ldr	r4, [r5, r0, lsl #3]
	cmp	r3, #3
	bls	.L1433
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L1433
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	str	r3, [sp]
	beq	.L1404
	ldr	r7, .L1442+4
	mov	r6, #0
	ldr	r8, .L1442+8
.L1405:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1413
	ldr	r3, [r5, #2264]
	cmp	r3, #5
	bhi	.L1406
.L1409:
	add	r6, r6, #1
	bics	r3, r6, #-16777216
	bne	.L1408
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	mov	r0, r7
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	bl	printk
	mov	r3, #512
	mov	r2, #4
	mov	r1, r4
	mov	r0, r8
	bl	rknand_print_hex
.L1408:
	mov	r1, #5
	mov	r0, #1
	bl	usleep_range
	b	.L1405
.L1406:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L1409
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L1409
.L1413:
	ldr	r3, [r5, #2300]
	cmp	r3, #0
	beq	.L1414
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #2292]
	ubfx	r1, r1, #22, #5
	lsl	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #2296]
	ubfx	r1, r1, #22, #5
	lsl	r1, r1, #7
	bl	rknand_dma_unmap_single
.L1414:
	mov	r3, #0
	str	r3, [r5, #2300]
.L1402:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1404:
	ldr	r7, .L1442+12
	ldr	r8, .L1442+8
.L1415:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1417
	ldr	r3, [r5, #2308]
	cmp	r3, #0
	beq	.L1418
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1418:
	ldr	r3, [r5, #2300]
	cmp	r3, #0
	beq	.L1419
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #2292]
	ubfx	r1, r1, #22, #5
	lsl	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #2296]
	ubfx	r1, r1, #22, #5
	lsl	r1, r1, #7
	bl	rknand_dma_unmap_single
.L1419:
	ldr	r3, [r5, #2308]
	cmp	r3, #0
	beq	.L1414
	mov	r0, r4
	bl	NandcSendDumpDataDone
	b	.L1414
.L1417:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bics	r3, r6, #-16777216
	bne	.L1416
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	mov	r0, r7
	ubfx	r3, r3, #16, #5
	bl	printk
	mov	r3, #512
	mov	r2, #4
	mov	r1, r4
	mov	r0, r8
	bl	rknand_print_hex
.L1416:
	mov	r1, #5
	mov	r0, #1
	bl	usleep_range
	b	.L1415
.L1433:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1433
	b	.L1402
.L1443:
	.align	2
.L1442:
	.word	.LANCHOR0
	.word	.LC87
	.word	.LC88
	.word	.LC89
	.fnend
	.size	NandcXferComp, .-NandcXferComp
	.align	2
	.global	NandcXferData
	.syntax unified
	.arm
	.fpu softvfp
	.type	NandcXferData, %function
NandcXferData:
	.fnstart
	@ args = 4, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	tst	r3, #63
	ldr	r9, .L1484
	.pad #92
	sub	sp, sp, #92
	mov	r7, r0
	mov	r5, r1
	str	r2, [sp, #8]
	mov	r8, r3
	ldr	r4, [sp, #128]
	ldr	r6, [r9, r0, lsl #3]
	bne	.L1445
	cmp	r4, #0
	bne	.L1446
	mov	r2, #64
	mov	r1, #255
	add	r0, sp, #24
	bl	ftl_memset
	add	r4, sp, #24
.L1446:
	mov	r3, #0
	ldr	r2, [sp, #8]
	mov	r1, r5
	mov	r0, r7
	str	r4, [sp, #4]
	str	r8, [sp]
	bl	NandcXferStart
	mov	r1, r5
	mov	r0, r7
	bl	NandcXferComp
	cmp	r5, #0
	movne	r9, #0
	bne	.L1447
	ldr	r3, [r9, #2312]
	mov	r2, r5
	cmp	r3, #25
	ldr	r3, [sp, #8]
	movcc	ip, #64
	movcs	ip, #128
	lsr	r1, r3, #1
	mov	r3, r5
.L1449:
	cmp	r2, r1
	add	r4, r4, #4
	add	r0, ip, r3
	bcc	.L1450
	ldr	r3, [sp, #8]
	mov	r2, #0
	ldr	r1, [r9, #2312]
	ldr	ip, [r9, #2264]
	mov	r9, r2
	lsr	r0, r3, #2
.L1451:
	cmp	r2, r0
	bcs	.L1447
	cmp	r1, #0
	bne	.L1457
.L1447:
	mov	r3, #0
	str	r3, [r6, #16]
.L1458:
	ldr	r3, .L1484
	ldr	r3, [r3, #2264]
	cmp	r3, #5
	movls	r3, #0
	movhi	r3, #1
	cmp	r5, #0
	movne	r3, #0
	cmp	r3, #0
	beq	.L1444
	ldr	r3, [r6]
	and	r2, r3, #139264
	cmp	r2, #139264
	mvneq	r9, #0
	orreq	r3, r3, #131072
	streq	r3, [r6]
.L1444:
	mov	r0, r9
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1450:
	ldr	lr, [r9, #2280]
	bic	r3, r3, #3
	add	r2, r2, #1
	ldr	r3, [lr, r3]
	strb	r3, [r4, #-4]
	lsr	lr, r3, #8
	strb	lr, [r4, #-3]
	lsr	lr, r3, #16
	lsr	r3, r3, #24
	strb	lr, [r4, #-2]
	strb	r3, [r4, #-1]
	mov	r3, r0
	b	.L1449
.L1457:
	add	r3, r2, #8
	ldr	r3, [r6, r3, lsl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	tst	r3, #4
	bne	.L1473
	ldr	r3, [sp, #20]
	ubfx	r3, r3, #15, #1
	cmp	r3, #0
	bne	.L1473
	cmp	ip, #5
	bls	.L1453
	ldr	lr, [sp, #20]
	ldr	r7, [sp, #20]
	ldr	r3, [sp, #20]
	ldr	r4, [sp, #20]
	ubfx	lr, lr, #3, #5
	ubfx	r7, r7, #27, #1
	ubfx	r3, r3, #16, #5
	orr	lr, lr, r7, lsl #5
	ubfx	r4, r4, #29, #1
	orr	r3, r3, r4, lsl #5
	cmp	lr, r3
	ldr	r3, [sp, #20]
	ldrhi	lr, [sp, #20]
	ldrls	lr, [sp, #20]
	ubfxhi	r3, r3, #3, #5
	ubfxls	r3, r3, #16, #5
	ubfxhi	lr, lr, #27, #1
	ubfxls	lr, lr, #29, #1
.L1483:
	orr	r3, r3, lr, lsl #5
.L1455:
	cmp	r9, r3
	movcc	r9, r3
.L1452:
	add	r2, r2, #1
	b	.L1451
.L1453:
	cmp	ip, #3
	bls	.L1455
	ldr	lr, [sp, #20]
	ldr	r7, [sp, #20]
	ldr	r3, [sp, #20]
	ldr	r4, [sp, #20]
	ubfx	lr, lr, #3, #5
	ubfx	r7, r7, #28, #1
	ubfx	r3, r3, #16, #5
	orr	lr, lr, r7, lsl #5
	ubfx	r4, r4, #30, #1
	orr	r3, r3, r4, lsl #5
	cmp	lr, r3
	ldr	r3, [sp, #20]
	ldrhi	lr, [sp, #20]
	ldrls	lr, [sp, #20]
	ubfxhi	r3, r3, #3, #5
	ubfxls	r3, r3, #16, #5
	ubfxhi	lr, lr, #28, #1
	ubfxls	lr, lr, #30, #1
	b	.L1483
.L1473:
	mvn	r9, #0
	b	.L1452
.L1445:
	cmp	r1, #1
	bne	.L1459
	mov	r9, #0
	cmp	r4, #0
	mov	r10, r9
	movne	r3, #4
	moveq	r3, #0
	str	r3, [sp, #12]
.L1460:
	ldr	r3, [sp, #8]
	cmp	r9, r3
	movcs	r9, #0
	bcs	.L1458
.L1462:
	cmp	r8, #0
	and	fp, r9, #3
	addne	r3, r8, r9, lsl #9
	moveq	r3, r8
	str	r4, [sp]
	mov	r2, fp
	mov	r1, #1
	mov	r0, r6
	bl	NandcCopy1KB
	mov	r3, fp
	mov	r2, #2
	mov	r1, #1
	mov	r0, r7
	str	r10, [sp, #4]
	add	r9, r9, #2
	str	r10, [sp]
	bl	NandcXferStart
	mov	r1, #1
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [sp, #12]
	add	r4, r4, r3
	b	.L1460
.L1459:
	mov	r10, #0
	mov	r2, #2
	mov	r3, r10
	str	r10, [sp, #4]
	str	r10, [sp]
	mov	r1, r10
	bl	NandcXferStart
	mov	fp, r8
	cmp	r4, r10
	mov	r9, r10
	movne	r3, #4
	moveq	r3, r10
	str	r3, [sp, #12]
.L1463:
	ldr	r3, [sp, #8]
	cmp	r10, r3
	bcs	.L1458
	mov	r1, r5
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [r6, #32]
	add	r10, r10, #2
	str	r3, [sp, #20]
	ldr	r3, [sp, #8]
	cmp	r3, r10
	bls	.L1464
	mov	r3, #0
	mov	r2, #2
	str	r3, [sp, #4]
	mov	r1, #0
	str	r3, [sp]
	mov	r0, r7
	and	r3, r10, #3
	bl	NandcXferStart
.L1464:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r9, #0
	bne	.L1465
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #20]
	ubfx	r3, r3, #3, #5
	ubfx	r2, r2, #27, #1
	orr	r3, r3, r2, lsl #5
	cmp	r9, r3
	movcc	r9, r3
.L1465:
	cmp	r8, #0
	sub	r2, r10, #2
	movne	r3, fp
	str	r4, [sp]
	moveq	r3, #0
	and	r2, r2, #3
	mov	r1, #0
	mov	r0, r6
	bl	NandcCopy1KB
	ldr	r3, [sp, #12]
	add	fp, fp, #1024
	add	r4, r4, r3
	b	.L1463
.L1485:
	.align	2
.L1484:
	.word	.LANCHOR0
	.fnend
	.size	NandcXferData, .-NandcXferData
	.align	2
	.global	FlashReadRawPage
	.syntax unified
	.arm
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
	ldr	r3, .L1489
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #477]	@ zero_extendqisi2
	bne	.L1487
	ldr	r1, .L1489+4
	ldrb	r3, [r1, #37]	@ zero_extendqisi2
	ldr	r0, [r1, #40]
	mul	r0, r0, r3
	cmp	r0, r6
	movhi	r5, #4
.L1487:
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
	mov	r1, #0
	mov	r0, r4
	bl	NandcXferData
	mov	r1, r0
	mov	r0, r4
	bl	NandcFlashDeCs
	mov	r0, r1
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1490:
	.align	2
.L1489:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashReadRawPage, .-FlashReadRawPage
	.align	2
	.global	FlashDdrTunningRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashDdrTunningRead, %function
FlashDdrTunningRead:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r3
	ldr	r4, .L1517
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
	movcc	r10, #6
	movcs	r10, #12
	cmp	r3, #0
	moveq	r5, #1024
	beq	.L1493
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
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
	cmn	r5, #1
	bne	.L1494
.L1503:
	mvn	r5, #0
.L1491:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1494:
	mov	r2, r5
	ldr	r1, [sp, #4]
	ldr	r0, .L1517+4
	bl	printk
	cmp	r5, #9
	ldrls	r3, [sp]
	ldrls	r3, [r4, r3, lsl #3]
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r2, .L1517+8
	ldr	r3, [r2, #1700]
	add	r3, r3, #1
	cmp	r3, #2048
	str	r3, [r2, #1700]
	movcs	r7, #0
	strcs	r7, [r2, #1700]
	movcs	fp, r7
	bcc	.L1491
.L1493:
	mov	r9, #0
	mvn	r8, #0
	mov	r6, r9
	mov	r4, r9
	str	r9, [sp, #8]
.L1501:
	uxtb	r0, r10
	bl	NandcSetDdrPara
	mov	r3, r7
	mov	r2, fp
	ldm	sp, {r0, r1}
	bl	FlashReadRawPage
	add	r3, r5, #1
	cmp	r0, r3
	bhi	.L1497
	cmp	r0, #2
	bhi	.L1507
	add	r4, r4, #1
	cmp	r4, #9
	bls	.L1507
	mov	r3, r6
	mov	r5, r0
	sub	r6, r10, r4
	mov	r8, #0
.L1499:
	ldr	r2, [sp, #8]
	cmp	r4, r2
	movls	r6, r3
.L1500:
	cmp	r6, #0
	beq	.L1502
	mov	r1, r6
	ldr	r0, .L1517+12
	bl	printk
	uxtb	r0, r6
	bl	NandcSetDdrPara
.L1502:
	cmn	r8, #1
	bne	.L1491
	ldm	sp, {r1, r2}
	ldr	r0, .L1517+16
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1503
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
	b	.L1491
.L1497:
	ldr	r3, [sp, #8]
	cmp	r4, r3
	bls	.L1508
	cmp	r4, #7
	sub	r6, r9, r4
	bhi	.L1500
	str	r4, [sp, #8]
.L1508:
	mov	r4, #0
	b	.L1498
.L1507:
	mov	r8, #0
	mov	r9, r10
	mov	r5, r0
	mov	r7, r8
	mov	fp, r8
.L1498:
	add	r10, r10, #2
	cmp	r10, #69
	bls	.L1501
	mov	r3, r6
	mov	r6, r9
	b	.L1499
.L1518:
	.align	2
.L1517:
	.word	.LANCHOR0
	.word	.LC90
	.word	.LANCHOR2
	.word	.LC91
	.word	.LC92
	.fnend
	.size	FlashDdrTunningRead, .-FlashDdrTunningRead
	.align	2
	.global	FlashReadPage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadPage, %function
FlashReadPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r5, r0
	mov	r6, r1
	mov	r7, r2
	mov	r8, r3
	bl	FlashReadRawPage
	cmn	r0, #1
	mov	r4, r0
	bne	.L1520
	ldr	r9, .L1539
	ldrb	fp, [r9, #44]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1521
.L1523:
	ldrb	r3, [r10, #2256]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1520
	ldr	r3, [r10, #88]
	mov	r2, r7
	mov	r1, r6
	mov	r0, r5
	ldr	r9, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	mov	r3, r8
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r4, r0
	beq	.L1524
	ldrb	r3, [r10, #2316]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1520
.L1524:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
	b	.L1520
.L1521:
	mov	r3, #0
	mov	r2, r7
	strb	r3, [r9, #44]
	mov	r1, r6
	mov	r3, r8
	mov	r0, r5
	bl	FlashReadRawPage
	cmn	r0, #1
	strb	fp, [r9, #44]
	movne	r4, r0
	beq	.L1523
.L1520:
	ldr	r9, .L1539+4
	ldr	r10, [r9, #1704]
	adds	r3, r10, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1519
	mov	r3, r8
	mov	r2, r7
	mov	r1, r6
	mov	r0, r5
	blx	r10
	mov	r3, r6
	mov	r4, r0
	mov	r1, r0
	mov	r2, r5
	ldr	r0, .L1539+8
	bl	printk
	cmn	r4, #1
	bne	.L1519
	ldr	r3, .L1539
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1519
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	r4, [r9, #1704]
	mov	r3, r8
	mov	r2, r7
	mov	r1, r6
	mov	r0, r5
	blx	r4
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1519:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1540:
	.align	2
.L1539:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC93
	.fnend
	.size	FlashReadPage, .-FlashReadPage
	.align	2
	.global	FlashDdrParaScan
	.syntax unified
	.arm
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
	ldr	r5, .L1551
	mov	r4, #0
	mov	r7, r1
	ldrb	r0, [r5, #2233]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2233]	@ zero_extendqisi2
	bl	NandcSetMode
	mov	r3, r4
	mov	r2, r4
	mov	r1, r7
	str	r4, [sp]
	mov	r0, r6
	bl	FlashDdrTunningRead
	mov	r3, r4
	mov	r8, r0
	mov	r2, r4
	mov	r1, r7
	mov	r0, r6
	bl	FlashReadRawPage
	cmn	r8, #1
	cmnne	r0, #1
	mov	r3, r5
	bne	.L1542
	ldrb	r2, [r5, #2233]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L1542
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #2256]
.L1543:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1542:
	mov	r2, #1
	strb	r2, [r3, #2256]
	b	.L1543
.L1552:
	.align	2
.L1551:
	.word	.LANCHOR0
	.fnend
	.size	FlashDdrParaScan, .-FlashDdrParaScan
	.align	2
	.global	FlashLoadPhyInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashLoadPhyInfo, %function
FlashLoadPhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r3, #60
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L1568
	mov	r5, #0
	mov	r8, #4
	strb	r3, [sp, #12]
	mov	r3, #40
	strb	r3, [sp, #13]
	mov	r3, #24
	strb	r3, [sp, #14]
	mov	r3, #16
	ldr	r4, .L1568+4
	mvn	r7, #0
	strb	r3, [sp, #15]
	mov	r0, r5
	ldr	r3, [r6, #40]
	str	r5, [r4, #1712]
	str	r3, [sp, #4]
	ldr	r3, [r4, #1696]
	str	r3, [r4, #1708]
	bl	flash_enter_slc_mode
.L1554:
	add	fp, r5, #1
	mov	r9, #0
	add	r10, sp, #12
.L1556:
	ldrb	r0, [r10, r9]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r3, #0
	ldr	r2, [r4, #1696]
	mov	r1, r5
	mov	r0, r3
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1555
	mov	r3, #0
	ldr	r2, [r4, #1696]
	mov	r1, fp
	mov	r0, r3
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1555
	add	r9, r9, #1
	cmp	r9, #4
	bne	.L1556
.L1557:
	ldr	r3, [sp, #4]
	subs	r8, r8, #1
	add	r5, r5, r3
	bne	.L1554
	mov	r0, r8
	b	.L1567
.L1558:
	movw	r1, #2036
	add	r0, r9, #12
	bl	js_hash
	ldr	r3, [r9, #8]
	cmp	r3, r0
	mvnne	r7, #0
	bne	.L1557
	mov	r2, #32
	add	r1, r9, #160
	ldr	r0, .L1568+8
	bl	ftl_memcpy
	ldr	r1, [r4, #1708]
	mov	r2, #32
	ldr	r0, .L1568+12
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r4, #1708]
	mov	r2, #852
	ldr	r0, .L1568+16
	add	r1, r1, #224
	bl	ftl_memcpy
	ldr	r3, .L1568+8
	ldrh	r0, [r3, #10]
	bl	FlashBlockAlignInit
	ldr	r7, [r4, #1708]
	mov	r0, r5
	str	r5, [r4, #1712]
	ldr	r1, [r6, #40]
	ldr	r3, [r7, #1076]
	strb	r3, [r6, #2256]
	bl	__aeabi_uidiv
	add	r0, r0, #1
	cmp	r0, #1
	movls	r3, #2
	strhi	r0, [r4, #1716]
	strls	r3, [r4, #1716]
	ldrh	r3, [r7, #14]
	mov	r7, #0
	strb	r3, [r4, #1720]
	b	.L1557
.L1555:
	ldr	r9, [r4, #1708]
	ldr	r2, .L1568+20
	ldr	r3, [r9]
	cmp	r3, r2
	bne	.L1557
	cmp	r7, #0
	bne	.L1558
	ldr	r1, [r6, #40]
	mov	r0, r5
	bl	__aeabi_uidiv
	add	r0, r0, #1
	str	r0, [r4, #1716]
	mov	r0, r7
.L1567:
	bl	flash_exit_slc_mode
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1569:
	.align	2
.L1568:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+468
	.word	.LANCHOR0+52
	.word	.LANCHOR0+1216
	.word	1312902724
	.fnend
	.size	FlashLoadPhyInfo, .-FlashLoadPhyInfo
	.align	2
	.global	ToshibaReadRetrial
	.syntax unified
	.arm
	.fpu softvfp
	.type	ToshibaReadRetrial, %function
ToshibaReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r4, .L1598
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
	sub	r3, r3, #67
	add	r7, r6, r7, lsl #8
	cmp	r3, #1
	lsl	r3, r1, #8
	movls	r5, #0
	str	r3, [sp, #16]
	bls	.L1571
	ldrb	r5, [r4, #2256]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1572
	mov	r5, #1
	mov	r0, #0
	bl	NandcSetDdrMode
.L1572:
	lsl	r3, r1, #8
	mov	r2, #92
	add	r3, r6, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L1571:
	mvn	r3, #0
	mov	r9, #1
	str	r3, [sp, #4]
	lsl	r3, r1, #8
	str	r3, [sp, #20]
.L1573:
	ldr	r3, .L1598+4
	ldrb	r3, [r3, #1721]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r9, r3
	bcc	.L1582
	ldr	r10, [sp, #4]
.L1581:
	ldrb	r2, [r4, #84]	@ zero_extendqisi2
	mov	r1, #0
	mov	r0, r7
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L1583
	bl	SandiskSetRRPara
.L1584:
	ldr	r3, [sp, #16]
	mov	r2, #255
	add	r6, r6, r3
	str	r2, [r6, #2056]
	ldrb	r2, [r4, #2316]	@ zero_extendqisi2
	add	r2, r2, r2, lsl #1
	cmp	r10, r2, asr #2
	bcc	.L1585
	cmn	r10, #1
	movne	r10, #256
.L1585:
	mov	r0, r8
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1570
	mov	r0, #4
	bl	NandcSetDdrMode
.L1570:
	mov	r0, r10
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1582:
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	mov	r0, r7
	uxtb	r1, r9
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L1574
	bl	SandiskSetRRPara
.L1575:
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1576
	ldr	r3, .L1598+4
	ldrb	r3, [r3, #1721]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r9, r3
	ldreq	r3, [sp, #20]
	moveq	r2, #179
	addeq	r3, r6, r3
	streq	r2, [r3, #2056]
.L1576:
	ldr	r3, [sp, #16]
	mov	r2, #38
	cmp	r5, #0
	add	r3, r6, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	beq	.L1577
	mov	r0, #4
	bl	NandcSetDdrMode
	mov	r3, fp
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #12]
	mov	r0, r8
	bl	FlashReadRawPage
	mov	r10, r0
	mov	r0, #0
	bl	NandcSetDdrMode
.L1578:
	cmn	r10, #1
	beq	.L1579
	ldrb	r2, [r4, #2316]	@ zero_extendqisi2
	ldr	r3, [sp, #4]
	add	r2, r2, r2, lsl #1
	cmn	r3, #1
	moveq	r3, r10
	cmp	r10, r2, asr #2
	str	r3, [sp, #4]
	bcc	.L1581
	mov	fp, #0
	str	fp, [sp, #8]
.L1579:
	add	r9, r9, #1
	b	.L1573
.L1574:
	bl	ToshibaSetRRPara
	b	.L1575
.L1577:
	mov	r3, fp
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #12]
	mov	r0, r8
	bl	FlashReadRawPage
	mov	r10, r0
	b	.L1578
.L1583:
	bl	ToshibaSetRRPara
	b	.L1584
.L1599:
	.align	2
.L1598:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	ToshibaReadRetrial, .-ToshibaReadRetrial
	.align	2
	.global	SamsungReadRetrial
	.syntax unified
	.arm
	.fpu softvfp
	.type	SamsungReadRetrial, %function
SamsungReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r5, .L1614
	mov	r9, r3
	mov	fp, r1
	mov	r10, r2
	bl	NandcWaitFlashReady
	mov	r7, #1
	mvn	r4, #0
	add	r3, r5, r8, lsl #3
	ldrb	r6, [r3, #4]	@ zero_extendqisi2
	add	r3, r6, #8
	ldr	r6, [r5, r8, lsl #3]
	add	r6, r6, r3, lsl #8
.L1601:
	ldr	r3, .L1614+4
	ldrb	r3, [r3, #1721]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcc	.L1605
.L1604:
	mov	r1, #0
	mov	r0, r6
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L1600
	cmn	r4, #1
	movne	r4, #256
.L1600:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1605:
	uxtb	r1, r7
	mov	r0, r6
	bl	SamsungSetRRPara
	mov	r3, r9
	mov	r2, r10
	mov	r1, fp
	mov	r0, r8
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1602
	ldrb	r3, [r5, #2316]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1608
	mov	r9, #0
	mov	r10, r9
.L1602:
	add	r7, r7, #1
	b	.L1601
.L1608:
	mov	r4, r0
	b	.L1604
.L1615:
	.align	2
.L1614:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	2
	.global	MicronReadRetrial
	.syntax unified
	.arm
	.fpu softvfp
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
.L1618:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r3
	ldr	r3, .L1642
	mov	r10, r2
	.pad #44
	sub	sp, sp, #44
	mov	r5, r0
	ldr	fp, .L1642
	ldrb	r2, [r3, #2316]	@ zero_extendqisi2
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	str	r1, [sp, #28]
	cmp	r3, #0
	ldrne	r3, .L1642+4
	addeq	r2, r2, r2, lsl #1
	asreq	r3, r2, #2
	smullne	r2, r3, r2, r3
	str	r3, [sp, #12]
	mov	r3, #0
	str	r3, [sp, #16]
	add	r3, fp, r0, lsl #3
	str	r3, [sp, #36]
.L1628:
	mov	r0, r5
	mov	r9, #0
	bl	NandcWaitFlashReady
	ldr	r3, [fp, r5, lsl #3]
	mvn	r4, #0
	str	r3, [sp, #20]
	ldr	r3, [sp, #36]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [sp, #24]
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #20]
	add	r6, r3, r2, lsl #8
.L1619:
	ldr	r3, .L1642+8
	ldrb	r3, [r3, #1721]	@ zero_extendqisi2
	cmp	r9, r3
	bcc	.L1623
.L1622:
	ldr	r3, [sp, #20]
	mov	r0, #200
	ldr	r2, [sp, #24]
	add	r6, r3, r2, lsl #8
	mov	r3, #239
	str	r3, [r6, #2056]
	mov	r3, #137
	str	r3, [r6, #2052]
	bl	ndelay
	mov	r3, #0
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	str	r3, [r6, #2048]
	ldr	r3, [sp, #12]
	cmp	r4, r3
	bcc	.L1624
	cmn	r4, #1
	movne	r4, #256
.L1624:
	cmn	r4, #1
	movne	r6, #0
	moveq	r6, #1
	cmp	r4, #256
	movne	r1, r6
	orreq	r1, r6, #1
	cmp	r1, #0
	beq	.L1625
	mov	r3, r9
	str	r4, [sp]
	ldr	r2, [sp, #28]
	mov	r1, r9
	ldr	r0, .L1642+12
	bl	printk
	ldr	r3, [sp, #16]
	cmp	r3, #0
	bne	.L1626
	ldrb	r3, [fp, #152]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r6, #0
	andne	r6, r6, #1
	cmp	r6, #0
	beq	.L1616
	mov	r1, #3
	mov	r0, r5
	bl	micron_auto_read_calibration_config
	mov	r3, #1
	str	r3, [sp, #16]
	b	.L1628
.L1623:
	mov	r3, #239
	mov	r0, #200
	str	r3, [r6, #2056]
	mov	r3, #137
	str	r3, [r6, #2052]
	mov	r8, #0
	bl	ndelay
	add	r3, r9, #1
	mov	r2, r10
	str	r3, [r6, #2048]
	mov	r0, r5
	str	r8, [r6, #2048]
	str	r3, [sp, #32]
	mov	r3, r7
	str	r8, [r6, #2048]
	ldr	r1, [sp, #28]
	str	r8, [r6, #2048]
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1620
	ldr	r3, [sp, #12]
	cmn	r4, #1
	moveq	r4, r0
	cmp	r0, r3
	bcc	.L1630
	mov	r7, r8
	mov	r10, r8
.L1620:
	ldr	r9, [sp, #32]
	b	.L1619
.L1630:
	mov	r4, r0
	mov	r7, r8
	mov	r10, r8
	b	.L1622
.L1626:
	mov	r1, #0
	mov	r0, r5
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
.L1616:
	mov	r0, r4
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1625:
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L1616
	mov	r0, r5
	mov	r4, #256
	bl	micron_auto_read_calibration_config
	b	.L1616
.L1643:
	.align	2
.L1642:
	.word	.LANCHOR0
	.word	1431655766
	.word	.LANCHOR2
	.word	.LC94
	.fnend
	.size	MicronReadRetrial, .-MicronReadRetrial
	.align	2
	.global	HynixReadRetrial
	.syntax unified
	.arm
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
	ldr	r5, .L1662
	mov	r8, #0
	mvn	r6, #0
	mov	fp, r2
	mov	r7, r0
	str	r1, [sp, #4]
	ldr	r3, [r5, #48]
	add	r2, r5, r0
	ldrb	r4, [r2, #1228]	@ zero_extendqisi2
	ldrb	r10, [r5, #1218]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrbls	r4, [r2, #1236]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
.L1646:
	cmp	r8, r10
	bcc	.L1651
.L1650:
	ldr	r3, [r5, #48]
	add	r7, r5, r7
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #2316]	@ zero_extendqisi2
	strbls	r4, [r7, #1236]
	strbhi	r4, [r7, #1228]
	add	r3, r3, r3, lsl #1
	cmp	r6, r3, asr #2
	bcc	.L1644
	cmn	r6, #1
	movne	r6, #256
.L1644:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1651:
	add	r4, r4, #1
	ldr	r2, .L1662+4
	uxtb	r4, r4
	ldrb	r1, [r5, #1217]	@ zero_extendqisi2
	mov	r0, r7
	cmp	r10, r4
	movls	r4, #0
	mov	r3, r4
	bl	HynixSetRRPara
	mov	r3, r9
	mov	r2, fp
	ldr	r1, [sp, #4]
	mov	r0, r7
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1648
	ldrb	r3, [r5, #2316]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1655
	mov	r9, #0
	mov	fp, r9
.L1648:
	add	r8, r8, #1
	b	.L1646
.L1655:
	mov	r6, r0
	b	.L1650
.L1663:
	.align	2
.L1662:
	.word	.LANCHOR0
	.word	.LANCHOR0+1220
	.fnend
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	2
	.syntax unified
	.arm
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
	str	r1, [sp, #16]
	bl	NandcWaitFlashReady
	ldr	r3, .L1694
	ldr	r2, [r3, r10, lsl #3]
	str	r3, [sp, #20]
	str	r2, [sp, #12]
	add	r2, r3, r10, lsl #3
	ldrb	r6, [r2, #4]	@ zero_extendqisi2
	ldrb	r2, [r3, #2232]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1665
	ldr	r3, [sp, #12]
	lsl	r8, r6, #8
	mvn	r4, #0
	mov	r7, #1
	add	r5, r3, r8
.L1669:
	mov	r3, #239
	mov	r6, #0
	str	r3, [r5, #2056]
	mov	r3, #141
	str	r3, [r5, #2052]
	mov	r2, fp
	ldr	r3, .L1694+4
	mov	r0, r10
	ldr	r1, [sp, #16]
	ldrsb	r3, [r7, r3]
	str	r3, [r5, #2048]
	mov	r3, r9
	str	r6, [r5, #2048]
	str	r6, [r5, #2048]
	str	r6, [r5, #2048]
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1666
	ldr	r3, [sp, #20]
	cmn	r4, #1
	moveq	r4, r0
	ldrb	r3, [r3, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1677
	mov	r9, r6
	mov	fp, r6
.L1666:
	add	r7, r7, #1
	cmp	r7, #26
	bne	.L1669
.L1668:
	ldr	r3, [sp, #12]
	add	r8, r3, r8
	mov	r3, #239
	str	r3, [r8, #2056]
	mov	r3, #141
.L1693:
	str	r3, [r5, #2052]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	ldr	r3, .L1694
	ldrb	r3, [r3, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L1675
	cmn	r4, #1
	movne	r4, #256
.L1675:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L1676
	str	r4, [sp]
	mov	r3, r7
	ldr	r2, [sp, #16]
	mov	r1, r7
	ldr	r0, .L1694+8
	bl	printk
.L1676:
	mov	r0, r10
	bl	NandcWaitFlashReady
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1677:
	mov	r4, r0
	b	.L1668
.L1665:
	ldr	r3, [sp, #12]
	lsl	r6, r6, #8
	ldr	r8, .L1694+12
	mvn	r4, #0
	mov	r7, #1
	add	r5, r3, r6
.L1674:
	mov	r3, #239
	mov	r2, fp
	str	r3, [r5, #2056]
	mov	r3, #137
	str	r3, [r5, #2052]
	mov	r0, r10
	ldrb	r3, [r8, #4]	@ zero_extendqisi2
	ldr	r1, [sp, #16]
	str	r3, [r5, #2048]
	ldrb	r3, [r8, #5]	@ zero_extendqisi2
	str	r3, [r5, #2048]
	ldrb	r3, [r8, #6]	@ zero_extendqisi2
	str	r3, [r5, #2048]
	ldrb	r3, [r8, #7]	@ zero_extendqisi2
	str	r3, [r5, #2048]
	mov	r3, r9
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1671
	ldr	r3, .L1694
	cmn	r4, #1
	moveq	r4, r0
	ldrb	r3, [r3, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L1678
	mov	r9, #0
	mov	fp, r9
.L1671:
	add	r7, r7, #1
	add	r8, r8, #4
	cmp	r7, #26
	bne	.L1674
.L1673:
	ldr	r3, [sp, #12]
	add	r6, r3, r6
	mov	r3, #239
	str	r3, [r6, #2056]
	mov	r3, #137
	b	.L1693
.L1678:
	mov	r4, r0
	b	.L1673
.L1695:
	.align	2
.L1694:
	.word	.LANCHOR0
	.word	.LANCHOR3+11
	.word	.LC95
	.word	.LANCHOR3+37
	.fnend
	.size	samsung_read_retrial, .-samsung_read_retrial
	.align	2
	.global	FlashProgPage
	.syntax unified
	.arm
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
	ldr	r3, .L1700
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #477]	@ zero_extendqisi2
	bne	.L1697
	ldr	r1, .L1700+4
	ldrb	r3, [r1, #37]	@ zero_extendqisi2
	ldr	r0, [r1, #40]
	mul	r0, r0, r3
	cmp	r0, r5
	bls	.L1697
	ldrb	r3, [r1, #36]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r6, #4
.L1697:
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
	mov	r1, #1
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
.L1701:
	.align	2
.L1700:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashProgPage, .-FlashProgPage
	.align	2
	.global	FlashSavePhyInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashSavePhyInfo, %function
FlashSavePhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L1716
	ldr	r5, .L1716+4
	ldr	r3, [r4, #1696]
	ldrb	r0, [r4, #1722]	@ zero_extendqisi2
	ldr	r8, .L1716+8
	str	r3, [r4, #1708]
	bl	FlashBchSel
	mov	r2, #2048
	mov	r1, #0
	ldr	r0, [r4, #1696]
	bl	ftl_memset
	ldr	r3, [r4, #1708]
	mov	r2, #32
	ldr	r1, .L1716+12
	str	r8, [r3]
	ldr	r0, [r4, #1708]
	ldrb	r3, [r5, #2234]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #37]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #2256]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	mov	r2, #8
	ldr	r1, .L1716+16
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	mov	r2, #32
	ldr	r1, .L1716+20
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	mov	r2, #32
	ldr	r1, .L1716+24
	add	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	mov	r2, #32
	add	r1, r5, #52
	add	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	mov	r2, #852
	add	r1, r5, #1216
	add	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #1708]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	movw	r3, #1592
	str	r0, [r6, #8]
	str	r3, [r6, #4]
	mov	r6, #0
	ldr	r3, [r4, #1724]
	mov	r7, r6
	mov	r0, #0
	str	r3, [r4, #1708]
	bl	flash_enter_slc_mode
.L1708:
	ldr	r1, [r5, #40]
	mov	r2, #0
	mov	r0, r2
	mul	r1, r1, r7
	bl	FlashEraseBlock
	ldrb	r9, [r5, #152]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1703
	mov	r9, #0
.L1704:
	ldr	r1, [r5, #40]
	mov	r3, #0
	ldr	r2, [r4, #1696]
	mov	r0, r3
	mla	r1, r1, r7, r9
	add	r9, r9, #1
	bl	FlashProgPage
	cmp	r9, #10
	bne	.L1704
.L1705:
	ldr	r1, [r5, #40]
	mov	r3, #0
	ldr	r2, [r4, #1724]
	mov	r0, r3
	add	r10, r7, #1
	mul	r1, r1, r7
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1706
	ldr	r9, [r4, #1708]
	ldr	r3, [r9]
	cmp	r3, r8
	bne	.L1706
	movw	r1, #2036
	add	r0, r9, #12
	bl	js_hash
	ldr	r3, [r9, #8]
	cmp	r3, r0
	bne	.L1706
	ldr	r3, [r5, #40]
	cmp	r6, #1
	str	r10, [r4, #1716]
	mul	r7, r7, r3
	str	r7, [r4, #1712]
	beq	.L1709
	mov	r6, #1
.L1706:
	cmp	r10, #4
	mov	r7, r10
	bne	.L1708
.L1707:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	lsr	r0, r0, #5
	rsb	r0, r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1703:
	ldr	r1, [r5, #40]
	mov	r3, r9
	ldr	r2, [r4, #1696]
	mov	r0, r9
	mul	r1, r1, r7
	bl	FlashProgPage
	ldr	r1, [r5, #40]
	mov	r3, r9
	ldr	r2, [r4, #1696]
	mov	r0, r9
	mul	r1, r1, r7
	add	r1, r1, #1
	bl	FlashProgPage
	b	.L1705
.L1709:
	mov	r6, #2
	b	.L1707
.L1717:
	.align	2
.L1716:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	.LANCHOR0+2072
	.word	.LANCHOR0+2236
	.word	.LANCHOR0+1180
	.word	.LANCHOR1+468
	.fnend
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	2
	.global	FlashReadIdbDataRaw
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r3, #60
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1737
	mov	r10, r0
	strb	r3, [sp, #12]
	mov	r3, #40
	strb	r3, [sp, #13]
	mov	r3, #24
	strb	r3, [sp, #14]
	mov	r3, #16
	strb	r3, [sp, #15]
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	str	r3, [sp, #4]
	ldr	r3, [r4, #2252]
	cmp	r3, #0
	beq	.L1719
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1719:
	ldr	r6, .L1737+4
	mvn	r8, #0
	mov	r5, #2
	mov	r2, #2048
	mov	r1, #0
	mov	r0, r10
	bl	ftl_memset
.L1720:
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L1725
.L1724:
	ldr	r0, [sp, #4]
	bl	FlashBchSel
	ldr	r3, [r4, #2252]
	cmp	r3, #0
	beq	.L1718
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1718:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1725:
	mov	r7, #0
	add	fp, sp, #12
.L1722:
	ldrb	r9, [r7, fp]	@ zero_extendqisi2
	mov	r0, r9
	bl	FlashBchSel
	ldr	r1, [r4, #40]
	mov	r3, #0
	ldr	r2, [r6, #1696]
	mov	r0, r3
	mul	r1, r1, r5
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1721
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1722
.L1723:
	add	r5, r5, #1
	b	.L1720
.L1728:
	mov	r8, #0
	b	.L1724
.L1721:
	ldr	r3, [r6, #1696]
	ldr	r2, .L1737+8
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1723
	mov	r1, r9
	ldr	r0, .L1737+12
	bl	printk
	mov	r2, #2048
	ldr	r1, [r6, #1696]
	mov	r0, r10
	bl	ftl_memcpy
	ldr	r3, [r6, #1696]
	ldr	r3, [r3, #512]
	strb	r3, [r4, #37]
	ldr	r3, [r6, #1716]
	cmp	r5, r3
	bcs	.L1728
	str	r5, [r6, #1716]
	mov	r8, #0
	bl	FlashSavePhyInfo
	b	.L1723
.L1738:
	.align	2
.L1737:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-52655045
	.word	.LC96
	.fnend
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	2
	.global	FlashInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashInit, %function
FlashInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	ldr	r5, .L1837
	.pad #28
	sub	sp, sp, #28
	mov	r0, #32768
	mov	r6, #0
	bl	ftl_malloc
	str	r0, [r5, #1696]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #1724]
	mov	r0, #4096
	bl	ftl_dma32_malloc
	str	r0, [r5, #1728]
	mov	r0, #32768
	bl	ftl_malloc
	ldr	r4, .L1837+4
	mov	r8, r6
	str	r0, [r5, #1732]
	mov	r0, #4096
	bl	ftl_dma32_malloc
	ldr	fp, .L1837+8
	mov	r3, #50
	str	r0, [r5, #1736]
	mov	r0, r7
	ldr	r7, .L1837+12
	strb	r3, [r4, #37]
	strb	r3, [r5, #1720]
	mov	r3, #128
	str	r3, [r4, #40]
	mov	r3, #60
	str	r6, [r5, #1716]
	strb	r6, [r4, #2256]
	str	r6, [r5, #1700]
	strb	r6, [r4, #36]
	strb	r6, [r5, #1740]
	strb	r3, [r5, #1722]
	bl	NandcInit
.L1745:
	add	r2, r4, r6, lsl #3
	uxtb	r9, r6
	ldr	r10, [r4, r6, lsl #3]
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	mov	r0, r9
	str	r2, [sp, #20]
	bl	FlashReset
	mov	r0, r9
	bl	NandcFlashCs
	ldr	r2, [sp, #20]
	mov	r3, #144
	mov	r0, #200
	add	r10, r10, r2, lsl #8
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
	bne	.L1740
	mov	r2, #239
	mov	r0, #200
	str	r2, [r10, #2056]
	mov	r2, #1
	str	r2, [r10, #2052]
	bl	ndelay
	mov	r2, #4
	str	r2, [r10, #2048]
	str	r8, [r10, #2048]
	str	r8, [r10, #2048]
	str	r8, [r10, #2048]
.L1740:
	mov	r0, r9
	bl	NandcFlashDeCs
	ldrb	r2, [r7]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1741
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
	add	r1, r6, #1
	bl	printk
.L1741:
	cmp	r6, #0
	bne	.L1742
	ldrb	r3, [r4, #2072]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1792
	ldrb	r3, [r4, #2073]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1792
.L1742:
	ldrb	r3, [r7]	@ zero_extendqisi2
	add	r6, r6, #1
	add	r7, r7, #8
	cmp	r3, #181
	moveq	r3, #44
	strbeq	r3, [r7, #-8]
	cmp	r6, #4
	bne	.L1745
	ldrb	r3, [r4, #2072]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1746
	ldr	r0, [r4, #2260]
	bl	NandcSetDdrMode
.L1746:
	mov	r2, #852
	mov	r1, #0
	ldr	r0, .L1837+16
	bl	ftl_memset
	ldr	r6, .L1837+20
	ldr	r2, .L1837+24
	ldr	r0, [r4, #2268]
	add	r3, r2, #468
	cmp	r0, r6
	str	r3, [r4, #48]
	mov	r3, #0
	strb	r3, [r4, #44]
	bne	.L1747
	ldrb	r3, [r2, #487]	@ zero_extendqisi2
	cmp	r3, #50
	movne	r3, #1
	strne	r3, [r4, #2252]
.L1747:
	ldrb	r3, [r4, #2073]	@ zero_extendqisi2
	cmp	r3, #241
	cmpne	r3, #161
	and	ip, r3, #253
	moveq	r1, #1
	movne	r1, #0
	cmp	r3, #218
	orreq	r1, r1, #1
	cmp	ip, #209
	orreq	r1, r1, #1
	cmp	r1, #0
	bne	.L1748
	cmp	r3, #220
	bne	.L1749
	ldrb	r1, [r4, #2075]	@ zero_extendqisi2
	cmp	r1, #149
	bne	.L1749
.L1748:
	mov	ip, #16
	mov	r1, #1
	strb	ip, [r4, #37]
	strb	ip, [r5, #1722]
	ldrb	ip, [r4, #2072]	@ zero_extendqisi2
	strb	r1, [r4, #36]
	strb	r3, [r2, #3414]
	cmp	ip, #152
	strb	ip, [r2, #3413]
	bne	.L1751
	ldrb	ip, [r4, #2076]	@ zero_extendqisi2
	lsrs	ip, ip, #7
	moveq	r1, #24
	strbne	r1, [r5, #1740]
	strbeq	r1, [r5, #1722]
.L1751:
	movw	r1, #2049
	cmp	r0, r1
	cmpne	r0, r6
	moveq	r1, #16
	strbeq	r1, [r5, #1722]
	cmp	r3, #218
	bne	.L1755
	ldr	r3, .L1837+28
	mov	r1, #2048
	strh	r1, [r3, #14]	@ movhi
	mvn	r3, #37
.L1831:
	strb	r3, [r2, #3414]
.L1756:
	mov	r2, #32
	ldr	r1, .L1837+32
	ldr	r0, .L1837+36
	bl	ftl_memcpy
	ldr	r1, .L1837+28
	mov	r2, #32
	sub	r0, r1, #2944
	bl	ftl_memcpy
.L1749:
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1759
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1761
	ldr	r3, [r4, #48]
	ldrh	r3, [r3, #16]
	lsr	r3, r3, #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r4, #2233]
	bne	.L1761
	mov	r3, #1
	strb	r3, [r4, #2256]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	NandcSetMode
.L1761:
	ldr	r3, [r4, #48]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #152]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1759
	ldr	r3, [r4, #2260]
	cmp	r3, #0
	beq	.L1764
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
.L1832:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1759
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #48]
	ldr	r0, .L1837+40
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1739
	bl	FlashDieInfoInit
	ldr	r3, [r4, #48]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, .L1837+44
	ldr	r2, [r4, #48]
	ldrh	r3, [r3, #-2]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	add	r3, r3, #4080
	add	r3, r3, #15
	cmp	r1, r3, asr #12
	ldrh	r3, [r2, #14]
	blt	.L1766
	add	r0, r3, #255
	cmp	r1, r0, asr #8
	bge	.L1767
.L1766:
	bic	r3, r3, #255
	strh	r3, [r2, #14]	@ movhi
.L1767:
	ldrb	r3, [r4, #2233]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1768
	bl	FlashSavePhyInfo
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r1, [r5, #1712]
	mov	r0, #0
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1768:
	bl	FlashSavePhyInfo
.L1759:
	ldr	r7, [r4, #48]
	ldrb	r3, [r7, #26]	@ zero_extendqisi2
	ldrb	r1, [r7, #12]	@ zero_extendqisi2
	ldrh	r0, [r7, #10]
	strb	r3, [r4, #152]
	ldrh	r3, [r7, #16]
	ubfx	r2, r3, #7, #1
	strb	r2, [r4, #44]
	ubfx	r2, r3, #3, #1
	strb	r2, [r5, #1741]
	ubfx	r2, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r2, [r4, #2244]
	strb	r3, [r4, #2233]
	mov	r3, #0
	str	r3, [r5, #1704]
	bl	__aeabi_idiv
	mov	r1, r0
	ldrb	r0, [r7, #18]	@ zero_extendqisi2
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #48]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L1770
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r4, #1217]	@ zero_extendqisi2
	strb	r0, [r4, #84]
	strb	r3, [r4, #85]
	ldrb	r3, [r4, #1218]	@ zero_extendqisi2
	strb	r3, [r5, #1721]
	sub	r3, r0, #1
	cmp	r3, #7
	bhi	.L1771
	ldr	r3, .L1837+48
	str	r3, [r5, #1704]
	sub	r3, r0, #5
	cmp	r0, #8
	cmpne	r3, #1
	movls	r3, #1
	strls	r3, [r4, #2308]
	cmp	r0, #7
	ldr	r3, .L1837+16
	beq	.L1793
	cmp	r0, #8
	addne	r3, r3, #20
	bne	.L1773
.L1793:
	add	r3, r3, #28
.L1773:
	sub	r1, r3, #1
	mov	r2, #0
	add	r3, r3, #31
.L1775:
	ldrsb	ip, [r1, #1]!
	cmp	ip, #0
	addeq	r2, r2, #1
	cmp	r3, r1
	bne	.L1775
	cmp	r2, #27
	bls	.L1770
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
.L1770:
	ldr	r3, [r4, #2268]
	cmp	r3, r6
	bne	.L1785
	ldrb	r2, [r4, #152]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [r4, #48]
	movne	r1, #0
	strbne	r1, [r2, #18]
.L1785:
	ldrb	r2, [r4, #2072]	@ zero_extendqisi2
	cmp	r2, #44
	bne	.L1786
	ldrb	r2, [r4, #2256]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1786
	cmp	r3, r6
	bne	.L1787
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1786
.L1787:
	mov	r3, #0
	mov	r0, #1
	strb	r3, [r4, #2256]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1786:
	ldrb	r3, [r4, #2233]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1788
	ldrb	r2, [r4, #2256]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1789
	tst	r3, #1
	bne	.L1788
.L1789:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r1, [r5, #1712]
	mov	r0, #0
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1788:
	ldr	r3, [r4, #48]
	mov	r6, #16
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1837+52
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1837+56
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
	orr	r3, r3, r2
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
	bne	.L1790
	lsl	r3, r3, #1
	lsr	r1, r7, #1
	lsl	r2, r2, #1
	strb	r6, [r4, #37]
	strh	r3, [r4, #146]	@ movhi
	mov	r3, #8
	strh	r1, [r4, #138]	@ movhi
	strh	r2, [r4, #144]	@ movhi
	strh	r3, [r4, #150]	@ movhi
.L1790:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
.L1739:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1755:
	cmp	r3, #220
	ldreq	r3, .L1837+28
	moveq	r1, #4096
	strheq	r1, [r3, #14]	@ movhi
	mvneq	r3, #35
	beq	.L1831
.L1757:
	cmp	r3, #211
	ldreq	r3, .L1837+28
	moveq	r1, #4096
	strheq	r1, [r3, #14]	@ movhi
	moveq	r3, #2
	strbeq	r3, [r2, #3425]
	b	.L1756
.L1764:
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2233]	@ zero_extendqisi2
	b	.L1832
.L1771:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1777
	ldr	r3, .L1837+60
	cmp	r0, #19
	str	r3, [r5, #1704]
	moveq	r3, #15
	beq	.L1834
.L1836:
	mov	r3, #7
.L1834:
	strb	r3, [r5, #1721]
	b	.L1770
.L1777:
	sub	r3, r0, #65
	cmp	r0, #33
	cmpne	r3, #1
	ldrls	r3, .L1837+64
	strls	r3, [r5, #1704]
	movls	r3, #4
	strbls	r3, [r4, #85]
	bls	.L1836
.L1779:
	sub	r3, r0, #67
	sub	r2, r0, #34
	uxtb	r3, r3
	cmp	r3, #1
	cmphi	r2, #1
	movls	r2, #1
	movhi	r2, #0
	bhi	.L1780
	ldr	r2, .L1837+64
	cmp	r0, #68
	cmpne	r0, #35
	str	r2, [r5, #1704]
	movne	r2, #7
	moveq	r2, #17
	cmp	r3, #1
	movls	r3, #4
	movhi	r3, #5
	strb	r2, [r5, #1721]
	strb	r3, [r4, #85]
	b	.L1770
.L1780:
	cmp	r0, #49
	ldreq	r3, .L1837+68
	streq	r3, [r5, #1704]
	beq	.L1770
	cmp	r0, #50
	ldreq	r3, .L1837+72
	streq	r2, [r4, #2252]
	streq	r3, [r5, #1704]
	b	.L1770
.L1792:
	mvn	r0, #1
	b	.L1739
.L1838:
	.align	2
.L1837:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC97
	.word	.LANCHOR0+2072
	.word	.LANCHOR0+1216
	.word	1446522928
	.word	.LANCHOR1
	.word	.LANCHOR1+3412
	.word	.LANCHOR1+3288
	.word	.LANCHOR0+52
	.word	.LC98
	.word	.LANCHOR2-568
	.word	HynixReadRetrial
	.word	.LANCHOR2-364
	.word	150000
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	samsung_read_retrial
	.fnend
	.size	FlashInit, .-FlashInit
	.align	2
	.global	FlashPageProgMsbFFData
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashPageProgMsbFFData, %function
FlashPageProgMsbFFData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r0
	ldr	r5, .L1855
	mov	r9, r1
	mov	r4, r2
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1840
	ldr	r3, [r5, #2252]
	cmp	r3, #0
	popne	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1840:
	ldr	r3, [r5, #48]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r2, r3, #5
	cmp	r3, #50
	cmpne	r2, #2
	bls	.L1841
	sub	r2, r3, #19
	and	r2, r2, #239
	cmp	r2, #0
	cmpne	r3, #68
	popne	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1841:
	ldr	r6, .L1855+4
	sub	r7, r6, #2608
	sub	r7, r7, #12
.L1843:
	ldr	r3, [r5, #48]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bhi	.L1844
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1844:
	lsl	r3, r4, #1
	ldrh	r2, [r7, r3]
	movw	r3, #65535
	cmp	r2, r3
	popne	{r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r2, #32768
	mov	r1, #255
	ldr	r0, [r6, #1724]
	bl	ftl_memset
	ldr	r3, [r6, #1724]
	add	r1, r4, r9
	mov	r0, r8
	add	r4, r4, #1
	uxth	r4, r4
	mov	r2, r3
	bl	FlashProgPage
	b	.L1843
.L1856:
	.align	2
.L1855:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1906
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r8, .L1906+4
	mov	r9, #0
	.pad #36
	sub	sp, sp, #36
	ldr	fp, .L1906+8
	ldrb	r3, [r3, #477]	@ zero_extendqisi2
	str	r1, [sp, #16]
	str	r2, [sp, #20]
	str	r3, [sp, #12]
.L1858:
	ldr	r3, [sp, #16]
	cmp	r9, r3
	bne	.L1878
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1878:
	ldr	r3, [sp, #16]
	add	r2, sp, #28
	ldr	r1, [sp, #20]
	mov	r0, r4
	sub	r3, r3, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2234]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r2, r3
	mvnls	r3, #0
	strls	r3, [r4]
	bls	.L1860
	add	r3, r8, r3
	mov	r7, #0
	ldrb	r6, [r3, #2236]	@ zero_extendqisi2
	mov	r0, r6
	bl	NandcWaitFlashReady
	mov	r0, r6
	bl	NandcFlashCs
.L1861:
	ldr	r1, [sp, #28]
	mov	r0, r6
	bl	FlashReadCmd
	mov	r0, r6
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #12]
	mov	r1, #0
	ldr	r2, [sp, #12]
	mov	r0, r6
	str	r3, [sp]
	ldr	r3, [r4, #8]
	bl	NandcXferData
	ldrb	r3, [fp, #1740]	@ zero_extendqisi2
	mov	r5, r0
	cmp	r3, #0
	beq	.L1862
	mov	r0, r6
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r5, #256
.L1862:
	cmp	r7, #9
	cmnls	r5, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1861
.L1863:
	cmp	r7, #0
	mov	r7, r3
	movne	r5, #256
.L1865:
	ldr	r3, [r8, #40]
	mov	r0, r6
	ldr	r1, [sp, #28]
	add	r1, r1, r3
	bl	FlashReadCmd
	mov	r0, r6
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #8]
	mov	r1, #0
	ldr	r2, [r4, #12]
	mov	r0, r6
	cmp	r3, #0
	addne	r3, r3, #2048
	cmp	r2, #0
	addne	r2, r2, #8
	str	r2, [sp]
	ldr	r2, [sp, #12]
	bl	NandcXferData
	ldrb	r2, [fp, #1740]	@ zero_extendqisi2
	mov	r10, r0
	cmp	r2, #0
	beq	.L1868
	mov	r0, r6
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r10, #256
.L1868:
	cmp	r7, #9
	cmnls	r10, #1
	addeq	r7, r7, #1
	beq	.L1865
.L1869:
	cmp	r7, #0
	mov	r0, r6
	movne	r10, #256
	bl	NandcFlashDeCs
	ldrb	r3, [r8, #2316]	@ zero_extendqisi2
	cmp	r5, r10
	movcc	r5, r10
	add	r3, r3, r3, lsl #1
	cmp	r5, r3, asr #2
	bls	.L1871
	cmn	r5, #1
	movne	r5, #256
.L1871:
	cmp	r5, #256
	cmnne	r5, #1
	movne	r3, #0
	streq	r5, [r4]
	strne	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1874
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1874
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1874
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1874:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1860
	ldr	r1, [r4, #4]
	ldrb	r2, [r8, #2316]	@ zero_extendqisi2
	ldr	r0, .L1906+12
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1876
	mov	r3, #8
	mov	r2, #4
	ldr	r0, .L1906+16
	bl	rknand_print_hex
.L1876:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1860
	mov	r3, #4
	ldr	r0, .L1906+20
	mov	r2, r3
	bl	rknand_print_hex
.L1860:
	add	r9, r9, #1
	add	r4, r4, #36
	b	.L1858
.L1907:
	.align	2
.L1906:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC99
	.word	.LC100
	.word	.LC101
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.syntax unified
	.arm
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
	ldr	r4, .L1979
	str	r1, [sp, #24]
	ldrb	r9, [r4, #36]	@ zero_extendqisi2
	str	r2, [sp, #28]
	cmp	r9, #0
	bne	.L1909
	ldr	r3, .L1979+4
	mov	fp, r0
	ldr	r10, .L1979+8
	str	r9, [sp, #8]
	ldrb	r3, [r3, #477]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldrb	r3, [r4, #44]	@ zero_extendqisi2
	str	r3, [sp, #36]
.L1910:
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #24]
	cmp	r3, r2
	bcc	.L1943
	mov	r0, #0
	b	.L1908
.L1909:
	bl	FlashReadSlc2KPages
.L1908:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1943:
	ldr	r2, [sp, #8]
	mov	r3, #36
	ldr	r1, [sp, #28]
	mul	r3, r3, r2
	add	r8, fp, r3
	str	r3, [sp, #12]
	ldr	r3, [sp, #24]
	mov	r0, r8
	ldr	r7, [r8, #4]
	sub	r3, r3, r2
	add	r2, sp, #44
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldrb	r2, [r4, #2234]	@ zero_extendqisi2
	mov	r6, r0
	ldr	r3, [sp, #40]
	cmp	r2, r3
	ldrls	r2, [sp, #12]
	mvnls	r3, #0
	strls	r3, [fp, r2]
	bls	.L1913
	add	r3, r4, r3
	ldrb	r5, [r3, #2236]	@ zero_extendqisi2
	ldrb	r3, [r10, #1741]	@ zero_extendqisi2
	mov	r0, r5
	cmp	r3, #0
	moveq	r6, #0
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #48]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1915
	sub	r2, r2, #7
	add	r1, r4, r5
	cmp	r2, #1
	add	r2, r4, r5
	ldrb	r3, [r1, #1228]	@ zero_extendqisi2
	ldrb	r2, [r2, #2068]	@ zero_extendqisi2
	ldrbls	r3, [r1, #1236]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1915
	ldr	r2, .L1979+12
	mov	r0, r5
	ldrb	r1, [r4, #1217]	@ zero_extendqisi2
	bl	HynixSetRRPara
.L1915:
	mov	r0, r5
	lsr	r7, r7, #31
	bl	NandcFlashCs
	ldr	r3, [sp, #28]
	mov	r0, r5
	cmp	r3, #1
	orreq	r7, r7, #1
	cmp	r7, #0
	str	r7, [sp, #16]
	beq	.L1917
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1917
	bl	flash_enter_slc_mode
.L1923:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r5, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r6, r3
	beq	.L1919
	cmp	r6, #0
	beq	.L1920
	ldr	r2, [r4, #40]
	mov	r0, r5
	add	r2, r1, r2
	bl	FlashReadDpCmd
.L1921:
	mov	r0, r5
	bl	NandcWaitFlashReady
	cmp	r6, #0
	beq	.L1919
	ldr	r1, [sp, #44]
	mov	r0, r5
	bl	FlashReadDpDataOutCmd
.L1919:
	ldr	r3, [r8, #12]
	mov	r1, #0
	ldr	r2, [sp, #20]
	mov	r0, r5
	str	r3, [sp]
	ldr	r3, [r8, #8]
	bl	NandcXferData
	ldrb	r3, [r4, #44]	@ zero_extendqisi2
	mov	r7, r0
	adds	r2, r3, #0
	movne	r2, #1
	cmn	r0, #1
	movne	r2, #0
	cmp	r2, #0
	str	r2, [sp, #32]
	beq	.L1922
	mov	r3, #0
	mov	r6, #0
	strb	r3, [r4, #44]
	b	.L1923
.L1917:
	bl	flash_exit_slc_mode
	b	.L1923
.L1920:
	mov	r0, r5
	bl	FlashReadCmd
	b	.L1921
.L1922:
	cmp	r6, #0
	beq	.L1924
	ldr	r3, [r4, #40]
	mov	r0, r5
	ldr	r1, [sp, #44]
	add	r1, r1, r3
	bl	FlashReadDpDataOutCmd
	ldr	r3, [sp, #12]
	mov	r0, r5
	ldr	r1, [sp, #32]
	add	r3, r3, #36
	add	r3, fp, r3
	ldr	r2, [r3, #12]
	str	r2, [sp]
	ldr	r2, [sp, #20]
	ldr	r3, [r3, #8]
	bl	NandcXferData
	cmn	r0, #1
	mov	r9, r0
	moveq	r6, #0
.L1924:
	mov	r0, r5
	bl	NandcFlashDeCs
	ldrb	r3, [sp, #36]	@ zero_extendqisi2
	cmn	r7, #1
	strb	r3, [r4, #44]
	bne	.L1925
	ldrb	r3, [r4, #2256]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1926
.L1930:
	ldr	r6, [r10, #1704]
	cmp	r6, #0
	bne	.L1927
	ldr	r3, [r8, #12]
	mov	r0, r5
	ldr	r2, [r8, #8]
	ldr	r1, [sp, #44]
	bl	FlashReadRawPage
	mov	r7, r0
.L1931:
	cmp	r7, #256
	cmnne	r7, #1
	ldreq	r3, [sp, #12]
	movne	r3, #0
	ldrne	r2, [sp, #12]
	streq	r7, [fp, r3]
	strne	r3, [fp, r2]
	ldr	r3, [sp, #12]
	ldr	r3, [fp, r3]
	cmn	r3, #1
	bne	.L1938
	ldr	r1, [r8, #4]
	ldrb	r2, [r4, #2316]	@ zero_extendqisi2
	ldr	r0, .L1979+16
	bl	printk
	ldr	r1, [r8, #12]
	cmp	r1, #0
	beq	.L1938
	mov	r3, #4
	ldr	r0, .L1979+20
	mov	r2, r3
	bl	rknand_print_hex
.L1938:
	cmp	r6, #0
	beq	.L1940
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r9, r3, asr #2
	bls	.L1941
	ldr	r3, [r10, #1704]
	cmp	r3, #0
	moveq	r9, #256
.L1941:
	ldr	r3, [sp, #12]
	cmp	r9, #256
	cmnne	r9, #1
	movne	r2, #0
	add	r3, r3, #36
	streq	r9, [fp, r3]
	strne	r2, [fp, r3]
.L1940:
	ldr	r3, [sp, #8]
	add	r3, r3, r6
	str	r3, [sp, #8]
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L1913
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1913
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1913:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L1910
.L1926:
	ldr	r3, [r4, #88]
	mov	r0, r5
	ldr	r1, [sp, #44]
	ldr	r6, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	ldr	r3, [r8, #12]
	ldr	r2, [r8, #8]
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r7, r0
	beq	.L1929
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1946
.L1929:
	ubfx	r0, r6, #8, #8
	bl	NandcSetDdrPara
	cmn	r7, #1
	beq	.L1930
.L1946:
	mov	r6, #0
.L1925:
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r7, r3, asr #2
	bls	.L1931
	ldr	r3, [r10, #1704]
	cmp	r3, #0
	moveq	r7, #256
	b	.L1931
.L1927:
	ldr	r3, [r8, #12]
	mov	r0, r5
	ldr	r2, [r8, #8]
	ldr	r1, [sp, #44]
	blx	r6
	cmn	r0, #1
	mov	r7, r0
	bne	.L1948
	ldr	r3, [r4, #48]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1932
	mov	r3, #0
	ldr	r2, .L1979+12
	ldrb	r1, [r4, #1217]	@ zero_extendqisi2
	mov	r0, r5
	bl	HynixSetRRPara
.L1932:
	ldr	r3, [r8, #12]
	mov	r0, r5
	ldr	r2, [r8, #8]
	ldr	r1, [sp, #44]
	bl	FlashReadRawPage
	ldrb	r2, [r4, #2316]	@ zero_extendqisi2
	mov	r7, r0
	mov	r3, r0
	ldr	r1, [r8, #4]
	ldr	r0, .L1979+24
	bl	printk
	cmn	r7, #1
	bne	.L1948
	ldrb	r6, [r4, #152]	@ zero_extendqisi2
	cmp	r6, #0
	beq	.L1931
	ldr	r3, [sp, #16]
	mov	r0, r5
	cmp	r3, #0
	beq	.L1933
	bl	flash_enter_slc_mode
.L1934:
	ldr	r6, [r10, #1704]
	mov	r0, r5
	ldr	r3, [r8, #12]
	ldr	r2, [r8, #8]
	ldr	r1, [sp, #44]
	blx	r6
	mov	r7, r0
.L1948:
	mov	r6, #0
	b	.L1931
.L1933:
	bl	flash_exit_slc_mode
	b	.L1934
.L1980:
	.align	2
.L1979:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0+1220
	.word	.LC99
	.word	.LC101
	.word	.LC102
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FlashLoadFactorBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r2, #16
	ldr	r8, .L1993
	.pad #60
	sub	sp, sp, #60
	mov	r1, #0
	mvn	fp, #0
	ldr	r10, .L1993+4
	mov	r5, #0
	ldrh	r3, [r8, #136]
	mov	r9, r5
	ldrh	r6, [r8, #138]
	ldr	r0, .L1993+8
	smulbb	r6, r6, r3
	bl	ftl_memset
	ldr	r3, [r10, #1728]
	uxth	r6, r6
	str	r5, [sp, #28]
	str	r3, [sp, #32]
	add	r3, r6, fp
	uxth	r3, r3
	str	r3, [sp, #8]
.L1982:
	ldrb	r3, [r8, #2234]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bhi	.L1988
	mov	r0, fp
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1988:
	ldr	r4, [sp, #8]
	mul	r3, r7, r6
	sub	r2, r6, #12
	str	r2, [sp, #4]
.L1983:
	ldr	r2, [sp, #4]
	cmp	r4, r2
	ble	.L1985
	add	r2, r4, r3
	add	r0, sp, #20
	lsl	r2, r2, #10
	str	r3, [sp, #12]
	str	r2, [sp, #24]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #12]
	cmn	r2, #1
	beq	.L1984
	ldr	r2, [r10, #1728]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1984
	mov	r1, r7
	mov	r2, r4
	ldr	r0, .L1993+12
	add	r9, r9, #1
	bl	printk
	uxth	r9, r9
	ldr	r3, .L1993+8
	lsl	r7, r7, #1
	strh	r4, [r3, r7]	@ movhi
.L1985:
	ldrb	r3, [r8, #2234]	@ zero_extendqisi2
	add	r5, r5, #1
	cmp	r3, r9
	moveq	fp, #0
	b	.L1982
.L1984:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1983
.L1994:
	.align	2
.L1993:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+1742
	.word	.LC103
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashProgSlc2KPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2023
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r1
	ldr	r9, .L2023+4
	.pad #60
	sub	sp, sp, #60
	mov	r8, r2
	mov	r4, r0
	ldrb	fp, [r3, #477]	@ zero_extendqisi2
	mov	r6, r0
	mov	r7, #0
.L1996:
	cmp	r7, r10
	bne	.L2002
	ldr	r5, .L2023+8
	mov	r6, #0
	ldr	r9, .L2023+12
.L2003:
	cmp	r7, r6
	bne	.L2010
	mov	r0, #0
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2002:
	sub	r3, r10, r7
	add	r2, sp, #12
	uxtb	r3, r3
	mov	r1, r8
	mov	r0, r6
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #2234]	@ zero_extendqisi2
	ldr	r3, [sp, #16]
	cmp	r2, r3
	mvnls	r3, #0
	strls	r3, [r6]
	bls	.L1998
	add	r3, r9, r3
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
	mov	r1, #1
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
	ldr	r3, [r9, #40]
	add	r1, r1, r3
	bl	FlashProgFirstCmd
	ldr	r3, [r6, #8]
	mov	r1, #1
	ldr	r2, [r6, #12]
	mov	r0, r5
	cmp	r3, #0
	addne	r3, r3, #2048
	cmp	r2, #0
	addne	r2, r2, #8
	str	r2, [sp]
	mov	r2, fp
	bl	NandcXferData
	ldr	r3, [r9, #40]
	mov	r0, r5
	ldr	r1, [sp, #12]
	add	r1, r1, r3
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	ldr	r1, [sp, #12]
	bl	FlashReadStatus
	tst	r0, #1
	mov	r0, r5
	mvnne	r3, #0
	strne	r3, [r6]
	bl	NandcFlashDeCs
.L1998:
	add	r7, r7, #1
	add	r6, r6, #36
	b	.L1996
.L2010:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L2004
	ldr	r1, [r4, #4]
	ldr	r0, .L2023+16
	bl	printk
.L2005:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L2003
.L2004:
	sub	r3, r7, r6
	add	r2, sp, #12
	uxtb	r3, r3
	mov	r1, r8
	mov	r0, r4
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #1732]
	mov	r3, #0
	mov	lr, r4
	add	ip, sp, #20
	str	r3, [r2]
	ldr	r2, [r5, #1736]
	str	r3, [r2]
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	mov	r2, r8
	ldr	r3, [lr]
	mov	r1, #1
	add	r0, sp, #20
	str	r3, [ip]
	ldr	r3, [r5, #1732]
	str	r3, [sp, #28]
	ldr	r3, [r5, #1736]
	str	r3, [sp, #32]
	bl	FlashReadPages
	ldr	r10, [sp, #20]
	cmn	r10, #1
	bne	.L2006
	ldr	r1, [r4, #4]
	ldr	r0, .L2023+20
	bl	printk
	str	r10, [r4]
.L2006:
	ldr	r10, [sp, #20]
	cmp	r10, #256
	bne	.L2007
	ldr	r1, [r4, #4]
	ldr	r0, .L2023+24
	bl	printk
	str	r10, [r4]
.L2007:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L2008
	ldr	r2, [r3]
	ldr	r3, [r5, #1736]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2008
	ldr	r1, [r4, #4]
	ldr	r0, .L2023+28
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2008:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2005
	ldr	r2, [r3]
	ldr	r3, [r5, #1732]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2005
	ldr	r1, [r4, #4]
	mov	r0, r9
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
	b	.L2005
.L2024:
	.align	2
.L2023:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC108
	.word	.LC104
	.word	.LC105
	.word	.LC106
	.word	.LC107
	.fnend
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FtlLoadFactoryBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r7, #0
	ldr	r5, .L2036
	ldr	r6, .L2036+4
	ldr	r3, [r5, #-524]
	ldr	r8, [r5, #-500]
	sub	r10, r6, #120
	sub	r9, r6, #78
	str	r3, [r5, #1768]
	str	r8, [r5, #1772]
.L2026:
	ldrh	r3, [r10]
	cmp	r7, r3
	bcc	.L2031
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2031:
	ldrh	r4, [r9]
	mvn	r3, #0
	ldr	fp, .L2036+8
	strh	r3, [r6, #2]!	@ movhi
	add	r4, r4, r3
	movw	r3, #61664
	uxth	r4, r4
.L2027:
	ldrh	r2, [r9]
	sub	r1, r2, #16
	cmp	r4, r1
	ble	.L2029
	mla	r2, r7, r2, r4
	str	r3, [sp, #4]
	mov	r0, fp
	lsl	r2, r2, #10
	str	r2, [r5, #1764]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #1760]
	ldr	r3, [sp, #4]
	cmn	r2, #1
	beq	.L2028
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L2028
	strh	r4, [r6]	@ movhi
.L2029:
	add	r7, r7, #1
	b	.L2026
.L2028:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2027
.L2037:
	.align	2
.L2036:
	.word	.LANCHOR2
	.word	.LANCHOR0+2466
	.word	.LANCHOR2+1760
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2051
	cmp	r1, #1
	movweq	r2, #2392
	movwne	r2, #2390
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	lsl	r8, r0, #10
	ldrh	r5, [r3, r2]
	.pad #104
	sub	sp, sp, #104
	add	r3, sp, #40
	mov	r2, r1
	str	r3, [sp, #16]
	mov	r7, r1
	sub	r5, r5, #1
	mov	r6, #0
	sxth	r5, r5
	mov	r1, #1
	add	r0, sp, #4
	str	r6, [sp, #12]
	orr	r3, r5, r8
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L2041
.L2042:
	cmp	r6, r5
	ble	.L2045
.L2041:
	mov	r0, r5
	add	sp, sp, #104
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2045:
	add	r3, r6, r5
	mov	r2, r7
	add	r3, r3, r3, lsr #31
	mov	r1, #1
	add	r0, sp, #4
	asr	r4, r3, #1
	sxth	r3, r4
	orr	r3, r3, r8
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L2043
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L2043
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r4, #1
	sxthne	r5, r4
	bne	.L2042
.L2043:
	add	r4, r4, #1
	sxth	r6, r4
	b	.L2042
.L2052:
	.align	2
.L2051:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L2085
	ldr	r5, .L2085+4
	ldr	r3, [r4, #-524]
	add	r8, r4, #1760
	ldr	r7, [r4, #-500]
	ldr	r9, .L2085+8
	str	r3, [r4, #1768]
	str	r7, [r4, #1772]
	bl	FtlBbtMemInit
	movw	r3, #2388
	ldrh	r6, [r5, r3]
	sub	r6, r6, #1
	uxth	r6, r6
.L2054:
	ldrh	r3, [r9]
	sub	r3, r3, #48
	cmp	r6, r3
	ble	.L2057
	lsl	r3, r6, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r8
	str	r3, [r4, #1764]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	bne	.L2055
	ldr	r3, [r4, #1764]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r4, #1764]
	bl	FlashReadPages
.L2055:
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	beq	.L2056
	ldrh	r2, [r7]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2056
	movw	r3, #2456
	strh	r6, [r5, r3]	@ movhi
	ldr	r3, [r7, #4]
	str	r3, [r5, #2464]
	ldr	r3, .L2085+12
	ldrh	r2, [r7, #8]
	strh	r2, [r3, #4]	@ movhi
.L2057:
	movw	r8, #2456
	movw	r2, #65535
	ldrh	r3, [r5, r8]
	ldr	r6, .L2085+12
	cmp	r3, r2
	beq	.L2071
	ldrh	r3, [r6, #4]
	cmp	r3, r2
	beq	.L2061
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L2085+16
	str	r3, [r4, #1764]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	beq	.L2061
	ldrh	r2, [r7]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2061
	ldr	r3, [r7, #4]
	ldr	r2, [r5, #2464]
	cmp	r3, r2
	ldrhhi	r2, [r6, #4]
	strhi	r3, [r5, #2464]
	ldrhhi	r3, [r7, #8]
	strhhi	r2, [r5, r8]	@ movhi
	strhhi	r3, [r6, #4]	@ movhi
.L2061:
	ldr	r9, .L2085+16
	movw	r3, #2456
	mov	r1, #1
	ldrh	r0, [r5, r3]
	movw	r10, #61649
	bl	FtlGetLastWrittenPage
	sxth	r8, r0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L2063:
	cmp	r8, #0
	blt	.L2068
	ldrh	r3, [r6]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r9
	orr	r3, r8, r3, lsl #10
	str	r3, [r4, #1764]
	ldr	r3, [r4, #-524]
	str	r3, [r4, #1768]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	beq	.L2064
	ldrh	r3, [r7]
	cmp	r3, r10
	bne	.L2064
.L2068:
	ldrh	r3, [r7, #10]
	ldrh	r0, [r7, #12]
	strh	r3, [r6, #6]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2065
.L2066:
	ldr	r6, .L2085+20
	mov	r5, #0
	ldr	r8, .L2085+24
	sub	r7, r6, #134
.L2069:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcc	.L2070
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2056:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2054
.L2064:
	sub	r8, r8, #1
	sxth	r8, r8
	b	.L2063
.L2065:
	ldr	r2, [r5, #2320]
	cmp	r0, r2
	beq	.L2066
	movw	r3, #2334
	ldrh	r3, [r5, r3]
	lsr	r3, r3, #2
	cmp	r2, r3
	cmpcc	r0, r3
	bcs	.L2066
	bl	FtlSysBlkNumInit
	b	.L2066
.L2070:
	ldrh	r2, [r8]
	ldr	r1, [r4, #1768]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	ftl_memcpy
	b	.L2069
.L2071:
	mvn	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2086:
	.align	2
.L2085:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2388
	.word	.LANCHOR0+2456
	.word	.LANCHOR2+1760
	.word	.LANCHOR0+2480
	.word	.LANCHOR2-436
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlScanSysBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, #0
	ldr	r5, .L2166
	movw	r3, #2438
	mov	r1, r6
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L2166+4
	movw	r8, #2412
	ldr	r2, [r5, #2420]
	strh	r6, [r5, r3]	@ movhi
	sub	r7, r4, #388
	ldr	r0, [r4, #-444]
	strh	r6, [r7]	@ movhi
	lsl	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r5, #2420]
	mov	r1, r6
	ldr	r0, [r4, #-472]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r5, r8]
	mov	r1, r6
	ldr	r0, [r4, #-460]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, r8]
	mov	r1, r6
	ldr	r0, [r5, #2440]
	lsl	r2, r2, #1
	bl	ftl_memset
	sub	r0, r4, #3296
	mov	r2, #16
	mov	r1, #255
	sub	r0, r0, #4
	bl	ftl_memset
	movw	r3, #2332
	str	r7, [sp]
	ldrh	fp, [r5, r3]
	str	r5, [sp, #4]
.L2088:
	ldr	r3, .L2166+8
	ldr	r2, .L2166
	ldrh	r3, [r3]
	cmp	r3, fp
	bls	.L2128
	ldr	r1, [r4, #-2692]
	mov	r6, #0
	ldr	r3, .L2166+12
	mov	r5, r6
	ldr	r7, [r4, #-536]
	mov	r8, #36
	str	r1, [sp, #8]
	ldr	r1, .L2166+16
	ldrh	r2, [r3]
	ldr	r3, [r4, #-2696]
	ldrh	r10, [r1]
	sub	r9, r1, #52
	b	.L2129
.L2090:
	mov	r1, fp
	ldrb	r0, [r9, r6]	@ zero_extendqisi2
	str	r3, [sp, #20]
	str	r2, [sp, #16]
	bl	V2P_block
	str	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #20]
	bne	.L2089
	ldr	r1, [sp, #12]
	mla	r0, r8, r5, r7
	lsl	r1, r1, #10
	stmib	r0, {r1, r3}
	mul	r1, r10, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	ip, r1, #3
	cmp	r1, #0
	movlt	r1, ip
	ldr	ip, [sp, #8]
	bic	r1, r1, #3
	add	r1, ip, r1
	str	r1, [r0, #12]
.L2089:
	add	r6, r6, #1
.L2129:
	uxth	r1, r6
	cmp	r2, r1
	bhi	.L2090
	cmp	r5, #0
	bne	.L2091
.L2127:
	add	r3, fp, #1
	uxth	fp, r3
	b	.L2088
.L2091:
	mov	r8, #0
	mov	r2, #1
	mov	r1, r5
	mov	r0, r7
	bl	FlashReadPages
.L2092:
	uxth	r3, r8
	cmp	r5, r3
	bls	.L2127
	mov	r9, #36
	ldr	r3, [r4, #-536]
	mul	r9, r9, r8
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r6, [r2, #4]
	ldr	r7, [r2, #12]
	cmn	r3, #1
	ubfx	r6, r6, #10, #16
	bne	.L2095
	mov	r10, #16
.L2097:
	ldr	r0, [r4, #-536]
	mov	r2, #1
	mov	r1, r2
	add	r0, r0, r9
	ldr	r3, [r0, #4]
	add	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r7]
	movw	r2, #65535
	cmp	r3, r2
	ldr	r3, [r4, #-536]
	bne	.L2094
	mvn	r2, #0
	str	r2, [r3, r9]
	ldr	r3, [r4, #-536]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	beq	.L2096
.L2095:
	ldr	r2, [r4, #-3332]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L2098
	cmp	r2, r3
	bhi	.L2099
.L2098:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r4, #-3332]
.L2099:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2101
	bhi	.L2102
	movw	r3, #61574
	cmp	r2, r3
	beq	.L2103
.L2100:
	add	r8, r8, #1
	b	.L2092
.L2094:
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2095
	sub	r10, r10, #1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L2097
.L2096:
	ldr	r3, .L2166
	ldrb	r1, [r3, #152]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L2165
.L2125:
	mov	r0, r6
	bl	FtlFreeSysBlkQueueIn
	b	.L2100
.L2102:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2104
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2100
.L2165:
	mov	r1, #0
	b	.L2125
.L2104:
	ldr	r3, [sp, #4]
	ldr	r2, [sp]
	ldr	ip, [r4, #-444]
	ldr	r0, [r3, #2420]
	ldrh	r2, [r2]
	uxth	r1, r0
	sub	r3, r1, #1
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r3, r3
	sxth	r1, r1
.L2106:
	cmp	r3, r1
	bgt	.L2112
	cmp	r3, #0
	bge	.L2142
	b	.L2100
.L2112:
	ldr	r10, [r7, #4]
	lsl	lr, r3, #2
	ldr	r9, [ip, r3, lsl #2]
	cmp	r10, r9
	bls	.L2107
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L2108
	cmp	r0, r2
	ldrne	r1, .L2166+20
	addne	r2, r2, #1
	strhne	r2, [r1]	@ movhi
.L2108:
	uxth	ip, r3
	mov	r1, #0
.L2109:
	uxth	r0, r1
	sxth	r2, r1
	cmp	r0, ip
	bcc	.L2110
	ldr	r1, [r7, #4]
	cmp	r3, #0
	ldr	r2, [r4, #-444]
	str	r1, [r2, lr]
	lsl	r2, r3, #1
	ldr	r1, [r4, #-472]
	strh	r6, [r1, r2]	@ movhi
	blt	.L2100
	ldr	r2, [sp]
	ldr	r1, .L2166
	ldrh	r2, [r2]
	ldr	r1, [r1, #2420]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L2100
.L2142:
	ldr	r1, [sp]
	add	r2, r2, #1
	strh	r2, [r1]	@ movhi
	ldr	r2, [r4, #-444]
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #-472]
.L2163:
	strh	r6, [r2, r3]	@ movhi
	b	.L2100
.L2110:
	ldr	r0, [r4, #-444]
	add	r1, r1, #1
	add	r9, r0, r2, lsl #2
	ldr	r9, [r9, #4]
	str	r9, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r4, #-472]
	add	r9, r0, r2
	ldrh	r9, [r9, #2]
	strh	r9, [r0, r2]	@ movhi
	b	.L2109
.L2107:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L2106
.L2103:
	ldr	r3, .L2166+24
	ldr	r1, .L2166+28
	ldr	lr, [r4, #-460]
	ldrh	r2, [r3]
	ldrh	r1, [r1]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
.L2115:
	cmp	r3, r0
	ble	.L2120
	ldr	r10, [r7, #4]
	lsl	ip, r3, #2
	ldr	r9, [lr, r3, lsl #2]
	cmp	r10, r9
	bls	.L2116
	sub	r2, r2, r1
	ldr	r0, [lr]
	clz	r2, r2
	uxth	r9, r3
	lsr	r2, r2, #5
	cmp	r0, #0
	orrne	r2, r2, #1
	ldr	r0, .L2166
	cmp	r2, #0
	ldreq	r2, .L2166+28
	addeq	r1, r1, #1
	strheq	r1, [r2]	@ movhi
	mov	r1, #0
.L2118:
	uxth	lr, r1
	sxth	r2, r1
	cmp	lr, r9
	bcc	.L2119
	ldr	r1, [r7, #4]
	ldr	r2, [r4, #-460]
	str	r1, [r2, ip]
	lsl	r2, r3, #1
	ldr	r1, [r0, #2440]
	strh	r6, [r1, r2]	@ movhi
.L2120:
	cmp	r3, #0
	blt	.L2100
	ldr	r0, .L2166+28
	ldrh	r2, [r0, #-26]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2100
	add	r1, r1, #1
	ldr	r2, [r4, #-460]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [sp, #4]
	ldr	r2, [r2, #2440]
	b	.L2163
.L2119:
	ldr	lr, [r4, #-460]
	add	r1, r1, #1
	add	r10, lr, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [lr, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	lr, [r0, #2440]
	add	r10, lr, r2
	ldrh	r10, [r10, #2]
	strh	r10, [lr, r2]	@ movhi
	b	.L2118
.L2116:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L2115
.L2101:
	ldr	r2, .L2166+32
	movw	r1, #65535
	mov	r9, r2
	ldrh	r0, [r9], #4
	cmp	r0, r1
	strheq	r6, [r2]	@ movhi
	beq	.L2164
	ldrh	r0, [r2, #4]
	cmp	r0, r1
	beq	.L2123
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2123:
	ldr	r3, [r7, #4]
	ldr	r2, [r4, #-3292]
	cmp	r2, r3
	strhcs	r6, [r9]	@ movhi
	bcs	.L2100
	ldrh	r3, [r9, #-4]
	strh	r6, [r9, #-4]	@ movhi
	strh	r3, [r9]	@ movhi
	ldr	r3, [r7, #4]
.L2164:
	str	r3, [r4, #-3292]
	b	.L2100
.L2128:
	ldr	r0, [r4, #-472]
	ldrh	r3, [r0]
	cmp	r3, #0
	beq	.L2130
.L2133:
	ldr	r0, [r2, #2440]
	ldrh	r1, [r0]
	cmp	r1, #0
	beq	.L2131
.L2153:
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2130:
	ldr	r1, .L2166+20
	ldrh	r1, [r1]
	cmp	r1, #0
	ldrne	ip, [r2, #2420]
	beq	.L2133
.L2134:
	sxth	r1, r3
	cmp	r1, ip
	bcs	.L2133
	lsl	lr, r1, #1
	add	r3, r3, #1
	ldrh	lr, [r0, lr]
	cmp	lr, #0
	beq	.L2134
	mov	r3, r1
	mov	r5, #0
.L2135:
	ldr	r0, [r2, #2420]
	cmp	r3, r0
	bcs	.L2133
	ldr	ip, [r4, #-472]
	lsl	r0, r3, #1
	sub	lr, r3, r1
	lsl	r6, lr, #1
	ldrh	r7, [ip, r0]
	strh	r7, [ip, r6]	@ movhi
	ldr	ip, [r4, #-444]
	ldr	r6, [ip, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [ip, lr, lsl #2]
	ldr	ip, [r4, #-472]
	strh	r5, [ip, r0]	@ movhi
	b	.L2135
.L2131:
	movw	r3, #2438
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	movwne	r3, #2412
	ldrhne	ip, [r2, r3]
	beq	.L2153
.L2138:
	sxth	r3, r1
	cmp	r3, ip
	mov	lr, r3
	bge	.L2153
	lsl	r5, r3, #1
	add	r1, r1, #1
	ldrh	r5, [r0, r5]
	cmp	r5, #0
	beq	.L2138
	ldr	r5, .L2166+24
	mov	r6, #0
.L2139:
	ldrh	r1, [r5]
	cmp	r3, r1
	bge	.L2153
	ldr	r0, [r2, #2440]
	lsl	r1, r3, #1
	sub	ip, r3, lr
	lsl	r7, ip, #1
	ldrh	r8, [r0, r1]
	strh	r8, [r0, r7]	@ movhi
	ldr	r0, [r4, #-460]
	ldr	r7, [r0, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r7, [r0, ip, lsl #2]
	ldr	r0, [r2, #2440]
	strh	r6, [r0, r1]	@ movhi
	b	.L2139
.L2167:
	.align	2
.L2166:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2334
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2402
	.word	.LANCHOR2-388
	.word	.LANCHOR0+2412
	.word	.LANCHOR0+2438
	.word	.LANCHOR2-3300
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadSysInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldr	r4, .L2197
	.pad #36
	sub	sp, sp, #36
	ldr	r5, .L2197+4
	ldr	r3, [r4, #-524]
	sub	r7, r4, #3296
	ldr	r0, [r4, #-3540]
	str	r3, [r4, #1768]
	ldr	r3, [r4, #-500]
	str	r3, [r4, #1772]
	movw	r3, #2332
	ldrh	r2, [r5, r3]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r0, [r7, #-4]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2169
.L2180:
	mvn	r0, #0
.L2168:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2169:
	mov	r1, #1
	add	fp, r5, #2400
	bl	FtlGetLastWrittenPage
	ldrsh	r9, [r7, #-4]
	sub	r8, r7, #4
	sxth	r6, r0
	add	r0, r0, #1
	strh	r0, [r8, #2]	@ movhi
.L2171:
	cmp	r6, #0
	blt	.L2179
	orr	r3, r6, r9, lsl #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L2197+8
	str	r3, [r4, #1764]
	ldr	r3, [r4, #-524]
	str	r3, [r4, #1768]
	bl	FlashReadPages
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2172
	ldr	r8, [r4, #1772]
	ldr	r3, [r8, #12]
	cmp	r3, #0
	str	r3, [sp, #28]
	beq	.L2172
	ldr	r2, [r4, #1768]
	ldrh	r1, [fp]
	mov	r0, r2
	str	r2, [sp, #24]
	bl	js_hash
	ldr	r3, [sp, #28]
	cmp	r3, r0
	beq	.L2172
	cmp	r6, #0
	bne	.L2173
	mov	r10, r7
	ldr	r2, [sp, #24]
	ldrh	r1, [r10], #-4
	cmp	r9, r1
	beq	.L2173
	ldr	r2, [r2]
	ldrh	r1, [r7, #-4]
	str	r3, [sp, #12]
	str	r2, [sp, #16]
	ldr	r3, [r8, #8]
	ldr	r2, [r4, #1760]
	ldr	r0, .L2197+12
	str	r3, [sp, #8]
	ldr	r3, [r8, #4]
	str	r3, [sp, #4]
	ldr	r3, [r8]
	str	r3, [sp]
	ldr	r3, [r4, #1764]
	bl	printk
	ldr	r3, .L2197+16
	ldrsh	r9, [r10, #4]
	ldrh	r6, [r3]
.L2175:
	sub	r6, r6, #1
	sxth	r6, r6
	b	.L2171
.L2173:
	mvn	r3, #0
	str	r3, [r4, #1760]
.L2172:
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	beq	.L2175
	ldr	r3, [r4, #-524]
	ldr	r2, .L2197+20
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2175
	ldr	r3, [r4, #-500]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2175
.L2179:
	mov	r2, #48
	ldr	r1, [r4, #1768]
	movw	r6, #2332
	ldr	r0, .L2197+24
	bl	ftl_memcpy
	ldrh	r2, [r5, r6]
	ldr	r1, [r4, #1768]
	ldr	r0, [r4, #-3540]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r5, r6]
	ldr	r3, [r4, #1768]
	ldr	r0, [r5, #32]
	lsr	r2, r1, #3
	lsl	r1, r1, #1
	add	r1, r1, #51
	add	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r3, r1
	bl	ftl_memcpy
	movw	r3, #2436
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L2177
	ldrh	r1, [r5, r6]
	movw	r3, #2428
	ldrh	r2, [r5, r3]
	ldr	r0, [r4, #-448]
	lsr	r3, r1, #3
	lsl	r2, r2, #2
	add	r3, r3, r1, lsl #1
	ldr	r1, [r4, #1768]
	add	r3, r3, #52
	ubfx	r3, r3, #2, #14
	add	r1, r1, r3, lsl #2
	bl	ftl_memcpy
.L2177:
	ldr	r2, [r4, #-3596]
	ldr	r3, .L2197+20
	cmp	r2, r3
	bne	.L2180
	ldr	r6, .L2197+24
	movw	r3, #2346
	ldrb	r1, [r4, #-3586]	@ zero_extendqisi2
	ldrh	r3, [r5, r3]
	ldrh	r2, [r6, #8]
	cmp	r1, r3
	strh	r2, [r7, #2]	@ movhi
	bne	.L2180
	movw	r3, #2390
	movw	r1, #2396
	ldrh	r3, [r5, r3]
	ldrh	r1, [r5, r1]
	ldr	r0, [r5, #2336]
	str	r2, [r4, #1796]
	mul	r3, r2, r3
	str	r3, [r5, #2452]
	mul	r3, r3, r1
	str	r3, [r5, #2432]
	ldr	r3, .L2197+28
	ldrh	r3, [r3, #6]
	sub	r0, r0, r3
	movw	r3, #2324
	ldrh	r1, [r5, r3]
	sub	r0, r0, r2
	bl	__aeabi_uidiv
	ldrh	r3, [r6, #16]
	add	r2, r6, #76
	mov	r5, r2
	strh	r0, [r7, #-8]	@ movhi
	ldrh	r0, [r6, #14]
	add	r7, r2, #240
	lsr	r1, r3, #6
	and	r3, r3, #63
	strb	r3, [r4, #-3514]
	strh	r1, [r2, #2]	@ movhi
	ldrh	r1, [r6, #18]
	ldrb	r3, [r4, #-3585]	@ zero_extendqisi2
	strh	r0, [r2]	@ movhi
	strh	r1, [r5, #48]!	@ movhi
	ldrh	r1, [r6, #20]
	strb	r3, [r4, #-3512]
	mvn	r3, #0
	strh	r3, [r7, #-4]	@ movhi
	mov	r3, #0
	strh	r3, [r2, #238]	@ movhi
	lsr	ip, r1, #6
	and	r1, r1, #63
	strb	r1, [r4, #-3466]
	ldrb	r1, [r4, #-3584]	@ zero_extendqisi2
	strh	ip, [r5, #2]	@ movhi
	strb	r3, [r4, #-3278]
	strb	r1, [r4, #-3464]
	ldrh	r1, [r6, #22]
	strb	r3, [r4, #-3276]
	strh	r1, [r2, #96]!	@ movhi
	ldrh	r1, [r6, #24]
	mov	r6, r2
	lsr	ip, r1, #6
	and	r1, r1, #63
	strb	r1, [r4, #-3418]
	ldrb	r1, [r4, #-3583]	@ zero_extendqisi2
	strh	ip, [r2, #2]	@ movhi
	strb	r1, [r4, #-3416]
	str	r3, [r4, #-3344]
	ldr	r1, [r4, #-3564]
	str	r3, [r4, #-3356]
	str	r3, [r4, #-3364]
	str	r3, [r4, #-3348]
	str	r1, [r4, #-3324]
	str	r3, [r4, #-3320]
	ldr	r1, [r4, #-3332]
	str	r3, [r4, #-3312]
	str	r3, [r4, #-3352]
	ldr	r3, [r4, #-3556]
	ldr	r2, [r4, #-3328]
	cmp	r3, r1
	strhi	r3, [r4, #-3332]
	ldr	r3, [r4, #-3560]
	cmp	r3, r2
	strhi	r3, [r4, #-3328]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2183
	ldr	r0, .L2197+32
	bl	make_superblock
.L2183:
	ldrh	r2, [r5]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2184
	ldr	r0, .L2197+36
	bl	make_superblock
.L2184:
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2185
	ldr	r0, .L2197+40
	bl	make_superblock
.L2185:
	ldrh	r2, [r7, #-4]
	movw	r3, #65535
	sub	r0, r7, #4
	cmp	r2, r3
	beq	.L2186
	bl	make_superblock
.L2186:
	mov	r0, #0
	b	.L2168
.L2198:
	.align	2
.L2197:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+1760
	.word	.LC109
	.word	.LANCHOR0+2392
	.word	1179929683
	.word	.LANCHOR2-3596
	.word	.LANCHOR0+2456
	.word	.LANCHOR2-3520
	.word	.LANCHOR2-3472
	.word	.LANCHOR2-3424
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlDumpBlockInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2390
	ldr	r5, .L2211
	.pad #92
	sub	sp, sp, #92
	ubfx	r0, r0, #10, #16
	mov	r9, r1
	ldr	r7, .L2211+4
	ldrh	r8, [r5, r3]
	bl	P2V_block_in_plane
	ldr	r1, .L2211+8
	mov	r6, r0
	ldr	r0, .L2211+12
	bl	printk
	ldr	r2, [r7, #-3540]
	lsl	r3, r6, #1
	mov	r1, r6
	ldr	r0, .L2211+16
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #88
	strh	r6, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r4, [r5, #152]	@ zero_extendqisi2
	str	r7, [sp, #36]
	adds	r3, r4, #0
	movne	r3, #1
	cmp	r9, #0
	movne	r3, #0
	cmp	r3, #0
	moveq	r4, r3
	beq	.L2200
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	movweq	r3, #2392
	ldrheq	r8, [r5, r3]
.L2200:
	movw	r3, #2390
	mov	r6, #0
	mov	r9, #36
	ldrh	r3, [r5, r3]
	mov	r2, r8
	mov	r1, r4
	ldr	r0, .L2211+20
	bl	printk
.L2201:
	ldr	r3, .L2211+24
	add	ip, sp, #54
	movw	r10, #65535
	ldrh	r3, [r3]
	mov	r7, r3
	ldr	r3, .L2211+4
	ldr	r2, [r3, #-2696]
	ldr	r0, [r3, #-536]
	ldr	r3, [r3, #-2692]
	str	r2, [sp, #24]
	ldr	r2, .L2211+28
	str	r3, [sp, #32]
	ldrh	r1, [r2]
	ldrh	lr, [r2, #2]
	mov	r2, #0
	mov	r5, r2
	str	r1, [sp, #28]
.L2202:
	uxth	r3, r2
	cmp	r7, r3
	bhi	.L2204
	ldr	fp, .L2211+32
	mov	r10, #0
	mov	r2, r4
	mov	r1, r5
	bl	FlashReadPages
.L2205:
	uxth	r3, r10
	cmp	r5, r3
	bhi	.L2206
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r8, r6
	bne	.L2201
.L2207:
	mov	r0, #0
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2204:
	ldrh	r3, [ip, #2]!
	cmp	r3, r10
	beq	.L2203
	mla	r1, r9, r5, r0
	orr	r3, r6, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #28]
	mul	r3, r3, r5
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #24]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #8]
	mul	r3, lr, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #32]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #12]
.L2203:
	add	r2, r2, #1
	b	.L2202
.L2206:
	ldr	r3, [sp, #36]
	mul	r0, r9, r10
	ldrh	r1, [sp, #40]
	add	r10, r10, #1
	ldr	ip, [r3, #-536]
	add	r2, ip, r0
	ldr	lr, [r2, #8]
	ldr	r3, [r2, #12]
	ldr	r7, [lr, #4]
	str	r7, [sp, #20]
	ldr	lr, [lr]
	str	lr, [sp, #16]
	ldr	lr, [r3, #12]
	str	lr, [sp, #12]
	ldr	lr, [r3, #8]
	str	lr, [sp, #8]
	ldr	lr, [r3, #4]
	str	lr, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp]
	ldr	r3, [r2, #4]
	ldr	r2, [ip, r0]
	mov	r0, fp
	bl	printk
	b	.L2205
.L2212:
	.align	2
.L2211:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+141
	.word	.LC110
	.word	.LC111
	.word	.LC112
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2400
	.word	.LC113
	.fnend
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
	.align	2
	.global	FtlScanAllBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, #0
	ldr	r5, .L2224
	.pad #84
	sub	sp, sp, #84
	ldr	r1, .L2224+4
	ldr	r0, .L2224+8
	bl	printk
.L2214:
	ldr	r3, .L2224+12
	uxth	r0, r6
	ldrh	r3, [r3]
	cmp	r3, r0
	bhi	.L2222
	mov	r0, #0
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2222:
	add	r4, sp, #80
	movw	r9, #65535
	strh	r0, [r4, #-48]!	@ movhi
	mov	r10, #36
	bl	ftl_get_blk_mode
	uxth	r1, r6
	ldr	ip, [r5, #-3540]
	mov	r3, r0
	ldr	r0, .L2224+16
	lsl	r2, r1, #1
	ldrh	r2, [ip, r2]
	bl	printk
	mov	r0, r4
	bl	make_superblock
	ldr	r3, .L2224+20
	add	ip, sp, #46
	ldr	r0, [r5, #-536]
	ldr	r7, [r5, #-2692]
	ldrh	r2, [r3]
	ldrh	lr, [r3, #76]
	ldrh	r8, [r3, #78]
	str	r2, [sp, #24]
	ldr	r2, [r5, #-2696]
	str	r2, [sp, #28]
	mov	r2, #0
	mov	r4, r2
.L2215:
	ldr	r1, [sp, #24]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L2217
	ldr	r9, .L2224+24
	mov	r7, #0
	mov	r8, #36
	mov	r2, #0
	mov	r1, r4
	bl	FlashReadPages
.L2218:
	uxth	r3, r7
	cmp	r4, r3
	bhi	.L2219
	ldr	r9, .L2224+28
	mov	r7, #0
	mov	r8, #36
	mov	r2, #1
	mov	r1, r4
	ldr	r0, [r5, #-536]
	bl	FlashReadPages
.L2220:
	uxth	r3, r7
	cmp	r4, r3
	bhi	.L2221
	add	r6, r6, #1
	b	.L2214
.L2217:
	ldrh	r3, [ip, #2]!
	cmp	r3, r9
	beq	.L2216
	mla	r1, r10, r4, r0
	lsl	r3, r3, #10
	str	r3, [r1, #4]
	mul	r3, lr, r4
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #28]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #8]
	mul	r3, r8, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	bic	r3, r3, #3
	add	r3, r7, r3
	str	r3, [r1, #12]
.L2216:
	add	r2, r2, #1
	b	.L2215
.L2219:
	mul	r0, r8, r7
	ldr	ip, [r5, #-536]
	ldrh	r1, [sp, #32]
	add	r7, r7, #1
	add	r2, ip, r0
	ldr	lr, [r2, #8]
	ldr	r3, [r2, #12]
	ldr	r10, [lr, #4]
	str	r10, [sp, #20]
	ldr	lr, [lr]
	str	lr, [sp, #16]
	ldr	lr, [r3, #12]
	str	lr, [sp, #12]
	ldr	lr, [r3, #8]
	str	lr, [sp, #8]
	ldr	lr, [r3, #4]
	str	lr, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp]
	ldr	r3, [r2, #4]
	ldr	r2, [ip, r0]
	mov	r0, r9
	bl	printk
	b	.L2218
.L2221:
	mul	r0, r8, r7
	ldr	ip, [r5, #-536]
	ldrh	r1, [sp, #32]
	add	r7, r7, #1
	add	r2, ip, r0
	ldr	lr, [r2, #8]
	ldr	r3, [r2, #12]
	ldr	r10, [lr, #4]
	str	r10, [sp, #20]
	ldr	lr, [lr]
	str	lr, [sp, #16]
	ldr	lr, [r3, #12]
	str	lr, [sp, #12]
	ldr	lr, [r3, #8]
	str	lr, [sp, #8]
	ldr	lr, [r3, #4]
	str	lr, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp]
	ldr	r3, [r2, #4]
	ldr	r2, [ip, r0]
	mov	r0, r9
	bl	printk
	b	.L2220
.L2225:
	.align	2
.L2224:
	.word	.LANCHOR2
	.word	.LANCHOR3+158
	.word	.LC110
	.word	.LANCHOR0+2334
	.word	.LC114
	.word	.LANCHOR0+2324
	.word	.LC115
	.word	.LC116
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	2
	.global	ftl_scan_all_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2243
	movw	r2, #2388
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r5, .L2243+4
	ldrh	r4, [r3, r2]
	add	r10, r5, #1760
	str	r3, [sp, #28]
	sub	r4, r4, #16
	lsl	r9, r4, #10
.L2227:
	ldr	r3, .L2243+8
	ldrh	r3, [r3]
	cmp	r4, r3
	blt	.L2235
	ldr	r1, .L2243+12
	ldr	r0, .L2243+16
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L2235:
	uxth	r7, r4
	mov	r0, r7
	bl	ftl_get_blk_mode
	ldr	r3, [sp, #28]
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2228
	ldr	r3, .L2243+20
	ldrh	r2, [r3]
	cmp	r4, r2
	bge	.L2229
	ldrh	r3, [r3, #72]
	cmp	r4, r3
	blt	.L2229
.L2228:
	cmp	r0, #1
	bne	.L2230
.L2229:
	ldr	r3, .L2243+24
	mov	r8, #-2147483648
	ldrh	r6, [r3]
.L2231:
	mov	r3, r8
	mov	r2, r6
	mov	r1, r4
	ldr	r0, .L2243+28
	bl	printk
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	beq	.L2232
	mov	r3, r8
	mov	r2, r6
	mov	r1, r4
	ldr	r0, .L2243+32
	bl	printk
.L2232:
	ldr	fp, .L2243+36
	mov	r7, #0
.L2233:
	cmp	r7, r6
	bne	.L2234
	add	r4, r4, #1
	add	r9, r9, #1024
	b	.L2227
.L2230:
	ldr	r3, .L2243+40
	mov	r8, #0
	ldrh	r6, [r3]
	b	.L2231
.L2234:
	add	r3, r8, r9
	mov	r2, #0
	add	r3, r3, r7
	mov	r1, #1
	str	r3, [r5, #1764]
	mov	r0, r10
	ldr	r3, [r5, #-524]
	add	r7, r7, #1
	str	r2, [r5, #1760]
	str	r3, [r5, #1768]
	ldr	r3, [r5, #-500]
	str	r3, [r5, #1772]
	bl	FlashReadPages
	ldr	r2, [r5, #1768]
	mov	r0, fp
	ldr	r3, [r5, #1772]
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	ldr	r2, [r2]
	ldr	r1, [r5, #1764]
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r2, [r5, #1760]
	ldr	r3, [r3]
	bl	printk
	b	.L2233
.L2244:
	.align	2
.L2243:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2388
	.word	.LANCHOR3+174
	.word	.LC120
	.word	.LANCHOR0+2332
	.word	.LANCHOR0+2392
	.word	.LC117
	.word	.LC118
	.word	.LC119
	.word	.LANCHOR0+2390
	.fnend
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
	.align	2
	.global	FlashProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r5, .L2298
	str	r1, [sp, #12]
	ldr	ip, [r5, #48]
	ldrb	r8, [r5, #36]	@ zero_extendqisi2
	str	r3, [sp, #24]
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #20]
	bne	.L2246
	ldr	r3, .L2298+4
	mov	r4, r0
	mov	r9, r2
	ldrb	r3, [r3, #477]	@ zero_extendqisi2
	str	r3, [sp, #16]
	add	r3, r5, #1216
	add	r3, r3, #4
	str	r3, [sp, #28]
.L2247:
	ldr	r3, [sp, #12]
	cmp	r8, r3
	bcc	.L2260
	ldr	r7, .L2298+8
	mov	r6, #0
.L2261:
	ldrb	r3, [r5, #2234]	@ zero_extendqisi2
	cmp	r6, r3
	bcc	.L2263
	ldr	r3, [sp, #24]
	cmp	r3, #0
	bne	.L2264
.L2272:
	mov	r0, #0
	b	.L2245
.L2246:
	bl	FlashProgSlc2KPages
.L2245:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2260:
	ldr	r3, [sp, #12]
	mov	r7, #36
	mul	r7, r7, r8
	add	r2, sp, #36
	mov	r1, r9
	sub	r3, r3, r8
	uxtb	r3, r3
	add	fp, r4, r7
	str	r3, [sp]
	mov	r0, fp
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldrb	r3, [r5, #2234]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r0, [sp, #40]
	cmp	r3, r0
	mvnls	r3, #0
	strls	r3, [r4, r7]
	bls	.L2250
	ldrb	r3, [r5, #2244]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r5, r0, lsl #4
	moveq	r10, #0
	ldr	r3, [r3, #2112]
	cmp	r3, #0
	beq	.L2252
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L2252:
	ldr	r3, [sp, #40]
	mov	r1, #0
	cmp	r10, #0
	add	r2, r5, r3, lsl #4
	str	r1, [r2, #2116]
	ldr	r1, [sp, #36]
	str	fp, [r2, #2112]
	str	r1, [r2, #2108]
	addne	r1, r7, #36
	addne	r1, r4, r1
	strne	r1, [r2, #2116]
	add	r2, r5, r3
	ldrb	r6, [r2, #2236]	@ zero_extendqisi2
	add	r3, r5, r3, lsl #4
	strb	r6, [r3, #2104]
	mov	r0, r6
	ldrb	r3, [r5, #2234]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2254
	bl	NandcWaitFlashReady
.L2255:
	ldr	r3, [sp, #20]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L2256
	add	r3, r5, r6
	ldrb	r3, [r3, #2068]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2256
	mov	r3, #0
	ldr	r2, [sp, #28]
	ldrb	r1, [r5, #1217]	@ zero_extendqisi2
	mov	r0, r6
	bl	HynixSetRRPara
.L2256:
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r6
	bne	.L2257
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2257
	bl	flash_enter_slc_mode
.L2258:
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r1, #1
	ldr	r2, [sp, #16]
	mov	r0, r6
	str	r3, [sp]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L2259
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FlashProgDpFirstCmd
	ldr	r3, [sp, #40]
	mov	r0, r6
	ldr	r1, [sp, #36]
	add	r7, r7, #36
	add	r7, r4, r7
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #1180]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r3, [r5, #40]
	mov	r0, r6
	ldr	r1, [sp, #36]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r7, #12]
	mov	r1, #1
	ldr	r2, [sp, #16]
	mov	r0, r6
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
.L2259:
	ldr	r1, [sp, #36]
	mov	r0, r6
	add	r8, r8, r10
	bl	FlashProgSecondCmd
	mov	r0, r6
	bl	NandcFlashDeCs
.L2250:
	add	r8, r8, #1
	b	.L2247
.L2254:
	bl	NandcFlashCs
	ldr	r3, [sp, #40]
	mov	r0, r6
	ldr	r1, [sp, #36]
	add	r3, r5, r3, lsl #2
	ldr	r2, [r3, #1180]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	bl	NandcFlashDeCs
	b	.L2255
.L2257:
	bl	flash_exit_slc_mode
	b	.L2258
.L2263:
	uxtb	r0, r6
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2262
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2262
	ldrb	r0, [r7, r6, lsl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2262:
	add	r6, r6, #1
	b	.L2261
.L2264:
	ldr	r5, .L2298+12
	mov	r6, #0
	ldr	r7, .L2298+16
.L2265:
	ldr	r3, [sp, #12]
	cmp	r6, r3
	beq	.L2272
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L2266
	ldr	r1, [r4, #4]
	ldr	r0, .L2298+20
	bl	printk
.L2267:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L2265
.L2266:
	ldr	r3, [sp, #12]
	add	r2, sp, #36
	mov	r1, r9
	mov	r0, r4
	sub	r3, r3, r6
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #1732]
	mov	r3, #0
	mov	lr, r4
	add	ip, sp, #44
	str	r3, [r2]
	ldr	r2, [r5, #1736]
	str	r3, [r2]
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	mov	r2, r9
	ldr	r3, [lr]
	mov	r1, #1
	add	r0, sp, #44
	str	r3, [ip]
	ldr	r3, [r5, #1732]
	str	r3, [sp, #52]
	ldr	r3, [r5, #1736]
	str	r3, [sp, #56]
	bl	FlashReadPages
	ldr	r8, [sp, #44]
	cmn	r8, #1
	bne	.L2268
	ldr	r1, [r4, #4]
	ldr	r0, .L2298+24
	bl	printk
	str	r8, [r4]
.L2268:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L2269
	ldr	r2, [r3]
	ldr	r3, [r5, #1736]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2269
	ldr	r1, [r4, #4]
	ldr	r0, .L2298+28
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2269:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2267
	ldr	r2, [r3]
	ldr	r3, [r5, #1732]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2267
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
	b	.L2267
.L2299:
	.align	2
.L2298:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+2104
	.word	.LANCHOR2
	.word	.LC108
	.word	.LC104
	.word	.LC105
	.word	.LC107
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FlashTestBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r5, .L2304
	ldr	r3, [r5, #1716]
	cmp	r0, r3
	movcc	r4, #0
	bcc	.L2300
	ldr	r3, [r5, #1724]
	mov	r4, r0
	mov	r2, #32
	add	r0, sp, #40
	mov	r1, #165
	str	r0, [sp, #16]
	str	r3, [sp, #12]
	bl	ftl_memset
	mov	r2, #8
	mov	r1, #90
	ldr	r0, [r5, #1724]
	bl	ftl_memset
	lsl	r0, r4, #10
	mov	r2, #1
	mov	r1, r2
	str	r0, [sp, #8]
	add	r0, sp, #4
	bl	FlashEraseBlocks
	mov	r3, #1
	add	r0, sp, #4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r4, [sp, #4]
	mov	r2, #1
	mov	r1, #0
	add	r0, sp, #4
	adds	r4, r4, #0
	movne	r4, #1
	rsb	r4, r4, #0
	bl	FlashEraseBlocks
.L2300:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, pc}
.L2305:
	.align	2
.L2304:
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2357
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r5, .L2357
	mov	r4, r2
	ldr	r3, [r2, #1728]
	str	r3, [sp, #24]
	ldr	r3, .L2357+4
	ldrh	r0, [r3, #136]
	ldrh	r1, [r3, #138]
	smulbb	r1, r1, r0
	ldr	r0, .L2357+8
	uxth	r1, r1
	str	r1, [sp, #4]
	ldr	r1, [r3, #48]
	ldrb	r1, [r1, #24]	@ zero_extendqisi2
	str	r1, [sp, #28]
	ldrh	r1, [r3, #40]
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	str	r1, [sp, #20]
	cmp	r3, #1
	moveq	r3, r1
	mov	r1, #1
	lsleq	r3, r3, #1
	uxtheq	r3, r3
	streq	r3, [sp, #20]
	bl	printk
	ldr	r0, [r4, #1728]
	mov	r2, #4096
	mov	r1, #0
	ldr	r4, .L2357+4
	bl	ftl_memset
	ldr	r3, [sp, #4]
	lsr	r3, r3, #4
	str	r3, [sp, #32]
	mov	r3, #0
	str	r3, [sp, #12]
.L2308:
	ldrb	r7, [sp, #12]	@ zero_extendqisi2
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	cmp	r3, r7
	bhi	.L2335
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2335:
	ldr	r2, .L2357+12
	lsl	r3, r7, #1
	ldrh	r6, [r2, r3]
	cmp	r6, #0
	bne	.L2309
	ldrh	r2, [r4, #144]
	mov	r1, r6
	ldr	r0, [r5, #1696]
	add	fp, r4, r7, lsl #2
	mov	r8, r6
	mov	r9, r6
	lsl	r2, r2, #9
	bl	ftl_memset
	add	r3, r4, r7
	str	r6, [sp, #8]
	ldrb	r10, [r3, #2236]	@ zero_extendqisi2
.L2310:
	ldrh	r3, [sp, #8]
	ldr	r2, [sp, #4]
	str	r3, [sp, #16]
	cmp	r3, r2
	bcc	.L2321
.L2320:
	mov	r2, r8
	mov	r1, r7
	ldr	r0, .L2357+16
	bl	printk
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	ldr	r2, [sp, #32]
	mul	r3, r2, r3
	cmp	r8, r3
	blt	.L2322
	ldrh	r2, [r4, #144]
	mov	r1, #0
	ldr	r0, [r5, #1696]
	lsl	r2, r2, #9
	bl	ftl_memset
.L2322:
	cmp	r7, #0
	bne	.L2324
	add	r3, r5, #1712
	ldr	r8, .L2357+20
	add	r3, r3, #4
	ldrh	r10, [r3]
	mov	r9, #1
.L2325:
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	cmp	r3, r10
	bhi	.L2327
	ldr	r3, [sp, #4]
	mov	r10, #1
	ldr	r9, .L2357+20
	sub	fp, r3, #1
	sub	r8, r3, #50
	uxth	fp, fp
.L2328:
	cmp	fp, r8
	bgt	.L2330
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	ldr	r2, [r5, #1716]
	sub	r3, r3, r2
	cmp	r6, r3
	bcc	.L2324
	ldrh	r2, [r4, #144]
	mov	r1, #0
	ldr	r0, [r5, #1696]
	lsl	r2, r2, #9
	bl	ftl_memset
.L2324:
	ldr	r3, [sp, #4]
	ldrb	r8, [sp, #12]	@ zero_extendqisi2
	ldr	r9, .L2357+12
	sub	r6, r3, #1
	ldr	fp, .L2357+24
	ldr	r10, .L2357+28
	uxth	r6, r6
	mul	r8, r3, r8
	add	r9, r9, r7, lsl #1
.L2332:
	mov	r1, r7
	mov	r2, r6
	mov	r0, fp
	bl	printk
	ldr	r1, [r5, #1696]
.L2333:
	lsr	r2, r6, #5
	and	r3, r6, #31
	ldr	r2, [r1, r2, lsl #2]
	lsr	r3, r2, r3
	ands	r3, r3, #1
	bne	.L2334
	ldr	r2, [sp, #24]
	add	r0, sp, #44
	strh	r6, [r9]	@ movhi
	strh	r10, [r2]	@ movhi
	strh	r6, [r2, #2]	@ movhi
	strh	r3, [r2, #8]	@ movhi
	mov	r2, #1
	ldr	r3, [r5, #1696]
	mov	r1, r2
	str	r3, [sp, #52]
	ldr	r3, [r5, #1728]
	str	r3, [sp, #56]
	add	r3, r6, r8
	lsl	r3, r3, #10
	str	r3, [sp, #48]
	bl	FlashEraseBlocks
	mov	r3, #1
	add	r0, sp, #44
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [sp, #44]
	cmp	r3, #0
	beq	.L2309
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2332
.L2321:
	mvn	r3, #0
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #28]
	tst	r3, #1
	beq	.L2312
	ldr	r3, [fp, #1180]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r9, r3
	mov	r1, r3
	str	r3, [sp, #36]
	bl	FlashReadSpare
	ldrb	r2, [r4, #36]	@ zero_extendqisi2
	ldr	r3, [sp, #36]
	cmp	r2, #1
	bne	.L2312
	ldr	r1, [r4, #40]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #42]
.L2312:
	ldr	r3, [sp, #28]
	tst	r3, #2
	beq	.L2314
	ldr	r3, [r4, #48]
	add	r2, sp, #43
	mov	r0, r10
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1180]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r9
	bl	FlashReadSpare
.L2314:
	ldr	r2, [r4, #48]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #8
	cmpne	r3, #1
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	beq	.L2315
	ldrb	r2, [r2, #18]	@ zero_extendqisi2
	cmp	r2, #12
	bne	.L2316
.L2315:
	cmp	r3, #0
	ldrbne	r0, [sp, #43]	@ zero_extendqisi2
	clzne	r0, r0
	lsrne	r0, r0, #5
	bne	.L2317
.L2337:
	mov	r0, #1
	b	.L2317
.L2316:
	cmp	r3, #255
	bne	.L2337
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
.L2317:
	ldr	r3, [sp, #28]
	tst	r3, #4
	beq	.L2318
	ldr	r1, [fp, #1180]
	mov	r0, r10
	add	r1, r9, r1
	bl	SandiskProgTestBadBlock
.L2318:
	cmp	r0, #0
	beq	.L2319
	ldr	r2, [sp, #8]
	mov	r1, r7
	ldr	r0, .L2357+32
	add	r8, r8, #1
	bl	printk
	ldr	r3, [sp, #16]
	mov	ip, #1
	ldr	r2, [r5, #1696]
	uxth	r8, r8
	and	r0, r3, #31
	lsr	r1, r3, #5
	ldr	r3, [r2, r1, lsl #2]
	orr	r3, r3, ip, lsl r0
	str	r3, [r2, r1, lsl #2]
	ldr	r2, [sp, #32]
	ldrb	r3, [r4, #2234]	@ zero_extendqisi2
	mul	r3, r2, r3
	cmp	r8, r3
	bgt	.L2320
.L2319:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	ldr	r3, [sp, #20]
	add	r9, r9, r3
	b	.L2310
.L2327:
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2326
	mov	r1, r10
	mov	r0, r8
	bl	printk
	ldr	r1, [r5, #1696]
	lsr	r0, r10, #5
	add	r6, r6, #1
	and	r3, r10, #31
	uxth	r6, r6
	ldr	r2, [r1, r0, lsl #2]
	orr	r3, r2, r9, lsl r3
	str	r3, [r1, r0, lsl #2]
.L2326:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2325
.L2330:
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2329
	mov	r1, fp
	mov	r0, r9
	bl	printk
	ldr	r1, [r5, #1696]
	lsr	r0, fp, #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, lsl #2]
	orr	r3, r2, r10, lsl r3
	str	r3, [r1, r0, lsl #2]
.L2329:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L2328
.L2334:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2333
.L2309:
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
	b	.L2308
.L2358:
	.align	2
.L2357:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC121
	.word	.LANCHOR2+1742
	.word	.LC123
	.word	.LC124
	.word	.LC125
	.word	-3872
	.word	.LC122
	.fnend
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2404
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r2, [r3, #-3612]
	cmp	r2, #0
	movne	r4, #0
	bne	.L2359
	ldr	r5, .L2404+4
	mov	r10, r3
	ldr	fp, .L2404+8
	mov	r9, r2
	mov	r6, r2
	mov	r4, r2
	ldrb	r3, [r5, #2244]	@ zero_extendqisi2
	mov	r8, #36
	str	r1, [sp, #4]
	str	r0, [sp, #8]
	str	r3, [sp, #20]
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	str	r0, [r10, #-548]
	str	r3, [sp, #12]
.L2361:
	ldrh	r0, [fp]
	uxth	r1, r9
	cmp	r0, r1
	bhi	.L2365
	cmp	r6, #0
	beq	.L2359
	ldr	r3, [sp, #12]
	mov	r8, #0
	mov	r2, r6
	ldr	r0, [r10, #-3608]
	strb	r8, [r5, #2244]
	mov	r9, #36
	adds	r7, r3, #0
	movne	r7, #1
	mov	r1, r7
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	strb	r3, [r5, #2244]
.L2367:
	uxth	r2, r8
	cmp	r6, r2
	bhi	.L2369
	ldr	r3, [sp, #4]
	cmp	r3, #0
	bne	.L2370
	uxth	r7, r7
	mov	r3, #6
	str	r3, [sp, #16]
	mov	r3, #1
	str	r3, [sp, #12]
.L2371:
	ldr	r6, .L2404
	mov	r9, #0
.L2380:
	ldr	fp, .L2404+4
	mov	r10, #0
	mov	r5, r10
.L2372:
	ldr	r3, .L2404+8
	ldrh	r1, [r3]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L2375
	cmp	r5, #0
	beq	.L2359
	mov	r3, #1
	mov	r8, #0
	mov	r2, r7
	mov	r1, r5
	ldr	r0, [r6, #-3608]
	strb	r8, [fp, #2244]
	bl	FlashProgPages
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	strb	r3, [fp, #2244]
	mov	fp, #36
.L2377:
	uxth	r3, r8
	cmp	r5, r3
	bhi	.L2379
	ldr	r3, [sp, #16]
	add	r9, r9, r3
	ldr	r3, [sp, #12]
	uxth	r9, r9
	cmp	r3, r9
	bhi	.L2380
	mov	r8, #0
	mov	r9, #36
.L2381:
	uxth	r3, r8
	cmp	r5, r3
	ldr	r3, [sp, #4]
	bhi	.L2383
	adds	r0, r3, #0
	ldr	r3, [sp, #8]
	movne	r0, #1
	cmp	r3, #63
	orrls	r0, r0, #1
	cmp	r0, #0
	beq	.L2359
	mov	r2, r5
	mov	r1, r7
	ldr	r0, [r6, #-3608]
	bl	FlashEraseBlocks
.L2359:
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2365:
	uxth	r1, r9
	ldr	ip, [r10, #-3608]
	mov	r3, #0
	mul	r0, r8, r1
	str	r3, [ip, r0]
	add	r0, r5, r1
	ldrb	r0, [r0, #2350]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	bl	V2P_block
	ldr	r3, [sp, #4]
	mov	r7, r0
	cmp	r3, #0
	beq	.L2362
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2363
.L2362:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L2363
	ldr	r3, .L2404+12
	lsl	r7, r7, #10
	ldr	ip, [r10, #-3608]
	ldrh	r1, [r3]
	mla	ip, r8, r6, ip
	mul	r1, r6, r1
	add	r6, r6, #1
	uxth	r6, r6
	str	r0, [ip, #8]
	str	r7, [ip, #4]
	add	r0, r1, #3
	cmp	r1, #0
	movlt	r1, r0
	ldr	r0, [r10, #-496]
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [ip, #12]
.L2363:
	add	r9, r9, #1
	b	.L2361
.L2369:
	mul	r2, r9, r8
	ldr	r1, [r10, #-3608]
	add	ip, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L2368
	ldr	r0, [ip, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L2368:
	add	r8, r8, #1
	b	.L2367
.L2370:
	movw	r3, #2392
	ldrh	r3, [r5, r3]
	str	r3, [sp, #12]
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #12]
	movne	r7, #1
	moveq	r7, #1
	strne	r7, [sp, #16]
	lsreq	r3, r3, #2
	streq	r3, [sp, #16]
	b	.L2371
.L2375:
	uxth	r3, r10
	mov	r2, #36
	ldr	r0, [r6, #-3608]
	mul	r1, r2, r3
	add	r3, fp, r3
	mov	r2, #0
	str	r2, [r0, r1]
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #2350]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp, #4]
	mov	r8, r0
	cmp	r3, #0
	beq	.L2373
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2374
.L2373:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L2374
	ldr	r1, [r6, #-3608]
	mov	r3, #36
	add	r8, r9, r8, lsl #10
	mla	r1, r3, r5, r1
	ldr	r3, [r6, #-508]
	str	r3, [r1, #8]
	ldr	r3, .L2404+12
	str	r8, [r1, #4]
	ldrh	r3, [r3]
	mul	r3, r5, r3
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r6, #-504]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L2374:
	add	r10, r10, #1
	b	.L2372
.L2379:
	mul	r3, fp, r8
	ldr	r2, [r6, #-3608]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2378
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L2378:
	add	r8, r8, #1
	b	.L2377
.L2383:
	cmp	r3, #0
	beq	.L2382
	mul	r3, r9, r8
	ldr	r2, [r6, #-3608]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L2382
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2382:
	add	r8, r8, #1
	b	.L2381
.L2405:
	.align	2
.L2404:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2402
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlBbmTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L2421
	ldr	r5, [r4, #-3612]
	cmp	r5, #0
	bne	.L2408
	ldr	r7, .L2421+4
	sub	r10, r4, #436
	ldr	r8, .L2421+8
	mov	r1, r5
	ldr	r0, [r4, #-524]
	ldr	r3, [r4, #-500]
	mov	r6, r7
	sub	r7, r7, #2400
	ldrh	r2, [r6], #80
	mov	r9, r7
	str	r0, [r4, #1768]
	str	r3, [r4, #1772]
	bl	ftl_memset
.L2409:
	ldrh	r3, [r8]
	cmp	r5, r3
	blt	.L2410
	ldr	r6, [r4, #1772]
	mov	r2, #16
	mov	r1, #255
	ldr	r5, .L2421+12
	ldr	fp, .L2421+16
	mov	r0, r6
	bl	ftl_memset
	ldr	r3, .L2421+20
	strh	r3, [r6]	@ movhi
	ldr	r3, [r7, #2464]
	str	r3, [r6, #4]
	movw	r3, #2456
	ldrh	r3, [r7, r3]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r5, #4]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r5, #6]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r7, #2320]
	mov	r7, #0
	mov	r8, r7
	strh	r3, [r6, #12]	@ movhi
.L2411:
	ldr	r3, [r4, #-524]
	mov	r10, #0
	ldrh	r2, [r5, #2]
	ldrh	r1, [r5]
	str	r3, [r4, #1768]
	ldr	r3, [r4, #-500]
	str	r10, [r4, #1760]
	str	r3, [r4, #1772]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #1764]
	ldrh	r3, [r5, #4]
	str	r0, [sp]
	mov	r0, fp
	bl	printk
	ldr	r3, .L2421+24
	ldrh	r2, [r5, #2]
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L2412
	ldr	r3, [r9, #2464]
	ldrh	r2, [r5]
	ldr	r0, [r4, #-3608]
	add	r3, r3, #1
	strh	r10, [r5, #2]	@ movhi
	str	r3, [r9, #2464]
	str	r3, [r6, #4]
	ldrh	r3, [r5, #4]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r5, #4]	@ movhi
	mov	r2, #1
	strh	r3, [r5]	@ movhi
	mov	r1, r2
	lsl	r3, r3, #10
	str	r3, [r4, #1764]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L2412:
	mov	r3, #1
	ldr	r0, .L2421+28
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	add	r3, r3, #1
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	bne	.L2413
	add	r7, r7, #1
	ldr	r1, [r4, #1764]
	uxth	r7, r7
	ldr	r0, .L2421+32
	bl	printk
	cmp	r7, #3
	bls	.L2411
	mov	r2, r7
	ldr	r1, [r4, #1764]
	ldr	r0, .L2421+36
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-3612]
.L2408:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2410:
	ldrh	r2, [r10]
	ldr	r3, [r4, #1768]
	ldr	r1, [r6, #4]!
	mul	r0, r5, r2
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L2409
.L2416:
	mov	r8, #1
	b	.L2411
.L2413:
	add	r8, r8, #1
	cmp	r8, #1
	ble	.L2416
	cmp	r3, #256
	bne	.L2408
	b	.L2411
.L2422:
	.align	2
.L2421:
	.word	.LANCHOR2
	.word	.LANCHOR0+2400
	.word	.LANCHOR0+2346
	.word	.LANCHOR0+2456
	.word	.LC126
	.word	-3887
	.word	.LANCHOR0+2392
	.word	.LANCHOR2+1760
	.word	.LC127
	.word	.LC128
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L2473
	ldr	r3, [r4, #-3612]
	cmp	r3, #0
	bne	.L2424
	sub	r9, r4, #3520
	mov	r7, r0
	sub	r3, r9, #8
	str	r3, [sp, #12]
.L2425:
	ldr	r3, .L2473+4
	ldr	r8, .L2473+8
	ldrb	r2, [r7, #8]	@ zero_extendqisi2
	cmp	r7, r3
	bne	.L2426
	ldrh	r0, [r9, #-4]
	ldr	lr, [r4, #-2708]
	lsr	ip, r0, #1
	mul	r5, lr, r0
	add	r1, ip, #1
	add	r1, r1, r5, lsr #2
	ldr	r5, [r8, #2248]
	uxth	r1, r1
	cmp	r5, #0
	beq	.L2427
	ldr	r5, [r4, #-3308]
	cmp	r5, #39
	bhi	.L2427
	cmp	r5, #2
	bls	.L2453
	cmp	lr, #0
	movne	r0, #0
	andeq	r0, r0, #1
	cmp	r0, #0
	moveq	r1, ip
	beq	.L2427
.L2453:
	mov	r1, #0
	b	.L2428
.L2426:
	cmp	r2, #1
	bne	.L2453
	ldr	r1, .L2473+12
	ldrh	r1, [r1]
	cmp	r1, #1
	beq	.L2453
	ldrb	r1, [r8, #152]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L2453
	ldr	ip, [r8, #2248]
	ldrh	r0, [r9, #-4]
	cmp	ip, #0
	lsr	r1, r0, #3
	beq	.L2427
	ldr	ip, [r4, #-3308]
	cmp	ip, #1
	rsbls	r0, r0, r0, lsl #3
	ubfxls	r1, r0, #3, #16
.L2427:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
.L2428:
	ldr	r0, [sp, #12]
	bl	List_pop_index_node
	ldrh	r2, [r9, #-4]
	uxth	r3, r0
	ldr	r10, .L2473+16
	str	r3, [sp, #4]
	sub	r2, r2, #1
	strh	r2, [r9, #-4]	@ movhi
	ldrh	r2, [r10]
	cmp	r2, r3
	bls	.L2425
	ldr	r2, [r4, #-3540]
	lsl	r5, r3, #1
	ldrh	r6, [r2, r5]
	cmp	r6, #0
	bne	.L2425
	ldrh	r3, [sp, #4]
	mov	r0, r7
	strh	r3, [r7]	@ movhi
	bl	make_superblock
	ldrb	r2, [r7, #7]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2470
	ldr	r0, [r4, #-3608]
	add	r3, r7, #16
	ldrh	ip, [r10, #-8]
	mov	r1, #36
	str	r3, [sp, #8]
	add	fp, r7, #16
	mov	r2, r0
	str	r1, [sp, #16]
	mla	r3, r1, ip, r0
	mov	ip, r6
.L2431:
	cmp	r3, r2
	bne	.L2433
	ldr	r2, [r8, #2248]
	adds	r2, r2, #0
	movne	r2, #1
	cmp	r7, r9
	movne	r2, #0
	cmp	r2, #0
	beq	.L2434
	ldr	r2, [r4, #-3604]
	ldrh	r2, [r2, r5]
	cmp	r2, #40
	movhi	r2, #0
	strbhi	r2, [r4, #-3512]
.L2434:
	ldrb	r2, [r7, #8]	@ zero_extendqisi2
	ldr	r1, [r4, #-3604]
	ldr	fp, .L2473+20
	cmp	r2, #0
	ldrh	r2, [r1, r5]
	bne	.L2435
	cmp	r2, #0
	ldrhne	r0, [fp]
	moveq	r2, #2
	addne	r2, r2, r0
	ldr	r0, [sp, #4]
	strh	r2, [r1, r5]	@ movhi
	mov	r1, #0
	ldr	r2, [r4, #-3324]
	add	r2, r2, #1
	str	r2, [r4, #-3324]
	bl	ftl_set_blk_mode
.L2438:
	ldr	r2, [r4, #-3604]
	ldr	r1, [r4, #-3312]
	ldr	ip, [r4, #-3324]
	ldrh	r2, [r2, r5]
	ldrh	r0, [fp]
	cmp	r2, r1
	ldrh	r1, [r10]
	strhi	r2, [r4, #-3312]
	ldr	r2, [r4, #-3320]
	mla	r0, ip, r0, r2
	bl	__aeabi_uidiv
	ldr	r1, [r4, #-480]
	str	r0, [r4, #-3316]
	ldr	r0, [r4, #-3608]
	ldr	r2, [r1, #16]
	ldr	ip, .L2473+24
	add	r2, r2, #1
	str	r2, [r1, #16]
	mov	r1, #36
	mla	r1, r1, r6, r0
	add	r2, r0, #4
	add	r1, r1, #40
.L2440:
	add	r2, r2, #36
	cmp	r1, r2
	bne	.L2441
	ldrb	r2, [r8, #152]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2442
	ldrb	r2, [r7, #8]	@ zero_extendqisi2
	ldr	r0, [r4, #-3608]
	cmp	r2, #1
	mov	r2, r6
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2442:
	ldrb	r1, [r7, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-3608]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	fp, r10
	mov	r1, #36
.L2444:
	uxth	r2, r10
	cmp	r6, r2
	bhi	.L2446
	cmp	fp, #0
	ble	.L2447
	ldr	r0, [sp, #4]
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2447:
	ldrb	r1, [r7, #7]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L2448
.L2470:
	ldr	r2, [r4, #-3540]
	mvn	r1, #0
	strh	r1, [r2, r5]	@ movhi
	b	.L2425
.L2433:
	str	ip, [r2, #8]
	movw	lr, #65535
	str	ip, [r2, #12]
	add	r2, r2, #36
	ldrh	r1, [fp], #2
	cmp	r1, lr
	ldrne	lr, [sp, #16]
	lslne	r1, r1, #10
	mlane	lr, lr, r6, r0
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r1, [lr, #4]
	b	.L2431
.L2435:
	add	r2, r2, #1
	ldr	r0, [sp, #4]
	strh	r2, [r1, r5]	@ movhi
	ldr	r2, [r4, #-3320]
	add	r2, r2, #1
	str	r2, [r4, #-3320]
	bl	ftl_set_blk_mode.part.9
	b	.L2438
.L2441:
	ldr	r0, [r2, #-36]
	and	r0, r0, ip
	str	r0, [r2, #-36]
	b	.L2440
.L2446:
	mul	r2, r1, r10
	ldr	r0, [r4, #-3608]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	cmn	r2, #1
	bne	.L2445
	ldr	r0, [ip, #4]
	add	fp, fp, #1
	str	r1, [sp, #20]
	str	r2, [sp, #16]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #8]
	ldr	r1, [sp, #20]
	strh	r2, [r3]	@ movhi
	ldrb	r2, [r7, #7]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r7, #7]
.L2445:
	ldr	r3, [sp, #8]
	add	r10, r10, #1
	add	r3, r3, #2
	str	r3, [sp, #8]
	b	.L2444
.L2448:
	movw	r2, #2390
	ldrh	r3, [sp, #4]
	ldrh	r2, [r8, r2]
	strh	r3, [r7]	@ movhi
	smulbb	r2, r2, r1
	mov	r1, #0
	strh	r1, [r7, #2]	@ movhi
	strb	r1, [r7, #6]
	ldr	r1, [r4, #-3332]
	uxth	r2, r2
	strh	r2, [r7, #4]	@ movhi
	str	r1, [r7, #12]
	add	r1, r1, #1
	str	r1, [r4, #-3332]
	ldrh	r3, [r7]
	ldr	r1, [r4, #-3540]
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
.L2424:
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2474:
	.align	2
.L2473:
	.word	.LANCHOR2
	.word	.LANCHOR2-3424
	.word	.LANCHOR0
	.word	.LANCHOR0+2344
	.word	.LANCHOR0+2332
	.word	.LANCHOR0+2382
	.word	-1024
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L2488
	ldrh	r3, [r4, #-2]
	cmp	r3, #0
	beq	.L2476
	ldr	r9, .L2488+4
	mov	r6, #0
	ldr	r8, .L2488+8
	ldr	r10, .L2488+12
	str	r0, [sp]
.L2477:
	ldrh	r2, [r8]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L2483
	bl	FtlGcReFreshBadBlk
.L2476:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2483:
	uxtah	r3, r9, r6
	ldr	r1, [sp]
	mov	fp, #0
	ldrb	r0, [r3, #2350]	@ zero_extendqisi2
	bl	V2P_block
	mov	r7, r0
.L2478:
	ldrh	r3, [r4, #-2]
	uxth	r5, fp
	cmp	r3, r5
	addls	r6, r6, #1
	bls	.L2477
.L2482:
	uxth	r3, fp
	lsl	r1, r3, #1
	ldrh	r1, [r4, r1]
	cmp	r1, r7
	bne	.L2479
	mov	r1, r7
	mov	r0, r10
	str	r3, [sp, #4]
	bl	printk
	mov	r0, r7
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [sp, #4]
	ldrh	r1, [r4, #-2]
	add	r3, r4, r3, lsl #1
.L2480:
	cmp	r5, r1
	bcc	.L2481
	sub	r1, r1, #1
	strh	r1, [r4, #-2]	@ movhi
.L2479:
	add	fp, fp, #1
	b	.L2478
.L2481:
	ldrh	r0, [r3, #2]!
	add	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r3, #-2]	@ movhi
	b	.L2480
.L2489:
	.align	2
.L2488:
	.word	.LANCHOR2-2656
	.word	.LANCHOR0
	.word	.LANCHOR0+2324
	.word	.LC129
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2499
	lsl	r3, r0, #1
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r1, [r2, #-3540]
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2491
	sub	r1, r2, #3280
	mov	r4, r0
	ldrh	r0, [r1, #-4]
	sub	r5, r2, #3520
	cmp	r0, r4
	mvneq	r3, #0
	strheq	r3, [r1, #-4]	@ movhi
	beq	.L2493
	ldrh	r1, [r5]
	cmp	r1, r4
	beq	.L2490
	sub	r1, r2, #3472
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2490
	sub	r2, r2, #3424
	ldrh	r2, [r2]
	cmp	r2, r4
	beq	.L2490
.L2493:
	mov	r1, r4
	ldr	r0, .L2499+4
	bl	List_remove_node
	ldrh	r3, [r5, #-12]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r5, #-12]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r3, #1
.L2490:
	mov	r0, r3
	pop	{r4, r5, r6, pc}
.L2491:
	bl	List_update_data_list
	mov	r3, #0
	b	.L2490
.L2500:
	.align	2
.L2499:
	.word	.LANCHOR2
	.word	.LANCHOR2-3544
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.syntax unified
	.arm
	.fpu softvfp
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #65535
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	cmp	r0, r3
	mov	r4, r0
	beq	.L2502
	ldr	r6, .L2512
	lsl	r5, r0, #1
	ldr	r3, [r6, #-3540]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r5]	@ movhi
	bne	.L2502
	mov	r1, r0
	ldr	r0, .L2512+4
	bl	printk
	ldr	r3, [r6, #-3540]
	sub	r7, r6, #3520
	mov	r2, #32
	sub	r8, r7, #8
	mov	r1, r4
	mov	r0, r8
	strh	r2, [r3, r5]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2504
	mov	r1, r4
	mov	r0, r8
	bl	List_remove_node
	ldrh	r3, [r7, #-4]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r7, #-4]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-3540]
	mov	r1, r4
	ldr	r0, .L2512+8
	ldrh	r2, [r3, r5]
	bl	printk
.L2504:
	mov	r0, r4
	bl	FtlGcRefreshBlock
.L2507:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2502:
	ldr	r5, .L2512+12
	movw	r3, #65535
	ldrh	r0, [r5]
	cmp	r0, r3
	strheq	r4, [r5]	@ movhi
	beq	.L2507
	cmp	r4, r0
	beq	.L2507
	bl	update_vpc_list
	adds	r0, r0, #0
	strh	r4, [r5]	@ movhi
	movne	r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L2513:
	.align	2
.L2512:
	.word	.LANCHOR2
	.word	.LC130
	.word	.LC131
	.word	.LANCHOR2-380
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSlcSuperblockCheck
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldrh	r3, [r0, #4]
	cmp	r3, #0
	bxeq	lr
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	bxeq	lr
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r5, .L2529
	ldr	r6, .L2529+4
	add	r3, r0, r3, lsl #1
	ldrh	r3, [r3, #16]
.L2518:
	movw	r1, #65535
	cmp	r3, r1
	beq	.L2520
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2521
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2521
	ldrh	r3, [r4, #2]
	lsl	r3, r3, #1
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L2521
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2520
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	pop	{r4, r5, r6, pc}
.L2520:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldr	r2, .L2529+8
	add	r3, r3, #1
	ldrh	r2, [r2]
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r2, r3
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	strbeq	r3, [r4, #6]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, lsl #1
	ldrh	r3, [r3, #16]
	b	.L2518
.L2521:
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
	cmp	r2, #1
	movne	r3, #0
	cmp	r3, #0
	popeq	{r4, r5, r6, pc}
	movw	r3, #2392
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, r3]
	cmp	r2, r3
	popcc	{r4, r5, r6, pc}
	ldrh	r3, [r4]
	ldr	r2, .L2529+12
	ldrh	r0, [r4, #4]
	ldr	r1, [r2, #-3540]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	movw	r2, #2390
	ldrh	r2, [r5, r2]
	mov	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
	pop	{r4, r5, r6, pc}
.L2530:
	.align	2
.L2529:
	.word	.LANCHOR0
	.word	.LANCHOR2-2620
	.word	.LANCHOR0+2324
	.word	.LANCHOR2
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	get_new_active_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	strb	r3, [r0, #10]
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r5, .L2547
	ldr	r7, .L2547+4
	add	r3, r0, r3, lsl #1
	ldrh	r2, [r3, #16]
.L2532:
	movw	r1, #65535
	cmp	r2, r1
	beq	.L2533
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r6, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2535
	ldr	r0, .L2547+8
	ldrb	r0, [r0, #152]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L2535
	lsl	r0, r6, #1
	ldrh	r0, [r7, r0]
	cmp	r0, r1
	bne	.L2535
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
.L2533:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r5]
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r2, r3
	strb	r3, [r4, #6]
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	strbeq	r3, [r4, #6]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, lsl #1
	ldrh	r2, [r3, #16]
	b	.L2532
.L2535:
	ldr	r7, .L2547+4
	orr	r6, r6, r2, lsl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2536:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r1, #65535
	ldrh	r0, [r5]
.L2538:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r0
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	add	r2, r4, r3, lsl #1
	ldrh	r2, [r2, #16]
	cmp	r2, r1
	beq	.L2538
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2531
	ldr	r2, .L2547+8
	ldrb	r3, [r2, #152]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #2]
	bne	.L2540
	lsl	r3, r3, #1
	ldrh	r3, [r7, r3]
	cmp	r3, r1
	bne	.L2531
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L2531
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2536
.L2540:
	movw	r1, #2392
	ldrh	r1, [r2, r1]
	cmp	r3, r1
	bcc	.L2531
	ldrh	r3, [r4]
	ldr	r1, .L2547+12
	ldrh	ip, [r4, #4]
	ldr	r0, [r1, #-3540]
	lsl	r3, r3, #1
	ldrh	r1, [r0, r3]
	sub	r1, r1, ip
	strh	r1, [r0, r3]	@ movhi
	movw	r1, #2390
	ldrh	r2, [r2, r1]
	mov	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2531:
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, pc}
.L2548:
	.align	2
.L2547:
	.word	.LANCHOR0+2324
	.word	.LANCHOR2-2620
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlVpcTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L2567
	ldr	r3, [r4, #-3612]
	cmp	r3, #0
	bne	.L2551
	ldr	r2, [r4, #-524]
	sub	r5, r4, #3296
	ldr	r7, [r4, #-500]
	sub	r8, r5, #300
	ldr	r6, .L2567+4
	mov	r1, #255
	str	r2, [r4, #1768]
	ldrh	r2, [r5, #-4]
	str	r7, [r4, #1772]
	add	r9, r6, #2400
	str	r3, [r7, #12]
	strh	r2, [r7, #2]	@ movhi
	ldr	r2, .L2567+8
	strh	r2, [r7]	@ movhi
	ldr	r2, [r4, #-3292]
	stmib	r7, {r2, r3}
	ldr	r3, .L2567+12
	str	r3, [r4, #-3596]
	ldr	r3, .L2567+16
	str	r3, [r4, #-3592]
	ldrh	r3, [r5, #2]
	strh	r3, [r8, #8]	@ movhi
	movw	r3, #2346
	ldrh	r3, [r6, r3]
	strb	r3, [r4, #-3586]
	sub	r3, r4, #3520
	ldrh	r2, [r3]
	strh	r2, [r8, #14]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #-3514]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r8, #16]	@ movhi
	ldrb	r3, [r4, #-3512]	@ zero_extendqisi2
	strb	r3, [r4, #-3585]
	sub	r3, r4, #3472
	ldrh	r2, [r3]
	strh	r2, [r8, #18]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #-3466]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r8, #20]	@ movhi
	ldrb	r3, [r4, #-3464]	@ zero_extendqisi2
	strb	r3, [r4, #-3584]
	sub	r3, r4, #3424
	ldrh	r2, [r3]
	strh	r2, [r8, #22]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #-3418]	@ zero_extendqisi2
	ldr	r0, [r4, #1768]
	orr	r3, r3, r2, lsl #6
	strh	r3, [r8, #24]	@ movhi
	ldrb	r3, [r4, #-3416]	@ zero_extendqisi2
	strb	r3, [r4, #-3583]
	ldr	r3, [r4, #-3324]
	str	r3, [r4, #-3564]
	ldr	r3, [r4, #-3332]
	str	r3, [r4, #-3556]
	ldr	r3, [r4, #-3328]
	str	r3, [r4, #-3560]
	sub	r3, r4, #2656
	ldrh	r2, [r3, #-10]
	ldrh	r3, [r3, #-8]
	strh	r2, [r8, #44]	@ movhi
	ldrh	r2, [r9]
	strh	r3, [r5, #-254]	@ movhi
	bl	ftl_memset
	mov	r1, r8
	mov	r2, #48
	movw	r8, #2332
	ldr	r0, [r4, #1768]
	bl	ftl_memcpy
	ldrh	r2, [r6, r8]
	ldr	r0, [r4, #1768]
	ldr	r1, [r4, #-3540]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r6, r8]
	ldr	r3, [r4, #1768]
	ldr	r1, [r6, #32]
	lsr	r2, r0, #3
	lsl	r0, r0, #1
	add	r0, r0, #51
	add	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r3, r0
	bl	ftl_memcpy
	movw	r3, #2436
	str	r9, [sp, #4]
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	beq	.L2552
	ldrh	r0, [r6, r8]
	movw	r3, #2428
	ldrh	r2, [r6, r3]
	ldr	r1, [r4, #-452]
	lsr	r3, r0, #3
	lsl	r2, r2, #2
	add	r3, r3, r0, lsl #1
	ldr	r0, [r4, #1768]
	add	r3, r3, #52
	ubfx	r3, r3, #2, #14
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
.L2552:
	ldr	r10, .L2567+20
	mov	r8, #0
	movw	r9, #65535
	sub	r5, r5, #4
	mov	r0, #0
	mov	fp, r10
	bl	FtlUpdateVaildLpn
.L2553:
	ldr	r3, [r4, #-524]
	ldrh	r1, [r5, #2]
	ldrh	r2, [r5]
	str	r3, [r4, #1768]
	ldr	r3, [r4, #-500]
	str	r3, [r4, #1772]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #1764]
	ldrh	r3, [r10]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L2554
	mov	r3, #0
	ldrh	r9, [r5, #4]
	strh	r3, [r5, #2]	@ movhi
	strh	r2, [r5, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #-3332]
	strh	r0, [r5]	@ movhi
	add	r2, r3, #1
	str	r3, [r4, #-3292]
	str	r2, [r4, #-3332]
	lsl	r2, r0, #10
	str	r2, [r4, #1764]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L2554:
	ldrb	r3, [r6, #36]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2555
	ldr	r3, [sp, #4]
	ldr	r0, [r4, #-524]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r7, #12]
.L2555:
	mov	r3, #1
	ldr	r0, .L2567+24
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	ldr	r2, [r4, #1760]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r5, #2]	@ movhi
	bne	.L2556
	cmp	r3, #1
	add	r8, r8, #1
	ldrheq	r3, [fp]
	uxth	r8, r8
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
	cmp	r8, #3
	bls	.L2553
	mov	r2, r8
	ldr	r1, [r4, #1764]
	ldr	r0, .L2567+28
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-3612]
.L2551:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2556:
	cmp	r3, #1
	cmpne	r2, #256
	beq	.L2553
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2551
	mov	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	b	.L2551
.L2568:
	.align	2
.L2567:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-3932
	.word	1179929683
	.word	1342177379
	.word	.LANCHOR0+2392
	.word	.LANCHOR2+1760
	.word	.LC132
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r5, .L2585
	ldr	r10, [r5, #-3612]
	cmp	r10, #0
	bne	.L2569
	ldr	r8, .L2585+4
	ldrb	r3, [r8, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2580
	ldrb	r3, [r0, #8]	@ zero_extendqisi2
	cmp	r3, #1
	ldrheq	r7, [r0, #4]
	moveq	r10, r3
	beq	.L2571
.L2580:
	mov	r7, #12
.L2571:
	mvn	r3, #0
	ldr	r6, [r5, #-500]
	str	r3, [sp, #20]
	mov	r9, #0
	ldr	r3, [r5, #-524]
	movw	r2, #61589
	str	r6, [sp, #16]
	mov	r4, r0
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r6, #8]
	mvn	r3, #1
	str	r3, [r6, #12]
	ldrh	r3, [r0]
	strh	r9, [r6]	@ movhi
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r5, #-524]
	str	r2, [r3]
	ldr	r2, .L2585+8
	ldr	r3, [r5, #-524]
	str	r2, [r3, #4]
.L2572:
	subs	r7, r7, #1
	bcc	.L2575
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2573
.L2575:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-3540]
	ldrh	r0, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #2390
	ldrh	r3, [r8, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
.L2569:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2573:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L2575
	ldr	r3, [r5, #-3328]
	mov	r2, r10
	mov	r1, #1
	add	r0, sp, #4
	str	r3, [r6, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r9
	str	r3, [r5, #-3328]
	mov	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2572
.L2586:
	.align	2
.L2585:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	305419896
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	ftl_map_blk_gc
	.syntax unified
	.arm
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
	ldr	fp, .L2600
	sub	r3, r3, #4
	cmp	r2, r3
	blt	.L2588
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r9, [r5, r0]
	cmp	r9, #0
	beq	.L2588
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2588
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #2392
	ldrh	r3, [fp, r3]
	cmp	r2, r3
	bcc	.L2589
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2589:
	ldr	r5, .L2600+4
	mov	r6, #0
.L2590:
	ldrh	r2, [r4, #6]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L2595
	mov	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2588:
	movw	r3, #2392
	ldrh	r2, [r4, #2]
	ldrh	r3, [fp, r3]
	cmp	r2, r3
	bcc	.L2593
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2593
.L2595:
	uxth	r7, r6
	add	r2, r10, r7, lsl #2
	str	r2, [sp]
	ldr	r2, [r10, r7, lsl #2]
	cmp	r9, r2, lsr #10
	bne	.L2591
	ldr	r2, [r5, #-520]
	ldr	r8, [r5, #-500]
	ldr	r0, .L2600+8
	str	r2, [r5, #1768]
	str	r8, [r5, #1772]
	ldr	r2, [r10, r7, lsl #2]
	str	r3, [sp, #4]
	str	r2, [r5, #1764]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #1760]
	ldr	r3, [sp, #4]
	cmn	r2, #1
	bne	.L2592
.L2594:
	ldr	r2, [sp]
	mov	r3, #0
	ldr	r0, .L2600+12
	str	r3, [r2]
	ldrh	r2, [r8, #8]
	ldr	r1, [r5, #1764]
	bl	printk
	mov	r3, #1
	str	r3, [r5, #-3612]
.L2593:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2592:
	ldrh	r2, [r8, #8]
	cmp	r2, r3
	bne	.L2594
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2594
	ldr	r2, [r5, #1768]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L2591:
	add	r6, r6, #1
	b	.L2590
.L2601:
	.align	2
.L2600:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+1760
	.word	.LC133
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r5, .L2613
	ldr	r6, [r5, #-3612]
	cmp	r6, #0
	bne	.L2603
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	ldr	r7, [r0, #12]
	cmp	r3, r2
	bne	.L2604
	ldrh	r3, [r0, #8]
	add	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r7]	@ movhi
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r6, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
.L2603:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2604:
	lsl	r3, r3, #1
	ldr	r8, [r0, #24]
	ldr	r10, .L2613+4
	mov	r1, #255
	ldrh	r9, [r7, r3]
	ldrh	r3, [r0, #2]
	ldr	r7, [r5, #-500]
	orr	r3, r3, r9, lsl #10
	str	r7, [r5, #1772]
	str	r3, [r5, #1764]
	ldr	r3, [r5, #-524]
	str	r3, [r5, #1768]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	ldr	r3, .L2613+8
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r9, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	movw	r3, #2392
	ldrh	r2, [r10, r3]
	ldr	r0, [r5, #-524]
	lsl	r2, r2, #3
	bl	ftl_memset
	mov	r2, r6
	mov	r3, r6
.L2605:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L2607
	ldrb	r3, [r10, #36]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2608
	ldr	r3, .L2613+12
	ldr	r0, [r5, #1768]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r7, #12]
.L2608:
	mov	r2, #1
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L2613+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L2603
.L2607:
	uxth	r1, r2
	ldr	r0, [r8, r1, lsl #2]
	cmp	r9, r0, lsr #10
	bne	.L2606
	ldr	r0, [r5, #-524]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r8, r1, lsl #2]
	ldr	r1, [r5, #-524]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L2606:
	add	r2, r2, #1
	b	.L2605
.L2614:
	.align	2
.L2613:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR0+2400
	.word	.LANCHOR2+1760
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.syntax unified
	.arm
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
	ldr	r7, .L2634
	mov	r9, r1
	mov	fp, r2
	mov	r6, #0
	ldr	r10, .L2634+4
	mov	r5, r7
.L2616:
	ldr	r3, [r7, #-3348]
	add	r3, r3, #1
	str	r3, [r7, #-3348]
	ldrh	r3, [r10]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2617
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2618
.L2617:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2618:
	ldr	r1, [r5, #-3612]
	cmp	r1, #0
	bne	.L2619
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #-500]
	lsl	r3, r3, #1
	ldrh	r8, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	fp, [r5, #1768]
	str	r0, [r5, #1772]
	orr	r3, r3, r8, lsl #10
	str	r3, [r5, #1764]
	bl	ftl_memset
	ldr	r3, [r5, #1772]
	ldr	r2, [r4, #28]
	strh	r9, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	str	r3, [sp, #4]
	strh	r8, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r2, .L2634+8
	ldrb	r1, [r2, #36]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2620
	add	r2, r2, #2400
	ldr	r0, [r5, #1768]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2620:
	mov	r3, #1
	ldr	r0, .L2634+12
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #1760]
	cmn	r2, #1
	bne	.L2621
	ldr	r1, [r5, #1764]
	add	r6, r6, #1
	ldr	r0, .L2634+16
	uxth	r6, r6
	bl	printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [r10]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L2616
	mov	r2, r6
	ldr	r1, [r5, #1764]
	ldr	r0, .L2634+20
	bl	printk
	mov	r3, #1
	str	r3, [r5, #-3612]
.L2619:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2621:
	cmp	r2, #0
	strhne	r8, [r4, #40]	@ movhi
	cmp	r3, #1
	cmpne	r2, #256
	beq	.L2625
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L2626
.L2625:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L2616
.L2626:
	ldr	r2, [r5, #1764]
	ldr	r3, [r4, #24]
	str	r2, [r3, r9, lsl #2]
	b	.L2619
.L2635:
	.align	2
.L2634:
	.word	.LANCHOR2
	.word	.LANCHOR0+2392
	.word	.LANCHOR0
	.word	.LANCHOR2+1760
	.word	.LC134
	.word	.LC135
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	flush_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	flush_l2p_region, %function
flush_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #12
	ldr	r5, .L2638
	mul	r4, r4, r0
	ldr	r3, [r5, #-3376]
	sub	r0, r5, #432
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-3376]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r4, r5, r6, pc}
.L2639:
	.align	2
.L2638:
	.word	.LANCHOR2
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	FtlMapTblRecovery
	.syntax unified
	.arm
	.fpu softvfp
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
	mov	r1, #0
	mov	r7, #0
	ldr	r5, .L2682
	str	r3, [sp]
	ldr	r3, [r0, #16]
	ldr	r8, [r0, #12]
	ldr	fp, .L2682+4
	str	r3, [sp, #12]
	ldrh	r3, [r0, #6]
	str	r3, [sp, #4]
	ldrh	r3, [r0, #8]
	ldr	r0, [sp]
	str	r3, [sp, #8]
	ldr	r3, [sp, #4]
	lsl	r2, r3, #2
	bl	ftl_memset
	ldr	r3, [r5, #-524]
	ldr	r6, [r5, #-500]
	str	r7, [r4, #32]
	str	r3, [r5, #1768]
	mvn	r3, #0
	str	r6, [r5, #1772]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r7, [r4, #28]
	str	r3, [r4, #36]
.L2641:
	ldr	r3, [sp, #8]
	sxth	r10, r7
	cmp	r10, r3
	bge	.L2660
	ldr	r3, [sp, #8]
	sub	r3, r3, #1
	cmp	r10, r3
	lsl	r3, r10, #1
	bne	.L2642
	ldrh	r0, [r8, r3]
	mov	r1, #1
	add	r9, r8, r3
	mov	r8, #0
	bl	FtlGetLastWrittenPage
	sxth	r3, r0
	strh	r7, [r4]	@ movhi
	add	r0, r0, #1
	ldr	r7, .L2682+8
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	strh	r0, [r4, #2]	@ movhi
	add	fp, r7, #2400
	ldr	r3, [r3, r10, lsl #2]
	ldr	r10, .L2682+12
	str	r3, [r4, #28]
.L2643:
	ldr	r3, [sp, #8]
	sxth	r2, r8
	add	r1, r3, #1
	cmp	r2, r1
	blt	.L2646
.L2660:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L2682+8
	movw	r3, #2392
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L2648
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2648:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2646:
	ldrh	r1, [r9]
	mov	r0, r10
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #1764]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrb	r2, [r7, #36]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2644
	ldr	r2, [r5, #1772]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	str	r2, [sp, #12]
	beq	.L2644
	ldrh	r1, [fp]
	ldr	r0, [r5, #1768]
	bl	js_hash
	ldr	r2, [sp, #12]
	cmp	r2, r0
	mvnne	r2, #0
	strne	r2, [r5, #1760]
.L2644:
	ldr	r1, .L2682
	ldr	r2, [r1, #1760]
	cmn	r2, #1
	beq	.L2645
	ldrh	r2, [r6, #8]
	ldr	r3, [sp, #4]
	cmp	r3, r2
	bls	.L2645
	ldrh	ip, [r6]
	ldrh	r0, [r4, #4]
	cmp	ip, r0
	ldreq	r1, [r1, #1764]
	ldreq	r3, [sp]
	streq	r1, [r3, r2, lsl #2]
.L2645:
	add	r8, r8, #1
	b	.L2643
.L2642:
	ldr	r2, [r5, #-524]
	ldr	r0, .L2682+12
	str	r2, [r5, #1768]
	add	r2, r8, r3
	str	r2, [sp, #16]
	ldrh	r2, [r8, r3]
	ldrh	r3, [fp]
	sub	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r5, #1764]
	bl	FlashReadPages
	ldr	r3, [r5, #1760]
	cmn	r3, #1
	beq	.L2662
	ldrh	r2, [r6]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2662
	ldrh	r2, [r6, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L2650
.L2662:
	ldr	r9, .L2682
	mov	r10, #0
.L2651:
	ldrh	r2, [fp]
	sxth	r3, r10
	cmp	r3, r2
	bge	.L2658
	ldr	r2, [sp, #16]
	ldr	r0, .L2682+12
	ldrh	r2, [r2]
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r9, #1764]
	bl	FlashReadPages
	ldr	r3, .L2682+8
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2655
	ldr	r3, [r9, #1772]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	str	r3, [sp, #20]
	beq	.L2655
	ldr	r2, .L2682+16
	ldr	r0, [r9, #1768]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #20]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [r9, #1760]
.L2655:
	ldr	r3, [r9, #1760]
	cmn	r3, #1
	beq	.L2656
	ldrh	r3, [r6, #8]
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bls	.L2656
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r9, #1764]
	ldreq	r1, [sp]
	streq	r2, [r1, r3, lsl #2]
.L2656:
	add	r10, r10, #1
	b	.L2651
.L2650:
	mov	r1, #0
	mov	ip, #4
.L2652:
	ldrh	r2, [fp]
	sxth	r3, r1
	sub	r2, r2, #1
	cmp	r3, r2
	blt	.L2654
.L2658:
	add	r7, r7, #1
	b	.L2641
.L2654:
	ldr	r0, [r5, #-524]
	add	r1, r1, #1
	ldr	r9, [sp, #4]
	ldr	r2, [r0, r3, lsl #3]
	uxth	lr, r2
	cmp	r9, lr
	addhi	r3, ip, r3, lsl #3
	movhi	r2, lr
	ldrhi	r3, [r0, r3]
	ldrhi	r0, [sp]
	strhi	r3, [r0, r2, lsl #2]
	b	.L2652
.L2683:
	.align	2
.L2682:
	.word	.LANCHOR2
	.word	.LANCHOR0+2392
	.word	.LANCHOR0
	.word	.LANCHOR2+1760
	.word	.LANCHOR0+2400
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2686
	movw	r1, #2412
	ldr	r3, .L2686+4
	push	{r4, lr}
	.save {r4, lr}
	ldrh	r1, [r2, r1]
	add	r0, r3, #1792
	add	r0, r0, #8
	strh	r1, [r0, #10]	@ movhi
	ldr	r1, .L2686+8
	strh	r1, [r0, #4]	@ movhi
	movw	r1, #2438
	ldrh	r1, [r2, r1]
	strh	r1, [r0, #8]	@ movhi
	movw	r1, #2414
	ldrh	r1, [r2, r1]
	ldr	r2, [r2, #2440]
	strh	r1, [r0, #6]	@ movhi
	str	r2, [r3, #1812]
	ldr	r2, [r3, #-460]
	str	r2, [r3, #1816]
	ldr	r2, [r3, #-464]
	str	r2, [r3, #1820]
	ldr	r2, [r3, #-456]
	str	r2, [r3, #1824]
	bl	FtlMapTblRecovery
	mov	r0, #0
	pop	{r4, pc}
.L2687:
	.align	2
.L2686:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-3962
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlLoadMapInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	FtlL2PDataInit
	ldr	r0, .L2690
	bl	FtlMapTblRecovery
	mov	r0, #0
	pop	{r4, pc}
.L2691:
	.align	2
.L2690:
	.word	.LANCHOR2-432
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlVendorPartWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2702
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	movw	r2, #2386
	mov	r5, r1
	add	r1, r0, r1
	.pad #60
	sub	sp, sp, #60
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	mvnhi	r9, #0
	bhi	.L2692
	movw	r2, #2398
	ldr	r8, .L2702+4
	ldrh	r6, [r3, r2]
	mov	r7, r0
	mov	r9, #0
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L2694:
	cmp	r5, #0
	bne	.L2699
.L2692:
	mov	r0, r9
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2699:
	ldr	r3, [r8, #-456]
	mov	r0, r7
	ldr	r2, [r3, fp]
	ldr	r3, .L2702+8
	str	r2, [sp, #12]
	ldrh	r3, [r3]
	mov	r1, r3
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	str	r1, [sp, #4]
	sub	r4, r3, r1
	uxth	r4, r4
	cmp	r5, r4
	uxthcc	r4, r5
	cmp	r2, #0
	cmpne	r4, r3
	movne	r1, #1
	moveq	r1, #0
	beq	.L2696
	ldr	r3, [r8, #-516]
	add	r0, sp, #20
	str	r2, [sp, #24]
	mov	r2, #1
	mov	r1, r2
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
.L2697:
	lsl	r3, r4, #9
	ldr	r0, [r8, #-516]
	mov	r1, r10
	mov	r2, r3
	str	r3, [sp, #8]
	ldr	r3, [sp, #4]
	sub	r5, r5, r4
	add	r7, r7, r4
	add	fp, fp, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r2, [r8, #-516]
	ldr	r0, .L2702+12
	add	r6, r6, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	cmn	r0, #1
	mvneq	r9, #0
	add	r10, r10, r3
	b	.L2694
.L2696:
	ldr	r3, .L2702+16
	ldr	r0, [r8, #-516]
	ldrh	r2, [r3]
	bl	ftl_memset
	b	.L2697
.L2703:
	.align	2
.L2702:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2396
	.word	.LANCHOR2+1800
	.word	.LANCHOR0+2400
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L2706
	ldr	r2, .L2706+4
	ldr	r1, [r3, #-3236]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L2706+8
	mov	r1, #1
	mov	r0, #0
	str	r2, [r3, #-3232]
	ldr	r2, [r3, #-3340]
	str	r2, [r3, #-3148]
	ldr	r2, [r3, #-3336]
	str	r2, [r3, #-3144]
	ldr	r2, [r3, #-3344]
	str	r2, [r3, #-3228]
	ldr	r2, [r3, #-3356]
	str	r2, [r3, #-3224]
	ldr	r2, [r3, #-3364]
	str	r2, [r3, #-3220]
	ldr	r2, [r3, #-3348]
	str	r2, [r3, #-3216]
	ldr	r2, [r3, #-3320]
	str	r2, [r3, #-3208]
	ldr	r2, [r3, #-3600]
	str	r2, [r3, #-3204]
	ldr	r2, [r3, #-3360]
	str	r2, [r3, #-3200]
	ldr	r2, [r3, #-3352]
	str	r2, [r3, #-3196]
	ldr	r2, [r3, #-3312]
	str	r2, [r3, #-3192]
	ldr	r2, [r3, #-3308]
	str	r2, [r3, #-3188]
	ldr	r2, [r3, #-2724]
	str	r2, [r3, #-3176]
	ldr	r2, [r3, #-564]
	str	r2, [r3, #-3172]
	ldr	r2, .L2706+12
	b	FtlVendorPartWrite
.L2707:
	.align	2
.L2706:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177379
	.word	.LANCHOR2-3236
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2718
	ldr	r3, [r3, #2248]
	cmp	r3, #0
	ldr	r3, .L2718+4
	moveq	r2, #32
	beq	.L2709
	ldr	r2, [r3, #-3308]
	cmp	r2, #39
	movhi	r2, #32
	movls	r2, #4
.L2709:
	movw	ip, #1844
	ldrh	r1, [r3, ip]
	cmp	r1, #31
	addls	r1, r1, #1
	movls	r2, #1
	strhls	r1, [r3, ip]	@ movhi
	cmp	r0, #0
	bne	.L2711
	ldr	r1, [r3, #-480]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2716
.L2711:
	push	{r4, lr}
	.save {r4, lr}
	mov	r0, #64
	ldr	r2, [r3, #-480]
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2718+8
	str	r1, [r2]
	ldr	r2, [r3, #-480]
	ldr	r3, .L2718+12
	ldrh	r1, [r3]
	lsl	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	mov	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
	mov	r0, #0
	pop	{r4, pc}
.L2716:
	mov	r0, #0
	bx	lr
.L2719:
	.align	2
.L2718:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1112818501
	.word	.LANCHOR2-488
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2731
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	movw	r2, #2386
	mov	r6, r1
	add	r1, r0, r1
	.pad #60
	sub	sp, sp, #60
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	mvnhi	r9, #0
	bhi	.L2720
	movw	r2, #2398
	ldr	r8, .L2731+4
	ldrh	r5, [r3, r2]
	mov	r7, r0
	mov	r9, #0
	lsr	r5, r0, r5
	lsl	fp, r5, #2
.L2722:
	cmp	r6, #0
	bne	.L2728
.L2720:
	mov	r0, r9
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2728:
	ldr	r3, [r8, #-456]
	mov	r0, r7
	ldr	r3, [r3, fp]
	str	r3, [sp, #8]
	ldr	r3, .L2731+8
	ldrh	r4, [r3]
	mov	r1, r4
	bl	__aeabi_uidivmod
	sub	r4, r4, r1
	ldr	r3, [sp, #8]
	uxth	r4, r4
	str	r1, [sp, #4]
	cmp	r6, r4
	uxthcc	r4, r6
	cmp	r3, #0
	lsl	r2, r4, #9
	str	r2, [sp, #8]
	beq	.L2724
	ldr	r2, [r8, #-516]
	add	r0, sp, #20
	str	r3, [sp, #24]
	str	r3, [sp, #12]
	str	r2, [sp, #28]
	mov	r2, #0
	str	r2, [sp, #32]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #12]
	cmn	r2, #1
	ldr	r2, [r8, #1760]
	mvneq	r9, #0
	cmp	r2, #256
	bne	.L2726
	mov	r2, r3
	mov	r1, r5
	ldr	r0, .L2731+12
	bl	printk
	ldr	r2, [r8, #-516]
	mov	r1, r5
	ldr	r0, .L2731+16
	bl	FtlMapWritePage
.L2726:
	ldr	r1, [r8, #-516]
	lsl	r2, r4, #9
	ldr	r3, [sp, #4]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L2727:
	ldr	r3, [sp, #8]
	add	r5, r5, #1
	sub	r6, r6, r4
	add	r7, r7, r4
	add	fp, fp, #4
	add	r10, r10, r3
	b	.L2722
.L2724:
	lsl	r2, r4, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L2727
.L2732:
	.align	2
.L2731:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2396
	.word	.LC136
	.word	.LANCHOR2+1800
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlLoadEctTbl
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #64
	ldr	r4, .L2736
	sub	r5, r4, #488
	ldr	r2, [r4, #-480]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-480]
	ldr	r2, [r3]
	ldr	r3, .L2736+4
	cmp	r2, r3
	beq	.L2734
	ldr	r1, .L2736+8
	ldr	r0, .L2736+12
	bl	printk
	ldrh	r2, [r5]
	mov	r1, #0
	ldr	r0, [r4, #-480]
	lsl	r2, r2, #9
	bl	ftl_memset
.L2734:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L2737:
	.align	2
.L2736:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC137
	.word	.LC77
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #1
	ldr	r4, .L2744
	mov	r0, #0
	ldr	r5, .L2744+4
	sub	r6, r4, #3232
	sub	r6, r6, #4
	mov	r2, r6
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-3236]
	cmp	r3, r5
	beq	.L2739
	mov	r2, #512
	mov	r1, #0
	mov	r0, r6
	bl	ftl_memset
	str	r5, [r4, #-3236]
.L2739:
	ldr	r3, [r4, #-3236]
	cmp	r3, r5
	bne	.L2740
	ldr	r3, [r4, #-3148]
	str	r3, [r4, #-3340]
	ldr	r3, [r4, #-3144]
	str	r3, [r4, #-3336]
	ldr	r3, [r4, #-3228]
	str	r3, [r4, #-3344]
	ldr	r3, [r4, #-3224]
	str	r3, [r4, #-3356]
	ldr	r3, [r4, #-3220]
	str	r3, [r4, #-3364]
	ldr	r3, [r4, #-3216]
	str	r3, [r4, #-3348]
	ldr	r3, [r4, #-3208]
	str	r3, [r4, #-3320]
	ldr	r3, [r4, #-3204]
	str	r3, [r4, #-3600]
	ldr	r3, [r4, #-3200]
	str	r3, [r4, #-3360]
	ldr	r3, [r4, #-3196]
	str	r3, [r4, #-3352]
	ldr	r3, [r4, #-3192]
	str	r3, [r4, #-3312]
	ldr	r3, [r4, #-3188]
	str	r3, [r4, #-3308]
	ldr	r3, [r4, #-3176]
	str	r3, [r4, #-2724]
.L2740:
	ldr	r1, [r4, #-3168]
	mov	r3, #0
	ldr	r2, .L2744+8
	str	r3, [r4, #-564]
	ldr	r5, .L2744+12
	cmp	r1, r2
	bne	.L2741
	ldrb	r2, [r5, #152]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L2742
	str	r3, [r4, #-3168]
	bl	Ftl_save_ext_data
.L2741:
	movw	r3, #2382
	ldr	r0, [r4, #-3324]
	ldrh	r2, [r5, r3]
	movw	r1, #2332
	ldr	r3, [r4, #-3320]
	ldrh	r1, [r5, r1]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #-3316]
	pop	{r4, r5, r6, pc}
.L2742:
	mov	r3, #1
	ldr	r1, .L2744+16
	ldr	r0, .L2744+20
	str	r3, [r5, #2248]
	bl	printk
	b	.L2741
.L2745:
	.align	2
.L2744:
	.word	.LANCHOR2
	.word	1179929683
	.word	305432421
	.word	.LANCHOR0
	.word	.LC138
	.word	.LC77
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	FtlMapBlkWriteDumpData
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, [r0, #36]
	cmp	r3, #0
	bxeq	lr
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r2, #0
	ldr	r4, .L2756
	str	r2, [r0, #36]
	ldr	r2, [r4, #-3612]
	ldrh	r5, [r0, #6]
	ldr	r3, [r0, #24]
	cmp	r2, #0
	popne	{r4, r5, r6, pc}
	mov	r6, r0
	ldr	r2, [r4, #-500]
	ldr	r0, [r4, #-520]
	sub	r5, r5, #1
	uxth	r5, r5
	str	r2, [r4, #1772]
	str	r0, [r4, #1768]
	ldr	r3, [r3, r5, lsl #2]
	cmp	r3, #0
	str	r3, [r4, #1764]
	beq	.L2750
	mov	r2, #1
	add	r0, r4, #1760
	mov	r1, r2
	bl	FlashReadPages
.L2751:
	ldr	r2, [r4, #1768]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L2750:
	ldr	r3, .L2756+4
	mov	r1, #255
	ldrh	r2, [r3]
	bl	ftl_memset
	b	.L2751
.L2757:
	.align	2
.L2756:
	.word	.LANCHOR2
	.word	.LANCHOR0+2400
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FlashReadFacBbtData
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldr	r2, .L2771
	.pad #40
	sub	sp, sp, #40
	mov	r5, r0
	mov	r7, r1
	ldr	r9, .L2771+4
	ldrh	r3, [r2, #138]
	ldrh	r2, [r2, #136]
	smulbb	r3, r3, r2
	ldr	r2, [r9, #1696]
	uxth	r3, r3
	str	r2, [sp, #12]
	ldr	r2, [r9, #1728]
	sub	r6, r3, #1
	mul	r10, r1, r3
	uxth	r6, r6
	sub	r4, r3, #16
	str	r2, [sp, #16]
.L2759:
	cmp	r6, r4
	mvnle	r0, #0
	ble	.L2758
.L2765:
	add	r3, r6, r10
	mov	r2, #1
	lsl	r3, r3, #10
	mov	r1, r2
	add	r0, sp, #4
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2760
	ldr	r3, [r9, #1728]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2760
	cmp	r5, #0
	moveq	r0, r5
	beq	.L2758
	cmp	r7, #0
	moveq	r1, r7
	moveq	lr, #1
	beq	.L2763
.L2762:
	mov	r2, r8
	ldr	r1, [r9, #1696]
	mov	r0, r5
	bl	ftl_memcpy
	mov	r3, #4
	ldr	r0, .L2771+8
	mov	r2, r3
	mov	r1, r5
	bl	rknand_print_hex
	mov	r0, #0
.L2758:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2764:
	ldr	r0, [r9, #1696]
	lsr	ip, r3, #5
	and	r3, r3, #31
	ldr	r2, [r0, ip, lsl #2]
	orr	r3, r2, lr, lsl r3
	str	r3, [r0, ip, lsl #2]
.L2763:
	ldr	r0, [r9, #1716]
	uxth	r3, r1
	add	r1, r1, #1
	cmp	r3, r0
	bcc	.L2764
	b	.L2762
.L2760:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2759
.L2772:
	.align	2
.L2771:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC139
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2784
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r6, .L2784+4
	ldr	r3, [r3, #48]
	ldr	r0, [r6, #1724]
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	asr	r2, r2, #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L2774
.L2778:
	mov	r3, #0
.L2775:
	lsl	r3, r3, #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L2774:
	mov	r2, #0
	lsr	lr, r4, #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r7, #1
.L2776:
	uxth	r1, r2
	cmp	r1, r4
	bge	.L2775
	ldr	r8, [r6, #1724]
	lsr	ip, r1, #5
	and	r0, r1, #31
	add	r2, r2, #1
	ldr	ip, [r8, ip, lsl #2]
	ands	r0, ip, r7, lsl r0
	addne	r0, r3, #1
	lslne	r3, r3, #1
	strhne	r1, [r5, r3]	@ movhi
	uxthne	r3, r0
	cmp	r3, lr
	bcc	.L2776
	b	.L2778
.L2785:
	.align	2
.L2784:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	FtlMakeBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L2807
	ldr	r8, [r4, #-3612]
	cmp	r8, #0
	bne	.L2787
	ldr	r9, .L2807+4
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
	sub	r10, r9, #18
	sub	r5, r9, #28
.L2788:
	ldr	r3, .L2807+8
	ldr	r6, .L2807+12
	ldrh	r3, [r3]
	cmp	r8, r3
	bcc	.L2794
	ldr	r8, .L2807+16
	mov	r7, #0
.L2795:
	ldrh	r3, [r8]
	uxth	r0, r7
	add	r7, r7, #1
	cmp	r3, r0
	bhi	.L2796
	ldrh	r7, [r5, #12]
	movw	r8, #65535
	sub	r7, r7, #1
	uxth	r7, r7
.L2797:
	ldrh	r3, [r5, #12]
	sub	r3, r3, #48
	cmp	r7, r3
	ble	.L2801
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2798
	mov	r0, r7
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2799
	mov	r0, r7
	bl	FtlBbmMapBadBlock
.L2798:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L2797
.L2794:
	ldr	r3, [r4, #-500]
	movw	r2, #65535
	ldr	r0, [r4, #-524]
	ldr	fp, .L2807+20
	str	r3, [sp, #4]
	str	r3, [r4, #1772]
	ldrh	r3, [r10, #2]!
	str	r0, [r4, #1768]
	cmp	r3, r2
	beq	.L2789
	ldrh	r7, [fp]
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L2807+24
	mla	r7, r8, r7, r3
	lsl	r3, r7, #10
	str	r3, [r4, #1764]
	bl	FlashReadPages
	ldrh	r2, [fp]
	ldr	r1, [r4, #1768]
	ldr	r0, [r9]
	add	r2, r2, #7
	asr	r2, r2, #3
	bl	ftl_memcpy
.L2790:
	uxth	r0, r7
	add	r8, r8, #1
	add	r9, r9, #4
	bl	FtlBbmMapBadBlock
	b	.L2788
.L2789:
	mov	r1, r8
	bl	FlashGetBadBlockList
	ldr	r1, [r9]
	ldr	r0, [r4, #1768]
	bl	FtlBbt2Bitmap
	ldrh	r6, [fp]
.L2792:
	sub	r6, r6, #1
	uxth	r6, r6
.L2791:
	ldrh	r0, [fp]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2792
	mov	r2, #16
	mov	r1, #0
	strh	r6, [r10]	@ movhi
	ldr	r0, [r4, #-500]
	bl	ftl_memset
	ldr	r3, [sp, #4]
	movw	r2, 61664	@ movhi
	strh	r2, [r3]	@ movhi
	mov	r3, #0
	ldr	r2, [sp, #4]
	ldrh	r7, [fp]
	str	r3, [r2, #4]
	ldrh	r3, [r10]
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r10]
	ldr	r1, [r9]
	ldr	r0, [r4, #1768]
	mla	r7, r8, r7, r3
	lsl	r3, r7, #10
	str	r3, [r4, #1764]
	ldr	r3, .L2807+28
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L2807+24
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L2807+24
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	bne	.L2790
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L2791
.L2796:
	bl	FtlBbmMapBadBlock
	b	.L2795
.L2799:
	ldrh	r3, [r5]
	cmp	r3, r8
	strheq	r7, [r5]	@ movhi
	beq	.L2798
.L2800:
	strh	r7, [r5, #4]	@ movhi
.L2801:
	movw	r7, #2456
	ldr	r0, [r4, #-3608]
	ldrh	r3, [r6, r7]
	mov	r8, #0
	str	r8, [r6, #2464]
	mov	r2, #2
	mov	r1, #1
	strh	r8, [r5, #2]	@ movhi
	lsl	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r5, #4]
	lsl	r3, r3, #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r6, r7]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r5, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r6, #2464]
	ldrh	r2, [r5, #4]
	strh	r8, [r5, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r6, #2464]
	ldrh	r3, [r6, r7]
	strh	r2, [r6, r7]	@ movhi
	strh	r3, [r5, #4]	@ movhi
	bl	FtlBbmTblFlush
.L2787:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2808:
	.align	2
.L2807:
	.word	.LANCHOR2
	.word	.LANCHOR0+2484
	.word	.LANCHOR0+2346
	.word	.LANCHOR0
	.word	.LANCHOR0+2404
	.word	.LANCHOR0+2388
	.word	.LANCHOR2+1760
	.word	.LANCHOR2-436
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	log2phys
	.syntax unified
	.arm
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r8, .L2825
	ldr	r3, [r8, #2452]
	cmp	r0, r3
	bcs	.L2810
	movw	r3, #2398
	mov	r10, r0
	ldrh	r0, [r8, r3]
	mov	r5, #12
	ldr	r4, .L2825+4
	str	r1, [sp, #8]
	add	r3, r0, #7
	str	r2, [sp, #12]
	lsr	r6, r10, r3
	ldr	fp, [r4, #-3376]
	str	r3, [sp, #4]
	movw	r3, #2430
	uxth	r9, r6
	ldrh	r1, [r8, r3]
	mov	r3, #0
.L2811:
	uxth	r7, r3
	cmp	r7, r1
	bcc	.L2816
	bl	select_l2p_ram_region
	mul	r5, r5, r0
	movw	r2, #65535
	mov	r7, r0
	ldrh	r1, [fp, r5]
	add	r3, fp, r5
	cmp	r1, r2
	beq	.L2817
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2817
	bl	flush_l2p_region
.L2817:
	ldr	r3, [r4, #-452]
	uxth	r6, r6
	ldr	fp, [r3, r6, lsl #2]
	cmp	fp, #0
	bne	.L2818
	ldr	r0, [r4, #-3376]
	mov	r1, #255
	ldr	r2, .L2825+8
	add	r0, r0, r5
	ldrh	r2, [r2]
	ldr	r0, [r0, #8]
	bl	ftl_memset
	ldr	r2, [r4, #-3376]
	strh	r9, [r2, r5]	@ movhi
	ldr	r2, [r4, #-3376]
	add	r5, r2, r5
	str	fp, [r5, #4]
	b	.L2813
.L2810:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
.L2809:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2816:
	add	r3, r3, #1
	mla	r0, r5, r3, fp
	ldrh	r0, [r0, #-12]
	cmp	r0, r9
	bne	.L2811
.L2813:
	ldr	r3, [sp, #4]
	mvn	r0, #0
	ldr	r1, .L2825+4
	bic	r10, r10, r0, lsl r3
	ldr	r3, [sp, #12]
	uxth	r10, r10
	cmp	r3, #0
	mov	r3, #12
	bne	.L2814
	ldr	r2, [r1, #-3376]
	mla	r3, r3, r7, r2
	ldr	r2, [sp, #8]
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r10, lsl #2]
	str	r3, [r2]
.L2815:
	ldr	r2, [r1, #-3376]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2809
.L2814:
	ldr	r2, [sp, #8]
	mul	r3, r3, r7
	ldr	r0, [r2]
	ldr	r2, [r4, #-3376]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r0, [r2, r10, lsl #2]
	ldr	r2, [r4, #-3376]
	add	r3, r2, r3
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	sub	r3, r1, #3360
	strh	r9, [r3, #-12]	@ movhi
	b	.L2815
.L2818:
	ldr	r2, [r4, #-3376]
	ldr	r0, .L2825+12
	str	fp, [r4, #1764]
	add	r2, r2, r5
	ldr	r2, [r2, #8]
	str	r2, [r4, #1768]
	ldr	r2, [r4, #-500]
	str	r2, [r4, #1772]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r4, #1772]
	ldrh	r2, [r2, #8]
	cmp	r2, r9
	beq	.L2819
	mov	r2, fp
	mov	r1, r6
	ldr	r0, .L2825+16
	bl	printk
	mov	r3, #4
	ldr	r1, [r4, #1772]
	mov	r2, r3
	ldr	r0, .L2825+20
	bl	rknand_print_hex
	movw	r3, #2428
	mov	r2, #4
	ldrh	r3, [r8, r3]
	ldr	r1, [r4, #-452]
	ldr	r0, .L2825+24
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r4, #-3612]
.L2820:
	ldr	r3, .L2825+4
	mov	r1, #0
	ldr	r3, [r3, #-3376]
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r9, [r3, r5]	@ movhi
	b	.L2813
.L2819:
	ldr	r2, [r4, #1760]
	cmp	r2, #256
	bne	.L2820
	mov	r2, fp
	mov	r1, r6
	ldr	r0, .L2825+28
	bl	printk
	ldr	r3, [r4, #-3376]
	mov	r1, r6
	ldr	r0, .L2825+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L2820
.L2826:
	.align	2
.L2825:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2400
	.word	.LANCHOR2+1760
	.word	.LC140
	.word	.LC101
	.word	.LC141
	.word	.LC142
	.word	.LANCHOR2-432
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlWriteDumpData
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r4, .L2846
	ldr	r3, [r4, #-3612]
	cmp	r3, #0
	bne	.L2827
	sub	r6, r4, #3520
	ldrh	r2, [r6, #4]
	cmp	r2, #0
	beq	.L2829
	ldrb	r3, [r4, #-3512]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2829
	ldr	r8, .L2846+4
	movw	r3, #2390
	ldrb	r1, [r4, #-3513]	@ zero_extendqisi2
	ldrh	r3, [r8, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L2829
	ldrb	r10, [r4, #-3510]	@ zero_extendqisi2
	cmp	r10, #0
	bne	.L2827
	ldr	r7, [r8, #2452]
	movw	r3, #2324
	mov	r2, r10
	mov	r1, sp
	ldrh	r9, [r8, r3]
	sub	r7, r7, #1
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp]
	ldr	r5, [r4, #-500]
	ldr	r0, [r4, #-524]
	cmn	r3, #1
	str	r3, [sp, #8]
	str	r7, [sp, #20]
	str	r0, [sp, #12]
	str	r5, [sp, #16]
	str	r10, [r5, #4]
	beq	.L2831
	mov	r2, r10
	mov	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.L2832:
	ldr	r10, .L2846+8
	mov	r8, #0
	ldr	r3, .L2846+12
	lsl	r9, r9, #2
	mov	fp, r8
	strh	r3, [r5]	@ movhi
.L2833:
	cmp	r9, r8
	bne	.L2837
.L2834:
	mov	r3, #1
.L2845:
	strb	r3, [r4, #-3510]
.L2827:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2831:
	add	r8, r8, #2400
	mov	r1, #255
	ldrh	r2, [r8]
	bl	ftl_memset
	b	.L2832
.L2837:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	beq	.L2834
	ldr	r3, [sp, #8]
	mov	r0, r10
	str	r7, [r5, #8]
	add	r8, r8, #1
	str	r3, [r5, #12]
	ldrh	r3, [r6]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r4, #-3328]
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, fp
	str	r3, [r4, #-3328]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r6]
	bl	decrement_vpc_count
	b	.L2833
.L2829:
	mov	r3, #0
	b	.L2845
.L2847:
	.align	2
.L2846:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3520
	.word	-3947
	.fnend
	.size	FtlWriteDumpData, .-FtlWriteDumpData
	.align	2
	.global	l2p_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, #0
	ldr	r5, .L2853
	mov	r7, #12
	ldr	r6, .L2853+4
	bl	FtlWriteDumpData
.L2849:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L2851
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2851:
	ldr	r2, [r6, #-3376]
	uxth	r3, r4
	mla	r3, r7, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2850
	bl	flush_l2p_region
.L2850:
	add	r4, r4, #1
	b	.L2849
.L2854:
	.align	2
.L2853:
	.word	.LANCHOR0+2430
	.word	.LANCHOR2
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	allocate_new_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r4, .L2882
	ldr	r3, [r4, #-3612]
	cmp	r3, #0
	bne	.L2856
	ldrh	r5, [r0]
	movw	r3, #65535
	mov	r7, r0
	cmp	r5, r3
	beq	.L2857
	ldr	r2, [r4, #-3540]
	lsl	r3, r5, #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2858
	bl	INSERT_DATA_LIST
.L2857:
	ldr	r2, .L2882+4
	mov	r3, #0
	strb	r3, [r7, #8]
	cmp	r7, r2
	beq	.L2859
	ldr	r3, .L2882+8
	movw	r1, #2344
	ldrh	r1, [r3, r1]
	cmp	r1, #1
	beq	.L2859
	ldrb	r0, [r3, #152]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L2860
.L2859:
	mov	r3, #1
	strb	r3, [r7, #8]
.L2861:
	ldr	r3, .L2882+12
	movw	r2, #65535
	ldrh	r0, [r3]
	mov	r6, r3
	cmp	r0, r2
	beq	.L2866
	cmp	r5, r0
	bne	.L2867
	ldr	r2, [r4, #-3540]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2868
.L2867:
	bl	update_vpc_list
.L2868:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L2866:
	mov	r0, r7
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2856:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2858:
	bl	INSERT_FREE_LIST
	b	.L2857
.L2860:
	sub	r2, r2, #48
	cmp	r7, r2
	bne	.L2861
	cmp	r1, #3
	beq	.L2863
	ldr	r2, [r4, #-2724]
	cmp	r2, #1
	bne	.L2864
.L2863:
	mov	r2, #1
	strb	r2, [r4, #-3512]
.L2864:
	ldr	r3, [r3, #2248]
	cmp	r3, #0
	beq	.L2861
	ldr	r3, [r4, #-3308]
	cmp	r3, #39
	movls	r3, #1
	strbls	r3, [r4, #-3512]
	b	.L2861
.L2883:
	.align	2
.L2882:
	.word	.LANCHOR2
	.word	.LANCHOR2-3472
	.word	.LANCHOR0
	.word	.LANCHOR2-380
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlCheckVpc
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, #0
	ldr	r6, .L2905
	ldr	r5, .L2905+4
	ldr	r1, .L2905+8
	ldr	r0, .L2905+12
	bl	printk
	mov	r2, #8192
	mov	r1, #0
	ldr	r0, .L2905+4
	bl	memset
.L2885:
	ldr	r3, [r6, #2452]
	cmp	r4, r3
	bcc	.L2887
	ldr	r9, .L2905+16
	mov	r4, #0
	ldr	r5, .L2905+20
	mov	r6, r4
	ldr	r8, .L2905+4
.L2888:
	ldrh	r2, [r9]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L2890
	ldr	r4, [r5, #-3528]
	cmp	r4, #0
	beq	.L2891
	ldr	r3, .L2905+24
	mov	r7, #0
	ldr	r9, .L2905+4
	mov	fp, #6
	ldr	r10, .L2905+28
	ldrh	r8, [r3, #-4]
	ldr	r3, [r5, #-3548]
	sub	r4, r4, r3
	ldr	r3, .L2905+32
	asr	r4, r4, #1
	mul	r4, r3, r4
	uxth	r4, r4
.L2892:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L2891
	ldr	r2, [r5, #-3540]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2893
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	printk
.L2893:
	mul	r4, fp, r4
	ldr	r3, [r5, #-3548]
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2892
.L2891:
	mov	r1, r6
	ldr	r0, .L2905+36
	bl	printk
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2887:
	mov	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2886
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	lsl	r0, r0, #1
	ldrh	r3, [r5, r0]
	add	r3, r3, #1
	strh	r3, [r5, r0]	@ movhi
.L2886:
	add	r4, r4, #1
	b	.L2885
.L2890:
	uxth	r1, r4
	ldr	r3, [r5, #-3540]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L2889
	ldr	r0, .L2905+40
	bl	printk
	ldr	r3, [r5, #-3540]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L2889
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L2889:
	add	r4, r4, #1
	b	.L2888
.L2906:
	.align	2
.L2905:
	.word	.LANCHOR0
	.word	check_valid_page_count_table
	.word	.LANCHOR3+191
	.word	.LC110
	.word	.LANCHOR0+2332
	.word	.LANCHOR2
	.word	.LANCHOR2-3520
	.word	.LC144
	.word	-1431655765
	.word	.LC145
	.word	.LC143
	.fnend
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	2
	.global	Ftlscanalldata
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, #0
	ldr	r4, .L2916
	.pad #32
	sub	sp, sp, #32
	mov	r1, #0
	ldr	r7, .L2916+4
	add	r8, r4, #1760
	ldr	r0, .L2916+8
	bl	printk
.L2908:
	ldr	r3, [r7, #2452]
	cmp	r5, r3
	bcc	.L2914
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2914:
	mov	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L2909
	ldr	r2, [sp, #28]
	mov	r1, r5
	ldr	r0, .L2916+12
	bl	printk
.L2909:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L2911
	str	r3, [r4, #1764]
	mov	r2, #0
	ldr	r3, [r4, #-524]
	mov	r1, #1
	ldr	r6, [r4, #-500]
	mov	r0, r8
	str	r5, [r4, #1776]
	str	r3, [r4, #1768]
	str	r6, [r4, #1772]
	str	r2, [r4, #1760]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2912
	ldr	r3, [r6, #8]
	cmp	r5, r3
	beq	.L2911
.L2912:
	ldr	r2, [r4, #1768]
	ldr	r3, [r4, #1772]
	ldr	r0, .L2916+16
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
	ldr	r2, [r4, #1764]
	ldr	r3, [r3]
	bl	printk
.L2911:
	add	r5, r5, #1
	b	.L2908
.L2917:
	.align	2
.L2916:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC146
	.word	.LC147
	.word	.LC148
	.fnend
	.size	Ftlscanalldata, .-Ftlscanalldata
	.align	2
	.global	FtlReUsePrevPpa
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r5, r0
	ldr	r6, .L2928
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-3540]
	lsl	r7, r0, #1
	ldrh	r3, [r2, r7]
	cmp	r3, #0
	bne	.L2919
	ldr	r4, [r6, #-3528]
	cmp	r4, #0
	beq	.L2920
	ldr	r2, [r6, #-3548]
	sub	r8, r6, #3520
	ldr	ip, .L2928+4
	mov	lr, #6
	ldrh	r1, [r8, #-4]
	movw	r9, #65535
	sub	r4, r4, r2
	asr	r4, r4, #1
	mul	r4, ip, r4
	uxth	r4, r4
.L2921:
	uxth	ip, r3
	cmp	r1, ip
	bls	.L2920
	cmp	r4, r0
	bne	.L2922
	mov	r1, r4
	sub	r0, r8, #8
	bl	List_remove_node
	ldrh	r3, [r8, #-4]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r8, #-4]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r6, #-3540]
	ldrh	r3, [r2, r7]
.L2919:
	add	r3, r3, #1
	strh	r3, [r2, r7]	@ movhi
	b	.L2920
.L2922:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
	cmp	r4, r9
	bne	.L2921
.L2920:
	mov	r2, #1
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2929:
	.align	2
.L2928:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	FtlRecoverySuperblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldrh	r2, [r0]
	movw	r1, #65535
	cmp	r2, r1
	beq	.L3075
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r2, #2390
	ldr	r6, .L3087
	.pad #52
	sub	sp, sp, #52
	mov	r10, r0
	ldrh	r3, [r0, #2]
	ldrh	r2, [r6, r2]
	str	r3, [sp, #8]
	cmp	r2, r3
	mov	r2, #0
	strheq	r2, [r0, #4]	@ movhi
	strbeq	r2, [r0, #6]
	ldrhne	r0, [r0, #16]
	bne	.L2934
.L3073:
	mov	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2935:
	uxth	r0, r2
	add	r0, r10, r0, lsl #1
	ldrh	r0, [r0, #16]
.L2934:
	cmp	r0, r1
	add	r2, r2, #1
	beq	.L2935
	ldrb	r1, [r10, #8]	@ zero_extendqisi2
	ldrb	r3, [r10, #6]	@ zero_extendqisi2
	cmp	r1, #1
	str	r3, [sp, #12]
	bne	.L2936
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2937
	ldrb	r3, [r6, #152]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r3, r6, r0, lsl #1
	ldrheq	r5, [r3, #156]
	beq	.L2938
.L3008:
	mov	r5, r4
.L2938:
	movw	r3, #2324
	mov	r2, #0
	ldrh	r3, [r6, r3]
	movw	r8, #65535
	mov	r9, #36
	str	r3, [sp]
	ldr	r3, .L3087+4
	ldr	r0, [r3, #-536]
	ldr	lr, [r3, #-2692]
	movw	r3, #2402
	ldrh	r7, [r6, r3]
	add	r3, r10, #16
	mov	ip, r3
	mov	r6, r2
	str	r3, [sp, #20]
.L2939:
	ldr	r1, [sp]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L2941
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L3078
	ldr	r3, .L3087
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L3078:
	ldr	r7, .L3087+4
	mov	r1, r6
	str	r3, [sp, #24]
	mov	r8, #0
	ldr	r2, [sp, #24]
	movw	r9, #65535
	bl	FlashReadPages
	ldr	r3, [r7, #-3328]
	mov	fp, r7
	str	r3, [sp, #16]
.L2943:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L2948
	bne	.L2946
	add	r4, r4, #1
	uxth	r3, r4
	str	r3, [sp]
	ldr	r3, [r7, #-536]
	ldr	r0, [r3, #4]
.L3079:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r2, [r10, #8]	@ zero_extendqisi2
	str	r0, [sp, #4]
	ldr	r3, .L3087
	cmp	r2, #1
	bne	.L2950
	ldrb	r1, [r3, #152]	@ zero_extendqisi2
	cmp	r1, #0
	ldreq	r1, [sp]
	addeq	r4, r3, r1, lsl #1
	ldrheq	r1, [r4, #156]
	streq	r1, [sp]
.L2950:
	movw	r1, #2390
	ldrh	r3, [r3, r1]
	ldr	r1, [sp]
	cmp	r3, r1
	ldmib	sp, {r0, r1}
	ldrheq	r3, [sp]
	strheq	r3, [r10, #2]	@ movhi
	moveq	r3, #0
	strbeq	r3, [r10, #6]
	strheq	r3, [r10, #4]	@ movhi
	ldrh	r3, [sp, #12]
	str	r3, [sp, #28]
	ldr	ip, [sp, #28]
	ldr	r3, [sp]
	cmp	r3, r1
	cmpeq	r0, ip
	moveq	r2, r0
	moveq	r1, r3
	beq	.L3085
	ldr	r3, [sp, #16]
	sub	fp, r3, #1
	movw	r3, #65535
	subs	r9, r9, r3
	movne	r9, #1
	cmp	r2, #0
	orreq	r9, r9, #1
	cmp	r9, #0
	beq	.L2954
	ldr	r3, [r7, #-368]
	uxth	r8, r5
	uxth	r5, r5
	cmn	r3, #1
	streq	fp, [r7, #-368]
	ldr	r3, [r7, #-368]
	mvn	r7, #0
	mov	r6, r7
	str	r3, [sp, #12]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	r5, r3
	subgt	r4, r8, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L2957:
	cmp	r4, r8
	ldr	r3, .L3087+4
	bhi	.L2970
	ldr	r2, .L3087+8
	mov	ip, #36
	ldr	r0, [r3, #-536]
	mov	r3, #0
	ldr	r1, [sp, #20]
	mov	r5, r3
	ldrh	r9, [r2]
	b	.L2971
.L2936:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	bne	.L3008
.L2937:
	mov	r3, #0
	strh	r3, [r10, #2]	@ movhi
.L3084:
	strb	r3, [r10, #6]
	b	.L3073
.L2941:
	ldrh	r3, [ip], #2
	cmp	r3, r8
	beq	.L2940
	mla	r1, r9, r6, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	mov	r3, #0
	str	r3, [r1, #8]
	mul	r3, r7, r6
	add	r6, r6, #1
	uxth	r6, r6
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.L2940:
	add	r2, r2, #1
	b	.L2939
.L2948:
	mov	r3, #36
	ldr	r1, [fp, #-536]
	mul	r3, r3, r8
	add	r2, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2944
	ldr	r2, [r2, #12]
	ldr	r3, [r2, #4]
	cmn	r3, #1
	beq	.L2945
	ldr	r1, [fp, #-3328]
	mov	r0, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r3, #1
	strne	r3, [fp, #-3328]
.L2945:
	ldr	r3, [r2]
	cmn	r3, #1
	bne	.L2947
.L2946:
	uxth	r3, r4
	uxth	r8, r8
	str	r3, [sp]
	mov	r2, #36
	ldr	r3, [r7, #-536]
	mla	r8, r2, r8, r3
	ldr	r0, [r8, #4]
	b	.L3079
.L2944:
	ldr	r1, [r2, #4]
	uxth	r9, r5
	ldr	r0, .L3087+12
	bl	printk
	ldrh	r3, [r10]
	ldr	r2, .L3087+16
	strh	r3, [r2]	@ movhi
.L2947:
	add	r8, r8, #1
	b	.L2943
.L2959:
	ldrh	r2, [r1], #2
	movw	lr, #65535
	add	r3, r3, #1
	cmp	r2, lr
	mlane	lr, ip, r5, r0
	addne	r5, r5, #1
	orrne	r2, r4, r2, lsl #10
	uxthne	r5, r5
	strne	r2, [lr, #4]
.L2971:
	uxth	r2, r3
	cmp	r2, r9
	bcc	.L2959
	mov	r1, r5
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	ldr	r3, .L3087
	mov	r1, #36
	ldr	r2, .L3087+4
	ldr	r0, .L3087+20
	ldrb	ip, [r3, #152]	@ zero_extendqisi2
	ldr	r3, [r2, #-536]
	add	lr, r0, r4, lsl #1
	mla	r5, r1, r5, r3
	movw	r1, #65535
.L2960:
	cmp	r5, r3
	addeq	r4, r4, #1
	uxtheq	r4, r4
	beq	.L2957
.L2969:
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L2961
	ldr	r0, [r3, #12]
	ldrh	r9, [r0]
	cmp	r9, r1
	beq	.L2962
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L2962
	cmn	r7, #1
	ldr	r6, [r2, #-368]
	str	r0, [r2, #-368]
	bne	.L2962
	ldrh	r0, [lr]
	cmp	r0, r1
	bne	.L2963
	cmp	ip, #0
	beq	.L2962
.L2963:
	cmp	fp, r6
	movne	r7, r6
.L2962:
	add	r3, r3, #36
	b	.L2960
.L2961:
	ldrh	r1, [r10]
	movw	r2, #1846
	ldr	r3, .L3087+4
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r10, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2954
	ldr	r2, .L3087+20
	lsl	r4, r4, #1
	ldrh	r1, [r2, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2965
	cmn	r7, #1
	strne	r7, [r3, #-368]
	bne	.L2954
	ldr	r2, [sp, #12]
	cmp	fp, r2
	beq	.L2967
.L3080:
	str	r2, [r3, #-368]
	b	.L2954
.L2967:
	ldr	r2, [r3, #-368]
.L3086:
	sub	r2, r2, #1
	b	.L3080
.L2965:
	cmp	r6, fp
	beq	.L2968
	cmn	r6, #1
	strne	r6, [r3, #-368]
.L2954:
	ldr	r9, [sp, #8]
	mov	r2, #1
	ldr	r4, .L3087+4
	movw	r3, #1848
	strh	r2, [r4, r3]	@ movhi
.L2972:
	ldr	r3, .L3087+8
	movw	r6, #65535
	ldr	r0, [r4, #-536]
	mov	r7, #36
	ldr	r1, [sp, #20]
	mov	r2, #0
	ldrh	lr, [r3]
	ldr	r3, .L3087
	str	r2, [sp, #12]
	ldrb	r5, [r3, #152]	@ zero_extendqisi2
.L2973:
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L2976
	ldr	r2, [sp, #24]
	ldr	r1, [sp, #12]
	bl	FlashReadPages
	mov	r3, #0
.L3083:
	str	r3, [sp, #16]
	ldr	r2, [sp, #12]
	ldrh	r3, [sp, #16]
	cmp	r2, r3
	bhi	.L3002
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	add	r9, r9, #1
	uxth	r9, r9
	cmp	r3, #1
	bne	.L3003
	ldr	r3, .L3087
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3003
	ldr	r3, .L3087+24
	ldr	r2, [sp]
	ldrh	r3, [r3]
	cmp	r3, r9
	cmpeq	r2, r9
	beq	.L2979
.L3003:
	ldr	r3, .L3087+28
	ldrh	r3, [r3]
	cmp	r3, r9
	bne	.L2972
	ldr	r1, .L3087
	movw	r2, #2324
	movw	r0, #65535
	mov	r3, #0
	strh	r9, [r10, #2]	@ movhi
	ldrh	r2, [r1, r2]
	strh	r3, [r10, #4]	@ movhi
.L3004:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L3073
	ldr	r1, [sp, #20]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [sp, #20]
	add	r1, r3, #1
	bne	.L3084
	mov	r3, r1
	b	.L3004
.L2968:
	ldr	r2, [r3, #-368]
	cmp	fp, r2
	bne	.L3086
	b	.L2954
.L2970:
	mvn	r2, #0
	b	.L3080
.L2976:
	ldrh	r3, [r1], #2
	cmp	r3, r6
	beq	.L2974
	ldr	ip, [sp, #12]
	orr	r3, r9, r3, lsl #10
	mla	ip, r7, ip, r0
	str	r3, [ip, #4]
	ldrb	r8, [r10, #8]	@ zero_extendqisi2
	cmp	r8, #1
	bne	.L2975
	cmp	r5, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [ip, #4]
.L2975:
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #12]
.L2974:
	add	r2, r2, #1
	b	.L2973
.L3002:
	ldr	r3, [sp, #16]
	mov	r6, #36
	ldr	r8, [r4, #-536]
	mul	r6, r6, r3
	add	r7, r8, r6
	ldr	r5, [r7, #4]
	ubfx	r0, r5, #10, #16
	str	r5, [sp, #44]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r9, r3
	bcc	.L2978
	ldr	r2, [sp, #28]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L2978
	ldr	r3, [sp]
	ldr	r2, [sp, #4]
	cmp	r9, r3
	cmpeq	r2, r0
	beq	.L2979
	ldr	r3, [r8, r6]
	cmn	r3, #1
	beq	.L2980
	ldr	r3, [r7, #12]
	movw	r2, #61589
	ldrh	r1, [r3]
	cmp	r1, r2
	ldrhne	r0, [r10]
	bne	.L3081
	ldr	fp, [r3, #4]
	cmn	fp, #1
	beq	.L2982
	ldr	r1, [r4, #-3328]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r2, fp, #1
	strne	r2, [r4, #-3328]
.L2982:
	ldr	r5, [r3, #8]
	add	r1, sp, #40
	ldr	r3, [r3, #12]
	mov	r2, #0
	mov	r0, r5
	str	r3, [sp, #36]
	bl	log2phys
	ldr	r1, [r4, #-368]
	cmn	r1, #1
	beq	.L2983
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2983
	ldr	r3, [sp, #36]
	cmn	r3, #1
	beq	.L2984
	ldr	r0, [r4, #-536]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r6
	str	r3, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #-536]
	ldr	r1, [r2, r6]
	add	r3, r2, r6
	cmn	r1, #1
	bne	.L2985
.L2986:
	mvn	r3, #0
	str	r3, [sp, #36]
.L2993:
	ldr	r8, [sp, #36]
	cmn	r8, #1
	beq	.L2978
.L3007:
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-3540]
	lsl	r3, r0, #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2999
.L3081:
	bl	decrement_vpc_count
	b	.L2978
.L2984:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L2978
	mov	r2, #1
	add	r1, sp, #36
	mov	r0, r5
	bl	log2phys
.L2978:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L3083
.L2985:
	ldr	r1, [r7, #8]
	cmp	r5, r1
	bne	.L2986
	ldr	r8, [r7, #4]
	ldr	r0, [r4, #-368]
	mov	r1, r8
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2986
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L2988
.L2991:
	ldr	r1, [sp, #36]
	mov	r0, r5
	bl	FtlReUsePrevPpa
	b	.L2986
.L2988:
	ldr	r0, [sp, #36]
	cmp	r1, r0
	beq	.L2986
	cmn	r1, #1
	streq	r1, [r2, r6]
	beq	.L2990
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r7, [r3, #12]
	bl	FlashReadPages
.L2990:
	ldr	r3, [r4, #-536]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	beq	.L2991
	ldr	r3, [r7, #4]
	ldr	r0, [r4, #-368]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2991
	mov	r1, r3
	mov	r0, r8
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2986
	b	.L2991
.L2983:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L2993
	ldr	r3, [sp, #36]
	cmn	r3, #1
	beq	.L2995
	ldr	r2, .L3087
	ubfx	r3, r3, #10, #21
	ldr	r2, [r2, #2340]
	cmp	r3, r2
	bcs	.L2978
.L2995:
	mov	r2, #1
	add	r1, sp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r8, [sp, #40]
	cmn	r8, #1
	beq	.L2993
	ldr	r3, [sp, #36]
	cmp	r8, r3
	beq	.L3007
	ldr	r6, .L3087+32
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6]
	cmp	r3, r0
	beq	.L2998
	add	r2, r6, #48
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L2998
	add	r3, r6, #96
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L2993
.L2998:
	ldr	r0, [r6, #2984]
	mov	r2, #0
	mov	r1, #1
	str	r8, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r6, #2984]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2993
	ldr	r1, [r7, #4]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L2993
	mov	r2, #1
	add	r1, sp, #40
	mov	r0, r5
	bl	log2phys
	b	.L2993
.L2999:
	ldr	r0, .L3087+36
	bl	printk
	b	.L2978
.L2980:
	ldrh	r3, [r10]
	mov	r1, r5
	ldr	r2, .L3087+16
	ldr	r0, .L3087+40
	strh	r3, [r2]	@ movhi
	mov	r2, fp
	bl	printk
	ldr	r3, [r4, #1852]
	cmp	r3, #31
	ldrls	r1, [sp, #44]
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #1852]
	strls	r1, [r2, #1856]
	ldrh	r0, [r10]
	bl	decrement_vpc_count
	ldr	r3, [r4, #-368]
	cmn	r3, #1
	bne	.L3001
.L3082:
	str	fp, [r4, #-368]
	b	.L2978
.L3001:
	cmp	fp, r3
	bcs	.L2978
	b	.L3082
.L2979:
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	ldm	sp, {r1, r2}
	strb	r3, [r10, #6]
	ldrh	r3, [sp]
	strh	r3, [r10, #2]	@ movhi
.L3085:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L3073
.L3075:
	mov	r0, #0
	bx	lr
.L3088:
	.align	2
.L3087:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2324
	.word	.LC149
	.word	.LANCHOR2+1846
	.word	.LANCHOR2-2620
	.word	.LANCHOR0+2392
	.word	.LANCHOR0+2390
	.word	.LANCHOR2-3520
	.word	.LC150
	.word	.LC151
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlVpcCheckAndModify
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r5, #0
	ldr	r6, .L3104
	ldr	r1, .L3104+4
	ldr	r0, .L3104+8
	bl	printk
	movw	r3, #2334
	ldr	r4, .L3104+12
	ldrh	r2, [r6, r3]
	mov	r1, #0
	ldr	r0, [r4, #-476]
	lsl	r2, r2, #1
	bl	ftl_memset
.L3090:
	ldr	r3, [r6, #2452]
	cmp	r5, r3
	bcc	.L3092
	ldr	r8, .L3104+16
	mov	r7, #0
	ldr	r9, .L3104+20
	add	r10, r8, #96
.L3093:
	ldrh	r3, [r9]
	uxth	r6, r7
	cmp	r3, r6
	bhi	.L3096
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3092:
	mov	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L3091
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-476]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3091:
	add	r5, r5, #1
	b	.L3090
.L3096:
	uxth	r1, r7
	ldr	r3, [r4, #-3540]
	movw	r0, #65535
	lsl	r5, r1, #1
	ldrh	r2, [r3, r5]
	ldr	r3, [r4, #-476]
	ldrh	r3, [r3, r5]
	cmp	r2, r0
	cmpne	r2, r3
	beq	.L3094
	ldrh	r0, [r8]
	cmp	r0, r6
	beq	.L3094
	ldrh	r0, [r10]
	cmp	r0, r6
	beq	.L3094
	ldr	r0, .L3104+24
	ldrh	r0, [r0]
	cmp	r0, r6
	beq	.L3094
	ldr	r0, .L3104+28
	bl	printk
	ldr	r3, [r4, #-3540]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r4, #-476]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	bne	.L3095
.L3094:
	add	r7, r7, #1
	b	.L3093
.L3095:
	mov	r0, r6
	bl	update_vpc_list
	b	.L3094
.L3105:
	.align	2
.L3104:
	.word	.LANCHOR0
	.word	.LANCHOR3+203
	.word	.LC110
	.word	.LANCHOR2
	.word	.LANCHOR2-3520
	.word	.LANCHOR0+2332
	.word	.LANCHOR2-3472
	.word	.LC152
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	FtlGcScanTempBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L3154
	movw	r3, #3444
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	mov	r4, r0
	str	r1, [sp, #12]
	ldrh	r6, [r2, r3]
	movw	r3, #65535
	cmp	r6, r3
	beq	.L3138
	cmp	r6, #0
	bne	.L3107
.L3108:
	bl	FtlGcPageVarInit
	b	.L3109
.L3138:
	mov	r6, #0
.L3107:
	ldr	r2, .L3154+4
	movw	r3, #2390
	ldrh	r3, [r2, r3]
	ldr	r2, [sp, #12]
	cmp	r3, r2
	beq	.L3108
.L3109:
	ldr	r5, .L3154+8
	mvn	r3, #0
	str	r3, [sp, #8]
	mov	r3, #0
	str	r3, [sp]
.L3110:
	ldrh	r1, [r4]
	movw	r3, #65535
	mov	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L3111
.L3135:
	ldr	r3, .L3154+12
	add	ip, r4, #16
	ldr	r0, [r5, #-536]
	movw	r9, #65535
	ldr	lr, [r5, #-2692]
	mov	r10, #36
	ldrh	r3, [r3]
	str	r3, [sp, #4]
	ldr	r3, [r5, #-2696]
	str	r3, [sp, #16]
	ldr	r3, .L3154+16
	ldrh	r2, [r3]
	ldrh	r8, [r3, #2]
	str	r2, [sp, #20]
	mov	r2, #0
	mov	r7, r2
.L3112:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L3114
	ldr	r10, .L3154+4
	mov	fp, #0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.L3115:
	uxth	r3, fp
	cmp	r7, r3
	bhi	.L3133
	ldr	r3, [sp]
	add	r6, r6, #1
	uxth	r6, r6
	add	r3, r3, #1
	str	r3, [sp]
	ldr	r2, [sp]
	ldr	r3, [sp, #12]
	cmp	r3, r2
	ldr	r2, .L3154+20
	bls	.L3134
.L3136:
	ldrh	r3, [r2]
	cmp	r3, r6
	bhi	.L3135
	mov	r2, #0
	b	.L3111
.L3114:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L3113
	mla	r1, r10, r7, r0
	orr	r3, r6, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #20]
	mul	r3, r3, r7
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #16]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #8]
	mul	r3, r8, r7
	add	r7, r7, #1
	uxth	r7, r7
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.L3113:
	add	r2, r2, #1
	b	.L3112
.L3133:
	mov	r9, #36
	ldr	r8, [r5, #-536]
	mul	r9, r9, fp
	add	r3, r8, r9
	ldr	r2, [r3, #4]
	str	r3, [sp, #16]
	ubfx	r0, r2, #10, #16
	str	r2, [sp, #4]
	bl	P2V_plane
	ldr	r8, [r8, r9]
	mov	r2, r0
	ldr	r3, [sp, #16]
	cmp	r8, #0
	ldr	r3, [r3, #12]
	bne	.L3116
	ldrh	r0, [r3]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L3117
.L3120:
	ldrb	r1, [r10, #152]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3150
	mov	r3, #1
	str	r3, [r5, #-372]
.L3111:
	ldr	r1, .L3154
	mvn	r0, #0
	movw	r3, #3444
	strh	r6, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r1, r6
	mov	r0, r4
	bl	ftl_sb_update_avl_pages
	b	.L3106
.L3117:
	ldr	r0, [r3, #8]
	ldr	r1, [r10, #2452]
	cmp	r0, r1
	bhi	.L3120
	ldrb	r2, [r10, #36]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3123
.L3124:
	ldr	r2, [r3, #8]
	add	fp, fp, #1
	ldr	r1, [sp, #4]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L3115
.L3150:
	ldrh	r3, [r4]
	ldr	r2, [r5, #-3540]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L3154+24
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
.L3153:
	bl	FtlGcPageVarInit
	mov	r6, #0
	b	.L3110
.L3123:
	mov	r2, r8
	add	r1, sp, #24
	str	r3, [sp, #16]
	bl	log2phys
	ldr	r3, [sp, #16]
	ldr	r1, [sp, #24]
	ldr	r2, [r3, #12]
	cmn	r1, #1
	sub	r0, r2, r1
	clz	r0, r0
	lsr	r0, r0, #5
	moveq	r0, #0
	cmp	r0, #0
	beq	.L3124
	str	r2, [sp, #32]
	mov	r1, #1
	ldr	r2, [r5, #-504]
	add	r0, sp, #28
	str	r2, [sp, #36]
	ldr	r2, [r5, #-496]
	str	r2, [sp, #40]
	mov	r2, r8
	bl	FlashReadPages
	ldr	r2, .L3154+28
	ldr	r1, [r5, #-536]
	ldr	r3, [sp, #16]
	ldrh	r2, [r2]
	add	r9, r1, r9
	ldr	r1, [sp, #36]
	lsl	r2, r2, #7
.L3125:
	cmp	r8, r2
	beq	.L3124
	ldr	r0, [r9, #8]
	ldr	ip, [r0, r8, lsl #2]
	ldr	r0, [r1, r8, lsl #2]
	cmp	ip, r0
	beq	.L3126
	ldr	r2, [sp, #32]
	ldrh	r1, [r4]
	ldr	r0, .L3154+32
	bl	printk
	ldrh	r3, [r4]
	mov	r1, #0
	ldr	r2, [r5, #-3540]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L3154+36
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2, #-4]	@ movhi
	b	.L3153
.L3126:
	add	r8, r8, #1
	b	.L3125
.L3116:
	ldr	r2, [sp, #4]
	ldrh	r1, [r4]
	ldr	r0, .L3154+40
	bl	printk
	ldr	r3, [r10, #2248]
	cmp	r3, #0
	ldrh	r3, [r4]
	bne	.L3129
	ldrb	r2, [r10, #152]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3130
.L3129:
	ldr	r1, [r5, #-3604]
	lsl	r2, r3, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #159
	bls	.L3131
.L3130:
	ldr	r2, [r5, #-536]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L3132
.L3131:
	ldr	r2, [r5, #-536]
	add	r9, r2, r9
	ldr	r2, [r9, #4]
	str	r2, [sp, #8]
.L3132:
	ldr	r2, [r5, #-3540]
	lsl	r3, r3, #1
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	b	.L3153
.L3134:
	ldr	r1, .L3154+44
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L3136
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r6
	bls	.L3136
.L3106:
	ldr	r0, [sp, #8]
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3155:
	.align	2
.L3154:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2400
	.word	.LANCHOR0+2390
	.word	.LANCHOR2-3284
	.word	.LANCHOR0+2396
	.word	.LC153
	.word	.LANCHOR2-3280
	.word	.LC154
	.word	.LANCHOR1+3444
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r5, .L3173
	ldr	r9, [r5, #-3156]
	mov	r6, r5
	cmp	r9, #0
	beq	.L3157
	ldr	r3, .L3173+4
	ldr	r1, [r5, #-3152]
	ldr	r2, [r3, #2452]
	mov	r4, r3
	cmp	r1, r2
	bcs	.L3158
	mov	r5, #2048
.L3163:
	ldr	r0, [r6, #-3152]
	ldr	r3, [r4, #2452]
	cmp	r0, r3
	bcc	.L3159
.L3162:
	mvn	r0, #0
.L3156:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3159:
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r6, #-3152]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r6, #-3152]
	beq	.L3161
	str	r2, [sp, #8]
	add	r0, sp, #40
	mov	r2, #0
	mov	r1, #1
	str	r2, [r0, #-36]!
	str	r3, [sp, #20]
	str	r2, [sp, #12]
	str	r2, [sp, #16]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmp	r3, #256
	bne	.L3162
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L3162
.L3161:
	subs	r5, r5, #1
	bne	.L3163
	b	.L3162
.L3158:
	ldr	r3, [r5, #-3364]
	mov	r0, #0
	str	r0, [r5, #-3156]
	str	r0, [r5, #-3152]
	str	r3, [r5, #-3160]
	b	.L3156
.L3157:
	ldr	r1, [r5, #-3312]
	movw	r4, #10000
	ldr	r8, [r5, #-3364]
	sub	r10, r5, #3584
	ldr	r7, [r5, #-3160]
	cmp	r1, r4
	add	r3, r8, #1048576
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r3
	bhi	.L3167
	ldr	r3, .L3173+4
	lsr	r1, r1, #10
	mov	r0, #1000
	add	r1, r1, #1
	ldr	r3, [r3, #2452]
	mul	r0, r0, r3
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r8, r0
	bhi	.L3167
	ldrh	r3, [r10, #16]
	ands	r0, r4, r3
	movne	r0, r9
	bne	.L3156
	ldr	r2, [r5, #-3136]
	cmp	r3, r2
	beq	.L3156
.L3167:
	ldrh	r3, [r10, #16]
	mov	r0, #0
	str	r0, [r6, #-3152]
	str	r8, [r6, #-3160]
	str	r3, [r6, #-3136]
	mov	r3, #1
	str	r3, [r6, #-3156]
	b	.L3156
.L3174:
	.align	2
.L3173:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlGcFreeTempBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2390
	ldr	r4, .L3213
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L3213+4
	ldr	ip, [r4, #-3612]
	ldrh	r1, [r6, r3]
	cmp	ip, #0
	beq	.L3176
.L3212:
	mov	r0, #0
.L3175:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3176:
	sub	r5, r4, #3424
	movw	lr, #65535
	ldrh	r7, [r5]
	cmp	r7, lr
	bne	.L3178
.L3187:
	ldrh	r2, [r5]
	movw	r3, #65535
	mov	r7, #0
	str	r7, [r4, #-372]
	cmp	r2, r3
	beq	.L3212
	bl	FtlCacheWriteBack
	movw	r0, #2390
	ldrb	r2, [r4, #-3417]	@ zero_extendqisi2
	ldrh	r0, [r6, r0]
	mov	fp, #12
	ldrh	r3, [r5]
	ldr	r1, [r4, #-3540]
	ldr	r8, .L3213+8
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r2, [r4, #-3344]
	ldrh	r3, [r8, #-12]
	add	r3, r3, r2
	str	r3, [r4, #-3344]
.L3188:
	ldrh	r2, [r8, #-12]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L3192
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r6, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3193
	ldrh	r1, [r5]
	ldr	r0, .L3213+12
	bl	printk
.L3193:
	ldrh	r0, [r5]
	ldr	r2, [r4, #-3540]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3194
	bl	INSERT_DATA_LIST
.L3195:
	ldr	r7, .L3213+16
	mvn	r9, #0
	strh	r9, [r5]	@ movhi
	mov	r5, #0
	strh	r5, [r8, #-12]	@ movhi
	strh	r5, [r7, #-4]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	sub	r3, r7, #608
	sub	r2, r7, #848
	strh	r9, [r3, #-4]	@ movhi
	ldr	r3, [r6, #2248]
	ldrh	r2, [r2, #-4]
	cmp	r3, r5
	sub	r3, r7, #624
	ldrh	r3, [r3, #-8]
	beq	.L3196
	ldr	r1, [r4, #-3308]
	cmp	r1, #39
	bhi	.L3196
	cmp	r2, r3
	lslcc	r3, r3, #1
	strhcc	r3, [r7, #-48]	@ movhi
	b	.L3212
.L3178:
	cmp	r0, #0
	beq	.L3181
	ldr	r2, .L3213+20
	movw	r3, #3444
	ldrh	r0, [r2, r3]
	cmp	r0, lr
	beq	.L3182
.L3183:
	mov	r1, #2
.L3181:
	ldr	r0, .L3213+24
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #12]
	beq	.L3184
	ldr	r2, [r4, #-3604]
	lsl	r7, r7, #1
	ldrh	r3, [r2, r7]
	cmp	r3, #4
	bls	.L3185
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r7]	@ movhi
	bl	FtlEctTblFlush
.L3185:
	ldr	r3, [r4, #-372]
	cmp	r3, #0
	bne	.L3186
	ldr	r3, [r4, #-3140]
	ldr	r0, [sp, #12]
	add	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #-3140]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3186:
	mov	r3, #0
	str	r3, [r4, #-372]
.L3198:
	mov	r0, #1
	b	.L3175
.L3182:
	strh	ip, [r2, r3]	@ movhi
	sub	r3, r4, #3520
	ldrh	r3, [r3, #-4]
	cmp	r3, #17
	bhi	.L3183
	b	.L3181
.L3184:
	ldr	r2, .L3213+20
	movw	r3, #3444
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3198
	b	.L3187
.L3192:
	uxth	r10, r7
	ldr	r3, [r4, #-2672]
	ldr	r2, [r6, #2452]
	mul	r10, fp, r10
	add	r9, r3, r10
	ldr	r0, [r9, #8]
	cmp	r0, r2
	bcc	.L3189
.L3210:
	ldrh	r0, [r5]
	b	.L3211
.L3189:
	mov	r2, #0
	add	r1, sp, #12
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #12]
	ldr	r0, [r3, r10]
	cmp	r0, r2
	bne	.L3191
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r10, r0
	add	r1, r9, #4
	ldr	r0, [r9, #8]
	bl	log2phys
	mov	r0, r10
.L3211:
	bl	decrement_vpc_count
	b	.L3190
.L3191:
	ldr	r3, [r9, #4]
	cmp	r2, r3
	bne	.L3210
.L3190:
	add	r7, r7, #1
	b	.L3188
.L3194:
	bl	INSERT_FREE_LIST
	b	.L3195
.L3196:
	add	r1, r3, r3, lsl #1
	cmp	r2, r1, asr #2
	ble	.L3212
	ldrb	r0, [r6, #152]	@ zero_extendqisi2
	ldr	r2, .L3213+28
	cmp	r0, #0
	moveq	r3, #20
	strheq	r3, [r2]	@ movhi
	beq	.L3175
	sub	r3, r3, #2
	strh	r3, [r2]	@ movhi
	b	.L3212
.L3214:
	.align	2
.L3213:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-2656
	.word	.LC155
	.word	.LANCHOR2-2672
	.word	.LANCHOR1
	.word	.LANCHOR2-3424
	.word	.LANCHOR2-2720
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r5, #2390
	ldr	r4, .L3218
	ldr	r6, .L3218+4
	sub	r7, r4, #3424
	mov	r0, r7
	ldrh	r1, [r6, r5]
	bl	FtlGcScanTempBlk
	ldrh	r2, [r7, #2]
	ldrh	r3, [r6, r5]
	cmp	r2, r3
	popcc	{r4, r5, r6, r7, r8, pc}
	sub	r0, r4, #432
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #-372]
	pop	{r4, r5, r6, r7, r8, pc}
.L3219:
	.align	2
.L3218:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r4, .L3222
	sub	r6, r4, #3520
	str	r5, [r4, #1852]
	mov	r0, r6
	sub	r4, r4, #3472
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
.L3223:
	.align	2
.L3222:
	.word	.LANCHOR2
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	FtlSysBlkInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r2, #0
	ldr	r5, .L3242
	movw	r3, #1848
	ldr	r6, .L3242+4
	strh	r2, [r5, r3]	@ movhi
	mvn	r2, #0
	movw	r3, #1846
	strh	r2, [r5, r3]	@ movhi
	add	r3, r6, #2320
	add	r3, r3, #8
	ldrh	r0, [r3]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	sub	r3, r5, #3296
	ldrh	r2, [r3, #-4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3225
.L3227:
	mvn	r7, #0
.L3224:
	mov	r0, r7
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3225:
	bl	FtlLoadSysInfo
	subs	r7, r0, #0
	bne	.L3227
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	ldr	r2, [r5, #-3376]
	movw	r3, #2430
	ldrh	r1, [r6, r3]
	mov	r0, #12
	mov	r3, r7
.L3228:
	cmp	r3, r1
	bge	.L3233
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3229
.L3233:
	ldr	r4, .L3242+8
	cmp	r3, r1
	ldrh	r2, [r4, #28]
	add	r8, r4, #12
	add	r4, r4, #76
	add	r2, r2, #1
	strh	r2, [r4, #-48]	@ movhi
	bge	.L3240
.L3230:
	ldrh	r3, [r4]
	movw	ip, #2390
	ldr	r1, [r5, #-3540]
	ldrh	r0, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	mov	r2, #0
	ldrh	r3, [r6, ip]
	ldr	lr, [r5, #-3540]
	strb	r2, [r5, #-3514]
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, .L3242+12
	strh	r2, [r4, #4]	@ movhi
	ldrh	r1, [r3]
	ldrh	r9, [r3, #4]
	lsl	r1, r1, #1
	ldrh	r0, [lr, r1]
	sub	r0, r0, r9
	strh	r0, [lr, r1]	@ movhi
	ldrh	r1, [r6, ip]
	strh	r2, [r3, #4]	@ movhi
	strb	r2, [r5, #-3466]
	strh	r1, [r3, #2]	@ movhi
	ldrh	r3, [r8, #18]
	add	r3, r3, #1
	strh	r3, [r8, #18]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L3234
.L3229:
	add	r3, r3, #1
	b	.L3228
.L3240:
	movw	r3, #1848
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L3230
.L3234:
	ldrh	r0, [r4]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3235
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3235
	ldr	r4, .L3242+12
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3235
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	sub	r0, r4, #48
	bl	allocate_new_data_superblock
	mov	r0, r4
	bl	allocate_new_data_superblock
.L3235:
	ldrb	r3, [r6, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3236
	ldrh	r3, [r8, #16]
	tst	r3, #31
	bne	.L3224
.L3236:
	bl	FtlVpcCheckAndModify
	b	.L3224
.L3243:
	.align	2
.L3242:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3596
	.word	.LANCHOR2-3472
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	FtlLowFormat
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLowFormat, %function
FtlLowFormat:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L3276
	ldr	r6, [r4, #-3612]
	cmp	r6, #0
	bne	.L3246
	ldr	r5, .L3276+4
	movw	r7, #2428
	mov	r1, r6
	ldr	r0, [r4, #-448]
	ldrh	r2, [r5, r7]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-452]
	lsl	r2, r2, #2
	bl	ftl_memset
	add	r3, r5, #2320
	str	r6, [r4, #-3332]
	add	r3, r3, #8
	ldrh	r0, [r3]
	str	r6, [r4, #-3328]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3247
	bl	FtlMakeBbt
.L3247:
	ldr	r0, .L3276+8
	mov	r2, #0
	ldr	ip, .L3276+12
.L3248:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L3249
	movw	r3, #2332
	ldr	r9, .L3276+16
	ldrh	r7, [r5, r3]
	mov	r6, #0
	mov	r8, r9
.L3250:
	ldrh	r3, [r9]
	cmp	r3, r7
	bhi	.L3251
	movw	r3, #2324
	ldrh	r1, [r5, r3]
	sub	r3, r6, #3
	cmp	r3, r1, lsl #1
	blt	.L3252
	mov	r0, r6
	mov	r6, #0
	bl	__aeabi_uidiv
	ldr	r3, [r5, #2424]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	add	r3, r5, #2320
	add	r3, r3, #8
	ldrh	r0, [r3]
	bl	FtlFreeSysBlkQueueInit
	movw	r3, #2332
	ldrh	r7, [r5, r3]
.L3253:
	ldrh	r3, [r8]
	cmp	r3, r7
	bhi	.L3254
.L3252:
	ldr	r8, .L3276+20
	mov	r7, #0
	mov	r9, r7
.L3255:
	ldrh	r3, [r8]
	uxth	r0, r7
	add	r7, r7, #1
	cmp	r3, r0
	bhi	.L3256
	movw	r3, #2334
	ldr	r2, [r5, #2336]
	ldrh	r3, [r5, r3]
	ldr	r8, .L3276+24
	mov	r0, r2
	str	r2, [sp, #4]
	str	r3, [r4, #-548]
	movw	r3, #2324
	ldrh	r7, [r5, r3]
	mov	r1, r7
	bl	__aeabi_uidiv
	ubfx	r10, r0, #5, #16
	mov	fp, r0
	add	r3, r10, #36
	str	r0, [r5, #2452]
	strh	r3, [r8, #-8]	@ movhi
	mov	r3, #24
	mul	r3, r3, r7
	cmp	r9, r3
	ble	.L3257
	ldr	r2, [sp, #4]
	mov	r1, r7
	sub	r0, r2, r9
	bl	__aeabi_uidiv
	str	r0, [r5, #2452]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r8, #-8]	@ movhi
.L3257:
	ldr	r3, [r5, #2248]
	cmp	r3, #1
	bne	.L3258
	ldrh	r3, [r8, #-8]
	mov	r1, r7
	mov	r0, r9
	str	r3, [sp, #4]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	uxtah	r0, r3, r0
	add	r3, r3, r0, asr #2
	strh	r3, [r8, #-8]	@ movhi
.L3258:
	ldrb	r3, [r5, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3259
	ldrh	r3, [r8, #-8]
	mov	r1, r7
	mov	r0, r9
	str	r3, [sp, #4]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	uxtah	r0, r3, r0
	add	r3, r3, r0, asr #2
	strh	r3, [r8, #-8]	@ movhi
.L3259:
	ldr	r3, .L3276+28
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L3261
	ldrh	r2, [r8, #-8]
	add	r2, r2, r3, lsr #1
	strh	r2, [r8, #-8]	@ movhi
	mul	r2, r7, r3
	cmp	r9, r2
	addlt	r3, r3, #32
	strlt	fp, [r5, #2452]
	addlt	r3, r10, r3
	strhlt	r3, [r8, #-8]	@ movhi
.L3261:
	ldrh	r2, [r8, #-8]
	ldr	r3, [r5, #2452]
	sub	r3, r3, r2
	mul	r7, r7, r3
	movw	r3, #2390
	ldrh	r3, [r5, r3]
	str	r7, [r4, #1796]
	mul	r7, r7, r3
	movw	r3, #2396
	ldrh	r3, [r5, r3]
	str	r7, [r5, #2452]
	mul	r7, r7, r3
	str	r7, [r5, #2432]
	bl	FtlBbmTblFlush
	movw	r3, #2404
	ldr	r2, [r5, #2340]
	ldrh	r3, [r5, r3]
	add	r1, r6, r9
	add	r3, r3, r2, lsr #3
	cmp	r1, r3
	bls	.L3263
	lsr	r2, r2, #5
	ldr	r0, .L3276+32
	bl	printk
.L3263:
	movw	r3, #2334
	ldr	r6, .L3276+36
	ldrh	r2, [r5, r3]
	mov	r1, #0
	ldr	r0, [r4, #-3540]
	mvn	r7, #0
	sub	r6, r6, #240
	lsl	r2, r2, #1
	bl	ftl_memset
	mov	r3, #0
	strh	r7, [r6, #236]	@ movhi
	strh	r3, [r6, #238]	@ movhi
	mov	r1, #255
	str	r3, [r4, #-3368]
	strb	r3, [r4, #-3278]
	strb	r3, [r4, #-3276]
	strh	r3, [r6, #2]	@ movhi
	strb	r3, [r4, #-3514]
	strh	r3, [r6]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #-3512]
	movw	r3, #2332
	ldrh	r2, [r5, r3]
	ldr	r0, [r5, #32]
	mov	r5, r6
	lsr	r2, r2, #3
	bl	ftl_memset
.L3264:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #-3513]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L3265
	ldr	r2, [r4, #-3540]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L3264
.L3249:
	ldr	lr, [r4, #-508]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [lr, r3, lsl #2]
	ldr	r1, [r4, #-504]
	str	ip, [r1, r3, lsl #2]
	b	.L3248
.L3251:
	mov	r0, r7
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r7, r7, #1
	add	r6, r6, r0
	uxth	r6, r6
	uxth	r7, r7
	b	.L3250
.L3254:
	mov	r0, r7
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r7, r7, #1
	add	r6, r6, r0
	uxth	r6, r6
	uxth	r7, r7
	b	.L3253
.L3256:
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r9, r9, r0
	uxth	r9, r9
	b	.L3255
.L3265:
	ldr	r2, [r4, #-3332]
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	mvn	r6, #0
	str	r2, [r4, #-3508]
	add	r2, r2, #1
	str	r2, [r4, #-3332]
	ldr	r2, [r4, #-3540]
	strh	r1, [r2, r3]	@ movhi
	mov	r2, #0
	ldr	r3, .L3276+40
	strb	r2, [r4, #-3466]
	strh	r2, [r3, #2]	@ movhi
	mov	r7, r3
	ldrh	r2, [r5]
	mov	r5, r3
	add	r2, r2, #1
	strh	r2, [r3]	@ movhi
	mov	r2, #1
	strb	r2, [r4, #-3464]
.L3266:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #-3465]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L3267
	ldr	r2, [r4, #-3540]
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L3266
.L3267:
	ldr	r2, [r4, #-3332]
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	mvn	r5, #0
	str	r2, [r4, #-3460]
	add	r2, r2, #1
	str	r2, [r4, #-3332]
	ldr	r2, [r4, #-3540]
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, .L3276+44
	strh	r5, [r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r8, #4
	mov	r2, #0
	strh	r5, [r3, #4]	@ movhi
	ldr	r3, [r4, #-3332]
	strh	r2, [r8, #-2]	@ movhi
	ldr	r2, [r4, #1796]
	str	r3, [r4, #-3292]
	add	r3, r3, #1
	str	r3, [r4, #-3332]
	strh	r2, [r8, #2]	@ movhi
	strh	r0, [r8, #-4]	@ movhi
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldreq	r3, .L3276+48
	moveq	r2, #1
	streq	r2, [r3, #500]
.L3246:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3277:
	.align	2
.L3276:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2396
	.word	168778952
	.word	.LANCHOR0+2334
	.word	.LANCHOR0+2332
	.word	.LANCHOR2-3296
	.word	.LANCHOR0+2384
	.word	.LC156
	.word	.LANCHOR2-3280
	.word	.LANCHOR2-3472
	.word	.LANCHOR2-3424
	.word	.LANCHOR1
	.fnend
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	FtlReInitForSDUpdata
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #16
	ldr	r4, .L3314
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3279
.L3281:
	mov	r6, #0
.L3278:
	mov	r0, r6
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L3279:
	ldr	r5, .L3314+4
	ldr	r0, [r5, #1684]
	bl	FlashInit
	subs	r6, r0, #0
	bne	.L3281
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3282
	bl	FlashMakeFactorBbt
.L3282:
	ldr	r0, [r5, #1724]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3283
	mov	r2, #16
	mov	r1, #0
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp]
	mov	r3, #0
	mov	r2, r3
	mov	r0, #1
.L3285:
	ands	ip, r1, r0, lsl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L3285
	cmp	r3, #6
	bhi	.L3286
.L3311:
	strb	r2, [r4, #37]
	b	.L3287
.L3286:
	mov	r2, #0
	mov	r0, #1
.L3289:
	ands	ip, r1, r0, lsl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L3289
	cmp	r3, #17
	movhi	r3, #36
	strbhi	r3, [r4, #37]
	bls	.L3311
.L3287:
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	strh	r3, [r4, #150]	@ movhi
.L3283:
	ldr	r1, .L3314+8
	ldr	r0, .L3314+12
	bl	printk
	ldr	r0, .L3314+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #2328]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3291:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3292
.L3313:
	bl	FtlLowFormat
	cmp	r4, #3
	mvnhi	r6, #0
	bhi	.L3278
.L3293:
	add	r4, r4, #1
	b	.L3291
.L3292:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3313
	ldr	r3, .L3314+20
	mov	r2, #1
	str	r2, [r3, #500]
	b	.L3278
.L3315:
	.align	2
.L3314:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC76
	.word	.LC77
	.word	.LANCHOR0+124
	.word	.LANCHOR1
	.fnend
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, .L3332
	ldr	r3, [r4, #-3612]
	cmp	r3, #0
	beq	.L3317
.L3320:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L3317:
	ldr	r3, .L3332+4
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3319
	ldr	r3, [r4, #-2704]
	tst	r3, #1
	beq	.L3319
	sub	r3, r4, #3424
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3320
.L3319:
	mov	r3, #0
	mov	r5, #0
	mov	r6, #36
	mov	r2, r3
	ldr	r1, [r4, #-2704]
	ldr	r0, [r4, #-532]
	bl	FlashProgPages
.L3321:
	ldr	r1, [r4, #-2704]
	uxth	r3, r5
	cmp	r3, r1
	bcc	.L3323
	ldr	r0, [r4, #-532]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r4, #-2704]
	ldr	r3, .L3332+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3320
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L3331
.L3323:
	mul	r3, r6, r3
	ldr	r2, [r4, #-532]
	add	r5, r5, #1
	ldr	ip, [r2, r3]
	add	r1, r2, r3
	ldr	r0, [r1, #12]
	cmn	ip, #1
	bne	.L3322
	ldr	r1, .L3332+8
	mov	lr, #0
	ldr	r0, [r4, #-3540]
	ldrh	r2, [r1]
	lsl	r2, r2, #1
	strh	lr, [r0, r2]	@ movhi
	ldr	r2, [r4, #-3140]
	strh	ip, [r1]	@ movhi
	add	r2, r2, #1
	str	r2, [r4, #-3140]
	ldr	r2, [r4, #-532]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L3331:
	mov	r0, #1
	pop	{r4, r5, r6, pc}
.L3322:
	ldr	r2, [r0, #8]
	ldr	r1, [r1, #4]
	ldr	r0, [r0, #12]
	bl	FtlGcUpdatePage
	b	.L3321
.L3333:
	.align	2
.L3332:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3424
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3341
	movw	r2, #65535
	ldrh	r1, [r3]
	cmp	r1, r2
	beq	.L3335
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	ldrne	r0, .L3341
	bne	.L3340
.L3335:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r4, .L3341+4
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	sub	r0, r4, #3424
	strb	r5, [r4, #-3416]
	bl	allocate_data_superblock
	sub	r3, r4, #2672
	sub	r4, r4, #2656
	strh	r5, [r3, #-4]	@ movhi
	strh	r5, [r4, #-12]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	pop	{r4, r5, r6, lr}
	ldr	r0, .L3341
.L3340:
	b	get_new_active_ppa
.L3342:
	.align	2
.L3341:
	.word	.LANCHOR2-3424
	.word	.LANCHOR2
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	ftl_do_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3507
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	lr, r0
	.pad #44
	sub	sp, sp, #44
	ldr	r0, [r3, #-3612]
	cmp	r0, #0
	bne	.L3439
	ldr	ip, .L3507+4
	ldr	r5, [ip, #500]
	cmp	r5, #1
	bne	.L3343
	ldr	r2, [r3, #-560]
	cmp	r2, #0
	bne	.L3343
	sub	r8, r3, #3520
	ldrh	r0, [r8, #-12]
	cmp	r0, #47
	bls	.L3439
	movw	r2, #3444
	mov	r7, r1
	ldrh	r1, [ip, r2]
	movw	r2, #65535
	mov	r4, r3
	str	lr, [sp, #16]
	cmp	r1, r2
	bne	.L3345
.L3348:
	ldr	r6, .L3507+8
	movw	r2, #65535
	ldrh	r0, [r6, #-8]
	cmp	r0, r2
	bne	.L3346
.L3347:
	ldr	r3, [r4, #-2716]
	ldr	r2, [sp, #16]
	add	r3, r3, #1
	cmp	r2, #1
	add	r3, r3, r2, lsl #7
	str	r3, [r4, #-2716]
	bne	.L3349
	ldr	r9, .L3507+12
	ldr	r2, [r9, #2248]
	cmp	r2, #0
	bne	.L3350
	ldrb	r2, [r9, #152]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3349
.L3350:
	ldr	r2, [r4, #-3308]
	cmp	r2, #39
	bhi	.L3349
	ldr	r10, .L3507+16
	movw	r5, #65535
	ldrh	r2, [r10]
	add	r3, r2, r3
	str	r3, [r4, #-2716]
	bl	FtlGcReFreshBadBlk
	ldr	r3, .L3507+20
	mov	r2, r10
	ldrh	r3, [r3, #-4]
	cmp	r3, r5
	bne	.L3351
	ldrh	r1, [r6, #-10]
	cmp	r1, r3
	bne	.L3438
	ldr	r3, [r4, #-2716]
	cmp	r3, #1024
	bhi	.L3353
	ldrh	r3, [r8, #-4]
	cmp	r3, #63
	bhi	.L3438
.L3353:
	ldr	r6, .L3507+24
	mov	r1, #0
	ldrh	r0, [r8, #-4]
	strh	r1, [r2]	@ movhi
	ldrh	r3, [r6, #-14]
	add	r3, r3, #64
	cmp	r0, r3
	bgt	.L3438
	ldr	r3, [r4, #-3308]
	str	r1, [r4, #-2716]
	cmp	r3, r1
	moveq	r3, #6
	beq	.L3499
	cmp	r3, #5
	bhi	.L3355
	mov	r3, #18
.L3499:
	strh	r3, [r2]	@ movhi
.L3355:
	mov	r0, #32
	movw	r10, #65535
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r10
	beq	.L3359
	ldrh	ip, [r6, #-8]
	cmp	ip, #0
	beq	.L3357
	movw	r2, #2392
	uxth	r0, r0
	ldrh	lr, [r9, r2]
	movw	r2, #2324
	ldrh	r2, [r9, r2]
	lsl	r1, r0, #1
	ldr	r3, [r4, #-3540]
	str	r1, [sp, #12]
	mul	r2, r2, lr
	ldrh	r0, [r3, r1]
	str	r3, [sp, #8]
	add	r2, r2, #1
	cmp	r0, r2
	bgt	.L3359
	add	fp, ip, #1
	mov	r9, #0
	uxth	fp, fp
	mov	r0, ip
	str	r9, [r4, #-2708]
	strh	fp, [r6, #-8]	@ movhi
	bl	List_get_gc_head_node
	uxth	r5, r0
	ldr	r3, [sp, #8]
	ldr	r1, [sp, #12]
	cmp	r5, r10
	beq	.L3359
	lsl	r10, r5, #1
	mov	r2, r5
	ldrh	r0, [r3, r10]
	ldrh	r3, [r3, r1]
	mov	r1, fp
	str	r3, [sp]
	mov	r3, r0
	ldr	r0, .L3507+28
	bl	printk
	ldrh	r3, [r6, #-8]
	cmp	r3, #40
	bls	.L3358
	ldr	r3, [r4, #-3540]
	ldrh	r3, [r3, r10]
	cmp	r3, #32
	strhhi	r9, [r6, #-8]	@ movhi
.L3358:
	ldr	r3, .L3507+16
	mov	r2, #6
	strh	r2, [r3]	@ movhi
.L3351:
	movw	r0, #65535
	ldr	r3, [sp, #16]
	sub	r2, r5, r0
	clz	r2, r2
	lsr	r2, r2, #5
	cmp	r3, #0
	movne	r1, #0
	andeq	r1, r2, #1
	cmp	r1, #0
	beq	.L3373
	ldrh	r3, [r8, #-4]
	cmp	r3, #24
	movhi	r6, #1
	bhi	.L3374
	ldr	r1, .L3507+12
	movw	r2, #2390
	cmp	r3, #16
	ldrh	r6, [r1, r2]
	lsrhi	r6, r6, #5
	bhi	.L3374
	cmp	r3, #12
	lsrhi	r6, r6, #4
	bhi	.L3374
	cmp	r3, #8
	lsrhi	r6, r6, #2
.L3374:
	ldr	r1, .L3507+32
	ldrh	r2, [r1]
	cmp	r2, r3
	mov	r2, r1
	bcs	.L3378
	sub	r3, r1, #704
	movw	r0, #65535
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L3379
	ldrh	r0, [r1, #54]
	cmp	r0, r3
	bne	.L3379
	ldr	r3, .L3507+16
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L3380
	ldr	r3, .L3507+12
	ldr	ip, [r4, #-3368]
	ldr	r3, [r3, #2452]
	add	r3, r3, r3, lsl #1
	cmp	ip, r3, lsr #2
	movcs	r3, #18
	strhcs	r3, [r1]	@ movhi
	bcs	.L3382
.L3380:
	ldr	r3, .L3507+36
	ldrh	r3, [r3, #-8]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r2]	@ movhi
.L3382:
	mov	r3, #0
	str	r3, [r4, #-2708]
.L3343:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3345:
	sub	r3, r3, #3424
	ldrh	r3, [r3]
	cmp	r3, r2
	beq	.L3348
	mov	r0, r5
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3348
	mov	r0, r5
	b	.L3343
.L3346:
	ldrh	r3, [r6, #-10]
	cmp	r3, r2
	bne	.L3347
	ldrh	r1, [r6, #-6]
	cmp	r1, r3
	beq	.L3347
	ldrh	r2, [r6, #-4]
	cmp	r2, r3
	mvnne	r3, #0
	strhne	r0, [r6, #-10]	@ movhi
	strhne	r1, [r6, #-8]	@ movhi
	strhne	r2, [r6, #-6]	@ movhi
	strhne	r3, [r6, #-4]	@ movhi
	b	.L3347
.L3357:
	mov	r3, #1
	strh	r3, [r6, #-8]	@ movhi
.L3359:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	mov	r5, r0
	cmp	r0, r3
	bne	.L3351
	ldr	r3, .L3507+16
	mov	r2, #0
	strh	r2, [r3]	@ movhi
.L3349:
	ldr	r3, .L3507+20
	movw	r5, #65535
	ldrh	r3, [r3, #-4]
	cmp	r3, r5
	bne	.L3351
.L3438:
	ldr	r6, .L3507+40
	movw	r3, #65535
	ldrh	r5, [r6]
	cmp	r5, r3
	movne	r5, r3
	bne	.L3351
	add	r3, r6, #768
	ldrh	r9, [r3, #-10]
	cmp	r9, r5
	bne	.L3351
	ldrh	r3, [r8, #-4]
	ldr	r2, [r4, #-2716]
	cmp	r3, #24
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	bls	.L3351
	ldr	r2, .L3507+16
	mov	r3, #0
	str	r3, [r4, #-2716]
	strh	r3, [r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r9
	mov	r5, r0
	movne	r10, r0
	bne	.L3361
	add	r6, r6, #720
	ldrh	r2, [r8, #-4]
	ldrh	r3, [r6, #-14]
	mov	r9, r6
	cmp	r2, r3
	movcs	r3, #80
	strhcs	r3, [r6, #-14]	@ movhi
	bcs	.L3364
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r5
	beq	.L3364
	ldr	r3, [r4, #-564]
	ldr	r6, .L3507+12
	cmp	r3, #0
	uxth	r3, r0
	bne	.L3365
	movw	r2, #2344
	ldrh	r2, [r6, r2]
	cmp	r2, #3
	beq	.L3365
	ldr	r2, [r4, #-2724]
	cmp	r2, #0
	bne	.L3365
	ldr	r2, [r6, #2248]
	cmp	r2, #0
	bne	.L3365
	ldrb	r0, [r6, #152]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3366
.L3365:
	ldr	r2, [r4, #-3540]
	lsl	r3, r3, #1
	movw	r0, #2344
	ldrh	r0, [r6, r0]
	ldrh	r1, [r2, r3]
	movw	r3, #2392
	movw	r2, #2324
	ldrh	r3, [r6, r3]
	ldrh	r2, [r6, r2]
	cmp	r0, #3
	mul	r2, r3, r2
	lsreq	r3, r3, #1
	movne	r3, #0
	add	r3, r3, r2
	cmp	r1, r3
	bgt	.L3368
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r6, #2452]
	uxth	r10, r0
	ldr	r2, [r4, #-3368]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	bls	.L3500
.L3501:
	mov	r3, #128
.L3500:
	strh	r3, [r9, #-14]	@ movhi
	movw	r3, #65535
	cmp	r10, r3
	beq	.L3364
.L3361:
	ldr	r0, .L3507+32
	lsl	r1, r10, #1
	ldr	r3, [r4, #-3540]
	mov	r5, r10
	ldrh	r2, [r8, #-4]
	ldrh	r0, [r0]
	ldrh	r3, [r3, r1]
	str	r0, [sp, #4]
	ldr	r0, [r4, #-3604]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3507+44
	str	r1, [sp]
	mov	r1, r10
	bl	printk
	b	.L3364
.L3368:
	mov	r3, #128
.L3502:
	strh	r3, [r9, #-14]	@ movhi
.L3364:
	bl	FtlGcReFreshBadBlk
	b	.L3351
.L3366:
	ldr	r2, [r4, #-3540]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3371
	bl	List_get_gc_head_node
	uxth	r10, r0
	b	.L3501
.L3371:
	mov	r3, #64
	b	.L3502
.L3379:
	ldr	r3, .L3507+36
	ldrh	r3, [r3, #-8]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r2]	@ movhi
.L3378:
	ldr	r3, .L3507+12
	movw	r5, #65535
	ldr	r3, [r3, #2248]
	adds	r3, r3, #0
	movne	r3, #1
	cmp	r7, #2
	movhi	r3, #0
	cmp	r3, #0
	addne	r6, r6, #1
	uxthne	r6, r6
.L3384:
	ldr	r7, .L3507+20
	movw	r2, #65535
	ldrh	r3, [r7, #-4]
	cmp	r3, r2
	bne	.L3394
	cmp	r5, r3
	strhne	r5, [r7, #-4]	@ movhi
	bne	.L3396
	add	r3, r7, #624
	ldrh	r2, [r3, #-10]
	cmp	r2, r5
	beq	.L3396
	ldr	r1, [r4, #-3540]
	lsl	r2, r2, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	mvneq	r2, #0
	strheq	r2, [r3, #-10]	@ movhi
	ldrh	r2, [r3, #-10]
	strh	r2, [r7, #-4]	@ movhi
	mvn	r2, #0
	strh	r2, [r3, #-10]	@ movhi
.L3396:
	ldrh	r0, [r7, #-4]
	mov	r3, #0
	strb	r3, [r4, #-3276]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3394
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strhne	r3, [r7, #-4]	@ movhi
	ldr	r3, .L3507+12
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3400
	ldrh	r0, [r7, #-4]
	bl	ftl_get_blk_mode
	strb	r0, [r4, #-3276]
.L3400:
	ldrh	r2, [r7, #-4]
	movw	r3, #65535
	sub	r9, r7, #4
	cmp	r2, r3
	beq	.L3394
	mov	r0, r9
	bl	make_superblock
	mov	r3, #0
	movw	r2, #1986
	strh	r3, [r4, r2]	@ movhi
	strh	r3, [r7, #-2]	@ movhi
	strb	r3, [r4, #-3278]
	ldrh	r3, [r7, #-4]
	ldr	r2, [r4, #-3540]
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	movw	r3, #1988
	strh	r2, [r4, r3]	@ movhi
.L3394:
	ldrh	r3, [r7, #-4]
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L3401
	ldr	r2, .L3507+48
	ldrh	r1, [r2]
	cmp	r1, r3
	beq	.L3401
	ldrh	r2, [r2, #48]
	cmp	r2, r3
	bne	.L3402
.L3401:
	mvn	r3, #0
	strh	r3, [r7, #-4]	@ movhi
.L3402:
	ldr	r4, .L3507
	sub	r7, r4, #3280
.L3435:
	ldrh	r2, [r7, #-4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3403
	ldr	r9, .L3507+24
	mov	r3, #0
	ldr	r10, .L3507+52
	str	r3, [r4, #-2708]
.L3404:
	ldr	fp, .L3507+56
	ldrh	r8, [fp]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r2, r0
	sub	r1, fp, #568
	strh	r2, [r1, #-4]	@ movhi
	movw	r1, #65535
	cmp	r2, r1
	bne	.L3405
	mov	r3, #0
	mov	r0, #8
	strh	r3, [fp]	@ movhi
	b	.L3343
.L3373:
	ldr	r3, .L3507+40
	ldrh	r7, [r3]
	cmp	r7, r0
	bne	.L3385
	add	r0, r3, #768
	ldrh	r0, [r0, #-10]
	cmp	r0, r7
	movne	r2, #0
	andeq	r2, r2, #1
	cmp	r2, #0
	beq	.L3385
	ldrh	r2, [r3, #140]
	cmp	r2, r7
	beq	.L3386
.L3391:
	mov	r5, r7
.L3385:
	ldr	r3, .L3507+12
	ldr	r3, [r3, #2248]
	cmp	r3, #0
	moveq	r6, #1
	movne	r6, #2
	b	.L3384
.L3386:
	add	r5, r3, #704
	ldrh	r2, [r8, #-4]
	ldrh	r3, [r5]
	str	r1, [r4, #-2708]
	ldr	r6, .L3507+16
	cmp	r2, r3
	bls	.L3387
	ldrh	r3, [r6]
	cmp	r3, #0
	bne	.L3388
	ldr	r3, .L3507+12
	ldr	r2, [r4, #-3368]
	ldr	r3, [r3, #2452]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3503
.L3388:
	ldr	r3, .L3507+36
	ldrh	r3, [r3, #-8]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L3503:
	strh	r3, [r5]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #-3540]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	bls	.L3387
.L3504:
	ldrh	r0, [r6]
	b	.L3343
.L3387:
	ldrh	r0, [r6]
	cmp	r0, #0
	bne	.L3391
	ldr	r3, .L3507+36
	ldrh	r9, [r3, #-8]
	add	r3, r9, r9, lsl #1
	asr	r3, r3, #2
	strh	r3, [r5]	@ movhi
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #-3540]
	ldr	r2, .L3507+12
	lsl	r0, r0, #1
	ldrh	r1, [r3, r0]
	movw	r3, #2392
	ldrh	r0, [r2, r3]
	movw	r3, #2324
	ldrh	r3, [r2, r3]
	mul	r3, r3, r0
	add	r3, r3, r3, lsr #31
	cmp	r1, r3, asr #1
	ble	.L3392
	ldrh	r2, [r8, #-4]
	sub	r3, r9, #1
	cmp	r2, r3
	blt	.L3392
	bl	FtlReadRefresh
	b	.L3504
.L3392:
	cmp	r1, #0
	bne	.L3391
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r8, #-4]
	add	r0, r0, #1
	b	.L3343
.L3405:
	str	r0, [sp, #12]
	mov	r0, r2
	str	r2, [sp, #8]
	add	r8, r8, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #12]
	strhne	r8, [fp]	@ movhi
	bne	.L3404
	uxth	r3, r3
	ldrh	lr, [r10]
	ldr	r0, [r4, #-3540]
	uxth	r8, r8
	lsl	r1, r3, #1
	ldr	r3, .L3507+60
	strh	r8, [fp]	@ movhi
	ldrh	ip, [r0, r1]
	ldrh	r3, [r3]
	mul	r3, r3, lr
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L3408
	cmp	r8, #48
	cmphi	ip, #8
	bls	.L3409
	ldrh	ip, [fp, #36]
	cmp	ip, #35
	bhi	.L3409
.L3408:
	mov	ip, #0
	strh	ip, [r9, #-8]	@ movhi
.L3409:
	ldrh	r1, [r0, r1]
	movw	r0, #65535
	cmp	r3, r1
	cmple	r5, r0
	bne	.L3410
	ldrh	r0, [r9, #-8]
	cmp	r0, #3
	bhi	.L3410
	ldr	r1, .L3507+20
	mvn	r2, #0
	strh	r2, [r1, #-4]	@ movhi
	mov	r2, #0
	strh	r2, [r9, #-8]	@ movhi
.L3506:
	ldr	r3, .L3507+16
	b	.L3505
.L3410:
	cmp	r1, #0
	bne	.L3411
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r9, #-8]
	add	r3, r3, #1
	strh	r3, [r9, #-8]	@ movhi
	b	.L3404
.L3411:
	mov	r3, #0
	strb	r3, [r4, #-3276]
	ldr	r3, .L3507+12
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3412
	mov	r0, r2
	bl	ftl_get_blk_mode
	strb	r0, [r4, #-3276]
.L3412:
	ldr	r3, .L3507+20
	sub	r8, r3, #4
	mov	r0, r8
	bl	make_superblock
	add	r2, r8, #4
	ldr	r1, .L3507+64
	ldrh	r2, [r2, #-4]
	mov	r3, #0
	ldr	r0, [r4, #-3540]
	strh	r3, [r1]	@ movhi
	lsl	r2, r2, #1
	ldrh	r2, [r0, r2]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r4, #-3278]
	strh	r2, [r1, #2]	@ movhi
.L3403:
	ldr	r3, [sp, #16]
	cmp	r3, #1
	bne	.L3413
	bl	FtlReadRefresh
.L3413:
	mov	r3, #1
	str	r3, [r4, #-560]
	ldr	r3, .L3507+52
	ldrh	r3, [r3]
	str	r3, [sp, #8]
	ldr	r3, .L3507+12
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3414
	ldrb	r3, [r4, #-3276]	@ zero_extendqisi2
	cmp	r3, #1
	ldreq	r3, .L3507+68
	ldrheq	r3, [r3]
	streq	r3, [sp, #8]
.L3414:
	ldr	r3, .L3507+20
	ldr	r1, [sp, #8]
	ldrh	r3, [r3, #-2]
	add	r2, r3, r6
	cmp	r2, r1
	movgt	r2, r1
	subgt	r6, r2, r3
	mov	r3, #0
	uxthgt	r6, r6
	str	r3, [sp, #12]
	sub	r3, r7, #4
	str	r3, [sp, #20]
.L3416:
	ldrh	r3, [sp, #12]
	cmp	r6, r3
	bls	.L3423
	ldr	r3, .L3507+60
	add	ip, r7, #10
	ldr	r0, [r4, #-2688]
	movw	r9, #65535
	mov	lr, #36
	ldrh	r10, [r3]
	ldr	r3, [sp, #20]
	ldrh	r1, [r3, #2]
	ldr	r3, [sp, #12]
	add	r1, r1, r3
	mov	r3, #0
	mov	r8, r3
	b	.L3424
.L3418:
	ldrh	r2, [ip, #2]!
	add	r3, r3, #1
	cmp	r2, r9
	mlane	fp, lr, r8, r0
	addne	r8, r8, #1
	orrne	r2, r1, r2, lsl #10
	uxthne	r8, r8
	strne	r2, [fp, #4]
.L3424:
	uxth	r2, r3
	cmp	r10, r2
	bhi	.L3418
	mov	fp, #0
	ldrb	r2, [r4, #-3276]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
.L3419:
	uxth	r3, fp
	cmp	r8, r3
	ldrls	r3, [sp, #12]
	addls	r3, r3, #1
	strls	r3, [sp, #12]
	bls	.L3416
.L3422:
	mov	r3, #36
	ldr	r2, [r4, #-2688]
	mul	r9, r3, fp
	add	r1, r2, r9
	ldr	r2, [r2, r9]
	cmn	r2, #1
	beq	.L3420
	ldr	r10, [r1, #12]
	movw	r2, #61589
	ldrh	r1, [r10]
	cmp	r1, r2
	bne	.L3420
	mov	r2, #0
	add	r1, sp, #32
	ldr	r0, [r10, #8]
	str	r3, [sp, #24]
	bl	log2phys
	ldr	r1, [r4, #-2688]
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #24]
	add	r1, r1, r9
	ldr	r0, [r1, #4]
	bic	r2, r2, #-2147483648
	cmp	r2, r0
	bne	.L3420
	ldr	r2, .L3507+64
	ldr	r0, .L3507+64
	ldr	r1, [r1, #16]
	ldrh	r2, [r2]
	str	r3, [sp, #28]
	add	r2, r2, #1
	strh	r2, [r0]	@ movhi
	ldr	r0, [r4, #-2704]
	ldr	r2, [r4, #-532]
	mla	r2, r3, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #24]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [sp, #24]
	ldr	r1, [r4, #-532]
	ldr	r3, [sp, #28]
	str	r0, [r2, #4]
	ldr	r2, [r4, #-2704]
	mla	r3, r3, r2, r1
	ldr	r2, [r4, #-2688]
	add	r2, r2, r9
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #32]
	str	r3, [r10, #12]
	ldr	r3, .L3507+40
	ldrh	r3, [r3]
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r4, #-3328]
	ldr	r0, [r4, #-2688]
	str	r3, [r10, #4]
	ldr	r3, [r4, #-2704]
	add	r0, r0, r9
	add	r3, r3, #1
	str	r3, [r4, #-2704]
	bl	FtlGcBufAlloc
	ldr	r3, .L3507+12
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3421
	ldrb	r2, [r4, #-3417]	@ zero_extendqisi2
	ldr	r3, [r4, #-2704]
	cmp	r2, r3
	beq	.L3421
	ldr	r3, .L3507+40
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3420
.L3421:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3420
	ldr	r3, .L3507
	mov	r2, #0
	mvn	ip, #0
	sub	r1, r3, #3280
	str	r2, [r3, #-560]
	add	r3, r3, #1984
	strh	ip, [r1, #-4]	@ movhi
	strh	r2, [r1, #-2]	@ movhi
.L3505:
	ldrh	r0, [r3]
	b	.L3343
.L3420:
	add	fp, fp, #1
	b	.L3419
.L3423:
	ldrh	r3, [r7, #-2]
	add	r6, r6, r3
	ldr	r3, [sp, #8]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r7, #-2]	@ movhi
	bhi	.L3425
	ldr	r3, [r4, #-2704]
	cmp	r3, #0
	beq	.L3426
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r4, #-560]
	bne	.L3506
.L3426:
	ldr	r3, .L3507+64
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L3427
	ldrh	r3, [r7, #-4]
	ldr	r2, [r4, #-3540]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	movne	r6, r0
	ldrne	r8, .L3507+12
	bne	.L3428
.L3427:
	mvn	r3, #0
	strh	r3, [r7, #-4]	@ movhi
.L3425:
	ldr	r3, .L3507+72
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L3434
	ldr	r3, .L3507+52
	ldrh	r6, [r3]
	b	.L3435
.L3429:
	add	r6, r6, #1
.L3428:
	ldr	r3, [r8, #2452]
	cmp	r6, r3
	bcs	.L3433
	mov	r2, #0
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
	ldr	r0, [sp, #36]
	cmn	r0, #1
	beq	.L3429
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, #-4]
	cmp	r3, r0
	bne	.L3429
.L3433:
	ldr	r3, [r8, #2452]
	cmp	r6, r3
	bcc	.L3427
	ldrh	r3, [r7, #-4]
	mov	r1, #0
	ldr	r2, [r4, #-3540]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r7, #-4]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3427
.L3434:
	mov	r2, #0
	str	r2, [r4, #-560]
	ldr	r2, .L3507+16
	ldrh	r0, [r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L3343
.L3439:
	mov	r0, #0
	b	.L3343
.L3508:
	.align	2
.L3507:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-2656
	.word	.LANCHOR0
	.word	.LANCHOR2+1984
	.word	.LANCHOR2-3280
	.word	.LANCHOR2-2704
	.word	.LC157
	.word	.LANCHOR2-2720
	.word	.LANCHOR2-3296
	.word	.LANCHOR2-3424
	.word	.LC158
	.word	.LANCHOR2-3472
	.word	.LANCHOR0+2390
	.word	.LANCHOR2-2712
	.word	.LANCHOR0+2324
	.word	.LANCHOR2+1986
	.word	.LANCHOR0+2392
	.word	.LANCHOR2-3524
	.fnend
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L3552
	ldr	r9, [r6, #-3612]
	cmp	r9, #0
	bne	.L3511
	ldr	r4, .L3552+4
	ldr	r1, [r4, #2444]
	cmp	r1, #0
	beq	.L3511
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	mov	r7, #0
	ldr	r5, [r6, #1992]
	mov	r10, #36
	ldr	fp, .L3552+8
	cmp	r3, #0
	ldr	r0, [r4, #2448]
	ldrbne	r8, [r5, #8]	@ zero_extendqisi2
	moveq	r8, r9
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	subne	r8, r8, #1
	clzne	r8, r8
	lsrne	r8, r8, #5
	mov	r2, r8
	bl	FlashProgPages
.L3514:
	ldr	r3, [r4, #2444]
	cmp	r7, r3
	bcc	.L3521
.L3533:
	mov	r3, #0
	str	r3, [r4, #2444]
.L3511:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3521:
	mul	r3, r10, r7
	ldr	r2, [r4, #2448]
	add	r0, r2, r3
	ldr	r2, [r2, r3]
	cmn	r2, #1
	beq	.L3516
	ldr	r2, [r0, #4]
	cmp	r8, #0
	add	r1, sp, #12
	ldr	r0, [r0, #16]
	str	r3, [sp, #4]
	orrne	r2, r2, #-2147483648
	str	r2, [sp, #12]
	mov	r2, #1
	bl	log2phys
	ldr	r2, [r4, #2448]
	ldr	r3, [sp, #4]
	add	r3, r2, r3
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3519
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r1, [r6, #-3540]
	lsl	r2, r0, #1
	mov	r3, r0
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	bne	.L3520
	mov	r1, r0
	str	r0, [sp, #4]
	mov	r0, fp
	bl	printk
	ldr	r3, [sp, #4]
.L3520:
	mov	r0, r3
	bl	decrement_vpc_count
.L3519:
	add	r7, r7, #1
	b	.L3514
.L3531:
	mov	r7, #36
	ldr	r3, [r4, #2448]
	mul	r7, r7, r9
	mov	r10, #0
	mov	fp, #1
	mvn	r2, #0
	str	r2, [r3, r7]
.L3522:
	ldr	r2, [r4, #2448]
	add	r3, r2, r7
	ldr	r2, [r2, r7]
	ldr	r0, [r3, #4]
	cmn	r2, #1
	beq	.L3526
	cmp	r8, #0
	mov	r2, #1
	orrne	r0, r0, #-2147483648
	add	r1, sp, #12
	str	r0, [sp, #12]
	ldr	r0, [r3, #16]
	bl	log2phys
	ldr	r3, [r4, #2448]
	add	r7, r3, r7
	ldr	r3, [r7, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3529
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-3540]
	lsl	r3, r0, #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3530
	mov	r1, r0
	ldr	r0, .L3552+8
	bl	printk
.L3530:
	mov	r0, r7
	bl	decrement_vpc_count
.L3529:
	add	r9, r9, #1
.L3516:
	ldr	r3, [r4, #2444]
	cmp	r9, r3
	bcc	.L3531
	movw	r5, #16386
.L3534:
	ldr	r3, .L3552+12
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L3533
	mov	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3534
	b	.L3533
.L3526:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3523
	ldr	r1, [r6, #-3540]
	lsl	r3, r3, #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3552+16
	strb	r10, [r5, #6]
	strh	r10, [r5, #4]	@ movhi
	ldrh	r3, [r3]
	strh	r3, [r5, #2]	@ movhi
.L3523:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3524
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3524:
	ldr	r3, [r6, #-3140]
	add	r3, r3, #1
	str	r3, [r6, #-3140]
	ldr	r3, [r4, #2448]
	add	r3, r3, r7
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #2448]
	mov	r2, r0
	str	r0, [sp, #12]
	mov	r1, #1
	add	r0, r3, r7
	str	r2, [r0, #4]
	mov	r2, r8
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #2448]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	streq	fp, [r6, #-3612]
	ldr	r3, [r6, #-3612]
	cmp	r3, #0
	beq	.L3522
	b	.L3511
.L3553:
	.align	2
.L3552:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC159
	.word	.LANCHOR2-2658
	.word	.LANCHOR0+2390
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3560
	ldr	r3, [r3, #-3612]
	cmp	r3, #0
	bne	.L3557
	ldr	r3, .L3560+4
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, #500]
	cmp	r4, #1
	bne	.L3555
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3555:
	mov	r0, #0
	pop	{r4, pc}
.L3557:
	mov	r0, #0
	bx	lr
.L3561:
	.align	2
.L3560:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.fnend
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlDeInit, %function
FtlDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3568
	ldr	r3, [r3, #500]
	cmp	r3, #1
	bne	.L3565
	push	{r4, lr}
	.save {r4, lr}
	bl	FtlSysFlush
	mov	r0, #0
	pop	{r4, pc}
.L3565:
	mov	r0, #0
	bx	lr
.L3569:
	.align	2
.L3568:
	.word	.LANCHOR1
	.fnend
	.size	FtlDeInit, .-FtlDeInit
	.align	2
	.global	ftl_deinit
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_deinit, %function
ftl_deinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	ftl_flash_de_init
	bl	FtlDeInit
	pop	{r4, lr}
	b	ftl_flash_de_init
	.fnend
	.size	ftl_deinit, .-ftl_deinit
	.align	2
	.global	rk_ftl_de_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r1, #0
	ldr	r0, .L3574
	bl	printk
	pop	{r4, lr}
	b	ftl_deinit
.L3575:
	.align	2
.L3574:
	.word	.LC160
	.fnend
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	ftl_cache_flush
	.syntax unified
	.arm
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
	.align	2
	.global	rk_ftl_cache_write_back
	.syntax unified
	.arm
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
	.align	2
	.global	ftl_discard
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_discard, %function
ftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r6, r0
	ldr	r5, .L3596
	ldr	r3, [r5, #2432]
	cmp	r3, r1
	cmpcs	r3, r0
	movls	r0, #1
	movhi	r0, #0
	bls	.L3586
	add	r2, r6, r1
	mov	r4, r1
	cmp	r3, r2
	bcc	.L3586
	cmp	r1, #31
	bls	.L3578
	ldr	r7, .L3596+4
	ldr	r3, [r7, #-3612]
	cmp	r3, #0
	bne	.L3578
	bl	FtlCacheWriteBack
	movw	r3, #2396
	mov	r0, r6
	ldrh	r5, [r5, r3]
	mov	r1, r5
	bl	__aeabi_uidiv
	smulbb	r3, r0, r5
	mov	r8, r0
	sub	r6, r6, r3
	uxth	r6, r6
	cmp	r6, #0
	beq	.L3580
	sub	r5, r5, r6
	add	r8, r0, #1
	cmp	r5, r4
	movcs	r5, r4
	uxth	r5, r5
	sub	r4, r4, r5
.L3580:
	ldr	r5, .L3596+8
	mvn	r3, #0
	str	r3, [sp, #4]
	mov	r6, r5
.L3581:
	ldrh	r3, [r5]
	cmp	r4, r3
	bcs	.L3583
	ldr	r3, [r7, #1996]
	cmp	r3, #32
	bls	.L3584
	mov	r3, #0
	str	r3, [r7, #1996]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3584:
	mov	r0, #0
.L3578:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L3583:
	mov	r2, #0
	mov	r1, sp
	mov	r0, r8
	bl	log2phys
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L3582
	ldr	r3, [r7, #1996]
	mov	r2, #1
	add	r1, sp, #4
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r7, #1996]
	ldr	r3, [r7, #-3360]
	add	r3, r3, #1
	str	r3, [r7, #-3360]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3582:
	ldrh	r3, [r6]
	add	r8, r8, #1
	sub	r4, r4, r3
	b	.L3581
.L3586:
	mvn	r0, #0
	b	.L3578
.L3597:
	.align	2
.L3596:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2396
	.fnend
	.size	ftl_discard, .-ftl_discard
	.align	2
	.global	FtlDiscard
	.syntax unified
	.arm
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
	.align	2
	.global	ftl_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_read, %function
ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L3643
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	ip, [ip, #500]
	cmp	ip, #1
	bne	.L3623
	cmp	r0, #16
	mov	r8, r3
	str	r2, [sp, #28]
	mov	r5, r1
	bne	.L3601
	mov	r2, r3
	ldr	r1, [sp, #28]
	add	r0, r5, #256
	bl	FtlVendorPartRead
	mov	r10, r0
.L3599:
	mov	r0, r10
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3601:
	ldr	r2, .L3643+4
	ldr	r1, [sp, #28]
	ldr	r3, [r2, #2432]
	cmp	r1, r3
	cmpls	r5, r3
	bcs	.L3623
	add	r1, r5, r1
	cmp	r3, r1
	str	r1, [sp, #44]
	bcc	.L3623
	movw	r3, #2396
	mov	r0, r5
	ldrh	r4, [r2, r3]
	mov	r1, r4
	bl	__aeabi_uidiv
	ldr	r3, [sp, #44]
	mov	r1, r4
	str	r0, [sp, #36]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #36]
	ldr	r1, [sp, #28]
	str	r0, [sp, #40]
	rsb	r3, r3, #1
	add	r3, r3, r0
	str	r3, [sp, #32]
	ldr	r3, .L3643+8
	ldr	r2, [r3, #-3336]
	add	r2, r2, r1
	ldr	r1, [sp, #32]
	str	r2, [r3, #-3336]
	ldr	r2, [r3, #-3364]
	add	r2, r2, r1
	mov	r1, r0
	ldr	r0, [sp, #36]
	str	r2, [r3, #-3364]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3602
	bl	FtlCacheWriteBack
.L3602:
	ldr	r6, [sp, #36]
	mov	r3, #0
	ldr	r4, .L3643+8
	mov	r7, r3
	mov	r10, r3
	str	r3, [sp, #52]
	str	r3, [sp, #48]
.L3603:
	ldr	r3, [sp, #32]
	cmp	r3, #0
	bne	.L3620
	ldr	r3, .L3643+12
	ldrh	r3, [r3, #-2]
	cmp	r3, #0
	beq	.L3599
	mov	r1, #1
	ldr	r0, [sp, #32]
	bl	ftl_do_gc
	b	.L3599
.L3620:
	mov	r2, #0
	add	r1, sp, #76
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp, #76]
	cmn	r3, #1
	moveq	r9, #0
	beq	.L3605
	ldr	r2, [r4, #-536]
	mov	r9, #36
	mla	r9, r9, r7, r2
	str	r3, [r9, #4]
	ldr	r3, [sp, #36]
	cmp	r6, r3
	bne	.L3609
	ldr	r3, [r4, #-508]
	mov	r0, r5
	str	r3, [r9, #8]
	ldr	r3, .L3643+16
	ldrh	fp, [r3]
	mov	r1, fp
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #28]
	sub	r3, fp, r1
	str	r1, [sp, #56]
	cmp	r2, r3
	movcc	r3, r2
	cmp	r3, fp
	str	r3, [sp, #48]
	streq	r8, [r9, #8]
.L3610:
	ldr	r3, .L3643+20
	ldr	r2, [r4, #-496]
	str	r6, [r9, #16]
	ldrh	r3, [r3]
	mul	r3, r7, r3
	add	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [r9, #12]
	b	.L3608
.L3607:
	mla	r0, r0, r6, r9
	ldr	r2, [sp, #44]
	cmp	r5, r0
	movls	r3, #1
	movhi	r3, #0
	cmp	r2, r0
	movls	r3, #0
	cmp	r3, #0
	beq	.L3606
	sub	r0, r0, r5
	mov	r2, #512
	mov	r1, #0
	add	r0, r8, r0, lsl #9
	bl	ftl_memset
.L3606:
	add	r9, r9, #1
.L3605:
	ldr	r3, .L3643+16
	ldrh	r0, [r3]
	cmp	r9, r0
	bcc	.L3607
.L3608:
	ldr	r3, [sp, #32]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #32]
	beq	.L3612
	ldr	r3, .L3643+24
	ldrh	r3, [r3]
	cmp	r7, r3, lsl #3
	bne	.L3603
.L3612:
	cmp	r7, #0
	beq	.L3603
	mov	r2, #0
	mov	r1, r7
	ldr	r0, [r4, #-536]
	mov	fp, #0
	bl	FlashReadPages
	ldr	r3, [sp, #52]
	lsl	r3, r3, #9
	str	r3, [sp, #68]
	ldr	r3, [sp, #56]
	lsl	r3, r3, #9
	str	r3, [sp, #60]
	ldr	r3, [sp, #48]
	lsl	r3, r3, #9
	str	r3, [sp, #64]
.L3619:
	mov	r9, #36
	ldr	r3, [r4, #-536]
	mul	r9, r9, fp
	ldr	r1, [sp, #36]
	add	r3, r3, r9
	ldr	r2, [r3, #16]
	cmp	r1, r2
	bne	.L3614
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-508]
	cmp	r1, r3
	bne	.L3615
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
.L3642:
	bl	ftl_memcpy
.L3615:
	ldr	r3, [r4, #-536]
	ldr	r2, [r3, r9]
	add	r1, r3, r9
	cmn	r2, #1
	ldreq	r3, [r4, #-3164]
	moveq	r10, r2
	addeq	r3, r3, #1
	streq	r3, [r4, #-3164]
	ldr	r3, [r1, #12]
	ldr	r2, [r1, #16]
	ldr	r3, [r3, #8]
	cmp	r2, r3
	beq	.L3617
	ldr	r3, [r4, #-3164]
	add	r3, r3, #1
	str	r3, [r4, #-3164]
	ldr	r2, [r1, #8]
	ldr	r3, [r1, #12]
	ldr	r0, [r2, #4]
	str	r0, [sp, #16]
	ldr	r2, [r2]
	ldr	r0, .L3643+28
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
.L3617:
	ldr	r3, [r4, #-536]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmp	r3, #256
	bne	.L3618
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3618:
	add	fp, fp, #1
	cmp	r7, fp
	bne	.L3619
	mov	r7, #0
	b	.L3603
.L3609:
	ldr	r3, [sp, #40]
	cmp	r6, r3
	bne	.L3611
	ldr	r3, [r4, #-504]
	ldr	r1, [sp, #44]
	str	r3, [r9, #8]
	ldr	r3, .L3643+16
	ldrh	r2, [r3]
	mul	r3, r2, r6
	sub	r1, r1, r3
	cmp	r2, r1
	str	r1, [sp, #52]
	bne	.L3610
.L3641:
	sub	r3, r3, r5
	add	r3, r8, r3, lsl #9
	str	r3, [r9, #8]
	b	.L3610
.L3611:
	ldr	r3, .L3643+16
	ldrh	r3, [r3]
	mul	r3, r6, r3
	b	.L3641
.L3614:
	ldr	r1, [sp, #40]
	cmp	r1, r2
	bne	.L3615
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-504]
	cmp	r1, r3
	bne	.L3615
	ldr	r3, .L3643+16
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #40]
	mul	r0, r3, r0
	sub	r0, r0, r5
	add	r0, r8, r0, lsl #9
	b	.L3642
.L3623:
	mvn	r10, #0
	b	.L3599
.L3644:
	.align	2
.L3643:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2656
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2402
	.word	.LANCHOR0+2324
	.word	.LC148
	.fnend
	.size	ftl_read, .-ftl_read
	.align	2
	.global	ftl_vendor_read
	.syntax unified
	.arm
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
	mov	r0, #16
	b	ftl_read
	.fnend
	.size	ftl_vendor_read, .-ftl_vendor_read
	.align	2
	.global	FlashBootVendorRead
	.syntax unified
	.arm
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
	ldr	r3, .L3650
	ldr	r3, [r3, #500]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3647
	mov	r0, r4
	mov	r2, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
.L3647:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3651:
	.align	2
.L3650:
	.word	.LANCHOR1
	.fnend
	.size	FlashBootVendorRead, .-FlashBootVendorRead
	.align	2
	.global	ftl_sys_read
	.syntax unified
	.arm
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
	mov	r0, #16
	b	ftl_read
	.fnend
	.size	ftl_sys_read, .-ftl_sys_read
	.align	2
	.global	StorageSysDataLoad
	.syntax unified
	.arm
	.fpu softvfp
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r1
	mov	r5, r0
	mov	r2, #512
	mov	r1, #0
	mov	r0, r4
	bl	ftl_memset
	bl	rknand_device_lock
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	bl	ftl_sys_read
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
	.fnend
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.global	FtlRead
	.syntax unified
	.arm
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
	.align	2
	.global	FtlInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mvn	r3, #0
	ldr	r7, .L3673
	ldr	r5, .L3673+4
	ldr	r6, .L3673+8
	ldr	r1, .L3673+12
	str	r3, [r7, #500]
	mov	r3, #0
	ldr	r0, .L3673+16
	str	r3, [r5, #2000]
	str	r3, [r5, #-3612]
	bl	printk
	add	r0, r6, #124
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	add	r3, r6, #2320
	add	r3, r3, #8
	ldrh	r0, [r3]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3657
	ldr	r1, .L3673+20
	ldr	r0, .L3673+24
.L3672:
	bl	printk
.L3658:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3657:
	bl	FtlSysBlkInit
	subs	r4, r0, #0
	ldrne	r1, .L3673+20
	ldrne	r0, .L3673+28
	bne	.L3672
.L3659:
	mov	r1, #1
	sub	r5, r5, #3520
	str	r1, [r7, #500]
	bl	ftl_do_gc
	ldrh	r7, [r5, #-4]
	cmp	r7, #15
	bhi	.L3660
.L3663:
	ldr	r3, .L3673+32
	movw	r2, #65535
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L3661
	ldr	r2, .L3673+36
	ldrh	r2, [r2]
	cmp	r2, r3
	bne	.L3661
	and	r0, r4, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3661:
	mov	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	mov	r1, #1
	mov	r0, #0
	bl	ftl_do_gc
	ldrh	r2, [r5, #-4]
	add	r3, r7, #2
	cmp	r2, r3
	bhi	.L3658
	add	r4, r4, #1
	cmp	r4, #4096
	bne	.L3663
	b	.L3658
.L3660:
	ldrb	r3, [r6, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3658
	mov	r4, #128
.L3665:
	mov	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3665
	b	.L3658
.L3674:
	.align	2
.L3673:
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC76
	.word	.LC77
	.word	.LANCHOR3+224
	.word	.LC161
	.word	.LC162
	.word	.LANCHOR2-3284
	.word	.LANCHOR2-2666
	.fnend
	.size	FtlInit, .-FtlInit
	.align	2
	.global	rk_ftl_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_init, %function
rk_ftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #2048
	bl	ftl_dma32_malloc
	mov	r5, #0
	ldr	r4, .L3680
	ldr	r1, .L3680+4
	str	r0, [r4, #2004]
	sub	r0, r1, #324
	str	r5, [r4, #1688]
	str	r5, [r4, #1684]
	str	r5, [r4, #2008]
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #1684]
	cmp	r3, r5
	mvneq	r4, #0
	beq	.L3675
	bl	rk_nandc_irq_init
	mov	r3, #2048
	mov	r2, r5
	mov	r1, r5
	ldr	r0, [r4, #2004]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #1684]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L3677
	bl	FtlInit
.L3677:
	mov	r1, r4
	ldr	r0, .L3680+8
	bl	printk
.L3675:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3681:
	.align	2
.L3680:
	.word	.LANCHOR2
	.word	.LANCHOR2+2008
	.word	.LC163
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #48
	sub	sp, sp, #48
	ldr	r8, .L3697
	ldrb	r3, [r8, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3682
	ldr	r4, .L3697+4
	movw	r3, #1846
	ldr	r0, .L3697+8
	ldrh	r6, [r4, r3]
	sub	r9, r4, #3520
	ldr	r3, [r4, #-3540]
	sub	r5, r4, #3472
	mov	r1, r6
	lsl	r7, r6, #1
	ldrh	r2, [r3, r7]
	bl	printk
	ldrh	r0, [r9]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r5]
	bl	FtlGcRefreshOpenBlock
	mov	r0, r9
	bl	allocate_new_data_superblock
	mov	r0, r5
	movw	r5, #4097
	bl	allocate_new_data_superblock
.L3684:
	subs	r5, r5, #1
	beq	.L3688
	mov	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	ldr	r3, [r4, #-3540]
	ldrh	r3, [r3, r7]
	cmp	r3, #0
	bne	.L3684
.L3688:
	ldr	r3, [r4, #-3540]
	mov	r1, r6
	ldr	r0, .L3697+8
	ldrh	r2, [r3, r7]
	bl	printk
	ldr	r3, [r4, #-3540]
	ldrh	r5, [r3, r7]
	cmp	r5, #0
	bne	.L3686
	add	r0, sp, #48
	movw	r9, #65535
	strh	r6, [r0, #-48]!	@ movhi
	mov	r10, #36
	bl	make_superblock
	movw	r3, #2324
	add	r0, sp, #14
	ldrh	lr, [r8, r3]
	mov	r2, r5
	ldr	r8, [r4, #-3608]
	mov	ip, r5
.L3689:
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L3691
	ldr	r3, [r4, #-3540]
	mov	r1, r6
	ldr	r0, .L3697+12
	ldrh	r2, [r3, r7]
	bl	printk
	mov	r2, r5
	mov	r1, #0
	ldr	r0, [r4, #-3608]
	bl	FlashEraseBlocks
	mov	r2, r5
	mov	r1, #1
	ldr	r0, [r4, #-3608]
	bl	FlashEraseBlocks
.L3686:
	mvn	r2, #0
	movw	r3, #1846
	strh	r2, [r4, r3]	@ movhi
.L3682:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3691:
	ldrh	r3, [r0, #2]!
	add	r2, r2, #1
	cmp	r3, r9
	mlane	r1, r10, r5, r8
	lslne	r3, r3, #10
	addne	r5, r5, #1
	uxthne	r5, r5
	stmibne	r1, {r3, ip}
	strne	ip, [r1, #12]
	b	.L3689
.L3698:
	.align	2
.L3697:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC164
	.word	.LC165
	.fnend
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	2
	.global	rk_ftl_garbage_collect
	.syntax unified
	.arm
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
	.align	2
	.global	ftl_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_write, %function
ftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r3
	ldr	r4, .L3768
	.pad #84
	sub	sp, sp, #84
	ldr	r3, [r4, #-3612]
	cmp	r3, #0
	bne	.L3741
	mov	r9, r2
	ldr	r2, .L3768+4
	ldr	r2, [r2, #500]
	cmp	r2, #1
	movne	r0, r3
	bne	.L3700
	cmp	r0, #16
	mov	r7, r1
	bne	.L3702
	mov	r2, fp
	mov	r1, r9
	add	r0, r7, #256
	bl	FtlVendorPartWrite
.L3700:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3702:
	ldr	r10, .L3768+8
	ldr	r3, [r10, #2432]
	cmp	r9, r3
	cmpls	r1, r3
	bcs	.L3744
	add	r6, r1, r9
	cmp	r3, r6
	bcc	.L3744
	mov	r3, #2048
	mov	r0, r7
	str	r3, [r4, #2012]
	movw	r3, #2396
	ldrh	r5, [r10, r3]
	mov	r1, r5
	bl	__aeabi_uidiv
	mov	r1, r5
	str	r0, [sp, #4]
	sub	r0, r6, #1
	bl	__aeabi_uidiv
	ldr	r2, [sp, #4]
	cmp	r9, r5, lsl #1
	ldr	r3, [r4, #-3356]
	ldr	r1, [r10, #2444]
	sub	r6, r0, r2
	str	r0, [sp, #24]
	add	r8, r6, #1
	add	r3, r3, r8
	str	r3, [r4, #-3356]
	ldr	r3, [r4, #-3340]
	add	r3, r3, r9
	str	r3, [r4, #-3340]
	movcs	r3, #1
	movcc	r3, #0
	cmp	r1, #0
	str	r3, [sp, #16]
	beq	.L3745
	mov	r3, #36
	ldr	r2, [r10, #2448]
	mul	r3, r3, r1
	sub	r3, r3, #36
	add	r10, r2, r3
	ldr	r3, [sp, #4]
	ldr	r2, [r10, #16]
	cmp	r3, r2
	strne	fp, [sp, #12]
	bne	.L3705
	ldr	r2, [r4, #-3352]
	mov	r1, r5
	mov	r0, r7
	add	r2, r2, #1
	str	r2, [r4, #-3352]
	ldr	r2, [r4, #2016]
	add	r2, r2, #1
	str	r2, [r4, #2016]
	bl	__aeabi_uidivmod
	sub	r5, r5, r1
	ldr	r3, [r10, #8]
	cmp	r9, r5
	mov	r0, r1
	movcc	r5, r9
	mov	r1, fp
	lsl	r8, r5, #9
	add	r0, r3, r0, lsl #9
	mov	r2, r8
	bl	ftl_memcpy
	cmp	r6, #0
	bne	.L3706
	ldr	r3, [r4, #2016]
	cmp	r3, #2
	bgt	.L3706
.L3741:
	mov	r0, #0
	b	.L3700
.L3706:
	add	r3, fp, r8
	sub	r9, r9, r5
	str	r3, [sp, #12]
	add	r7, r7, r5
	ldr	r3, [sp, #4]
	mov	r8, r6
	add	r3, r3, #1
	str	r3, [sp, #4]
.L3705:
	mov	r3, #0
	str	r3, [r4, #2016]
.L3704:
	ldr	r1, [sp, #24]
	ldr	r0, [sp, #4]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3707
	bl	FtlCacheWriteBack
.L3707:
	ldr	r6, [sp, #4]
	ldr	r10, .L3768+8
	ldr	r5, .L3768+12
	str	r5, [r4, #1992]
.L3708:
	cmp	r8, #0
	bne	.L3736
	ldr	r3, [sp, #24]
	mov	r0, r8
	ldr	r2, [sp, #4]
	sub	r1, r3, r2
	bl	ftl_do_gc
	ldr	r2, .L3768+12
	ldrh	r3, [r2, #-4]
	mov	r4, r2
	cmp	r3, #5
	bls	.L3737
	cmp	r3, #31
	bhi	.L3741
	ldr	r3, .L3768+8
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3741
.L3737:
	ldr	r5, .L3768+16
.L3740:
	ldr	r3, .L3768+20
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3739
	ldr	r3, .L3768+24
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L3739
	ldrh	r2, [r5, #-8]
	cmp	r2, r3
	bne	.L3739
	and	r0, r8, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3739:
	ldr	r3, .L3768+28
	mov	r1, #1
	mov	r2, #128
	mov	r0, r1
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	ftl_do_gc
	mov	r1, #1
	mov	r0, #0
	bl	ftl_do_gc
	ldr	r3, .L3768
	ldr	r3, [r3, #-3612]
	cmp	r3, #0
	bne	.L3741
	ldrh	r3, [r4, #-4]
	cmp	r3, #2
	bhi	.L3741
	add	r8, r8, #1
	cmp	r8, #256
	bne	.L3740
	b	.L3741
.L3745:
	str	fp, [sp, #12]
	b	.L3704
.L3736:
	ldrh	r1, [r5, #4]
	ldr	r4, .L3768
	cmp	r1, #0
	bne	.L3709
	sub	r2, r4, #3520
	ldr	fp, .L3768+4
	cmp	r5, r2
	bne	.L3710
	sub	r0, r4, #3472
	ldrh	r5, [r0, #4]
	cmp	r5, #0
	bne	.L3711
	bl	allocate_new_data_superblock
	str	r5, [fp, #3448]
.L3711:
	ldr	r0, .L3768+12
	bl	allocate_new_data_superblock
	ldr	r5, .L3768+12
	ldr	r1, [fp, #3448]
	add	r2, r5, #48
	cmp	r1, #0
	movne	r5, r2
.L3712:
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3713
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3713:
	str	r5, [r4, #1992]
.L3709:
	ldr	r1, [r10, #2444]
	ldr	r2, [r4, #-540]
	sub	r2, r2, r1
	ldrh	r1, [r5, #4]
	cmp	r2, r8
	movcs	r2, r8
	cmp	r1, r2
	movcc	r3, r1
	movcs	r3, r2
	str	r3, [sp, #36]
	mov	r3, #0
.L3766:
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #36]
	cmp	r3, r2
	bne	.L3732
.L3715:
	ldr	r3, [sp, #20]
	ldr	r1, .L3768
	ldr	r2, [r10, #2444]
	sub	r8, r8, r3
	ldr	r3, [sp, #16]
	ldr	r1, [r1, #-540]
	cmp	r2, r1
	orrcs	r3, r3, #1
	cmp	r3, #0
	bne	.L3733
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	beq	.L3733
.L3735:
	mov	r3, #0
	str	r3, [sp, #16]
	b	.L3708
.L3710:
	str	r1, [fp, #3448]
	ldrh	r1, [r2, #4]
	cmp	r1, #0
	movne	r5, r2
	bne	.L3713
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L3712
.L3732:
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	beq	.L3715
	ldr	r3, [sp, #24]
	sub	r4, r3, r6
	ldr	r3, [sp, #16]
	clz	r4, r4
	lsr	r4, r4, #5
	and	r2, r4, r3
	ldr	r3, [sp, #20]
	cmp	r3, #0
	moveq	r2, #0
	andne	r2, r2, #1
	cmp	r2, #0
	beq	.L3716
	ldr	r3, .L3768+32
	add	r2, r7, r9
	ldrh	r1, [r3]
	mls	r2, r1, r6, r2
	cmp	r1, r2
	bne	.L3715
.L3716:
	mov	r2, #0
	add	r1, sp, #40
	mov	r0, r6
	mov	fp, #36
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r2, .L3768+36
	ldr	r1, [r10, #2444]
	ldr	ip, [r10, #2448]
	ldrh	r2, [r2]
	mla	ip, fp, r1, ip
	mul	lr, r2, r1
	str	r0, [ip, #4]
	ldr	r0, .L3768
	bic	r3, lr, #3
	str	r6, [ip, #16]
	str	r3, [sp, #28]
	ldr	lr, [sp, #28]
	ldr	r3, [r0, #-492]
	ldr	r0, [r0, #-512]
	str	r3, [sp, #32]
	add	r3, r3, lr
	str	r3, [sp, #8]
	str	r3, [ip, #12]
	ldr	r3, .L3768+40
	ldrh	lr, [r3]
	mul	r1, r1, lr
	bic	r1, r1, #3
	add	r1, r0, r1
	ldr	r0, [sp, #8]
	str	r1, [ip, #8]
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [sp, #4]
	cmp	r3, r6
	orreq	r4, r4, #1
	cmp	r4, #0
	beq	.L3717
	cmp	r3, r6
	bne	.L3718
	ldr	r3, .L3768+32
	mov	r0, r7
	ldrh	r4, [r3]
	mov	r1, r4
	bl	__aeabi_uidivmod
	sub	r4, r4, r1
	mov	fp, r1
	cmp	r4, r9
	movcs	r4, r9
.L3719:
	ldr	r3, .L3768+32
	ldrh	r2, [r3]
	cmp	r2, r4
	bne	.L3720
	ldr	r3, [sp, #4]
	cmp	r3, r6
	mulne	r1, r4, r6
	ldrne	r3, [sp, #12]
	ldreq	r1, [sp, #12]
	subne	r1, r1, r7
	addne	r1, r3, r1, lsl #9
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L3722
	ldr	r2, [r10, #2444]
	mov	ip, #36
	ldr	r0, [r10, #2448]
	mla	r2, ip, r2, r0
	str	r1, [r2, #8]
.L3723:
	ldr	r2, .L3768+44
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	strh	r2, [r3, r1]	@ movhi
	ldr	r1, .L3768
	ldr	r3, [sp, #8]
	ldr	r2, [r1, #-3328]
	str	r2, [r3, #4]
	add	r2, r2, #1
	cmn	r2, #1
	ldr	r3, [sp, #8]
	moveq	r2, #0
	str	r2, [r1, #-3328]
	ldr	r2, [sp, #40]
	str	r6, [r3, #8]
	add	r6, r6, #1
	str	r2, [r3, #12]
	ldrh	r2, [r5]
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r10, #2444]
	ldr	r3, [sp, #20]
	add	r2, r2, #1
	str	r2, [r10, #2444]
	add	r3, r3, #1
	b	.L3766
.L3718:
	ldr	r3, .L3768+32
	add	r4, r7, r9
	mov	fp, #0
	ldrh	r2, [r3]
	smulbb	r2, r2, r6
	sub	r4, r4, r2
	uxth	r4, r4
	b	.L3719
.L3722:
	ldr	r2, [r10, #2448]
	mov	ip, #36
	ldr	r0, [r10, #2444]
	mla	r0, ip, r0, r2
	ldr	r2, .L3768+40
	ldrh	r2, [r2]
.L3767:
	ldr	r0, [r0, #8]
	b	.L3764
.L3720:
	ldr	r2, [sp, #40]
	cmn	r2, #1
	beq	.L3724
	ldr	r1, [r10, #2448]
	mov	r0, #36
	str	r2, [sp, #48]
	ldr	r2, [r10, #2444]
	str	r6, [sp, #60]
	mla	r2, r0, r2, r1
	add	r0, sp, #44
	ldr	r1, [r2, #8]
	ldr	r2, [r2, #12]
	str	r1, [sp, #52]
	mov	r1, #1
	str	r2, [sp, #56]
	mov	r2, #0
	bl	FlashReadPages
	ldr	r2, [sp, #44]
	cmn	r2, #1
	ldr	r2, .L3768
	ldreq	r1, [r2, #-3164]
	addeq	r1, r1, #1
	streq	r1, [r2, #-3164]
	beq	.L3727
	ldr	r3, [sp, #8]
	ldr	r1, [r3, #8]
	cmp	r6, r1
	beq	.L3727
	ldr	r1, [r2, #-3164]
	ldr	r0, .L3768+48
	add	r1, r1, #1
	str	r1, [r2, #-3164]
	mov	r2, r6
	ldr	r1, [r3, #8]
	bl	printk
.L3727:
	ldr	r3, [sp, #4]
	lsl	r2, r4, #9
	cmp	r3, r6
	bne	.L3728
	ldr	r0, [r10, #2448]
	mov	ip, #36
	ldr	r1, [r10, #2444]
	mla	r1, ip, r1, r0
	ldr	r0, [r1, #8]
	ldr	r1, [sp, #12]
	add	r0, r0, fp, lsl #9
.L3764:
	bl	ftl_memcpy
	b	.L3723
.L3724:
	ldr	r2, [r10, #2448]
	mov	r1, #36
	ldr	r0, [r10, #2444]
	mla	r0, r1, r0, r2
	ldr	r2, .L3768+40
	mov	r1, #0
	ldrh	r2, [r2]
	ldr	r0, [r0, #8]
	bl	ftl_memset
	b	.L3727
.L3728:
	ldr	r3, .L3768+32
	mov	lr, #36
	ldr	r0, [r10, #2444]
	ldr	ip, [r10, #2448]
	ldrh	r1, [r3]
	ldr	r3, [sp, #12]
	mla	r0, lr, r0, ip
	mul	r1, r6, r1
	sub	r1, r1, r7
	add	r1, r3, r1, lsl #9
	b	.L3767
.L3717:
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L3729
	ldr	r2, [r10, #2444]
	ldr	r1, [r10, #2448]
	ldr	r3, .L3768+32
	mla	fp, fp, r2, r1
	ldrh	r2, [r3]
	ldr	r3, [sp, #12]
	mul	r2, r6, r2
	sub	r2, r2, r7
	add	r2, r3, r2, lsl #9
	str	r2, [fp, #8]
	b	.L3723
.L3729:
	ldr	r3, .L3768+32
	ldr	r2, [r10, #2444]
	ldr	r0, [r10, #2448]
	ldrh	r1, [r3]
	mla	fp, fp, r2, r0
	ldrh	r2, [r3, #4]
	ldr	r3, [sp, #12]
	mul	r1, r6, r1
	ldr	r0, [fp, #8]
	sub	r1, r1, r7
	add	r1, r3, r1, lsl #9
	b	.L3764
.L3733:
	bl	FtlCacheWriteBack
	cmp	r8, #1
	mov	r2, #0
	str	r2, [r10, #2444]
	bhi	.L3708
	b	.L3735
.L3744:
	mvn	r0, #0
	b	.L3700
.L3769:
	.align	2
.L3768:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2-3520
	.word	.LANCHOR2-2656
	.word	.LANCHOR2-3284
	.word	.LANCHOR2-2666
	.word	.LANCHOR2-2718
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2402
	.word	.LANCHOR0+2400
	.word	-3947
	.word	.LC166
	.fnend
	.size	ftl_write, .-ftl_write
	.align	2
	.global	ftl_vendor_write
	.syntax unified
	.arm
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
	mov	r0, #16
	b	ftl_write
	.fnend
	.size	ftl_vendor_write, .-ftl_vendor_write
	.align	2
	.global	FlashBootVendorWrite
	.syntax unified
	.arm
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
	ldr	r3, .L3775
	ldr	r3, [r3, #500]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3772
	mov	r0, r4
	mov	r2, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
.L3772:
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3776:
	.align	2
.L3775:
	.word	.LANCHOR1
	.fnend
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
	.align	2
	.global	ftl_sys_write
	.syntax unified
	.arm
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
	mov	r0, #16
	b	ftl_write
	.fnend
	.size	ftl_sys_write, .-ftl_sys_write
	.align	2
	.global	StorageSysDataStore
	.syntax unified
	.arm
	.fpu softvfp
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	mov	r4, r0
	bl	rknand_device_lock
	mov	r2, r5
	mov	r1, #1
	mov	r0, r4
	bl	ftl_sys_write
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	pop	{r4, r5, r6, pc}
	.fnend
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.global	FtlDumpSysBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	lsl	r8, r0, #10
	ldr	r4, .L3788
	.pad #24
	sub	sp, sp, #24
	mov	r6, r0
	mov	r5, #0
	ldr	r7, .L3788+4
	ldr	r3, [r4, #-524]
	add	r9, r4, #1760
	ldr	r10, .L3788+8
	str	r3, [r4, #1768]
	ldr	r3, [r4, #-500]
	str	r3, [r4, #1772]
.L3781:
	ldrh	r2, [r7]
	sxth	r3, r5
	cmp	r3, r2
	blt	.L3783
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3783:
	mov	r2, #1
	orr	r3, r3, r8
	mov	r1, r2
	mov	r0, r9
	str	r3, [r4, #1764]
	bl	FlashReadPages
	ldr	r2, [r4, #1768]
	mov	r1, r6
	ldr	r3, [r4, #1772]
	mov	r0, r10
	ldr	r2, [r2]
	str	r2, [sp, #16]
	ldr	r2, [r3, #12]
	str	r2, [sp, #12]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r3, [r3]
	ldr	r2, [r4, #1760]
	str	r3, [sp]
	ldr	r3, [r4, #1764]
	bl	printk
	ldr	r3, [r4, #1772]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3782
	mov	r3, #768
	mov	r2, #4
	ldr	r1, [r4, #-524]
	ldr	r0, .L3788+12
	bl	rknand_print_hex
.L3782:
	add	r5, r5, #1
	b	.L3781
.L3789:
	.align	2
.L3788:
	.word	.LANCHOR2
	.word	.LANCHOR0+2392
	.word	.LC167
	.word	.LC168
	.fnend
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	2
	.global	dump_map_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	dump_map_info, %function
dump_map_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3805
	movw	r2, #2332
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldrh	r6, [r3, r2]
	str	r3, [sp, #24]
.L3791:
	ldr	r3, .L3805+4
	ldr	r4, .L3805+8
	ldrh	r3, [r3]
	cmp	r3, r6
	bhi	.L3798
	ldr	r10, .L3805+12
	mov	r7, #0
	sub	r9, r4, #388
	add	fp, r4, #1760
.L3799:
	ldrh	r3, [r9]
	sxth	r5, r7
	cmp	r5, r3
	bge	.L3802
	lsl	r5, r5, #1
	mov	r6, #0
	ldr	r8, .L3805+16
	b	.L3803
.L3793:
	str	r3, [sp, #32]
	mov	r1, r6
	ldr	r3, .L3805+20
	str	r2, [sp, #36]
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #28]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	bne	.L3792
	ldr	r1, [sp, #28]
	mla	r0, r2, r5, r8
	lsl	r1, r1, #10
	stmib	r0, {r1, fp}
	mul	r1, r9, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	ip, r1, #3
	cmp	r1, #0
	movlt	r1, ip
	bic	r1, r1, #3
	add	r1, r10, r1
	str	r1, [r0, #12]
.L3792:
	add	r7, r7, #1
.L3800:
	uxth	r1, r7
	cmp	r3, r1
	bhi	.L3793
	cmp	r5, #0
	bne	.L3794
.L3797:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L3791
.L3794:
	ldr	r9, .L3805+24
	mov	r0, r8
	mov	r7, #0
	mov	r8, #36
	mov	r2, #1
	mov	r1, r5
	bl	FlashReadPages
.L3795:
	uxth	r3, r7
	cmp	r5, r3
	bls	.L3797
	ldr	r3, [r4, #-536]
	mla	r3, r8, r7, r3
	add	r7, r7, #1
	ldr	r1, [r3, #12]
	ldr	r2, [r3, #4]
	ldr	r3, [r3, #8]
	ldr	r0, [r3, #4]
	str	r0, [sp, #16]
	mov	r0, r9
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
	b	.L3795
.L3798:
	ldr	r2, .L3805+28
	mov	r7, #0
	ldr	r8, [r4, #-536]
	mov	r5, r7
	ldr	fp, [r4, #-2696]
	ldrh	r3, [r2]
	ldrh	r9, [r2, #78]
	mov	r2, #36
	ldr	r10, [r4, #-2692]
	b	.L3800
.L3801:
	ldr	r2, [r4, #-472]
	mov	r0, fp
	ldrh	r2, [r2, r5]
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r4, #1764]
	bl	FlashReadPages
	ldr	r2, [r4, #1768]
	ldr	r1, [r4, #-472]
	ldr	r3, [r4, #1772]
	ldr	r0, [r2, #4]
	ldrh	r1, [r1, r5]
	str	r0, [sp, #20]
	mov	r0, r8
	ldr	r2, [r2]
	str	r2, [sp, #16]
	ldr	r2, [r3, #12]
	str	r2, [sp, #12]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r3, [r3]
	ldr	r2, [r4, #1760]
	str	r3, [sp]
	ldr	r3, [r4, #1764]
	bl	printk
.L3803:
	ldrh	r2, [r10]
	sxth	r3, r6
	add	r6, r6, #1
	cmp	r3, r2
	blt	.L3801
	add	r7, r7, #1
	b	.L3799
.L3802:
	ldr	r3, [sp, #24]
	mov	r2, #2
	ldr	r1, [r4, #-472]
	movw	r5, #2428
	ldr	r0, .L3805+32
	ldr	r3, [r3, #2420]
	bl	rknand_print_hex
	ldr	r3, [sp, #24]
	mov	r2, #4
	ldr	r1, [r4, #-452]
	ldr	r0, .L3805+36
	ldrh	r3, [r3, r5]
	bl	rknand_print_hex
	ldr	r3, [sp, #24]
	mov	r2, #4
	ldr	r1, [r4, #-448]
	ldr	r0, .L3805+40
	ldrh	r3, [r3, r5]
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3806:
	.align	2
.L3805:
	.word	.LANCHOR0
	.word	.LANCHOR0+2334
	.word	.LANCHOR2
	.word	.LANCHOR0+2392
	.word	.LC113
	.word	.LANCHOR0+2350
	.word	.LC169
	.word	.LANCHOR0+2324
	.word	.LC170
	.word	.LC171
	.word	.LC172
	.fnend
	.size	dump_map_info, .-dump_map_info
	.align	2
	.global	flash_boot_enter_slc_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_boot_enter_slc_mode, %function
flash_boot_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L3809
	ldr	r2, [r3, #2268]
	ldr	r3, .L3809+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L3810:
	.align	2
.L3809:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	2
	.global	flash_boot_exit_slc_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L3813
	ldr	r2, [r3, #2268]
	ldr	r3, .L3813+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L3814:
	.align	2
.L3813:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	2
	.global	write_idblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 112
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r4, .L3864
	.pad #124
	sub	sp, sp, #124
	mov	r0, #256000
	mov	r10, r1
	mov	r5, r2
	ldr	r3, [r4, #48]
	ldr	r8, [r4, #40]
	ldrb	r7, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #8]
	beq	.L3842
	add	r0, r6, #508
	add	r0, r0, #3
	lsr	r9, r0, #9
	cmp	r9, #8
	bls	.L3840
	cmp	r9, #500
	bhi	.L3842
.L3817:
	ldr	r2, [r10]
	ldr	r3, .L3864+4
	cmp	r2, r3
	bne	.L3842
	smulbb	r7, r7, r8
	uxth	fp, r7
	sub	r0, fp, #1
	mov	r1, fp
	add	r0, r0, r9
	bl	__aeabi_uidiv
	str	r0, [sp, #32]
	add	r0, r10, #254976
	add	r0, r0, #512
	mov	r3, #0
	movw	r2, #63871
.L3821:
	ldr	r1, [r0, #-4]!
	cmp	r1, #0
	bne	.L3818
	ldr	r1, [r10, r3, lsl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	sub	r2, r2, #1
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3821
.L3820:
	mov	r3, #5
	mov	r1, r5
	mov	r2, #4
	ldr	r0, .L3864+8
	bl	rknand_print_hex
	ldrb	r2, [r4, #37]	@ zero_extendqisi2
	sub	r5, r5, #4
	ldr	r1, [r10, #512]
	ldr	r0, .L3864+12
	bl	printk
	ldr	r2, .L3864+16
	mov	r1, r9
	ldrh	r3, [r4, #150]
	ldr	r0, .L3864+20
	ldr	r2, [r2, #1716]
	str	r2, [sp]
	mov	r2, r9
	bl	printk
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	ldr	r2, [r10, #512]
	ldr	r4, .L3864
	cmp	r2, r3
	strhi	r3, [r10, #512]
	lsl	r3, r9, #7
	str	r3, [sp, #40]
	mov	r3, #0
	str	r3, [sp, #20]
	str	r3, [sp, #12]
.L3838:
	ldr	r2, [r5, #4]
	ldrb	r3, [r4, #37]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L3823
	ldr	r3, .L3864+16
	ldr	r3, [r3, #1716]
	cmp	r2, r3
	bcc	.L3823
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #12]
	cmp	r1, #0
	cmpne	r3, #1
	bls	.L3824
	ldr	r3, [r5]
	add	r3, r3, #1
	cmp	r2, r3
	beq	.L3823
.L3824:
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r3, [r4, #48]
	mov	r2, r9
	ldr	r6, [r5, #4]
	ldr	r7, [r4, #40]
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	ldr	r0, .L3864+24
	mul	r6, r6, fp
	str	r3, [sp, #16]
	ldrh	r3, [sp, #16]
	mov	r1, r6
	smulbb	r7, r7, r3
	bl	printk
	mov	r0, #0
	uxth	r7, r7
	bl	flash_boot_enter_slc_mode
	mov	r0, r6
	ldr	r1, [sp, #16]
	bl	__aeabi_uidiv
	mov	r2, #0
	mov	r1, r0
	mov	r0, r2
	bl	FlashEraseBlock
	cmp	r7, r9
	movcs	r8, #1
	bcs	.L3825
	mov	r2, #0
	mov	r8, #2
	add	r1, r6, r7
	mov	r0, r2
	bl	FlashEraseBlock
.L3825:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r4, #48]
	ldrh	r0, [r3, #10]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	lsl	r0, r0, #2
	mul	r0, r8, r0
	mov	r8, #0
	bl	__aeabi_idiv
	mov	r1, r7
	str	r0, [sp, #44]
	mov	r0, r6
	bl	__aeabi_uidivmod
	sub	r3, r6, r1
	str	r1, [sp, #28]
	str	r3, [sp, #36]
	str	r10, [sp, #24]
.L3826:
	ldr	r3, [sp, #44]
	cmp	r3, r8
	bhi	.L3830
	mov	r1, r6
	mov	r3, #0
	mov	r2, r9
	ldr	r0, .L3864+28
	bl	printk
	ldr	r3, [r4, #48]
	mov	r2, r9
	ldr	r6, [r5, #4]
	mov	r8, #0
	ldr	r7, [r4, #40]
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	ldr	r0, .L3864+32
	mul	r6, r6, fp
	str	r3, [sp, #16]
	ldrh	r3, [sp, #16]
	mov	r1, r6
	smulbb	r7, r7, r3
	bl	printk
	uxth	r7, r7
	mov	r0, r6
	mov	r1, r7
	bl	__aeabi_uidivmod
	sub	r3, r6, r1
	ldr	r2, [sp, #8]
	str	r3, [sp, #36]
	ldr	r3, [sp, #16]
	str	r1, [sp, #24]
	str	r2, [sp, #28]
	mul	r3, r3, r1
	ubfx	r3, r3, #2, #2
.L3831:
	cmp	r8, r9
	bcc	.L3833
	mov	r3, #0
	mov	r2, r9
	mov	r1, r6
	ldr	r0, .L3864+36
	bl	printk
	ldr	r2, [sp, #8]
	mov	r3, r10
	mov	r6, #0
.L3836:
	mov	r7, r2
	mov	r8, r3
	ldr	r0, [r7]
	add	r2, r2, #4
	ldr	r1, [r8]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L3834
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r3, [r8]
	ldr	r1, [sp, #12]
	ldr	r0, .L3864+40
	str	r3, [sp, #4]
	ldr	r3, [r7]
	str	r3, [sp]
	mov	r3, r6
	bic	r6, r6, #255
	ldr	r2, [r5, #4]
	lsl	r6, r6, #2
	bl	printk
	mov	r3, #256
	mov	r2, #4
	add	r1, r10, r6
	ldr	r0, .L3864+44
	bl	rknand_print_hex
	ldr	r1, [sp, #8]
	mov	r3, #256
	mov	r2, #4
	ldr	r0, .L3864+48
	add	r1, r1, r6
	bl	rknand_print_hex
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r1, [r5, #4]
	mov	r2, #0
	mov	r0, r2
	mul	r1, r1, fp
	bl	FlashEraseBlock
	ldr	r3, [sp, #32]
	cmp	r3, #1
	bls	.L3835
	ldr	r1, [r5, #4]
	mov	r2, #0
	mov	r0, r2
	mla	r1, r1, fp, fp
	bl	FlashEraseBlock
.L3835:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r1, [r5, #4]
	ldr	r0, .L3864+52
	bl	printk
.L3823:
	ldr	r3, [sp, #12]
	add	r5, r5, #4
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #12]
	bne	.L3838
	ldr	r0, [sp, #8]
	bl	ftl_free
	ldr	r3, [sp, #20]
	clz	r0, r3
	lsr	r0, r0, #5
	rsb	r0, r0, #0
.L3815:
	add	sp, sp, #124
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3840:
	mov	r9, #8
	b	.L3817
.L3818:
	ldr	r0, .L3864+56
	bl	printk
	b	.L3820
.L3830:
	ldr	r3, [sp, #28]
	add	r2, r3, r8
	lsrs	r2, r2, #2
	beq	.L3827
	ldrb	r0, [r4, #152]	@ zero_extendqisi2
	add	r1, r2, #1
	add	r3, r4, r1, lsl #1
	cmp	r0, #0
	ldrh	r3, [r3, #156]
	beq	.L3828
	ldr	r0, [r4, #2268]
	ldr	ip, .L3864+60
	cmp	r0, ip
	moveq	r3, r1
.L3828:
	sub	r3, r3, #-1073741823
	lsl	r3, r3, #2
	str	r3, [sp, #56]
.L3827:
	movw	r3, #61424
	str	r3, [sp, #60]
	add	r3, r4, r2, lsl #1
	ldrh	r7, [r3, #156]
	ldrb	r3, [r4, #152]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3829
	ldr	r3, [r4, #2268]
	ldr	r1, .L3864+60
	cmp	r3, r1
	moveq	r7, r2
.L3829:
	ldr	r2, [sp, #36]
	add	r8, r8, #4
	ldr	r3, [sp, #16]
	uxth	r8, r8
	mla	r3, r7, r3, r2
	ldr	r2, .L3864+16
	add	r7, r7, #1
	ldrb	r0, [r2, #1722]	@ zero_extendqisi2
	uxth	r7, r7
	str	r3, [sp, #52]
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	str	r3, [sp, #48]
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r2, [r4, #48]
	ldr	r3, [sp, #52]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	mov	r0, r3
	bl	__aeabi_uidiv
	add	r3, sp, #56
	ldr	r2, [sp, #24]
	mov	r1, r0
	mov	r0, #0
	bl	FlashProgPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #48]
	bl	FlashBchSel
	ldr	r1, [sp, #16]
	ldr	r0, [sp, #36]
	bl	__aeabi_uidiv
	mov	r2, r7
	mov	r1, r0
	mov	r0, #0
	bl	FlashPageProgMsbFFData
	ldr	r3, [sp, #24]
	add	r3, r3, #2048
	str	r3, [sp, #24]
	b	.L3826
.L3833:
	ldr	r2, [sp, #24]
	rsb	r7, r3, #4
	ldrb	r0, [r4, #152]	@ zero_extendqisi2
	uxth	r7, r7
	add	r2, r2, r8
	lsr	r2, r2, #2
	cmp	r0, #0
	add	r1, r4, r2, lsl #1
	ldrh	r1, [r1, #156]
	beq	.L3832
	ldr	r0, [r4, #2268]
	ldr	ip, .L3864+60
	cmp	r0, ip
	moveq	r1, r2
.L3832:
	ldr	r2, [sp, #36]
	add	r8, r7, r8
	uxth	r8, r8
	add	r3, r3, r2
	ldr	r2, [sp, #16]
	mla	r3, r1, r2, r3
	ldr	r2, [r4, #48]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	ldr	r2, .L3864+16
	str	r3, [sp, #52]
	ldrb	r3, [r4, #2316]	@ zero_extendqisi2
	ldrb	r0, [r2, #1722]	@ zero_extendqisi2
	str	r1, [sp, #48]
	str	r3, [sp, #44]
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [sp, #52]
	ldr	r1, [sp, #48]
	mov	r0, r3
	bl	__aeabi_uidiv
	mov	r3, #0
	mov	r1, r0
	ldr	r2, [sp, #28]
	mov	r0, r3
	bl	FlashReadPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #44]
	bl	FlashBchSel
	ldr	r3, [sp, #28]
	add	r3, r3, r7, lsl #9
	str	r3, [sp, #28]
	mov	r3, #0
	b	.L3831
.L3834:
	ldr	r1, [sp, #40]
	add	r6, r6, #1
	cmp	r1, r6
	bne	.L3836
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	str	r3, [sp, #20]
	b	.L3823
.L3842:
	mvn	r0, #0
	b	.L3815
.L3865:
	.align	2
.L3864:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC174
	.word	.LC175
	.word	.LANCHOR2
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
	.word	1446522928
	.fnend
	.size	write_idblock, .-write_idblock
	.align	2
	.global	write_loader_lba
	.syntax unified
	.arm
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
	ldr	r4, .L3890
	bne	.L3867
	ldr	r2, [r2]
	ldr	r3, .L3890+4
	cmp	r2, r3
	bne	.L3867
	mov	r3, #1
	mov	r0, #256000
	strb	r3, [r4, #2020]
	bl	ftl_malloc
	mov	r2, #256000
	mov	r1, #0
	str	r0, [r4, #2024]
	bl	ftl_memset
	str	r5, [r4, #2028]
.L3867:
	str	r6, [sp]
	mov	r3, r5
	ldr	r2, [r8]
	ldr	r1, [r4, #2024]
	ldr	r0, .L3890+8
	bl	printk
	ldrb	r3, [r4, #2020]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3866
	sub	r0, r5, #64
	ldr	r7, [r4, #2024]
	cmp	r0, #500
	bcs	.L3869
	rsb	r2, r5, #564
	mov	r1, r8
	cmp	r6, r2
	add	r0, r7, r0, lsl #9
	movcc	r2, r6
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L3870:
	ldr	r3, [r4, #2028]
	cmp	r5, r3
	beq	.L3879
	mov	r3, #0
	cmp	r7, r3
	strb	r3, [r4, #2020]
	mov	r8, r3
	beq	.L3880
	mov	r0, r7
	bl	ftl_free
.L3880:
	str	r8, [r4, #2024]
	b	.L3879
.L3869:
	cmp	r5, #564
	bcc	.L3870
	ldr	r3, .L3890+12
	ldr	r0, [r4, #2028]
	ldr	r3, [r3, #48]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L3871
	mov	r3, #2
	str	r3, [sp, #8]
	mov	r3, #3
	str	r3, [sp, #12]
	mov	r3, #4
	str	r3, [sp, #16]
	mov	r3, #5
	str	r3, [sp, #20]
	mov	r3, #6
	str	r3, [sp, #24]
.L3872:
	movw	r3, #63872
.L3878:
	ldr	r2, [r7, r3, lsl #2]
	cmp	r2, #0
	beq	.L3876
	add	r3, r3, #128
	lsl	r0, r3, #2
.L3877:
	mov	r1, r7
	add	r2, sp, #8
	mov	r7, #0
	bl	write_idblock
	ldr	r0, [r4, #2024]
	strb	r7, [r4, #2020]
	bl	ftl_free
	str	r7, [r4, #2024]
.L3879:
	add	r5, r5, r6
	str	r5, [r4, #2028]
.L3866:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L3871:
	mov	r2, #0
	add	r3, sp, #8
.L3875:
	cmp	r0, #256
	lslhi	r1, r2, #1
	strls	r2, [r3, r2, lsl #2]
	strhi	r1, [r3, r2, lsl #2]
	add	r2, r2, #1
	cmp	r2, #5
	bne	.L3875
	b	.L3872
.L3876:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L3878
	lsl	r0, r0, #9
	b	.L3877
.L3891:
	.align	2
.L3890:
	.word	.LANCHOR2
	.word	-52655045
	.word	.LC185
	.word	.LANCHOR0
	.fnend
	.size	write_loader_lba, .-write_loader_lba
	.align	2
	.global	FtlWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r2
	sub	r2, r1, #64
	mov	r4, r1
	cmp	r2, #1984
	mov	r7, r3
	movcs	r2, #0
	movcc	r2, #1
	cmp	r0, #0
	mov	r5, r0
	movne	r2, #0
	cmp	r2, #0
	beq	.L3893
	mov	r2, r3
	mov	r1, r6
	mov	r0, r4
	bl	write_loader_lba
.L3893:
	mov	r3, r7
	mov	r2, r6
	mov	r1, r4
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, lr}
	b	ftl_write
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 520
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3955
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	mov	r4, r1
	.pad #524
	sub	sp, sp, #524
	mov	r5, r2
	cmp	r1, r3
	beq	.L3900
	bhi	.L3901
	sub	r3, r3, #2080
	sub	r3, r3, #6
	cmp	r1, r3
	beq	.L3902
	bhi	.L3903
	sub	r3, r3, #238
	cmp	r1, r3
	beq	.L3904
	add	r3, r3, #237
	cmp	r1, r3
	beq	.L3905
.L3935:
	mvn	r4, #21
	b	.L3898
.L3903:
	ldr	r3, .L3955+4
	cmp	r1, r3
	beq	.L3906
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L3907
	sub	r3, r3, #124
	cmp	r1, r3
	bne	.L3935
	ldr	r0, .L3955+8
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3918
	ldr	r2, [sp]
	ldr	r3, .L3955+12
	cmp	r2, r3
	bne	.L3915
	ldr	r2, [sp, #4]
	cmp	r2, #512
	ldrls	r1, .L3955+16
	bhi	.L3915
.L3953:
	add	r0, sp, #8
	bl	memcpy
	b	.L3947
.L3901:
	ldr	r3, .L3955+20
	cmp	r1, r3
	mov	r6, r3
	beq	.L3909
	bhi	.L3910
	sub	r3, r3, #2512
	sub	r3, r3, #14
	cmp	r1, r3
	beq	.L3900
	add	r3, r3, #10
	cmp	r1, r3
	bne	.L3935
.L3900:
	ldr	r3, .L3955+24
	cmp	r4, r3
	mov	r7, r3
	ldreq	r0, .L3955+28
	beq	.L3949
	ldr	r3, .L3955+32
	cmp	r4, r3
	ldreq	r0, .L3955+36
	ldrne	r0, .L3955+40
.L3949:
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3918
	ldr	r2, [sp]
	ldr	r3, .L3955+44
	cmp	r2, r3
	bne	.L3952
	ldr	r3, .L3955+32
	ldr	r6, .L3955+48
	cmp	r4, r3
	bne	.L3928
	ldr	r3, [r6, #2032]
	mov	r2, #16
	mov	r1, sp
	mov	r0, r5
	ldr	r3, [r3, #20]
	str	r3, [sp, #4]
	strb	r3, [sp, #8]
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3952
.L3919:
	mov	r4, #0
.L3898:
	mov	r0, r4
	add	sp, sp, #524
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L3910:
	ldr	r3, .L3955+52
	cmp	r1, r3
	beq	.L3909
	bcc	.L3911
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L3935
.L3911:
	ldr	r0, .L3955+56
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3918
	ldr	r2, [sp]
	ldr	r3, .L3955+60
	cmp	r2, r3
	bne	.L3915
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L3915
	ldr	r3, .L3955+64
	mov	r1, sp
	add	r2, r2, #8
	cmp	r4, r3
	ldr	r4, .L3955+48
	bne	.L3934
	ldr	r0, [r4, #2564]
	bl	memcpy
	ldr	r1, [r4, #2564]
	mov	r0, #2
	b	.L3950
.L3905:
	ldr	r0, .L3955+68
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L3912
.L3918:
	ldr	r0, .L3955+72
	bl	printk
.L3952:
	mvn	r4, #13
	b	.L3898
.L3912:
	ldr	r2, [sp]
	ldr	r3, .L3955+76
	cmp	r2, r3
	beq	.L3913
.L3915:
	mvn	r4, #0
.L3914:
	mov	r1, r4
	ldr	r0, .L3955+80
	bl	printk
	b	.L3898
.L3913:
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3915
	ldr	r4, .L3955+48
	mov	r2, #512
	mov	r0, sp
	ldr	r1, [r4, #2032]
	bl	memcpy
	ldr	r2, [r4, #2036]
	ldr	r3, .L3955+84
	cmp	r2, r3
	beq	.L3916
	mov	r1, #0
	mov	r2, #128
	add	r0, sp, #64
	str	r1, [sp, #8]
	str	r1, [sp, #12]
	bl	memset
.L3916:
	mov	r2, #256
	mov	r1, #0
	add	r0, sp, r2
	str	r1, [sp, #16]
	bl	memset
.L3947:
	mov	r2, #520
	mov	r1, sp
	mov	r0, r5
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L3952
.L3951:
	mov	r4, #0
	b	.L3914
.L3902:
	ldr	r0, .L3955+88
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3918
	ldr	r2, [sp]
	ldr	r3, .L3955+76
	cmp	r2, r3
	bne	.L3915
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3915
	ldr	r2, .L3955+48
	ldr	r3, .L3955+84
	ldr	r1, [r2, #2036]
	cmp	r1, r3
	mvnne	r4, #1
	bne	.L3898
	ldr	r3, [sp, #12]
	sub	r1, r3, #1
	cmp	r1, #127
	mvnhi	r4, #2
	bhi	.L3898
	ldr	r4, [r2, #2032]
	add	r1, sp, #64
	str	r3, [r4, #12]
	add	r0, r4, #64
	ldr	r2, [sp, #12]
	bl	memcpy
	mov	r1, r4
	mov	r0, #1
.L3950:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L3914
.L3907:
	ldr	r0, .L3955+92
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3918
	ldr	r2, [sp]
	ldr	r3, .L3955+96
	cmp	r2, r3
	bne	.L3915
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3915
	ldr	r5, .L3955+48
	ldr	r3, [r5, #2040]
	cmp	r3, #0
	beq	.L3919
	ldr	r3, [r5, #2044]
	ldr	r2, .L3955+100
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3920
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r5, #2044]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3920:
	ldr	r1, [r5, #2044]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #2032]
	ldr	r2, .L3955+76
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	movne	r2, #504
	ldrne	r3, [r5, #2032]
	ldr	r6, [r5, #2032]
	stmibne	r3, {r2, r4}
	mov	r4, #0
	mov	r2, #128
	mov	r1, r4
	str	r4, [r6, #12]
	add	r0, r6, #64
	bl	memset
	mov	r1, r6
	mov	r0, #1
	bl	StorageSysDataStore
	str	r4, [r5, #2040]
	str	r4, [r5, #2036]
	b	.L3914
.L3906:
	ldr	r0, .L3955+104
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3918
	ldr	r2, [sp]
	ldr	r3, .L3955+108
	cmp	r2, r3
	bne	.L3915
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L3915
	ldr	r5, .L3955+48
	ldr	r3, [r5, #2040]
	cmp	r3, #1
	beq	.L3919
	ldr	r2, [r5, #2044]
	ldr	r3, .L3955+100
	ldr	r1, [r2]
	cmp	r1, r3
	beq	.L3922
	str	r3, [r2]
	mov	r2, #504
	ldr	r3, [r5, #2044]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3922:
	ldr	r1, [r5, #2044]
	mov	r3, #1
	mov	r0, #0
	mov	r4, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #2032]
	ldr	r2, .L3955+76
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	movne	r1, #504
	ldrne	r3, [r5, #2032]
	movne	r2, #0
	ldr	r6, [r5, #2032]
	stmibne	r3, {r1, r2}
	mov	r2, #128
	mov	r1, r4
	str	r4, [r6, #12]
	add	r0, r6, #64
	bl	memset
	mov	r1, r6
	mov	r0, #1
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #2040]
	b	.L3914
.L3928:
	ldr	r3, [r6, #2560]
	cmp	r3, #10
	bhi	.L3952
	ldr	r2, [r6, #2032]
	ldr	r1, [sp, #4]
	ldr	r3, [r2, #24]
	cmp	r3, r1
	cmpne	r3, #0
	movne	r3, #1
	moveq	r3, #0
	beq	.L3929
	ldr	r0, .L3955+112
	bl	printk
	ldr	r3, [r6, #2560]
	add	r3, r3, #1
	str	r3, [r6, #2560]
	b	.L3952
.L3929:
	cmp	r4, r7
	str	r3, [r6, #2560]
	movne	r3, #1
	strne	r1, [r2, #24]
	streq	r3, [r2, #20]
	mov	r1, r2
	streq	r3, [r2, #24]
	mov	r0, #1
	strne	r3, [r2, #20]
	bl	StorageSysDataStore
	cmn	r0, #1
	bne	.L3951
	mvn	r4, #1
	b	.L3914
.L3909:
	ldr	r0, .L3955+116
	bl	printk
	mov	r2, #520
	mov	r1, r5
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3918
	ldr	r2, [sp]
	ldr	r3, .L3955+60
	cmp	r2, r3
	bne	.L3915
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L3915
	ldr	r3, .L3955+48
	cmp	r4, r6
	ldreq	r1, [r3, #2564]
	ldrne	r1, [r3, #2568]
	add	r1, r1, #8
	b	.L3953
.L3934:
	ldr	r0, [r4, #2568]
	bl	memcpy
	ldr	r1, [r4, #2568]
	mov	r0, #3
	b	.L3950
.L3904:
	bl	rknand_dev_flush
	b	.L3951
.L3956:
	.align	2
.L3955:
	.word	1074031656
	.word	1074029694
	.word	.LC191
	.word	1094995539
	.word	.LANCHOR2+2048
	.word	1074034192
	.word	1074031666
	.word	.LC192
	.word	1074031676
	.word	.LC193
	.word	.LC194
	.word	1280262987
	.word	.LANCHOR2
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
	.word	.LC190
	.word	1112755781
	.word	.LC195
	.word	.LC196
	.fnend
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mvn	r3, #0
	ldr	r4, .L3967
	mov	r5, #0
	mov	r2, #512
	ldr	r1, [r4, #2004]
	add	r0, r4, #2048
	str	r3, [r4, #2028]
	strb	r5, [r4, #2020]
	add	r3, r1, #512
	str	r1, [r4, #2044]
	str	r3, [r4, #2032]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #2564]
	str	r5, [r4, #2024]
	str	r5, [r4, #2572]
	str	r1, [r4, #2568]
	bl	ftl_memcpy
	ldr	r6, [r4, #2044]
	str	r5, [r4, #2036]
	str	r5, [r4, #2560]
	ldr	r7, [r6, #508]
	ldr	r3, [r6, #16]
	cmp	r7, r5
	str	r3, [r4, #2040]
	beq	.L3958
	mov	r1, #508
	mov	r0, r6
	bl	js_hash
	cmp	r7, r0
	beq	.L3958
	str	r5, [r6, #16]
	ldr	r0, .L3967+4
	str	r5, [r4, #2040]
	bl	printk
.L3958:
	ldr	r3, [r4, #2040]
	mov	r0, #2
	ldr	r1, [r4, #2564]
	cmp	r3, #0
	ldrne	r3, .L3967+8
	strne	r3, [r4, #2036]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #2568]
	mov	r0, #3
	bl	StorageSysDataLoad
	pop	{r4, r5, r6, r7, r8, lr}
	b	rknand_sys_storage_init
.L3968:
	.align	2
.L3967:
	.word	.LANCHOR2
	.word	.LC199
	.word	-1067903959
	.fnend
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	2
	.global	StorageSysDataDeInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	StorageSysDataDeInit, %function
StorageSysDataDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r0, #0
	bx	lr
	.fnend
	.size	StorageSysDataDeInit, .-StorageSysDataDeInit
	.align	2
	.global	rk_ftl_vendor_storage_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_vendor_storage_init, %function
rk_ftl_vendor_storage_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r0, #65536
	ldr	r6, .L3980
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r6, #2576]
	beq	.L3976
	ldr	r10, .L3980+4
	mov	r7, #0
	ldr	r9, .L3980+8
	mov	r4, r7
	mov	r8, r7
.L3974:
	ldr	r2, [r6, #2576]
	mov	r1, #128
	lsl	r0, r8, #7
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L3972
	ldr	r1, [r6, #2576]
	mov	r0, r10
	add	r2, r1, #61440
	ldr	r3, [r1, #4]
	ldr	r2, [r2, #4092]
	ldr	r1, [r1]
	bl	printk
	ldr	r5, [r6, #2576]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L3973
	add	r2, r5, #61440
	ldr	r3, [r5, #4]
	ldr	r2, [r2, #4092]
	cmp	r3, r4
	sub	r2, r2, r3
	clz	r2, r2
	lsr	r2, r2, #5
	movls	r2, #0
	cmp	r2, #0
	movne	r7, r8
	movne	r4, r3
.L3973:
	add	r8, r8, #1
	cmp	r8, #2
	bne	.L3974
	cmp	r4, #0
	beq	.L3975
	mov	r2, r5
	mov	r1, #128
	lsl	r0, r7, #7
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L3972
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3975:
	mov	r2, #65536
	mov	r1, r4
	mov	r0, r5
	bl	memset
	mov	r3, #1
	add	r2, r5, #61440
	str	r3, [r5, #4]
	mov	r0, r4
	str	r9, [r5]
	str	r3, [r2, #4092]
	ldr	r3, .L3980+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3972:
	ldr	r0, [r6, #2576]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r6, #2576]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3976:
	mvn	r0, #11
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3981:
	.align	2
.L3980:
	.word	.LANCHOR2
	.word	.LC200
	.word	1380668996
	.word	-1032
	.fnend
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	2
	.global	rk_ftl_vendor_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3992
	ldr	ip, [r3, #2576]
	cmp	ip, #0
	beq	.L3987
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, #0
	ldrh	r4, [ip, #10]
.L3984:
	cmp	r3, r4
	bcc	.L3986
	mvn	r0, #0
	pop	{r4, r5, r6, pc}
.L3986:
	add	lr, ip, r3, lsl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L3985
	ldrh	r4, [lr, #20]
	mov	r0, r1
	ldrh	r1, [lr, #18]
	cmp	r4, r2
	movcs	r4, r2
	add	r1, r1, #1024
	mov	r2, r4
	add	r1, ip, r1
	bl	memcpy
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L3985:
	add	r3, r3, #1
	b	.L3984
.L3987:
	mvn	r0, #0
	bx	lr
.L3993:
	.align	2
.L3992:
	.word	.LANCHOR2
	.fnend
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	2
	.global	rk_ftl_vendor_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4015
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r3, #2576]
	cmp	r4, #0
	beq	.L4009
	mov	r8, r2
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r3, [r4, #8]
	mov	fp, r1
	bic	r6, r6, #63
	mov	r7, #0
	str	r3, [sp, #4]
.L3996:
	cmp	r7, r2
	bcc	.L4004
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L4009
	add	r3, r4, r2, lsl #3
	uxth	r6, r6
	strh	r0, [r3, #16]	@ movhi
	ldrh	r2, [r4, #12]
	strh	r8, [r3, #20]	@ movhi
	strh	r2, [r3, #18]	@ movhi
	add	r2, r2, r6
	sub	r6, r1, r6
	strh	r2, [r4, #12]	@ movhi
	strh	r6, [r4, #14]	@ movhi
	mov	r2, r8
	ldrh	r0, [r3, #18]
	mov	r1, fp
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	ldrh	r3, [r4, #10]
	add	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
	b	.L4014
.L4004:
	add	r5, r4, r7, lsl #3
	ldrh	r3, [r5, #16]
	cmp	r3, r0
	str	r3, [sp, #8]
	bne	.L3997
	ldrh	r1, [r5, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L3998
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	subls	r2, r2, #1
	ldrhls	r10, [r5, #18]
	strls	r2, [sp, #16]
	bls	.L3999
.L4009:
	mvn	r0, #0
	b	.L3994
.L4000:
	ldrh	r9, [r5, #20]
	add	r0, r3, r10
	ldrh	r2, [r5, #16]
	add	r7, r7, #1
	ldrh	r1, [r5, #18]
	strh	r9, [r5, #12]	@ movhi
	add	r9, r9, #63
	bic	r9, r9, #63
	strh	r2, [r5, #8]	@ movhi
	strh	r10, [r5, #10]	@ movhi
	add	r1, r3, r1
	mov	r2, r9
	str	r3, [sp, #20]
	bl	memcpy
	ldr	r3, [sp, #20]
	add	r10, r10, r9
.L3999:
	ldr	r2, [sp, #16]
	add	r5, r5, #8
	cmp	r7, r2
	bcc	.L4000
	ldrh	r2, [sp, #8]
	add	r7, r4, r7, lsl #3
	uxth	r5, r10
	uxtah	r0, r3, r10
	strh	r8, [r7, #20]	@ movhi
	strh	r2, [r7, #16]	@ movhi
	mov	r1, fp
	strh	r5, [r7, #18]	@ movhi
	mov	r2, r8
	bl	memcpy
	uxth	r3, r6
	ldrh	r6, [r4, #14]
	add	r5, r5, r3
	sub	r6, r6, r3
	ldr	r3, [sp, #12]
	strh	r5, [r4, #12]	@ movhi
	add	r6, r6, r3
	strh	r6, [r4, #14]	@ movhi
.L4014:
	ldr	r3, [r4, #4]
	add	r2, r4, #61440
	mov	r1, #128
	add	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r2, #4092]
	mov	r2, r4
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, [sp, #4]
	lsl	r0, r3, #7
	bl	FlashBootVendorWrite
	mov	r0, #0
.L3994:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3998:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, fp
	add	r0, r3, r0
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L4014
.L3997:
	add	r7, r7, #1
	b	.L3996
.L4016:
	.align	2
.L4015:
	.word	.LANCHOR2
	.fnend
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	2
	.global	rk_ftl_vendor_storage_ioctl
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_vendor_storage_ioctl, %function
rk_ftl_vendor_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #4096
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L4017
	ldr	r3, .L4033
	cmp	r6, r3
	beq	.L4020
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L4021
.L4031:
	mvn	r5, #13
	b	.L4019
.L4020:
	mov	r2, #8
	mov	r1, r5
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4031
	ldr	r2, [r4]
	ldr	r3, .L4033+4
	cmp	r2, r3
	beq	.L4023
.L4024:
	mvn	r5, #0
.L4019:
	mov	r0, r4
	bl	kfree
.L4017:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L4023:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L4024
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	subs	r5, r0, #0
	beq	.L4019
	b	.L4031
.L4021:
	mov	r2, #8
	mov	r1, r5
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4031
	ldr	r2, [r4]
	ldr	r3, .L4033+4
	cmp	r2, r3
	bne	.L4024
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4024
	add	r2, r2, #8
	mov	r1, r5
	mov	r0, r4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4031
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4019
.L4034:
	.align	2
.L4033:
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
	.set	.LANCHOR2,. + 8184
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
